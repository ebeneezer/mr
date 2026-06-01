#ifndef MRVM_EDITOR_HPP
#define MRVM_EDITOR_HPP

#include <string>

#include "../MRVM.hpp"

class MRFileEditor;

std::string mrvmEditorExpandUserPath(const std::string &path);

MREditWindow *mrvmEditorActiveWindow();
MRFileEditor *mrvmEditorCurrentEditor();

bool mrvmEditorMarkPosition(MREditWindow *win, MRFileEditor *editor);
bool mrvmEditorGotoMark(MREditWindow *win, MRFileEditor *editor);
bool mrvmEditorSetRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index);
bool mrvmEditorGotoRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index);

bool mrvmEditorBeginBlockMode(int mode);
bool mrvmEditorEndBlockMode();
bool mrvmEditorClearBlockMode();

bool mrvmEditorMoveCursorToNextPageBreak(MRFileEditor *editor);
bool mrvmEditorMoveCursorToPrevPageBreak(MRFileEditor *editor);
bool mrvmEditorMoveCursorTabRight(MRFileEditor *editor);
bool mrvmEditorMoveCursorTabLeft(MRFileEditor *editor);
bool mrvmEditorIndentCursor(MRFileEditor *editor);
bool mrvmEditorUndentCursor(MRFileEditor *editor);

bool mrvmEditorCopyCurrentBlock(MREditWindow *win, MRFileEditor *editor);
bool mrvmEditorMoveCurrentBlock(MREditWindow *win, MRFileEditor *editor);
bool mrvmEditorDeleteCurrentBlock(MREditWindow *win, MRFileEditor *editor, bool leaveColumnSpace = false);
bool mrvmEditorExtractCurrentBlockText(MREditWindow *win, MRFileEditor *editor, std::string &out);
bool mrvmEditorIndentBlock(MREditWindow *win, MRFileEditor *editor);
bool mrvmEditorUndentBlock(MREditWindow *win, MRFileEditor *editor);

MREditWindow *mrvmEditorWindowByIndex(int index);
bool mrvmEditorCopyBlockFromWindow(MREditWindow *srcWin, MRFileEditor *srcEditor, MREditWindow *destWin, MRFileEditor *destEditor);
bool mrvmEditorMoveBlockFromWindow(MREditWindow *srcWin, MRFileEditor *srcEditor, MREditWindow *destWin, MRFileEditor *destEditor);
bool mrvmEditorShouldLeaveColumnSpaceForDelete(MREditWindow *win);
bool mrvmEditorLoadBlockFromFile(MREditWindow *win, const std::string &path);
bool mrvmEditorSaveCurrentBlockToFile(MREditWindow *win, MRFileEditor *editor, const std::string &path);

bool mrvmEditorLinkCurrentWindow();
bool mrvmEditorUnlinkCurrentWindow();
bool mrvmEditorRedrawCurrentWindow();
bool mrvmEditorRedrawEntireScreen();
bool mrvmEditorZoomCurrentWindow();

bool mrvmEditorCreateWindow();
bool mrvmEditorSwitchWindow(int index);
bool mrvmEditorSizeCurrentWindow(int x1, int y1, int x2, int y2);
bool mrvmEditorDeleteCurrentWindow();
bool mrvmEditorEraseCurrentWindow();
bool mrvmEditorModifyCurrentWindow();

#endif
