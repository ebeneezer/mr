#include "MRTextDocumentLineIndex.hpp"
#include <atomic>
#include <limits>

namespace mr {
namespace editor {

using namespace lineindex;

namespace {
constexpr Offset kLargeMappedEditNormalizationLength = static_cast<Offset>(8) * 1024 * 1024;
constexpr std::size_t kLargeMappedEditNormalizationPieceThreshold = 256;

struct LineIndexOffsetSpan {
	Offset start;
	Offset end;

	LineIndexOffsetSpan() noexcept : start(0), end(0) {
	}

	LineIndexOffsetSpan(Offset aStart, Offset anEnd) noexcept : start(aStart), end(anEnd) {
	}
};

inline std::size_t checkpointStrideForMode(bool directMode) noexcept {
	return directMode ? kLazyLineIndexStride : kPiecewiseLineIndexCheckpointStride;
}

void normalizeLineIndexSpans(std::vector<LineIndexOffsetSpan> &spans, Offset length) {
	for (LineIndexOffsetSpan &span : spans) {
		span.start = std::min(span.start, length);
		span.end = std::min(span.end, length);
		if (span.end < span.start) std::swap(span.start, span.end);
	}
	spans.erase(std::remove_if(spans.begin(), spans.end(), [](const LineIndexOffsetSpan &span) { return span.end <= span.start; }), spans.end());
	std::sort(spans.begin(), spans.end(), [](const LineIndexOffsetSpan &lhs, const LineIndexOffsetSpan &rhs) {
		if (lhs.start != rhs.start) return lhs.start < rhs.start;
		return lhs.end < rhs.end;
	});
	std::size_t writeIndex = 0;
	for (const LineIndexOffsetSpan &span : spans) {
		if (writeIndex == 0 || spans[writeIndex - 1].end < span.start) {
			spans[writeIndex] = span;
			++writeIndex;
		} else if (span.end > spans[writeIndex - 1].end)
			spans[writeIndex - 1].end = span.end;
	}
	spans.resize(writeIndex);
}

std::vector<LineIndexOffsetSpan> availableLineIndexGaps(const std::vector<LineIndexOffsetSpan> &blocked, Offset length) {
	std::vector<LineIndexOffsetSpan> gaps;
	Offset cursor = 0;
	for (const LineIndexOffsetSpan &span : blocked) {
		if (span.start > cursor) gaps.push_back(LineIndexOffsetSpan(cursor, span.start));
		if (span.end > cursor) cursor = span.end;
	}
	if (cursor < length) gaps.push_back(LineIndexOffsetSpan(cursor, length));
	return gaps;
}

bool nextEofLineIndexGap(const std::vector<LineIndexOffsetSpan> &gaps, Offset cursor, Offset targetLength, LineIndexOffsetSpan &result) noexcept {
	for (const LineIndexOffsetSpan &gap : gaps) {
		if (gap.end <= cursor) continue;
		result.start = std::max(gap.start, cursor);
		result.end = std::min(gap.end, result.start + targetLength);
		return result.end > result.start;
	}
	return false;
}

bool nextBofLineIndexGap(const std::vector<LineIndexOffsetSpan> &gaps, Offset cursor, Offset targetLength, LineIndexOffsetSpan &result) noexcept {
	for (std::size_t i = gaps.size(); i > 0; --i) {
		const LineIndexOffsetSpan &gap = gaps[i - 1];
		if (gap.start >= cursor) continue;
		result.end = std::min(gap.end, cursor);
		result.start = result.end > targetLength ? std::max(gap.start, result.end - targetLength) : gap.start;
		return result.end > result.start;
	}
	return false;
}

std::size_t estimatedLineIndexFromPrefix(Offset pos, Offset length, Offset indexedOffset, std::size_t indexedLine, std::size_t estimatedLineCount) noexcept {
	if (length == 0 || pos == 0) return 0;
	pos = std::min(pos, length);
	std::size_t estimate = 0;
	if (indexedOffset > 0 && indexedLine > 0) {
		const long double density = static_cast<long double>(indexedLine) / static_cast<long double>(indexedOffset);
		estimate = indexedLine + static_cast<std::size_t>(static_cast<long double>(pos > indexedOffset ? pos - indexedOffset : 0) * density);
	} else
		estimate = static_cast<std::size_t>((static_cast<long double>(pos) * std::max<std::size_t>(estimatedLineCount, 1)) / static_cast<long double>(length));
	return std::min(estimate, std::max<std::size_t>(estimatedLineCount, 1) - 1);
}

} // namespace

std::vector<LineIndexScanReservation> TextDocument::reserveLineIndexScanSpans(Offset focusOffset, std::size_t maximumCount, Offset targetSpanLength) {
	std::vector<LineIndexScanReservation> reserved;
	if (maximumCount == 0 || mLength == 0 || mLazyLineIndexComplete) return reserved;
	if (targetSpanLength == 0) targetSpanLength = 1;
	focusOffset = std::min(focusOffset, mLength);

	std::vector<LineIndexOffsetSpan> blocked;
	if (mLazyIndexedOffset > 0) blocked.push_back(LineIndexOffsetSpan(0, std::min(mLazyIndexedOffset, mLength)));
	for (const LineIndexScanPacket &packet : mPendingLineIndexScanPackets)
		blocked.push_back(LineIndexOffsetSpan(packet.startOffset, packet.endOffset));
	for (const LineIndexScanReservation &reservation : mLineIndexScanReservations)
		blocked.push_back(LineIndexOffsetSpan(reservation.startOffset, reservation.endOffset));
	normalizeLineIndexSpans(blocked, mLength);

	Offset bofCursor = focusOffset;
	Offset eofCursor = focusOffset;
	bool preferEof = true;
	while (reserved.size() < maximumCount) {
		const std::vector<LineIndexOffsetSpan> gaps = availableLineIndexGaps(blocked, mLength);
		LineIndexOffsetSpan span;
		LineIndexScanDirection direction = preferEof ? LineIndexScanDirection::Eof : LineIndexScanDirection::Bof;
		bool found = direction == LineIndexScanDirection::Eof ? nextEofLineIndexGap(gaps, eofCursor, targetSpanLength, span) : nextBofLineIndexGap(gaps, bofCursor, targetSpanLength, span);
		if (!found) {
			direction = direction == LineIndexScanDirection::Eof ? LineIndexScanDirection::Bof : LineIndexScanDirection::Eof;
			found = direction == LineIndexScanDirection::Eof ? nextEofLineIndexGap(gaps, eofCursor, targetSpanLength, span) : nextBofLineIndexGap(gaps, bofCursor, targetSpanLength, span);
		}
		if (!found) break;

		if (mNextLineIndexScanReservationId == 0) ++mNextLineIndexScanReservationId;
		LineIndexScanReservation reservation(mNextLineIndexScanReservationId++, span.start, span.end, direction);
		mLineIndexScanReservations.push_back(reservation);
		reserved.push_back(reservation);
		blocked.push_back(span);
		normalizeLineIndexSpans(blocked, mLength);
		if (direction == LineIndexScanDirection::Eof) eofCursor = span.end;
		else
			bofCursor = span.start;
		preferEof = !preferEof;
	}
	return reserved;
}

void TextDocument::releaseLineIndexScanReservation(std::uint64_t reservationId) noexcept {
	if (reservationId == 0) return;
	mLineIndexScanReservations.erase(std::remove_if(mLineIndexScanReservations.begin(), mLineIndexScanReservations.end(), [reservationId](const LineIndexScanReservation &reservation) { return reservation.reservationId == reservationId; }), mLineIndexScanReservations.end());
}

bool TextDocument::adoptLineIndexScanPacket(const LineIndexScanPacket &packet, std::size_t expectedVersion) noexcept {
	if (!matchesVersion(expectedVersion)) return false;
	if (packet.endOffset <= packet.startOffset || packet.endOffset > mLength) return false;
	releaseLineIndexScanReservation(packet.reservationId);
	if (packet.endOffset <= mLazyIndexedOffset) return true;
	if (packet.startOffset < mLazyIndexedOffset) return false;
	for (const LineIndexScanPacket &pending : mPendingLineIndexScanPackets) {
		if (packet.startOffset == pending.startOffset && packet.endOffset == pending.endOffset) return true;
		if (packet.startOffset < pending.endOffset && packet.endOffset > pending.startOffset) return false;
	}

	mPendingLineIndexScanPackets.push_back(packet);
	std::sort(mPendingLineIndexScanPackets.begin(), mPendingLineIndexScanPackets.end(), [](const LineIndexScanPacket &lhs, const LineIndexScanPacket &rhs) {
		if (lhs.startOffset != rhs.startOffset) return lhs.startOffset < rhs.startOffset;
		return lhs.endOffset < rhs.endOffset;
	});

	bool advanced = true;
	while (advanced && !mPendingLineIndexScanPackets.empty()) {
		advanced = false;
		for (std::size_t i = 0; i < mPendingLineIndexScanPackets.size(); ++i) {
			const LineIndexScanPacket &ready = mPendingLineIndexScanPackets[i];
			if (ready.startOffset != mLazyIndexedOffset) continue;
			const std::size_t baseLine = mLazyIndexedLine;
			for (const LineIndexScanCheckpoint &checkpoint : ready.checkpoints) {
				const LineIndexCheckpoint absolute(checkpoint.offset, baseLine + checkpoint.lineBreakCount);
				if (mLineIndexCheckpoints.empty() || absolute.offset > mLineIndexCheckpoints.back().offset) mLineIndexCheckpoints.push_back(absolute);
			}
			mLazyIndexedOffset = ready.endOffset;
			mLazyIndexedLine += ready.lineBreakCount;
			mPendingLineIndexScanPackets.erase(mPendingLineIndexScanPackets.begin() + static_cast<std::ptrdiff_t>(i));
			advanced = true;
			break;
		}
	}
	if (mLazyIndexedOffset == mLength) {
		mLazyLineIndexComplete = true;
		mLazyTotalLineCount = mLazyIndexedLine + 1;
		mPendingLineIndexScanPackets.clear();
	}
	return true;
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
	clearLineIndexScanLedger();
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
		if (pos < mLength && pos > 0 && data[pos] == '\n' && data[pos - 1] == '\r') --pos;
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

std::size_t ReadSnapshot::estimatedLineIndex(Offset pos) const noexcept {
	pos = clampOffset(pos);
	if (hasEditedLineStartIndex() || mLazyLineIndexComplete || pos <= mLazyIndexedOffset) return lineIndex(pos);
	return estimatedLineIndexFromPrefix(pos, mLength, mLazyIndexedOffset, mLazyIndexedLine, estimatedLineCount());
}

Offset ReadSnapshot::lineStartByIndex(std::size_t index) const noexcept {
	if (hasEditedLineStartIndex()) return lineStartFromExactStarts(*mEditedLineStarts, index);
	ensureLazyIndexSeeded();
	if (mLineIndexCheckpoints.empty()) return 0;
	return localInterpolatedLineStartByIndex(*this, index, mLineIndexCheckpoints, mLazyLineIndexComplete, mLazyTotalLineCount);
}

std::size_t ReadSnapshot::estimatedLineCount() const noexcept {
	if (hasEditedLineStartIndex()) return mEditedLineStarts->size();
	ensureLazyIndexSeeded();
	if (mLazyLineIndexComplete) return mLazyTotalLineCount;
	if (mLazyIndexedOffset == 0 || mLazyIndexedLine == 0) return std::max<std::size_t>(1, mLength / 80 + 1);

	const std::size_t observedLines = mLazyIndexedLine + 1;
	const std::size_t estimated = static_cast<std::size_t>((static_cast<long double>(mLength) * observedLines) / std::max<Offset>(mLazyIndexedOffset, 1));
	return std::max<std::size_t>(observedLines, estimated);
}

bool ReadSnapshot::exactLineCountKnown() const noexcept {
	if (hasEditedLineStartIndex()) return true;
	return mLazyLineIndexComplete;
}

void ReadSnapshot::compactLineIndexForUndo(Offset focusOffset) noexcept {
	if (!hasEditedLineStartIndex()) return;

	LineIndexCheckpoint anchor(0, 0);

	focusOffset = clampOffset(focusOffset);
	const Offset lookupOffset = focusOffset == mLength && mLength > 0 ? mLength - 1 : focusOffset;
	const std::size_t focusLine = lineIndexFromExactStarts(*mEditedLineStarts, lookupOffset);
	anchor.lineIndex = focusLine > kLazyLineIndexStride ? focusLine - kLazyLineIndexStride : 0;
	anchor.offset = lineStartFromExactStarts(*mEditedLineStarts, anchor.lineIndex);

	mEditedLineStarts.reset();
	mLineIndexCheckpoints.clear();
	mLineIndexCheckpoints.push_back(LineIndexCheckpoint(0, 0));
	if (anchor.offset != 0 || anchor.lineIndex != 0) mLineIndexCheckpoints.push_back(anchor);
	mLazyIndexedOffset = anchor.offset;
	mLazyIndexedLine = anchor.lineIndex;
	mLazyLineIndexComplete = (mLength == 0);
	mLazyTotalLineCount = std::max<std::size_t>(1, anchor.lineIndex + 1);
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
	(void)completeLineIndexWarmup(warmup);
	return warmup;
}

bool ReadSnapshot::warmLineIndexChunk(LineIndexWarmupData &warmup, std::size_t maxStrides, const std::atomic_bool *cancelFlag) const {
	ensureLazyIndexSeeded();
	if (maxStrides == 0) maxStrides = 1;
	for (std::size_t strideIndex = 0; strideIndex < maxStrides && !mLazyLineIndexComplete; ++strideIndex) {
		if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire)) return false;
		advanceLazyIndexByStride();
	}
	if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire)) return false;
	warmup.checkpoints = mLineIndexCheckpoints;
	warmup.lazyIndexedOffset = mLazyIndexedOffset;
	warmup.lazyIndexedLine = mLazyIndexedLine;
	warmup.lazyLineIndexComplete = mLazyLineIndexComplete;
	warmup.lazyTotalLineCount = mLazyTotalLineCount;
	return true;
}

