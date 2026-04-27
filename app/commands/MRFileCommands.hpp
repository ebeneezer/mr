#ifndef MRFILECOMMANDS_HPP
#define MRFILECOMMANDS_HPP

#include "../../config/MRDialogPaths.hpp"

#include <cstddef>
#include <string>

class MREditWindow;

[[nodiscard]] bool promptForPath(const char *title, char *fileName, std::size_t fileNameSize);
[[nodiscard]] bool resolveReadableExistingPath(MRDialogHistoryScope scope, const char *path,
                                               std::string &resolvedPath);
[[nodiscard]] bool loadResolvedFileIntoWindow(MREditWindow *win, const std::string &resolvedPath,
                                              const char *operationLabel = "Load file");
[[nodiscard]] bool saveCurrentEditWindow();
[[nodiscard]] bool saveCurrentEditWindowAs();

#endif
