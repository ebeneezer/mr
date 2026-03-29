#ifndef TMRFILEEDITOR_HPP
#define TMRFILEEDITOR_HPP

#define Uses_TDrawBuffer
#define Uses_TFileEditor
#define Uses_TEvent
#define Uses_MsgBox
#include <tvision/tv.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "TMRTextBufferModel.hpp"

class TMRFileEditor : public TFileEditor {
  public:
	TMRFileEditor(const TRect &bounds, TScrollBar *aHScrollBar, TScrollBar *aVScrollBar,
	              TIndicator *aIndicator, TStringView aFileName) noexcept
	    : TFileEditor(bounds, aHScrollBar, aVScrollBar, aIndicator, aFileName), readOnly_(false),
	      syntaxTitleHint_(), bufferModel_() {
		syncFromEditorState();
	}

	void setReadOnly(bool readOnly) {
		if (readOnly_ != readOnly) {
			readOnly_ = readOnly;
			if (readOnly_) {
				modified = False;
				delCount = 0;
				insCount = 0;
				update(ufUpdate);
			}
			updateCommands();
			syncFromEditorState(false);
		}
	}

	bool isReadOnly() const {
		return readOnly_;
	}

	const TMRTextBufferModel &bufferModel() const noexcept {
		return bufferModel_;
	}

	TMRTextBufferModel &bufferModel() noexcept {
		return bufferModel_;
	}

	void syncFromEditorState(bool includeText = true) {
		if (includeText) {
			std::string snapshot;
			snapshot.reserve(bufLen);
			for (uint i = 0; i < bufLen; ++i)
				snapshot.push_back(bufChar(i));
			bufferModel_.setText(snapshot);
		}
		bufferModel_.setSyntaxContext(hasPersistentFileName() ? fileName : "", syntaxTitleHint_);
		bufferModel_.setCursorAndSelection(curPtr, selStart, selEnd);
		bufferModel_.setModified(modified == True);
	}

	std::string snapshotText() const {
		return bufferModel_.text();
	}

	void setSyntaxTitleHint(const std::string &title) {
		syntaxTitleHint_ = title;
		syncFromEditorState(false);
	}

	const char *syntaxLanguageName() const noexcept {
		return bufferModel_.languageName();
	}

	TMRSyntaxLanguage syntaxLanguage() const noexcept {
		return bufferModel_.language();
	}

	bool hasPersistentFileName() const {
		return fileName[0] != EOS;
	}

	bool canSaveInPlace() const {
		return !readOnly_ && hasPersistentFileName();
	}

	bool canSaveAs() const {
		return !readOnly_;
	}

	Boolean saveInPlace() noexcept {
		if (!canSaveInPlace())
			return False;
		Boolean ok = saveFile();
		if (ok == True)
			syncFromEditorState();
		return ok;
	}

	Boolean saveAsWithPrompt() noexcept {
		if (!canSaveAs())
			return False;
		Boolean ok = saveAs();
		if (ok == True)
			syncFromEditorState();
		return ok;
	}

	bool replaceBufferData(const char *data, uint length) {
		bool wasReadOnly = readOnly_;

		if (wasReadOnly)
			readOnly_ = false;

		setBufLen(0);
		setCurPtr(0, 0);
		setSelect(0, 0, False);
		if (length != 0 && !insertText(data, length, False)) {
			if (wasReadOnly)
				readOnly_ = true;
			updateCommands();
			return false;
		}
		setCurPtr(0, 0);
		setSelect(0, 0, False);
		modified = False;
		delCount = 0;
		insCount = 0;
		if (wasReadOnly)
			readOnly_ = true;
		update(ufUpdate);
		updateCommands();
		syncFromEditorState();
		return true;
	}

	bool replaceBufferText(const char *text) {
		uint length = 0;
		if (text != nullptr)
			length = static_cast<uint>(std::strlen(text));
		return replaceBufferData(text, length);
	}

	bool appendBufferData(const char *data, uint length) {
		uint endPtr;
		bool wasReadOnly = readOnly_;

		if (length == 0)
			return true;
		if (wasReadOnly)
			readOnly_ = false;

		endPtr = bufLen;
		setCurPtr(endPtr, 0);
		setSelect(endPtr, endPtr, False);
		if (!insertText(data, length, False)) {
			if (wasReadOnly)
				readOnly_ = true;
			updateCommands();
			return false;
		}
		setCurPtr(bufLen, 0);
		setSelect(bufLen, bufLen, False);
		modified = False;
		delCount = 0;
		insCount = 0;
		if (wasReadOnly)
			readOnly_ = true;
		update(ufUpdate);
		updateCommands();
		syncFromEditorState();
		return true;
	}

	bool appendBufferText(const char *text) {
		uint length = 0;
		if (text != nullptr)
			length = static_cast<uint>(std::strlen(text));
		return appendBufferData(text, length);
	}

