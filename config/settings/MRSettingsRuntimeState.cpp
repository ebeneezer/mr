#include "../../app/utils/MRStringUtils.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "MRSettingsHistory.hpp"
#include "MRSettingsRuntimeState.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

bool g_windowManagerEnabled = true;
bool g_menulineMessagesEnabled = true;
MRSearchDialogOptions g_searchDialogOptions;
MRSarDialogOptions g_sarDialogOptions;
MRMultiSearchDialogOptions g_multiSearchDialogOptions;
MRMultiSarDialogOptions g_multiSarDialogOptions;
MRPdfExportSettings g_pdfExportSettings;
MRAcquireSettings g_acquireSettings;
MRLiveLogSettings g_liveLogSettings;
int g_virtualDesktops = 1;
bool g_cyclicVirtualDesktops = false;
MRCursorBehaviour g_cursorBehaviour = MRCursorBehaviour::BoundToText;
MRCompilerErrorMessagePlacement g_compilerErrorMessagePlacement = MRCompilerErrorMessagePlacement::RightMargin;
bool g_languageServerSpawnDaemon = true;
MRLanguageServerSidekickPlacement g_languageServerSidekickPlacement = MRLanguageServerSidekickPlacement::RightMargin;
int g_languageServerHoverDwellMs = kLanguageServerHoverDwellMsDefault;
int g_languageServerDocumentSyncDelayMs = kLanguageServerDocumentSyncDelayMsDefault;
int g_languageServerSignatureQuietMs = kLanguageServerSignatureQuietMsDefault;
MRLanguageServerChannelSettings g_languageServerChannelSettings;
MRScrollbarVisibility g_scrollbarVisibility = MRScrollbarVisibility::Smart;
bool g_trackCompilerWarnings = false;
bool g_trackCompilerNotes = false;
MRUiIndentStyle g_uiIndentStyle = MRUiIndentStyle::KandR;
std::string g_cursorPositionMarker = "R:C";
std::string g_fileCompareOriginalLeadingGutters = "L";
std::string g_fileCompareOriginalTrailingGutters = "M";
std::string g_fileCompareCompareLeadingGutters = "LD";
std::string g_fileCompareCompareTrailingGutters;
MRFileCompareStartConfiguration g_fileCompareStartConfiguration = MRFileCompareStartConfiguration::OriginalCompare;
bool g_fileCompareComparePanelReadOnly = true;
bool g_autosaveWorkspace = false;
bool g_runtimePreserveAutosavedWorkspace = false;
bool g_autoloadWorkspace = false;
MRLogHandling g_logHandling = MRLogHandling::Volatile;
std::map<std::string, std::string> g_autoexecMacroDiagnostics;

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

bool normalizeFileCompareGutters(const std::string &value, std::string &out, std::string *errorMessage) {
	out.clear();
	for (char ch : trimAscii(value)) {
		switch (static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(ch)))) {
			case 'M':
			case 'D':
			case 'L':
			case 'C':
				out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
				break;
			default:
				return setError(errorMessage, "FILE_COMPARE_*_GUTTERS may contain only M, D, L or C.");
		}
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

void normalizeAcquireCommandHistory(std::vector<std::string> &history) {
	constexpr std::size_t kAcquireHistoryLimit = 15;
	std::vector<std::string> normalized;

	normalized.reserve(history.size());
	for (const std::string &entry : history) {
		const std::string trimmed = trimAscii(entry);

		if (trimmed.empty()) continue;
		if (std::find(normalized.begin(), normalized.end(), trimmed) != normalized.end()) continue;
		normalized.push_back(trimmed);
		if (normalized.size() >= kAcquireHistoryLimit) break;
	}
	history = std::move(normalized);
}

