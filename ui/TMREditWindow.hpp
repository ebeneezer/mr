#ifndef TMREDITWINDOW_HPP
#define TMREDITWINDOW_HPP

#define Uses_TWindow
#define Uses_TScrollBar
#define Uses_TIndicator
#define Uses_TFileEditor
#define Uses_TRect
#define Uses_TEvent
#define Uses_TEditor
#define Uses_TObject
#include <tvision/tv.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

#include "TMRFrame.hpp"
#include "TMRIndicator.hpp"
#include "TMRTextBuffer.hpp"

void mrTraceCoprocessorTaskCancel(int bufferId, std::uint64_t taskId);

class TMREditWindow : public TWindow {
  public:
	enum WindowRole {
		wrText = 0,
		wrFile,
		wrCommunicationCommand,
		wrCommunicationPipe,
		wrCommunicationDevice,
		wrLog,
		wrHelp
	};

	TMREditWindow(const TRect &bounds, const char *title, int aNumber)
	    : TWindowInit(&TMREditWindow::initFrame), TWindow(bounds, 0, aNumber), vScrollBar(nullptr),
	      hScrollBar(nullptr), indicator(nullptr), editor(nullptr), bufferId_(allocateBufferId()),
	      firstSaveDone_(false), temporaryFileUsed_(false), temporaryFileName_(), indentLevel_(1),
	      blockMode_(bmNone), blockMarkingOn_(false), blockAnchor_(0), blockEnd_(0),
	      trackedCoprocessorTasks_(), windowRole_(wrText), windowRoleDetail_() {
		options |= ofTileable;

		std::strncpy(displayTitle, (title != nullptr && *title != '\0') ? title : "Untitled",
		             sizeof(displayTitle) - 1);
		displayTitle[sizeof(displayTitle) - 1] = '\0';

		hScrollBar = new TScrollBar(TRect(39, size.y - 1, size.x - 2, size.y));
		hScrollBar->hide();
		insert(hScrollBar);

		vScrollBar = new TScrollBar(TRect(size.x - 1, 1, size.x, size.y - 1));
		vScrollBar->hide();
		insert(vScrollBar);

		indicator = new TMRIndicator(TRect(2, size.y - 1, 38, size.y));
		indicator->hide();
		insert(indicator);

		TRect r(getExtent());
		r.grow(-1, -1);

		editor = createEditor(r, "");
		insert(editor);
		refreshSyntaxContext();
	}

	virtual ~TMREditWindow() override {
		cancelTrackedCoprocessorTasks();
	}

	virtual TPalette &getPalette() const override {
		return TWindow::getPalette();
	}

	virtual const char *getTitle(short) override {
		if (editor != nullptr && editor->hasPersistentFileName())
			return editor->persistentFileName();
		return displayTitle;
	}

	virtual void handleEvent(TEvent &event) override {
		TWindow::handleEvent(event);
		if (event.what == evBroadcast && event.message.command == cmUpdateTitle) {
			if (frame != nullptr)
				frame->drawView();
			clearEvent(event);
		}
	}

	bool loadFromFile(const char *fileName) {
		std::string expandedName;
		std::string loadError;

		if (editor == nullptr || fileName == nullptr || *fileName == '\0')
			return false;

		expandedName = fileName;
		if (expandedName.size() >= editor->persistentFileNameCapacity())
			return false;
		char expandedPath[MAXPATH];
		strnzcpy(expandedPath, expandedName.c_str(), sizeof(expandedPath));
		fexpand(expandedPath);
		expandedName = expandedPath;

		if (!editor->loadMappedFile(expandedName.c_str(), loadError))
			return false;

		resetTransientEditorState();
		setReadOnly(isExistingPathReadOnly(editor->persistentFileName()));
		temporaryFileUsed_ = false;
		temporaryFileName_.clear();
		setWindowRole(wrFile);
		updateTitleFromEditor();
		return true;
	}

	bool loadTextBuffer(const char *text, const char *title = nullptr) {
		if (editor == nullptr)
			return false;
		if (!editor->replaceBufferText(text))
			return false;

		resetTransientEditorState();
		setReadOnly(false);
		temporaryFileUsed_ = false;
		temporaryFileName_.clear();
		editor->clearPersistentFileName();
		setWindowRole(wrText);
		if (title != nullptr && *title != '\0')
			setDisplayTitle(title);
		else
			updateTitleFromEditor();
		refreshSyntaxContext();
		return true;
	}