bool ReadSnapshot::completeLineIndexWarmup(LineIndexWarmupData &warmup, const std::atomic_bool *cancelFlag) const {
	return warmLineIndexChunk(warmup, std::numeric_limits<std::size_t>::max(), cancelFlag);
}

bool ReadSnapshot::isLineBreakChar(char ch) const noexcept {
	return ch == '\n' || ch == '\r';
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
	return snapshot;
}

void TextDocument::restoreFromSnapshot(const ReadSnapshot &snapshot) {
	if (snapshot.empty() && snapshot.length() == 0) {
		setTextNoVersionBump("");
	} else {
		mDocumentId = snapshot.mDocumentId;
		mVersion = snapshot.mVersion;
		mMappedOriginal = snapshot.mMappedOriginal;
		mOriginalBuffer = snapshot.mOriginalBuffer != nullptr ? snapshot.mOriginalBuffer : std::make_shared<std::string>();
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
		clearLineIndexScanLedger();
		mEditedLineStarts = snapshot.mEditedLineStarts != nullptr ? std::const_pointer_cast<std::vector<Offset>>(snapshot.mEditedLineStarts) : std::shared_ptr<std::vector<Offset>>();
		if (mLength != 0 && (mPieces == nullptr || mPieces->empty())) {
			const std::size_t documentId = mDocumentId;
			const std::size_t version = mVersion;
			initializeFromOriginal(snapshot.text(), false);
			mDocumentId = documentId;
			mVersion = version;
		}
	}
}

