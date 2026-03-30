#include "MRTextDocument.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mr {
namespace editor {

namespace {
constexpr std::size_t kLazyLineIndexStride = 4096;
}

struct MappedFileSource::State {
	int fd;
	const char *data;
	std::size_t size;
	std::string path;

	State() noexcept : fd(-1), data(nullptr), size(0), path() {
	}

	~State() {
		if (data != nullptr && size != 0)
			::munmap(const_cast<char *>(data), size);
		if (fd >= 0)
			::close(fd);
	}
};

bool Range::empty() const noexcept {
	return start == end;
}

Offset Range::length() const noexcept {
	return start <= end ? end - start : start - end;
}

void Range::normalize() noexcept {
	if (end < start)
		std::swap(start, end);
}

Range Range::normalized() const noexcept {
	Range result(*this);
	result.normalize();
	return result;
}

Range Range::clamped(Offset maxOffset) const noexcept {
	Range result(std::min(start, maxOffset), std::min(end, maxOffset));
	result.normalize();
	return result;
}

void Cursor::clamp(Offset maxOffset) noexcept {
	if (offset > maxOffset)
		offset = maxOffset;
}

bool Selection::empty() const noexcept {
	return anchor == cursor;
}

Range Selection::range() const noexcept {
	return Range(anchor, cursor).normalized();
}

void Selection::clamp(Offset maxOffset) noexcept {
	if (anchor > maxOffset)
		anchor = maxOffset;
	if (cursor > maxOffset)
		cursor = maxOffset;
}

bool TextSpan::empty() const noexcept {
	return length == 0;
}

Offset TextSpan::end() const noexcept {
	return start + length;
}

TextSpan TextSpan::clamped(Offset maxLength) const noexcept {
	if (start >= maxLength)
		return TextSpan(maxLength, 0);
	return TextSpan(start, std::min(length, maxLength - start));
}

bool Piece::empty() const noexcept {
	return span.empty();
}

bool MappedFileSource::openReadOnly(const std::string &path, std::string &error) {
	struct stat st;
	int fd = -1;
	void *mapping = MAP_FAILED;
	std::shared_ptr<State> state(new State());

	error.clear();
	fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0) {
		error = std::strerror(errno);
		return false;
	}
	if (::fstat(fd, &st) != 0) {
		error = std::strerror(errno);
		::close(fd);
		return false;
	}
	if (!S_ISREG(st.st_mode)) {
		error = "Only regular files can be memory-mapped.";
		::close(fd);
		return false;
	}