	bool saveCurrentFile() {
		if (editor == nullptr || isReadOnly() || !editor->canSaveInPlace())
			return false;

		bool ok = editor->saveInPlace() == True;
		if (ok) {
			firstSaveDone_ = true;
			temporaryFileUsed_ = false;
			temporaryFileName_.clear();
			setWindowRole(wrFile);
			updateTitleFromEditor();
		}
		return ok;
	}

	bool saveCurrentFileAs() {
		if (editor == nullptr || isReadOnly() || !editor->canSaveAs())
			return false;

		bool ok = editor->saveAsWithPrompt() == True;
		if (ok) {
			firstSaveDone_ = true;
			temporaryFileUsed_ = false;
			temporaryFileName_.clear();
			setReadOnly(isExistingPathReadOnly(editor->persistentFileName()));
			setWindowRole(wrFile);
			updateTitleFromEditor();
		}
		return ok;
	}

	const char *currentFileName() const {
		if (editor != nullptr && editor->hasPersistentFileName())
			return editor->persistentFileName();
		return "";
	}

	TMRFileEditor *getEditor() const {
		return editor;
	}

	TMRTextBuffer buffer() const {
		return TMRTextBuffer(editor);
	}

	bool isBufferEmpty() const {
		return buffer().isEmpty();
	}

	std::size_t bufferLength() const {
		return buffer().length();
	}

	std::size_t bufferLineCount() const {
		return buffer().lineCount();
	}

	bool hasSelection() const {
		return buffer().hasSelection();
	}

	bool hasUndoHistory() const {
		return buffer().hasUndoHistory();
	}

	TPoint cursorPoint() const {
		return buffer().cursorPoint();
	}

	unsigned long cursorLineNumber() const {
		return buffer().cursorLineNumber();
	}

	unsigned long cursorColumnNumber() const {
		return buffer().cursorColumnNumber();
	}

	std::size_t cursorOffset() const noexcept {
		return editor != nullptr ? editor->cursorOffset() : 0;
	}

	std::size_t selectionLength() const noexcept {
		return editor != nullptr ? editor->selectionLength() : 0;
	}

	const char *syntaxLanguageName() const {
		return editor != nullptr ? editor->syntaxLanguageName() : "Plain Text";
	}

	TMRSyntaxLanguage syntaxLanguage() const {
		return editor != nullptr ? editor->syntaxLanguage() : TMRSyntaxLanguage::PlainText;
	}

	bool hasPersistentFileName() const {
		return editor != nullptr && editor->hasPersistentFileName();
	}

	bool canSaveInPlace() const {
		return editor != nullptr && editor->canSaveInPlace();
	}

	bool canSaveAs() const {
		return editor != nullptr && editor->canSaveAs();
	}

	bool isReadOnly() const {
		return editor != nullptr && editor->isReadOnly();
	}

	void setReadOnly(bool readOnly) {
		if (editor != nullptr)
			editor->setReadOnly(readOnly);
		if (indicator != nullptr)
			indicator->setReadOnly(readOnly);
		if (readOnly && editor != nullptr)
			editor->setDocumentModified(false);
	}

	bool hasBeenSavedInSession() const {
		return firstSaveDone_;
	}

	bool eofInMemory() const {
		return editor != nullptr;
	}

	int bufferId() const {
		return bufferId_;
	}

	WindowRole windowRole() const noexcept {
		return windowRole_;
	}

	const char *windowRoleName() const noexcept {
		switch (windowRole_) {
			case wrFile:
				return "File text";
			case wrCommunicationCommand:
				return "Communication command";
			case wrCommunicationPipe:
				return "Communication pipe";
			case wrCommunicationDevice:
				return "Communication device";
			case wrLog:
				return "Log window";
			case wrHelp:
				return "Help window";
			case wrText:
			default:
				return "Text window";
		}
	}

