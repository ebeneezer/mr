#ifndef MRTEXTDOCUMENTLINEINDEX_HPP
#define MRTEXTDOCUMENTLINEINDEX_HPP

#include "MRTextDocument.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace mr {
namespace editor {
namespace lineindex {

inline constexpr std::size_t kLazyLineIndexStride = 4096;
inline constexpr std::size_t kPiecewiseLineIndexCheckpointStride = 64;
inline constexpr std::size_t kLazyLineStartCatchupWindow = 256;

Offset directFindNextLineBreak(const char *data, Offset length, Offset start) noexcept;
Offset directFindPrevLineBreak(const char *data, Offset endExclusive) noexcept;
std::size_t countLineBreaksChunk(const char *data, Offset length, bool &prevWasCR) noexcept;
std::size_t directCountLineBreaksInRange(const char *data, Offset length, Offset start, Offset end) noexcept;
void buildDirectInitialLineIndex(const char *data, Offset length, std::vector<LineIndexCheckpoint> &checkpoints, Offset &indexedOffset, std::size_t &indexedLine, std::size_t &totalLines);
void appendLineStartsFromInsertedText(std::vector<Offset> &starts, Offset baseOffset, std::string_view text, bool includeTerminalStart);

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
	if (count > 0 && end < doc.length() && end > start && piecewiseCharAt(doc, end) == '\n' && piecewiseCharAt(doc, end - 1) == '\r') --count;
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
	if (pos < doc.length() && piecewiseCharAt(doc, pos) == '\n' && piecewiseCharAt(doc, pos - 1) == '\r') --pos;

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

template <class Starts> std::size_t lineIndexFromExactStarts(const Starts &starts, Offset pos) noexcept {
	if (starts.empty()) return 0;
	const auto it = std::upper_bound(starts.begin(), starts.end(), pos);
	if (it == starts.begin()) return 0;
	return static_cast<std::size_t>(std::distance(starts.begin(), it) - 1);
}

template <class Starts> Offset lineStartFromExactStarts(const Starts &starts, std::size_t index) noexcept {
	if (starts.empty()) return 0;
	return index < starts.size() ? starts[index] : starts.back();
}

template <class Doc> Offset localLineStartForDoc(const Doc &doc, Offset pos) noexcept {
	return doc.lineStart(pos);
}

template <class Doc> std::size_t localCountLineBreaksForDoc(const Doc &doc, Offset start, Offset end) noexcept {
	return piecewiseCountLineBreaksInRange(doc, start, end);
}

template <class Doc> Offset localInterpolatedLineStartByIndex(const Doc &doc, std::size_t index, const std::vector<LineIndexCheckpoint> &checkpoints, bool lazyComplete, std::size_t lazyTotalLineCount) noexcept {
	if (checkpoints.empty()) return 0;
	if (lazyComplete && index >= lazyTotalLineCount) index = lazyTotalLineCount > 0 ? lazyTotalLineCount - 1 : 0;

	std::size_t left = 0;
	std::size_t right = checkpoints.size();
	while (left < right) {
		std::size_t mid = left + (right - left) / 2;
		if (checkpoints[mid].lineIndex <= index) left = mid + 1;
		else
			right = mid;
	}

	const LineIndexCheckpoint checkpoint = checkpoints[left == 0 ? 0 : static_cast<std::size_t>(left - 1)];
	Offset cursor = checkpoint.offset;
	std::size_t line = checkpoint.lineIndex;

	for (std::size_t iteration = 0; iteration < 3 && !lazyComplete; ++iteration) {
		const std::size_t remainingLines = index > line ? index - line : 0;
		if (remainingLines <= kLazyLineStartCatchupWindow) break;

		const std::size_t estimatedTotalLines = std::max<std::size_t>(doc.estimatedLineCount(), line + 1);
		if (estimatedTotalLines <= line + 1 || cursor >= doc.length()) break;

		const std::size_t estimatedRemainingLines = estimatedTotalLines - line - 1;
		const Offset remainingBytes = doc.length() - cursor;
		if (estimatedRemainingLines == 0 || remainingBytes == 0) break;

		const long double avgBytesPerLine = static_cast<long double>(remainingBytes) / static_cast<long double>(estimatedRemainingLines);
		Offset approx = cursor + static_cast<Offset>(std::min<long double>(remainingBytes, avgBytesPerLine * static_cast<long double>(remainingLines)));
		approx = doc.clampOffset(approx);
		Offset approxStart = localLineStartForDoc(doc, approx);
		if (approxStart <= cursor) break;

		std::size_t advanced = localCountLineBreaksForDoc(doc, cursor, approxStart);
		if (advanced == 0) break;

		if (advanced > remainingLines) {
			const long double scale = static_cast<long double>(remainingLines) / static_cast<long double>(advanced);
			const Offset delta = approxStart - cursor;
			const Offset scaledDelta = std::max<Offset>(1, static_cast<Offset>(static_cast<long double>(delta) * scale));
			approxStart = localLineStartForDoc(doc, cursor + scaledDelta);
			if (approxStart <= cursor) break;
			advanced = localCountLineBreaksForDoc(doc, cursor, approxStart);
			if (advanced == 0 || advanced > remainingLines) break;
		}

		cursor = approxStart;
		line += advanced;
	}

	while (line < index) {
		const Offset next = doc.nextLine(cursor);
		if (next <= cursor) break;
		cursor = next;
		++line;
	}
	return cursor;
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

} // namespace lineindex
} // namespace editor
} // namespace mr

#endif
