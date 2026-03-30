#ifndef MRDIALOGPATHS_HPP
#define MRDIALOGPATHS_HPP

#include <cstddef>

void initRememberedLoadDialogPath(char *buffer, std::size_t bufferSize, const char *pattern);
void rememberLoadDialogPath(const char *path);

#endif
