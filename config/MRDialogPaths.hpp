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

struct MRFileExtensionEditorSettings {
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
	std::string backupMethod;
	std::string backupFrequency;
	std::string backupExtension;
	std::string backupDirectory;
	int autosaveInactivitySeconds;
	int autosaveIntervalSeconds;
	bool showEofMarker;
	bool showEofMarkerEmoji;
	bool showLineNumbers;
	bool lineNumZeroFill;
	bool persistentBlocks;
	bool codeFolding;
	std::string columnBlockMove;
	std::string defaultMode;
	std::string cursorStatusColor;

	MRFileExtensionEditorSettings() noexcept
	    : pageBreak(), wordDelimiters(), defaultExtensions(), truncateSpaces(true), eofCtrlZ(false),
	      eofCrLf(false), tabExpand(true), tabSize(8), rightMargin(78), wordWrap(true), indentStyle(),
	      fileType(), binaryRecordLength(100), postLoadMacro(), preSaveMacro(), defaultPath(), formatLine(),
	      backupMethod("BAK_FILE"), backupFrequency("FIRST_SAVE_ONLY"),
	      backupExtension("bak"), backupDirectory(), autosaveInactivitySeconds(15),
	      autosaveIntervalSeconds(180), showEofMarker(false),
	      showEofMarkerEmoji(true),
	      showLineNumbers(false), lineNumZeroFill(false), persistentBlocks(true), codeFolding(false), columnBlockMove(),
	      defaultMode(), cursorStatusColor() {
	}
};



enum class MRFileExtensionEditorSettingSection : unsigned char {
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

enum class MRFileExtensionEditorSettingKind : unsigned char {
	String,
	Boolean,
	Integer,
	Choice
};

enum MRFileExtensionEditorSettingsOverrideMask : unsigned long long {
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
	kOvBackupMethod = 1u << 18,
	kOvBackupFrequency = 1u << 19,
	kOvBackupExtension = 1u << 20,
	kOvBackupDirectory = 1u << 21,
	kOvAutosaveInactivitySeconds = 1u << 22,
	kOvAutosaveIntervalSeconds = 1u << 23,
	kOvShowEofMarker = 1u << 24,
	kOvShowEofMarkerEmoji = 1u << 25,
	kOvShowLineNumbers = 1u << 26,
	kOvLineNumZeroFill = 1u << 27,
	kOvPersistentBlocks = 1u << 28,
	kOvCodeFolding = 1u << 29,
	kOvColumnBlockMove = 1u << 30,
	kOvDefaultMode = 1u << 31,
	kOvCursorStatusColor = 1ull << 32,
};

struct MRFileExtensionEditorSettingDescriptor {
	const char *key;
	const char *label;
	MRFileExtensionEditorSettingSection section;
	MRFileExtensionEditorSettingKind kind;
	bool profileSupported;
	unsigned long long overrideBit;
};

struct MRFileExtensionEditorSettingsOverrides {
	MRFileExtensionEditorSettings values;
	unsigned long long mask;

