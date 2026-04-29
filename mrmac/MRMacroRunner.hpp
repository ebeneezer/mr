#ifndef MRMACRORUNNER_HPP
#define MRMACRORUNNER_HPP

#include <string>

bool runMacroFileByPath(const char *path);
bool runMacroFileByPath(const char *path, std::string *errorMessage, bool showErrorDialogs = true);
bool runMacroSpecByName(const char *macroSpec, std::string *errorMessage = nullptr, bool showErrorDialogs = true);
void pumpForegroundMacroDelays();
void cancelForegroundMacroDelays();

#endif
