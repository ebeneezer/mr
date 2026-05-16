#ifndef MRFILECOMMANDS_HPP
#define MRFILECOMMANDS_HPP

#include "../../config/settings/MRSettingsRuntime.hpp"

#include <cstddef>
#include <string>
#include <vector>

class MREditWindow;

enum class MRLoadedWindowActivation : unsigned char {
	ActivateLast = 0,
	KeepBackground
};

[[nodiscard]] bool promptForPath(const char *title, char *fileName, std::size_t fileNameSize);
[[nodiscard]] bool resolveReadableExistingPath(MRDialogHistoryScope scope, const char *path, std::string &resolvedPath, bool reportErrors = true);
[[nodiscard]] bool loadResolvedFileIntoWindow(MREditWindow *win, const std::string &resolvedPath, const char *operationLabel = "Load file");
[[nodiscard]] bool openResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation = MRLoadedWindowActivation::ActivateLast, MREditWindow *restoreWindow = nullptr);
[[nodiscard]] bool loadResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation = MRLoadedWindowActivation::ActivateLast, MREditWindow *restoreWindow = nullptr);
[[nodiscard]] bool saveCurrentEditWindow();
[[nodiscard]] bool saveCurrentEditWindowAs();

#endif
