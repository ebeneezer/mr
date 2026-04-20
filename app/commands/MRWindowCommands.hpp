#ifndef MRWINDOWCOMMANDS_HPP
#define MRWINDOWCOMMANDS_HPP

#include <vector>

class TMREditWindow;

[[nodiscard]] TMREditWindow *createEditorWindow(const char *title);
[[nodiscard]] std::vector<TMREditWindow *> allEditWindowsInZOrder();
[[nodiscard]] TMREditWindow *currentEditWindow();
[[nodiscard]] TMREditWindow *findEditWindowByBufferId(int bufferId);
[[nodiscard]] bool isEmptyUntitledEditableWindow(TMREditWindow *win);
[[nodiscard]] TMREditWindow *findReusableEmptyWindow(TMREditWindow *preferred);
[[nodiscard]] bool closeCurrentEditWindow();
[[nodiscard]] bool activateRelativeEditWindow(int delta);
[[nodiscard]] bool hideCurrentEditWindow();
void mrUpdateAllWindowsColorTheme();

#endif