Offset TextDocument::clampOffset(Offset pos) const noexcept {
	return std::min(pos, mLength);
}

std::size_t TextDocument::lineCount() const noexcept {
	if (hasEditedLineStartIndex()) return mEditedLineStarts->size();
	ensureLazyIndexComplete();
	return mLazyTotalLineCount;
}

Offset TextDocument::lineStart(Offset pos) const noexcept {
	Offset result = 0;
	if (hasEditedLineStartIndex()) result = lineStartFromExactStarts(*mEditedLineStarts, lineIndex(pos));
	else if (const char *data = directTextData()) {
		pos = clampOffset(pos);
		if (pos < mLength && pos > 0 && data[pos] == '\n' && data[pos - 1] == '\r') --pos;
		Offset breakPos = directFindPrevLineBreak(data, pos);
		result = breakPos == static_cast<Offset>(-1) ? 0 : breakPos + 1;
	} else
		result = piecewiseLineStart(*this, pos);
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
	return result;
}

Offset TextDocument::prevLine(Offset pos) const noexcept {
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
	return result;
}

std::size_t TextDocument::lineIndex(Offset pos) const noexcept {
	pos = clampOffset(pos);
	if (hasEditedLineStartIndex()) {
		if (pos == mLength && mLength > 0 && isLineBreakChar(charAt(mLength - 1))) return mEditedLineStarts->size() - 1;
		const Offset lookupPos = pos == mLength && mLength > 0 ? mLength - 1 : pos;
		return lineIndexFromExactStarts(*mEditedLineStarts, lookupPos);
	}
	ensureLazyIndexSeeded();
	std::size_t result = 0;
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
	if (checkpoint.offset >= pos) result = checkpoint.lineIndex;
	else if (const char *data = directTextData()) {
		const std::size_t delta = directCountLineBreaksInRange(data, mLength, checkpoint.offset, pos);
		result = checkpoint.lineIndex + delta;
	} else
		result = checkpoint.lineIndex + piecewiseCountLineBreaksInRange(*this, checkpoint.offset, pos);
	return result;
}

