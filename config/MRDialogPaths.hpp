#ifndef MRDIALOGPATHS_HPP
#define MRDIALOGPATHS_HPP

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
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
	int rightMargin;
	bool wordWrap;
	std::string indentStyle;
	std::string fileType;
	int binaryRecordLength;
	std::string postLoadMacro;
	std::string preSaveMacro;
	std::string defaultPath;
	std::string formatLine;
	bool backupFiles;
	bool showEofMarker;
	bool showEofMarkerEmoji;
	bool showLineNumbers;
	bool lineNumZeroFill;
	bool persistentBlocks;
	bool codeFolding;
	std::string columnBlockMove;
	std::string defaultMode;
	std::string cursorStatusColor;

	MREditSetupSettings() noexcept
	    : pageBreak(), wordDelimiters(), defaultExtensions(), truncateSpaces(true), eofCtrlZ(false),
	      eofCrLf(false), tabExpand(true), tabSize(8), rightMargin(78), wordWrap(true), indentStyle(),
	      fileType(), binaryRecordLength(78), postLoadMacro(), preSaveMacro(), defaultPath(), formatLine(),
	      backupFiles(true), showEofMarker(false),
	      showEofMarkerEmoji(true),
	      showLineNumbers(false), lineNumZeroFill(false), persistentBlocks(true), codeFolding(false), columnBlockMove(),
	      defaultMode(), cursorStatusColor() {
	}
};



enum class MREditSettingSection : unsigned char {
	Text,
	OpenFile,
	Save,
	Tabs,
	Formatting,
	Macros,
	Paths,
	Display,
	Blocks,
	Mode
};

enum class MREditSettingKind : unsigned char {
	String,
	Boolean,
	Integer,
	Choice
};

enum MREditSetupOverrideMask : unsigned int {
	kOvNone = 0,
	kOvPageBreak = 1u << 0,
	kOvWordDelimiters = 1u << 1,
	kOvDefaultExtensions = 1u << 2,
	kOvTruncateSpaces = 1u << 3,
	kOvEofCtrlZ = 1u << 4,
	kOvEofCrLf = 1u << 5,
	kOvTabExpand = 1u << 6,
	kOvTabSize = 1u << 7,
	kOvRightMargin = 1u << 8,
	kOvWordWrap = 1u << 9,
	kOvIndentStyle = 1u << 10,
	kOvFileType = 1u << 11,
	kOvBinaryRecordLength = 1u << 12,
	kOvPostLoadMacro = 1u << 13,
	kOvPreSaveMacro = 1u << 14,
	kOvDefaultPath = 1u << 15,
	kOvFormatLine = 1u << 16,
	kOvBackupFiles = 1u << 17,
	kOvShowEofMarker = 1u << 18,
	kOvShowEofMarkerEmoji = 1u << 19,
	kOvShowLineNumbers = 1u << 20,
	kOvLineNumZeroFill = 1u << 21,
	kOvPersistentBlocks = 1u << 22,
	kOvCodeFolding = 1u << 23,
	kOvColumnBlockMove = 1u << 24,
	kOvDefaultMode = 1u << 25,
	kOvCursorStatusColor = 1u << 26,
};

struct MREditSettingDescriptor {
	const char *key;
	const char *label;
	MREditSettingSection section;
	MREditSettingKind kind;
	bool profileSupported;
	unsigned int overrideBit;
};

struct MREditSetupOverrides {
	MREditSetupSettings values;
	unsigned int mask;

	MREditSetupOverrides() noexcept : values(), mask(kOvNone) {
	}
};

struct MREditExtensionProfile {
	std::string id;
	std::string name;
	std::vector<std::string> extensions;
	std::string windowColorThemeUri;
	MREditSetupOverrides overrides;
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
	kMrPaletteDialogInactiveElements = 144,
	kMrPaletteMessageHero = 145,
	kMrPaletteCursorPositionMarker = 146,
	kMrPaletteMax = kMrPaletteCursorPositionMarker
};

struct MRColorSetupSettings {
	static const std::size_t kWindowCount = 9;
	static const std::size_t kMenuDialogCount = 17;
	static const std::size_t kHelpCount = 9;
	static const std::size_t kOtherCount = 9;

	std::array<unsigned char, kWindowCount> windowColors;
	std::array<unsigned char, kMenuDialogCount> menuDialogColors;
	std::array<unsigned char, kHelpCount> helpColors;
	std::array<unsigned char, kOtherCount> otherColors;

	MRColorSetupSettings() noexcept : windowColors(), menuDialogColors(), helpColors(), otherColors() {
	}
};

