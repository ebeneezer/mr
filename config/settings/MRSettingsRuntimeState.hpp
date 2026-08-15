#ifndef MRSETTINGSRUNTIMESTATE_HPP
#define MRSETTINGSRUNTIMESTATE_HPP

#include "MRSettingsRuntime.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct MRSettingsRuntimeIoRateSnapshot {
	std::uint64_t readsPerMinute = 0;
	std::uint64_t writesPerMinute = 0;
};

struct MRConfiguredKeymapBatchState {
	bool initialized = false;
	bool profilesDirty = false;
	bool activeDirty = false;
	std::vector<MRKeymapProfile> profiles;
	std::string activeProfile;
};

void recordSettingsRuntimeRead();
void recordSettingsRuntimeWrite();
[[nodiscard]] MRSettingsRuntimeIoRateSnapshot settingsRuntimeIoRateSnapshot();

std::vector<std::string> configuredAutoexecMacroStorage();
void storeConfiguredAutoexecMacroStorage(const std::vector<std::string> &value);
bool configuredSettingsDirtyFlag();
void storeConfiguredSettingsDirtyFlag(bool value);
void markConfiguredSettingsDirty();
std::string configuredSettingsMacroFile();
void storeConfiguredSettingsMacroFile(const std::string &value);
std::string configuredMacroDirectory();
void storeConfiguredMacroDirectory(const std::string &value);
std::string configuredHelpFile();
void storeConfiguredHelpFile(const std::string &value);
std::string configuredTempDirectory();
void storeConfiguredTempDirectory(const std::string &value);
std::string configuredShellExecutable();
void storeConfiguredShellExecutable(const std::string &value);
std::string configuredAudioPlayer();
void storeConfiguredAudioPlayer(const std::string &value);
std::string configuredLogFile();
void storeConfiguredLogFile(const std::string &value);
std::string configuredColorThemeFile();
void storeConfiguredColorThemeFile(const std::string &value);
std::string configuredColorThemeDisplayNameValue();
void storeConfiguredColorThemeDisplayNameValue(const std::string &value);
MREditSetupSettings configuredEditSettings();
void storeConfiguredEditSettings(const MREditSetupSettings &value);
std::vector<MREditExtensionProfile> configuredEditProfiles();
void storeConfiguredEditProfiles(const std::vector<MREditExtensionProfile> &value);
std::vector<MRCompilerProfile> configuredCompilerProfilesValue();
void storeConfiguredCompilerProfilesValue(const std::vector<MRCompilerProfile> &value);
std::string detectedCompilerProfilesCacheKey();
std::vector<MRCompilerProfile> detectedCompilerProfilesCacheValue();
void storeDetectedCompilerProfilesCache(const std::string &key, const std::vector<MRCompilerProfile> &value);
std::vector<MRKeymapProfile> configuredKeymapProfilesValue();
void storeConfiguredKeymapProfilesValue(const std::vector<MRKeymapProfile> &value);
std::string configuredDefaultProfileDescriptionValue();
void storeConfiguredDefaultProfileDescriptionValue(const std::string &value);
std::string configuredKeymapFileValue();
void storeConfiguredKeymapFileValue(const std::string &value);
std::string configuredActiveKeymapProfileValue();
void storeConfiguredActiveKeymapProfileValue(const std::string &value);
MRColorSetupSettings configuredColorSettings();
void storeConfiguredColorSettings(const MRColorSetupSettings &value);
bool configuredColorSettingsInitialized();
void storeConfiguredColorSettingsInitialized(bool value);
MRColorOutputMode configuredColorOutputModeValue();
bool settingsStartupModeValue();
void storeSettingsStartupModeValue(bool value);
int settingsKeymapBatchDepthValue();
void storeSettingsKeymapBatchDepthValue(int value);
bool configuredKeymapBatchInitializedValue();
void initializeConfiguredKeymapBatchStateValue(const std::vector<MRKeymapProfile> &profiles, const std::string &activeProfile);
void storeConfiguredKeymapBatchActiveProfileValue(const std::string &activeProfile);
void storeConfiguredKeymapBatchProfileValue(const MRKeymapProfile &profile);
bool appendConfiguredKeymapBatchBindingValue(const MRKeymapBindingRecord &binding);
MRConfiguredKeymapBatchState configuredKeymapBatchStateValue();
void clearConfiguredKeymapBatchStateValue();

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
