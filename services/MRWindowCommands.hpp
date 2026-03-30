#ifndef MRWINDOWCOMMANDS_HPP
#define MRWINDOWCOMMANDS_HPP

#include <vector>

class TMREditWindow;

TMREditWindow *createEditorWindow(const char *title);
std::vector<TMREditWindow *> allEditWindowsInZOrder();
TMREditWindow *currentEditWindow();
TMREditWindow *findEditWindowByBufferId(int bufferId);
bool isEmptyUntitledEditableWindow(TMREditWindow *win);
TMREditWindow *findReusableEmptyWindow(TMREditWindow *preferred);
bool closeCurrentEditWindow();
bool activateRelativeEditWindow(int delta);
bool hideCurrentEditWindow();

#endif
