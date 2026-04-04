#include "MRDialogPaths.hpp"

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

MRColorSetupSettings &configuredColorSettings() {
	static MRColorSetupSettings value;
	return value;
}

bool &configuredColorSettingsInitialized() {
	static bool initialized = false;
	return initialized;
}

std::string trimAscii(const std::string &value) {
	std::size_t start = 0;
	std::size_t end = value.size();

	while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0)
		++start;
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
		--end;
	return value.substr(start, end - start);
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

std::string expandUserPath(const std::string &input) {
	std::string path = trimAscii(input);

	if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != nullptr && *home != '\0')
			return std::string(home) + path.substr(1);
	}
	return path;
}

bool isReadableDirectory(const std::string &path) {
	struct stat st;
	if (path.empty())
		return false;
	if (::stat(path.c_str(), &st) != 0)
		return false;
	if (!S_ISDIR(st.st_mode))
		return false;
	return ::access(path.c_str(), R_OK | X_OK) == 0;
}

bool isWritableDirectory(const std::string &path) {
	struct stat st;
	if (path.empty())
		return false;
	if (::stat(path.c_str(), &st) != 0)
		return false;
	if (!S_ISDIR(st.st_mode))
		return false;
	return ::access(path.c_str(), R_OK | W_OK | X_OK) == 0;
}

bool isReadableFile(const std::string &path) {
	struct stat st;
	if (path.empty())
		return false;
	if (::stat(path.c_str(), &st) != 0)
		return false;
	if (!S_ISREG(st.st_mode))
		return false;
	return ::access(path.c_str(), R_OK) == 0;
}

bool isExecutableFile(const std::string &path) {
	struct stat st;
	if (path.empty())
		return false;
	if (::stat(path.c_str(), &st) != 0)
		return false;
	if (!S_ISREG(st.st_mode))
		return false;
	return ::access(path.c_str(), X_OK) == 0;
}

std::string directoryPartOf(const std::string &path) {
	if (path.empty())
		return std::string();
	std::size_t pos = path.find_last_of('/');
	if (pos == std::string::npos)
		return std::string();
	if (pos == 0)
		return "/";
	return path.substr(0, pos);
}