	bool replaceRangeAndSelect(uint start, uint end, const char *data, uint length) {
		if (readOnly_)
			return false;
		if (end < start)
			std::swap(start, end);
		if (start > bufLen)
			start = bufLen;
		if (end > bufLen)
			end = bufLen;
		lock();
		deleteRange(start, end, False);
		setCurPtr(start, 0);
		if (data != nullptr && length != 0)
			insertText(data, length, False);
		setCurPtr(start, 0);
		setSelect(start, start + length, False);
		trackCursor(True);
		unlock();
		doUpdate();
		syncFromEditorState();
		return true;
	}

	bool insertBufferText(const std::string &text) {
		uint endSel;
		if (readOnly_)
			return false;
		lock();
		if (overwrite == True && hasSelection() == False) {
			endSel = curPtr;
			for (std::string::size_type i = 0; i < text.size() && endSel < lineEnd(curPtr); ++i)
				endSel = nextChar(endSel);
			if (endSel > curPtr)
				setSelect(curPtr, endSel, False);
		}
		if (!text.empty())
			insertText(text.c_str(), static_cast<uint>(text.size()), False);
		trackCursor(True);
		unlock();
		doUpdate();
		syncFromEditorState();
		return true;
	}

	bool replaceCurrentLineText(const std::string &text) {
		uint start;
		uint end;
		if (readOnly_)
			return false;
		start = lineStart(curPtr);
		end = lineEnd(curPtr);
		lock();
		deleteRange(start, end, False);
		setCurPtr(start, 0);
		if (!text.empty())
			insertText(text.c_str(), static_cast<uint>(text.size()), False);
		setCurPtr(start, 0);
		trackCursor(True);
		unlock();
		doUpdate();
		syncFromEditorState();
		return true;
	}

	bool deleteCharsAtCursor(int count) {
		uint start;
		uint end;
		if (readOnly_)
			return false;
		if (count <= 0)
			return true;
		start = curPtr;
		end = start;
		for (int i = 0; i < count && end < bufLen; ++i)
			end = nextChar(end);
		if (end <= start)
			return true;
		lock();
		deleteRange(start, end, False);
		setCurPtr(start, 0);
		trackCursor(True);
		unlock();
		doUpdate();
		syncFromEditorState();
		return true;
	}

	bool deleteCurrentLineText() {
		uint start;
		uint end;
		if (readOnly_)
			return false;
		start = lineStart(curPtr);
		end = nextLine(curPtr);
		if (end < start)
			end = start;
		if (end > bufLen)
			end = bufLen;
		lock();
		deleteRange(start, end, False);
		if (start > bufLen)
			start = bufLen;
		setCurPtr(start, 0);
		trackCursor(True);
		unlock();
		doUpdate();
		syncFromEditorState();
		return true;
	}

	bool replaceWholeBuffer(const std::string &text, std::size_t cursorPos) {
		if (readOnly_)
			return false;
		if (cursorPos > text.size())
			cursorPos = text.size();
		lock();
		deleteRange(0, bufLen, False);
		setCurPtr(0, 0);
		if (!text.empty())
			insertText(text.c_str(), static_cast<uint>(text.size()), False);
		if (cursorPos > bufLen)
			cursorPos = bufLen;
		setSelect(static_cast<uint>(cursorPos), static_cast<uint>(cursorPos), False);
		setCurPtr(static_cast<uint>(cursorPos), 0);
		trackCursor(True);
		unlock();
		doUpdate();
		syncFromEditorState();
		return true;
	}

	bool newLineWithIndent(const std::string &fill) {
		if (readOnly_)
			return false;
		lock();
		newLine();
		if (!fill.empty())
			insertText(fill.c_str(), static_cast<uint>(fill.size()), False);
		trackCursor(True);
		unlock();
		doUpdate();
		syncFromEditorState();
		return true;
	}

	virtual void draw() override {
		uint linePtr;
		int y;

		if (drawLine != delta.y) {
			drawPtr = lineMove(drawPtr, delta.y - drawLine);
			drawLine = delta.y;
		}

		linePtr = drawPtr;
		for (y = 0; y < size.y; ++y) {
			TDrawBuffer buffer;
			formatSyntaxLine(buffer, linePtr, delta.x, size.x);
			writeBuf(0, y, size.x, 1, buffer);
			linePtr = nextLine(linePtr);
		}
	}