std::size_t TextDocument::estimatedLineIndex(Offset pos) const noexcept {
	pos = clampOffset(pos);
	if (hasEditedLineStartIndex() || mLazyLineIndexComplete || pos <= mLazyIndexedOffset) return lineIndex(pos);
	return estimatedLineIndexFromPrefix(pos, mLength, mLazyIndexedOffset, mLazyIndexedLine, estimatedLineCount());
}

Offset TextDocument::lineStartByIndex(std::size_t index) const noexcept {
	if (hasEditedLineStartIndex()) return lineStartFromExactStarts(*mEditedLineStarts, index);
	ensureLazyIndexSeeded();
	if (mLineIndexCheckpoints.empty()) return 0;
	if (mLazyLineIndexComplete && index >= mLazyTotalLineCount) index = mLazyTotalLineCount > 0 ? mLazyTotalLineCount - 1 : 0;
	return localInterpolatedLineStartByIndex(*this, index, mLineIndexCheckpoints, mLazyLineIndexComplete, mLazyTotalLineCount);
}

bool TextDocument::lineStartByIndexKnown(std::size_t index) const noexcept {
	if (hasEditedLineStartIndex()) return true;
	ensureLazyIndexSeeded();
	if (mLineIndexCheckpoints.empty()) return false;
	return mLazyLineIndexComplete || index <= mLazyIndexedLine;
}

