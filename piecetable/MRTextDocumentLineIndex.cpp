#include "MRTextDocumentLineIndex.hpp"

#include <array>
#include <atomic>
#include <ctime>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
#include <emmintrin.h>
#endif

namespace mr {
namespace editor {

using namespace lineindex;

namespace {
constexpr Offset kLargeMappedEditNormalizationLength = static_cast<Offset>(8) * 1024 * 1024;
constexpr std::size_t kLargeMappedEditNormalizationPieceThreshold = 256;
constexpr Offset kDirectLineIndexTargetChunkBytes = static_cast<Offset>(1) * 1024 * 1024;

inline void appendLineCheckpoint(std::vector<LineIndexCheckpoint> &checkpoints, Offset lineStart, std::size_t lineIndex) {
	if ((lineIndex % kLazyLineIndexStride) == 0) checkpoints.push_back(LineIndexCheckpoint(lineStart, lineIndex));
}

inline std::size_t checkpointStrideForMode(bool directMode) noexcept {
	return directMode ? kLazyLineIndexStride : kPiecewiseLineIndexCheckpointStride;
}

inline bool isLineBreakByte(char ch) noexcept {
	return ch == '\n' || ch == '\r';
}

inline void applyLineBreakAt(const char *data, Offset length, Offset breakOffset, Offset &lineStart, std::size_t &lineIndex, std::vector<LineIndexCheckpoint> &checkpoints, Offset &skipLfAt) {
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

[[maybe_unused]] void buildDirectInitialLineIndexScalar(const char *data, Offset length, std::vector<LineIndexCheckpoint> &checkpoints, Offset &lineStart, std::size_t &lineIndex) {
	Offset skipLfAt = std::numeric_limits<Offset>::max();

	for (Offset i = 0; i < length; ++i) {
		if (i == skipLfAt) {
			skipLfAt = std::numeric_limits<Offset>::max();
			continue;
		}
		if (!isLineBreakByte(data[i])) continue;
		applyLineBreakAt(data, length, i, lineStart, lineIndex, checkpoints, skipLfAt);
	}
}

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
void buildDirectInitialLineIndexSse2(const char *data, Offset length, std::vector<LineIndexCheckpoint> &checkpoints, Offset &lineStart, std::size_t &lineIndex) {
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
		if (!isLineBreakByte(data[i])) continue;
		applyLineBreakAt(data, length, i, lineStart, lineIndex, checkpoints, skipLfAt);
	}
}
#endif

struct DirectLineIndexChunk {
	Offset start;
	Offset end;
	std::size_t breakCount;
	std::size_t startLineIndex;
	Offset lastLineStart;
	std::vector<LineIndexCheckpoint> checkpoints;

