#include "MRDialogPaths.hpp"
#include "MRSettingsLoader.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <pwd.h>
#include <regex>
#include <set>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {

std::string &rememberedLoadDirectory() {
	static std::string value;
	return value;
}

std::string &configuredSettingsMacroFile() {
	static std::string value;
	return value;
}

std::string &configuredMacroDirectory() {
	static std::string value;
	return value;
}

std::string &configuredHelpFile() {
	static std::string value;
	return value;
}

std::string &configuredTempDirectory() {
	static std::string value;
	return value;
}

std::string &configuredShellExecutable() {
	static std::string value;
	return value;
}

std::string &configuredColorThemeFile() {
	static std::string value;
	return value;
}

MREditSetupSettings &configuredEditSettings() {
	static MREditSetupSettings value;
	return value;
}

std::vector<MREditExtensionProfile> &configuredEditProfiles() {
	static std::vector<MREditExtensionProfile> value;
	return value;
}

std::string &configuredDefaultProfileDescriptionValue() {
	static std::string value = "Global defaults";
	return value;
}

MRColorSetupSettings &configuredColorSettings() {
	static MRColorSetupSettings value;
	return value;
}

bool &configuredColorSettingsInitialized() {
	static bool initialized = false;
	return initialized;
}

std::string trimAscii(std::string_view value) {
	std::size_t start = 0;
	std::size_t end = value.size();

	while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0)
		++start;
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
		--end;
	return std::string(value.substr(start, end - start));
}

std::string upperAscii(std::string value) {
	for (char & i : value)
		i = static_cast<char>(std::toupper(static_cast<unsigned char>(i)));
	return value;
}

std::string normalizeDialogPath(const char *path) {
	std::string result = path != nullptr ? path : "";
	for (char & i : result)
		if (i == '\\')
			i = '/';
	return result;
}

std::string expandUserPath(std::string_view input) {
	std::string path = trimAscii(input);

	if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != nullptr && *home != '\0')
			return std::string(home) + path.substr(1);
	}
	return path;
}

[[nodiscard]] bool isReadableDirectory(std::string_view path) {
	const std::string pathString(path);
	struct stat st;
	if (path.empty())
		return false;
	if (::stat(pathString.c_str(), &st) != 0)
		return false;
	if (!S_ISDIR(st.st_mode))
		return false;
	return ::access(pathString.c_str(), R_OK | X_OK) == 0;
}

[[nodiscard]] bool isWritableDirectory(std::string_view path) {
	const std::string pathString(path);
	struct stat st;
	if (path.empty())
		return false;
	if (::stat(pathString.c_str(), &st) != 0)
		return false;
	if (!S_ISDIR(st.st_mode))
		return false;
	return ::access(pathString.c_str(), R_OK | W_OK | X_OK) == 0;
}

[[nodiscard]] bool isReadableFile(std::string_view path) {
	const std::string pathString(path);
	struct stat st;
	if (path.empty())
		return false;
	if (::stat(pathString.c_str(), &st) != 0)
		return false;
	if (!S_ISREG(st.st_mode))
		return false;
	return ::access(pathString.c_str(), R_OK) == 0;
}

[[nodiscard]] bool isExecutableFile(std::string_view path) {
	const std::string pathString(path);
	struct stat st;
	if (path.empty())
		return false;
	if (::stat(pathString.c_str(), &st) != 0)
		return false;
	if (!S_ISREG(st.st_mode))
		return false;
	return ::access(pathString.c_str(), X_OK) == 0;
}

std::string directoryPartOf(std::string_view path) {
	if (path.empty())
		return std::string();
	std::size_t pos = path.find_last_of('/');
	if (pos == std::string::npos)
		return std::string();
	if (pos == 0)
		return "/";
	return std::string(path.substr(0, pos));
}

[[nodiscard]] bool hasDirectorySeparator(std::string_view path) {
	return path.find('/') != std::string::npos;
}

void copyToBuffer(char *buffer, std::size_t bufferSize, const std::string &value) {
	if (buffer == nullptr || bufferSize == 0)
		return;
	std::memset(buffer, 0, bufferSize);
	std::strncpy(buffer, value.c_str(), bufferSize - 1);
	buffer[bufferSize - 1] = '\0';
}

std::string currentWorkingDirectory() {
	char cwd[4096];

	if (::getcwd(cwd, sizeof(cwd)) == nullptr)
		return std::string();
	return normalizeDialogPath(cwd);
}

[[nodiscard]] bool isAbsolutePath(std::string_view path) {
	return !path.empty() && path[0] == '/';
}

std::string makeAbsolutePath(const std::string &path) {
	std::string normalized = normalizeDialogPath(path.c_str());
	std::string cwd;

	if (normalized.empty() || isAbsolutePath(normalized))
		return normalized;
	cwd = currentWorkingDirectory();
	if (cwd.empty())
		return normalized;
	if (cwd.back() != '/')
		cwd.push_back('/');
	cwd += normalized;
	return cwd;
}

std::string fallbackRememberedLoadDirectory() {
	std::string macroDir = makeAbsolutePath(configuredMacroDirectory());
	std::string cwd = currentWorkingDirectory();

	if (isReadableDirectory(macroDir))
		return macroDir;
	if (isReadableDirectory(cwd))
		return cwd;
	return std::string();
}

std::string effectiveRememberedLoadDirectory() {
	std::string remembered = makeAbsolutePath(rememberedLoadDirectory());

	if (isReadableDirectory(remembered))
		return remembered;
	return fallbackRememberedLoadDirectory();
}

std::string normalizedDialogDirectoryFromPath(const std::string &path) {
	std::string normalized = normalizeConfiguredPathInput(path);
	std::string dir;

	if (normalized.empty())
		return std::string();
	if (isReadableDirectory(normalized))
		return normalized;
	dir = directoryPartOf(normalized);
	if (dir.empty())
		return std::string();
	dir = makeAbsolutePath(dir);
	return isReadableDirectory(dir) ? dir : std::string();
}

std::string executableDirectory() {
	char path[4096];
	ssize_t len = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
	std::size_t sep = 0;

	if (len <= 0)
		return std::string();
	path[len] = '\0';
	sep = std::string(path).find_last_of('/');
	if (sep == std::string::npos)
		return std::string();
	if (sep == 0)
		return "/";
	return std::string(path).substr(0, sep);
}

std::string pathFromEnvironment(const char *name) {
	const char *value = std::getenv(name);

	if (value == nullptr || *value == '\0')
		return std::string();
	return makeAbsolutePath(normalizeDialogPath(expandUserPath(value).c_str()));
}

std::string firstWritableDirectoryFromEnvironment() {
	static constexpr std::array<const char *, 3> names = {"TMPDIR", "TEMP", "TMP"};
	for (const char *name : names) {
		std::string value = pathFromEnvironment(name);
		if (isWritableDirectory(value))
			return value;
	}
	return std::string();
}

std::string shellFromUserDatabase() {
	struct passwd *entry = ::getpwuid(::getuid());

	if (entry == nullptr || entry->pw_shell == nullptr || *entry->pw_shell == '\0')
		return std::string();
	return normalizeDialogPath(entry->pw_shell);
}

std::string builtInTempDirectoryPath();

std::string appendFileName(std::string_view directory, const char *fileName) {
	std::string base(directory);

	if (fileName == nullptr || *fileName == '\0')
		return base;
	if (base.empty())
		return std::string(fileName);
	if (base.back() != '/')
		base.push_back('/');
	base += fileName;
	return base;
}

std::string appendPathSegment(std::string_view base, const char *segment) {
	std::string out(base);

	if (segment == nullptr || *segment == '\0')
		return out;
	if (out.empty())
		return std::string(segment);
	if (out.back() != '/')
		out.push_back('/');
	out += segment;
	return out;
}

std::string fileNamePartOf(std::string_view path) {
	std::size_t pos;
	if (path.empty())
		return std::string();
	pos = path.find_last_of('/');
	if (pos == std::string::npos)
		return std::string(path);
	return std::string(path.substr(pos + 1));
}

std::string defaultColorThemePathForSettings(std::string_view settingsPath) {
	std::string dir = directoryPartOf(makeAbsolutePath(std::string(settingsPath)));
	if (dir.empty())
		dir = currentWorkingDirectory();
	if (dir.empty())
		dir = "/tmp";
	return appendFileName(dir, "default-theme.mrmac");
}

std::string builtInTempDirectoryPath() {
	std::string envValue = firstWritableDirectoryFromEnvironment();
	std::string cwd;

	if (!envValue.empty())
		return envValue;
	if (isWritableDirectory("/tmp"))
		return "/tmp";
	cwd = currentWorkingDirectory();
	if (!cwd.empty() && isWritableDirectory(cwd))
		return cwd;
	return "/tmp";
}

std::string builtInShellExecutablePath() {
	std::string shell = pathFromEnvironment("SHELL");

	if (isExecutableFile(shell))
		return shell;
	shell = shellFromUserDatabase();
	if (isExecutableFile(shell))
		return shell;
	shell = pathFromEnvironment("COMSPEC");
	if (isExecutableFile(shell))
		return shell;
	if (isExecutableFile("/bin/sh"))
		return "/bin/sh";
	return "/bin/sh";
}

bool ensureDirectoryTree(const std::string &directoryPath, std::string *errorMessage) {
	struct stat st;
	std::string parentPath;

	if (directoryPath.empty() || directoryPath == "." || directoryPath == "/") {
		if (errorMessage != nullptr)
			errorMessage->clear();
		return true;
	}
	if (::stat(directoryPath.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			if (errorMessage != nullptr)
				errorMessage->clear();
			return true;
		}
		if (errorMessage != nullptr)
			*errorMessage = "Path exists and is not a path container: " + directoryPath;
		return false;
	}
	parentPath = directoryPartOf(directoryPath);
	if (!parentPath.empty() && parentPath != directoryPath)
		if (!ensureDirectoryTree(parentPath, errorMessage))
			return false;
	if (::mkdir(directoryPath.c_str(), 0755) != 0 && errno != EEXIST) {
		if (errorMessage != nullptr)
			*errorMessage = "Unable to create path container: " + directoryPath;
		return false;
	}
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool writeTextFile(const std::string &path, const std::string &content) {
	std::ofstream out(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out << content;
	return out.good();
}

bool readTextFile(const std::string &path, std::string &content) {
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	if (!in)
		return false;
	content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return in.good() || in.eof();
}

void accumulateSettingsChangeCounts(const std::vector<MRSettingsChangeEntry> &changes, std::size_t &addedCount,
                                    std::size_t &removedCount, std::size_t &changedCount) {
	addedCount = 0;
	removedCount = 0;
	changedCount = 0;
	for (const MRSettingsChangeEntry &change : changes)
		if (change.kind == MRSettingsChangeEntry::Kind::Added)
			++addedCount;
		else if (change.kind == MRSettingsChangeEntry::Kind::Removed)
			++removedCount;
		else
			++changedCount;
}

void populateSettingsWriteReport(const std::string &settingsPath, const std::string &beforeSource,
                                 const std::string &afterSource, MRSettingsWriteReport *report) {
	std::vector<MRSettingsChangeEntry> changes;

	if (report == nullptr)
		return;
	*report = MRSettingsWriteReport();
	report->settingsPath = settingsPath;
	report->fileWritten = true;
	report->contentChanged = beforeSource != afterSource;
	if (!diffSettingsSources(beforeSource, afterSource, changes, nullptr))
		return;
	accumulateSettingsChangeCounts(changes, report->addedCount, report->removedCount, report->changedCount);
	if (report->contentChanged && !changes.empty()) {
		report->logLines.push_back("settings.mrmac updated: " + std::to_string(report->changedCount) +
		                      " changed, " + std::to_string(report->addedCount) +
		                      " added, " + std::to_string(report->removedCount) + " removed.");
		for (const MRSettingsChangeEntry &change : changes)
			report->logLines.push_back(formatSettingsChangeForLog(change));
	} else if (report->contentChanged)
		report->logLines.push_back("settings.mrmac rewritten without semantic change.");
}

std::string escapeMrmacSingleQuotedLiteral(const std::string &value) {
	std::string out;
	out.reserve(value.size() + 8);
	for (char ch : value) {
			if (ch == '\'')
			out += "''";
		else
			out.push_back(ch);
	}
	return out;
}

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr)
		*errorMessage = message;
	return false;
}

static const char *const kDefaultPageBreakLiteral = "\\f";
static const char *const kDefaultWordDelimiters = ".()'\\\",#$012%^&*+-/[]?";
static const char *const kDefaultDefaultExtensions = "PAS;ASM;BAT;TXT;DO";
static const char *const kColumnBlockMoveDelete = "DELETE_SPACE";
static const char *const kColumnBlockMoveLeave = "LEAVE_SPACE";
static const char *const kDefaultModeInsert = "INSERT";
static const char *const kDefaultModeOverwrite = "OVERWRITE";
static const char *const kIndentStyleOff = "OFF";
static const char *const kIndentStyleAutomatic = "AUTOMATIC";
static const char *const kIndentStyleSmart = "SMART";
static const char *const kFileTypeLegacyText = "LEGACY_TEXT";
static const char *const kFileTypeUnix = "UNIX";
static const char *const kFileTypeBinary = "BINARY";
static const int kDefaultTabSize = 8;
static const int kMinTabSize = 2;
static const int kMaxTabSize = 32;
static const char *const kDefaultCursorStatusColor = "";
static const char *const kThemeSettingsKey = "COLORTHEMEURI";
static const char *const kWindowColorThemeProfileKey = "WINDOW_COLORTHEME_URI";
static const char *const kSettingsVersionKey = "SETTINGS_VERSION";
static const char *const kCurrentSettingsVersion = "2";

struct MRSettingsKeyDescriptor {
	const char *key;
	MRSettingsKeyClass keyClass;
	bool serialized;
};

static const MRSettingsKeyDescriptor kFixedSettingsKeyDescriptors[] = {
    {kSettingsVersionKey, MRSettingsKeyClass::Version, true},
    {"SETTINGSPATH", MRSettingsKeyClass::Path, true},
    {"MACROPATH", MRSettingsKeyClass::Path, true},
    {"HELPPATH", MRSettingsKeyClass::Path, true},
    {"TEMPDIR", MRSettingsKeyClass::Path, true},
    {"SHELLPATH", MRSettingsKeyClass::Path, true},
    {"LASTFILEDIALOGPATH", MRSettingsKeyClass::Global, true},
    {"DEFAULT_PROFILE_DESCRIPTION", MRSettingsKeyClass::Global, true},
    {kThemeSettingsKey, MRSettingsKeyClass::Global, true},
    {"WINDOWCOLORS", MRSettingsKeyClass::ColorInline, false},
    {"MENUDIALOGCOLORS", MRSettingsKeyClass::ColorInline, false},
    {"HELPCOLORS", MRSettingsKeyClass::ColorInline, false},
    {"OTHERCOLORS", MRSettingsKeyClass::ColorInline, false},
};

const MREditSettingDescriptor *editSettingDescriptorByKeyInternal(const std::string &key);

