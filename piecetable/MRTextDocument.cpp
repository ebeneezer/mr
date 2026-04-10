#include "MRTextDocument.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <limits>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
#include <emmintrin.h>
#endif

namespace mr {
namespace editor {

namespace {
constexpr std::size_t kLazyLineIndexStride = 4096;

std::size_t allocateDocumentId() noexcept {
	static std::atomic<std::size_t> nextId(1);
	return nextId.fetch_add(1, std::memory_order_relaxed);
}

template <class Doc>
char piecewiseCharAt(const Doc &doc, Offset pos) noexcept {
	pos = doc.clampOffset(pos);
	if (pos >= doc.length())
		return '\0';
	Offset logical = 0;
	for (std::size_t i = 0; i < doc.pieceCount(); ++i) {
		PieceChunkView chunk = doc.pieceChunk(i);
		if (chunk.data == nullptr || chunk.length == 0)
			continue;
		if (pos < logical + chunk.length)
			return chunk.data[pos - logical];
		logical += chunk.length;
	}
	return '\0';
}

inline bool isLineBreakByte(char ch) noexcept {
	return ch == '\n' || ch == '\r';
}

inline void appendLineCheckpoint(std::vector<LineIndexCheckpoint> &checkpoints, Offset lineStart,
                                 std::size_t lineIndex) {
	if ((lineIndex % kLazyLineIndexStride) == 0)
		checkpoints.push_back(LineIndexCheckpoint(lineStart, lineIndex));
}

inline void applyLineBreakAt(const char *data, Offset length, Offset breakOffset, Offset &lineStart,
                             std::size_t &lineIndex, std::vector<LineIndexCheckpoint> &checkpoints,
                             Offset &skipLfAt) {
	const char ch = data[breakOffset];
	Offset nextLineStart = breakOffset + 1;

	if (ch == '\r' && breakOffset + 1 < length && data[breakOffset + 1] == '\n') {
		nextLineStart = breakOffset + 2;
		skipLfAt = breakOffset + 1;
	}
	lineStart = nextLineStart;
	++lineIndex;
	appendLineCheckpoint(checkpoints, lineStart, lineIndex);
}

[[maybe_unused]] void buildDirectInitialLineIndexScalar(const char *data, Offset length,
                                                        std::vector<LineIndexCheckpoint> &checkpoints,
                                                        Offset &lineStart, std::size_t &lineIndex) {
	Offset skipLfAt = std::numeric_limits<Offset>::max();

	for (Offset i = 0; i < length; ++i) {
		if (i == skipLfAt) {
			skipLfAt = std::numeric_limits<Offset>::max();
			continue;
		}
		if (!isLineBreakByte(data[i]))
			continue;
		applyLineBreakAt(data, length, i, lineStart, lineIndex, checkpoints, skipLfAt);
	}
}

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
void buildDirectInitialLineIndexSse2(const char *data, Offset length,
                                     std::vector<LineIndexCheckpoint> &checkpoints, Offset &lineStart,
                                     std::size_t &lineIndex) {
	const __m128i cr = _mm_set1_epi8('\r');
	const __m128i lf = _mm_set1_epi8('\n');
	const Offset width = 16;
	Offset i = 0;
	Offset skipLfAt = std::numeric_limits<Offset>::max();

	for (; i + width <= length; i += width) {
		const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + i));
		const __m128i isCr = _mm_cmpeq_epi8(bytes, cr);
		const __m128i isLf = _mm_cmpeq_epi8(bytes, lf);
		unsigned int mask = static_cast<unsigned int>(_mm_movemask_epi8(_mm_or_si128(isCr, isLf)));

		while (mask != 0) {
			const unsigned int bit = static_cast<unsigned int>(__builtin_ctz(mask));
			const Offset at = i + static_cast<Offset>(bit);

			mask &= (mask - 1);
			if (at == skipLfAt) {
				skipLfAt = std::numeric_limits<Offset>::max();
				continue;
			}
			applyLineBreakAt(data, length, at, lineStart, lineIndex, checkpoints, skipLfAt);
		}
	}
	for (; i < length; ++i) {
		if (i == skipLfAt) {
			skipLfAt = std::numeric_limits<Offset>::max();
			continue;
		}
		if (!isLineBreakByte(data[i]))
			continue;
		applyLineBreakAt(data, length, i, lineStart, lineIndex, checkpoints, skipLfAt);
	}
}
#endif

void buildDirectInitialLineIndex(const char *data, Offset length,
                                 std::vector<LineIndexCheckpoint> &checkpoints, Offset &indexedOffset,
                                 std::size_t &indexedLine, std::size_t &totalLines) {
	checkpoints.clear();
	checkpoints.push_back(LineIndexCheckpoint(0, 0));
	indexedOffset = 0;
	indexedLine = 0;

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
	buildDirectInitialLineIndexSse2(data, length, checkpoints, indexedOffset, indexedLine);
#else
	buildDirectInitialLineIndexScalar(data, length, checkpoints, indexedOffset, indexedLine);
#endif
	totalLines = indexedLine + 1;
}

Offset directFindNextLineBreak(const char *data, Offset length, Offset start) noexcept {
	if (data == nullptr || start >= length)
		return length;

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
	const __m128i cr = _mm_set1_epi8('\r');
	const __m128i lf = _mm_set1_epi8('\n');
	Offset i = start;

	for (; i + 16 <= length; i += 16) {
		const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + i));
		const __m128i maskVec = _mm_or_si128(_mm_cmpeq_epi8(bytes, cr), _mm_cmpeq_epi8(bytes, lf));
		unsigned int mask = static_cast<unsigned int>(_mm_movemask_epi8(maskVec));
		if (mask != 0)
			return i + static_cast<Offset>(__builtin_ctz(mask));
	}
	for (; i < length; ++i)
		if (isLineBreakByte(data[i]))
			return i;
	return length;
