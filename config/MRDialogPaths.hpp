#ifndef MRDIALOGPATHS_HPP
#define MRDIALOGPATHS_HPP

#include "../keymap/MRKeymapProfile.hpp"

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

	auto operator==(const MRSetupPaths &) const noexcept -> bool = default;
};

struct MREditSetupSettings {
	std::string pageBreak;
	std::string wordDelimiters;
	std::string defaultExtensions;
	bool truncateSpaces;
	bool eofCtrlZ;
	bool eofCrLf;
	bool tabExpand;
	bool displayTabs;
	int tabSize;
	int leftMargin;
	int rightMargin;
	bool formatRuler;
	bool wordWrap;
	std::string indentStyle;
	std::string codeLanguage;
	bool codeColoring;
	bool codeFoldingFeature;
	bool smartIndenting;
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
	bool backupFiles;
	bool showEofMarker;
	bool showEofMarkerEmoji;
	bool showLineNumbers;
	std::string lineNumbersPosition;
	bool lineNumZeroFill;
	bool persistentBlocks;
	bool codeFolding;
	std::string codeFoldingPosition;
	std::string columnBlockMove;
	std::string defaultMode;
	std::string cursorStatusColor;
	std::string miniMapPosition;
	int miniMapWidth;
	std::string miniMapMarkerGlyph;
	std::string gutters;

	MREditSetupSettings() noexcept : pageBreak(), wordDelimiters(), defaultExtensions(), truncateSpaces(true), eofCtrlZ(false), eofCrLf(false), tabExpand(true), displayTabs(false), tabSize(8), leftMargin(1), rightMargin(78), formatRuler(false), wordWrap(true), indentStyle(), codeLanguage("NONE"), codeColoring(false), codeFoldingFeature(false), smartIndenting(false), fileType(), binaryRecordLength(100), postLoadMacro(), preSaveMacro(), defaultPath(), formatLine(), backupMethod("BAK_FILE"), backupFrequency("FIRST_SAVE_ONLY"), backupExtension("bak"), backupDirectory(), autosaveInactivitySeconds(15), autosaveIntervalSeconds(180), backupFiles(true), showEofMarker(false), showEofMarkerEmoji(true), showLineNumbers(false), lineNumbersPosition("OFF"), lineNumZeroFill(false), persistentBlocks(true), codeFolding(false), codeFoldingPosition("OFF"), columnBlockMove(), defaultMode(), cursorStatusColor(), miniMapPosition("OFF"), miniMapWidth(4), miniMapMarkerGlyph("│"), gutters("LCM") {
	}

	auto operator==(const MREditSetupSettings &) const noexcept -> bool = default;
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

enum MREditSetupOverrideMask : unsigned long long {
	kOvNone = 0,
	kOvPageBreak = 1ull << 0,
	kOvWordDelimiters = 1ull << 1,
	kOvDefaultExtensions = 1ull << 2,
	kOvTruncateSpaces = 1ull << 3,
	kOvEofCtrlZ = 1ull << 4,
	kOvEofCrLf = 1ull << 5,
	kOvTabExpand = 1ull << 6,
	kOvTabSize = 1ull << 7,
	kOvRightMargin = 1ull << 8,
	kOvWordWrap = 1ull << 9,
	kOvIndentStyle = 1ull << 10,
	kOvFileType = 1ull << 11,
	kOvBinaryRecordLength = 1ull << 12,
	kOvPostLoadMacro = 1ull << 13,
	kOvPreSaveMacro = 1ull << 14,
	kOvDefaultPath = 1ull << 15,
	kOvFormatLine = 1ull << 16,
	kOvBackupFiles = 1ull << 17,
	kOvShowEofMarker = 1ull << 18,
	kOvShowEofMarkerEmoji = 1ull << 19,
	kOvShowLineNumbers = 1ull << 20,
	kOvLineNumZeroFill = 1ull << 21,
	kOvPersistentBlocks = 1ull << 22,
	kOvCodeFolding = 1ull << 23,
	kOvColumnBlockMove = 1ull << 24,
	kOvDefaultMode = 1ull << 25,
	kOvCursorStatusColor = 1ull << 26,
	kOvBackupMethod = 1ull << 27,
	kOvBackupFrequency = 1ull << 28,
	kOvBackupExtension = 1ull << 29,
	kOvBackupDirectory = 1ull << 30,
	kOvAutosaveInactivitySeconds = 1ull << 31,
	kOvAutosaveIntervalSeconds = 1ull << 32,
	kOvMiniMapPosition = 1ull << 33,
	kOvMiniMapWidth = 1ull << 34,
	kOvMiniMapMarkerGlyph = 1ull << 35,
	kOvLineNumbersPosition = 1ull << 36,
	kOvCodeFoldingPosition = 1ull << 37,
	kOvGutters = 1ull << 38,
	kOvDisplayTabs = 1ull << 39,
	kOvLeftMargin = 1ull << 40,
	kOvFormatRuler = 1ull << 41,
	kOvCodeLanguage = 1ull << 42,
	kOvCodeColoring = 1ull << 43,
	kOvSmartIndenting = 1ull << 44,
	kOvCodeFoldingFeature = 1ull << 45,
};

struct MREditSettingDescriptor {
	const char *key;
	const char *label;
	MREditSettingSection section;
	MREditSettingKind kind;
	bool profileSupported;
	unsigned long long overrideBit;
};

struct MREditSetupOverrides {
	MREditSetupSettings values;
	unsigned long long mask;

