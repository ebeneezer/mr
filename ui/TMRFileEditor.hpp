#ifndef TMRFILEEDITOR_HPP
#define TMRFILEEDITOR_HPP

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
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "MRCoprocessor.hpp"
#include "MRUnsavedChangesDialog.hpp"
#include "TMRIndicator.hpp"
#include "TMRTextBufferModel.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../app/MRCommands.hpp"

class TMRFileEditor : public TScroller {
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

	TMRFileEditor(const TRect &bounds, TScrollBar *aHScrollBar, TScrollBar *aVScrollBar,
	              TIndicator *aIndicator, TStringView aFileName) noexcept
	    : TScroller(bounds, aHScrollBar, aVScrollBar), indicator_(aIndicator), readOnly_(false),
	      insertMode_(true), autoIndent_(false), syntaxTitleHint_(), bufferModel_(), delCount_(0),
	      insCount_(0), selectionAnchor_(0), indicatorUpdateInProgress_(false),
	      lineIndexWarmupTaskId_(0), lineIndexWarmupDocumentId_(0), lineIndexWarmupVersion_(0),
		      syntaxTokenCache_(), syntaxWarmupTaskId_(0), syntaxWarmupDocumentId_(0),
			      syntaxWarmupVersion_(0), syntaxWarmupTopLine_(0), syntaxWarmupBottomLine_(0),
				      syntaxWarmupLanguage_(TMRSyntaxLanguage::PlainText), miniMapWarmupTaskId_(0),
				      miniMapWarmupDocumentId_(0), miniMapWarmupVersion_(0), miniMapWarmupRows_(0),
				      miniMapWarmupBodyWidth_(0), miniMapWarmupViewportWidth_(0), miniMapWarmupBraille_(true),
				      miniMapWarmupWindowStartLine_(0), miniMapWarmupWindowLineCount_(0),
				      miniMapWarmupReschedulePending_(false), miniMapCache_(),
				      miniMapInitialRenderReportedDocumentId_(0), lastLoadTiming_() {
		fileName[0] = EOS;
		options |= ofFirstClick;
		eventMask |= evMouse | evKeyboard | evCommand;
		if (!aFileName.empty())
			setPersistentFileName(aFileName);
		syncFromEditorState(false);
	}

	bool isReadOnly() const {
		return readOnly_;
	}

	void setWindowEofMarkerColorOverride(bool enabled, TColorAttr color = 0) {
		customWindowEofMarkerColorOverrideValid_ = enabled;
		customWindowEofMarkerColorOverride_ = color;
		drawView();
	}