	DirectLineIndexChunk() noexcept : start(0), end(0), breakCount(0), startLineIndex(0), lastLineStart(0), checkpoints() {
	}
};

Offset nextDirectLineStartAtOrAfter(const char *data, Offset length, Offset pos) noexcept {
	pos = std::min(pos, length);
	if (data == nullptr || pos == 0 || pos >= length) return pos;
	if (data[pos - 1] == '\n') return pos;
	if (data[pos - 1] == '\r') return data[pos] == '\n' ? std::min(length, pos + 1) : pos;

	const Offset breakPos = directFindNextLineBreak(data, length, pos);
	if (breakPos >= length) return length;
	if (data[breakPos] == '\r' && breakPos + 1 < length && data[breakPos + 1] == '\n') return breakPos + 2;
	return breakPos + 1;
}

std::vector<DirectLineIndexChunk> planDirectLineIndexChunks(const char *data, Offset length) {
	std::vector<DirectLineIndexChunk> chunks;
	chunks.reserve(1);
	if (data == nullptr || length == 0) {
		chunks.push_back(DirectLineIndexChunk());
		chunks.back().start = 0;
		chunks.back().end = length;
		chunks.back().lastLineStart = 0;
		return chunks;
	}

	unsigned int hardwareThreads = std::thread::hardware_concurrency();
	if (hardwareThreads == 0) hardwareThreads = 1;
	const std::size_t requestedWorkers = std::max<std::size_t>(1, static_cast<std::size_t>((length + kDirectLineIndexTargetChunkBytes - 1) / kDirectLineIndexTargetChunkBytes));
	const std::size_t workerCount = std::max<std::size_t>(1, std::min<std::size_t>(hardwareThreads, requestedWorkers));
	if (workerCount == 1) {
		chunks.push_back(DirectLineIndexChunk());
		chunks.back().start = 0;
		chunks.back().end = length;
		chunks.back().lastLineStart = 0;
		return chunks;
	}

	std::vector<Offset> boundaries;
	boundaries.reserve(workerCount + 1);
	boundaries.push_back(0);
	for (std::size_t i = 1; i < workerCount; ++i) {
		const Offset ideal = static_cast<Offset>((static_cast<long double>(length) * static_cast<long double>(i)) / static_cast<long double>(workerCount));
		const Offset boundary = nextDirectLineStartAtOrAfter(data, length, ideal);
		if (boundary > boundaries.back() && boundary < length) boundaries.push_back(boundary);
	}
	boundaries.push_back(length);

	chunks.reserve(boundaries.size() > 1 ? boundaries.size() - 1 : 1);
	for (std::size_t i = 0; i + 1 < boundaries.size(); ++i) {
		if (boundaries[i + 1] < boundaries[i]) continue;
		DirectLineIndexChunk chunk;
		chunk.start = boundaries[i];
		chunk.end = boundaries[i + 1];
		chunk.lastLineStart = chunk.start;
		chunks.push_back(std::move(chunk));
	}
	if (chunks.empty()) {
		chunks.push_back(DirectLineIndexChunk());
		chunks.back().start = 0;
		chunks.back().end = length;
		chunks.back().lastLineStart = 0;
	}
	return chunks;
}

void buildDirectInitialLineIndexSingleThreaded(const char *data, Offset length, std::vector<LineIndexCheckpoint> &checkpoints, Offset &indexedOffset, std::size_t &indexedLine, std::size_t &totalLines) {
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

void countDirectLineIndexChunkBreaks(const char *data, DirectLineIndexChunk &chunk) noexcept {
	if (data == nullptr || chunk.end <= chunk.start) {
		chunk.breakCount = 0;
		return;
	}
	bool prevWasCR = false;
	chunk.breakCount = countLineBreaksChunk(data + chunk.start, chunk.end - chunk.start, prevWasCR);
}

void collectDirectLineIndexChunkCheckpoints(const char *data, Offset length, DirectLineIndexChunk &chunk) {
	chunk.checkpoints.clear();
	chunk.lastLineStart = chunk.start;

	if (data == nullptr || chunk.end < chunk.start) return;

	Offset lineStart = chunk.start;
	std::size_t lineIndex = chunk.startLineIndex;
	while (lineStart < chunk.end) {
		if (lineIndex != 0 && (lineIndex % kLazyLineIndexStride) == 0) chunk.checkpoints.push_back(LineIndexCheckpoint(lineStart, lineIndex));
		const Offset breakPos = directFindNextLineBreak(data, chunk.end, lineStart);
		if (breakPos >= chunk.end) break;
		if (data[breakPos] == '\r' && breakPos + 1 < chunk.end && data[breakPos + 1] == '\n') lineStart = breakPos + 2;
		else
			lineStart = breakPos + 1;
		++lineIndex;
		chunk.lastLineStart = lineStart;
	}

	if (chunk.end == length && lineStart == length && lineIndex != 0 && (lineIndex % kLazyLineIndexStride) == 0) chunk.checkpoints.push_back(LineIndexCheckpoint(lineStart, lineIndex));
}

inline std::size_t countLineBreaksScalar(const char *data, Offset length, bool &prevWasCR) noexcept {
	std::size_t count = 0;
	for (Offset i = 0; i < length; ++i) {
		const char ch = data[i];
		if (ch == '\n') {
			if (!prevWasCR) ++count;
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
		const unsigned int crMask = static_cast<unsigned int>(_mm_movemask_epi8(_mm_cmpeq_epi8(bytes, cr)));
		const unsigned int lfMask = static_cast<unsigned int>(_mm_movemask_epi8(_mm_cmpeq_epi8(bytes, lf)));
		unsigned int lfNotAfterCr = lfMask & ~(crMask << 1);
		if (prevWasCR) lfNotAfterCr &= ~1u;
		count += static_cast<std::size_t>(__builtin_popcount(crMask));
		count += static_cast<std::size_t>(__builtin_popcount(lfNotAfterCr));
		prevWasCR = (crMask & (1u << 15)) != 0;
	}
	count += countLineBreaksScalar(data + i, length - i, prevWasCR);
	return count;
}
#endif

} // namespace

namespace lineindex {

void appendDocumentTrace(std::string_view message) {
	std::array<char, 32> buffer{};
	const std::time_t now = std::time(nullptr);
	const std::tm *tmNow = std::localtime(&now);
	if (tmNow == nullptr) return;
	if (std::strftime(buffer.data(), buffer.size(), "%H:%M:%S", tmNow) == 0) return;

	std::ofstream out("misc/mr.log", std::ios::out | std::ios::app | std::ios::binary);
	if (!out) return;
	out << "[" << buffer.data() << "] " << message << '\n';
	out.flush();
}

void buildDirectInitialLineIndex(const char *data, Offset length, std::vector<LineIndexCheckpoint> &checkpoints, Offset &indexedOffset, std::size_t &indexedLine, std::size_t &totalLines) {
	std::vector<DirectLineIndexChunk> chunks = planDirectLineIndexChunks(data, length);
	if (chunks.size() <= 1) {
		buildDirectInitialLineIndexSingleThreaded(data, length, checkpoints, indexedOffset, indexedLine, totalLines);
		return;
	}

	std::vector<std::thread> workers;
	workers.reserve(chunks.size());
	for (std::size_t i = 0; i < chunks.size(); ++i)
		workers.emplace_back([data, &chunks, i]() { countDirectLineIndexChunkBreaks(data, chunks[i]); });
	for (std::thread &worker : workers)
		worker.join();

	std::size_t totalBreaks = 0;
	for (DirectLineIndexChunk &chunk : chunks) {
		chunk.startLineIndex = totalBreaks;
		totalBreaks += chunk.breakCount;
	}

	workers.clear();
	workers.reserve(chunks.size());
	for (std::size_t i = 0; i < chunks.size(); ++i)
		workers.emplace_back([data, length, &chunks, i]() { collectDirectLineIndexChunkCheckpoints(data, length, chunks[i]); });
	for (std::thread &worker : workers)
		worker.join();

	checkpoints.clear();
	checkpoints.reserve(1 + totalBreaks / kLazyLineIndexStride);
	checkpoints.push_back(LineIndexCheckpoint(0, 0));
	for (const DirectLineIndexChunk &chunk : chunks)
		for (const LineIndexCheckpoint &checkpoint : chunk.checkpoints)
			checkpoints.push_back(checkpoint);

	totalLines = totalBreaks + 1;
	indexedLine = totalLines - 1;
	indexedOffset = chunks.back().lastLineStart;
}

Offset directFindNextLineBreak(const char *data, Offset length, Offset start) noexcept {
	if (data == nullptr || start >= length) return length;

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
	const __m128i cr = _mm_set1_epi8('\r');
	const __m128i lf = _mm_set1_epi8('\n');
	Offset i = start;

	for (; i + 16 <= length; i += 16) {
		const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + i));
		const __m128i maskVec = _mm_or_si128(_mm_cmpeq_epi8(bytes, cr), _mm_cmpeq_epi8(bytes, lf));
		unsigned int mask = static_cast<unsigned int>(_mm_movemask_epi8(maskVec));
		if (mask != 0) return i + static_cast<Offset>(__builtin_ctz(mask));
	}
	for (; i < length; ++i)
		if (isLineBreakByte(data[i])) return i;
	return length;
#else
	for (Offset i = start; i < length; ++i)
		if (isLineBreakByte(data[i])) return i;
	return length;
#endif
}

Offset directFindPrevLineBreak(const char *data, Offset endExclusive) noexcept {
	if (data == nullptr || endExclusive == 0) return static_cast<Offset>(-1);

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
		if (isLineBreakByte(data[j - 1])) return j - 1;
	return static_cast<Offset>(-1);
#else
	for (Offset i = endExclusive; i > 0; --i)
		if (isLineBreakByte(data[i - 1])) return i - 1;
	return static_cast<Offset>(-1);
#endif
}

std::size_t directCountLineBreaksInRange(const char *data, Offset length, Offset start, Offset end) noexcept {
	if (data == nullptr || length == 0) return 0;
	start = std::min(start, length);
	end = std::min(end, length);
	if (end <= start) return 0;
	bool prevWasCR = start > 0 && data[start - 1] == '\r';
	return countLineBreaksChunk(data + start, end - start, prevWasCR);
}

std::size_t countLineBreaksChunk(const char *data, Offset length, bool &prevWasCR) noexcept {
	if (data == nullptr || length == 0) return 0;
#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
	return countLineBreaksSse2(data, length, prevWasCR);
#else
	return countLineBreaksScalar(data, length, prevWasCR);
#endif
}

void appendLineStartsFromInsertedText(std::vector<Offset> &starts, Offset baseOffset, std::string_view text, bool includeTerminalStart) {
	bool endedWithBreak = false;
	for (std::size_t i = 0; i < text.size();) {
		if (text[i] == '\r') {
			++i;
			if (i < text.size() && text[i] == '\n') ++i;
			endedWithBreak = true;
			if (i < text.size()) starts.push_back(baseOffset + static_cast<Offset>(i));
			continue;
		}
		if (text[i] == '\n') {
			++i;
			endedWithBreak = true;
			if (i < text.size()) starts.push_back(baseOffset + static_cast<Offset>(i));
			continue;
		}
		endedWithBreak = false;
		++i;
	}
	if (includeTerminalStart && endedWithBreak && !text.empty()) starts.push_back(baseOffset + static_cast<Offset>(text.size()));
}

} // namespace lineindex

bool TextDocument::adoptLineIndexWarmup(const LineIndexWarmupData &warmup, std::size_t expectedVersion) noexcept {
	if (!matchesVersion(expectedVersion)) return false;
	if (warmup.checkpoints.empty()) return false;

	const bool currentComplete = mLazyLineIndexComplete;
	const Offset currentOffset = mLazyIndexedOffset;
	const std::size_t currentCheckpointCount = mLineIndexCheckpoints.size();

	const bool incomingBetter = !currentComplete && warmup.lazyLineIndexComplete;
	const bool incomingFurther = warmup.lazyIndexedOffset > currentOffset;
	const bool incomingDenser = warmup.checkpoints.size() > currentCheckpointCount;

	if (!incomingBetter && !incomingFurther && !incomingDenser) return false;

	mLineIndexCheckpoints = warmup.checkpoints;
	mLazyIndexedOffset = warmup.lazyIndexedOffset;
	mLazyIndexedLine = warmup.lazyIndexedLine;
	mLazyLineIndexComplete = warmup.lazyLineIndexComplete;
	mLazyTotalLineCount = warmup.lazyTotalLineCount;
	if (mLineIndexCheckpoints.empty()) resetLazyLineIndex();
	return true;
}

ReadSnapshot::ReadSnapshot() noexcept : mDocumentId(0), mVersion(0), mLength(0), mCacheDirty(false), mLazyIndexedOffset(0), mLazyIndexedLine(0), mLazyLineIndexComplete(true), mLazyTotalLineCount(1) {
	resetLazyLineIndex();
}

bool ReadSnapshot::hasEditedLineStartIndex() const noexcept {
	return mEditedLineStarts != nullptr && !mEditedLineStarts->empty();
}

Offset ReadSnapshot::length() const noexcept {
	return mLength;
}

bool ReadSnapshot::empty() const noexcept {
	return mLength == 0;
}

char ReadSnapshot::charAt(Offset pos) const noexcept {
	if (const char *data = directTextData()) return pos < mLength ? data[pos] : '\0';
	return piecewiseCharAt(*this, pos);
}

std::string ReadSnapshot::text() const {
	ensureMaterialized();
	return mMaterializedText;
}

std::size_t ReadSnapshot::addBufferLength() const noexcept {
	return mAddBuffer != nullptr ? mAddBuffer->size() : 0;
}

std::size_t ReadSnapshot::pieceCount() const noexcept {
	return mPieces != nullptr ? mPieces->size() : 0;
}

PieceChunkView ReadSnapshot::pieceChunk(std::size_t index) const noexcept {
	if (mPieces == nullptr || index >= mPieces->size()) return PieceChunkView();

	const Piece &piece = (*mPieces)[index];
	if (piece.empty()) return PieceChunkView();
	if (piece.source == BufferKind::Original) {
		const char *base = originalData();
		if (base == nullptr) return PieceChunkView();
		return PieceChunkView(base + piece.span.start, piece.span.length);
	}
	if (mAddBuffer == nullptr || piece.span.start >= mAddBuffer->size()) return PieceChunkView();
	return PieceChunkView(mAddBuffer->data() + piece.span.start, piece.span.length);
}

Offset ReadSnapshot::clampOffset(Offset pos) const noexcept {
	return std::min(pos, mLength);
}

std::size_t ReadSnapshot::lineCount() const noexcept {
	if (hasEditedLineStartIndex()) return mEditedLineStarts->size();
	ensureLazyIndexComplete();
	return mLazyTotalLineCount;
}

Offset ReadSnapshot::lineStart(Offset pos) const noexcept {
	if (hasEditedLineStartIndex()) return lineStartFromExactStarts(*mEditedLineStarts, lineIndex(pos));
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
		return directFindNextLineBreak(data, mLength, pos);
	}

