#include "MRVMEditor.hpp"

#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TView
#include <tvision/tv.h>

#include "MRVMScreen.hpp"
#include "../../vm/MRVMRuntimeInternal.hpp"
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

bool mrvmUiDeleteBlock() {
	MREditWindow *win = currentEditorCommandWindow();
	std::string errorText;

	if (win == nullptr) return false;
	return win->deleteBlock(&errorText);
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

bool mrvmEditorMarkPosition(MREditWindow *win, MRFileEditor *editor) {
	return mrvm_runtime::markEditorPosition(win, editor);
}

bool mrvmEditorGotoMark(MREditWindow *win, MRFileEditor *editor) {
	return mrvm_runtime::gotoEditorMark(win, editor);
}

bool mrvmEditorSetRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index) {
	return mrvm_runtime::setEditorRandomAccessMark(win, editor, index);
}

bool mrvmEditorGotoRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index) {
	return mrvm_runtime::gotoEditorRandomAccessMark(win, editor, index);
}

bool mrvmEditorMoveCursorToNextPageBreak(MRFileEditor *editor) {
	return mrvm_runtime::moveEditorNextPageBreak(editor);
}

bool mrvmEditorMoveCursorToPrevPageBreak(MRFileEditor *editor) {
	return mrvm_runtime::moveEditorLastPageBreak(editor);
}

bool mrvmEditorMoveCursorTabRight(MRFileEditor *editor) {
	return mrvm_runtime::moveEditorTabRight(editor);
}

bool mrvmEditorMoveCursorTabLeft(MRFileEditor *editor) {
	return mrvm_runtime::moveEditorTabLeft(editor);
}

bool mrvmEditorIndentCursor(MRFileEditor *editor) {
	return mrvm_runtime::indentEditor(editor);
}

bool mrvmEditorUndentCursor(MRFileEditor *editor) {
	return mrvm_runtime::undentEditor(editor);
}

bool mrvmEditorLoadBlockFromFile(MREditWindow *win, const std::string &path) {
	return win != nullptr && win->loadStreamBlockFromFile(path);
}

bool mrvmEditorSaveCurrentBlockToFile(MREditWindow *win, MRFileEditor *editor, const std::string &path) {
	(void)editor;
	return win != nullptr && win->saveStreamBlockToFile(path);
}

bool mrvmEditorLinkCurrentWindow() {
	return mrvm_runtime::linkCurrentEditWindow();
}

bool mrvmEditorUnlinkCurrentWindow() {
	return mrvm_runtime::unlinkCurrentEditWindow();
}

bool mrvmEditorRedrawCurrentWindow() {
	return mrvm_runtime::redrawCurrentEditWindow();
}

bool mrvmEditorRedrawEntireScreen() {
	return mrvm_runtime::redrawEntireScreen();
}

bool mrvmEditorZoomCurrentWindow() {
	return mrvm_runtime::zoomCurrentEditWindow();
}

bool mrvmEditorCreateWindow() {
	return mrvm_runtime::createEditWindow();
}

bool mrvmEditorSwitchWindow(int index) {
	return mrvm_runtime::switchEditWindow(index);
}

bool mrvmEditorSizeCurrentWindow(int x1, int y1, int x2, int y2) {
	return mrvm_runtime::sizeCurrentEditWindow(x1, y1, x2, y2);
}

bool mrvmEditorDeleteCurrentWindow() {
	return mrvm_runtime::deleteCurrentEditWindow();
}

bool mrvmEditorEraseCurrentWindow() {
	return mrvm_runtime::eraseCurrentEditWindow();
}

bool mrvmEditorModifyCurrentWindow() {
	return mrvm_runtime::modifyCurrentEditWindow();
}
