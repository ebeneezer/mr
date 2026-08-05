#ifndef MRVM_EDITOR_HPP
#define MRVM_EDITOR_HPP

#include <string>

#include "../../MRVM.hpp"

class MRFileEditor;

MREditWindow *mrvmEditorActiveWindow();
MRFileEditor *mrvmEditorCurrentEditor();

bool mrvmEditorMarkPosition(MREditWindow *win, MRFileEditor *editor);
bool mrvmEditorGotoMark(MREditWindow *win, MRFileEditor *editor);
bool mrvmEditorSetRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index);
bool mrvmEditorGotoRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index);

bool mrvmEditorMoveCursorToNextPageBreak(MRFileEditor *editor);
bool mrvmEditorMoveCursorToPrevPageBreak(MRFileEditor *editor);
bool mrvmEditorMoveCursorTabRight(MRFileEditor *editor);
bool mrvmEditorMoveCursorTabLeft(MRFileEditor *editor);
bool mrvmEditorIndentCursor(MRFileEditor *editor);
bool mrvmEditorUndentCursor(MRFileEditor *editor);

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