std::size_t TextDocument::estimatedLineCount() const noexcept {
	if (hasEditedLineStartIndex()) return mEditedLineStarts->size();
	ensureLazyIndexSeeded();
	if (mLazyLineIndexComplete) return mLazyTotalLineCount;
	if (mLazyIndexedOffset == 0 || mLazyIndexedLine == 0) return std::max<std::size_t>(1, mLength / 80 + 1);

	const std::size_t observedLines = mLazyIndexedLine + 1;
	const std::size_t estimated = static_cast<std::size_t>((static_cast<long double>(mLength) * observedLines) / std::max<Offset>(mLazyIndexedOffset, 1));
	return std::max<std::size_t>(observedLines, estimated);
}

bool TextDocument::exactLineCountKnown() const noexcept {
	if (hasEditedLineStartIndex()) return true;
	return mLazyLineIndexComplete;
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
	resetLazyLineIndex();
	mEditedLineStarts.reset();
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
	resetLazyLineIndex();
	mEditedLineStarts.reset();
	if (bumpVersionFlag) bumpVersion();
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
	if (text.empty()) return;
	if (!hasEditedLineStartIndex()) {
		rebuildEditedLineStartIndex();
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
}

void TextDocument::updateEditedLineStartIndexForErase(Range range) {
	if (range.empty()) return;
	if (!hasEditedLineStartIndex()) {
		rebuildEditedLineStartIndex();
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
	clearLineIndexScanLedger();
}

void TextDocument::clearLineIndexScanLedger() noexcept {
	mPendingLineIndexScanPackets.clear();
	mLineIndexScanReservations.clear();
	mNextLineIndexScanReservationId = 1;
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
	if (length <= 0) return;
	clearLineIndexScanLedger();
	if (mLineIndexCheckpoints.empty()) return;
	offset = clampOffset(offset);
	for (LineIndexCheckpoint &checkpoint : mLineIndexCheckpoints)
		if (checkpoint.offset > offset) checkpoint.offset += length;
	if (mLazyIndexedOffset > offset) mLazyIndexedOffset += length;
}

void TextDocument::shiftLazyLineIndexForEraseWithoutLineBreak(Offset offset, Offset length) noexcept {
	if (length == 0) return;
	clearLineIndexScanLedger();
	if (mLineIndexCheckpoints.empty()) return;
	const Offset erasedEnd = offset + length;
	for (LineIndexCheckpoint &checkpoint : mLineIndexCheckpoints) {
		if (checkpoint.offset >= erasedEnd) checkpoint.offset -= length;
		else if (checkpoint.offset > offset)
			checkpoint.offset = offset;
	}
	if (mLazyIndexedOffset >= erasedEnd) mLazyIndexedOffset -= length;
	else if (mLazyIndexedOffset > offset)
		mLazyIndexedOffset = offset;
}

void TextDocument::invalidateLazyLineIndexFrom(Offset offset) noexcept {
	offset = clampOffset(offset);
	clearLineIndexScanLedger();
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