static const MREditSettingDescriptor kEditSettingDescriptors[] = {
    {"PAGE_BREAK", "Page break", MREditSettingSection::Text, MREditSettingKind::String, true, kOvPageBreak},
    {"WORD_DELIMITERS", "Word delimiters", MREditSettingSection::Text, MREditSettingKind::String, true,
     kOvWordDelimiters},
    {"DEFAULT_EXTENSIONS", "Filename extension fallback", MREditSettingSection::OpenFile, MREditSettingKind::String,
     true, kOvDefaultExtensions},
    {"TRUNCATE_SPACES", "Truncate spaces", MREditSettingSection::Save, MREditSettingKind::Boolean, true,
     kOvTruncateSpaces},
    {"EOF_CTRL_Z", "Write Ctrl+Z EOF", MREditSettingSection::Save, MREditSettingKind::Boolean, true,
     kOvEofCtrlZ},
    {"EOF_CR_LF", "Write CR/LF", MREditSettingSection::Save, MREditSettingKind::Boolean, true, kOvEofCrLf},
    {"TAB_EXPAND", "Expand tabs", MREditSettingSection::Tabs, MREditSettingKind::Boolean, true,
     kOvTabExpand},
    {"TAB_SIZE", "Tab size", MREditSettingSection::Tabs, MREditSettingKind::Integer, true, kOvTabSize},
    {"RIGHT_MARGIN", "Right margin", MREditSettingSection::Formatting, MREditSettingKind::Integer, true,
     kOvRightMargin},
    {"WORD_WRAP", "Word wrap", MREditSettingSection::Formatting, MREditSettingKind::Boolean, true,
     kOvWordWrap},
    {"INDENT_STYLE", "Indent style", MREditSettingSection::Formatting, MREditSettingKind::Choice, true,
     kOvIndentStyle},
    {"FILE_TYPE", "File type", MREditSettingSection::Formatting, MREditSettingKind::Choice, true,
     kOvFileType},
    {"BINARY_RECORD_LENGTH", "Binary record length", MREditSettingSection::Formatting, MREditSettingKind::Integer, true,
     kOvBinaryRecordLength},
    {"POST_LOAD_MACRO", "Post-load macro", MREditSettingSection::Macros, MREditSettingKind::String, true,
     kOvPostLoadMacro},
    {"PRE_SAVE_MACRO", "Pre-save macro", MREditSettingSection::Macros, MREditSettingKind::String, true,
     kOvPreSaveMacro},
    {"DEFAULT_PATH", "Default path", MREditSettingSection::Paths, MREditSettingKind::String, true,
     kOvDefaultPath},
    {"FORMAT_LINE", "Format line", MREditSettingSection::Formatting, MREditSettingKind::String, true,
     kOvFormatLine},
    {"BACKUP_FILES", "Backup files", MREditSettingSection::Save, MREditSettingKind::Boolean, true,
     kOvBackupFiles},
    {"BACKUP_METHOD", "Backup method", MREditSettingSection::Save, MREditSettingKind::Choice, false,
     kOvBackupMethod},
    {"BACKUP_FREQUENCY", "Backup frequency", MREditSettingSection::Save, MREditSettingKind::Choice, false,
     kOvBackupFrequency},
    {"BACKUP_EXTENSION", "Backup extension", MREditSettingSection::Save, MREditSettingKind::String, false,
     kOvBackupExtension},
    {"BACKUP_DIRECTORY", "Backup directory", MREditSettingSection::Save, MREditSettingKind::String, false,
     kOvBackupDirectory},
    {"AUTOSAVE_INACTIVITY_SECONDS", "Autosave inactivity", MREditSettingSection::Save, MREditSettingKind::Integer, false,
     kOvAutosaveInactivitySeconds},
    {"AUTOSAVE_INTERVAL_SECONDS", "Autosave interval", MREditSettingSection::Save, MREditSettingKind::Integer, false,
     kOvAutosaveIntervalSeconds},
    {"SHOW_EOF_MARKER", "Show EOF marker", MREditSettingSection::Display, MREditSettingKind::Boolean, true,
     kOvShowEofMarker},
    {"SHOW_EOF_MARKER_EMOJI", "EOF marker emoji", MREditSettingSection::Display, MREditSettingKind::Boolean, true,
     kOvShowEofMarkerEmoji},
    {"SHOW_LINE_NUMBERS", "Show line numbers", MREditSettingSection::Display, MREditSettingKind::Boolean, true,
     kOvShowLineNumbers},
    {"LINE_NUM_ZERO_FILL", "Zero-fill line numbers", MREditSettingSection::Display, MREditSettingKind::Boolean,
     true, kOvLineNumZeroFill},
    {"PERSISTENT_BLOCKS", "Persistent blocks", MREditSettingSection::Blocks, MREditSettingKind::Boolean, true,
     kOvPersistentBlocks},
    {"CODE_FOLDING", "Code folding", MREditSettingSection::Display, MREditSettingKind::Boolean, true,
     kOvCodeFolding},
    {"COLUMN_BLOCK_MOVE", "Column block move", MREditSettingSection::Blocks, MREditSettingKind::Choice, true,
     kOvColumnBlockMove},
    {"DEFAULT_MODE", "Default mode", MREditSettingSection::Mode, MREditSettingKind::Choice, true,
     kOvDefaultMode},
    {"CURSOR_STATUS_COLOR", "Cursor status color", MREditSettingSection::Display, MREditSettingKind::String, true,
     kOvCursorStatusColor},
};

static const unsigned char kPaletteDialogFrame = 33;
static const unsigned char kPaletteDialogText = 37;
static const unsigned char kPaletteDialogBackground = 32;
static const unsigned char kPaletteDialogInactiveClusterGray = 62;
static const unsigned char kPaletteDialogInactiveClusterBlue = 94;
static const unsigned char kPaletteDialogInactiveClusterCyan = 126;

enum : std::size_t {
	kMenuDialogIndexListboxSelector = 11,
	kMenuDialogIndexInactiveCluster = 12,
	kMenuDialogIndexDialogFrame = 13,
	kMenuDialogIndexDialogText = 14,
	kMenuDialogIndexDialogBackground = 15
};

struct ColorGroupDefinition {
	MRColorSetupGroup group;
	const char *title;
	const char *key;
	const MRColorSetupItem *items;
	std::size_t count;
};

static const MRColorSetupItem kWindowColorItems[] = {
    // TProgram palette layout:
    //   8..15  = TWindow(Blue), 16..23 = TWindow(Cyan), 24..31 = TWindow(Gray).
    // Our editor windows use wpBlueWindow, so frame/text colors map into 8..15.
    // Current-line, current-line-in-block and changed-text intentionally use
    // dedicated app palette extension slots (136..138) to avoid recoloring
    // frame icons/scrollbar controls.
	{"text", 13},
	{"changed text", kMrPaletteChangedText},
	{"highlighted text", 14},
	{"EOF marker", kMrPaletteEofMarker},
	{"window border", 8},
	{"window bold", 9},
	{"current line", kMrPaletteCurrentLine},
	{"current line in block", kMrPaletteCurrentLineInBlock},
	{"line numbers", kMrPaletteLineNumbers},
};

static const MRColorSetupItem kMenuDialogColorItems[] = {
    // Mixed group by design:
    // - Menu/status palette slots 2..6.
    // - Gray dialog block (32..63), as TDialog defaults to dpGrayDialog.
    {"description of selectable menu element", 2},
    {"description of ghosted menu element", 3},
    {"hotkey of menu element", 4},
    {"menu selector on selectable menu element", 5},
    {"menu selector on ghosted menu element", 6},
    {"description of buttons", 41},
    {"hotkey on buttons", 45},
    {"button shadow", 46},
    {"selected element in unfocussed listbox", 59},
    {"element description in listbox", 57},
    {"hotkeys on radio buttons & check boxes", 49},
    {"dialog selector", 58},
    {"inactive radio buttons and checkboxes", kPaletteDialogInactiveClusterGray},
    {"inactive dialog elements", kMrPaletteDialogInactiveElements},
    {"dialog frame", kPaletteDialogFrame},
    {"dialog text", kPaletteDialogText},
    {"dialog background", kPaletteDialogBackground},
};

static const MRColorSetupItem kHelpColorItems[] = {
    {"Help-Text", 133},     {"help-Highlight", 134}, {"help-Chapter", 135},
    {"help-Border", 128},   {"help-Link", 134},      {"help-F-keys", 135},
    {"help-attr-1", 133},   {"help-attr-2", 134},    {"help-attr-3", 135},
};

static const MRColorSetupItem kOtherColorItems[] = {
    {"statusline", 2},
    {"statusline bold", 3},
    {"function descriptions on statusline", 4},
    {"function keys on statusline", 5},
    {"error message", kMrPaletteMessageError},
    {"message", kMrPaletteMessage},
    {"warning message", kMrPaletteMessageWarning},
    {"hero events", kMrPaletteMessageHero},
    {"cursor position marker", kMrPaletteCursorPositionMarker},
};

static const ColorGroupDefinition kColorGroups[] = {
    {MRColorSetupGroup::Window, "WINDOW COLORS", "WINDOWCOLORS", kWindowColorItems,
     std::size(kWindowColorItems)},
    {MRColorSetupGroup::MenuDialog, "MENU / DIALOG COLORS", "MENUDIALOGCOLORS", kMenuDialogColorItems,
     std::size(kMenuDialogColorItems)},
    {MRColorSetupGroup::Help, "HELP COLORS", "HELPCOLORS", kHelpColorItems,
     std::size(kHelpColorItems)},
    {MRColorSetupGroup::Other, "OTHER COLORS", "OTHERCOLORS", kOtherColorItems,
     std::size(kOtherColorItems)},
};

const ColorGroupDefinition *findColorGroupDefinition(MRColorSetupGroup group) {
	for (const auto & kColorGroup : kColorGroups)
		if (kColorGroup.group == group)
			return &kColorGroup;
	return nullptr;
}

const ColorGroupDefinition *findColorGroupDefinitionByKey(const std::string &key) {
	std::string upper = upperAscii(trimAscii(key));
	for (const auto & kColorGroup : kColorGroups)
		if (upper == kColorGroup.key)
			return &kColorGroup;
	return nullptr;
}

unsigned char defaultColorForSlot(unsigned char paletteIndex) {
	// Defaults from cpAppColor (TVision app.h), expanded explicitly for stability.
	static constexpr std::array<unsigned char, 146> defaults = {
	    0x00, 0x71, 0x70, 0x78, 0x74, 0x20, 0x28, 0x24, 0x17, 0x1F, 0x1A, 0x31, 0x31, 0x1E, 0x71, 0x1F,
	    0x37, 0x3F, 0x3A, 0x13, 0x13, 0x3E, 0x21, 0x3F, 0x70, 0x7F, 0x7A, 0x13, 0x13, 0x70, 0x7F, 0x7E,
	    0x70, 0x7F, 0x7A, 0x13, 0x13, 0x70, 0x70, 0x7F, 0x7E, 0x20, 0x2B, 0x2F, 0x78, 0x2E, 0x70, 0x30,
	    0x3F, 0x3E, 0x1F, 0x2F, 0x1A, 0x20, 0x72, 0x31, 0x31, 0x30, 0x2F, 0x3E, 0x31, 0x13, 0x38, 0x00,
	    0x17, 0x1F, 0x1A, 0x71, 0x71, 0x1E, 0x17, 0x1F, 0x1E, 0x20, 0x2B, 0x2F, 0x78, 0x2E, 0x10, 0x30,
	    0x3F, 0x3E, 0x70, 0x2F, 0x7A, 0x20, 0x12, 0x31, 0x31, 0x30, 0x2F, 0x3E, 0x31, 0x13, 0x38, 0x00,
	    0x37, 0x3F, 0x3A, 0x13, 0x13, 0x3E, 0x30, 0x3F, 0x3E, 0x20, 0x2B, 0x2F, 0x78, 0x2E, 0x30, 0x70,
	    0x7F, 0x7E, 0x1F, 0x2F, 0x1A, 0x20, 0x32, 0x31, 0x71, 0x70, 0x2F, 0x7E, 0x71, 0x13, 0x78, 0x00,
	    0x37, 0x3F, 0x3A, 0x13, 0x13, 0x30, 0x3E, 0x1E,
	};

	if (paletteIndex == kMrPaletteCurrentLine)
		return defaults[10];
	if (paletteIndex == kMrPaletteCurrentLineInBlock)
		return defaults[12];
	if (paletteIndex == kMrPaletteChangedText)
		return defaults[14];
	if (paletteIndex == kMrPaletteMessageError)
		return defaults[42];
	if (paletteIndex == kMrPaletteMessage)
		return defaults[43];
	if (paletteIndex == kMrPaletteMessageWarning)
		return defaults[44];
	if (paletteIndex == kMrPaletteMessageHero)
		return defaults[43];
	if (paletteIndex == kMrPaletteCursorPositionMarker)
		return defaults[3];
	if (paletteIndex == kMrPaletteLineNumbers)
		return defaults[9];
	if (paletteIndex == kMrPaletteEofMarker)
		return defaults[14];
	if (paletteIndex == kMrPaletteDialogInactiveElements)
		return defaults[kPaletteDialogInactiveClusterGray];
	if (paletteIndex == 0 || paletteIndex >= std::size(defaults))
		return 0x70;
	return defaults[paletteIndex];
}

MRColorSetupSettings defaultsFromColorGroups() {
	MRColorSetupSettings settings;

	for (std::size_t i = 0; i < settings.windowColors.size(); ++i)
		settings.windowColors[i] = defaultColorForSlot(kWindowColorItems[i].paletteIndex);
	for (std::size_t i = 0; i < settings.menuDialogColors.size(); ++i)
		settings.menuDialogColors[i] = defaultColorForSlot(kMenuDialogColorItems[i].paletteIndex);
	for (std::size_t i = 0; i < settings.helpColors.size(); ++i)
		settings.helpColors[i] = defaultColorForSlot(kHelpColorItems[i].paletteIndex);
	for (std::size_t i = 0; i < settings.otherColors.size(); ++i)
		settings.otherColors[i] = defaultColorForSlot(kOtherColorItems[i].paletteIndex);
	return settings;
}

bool parseHexColorToken(const std::string &token, unsigned char &outValue) {
	std::string value = trimAscii(token);
	unsigned int parsed = 0;

	if (value.empty() || value.size() > 2)
		return false;
	for (char i : value)
		if (!std::isxdigit(static_cast<unsigned char>(i)))
			return false;
	parsed = static_cast<unsigned int>(std::strtoul(value.c_str(), nullptr, 16));
	if (parsed > 0xFF)
		return false;
	outValue = static_cast<unsigned char>(parsed);
	return true;
}

template <std::size_t N>
std::string formatColorListLiteral(const std::array<unsigned char, N> &values) {
	static const char *const hex = "0123456789ABCDEF";
	std::string out = "v1:";

	for (std::size_t i = 0; i < N; ++i) {
		unsigned char value = values[i];
		if (i != 0)
			out.push_back(',');
		out.push_back(hex[(value >> 4) & 0x0F]);
		out.push_back(hex[value & 0x0F]);
	}
	return out;
}

std::string formatWindowColorListLiteral(
    const std::array<unsigned char, MRColorSetupSettings::kWindowCount> &values) {
	std::string out = formatColorListLiteral(values);

	// WINDOWCOLORS uses v3 (adds EOF marker color to v2's 8-value layout).
	if (out.size() >= 2 && out[0] == 'v')
		out[1] = '3';
	return out;
}