	MREditSetupOverrides() noexcept : values(), mask(kOvNone) {
	}

	auto operator==(const MREditSetupOverrides &) const noexcept -> bool = default;
};

struct MREditExtensionProfile {
	std::string id;
	std::string name;
	std::vector<std::string> extensions;
	std::string windowColorThemeUri;
	MREditSetupOverrides overrides;

	auto operator==(const MREditExtensionProfile &) const noexcept -> bool = default;
};

enum class MRColorSetupGroup : unsigned char {
	Window,
	MenuDialog,
	Help,
	Other,
	MiniMap,
	Code
};

enum class MRLogHandling : unsigned char {
	Volatile = 0,
	Persist = 1,
	Journalctl = 2
};

enum class MRCursorBehaviour : unsigned char {
	BoundToText = 0,
	FreeMovement = 1
};

enum class MRSettingsKeyClass : unsigned char {
	Unknown,
	Version,
	Path,
	Global,
	Edit,
	ColorInline
};

enum class MRSearchTextType : unsigned char {
	Literal = 0,
	Pcre = 1,
	Word = 2
};

enum class MRSearchDirection : unsigned char {
	Forward = 0,
	Backward = 1
};

enum class MRSearchMode : unsigned char {
	StopFirst = 0,
	PromptNext = 1,
	ListAll = 2
};

enum class MRSarMode : unsigned char {
	ReplaceFirst = 0,
	PromptEach = 1,
	ReplaceAll = 2
};

enum class MRSarLeaveCursor : unsigned char {
	EndOfReplaceString = 0,
	StartOfReplaceString = 1
};

struct MRSearchDialogOptions {
	MRSearchTextType textType;
	MRSearchDirection direction;
	MRSearchMode mode;
	bool caseSensitive;
	bool globalSearch;
	bool restrictToMarkedBlock;
	bool searchAllWindows;

	MRSearchDialogOptions() noexcept : textType(MRSearchTextType::Literal), direction(MRSearchDirection::Forward), mode(MRSearchMode::StopFirst), caseSensitive(false), globalSearch(true), restrictToMarkedBlock(false), searchAllWindows(false) {
	}

	auto operator==(const MRSearchDialogOptions &) const noexcept -> bool = default;
};

struct MRSarDialogOptions {
	MRSearchTextType textType;
	MRSearchDirection direction;
	MRSarMode mode;
	MRSarLeaveCursor leaveCursorAt;
	bool caseSensitive;
	bool globalSearch;
	bool restrictToMarkedBlock;
	bool searchAllWindows;

	MRSarDialogOptions() noexcept : textType(MRSearchTextType::Literal), direction(MRSearchDirection::Forward), mode(MRSarMode::ReplaceFirst), leaveCursorAt(MRSarLeaveCursor::EndOfReplaceString), caseSensitive(false), globalSearch(true), restrictToMarkedBlock(false), searchAllWindows(false) {
	}