#else
	for (Offset i = start; i < length; ++i)
		if (isLineBreakByte(data[i]))
			return i;
	return length;
#endif
}

Offset directFindPrevLineBreak(const char *data, Offset endExclusive) noexcept {
	if (data == nullptr || endExclusive == 0)
		return static_cast<Offset>(-1);

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
	const __m128i cr = _mm_set1_epi8('\r');
	const __m128i lf = _mm_set1_epi8('\n');
	Offset i = endExclusive;

	while (i >= 16) {
		const Offset blockStart = i - 16;
		const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + blockStart));
		const __m128i maskVec = _mm_or_si128(_mm_cmpeq_epi8(bytes, cr), _mm_cmpeq_epi8(bytes, lf));
		unsigned int mask = static_cast<unsigned int>(_mm_movemask_epi8(maskVec));
		if (mask != 0) {
			const unsigned int highestBit = 31u - static_cast<unsigned int>(__builtin_clz(mask));
			return blockStart + static_cast<Offset>(highestBit);
		}
		i = blockStart;
	}
	for (Offset j = i; j > 0; --j)
		if (isLineBreakByte(data[j - 1]))
			return j - 1;
	return static_cast<Offset>(-1);
#else
	for (Offset i = endExclusive; i > 0; --i)
		if (isLineBreakByte(data[i - 1]))
			return i - 1;
	return static_cast<Offset>(-1);
#endif
}

inline std::size_t countLineBreaksScalar(const char *data, Offset length, bool &prevWasCR) noexcept {
	std::size_t count = 0;
	for (Offset i = 0; i < length; ++i) {
		const char ch = data[i];
		if (ch == '\n') {
			if (!prevWasCR)
				++count;
			prevWasCR = false;
		} else if (ch == '\r') {
			++count;
			prevWasCR = true;
		} else
			prevWasCR = false;
	}
	return count;
}

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
inline std::size_t countLineBreaksSse2(const char *data, Offset length, bool &prevWasCR) noexcept {
	const __m128i cr = _mm_set1_epi8('\r');
	const __m128i lf = _mm_set1_epi8('\n');
	const Offset width = 16;
	Offset i = 0;
	std::size_t count = 0;

	for (; i + width <= length; i += width) {
		const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + i));
		const unsigned int crMask =
		    static_cast<unsigned int>(_mm_movemask_epi8(_mm_cmpeq_epi8(bytes, cr)));
		const unsigned int lfMask =
		    static_cast<unsigned int>(_mm_movemask_epi8(_mm_cmpeq_epi8(bytes, lf)));
		unsigned int lfNotAfterCr = lfMask & ~(crMask << 1);
		if (prevWasCR)
			lfNotAfterCr &= ~1u;
		count += static_cast<std::size_t>(__builtin_popcount(crMask));
		count += static_cast<std::size_t>(__builtin_popcount(lfNotAfterCr));
		prevWasCR = (crMask & (1u << 15)) != 0;
	}
	count += countLineBreaksScalar(data + i, length - i, prevWasCR);
	return count;
}
#endif

inline std::size_t countLineBreaksChunk(const char *data, Offset length, bool &prevWasCR) noexcept {
	if (data == nullptr || length == 0)
		return 0;
#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
	return countLineBreaksSse2(data, length, prevWasCR);
#else
	return countLineBreaksScalar(data, length, prevWasCR);
#endif
}

inline std::size_t directCountLineBreaksInRange(const char *data, Offset length, Offset start,
                                                Offset end) noexcept {
	if (data == nullptr || length == 0)
		return 0;
	start = std::min(start, length);
	end = std::min(end, length);
	if (end <= start)
		return 0;
	bool prevWasCR = start > 0 && data[start - 1] == '\r';
	return countLineBreaksChunk(data + start, end - start, prevWasCR);
}

template <class Doc>
std::size_t piecewiseCountLineBreaksInRange(const Doc &doc, Offset start, Offset end) noexcept {
	start = doc.clampOffset(start);
	end = doc.clampOffset(end);
	if (end <= start)
		return 0;

	bool prevWasCR = false;
	if (start > 0)
		prevWasCR = piecewiseCharAt(doc, start - 1) == '\r';

	std::size_t count = 0;
	Offset logical = 0;
	for (std::size_t i = 0; i < doc.pieceCount() && logical < end; ++i) {
		PieceChunkView chunk = doc.pieceChunk(i);
		if (chunk.data == nullptr || chunk.length == 0)
			continue;
		const Offset chunkStart = logical;
		const Offset chunkEnd = logical + chunk.length;
		if (chunkEnd <= start) {
			logical = chunkEnd;
			continue;
		}
		const Offset takeStart = std::max(start, chunkStart);
		const Offset takeEnd = std::min(end, chunkEnd);
		if (takeEnd > takeStart)
			count += countLineBreaksChunk(chunk.data + (takeStart - chunkStart),
			                              takeEnd - takeStart, prevWasCR);
		logical = chunkEnd;
	}
	return count;
}

