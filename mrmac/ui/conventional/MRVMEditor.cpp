#include "MRVMEditor.hpp"

#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TView
#include <tvision/tv.h>

#include "MRVMScreen.hpp"
#include "../../../app/commands/MRWindowCommands.hpp"
#include "../../../ui/MREditWindow.hpp"

MREditWindow *mrvmEditorActiveWindow() {
	return currentEditWindow();
}

MRFileEditor *mrvmEditorCurrentEditor() {
	MREditWindow *win = currentEditorCommandWindow();

	return win != nullptr ? win->getEditor() : nullptr;
}

bool mrvmUiSetCurrentWindow(const void *windowKey) {
	MREditWindow *win;

	if (TProgram::deskTop == nullptr || windowKey == nullptr) return false;
	win = const_cast<MREditWindow *>(static_cast<const MREditWindow *>(windowKey));
	if (win == nullptr) return false;
	TProgram::deskTop->setCurrent(win, TView::normalSelect);
	returnWithDirectScreenMutation(true);
	return true;
}

bool mrvmUiCreateWindow() {
	return returnWithDirectScreenMutation(mrvmEditorCreateWindow());
}

bool mrvmUiDeleteCurrentWindow() {
	return returnWithDirectScreenMutation(mrvmEditorDeleteCurrentWindow());
}

bool mrvmUiEraseCurrentWindow() {
	return returnWithDirectScreenMutation(mrvmEditorEraseCurrentWindow());
}

bool mrvmUiModifyCurrentWindow() {
	return returnWithDirectScreenMutation(mrvmEditorModifyCurrentWindow());
}

bool mrvmUiSwitchWindow(int index) {
	return returnWithDirectScreenMutation(mrvmEditorSwitchWindow(index));
}

bool mrvmUiSizeCurrentWindow(int x1, int y1, int x2, int y2) {
	return returnWithDirectScreenMutation(mrvmEditorSizeCurrentWindow(x1, y1, x2, y2));
}

bool mrvmUiPushMarker() {
	return mrvmEditorMarkPosition(currentEditorCommandWindow(), mrvmEditorCurrentEditor());
}

bool mrvmUiGetMarker() {
	return mrvmEditorGotoMark(currentEditorCommandWindow(), mrvmEditorCurrentEditor());
}

bool mrvmUiSetRandomAccessMark(int index) {
	return mrvmEditorSetRandomAccessMark(currentEditorCommandWindow(), mrvmEditorCurrentEditor(), index);
}

bool mrvmUiGetRandomAccessMark(int index) {
	return mrvmEditorGotoRandomAccessMark(currentEditorCommandWindow(), mrvmEditorCurrentEditor(), index);
}

bool mrvmUiBlockBeginLine() {
	MREditWindow *win = currentEditorCommandWindow();
	if (win == nullptr) return false;
	win->beginLineBlock();
	return true;
}

bool mrvmUiBlockBeginColumn() {
	MREditWindow *win = currentEditorCommandWindow();
	if (win == nullptr) return false;
	win->beginColumnBlock();
	return true;
}

bool mrvmUiBlockBeginStream() {
	MREditWindow *win = currentEditorCommandWindow();
	if (win == nullptr) return false;
	win->beginStreamBlock();
	return true;
}

bool mrvmUiBlockEndMarking() {
	MREditWindow *win = currentEditorCommandWindow();
	if (win == nullptr) return false;
	win->endBlock();
	return true;
}

bool mrvmUiBlockTurnMarkingOff() {
	MREditWindow *win = currentEditorCommandWindow();
	if (win == nullptr) return false;
	win->clearBlock();
	return true;
}

bool mrvmUiBlockToggleVisibility() {
	MREditWindow *win = currentEditorCommandWindow();
	return win != nullptr && win->toggleBlockVisibility();
}

bool mrvmUiCopyBlock() {
	return true;
}

bool mrvmUiMoveBlock() {
	return true;
}

bool mrvmUiDeleteBlock() {
	MREditWindow *win = currentEditorCommandWindow();
	std::string errorText;

	if (win == nullptr) return false;
	return win->deleteBlock(&errorText);
}

bool mrvmUiExtractCurrentBlockText(std::string &out) {
	out.clear();
	return false;
}

bool mrvmUiIndentBlock() {
	return true;
}

bool mrvmUiUndentBlock() {
	return true;
}

bool mrvmUiMoveCursorToNextPageBreak() {
	return mrvmEditorMoveCursorToNextPageBreak(mrvmEditorCurrentEditor());
}

bool mrvmUiMoveCursorToPrevPageBreak() {
	return mrvmEditorMoveCursorToPrevPageBreak(mrvmEditorCurrentEditor());
}

bool mrvmUiCursorTabRight() {
	return mrvmEditorMoveCursorTabRight(mrvmEditorCurrentEditor());
}

bool mrvmUiCursorTabLeft() {
	return mrvmEditorMoveCursorTabLeft(mrvmEditorCurrentEditor());
}

bool mrvmUiCursorIndent() {
	return mrvmEditorIndentCursor(mrvmEditorCurrentEditor());
}

bool mrvmUiCursorUndent() {
	return mrvmEditorUndentCursor(mrvmEditorCurrentEditor());
}

bool mrvmUiWindowCopyBlock(int sourceWindowIndex) {
	(void)sourceWindowIndex;
	return true;
}

bool mrvmUiWindowMoveBlock(int sourceWindowIndex) {
	(void)sourceWindowIndex;
	return true;
}

bool mrvmUiWindowCopyBlockFromWindow(const void *sourceWindowKey) {
	(void)sourceWindowKey;
	return true;
}

bool mrvmUiWindowMoveBlockFromWindow(const void *sourceWindowKey) {
	(void)sourceWindowKey;
	return true;
}

bool mrvmUiWindowCopyBlockBetween(const void *sourceWindowKey, const void *targetWindowKey) {
	(void)sourceWindowKey;
	(void)targetWindowKey;
	return true;
}

bool mrvmUiWindowMoveBlockBetween(const void *sourceWindowKey, const void *targetWindowKey) {
	(void)sourceWindowKey;
	(void)targetWindowKey;
	return true;
}

bool mrvmUiSaveBlockToFile(const std::string &pathSpec) {
	(void)pathSpec;
	return true;
}

bool mrvmUiLinkCurrentWindow() {
	return returnWithDirectScreenMutation(mrvmEditorLinkCurrentWindow());
}

bool mrvmUiUnlinkCurrentWindow() {
	return returnWithDirectScreenMutation(mrvmEditorUnlinkCurrentWindow());
}

bool mrvmUiZoomCurrentWindow() {
	return returnWithDirectScreenMutation(mrvmEditorZoomCurrentWindow());
}

bool mrvmUiRedrawCurrentWindow() {
	return returnWithDirectScreenMutation(mrvmEditorRedrawCurrentWindow());
}

bool mrvmUiNewScreen() {
	return returnWithDirectScreenMutation(mrvmEditorRedrawEntireScreen());
}
