#include "MRTextDocumentLineIndex.hpp"

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
#include <emmintrin.h>
#endif

namespace mr {
namespace editor {
namespace lineindex {

namespace {

inline bool isLineBreakByte(char ch) noexcept {
	return ch == '\n' || ch == '\r';
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

} // namespace

Offset directFindNextLineBreak(const char *data, Offset length, Offset start) noexcept {
	if (data == nullptr || start >= length) return length;

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
	const __m128i cr = _mm_set1_epi8('\r');
	const __m128i lf = _mm_set1_epi8('\n');
	Offset i = start;

	for (; i + 16 <= length; i += 16) {
		const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + i));
		const __m128i maskVec = _mm_or_si128(_mm_cmpeq_epi8(bytes, cr), _mm_cmpeq_epi8(bytes, lf));
		const unsigned int mask = static_cast<unsigned int>(_mm_movemask_epi8(maskVec));
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
		const unsigned int mask = static_cast<unsigned int>(_mm_movemask_epi8(maskVec));
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
	std::size_t count = countLineBreaksChunk(data + start, end - start, prevWasCR);
	if (count > 0 && end < length && end > start && data[end] == '\n' && data[end - 1] == '\r') --count;
	return count;
}

#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))
__attribute__((target_clones("default", "popcnt", "arch=x86-64-v3")))
#endif
std::size_t countLineBreaksChunk(const char *data, Offset length, bool &prevWasCR) noexcept {
	if (data == nullptr || length == 0) return 0;
#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
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
#else
	return countLineBreaksScalar(data, length, prevWasCR);
#endif
}

void buildDirectInitialLineIndex(const char *data, Offset length, std::vector<LineIndexCheckpoint> &checkpoints, Offset &indexedOffset, std::size_t &indexedLine, std::size_t &totalLines) {
	checkpoints.clear();
	checkpoints.push_back(LineIndexCheckpoint(0, 0));
	indexedOffset = 0;
	indexedLine = 0;

	while (data != nullptr && indexedOffset < length) {
		const Offset breakOffset = directFindNextLineBreak(data, length, indexedOffset);
		if (breakOffset >= length) break;
		if (data[breakOffset] == '\r' && breakOffset + 1 < length && data[breakOffset + 1] == '\n') indexedOffset = breakOffset + 2;
		else
			indexedOffset = breakOffset + 1;
		++indexedLine;
		if ((indexedLine % kLazyLineIndexStride) == 0) checkpoints.push_back(LineIndexCheckpoint(indexedOffset, indexedLine));
	}
	totalLines = indexedLine + 1;
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

namespace {

class LineIndexSpanScanner {
  public:
	LineIndexSpanScanner(LineIndexScanPacket &aPacket, bool previousByteWasCr, const std::atomic_bool *aCancelFlag) noexcept
	    : packet(aPacket), previousWasCr(previousByteWasCr), pendingCr(false), pendingCrOffset(0), firstByte(true), cancelled(false), nextCancelCheckOffset(aPacket.startOffset), cancelFlag(aCancelFlag) {
	}

	void consume(const char *data, Offset length, Offset globalOffset) noexcept {
		if (data == nullptr || length == 0 || cancelled) return;
		Offset cursor = 0;
		if (firstByte) {
			if (previousWasCr && data[0] == '\n') {
				rememberLineBreak(globalOffset + 1);
				cursor = 1;
			}
			firstByte = false;
		}
		if (pendingCr) {
			if (cursor < length && data[cursor] == '\n') {
				rememberLineBreak(globalOffset + cursor + 1);
				++cursor;
			} else
				rememberLineBreak(pendingCrOffset + 1);
			pendingCr = false;
		}

		while (cursor < length) {
			const Offset absoluteCursor = globalOffset + cursor;
			if (absoluteCursor >= nextCancelCheckOffset) {
				if (cancelRequested()) {
					cancelled = true;
					return;
				}
				nextCancelCheckOffset = absoluteCursor + static_cast<Offset>(256 * 1024);
			}
			const Offset searchEnd = std::min(length, cursor + static_cast<Offset>(256 * 1024));
			const Offset breakOffset = lineindex::directFindNextLineBreak(data, searchEnd, cursor);
			if (breakOffset >= searchEnd) {
				cursor = searchEnd;
				continue;
			}
			if (data[breakOffset] == '\r') {
				if (breakOffset + 1 < length) {
					if (data[breakOffset + 1] == '\n') {
						rememberLineBreak(globalOffset + breakOffset + 2);
						cursor = breakOffset + 2;
					} else {
						rememberLineBreak(globalOffset + breakOffset + 1);
						cursor = breakOffset + 1;
					}
				} else {
					pendingCr = true;
					pendingCrOffset = globalOffset + breakOffset;
					cursor = length;
				}
			} else {
				rememberLineBreak(globalOffset + breakOffset + 1);
				cursor = breakOffset + 1;
			}
		}
	}

	void finish(bool nextByteIsLf) noexcept {
		if (pendingCr && !nextByteIsLf) rememberLineBreak(pendingCrOffset + 1);
		pendingCr = false;
	}

	bool succeeded() const noexcept {
		return !cancelled && !cancelRequested();
	}

  private:
	bool cancelRequested() const noexcept {
		return cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire);
	}

	void rememberLineBreak(Offset lineStartOffset) noexcept {
		++packet.lineBreakCount;
		if ((packet.lineBreakCount % lineindex::kLazyLineIndexStride) == 0) packet.checkpoints.push_back(LineIndexScanCheckpoint(lineStartOffset, packet.lineBreakCount));
	}

	LineIndexScanPacket &packet;
	bool previousWasCr;
	bool pendingCr;
	Offset pendingCrOffset;
	bool firstByte;
	bool cancelled;
	Offset nextCancelCheckOffset;
	const std::atomic_bool *cancelFlag;
};

} // namespace

bool ReadSnapshot::scanLineIndexSpan(LineIndexScanPacket &packet, std::uint64_t reservationId, Offset startOffset, Offset endOffset, const std::atomic_bool *cancelFlag) const {
	startOffset = std::min(startOffset, mLength);
	endOffset = std::min(endOffset, mLength);
	if (endOffset < startOffset) std::swap(startOffset, endOffset);
	packet = LineIndexScanPacket();
	packet.reservationId = reservationId;
	packet.startOffset = startOffset;
	packet.endOffset = endOffset;
	if (endOffset <= startOffset) return true;

	LineIndexSpanScanner scanner(packet, startOffset > 0 && charAt(startOffset - 1) == '\r', cancelFlag);
	if (const char *data = directTextData()) {
		scanner.consume(data + startOffset, endOffset - startOffset, startOffset);
	} else {
		Offset logicalOffset = 0;
		for (std::size_t pieceIndex = 0; pieceIndex < pieceCount() && logicalOffset < endOffset; ++pieceIndex) {
			const PieceChunkView chunk = pieceChunk(pieceIndex);
			if (chunk.data == nullptr || chunk.length == 0) continue;
			const Offset chunkStart = logicalOffset;
			const Offset chunkEnd = logicalOffset + chunk.length;
			logicalOffset = chunkEnd;
			if (chunkEnd <= startOffset) continue;
			const Offset takeStart = std::max(startOffset, chunkStart);
			const Offset takeEnd = std::min(endOffset, chunkEnd);
			if (takeEnd > takeStart) scanner.consume(chunk.data + (takeStart - chunkStart), takeEnd - takeStart, takeStart);
			if (!scanner.succeeded()) return false;
		}
	}
	const bool nextByteIsLf = endOffset < mLength && charAt(endOffset) == '\n';
	scanner.finish(nextByteIsLf);
	return scanner.succeeded();
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

} // namespace editor
} // namespace mr
