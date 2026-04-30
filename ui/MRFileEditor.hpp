#ifndef MRFILEEDITOR_HPP
#define MRFILEEDITOR_HPP

#define Uses_TScroller
#define Uses_TScrollBar
#define Uses_TEditor
#define Uses_TIndicator
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TClipboard
#define Uses_TText
#define Uses_MsgBox
#include <tvision/tv.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#include "MRCoprocessor.hpp"
#include "../dialogs/MRDirtyGating.hpp"
#include "MRIndicator.hpp"
#include "MRTextBufferModel.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../app/MRCommands.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#include "../app/utils/MRStringUtils.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRWindowSupport.hpp"

class MREditWindow;

class MRFileEditor : public TScroller {
  public:
	struct LoadTiming {
		bool valid;
		std::size_t bytes;
		std::size_t lines;
		double mappedLoadMs;
		double lineCountMs;

		LoadTiming() noexcept : valid(false), bytes(0), lines(0), mappedLoadMs(0.0), lineCountMs(0.0) {
		}
	};

	MRFileEditor(const TRect &bounds, TScrollBar *aHScrollBar, TScrollBar *aVScrollBar, TIndicator *aIndicator, TStringView aFileName) noexcept
	    : TScroller(bounds, aHScrollBar, aVScrollBar), mIndicator(aIndicator), mReadOnly(false), mInsertMode(true), mAutoIndent(false), mSyntaxTitleHint(), mBufferModel(), mSelectionAnchor(0), mCursorVisualColumn(0), mIndicatorUpdateInProgress(false), mLineIndexWarmupTaskId(0), mLineIndexWarmupDocumentId(0), mLineIndexWarmupVersion(0), mSyntaxTokenCache(), mSyntaxWarmupTaskId(0), mSyntaxWarmupDocumentId(0), mSyntaxWarmupVersion(0), mSyntaxWarmupTopLine(0), mSyntaxWarmupBottomLine(0), mSyntaxWarmupLanguage(MRSyntaxLanguage::PlainText), mMiniMapWarmupTaskId(0), mMiniMapWarmupDocumentId(0), mMiniMapWarmupVersion(0), mMiniMapWarmupRows(0), mMiniMapWarmupBodyWidth(0), mMiniMapWarmupViewportWidth(0), mMiniMapWarmupBraille(true), mMiniMapWarmupWindowStartLine(0), mMiniMapWarmupWindowLineCount(0), mMiniMapWarmupReschedulePending(false), mMiniMapCache(), mSaveNormalizationCache(), mSaveNormalizationWarmupTaskId(0), mSaveNormalizationWarmupDocumentId(0), mSaveNormalizationWarmupVersion(0),
	      mSaveNormalizationWarmupOptionsHash(0), mSaveNormalizationWarmupSourceBytes(0), mSaveNormalizationWarmupStartedAt(std::chrono::steady_clock::time_point()), mSaveNormalizationThroughputBytesPerMicro(0.0), mSaveNormalizationThroughputSamples(0), mMiniMapInitialRenderReportedDocumentId(0), mBlockOverlayActive(false), mBlockOverlayMode(0), mBlockOverlayAnchor(0), mBlockOverlayEnd(0), mBlockOverlayTrackingCursor(false), mPreferredIndentColumn(1), mLastLoadTiming(), mLargeFileMetricsTraceValid(false), mLastLargeFileMetricsExactKnown(false), mLastLargeFileMetricsLimitY(0), mLastLargeFileMetricsMaxY(0), mLastLargeFileMetricsDeltaY(0), mLastLargeFileMetricsNewDeltaY(0) {
		fileName[0] = EOS;
		options |= ofFirstClick;
		eventMask |= evMouse | evKeyboard | evCommand;
		if (!aFileName.empty()) setPersistentFileName(aFileName);
		syncFromEditorState(false);
	}

	bool isReadOnly() const {
		return mReadOnly;
	}

	void setWindowEofMarkerColorOverride(bool enabled, TColorAttr color = 0) {
		mCustomWindowEofMarkerColorOverrideValid = enabled;
		mCustomWindowEofMarkerColorOverride = color;
		drawView();
	}

	void setReadOnly(bool readOnly) {
		if (mReadOnly != readOnly) {
			mReadOnly = readOnly;
			if (mReadOnly) setDocumentModified(false);
			syncFromEditorState(false);
		}
	}

	const char *persistentFileName() const noexcept {
		return hasPersistentFileName() ? fileName : "";
	}

	std::size_t persistentFileNameCapacity() const noexcept {
		return sizeof(fileName);
	}

	bool hasPersistentFileName() const {
		return fileName[0] != EOS;
	}

	void setPersistentFileName(TStringView name) noexcept {
		strnzcpy(fileName, name, sizeof(fileName));
		refreshSyntaxContext();
		scheduleSaveNormalizationWarmupIfNeeded();
	}

	void clearPersistentFileName() noexcept {
		fileName[0] = EOS;
		refreshSyntaxContext();
		scheduleSaveNormalizationWarmupIfNeeded();
	}

	bool isDocumentModified() const noexcept {
		return mBufferModel.isModified();
	}

	void setDocumentModified(bool changed) {
		mBufferModel.setModified(changed);
		if (!changed) {
			mBufferModel.document().flatten();
			mBufferModel.clearUndoRedo();
			clearDirtyRanges();
		}
		syncFromEditorState(false);
	}

	bool hasUndoHistory() const noexcept {
		return mBufferModel.undoStackDepth() > 0;
	}

	bool hasRedoHistory() const noexcept {
		return mBufferModel.redoStackDepth() > 0;
	}

	bool insertModeEnabled() const noexcept {
		return mInsertMode;
	}

	std::size_t originalBufferLength() const noexcept {
		return mBufferModel.document().originalLength();
	}

	std::size_t addBufferLength() const noexcept {
		return mBufferModel.document().addBufferLength();
	}

	std::size_t pieceCount() const noexcept {
		return mBufferModel.document().pieceCount();
	}

	bool hasMappedOriginalSource() const noexcept {
		return mBufferModel.document().hasMappedOriginal();
	}

	const std::string &mappedOriginalPath() const noexcept {
		return mBufferModel.document().mappedPath();
	}

	std::size_t estimatedLineCount() const noexcept {
		return mBufferModel.estimatedLineCount();
	}

	bool exactLineCountKnown() const noexcept {
		return mBufferModel.exactLineCountKnown();
	}

	std::size_t selectionLength() const noexcept {
		return mBufferModel.selection().range().length();
	}

	std::uint64_t pendingLineIndexWarmupTaskId() const noexcept {
		return mLineIndexWarmupTaskId;
	}

	std::uint64_t pendingSyntaxWarmupTaskId() const noexcept {
		return mSyntaxWarmupTaskId;
	}

	std::uint64_t pendingMiniMapWarmupTaskId() const noexcept {
		return mMiniMapWarmupTaskId;
	}

	std::uint64_t pendingSaveNormalizationWarmupTaskId() const noexcept {
		return mSaveNormalizationWarmupTaskId;
	}

	bool shouldReportMiniMapInitialRender() const noexcept {
		return mMiniMapInitialRenderReportedDocumentId != mBufferModel.documentId();
	}

	void markMiniMapInitialRenderReported() noexcept {
		mMiniMapInitialRenderReportedDocumentId = mBufferModel.documentId();
	}

	bool lineIndexWarmupPending() const noexcept {
		return mLineIndexWarmupTaskId != 0;
	}

	bool syntaxWarmupPending() const noexcept {
		return mSyntaxWarmupTaskId != 0;
	}

	bool miniMapWarmupPending() const noexcept {
		return mMiniMapWarmupTaskId != 0;
	}

	bool saveNormalizationWarmupPending() const noexcept {
		return mSaveNormalizationWarmupTaskId != 0;
	}

	bool usesApproximateMetrics() const noexcept {
		return useApproximateLargeFileMetrics();
	}

	void setInsertModeEnabled(bool on) {
		if (mInsertMode == on) return;
		mInsertMode = on;
		syncFromEditorState(false);
		if (owner != nullptr) message(owner, evBroadcast, cmUpdateTitle, 0);
	}

	int preferredIndentColumn() const noexcept {
		return mPreferredIndentColumn;
	}

	void setPreferredIndentColumn(int column) noexcept {
		if (column < 1) column = 1;
		if (column > 999) column = 999;
		mPreferredIndentColumn = column;
	}

	bool freeCursorMovementEnabled() const noexcept {
		return configuredCursorBehaviour() == MRCursorBehaviour::FreeMovement;
	}

	int actualCursorVisualColumn(std::size_t offset) const noexcept {
		return charColumn(mBufferModel.lineStart(offset), offset);
	}

	int displayedCursorColumn() const noexcept {
		const int actualColumn = actualCursorVisualColumn(mBufferModel.cursor());
		if (!freeCursorMovementEnabled()) return actualColumn;
		return std::max(actualColumn, mCursorVisualColumn);
	}

	void syncDisplayedCursorColumnFromCursor(bool preserveFreeColumn) noexcept {
		const int actualColumn = actualCursorVisualColumn(mBufferModel.cursor());

		if (!freeCursorMovementEnabled() || !preserveFreeColumn) {
			mCursorVisualColumn = actualColumn;
			return;
		}
		if (mCursorVisualColumn < actualColumn) mCursorVisualColumn = actualColumn;
	}

	void refreshConfiguredVisualSettings() {
		syncDisplayedCursorColumnFromCursor(true);
		syncIndicatorVisualSettings();
		updateMetrics();
		scheduleSyntaxWarmupIfNeeded();
		updateIndicator();
		drawView();
	}

	std::size_t cursorOffset() const noexcept {
		return mBufferModel.cursor();
	}

	std::size_t bufferLength() const noexcept {
		return mBufferModel.length();
	}

	std::size_t selectionStartOffset() const noexcept {
		return mBufferModel.selectionStart();
	}

	std::size_t selectionEndOffset() const noexcept {
		return mBufferModel.selectionEnd();
	}

	bool hasTextSelection() const noexcept {
		return mBufferModel.hasSelection();
	}

	std::size_t lineStartOffset(std::size_t pos) const noexcept {
		return mBufferModel.lineStart(pos);
	}

	std::size_t lineEndOffset(std::size_t pos) const noexcept {
		return mBufferModel.lineEnd(pos);
	}

	std::size_t nextLineOffset(std::size_t pos) const noexcept {
		return mBufferModel.nextLine(pos);
	}

	std::size_t prevLineOffset(std::size_t pos) const noexcept {
		return mBufferModel.prevLine(pos);
	}

	std::size_t lineIndexOfOffset(std::size_t pos) const noexcept {
		return mBufferModel.lineIndex(pos);
	}

	std::size_t columnOfOffset(std::size_t pos) const noexcept {
		return mBufferModel.column(pos);
	}

	char charAtOffset(std::size_t pos) const noexcept {
		return mBufferModel.charAt(pos);
	}

	std::string lineTextAtOffset(std::size_t pos) const {
		return mBufferModel.lineText(pos);
	}

	std::size_t nextCharOffset(std::size_t pos) noexcept {
		std::size_t len = mBufferModel.length();
		char bytes[4];
		std::size_t count = 0;

		if (pos >= len) return len;
		if (mBufferModel.charAt(pos) == '\r' && pos + 1 < len && mBufferModel.charAt(pos + 1) == '\n') return std::min(len, pos + 2);
		for (; count < sizeof(bytes) && pos + count < len; ++count)
			bytes[count] = mBufferModel.charAt(pos + count);
		std::size_t step = TText::next(TStringView(bytes, count));
		return std::min(len, pos + std::max<std::size_t>(step, 1));
	}

	std::size_t prevCharOffset(std::size_t pos) noexcept {
		char bytes[4];
		std::size_t start = 0;
		std::size_t count = 0;

		if (pos == 0) return 0;
		if (pos > 1 && mBufferModel.charAt(pos - 2) == '\r' && mBufferModel.charAt(pos - 1) == '\n') return pos - 2;
		start = pos > sizeof(bytes) ? pos - sizeof(bytes) : 0;
		count = pos - start;
		for (std::size_t i = 0; i < count; ++i)
			bytes[i] = mBufferModel.charAt(start + i);
		std::size_t step = TText::prev(TStringView(bytes, count), count);
		return pos - std::max<std::size_t>(step, 1);
	}

	std::size_t lineMoveOffset(std::size_t pos, int deltaLines, int targetVisualColumn = -1) noexcept {
		std::size_t target = std::min(pos, mBufferModel.length());

		if (targetVisualColumn < 0) targetVisualColumn = charColumn(mBufferModel.lineStart(pos), std::min(pos, mBufferModel.length()));

		if (deltaLines < 0) {
			for (std::size_t i = 0, distance = static_cast<std::size_t>(-deltaLines); i < distance; ++i) {
				std::size_t prev = prevLineOffset(target);
				if (prev == target) break;
				target = prev;
			}
		} else {
			for (int i = 0; i < deltaLines; ++i) {
				std::size_t next = nextLineOffset(target);
				if (next == target) break;
				target = next;
			}
		}

		return charPtrOffset(lineStartOffset(target), targetVisualColumn);
	}