template <class Doc>
std::size_t piecewiseLineCount(const Doc &doc) noexcept {
	bool prevWasCR = false;
	std::size_t breaks = 0;
	for (std::size_t i = 0; i < doc.pieceCount(); ++i) {
		PieceChunkView chunk = doc.pieceChunk(i);
		if (chunk.data == nullptr || chunk.length == 0)
			continue;
		breaks += countLineBreaksChunk(chunk.data, chunk.length, prevWasCR);
	}
	return breaks + 1;
}

template <class Doc>
Offset piecewiseLineStart(const Doc &doc, Offset pos) noexcept {
	pos = doc.clampOffset(pos);
	if (pos == 0)
		return 0;

	Offset logicalEnd = doc.length();
	for (std::size_t i = doc.pieceCount(); i > 0; --i) {
		PieceChunkView chunk = doc.pieceChunk(i - 1);
		if (chunk.data == nullptr || chunk.length == 0)
			continue;
		Offset chunkStart = logicalEnd - chunk.length;
		if (pos <= chunkStart) {
			logicalEnd = chunkStart;
			continue;
		}

		Offset localLimit = std::min(chunk.length, pos - chunkStart);
		const Offset breakPos = directFindPrevLineBreak(chunk.data, localLimit);
		if (breakPos != static_cast<Offset>(-1))
			return chunkStart + breakPos + 1;

		pos = chunkStart;
		logicalEnd = chunkStart;
		if (pos == 0)
			break;
	}
	return 0;
}

template <class Doc>
Offset piecewiseLineEnd(const Doc &doc, Offset pos) noexcept {
	pos = doc.clampOffset(pos);
	Offset logical = 0;
	bool active = false;
	for (std::size_t i = 0; i < doc.pieceCount(); ++i) {
		PieceChunkView chunk = doc.pieceChunk(i);
		if (chunk.data == nullptr || chunk.length == 0)
			continue;
		Offset start = 0;
		if (!active) {
			if (pos >= logical + chunk.length) {
				logical += chunk.length;
				continue;
			}
			start = pos - logical;
			active = true;
		}
		const Offset breakPos = directFindNextLineBreak(chunk.data, chunk.length, start);
		if (breakPos < chunk.length)
			return logical + breakPos;
		logical += chunk.length;
	}
	return doc.length();
}

template <class Doc>
Offset piecewiseNextLine(const Doc &doc, Offset pos) noexcept {
	Offset end = piecewiseLineEnd(doc, pos);
	if (end < doc.length()) {
		if (piecewiseCharAt(doc, end) == '\r' && end + 1 < doc.length() &&
		    piecewiseCharAt(doc, end + 1) == '\n')
			end += 2;
		else
			++end;
	}
	return end;
}

template <class Doc>
bool piecewiseAdvanceLine(const Doc &doc, Offset &offset) noexcept {
	Offset end = piecewiseLineEnd(doc, offset);
	if (end >= doc.length())
		return false;
	if (piecewiseCharAt(doc, end) == '\r' && end + 1 < doc.length() && piecewiseCharAt(doc, end + 1) == '\n')
		offset = end + 2;
	else
		offset = end + 1;
	return true;
}

template <class Doc>
Offset piecewisePrevLine(const Doc &doc, Offset pos) noexcept {
	Offset start = piecewiseLineStart(doc, pos);
	if (start == 0)
		return 0;
	Offset probe = start - 1;
	if (probe > 0 && piecewiseCharAt(doc, probe - 1) == '\r' && piecewiseCharAt(doc, probe) == '\n')
		--probe;
	return piecewiseLineStart(doc, probe);
}

template <class Doc>
std::size_t piecewiseLineIndex(const Doc &doc, Offset pos) noexcept {
	std::size_t line = 0;
	bool prevWasCR = false;
	pos = doc.clampOffset(pos);
	Offset logical = 0;
	for (std::size_t i = 0; i < doc.pieceCount() && logical < pos; ++i) {
		PieceChunkView chunk = doc.pieceChunk(i);
		if (chunk.data == nullptr || chunk.length == 0)
			continue;
		Offset localLimit = std::min(chunk.length, pos - logical);
		for (Offset j = 0; j < localLimit; ++j) {
			char ch = chunk.data[j];
			if (ch == '\n') {
				if (!prevWasCR)
					++line;
				prevWasCR = false;
			} else if (ch == '\r') {
				++line;
				prevWasCR = true;
			} else
				prevWasCR = false;
		}
		logical += chunk.length;
	}
	return line;
}

template <class Doc>
Offset piecewiseLineStartByIndex(const Doc &doc, std::size_t index) noexcept {
	Offset pos = 0;
	std::size_t line = 0;
	while (line < index && pos < doc.length()) {
		Offset next = piecewiseNextLine(doc, pos);
		if (next <= pos)
			break;
		pos = next;
		++line;
	}
	return pos;
}