	state->fd = fd;
	state->path = path;
	state->size = static_cast<std::size_t>(st.st_size);
	if (state->size != 0) {
		mapping = ::mmap(nullptr, state->size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (mapping == MAP_FAILED) {
			error = std::strerror(errno);
			::close(fd);
			return false;
		}
		state->data = static_cast<const char *>(mapping);
	}

	state_ = state;
	return true;
}

void MappedFileSource::reset() noexcept {
	state_.reset();
}

std::size_t MappedFileSource::size() const noexcept {
	return state_ != nullptr ? state_->size : 0;
}

const char *MappedFileSource::data() const noexcept {
	return state_ != nullptr ? state_->data : nullptr;
}

const std::string &MappedFileSource::path() const noexcept {
	static const std::string emptyPath;
	return state_ != nullptr ? state_->path : emptyPath;
}

std::string MappedFileSource::sliceText(TextSpan span) const {
	TextSpan bounded = span.clamped(size());
	if (bounded.length == 0 || data() == nullptr)
		return std::string();
	return std::string(data() + bounded.start, bounded.length);
}

TextSpan AppendBuffer::append(const std::string &text) {
	TextSpan span(text_.size(), text.size());
	text_ += text;
	return span;
}

void AppendBuffer::clear() noexcept {
	text_.clear();
}

std::string AppendBuffer::sliceText(TextSpan span) const {
	TextSpan bounded = span.clamped(text_.size());
	return text_.substr(bounded.start, bounded.length);
}

TextSpan StagedAddBuffer::append(const std::string &text) {
	TextSpan span(text_.size(), text.size());
	text_ += text;
	return span;
}

void StagedAddBuffer::clear() noexcept {
	text_.clear();
}

std::string StagedAddBuffer::sliceText(TextSpan span) const {
	TextSpan bounded = span.clamped(text_.size());
	return text_.substr(bounded.start, bounded.length);
}

void EditTransaction::setText(const std::string &text) {
	operations_.push_back(EditOperation(EditKind::SetText, Range(), text));
}

void EditTransaction::insert(Offset offset, const std::string &text) {
	operations_.push_back(EditOperation(EditKind::Insert, Range(offset, offset), text));
}

void EditTransaction::erase(Range range) {
	range.normalize();
	operations_.push_back(EditOperation(EditKind::Erase, range, std::string()));
}

void EditTransaction::replace(Range range, const std::string &text) {
	range.normalize();
	operations_.push_back(EditOperation(EditKind::Replace, range, text));
}

void StagedEditTransaction::setText(const std::string &text) {
	operations_.push_back(
	    StagedEditOperation(EditKind::SetText, Range(), addBuffer_.append(text)));
}

void StagedEditTransaction::insert(Offset offset, const std::string &text) {
	operations_.push_back(
	    StagedEditOperation(EditKind::Insert, Range(offset, offset), addBuffer_.append(text)));
}

void StagedEditTransaction::erase(Range range) {
	range.normalize();
	operations_.push_back(StagedEditOperation(EditKind::Erase, range, TextSpan()));
}

void StagedEditTransaction::replace(Range range, const std::string &text) {
	range.normalize();
	operations_.push_back(
	    StagedEditOperation(EditKind::Replace, range, addBuffer_.append(text)));
}

TextDocument::TextDocument() noexcept
    : originalBuffer_(), mappedOriginal_(), addBuffer_(), pieces_(), length_(0), version_(0),
      cacheDirty_(false), materializedText_(), lineIndexCheckpoints_(), lazyIndexedOffset_(0),
      lazyIndexedLine_(0), lazyLineIndexComplete_(false), lazyTotalLineCount_(1) {
	resetLazyLineIndex();
}

TextDocument::TextDocument(const std::string &text)
    : originalBuffer_(), mappedOriginal_(), addBuffer_(), pieces_(), length_(0), version_(0),
      cacheDirty_(false), materializedText_(), lineIndexCheckpoints_(), lazyIndexedOffset_(0),
      lazyIndexedLine_(0), lazyLineIndexComplete_(false), lazyTotalLineCount_(1) {
	resetLazyLineIndex();
	initializeFromOriginal(text, true);
}

const std::string &TextDocument::text() const noexcept {
	ensureMaterialized();
	return materializedText_;
}

Offset TextDocument::length() const noexcept {
	return length_;
}

bool TextDocument::empty() const noexcept {
	return length_ == 0;
}

char TextDocument::charAt(Offset pos) const noexcept {
	if (const char *data = directTextData())
		return pos < length_ ? data[pos] : '\0';
	const std::string &flat = text();
	return pos < flat.size() ? flat[pos] : '\0';
}

Snapshot TextDocument::snapshot() const {
	return Snapshot(text(), version_);
}

bool TextDocument::loadMappedFile(const std::string &path, std::string &error) {
	MappedFileSource source;
	if (!source.openReadOnly(path, error))
		return false;
	initializeFromMappedSource(source, true);
	return true;
}

PieceChunkView TextDocument::pieceChunk(std::size_t index) const noexcept {
	if (index >= pieces_.size())
		return PieceChunkView();

	const Piece &piece = pieces_[index];
	if (piece.empty())
		return PieceChunkView();
	if (piece.source == BufferKind::Original) {
		const char *base = originalData();
		if (base == nullptr)
			return PieceChunkView();
		return PieceChunkView(base + piece.span.start, piece.span.length);
	}
	if (piece.span.start >= addBuffer_.size())
		return PieceChunkView();
	return PieceChunkView(addBuffer_.text().data() + piece.span.start, piece.span.length);
}

void TextDocument::setText(const std::string &text) {
	if (setTextNoVersionBump(text))
		bumpVersion();
}

void TextDocument::apply(const EditTransaction &transaction) {
	const std::vector<EditOperation> &ops = transaction.operations();
	bool mutated = false;
	for (std::size_t i = 0; i < ops.size(); ++i) {
		mutated = applyOperationNoVersionBump(ops[i]) || mutated;
	}
	if (mutated)
		bumpVersion();
}

CommitResult TextDocument::tryApply(const EditTransaction &transaction, std::size_t expectedVersion) {
	CommitResult result;
	result.expectedVersion = expectedVersion;
	result.actualVersion = version_;
	if (!matchesVersion(expectedVersion)) {
		result.status = CommitStatus::VersionConflict;
		return result;
	}

	const std::size_t oldVersion = version_;
	const Offset oldLength = length_;
	bool mutated = false;
	Offset touchStart = oldLength;
	Offset touchEnd = 0;
	bool touched = false;

	const std::vector<EditOperation> &ops = transaction.operations();
	for (std::size_t i = 0; i < ops.size(); ++i) {
		const EditOperation &op = ops[i];
		if (op.kind == EditKind::SetText) {
			touchStart = 0;
			touchEnd = std::max(oldLength, static_cast<Offset>(op.text.size()));
			touched = true;
		} else {
			Range range = op.range.normalized();
			Offset end = range.end;
			if (op.kind == EditKind::Insert)
				end = range.start + op.text.size();
			else if (op.kind == EditKind::Replace)
				end = std::max(range.end, static_cast<Offset>(range.start + op.text.size()));

			touchStart = touched ? std::min(touchStart, range.start) : range.start;
			touchEnd = touched ? std::max(touchEnd, end) : end;
			touched = true;
		}
		mutated = applyOperationNoVersionBump(op) || mutated;
	}

	if (!mutated) {
		result.status = CommitStatus::NoOp;
		return result;
	}

	bumpVersion();
	result.status = CommitStatus::Applied;
	result.actualVersion = version_;
	result.change.changed = true;
	result.change.oldVersion = oldVersion;
	result.change.newVersion = version_;
	result.change.oldLength = oldLength;
	result.change.newLength = length_;
	if (touched)
		result.change.touchedRange =
		    Range(touchStart, std::max(touchEnd, std::max(oldLength, length_))).normalized();
	return result;
}

CommitResult TextDocument::tryApply(const StagedEditTransaction &transaction) {
	CommitResult result;
	result.expectedVersion = transaction.baseVersion();
	result.actualVersion = version_;
	if (!matchesVersion(transaction.baseVersion())) {
		result.status = CommitStatus::VersionConflict;
		return result;
	}

	const std::size_t oldVersion = version_;
	const Offset oldLength = length_;
	bool mutated = false;
	Offset touchStart = oldLength;
	Offset touchEnd = 0;
	bool touched = false;

	const std::vector<StagedEditOperation> &ops = transaction.operations();
	for (std::size_t i = 0; i < ops.size(); ++i) {
		const StagedEditOperation &op = ops[i];
		if (op.kind == EditKind::SetText) {
			touchStart = 0;
			touchEnd = std::max(oldLength, op.span.length);
			touched = true;
		} else {
			Range range = op.range.normalized();
			Offset end = range.end;
			if (op.kind == EditKind::Insert)
				end = range.start + op.span.length;
			else if (op.kind == EditKind::Replace)
				end = std::max(range.end, static_cast<Offset>(range.start + op.span.length));

			touchStart = touched ? std::min(touchStart, range.start) : range.start;
			touchEnd = touched ? std::max(touchEnd, end) : end;
			touched = true;
		}
		mutated = applyStagedOperationNoVersionBump(op, transaction.buffer()) || mutated;
	}

	if (!mutated) {
		result.status = CommitStatus::NoOp;
		return result;
	}

	bumpVersion();
	result.status = CommitStatus::Applied;
	result.actualVersion = version_;
	result.change.changed = true;
	result.change.oldVersion = oldVersion;
	result.change.newVersion = version_;
	result.change.oldLength = oldLength;
	result.change.newLength = length_;
	if (touched)
		result.change.touchedRange =
		    Range(touchStart, std::max(touchEnd, std::max(oldLength, length_))).normalized();
	return result;
}

void TextDocument::insert(Offset offset, const std::string &text) {
	if (text.empty())
		return;
	if (insertAddSpanNoVersionBump(offset, addBuffer_.append(text)))
		bumpVersion();
}

void TextDocument::erase(Range range) {
	if (eraseNoVersionBump(range))
		bumpVersion();
}

void TextDocument::replace(Range range, const std::string &text) {
	if (replaceNoVersionBump(range, text))
		bumpVersion();
}

void TextDocument::insertFromStaged(Offset offset, const StagedAddBuffer &buffer, TextSpan span) {
	insert(offset, buffer.sliceText(span));
}

void TextDocument::replaceFromStaged(Range range, const StagedAddBuffer &buffer, TextSpan span) {
	replace(range, buffer.sliceText(span));
}

Offset TextDocument::clampOffset(Offset pos) const noexcept {
	return std::min(pos, length_);
}

std::size_t TextDocument::lineCount() const noexcept {
	if (const char *data = directTextData()) {
		static_cast<void>(data);
		ensureLazyIndexComplete();
		return lazyTotalLineCount_;
	}

	const std::string &flat = text();
	std::size_t lines = 1;
	for (Offset i = 0; i < flat.size(); ++i) {
		if (flat[i] == '\n')
			++lines;
		else if (flat[i] == '\r') {
			++lines;
			if (i + 1 < flat.size() && flat[i + 1] == '\n')
				++i;
		}
	}
	return lines;
}

Offset TextDocument::lineStart(Offset pos) const noexcept {
	if (const char *data = directTextData()) {
		pos = clampOffset(pos);
		while (pos > 0 && !isLineBreakChar(data[pos - 1]))
			--pos;
		return pos;
	}

	const std::string &flat = text();
	pos = clampOffset(pos);
	while (pos > 0 && !isLineBreakChar(flat[pos - 1]))
		--pos;
	return pos;
}

Offset TextDocument::lineEnd(Offset pos) const noexcept {
	if (const char *data = directTextData()) {
		pos = clampOffset(pos);
		while (pos < length_ && !isLineBreakChar(data[pos]))
			++pos;
		return pos;
	}

	const std::string &flat = text();
	pos = clampOffset(pos);
	while (pos < flat.size() && !isLineBreakChar(flat[pos]))
		++pos;
	return pos;
}

Offset TextDocument::nextLine(Offset pos) const noexcept {
	if (const char *data = directTextData()) {
		pos = lineEnd(pos);
		if (pos < length_) {
			if (data[pos] == '\r' && pos + 1 < length_ && data[pos + 1] == '\n')
				pos += 2;
			else
				++pos;
		}
		return pos;
	}

	const std::string &flat = text();
	pos = lineEnd(pos);
	if (pos < flat.size()) {
		if (flat[pos] == '\r' && pos + 1 < flat.size() && flat[pos + 1] == '\n')
			pos += 2;
		else
			++pos;
	}
	return pos;
}

Offset TextDocument::prevLine(Offset pos) const noexcept {
	if (const char *data = directTextData()) {
		pos = lineStart(pos);
		if (pos == 0)
			return 0;
		--pos;
		if (pos > 0 && data[pos - 1] == '\r' && data[pos] == '\n')
			--pos;
		while (pos > 0 && !isLineBreakChar(data[pos - 1]))
			--pos;
		return pos;
	}

	const std::string &flat = text();
	pos = lineStart(pos);
	if (pos == 0)
		return 0;
	--pos;
	if (pos > 0 && flat[pos - 1] == '\r' && flat[pos] == '\n')
		--pos;
	return lineStart(pos);
}

std::size_t TextDocument::lineIndex(Offset pos) const noexcept {
	if (const char *data = directTextData()) {
		pos = clampOffset(pos);
		static_cast<void>(data);
		ensureLazyIndexForOffset(pos);
		if (lineIndexCheckpoints_.empty())
			return 0;

		std::size_t left = 0;
		std::size_t right = lineIndexCheckpoints_.size();
		while (left < right) {
			std::size_t mid = left + (right - left) / 2;
			if (lineIndexCheckpoints_[mid].offset <= pos)
				left = mid + 1;
			else
				right = mid;
		}
		LineIndexCheckpoint checkpoint =
		    lineIndexCheckpoints_[left == 0 ? 0 : static_cast<std::size_t>(left - 1)];
		Offset cursor = checkpoint.offset;
		std::size_t line = checkpoint.lineIndex;
		while (cursor < pos) {
			Offset next = cursor;
			if (!directAdvanceLine(next) || next > pos)
				break;
			cursor = next;
			++line;
		}
		return line;
	}

	const std::string &flat = text();
	std::size_t line = 0;
	pos = clampOffset(pos);
	for (Offset i = 0; i < pos; ++i) {
		if (flat[i] == '\n')
			++line;
		else if (flat[i] == '\r') {
			++line;
			if (i + 1 < pos && flat[i + 1] == '\n')
				++i;
		}
	}
	return line;
}

Offset TextDocument::lineStartByIndex(std::size_t index) const noexcept {
	if (const char *data = directTextData()) {
		static_cast<void>(data);
		ensureLazyIndexForLine(index);
		if (lineIndexCheckpoints_.empty())
			return 0;
		if (lazyLineIndexComplete_ && index >= lazyTotalLineCount_)
			index = lazyTotalLineCount_ > 0 ? lazyTotalLineCount_ - 1 : 0;

		std::size_t left = 0;
		std::size_t right = lineIndexCheckpoints_.size();
		while (left < right) {
			std::size_t mid = left + (right - left) / 2;
			if (lineIndexCheckpoints_[mid].lineIndex <= index)
				left = mid + 1;
			else
				right = mid;
		}
		LineIndexCheckpoint checkpoint =
		    lineIndexCheckpoints_[left == 0 ? 0 : static_cast<std::size_t>(left - 1)];
		Offset cursor = checkpoint.offset;
		std::size_t line = checkpoint.lineIndex;
		while (line < index) {
			Offset next = cursor;
			if (!directAdvanceLine(next))
				break;
			cursor = next;
			++line;
		}
		return cursor;
	}

	std::size_t line = 0;
	Offset pos = 0;
	Offset len = length();
	while (line < index && pos < len) {
		Offset next = nextLine(pos);
		if (next <= pos)
			break;
		pos = next;
		++line;
	}
	return pos;
}

std::size_t TextDocument::estimatedLineCount() const noexcept {
	if (const char *data = directTextData()) {
		static_cast<void>(data);
		ensureLazyIndexSeeded();
		if (lazyLineIndexComplete_)
			return lazyTotalLineCount_;
		if (lazyIndexedOffset_ == 0 || lazyIndexedLine_ == 0)
			return std::max<std::size_t>(1, length_ / 80 + 1);

		const std::size_t observedLines = lazyIndexedLine_ + 1;
		const std::size_t estimated =
		    static_cast<std::size_t>((static_cast<long double>(length_) * observedLines) /
		                             std::max<Offset>(lazyIndexedOffset_, 1));
		return std::max<std::size_t>(observedLines, estimated);
	}

	return lineCount();
}

bool TextDocument::exactLineCountKnown() const noexcept {
	return !hasDirectOriginalView() || lazyLineIndexComplete_;
}

std::size_t TextDocument::column(Offset pos) const noexcept {
	pos = clampOffset(pos);
	return pos - lineStart(pos);
}

std::string TextDocument::lineText(Offset pos) const {
	Offset start = lineStart(pos);
	Offset end = lineEnd(pos);
	if (const char *data = directTextData())
		return std::string(data + start, end - start);
	return text().substr(start, end - start);
}

bool TextDocument::isLineBreakChar(char ch) const noexcept {
	return ch == '\n' || ch == '\r';
}

bool TextDocument::setTextNoVersionBump(const std::string &text) {
	if (text == this->text())
		return false;
	initializeFromOriginal(text, false);
	return true;
}

void TextDocument::initializeFromOriginal(const std::string &text, bool bumpVersionFlag) {
	originalBuffer_ = text;
	mappedOriginal_.reset();
	addBuffer_.clear();
	pieces_.clear();
	length_ = originalBuffer_.size();
	resetLazyLineIndex();
	if (!originalBuffer_.empty())
		pieces_.push_back(Piece(BufferKind::Original, TextSpan(0, originalBuffer_.size())));
	materializedText_ = originalBuffer_;
	cacheDirty_ = false;
	if (bumpVersionFlag)
		bumpVersion();
}

void TextDocument::initializeFromMappedSource(const MappedFileSource &source, bool bumpVersionFlag) {
	originalBuffer_.clear();
	mappedOriginal_ = source;
	addBuffer_.clear();
	pieces_.clear();
	length_ = mappedOriginal_.size();
	resetLazyLineIndex();
	materializedText_.clear();
	cacheDirty_ = length_ != 0;
	if (length_ != 0)
		pieces_.push_back(Piece(BufferKind::Original, TextSpan(0, length_)));
	if (bumpVersionFlag)
		bumpVersion();
}

void TextDocument::bumpVersion() noexcept {
	++version_;
}

void TextDocument::markDirty() noexcept {
	cacheDirty_ = true;
	resetLazyLineIndex();
}

void TextDocument::ensureMaterialized() const noexcept {
	if (!cacheDirty_)
		return;

	materializedText_.clear();
	materializedText_.reserve(length_);
	for (std::size_t i = 0; i < pieces_.size(); ++i)
		materializedText_ += pieceText(pieces_[i]);
	cacheDirty_ = false;
}

std::string TextDocument::pieceText(const Piece &piece) const {
	if (piece.source == BufferKind::Original)
		return mappedOriginal_.mapped() ? mappedOriginal_.sliceText(piece.span)
		                                : originalBuffer_.substr(piece.span.start, piece.span.length);
	return addBuffer_.sliceText(piece.span);
}

const char *TextDocument::originalData() const noexcept {
	return mappedOriginal_.mapped() ? mappedOriginal_.data() : originalBuffer_.data();
}

bool TextDocument::applyOperationNoVersionBump(const EditOperation &operation) {
	switch (operation.kind) {
		case EditKind::SetText:
			return setTextNoVersionBump(operation.text);
		case EditKind::Insert:
			if (operation.text.empty())
				return false;
			return insertAddSpanNoVersionBump(operation.range.start, addBuffer_.append(operation.text));
		case EditKind::Erase:
			return eraseNoVersionBump(operation.range);
		case EditKind::Replace:
			return replaceNoVersionBump(operation.range, operation.text);
	}
	return false;
}

bool TextDocument::applyStagedOperationNoVersionBump(const StagedEditOperation &operation,
                                                     const StagedAddBuffer &buffer) {
	switch (operation.kind) {
		case EditKind::SetText:
			return setTextNoVersionBump(buffer.sliceText(operation.span));
		case EditKind::Insert:
			if (operation.span.empty())
				return false;
			return insertAddSpanNoVersionBump(operation.range.start,
			                                 addBuffer_.append(buffer.sliceText(operation.span)));
		case EditKind::Erase:
			return eraseNoVersionBump(operation.range);
		case EditKind::Replace:
			return replaceNoVersionBump(operation.range, buffer.sliceText(operation.span));
	}
	return false;
}

std::size_t TextDocument::splitAt(Offset offset) {
	Offset logical = clampOffset(offset);
	Offset consumed = 0;

	if (logical == 0)
		return 0;
	if (logical >= length_)
		return pieces_.size();

	for (std::size_t i = 0; i < pieces_.size(); ++i) {
		Offset pieceLen = pieces_[i].span.length;
		Offset pieceEnd = consumed + pieceLen;

		if (logical == consumed)
			return i;
		if (logical == pieceEnd)
			return i + 1;
		if (consumed < logical && logical < pieceEnd) {
			Offset leftLen = logical - consumed;
			Piece left = pieces_[i];
			Piece right = pieces_[i];
			left.span.length = leftLen;
			right.span.start += leftLen;
			right.span.length -= leftLen;
			pieces_[i] = left;
			pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(i + 1), right);
			return i + 1;
		}
		consumed = pieceEnd;
	}