	auto operator==(const MRSarDialogOptions &) const noexcept -> bool = default;
};

struct MRMultiSearchDialogOptions {
	bool searchSubdirectories;
	bool caseSensitive;
	bool regularExpressions;
	bool searchFilesInMemory;
	std::string filespec;
	std::string startingPath;
	std::string searchText;

	MRMultiSearchDialogOptions() noexcept : searchSubdirectories(true), caseSensitive(false), regularExpressions(true), searchFilesInMemory(true), filespec("*.*"), startingPath(), searchText() {
	}

	auto operator==(const MRMultiSearchDialogOptions &) const noexcept -> bool = default;
};

struct MRMultiSarDialogOptions {
	bool searchSubdirectories;
	bool caseSensitive;
	bool regularExpressions;
	bool searchFilesInMemory;
	bool keepFilesOpen;
	std::string filespec;
	std::string startingPath;
	std::string searchText;
	std::string replacementText;

	MRMultiSarDialogOptions() noexcept : searchSubdirectories(true), caseSensitive(false), regularExpressions(true), searchFilesInMemory(true), keepFilesOpen(false), filespec("*.*"), startingPath(), searchText(), replacementText() {
	}

	auto operator==(const MRMultiSarDialogOptions &) const noexcept -> bool = default;
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
	kMrPaletteMiniMapNormal = 147,
	kMrPaletteMiniMapViewport = 148,
	kMrPaletteMiniMapChanged = 149,
	kMrPaletteMiniMapFindMarker = 150,
	kMrPaletteMiniMapErrorMarker = 151,
	kMrPaletteCodeFolding = 152,
	kMrPaletteDesktop = 153,
	kMrPaletteVirtualDesktopMarker = 154,
	kMrPaletteFormatRuler = 155,
	kMrPaletteCodeComments = 156,
	kMrPaletteCodeStrings = 157,
	kMrPaletteCodeCharacters = 158,
	kMrPaletteCodeNumbers = 159,
	kMrPaletteCodeKeywords = 160,
	kMrPaletteCodeTypes = 161,
	kMrPaletteCodeDirectives = 162,
	kMrPaletteCodeFunctions = 163,
	kMrPaletteCodeBuiltins = 164,
	kMrPaletteCodeConstants = 165,
	kMrPaletteCodeOperators = 166,
	kMrPaletteCodeBrackets = 167,
	kMrPaletteCodeDelimiters = 168,
	kMrPaletteDropListDescription = 169,
	kMrPaletteDropListSelectedInactive = 170,
	kMrPaletteMax = kMrPaletteDropListSelectedInactive
};

struct MRColorSetupSettings {
	static const std::size_t kWindowCount = 11;
	static const std::size_t kMenuDialogCount = 19;
	static const std::size_t kHelpCount = 9;
	static const std::size_t kOtherCount = 11;
	static const std::size_t kMiniMapCount = 5;
	static const std::size_t kCodeCount = 13;

	std::array<unsigned char, kWindowCount> windowColors;
	std::array<unsigned char, kMenuDialogCount> menuDialogColors;
	std::array<unsigned char, kHelpCount> helpColors;
	std::array<unsigned char, kOtherCount> otherColors;
	std::array<unsigned char, kMiniMapCount> miniMapColors;
	std::array<unsigned char, kCodeCount> codeColors;

	MRColorSetupSettings() noexcept : windowColors(), menuDialogColors(), helpColors(), otherColors(), miniMapColors(), codeColors() {
	}

