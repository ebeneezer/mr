#ifndef MRDIALOGPATHS_HPP
#define MRDIALOGPATHS_HPP

#include <cstddef>
#include <string>

void initRememberedLoadDialogPath(char *buffer, std::size_t bufferSize, const char *pattern);
void rememberLoadDialogPath(const char *path);
std::string normalizeConfiguredPathInput(const std::string &input);
bool validateSettingsMacroFilePath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredSettingsMacroFilePath(const std::string &path, std::string *errorMessage = nullptr);
std::string configuredSettingsMacroFilePath();
bool validateMacroDirectoryPath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredMacroDirectoryPath(const std::string &path, std::string *errorMessage = nullptr);
std::string configuredMacroDirectoryPath();
bool validateHelpFilePath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredHelpFilePath(const std::string &path, std::string *errorMessage = nullptr);
std::string configuredHelpFilePath();
bool validateTempDirectoryPath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredTempDirectoryPath(const std::string &path, std::string *errorMessage = nullptr);
std::string configuredTempDirectoryPath();
bool validateShellExecutablePath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredShellExecutablePath(const std::string &path, std::string *errorMessage = nullptr);
std::string configuredShellExecutablePath();
std::string defaultSettingsMacroFilePath();
std::string defaultMacroDirectoryPath();

#endif
