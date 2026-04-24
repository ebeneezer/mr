#ifndef MRWINDOWCOMMANDS_HPP
#define MRWINDOWCOMMANDS_HPP

#include <vector>
#include <string>

class MREditWindow;

[[nodiscard]] MREditWindow *createEditorWindow(const char *title);
[[nodiscard]] std::vector<MREditWindow *> allEditWindowsInZOrder();
[[nodiscard]] MREditWindow *currentEditWindow();
[[nodiscard]] MREditWindow *findEditWindowByBufferId(int bufferId);
[[nodiscard]] bool isEmptyUntitledEditableWindow(MREditWindow *win);
[[nodiscard]] MREditWindow *findReusableEmptyWindow(MREditWindow *preferred);
[[nodiscard]] bool closeCurrentEditWindow();
[[nodiscard]] bool activateRelativeEditWindow(int delta);
[[nodiscard]] bool hideCurrentEditWindow();
void mrUpdateAllWindowsColorTheme();
[[nodiscard]] bool handleWindowCascade();
[[nodiscard]] bool handleWindowTile();
void applyVirtualDesktopConfigurationChange(int count);
void setWindowManuallyHidden(MREditWindow *win, bool hidden);
[[nodiscard]] bool isWindowManuallyHidden(const MREditWindow *win);

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