	auto operator==(const MRColorSetupSettings &) const noexcept -> bool = default;
};

enum : unsigned char {
	kFileDialogHistoryId = 100,
	kPathDialogHistoryId = 231
};

enum class MRDialogHistoryScope : unsigned char {
	General = 0,
	EditorSaveAs,
	OpenFile,
	LoadFile,
	SaveLogAs,
	BlockSave,
	BlockLoad,
	MacroFile,
	KeymapProfileLoad,
	KeymapProfileSave,
	WorkspaceLoad,
	WorkspaceSave,
	SetupSettingsMacro,
	SetupMacroDirectory,
	SetupHelpFile,
	SetupTempDirectory,
	SetupShellExecutable,
	SetupLogFile,
	SetupBackupDirectory,
	SetupThemeLoad,
	SetupThemeSave,
	ExtensionThemeFile,
	ExtensionPostLoadMacro,
	ExtensionPreSaveMacro,
	ExtensionDefaultPath,
	Count
};

void initRememberedLoadDialogPath(MRDialogHistoryScope scope, char *buffer, std::size_t bufferSize, const char *pattern);
void rememberLoadDialogPath(MRDialogHistoryScope scope, const char *path);
void forgetLoadDialogPath(MRDialogHistoryScope scope, const char *path);
[[nodiscard]] std::string configuredLastFileDialogFilePath(MRDialogHistoryScope scope);
[[nodiscard]] std::string configuredLastFileDialogPath(MRDialogHistoryScope scope);
void configuredScopedDialogFileHistoryEntries(MRDialogHistoryScope scope, std::vector<std::string> &outValues);
void configuredScopedDialogPathHistoryEntries(MRDialogHistoryScope scope, std::vector<std::string> &outValues);
void initRememberedLoadDialogPath(char *buffer, std::size_t bufferSize, const char *pattern);
void rememberLoadDialogPath(const char *path);
[[nodiscard]] std::string normalizeConfiguredPathInput(std::string_view input);
[[nodiscard]] MRSetupPaths resolveSetupPathDefaults();
[[nodiscard]] MREditSetupSettings resolveEditSetupDefaults();
[[nodiscard]] MRColorSetupSettings resolveColorSetupDefaults();
[[nodiscard]] MREditSetupSettings configuredEditSetupSettings();
[[nodiscard]] MRColorSetupSettings configuredColorSetupSettings();
bool setConfiguredEditSetupSettings(const MREditSetupSettings &settings, std::string *errorMessage = nullptr);
bool applyConfiguredEditSetupValue(const std::string &key, const std::string &value, std::string *errorMessage = nullptr);
bool applyConfiguredColorSetupValue(const std::string &key, const std::string &value, std::string *errorMessage = nullptr);
bool configuredColorSlotOverride(unsigned char paletteIndex, unsigned char &value);
const char *colorSetupGroupTitle(MRColorSetupGroup group);
const char *colorSetupGroupKey(MRColorSetupGroup group);
const MRColorSetupItem *colorSetupGroupItems(MRColorSetupGroup group, std::size_t &count);
bool setConfiguredColorSetupGroupValues(MRColorSetupGroup group, const unsigned char *values, std::size_t count, std::string *errorMessage = nullptr);
void configuredColorSetupGroupValues(MRColorSetupGroup group, unsigned char *values, std::size_t count);
[[nodiscard]] std::string configuredColorThemeFilePath();
[[nodiscard]] std::string configuredColorThemeDisplayName();
[[nodiscard]] std::string defaultColorThemeFilePath();
bool validateColorThemeFilePath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredColorThemeFilePath(const std::string &path, std::string *errorMessage = nullptr);
bool writeColorThemeFile(const std::string &themeUri, std::string *errorMessage = nullptr);
bool ensureColorThemeFileExists(const std::string &themeUri, std::string *errorMessage = nullptr);
bool loadColorThemeFile(const std::string &themeUri, std::string *errorMessage = nullptr);
bool loadWindowColorThemeGroupValues(const std::string &themeUri, std::array<unsigned char, MRColorSetupSettings::kWindowCount> &outValues, std::string *errorMessage = nullptr);
[[nodiscard]] std::string buildColorThemeMacroSource(const MRColorSetupSettings &colors);

[[nodiscard]] MRSettingsKeyClass classifySettingsKey(std::string_view key);
[[nodiscard]] bool isCanonicalSerializedSettingsKey(std::string_view key);
[[nodiscard]] std::size_t canonicalSerializedSettingsKeyCount();
bool resetConfiguredSettingsModel(const std::string &settingsPath, MRSetupPaths &paths, std::string *errorMessage = nullptr);
bool applyConfiguredSettingsAssignment(const std::string &key, const std::string &value, MRSetupPaths &paths, std::string *errorMessage = nullptr);

const MREditSettingDescriptor *editSettingDescriptors(std::size_t &count);
[[nodiscard]] const MREditSettingDescriptor *findEditSettingDescriptorByKey(std::string_view key);
[[nodiscard]] std::string normalizeEditExtensionSelector(std::string_view value);
bool normalizeEditExtensionSelectors(std::vector<std::string> &selectors, std::string *errorMessage = nullptr);
int clampEditFormatTabSize(int tabSize) noexcept;
int clampEditFormatRightMargin(int rightMargin) noexcept;
int clampEditFormatLeftMargin(int leftMargin, int rightMargin) noexcept;
[[nodiscard]] std::string defaultEditFormatLineForTabSize(int tabSize, int leftMargin, int rightMargin);
bool normalizeEditFormatLine(const std::string &value, int tabSize, int fallbackLeftMargin, int fallbackRightMargin, std::string &outValue, int *outLeftMargin = nullptr, int *outRightMargin = nullptr, std::string *errorMessage = nullptr);
[[nodiscard]] std::string synchronizeEditFormatLineMargins(const std::string &value, int leftMargin, int rightMargin, int tabSize);
bool editFormatLineAtColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column, char symbol, std::string &outValue, int *outLeftMargin = nullptr, int *outRightMargin = nullptr, std::string *errorMessage = nullptr);
bool translateEditFormatLine(const std::string &value, int tabSize, int leftMargin, int rightMargin, int deltaColumns, std::string &outValue, int *outLeftMargin = nullptr, int *outRightMargin = nullptr, std::string *errorMessage = nullptr);
[[nodiscard]] int nextResolvedEditFormatTabStopColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column);
[[nodiscard]] int prevResolvedEditFormatTabStopColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column);
[[nodiscard]] int resolvedEditFormatTabDisplayColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column);
[[nodiscard]] int resolvedEditFormatIndentColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int preferredColumn);
[[nodiscard]] std::string buildEditIndentFill(const MREditSetupSettings &settings, int startColumn, int targetColumn, bool preferTabs);
MREditSetupSettings mergeEditSetupSettings(const MREditSetupSettings &defaults, const MREditSetupOverrides &overrides);
const std::vector<MREditExtensionProfile> &configuredEditExtensionProfiles();
bool setConfiguredEditExtensionProfiles(const std::vector<MREditExtensionProfile> &profiles, std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredDefaultProfileDescription();
bool setConfiguredDefaultProfileDescription(const std::string &value, std::string *errorMessage = nullptr);
const std::vector<MRKeymapProfile> &configuredKeymapProfiles();
bool setConfiguredKeymapProfiles(const std::vector<MRKeymapProfile> &profiles, std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredKeymapFilePath();
bool setConfiguredKeymapFilePath(const std::string &path, std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredActiveKeymapProfile();
bool setConfiguredActiveKeymapProfile(const std::string &value, std::string *errorMessage = nullptr);
bool applyConfiguredEditExtensionProfileDirective(const std::string &operation, const std::string &profileId, const std::string &arg3, const std::string &arg4, std::string *errorMessage = nullptr);
bool effectiveEditSetupSettingsForPath(const std::string &path, MREditSetupSettings &out, std::string *matchedProfileName = nullptr);
bool effectiveEditWindowColorThemePathForPath(const std::string &path, std::string &themeUri, std::string *matchedProfileName = nullptr);
[[nodiscard]] std::string formatEditSetupBoolean(bool value);
std::vector<std::string> configuredDefaultExtensionList();
[[nodiscard]] bool configuredDefaultInsertMode();
[[nodiscard]] bool configuredTabExpandSetting();
[[nodiscard]] bool configuredDisplayTabsSetting();
[[nodiscard]] int configuredTabSizeSetting();
[[nodiscard]] bool configuredBackupFilesSetting();
[[nodiscard]] bool configuredPersistentBlocksSetting();
[[nodiscard]] char configuredPageBreakCharacter();
[[nodiscard]] int configuredMaxPathHistory();
[[nodiscard]] int configuredMaxFileHistory();
void configuredPathHistoryEntries(std::vector<std::string> &outValues);
void configuredFileHistoryEntries(std::vector<std::string> &outValues);
bool setConfiguredWindowManager(bool enabled, std::string *errorMessage = nullptr);
[[nodiscard]] bool configuredWindowManager();
bool setConfiguredMenulineMessages(bool enabled, std::string *errorMessage = nullptr);
[[nodiscard]] bool configuredMenulineMessages();
bool setConfiguredSearchDialogOptions(const MRSearchDialogOptions &options, std::string *errorMessage = nullptr);
[[nodiscard]] MRSearchDialogOptions configuredSearchDialogOptions();
bool setConfiguredSarDialogOptions(const MRSarDialogOptions &options, std::string *errorMessage = nullptr);
[[nodiscard]] MRSarDialogOptions configuredSarDialogOptions();
bool setConfiguredMultiSearchDialogOptions(const MRMultiSearchDialogOptions &options, std::string *errorMessage = nullptr);
[[nodiscard]] MRMultiSearchDialogOptions configuredMultiSearchDialogOptions();
bool setConfiguredMultiSarDialogOptions(const MRMultiSarDialogOptions &options, std::string *errorMessage = nullptr);
[[nodiscard]] MRMultiSarDialogOptions configuredMultiSarDialogOptions();
void configuredMultiFilespecHistoryEntries(std::vector<std::string> &outValues);
void configuredMultiPathHistoryEntries(std::vector<std::string> &outValues);
bool addConfiguredMultiFilespecHistoryEntry(const std::string &value, std::string *errorMessage = nullptr);
bool addConfiguredMultiPathHistoryEntry(const std::string &value, std::string *errorMessage = nullptr);
bool setConfiguredVirtualDesktops(int count, std::string *errorMessage = nullptr);
[[nodiscard]] int configuredVirtualDesktops();
bool setConfiguredCyclicVirtualDesktops(bool enabled, std::string *errorMessage = nullptr);
[[nodiscard]] bool configuredCyclicVirtualDesktops();
bool setConfiguredCursorBehaviour(MRCursorBehaviour behaviour, std::string *errorMessage = nullptr);
[[nodiscard]] MRCursorBehaviour configuredCursorBehaviour();
bool setConfiguredCursorPositionMarker(const std::string &value, std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredCursorPositionMarker();
bool setConfiguredAutoloadWorkspace(bool enabled, std::string *errorMessage = nullptr);
[[nodiscard]] bool configuredAutoloadWorkspace();
bool setConfiguredLogHandling(MRLogHandling handling, std::string *errorMessage = nullptr);
[[nodiscard]] MRLogHandling configuredLogHandling();
void configuredAutoexecMacroEntries(std::vector<std::string> &outValues);
bool setConfiguredAutoexecMacroEntries(const std::vector<std::string> &values, std::string *errorMessage = nullptr);
bool addConfiguredAutoexecMacroEntry(const std::string &value, std::string *errorMessage = nullptr);
void clearConfiguredAutoexecMacroDiagnostics();
void rememberConfiguredAutoexecMacroDiagnostic(const std::string &fileName, const std::string &errorText);
[[nodiscard]] bool configuredAutoexecMacroDiagnosticForFile(const std::string &fileName, std::string &errorText);
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
[[nodiscard]] bool configuredSettingsDirty();
void clearConfiguredSettingsDirty();
bool persistConfiguredSettingsSnapshot(std::string *errorMessage = nullptr, MRSettingsWriteReport *report = nullptr);
bool writeSettingsMacroFile(const MRSetupPaths &paths, std::string *errorMessage = nullptr, MRSettingsWriteReport *report = nullptr);
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
bool validateLogFilePath(const std::string &path, std::string *errorMessage = nullptr);
bool setConfiguredLogFilePath(const std::string &path, std::string *errorMessage = nullptr);
[[nodiscard]] std::string configuredLogFilePath();
[[nodiscard]] std::string defaultSettingsMacroFilePath();
[[nodiscard]] std::string defaultMacroDirectoryPath();

#endif