	virtual void handleEvent(TEvent &event) override {
		uint beforeLen = bufLen;
		uint beforeCurPtr = curPtr;
		uint beforeSelStart = selStart;
		uint beforeSelEnd = selEnd;
		Boolean beforeModified = modified;
		bool mayChangeText = isTextInputEvent(event) || isMutatingEditorCommand(event);

		if (readOnly_) {
			if (isTextInputEvent(event) || isMutatingEditorCommand(event)) {
				clearEvent(event);
				return;
			}
		}
		TFileEditor::handleEvent(event);
		if (mayChangeText || bufLen != beforeLen || modified != beforeModified)
			syncFromEditorState();
		else if (curPtr != beforeCurPtr || selStart != beforeSelStart || selEnd != beforeSelEnd)
			syncFromEditorState(false);
	}

	virtual void updateCommands() override {
		TFileEditor::updateCommands();
		setCmdState(cmSave, readOnly_ ? False : True);
		setCmdState(cmSaveAs, canSaveAs() ? True : False);
		if (readOnly_) {
			setCmdState(cmUndo, False);
			setCmdState(cmCut, False);
			setCmdState(cmPaste, False);
			setCmdState(cmClear, False);
			setCmdState(cmReplace, False);
		}
	}

	virtual Boolean valid(ushort command) override {
		if (command == cmValid || command == cmReleasedFocus)
			return TFileEditor::valid(command);
		if (readOnly_ || modified == False)
			return True;
		if (!hasPersistentFileName())
			return confirmSaveOrDiscardUntitled();
		return confirmSaveOrDiscardNamed();
	}

  private:
	bool isTextInputEvent(const TEvent &event) const {
		if (event.what != evKeyDown)
			return false;
		return (event.keyDown.controlKeyState & kbPaste) != 0 || event.keyDown.charScan.charCode == 9 ||
		       (event.keyDown.charScan.charCode >= 32 && event.keyDown.charScan.charCode < 255) ||
		       (encoding != encSingleByte && event.keyDown.textLength > 0);
	}

	static bool isMutatingEditorCommand(const TEvent &event) {
		if (event.what != evCommand)
			return false;
		switch (event.message.command) {
			case cmSave:
			case cmSaveAs:
			case cmReplace:
			case cmCut:
			case cmPaste:
			case cmUndo:
			case cmClear:
			case cmNewLine:
			case cmBackSpace:
			case cmDelChar:
			case cmDelWord:
			case cmDelWordLeft:
			case cmDelStart:
			case cmDelEnd:
			case cmDelLine:
				return true;
			default:
				return false;
		}
	}

	Boolean confirmSaveOrDiscardUntitled() {
		switch (messageBox(mfConfirmation | mfYesNoCancel,
		                  "Window has unsaved changes.\n\nYes = Save As  No = Discard  Cancel = Abort")) {
			case cmYes:
				return saveAsWithPrompt();
			case cmNo:
				modified = False;
				update(ufUpdate);
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
				modified = False;
				update(ufUpdate);
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

	void formatSyntaxLine(TDrawBuffer &b, uint linePtr, int hScroll, int width) {
		TMRSyntaxTokenMap tokens = bufferModel_.tokenMapForLine(linePtr);
		uint p = linePtr;
		int pos = 0;
		int x = 0;

		hScroll = std::max(hScroll, 0);
		width = std::max(width, 0);

		while (p < bufLen) {
			uint nextP = p;
			int nextPos = pos;
			nextCharAndPos(nextP, nextPos);

			if (x > width || (x == width && pos < nextPos))
				break;

			char bytes[maxCharSize];
			uint charLen = nextP - p;
			getText(p, TSpan<char>(bytes, charLen));

			if (bytes[0] == '\r' || bytes[0] == '\n')
				break;

			if (nextPos > hScroll) {
				std::size_t tokenIndex = p >= linePtr ? static_cast<std::size_t>(p - linePtr) : 0;
				TMRSyntaxToken token =
				    tokenIndex < tokens.size() ? tokens[tokenIndex] : TMRSyntaxToken::Text;
				TColorAttr color = tokenColor(token, selStart <= p && p < selEnd);
				int charWidth = nextPos - std::max(pos, hScroll);

				if (bytes[0] == '\t' || pos < hScroll)
					b.moveChar(static_cast<ushort>(x), ' ', color, static_cast<ushort>(charWidth));
				else
					b.moveStr(static_cast<ushort>(x), TStringView(bytes, charLen), color);

				x += charWidth;
			}

			p = nextP;
			pos = nextPos;
		}

		if (x < width) {
			std::size_t tokenIndex = p >= linePtr ? static_cast<std::size_t>(p - linePtr) : 0;
			TMRSyntaxToken token =
			    tokenIndex < tokens.size() ? tokens[tokenIndex] : TMRSyntaxToken::Text;
			TColorAttr color = tokenColor(token, selStart <= p && p < selEnd);
			b.moveChar(static_cast<ushort>(x), ' ', color, static_cast<ushort>(width - x));
		}
	}

	bool readOnly_;
	std::string syntaxTitleHint_;
	TMRTextBufferModel bufferModel_;
};

#endif
