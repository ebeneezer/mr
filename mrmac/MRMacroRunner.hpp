#ifndef MRMACRORUNNER_HPP
#define MRMACRORUNNER_HPP

#include "MRMacroExecutionSession.hpp"

#include <cstddef>
#include <string>

bool runMacroFileByPath(const char *path);
bool runMacroFileByPath(const char *path, std::string *errorMessage, bool showErrorDialogs = true);
bool runMacroFileByPathOnUiThread(const char *path, std::string *errorMessage = nullptr, bool showErrorDialogs = true);
bool runMacroSourceText(const char *displayName, const char *source, std::string *errorMessage = nullptr, bool showErrorDialogs = true);
bool runMacroSourceTextAsExecutionSession(const char *displayName, const char *source, MRMacroExecutionSession *sessionOut, std::string *errorMessage = nullptr, bool showErrorDialogs = true);
bool runMacroSourceTextAsExecutionSessionForOwner(const char *displayName, const char *source, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut, std::string *errorMessage = nullptr, bool showErrorDialogs = true);
bool runMacroSourceUnitAsExecutionSessionForOwner(const char *displayName, const char *source, const char *unitName, const char *closureId, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut, std::string *errorMessage = nullptr, bool showErrorDialogs = true);
bool runMacroSpecByName(const char *macroSpec, std::string *errorMessage = nullptr, bool showErrorDialogs = true);
bool runMacroSpecByNameAsExecutionSession(const char *macroSpec, MRMacroExecutionSession *sessionOut, std::string *errorMessage = nullptr, bool showErrorDialogs = true);
bool runMacroSpecByNameAsExecutionSessionForOwner(const char *macroSpec, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut, std::string *errorMessage = nullptr, bool showErrorDialogs = true);
bool runMacroSpecByNameAsExecutionSessionForOwnerOnUiThread(const char *macroSpec, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut, std::string *errorMessage = nullptr, bool showErrorDialogs = true);
void pumpForegroundMacroDelays();
void cancelForegroundMacroDelays();
std::size_t requestMacroExecutionCancellationForOwner(const MRMacroExecutionOwner &owner);
std::size_t requestMacroExecutionCancellationForBuffer(int bufferId);

#endif
