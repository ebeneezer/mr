#include "MRDialogPaths.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <pwd.h>
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

MREditSetupSettings &configuredEditSettings() {
	static MREditSetupSettings value;
	return value;
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
	for (std::size_t i = 0; i < value.size(); ++i)
		value[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[i])));
	return value;
}

std::string normalizeDialogPath(const char *path) {
	std::string result = path != nullptr ? path : "";
	for (std::size_t i = 0; i < result.size(); ++i)
		if (result[i] == '\\')
			result[i] = '/';
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
	for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		std::string value = pathFromEnvironment(names[i]);
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

std::string escapeMrmacSingleQuotedLiteral(const std::string &value) {
	std::string out;
	out.reserve(value.size() + 8);
	for (std::size_t i = 0; i < value.size(); ++i) {
		char ch = value[i];
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
static const char *const kCursorVisibilityHidden = "HIDDEN";
static const char *const kCursorVisibilityNormal = "NORMAL";
static const char *const kCursorVisibilityProminent = "PROMINENT";

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

	for (std::size_t i = 0; i < key.size(); ++i) {
		char ch = key[i];
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

std::string normalizeCursorVisibility(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "HIDDEN" || key == "OFF" || key == "0")
		return kCursorVisibilityHidden;
	if (key == "NORMAL" || key == "1")
		return kCursorVisibilityNormal;
	if (key == "PROMINENT" || key == "VERY_VISIBLE" || key == "VERYVISIBLE" || key == "2")
		return kCursorVisibilityProminent;
	return std::string();
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
			for (std::size_t j = 0; j < out.size(); ++j)
				if (out[j] == ext) {
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
	defaults.columnBlockMove = kColumnBlockMoveDelete;
	defaults.defaultMode = kDefaultModeInsert;
	defaults.cursorVisibility = kCursorVisibilityNormal;
	return defaults;
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
	std::string cursorVisibility = normalizeCursorVisibility(settings.cursorVisibility);

	if (wordDelimiters.empty())
		return setError(errorMessage, "WORDDELIMS may not be empty.");
	if (columnStyle.empty())
		return setError(errorMessage, "COLBLOCKMOVE must be DELETE_SPACE or LEAVE_SPACE.");
	if (defaultMode.empty())
		return setError(errorMessage, "DEFAULTMODE must be INSERT or OVERWRITE.");
	if (cursorVisibility.empty())
		return setError(errorMessage, "CURSORVISIBILITY must be HIDDEN, NORMAL, or PROMINENT.");

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

	normalized.pageBreak = pageBreak;
	normalized.wordDelimiters = wordDelimiters;
	normalized.defaultExtensions = defaultExts;
	normalized.columnBlockMove = columnStyle;
	normalized.defaultMode = defaultMode;
	normalized.cursorVisibility = cursorVisibility;
	configuredEditSettings() = normalized;
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
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
	} else if (upperKeyName == "CURSORVISIBILITY") {
		normalized = normalizeCursorVisibility(value);
		if (normalized.empty())
			return setError(errorMessage, "CURSORVISIBILITY must be HIDDEN, NORMAL, or PROMINENT.");
		current.cursorVisibility = normalized;
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

char configuredPageBreakCharacter() {
	return decodePageBreakLiteral(configuredEditSetupSettings().pageBreak);
}

std::string configuredCursorVisibility() {
	std::string normalized = normalizeCursorVisibility(configuredEditSetupSettings().cursorVisibility);

	if (!normalized.empty())
		return normalized;
	return kCursorVisibilityNormal;
}

unsigned short configuredCursorTypeCode() {
	std::string cursorVisibility = configuredCursorVisibility();

	if (cursorVisibility == kCursorVisibilityHidden)
		return 0;
	if (cursorVisibility == kCursorVisibilityProminent)
		return 100;
	return 1;
}

std::string buildSettingsMacroSource(const MRSetupPaths &paths) {
	std::string settingsPath = normalizeConfiguredPathInput(paths.settingsMacroUri);
	std::string macroDir = normalizeConfiguredPathInput(paths.macroPath);
	std::string helpPath = normalizeConfiguredPathInput(paths.helpUri);
	std::string tempDir = normalizeConfiguredPathInput(paths.tempPath);
	std::string shellPath = normalizeConfiguredPathInput(paths.shellUri);
	MREditSetupSettings edit = configuredEditSetupSettings();
	std::string source;

	source += "$MACRO MR_SETTINGS FROM EDIT;\n";
	source += "MRSETUP('SETTINGSPATH', '" + escapeMrmacSingleQuotedLiteral(settingsPath) + "');\n";
	source += "MRSETUP('MACROPATH', '" + escapeMrmacSingleQuotedLiteral(macroDir) + "');\n";
	source += "MRSETUP('HELPPATH', '" + escapeMrmacSingleQuotedLiteral(helpPath) + "');\n";
	source += "MRSETUP('TEMPDIR', '" + escapeMrmacSingleQuotedLiteral(tempDir) + "');\n";
	source += "MRSETUP('SHELLPATH', '" + escapeMrmacSingleQuotedLiteral(shellPath) + "');\n";
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
	source += "MRSETUP('COLBLOCKMOVE', '" + escapeMrmacSingleQuotedLiteral(edit.columnBlockMove) + "');\n";
	source += "MRSETUP('DEFAULTMODE', '" + escapeMrmacSingleQuotedLiteral(edit.defaultMode) + "');\n";
	source +=
	    "MRSETUP('CURSORVISIBILITY', '" + escapeMrmacSingleQuotedLiteral(edit.cursorVisibility) + "');\n";
	source += "END_MACRO;\n";
	return source;
}

bool writeSettingsMacroFile(const MRSetupPaths &paths, std::string *errorMessage) {
	std::string settingsPath = normalizeConfiguredPathInput(paths.settingsMacroUri);
	std::string settingsDir = directoryPartOf(settingsPath);
	std::string source;

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
	const std::string &dir = rememberedLoadDirectory();
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
	std::string normalized = normalizeDialogPath(path);
	std::string dir = directoryPartOf(normalized);

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