	void setWindowRole(WindowRole role, const std::string &detail = std::string()) {
		windowRole_ = role;
		windowRoleDetail_ = detail;
	}

	const std::string &windowRoleDetail() const noexcept {
		return windowRoleDetail_;
	}

	bool isTemporaryFile() const {
		return temporaryFileUsed_;
	}

	const char *temporaryFileName() const {
		return temporaryFileName_.c_str();
	}

	bool isFileChanged() const {
		return editor != nullptr && !isReadOnly() && editor->isDocumentModified();
	}

	void setFileChanged(bool changed) {
		if (editor != nullptr)
			editor->setDocumentModified(changed && !isReadOnly());
	}

	void setCurrentFileName(const char *fileName) {
		if (editor == nullptr)
			return;

		if (fileName == nullptr || *fileName == '\0')
			editor->clearPersistentFileName();
		else {
			editor->setPersistentFileName(fileName);
			setWindowRole(wrFile);
		}
		if ((fileName == nullptr || *fileName == '\0') && windowRole_ == wrFile)
			setWindowRole(wrText);
		updateTitleFromEditor();
		refreshSyntaxContext();
	}

	bool confirmAbandonForReload() {
		if (editor == nullptr)
			return false;
		return editor->valid(cmClose) == True;
	}

	bool replaceTextBuffer(const char *text, const char *title = nullptr) {
		if (editor == nullptr || !editor->replaceBufferText(text))
			return false;
		if (title != nullptr && *title != '\0')
			setDisplayTitle(title);
		return true;
	}

	bool appendTextBuffer(const char *text) {
		if (editor == nullptr)
			return false;
		return editor->appendBufferText(text);
	}

	void setDisplayTitle(const char *title) {
		std::strncpy(displayTitle, (title != nullptr && *title != '\0') ? title : "Untitled",
		             sizeof(displayTitle) - 1);
		displayTitle[sizeof(displayTitle) - 1] = '\0';
		refreshSyntaxContext();
		message(owner, evBroadcast, cmUpdateTitle, 0);
	}

	void trackCoprocessorTask(std::uint64_t taskId) {
		if (taskId == 0)
			return;
		for (std::size_t i = 0; i < trackedCoprocessorTasks_.size(); ++i)
			if (trackedCoprocessorTasks_[i] == taskId)
				return;
		trackedCoprocessorTasks_.push_back(taskId);
			updateTaskMarkers();
	}

	std::size_t trackedCoprocessorTaskCount() const noexcept {
		return trackedCoprocessorTasks_.size();
	}

	std::uint64_t trackedCoprocessorTaskId(std::size_t index) const noexcept {
		return index < trackedCoprocessorTasks_.size() ? trackedCoprocessorTasks_[index] : 0;
	}

	std::size_t originalBufferLength() const noexcept {
		return editor != nullptr ? editor->originalBufferLength() : 0;
	}

	std::size_t addBufferLength() const noexcept {
		return editor != nullptr ? editor->addBufferLength() : 0;
	}

	std::size_t pieceCount() const noexcept {
		return editor != nullptr ? editor->pieceCount() : 0;
	}

	std::size_t documentVersion() const noexcept {
		return editor != nullptr ? editor->documentVersion() : 0;
	}

	bool hasMappedOriginalSource() const noexcept {
		return editor != nullptr && editor->hasMappedOriginalSource();
	}

	const std::string &mappedOriginalPath() const noexcept {
		if (editor != nullptr)
			return editor->mappedOriginalPath();
		return emptyString();
	}

	std::size_t estimatedLineCount() const noexcept {
		return editor != nullptr ? editor->estimatedLineCount() : 1;
	}

	bool exactLineCountKnown() const noexcept {
		return editor != nullptr && editor->exactLineCountKnown();
	}

	std::uint64_t pendingLineIndexWarmupTaskId() const noexcept {
		return editor != nullptr ? editor->pendingLineIndexWarmupTaskId() : 0;
	}

	std::uint64_t pendingSyntaxWarmupTaskId() const noexcept {
		return editor != nullptr ? editor->pendingSyntaxWarmupTaskId() : 0;
	}

	bool usesApproximateMetrics() const noexcept {
		return editor != nullptr && editor->usesApproximateMetrics();
	}

