#ifndef MRDIALOGPATHS_HPP
#define MRDIALOGPATHS_HPP

#include <array>
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
	int tabSize;
	bool backupFiles;
	bool showEofMarker;
	bool showEofMarkerEmoji;
	bool showLineNumbers;
	bool lineNumZeroFill;
	bool persistentBlocks;
	std::string columnBlockMove;
	std::string defaultMode;

	MREditSetupSettings() noexcept
	    : pageBreak(), wordDelimiters(), defaultExtensions(), truncateSpaces(true), eofCtrlZ(false),
	      eofCrLf(false), tabExpand(true), tabSize(8), backupFiles(true), showEofMarker(false),
	      showEofMarkerEmoji(true),
	      showLineNumbers(false), lineNumZeroFill(false), persistentBlocks(true), columnBlockMove(),
	      defaultMode() {
	}
};

enum class MRColorSetupGroup : unsigned char {
	Window,
	MenuDialog,
	Help,
	Other
};

struct MRColorSetupItem {
	const char *label;
	unsigned char paletteIndex;
};

enum : unsigned char {
	kMrPaletteCurrentLine = 136,
	kMrPaletteCurrentLineInBlock = 137,
	kMrPaletteChangedText = 138,
	kMrPaletteMessageError = 139,
	kMrPaletteMessage = 140,
	kMrPaletteMessageWarning = 141,
	kMrPaletteLineNumbers = 142,
	kMrPaletteEofMarker = 143,
	kMrPaletteMax = kMrPaletteEofMarker
};

struct MRColorSetupSettings {
	static const std::size_t kWindowCount = 9;
	static const std::size_t kMenuDialogCount = 16;
	static const std::size_t kHelpCount = 9;
	static const std::size_t kOtherCount = 7;

	std::array<unsigned char, kWindowCount> windowColors;
	std::array<unsigned char, kMenuDialogCount> menuDialogColors;
	std::array<unsigned char, kHelpCount> helpColors;
	std::array<unsigned char, kOtherCount> otherColors;

	MRColorSetupSettings() noexcept : windowColors(), menuDialogColors(), helpColors(), otherColors() {
	}
};

void initRememberedLoadDialogPath(char *buffer, std::size_t bufferSize, const char *pattern);
void rememberLoadDialogPath(const char *path);
std::string normalizeConfiguredPathInput(const std::string &input);
MRSetupPaths resolveSetupPathDefaults();
MREditSetupSettings resolveEditSetupDefaults();
MRColorSetupSettings resolveColorSetupDefaults();
MREditSetupSettings configuredEditSetupSettings();
MRColorSetupSettings configuredColorSetupSettings();
bool setConfiguredEditSetupSettings(const MREditSetupSettings &settings, std::string *errorMessage = nullptr);
bool applyConfiguredEditSetupValue(const std::string &key, const std::string &value,
                                   std::string *errorMessage = nullptr);
bool applyConfiguredColorSetupValue(const std::string &key, const std::string &value,
                                    std::string *errorMessage = nullptr);
bool configuredColorSlotOverride(unsigned char paletteIndex, unsigned char &value);
const char *colorSetupGroupTitle(MRColorSetupGroup group);
const char *colorSetupGroupKey(MRColorSetupGroup group);
const MRColorSetupItem *colorSetupGroupItems(MRColorSetupGroup group, std::size_t &count);
bool setConfiguredColorSetupGroupValues(MRColorSetupGroup group, const unsigned char *values,
                                        std::size_t count, std::string *errorMessage = nullptr);
void configuredColorSetupGroupValues(MRColorSetupGroup group, unsigned char *values, std::size_t count);
std::string configuredColorThemeFilePath();
std::string configuredColorThemeDisplayName();
std::string defaultColorThemeFilePath();
bool validateColorThemeFilePath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredColorThemeFilePath(const std::string &path, std::string *errorMessage = nullptr);
bool writeColorThemeFile(const std::string &themeUri, std::string *errorMessage = nullptr);
bool ensureColorThemeFileExists(const std::string &themeUri, std::string *errorMessage = nullptr);
bool loadColorThemeFile(const std::string &themeUri, std::string *errorMessage = nullptr);
std::string formatEditSetupBoolean(bool value);
std::vector<std::string> configuredDefaultExtensionList();
bool configuredDefaultInsertMode();
bool configuredTabExpandSetting();
int configuredTabSizeSetting();
bool configuredBackupFilesSetting();
bool configuredPersistentBlocksSetting();
char configuredPageBreakCharacter();
bool setConfiguredLastFileDialogPath(const std::string &path, std::string *errorMessage = nullptr);
std::string configuredLastFileDialogPath();
std::string buildSettingsMacroSource(const MRSetupPaths &paths);
bool persistConfiguredSettingsSnapshot(std::string *errorMessage = nullptr);
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
