#ifndef MRSETTINGSRUNTIMESTATE_HPP
#define MRSETTINGSRUNTIMESTATE_HPP

#include "MRSettingsRuntime.hpp"

#include <string>
#include <string_view>
#include <vector>

std::vector<std::string> &configuredAutoexecMacroStorage();
bool &configuredSettingsDirtyFlag();
void markConfiguredSettingsDirty();
std::string &configuredSettingsMacroFile();
std::string &configuredMacroDirectory();
std::string &configuredHelpFile();
std::string &configuredTempDirectory();
std::string &configuredShellExecutable();
std::string &configuredLogFile();
std::string &configuredColorThemeFile();
std::string &configuredColorThemeDisplayNameValue();
MREditSetupSettings &configuredEditSettings();
std::vector<MREditExtensionProfile> &configuredEditProfiles();
std::vector<MRKeymapProfile> &configuredKeymapProfilesValue();
std::string &configuredDefaultProfileDescriptionValue();
std::string &configuredKeymapFileValue();
std::string &configuredActiveKeymapProfileValue();
MRColorSetupSettings &configuredColorSettings();
bool &configuredColorSettingsInitialized();

[[nodiscard]] std::string normalizeDialogPath(const char *path);
[[nodiscard]] std::string expandUserPath(std::string_view input);
[[nodiscard]] bool isReadableDirectory(std::string_view path);
[[nodiscard]] bool isWritableDirectory(std::string_view path);
[[nodiscard]] bool isReadableFile(std::string_view path);
[[nodiscard]] bool isExecutableFile(std::string_view path);
[[nodiscard]] bool isWritableRegularFile(std::string_view path);
[[nodiscard]] std::string directoryPartOf(std::string_view path);
[[nodiscard]] bool hasDirectorySeparator(std::string_view path);
[[nodiscard]] std::string normalizeAutoexecMacroEntry(std::string_view value);
bool validateAutoexecMacroEntry(const std::string &value, std::string *errorMessage);
void copyToBuffer(char *buffer, std::size_t bufferSize, const std::string &value);
[[nodiscard]] std::string currentWorkingDirectory();
[[nodiscard]] bool isAbsolutePath(std::string_view path);
[[nodiscard]] std::string makeAbsolutePath(const std::string &path);
[[nodiscard]] std::string normalizedDialogDirectoryFromPath(const std::string &path);
[[nodiscard]] std::string fallbackRememberedLoadDirectory();
[[nodiscard]] std::string normalizeConfiguredPathInput(std::string_view input);
[[nodiscard]] MRSetupPaths resolveSetupPathDefaults();

#endif