	std::size_t tabStopMoveOffset(std::size_t pos, bool forward) noexcept {
		const std::size_t cursor = std::min(pos, mBufferModel.length());
		const std::size_t lineStart = lineStartOffset(cursor);
		const MREditSetupSettings settings = configuredEditSetupSettings();
		const int currentColumn = (freeCursorMovementEnabled() && cursor == mBufferModel.cursor() && !mBufferModel.hasSelection() ? displayedCursorColumn() : charColumn(lineStart, cursor)) + 1;
		const int targetColumn = forward ? nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn) : prevResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);

		return charPtrOffset(lineStart, targetColumn - 1);
	}

	std::size_t prevWordOffset(std::size_t pos) noexcept {
		std::size_t p = std::min(pos, mBufferModel.length());

		while (p > 0 && !isWordByte(mBufferModel.charAt(p - 1)))
			--p;
		while (p > 0 && isWordByte(mBufferModel.charAt(p - 1)))
			--p;
		return p;
	}

	std::size_t nextWordOffset(std::size_t pos) noexcept {
		std::size_t p = std::min(pos, mBufferModel.length());
		std::size_t len = mBufferModel.length();

		while (p < len && isWordByte(mBufferModel.charAt(p)))
			++p;
		while (p < len && !isWordByte(mBufferModel.charAt(p)))
			++p;
		return p;
	}

	std::size_t charPtrOffset(std::size_t start, int pos) noexcept {
		std::size_t lineStart = mBufferModel.lineStart(start);
		std::string lineText = mBufferModel.lineText(lineStart);
		TStringView line(lineText.data(), lineText.size());
		const MREditSetupSettings settings = configuredEditSetupSettings();
		std::size_t p = 0;
		int visual = 0;
		int target = std::max(pos, 0);

		while (p < line.size()) {
			std::size_t next = p;
			std::size_t width = 0;
			if (!nextDisplayChar(line, next, width, visual, settings)) break;
			if (visual + static_cast<int>(width) > target) break;
			visual += static_cast<int>(width);
			p = next;
		}
		return lineStart + p;
	}

	int charColumn(std::size_t start, std::size_t pos) const noexcept {
		std::size_t lineStart = mBufferModel.lineStart(start);
		std::string lineText = mBufferModel.lineText(lineStart);
		TStringView line(lineText.data(), lineText.size());
		const MREditSetupSettings settings = configuredEditSetupSettings();
		std::size_t p = 0;
		std::size_t end = std::min(pos, mBufferModel.length()) - lineStart;
		int visual = 0;

		end = std::min(end, line.size());
		while (p < end) {
			std::size_t next = p;
			std::size_t width = 0;
			if (!nextDisplayChar(line, next, width, visual, settings)) break;
			if (next > end) break;
			visual += static_cast<int>(width);
			p = next;
		}
		return visual;
	}

	void setCursorOffset(std::size_t pos, int = 0) {
		moveCursor(std::min(pos, mBufferModel.length()), false, false);
	}

	bool scrollWindowByLines(int deltaRows) {
		const std::size_t cursorBefore = cursorOffset();
		const int rowBefore = currentViewRow();
		const int targetVisualColumn = displayedCursorColumn();
		const std::size_t target = lineMoveOffset(cursorBefore, deltaRows, targetVisualColumn);

		if (deltaRows == 0) return true;
		if (target == cursorBefore) return false;
		moveCursor(target, false, false, targetVisualColumn);
		if (const int rowDelta = currentViewRow() - rowBefore; rowDelta != 0) scrollTo(std::max(delta.x, 0), std::max(delta.y + rowDelta, 0));
		return true;
	}

	std::size_t offsetForGlobalPoint(TPoint where) noexcept {
		return mouseOffset(makeLocal(where));
	}

	void setBlockOverlayState(int mode, std::size_t anchor, std::size_t end, bool active, bool trackCursor = false) {
		const std::size_t length = mBufferModel.length();

		if (!active || mode < 1 || mode > 3) {
			mBlockOverlayActive = false;
			mBlockOverlayMode = 0;
			mBlockOverlayAnchor = 0;
			mBlockOverlayEnd = 0;
			mBlockOverlayTrackingCursor = false;
			drawView();
			return;
		}
		mBlockOverlayActive = true;
		mBlockOverlayMode = mode;
		mBlockOverlayAnchor = std::min(anchor, length);
		mBlockOverlayEnd = std::min(end, length);
		mBlockOverlayTrackingCursor = trackCursor;
		drawView();
	}

	void setSelectionOffsets(std::size_t start, std::size_t end, Boolean = False) {
		start = std::min(start, mBufferModel.length());
		end = std::min(end, mBufferModel.length());
		mSelectionAnchor = start;
		mBufferModel.setSelection(start, end);
		syncFromEditorState(false);
	}

	void setFindMarkerRanges(const std::vector<std::pair<std::size_t, std::size_t>> &ranges) {
		std::vector<MRTextBufferModel::Range> normalized;
		const std::size_t length = mBufferModel.length();

		normalized.reserve(ranges.size());
		if (length != 0) {
			for (const auto &rangePair : ranges) {
				std::size_t start = std::min(rangePair.first, length);
				std::size_t end = std::min(rangePair.second, length);
				if (end < start) std::swap(start, end);
				if (end == start) {
					if (end < length) ++end;
					else if (start > 0)
						--start;
				}
				if (end > start) normalized.push_back(MRTextBufferModel::Range(start, end));
			}
		}
		normalizeRangeList(normalized);
		mFindMarkerRanges.swap(normalized);
		drawView();
	}

	void clearFindMarkerRanges() {
		if (mFindMarkerRanges.empty()) return;
		mFindMarkerRanges.clear();
		drawView();
	}

	void revealCursor(Boolean centerCursor = True) {
		ensureCursorVisible(centerCursor == True);
		updateIndicator();
		drawView();
	}

	void refreshViewState() {
		updateIndicator();
		drawView();
	}

	void update(uchar) {
		refreshViewState();
	}

	int currentLineNumber() const noexcept {
		return static_cast<int>(mBufferModel.lineIndex(mBufferModel.cursor())) + 1;
	}

	int currentViewRow() const noexcept {
		return std::max(1, static_cast<int>(mBufferModel.lineIndex(mBufferModel.cursor())) - delta.y + 1);
	}

	int visibleViewportRows() const noexcept {
		return std::max(1, visibleTextRows());
	}

	const MRTextBufferModel &bufferModel() const noexcept {
		return mBufferModel;
	}

	MRTextBufferModel &bufferModel() noexcept {
		return mBufferModel;
	}

	void syncFromEditorState(bool = true) {
		syncDisplayedCursorColumnFromCursor(true);
		refreshSyntaxContext();
		updateMetrics();
		syncIndicatorVisualSettings();
		updateIndicator();
	}

	void syncIndicatorVisualSettings() {
		if (auto *mrIndicator = dynamic_cast<MRIndicator *>(mIndicator)) {
			mrIndicator->setInsertMode(mInsertMode);
			mrIndicator->setWordWrap(configuredEditSetupSettings().wordWrap);
		}
	}

	void notifyWindowTaskStateChanged() {
		if (owner != nullptr) message(owner, evBroadcast, cmUpdateTitle, 0);
	}

	std::string snapshotText() const {
		return mBufferModel.text();
	}

	MRTextBufferModel::ReadSnapshot readSnapshot() const {
		return mBufferModel.readSnapshot();
	}

	MRTextBufferModel::Document documentCopy() const {
		return mBufferModel.document();
	}

	std::size_t documentId() const noexcept {
		return mBufferModel.documentId();
	}

	std::size_t documentVersion() const noexcept {
		return mBufferModel.version();
	}

	LoadTiming lastLoadTiming() const noexcept {
		return mLastLoadTiming;
	}

	bool applyLineIndexWarmup(const mr::editor::LineIndexWarmupData &warmup, std::size_t expectedVersion) {
		if (shouldTraceLargeFileDiagnostics()) {
			std::ostringstream detail;
			detail << "expected_version=" << expectedVersion << " checkpoints=" << warmup.checkpoints.size() << " indexed_offset=" << warmup.lazyIndexedOffset << " indexed_line=" << warmup.lazyIndexedLine << " complete=" << (warmup.lazyLineIndexComplete ? 1 : 0) << " total_lines=" << warmup.lazyTotalLineCount;
			traceLargeFileMessage("line-index-apply", detail.str());
		}
		if (!mBufferModel.adoptLineIndexWarmup(warmup, expectedVersion)) return false;
		mLineIndexWarmupTaskId = 0;
		mLineIndexWarmupDocumentId = 0;
		mLineIndexWarmupVersion = 0;
		notifyWindowTaskStateChanged();
		updateMetrics();
		updateIndicator();
		drawView();
		return true;
	}

	bool applySyntaxWarmup(const mr::coprocessor::SyntaxWarmupPayload &warmup, std::size_t expectedVersion, std::uint64_t expectedTaskId) {
		if (expectedTaskId == 0 || mSyntaxWarmupTaskId != expectedTaskId) return false;
		if (mBufferModel.documentId() != mSyntaxWarmupDocumentId || mBufferModel.version() != expectedVersion) return false;
		if (mBufferModel.language() != warmup.language) return false;

		mSyntaxTokenCache.clear();
		for (std::size_t i = 0; i < warmup.lines.size(); ++i)
			mSyntaxTokenCache[warmup.lines[i].lineStart] = warmup.lines[i].tokens;

		mSyntaxWarmupTaskId = 0;
		mSyntaxWarmupDocumentId = 0;
		mSyntaxWarmupVersion = 0;
		mSyntaxWarmupTopLine = 0;
		mSyntaxWarmupBottomLine = 0;
		mSyntaxWarmupLanguage = MRSyntaxLanguage::PlainText;
		notifyWindowTaskStateChanged();
		drawView();
		return true;
	}

	bool applyMiniMapWarmup(const mr::coprocessor::MiniMapWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId) {
		return applyMiniMapWarmupInternal(payload, expectedVersion, expectedTaskId);
	}

	bool applySaveNormalizationWarmup(const mr::coprocessor::SaveNormalizationWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId, double runMicros) {
		if (expectedTaskId == 0 || mSaveNormalizationWarmupTaskId != expectedTaskId) return false;
		if (mBufferModel.documentId() != mSaveNormalizationWarmupDocumentId || mBufferModel.version() != expectedVersion) return false;
		if (mSaveNormalizationWarmupOptionsHash != payload.optionsHash) return false;
		noteSaveNormalizationThroughput(payload.sourceBytes, runMicros);
		clearSaveNormalizationWarmupTask(expectedTaskId);
		return true;
	}

	void clearLineIndexWarmupTask(std::uint64_t expectedTaskId) noexcept {
		if (expectedTaskId != 0 && mLineIndexWarmupTaskId != expectedTaskId) return;
		if (mLineIndexWarmupTaskId == 0) return;
		mLineIndexWarmupTaskId = 0;
		mLineIndexWarmupDocumentId = 0;
		mLineIndexWarmupVersion = 0;
		notifyWindowTaskStateChanged();
	}

	void clearSyntaxWarmupTask(std::uint64_t expectedTaskId) noexcept {
		if (expectedTaskId != 0 && mSyntaxWarmupTaskId != expectedTaskId) return;
		if (mSyntaxWarmupTaskId == 0) return;
		mSyntaxWarmupTaskId = 0;
		mSyntaxWarmupDocumentId = 0;
		mSyntaxWarmupVersion = 0;
		mSyntaxWarmupTopLine = 0;
		mSyntaxWarmupBottomLine = 0;
		mSyntaxWarmupLanguage = MRSyntaxLanguage::PlainText;
		notifyWindowTaskStateChanged();
	}

	void clearMiniMapWarmupTask(std::uint64_t expectedTaskId) noexcept {
		clearMiniMapWarmupTaskInternal(expectedTaskId);
	}

	void clearSaveNormalizationWarmupTask(std::uint64_t expectedTaskId = 0) noexcept {
		if (expectedTaskId != 0 && mSaveNormalizationWarmupTaskId != expectedTaskId) return;
		if (mSaveNormalizationWarmupTaskId == 0) return;
		mSaveNormalizationWarmupTaskId = 0;
		mSaveNormalizationWarmupDocumentId = 0;
		mSaveNormalizationWarmupVersion = 0;
		mSaveNormalizationWarmupOptionsHash = 0;
		mSaveNormalizationWarmupSourceBytes = 0;
		mSaveNormalizationWarmupStartedAt = std::chrono::steady_clock::time_point();
		notifyWindowTaskStateChanged();
	}

	void setSyntaxTitleHint(const std::string &title) {
		mSyntaxTitleHint = title;
		refreshSyntaxContext();
		updateMetrics();
		updateIndicator();
	}

	const char *syntaxLanguageName() const noexcept {
		return mBufferModel.languageName();
	}

	MRSyntaxLanguage syntaxLanguage() const noexcept {
		return mBufferModel.language();
	}

	bool canSaveInPlace() const {
		std::string persistentName;

		if (mReadOnly || !hasPersistentFileName()) return false;
		persistentName = trimAscii(fileName);
		if (upperAscii(persistentName) == "?NO-FILE?") return false;
		if (looksLikeUri(persistentName)) return false;
		return true;
	}

	bool canSaveAs() const {
		return !mReadOnly;
	}

	bool loadMappedFile(TStringView path, std::string &error) {
		MRTextBufferModel::Document document;
		const auto mapStartedAt = std::chrono::steady_clock::now();

		mLastLoadTiming = LoadTiming();

		if (!document.loadMappedFile(path, error)) return false;
		const double mappedLoadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mapStartedAt).count();
		const auto lineCountStartedAt = std::chrono::steady_clock::now();
		const std::size_t lines = document.lineCount();
		const double lineCountMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lineCountStartedAt).count();

		mLastLoadTiming.valid = true;
		mLastLoadTiming.bytes = document.length();
		mLastLoadTiming.lines = lines;
		mLastLoadTiming.mappedLoadMs = mappedLoadMs;
		mLastLoadTiming.lineCountMs = lineCountMs;
		setPersistentFileName(path);
		if (!adoptCommittedDocument(document, 0, 0, 0, false)) {
			clearPersistentFileName();
			mLastLoadTiming = LoadTiming();
			error = "Unable to adopt mapped document.";
			return false;
		}
		return true;
	}

	Boolean saveInPlace() noexcept {
		if (!canSaveInPlace()) return False;
		Boolean ok = writeDocumentToPath(fileName) ? True : False;
		if (ok == True) setDocumentModified(false);
		return ok;
	}

	Boolean saveAsWithPrompt() noexcept {
		char saveName[MAXPATH];

		if (!canSaveAs()) return False;
		if (hasPersistentFileName()) strnzcpy(saveName, fileName, sizeof(saveName));
		else
			initRememberedLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName, sizeof(saveName), "*.*");
		if (TEditor::editorDialog(edSaveAs, saveName) == cmCancel) return False;
		fexpand(saveName);
		if (!samePath(saveName, fileName) && !confirmOverwriteForSaveAs(saveName)) return False;
		if (!writeDocumentToPath(saveName)) return False;
		rememberLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName);
		setPersistentFileName(saveName);
		if (owner != nullptr) message((TView *)owner, evBroadcast, cmUpdateTitle, 0);
		setDocumentModified(false);
		return True;
	}

	Boolean saveAsWithoutOverwritePrompt() noexcept {
		char saveName[MAXPATH];

		if (!canSaveAs()) return False;
		if (hasPersistentFileName()) strnzcpy(saveName, fileName, sizeof(saveName));
		else
			initRememberedLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName, sizeof(saveName), "*.*");
		if (TEditor::editorDialog(edSaveAs, saveName) == cmCancel) return False;
		fexpand(saveName);
		if (!writeDocumentToPath(saveName)) return False;
		rememberLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName);
		setPersistentFileName(saveName);
		if (owner != nullptr) message((TView *)owner, evBroadcast, cmUpdateTitle, 0);
		setDocumentModified(false);
		return True;
	}

	void pushUndoSnapshot() {
		MRTextBufferModel::CustomUndoRecord record;
		record.preSnapshot = mBufferModel.readSnapshot();
		record.cursor = mBufferModel.cursor();
		record.modifiedState = mBufferModel.isModified();
		if (mBufferModel.hasSelection()) {
			record.selAnchor = mBufferModel.selection().range().start;
			record.selCursor = mBufferModel.selection().range().end;
		} else {
			record.selAnchor = 0;
			record.selCursor = 0;
		}
		if (owner != nullptr) {
			record.blockMode = mBlockOverlayMode;
			record.blockAnchor = mBlockOverlayAnchor;
			record.blockEnd = mBlockOverlayEnd;
			record.blockMarkingOn = mBlockOverlayActive;
		}
		mBufferModel.pushUndoSnapshot(record);
	}

	bool replaceBufferData(const char *data, uint length) {
		std::string text;
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "replace-buffer-data");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;

		if (data != nullptr && length != 0) text.assign(data, length);
		transaction.setText(text);
		preview = mBufferModel.document();
		pushUndoSnapshot();
		commit = preview.tryApply(transaction);
		if (!commit.applied()) {
			mBufferModel.popUndoSnapshot();
			return false;
		}
		return adoptCommittedDocument(preview, 0, 0, 0, false);
	}

	bool replaceBufferText(const char *text) {
		uint length = text != nullptr ? static_cast<uint>(std::strlen(text)) : 0;
		return replaceBufferData(text, length);
	}

	bool appendBufferData(const char *data, uint length) {
		std::string text;
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "append-buffer-data");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;
		std::size_t endPtr = mBufferModel.length();

		if (length == 0) return true;
		if (data != nullptr) text.assign(data, length);
		transaction.insert(endPtr, text);
		preview = mBufferModel.document();
		pushUndoSnapshot();
		commit = preview.tryApply(transaction);
		if (!commit.applied()) {
			mBufferModel.popUndoSnapshot();
			return false;
		}
		return adoptCommittedDocument(preview, endPtr + text.size(), endPtr + text.size(), endPtr + text.size(), false);
	}

	bool appendBufferText(const char *text) {
		uint length = text != nullptr ? static_cast<uint>(std::strlen(text)) : 0;
		return appendBufferData(text, length);
	}

	bool replaceRangeAndSelect(uint start, uint end, const char *data, uint length) {
		std::string text;
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "replace-range-select");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;
		MRTextBufferModel::Range range;

		if (mReadOnly) return false;
		if (end < start) std::swap(start, end);
		range = MRTextBufferModel::Range(start, end).clamped(mBufferModel.length());
		if (data != nullptr && length != 0) text.assign(data, length);
		transaction.replace(range, text);
		preview = mBufferModel.document();
		pushUndoSnapshot();
		commit = preview.tryApply(transaction);
		if (!commit.applied()) {
			mBufferModel.popUndoSnapshot();
			return false;
		}
		return adoptCommittedDocument(preview, range.start, range.start, range.start + text.size(), true, &commit.change);
	}

	int paddingColumnsBeforeInsertAtCursor() const noexcept {
		const std::size_t cursor = mBufferModel.cursor();
		const std::size_t lineEnd = lineEndOffset(cursor);

		if (!freeCursorMovementEnabled() || mBufferModel.hasSelection() || cursor != lineEnd) return 0;
		return std::max(0, displayedCursorColumn() - actualCursorVisualColumn(cursor));
	}

	bool insertBufferText(const std::string &text) {
		std::string insertedText = text;
		std::size_t start = mBufferModel.cursor();
		std::size_t end = start;
		MRTextBufferModel::Range range;
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "insert-buffer-text");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;

		if (mReadOnly) return false;
		if (!insertedText.empty()) {
			const int paddingColumns = paddingColumnsBeforeInsertAtCursor();
			if (paddingColumns > 0) insertedText.insert(0, static_cast<std::size_t>(paddingColumns), ' ');
		}
		if (mBufferModel.hasSelection()) {
			range = mBufferModel.selection().range();
			start = range.start;
			end = range.end;
		} else if (!mInsertMode) {
			std::size_t endSel = mBufferModel.cursor();
			for (std::string::size_type i = 0; i < insertedText.size() && endSel < lineEndOffset(start); ++i)
				endSel = nextCharOffset(endSel);
			end = endSel;
		}
		range = MRTextBufferModel::Range(start, end).clamped(mBufferModel.length());
		transaction.replace(range, insertedText);
		preview = mBufferModel.document();
		pushUndoSnapshot();
		commit = preview.tryApply(transaction);
		if (!commit.applied()) {
			mBufferModel.popUndoSnapshot();
			return false;
		}
		start = range.start + insertedText.size();
		return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
	}

	bool replaceCurrentLineText(const std::string &text) {
		std::size_t start = mBufferModel.lineStart(mBufferModel.cursor());
		std::size_t end = mBufferModel.lineEnd(mBufferModel.cursor());
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "replace-current-line");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;

		if (mReadOnly) return false;
		transaction.replace(MRTextBufferModel::Range(start, end), text);
		preview = mBufferModel.document();
		pushUndoSnapshot();
		commit = preview.tryApply(transaction);
		if (!commit.applied()) {
			mBufferModel.popUndoSnapshot();
			return false;
		}
		return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
	}

	bool centerCurrentLine(int leftMargin, int rightMargin) {
		std::string text;
		std::string trimmed;
		const int safeLeftMargin = std::max(1, leftMargin);
		const int safeRightMargin = std::max(safeLeftMargin, rightMargin);
		int contentWidth = 0;
		int padWidth = 0;

		if (mReadOnly) return false;
		text = lineTextAtOffset(cursorOffset());
		trimmed = trimAscii(text);
		if (trimmed.empty()) return replaceCurrentLineText(std::string());
		contentWidth = displayWidthForText(trimmed, configuredEditSetupSettings());
		padWidth = std::max(safeLeftMargin - 1, ((safeRightMargin - contentWidth) / 2));
		return replaceCurrentLineText(std::string(static_cast<std::size_t>(padWidth), ' ') + trimmed);
	}

	bool copyCharFromLineAbove() {
		const std::size_t cursor = cursorOffset();
		const std::size_t currentLineStart = lineStartOffset(cursor);
		const std::size_t previousLineStart = prevLineOffset(currentLineStart);
		const std::size_t previousLineEnd = lineEndOffset(previousLineStart);
		const int targetColumn = charColumn(currentLineStart, cursor);
		const std::size_t sourceOffset = charPtrOffset(previousLineStart, targetColumn);
		char ch = '\0';

		if (mReadOnly || currentLineStart == previousLineStart || sourceOffset >= previousLineEnd) return false;
		ch = charAtOffset(sourceOffset);
		if (ch == '\0' || ch == '\r' || ch == '\n') return false;
		return insertBufferText(std::string(1, ch));
	}

	bool formatParagraph(int rightMargin) {
		return formatParagraph(configuredEditSetupSettings().leftMargin, rightMargin);
	}

	std::string buildFormattedParagraphText(std::string_view paragraphText, int leftMargin, int rightMargin) const {
		const int safeLeftMargin = std::max(1, leftMargin);
		const int safeRightMargin = std::max(safeLeftMargin, rightMargin);
		const std::size_t indentWidth = static_cast<std::size_t>(safeLeftMargin - 1);
		const std::size_t contentWidth = static_cast<std::size_t>(std::max(1, safeRightMargin - safeLeftMargin + 1));
		std::vector<std::string> words;
		std::string formattedText;
		std::string currentWord;
		std::string currentLine;
		std::size_t currentLineLength = 0;

		for (std::size_t i = 0; i <= paragraphText.length(); ++i) {
			if (i == paragraphText.length() || std::isspace(static_cast<unsigned char>(paragraphText[i])) != 0) {
				if (!currentWord.empty()) {
					words.push_back(currentWord);
					currentWord.clear();
				}
				continue;
			}
			currentWord.push_back(paragraphText[i]);
		}
		if (words.empty()) return std::string();
		for (const std::string &word : words) {
			const std::size_t projectedLength = currentLine.empty() ? word.size() : currentLineLength + 1 + word.size();
			if (!currentLine.empty() && projectedLength > contentWidth) {
				if (!formattedText.empty()) formattedText.push_back('\n');
				formattedText.append(indentWidth, ' ');
				formattedText += currentLine;
				currentLine = word;
				currentLineLength = word.size();
				continue;
			}
			if (!currentLine.empty()) {
				currentLine.push_back(' ');
				++currentLineLength;
			}
			currentLine += word;
			currentLineLength += word.size();
		}
		if (!currentLine.empty()) {
			if (!formattedText.empty()) formattedText.push_back('\n');
			formattedText.append(indentWidth, ' ');
			formattedText += currentLine;
		}
		return formattedText;
	}

	bool formatParagraph(int leftMargin, int rightMargin) {
		if (mReadOnly) return false;

		std::size_t start = mBufferModel.cursor();
		std::size_t end = start;

		while (start > 0) {
			std::size_t prevLineStart = mBufferModel.lineStart(mBufferModel.prevLine(start));
			if (isBlankString(mBufferModel.lineText(prevLineStart))) break;
			start = prevLineStart;
		}

		while (end < mBufferModel.length()) {
			std::size_t nextLineStart = mBufferModel.nextLine(end);
			if (isBlankString(mBufferModel.lineText(end))) break;
			end = nextLineStart;
		}

		if (start == end) return true;

		std::string paragraphText;
		paragraphText.reserve(end - start);
		std::size_t current = start;
		while (current < end) {
			std::string chunk = mBufferModel.document().lineText(current);
			if (!paragraphText.empty()) paragraphText.push_back('\n');
			paragraphText += chunk;
			current = mBufferModel.document().nextLine(current);
		}
		std::string formattedText = buildFormattedParagraphText(paragraphText, leftMargin, rightMargin);

		if (formattedText.empty()) return true;

		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "format-paragraph");
		transaction.replace(MRTextBufferModel::Range(start, end), formattedText);

		MRTextBufferModel::Document preview = mBufferModel.document();
		pushUndoSnapshot();
		MRTextBufferModel::CommitResult commit = preview.tryApply(transaction);

		if (!commit.applied()) {
			mBufferModel.popUndoSnapshot();
			return false;
		}

		return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
	}

	bool formatDocument(int leftMargin, int rightMargin) {
		std::string formattedText;
		const std::size_t length = mBufferModel.length();
		const std::size_t cursor = mBufferModel.cursor();
		std::size_t current = 0;

		if (mReadOnly) return false;
		while (current < length) {
			if (isBlankString(mBufferModel.lineText(current))) {
				formattedText.push_back('\n');
				current = mBufferModel.nextLine(current);
				continue;
			}
			std::string paragraphText;
			const std::size_t paragraphStart = current;
			std::size_t paragraphEnd = current;

			while (paragraphEnd < length && !isBlankString(mBufferModel.lineText(paragraphEnd))) {
				if (!paragraphText.empty()) paragraphText.push_back('\n');
				paragraphText += mBufferModel.document().lineText(paragraphEnd);
				paragraphEnd = mBufferModel.document().nextLine(paragraphEnd);
			}
			if (!formattedText.empty() && formattedText.back() != '\n') formattedText.push_back('\n');
			formattedText += buildFormattedParagraphText(paragraphText, leftMargin, rightMargin);
			current = paragraphEnd;
			if (current == paragraphStart) break;
		}
		return replaceWholeBuffer(formattedText, std::min(cursor, formattedText.size()));
	}

	bool justifyParagraph(int leftMargin, int rightMargin) {
		if (mReadOnly) return false;

		std::size_t start = mBufferModel.cursor();
		std::size_t end = start;
		const int safeLeftMargin = std::max(1, leftMargin);
		const int safeRightMargin = std::max(safeLeftMargin, rightMargin);
		const std::size_t indentWidth = static_cast<std::size_t>(safeLeftMargin - 1);
		const std::size_t contentWidth = static_cast<std::size_t>(std::max(1, safeRightMargin - safeLeftMargin + 1));
		std::vector<std::string> words;
		std::vector<std::vector<std::string>> lines;
		std::vector<std::string> currentLineWords;
		std::size_t currentLineLength = 0;
		std::string paragraphText;
		std::string currentWord;
		std::string justifiedText;

		while (start > 0) {
			std::size_t prevLineStart = mBufferModel.lineStart(mBufferModel.prevLine(start));
			if (isBlankString(mBufferModel.lineText(prevLineStart))) break;
			start = prevLineStart;
		}
		while (end < mBufferModel.length()) {
			std::size_t nextLineStart = mBufferModel.nextLine(end);
			if (isBlankString(mBufferModel.lineText(end))) break;
			end = nextLineStart;
		}
		if (start == end) return true;
		paragraphText.reserve(end - start);
		for (std::size_t current = start; current < end; current = mBufferModel.document().nextLine(current))
			if (std::string chunk = mBufferModel.document().lineText(current); true) {
				if (!paragraphText.empty()) paragraphText.push_back('\n');
				paragraphText += chunk;
			}
		for (std::size_t i = 0; i <= paragraphText.length(); ++i) {
			if (i == paragraphText.length() || std::isspace(static_cast<unsigned char>(paragraphText[i]))) {
				if (!currentWord.empty()) {
					words.push_back(currentWord);
					currentWord.clear();
				}
				continue;
			}
			currentWord.push_back(paragraphText[i]);
		}
		if (words.empty()) return true;
		for (const std::string &word : words) {
			const std::size_t projectedLength = currentLineWords.empty() ? word.size() : currentLineLength + 1 + word.size();
			if (!currentLineWords.empty() && projectedLength > contentWidth) {
				lines.push_back(currentLineWords);
				currentLineWords.clear();
				currentLineLength = 0;
			}
			if (!currentLineWords.empty()) ++currentLineLength;
			currentLineWords.push_back(word);
			currentLineLength += word.size();
		}
		if (!currentLineWords.empty()) lines.push_back(currentLineWords);
		for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
			const bool lastLine = lineIndex + 1 == lines.size();
			std::size_t wordsLength = 0;
			std::string line(indentWidth, ' ');

			for (const std::string &word : lines[lineIndex])
				wordsLength += word.size();
			if (lastLine || lines[lineIndex].size() == 1) {
				for (std::size_t i = 0; i < lines[lineIndex].size(); ++i) {
					if (i != 0) line.push_back(' ');
					line += lines[lineIndex][i];
				}
			} else {
				const std::size_t gapCount = lines[lineIndex].size() - 1;
				const std::size_t totalSpaces = contentWidth > wordsLength ? contentWidth - wordsLength : gapCount;
				const std::size_t baseGap = totalSpaces / gapCount;
				const std::size_t extraGap = totalSpaces % gapCount;

				for (std::size_t i = 0; i < lines[lineIndex].size(); ++i) {
					if (i != 0) line.append(baseGap + (i <= extraGap ? 1 : 0), ' ');
					line += lines[lineIndex][i];
				}
			}
			if (!justifiedText.empty()) justifiedText.push_back('\n');
			justifiedText += line;
		}
		return replaceRangeAndSelect(static_cast<uint>(start), static_cast<uint>(end), justifiedText.data(), static_cast<uint>(justifiedText.size()));
	}

	bool deleteCharsAtCursor(int count) {
		std::size_t start = mBufferModel.cursor();
		std::size_t end = start;
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "delete-chars-at-cursor");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;

		if (mReadOnly) return false;
		if (count <= 0) return true;
		for (int i = 0; i < count && end < mBufferModel.length(); ++i)
			end = nextCharOffset(end);
		if (end <= start) return true;
		transaction.erase(MRTextBufferModel::Range(start, end));
		preview = mBufferModel.document();
		pushUndoSnapshot();
		commit = preview.tryApply(transaction);
		if (!commit.applied()) {
			mBufferModel.popUndoSnapshot();
			return false;
		}
		return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
	}

	bool deleteCurrentLineText() {
		std::size_t start = mBufferModel.lineStart(mBufferModel.cursor());
		std::size_t end = mBufferModel.nextLine(mBufferModel.cursor());
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "delete-current-line");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;

		if (mReadOnly) return false;
		transaction.erase(MRTextBufferModel::Range(start, end));
		preview = mBufferModel.document();
		pushUndoSnapshot();
		commit = preview.tryApply(transaction);
		if (!commit.applied()) {
			mBufferModel.popUndoSnapshot();
			return false;
		}
		return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
	}

	bool replaceWholeBuffer(const std::string &text, std::size_t cursorPos) {
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "replace-whole-buffer");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;

		if (mReadOnly) return false;
		transaction.setText(text);
		preview = mBufferModel.document();
		pushUndoSnapshot();
		commit = preview.tryApply(transaction);
		if (!commit.applied()) {
			mBufferModel.popUndoSnapshot();
			return false;
		}
		cursorPos = std::min(cursorPos, text.size());
		return adoptCommittedDocument(preview, cursorPos, cursorPos, cursorPos, true, &commit.change);
	}

	MRTextBufferModel::CommitResult applyStagedTransaction(const MRTextBufferModel::StagedTransaction &transaction, std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState = true) {
		MRTextBufferModel::Document preview = mBufferModel.document();
		pushUndoSnapshot();
		MRTextBufferModel::CommitResult result = preview.tryApply(transaction);

		if (result.applied()) {
			adoptCommittedDocument(preview, cursorPos, selStart, selEnd, modifiedState, &result.change);
		} else {
			mBufferModel.popUndoSnapshot();
		}
		return result;
	}

	bool newLineWithIndent(const std::string &fill) {
		return insertBufferText(std::string("\n") + fill);
	}

	bool newLineWithPreferredIndent() {
		return newLineWithIndent(preferredIndentFill());
	}

	void effectiveFormatMargins(const MREditSetupSettings &settings, int &leftMargin, int &rightMargin) const noexcept {
		std::string normalized;

		leftMargin = settings.leftMargin;
		rightMargin = settings.rightMargin;
		if (normalizeEditFormatLine(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, normalized, &leftMargin, &rightMargin, nullptr)) return;
		leftMargin = std::max(1, settings.leftMargin);
		rightMargin = std::max(leftMargin, settings.rightMargin > 0 ? settings.rightMargin : 78);
	}

	bool wrapCurrentLineOnce(int leftMargin, int rightMargin) {
		const std::size_t cursor = cursorOffset();
		const std::size_t lineStart = lineStartOffset(cursor);
		const std::size_t lineEnd = lineEndOffset(cursor);
		const int safeLeftMargin = std::max(1, leftMargin);
		const int safeRightMargin = std::max(safeLeftMargin, rightMargin);
		const int lineWidth = charColumn(lineStart, lineEnd);
		const std::string indent(static_cast<std::size_t>(safeLeftMargin - 1), ' ');
		const std::string replacement = "\n" + indent;
		std::size_t limitOffset = std::min(charPtrOffset(lineStart, safeRightMargin), lineEnd);
		std::size_t replaceStart = limitOffset;
		std::size_t replaceEnd = limitOffset;
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "live-word-wrap-line");
		std::size_t newCursor = cursor;

		if (lineWidth <= safeRightMargin) return false;
		for (std::size_t probe = limitOffset; probe > lineStart; probe = prevCharOffset(probe)) {
			const std::size_t candidate = prevCharOffset(probe);
			const char ch = charAtOffset(candidate);

			if (ch != ' ' && ch != '\t') continue;
			replaceStart = candidate;
			replaceEnd = probe;
			while (replaceStart > lineStart) {
				const std::size_t previous = prevCharOffset(replaceStart);
				const char previousChar = charAtOffset(previous);
				if (previousChar != ' ' && previousChar != '\t') break;
				replaceStart = previous;
			}
			while (replaceEnd < lineEnd) {
				const char nextChar = charAtOffset(replaceEnd);
				if (nextChar != ' ' && nextChar != '\t') break;
				replaceEnd = nextCharOffset(replaceEnd);
			}
			break;
		}
		transaction.replace(MRTextBufferModel::Range(replaceStart, replaceEnd), replacement);
		if (cursor <= replaceStart) newCursor = cursor;
		else if (cursor >= replaceEnd)
			newCursor = cursor - (replaceEnd - replaceStart) + replacement.size();
		else
			newCursor = replaceStart + replacement.size();
		return applyStagedTransaction(transaction, newCursor, newCursor, newCursor, true).applied();
	}

	void applyLiveWordWrapAfterTextInput() {
		MREditSetupSettings settings = configuredEditSetupSettings();
		int leftMargin = 1;
		int rightMargin = 78;

		if (mReadOnly || !settings.wordWrap) return;
		effectiveFormatMargins(settings, leftMargin, rightMargin);
		for (int wraps = 0; wraps < 64; ++wraps)
			if (!wrapCurrentLineOnce(leftMargin, rightMargin)) break;
	}

	virtual void draw() override {
		syncScrollBarsToState();
		MREditSetupSettings editSettings = configuredEditSetupSettings();
		std::size_t topLine = static_cast<std::size_t>(std::max(delta.y, 0));
		std::size_t linePtr = lineStartForIndex(topLine);
		std::size_t lineIndex = topLine;
		std::size_t totalLines = 1;
		TextViewportGeometry viewport = textViewportGeometryFor(editSettings);
		bool showLineNumbers = viewport.lineNumberWidth > 0;
		bool drawCodeFolding = viewport.codeFoldingWidth > 0;
		bool zeroFillLineNumbers = showLineNumbers && editSettings.lineNumZeroFill;
		int textWidth = viewport.width;
		MRTextBufferModel::Range selection = mBufferModel.selection().range().normalized();
		if ((selection.end <= selection.start) && owner != nullptr) {
			std::size_t lastSearchStart = 0;
			std::size_t lastSearchEnd = 0;
			std::size_t lastSearchCursor = 0;
			std::string currentFileName = hasPersistentFileName() ? std::string(fileName) : std::string();

			if (mrvmUiCopyWindowLastSearch(owner, currentFileName, lastSearchStart, lastSearchEnd, lastSearchCursor) && lastSearchEnd > lastSearchStart) {
				selection.start = std::min(lastSearchStart, mBufferModel.length());
				selection.end = std::min(lastSearchEnd, mBufferModel.length());
			}
		}
		MiniMapPalette miniMapPalette = resolveMiniMapPalette();
		const bool drawMiniMap = viewport.miniMapBodyWidth > 0 && viewport.miniMapInfoX >= 0;
		const bool miniMapUseBraille = useBrailleMiniMapRenderer();
		std::string viewportMarkerGlyph = normalizedMiniMapViewportMarkerGlyph(editSettings.miniMapMarkerGlyph);
		const int miniMapRows = std::max(0, miniMapViewportRows());
		if (mBufferModel.exactLineCountKnown()) totalLines = std::max<std::size_t>(1, mBufferModel.lineCount());
		else
			totalLines = std::max<std::size_t>(1, std::max<std::size_t>(mBufferModel.estimatedLineCount(), topLine + static_cast<std::size_t>(std::max(miniMapRows, 1))));
		if (drawMiniMap) scheduleMiniMapWarmupIfNeeded(viewport, miniMapUseBraille, totalLines, topLine);
		MiniMapOverlayState miniMapOverlay = computeMiniMapOverlayState(selection, totalLines, viewport.width, viewport.miniMapBodyWidth, miniMapUseBraille, editSettings);

		if (editSettings.formatRuler && viewport.topInset > 0) drawFormatRulerOverlay(viewport, editSettings);
		const int textRows = std::max(0, visibleTextRows());
		for (int y = 0; y < textRows; ++y) {
			TDrawBuffer buffer;
			bool isDocumentLine = lineIndex < totalLines;
			bool drawEofMarker = editSettings.showEofMarker && lineIndex == totalLines;
			bool drawEofMarkerAsEmoji = drawEofMarker && editSettings.showEofMarkerEmoji;
			if (showLineNumbers) drawLineNumberGutter(buffer, lineIndex, isDocumentLine, viewport.lineNumberX, viewport.lineNumberWidth, zeroFillLineNumbers);
			if (drawCodeFolding) drawCodeFoldingGutter(buffer, viewport.codeFoldingX, viewport.codeFoldingWidth);
			if (drawMiniMap) drawMiniMapGutter(buffer, y, miniMapRows, viewport, totalLines, topLine, miniMapUseBraille, viewportMarkerGlyph, miniMapPalette, miniMapOverlay);
			formatSyntaxLine(buffer, linePtr, delta.x, textWidth, viewport.textLeft, isDocumentLine, drawEofMarker, drawEofMarkerAsEmoji);
			writeBuf(0, y + viewport.topInset, size.x, 1, buffer);
			if (linePtr < mBufferModel.length()) linePtr = mBufferModel.nextLine(linePtr);
			++lineIndex;
		}
		scheduleSyntaxWarmupIfNeeded();
		scheduleSaveNormalizationWarmupIfNeeded();
		updateIndicator();
	}

	virtual TPalette &getPalette() const override {
		// 1..2: scroller text/selected text (window slots 6/7)
		// 3..5: editor-only highlight slots (window-local palette slots 9..11)
		// mapped to app palette extension 136..138.
		// 6: line number gutter (window-local slot 12, app slot 142).
		static TPalette palette("\x06\x07\x09\x0A\x0B\x0C", 6);
		return palette;
	}

	virtual void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown) {
			const ushort mods = event.keyDown.controlKeyState;
			const bool shiftTabPressed = event.keyDown.keyCode == kbShiftTab || ((event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) && hasShiftModifier(mods));
			if (shiftTabPressed) {
				handleKeyDown(event);
				return;
			}
		}

		TScroller::handleEvent(event);

		if (event.what == evBroadcast) {
			if (event.message.command == cmScrollBarClicked && (event.message.infoPtr == hScrollBar || event.message.infoPtr == vScrollBar)) {
				select();
				clearEvent(event);
				return;
			}
			if (event.message.command == cmScrollBarChanged && (event.message.infoPtr == hScrollBar || event.message.infoPtr == vScrollBar)) {
				clearEvent(event);
				return;
			}
		}

		switch (event.what) {
			case evMouseDown:
				handleMouse(event);
				break;
			case evMouseWheel:
				if (vScrollBar != nullptr) vScrollBar->handleEvent(event);
				if (event.what != evNothing && hScrollBar != nullptr) hScrollBar->handleEvent(event);
				break;
			case evKeyDown:
				handleKeyDown(event);
				break;
			case evCommand:
				handleCommand(event);
				break;
			default:
				break;
		}
	}

	virtual void scrollDraw() override {
		int newDeltaX = hScrollBar != nullptr ? hScrollBar->value : 0;
		int newDeltaY = vScrollBar != nullptr ? vScrollBar->value : 0;

		if (newDeltaX != delta.x || newDeltaY != delta.y) {
			delta.x = newDeltaX;
			delta.y = newDeltaY;
			if (useApproximateLargeFileMetrics()) updateMetrics();
			scheduleSyntaxWarmupIfNeeded();
			drawView();
		} else {
			if (useApproximateLargeFileMetrics()) updateMetrics();
			scheduleSyntaxWarmupIfNeeded();
			updateIndicator();
		}
	}

	virtual void setState(ushort aState, Boolean enable) override {
		TScroller::setState(aState, enable);
		if ((aState & (sfActive | sfSelected)) != 0) syncScrollBarsToState();
		if (aState == sfCursorVis || mIndicatorUpdateInProgress) return;
		updateIndicator();
	}

	virtual Boolean valid(ushort command) override {
		if (command == cmValid || command == cmReleasedFocus) return True;
		if (mReadOnly || !mBufferModel.isModified()) return True;
		if (!canSaveInPlace()) return confirmSaveOrDiscardUntitled();
		return confirmSaveOrDiscardNamed();
	}

  private:
	static bool isWordByte(char ch) noexcept {
		unsigned char uch = static_cast<unsigned char>(ch);
		return std::isalnum(uch) != 0 || ch == '_';
	}

	static bool hasShiftModifier(ushort mods) noexcept {
		return (mods & (kbShift | kbCtrlShift | kbAltShift)) != 0;
	}

	static int configuredTabSize() noexcept {
		int tabSize = configuredTabSizeSetting();
		if (tabSize < 1) tabSize = 1;
		if (tabSize > 32) tabSize = 32;
		return tabSize;
	}

	static bool configuredDisplayTabs() noexcept {
		return configuredDisplayTabsSetting();
	}

	static bool configuredFormatRuler() noexcept {
		return configuredEditSetupSettings().formatRuler;
	}

	static int tabDisplayWidth(const MREditSetupSettings &settings, int visualColumn) noexcept {
		const int currentColumn = std::max(1, visualColumn + 1);
		const int targetColumn = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);
		return std::max(1, targetColumn - currentColumn);
	}

	std::string preferredIndentFill() const {
		const MREditSetupSettings settings = configuredEditSetupSettings();
		const int targetColumn = resolvedEditFormatIndentColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, mPreferredIndentColumn);

		return buildEditIndentFill(settings, 1, targetColumn, configuredTabExpandSetting());
	}

	int visibleTextRows() const noexcept {
		return std::max(0, size.y - (configuredFormatRuler() ? 1 : 0));
	}

	void syncScrollBarsToState() noexcept {
		bool show = (state & (sfActive | sfSelected)) != 0;
		if (hScrollBar != nullptr) {
			if (show) hScrollBar->show();
			else
				hScrollBar->hide();
		}
		if (vScrollBar != nullptr) {
			if (show) vScrollBar->show();
			else
				vScrollBar->hide();
		}
	}

	static int decimalDigits(std::size_t value) noexcept {
		int digits = 1;
		while (value >= 10) {
			value /= 10;
			++digits;
		}
		return digits;
	}

	bool shouldTraceLargeFileDiagnostics() const noexcept {
		return mBufferModel.document().hasMappedOriginal() && mBufferModel.document().length() >= static_cast<std::size_t>(8) * 1024 * 1024;
	}

	void traceLargeFileMessage(const char *stage, const std::string &detail) const {
		if (!shouldTraceLargeFileDiagnostics()) return;
		std::ostringstream line;

		line << "Large-file " << stage << ": doc=" << mBufferModel.documentId() << " ver=" << mBufferModel.version();
		if (hasPersistentFileName()) line << " file='" << fileName << "'";
		if (!detail.empty()) line << " " << detail;
		mrLogMessage(line.str());
	}

	void traceLargeFileMetrics(const char *stage, int limitY, int maxY, int textRows, int newDeltaY) {
		if (!shouldTraceLargeFileDiagnostics()) return;
		const bool exactKnown = mBufferModel.exactLineCountKnown();
		const bool nearBottom = std::max(delta.y, newDeltaY) >= std::max(0, maxY - 2);
		const bool clamped = newDeltaY != delta.y;

		if (!nearBottom && !clamped && exactKnown == mLastLargeFileMetricsExactKnown && mLargeFileMetricsTraceValid) return;
		if (mLargeFileMetricsTraceValid && exactKnown == mLastLargeFileMetricsExactKnown && limitY == mLastLargeFileMetricsLimitY && maxY == mLastLargeFileMetricsMaxY && delta.y == mLastLargeFileMetricsDeltaY && newDeltaY == mLastLargeFileMetricsNewDeltaY) return;
		std::ostringstream detail;
		detail << "stage=" << stage << " exact=" << (exactKnown ? 1 : 0) << " estimated_lines=" << mBufferModel.estimatedLineCount();
		if (exactKnown) detail << " line_count=" << mBufferModel.lineCount();
		detail << " cursor_line=" << mBufferModel.lineIndex(mBufferModel.cursor()) << " delta_y=" << delta.y << " new_delta_y=" << newDeltaY << " limit_y=" << limitY << " max_y=" << maxY << " text_rows=" << textRows;
		traceLargeFileMessage("metrics", detail.str());
		mLargeFileMetricsTraceValid = true;
		mLastLargeFileMetricsExactKnown = exactKnown;
		mLastLargeFileMetricsLimitY = limitY;
		mLastLargeFileMetricsMaxY = maxY;
		mLastLargeFileMetricsDeltaY = delta.y;
		mLastLargeFileMetricsNewDeltaY = newDeltaY;
	}

	bool configuredShowLineNumbers() const {
		const MREditSetupSettings settings = configuredEditSetupSettings();
		return normalizedLineNumbersPosition(settings) != "OFF";
	}

	int lineNumberGutterWidthFor(bool showLineNumbers) const noexcept {
		if (!showLineNumbers) return 0;
		const int textRows = std::max(1, visibleTextRows());
		std::size_t visibleEnd = static_cast<std::size_t>(std::max(delta.y, 0) + textRows);
		std::size_t lines = 0;
		if (mBufferModel.exactLineCountKnown()) lines = std::max<std::size_t>(mBufferModel.lineCount(), visibleEnd);
		else
			lines = std::max<std::size_t>(mBufferModel.estimatedLineCount(), visibleEnd);
		int digits = decimalDigits(std::max<std::size_t>(lines, 1));
		int gutter = digits;
		return std::min(gutter, std::max(0, size.x - 1));
	}

	int lineNumberGutterWidth() const {
		return lineNumberGutterWidthFor(configuredShowLineNumbers());
	}

	struct MiniMapPalette {
		TColorAttr normal = 0;
		TColorAttr viewport = 0;
		TColorAttr changed = 0;
		TColorAttr findMarker = 0;
		TColorAttr errorMarker = 0;
	};

	struct MiniMapOverlayState {
		struct LineMask {
			std::size_t lineIndex = 0;
			std::uint64_t dotColumnBits = 0;
		};
		std::vector<LineMask> findLineMasks;
		std::vector<LineMask> dirtyLineMasks;
	};

	struct MiniMapRenderCache {
		bool valid = false;
		bool braille = true;
		int rowCount = 0;
		int bodyWidth = 0;
		std::size_t documentId = 0;
		std::size_t documentVersion = 0;
		std::size_t totalLines = 1;
		std::size_t windowStartLine = 0;
		std::size_t windowLineCount = 1;
		int viewportWidth = 1;
		std::vector<unsigned char> rowPatterns;
		std::vector<std::size_t> rowLineStarts;
		std::vector<std::size_t> rowLineEnds;
	};

	struct SaveNormalizationCache {
		bool valid = false;
		std::size_t documentId = 0;
		std::size_t version = 0;
		std::size_t optionsHash = 0;
		std::size_t sourceBytes = 0;
	};

	struct TextViewportGeometry {
		int gutterWidth = 0;
		int rightInset = 0;
		int topInset = 0;
		int lineNumberX = 0;
		int lineNumberWidth = 0;
		int codeFoldingX = 0;
		int codeFoldingWidth = 0;
		int miniMapInfoX = -1;
		int miniMapBodyX = -1;
		int miniMapBodyWidth = 0;
		int miniMapTotalWidth = 0;
		int miniMapSeparatorX = -1;
		int textLeft = 0;
		int textRight = 1;
		int width = 1;
		int deltaX = 0;
		int deltaY = 0;

		bool containsTextX(long long x) const noexcept {
			return x >= textLeft && x < textRight;
		}

		bool containsTextPoint(long long x, long long y, int viewHeight) const noexcept {
			return containsTextX(x) && y >= topInset && y < topInset + viewHeight;
		}

		int textColumnFromLocalX(int localX) const noexcept {
			int clampedRight = std::max(textLeft, textRight - 1);
			int clampedX = std::max(textLeft, std::min(localX, clampedRight));
			return std::max(0, clampedX - textLeft) + deltaX;
		}

		long long localXFromVisualColumn(long long visualColumn) const noexcept {
			return visualColumn - deltaX + textLeft;
		}
	};

	struct GutterLayoutLane {
		int left = 0;
		int right = 0;

		explicit GutterLayoutLane(int width) noexcept : left(0), right(std::max(0, width)) {
		}

		bool placeLeading(int width, int &outX) noexcept {
			if (width <= 0 || width >= right - left) return false;
			outX = left;
			left += width;
			return true;
		}

		bool placeTrailing(int width, int &outX) noexcept {
			if (width <= 0 || width >= right - left) return false;
			right -= width;
			outX = right;
			return true;
		}
	};

	static bool isGutterPositionLeading(const std::string &position) noexcept {
		return position == "LEADING";
	}

	static bool isGutterPositionTrailing(const std::string &position) noexcept {
		return position == "TRAILING";
	}

	static std::string normalizedLineNumbersPosition(const MREditSetupSettings &settings) {
		if (isGutterPositionLeading(settings.lineNumbersPosition) || isGutterPositionTrailing(settings.lineNumbersPosition)) return settings.lineNumbersPosition;
		return settings.showLineNumbers ? std::string("LEADING") : std::string("OFF");
	}

	static std::string normalizedCodeFoldingPosition(const MREditSetupSettings &settings) {
		if (isGutterPositionLeading(settings.codeFoldingPosition) || isGutterPositionTrailing(settings.codeFoldingPosition)) return settings.codeFoldingPosition;
		return settings.codeFolding ? std::string("LEADING") : std::string("OFF");
	}

	static bool isMiniMapPositionLeading(const std::string &position) noexcept {
		return isGutterPositionLeading(position);
	}

	static bool isMiniMapPositionTrailing(const std::string &position) noexcept {
		return isGutterPositionTrailing(position);
	}

	static int normalizedMiniMapWidth(const MREditSetupSettings &settings) noexcept {
		if (!isMiniMapPositionLeading(settings.miniMapPosition) && !isMiniMapPositionTrailing(settings.miniMapPosition)) return 0;
		return std::max(2, std::min(settings.miniMapWidth, 20));
	}

	static std::string normalizedGuttersOrder(const std::string &configured) {
		std::string normalized;
		std::array<bool, 3> seen = {false, false, false};
		for (char ch : configured) {
			switch (static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(ch)))) {
				case 'L':
					if (!seen[0]) {
						normalized.push_back('L');
						seen[0] = true;
					}
					break;
				case 'C':
					if (!seen[1]) {
						normalized.push_back('C');
						seen[1] = true;
					}
					break;
				case 'M':
					if (!seen[2]) {
						normalized.push_back('M');
						seen[2] = true;
					}
					break;
				default:
					break;
			}
		}
		if (normalized.empty()) normalized = "LCM";
		return normalized;
	}

	TextViewportGeometry textViewportGeometryFor(const MREditSetupSettings &settings) const noexcept {
		TextViewportGeometry viewport;
		GutterLayoutLane lane(std::max(0, size.x));
		viewport.topInset = settings.formatRuler ? 1 : 0;
		const std::string lineNumbersPosition = normalizedLineNumbersPosition(settings);
		const std::string codeFoldingPosition = normalizedCodeFoldingPosition(settings);
		const bool lineNumbersLeading = lineNumbersPosition == "LEADING";
		const bool lineNumbersTrailing = lineNumbersPosition == "TRAILING";
		const bool codeFoldingLeading = codeFoldingPosition == "LEADING";
		const bool codeFoldingTrailing = codeFoldingPosition == "TRAILING";
		const int lineNumberWidth = lineNumberGutterWidthFor(lineNumbersLeading || lineNumbersTrailing);
		const int codeFoldingWidth = codeFoldingLeading || codeFoldingTrailing ? 1 : 0;
		const int miniMapTotalWidth = normalizedMiniMapWidth(settings);
		const bool leadingMiniMap = miniMapTotalWidth > 0 && isMiniMapPositionLeading(settings.miniMapPosition);
		const bool trailingMiniMap = miniMapTotalWidth > 0 && isMiniMapPositionTrailing(settings.miniMapPosition);
		const std::string guttersOrder = normalizedGuttersOrder(settings.gutters);
		const auto gutterWidthFor = [&](char marker) noexcept -> int {
			switch (marker) {
				case 'L':
					return lineNumberWidth;
				case 'C':
					return codeFoldingWidth;
				case 'M':
					return miniMapTotalWidth;
				default:
					return 0;
			}
		};
		const auto enabledOnLeading = [&](char marker) noexcept -> bool {
			switch (marker) {
				case 'L':
					return lineNumbersLeading;
				case 'C':
					return codeFoldingLeading;
				case 'M':
					return leadingMiniMap;
				default:
					return false;
			}
		};
		const auto enabledOnTrailing = [&](char marker) noexcept -> bool {
			switch (marker) {
				case 'L':
					return lineNumbersTrailing;
				case 'C':
					return codeFoldingTrailing;
				case 'M':
					return trailingMiniMap;
				default:
					return false;
			}
		};
		const auto assignGutterPlacement = [&](char marker, int x, bool leadingSide) {
			switch (marker) {
				case 'L':
					viewport.lineNumberX = x;
					viewport.lineNumberWidth = lineNumberWidth;
					break;
				case 'C':
					viewport.codeFoldingX = x;
					viewport.codeFoldingWidth = codeFoldingWidth;
					break;
				case 'M':
					viewport.miniMapTotalWidth = miniMapTotalWidth;
					viewport.miniMapBodyWidth = std::max(1, miniMapTotalWidth - 1);
					if (leadingSide) {
						viewport.miniMapBodyX = x;
						viewport.miniMapInfoX = x + viewport.miniMapBodyWidth;
					} else {
						viewport.miniMapInfoX = x;
						viewport.miniMapBodyX = x + 1;
					}
					break;
				default:
					break;
			}
		};
		auto leadingSequence = std::vector<char>();
		auto trailingSequence = std::vector<char>();
		leadingSequence.reserve(3);
		trailingSequence.reserve(3);
		for (char marker : guttersOrder) {
			if (enabledOnLeading(marker)) leadingSequence.push_back(marker);
			if (enabledOnTrailing(marker)) trailingSequence.push_back(marker);
		}
		for (char marker : leadingSequence) {
			int width = gutterWidthFor(marker);
			int x = -1;
			if (width > 0 && lane.placeLeading(width, x)) assignGutterPlacement(marker, x, true);
		}
		for (auto it = trailingSequence.rbegin(); it != trailingSequence.rend(); ++it) {
			int width = gutterWidthFor(*it);
			int x = -1;
			if (width > 0 && lane.placeTrailing(width, x)) assignGutterPlacement(*it, x, false);
		}
		if (!leadingSequence.empty() && leadingSequence.back() == 'M') {
			int separatorX = -1;
			if (lane.placeLeading(1, separatorX)) viewport.miniMapSeparatorX = separatorX;
		}
		if (!trailingSequence.empty() && trailingSequence.front() == 'M') {
			int separatorX = -1;
			if (lane.placeTrailing(1, separatorX)) viewport.miniMapSeparatorX = separatorX;
		}

		if (lane.right <= lane.left) {
			lane.left = std::max(0, std::min(lane.left, std::max(0, size.x - 1)));
			lane.right = std::max(lane.left + 1, size.x);
		}

		viewport.textLeft = lane.left;
		viewport.textRight = std::max(viewport.textLeft + 1, std::min(lane.right, size.x));
		viewport.width = std::max(1, viewport.textRight - viewport.textLeft);
		viewport.gutterWidth = viewport.textLeft;
		viewport.rightInset = std::max(0, size.x - viewport.textRight);
		viewport.deltaX = delta.x;
		viewport.deltaY = delta.y;
		return viewport;
	}

	TextViewportGeometry textViewportGeometry() const noexcept {
		return textViewportGeometryFor(configuredEditSetupSettings());
	}

	bool shouldShowEditorCursor(long long x, long long y, const TextViewportGeometry &viewport) const noexcept {
		const bool viewActive = (state & sfActive) != 0;
		const bool viewSelected = (state & sfSelected) != 0;
		return viewActive && viewSelected && viewport.containsTextPoint(x, y, visibleTextRows());
	}

	bool shouldShowEditorCursor(long long x, long long y) const noexcept {
		TextViewportGeometry viewport = textViewportGeometry();
		return shouldShowEditorCursor(x, y, viewport);
	}

	int textColumnFromLocalX(int localX) const noexcept {
		return textViewportGeometry().textColumnFromLocalX(localX);
	}

	int textViewportWidth() const {
		return textViewportGeometry().width;
	}

	std::string normalizedFormatRulerLine(const MREditSetupSettings &settings, int *leftMarginOut = nullptr, int *rightMarginOut = nullptr) const {
		std::string normalized;
		int leftMargin = settings.leftMargin;
		int rightMargin = settings.rightMargin;

		if (!normalizeEditFormatLine(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, normalized, &leftMargin, &rightMargin, nullptr)) normalized = defaultEditFormatLineForTabSize(settings.tabSize, settings.leftMargin, settings.rightMargin);
		if (leftMarginOut != nullptr) *leftMarginOut = leftMargin;
		if (rightMarginOut != nullptr) *rightMarginOut = rightMargin;
		return normalized;
	}

	bool persistVisibleEditSetupSettings(const MREditSetupSettings &settings, const std::string &errorPrefix) {
		MREditSetupSettings previousSettings = configuredEditSetupSettings();
		MRSettingsWriteReport writeReport;
		std::string errorText;

		if (!setConfiguredEditSetupSettings(settings, &errorText)) {
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			return false;
		}
		if (!persistConfiguredSettingsSnapshot(&errorText, &writeReport)) {
			static_cast<void>(setConfiguredEditSetupSettings(previousSettings, nullptr));
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			return false;
		}
		return true;
	}

	bool previewVisibleEditSetupSettings(const MREditSetupSettings &settings, const std::string &errorPrefix) {
		std::string errorText;

		if (!setConfiguredEditSetupSettings(settings, &errorText)) {
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			return false;
		}
		refreshConfiguredVisualSettings();
		return true;
	}

	bool finalizeVisibleEditSetupPreview(const MREditSetupSettings &previousSettings, const std::string &errorPrefix) {
		MRSettingsWriteReport writeReport;
		std::string errorText;

		if (persistConfiguredSettingsSnapshot(&errorText, &writeReport)) return true;
		static_cast<void>(setConfiguredEditSetupSettings(previousSettings, nullptr));
		refreshConfiguredVisualSettings();
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return false;
	}

	void drawFormatRulerOverlay(const TextViewportGeometry &viewport, const MREditSetupSettings &settings) {
		TDrawBuffer buffer;
		unsigned char configured = 0;
		TColorAttr normal = static_cast<TColorAttr>(getColor(0x0606));
		const TColorAttr accent = static_cast<TColorAttr>(getColor(0x0404));
		const std::string normalized = normalizedFormatRulerLine(settings);

		if (configuredColorSlotOverride(kMrPaletteFormatRuler, configured)) normal = static_cast<TColorAttr>(configured);
		buffer.moveChar(0, ' ', normal, size.x);
		for (int x = 0; x < viewport.width; ++x) {
			const int column = viewport.deltaX + x + 1;
			const char ch = column >= 1 && column <= static_cast<int>(normalized.size()) ? normalized[static_cast<std::size_t>(column - 1)] : ' ';
			const bool atCursor = static_cast<int>(mBufferModel.lineIndex(mBufferModel.cursor())) == delta.y && displayedCursorColumn() == viewport.deltaX + x;
			buffer.moveChar(static_cast<ushort>(viewport.textLeft + x), ch, atCursor ? accent : normal, 1);
		}
		writeBuf(0, 0, size.x, 1, buffer);
	}

	bool editFormatRulerAtLocalPoint(TPoint local, ushort modifiers) {
		MREditSetupSettings settings = configuredEditSetupSettings();
		const TextViewportGeometry viewport = textViewportGeometryFor(settings);
		if (!settings.formatRuler || local.y != 0 || !viewport.containsTextX(local.x)) return false;
		const int column = viewport.textColumnFromLocalX(local.x) + 1;
		const std::string normalized = normalizedFormatRulerLine(settings);
		const char current = column >= 1 && column <= static_cast<int>(normalized.size()) ? normalized[static_cast<std::size_t>(column - 1)] : '.';
		char symbol = current == '|' ? '.' : '|';
		std::string updated;
		int leftMargin = settings.leftMargin;
		int rightMargin = settings.rightMargin;
		if ((modifiers & kbShift) != 0) symbol = 'L';
		else if ((modifiers & kbCtrlShift) != 0)
			symbol = 'R';
		else if ((modifiers & kbAltShift) != 0)
			symbol = '.';
		else if (column <= leftMargin)
			symbol = 'L';
		else if (column >= rightMargin)
			symbol = 'R';
		if (!editFormatLineAtColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, column, symbol, updated, &leftMargin, &rightMargin, nullptr)) return true;
		settings.formatLine = updated;
		settings.leftMargin = leftMargin;
		settings.rightMargin = rightMargin;
		if (!persistVisibleEditSetupSettings(settings, "Format ruler update failed: ")) return true;
		refreshConfiguredVisualSettings();
		return true;
	}

	bool dragFormatRulerAtLocalPoint(TEvent &event, TPoint local) {
		const MREditSetupSettings initialSettings = configuredEditSetupSettings();
		const TextViewportGeometry viewport = textViewportGeometryFor(initialSettings);
		const ushort modifiers = event.mouse.controlKeyState;
		const int startColumn = viewport.textColumnFromLocalX(local.x) + 1;
		bool dragged = false;

		if (!initialSettings.formatRuler || local.y != 0 || !viewport.containsTextX(local.x)) return false;
		while (mouseEvent(event, evMouseMove | evMouseAuto | evMouseUp)) {
			TPoint currentLocal = makeLocal(event.mouse.where);
			MREditSetupSettings preview = initialSettings;
			std::string translated;
			int leftMargin = initialSettings.leftMargin;
			int rightMargin = initialSettings.rightMargin;
			const int currentColumn = viewport.textColumnFromLocalX(currentLocal.x) + 1;
			const int delta = currentColumn - startColumn;

			if (event.what == evMouseUp) break;
			if (delta == 0) continue;
			dragged = true;
			if (!translateEditFormatLine(initialSettings.formatLine, initialSettings.tabSize, initialSettings.leftMargin, initialSettings.rightMargin, delta, translated, &leftMargin, &rightMargin, nullptr)) continue;
			preview.formatLine = translated;
			preview.leftMargin = leftMargin;
			preview.rightMargin = rightMargin;
			if (!previewVisibleEditSetupSettings(preview, "Format ruler drag failed: ")) return true;
		}
		if (!dragged) return editFormatRulerAtLocalPoint(local, modifiers);
		static_cast<void>(finalizeVisibleEditSetupPreview(initialSettings, "Format ruler drag failed: "));
		return true;
	}

	void drawLineNumberGutter(TDrawBuffer &b, std::size_t lineIndex, bool showNumber, int drawX, int width, bool zeroFill) {
		TColorAttr color = static_cast<TColorAttr>(getColor(0x0606));
		char numberBuffer[32];
		int digits = std::max(1, width);

		if (width <= 0) return;
		b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
		if (!showNumber) return;
		if (zeroFill) std::snprintf(numberBuffer, sizeof(numberBuffer), "%0*lu", digits, static_cast<unsigned long>(lineIndex + 1));
		else
			std::snprintf(numberBuffer, sizeof(numberBuffer), "%*lu", digits, static_cast<unsigned long>(lineIndex + 1));
		b.moveStr(static_cast<ushort>(drawX), numberBuffer, color, static_cast<ushort>(width));
	}

	void drawCodeFoldingGutter(TDrawBuffer &b, int drawX, int width) {
		unsigned char configured = 0;
		TColorAttr color = static_cast<TColorAttr>(getColor(0x0606));

		if (width <= 0) return;
		if (configuredColorSlotOverride(kMrPaletteCodeFolding, configured)) color = static_cast<TColorAttr>(configured);
		b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
	}

	static bool useBrailleMiniMapRenderer() noexcept;

	static std::string normalizedMiniMapViewportMarkerGlyph(const std::string &configuredGlyph);

	static const char *miniMapWarmupTaskLabel() noexcept;

	static const char *lineIndexWarmupTaskLabel() noexcept {
		return "line-index-warmup";
	}

	static const char *syntaxWarmupTaskLabel() noexcept {
		return "syntax-warmup";
	}

	static const char *saveNormalizationWarmupTaskLabel() noexcept {
		return "save-normalization";
	}

	static std::string utf8FromCodepoint(std::uint32_t codepoint);

	static const std::array<std::string, 256> &brailleGlyphTable();

	static std::size_t scaledMidpoint(std::size_t sampleIndex, std::size_t sampleCount, std::size_t targetCount) noexcept;

	bool lineIntersectsDirtyRanges(std::size_t lineStart, std::size_t lineEnd) const noexcept {
		if (lineEnd <= lineStart || mDirtyRanges.empty()) return false;
		for (const MRTextBufferModel::Range &range : mDirtyRanges) {
			if (range.end <= lineStart) continue;
			if (range.start >= lineEnd) break;
			return true;
		}
		return false;
	}

	MiniMapPalette resolveMiniMapPalette();

	static std::pair<std::size_t, std::size_t> scaledInterval(std::size_t index, std::size_t count, std::size_t targetCount) noexcept;

	static void normalizeMiniMapLineMasks(std::vector<MiniMapOverlayState::LineMask> &masks);

	static std::uint64_t miniMapLineMaskBits(const std::vector<MiniMapOverlayState::LineMask> &masks, std::size_t lineIndex) noexcept;

	static bool miniMapCellHasOverlayBits(std::uint64_t lineBits, int x, bool useBraille) noexcept;

	struct MiniMapSamplingWindow {
		std::size_t startLine = 0;
		std::size_t lineCount = 1;
	};

	static MiniMapSamplingWindow miniMapSamplingWindowFor(std::size_t totalLines, std::size_t topLine, int rowCount, bool useBraille) noexcept;

	static bool ratioCellActive(int numerator, int denominator, int cellIndex, int cellCount) noexcept {
		if (numerator <= 0 || denominator <= 0 || cellCount <= 0) return false;
		if (numerator >= denominator) return true;
		long long lhs = static_cast<long long>(numerator) * static_cast<long long>(cellCount);
		long long rhs = static_cast<long long>(cellIndex + 1) * static_cast<long long>(denominator);
		return lhs >= rhs;
	}

	// Returns true if minimap cell [cellIndex] overlaps the content column range [from, to)
	// when the viewport has viewportWidth columns and the minimap has cellCount cells.
	static bool ratioCellInRange(int from, int to, int viewportWidth, int cellIndex, int cellCount) noexcept;

	bool miniMapCacheReadyForViewport(const TextViewportGeometry &viewport, bool braille, const MiniMapSamplingWindow &window) const noexcept;

	int miniMapViewportRows() const noexcept;

	void clearMiniMapWarmupTaskInternal(std::uint64_t expectedTaskId) noexcept;

	void invalidateMiniMapCache(bool cancelTask);

	bool applyMiniMapWarmupInternal(const mr::coprocessor::MiniMapWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId);

	void scheduleMiniMapWarmupIfNeeded(const TextViewportGeometry &viewport, bool useBraille, std::size_t totalLinesHint, std::size_t topLine);

	MiniMapOverlayState computeMiniMapOverlayState(const MRTextBufferModel::Range &selection, std::size_t totalLines, int viewportWidth, int miniMapBodyWidth, bool useBraille, const MREditSetupSettings &settings) const;

	void drawMiniMapGutter(TDrawBuffer &b, int y, int miniMapRows, const TextViewportGeometry &viewport, std::size_t totalLines, std::size_t topLine, bool useBraille, const std::string &viewportMarkerGlyph, const MiniMapPalette &palette, const MiniMapOverlayState &overlay);

	static bool nextDisplayChar(TStringView text, std::size_t &index, std::size_t &width, int visualColumn, const MREditSetupSettings &settings) noexcept {
		if (index >= text.size()) return false;
		if (text[index] == '\t') {
			++index;
			width = static_cast<std::size_t>(tabDisplayWidth(settings, visualColumn));
			return true;
		}
		return TText::next(text, index, width);
	}

	static int displayWidthForText(TStringView text, const MREditSetupSettings &settings) noexcept {
		std::size_t index = 0;
		int visual = 0;

		while (index < text.size()) {
			std::size_t next = index;
			std::size_t width = 0;
			if (!nextDisplayChar(text, next, width, visual, settings)) break;
			visual += static_cast<int>(width);
			index = next;
		}
		return visual;
	}

	static void writeChunk(std::ofstream &out, const char *data, std::size_t length) {
		while (length > 0) {
			std::size_t part = std::min<std::size_t>(length, static_cast<std::size_t>(INT_MAX));
			out.write(data, static_cast<std::streamsize>(part));
			data += part;
			length -= part;
		}
	}

	bool writeDocumentToPath(const char *targetPath) {
		char drive[MAXDRIVE];
		char dir[MAXDIR];
		char file[MAXFILE];
		char ext[MAXEXT];
		MRTextSaveOptions saveOptions;
		const std::size_t pieceCount = mBufferModel.document().pieceCount();

		resolveSaveOptionsForPath(targetPath, saveOptions);

		if (configuredBackupFilesSetting()) {
			fnsplit(targetPath, drive, dir, file, ext);
			char backupName[MAXPATH];
			fnmerge(backupName, drive, dir, file, ".bak");
			unlink(backupName);
			rename(targetPath, backupName);
		}

		std::ofstream out(targetPath, std::ios::out | std::ios::binary | std::ios::trunc);
		if (!out) {
			TEditor::editorDialog(edCreateError, targetPath);
			return false;
		}
		auto failWrite = [&]() -> bool {
			TEditor::editorDialog(edWriteError, targetPath);
			return false;
		};

		if (saveOptions.binaryMode) {
			for (std::size_t i = 0; i < pieceCount; ++i) {
				mr::editor::PieceChunkView chunk = mBufferModel.document().pieceChunk(i);
				writeChunk(out, chunk.data, chunk.length);
				if (!out) return failWrite();
			}
			return true;
		}
		const std::size_t sourceBytes = mBufferModel.document().length();
		const auto normalizeStartedAt = std::chrono::steady_clock::now();
		const std::size_t flushThresholdBytes = static_cast<std::size_t>(256) * 1024;
		MRTextSaveStreamState normalizeState;
		std::string outputBuffer;
		auto flushOutput = [&]() -> bool {
			if (outputBuffer.empty()) return true;
			writeChunk(out, outputBuffer.data(), outputBuffer.size());
			outputBuffer.clear();
			return static_cast<bool>(out);
		};

		outputBuffer.reserve(flushThresholdBytes + 1024);
		for (std::size_t i = 0; i < pieceCount; ++i) {
			mr::editor::PieceChunkView chunk = mBufferModel.document().pieceChunk(i);
			if (chunk.length == 0) continue;
			appendNormalizedTextSaveChunk(std::string_view(chunk.data, chunk.length), saveOptions, normalizeState, outputBuffer);
			if (outputBuffer.size() >= flushThresholdBytes && !flushOutput()) return failWrite();
		}
		finalizeNormalizedTextSaveStream(saveOptions, normalizeState, outputBuffer);
		if (!flushOutput()) return failWrite();

		noteSaveNormalizationThroughput(sourceBytes, static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - normalizeStartedAt).count()));
		if (!out) return failWrite();
		return true;
	}

	static bool pathIsRegularFile(const char *path) noexcept {
		struct stat st;
		if (path == nullptr || *path == '\0') return false;
		return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
	}

	static bool samePath(const char *lhs, const char *rhs) noexcept {
		struct stat lhsStat;
		struct stat rhsStat;
		char lhsExpanded[MAXPATH];
		char rhsExpanded[MAXPATH];
		std::size_t i = 0;

		if (lhs == nullptr || rhs == nullptr) return false;
		if (::stat(lhs, &lhsStat) == 0 && ::stat(rhs, &rhsStat) == 0) return lhsStat.st_dev == rhsStat.st_dev && lhsStat.st_ino == rhsStat.st_ino;

		strnzcpy(lhsExpanded, lhs, sizeof(lhsExpanded));
		strnzcpy(rhsExpanded, rhs, sizeof(rhsExpanded));
		fexpand(lhsExpanded);
		fexpand(rhsExpanded);
		for (i = 0; lhsExpanded[i] != EOS; ++i)
			if (lhsExpanded[i] == '\\') lhsExpanded[i] = '/';
		for (i = 0; rhsExpanded[i] != EOS; ++i)
			if (rhsExpanded[i] == '\\') rhsExpanded[i] = '/';
		return std::strcmp(lhsExpanded, rhsExpanded) == 0;
	}

	bool confirmOverwriteForSaveAs(const char *targetPath) const {
		if (!pathIsRegularFile(targetPath)) return true;
		return mr::dialogs::showUnsavedChangesDialog("Overwrite", "Target file exists. Overwrite?", targetPath) == mr::dialogs::UnsavedChangesChoice::Save;
	}

	std::size_t lineStartForIndex(std::size_t index) const noexcept {
		return mBufferModel.lineStartByIndex(index);
	}

	int longestLineWidth() const noexcept {
		std::size_t pos = 0;
		std::size_t len = mBufferModel.length();
		const MREditSetupSettings settings = configuredEditSetupSettings();
		int maxWidth = 1;

		while (true) {
			int width = displayWidthForText(mBufferModel.lineText(pos), settings);
			maxWidth = std::max(maxWidth, width + 1);
			if (pos >= len) break;
			std::size_t next = mBufferModel.nextLine(pos);
			if (next <= pos) break;
			pos = next;
		}
		return maxWidth;
	}

	bool useApproximateLargeFileMetrics() const noexcept {
		const MRTextBufferModel::Document &document = mBufferModel.document();
		return document.hasMappedOriginal() && document.length() >= static_cast<std::size_t>(8) * 1024 * 1024;
	}

	int dynamicLargeFileLineLimit() const noexcept {
		const std::size_t estimated = mBufferModel.estimatedLineCount();
		const std::size_t currentLine = mBufferModel.lineIndex(mBufferModel.cursor());
		const int textRows = std::max(1, visibleTextRows());
		const std::size_t minimum = static_cast<std::size_t>(textRows);
		const std::size_t margin = static_cast<std::size_t>(std::max(textRows * 4, 256));
		std::size_t limitValue = std::max<std::size_t>(estimated, currentLine + margin);
		limitValue = std::max<std::size_t>(limitValue, minimum);
		return static_cast<int>(std::min<std::size_t>(limitValue, static_cast<std::size_t>(INT_MAX)));
	}

	int dynamicLargeFileWidthLimit() const {
		const int viewportWidth = textViewportWidth();
		return std::max(std::max(viewportWidth, 256), std::max(delta.x + viewportWidth + 64, displayedCursorColumn() + 64));
	}

	void scheduleLineIndexWarmupIfNeeded() {
		if (!mBufferModel.document().hasMappedOriginal() || mBufferModel.document().exactLineCountKnown()) {
			std::uint64_t cancelledTaskId = mLineIndexWarmupTaskId;
			bool hadTask = cancelledTaskId != 0;
			mLineIndexWarmupTaskId = 0;
			mLineIndexWarmupDocumentId = 0;
			mLineIndexWarmupVersion = 0;
			if (hadTask) {
				if (shouldTraceLargeFileDiagnostics()) traceLargeFileMessage("line-index-cancel", "reason=exact-line-count-known");
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
				notifyWindowTaskStateChanged();
			}
			return;
		}

		const std::size_t docId = mBufferModel.documentId();
		const std::size_t version = mBufferModel.version();
		if (mLineIndexWarmupTaskId != 0 && mLineIndexWarmupDocumentId == docId && mLineIndexWarmupVersion == version) return;

		MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
		std::uint64_t previousTaskId = mLineIndexWarmupTaskId;
		if (previousTaskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
		mLineIndexWarmupDocumentId = docId;
		mLineIndexWarmupVersion = version;
		mLineIndexWarmupTaskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::LineIndexWarmup, docId, version, lineIndexWarmupTaskLabel(), [snapshot](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
			mr::coprocessor::Result result;
			mr::editor::LineIndexWarmupData warmup;
			result.task = info;
			if (stopToken.stop_requested() || info.cancelRequested()) {
				result.status = mr::coprocessor::TaskStatus::Cancelled;
				return result;
			}
			if (!snapshot.completeLineIndexWarmup(warmup, stopToken, info.cancelFlag.get())) {
				result.status = mr::coprocessor::TaskStatus::Cancelled;
				return result;
			}
			result.status = mr::coprocessor::TaskStatus::Completed;
			result.payload = std::make_shared<mr::coprocessor::LineIndexWarmupPayload>(warmup);
			return result;
		});
		if (shouldTraceLargeFileDiagnostics()) {
			std::ostringstream detail;
			detail << "task=" << mLineIndexWarmupTaskId << " estimated_lines=" << mBufferModel.estimatedLineCount() << " cursor_line=" << mBufferModel.lineIndex(mBufferModel.cursor()) << " delta_y=" << delta.y;
			traceLargeFileMessage("line-index-schedule", detail.str());
		}
		if (mLineIndexWarmupTaskId != previousTaskId) notifyWindowTaskStateChanged();
	}

	void scheduleSyntaxWarmupIfNeeded() {
		const int textRows = visibleTextRows();
		if (mBufferModel.language() == MRSyntaxLanguage::PlainText || textRows <= 0) {
			resetSyntaxWarmupState(true);
			return;
		}

		const std::size_t docId = mBufferModel.documentId();
		const std::size_t version = mBufferModel.version();
		const MRSyntaxLanguage language = mBufferModel.language();
		const std::size_t topLine = static_cast<std::size_t>(std::max(delta.y - 4, 0));
		const int rowBudget = std::max(textRows + 8, 8);
		std::vector<std::size_t> lineStarts = syntaxWarmupLineStarts(topLine, rowBudget);
		if (lineStarts.empty()) return;

		const std::size_t bottomLine = topLine + lineStarts.size();
		if (hasSyntaxTokensForLineStarts(lineStarts)) {
			std::uint64_t previousTaskId = mSyntaxWarmupTaskId;
			bool hadTask = previousTaskId != 0;
			mSyntaxWarmupTaskId = 0;
			mSyntaxWarmupDocumentId = docId;
			mSyntaxWarmupVersion = version;
			mSyntaxWarmupTopLine = topLine;
			mSyntaxWarmupBottomLine = bottomLine;
			mSyntaxWarmupLanguage = language;
			if (hadTask) {
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
				notifyWindowTaskStateChanged();
			}
			return;
		}

		if (mSyntaxWarmupTaskId != 0 && mSyntaxWarmupDocumentId == docId && mSyntaxWarmupVersion == version && mSyntaxWarmupLanguage == language && topLine >= mSyntaxWarmupTopLine && bottomLine <= mSyntaxWarmupBottomLine) return;

		MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
		std::uint64_t previousTaskId = mSyntaxWarmupTaskId;
		if (previousTaskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
		mSyntaxWarmupDocumentId = docId;
		mSyntaxWarmupVersion = version;
		mSyntaxWarmupTopLine = topLine;
		mSyntaxWarmupBottomLine = bottomLine;
		mSyntaxWarmupLanguage = language;
		mSyntaxWarmupTaskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::SyntaxWarmup, docId, version, syntaxWarmupTaskLabel(), [snapshot, language, lineStarts](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
			mr::coprocessor::Result result;
			std::vector<mr::coprocessor::SyntaxWarmLine> warmed;
			auto shouldStop = [&]() noexcept { return stopToken.stop_requested() || info.cancelRequested(); };
			result.task = info;
			if (shouldStop()) {
				result.status = mr::coprocessor::TaskStatus::Cancelled;
				return result;
			}
			warmed.reserve(lineStarts.size());
			for (std::size_t i = 0; i < lineStarts.size(); ++i) {
				if (shouldStop()) {
					result.status = mr::coprocessor::TaskStatus::Cancelled;
					return result;
				}
				warmed.push_back(mr::coprocessor::SyntaxWarmLine(lineStarts[i], tmrBuildTokenMapForTextLine(language, snapshot.lineText(lineStarts[i]))));
			}
			result.status = mr::coprocessor::TaskStatus::Completed;
			result.payload = std::make_shared<mr::coprocessor::SyntaxWarmupPayload>(language, std::move(warmed));
			return result;
		});
		if (mSyntaxWarmupTaskId != previousTaskId) notifyWindowTaskStateChanged();
	}

	bool resolveSaveOptionsForPath(const char *path, MRTextSaveOptions &options, std::size_t *optionsHash = nullptr) const {
		options = effectiveTextSaveOptionsForPath(path != nullptr ? path : "", optionsHash);
		return true;
	}

	void invalidateSaveNormalizationCache() noexcept {
		mSaveNormalizationCache.valid = false;
		mSaveNormalizationCache.documentId = 0;
		mSaveNormalizationCache.version = 0;
		mSaveNormalizationCache.optionsHash = 0;
		mSaveNormalizationCache.sourceBytes = 0;
	}

	void noteSaveNormalizationThroughput(std::size_t sourceBytes, double runMicros) noexcept {
		if (sourceBytes == 0 || runMicros <= 0.0) return;
		const double sampleBytesPerMicro = static_cast<double>(sourceBytes) / std::max(1.0, runMicros);
		if (mSaveNormalizationThroughputBytesPerMicro <= 0.0) mSaveNormalizationThroughputBytesPerMicro = sampleBytesPerMicro;
		else
			mSaveNormalizationThroughputBytesPerMicro = mSaveNormalizationThroughputBytesPerMicro * 0.75 + sampleBytesPerMicro * 0.25;
		++mSaveNormalizationThroughputSamples;
	}

	void scheduleSaveNormalizationWarmupIfNeeded() {
		invalidateSaveNormalizationCache();
		if (mSaveNormalizationWarmupTaskId == 0) return;
		std::uint64_t cancelledTaskId = mSaveNormalizationWarmupTaskId;
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
		clearSaveNormalizationWarmupTask(cancelledTaskId);
	}

	void updateMetrics() {
		int limitX = 1;
		int limitY = 1;
		TextViewportGeometry viewport = textViewportGeometry();
		int gutterWidth = viewport.gutterWidth;
		int rightInset = viewport.rightInset;
		int viewportWidth = viewport.width;
		const int textRows = std::max(1, visibleTextRows());
		const bool showEofMarker = configuredEditSetupSettings().showEofMarker;

		if (useApproximateLargeFileMetrics()) {
			limitX = dynamicLargeFileWidthLimit();
			limitY = dynamicLargeFileLineLimit();
		} else {
			limitX = longestLineWidth();
			limitY = std::max<int>(1, static_cast<int>(mBufferModel.lineCount()));
		}
		limitX = std::max(limitX, displayedCursorColumn() + 1);
		if (showEofMarker && limitY < INT_MAX) ++limitY;

		int maxX = std::max(0, limitX - viewportWidth);
		int maxY = std::max(0, limitY - textRows);
		int newDeltaX = std::min(std::max(delta.x, 0), maxX);
		int newDeltaY = std::min(std::max(delta.y, 0), maxY);
		traceLargeFileMetrics("updateMetrics", limitY, maxY, textRows, newDeltaY);

		setLimit(limitX + gutterWidth + rightInset, limitY + viewport.topInset);
		if (newDeltaX != delta.x || newDeltaY != delta.y) scrollTo(newDeltaX, newDeltaY);
	}

	void updateIndicator() {
		if (mIndicatorUpdateInProgress) return;
		mIndicatorUpdateInProgress = true;
		TextViewportGeometry viewport = textViewportGeometry();
		unsigned long visualColumn = static_cast<unsigned long>(displayedCursorColumn());
		unsigned long line = static_cast<unsigned long>(mBufferModel.lineIndex(mBufferModel.cursor()));
		long long localX = viewport.localXFromVisualColumn(static_cast<long long>(visualColumn));
		long long localY = static_cast<long long>(line) - delta.y + viewport.topInset;

		if (mIndicator != nullptr) {
			if (auto *mrIndicator = dynamic_cast<MRIndicator *>(mIndicator)) mrIndicator->setDisplayValue(visualColumn, line, mBufferModel.isModified() ? True : False);
			else {
				TPoint location = {short(visualColumn > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : visualColumn), short(line > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : line)};
				mIndicator->setValue(location, mBufferModel.isModified() ? True : False);
			}
		}

		if (shouldShowEditorCursor(localX, localY, viewport)) {
			setCursor(static_cast<int>(localX), static_cast<int>(localY));
			showCursor();
		} else
			hideCursor();
		mIndicatorUpdateInProgress = false;
	}

	void ensureCursorVisible(bool centerCursor) {
		int visualColumn = displayedCursorColumn();
		int line = static_cast<int>(mBufferModel.lineIndex(mBufferModel.cursor()));
		int targetX = delta.x;
		int targetY = delta.y;
		int viewportWidth = textViewportWidth();
		int textRows = std::max(1, visibleTextRows());

		if (visualColumn < targetX) targetX = visualColumn;
		else if (visualColumn >= targetX + viewportWidth)
			targetX = visualColumn - viewportWidth + 1;

		if (centerCursor) targetY = std::max(0, line - textRows / 2);
		else if (line < targetY)
			targetY = line;
		else if (line >= targetY + textRows)
			targetY = line - textRows + 1;

		scrollTo(targetX, targetY);
	}

	void moveCursor(std::size_t target, bool extendSelection, bool centerCursor, int requestedVisualColumn = -1) {
		target = canonicalCursorOffset(std::min(target, mBufferModel.length()));
		if (extendSelection) {
			std::size_t anchor = mBufferModel.hasSelection() ? mBufferModel.selection().anchor : mBufferModel.cursor();
			mSelectionAnchor = anchor;
			mBufferModel.setCursorAndSelection(target, anchor, target);
		} else {
			if (configuredPersistentBlocksSetting() && mBufferModel.hasSelection()) mBufferModel.setCursor(target);
			else
				mBufferModel.setCursorAndSelection(target, target, target);
			mSelectionAnchor = target;
		}
		if (freeCursorMovementEnabled() && requestedVisualColumn >= 0) mCursorVisualColumn = std::max(actualCursorVisualColumn(target), requestedVisualColumn);
		else
			mCursorVisualColumn = actualCursorVisualColumn(target);
		if (useApproximateLargeFileMetrics()) updateMetrics();
		ensureCursorVisible(centerCursor);
		scheduleSyntaxWarmupIfNeeded();
		updateIndicator();
		drawView();
	}

	bool isTextInputEvent(const TEvent &event) const {
		if (event.what != evKeyDown) return false;
		const ushort mods = event.keyDown.controlKeyState;
		const bool plainTab = (event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) && (mods & (kbShift | kbCtrlShift | kbAltShift | kbPaste)) == 0;
		return (event.keyDown.controlKeyState & kbPaste) != 0 || event.keyDown.textLength > 0 || plainTab || (event.keyDown.charScan.charCode >= 32 && event.keyDown.charScan.charCode < 255);
	}

	void handleTextInput(TEvent &event) {
		if (mReadOnly) {
			clearEvent(event);
			return;
		}

		if ((event.keyDown.controlKeyState & kbPaste) != 0) {
			char buf[512];
			size_t length = 0;
			while (textEvent(event, TSpan<char>(buf, sizeof(buf)), length))
				insertBufferText(std::string(buf, length));
			applyLiveWordWrapAfterTextInput();
			clearEvent(event);
			return;
		}

		const ushort mods = event.keyDown.controlKeyState;
		const bool plainTab = (event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) && (mods & (kbShift | kbCtrlShift | kbAltShift | kbPaste)) == 0;

		if (event.keyDown.textLength > 0) insertBufferText(std::string(event.keyDown.text, event.keyDown.textLength));
		else if (plainTab)
			insertBufferText(tabKeyText());
		else
			insertBufferText(std::string(1, static_cast<char>(event.keyDown.charScan.charCode)));
		applyLiveWordWrapAfterTextInput();
		clearEvent(event);
	}

	std::string tabKeyText() const {
		if (configuredTabExpandSetting()) return "\t";
		std::size_t insertPos = mBufferModel.cursor();
		const MREditSetupSettings settings = configuredEditSetupSettings();
		if (mBufferModel.hasSelection()) insertPos = mBufferModel.selection().range().start;
		int visualColumn = freeCursorMovementEnabled() && insertPos == mBufferModel.cursor() && !mBufferModel.hasSelection() ? displayedCursorColumn() : charColumn(mBufferModel.lineStart(insertPos), insertPos);
		return std::string(static_cast<std::size_t>(tabDisplayWidth(settings, visualColumn)), ' ');
	}

	void handleKeyDown(TEvent &event) {
		ushort key = ctrlToArrow(event.keyDown.keyCode);
		const ushort mods = event.keyDown.controlKeyState;
		bool extend = hasShiftModifier(mods);
		const bool shiftTabPressed = event.keyDown.keyCode == kbShiftTab || ((event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) && hasShiftModifier(mods));

		if (shiftTabPressed) {
			const std::size_t target = tabStopMoveOffset(cursorOffset(), false);
			if (target != cursorOffset()) setPreferredIndentColumn(charColumn(lineStartOffset(target), target) + 1);
			moveCursor(target, false, false);
			clearEvent(event);
			return;
		}

		if (isTextInputEvent(event)) {
			handleTextInput(event);
			return;
		}

		switch (key) {
			case kbLeft:
				moveCursor(prevCharOffset(cursorOffset()), extend, false);
				break;
			case kbRight:
				moveCursor(nextCharOffset(cursorOffset()), extend, false);
				break;
			case kbUp:
				moveCursor(lineMoveOffset(cursorOffset(), -1, displayedCursorColumn()), extend, false, displayedCursorColumn());
				break;
			case kbDown:
				moveCursor(lineMoveOffset(cursorOffset(), 1, displayedCursorColumn()), extend, false, displayedCursorColumn());
				break;
			case kbHome:
				moveCursor(mAutoIndent ? charPtrOffset(lineStartOffset(cursorOffset()), 0) : lineStartOffset(cursorOffset()), extend, false);
				break;
			case kbEnd:
				moveCursor(lineEndOffset(cursorOffset()), extend, false);
				break;
			case kbPgUp:
				moveCursor(lineMoveOffset(cursorOffset(), -(std::max(2, visibleTextRows()) - 1), displayedCursorColumn()), extend, true, displayedCursorColumn());
				break;
			case kbPgDn:
				moveCursor(lineMoveOffset(cursorOffset(), std::max(2, visibleTextRows()) - 1, displayedCursorColumn()), extend, true, displayedCursorColumn());
				break;
			case kbCtrlHome:
				moveCursor(0, extend, true);
				break;
			case kbCtrlEnd:
				moveCursor(bufferLength(), extend, true);
				break;
			case kbCtrlLeft:
				moveCursor(prevWordOffset(cursorOffset()), extend, false);
				break;
			case kbCtrlRight:
				moveCursor(nextWordOffset(cursorOffset()), extend, false);
				break;
			case kbEnter:
				if (!mReadOnly) newLineWithPreferredIndent();
				clearEvent(event);
				return;
			case kbBack:
				if (!mReadOnly) {
					if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
					else if (cursorOffset() > 0)
						replaceRangeAndSelect(static_cast<uint>(prevCharOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
				}
				clearEvent(event);
				return;
			case kbDel:
				if (!mReadOnly) {
					if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
					else
						deleteCharsAtCursor(1);
				}
				clearEvent(event);
				return;
			case kbIns:
				setInsertModeEnabled(!insertModeEnabled());
				clearEvent(event);
				return;
			case kbShiftIns:
				requestSystemClipboardPaste();
				clearEvent(event);
				return;
			case kbCtrlIns:
				copySelection();
				clearEvent(event);
				return;
			case kbShiftDel:
				cutSelection();
				clearEvent(event);
				return;
			default:
				return;
		}
		clearEvent(event);
	}

	void handleCommand(TEvent &event) {
		switch (event.message.command) {
			case cmSave:
				saveInPlace();
				break;
			case cmSaveAs:
				saveAsWithPrompt();
				break;
			case cmCut:
				cutSelection();
				break;
			case cmCopy:
				copySelection();
				break;
			case cmPaste:
				requestSystemClipboardPaste();
				break;
			case cmMrEditUndo: {
				MRTextBufferModel::CustomUndoRecord record;
				if (mBufferModel.undo(&record)) {
					const bool modifiedState = mBufferModel.isModified();
					adoptCommittedDocument(mBufferModel.document(), mBufferModel.cursor(), mBufferModel.selectionStart(), mBufferModel.selectionEnd(), modifiedState);
					if (owner != nullptr) {
						setBlockOverlayState(record.blockMode, record.blockAnchor, record.blockEnd, record.blockMarkingOn, false);
					}
				}
				break;
			}
			case cmMrEditRedo: {
				MRTextBufferModel::CustomUndoRecord record;
				if (mBufferModel.redo(&record)) {
					const bool modifiedState = mBufferModel.isModified();
					adoptCommittedDocument(mBufferModel.document(), mBufferModel.cursor(), mBufferModel.selectionStart(), mBufferModel.selectionEnd(), modifiedState);
					if (owner != nullptr) {
						setBlockOverlayState(record.blockMode, record.blockAnchor, record.blockEnd, record.blockMarkingOn, false);
					}
				}
				break;
			}
			case cmMrTextUpperCaseMenu:
				convertSelectionToUpperCase();
				break;
			case cmMrTextLowerCaseMenu:
				convertSelectionToLowerCase();
				break;
			case cmMrTextCenterLine:
				if (!mReadOnly) {
					MREditSetupSettings settings = configuredEditSetupSettings();
					centerCurrentLine(settings.leftMargin, settings.rightMargin > 0 ? settings.rightMargin : 78);
				}
				break;
			case cmMrTextReformatParagraph:
				if (!mReadOnly) {
					MREditSetupSettings settings = configuredEditSetupSettings();
					formatParagraph(settings.leftMargin, settings.rightMargin > 0 ? settings.rightMargin : 78);
				}
				break;
			case cmClear:
				if (!mReadOnly) replaceSelectionText(std::string());
				break;
			case cmCharLeft:
				moveCursor(prevCharOffset(cursorOffset()), false, false);
				break;
			case cmCharRight:
				moveCursor(nextCharOffset(cursorOffset()), false, false);
				break;
			case cmWordLeft:
				moveCursor(prevWordOffset(cursorOffset()), false, false);
				break;
			case cmWordRight:
				moveCursor(nextWordOffset(cursorOffset()), false, false);
				break;
			case cmLineStart:
				moveCursor(lineStartOffset(cursorOffset()), false, false);
				break;
			case cmLineEnd:
				moveCursor(lineEndOffset(cursorOffset()), false, false);
				break;
			case cmLineUp:
				moveCursor(lineMoveOffset(cursorOffset(), -1, displayedCursorColumn()), false, false, displayedCursorColumn());
				break;
			case cmLineDown:
				moveCursor(lineMoveOffset(cursorOffset(), 1, displayedCursorColumn()), false, false, displayedCursorColumn());
				break;
			case cmPageUp:
				moveCursor(lineMoveOffset(cursorOffset(), -(std::max(2, visibleTextRows()) - 1), displayedCursorColumn()), false, true, displayedCursorColumn());
				break;
			case cmPageDown:
				moveCursor(lineMoveOffset(cursorOffset(), std::max(2, visibleTextRows()) - 1, displayedCursorColumn()), false, true, displayedCursorColumn());
				break;
			case cmTextStart:
				moveCursor(0, false, true);
				break;
			case cmTextEnd:
				moveCursor(bufferLength(), false, true);
				break;
			case cmNewLine:
				if (!mReadOnly) newLineWithPreferredIndent();
				break;
			case cmBackSpace:
				if (!mReadOnly) {
					if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
					else if (cursorOffset() > 0)
						replaceRangeAndSelect(static_cast<uint>(prevCharOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
				}
				break;
			case cmDelChar:
				if (!mReadOnly) {
					if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
					else
						deleteCharsAtCursor(1);
				}
				break;
			case cmDelWord:
				if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(cursorOffset()), static_cast<uint>(nextWordOffset(cursorOffset())), "", 0);
				break;
			case cmDelWordLeft:
				if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(prevWordOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
				break;
			case cmDelStart:
				if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(lineStartOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
				break;
			case cmDelEnd:
				if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(cursorOffset()), static_cast<uint>(lineEndOffset(cursorOffset())), "", 0);
				break;
			case cmDelLine:
				if (!mReadOnly) deleteCurrentLineText();
				break;
			case cmInsMode:
				setInsertModeEnabled(!insertModeEnabled());
				break;
			case cmSelectAll:
				mSelectionAnchor = 0;
				mBufferModel.setCursorAndSelection(mBufferModel.length(), 0, mBufferModel.length());
				revealCursor(True);
				break;
			default:
				return;
		}
		clearEvent(event);
	}

	void handleMouse(TEvent &event) {
		const TextViewportGeometry viewport = textViewportGeometry();

		if ((event.mouse.buttons & mbLeftButton) == 0) return;
		if (dragFormatRulerAtLocalPoint(event, makeLocal(event.mouse.where))) {
			clearEvent(event);
			return;
		}

		select();
		std::size_t anchor = (event.mouse.controlKeyState & kbShift) != 0 && mBufferModel.hasSelection() ? mBufferModel.selection().anchor : mBufferModel.cursor();
		int targetColumn = 0;
		mSelectionAnchor = anchor;
		moveCursor(mouseOffset(makeLocal(event.mouse.where), &targetColumn), (event.mouse.controlKeyState & kbShift) != 0, false, targetColumn);

		while (mouseEvent(event, evMouseMove | evMouseAuto)) {
			if (event.what == evMouseAuto) {
				TPoint mouse = makeLocal(event.mouse.where);
				int dx = delta.x;
				int dy = delta.y;
				if (mouse.x < 0) --dx;
				else if (mouse.x >= size.x)
					++dx;
				if (mouse.y < viewport.topInset) --dy;
				else if (mouse.y >= viewport.topInset + std::max(0, visibleTextRows()))
					++dy;
				scrollTo(std::max(dx, 0), std::max(dy, 0));
			}
			int dragColumn = 0;
			std::size_t target = mouseOffset(makeLocal(event.mouse.where), &dragColumn);
			mBufferModel.setCursorAndSelection(target, mSelectionAnchor, target);
			if (freeCursorMovementEnabled()) mCursorVisualColumn = std::max(actualCursorVisualColumn(target), dragColumn);
			else
				mCursorVisualColumn = actualCursorVisualColumn(target);
			updateIndicator();
			drawView();
		}
		clearEvent(event);
	}

	std::size_t mouseOffset(TPoint local, int *visualColumnOut = nullptr) noexcept {
		TextViewportGeometry viewport = textViewportGeometry();
		const int textRows = std::max(1, visibleTextRows());
		int clampedY = std::max(0, std::min(local.y - viewport.topInset, textRows - 1));
		int row = clampedY + delta.y;
		int column = viewport.textColumnFromLocalX(local.x);
		std::size_t start = lineStartForIndex(static_cast<std::size_t>(std::max(row, 0)));
		if (visualColumnOut != nullptr) *visualColumnOut = column;
		return canonicalCursorOffset(charPtrOffset(start, column));
	}

	std::size_t canonicalCursorOffset(std::size_t pos) const noexcept {
		pos = std::min(pos, mBufferModel.length());
		if (pos > 0 && pos < mBufferModel.length() && mBufferModel.charAt(pos) == '\n' && mBufferModel.charAt(pos - 1) == '\r') return pos - 1;
		return pos;
	}

	void copySelection() {
		if (!mBufferModel.hasSelection()) return;
		MRTextBufferModel::Range range = mBufferModel.selection().range();
		const std::string text = mBufferModel.text().substr(range.start, range.length());
		TClipboard::setText(TStringView(text.data(), text.size()));
	}

	void cutSelection() {
		if (mReadOnly || !mBufferModel.hasSelection()) return;
		copySelection();
		replaceSelectionText(std::string());
	}

	void requestSystemClipboardPaste() {
		if (mReadOnly) return;
		TClipboard::requestText();
	}

	void replaceSelectionText(const std::string &text) {
		if (!mBufferModel.hasSelection()) {
			if (!text.empty()) insertBufferText(text);
			return;
		}
		MRTextBufferModel::Range range = mBufferModel.selection().range();
		replaceRangeAndSelect(static_cast<uint>(range.start), static_cast<uint>(range.end), text.data(), static_cast<uint>(text.size()));
	}

	void convertSelectionToUpperCase() {
		if (mReadOnly || !mBufferModel.hasSelection()) return;
		MRTextBufferModel::Range range = mBufferModel.selection().range();
		std::string text = mBufferModel.text().substr(range.start, range.length());
		for (char &c : text)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		replaceSelectionText(text);
		setSelectionOffsets(range.start, range.start + text.length());
	}

	void convertSelectionToLowerCase() {
		if (mReadOnly || !mBufferModel.hasSelection()) return;
		MRTextBufferModel::Range range = mBufferModel.selection().range();
		std::string text = mBufferModel.text().substr(range.start, range.length());
		for (char &c : text)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		replaceSelectionText(text);
		setSelectionOffsets(range.start, range.start + text.length());
	}

	Boolean confirmSaveOrDiscardUntitled() {
		const char *detail = nullptr;
		std::string persistentName;

		if (hasPersistentFileName()) {
			persistentName = trimAscii(fileName);
			if (!persistentName.empty() && upperAscii(persistentName) != "?NO-FILE?") detail = persistentName.c_str();
		}
		switch (mr::dialogs::showUnsavedChangesDialog("Save As", "Window has unsaved changes.", detail)) {
			case mr::dialogs::UnsavedChangesChoice::Save:
				return saveAsWithPrompt();
			case mr::dialogs::UnsavedChangesChoice::Discard:
				setDocumentModified(false);
				return True;
			default:
				return False;
		}
	}

	Boolean confirmSaveOrDiscardNamed() {
		switch (mr::dialogs::showUnsavedChangesDialog("Save", "Save changes to:", fileName)) {
			case mr::dialogs::UnsavedChangesChoice::Save:
				return saveInPlace();
			case mr::dialogs::UnsavedChangesChoice::Discard:
				setDocumentModified(false);
				return True;
			default:
				return False;
		}
	}

	TColorAttr tokenColor(MRSyntaxToken token, bool selected, TAttrPair pair) noexcept {
		TColorAttr normal = static_cast<TColorAttr>(pair);
		TColorAttr selectedAttr = static_cast<TColorAttr>(pair >> 8);
		uchar background = static_cast<uchar>((selected ? selectedAttr : normal) & 0xF0);

		if (selected) return selectedAttr;

		switch (token) {
			case MRSyntaxToken::Keyword:
			case MRSyntaxToken::Directive:
			case MRSyntaxToken::Section:
				return static_cast<TColorAttr>(background | 0x0E);
			case MRSyntaxToken::Type:
			case MRSyntaxToken::Key:
				return static_cast<TColorAttr>(background | 0x0B);
			case MRSyntaxToken::Number:
				return static_cast<TColorAttr>(background | 0x0A);
			case MRSyntaxToken::String:
				return static_cast<TColorAttr>(background | 0x0D);
			case MRSyntaxToken::Comment:
				return static_cast<TColorAttr>(background | 0x03);
			case MRSyntaxToken::Heading:
				return static_cast<TColorAttr>(background | 0x0F);
			default:
				return normal;
		}
	}

	void refreshSyntaxContext() {
		MRSyntaxLanguage oldLanguage = mBufferModel.language();
		mBufferModel.setSyntaxContext(hasPersistentFileName() ? fileName : "", mSyntaxTitleHint);
		if (mBufferModel.language() != oldLanguage) resetSyntaxWarmupState(true);
	}

	void resetSyntaxWarmupState(bool clearCache) noexcept {
		std::uint64_t cancelledTaskId = mSyntaxWarmupTaskId;
		bool hadTask = cancelledTaskId != 0;
		if (clearCache) mSyntaxTokenCache.clear();
		mSyntaxWarmupTaskId = 0;
		mSyntaxWarmupDocumentId = 0;
		mSyntaxWarmupVersion = 0;
		mSyntaxWarmupTopLine = 0;
		mSyntaxWarmupBottomLine = 0;
		mSyntaxWarmupLanguage = MRSyntaxLanguage::PlainText;
		if (hadTask) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
			notifyWindowTaskStateChanged();
		}
	}

	std::vector<std::size_t> syntaxWarmupLineStarts(std::size_t topLine, int rowCount) const {
		std::vector<std::size_t> lineStarts;
		if (rowCount <= 0) return lineStarts;

		std::size_t lineStart = lineStartForIndex(topLine);
		for (int i = 0; i < rowCount; ++i) {
			lineStarts.push_back(lineStart);
			if (lineStart >= mBufferModel.length()) break;
			std::size_t next = mBufferModel.nextLine(lineStart);
			if (next <= lineStart) break;
			lineStart = next;
		}
		return lineStarts;
	}

	bool hasSyntaxTokensForLineStarts(const std::vector<std::size_t> &lineStarts) const {
		for (std::size_t i = 0; i < lineStarts.size(); ++i)
			if (mSyntaxTokenCache.find(lineStarts[i]) == mSyntaxTokenCache.end()) return false;
		return true;
	}

	MRSyntaxTokenMap syntaxTokensForLine(std::size_t lineStart) const {
		std::map<std::size_t, MRSyntaxTokenMap>::const_iterator found = mSyntaxTokenCache.find(lineStart);
		if (found != mSyntaxTokenCache.end()) return found->second;
		return mBufferModel.tokenMapForLine(lineStart);
	}

	void formatSyntaxLine(TDrawBuffer &b, std::size_t lineStart, int hScroll, int width, int drawX, bool isDocumentLine, bool drawEofMarker, bool drawEofMarkerAsEmoji) {
		TAttrPair basePair = getColor(0x0201);
		TAttrPair changedPair = getColor(0x0505);
		TAttrPair selectionPair = getColor(0x0201);
		MRSyntaxTokenMap tokens;
		MRTextBufferModel::Range selection;
		std::size_t documentLength = mBufferModel.length();
		std::size_t lineEnd = lineStart;
		std::size_t cursorPos = 0;
		std::size_t lineIndex = 0;
		bool currentLine = false;
		bool currentLineInBlock = false;
		bool overlayActive = false;
		int overlayMode = 0;
		std::size_t overlayStart = 0;
		std::size_t overlayEnd = 0;
		std::size_t overlayLine1 = 0;
		std::size_t overlayLine2 = 0;
		int overlayCol1 = 0;
		int overlayCol2Exclusive = 0;
		std::size_t bytePos = 0;
		int visual = 0;
		int x = 0;
		const MREditSetupSettings settings = configuredEditSetupSettings();
		const bool displayTabs = configuredDisplayTabs();

		hScroll = std::max(hScroll, 0);
		width = std::max(width, 0);
		drawX = std::max(drawX, 0);
		if (!isDocumentLine) {
			TColorAttr color = tokenColor(MRSyntaxToken::Text, false, basePair);
			b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
			if (drawEofMarker) drawEofMarkerGlyph(b, hScroll, width, drawX, basePair, drawEofMarkerAsEmoji);
			return;
		}
		std::string lineText = mBufferModel.lineText(lineStart);
		TStringView line(lineText.data(), lineText.size());
		tokens = syntaxTokensForLine(lineStart);
		selection = mBufferModel.selection().range();
		lineEnd = mBufferModel.nextLine(lineStart);
		lineIndex = mBufferModel.lineIndex(lineStart);
		cursorPos = mBufferModel.cursor();
		overlayActive = mBlockOverlayActive && mBlockOverlayMode >= 1 && mBlockOverlayMode <= 3;
		if (overlayActive) {
			const std::size_t trackedEnd = mBlockOverlayTrackingCursor ? std::min(mBufferModel.cursor(), documentLength) : std::min(mBlockOverlayEnd, documentLength);
			const std::size_t trackedAnchor = std::min(mBlockOverlayAnchor, documentLength);
			overlayMode = mBlockOverlayMode;
			overlayStart = std::min(trackedAnchor, trackedEnd);
			overlayEnd = std::max(trackedAnchor, trackedEnd);
			if (overlayMode == 1 || overlayMode == 2) {
				overlayLine1 = std::min(mBufferModel.lineIndex(trackedAnchor), mBufferModel.lineIndex(trackedEnd));
				overlayLine2 = std::max(mBufferModel.lineIndex(trackedAnchor), mBufferModel.lineIndex(trackedEnd));
			}
			if (overlayMode == 2) {
				const std::size_t aLineStart = mBufferModel.lineStart(trackedAnchor);
				const std::size_t bLineStart = mBufferModel.lineStart(trackedEnd);
				const int aCol = charColumn(aLineStart, trackedAnchor);
				const int bCol = charColumn(bLineStart, trackedEnd);
				overlayCol1 = std::min(aCol, bCol);
				overlayCol2Exclusive = std::max(aCol, bCol);
				if (overlayCol2Exclusive <= overlayCol1) overlayCol2Exclusive = overlayCol1 + 1;
			}
		}
		currentLine = (lineStart <= cursorPos && cursorPos < lineEnd) || (cursorPos == documentLength && lineStart == cursorPos && lineEnd == cursorPos);
		if (overlayActive) {
			if (overlayMode == 3) currentLineInBlock = currentLine && overlayStart < overlayEnd && overlayStart < lineEnd && overlayEnd > lineStart;
			else
				currentLineInBlock = currentLine && overlayLine1 <= lineIndex && lineIndex <= overlayLine2;
		} else
			currentLineInBlock = currentLine && selection.start < selection.end && selection.start < lineEnd && selection.end > lineStart;
		if (currentLineInBlock) basePair = getColor(0x0204);
		else if (currentLine)
			basePair = getColor(0x0303);

		while (bytePos < line.size() && x < width) {
			std::size_t next = bytePos;
			std::size_t charWidth = 0;
			if (!nextDisplayChar(line, next, charWidth, visual, settings)) break;

			int nextVisual = visual + static_cast<int>(charWidth);
			if (nextVisual > hScroll) {
				std::size_t tokenIndex = bytePos;
				std::size_t documentPos = lineStart + bytePos;
				MRSyntaxToken token = tokenIndex < tokens.size() ? tokens[tokenIndex] : MRSyntaxToken::Text;
				bool selected = false;
				TAttrPair tokenPair;
				TColorAttr color;
				int visibleWidth = 0;

				if (overlayActive) {
					if (overlayMode == 3) selected = overlayStart <= documentPos && documentPos < overlayEnd;
					else if (overlayMode == 1)
						selected = overlayLine1 <= lineIndex && lineIndex <= overlayLine2;
					else if (overlayMode == 2)
						selected = overlayLine1 <= lineIndex && lineIndex <= overlayLine2 && visual < overlayCol2Exclusive && nextVisual > overlayCol1;
				} else
					selected = selection.start <= documentPos && documentPos < selection.end;
				bool changedChar = !currentLine && !currentLineInBlock && isDirtyOffset(documentPos);
				TAttrPair effectivePair = changedChar ? changedPair : basePair;
				tokenPair = selected ? selectionPair : effectivePair;
				color = tokenColor(token, selected, tokenPair);
				visibleWidth = nextVisual - std::max(visual, hScroll);

				if (line[bytePos] == '\t' && displayTabs && visual >= hScroll && visibleWidth > 0) {
					b.moveStr(static_cast<ushort>(drawX + x), "\xE2\x96\xB6", color, 1); // U+25B6
					if (visibleWidth > 1) b.moveChar(static_cast<ushort>(drawX + x + 1), ' ', color, static_cast<ushort>(visibleWidth - 1));
				} else if (line[bytePos] == '\t' || visual < hScroll)
					b.moveChar(static_cast<ushort>(drawX + x), ' ', color, static_cast<ushort>(visibleWidth));
				else
					b.moveStr(static_cast<ushort>(drawX + x), line.substr(bytePos, next - bytePos), color, static_cast<ushort>(visibleWidth));
				x += visibleWidth;
			}
			visual = nextVisual;
			bytePos = next;
		}

		if (x < width) {
			TColorAttr color = tokenColor(MRSyntaxToken::Text, false, basePair);
			b.moveChar(static_cast<ushort>(drawX + x), ' ', color, static_cast<ushort>(width - x));
		}
	}

	void drawEofMarkerGlyph(TDrawBuffer &b, int hScroll, int width, int drawX, TAttrPair basePair, bool drawEmoji) {
		static const char *const kEofMarkerText = "EOF";
		static const char *const kEofMarkerEmoji = "\xF0\x9F\x94\x9A";
		const char *marker = drawEmoji ? kEofMarkerEmoji : kEofMarkerText;
		int markerWidth = 0;
		TColorAttr markerColor = tokenColor(MRSyntaxToken::Text, false, basePair);
		unsigned char configuredMarkerColor = 0;

		if (width <= 0 || hScroll != 0) return;
		if (!drawEmoji && mCustomWindowEofMarkerColorOverrideValid) markerColor = mCustomWindowEofMarkerColorOverride;
		else if (!drawEmoji && configuredColorSlotOverride(kMrPaletteEofMarker, configuredMarkerColor))
			markerColor = static_cast<TColorAttr>(configuredMarkerColor);
		markerWidth = std::max(1, strwidth(marker));
		markerWidth = std::min(markerWidth, width);
		b.moveStr(static_cast<ushort>(drawX), marker, markerColor, static_cast<ushort>(markerWidth));
	}

	bool adoptCommittedDocument(const MRTextBufferModel::Document &document, std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState, const MRTextBufferModel::DocumentChangeSet *changeSet = nullptr) {
		cursorPos = std::min(cursorPos, document.length());
		selStart = std::min(selStart, document.length());
		selEnd = std::min(selEnd, document.length());
		if (selEnd < selStart) std::swap(selStart, selEnd);

		mBufferModel.document() = document;
		invalidateSaveNormalizationCache();
		resetSyntaxWarmupState(true);
		invalidateMiniMapCache(false);
		refreshSyntaxContext();
		cursorPos = canonicalCursorOffset(cursorPos);
		selStart = canonicalCursorOffset(selStart);
		selEnd = canonicalCursorOffset(selEnd);
		mBufferModel.setCursorAndSelection(cursorPos, selStart, selEnd);
		syncDisplayedCursorColumnFromCursor(false);
		mBufferModel.setModified(modifiedState);
		if (changeSet == nullptr || changeSet->changed) mFindMarkerRanges.clear();
		if (!modifiedState) clearDirtyRanges();
		else if (changeSet != nullptr && changeSet->changed) {
			remapDirtyRangesForAppliedChange(*changeSet);
			addDirtyRange(changeSet->touchedRange);
		}
		mSelectionAnchor = selStart;
		updateMetrics();
		scheduleLineIndexWarmupIfNeeded();
		scheduleSyntaxWarmupIfNeeded();
		scheduleSaveNormalizationWarmupIfNeeded();
		ensureCursorVisible(false);
		updateIndicator();
		drawView();
		return true;
	}

	TIndicator *mIndicator;
	bool mReadOnly;
	bool mCustomWindowEofMarkerColorOverrideValid = false;
	TColorAttr mCustomWindowEofMarkerColorOverride = 0;
	bool mInsertMode;
	bool mAutoIndent;
	char fileName[MAXPATH];
	std::string mSyntaxTitleHint;
	MRTextBufferModel mBufferModel;
	std::size_t mSelectionAnchor;
	int mCursorVisualColumn;
	bool mIndicatorUpdateInProgress;
	std::uint64_t mLineIndexWarmupTaskId;
	std::size_t mLineIndexWarmupDocumentId;
	std::size_t mLineIndexWarmupVersion;
	std::map<std::size_t, MRSyntaxTokenMap> mSyntaxTokenCache;
	std::uint64_t mSyntaxWarmupTaskId;
	std::size_t mSyntaxWarmupDocumentId;
	std::size_t mSyntaxWarmupVersion;
	std::size_t mSyntaxWarmupTopLine;
	std::size_t mSyntaxWarmupBottomLine;
	MRSyntaxLanguage mSyntaxWarmupLanguage;
	std::uint64_t mMiniMapWarmupTaskId;
	std::size_t mMiniMapWarmupDocumentId;
	std::size_t mMiniMapWarmupVersion;
	int mMiniMapWarmupRows;
	int mMiniMapWarmupBodyWidth;
	int mMiniMapWarmupViewportWidth;
	bool mMiniMapWarmupBraille;
	std::size_t mMiniMapWarmupWindowStartLine;
	std::size_t mMiniMapWarmupWindowLineCount;
	bool mMiniMapWarmupReschedulePending;
	MiniMapRenderCache mMiniMapCache;
	SaveNormalizationCache mSaveNormalizationCache;
	std::uint64_t mSaveNormalizationWarmupTaskId;
	std::size_t mSaveNormalizationWarmupDocumentId;
	std::size_t mSaveNormalizationWarmupVersion;
	std::size_t mSaveNormalizationWarmupOptionsHash;
	std::size_t mSaveNormalizationWarmupSourceBytes;
	std::chrono::steady_clock::time_point mSaveNormalizationWarmupStartedAt;
	double mSaveNormalizationThroughputBytesPerMicro;
	std::size_t mSaveNormalizationThroughputSamples;
	std::size_t mMiniMapInitialRenderReportedDocumentId;
	bool mBlockOverlayActive;
	int mBlockOverlayMode;
	std::size_t mBlockOverlayAnchor;
	std::size_t mBlockOverlayEnd;
	bool mBlockOverlayTrackingCursor;
	int mPreferredIndentColumn;
	std::vector<MRTextBufferModel::Range> mFindMarkerRanges;
	std::vector<MRTextBufferModel::Range> mDirtyRanges;
	LoadTiming mLastLoadTiming;
	bool mLargeFileMetricsTraceValid;
	bool mLastLargeFileMetricsExactKnown;
	int mLastLargeFileMetricsLimitY;
	int mLastLargeFileMetricsMaxY;
	int mLastLargeFileMetricsDeltaY;
	int mLastLargeFileMetricsNewDeltaY;

	void clearDirtyRanges() noexcept {
		mDirtyRanges.clear();
	}

	static void normalizePairRangeList(std::vector<std::pair<std::size_t, std::size_t>> &ranges) {
		std::sort(ranges.begin(), ranges.end(), [](const std::pair<std::size_t, std::size_t> &a, const std::pair<std::size_t, std::size_t> &b) { return a.first < b.first || (a.first == b.first && a.second < b.second); });
		std::vector<std::pair<std::size_t, std::size_t>> merged;
		for (const auto &item : ranges) {
			if (item.second <= item.first) continue;
			if (merged.empty() || item.first > merged.back().second) merged.push_back(item);
			else if (item.second > merged.back().second)
				merged.back().second = item.second;
		}
		ranges.swap(merged);
	}

	static void normalizeRangeList(std::vector<MRTextBufferModel::Range> &ranges) {
		std::sort(ranges.begin(), ranges.end(), [](const MRTextBufferModel::Range &a, const MRTextBufferModel::Range &b) { return a.start < b.start || (a.start == b.start && a.end < b.end); });
		std::vector<MRTextBufferModel::Range> merged;
		for (const MRTextBufferModel::Range &item : ranges) {
			if (item.end <= item.start) continue;
			if (merged.empty() || item.start > merged.back().end) merged.push_back(item);
			else if (item.end > merged.back().end)
				merged.back().end = item.end;
		}
		ranges.swap(merged);
	}

	void normalizeDirtyRanges() {
		normalizeRangeList(mDirtyRanges);
	}

	void pushMappedDirtyRange(std::vector<MRTextBufferModel::Range> &mapped, std::size_t start, std::size_t end, std::size_t maxLength) {
		start = std::min(start, maxLength);
		end = std::min(end, maxLength);
		if (end <= start) return;
		mapped.push_back(MRTextBufferModel::Range(start, end));
	}

	void remapDirtyRangesForAppliedChange(const MRTextBufferModel::DocumentChangeSet &change) {
		const std::size_t oldLength = change.oldLength;
		const std::size_t newLength = change.newLength;
		const MRTextBufferModel::Range touched = change.touchedRange.normalized();
		const long long delta = static_cast<long long>(newLength) - static_cast<long long>(oldLength);
		const std::size_t touchedLength = touched.length();
		const std::size_t editStart = std::min(touched.start, oldLength);
		std::size_t replacedOldLength = touchedLength;

		if (mDirtyRanges.empty()) return;
		if (delta >= 0) {
			const std::size_t deltaUnsigned = static_cast<std::size_t>(delta);
			replacedOldLength = touchedLength > deltaUnsigned ? touchedLength - deltaUnsigned : 0;
		}
		if (replacedOldLength > oldLength - editStart) replacedOldLength = oldLength - editStart;
		const std::size_t oldEditEnd = editStart + replacedOldLength;

		std::vector<MRTextBufferModel::Range> mapped;
		mapped.reserve(mDirtyRanges.size() + 2);

		for (std::size_t i = 0; i < mDirtyRanges.size(); ++i) {
			MRTextBufferModel::Range range = mDirtyRanges[i].clamped(oldLength).normalized();

			if (range.end <= range.start) continue;
			if (range.end <= editStart) {
				pushMappedDirtyRange(mapped, range.start, range.end, newLength);
				continue;
			}
			if (range.start >= oldEditEnd) {
				const long long shiftedStart = static_cast<long long>(range.start) + delta;
				const long long shiftedEnd = static_cast<long long>(range.end) + delta;
				if (shiftedEnd <= 0) continue;
				pushMappedDirtyRange(mapped, static_cast<std::size_t>(std::max<long long>(0, shiftedStart)), static_cast<std::size_t>(std::max<long long>(0, shiftedEnd)), newLength);
				continue;
			}

			if (range.start < editStart) pushMappedDirtyRange(mapped, range.start, editStart, newLength);
			if (range.end > oldEditEnd) {
				const long long shiftedStart = static_cast<long long>(oldEditEnd) + delta;
				const long long shiftedEnd = static_cast<long long>(range.end) + delta;
				if (shiftedEnd > 0) pushMappedDirtyRange(mapped, static_cast<std::size_t>(std::max<long long>(0, shiftedStart)), static_cast<std::size_t>(std::max<long long>(0, shiftedEnd)), newLength);
			}
		}

		mDirtyRanges.swap(mapped);
		normalizeDirtyRanges();
	}

	void addDirtyRange(MRTextBufferModel::Range range) {
		if (mBufferModel.length() == 0) return;
		range = range.clamped(mBufferModel.length());
		range.normalize();
		if (range.empty()) {
			std::size_t point = std::min(range.start, mBufferModel.length() - 1);
			range = MRTextBufferModel::Range(point, point + 1);
		}
		mDirtyRanges.push_back(range);
		normalizeDirtyRanges();
	}

	bool isDirtyOffset(std::size_t pos) const noexcept {
		if (mDirtyRanges.empty() || mBufferModel.length() == 0) return false;
		if (pos >= mBufferModel.length()) return false;
		for (const MRTextBufferModel::Range &item : mDirtyRanges) {
			if (item.end <= pos) continue;
			if (item.start > pos) break;
			return pos < item.end;
		}
		return false;
	}
};

#endif
