#ifndef MRFILECOMMANDS_HPP
#define MRFILECOMMANDS_HPP

#include "../../config/settings/MRSettingsRuntime.hpp"

#include <cstddef>
#include <string>
#include <vector>

class MREditWindow;
class MRWindowOpenBatch;

enum class MRLoadedWindowActivation : unsigned char {
	ActivateLast = 0,
	KeepBackground
};

enum class MRFileLoadMessages : unsigned char {
	PerFile = 0,
	Suppressed
};

[[nodiscard]] bool promptForPath(const char *title, char *fileName, std::size_t fileNameSize);
[[nodiscard]] bool promptForPath(MRDialogHistoryScope scope, const char *title, char *fileName, std::size_t fileNameSize);
[[nodiscard]] bool resolveReadableExistingPath(MRDialogHistoryScope scope, const char *path, std::string &resolvedPath, bool reportErrors = true);
[[nodiscard]] bool loadResolvedFileIntoWindow(MREditWindow *win, const std::string &resolvedPath, const char *operationLabel = "Load file");
[[nodiscard]] bool loadResolvedFileIntoWindow(MREditWindow *win, const std::string &resolvedPath, const char *operationLabel, MRFileLoadMessages messages);
[[nodiscard]] bool openResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation = MRLoadedWindowActivation::ActivateLast, MREditWindow *restoreWindow = nullptr);
[[nodiscard]] bool openResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation, MREditWindow *restoreWindow, MRFileLoadMessages messages);
[[nodiscard]] bool openResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation, MREditWindow *restoreWindow, MRFileLoadMessages messages,
                                                MRWindowOpenBatch &openBatch);
[[nodiscard]] bool loadResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation = MRLoadedWindowActivation::ActivateLast, MREditWindow *restoreWindow = nullptr);
[[nodiscard]] bool loadResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation, MREditWindow *restoreWindow, MRFileLoadMessages messages);
[[nodiscard]] bool loadResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation, MREditWindow *restoreWindow, MRFileLoadMessages messages,
                                                MRWindowOpenBatch &openBatch);
[[nodiscard]] bool saveEditWindowAs(MREditWindow *win);
[[nodiscard]] bool saveAllDirtyEditWindows();
[[nodiscard]] bool revertEditWindow(MREditWindow *win);
[[nodiscard]] bool saveCurrentEditWindow();
[[nodiscard]] bool saveCurrentEditWindowAs();

#endif
