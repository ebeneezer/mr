#include "MRTextDocument.hpp"
#include "MRTextDocumentLineIndex.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <sstream>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mr {
namespace editor {

using lineindex::appendDocumentTrace;
using lineindex::kSlowDocumentTraceThreshold;
using lineindex::traceMicros;

namespace {
std::size_t allocateDocumentId() noexcept {
	static std::atomic<std::size_t> nextId(1);
	return nextId.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

struct MappedFileSource::State {
	int fd;
	const char *data;
	std::size_t size;
	std::string path;

	State() noexcept : fd(-1), data(nullptr), size(0) {
	}

	~State() {
		if (data != nullptr && size != 0) ::munmap(const_cast<char *>(data), size);
		if (fd >= 0) ::close(fd);
	}
};

bool Range::empty() const noexcept {
	return start == end;
}

Offset Range::length() const noexcept {
	return start <= end ? end - start : start - end;
}

void Range::normalize() noexcept {
	if (end < start) std::swap(start, end);
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
	if (offset > maxOffset) offset = maxOffset;
}

bool Selection::empty() const noexcept {
	return anchor == cursor;
}

Range Selection::range() const noexcept {
	return Range(anchor, cursor).normalized();
}

void Selection::clamp(Offset maxOffset) noexcept {
	if (anchor > maxOffset) anchor = maxOffset;
	if (cursor > maxOffset) cursor = maxOffset;
}

bool TextSpan::empty() const noexcept {
	return length == 0;
}

Offset TextSpan::end() const noexcept {
	return start + length;
}

TextSpan TextSpan::clamped(Offset maxLength) const noexcept {
	if (start >= maxLength) return TextSpan(maxLength, 0);
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

	mState = state;
	return true;
}

void MappedFileSource::reset() noexcept {
	mState.reset();
}

std::size_t MappedFileSource::size() const noexcept {
	return mState != nullptr ? mState->size : 0;
}

const char *MappedFileSource::data() const noexcept {
	return mState != nullptr ? mState->data : nullptr;
}

const std::string &MappedFileSource::path() const noexcept {
	static const std::string emptyPath;
	return mState != nullptr ? mState->path : emptyPath;
}

std::string MappedFileSource::sliceText(TextSpan span) const {
	TextSpan bounded = span.clamped(size());
	if (bounded.length == 0 || data() == nullptr) return std::string();
	return std::string(data() + bounded.start, bounded.length);
}

void AppendBuffer::ensureUnique() {
	if (!mText) {
		mText = std::make_shared<std::string>();
		return;
	}
	if (!mText.unique()) mText = std::make_shared<std::string>(*mText);
}

TextSpan AppendBuffer::append(std::string_view text) {
	ensureUnique();
	TextSpan span(mText->size(), text.size());
	mText->append(text.data(), text.size());
	return span;
}

void AppendBuffer::clear() noexcept {
	if (!mText) {
		mText = std::make_shared<std::string>();
		return;
	}
	if (mText.unique()) mText->clear();
	else
		mText = std::make_shared<std::string>();
}

void AppendBuffer::setSharedText(const std::shared_ptr<const std::string> &text) noexcept {
	mText = text != nullptr ? std::const_pointer_cast<std::string>(text) : std::make_shared<std::string>();
}

std::string AppendBuffer::sliceText(TextSpan span) const {
	TextSpan bounded = span.clamped(size());
	return mText != nullptr ? mText->substr(bounded.start, bounded.length) : std::string();
}

TextSpan StagedAddBuffer::append(std::string_view text) {
	TextSpan span(mText.size(), text.size());
	mText.append(text.data(), text.size());
	return span;
}

void StagedAddBuffer::clear() noexcept {
	mText.clear();
}

std::string StagedAddBuffer::sliceText(TextSpan span) const {
	TextSpan bounded = span.clamped(mText.size());
	return mText.substr(bounded.start, bounded.length);
}

void EditTransaction::setText(std::string_view text) {
	mOperations.push_back(EditOperation(EditKind::SetText, Range(), text));
}

void EditTransaction::insert(Offset offset, std::string_view text) {
	mOperations.push_back(EditOperation(EditKind::Insert, Range(offset, offset), text));
}

void EditTransaction::erase(Range range) {
	range.normalize();
	mOperations.push_back(EditOperation(EditKind::Erase, range, std::string()));
}

void EditTransaction::replace(Range range, std::string_view text) {
	range.normalize();
	mOperations.push_back(EditOperation(EditKind::Replace, range, text));
}

void StagedEditTransaction::setText(std::string_view text) {
	mOperations.push_back(StagedEditOperation(EditKind::SetText, Range(), mAddBuffer.append(text)));
}

void StagedEditTransaction::insert(Offset offset, std::string_view text) {
	mOperations.push_back(StagedEditOperation(EditKind::Insert, Range(offset, offset), mAddBuffer.append(text)));
}

void StagedEditTransaction::erase(Range range) {
	range.normalize();
	mOperations.push_back(StagedEditOperation(EditKind::Erase, range, TextSpan()));
}

void StagedEditTransaction::replace(Range range, std::string_view text) {
	range.normalize();
	mOperations.push_back(StagedEditOperation(EditKind::Replace, range, mAddBuffer.append(text)));
}

StagedEditTransaction::StagedEditTransaction(const ReadSnapshot &snapshot, std::string_view label) : mBaseVersion(snapshot.version()), mLabel(label) {
}

TextDocument::TextDocument() noexcept : mOriginalBuffer(std::make_shared<std::string>()), mLength(0), mDocumentId(allocateDocumentId()), mVersion(0), mCacheDirty(false), mLazyIndexedOffset(0), mLazyIndexedLine(0), mLazyLineIndexComplete(false), mLazyTotalLineCount(1) {
	mPieces = std::make_shared<std::vector<Piece>>();
	resetLazyLineIndex();
	mEditedLineStarts = std::make_shared<std::vector<Offset>>(1, 0);
}

TextDocument::TextDocument(std::string_view text) : mOriginalBuffer(std::make_shared<std::string>()), mLength(0), mDocumentId(allocateDocumentId()), mVersion(0), mCacheDirty(false), mLazyIndexedOffset(0), mLazyIndexedLine(0), mLazyLineIndexComplete(false), mLazyTotalLineCount(1) {
	mPieces = std::make_shared<std::vector<Piece>>();
	resetLazyLineIndex();
	mEditedLineStarts = std::make_shared<std::vector<Offset>>(1, 0);
	initializeFromOriginal(text, true);
}

const std::string &TextDocument::text() const noexcept {
	ensureMaterialized();
	return mMaterializedText;
}

Offset TextDocument::length() const noexcept {
	return mLength;
}

bool TextDocument::empty() const noexcept {
	return mLength == 0;
}

Snapshot TextDocument::snapshot() const {
	return Snapshot(text(), mVersion);
}

bool TextDocument::loadMappedFile(const std::string &path, std::string &error) {
	MappedFileSource source;
	if (!source.openReadOnly(path, error)) return false;
	initializeFromMappedSource(source, true);
	return true;
}

PieceChunkView TextDocument::pieceChunk(std::size_t index) const noexcept {
	if (mPieces == nullptr || index >= mPieces->size()) return PieceChunkView();

	const Piece &piece = (*mPieces)[index];
	if (piece.empty()) return PieceChunkView();
	if (piece.source == BufferKind::Original) {
		const char *base = originalData();
		if (base == nullptr) return PieceChunkView();
		return PieceChunkView(base + piece.span.start, piece.span.length);
	}
	if (piece.span.start >= mAddBuffer.size()) return PieceChunkView();
	return PieceChunkView(mAddBuffer.text().data() + piece.span.start, piece.span.length);
}

void TextDocument::setText(std::string_view text) {
	if (setTextNoVersionBump(text)) bumpVersion();
}

void TextDocument::apply(const EditTransaction &transaction) {
	const std::vector<EditOperation> &ops = transaction.operations();
	bool mutated = false;
	for (const auto &op : ops) {
		mutated = applyOperationNoVersionBump(op) || mutated;
	}
	if (mutated) bumpVersion();
}

CommitResult TextDocument::tryApply(const EditTransaction &transaction, std::size_t expectedVersion) {
	const auto startedAt = std::chrono::steady_clock::now();
	CommitResult result;
	result.expectedVersion = expectedVersion;
	result.actualVersion = mVersion;
	if (!matchesVersion(expectedVersion)) {
		result.status = CommitStatus::VersionConflict;
		return result;
	}

	const std::size_t oldVersion = mVersion;
	const Offset oldLength = mLength;
	bool mutated = false;
	Offset touchStart = oldLength;
	Offset touchEnd = 0;
	bool touched = false;

	const std::vector<EditOperation> &ops = transaction.operations();
	for (const auto &op : ops) {
		if (op.kind == EditKind::SetText) {
			touchStart = 0;
			touchEnd = std::max(oldLength, static_cast<Offset>(op.text.size()));
			touched = true;
		} else {
			Range range = op.range.normalized();
			Offset end = range.end;
			if (op.kind == EditKind::Insert) end = range.start + op.text.size();
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
	result.actualVersion = mVersion;
	result.change.changed = true;
	result.change.oldVersion = oldVersion;
	result.change.newVersion = mVersion;
	result.change.oldLength = oldLength;
	result.change.newLength = mLength;
	if (touched) result.change.touchedRange = Range(touchStart, touchEnd).normalized();
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc tryApply total_us=" << traceMicros(totalElapsed) << " ops=" << ops.size() << " old_len=" << oldLength << " new_len=" << mLength << " add=" << mAddBuffer.size()
		     << " pieces=" << pieceCount() << " exact_lines=" << (mEditedLineStarts != nullptr ? mEditedLineStarts->size() : 0) << " lazy_checkpoints=" << mLineIndexCheckpoints.size();
		if (touched) line << " touch_start=" << touchStart << " touch_end=" << touchEnd;
		appendDocumentTrace(line.str());
	}
	return result;
}

CommitResult TextDocument::tryApply(const StagedEditTransaction &transaction) {
	const auto startedAt = std::chrono::steady_clock::now();
	CommitResult result;
	result.expectedVersion = transaction.baseVersion();
	result.actualVersion = mVersion;
	if (!matchesVersion(transaction.baseVersion())) {
		result.status = CommitStatus::VersionConflict;
		return result;
	}

	const std::size_t oldVersion = mVersion;
	const Offset oldLength = mLength;
	bool mutated = false;
	Offset touchStart = oldLength;
	Offset touchEnd = 0;
	bool touched = false;

	const std::vector<StagedEditOperation> &ops = transaction.operations();
	for (const auto &op : ops) {
		if (op.kind == EditKind::SetText) {
			touchStart = 0;
			touchEnd = std::max(oldLength, op.span.length);
			touched = true;
		} else {
			Range range = op.range.normalized();
			Offset end = range.end;
			if (op.kind == EditKind::Insert) end = range.start + op.span.length;
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
	result.actualVersion = mVersion;
	result.change.changed = true;
	result.change.oldVersion = oldVersion;
	result.change.newVersion = mVersion;
	result.change.oldLength = oldLength;
	result.change.newLength = mLength;
	if (touched) result.change.touchedRange = Range(touchStart, touchEnd).normalized();
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc tryApplyStaged total_us=" << traceMicros(totalElapsed) << " ops=" << ops.size() << " old_len=" << oldLength << " new_len=" << mLength << " add=" << mAddBuffer.size()
		     << " pieces=" << pieceCount() << " exact_lines=" << (mEditedLineStarts != nullptr ? mEditedLineStarts->size() : 0) << " lazy_checkpoints=" << mLineIndexCheckpoints.size();
		if (touched) line << " touch_start=" << touchStart << " touch_end=" << touchEnd;
		appendDocumentTrace(line.str());
	}
	return result;
}

void TextDocument::insert(Offset offset, std::string_view text) {
	if (text.empty()) return;
	if (insertAddSpanNoVersionBump(offset, mAddBuffer.append(text))) bumpVersion();
}

void TextDocument::erase(Range range) {
	if (eraseNoVersionBump(range)) bumpVersion();
}

void TextDocument::replace(Range range, std::string_view text) {
	if (replaceNoVersionBump(range, text)) bumpVersion();
}

void TextDocument::insertFromStaged(Offset offset, const StagedAddBuffer &buffer, TextSpan span) {
	insert(offset, buffer.sliceText(span));
}

void TextDocument::replaceFromStaged(Range range, const StagedAddBuffer &buffer, TextSpan span) {
	replace(range, buffer.sliceText(span));
}

void TextDocument::flatten() {
	if ((mPieces == nullptr || mPieces->size() <= 1) && mAddBuffer.size() == 0 && !mMappedOriginal.mapped()) return;

	std::string currentText = text();
	mOriginalBuffer = std::make_shared<std::string>(std::move(currentText));
	mMappedOriginal.reset();
	mAddBuffer.clear();

	ensureUniquePieces();
	mPieces->clear();
	mPieces->emplace_back(BufferKind::Original, TextSpan(0, mOriginalBuffer->length()));
	mLength = mOriginalBuffer->length();

	markDirty();
	bumpVersion();
}

bool TextDocument::setTextNoVersionBump(std::string_view text) {
	if (text == this->text()) return false;
	initializeFromOriginal(text, false);
	return true;
}

void TextDocument::bumpVersion() noexcept {
	++mVersion;
}

void TextDocument::markDirty() noexcept {
	mCacheDirty = true;
}

void TextDocument::ensureUniqueOriginalBuffer() {
	if (!mOriginalBuffer) {
		mOriginalBuffer = std::make_shared<std::string>();
		return;
	}
	if (!mOriginalBuffer.unique()) mOriginalBuffer = std::make_shared<std::string>(*mOriginalBuffer);
}

void TextDocument::ensureUniquePieces() {
	if (!mPieces) {
		mPieces = std::make_shared<std::vector<Piece>>();
		return;
	}
	if (!mPieces.unique()) mPieces = std::make_shared<std::vector<Piece>>(*mPieces);
}

void TextDocument::ensureMaterialized() const noexcept {
	if (!mCacheDirty) return;

	mMaterializedText.clear();
	mMaterializedText.reserve(mLength);
	if (mPieces != nullptr)
		for (const auto &piece : *mPieces)
			mMaterializedText += pieceText(piece);
	mCacheDirty = false;
}

std::string TextDocument::pieceText(const Piece &piece) const {
	if (piece.source == BufferKind::Original) return mMappedOriginal.mapped() ? mMappedOriginal.sliceText(piece.span) : (mOriginalBuffer != nullptr ? mOriginalBuffer->substr(piece.span.start, piece.span.length) : std::string());
	return mAddBuffer.sliceText(piece.span);
}

const char *TextDocument::originalData() const noexcept {
	return mMappedOriginal.mapped() ? mMappedOriginal.data() : (mOriginalBuffer != nullptr ? mOriginalBuffer->data() : nullptr);
}

bool TextDocument::applyOperationNoVersionBump(const EditOperation &operation) {
	switch (operation.kind) {
		case EditKind::SetText:
			return setTextNoVersionBump(operation.text);
		case EditKind::Insert:
			if (operation.text.empty()) return false;
			return insertAddSpanNoVersionBump(operation.range.start, mAddBuffer.append(operation.text));
		case EditKind::Erase:
			return eraseNoVersionBump(operation.range);
		case EditKind::Replace:
			return replaceNoVersionBump(operation.range, operation.text);
	}
	return false;
}

bool TextDocument::applyStagedOperationNoVersionBump(const StagedEditOperation &operation, const StagedAddBuffer &buffer) {
	switch (operation.kind) {
		case EditKind::SetText:
			return setTextNoVersionBump(buffer.sliceText(operation.span));
		case EditKind::Insert:
			if (operation.span.empty()) return false;
			return insertAddSpanNoVersionBump(operation.range.start, mAddBuffer.append(buffer.sliceText(operation.span)));
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

	if (logical == 0) return 0;
	if (mPieces == nullptr || logical >= mLength) return pieceCount();

	ensureUniquePieces();

	for (std::size_t i = 0; i < mPieces->size(); ++i) {
		Offset pieceLen = (*mPieces)[i].span.length;
		Offset pieceEnd = consumed + pieceLen;

		if (logical == consumed) return i;
		if (logical == pieceEnd) return i + 1;
		if (consumed < logical && logical < pieceEnd) {
			Offset leftLen = logical - consumed;
			Piece left = (*mPieces)[i];
			Piece right = (*mPieces)[i];
			left.span.length = leftLen;
			right.span.start += leftLen;
			right.span.length -= leftLen;
			(*mPieces)[i] = left;
			mPieces->insert(mPieces->begin() + static_cast<std::ptrdiff_t>(i + 1), right);
			return i + 1;
		}
		consumed = pieceEnd;
	}

	return mPieces->size();
}

bool TextDocument::eraseNoVersionBump(Range range) {
	const auto startedAt = std::chrono::steady_clock::now();
	Range bounded = range.clamped(mLength);
	if (bounded.empty()) return false;

	std::chrono::steady_clock::duration splitElapsed{};
	std::chrono::steady_clock::duration eraseElapsed{};
	std::chrono::steady_clock::duration compactElapsed{};
	std::chrono::steady_clock::duration invalidateElapsed{};
	std::chrono::steady_clock::duration lineIndexElapsed{};
	const std::size_t piecesBefore = pieceCount();
	const std::size_t exactLinesBefore = hasEditedLineStartIndex() ? mEditedLineStarts->size() : 0;
	std::size_t startIndex = splitAt(bounded.start);
	std::size_t endIndex = splitAt(bounded.end);
	splitElapsed = std::chrono::steady_clock::now() - startedAt;

	{
		const auto eraseStartedAt = std::chrono::steady_clock::now();
		if (startIndex < endIndex) mPieces->erase(mPieces->begin() + static_cast<std::ptrdiff_t>(startIndex), mPieces->begin() + static_cast<std::ptrdiff_t>(endIndex));
		eraseElapsed = std::chrono::steady_clock::now() - eraseStartedAt;
	}
	mLength -= bounded.end - bounded.start;
	{
		const auto compactStartedAt = std::chrono::steady_clock::now();
		compactPieces();
		compactElapsed = std::chrono::steady_clock::now() - compactStartedAt;
	}
	{
		const auto invalidateStartedAt = std::chrono::steady_clock::now();
		invalidateLazyLineIndexFrom(bounded.start);
		invalidateElapsed = std::chrono::steady_clock::now() - invalidateStartedAt;
	}
	{
		const auto lineIndexStartedAt = std::chrono::steady_clock::now();
		if (hasEditedLineStartIndex()) updateEditedLineStartIndexForErase(bounded);
		lineIndexElapsed = std::chrono::steady_clock::now() - lineIndexStartedAt;
	}
	markDirty();
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc eraseNoVersionBump total_us=" << traceMicros(totalElapsed) << " split_us=" << traceMicros(splitElapsed) << " erase_us=" << traceMicros(eraseElapsed)
		     << " compact_us=" << traceMicros(compactElapsed) << " invalidate_us=" << traceMicros(invalidateElapsed) << " line_index_us=" << traceMicros(lineIndexElapsed)
		     << " start=" << bounded.start << " end=" << bounded.end << " erased_bytes=" << bounded.length() << " pieces_before=" << piecesBefore << " pieces_after=" << pieceCount()
		     << " exact_lines_before=" << exactLinesBefore << " exact_lines_after=" << (hasEditedLineStartIndex() ? mEditedLineStarts->size() : 0) << " len=" << mLength
		     << " add=" << mAddBuffer.size();
		appendDocumentTrace(line.str());
	}
	return true;
}

bool TextDocument::replaceNoVersionBump(Range range, std::string_view text) {
	const auto startedAt = std::chrono::steady_clock::now();
	Range bounded = range.clamped(mLength);
	const auto eraseStartedAt = std::chrono::steady_clock::now();
	bool removed = eraseNoVersionBump(bounded);
	const auto eraseElapsed = std::chrono::steady_clock::now() - eraseStartedAt;
	bool inserted = false;
	std::chrono::steady_clock::duration insertElapsed{};
	if (!text.empty()) {
		const auto insertStartedAt = std::chrono::steady_clock::now();
		inserted = insertAddSpanNoVersionBump(bounded.start, mAddBuffer.append(text));
		insertElapsed = std::chrono::steady_clock::now() - insertStartedAt;
	}
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc replaceNoVersionBump total_us=" << traceMicros(totalElapsed) << " erase_us=" << traceMicros(eraseElapsed) << " insert_us=" << traceMicros(insertElapsed)
		     << " start=" << bounded.start << " end=" << bounded.end << " removed_bytes=" << bounded.length() << " inserted_bytes=" << text.size() << " len=" << mLength
		     << " add=" << mAddBuffer.size() << " pieces=" << pieceCount() << " exact_lines=" << (hasEditedLineStartIndex() ? mEditedLineStarts->size() : 0);
		appendDocumentTrace(line.str());
	}
	return inserted || removed;
}

bool TextDocument::insertAddSpanNoVersionBump(Offset offset, TextSpan span) {
	const auto startedAt = std::chrono::steady_clock::now();
	Offset logical = clampOffset(offset);
	const auto splitStartedAt = std::chrono::steady_clock::now();
	std::size_t index = splitAt(logical);
	const auto splitElapsed = std::chrono::steady_clock::now() - splitStartedAt;

	if (!span.empty()) mPieces->insert(mPieces->begin() + static_cast<std::ptrdiff_t>(index), Piece(BufferKind::Add, span));
	else
		return false;

	mLength += span.length;
	const auto compactStartedAt = std::chrono::steady_clock::now();
	compactPieces();
	const auto compactElapsed = std::chrono::steady_clock::now() - compactStartedAt;
	std::string_view inserted(mAddBuffer.text().data() + span.start, span.length);
	bool insertedLineBreak = false;
	for (char ch : inserted) {
		if (isLineBreakChar(ch)) {
			insertedLineBreak = true;
			break;
		}
	}
	const auto invalidateStartedAt = std::chrono::steady_clock::now();
	if (insertedLineBreak) invalidateLazyLineIndexFrom(logical);
	else
		shiftLazyLineIndexForInsertWithoutLineBreak(logical, span.length);
	const auto invalidateElapsed = std::chrono::steady_clock::now() - invalidateStartedAt;
	const auto indexStartedAt = std::chrono::steady_clock::now();
	if (hasEditedLineStartIndex()) updateEditedLineStartIndexForInsert(logical, inserted);
	const auto indexElapsed = std::chrono::steady_clock::now() - indexStartedAt;
	markDirty();
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc insertAddSpanNoVersionBump total_us=" << traceMicros(totalElapsed) << " split_us=" << traceMicros(splitElapsed) << " compact_us=" << traceMicros(compactElapsed)
		     << " invalidate_us=" << traceMicros(invalidateElapsed) << " line_index_us=" << traceMicros(indexElapsed) << " offset=" << offset << " logical=" << logical << " bytes=" << span.length
		     << " len=" << mLength << " add=" << mAddBuffer.size() << " pieces=" << pieceCount() << " exact_lines=" << (mEditedLineStarts != nullptr ? mEditedLineStarts->size() : 0);
		appendDocumentTrace(line.str());
	}
	return true;
}

void TextDocument::compactPieces() {
	std::vector<Piece> compacted;
	compacted.reserve(pieceCount());

	if (mPieces != nullptr)
		for (const auto &piece : *mPieces) {
			if (piece.empty()) continue;
			if (!compacted.empty() && compacted.back().source == piece.source && compacted.back().span.end() == piece.span.start) {
				compacted.back().span.length += piece.span.length;
			} else
				compacted.push_back(piece);
		}

	ensureUniquePieces();
	mPieces->swap(compacted);
}

} // namespace editor
} // namespace mr