	void releaseCoprocessorTask(std::uint64_t taskId) {
		for (std::vector<std::uint64_t>::iterator it = trackedCoprocessorTasks_.begin();
		     it != trackedCoprocessorTasks_.end(); ++it)
			if (*it == taskId) {
				trackedCoprocessorTasks_.erase(it);
				updateTaskMarkers();
				return;
			}
	}

	int indentLevel() const {
		return indentLevel_;
	}

	void setIndentLevel(int level) {
		if (level < 1)
			level = 1;
		if (level > 254)
			level = 254;
		indentLevel_ = level;
	}

	enum BlockMode { bmNone = 0, bmLine = 1, bmColumn = 2, bmStream = 3 };

	void beginLineBlock() {
		beginBlock(bmLine);
	}

	void beginColumnBlock() {
		beginBlock(bmColumn);
	}

	void beginStreamBlock() {
		beginBlock(bmStream);
	}

	void endBlock() {
		if (editor == nullptr || blockMode_ == bmNone)
			return;
		blockEnd_ = static_cast<uint>(editor->cursorOffset());
		blockMarkingOn_ = false;
		syncBlockVisual();
	}

	void clearBlock() {
		blockMode_ = bmNone;
		blockMarkingOn_ = false;
		blockAnchor_ = 0;
		blockEnd_ = 0;
		if (editor != nullptr) {
			editor->setSelectionOffsets(editor->cursorOffset(), editor->cursorOffset(), False);
			editor->revealCursor(True);
			editor->update(ufView);
		}
	}

	bool hasBlock() const {
		return blockMode_ != bmNone;
	}

	bool isBlockMarking() const {
		return blockMode_ != bmNone && blockMarkingOn_;
	}

	int blockStatus() const {
		return static_cast<int>(blockMode_);
	}

	uint blockAnchorPtr() const {
		return blockAnchor_;
	}

	uint blockEffectiveEndPtr() const {
		return effectiveBlockEnd();
	}

	int blockLine1() const {
		return normalizedBlockLine1();
	}

	int blockLine2() const {
		return normalizedBlockLine2();
	}

	int blockCol1() const {
		return normalizedBlockCol1();
	}

	int blockCol2() const {
		return normalizedBlockCol2();
	}

	void refreshBlockVisual() {
		syncBlockVisual();
	}

  private:
	TMRFileEditor *createEditor(const TRect &bounds, const char *fileName) {
		return new TMRFileEditor(bounds, hScrollBar, vScrollBar, indicator, fileName != nullptr ? fileName : "");
	}

	static int allocateBufferId() {
		static int nextId = 1;
		return nextId++;
	}

	static const std::string &emptyString() noexcept {
		static const std::string value;
		return value;
	}

	static TFrame *initFrame(TRect r) {
		return new TMRFrame(r);
	}

	static bool isExistingPathReadOnly(const char *fileName) {
		if (fileName == nullptr || *fileName == '\0')
			return false;
		if (access(fileName, F_OK) != 0)
			return false;
		return access(fileName, W_OK) != 0;
	}

	void resetTransientEditorState() {
		if (editor == nullptr)
			return;
		editor->resetUndoState();
		clearBlock();
	}

	void updateTitleFromEditor() {
		if (editor != nullptr && editor->hasPersistentFileName()) {
			std::strncpy(displayTitle, editor->persistentFileName(), sizeof(displayTitle) - 1);
			displayTitle[sizeof(displayTitle) - 1] = '\0';
		}
		refreshSyntaxContext();
		message(owner, evBroadcast, cmUpdateTitle, 0);
	}

	void refreshSyntaxContext() {
		if (editor != nullptr)
			editor->setSyntaxTitleHint(displayTitle);
	}

	void updateTaskMarkers() {
		if (indicator != nullptr)
			indicator->setTaskCount(trackedCoprocessorTasks_.size());
	}

	void cancelTrackedCoprocessorTasks() {
		for (std::size_t i = 0; i < trackedCoprocessorTasks_.size(); ++i) {
			mrTraceCoprocessorTaskCancel(bufferId_, trackedCoprocessorTasks_[i]);
			mr::coprocessor::globalCoprocessor().cancelTask(trackedCoprocessorTasks_[i]);
		}
		trackedCoprocessorTasks_.clear();
		updateTaskMarkers();
	}