template <class Doc>
std::string piecewiseRangeText(const Doc &doc, Offset start, Offset end) {
	start = doc.clampOffset(start);
	end = doc.clampOffset(end);
	if (end < start)
		std::swap(start, end);
	std::string out;
	out.reserve(end - start);
	Offset logical = 0;
	for (std::size_t i = 0; i < doc.pieceCount() && logical < end; ++i) {
		PieceChunkView chunk = doc.pieceChunk(i);
		if (chunk.data == nullptr || chunk.length == 0)
			continue;
		Offset chunkStart = logical;
		Offset chunkEnd = logical + chunk.length;
		if (chunkEnd <= start) {
			logical = chunkEnd;
			continue;
		}
		Offset takeStart = std::max(start, chunkStart);
		Offset takeEnd = std::min(end, chunkEnd);
		if (takeEnd > takeStart)
			out.append(chunk.data + (takeStart - chunkStart), takeEnd - takeStart);
		logical = chunkEnd;
	}
	return out;
}
}

struct MappedFileSource::State {
	int fd;
	const char *data;
	std::size_t size;
	std::string path;

	State() noexcept : fd(-1), data(nullptr), size(0) {
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

TextSpan AppendBuffer::append(std::string_view text) {
	TextSpan span(text_.size(), text.size());
	text_.append(text.data(), text.size());
	return span;
}

void AppendBuffer::clear() noexcept {
	text_.clear();
}

std::string AppendBuffer::sliceText(TextSpan span) const {
	TextSpan bounded = span.clamped(text_.size());
	return text_.substr(bounded.start, bounded.length);
}

TextSpan StagedAddBuffer::append(std::string_view text) {
	TextSpan span(text_.size(), text.size());
	text_.append(text.data(), text.size());
	return span;
}

void StagedAddBuffer::clear() noexcept {
	text_.clear();
}

std::string StagedAddBuffer::sliceText(TextSpan span) const {
	TextSpan bounded = span.clamped(text_.size());
	return text_.substr(bounded.start, bounded.length);
}

void EditTransaction::setText(std::string_view text) {
	operations_.push_back(EditOperation(EditKind::SetText, Range(), text));
}

void EditTransaction::insert(Offset offset, std::string_view text) {
	operations_.push_back(EditOperation(EditKind::Insert, Range(offset, offset), text));
}

void EditTransaction::erase(Range range) {
	range.normalize();
	operations_.push_back(EditOperation(EditKind::Erase, range, std::string()));
}

void EditTransaction::replace(Range range, std::string_view text) {
	range.normalize();
	operations_.push_back(EditOperation(EditKind::Replace, range, text));
}

void StagedEditTransaction::setText(std::string_view text) {
	operations_.push_back(
	    StagedEditOperation(EditKind::SetText, Range(), addBuffer_.append(text)));
}

void StagedEditTransaction::insert(Offset offset, std::string_view text) {
	operations_.push_back(
	    StagedEditOperation(EditKind::Insert, Range(offset, offset), addBuffer_.append(text)));
}

void StagedEditTransaction::erase(Range range) {
	range.normalize();
	operations_.push_back(StagedEditOperation(EditKind::Erase, range, TextSpan()));
}

void StagedEditTransaction::replace(Range range, std::string_view text) {
	range.normalize();
	operations_.push_back(
	    StagedEditOperation(EditKind::Replace, range, addBuffer_.append(text)));
}

StagedEditTransaction::StagedEditTransaction(const ReadSnapshot &snapshot,
                                             std::string_view label)
    : baseVersion_(snapshot.version()), label_(label) {
}

TextDocument::TextDocument() noexcept
    :  length_(0),
      documentId_(allocateDocumentId()), version_(0), cacheDirty_(false),  lazyIndexedOffset_(0), lazyIndexedLine_(0),
      lazyLineIndexComplete_(false), lazyTotalLineCount_(1) {
	resetLazyLineIndex();
}

TextDocument::TextDocument(std::string_view text)
    :  length_(0),
      documentId_(allocateDocumentId()), version_(0), cacheDirty_(false),  lazyIndexedOffset_(0), lazyIndexedLine_(0),
      lazyLineIndexComplete_(false), lazyTotalLineCount_(1) {
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
	return piecewiseCharAt(*this, pos);
}

Snapshot TextDocument::snapshot() const {
	return Snapshot(text(), version_);
}

ReadSnapshot TextDocument::readSnapshot() const {
	ReadSnapshot snapshot;
	snapshot.documentId_ = documentId_;
	snapshot.version_ = version_;
	snapshot.mappedOriginal_ = mappedOriginal_;
	snapshot.originalBuffer_ = std::make_shared<const std::string>(originalBuffer_);
	snapshot.addBuffer_ = std::make_shared<const std::string>(addBuffer_.text());
	snapshot.pieces_ = std::make_shared<const std::vector<Piece>>(pieces_);
	snapshot.length_ = length_;
	snapshot.cacheDirty_ = cacheDirty_;
	snapshot.materializedText_ = cacheDirty_ ? std::string() : materializedText_;
	snapshot.lineIndexCheckpoints_ = lineIndexCheckpoints_;
	snapshot.lazyIndexedOffset_ = lazyIndexedOffset_;
	snapshot.lazyIndexedLine_ = lazyIndexedLine_;
	snapshot.lazyLineIndexComplete_ = lazyLineIndexComplete_;
	snapshot.lazyTotalLineCount_ = lazyTotalLineCount_;
	if (snapshot.lineIndexCheckpoints_.empty())
		snapshot.resetLazyLineIndex();
	return snapshot;
}

bool TextDocument::adoptLineIndexWarmup(const LineIndexWarmupData &warmup,
                                        std::size_t expectedVersion) noexcept {
	if (!matchesVersion(expectedVersion))
		return false;
	if (warmup.checkpoints.empty())
		return false;

	const bool currentComplete = lazyLineIndexComplete_;
	const Offset currentOffset = lazyIndexedOffset_;
	const std::size_t currentCheckpointCount = lineIndexCheckpoints_.size();

	const bool incomingBetter = !currentComplete && warmup.lazyLineIndexComplete;
	const bool incomingFurther = warmup.lazyIndexedOffset > currentOffset;
	const bool incomingDenser = warmup.checkpoints.size() > currentCheckpointCount;

	if (!incomingBetter && !incomingFurther && !incomingDenser)
		return false;

	lineIndexCheckpoints_ = warmup.checkpoints;
	lazyIndexedOffset_ = warmup.lazyIndexedOffset;
	lazyIndexedLine_ = warmup.lazyIndexedLine;
	lazyLineIndexComplete_ = warmup.lazyLineIndexComplete;
	lazyTotalLineCount_ = warmup.lazyTotalLineCount;
	if (lineIndexCheckpoints_.empty())
		resetLazyLineIndex();
	return true;
}

ReadSnapshot::ReadSnapshot() noexcept
    : documentId_(0), version_(0), 
      length_(0), cacheDirty_(false), 
      lazyIndexedOffset_(0), lazyIndexedLine_(0), lazyLineIndexComplete_(true),
      lazyTotalLineCount_(1) {
	resetLazyLineIndex();
}

Offset ReadSnapshot::length() const noexcept {
	return length_;
}

bool ReadSnapshot::empty() const noexcept {
	return length_ == 0;
}

char ReadSnapshot::charAt(Offset pos) const noexcept {
	if (const char *data = directTextData())
		return pos < length_ ? data[pos] : '\0';
	return piecewiseCharAt(*this, pos);
}

std::string ReadSnapshot::text() const {
	ensureMaterialized();
	return materializedText_;
}

std::size_t ReadSnapshot::addBufferLength() const noexcept {
	return addBuffer_ != nullptr ? addBuffer_->size() : 0;
}

std::size_t ReadSnapshot::pieceCount() const noexcept {
	return pieces_ != nullptr ? pieces_->size() : 0;
}

PieceChunkView ReadSnapshot::pieceChunk(std::size_t index) const noexcept {
	if (pieces_ == nullptr || index >= pieces_->size())
		return PieceChunkView();

	const Piece &piece = (*pieces_)[index];
	if (piece.empty())
		return PieceChunkView();
	if (piece.source == BufferKind::Original) {
		const char *base = originalData();
		if (base == nullptr)
			return PieceChunkView();
		return PieceChunkView(base + piece.span.start, piece.span.length);
	}
	if (addBuffer_ == nullptr || piece.span.start >= addBuffer_->size())
		return PieceChunkView();
	return PieceChunkView(addBuffer_->data() + piece.span.start, piece.span.length);
}

Offset ReadSnapshot::clampOffset(Offset pos) const noexcept {
	return std::min(pos, length_);
}

std::size_t ReadSnapshot::lineCount() const noexcept {
	ensureLazyIndexComplete();
	return lazyTotalLineCount_;
}

Offset ReadSnapshot::lineStart(Offset pos) const noexcept {
	if (const char *data = directTextData()) {
		pos = clampOffset(pos);
		Offset breakPos = directFindPrevLineBreak(data, pos);
		return breakPos == static_cast<Offset>(-1) ? 0 : breakPos + 1;
	}

	return piecewiseLineStart(*this, pos);
}

Offset ReadSnapshot::lineEnd(Offset pos) const noexcept {
	if (const char *data = directTextData()) {
		pos = clampOffset(pos);
		return directFindNextLineBreak(data, length_, pos);
	}

	return piecewiseLineEnd(*this, pos);
}

Offset ReadSnapshot::nextLine(Offset pos) const noexcept {
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

	return piecewiseNextLine(*this, pos);
}

Offset ReadSnapshot::prevLine(Offset pos) const noexcept {
	if (const char *data = directTextData()) {
		pos = lineStart(pos);
		if (pos == 0)
			return 0;
		--pos;
		if (pos > 0 && data[pos - 1] == '\r' && data[pos] == '\n')
			--pos;
		Offset breakPos = directFindPrevLineBreak(data, pos);
		return breakPos == static_cast<Offset>(-1) ? 0 : breakPos + 1;
	}

	return piecewisePrevLine(*this, pos);
}

std::size_t ReadSnapshot::lineIndex(Offset pos) const noexcept {
	pos = clampOffset(pos);
	ensureLazyIndexSeeded();
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
	if (checkpoint.offset >= pos)
		return checkpoint.lineIndex;
	if (const char *data = directTextData()) {
		const std::size_t delta =
		    directCountLineBreaksInRange(data, length_, checkpoint.offset, pos);
		return checkpoint.lineIndex + delta;
	}
	return checkpoint.lineIndex + piecewiseCountLineBreaksInRange(*this, checkpoint.offset, pos);
}

Offset ReadSnapshot::lineStartByIndex(std::size_t index) const noexcept {
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
		if (!advanceLine(next))
			break;
		cursor = next;
		++line;
	}
	return cursor;
}

std::size_t ReadSnapshot::estimatedLineCount() const noexcept {
	ensureLazyIndexSeeded();
	if (lazyLineIndexComplete_)
		return lazyTotalLineCount_;
	if (!mappedOriginal_.mapped())
		return piecewiseLineCount(*this);
	if (lazyIndexedOffset_ == 0 || lazyIndexedLine_ == 0)
		return std::max<std::size_t>(1, length_ / 80 + 1);

	const std::size_t observedLines = lazyIndexedLine_ + 1;
	const std::size_t estimated =
	    static_cast<std::size_t>((static_cast<long double>(length_) * observedLines) /
	                             std::max<Offset>(lazyIndexedOffset_, 1));
	return std::max<std::size_t>(observedLines, estimated);
}

bool ReadSnapshot::exactLineCountKnown() const noexcept {
	return !mappedOriginal_.mapped() || lazyLineIndexComplete_;
}

std::size_t ReadSnapshot::column(Offset pos) const noexcept {
	pos = clampOffset(pos);
	return pos - lineStart(pos);
}

std::string ReadSnapshot::lineText(Offset pos) const {
	Offset start = lineStart(pos);
	Offset end = lineEnd(pos);
	if (const char *data = directTextData())
		return std::string(data + start, end - start);
	return piecewiseRangeText(*this, start, end);
}

LineIndexWarmupData ReadSnapshot::completeLineIndexWarmup() const {
	LineIndexWarmupData warmup;
	(void)completeLineIndexWarmup(warmup, std::stop_token());
	return warmup;
}

bool ReadSnapshot::completeLineIndexWarmup(LineIndexWarmupData &warmup, std::stop_token stopToken) const {
	ensureLazyIndexSeeded();
	while (!lazyLineIndexComplete_) {
		if (stopToken.stop_requested())
			return false;
		advanceLazyIndexByStride();
	}
	if (stopToken.stop_requested())
		return false;
	warmup.checkpoints = lineIndexCheckpoints_;
	warmup.lazyIndexedOffset = lazyIndexedOffset_;
	warmup.lazyIndexedLine = lazyIndexedLine_;
	warmup.lazyLineIndexComplete = lazyLineIndexComplete_;
	warmup.lazyTotalLineCount = lazyTotalLineCount_;
	return true;
}

bool ReadSnapshot::isLineBreakChar(char ch) const noexcept {
	return ch == '\n' || ch == '\r';
}

bool ReadSnapshot::hasDirectOriginalView() const noexcept {
	return mappedOriginal_.mapped() && addBufferLength() == 0 && pieces_ != nullptr &&
	       pieces_->size() == 1 && (*pieces_)[0].source == BufferKind::Original &&
	       (*pieces_)[0].span.start == 0 && (*pieces_)[0].span.length == length_;
}

const char *ReadSnapshot::directTextData() const noexcept {
	return hasDirectOriginalView() ? mappedOriginal_.data() : nullptr;
}

void ReadSnapshot::resetLazyLineIndex() noexcept {
	lineIndexCheckpoints_.clear();
	lineIndexCheckpoints_.push_back(LineIndexCheckpoint(0, 0));
	lazyIndexedOffset_ = 0;
	lazyIndexedLine_ = 0;
	lazyLineIndexComplete_ = (length_ == 0);
	lazyTotalLineCount_ = 1;
}

bool ReadSnapshot::advanceLine(Offset &offset) const noexcept {
	if (directAdvanceLine(offset))
		return true;
	return piecewiseAdvanceLine(*this, offset);
}

bool ReadSnapshot::directAdvanceLine(Offset &offset) const noexcept {
	const char *data = directTextData();
	if (data == nullptr)
		return false;
	if (offset >= length_)
		return false;

	Offset breakPos = directFindNextLineBreak(data, length_, offset);
	if (breakPos >= length_)
		return false;
	if (data[breakPos] == '\r' && breakPos + 1 < length_ && data[breakPos + 1] == '\n')
		offset = breakPos + 2;
	else
		offset = breakPos + 1;
	return true;
}

void ReadSnapshot::ensureLazyIndexSeeded() const noexcept {
	if (lineIndexCheckpoints_.empty())
		const_cast<ReadSnapshot *>(this)->resetLazyLineIndex();
}

void ReadSnapshot::advanceLazyIndexByStride() const noexcept {
	ensureLazyIndexSeeded();
	if (lazyLineIndexComplete_)
		return;

	for (std::size_t steps = 0; steps < kLazyLineIndexStride; ++steps) {
		Offset next = lazyIndexedOffset_;
		if (!advanceLine(next)) {
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

void ReadSnapshot::ensureLazyIndexForLine(std::size_t targetLine) const noexcept {
	ensureLazyIndexSeeded();
	while (!lazyLineIndexComplete_ && lineIndexCheckpoints_.back().lineIndex < targetLine)
		advanceLazyIndexByStride();
}

void ReadSnapshot::ensureLazyIndexForOffset(Offset targetOffset) const noexcept {
	ensureLazyIndexSeeded();
	targetOffset = clampOffset(targetOffset);
	while (!lazyLineIndexComplete_ && lineIndexCheckpoints_.back().offset <= targetOffset)
		advanceLazyIndexByStride();
}

void ReadSnapshot::ensureLazyIndexComplete() const noexcept {
	ensureLazyIndexSeeded();
	if (!lazyLineIndexComplete_ && lineIndexCheckpoints_.size() == 1 && lineIndexCheckpoints_[0].offset == 0 &&
	    lineIndexCheckpoints_[0].lineIndex == 0 && lazyIndexedOffset_ == 0 && lazyIndexedLine_ == 0) {
		if (const char *data = directTextData()) {
			buildDirectInitialLineIndex(data, length_, lineIndexCheckpoints_, lazyIndexedOffset_,
			                           lazyIndexedLine_, lazyTotalLineCount_);
			lazyLineIndexComplete_ = true;
			return;
		}
	}
	while (!lazyLineIndexComplete_)
		advanceLazyIndexByStride();
}

const char *ReadSnapshot::originalData() const noexcept {
	if (mappedOriginal_.mapped())
		return mappedOriginal_.data();
	return originalBuffer_ != nullptr ? originalBuffer_->data() : nullptr;
}

std::string ReadSnapshot::pieceText(const Piece &piece) const {
	if (piece.source == BufferKind::Original) {
		if (mappedOriginal_.mapped())
			return mappedOriginal_.sliceText(piece.span);
		if (originalBuffer_ != nullptr)
			return originalBuffer_->substr(piece.span.start, piece.span.length);
		return std::string();
	}
	return addBuffer_ != nullptr ? addBuffer_->substr(piece.span.start, piece.span.length) : std::string();
}

void ReadSnapshot::ensureMaterialized() const noexcept {
	if (!cacheDirty_)
		return;

	materializedText_.clear();
	materializedText_.reserve(length_);
	if (pieces_ != nullptr)
		for (std::size_t i = 0; i < pieces_->size(); ++i)
			materializedText_ += pieceText((*pieces_)[i]);
	cacheDirty_ = false;
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

void TextDocument::setText(std::string_view text) {
	if (setTextNoVersionBump(text))
		bumpVersion();
}

void TextDocument::apply(const EditTransaction &transaction) {
	const std::vector<EditOperation> &ops = transaction.operations();
	bool mutated = false;
	for (const auto & op : ops) {
		mutated = applyOperationNoVersionBump(op) || mutated;
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
	for (const auto & op : ops) {
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
		result.change.touchedRange = Range(touchStart, touchEnd).normalized();
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
	for (const auto & op : ops) {
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
		result.change.touchedRange = Range(touchStart, touchEnd).normalized();
	return result;
}

void TextDocument::insert(Offset offset, std::string_view text) {
	if (text.empty())
		return;
	if (insertAddSpanNoVersionBump(offset, addBuffer_.append(text)))
		bumpVersion();
}

void TextDocument::erase(Range range) {
	if (eraseNoVersionBump(range))
		bumpVersion();
}

void TextDocument::replace(Range range, std::string_view text) {
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
	ensureLazyIndexComplete();
	return lazyTotalLineCount_;
}

Offset TextDocument::lineStart(Offset pos) const noexcept {
	if (const char *data = directTextData()) {
		pos = clampOffset(pos);
		Offset breakPos = directFindPrevLineBreak(data, pos);
		return breakPos == static_cast<Offset>(-1) ? 0 : breakPos + 1;
	}

	return piecewiseLineStart(*this, pos);
}

Offset TextDocument::lineEnd(Offset pos) const noexcept {
	if (const char *data = directTextData()) {
		pos = clampOffset(pos);
		return directFindNextLineBreak(data, length_, pos);
	}

	return piecewiseLineEnd(*this, pos);
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

	return piecewiseNextLine(*this, pos);
}

Offset TextDocument::prevLine(Offset pos) const noexcept {
	if (const char *data = directTextData()) {
		pos = lineStart(pos);
		if (pos == 0)
			return 0;
		--pos;
		if (pos > 0 && data[pos - 1] == '\r' && data[pos] == '\n')
			--pos;
		Offset breakPos = directFindPrevLineBreak(data, pos);
		return breakPos == static_cast<Offset>(-1) ? 0 : breakPos + 1;
	}

	return piecewisePrevLine(*this, pos);
}

std::size_t TextDocument::lineIndex(Offset pos) const noexcept {
	pos = clampOffset(pos);
	ensureLazyIndexSeeded();
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
	if (checkpoint.offset >= pos)
		return checkpoint.lineIndex;
	if (const char *data = directTextData()) {
		const std::size_t delta =
		    directCountLineBreaksInRange(data, length_, checkpoint.offset, pos);
		return checkpoint.lineIndex + delta;
	}
	return checkpoint.lineIndex + piecewiseCountLineBreaksInRange(*this, checkpoint.offset, pos);
}

Offset TextDocument::lineStartByIndex(std::size_t index) const noexcept {
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
		if (!advanceLine(next))
			break;
		cursor = next;
		++line;
	}
	return cursor;
}

std::size_t TextDocument::estimatedLineCount() const noexcept {
	ensureLazyIndexSeeded();
	if (lazyLineIndexComplete_)
		return lazyTotalLineCount_;
	if (!mappedOriginal_.mapped())
		return piecewiseLineCount(*this);
	if (lazyIndexedOffset_ == 0 || lazyIndexedLine_ == 0)
		return std::max<std::size_t>(1, length_ / 80 + 1);

	const std::size_t observedLines = lazyIndexedLine_ + 1;
	const std::size_t estimated =
	    static_cast<std::size_t>((static_cast<long double>(length_) * observedLines) /
	                             std::max<Offset>(lazyIndexedOffset_, 1));
	return std::max<std::size_t>(observedLines, estimated);
}

bool TextDocument::exactLineCountKnown() const noexcept {
	return !mappedOriginal_.mapped() || lazyLineIndexComplete_;
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
	return piecewiseRangeText(*this, start, end);
}

bool TextDocument::isLineBreakChar(char ch) const noexcept {
	return ch == '\n' || ch == '\r';
}

bool TextDocument::setTextNoVersionBump(std::string_view text) {
	if (text == this->text())
		return false;
	initializeFromOriginal(text, false);
	return true;
}

void TextDocument::initializeFromOriginal(std::string_view text, bool bumpVersionFlag) {
	originalBuffer_.assign(text.data(), text.size());
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
}

void TextDocument::ensureMaterialized() const noexcept {
	if (!cacheDirty_)
		return;

	materializedText_.clear();
	materializedText_.reserve(length_);
	for (const auto & piece : pieces_)
		materializedText_ += pieceText(piece);
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
	invalidateLazyLineIndexFrom(bounded.start);
	markDirty();
	return true;
}

bool TextDocument::replaceNoVersionBump(Range range, std::string_view text) {
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

	Offset breakPos = directFindNextLineBreak(data, length_, offset);
	if (breakPos >= length_)
		return false;
	if (data[breakPos] == '\r' && breakPos + 1 < length_ && data[breakPos + 1] == '\n')
		offset = breakPos + 2;
	else
		offset = breakPos + 1;
	return true;
}

bool TextDocument::advanceLine(Offset &offset) const noexcept {
	if (directAdvanceLine(offset))
		return true;
	return piecewiseAdvanceLine(*this, offset);
}

void TextDocument::ensureLazyIndexSeeded() const noexcept {
	if (lineIndexCheckpoints_.empty())
		const_cast<TextDocument *>(this)->resetLazyLineIndex();
}

void TextDocument::advanceLazyIndexByStride() const noexcept {
	ensureLazyIndexSeeded();
	if (lazyLineIndexComplete_)
		return;

	for (std::size_t steps = 0; steps < kLazyLineIndexStride; ++steps) {
		Offset next = lazyIndexedOffset_;
		if (!advanceLine(next)) {
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
	while (!lazyLineIndexComplete_ && lineIndexCheckpoints_.back().lineIndex < targetLine)
		advanceLazyIndexByStride();
}

void TextDocument::ensureLazyIndexForOffset(Offset targetOffset) const noexcept {
	ensureLazyIndexSeeded();
	targetOffset = clampOffset(targetOffset);
	while (!lazyLineIndexComplete_ && lineIndexCheckpoints_.back().offset <= targetOffset)
		advanceLazyIndexByStride();
}

void TextDocument::ensureLazyIndexComplete() const noexcept {
	ensureLazyIndexSeeded();
	if (!lazyLineIndexComplete_ && lineIndexCheckpoints_.size() == 1 && lineIndexCheckpoints_[0].offset == 0 &&
	    lineIndexCheckpoints_[0].lineIndex == 0 && lazyIndexedOffset_ == 0 && lazyIndexedLine_ == 0) {
		if (const char *data = directTextData()) {
			buildDirectInitialLineIndex(data, length_, lineIndexCheckpoints_, lazyIndexedOffset_,
			                           lazyIndexedLine_, lazyTotalLineCount_);
			lazyLineIndexComplete_ = true;
			return;
		}
	}
	while (!lazyLineIndexComplete_)
		advanceLazyIndexByStride();
}

void TextDocument::invalidateLazyLineIndexFrom(Offset offset) noexcept {
	offset = clampOffset(offset);
	if (lineIndexCheckpoints_.empty()) {
		resetLazyLineIndex();
		return;
	}

	std::vector<LineIndexCheckpoint>::iterator keepEnd =
	    std::upper_bound(lineIndexCheckpoints_.begin(), lineIndexCheckpoints_.end(), offset,
	                     [](Offset value, const LineIndexCheckpoint &checkpoint) {
		                     return value < checkpoint.offset;
	                     });
	if (keepEnd == lineIndexCheckpoints_.begin())
		++keepEnd;
	lineIndexCheckpoints_.erase(keepEnd, lineIndexCheckpoints_.end());
	if (lineIndexCheckpoints_.empty())
		resetLazyLineIndex();
	else {
		lazyIndexedOffset_ = lineIndexCheckpoints_.back().offset;
		lazyIndexedLine_ = lineIndexCheckpoints_.back().lineIndex;
		lazyLineIndexComplete_ = false;
		lazyTotalLineCount_ = std::max<std::size_t>(1, lazyIndexedLine_ + 1);
	}
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
	invalidateLazyLineIndexFrom(logical);
	markDirty();
	return true;
}

void TextDocument::compactPieces() {
	std::vector<Piece> compacted;
	compacted.reserve(pieces_.size());

	for (const auto & piece : pieces_) {
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
