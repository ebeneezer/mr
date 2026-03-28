#ifndef TMREDITWINDOW_HPP
#define TMREDITWINDOW_HPP

#define Uses_TWindow
#define Uses_TScrollBar
#define Uses_TIndicator
#define Uses_TFileEditor
#define Uses_TRect
#define Uses_TEvent
#define Uses_TEditor
#include <tvision/tv.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "TMRFrame.hpp"

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

		indicator = new TIndicator(TRect(2, size.y - 1, 16, size.y));
		indicator->hide();
		insert(indicator);

		TRect r(getExtent());
		r.grow(-1, -1);

		editor = new TFileEditor(r, hScrollBar, vScrollBar, indicator, "");
		insert(editor);
	}

	virtual TPalette &getPalette() const override {
		return TWindow::getPalette();
	}

	virtual const char *getTitle(short) override {
		if (editor != nullptr && editor->fileName[0] != EOS)
			return editor->fileName;
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
		if (editor == nullptr || fileName == nullptr || *fileName == '\0')
			return false;

		strnzcpy(editor->fileName, fileName, sizeof(editor->fileName));
		fexpand(editor->fileName);
		if (!editor->loadFile())
			return false;

		temporaryFileUsed_ = false;
		temporaryFileName_.clear();
		updateTitleFromEditor();
		return true;
	}

	bool saveCurrentFile() {
		if (editor == nullptr)
			return false;

		bool ok = editor->save() == True;
		if (ok) {
			firstSaveDone_ = true;
			temporaryFileUsed_ = false;
			temporaryFileName_.clear();
			updateTitleFromEditor();
		}
		return ok;
	}

	const char *currentFileName() const {
		if (editor != nullptr && editor->fileName[0] != EOS)
			return editor->fileName;
		return "";
	}

	TFileEditor *getEditor() const {
		return editor;
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
		return editor != nullptr && editor->modified == True;
	}

	void setFileChanged(bool changed) {
		if (editor != nullptr) {
			editor->modified = changed ? True : False;
			editor->update(ufUpdate);
		}
	}

	void setCurrentFileName(const char *fileName) {
		if (editor == nullptr)
			return;

		if (fileName == nullptr || *fileName == '\0')
			editor->fileName[0] = EOS;
		else {
			strnzcpy(editor->fileName, fileName, sizeof(editor->fileName));
			fexpand(editor->fileName);
		}
		updateTitleFromEditor();
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
		blockEnd_ = editor->curPtr;
		blockMarkingOn_ = false;
		syncBlockVisual();
	}

	void clearBlock() {
		blockMode_ = bmNone;
		blockMarkingOn_ = false;
		blockAnchor_ = 0;
		blockEnd_ = 0;
		if (editor != nullptr) {
			editor->setSelect(editor->curPtr, editor->curPtr, False);
			editor->trackCursor(True);
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
	static int allocateBufferId() {
		static int nextId = 1;
		return nextId++;
	}

	static TFrame *initFrame(TRect r) {
		return new TMRFrame(r);
	}

	void updateTitleFromEditor() {
		if (editor != nullptr && editor->fileName[0] != EOS) {
			std::strncpy(displayTitle, editor->fileName, sizeof(displayTitle) - 1);
			displayTitle[sizeof(displayTitle) - 1] = '\0';
		}
		message(owner, evBroadcast, cmUpdateTitle, 0);
	}

	void beginBlock(BlockMode mode) {
		if (editor == nullptr)
			return;
		blockMode_ = mode;
		blockMarkingOn_ = true;
		blockAnchor_ = editor->curPtr;
		blockEnd_ = editor->curPtr;
		syncBlockVisual();
	}

	uint effectiveBlockEnd() const {
		if (editor != nullptr && blockMarkingOn_)
			return editor->curPtr;
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
		if (ptr > editor->bufLen)
			ptr = editor->bufLen;
		while (pos < ptr && pos < editor->bufLen) {
			uint next = editor->nextLine(pos);
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
		if (ptr > editor->bufLen)
			ptr = editor->bufLen;
		start = editor->lineStart(ptr);
		return editor->charPos(start, ptr) + 1;
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
			editor->setSelect(a, b, False);
		} else if (blockMode_ == bmLine) {
			blockPtrRange(a, b);
			a = editor->lineStart(a);
			b = editor->nextLine(b);
			if (b > editor->bufLen)
				b = editor->bufLen;
			editor->setSelect(a, b, False);
		} else
			editor->setSelect(editor->curPtr, editor->curPtr, False);
		editor->trackCursor(True);
		editor->update(ufView);
	}

	TScrollBar *vScrollBar;
	TScrollBar *hScrollBar;
	TIndicator *indicator;
	TFileEditor *editor;
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
