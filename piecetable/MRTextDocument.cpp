#include "MRTextDocument.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../ui/MRWindowSupport.hpp"

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
#include <emmintrin.h>
#endif

namespace mr {
namespace editor {

namespace {
constexpr std::size_t kLazyLineIndexStride = 4096;
constexpr Offset kMinParallelLineIndexBytes = 1u << 20;
constexpr Offset kMinParallelLineIndexBytesPerWorker = 1u << 19;
constexpr std::size_t kMaxParallelLineIndexWorkers = 4;

bool traceWarmupParallelEnabled() noexcept {
	static const bool enabled = []() noexcept {
		const char *value = std::getenv("MR_TRACE_WARMUP_PARALLEL");
		return value != nullptr && value[0] == '1' && value[1] == '\0';
	}();
	return enabled;
}

void logLineIndexTrace(const std::ostringstream &line) {
	if (!traceWarmupParallelEnabled()) return;
	mrLogMessage(line.str().c_str());
}

std::size_t allocateDocumentId() noexcept {
	static std::atomic<std::size_t> nextId(1);
	return nextId.fetch_add(1, std::memory_order_relaxed);
}

template <class Doc> char piecewiseCharAt(const Doc &doc, Offset pos) noexcept {
	pos = doc.clampOffset(pos);
	if (pos >= doc.length()) return '\0';
	Offset logical = 0;
	for (std::size_t i = 0; i < doc.pieceCount(); ++i) {
		PieceChunkView chunk = doc.pieceChunk(i);
		if (chunk.data == nullptr || chunk.length == 0) continue;
		if (pos < logical + chunk.length) return chunk.data[pos - logical];
		logical += chunk.length;
	}
	return '\0';
}

inline bool isLineBreakByte(char ch) noexcept {
	return ch == '\n' || ch == '\r';
}

inline void appendLineCheckpoint(std::vector<LineIndexCheckpoint> &checkpoints, Offset lineStart, std::size_t lineIndex) {
	if ((lineIndex % kLazyLineIndexStride) == 0) checkpoints.push_back(LineIndexCheckpoint(lineStart, lineIndex));
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

void buildDirectInitialLineIndex(const char *data, Offset length, std::vector<LineIndexCheckpoint> &checkpoints, Offset &indexedOffset, std::size_t &indexedLine, std::size_t &totalLines) {
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

inline std::size_t directCountLineBreaksInRange(const char *data, Offset length, Offset start, Offset end) noexcept;

struct DirectLineIndexPartition {
	Offset start;
	Offset end;

	DirectLineIndexPartition() noexcept : start(0), end(0) {
	}

	DirectLineIndexPartition(Offset aStart, Offset aEnd) noexcept : start(aStart), end(aEnd) {
	}
};

struct DirectLineIndexPartitionResult {
	bool complete;
	std::vector<Offset> lineStarts;

	DirectLineIndexPartitionResult() noexcept : complete(false), lineStarts() {
	}
};

std::vector<DirectLineIndexPartition> buildDirectLineIndexPartitions(Offset length, std::size_t workerCount) {
	std::vector<DirectLineIndexPartition> partitions;
	if (length == 0 || workerCount == 0) return partitions;

	const Offset baseChunk = length / workerCount;
	const Offset remainder = length % workerCount;
	Offset start = 0;
	for (std::size_t i = 0; i < workerCount; ++i) {
		const Offset chunk = baseChunk + (i < remainder ? 1 : 0);
		const Offset end = start + chunk;
		if (end > start) partitions.push_back(DirectLineIndexPartition(start, end));
		start = end;
	}
	return partitions;
}

std::size_t chooseParallelDirectLineIndexWorkerCount(Offset length) noexcept {
	if (length < kMinParallelLineIndexBytes) return 0;

	const unsigned int hardware = std::thread::hardware_concurrency();
	if (hardware <= 1) return 0;

	const Offset byWork = length / kMinParallelLineIndexBytesPerWorker;
	if (byWork <= 1) return 0;

	std::size_t workerCount = std::min(static_cast<std::size_t>(hardware), kMaxParallelLineIndexWorkers);
	workerCount = std::min(workerCount, static_cast<std::size_t>(byWork));
	return workerCount > 1 ? workerCount : 0;
}

const char *parallelDirectLineIndexSerialReason(Offset length) noexcept {
	if (length < kMinParallelLineIndexBytes) return "below-byte-threshold";
	const unsigned int hardware = std::thread::hardware_concurrency();
	if (hardware <= 1) return "hardware-concurrency-too-small";
	const Offset byWork = length / kMinParallelLineIndexBytesPerWorker;
	if (byWork <= 1) return "below-work-per-worker-threshold";
	if (chooseParallelDirectLineIndexWorkerCount(length) <= 1) return "single-worker";
	return "parallel";
}

bool buildDirectInitialLineIndexParallel(const char *data, Offset length, std::vector<LineIndexCheckpoint> &checkpoints, Offset &indexedOffset, std::size_t &indexedLine, std::size_t &totalLines, std::stop_token stopToken,
                                         const std::atomic_bool *cancelFlag) {
	const auto startedAt = std::chrono::steady_clock::now();
	const std::size_t workerCount = chooseParallelDirectLineIndexWorkerCount(length);
	if (data == nullptr || workerCount == 0) return false;

	const std::vector<DirectLineIndexPartition> partitions = buildDirectLineIndexPartitions(length, workerCount);
	if (partitions.size() <= 1) return false;

	if (traceWarmupParallelEnabled()) {
		std::ostringstream line;
		line << "WARMUP-LINEINDEX begin-direct-parallel bytes=" << length << " partitions=" << partitions.size() << " worker_count=" << workerCount;
		logLineIndexTrace(line);
	}

	std::vector<DirectLineIndexPartitionResult> partitionResults(partitions.size());
	std::atomic_bool failed(false);
	auto cancelled = [&](std::stop_token workerStopToken) noexcept {
		if (failed.load(std::memory_order_acquire)) return true;
		if (workerStopToken.stop_requested() || stopToken.stop_requested()) return true;
		return cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire);
	};

	{
		std::vector<std::jthread> workers;
		workers.reserve(partitions.size());
		for (std::size_t i = 0; i < partitions.size(); ++i) {
			workers.emplace_back([&, i](std::stop_token workerStopToken) {
				if (cancelled(workerStopToken)) {
					failed.store(true, std::memory_order_release);
					return;
				}

				const DirectLineIndexPartition partition = partitions[i];
				DirectLineIndexPartitionResult result;
				result.lineStarts.reserve(directCountLineBreaksInRange(data, length, partition.start, partition.end));

				for (Offset at = partition.start; at < partition.end; ++at) {
					if ((at & 0xFFFFu) == 0 && cancelled(workerStopToken)) {
						failed.store(true, std::memory_order_release);
						return;
					}

					const char ch = data[at];
					if (!isLineBreakByte(ch)) continue;
					if (ch == '\n' && at > 0 && data[at - 1] == '\r') continue;

					Offset nextLineStart = at + 1;
					if (ch == '\r' && at + 1 < length && data[at + 1] == '\n') nextLineStart = at + 2;
					result.lineStarts.push_back(nextLineStart);
				}

				if (cancelled(workerStopToken)) {
					failed.store(true, std::memory_order_release);
					return;
				}

				result.complete = true;
				partitionResults[i] = std::move(result);
			});
		}
	}

	if (failed.load(std::memory_order_acquire)) {
		if (traceWarmupParallelEnabled()) {
			std::ostringstream line;
			line << "WARMUP-LINEINDEX end-direct-parallel bytes=" << length << " result=fallback reason=worker-failed";
			logLineIndexTrace(line);
		}
		return false;
	}

	checkpoints.clear();
	checkpoints.push_back(LineIndexCheckpoint(0, 0));
	indexedOffset = 0;
	indexedLine = 0;

	for (std::size_t i = 0; i < partitionResults.size(); ++i) {
		if (!partitionResults[i].complete) return false;
		for (std::size_t j = 0; j < partitionResults[i].lineStarts.size(); ++j) {
			indexedOffset = partitionResults[i].lineStarts[j];
			++indexedLine;
			appendLineCheckpoint(checkpoints, indexedOffset, indexedLine);
		}
	}

	totalLines = indexedLine + 1;
	if (traceWarmupParallelEnabled()) {
		std::ostringstream line;
		line << "WARMUP-LINEINDEX end-direct-parallel bytes=" << length << " result=completed checkpoints=" << checkpoints.size() << " total_lines=" << totalLines
		     << " duration_us=" << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
		logLineIndexTrace(line);
	}
	return true;
}

bool isParallelLineIndexValidationEnabled() noexcept {
	const char *value = std::getenv("MR_VALIDATE_PARALLEL_LINE_INDEX");
	return value != nullptr && std::strcmp(value, "1") == 0;
}

void assignLineIndexWarmupData(LineIndexWarmupData &warmup, std::vector<LineIndexCheckpoint> checkpoints, Offset indexedOffset, std::size_t indexedLine, std::size_t totalLines) {
	warmup.checkpoints = std::move(checkpoints);
	warmup.lazyIndexedOffset = indexedOffset;
	warmup.lazyIndexedLine = indexedLine;
	warmup.lazyLineIndexComplete = true;
	warmup.lazyTotalLineCount = totalLines;
}

bool hasMatchingLineIndexWarmupData(const LineIndexWarmupData &left, const LineIndexWarmupData &right) noexcept {
	if (left.lazyIndexedOffset != right.lazyIndexedOffset || left.lazyIndexedLine != right.lazyIndexedLine || left.lazyLineIndexComplete != right.lazyLineIndexComplete || left.lazyTotalLineCount != right.lazyTotalLineCount ||
	    left.checkpoints.size() != right.checkpoints.size())
		return false;

	for (std::size_t i = 0; i < left.checkpoints.size(); ++i)
		if (left.checkpoints[i].offset != right.checkpoints[i].offset || left.checkpoints[i].lineIndex != right.checkpoints[i].lineIndex) return false;

	return true;
}

void logParallelLineIndexValidationMismatch(const LineIndexWarmupData &serialWarmup, const LineIndexWarmupData &parallelWarmup) noexcept {
	std::fprintf(stderr,
	             "MR line-index parallel validation mismatch: serial(checkpoints=%zu offset=%zu line=%zu complete=%d total=%zu) parallel(checkpoints=%zu offset=%zu line=%zu complete=%d total=%zu)\n",
	             serialWarmup.checkpoints.size(), serialWarmup.lazyIndexedOffset, serialWarmup.lazyIndexedLine, serialWarmup.lazyLineIndexComplete ? 1 : 0, serialWarmup.lazyTotalLineCount, parallelWarmup.checkpoints.size(),
	             parallelWarmup.lazyIndexedOffset, parallelWarmup.lazyIndexedLine, parallelWarmup.lazyLineIndexComplete ? 1 : 0, parallelWarmup.lazyTotalLineCount);
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

inline std::size_t countLineBreaksChunk(const char *data, Offset length, bool &prevWasCR) noexcept {
	if (data == nullptr || length == 0) return 0;
#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
	return countLineBreaksSse2(data, length, prevWasCR);
#else
	return countLineBreaksScalar(data, length, prevWasCR);
#endif
}

inline std::size_t directCountLineBreaksInRange(const char *data, Offset length, Offset start, Offset end) noexcept {
	if (data == nullptr || length == 0) return 0;
	start = std::min(start, length);
	end = std::min(end, length);
	if (end <= start) return 0;
	bool prevWasCR = start > 0 && data[start - 1] == '\r';
	return countLineBreaksChunk(data + start, end - start, prevWasCR);
}

template <class Doc> std::size_t piecewiseCountLineBreaksInRange(const Doc &doc, Offset start, Offset end) noexcept {
	start = doc.clampOffset(start);
	end = doc.clampOffset(end);
	if (end <= start) return 0;

	bool prevWasCR = false;
	if (start > 0) prevWasCR = piecewiseCharAt(doc, start - 1) == '\r';

	std::size_t count = 0;
	Offset logical = 0;
	for (std::size_t i = 0; i < doc.pieceCount() && logical < end; ++i) {
		PieceChunkView chunk = doc.pieceChunk(i);
		if (chunk.data == nullptr || chunk.length == 0) continue;
		const Offset chunkStart = logical;
		const Offset chunkEnd = logical + chunk.length;
		if (chunkEnd <= start) {
			logical = chunkEnd;
			continue;
		}
		const Offset takeStart = std::max(start, chunkStart);
		const Offset takeEnd = std::min(end, chunkEnd);
		if (takeEnd > takeStart) count += countLineBreaksChunk(chunk.data + (takeStart - chunkStart), takeEnd - takeStart, prevWasCR);
		logical = chunkEnd;
	}
	return count;
}

template <class Doc> std::size_t piecewiseLineCount(const Doc &doc) noexcept {
	bool prevWasCR = false;
	std::size_t breaks = 0;
	for (std::size_t i = 0; i < doc.pieceCount(); ++i) {
		PieceChunkView chunk = doc.pieceChunk(i);
		if (chunk.data == nullptr || chunk.length == 0) continue;
		breaks += countLineBreaksChunk(chunk.data, chunk.length, prevWasCR);
	}
	return breaks + 1;
}

template <class Doc> Offset piecewiseLineStart(const Doc &doc, Offset pos) noexcept {
	pos = doc.clampOffset(pos);
	if (pos == 0) return 0;

	Offset logicalEnd = doc.length();
	for (std::size_t i = doc.pieceCount(); i > 0; --i) {
		PieceChunkView chunk = doc.pieceChunk(i - 1);
		if (chunk.data == nullptr || chunk.length == 0) continue;
		Offset chunkStart = logicalEnd - chunk.length;
		if (pos <= chunkStart) {
			logicalEnd = chunkStart;
			continue;
		}

		Offset localLimit = std::min(chunk.length, pos - chunkStart);
		const Offset breakPos = directFindPrevLineBreak(chunk.data, localLimit);
		if (breakPos != static_cast<Offset>(-1)) return chunkStart + breakPos + 1;

		pos = chunkStart;
		logicalEnd = chunkStart;
		if (pos == 0) break;
	}
	return 0;
}

template <class Doc> Offset piecewiseLineEnd(const Doc &doc, Offset pos) noexcept {
	pos = doc.clampOffset(pos);
	Offset logical = 0;
	bool active = false;
	for (std::size_t i = 0; i < doc.pieceCount(); ++i) {
		PieceChunkView chunk = doc.pieceChunk(i);
		if (chunk.data == nullptr || chunk.length == 0) continue;
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
		if (breakPos < chunk.length) return logical + breakPos;
		logical += chunk.length;
	}
	return doc.length();
}

template <class Doc> Offset piecewiseNextLine(const Doc &doc, Offset pos) noexcept {
	Offset end = piecewiseLineEnd(doc, pos);
	if (end < doc.length()) {
		if (piecewiseCharAt(doc, end) == '\r' && end + 1 < doc.length() && piecewiseCharAt(doc, end + 1) == '\n') end += 2;
		else
			++end;
	}
	return end;
}

template <class Doc> bool piecewiseAdvanceLine(const Doc &doc, Offset &offset) noexcept {
	Offset end = piecewiseLineEnd(doc, offset);
	if (end >= doc.length()) return false;
	if (piecewiseCharAt(doc, end) == '\r' && end + 1 < doc.length() && piecewiseCharAt(doc, end + 1) == '\n') offset = end + 2;
	else
		offset = end + 1;
	return true;
}

template <class Doc> Offset piecewisePrevLine(const Doc &doc, Offset pos) noexcept {
	Offset start = piecewiseLineStart(doc, pos);
	if (start == 0) return 0;
	Offset probe = start - 1;
	if (probe > 0 && piecewiseCharAt(doc, probe - 1) == '\r' && piecewiseCharAt(doc, probe) == '\n') --probe;
	return piecewiseLineStart(doc, probe);
}

template <class Doc> std::size_t piecewiseLineIndex(const Doc &doc, Offset pos) noexcept {
	pos = doc.clampOffset(pos);
	return piecewiseCountLineBreaksInRange(doc, 0, pos);
}

template <class Doc> Offset piecewiseLineStartByIndex(const Doc &doc, std::size_t index) noexcept {
	Offset pos = 0;
	std::size_t line = 0;
	while (line < index && pos < doc.length()) {
		Offset next = piecewiseNextLine(doc, pos);
		if (next <= pos) break;
		pos = next;
		++line;
	}
	return pos;
}

template <class Doc> std::string piecewiseRangeText(const Doc &doc, Offset start, Offset end) {
	start = doc.clampOffset(start);
	end = doc.clampOffset(end);
	if (end < start) std::swap(start, end);
	std::string out;
	out.reserve(end - start);
	Offset logical = 0;
	for (std::size_t i = 0; i < doc.pieceCount() && logical < end; ++i) {
		PieceChunkView chunk = doc.pieceChunk(i);
		if (chunk.data == nullptr || chunk.length == 0) continue;
		Offset chunkStart = logical;
		Offset chunkEnd = logical + chunk.length;
		if (chunkEnd <= start) {
			logical = chunkEnd;
			continue;
		}
		Offset takeStart = std::max(start, chunkStart);
		Offset takeEnd = std::min(end, chunkEnd);
		if (takeEnd > takeStart) out.append(chunk.data + (takeStart - chunkStart), takeEnd - takeStart);
		logical = chunkEnd;
	}
	return out;
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

TextSpan AppendBuffer::append(std::string_view text) {
	TextSpan span(mText.size(), text.size());
	mText.append(text.data(), text.size());
	return span;
}

void AppendBuffer::clear() noexcept {
	mText.clear();
}

std::string AppendBuffer::sliceText(TextSpan span) const {
	TextSpan bounded = span.clamped(mText.size());
	return mText.substr(bounded.start, bounded.length);
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

TextDocument::TextDocument() noexcept : mLength(0), mDocumentId(allocateDocumentId()), mVersion(0), mCacheDirty(false), mLazyIndexedOffset(0), mLazyIndexedLine(0), mLazyLineIndexComplete(false), mLazyTotalLineCount(1) {
	resetLazyLineIndex();
}

TextDocument::TextDocument(std::string_view text) : mLength(0), mDocumentId(allocateDocumentId()), mVersion(0), mCacheDirty(false), mLazyIndexedOffset(0), mLazyIndexedLine(0), mLazyLineIndexComplete(false), mLazyTotalLineCount(1) {
	resetLazyLineIndex();
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

char TextDocument::charAt(Offset pos) const noexcept {
	if (const char *data = directTextData()) return pos < mLength ? data[pos] : '\0';
	return piecewiseCharAt(*this, pos);
}

Snapshot TextDocument::snapshot() const {
	return Snapshot(text(), mVersion);
}

ReadSnapshot TextDocument::readSnapshot() const {
	ReadSnapshot snapshot;
	snapshot.mDocumentId = mDocumentId;
	snapshot.mVersion = mVersion;
	snapshot.mMappedOriginal = mMappedOriginal;
	snapshot.mOriginalBuffer = std::make_shared<const std::string>(mOriginalBuffer);
	snapshot.mAddBuffer = std::make_shared<const std::string>(mAddBuffer.text());
	snapshot.mPieces = std::make_shared<const std::vector<Piece>>(mPieces);
	snapshot.mLength = mLength;
	snapshot.mCacheDirty = mCacheDirty;
	snapshot.mMaterializedText = mCacheDirty ? std::string() : mMaterializedText;
	snapshot.mLineIndexCheckpoints = mLineIndexCheckpoints;
	snapshot.mLazyIndexedOffset = mLazyIndexedOffset;
	snapshot.mLazyIndexedLine = mLazyIndexedLine;
	snapshot.mLazyLineIndexComplete = mLazyLineIndexComplete;
	snapshot.mLazyTotalLineCount = mLazyTotalLineCount;
	if (snapshot.mLineIndexCheckpoints.empty()) snapshot.resetLazyLineIndex();
	return snapshot;
}

void TextDocument::restoreFromSnapshot(const ReadSnapshot &snapshot) {
	if (snapshot.empty() && snapshot.length() == 0) {
		setTextNoVersionBump("");
	} else {
		mDocumentId = snapshot.mDocumentId;
		mVersion = snapshot.mVersion;
		mMappedOriginal = snapshot.mMappedOriginal;
		if (snapshot.mOriginalBuffer) mOriginalBuffer = *snapshot.mOriginalBuffer;
		else
			mOriginalBuffer.clear();

		mAddBuffer.clear();
		if (snapshot.mAddBuffer) mAddBuffer.append(*snapshot.mAddBuffer);

		if (snapshot.mPieces) mPieces = *snapshot.mPieces;
		else
			mPieces.clear();

		mLength = snapshot.mLength;
		mCacheDirty = snapshot.mCacheDirty;
		mMaterializedText = snapshot.mMaterializedText;
		mLineIndexCheckpoints = snapshot.mLineIndexCheckpoints;
		mLazyIndexedOffset = snapshot.mLazyIndexedOffset;
		mLazyIndexedLine = snapshot.mLazyIndexedLine;
		mLazyLineIndexComplete = snapshot.mLazyLineIndexComplete;
		mLazyTotalLineCount = snapshot.mLazyTotalLineCount;
	}
}

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
	ensureLazyIndexComplete();
	return mLazyTotalLineCount;
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
		return directFindNextLineBreak(data, mLength, pos);
	}

	return piecewiseLineEnd(*this, pos);
}

Offset ReadSnapshot::nextLine(Offset pos) const noexcept {
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
	ensureLazyIndexSeeded();
	if (mLineIndexCheckpoints.empty()) return 0;

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
	ensureLazyIndexForLine(index);
	if (mLineIndexCheckpoints.empty()) return 0;
	if (mLazyLineIndexComplete && index >= mLazyTotalLineCount) index = mLazyTotalLineCount > 0 ? mLazyTotalLineCount - 1 : 0;

	std::size_t left = 0;
	std::size_t right = mLineIndexCheckpoints.size();
	while (left < right) {
		std::size_t mid = left + (right - left) / 2;
		if (mLineIndexCheckpoints[mid].lineIndex <= index) left = mid + 1;
		else
			right = mid;
	}
	LineIndexCheckpoint checkpoint = mLineIndexCheckpoints[left == 0 ? 0 : static_cast<std::size_t>(left - 1)];
	Offset cursor = checkpoint.offset;
	std::size_t line = checkpoint.lineIndex;
	while (line < index) {
		Offset next = cursor;
		if (!advanceLine(next)) break;
		cursor = next;
		++line;
	}
	return cursor;
}

std::size_t ReadSnapshot::estimatedLineCount() const noexcept {
	ensureLazyIndexSeeded();
	if (mLazyLineIndexComplete) return mLazyTotalLineCount;
	if (!mMappedOriginal.mapped()) return piecewiseLineCount(*this);
	if (mLazyIndexedOffset == 0 || mLazyIndexedLine == 0) return std::max<std::size_t>(1, mLength / 80 + 1);

	const std::size_t observedLines = mLazyIndexedLine + 1;
	const std::size_t estimated = static_cast<std::size_t>((static_cast<long double>(mLength) * observedLines) / std::max<Offset>(mLazyIndexedOffset, 1));
	return std::max<std::size_t>(observedLines, estimated);
}

bool ReadSnapshot::exactLineCountKnown() const noexcept {
	return !mMappedOriginal.mapped() || mLazyLineIndexComplete;
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

bool ReadSnapshot::completeLineIndexWarmup(LineIndexWarmupData &warmup, std::stop_token stopToken, const std::atomic_bool *cancelFlag) const {
	const bool traceWarmup = traceWarmupParallelEnabled();
	const auto startedAt = std::chrono::steady_clock::now();
	ensureLazyIndexSeeded();
	if (!mLazyLineIndexComplete && mLineIndexCheckpoints.size() == 1 && mLineIndexCheckpoints[0].offset == 0 && mLineIndexCheckpoints[0].lineIndex == 0 && mLazyIndexedOffset == 0 && mLazyIndexedLine == 0) {
		if (const char *data = directTextData()) {
			if (traceWarmup) {
				std::ostringstream line;
				line << "WARMUP-LINEINDEX decide direct_mapped_original=yes bytes=" << mLength << " validation=" << (isParallelLineIndexValidationEnabled() ? "yes" : "no")
				     << " mode=" << (std::strcmp(parallelDirectLineIndexSerialReason(mLength), "parallel") == 0 ? "parallel" : "serial");
				if (std::strcmp(parallelDirectLineIndexSerialReason(mLength), "parallel") == 0)
					line << " parallel_workers=" << chooseParallelDirectLineIndexWorkerCount(mLength);
				else
					line << " reason=" << parallelDirectLineIndexSerialReason(mLength);
				logLineIndexTrace(line);
			}
			if (isParallelLineIndexValidationEnabled()) {
				LineIndexWarmupData serialWarmup;
				LineIndexWarmupData parallelWarmup;
				std::vector<LineIndexCheckpoint> serialCheckpoints;
				std::vector<LineIndexCheckpoint> parallelCheckpoints;
				Offset serialIndexedOffset = 0;
				Offset parallelIndexedOffset = 0;
				std::size_t serialIndexedLine = 0;
				std::size_t parallelIndexedLine = 0;
				std::size_t serialTotalLines = 1;
				std::size_t parallelTotalLines = 1;

				buildDirectInitialLineIndex(data, mLength, serialCheckpoints, serialIndexedOffset, serialIndexedLine, serialTotalLines);
				assignLineIndexWarmupData(serialWarmup, std::move(serialCheckpoints), serialIndexedOffset, serialIndexedLine, serialTotalLines);

				if (stopToken.stop_requested() || (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire))) return false;

				if (buildDirectInitialLineIndexParallel(data, mLength, parallelCheckpoints, parallelIndexedOffset, parallelIndexedLine, parallelTotalLines, stopToken, cancelFlag)) {
					assignLineIndexWarmupData(parallelWarmup, std::move(parallelCheckpoints), parallelIndexedOffset, parallelIndexedLine, parallelTotalLines);
					if (hasMatchingLineIndexWarmupData(serialWarmup, parallelWarmup)) {
						if (traceWarmup) {
							std::ostringstream line;
							line << "WARMUP-LINEINDEX end direct_mapped_original=yes mode=parallel validation=yes result=completed checkpoints=" << parallelWarmup.checkpoints.size()
							     << " total_lines=" << parallelWarmup.lazyTotalLineCount << " duration_us="
							     << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
							logLineIndexTrace(line);
						}
						warmup = std::move(parallelWarmup);
						return true;
					}
					logParallelLineIndexValidationMismatch(serialWarmup, parallelWarmup);
					if (traceWarmup) {
						std::ostringstream line;
						line << "WARMUP-LINEINDEX validation result=mismatch fallback_to_serial=yes";
						logLineIndexTrace(line);
					}
				}
				if (stopToken.stop_requested() || (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire))) return false;

				if (traceWarmup) {
					std::ostringstream line;
					line << "WARMUP-LINEINDEX end direct_mapped_original=yes mode=serial validation=yes result=completed checkpoints=" << serialWarmup.checkpoints.size()
					     << " total_lines=" << serialWarmup.lazyTotalLineCount << " duration_us="
					     << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
					logLineIndexTrace(line);
				}
				warmup = std::move(serialWarmup);
				return true;
			}

			std::vector<LineIndexCheckpoint> checkpoints;
			Offset indexedOffset = 0;
			std::size_t indexedLine = 0;
			std::size_t totalLines = 1;
			if (buildDirectInitialLineIndexParallel(data, mLength, checkpoints, indexedOffset, indexedLine, totalLines, stopToken, cancelFlag)) {
				assignLineIndexWarmupData(warmup, std::move(checkpoints), indexedOffset, indexedLine, totalLines);
				if (traceWarmup) {
					std::ostringstream line;
					line << "WARMUP-LINEINDEX end direct_mapped_original=yes mode=parallel validation=no result=completed checkpoints=" << warmup.checkpoints.size()
					     << " total_lines=" << warmup.lazyTotalLineCount << " duration_us="
					     << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
					logLineIndexTrace(line);
				}
				return true;
			}
			if (traceWarmup) {
				std::ostringstream line;
				line << "WARMUP-LINEINDEX fallback direct_mapped_original=yes reason=" << parallelDirectLineIndexSerialReason(mLength);
				logLineIndexTrace(line);
			}
			if (stopToken.stop_requested() || (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire))) return false;
		}
	}
	if (traceWarmup) {
		std::ostringstream line;
		line << "WARMUP-LINEINDEX decide direct_mapped_original=" << (directTextData() != nullptr ? "yes" : "no") << " mode=serial reason=piecewise-or-seeded";
		logLineIndexTrace(line);
	}
	while (!mLazyLineIndexComplete) {
		if (stopToken.stop_requested() || (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire))) return false;
		advanceLazyIndexByStride();
	}
	if (stopToken.stop_requested() || (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire))) return false;
	warmup.checkpoints = mLineIndexCheckpoints;
	warmup.lazyIndexedOffset = mLazyIndexedOffset;
	warmup.lazyIndexedLine = mLazyIndexedLine;
	warmup.lazyLineIndexComplete = mLazyLineIndexComplete;
	warmup.lazyTotalLineCount = mLazyTotalLineCount;
	if (traceWarmup) {
		std::ostringstream line;
		line << "WARMUP-LINEINDEX end direct_mapped_original=no mode=serial result=completed checkpoints=" << warmup.checkpoints.size() << " total_lines=" << warmup.lazyTotalLineCount
		     << " duration_us=" << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
		logLineIndexTrace(line);
	}
	return true;
}

bool ReadSnapshot::isLineBreakChar(char ch) const noexcept {
	return ch == '\n' || ch == '\r';
}

bool ReadSnapshot::hasDirectOriginalView() const noexcept {
	return mMappedOriginal.mapped() && addBufferLength() == 0 && mPieces != nullptr && mPieces->size() == 1 && (*mPieces)[0].source == BufferKind::Original && (*mPieces)[0].span.start == 0 && (*mPieces)[0].span.length == mLength;
}

const char *ReadSnapshot::directTextData() const noexcept {
	return hasDirectOriginalView() ? mMappedOriginal.data() : nullptr;
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

	for (std::size_t steps = 0; steps < kLazyLineIndexStride; ++steps) {
		Offset next = mLazyIndexedOffset;
		if (!advanceLine(next)) {
			mLazyLineIndexComplete = true;
			mLazyTotalLineCount = mLazyIndexedLine + 1;
			return;
		}
		mLazyIndexedOffset = next;
		++mLazyIndexedLine;
		if ((mLazyIndexedLine % kLazyLineIndexStride) == 0) mLineIndexCheckpoints.push_back(LineIndexCheckpoint(mLazyIndexedOffset, mLazyIndexedLine));
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

const char *ReadSnapshot::originalData() const noexcept {
	if (mMappedOriginal.mapped()) return mMappedOriginal.data();
	return mOriginalBuffer != nullptr ? mOriginalBuffer->data() : nullptr;
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

bool TextDocument::loadMappedFile(const std::string &path, std::string &error) {
	MappedFileSource source;
	if (!source.openReadOnly(path, error)) return false;
	initializeFromMappedSource(source, true);
	return true;
}

PieceChunkView TextDocument::pieceChunk(std::size_t index) const noexcept {
	if (index >= mPieces.size()) return PieceChunkView();

	const Piece &piece = mPieces[index];
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
	return result;
}

CommitResult TextDocument::tryApply(const StagedEditTransaction &transaction) {
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
	if (mPieces.size() <= 1 && mAddBuffer.size() == 0 && !mMappedOriginal.mapped()) return;

	std::string currentText = text();
	mOriginalBuffer = std::move(currentText);
	mMappedOriginal.reset();
	mAddBuffer.clear();

	mPieces.clear();
	mPieces.emplace_back(BufferKind::Original, TextSpan(0, mOriginalBuffer.length()));
	mLength = mOriginalBuffer.length();

	markDirty();
	bumpVersion();
}

Offset TextDocument::clampOffset(Offset pos) const noexcept {
	return std::min(pos, mLength);
}

std::size_t TextDocument::lineCount() const noexcept {
	ensureLazyIndexComplete();
	return mLazyTotalLineCount;
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
		return directFindNextLineBreak(data, mLength, pos);
	}

	return piecewiseLineEnd(*this, pos);
}

Offset TextDocument::nextLine(Offset pos) const noexcept {
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

Offset TextDocument::prevLine(Offset pos) const noexcept {
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

std::size_t TextDocument::lineIndex(Offset pos) const noexcept {
	pos = clampOffset(pos);
	ensureLazyIndexSeeded();
	if (mLineIndexCheckpoints.empty()) return 0;

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

Offset TextDocument::lineStartByIndex(std::size_t index) const noexcept {
	ensureLazyIndexForLine(index);
	if (mLineIndexCheckpoints.empty()) return 0;
	if (mLazyLineIndexComplete && index >= mLazyTotalLineCount) index = mLazyTotalLineCount > 0 ? mLazyTotalLineCount - 1 : 0;

	std::size_t left = 0;
	std::size_t right = mLineIndexCheckpoints.size();
	while (left < right) {
		std::size_t mid = left + (right - left) / 2;
		if (mLineIndexCheckpoints[mid].lineIndex <= index) left = mid + 1;
		else
			right = mid;
	}
	LineIndexCheckpoint checkpoint = mLineIndexCheckpoints[left == 0 ? 0 : static_cast<std::size_t>(left - 1)];
	Offset cursor = checkpoint.offset;
	std::size_t line = checkpoint.lineIndex;
	while (line < index) {
		Offset next = cursor;
		if (!advanceLine(next)) break;
		cursor = next;
		++line;
	}
	return cursor;
}

std::size_t TextDocument::estimatedLineCount() const noexcept {
	ensureLazyIndexSeeded();
	if (mLazyLineIndexComplete) return mLazyTotalLineCount;
	if (!mMappedOriginal.mapped()) return piecewiseLineCount(*this);
	if (mLazyIndexedOffset == 0 || mLazyIndexedLine == 0) return std::max<std::size_t>(1, mLength / 80 + 1);

	const std::size_t observedLines = mLazyIndexedLine + 1;
	const std::size_t estimated = static_cast<std::size_t>((static_cast<long double>(mLength) * observedLines) / std::max<Offset>(mLazyIndexedOffset, 1));
	return std::max<std::size_t>(observedLines, estimated);
}

bool TextDocument::exactLineCountKnown() const noexcept {
	return !mMappedOriginal.mapped() || mLazyLineIndexComplete;
}

std::size_t TextDocument::column(Offset pos) const noexcept {
	pos = clampOffset(pos);
	return pos - lineStart(pos);
}

std::string TextDocument::lineText(Offset pos) const {
	Offset start = lineStart(pos);
	Offset end = lineEnd(pos);
	if (const char *data = directTextData()) return std::string(data + start, end - start);
	return piecewiseRangeText(*this, start, end);
}

bool TextDocument::isLineBreakChar(char ch) const noexcept {
	return ch == '\n' || ch == '\r';
}

bool TextDocument::setTextNoVersionBump(std::string_view text) {
	if (text == this->text()) return false;
	initializeFromOriginal(text, false);
	return true;
}

void TextDocument::initializeFromOriginal(std::string_view text, bool bumpVersionFlag) {
	mOriginalBuffer.assign(text.data(), text.size());
	mMappedOriginal.reset();
	mAddBuffer.clear();
	mPieces.clear();
	mLength = mOriginalBuffer.size();
	resetLazyLineIndex();
	if (!mOriginalBuffer.empty()) mPieces.push_back(Piece(BufferKind::Original, TextSpan(0, mOriginalBuffer.size())));
	mMaterializedText = mOriginalBuffer;
	mCacheDirty = false;
	if (bumpVersionFlag) bumpVersion();
}

void TextDocument::initializeFromMappedSource(const MappedFileSource &source, bool bumpVersionFlag) {
	mOriginalBuffer.clear();
	mMappedOriginal = source;
	mAddBuffer.clear();
	mPieces.clear();
	mLength = mMappedOriginal.size();
	resetLazyLineIndex();
	mMaterializedText.clear();
	mCacheDirty = mLength != 0;
	if (mLength != 0) mPieces.push_back(Piece(BufferKind::Original, TextSpan(0, mLength)));
	if (bumpVersionFlag) bumpVersion();
}

void TextDocument::bumpVersion() noexcept {
	++mVersion;
}

void TextDocument::markDirty() noexcept {
	mCacheDirty = true;
}

void TextDocument::ensureMaterialized() const noexcept {
	if (!mCacheDirty) return;

	mMaterializedText.clear();
	mMaterializedText.reserve(mLength);
	for (const auto &piece : mPieces)
		mMaterializedText += pieceText(piece);
	mCacheDirty = false;
}

std::string TextDocument::pieceText(const Piece &piece) const {
	if (piece.source == BufferKind::Original) return mMappedOriginal.mapped() ? mMappedOriginal.sliceText(piece.span) : mOriginalBuffer.substr(piece.span.start, piece.span.length);
	return mAddBuffer.sliceText(piece.span);
}

const char *TextDocument::originalData() const noexcept {
	return mMappedOriginal.mapped() ? mMappedOriginal.data() : mOriginalBuffer.data();
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
	if (logical >= mLength) return mPieces.size();

	for (std::size_t i = 0; i < mPieces.size(); ++i) {
		Offset pieceLen = mPieces[i].span.length;
		Offset pieceEnd = consumed + pieceLen;

		if (logical == consumed) return i;
		if (logical == pieceEnd) return i + 1;
		if (consumed < logical && logical < pieceEnd) {
			Offset leftLen = logical - consumed;
			Piece left = mPieces[i];
			Piece right = mPieces[i];
			left.span.length = leftLen;
			right.span.start += leftLen;
			right.span.length -= leftLen;
			mPieces[i] = left;
			mPieces.insert(mPieces.begin() + static_cast<std::ptrdiff_t>(i + 1), right);
			return i + 1;
		}
		consumed = pieceEnd;
	}

	return mPieces.size();
}

bool TextDocument::eraseNoVersionBump(Range range) {
	Range bounded = range.clamped(mLength);
	if (bounded.empty()) return false;

	std::size_t startIndex = splitAt(bounded.start);
	std::size_t endIndex = splitAt(bounded.end);

	if (startIndex < endIndex) mPieces.erase(mPieces.begin() + static_cast<std::ptrdiff_t>(startIndex), mPieces.begin() + static_cast<std::ptrdiff_t>(endIndex));
	mLength -= bounded.end - bounded.start;
	compactPieces();
	invalidateLazyLineIndexFrom(bounded.start);
	markDirty();
	return true;
}

bool TextDocument::replaceNoVersionBump(Range range, std::string_view text) {
	Range bounded = range.clamped(mLength);
	bool removed = eraseNoVersionBump(bounded);
	if (text.empty()) return removed;

	return insertAddSpanNoVersionBump(bounded.start, mAddBuffer.append(text)) || removed;
}

bool TextDocument::hasDirectOriginalView() const noexcept {
	return mMappedOriginal.mapped() && mAddBuffer.size() == 0 && mPieces.size() == 1 && mPieces[0].source == BufferKind::Original && mPieces[0].span.start == 0 && mPieces[0].span.length == mLength;
}

const char *TextDocument::directTextData() const noexcept {
	return hasDirectOriginalView() ? mMappedOriginal.data() : nullptr;
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

	for (std::size_t steps = 0; steps < kLazyLineIndexStride; ++steps) {
		Offset next = mLazyIndexedOffset;
		if (!advanceLine(next)) {
			mLazyLineIndexComplete = true;
			mLazyTotalLineCount = mLazyIndexedLine + 1;
			return;
		}
		mLazyIndexedOffset = next;
		++mLazyIndexedLine;
		if ((mLazyIndexedLine % kLazyLineIndexStride) == 0) mLineIndexCheckpoints.push_back(LineIndexCheckpoint(mLazyIndexedOffset, mLazyIndexedLine));
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

bool TextDocument::insertAddSpanNoVersionBump(Offset offset, TextSpan span) {
	Offset logical = clampOffset(offset);
	std::size_t index = splitAt(logical);

	if (!span.empty()) mPieces.insert(mPieces.begin() + static_cast<std::ptrdiff_t>(index), Piece(BufferKind::Add, span));
	else
		return false;

	mLength += span.length;
	compactPieces();
	invalidateLazyLineIndexFrom(logical);
	markDirty();
	return true;
}

void TextDocument::compactPieces() {
	std::vector<Piece> compacted;
	compacted.reserve(mPieces.size());

	for (const auto &piece : mPieces) {
		if (piece.empty()) continue;
		if (!compacted.empty() && compacted.back().source == piece.source && compacted.back().span.end() == piece.span.start) {
			compacted.back().span.length += piece.span.length;
		} else
			compacted.push_back(piece);
	}

	mPieces.swap(compacted);
}

} // namespace editor
} // namespace mr
