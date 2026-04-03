#ifndef MRFILECOMMANDS_HPP
#define MRFILECOMMANDS_HPP

#include <cstddef>
#include <string>

class TMREditWindow;

bool promptForPath(const char *title, char *fileName, std::size_t fileNameSize);
bool resolveReadableExistingPath(const char *path, std::string &resolvedPath);
bool loadResolvedFileIntoWindow(TMREditWindow *win, const std::string &resolvedPath,
                                const char *operationLabel = "Load file");
bool saveCurrentEditWindow();
bool saveCurrentEditWindowAs();

#endif