void normalizeLiveLogSettings(MRLiveLogSettings &settings) {
	constexpr std::size_t kJournalAppTagHistoryLimit = 20;
	std::vector<std::string> normalizedHistory;

	normalizedHistory.reserve(settings.journalAppTagHistory.size());
	for (const std::string &entry : settings.journalAppTagHistory) {
		const std::string trimmed = trimAscii(entry);

		if (trimmed.empty()) continue;
		if (std::find(normalizedHistory.begin(), normalizedHistory.end(), trimmed) != normalizedHistory.end()) continue;
		normalizedHistory.push_back(trimmed);
		if (normalizedHistory.size() >= kJournalAppTagHistoryLimit) break;
	}
	settings.journalAppTagHistory = std::move(normalizedHistory);
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

bool normalizeCursorPositionMarker(const std::string &value, std::string &out, std::string *errorMessage) {
	std::string trimmed = trimAscii(value);
	int rCount = 0;
	int cCount = 0;

	if (trimmed.empty()) return setError(errorMessage, "must not be empty.");
	if (trimmed.size() > 10) return setError(errorMessage, "must be at most 10 characters.");
	out.clear();
	out.reserve(trimmed.size());
	for (char ch : trimmed) {
		if (ch == 'R') {
			++rCount;
			if (rCount > 1) return setError(errorMessage, "R placeholder may appear only once.");
			out.push_back(ch);
			continue;
		}
		if (ch == 'C') {
			++cCount;
			if (cCount > 1) return setError(errorMessage, "C placeholder may appear only once.");
			out.push_back(ch);
			continue;
		}
		if (std::isalnum(static_cast<unsigned char>(ch)) != 0) return setError(errorMessage, "must use only R, C and punctuation.");
		if (ch == '\n' || ch == '\r' || ch == '\t') return setError(errorMessage, "must not contain control characters.");
		out.push_back(ch);
	}
	if (rCount != 1 || cCount != 1) return setError(errorMessage, "must contain exactly one R and one C placeholder.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool validateBoundedMilliseconds(const char *key, int value, int minValue, int maxValue, std::string *errorMessage) {
	if (value < minValue || value > maxValue) {
		return setError(errorMessage, std::string(key) + " must be between " + std::to_string(minValue) + " and " + std::to_string(maxValue) + " milliseconds.");
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

} // namespace

std::vector<std::string> &configuredAutoexecMacroStorage() {
	static std::vector<std::string> value;
	return value;
}

bool &configuredSettingsDirtyFlag() {
	static bool value = false;
	return value;
}

void markConfiguredSettingsDirty() {
	configuredSettingsDirtyFlag() = true;
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

std::string &configuredLogFile() {
	static std::string value;
	return value;
}

std::string &configuredColorThemeFile() {
	static std::string value;
	return value;
}

std::string &configuredColorThemeDisplayNameValue() {
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

std::vector<MRCompilerProfile> &configuredCompilerProfilesValue() {
	static std::vector<MRCompilerProfile> value;
	return value;
}

std::vector<MRKeymapProfile> &configuredKeymapProfilesValue() {
	static std::vector<MRKeymapProfile> value;
	return value;
}

std::string &configuredDefaultProfileDescriptionValue() {
	static std::string value = "Global defaults";
	return value;
}

std::string &configuredKeymapFileValue() {
	static std::string value;
	return value;
}

std::string &configuredActiveKeymapProfileValue() {
	static std::string value;
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

	if (!cwd.empty()) {
		candidate = appendPathSegment(appendPathSegment(cwd, "mrmac"), "macros");
		if (isReadableDirectory(candidate)) defaults.macroPath = candidate;
	}
	if (defaults.macroPath.empty() && !exeDir.empty()) {
		candidate = appendPathSegment(appendPathSegment(exeDir, "mrmac"), "macros");
		if (isReadableDirectory(candidate)) defaults.macroPath = candidate;
	}
	if (defaults.macroPath.empty() && !cwd.empty() && isReadableDirectory(cwd)) defaults.macroPath = cwd;
	if (defaults.macroPath.empty()) defaults.macroPath = defaults.tempPath;

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

bool setConfiguredWindowManager(bool enabled, std::string *errorMessage) {
	if (g_windowManagerEnabled != enabled) markConfiguredSettingsDirty();
	g_windowManagerEnabled = enabled;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredWindowManager() {
	return g_windowManagerEnabled;
}

bool setConfiguredMenulineMessages(bool enabled, std::string *errorMessage) {
	const bool previous = g_menulineMessagesEnabled;

	if (!enabled) {
		mr::messageline::clearOwner(mr::messageline::Owner::HeroEvent);
		mr::messageline::clearOwner(mr::messageline::Owner::HeroEventFollowup);
		mr::messageline::clearOwner(mr::messageline::Owner::MacroMessage);
		mr::messageline::clearOwner(mr::messageline::Owner::MacroMarquee);
		mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
		mr::messageline::clearOwner(mr::messageline::Owner::DialogInteraction);
	}
	g_menulineMessagesEnabled = enabled;
	if (previous != g_menulineMessagesEnabled) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredMenulineMessages() {
	return g_menulineMessagesEnabled;
}

bool setConfiguredSearchDialogOptions(const MRSearchDialogOptions &options, std::string *errorMessage) {
	if (g_searchDialogOptions != options) markConfiguredSettingsDirty();
	g_searchDialogOptions = options;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRSearchDialogOptions configuredSearchDialogOptions() {
	return g_searchDialogOptions;
}

bool setConfiguredSarDialogOptions(const MRSarDialogOptions &options, std::string *errorMessage) {
	if (g_sarDialogOptions != options) markConfiguredSettingsDirty();
	g_sarDialogOptions = options;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRSarDialogOptions configuredSarDialogOptions() {
	return g_sarDialogOptions;
}

bool setConfiguredMultiSearchDialogOptions(const MRMultiSearchDialogOptions &options, std::string *errorMessage) {
	if (g_multiSearchDialogOptions != options) markConfiguredSettingsDirty();
	g_multiSearchDialogOptions = options;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRMultiSearchDialogOptions configuredMultiSearchDialogOptions() {
	return g_multiSearchDialogOptions;
}

bool setConfiguredMultiSarDialogOptions(const MRMultiSarDialogOptions &options, std::string *errorMessage) {
	if (g_multiSarDialogOptions != options) markConfiguredSettingsDirty();
	g_multiSarDialogOptions = options;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRMultiSarDialogOptions configuredMultiSarDialogOptions() {
	return g_multiSarDialogOptions;
}

bool setConfiguredPdfExportSettings(const MRPdfExportSettings &settings, std::string *errorMessage) {
	const MRPdfExportSettings previousSettings = g_pdfExportSettings;
	const MRScopedDialogHistoryState previousHistory = dialogHistoryState(MRDialogHistoryScope::PdfExport);

	g_pdfExportSettings = settings;
	if (!trimAscii(settings.outputPath).empty()) static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::PdfExport, settings.outputPath, nullptr));
	if (previousSettings != g_pdfExportSettings || previousHistory != dialogHistoryState(MRDialogHistoryScope::PdfExport)) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRPdfExportSettings configuredPdfExportSettings() {
	return g_pdfExportSettings;
}

bool setConfiguredAcquireSettings(const MRAcquireSettings &settings, std::string *errorMessage) {
	const MRAcquireSettings previousSettings = g_acquireSettings;
	MRAcquireSettings normalized = settings;

	normalized.commandLine = trimAscii(normalized.commandLine);
	normalizeAcquireCommandHistory(normalized.commandHistory);
	if (!normalized.commandLine.empty()) {
		normalized.commandHistory.erase(std::remove(normalized.commandHistory.begin(), normalized.commandHistory.end(), normalized.commandLine), normalized.commandHistory.end());
		normalized.commandHistory.insert(normalized.commandHistory.begin(), normalized.commandLine);
		normalizeAcquireCommandHistory(normalized.commandHistory);
	}
	g_acquireSettings = std::move(normalized);
	if (previousSettings != g_acquireSettings) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRAcquireSettings configuredAcquireSettings() {
	return g_acquireSettings;
}

bool setConfiguredLiveLogSettings(const MRLiveLogSettings &settings, std::string *errorMessage) {
	MRLiveLogSettings normalized = settings;

	normalizeLiveLogSettings(normalized);
	if (g_liveLogSettings != normalized) markConfiguredSettingsDirty();
	g_liveLogSettings = normalized;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRLiveLogSettings configuredLiveLogSettings() {
	return g_liveLogSettings;
}

bool setConfiguredVirtualDesktops(int count, std::string *errorMessage) {
	if (count < 1) count = 1;
	if (count > 9) count = 9;
	if (g_virtualDesktops != count) markConfiguredSettingsDirty();
	g_virtualDesktops = count;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

int configuredVirtualDesktops() {
	return g_virtualDesktops;
}

bool setConfiguredCyclicVirtualDesktops(bool enabled, std::string *errorMessage) {
	if (g_cyclicVirtualDesktops != enabled) markConfiguredSettingsDirty();
	g_cyclicVirtualDesktops = enabled;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredCyclicVirtualDesktops() {
	return g_cyclicVirtualDesktops;
}

bool setConfiguredCursorBehaviour(MRCursorBehaviour behaviour, std::string *errorMessage) {
	if (g_cursorBehaviour != behaviour) markConfiguredSettingsDirty();
	g_cursorBehaviour = behaviour;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRCursorBehaviour configuredCursorBehaviour() {
	return g_cursorBehaviour;
}

bool setConfiguredCompilerErrorMessagePlacement(MRCompilerErrorMessagePlacement placement, std::string *errorMessage) {
	if (g_compilerErrorMessagePlacement != placement) markConfiguredSettingsDirty();
	g_compilerErrorMessagePlacement = placement;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRCompilerErrorMessagePlacement configuredCompilerErrorMessagePlacement() {
	return g_compilerErrorMessagePlacement;
}

bool setConfiguredLanguageServerSpawnDaemon(bool enabled, std::string *errorMessage) {
	if (g_languageServerSpawnDaemon != enabled) markConfiguredSettingsDirty();
	g_languageServerSpawnDaemon = enabled;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredLanguageServerSpawnDaemon() {
	return g_languageServerSpawnDaemon;
}

bool setConfiguredLanguageServerSidekickPlacement(MRLanguageServerSidekickPlacement placement, std::string *errorMessage) {
	if (g_languageServerSidekickPlacement != placement) markConfiguredSettingsDirty();
	g_languageServerSidekickPlacement = placement;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRLanguageServerSidekickPlacement configuredLanguageServerSidekickPlacement() {
	return g_languageServerSidekickPlacement;
}

bool setConfiguredLanguageServerHoverDwellMs(int value, std::string *errorMessage) {
	if (!validateBoundedMilliseconds("LANGUAGE_SERVER_HOVER_DWELL_MS", value, kLanguageServerHoverDwellMsMin, kLanguageServerHoverDwellMsMax, errorMessage)) return false;
	if (g_languageServerHoverDwellMs != value) markConfiguredSettingsDirty();
	g_languageServerHoverDwellMs = value;
	return true;
}

int configuredLanguageServerHoverDwellMs() {
	return g_languageServerHoverDwellMs;
}

bool setConfiguredLanguageServerDocumentSyncDelayMs(int value, std::string *errorMessage) {
	if (!validateBoundedMilliseconds("LANGUAGE_SERVER_DOCUMENT_SYNC_DELAY_MS", value, kLanguageServerDocumentSyncDelayMsMin, kLanguageServerDocumentSyncDelayMsMax, errorMessage)) return false;
	if (g_languageServerDocumentSyncDelayMs != value) markConfiguredSettingsDirty();
	g_languageServerDocumentSyncDelayMs = value;
	return true;
}

int configuredLanguageServerDocumentSyncDelayMs() {
	return g_languageServerDocumentSyncDelayMs;
}

bool setConfiguredLanguageServerSignatureQuietMs(int value, std::string *errorMessage) {
	if (!validateBoundedMilliseconds("LANGUAGE_SERVER_SIGNATURE_QUIET_MS", value, kLanguageServerSignatureQuietMsMin, kLanguageServerSignatureQuietMsMax, errorMessage)) return false;
	if (g_languageServerSignatureQuietMs != value) markConfiguredSettingsDirty();
	g_languageServerSignatureQuietMs = value;
	return true;
}

int configuredLanguageServerSignatureQuietMs() {
	return g_languageServerSignatureQuietMs;
}

bool setConfiguredLanguageServerChannelSettings(const MRLanguageServerChannelSettings &settings, std::string *errorMessage) {
	if (!(g_languageServerChannelSettings == settings)) markConfiguredSettingsDirty();
	g_languageServerChannelSettings = settings;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRLanguageServerChannelSettings configuredLanguageServerChannelSettings() {
	return g_languageServerChannelSettings;
}

bool setConfiguredScrollbarVisibility(MRScrollbarVisibility visibility, std::string *errorMessage) {
	if (g_scrollbarVisibility != visibility) markConfiguredSettingsDirty();
	g_scrollbarVisibility = visibility;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRScrollbarVisibility configuredScrollbarVisibility() {
	return g_scrollbarVisibility;
}

bool setConfiguredTrackCompilerWarnings(bool enabled, std::string *errorMessage) {
	if (g_trackCompilerWarnings != enabled) markConfiguredSettingsDirty();
	g_trackCompilerWarnings = enabled;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredTrackCompilerWarnings() {
	return g_trackCompilerWarnings;
}

bool setConfiguredTrackCompilerNotes(bool enabled, std::string *errorMessage) {
	if (g_trackCompilerNotes != enabled) markConfiguredSettingsDirty();
	g_trackCompilerNotes = enabled;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredTrackCompilerNotes() {
	return g_trackCompilerNotes;
}

bool setConfiguredUiIndentStyle(MRUiIndentStyle style, std::string *errorMessage) {
	if (g_uiIndentStyle != style) markConfiguredSettingsDirty();
	g_uiIndentStyle = style;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRUiIndentStyle configuredUiIndentStyle() {
	return g_uiIndentStyle;
}

bool setConfiguredCursorPositionMarker(const std::string &value, std::string *errorMessage) {
	std::string normalized;

	if (!normalizeCursorPositionMarker(value, normalized, errorMessage)) return false;
	if (g_cursorPositionMarker != normalized) markConfiguredSettingsDirty();
	g_cursorPositionMarker = normalized;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredCursorPositionMarker() {
	return g_cursorPositionMarker;
}

bool setConfiguredFileCompareOriginalLeadingGutters(const std::string &value, std::string *errorMessage) {
	std::string normalized;

	if (!normalizeFileCompareGutters(value, normalized, errorMessage)) return false;
	if (g_fileCompareOriginalLeadingGutters != normalized) markConfiguredSettingsDirty();
	g_fileCompareOriginalLeadingGutters = normalized;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredFileCompareOriginalLeadingGutters() {
	return g_fileCompareOriginalLeadingGutters;
}

bool setConfiguredFileCompareOriginalTrailingGutters(const std::string &value, std::string *errorMessage) {
	std::string normalized;

	if (!normalizeFileCompareGutters(value, normalized, errorMessage)) return false;
	if (g_fileCompareOriginalTrailingGutters != normalized) markConfiguredSettingsDirty();
	g_fileCompareOriginalTrailingGutters = normalized;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredFileCompareOriginalTrailingGutters() {
	return g_fileCompareOriginalTrailingGutters;
}

bool setConfiguredFileCompareCompareLeadingGutters(const std::string &value, std::string *errorMessage) {
	std::string normalized;

	if (!normalizeFileCompareGutters(value, normalized, errorMessage)) return false;
	if (g_fileCompareCompareLeadingGutters != normalized) markConfiguredSettingsDirty();
	g_fileCompareCompareLeadingGutters = normalized;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredFileCompareCompareLeadingGutters() {
	return g_fileCompareCompareLeadingGutters;
}

bool setConfiguredFileCompareCompareTrailingGutters(const std::string &value, std::string *errorMessage) {
	std::string normalized;

	if (!normalizeFileCompareGutters(value, normalized, errorMessage)) return false;
	if (g_fileCompareCompareTrailingGutters != normalized) markConfiguredSettingsDirty();
	g_fileCompareCompareTrailingGutters = normalized;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredFileCompareCompareTrailingGutters() {
	return g_fileCompareCompareTrailingGutters;
}

bool setConfiguredFileCompareStartConfiguration(MRFileCompareStartConfiguration configuration, std::string *errorMessage) {
	if (g_fileCompareStartConfiguration != configuration) markConfiguredSettingsDirty();
	g_fileCompareStartConfiguration = configuration;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRFileCompareStartConfiguration configuredFileCompareStartConfiguration() {
	return g_fileCompareStartConfiguration;
}

bool setConfiguredFileCompareComparePanelReadOnly(bool enabled, std::string *errorMessage) {
	if (g_fileCompareComparePanelReadOnly != enabled) markConfiguredSettingsDirty();
	g_fileCompareComparePanelReadOnly = enabled;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredFileCompareComparePanelReadOnly() {
	return g_fileCompareComparePanelReadOnly;
}

bool setConfiguredAutosaveWorkspace(bool enabled, std::string *errorMessage) {
	if (g_autosaveWorkspace != enabled) markConfiguredSettingsDirty();
	g_autosaveWorkspace = enabled;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredAutosaveWorkspace() {
	return g_autosaveWorkspace;
}

void setRuntimePreserveAutosavedWorkspace(bool enabled) {
	g_runtimePreserveAutosavedWorkspace = enabled;
}

bool runtimePreserveAutosavedWorkspace() {
	return g_runtimePreserveAutosavedWorkspace;
}

bool setConfiguredAutoloadWorkspace(bool enabled, std::string *errorMessage) {
	if (g_autoloadWorkspace != enabled) markConfiguredSettingsDirty();
	g_autoloadWorkspace = enabled;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredAutoloadWorkspace() {
	return g_autoloadWorkspace;
}

bool setConfiguredLogHandling(MRLogHandling handling, std::string *errorMessage) {
	if (g_logHandling != handling) markConfiguredSettingsDirty();
	g_logHandling = handling;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRLogHandling configuredLogHandling() {
	return g_logHandling;
}

void configuredAutoexecMacroEntries(std::vector<std::string> &outValues) {
	outValues = configuredAutoexecMacroStorage();
}

bool setConfiguredAutoexecMacroEntries(const std::vector<std::string> &values, std::string *errorMessage) {
	std::vector<std::string> normalizedValues;
	const std::vector<std::string> previousValues = configuredAutoexecMacroStorage();

	for (const std::string &value : values) {
		const std::string normalized = normalizeAutoexecMacroEntry(value);
		if (!validateAutoexecMacroEntry(normalized, errorMessage)) return false;
		if (std::find(normalizedValues.begin(), normalizedValues.end(), normalized) == normalizedValues.end()) normalizedValues.push_back(normalized);
	}
	configuredAutoexecMacroStorage() = std::move(normalizedValues);
	if (previousValues != configuredAutoexecMacroStorage()) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool addConfiguredAutoexecMacroEntry(const std::string &value, std::string *errorMessage) {
	const std::string normalized = normalizeAutoexecMacroEntry(value);
	std::vector<std::string> values = configuredAutoexecMacroStorage();

	if (!validateAutoexecMacroEntry(normalized, errorMessage)) return false;
	if (std::find(values.begin(), values.end(), normalized) == values.end()) values.push_back(normalized);
	return setConfiguredAutoexecMacroEntries(values, errorMessage);
}

void clearConfiguredAutoexecMacroDiagnostics() {
	g_autoexecMacroDiagnostics.clear();
}

void rememberConfiguredAutoexecMacroDiagnostic(const std::string &fileName, const std::string &errorText) {
	std::string key = trimAscii(fileName);
	for (char &ch : key)
		ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
	if (key.empty()) return;
	g_autoexecMacroDiagnostics[key] = errorText;
}

bool configuredAutoexecMacroDiagnosticForFile(const std::string &fileName, std::string &errorText) {
	std::string key = trimAscii(fileName);
	for (char &ch : key)
		ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
	auto it = g_autoexecMacroDiagnostics.find(key);

	errorText.clear();
	if (it == g_autoexecMacroDiagnostics.end()) return false;
	errorText = it->second;
	return true;
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

	if (!validateSettingsMacroFilePath(path, errorMessage)) return false;
	configuredSettingsMacroFile() = makeAbsolutePath(normalized);
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::SetupSettingsMacro, configuredSettingsMacroFile(), nullptr));
	if (previousPath != configuredSettingsMacroFile() || previousHistory != dialogHistoryState(MRDialogHistoryScope::SetupSettingsMacro)) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredSettingsMacroFilePath() {
	const std::string &configured = configuredSettingsMacroFile();
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

	if (!validateMacroDirectoryPath(path, errorMessage)) return false;
	configuredMacroDirectory() = makeAbsolutePath(normalized);
	MRScopedDialogHistoryState &generalDialogHistory = dialogHistoryState(MRDialogHistoryScope::General);
	if (generalDialogHistory.pathHistory.empty() && isReadableDirectory(configuredMacroDirectory())) addHistoryEntry(generalDialogHistory.pathHistory, configuredMacroDirectory(), configuredPathHistoryLimit());
	if (previousPath != configuredMacroDirectory() || previousHistory != dialogHistoryState(MRDialogHistoryScope::General)) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredMacroDirectoryPath() {
	const std::string &configured = configuredMacroDirectory();
	std::string absoluteConfigured = makeAbsolutePath(configured);
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

	if (!validateHelpFilePath(path, errorMessage)) return false;
	configuredHelpFile() = makeAbsolutePath(normalized);
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::SetupHelpFile, configuredHelpFile(), nullptr));
	if (previousPath != configuredHelpFile() || previousHistory != dialogHistoryState(MRDialogHistoryScope::SetupHelpFile)) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredHelpFilePath() {
	const std::string &configured = configuredHelpFile();
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

	if (!validateTempDirectoryPath(path, errorMessage)) return false;
	configuredTempDirectory() = makeAbsolutePath(normalized);
	if (previousPath != configuredTempDirectory()) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredTempDirectoryPath() {
	const std::string &configured = configuredTempDirectory();
	std::string absoluteConfigured = makeAbsolutePath(configured);
	std::string builtIn = resolveSetupPathDefaults().tempPath;
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

	if (!validateShellExecutablePath(path, errorMessage)) return false;
	configuredShellExecutable() = makeAbsolutePath(normalized);
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::SetupShellExecutable, configuredShellExecutable(), nullptr));
	if (previousPath != configuredShellExecutable() || previousHistory != dialogHistoryState(MRDialogHistoryScope::SetupShellExecutable)) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredShellExecutablePath() {
	const std::string &configured = configuredShellExecutable();
	std::string absoluteConfigured = makeAbsolutePath(configured);
	std::string builtIn = resolveSetupPathDefaults().shellUri;
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

	if (!validateLogFilePath(path, errorMessage)) return false;
	configuredLogFile() = makeAbsolutePath(normalized);
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::SetupLogFile, configuredLogFile(), nullptr));
	if (previousPath != configuredLogFile() || previousHistory != dialogHistoryState(MRDialogHistoryScope::SetupLogFile)) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredLogFilePath() {
	const std::string &configured = configuredLogFile();
	std::string dir;

	if (!configured.empty()) return makeAbsolutePath(configured);
	dir = directoryPartOf(makeAbsolutePath(configuredSettingsMacroFilePath()));
	if (dir.empty() || !isWritableDirectory(dir)) dir = builtInTempDirectoryPath();
	return appendFileName(dir, "mr.log");
}

std::string defaultSettingsMacroFilePath() {
	return configuredSettingsMacroFilePath();
}

std::string defaultMacroDirectoryPath() {
	std::string configured = configuredMacroDirectoryPath();

	if (!configured.empty()) return configured;
	return resolveSetupPathDefaults().macroPath;
}