template <std::size_t N>
bool parseColorListLiteral(const std::string &literal, std::array<unsigned char, N> &outValues,
                           std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::size_t itemIndex = 0;
	std::array<unsigned char, N> parsed;

	if (text.rfind("v1:", 0) == 0 || text.rfind("V1:", 0) == 0)
		text = text.substr(3);
	if (text.empty())
		return setError(errorMessage, "Empty color list.");

	while (cursor <= text.size() && itemIndex < N) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
		unsigned char value = 0;

		if (!parseHexColorToken(token, value))
			return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed[itemIndex++] = value;
		if (comma == std::string::npos)
			break;
		cursor = comma + 1;
	}

	if (itemIndex != N)
		return setError(errorMessage, "Unexpected color list size.");
	if (text.find(',', cursor) != std::string::npos)
		return setError(errorMessage, "Too many color values in list.");

	outValues = parsed;
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool parseWindowColorListLiteral(const std::string &literal,
                                 std::array<unsigned char, MRColorSetupSettings::kWindowCount> &outValues,
                                 std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::vector<unsigned char> parsed;
	const unsigned char defaultEofMarker = defaultColorForSlot(kMrPaletteEofMarker);
	const unsigned char defaultLineNumbers = defaultColorForSlot(kMrPaletteLineNumbers);
	unsigned char value = 0;
	bool v3Format = false;
	bool v2Format = false;

	if (text.rfind("v3:", 0) == 0 || text.rfind("V3:", 0) == 0) {
		text = text.substr(3);
		v3Format = true;
	} else if (text.rfind("v2:", 0) == 0 || text.rfind("V2:", 0) == 0) {
		text = text.substr(3);
		v2Format = true;
	} else if (text.rfind("v1:", 0) == 0 || text.rfind("V1:", 0) == 0)
		text = text.substr(3);
	if (text.empty())
		return setError(errorMessage, "Empty color list.");

	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
		if (!parseHexColorToken(token, value))
			return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos)
			break;
		cursor = comma + 1;
	}

	// Formats:
	// - v3 + 9 values: current format (..., EOF marker, ..., line numbers)
	// - v2 + 8 values: previous format (without EOF marker)
	// - v1 + 7 values: legacy format (without EOF marker and line numbers)
	// - v1 + 8 values: legacy format with EOF marker as 4th entry, no line numbers
	if (v3Format) {
		if (parsed.size() != MRColorSetupSettings::kWindowCount)
			return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v3.");
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = parsed[i];
	} else if (v2Format) {
		if (parsed.size() != MRColorSetupSettings::kWindowCount - 1)
			return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v2.");
		outValues[0] = parsed[0];
		outValues[1] = parsed[1];
		outValues[2] = parsed[2];
		outValues[3] = defaultEofMarker;
		outValues[4] = parsed[3];
		outValues[5] = parsed[4];
		outValues[6] = parsed[5];
		outValues[7] = parsed[6];
		outValues[8] = parsed[7];
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount) {
		// Accept unversioned current layout as a tolerant input.
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 1) {
		// Legacy v1 with EOF marker and without line-number color.
		outValues[0] = parsed[0];
		outValues[1] = parsed[1];
		outValues[2] = parsed[2];
		outValues[3] = parsed[3];
		outValues[4] = parsed[4];
		outValues[5] = parsed[5];
		outValues[6] = parsed[6];
		outValues[7] = parsed[7];
		outValues[8] = defaultLineNumbers;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 2) {
		// Legacy v1 without EOF marker and without line-number color.
		outValues[0] = parsed[0];
		outValues[1] = parsed[1];
		outValues[2] = parsed[2];
		outValues[3] = defaultEofMarker;
		outValues[4] = parsed[3];
		outValues[5] = parsed[4];
		outValues[6] = parsed[5];
		outValues[7] = parsed[6];
		outValues[8] = defaultLineNumbers;
	} else
		return setError(errorMessage, "Unexpected WINDOWCOLORS list size.");

	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool parseMenuDialogColorListLiteral(
    const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kMenuDialogCount> &outValues,
    std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::vector<unsigned char> parsed;
	unsigned char value = 0;

	if (text.rfind("v1:", 0) == 0 || text.rfind("V1:", 0) == 0)
		text = text.substr(3);
	if (text.empty())
		return setError(errorMessage, "Empty color list.");

	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
		if (!parseHexColorToken(token, value))
			return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos)
			break;
		cursor = comma + 1;
	}

	for (std::size_t i = 0; i < outValues.size(); ++i)
		outValues[i] = defaultColorForSlot(kMenuDialogColorItems[i].paletteIndex);

	// Accepted formats:
	// - 16: current format (..., dialog selector, inactive radio/checkbox, dialog frame/text/background)
	// - 15: previous format without inactive radio/checkbox color
	// - 14: legacy format (..., dialog frame, dialog background[old=dialog text])
	//       -> dialog background inherits legacy dialog frame color.
	// - 13: legacy format missing old dialog background(text)
	//       -> dialog text defaults, dialog background inherits legacy frame color.
	// - 12: legacy format missing legacy frame + legacy background(text)
	// - 11: legacy format missing dialog selector + legacy frame + legacy background(text)
	if (parsed.size() == MRColorSetupSettings::kMenuDialogCount) {
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == MRColorSetupSettings::kMenuDialogCount - 1) {
		for (std::size_t i = 0; i <= kMenuDialogIndexListboxSelector; ++i)
			outValues[i] = parsed[i];
		outValues[kMenuDialogIndexDialogFrame] = parsed[12];
		outValues[kMenuDialogIndexDialogText] = parsed[13];
		outValues[kMenuDialogIndexDialogBackground] = parsed[14];
	} else if (parsed.size() == MRColorSetupSettings::kMenuDialogCount - 2) {
		for (std::size_t i = 0; i <= kMenuDialogIndexListboxSelector; ++i)
			outValues[i] = parsed[i];
		outValues[kMenuDialogIndexDialogFrame] = parsed[12];
		outValues[kMenuDialogIndexDialogText] = parsed[13];
		outValues[kMenuDialogIndexDialogBackground] = parsed[12];
	} else if (parsed.size() == MRColorSetupSettings::kMenuDialogCount - 3) {
		for (std::size_t i = 0; i <= kMenuDialogIndexListboxSelector; ++i)
			outValues[i] = parsed[i];
		outValues[kMenuDialogIndexDialogFrame] = parsed[12];
		outValues[kMenuDialogIndexDialogBackground] = parsed[12];
	} else if (parsed.size() == MRColorSetupSettings::kMenuDialogCount - 4) {
		for (std::size_t i = 0; i <= kMenuDialogIndexListboxSelector; ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == MRColorSetupSettings::kMenuDialogCount - 5) {
		for (std::size_t i = 0; i < 11; ++i)
			outValues[i] = parsed[i];
	} else {
		return setError(errorMessage, "Unexpected MENUDIALOGCOLORS list size.");
	}

	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool parseOtherColorListLiteral(const std::string &literal,
                                std::array<unsigned char, MRColorSetupSettings::kOtherCount> &outValues,
                                std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::vector<unsigned char> parsed;
	unsigned char value = 0;

	if (text.rfind("v1:", 0) == 0 || text.rfind("V1:", 0) == 0)
		text = text.substr(3);
	if (text.empty())
		return setError(errorMessage, "Empty color list.");

	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
		if (!parseHexColorToken(token, value))
			return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos)
			break;
		cursor = comma + 1;
	}

	// Accept current 9-value format, prior 8/7-value formats and legacy 10-value format.
	// Legacy format ended with shadow / shadow-character / background color entries,
	// which are no longer configurable in OTHERCOLORS.
	if (parsed.size() == MRColorSetupSettings::kOtherCount)
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = parsed[i];
	else if (parsed.size() == MRColorSetupSettings::kOtherCount - 1) {
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[MRColorSetupSettings::kOtherCount - 1] =
		    defaultColorForSlot(kMrPaletteCursorPositionMarker);
	}
	else if (parsed.size() == MRColorSetupSettings::kOtherCount - 2) {
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[MRColorSetupSettings::kOtherCount - 2] = defaultColorForSlot(kMrPaletteMessageHero);
		outValues[MRColorSetupSettings::kOtherCount - 1] =
		    defaultColorForSlot(kMrPaletteCursorPositionMarker);
	}
	else if (parsed.size() == 10) {
		for (std::size_t i = 0; i < 7; ++i)
			outValues[i] = parsed[i];
		outValues[7] = defaultColorForSlot(kMrPaletteMessageHero);
		outValues[8] = defaultColorForSlot(kMrPaletteCursorPositionMarker);
	}
	else
		return setError(errorMessage, "Unexpected OTHERCOLORS list size.");

	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string unescapeMrmacSingleQuotedLiteral(const std::string &value) {
	std::string out;
	out.reserve(value.size());
	for (std::size_t i = 0; i < value.size(); ++i) {
		char ch = value[i];
		if (ch == '\'' && i + 1 < value.size() && value[i + 1] == '\'') {
			out.push_back('\'');
			++i;
			continue;
		}
		out.push_back(ch);
	}
	return out;
}

bool parseThemeSetupAssignments(const std::string &source, std::map<std::string, std::string> &assignments,
                                std::string *errorMessage) {
	static const std::regex pattern(
	    "MRSETUP\\s*\\(\\s*'([^']+)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)",
	    std::regex_constants::ECMAScript | std::regex_constants::icase);
	std::set<std::string> allowed = {"WINDOWCOLORS", "MENUDIALOGCOLORS", "HELPCOLORS", "OTHERCOLORS"};
	std::sregex_iterator it(source.begin(), source.end(), pattern);
	std::sregex_iterator end;

	assignments.clear();
	for (; it != end; ++it) {
		std::string key = upperAscii(trimAscii((*it)[1].str()));
		std::string value = unescapeMrmacSingleQuotedLiteral((*it)[2].str());

		if (allowed.find(key) == allowed.end())
			return setError(errorMessage, "Theme file contains unsupported MRSETUP key: " + key);
		assignments[key] = value;
	}
	for (const std::string &required : allowed)
		if (assignments.find(required) == assignments.end())
			return setError(errorMessage, "Theme file must define MRSETUP('" + required + "', ...).");
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

void ensureConfiguredColorSettingsInitialized() {
	if (configuredColorSettingsInitialized())
		return;
	configuredColorSettings() = defaultsFromColorGroups();
	configuredColorSettingsInitialized() = true;
}

bool parseBooleanLiteral(const std::string &value, bool &outValue, std::string *errorMessage) {
	std::string upper = upperAscii(trimAscii(value));

	if (upper == "TRUE" || upper == "1" || upper == "YES" || upper == "ON") {
		outValue = true;
		if (errorMessage != nullptr)
			errorMessage->clear();
		return true;
	}
	if (upper == "FALSE" || upper == "0" || upper == "NO" || upper == "OFF") {
		outValue = false;
		if (errorMessage != nullptr)
			errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "Expected boolean literal true/false.");
}

std::string canonicalBooleanLiteral(bool value) {
	return value ? "true" : "false";
}

std::string normalizeColumnBlockMove(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));
	std::string compact;

	for (char ch : key) {
			if (ch == '-' || ch == ' ')
			ch = '_';
		compact.push_back(ch);
	}

	if (compact == "DELETE_SPACE" || compact == "DELETE")
		return kColumnBlockMoveDelete;
	if (compact == "LEAVE_SPACE" || compact == "LEAVE")
		return kColumnBlockMoveLeave;
	return std::string();
}

std::string normalizeDefaultMode(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "INSERT")
		return kDefaultModeInsert;
	if (key == "OVERWRITE" || key == "OVR")
		return kDefaultModeOverwrite;
	return std::string();
}

std::string normalizeIndentStyle(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "OFF")
		return kIndentStyleOff;
	if (key == "AUTOMATIC" || key == "AUTO")
		return kIndentStyleAutomatic;
	if (key == "SMART")
		return kIndentStyleSmart;
	return std::string();
}

std::string normalizeFileType(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "LEGACY_TEXT" || key == "LEGACY" || key == "CRLF" || key == "TEXT")
		return kFileTypeLegacyText;
	if (key == "UNIX" || key == "LF")
		return kFileTypeUnix;
	if (key == "BINARY" || key == "BIN")
		return kFileTypeBinary;
	return std::string();
}

constexpr const char *kBackupMethodOff = "OFF";
constexpr const char *kBackupMethodBakFile = "BAK_FILE";
constexpr const char *kBackupMethodDirectory = "DIRECTORY";
constexpr const char *kBackupFrequencyFirstSaveOnly = "FIRST_SAVE_ONLY";
constexpr const char *kBackupFrequencyEverySave = "EVERY_SAVE";
constexpr int kDefaultAutosaveInactivitySeconds = 15;
constexpr int kDefaultAutosaveIntervalSeconds = 180;
constexpr int kMinAutosaveInactivitySeconds = 5;
constexpr int kMaxAutosaveInactivitySeconds = 100;
constexpr int kMinAutosaveIntervalSeconds = 100;
constexpr int kMaxAutosaveIntervalSeconds = 300;

std::string normalizeBackupMethod(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "OFF")
		return kBackupMethodOff;
	if (key == "BAK_FILE" || key == "BAKFILE" || key == "FILE")
		return kBackupMethodBakFile;
	if (key == "DIRECTORY" || key == "DIR" || key == "PATH")
		return kBackupMethodDirectory;
	return std::string();
}

std::string normalizeBackupFrequency(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "FIRST_SAVE_ONLY" || key == "FIRST_SAVE" || key == "FIRST")
		return kBackupFrequencyFirstSaveOnly;
	if (key == "EVERY_SAVE" || key == "EVERY")
		return kBackupFrequencyEverySave;
	return std::string();
}

