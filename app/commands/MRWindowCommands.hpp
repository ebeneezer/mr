#ifndef MRWINDOWCOMMANDS_HPP
#define MRWINDOWCOMMANDS_HPP

#include <set>
#include <vector>
#include <string>

class MREditWindow;
class MRBentoBox;
class MRDesktopWindow;
struct MRSetupPaths;

class MRWindowOpenBatch {
  public:
	MRWindowOpenBatch();
	void begin();
	[[nodiscard]] MREditWindow *createEditorWindow(const char *title);
	void finish(bool syncVisibility, bool notifyTopology);
	[[nodiscard]] bool active() const noexcept;

  private:
	std::set<short> usedNumbers;
	bool mActive;
	bool mDesktopLocked;
	std::size_t mCreatedCount;
};

[[nodiscard]] MREditWindow *createEditorWindow(const char *title);
[[nodiscard]] MREditWindow *createHelpWindow(const char *title);
[[nodiscard]] MREditWindow *createLogWindow(const char *title);
[[nodiscard]] MREditWindow *createCommunicationWindow(const char *title);
[[nodiscard]] MRBentoBox *createBentoBoxWindow(const char *title);
[[nodiscard]] MRBentoBox *createFileCompareBentoBoxWindow(const char *title);
[[nodiscard]] MRBentoBox *convertEditWindowToBentoBox(MREditWindow *source);
[[nodiscard]] std::vector<MREditWindow *> allEditWindowsInZOrder();
[[nodiscard]] std::vector<MREditWindow *> allEditWindowsAndBentoPanesInZOrder();
[[nodiscard]] std::vector<MRDesktopWindow *> allDesktopWindowsInZOrder();
[[nodiscard]] MREditWindow *currentEditWindow();
[[nodiscard]] MRDesktopWindow *currentDesktopWindow();
[[nodiscard]] MREditWindow *currentEditorCommandWindow();
[[nodiscard]] MREditWindow *findEditWindowByBufferId(int bufferId);
[[nodiscard]] bool isEmptyUntitledEditableWindow(MREditWindow *win);
[[nodiscard]] MREditWindow *findReusableEmptyWindow(MREditWindow *preferred);
[[nodiscard]] bool closeCurrentEditWindow();
[[nodiscard]] bool closeCurrentDesktopWindow();
[[nodiscard]] bool activateRelativeEditWindow(int delta);
[[nodiscard]] bool activateRelativeDesktopWindow(int delta);
[[nodiscard]] bool hideCurrentEditWindow();
[[nodiscard]] bool hideCurrentDesktopWindow();
[[nodiscard]] bool zoomCurrentDesktopWindow();
void mrUpdateAllWindowsColorTheme();
[[nodiscard]] bool handleWindowCascade();
[[nodiscard]] bool handleWindowTile();
void applyVirtualDesktopConfigurationChange(int count);
void mrRefreshVirtualDesktopSettingsSnapshot(int count, bool cyclic);
void mrRefreshVirtualDesktopSettingsSnapshot();
[[nodiscard]] int mrVirtualDesktopCountSnapshot();
[[nodiscard]] bool mrCyclicVirtualDesktopsSnapshot();
void setWindowManuallyHidden(MREditWindow *win, bool hidden);
[[nodiscard]] bool isWindowManuallyHidden(const MREditWindow *win);
[[nodiscard]] bool mrSetWorkspaceMainFile(MREditWindow *win);
void mrClearWorkspaceMainFile();
[[nodiscard]] bool mrIsWorkspaceMainFile(const MREditWindow *win);
[[nodiscard]] std::string mrWorkspaceMainFilePath();

[[nodiscard]] int currentVirtualDesktop();
void setCurrentVirtualDesktop(int vd);
[[nodiscard]] bool moveToNextVirtualDesktop();
[[nodiscard]] bool moveToPrevVirtualDesktop();
[[nodiscard]] bool viewportRight();
[[nodiscard]] bool viewportLeft();
void syncVirtualDesktopVisibility();
[[nodiscard]] std::string buildSettingsMacroSourceWithWorkspace(const MRSetupPaths &paths);
void mrSaveWorkspace(const std::string &filename);
void mrMarkWorkspaceAutosaveDirty(const char *source, const MREditWindow *window = nullptr);
void mrFlushWorkspaceAutosaveIfDue();
void mrFlushWorkspaceAutosaveNow();
[[nodiscard]] bool mrWorkspaceRestoreInProgress();
[[nodiscard]] bool mrSettingsFileHasAutosavedWorkspace();
[[nodiscard]] bool mrClearAutosavedWorkspace();
[[nodiscard]] bool mrLoadWorkspaceWithDialog();
void mrLoadWorkspace(const std::string &filename);

#endif