	return piecewiseLineEnd(*this, pos);
}

Offset ReadSnapshot::nextLine(Offset pos) const noexcept {
	if (hasEditedLineStartIndex()) {
		const std::size_t line = lineIndex(pos);
		return line + 1 < mEditedLineStarts->size() ? (*mEditedLineStarts)[line + 1] : mLength;
	}
	if (const char *data = directTextData()) {
		pos = lineEnd(pos);
		if (pos < mLength) {
			if (data[pos] == '\r' && pos + 1 < mLength && data[pos + 1] == '\n') pos += 2;
			else
				++pos;
		}
		return pos;
	}

	return piecewiseNextLine(*this, pos);
}

Offset ReadSnapshot::prevLine(Offset pos) const noexcept {
	if (hasEditedLineStartIndex()) {
		const std::size_t line = lineIndex(pos);
		return line == 0 ? 0 : (*mEditedLineStarts)[line - 1];
	}
	if (const char *data = directTextData()) {
		pos = lineStart(pos);
		if (pos == 0) return 0;
		--pos;
		if (pos > 0 && data[pos - 1] == '\r' && data[pos] == '\n') --pos;
		Offset breakPos = directFindPrevLineBreak(data, pos);
		return breakPos == static_cast<Offset>(-1) ? 0 : breakPos + 1;
	}

	return piecewisePrevLine(*this, pos);
}

std::size_t ReadSnapshot::lineIndex(Offset pos) const noexcept {
	pos = clampOffset(pos);
	if (hasEditedLineStartIndex()) {
		if (pos == mLength && mLength > 0 && isLineBreakChar(charAt(mLength - 1))) return mEditedLineStarts->size() - 1;
		const Offset lookupPos = pos == mLength && mLength > 0 ? mLength - 1 : pos;
		return lineIndexFromExactStarts(*mEditedLineStarts, lookupPos);
	}
	ensureLazyIndexSeeded();
	if (mLineIndexCheckpoints.empty()) return 0;
	if (pos == mLength) {
		ensureLazyIndexComplete();
		return mLazyTotalLineCount > 0 ? mLazyTotalLineCount - 1 : 0;
	}

	std::size_t left = 0;
	std::size_t right = mLineIndexCheckpoints.size();
	while (left < right) {
		std::size_t mid = left + (right - left) / 2;
		if (mLineIndexCheckpoints[mid].offset <= pos) left = mid + 1;
		else
			right = mid;
	}
	LineIndexCheckpoint checkpoint = mLineIndexCheckpoints[left == 0 ? 0 : static_cast<std::size_t>(left - 1)];
	if (checkpoint.offset >= pos) return checkpoint.lineIndex;
	if (const char *data = directTextData()) {
		const std::size_t delta = directCountLineBreaksInRange(data, mLength, checkpoint.offset, pos);
		return checkpoint.lineIndex + delta;
	}
	return checkpoint.lineIndex + piecewiseCountLineBreaksInRange(*this, checkpoint.offset, pos);
}

Offset ReadSnapshot::lineStartByIndex(std::size_t index) const noexcept {
	if (hasEditedLineStartIndex()) return lineStartFromExactStarts(*mEditedLineStarts, index);
	ensureLazyIndexSeeded();
	if (mLineIndexCheckpoints.empty()) return 0;
	if (directTextData() != nullptr || mLazyLineIndexComplete || index <= mLineIndexCheckpoints.back().lineIndex + kLazyLineStartCatchupWindow) ensureLazyIndexForLine(index);
	if (mLineIndexCheckpoints.empty()) return 0;
	return localInterpolatedLineStartByIndex(*this, index, mLineIndexCheckpoints, mLazyLineIndexComplete, mLazyTotalLineCount);
}

std::size_t ReadSnapshot::estimatedLineCount() const noexcept {
	if (hasEditedLineStartIndex()) return mEditedLineStarts->size();
	ensureLazyIndexSeeded();
	if (mLazyLineIndexComplete) return mLazyTotalLineCount;
	if (!mMappedOriginal.mapped()) return piecewiseLineCount(*this);
	if (mLazyIndexedOffset == 0 || mLazyIndexedLine == 0) return std::max<std::size_t>(1, mLength / 80 + 1);

	const std::size_t observedLines = mLazyIndexedLine + 1;
	const std::size_t estimated = static_cast<std::size_t>((static_cast<long double>(mLength) * observedLines) / std::max<Offset>(mLazyIndexedOffset, 1));
	return std::max<std::size_t>(observedLines, estimated);
}

bool ReadSnapshot::exactLineCountKnown() const noexcept {
	if (hasEditedLineStartIndex()) return true;
	return !mMappedOriginal.mapped() || mLazyLineIndexComplete;
}

void ReadSnapshot::dropExactLineStartIndex() noexcept {
	mEditedLineStarts.reset();
}

std::size_t ReadSnapshot::column(Offset pos) const noexcept {
	pos = clampOffset(pos);
	return pos - lineStart(pos);
}

std::string ReadSnapshot::lineText(Offset pos) const {
	Offset start = lineStart(pos);
	Offset end = lineEnd(pos);
	if (const char *data = directTextData()) return std::string(data + start, end - start);
	return piecewiseRangeText(*this, start, end);
}

LineIndexWarmupData ReadSnapshot::completeLineIndexWarmup() const {
	LineIndexWarmupData warmup;
	(void)completeLineIndexWarmup(warmup, std::stop_token());
	return warmup;
}

bool ReadSnapshot::warmLineIndexChunk(LineIndexWarmupData &warmup, std::size_t maxStrides, std::stop_token stopToken, const std::atomic_bool *cancelFlag) const {
	ensureLazyIndexSeeded();
	if (maxStrides == 0) maxStrides = 1;
	for (std::size_t strideIndex = 0; strideIndex < maxStrides && !mLazyLineIndexComplete; ++strideIndex) {
		if (stopToken.stop_requested() || (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire))) return false;
		advanceLazyIndexByStride();
	}
	if (stopToken.stop_requested() || (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire))) return false;
	warmup.checkpoints = mLineIndexCheckpoints;
	warmup.lazyIndexedOffset = mLazyIndexedOffset;
	warmup.lazyIndexedLine = mLazyIndexedLine;
	warmup.lazyLineIndexComplete = mLazyLineIndexComplete;
	warmup.lazyTotalLineCount = mLazyTotalLineCount;
	return true;
}

