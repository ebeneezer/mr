#include "../../app/utils/MRStringUtils.hpp"
#include "MRSettingsHistory.hpp"
#include "MRSettingsRuntimeState.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

std::string pathFromEnvironment(const char *name) {
	const char *value = std::getenv(name);
	return value != nullptr && *value != '\0' ? makeAbsolutePath(normalizeDialogPath(expandUserPath(value).c_str())) : std::string();
}

std::string firstWritableDirectoryFromEnvironment() {
	for (const char *name : {"TMPDIR", "TEMP", "TMP"}) {
		std::string value = pathFromEnvironment(name);
		if (isWritableDirectory(value)) return value;
	}
	return std::string();
}

std::string shellFromUserDatabase() {
	struct passwd *entry = ::getpwuid(::geteuid());
	return entry != nullptr && entry->pw_shell != nullptr ? normalizeDialogPath(entry->pw_shell) : std::string();
}

std::string builtInTempDirectoryPath() {
	std::string env = firstWritableDirectoryFromEnvironment();
	std::string cwd;

	if (!env.empty()) return env;
	if (isWritableDirectory("/tmp")) return "/tmp";
	cwd = currentWorkingDirectory();
	if (!cwd.empty() && isWritableDirectory(cwd)) return cwd;
	return "/tmp";
}

std::string appendFileName(std::string_view directory, const char *fileName) {
	if (directory.empty()) return normalizeDialogPath(fileName);
	std::string result(directory);
	if (!result.empty() && result.back() != '/') result.push_back('/');
	result += fileName != nullptr ? fileName : "";
	return normalizeDialogPath(result.c_str());
}

std::string appendPathSegment(std::string_view base, const char *segment) {
	if (base.empty()) return normalizeDialogPath(segment);
	std::string result(base);
	if (!result.empty() && result.back() != '/') result.push_back('/');
	result += segment != nullptr ? segment : "";
	return normalizeDialogPath(result.c_str());
}

std::string executableDirectory() {
	char exePath[4096];
	ssize_t length = ::readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);

	if (length <= 0) return std::string();
	exePath[length] = '\0';
	return directoryPartOf(normalizeDialogPath(exePath));
}

std::string builtInShellExecutablePath() {
	std::string shell = pathFromEnvironment("SHELL");
	if (isExecutableFile(shell)) return shell;
	shell = shellFromUserDatabase();
	if (isExecutableFile(shell)) return shell;
	shell = "/bin/bash";
	if (isExecutableFile(shell)) return shell;
	if (isExecutableFile("/bin/sh")) return "/bin/sh";
	return shell;
}


} // namespace

std::string normalizeDialogPath(const char *path) {
	std::string result = path != nullptr ? path : "";
	for (char &ch : result)
		if (ch == '\\') ch = '/';
	if (!result.empty()) result = std::filesystem::path(result).lexically_normal().generic_string();
	return result;
}

std::string expandUserPath(std::string_view input) {
	std::string path = trimAscii(input);

	if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != nullptr && *home != '\0') return std::string(home) + path.substr(1);
	}
	return path;
}

bool isReadableDirectory(std::string_view path) {
	const std::string pathString(path);
	struct stat st;
	if (path.empty()) return false;
	if (::stat(pathString.c_str(), &st) != 0) return false;
	if (!S_ISDIR(st.st_mode)) return false;
	return ::access(pathString.c_str(), R_OK | X_OK) == 0;
}

bool isWritableDirectory(std::string_view path) {
	const std::string pathString(path);
	struct stat st;
	if (path.empty()) return false;
	if (::stat(pathString.c_str(), &st) != 0) return false;
	if (!S_ISDIR(st.st_mode)) return false;
	return ::access(pathString.c_str(), R_OK | W_OK | X_OK) == 0;
}

bool isReadableFile(std::string_view path) {
	const std::string pathString(path);
	struct stat st;
	if (path.empty()) return false;
	if (::stat(pathString.c_str(), &st) != 0) return false;
	if (!S_ISREG(st.st_mode)) return false;
	return ::access(pathString.c_str(), R_OK) == 0;
}

bool isExecutableFile(std::string_view path) {
	const std::string pathString(path);
	struct stat st;
	if (path.empty()) return false;
	if (::stat(pathString.c_str(), &st) != 0) return false;
	if (!S_ISREG(st.st_mode)) return false;
	return ::access(pathString.c_str(), X_OK) == 0;
}

bool isWritableRegularFile(std::string_view path) {
	const std::string pathString(path);
	struct stat st;
	if (path.empty()) return false;
	if (::stat(pathString.c_str(), &st) != 0) return false;
	if (!S_ISREG(st.st_mode)) return false;
	return ::access(pathString.c_str(), W_OK) == 0;
}