	void beginBlock(BlockMode mode) {
		if (editor == nullptr)
			return;
		blockMode_ = mode;
		blockMarkingOn_ = true;
		blockAnchor_ = static_cast<uint>(editor->cursorOffset());
		blockEnd_ = static_cast<uint>(editor->cursorOffset());
		syncBlockVisual();
	}

	uint effectiveBlockEnd() const {
		if (editor != nullptr && blockMarkingOn_)
			return static_cast<uint>(editor->cursorOffset());
		return blockEnd_;
	}

	void blockPtrRange(uint &a, uint &b) const {
		a = blockAnchor_;
		b = effectiveBlockEnd();
		if (a > b)
			std::swap(a, b);
	}

	int lineNumberForPtr(uint ptr) const {
		uint pos = 0;
		int line = 1;
		if (editor == nullptr)
			return 0;
		if (ptr > editor->bufferLength())
			ptr = static_cast<uint>(editor->bufferLength());
		while (pos < ptr && pos < editor->bufferLength()) {
			uint next = static_cast<uint>(editor->nextLineOffset(pos));
			if (next <= pos || next > ptr)
				break;
			pos = next;
			++line;
		}
		return line;
	}

	int columnForPtr(uint ptr) const {
		uint start;
		if (editor == nullptr)
			return 0;
		if (ptr > editor->bufferLength())
			ptr = static_cast<uint>(editor->bufferLength());
		start = static_cast<uint>(editor->lineStartOffset(ptr));
		return editor->charColumn(start, ptr) + 1;
	}

	int normalizedBlockLine1() const {
		uint a, b;
		if (blockMode_ == bmNone)
			return 0;
		blockPtrRange(a, b);
		return lineNumberForPtr(a);
	}

	int normalizedBlockLine2() const {
		uint a, b;
		if (blockMode_ == bmNone)
			return 0;
		blockPtrRange(a, b);
		return lineNumberForPtr(b);
	}

	int normalizedBlockCol1() const {
		int aCol;
		int bCol;
		if (blockMode_ == bmNone)
			return 0;
		if (blockMode_ == bmLine)
			return 1;
		aCol = columnForPtr(blockAnchor_);
		bCol = columnForPtr(effectiveBlockEnd());
		return std::min(aCol, bCol);
	}

	int normalizedBlockCol2() const {
		int aCol;
		int bCol;
		if (blockMode_ == bmNone)
			return 0;
		if (blockMode_ != bmColumn)
			return 256;
		aCol = columnForPtr(blockAnchor_);
		bCol = columnForPtr(effectiveBlockEnd());
		return std::max(aCol, bCol);
	}

	void syncBlockVisual() {
		uint a;
		uint b;
		if (editor == nullptr)
			return;
		if (blockMode_ == bmStream) {
			blockPtrRange(a, b);
			editor->setSelectionOffsets(a, b, False);
		} else if (blockMode_ == bmLine) {
			blockPtrRange(a, b);
			a = static_cast<uint>(editor->lineStartOffset(a));
			b = static_cast<uint>(editor->nextLineOffset(b));
			if (b > editor->bufferLength())
				b = static_cast<uint>(editor->bufferLength());
			editor->setSelectionOffsets(a, b, False);
		} else
			editor->setSelectionOffsets(editor->cursorOffset(), editor->cursorOffset(), False);
		editor->revealCursor(True);
		editor->update(ufView);
	}

	TScrollBar *vScrollBar;
	TScrollBar *hScrollBar;
	TMRIndicator *indicator;
	TMRFileEditor *editor;
	int bufferId_;
	bool firstSaveDone_;
	bool temporaryFileUsed_;
	std::string temporaryFileName_;
	int indentLevel_;
	BlockMode blockMode_;
	bool blockMarkingOn_;
	uint blockAnchor_;
	uint blockEnd_;
	std::vector<std::uint64_t> trackedCoprocessorTasks_;
	WindowRole windowRole_;
	std::string windowRoleDetail_;
	char displayTitle[MAXPATH];
};

#endif