bool normalizeAutosaveSeconds(const std::string &value, int minValue, int maxValue, int &outValue,
                              const char *fieldName, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty())
		return setError(errorMessage, std::string(fieldName) + " must be 0 or within " + std::to_string(minValue) +
		                               ".." + std::to_string(maxValue) + " seconds.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0')
		return setError(errorMessage, std::string(fieldName) + " must be 0 or within " + std::to_string(minValue) +
		                               ".." + std::to_string(maxValue) + " seconds.");
	if (parsed != 0 && (parsed < minValue || parsed > maxValue))
		return setError(errorMessage, std::string(fieldName) + " must be 0 or within " + std::to_string(minValue) +
		                               ".." + std::to_string(maxValue) + " seconds.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string normalizeBackupExtension(const std::string &value) {
	std::string normalized = trimAscii(value);

	while (!normalized.empty() && normalized.front() == '.')
		normalized.erase(normalized.begin());
	return normalized;
}

bool validateBackupExtension(const std::string &value, std::string *errorMessage) {
	static const std::string invalidChars = std::string("\\") + "/*?:\"<>|";
	std::string normalized = normalizeBackupExtension(value);

	if (normalized.empty())
		return setError(errorMessage, "BACKUP_EXTENSION may not be empty when BACKUP_METHOD=BAK_FILE.");
	if (normalized.size() > 255)
		return setError(errorMessage, "BACKUP_EXTENSION may not exceed 255 characters.");
	for (char ch : normalized)
		if (invalidChars.find(ch) != std::string::npos)
			return setError(errorMessage, "BACKUP_EXTENSION contains invalid filename characters.");
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool validateWritableDirectoryPath(const std::string &path, const char *label, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (normalized.empty())
		return setError(errorMessage, std::string(label) + " may not be empty.");
	if (!isWritableDirectory(normalized))
		return setError(errorMessage, std::string(label) + " is missing or not writable: " + normalized);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

constexpr int kDefaultBinaryRecordLength = 100;
constexpr int kMinBinaryRecordLength = 1;
constexpr int kMaxBinaryRecordLength = 99999;

bool parseBinaryRecordLengthLiteral(const std::string &value, int &outValue, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty())
		return setError(errorMessage, "BINARY_RECORD_LENGTH must be an integer between 1 and 99999.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0')
		return setError(errorMessage, "BINARY_RECORD_LENGTH must be an integer between 1 and 99999.");
	if (parsed < kMinBinaryRecordLength || parsed > kMaxBinaryRecordLength)
		return setError(errorMessage, "BINARY_RECORD_LENGTH must be between 1 and 99999.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string normalizeFormatLine(const std::string &value) {
	if (trimAscii(value).empty())
		return std::string(8, ' ');
	return value;
}

bool normalizeCursorStatusColor(const std::string &value, std::string &outValue, std::string *errorMessage) {
	std::string normalized = upperAscii(trimAscii(value));
	unsigned char parsed = 0;
	static const char *const hex = "0123456789ABCDEF";

	if (normalized.empty()) {
		outValue.clear();
		if (errorMessage != nullptr)
			errorMessage->clear();
		return true;
	}
	if (!parseHexColorToken(normalized, parsed))
		return setError(errorMessage, "CURSOR_STATUS_COLOR must be a hex byte (00..FF) or empty.");
	outValue.clear();
	outValue.push_back(hex[(parsed >> 4) & 0x0F]);
	outValue.push_back(hex[parsed & 0x0F]);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool parseTabSizeLiteral(const std::string &value, int &outValue, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty())
		return setError(errorMessage, "TAB_SIZE must be an integer between 2 and 32.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0')
		return setError(errorMessage, "TAB_SIZE must be an integer between 2 and 32.");
	if (parsed < kMinTabSize || parsed > kMaxTabSize)
		return setError(errorMessage, "TAB_SIZE must be between 2 and 32.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

constexpr int kDefaultRightMargin = 78;
constexpr int kMinRightMargin = 1;
constexpr int kMaxRightMargin = 999;

bool parseRightMarginLiteral(const std::string &value, int &outValue, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty())
		return setError(errorMessage, "RIGHT_MARGIN must be an integer between 1 and 999.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0')
		return setError(errorMessage, "RIGHT_MARGIN must be an integer between 1 and 999.");
	if (parsed < kMinRightMargin || parsed > kMaxRightMargin)
		return setError(errorMessage, "RIGHT_MARGIN must be between 1 and 999.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string normalizePageBreakLiteral(const std::string &value) {
	std::string trimmed = trimAscii(value);

	if (trimmed.empty())
		return kDefaultPageBreakLiteral;
	while (trimmed.size() > 2 && trimmed[0] == '\\' && trimmed[1] == '\\')
		trimmed.erase(trimmed.begin());
	if (trimmed == "\\F")
		return "\\f";
	if (trimmed == "\\N")
		return "\\n";
	if (trimmed == "\\R")
		return "\\r";
	if (trimmed == "\\T")
		return "\\t";
	if (trimmed == "\\f" || trimmed == "\\n" || trimmed == "\\r" || trimmed == "\\t")
		return trimmed;
	if (trimmed.size() == 1) {
		unsigned char ch = static_cast<unsigned char>(trimmed[0]);
		if (ch == '\f')
			return "\\f";
		if (ch == '\n')
			return "\\n";
		if (ch == '\r')
			return "\\r";
		if (ch == '\t')
			return "\\t";
	}
	return trimmed;
}

char decodePageBreakLiteral(const std::string &literal) {
	std::string value = normalizePageBreakLiteral(literal);

	if (value == "\\f")
		return '\f';
	if (value == "\\n")
		return '\n';
	if (value == "\\r")
		return '\r';
	if (value == "\\t")
		return '\t';
	return value.empty() ? '\f' : value[0];
}

std::vector<std::string> parseDefaultExtensions(const std::string &value) {
	std::string text = trimAscii(value);
	std::vector<std::string> out;
	std::string token;
	std::size_t i = 0;

	if (text.size() >= 2 && std::isalpha(static_cast<unsigned char>(text[0])) != 0 && text[1] == ':')
		text = text.substr(2);

	for (i = 0; i <= text.size(); ++i) {
		char ch = (i < text.size()) ? text[i] : ';';
		if (ch == ';' || ch == ':' || ch == ',' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
			std::string ext = trimAscii(token);
			bool duplicate = false;

			token.clear();
			if (ext.empty())
				continue;
			while (!ext.empty() && ext[0] == '.')
				ext.erase(ext.begin());
			ext = trimAscii(ext);
			if (ext.empty())
				continue;
			for (const auto & j : out)
				if (j == ext) {
					duplicate = true;
					break;
				}
			if (!duplicate)
				out.push_back(ext);
			continue;
		}
		token.push_back(ch);
	}
	return out;
}

std::string canonicalDefaultExtensionsLiteral(const std::string &value) {
	std::vector<std::string> list = parseDefaultExtensions(value);
	std::string out;

	for (std::size_t i = 0; i < list.size(); ++i) {
		if (i != 0)
			out.push_back(';');
		out += list[i];
	}
	return out;
}

const MREditSettingDescriptor *editSettingDescriptorByKeyInternal(const std::string &key) {
	std::string upper = upperAscii(trimAscii(key));

		for (const auto & descriptor : kEditSettingDescriptors)
		if (upper == descriptor.key)
			return &descriptor;
	return nullptr;
}

std::string canonicalEditProfileId(const std::string &value) {
	return trimAscii(value);
}

std::string profileIdLookupKey(const std::string &value) {
	return canonicalEditProfileId(value);
}

std::string canonicalEditProfileName(const std::string &value) {
	return trimAscii(value);
}

std::string canonicalWindowColorThemeUri(const std::string &value) {
	std::string trimmed = trimAscii(value);
	if (trimmed.empty())
		return std::string();
	return normalizeConfiguredPathInput(trimmed);
}

std::string normalizeEditExtensionSelectorValue(const std::string &value) {
	std::string normalized = trimAscii(value);

	while (!normalized.empty() && normalized[0] == '.')
		normalized.erase(normalized.begin());
	return trimAscii(normalized);
}

bool containsAsciiSpace(const std::string &value) {
	for (char ch : value)
		if (std::isspace(static_cast<unsigned char>(ch)) != 0)
			return true;
	return false;
}

bool normalizeEditExtensionSelectorsInPlace(std::vector<std::string> &selectors, std::string *errorMessage) {
	std::vector<std::string> normalized;
	std::set<std::string> seen;

	normalized.reserve(selectors.size());
	for (const std::string & selector : selectors) {
		std::string ext = normalizeEditExtensionSelectorValue(selector);

		if (ext.empty())
			continue;
		if (containsAsciiSpace(ext))
			return setError(errorMessage, "Extensions may not contain whitespace.");
		if (ext.find('/') != std::string::npos || ext.find('\\') != std::string::npos)
			return setError(errorMessage, "Extensions may not contain path separators.");
		if (seen.insert(ext).second)
			normalized.push_back(ext);
	}
	selectors.swap(normalized);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool parseAndAssignBooleanLiteral(const std::string &value, bool &target, std::string *errorMessage) {
	bool parsed = false;

	if (!parseBooleanLiteral(value, parsed, errorMessage))
		return false;
	target = parsed;
	return true;
}

std::string extensionSelectorForPath(std::string_view path) {
	std::string normalized = normalizeDialogPath(std::string(path).c_str());
	std::string_view base = normalized;
	std::size_t sep = base.find_last_of("/\\");

	if (sep != std::string::npos)
		base.remove_prefix(sep + 1);
	std::size_t dot = base.find_last_of('.');
	if (base.empty() || dot == std::string::npos || dot + 1 >= base.size())
		return std::string();
	return std::string(base.substr(dot + 1));
}

bool applyEditSetupValueInternal(MREditSetupSettings &current, const std::string &keyName, const std::string &value,
                                 std::string *errorMessage) {
	std::string upperKeyName = upperAscii(trimAscii(keyName));
	std::string normalized;

	if (upperKeyName == "PAGE_BREAK")
		current.pageBreak = normalizePageBreakLiteral(value);
	else if (upperKeyName == "WORD_DELIMITERS") {
		if (trimAscii(value).empty())
			current.wordDelimiters = resolveEditSetupDefaults().wordDelimiters;
		else
			current.wordDelimiters = value;
	} else if (upperKeyName == "DEFAULT_EXTENSIONS")
		current.defaultExtensions = canonicalDefaultExtensionsLiteral(value);
	else if (upperKeyName == "TRUNCATE_SPACES") {
		if (!parseAndAssignBooleanLiteral(value, current.truncateSpaces, errorMessage))
			return false;
	} else if (upperKeyName == "EOF_CTRL_Z") {
		if (!parseAndAssignBooleanLiteral(value, current.eofCtrlZ, errorMessage))
			return false;
	} else if (upperKeyName == "EOF_CR_LF") {
		if (!parseAndAssignBooleanLiteral(value, current.eofCrLf, errorMessage))
			return false;
	} else if (upperKeyName == "TAB_EXPAND") {
		if (!parseAndAssignBooleanLiteral(value, current.tabExpand, errorMessage))
			return false;
	} else if (upperKeyName == "TAB_SIZE") {
		int tabSize = 0;
		if (!parseTabSizeLiteral(value, tabSize, errorMessage))
			return false;
		current.tabSize = tabSize;
	} else if (upperKeyName == "RIGHT_MARGIN") {
		int rightMargin = 0;
		if (!parseRightMarginLiteral(value, rightMargin, errorMessage))
			return false;
		current.rightMargin = rightMargin;
	} else if (upperKeyName == "WORD_WRAP") {
		if (!parseAndAssignBooleanLiteral(value, current.wordWrap, errorMessage))
			return false;
	} else if (upperKeyName == "INDENT_STYLE") {
		normalized = normalizeIndentStyle(value);
		if (normalized.empty())
			return setError(errorMessage, "INDENT_STYLE must be OFF, AUTOMATIC or SMART.");
		current.indentStyle = normalized;
	} else if (upperKeyName == "FILE_TYPE") {
		normalized = normalizeFileType(value);
		if (normalized.empty())
			return setError(errorMessage, "FILE_TYPE must be LEGACY_TEXT, UNIX or BINARY.");
		current.fileType = normalized;
	} else if (upperKeyName == "BINARY_RECORD_LENGTH") {
		int binaryRecordLength = 0;
		if (!parseBinaryRecordLengthLiteral(value, binaryRecordLength, errorMessage))
			return false;
		current.binaryRecordLength = binaryRecordLength;
	} else if (upperKeyName == "POST_LOAD_MACRO")
		current.postLoadMacro = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
	else if (upperKeyName == "PRE_SAVE_MACRO")
		current.preSaveMacro = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
	else if (upperKeyName == "DEFAULT_PATH")
		current.defaultPath = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
	else if (upperKeyName == "FORMAT_LINE")
		current.formatLine = normalizeFormatLine(value);
	else if (upperKeyName == "BACKUP_FILES") {
		if (!parseAndAssignBooleanLiteral(value, current.backupFiles, errorMessage))
			return false;
		if (!current.backupFiles)
			current.backupMethod = kBackupMethodOff;
		else if (normalizeBackupMethod(current.backupMethod).empty() || current.backupMethod == kBackupMethodOff)
			current.backupMethod = kBackupMethodBakFile;
	} else if (upperKeyName == "BACKUP_METHOD") {
		normalized = normalizeBackupMethod(value);
		if (normalized.empty())
			return setError(errorMessage, "BACKUP_METHOD must be OFF, BAK_FILE or DIRECTORY.");
		current.backupMethod = normalized;
		current.backupFiles = normalized != kBackupMethodOff;
	} else if (upperKeyName == "BACKUP_FREQUENCY") {
		normalized = normalizeBackupFrequency(value);
		if (normalized.empty())
			return setError(errorMessage, "BACKUP_FREQUENCY must be FIRST_SAVE_ONLY or EVERY_SAVE.");
		current.backupFrequency = normalized;
	} else if (upperKeyName == "BACKUP_EXTENSION")
		current.backupExtension = normalizeBackupExtension(value);
	else if (upperKeyName == "BACKUP_DIRECTORY")
		current.backupDirectory = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
	else if (upperKeyName == "AUTOSAVE_INACTIVITY_SECONDS") {
		int parsedSeconds = 0;
		if (!normalizeAutosaveSeconds(value, kMinAutosaveInactivitySeconds, kMaxAutosaveInactivitySeconds, parsedSeconds,
		                             "AUTOSAVE_INACTIVITY_SECONDS", errorMessage))
			return false;
		current.autosaveInactivitySeconds = parsedSeconds;
	} else if (upperKeyName == "AUTOSAVE_INTERVAL_SECONDS") {
		int parsedSeconds = 0;
		if (!normalizeAutosaveSeconds(value, kMinAutosaveIntervalSeconds, kMaxAutosaveIntervalSeconds, parsedSeconds,
		                             "AUTOSAVE_INTERVAL_SECONDS", errorMessage))
			return false;
		current.autosaveIntervalSeconds = parsedSeconds;
	} else if (upperKeyName == "SHOW_EOF_MARKER") {
		if (!parseAndAssignBooleanLiteral(value, current.showEofMarker, errorMessage))
			return false;
	} else if (upperKeyName == "SHOW_EOF_MARKER_EMOJI") {
		if (!parseAndAssignBooleanLiteral(value, current.showEofMarkerEmoji, errorMessage))
			return false;
	} else if (upperKeyName == "SHOW_LINE_NUMBERS") {
		if (!parseAndAssignBooleanLiteral(value, current.showLineNumbers, errorMessage))
			return false;
	} else if (upperKeyName == "LINE_NUM_ZERO_FILL") {
		if (!parseAndAssignBooleanLiteral(value, current.lineNumZeroFill, errorMessage))
			return false;
	} else if (upperKeyName == "PERSISTENT_BLOCKS") {
		if (!parseAndAssignBooleanLiteral(value, current.persistentBlocks, errorMessage))
			return false;
	} else if (upperKeyName == "CODE_FOLDING") {
		if (!parseAndAssignBooleanLiteral(value, current.codeFolding, errorMessage))
			return false;
	} else if (upperKeyName == "COLUMN_BLOCK_MOVE") {
		normalized = normalizeColumnBlockMove(value);
		if (normalized.empty())
			return setError(errorMessage, "COLUMN_BLOCK_MOVE must be DELETE_SPACE or LEAVE_SPACE.");
		current.columnBlockMove = normalized;
	} else if (upperKeyName == "DEFAULT_MODE") {
		normalized = normalizeDefaultMode(value);
		if (normalized.empty())
			return setError(errorMessage, "DEFAULT_MODE must be INSERT or OVERWRITE.");
		current.defaultMode = normalized;
	} else if (upperKeyName == "CURSOR_STATUS_COLOR") {
		if (!normalizeCursorStatusColor(value, normalized, errorMessage))
			return false;
		current.cursorStatusColor = normalized;
	} else
		return setError(errorMessage, "Unknown edit setting key.");

	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string editSetupValueLiteral(const MREditSetupSettings &settings, const char *key) {
	std::string upperKey = upperAscii(trimAscii(key != nullptr ? key : ""));

	if (upperKey == "PAGE_BREAK")
		return settings.pageBreak;
	if (upperKey == "WORD_DELIMITERS")
		return settings.wordDelimiters;
	if (upperKey == "DEFAULT_EXTENSIONS")
		return settings.defaultExtensions;
	if (upperKey == "TRUNCATE_SPACES")
		return formatEditSetupBoolean(settings.truncateSpaces);
	if (upperKey == "EOF_CTRL_Z")
		return formatEditSetupBoolean(settings.eofCtrlZ);
	if (upperKey == "EOF_CR_LF")
		return formatEditSetupBoolean(settings.eofCrLf);
	if (upperKey == "TAB_EXPAND")
		return formatEditSetupBoolean(settings.tabExpand);
	if (upperKey == "TAB_SIZE")
		return std::to_string(settings.tabSize);
	if (upperKey == "RIGHT_MARGIN")
		return std::to_string(settings.rightMargin);
	if (upperKey == "WORD_WRAP")
		return formatEditSetupBoolean(settings.wordWrap);
	if (upperKey == "INDENT_STYLE")
		return settings.indentStyle;
	if (upperKey == "FILE_TYPE")
		return settings.fileType;
	if (upperKey == "BINARY_RECORD_LENGTH")
		return std::to_string(settings.binaryRecordLength);
	if (upperKey == "POST_LOAD_MACRO")
		return settings.postLoadMacro;
	if (upperKey == "PRE_SAVE_MACRO")
		return settings.preSaveMacro;
	if (upperKey == "DEFAULT_PATH")
		return settings.defaultPath;
	if (upperKey == "FORMAT_LINE")
		return settings.formatLine;
	if (upperKey == "BACKUP_FILES")
		return formatEditSetupBoolean(settings.backupFiles);
	if (upperKey == "BACKUP_METHOD")
		return settings.backupMethod;
	if (upperKey == "BACKUP_FREQUENCY")
		return settings.backupFrequency;
	if (upperKey == "BACKUP_EXTENSION")
		return settings.backupExtension;
	if (upperKey == "BACKUP_DIRECTORY")
		return settings.backupDirectory;
	if (upperKey == "AUTOSAVE_INACTIVITY_SECONDS")
		return std::to_string(settings.autosaveInactivitySeconds);
	if (upperKey == "AUTOSAVE_INTERVAL_SECONDS")
		return std::to_string(settings.autosaveIntervalSeconds);
	if (upperKey == "SHOW_EOF_MARKER")
		return formatEditSetupBoolean(settings.showEofMarker);
	if (upperKey == "SHOW_EOF_MARKER_EMOJI")
		return formatEditSetupBoolean(settings.showEofMarkerEmoji);
	if (upperKey == "SHOW_LINE_NUMBERS")
		return formatEditSetupBoolean(settings.showLineNumbers);
	if (upperKey == "LINE_NUM_ZERO_FILL")
		return formatEditSetupBoolean(settings.lineNumZeroFill);
	if (upperKey == "PERSISTENT_BLOCKS")
		return formatEditSetupBoolean(settings.persistentBlocks);
	if (upperKey == "CODE_FOLDING")
		return formatEditSetupBoolean(settings.codeFolding);
	if (upperKey == "COLUMN_BLOCK_MOVE")
		return settings.columnBlockMove;
	if (upperKey == "DEFAULT_MODE")
		return settings.defaultMode;
	if (upperKey == "CURSOR_STATUS_COLOR")
		return settings.cursorStatusColor;
	return std::string();
}

unsigned long long supportedEditProfileOverrideMask() noexcept {
	static constexpr unsigned long long mask = kOvPageBreak | kOvWordDelimiters | kOvDefaultExtensions | kOvTruncateSpaces |
	                                     kOvEofCtrlZ | kOvEofCrLf | kOvTabExpand | kOvTabSize | kOvRightMargin |
	                                     kOvWordWrap | kOvIndentStyle | kOvFileType | kOvBinaryRecordLength |
	                                     kOvPostLoadMacro | kOvPreSaveMacro | kOvDefaultPath | kOvFormatLine |
	                                     kOvBackupFiles | kOvShowEofMarker | kOvShowEofMarkerEmoji | kOvShowLineNumbers |
	                                     kOvLineNumZeroFill | kOvPersistentBlocks | kOvCodeFolding | kOvColumnBlockMove |
	                                     kOvDefaultMode | kOvCursorStatusColor;
	return mask;
}

bool normalizeEditProfileOverridesInPlace(MREditExtensionProfile &profile, std::string *errorMessage) {
	std::size_t descriptorCount = 0;
	const MREditSettingDescriptor *descriptors = editSettingDescriptors(descriptorCount);
	MREditSetupSettings normalizedValues = resolveEditSetupDefaults();
	unsigned long long mask = profile.overrides.mask;

	if ((mask & ~supportedEditProfileOverrideMask()) != 0)
		return setError(errorMessage, "Extension profile override mask contains unsupported bits.");

	for (std::size_t i = 0; i < descriptorCount; ++i) {
		const MREditSettingDescriptor &descriptor = descriptors[i];

		if (!descriptor.profileSupported)
			continue;
		if ((mask & descriptor.overrideBit) == 0)
			continue;
		if (!applyEditSetupValueInternal(normalizedValues, descriptor.key,
		                               editSetupValueLiteral(profile.overrides.values, descriptor.key),
		                               errorMessage))
			return false;
	}

	profile.overrides.mask = mask;
	profile.overrides.values = normalizedValues;
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool validateNormalizedEditProfiles(const std::vector<MREditExtensionProfile> &profiles, std::string *errorMessage) {
	std::set<std::string> profileIds;
	std::set<std::string> selectors;

	std::map<std::string, std::string> selectorOwners;

	for (const auto & profile : profiles) {
		std::string id = canonicalEditProfileId(profile.id);
		std::string name = canonicalEditProfileName(profile.name);
		std::string lookup = profileIdLookupKey(id);

		if (id.empty())
			return setError(errorMessage, "Extension profile id may not be empty.");
		if (name.empty())
			return setError(errorMessage, "Extension profile name may not be empty.");
		if (!profileIds.insert(lookup).second)
			return setError(errorMessage, "Duplicate extension profile id: " + id + " (" + name + ")");
		if (!profile.windowColorThemeUri.empty() &&
		    !validateColorThemeFilePath(profile.windowColorThemeUri, errorMessage))
			return false;
		for (const std::string & ext : profile.extensions) {
			std::string ownerLabel = id + " (" + name + ")";
			auto it = selectorOwners.find(ext);
			if (it != selectorOwners.end())
				return setError(errorMessage, "Duplicate profile extension '" + ext + "': " + it->second + " and " + ownerLabel);
			selectorOwners.insert(std::make_pair(ext, ownerLabel));
		}
	}
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

} // namespace

std::string normalizeConfiguredPathInput(std::string_view input) {
	return makeAbsolutePath(normalizeDialogPath(expandUserPath(input).c_str()));
}

MRSetupPaths resolveSetupPathDefaults() {
	MRSetupPaths defaults;
	std::string xdgConfig = pathFromEnvironment("XDG_CONFIG_HOME");
	const char *homeEnv = std::getenv("HOME");
	std::string home = (homeEnv != nullptr && *homeEnv != '\0')
	                       ? makeAbsolutePath(normalizeDialogPath(homeEnv))
	                       : std::string();
	std::string cwd = currentWorkingDirectory();
	std::string exeDir = makeAbsolutePath(executableDirectory());
	std::string candidate;
	std::string configBase;

	defaults.tempPath = builtInTempDirectoryPath();
	if (defaults.tempPath.empty())
		defaults.tempPath = "/tmp";

	if (!xdgConfig.empty())
		defaults.settingsMacroUri = appendFileName(appendPathSegment(xdgConfig, "mr"), "settings.mrmac");
	else if (!home.empty()) {
		configBase = appendPathSegment(home, ".config");
		defaults.settingsMacroUri = appendFileName(appendPathSegment(configBase, "mr"), "settings.mrmac");
	} else if (!cwd.empty()) {
		configBase = appendPathSegment(cwd, ".config");
		defaults.settingsMacroUri = appendFileName(appendPathSegment(configBase, "mr"), "settings.mrmac");
	} else if (!exeDir.empty()) {
		configBase = appendPathSegment(exeDir, ".config");
		defaults.settingsMacroUri = appendFileName(appendPathSegment(configBase, "mr"), "settings.mrmac");
	} else
		defaults.settingsMacroUri = appendFileName(defaults.tempPath, "settings.mrmac");

	if (!cwd.empty()) {
		candidate = appendPathSegment(appendPathSegment(cwd, "mrmac"), "macros");
		if (isReadableDirectory(candidate))
			defaults.macroPath = candidate;
	}
	if (defaults.macroPath.empty() && !exeDir.empty()) {
		candidate = appendPathSegment(appendPathSegment(exeDir, "mrmac"), "macros");
		if (isReadableDirectory(candidate))
			defaults.macroPath = candidate;
	}
	if (defaults.macroPath.empty() && !cwd.empty() && isReadableDirectory(cwd))
		defaults.macroPath = cwd;
	if (defaults.macroPath.empty())
		defaults.macroPath = defaults.tempPath;

	if (!cwd.empty()) {
		candidate = appendFileName(cwd, "mr.hlp");
		if (isReadableFile(candidate))
			defaults.helpUri = candidate;
	}
	if (defaults.helpUri.empty() && !exeDir.empty()) {
		candidate = appendFileName(exeDir, "mr.hlp");
		if (isReadableFile(candidate))
			defaults.helpUri = candidate;
	}
	if (defaults.helpUri.empty() && !cwd.empty())
		defaults.helpUri = appendFileName(cwd, "mr.hlp");
	if (defaults.helpUri.empty() && !exeDir.empty())
		defaults.helpUri = appendFileName(exeDir, "mr.hlp");
	if (defaults.helpUri.empty())
		defaults.helpUri = appendFileName(defaults.tempPath, "mr.hlp");

	defaults.shellUri = builtInShellExecutablePath();
	if (defaults.shellUri.empty())
		defaults.shellUri = "/bin/sh";

	defaults.settingsMacroUri = makeAbsolutePath(defaults.settingsMacroUri);
	defaults.macroPath = makeAbsolutePath(defaults.macroPath);
	defaults.helpUri = makeAbsolutePath(defaults.helpUri);
	defaults.tempPath = makeAbsolutePath(defaults.tempPath);
	defaults.shellUri = makeAbsolutePath(defaults.shellUri);
	return defaults;
}

MREditSetupSettings resolveEditSetupDefaults() {
	MREditSetupSettings defaults;

	defaults.pageBreak = kDefaultPageBreakLiteral;
	defaults.wordDelimiters = kDefaultWordDelimiters;
	defaults.defaultExtensions = kDefaultDefaultExtensions;
	defaults.truncateSpaces = true;
	defaults.eofCtrlZ = false;
	defaults.eofCrLf = false;
	defaults.tabExpand = true;
	defaults.tabSize = kDefaultTabSize;
	defaults.rightMargin = kDefaultRightMargin;
	defaults.wordWrap = true;
	defaults.indentStyle = kIndentStyleOff;
	defaults.fileType = kFileTypeUnix;
	defaults.binaryRecordLength = kDefaultBinaryRecordLength;
	defaults.postLoadMacro.clear();
	defaults.preSaveMacro.clear();
	defaults.defaultPath.clear();
	defaults.formatLine = std::string(8, ' ');
	defaults.backupMethod = kBackupMethodBakFile;
	defaults.backupFrequency = kBackupFrequencyFirstSaveOnly;
	defaults.backupExtension = "bak";
	defaults.backupDirectory.clear();
	defaults.autosaveInactivitySeconds = kDefaultAutosaveInactivitySeconds;
	defaults.autosaveIntervalSeconds = kDefaultAutosaveIntervalSeconds;
	defaults.backupFiles = true;
	defaults.showEofMarker = false;
	defaults.showEofMarkerEmoji = true;
	defaults.showLineNumbers = false;
	defaults.lineNumZeroFill = false;
	defaults.persistentBlocks = true;
	defaults.codeFolding = false;
	defaults.columnBlockMove = kColumnBlockMoveDelete;
	defaults.defaultMode = kDefaultModeInsert;
	defaults.cursorStatusColor = kDefaultCursorStatusColor;
	return defaults;
}

MRColorSetupSettings resolveColorSetupDefaults() {
	return defaultsFromColorGroups();
}

MRSettingsKeyClass classifySettingsKey(std::string_view key) {
	std::string upper = upperAscii(trimAscii(key));

	if (upper.empty())
		return MRSettingsKeyClass::Unknown;
	for (const auto & descriptor : kFixedSettingsKeyDescriptors)
		if (upper == descriptor.key)
			return descriptor.keyClass;
	return editSettingDescriptorByKeyInternal(upper) != nullptr ? MRSettingsKeyClass::Edit
	                                                           : MRSettingsKeyClass::Unknown;
}

bool isCanonicalSerializedSettingsKey(std::string_view key) {
	std::string upper = upperAscii(trimAscii(key));

	if (upper.empty())
		return false;
	for (const auto & descriptor : kFixedSettingsKeyDescriptors)
		if (upper == descriptor.key)
			return descriptor.serialized;
	return editSettingDescriptorByKeyInternal(upper) != nullptr;
}

std::size_t canonicalSerializedSettingsKeyCount() {
	std::size_t editDescriptorCount = 0;
	std::size_t fixedSerializedCount = 0;

	for (const auto & descriptor : kFixedSettingsKeyDescriptors)
		if (descriptor.serialized)
			++fixedSerializedCount;
	(void)editSettingDescriptors(editDescriptorCount);
	return fixedSerializedCount + editDescriptorCount;
}

bool resetConfiguredSettingsModel(const std::string &settingsPath, MRSetupPaths &paths, std::string *errorMessage) {
	paths = resolveSetupPathDefaults();
	paths.settingsMacroUri = normalizeConfiguredPathInput(settingsPath);
	if (paths.settingsMacroUri.empty())
		return setError(errorMessage, "Settings path is empty.");
	if (!setConfiguredSettingsMacroFilePath(paths.settingsMacroUri, errorMessage))
		return false;
	if (!setConfiguredMacroDirectoryPath(paths.macroPath, errorMessage))
		return false;
	if (!setConfiguredHelpFilePath(paths.helpUri, errorMessage))
		return false;
	if (!setConfiguredTempDirectoryPath(paths.tempPath, errorMessage))
		return false;
	if (!setConfiguredShellExecutablePath(paths.shellUri, errorMessage))
		return false;
	if (!setConfiguredLastFileDialogPath(paths.macroPath, errorMessage))
		return false;
	if (!setConfiguredDefaultProfileDescription("Global defaults", errorMessage))
		return false;
	if (!setConfiguredEditSetupSettings(resolveEditSetupDefaults(), errorMessage))
		return false;
	configuredColorSettings() = defaultsFromColorGroups();
	configuredColorSettingsInitialized() = true;
	if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(), errorMessage))
		return false;
	if (!setConfiguredColorThemeFilePath(defaultColorThemeFilePath(), errorMessage))
		return false;
	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = configuredMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool applyConfiguredSettingsAssignment(const std::string &key, const std::string &value, MRSetupPaths &paths,
                                       std::string *errorMessage) {
	auto applyValidatedNormalizedPath = [&](auto validator, auto setter, std::string &target) {
		if (!validator(value, errorMessage))
			return false;
		if (!setter(value, errorMessage))
			return false;
		target = normalizeConfiguredPathInput(value);
		return true;
	};

	switch (classifySettingsKey(key)) {
		case MRSettingsKeyClass::Unknown:
			return setError(errorMessage, "Unsupported MRSETUP key.");
		case MRSettingsKeyClass::Version:
			if (trimAscii(value) != kCurrentSettingsVersion)
				return setError(errorMessage, "Unsupported settings version.");
			if (errorMessage != nullptr)
				errorMessage->clear();
			return true;
		case MRSettingsKeyClass::Path: {
			std::string upper = upperAscii(trimAscii(key));
			if (upper == "SETTINGSPATH") {
				paths.settingsMacroUri = configuredSettingsMacroFilePath();
				if (errorMessage != nullptr)
					errorMessage->clear();
				return true;
			}
			if (upper == "MACROPATH")
				return applyValidatedNormalizedPath(validateMacroDirectoryPath, setConfiguredMacroDirectoryPath,
				                                   paths.macroPath);
			if (upper == "HELPPATH")
				return applyValidatedNormalizedPath(validateHelpFilePath, setConfiguredHelpFilePath, paths.helpUri);
			if (upper == "TEMPDIR")
				return applyValidatedNormalizedPath(validateTempDirectoryPath, setConfiguredTempDirectoryPath,
				                                   paths.tempPath);
			if (upper == "SHELLPATH")
				return applyValidatedNormalizedPath(validateShellExecutablePath, setConfiguredShellExecutablePath,
				                                   paths.shellUri);
			break;
		}
		case MRSettingsKeyClass::Global: {
			std::string upper = upperAscii(trimAscii(key));
			if (upper == "LASTFILEDIALOGPATH")
				return setConfiguredLastFileDialogPath(value, errorMessage);
			if (upper == "DEFAULT_PROFILE_DESCRIPTION")
				return setConfiguredDefaultProfileDescription(value, errorMessage);
			if (upper == kThemeSettingsKey)
				return setConfiguredColorThemeFilePath(value, errorMessage);
			break;
		}
		case MRSettingsKeyClass::Edit:
			return applyConfiguredEditSetupValue(key, value, errorMessage);
		case MRSettingsKeyClass::ColorInline:
			return applyConfiguredColorSetupValue(key, value, errorMessage);
	}
	return setError(errorMessage, "Unsupported MRSETUP key.");
}

const MREditSettingDescriptor *editSettingDescriptors(std::size_t &count) {
	count = std::size(kEditSettingDescriptors);
	return kEditSettingDescriptors;
}

const MREditSettingDescriptor *findEditSettingDescriptorByKey(std::string_view key) {
		return editSettingDescriptorByKeyInternal(std::string(key));
}

std::string normalizeEditExtensionSelector(std::string_view value) {
		return normalizeEditExtensionSelectorValue(std::string(value));
}

bool normalizeEditExtensionSelectors(std::vector<std::string> &selectors, std::string *errorMessage) {
	return normalizeEditExtensionSelectorsInPlace(selectors, errorMessage);
}

MREditSetupSettings mergeEditSetupSettings(const MREditSetupSettings &defaults,
                                           const MREditSetupOverrides &overrides) {
	MREditSetupSettings merged = defaults;

	if ((overrides.mask & kOvPageBreak) != 0)
		merged.pageBreak = overrides.values.pageBreak;
	if ((overrides.mask & kOvWordDelimiters) != 0)
		merged.wordDelimiters = overrides.values.wordDelimiters;
	if ((overrides.mask & kOvDefaultExtensions) != 0)
		merged.defaultExtensions = overrides.values.defaultExtensions;
	if ((overrides.mask & kOvTruncateSpaces) != 0)
		merged.truncateSpaces = overrides.values.truncateSpaces;
	if ((overrides.mask & kOvEofCtrlZ) != 0)
		merged.eofCtrlZ = overrides.values.eofCtrlZ;
	if ((overrides.mask & kOvEofCrLf) != 0)
		merged.eofCrLf = overrides.values.eofCrLf;
	if ((overrides.mask & kOvTabExpand) != 0)
		merged.tabExpand = overrides.values.tabExpand;
	if ((overrides.mask & kOvTabSize) != 0)
		merged.tabSize = overrides.values.tabSize;
	if ((overrides.mask & kOvRightMargin) != 0)
		merged.rightMargin = overrides.values.rightMargin;
	if ((overrides.mask & kOvWordWrap) != 0)
		merged.wordWrap = overrides.values.wordWrap;
	if ((overrides.mask & kOvIndentStyle) != 0)
		merged.indentStyle = overrides.values.indentStyle;
	if ((overrides.mask & kOvFileType) != 0)
		merged.fileType = overrides.values.fileType;
	if ((overrides.mask & kOvBinaryRecordLength) != 0)
		merged.binaryRecordLength = overrides.values.binaryRecordLength;
	if ((overrides.mask & kOvPostLoadMacro) != 0)
		merged.postLoadMacro = overrides.values.postLoadMacro;
	if ((overrides.mask & kOvPreSaveMacro) != 0)
		merged.preSaveMacro = overrides.values.preSaveMacro;
	if ((overrides.mask & kOvDefaultPath) != 0)
		merged.defaultPath = overrides.values.defaultPath;
	if ((overrides.mask & kOvFormatLine) != 0)
		merged.formatLine = overrides.values.formatLine;
	if ((overrides.mask & kOvBackupFiles) != 0)
		merged.backupFiles = overrides.values.backupFiles;
	if ((overrides.mask & kOvBackupMethod) != 0)
		merged.backupMethod = overrides.values.backupMethod;
	if ((overrides.mask & kOvBackupFrequency) != 0)
		merged.backupFrequency = overrides.values.backupFrequency;
	if ((overrides.mask & kOvBackupExtension) != 0)
		merged.backupExtension = overrides.values.backupExtension;
	if ((overrides.mask & kOvBackupDirectory) != 0)
		merged.backupDirectory = overrides.values.backupDirectory;
	if ((overrides.mask & kOvAutosaveInactivitySeconds) != 0)
		merged.autosaveInactivitySeconds = overrides.values.autosaveInactivitySeconds;
	if ((overrides.mask & kOvAutosaveIntervalSeconds) != 0)
		merged.autosaveIntervalSeconds = overrides.values.autosaveIntervalSeconds;
	if ((overrides.mask & kOvShowEofMarker) != 0)
		merged.showEofMarker = overrides.values.showEofMarker;
	if ((overrides.mask & kOvShowEofMarkerEmoji) != 0)
		merged.showEofMarkerEmoji = overrides.values.showEofMarkerEmoji;
	if ((overrides.mask & kOvShowLineNumbers) != 0)
		merged.showLineNumbers = overrides.values.showLineNumbers;
	if ((overrides.mask & kOvLineNumZeroFill) != 0)
		merged.lineNumZeroFill = overrides.values.lineNumZeroFill;
	if ((overrides.mask & kOvPersistentBlocks) != 0)
		merged.persistentBlocks = overrides.values.persistentBlocks;
	if ((overrides.mask & kOvCodeFolding) != 0)
		merged.codeFolding = overrides.values.codeFolding;
	if ((overrides.mask & kOvColumnBlockMove) != 0)
		merged.columnBlockMove = overrides.values.columnBlockMove;
	if ((overrides.mask & kOvDefaultMode) != 0)
		merged.defaultMode = overrides.values.defaultMode;
	if ((overrides.mask & kOvCursorStatusColor) != 0)
		merged.cursorStatusColor = overrides.values.cursorStatusColor;
	return merged;
}

const std::vector<MREditExtensionProfile> &configuredEditExtensionProfiles() {
	return configuredEditProfiles();
}

bool setConfiguredEditExtensionProfiles(const std::vector<MREditExtensionProfile> &profiles,
                                        std::string *errorMessage) {
	std::vector<MREditExtensionProfile> normalized = profiles;

	for (auto & profile : normalized) {
		profile.id = canonicalEditProfileId(profile.id);
		profile.name = canonicalEditProfileName(profile.name);
		profile.windowColorThemeUri = canonicalWindowColorThemeUri(profile.windowColorThemeUri);
		if (!normalizeEditExtensionSelectorsInPlace(profile.extensions, errorMessage))
			return false;
		if (!normalizeEditProfileOverridesInPlace(profile, errorMessage))
			return false;
	}
	if (!validateNormalizedEditProfiles(normalized, errorMessage))
		return false;
	configuredEditProfiles() = normalized;
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string configuredDefaultProfileDescription() {
	return configuredDefaultProfileDescriptionValue();
}

bool setConfiguredDefaultProfileDescription(const std::string &value, std::string *errorMessage) {
	configuredDefaultProfileDescriptionValue() = trimAscii(value);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool applyConfiguredEditExtensionProfileDirective(const std::string &operation, const std::string &profileId,
                                                  const std::string &arg3, const std::string &arg4,
                                                  std::string *errorMessage) {
	std::string op = upperAscii(trimAscii(operation));
	std::string id = canonicalEditProfileId(profileId);
	std::vector<MREditExtensionProfile> profiles = configuredEditProfiles();
	MREditExtensionProfile *profile = nullptr;
	std::size_t i = 0;

	if (op.empty())
		return setError(errorMessage, "MRFEPROFILE operation may not be empty.");
	if (id.empty())
		return setError(errorMessage, "MRFEPROFILE profile id may not be empty.");

	for (i = 0; i < profiles.size(); ++i)
		if (profileIdLookupKey(profiles[i].id) == profileIdLookupKey(id)) {
			profile = &profiles[i];
			break;
		}

	if (op == "DEFINE") {
		std::string name = canonicalEditProfileName(arg3);

		if (name.empty() && trimAscii(arg4).empty())
			name = id;
		if (name.empty())
			return setError(errorMessage, "MRFEPROFILE DEFINE requires a non-empty display name.");
		if (profile != nullptr)
			return setError(errorMessage, "Duplicate extension profile id: " + id);
		MREditExtensionProfile created;
		created.id = id;
		created.name = name;
		created.overrides.values = resolveEditSetupDefaults();
		profiles.push_back(created);
		return setConfiguredEditExtensionProfiles(profiles, errorMessage);
	}

	if (profile == nullptr)
		return setError(errorMessage, "Unknown extension profile id: " + id);

	if (op == "EXT") {
		profile->extensions.push_back(arg3);
		return setConfiguredEditExtensionProfiles(profiles, errorMessage);
	}

	if (op == "SET") {
		if (upperAscii(trimAscii(arg3)) == kWindowColorThemeProfileKey) {
			std::string normalizedTheme = canonicalWindowColorThemeUri(arg4);
			if (!normalizedTheme.empty() && !validateColorThemeFilePath(normalizedTheme, errorMessage))
				return false;
			profile->windowColorThemeUri = normalizedTheme;
			return setConfiguredEditExtensionProfiles(profiles, errorMessage);
		}

		const MREditSettingDescriptor *descriptor = editSettingDescriptorByKeyInternal(arg3);

		if (descriptor == nullptr)
			return setError(errorMessage, "Unknown edit setting key for extension profile.");
		if (!descriptor->profileSupported)
			return setError(errorMessage, std::string("Setting is global-only and cannot be overridden: ") +
			                             descriptor->key);
		if (!applyEditSetupValueInternal(profile->overrides.values, descriptor->key, arg4, errorMessage))
			return false;
		profile->overrides.mask |= descriptor->overrideBit;
		return setConfiguredEditExtensionProfiles(profiles, errorMessage);
	}

	return setError(errorMessage, "MRFEPROFILE supports operations DEFINE, EXT and SET.");
}

bool effectiveEditSetupSettingsForPath(const std::string &path, MREditSetupSettings &out,
                                       std::string *matchedProfileName) {
	MREditSetupSettings defaults = configuredEditSetupSettings();
	std::string ext = extensionSelectorForPath(path);

	out = defaults;
	if (matchedProfileName != nullptr)
		matchedProfileName->clear();
	if (ext.empty())
		return true;
	for (const auto & profile : configuredEditProfiles())
		for (const std::string & selector : profile.extensions)
			if (selector == ext) {
				out = mergeEditSetupSettings(defaults, profile.overrides);
				if (matchedProfileName != nullptr)
					*matchedProfileName = profile.name;
				return true;
			}
	return true;
}

bool effectiveEditWindowColorThemePathForPath(const std::string &path, std::string &themeUri,
                                              std::string *matchedProfileName) {
	std::string ext = extensionSelectorForPath(path);

	themeUri = configuredColorThemeFilePath();
	if (matchedProfileName != nullptr)
		matchedProfileName->clear();
	if (ext.empty())
		return true;
	for (const auto & profile : configuredEditProfiles())
		for (const std::string & selector : profile.extensions)
			if (selector == ext) {
				if (!profile.windowColorThemeUri.empty())
					themeUri = profile.windowColorThemeUri;
				if (matchedProfileName != nullptr)
					*matchedProfileName = profile.name;
				return true;
			}
	return true;
}

MREditSetupSettings configuredEditSetupSettings() {
	static bool initialized = false;
	MREditSetupSettings &configured = configuredEditSettings();

	if (!initialized) {
		configured = resolveEditSetupDefaults();
		initialized = true;
	}
	return configured;
}

MRColorSetupSettings configuredColorSetupSettings() {
	ensureConfiguredColorSettingsInitialized();
	return configuredColorSettings();
}

bool setConfiguredEditSetupSettings(const MREditSetupSettings &settings, std::string *errorMessage) {
	MREditSetupSettings defaults = resolveEditSetupDefaults();
	MREditSetupSettings normalized = settings;
	std::string pageBreak = normalizePageBreakLiteral(settings.pageBreak);
	std::string wordDelimiters = settings.wordDelimiters.empty() ? defaults.wordDelimiters
	                                                             : settings.wordDelimiters;
	std::string defaultExts = canonicalDefaultExtensionsLiteral(settings.defaultExtensions);
	std::string columnStyle = normalizeColumnBlockMove(settings.columnBlockMove);
	std::string defaultMode = normalizeDefaultMode(settings.defaultMode);
	std::string indentStyle = normalizeIndentStyle(settings.indentStyle);
	std::string fileType = normalizeFileType(settings.fileType);
	std::string formatLine = normalizeFormatLine(settings.formatLine);
	std::string cursorStatusColor;
	std::string postLoadMacro = trimAscii(settings.postLoadMacro).empty() ? std::string()
	                                                                  : normalizeConfiguredPathInput(settings.postLoadMacro);
	std::string preSaveMacro = trimAscii(settings.preSaveMacro).empty() ? std::string()
	                                                                : normalizeConfiguredPathInput(settings.preSaveMacro);
	std::string defaultPath = trimAscii(settings.defaultPath).empty() ? std::string()
	                                                              : normalizeConfiguredPathInput(settings.defaultPath);

	if (wordDelimiters.empty())
		return setError(errorMessage, "WORD_DELIMITERS may not be empty.");
	if (columnStyle.empty())
		return setError(errorMessage, "COLUMN_BLOCK_MOVE must be DELETE_SPACE or LEAVE_SPACE.");
	if (defaultMode.empty())
		return setError(errorMessage, "DEFAULT_MODE must be INSERT or OVERWRITE.");
	if (indentStyle.empty())
		return setError(errorMessage, "INDENT_STYLE must be OFF, AUTOMATIC or SMART.");
	if (fileType.empty())
		return setError(errorMessage, "FILE_TYPE must be LEGACY_TEXT, UNIX or BINARY.");
	if (!normalizeCursorStatusColor(settings.cursorStatusColor, cursorStatusColor, errorMessage))
		return false;
	if (settings.binaryRecordLength < kMinBinaryRecordLength || settings.binaryRecordLength > kMaxBinaryRecordLength)
		return setError(errorMessage, "BINARY_RECORD_LENGTH must be between 1 and 99999.");
	if (settings.tabSize < kMinTabSize || settings.tabSize > kMaxTabSize)
		return setError(errorMessage, "TAB_SIZE must be between 2 and 32.");
	if (settings.rightMargin < kMinRightMargin || settings.rightMargin > kMaxRightMargin)
		return setError(errorMessage, "RIGHT_MARGIN must be between 1 and 999.");

	normalized.truncateSpaces = settings.truncateSpaces;
	normalized.eofCtrlZ = settings.eofCtrlZ;
	normalized.eofCrLf = settings.eofCrLf;
	normalized.tabExpand = settings.tabExpand;
	normalized.tabSize = settings.tabSize;
	normalized.rightMargin = settings.rightMargin;
	normalized.wordWrap = settings.wordWrap;
	normalized.indentStyle = indentStyle;
	normalized.fileType = fileType;
	normalized.binaryRecordLength = settings.binaryRecordLength;
	normalized.postLoadMacro = postLoadMacro;
	normalized.preSaveMacro = preSaveMacro;
	normalized.defaultPath = defaultPath;
	normalized.formatLine = formatLine;
	normalized.backupMethod = normalizeBackupMethod(settings.backupMethod);
	if (normalized.backupMethod.empty())
		return setError(errorMessage, "BACKUP_METHOD must be OFF, BAK_FILE or DIRECTORY.");
	normalized.backupFrequency = normalizeBackupFrequency(settings.backupFrequency);
	if (normalized.backupFrequency.empty())
		return setError(errorMessage, "BACKUP_FREQUENCY must be FIRST_SAVE_ONLY or EVERY_SAVE.");
	normalized.backupExtension = normalizeBackupExtension(settings.backupExtension);
	normalized.backupDirectory = trimAscii(settings.backupDirectory).empty() ? std::string() : normalizeConfiguredPathInput(settings.backupDirectory);
	if (normalized.backupMethod == kBackupMethodBakFile && !validateBackupExtension(normalized.backupExtension, errorMessage))
		return false;
	if (normalized.backupMethod == kBackupMethodDirectory &&
	    !validateWritableDirectoryPath(normalized.backupDirectory, "BACKUP_DIRECTORY", errorMessage))
		return false;
	if (normalized.backupMethod != kBackupMethodBakFile)
		normalized.backupExtension = normalizeBackupExtension(settings.backupExtension).empty() ? defaults.backupExtension : normalizeBackupExtension(settings.backupExtension);
	if (normalized.backupMethod != kBackupMethodDirectory && trimAscii(normalized.backupDirectory).empty())
		normalized.backupDirectory.clear();
	if (settings.autosaveInactivitySeconds != 0 &&
	    (settings.autosaveInactivitySeconds < kMinAutosaveInactivitySeconds || settings.autosaveInactivitySeconds > kMaxAutosaveInactivitySeconds))
		return setError(errorMessage, "AUTOSAVE_INACTIVITY_SECONDS must be 0 or within 5..100 seconds.");
	if (settings.autosaveIntervalSeconds != 0 &&
	    (settings.autosaveIntervalSeconds < kMinAutosaveIntervalSeconds || settings.autosaveIntervalSeconds > kMaxAutosaveIntervalSeconds))
		return setError(errorMessage, "AUTOSAVE_INTERVAL_SECONDS must be 0 or within 100..300 seconds.");
	normalized.autosaveInactivitySeconds = settings.autosaveInactivitySeconds;
	normalized.autosaveIntervalSeconds = settings.autosaveIntervalSeconds;
	normalized.backupFiles = normalized.backupMethod != kBackupMethodOff;
	normalized.showEofMarker = settings.showEofMarker;
	normalized.showEofMarkerEmoji = settings.showEofMarkerEmoji;
	normalized.showLineNumbers = settings.showLineNumbers;
	normalized.lineNumZeroFill = settings.lineNumZeroFill;
	normalized.persistentBlocks = settings.persistentBlocks;
	normalized.codeFolding = settings.codeFolding;

	normalized.pageBreak = pageBreak;
	normalized.wordDelimiters = wordDelimiters;
	normalized.defaultExtensions = defaultExts;
	normalized.columnBlockMove = columnStyle;
	normalized.defaultMode = defaultMode;
	normalized.cursorStatusColor = cursorStatusColor;
	configuredEditSettings() = normalized;
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool setConfiguredColorSetupGroupValues(MRColorSetupGroup group, const unsigned char *values, std::size_t count,
                                        std::string *errorMessage) {
	const ColorGroupDefinition *definition = findColorGroupDefinition(group);
	MRColorSetupSettings &configured = configuredColorSettings();

	ensureConfiguredColorSettingsInitialized();
	if (definition == nullptr)
		return setError(errorMessage, "Unknown color setup group.");
	if (values == nullptr || count != definition->count)
		return setError(errorMessage, "Unexpected color setup group value count.");

	switch (group) {
		case MRColorSetupGroup::Window:
			for (std::size_t i = 0; i < configured.windowColors.size(); ++i)
				configured.windowColors[i] = values[i];
			break;
		case MRColorSetupGroup::MenuDialog:
			for (std::size_t i = 0; i < configured.menuDialogColors.size(); ++i)
				configured.menuDialogColors[i] = values[i];
			break;
		case MRColorSetupGroup::Help:
			for (std::size_t i = 0; i < configured.helpColors.size(); ++i)
				configured.helpColors[i] = values[i];
			break;
		case MRColorSetupGroup::Other:
			for (std::size_t i = 0; i < configured.otherColors.size(); ++i)
				configured.otherColors[i] = values[i];
			break;
	}

	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

void configuredColorSetupGroupValues(MRColorSetupGroup group, unsigned char *values, std::size_t count) {
	const ColorGroupDefinition *definition = findColorGroupDefinition(group);
	MRColorSetupSettings configured = configuredColorSetupSettings();

	if (values == nullptr || definition == nullptr || count != definition->count)
		return;

	switch (group) {
		case MRColorSetupGroup::Window:
			for (std::size_t i = 0; i < configured.windowColors.size(); ++i)
				values[i] = configured.windowColors[i];
			break;
		case MRColorSetupGroup::MenuDialog:
			for (std::size_t i = 0; i < configured.menuDialogColors.size(); ++i)
				values[i] = configured.menuDialogColors[i];
			break;
		case MRColorSetupGroup::Help:
			for (std::size_t i = 0; i < configured.helpColors.size(); ++i)
				values[i] = configured.helpColors[i];
			break;
		case MRColorSetupGroup::Other:
			for (std::size_t i = 0; i < configured.otherColors.size(); ++i)
				values[i] = configured.otherColors[i];
			break;
	}
}

std::string defaultColorThemeFilePath() {
	return defaultColorThemePathForSettings(configuredSettingsMacroFilePath());
}

bool validateColorThemeFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	struct stat st;

	if (normalized.empty())
		return setError(errorMessage, "Empty color theme URI.");
	if (::stat(normalized.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
		return setError(errorMessage, "Color theme URI must include a filename.");
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool setConfiguredColorThemeFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (!validateColorThemeFilePath(path, errorMessage))
		return false;
	configuredColorThemeFile() = makeAbsolutePath(normalized);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string configuredColorThemeFilePath() {
	const std::string &configured = configuredColorThemeFile();
	if (!configured.empty())
		return makeAbsolutePath(configured);
	return defaultColorThemeFilePath();
}

std::string configuredColorThemeDisplayName() {
	std::string path = configuredColorThemeFilePath();
	std::string name = fileNamePartOf(path);

	if (name.empty())
		return std::string("<none>");
	return name;
}

std::string buildColorThemeMacroSource() {
	MRColorSetupSettings colors = configuredColorSetupSettings();
	std::string source;

	source += "$MACRO MR_COLOR_THEME FROM EDIT;\n";
	source +=
	    "MRSETUP('WINDOWCOLORS', '" +
	    escapeMrmacSingleQuotedLiteral(formatWindowColorListLiteral(colors.windowColors)) +
	    "');\n";
	source += "MRSETUP('MENUDIALOGCOLORS', '" +
	          escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.menuDialogColors)) + "');\n";
	source += "MRSETUP('HELPCOLORS', '" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.helpColors)) +
	          "');\n";
	source +=
	    "MRSETUP('OTHERCOLORS', '" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.otherColors)) +
	    "');\n";
	source += "END_MACRO;\n";
	return source;
}

bool writeColorThemeFile(const std::string &themeUri, std::string *errorMessage) {
	std::string themePath = normalizeConfiguredPathInput(themeUri);
	std::string themeDir = directoryPartOf(themePath);
	std::string source;

	if (!validateColorThemeFilePath(themePath, errorMessage))
		return false;
	if (!ensureDirectoryTree(themeDir, errorMessage))
		return false;
	source = buildColorThemeMacroSource();
	if (!writeTextFile(themePath, source))
		return setError(errorMessage, "Unable to write color theme file: " + themePath);
	if (!setConfiguredColorThemeFilePath(themePath, errorMessage))
		return false;
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool ensureColorThemeFileExists(const std::string &themeUri, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(themeUri);
	struct stat st;

	if (!validateColorThemeFilePath(normalized, errorMessage))
		return false;
	if (::stat(normalized.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode))
			return setError(errorMessage, "Color theme URI must include a filename.");
		if (errorMessage != nullptr)
			errorMessage->clear();
		return true;
	}
	return writeColorThemeFile(normalized, errorMessage);
}

bool loadColorThemeFile(const std::string &themeUri, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(themeUri);
	std::string source;
	std::string applyError;
	std::map<std::string, std::string> assignments;
	static const char *const order[] = {"WINDOWCOLORS", "MENUDIALOGCOLORS", "HELPCOLORS", "OTHERCOLORS"};

	if (!validateColorThemeFilePath(normalized, errorMessage))
		return false;
	if (!ensureColorThemeFileExists(normalized, errorMessage))
		return false;
	if (!readTextFile(normalized, source))
		return setError(errorMessage, "Unable to read color theme file: " + normalized);
	if (!parseThemeSetupAssignments(source, assignments, errorMessage))
		return false;
	for (const char *key : order)
		if (!applyConfiguredColorSetupValue(key, assignments[key], &applyError))
			return setError(errorMessage, "Theme apply failed for " + std::string(key) + ": " + applyError);
	if (!setConfiguredColorThemeFilePath(normalized, errorMessage))
		return false;
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool loadWindowColorThemeGroupValues(const std::string &themeUri,
                                     std::array<unsigned char, MRColorSetupSettings::kWindowCount> &outValues,
                                     std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(themeUri);
	std::string source;
	std::map<std::string, std::string> assignments;

	if (!validateColorThemeFilePath(normalized, errorMessage))
		return false;
	if (!ensureColorThemeFileExists(normalized, errorMessage))
		return false;
	if (!readTextFile(normalized, source))
		return setError(errorMessage, "Unable to read color theme file: " + normalized);
	if (!parseThemeSetupAssignments(source, assignments, errorMessage))
		return false;
	if (!parseWindowColorListLiteral(assignments["WINDOWCOLORS"], outValues, errorMessage))
		return false;
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

const char *colorSetupGroupTitle(MRColorSetupGroup group) {
	const ColorGroupDefinition *definition = findColorGroupDefinition(group);
	return definition != nullptr ? definition->title : "";
}

const char *colorSetupGroupKey(MRColorSetupGroup group) {
	const ColorGroupDefinition *definition = findColorGroupDefinition(group);
	return definition != nullptr ? definition->key : "";
}

const MRColorSetupItem *colorSetupGroupItems(MRColorSetupGroup group, std::size_t &count) {
	const ColorGroupDefinition *definition = findColorGroupDefinition(group);
	if (definition == nullptr) {
		count = 0;
		return nullptr;
	}
	count = definition->count;
	return definition->items;
}

bool applyConfiguredColorSetupValue(const std::string &key, const std::string &value,
                                    std::string *errorMessage) {
	const ColorGroupDefinition *definition = findColorGroupDefinitionByKey(key);
	MRColorSetupSettings configured = configuredColorSetupSettings();

	if (definition == nullptr)
		return setError(errorMessage, "Unknown color setup key.");

	switch (definition->group) {
		case MRColorSetupGroup::Window:
			if (!parseWindowColorListLiteral(value, configured.windowColors, errorMessage))
				return false;
			break;
		case MRColorSetupGroup::MenuDialog:
			if (!parseMenuDialogColorListLiteral(value, configured.menuDialogColors, errorMessage))
				return false;
			break;
		case MRColorSetupGroup::Help:
			if (!parseColorListLiteral(value, configured.helpColors, errorMessage))
				return false;
			break;
		case MRColorSetupGroup::Other:
			if (!parseOtherColorListLiteral(value, configured.otherColors, errorMessage))
				return false;
			break;
	}

	configuredColorSettings() = configured;
	configuredColorSettingsInitialized() = true;
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool configuredColorSlotOverride(unsigned char paletteIndex, unsigned char &value) {
	MRColorSetupSettings configured = configuredColorSetupSettings();
	unsigned char dialogFrame = 0;
	unsigned char dialogText = 0;
	unsigned char dialogBackground = 0;
	unsigned char dialogInactiveCluster = 0;
	unsigned char dialogInactiveElements = 0;

	// Turbo Vision menu hotkeys use two app slots:
	// - slot 4: shortcut text (normal item)
	// - slot 7: shortcut selection (selected item)
	// Our setup exposes a single "entry-hotkey" control (slot 4), so keep both in sync.
	if (paletteIndex == 7) {
		for (std::size_t i = 0; i < std::size(kMenuDialogColorItems); ++i)
			if (kMenuDialogColorItems[i].paletteIndex == 4) {
				value = configured.menuDialogColors[i];
				return true;
			}
	}

	for (std::size_t i = 0; i < std::size(kMenuDialogColorItems); ++i) {
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogFrame)
			dialogFrame = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogText)
			dialogText = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogBackground)
			dialogBackground = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogInactiveClusterGray)
			dialogInactiveCluster = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kMrPaletteDialogInactiveElements)
			dialogInactiveElements = configured.menuDialogColors[i];
	}

	switch (paletteIndex) {
		case kPaletteDialogFrame:
		case 34:
		case 65:
		case 66:
		case 97:
		case 98:
			value = dialogFrame;
			return true;
		case kPaletteDialogText:
		case 69:
		case 101:
			value = dialogText;
			return true;
		case kPaletteDialogBackground:
		case 64:
		case 96:
			value = dialogBackground;
			return true;
		case kPaletteDialogInactiveClusterGray:
		case kPaletteDialogInactiveClusterBlue:
		case kPaletteDialogInactiveClusterCyan:
			value = dialogInactiveElements != 0 ? dialogInactiveElements : dialogInactiveCluster;
			return true;
		case kMrPaletteDialogInactiveElements:
			value = dialogInactiveElements;
			return true;
		default:
			break;
	}

	for (std::size_t i = 0; i < std::size(kWindowColorItems); ++i)
		if (kWindowColorItems[i].paletteIndex == paletteIndex) {
			value = configured.windowColors[i];
			return true;
		}
	for (std::size_t i = 0; i < std::size(kMenuDialogColorItems); ++i)
		if (kMenuDialogColorItems[i].paletteIndex == paletteIndex) {
			value = configured.menuDialogColors[i];
			return true;
		}
	for (std::size_t i = 0; i < std::size(kHelpColorItems); ++i)
		if (kHelpColorItems[i].paletteIndex == paletteIndex) {
			value = configured.helpColors[i];
			return true;
		}
	for (std::size_t i = 0; i < std::size(kOtherColorItems); ++i)
		if (kOtherColorItems[i].paletteIndex == paletteIndex) {
			value = configured.otherColors[i];
			return true;
		}
	return false;
}

bool applyConfiguredEditSetupValue(const std::string &key, const std::string &value,
                                   std::string *errorMessage) {
	MREditSetupSettings current = configuredEditSetupSettings();

	if (!applyEditSetupValueInternal(current, key, value, errorMessage))
		return false;
	return setConfiguredEditSetupSettings(current, errorMessage);
}

std::string formatEditSetupBoolean(bool value) {
	return canonicalBooleanLiteral(value);
}

std::vector<std::string> configuredDefaultExtensionList() {
	return parseDefaultExtensions(configuredEditSetupSettings().defaultExtensions);
}

bool configuredDefaultInsertMode() {
	return upperAscii(configuredEditSetupSettings().defaultMode) != kDefaultModeOverwrite;
}

bool configuredTabExpandSetting() {
	return configuredEditSetupSettings().tabExpand;
}

int configuredTabSizeSetting() {
	return configuredEditSetupSettings().tabSize;
}

bool configuredBackupFilesSetting() {
	return configuredEditSetupSettings().backupFiles;
}

bool configuredPersistentBlocksSetting() {
	return configuredEditSetupSettings().persistentBlocks;
}

char configuredPageBreakCharacter() {
	return decodePageBreakLiteral(configuredEditSetupSettings().pageBreak);
}

bool setConfiguredLastFileDialogPath(const std::string &path, std::string *errorMessage) {
	std::string directory = normalizedDialogDirectoryFromPath(path);
	std::string fallback;

	if (!directory.empty())
		rememberedLoadDirectory() = directory;
	else {
		fallback = fallbackRememberedLoadDirectory();
		if (!fallback.empty())
			rememberedLoadDirectory() = fallback;
	}
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string configuredLastFileDialogPath() {
	return effectiveRememberedLoadDirectory();
}

std::string buildSettingsMacroSource(const MRSetupPaths &paths) {
	std::string settingsPath = normalizeConfiguredPathInput(paths.settingsMacroUri);
	std::string macroDir = normalizeConfiguredPathInput(paths.macroPath);
	std::string helpPath = normalizeConfiguredPathInput(paths.helpUri);
	std::string tempDir = normalizeConfiguredPathInput(paths.tempPath);
	std::string shellPath = normalizeConfiguredPathInput(paths.shellUri);
	std::string themePath = configuredColorThemeFile().empty() ? defaultColorThemePathForSettings(settingsPath)
	                                                          : configuredColorThemeFilePath();
	MREditSetupSettings edit = configuredEditSetupSettings();
	std::string source;
	std::size_t descriptorCount = 0;
	const MREditSettingDescriptor *descriptors = editSettingDescriptors(descriptorCount);

	themePath = normalizeConfiguredPathInput(themePath);

	source += "$MACRO MR_SETTINGS FROM EDIT;\n";
	source += "MRSETUP('" + std::string(kSettingsVersionKey) + "', '" +
	          escapeMrmacSingleQuotedLiteral(kCurrentSettingsVersion) + "');\n";
	source += "MRSETUP('SETTINGSPATH', '" + escapeMrmacSingleQuotedLiteral(settingsPath) + "');\n";
	source += "MRSETUP('MACROPATH', '" + escapeMrmacSingleQuotedLiteral(macroDir) + "');\n";
	source += "MRSETUP('HELPPATH', '" + escapeMrmacSingleQuotedLiteral(helpPath) + "');\n";
	source += "MRSETUP('TEMPDIR', '" + escapeMrmacSingleQuotedLiteral(tempDir) + "');\n";
	source += "MRSETUP('SHELLPATH', '" + escapeMrmacSingleQuotedLiteral(shellPath) + "');\n";
	source += "MRSETUP('LASTFILEDIALOGPATH', '" +
	          escapeMrmacSingleQuotedLiteral(configuredLastFileDialogPath()) + "');\n";
	source += "MRSETUP('DEFAULT_PROFILE_DESCRIPTION', '" +
	          escapeMrmacSingleQuotedLiteral(configuredDefaultProfileDescription()) + "');\n";
	source += "MRSETUP('PAGE_BREAK', '" + escapeMrmacSingleQuotedLiteral(edit.pageBreak) + "');\n";
	source += "MRSETUP('WORD_DELIMITERS', '" + escapeMrmacSingleQuotedLiteral(edit.wordDelimiters) + "');\n";
	source +=
	    "MRSETUP('DEFAULT_EXTENSIONS', '" + escapeMrmacSingleQuotedLiteral(edit.defaultExtensions) + "');\n";
	source += "MRSETUP('TRUNCATE_SPACES', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.truncateSpaces)) +
	          "');\n";
	source +=
	    "MRSETUP('EOF_CTRL_Z', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.eofCtrlZ)) +
	    "');\n";
	source += "MRSETUP('EOF_CR_LF', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.eofCrLf)) +
	          "');\n";
	source +=
	    "MRSETUP('TAB_EXPAND', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.tabExpand)) +
	    "');\n";
	source += "MRSETUP('TAB_SIZE', '" + std::to_string(edit.tabSize) + "');\n";
	source += "MRSETUP('RIGHT_MARGIN', '" + std::to_string(edit.rightMargin) + "');\n";
	source += "MRSETUP('WORD_WRAP', '" +
	          escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.wordWrap)) + "');\n";
	source += "MRSETUP('INDENT_STYLE', '" + escapeMrmacSingleQuotedLiteral(edit.indentStyle) + "');\n";
	source += "MRSETUP('FILE_TYPE', '" + escapeMrmacSingleQuotedLiteral(edit.fileType) + "');\n";
	source += "MRSETUP('BINARY_RECORD_LENGTH', '" + std::to_string(edit.binaryRecordLength) + "');\n";
	source += "MRSETUP('POST_LOAD_MACRO', '" + escapeMrmacSingleQuotedLiteral(edit.postLoadMacro) + "');\n";
	source += "MRSETUP('PRE_SAVE_MACRO', '" + escapeMrmacSingleQuotedLiteral(edit.preSaveMacro) + "');\n";
	source += "MRSETUP('DEFAULT_PATH', '" + escapeMrmacSingleQuotedLiteral(edit.defaultPath) + "');\n";
	source += "MRSETUP('FORMAT_LINE', '" + escapeMrmacSingleQuotedLiteral(edit.formatLine) + "');\n";
	source += "MRSETUP('BACKUP_METHOD', '" + escapeMrmacSingleQuotedLiteral(edit.backupMethod) + "');\n";
	source += "MRSETUP('BACKUP_FREQUENCY', '" + escapeMrmacSingleQuotedLiteral(edit.backupFrequency) + "');\n";
	source += "MRSETUP('BACKUP_EXTENSION', '" + escapeMrmacSingleQuotedLiteral(edit.backupExtension) + "');\n";
	source += "MRSETUP('BACKUP_DIRECTORY', '" + escapeMrmacSingleQuotedLiteral(edit.backupDirectory) + "');\n";
	source += "MRSETUP('AUTOSAVE_INACTIVITY_SECONDS', '" + std::to_string(edit.autosaveInactivitySeconds) + "');\n";
	source += "MRSETUP('AUTOSAVE_INTERVAL_SECONDS', '" + std::to_string(edit.autosaveIntervalSeconds) + "');\n";
	source += "MRSETUP('BACKUP_FILES', '" +
	          escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.backupFiles)) + "');\n";
	source += "MRSETUP('SHOW_EOF_MARKER', '" +
	          escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.showEofMarker)) + "');\n";
	source += "MRSETUP('SHOW_EOF_MARKER_EMOJI', '" +
	          escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.showEofMarkerEmoji)) + "');\n";
	source += "MRSETUP('SHOW_LINE_NUMBERS', '" +
	          escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.showLineNumbers)) + "');\n";
	source += "MRSETUP('LINE_NUM_ZERO_FILL', '" +
	          escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.lineNumZeroFill)) + "');\n";
	source += "MRSETUP('PERSISTENT_BLOCKS', '" +
	          escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.persistentBlocks)) + "');\n";
	source += "MRSETUP('CODE_FOLDING', '" +
	          escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.codeFolding)) + "');\n";
	source += "MRSETUP('COLUMN_BLOCK_MOVE', '" + escapeMrmacSingleQuotedLiteral(edit.columnBlockMove) + "');\n";
	source += "MRSETUP('DEFAULT_MODE', '" + escapeMrmacSingleQuotedLiteral(edit.defaultMode) + "');\n";
	source += "MRSETUP('CURSOR_STATUS_COLOR', '" + escapeMrmacSingleQuotedLiteral(edit.cursorStatusColor) + "');\n";
	source += "MRSETUP('" + std::string(kThemeSettingsKey) + "', '" +
	          escapeMrmacSingleQuotedLiteral(themePath) + "');\n";

	for (const auto & profile : configuredEditExtensionProfiles()) {
		source += "MRFEPROFILE('DEFINE', '" + escapeMrmacSingleQuotedLiteral(profile.id) +
		          "', '" + escapeMrmacSingleQuotedLiteral(profile.name) + "', '');\n";
		for (const std::string & ext : profile.extensions)
			source += "MRFEPROFILE('EXT', '" + escapeMrmacSingleQuotedLiteral(profile.id) + "', '" +
			          escapeMrmacSingleQuotedLiteral(ext) + "', '');\n";
		if (!profile.windowColorThemeUri.empty())
			source += "MRFEPROFILE('SET', '" + escapeMrmacSingleQuotedLiteral(profile.id) + "', '" +
			          std::string(kWindowColorThemeProfileKey) + "', '" +
			          escapeMrmacSingleQuotedLiteral(profile.windowColorThemeUri) + "');\n";
		for (std::size_t i = 0; i < descriptorCount; ++i)
			if (descriptors[i].profileSupported && (profile.overrides.mask & descriptors[i].overrideBit) != 0) {
				std::string value;

				if (std::string(descriptors[i].key) == "PAGE_BREAK")
					value = profile.overrides.values.pageBreak;
				else if (std::string(descriptors[i].key) == "WORD_DELIMITERS")
					value = profile.overrides.values.wordDelimiters;
				else if (std::string(descriptors[i].key) == "DEFAULT_EXTENSIONS")
					value = profile.overrides.values.defaultExtensions;
				else if (std::string(descriptors[i].key) == "TRUNCATE_SPACES")
					value = formatEditSetupBoolean(profile.overrides.values.truncateSpaces);
				else if (std::string(descriptors[i].key) == "EOF_CTRL_Z")
					value = formatEditSetupBoolean(profile.overrides.values.eofCtrlZ);
				else if (std::string(descriptors[i].key) == "EOF_CR_LF")
					value = formatEditSetupBoolean(profile.overrides.values.eofCrLf);
				else if (std::string(descriptors[i].key) == "TAB_EXPAND")
					value = formatEditSetupBoolean(profile.overrides.values.tabExpand);
				else if (std::string(descriptors[i].key) == "TAB_SIZE")
					value = std::to_string(profile.overrides.values.tabSize);
				else if (std::string(descriptors[i].key) == "RIGHT_MARGIN")
					value = std::to_string(profile.overrides.values.rightMargin);
				else if (std::string(descriptors[i].key) == "WORD_WRAP")
					value = formatEditSetupBoolean(profile.overrides.values.wordWrap);
				else if (std::string(descriptors[i].key) == "INDENT_STYLE")
					value = profile.overrides.values.indentStyle;
				else if (std::string(descriptors[i].key) == "FILE_TYPE")
					value = profile.overrides.values.fileType;
				else if (std::string(descriptors[i].key) == "BINARY_RECORD_LENGTH")
					value = std::to_string(profile.overrides.values.binaryRecordLength);
				else if (std::string(descriptors[i].key) == "POST_LOAD_MACRO")
					value = profile.overrides.values.postLoadMacro;
				else if (std::string(descriptors[i].key) == "PRE_SAVE_MACRO")
					value = profile.overrides.values.preSaveMacro;
				else if (std::string(descriptors[i].key) == "DEFAULT_PATH")
					value = profile.overrides.values.defaultPath;
				else if (std::string(descriptors[i].key) == "FORMAT_LINE")
					value = profile.overrides.values.formatLine;
				else if (std::string(descriptors[i].key) == "BACKUP_FILES")
					value = formatEditSetupBoolean(profile.overrides.values.backupFiles);
				else if (std::string(descriptors[i].key) == "SHOW_EOF_MARKER")
					value = formatEditSetupBoolean(profile.overrides.values.showEofMarker);
				else if (std::string(descriptors[i].key) == "SHOW_EOF_MARKER_EMOJI")
					value = formatEditSetupBoolean(profile.overrides.values.showEofMarkerEmoji);
				else if (std::string(descriptors[i].key) == "SHOW_LINE_NUMBERS")
					value = formatEditSetupBoolean(profile.overrides.values.showLineNumbers);
				else if (std::string(descriptors[i].key) == "LINE_NUM_ZERO_FILL")
					value = formatEditSetupBoolean(profile.overrides.values.lineNumZeroFill);
				else if (std::string(descriptors[i].key) == "PERSISTENT_BLOCKS")
					value = formatEditSetupBoolean(profile.overrides.values.persistentBlocks);
				else if (std::string(descriptors[i].key) == "CODE_FOLDING")
					value = formatEditSetupBoolean(profile.overrides.values.codeFolding);
				else if (std::string(descriptors[i].key) == "COLUMN_BLOCK_MOVE")
					value = profile.overrides.values.columnBlockMove;
				else if (std::string(descriptors[i].key) == "DEFAULT_MODE")
					value = profile.overrides.values.defaultMode;
				else if (std::string(descriptors[i].key) == "CURSOR_STATUS_COLOR")
					value = profile.overrides.values.cursorStatusColor;

				source += "MRFEPROFILE('SET', '" + escapeMrmacSingleQuotedLiteral(profile.id) + "', '" +
				          descriptors[i].key + "', '" + escapeMrmacSingleQuotedLiteral(value) + "');\n";
			}
	}
	source += "END_MACRO;\n";
	return source;
}

