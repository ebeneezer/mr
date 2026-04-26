#ifndef MRFILEEDITOR_HPP
#define MRFILEEDITOR_HPP

#define Uses_TScroller
#define Uses_TEditor
#define Uses_TIndicator
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
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
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
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

	MRFileEditor(const TRect &bounds, TScrollBar *aHScrollBar, TScrollBar *aVScrollBar,
	              TIndicator *aIndicator, TStringView aFileName) noexcept
	    : TScroller(bounds, aHScrollBar, aVScrollBar), mIndicator(aIndicator), mReadOnly(false),
	      mInsertMode(true), mAutoIndent(false), mSyntaxTitleHint(), mBufferModel(),
		      mSelectionAnchor(0), mIndicatorUpdateInProgress(false),
		      mLineIndexWarmupTaskId(0), mLineIndexWarmupDocumentId(0), mLineIndexWarmupVersion(0),
			      mSyntaxTokenCache(), mSyntaxWarmupTaskId(0), mSyntaxWarmupDocumentId(0),
				      mSyntaxWarmupVersion(0), mSyntaxWarmupTopLine(0), mSyntaxWarmupBottomLine(0),
					      mSyntaxWarmupLanguage(MRSyntaxLanguage::PlainText), mMiniMapWarmupTaskId(0),
					      mMiniMapWarmupDocumentId(0), mMiniMapWarmupVersion(0), mMiniMapWarmupRows(0),
					      mMiniMapWarmupBodyWidth(0), mMiniMapWarmupViewportWidth(0), mMiniMapWarmupBraille(true),
					      mMiniMapWarmupWindowStartLine(0), mMiniMapWarmupWindowLineCount(0),
					      mMiniMapWarmupReschedulePending(false), mMiniMapCache(), mSaveNormalizationCache(),
			      mSaveNormalizationWarmupTaskId(0), mSaveNormalizationWarmupDocumentId(0),
			      mSaveNormalizationWarmupVersion(0), mSaveNormalizationWarmupOptionsHash(0),
			      mSaveNormalizationWarmupSourceBytes(0),
			      mSaveNormalizationWarmupStartedAt(std::chrono::steady_clock::time_point()),
			      mSaveNormalizationThroughputBytesPerMicro(0.0), mSaveNormalizationThroughputSamples(0),
			      mMiniMapInitialRenderReportedDocumentId(0), mBlockOverlayActive(false),
			      mBlockOverlayMode(0), mBlockOverlayAnchor(0), mBlockOverlayEnd(0),
			      mBlockOverlayTrackingCursor(false), mLastLoadTiming() {
		fileName[0] = EOS;
		options |= ofFirstClick;
		eventMask |= evMouse | evKeyboard | evCommand;
		if (!aFileName.empty())
			setPersistentFileName(aFileName);
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
			if (mReadOnly)
				setDocumentModified(false);
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
		if (mInsertMode == on)
			return;
		mInsertMode = on;
		syncFromEditorState(false);
		if (owner != nullptr)
			message(owner, evBroadcast, cmUpdateTitle, 0);
	}

	void refreshConfiguredVisualSettings() {
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

		if (pos >= len)
			return len;
		if (mBufferModel.charAt(pos) == '\r' && pos + 1 < len && mBufferModel.charAt(pos + 1) == '\n')
			return std::min(len, pos + 2);
		for (; count < sizeof(bytes) && pos + count < len; ++count)
			bytes[count] = mBufferModel.charAt(pos + count);
		std::size_t step = TText::next(TStringView(bytes, count));
		return std::min(len, pos + std::max<std::size_t>(step, 1));
	}

	std::size_t prevCharOffset(std::size_t pos) noexcept {
		char bytes[4];
		std::size_t start = 0;
		std::size_t count = 0;

		if (pos == 0)
			return 0;
		if (pos > 1 && mBufferModel.charAt(pos - 2) == '\r' && mBufferModel.charAt(pos - 1) == '\n')
			return pos - 2;
		start = pos > sizeof(bytes) ? pos - sizeof(bytes) : 0;
		count = pos - start;
		for (std::size_t i = 0; i < count; ++i)
			bytes[i] = mBufferModel.charAt(start + i);
		std::size_t step = TText::prev(TStringView(bytes, count), count);
		return pos - std::max<std::size_t>(step, 1);
	}

		std::size_t lineMoveOffset(std::size_t pos, int deltaLines) noexcept {
			std::size_t target = std::min(pos, mBufferModel.length());
			int targetVisualColumn =
			    charColumn(mBufferModel.lineStart(pos), std::min(pos, mBufferModel.length()));

			if (deltaLines < 0) {
				for (std::size_t i = 0, distance = static_cast<std::size_t>(-deltaLines); i < distance; ++i) {
					std::size_t prev = prevLineOffset(target);
					if (prev == target)
						break;
					target = prev;
				}
			} else {
				for (int i = 0; i < deltaLines; ++i) {
					std::size_t next = nextLineOffset(target);
					if (next == target)
						break;
					target = next;
				}
			}

			return charPtrOffset(lineStartOffset(target), targetVisualColumn);
		}

	std::size_t tabStopMoveOffset(std::size_t pos, bool forward) noexcept {
		const std::size_t cursor = std::min(pos, mBufferModel.length());
		const std::size_t lineStart = lineStartOffset(cursor);
		const int currentColumn = charColumn(lineStart, cursor) + 1;
		const int targetColumn = forward ? nextTabStopColumn(currentColumn)
		                                 : prevTabStopColumn(currentColumn);

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
		std::size_t p = 0;
		int visual = 0;
		int target = std::max(pos, 0);
		int tabSize = configuredTabSize();

		while (p < line.size()) {
			std::size_t next = p;
			std::size_t width = 0;
			if (!nextDisplayChar(line, next, width, visual, tabSize))
				break;
			if (visual + static_cast<int>(width) > target)
				break;
			visual += static_cast<int>(width);
			p = next;
		}
		return lineStart + p;
	}

	int charColumn(std::size_t start, std::size_t pos) const noexcept {
		std::size_t lineStart = mBufferModel.lineStart(start);
		std::string lineText = mBufferModel.lineText(lineStart);
		TStringView line(lineText.data(), lineText.size());
		std::size_t p = 0;
		std::size_t end = std::min(pos, mBufferModel.length()) - lineStart;
		int visual = 0;
		int tabSize = configuredTabSize();

		end = std::min(end, line.size());
		while (p < end) {
			std::size_t next = p;
			std::size_t width = 0;
			if (!nextDisplayChar(line, next, width, visual, tabSize))
				break;
			if (next > end)
				break;
			visual += static_cast<int>(width);
			p = next;
		}
		return visual;
	}

	void setCursorOffset(std::size_t pos, int = 0) {
		moveCursor(std::min(pos, mBufferModel.length()), false, false);
	}

	std::size_t offsetForGlobalPoint(TPoint where) noexcept {
		return mouseOffset(makeLocal(where));
	}

	void setBlockOverlayState(int mode, std::size_t anchor, std::size_t end, bool active,
	                          bool trackCursor = false) {
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
				if (end < start)
					std::swap(start, end);
				if (end == start) {
					if (end < length)
						++end;
					else if (start > 0)
						--start;
				}
				if (end > start)
					normalized.push_back(MRTextBufferModel::Range(start, end));
			}
		}
		normalizeRangeList(normalized);
		mFindMarkerRanges.swap(normalized);
		drawView();
	}

	void clearFindMarkerRanges() {
		if (mFindMarkerRanges.empty())
			return;
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

	const MRTextBufferModel &bufferModel() const noexcept {
		return mBufferModel;
	}

	MRTextBufferModel &bufferModel() noexcept {
		return mBufferModel;
	}

	void syncFromEditorState(bool = true) {
		refreshSyntaxContext();
		updateMetrics();
		updateIndicator();
	}

	void notifyWindowTaskStateChanged() {
		if (owner != nullptr)
			message(owner, evBroadcast, cmUpdateTitle, 0);
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

	bool applyLineIndexWarmup(const mr::editor::LineIndexWarmupData &warmup,
	                          std::size_t expectedVersion) {
		if (!mBufferModel.adoptLineIndexWarmup(warmup, expectedVersion))
			return false;
		mLineIndexWarmupTaskId = 0;
		mLineIndexWarmupDocumentId = 0;
		mLineIndexWarmupVersion = 0;
		notifyWindowTaskStateChanged();
		updateMetrics();
		updateIndicator();
		drawView();
		return true;
	}

	bool applySyntaxWarmup(const mr::coprocessor::SyntaxWarmupPayload &warmup,
	                       std::size_t expectedVersion, std::uint64_t expectedTaskId) {
		if (expectedTaskId == 0 || mSyntaxWarmupTaskId != expectedTaskId)
			return false;
		if (mBufferModel.documentId() != mSyntaxWarmupDocumentId || mBufferModel.version() != expectedVersion)
			return false;
		if (mBufferModel.language() != warmup.language)
			return false;

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

	bool applyMiniMapWarmup(const mr::coprocessor::MiniMapWarmupPayload &payload, std::size_t expectedVersion,
	                        std::uint64_t expectedTaskId) {
		return applyMiniMapWarmupInternal(payload, expectedVersion, expectedTaskId);
	}

	bool applySaveNormalizationWarmup(const mr::coprocessor::SaveNormalizationWarmupPayload &payload,
	                                  std::size_t expectedVersion, std::uint64_t expectedTaskId,
	                                  double runMicros) {
		if (expectedTaskId == 0 || mSaveNormalizationWarmupTaskId != expectedTaskId)
			return false;
		if (mBufferModel.documentId() != mSaveNormalizationWarmupDocumentId ||
		    mBufferModel.version() != expectedVersion)
			return false;
		if (mSaveNormalizationWarmupOptionsHash != payload.optionsHash)
			return false;
		noteSaveNormalizationThroughput(payload.sourceBytes, runMicros);
		clearSaveNormalizationWarmupTask(expectedTaskId);
		return true;
	}

	void clearLineIndexWarmupTask(std::uint64_t expectedTaskId) noexcept {
		if (expectedTaskId != 0 && mLineIndexWarmupTaskId != expectedTaskId)
			return;
		if (mLineIndexWarmupTaskId == 0)
			return;
		mLineIndexWarmupTaskId = 0;
		mLineIndexWarmupDocumentId = 0;
		mLineIndexWarmupVersion = 0;
		notifyWindowTaskStateChanged();
	}

	void clearSyntaxWarmupTask(std::uint64_t expectedTaskId) noexcept {
		if (expectedTaskId != 0 && mSyntaxWarmupTaskId != expectedTaskId)
			return;
		if (mSyntaxWarmupTaskId == 0)
			return;
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
		if (expectedTaskId != 0 && mSaveNormalizationWarmupTaskId != expectedTaskId)
			return;
		if (mSaveNormalizationWarmupTaskId == 0)
			return;
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

		if (mReadOnly || !hasPersistentFileName())
			return false;
		persistentName = trimAscii(fileName);
		if (upperAscii(persistentName) == "?NO-FILE?")
			return false;
		if (looksLikeUri(persistentName))
			return false;
		return true;
	}

	bool canSaveAs() const {
		return !mReadOnly;
	}

	bool loadMappedFile(TStringView path, std::string &error) {
		MRTextBufferModel::Document document;
		const auto mapStartedAt = std::chrono::steady_clock::now();

		mLastLoadTiming = LoadTiming();

		if (!document.loadMappedFile(path, error))
			return false;
		const double mappedLoadMs =
		    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mapStartedAt).count();
		const auto lineCountStartedAt = std::chrono::steady_clock::now();
		const std::size_t lines = document.lineCount();
		const double lineCountMs =
		    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lineCountStartedAt).count();

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
		if (!canSaveInPlace())
			return False;
		Boolean ok = writeDocumentToPath(fileName) ? True : False;
		if (ok == True)
			setDocumentModified(false);
		return ok;
	}

	Boolean saveAsWithPrompt() noexcept {
		char saveName[MAXPATH];

		if (!canSaveAs())
			return False;
		if (hasPersistentFileName())
			strnzcpy(saveName, fileName, sizeof(saveName));
		else
			initRememberedLoadDialogPath(saveName, sizeof(saveName), "*.*");
		if (TEditor::editorDialog(edSaveAs, saveName) == cmCancel)
			return False;
		fexpand(saveName);
		if (!samePath(saveName, fileName) && !confirmOverwriteForSaveAs(saveName))
			return False;
		if (!writeDocumentToPath(saveName))
			return False;
		rememberLoadDialogPath(saveName);
		setPersistentFileName(saveName);
		if (owner != nullptr)
			message((TView *)owner, evBroadcast, cmUpdateTitle, 0);
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
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(),
		                                                 "replace-buffer-data");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;

		if (data != nullptr && length != 0)
			text.assign(data, length);
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
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(),
		                                                 "append-buffer-data");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;
		std::size_t endPtr = mBufferModel.length();

		if (length == 0)
			return true;
		if (data != nullptr)
			text.assign(data, length);
		transaction.insert(endPtr, text);
		preview = mBufferModel.document();
		pushUndoSnapshot();
		commit = preview.tryApply(transaction);
		if (!commit.applied()) {
			mBufferModel.popUndoSnapshot();
			return false;
		}
		return adoptCommittedDocument(preview, endPtr + text.size(), endPtr + text.size(),
		                              endPtr + text.size(), false);
	}

	bool appendBufferText(const char *text) {
		uint length = text != nullptr ? static_cast<uint>(std::strlen(text)) : 0;
		return appendBufferData(text, length);
	}

	bool replaceRangeAndSelect(uint start, uint end, const char *data, uint length) {
		std::string text;
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(),
		                                                 "replace-range-select");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;
		MRTextBufferModel::Range range;

		if (mReadOnly)
			return false;
		if (end < start)
			std::swap(start, end);
		range = MRTextBufferModel::Range(start, end).clamped(mBufferModel.length());
		if (data != nullptr && length != 0)
			text.assign(data, length);
		transaction.replace(range, text);
		preview = mBufferModel.document();
		pushUndoSnapshot();
		commit = preview.tryApply(transaction);
		if (!commit.applied()) {
			mBufferModel.popUndoSnapshot();
			return false;
		}
		return adoptCommittedDocument(preview, range.start, range.start, range.start + text.size(), true,
		                              &commit.change);
	}

	bool insertBufferText(const std::string &text) {
		std::size_t start = mBufferModel.cursor();
		std::size_t end = start;
		MRTextBufferModel::Range range;
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(),
		                                                 "insert-buffer-text");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;

		if (mReadOnly)
			return false;
		if (mBufferModel.hasSelection()) {
			range = mBufferModel.selection().range();
			start = range.start;
			end = range.end;
		} else if (!mInsertMode) {
			std::size_t endSel = mBufferModel.cursor();
			for (std::string::size_type i = 0; i < text.size() && endSel < lineEndOffset(start); ++i)
				endSel = nextCharOffset(endSel);
			end = endSel;
		}
		range = MRTextBufferModel::Range(start, end).clamped(mBufferModel.length());
		transaction.replace(range, text);
		preview = mBufferModel.document();
		pushUndoSnapshot();
		commit = preview.tryApply(transaction);
		if (!commit.applied()) {
			mBufferModel.popUndoSnapshot();
			return false;
		}
		start = range.start + text.size();
		return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
	}

	bool replaceCurrentLineText(const std::string &text) {
		std::size_t start = mBufferModel.lineStart(mBufferModel.cursor());
		std::size_t end = mBufferModel.lineEnd(mBufferModel.cursor());
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(),
		                                                 "replace-current-line");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;

		if (mReadOnly)
			return false;
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

	bool formatParagraph(int rightMargin) {
		if (mReadOnly)
			return false;

		std::size_t start = mBufferModel.cursor();
		std::size_t end = start;

		while (start > 0) {
			std::size_t prevLineStart = mBufferModel.lineStart(mBufferModel.prevLine(start));
			if (isBlankString(mBufferModel.lineText(prevLineStart)))
				break;
			start = prevLineStart;
		}

		while (end < mBufferModel.length()) {
			std::size_t nextLineStart = mBufferModel.nextLine(end);
			if (isBlankString(mBufferModel.lineText(end)))
				break;
			end = nextLineStart;
		}

		if (start == end)
			return true;

		std::string paragraphText;
		paragraphText.reserve(end - start);
		std::size_t current = start;
		while (current < end) {
			std::string chunk = mBufferModel.document().lineText(current);
			paragraphText += chunk;
			current = mBufferModel.document().nextLine(current);
		}

		std::string formattedText;
		std::string word;
		std::size_t currentLineLength = 0;

		for (std::size_t i = 0; i <= paragraphText.length(); ++i) {
			if (i == paragraphText.length() || std::isspace(static_cast<unsigned char>(paragraphText[i]))) {
				if (!word.empty()) {
					if (currentLineLength > 0 && currentLineLength + 1 + word.length() > static_cast<std::size_t>(rightMargin)) {
						formattedText += "\n";
						currentLineLength = 0;
					} else if (currentLineLength > 0) {
						formattedText += " ";
						currentLineLength++;
					}
					formattedText += word;
					currentLineLength += word.length();
					word.clear();
				}
				if (i < paragraphText.length() && paragraphText[i] == '\n' && i > 0 && paragraphText[i-1] == '\n') {
					formattedText += "\n\n";
					currentLineLength = 0;
				}
			} else {
				word += paragraphText[i];
			}
		}

		if (paragraphText.back() == '\n')
			formattedText += "\n";

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

	bool deleteCharsAtCursor(int count) {
		std::size_t start = mBufferModel.cursor();
		std::size_t end = start;
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(),
		                                                 "delete-chars-at-cursor");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;

		if (mReadOnly)
			return false;
		if (count <= 0)
			return true;
		for (int i = 0; i < count && end < mBufferModel.length(); ++i)
			end = nextCharOffset(end);
		if (end <= start)
			return true;
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
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(),
		                                                 "delete-current-line");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;

		if (mReadOnly)
			return false;
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
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(),
		                                                 "replace-whole-buffer");
		MRTextBufferModel::Document preview;
		MRTextBufferModel::CommitResult commit;

		if (mReadOnly)
			return false;
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

	MRTextBufferModel::CommitResult applyStagedTransaction(
	    const MRTextBufferModel::StagedTransaction &transaction, std::size_t cursorPos,
	    std::size_t selStart, std::size_t selEnd, bool modifiedState = true) {
		MRTextBufferModel::Document preview = mBufferModel.document();
		pushUndoSnapshot();
		MRTextBufferModel::CommitResult result = preview.tryApply(transaction);

		if (result.applied()) {
			adoptCommittedDocument(preview, cursorPos, selStart, selEnd, modifiedState,
			                       &result.change);
		} else {
			mBufferModel.popUndoSnapshot();
		}
		return result;
	}

	bool newLineWithIndent(const std::string &fill) {
		return insertBufferText(std::string("\n") + fill);
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

			if (mrvmUiCopyWindowLastSearch(owner, currentFileName, lastSearchStart, lastSearchEnd,
			                              lastSearchCursor) &&
			    lastSearchEnd > lastSearchStart) {
				selection.start = std::min(lastSearchStart, mBufferModel.length());
				selection.end = std::min(lastSearchEnd, mBufferModel.length());
			}
		}
		MiniMapPalette miniMapPalette = resolveMiniMapPalette();
		const bool drawMiniMap = viewport.miniMapBodyWidth > 0 && viewport.miniMapInfoX >= 0;
		const bool miniMapUseBraille = useBrailleMiniMapRenderer();
		std::string viewportMarkerGlyph = normalizedMiniMapViewportMarkerGlyph(editSettings.miniMapMarkerGlyph);
		const int miniMapRows = std::max(0, miniMapViewportRows());
		if (mBufferModel.exactLineCountKnown())
			totalLines = std::max<std::size_t>(1, mBufferModel.lineCount());
		else
			totalLines = std::max<std::size_t>(
			    1, std::max<std::size_t>(mBufferModel.estimatedLineCount(),
			                             topLine + static_cast<std::size_t>(std::max(miniMapRows, 1))));
		if (drawMiniMap)
			scheduleMiniMapWarmupIfNeeded(viewport, miniMapUseBraille, totalLines, topLine);
		MiniMapOverlayState miniMapOverlay = computeMiniMapOverlayState(selection, totalLines);

		const int textRows = std::max(0, visibleTextRows());
		for (int y = 0; y < textRows; ++y) {
			TDrawBuffer buffer;
			bool isDocumentLine = lineIndex < totalLines;
			bool drawEofMarker = editSettings.showEofMarker && lineIndex == totalLines;
			bool drawEofMarkerAsEmoji = drawEofMarker && editSettings.showEofMarkerEmoji;
			if (showLineNumbers)
				drawLineNumberGutter(buffer, lineIndex, isDocumentLine, viewport.lineNumberX, viewport.lineNumberWidth,
				                     zeroFillLineNumbers);
			if (drawCodeFolding)
				drawCodeFoldingGutter(buffer, viewport.codeFoldingX, viewport.codeFoldingWidth);
			if (drawMiniMap)
				drawMiniMapGutter(buffer, y, miniMapRows, viewport, totalLines, topLine, miniMapUseBraille,
				                  viewportMarkerGlyph, miniMapPalette, miniMapOverlay);
			formatSyntaxLine(buffer, linePtr, delta.x, textWidth, viewport.textLeft, isDocumentLine, drawEofMarker,
			                 drawEofMarkerAsEmoji);
			writeBuf(0, y, size.x, 1, buffer);
			if (linePtr < mBufferModel.length())
				linePtr = mBufferModel.nextLine(linePtr);
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
			const bool shiftTabPressed =
			    event.keyDown.keyCode == kbShiftTab ||
			    ((event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) &&
			     hasShiftModifier(mods));
			if (shiftTabPressed) {
				handleKeyDown(event);
				return;
			}
		}

		TScroller::handleEvent(event);

		if (event.what == evBroadcast) {
			if (event.message.command == cmScrollBarClicked &&
			    (event.message.infoPtr == hScrollBar || event.message.infoPtr == vScrollBar)) {
				select();
				clearEvent(event);
				return;
			}
			if (event.message.command == cmScrollBarChanged &&
			    (event.message.infoPtr == hScrollBar || event.message.infoPtr == vScrollBar)) {
				clearEvent(event);
				return;
			}
		}

		switch (event.what) {
			case evMouseDown:
				handleMouse(event);
				break;
			case evMouseWheel:
				if (vScrollBar != nullptr)
					vScrollBar->handleEvent(event);
				if (event.what != evNothing && hScrollBar != nullptr)
					hScrollBar->handleEvent(event);
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
			if (useApproximateLargeFileMetrics())
				updateMetrics();
			scheduleSyntaxWarmupIfNeeded();
			drawView();
		} else {
			if (useApproximateLargeFileMetrics())
				updateMetrics();
			scheduleSyntaxWarmupIfNeeded();
			updateIndicator();
		}
	}

	virtual void setState(ushort aState, Boolean enable) override {
		TScroller::setState(aState, enable);
		if ((aState & (sfActive | sfSelected)) != 0)
			syncScrollBarsToState();
		if (aState == sfCursorVis || mIndicatorUpdateInProgress)
			return;
		updateIndicator();
	}

	virtual Boolean valid(ushort command) override {
		if (command == cmValid || command == cmReleasedFocus)
			return True;
		if (mReadOnly || !mBufferModel.isModified())
			return True;
		if (!canSaveInPlace())
			return confirmSaveOrDiscardUntitled();
		return confirmSaveOrDiscardNamed();
	}

  private:
	static std::string &clipboardText() {
		static std::string clipboard;
		return clipboard;
	}

	static bool isWordByte(char ch) noexcept {
		unsigned char uch = static_cast<unsigned char>(ch);
		return std::isalnum(uch) != 0 || ch == '_';
	}

	static bool hasShiftModifier(ushort mods) noexcept {
		return (mods & (kbShift | kbCtrlShift | kbAltShift)) != 0;
	}

	static int configuredTabSize() noexcept {
		int tabSize = configuredTabSizeSetting();
		if (tabSize < 1)
			tabSize = 1;
		if (tabSize > 32)
			tabSize = 32;
		return tabSize;
	}

	static bool configuredDisplayTabs() noexcept {
		return configuredDisplayTabsSetting();
	}

	static int tabDisplayWidth(int visualColumn, int tabSize) noexcept {
		int nextStop = ((visualColumn / tabSize) + 1) * tabSize;
		return std::max(1, nextStop - visualColumn);
	}

	static int nextTabStopColumn(int col) noexcept {
		int width = configuredTabSize();
		if (col < 1)
			col = 1;
		return ((col - 1) / width + 1) * width + 1;
	}

	static int prevTabStopColumn(int col) noexcept {
		int width = configuredTabSize();
		if (col <= 1)
			return 1;
		return ((col - 2) / width) * width + 1;
	}

	int visibleTextRows() const noexcept {
		return std::max(0, size.y);
	}

	void syncScrollBarsToState() noexcept {
		bool show = (state & (sfActive | sfSelected)) != 0;
		if (hScrollBar != nullptr) {
			if (show)
				hScrollBar->show();
			else
				hScrollBar->hide();
		}
		if (vScrollBar != nullptr) {
			if (show)
				vScrollBar->show();
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

	bool configuredShowLineNumbers() const {
		const MREditSetupSettings settings = configuredEditSetupSettings();
		return normalizedLineNumbersPosition(settings) != "OFF";
	}

	int lineNumberGutterWidthFor(bool showLineNumbers) const noexcept {
		if (!showLineNumbers)
			return 0;
		const int textRows = std::max(1, visibleTextRows());
		std::size_t visibleEnd =
		    static_cast<std::size_t>(std::max(delta.y, 0) + textRows);
		std::size_t lines = 0;
		if (mBufferModel.exactLineCountKnown())
			lines = std::max<std::size_t>(mBufferModel.lineCount(), visibleEnd);
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
		std::vector<std::pair<std::size_t, std::size_t>> findLineRanges;
		std::vector<std::pair<std::size_t, std::size_t>> dirtyLineRanges;
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
			return containsTextX(x) && y >= 0 && y < viewHeight;
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
			if (width <= 0 || width >= right - left)
				return false;
			outX = left;
			left += width;
			return true;
		}

		bool placeTrailing(int width, int &outX) noexcept {
			if (width <= 0 || width >= right - left)
				return false;
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
		if (isGutterPositionLeading(settings.lineNumbersPosition) ||
		    isGutterPositionTrailing(settings.lineNumbersPosition))
			return settings.lineNumbersPosition;
		return settings.showLineNumbers ? std::string("LEADING") : std::string("OFF");
	}

	static std::string normalizedCodeFoldingPosition(const MREditSetupSettings &settings) {
		if (isGutterPositionLeading(settings.codeFoldingPosition) ||
		    isGutterPositionTrailing(settings.codeFoldingPosition))
			return settings.codeFoldingPosition;
		return settings.codeFolding ? std::string("LEADING") : std::string("OFF");
	}

	static bool isMiniMapPositionLeading(const std::string &position) noexcept {
		return isGutterPositionLeading(position);
	}

	static bool isMiniMapPositionTrailing(const std::string &position) noexcept {
		return isGutterPositionTrailing(position);
	}

	static int normalizedMiniMapWidth(const MREditSetupSettings &settings) noexcept {
		if (!isMiniMapPositionLeading(settings.miniMapPosition) &&
		    !isMiniMapPositionTrailing(settings.miniMapPosition))
			return 0;
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
		if (normalized.empty())
			normalized = "LCM";
		return normalized;
	}

	TextViewportGeometry textViewportGeometryFor(const MREditSetupSettings &settings) const noexcept {
		TextViewportGeometry viewport;
		GutterLayoutLane lane(std::max(0, size.x));
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
			if (enabledOnLeading(marker))
				leadingSequence.push_back(marker);
			if (enabledOnTrailing(marker))
				trailingSequence.push_back(marker);
		}
		for (char marker : leadingSequence) {
			int width = gutterWidthFor(marker);
			int x = -1;
			if (width > 0 && lane.placeLeading(width, x))
				assignGutterPlacement(marker, x, true);
		}
		for (auto it = trailingSequence.rbegin(); it != trailingSequence.rend(); ++it) {
			int width = gutterWidthFor(*it);
			int x = -1;
			if (width > 0 && lane.placeTrailing(width, x))
				assignGutterPlacement(*it, x, false);
		}
		if (!leadingSequence.empty() && leadingSequence.back() == 'M') {
			int separatorX = -1;
			if (lane.placeLeading(1, separatorX))
				viewport.miniMapSeparatorX = separatorX;
		}
		if (!trailingSequence.empty() && trailingSequence.front() == 'M') {
			int separatorX = -1;
			if (lane.placeTrailing(1, separatorX))
				viewport.miniMapSeparatorX = separatorX;
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

	void drawLineNumberGutter(TDrawBuffer &b, std::size_t lineIndex, bool showNumber, int drawX, int width,
	                          bool zeroFill) {
		TColorAttr color = static_cast<TColorAttr>(getColor(0x0606));
		char numberBuffer[32];
		int digits = std::max(1, width);

		if (width <= 0)
			return;
		b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
		if (!showNumber)
			return;
		if (zeroFill)
			std::snprintf(numberBuffer, sizeof(numberBuffer), "%0*lu", digits,
			              static_cast<unsigned long>(lineIndex + 1));
		else
			std::snprintf(numberBuffer, sizeof(numberBuffer), "%*lu", digits,
			              static_cast<unsigned long>(lineIndex + 1));
		b.moveStr(static_cast<ushort>(drawX), numberBuffer, color, static_cast<ushort>(width));
	}

	void drawCodeFoldingGutter(TDrawBuffer &b, int drawX, int width) {
		unsigned char configured = 0;
		TColorAttr color = static_cast<TColorAttr>(getColor(0x0606));

		if (width <= 0)
			return;
		if (configuredColorSlotOverride(kMrPaletteCodeFolding, configured))
			color = static_cast<TColorAttr>(configured);
		b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
	}

	static bool useBrailleMiniMapRenderer() noexcept {
		static const int kBrailleWidth = strwidth("\xE2\xA3\xBF"); // U+28FF
		return kBrailleWidth == 1;
	}

	static std::string normalizedMiniMapViewportMarkerGlyph(const std::string &configuredGlyph) {
		if (configuredGlyph.empty() || strwidth(configuredGlyph.c_str()) != 1)
			return "│";
		return configuredGlyph;
	}

	static const char *miniMapWarmupTaskLabel() noexcept {
		return "rendering mini map";
	}

	static const char *lineIndexWarmupTaskLabel() noexcept {
		return "line-index-warmup";
	}

	static const char *syntaxWarmupTaskLabel() noexcept {
		return "syntax-warmup";
	}

	static const char *saveNormalizationWarmupTaskLabel() noexcept {
		return "save-normalization";
	}

	static std::string utf8FromCodepoint(std::uint32_t codepoint) {
		char bytes[5] = {0, 0, 0, 0, 0};
		if (codepoint <= 0x7F) {
			bytes[0] = static_cast<char>(codepoint);
			return std::string(bytes, 1);
		}
		if (codepoint <= 0x7FF) {
			bytes[0] = static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
			bytes[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
			return std::string(bytes, 2);
		}
		if (codepoint <= 0xFFFF) {
			bytes[0] = static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
			bytes[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
			bytes[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
			return std::string(bytes, 3);
		}
		bytes[0] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
		bytes[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
		bytes[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
		bytes[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
		return std::string(bytes, 4);
	}

	static const std::array<std::string, 256> &brailleGlyphTable() {
		static const std::array<std::string, 256> table = []() {
			std::array<std::string, 256> generated;
			generated[0] = " ";
			for (std::size_t i = 1; i < generated.size(); ++i)
				generated[i] = utf8FromCodepoint(static_cast<std::uint32_t>(0x2800 + i));
			return generated;
		}();
		return table;
	}

	static std::size_t scaledMidpoint(std::size_t sampleIndex, std::size_t sampleCount,
	                                  std::size_t targetCount) noexcept {
		if (sampleCount == 0 || targetCount == 0)
			return 0;
		unsigned long long numerator =
		    static_cast<unsigned long long>(sampleIndex) * 2ull + 1ull;
		unsigned long long scaled =
		    numerator * static_cast<unsigned long long>(targetCount);
		unsigned long long denominator = static_cast<unsigned long long>(sampleCount) * 2ull;
		std::size_t mapped = static_cast<std::size_t>(scaled / denominator);
		return std::min(mapped, targetCount - 1);
	}

	bool lineIntersectsDirtyRanges(std::size_t lineStart, std::size_t lineEnd) const noexcept {
		if (lineEnd <= lineStart || mDirtyRanges.empty())
			return false;
		for (const MRTextBufferModel::Range &range : mDirtyRanges) {
			if (range.end <= lineStart)
				continue;
			if (range.start >= lineEnd)
				break;
			return true;
		}
		return false;
	}

	MiniMapPalette resolveMiniMapPalette() {
		MiniMapPalette palette;
		unsigned char configured = 0;
		const TColorAttr fallback = static_cast<TColorAttr>(getColor(0x0201));

		palette.normal =
		    configuredColorSlotOverride(kMrPaletteMiniMapNormal, configured) ? static_cast<TColorAttr>(configured)
		                                                                    : fallback;
		palette.viewport =
		    configuredColorSlotOverride(kMrPaletteMiniMapViewport, configured) ? static_cast<TColorAttr>(configured)
		                                                                      : palette.normal;
		palette.changed =
		    configuredColorSlotOverride(kMrPaletteMiniMapChanged, configured) ? static_cast<TColorAttr>(configured)
		                                                                     : palette.normal;
		palette.findMarker =
		    configuredColorSlotOverride(kMrPaletteMiniMapFindMarker, configured) ? static_cast<TColorAttr>(configured)
		                                                                        : palette.normal;
		palette.errorMarker =
		    configuredColorSlotOverride(kMrPaletteMiniMapErrorMarker, configured)
		        ? static_cast<TColorAttr>(configured)
		        : palette.normal;
		return palette;
	}

	static std::pair<std::size_t, std::size_t> scaledInterval(std::size_t index, std::size_t count,
	                                                           std::size_t targetCount) noexcept {
		if (count == 0 || targetCount == 0)
			return std::make_pair(0u, 0u);
		std::size_t start = (index * targetCount) / count;
		std::size_t end = ((index + 1) * targetCount + count - 1) / count;
		if (end <= start)
			end = std::min(targetCount, start + 1);
		return std::make_pair(std::min(start, targetCount), std::min(end, targetCount));
	}

	struct MiniMapSamplingWindow {
		std::size_t startLine = 0;
		std::size_t lineCount = 1;
	};

	static MiniMapSamplingWindow miniMapSamplingWindowFor(std::size_t totalLines, std::size_t topLine,
	                                                      int rowCount, bool useBraille) noexcept {
		const std::size_t normalizedTotalLines = std::max<std::size_t>(1, totalLines);
		const std::size_t normalizedRowCount = std::max<std::size_t>(1, static_cast<std::size_t>(std::max(rowCount, 1)));
		const std::size_t samplingRowCount = useBraille ? normalizedRowCount * 4u : normalizedRowCount;
		const std::size_t maxWindowLineCount = normalizedRowCount * 9u;
		MiniMapSamplingWindow window;
		if (normalizedTotalLines <= maxWindowLineCount) {
			window.startLine = 0;
			window.lineCount = std::max(normalizedTotalLines, samplingRowCount);
			return window;
		}
		window.lineCount = std::max<std::size_t>(1, maxWindowLineCount);
		const std::size_t clampedTop = std::min(topLine, normalizedTotalLines - 1);
		std::size_t preferredStart = 0;
		if (clampedTop > window.lineCount / 2)
			preferredStart = clampedTop - window.lineCount / 2;
		const std::size_t maxStart = normalizedTotalLines - window.lineCount;
		window.startLine = std::min(preferredStart, maxStart);
		return window;
	}

	static bool ratioCellActive(int numerator, int denominator, int cellIndex, int cellCount) noexcept {
		if (numerator <= 0 || denominator <= 0 || cellCount <= 0)
			return false;
		if (numerator >= denominator)
			return true;
		long long lhs = static_cast<long long>(numerator) * static_cast<long long>(cellCount);
		long long rhs = static_cast<long long>(cellIndex + 1) * static_cast<long long>(denominator);
		return lhs >= rhs;
	}

	// Returns true if minimap cell [cellIndex] overlaps the content column range [from, to)
	// when the viewport has viewportWidth columns and the minimap has cellCount cells.
	static bool ratioCellInRange(int from, int to, int viewportWidth, int cellIndex, int cellCount) noexcept {
		if (from < 0 || to <= 0 || from >= to || viewportWidth <= 0 || cellCount <= 0)
			return false;
		long long cellLeft = static_cast<long long>(cellIndex) * viewportWidth;
		long long cellRight = static_cast<long long>(cellIndex + 1) * viewportWidth;
		long long cLeft = static_cast<long long>(from) * cellCount;
		long long cRight = static_cast<long long>(to) * cellCount;
		return cellRight > cLeft && cellLeft < cRight;
	}

	bool miniMapCacheReadyForViewport(const TextViewportGeometry &viewport, bool braille,
	                                  const MiniMapSamplingWindow &window) const noexcept {
		const int rowCount = std::max(0, miniMapViewportRows());
		return mMiniMapCache.valid && mMiniMapCache.documentId == mBufferModel.documentId() &&
		       mMiniMapCache.documentVersion == mBufferModel.version() &&
		       mMiniMapCache.rowCount == rowCount &&
		       mMiniMapCache.bodyWidth == viewport.miniMapBodyWidth &&
		       mMiniMapCache.viewportWidth == std::max(1, viewport.width) && mMiniMapCache.braille == braille &&
		       mMiniMapCache.windowStartLine == window.startLine &&
		       mMiniMapCache.windowLineCount == std::max<std::size_t>(1, window.lineCount);
	}

	int miniMapViewportRows() const noexcept {
		return std::max(0, visibleTextRows());
	}

	void clearMiniMapWarmupTaskInternal(std::uint64_t expectedTaskId) noexcept {
		if (expectedTaskId != 0 && mMiniMapWarmupTaskId != expectedTaskId)
			return;
		if (mMiniMapWarmupTaskId == 0)
			return;
		const bool shouldReschedule = mMiniMapWarmupReschedulePending;
		mMiniMapWarmupTaskId = 0;
		mMiniMapWarmupDocumentId = 0;
		mMiniMapWarmupVersion = 0;
		mMiniMapWarmupRows = 0;
		mMiniMapWarmupBodyWidth = 0;
		mMiniMapWarmupViewportWidth = 0;
		mMiniMapWarmupBraille = true;
		mMiniMapWarmupWindowStartLine = 0;
		mMiniMapWarmupWindowLineCount = 0;
		mMiniMapWarmupReschedulePending = false;
		notifyWindowTaskStateChanged();
		if (shouldReschedule)
			drawView();
	}

	void invalidateMiniMapCache(bool cancelTask) {
		const bool keepStaleCache = !cancelTask && mMiniMapCache.documentId == mBufferModel.documentId() &&
		                           mMiniMapCache.bodyWidth > 0 && mMiniMapCache.rowCount > 0;
		mMiniMapCache.valid = false;
		if (!keepStaleCache) {
			mMiniMapCache.rowPatterns.clear();
			mMiniMapCache.rowLineStarts.clear();
			mMiniMapCache.rowLineEnds.clear();
		}
		mMiniMapWarmupReschedulePending = false;
		if (cancelTask && mMiniMapWarmupTaskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mMiniMapWarmupTaskId));
			clearMiniMapWarmupTaskInternal(mMiniMapWarmupTaskId);
		}
	}

	bool applyMiniMapWarmupInternal(const mr::coprocessor::MiniMapWarmupPayload &payload,
	                                std::size_t expectedVersion, std::uint64_t expectedTaskId) {
		if (expectedTaskId == 0 || mMiniMapWarmupTaskId != expectedTaskId)
			return false;
		if (mBufferModel.documentId() != mMiniMapWarmupDocumentId || mBufferModel.version() != expectedVersion)
			return false;

		mMiniMapCache.valid = true;
		mMiniMapCache.braille = payload.braille;
		mMiniMapCache.rowCount = payload.rowCount;
		mMiniMapCache.bodyWidth = payload.bodyWidth;
		mMiniMapCache.documentId = mBufferModel.documentId();
		mMiniMapCache.documentVersion = mBufferModel.version();
		mMiniMapCache.totalLines = std::max<std::size_t>(1, payload.totalLines);
		mMiniMapCache.windowStartLine = payload.windowStartLine;
		mMiniMapCache.windowLineCount = std::max<std::size_t>(1, payload.windowLineCount);
		mMiniMapCache.viewportWidth = std::max(1, payload.viewportWidth);
		mMiniMapCache.rowPatterns = payload.rowPatterns;
		mMiniMapCache.rowLineStarts = payload.rowLineStarts;
		mMiniMapCache.rowLineEnds = payload.rowLineEnds;

		clearMiniMapWarmupTaskInternal(expectedTaskId);
		drawView();
		return true;
	}

	void scheduleMiniMapWarmupIfNeeded(const TextViewportGeometry &viewport, bool useBraille,
	                                   std::size_t totalLinesHint, std::size_t topLine) {
		const int rowCount = miniMapViewportRows();
		if (viewport.miniMapBodyWidth <= 0 || rowCount <= 0) {
			invalidateMiniMapCache(true);
			return;
		}
		const std::size_t docId = mBufferModel.documentId();
		const std::size_t version = mBufferModel.version();
		std::size_t totalLines = std::max<std::size_t>(1, totalLinesHint);
		if (mBufferModel.exactLineCountKnown())
			totalLines = std::max<std::size_t>(1, mBufferModel.lineCount());
		const MiniMapSamplingWindow samplingWindow = miniMapSamplingWindowFor(totalLines, topLine, rowCount, useBraille);
		if (miniMapCacheReadyForViewport(viewport, useBraille, samplingWindow))
			return;

		const int bodyWidth = viewport.miniMapBodyWidth;
		const int viewportWidth = std::max(1, viewport.width);
		const int tabSize = configuredTabSize();
		if (mMiniMapWarmupTaskId != 0) {
			if (mMiniMapWarmupDocumentId == docId && mMiniMapWarmupVersion == version &&
			    mMiniMapWarmupRows == rowCount && mMiniMapWarmupBodyWidth == bodyWidth &&
			    mMiniMapWarmupViewportWidth == viewportWidth && mMiniMapWarmupBraille == useBraille &&
			    mMiniMapWarmupWindowStartLine == samplingWindow.startLine &&
			    mMiniMapWarmupWindowLineCount == samplingWindow.lineCount)
				return;
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mMiniMapWarmupTaskId));
			clearMiniMapWarmupTaskInternal(mMiniMapWarmupTaskId);
		}

		MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
		std::uint64_t previousTaskId = mMiniMapWarmupTaskId;
		mMiniMapWarmupDocumentId = docId;
		mMiniMapWarmupVersion = version;
		mMiniMapWarmupRows = rowCount;
		mMiniMapWarmupBodyWidth = bodyWidth;
		mMiniMapWarmupViewportWidth = viewportWidth;
		mMiniMapWarmupBraille = useBraille;
		mMiniMapWarmupWindowStartLine = samplingWindow.startLine;
		mMiniMapWarmupWindowLineCount = samplingWindow.lineCount;
		mMiniMapWarmupReschedulePending = false;
		mMiniMapWarmupTaskId = mr::coprocessor::globalCoprocessor().submit(
		    mr::coprocessor::Lane::MiniMap, mr::coprocessor::TaskKind::MiniMapWarmup, docId, version,
		    miniMapWarmupTaskLabel(),
		    [snapshot, rowCount, bodyWidth, viewportWidth, useBraille, tabSize,
		     totalLines, samplingWindow](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
			    mr::coprocessor::Result result;
			    struct MiniMapLineSample {
				    std::uint64_t dotColumnBits = 0;
			    };
			    std::map<std::size_t, MiniMapLineSample> sampledLineSamples;
			    std::vector<unsigned char> rowPatterns;
			    std::vector<std::size_t> rowLineStarts;
			    std::vector<std::size_t> rowLineEnds;
			    const int dotRows = useBraille ? std::max(1, rowCount * 4) : std::max(1, rowCount);
			    const int dotCols = useBraille ? std::max(1, bodyWidth * 2) : std::max(1, bodyWidth);
			    const std::size_t windowStartLine = samplingWindow.startLine;
			    const std::size_t windowLineCount = std::max<std::size_t>(1, samplingWindow.lineCount);
			    std::size_t normalizedTotalLines = std::max<std::size_t>(1, totalLines);
			    auto shouldStop = [&]() noexcept { return stopToken.stop_requested() || info.cancelRequested(); };
			    result.task = info;

			    if (shouldStop()) {
				    result.status = mr::coprocessor::TaskStatus::Cancelled;
				    return result;
			    }
			    if (snapshot.exactLineCountKnown())
				    normalizedTotalLines = std::max<std::size_t>(1, snapshot.lineCount());
			    rowPatterns.assign(static_cast<std::size_t>(std::max(0, rowCount) * std::max(0, bodyWidth)), 0);
			    rowLineStarts.assign(static_cast<std::size_t>(std::max(0, rowCount)), 0);
			    rowLineEnds.assign(static_cast<std::size_t>(std::max(0, rowCount)), 0);

			    auto lineSampleAt = [&](std::size_t lineIndex) -> const MiniMapLineSample & {
				    auto cached = sampledLineSamples.find(lineIndex);
				    if (cached != sampledLineSamples.end())
					    return cached->second;
				    MiniMapLineSample sample;
				    if (lineIndex < normalizedTotalLines) {
					    std::size_t lineStart = snapshot.lineStartByIndex(lineIndex);
					    std::string lineText = snapshot.lineText(lineStart);
					    std::size_t index = 0;
					    int visualColumn = 0;
					    while (index < lineText.size()) {
						    std::size_t current = index;
						    std::size_t next = index;
						    std::size_t width = 0;
						    if (!nextDisplayChar(lineText, next, width, visualColumn, tabSize))
							    break;
						    unsigned char ch = static_cast<unsigned char>(lineText[current]);
						    if (std::isspace(ch) == 0) {
							    const long long c = static_cast<long long>(visualColumn);
							    const long long w = static_cast<long long>(width);
							    const long long n = static_cast<long long>(dotCols);
							    const long long v = static_cast<long long>(viewportWidth);
							    const int dotColStart = static_cast<int>(c * n / v);
							    const int dotColEnd = static_cast<int>(((c + w) * n - 1) / v);
							    for (int dc = std::max(0, dotColStart); dc <= std::min(63, dotColEnd); ++dc)
								    sample.dotColumnBits |= (1ULL << dc);
						    }
						    visualColumn += static_cast<int>(width);
						    index = next;
					    }
				    }
				    auto inserted = sampledLineSamples.insert(std::make_pair(lineIndex, sample));
				    return inserted.first->second;
			    };

			    for (int y = 0; y < rowCount; ++y) {
				    if (shouldStop()) {
					    result.status = mr::coprocessor::TaskStatus::Cancelled;
					    return result;
				    }
				    std::pair<std::size_t, std::size_t> lineSpan =
				        scaledInterval(static_cast<std::size_t>(y), static_cast<std::size_t>(rowCount), windowLineCount);
				    rowLineStarts[static_cast<std::size_t>(y)] =
				        std::min(normalizedTotalLines, windowStartLine + lineSpan.first);
				    rowLineEnds[static_cast<std::size_t>(y)] =
				        std::min(normalizedTotalLines, windowStartLine + lineSpan.second);
				    for (int x = 0; x < bodyWidth; ++x) {
					    unsigned char pattern = 0;
					    if (useBraille) {
						    static const unsigned char dotBits[4][2] = {
						        {0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};
						    for (int py = 0; py < 4; ++py) {
							    std::size_t sampleRow = static_cast<std::size_t>(y * 4 + py);
							    if (sampleRow >= windowLineCount)
								    continue;
							    std::size_t lineIndex =
							        windowStartLine +
							        scaledMidpoint(sampleRow, static_cast<std::size_t>(dotRows), windowLineCount);
							    const MiniMapLineSample &sample = lineSampleAt(lineIndex);
							    for (int px = 0; px < 2; ++px) {
								    const int dotColumn = x * 2 + px;
								    if (dotColumn < 64 && (sample.dotColumnBits & (1ULL << dotColumn)) != 0)
									    pattern |= dotBits[py][px];
							    }
						    }
					    } else {
						    if (static_cast<std::size_t>(y) < windowLineCount) {
							    std::size_t lineIndex = windowStartLine +
							                            scaledMidpoint(static_cast<std::size_t>(y),
							                                           static_cast<std::size_t>(rowCount), windowLineCount);
							    const MiniMapLineSample &sample = lineSampleAt(lineIndex);
							    if (x < 64 && (sample.dotColumnBits & (1ULL << x)) != 0)
								    pattern = 1;
						    }
					    }
					    rowPatterns[static_cast<std::size_t>(y * bodyWidth + x)] = pattern;
					    }
				    }
			    result.status = mr::coprocessor::TaskStatus::Completed;
			    result.payload = std::make_shared<mr::coprocessor::MiniMapWarmupPayload>(
			        useBraille, rowCount, bodyWidth, normalizedTotalLines, windowStartLine, windowLineCount,
			        viewportWidth, std::move(rowPatterns),
			        std::move(rowLineStarts), std::move(rowLineEnds));
			    return result;
		    });
		if (mMiniMapWarmupTaskId != previousTaskId)
			notifyWindowTaskStateChanged();
	}

	MiniMapOverlayState computeMiniMapOverlayState(const MRTextBufferModel::Range &selection,
	                                               std::size_t totalLines) const {
		MiniMapOverlayState overlay;
		auto addFindLineRange = [&](const MRTextBufferModel::Range &range) {
			if (range.end <= range.start || mBufferModel.length() == 0)
				return;
			const std::size_t startOffset = std::min(range.start, mBufferModel.length() - 1);
			const std::size_t endOffset = std::min(range.end - 1, mBufferModel.length() - 1);
			const std::size_t lineStart = std::min<std::size_t>(mBufferModel.lineIndex(startOffset), totalLines);
			const std::size_t lineEnd = std::min<std::size_t>(mBufferModel.lineIndex(endOffset) + 1, totalLines);
			if (lineEnd > lineStart)
				overlay.findLineRanges.push_back(std::make_pair(lineStart, lineEnd));
		};

		if (selection.end > selection.start)
			addFindLineRange(selection);
		for (const MRTextBufferModel::Range &range : mFindMarkerRanges)
			addFindLineRange(range);
		normalizePairRangeList(overlay.findLineRanges);
		for (const MRTextBufferModel::Range &range : mDirtyRanges) {
			if (range.end <= range.start || range.start >= mBufferModel.length())
				continue;
			std::size_t startOffset = std::min(range.start, mBufferModel.length() - 1);
			std::size_t endOffset = std::min(range.end - 1, mBufferModel.length() - 1);
			std::size_t lineStart = std::min<std::size_t>(mBufferModel.lineIndex(startOffset), totalLines);
			std::size_t lineEnd = std::min<std::size_t>(mBufferModel.lineIndex(endOffset) + 1, totalLines);
			if (lineEnd > lineStart)
				overlay.dirtyLineRanges.push_back(std::make_pair(lineStart, lineEnd));
		}
		normalizePairRangeList(overlay.dirtyLineRanges);
		return overlay;
	}

	static bool lineIntervalOverlaps(std::size_t start, std::size_t end,
	                                 const std::vector<std::pair<std::size_t, std::size_t>> &ranges) noexcept {
		for (const auto &range : ranges) {
			if (range.second <= start)
				continue;
			if (range.first >= end)
				continue;
			return true;
		}
		return false;
	}

	void drawMiniMapGutter(TDrawBuffer &b, int y, int miniMapRows, const TextViewportGeometry &viewport,
	                       std::size_t totalLines, std::size_t topLine, bool useBraille,
	                       const std::string &viewportMarkerGlyph,
	                       const MiniMapPalette &palette, const MiniMapOverlayState &overlay) {
		if (viewport.miniMapBodyWidth <= 0 || viewport.miniMapBodyX < 0 || viewport.miniMapInfoX < 0 || totalLines == 0 ||
		    miniMapRows <= 0)
			return;

		const std::array<std::string, 256> &glyphTable = brailleGlyphTable();
		const int bodyX = viewport.miniMapBodyX;
		const int bodyWidth = viewport.miniMapBodyWidth;
		const MiniMapSamplingWindow samplingWindow = miniMapSamplingWindowFor(totalLines, topLine, miniMapRows, useBraille);
		const bool cacheReady = miniMapCacheReadyForViewport(viewport, useBraille, samplingWindow);
		const bool stalePatternCacheUsable =
		    !cacheReady && mMiniMapCache.bodyWidth == bodyWidth && mMiniMapCache.rowCount == miniMapRows &&
		    !mMiniMapCache.rowPatterns.empty();
		std::size_t rowLineStart = 0;
		std::size_t rowLineEnd = 0;

		if (y >= miniMapRows) {
			b.moveChar(static_cast<ushort>(bodyX), ' ', palette.normal, static_cast<ushort>(bodyWidth));
			if (viewport.miniMapSeparatorX >= 0 && viewport.miniMapSeparatorX < size.x)
				b.moveChar(static_cast<ushort>(viewport.miniMapSeparatorX), ' ', palette.normal, 1);
			b.moveChar(static_cast<ushort>(viewport.miniMapInfoX), ' ', palette.normal, 1);
			return;
		}

		if ((cacheReady || stalePatternCacheUsable) && static_cast<std::size_t>(y) < mMiniMapCache.rowLineStarts.size() &&
		    static_cast<std::size_t>(y) < mMiniMapCache.rowLineEnds.size()) {
			rowLineStart = mMiniMapCache.rowLineStarts[static_cast<std::size_t>(y)];
			rowLineEnd = mMiniMapCache.rowLineEnds[static_cast<std::size_t>(y)];
		} else {
			std::pair<std::size_t, std::size_t> rowSpan = scaledInterval(
			    static_cast<std::size_t>(y), static_cast<std::size_t>(std::max(miniMapRows, 1)),
			    samplingWindow.lineCount);
			const std::size_t normTotal = std::max<std::size_t>(1, totalLines);
			rowLineStart = std::min(samplingWindow.startLine + rowSpan.first, normTotal);
			rowLineEnd = std::min(samplingWindow.startLine + rowSpan.second, normTotal);
		}

		bool rowChanged = lineIntervalOverlaps(rowLineStart, rowLineEnd, overlay.dirtyLineRanges);
		bool rowFind = lineIntervalOverlaps(rowLineStart, rowLineEnd, overlay.findLineRanges);
		bool rowError = false;

		for (int x = 0; x < bodyWidth; ++x) {
			unsigned char pattern = 0;
			if (cacheReady || stalePatternCacheUsable) {
				std::size_t index = static_cast<std::size_t>(y * bodyWidth + x);
				if (index < mMiniMapCache.rowPatterns.size())
					pattern = mMiniMapCache.rowPatterns[index];
			}
				TColorAttr rowPriorityColor = palette.normal;
				if (rowError) {
					rowPriorityColor = palette.errorMarker;
				} else if (rowFind) {
					rowPriorityColor = palette.findMarker;
				} else if (rowChanged) {
					rowPriorityColor = palette.changed;
				}
				const bool rowOverlayActive = rowError || rowFind || rowChanged;
				TColorAttr cellColor = (pattern != 0 || rowOverlayActive) ? rowPriorityColor : palette.normal;
			if (useBraille)
				b.moveStr(static_cast<ushort>(bodyX + x), glyphTable[pattern], cellColor, 1);
			else if (pattern != 0)
				b.moveStr(static_cast<ushort>(bodyX + x), "\xE2\x96\x88", cellColor, 1); // U+2588
			else
				b.moveChar(static_cast<ushort>(bodyX + x), ' ', cellColor, 1);
		}

		if (viewport.miniMapSeparatorX >= 0 && viewport.miniMapSeparatorX < size.x)
			b.moveChar(static_cast<ushort>(viewport.miniMapSeparatorX), ' ', palette.normal, 1);

		std::size_t clampedTopLine = std::min(topLine, totalLines - 1);
		std::size_t visibleLines = static_cast<std::size_t>(std::max(miniMapRows, 1));
		std::size_t viewportLineEnd = std::min(totalLines, clampedTopLine + visibleLines);
		bool markerVisible = false;
		if (rowLineEnd > rowLineStart)
			markerVisible = rowLineStart < viewportLineEnd && rowLineEnd > clampedTopLine;
		if (markerVisible)
			b.moveStr(static_cast<ushort>(viewport.miniMapInfoX), viewportMarkerGlyph, palette.viewport, 1);
		else
			b.moveChar(static_cast<ushort>(viewport.miniMapInfoX), ' ', palette.normal, 1);
	}

	static bool nextDisplayChar(TStringView text, std::size_t &index, std::size_t &width, int visualColumn,
	                            int tabSize) noexcept {
		if (index >= text.size())
			return false;
		if (text[index] == '\t') {
			++index;
			width = static_cast<std::size_t>(tabDisplayWidth(visualColumn, tabSize));
			return true;
		}
		return TText::next(text, index, width);
	}

	static int displayWidthForText(TStringView text, int tabSize) noexcept {
		std::size_t index = 0;
		int visual = 0;

		while (index < text.size()) {
			std::size_t next = index;
			std::size_t width = 0;
			if (!nextDisplayChar(text, next, width, visual, tabSize))
				break;
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
				if (!out)
					return failWrite();
			}
			return true;
		}
		const std::size_t sourceBytes = mBufferModel.document().length();
		const auto normalizeStartedAt = std::chrono::steady_clock::now();
		const std::size_t flushThresholdBytes = static_cast<std::size_t>(256) * 1024;
		MRTextSaveStreamState normalizeState;
		std::string outputBuffer;
		auto flushOutput = [&]() -> bool {
			if (outputBuffer.empty())
				return true;
			writeChunk(out, outputBuffer.data(), outputBuffer.size());
			outputBuffer.clear();
			return static_cast<bool>(out);
		};

		outputBuffer.reserve(flushThresholdBytes + 1024);
		for (std::size_t i = 0; i < pieceCount; ++i) {
			mr::editor::PieceChunkView chunk = mBufferModel.document().pieceChunk(i);
			if (chunk.length == 0)
				continue;
			appendNormalizedTextSaveChunk(std::string_view(chunk.data, chunk.length), saveOptions,
			                              normalizeState, outputBuffer);
			if (outputBuffer.size() >= flushThresholdBytes && !flushOutput())
				return failWrite();
		}
		finalizeNormalizedTextSaveStream(saveOptions, normalizeState, outputBuffer);
		if (!flushOutput())
			return failWrite();

		noteSaveNormalizationThroughput(
		    sourceBytes,
		    static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
		                            std::chrono::steady_clock::now() - normalizeStartedAt)
		                            .count()));
		if (!out)
			return failWrite();
		return true;
	}

	static bool pathIsRegularFile(const char *path) noexcept {
		struct stat st;
		if (path == nullptr || *path == '\0')
			return false;
		return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
	}

	static bool samePath(const char *lhs, const char *rhs) noexcept {
		struct stat lhsStat;
		struct stat rhsStat;
		char lhsExpanded[MAXPATH];
		char rhsExpanded[MAXPATH];
		std::size_t i = 0;

		if (lhs == nullptr || rhs == nullptr)
			return false;
		if (::stat(lhs, &lhsStat) == 0 && ::stat(rhs, &rhsStat) == 0)
			return lhsStat.st_dev == rhsStat.st_dev && lhsStat.st_ino == rhsStat.st_ino;

		strnzcpy(lhsExpanded, lhs, sizeof(lhsExpanded));
		strnzcpy(rhsExpanded, rhs, sizeof(rhsExpanded));
		fexpand(lhsExpanded);
		fexpand(rhsExpanded);
		for (i = 0; lhsExpanded[i] != EOS; ++i)
			if (lhsExpanded[i] == '\\')
				lhsExpanded[i] = '/';
		for (i = 0; rhsExpanded[i] != EOS; ++i)
			if (rhsExpanded[i] == '\\')
				rhsExpanded[i] = '/';
		return std::strcmp(lhsExpanded, rhsExpanded) == 0;
	}

	bool confirmOverwriteForSaveAs(const char *targetPath) const {
		if (!pathIsRegularFile(targetPath))
			return true;
		return mr::dialogs::showUnsavedChangesDialog("Overwrite", "Target file exists. Overwrite?",
		                                             targetPath) == mr::dialogs::UnsavedChangesChoice::Save;
	}

	std::size_t lineStartForIndex(std::size_t index) const noexcept {
		return mBufferModel.lineStartByIndex(index);
	}

	int longestLineWidth() const noexcept {
		std::size_t pos = 0;
		std::size_t len = mBufferModel.length();
		int maxWidth = 1;
		int tabSize = configuredTabSize();

		while (true) {
			int width = displayWidthForText(mBufferModel.lineText(pos), tabSize);
			maxWidth = std::max(maxWidth, width + 1);
			if (pos >= len)
				break;
			std::size_t next = mBufferModel.nextLine(pos);
			if (next <= pos)
				break;
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
		std::size_t limitValue = std::max<std::size_t>(
		    estimated,
		    std::max<std::size_t>(currentLine + margin,
		                          static_cast<std::size_t>(std::max(delta.y, 0)) + margin));
		limitValue = std::max<std::size_t>(limitValue, minimum);
		return static_cast<int>(std::min<std::size_t>(limitValue, static_cast<std::size_t>(INT_MAX)));
	}

	int dynamicLargeFileWidthLimit() const {
		const std::size_t cursor = mBufferModel.cursor();
		const int cursorColumn = charColumn(mBufferModel.lineStart(cursor), cursor);
		const int viewportWidth = textViewportWidth();
		return std::max(std::max(viewportWidth, 256),
		                std::max(delta.x + viewportWidth + 64, cursorColumn + 64));
	}

	void scheduleLineIndexWarmupIfNeeded() {
		if (!mBufferModel.document().hasMappedOriginal() || mBufferModel.document().exactLineCountKnown()) {
			std::uint64_t cancelledTaskId = mLineIndexWarmupTaskId;
			bool hadTask = cancelledTaskId != 0;
			mLineIndexWarmupTaskId = 0;
			mLineIndexWarmupDocumentId = 0;
			mLineIndexWarmupVersion = 0;
			if (hadTask) {
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
				notifyWindowTaskStateChanged();
			}
			return;
		}

			const std::size_t docId = mBufferModel.documentId();
			const std::size_t version = mBufferModel.version();
			if (mLineIndexWarmupTaskId != 0 && mLineIndexWarmupDocumentId == docId &&
			    mLineIndexWarmupVersion == version)
				return;

			MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
			std::uint64_t previousTaskId = mLineIndexWarmupTaskId;
			if (previousTaskId != 0)
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
			mLineIndexWarmupDocumentId = docId;
			mLineIndexWarmupVersion = version;
			mLineIndexWarmupTaskId = mr::coprocessor::globalCoprocessor().submit(
			    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::LineIndexWarmup, docId, version,
			    lineIndexWarmupTaskLabel(),
			    [snapshot](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
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
				    result.payload =
				        std::make_shared<mr::coprocessor::LineIndexWarmupPayload>(warmup);
				    return result;
			    });
			if (mLineIndexWarmupTaskId != previousTaskId)
				notifyWindowTaskStateChanged();
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
		if (lineStarts.empty())
			return;

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

		if (mSyntaxWarmupTaskId != 0 && mSyntaxWarmupDocumentId == docId &&
		    mSyntaxWarmupVersion == version && mSyntaxWarmupLanguage == language &&
		    topLine >= mSyntaxWarmupTopLine && bottomLine <= mSyntaxWarmupBottomLine)
			return;

		MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
		std::uint64_t previousTaskId = mSyntaxWarmupTaskId;
		if (previousTaskId != 0)
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
		mSyntaxWarmupDocumentId = docId;
		mSyntaxWarmupVersion = version;
		mSyntaxWarmupTopLine = topLine;
		mSyntaxWarmupBottomLine = bottomLine;
		mSyntaxWarmupLanguage = language;
		mSyntaxWarmupTaskId = mr::coprocessor::globalCoprocessor().submit(
		    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::SyntaxWarmup, docId, version,
		    syntaxWarmupTaskLabel(),
		    [snapshot, language, lineStarts](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
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
				    warmed.push_back(mr::coprocessor::SyntaxWarmLine(
				        lineStarts[i],
				        tmrBuildTokenMapForTextLine(language, snapshot.lineText(lineStarts[i]))));
			    }
			    result.status = mr::coprocessor::TaskStatus::Completed;
			    result.payload =
			        std::make_shared<mr::coprocessor::SyntaxWarmupPayload>(language, std::move(warmed));
			    return result;
		    });
		if (mSyntaxWarmupTaskId != previousTaskId)
			notifyWindowTaskStateChanged();
	}

	bool resolveSaveOptionsForPath(const char *path, MRTextSaveOptions &options,
	                               std::size_t *optionsHash = nullptr) const {
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
		if (sourceBytes == 0 || runMicros <= 0.0)
			return;
		const double sampleBytesPerMicro =
		    static_cast<double>(sourceBytes) / std::max(1.0, runMicros);
		if (mSaveNormalizationThroughputBytesPerMicro <= 0.0)
			mSaveNormalizationThroughputBytesPerMicro = sampleBytesPerMicro;
		else
			mSaveNormalizationThroughputBytesPerMicro =
			    mSaveNormalizationThroughputBytesPerMicro * 0.75 + sampleBytesPerMicro * 0.25;
		++mSaveNormalizationThroughputSamples;
	}

	void scheduleSaveNormalizationWarmupIfNeeded() {
		invalidateSaveNormalizationCache();
		if (mSaveNormalizationWarmupTaskId == 0)
			return;
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

		if (useApproximateLargeFileMetrics()) {
			limitX = dynamicLargeFileWidthLimit();
			limitY = dynamicLargeFileLineLimit();
		} else {
			limitX = longestLineWidth();
			limitY = std::max<int>(1, static_cast<int>(mBufferModel.lineCount()));
		}

		int maxX = std::max(0, limitX - viewportWidth);
		int maxY = std::max(0, limitY - textRows);
		int newDeltaX = std::min(std::max(delta.x, 0), maxX);
		int newDeltaY = std::min(std::max(delta.y, 0), maxY);

		setLimit(limitX + gutterWidth + rightInset, limitY);
		if (vScrollBar != nullptr)
			vScrollBar->setParams(vScrollBar->value, 0, std::max(0, limitY - textRows), std::max(0, textRows - 1),
			                      vScrollBar->arStep);
		if (newDeltaX != delta.x || newDeltaY != delta.y)
			scrollTo(newDeltaX, newDeltaY);
	}

	void updateIndicator() {
		if (mIndicatorUpdateInProgress)
			return;
		mIndicatorUpdateInProgress = true;
		TextViewportGeometry viewport = textViewportGeometry();
		std::size_t cursor = mBufferModel.cursor();
		unsigned long visualColumn =
		    static_cast<unsigned long>(charColumn(mBufferModel.lineStart(cursor), cursor));
		unsigned long line = static_cast<unsigned long>(mBufferModel.lineIndex(cursor));
		long long localX = viewport.localXFromVisualColumn(static_cast<long long>(visualColumn));
		long long localY = static_cast<long long>(line) - delta.y;

		if (mIndicator != nullptr) {
			if (auto *mrIndicator = dynamic_cast<MRIndicator *>(mIndicator))
				mrIndicator->setDisplayValue(visualColumn, line, mBufferModel.isModified() ? True : False);
			else {
				TPoint location = {
				    short(visualColumn > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : visualColumn),
				    short(line > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : line)};
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
		std::size_t cursor = mBufferModel.cursor();
		int visualColumn = charColumn(mBufferModel.lineStart(cursor), cursor);
		int line = static_cast<int>(mBufferModel.lineIndex(cursor));
		int targetX = delta.x;
		int targetY = delta.y;
		int viewportWidth = textViewportWidth();
		int textRows = std::max(1, visibleTextRows());

		if (visualColumn < targetX)
			targetX = visualColumn;
		else if (visualColumn >= targetX + viewportWidth)
			targetX = visualColumn - viewportWidth + 1;

		if (centerCursor)
			targetY = std::max(0, line - textRows / 2);
		else if (line < targetY)
			targetY = line;
		else if (line >= targetY + textRows)
			targetY = line - textRows + 1;

		scrollTo(targetX, targetY);
	}

	void moveCursor(std::size_t target, bool extendSelection, bool centerCursor) {
		target = canonicalCursorOffset(std::min(target, mBufferModel.length()));
		if (extendSelection) {
			std::size_t anchor =
			    mBufferModel.hasSelection() ? mBufferModel.selection().anchor : mBufferModel.cursor();
			mSelectionAnchor = anchor;
			mBufferModel.setCursorAndSelection(target, anchor, target);
		} else {
			if (configuredPersistentBlocksSetting() && mBufferModel.hasSelection())
				mBufferModel.setCursor(target);
			else
				mBufferModel.setCursorAndSelection(target, target, target);
			mSelectionAnchor = target;
		}
		if (useApproximateLargeFileMetrics())
			updateMetrics();
		ensureCursorVisible(centerCursor);
		scheduleSyntaxWarmupIfNeeded();
		updateIndicator();
		drawView();
	}

		bool isTextInputEvent(const TEvent &event) const {
			if (event.what != evKeyDown)
				return false;
			const ushort mods = event.keyDown.controlKeyState;
			const bool plainTab =
			    (event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) &&
			    (mods & (kbShift | kbCtrlShift | kbAltShift | kbPaste)) == 0;
			return (event.keyDown.controlKeyState & kbPaste) != 0 || event.keyDown.textLength > 0 ||
			       plainTab ||
			       (event.keyDown.charScan.charCode >= 32 && event.keyDown.charScan.charCode < 255);
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
			clearEvent(event);
			return;
		}

			const ushort mods = event.keyDown.controlKeyState;
			const bool plainTab =
			    (event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) &&
			    (mods & (kbShift | kbCtrlShift | kbAltShift | kbPaste)) == 0;

		if (event.keyDown.textLength > 0)
			insertBufferText(std::string(event.keyDown.text, event.keyDown.textLength));
		else if (plainTab)
			insertBufferText(tabKeyText());
		else
			insertBufferText(std::string(1, static_cast<char>(event.keyDown.charScan.charCode)));
		clearEvent(event);
	}

	std::string tabKeyText() const {
		if (configuredTabExpandSetting())
			return "\t";
		std::size_t insertPos = mBufferModel.cursor();
		if (mBufferModel.hasSelection())
			insertPos = mBufferModel.selection().range().start;
		int visualColumn = charColumn(mBufferModel.lineStart(insertPos), insertPos);
		return std::string(static_cast<std::size_t>(tabDisplayWidth(visualColumn, configuredTabSize())), ' ');
	}

	void handleKeyDown(TEvent &event) {
			ushort key = ctrlToArrow(event.keyDown.keyCode);
			const ushort mods = event.keyDown.controlKeyState;
			bool extend = hasShiftModifier(mods);
			const bool shiftTabPressed =
			    event.keyDown.keyCode == kbShiftTab ||
			    ((event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) &&
			     hasShiftModifier(mods));

		if (shiftTabPressed) {
			moveCursor(tabStopMoveOffset(cursorOffset(), false), false, false);
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
				moveCursor(lineMoveOffset(cursorOffset(), -1), extend, false);
				break;
			case kbDown:
				moveCursor(lineMoveOffset(cursorOffset(), 1), extend, false);
				break;
			case kbHome:
				moveCursor(mAutoIndent ? charPtrOffset(lineStartOffset(cursorOffset()), 0)
				                      : lineStartOffset(cursorOffset()),
				           extend, false);
				break;
			case kbEnd:
				moveCursor(lineEndOffset(cursorOffset()), extend, false);
				break;
			case kbPgUp:
				moveCursor(lineMoveOffset(cursorOffset(), -(std::max(2, visibleTextRows()) - 1)), extend, true);
				break;
			case kbPgDn:
				moveCursor(lineMoveOffset(cursorOffset(), std::max(2, visibleTextRows()) - 1), extend, true);
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
				if (!mReadOnly)
					newLineWithIndent("");
				clearEvent(event);
				return;
			case kbBack:
				if (!mReadOnly) {
					if (mBufferModel.hasSelection())
						replaceSelectionText(std::string());
					else if (cursorOffset() > 0)
						replaceRangeAndSelect(static_cast<uint>(prevCharOffset(cursorOffset())),
						                      static_cast<uint>(cursorOffset()), "", 0);
				}
				clearEvent(event);
				return;
			case kbDel:
				if (!mReadOnly) {
					if (mBufferModel.hasSelection())
						replaceSelectionText(std::string());
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
				pasteClipboard();
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
				pasteClipboard();
				break;
			case cmMrEditUndo: {
				MRTextBufferModel::CustomUndoRecord record;
				if (mBufferModel.undo(&record)) {
					const bool modifiedState = mBufferModel.isModified();
					adoptCommittedDocument(mBufferModel.document(), mBufferModel.cursor(),
					                       mBufferModel.selectionStart(), mBufferModel.selectionEnd(),
					                       modifiedState);
					if (owner != nullptr) {
						setBlockOverlayState(record.blockMode, record.blockAnchor,
						                     record.blockEnd, record.blockMarkingOn, false);
					}
				}
				break;
			}
			case cmMrEditRedo: {
				MRTextBufferModel::CustomUndoRecord record;
				if (mBufferModel.redo(&record)) {
					const bool modifiedState = mBufferModel.isModified();
					adoptCommittedDocument(mBufferModel.document(), mBufferModel.cursor(),
					                       mBufferModel.selectionStart(), mBufferModel.selectionEnd(),
					                       modifiedState);
					if (owner != nullptr) {
						setBlockOverlayState(record.blockMode, record.blockAnchor,
						                     record.blockEnd, record.blockMarkingOn, false);
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
			case cmMrTextReformatParagraph:
				if (!mReadOnly) {
					int margin = configuredEditSetupSettings().rightMargin;
					formatParagraph(margin > 0 ? margin : 78);
				}
				break;
			case cmClear:
				if (!mReadOnly)
					replaceSelectionText(std::string());
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
				moveCursor(lineMoveOffset(cursorOffset(), -1), false, false);
				break;
			case cmLineDown:
				moveCursor(lineMoveOffset(cursorOffset(), 1), false, false);
				break;
			case cmPageUp:
				moveCursor(lineMoveOffset(cursorOffset(), -(std::max(2, visibleTextRows()) - 1)), false, true);
				break;
			case cmPageDown:
				moveCursor(lineMoveOffset(cursorOffset(), std::max(2, visibleTextRows()) - 1), false, true);
				break;
			case cmTextStart:
				moveCursor(0, false, true);
				break;
			case cmTextEnd:
				moveCursor(bufferLength(), false, true);
				break;
			case cmNewLine:
				if (!mReadOnly)
					newLineWithIndent("");
				break;
			case cmBackSpace:
				if (!mReadOnly) {
					if (mBufferModel.hasSelection())
						replaceSelectionText(std::string());
					else if (cursorOffset() > 0)
						replaceRangeAndSelect(static_cast<uint>(prevCharOffset(cursorOffset())),
						                      static_cast<uint>(cursorOffset()), "", 0);
				}
				break;
			case cmDelChar:
				if (!mReadOnly) {
					if (mBufferModel.hasSelection())
						replaceSelectionText(std::string());
					else
						deleteCharsAtCursor(1);
				}
				break;
			case cmDelWord:
				if (!mReadOnly)
					replaceRangeAndSelect(static_cast<uint>(cursorOffset()),
					                      static_cast<uint>(nextWordOffset(cursorOffset())), "", 0);
				break;
			case cmDelWordLeft:
				if (!mReadOnly)
					replaceRangeAndSelect(static_cast<uint>(prevWordOffset(cursorOffset())),
					                      static_cast<uint>(cursorOffset()), "", 0);
				break;
			case cmDelStart:
				if (!mReadOnly)
					replaceRangeAndSelect(static_cast<uint>(lineStartOffset(cursorOffset())),
					                      static_cast<uint>(cursorOffset()), "", 0);
				break;
			case cmDelEnd:
				if (!mReadOnly)
					replaceRangeAndSelect(static_cast<uint>(cursorOffset()),
					                      static_cast<uint>(lineEndOffset(cursorOffset())), "", 0);
				break;
			case cmDelLine:
				if (!mReadOnly)
					deleteCurrentLineText();
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
		if ((event.mouse.buttons & mbLeftButton) == 0)
			return;

		select();
		std::size_t anchor =
		    (event.mouse.controlKeyState & kbShift) != 0 && mBufferModel.hasSelection()
		        ? mBufferModel.selection().anchor
		        : mBufferModel.cursor();
		mSelectionAnchor = anchor;
		moveCursor(mouseOffset(makeLocal(event.mouse.where)),
		           (event.mouse.controlKeyState & kbShift) != 0, false);

		while (mouseEvent(event, evMouseMove | evMouseAuto)) {
			if (event.what == evMouseAuto) {
				TPoint mouse = makeLocal(event.mouse.where);
				int dx = delta.x;
				int dy = delta.y;
				if (mouse.x < 0)
					--dx;
				else if (mouse.x >= size.x)
					++dx;
				if (mouse.y < 0)
					--dy;
				else if (mouse.y >= std::max(0, visibleTextRows()))
					++dy;
				scrollTo(std::max(dx, 0), std::max(dy, 0));
			}
			std::size_t target = mouseOffset(makeLocal(event.mouse.where));
			mBufferModel.setCursorAndSelection(target, mSelectionAnchor, target);
			updateIndicator();
			drawView();
		}
		clearEvent(event);
	}

	std::size_t mouseOffset(TPoint local) noexcept {
		TextViewportGeometry viewport = textViewportGeometry();
		const int textRows = std::max(1, visibleTextRows());
		int clampedY = std::max(0, std::min(local.y, textRows - 1));
		int row = clampedY + delta.y;
		int column = viewport.textColumnFromLocalX(local.x);
		std::size_t start = lineStartForIndex(static_cast<std::size_t>(std::max(row, 0)));
		return canonicalCursorOffset(charPtrOffset(start, column));
	}

	std::size_t canonicalCursorOffset(std::size_t pos) const noexcept {
		pos = std::min(pos, mBufferModel.length());
		if (pos > 0 && pos < mBufferModel.length() && mBufferModel.charAt(pos) == '\n' &&
		    mBufferModel.charAt(pos - 1) == '\r')
			return pos - 1;
		return pos;
	}

	void copySelection() {
		if (!mBufferModel.hasSelection())
			return;
		MRTextBufferModel::Range range = mBufferModel.selection().range();
		clipboardText() = mBufferModel.text().substr(range.start, range.length());
	}

	void cutSelection() {
		if (mReadOnly || !mBufferModel.hasSelection())
			return;
		copySelection();
		replaceSelectionText(std::string());
	}

	void pasteClipboard() {
		if (mReadOnly || clipboardText().empty())
			return;
		insertBufferText(clipboardText());
	}

	void replaceSelectionText(const std::string &text) {
		if (!mBufferModel.hasSelection()) {
			if (!text.empty())
				insertBufferText(text);
			return;
		}
		MRTextBufferModel::Range range = mBufferModel.selection().range();
		replaceRangeAndSelect(static_cast<uint>(range.start), static_cast<uint>(range.end), text.data(),
		                      static_cast<uint>(text.size()));
	}

	void convertSelectionToUpperCase() {
		if (mReadOnly || !mBufferModel.hasSelection())
			return;
		MRTextBufferModel::Range range = mBufferModel.selection().range();
		std::string text = mBufferModel.text().substr(range.start, range.length());
		for (char &c : text)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		replaceSelectionText(text);
		setSelectionOffsets(range.start, range.start + text.length());
	}

	void convertSelectionToLowerCase() {
		if (mReadOnly || !mBufferModel.hasSelection())
			return;
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
			if (!persistentName.empty() && upperAscii(persistentName) != "?NO-FILE?")
				detail = persistentName.c_str();
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

		if (selected)
			return selectedAttr;

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
		if (mBufferModel.language() != oldLanguage)
			resetSyntaxWarmupState(true);
	}

	void resetSyntaxWarmupState(bool clearCache) noexcept {
		std::uint64_t cancelledTaskId = mSyntaxWarmupTaskId;
		bool hadTask = cancelledTaskId != 0;
		if (clearCache)
			mSyntaxTokenCache.clear();
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
		if (rowCount <= 0)
			return lineStarts;

		std::size_t lineStart = lineStartForIndex(topLine);
		for (int i = 0; i < rowCount; ++i) {
			lineStarts.push_back(lineStart);
			if (lineStart >= mBufferModel.length())
				break;
			std::size_t next = mBufferModel.nextLine(lineStart);
			if (next <= lineStart)
				break;
			lineStart = next;
		}
		return lineStarts;
	}

	bool hasSyntaxTokensForLineStarts(const std::vector<std::size_t> &lineStarts) const {
		for (std::size_t i = 0; i < lineStarts.size(); ++i)
			if (mSyntaxTokenCache.find(lineStarts[i]) == mSyntaxTokenCache.end())
				return false;
		return true;
	}

	MRSyntaxTokenMap syntaxTokensForLine(std::size_t lineStart) const {
		std::map<std::size_t, MRSyntaxTokenMap>::const_iterator found = mSyntaxTokenCache.find(lineStart);
		if (found != mSyntaxTokenCache.end())
			return found->second;
		return mBufferModel.tokenMapForLine(lineStart);
	}

	void formatSyntaxLine(TDrawBuffer &b, std::size_t lineStart, int hScroll, int width, int drawX,
	                      bool isDocumentLine, bool drawEofMarker, bool drawEofMarkerAsEmoji) {
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
		int tabSize = configuredTabSize();
		const bool displayTabs = configuredDisplayTabs();

		hScroll = std::max(hScroll, 0);
		width = std::max(width, 0);
		drawX = std::max(drawX, 0);
		if (!isDocumentLine) {
			TColorAttr color = tokenColor(MRSyntaxToken::Text, false, basePair);
			b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
			if (drawEofMarker)
				drawEofMarkerGlyph(b, hScroll, width, drawX, basePair, drawEofMarkerAsEmoji);
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
			const std::size_t trackedEnd =
			    mBlockOverlayTrackingCursor ? std::min(mBufferModel.cursor(), documentLength)
			                                : std::min(mBlockOverlayEnd, documentLength);
			const std::size_t trackedAnchor = std::min(mBlockOverlayAnchor, documentLength);
			overlayMode = mBlockOverlayMode;
			overlayStart = std::min(trackedAnchor, trackedEnd);
			overlayEnd = std::max(trackedAnchor, trackedEnd);
			if (overlayMode == 1 || overlayMode == 2) {
				overlayLine1 = std::min(mBufferModel.lineIndex(trackedAnchor),
				                        mBufferModel.lineIndex(trackedEnd));
				overlayLine2 = std::max(mBufferModel.lineIndex(trackedAnchor),
				                        mBufferModel.lineIndex(trackedEnd));
			}
			if (overlayMode == 2) {
				const std::size_t aLineStart = mBufferModel.lineStart(trackedAnchor);
				const std::size_t bLineStart = mBufferModel.lineStart(trackedEnd);
				const int aCol = charColumn(aLineStart, trackedAnchor);
				const int bCol = charColumn(bLineStart, trackedEnd);
				overlayCol1 = std::min(aCol, bCol);
				overlayCol2Exclusive = std::max(aCol, bCol);
				if (overlayCol2Exclusive <= overlayCol1)
					overlayCol2Exclusive = overlayCol1 + 1;
			}
		}
		currentLine = (lineStart <= cursorPos && cursorPos < lineEnd) ||
		              (cursorPos == documentLength && lineStart == cursorPos && lineEnd == cursorPos);
		if (overlayActive) {
			if (overlayMode == 3)
				currentLineInBlock = currentLine && overlayStart < overlayEnd && overlayStart < lineEnd &&
				                     overlayEnd > lineStart;
			else
				currentLineInBlock = currentLine && overlayLine1 <= lineIndex && lineIndex <= overlayLine2;
		} else
			currentLineInBlock = currentLine && selection.start < selection.end && selection.start < lineEnd &&
			                     selection.end > lineStart;
		if (currentLineInBlock)
			basePair = getColor(0x0204);
		else if (currentLine)
			basePair = getColor(0x0303);

		while (bytePos < line.size() && x < width) {
			std::size_t next = bytePos;
			std::size_t charWidth = 0;
			if (!nextDisplayChar(line, next, charWidth, visual, tabSize))
				break;

			int nextVisual = visual + static_cast<int>(charWidth);
			if (nextVisual > hScroll) {
				std::size_t tokenIndex = bytePos;
				std::size_t documentPos = lineStart + bytePos;
				MRSyntaxToken token =
				    tokenIndex < tokens.size() ? tokens[tokenIndex] : MRSyntaxToken::Text;
				bool selected = false;
				TAttrPair tokenPair;
				TColorAttr color;
				int visibleWidth = 0;

				if (overlayActive) {
					if (overlayMode == 3)
						selected = overlayStart <= documentPos && documentPos < overlayEnd;
					else if (overlayMode == 1)
						selected = overlayLine1 <= lineIndex && lineIndex <= overlayLine2;
					else if (overlayMode == 2)
						selected = overlayLine1 <= lineIndex && lineIndex <= overlayLine2 &&
						           visual < overlayCol2Exclusive && nextVisual > overlayCol1;
				} else
					selected = selection.start <= documentPos && documentPos < selection.end;
				bool changedChar = !currentLine && !currentLineInBlock && isDirtyOffset(documentPos);
				TAttrPair effectivePair = changedChar ? changedPair : basePair;
				tokenPair = selected ? selectionPair : effectivePair;
				color = tokenColor(token, selected, tokenPair);
				visibleWidth = nextVisual - std::max(visual, hScroll);

				if (line[bytePos] == '\t' && displayTabs && visual >= hScroll && visibleWidth > 0) {
					b.moveStr(static_cast<ushort>(drawX + x), "\xE2\x96\xB6", color, 1); // U+25B6
					if (visibleWidth > 1)
						b.moveChar(static_cast<ushort>(drawX + x + 1), ' ', color,
						           static_cast<ushort>(visibleWidth - 1));
				} else if (line[bytePos] == '\t' || visual < hScroll)
					b.moveChar(static_cast<ushort>(drawX + x), ' ', color, static_cast<ushort>(visibleWidth));
				else
					b.moveStr(static_cast<ushort>(drawX + x), line.substr(bytePos, next - bytePos), color,
					          static_cast<ushort>(visibleWidth));
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

	void drawEofMarkerGlyph(TDrawBuffer &b, int hScroll, int width, int drawX, TAttrPair basePair,
	                        bool drawEmoji) {
		static const char *const kEofMarkerText = "EOF";
		static const char *const kEofMarkerEmoji = "\xF0\x9F\x94\x9A";
		const char *marker = drawEmoji ? kEofMarkerEmoji : kEofMarkerText;
		int markerWidth = 0;
		TColorAttr markerColor = tokenColor(MRSyntaxToken::Text, false, basePair);
		unsigned char configuredMarkerColor = 0;

		if (width <= 0 || hScroll != 0)
			return;
		if (!drawEmoji && mCustomWindowEofMarkerColorOverrideValid)
			markerColor = mCustomWindowEofMarkerColorOverride;
		else if (!drawEmoji && configuredColorSlotOverride(kMrPaletteEofMarker, configuredMarkerColor))
			markerColor = static_cast<TColorAttr>(configuredMarkerColor);
		markerWidth = std::max(1, strwidth(marker));
		markerWidth = std::min(markerWidth, width);
		b.moveStr(static_cast<ushort>(drawX), marker, markerColor, static_cast<ushort>(markerWidth));
	}

	bool adoptCommittedDocument(const MRTextBufferModel::Document &document, std::size_t cursorPos,
	                            std::size_t selStart, std::size_t selEnd, bool modifiedState,
	                            const MRTextBufferModel::DocumentChangeSet *changeSet = nullptr) {
		cursorPos = std::min(cursorPos, document.length());
		selStart = std::min(selStart, document.length());
		selEnd = std::min(selEnd, document.length());
		if (selEnd < selStart)
			std::swap(selStart, selEnd);

		mBufferModel.document() = document;
		invalidateSaveNormalizationCache();
		resetSyntaxWarmupState(true);
		invalidateMiniMapCache(false);
		refreshSyntaxContext();
		cursorPos = canonicalCursorOffset(cursorPos);
		selStart = canonicalCursorOffset(selStart);
		selEnd = canonicalCursorOffset(selEnd);
		mBufferModel.setCursorAndSelection(cursorPos, selStart, selEnd);
		mBufferModel.setModified(modifiedState);
		if (changeSet == nullptr || changeSet->changed)
			mFindMarkerRanges.clear();
		if (!modifiedState)
			clearDirtyRanges();
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
	std::vector<MRTextBufferModel::Range> mFindMarkerRanges;
	std::vector<MRTextBufferModel::Range> mDirtyRanges;
	LoadTiming mLastLoadTiming;

	void clearDirtyRanges() noexcept {
		mDirtyRanges.clear();
	}

	static void normalizePairRangeList(std::vector<std::pair<std::size_t, std::size_t>> &ranges) {
		std::sort(ranges.begin(), ranges.end(),
		          [](const std::pair<std::size_t, std::size_t> &a,
		             const std::pair<std::size_t, std::size_t> &b) {
			          return a.first < b.first || (a.first == b.first && a.second < b.second);
		          });
		std::vector<std::pair<std::size_t, std::size_t>> merged;
		for (const auto &item : ranges) {
			if (item.second <= item.first)
				continue;
			if (merged.empty() || item.first > merged.back().second)
				merged.push_back(item);
			else if (item.second > merged.back().second)
				merged.back().second = item.second;
		}
		ranges.swap(merged);
	}

	static void normalizeRangeList(std::vector<MRTextBufferModel::Range> &ranges) {
		std::sort(ranges.begin(), ranges.end(),
		          [](const MRTextBufferModel::Range &a, const MRTextBufferModel::Range &b) {
			          return a.start < b.start || (a.start == b.start && a.end < b.end);
		          });
		std::vector<MRTextBufferModel::Range> merged;
		for (const MRTextBufferModel::Range &item : ranges) {
			if (item.end <= item.start)
				continue;
			if (merged.empty() || item.start > merged.back().end)
				merged.push_back(item);
			else if (item.end > merged.back().end)
				merged.back().end = item.end;
		}
		ranges.swap(merged);
	}

	void normalizeDirtyRanges() {
		normalizeRangeList(mDirtyRanges);
	}

	void pushMappedDirtyRange(std::vector<MRTextBufferModel::Range> &mapped, std::size_t start,
	                          std::size_t end, std::size_t maxLength) {
		start = std::min(start, maxLength);
		end = std::min(end, maxLength);
		if (end <= start)
			return;
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

		if (mDirtyRanges.empty())
			return;
		if (delta >= 0) {
			const std::size_t deltaUnsigned = static_cast<std::size_t>(delta);
			replacedOldLength = touchedLength > deltaUnsigned ? touchedLength - deltaUnsigned : 0;
		}
		if (replacedOldLength > oldLength - editStart)
			replacedOldLength = oldLength - editStart;
		const std::size_t oldEditEnd = editStart + replacedOldLength;

		std::vector<MRTextBufferModel::Range> mapped;
		mapped.reserve(mDirtyRanges.size() + 2);

		for (std::size_t i = 0; i < mDirtyRanges.size(); ++i) {
			MRTextBufferModel::Range range = mDirtyRanges[i].clamped(oldLength).normalized();

			if (range.end <= range.start)
				continue;
			if (range.end <= editStart) {
				pushMappedDirtyRange(mapped, range.start, range.end, newLength);
				continue;
			}
			if (range.start >= oldEditEnd) {
				const long long shiftedStart = static_cast<long long>(range.start) + delta;
				const long long shiftedEnd = static_cast<long long>(range.end) + delta;
				if (shiftedEnd <= 0)
					continue;
				pushMappedDirtyRange(mapped, static_cast<std::size_t>(std::max<long long>(0, shiftedStart)),
				                     static_cast<std::size_t>(std::max<long long>(0, shiftedEnd)), newLength);
				continue;
			}

			if (range.start < editStart)
				pushMappedDirtyRange(mapped, range.start, editStart, newLength);
			if (range.end > oldEditEnd) {
				const long long shiftedStart = static_cast<long long>(oldEditEnd) + delta;
				const long long shiftedEnd = static_cast<long long>(range.end) + delta;
				if (shiftedEnd > 0)
					pushMappedDirtyRange(mapped,
					                     static_cast<std::size_t>(std::max<long long>(0, shiftedStart)),
					                     static_cast<std::size_t>(std::max<long long>(0, shiftedEnd)), newLength);
			}
		}

		mDirtyRanges.swap(mapped);
		normalizeDirtyRanges();
	}

	void addDirtyRange(MRTextBufferModel::Range range) {
		if (mBufferModel.length() == 0)
			return;
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
		if (mDirtyRanges.empty() || mBufferModel.length() == 0)
			return false;
		if (pos >= mBufferModel.length())
			return false;
		for (const MRTextBufferModel::Range &item : mDirtyRanges) {
			if (item.end <= pos)
				continue;
			if (item.start > pos)
				break;
			return pos < item.end;
		}
		return false;
	}
};

#endif