bool ReadSnapshot::completeLineIndexWarmup(LineIndexWarmupData &warmup, std::stop_token stopToken, const std::atomic_bool *cancelFlag) const {
	return warmLineIndexChunk(warmup, std::numeric_limits<std::size_t>::max(), stopToken, cancelFlag);
}

bool ReadSnapshot::isLineBreakChar(char ch) const noexcept {
	return ch == '\n' || ch == '\r';
}

bool ReadSnapshot::hasDirectOriginalView() const noexcept {
	if (mPieces == nullptr || mPieces->size() != 1) return false;

	const Piece &piece = (*mPieces)[0];
	if (piece.span.start != 0 || piece.span.length != mLength) return false;
	if (piece.source == BufferKind::Original) return originalData() != nullptr;
	if (piece.source == BufferKind::Add) return mAddBuffer != nullptr;
	return false;
}

const char *ReadSnapshot::directTextData() const noexcept {
	if (!hasDirectOriginalView() || mPieces == nullptr || mPieces->empty()) return nullptr;

	const Piece &piece = (*mPieces)[0];
	if (piece.source == BufferKind::Original) return originalData();
	return mAddBuffer != nullptr ? mAddBuffer->data() : nullptr;
}

const char *ReadSnapshot::originalData() const noexcept {
	if (mMappedOriginal.mapped()) return mMappedOriginal.data();
	return mOriginalBuffer != nullptr ? mOriginalBuffer->data() : nullptr;
}

void ReadSnapshot::resetLazyLineIndex() noexcept {
	mLineIndexCheckpoints.clear();
	mLineIndexCheckpoints.push_back(LineIndexCheckpoint(0, 0));
	mLazyIndexedOffset = 0;
	mLazyIndexedLine = 0;
	mLazyLineIndexComplete = (mLength == 0);
	mLazyTotalLineCount = 1;
}

bool ReadSnapshot::advanceLine(Offset &offset) const noexcept {
	if (directAdvanceLine(offset)) return true;
	return piecewiseAdvanceLine(*this, offset);
}

bool ReadSnapshot::directAdvanceLine(Offset &offset) const noexcept {
	const char *data = directTextData();
	if (data == nullptr) return false;
	if (offset >= mLength) return false;

	Offset breakPos = directFindNextLineBreak(data, mLength, offset);
	if (breakPos >= mLength) return false;
	if (data[breakPos] == '\r' && breakPos + 1 < mLength && data[breakPos + 1] == '\n') offset = breakPos + 2;
	else
		offset = breakPos + 1;
	return true;
}

void ReadSnapshot::ensureLazyIndexSeeded() const noexcept {
	if (mLineIndexCheckpoints.empty()) const_cast<ReadSnapshot *>(this)->resetLazyLineIndex();
}

void ReadSnapshot::advanceLazyIndexByStride() const noexcept {
	ensureLazyIndexSeeded();
	if (mLazyLineIndexComplete) return;
	const std::size_t checkpointStride = checkpointStrideForMode(directTextData() != nullptr);

	for (std::size_t steps = 0; steps < kLazyLineIndexStride; ++steps) {
		Offset next = mLazyIndexedOffset;
		if (!advanceLine(next)) {
			mLazyLineIndexComplete = true;
			mLazyTotalLineCount = mLazyIndexedLine + 1;
			return;
		}
		mLazyIndexedOffset = next;
		++mLazyIndexedLine;
		if ((mLazyIndexedLine % checkpointStride) == 0) mLineIndexCheckpoints.push_back(LineIndexCheckpoint(mLazyIndexedOffset, mLazyIndexedLine));
	}
}

void ReadSnapshot::ensureLazyIndexForLine(std::size_t targetLine) const noexcept {
	ensureLazyIndexSeeded();
	while (!mLazyLineIndexComplete && mLineIndexCheckpoints.back().lineIndex < targetLine)
		advanceLazyIndexByStride();
}

void ReadSnapshot::ensureLazyIndexForOffset(Offset targetOffset) const noexcept {
	ensureLazyIndexSeeded();
	targetOffset = clampOffset(targetOffset);
	while (!mLazyLineIndexComplete && mLineIndexCheckpoints.back().offset <= targetOffset)
		advanceLazyIndexByStride();
}

void ReadSnapshot::ensureLazyIndexComplete() const noexcept {
	ensureLazyIndexSeeded();
	if (!mLazyLineIndexComplete && mLineIndexCheckpoints.size() == 1 && mLineIndexCheckpoints[0].offset == 0 && mLineIndexCheckpoints[0].lineIndex == 0 && mLazyIndexedOffset == 0 && mLazyIndexedLine == 0) {
		if (const char *data = directTextData()) {
			buildDirectInitialLineIndex(data, mLength, mLineIndexCheckpoints, mLazyIndexedOffset, mLazyIndexedLine, mLazyTotalLineCount);
			mLazyLineIndexComplete = true;
			return;
		}
	}
	while (!mLazyLineIndexComplete)
		advanceLazyIndexByStride();
}

std::string ReadSnapshot::pieceText(const Piece &piece) const {
	if (piece.source == BufferKind::Original) {
		if (mMappedOriginal.mapped()) return mMappedOriginal.sliceText(piece.span);
		if (mOriginalBuffer != nullptr) return mOriginalBuffer->substr(piece.span.start, piece.span.length);
		return std::string();
	}
	return mAddBuffer != nullptr ? mAddBuffer->substr(piece.span.start, piece.span.length) : std::string();
}

void ReadSnapshot::ensureMaterialized() const noexcept {
	if (!mCacheDirty) return;

	mMaterializedText.clear();
	mMaterializedText.reserve(mLength);
	if (mPieces != nullptr)
		for (std::size_t i = 0; i < mPieces->size(); ++i)
			mMaterializedText += pieceText((*mPieces)[i]);
	mCacheDirty = false;
}

char TextDocument::charAt(Offset pos) const noexcept {
	if (const char *data = directTextData()) return pos < mLength ? data[pos] : '\0';
	return piecewiseCharAt(*this, pos);
}