	MRFileExtensionEditorSettingsOverrides() noexcept : values(), mask(kOvNone) {
	}
};

struct MRFileExtensionProfile {
	std::string id;
	std::string name;
	std::vector<std::string> extensions;
	std::string windowColorThemeUri;
	MRFileExtensionEditorSettingsOverrides overrides;
};

enum class MRColorSetupGroup : unsigned char {
	Window,
	MenuDialog,
	Help,
	Other
};

enum class MRSettingsKeyClass : unsigned char {
	Unknown,
	Version,
	Path,
	Global,
	Edit,
	ColorInline
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
[[nodiscard]] MRFileExtensionEditorSettings resolveFileExtensionEditorSettingsDefaults();
[[nodiscard]] MRColorSetupSettings resolveColorSetupDefaults();
[[nodiscard]] MRFileExtensionEditorSettings configuredFileExtensionEditorSettings();
[[nodiscard]] MRColorSetupSettings configuredColorSetupSettings();
bool setConfiguredFileExtensionEditorSettings(const MRFileExtensionEditorSettings &settings, std::string *errorMessage = nullptr);
bool applyConfiguredFileExtensionEditorSettingValue(const std::string &key, const std::string &value,
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

[[nodiscard]] MRSettingsKeyClass classifySettingsKey(std::string_view key);
[[nodiscard]] bool isCanonicalSerializedSettingsKey(std::string_view key);
[[nodiscard]] std::size_t canonicalSerializedSettingsKeyCount();
bool resetConfiguredSettingsModel(const std::string &settingsPath, MRSetupPaths &paths,
                                  std::string *errorMessage = nullptr);
bool applyConfiguredSettingsAssignment(const std::string &key, const std::string &value, MRSetupPaths &paths,
                                       std::string *errorMessage = nullptr);

const MRFileExtensionEditorSettingDescriptor *fileExtensionEditorSettingDescriptors(std::size_t &count);
[[nodiscard]] const MRFileExtensionEditorSettingDescriptor *findFileExtensionEditorSettingDescriptorByKey(std::string_view key);
[[nodiscard]] std::string normalizeEditExtensionSelector(std::string_view value);
bool normalizeEditExtensionSelectors(std::vector<std::string> &selectors, std::string *errorMessage = nullptr);
MRFileExtensionEditorSettings mergeFileExtensionEditorSettings(const MRFileExtensionEditorSettings &defaults,
                                           const MRFileExtensionEditorSettingsOverrides &overrides);
const std::vector<MRFileExtensionProfile> &configuredFileExtensionProfiles();
bool setConfiguredFileExtensionProfiles(const std::vector<MRFileExtensionProfile> &profiles,
                                        std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredDefaultProfileDescription();
bool setConfiguredDefaultProfileDescription(const std::string &value,
                                            std::string *errorMessage = nullptr);
bool applyConfiguredFileExtensionProfileDirective(const std::string &operation, const std::string &profileId,
                                                  const std::string &arg3, const std::string &arg4,
                                                  std::string *errorMessage = nullptr);
bool effectiveFileExtensionEditorSettingsForPath(const std::string &path, MRFileExtensionEditorSettings &out,
                                       std::string *matchedProfileName = nullptr);
bool effectiveEditWindowColorThemePathForPath(const std::string &path, std::string &themeUri,
                                              std::string *matchedProfileName = nullptr);
[[nodiscard]] std::string formatFileExtensionEditorSettingBoolean(bool value);
std::vector<std::string> configuredDefaultExtensionList();
[[nodiscard]] bool configuredDefaultInsertMode();
[[nodiscard]] bool configuredTabExpandSetting();
[[nodiscard]] int configuredTabSizeSetting();
[[nodiscard]] bool configuredPersistentBlocksSetting();
[[nodiscard]] char configuredPageBreakCharacter();
bool setConfiguredLastFileDialogPath(const std::string &path, std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredLastFileDialogPath();
struct MRSettingsWriteReport {
	std::string settingsPath;
	bool fileWritten = false;
	bool contentChanged = false;
	std::size_t addedCount = 0;
	std::size_t removedCount = 0;
	std::size_t changedCount = 0;
	std::vector<std::string> logLines;
};

[[nodiscard]] std::string buildSettingsMacroSource(const MRSetupPaths &paths);
bool persistConfiguredSettingsSnapshot(std::string *errorMessage = nullptr,
                                       MRSettingsWriteReport *report = nullptr);
bool writeSettingsMacroFile(const MRSetupPaths &paths, std::string *errorMessage = nullptr,
                            MRSettingsWriteReport *report = nullptr);
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
bool normalizeBackupExtension(const std::string &value, std::string &outValue, std::string *errorMessage = nullptr);
bool validateWritableDirectoryPath(const std::string &path, const std::string &fieldLabel, std::string *errorMessage = nullptr);
bool validateBackupDirectoryPath(const std::string &path, std::string *errorMessage = nullptr);
bool validateTempDirectoryPath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredTempDirectoryPath(const std::string &path, std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredTempDirectoryPath();
bool validateShellExecutablePath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredShellExecutablePath(const std::string &path, std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredShellExecutablePath();
[[nodiscard]] std::string defaultSettingsMacroFilePath();
[[nodiscard]] std::string defaultMacroDirectoryPath();

#endif