std::string directoryPartOf(std::string_view path) {
	if (path.empty()) return std::string();
	std::size_t pos = path.find_last_of('/');
	if (pos == std::string::npos) return std::string();
	if (pos == 0) return "/";
	return std::string(path.substr(0, pos));
}

bool hasDirectorySeparator(std::string_view path) {
	return path.find('/') != std::string::npos;
}

std::string normalizeAutoexecMacroEntry(std::string_view value) {
	return trimAscii(value);
}

bool validateAutoexecMacroEntry(const std::string &value, std::string *errorMessage) {
	const std::string normalized = normalizeDialogPath(normalizeAutoexecMacroEntry(value).c_str());
	std::filesystem::path relativePath;

	if (normalized.empty()) return setError(errorMessage, "Autoexec macro name must not be empty.");
	if (normalized.find(':') != std::string::npos) return setError(errorMessage, "Autoexec macro must be a relative path under MACROPATH.");
	relativePath = std::filesystem::path(normalized);
	if (relativePath.is_absolute()) return setError(errorMessage, "Autoexec macro must be a relative path under MACROPATH.");
	for (const std::filesystem::path &part : relativePath)
		if (part == "..") return setError(errorMessage, "Autoexec macro must stay under MACROPATH.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

void copyToBuffer(char *buffer, std::size_t bufferSize, const std::string &value) {
	if (buffer == nullptr || bufferSize == 0) return;
	std::memset(buffer, 0, bufferSize);
	std::strncpy(buffer, value.c_str(), bufferSize - 1);
	buffer[bufferSize - 1] = '\0';
}

std::string currentWorkingDirectory() {
	char cwd[4096];

	if (::getcwd(cwd, sizeof(cwd)) == nullptr) return std::string();
	return normalizeDialogPath(cwd);
}

bool isAbsolutePath(std::string_view path) {
	return !path.empty() && path[0] == '/';
}

std::string makeAbsolutePath(const std::string &path) {
	std::string normalized = normalizeDialogPath(path.c_str());
	std::string cwd;

	if (normalized.empty() || isAbsolutePath(normalized)) return normalized;
	cwd = currentWorkingDirectory();
	if (cwd.empty()) return normalized;
	if (cwd.back() != '/') cwd.push_back('/');
	cwd += normalized;
	return normalizeDialogPath(cwd.c_str());
}

std::string normalizedDialogDirectoryFromPath(const std::string &path) {
	std::string normalized = normalizeConfiguredPathInput(path);
	std::string dir;

	if (normalized.empty()) return std::string();
	if (isReadableDirectory(normalized)) return normalized;
	dir = directoryPartOf(normalized);
	if (dir.empty()) return std::string();
	dir = makeAbsolutePath(dir);
	return isReadableDirectory(dir) ? dir : std::string();
}

std::string fallbackRememberedLoadDirectory() {
	std::string macroDir = makeAbsolutePath(configuredMacroDirectory());
	std::string cwd = currentWorkingDirectory();

	if (isReadableDirectory(macroDir)) return macroDir;
	if (isReadableDirectory(cwd)) return cwd;
	return std::string();
}

std::string normalizeConfiguredPathInput(std::string_view input) {
	return makeAbsolutePath(normalizeDialogPath(expandUserPath(input).c_str()));
}

MRSetupPaths resolveSetupPathDefaults() {
	MRSetupPaths defaults;
	std::string xdgConfig = pathFromEnvironment("XDG_CONFIG_HOME");
	const char *homeEnv = std::getenv("HOME");
	std::string home = (homeEnv != nullptr && *homeEnv != '\0') ? makeAbsolutePath(normalizeDialogPath(homeEnv)) : std::string();
	std::string cwd = currentWorkingDirectory();
	std::string exeDir = makeAbsolutePath(executableDirectory());
	std::string candidate;
	std::string configBase;

	defaults.tempPath = builtInTempDirectoryPath();
	if (defaults.tempPath.empty()) defaults.tempPath = "/tmp";

	if (!xdgConfig.empty()) defaults.settingsMacroUri = appendFileName(appendPathSegment(xdgConfig, "mr"), "settings.mrmac");
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

	defaults.macroPath = appendPathSegment(directoryPartOf(defaults.settingsMacroUri), "macros");

	if (!cwd.empty()) {
		candidate = appendFileName(cwd, "mr.hlp");
		if (isReadableFile(candidate)) defaults.helpUri = candidate;
	}
	if (defaults.helpUri.empty() && !exeDir.empty()) {
		candidate = appendFileName(exeDir, "mr.hlp");
		if (isReadableFile(candidate)) defaults.helpUri = candidate;
	}
	if (defaults.helpUri.empty() && !cwd.empty()) defaults.helpUri = appendFileName(cwd, "mr.hlp");
	if (defaults.helpUri.empty() && !exeDir.empty()) defaults.helpUri = appendFileName(exeDir, "mr.hlp");
	if (defaults.helpUri.empty()) defaults.helpUri = appendFileName(defaults.tempPath, "mr.hlp");

	defaults.shellUri = builtInShellExecutablePath();
	if (defaults.shellUri.empty()) defaults.shellUri = "/bin/sh";

	defaults.settingsMacroUri = makeAbsolutePath(defaults.settingsMacroUri);
	defaults.macroPath = makeAbsolutePath(defaults.macroPath);
	defaults.helpUri = makeAbsolutePath(defaults.helpUri);
	defaults.tempPath = makeAbsolutePath(defaults.tempPath);
	defaults.shellUri = makeAbsolutePath(defaults.shellUri);
	return defaults;
}

void initRememberedLoadDialogPath(char *buffer, std::size_t bufferSize, const char *pattern) {
	initRememberedLoadDialogPath(MRDialogHistoryScope::General, buffer, bufferSize, pattern);
}

void rememberLoadDialogPath(const char *path) {
	rememberLoadDialogPath(MRDialogHistoryScope::General, path);
}

bool validateSettingsMacroFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	struct stat st;

	if (normalized.empty()) return setError(errorMessage, "Empty settings macro URI.");
	if (::stat(normalized.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return setError(errorMessage, "Settings macro URI must include a filename.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setConfiguredSettingsMacroFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	const std::string previousPath = configuredSettingsMacroFile();
	const MRScopedDialogHistoryState previousHistory = dialogHistoryState(MRDialogHistoryScope::SetupSettingsMacro);
	std::string configured;

	if (!validateSettingsMacroFilePath(path, errorMessage)) return false;
	configured = makeAbsolutePath(normalized);
	storeConfiguredSettingsMacroFile(configured);
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::SetupSettingsMacro, configured, nullptr));
	if (previousPath != configured || previousHistory != dialogHistoryState(MRDialogHistoryScope::SetupSettingsMacro)) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredSettingsMacroFilePath() {
	const std::string configured = configuredSettingsMacroFile();

	recordSettingsRuntimeRead();
	if (!configured.empty()) return makeAbsolutePath(configured);
	return resolveSetupPathDefaults().settingsMacroUri;
}

bool validateMacroDirectoryPath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (normalized.empty()) return setError(errorMessage, "Empty macro path.");
	if (!isReadableDirectory(normalized)) return setError(errorMessage, "Macro path is missing or not readable: " + normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setConfiguredMacroDirectoryPath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	const std::string previousPath = configuredMacroDirectory();
	const MRScopedDialogHistoryState previousHistory = dialogHistoryState(MRDialogHistoryScope::General);
	MRScopedDialogHistoryState generalDialogHistory;
	std::string configured;

	if (!validateMacroDirectoryPath(path, errorMessage)) return false;
	configured = makeAbsolutePath(normalized);
	storeConfiguredMacroDirectory(configured);
	generalDialogHistory = dialogHistoryState(MRDialogHistoryScope::General);
	if (generalDialogHistory.pathHistory.empty() && isReadableDirectory(configured)) {
		addHistoryEntry(generalDialogHistory.pathHistory, configured, configuredPathHistoryLimit());
		storeDialogHistoryState(MRDialogHistoryScope::General, generalDialogHistory);
	}
	if (previousPath != configured || previousHistory != dialogHistoryState(MRDialogHistoryScope::General)) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredMacroDirectoryPath() {
	const std::string configured = configuredMacroDirectory();
	std::string absoluteConfigured = makeAbsolutePath(configured);

	recordSettingsRuntimeRead();
	if (!isReadableDirectory(absoluteConfigured)) return std::string();
	return absoluteConfigured;
}

bool validateHelpFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (normalized.empty()) return setError(errorMessage, "Empty help URI.");
	if (hasDirectorySeparator(normalized) && !isReadableFile(normalized)) return setError(errorMessage, "Help URI is missing or not readable: " + normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setConfiguredHelpFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	const std::string previousPath = configuredHelpFile();
	const MRScopedDialogHistoryState previousHistory = dialogHistoryState(MRDialogHistoryScope::SetupHelpFile);
	std::string configured;

	if (!validateHelpFilePath(path, errorMessage)) return false;
	configured = makeAbsolutePath(normalized);
	storeConfiguredHelpFile(configured);
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::SetupHelpFile, configured, nullptr));
	if (previousPath != configured || previousHistory != dialogHistoryState(MRDialogHistoryScope::SetupHelpFile)) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredHelpFilePath() {
	const std::string configured = configuredHelpFile();

	recordSettingsRuntimeRead();
	if (!configured.empty()) return makeAbsolutePath(configured);
	return resolveSetupPathDefaults().helpUri;
}

bool validateTempDirectoryPath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (normalized.empty()) return setError(errorMessage, "Empty temp path.");
	if (!isWritableDirectory(normalized)) return setError(errorMessage, "Temp path is missing or not writable: " + normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setConfiguredTempDirectoryPath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	const std::string previousPath = configuredTempDirectory();
	std::string configured;

	if (!validateTempDirectoryPath(path, errorMessage)) return false;
	configured = makeAbsolutePath(normalized);
	storeConfiguredTempDirectory(configured);
	if (previousPath != configured) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredTempDirectoryPath() {
	const std::string configured = configuredTempDirectory();
	std::string absoluteConfigured = makeAbsolutePath(configured);
	std::string builtIn = resolveSetupPathDefaults().tempPath;

	recordSettingsRuntimeRead();
	if (isWritableDirectory(absoluteConfigured)) return absoluteConfigured;
	if (isWritableDirectory(builtIn)) return builtIn;
	return "/tmp";
}

bool validateShellExecutablePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (normalized.empty()) return setError(errorMessage, "Empty shell executable URI.");
	if (!isExecutableFile(normalized)) return setError(errorMessage, "Shell executable URI is missing or not executable: " + normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setConfiguredShellExecutablePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	const std::string previousPath = configuredShellExecutable();
	const MRScopedDialogHistoryState previousHistory = dialogHistoryState(MRDialogHistoryScope::SetupShellExecutable);
	std::string configured;

	if (!validateShellExecutablePath(path, errorMessage)) return false;
	configured = makeAbsolutePath(normalized);
	storeConfiguredShellExecutable(configured);
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::SetupShellExecutable, configured, nullptr));
	if (previousPath != configured || previousHistory != dialogHistoryState(MRDialogHistoryScope::SetupShellExecutable)) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredShellExecutablePath() {
	const std::string configured = configuredShellExecutable();
	std::string absoluteConfigured = makeAbsolutePath(configured);
	std::string builtIn = resolveSetupPathDefaults().shellUri;

	recordSettingsRuntimeRead();
	if (isExecutableFile(absoluteConfigured)) return absoluteConfigured;
	if (isExecutableFile(builtIn)) return builtIn;
	return "/bin/sh";
}

bool validateLogFilePath(const std::string &path, std::string *errorMessage) {
	const std::string normalized = normalizeConfiguredPathInput(path);
	std::string directory;
	struct stat st;

	if (normalized.empty()) return setError(errorMessage, "Empty log file URI.");
	if (isReadableDirectory(normalized)) return setError(errorMessage, "Log file URI points to a directory: " + normalized);
	if (::stat(normalized.c_str(), &st) == 0 && !S_ISREG(st.st_mode)) return setError(errorMessage, "Log file URI must point to a regular file: " + normalized);
	if (!isWritableRegularFile(normalized) && ::stat(normalized.c_str(), &st) == 0) return setError(errorMessage, "Log file is not writable: " + normalized);
	directory = directoryPartOf(normalized);
	if (directory.empty()) directory = currentWorkingDirectory();
	if (directory.empty() || !isWritableDirectory(makeAbsolutePath(directory))) return setError(errorMessage, "Log file path is missing or parent directory is not writable: " + normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setConfiguredLogFilePath(const std::string &path, std::string *errorMessage) {
	const std::string normalized = normalizeConfiguredPathInput(path);
	const std::string previousPath = configuredLogFile();
	const MRScopedDialogHistoryState previousHistory = dialogHistoryState(MRDialogHistoryScope::SetupLogFile);
	std::string configured;

	if (!validateLogFilePath(path, errorMessage)) return false;
	configured = makeAbsolutePath(normalized);
	storeConfiguredLogFile(configured);
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::SetupLogFile, configured, nullptr));
	if (previousPath != configured || previousHistory != dialogHistoryState(MRDialogHistoryScope::SetupLogFile)) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredLogFilePath() {
	const std::string configured = configuredLogFile();

	recordSettingsRuntimeRead();
	if (!configured.empty()) return makeAbsolutePath(configured);
	return appendFileName(configuredTempDirectoryPath(), "mr.log");
}

std::string defaultSettingsMacroFilePath() {
	return configuredSettingsMacroFilePath();
}

std::string defaultMacroDirectoryPath() {
	std::string configured = configuredMacroDirectoryPath();

	if (!configured.empty()) return configured;
	return resolveSetupPathDefaults().macroPath;
}