ReadSnapshot TextDocument::readSnapshot() const {
	const auto startedAt = std::chrono::steady_clock::now();
	ReadSnapshot snapshot;
	snapshot.mDocumentId = mDocumentId;
	snapshot.mVersion = mVersion;
	snapshot.mMappedOriginal = mMappedOriginal;
	snapshot.mOriginalBuffer = mOriginalBuffer;
	snapshot.mAddBuffer = mAddBuffer.sharedText();
	snapshot.mPieces = mPieces;
	snapshot.mLength = mLength;
	snapshot.mCacheDirty = mCacheDirty;
	snapshot.mMaterializedText = mCacheDirty ? std::string() : mMaterializedText;
	snapshot.mLineIndexCheckpoints = mLineIndexCheckpoints;
	snapshot.mLazyIndexedOffset = mLazyIndexedOffset;
	snapshot.mLazyIndexedLine = mLazyIndexedLine;
	snapshot.mLazyLineIndexComplete = mLazyLineIndexComplete;
	snapshot.mLazyTotalLineCount = mLazyTotalLineCount;
	snapshot.mEditedLineStarts = mEditedLineStarts;
	if (snapshot.mLineIndexCheckpoints.empty()) snapshot.resetLazyLineIndex();
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc readSnapshot total_us=" << traceMicros(totalElapsed) << " len=" << mLength << " add=" << mAddBuffer.size() << " pieces=" << pieceCount() << " exact_lines="
		     << (mEditedLineStarts != nullptr ? mEditedLineStarts->size() : 0) << " snap_exact_lines=" << (snapshot.mEditedLineStarts != nullptr ? snapshot.mEditedLineStarts->size() : 0) << " lazy_checkpoints=" << snapshot.mLineIndexCheckpoints.size() << " lazy_complete="
		     << (snapshot.mLazyLineIndexComplete ? 1 : 0);
		appendDocumentTrace(line.str());
	}
	return snapshot;
}

void TextDocument::restoreFromSnapshot(const ReadSnapshot &snapshot) {
	if (snapshot.empty() && snapshot.length() == 0) {
		setTextNoVersionBump("");
	} else {
		mDocumentId = snapshot.mDocumentId;
		mVersion = snapshot.mVersion;
		mMappedOriginal = snapshot.mMappedOriginal;
		mOriginalBuffer = snapshot.mOriginalBuffer != nullptr ? std::const_pointer_cast<std::string>(snapshot.mOriginalBuffer) : std::make_shared<std::string>();
		mAddBuffer.setSharedText(snapshot.mAddBuffer);
		mPieces = snapshot.mPieces != nullptr ? std::const_pointer_cast<std::vector<Piece>>(snapshot.mPieces) : std::make_shared<std::vector<Piece>>();

		mLength = snapshot.mLength;
		mCacheDirty = snapshot.mCacheDirty;
		mMaterializedText = snapshot.mMaterializedText;
		mLineIndexCheckpoints = snapshot.mLineIndexCheckpoints;
		mLazyIndexedOffset = snapshot.mLazyIndexedOffset;
		mLazyIndexedLine = snapshot.mLazyIndexedLine;
		mLazyLineIndexComplete = snapshot.mLazyLineIndexComplete;
		mLazyTotalLineCount = snapshot.mLazyTotalLineCount;
		mEditedLineStarts = snapshot.mEditedLineStarts != nullptr ? std::const_pointer_cast<std::vector<Offset>>(snapshot.mEditedLineStarts) : std::shared_ptr<std::vector<Offset>>();
	}
}

Offset TextDocument::clampOffset(Offset pos) const noexcept {
	return std::min(pos, mLength);
}

std::size_t TextDocument::lineCount() const noexcept {
	const auto startedAt = std::chrono::steady_clock::now();
	if (hasEditedLineStartIndex()) {
		const std::size_t result = mEditedLineStarts->size();
		const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
		if (totalElapsed >= kSlowDocumentTraceThreshold) {
			std::ostringstream line;
			line << "Phase1 doc lineCount total_us=" << traceMicros(totalElapsed) << " result=" << result << " exact_lines=" << result << " len=" << mLength << " add=" << mAddBuffer.size()
			     << " pieces=" << pieceCount();
			appendDocumentTrace(line.str());
		}
		return result;
	}
	ensureLazyIndexComplete();
	const std::size_t result = mLazyTotalLineCount;
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc lineCount total_us=" << traceMicros(totalElapsed) << " result=" << result << " lazy_complete=" << (mLazyLineIndexComplete ? 1 : 0)
		     << " lazy_indexed_line=" << mLazyIndexedLine << " checkpoints=" << mLineIndexCheckpoints.size() << " len=" << mLength << " add=" << mAddBuffer.size()
		     << " pieces=" << pieceCount();
		appendDocumentTrace(line.str());
	}
	return result;
}

Offset TextDocument::lineStart(Offset pos) const noexcept {
	const auto startedAt = std::chrono::steady_clock::now();
	const Offset requestedPos = pos;
	const bool direct = directTextData() != nullptr;
	Offset result = 0;
	if (hasEditedLineStartIndex()) result = lineStartFromExactStarts(*mEditedLineStarts, lineIndex(pos));
	else if (const char *data = directTextData()) {
		pos = clampOffset(pos);
		Offset breakPos = directFindPrevLineBreak(data, pos);
		result = breakPos == static_cast<Offset>(-1) ? 0 : breakPos + 1;
	} else
		result = piecewiseLineStart(*this, pos);
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc lineStart total_us=" << traceMicros(totalElapsed) << " pos=" << requestedPos << " result=" << result << " len=" << mLength << " add=" << mAddBuffer.size() << " pieces=" << pieceCount()
		     << " direct=" << (direct ? 1 : 0);
		appendDocumentTrace(line.str());
	}
	return result;
}

Offset TextDocument::lineEnd(Offset pos) const noexcept {
	if (const char *data = directTextData()) {
		pos = clampOffset(pos);
		return directFindNextLineBreak(data, mLength, pos);
	}

	return piecewiseLineEnd(*this, pos);
}

Offset TextDocument::nextLine(Offset pos) const noexcept {
	const auto startedAt = std::chrono::steady_clock::now();
	const Offset requestedPos = pos;
	const bool direct = directTextData() != nullptr;
	Offset result = 0;
	if (hasEditedLineStartIndex()) {
		const std::size_t line = lineIndex(pos);
		result = line + 1 < mEditedLineStarts->size() ? (*mEditedLineStarts)[line + 1] : mLength;
	} else if (const char *data = directTextData()) {
		pos = lineEnd(pos);
		if (pos < mLength) {
			if (data[pos] == '\r' && pos + 1 < mLength && data[pos + 1] == '\n') pos += 2;
			else
				++pos;
		}
		result = pos;
	} else
		result = piecewiseNextLine(*this, pos);
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc nextLine total_us=" << traceMicros(totalElapsed) << " pos=" << requestedPos << " result=" << result << " len=" << mLength << " add=" << mAddBuffer.size() << " pieces=" << pieceCount()
		     << " direct=" << (direct ? 1 : 0);
		appendDocumentTrace(line.str());
	}
	return result;
}

Offset TextDocument::prevLine(Offset pos) const noexcept {
	const auto startedAt = std::chrono::steady_clock::now();
	const Offset requestedPos = pos;
	const bool direct = directTextData() != nullptr;
	Offset result = 0;
	if (hasEditedLineStartIndex()) {
		const std::size_t line = lineIndex(pos);
		result = line == 0 ? 0 : (*mEditedLineStarts)[line - 1];
	} else if (const char *data = directTextData()) {
		pos = lineStart(pos);
		if (pos == 0) result = 0;
		else {
			--pos;
			if (pos > 0 && data[pos - 1] == '\r' && data[pos] == '\n') --pos;
			Offset breakPos = directFindPrevLineBreak(data, pos);
			result = breakPos == static_cast<Offset>(-1) ? 0 : breakPos + 1;
		}
	} else
		result = piecewisePrevLine(*this, pos);
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc prevLine total_us=" << traceMicros(totalElapsed) << " pos=" << requestedPos << " result=" << result << " len=" << mLength << " add=" << mAddBuffer.size() << " pieces=" << pieceCount()
		     << " direct=" << (direct ? 1 : 0);
		appendDocumentTrace(line.str());
	}
	return result;
}

