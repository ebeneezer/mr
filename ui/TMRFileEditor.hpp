#ifndef TMRFILEEDITOR_HPP
#define TMRFILEEDITOR_HPP

#define Uses_TScroller
#define Uses_TEditor
#define Uses_TIndicator
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_MsgBox
#include <tvision/tv.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "MRCoprocessor.hpp"
#include "TMRIndicator.hpp"
#include "TMRTextBufferModel.hpp"

class TMRFileEditor : public TScroller {
  public:
	TMRFileEditor(const TRect &bounds, TScrollBar *aHScrollBar, TScrollBar *aVScrollBar,
	              TIndicator *aIndicator, TStringView aFileName) noexcept
	    : TScroller(bounds, aHScrollBar, aVScrollBar), indicator_(aIndicator), readOnly_(false),
	      insertMode_(true), autoIndent_(false), syntaxTitleHint_(), bufferModel_(), delCount_(0),
	      insCount_(0), selectionAnchor_(0), indicatorUpdateInProgress_(false),
	      lineIndexWarmupTaskId_(0), lineIndexWarmupDocumentId_(0), lineIndexWarmupVersion_(0),
	      syntaxTokenCache_(), syntaxWarmupTaskId_(0), syntaxWarmupDocumentId_(0),
	      syntaxWarmupVersion_(0), syntaxWarmupTopLine_(0), syntaxWarmupBottomLine_(0),
	      syntaxWarmupLanguage_(TMRSyntaxLanguage::PlainText) {
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

	bool lineIndexWarmupPending() const noexcept {
		return lineIndexWarmupTaskId_ != 0;
	}

	bool syntaxWarmupPending() const noexcept {
		return syntaxWarmupTaskId_ != 0;
	}

	bool usesApproximateMetrics() const noexcept {
		return useApproximateLargeFileMetrics();
	}

	void setInsertModeEnabled(bool on) {
		insertMode_ = on;
		syncFromEditorState(false);
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
		return pos >= bufferModel_.length() ? bufferModel_.length() : pos + 1;
	}

	std::size_t prevCharOffset(std::size_t pos) noexcept {
		return pos > 0 ? pos - 1 : 0;
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
		std::size_t lineEnd = bufferModel_.lineEnd(lineStart);
		std::size_t p = lineStart;
		int visual = 0;
		int target = std::max(pos, 0);

		while (p < lineEnd) {
			int width = displayWidthForByte(bufferModel_.charAt(p), visual);
			if (visual + width > target)
				break;
			visual += width;
			++p;
		}
		return p;
	}

	int charColumn(std::size_t start, std::size_t pos) const noexcept {
		std::size_t p = bufferModel_.lineStart(start);
		std::size_t end = std::min(pos, bufferModel_.length());
		int visual = 0;

		while (p < end) {
			visual += displayWidthForByte(bufferModel_.charAt(p), visual);
			++p;
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

	std::string snapshotText() const {
		return bufferModel_.text();
	}

	TMRTextBufferModel::ReadSnapshot readSnapshot() const {
		return bufferModel_.readSnapshot();
	}

	std::size_t documentId() const noexcept {
		return bufferModel_.documentId();
	}

	std::size_t documentVersion() const noexcept {
		return bufferModel_.version();
	}

	bool applyLineIndexWarmup(const mr::editor::LineIndexWarmupData &warmup,
	                          std::size_t expectedVersion) {
		if (!bufferModel_.adoptLineIndexWarmup(warmup, expectedVersion))
			return false;
		lineIndexWarmupTaskId_ = 0;
		lineIndexWarmupDocumentId_ = 0;
		lineIndexWarmupVersion_ = 0;
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
		drawView();
		return true;
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

		if (!document.loadMappedFile(path, error))
			return false;
		setPersistentFileName(path);
		if (!adoptCommittedDocument(document, 0, 0, 0, false)) {
			clearPersistentFileName();
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
			strnzcpy(saveName, "*.*", sizeof(saveName));
		if (TEditor::editorDialog(edSaveAs, saveName) == cmCancel)
			return False;
		fexpand(saveName);
		if (!writeDocumentToPath(saveName))
			return False;
		setPersistentFileName(saveName);
		if (owner != nullptr)
			message((TView *)owner, evBroadcast, cmUpdateTitle, 0);
		setDocumentModified(false);
		return True;
	}

	bool replaceBufferData(const char *data, uint length) {
		std::string text;
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.snapshot(),
		                                                 "replace-buffer-data");
		TMRTextBufferModel::Document preview;

		if (data != nullptr && length != 0)
			text.assign(data, length);
		transaction.setText(text);
		preview = bufferModel_.document();
		if (!preview.tryApply(transaction).applied())
			return false;
		return adoptCommittedDocument(preview, 0, 0, 0, false);
	}

	bool replaceBufferText(const char *text) {
		uint length = text != nullptr ? static_cast<uint>(std::strlen(text)) : 0;
		return replaceBufferData(text, length);
	}

	bool appendBufferData(const char *data, uint length) {
		std::string text;
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.snapshot(),
		                                                 "append-buffer-data");
		TMRTextBufferModel::Document preview;
		std::size_t endPtr = bufferModel_.length();

		if (length == 0)
			return true;
		if (data != nullptr)
			text.assign(data, length);
		transaction.insert(endPtr, text);
		preview = bufferModel_.document();
		if (!preview.tryApply(transaction).applied())
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
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.snapshot(),
		                                                 "replace-range-select");
		TMRTextBufferModel::Document preview;
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
		if (!preview.tryApply(transaction).applied())
			return false;
		return adoptCommittedDocument(preview, range.start, range.start, range.start + text.size(), true);
	}

	bool insertBufferText(const std::string &text) {
		std::size_t start = bufferModel_.cursor();
		std::size_t end = start;
		TMRTextBufferModel::Range range;
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.snapshot(),
		                                                 "insert-buffer-text");
		TMRTextBufferModel::Document preview;

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
		if (!preview.tryApply(transaction).applied())
			return false;
		bumpUndoCounters(range.length(), text.size());
		start = range.start + text.size();
		return adoptCommittedDocument(preview, start, start, start, true);
	}