bool hasDirectorySeparator(const std::string &path) {
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

bool isAbsolutePath(const std::string &path) {
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
	static const char *const names[] = {"TMPDIR", "TEMP", "TMP"};
	for (auto name : names) {
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

std::string appendFileName(const std::string &directory, const char *fileName) {
	std::string base = directory;

	if (fileName == nullptr || *fileName == '\0')
		return base;
	if (base.empty())
		return std::string(fileName);
	if (base.back() != '/')
		base.push_back('/');
	base += fileName;
	return base;
}

std::string appendPathSegment(const std::string &base, const char *segment) {
	std::string out = base;

	if (segment == nullptr || *segment == '\0')
		return out;
	if (out.empty())
		return std::string(segment);
	if (out.back() != '/')
		out.push_back('/');
	out += segment;
	return out;
}

std::string fileNamePartOf(const std::string &path) {
	std::size_t pos;
	if (path.empty())
		return std::string();
	pos = path.find_last_of('/');
	if (pos == std::string::npos)
		return path;
	return path.substr(pos + 1);
}

std::string defaultColorThemePathForSettings(const std::string &settingsPath) {
	std::string dir = directoryPartOf(makeAbsolutePath(settingsPath));
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
static const int kDefaultTabSize = 8;
static const int kMinTabSize = 1;
static const int kMaxTabSize = 32;
static const char *const kThemeSettingsKey = "COLORTHEMEURI";
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
    {"listbox selector", 58},
    {"inactive radio buttons and checkboxes", kPaletteDialogInactiveClusterGray},
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
};

static const ColorGroupDefinition kColorGroups[] = {
    {MRColorSetupGroup::Window, "WINDOW COLORS", "WINDOWCOLORS", kWindowColorItems,
     sizeof(kWindowColorItems) / sizeof(kWindowColorItems[0])},
    {MRColorSetupGroup::MenuDialog, "MENU / DIALOG COLORS", "MENUDIALOGCOLORS", kMenuDialogColorItems,
     sizeof(kMenuDialogColorItems) / sizeof(kMenuDialogColorItems[0])},
    {MRColorSetupGroup::Help, "HELP COLORS", "HELPCOLORS", kHelpColorItems,
     sizeof(kHelpColorItems) / sizeof(kHelpColorItems[0])},
    {MRColorSetupGroup::Other, "OTHER COLORS", "OTHERCOLORS", kOtherColorItems,
     sizeof(kOtherColorItems) / sizeof(kOtherColorItems[0])},
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
	static const unsigned char defaults[] = {
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
	if (paletteIndex == kMrPaletteLineNumbers)
		return defaults[9];
	if (paletteIndex == kMrPaletteEofMarker)
		return defaults[14];
	if (paletteIndex == 0 || paletteIndex >= sizeof(defaults) / sizeof(defaults[0]))
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
	// - 16: current format (..., listbox selector, inactive radio/checkbox, dialog frame/text/background)
	// - 15: previous format without inactive radio/checkbox color
	// - 14: legacy format (..., dialog frame, dialog background[old=dialog text])
	//       -> dialog background inherits legacy dialog frame color.
	// - 13: legacy format missing old dialog background(text)
	//       -> dialog text defaults, dialog background inherits legacy frame color.
	// - 12: legacy format missing legacy frame + legacy background(text)
	// - 11: legacy format missing listbox selector + legacy frame + legacy background(text)
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

	// Accept current 7-value format and legacy 10-value format.
	// Legacy format ended with shadow / shadow-character / background color entries,
	// which are no longer configurable in OTHERCOLORS.
	if (parsed.size() == MRColorSetupSettings::kOtherCount)
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = parsed[i];
	else if (parsed.size() == 10)
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = parsed[i];
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

bool parseTabSizeLiteral(const std::string &value, int &outValue, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty())
		return setError(errorMessage, "TABSIZE must be an integer between 1 and 32.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0')
		return setError(errorMessage, "TABSIZE must be an integer between 1 and 32.");
	if (parsed < kMinTabSize || parsed > kMaxTabSize)
		return setError(errorMessage, "TABSIZE must be between 1 and 32.");
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
			std::string ext = upperAscii(trimAscii(token));
			bool duplicate = false;

			token.clear();
			if (ext.empty())
				continue;
			while (!ext.empty() && ext[0] == '.')
				ext.erase(ext.begin());
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

} // namespace

std::string normalizeConfiguredPathInput(const std::string &input) {
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
	defaults.backupFiles = true;
	defaults.showEofMarker = false;
	defaults.showEofMarkerEmoji = true;
	defaults.showLineNumbers = false;
	defaults.lineNumZeroFill = false;
	defaults.persistentBlocks = true;
	defaults.columnBlockMove = kColumnBlockMoveDelete;
	defaults.defaultMode = kDefaultModeInsert;
	return defaults;
}

MRColorSetupSettings resolveColorSetupDefaults() {
	return defaultsFromColorGroups();
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
	std::string boolError;
	bool parsedBool = false;
	std::string pageBreak = normalizePageBreakLiteral(settings.pageBreak);
	std::string wordDelimiters = settings.wordDelimiters.empty() ? defaults.wordDelimiters
	                                                             : settings.wordDelimiters;
	std::string defaultExts = canonicalDefaultExtensionsLiteral(settings.defaultExtensions);
	std::string columnStyle = normalizeColumnBlockMove(settings.columnBlockMove);
	std::string defaultMode = normalizeDefaultMode(settings.defaultMode);

	if (wordDelimiters.empty())
		return setError(errorMessage, "WORDDELIMS may not be empty.");
	if (columnStyle.empty())
		return setError(errorMessage, "COLBLOCKMOVE must be DELETE_SPACE or LEAVE_SPACE.");
	if (defaultMode.empty())
		return setError(errorMessage, "DEFAULTMODE must be INSERT or OVERWRITE.");

	if (!parseBooleanLiteral(settings.truncateSpaces ? "true" : "false", parsedBool, &boolError))
		return setError(errorMessage, boolError);
	normalized.truncateSpaces = parsedBool;
	if (!parseBooleanLiteral(settings.eofCtrlZ ? "true" : "false", parsedBool, &boolError))
		return setError(errorMessage, boolError);
	normalized.eofCtrlZ = parsedBool;
	if (!parseBooleanLiteral(settings.eofCrLf ? "true" : "false", parsedBool, &boolError))
		return setError(errorMessage, boolError);
	normalized.eofCrLf = parsedBool;
	if (!parseBooleanLiteral(settings.tabExpand ? "true" : "false", parsedBool, &boolError))
		return setError(errorMessage, boolError);
	normalized.tabExpand = parsedBool;
	if (settings.tabSize < kMinTabSize || settings.tabSize > kMaxTabSize)
		return setError(errorMessage, "TABSIZE must be between 1 and 32.");
	if (!parseBooleanLiteral(settings.backupFiles ? "true" : "false", parsedBool, &boolError))
		return setError(errorMessage, boolError);
	normalized.backupFiles = parsedBool;
	if (!parseBooleanLiteral(settings.showEofMarker ? "true" : "false", parsedBool, &boolError))
		return setError(errorMessage, boolError);
	normalized.showEofMarker = parsedBool;
	if (!parseBooleanLiteral(settings.showEofMarkerEmoji ? "true" : "false", parsedBool, &boolError))
		return setError(errorMessage, boolError);
	normalized.showEofMarkerEmoji = parsedBool;
	if (!parseBooleanLiteral(settings.showLineNumbers ? "true" : "false", parsedBool, &boolError))
		return setError(errorMessage, boolError);
	normalized.showLineNumbers = parsedBool;
	if (!parseBooleanLiteral(settings.lineNumZeroFill ? "true" : "false", parsedBool, &boolError))
		return setError(errorMessage, boolError);
	normalized.lineNumZeroFill = parsedBool;
	if (!parseBooleanLiteral(settings.persistentBlocks ? "true" : "false", parsedBool, &boolError))
		return setError(errorMessage, boolError);
	normalized.persistentBlocks = parsedBool;

	normalized.pageBreak = pageBreak;
	normalized.wordDelimiters = wordDelimiters;
	normalized.defaultExtensions = defaultExts;
	normalized.tabSize = settings.tabSize;
	normalized.columnBlockMove = columnStyle;
	normalized.defaultMode = defaultMode;
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

	// Turbo Vision menu hotkeys use two app slots:
	// - slot 4: shortcut text (normal item)
	// - slot 7: shortcut selection (selected item)
	// Our setup exposes a single "entry-hotkey" control (slot 4), so keep both in sync.
	if (paletteIndex == 7) {
		for (std::size_t i = 0; i < sizeof(kMenuDialogColorItems) / sizeof(kMenuDialogColorItems[0]); ++i)
			if (kMenuDialogColorItems[i].paletteIndex == 4) {
				value = configured.menuDialogColors[i];
				return true;
			}
	}

	for (std::size_t i = 0; i < sizeof(kMenuDialogColorItems) / sizeof(kMenuDialogColorItems[0]); ++i) {
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogFrame)
			dialogFrame = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogText)
			dialogText = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogBackground)
			dialogBackground = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogInactiveClusterGray)
			dialogInactiveCluster = configured.menuDialogColors[i];
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
			value = dialogInactiveCluster;
			return true;
		default:
			break;
	}

	for (std::size_t i = 0; i < sizeof(kWindowColorItems) / sizeof(kWindowColorItems[0]); ++i)
		if (kWindowColorItems[i].paletteIndex == paletteIndex) {
			value = configured.windowColors[i];
			return true;
		}
	for (std::size_t i = 0; i < sizeof(kMenuDialogColorItems) / sizeof(kMenuDialogColorItems[0]); ++i)
		if (kMenuDialogColorItems[i].paletteIndex == paletteIndex) {
			value = configured.menuDialogColors[i];
			return true;
		}
	for (std::size_t i = 0; i < sizeof(kHelpColorItems) / sizeof(kHelpColorItems[0]); ++i)
		if (kHelpColorItems[i].paletteIndex == paletteIndex) {
			value = configured.helpColors[i];
			return true;
		}
	for (std::size_t i = 0; i < sizeof(kOtherColorItems) / sizeof(kOtherColorItems[0]); ++i)
		if (kOtherColorItems[i].paletteIndex == paletteIndex) {
			value = configured.otherColors[i];
			return true;
		}
	return false;
}

bool applyConfiguredEditSetupValue(const std::string &key, const std::string &value,
                                   std::string *errorMessage) {
	MREditSetupSettings current = configuredEditSetupSettings();
	std::string upperKeyName = upperAscii(trimAscii(key));
	bool boolValue = false;
	std::string normalized;

	if (upperKeyName == "PAGEBREAK")
		current.pageBreak = normalizePageBreakLiteral(value);
	else if (upperKeyName == "WORDDELIMS") {
		current.wordDelimiters = trimAscii(value);
		if (current.wordDelimiters.empty())
			current.wordDelimiters = resolveEditSetupDefaults().wordDelimiters;
	} else if (upperKeyName == "DEFAULTEXTS")
		current.defaultExtensions = canonicalDefaultExtensionsLiteral(value);
	else if (upperKeyName == "TRUNCSPACES") {
		if (!parseBooleanLiteral(value, boolValue, errorMessage))
			return false;
		current.truncateSpaces = boolValue;
	} else if (upperKeyName == "EOFCTRLZ") {
		if (!parseBooleanLiteral(value, boolValue, errorMessage))
			return false;
		current.eofCtrlZ = boolValue;
	} else if (upperKeyName == "EOFCRLF") {
		if (!parseBooleanLiteral(value, boolValue, errorMessage))
			return false;
		current.eofCrLf = boolValue;
	} else if (upperKeyName == "TABEXPAND") {
		if (!parseBooleanLiteral(value, boolValue, errorMessage))
			return false;
		current.tabExpand = boolValue;
	} else if (upperKeyName == "TABSIZE") {
		int tabSize = 0;
		if (!parseTabSizeLiteral(value, tabSize, errorMessage))
			return false;
		current.tabSize = tabSize;
	} else if (upperKeyName == "BACKUPFILES") {
		if (!parseBooleanLiteral(value, boolValue, errorMessage))
			return false;
		current.backupFiles = boolValue;
	} else if (upperKeyName == "SHOWEOFMARKER" || upperKeyName == "EOFMARKER") {
		if (!parseBooleanLiteral(value, boolValue, errorMessage))
			return false;
		current.showEofMarker = boolValue;
	} else if (upperKeyName == "SHOWEOFMARKEREMOJI" || upperKeyName == "EOFMARKEREMOJI" ||
	           upperKeyName == "SHOWEOFEMOJI") {
		if (!parseBooleanLiteral(value, boolValue, errorMessage))
			return false;
		current.showEofMarkerEmoji = boolValue;
	} else if (upperKeyName == "SHOWLINENUMBERS") {
		if (!parseBooleanLiteral(value, boolValue, errorMessage))
			return false;
		current.showLineNumbers = boolValue;
	} else if (upperKeyName == "LINENUMZEROFILL") {
		if (!parseBooleanLiteral(value, boolValue, errorMessage))
			return false;
		current.lineNumZeroFill = boolValue;
	} else if (upperKeyName == "PERSISTBLOCKS" || upperKeyName == "PERSISTENTBLOCKS") {
		if (!parseBooleanLiteral(value, boolValue, errorMessage))
			return false;
		current.persistentBlocks = boolValue;
	} else if (upperKeyName == "COLBLOCKMOVE") {
		normalized = normalizeColumnBlockMove(value);
		if (normalized.empty())
			return setError(errorMessage, "COLBLOCKMOVE must be DELETE_SPACE or LEAVE_SPACE.");
		current.columnBlockMove = normalized;
	} else if (upperKeyName == "DEFAULTMODE") {
		normalized = normalizeDefaultMode(value);
		if (normalized.empty())
			return setError(errorMessage, "DEFAULTMODE must be INSERT or OVERWRITE.");
		current.defaultMode = normalized;
	} else
		return setError(errorMessage, "Unknown edit setting key.");

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

	themePath = normalizeConfiguredPathInput(themePath);

	source += "$MACRO MR_SETTINGS FROM EDIT;\n";
	source += "MRSETUP('SETTINGSPATH', '" + escapeMrmacSingleQuotedLiteral(settingsPath) + "');\n";
	source += "MRSETUP('MACROPATH', '" + escapeMrmacSingleQuotedLiteral(macroDir) + "');\n";
	source += "MRSETUP('HELPPATH', '" + escapeMrmacSingleQuotedLiteral(helpPath) + "');\n";
	source += "MRSETUP('TEMPDIR', '" + escapeMrmacSingleQuotedLiteral(tempDir) + "');\n";
	source += "MRSETUP('SHELLPATH', '" + escapeMrmacSingleQuotedLiteral(shellPath) + "');\n";
	source += "MRSETUP('LASTFILEDIALOGPATH', '" +
	          escapeMrmacSingleQuotedLiteral(configuredLastFileDialogPath()) + "');\n";
	source += "MRSETUP('PAGEBREAK', '" + escapeMrmacSingleQuotedLiteral(edit.pageBreak) + "');\n";
	source += "MRSETUP('WORDDELIMS', '" + escapeMrmacSingleQuotedLiteral(edit.wordDelimiters) + "');\n";
	source +=
	    "MRSETUP('DEFAULTEXTS', '" + escapeMrmacSingleQuotedLiteral(edit.defaultExtensions) + "');\n";
	source += "MRSETUP('TRUNCSPACES', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.truncateSpaces)) +
	          "');\n";
	source +=
	    "MRSETUP('EOFCTRLZ', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.eofCtrlZ)) +
	    "');\n";
	source += "MRSETUP('EOFCRLF', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.eofCrLf)) +
	          "');\n";
	source +=
	    "MRSETUP('TABEXPAND', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.tabExpand)) +
	    "');\n";
	source += "MRSETUP('TABSIZE', '" + std::to_string(edit.tabSize) + "');\n";
	source += "MRSETUP('BACKUPFILES', '" +
	          escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.backupFiles)) + "');\n";
	source += "MRSETUP('SHOWEOFMARKER', '" +
	          escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.showEofMarker)) + "');\n";
	source += "MRSETUP('SHOWEOFMARKEREMOJI', '" +
	          escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.showEofMarkerEmoji)) + "');\n";
	source += "MRSETUP('SHOWLINENUMBERS', '" +
	          escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.showLineNumbers)) + "');\n";
	source += "MRSETUP('LINENUMZEROFILL', '" +
	          escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.lineNumZeroFill)) + "');\n";
	source += "MRSETUP('PERSISTENTBLOCKS', '" +
	          escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.persistentBlocks)) + "');\n";
	source += "MRSETUP('COLBLOCKMOVE', '" + escapeMrmacSingleQuotedLiteral(edit.columnBlockMove) + "');\n";
	source += "MRSETUP('DEFAULTMODE', '" + escapeMrmacSingleQuotedLiteral(edit.defaultMode) + "');\n";
	source += "MRSETUP('" + std::string(kThemeSettingsKey) + "', '" +
	          escapeMrmacSingleQuotedLiteral(themePath) + "');\n";
	source += "END_MACRO;\n";
	return source;
}