std::size_t TextDocument::lineIndex(Offset pos) const noexcept {
	const auto startedAt = std::chrono::steady_clock::now();
	const Offset requestedPos = pos;
	pos = clampOffset(pos);
	if (hasEditedLineStartIndex()) {
		if (pos == mLength && mLength > 0 && isLineBreakChar(charAt(mLength - 1))) {
			const std::size_t result = mEditedLineStarts->size() - 1;
			const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
			if (totalElapsed >= kSlowDocumentTraceThreshold) {
				std::ostringstream line;
				line << "Phase1 doc lineIndex total_us=" << traceMicros(totalElapsed) << " pos=" << requestedPos << " result=" << result << " exact_lines=" << mEditedLineStarts->size()
				     << " len=" << mLength << " add=" << mAddBuffer.size() << " pieces=" << pieceCount() << " direct=0 eof_trailing_break=1";
				appendDocumentTrace(line.str());
			}
			return result;
		}
		const Offset lookupPos = pos == mLength && mLength > 0 ? mLength - 1 : pos;
		const std::size_t result = lineIndexFromExactStarts(*mEditedLineStarts, lookupPos);
		const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
		if (totalElapsed >= kSlowDocumentTraceThreshold) {
			std::ostringstream line;
			line << "Phase1 doc lineIndex total_us=" << traceMicros(totalElapsed) << " pos=" << requestedPos << " result=" << result << " exact_lines=" << mEditedLineStarts->size()
			     << " len=" << mLength << " add=" << mAddBuffer.size() << " pieces=" << pieceCount() << " direct=0";
			appendDocumentTrace(line.str());
		}
		return result;
	}
	ensureLazyIndexSeeded();
	std::size_t result = 0;
	if (mLineIndexCheckpoints.empty()) return 0;
	if (pos == mLength) {
		ensureLazyIndexComplete();
		result = mLazyTotalLineCount > 0 ? mLazyTotalLineCount - 1 : 0;
		const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
		if (totalElapsed >= kSlowDocumentTraceThreshold) {
			std::ostringstream line;
			line << "Phase1 doc lineIndex total_us=" << traceMicros(totalElapsed) << " pos=" << requestedPos << " result=" << result << " checkpoints=" << mLineIndexCheckpoints.size()
			     << " lazy_complete=" << (mLazyLineIndexComplete ? 1 : 0) << " len=" << mLength << " add=" << mAddBuffer.size() << " pieces=" << pieceCount() << " direct=" << (directTextData() != nullptr ? 1 : 0);
			appendDocumentTrace(line.str());
		}
		return result;
	}

	std::size_t left = 0;
	std::size_t right = mLineIndexCheckpoints.size();
	while (left < right) {
		std::size_t mid = left + (right - left) / 2;
		if (mLineIndexCheckpoints[mid].offset <= pos) left = mid + 1;
		else
			right = mid;
	}
	LineIndexCheckpoint checkpoint = mLineIndexCheckpoints[left == 0 ? 0 : static_cast<std::size_t>(left - 1)];
	if (checkpoint.offset >= pos) result = checkpoint.lineIndex;
	else if (const char *data = directTextData()) {
		const std::size_t delta = directCountLineBreaksInRange(data, mLength, checkpoint.offset, pos);
		result = checkpoint.lineIndex + delta;
	} else
		result = checkpoint.lineIndex + piecewiseCountLineBreaksInRange(*this, checkpoint.offset, pos);
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc lineIndex total_us=" << traceMicros(totalElapsed) << " pos=" << requestedPos << " result=" << result << " checkpoint_offset=" << checkpoint.offset
		     << " checkpoint_line=" << checkpoint.lineIndex << " checkpoints=" << mLineIndexCheckpoints.size() << " lazy_complete=" << (mLazyLineIndexComplete ? 1 : 0) << " len=" << mLength
		     << " add=" << mAddBuffer.size() << " pieces=" << pieceCount() << " direct=" << (directTextData() != nullptr ? 1 : 0);
		appendDocumentTrace(line.str());
	}
	return result;
}

Offset TextDocument::lineStartByIndex(std::size_t index) const noexcept {
	const auto startedAt = std::chrono::steady_clock::now();
	const std::size_t requestedIndex = index;
	if (hasEditedLineStartIndex()) {
		const Offset cursor = lineStartFromExactStarts(*mEditedLineStarts, index);
		const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
		if (totalElapsed >= kSlowDocumentTraceThreshold) {
			std::ostringstream trace;
			trace << "Phase1 doc lineStartByIndex total_us=" << traceMicros(totalElapsed) << " requested_index=" << requestedIndex << " resolved_index=" << index << " result=" << cursor
			      << " exact_lines=" << mEditedLineStarts->size() << " len=" << mLength << " add=" << mAddBuffer.size() << " pieces=" << pieceCount() << " direct=0";
			appendDocumentTrace(trace.str());
		}
		return cursor;
	}
	ensureLazyIndexSeeded();
	if (mLineIndexCheckpoints.empty()) return 0;
	if (directTextData() != nullptr || mLazyLineIndexComplete || index <= mLineIndexCheckpoints.back().lineIndex + kLazyLineStartCatchupWindow) ensureLazyIndexForLine(index);
	if (mLineIndexCheckpoints.empty()) return 0;
	if (mLazyLineIndexComplete && index >= mLazyTotalLineCount) index = mLazyTotalLineCount > 0 ? mLazyTotalLineCount - 1 : 0;
	const Offset cursor = localInterpolatedLineStartByIndex(*this, index, mLineIndexCheckpoints, mLazyLineIndexComplete, mLazyTotalLineCount);
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream trace;
		trace << "Phase1 doc lineStartByIndex total_us=" << traceMicros(totalElapsed) << " requested_index=" << requestedIndex << " resolved_index=" << index << " result=" << cursor
		      << " checkpoints=" << mLineIndexCheckpoints.size() << " lazy_complete=" << (mLazyLineIndexComplete ? 1 : 0) << " len=" << mLength << " add=" << mAddBuffer.size() << " pieces=" << pieceCount()
		      << " direct=" << (directTextData() != nullptr ? 1 : 0);
		appendDocumentTrace(trace.str());
	}
	return cursor;
}

std::size_t TextDocument::estimatedLineCount() const noexcept {
	if (hasEditedLineStartIndex()) return mEditedLineStarts->size();
	ensureLazyIndexSeeded();
	if (mLazyLineIndexComplete) return mLazyTotalLineCount;
	if (!mMappedOriginal.mapped()) return piecewiseLineCount(*this);
	if (mLazyIndexedOffset == 0 || mLazyIndexedLine == 0) return std::max<std::size_t>(1, mLength / 80 + 1);

	const std::size_t observedLines = mLazyIndexedLine + 1;
	const std::size_t estimated = static_cast<std::size_t>((static_cast<long double>(mLength) * observedLines) / std::max<Offset>(mLazyIndexedOffset, 1));
	return std::max<std::size_t>(observedLines, estimated);
}

bool TextDocument::exactLineCountKnown() const noexcept {
	if (hasEditedLineStartIndex()) return true;
	return !mMappedOriginal.mapped() || mLazyLineIndexComplete;
}

std::size_t TextDocument::column(Offset pos) const noexcept {
	const auto startedAt = std::chrono::steady_clock::now();
	const Offset requestedPos = pos;
	pos = clampOffset(pos);
	const std::size_t result = pos - lineStart(pos);
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc column total_us=" << traceMicros(totalElapsed) << " pos=" << requestedPos << " result=" << result << " len=" << mLength << " add=" << mAddBuffer.size() << " pieces=" << pieceCount()
		     << " direct=" << (directTextData() != nullptr ? 1 : 0);
		appendDocumentTrace(line.str());
	}
	return result;
}