bool persistConfiguredSettingsSnapshot(std::string *errorMessage, MRSettingsWriteReport *report) {
	MRSetupPaths paths;
	std::string settingsPath = configuredSettingsMacroFilePath();
	std::string settingsDir = directoryPartOf(settingsPath);
	std::string source;
	std::string previousSource;

	paths.settingsMacroUri = settingsPath;
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();

	if (!validateSettingsMacroFilePath(settingsPath, errorMessage))
		return false;
	if (!ensureDirectoryTree(settingsDir, errorMessage))
		return false;
	static_cast<void>(readTextFile(settingsPath, previousSource));
	source = buildSettingsMacroSource(paths);
	if (!writeTextFile(settingsPath, source))
		return setError(errorMessage, "Unable to write settings macro file: " + settingsPath);
	populateSettingsWriteReport(settingsPath, previousSource, source, report);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool writeSettingsMacroFile(const MRSetupPaths &paths, std::string *errorMessage, MRSettingsWriteReport *report) {
	std::string settingsPath = normalizeConfiguredPathInput(paths.settingsMacroUri);
	std::string settingsDir = directoryPartOf(settingsPath);
	std::string themePath = configuredColorThemeFile().empty() ? defaultColorThemePathForSettings(settingsPath)
	                                                          : configuredColorThemeFilePath();
	std::string source;
	std::string previousSource;

	if (!validateSettingsMacroFilePath(settingsPath, errorMessage))
		return false;
	if (!ensureDirectoryTree(settingsDir, errorMessage))
		return false;
	if (!writeColorThemeFile(themePath, errorMessage))
		return false;
	if (!setConfiguredColorThemeFilePath(themePath, errorMessage))
		return false;

	static_cast<void>(readTextFile(settingsPath, previousSource));
	source = buildSettingsMacroSource(paths);
	if (!writeTextFile(settingsPath, source))
		return setError(errorMessage, "Unable to write settings macro file: " + settingsPath);
	populateSettingsWriteReport(settingsPath, previousSource, source, report);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool ensureSettingsMacroFileExists(const std::string &settingsMacroUri, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(settingsMacroUri);
	struct stat st;
	MRSetupPaths defaults;

	if (!validateSettingsMacroFilePath(normalized, errorMessage))
		return false;
	if (::stat(normalized.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode))
			return setError(errorMessage, "Settings macro URI must include a filename.");
		if (errorMessage != nullptr)
			errorMessage->clear();
		return true;
	}

	defaults = resolveSetupPathDefaults();
	defaults.settingsMacroUri = normalized;
	return writeSettingsMacroFile(defaults, errorMessage);
}

void initRememberedLoadDialogPath(char *buffer, std::size_t bufferSize, const char *pattern) {
	std::string initial;
	std::string dir = effectiveRememberedLoadDirectory();
	const char *safePattern = (pattern != nullptr && *pattern != '\0') ? pattern : "*.*";

	if (!dir.empty()) {
		initial = dir;
		if (initial.back() != '/')
			initial += '/';
		initial += safePattern;
	} else
		initial = safePattern;
	copyToBuffer(buffer, bufferSize, initial);
}

void rememberLoadDialogPath(const char *path) {
	std::string dir = normalizedDialogDirectoryFromPath(path != nullptr ? path : "");

	if (!dir.empty())
		rememberedLoadDirectory() = dir;
}

bool validateSettingsMacroFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	struct stat st;

	if (normalized.empty())
		return setError(errorMessage, "Empty settings macro URI.");
	if (::stat(normalized.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
		return setError(errorMessage, "Settings macro URI must include a filename.");
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool setConfiguredSettingsMacroFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (!validateSettingsMacroFilePath(path, errorMessage))
		return false;
	configuredSettingsMacroFile() = makeAbsolutePath(normalized);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string configuredSettingsMacroFilePath() {
	const std::string &configured = configuredSettingsMacroFile();
	if (!configured.empty())
		return makeAbsolutePath(configured);
	return resolveSetupPathDefaults().settingsMacroUri;
}

bool validateMacroDirectoryPath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (normalized.empty())
		return setError(errorMessage, "Empty macro path.");
	if (!isReadableDirectory(normalized))
		return setError(errorMessage, "Macro path is missing or not readable: " + normalized);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool setConfiguredMacroDirectoryPath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (!validateMacroDirectoryPath(path, errorMessage))
		return false;
	configuredMacroDirectory() = makeAbsolutePath(normalized);
	if (!isReadableDirectory(makeAbsolutePath(rememberedLoadDirectory())))
		rememberedLoadDirectory() = configuredMacroDirectory();
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string configuredMacroDirectoryPath() {
	const std::string &configured = configuredMacroDirectory();
	std::string absoluteConfigured = makeAbsolutePath(configured);
	if (!isReadableDirectory(absoluteConfigured))
		return std::string();
	return absoluteConfigured;
}

bool validateHelpFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (normalized.empty())
		return setError(errorMessage, "Empty help URI.");
	if (hasDirectorySeparator(normalized) && !isReadableFile(normalized))
		return setError(errorMessage, "Help URI is missing or not readable: " + normalized);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool setConfiguredHelpFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (!validateHelpFilePath(path, errorMessage))
		return false;
	configuredHelpFile() = makeAbsolutePath(normalized);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string configuredHelpFilePath() {
	const std::string &configured = configuredHelpFile();
	if (!configured.empty())
		return makeAbsolutePath(configured);
	return resolveSetupPathDefaults().helpUri;
}

bool validateTempDirectoryPath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (normalized.empty())
		return setError(errorMessage, "Empty temp path.");
	if (!isWritableDirectory(normalized))
		return setError(errorMessage, "Temp path is missing or not writable: " + normalized);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool setConfiguredTempDirectoryPath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (!validateTempDirectoryPath(path, errorMessage))
		return false;
	configuredTempDirectory() = makeAbsolutePath(normalized);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string configuredTempDirectoryPath() {
	const std::string &configured = configuredTempDirectory();
	std::string absoluteConfigured = makeAbsolutePath(configured);
	std::string builtIn = resolveSetupPathDefaults().tempPath;
	if (isWritableDirectory(absoluteConfigured))
		return absoluteConfigured;
	if (isWritableDirectory(builtIn))
		return builtIn;
	return "/tmp";
}

bool validateShellExecutablePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (normalized.empty())
		return setError(errorMessage, "Empty shell executable URI.");
	if (!isExecutableFile(normalized))
		return setError(errorMessage, "Shell executable URI is missing or not executable: " + normalized);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool setConfiguredShellExecutablePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (!validateShellExecutablePath(path, errorMessage))
		return false;
	configuredShellExecutable() = makeAbsolutePath(normalized);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string configuredShellExecutablePath() {
	const std::string &configured = configuredShellExecutable();
	std::string absoluteConfigured = makeAbsolutePath(configured);
	std::string builtIn = resolveSetupPathDefaults().shellUri;
	if (isExecutableFile(absoluteConfigured))
		return absoluteConfigured;
	if (isExecutableFile(builtIn))
		return builtIn;
	return "/bin/sh";
}

std::string defaultSettingsMacroFilePath() {
	return configuredSettingsMacroFilePath();
}

std::string defaultMacroDirectoryPath() {
	std::string configured = configuredMacroDirectoryPath();

	if (!configured.empty())
		return configured;
	return resolveSetupPathDefaults().macroPath;
}
