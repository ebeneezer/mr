#include "MRVMEditor.hpp"

#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TView
#include <tvision/tv.h>

#include "MRVMScreen.hpp"
#include "../../ui/MREditWindow.hpp"

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
	return mrvmEditorMarkPosition(mrvmEditorActiveWindow(), mrvmEditorCurrentEditor());
}

bool mrvmUiGetMarker() {
	return mrvmEditorGotoMark(mrvmEditorActiveWindow(), mrvmEditorCurrentEditor());
}

bool mrvmUiSetRandomAccessMark(int index) {
	return mrvmEditorSetRandomAccessMark(mrvmEditorActiveWindow(), mrvmEditorCurrentEditor(), index);
}

bool mrvmUiGetRandomAccessMark(int index) {
	return mrvmEditorGotoRandomAccessMark(mrvmEditorActiveWindow(), mrvmEditorCurrentEditor(), index);
}

bool mrvmUiBlockBeginLine() {
	MREditWindow *win = mrvmEditorActiveWindow();
	if (win == nullptr) return false;
	win->beginLineBlock();
	return true;
}

bool mrvmUiBlockBeginColumn() {
	MREditWindow *win = mrvmEditorActiveWindow();
	if (win == nullptr) return false;
	win->beginColumnBlock();
	return true;
}

bool mrvmUiBlockBeginStream() {
	MREditWindow *win = mrvmEditorActiveWindow();
	if (win == nullptr) return false;
	win->beginStreamBlock();
	return true;
}

bool mrvmUiBlockEndMarking() {
	MREditWindow *win = mrvmEditorActiveWindow();
	if (win == nullptr) return false;
	win->endBlock();
	return true;
}

bool mrvmUiBlockTurnMarkingOff() {
	MREditWindow *win = mrvmEditorActiveWindow();
	if (win == nullptr) return false;
	win->clearBlock();
	return true;
}

bool mrvmUiBlockToggleVisibility() {
	MREditWindow *win = mrvmEditorActiveWindow();
	return win != nullptr && win->toggleBlockVisibility();
}

bool mrvmUiCopyBlock() {
	return true;
}

bool mrvmUiMoveBlock() {
	return true;
}

bool mrvmUiDeleteBlock() {
	MREditWindow *win = mrvmEditorActiveWindow();
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