std::string TextDocument::lineText(Offset pos) const {
	const auto startedAt = std::chrono::steady_clock::now();
	const Offset requestedPos = pos;
	Offset start = lineStart(pos);
	Offset end = lineEnd(pos);
	std::string result;
	if (const char *data = directTextData()) result = std::string(data + start, end - start);
	else
		result = piecewiseRangeText(*this, start, end);
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc lineText total_us=" << traceMicros(totalElapsed) << " pos=" << requestedPos << " start=" << start << " end=" << end << " bytes=" << result.size() << " len=" << mLength
		     << " add=" << mAddBuffer.size() << " pieces=" << pieceCount() << " direct=" << (directTextData() != nullptr ? 1 : 0);
		appendDocumentTrace(line.str());
	}
	return result;
}

bool TextDocument::isLineBreakChar(char ch) const noexcept {
	return ch == '\n' || ch == '\r';
}

void TextDocument::initializeFromOriginal(std::string_view text, bool bumpVersionFlag) {
	mOriginalBuffer = std::make_shared<std::string>(text.data(), text.size());
	mMappedOriginal.reset();
	mAddBuffer.clear();
	ensureUniquePieces();
	mPieces->clear();
	mLength = mOriginalBuffer->size();
	if (!mOriginalBuffer->empty()) mPieces->push_back(Piece(BufferKind::Original, TextSpan(0, mOriginalBuffer->size())));
	mMaterializedText = *mOriginalBuffer;
	mCacheDirty = false;
	if (const char *data = directTextData()) {
		buildDirectInitialLineIndex(data, mLength, mLineIndexCheckpoints, mLazyIndexedOffset, mLazyIndexedLine, mLazyTotalLineCount);
		mLazyLineIndexComplete = true;
	} else
		resetLazyLineIndex();
	rebuildEditedLineStartIndex();
	if (bumpVersionFlag) bumpVersion();
}

void TextDocument::initializeFromMappedSource(const MappedFileSource &source, bool bumpVersionFlag) {
	mOriginalBuffer = std::make_shared<std::string>();
	mMappedOriginal = source;
	mAddBuffer.clear();
	ensureUniquePieces();
	mPieces->clear();
	mLength = mMappedOriginal.size();
	mMaterializedText.clear();
	mCacheDirty = mLength != 0;
	if (mLength != 0) mPieces->push_back(Piece(BufferKind::Original, TextSpan(0, mLength)));
	if (const char *data = directTextData()) {
		buildDirectInitialLineIndex(data, mLength, mLineIndexCheckpoints, mLazyIndexedOffset, mLazyIndexedLine, mLazyTotalLineCount);
		mLazyLineIndexComplete = true;
	} else
		resetLazyLineIndex();
	mEditedLineStarts.reset();
	if (bumpVersionFlag) bumpVersion();
}

bool TextDocument::hasDirectOriginalView() const noexcept {
	if (mPieces == nullptr || mPieces->size() != 1) return false;

	const Piece &piece = (*mPieces)[0];
	if (piece.span.start != 0 || piece.span.length != mLength) return false;
	if (piece.source == BufferKind::Original) return originalData() != nullptr;
	if (piece.source == BufferKind::Add) return mAddBuffer.size() >= piece.span.length;
	return false;
}

const char *TextDocument::directTextData() const noexcept {
	if (!hasDirectOriginalView() || mPieces == nullptr || mPieces->empty()) return nullptr;

	const Piece &piece = (*mPieces)[0];
	if (piece.source == BufferKind::Original) return originalData();
	return mAddBuffer.text().data();
}

bool TextDocument::hasEditedLineStartIndex() const noexcept {
	return mEditedLineStarts != nullptr && !mEditedLineStarts->empty();
}

void TextDocument::clearEditedLineStartIndex() noexcept {
	mEditedLineStarts = std::make_shared<std::vector<Offset>>(1, 0);
}

void TextDocument::rebuildEditedLineStartIndex() {
	std::shared_ptr<std::vector<Offset>> starts = std::make_shared<std::vector<Offset>>();
	starts->reserve(std::max<std::size_t>(mLineIndexCheckpoints.empty() ? 1 : mLineIndexCheckpoints.back().lineIndex + 1, std::max<std::size_t>(1, mLength / 80 + 1)));
	starts->push_back(0);

	Offset cursor = 0;
	while (cursor < mLength) {
		Offset next = cursor;
		if (!advanceLine(next) || next <= cursor) break;
		starts->push_back(next);
		cursor = next;
	}

	mEditedLineStarts = starts;
}

void TextDocument::updateEditedLineStartIndexForInsert(Offset offset, std::string_view text) {
	const auto startedAt = std::chrono::steady_clock::now();
	if (text.empty()) return;
	if (!hasEditedLineStartIndex()) {
		rebuildEditedLineStartIndex();
		const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
		if (totalElapsed >= kSlowDocumentTraceThreshold) {
			std::ostringstream line;
			line << "Phase1 doc updateEditedLineStartIndexForInsert total_us=" << traceMicros(totalElapsed) << " offset=" << offset << " inserted_bytes=" << text.size()
			     << " mode=rebuild-missing len=" << mLength << " add=" << mAddBuffer.size() << " pieces=" << pieceCount() << " exact_lines="
			     << (mEditedLineStarts != nullptr ? mEditedLineStarts->size() : 0);
			appendDocumentTrace(line.str());
		}
		return;
	}

	if (!mEditedLineStarts.unique()) mEditedLineStarts = std::make_shared<std::vector<Offset>>(*mEditedLineStarts);
	std::vector<Offset> &starts = *mEditedLineStarts;
	std::vector<Offset> insertedStarts;
	appendLineStartsFromInsertedText(insertedStarts, offset, text, true);

	std::vector<Offset>::iterator suffix = std::upper_bound(starts.begin() + 1, starts.end(), offset);
	for (std::vector<Offset>::iterator it = suffix; it != starts.end(); ++it)
		*it += static_cast<Offset>(text.size());
	starts.insert(suffix, insertedStarts.begin(), insertedStarts.end());
	starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc updateEditedLineStartIndexForInsert total_us=" << traceMicros(totalElapsed) << " offset=" << offset << " inserted_bytes=" << text.size()
		     << " inserted_lines=" << insertedStarts.size() << " total_exact_lines=" << mEditedLineStarts->size() << " len=" << mLength << " add=" << mAddBuffer.size() << " pieces=" << pieceCount();
		appendDocumentTrace(line.str());
	}
}