	return pieces_.size();
}

bool TextDocument::eraseNoVersionBump(Range range) {
	Range bounded = range.clamped(length_);
	if (bounded.empty())
		return false;

	std::size_t startIndex = splitAt(bounded.start);
	std::size_t endIndex = splitAt(bounded.end);

	if (startIndex < endIndex)
		pieces_.erase(pieces_.begin() + static_cast<std::ptrdiff_t>(startIndex),
		              pieces_.begin() + static_cast<std::ptrdiff_t>(endIndex));
	length_ -= bounded.end - bounded.start;
	compactPieces();
	markDirty();
	return true;
}

bool TextDocument::replaceNoVersionBump(Range range, const std::string &text) {
	Range bounded = range.clamped(length_);
	bool removed = eraseNoVersionBump(bounded);
	if (text.empty())
		return removed;

	return insertAddSpanNoVersionBump(bounded.start, addBuffer_.append(text)) || removed;
}

bool TextDocument::hasDirectOriginalView() const noexcept {
	return mappedOriginal_.mapped() && addBuffer_.size() == 0 && pieces_.size() == 1 &&
	       pieces_[0].source == BufferKind::Original && pieces_[0].span.start == 0 &&
	       pieces_[0].span.length == length_;
}

const char *TextDocument::directTextData() const noexcept {
	return hasDirectOriginalView() ? mappedOriginal_.data() : nullptr;
}

