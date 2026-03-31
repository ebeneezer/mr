#ifndef MRDIALOGPATHS_HPP
#define MRDIALOGPATHS_HPP

#include <cstddef>
#include <string>

void initRememberedLoadDialogPath(char *buffer, std::size_t bufferSize, const char *pattern);
void rememberLoadDialogPath(const char *path);
std::string defaultMacroDirectoryPath();

#endif
