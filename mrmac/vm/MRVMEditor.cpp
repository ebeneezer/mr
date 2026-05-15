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
	return mrvmEditorBeginBlockMode(MREditWindow::bmLine);
}

bool mrvmUiBlockBeginColumn() {
	return mrvmEditorBeginBlockMode(MREditWindow::bmColumn);
}

bool mrvmUiBlockBeginStream() {
	return mrvmEditorBeginBlockMode(MREditWindow::bmStream);
}

bool mrvmUiBlockEndMarking() {
	return mrvmEditorEndBlockMode();
}

bool mrvmUiBlockTurnMarkingOff() {
	return mrvmEditorClearBlockMode();
}

bool mrvmUiCopyBlock() {
	MREditWindow *win = mrvmEditorActiveWindow();
	MRFileEditor *editor = mrvmEditorCurrentEditor();
	if (win == nullptr || editor == nullptr) return false;
	return mrvmEditorCopyCurrentBlock(win, editor);
}

bool mrvmUiMoveBlock() {
	MREditWindow *win = mrvmEditorActiveWindow();
	MRFileEditor *editor = mrvmEditorCurrentEditor();
	if (win == nullptr || editor == nullptr) return false;
	return mrvmEditorMoveCurrentBlock(win, editor);
}

bool mrvmUiDeleteBlock() {
	MREditWindow *win = mrvmEditorActiveWindow();
	MRFileEditor *editor = mrvmEditorCurrentEditor();
	if (win == nullptr || editor == nullptr) return false;
	return mrvmEditorDeleteCurrentBlock(win, editor, mrvmEditorShouldLeaveColumnSpaceForDelete(win));
}

bool mrvmUiExtractCurrentBlockText(std::string &out) {
	MREditWindow *win = mrvmEditorActiveWindow();
	MRFileEditor *editor = mrvmEditorCurrentEditor();
	if (win == nullptr || editor == nullptr) return false;
	return mrvmEditorExtractCurrentBlockText(win, editor, out);
}

bool mrvmUiIndentBlock() {
	MREditWindow *win = mrvmEditorActiveWindow();
	MRFileEditor *editor = mrvmEditorCurrentEditor();
	if (win == nullptr || editor == nullptr) return false;
	return mrvmEditorIndentBlock(win, editor);
}

bool mrvmUiUndentBlock() {
	MREditWindow *win = mrvmEditorActiveWindow();
	MRFileEditor *editor = mrvmEditorCurrentEditor();
	if (win == nullptr || editor == nullptr) return false;
	return mrvmEditorUndentBlock(win, editor);
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
	MREditWindow *destWin = mrvmEditorActiveWindow();
	MRFileEditor *destEditor = mrvmEditorCurrentEditor();
	MREditWindow *srcWin = mrvmEditorWindowByIndex(sourceWindowIndex);
	MRFileEditor *srcEditor = srcWin != nullptr ? srcWin->getEditor() : nullptr;
	if (destWin == nullptr || destEditor == nullptr || srcWin == nullptr || srcEditor == nullptr) return false;
	if (srcWin == destWin) return false;
	return mrvmEditorCopyBlockFromWindow(srcWin, srcEditor, destWin, destEditor);
}

bool mrvmUiWindowMoveBlock(int sourceWindowIndex) {
	MREditWindow *destWin = mrvmEditorActiveWindow();
	MRFileEditor *destEditor = mrvmEditorCurrentEditor();
	MREditWindow *srcWin = mrvmEditorWindowByIndex(sourceWindowIndex);
	MRFileEditor *srcEditor = srcWin != nullptr ? srcWin->getEditor() : nullptr;
	if (destWin == nullptr || destEditor == nullptr || srcWin == nullptr || srcEditor == nullptr) return false;
	if (srcWin == destWin) return false;
	return mrvmEditorMoveBlockFromWindow(srcWin, srcEditor, destWin, destEditor);
}

bool mrvmUiWindowCopyBlockFromWindow(const void *sourceWindowKey) {
	MREditWindow *destWin = mrvmEditorActiveWindow();
	MRFileEditor *destEditor = mrvmEditorCurrentEditor();
	MREditWindow *srcWin = const_cast<MREditWindow *>(static_cast<const MREditWindow *>(sourceWindowKey));
	MRFileEditor *srcEditor = srcWin != nullptr ? srcWin->getEditor() : nullptr;
	if (destWin == nullptr || destEditor == nullptr || srcWin == nullptr || srcEditor == nullptr) return false;
	if (srcWin == destWin) return false;
	return mrvmEditorCopyBlockFromWindow(srcWin, srcEditor, destWin, destEditor);
}

bool mrvmUiWindowMoveBlockFromWindow(const void *sourceWindowKey) {
	MREditWindow *destWin = mrvmEditorActiveWindow();
	MRFileEditor *destEditor = mrvmEditorCurrentEditor();
	MREditWindow *srcWin = const_cast<MREditWindow *>(static_cast<const MREditWindow *>(sourceWindowKey));
	MRFileEditor *srcEditor = srcWin != nullptr ? srcWin->getEditor() : nullptr;
	if (destWin == nullptr || destEditor == nullptr || srcWin == nullptr || srcEditor == nullptr) return false;
	if (srcWin == destWin) return false;
	return mrvmEditorMoveBlockFromWindow(srcWin, srcEditor, destWin, destEditor);
}

bool mrvmUiWindowCopyBlockBetween(const void *sourceWindowKey, const void *targetWindowKey) {
	MREditWindow *srcWin = const_cast<MREditWindow *>(static_cast<const MREditWindow *>(sourceWindowKey));
	MREditWindow *destWin = const_cast<MREditWindow *>(static_cast<const MREditWindow *>(targetWindowKey));
	MRFileEditor *srcEditor = srcWin != nullptr ? srcWin->getEditor() : nullptr;
	MRFileEditor *destEditor = destWin != nullptr ? destWin->getEditor() : nullptr;
	if (destWin == nullptr || destEditor == nullptr || srcWin == nullptr || srcEditor == nullptr) return false;
	if (srcWin == destWin) return false;
	return mrvmEditorCopyBlockFromWindow(srcWin, srcEditor, destWin, destEditor);
}

bool mrvmUiWindowMoveBlockBetween(const void *sourceWindowKey, const void *targetWindowKey) {
	MREditWindow *srcWin = const_cast<MREditWindow *>(static_cast<const MREditWindow *>(sourceWindowKey));
	MREditWindow *destWin = const_cast<MREditWindow *>(static_cast<const MREditWindow *>(targetWindowKey));
	MRFileEditor *srcEditor = srcWin != nullptr ? srcWin->getEditor() : nullptr;
	MRFileEditor *destEditor = destWin != nullptr ? destWin->getEditor() : nullptr;
	if (destWin == nullptr || destEditor == nullptr || srcWin == nullptr || srcEditor == nullptr) return false;
	if (srcWin == destWin) return false;
	return mrvmEditorMoveBlockFromWindow(srcWin, srcEditor, destWin, destEditor);
}

bool mrvmUiSaveBlockToFile(const std::string &pathSpec) {
	MREditWindow *win = mrvmEditorActiveWindow();
	MRFileEditor *editor = mrvmEditorCurrentEditor();
	std::string path;
	if (win == nullptr || editor == nullptr) return false;
	path = mrvmEditorExpandUserPath(pathSpec);
	if (path.empty()) return false;
	return mrvmEditorSaveCurrentBlockToFile(win, editor, path);
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