void TextDocument::resetLazyLineIndex() noexcept {
	lineIndexCheckpoints_.clear();
	lineIndexCheckpoints_.push_back(LineIndexCheckpoint(0, 0));
	lazyIndexedOffset_ = 0;
	lazyIndexedLine_ = 0;
	lazyLineIndexComplete_ = (length_ == 0);
	lazyTotalLineCount_ = 1;
}

bool TextDocument::directAdvanceLine(Offset &offset) const noexcept {
	const char *data = directTextData();
	if (data == nullptr)
		return false;
	if (offset >= length_)
		return false;

	while (offset < length_ && !isLineBreakChar(data[offset]))
		++offset;
	if (offset >= length_)
		return false;
	if (data[offset] == '\r' && offset + 1 < length_ && data[offset + 1] == '\n')
		offset += 2;
	else
		++offset;
	return true;
}

void TextDocument::ensureLazyIndexSeeded() const noexcept {
	if (!hasDirectOriginalView())
		return;
	if (lineIndexCheckpoints_.empty())
		const_cast<TextDocument *>(this)->resetLazyLineIndex();
}

void TextDocument::advanceLazyIndexByStride() const noexcept {
	ensureLazyIndexSeeded();
	if (!hasDirectOriginalView() || lazyLineIndexComplete_)
		return;

	for (std::size_t steps = 0; steps < kLazyLineIndexStride; ++steps) {
		Offset next = lazyIndexedOffset_;
		if (!directAdvanceLine(next)) {
			lazyLineIndexComplete_ = true;
			lazyTotalLineCount_ = lazyIndexedLine_ + 1;
			return;
		}
		lazyIndexedOffset_ = next;
		++lazyIndexedLine_;
		if ((lazyIndexedLine_ % kLazyLineIndexStride) == 0)
			lineIndexCheckpoints_.push_back(LineIndexCheckpoint(lazyIndexedOffset_, lazyIndexedLine_));
	}
}

