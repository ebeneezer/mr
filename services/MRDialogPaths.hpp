#ifndef MRDIALOGPATHS_HPP
#define MRDIALOGPATHS_HPP

#include <cstddef>
#include <string>
#include <vector>

struct MRSetupPaths {
	std::string settingsMacroUri;
	std::string macroPath;
	std::string helpUri;
	std::string tempPath;
	std::string shellUri;
};

struct MREditSetupSettings {
	std::string pageBreak;
	std::string wordDelimiters;
	std::string defaultExtensions;
	bool truncateSpaces;
	bool eofCtrlZ;
	bool eofCrLf;
	bool tabExpand;
	std::string columnBlockMove;
	std::string defaultMode;
	std::string cursorVisibility;

	MREditSetupSettings() noexcept
	    : pageBreak(), wordDelimiters(), defaultExtensions(), truncateSpaces(true), eofCtrlZ(false),
	      eofCrLf(false), tabExpand(true), columnBlockMove(), defaultMode(), cursorVisibility() {
	}
};

void initRememberedLoadDialogPath(char *buffer, std::size_t bufferSize, const char *pattern);
void rememberLoadDialogPath(const char *path);
std::string normalizeConfiguredPathInput(const std::string &input);
MRSetupPaths resolveSetupPathDefaults();
MREditSetupSettings resolveEditSetupDefaults();
MREditSetupSettings configuredEditSetupSettings();
bool setConfiguredEditSetupSettings(const MREditSetupSettings &settings, std::string *errorMessage = nullptr);
bool applyConfiguredEditSetupValue(const std::string &key, const std::string &value,
                                   std::string *errorMessage = nullptr);
std::string formatEditSetupBoolean(bool value);
std::vector<std::string> configuredDefaultExtensionList();
bool configuredDefaultInsertMode();
bool configuredTabExpandSetting();
char configuredPageBreakCharacter();
std::string configuredCursorVisibility();
unsigned short configuredCursorTypeCode();
std::string buildSettingsMacroSource(const MRSetupPaths &paths);
bool writeSettingsMacroFile(const MRSetupPaths &paths, std::string *errorMessage = nullptr);
bool ensureSettingsMacroFileExists(const std::string &settingsMacroUri, std::string *errorMessage = nullptr);
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