void TextDocument::updateEditedLineStartIndexForErase(Range range) {
	const auto startedAt = std::chrono::steady_clock::now();
	if (range.empty()) return;
	if (!hasEditedLineStartIndex()) {
		rebuildEditedLineStartIndex();
		const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
		if (totalElapsed >= kSlowDocumentTraceThreshold) {
			std::ostringstream line;
			line << "Phase1 doc updateEditedLineStartIndexForErase total_us=" << traceMicros(totalElapsed) << " start=" << range.start << " end=" << range.end
			     << " erased_bytes=" << range.length() << " mode=rebuild-missing len=" << mLength << " add=" << mAddBuffer.size() << " pieces=" << pieceCount()
			     << " exact_lines=" << (mEditedLineStarts != nullptr ? mEditedLineStarts->size() : 0);
			appendDocumentTrace(line.str());
		}
		return;
	}

	if (!mEditedLineStarts.unique()) mEditedLineStarts = std::make_shared<std::vector<Offset>>(*mEditedLineStarts);
	std::vector<Offset> &starts = *mEditedLineStarts;
	const std::size_t baseLineIndex = lineIndexFromExactStarts(starts, range.start);
	const Offset baseOffset = lineStartFromExactStarts(starts, baseLineIndex);
	const Offset erasedBytes = range.length();
	std::vector<Offset>::iterator removedBegin = starts.begin() + static_cast<std::ptrdiff_t>(baseLineIndex + 1);
	std::vector<Offset>::iterator removedEnd = std::upper_bound(removedBegin, starts.end(), range.end);
	const std::size_t suffixIndex = static_cast<std::size_t>(removedBegin - starts.begin());

	starts.erase(removedBegin, removedEnd);
	for (std::size_t i = suffixIndex; i < starts.size(); ++i)
		starts[i] -= erasedBytes;

	const bool hasSuffix = suffixIndex < starts.size();
	const Offset stopOffset = hasSuffix ? starts[suffixIndex] : mLength;
	std::vector<Offset> rebuiltStarts;
	Offset cursor = baseOffset;
	while (cursor < mLength) {
		Offset next = cursor;
		if (!advanceLine(next) || next <= cursor) break;
		if (next > stopOffset) break;
		if (hasSuffix && next == stopOffset) break;
		rebuiltStarts.push_back(next);
		cursor = next;
		if (!hasSuffix && next == stopOffset) break;
	}
	starts.insert(starts.begin() + static_cast<std::ptrdiff_t>(suffixIndex), rebuiltStarts.begin(), rebuiltStarts.end());
	starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowDocumentTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 doc updateEditedLineStartIndexForErase total_us=" << traceMicros(totalElapsed) << " start=" << range.start << " end=" << range.end
		     << " erased_bytes=" << erasedBytes << " rebuilt_lines=" << rebuiltStarts.size() << " total_exact_lines=" << mEditedLineStarts->size() << " len=" << mLength
		     << " add=" << mAddBuffer.size() << " pieces=" << pieceCount();
		appendDocumentTrace(line.str());
	}
}

void TextDocument::normalizeLargeMappedEditStateNoVersionBump() {
	if (!mMappedOriginal.mapped()) return;
	if (mLength < kLargeMappedEditNormalizationLength) return;
	if (mPieces == nullptr || mPieces->size() < kLargeMappedEditNormalizationPieceThreshold) return;

	std::string currentText = text();
	mMappedOriginal.reset();
	mOriginalBuffer = std::make_shared<std::string>();
	mAddBuffer.clear();
	TextSpan fullSpan = mAddBuffer.append(currentText);
	ensureUniquePieces();
	mPieces->clear();
	mPieces->emplace_back(BufferKind::Add, fullSpan);
	mLength = static_cast<Offset>(currentText.size());
	mMaterializedText = std::move(currentText);
	mCacheDirty = false;
	if (const char *data = directTextData()) {
		buildDirectInitialLineIndex(data, mLength, mLineIndexCheckpoints, mLazyIndexedOffset, mLazyIndexedLine, mLazyTotalLineCount);
		mLazyLineIndexComplete = true;
	} else
		resetLazyLineIndex();
	mEditedLineStarts.reset();
}

void TextDocument::resetLazyLineIndex() noexcept {
	mLineIndexCheckpoints.clear();
	mLineIndexCheckpoints.push_back(LineIndexCheckpoint(0, 0));
	mLazyIndexedOffset = 0;
	mLazyIndexedLine = 0;
	mLazyLineIndexComplete = (mLength == 0);
	mLazyTotalLineCount = 1;
}

bool TextDocument::directAdvanceLine(Offset &offset) const noexcept {
	const char *data = directTextData();
	if (data == nullptr) return false;
	if (offset >= mLength) return false;

	Offset breakPos = directFindNextLineBreak(data, mLength, offset);
	if (breakPos >= mLength) return false;
	if (data[breakPos] == '\r' && breakPos + 1 < mLength && data[breakPos + 1] == '\n') offset = breakPos + 2;
	else
		offset = breakPos + 1;
	return true;
}

bool TextDocument::advanceLine(Offset &offset) const noexcept {
	if (directAdvanceLine(offset)) return true;
	return piecewiseAdvanceLine(*this, offset);
}

void TextDocument::ensureLazyIndexSeeded() const noexcept {
	if (mLineIndexCheckpoints.empty()) const_cast<TextDocument *>(this)->resetLazyLineIndex();
}

void TextDocument::advanceLazyIndexByStride() const noexcept {
	ensureLazyIndexSeeded();
	if (mLazyLineIndexComplete) return;
	const std::size_t checkpointStride = checkpointStrideForMode(directTextData() != nullptr);

	for (std::size_t steps = 0; steps < kLazyLineIndexStride; ++steps) {
		Offset next = mLazyIndexedOffset;
		if (!advanceLine(next)) {
			mLazyLineIndexComplete = true;
			mLazyTotalLineCount = mLazyIndexedLine + 1;
			return;
		}
		mLazyIndexedOffset = next;
		++mLazyIndexedLine;
		if ((mLazyIndexedLine % checkpointStride) == 0) mLineIndexCheckpoints.push_back(LineIndexCheckpoint(mLazyIndexedOffset, mLazyIndexedLine));
	}
}

void TextDocument::ensureLazyIndexForLine(std::size_t targetLine) const noexcept {
	ensureLazyIndexSeeded();
	while (!mLazyLineIndexComplete && mLineIndexCheckpoints.back().lineIndex < targetLine)
		advanceLazyIndexByStride();
}

void TextDocument::ensureLazyIndexForOffset(Offset targetOffset) const noexcept {
	ensureLazyIndexSeeded();
	targetOffset = clampOffset(targetOffset);
	while (!mLazyLineIndexComplete && mLineIndexCheckpoints.back().offset <= targetOffset)
		advanceLazyIndexByStride();
}

void TextDocument::ensureLazyIndexComplete() const noexcept {
	ensureLazyIndexSeeded();
	if (!mLazyLineIndexComplete && mLineIndexCheckpoints.size() == 1 && mLineIndexCheckpoints[0].offset == 0 && mLineIndexCheckpoints[0].lineIndex == 0 && mLazyIndexedOffset == 0 && mLazyIndexedLine == 0) {
		if (const char *data = directTextData()) {
			buildDirectInitialLineIndex(data, mLength, mLineIndexCheckpoints, mLazyIndexedOffset, mLazyIndexedLine, mLazyTotalLineCount);
			mLazyLineIndexComplete = true;
			return;
		}
	}
	while (!mLazyLineIndexComplete)
		advanceLazyIndexByStride();
}

void TextDocument::shiftLazyLineIndexForInsertWithoutLineBreak(Offset offset, Offset length) noexcept {
	if (length <= 0 || mLineIndexCheckpoints.empty()) return;
	offset = clampOffset(offset);
	for (LineIndexCheckpoint &checkpoint : mLineIndexCheckpoints)
		if (checkpoint.offset > offset) checkpoint.offset += length;
	if (mLazyIndexedOffset > offset) mLazyIndexedOffset += length;
}

void TextDocument::invalidateLazyLineIndexFrom(Offset offset) noexcept {
	offset = clampOffset(offset);
	if (mLineIndexCheckpoints.empty()) {
		resetLazyLineIndex();
		return;
	}

	std::vector<LineIndexCheckpoint>::iterator keepEnd = std::upper_bound(mLineIndexCheckpoints.begin(), mLineIndexCheckpoints.end(), offset, [](Offset value, const LineIndexCheckpoint &checkpoint) { return value < checkpoint.offset; });
	if (keepEnd == mLineIndexCheckpoints.begin()) ++keepEnd;
	mLineIndexCheckpoints.erase(keepEnd, mLineIndexCheckpoints.end());
	if (mLineIndexCheckpoints.empty()) resetLazyLineIndex();
	else {
		mLazyIndexedOffset = mLineIndexCheckpoints.back().offset;
		mLazyIndexedLine = mLineIndexCheckpoints.back().lineIndex;
		mLazyLineIndexComplete = false;
		mLazyTotalLineCount = std::max<std::size_t>(1, mLazyIndexedLine + 1);
	}
}

} // namespace editor
} // namespace mr