void TextDocument::ensureLazyIndexForLine(std::size_t targetLine) const noexcept {
	ensureLazyIndexSeeded();
	if (!hasDirectOriginalView())
		return;
	while (!lazyLineIndexComplete_ && lineIndexCheckpoints_.back().lineIndex < targetLine)
		advanceLazyIndexByStride();
}

void TextDocument::ensureLazyIndexForOffset(Offset targetOffset) const noexcept {
	ensureLazyIndexSeeded();
	if (!hasDirectOriginalView())
		return;
	targetOffset = clampOffset(targetOffset);
	while (!lazyLineIndexComplete_ && lineIndexCheckpoints_.back().offset <= targetOffset)
		advanceLazyIndexByStride();
}

void TextDocument::ensureLazyIndexComplete() const noexcept {
	ensureLazyIndexSeeded();
	if (!hasDirectOriginalView())
		return;
	while (!lazyLineIndexComplete_)
		advanceLazyIndexByStride();
}

bool TextDocument::insertAddSpanNoVersionBump(Offset offset, TextSpan span) {
	Offset logical = clampOffset(offset);
	std::size_t index = splitAt(logical);

	if (!span.empty())
		pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(index),
		              Piece(BufferKind::Add, span));
	else
		return false;

	length_ += span.length;
	compactPieces();
	markDirty();
	return true;
}

void TextDocument::compactPieces() {
	std::vector<Piece> compacted;
	compacted.reserve(pieces_.size());

	for (std::size_t i = 0; i < pieces_.size(); ++i) {
		const Piece &piece = pieces_[i];
		if (piece.empty())
			continue;
		if (!compacted.empty() && compacted.back().source == piece.source &&
		    compacted.back().span.end() == piece.span.start) {
			compacted.back().span.length += piece.span.length;
		} else
			compacted.push_back(piece);
	}

	pieces_.swap(compacted);
}

} // namespace editor
} // namespace mr
