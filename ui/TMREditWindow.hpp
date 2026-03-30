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
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

#include "TMRFrame.hpp"
#include "TMRIndicator.hpp"
#include "TMRTextBuffer.hpp"

class TMREditWindow : public TWindow {
  public:
	TMREditWindow(const TRect &bounds, const char *title, int aNumber)
	    : TWindowInit(&TMREditWindow::initFrame), TWindow(bounds, 0, aNumber), vScrollBar(nullptr),
	      hScrollBar(nullptr), indicator(nullptr), editor(nullptr), bufferId_(allocateBufferId()),
	      firstSaveDone_(false), temporaryFileUsed_(false), temporaryFileName_(), indentLevel_(1),
	      blockMode_(bmNone), blockMarkingOn_(false), blockAnchor_(0), blockEnd_(0) {
		options |= ofTileable;

		std::strncpy(displayTitle, (title != nullptr && *title != '\0') ? title : "Untitled",
		             sizeof(displayTitle) - 1);
		displayTitle[sizeof(displayTitle) - 1] = '\0';

		hScrollBar = new TScrollBar(TRect(18, size.y - 1, size.x - 2, size.y));
		hScrollBar->hide();
		insert(hScrollBar);

		vScrollBar = new TScrollBar(TRect(size.x - 1, 1, size.x, size.y - 1));
		vScrollBar->hide();
		insert(vScrollBar);

		indicator = new TMRIndicator(TRect(2, size.y - 1, 16, size.y));
		indicator->hide();
		insert(indicator);

		TRect r(getExtent());
		r.grow(-1, -1);

		editor = createEditor(r, "");
		insert(editor);
		refreshSyntaxContext();
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
		else
			editor->setPersistentFileName(fileName);
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
	char displayTitle[MAXPATH];
};

#endif