	bool replaceCurrentLineText(const std::string &text) {
		std::size_t start = bufferModel_.lineStart(bufferModel_.cursor());
		std::size_t end = bufferModel_.lineEnd(bufferModel_.cursor());
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.snapshot(),
		                                                 "replace-current-line");
		TMRTextBufferModel::Document preview;

		if (readOnly_)
			return false;
		transaction.replace(TMRTextBufferModel::Range(start, end), text);
		preview = bufferModel_.document();
		if (!preview.tryApply(transaction).applied())
			return false;
		bumpUndoCounters(end - start, text.size());
		return adoptCommittedDocument(preview, start, start, start, true);
	}

	bool deleteCharsAtCursor(int count) {
		std::size_t start = bufferModel_.cursor();
		std::size_t end = start;
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.snapshot(),
		                                                 "delete-chars-at-cursor");
		TMRTextBufferModel::Document preview;

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
		if (!preview.tryApply(transaction).applied())
			return false;
		bumpUndoCounters(end - start, 0);
		return adoptCommittedDocument(preview, start, start, start, true);
	}

	bool deleteCurrentLineText() {
		std::size_t start = bufferModel_.lineStart(bufferModel_.cursor());
		std::size_t end = bufferModel_.nextLine(bufferModel_.cursor());
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.snapshot(),
		                                                 "delete-current-line");
		TMRTextBufferModel::Document preview;

		if (readOnly_)
			return false;
		transaction.erase(TMRTextBufferModel::Range(start, end));
		preview = bufferModel_.document();
		if (!preview.tryApply(transaction).applied())
			return false;
		bumpUndoCounters(end - start, 0);
		return adoptCommittedDocument(preview, start, start, start, true);
	}

	bool replaceWholeBuffer(const std::string &text, std::size_t cursorPos) {
		TMRTextBufferModel::StagedTransaction transaction(bufferModel_.snapshot(),
		                                                 "replace-whole-buffer");
		TMRTextBufferModel::Document preview;

		if (readOnly_)
			return false;
		transaction.setText(text);
		preview = bufferModel_.document();
		if (!preview.tryApply(transaction).applied())
			return false;
		bumpUndoCounters(bufferModel_.length(), text.size());
		cursorPos = std::min(cursorPos, text.size());
		return adoptCommittedDocument(preview, cursorPos, cursorPos, cursorPos, true);
	}

	bool newLineWithIndent(const std::string &fill) {
		return insertBufferText(std::string("\n") + fill);
	}

	virtual void draw() override {
		std::size_t linePtr = lineStartForIndex(static_cast<std::size_t>(std::max(delta.y, 0)));

		for (int y = 0; y < size.y; ++y) {
			TDrawBuffer buffer;
			formatSyntaxLine(buffer, linePtr, delta.x, size.x);
			writeBuf(0, y, size.x, 1, buffer);
			if (linePtr < bufferModel_.length())
				linePtr = bufferModel_.nextLine(linePtr);
		}
		scheduleSyntaxWarmupIfNeeded();
		updateIndicator();
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
		if ((aState & (sfActive | sfSelected)) != 0 && indicator_ != nullptr) {
			if ((state & (sfActive | sfSelected)) != 0)
				indicator_->show();
			else
				indicator_->hide();
		}
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

	static int displayWidthForByte(char ch, int visualColumn) noexcept {
		if (ch == '\t') {
			int nextStop = ((visualColumn / 8) + 1) * 8;
			return std::max(1, nextStop - visualColumn);
		}
		return 1;
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

		if ((TEditor::editorFlags & efBackupFiles) != 0) {
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

		for (std::size_t i = 0; i < bufferModel_.document().pieceCount(); ++i) {
			mr::editor::PieceChunkView chunk = bufferModel_.document().pieceChunk(i);
			writeChunk(out, chunk.data, chunk.length);
			if (!out) {
				TEditor::editorDialog(edWriteError, targetPath);
				return false;
			}
		}
		return true;
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

		while (true) {
			std::size_t end = bufferModel_.lineEnd(pos);
			int width = 0;
			for (std::size_t p = pos; p < end; ++p)
				width += displayWidthForByte(bufferModel_.charAt(p), width);
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
		const std::size_t minimum = static_cast<std::size_t>(std::max(size.y, 1));
		const std::size_t margin = static_cast<std::size_t>(std::max(size.y * 4, 256));
		std::size_t limitValue = std::max<std::size_t>(
		    estimated,
		    std::max<std::size_t>(currentLine + margin,
		                          static_cast<std::size_t>(std::max(delta.y, 0)) + margin));
		limitValue = std::max<std::size_t>(limitValue, minimum);
		return static_cast<int>(std::min<std::size_t>(limitValue, static_cast<std::size_t>(INT_MAX)));
	}

	int dynamicLargeFileWidthLimit() const noexcept {
		const std::size_t cursor = bufferModel_.cursor();
		const int cursorColumn = charColumn(bufferModel_.lineStart(cursor), cursor);
		return std::max(std::max(size.x, 256), std::max(delta.x + size.x + 64, cursorColumn + 64));
	}

	void scheduleLineIndexWarmupIfNeeded() {
		if (!bufferModel_.document().hasMappedOriginal() || bufferModel_.document().exactLineCountKnown()) {
			lineIndexWarmupTaskId_ = 0;
				lineIndexWarmupDocumentId_ = 0;
				lineIndexWarmupVersion_ = 0;
				return;
			}

			const std::size_t docId = bufferModel_.documentId();
			const std::size_t version = bufferModel_.version();
			if (lineIndexWarmupTaskId_ != 0 && lineIndexWarmupDocumentId_ == docId &&
			    lineIndexWarmupVersion_ == version)
				return;

			TMRTextBufferModel::ReadSnapshot snapshot = bufferModel_.readSnapshot();
			lineIndexWarmupDocumentId_ = docId;
			lineIndexWarmupVersion_ = version;
			lineIndexWarmupTaskId_ = mr::coprocessor::globalCoprocessor().submit(
			    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::LineIndexWarmup, docId, version,
			    "line-index-warmup",
			    [snapshot](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
				    mr::coprocessor::Result result;
				    result.task = info;
				    if (stopToken.stop_requested()) {
					    result.status = mr::coprocessor::TaskStatus::Cancelled;
					    return result;
				    }
				    mr::editor::LineIndexWarmupData warmup = snapshot.completeLineIndexWarmup();
				    if (stopToken.stop_requested()) {
					    result.status = mr::coprocessor::TaskStatus::Cancelled;
					    return result;
				    }
				    result.status = mr::coprocessor::TaskStatus::Completed;
				    result.payload =
				        std::make_shared<mr::coprocessor::LineIndexWarmupPayload>(warmup);
				    return result;
			    });
	}

	void scheduleSyntaxWarmupIfNeeded() {
		if (bufferModel_.language() == TMRSyntaxLanguage::PlainText || size.y <= 0) {
			resetSyntaxWarmupState(true);
			return;
		}

		const std::size_t docId = bufferModel_.documentId();
		const std::size_t version = bufferModel_.version();
		const TMRSyntaxLanguage language = bufferModel_.language();
		const std::size_t topLine = static_cast<std::size_t>(std::max(delta.y - 4, 0));
		const int rowBudget = std::max(size.y + 8, 8);
		std::vector<std::size_t> lineStarts = syntaxWarmupLineStarts(topLine, rowBudget);
		if (lineStarts.empty())
			return;

		const std::size_t bottomLine = topLine + lineStarts.size();
		if (hasSyntaxTokensForLineStarts(lineStarts)) {
			syntaxWarmupTaskId_ = 0;
			syntaxWarmupDocumentId_ = docId;
			syntaxWarmupVersion_ = version;
			syntaxWarmupTopLine_ = topLine;
			syntaxWarmupBottomLine_ = bottomLine;
			syntaxWarmupLanguage_ = language;
			return;
		}

		if (syntaxWarmupTaskId_ != 0 && syntaxWarmupDocumentId_ == docId &&
		    syntaxWarmupVersion_ == version && syntaxWarmupLanguage_ == language &&
		    topLine >= syntaxWarmupTopLine_ && bottomLine <= syntaxWarmupBottomLine_)
			return;

		TMRTextBufferModel::ReadSnapshot snapshot = bufferModel_.readSnapshot();
		syntaxWarmupDocumentId_ = docId;
		syntaxWarmupVersion_ = version;
		syntaxWarmupTopLine_ = topLine;
		syntaxWarmupBottomLine_ = bottomLine;
		syntaxWarmupLanguage_ = language;
		syntaxWarmupTaskId_ = mr::coprocessor::globalCoprocessor().submit(
		    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::SyntaxWarmup, docId, version,
		    "syntax-warmup",
		    [snapshot, language, lineStarts](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
			    mr::coprocessor::Result result;
			    std::vector<mr::coprocessor::SyntaxWarmLine> warmed;
			    result.task = info;
			    if (stopToken.stop_requested()) {
				    result.status = mr::coprocessor::TaskStatus::Cancelled;
				    return result;
			    }
			    warmed.reserve(lineStarts.size());
			    for (std::size_t i = 0; i < lineStarts.size(); ++i) {
				    if (stopToken.stop_requested()) {
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
	}

	void updateMetrics() {
			int limitX = 1;
			int limitY = 1;

		if (useApproximateLargeFileMetrics()) {
			limitX = dynamicLargeFileWidthLimit();
			limitY = dynamicLargeFileLineLimit();
		} else {
			limitX = longestLineWidth();
			limitY = std::max<int>(1, static_cast<int>(bufferModel_.lineCount()));
		}

			int maxX = std::max(0, limitX - size.x);
		int maxY = std::max(0, limitY - size.y);
		int newDeltaX = std::min(std::max(delta.x, 0), maxX);
		int newDeltaY = std::min(std::max(delta.y, 0), maxY);

		setLimit(limitX, limitY);
		if (newDeltaX != delta.x || newDeltaY != delta.y)
			scrollTo(newDeltaX, newDeltaY);
	}

	void updateIndicator() {
		if (indicatorUpdateInProgress_)
			return;
		indicatorUpdateInProgress_ = true;
		std::size_t cursor = bufferModel_.cursor();
		unsigned long visualColumn =
		    static_cast<unsigned long>(charColumn(bufferModel_.lineStart(cursor), cursor));
		unsigned long line = static_cast<unsigned long>(bufferModel_.lineIndex(cursor));
		long long localX = static_cast<long long>(visualColumn) - delta.x;
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

		if ((state & (sfFocused | sfSelected)) != 0 && localX >= 0 && localX < size.x && localY >= 0 &&
		    localY < size.y) {
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

		if (visualColumn < targetX)
			targetX = visualColumn;
		else if (visualColumn >= targetX + size.x)
			targetX = visualColumn - size.x + 1;

		if (centerCursor)
			targetY = std::max(0, line - size.y / 2);
		else if (line < targetY)
			targetY = line;
		else if (line >= targetY + size.y)
			targetY = line - size.y + 1;

		scrollTo(targetX, targetY);
	}

	void moveCursor(std::size_t target, bool extendSelection, bool centerCursor) {
		target = std::min(target, bufferModel_.length());
		if (extendSelection) {
			std::size_t anchor =
			    bufferModel_.hasSelection() ? bufferModel_.selection().anchor : bufferModel_.cursor();
			selectionAnchor_ = anchor;
			bufferModel_.setCursorAndSelection(target, anchor, target);
		} else {
			selectionAnchor_ = target;
			bufferModel_.setCursorAndSelection(target, target, target);
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
			insertBufferText("\t");
		else
			insertBufferText(std::string(1, static_cast<char>(event.keyDown.charScan.charCode)));
		clearEvent(event);
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
				moveCursor(lineMoveOffset(cursorOffset(), -(size.y - 1)), extend, true);
				break;
			case kbPgDn:
				moveCursor(lineMoveOffset(cursorOffset(), size.y - 1), extend, true);
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
				moveCursor(lineMoveOffset(cursorOffset(), -(size.y - 1)), false, true);
				break;
			case cmPageDown:
				moveCursor(lineMoveOffset(cursorOffset(), size.y - 1), false, true);
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
				else if (mouse.y >= size.y)
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
		int row = std::max(0, local.y) + delta.y;
		int column = std::max(0, local.x) + delta.x;
		std::size_t start = lineStartForIndex(static_cast<std::size_t>(std::max(row, 0)));
		return charPtrOffset(start, column);
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

	Boolean confirmSaveOrDiscardUntitled() {
		switch (messageBox(mfConfirmation | mfYesNoCancel,
		                  "Window has unsaved changes.\n\nYes = Save As  No = Discard  Cancel = Abort")) {
			case cmYes:
				return saveAsWithPrompt();
			case cmNo:
				setDocumentModified(false);
				return True;
			default:
				return False;
		}
	}

	Boolean confirmSaveOrDiscardNamed() {
		switch (messageBox(mfConfirmation | mfYesNoCancel,
		                  "Save changes to:\n%s\n\nYes = Save  No = Discard  Cancel = Abort", fileName)) {
			case cmYes:
				return saveInPlace();
			case cmNo:
				setDocumentModified(false);
				return True;
			default:
				return False;
		}
	}

	TColorAttr tokenColor(TMRSyntaxToken token, bool selected) noexcept {
		TAttrPair pair = getColor(0x0201);
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
		if (clearCache)
			syntaxTokenCache_.clear();
		syntaxWarmupTaskId_ = 0;
		syntaxWarmupDocumentId_ = 0;
		syntaxWarmupVersion_ = 0;
		syntaxWarmupTopLine_ = 0;
		syntaxWarmupBottomLine_ = 0;
		syntaxWarmupLanguage_ = TMRSyntaxLanguage::PlainText;
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

	void formatSyntaxLine(TDrawBuffer &b, std::size_t lineStart, int hScroll, int width) {
		TMRSyntaxTokenMap tokens = syntaxTokensForLine(lineStart);
		TMRTextBufferModel::Range selection = bufferModel_.selection().range();
		std::size_t lineEnd = bufferModel_.lineEnd(lineStart);
		int visual = 0;
		int x = 0;

		hScroll = std::max(hScroll, 0);
		width = std::max(width, 0);
		for (std::size_t p = lineStart; p < lineEnd && x < width; ++p) {
			char ch = bufferModel_.charAt(p);
			int charWidth = displayWidthForByte(ch, visual);
			int nextVisual = visual + charWidth;

			if (nextVisual > hScroll) {
				std::size_t tokenIndex = p >= lineStart ? p - lineStart : 0;
				TMRSyntaxToken token =
				    tokenIndex < tokens.size() ? tokens[tokenIndex] : TMRSyntaxToken::Text;
				TColorAttr color = tokenColor(token, selection.start <= p && p < selection.end);
				int visibleWidth = nextVisual - std::max(visual, hScroll);

				if (ch == '\t' || visual < hScroll)
					b.moveChar(static_cast<ushort>(x), ' ', color, static_cast<ushort>(visibleWidth));
				else
					b.moveChar(static_cast<ushort>(x), ch, color, static_cast<ushort>(visibleWidth));
				x += visibleWidth;
			}
			visual = nextVisual;
		}

		if (x < width) {
			TColorAttr color = tokenColor(TMRSyntaxToken::Text, false);
			b.moveChar(static_cast<ushort>(x), ' ', color, static_cast<ushort>(width - x));
		}
	}

	bool adoptCommittedDocument(const TMRTextBufferModel::Document &document, std::size_t cursorPos,
	                            std::size_t selStart, std::size_t selEnd, bool modifiedState) {
		cursorPos = std::min(cursorPos, document.length());
		selStart = std::min(selStart, document.length());
		selEnd = std::min(selEnd, document.length());
		if (selEnd < selStart)
			std::swap(selStart, selEnd);

		bufferModel_.document() = document;
		resetSyntaxWarmupState(true);
		refreshSyntaxContext();
		bufferModel_.setCursorAndSelection(cursorPos, selStart, selEnd);
		bufferModel_.setModified(modifiedState);
		selectionAnchor_ = selStart;
		updateMetrics();
		scheduleLineIndexWarmupIfNeeded();
		scheduleSyntaxWarmupIfNeeded();
		ensureCursorVisible(true);
		updateIndicator();
		drawView();
		return true;
	}

	TIndicator *indicator_;
	bool readOnly_;
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
};

#endif