void initRememberedLoadDialogPath(char *buffer, std::size_t bufferSize, const char *pattern);
void rememberLoadDialogPath(const char *path);
[[nodiscard]] std::string normalizeConfiguredPathInput(std::string_view input);
[[nodiscard]] MRSetupPaths resolveSetupPathDefaults();
[[nodiscard]] MREditSetupSettings resolveEditSetupDefaults();
[[nodiscard]] MRColorSetupSettings resolveColorSetupDefaults();
[[nodiscard]] MREditSetupSettings configuredEditSetupSettings();
[[nodiscard]] MRColorSetupSettings configuredColorSetupSettings();
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
[[nodiscard]] std::string configuredColorThemeFilePath();
[[nodiscard]] std::string configuredColorThemeDisplayName();
[[nodiscard]] std::string defaultColorThemeFilePath();
bool validateColorThemeFilePath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredColorThemeFilePath(const std::string &path, std::string *errorMessage = nullptr);
bool writeColorThemeFile(const std::string &themeUri, std::string *errorMessage = nullptr);
bool ensureColorThemeFileExists(const std::string &themeUri, std::string *errorMessage = nullptr);
bool loadColorThemeFile(const std::string &themeUri, std::string *errorMessage = nullptr);
bool loadWindowColorThemeGroupValues(const std::string &themeUri,
                                     std::array<unsigned char, MRColorSetupSettings::kWindowCount> &outValues,
                                     std::string *errorMessage = nullptr);

const MREditSettingDescriptor *editSettingDescriptors(std::size_t &count);
[[nodiscard]] const MREditSettingDescriptor *findEditSettingDescriptorByKey(std::string_view key);
[[nodiscard]] std::string normalizeEditExtensionSelector(std::string_view value);
bool normalizeEditExtensionSelectors(std::vector<std::string> &selectors, std::string *errorMessage = nullptr);
MREditSetupSettings mergeEditSetupSettings(const MREditSetupSettings &defaults,
                                           const MREditSetupOverrides &overrides);
const std::vector<MREditExtensionProfile> &configuredEditExtensionProfiles();
bool setConfiguredEditExtensionProfiles(const std::vector<MREditExtensionProfile> &profiles,
                                        std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredDefaultProfileDescription();
bool setConfiguredDefaultProfileDescription(const std::string &value,
                                            std::string *errorMessage = nullptr);
bool applyConfiguredEditExtensionProfileDirective(const std::string &operation, const std::string &profileId,
                                                  const std::string &arg3, const std::string &arg4,
                                                  std::string *errorMessage = nullptr);
bool effectiveEditSetupSettingsForPath(const std::string &path, MREditSetupSettings &out,
                                       std::string *matchedProfileName = nullptr);
bool effectiveEditWindowColorThemePathForPath(const std::string &path, std::string &themeUri,
                                              std::string *matchedProfileName = nullptr);
[[nodiscard]] std::string formatEditSetupBoolean(bool value);
std::vector<std::string> configuredDefaultExtensionList();
[[nodiscard]] bool configuredDefaultInsertMode();
[[nodiscard]] bool configuredTabExpandSetting();
[[nodiscard]] int configuredTabSizeSetting();
[[nodiscard]] bool configuredBackupFilesSetting();
[[nodiscard]] bool configuredPersistentBlocksSetting();
[[nodiscard]] char configuredPageBreakCharacter();
bool setConfiguredLastFileDialogPath(const std::string &path, std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredLastFileDialogPath();
[[nodiscard]] std::string buildSettingsMacroSource(const MRSetupPaths &paths);
bool persistConfiguredSettingsSnapshot(std::string *errorMessage = nullptr);
bool writeSettingsMacroFile(const MRSetupPaths &paths, std::string *errorMessage = nullptr);
bool ensureSettingsMacroFileExists(const std::string &settingsMacroUri, std::string *errorMessage = nullptr);
bool validateSettingsMacroFilePath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredSettingsMacroFilePath(const std::string &path, std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredSettingsMacroFilePath();
bool validateMacroDirectoryPath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredMacroDirectoryPath(const std::string &path, std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredMacroDirectoryPath();
bool validateHelpFilePath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredHelpFilePath(const std::string &path, std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredHelpFilePath();
bool validateTempDirectoryPath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredTempDirectoryPath(const std::string &path, std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredTempDirectoryPath();
bool validateShellExecutablePath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredShellExecutablePath(const std::string &path, std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredShellExecutablePath();
[[nodiscard]] std::string defaultSettingsMacroFilePath();
[[nodiscard]] std::string defaultMacroDirectoryPath();

#endif