bool persistConfiguredSettingsSnapshot(std::string *errorMessage) {
	MRSetupPaths paths;
	std::string settingsPath = configuredSettingsMacroFilePath();
	std::string settingsDir = directoryPartOf(settingsPath);
	std::string source;

	paths.settingsMacroUri = settingsPath;
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();

	if (!validateSettingsMacroFilePath(settingsPath, errorMessage))
		return false;
	if (!ensureDirectoryTree(settingsDir, errorMessage))
		return false;
	source = buildSettingsMacroSource(paths);
	if (!writeTextFile(settingsPath, source))
		return setError(errorMessage, "Unable to write settings macro file: " + settingsPath);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

bool writeSettingsMacroFile(const MRSetupPaths &paths, std::string *errorMessage) {
	std::string settingsPath = normalizeConfiguredPathInput(paths.settingsMacroUri);
	std::string settingsDir = directoryPartOf(settingsPath);
	std::string themePath = configuredColorThemeFile().empty() ? defaultColorThemePathForSettings(settingsPath)
	                                                          : configuredColorThemeFilePath();
	std::string source;

	if (!validateSettingsMacroFilePath(settingsPath, errorMessage))
		return false;
	if (!ensureDirectoryTree(settingsDir, errorMessage))
		return false;
	if (!writeColorThemeFile(themePath, errorMessage))
		return false;
	if (!setConfiguredColorThemeFilePath(themePath, errorMessage))
		return false;

	source = buildSettingsMacroSource(paths);
	if (!writeTextFile(settingsPath, source))
		return setError(errorMessage, "Unable to write settings macro file: " + settingsPath);
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
