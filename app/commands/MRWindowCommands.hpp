#ifndef MRWINDOWCOMMANDS_HPP
#define MRWINDOWCOMMANDS_HPP

#include <cstddef>
#include <set>
#include <vector>
#include <string>

class MREditWindow;
class MRBentoBox;
class MRBentoHexEditor;
class MRDesktopWindow;
struct MRSetupPaths;

class MRWindowOpenBatch {
  public:
	MRWindowOpenBatch();
	void begin();
	void beginInteractive();
	[[nodiscard]] MREditWindow *createEditorWindow(const char *title);
	[[nodiscard]] MRBentoHexEditor *createHexEditorWindow(const char *title);
	void finish(bool syncVisibility, bool notifyTopology);
	[[nodiscard]] bool active() const noexcept;

  private:
	void beginBatch(bool deferVisibility);

	std::set<short> usedNumbers;
	bool mActive;
	bool mDesktopLocked;
	bool mDeferVisibility;
	std::size_t mCreatedCount;
};

[[nodiscard]] MREditWindow *createEditorWindow(const char *title);
[[nodiscard]] MRBentoHexEditor *createHexEditorWindow(const char *title);
[[nodiscard]] MREditWindow *createHelpWindow(const char *title);
[[nodiscard]] MREditWindow *createLogWindow(const char *title);
[[nodiscard]] MREditWindow *createCommunicationWindow(const char *title);
[[nodiscard]] MRBentoBox *createBentoBoxWindow(const char *title);
[[nodiscard]] MRBentoBox *createFileCompareBentoBoxWindow(const char *title);
[[nodiscard]] MRBentoBox *convertEditWindowToBentoBox(MREditWindow *source);
[[nodiscard]] MRBentoHexEditor *convertEditWindowToHexEditor(MREditWindow *source);
[[nodiscard]] bool mrDispatchDeferredWindowClose(MREditWindow *window);
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
[[nodiscard]] bool activateRelativeDesktopWindow(int delta);
[[nodiscard]] bool hideCurrentDesktopWindow();
[[nodiscard]] bool zoomCurrentDesktopWindow();
void mrUpdateAllWindowsColorTheme();
void mrRefreshAllHexEditorProjections();
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
[[nodiscard]] bool mrSaveWorkspace(const std::string &filename);
void mrMarkWorkspaceAutosaveDirty(const char *source, const MREditWindow *window = nullptr);
void mrFlushWorkspaceAutosaveIfDue();
void mrFlushWorkspaceAutosaveNow();
[[nodiscard]] bool mrWorkspaceRestoreInProgress();
[[nodiscard]] std::vector<std::string> mrSettingsFileAutosavedWorkspaceFiles();
[[nodiscard]] bool mrClearAutosavedWorkspace();
[[nodiscard]] bool mrLoadWorkspaceWithDialog();
void mrLoadWorkspace(const std::string &filename);

#endif
