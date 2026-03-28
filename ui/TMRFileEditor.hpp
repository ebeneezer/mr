#ifndef TMRFILEEDITOR_HPP
#define TMRFILEEDITOR_HPP

#define Uses_TFileEditor
#define Uses_TEvent
#define Uses_MsgBox
#include <tvision/tv.h>

#include <cstring>
#include <string>

class TMRFileEditor : public TFileEditor {
  public:
	TMRFileEditor(const TRect &bounds, TScrollBar *aHScrollBar, TScrollBar *aVScrollBar,
	              TIndicator *aIndicator, TStringView aFileName) noexcept
	    : TFileEditor(bounds, aHScrollBar, aVScrollBar, aIndicator, aFileName), readOnly_(false) {
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
		}
	}

	bool isReadOnly() const {
		return readOnly_;
	}

	bool hasPersistentFileName() const {
		return fileName[0] != EOS;
	}

	bool canSaveInPlace() const {
		return !readOnly_ && hasPersistentFileName();
	}

	Boolean saveInPlace() noexcept {
		if (!canSaveInPlace())
			return False;
		return saveFile();
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
		return true;
	}

	bool appendBufferText(const char *text) {
		uint length = 0;
		if (text != nullptr)
			length = static_cast<uint>(std::strlen(text));
		return appendBufferData(text, length);
	}

	virtual void handleEvent(TEvent &event) override {
		if (readOnly_) {
			if (isTextInputEvent(event) || isMutatingEditorCommand(event)) {
				clearEvent(event);
				return;
			}
		}
		TFileEditor::handleEvent(event);
	}

	virtual void updateCommands() override {
		TFileEditor::updateCommands();
		setCmdState(cmSave, canSaveInPlace() ? True : False);
		setCmdState(cmSaveAs, False);
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
			return confirmDiscardUntitled();
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

	Boolean confirmDiscardUntitled() {
		switch (messageBox(mfConfirmation | mfYesButton | mfCancelButton,
		                  "Window has unsaved changes.\nSave As is not available yet.\n\nDiscard changes?")) {
			case cmYes:
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

	bool readOnly_;
};

#endif