	void setReadOnly(bool readOnly) {
		if (readOnly_ != readOnly) {
			readOnly_ = readOnly;
			if (readOnly_)
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
	}

	void clearPersistentFileName() noexcept {
		fileName[0] = EOS;
		refreshSyntaxContext();
	}

	bool isDocumentModified() const noexcept {
		return bufferModel_.isModified();
	}

	void setDocumentModified(bool changed) {
		bufferModel_.setModified(changed);
		if (!changed) {
			delCount_ = 0;
			insCount_ = 0;
			clearDirtyRanges();
		}
		syncFromEditorState(false);
	}

	bool hasUndoHistoryState() const noexcept {
		return delCount_ != 0 || insCount_ != 0;
	}

	void resetUndoState() noexcept {
		delCount_ = 0;
		insCount_ = 0;
	}

	bool insertModeEnabled() const noexcept {
		return insertMode_;
	}

	std::size_t originalBufferLength() const noexcept {
		return bufferModel_.document().originalLength();
	}

	std::size_t addBufferLength() const noexcept {
		return bufferModel_.document().addBufferLength();
	}

	std::size_t pieceCount() const noexcept {
		return bufferModel_.document().pieceCount();
	}

	bool hasMappedOriginalSource() const noexcept {
		return bufferModel_.document().hasMappedOriginal();
	}

	const std::string &mappedOriginalPath() const noexcept {
		return bufferModel_.document().mappedPath();
	}

	std::size_t estimatedLineCount() const noexcept {
		return bufferModel_.estimatedLineCount();
	}

	bool exactLineCountKnown() const noexcept {
		return bufferModel_.exactLineCountKnown();
	}

	std::size_t selectionLength() const noexcept {
		return bufferModel_.selection().range().length();
	}

	std::uint64_t pendingLineIndexWarmupTaskId() const noexcept {
		return lineIndexWarmupTaskId_;
	}

	std::uint64_t pendingSyntaxWarmupTaskId() const noexcept {
		return syntaxWarmupTaskId_;
	}

	std::uint64_t pendingMiniMapWarmupTaskId() const noexcept {
		return miniMapWarmupTaskId_;
	}

	bool shouldReportMiniMapInitialRender() const noexcept {
		return miniMapInitialRenderReportedDocumentId_ != bufferModel_.documentId();
	}

	void markMiniMapInitialRenderReported() noexcept {
		miniMapInitialRenderReportedDocumentId_ = bufferModel_.documentId();
	}

	bool lineIndexWarmupPending() const noexcept {
		return lineIndexWarmupTaskId_ != 0;
	}

	bool syntaxWarmupPending() const noexcept {
		return syntaxWarmupTaskId_ != 0;
	}

	bool miniMapWarmupPending() const noexcept {
		return miniMapWarmupTaskId_ != 0;
	}

	bool usesApproximateMetrics() const noexcept {
		return useApproximateLargeFileMetrics();
	}

	void setInsertModeEnabled(bool on) {
		if (insertMode_ == on)
			return;
		insertMode_ = on;
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
		return bufferModel_.cursor();
	}

	std::size_t bufferLength() const noexcept {
		return bufferModel_.length();
	}

	std::size_t selectionStartOffset() const noexcept {
		return bufferModel_.selectionStart();
	}

	std::size_t selectionEndOffset() const noexcept {
		return bufferModel_.selectionEnd();
	}

	bool hasTextSelection() const noexcept {
		return bufferModel_.hasSelection();
	}

	std::size_t lineStartOffset(std::size_t pos) const noexcept {
		return bufferModel_.lineStart(pos);
	}

	std::size_t lineEndOffset(std::size_t pos) const noexcept {
		return bufferModel_.lineEnd(pos);
	}

	std::size_t nextLineOffset(std::size_t pos) const noexcept {
		return bufferModel_.nextLine(pos);
	}

	std::size_t prevLineOffset(std::size_t pos) const noexcept {
		return bufferModel_.prevLine(pos);
	}

	std::size_t lineIndexOfOffset(std::size_t pos) const noexcept {
		return bufferModel_.lineIndex(pos);
	}

	std::size_t columnOfOffset(std::size_t pos) const noexcept {
		return bufferModel_.column(pos);
	}

	char charAtOffset(std::size_t pos) const noexcept {
		return bufferModel_.charAt(pos);
	}

	std::string lineTextAtOffset(std::size_t pos) const {
		return bufferModel_.lineText(pos);
	}

	std::size_t nextCharOffset(std::size_t pos) noexcept {
		std::size_t len = bufferModel_.length();
		char bytes[4];
		std::size_t count = 0;

		if (pos >= len)
			return len;
		if (bufferModel_.charAt(pos) == '\r' && pos + 1 < len && bufferModel_.charAt(pos + 1) == '\n')
			return std::min(len, pos + 2);
		for (; count < sizeof(bytes) && pos + count < len; ++count)
			bytes[count] = bufferModel_.charAt(pos + count);
		std::size_t step = TText::next(TStringView(bytes, count));
		return std::min(len, pos + std::max<std::size_t>(step, 1));
	}

	std::size_t prevCharOffset(std::size_t pos) noexcept {
		char bytes[4];
		std::size_t start = 0;
		std::size_t count = 0;

		if (pos == 0)
			return 0;
		if (pos > 1 && bufferModel_.charAt(pos - 2) == '\r' && bufferModel_.charAt(pos - 1) == '\n')
			return pos - 2;
		start = pos > sizeof(bytes) ? pos - sizeof(bytes) : 0;
		count = pos - start;
		for (std::size_t i = 0; i < count; ++i)
			bytes[i] = bufferModel_.charAt(start + i);
		std::size_t step = TText::prev(TStringView(bytes, count), count);
		return pos - std::max<std::size_t>(step, 1);
	}

		std::size_t lineMoveOffset(std::size_t pos, int deltaLines) noexcept {
			std::size_t target = std::min(pos, bufferModel_.length());
			int targetVisualColumn =
			    charColumn(bufferModel_.lineStart(pos), std::min(pos, bufferModel_.length()));

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

	std::size_t prevWordOffset(std::size_t pos) noexcept {
		std::size_t p = std::min(pos, bufferModel_.length());

		while (p > 0 && !isWordByte(bufferModel_.charAt(p - 1)))
			--p;
		while (p > 0 && isWordByte(bufferModel_.charAt(p - 1)))
			--p;
		return p;
	}

	std::size_t nextWordOffset(std::size_t pos) noexcept {
		std::size_t p = std::min(pos, bufferModel_.length());
		std::size_t len = bufferModel_.length();

		while (p < len && isWordByte(bufferModel_.charAt(p)))
			++p;
		while (p < len && !isWordByte(bufferModel_.charAt(p)))
			++p;
		return p;
	}

	std::size_t charPtrOffset(std::size_t start, int pos) noexcept {
		std::size_t lineStart = bufferModel_.lineStart(start);
		std::string lineText = bufferModel_.lineText(lineStart);
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
		std::size_t lineStart = bufferModel_.lineStart(start);
		std::string lineText = bufferModel_.lineText(lineStart);
		TStringView line(lineText.data(), lineText.size());
		std::size_t p = 0;
		std::size_t end = std::min(pos, bufferModel_.length()) - lineStart;
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
		moveCursor(std::min(pos, bufferModel_.length()), false, false);
	}

	void setSelectionOffsets(std::size_t start, std::size_t end, Boolean = False) {
		start = std::min(start, bufferModel_.length());
		end = std::min(end, bufferModel_.length());
		selectionAnchor_ = start;
		bufferModel_.setSelection(start, end);
		syncFromEditorState(false);
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
		return static_cast<int>(bufferModel_.lineIndex(bufferModel_.cursor())) + 1;
	}

	int currentViewRow() const noexcept {
		return std::max(1, static_cast<int>(bufferModel_.lineIndex(bufferModel_.cursor())) - delta.y + 1);
	}

	const TMRTextBufferModel &bufferModel() const noexcept {
		return bufferModel_;
	}

	TMRTextBufferModel &bufferModel() noexcept {
		return bufferModel_;
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
		return bufferModel_.text();
	}

	TMRTextBufferModel::ReadSnapshot readSnapshot() const {
		return bufferModel_.readSnapshot();
	}

	TMRTextBufferModel::Document documentCopy() const {
		return bufferModel_.document();
	}

	std::size_t documentId() const noexcept {
		return bufferModel_.documentId();
	}

	std::size_t documentVersion() const noexcept {
		return bufferModel_.version();
	}

	LoadTiming lastLoadTiming() const noexcept {
		return lastLoadTiming_;
	}

	bool applyLineIndexWarmup(const mr::editor::LineIndexWarmupData &warmup,
	                          std::size_t expectedVersion) {
		if (!bufferModel_.adoptLineIndexWarmup(warmup, expectedVersion))
			return false;
		lineIndexWarmupTaskId_ = 0;
		lineIndexWarmupDocumentId_ = 0;
		lineIndexWarmupVersion_ = 0;
		notifyWindowTaskStateChanged();
		updateMetrics();
		updateIndicator();
		drawView();
		return true;
	}

	bool applySyntaxWarmup(const mr::coprocessor::SyntaxWarmupPayload &warmup,
	                       std::size_t expectedVersion, std::uint64_t expectedTaskId) {
		if (expectedTaskId == 0 || syntaxWarmupTaskId_ != expectedTaskId)
			return false;
		if (bufferModel_.documentId() != syntaxWarmupDocumentId_ || bufferModel_.version() != expectedVersion)
			return false;
		if (bufferModel_.language() != warmup.language)
			return false;

		syntaxTokenCache_.clear();
		for (std::size_t i = 0; i < warmup.lines.size(); ++i)
			syntaxTokenCache_[warmup.lines[i].lineStart] = warmup.lines[i].tokens;

		syntaxWarmupTaskId_ = 0;
		syntaxWarmupDocumentId_ = 0;
		syntaxWarmupVersion_ = 0;
		syntaxWarmupTopLine_ = 0;
		syntaxWarmupBottomLine_ = 0;
		syntaxWarmupLanguage_ = TMRSyntaxLanguage::PlainText;
		notifyWindowTaskStateChanged();
		drawView();
		return true;
	}

	bool applyMiniMapWarmup(const mr::coprocessor::MiniMapWarmupPayload &payload, std::size_t expectedVersion,
	                        std::uint64_t expectedTaskId) {
		return applyMiniMapWarmupInternal(payload, expectedVersion, expectedTaskId);
	}

	void clearLineIndexWarmupTask(std::uint64_t expectedTaskId) noexcept {
		if (expectedTaskId != 0 && lineIndexWarmupTaskId_ != expectedTaskId)
			return;
		if (lineIndexWarmupTaskId_ == 0)
			return;
		lineIndexWarmupTaskId_ = 0;
		lineIndexWarmupDocumentId_ = 0;
		lineIndexWarmupVersion_ = 0;
		notifyWindowTaskStateChanged();
	}

	void clearSyntaxWarmupTask(std::uint64_t expectedTaskId) noexcept {
		if (expectedTaskId != 0 && syntaxWarmupTaskId_ != expectedTaskId)
			return;
		if (syntaxWarmupTaskId_ == 0)
			return;
		syntaxWarmupTaskId_ = 0;
		syntaxWarmupDocumentId_ = 0;
		syntaxWarmupVersion_ = 0;
		syntaxWarmupTopLine_ = 0;
		syntaxWarmupBottomLine_ = 0;
		syntaxWarmupLanguage_ = TMRSyntaxLanguage::PlainText;
		notifyWindowTaskStateChanged();
	}

	void clearMiniMapWarmupTask(std::uint64_t expectedTaskId) noexcept {
		clearMiniMapWarmupTaskInternal(expectedTaskId);
	}

	void setSyntaxTitleHint(const std::string &title) {
		syntaxTitleHint_ = title;
		refreshSyntaxContext();
		updateMetrics();
		updateIndicator();
	}

	const char *syntaxLanguageName() const noexcept {
		return bufferModel_.languageName();
	}

	TMRSyntaxLanguage syntaxLanguage() const noexcept {
		return bufferModel_.language();
	}

	bool canSaveInPlace() const {
		return !readOnly_ && hasPersistentFileName();
	}

	bool canSaveAs() const {
		return !readOnly_;
	}

	bool loadMappedFile(TStringView path, std::string &error) {
		TMRTextBufferModel::Document document;
		const auto mapStartedAt = std::chrono::steady_clock::now();

		lastLoadTiming_ = LoadTiming();

		if (!document.loadMappedFile(path, error))
			return false;
		const double mappedLoadMs =
		    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mapStartedAt).count();
		const auto lineCountStartedAt = std::chrono::steady_clock::now();
		const std::size_t lines = document.lineCount();
		const double lineCountMs =
		    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lineCountStartedAt).count();

		lastLoadTiming_.valid = true;
		lastLoadTiming_.bytes = document.length();
		lastLoadTiming_.lines = lines;
		lastLoadTiming_.mappedLoadMs = mappedLoadMs;
		lastLoadTiming_.lineCountMs = lineCountMs;
		setPersistentFileName(path);
		if (!adoptCommittedDocument(document, 0, 0, 0, false)) {
			clearPersistentFileName();
			lastLoadTiming_ = LoadTiming();
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

	bool replaceBufferData(const char *data, uint length) {
		std::string text;
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.readSnapshot(),
		                                                 "replace-buffer-data");
		TMRTextBufferModel::Document preview;
		TMRTextBufferModel::CommitResult commit;

		if (data != nullptr && length != 0)
			text.assign(data, length);
		transaction.setText(text);
		preview = bufferModel_.document();
		commit = preview.tryApply(transaction);
		if (!commit.applied())
			return false;
		return adoptCommittedDocument(preview, 0, 0, 0, false);
	}

	bool replaceBufferText(const char *text) {
		uint length = text != nullptr ? static_cast<uint>(std::strlen(text)) : 0;
		return replaceBufferData(text, length);
	}

	bool appendBufferData(const char *data, uint length) {
		std::string text;
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.readSnapshot(),
		                                                 "append-buffer-data");
		TMRTextBufferModel::Document preview;
		TMRTextBufferModel::CommitResult commit;
		std::size_t endPtr = bufferModel_.length();

		if (length == 0)
			return true;
		if (data != nullptr)
			text.assign(data, length);
		transaction.insert(endPtr, text);
		preview = bufferModel_.document();
		commit = preview.tryApply(transaction);
		if (!commit.applied())
			return false;
		return adoptCommittedDocument(preview, endPtr + text.size(), endPtr + text.size(),
		                              endPtr + text.size(), false);
	}

