#ifndef MRWINDOWCOMMANDS_HPP
#define MRWINDOWCOMMANDS_HPP

#include <vector>
#include <string>

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
[[nodiscard]] bool handleWindowCascade();
[[nodiscard]] bool handleWindowTile();

[[nodiscard]] int currentVirtualDesktop();
void setCurrentVirtualDesktop(int vd);
[[nodiscard]] bool moveToNextVirtualDesktop();
[[nodiscard]] bool moveToPrevVirtualDesktop();
[[nodiscard]] bool viewportRight();
[[nodiscard]] bool viewportLeft();
void syncVirtualDesktopVisibility();
void mrSaveWorkspace(const std::string &filename);
void mrLoadWorkspace(const std::string &filename);

#endif