	bool appendBufferText(const char *text) {
		uint length = text != nullptr ? static_cast<uint>(std::strlen(text)) : 0;
		return appendBufferData(text, length);
	}

	bool replaceRangeAndSelect(uint start, uint end, const char *data, uint length) {
		std::string text;
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.readSnapshot(),
		                                                 "replace-range-select");
		TMRTextBufferModel::Document preview;
		TMRTextBufferModel::CommitResult commit;
		TMRTextBufferModel::Range range;

		if (readOnly_)
			return false;
		if (end < start)
			std::swap(start, end);
		range = TMRTextBufferModel::Range(start, end).clamped(bufferModel_.length());
		if (data != nullptr && length != 0)
			text.assign(data, length);
		transaction.replace(range, text);
		preview = bufferModel_.document();
		commit = preview.tryApply(transaction);
		if (!commit.applied())
			return false;
		return adoptCommittedDocument(preview, range.start, range.start, range.start + text.size(), true,
		                              &commit.change);
	}

	bool insertBufferText(const std::string &text) {
		std::size_t start = bufferModel_.cursor();
		std::size_t end = start;
		TMRTextBufferModel::Range range;
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.readSnapshot(),
		                                                 "insert-buffer-text");
		TMRTextBufferModel::Document preview;
		TMRTextBufferModel::CommitResult commit;

		if (readOnly_)
			return false;
		if (bufferModel_.hasSelection()) {
			range = bufferModel_.selection().range();
			start = range.start;
			end = range.end;
		} else if (!insertMode_) {
			std::size_t endSel = bufferModel_.cursor();
			for (std::string::size_type i = 0; i < text.size() && endSel < lineEndOffset(start); ++i)
				endSel = nextCharOffset(endSel);
			end = endSel;
		}
		range = TMRTextBufferModel::Range(start, end).clamped(bufferModel_.length());
		transaction.replace(range, text);
		preview = bufferModel_.document();
		commit = preview.tryApply(transaction);
		if (!commit.applied())
			return false;
		bumpUndoCounters(range.length(), text.size());
		start = range.start + text.size();
		return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
	}

	bool replaceCurrentLineText(const std::string &text) {
		std::size_t start = bufferModel_.lineStart(bufferModel_.cursor());
		std::size_t end = bufferModel_.lineEnd(bufferModel_.cursor());
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.readSnapshot(),
		                                                 "replace-current-line");
		TMRTextBufferModel::Document preview;
		TMRTextBufferModel::CommitResult commit;

		if (readOnly_)
			return false;
		transaction.replace(TMRTextBufferModel::Range(start, end), text);
		preview = bufferModel_.document();
		commit = preview.tryApply(transaction);
		if (!commit.applied())
			return false;
		bumpUndoCounters(end - start, text.size());
		return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
	}

	bool deleteCharsAtCursor(int count) {
		std::size_t start = bufferModel_.cursor();
		std::size_t end = start;
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.readSnapshot(),
		                                                 "delete-chars-at-cursor");
		TMRTextBufferModel::Document preview;
		TMRTextBufferModel::CommitResult commit;

		if (readOnly_)
			return false;
		if (count <= 0)
			return true;
		for (int i = 0; i < count && end < bufferModel_.length(); ++i)
			end = nextCharOffset(end);
		if (end <= start)
			return true;
		transaction.erase(TMRTextBufferModel::Range(start, end));
		preview = bufferModel_.document();
		commit = preview.tryApply(transaction);
		if (!commit.applied())
			return false;
		bumpUndoCounters(end - start, 0);
		return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
	}

	bool deleteCurrentLineText() {
		std::size_t start = bufferModel_.lineStart(bufferModel_.cursor());
		std::size_t end = bufferModel_.nextLine(bufferModel_.cursor());
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.readSnapshot(),
		                                                 "delete-current-line");
		TMRTextBufferModel::Document preview;
		TMRTextBufferModel::CommitResult commit;

		if (readOnly_)
			return false;
		transaction.erase(TMRTextBufferModel::Range(start, end));
		preview = bufferModel_.document();
		commit = preview.tryApply(transaction);
		if (!commit.applied())
			return false;
		bumpUndoCounters(end - start, 0);
		return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
	}

	bool replaceWholeBuffer(const std::string &text, std::size_t cursorPos) {
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.readSnapshot(),
		                                                 "replace-whole-buffer");
		TMRTextBufferModel::Document preview;
		TMRTextBufferModel::CommitResult commit;

		if (readOnly_)
			return false;
		transaction.setText(text);
		preview = bufferModel_.document();
		commit = preview.tryApply(transaction);
		if (!commit.applied())
			return false;
		bumpUndoCounters(bufferModel_.length(), text.size());
		cursorPos = std::min(cursorPos, text.size());
		return adoptCommittedDocument(preview, cursorPos, cursorPos, cursorPos, true, &commit.change);
	}

	TMRTextBufferModel::CommitResult applyStagedTransaction(
	    const TMRTextBufferModel::StagedTransaction &transaction, std::size_t cursorPos,
	    std::size_t selStart, std::size_t selEnd, bool modifiedState = true) {
		TMRTextBufferModel::Document preview = bufferModel_.document();
		TMRTextBufferModel::CommitResult result = preview.tryApply(transaction);

		if (result.applied())
			adoptCommittedDocument(preview, cursorPos, selStart, selEnd, modifiedState,
			                       &result.change);
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
		TMRTextBufferModel::Range selection = bufferModel_.selection().range().normalized();
		MiniMapPalette miniMapPalette = resolveMiniMapPalette();
		const bool drawMiniMap = viewport.miniMapBodyWidth > 0 && viewport.miniMapInfoX >= 0;
		const bool miniMapUseBraille = useBrailleMiniMapRenderer();
		std::string viewportMarkerGlyph = normalizedMiniMapViewportMarkerGlyph(editSettings.miniMapMarkerGlyph);
		const int miniMapRows = std::max(0, miniMapViewportRows());
		if (bufferModel_.exactLineCountKnown())
			totalLines = std::max<std::size_t>(1, bufferModel_.lineCount());
		else
			totalLines = std::max<std::size_t>(
			    1, std::max<std::size_t>(bufferModel_.estimatedLineCount(),
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
			                 drawEofMarkerAsEmoji, editSettings.displayTabs);
			writeBuf(0, y, size.x, 1, buffer);
			if (linePtr < bufferModel_.length())
				linePtr = bufferModel_.nextLine(linePtr);
			++lineIndex;
		}
		scheduleSyntaxWarmupIfNeeded();
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
		if (aState == sfCursorVis || indicatorUpdateInProgress_)
			return;
		updateIndicator();
	}

	virtual Boolean valid(ushort command) override {
		if (command == cmValid || command == cmReleasedFocus)
			return True;
		if (readOnly_ || !bufferModel_.isModified())
			return True;
		if (!hasPersistentFileName())
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

	static int configuredTabSize() noexcept {
		int tabSize = configuredTabSizeSetting();
		if (tabSize < 1)
			tabSize = 1;
		if (tabSize > 32)
			tabSize = 32;
		return tabSize;
	}

	static int tabDisplayWidth(int visualColumn, int tabSize) noexcept {
		int nextStop = ((visualColumn / tabSize) + 1) * tabSize;
		return std::max(1, nextStop - visualColumn);
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
		if (bufferModel_.exactLineCountKnown())
			lines = std::max<std::size_t>(bufferModel_.lineCount(), visibleEnd);
		else
			lines = std::max<std::size_t>(bufferModel_.estimatedLineCount(), visibleEnd);
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
		bool hasFindRange = false;
		std::size_t findLineStart = 0;
		std::size_t findLineEnd = 0;
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
		return std::strcmp(position.c_str(), "LEADING") == 0;
	}

	static bool isGutterPositionTrailing(const std::string &position) noexcept {
		return std::strcmp(position.c_str(), "TRAILING") == 0;
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
		if (lineEnd <= lineStart || dirtyRanges_.empty())
			return false;
		for (const TMRTextBufferModel::Range &range : dirtyRanges_) {
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
		return miniMapCache_.valid && miniMapCache_.documentId == bufferModel_.documentId() &&
		       miniMapCache_.documentVersion == bufferModel_.version() &&
		       miniMapCache_.rowCount == rowCount &&
		       miniMapCache_.bodyWidth == viewport.miniMapBodyWidth &&
		       miniMapCache_.viewportWidth == std::max(1, viewport.width) && miniMapCache_.braille == braille &&
		       miniMapCache_.windowStartLine == window.startLine &&
		       miniMapCache_.windowLineCount == std::max<std::size_t>(1, window.lineCount);
	}

	int miniMapViewportRows() const noexcept {
		return std::max(0, visibleTextRows());
	}

	void clearMiniMapWarmupTaskInternal(std::uint64_t expectedTaskId) noexcept {
		if (expectedTaskId != 0 && miniMapWarmupTaskId_ != expectedTaskId)
			return;
		if (miniMapWarmupTaskId_ == 0)
			return;
		const bool shouldReschedule = miniMapWarmupReschedulePending_;
		miniMapWarmupTaskId_ = 0;
		miniMapWarmupDocumentId_ = 0;
		miniMapWarmupVersion_ = 0;
		miniMapWarmupRows_ = 0;
		miniMapWarmupBodyWidth_ = 0;
		miniMapWarmupViewportWidth_ = 0;
		miniMapWarmupBraille_ = true;
		miniMapWarmupWindowStartLine_ = 0;
		miniMapWarmupWindowLineCount_ = 0;
		miniMapWarmupReschedulePending_ = false;
		notifyWindowTaskStateChanged();
		if (shouldReschedule)
			drawView();
	}

	void invalidateMiniMapCache(bool cancelTask) {
		const bool keepStaleCache = !cancelTask && miniMapCache_.documentId == bufferModel_.documentId() &&
		                           miniMapCache_.bodyWidth > 0 && miniMapCache_.rowCount > 0;
		miniMapCache_.valid = false;
		if (!keepStaleCache) {
			miniMapCache_.rowPatterns.clear();
			miniMapCache_.rowLineStarts.clear();
			miniMapCache_.rowLineEnds.clear();
		}
		miniMapWarmupReschedulePending_ = false;
		if (cancelTask && miniMapWarmupTaskId_ != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(miniMapWarmupTaskId_));
			clearMiniMapWarmupTaskInternal(miniMapWarmupTaskId_);
		}
	}

	bool applyMiniMapWarmupInternal(const mr::coprocessor::MiniMapWarmupPayload &payload,
	                                std::size_t expectedVersion, std::uint64_t expectedTaskId) {
		if (expectedTaskId == 0 || miniMapWarmupTaskId_ != expectedTaskId)
			return false;
		if (bufferModel_.documentId() != miniMapWarmupDocumentId_ || bufferModel_.version() != expectedVersion)
			return false;

		miniMapCache_.valid = true;
		miniMapCache_.braille = payload.braille;
		miniMapCache_.rowCount = payload.rowCount;
		miniMapCache_.bodyWidth = payload.bodyWidth;
		miniMapCache_.documentId = bufferModel_.documentId();
		miniMapCache_.documentVersion = bufferModel_.version();
		miniMapCache_.totalLines = std::max<std::size_t>(1, payload.totalLines);
		miniMapCache_.windowStartLine = payload.windowStartLine;
		miniMapCache_.windowLineCount = std::max<std::size_t>(1, payload.windowLineCount);
		miniMapCache_.viewportWidth = std::max(1, payload.viewportWidth);
		miniMapCache_.rowPatterns = payload.rowPatterns;
		miniMapCache_.rowLineStarts = payload.rowLineStarts;
		miniMapCache_.rowLineEnds = payload.rowLineEnds;

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
		const std::size_t docId = bufferModel_.documentId();
		const std::size_t version = bufferModel_.version();
		std::size_t totalLines = std::max<std::size_t>(1, totalLinesHint);
		if (bufferModel_.exactLineCountKnown())
			totalLines = std::max<std::size_t>(1, bufferModel_.lineCount());
		const MiniMapSamplingWindow samplingWindow = miniMapSamplingWindowFor(totalLines, topLine, rowCount, useBraille);
		if (miniMapCacheReadyForViewport(viewport, useBraille, samplingWindow))
			return;

		const int bodyWidth = viewport.miniMapBodyWidth;
		const int viewportWidth = std::max(1, viewport.width);
		const int tabSize = configuredTabSize();
		if (miniMapWarmupTaskId_ != 0) {
			if (miniMapWarmupDocumentId_ == docId && miniMapWarmupVersion_ == version &&
			    miniMapWarmupRows_ == rowCount && miniMapWarmupBodyWidth_ == bodyWidth &&
			    miniMapWarmupViewportWidth_ == viewportWidth && miniMapWarmupBraille_ == useBraille &&
			    miniMapWarmupWindowStartLine_ == samplingWindow.startLine &&
			    miniMapWarmupWindowLineCount_ == samplingWindow.lineCount)
				return;
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(miniMapWarmupTaskId_));
			clearMiniMapWarmupTaskInternal(miniMapWarmupTaskId_);
		}

		TMRTextBufferModel::ReadSnapshot snapshot = bufferModel_.readSnapshot();
		std::uint64_t previousTaskId = miniMapWarmupTaskId_;
		miniMapWarmupDocumentId_ = docId;
		miniMapWarmupVersion_ = version;
		miniMapWarmupRows_ = rowCount;
		miniMapWarmupBodyWidth_ = bodyWidth;
		miniMapWarmupViewportWidth_ = viewportWidth;
		miniMapWarmupBraille_ = useBraille;
		miniMapWarmupWindowStartLine_ = samplingWindow.startLine;
		miniMapWarmupWindowLineCount_ = samplingWindow.lineCount;
		miniMapWarmupReschedulePending_ = false;
		miniMapWarmupTaskId_ = mr::coprocessor::globalCoprocessor().submit(
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
		if (miniMapWarmupTaskId_ != previousTaskId)
			notifyWindowTaskStateChanged();
	}

	MiniMapOverlayState computeMiniMapOverlayState(const TMRTextBufferModel::Range &selection,
	                                               std::size_t totalLines) const {
		MiniMapOverlayState overlay;
		if (selection.end > selection.start && bufferModel_.length() != 0) {
			const std::size_t startOffset = std::min(selection.start, bufferModel_.length() - 1);
			const std::size_t endOffset = std::min(selection.end - 1, bufferModel_.length() - 1);
			overlay.findLineStart = std::min<std::size_t>(bufferModel_.lineIndex(startOffset), totalLines);
			overlay.findLineEnd = std::min<std::size_t>(bufferModel_.lineIndex(endOffset) + 1, totalLines);
			overlay.hasFindRange = overlay.findLineEnd > overlay.findLineStart;
		}
		for (const TMRTextBufferModel::Range &range : dirtyRanges_) {
			if (range.end <= range.start || range.start >= bufferModel_.length())
				continue;
			std::size_t startOffset = std::min(range.start, bufferModel_.length() - 1);
			std::size_t endOffset = std::min(range.end - 1, bufferModel_.length() - 1);
			std::size_t lineStart = std::min<std::size_t>(bufferModel_.lineIndex(startOffset), totalLines);
			std::size_t lineEnd = std::min<std::size_t>(bufferModel_.lineIndex(endOffset) + 1, totalLines);
			if (lineEnd > lineStart)
				overlay.dirtyLineRanges.push_back(std::make_pair(lineStart, lineEnd));
		}
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
		    !cacheReady && miniMapCache_.bodyWidth == bodyWidth && miniMapCache_.rowCount == miniMapRows &&
		    !miniMapCache_.rowPatterns.empty();
		std::size_t rowLineStart = 0;
		std::size_t rowLineEnd = 0;

		if (y >= miniMapRows) {
			b.moveChar(static_cast<ushort>(bodyX), ' ', palette.normal, static_cast<ushort>(bodyWidth));
			if (viewport.miniMapSeparatorX >= 0 && viewport.miniMapSeparatorX < size.x)
				b.moveChar(static_cast<ushort>(viewport.miniMapSeparatorX), ' ', palette.normal, 1);
			b.moveChar(static_cast<ushort>(viewport.miniMapInfoX), ' ', palette.normal, 1);
			return;
		}

		if ((cacheReady || stalePatternCacheUsable) && static_cast<std::size_t>(y) < miniMapCache_.rowLineStarts.size() &&
		    static_cast<std::size_t>(y) < miniMapCache_.rowLineEnds.size()) {
			rowLineStart = miniMapCache_.rowLineStarts[static_cast<std::size_t>(y)];
			rowLineEnd = miniMapCache_.rowLineEnds[static_cast<std::size_t>(y)];
		} else {
			std::pair<std::size_t, std::size_t> rowSpan = scaledInterval(
			    static_cast<std::size_t>(y), static_cast<std::size_t>(std::max(miniMapRows, 1)),
			    samplingWindow.lineCount);
			const std::size_t normTotal = std::max<std::size_t>(1, totalLines);
			rowLineStart = std::min(samplingWindow.startLine + rowSpan.first, normTotal);
			rowLineEnd = std::min(samplingWindow.startLine + rowSpan.second, normTotal);
		}

		bool rowChanged = lineIntervalOverlaps(rowLineStart, rowLineEnd, overlay.dirtyLineRanges);
		bool rowFind = overlay.hasFindRange && rowLineStart < overlay.findLineEnd && rowLineEnd > overlay.findLineStart;
		bool rowError = false;

		for (int x = 0; x < bodyWidth; ++x) {
			unsigned char pattern = 0;
			if (cacheReady || stalePatternCacheUsable) {
				std::size_t index = static_cast<std::size_t>(y * bodyWidth + x);
				if (index < miniMapCache_.rowPatterns.size())
					pattern = miniMapCache_.rowPatterns[index];
			}
			TColorAttr rowPriorityColor = palette.normal;
			if (rowError)
				rowPriorityColor = palette.errorMarker;
			else if (rowFind)
				rowPriorityColor = palette.findMarker;
			else if (rowChanged)
				rowPriorityColor = palette.changed;
			TColorAttr cellColor = (pattern != 0) ? rowPriorityColor : palette.normal;
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
		MREditSetupSettings editSettings = configuredEditSetupSettings();
		bool eofCtrlZ = editSettings.eofCtrlZ;
		bool eofCrLf = editSettings.eofCrLf;
		bool hasAnyByte = false;
		bool hasLastOutputByte = false;
		unsigned char lastOutputByte = 0;

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

		auto writeByte = [&](unsigned char byte) -> bool {
			out.put(static_cast<char>(byte));
			if (!out)
				return false;
			hasLastOutputByte = true;
			lastOutputByte = byte;
			return true;
		};
		auto failWrite = [&]() -> bool {
			TEditor::editorDialog(edWriteError, targetPath);
			return false;
		};

		if (!eofCrLf) {
			for (std::size_t i = 0; i < bufferModel_.document().pieceCount(); ++i) {
				mr::editor::PieceChunkView chunk = bufferModel_.document().pieceChunk(i);
				writeChunk(out, chunk.data, chunk.length);
				if (!out)
					return failWrite();
				if (chunk.length > 0) {
					hasAnyByte = true;
					hasLastOutputByte = true;
					lastOutputByte = static_cast<unsigned char>(chunk.data[chunk.length - 1]);
				}
			}
		} else {
			bool hasPendingByte = false;
			unsigned char pendingByte = 0;

			for (std::size_t i = 0; i < bufferModel_.document().pieceCount(); ++i) {
				mr::editor::PieceChunkView chunk = bufferModel_.document().pieceChunk(i);
				for (std::size_t j = 0; j < chunk.length; ++j) {
					unsigned char nextByte = static_cast<unsigned char>(chunk.data[j]);

					if (hasPendingByte && !writeByte(pendingByte))
						return failWrite();
					pendingByte = nextByte;
					hasPendingByte = true;
					hasAnyByte = true;
				}
			}

			if (eofCtrlZ && pendingByte == static_cast<unsigned char>(0x1A) && hasAnyByte) {
				// Treat trailing Ctrl-Z as logical EOF marker and normalize EOL before final marker append.
			} else if (!hasAnyByte) {
				if (!writeByte('\r') || !writeByte('\n'))
					return failWrite();
			} else if (pendingByte == '\n') {
				if (hasLastOutputByte && lastOutputByte == '\r') {
					if (!writeByte('\n'))
						return failWrite();
				} else if (!writeByte('\r') || !writeByte('\n'))
					return failWrite();
			} else if (pendingByte == '\r') {
				if (!writeByte('\r') || !writeByte('\n'))
					return failWrite();
			} else {
				if (!writeByte(pendingByte) || !writeByte('\r') || !writeByte('\n'))
					return failWrite();
			}
		}

		if (eofCtrlZ && (!hasLastOutputByte || lastOutputByte != static_cast<unsigned char>(0x1A)))
			if (!writeByte(static_cast<unsigned char>(0x1A)))
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

	void bumpUndoCounters(std::size_t deletedBytes, std::size_t insertedBytes) noexcept {
		delCount_ += static_cast<uint>(std::min<std::size_t>(deletedBytes, UINT_MAX));
		insCount_ += static_cast<uint>(std::min<std::size_t>(insertedBytes, UINT_MAX));
	}

	std::size_t lineStartForIndex(std::size_t index) const noexcept {
		return bufferModel_.lineStartByIndex(index);
	}

	int longestLineWidth() const noexcept {
		std::size_t pos = 0;
		std::size_t len = bufferModel_.length();
		int maxWidth = 1;
		int tabSize = configuredTabSize();

		while (true) {
			int width = displayWidthForText(bufferModel_.lineText(pos), tabSize);
			maxWidth = std::max(maxWidth, width + 1);
			if (pos >= len)
				break;
			std::size_t next = bufferModel_.nextLine(pos);
			if (next <= pos)
				break;
			pos = next;
		}
		return maxWidth;
	}

	bool useApproximateLargeFileMetrics() const noexcept {
		const TMRTextBufferModel::Document &document = bufferModel_.document();
		return document.hasMappedOriginal() && document.length() >= static_cast<std::size_t>(8) * 1024 * 1024;
	}

	int dynamicLargeFileLineLimit() const noexcept {
		const std::size_t estimated = bufferModel_.estimatedLineCount();
		const std::size_t currentLine = bufferModel_.lineIndex(bufferModel_.cursor());
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
		const std::size_t cursor = bufferModel_.cursor();
		const int cursorColumn = charColumn(bufferModel_.lineStart(cursor), cursor);
		const int viewportWidth = textViewportWidth();
		return std::max(std::max(viewportWidth, 256),
		                std::max(delta.x + viewportWidth + 64, cursorColumn + 64));
	}

	void scheduleLineIndexWarmupIfNeeded() {
		if (!bufferModel_.document().hasMappedOriginal() || bufferModel_.document().exactLineCountKnown()) {
			std::uint64_t cancelledTaskId = lineIndexWarmupTaskId_;
			bool hadTask = cancelledTaskId != 0;
			lineIndexWarmupTaskId_ = 0;
			lineIndexWarmupDocumentId_ = 0;
			lineIndexWarmupVersion_ = 0;
			if (hadTask) {
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
				notifyWindowTaskStateChanged();
			}
			return;
		}

			const std::size_t docId = bufferModel_.documentId();
			const std::size_t version = bufferModel_.version();
			if (lineIndexWarmupTaskId_ != 0 && lineIndexWarmupDocumentId_ == docId &&
			    lineIndexWarmupVersion_ == version)
				return;

			TMRTextBufferModel::ReadSnapshot snapshot = bufferModel_.readSnapshot();
			std::uint64_t previousTaskId = lineIndexWarmupTaskId_;
			if (previousTaskId != 0)
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
			lineIndexWarmupDocumentId_ = docId;
			lineIndexWarmupVersion_ = version;
			lineIndexWarmupTaskId_ = mr::coprocessor::globalCoprocessor().submit(
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
			if (lineIndexWarmupTaskId_ != previousTaskId)
				notifyWindowTaskStateChanged();
	}

	void scheduleSyntaxWarmupIfNeeded() {
		const int textRows = visibleTextRows();
		if (bufferModel_.language() == TMRSyntaxLanguage::PlainText || textRows <= 0) {
			resetSyntaxWarmupState(true);
			return;
		}

		const std::size_t docId = bufferModel_.documentId();
		const std::size_t version = bufferModel_.version();
		const TMRSyntaxLanguage language = bufferModel_.language();
		const std::size_t topLine = static_cast<std::size_t>(std::max(delta.y - 4, 0));
		const int rowBudget = std::max(textRows + 8, 8);
		std::vector<std::size_t> lineStarts = syntaxWarmupLineStarts(topLine, rowBudget);
		if (lineStarts.empty())
			return;

		const std::size_t bottomLine = topLine + lineStarts.size();
		if (hasSyntaxTokensForLineStarts(lineStarts)) {
			std::uint64_t previousTaskId = syntaxWarmupTaskId_;
			bool hadTask = previousTaskId != 0;
			syntaxWarmupTaskId_ = 0;
			syntaxWarmupDocumentId_ = docId;
			syntaxWarmupVersion_ = version;
			syntaxWarmupTopLine_ = topLine;
			syntaxWarmupBottomLine_ = bottomLine;
			syntaxWarmupLanguage_ = language;
			if (hadTask) {
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
				notifyWindowTaskStateChanged();
			}
			return;
		}

		if (syntaxWarmupTaskId_ != 0 && syntaxWarmupDocumentId_ == docId &&
		    syntaxWarmupVersion_ == version && syntaxWarmupLanguage_ == language &&
		    topLine >= syntaxWarmupTopLine_ && bottomLine <= syntaxWarmupBottomLine_)
			return;

		TMRTextBufferModel::ReadSnapshot snapshot = bufferModel_.readSnapshot();
		std::uint64_t previousTaskId = syntaxWarmupTaskId_;
		if (previousTaskId != 0)
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
		syntaxWarmupDocumentId_ = docId;
		syntaxWarmupVersion_ = version;
		syntaxWarmupTopLine_ = topLine;
		syntaxWarmupBottomLine_ = bottomLine;
		syntaxWarmupLanguage_ = language;
		syntaxWarmupTaskId_ = mr::coprocessor::globalCoprocessor().submit(
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
		if (syntaxWarmupTaskId_ != previousTaskId)
			notifyWindowTaskStateChanged();
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
			limitY = std::max<int>(1, static_cast<int>(bufferModel_.lineCount()));
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
		if (indicatorUpdateInProgress_)
			return;
		indicatorUpdateInProgress_ = true;
		TextViewportGeometry viewport = textViewportGeometry();
		std::size_t cursor = bufferModel_.cursor();
		unsigned long visualColumn =
		    static_cast<unsigned long>(charColumn(bufferModel_.lineStart(cursor), cursor));
		unsigned long line = static_cast<unsigned long>(bufferModel_.lineIndex(cursor));
		long long localX = viewport.localXFromVisualColumn(static_cast<long long>(visualColumn));
		long long localY = static_cast<long long>(line) - delta.y;

		if (indicator_ != nullptr) {
			if (auto *mrIndicator = dynamic_cast<TMRIndicator *>(indicator_))
				mrIndicator->setDisplayValue(visualColumn, line, bufferModel_.isModified() ? True : False);
			else {
				TPoint location = {
				    short(visualColumn > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : visualColumn),
				    short(line > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : line)};
				indicator_->setValue(location, bufferModel_.isModified() ? True : False);
			}
		}

		if (shouldShowEditorCursor(localX, localY, viewport)) {
			setCursor(static_cast<int>(localX), static_cast<int>(localY));
			showCursor();
		} else
			hideCursor();
		indicatorUpdateInProgress_ = false;
	}

	void ensureCursorVisible(bool centerCursor) {
		std::size_t cursor = bufferModel_.cursor();
		int visualColumn = charColumn(bufferModel_.lineStart(cursor), cursor);
		int line = static_cast<int>(bufferModel_.lineIndex(cursor));
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
		target = canonicalCursorOffset(std::min(target, bufferModel_.length()));
		if (extendSelection) {
			std::size_t anchor =
			    bufferModel_.hasSelection() ? bufferModel_.selection().anchor : bufferModel_.cursor();
			selectionAnchor_ = anchor;
			bufferModel_.setCursorAndSelection(target, anchor, target);
		} else {
			if (configuredPersistentBlocksSetting() && bufferModel_.hasSelection())
				bufferModel_.setCursor(target);
			else
				bufferModel_.setCursorAndSelection(target, target, target);
			selectionAnchor_ = target;
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
		return (event.keyDown.controlKeyState & kbPaste) != 0 || event.keyDown.textLength > 0 ||
		       event.keyDown.keyCode == kbTab ||
		       (event.keyDown.charScan.charCode >= 32 && event.keyDown.charScan.charCode < 255);
	}

	void handleTextInput(TEvent &event) {
		if (readOnly_) {
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

		if (event.keyDown.textLength > 0)
			insertBufferText(std::string(event.keyDown.text, event.keyDown.textLength));
		else if (event.keyDown.keyCode == kbTab)
			insertBufferText(tabKeyText());
		else
			insertBufferText(std::string(1, static_cast<char>(event.keyDown.charScan.charCode)));
		clearEvent(event);
	}

	std::string tabKeyText() const {
		if (configuredTabExpandSetting())
			return "\t";
		std::size_t insertPos = bufferModel_.cursor();
		if (bufferModel_.hasSelection())
			insertPos = bufferModel_.selection().range().start;
		int visualColumn = charColumn(bufferModel_.lineStart(insertPos), insertPos);
		return std::string(static_cast<std::size_t>(tabDisplayWidth(visualColumn, configuredTabSize())), ' ');
	}

	void handleKeyDown(TEvent &event) {
		ushort key = ctrlToArrow(event.keyDown.keyCode);
		bool extend = (event.keyDown.controlKeyState & kbShift) != 0;

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
				moveCursor(autoIndent_ ? charPtrOffset(lineStartOffset(cursorOffset()), 0)
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
				if (!readOnly_)
					newLineWithIndent("");
				clearEvent(event);
				return;
			case kbBack:
				if (!readOnly_) {
					if (bufferModel_.hasSelection())
						replaceSelectionText(std::string());
					else if (cursorOffset() > 0)
						replaceRangeAndSelect(static_cast<uint>(prevCharOffset(cursorOffset())),
						                      static_cast<uint>(cursorOffset()), "", 0);
				}
				clearEvent(event);
				return;
			case kbDel:
				if (!readOnly_) {
					if (bufferModel_.hasSelection())
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
			case cmUndo:
				break;
			case cmMrTextUpperCaseMenu:
				convertSelectionToUpperCase();
				break;
			case cmMrTextLowerCaseMenu:
				convertSelectionToLowerCase();
				break;
			case cmClear:
				if (!readOnly_)
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
				if (!readOnly_)
					newLineWithIndent("");
				break;
			case cmBackSpace:
				if (!readOnly_) {
					if (bufferModel_.hasSelection())
						replaceSelectionText(std::string());
					else if (cursorOffset() > 0)
						replaceRangeAndSelect(static_cast<uint>(prevCharOffset(cursorOffset())),
						                      static_cast<uint>(cursorOffset()), "", 0);
				}
				break;
			case cmDelChar:
				if (!readOnly_) {
					if (bufferModel_.hasSelection())
						replaceSelectionText(std::string());
					else
						deleteCharsAtCursor(1);
				}
				break;
			case cmDelWord:
				if (!readOnly_)
					replaceRangeAndSelect(static_cast<uint>(cursorOffset()),
					                      static_cast<uint>(nextWordOffset(cursorOffset())), "", 0);
				break;
			case cmDelWordLeft:
				if (!readOnly_)
					replaceRangeAndSelect(static_cast<uint>(prevWordOffset(cursorOffset())),
					                      static_cast<uint>(cursorOffset()), "", 0);
				break;
			case cmDelStart:
				if (!readOnly_)
					replaceRangeAndSelect(static_cast<uint>(lineStartOffset(cursorOffset())),
					                      static_cast<uint>(cursorOffset()), "", 0);
				break;
			case cmDelEnd:
				if (!readOnly_)
					replaceRangeAndSelect(static_cast<uint>(cursorOffset()),
					                      static_cast<uint>(lineEndOffset(cursorOffset())), "", 0);
				break;
			case cmDelLine:
				if (!readOnly_)
					deleteCurrentLineText();
				break;
			case cmInsMode:
				setInsertModeEnabled(!insertModeEnabled());
				break;
			case cmSelectAll:
				selectionAnchor_ = 0;
				bufferModel_.setCursorAndSelection(bufferModel_.length(), 0, bufferModel_.length());
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
		    (event.mouse.controlKeyState & kbShift) != 0 && bufferModel_.hasSelection()
		        ? bufferModel_.selection().anchor
		        : bufferModel_.cursor();
		selectionAnchor_ = anchor;
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
			bufferModel_.setCursorAndSelection(target, selectionAnchor_, target);
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
		pos = std::min(pos, bufferModel_.length());
		if (pos > 0 && pos < bufferModel_.length() && bufferModel_.charAt(pos) == '\n' &&
		    bufferModel_.charAt(pos - 1) == '\r')
			return pos - 1;
		return pos;
	}

	void copySelection() {
		if (!bufferModel_.hasSelection())
			return;
		TMRTextBufferModel::Range range = bufferModel_.selection().range();
		clipboardText() = bufferModel_.text().substr(range.start, range.length());
	}

	void cutSelection() {
		if (readOnly_ || !bufferModel_.hasSelection())
			return;
		copySelection();
		replaceSelectionText(std::string());
	}

	void pasteClipboard() {
		if (readOnly_ || clipboardText().empty())
			return;
		insertBufferText(clipboardText());
	}

	void replaceSelectionText(const std::string &text) {
		if (!bufferModel_.hasSelection()) {
			if (!text.empty())
				insertBufferText(text);
			return;
		}
		TMRTextBufferModel::Range range = bufferModel_.selection().range();
		replaceRangeAndSelect(static_cast<uint>(range.start), static_cast<uint>(range.end), text.data(),
		                      static_cast<uint>(text.size()));
	}

	void convertSelectionToUpperCase() {
		if (readOnly_ || !bufferModel_.hasSelection())
			return;
		TMRTextBufferModel::Range range = bufferModel_.selection().range();
		std::string text = bufferModel_.text().substr(range.start, range.length());
		for (char &c : text)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		replaceSelectionText(text);
		setSelectionOffsets(range.start, range.start + text.length());
	}

	void convertSelectionToLowerCase() {
		if (readOnly_ || !bufferModel_.hasSelection())
			return;
		TMRTextBufferModel::Range range = bufferModel_.selection().range();
		std::string text = bufferModel_.text().substr(range.start, range.length());
		for (char &c : text)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		replaceSelectionText(text);
		setSelectionOffsets(range.start, range.start + text.length());
	}

	Boolean confirmSaveOrDiscardUntitled() {
		switch (mr::dialogs::showUnsavedChangesDialog("Save As", "Window has unsaved changes.")) {
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

	TColorAttr tokenColor(TMRSyntaxToken token, bool selected, TAttrPair pair) noexcept {
		TColorAttr normal = static_cast<TColorAttr>(pair);
		TColorAttr selectedAttr = static_cast<TColorAttr>(pair >> 8);
		uchar background = static_cast<uchar>((selected ? selectedAttr : normal) & 0xF0);

		if (selected)
			return selectedAttr;

		switch (token) {
			case TMRSyntaxToken::Keyword:
			case TMRSyntaxToken::Directive:
			case TMRSyntaxToken::Section:
				return static_cast<TColorAttr>(background | 0x0E);
			case TMRSyntaxToken::Type:
			case TMRSyntaxToken::Key:
				return static_cast<TColorAttr>(background | 0x0B);
			case TMRSyntaxToken::Number:
				return static_cast<TColorAttr>(background | 0x0A);
			case TMRSyntaxToken::String:
				return static_cast<TColorAttr>(background | 0x0D);
			case TMRSyntaxToken::Comment:
				return static_cast<TColorAttr>(background | 0x03);
			case TMRSyntaxToken::Heading:
				return static_cast<TColorAttr>(background | 0x0F);
			default:
				return normal;
		}
	}

	void refreshSyntaxContext() {
		TMRSyntaxLanguage oldLanguage = bufferModel_.language();
		bufferModel_.setSyntaxContext(hasPersistentFileName() ? fileName : "", syntaxTitleHint_);
		if (bufferModel_.language() != oldLanguage)
			resetSyntaxWarmupState(true);
	}

	void resetSyntaxWarmupState(bool clearCache) noexcept {
		std::uint64_t cancelledTaskId = syntaxWarmupTaskId_;
		bool hadTask = cancelledTaskId != 0;
		if (clearCache)
			syntaxTokenCache_.clear();
		syntaxWarmupTaskId_ = 0;
		syntaxWarmupDocumentId_ = 0;
		syntaxWarmupVersion_ = 0;
		syntaxWarmupTopLine_ = 0;
		syntaxWarmupBottomLine_ = 0;
		syntaxWarmupLanguage_ = TMRSyntaxLanguage::PlainText;
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
			if (lineStart >= bufferModel_.length())
				break;
			std::size_t next = bufferModel_.nextLine(lineStart);
			if (next <= lineStart)
				break;
			lineStart = next;
		}
		return lineStarts;
	}

	bool hasSyntaxTokensForLineStarts(const std::vector<std::size_t> &lineStarts) const {
		for (std::size_t i = 0; i < lineStarts.size(); ++i)
			if (syntaxTokenCache_.find(lineStarts[i]) == syntaxTokenCache_.end())
				return false;
		return true;
	}

	TMRSyntaxTokenMap syntaxTokensForLine(std::size_t lineStart) const {
		std::map<std::size_t, TMRSyntaxTokenMap>::const_iterator found = syntaxTokenCache_.find(lineStart);
		if (found != syntaxTokenCache_.end())
			return found->second;
		return bufferModel_.tokenMapForLine(lineStart);
	}

	void formatSyntaxLine(TDrawBuffer &b, std::size_t lineStart, int hScroll, int width, int drawX,
	                      bool isDocumentLine, bool drawEofMarker, bool drawEofMarkerAsEmoji, bool displayTabs) {
		TAttrPair basePair = getColor(0x0201);
		TAttrPair changedPair = getColor(0x0505);
		TAttrPair selectionPair = getColor(0x0201);
		TMRSyntaxTokenMap tokens;
		TMRTextBufferModel::Range selection;
		std::size_t documentLength = bufferModel_.length();
		std::size_t lineEnd = lineStart;
		std::size_t cursorPos = 0;
		bool currentLine = false;
		bool currentLineInBlock = false;
		std::size_t bytePos = 0;
		int visual = 0;
		int x = 0;
		int tabSize = configuredTabSize();

		hScroll = std::max(hScroll, 0);
		width = std::max(width, 0);
		drawX = std::max(drawX, 0);
		if (!isDocumentLine) {
			TColorAttr color = tokenColor(TMRSyntaxToken::Text, false, basePair);
			b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
			if (drawEofMarker)
				drawEofMarkerGlyph(b, hScroll, width, drawX, basePair, drawEofMarkerAsEmoji);
			return;
		}
		std::string lineText = bufferModel_.lineText(lineStart);
		TStringView line(lineText.data(), lineText.size());
		tokens = syntaxTokensForLine(lineStart);
		selection = bufferModel_.selection().range();
		lineEnd = bufferModel_.nextLine(lineStart);
		cursorPos = bufferModel_.cursor();
		currentLine = (lineStart <= cursorPos && cursorPos < lineEnd) ||
		              (cursorPos == documentLength && lineStart == cursorPos && lineEnd == cursorPos);
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
				TMRSyntaxToken token =
				    tokenIndex < tokens.size() ? tokens[tokenIndex] : TMRSyntaxToken::Text;
					bool selected = selection.start <= documentPos && documentPos < selection.end;
					bool changedChar = !currentLine && !currentLineInBlock && isDirtyOffset(documentPos);
					TAttrPair effectivePair = changedChar ? changedPair : basePair;
					TAttrPair tokenPair = selected ? selectionPair : effectivePair;
					TColorAttr color = tokenColor(token, selected, tokenPair);
					int visibleWidth = nextVisual - std::max(visual, hScroll);

				if (visual < hScroll) {
					b.moveChar(static_cast<ushort>(drawX + x), ' ', color, static_cast<ushort>(visibleWidth));
				} else if (line[bytePos] == '\t') {
					if (displayTabs) {
						b.moveChar(static_cast<ushort>(drawX + x), '\x10', color, 1);
						if (visibleWidth > 1)
							b.moveChar(static_cast<ushort>(drawX + x + 1), ' ', color, static_cast<ushort>(visibleWidth - 1));
					} else {
						b.moveChar(static_cast<ushort>(drawX + x), ' ', color, static_cast<ushort>(visibleWidth));
					}
				} else {
					b.moveStr(static_cast<ushort>(drawX + x), line.substr(bytePos, next - bytePos), color,
					          static_cast<ushort>(visibleWidth));
				}
				x += visibleWidth;
			}
			visual = nextVisual;
			bytePos = next;
		}

		if (x < width) {
			TColorAttr color = tokenColor(TMRSyntaxToken::Text, false, basePair);
			b.moveChar(static_cast<ushort>(drawX + x), ' ', color, static_cast<ushort>(width - x));
		}
	}

	void drawEofMarkerGlyph(TDrawBuffer &b, int hScroll, int width, int drawX, TAttrPair basePair,
	                        bool drawEmoji) {
		static const char *const kEofMarkerText = "EOF";
		static const char *const kEofMarkerEmoji = "\xF0\x9F\x94\x9A";
		const char *marker = drawEmoji ? kEofMarkerEmoji : kEofMarkerText;
		int markerWidth = 0;
		TColorAttr markerColor = tokenColor(TMRSyntaxToken::Text, false, basePair);
		unsigned char configuredMarkerColor = 0;

		if (width <= 0 || hScroll != 0)
			return;
		if (!drawEmoji && customWindowEofMarkerColorOverrideValid_)
			markerColor = customWindowEofMarkerColorOverride_;
		else if (!drawEmoji && configuredColorSlotOverride(kMrPaletteEofMarker, configuredMarkerColor))
			markerColor = static_cast<TColorAttr>(configuredMarkerColor);
		markerWidth = std::max(1, strwidth(marker));
		markerWidth = std::min(markerWidth, width);
		b.moveStr(static_cast<ushort>(drawX), marker, markerColor, static_cast<ushort>(markerWidth));
	}

	bool adoptCommittedDocument(const TMRTextBufferModel::Document &document, std::size_t cursorPos,
	                            std::size_t selStart, std::size_t selEnd, bool modifiedState,
	                            const TMRTextBufferModel::DocumentChangeSet *changeSet = nullptr) {
		cursorPos = std::min(cursorPos, document.length());
		selStart = std::min(selStart, document.length());
		selEnd = std::min(selEnd, document.length());
		if (selEnd < selStart)
			std::swap(selStart, selEnd);

		bufferModel_.document() = document;
		resetSyntaxWarmupState(true);
		invalidateMiniMapCache(false);
		refreshSyntaxContext();
		cursorPos = canonicalCursorOffset(cursorPos);
		selStart = canonicalCursorOffset(selStart);
		selEnd = canonicalCursorOffset(selEnd);
		bufferModel_.setCursorAndSelection(cursorPos, selStart, selEnd);
		bufferModel_.setModified(modifiedState);
		if (!modifiedState)
			clearDirtyRanges();
		else if (changeSet != nullptr && changeSet->changed) {
			remapDirtyRangesForAppliedChange(*changeSet);
			addDirtyRange(changeSet->touchedRange);
		}
		selectionAnchor_ = selStart;
		updateMetrics();
		scheduleLineIndexWarmupIfNeeded();
		scheduleSyntaxWarmupIfNeeded();
		ensureCursorVisible(false);
		updateIndicator();
		drawView();
		return true;
	}

	TIndicator *indicator_;
	bool readOnly_;
	bool customWindowEofMarkerColorOverrideValid_ = false;
	TColorAttr customWindowEofMarkerColorOverride_ = 0;
	bool insertMode_;
	bool autoIndent_;
	char fileName[MAXPATH];
	std::string syntaxTitleHint_;
	TMRTextBufferModel bufferModel_;
	uint delCount_;
	uint insCount_;
	std::size_t selectionAnchor_;
	bool indicatorUpdateInProgress_;
	std::uint64_t lineIndexWarmupTaskId_;
	std::size_t lineIndexWarmupDocumentId_;
	std::size_t lineIndexWarmupVersion_;
	std::map<std::size_t, TMRSyntaxTokenMap> syntaxTokenCache_;
	std::uint64_t syntaxWarmupTaskId_;
	std::size_t syntaxWarmupDocumentId_;
	std::size_t syntaxWarmupVersion_;
	std::size_t syntaxWarmupTopLine_;
	std::size_t syntaxWarmupBottomLine_;
	TMRSyntaxLanguage syntaxWarmupLanguage_;
	std::uint64_t miniMapWarmupTaskId_;
	std::size_t miniMapWarmupDocumentId_;
	std::size_t miniMapWarmupVersion_;
	int miniMapWarmupRows_;
	int miniMapWarmupBodyWidth_;
	int miniMapWarmupViewportWidth_;
	bool miniMapWarmupBraille_;
	std::size_t miniMapWarmupWindowStartLine_;
	std::size_t miniMapWarmupWindowLineCount_;
	bool miniMapWarmupReschedulePending_;
	MiniMapRenderCache miniMapCache_;
	std::size_t miniMapInitialRenderReportedDocumentId_;
	std::vector<TMRTextBufferModel::Range> dirtyRanges_;
	LoadTiming lastLoadTiming_;

	void clearDirtyRanges() noexcept {
		dirtyRanges_.clear();
	}

	void normalizeDirtyRanges() {
		std::sort(dirtyRanges_.begin(), dirtyRanges_.end(),
		          [](const TMRTextBufferModel::Range &a, const TMRTextBufferModel::Range &b) {
			          return a.start < b.start || (a.start == b.start && a.end < b.end);
		          });
		std::vector<TMRTextBufferModel::Range> merged;
		for (const TMRTextBufferModel::Range &item : dirtyRanges_) {
			if (merged.empty() || item.start > merged.back().end)
				merged.push_back(item);
			else if (item.end > merged.back().end)
				merged.back().end = item.end;
		}
		dirtyRanges_.swap(merged);
	}

	void pushMappedDirtyRange(std::vector<TMRTextBufferModel::Range> &mapped, std::size_t start,
	                          std::size_t end, std::size_t maxLength) {
		start = std::min(start, maxLength);
		end = std::min(end, maxLength);
		if (end <= start)
			return;
		mapped.push_back(TMRTextBufferModel::Range(start, end));
	}

	void remapDirtyRangesForAppliedChange(const TMRTextBufferModel::DocumentChangeSet &change) {
		const std::size_t oldLength = change.oldLength;
		const std::size_t newLength = change.newLength;
		const TMRTextBufferModel::Range touched = change.touchedRange.normalized();
		const long long delta = static_cast<long long>(newLength) - static_cast<long long>(oldLength);
		const std::size_t touchedLength = touched.length();
		const std::size_t editStart = std::min(touched.start, oldLength);
		std::size_t replacedOldLength = touchedLength;

		if (dirtyRanges_.empty())
			return;
		if (delta >= 0) {
			const std::size_t deltaUnsigned = static_cast<std::size_t>(delta);
			replacedOldLength = touchedLength > deltaUnsigned ? touchedLength - deltaUnsigned : 0;
		}
		if (replacedOldLength > oldLength - editStart)
			replacedOldLength = oldLength - editStart;
		const std::size_t oldEditEnd = editStart + replacedOldLength;

		std::vector<TMRTextBufferModel::Range> mapped;
		mapped.reserve(dirtyRanges_.size() + 2);

		for (std::size_t i = 0; i < dirtyRanges_.size(); ++i) {
			TMRTextBufferModel::Range range = dirtyRanges_[i].clamped(oldLength).normalized();

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

		dirtyRanges_.swap(mapped);
		normalizeDirtyRanges();
	}

	void addDirtyRange(TMRTextBufferModel::Range range) {
		if (bufferModel_.length() == 0)
			return;
		range = range.clamped(bufferModel_.length());
		range.normalize();
		if (range.empty()) {
			std::size_t point = std::min(range.start, bufferModel_.length() - 1);
			range = TMRTextBufferModel::Range(point, point + 1);
		}
		dirtyRanges_.push_back(range);
		normalizeDirtyRanges();
	}

	bool isDirtyOffset(std::size_t pos) const noexcept {
		if (dirtyRanges_.empty() || bufferModel_.length() == 0)
			return false;
		if (pos >= bufferModel_.length())
			return false;
		for (const TMRTextBufferModel::Range &item : dirtyRanges_) {
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
