#include "../app/utils/MRFileIOUtils.hpp"
#include "../app/utils/MRStringUtils.hpp"
#include "../keymap/MRKeymapResolver.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRWindowSupport.hpp"
#include <tvision/tv.h>
#include "MRDialogPaths.hpp"
#include "MRSettingsLoader.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <pwd.h>
#include <regex>
#include <set>
#include <span>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {

std::string summarizeConfiguredKeymapsForLog(const std::vector<MRKeymapProfile> &profiles, std::string_view activeProfileName) {
	std::string text = "Keymap configured state: active='" + std::string(activeProfileName) + "' profiles=" + std::to_string(profiles.size());

	for (const MRKeymapProfile &profile : profiles)
		text += " [" + profile.name + ":" + std::to_string(profile.bindings.size()) + "]";
	return text;
}

struct MRDialogHistoryEntry {
	std::string value;
	long long epoch = 0;

	auto operator==(const MRDialogHistoryEntry &) const noexcept -> bool = default;
};

struct MRScopedDialogHistoryState {
	std::string lastPath;
	std::vector<MRDialogHistoryEntry> pathHistory;
	std::vector<MRDialogHistoryEntry> fileHistory;

	auto operator==(const MRScopedDialogHistoryState &) const noexcept -> bool = default;
};

struct MRDialogHistoryScopeSpec {
	MRDialogHistoryScope scope;
	const char *name;
};

constexpr int kHistoryLimitMin = 5;
constexpr int kHistoryLimitMax = 50;
constexpr int kHistoryLimitDefault = 15;

static bool g_windowManagerEnabled = true;
static bool g_menulineMessagesEnabled = true;
static MRSearchDialogOptions g_searchDialogOptions;
static MRSarDialogOptions g_sarDialogOptions;
static MRMultiSearchDialogOptions g_multiSearchDialogOptions;
static MRMultiSarDialogOptions g_multiSarDialogOptions;
static int g_virtualDesktops = 1;
static bool g_cyclicVirtualDesktops = false;
static MRCursorBehaviour g_cursorBehaviour = MRCursorBehaviour::BoundToText;
static std::string g_cursorPositionMarker = "R:C";
static bool g_autoloadWorkspace = false;
static MRLogHandling g_logHandling = MRLogHandling::Volatile;
static std::map<std::string, std::string> g_autoexecMacroDiagnostics;

constexpr std::array kDialogHistoryScopeSpecs{
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::General, "GENERAL"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::EditorSaveAs, "EDITOR_SAVE_AS"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::OpenFile, "OPEN_FILE"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::LoadFile, "LOAD_FILE"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::SaveLogAs, "SAVE_LOG_AS"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::BlockSave, "BLOCK_SAVE"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::BlockLoad, "BLOCK_LOAD"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::MacroFile, "MACRO_FILE"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::KeymapProfileLoad, "KEYMAP_PROFILE_LOAD"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::KeymapProfileSave, "KEYMAP_PROFILE_SAVE"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::WorkspaceLoad, "WORKSPACE_LOAD"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::WorkspaceSave, "WORKSPACE_SAVE"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::SetupSettingsMacro, "SETUP_SETTINGS_MACRO"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::SetupMacroDirectory, "SETUP_MACRO_DIRECTORY"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::SetupHelpFile, "SETUP_HELP_FILE"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::SetupTempDirectory, "SETUP_TEMP_DIRECTORY"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::SetupShellExecutable, "SETUP_SHELL_EXECUTABLE"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::SetupLogFile, "SETUP_LOG_FILE"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::SetupBackupDirectory, "SETUP_BACKUP_DIRECTORY"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::SetupThemeLoad, "SETUP_THEME_LOAD"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::SetupThemeSave, "SETUP_THEME_SAVE"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::ExtensionThemeFile, "EXTENSION_THEME_FILE"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::ExtensionPostLoadMacro, "EXTENSION_POST_LOAD_MACRO"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::ExtensionPreSaveMacro, "EXTENSION_PRE_SAVE_MACRO"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::ExtensionDefaultPath, "EXTENSION_DEFAULT_PATH"},
};

static_assert(kDialogHistoryScopeSpecs.size() == static_cast<std::size_t>(MRDialogHistoryScope::Count));

std::array<MRScopedDialogHistoryState, static_cast<std::size_t>(MRDialogHistoryScope::Count)> &configuredDialogHistoryStorage() {
	static std::array<MRScopedDialogHistoryState, static_cast<std::size_t>(MRDialogHistoryScope::Count)> value;
	return value;
}

std::size_t dialogHistoryScopeIndex(MRDialogHistoryScope scope) noexcept {
	return static_cast<std::size_t>(scope);
}

MRScopedDialogHistoryState &dialogHistoryState(MRDialogHistoryScope scope) {
	return configuredDialogHistoryStorage()[dialogHistoryScopeIndex(scope)];
}

const MRDialogHistoryScopeSpec *findDialogHistoryScopeSpec(MRDialogHistoryScope scope) noexcept {
	const std::size_t index = dialogHistoryScopeIndex(scope);
	return index < kDialogHistoryScopeSpecs.size() ? &kDialogHistoryScopeSpecs[index] : nullptr;
}

const MRDialogHistoryScopeSpec *findDialogHistoryScopeSpecByName(std::string_view name) noexcept {
	for (const MRDialogHistoryScopeSpec &spec : kDialogHistoryScopeSpecs)
		if (spec.name == upperAscii(std::string(name))) return &spec;
	return nullptr;
}

const char *dialogHistoryScopeName(MRDialogHistoryScope scope) noexcept {
	const MRDialogHistoryScopeSpec *spec = findDialogHistoryScopeSpec(scope);
	return spec != nullptr ? spec->name : "GENERAL";
}

std::vector<MRDialogHistoryEntry> &configuredMultiFilespecHistoryStorage() {
	static std::vector<MRDialogHistoryEntry> value;
	return value;
}

std::vector<MRDialogHistoryEntry> &configuredMultiPathHistoryStorage() {
	static std::vector<MRDialogHistoryEntry> value;
	return value;
}

std::vector<std::string> &configuredAutoexecMacroStorage() {
	static std::vector<std::string> value;
	return value;
}

std::string autoexecDiagnosticKey(const std::string &fileName) {
	std::string key = trimAscii(fileName);

	for (char &ch : key)
		ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
	return key;
}

int &configuredPathHistoryLimit() {
	static int value = kHistoryLimitDefault;
	return value;
}

int &configuredFileHistoryLimit() {
	static int value = kHistoryLimitDefault;
	return value;
}

long long &configuredHistoryEpochCounter() {
	static long long value = 0;
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

MREditSetupSettings &configuredEditSettings() {
	static MREditSetupSettings value;
	return value;
}

std::vector<MREditExtensionProfile> &configuredEditProfiles() {
	static std::vector<MREditExtensionProfile> value;
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
	static std::string value = "DEFAULT";
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
	for (char &i : result)
		if (i == '\\') i = '/';
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

[[nodiscard]] bool isReadableDirectory(std::string_view path) {
	const std::string pathString(path);
	struct stat st;
	if (path.empty()) return false;
	if (::stat(pathString.c_str(), &st) != 0) return false;
	if (!S_ISDIR(st.st_mode)) return false;
	return ::access(pathString.c_str(), R_OK | X_OK) == 0;
}

[[nodiscard]] bool isWritableDirectory(std::string_view path) {
	const std::string pathString(path);
	struct stat st;
	if (path.empty()) return false;
	if (::stat(pathString.c_str(), &st) != 0) return false;
	if (!S_ISDIR(st.st_mode)) return false;
	return ::access(pathString.c_str(), R_OK | W_OK | X_OK) == 0;
}

[[nodiscard]] bool isReadableFile(std::string_view path) {
	const std::string pathString(path);
	struct stat st;
	if (path.empty()) return false;
	if (::stat(pathString.c_str(), &st) != 0) return false;
	if (!S_ISREG(st.st_mode)) return false;
	return ::access(pathString.c_str(), R_OK) == 0;
}

[[nodiscard]] bool isExecutableFile(std::string_view path) {
	const std::string pathString(path);
	struct stat st;
	if (path.empty()) return false;
	if (::stat(pathString.c_str(), &st) != 0) return false;
	if (!S_ISREG(st.st_mode)) return false;
	return ::access(pathString.c_str(), X_OK) == 0;
}

[[nodiscard]] bool isWritableRegularFile(std::string_view path) {
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

[[nodiscard]] bool hasDirectorySeparator(std::string_view path) {
	return path.find('/') != std::string::npos;
}

std::string normalizeAutoexecMacroEntry(std::string_view value) {
	return trimAscii(value);
}

bool validateAutoexecMacroEntry(const std::string &value, std::string *errorMessage) {
	const std::string normalized = normalizeDialogPath(normalizeAutoexecMacroEntry(value).c_str());

	if (normalized.empty()) {
		if (errorMessage != nullptr) *errorMessage = "Autoexec macro name must not be empty.";
		return false;
	}
	if (hasDirectorySeparator(normalized) || normalized.find(':') != std::string::npos) {
		if (errorMessage != nullptr) *errorMessage = "Autoexec macro must be a file name under MACROPATH.";
		return false;
	}
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

[[nodiscard]] bool isAbsolutePath(std::string_view path) {
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

long long nextHistoryEpoch() {
	long long nowEpoch = static_cast<long long>(std::time(nullptr));
	long long &counter = configuredHistoryEpochCounter();
	counter = std::max(counter + 1, nowEpoch);
	return counter;
}

void trimHistoryToLimit(std::vector<MRDialogHistoryEntry> &entries, int limit) {
	if (limit < 0) limit = 0;
	if (entries.size() > static_cast<std::size_t>(limit)) entries.resize(static_cast<std::size_t>(limit));
}

void addHistoryEntry(std::vector<MRDialogHistoryEntry> &entries, const std::string &value, int limit) {
	if (value.empty()) return;
	entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const MRDialogHistoryEntry &entry) { return entry.value == value; }), entries.end());
	entries.insert(entries.begin(), MRDialogHistoryEntry{value, nextHistoryEpoch()});
	trimHistoryToLimit(entries, limit);
}

void addSerializedHistoryEntry(std::vector<MRDialogHistoryEntry> &entries, const std::string &value, int limit, bool normalizeAsPath) {
	const std::string prepared = normalizeAsPath ? normalizeConfiguredPathInput(value) : trimAscii(value);

	if (prepared.empty()) return;
	for (const MRDialogHistoryEntry &entry : entries)
		if (entry.value == prepared) return;
	entries.push_back(MRDialogHistoryEntry{prepared, nextHistoryEpoch()});
	trimHistoryToLimit(entries, limit);
}

std::string latestReadableHistoryPath(const std::vector<MRDialogHistoryEntry> &entries) {
	for (const MRDialogHistoryEntry &entry : entries) {
		const std::string normalized = makeAbsolutePath(entry.value);
		if (isReadableDirectory(normalized)) return normalized;
	}
	return std::string();
}

std::string latestReadableHistoryFileDirectory(const std::vector<MRDialogHistoryEntry> &entries) {
	for (const MRDialogHistoryEntry &entry : entries) {
		std::string dir = normalizedDialogDirectoryFromPath(entry.value);
		if (!dir.empty()) return dir;
	}
	return std::string();
}

std::string latestHistoryValue(const std::vector<MRDialogHistoryEntry> &entries) {
	for (const MRDialogHistoryEntry &entry : entries) {
		const std::string normalized = normalizeConfiguredPathInput(entry.value);
		if (!normalized.empty()) return normalized;
	}
	return std::string();
}

std::string effectiveRememberedLoadDirectory(MRDialogHistoryScope scope) {
	const MRScopedDialogHistoryState &state = dialogHistoryState(scope);
	std::string remembered = normalizeConfiguredPathInput(state.lastPath);
	if (!remembered.empty() && isReadableDirectory(remembered)) return remembered;
	remembered = latestReadableHistoryPath(state.pathHistory);
	if (!remembered.empty()) return remembered;
	remembered = latestReadableHistoryFileDirectory(state.fileHistory);
	if (!remembered.empty()) return remembered;
	return fallbackRememberedLoadDirectory();
}

bool setError(std::string *errorMessage, const std::string &message);
std::string canonicalEditProfileId(const std::string &value);
std::string canonicalEditProfileName(const std::string &value);
std::string canonicalWindowColorThemeUri(const std::string &value);
bool normalizeEditExtensionSelectorsInPlace(std::vector<std::string> &selectors, std::string *errorMessage);
bool normalizeEditProfileOverridesInPlace(MREditExtensionProfile &profile, std::string *errorMessage);
bool validateNormalizedEditProfiles(const std::vector<MREditExtensionProfile> &profiles, std::string *errorMessage);
struct ColorGroupDefinition;
const ColorGroupDefinition *findColorGroupDefinitionByKey(const std::string &key);
template <std::size_t N> bool parseColorListLiteral(const std::string &literal, std::array<unsigned char, N> &outValues, std::string *errorMessage);
bool parseWindowColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kWindowCount> &outValues, std::string *errorMessage);
bool parseMenuDialogColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kMenuDialogCount> &outValues, std::string *errorMessage);
bool parseOtherColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kOtherCount> &outValues, std::string *errorMessage);

struct MRSettingsSnapshot {
	struct DialogHistoryState {
		std::string lastPath;
		std::vector<std::string> pathHistory;
		std::vector<std::string> fileHistory;

		auto operator==(const DialogHistoryState &) const noexcept -> bool = default;
	};

	MRSetupPaths paths;
	bool windowManagerEnabled{true};
	bool menulineMessagesEnabled{true};
	MRSearchDialogOptions searchDialogOptions;
	MRSarDialogOptions sarDialogOptions;
	MRMultiSearchDialogOptions multiSearchDialogOptions;
	MRMultiSarDialogOptions multiSarDialogOptions;
	int virtualDesktops{1};
	bool cyclicVirtualDesktops{false};
	MRCursorBehaviour cursorBehaviour{MRCursorBehaviour::BoundToText};
	std::string cursorPositionMarker{"R:C"};
	bool autoloadWorkspace{false};
	MRLogHandling logHandling{MRLogHandling::Volatile};
	std::string logFilePath;
	std::vector<std::string> autoexecMacros;
	int maxPathHistory{15};
	int maxFileHistory{15};
	std::array<DialogHistoryState, static_cast<std::size_t>(MRDialogHistoryScope::Count)> dialogHistory;
	std::vector<std::string> multiFilespecHistory;
	std::vector<std::string> multiPathHistory;
	std::string defaultProfileDescription{"Global defaults"};
	MREditSetupSettings editSettings;
	MRColorSetupSettings colorSettings;
	std::string colorThemeFilePath;
	std::vector<MREditExtensionProfile> editProfiles;
	std::string keymapFilePath;
	std::vector<MRKeymapProfile> keymapProfiles;
	std::string activeKeymapProfile{"DEFAULT"};

	auto operator==(const MRSettingsSnapshot &) const noexcept -> bool = default;
};

constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

const char *keymapDiagnosticSeverityName(MRKeymapDiagnosticSeverity severity) noexcept {
	switch (severity) {
		case MRKeymapDiagnosticSeverity::Warning:
			return "warning";
		case MRKeymapDiagnosticSeverity::Error:
		default:
			return "error";
	}
}

std::string keymapDiagnosticIdentity(const MRKeymapDiagnostic &diagnostic) {
	return std::to_string(static_cast<unsigned>(diagnostic.kind)) + "|" + std::to_string(static_cast<unsigned>(diagnostic.severity)) + "|" + std::to_string(diagnostic.profileIndex) + "|" + std::to_string(diagnostic.bindingIndex) + "|" + diagnostic.message;
}

std::string describeKeymapDiagnostic(std::span<const MRKeymapProfile> profiles, const MRKeymapDiagnostic &diagnostic) {
	std::string text = diagnostic.message;

	if (diagnostic.profileIndex != kNoIndex && diagnostic.profileIndex < profiles.size()) {
		const MRKeymapProfile &profile = profiles[diagnostic.profileIndex];
		text += " profile='" + profile.name + "'";
		if (diagnostic.bindingIndex != kNoIndex && diagnostic.bindingIndex < profile.bindings.size()) {
			const MRKeymapBindingRecord &binding = profile.bindings[diagnostic.bindingIndex];
			text += " binding=" + std::to_string(diagnostic.bindingIndex + 1);
			text += " target='" + binding.target.target + "'";
			text += " sequence='" + binding.sequence.toString() + "'";
		}
	}
	return text;
}

std::string summarizeKeymapLoadForLog(const MRKeymapLoadResult &load) {
	std::string text = "Keymap bootstrap parse: active='" + load.activeProfileName + "' profiles=" + std::to_string(load.profiles.size()) + " diagnostics=" + std::to_string(load.diagnostics.size());

	for (const MRKeymapProfile &profile : load.profiles)
		text += " [" + profile.name + ":" + std::to_string(profile.bindings.size()) + "]";
	return text;
}

std::string summarizeKeymapDiagnosticsForMessageLine(std::span<const MRKeymapDiagnostic> diagnostics, std::string_view operation) {
	std::set<std::string> seen;
	std::size_t errorCount = 0;
	std::size_t warningCount = 0;

	for (const MRKeymapDiagnostic &diagnostic : diagnostics) {
		if (!seen.insert(keymapDiagnosticIdentity(diagnostic)).second) continue;
		if (diagnostic.severity == MRKeymapDiagnosticSeverity::Error) ++errorCount;
		else
			++warningCount;
	}
	if (errorCount == 0 && warningCount == 0) return std::string();
	if (errorCount == 0) return std::string(operation) + ": " + std::to_string(warningCount) + " warning(s); see log.";
	return std::string(operation) + ": removed " + std::to_string(errorCount) + " invalid key binding(s); see log.";
}

struct MRParsedSettingsAssignment {
	std::string key;
	std::string value;
};

struct MRParsedEditProfileDirective {
	std::string operation;
	std::string profileId;
	std::string arg3;
	std::string arg4;
};

struct MRParsedSettingsDocument {
	std::vector<MRParsedSettingsAssignment> assignments;
	std::vector<MRParsedEditProfileDirective> profileDirectives;
};

struct MRFlattenedEditProfile {
	std::string id;
	std::string name;
	std::vector<std::string> extensions;
	std::map<std::string, std::string> settings;
};

struct MRFlattenedSettingsDocument {
	std::map<std::string, std::string> globals;
	std::map<std::string, MRFlattenedEditProfile> profiles;
};

std::string unescapeMrmacSingleQuotedLiteral(const std::string &value) {
	std::string out;
	out.reserve(value.size());
	for (std::size_t i = 0; i < value.size(); ++i) {
		char ch = value[i];
		if (ch == '\'' && i + 1 < value.size() && value[i + 1] == '\'') {
			out.push_back('\'');
			++i;
		} else
			out.push_back(ch);
	}
	return out;
}

std::string joinStrings(const std::vector<std::string> &values, std::string_view separator) {
	std::string out;

	for (std::size_t i = 0; i < values.size(); ++i) {
		if (i != 0) out += separator;
		out += values[i];
	}
	return out;
}

MRParsedSettingsDocument parseSettingsDocument(std::string_view source, bool acceptLegacyFeProfileToken) {
	static const std::regex assignmentPattern("MRSETUP\\s*\\(\\s*'([^']+)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)", std::regex::icase);
	static const std::regex profilePattern("MRFEPROFILE\\s*\\(\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)", std::regex::icase);
	static const std::regex profilePatternWithLegacy("(?:MRFEPROFILE|MREDITPROFILE)\\s*\\(\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)", std::regex::icase);
	const std::regex &activeProfilePattern = acceptLegacyFeProfileToken ? profilePatternWithLegacy : profilePattern;
	MRParsedSettingsDocument document;
	std::smatch match;
	std::string remaining(source);

	while (std::regex_search(remaining, match, assignmentPattern)) {
		if (match.size() >= 3) {
			MRParsedSettingsAssignment assignment;
			assignment.key = upperAscii(trimAscii(match[1].str()));
			assignment.value = unescapeMrmacSingleQuotedLiteral(match[2].str());
			document.assignments.push_back(std::move(assignment));
		}
		remaining = match.suffix().str();
	}

	remaining.assign(source.data(), source.size());
	while (std::regex_search(remaining, match, activeProfilePattern)) {
		if (match.size() >= 5) {
			MRParsedEditProfileDirective directive;
			directive.operation = unescapeMrmacSingleQuotedLiteral(match[1].str());
			directive.profileId = unescapeMrmacSingleQuotedLiteral(match[2].str());
			directive.arg3 = unescapeMrmacSingleQuotedLiteral(match[3].str());
			directive.arg4 = unescapeMrmacSingleQuotedLiteral(match[4].str());
			document.profileDirectives.push_back(std::move(directive));
		}
		remaining = match.suffix().str();
	}
	return document;
}

std::size_t countLegacyFeProfileDirectives(std::string_view source) {
	static const std::regex legacyProfilePattern("MREDITPROFILE\\s*\\(\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)", std::regex::icase);
	std::smatch match;
	std::string remaining(source);
	std::size_t count = 0;

	while (std::regex_search(remaining, match, legacyProfilePattern)) {
		++count;
		remaining = match.suffix().str();
	}
	return count;
}

MRFlattenedSettingsDocument flattenSettingsDocument(const MRParsedSettingsDocument &document) {
	MRFlattenedSettingsDocument flattened;
	std::size_t pathHistoryIndex = 1;
	std::size_t fileHistoryIndex = 1;
	std::size_t dialogLastPathIndex = 1;
	std::size_t dialogPathHistoryIndex = 1;
	std::size_t dialogFileHistoryIndex = 1;
	std::size_t autoexecMacroIndex = 1;

	for (const MRParsedSettingsAssignment &assignment : document.assignments)
		if (assignment.key == "PATH_HISTORY") flattened.globals[assignment.key + "[" + std::to_string(pathHistoryIndex++) + "]"] = assignment.value;
		else if (assignment.key == "FILE_HISTORY")
			flattened.globals[assignment.key + "[" + std::to_string(fileHistoryIndex++) + "]"] = assignment.value;
		else if (assignment.key == "DIALOG_LAST_PATH")
			flattened.globals[assignment.key + "[" + std::to_string(dialogLastPathIndex++) + "]"] = assignment.value;
		else if (assignment.key == "DIALOG_PATH_HISTORY")
			flattened.globals[assignment.key + "[" + std::to_string(dialogPathHistoryIndex++) + "]"] = assignment.value;
		else if (assignment.key == "DIALOG_FILE_HISTORY")
			flattened.globals[assignment.key + "[" + std::to_string(dialogFileHistoryIndex++) + "]"] = assignment.value;
		else if (assignment.key == "AUTOEXEC_MACRO")
			flattened.globals[assignment.key + "[" + std::to_string(autoexecMacroIndex++) + "]"] = assignment.value;
		else
			flattened.globals[assignment.key] = assignment.value;

	for (const MRParsedEditProfileDirective &directive : document.profileDirectives) {
		const std::string op = upperAscii(trimAscii(directive.operation));
		const std::string profileId = trimAscii(directive.profileId);
		MRFlattenedEditProfile &profile = flattened.profiles[profileId];
		const std::string key = upperAscii(trimAscii(directive.arg3));

		profile.id = profileId;
		if (op == "DEFINE") {
			profile.name = trimAscii(directive.arg3);
			if (profile.name.empty()) profile.name = trimAscii(directive.arg4);
			if (profile.name.empty()) profile.name = profileId;
		} else if (op == "EXT") {
			profile.extensions.push_back(normalizeEditExtensionSelector(directive.arg3));
		} else if (op == "SET")
			profile.settings[key] = directive.arg4;
	}

	for (auto &entry : flattened.profiles) {
		auto &extensions = entry.second.extensions;
		std::sort(extensions.begin(), extensions.end());
		extensions.erase(std::unique(extensions.begin(), extensions.end()), extensions.end());
	}

	return flattened;
}

void appendChange(std::vector<MRSettingsChangeEntry> &changes, MRSettingsChangeEntry::Kind kind, const std::string &scope, const std::string &key, const std::string &oldValue, const std::string &newValue) {
	MRSettingsChangeEntry change;

	change.kind = kind;
	change.scope = scope;
	change.key = key;
	change.oldValue = oldValue;
	change.newValue = newValue;
	changes.push_back(std::move(change));
}

void diffFlatMap(const std::string &scope, const std::map<std::string, std::string> &beforeMap, const std::map<std::string, std::string> &afterMap, std::vector<MRSettingsChangeEntry> &changes) {
	std::set<std::string> keys;

	for (const auto &entry : beforeMap)
		keys.insert(entry.first);
	for (const auto &entry : afterMap)
		keys.insert(entry.first);

	for (const std::string &key : keys) {
		auto beforeIt = beforeMap.find(key);
		auto afterIt = afterMap.find(key);

		if (beforeIt == beforeMap.end()) {
			appendChange(changes, MRSettingsChangeEntry::Kind::Added, scope, key, std::string(), afterIt->second);
			continue;
		}
		if (afterIt == afterMap.end()) {
			appendChange(changes, MRSettingsChangeEntry::Kind::Removed, scope, key, beforeIt->second, std::string());
			continue;
		}
		if (beforeIt->second != afterIt->second) appendChange(changes, MRSettingsChangeEntry::Kind::Changed, scope, key, beforeIt->second, afterIt->second);
	}
}

void diffFlattenedDocuments(const MRFlattenedSettingsDocument &before, const MRFlattenedSettingsDocument &after, std::vector<MRSettingsChangeEntry> &changes) {
	diffFlatMap("settings", before.globals, after.globals, changes);

	std::set<std::string> profileIds;
	for (const auto &entry : before.profiles)
		profileIds.insert(entry.first);
	for (const auto &entry : after.profiles)
		profileIds.insert(entry.first);

	for (const std::string &profileId : profileIds) {
		auto beforeIt = before.profiles.find(profileId);
		auto afterIt = after.profiles.find(profileId);
		const std::string scope = "fe-profile '" + profileId + "'";
		std::map<std::string, std::string> beforeMap;
		std::map<std::string, std::string> afterMap;

		if (beforeIt != before.profiles.end()) {
			beforeMap["PROFILE_NAME"] = beforeIt->second.name;
			if (!beforeIt->second.extensions.empty()) beforeMap["EXTENSIONS"] = joinStrings(beforeIt->second.extensions, ", ");
			for (const auto &entry : beforeIt->second.settings)
				beforeMap[entry.first] = entry.second;
		}
		if (afterIt != after.profiles.end()) {
			afterMap["PROFILE_NAME"] = afterIt->second.name;
			if (!afterIt->second.extensions.empty()) afterMap["EXTENSIONS"] = joinStrings(afterIt->second.extensions, ", ");
			for (const auto &entry : afterIt->second.settings)
				afterMap[entry.first] = entry.second;
		}
		diffFlatMap(scope, beforeMap, afterMap, changes);
	}
}

void markFlag(MRSettingsLoadReport &report, MRSettingsLoadReport::Flag flag) {
	report.flags |= static_cast<unsigned int>(flag);
}

bool hasFlag(const MRSettingsLoadReport &report, MRSettingsLoadReport::Flag flag) {
	return (report.flags & static_cast<unsigned int>(flag)) != 0;
}

std::string quoteValue(const std::string &value) {
	return "'" + value + "'";
}

bool writeNormalizedBootstrapFiles(const MRSettingsSnapshot &snapshot, std::string_view previousSource, const std::string &canonicalSource, std::string *errorMessage) {
	const std::string settingsPath = normalizeConfiguredPathInput(snapshot.paths.settingsMacroUri);
	const std::string themePath = normalizeConfiguredPathInput(snapshot.colorThemeFilePath);
	const std::filesystem::path settingsDir = std::filesystem::path(settingsPath).parent_path();
	const std::filesystem::path themeDir = std::filesystem::path(themePath).parent_path();
	static const std::regex workspacePattern(R"(MRSETUP\s*\(\s*'WORKSPACE'\s*,\s*'((?:''|[^'])*)'\s*\)\s*;?)", std::regex_constants::ECMAScript | std::regex_constants::icase);
	std::string finalSource = canonicalSource;
	std::string previousText(previousSource);
	std::smatch match;
	std::string workspaceLines;
	std::error_code ec;

	while (std::regex_search(previousText, match, workspacePattern)) {
		workspaceLines += "MRSETUP('WORKSPACE', '";
		workspaceLines += match[1].str();
		workspaceLines += "');\n";
		previousText = match.suffix().str();
	}
	if (!workspaceLines.empty()) {
		const std::size_t endMacro = finalSource.rfind("END_MACRO;");

		if (endMacro != std::string::npos) finalSource.insert(endMacro, workspaceLines);
	}

	if (!validateSettingsMacroFilePath(settingsPath, errorMessage)) return false;
	if (!validateColorThemeFilePath(themePath, errorMessage)) return false;
	if (!settingsDir.empty()) {
		std::filesystem::create_directories(settingsDir, ec);
		if (ec) {
			if (errorMessage != nullptr) *errorMessage = "Unable to create settings directory: " + settingsDir.string();
			return false;
		}
	}
	if (!themeDir.empty()) {
		ec.clear();
		std::filesystem::create_directories(themeDir, ec);
		if (ec) {
			if (errorMessage != nullptr) *errorMessage = "Unable to create color theme directory: " + themeDir.string();
			return false;
		}
	}
	if (!writeTextFile(themePath, buildColorThemeMacroSource(snapshot.colorSettings))) {
		if (errorMessage != nullptr) *errorMessage = "Unable to write color theme file: " + themePath;
		return false;
	}
	if (!writeTextFile(settingsPath, finalSource)) {
		if (errorMessage != nullptr) *errorMessage = "Unable to write settings macro file: " + settingsPath;
		return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

void trimHistoryToLimit(std::vector<std::string> &entries, int limit) {
	if (limit < 0) limit = 0;
	if (entries.size() > static_cast<std::size_t>(limit)) entries.resize(static_cast<std::size_t>(limit));
}

void addHistoryEntry(std::vector<std::string> &entries, const std::string &value, int limit) {
	if (value.empty()) return;
	entries.erase(std::remove(entries.begin(), entries.end(), value), entries.end());
	entries.insert(entries.begin(), value);
	trimHistoryToLimit(entries, limit);
}

void addSerializedHistoryEntry(std::vector<std::string> &entries, const std::string &value, int limit, bool normalizeAsPath) {
	const std::string prepared = normalizeAsPath ? normalizeConfiguredPathInput(value) : trimAscii(value);

	if (prepared.empty()) return;
	for (const std::string &entry : entries)
		if (entry == prepared) return;
	entries.push_back(prepared);
	trimHistoryToLimit(entries, limit);
}

std::string fallbackRememberedLoadDirectory(const MRSettingsSnapshot &snapshot) {
	std::string macroDir = makeAbsolutePath(snapshot.paths.macroPath);
	std::string cwd = currentWorkingDirectory();

	if (isReadableDirectory(macroDir)) return macroDir;
	if (isReadableDirectory(cwd)) return cwd;
	return std::string();
}

bool setSnapshotScopedDialogLastPath(MRSettingsSnapshot &snapshot, MRDialogHistoryScope scope, const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	std::string directory;
	MRSettingsSnapshot::DialogHistoryState &state = snapshot.dialogHistory[dialogHistoryScopeIndex(scope)];

	if (!normalized.empty() && isReadableDirectory(normalized)) {
		state.lastPath = normalized;
		addHistoryEntry(state.pathHistory, normalized, snapshot.maxPathHistory);
	} else if (!normalized.empty()) {
		addHistoryEntry(state.fileHistory, normalized, snapshot.maxFileHistory);
		directory = normalizedDialogDirectoryFromPath(normalized);
		if (!directory.empty()) {
			state.lastPath = directory;
			addHistoryEntry(state.pathHistory, directory, snapshot.maxPathHistory);
		}
	} else if (state.lastPath.empty()) {
		directory = fallbackRememberedLoadDirectory(snapshot);
		if (!directory.empty()) {
			state.lastPath = directory;
			addHistoryEntry(state.pathHistory, directory, snapshot.maxPathHistory);
		}
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setSnapshotPathHistoryLimit(MRSettingsSnapshot &snapshot, int value, std::string *errorMessage) {
	if (value < kHistoryLimitMin || value > kHistoryLimitMax) return setError(errorMessage, "MAX_PATH_HISTORY must be within 5..50.");
	snapshot.maxPathHistory = value;
	for (MRSettingsSnapshot::DialogHistoryState &state : snapshot.dialogHistory)
		trimHistoryToLimit(state.pathHistory, value);
	trimHistoryToLimit(snapshot.multiPathHistory, value);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setSnapshotFileHistoryLimit(MRSettingsSnapshot &snapshot, int value, std::string *errorMessage) {
	if (value < kHistoryLimitMin || value > kHistoryLimitMax) return setError(errorMessage, "MAX_FILE_HISTORY must be within 5..50.");
	snapshot.maxFileHistory = value;
	for (MRSettingsSnapshot::DialogHistoryState &state : snapshot.dialogHistory)
		trimHistoryToLimit(state.fileHistory, value);
	trimHistoryToLimit(snapshot.multiFilespecHistory, value);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setSnapshotEditProfiles(MRSettingsSnapshot &snapshot, const std::vector<MREditExtensionProfile> &profiles, std::string *errorMessage) {
	std::vector<MREditExtensionProfile> normalized = profiles;

	for (MREditExtensionProfile &profile : normalized) {
		profile.id = canonicalEditProfileId(profile.id);
		profile.name = canonicalEditProfileName(profile.name);
		profile.windowColorThemeUri = canonicalWindowColorThemeUri(profile.windowColorThemeUri);
		if (!normalizeEditExtensionSelectorsInPlace(profile.extensions, errorMessage)) return false;
		if (!normalizeEditProfileOverridesInPlace(profile, errorMessage)) return false;
	}
	if (!validateNormalizedEditProfiles(normalized, errorMessage)) return false;
	snapshot.editProfiles = std::move(normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRSettingsSnapshot captureConfiguredSettingsSnapshot(const MRSetupPaths &paths) {
	MRSettingsSnapshot snapshot;

	snapshot.paths = paths;
	snapshot.windowManagerEnabled = configuredWindowManager();
	snapshot.menulineMessagesEnabled = configuredMenulineMessages();
	snapshot.searchDialogOptions = configuredSearchDialogOptions();
	snapshot.sarDialogOptions = configuredSarDialogOptions();
	snapshot.multiSearchDialogOptions = configuredMultiSearchDialogOptions();
	snapshot.multiSarDialogOptions = configuredMultiSarDialogOptions();
	snapshot.virtualDesktops = configuredVirtualDesktops();
	snapshot.cyclicVirtualDesktops = configuredCyclicVirtualDesktops();
	snapshot.cursorBehaviour = configuredCursorBehaviour();
	snapshot.cursorPositionMarker = configuredCursorPositionMarker();
	snapshot.autoloadWorkspace = configuredAutoloadWorkspace();
	snapshot.logHandling = configuredLogHandling();
	snapshot.logFilePath = configuredLogFilePath();
	configuredAutoexecMacroEntries(snapshot.autoexecMacros);
	snapshot.maxPathHistory = configuredMaxPathHistory();
	snapshot.maxFileHistory = configuredMaxFileHistory();
	snapshot.defaultProfileDescription = configuredDefaultProfileDescription();
	snapshot.editSettings = configuredEditSetupSettings();
	snapshot.colorSettings = configuredColorSetupSettings();
	snapshot.colorThemeFilePath = configuredColorThemeFilePath();
	snapshot.editProfiles = configuredEditExtensionProfiles();
	snapshot.keymapFilePath = configuredKeymapFilePath();
	snapshot.keymapProfiles = configuredKeymapProfiles();
	snapshot.activeKeymapProfile = configuredActiveKeymapProfile();

	for (std::size_t i = 0; i < static_cast<std::size_t>(MRDialogHistoryScope::Count); ++i) {
		const MRDialogHistoryScope scope = static_cast<MRDialogHistoryScope>(i);
		const MRScopedDialogHistoryState &configuredState = dialogHistoryState(scope);
		MRSettingsSnapshot::DialogHistoryState &state = snapshot.dialogHistory[i];

		state.lastPath = configuredState.lastPath;
		for (const MRDialogHistoryEntry &entry : configuredState.pathHistory)
			state.pathHistory.push_back(entry.value);
		for (const MRDialogHistoryEntry &entry : configuredState.fileHistory)
			state.fileHistory.push_back(entry.value);
	}
	configuredMultiFilespecHistoryEntries(snapshot.multiFilespecHistory);
	configuredMultiPathHistoryEntries(snapshot.multiPathHistory);
	return snapshot;
}

std::string executableDirectory() {
	char path[4096];
	ssize_t len = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
	std::size_t sep = 0;

	if (len <= 0) return std::string();
	path[len] = '\0';
	sep = std::string(path).find_last_of('/');
	if (sep == std::string::npos) return std::string();
	if (sep == 0) return "/";
	return std::string(path).substr(0, sep);
}

std::string pathFromEnvironment(const char *name) {
	const char *value = std::getenv(name);

	if (value == nullptr || *value == '\0') return std::string();
	return makeAbsolutePath(normalizeDialogPath(expandUserPath(value).c_str()));
}

std::string firstWritableDirectoryFromEnvironment() {
	static constexpr std::array<const char *, 3> names = {"TMPDIR", "TEMP", "TMP"};
	for (const char *name : names) {
		std::string value = pathFromEnvironment(name);
		if (isWritableDirectory(value)) return value;
	}
	return std::string();
}

std::string shellFromUserDatabase() {
	struct passwd *entry = ::getpwuid(::getuid());

	if (entry == nullptr || entry->pw_shell == nullptr || *entry->pw_shell == '\0') return std::string();
	return normalizeDialogPath(entry->pw_shell);
}

std::string builtInTempDirectoryPath();

std::string appendFileName(std::string_view directory, const char *fileName) {
	std::string base(directory);

	if (fileName == nullptr || *fileName == '\0') return base;
	if (base.empty()) return std::string(fileName);
	if (base.back() != '/') base.push_back('/');
	base += fileName;
	return base;
}

std::string appendPathSegment(std::string_view base, const char *segment) {
	std::string out(base);

	if (segment == nullptr || *segment == '\0') return out;
	if (out.empty()) return std::string(segment);
	if (out.back() != '/') out.push_back('/');
	out += segment;
	return out;
}

std::string fileNamePartOf(std::string_view path) {
	std::size_t pos;
	if (path.empty()) return std::string();
	pos = path.find_last_of('/');
	if (pos == std::string::npos) return std::string(path);
	return std::string(path.substr(pos + 1));
}

std::string defaultColorThemePathForSettings(std::string_view settingsPath) {
	std::string dir = directoryPartOf(makeAbsolutePath(std::string(settingsPath)));
	if (dir.empty()) dir = currentWorkingDirectory();
	if (dir.empty()) dir = "/tmp";
	return appendFileName(dir, "default-theme.mrmac");
}

std::string defaultLogFilePathForSettings(std::string_view settingsPath) {
	std::string dir = directoryPartOf(makeAbsolutePath(std::string(settingsPath)));
	if (dir.empty() || !isWritableDirectory(dir)) dir = builtInTempDirectoryPath();
	return appendFileName(dir, "mr.log");
}

std::string builtInTempDirectoryPath() {
	std::string envValue = firstWritableDirectoryFromEnvironment();
	std::string cwd;

	if (!envValue.empty()) return envValue;
	if (isWritableDirectory("/tmp")) return "/tmp";
	cwd = currentWorkingDirectory();
	if (!cwd.empty() && isWritableDirectory(cwd)) return cwd;
	return "/tmp";
}

std::string builtInShellExecutablePath() {
	std::string shell = pathFromEnvironment("SHELL");

	if (isExecutableFile(shell)) return shell;
	shell = shellFromUserDatabase();
	if (isExecutableFile(shell)) return shell;
	shell = pathFromEnvironment("COMSPEC");
	if (isExecutableFile(shell)) return shell;
	if (isExecutableFile("/bin/sh")) return "/bin/sh";
	return "/bin/sh";
}

bool ensureDirectoryTree(const std::string &directoryPath, std::string *errorMessage) {
	struct stat st;
	std::string parentPath;

	if (directoryPath.empty() || directoryPath == "." || directoryPath == "/") {
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (::stat(directoryPath.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		}
		if (errorMessage != nullptr) *errorMessage = "Path exists and is not a path container: " + directoryPath;
		return false;
	}
	parentPath = directoryPartOf(directoryPath);
	if (!parentPath.empty() && parentPath != directoryPath)
		if (!ensureDirectoryTree(parentPath, errorMessage)) return false;
	if (::mkdir(directoryPath.c_str(), 0755) != 0 && errno != EEXIST) {
		if (errorMessage != nullptr) *errorMessage = "Unable to create path container: " + directoryPath;
		return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

void accumulateSettingsChangeCounts(const std::vector<MRSettingsChangeEntry> &changes, std::size_t &addedCount, std::size_t &removedCount, std::size_t &changedCount) {
	addedCount = 0;
	removedCount = 0;
	changedCount = 0;
	for (const MRSettingsChangeEntry &change : changes)
		if (change.kind == MRSettingsChangeEntry::Kind::Added) ++addedCount;
		else if (change.kind == MRSettingsChangeEntry::Kind::Removed)
			++removedCount;
		else
			++changedCount;
}

void populateSettingsWriteReport(const std::string &settingsPath, const std::string &beforeSource, const std::string &afterSource, MRSettingsWriteReport *report) {
	std::vector<MRSettingsChangeEntry> changes;

	if (report == nullptr) return;
	*report = MRSettingsWriteReport();
	report->settingsPath = settingsPath;
	report->fileWritten = true;
	report->contentChanged = beforeSource != afterSource;
	if (!diffSettingsSources(beforeSource, afterSource, changes, nullptr)) return;
	accumulateSettingsChangeCounts(changes, report->addedCount, report->removedCount, report->changedCount);
	if (report->contentChanged && !changes.empty()) {
		report->logLines.push_back("settings.mrmac updated: " + std::to_string(report->changedCount) + " changed, " + std::to_string(report->addedCount) + " added, " + std::to_string(report->removedCount) + " removed.");
		for (const MRSettingsChangeEntry &change : changes)
			report->logLines.push_back(formatSettingsChangeForLog(change));
	} else if (report->contentChanged)
		report->logLines.push_back("settings.mrmac rewritten without semantic change.");
}

std::string escapeMrmacSingleQuotedLiteral(const std::string &value) {
	std::string out;
	out.reserve(value.size() + 8);
	for (char ch : value) {
		if (ch == '\'') out += "''";
		else
			out.push_back(ch);
	}
	return out;
}

bool setError(std::string *errorMessage, const std::string &message);

std::string escapePayloadQuotedString(std::string_view value) {
	std::string escaped;

	escaped.reserve(value.size() + 8);
	for (const char ch : value)
		switch (ch) {
			case '"':
				escaped += "\\\"";
				break;
			case '\\':
				escaped += "\\\\";
				break;
			case '\n':
				escaped += "\\n";
				break;
			case '\r':
				escaped += "\\r";
				break;
			case '\t':
				escaped += "\\t";
				break;
			default:
				escaped.push_back(ch);
				break;
		}
	return escaped;
}

bool isPayloadKeyStart(char ch) noexcept {
	const unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalpha(uch) != 0;
}

bool isPayloadKeyChar(char ch) noexcept {
	const unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalnum(uch) != 0 || ch == '_';
}

bool isPayloadSpace(char ch) noexcept {
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

struct ScopedHistoryPayloadMember {
	std::string key;
	std::string value;
};

bool parseScopedHistoryPayloadMembers(std::string_view payload, std::vector<ScopedHistoryPayloadMember> &members, std::string *errorMessage) {
	std::size_t pos = 0;

	while (pos < payload.size()) {
		while (pos < payload.size() && isPayloadSpace(payload[pos]))
			++pos;
		if (pos >= payload.size()) break;
		if (!isPayloadKeyStart(payload[pos])) return setError(errorMessage, "Dialog history payload syntax error.");

		const std::size_t keyStart = pos;
		++pos;
		while (pos < payload.size() && isPayloadKeyChar(payload[pos]))
			++pos;
		std::string key = upperAscii(std::string(payload.substr(keyStart, pos - keyStart)));

		if (pos >= payload.size() || payload[pos] != '=') return setError(errorMessage, "Dialog history payload is missing '='.");
		++pos;
		if (pos >= payload.size() || payload[pos] != '"') return setError(errorMessage, "Dialog history payload expects quoted values.");
		++pos;

		std::string value;
		while (pos < payload.size()) {
			const char ch = payload[pos++];
			if (ch == '"') break;
			if (ch == '\\') {
				if (pos >= payload.size()) return setError(errorMessage, "Dialog history payload has a dangling escape.");
				switch (const char escaped = payload[pos++]) {
					case '"':
					case '\\':
						value.push_back(escaped);
						break;
					case 'n':
						value.push_back('\n');
						break;
					case 'r':
						value.push_back('\r');
						break;
					case 't':
						value.push_back('\t');
						break;
					default:
						return setError(errorMessage, "Dialog history payload has an unsupported escape.");
				}
				continue;
			}
			value.push_back(ch);
		}
		if (pos > payload.size() || payload[pos - 1] != '"') return setError(errorMessage, "Dialog history payload has an unterminated value.");

		members.push_back({std::move(key), std::move(value)});
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

const ScopedHistoryPayloadMember *findScopedHistoryPayloadMember(const std::vector<ScopedHistoryPayloadMember> &members, std::string_view key) noexcept {
	for (const ScopedHistoryPayloadMember &member : members)
		if (member.key == key) return &member;
	return nullptr;
}

bool parseScopedHistoryPayload(std::string_view payload, const char *valueMemberName, MRDialogHistoryScope &scopeOut, std::string &valueOut, std::string *errorMessage) {
	std::vector<ScopedHistoryPayloadMember> members;
	const ScopedHistoryPayloadMember *scopeMember = nullptr;
	const ScopedHistoryPayloadMember *valueMember = nullptr;
	const MRDialogHistoryScopeSpec *scopeSpec = nullptr;

	if (!parseScopedHistoryPayloadMembers(payload, members, errorMessage)) return false;
	scopeMember = findScopedHistoryPayloadMember(members, "SCOPE");
	valueMember = findScopedHistoryPayloadMember(members, upperAscii(std::string(valueMemberName)));
	if (scopeMember == nullptr || valueMember == nullptr) return setError(errorMessage, "Dialog history payload requires scope and value members.");
	scopeSpec = findDialogHistoryScopeSpecByName(scopeMember->value);
	if (scopeSpec == nullptr) return setError(errorMessage, "Dialog history payload references an unknown scope.");
	scopeOut = scopeSpec->scope;
	valueOut = valueMember->value;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string serializeScopedHistoryRecord(std::string_view key, MRDialogHistoryScope scope, std::string_view valueMemberName, std::string_view value) {
	std::string payload = "scope=\"";
	payload += escapePayloadQuotedString(dialogHistoryScopeName(scope));
	payload += "\" ";
	payload += valueMemberName;
	payload += "=\"";
	payload += escapePayloadQuotedString(value);
	payload += '"';
	return "MRSETUP('" + std::string(key) + "', '" + escapeMrmacSingleQuotedLiteral(payload) + "');\n";
}

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

static const char *const kDefaultPageBreakLiteral = "\\f";
static const char *const kDefaultWordDelimiters = ".()'\\\",#$012%^&*+-/[]?";
static const char *const kDefaultDefaultExtensions = "PAS;ASM;BAT;TXT;DO";
static const char *const kColumnBlockMoveDelete = "DELETE_SPACE";
static const char *const kColumnBlockMoveLeave = "LEAVE_SPACE";
static const char *const kDefaultModeInsert = "INSERT";
static const char *const kDefaultModeOverwrite = "OVERWRITE";
static const char *const kMiniMapPositionOff = "OFF";
static const char *const kMiniMapPositionLeading = "LEADING";
static const char *const kMiniMapPositionTrailing = "TRAILING";
static const char *const kLineNumbersPositionOff = "OFF";
static const char *const kLineNumbersPositionLeading = "LEADING";
static const char *const kLineNumbersPositionTrailing = "TRAILING";
static const char *const kCodeFoldingPositionOff = "OFF";
static const char *const kCodeFoldingPositionLeading = "LEADING";
static const char *const kDefaultGuttersOrder = "LCM";
static const char *const kIndentStyleOff = "OFF";
static const char *const kIndentStyleAutomatic = "AUTOMATIC";
static const char *const kIndentStyleSmart = "SMART";
static const char *const kFileTypeLegacyText = "LEGACY_TEXT";
static const char *const kFileTypeUnix = "UNIX";
static const char *const kFileTypeBinary = "BINARY";
static const int kDefaultTabSize = 8;
static const int kMinTabSize = 2;
static const int kMaxTabSize = 32;
static const int kDefaultMiniMapWidth = 4;
static const int kMinMiniMapWidth = 2;
static const int kMaxMiniMapWidth = 20;
static const char *const kDefaultCursorStatusColor = "";
static const char *const kThemeSettingsKey = "COLORTHEMEURI";
static const char *const kKeymapSettingsKey = "KEYMAPURI";
static const char *const kWindowColorThemeProfileKey = "WINDOW_COLORTHEME_URI";
static const char *const kSettingsVersionKey = "SETTINGS_VERSION";
static const char *const kCurrentSettingsVersion = "2";
static const char *const kSearchTextTypeLiteral = "LITERAL";
static const char *const kSearchTextTypePcre = "PCRE";
static const char *const kSearchTextTypeWord = "WORD";
static const char *const kSearchDirectionForward = "FORWARD";
static const char *const kSearchDirectionBackward = "BACKWARD";
static const char *const kSearchModeStopFirst = "STOP_FIRST_OCCURRENCE";
static const char *const kSearchModePromptNext = "PROMPT_FOR_NEXT_MATCH";
static const char *const kSearchModeListAll = "LIST_ALL_OCCURRENCES";
static const char *const kSarModeReplaceFirst = "REPLACE_FIRST_OCCURRENCE";
static const char *const kSarModePromptEach = "PROMPT_FOR_EACH_REPLACE";
static const char *const kSarModeReplaceAll = "REPLACE_ALL_OCCURRENCES";
static const char *const kSarLeaveCursorEnd = "END_OF_REPLACE_STRING";
static const char *const kSarLeaveCursorStart = "START_OF_REPLACE_STRING";
static const char *const kLogHandlingVolatile = "VOLATILE";
static const char *const kLogHandlingPersist = "PERSIST";
static const char *const kLogHandlingJournalctl = "JOURNALCTL";
static const char *const kCursorBehaviourBoundToText = "BOUND_TO_TEXT";
static const char *const kCursorBehaviourFreeMovement = "FREE_MOVEMENT";
static const char *const kDialogLastPathKey = "DIALOG_LAST_PATH";
static const char *const kDialogPathHistoryKey = "DIALOG_PATH_HISTORY";
static const char *const kDialogFileHistoryKey = "DIALOG_FILE_HISTORY";

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
    {"WINDOW_MANAGER", MRSettingsKeyClass::Global, true},
    {"MESSAGES", MRSettingsKeyClass::Global, true},
    {"SEARCH_TEXT_TYPE", MRSettingsKeyClass::Global, true},
    {"SEARCH_DIRECTION", MRSettingsKeyClass::Global, true},
    {"SEARCH_MODE", MRSettingsKeyClass::Global, true},
    {"SEARCH_CASE_SENSITIVE", MRSettingsKeyClass::Global, true},
    {"SEARCH_GLOBAL_SEARCH", MRSettingsKeyClass::Global, true},
    {"SEARCH_RESTRICT_MARKED_BLOCK", MRSettingsKeyClass::Global, true},
    {"SEARCH_ALL_WINDOWS", MRSettingsKeyClass::Global, true},
    {"SEARCH_LIST_ALL_OCCURRENCES", MRSettingsKeyClass::Global, false},
    {"SAR_TEXT_TYPE", MRSettingsKeyClass::Global, true},
    {"SAR_DIRECTION", MRSettingsKeyClass::Global, true},
    {"SAR_MODE", MRSettingsKeyClass::Global, true},
    {"SAR_LEAVE_CURSOR_AT", MRSettingsKeyClass::Global, true},
    {"SAR_CASE_SENSITIVE", MRSettingsKeyClass::Global, true},
    {"SAR_GLOBAL_SEARCH", MRSettingsKeyClass::Global, true},
    {"SAR_RESTRICT_MARKED_BLOCK", MRSettingsKeyClass::Global, true},
    {"SAR_ALL_WINDOWS", MRSettingsKeyClass::Global, true},
    {"SAR_REPLACE_MODE", MRSettingsKeyClass::Global, false},
    {"SAR_PROMPT_EACH_REPLACE", MRSettingsKeyClass::Global, false},
    {"MULTI_SEARCH_FILESPEC", MRSettingsKeyClass::Global, true},
    {"MULTI_SEARCH_TEXT", MRSettingsKeyClass::Global, true},
    {"MULTI_SEARCH_STARTING_PATH", MRSettingsKeyClass::Global, true},
    {"MULTI_SEARCH_SUBDIRECTORIES", MRSettingsKeyClass::Global, true},
    {"MULTI_SEARCH_CASE_SENSITIVE", MRSettingsKeyClass::Global, true},
    {"MULTI_SEARCH_REGULAR_EXPRESSIONS", MRSettingsKeyClass::Global, true},
    {"MULTI_SEARCH_FILES_IN_MEMORY", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_FILESPEC", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_TEXT", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_REPLACEMENT", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_STARTING_PATH", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_SUBDIRECTORIES", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_CASE_SENSITIVE", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_REGULAR_EXPRESSIONS", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_FILES_IN_MEMORY", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_KEEP_FILES_OPEN", MRSettingsKeyClass::Global, true},
    {"MULTI_FILESPEC_HISTORY", MRSettingsKeyClass::Global, false},
    {"MULTI_PATH_HISTORY", MRSettingsKeyClass::Global, false},
    {"VIRTUAL_DESKTOPS", MRSettingsKeyClass::Global, true},
    {"CYCLIC_VIRTUAL_DESKTOPS", MRSettingsKeyClass::Global, true},
    {"CURSOR_BEHAVIOUR", MRSettingsKeyClass::Global, true},
    {"CURSOR_POSITION_MARKER", MRSettingsKeyClass::Global, true},
    {"AUTOLOAD_WORKSPACE", MRSettingsKeyClass::Global, true},
    {"LOG_HANDLING", MRSettingsKeyClass::Global, true},
    {"LOGFILE", MRSettingsKeyClass::Global, true},
    {"AUTOEXEC_MACRO", MRSettingsKeyClass::Global, false},
    {"LASTFILEDIALOGPATH", MRSettingsKeyClass::Global, false},
    {"WORKSPACE", MRSettingsKeyClass::Global, false},
    {"MAX_PATH_HISTORY", MRSettingsKeyClass::Global, true},
    {"MAX_FILE_HISTORY", MRSettingsKeyClass::Global, true},
    {"PATH_HISTORY", MRSettingsKeyClass::Global, false},
    {"FILE_HISTORY", MRSettingsKeyClass::Global, false},
    {kDialogLastPathKey, MRSettingsKeyClass::Global, false},
    {kDialogPathHistoryKey, MRSettingsKeyClass::Global, false},
    {kDialogFileHistoryKey, MRSettingsKeyClass::Global, false},
    {"DEFAULT_PROFILE_DESCRIPTION", MRSettingsKeyClass::Global, true},
    {kKeymapSettingsKey, MRSettingsKeyClass::Global, true},
    {"ACTIVE_KEYMAP_PROFILE", MRSettingsKeyClass::Global, true},
    {"KEYMAP_PROFILE", MRSettingsKeyClass::Global, false},
    {"KEYMAP_BIND", MRSettingsKeyClass::Global, false},
    {kThemeSettingsKey, MRSettingsKeyClass::Global, true},
    {"WINDOWCOLORS", MRSettingsKeyClass::ColorInline, false},
    {"MENUDIALOGCOLORS", MRSettingsKeyClass::ColorInline, false},
    {"HELPCOLORS", MRSettingsKeyClass::ColorInline, false},
    {"OTHERCOLORS", MRSettingsKeyClass::ColorInline, false},
    {"MINIMAPCOLORS", MRSettingsKeyClass::ColorInline, false},
    {"CODECOLORS", MRSettingsKeyClass::ColorInline, false},
};

const MREditSettingDescriptor *editSettingDescriptorByKeyInternal(const std::string &key);

static const MREditSettingDescriptor kEditSettingDescriptors[] = {
    {"PAGE_BREAK", "Page break", MREditSettingSection::Text, MREditSettingKind::String, true, kOvPageBreak},
    {"WORD_DELIMITERS", "Word delimiters", MREditSettingSection::Text, MREditSettingKind::String, true, kOvWordDelimiters},
    {"DEFAULT_EXTENSIONS", "Filename extension fallback", MREditSettingSection::OpenFile, MREditSettingKind::String, true, kOvDefaultExtensions},
    {"TRUNCATE_SPACES", "Truncate whitespace", MREditSettingSection::Save, MREditSettingKind::Boolean, true, kOvTruncateSpaces},
    {"EOF_CTRL_Z", "Write Ctrl+Z EOF", MREditSettingSection::Save, MREditSettingKind::Boolean, true, kOvEofCtrlZ},
    {"EOF_CR_LF", "Write CR/LF", MREditSettingSection::Save, MREditSettingKind::Boolean, true, kOvEofCrLf},
    {"TAB_EXPAND", "Expand tabs", MREditSettingSection::Tabs, MREditSettingKind::Boolean, true, kOvTabExpand},
    {"DISPLAY_TABS", "Display tabs", MREditSettingSection::Tabs, MREditSettingKind::Boolean, true, kOvDisplayTabs},
    {"TAB_SIZE", "Tab size", MREditSettingSection::Tabs, MREditSettingKind::Integer, true, kOvTabSize},
    {"LEFT_MARGIN", "Left margin", MREditSettingSection::Formatting, MREditSettingKind::Integer, true, kOvLeftMargin},
    {"RIGHT_MARGIN", "Right margin", MREditSettingSection::Formatting, MREditSettingKind::Integer, true, kOvRightMargin},
    {"FORMAT_RULER", "Format ruler", MREditSettingSection::Formatting, MREditSettingKind::Boolean, true, kOvFormatRuler},
    {"WORD_WRAP", "Word wrap", MREditSettingSection::Formatting, MREditSettingKind::Boolean, true, kOvWordWrap},
    {"INDENT_STYLE", "Indent style", MREditSettingSection::Formatting, MREditSettingKind::Choice, true, kOvIndentStyle},
    {"CODE_LANGUAGE", "Code language", MREditSettingSection::Display, MREditSettingKind::Choice, true, kOvCodeLanguage},
    {"CODE_COLORING", "Code coloring", MREditSettingSection::Display, MREditSettingKind::Boolean, true, kOvCodeColoring},
    {"CODE_FOLDING", "Code folding", MREditSettingSection::Display, MREditSettingKind::Boolean, true, kOvCodeFoldingFeature},
    {"SMART_INDENTING", "Smart indenting", MREditSettingSection::Display, MREditSettingKind::Boolean, true, kOvSmartIndenting},
    {"FILE_TYPE", "File type", MREditSettingSection::Formatting, MREditSettingKind::Choice, true, kOvFileType},
    {"BINARY_RECORD_LENGTH", "Binary record length", MREditSettingSection::Formatting, MREditSettingKind::Integer, true, kOvBinaryRecordLength},
    {"POST_LOAD_MACRO", "Post-load macro", MREditSettingSection::Macros, MREditSettingKind::String, true, kOvPostLoadMacro},
    {"PRE_SAVE_MACRO", "Pre-save macro", MREditSettingSection::Macros, MREditSettingKind::String, true, kOvPreSaveMacro},
    {"DEFAULT_PATH", "Default path", MREditSettingSection::Paths, MREditSettingKind::String, true, kOvDefaultPath},
    {"FORMAT_LINE", "Format line", MREditSettingSection::Formatting, MREditSettingKind::String, true, kOvFormatLine},
    {"BACKUP_FILES", "Backup files", MREditSettingSection::Save, MREditSettingKind::Boolean, true, kOvBackupFiles},
    {"BACKUP_METHOD", "Backup method", MREditSettingSection::Save, MREditSettingKind::Choice, false, kOvBackupMethod},
    {"BACKUP_FREQUENCY", "Backup frequency", MREditSettingSection::Save, MREditSettingKind::Choice, false, kOvBackupFrequency},
    {"BACKUP_EXTENSION", "Backup extension", MREditSettingSection::Save, MREditSettingKind::String, false, kOvBackupExtension},
    {"BACKUP_DIRECTORY", "Backup directory", MREditSettingSection::Save, MREditSettingKind::String, false, kOvBackupDirectory},
    {"AUTOSAVE_INACTIVITY_SECONDS", "Autosave inactivity", MREditSettingSection::Save, MREditSettingKind::Integer, false, kOvAutosaveInactivitySeconds},
    {"AUTOSAVE_INTERVAL_SECONDS", "Autosave interval", MREditSettingSection::Save, MREditSettingKind::Integer, false, kOvAutosaveIntervalSeconds},
    {"SHOW_EOF_MARKER", "Show EOF marker", MREditSettingSection::Display, MREditSettingKind::Boolean, true, kOvShowEofMarker},
    {"SHOW_EOF_MARKER_EMOJI", "EOF marker emoji", MREditSettingSection::Display, MREditSettingKind::Boolean, true, kOvShowEofMarkerEmoji},
    {"LINE_NUMBERS_POSITION", "Line number position", MREditSettingSection::Display, MREditSettingKind::Choice, true, kOvLineNumbersPosition},
    {"LINE_NUM_ZERO_FILL", "Zero-fill line numbers", MREditSettingSection::Display, MREditSettingKind::Boolean, true, kOvLineNumZeroFill},
    {"MINIMAP_POSITION", "Minimap position", MREditSettingSection::Display, MREditSettingKind::Choice, true, kOvMiniMapPosition},
    {"MINIMAP_WIDTH", "Minimap width", MREditSettingSection::Display, MREditSettingKind::Integer, true, kOvMiniMapWidth},
    {"MINIMAP_MARKER_GLYPH", "Minimap marker glyph", MREditSettingSection::Display, MREditSettingKind::String, true, kOvMiniMapMarkerGlyph},
    {"GUTTERS", "Gutter render order", MREditSettingSection::Display, MREditSettingKind::String, true, kOvGutters},
    {"PERSISTENT_BLOCKS", "Persistent blocks", MREditSettingSection::Blocks, MREditSettingKind::Boolean, true, kOvPersistentBlocks},
    {"CODE_FOLDING_POSITION", "Code folding position", MREditSettingSection::Display, MREditSettingKind::Choice, true, kOvCodeFoldingPosition},
    {"COLUMN_BLOCK_MOVE", "Column block move", MREditSettingSection::Blocks, MREditSettingKind::Choice, true, kOvColumnBlockMove},
    {"DEFAULT_MODE", "Default mode", MREditSettingSection::Mode, MREditSettingKind::Choice, true, kOvDefaultMode},
    {"CURSOR_STATUS_COLOR", "Cursor status color", MREditSettingSection::Display, MREditSettingKind::String, true, kOvCursorStatusColor},
};

static const unsigned char kPaletteMenuDescription = 2;
static const unsigned char kPaletteMenuGhostedDescription = 3;
static const unsigned char kPaletteMenuHotkey = 4;
static const unsigned char kPaletteMenuSelector = 5;
static const unsigned char kPaletteMenuGhostedSelector = 6;
static const unsigned char kPaletteMenuSelectedHotkey = 7;

static const unsigned char kPaletteBlueWindowFrame = 8;
static const unsigned char kPaletteBlueWindowBold = 9;
static const unsigned char kPaletteBlueWindowText = 13;
static const unsigned char kPaletteBlueWindowHighlight = 14;

static const unsigned char kPaletteGrayDialogBackground = 32;
static const unsigned char kPaletteGrayDialogFrame = 33;
static const unsigned char kPaletteGrayDialogFrameAccent = 34;
static const unsigned char kPaletteGrayDialogText = 37;
static const unsigned char kPaletteDialogButtonDescription = 41;
static const unsigned char kPaletteDialogButtonHotkey = 45;
static const unsigned char kPaletteDialogButtonShadow = 46;
static const unsigned char kPaletteDialogClusterHotkey = 49;
static const unsigned char kPaletteDialogListFrameLegacyPrimary = 24;
static const unsigned char kPaletteDialogListFrameLegacySecondary = 25;
static const unsigned char kPaletteDialogListNormalLegacy = 26;
static const unsigned char kPaletteDialogListFocusedLegacy = 27;
static const unsigned char kPaletteDialogListSelectedLegacy = 28;
static const unsigned char kPaletteDialogListTextLegacy = 29;
static const unsigned char kPaletteDialogListFrameExtendedPrimary = 55;
static const unsigned char kPaletteDialogListFrameExtendedSecondary = 56;
static const unsigned char kPaletteDialogListNormal = 57;
static const unsigned char kPaletteDialogListFocused = 58;
static const unsigned char kPaletteDialogListSelectedInactive = 59;
static const unsigned char kPaletteDialogListText = 60;
static const unsigned char kPaletteBlueDialogBackground = 64;
static const unsigned char kPaletteBlueDialogFrame = 65;
static const unsigned char kPaletteBlueDialogFrameAccent = 66;
static const unsigned char kPaletteBlueDialogText = 69;
static const unsigned char kPaletteCyanDialogBackground = 96;
static const unsigned char kPaletteCyanDialogFrame = 97;
static const unsigned char kPaletteCyanDialogFrameAccent = 98;
static const unsigned char kPaletteCyanDialogText = 101;
static const unsigned char kPaletteDialogInactiveClusterGray = 62;
static const unsigned char kPaletteDialogInactiveClusterBlue = 94;
static const unsigned char kPaletteDialogInactiveClusterCyan = 126;
static const unsigned char kPaletteHelpFrame = 128;
static const unsigned char kPaletteHelpText = 133;
static const unsigned char kPaletteHelpHighlight = 134;
static const unsigned char kPaletteHelpChapter = 135;

enum : std::size_t {
	kMenuDialogIndexListboxSelector = 11,
	kMenuDialogIndexInactiveCluster = 12,
	kMenuDialogIndexInactiveElements = 13,
	kMenuDialogIndexDialogFrame = 14,
	kMenuDialogIndexDialogText = 15,
	kMenuDialogIndexDialogBackground = 16,
	kMenuDialogIndexDropListDescription = 17,
	kMenuDialogIndexDropListSelectedInactive = 18
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
    {"text", kPaletteBlueWindowText}, {"changed text", kMrPaletteChangedText}, {"highlighted text", kPaletteBlueWindowHighlight}, {"EOF marker", kMrPaletteEofMarker}, {"window border", kPaletteBlueWindowFrame}, {"window bold", kPaletteBlueWindowBold}, {"current line", kMrPaletteCurrentLine}, {"current line in block", kMrPaletteCurrentLineInBlock}, {"line numbers", kMrPaletteLineNumbers}, {"code folding", kMrPaletteCodeFolding}, {"format ruler", kMrPaletteFormatRuler},
};

static const MRColorSetupItem kMenuDialogColorItems[] = {
    // Mixed group by design:
    // - Menu/status palette slots 2..6.
    // - Gray dialog block (32..63), as TDialog defaults to dpGrayDialog.
    {"description of selectable menu element", kPaletteMenuDescription}, {"description of ghosted menu element", kPaletteMenuGhostedDescription}, {"hotkey of menu element", kPaletteMenuHotkey}, {"menu selector on selectable menu element", kPaletteMenuSelector}, {"menu selector on ghosted menu element", kPaletteMenuGhostedSelector}, {"description of buttons", kPaletteDialogButtonDescription}, {"hotkey on buttons", kPaletteDialogButtonHotkey}, {"button shadow", kPaletteDialogButtonShadow}, {"selected element in unfocussed listbox", kPaletteDialogListSelectedInactive}, {"element description in listbox", kPaletteDialogListNormal}, {"hotkeys on radio buttons & check boxes", kPaletteDialogClusterHotkey}, {"dialog selector", kPaletteDialogListFocused}, {"inactive radio buttons and checkboxes", kPaletteDialogInactiveClusterGray}, {"inactive dialog elements", kMrPaletteDialogInactiveElements}, {"dialog frame", kPaletteGrayDialogFrame}, {"dialog text", kPaletteGrayDialogText}, {"dialog background", kPaletteGrayDialogBackground}, {"element description in droplists", kMrPaletteDropListDescription}, {"selected element in unfocussed droplist", kMrPaletteDropListSelectedInactive},
};

static const MRColorSetupItem kHelpColorItems[] = {
    {"Help-Text", kPaletteHelpText}, {"help-Highlight", kPaletteHelpHighlight}, {"help-Chapter", kPaletteHelpChapter}, {"help-Border", kPaletteHelpFrame}, {"help-Link", kPaletteHelpHighlight}, {"help-F-keys", kPaletteHelpChapter}, {"help-attr-1", kPaletteHelpText}, {"help-attr-2", kPaletteHelpHighlight}, {"help-attr-3", kPaletteHelpChapter},
};

static const MRColorSetupItem kOtherColorItems[] = {
    {"statusline", kPaletteMenuDescription}, {"statusline bold", kPaletteMenuGhostedDescription}, {"function descriptions on statusline", kPaletteMenuHotkey}, {"function keys on statusline", kPaletteMenuSelector}, {"error message", kMrPaletteMessageError}, {"message", kMrPaletteMessage}, {"warning message", kMrPaletteMessageWarning}, {"hero events", kMrPaletteMessageHero}, {"cursor position marker", kMrPaletteCursorPositionMarker}, {"desktop background", kMrPaletteDesktop}, {"virtual desktop marker", kMrPaletteVirtualDesktopMarker},
};

static const MRColorSetupItem kMiniMapColorItems[] = {
    {"normal", kMrPaletteMiniMapNormal}, {"viewport cursor", kMrPaletteMiniMapViewport}, {"changed", kMrPaletteMiniMapChanged}, {"find marker", kMrPaletteMiniMapFindMarker}, {"error marker", kMrPaletteMiniMapErrorMarker},
};

static const MRColorSetupItem kCodeColorItems[] = {
    {"comments", kMrPaletteCodeComments}, {"strings", kMrPaletteCodeStrings}, {"characters", kMrPaletteCodeCharacters}, {"numbers", kMrPaletteCodeNumbers}, {"keywords", kMrPaletteCodeKeywords}, {"types", kMrPaletteCodeTypes}, {"directives", kMrPaletteCodeDirectives}, {"functions", kMrPaletteCodeFunctions}, {"builtins", kMrPaletteCodeBuiltins}, {"constants", kMrPaletteCodeConstants}, {"operators", kMrPaletteCodeOperators}, {"brackets", kMrPaletteCodeBrackets}, {"delimiters", kMrPaletteCodeDelimiters},
};

static const ColorGroupDefinition kColorGroups[] = {
    {MRColorSetupGroup::Window, "WINDOW COLORS", "WINDOWCOLORS", kWindowColorItems, std::size(kWindowColorItems)}, {MRColorSetupGroup::MenuDialog, "MENU / DIALOG COLORS", "MENUDIALOGCOLORS", kMenuDialogColorItems, std::size(kMenuDialogColorItems)}, {MRColorSetupGroup::Help, "HELP COLORS", "HELPCOLORS", kHelpColorItems, std::size(kHelpColorItems)}, {MRColorSetupGroup::Other, "OTHER COLORS", "OTHERCOLORS", kOtherColorItems, std::size(kOtherColorItems)}, {MRColorSetupGroup::MiniMap, "MINIMAP COLORS", "MINIMAPCOLORS", kMiniMapColorItems, std::size(kMiniMapColorItems)}, {MRColorSetupGroup::Code, "CODE COLORS", "CODECOLORS", kCodeColorItems, std::size(kCodeColorItems)},
};

const ColorGroupDefinition *findColorGroupDefinition(MRColorSetupGroup group) {
	for (const auto &kColorGroup : kColorGroups)
		if (kColorGroup.group == group) return &kColorGroup;
	return nullptr;
}

const ColorGroupDefinition *findColorGroupDefinitionByKey(const std::string &key) {
	std::string upper = upperAscii(trimAscii(key));
	for (const auto &kColorGroup : kColorGroups)
		if (upper == kColorGroup.key) return &kColorGroup;
	return nullptr;
}

unsigned char defaultColorForSlot(unsigned char paletteIndex) {
	// Defaults from cpAppColor (TVision app.h), expanded explicitly for stability.
	static constexpr std::array<unsigned char, 146> defaults = {
	    0x00, 0x71, 0x70, 0x78, 0x74, 0x20, 0x28, 0x24, 0x17, 0x1F, 0x1A, 0x31, 0x31, 0x1E, 0x71, 0x1F, 0x37, 0x3F, 0x3A, 0x13, 0x13, 0x3E, 0x21, 0x3F, 0x70, 0x7F, 0x7A, 0x13, 0x13, 0x70, 0x7F, 0x7E, 0x70, 0x7F, 0x7A, 0x13, 0x13, 0x70, 0x70, 0x7F, 0x7E, 0x20, 0x2B, 0x2F, 0x78, 0x2E, 0x70, 0x30, 0x3F, 0x3E, 0x1F, 0x2F, 0x1A, 0x20, 0x72, 0x31, 0x31, 0x30, 0x2F, 0x3E, 0x31, 0x13, 0x38, 0x00, 0x17, 0x1F, 0x1A, 0x71, 0x71, 0x1E, 0x17, 0x1F, 0x1E, 0x20, 0x2B, 0x2F, 0x78, 0x2E, 0x10, 0x30, 0x3F, 0x3E, 0x70, 0x2F, 0x7A, 0x20, 0x12, 0x31, 0x31, 0x30, 0x2F, 0x3E, 0x31, 0x13, 0x38, 0x00, 0x37, 0x3F, 0x3A, 0x13, 0x13, 0x3E, 0x30, 0x3F, 0x3E, 0x20, 0x2B, 0x2F, 0x78, 0x2E, 0x30, 0x70, 0x7F, 0x7E, 0x1F, 0x2F, 0x1A, 0x20, 0x32, 0x31, 0x71, 0x70, 0x2F, 0x7E, 0x71, 0x13, 0x78, 0x00, 0x37, 0x3F, 0x3A, 0x13, 0x13, 0x30, 0x3E, 0x1E,
	};

	if (paletteIndex == kMrPaletteCurrentLine) return defaults[10];
	if (paletteIndex == kMrPaletteCurrentLineInBlock) return defaults[12];
	if (paletteIndex == kMrPaletteChangedText) return defaults[14];
	if (paletteIndex == kMrPaletteMessageError) return defaults[42];
	if (paletteIndex == kMrPaletteMessage) return defaults[43];
	if (paletteIndex == kMrPaletteMessageWarning) return defaults[44];
	if (paletteIndex == kMrPaletteMessageHero) return defaults[43];
	if (paletteIndex == kMrPaletteCursorPositionMarker) return defaults[3];
	if (paletteIndex == kMrPaletteLineNumbers) return defaults[9];
	if (paletteIndex == kMrPaletteCodeFolding) return defaults[9];
	if (paletteIndex == kMrPaletteFormatRuler) return defaults[13];
	if (paletteIndex == kMrPaletteEofMarker) return defaults[14];
	if (paletteIndex == kMrPaletteMiniMapNormal) return defaults[13];
	if (paletteIndex == kMrPaletteMiniMapViewport) return defaults[11];
	if (paletteIndex == kMrPaletteMiniMapChanged) return defaults[14];
	if (paletteIndex == kMrPaletteMiniMapFindMarker) return defaults[5];
	if (paletteIndex == kMrPaletteMiniMapErrorMarker) return defaults[42];
	if (paletteIndex == kMrPaletteCodeComments) return defaults[12];
	if (paletteIndex == kMrPaletteCodeStrings) return defaults[14];
	if (paletteIndex == kMrPaletteCodeCharacters) return defaults[14];
	if (paletteIndex == kMrPaletteCodeNumbers) return defaults[13];
	if (paletteIndex == kMrPaletteCodeKeywords) return defaults[11];
	if (paletteIndex == kMrPaletteCodeTypes) return defaults[9];
	if (paletteIndex == kMrPaletteCodeDirectives) return defaults[42];
	if (paletteIndex == kMrPaletteCodeFunctions) return defaults[10];
	if (paletteIndex == kMrPaletteCodeBuiltins) return defaults[43];
	if (paletteIndex == kMrPaletteCodeConstants) return defaults[3];
	if (paletteIndex == kMrPaletteCodeOperators) return defaults[37];
	if (paletteIndex == kMrPaletteCodeBrackets) return defaults[9];
	if (paletteIndex == kMrPaletteCodeDelimiters) return defaults[13];
	if (paletteIndex == kMrPaletteDropListDescription) return defaults[57];
	if (paletteIndex == kMrPaletteDropListSelectedInactive) return defaults[59];
	if (paletteIndex == kMrPaletteDialogInactiveElements) return defaults[kPaletteDialogInactiveClusterGray];
	if (paletteIndex == kMrPaletteDesktop) return 0x90;
	if (paletteIndex == kMrPaletteVirtualDesktopMarker) return 0x9F;
	if (paletteIndex == 0 || paletteIndex >= std::size(defaults)) return 0x70;
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
	for (std::size_t i = 0; i < settings.miniMapColors.size(); ++i)
		settings.miniMapColors[i] = defaultColorForSlot(kMiniMapColorItems[i].paletteIndex);
	for (std::size_t i = 0; i < settings.codeColors.size(); ++i)
		settings.codeColors[i] = defaultColorForSlot(kCodeColorItems[i].paletteIndex);
	return settings;
}

bool parseHexColorToken(const std::string &token, unsigned char &outValue) {
	std::string value = trimAscii(token);
	unsigned int parsed = 0;

	if (value.empty() || value.size() > 2) return false;
	for (char i : value)
		if (!std::isxdigit(static_cast<unsigned char>(i))) return false;
	parsed = static_cast<unsigned int>(std::strtoul(value.c_str(), nullptr, 16));
	if (parsed > 0xFF) return false;
	outValue = static_cast<unsigned char>(parsed);
	return true;
}

template <std::size_t N> std::string formatColorListLiteral(const std::array<unsigned char, N> &values) {
	static const char *const hex = "0123456789ABCDEF";
	std::string out = "v1:";

	for (std::size_t i = 0; i < N; ++i) {
		unsigned char value = values[i];
		if (i != 0) out.push_back(',');
		out.push_back(hex[(value >> 4) & 0x0F]);
		out.push_back(hex[value & 0x0F]);
	}
	return out;
}

std::string formatWindowColorListLiteral(const std::array<unsigned char, MRColorSetupSettings::kWindowCount> &values) {
	std::string out = formatColorListLiteral(values);

	// WINDOWCOLORS uses v4 (adds format-ruler color to v3's 10-value layout).
	if (out.size() >= 2 && out[0] == 'v') out[1] = '4';
	return out;
}

template <std::size_t N> bool parseColorListLiteral(const std::string &literal, std::array<unsigned char, N> &outValues, std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::size_t itemIndex = 0;
	std::array<unsigned char, N> parsed;

	if (text.size() >= 3 && (text[0] == 'v' || text[0] == 'V') && std::isdigit(static_cast<unsigned char>(text[1])) && text[2] == ':') text = text.substr(3);
	if (text.empty()) return setError(errorMessage, "Empty color list.");

	while (cursor <= text.size() && itemIndex < N) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
		unsigned char value = 0;

		if (!parseHexColorToken(token, value)) return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed[itemIndex++] = value;
		if (comma == std::string::npos) break;
		cursor = comma + 1;
	}

	if (itemIndex != N) return setError(errorMessage, "Unexpected color list size.");
	if (text.find(',', cursor) != std::string::npos) return setError(errorMessage, "Too many color values in list.");

	outValues = parsed;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseWindowColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kWindowCount> &outValues, std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::vector<unsigned char> parsed;
	const unsigned char defaultEofMarker = defaultColorForSlot(kMrPaletteEofMarker);
	const unsigned char defaultLineNumbers = defaultColorForSlot(kMrPaletteLineNumbers);
	const unsigned char defaultCodeFolding = defaultColorForSlot(kMrPaletteCodeFolding);
	const unsigned char defaultFormatRuler = defaultColorForSlot(kMrPaletteFormatRuler);
	unsigned char value = 0;
	bool v4Format = false;
	bool v3Format = false;
	bool v2Format = false;

	if (text.rfind("v4:", 0) == 0 || text.rfind("V4:", 0) == 0) {
		text = text.substr(3);
		v4Format = true;
	} else if (text.rfind("v3:", 0) == 0 || text.rfind("V3:", 0) == 0) {
		text = text.substr(3);
		v3Format = true;
	} else if (text.rfind("v2:", 0) == 0 || text.rfind("V2:", 0) == 0) {
		text = text.substr(3);
		v2Format = true;
	} else if (text.rfind("v1:", 0) == 0 || text.rfind("V1:", 0) == 0)
		text = text.substr(3);
	if (text.empty()) return setError(errorMessage, "Empty color list.");

	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
		if (!parseHexColorToken(token, value)) return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos) break;
		cursor = comma + 1;
	}

	// Formats:
	// - v4 + 11 values: current format (..., EOF marker, ..., line numbers, code folding, format ruler)
	// - v3 + 10 values: previous format without dedicated format-ruler color
	// - v2 + 8 values: previous format (without EOF marker, line numbers, code folding)
	// - v1 + 7 values: legacy format (without EOF marker and without line-number/folding colors)
	// - v1 + 8 values: legacy format with EOF marker as 4th entry, no line-number/folding colors
	// - unversioned 9 values: layout with line numbers but without code folding
	if (v4Format) {
		if (parsed.size() != MRColorSetupSettings::kWindowCount) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v4.");
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = parsed[i];
	} else if (v3Format) {
		if (parsed.size() != MRColorSetupSettings::kWindowCount - 1) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v3.");
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[10] = defaultFormatRuler;
	} else if (v2Format) {
		if (parsed.size() != 8) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v2.");
		outValues[0] = parsed[0];
		outValues[1] = parsed[1];
		outValues[2] = parsed[2];
		outValues[3] = defaultEofMarker;
		outValues[4] = parsed[3];
		outValues[5] = parsed[4];
		outValues[6] = parsed[5];
		outValues[7] = parsed[6];
		outValues[8] = defaultLineNumbers;
		outValues[9] = defaultCodeFolding;
		outValues[10] = defaultFormatRuler;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount) {
		// Accept unversioned current layout as a tolerant input.
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 1) {
		// Previous current layout without dedicated format-ruler color.
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[10] = defaultFormatRuler;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 2) {
		// Layout with EOF+line numbers and missing code-folding/format-ruler colors.
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[9] = defaultCodeFolding;
		outValues[10] = defaultFormatRuler;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 3) {
		// Legacy v1 with EOF marker and without line-number/folding/ruler colors.
		outValues[0] = parsed[0];
		outValues[1] = parsed[1];
		outValues[2] = parsed[2];
		outValues[3] = parsed[3];
		outValues[4] = parsed[4];
		outValues[5] = parsed[5];
		outValues[6] = parsed[6];
		outValues[7] = parsed[7];
		outValues[8] = defaultLineNumbers;
		outValues[9] = defaultCodeFolding;
		outValues[10] = defaultFormatRuler;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 4) {
		// Legacy v1 without EOF marker and without line-number/folding/ruler colors.
		outValues[0] = parsed[0];
		outValues[1] = parsed[1];
		outValues[2] = parsed[2];
		outValues[3] = defaultEofMarker;
		outValues[4] = parsed[3];
		outValues[5] = parsed[4];
		outValues[6] = parsed[5];
		outValues[7] = parsed[6];
		outValues[8] = defaultLineNumbers;
		outValues[9] = defaultCodeFolding;
		outValues[10] = defaultFormatRuler;
	} else
		return setError(errorMessage, "Unexpected WINDOWCOLORS list size.");

	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseMenuDialogColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kMenuDialogCount> &outValues, std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::vector<unsigned char> parsed;
	unsigned char value = 0;

	if (text.size() >= 3 && (text[0] == 'v' || text[0] == 'V') && std::isdigit(static_cast<unsigned char>(text[1])) && text[2] == ':') text = text.substr(3);
	if (text.empty()) return setError(errorMessage, "Empty color list.");

	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
		if (!parseHexColorToken(token, value)) return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos) break;
		cursor = comma + 1;
	}

	for (std::size_t i = 0; i < outValues.size(); ++i)
		outValues[i] = defaultColorForSlot(kMenuDialogColorItems[i].paletteIndex);

	// Accepted formats:
	// - 19: current format with dedicated droplist colors.
	// - 18: current format missing "selected element in unfocussed droplist".
	// - 17: previous current format without dedicated droplist colors.
	// - 16: previous format without inactive dialog elements
	// - 15: previous format without inactive radio/checkbox and inactive dialog elements
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
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == MRColorSetupSettings::kMenuDialogCount - 2) {
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == MRColorSetupSettings::kMenuDialogCount - 3) {
		for (std::size_t i = 0; i <= kMenuDialogIndexInactiveCluster; ++i)
			outValues[i] = parsed[i];
		outValues[kMenuDialogIndexDialogFrame] = parsed[13];
		outValues[kMenuDialogIndexDialogText] = parsed[14];
		outValues[kMenuDialogIndexDialogBackground] = parsed[15];
	} else if (parsed.size() == MRColorSetupSettings::kMenuDialogCount - 4) {
		for (std::size_t i = 0; i <= kMenuDialogIndexListboxSelector; ++i)
			outValues[i] = parsed[i];
		outValues[kMenuDialogIndexDialogFrame] = parsed[12];
		outValues[kMenuDialogIndexDialogText] = parsed[13];
		outValues[kMenuDialogIndexDialogBackground] = parsed[12];
	} else if (parsed.size() == MRColorSetupSettings::kMenuDialogCount - 5) {
		for (std::size_t i = 0; i <= kMenuDialogIndexListboxSelector; ++i)
			outValues[i] = parsed[i];
		outValues[kMenuDialogIndexDialogFrame] = parsed[12];
		outValues[kMenuDialogIndexDialogText] = parsed[13];
		outValues[kMenuDialogIndexDialogBackground] = parsed[12];
	} else if (parsed.size() == MRColorSetupSettings::kMenuDialogCount - 6) {
		for (std::size_t i = 0; i <= kMenuDialogIndexListboxSelector; ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == MRColorSetupSettings::kMenuDialogCount - 7) {
		for (std::size_t i = 0; i < 11; ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == MRColorSetupSettings::kMenuDialogCount - 8) {
		for (std::size_t i = 0; i < 11; ++i)
			outValues[i] = parsed[i];
	} else {
		return setError(errorMessage, "Unexpected MENUDIALOGCOLORS list size.");
	}

	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseOtherColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kOtherCount> &outValues, std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::vector<unsigned char> parsed;
	unsigned char value = 0;

	if (text.size() >= 3 && (text[0] == 'v' || text[0] == 'V') && std::isdigit(static_cast<unsigned char>(text[1])) && text[2] == ':') text = text.substr(3);
	if (text.empty()) return setError(errorMessage, "Empty color list.");

	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
		if (!parseHexColorToken(token, value)) return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos) break;
		cursor = comma + 1;
	}

	// Accept current 11-value format, prior 9/8/7-value formats and legacy 10-value format.
	// Legacy format ended with shadow / shadow-character / background color entries,
	// which are no longer configurable in OTHERCOLORS.
	if (parsed.size() == MRColorSetupSettings::kOtherCount)
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = parsed[i];
	else if (parsed.size() == 9) {
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[9] = defaultColorForSlot(kMrPaletteDesktop);
		outValues[10] = defaultColorForSlot(kMrPaletteVirtualDesktopMarker);
	} else if (parsed.size() == 8) {
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[8] = defaultColorForSlot(kMrPaletteCursorPositionMarker);
		outValues[9] = defaultColorForSlot(kMrPaletteDesktop);
		outValues[10] = defaultColorForSlot(kMrPaletteVirtualDesktopMarker);
	} else if (parsed.size() == 7) {
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[7] = defaultColorForSlot(kMrPaletteMessageHero);
		outValues[8] = defaultColorForSlot(kMrPaletteCursorPositionMarker);
		outValues[9] = defaultColorForSlot(kMrPaletteDesktop);
		outValues[10] = defaultColorForSlot(kMrPaletteVirtualDesktopMarker);
	} else if (parsed.size() == 10) {
		for (std::size_t i = 0; i < 7; ++i)
			outValues[i] = parsed[i];
		outValues[7] = defaultColorForSlot(kMrPaletteMessageHero);
		outValues[8] = defaultColorForSlot(kMrPaletteCursorPositionMarker);
		outValues[9] = defaultColorForSlot(kMrPaletteDesktop);
		outValues[10] = defaultColorForSlot(kMrPaletteVirtualDesktopMarker);
	} else
		return setError(errorMessage, "Unexpected OTHERCOLORS list size.");

	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseThemeSetupAssignments(const std::string &source, std::map<std::string, std::string> &assignments, std::string *errorMessage) {
	static const std::regex pattern("MRSETUP\\s*\\(\\s*'([^']+)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)", std::regex_constants::ECMAScript | std::regex_constants::icase);
	std::set<std::string> allowed = {"WINDOWCOLORS", "MENUDIALOGCOLORS", "HELPCOLORS", "OTHERCOLORS", "MINIMAPCOLORS", "CODECOLORS"};
	std::set<std::string> required = {"WINDOWCOLORS", "MENUDIALOGCOLORS", "HELPCOLORS", "OTHERCOLORS"};
	std::sregex_iterator it(source.begin(), source.end(), pattern);
	std::sregex_iterator end;

	assignments.clear();
	for (; it != end; ++it) {
		std::string key = upperAscii(trimAscii((*it)[1].str()));
		std::string value = unescapeMrmacSingleQuotedLiteral((*it)[2].str());

		if (allowed.find(key) == allowed.end()) continue;
		assignments[key] = value;
	}
	for (const std::string &key : required)
		if (assignments.find(key) == assignments.end()) return setError(errorMessage, "Theme file must define MRSETUP('" + key + "', ...).");
	if (assignments.find("MINIMAPCOLORS") == assignments.end()) {
		MRColorSetupSettings defaults = resolveColorSetupDefaults();
		assignments["MINIMAPCOLORS"] = formatColorListLiteral(defaults.miniMapColors);
	}
	if (assignments.find("CODECOLORS") == assignments.end()) {
		MRColorSetupSettings defaults = resolveColorSetupDefaults();
		assignments["CODECOLORS"] = formatColorListLiteral(defaults.codeColors);
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

void ensureConfiguredColorSettingsInitialized() {
	if (configuredColorSettingsInitialized()) return;
	configuredColorSettings() = defaultsFromColorGroups();
	configuredColorSettingsInitialized() = true;
}

bool parseBooleanLiteral(const std::string &value, bool &outValue, std::string *errorMessage) {
	std::string upper = upperAscii(trimAscii(value));

	if (upper == "TRUE" || upper == "1" || upper == "YES" || upper == "ON") {
		outValue = true;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == "FALSE" || upper == "0" || upper == "NO" || upper == "OFF") {
		outValue = false;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "Expected boolean literal true/false.");
}

bool parseLogHandlingLiteral(const std::string &value, MRLogHandling &outValue, std::string *errorMessage) {
	const std::string upper = upperAscii(trimAscii(value));

	if (upper == kLogHandlingVolatile) {
		outValue = MRLogHandling::Volatile;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kLogHandlingPersist) {
		outValue = MRLogHandling::Persist;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kLogHandlingJournalctl) {
		outValue = MRLogHandling::Journalctl;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "Expected log handling VOLATILE, PERSIST or JOURNALCTL.");
}

std::string formatLogHandlingLiteral(MRLogHandling handling) {
	switch (handling) {
		case MRLogHandling::Volatile:
			return kLogHandlingVolatile;
		case MRLogHandling::Persist:
			return kLogHandlingPersist;
		case MRLogHandling::Journalctl:
			return kLogHandlingJournalctl;
	}
	return kLogHandlingVolatile;
}

bool parseCursorBehaviourLiteral(const std::string &value, MRCursorBehaviour &outValue, std::string *errorMessage) {
	const std::string upper = upperAscii(trimAscii(value));

	if (upper == kCursorBehaviourBoundToText || upper == "BOUND") {
		outValue = MRCursorBehaviour::BoundToText;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kCursorBehaviourFreeMovement || upper == "FREE") {
		outValue = MRCursorBehaviour::FreeMovement;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "CURSOR_BEHAVIOUR must be BOUND_TO_TEXT or FREE_MOVEMENT.");
}

std::string formatCursorBehaviourLiteral(MRCursorBehaviour behaviour) {
	return behaviour == MRCursorBehaviour::FreeMovement ? kCursorBehaviourFreeMovement : kCursorBehaviourBoundToText;
}

std::string canonicalBooleanLiteral(bool value) {
	return value ? "true" : "false";
}

std::string formatSearchTextType(MRSearchTextType value) {
	if (value == MRSearchTextType::Word) return kSearchTextTypeWord;
	if (value == MRSearchTextType::Pcre) return kSearchTextTypePcre;
	return kSearchTextTypeLiteral;
}

bool parseSearchTextTypeLiteral(const std::string &value, MRSearchTextType &outValue, std::string *errorMessage) {
	std::string upper = upperAscii(trimAscii(value));

	if (upper == kSearchTextTypeLiteral) {
		outValue = MRSearchTextType::Literal;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSearchTextTypeWord) {
		outValue = MRSearchTextType::Word;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSearchTextTypePcre || upper == "REGEX" || upper == "REGULAR_EXPRESSION") {
		outValue = MRSearchTextType::Pcre;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "SEARCH_TEXT_TYPE/SAR_TEXT_TYPE must be LITERAL, PCRE or WORD.");
}

std::string formatSearchDirection(MRSearchDirection value) {
	return value == MRSearchDirection::Backward ? kSearchDirectionBackward : kSearchDirectionForward;
}

bool parseSearchDirectionLiteral(const std::string &value, MRSearchDirection &outValue, std::string *errorMessage) {
	std::string upper = upperAscii(trimAscii(value));

	if (upper == kSearchDirectionForward) {
		outValue = MRSearchDirection::Forward;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSearchDirectionBackward) {
		outValue = MRSearchDirection::Backward;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "SEARCH_DIRECTION/SAR_DIRECTION must be FORWARD or BACKWARD.");
}

std::string formatSearchMode(MRSearchMode value) {
	if (value == MRSearchMode::PromptNext) return kSearchModePromptNext;
	if (value == MRSearchMode::ListAll) return kSearchModeListAll;
	return kSearchModeStopFirst;
}

bool parseSearchModeLiteral(const std::string &value, MRSearchMode &outValue, std::string *errorMessage) {
	std::string upper = upperAscii(trimAscii(value));

	if (upper == kSearchModeStopFirst || upper == "STOP_FIRST") {
		outValue = MRSearchMode::StopFirst;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSearchModePromptNext || upper == "PROMPT_NEXT") {
		outValue = MRSearchMode::PromptNext;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSearchModeListAll || upper == "LIST_ALL") {
		outValue = MRSearchMode::ListAll;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "SEARCH_MODE must be STOP_FIRST_OCCURRENCE, PROMPT_FOR_NEXT_MATCH or LIST_ALL_OCCURRENCES.");
}

std::string formatSarMode(MRSarMode value) {
	if (value == MRSarMode::PromptEach) return kSarModePromptEach;
	if (value == MRSarMode::ReplaceAll) return kSarModeReplaceAll;
	return kSarModeReplaceFirst;
}

bool parseSarModeLiteral(const std::string &value, MRSarMode &outValue, std::string *errorMessage) {
	std::string upper = upperAscii(trimAscii(value));

	if (upper == kSarModeReplaceFirst || upper == "FIRST") {
		outValue = MRSarMode::ReplaceFirst;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSarModePromptEach || upper == "PROMPT_EACH") {
		outValue = MRSarMode::PromptEach;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSarModeReplaceAll || upper == "ALL") {
		outValue = MRSarMode::ReplaceAll;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "SAR_MODE must be REPLACE_FIRST_OCCURRENCE, PROMPT_FOR_EACH_REPLACE or REPLACE_ALL_OCCURRENCES.");
}

std::string formatSarLeaveCursor(MRSarLeaveCursor value) {
	return value == MRSarLeaveCursor::StartOfReplaceString ? kSarLeaveCursorStart : kSarLeaveCursorEnd;
}

bool parseSarLeaveCursorLiteral(const std::string &value, MRSarLeaveCursor &outValue, std::string *errorMessage) {
	std::string upper = upperAscii(trimAscii(value));

	if (upper == kSarLeaveCursorEnd || upper == "END") {
		outValue = MRSarLeaveCursor::EndOfReplaceString;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSarLeaveCursorStart || upper == "START") {
		outValue = MRSarLeaveCursor::StartOfReplaceString;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "SAR_LEAVE_CURSOR_AT must be END_OF_REPLACE_STRING or START_OF_REPLACE_STRING.");
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
		out.push_back(ch);
	}
	if (rCount == 0 || cCount == 0) return setError(errorMessage, "must contain R and C placeholder.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string normalizeColumnBlockMove(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));
	std::string compact;

	for (char ch : key) {
		if (ch == '-' || ch == ' ') ch = '_';
		compact.push_back(ch);
	}

	if (compact == "DELETE_SPACE" || compact == "DELETE") return kColumnBlockMoveDelete;
	if (compact == "LEAVE_SPACE" || compact == "LEAVE") return kColumnBlockMoveLeave;
	return std::string();
}

std::string normalizeDefaultMode(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "INSERT") return kDefaultModeInsert;
	if (key == "OVERWRITE" || key == "OVR") return kDefaultModeOverwrite;
	return std::string();
}

std::string normalizeMiniMapPosition(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "OFF") return kMiniMapPositionOff;
	if (key == "LEADING" || key == "LEFT") return kMiniMapPositionLeading;
	if (key == "TRAILING" || key == "RIGHT") return kMiniMapPositionTrailing;
	return std::string();
}

std::string normalizeGutterPosition(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "OFF") return kLineNumbersPositionOff;
	if (key == "LEADING" || key == "LEFT") return kLineNumbersPositionLeading;
	if (key == "TRAILING" || key == "RIGHT") return kLineNumbersPositionTrailing;
	return std::string();
}

std::string normalizeLineNumbersPosition(const std::string &value) {
	return normalizeGutterPosition(value);
}

std::string normalizeCodeFoldingPosition(const std::string &value) {
	return normalizeGutterPosition(value);
}

std::string normalizeGuttersOrder(const std::string &value) {
	std::string normalized;
	std::array<bool, 3> seen = {false, false, false};
	const std::string upper = upperAscii(trimAscii(value));
	auto addChar = [&](char marker, std::size_t index) {
		if (!seen[index]) {
			normalized.push_back(marker);
			seen[index] = true;
		}
	};

	for (char ch : upper) {
		switch (ch) {
			case 'L':
				addChar('L', 0);
				break;
			case 'C':
				addChar('C', 1);
				break;
			case 'M':
				addChar('M', 2);
				break;
			default:
				break;
		}
	}
	if (normalized.empty()) normalized = kDefaultGuttersOrder;
	return normalized;
}

int utf8CodepointLength(unsigned char lead) {
	if ((lead & 0x80u) == 0u) return 1;
	if ((lead & 0xE0u) == 0xC0u) return 2;
	if ((lead & 0xF0u) == 0xE0u) return 3;
	if ((lead & 0xF8u) == 0xF0u) return 4;
	return 0;
}

bool normalizeMiniMapMarkerGlyph(const std::string &value, std::string &outGlyph, std::string *errorMessage) {
	const std::string trimmed = trimAscii(value);

	if (trimmed.empty()) {
		outGlyph = "│";
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	const unsigned char lead = static_cast<unsigned char>(trimmed[0]);
	const int cpLen = utf8CodepointLength(lead);
	if (cpLen == 0 || trimmed.size() != static_cast<std::size_t>(cpLen)) return setError(errorMessage, "MINIMAP_MARKER_GLYPH must be exactly one UTF-8 character.");
	for (int i = 1; i < cpLen; ++i) {
		const unsigned char ch = static_cast<unsigned char>(trimmed[static_cast<std::size_t>(i)]);
		if ((ch & 0xC0u) != 0x80u) return setError(errorMessage, "MINIMAP_MARKER_GLYPH must be valid UTF-8.");
	}
	if (cpLen == 1 && std::iscntrl(lead) != 0) return setError(errorMessage, "MINIMAP_MARKER_GLYPH may not be a control character.");
	outGlyph = trimmed;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string normalizeIndentStyle(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "OFF") return kIndentStyleOff;
	if (key == "AUTOMATIC" || key == "AUTO") return kIndentStyleAutomatic;
	if (key == "SMART") return kIndentStyleSmart;
	return std::string();
}

std::string normalizeFileType(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "LEGACY_TEXT" || key == "LEGACY" || key == "CRLF" || key == "TEXT") return kFileTypeLegacyText;
	if (key == "UNIX" || key == "LF") return kFileTypeUnix;
	if (key == "BINARY" || key == "BIN") return kFileTypeBinary;
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

	if (key == "OFF") return kBackupMethodOff;
	if (key == "BAK_FILE" || key == "BAKFILE" || key == "FILE") return kBackupMethodBakFile;
	if (key == "DIRECTORY" || key == "DIR" || key == "PATH") return kBackupMethodDirectory;
	return std::string();
}

std::string normalizeBackupFrequency(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "FIRST_SAVE_ONLY" || key == "FIRST_SAVE" || key == "FIRST") return kBackupFrequencyFirstSaveOnly;
	if (key == "EVERY_SAVE" || key == "EVERY") return kBackupFrequencyEverySave;
	return std::string();
}

bool normalizeAutosaveSeconds(const std::string &value, int minValue, int maxValue, int &outValue, const char *fieldName, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty()) return setError(errorMessage, std::string(fieldName) + " must be 0 or within " + std::to_string(minValue) + ".." + std::to_string(maxValue) + " seconds.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return setError(errorMessage, std::string(fieldName) + " must be 0 or within " + std::to_string(minValue) + ".." + std::to_string(maxValue) + " seconds.");
	if (parsed != 0 && (parsed < minValue || parsed > maxValue)) return setError(errorMessage, std::string(fieldName) + " must be 0 or within " + std::to_string(minValue) + ".." + std::to_string(maxValue) + " seconds.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr) errorMessage->clear();
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

	if (normalized.empty()) return setError(errorMessage, "BACKUP_EXTENSION may not be empty when BACKUP_METHOD=BAK_FILE.");
	if (normalized.size() > 255) return setError(errorMessage, "BACKUP_EXTENSION may not exceed 255 characters.");
	if (normalized.find_first_of(invalidChars) != std::string::npos) return setError(errorMessage, "BACKUP_EXTENSION contains invalid filename characters.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool validateWritableDirectoryPath(const std::string &path, const char *label, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (normalized.empty()) return setError(errorMessage, std::string(label) + " may not be empty.");
	if (!isWritableDirectory(normalized)) return setError(errorMessage, std::string(label) + " is missing or not writable: " + normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

constexpr int kDefaultBinaryRecordLength = 100;
constexpr int kMinBinaryRecordLength = 1;
constexpr int kMaxBinaryRecordLength = 99999;

bool parseBinaryRecordLengthLiteral(const std::string &value, int &outValue, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty()) return setError(errorMessage, "BINARY_RECORD_LENGTH must be an integer between 1 and 99999.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return setError(errorMessage, "BINARY_RECORD_LENGTH must be an integer between 1 and 99999.");
	if (parsed < kMinBinaryRecordLength || parsed > kMaxBinaryRecordLength) return setError(errorMessage, "BINARY_RECORD_LENGTH must be between 1 and 99999.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

} // namespace

bool applyColorSetupValueInternal(MRColorSetupSettings &configured, const std::string &key, const std::string &value, std::string *errorMessage);

int clampEditFormatTabSize(int tabSize) noexcept {
	return std::max(2, std::min(tabSize, 32));
}

int clampEditFormatRightMargin(int rightMargin) noexcept {
	return std::max(1, std::min(rightMargin, 999));
}

int clampEditFormatLeftMargin(int leftMargin, int rightMargin) noexcept {
	const int normalizedRightMargin = clampEditFormatRightMargin(rightMargin);
	if (normalizedRightMargin <= 1) return 1;
	return std::max(1, std::min(leftMargin, normalizedRightMargin - 1));
}

std::string resolvedEditFormatLineValue(const std::string &value, int tabSize, int leftMargin, int rightMargin, int &resolvedLeftMargin, int &resolvedRightMargin) {
	std::string normalized;

	if (normalizeEditFormatLine(value, tabSize, leftMargin, rightMargin, normalized, &resolvedLeftMargin, &resolvedRightMargin, nullptr)) return normalized;
	resolvedLeftMargin = clampEditFormatLeftMargin(leftMargin, rightMargin);
	resolvedRightMargin = clampEditFormatRightMargin(rightMargin);
	return defaultEditFormatLineForTabSize(tabSize, resolvedLeftMargin, resolvedRightMargin);
}

int nextNumericTabFillColumn(int column, int tabSize) noexcept {
	const int normalizedTabSize = clampEditFormatTabSize(tabSize);
	const int safeColumn = std::max(1, column);
	return ((safeColumn - 1) / normalizedTabSize + 1) * normalizedTabSize + 1;
}

std::string defaultEditFormatLineForTabSize(int tabSize, int leftMargin, int rightMargin) {
	const int normalizedTabSize = clampEditFormatTabSize(tabSize);
	const int normalizedRightMargin = clampEditFormatRightMargin(rightMargin);
	const int normalizedLeftMargin = clampEditFormatLeftMargin(leftMargin, normalizedRightMargin);
	std::string out(static_cast<std::size_t>(normalizedRightMargin), '.');

	if (normalizedRightMargin <= 1) {
		out[0] = 'R';
		return out;
	}
	for (int col = normalizedTabSize; col <= normalizedRightMargin; col += normalizedTabSize)
		if (col > normalizedLeftMargin && col < normalizedRightMargin) out[static_cast<std::size_t>(col - 1)] = '|';
	out[static_cast<std::size_t>(normalizedLeftMargin - 1)] = 'L';
	out[static_cast<std::size_t>(normalizedRightMargin - 1)] = 'R';
	return out;
}

bool normalizeEditFormatLine(const std::string &value, int tabSize, int fallbackLeftMargin, int fallbackRightMargin, std::string &outValue, int *outLeftMargin, int *outRightMargin, std::string *errorMessage) {
	std::string out = value;
	const int normalizedFallbackRightMargin = clampEditFormatRightMargin(fallbackRightMargin);
	const int normalizedFallbackLeftMargin = clampEditFormatLeftMargin(fallbackLeftMargin, normalizedFallbackRightMargin);
	int lCount = 0;
	int rCount = 0;
	int lIndex = -1;
	int rIndex = -1;

	if (out.empty()) {
		outValue = defaultEditFormatLineForTabSize(tabSize, normalizedFallbackLeftMargin, normalizedFallbackRightMargin);
		if (outLeftMargin != nullptr) *outLeftMargin = normalizedFallbackLeftMargin;
		if (outRightMargin != nullptr) *outRightMargin = normalizedFallbackRightMargin;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	{
		bool legacy = true;
		for (char ch : out)
			if (ch != '!' && ch != '-') {
				legacy = false;
				break;
			}
		if (legacy) {
			outValue = defaultEditFormatLineForTabSize(tabSize, normalizedFallbackLeftMargin, normalizedFallbackRightMargin);
			if (outLeftMargin != nullptr) *outLeftMargin = normalizedFallbackLeftMargin;
			if (outRightMargin != nullptr) *outRightMargin = normalizedFallbackRightMargin;
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		}
	}
	for (char &ch : out)
		if (ch == ' ') ch = '.';
	for (std::size_t i = 0; i < out.size(); ++i) {
		char ch = out[i];
		if (ch != '.' && ch != '|' && ch != 'L' && ch != 'R') return setError(errorMessage, "FORMAT_LINE may only contain '.', ' ', '|', 'L' and 'R'.");
		if (ch == 'L') {
			++lCount;
			lIndex = static_cast<int>(i);
		}
		if (ch == 'R') {
			++rCount;
			rIndex = static_cast<int>(i);
		}
	}
	if (lCount > 1) return setError(errorMessage, "FORMAT_LINE must contain at most one 'L'.");
	if (rCount != 1) return setError(errorMessage, "FORMAT_LINE must contain exactly one 'R'.");
	if (lCount == 0) lIndex = 0;
	if (lIndex >= rIndex && rIndex > 0) return setError(errorMessage, "FORMAT_LINE must place 'L' before 'R'.");
	out.resize(static_cast<std::size_t>(rIndex + 1), '.');
	if (rIndex > 0) out[static_cast<std::size_t>(lIndex)] = 'L';
	out[static_cast<std::size_t>(rIndex)] = 'R';
	outValue = out;
	if (outLeftMargin != nullptr) *outLeftMargin = rIndex > 0 ? lIndex + 1 : 1;
	if (outRightMargin != nullptr) *outRightMargin = rIndex + 1;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string synchronizeEditFormatLineMargins(const std::string &value, int leftMargin, int rightMargin, int tabSize) {
	std::string normalized;
	int oldLeftMargin = 1;
	int oldRightMargin = 1;
	const int normalizedRightMargin = clampEditFormatRightMargin(rightMargin);
	const int normalizedLeftMargin = clampEditFormatLeftMargin(leftMargin, normalizedRightMargin);
	std::string out;
	const int delta = normalizedLeftMargin - oldLeftMargin;

	if (!normalizeEditFormatLine(value, tabSize, normalizedLeftMargin, normalizedRightMargin, normalized, &oldLeftMargin, &oldRightMargin, nullptr)) return defaultEditFormatLineForTabSize(tabSize, normalizedLeftMargin, normalizedRightMargin);
	out = std::string(static_cast<std::size_t>(normalizedRightMargin), '.');
	if (normalizedRightMargin <= 1) {
		out[0] = 'R';
		return out;
	}
	out[static_cast<std::size_t>(normalizedLeftMargin - 1)] = 'L';
	out[static_cast<std::size_t>(normalizedRightMargin - 1)] = 'R';
	for (int i = 0; i < static_cast<int>(normalized.size()); ++i) {
		const char ch = normalized[static_cast<std::size_t>(i)];
		const int shifted = i + delta;
		const int column = shifted + 1;

		if (ch != '|') continue;
		if (column <= normalizedLeftMargin || column >= normalizedRightMargin) continue;
		if (shifted < 0 || shifted >= normalizedRightMargin) continue;
		out[static_cast<std::size_t>(shifted)] = '|';
	}
	return out;
}

bool editFormatLineAtColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column, char symbol, std::string &outValue, int *outLeftMargin, int *outRightMargin, std::string *errorMessage) {
	std::string normalized;
	std::string edited;
	int currentLeftMargin = leftMargin;
	int currentRightMargin = rightMargin;
	const char normalizedSymbol = symbol == ' ' ? '.' : symbol;
	const int safeColumn = std::max(1, std::min(column, 999));

	if (normalizedSymbol != '.' && normalizedSymbol != '|' && normalizedSymbol != 'L' && normalizedSymbol != 'R') return setError(errorMessage, "FORMAT_LINE editor accepts only '.', ' ', '|', 'L' and 'R'.");
	if (!normalizeEditFormatLine(value, tabSize, leftMargin, rightMargin, normalized, &currentLeftMargin, &currentRightMargin, errorMessage)) return false;
	edited = normalized;
	if (static_cast<int>(edited.size()) < safeColumn) edited.append(static_cast<std::size_t>(safeColumn - static_cast<int>(edited.size())), '.');
	if (normalizedSymbol == 'L') {
		if (safeColumn >= currentRightMargin) return setError(errorMessage, "FORMAT_LINE must place 'L' before 'R'.");
		for (char &ch : edited)
			if (ch == 'L') ch = '.';
		edited[static_cast<std::size_t>(safeColumn - 1)] = 'L';
	} else if (normalizedSymbol == 'R') {
		if (safeColumn <= currentLeftMargin) return setError(errorMessage, "FORMAT_LINE must place 'R' after 'L'.");
		for (char &ch : edited)
			if (ch == 'R') ch = '.';
		edited.resize(static_cast<std::size_t>(safeColumn), '.');
		edited[static_cast<std::size_t>(safeColumn - 1)] = 'R';
	} else if (normalizedSymbol == '|') {
		if (safeColumn <= currentLeftMargin || safeColumn >= currentRightMargin) {
			outValue = normalized;
			if (outLeftMargin != nullptr) *outLeftMargin = currentLeftMargin;
			if (outRightMargin != nullptr) *outRightMargin = currentRightMargin;
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		}
		edited[static_cast<std::size_t>(safeColumn - 1)] = '|';
	} else {
		if (safeColumn != currentLeftMargin && safeColumn != currentRightMargin && safeColumn <= static_cast<int>(edited.size())) edited[static_cast<std::size_t>(safeColumn - 1)] = '.';
	}
	return normalizeEditFormatLine(edited, tabSize, currentLeftMargin, currentRightMargin, outValue, outLeftMargin, outRightMargin, errorMessage);
}

bool translateEditFormatLine(const std::string &value, int tabSize, int leftMargin, int rightMargin, int deltaColumns, std::string &outValue, int *outLeftMargin, int *outRightMargin, std::string *errorMessage) {
	std::string normalized;
	int currentLeftMargin = leftMargin;
	int currentRightMargin = rightMargin;
	int clampedDelta = deltaColumns;
	std::string translated;

	if (!normalizeEditFormatLine(value, tabSize, leftMargin, rightMargin, normalized, &currentLeftMargin, &currentRightMargin, errorMessage)) return false;
	clampedDelta = std::max(1 - currentLeftMargin, std::min(deltaColumns, 999 - currentRightMargin));
	if (currentRightMargin <= 1) {
		outValue = "R";
		if (outLeftMargin != nullptr) *outLeftMargin = 1;
		if (outRightMargin != nullptr) *outRightMargin = 1;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	translated.assign(static_cast<std::size_t>(currentRightMargin + clampedDelta), '.');
	translated[static_cast<std::size_t>(currentLeftMargin + clampedDelta - 1)] = 'L';
	translated[static_cast<std::size_t>(currentRightMargin + clampedDelta - 1)] = 'R';
	for (int i = 0; i < static_cast<int>(normalized.size()); ++i) {
		const int shiftedColumn = i + clampedDelta + 1;
		if (normalized[static_cast<std::size_t>(i)] != '|') continue;
		if (shiftedColumn <= currentLeftMargin + clampedDelta || shiftedColumn >= currentRightMargin + clampedDelta) continue;
		translated[static_cast<std::size_t>(shiftedColumn - 1)] = '|';
	}
	return normalizeEditFormatLine(translated, tabSize, currentLeftMargin + clampedDelta, currentRightMargin + clampedDelta, outValue, outLeftMargin, outRightMargin, errorMessage);
}

int nextResolvedEditFormatTabStopColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column) {
	int resolvedLeftMargin = 1;
	int resolvedRightMargin = 1;
	const std::string normalized = resolvedEditFormatLineValue(value, tabSize, leftMargin, rightMargin, resolvedLeftMargin, resolvedRightMargin);
	const int safeColumn = std::max(1, column);

	for (int i = safeColumn; i < static_cast<int>(normalized.size()); ++i)
		if (normalized[static_cast<std::size_t>(i)] == '|') return i + 1;
	return safeColumn;
}

int prevResolvedEditFormatTabStopColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column) {
	int resolvedLeftMargin = 1;
	int resolvedRightMargin = 1;
	const std::string normalized = resolvedEditFormatLineValue(value, tabSize, leftMargin, rightMargin, resolvedLeftMargin, resolvedRightMargin);
	const int safeColumn = std::max(1, column);
	int i = std::min(std::max(0, safeColumn - 2), static_cast<int>(normalized.size()) - 1);

	for (; i >= 0; --i)
		if (normalized[static_cast<std::size_t>(i)] == '|') return i + 1;
	if (safeColumn > resolvedLeftMargin) return resolvedLeftMargin;
	return safeColumn;
}

int resolvedEditFormatTabDisplayColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column) {
	const int safeColumn = std::max(1, column);
	const int resolvedTabStop = nextResolvedEditFormatTabStopColumn(value, tabSize, leftMargin, rightMargin, safeColumn);

	if (resolvedTabStop > safeColumn) return resolvedTabStop;
	return nextNumericTabFillColumn(safeColumn, tabSize);
}

int resolvedEditFormatIndentColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int preferredColumn) {
	int resolvedLeftMargin = 1;
	int resolvedRightMargin = 1;
	const std::string normalized = resolvedEditFormatLineValue(value, tabSize, leftMargin, rightMargin, resolvedLeftMargin, resolvedRightMargin);
	const int safePreferredColumn = std::max(1, preferredColumn);
	int resolvedColumn = resolvedLeftMargin;

	if (safePreferredColumn <= resolvedLeftMargin) return resolvedLeftMargin;
	for (int i = 0; i < static_cast<int>(normalized.size()); ++i) {
		if (normalized[static_cast<std::size_t>(i)] != '|') continue;
		if (i + 1 > safePreferredColumn) break;
		resolvedColumn = i + 1;
	}
	return resolvedColumn;
}

std::string buildEditIndentFill(const MREditSetupSettings &settings, int startColumn, int targetColumn, bool preferTabs) {
	std::string out;
	int currentColumn = std::max(1, startColumn);
	const int safeTargetColumn = std::max(currentColumn, targetColumn);

	while (currentColumn < safeTargetColumn) {
		const int nextTabColumn = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);
		if (preferTabs && nextTabColumn <= safeTargetColumn) {
			out.push_back('\t');
			currentColumn = nextTabColumn;
		} else {
			out.push_back(' ');
			++currentColumn;
		}
	}
	return out;
}

namespace {

bool normalizeCursorStatusColor(const std::string &value, std::string &outValue, std::string *errorMessage) {
	std::string normalized = upperAscii(trimAscii(value));
	unsigned char parsed = 0;
	static const char *const hex = "0123456789ABCDEF";

	if (normalized.empty()) {
		outValue.clear();
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (!parseHexColorToken(normalized, parsed)) return setError(errorMessage, "CURSOR_STATUS_COLOR must be a hex byte (00..FF) or empty.");
	outValue.clear();
	outValue.push_back(hex[(parsed >> 4) & 0x0F]);
	outValue.push_back(hex[parsed & 0x0F]);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseTabSizeLiteral(const std::string &value, int &outValue, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty()) return setError(errorMessage, "TAB_SIZE must be an integer between 2 and 32.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return setError(errorMessage, "TAB_SIZE must be an integer between 2 and 32.");
	if (parsed < kMinTabSize || parsed > kMaxTabSize) return setError(errorMessage, "TAB_SIZE must be between 2 and 32.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseMiniMapWidthLiteral(const std::string &value, int &outValue, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty()) return setError(errorMessage, "MINIMAP_WIDTH must be between 2 and 20.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return setError(errorMessage, "MINIMAP_WIDTH must be an integer between 2 and 20.");
	if (parsed < kMinMiniMapWidth || parsed > kMaxMiniMapWidth) return setError(errorMessage, "MINIMAP_WIDTH must be between 2 and 20.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

constexpr int kDefaultLeftMargin = 1;
constexpr int kMinLeftMargin = 1;
constexpr int kMaxLeftMargin = 999;
constexpr int kDefaultRightMargin = 78;
constexpr int kMinRightMargin = 1;
constexpr int kMaxRightMargin = 999;

bool parseLeftMarginLiteral(const std::string &value, int &outValue, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty()) return setError(errorMessage, "LEFT_MARGIN must be an integer between 1 and 999.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return setError(errorMessage, "LEFT_MARGIN must be an integer between 1 and 999.");
	if (parsed < kMinLeftMargin || parsed > kMaxLeftMargin) return setError(errorMessage, "LEFT_MARGIN must be between 1 and 999.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseRightMarginLiteral(const std::string &value, int &outValue, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty()) return setError(errorMessage, "RIGHT_MARGIN must be an integer between 1 and 999.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return setError(errorMessage, "RIGHT_MARGIN must be an integer between 1 and 999.");
	if (parsed < kMinRightMargin || parsed > kMaxRightMargin) return setError(errorMessage, "RIGHT_MARGIN must be between 1 and 999.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string normalizePageBreakLiteral(const std::string &value) {
	std::string trimmed = trimAscii(value);

	if (trimmed.empty()) return kDefaultPageBreakLiteral;
	while (trimmed.size() > 2 && trimmed[0] == '\\' && trimmed[1] == '\\')
		trimmed.erase(trimmed.begin());
	if (trimmed == "\\F") return "\\f";
	if (trimmed == "\\N") return "\\n";
	if (trimmed == "\\R") return "\\r";
	if (trimmed == "\\T") return "\\t";
	if (trimmed == "\\f" || trimmed == "\\n" || trimmed == "\\r" || trimmed == "\\t") return trimmed;
	if (trimmed.size() == 1) {
		unsigned char ch = static_cast<unsigned char>(trimmed[0]);
		if (ch == '\f') return "\\f";
		if (ch == '\n') return "\\n";
		if (ch == '\r') return "\\r";
		if (ch == '\t') return "\\t";
	}
	return trimmed;
}

char decodePageBreakLiteral(const std::string &literal) {
	std::string value = normalizePageBreakLiteral(literal);

	if (value == "\\f") return '\f';
	if (value == "\\n") return '\n';
	if (value == "\\r") return '\r';
	if (value == "\\t") return '\t';
	return value.empty() ? '\f' : value[0];
}

std::vector<std::string> parseDefaultExtensions(const std::string &value) {
	std::string text = trimAscii(value);
	std::vector<std::string> out;
	std::string token;
	std::size_t i = 0;

	if (text.size() >= 2 && std::isalpha(static_cast<unsigned char>(text[0])) != 0 && text[1] == ':') text = text.substr(2);

	for (i = 0; i <= text.size(); ++i) {
		char ch = (i < text.size()) ? text[i] : ';';
		if (ch == ';' || ch == ':' || ch == ',' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
			std::string ext = trimAscii(token);
			bool duplicate = false;

			token.clear();
			if (ext.empty()) continue;
			while (!ext.empty() && ext[0] == '.')
				ext.erase(ext.begin());
			ext = trimAscii(ext);
			if (ext.empty()) continue;
			for (const auto &j : out)
				if (j == ext) {
					duplicate = true;
					break;
				}
			if (!duplicate) out.push_back(ext);
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
		if (i != 0) out.push_back(';');
		out += list[i];
	}
	return out;
}

const MREditSettingDescriptor *editSettingDescriptorByKeyInternal(const std::string &key) {
	std::string upper = upperAscii(trimAscii(key));

	for (const auto &descriptor : kEditSettingDescriptors)
		if (upper == descriptor.key) return &descriptor;
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
	if (trimmed.empty()) return std::string();
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
		if (std::isspace(static_cast<unsigned char>(ch)) != 0) return true;
	return false;
}

bool normalizeEditExtensionSelectorsInPlace(std::vector<std::string> &selectors, std::string *errorMessage) {
	std::vector<std::string> normalized;
	std::set<std::string> seen;

	normalized.reserve(selectors.size());
	for (const std::string &selector : selectors) {
		std::string ext = normalizeEditExtensionSelectorValue(selector);

		if (ext.empty()) continue;
		if (containsAsciiSpace(ext)) return setError(errorMessage, "Extensions may not contain whitespace.");
		if (ext.find('/') != std::string::npos || ext.find('\\') != std::string::npos) return setError(errorMessage, "Extensions may not contain path separators.");
		if (seen.insert(ext).second) normalized.push_back(ext);
	}
	selectors.swap(normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseAndAssignBooleanLiteral(const std::string &value, bool &target, std::string *errorMessage) {
	bool parsed = false;

	if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
	target = parsed;
	return true;
}

std::string extensionSelectorForPath(std::string_view path) {
	std::string normalized = normalizeDialogPath(std::string(path).c_str());
	std::string_view base = normalized;
	std::size_t sep = base.find_last_of("/\\");

	if (sep != std::string::npos) base.remove_prefix(sep + 1);
	std::size_t dot = base.find_last_of('.');
	if (base.empty() || dot == std::string::npos || dot + 1 >= base.size()) return std::string();
	return std::string(base.substr(dot + 1));
}

bool applyEditSetupValueInternal(MREditSetupSettings &current, const std::string &keyName, const std::string &value, std::string *errorMessage) {
	std::string upperKeyName = upperAscii(trimAscii(keyName));
	std::string normalized;

	if (upperKeyName == "PAGE_BREAK") current.pageBreak = normalizePageBreakLiteral(value);
	else if (upperKeyName == "WORD_DELIMITERS") {
		if (trimAscii(value).empty()) current.wordDelimiters = resolveEditSetupDefaults().wordDelimiters;
		else
			current.wordDelimiters = value;
	} else if (upperKeyName == "DEFAULT_EXTENSIONS")
		current.defaultExtensions = canonicalDefaultExtensionsLiteral(value);
	else if (upperKeyName == "TRUNCATE_SPACES") {
		if (!parseAndAssignBooleanLiteral(value, current.truncateSpaces, errorMessage)) return false;
	} else if (upperKeyName == "EOF_CTRL_Z") {
		if (!parseAndAssignBooleanLiteral(value, current.eofCtrlZ, errorMessage)) return false;
	} else if (upperKeyName == "EOF_CR_LF") {
		if (!parseAndAssignBooleanLiteral(value, current.eofCrLf, errorMessage)) return false;
	} else if (upperKeyName == "TAB_EXPAND") {
		if (!parseAndAssignBooleanLiteral(value, current.tabExpand, errorMessage)) return false;
	} else if (upperKeyName == "DISPLAY_TABS") {
		if (!parseAndAssignBooleanLiteral(value, current.displayTabs, errorMessage)) return false;
	} else if (upperKeyName == "TAB_SIZE") {
		int tabSize = 0;
		if (!parseTabSizeLiteral(value, tabSize, errorMessage)) return false;
		current.tabSize = tabSize;
		// Changing tab size resets the format line to the canonical tab/margin layout.
		current.formatLine = defaultEditFormatLineForTabSize(current.tabSize, current.leftMargin, current.rightMargin);
	} else if (upperKeyName == "LEFT_MARGIN") {
		int leftMargin = 0;
		if (!parseLeftMarginLiteral(value, leftMargin, errorMessage)) return false;
		current.leftMargin = leftMargin;
		current.formatLine = synchronizeEditFormatLineMargins(current.formatLine, current.leftMargin, current.rightMargin, current.tabSize);
	} else if (upperKeyName == "RIGHT_MARGIN") {
		int rightMargin = 0;
		if (!parseRightMarginLiteral(value, rightMargin, errorMessage)) return false;
		current.rightMargin = rightMargin;
		current.formatLine = synchronizeEditFormatLineMargins(current.formatLine, current.leftMargin, current.rightMargin, current.tabSize);
	} else if (upperKeyName == "FORMAT_RULER") {
		if (!parseAndAssignBooleanLiteral(value, current.formatRuler, errorMessage)) return false;
	} else if (upperKeyName == "WORD_WRAP") {
		if (!parseAndAssignBooleanLiteral(value, current.wordWrap, errorMessage)) return false;
	} else if (upperKeyName == "INDENT_STYLE") {
		normalized = normalizeIndentStyle(value);
		if (normalized.empty()) return setError(errorMessage, "INDENT_STYLE must be OFF, AUTOMATIC or SMART.");
		current.indentStyle = normalized;
	} else if (upperKeyName == "CODE_LANGUAGE") {
		normalized = upperAscii(trimAscii(value));
		if (normalized.empty()) normalized = "NONE";
		if (normalized != "NONE" && normalized != "AUTO" && normalized != "C" && normalized != "CPP" && normalized != "PYTHON" && normalized != "JAVASCRIPT" && normalized != "TYPESCRIPT" && normalized != "TSX" && normalized != "BASH" && normalized != "JSON" && normalized != "PERL" && normalized != "SWIFT")
			return setError(errorMessage, "CODE_LANGUAGE must be NONE, AUTO, C, CPP, PYTHON, JAVASCRIPT, TYPESCRIPT, TSX, BASH, JSON, PERL or SWIFT.");
		current.codeLanguage = normalized;
	} else if (upperKeyName == "CODE_COLORING") {
		if (!parseAndAssignBooleanLiteral(value, current.codeColoring, errorMessage)) return false;
	} else if (upperKeyName == "CODE_FOLDING") {
		if (!parseAndAssignBooleanLiteral(value, current.codeFoldingFeature, errorMessage)) return false;
	} else if (upperKeyName == "SMART_INDENTING") {
		if (!parseAndAssignBooleanLiteral(value, current.smartIndenting, errorMessage)) return false;
	} else if (upperKeyName == "FILE_TYPE") {
		normalized = normalizeFileType(value);
		if (normalized.empty()) return setError(errorMessage, "FILE_TYPE must be LEGACY_TEXT, UNIX or BINARY.");
		current.fileType = normalized;
	} else if (upperKeyName == "BINARY_RECORD_LENGTH") {
		int binaryRecordLength = 0;
		if (!parseBinaryRecordLengthLiteral(value, binaryRecordLength, errorMessage)) return false;
		current.binaryRecordLength = binaryRecordLength;
	} else if (upperKeyName == "POST_LOAD_MACRO")
		current.postLoadMacro = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
	else if (upperKeyName == "PRE_SAVE_MACRO")
		current.preSaveMacro = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
	else if (upperKeyName == "DEFAULT_PATH")
		current.defaultPath = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
	else if (upperKeyName == "FORMAT_LINE") {
		int leftMargin = current.leftMargin;
		int rightMargin = current.rightMargin;
		if (!normalizeEditFormatLine(value, current.tabSize, current.leftMargin, current.rightMargin, normalized, &leftMargin, &rightMargin, errorMessage)) return false;
		current.formatLine = normalized;
		current.leftMargin = leftMargin;
		current.rightMargin = rightMargin;
	} else if (upperKeyName == "BACKUP_FILES") {
		if (!parseAndAssignBooleanLiteral(value, current.backupFiles, errorMessage)) return false;
		if (!current.backupFiles) current.backupMethod = kBackupMethodOff;
		else if (normalizeBackupMethod(current.backupMethod).empty() || current.backupMethod == kBackupMethodOff)
			current.backupMethod = kBackupMethodBakFile;
	} else if (upperKeyName == "BACKUP_METHOD") {
		normalized = normalizeBackupMethod(value);
		if (normalized.empty()) return setError(errorMessage, "BACKUP_METHOD must be OFF, BAK_FILE or DIRECTORY.");
		current.backupMethod = normalized;
		current.backupFiles = normalized != kBackupMethodOff;
	} else if (upperKeyName == "BACKUP_FREQUENCY") {
		normalized = normalizeBackupFrequency(value);
		if (normalized.empty()) return setError(errorMessage, "BACKUP_FREQUENCY must be FIRST_SAVE_ONLY or EVERY_SAVE.");
		current.backupFrequency = normalized;
	} else if (upperKeyName == "BACKUP_EXTENSION")
		current.backupExtension = normalizeBackupExtension(value);
	else if (upperKeyName == "BACKUP_DIRECTORY")
		current.backupDirectory = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
	else if (upperKeyName == "AUTOSAVE_INACTIVITY_SECONDS") {
		int parsedSeconds = 0;
		if (!normalizeAutosaveSeconds(value, kMinAutosaveInactivitySeconds, kMaxAutosaveInactivitySeconds, parsedSeconds, "AUTOSAVE_INACTIVITY_SECONDS", errorMessage)) return false;
		current.autosaveInactivitySeconds = parsedSeconds;
	} else if (upperKeyName == "AUTOSAVE_INTERVAL_SECONDS") {
		int parsedSeconds = 0;
		if (!normalizeAutosaveSeconds(value, kMinAutosaveIntervalSeconds, kMaxAutosaveIntervalSeconds, parsedSeconds, "AUTOSAVE_INTERVAL_SECONDS", errorMessage)) return false;
		current.autosaveIntervalSeconds = parsedSeconds;
	} else if (upperKeyName == "SHOW_EOF_MARKER") {
		if (!parseAndAssignBooleanLiteral(value, current.showEofMarker, errorMessage)) return false;
	} else if (upperKeyName == "SHOW_EOF_MARKER_EMOJI") {
		if (!parseAndAssignBooleanLiteral(value, current.showEofMarkerEmoji, errorMessage)) return false;
	} else if (upperKeyName == "LINE_NUMBERS_POSITION") {
		normalized = normalizeLineNumbersPosition(value);
		if (normalized.empty()) return setError(errorMessage, "LINE_NUMBERS_POSITION must be OFF, LEADING or TRAILING.");
		current.lineNumbersPosition = normalized;
		current.showLineNumbers = normalized != kLineNumbersPositionOff;
	} else if (upperKeyName == "LINE_NUM_ZERO_FILL") {
		if (!parseAndAssignBooleanLiteral(value, current.lineNumZeroFill, errorMessage)) return false;
	} else if (upperKeyName == "MINIMAP_POSITION") {
		normalized = normalizeMiniMapPosition(value);
		if (normalized.empty()) return setError(errorMessage, "MINIMAP_POSITION must be OFF, LEADING or TRAILING.");
		current.miniMapPosition = normalized;
	} else if (upperKeyName == "MINIMAP_WIDTH") {
		int miniMapWidth = 0;
		if (!parseMiniMapWidthLiteral(value, miniMapWidth, errorMessage)) return false;
		current.miniMapWidth = miniMapWidth;
	} else if (upperKeyName == "MINIMAP_MARKER_GLYPH") {
		if (!normalizeMiniMapMarkerGlyph(value, normalized, errorMessage)) return false;
		current.miniMapMarkerGlyph = normalized;
	} else if (upperKeyName == "GUTTERS")
		current.gutters = normalizeGuttersOrder(value);
	else if (upperKeyName == "PERSISTENT_BLOCKS") {
		if (!parseAndAssignBooleanLiteral(value, current.persistentBlocks, errorMessage)) return false;
	} else if (upperKeyName == "CODE_FOLDING_POSITION") {
		normalized = normalizeCodeFoldingPosition(value);
		if (normalized.empty()) return setError(errorMessage, "CODE_FOLDING_POSITION must be OFF, LEADING or TRAILING.");
		current.codeFoldingPosition = normalized;
		current.codeFolding = normalized != kCodeFoldingPositionOff;
	} else if (upperKeyName == "COLUMN_BLOCK_MOVE") {
		normalized = normalizeColumnBlockMove(value);
		if (normalized.empty()) return setError(errorMessage, "COLUMN_BLOCK_MOVE must be DELETE_SPACE or LEAVE_SPACE.");
		current.columnBlockMove = normalized;
	} else if (upperKeyName == "DEFAULT_MODE") {
		normalized = normalizeDefaultMode(value);
		if (normalized.empty()) return setError(errorMessage, "DEFAULT_MODE must be INSERT or OVERWRITE.");
		current.defaultMode = normalized;
	} else if (upperKeyName == "CURSOR_STATUS_COLOR") {
		if (!normalizeCursorStatusColor(value, normalized, errorMessage)) return false;
		current.cursorStatusColor = normalized;
	} else
		return setError(errorMessage, "Unknown edit setting key.");

	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string editSetupValueLiteral(const MREditSetupSettings &settings, const char *key) {
	std::string upperKey = upperAscii(trimAscii(key != nullptr ? key : ""));

	if (upperKey == "PAGE_BREAK") return settings.pageBreak;
	if (upperKey == "WORD_DELIMITERS") return settings.wordDelimiters;
	if (upperKey == "DEFAULT_EXTENSIONS") return settings.defaultExtensions;
	if (upperKey == "TRUNCATE_SPACES") return formatEditSetupBoolean(settings.truncateSpaces);
	if (upperKey == "EOF_CTRL_Z") return formatEditSetupBoolean(settings.eofCtrlZ);
	if (upperKey == "EOF_CR_LF") return formatEditSetupBoolean(settings.eofCrLf);
	if (upperKey == "TAB_EXPAND") return formatEditSetupBoolean(settings.tabExpand);
	if (upperKey == "DISPLAY_TABS") return formatEditSetupBoolean(settings.displayTabs);
	if (upperKey == "TAB_SIZE") return std::to_string(settings.tabSize);
	if (upperKey == "LEFT_MARGIN") return std::to_string(settings.leftMargin);
	if (upperKey == "RIGHT_MARGIN") return std::to_string(settings.rightMargin);
	if (upperKey == "FORMAT_RULER") return formatEditSetupBoolean(settings.formatRuler);
	if (upperKey == "WORD_WRAP") return formatEditSetupBoolean(settings.wordWrap);
	if (upperKey == "INDENT_STYLE") return settings.indentStyle;
	if (upperKey == "CODE_LANGUAGE") return settings.codeLanguage;
	if (upperKey == "CODE_COLORING") return formatEditSetupBoolean(settings.codeColoring);
	if (upperKey == "CODE_FOLDING") return formatEditSetupBoolean(settings.codeFoldingFeature);
	if (upperKey == "SMART_INDENTING") return formatEditSetupBoolean(settings.smartIndenting);
	if (upperKey == "FILE_TYPE") return settings.fileType;
	if (upperKey == "BINARY_RECORD_LENGTH") return std::to_string(settings.binaryRecordLength);
	if (upperKey == "POST_LOAD_MACRO") return settings.postLoadMacro;
	if (upperKey == "PRE_SAVE_MACRO") return settings.preSaveMacro;
	if (upperKey == "DEFAULT_PATH") return settings.defaultPath;
	if (upperKey == "FORMAT_LINE") return settings.formatLine;
	if (upperKey == "BACKUP_FILES") return formatEditSetupBoolean(settings.backupFiles);
	if (upperKey == "BACKUP_METHOD") return settings.backupMethod;
	if (upperKey == "BACKUP_FREQUENCY") return settings.backupFrequency;
	if (upperKey == "BACKUP_EXTENSION") return settings.backupExtension;
	if (upperKey == "BACKUP_DIRECTORY") return settings.backupDirectory;
	if (upperKey == "AUTOSAVE_INACTIVITY_SECONDS") return std::to_string(settings.autosaveInactivitySeconds);
	if (upperKey == "AUTOSAVE_INTERVAL_SECONDS") return std::to_string(settings.autosaveIntervalSeconds);
	if (upperKey == "SHOW_EOF_MARKER") return formatEditSetupBoolean(settings.showEofMarker);
	if (upperKey == "SHOW_EOF_MARKER_EMOJI") return formatEditSetupBoolean(settings.showEofMarkerEmoji);
	if (upperKey == "LINE_NUMBERS_POSITION") return settings.lineNumbersPosition;
	if (upperKey == "LINE_NUM_ZERO_FILL") return formatEditSetupBoolean(settings.lineNumZeroFill);
	if (upperKey == "MINIMAP_POSITION") return settings.miniMapPosition;
	if (upperKey == "MINIMAP_WIDTH") return std::to_string(settings.miniMapWidth);
	if (upperKey == "MINIMAP_MARKER_GLYPH") return settings.miniMapMarkerGlyph;
	if (upperKey == "GUTTERS") return settings.gutters;
	if (upperKey == "PERSISTENT_BLOCKS") return formatEditSetupBoolean(settings.persistentBlocks);
	if (upperKey == "CODE_FOLDING_POSITION") return settings.codeFoldingPosition;
	if (upperKey == "COLUMN_BLOCK_MOVE") return settings.columnBlockMove;
	if (upperKey == "DEFAULT_MODE") return settings.defaultMode;
	if (upperKey == "CURSOR_STATUS_COLOR") return settings.cursorStatusColor;
	return std::string();
}

unsigned long long supportedEditProfileOverrideMask() noexcept {
	static constexpr unsigned long long mask = kOvPageBreak | kOvWordDelimiters | kOvDefaultExtensions | kOvTruncateSpaces | kOvEofCtrlZ | kOvEofCrLf | kOvTabExpand | kOvDisplayTabs | kOvTabSize | kOvLeftMargin | kOvRightMargin | kOvFormatRuler | kOvWordWrap | kOvIndentStyle | kOvCodeLanguage | kOvCodeColoring | kOvCodeFoldingFeature | kOvSmartIndenting | kOvFileType | kOvBinaryRecordLength | kOvPostLoadMacro | kOvPreSaveMacro | kOvDefaultPath | kOvFormatLine | kOvBackupFiles | kOvShowEofMarker | kOvShowEofMarkerEmoji | kOvLineNumZeroFill | kOvLineNumbersPosition | kOvMiniMapPosition | kOvMiniMapWidth | kOvMiniMapMarkerGlyph | kOvGutters | kOvPersistentBlocks | kOvCodeFoldingPosition | kOvColumnBlockMove | kOvDefaultMode | kOvCursorStatusColor;
	return mask;
}

bool normalizeEditProfileOverridesInPlace(MREditExtensionProfile &profile, std::string *errorMessage) {
	std::size_t descriptorCount = 0;
	const MREditSettingDescriptor *descriptors = editSettingDescriptors(descriptorCount);
	MREditSetupSettings normalizedValues = resolveEditSetupDefaults();
	unsigned long long mask = profile.overrides.mask;

	if ((mask & ~supportedEditProfileOverrideMask()) != 0) return setError(errorMessage, "Extension profile override mask contains unsupported bits.");

	for (std::size_t i = 0; i < descriptorCount; ++i) {
		const MREditSettingDescriptor &descriptor = descriptors[i];

		if (!descriptor.profileSupported) continue;
		if ((mask & descriptor.overrideBit) == 0) continue;
		if (!applyEditSetupValueInternal(normalizedValues, descriptor.key, editSetupValueLiteral(profile.overrides.values, descriptor.key), errorMessage)) return false;
	}

	profile.overrides.mask = mask;
	profile.overrides.values = normalizedValues;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool validateNormalizedEditProfiles(const std::vector<MREditExtensionProfile> &profiles, std::string *errorMessage) {
	std::set<std::string> profileIds;
	std::set<std::string> selectors;

	std::map<std::string, std::string> selectorOwners;

	for (const auto &profile : profiles) {
		std::string id = canonicalEditProfileId(profile.id);
		std::string name = canonicalEditProfileName(profile.name);
		std::string lookup = profileIdLookupKey(id);

		if (id.empty()) return setError(errorMessage, "Extension profile id may not be empty.");
		if (name.empty()) return setError(errorMessage, "Extension profile name may not be empty.");
		if (!profileIds.insert(lookup).second) return setError(errorMessage, "Duplicate extension profile id: " + id + " (" + name + ")");
		if (!profile.windowColorThemeUri.empty() && !validateColorThemeFilePath(profile.windowColorThemeUri, errorMessage)) return false;
		for (const std::string &ext : profile.extensions) {
			std::string ownerLabel = id + " (" + name + ")";
			auto it = selectorOwners.find(ext);
			if (it != selectorOwners.end()) return setError(errorMessage, "Duplicate profile extension '" + ext + "': " + it->second + " and " + ownerLabel);
			selectorOwners.insert(std::make_pair(ext, ownerLabel));
		}
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

} // namespace

bool parseHistoryLimitLiteral(const std::string &value, int &outValue, std::string *errorMessage, const char *keyName);
bool setConfiguredPathHistoryLimitValue(int value, std::string *errorMessage);
bool setConfiguredFileHistoryLimitValue(int value, std::string *errorMessage);
bool setScopedDialogLastPath(MRDialogHistoryScope scope, const std::string &path, std::string *errorMessage);
std::string buildSettingsMacroSource(const MRSettingsSnapshot &snapshot);

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

MREditSetupSettings resolveEditSetupDefaults() {
	MREditSetupSettings defaults;

	defaults.pageBreak = kDefaultPageBreakLiteral;
	defaults.wordDelimiters = kDefaultWordDelimiters;
	defaults.defaultExtensions = kDefaultDefaultExtensions;
	defaults.truncateSpaces = true;
	defaults.eofCtrlZ = false;
	defaults.eofCrLf = false;
	defaults.tabExpand = true;
	defaults.displayTabs = false;
	defaults.tabSize = kDefaultTabSize;
	defaults.leftMargin = kDefaultLeftMargin;
	defaults.rightMargin = kDefaultRightMargin;
	defaults.formatRuler = false;
	defaults.wordWrap = true;
	defaults.indentStyle = kIndentStyleOff;
	defaults.codeLanguage = "NONE";
	defaults.codeColoring = false;
	defaults.codeFoldingFeature = false;
	defaults.smartIndenting = false;
	defaults.fileType = kFileTypeUnix;
	defaults.binaryRecordLength = kDefaultBinaryRecordLength;
	defaults.postLoadMacro.clear();
	defaults.preSaveMacro.clear();
	defaults.defaultPath.clear();
	defaults.formatLine = defaultEditFormatLineForTabSize(defaults.tabSize, defaults.leftMargin, defaults.rightMargin);
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
	defaults.lineNumbersPosition = kLineNumbersPositionOff;
	defaults.lineNumZeroFill = false;
	defaults.miniMapPosition = kMiniMapPositionOff;
	defaults.miniMapWidth = kDefaultMiniMapWidth;
	defaults.miniMapMarkerGlyph = "│";
	defaults.gutters = kDefaultGuttersOrder;
	defaults.persistentBlocks = true;
	defaults.codeFolding = false;
	defaults.codeFoldingPosition = kCodeFoldingPositionOff;
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

	if (upper.empty()) return MRSettingsKeyClass::Unknown;
	for (const auto &descriptor : kFixedSettingsKeyDescriptors)
		if (upper == descriptor.key) return descriptor.keyClass;
	return editSettingDescriptorByKeyInternal(upper) != nullptr ? MRSettingsKeyClass::Edit : MRSettingsKeyClass::Unknown;
}

bool isCanonicalSerializedSettingsKey(std::string_view key) {
	std::string upper = upperAscii(trimAscii(key));

	if (upper.empty()) return false;
	for (const auto &descriptor : kFixedSettingsKeyDescriptors)
		if (upper == descriptor.key) return descriptor.serialized;
	return editSettingDescriptorByKeyInternal(upper) != nullptr;
}

std::size_t canonicalSerializedSettingsKeyCount() {
	std::size_t editDescriptorCount = 0;
	std::size_t fixedSerializedCount = 0;
	std::size_t serializedEditCount = 0;
	editSettingDescriptors(editDescriptorCount);

	for (const auto &descriptor : kFixedSettingsKeyDescriptors)
		if (descriptor.serialized) ++fixedSerializedCount;
	for (std::size_t i = 0; i < editDescriptorCount; ++i)
		++serializedEditCount;
	return fixedSerializedCount + serializedEditCount;
}

bool resetConfiguredSettingsModel(const std::string &settingsPath, MRSetupPaths &paths, std::string *errorMessage) {
	paths = resolveSetupPathDefaults();
	paths.settingsMacroUri = normalizeConfiguredPathInput(settingsPath);
	if (paths.settingsMacroUri.empty()) return setError(errorMessage, "Settings path is empty.");
	if (!setConfiguredSettingsMacroFilePath(paths.settingsMacroUri, errorMessage)) return false;
	if (!setConfiguredMacroDirectoryPath(paths.macroPath, errorMessage)) return false;
	if (!setConfiguredHelpFilePath(paths.helpUri, errorMessage)) return false;
	if (!setConfiguredTempDirectoryPath(paths.tempPath, errorMessage)) return false;
	if (!setConfiguredShellExecutablePath(paths.shellUri, errorMessage)) return false;
	if (!setConfiguredLogFilePath(defaultLogFilePathForSettings(paths.settingsMacroUri), errorMessage)) return false;
	if (!setConfiguredLastFileDialogPath(paths.macroPath, errorMessage)) return false;
	if (!setConfiguredDefaultProfileDescription("Global defaults", errorMessage)) return false;
	if (!setConfiguredSearchDialogOptions(MRSearchDialogOptions(), errorMessage)) return false;
	if (!setConfiguredSarDialogOptions(MRSarDialogOptions(), errorMessage)) return false;
	if (!setConfiguredMultiSearchDialogOptions(MRMultiSearchDialogOptions(), errorMessage)) return false;
	if (!setConfiguredMultiSarDialogOptions(MRMultiSarDialogOptions(), errorMessage)) return false;
	if (!setConfiguredCursorBehaviour(MRCursorBehaviour::BoundToText, errorMessage)) return false;
	if (!setConfiguredCursorPositionMarker("R:C", errorMessage)) return false;
	g_logHandling = MRLogHandling::Volatile;
	configuredAutoexecMacroStorage().clear();
	if (!setConfiguredEditSetupSettings(resolveEditSetupDefaults(), errorMessage)) return false;
	configuredColorSettings() = defaultsFromColorGroups();
	configuredColorSettingsInitialized() = true;
	if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(), errorMessage)) return false;
	if (!setConfiguredKeymapProfiles(std::vector<MRKeymapProfile>(), errorMessage)) return false;
	if (!setConfiguredKeymapFilePath("", errorMessage)) return false;
	if (!setConfiguredActiveKeymapProfile("DEFAULT", errorMessage)) return false;
	if (!setConfiguredColorThemeFilePath(defaultColorThemeFilePath(), errorMessage)) return false;
	configuredPathHistoryLimit() = kHistoryLimitDefault;
	configuredFileHistoryLimit() = kHistoryLimitDefault;
	for (MRScopedDialogHistoryState &state : configuredDialogHistoryStorage()) {
		state.lastPath.clear();
		state.pathHistory.clear();
		state.fileHistory.clear();
	}
	configuredMultiFilespecHistoryStorage().clear();
	configuredMultiPathHistoryStorage().clear();
	configuredHistoryEpochCounter() = std::max(static_cast<long long>(0), static_cast<long long>(std::time(nullptr)));
	clearConfiguredSettingsDirty();
	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = configuredMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool applyConfiguredSettingsAssignment(const std::string &key, const std::string &value, MRSetupPaths &paths, std::string *errorMessage) {
	switch (classifySettingsKey(key)) {
		case MRSettingsKeyClass::Unknown:
			return setError(errorMessage, "Unsupported MRSETUP key.");
		case MRSettingsKeyClass::Version:
			if (trimAscii(value) != kCurrentSettingsVersion) return setError(errorMessage, "Unsupported settings version.");
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		case MRSettingsKeyClass::Path: {
			std::string upper = upperAscii(trimAscii(key));
			if (upper == "SETTINGSPATH") {
				paths.settingsMacroUri = configuredSettingsMacroFilePath();
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MACROPATH") {
				if (!validateMacroDirectoryPath(value, errorMessage)) return false;
				if (!setConfiguredMacroDirectoryPath(value, errorMessage)) return false;
				paths.macroPath = normalizeConfiguredPathInput(value);
				return true;
			}
			if (upper == "HELPPATH") {
				if (!validateHelpFilePath(value, errorMessage)) return false;
				if (!setConfiguredHelpFilePath(value, errorMessage)) return false;
				paths.helpUri = normalizeConfiguredPathInput(value);
				return true;
			}
			if (upper == "TEMPDIR") {
				if (!validateTempDirectoryPath(value, errorMessage)) return false;
				if (!setConfiguredTempDirectoryPath(value, errorMessage)) return false;
				paths.tempPath = normalizeConfiguredPathInput(value);
				return true;
			}
			if (upper == "SHELLPATH") {
				if (!validateShellExecutablePath(value, errorMessage)) return false;
				if (!setConfiguredShellExecutablePath(value, errorMessage)) return false;
				paths.shellUri = normalizeConfiguredPathInput(value);
				return true;
			}
			break;
		}
		case MRSettingsKeyClass::Global: {
			std::string upper = upperAscii(trimAscii(key));
			if (upper == "WINDOW_MANAGER") {
				bool parsed = true;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredWindowManager(parsed, errorMessage);
			}
			if (upper == "MESSAGES") {
				bool parsed = true;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredMenulineMessages(parsed, errorMessage);
			}
			if (upper == "SEARCH_TEXT_TYPE") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseSearchTextTypeLiteral(value, options.textType, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_DIRECTION") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseSearchDirectionLiteral(value, options.direction, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_MODE") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseSearchModeLiteral(value, options.mode, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_CASE_SENSITIVE") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.caseSensitive, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_GLOBAL_SEARCH") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.globalSearch, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_RESTRICT_MARKED_BLOCK") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.restrictToMarkedBlock, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_ALL_WINDOWS") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.searchAllWindows, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_LIST_ALL_OCCURRENCES") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				bool listAll = false;
				if (!parseBooleanLiteral(value, listAll, errorMessage)) return false;
				options.mode = listAll ? MRSearchMode::ListAll : MRSearchMode::StopFirst;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_TEXT_TYPE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseSearchTextTypeLiteral(value, options.textType, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_DIRECTION") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseSearchDirectionLiteral(value, options.direction, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_MODE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseSarModeLiteral(value, options.mode, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_LEAVE_CURSOR_AT") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseSarLeaveCursorLiteral(value, options.leaveCursorAt, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_CASE_SENSITIVE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseBooleanLiteral(value, options.caseSensitive, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_GLOBAL_SEARCH") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseBooleanLiteral(value, options.globalSearch, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_RESTRICT_MARKED_BLOCK") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseBooleanLiteral(value, options.restrictToMarkedBlock, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_ALL_WINDOWS") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseBooleanLiteral(value, options.searchAllWindows, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_REPLACE_MODE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				MRSarMode mode = MRSarMode::ReplaceFirst;
				if (!parseSarModeLiteral(value, mode, errorMessage)) return false;
				options.mode = mode == MRSarMode::ReplaceAll ? MRSarMode::ReplaceAll : MRSarMode::ReplaceFirst;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_PROMPT_EACH_REPLACE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				bool promptEach = false;
				if (!parseBooleanLiteral(value, promptEach, errorMessage)) return false;
				if (promptEach) options.mode = MRSarMode::PromptEach;
				else if (options.mode == MRSarMode::PromptEach)
					options.mode = MRSarMode::ReplaceFirst;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_FILESPEC") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				options.filespec = trimAscii(value);
				if (options.filespec.empty()) options.filespec = "*.*";
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_TEXT") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				options.searchText = value;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_STARTING_PATH") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				options.startingPath = normalizeConfiguredPathInput(value);
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_SUBDIRECTORIES") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.searchSubdirectories, errorMessage)) return false;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_CASE_SENSITIVE") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.caseSensitive, errorMessage)) return false;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_REGULAR_EXPRESSIONS") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.regularExpressions, errorMessage)) return false;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_FILES_IN_MEMORY") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.searchFilesInMemory, errorMessage)) return false;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_FILESPEC") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				options.filespec = trimAscii(value);
				if (options.filespec.empty()) options.filespec = "*.*";
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_TEXT") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				options.searchText = value;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_REPLACEMENT") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				options.replacementText = value;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_STARTING_PATH") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				options.startingPath = normalizeConfiguredPathInput(value);
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_SUBDIRECTORIES") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.searchSubdirectories, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_CASE_SENSITIVE") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.caseSensitive, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_REGULAR_EXPRESSIONS") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.regularExpressions, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_FILES_IN_MEMORY") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.searchFilesInMemory, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_KEEP_FILES_OPEN") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.keepFilesOpen, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "VIRTUAL_DESKTOPS") {
				int parsed = 1;
				try {
					parsed = std::stoi(value);
				} catch (...) {
					parsed = 1;
				}
				if (parsed < 1) parsed = 1;
				if (parsed > 9) parsed = 9;
				applyVirtualDesktopConfigurationChange(parsed);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "CYCLIC_VIRTUAL_DESKTOPS") {
				bool parsed = false;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredCyclicVirtualDesktops(parsed, errorMessage);
			}
			if (upper == "CURSOR_BEHAVIOUR") {
				MRCursorBehaviour behaviour = MRCursorBehaviour::BoundToText;
				if (!parseCursorBehaviourLiteral(value, behaviour, errorMessage)) return false;
				return setConfiguredCursorBehaviour(behaviour, errorMessage);
			}
			if (upper == "CURSOR_POSITION_MARKER") return setConfiguredCursorPositionMarker(value, errorMessage);
			if (upper == "AUTOLOAD_WORKSPACE") {
				bool parsed = false;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredAutoloadWorkspace(parsed, errorMessage);
			}
			if (upper == "LOG_HANDLING") {
				MRLogHandling handling = MRLogHandling::Volatile;
				if (!parseLogHandlingLiteral(value, handling, errorMessage)) return false;
				return setConfiguredLogHandling(handling, errorMessage);
			}
			if (upper == "LOGFILE") return setConfiguredLogFilePath(value, errorMessage);
			if (upper == "AUTOEXEC_MACRO") return addConfiguredAutoexecMacroEntry(value, errorMessage);
			if (upper == "LASTFILEDIALOGPATH") return setConfiguredLastFileDialogPath(value, errorMessage);
			if (upper == "WORKSPACE") {
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MAX_PATH_HISTORY") {
				int parsed = 0;
				if (!parseHistoryLimitLiteral(value, parsed, errorMessage, "MAX_PATH_HISTORY")) return false;
				return setConfiguredPathHistoryLimitValue(parsed, errorMessage);
			}
			if (upper == "MAX_FILE_HISTORY") {
				int parsed = 0;
				if (!parseHistoryLimitLiteral(value, parsed, errorMessage, "MAX_FILE_HISTORY")) return false;
				return setConfiguredFileHistoryLimitValue(parsed, errorMessage);
			}
			if (upper == "PATH_HISTORY") {
				addSerializedHistoryEntry(dialogHistoryState(MRDialogHistoryScope::General).pathHistory, value, configuredPathHistoryLimit(), true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "FILE_HISTORY") {
				addSerializedHistoryEntry(dialogHistoryState(MRDialogHistoryScope::General).fileHistory, value, configuredFileHistoryLimit(), true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kDialogLastPathKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedPath;

				if (!parseScopedHistoryPayload(value, "path", scope, parsedPath, errorMessage)) return false;
				return setScopedDialogLastPath(scope, parsedPath, errorMessage);
			}
			if (upper == kDialogPathHistoryKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedValue;

				if (!parseScopedHistoryPayload(value, "value", scope, parsedValue, errorMessage)) return false;
				addSerializedHistoryEntry(dialogHistoryState(scope).pathHistory, parsedValue, configuredPathHistoryLimit(), true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kDialogFileHistoryKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedValue;

				if (!parseScopedHistoryPayload(value, "value", scope, parsedValue, errorMessage)) return false;
				addSerializedHistoryEntry(dialogHistoryState(scope).fileHistory, parsedValue, configuredFileHistoryLimit(), true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_FILESPEC_HISTORY") {
				addSerializedHistoryEntry(configuredMultiFilespecHistoryStorage(), value, configuredFileHistoryLimit(), false);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_PATH_HISTORY") {
				addSerializedHistoryEntry(configuredMultiPathHistoryStorage(), value, configuredPathHistoryLimit(), true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "DEFAULT_PROFILE_DESCRIPTION") return setConfiguredDefaultProfileDescription(value, errorMessage);
			if (upper == kKeymapSettingsKey) return setConfiguredKeymapFilePath(value, errorMessage);
			if (upper == "ACTIVE_KEYMAP_PROFILE" || upper == "KEYMAP_PROFILE" || upper == "KEYMAP_BIND") {
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kThemeSettingsKey) return setConfiguredColorThemeFilePath(value, errorMessage);
			break;
		}
		case MRSettingsKeyClass::Edit:
			return applyConfiguredEditSetupValue(key, value, errorMessage);
		case MRSettingsKeyClass::ColorInline:
			return applyConfiguredColorSetupValue(key, value, errorMessage);
	}
	return setError(errorMessage, "Unsupported MRSETUP key.");
}

bool resetSettingsSnapshot(const std::string &settingsPath, MRSettingsSnapshot &snapshot, std::string *errorMessage) {
	MRSetupPaths paths = resolveSetupPathDefaults();
	std::string normalized;

	snapshot = MRSettingsSnapshot();
	snapshot.paths = paths;
	normalized = normalizeConfiguredPathInput(settingsPath);
	if (!validateSettingsMacroFilePath(normalized, errorMessage)) return false;
	snapshot.paths.settingsMacroUri = makeAbsolutePath(normalized);
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupSettingsMacro, snapshot.paths.settingsMacroUri, errorMessage)) return false;
	normalized = normalizeConfiguredPathInput(paths.macroPath);
	if (!validateMacroDirectoryPath(paths.macroPath, errorMessage)) return false;
	snapshot.paths.macroPath = makeAbsolutePath(normalized);
	if (snapshot.dialogHistory[dialogHistoryScopeIndex(MRDialogHistoryScope::General)].pathHistory.empty() && isReadableDirectory(snapshot.paths.macroPath))
		addHistoryEntry(snapshot.dialogHistory[dialogHistoryScopeIndex(MRDialogHistoryScope::General)].pathHistory, snapshot.paths.macroPath, snapshot.maxPathHistory);
	normalized = normalizeConfiguredPathInput(paths.helpUri);
	if (!validateHelpFilePath(paths.helpUri, errorMessage)) return false;
	snapshot.paths.helpUri = makeAbsolutePath(normalized);
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupHelpFile, snapshot.paths.helpUri, errorMessage)) return false;
	normalized = normalizeConfiguredPathInput(paths.tempPath);
	if (!validateTempDirectoryPath(paths.tempPath, errorMessage)) return false;
	snapshot.paths.tempPath = makeAbsolutePath(normalized);
	normalized = normalizeConfiguredPathInput(paths.shellUri);
	if (!validateShellExecutablePath(paths.shellUri, errorMessage)) return false;
	snapshot.paths.shellUri = makeAbsolutePath(normalized);
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupShellExecutable, snapshot.paths.shellUri, errorMessage)) return false;
	normalized = normalizeConfiguredPathInput(defaultLogFilePathForSettings(snapshot.paths.settingsMacroUri));
	if (!validateLogFilePath(normalized, errorMessage)) return false;
	snapshot.logFilePath = makeAbsolutePath(normalized);
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupLogFile, snapshot.logFilePath, errorMessage)) return false;
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::General, snapshot.paths.macroPath, errorMessage)) return false;
	snapshot.defaultProfileDescription = "Global defaults";
	snapshot.editSettings = resolveEditSetupDefaults();
	snapshot.colorSettings = defaultsFromColorGroups();
	if (!setSnapshotEditProfiles(snapshot, std::vector<MREditExtensionProfile>(), errorMessage)) return false;
	snapshot.keymapFilePath.clear();
	normalized = normalizeConfiguredPathInput(defaultColorThemePathForSettings(snapshot.paths.settingsMacroUri));
	if (!validateColorThemeFilePath(normalized, errorMessage)) return false;
	snapshot.colorThemeFilePath = makeAbsolutePath(normalized);
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupThemeLoad, snapshot.colorThemeFilePath, errorMessage)) return false;
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupThemeSave, snapshot.colorThemeFilePath, errorMessage)) return false;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool applySettingsSnapshotAssignment(MRSettingsSnapshot &snapshot, const std::string &key, const std::string &value, std::string *errorMessage) {
	switch (classifySettingsKey(key)) {
		case MRSettingsKeyClass::Unknown:
			return setError(errorMessage, "Unsupported MRSETUP key.");
		case MRSettingsKeyClass::Version:
			if (trimAscii(value) != kCurrentSettingsVersion) return setError(errorMessage, "Unsupported settings version.");
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		case MRSettingsKeyClass::Path: {
			std::string upper = upperAscii(trimAscii(key));
			if (upper == "SETTINGSPATH") {
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MACROPATH") {
				const std::string normalized = normalizeConfiguredPathInput(value);
				MRSettingsSnapshot::DialogHistoryState &generalHistory = snapshot.dialogHistory[dialogHistoryScopeIndex(MRDialogHistoryScope::General)];

				if (!validateMacroDirectoryPath(value, errorMessage)) return false;
				snapshot.paths.macroPath = makeAbsolutePath(normalized);
				if (generalHistory.pathHistory.empty() && isReadableDirectory(snapshot.paths.macroPath)) addHistoryEntry(generalHistory.pathHistory, snapshot.paths.macroPath, snapshot.maxPathHistory);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "HELPPATH") {
				const std::string normalized = normalizeConfiguredPathInput(value);

				if (!validateHelpFilePath(value, errorMessage)) return false;
				snapshot.paths.helpUri = makeAbsolutePath(normalized);
				return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupHelpFile, snapshot.paths.helpUri, errorMessage);
			}
			if (upper == "TEMPDIR") {
				const std::string normalized = normalizeConfiguredPathInput(value);

				if (!validateTempDirectoryPath(value, errorMessage)) return false;
				snapshot.paths.tempPath = makeAbsolutePath(normalized);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "SHELLPATH") {
				const std::string normalized = normalizeConfiguredPathInput(value);

				if (!validateShellExecutablePath(value, errorMessage)) return false;
				snapshot.paths.shellUri = makeAbsolutePath(normalized);
				return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupShellExecutable, snapshot.paths.shellUri, errorMessage);
			}
			break;
		}
		case MRSettingsKeyClass::Global: {
			std::string upper = upperAscii(trimAscii(key));
			if (upper == "WINDOW_MANAGER") {
				if (!parseBooleanLiteral(value, snapshot.windowManagerEnabled, errorMessage)) return false;
				return true;
			}
			if (upper == "MESSAGES") {
				if (!parseBooleanLiteral(value, snapshot.menulineMessagesEnabled, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_TEXT_TYPE") {
				if (!parseSearchTextTypeLiteral(value, snapshot.searchDialogOptions.textType, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_DIRECTION") {
				if (!parseSearchDirectionLiteral(value, snapshot.searchDialogOptions.direction, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_MODE") {
				if (!parseSearchModeLiteral(value, snapshot.searchDialogOptions.mode, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_CASE_SENSITIVE") {
				if (!parseBooleanLiteral(value, snapshot.searchDialogOptions.caseSensitive, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_GLOBAL_SEARCH") {
				if (!parseBooleanLiteral(value, snapshot.searchDialogOptions.globalSearch, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_RESTRICT_MARKED_BLOCK") {
				if (!parseBooleanLiteral(value, snapshot.searchDialogOptions.restrictToMarkedBlock, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_ALL_WINDOWS") {
				if (!parseBooleanLiteral(value, snapshot.searchDialogOptions.searchAllWindows, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_LIST_ALL_OCCURRENCES") {
				bool listAll = false;
				if (!parseBooleanLiteral(value, listAll, errorMessage)) return false;
				snapshot.searchDialogOptions.mode = listAll ? MRSearchMode::ListAll : MRSearchMode::StopFirst;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "SAR_TEXT_TYPE") {
				if (!parseSearchTextTypeLiteral(value, snapshot.sarDialogOptions.textType, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_DIRECTION") {
				if (!parseSearchDirectionLiteral(value, snapshot.sarDialogOptions.direction, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_MODE") {
				if (!parseSarModeLiteral(value, snapshot.sarDialogOptions.mode, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_LEAVE_CURSOR_AT") {
				if (!parseSarLeaveCursorLiteral(value, snapshot.sarDialogOptions.leaveCursorAt, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_CASE_SENSITIVE") {
				if (!parseBooleanLiteral(value, snapshot.sarDialogOptions.caseSensitive, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_GLOBAL_SEARCH") {
				if (!parseBooleanLiteral(value, snapshot.sarDialogOptions.globalSearch, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_RESTRICT_MARKED_BLOCK") {
				if (!parseBooleanLiteral(value, snapshot.sarDialogOptions.restrictToMarkedBlock, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_ALL_WINDOWS") {
				if (!parseBooleanLiteral(value, snapshot.sarDialogOptions.searchAllWindows, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_REPLACE_MODE") {
				MRSarMode mode = MRSarMode::ReplaceFirst;
				if (!parseSarModeLiteral(value, mode, errorMessage)) return false;
				snapshot.sarDialogOptions.mode = mode == MRSarMode::ReplaceAll ? MRSarMode::ReplaceAll : MRSarMode::ReplaceFirst;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "SAR_PROMPT_EACH_REPLACE") {
				bool promptEach = false;
				if (!parseBooleanLiteral(value, promptEach, errorMessage)) return false;
				if (promptEach) snapshot.sarDialogOptions.mode = MRSarMode::PromptEach;
				else if (snapshot.sarDialogOptions.mode == MRSarMode::PromptEach)
					snapshot.sarDialogOptions.mode = MRSarMode::ReplaceFirst;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SEARCH_FILESPEC") {
				snapshot.multiSearchDialogOptions.filespec = trimAscii(value);
				if (snapshot.multiSearchDialogOptions.filespec.empty()) snapshot.multiSearchDialogOptions.filespec = "*.*";
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SEARCH_TEXT") {
				snapshot.multiSearchDialogOptions.searchText = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SEARCH_STARTING_PATH") {
				snapshot.multiSearchDialogOptions.startingPath = normalizeConfiguredPathInput(value);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SEARCH_SUBDIRECTORIES") {
				if (!parseBooleanLiteral(value, snapshot.multiSearchDialogOptions.searchSubdirectories, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SEARCH_CASE_SENSITIVE") {
				if (!parseBooleanLiteral(value, snapshot.multiSearchDialogOptions.caseSensitive, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SEARCH_REGULAR_EXPRESSIONS") {
				if (!parseBooleanLiteral(value, snapshot.multiSearchDialogOptions.regularExpressions, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SEARCH_FILES_IN_MEMORY") {
				if (!parseBooleanLiteral(value, snapshot.multiSearchDialogOptions.searchFilesInMemory, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_FILESPEC") {
				snapshot.multiSarDialogOptions.filespec = trimAscii(value);
				if (snapshot.multiSarDialogOptions.filespec.empty()) snapshot.multiSarDialogOptions.filespec = "*.*";
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SAR_TEXT") {
				snapshot.multiSarDialogOptions.searchText = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SAR_REPLACEMENT") {
				snapshot.multiSarDialogOptions.replacementText = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SAR_STARTING_PATH") {
				snapshot.multiSarDialogOptions.startingPath = normalizeConfiguredPathInput(value);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SAR_SUBDIRECTORIES") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.searchSubdirectories, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_CASE_SENSITIVE") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.caseSensitive, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_REGULAR_EXPRESSIONS") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.regularExpressions, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_FILES_IN_MEMORY") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.searchFilesInMemory, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_KEEP_FILES_OPEN") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.keepFilesOpen, errorMessage)) return false;
				return true;
			}
			if (upper == "VIRTUAL_DESKTOPS") {
				int parsed = 1;
				try {
					parsed = std::stoi(value);
				} catch (...) {
					parsed = 1;
				}
				if (parsed < 1) parsed = 1;
				if (parsed > 9) parsed = 9;
				snapshot.virtualDesktops = parsed;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "CYCLIC_VIRTUAL_DESKTOPS") {
				if (!parseBooleanLiteral(value, snapshot.cyclicVirtualDesktops, errorMessage)) return false;
				return true;
			}
			if (upper == "CURSOR_BEHAVIOUR") {
				if (!parseCursorBehaviourLiteral(value, snapshot.cursorBehaviour, errorMessage)) return false;
				return true;
			}
			if (upper == "CURSOR_POSITION_MARKER") return normalizeCursorPositionMarker(value, snapshot.cursorPositionMarker, errorMessage);
			if (upper == "AUTOLOAD_WORKSPACE") {
				if (!parseBooleanLiteral(value, snapshot.autoloadWorkspace, errorMessage)) return false;
				return true;
			}
			if (upper == "LOG_HANDLING") {
				if (!parseLogHandlingLiteral(value, snapshot.logHandling, errorMessage)) return false;
				return true;
			}
			if (upper == "LOGFILE") {
				const std::string normalized = normalizeConfiguredPathInput(value);

				if (!validateLogFilePath(value, errorMessage)) return false;
				snapshot.logFilePath = makeAbsolutePath(normalized);
				return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupLogFile, snapshot.logFilePath, errorMessage);
			}
			if (upper == "AUTOEXEC_MACRO") {
				const std::string normalized = normalizeAutoexecMacroEntry(value);
				if (!validateAutoexecMacroEntry(normalized, errorMessage)) return false;
				if (std::find(snapshot.autoexecMacros.begin(), snapshot.autoexecMacros.end(), normalized) == snapshot.autoexecMacros.end()) snapshot.autoexecMacros.push_back(normalized);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "LASTFILEDIALOGPATH") return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::General, value, errorMessage);
			if (upper == "WORKSPACE") {
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MAX_PATH_HISTORY") {
				int parsed = 0;
				if (!parseHistoryLimitLiteral(value, parsed, errorMessage, "MAX_PATH_HISTORY")) return false;
				return setSnapshotPathHistoryLimit(snapshot, parsed, errorMessage);
			}
			if (upper == "MAX_FILE_HISTORY") {
				int parsed = 0;
				if (!parseHistoryLimitLiteral(value, parsed, errorMessage, "MAX_FILE_HISTORY")) return false;
				return setSnapshotFileHistoryLimit(snapshot, parsed, errorMessage);
			}
			if (upper == "PATH_HISTORY") {
				addSerializedHistoryEntry(snapshot.dialogHistory[dialogHistoryScopeIndex(MRDialogHistoryScope::General)].pathHistory, value, snapshot.maxPathHistory, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "FILE_HISTORY") {
				addSerializedHistoryEntry(snapshot.dialogHistory[dialogHistoryScopeIndex(MRDialogHistoryScope::General)].fileHistory, value, snapshot.maxFileHistory, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kDialogLastPathKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedPath;

				if (!parseScopedHistoryPayload(value, "path", scope, parsedPath, errorMessage)) return false;
				return setSnapshotScopedDialogLastPath(snapshot, scope, parsedPath, errorMessage);
			}
			if (upper == kDialogPathHistoryKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedValue;

				if (!parseScopedHistoryPayload(value, "value", scope, parsedValue, errorMessage)) return false;
				addSerializedHistoryEntry(snapshot.dialogHistory[dialogHistoryScopeIndex(scope)].pathHistory, parsedValue, snapshot.maxPathHistory, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kDialogFileHistoryKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedValue;

				if (!parseScopedHistoryPayload(value, "value", scope, parsedValue, errorMessage)) return false;
				addSerializedHistoryEntry(snapshot.dialogHistory[dialogHistoryScopeIndex(scope)].fileHistory, parsedValue, snapshot.maxFileHistory, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_FILESPEC_HISTORY") {
				addSerializedHistoryEntry(snapshot.multiFilespecHistory, value, snapshot.maxFileHistory, false);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_PATH_HISTORY") {
				addSerializedHistoryEntry(snapshot.multiPathHistory, value, snapshot.maxPathHistory, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "DEFAULT_PROFILE_DESCRIPTION") {
				snapshot.defaultProfileDescription = trimAscii(value);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kKeymapSettingsKey) {
				const std::string normalized = normalizeConfiguredPathInput(value);
				struct stat st;

				if (normalized.empty()) {
					snapshot.keymapFilePath.clear();
					if (errorMessage != nullptr) errorMessage->clear();
					return true;
				}
				if (::stat(normalized.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return setError(errorMessage, "Keymap URI must include a filename.");
				snapshot.keymapFilePath = makeAbsolutePath(normalized);
				if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::KeymapProfileLoad, snapshot.keymapFilePath, errorMessage)) return false;
				return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::KeymapProfileSave, snapshot.keymapFilePath, errorMessage);
			}
			if (upper == "ACTIVE_KEYMAP_PROFILE" || upper == "KEYMAP_PROFILE" || upper == "KEYMAP_BIND") {
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kThemeSettingsKey) {
				const std::string normalized = normalizeConfiguredPathInput(value);

				if (!validateColorThemeFilePath(value, errorMessage)) return false;
				snapshot.colorThemeFilePath = makeAbsolutePath(normalized);
				if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupThemeLoad, snapshot.colorThemeFilePath, errorMessage)) return false;
				return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupThemeSave, snapshot.colorThemeFilePath, errorMessage);
			}
			break;
		}
		case MRSettingsKeyClass::Edit:
			return applyEditSetupValueInternal(snapshot.editSettings, key, value, errorMessage);
		case MRSettingsKeyClass::ColorInline:
			return applyColorSetupValueInternal(snapshot.colorSettings, key, value, errorMessage);
	}
	return setError(errorMessage, "Unsupported MRSETUP key.");
}

bool applySettingsSnapshotEditExtensionProfileDirective(MRSettingsSnapshot &snapshot, const std::string &operation, const std::string &profileId, const std::string &arg3, const std::string &arg4, std::string *errorMessage) {
	std::string op = upperAscii(trimAscii(operation));
	std::string id = canonicalEditProfileId(profileId);
	std::vector<MREditExtensionProfile> profiles = snapshot.editProfiles;
	MREditExtensionProfile *profile = nullptr;

	if (op.empty()) return setError(errorMessage, "MRFEPROFILE operation may not be empty.");
	if (id.empty()) return setError(errorMessage, "MRFEPROFILE profile id may not be empty.");
	for (std::size_t i = 0; i < profiles.size(); ++i)
		if (profileIdLookupKey(profiles[i].id) == profileIdLookupKey(id)) {
			profile = &profiles[i];
			break;
		}
	if (op == "DEFINE") {
		std::string name = canonicalEditProfileName(arg3);

		if (name.empty() && trimAscii(arg4).empty()) name = id;
		if (name.empty()) return setError(errorMessage, "MRFEPROFILE DEFINE requires a non-empty display name.");
		if (profile != nullptr) return setError(errorMessage, "Duplicate extension profile id: " + id);
		MREditExtensionProfile created;
		created.id = id;
		created.name = name;
		created.overrides.values = resolveEditSetupDefaults();
		profiles.push_back(created);
		return setSnapshotEditProfiles(snapshot, profiles, errorMessage);
	}
	if (profile == nullptr) return setError(errorMessage, "Unknown extension profile id: " + id);
	if (op == "EXT") {
		profile->extensions.push_back(arg3);
		return setSnapshotEditProfiles(snapshot, profiles, errorMessage);
	}
	if (op == "SET") {
		if (upperAscii(trimAscii(arg3)) == kWindowColorThemeProfileKey) {
			std::string normalizedTheme = canonicalWindowColorThemeUri(arg4);
			if (!normalizedTheme.empty() && !validateColorThemeFilePath(normalizedTheme, errorMessage)) return false;
			profile->windowColorThemeUri = normalizedTheme;
			return setSnapshotEditProfiles(snapshot, profiles, errorMessage);
		}
		const MREditSettingDescriptor *descriptor = editSettingDescriptorByKeyInternal(arg3);

		if (descriptor == nullptr) return setError(errorMessage, "Unknown edit setting key for extension profile.");
		if (!descriptor->profileSupported) return setError(errorMessage, std::string("Setting is global-only and cannot be overridden: ") + descriptor->key);
		if (!applyEditSetupValueInternal(profile->overrides.values, descriptor->key, arg4, errorMessage)) return false;
		profile->overrides.mask |= descriptor->overrideBit;
		return setSnapshotEditProfiles(snapshot, profiles, errorMessage);
	}
	return setError(errorMessage, "MRFEPROFILE supports operations DEFINE, EXT and SET.");
}

bool loadColorThemeFileIntoSettingsSnapshot(MRSettingsSnapshot &snapshot, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(snapshot.colorThemeFilePath);
	std::string source;
	std::map<std::string, std::string> assignments;
	static const char *const order[] = {"WINDOWCOLORS", "MENUDIALOGCOLORS", "HELPCOLORS", "OTHERCOLORS", "MINIMAPCOLORS", "CODECOLORS"};

	if (!validateColorThemeFilePath(normalized, errorMessage)) return false;
	if (!ensureColorThemeFileExists(normalized, errorMessage)) return false;
	if (!readTextFile(normalized, source)) return setError(errorMessage, "Unable to read color theme file: " + normalized);
	if (!parseThemeSetupAssignments(source, assignments, errorMessage)) return false;
	for (const char *key : order) {
		std::string applyError;
		if (!applyColorSetupValueInternal(snapshot.colorSettings, key, assignments[key], &applyError)) return setError(errorMessage, "Theme apply failed for " + std::string(key) + ": " + applyError);
	}
	snapshot.colorThemeFilePath = makeAbsolutePath(normalized);
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupThemeLoad, snapshot.colorThemeFilePath, errorMessage)) return false;
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupThemeSave, snapshot.colorThemeFilePath, errorMessage)) return false;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

// This is a staging/verification/canonicalization pass. It builds a transient
// settings snapshot while reading, normalizing and defaulting settings, but it
// is not the final bootstrap apply contract.
static bool loadAndNormalizeSettingsSource(const std::string &settingsPath, const std::string &source, MRSettingsSnapshot &snapshot, MRSettingsLoadReport *report, std::string *errorMessage) {
	MRSettingsLoadReport localReport;
	MRSettingsLoadReport &activeReport = report != nullptr ? *report : localReport;
	MRParsedSettingsDocument document = parseSettingsDocument(source, false);
	std::set<std::string> canonicalKeysSeen;
	std::string activeSettingsPath = normalizeConfiguredPathInput(settingsPath);
	std::string applyError;
	std::string themeError;

	activeReport = MRSettingsLoadReport();
	if (countLegacyFeProfileDirectives(source) != 0) markFlag(activeReport, MRSettingsLoadReport::ObsoleteFeProfileDropped);
	if (!resetSettingsSnapshot(activeSettingsPath, snapshot, errorMessage)) return false;

	for (const MRParsedSettingsAssignment &assignment : document.assignments) {
		MRSettingsKeyClass keyClass = classifySettingsKey(assignment.key);

		if (keyClass == MRSettingsKeyClass::Unknown) {
			markFlag(activeReport, MRSettingsLoadReport::UnknownKeyDropped);
			++activeReport.ignoredAssignmentCount;
			continue;
		}
		if (isCanonicalSerializedSettingsKey(assignment.key) && !canonicalKeysSeen.insert(assignment.key).second) {
			markFlag(activeReport, MRSettingsLoadReport::DuplicateKeySeen);
			++activeReport.duplicateAssignmentCount;
		}
		if (keyClass == MRSettingsKeyClass::ColorInline) markFlag(activeReport, MRSettingsLoadReport::LegacyInlineColorsSeen);
		if (assignment.key == "SETTINGSPATH" && normalizeConfiguredPathInput(assignment.value) != activeSettingsPath) markFlag(activeReport, MRSettingsLoadReport::AnchoredSettingsPath);
		if (!applySettingsSnapshotAssignment(snapshot, assignment.key, assignment.value, &applyError)) {
			markFlag(activeReport, MRSettingsLoadReport::InvalidValueReset);
			++activeReport.ignoredAssignmentCount;
			continue;
		}
		++activeReport.appliedAssignmentCount;
	}

	for (const MRParsedEditProfileDirective &directive : document.profileDirectives)
		if (!applySettingsSnapshotEditExtensionProfileDirective(snapshot, directive.operation, directive.profileId, directive.arg3, directive.arg4, errorMessage)) return false;

	{
		MRKeymapLoadResult keymapLoad = loadKeymapProfilesFromSettingsSource(source);
		const std::string diagnosticSummary = summarizeKeymapDiagnosticsForMessageLine(keymapLoad.diagnostics, "Keymap bootstrap");
		std::set<std::string> loggedDiagnostics;

		mrLogMessage(summarizeKeymapLoadForLog(keymapLoad));
		for (const MRKeymapDiagnostic &diagnostic : keymapLoad.diagnostics) {
			if (!loggedDiagnostics.insert(keymapDiagnosticIdentity(diagnostic)).second) continue;
			mrLogMessage("Keymap bootstrap diagnostic [" + std::string(keymapDiagnosticSeverityName(diagnostic.severity)) + "]: " + describeKeymapDiagnostic(keymapLoad.profiles, diagnostic));
		}
		if (!diagnosticSummary.empty()) mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, diagnosticSummary, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		if (keymapLoad.profiles.empty()) keymapLoad.profiles.push_back(builtInDefaultKeymapProfile());
		if (keymapLoad.activeProfileName.empty()) keymapLoad.activeProfileName = "DEFAULT";
		if (std::ranges::find(keymapLoad.profiles, keymapLoad.activeProfileName, &MRKeymapProfile::name) == keymapLoad.profiles.end()) keymapLoad.activeProfileName = "DEFAULT";
		if (keymapLoad.activeProfileName.empty() || std::ranges::find(keymapLoad.profiles, keymapLoad.activeProfileName, &MRKeymapProfile::name) == keymapLoad.profiles.end()) {
			mrLogMessage("Keymap bootstrap canonicalized result could not be applied; falling back to DEFAULT.");
			markFlag(activeReport, MRSettingsLoadReport::InvalidValueReset);
			snapshot.keymapProfiles = std::vector<MRKeymapProfile>{builtInDefaultKeymapProfile()};
			snapshot.activeKeymapProfile = "DEFAULT";
		} else {
			snapshot.keymapProfiles = std::move(keymapLoad.profiles);
			snapshot.activeKeymapProfile = std::move(keymapLoad.activeProfileName);
			mrLogMessage("Keymap bootstrap applied canonicalized result.");
		}
	}

	if (canonicalKeysSeen.size() < canonicalSerializedSettingsKeyCount()) {
		activeReport.defaultedCanonicalKeyCount = canonicalSerializedSettingsKeyCount() - canonicalKeysSeen.size();
		markFlag(activeReport, MRSettingsLoadReport::MissingCanonicalKeyDefaulted);
	}

	if (!loadColorThemeFileIntoSettingsSnapshot(snapshot, &themeError)) {
		markFlag(activeReport, MRSettingsLoadReport::ThemeFallbackUsed);
		mrLogMessage("Settings normalization retained staged color setup after theme load failure: " + snapshot.colorThemeFilePath + " (" + themeError + ")");
	}

	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool buildCanonicalSettingsSource(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource, std::string *errorMessage) {
	MRSettingsSnapshot snapshot;
	std::string normalizedPath = normalizeConfiguredPathInput(settingsPath);

	if (!loadAndNormalizeSettingsSource(normalizedPath, source, snapshot, report, errorMessage)) return false;
	canonicalSource = buildSettingsMacroSource(snapshot);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool prepareStartupSettingsSource(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource, std::string *errorMessage) {
	MRSettingsLoadReport localReport;
	MRSettingsLoadReport &activeReport = report != nullptr ? *report : localReport;
	MRSettingsSnapshot snapshot;
	std::string normalizedPath = normalizeConfiguredPathInput(settingsPath);
	std::string rewriteError;
	std::string summary;

	activeReport = MRSettingsLoadReport();
	if (!loadAndNormalizeSettingsSource(normalizedPath, source, snapshot, &activeReport, errorMessage)) return false;
	canonicalSource = buildSettingsMacroSource(snapshot);
	if (!activeReport.normalized()) {
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (!writeNormalizedBootstrapFiles(snapshot, source, canonicalSource, &rewriteError)) {
		if (errorMessage != nullptr) *errorMessage = "Settings rewrite failed: " + rewriteError;
		return false;
	}
	summary = describeSettingsLoadReport(activeReport);
	mrLogMessage(("Settings normalized: " + normalizedPath).c_str());
	if (!summary.empty()) mrLogMessage(("Settings normalization details: " + summary).c_str());
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string describeSettingsLoadReport(const MRSettingsLoadReport &report) {
	std::vector<std::string> parts;
	std::string text;

	if (hasFlag(report, MRSettingsLoadReport::UnknownKeyDropped)) parts.emplace_back("unknown keys dropped");
	if (hasFlag(report, MRSettingsLoadReport::DuplicateKeySeen)) parts.emplace_back("duplicates resolved");
	if (hasFlag(report, MRSettingsLoadReport::InvalidValueReset)) parts.emplace_back("invalid values reset to defaults");
	if (hasFlag(report, MRSettingsLoadReport::MissingCanonicalKeyDefaulted)) parts.emplace_back("missing canonical keys defaulted");
	if (hasFlag(report, MRSettingsLoadReport::LegacyInlineColorsSeen)) parts.emplace_back("legacy inline colors normalized");
	if (hasFlag(report, MRSettingsLoadReport::ThemeFallbackUsed)) parts.emplace_back("theme fallback applied");
	if (hasFlag(report, MRSettingsLoadReport::AnchoredSettingsPath)) parts.emplace_back("settings path anchored to active file");
	if (hasFlag(report, MRSettingsLoadReport::ObsoleteFeProfileDropped)) parts.emplace_back("obsolete MREDITPROFILE directives dropped; FE profile defaults restored");
	if (parts.empty()) return std::string();
	for (std::size_t i = 0; i < parts.size(); ++i) {
		if (i != 0) text += "; ";
		text += parts[i];
	}
	return text;
}

bool diffSettingsSources(const std::string &beforeSource, const std::string &afterSource, std::vector<MRSettingsChangeEntry> &changes, std::string *errorMessage) {
	const MRFlattenedSettingsDocument before = flattenSettingsDocument(parseSettingsDocument(beforeSource, true));
	const MRFlattenedSettingsDocument after = flattenSettingsDocument(parseSettingsDocument(afterSource, true));

	changes.clear();
	diffFlattenedDocuments(before, after, changes);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string formatSettingsChangeForLog(const MRSettingsChangeEntry &change) {
	std::string text = change.scope + " ";

	if (change.kind == MRSettingsChangeEntry::Kind::Added) text += "+ " + change.key + " = " + quoteValue(change.newValue);
	else if (change.kind == MRSettingsChangeEntry::Kind::Removed)
		text += "- " + change.key + " (was " + quoteValue(change.oldValue) + ")";
	else
		text += change.key + ": " + quoteValue(change.oldValue) + " -> " + quoteValue(change.newValue);
	return text;
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

MREditSetupSettings mergeEditSetupSettings(const MREditSetupSettings &defaults, const MREditSetupOverrides &overrides) {
	MREditSetupSettings merged = defaults;

	if ((overrides.mask & kOvPageBreak) != 0) merged.pageBreak = overrides.values.pageBreak;
	if ((overrides.mask & kOvWordDelimiters) != 0) merged.wordDelimiters = overrides.values.wordDelimiters;
	if ((overrides.mask & kOvDefaultExtensions) != 0) merged.defaultExtensions = overrides.values.defaultExtensions;
	if ((overrides.mask & kOvTruncateSpaces) != 0) merged.truncateSpaces = overrides.values.truncateSpaces;
	if ((overrides.mask & kOvEofCtrlZ) != 0) merged.eofCtrlZ = overrides.values.eofCtrlZ;
	if ((overrides.mask & kOvEofCrLf) != 0) merged.eofCrLf = overrides.values.eofCrLf;
	if ((overrides.mask & kOvTabExpand) != 0) merged.tabExpand = overrides.values.tabExpand;
	if ((overrides.mask & kOvDisplayTabs) != 0) merged.displayTabs = overrides.values.displayTabs;
	if ((overrides.mask & kOvTabSize) != 0) merged.tabSize = overrides.values.tabSize;
	if ((overrides.mask & kOvLeftMargin) != 0) merged.leftMargin = overrides.values.leftMargin;
	if ((overrides.mask & kOvRightMargin) != 0) merged.rightMargin = overrides.values.rightMargin;
	if ((overrides.mask & kOvFormatRuler) != 0) merged.formatRuler = overrides.values.formatRuler;
	if ((overrides.mask & kOvWordWrap) != 0) merged.wordWrap = overrides.values.wordWrap;
	if ((overrides.mask & kOvIndentStyle) != 0) merged.indentStyle = overrides.values.indentStyle;
	if ((overrides.mask & kOvCodeLanguage) != 0) merged.codeLanguage = overrides.values.codeLanguage;
	if ((overrides.mask & kOvCodeColoring) != 0) merged.codeColoring = overrides.values.codeColoring;
	if ((overrides.mask & kOvCodeFoldingFeature) != 0) merged.codeFoldingFeature = overrides.values.codeFoldingFeature;
	if ((overrides.mask & kOvSmartIndenting) != 0) merged.smartIndenting = overrides.values.smartIndenting;
	if ((overrides.mask & kOvFileType) != 0) merged.fileType = overrides.values.fileType;
	if ((overrides.mask & kOvBinaryRecordLength) != 0) merged.binaryRecordLength = overrides.values.binaryRecordLength;
	if ((overrides.mask & kOvPostLoadMacro) != 0) merged.postLoadMacro = overrides.values.postLoadMacro;
	if ((overrides.mask & kOvPreSaveMacro) != 0) merged.preSaveMacro = overrides.values.preSaveMacro;
	if ((overrides.mask & kOvDefaultPath) != 0) merged.defaultPath = overrides.values.defaultPath;
	if ((overrides.mask & kOvFormatLine) != 0) merged.formatLine = overrides.values.formatLine;
	if ((overrides.mask & kOvBackupFiles) != 0) merged.backupFiles = overrides.values.backupFiles;
	if ((overrides.mask & kOvBackupMethod) != 0) merged.backupMethod = overrides.values.backupMethod;
	if ((overrides.mask & kOvBackupFrequency) != 0) merged.backupFrequency = overrides.values.backupFrequency;
	if ((overrides.mask & kOvBackupExtension) != 0) merged.backupExtension = overrides.values.backupExtension;
	if ((overrides.mask & kOvBackupDirectory) != 0) merged.backupDirectory = overrides.values.backupDirectory;
	if ((overrides.mask & kOvAutosaveInactivitySeconds) != 0) merged.autosaveInactivitySeconds = overrides.values.autosaveInactivitySeconds;
	if ((overrides.mask & kOvAutosaveIntervalSeconds) != 0) merged.autosaveIntervalSeconds = overrides.values.autosaveIntervalSeconds;
	if ((overrides.mask & kOvShowEofMarker) != 0) merged.showEofMarker = overrides.values.showEofMarker;
	if ((overrides.mask & kOvShowEofMarkerEmoji) != 0) merged.showEofMarkerEmoji = overrides.values.showEofMarkerEmoji;
	if ((overrides.mask & kOvLineNumbersPosition) != 0) merged.lineNumbersPosition = overrides.values.lineNumbersPosition;
	if ((overrides.mask & kOvLineNumZeroFill) != 0) merged.lineNumZeroFill = overrides.values.lineNumZeroFill;
	if ((overrides.mask & kOvMiniMapPosition) != 0) merged.miniMapPosition = overrides.values.miniMapPosition;
	if ((overrides.mask & kOvMiniMapWidth) != 0) merged.miniMapWidth = overrides.values.miniMapWidth;
	if ((overrides.mask & kOvMiniMapMarkerGlyph) != 0) merged.miniMapMarkerGlyph = overrides.values.miniMapMarkerGlyph;
	if ((overrides.mask & kOvGutters) != 0) merged.gutters = overrides.values.gutters;
	if ((overrides.mask & kOvPersistentBlocks) != 0) merged.persistentBlocks = overrides.values.persistentBlocks;
	if ((overrides.mask & kOvCodeFoldingPosition) != 0) merged.codeFoldingPosition = overrides.values.codeFoldingPosition;
	if ((overrides.mask & kOvColumnBlockMove) != 0) merged.columnBlockMove = overrides.values.columnBlockMove;
	if ((overrides.mask & kOvDefaultMode) != 0) merged.defaultMode = overrides.values.defaultMode;
	if ((overrides.mask & kOvCursorStatusColor) != 0) merged.cursorStatusColor = overrides.values.cursorStatusColor;
	{
		std::string lineNumbersPosition = normalizeLineNumbersPosition(merged.lineNumbersPosition);
		if (lineNumbersPosition.empty()) lineNumbersPosition = merged.showLineNumbers ? kLineNumbersPositionLeading : kLineNumbersPositionOff;
		merged.lineNumbersPosition = lineNumbersPosition;
		merged.showLineNumbers = lineNumbersPosition != kLineNumbersPositionOff;
	}
	{
		std::string codeFoldingPosition = normalizeCodeFoldingPosition(merged.codeFoldingPosition);
		if (codeFoldingPosition.empty()) codeFoldingPosition = merged.codeFolding ? kCodeFoldingPositionLeading : kCodeFoldingPositionOff;
		merged.codeFoldingPosition = codeFoldingPosition;
		merged.codeFolding = codeFoldingPosition != kCodeFoldingPositionOff;
	}
	return merged;
}

const std::vector<MREditExtensionProfile> &configuredEditExtensionProfiles() {
	return configuredEditProfiles();
}

bool setConfiguredEditExtensionProfiles(const std::vector<MREditExtensionProfile> &profiles, std::string *errorMessage) {
	std::vector<MREditExtensionProfile> normalized = profiles;
	const std::vector<MREditExtensionProfile> previous = configuredEditProfiles();

	for (auto &profile : normalized) {
		profile.id = canonicalEditProfileId(profile.id);
		profile.name = canonicalEditProfileName(profile.name);
		profile.windowColorThemeUri = canonicalWindowColorThemeUri(profile.windowColorThemeUri);
		if (!normalizeEditExtensionSelectorsInPlace(profile.extensions, errorMessage)) return false;
		if (!normalizeEditProfileOverridesInPlace(profile, errorMessage)) return false;
	}
	if (!validateNormalizedEditProfiles(normalized, errorMessage)) return false;
	configuredEditProfiles() = normalized;
	if (previous != normalized) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredDefaultProfileDescription() {
	return configuredDefaultProfileDescriptionValue();
}

bool setConfiguredDefaultProfileDescription(const std::string &value, std::string *errorMessage) {
	const std::string normalized = trimAscii(value);

	if (configuredDefaultProfileDescriptionValue() != normalized) {
		configuredDefaultProfileDescriptionValue() = normalized;
		markConfiguredSettingsDirty();
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

const std::vector<MRKeymapProfile> &configuredKeymapProfiles() {
	return configuredKeymapProfilesValue();
}

bool setConfiguredKeymapProfiles(const std::vector<MRKeymapProfile> &profiles, std::string *errorMessage) {
	std::vector<MRKeymapProfile> normalized = profiles;
	const std::vector<MRKeymapProfile> previousProfiles = configuredKeymapProfilesValue();
	const std::string previousActive = configuredActiveKeymapProfileValue();
	bool hasDefault = false;
	std::string runtimeError;

	for (MRKeymapProfile &profile : normalized) {
		profile.name = trimAscii(profile.name);
		profile.description = trimAscii(profile.description);
		for (MRKeymapBindingRecord &binding : profile.bindings) {
			binding.profileName = trimAscii(binding.profileName);
			binding.target.target = trimAscii(binding.target.target);
			binding.description = trimAscii(binding.description);
		}
		if (upperAscii(profile.name) == "DEFAULT") {
			profile.name = "DEFAULT";
			hasDefault = true;
		}
	}
	if (!hasDefault) normalized.insert(normalized.begin(), builtInDefaultKeymapProfile());

	const auto diagnostics = validateKeymapProfiles(normalized);
	for (const MRKeymapDiagnostic &diagnostic : diagnostics)
		if (diagnostic.severity == MRKeymapDiagnosticSeverity::Error) return setError(errorMessage, diagnostic.message);
	if (!runtimeKeymapResolver().rebuild(normalized, configuredActiveKeymapProfileValue(), &runtimeError)) return setError(errorMessage, runtimeError);

	configuredKeymapProfilesValue() = normalized;
	if (configuredActiveKeymapProfileValue().empty()) configuredActiveKeymapProfileValue() = "DEFAULT";
	else if (std::ranges::find(normalized, configuredActiveKeymapProfileValue(), &MRKeymapProfile::name) == normalized.end())
		configuredActiveKeymapProfileValue() = "DEFAULT";
	if (previousProfiles != configuredKeymapProfilesValue() || previousActive != configuredActiveKeymapProfileValue()) markConfiguredSettingsDirty();
	mrLogMessage(summarizeConfiguredKeymapsForLog(configuredKeymapProfilesValue(), configuredActiveKeymapProfileValue()));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredKeymapFilePath() {
	const std::string &configured = configuredKeymapFileValue();
	return configured.empty() ? std::string() : makeAbsolutePath(configured);
}

bool setConfiguredKeymapFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	const std::string previousFile = configuredKeymapFileValue();
	const MRScopedDialogHistoryState previousLoadHistory = dialogHistoryState(MRDialogHistoryScope::KeymapProfileLoad);
	const MRScopedDialogHistoryState previousSaveHistory = dialogHistoryState(MRDialogHistoryScope::KeymapProfileSave);
	struct stat st;

	if (normalized.empty()) {
		if (!configuredKeymapFileValue().empty()) {
			configuredKeymapFileValue().clear();
			markConfiguredSettingsDirty();
		}
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (::stat(normalized.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return setError(errorMessage, "Keymap URI must include a filename.");
	configuredKeymapFileValue() = makeAbsolutePath(normalized);
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::KeymapProfileLoad, configuredKeymapFileValue(), nullptr));
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::KeymapProfileSave, configuredKeymapFileValue(), nullptr));
	if (previousFile != configuredKeymapFileValue() || previousLoadHistory != dialogHistoryState(MRDialogHistoryScope::KeymapProfileLoad) || previousSaveHistory != dialogHistoryState(MRDialogHistoryScope::KeymapProfileSave)) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredActiveKeymapProfile() {
	return configuredActiveKeymapProfileValue();
}

bool setConfiguredActiveKeymapProfile(const std::string &value, std::string *errorMessage) {
	std::string normalized = trimAscii(value);
	const std::string previous = configuredActiveKeymapProfileValue();
	std::string runtimeError;

	if (normalized.empty()) normalized = "DEFAULT";
	for (const MRKeymapProfile &profile : configuredKeymapProfilesValue())
		if (profile.name == normalized) {
			if (!runtimeKeymapResolver().rebuild(configuredKeymapProfilesValue(), normalized, &runtimeError)) return setError(errorMessage, runtimeError);
			configuredActiveKeymapProfileValue() = normalized;
			if (previous != configuredActiveKeymapProfileValue()) markConfiguredSettingsDirty();
			mrLogMessage("Keymap active profile set to '" + normalized + "'.");
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		}
	return setError(errorMessage, "Unknown keymap profile: " + normalized);
}

bool applyConfiguredEditExtensionProfileDirective(const std::string &operation, const std::string &profileId, const std::string &arg3, const std::string &arg4, std::string *errorMessage) {
	std::string op = upperAscii(trimAscii(operation));
	std::string id = canonicalEditProfileId(profileId);
	std::vector<MREditExtensionProfile> profiles = configuredEditProfiles();
	MREditExtensionProfile *profile = nullptr;
	std::size_t i = 0;

	if (op.empty()) return setError(errorMessage, "MRFEPROFILE operation may not be empty.");
	if (id.empty()) return setError(errorMessage, "MRFEPROFILE profile id may not be empty.");

	for (i = 0; i < profiles.size(); ++i)
		if (profileIdLookupKey(profiles[i].id) == profileIdLookupKey(id)) {
			profile = &profiles[i];
			break;
		}

	if (op == "DEFINE") {
		std::string name = canonicalEditProfileName(arg3);

		if (name.empty() && trimAscii(arg4).empty()) name = id;
		if (name.empty()) return setError(errorMessage, "MRFEPROFILE DEFINE requires a non-empty display name.");
		if (profile != nullptr) return setError(errorMessage, "Duplicate extension profile id: " + id);
		MREditExtensionProfile created;
		created.id = id;
		created.name = name;
		created.overrides.values = resolveEditSetupDefaults();
		profiles.push_back(created);
		return setConfiguredEditExtensionProfiles(profiles, errorMessage);
	}

	if (profile == nullptr) return setError(errorMessage, "Unknown extension profile id: " + id);

	if (op == "EXT") {
		profile->extensions.push_back(arg3);
		return setConfiguredEditExtensionProfiles(profiles, errorMessage);
	}

	if (op == "SET") {
		if (upperAscii(trimAscii(arg3)) == kWindowColorThemeProfileKey) {
			std::string normalizedTheme = canonicalWindowColorThemeUri(arg4);
			if (!normalizedTheme.empty() && !validateColorThemeFilePath(normalizedTheme, errorMessage)) return false;
			profile->windowColorThemeUri = normalizedTheme;
			return setConfiguredEditExtensionProfiles(profiles, errorMessage);
		}

		const MREditSettingDescriptor *descriptor = editSettingDescriptorByKeyInternal(arg3);

		if (descriptor == nullptr) return setError(errorMessage, "Unknown edit setting key for extension profile.");
		if (!descriptor->profileSupported) return setError(errorMessage, std::string("Setting is global-only and cannot be overridden: ") + descriptor->key);
		if (!applyEditSetupValueInternal(profile->overrides.values, descriptor->key, arg4, errorMessage)) return false;
		profile->overrides.mask |= descriptor->overrideBit;
		return setConfiguredEditExtensionProfiles(profiles, errorMessage);
	}

	return setError(errorMessage, "MRFEPROFILE supports operations DEFINE, EXT and SET.");
}

bool effectiveEditSetupSettingsForPath(const std::string &path, MREditSetupSettings &out, std::string *matchedProfileName) {
	MREditSetupSettings defaults = configuredEditSetupSettings();
	std::string ext = extensionSelectorForPath(path);

	out = defaults;
	if (matchedProfileName != nullptr) matchedProfileName->clear();
	if (ext.empty()) return true;
	for (const auto &profile : configuredEditProfiles())
		for (const std::string &selector : profile.extensions)
			if (selector == ext) {
				out = mergeEditSetupSettings(defaults, profile.overrides);
				if (matchedProfileName != nullptr) *matchedProfileName = profile.name;
				return true;
			}
	return true;
}

bool effectiveEditWindowColorThemePathForPath(const std::string &path, std::string &themeUri, std::string *matchedProfileName) {
	std::string ext = extensionSelectorForPath(path);

	themeUri = configuredColorThemeFilePath();
	if (matchedProfileName != nullptr) matchedProfileName->clear();
	if (ext.empty()) return true;
	for (const auto &profile : configuredEditProfiles())
		for (const std::string &selector : profile.extensions)
			if (selector == ext) {
				if (!profile.windowColorThemeUri.empty()) themeUri = profile.windowColorThemeUri;
				if (matchedProfileName != nullptr) *matchedProfileName = profile.name;
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
	const MREditSetupSettings previous = configuredEditSettings();
	MREditSetupSettings normalized = settings;
	std::string pageBreak = normalizePageBreakLiteral(settings.pageBreak);
	std::string wordDelimiters = settings.wordDelimiters.empty() ? defaults.wordDelimiters : settings.wordDelimiters;
	std::string defaultExts = canonicalDefaultExtensionsLiteral(settings.defaultExtensions);
	std::string columnStyle = normalizeColumnBlockMove(settings.columnBlockMove);
	std::string defaultMode = normalizeDefaultMode(settings.defaultMode);
	std::string indentStyle = normalizeIndentStyle(settings.indentStyle);
	std::string fileType = normalizeFileType(settings.fileType);
	std::string codeLanguage = upperAscii(trimAscii(settings.codeLanguage));
	std::string lineNumbersPosition = normalizeLineNumbersPosition(settings.lineNumbersPosition);
	std::string miniMapPosition = normalizeMiniMapPosition(settings.miniMapPosition);
	std::string codeFoldingPosition = normalizeCodeFoldingPosition(settings.codeFoldingPosition);
	std::string gutters = normalizeGuttersOrder(settings.gutters);
	std::string formatLine;
	std::string cursorStatusColor;
	std::string miniMapMarkerGlyph;
	int normalizedFormatLeftMargin = settings.leftMargin;
	int normalizedFormatRightMargin = settings.rightMargin;
	std::string postLoadMacro = trimAscii(settings.postLoadMacro).empty() ? std::string() : normalizeConfiguredPathInput(settings.postLoadMacro);
	std::string preSaveMacro = trimAscii(settings.preSaveMacro).empty() ? std::string() : normalizeConfiguredPathInput(settings.preSaveMacro);
	std::string defaultPath = trimAscii(settings.defaultPath).empty() ? std::string() : normalizeConfiguredPathInput(settings.defaultPath);

	if (wordDelimiters.empty()) return setError(errorMessage, "WORD_DELIMITERS may not be empty.");
	if (columnStyle.empty()) return setError(errorMessage, "COLUMN_BLOCK_MOVE must be DELETE_SPACE or LEAVE_SPACE.");
	if (defaultMode.empty()) return setError(errorMessage, "DEFAULT_MODE must be INSERT or OVERWRITE.");
	if (indentStyle.empty()) return setError(errorMessage, "INDENT_STYLE must be OFF, AUTOMATIC or SMART.");
	if (codeLanguage.empty()) codeLanguage = "NONE";
	if (codeLanguage != "NONE" && codeLanguage != "AUTO" && codeLanguage != "C" && codeLanguage != "CPP" && codeLanguage != "PYTHON" && codeLanguage != "JAVASCRIPT" && codeLanguage != "TYPESCRIPT" && codeLanguage != "TSX" && codeLanguage != "BASH" && codeLanguage != "JSON" && codeLanguage != "PERL" && codeLanguage != "SWIFT")
		return setError(errorMessage, "CODE_LANGUAGE must be NONE, AUTO, C, CPP, PYTHON, JAVASCRIPT, TYPESCRIPT, TSX, BASH, JSON, PERL or SWIFT.");
	if (fileType.empty()) return setError(errorMessage, "FILE_TYPE must be LEGACY_TEXT, UNIX or BINARY.");
	if (lineNumbersPosition.empty()) lineNumbersPosition = settings.showLineNumbers ? kLineNumbersPositionLeading : kLineNumbersPositionOff;
	if (miniMapPosition.empty()) return setError(errorMessage, "MINIMAP_POSITION must be OFF, LEADING or TRAILING.");
	if (codeFoldingPosition.empty()) codeFoldingPosition = settings.codeFolding ? kCodeFoldingPositionLeading : kCodeFoldingPositionOff;
	if (!normalizeCursorStatusColor(settings.cursorStatusColor, cursorStatusColor, errorMessage)) return false;
	if (!normalizeMiniMapMarkerGlyph(settings.miniMapMarkerGlyph, miniMapMarkerGlyph, errorMessage)) return false;
	if (settings.binaryRecordLength < kMinBinaryRecordLength || settings.binaryRecordLength > kMaxBinaryRecordLength) return setError(errorMessage, "BINARY_RECORD_LENGTH must be between 1 and 99999.");
	if (settings.tabSize < kMinTabSize || settings.tabSize > kMaxTabSize) return setError(errorMessage, "TAB_SIZE must be between 2 and 32.");
	if (settings.leftMargin < kMinLeftMargin || settings.leftMargin > kMaxLeftMargin) return setError(errorMessage, "LEFT_MARGIN must be between 1 and 999.");
	if (settings.rightMargin < kMinRightMargin || settings.rightMargin > kMaxRightMargin) return setError(errorMessage, "RIGHT_MARGIN must be between 1 and 999.");
	if (!normalizeEditFormatLine(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, formatLine, &normalizedFormatLeftMargin, &normalizedFormatRightMargin, errorMessage)) return false;
	if (settings.rightMargin > 1 && settings.leftMargin >= settings.rightMargin) return setError(errorMessage, "LEFT_MARGIN must be less than RIGHT_MARGIN.");
	if (settings.miniMapWidth < kMinMiniMapWidth || settings.miniMapWidth > kMaxMiniMapWidth) return setError(errorMessage, "MINIMAP_WIDTH must be between 2 and 20.");

	normalized.truncateSpaces = settings.truncateSpaces;
	normalized.eofCtrlZ = settings.eofCtrlZ;
	normalized.eofCrLf = settings.eofCrLf;
	normalized.tabExpand = settings.tabExpand;
	normalized.displayTabs = settings.displayTabs;
	normalized.tabSize = settings.tabSize;
	normalized.leftMargin = settings.leftMargin;
	normalized.rightMargin = settings.rightMargin;
	normalized.formatRuler = settings.formatRuler;
	normalized.wordWrap = settings.wordWrap;
	normalized.indentStyle = indentStyle;
	normalized.codeLanguage = codeLanguage;
	normalized.codeColoring = settings.codeColoring;
	normalized.codeFoldingFeature = settings.codeFoldingFeature;
	normalized.smartIndenting = settings.smartIndenting;
	normalized.fileType = fileType;
	normalized.binaryRecordLength = settings.binaryRecordLength;
	normalized.postLoadMacro = postLoadMacro;
	normalized.preSaveMacro = preSaveMacro;
	normalized.defaultPath = defaultPath;
	normalized.formatLine = synchronizeEditFormatLineMargins(formatLine, normalized.leftMargin, normalized.rightMargin, normalized.tabSize);
	normalized.backupMethod = normalizeBackupMethod(settings.backupMethod);
	if (normalized.backupMethod.empty()) return setError(errorMessage, "BACKUP_METHOD must be OFF, BAK_FILE or DIRECTORY.");
	normalized.backupFrequency = normalizeBackupFrequency(settings.backupFrequency);
	if (normalized.backupFrequency.empty()) return setError(errorMessage, "BACKUP_FREQUENCY must be FIRST_SAVE_ONLY or EVERY_SAVE.");
	normalized.backupExtension = normalizeBackupExtension(settings.backupExtension);
	normalized.backupDirectory = trimAscii(settings.backupDirectory).empty() ? std::string() : normalizeConfiguredPathInput(settings.backupDirectory);
	if (normalized.backupMethod == kBackupMethodBakFile && !validateBackupExtension(normalized.backupExtension, errorMessage)) return false;
	if (normalized.backupMethod == kBackupMethodDirectory && !validateWritableDirectoryPath(normalized.backupDirectory, "BACKUP_DIRECTORY", errorMessage)) return false;
	if (normalized.backupMethod != kBackupMethodBakFile) normalized.backupExtension = normalizeBackupExtension(settings.backupExtension).empty() ? defaults.backupExtension : normalizeBackupExtension(settings.backupExtension);
	if (normalized.backupMethod != kBackupMethodDirectory && trimAscii(normalized.backupDirectory).empty()) normalized.backupDirectory.clear();
	if (settings.autosaveInactivitySeconds != 0 && (settings.autosaveInactivitySeconds < kMinAutosaveInactivitySeconds || settings.autosaveInactivitySeconds > kMaxAutosaveInactivitySeconds)) return setError(errorMessage, "AUTOSAVE_INACTIVITY_SECONDS must be 0 or within 5..100 seconds.");
	if (settings.autosaveIntervalSeconds != 0 && (settings.autosaveIntervalSeconds < kMinAutosaveIntervalSeconds || settings.autosaveIntervalSeconds > kMaxAutosaveIntervalSeconds)) return setError(errorMessage, "AUTOSAVE_INTERVAL_SECONDS must be 0 or within 100..300 seconds.");
	normalized.autosaveInactivitySeconds = settings.autosaveInactivitySeconds;
	normalized.autosaveIntervalSeconds = settings.autosaveIntervalSeconds;
	normalized.backupFiles = normalized.backupMethod != kBackupMethodOff;
	normalized.showEofMarker = settings.showEofMarker;
	normalized.showEofMarkerEmoji = settings.showEofMarkerEmoji;
	normalized.showLineNumbers = lineNumbersPosition != kLineNumbersPositionOff;
	normalized.lineNumbersPosition = lineNumbersPosition;
	normalized.lineNumZeroFill = settings.lineNumZeroFill;
	normalized.miniMapPosition = miniMapPosition;
	normalized.miniMapWidth = settings.miniMapWidth;
	normalized.miniMapMarkerGlyph = miniMapMarkerGlyph;
	normalized.gutters = gutters;
	normalized.persistentBlocks = settings.persistentBlocks;
	normalized.codeFolding = codeFoldingPosition != kCodeFoldingPositionOff;
	normalized.codeFoldingPosition = codeFoldingPosition;

	normalized.pageBreak = pageBreak;
	normalized.wordDelimiters = wordDelimiters;
	normalized.defaultExtensions = defaultExts;
	normalized.columnBlockMove = columnStyle;
	normalized.defaultMode = defaultMode;
	normalized.cursorStatusColor = cursorStatusColor;
	configuredEditSettings() = normalized;
	if (previous != normalized) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setConfiguredColorSetupGroupValues(MRColorSetupGroup group, const unsigned char *values, std::size_t count, std::string *errorMessage) {
	const ColorGroupDefinition *definition = findColorGroupDefinition(group);
	MRColorSetupSettings &configured = configuredColorSettings();
	const MRColorSetupSettings previous = configuredColorSetupSettings();

	ensureConfiguredColorSettingsInitialized();
	if (definition == nullptr) return setError(errorMessage, "Unknown color setup group.");
	if (values == nullptr || count != definition->count) return setError(errorMessage, "Unexpected color setup group value count.");

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
		case MRColorSetupGroup::MiniMap:
			for (std::size_t i = 0; i < configured.miniMapColors.size(); ++i)
				configured.miniMapColors[i] = values[i];
			break;
		case MRColorSetupGroup::Code:
			for (std::size_t i = 0; i < configured.codeColors.size(); ++i)
				configured.codeColors[i] = values[i];
			break;
	}
	if (previous != configured) markConfiguredSettingsDirty();

	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

void configuredColorSetupGroupValues(MRColorSetupGroup group, unsigned char *values, std::size_t count) {
	const ColorGroupDefinition *definition = findColorGroupDefinition(group);
	MRColorSetupSettings configured = configuredColorSetupSettings();

	if (values == nullptr || definition == nullptr || count != definition->count) return;

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
		case MRColorSetupGroup::MiniMap:
			for (std::size_t i = 0; i < configured.miniMapColors.size(); ++i)
				values[i] = configured.miniMapColors[i];
			break;
		case MRColorSetupGroup::Code:
			for (std::size_t i = 0; i < configured.codeColors.size(); ++i)
				values[i] = configured.codeColors[i];
			break;
	}
}

std::string defaultColorThemeFilePath() {
	return defaultColorThemePathForSettings(configuredSettingsMacroFilePath());
}

bool validateColorThemeFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	struct stat st;

	if (normalized.empty()) return setError(errorMessage, "Empty color theme URI.");
	if (::stat(normalized.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return setError(errorMessage, "Color theme URI must include a filename.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setConfiguredColorThemeFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	const std::string previousTheme = configuredColorThemeFile();
	const MRScopedDialogHistoryState previousLoadHistory = dialogHistoryState(MRDialogHistoryScope::SetupThemeLoad);
	const MRScopedDialogHistoryState previousSaveHistory = dialogHistoryState(MRDialogHistoryScope::SetupThemeSave);

	if (!validateColorThemeFilePath(path, errorMessage)) return false;
	configuredColorThemeFile() = makeAbsolutePath(normalized);
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::SetupThemeLoad, configuredColorThemeFile(), nullptr));
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::SetupThemeSave, configuredColorThemeFile(), nullptr));
	if (previousTheme != configuredColorThemeFile() || previousLoadHistory != dialogHistoryState(MRDialogHistoryScope::SetupThemeLoad) || previousSaveHistory != dialogHistoryState(MRDialogHistoryScope::SetupThemeSave)) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredColorThemeFilePath() {
	const std::string &configured = configuredColorThemeFile();
	if (!configured.empty()) return makeAbsolutePath(configured);
	return defaultColorThemeFilePath();
}

std::string configuredColorThemeDisplayName() {
	std::string path = configuredColorThemeFilePath();
	std::string name = fileNamePartOf(path);

	if (name.empty()) return std::string("<none>");
	return name;
}

std::string buildColorThemeMacroSource(const MRColorSetupSettings &colors) {
	std::string source;

	source += "$MACRO MR_COLOR_THEME FROM EDIT;\n";
	source += "MRSETUP('WINDOWCOLORS', '" + escapeMrmacSingleQuotedLiteral(formatWindowColorListLiteral(colors.windowColors)) + "');\n";
	source += "MRSETUP('MENUDIALOGCOLORS', '" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.menuDialogColors)) + "');\n";
	source += "MRSETUP('HELPCOLORS', '" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.helpColors)) + "');\n";
	source += "MRSETUP('OTHERCOLORS', '" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.otherColors)) + "');\n";
	source += "MRSETUP('MINIMAPCOLORS', '" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.miniMapColors)) + "');\n";
	source += "MRSETUP('CODECOLORS', '" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.codeColors)) + "');\n";
	source += "END_MACRO;\n";
	return source;
}

bool writeColorThemeFile(const std::string &themeUri, std::string *errorMessage) {
	std::string themePath = normalizeConfiguredPathInput(themeUri);
	std::string themeDir = directoryPartOf(themePath);
	std::string source;

	if (!validateColorThemeFilePath(themePath, errorMessage)) return false;
	if (!ensureDirectoryTree(themeDir, errorMessage)) return false;
	source = buildColorThemeMacroSource(configuredColorSetupSettings());
	if (!writeTextFile(themePath, source)) return setError(errorMessage, "Unable to write color theme file: " + themePath);
	if (!setConfiguredColorThemeFilePath(themePath, errorMessage)) return false;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool ensureColorThemeFileExists(const std::string &themeUri, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(themeUri);
	std::string themeDir = directoryPartOf(normalized);
	struct stat st;

	if (!validateColorThemeFilePath(normalized, errorMessage)) return false;
	if (::stat(normalized.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode)) return setError(errorMessage, "Color theme URI must include a filename.");
		if (errorMessage != nullptr) errorMessage->clear();
			return true;
	}
	if (!ensureDirectoryTree(themeDir, errorMessage)) return false;
	if (!writeTextFile(normalized, buildColorThemeMacroSource(resolveColorSetupDefaults()))) return setError(errorMessage, "Unable to write color theme file: " + normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool loadColorThemeFile(const std::string &themeUri, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(themeUri);
	std::string source;
	std::string applyError;
	std::map<std::string, std::string> assignments;
	static const char *const order[] = {"WINDOWCOLORS", "MENUDIALOGCOLORS", "HELPCOLORS", "OTHERCOLORS", "MINIMAPCOLORS", "CODECOLORS"};

	if (!validateColorThemeFilePath(normalized, errorMessage)) return false;
	if (!ensureColorThemeFileExists(normalized, errorMessage)) return false;
	if (!readTextFile(normalized, source)) return setError(errorMessage, "Unable to read color theme file: " + normalized);
	if (!parseThemeSetupAssignments(source, assignments, errorMessage)) return false;
	for (const char *key : order)
		if (!applyConfiguredColorSetupValue(key, assignments[key], &applyError)) return setError(errorMessage, "Theme apply failed for " + std::string(key) + ": " + applyError);
	if (!setConfiguredColorThemeFilePath(normalized, errorMessage)) return false;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool loadWindowColorThemeGroupValues(const std::string &themeUri, std::array<unsigned char, MRColorSetupSettings::kWindowCount> &outValues, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(themeUri);
	std::string source;
	std::map<std::string, std::string> assignments;

	if (!validateColorThemeFilePath(normalized, errorMessage)) return false;
	if (!ensureColorThemeFileExists(normalized, errorMessage)) return false;
	if (!readTextFile(normalized, source)) return setError(errorMessage, "Unable to read color theme file: " + normalized);
	if (!parseThemeSetupAssignments(source, assignments, errorMessage)) return false;
	if (!parseWindowColorListLiteral(assignments["WINDOWCOLORS"], outValues, errorMessage)) return false;
	if (errorMessage != nullptr) errorMessage->clear();
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

bool applyColorSetupValueInternal(MRColorSetupSettings &configured, const std::string &key, const std::string &value, std::string *errorMessage) {
	const ColorGroupDefinition *definition = findColorGroupDefinitionByKey(key);

	if (definition == nullptr) return setError(errorMessage, "Unknown color setup key.");
	switch (definition->group) {
		case MRColorSetupGroup::Window:
			if (!parseWindowColorListLiteral(value, configured.windowColors, errorMessage)) return false;
			break;
		case MRColorSetupGroup::MenuDialog:
			if (!parseMenuDialogColorListLiteral(value, configured.menuDialogColors, errorMessage)) return false;
			break;
		case MRColorSetupGroup::Help:
			if (!parseColorListLiteral(value, configured.helpColors, errorMessage)) return false;
			break;
		case MRColorSetupGroup::Other:
			if (!parseOtherColorListLiteral(value, configured.otherColors, errorMessage)) return false;
			break;
		case MRColorSetupGroup::MiniMap:
			if (!parseColorListLiteral(value, configured.miniMapColors, errorMessage)) return false;
			break;
		case MRColorSetupGroup::Code:
			if (!parseColorListLiteral(value, configured.codeColors, errorMessage)) return false;
			break;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool applyConfiguredColorSetupValue(const std::string &key, const std::string &value, std::string *errorMessage) {
	MRColorSetupSettings configured = configuredColorSetupSettings();
	const MRColorSetupSettings previous = configured;

	if (!applyColorSetupValueInternal(configured, key, value, errorMessage)) return false;

	configuredColorSettings() = configured;
	configuredColorSettingsInitialized() = true;
	if (previous != configured) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredColorSlotOverride(unsigned char paletteIndex, unsigned char &value) {
	MRColorSetupSettings configured = configuredColorSetupSettings();
	unsigned char dialogFrame = 0;
	unsigned char dialogText = 0;
	unsigned char dialogBackground = 0;
	unsigned char dialogInactiveCluster = 0;
	unsigned char dialogInactiveElements = 0;
	unsigned char dialogListNormal = 0;
	unsigned char dialogListFocused = 0;
	unsigned char dialogListSelected = 0;
	unsigned char dropListNormal = 0;
	unsigned char dropListSelected = 0;

	// Turbo Vision menu hotkeys use two app slots:
	// - slot 4: shortcut text (normal item)
	// - slot 7: shortcut selection (selected item)
	// Our setup exposes a single "entry-hotkey" control (slot 4), so keep both in sync.
	if (paletteIndex == kPaletteMenuSelectedHotkey) {
		for (std::size_t i = 0; i < std::size(kMenuDialogColorItems); ++i)
			if (kMenuDialogColorItems[i].paletteIndex == kPaletteMenuHotkey) {
				value = configured.menuDialogColors[i];
				return true;
			}
	}

	for (std::size_t i = 0; i < std::size(kMenuDialogColorItems); ++i) {
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteGrayDialogFrame) dialogFrame = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteGrayDialogText) dialogText = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteGrayDialogBackground) dialogBackground = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogListNormal) dialogListNormal = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogListFocused) dialogListFocused = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogListSelectedInactive) dialogListSelected = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kMrPaletteDropListDescription) dropListNormal = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kMrPaletteDropListSelectedInactive) dropListSelected = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogInactiveClusterGray) dialogInactiveCluster = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kMrPaletteDialogInactiveElements) dialogInactiveElements = configured.menuDialogColors[i];
	}

	switch (paletteIndex) {
		case kPaletteGrayDialogFrame:
		case kPaletteGrayDialogFrameAccent:
		case kPaletteBlueDialogFrame:
		case kPaletteBlueDialogFrameAccent:
		case kPaletteCyanDialogFrame:
		case kPaletteCyanDialogFrameAccent:
			value = dialogFrame;
			return true;
		case kPaletteGrayDialogText:
		case kPaletteBlueDialogText:
		case kPaletteCyanDialogText:
			value = dialogText;
			return true;
		case kPaletteGrayDialogBackground:
		case kPaletteBlueDialogBackground:
		case kPaletteCyanDialogBackground:
			value = dialogBackground;
			return true;
		case kPaletteDialogListFrameLegacyPrimary:
		case kPaletteDialogListFrameLegacySecondary:
		case kPaletteDialogListFrameExtendedPrimary:
		case kPaletteDialogListFrameExtendedSecondary:
			value = dialogFrame;
			return true;
		case kPaletteDialogListNormalLegacy:
		case kPaletteDialogListNormal:
			value = dialogListNormal;
			return true;
		case kPaletteDialogListFocusedLegacy:
		case kPaletteDialogListFocused:
			value = dialogListFocused;
			return true;
		case kPaletteDialogListSelectedLegacy:
		case kPaletteDialogListSelectedInactive:
			value = dialogListSelected;
			return true;
		case kPaletteDialogListTextLegacy:
		case kPaletteDialogListText:
			value = dialogText;
			return true;
		case kMrPaletteDropListDescription:
			value = dropListNormal;
			return true;
		case kMrPaletteDropListSelectedInactive:
			value = dropListSelected;
			return true;
		case kPaletteDialogInactiveClusterGray:
		case kPaletteDialogInactiveClusterBlue:
		case kPaletteDialogInactiveClusterCyan:
			value = dialogInactiveCluster;
			return true;
		case kMrPaletteDialogInactiveElements:
			value = dialogInactiveElements;
			return true;
		case kMrPaletteDesktop:
			value = configured.otherColors[MRColorSetupSettings::kOtherCount - 2];
			return true;
		case kMrPaletteVirtualDesktopMarker:
			value = configured.otherColors[MRColorSetupSettings::kOtherCount - 1];
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
	for (std::size_t i = 0; i < std::size(kMiniMapColorItems); ++i)
		if (kMiniMapColorItems[i].paletteIndex == paletteIndex) {
			value = configured.miniMapColors[i];
			return true;
		}
	for (std::size_t i = 0; i < std::size(kCodeColorItems); ++i)
		if (kCodeColorItems[i].paletteIndex == paletteIndex) {
			value = configured.codeColors[i];
			return true;
		}
	return false;
}

bool applyConfiguredEditSetupValue(const std::string &key, const std::string &value, std::string *errorMessage) {
	MREditSetupSettings current = configuredEditSetupSettings();

	if (!applyEditSetupValueInternal(current, key, value, errorMessage)) return false;
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

bool configuredDisplayTabsSetting() {
	return configuredEditSetupSettings().displayTabs;
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

bool parseHistoryLimitLiteral(const std::string &value, int &outValue, std::string *errorMessage, const char *keyName) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty()) return setError(errorMessage, std::string(keyName) + " must be an integer within 5..50.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return setError(errorMessage, std::string(keyName) + " must be an integer within 5..50.");
	if (parsed < kHistoryLimitMin || parsed > kHistoryLimitMax) return setError(errorMessage, std::string(keyName) + " must be within 5..50.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setConfiguredPathHistoryLimitValue(int value, std::string *errorMessage) {
	const int previousLimit = configuredPathHistoryLimit();
	const auto previousStates = configuredDialogHistoryStorage();

	if (value < kHistoryLimitMin || value > kHistoryLimitMax) return setError(errorMessage, "MAX_PATH_HISTORY must be within 5..50.");
	configuredPathHistoryLimit() = value;
	for (MRScopedDialogHistoryState &state : configuredDialogHistoryStorage())
		trimHistoryToLimit(state.pathHistory, value);
	if (previousLimit != configuredPathHistoryLimit() || previousStates != configuredDialogHistoryStorage()) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setConfiguredFileHistoryLimitValue(int value, std::string *errorMessage) {
	const int previousLimit = configuredFileHistoryLimit();
	const auto previousStates = configuredDialogHistoryStorage();

	if (value < kHistoryLimitMin || value > kHistoryLimitMax) return setError(errorMessage, "MAX_FILE_HISTORY must be within 5..50.");
	configuredFileHistoryLimit() = value;
	for (MRScopedDialogHistoryState &state : configuredDialogHistoryStorage())
		trimHistoryToLimit(state.fileHistory, value);
	if (previousLimit != configuredFileHistoryLimit() || previousStates != configuredDialogHistoryStorage()) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

int configuredMaxPathHistory() {
	return configuredPathHistoryLimit();
}

int configuredMaxFileHistory() {
	return configuredFileHistoryLimit();
}

void configuredPathHistoryEntries(std::vector<std::string> &outValues) {
	outValues.clear();
	for (const MRDialogHistoryEntry &entry : dialogHistoryState(MRDialogHistoryScope::General).pathHistory)
		outValues.push_back(entry.value);
}

void configuredFileHistoryEntries(std::vector<std::string> &outValues) {
	outValues.clear();
	for (const MRDialogHistoryEntry &entry : dialogHistoryState(MRDialogHistoryScope::General).fileHistory)
		outValues.push_back(entry.value);
}

void configuredMultiFilespecHistoryEntries(std::vector<std::string> &outValues) {
	outValues.clear();
	for (const MRDialogHistoryEntry &entry : configuredMultiFilespecHistoryStorage())
		outValues.push_back(entry.value);
}

void configuredMultiPathHistoryEntries(std::vector<std::string> &outValues) {
	outValues.clear();
	for (const MRDialogHistoryEntry &entry : configuredMultiPathHistoryStorage())
		outValues.push_back(entry.value);
}

bool addConfiguredMultiFilespecHistoryEntry(const std::string &value, std::string *errorMessage) {
	const auto previous = configuredMultiFilespecHistoryStorage();

	addHistoryEntry(configuredMultiFilespecHistoryStorage(), trimAscii(value), configuredFileHistoryLimit());
	if (previous != configuredMultiFilespecHistoryStorage()) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool addConfiguredMultiPathHistoryEntry(const std::string &value, std::string *errorMessage) {
	const auto previous = configuredMultiPathHistoryStorage();

	addHistoryEntry(configuredMultiPathHistoryStorage(), normalizeConfiguredPathInput(value), configuredPathHistoryLimit());
	if (previous != configuredMultiPathHistoryStorage()) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
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
	const std::string key = autoexecDiagnosticKey(fileName);

	if (key.empty()) return;
	g_autoexecMacroDiagnostics[key] = errorText;
}

bool configuredAutoexecMacroDiagnosticForFile(const std::string &fileName, std::string &errorText) {
	const std::string key = autoexecDiagnosticKey(fileName);
	auto it = g_autoexecMacroDiagnostics.find(key);

	errorText.clear();
	if (it == g_autoexecMacroDiagnostics.end()) return false;
	errorText = it->second;
	return true;
}

bool setScopedDialogLastPath(MRDialogHistoryScope scope, const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	std::string directory;
	MRScopedDialogHistoryState &state = dialogHistoryState(scope);
	const MRScopedDialogHistoryState previous = state;

	if (!normalized.empty() && isReadableDirectory(normalized)) {
		state.lastPath = normalized;
		addHistoryEntry(state.pathHistory, normalized, configuredPathHistoryLimit());
	} else if (!normalized.empty()) {
		addHistoryEntry(state.fileHistory, normalized, configuredFileHistoryLimit());
		directory = normalizedDialogDirectoryFromPath(normalized);
		if (!directory.empty()) {
			state.lastPath = directory;
			addHistoryEntry(state.pathHistory, directory, configuredPathHistoryLimit());
		}
	} else if (state.lastPath.empty()) {
		directory = fallbackRememberedLoadDirectory();
		if (!directory.empty()) {
			state.lastPath = directory;
			addHistoryEntry(state.pathHistory, directory, configuredPathHistoryLimit());
		}
	}
	if (previous != state) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

void initRememberedLoadDialogPath(MRDialogHistoryScope scope, char *buffer, std::size_t bufferSize, const char *pattern) {
	std::string initial;
	std::string dir = effectiveRememberedLoadDirectory(scope);
	const char *safePattern = (pattern != nullptr && *pattern != '\0') ? pattern : "*.*";

	if (!dir.empty()) {
		initial = dir;
		if (initial.back() != '/') initial += '/';
		initial += safePattern;
	} else
		initial = safePattern;
	copyToBuffer(buffer, bufferSize, initial);
}

void rememberLoadDialogPath(MRDialogHistoryScope scope, const char *path) {
	static_cast<void>(setScopedDialogLastPath(scope, path != nullptr ? path : "", nullptr));
}

void forgetLoadDialogPath(MRDialogHistoryScope scope, const char *path) {
	const std::string normalized = normalizeConfiguredPathInput(path != nullptr ? path : "");
	MRScopedDialogHistoryState &state = dialogHistoryState(scope);
	const MRScopedDialogHistoryState previous = state;

	if (normalized.empty()) return;
	state.fileHistory.erase(std::remove_if(state.fileHistory.begin(), state.fileHistory.end(), [&](const MRDialogHistoryEntry &entry) { return normalizeConfiguredPathInput(entry.value) == normalized; }), state.fileHistory.end());
	if (previous != state) markConfiguredSettingsDirty();
}

std::string configuredLastFileDialogFilePath(MRDialogHistoryScope scope) {
	return latestHistoryValue(dialogHistoryState(scope).fileHistory);
}

std::string configuredLastFileDialogPath(MRDialogHistoryScope scope) {
	return effectiveRememberedLoadDirectory(scope);
}

void configuredScopedDialogFileHistoryEntries(MRDialogHistoryScope scope, std::vector<std::string> &outValues) {
	outValues.clear();
	for (const MRDialogHistoryEntry &entry : dialogHistoryState(scope).fileHistory)
		outValues.push_back(entry.value);
}

void configuredScopedDialogPathHistoryEntries(MRDialogHistoryScope scope, std::vector<std::string> &outValues) {
	outValues.clear();
	for (const MRDialogHistoryEntry &entry : dialogHistoryState(scope).pathHistory)
		outValues.push_back(entry.value);
}

bool setConfiguredLastFileDialogPath(const std::string &path, std::string *errorMessage) {
	return setScopedDialogLastPath(MRDialogHistoryScope::General, path, errorMessage);
}

std::string configuredLastFileDialogPath() {
	return effectiveRememberedLoadDirectory(MRDialogHistoryScope::General);
}

std::string buildSettingsMacroSource(const MRSettingsSnapshot &snapshot) {
	std::string settingsPath = normalizeConfiguredPathInput(snapshot.paths.settingsMacroUri);
	std::string macroDir = normalizeConfiguredPathInput(snapshot.paths.macroPath);
	std::string helpPath = normalizeConfiguredPathInput(snapshot.paths.helpUri);
	std::string tempDir = normalizeConfiguredPathInput(snapshot.paths.tempPath);
	std::string shellPath = normalizeConfiguredPathInput(snapshot.paths.shellUri);
	std::string themePath = snapshot.colorThemeFilePath.empty() ? defaultColorThemePathForSettings(settingsPath) : normalizeConfiguredPathInput(snapshot.colorThemeFilePath);
	const MREditSetupSettings &edit = snapshot.editSettings;
	std::string source;
	std::size_t descriptorCount = 0;
	const MREditSettingDescriptor *descriptors = editSettingDescriptors(descriptorCount);

	source += "$MACRO MR_SETTINGS FROM EDIT;\n";
	source += "MRSETUP('" + std::string(kSettingsVersionKey) + "', '" + escapeMrmacSingleQuotedLiteral(kCurrentSettingsVersion) + "');\n";
	source += "MRSETUP('SETTINGSPATH', '" + escapeMrmacSingleQuotedLiteral(settingsPath) + "');\n";
	source += "MRSETUP('MACROPATH', '" + escapeMrmacSingleQuotedLiteral(macroDir) + "');\n";
	source += "MRSETUP('HELPPATH', '" + escapeMrmacSingleQuotedLiteral(helpPath) + "');\n";
	source += "MRSETUP('TEMPDIR', '" + escapeMrmacSingleQuotedLiteral(tempDir) + "');\n";
	source += "MRSETUP('SHELLPATH', '" + escapeMrmacSingleQuotedLiteral(shellPath) + "');\n";
	source += "MRSETUP('WINDOW_MANAGER', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.windowManagerEnabled)) + "');\n";
	source += "MRSETUP('MESSAGES', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.menulineMessagesEnabled)) + "');\n";
	source += "MRSETUP('SEARCH_TEXT_TYPE', '" + escapeMrmacSingleQuotedLiteral(formatSearchTextType(snapshot.searchDialogOptions.textType)) + "');\n";
	source += "MRSETUP('SEARCH_DIRECTION', '" + escapeMrmacSingleQuotedLiteral(formatSearchDirection(snapshot.searchDialogOptions.direction)) + "');\n";
	source += "MRSETUP('SEARCH_MODE', '" + escapeMrmacSingleQuotedLiteral(formatSearchMode(snapshot.searchDialogOptions.mode)) + "');\n";
	source += "MRSETUP('SEARCH_CASE_SENSITIVE', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.searchDialogOptions.caseSensitive)) + "');\n";
	source += "MRSETUP('SEARCH_GLOBAL_SEARCH', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.searchDialogOptions.globalSearch)) + "');\n";
	source += "MRSETUP('SEARCH_RESTRICT_MARKED_BLOCK', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.searchDialogOptions.restrictToMarkedBlock)) + "');\n";
	source += "MRSETUP('SEARCH_ALL_WINDOWS', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.searchDialogOptions.searchAllWindows)) + "');\n";
	source += "MRSETUP('SAR_TEXT_TYPE', '" + escapeMrmacSingleQuotedLiteral(formatSearchTextType(snapshot.sarDialogOptions.textType)) + "');\n";
	source += "MRSETUP('SAR_DIRECTION', '" + escapeMrmacSingleQuotedLiteral(formatSearchDirection(snapshot.sarDialogOptions.direction)) + "');\n";
	source += "MRSETUP('SAR_MODE', '" + escapeMrmacSingleQuotedLiteral(formatSarMode(snapshot.sarDialogOptions.mode)) + "');\n";
	source += "MRSETUP('SAR_LEAVE_CURSOR_AT', '" + escapeMrmacSingleQuotedLiteral(formatSarLeaveCursor(snapshot.sarDialogOptions.leaveCursorAt)) + "');\n";
	source += "MRSETUP('SAR_CASE_SENSITIVE', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.sarDialogOptions.caseSensitive)) + "');\n";
	source += "MRSETUP('SAR_GLOBAL_SEARCH', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.sarDialogOptions.globalSearch)) + "');\n";
	source += "MRSETUP('SAR_RESTRICT_MARKED_BLOCK', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.sarDialogOptions.restrictToMarkedBlock)) + "');\n";
	source += "MRSETUP('SAR_ALL_WINDOWS', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.sarDialogOptions.searchAllWindows)) + "');\n";
	source += "MRSETUP('MULTI_SEARCH_FILESPEC', '" + escapeMrmacSingleQuotedLiteral(snapshot.multiSearchDialogOptions.filespec) + "');\n";
	source += "MRSETUP('MULTI_SEARCH_TEXT', '" + escapeMrmacSingleQuotedLiteral(snapshot.multiSearchDialogOptions.searchText) + "');\n";
	source += "MRSETUP('MULTI_SEARCH_STARTING_PATH', '" + escapeMrmacSingleQuotedLiteral(normalizeConfiguredPathInput(snapshot.multiSearchDialogOptions.startingPath)) + "');\n";
	source += "MRSETUP('MULTI_SEARCH_SUBDIRECTORIES', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSearchDialogOptions.searchSubdirectories)) + "');\n";
	source += "MRSETUP('MULTI_SEARCH_CASE_SENSITIVE', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSearchDialogOptions.caseSensitive)) + "');\n";
	source += "MRSETUP('MULTI_SEARCH_REGULAR_EXPRESSIONS', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSearchDialogOptions.regularExpressions)) + "');\n";
	source += "MRSETUP('MULTI_SEARCH_FILES_IN_MEMORY', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSearchDialogOptions.searchFilesInMemory)) + "');\n";
	source += "MRSETUP('MULTI_SAR_FILESPEC', '" + escapeMrmacSingleQuotedLiteral(snapshot.multiSarDialogOptions.filespec) + "');\n";
	source += "MRSETUP('MULTI_SAR_TEXT', '" + escapeMrmacSingleQuotedLiteral(snapshot.multiSarDialogOptions.searchText) + "');\n";
	source += "MRSETUP('MULTI_SAR_REPLACEMENT', '" + escapeMrmacSingleQuotedLiteral(snapshot.multiSarDialogOptions.replacementText) + "');\n";
	source += "MRSETUP('MULTI_SAR_STARTING_PATH', '" + escapeMrmacSingleQuotedLiteral(normalizeConfiguredPathInput(snapshot.multiSarDialogOptions.startingPath)) + "');\n";
	source += "MRSETUP('MULTI_SAR_SUBDIRECTORIES', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSarDialogOptions.searchSubdirectories)) + "');\n";
	source += "MRSETUP('MULTI_SAR_CASE_SENSITIVE', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSarDialogOptions.caseSensitive)) + "');\n";
	source += "MRSETUP('MULTI_SAR_REGULAR_EXPRESSIONS', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSarDialogOptions.regularExpressions)) + "');\n";
	source += "MRSETUP('MULTI_SAR_FILES_IN_MEMORY', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSarDialogOptions.searchFilesInMemory)) + "');\n";
	source += "MRSETUP('MULTI_SAR_KEEP_FILES_OPEN', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSarDialogOptions.keepFilesOpen)) + "');\n";
	source += "MRSETUP('VIRTUAL_DESKTOPS', '" + std::to_string(snapshot.virtualDesktops) + "');\n";
	source += "MRSETUP('CYCLIC_VIRTUAL_DESKTOPS', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.cyclicVirtualDesktops)) + "');\n";
	source += "MRSETUP('CURSOR_BEHAVIOUR', '" + escapeMrmacSingleQuotedLiteral(formatCursorBehaviourLiteral(snapshot.cursorBehaviour)) + "');\n";
	source += "MRSETUP('CURSOR_POSITION_MARKER', '" + escapeMrmacSingleQuotedLiteral(snapshot.cursorPositionMarker) + "');\n";
	source += "MRSETUP('AUTOLOAD_WORKSPACE', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.autoloadWorkspace)) + "');\n";
	source += "MRSETUP('LOG_HANDLING', '" + escapeMrmacSingleQuotedLiteral(formatLogHandlingLiteral(snapshot.logHandling)) + "');\n";
	source += "MRSETUP('LOGFILE', '" + escapeMrmacSingleQuotedLiteral(snapshot.logFilePath) + "');\n";
	for (const std::string &autoexecMacro : snapshot.autoexecMacros)
			source += "MRSETUP('AUTOEXEC_MACRO', '" + escapeMrmacSingleQuotedLiteral(autoexecMacro) + "');\n";
	source += "MRSETUP('MAX_PATH_HISTORY', '" + std::to_string(snapshot.maxPathHistory) + "');\n";
	source += "MRSETUP('MAX_FILE_HISTORY', '" + std::to_string(snapshot.maxFileHistory) + "');\n";
	for (const MRDialogHistoryScopeSpec &scopeSpec : kDialogHistoryScopeSpecs) {
		const MRSettingsSnapshot::DialogHistoryState &state = snapshot.dialogHistory[dialogHistoryScopeIndex(scopeSpec.scope)];

		if (!state.lastPath.empty()) source += serializeScopedHistoryRecord(kDialogLastPathKey, scopeSpec.scope, "path", state.lastPath);
		for (const std::string &entry : state.pathHistory)
			source += serializeScopedHistoryRecord(kDialogPathHistoryKey, scopeSpec.scope, "value", entry);
		for (const std::string &entry : state.fileHistory)
			source += serializeScopedHistoryRecord(kDialogFileHistoryKey, scopeSpec.scope, "value", entry);
	}
	for (const std::string &entry : snapshot.multiFilespecHistory)
		source += "MRSETUP('MULTI_FILESPEC_HISTORY', '" + escapeMrmacSingleQuotedLiteral(entry) + "');\n";
	for (const std::string &entry : snapshot.multiPathHistory)
		source += "MRSETUP('MULTI_PATH_HISTORY', '" + escapeMrmacSingleQuotedLiteral(entry) + "');\n";
	source += "MRSETUP('DEFAULT_PROFILE_DESCRIPTION', '" + escapeMrmacSingleQuotedLiteral(snapshot.defaultProfileDescription) + "');\n";
	source += "MRSETUP('PAGE_BREAK', '" + escapeMrmacSingleQuotedLiteral(edit.pageBreak) + "');\n";
	source += "MRSETUP('WORD_DELIMITERS', '" + escapeMrmacSingleQuotedLiteral(edit.wordDelimiters) + "');\n";
	source += "MRSETUP('DEFAULT_EXTENSIONS', '" + escapeMrmacSingleQuotedLiteral(edit.defaultExtensions) + "');\n";
	source += "MRSETUP('TRUNCATE_SPACES', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.truncateSpaces)) + "');\n";
	source += "MRSETUP('EOF_CTRL_Z', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.eofCtrlZ)) + "');\n";
	source += "MRSETUP('EOF_CR_LF', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.eofCrLf)) + "');\n";
	source += "MRSETUP('TAB_EXPAND', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.tabExpand)) + "');\n";
	source += "MRSETUP('DISPLAY_TABS', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.displayTabs)) + "');\n";
	source += "MRSETUP('TAB_SIZE', '" + std::to_string(edit.tabSize) + "');\n";
	source += "MRSETUP('LEFT_MARGIN', '" + std::to_string(edit.leftMargin) + "');\n";
	source += "MRSETUP('RIGHT_MARGIN', '" + std::to_string(edit.rightMargin) + "');\n";
	source += "MRSETUP('FORMAT_RULER', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.formatRuler)) + "');\n";
	source += "MRSETUP('WORD_WRAP', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.wordWrap)) + "');\n";
	source += "MRSETUP('INDENT_STYLE', '" + escapeMrmacSingleQuotedLiteral(edit.indentStyle) + "');\n";
	source += "MRSETUP('CODE_LANGUAGE', '" + escapeMrmacSingleQuotedLiteral(edit.codeLanguage) + "');\n";
	source += "MRSETUP('CODE_COLORING', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.codeColoring)) + "');\n";
	source += "MRSETUP('CODE_FOLDING', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.codeFoldingFeature)) + "');\n";
	source += "MRSETUP('SMART_INDENTING', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.smartIndenting)) + "');\n";
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
	source += "MRSETUP('BACKUP_FILES', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.backupFiles)) + "');\n";
	source += "MRSETUP('SHOW_EOF_MARKER', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.showEofMarker)) + "');\n";
	source += "MRSETUP('SHOW_EOF_MARKER_EMOJI', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.showEofMarkerEmoji)) + "');\n";
	source += "MRSETUP('LINE_NUMBERS_POSITION', '" + escapeMrmacSingleQuotedLiteral(edit.lineNumbersPosition) + "');\n";
	source += "MRSETUP('LINE_NUM_ZERO_FILL', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.lineNumZeroFill)) + "');\n";
	source += "MRSETUP('MINIMAP_POSITION', '" + escapeMrmacSingleQuotedLiteral(edit.miniMapPosition) + "');\n";
	source += "MRSETUP('MINIMAP_WIDTH', '" + std::to_string(edit.miniMapWidth) + "');\n";
	source += "MRSETUP('MINIMAP_MARKER_GLYPH', '" + escapeMrmacSingleQuotedLiteral(edit.miniMapMarkerGlyph) + "');\n";
	source += "MRSETUP('GUTTERS', '" + escapeMrmacSingleQuotedLiteral(edit.gutters) + "');\n";
	source += "MRSETUP('PERSISTENT_BLOCKS', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.persistentBlocks)) + "');\n";
	source += "MRSETUP('CODE_FOLDING_POSITION', '" + escapeMrmacSingleQuotedLiteral(edit.codeFoldingPosition) + "');\n";
	source += "MRSETUP('COLUMN_BLOCK_MOVE', '" + escapeMrmacSingleQuotedLiteral(edit.columnBlockMove) + "');\n";
	source += "MRSETUP('DEFAULT_MODE', '" + escapeMrmacSingleQuotedLiteral(edit.defaultMode) + "');\n";
	source += "MRSETUP('CURSOR_STATUS_COLOR', '" + escapeMrmacSingleQuotedLiteral(edit.cursorStatusColor) + "');\n";
	source += "MRSETUP('" + std::string(kThemeSettingsKey) + "', '" + escapeMrmacSingleQuotedLiteral(themePath) + "');\n";
	source += "MRSETUP('" + std::string(kKeymapSettingsKey) + "', '" + escapeMrmacSingleQuotedLiteral(snapshot.keymapFilePath) + "');\n";
	source += serializeKeymapProfilesToSettingsSource(snapshot.keymapProfiles, snapshot.activeKeymapProfile);

	for (const auto &profile : snapshot.editProfiles) {
		source += "MRFEPROFILE('DEFINE', '" + escapeMrmacSingleQuotedLiteral(profile.id) + "', '" + escapeMrmacSingleQuotedLiteral(profile.name) + "', '');\n";
		for (const std::string &ext : profile.extensions)
			source += "MRFEPROFILE('EXT', '" + escapeMrmacSingleQuotedLiteral(profile.id) + "', '" + escapeMrmacSingleQuotedLiteral(ext) + "', '');\n";
		if (!profile.windowColorThemeUri.empty()) source += "MRFEPROFILE('SET', '" + escapeMrmacSingleQuotedLiteral(profile.id) + "', '" + std::string(kWindowColorThemeProfileKey) + "', '" + escapeMrmacSingleQuotedLiteral(profile.windowColorThemeUri) + "');\n";
		for (std::size_t i = 0; i < descriptorCount; ++i)
			if (descriptors[i].profileSupported && (profile.overrides.mask & descriptors[i].overrideBit) != 0) {
				std::string value;

				if (std::string(descriptors[i].key) == "PAGE_BREAK") value = profile.overrides.values.pageBreak;
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
				else if (std::string(descriptors[i].key) == "DISPLAY_TABS")
					value = formatEditSetupBoolean(profile.overrides.values.displayTabs);
				else if (std::string(descriptors[i].key) == "TAB_SIZE")
					value = std::to_string(profile.overrides.values.tabSize);
				else if (std::string(descriptors[i].key) == "LEFT_MARGIN")
					value = std::to_string(profile.overrides.values.leftMargin);
				else if (std::string(descriptors[i].key) == "RIGHT_MARGIN")
					value = std::to_string(profile.overrides.values.rightMargin);
				else if (std::string(descriptors[i].key) == "FORMAT_RULER")
					value = formatEditSetupBoolean(profile.overrides.values.formatRuler);
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
				else if (std::string(descriptors[i].key) == "LINE_NUMBERS_POSITION")
					value = profile.overrides.values.lineNumbersPosition;
				else if (std::string(descriptors[i].key) == "LINE_NUM_ZERO_FILL")
					value = formatEditSetupBoolean(profile.overrides.values.lineNumZeroFill);
				else if (std::string(descriptors[i].key) == "MINIMAP_POSITION")
					value = profile.overrides.values.miniMapPosition;
				else if (std::string(descriptors[i].key) == "MINIMAP_WIDTH")
					value = std::to_string(profile.overrides.values.miniMapWidth);
				else if (std::string(descriptors[i].key) == "MINIMAP_MARKER_GLYPH")
					value = profile.overrides.values.miniMapMarkerGlyph;
				else if (std::string(descriptors[i].key) == "GUTTERS")
					value = profile.overrides.values.gutters;
				else if (std::string(descriptors[i].key) == "PERSISTENT_BLOCKS")
					value = formatEditSetupBoolean(profile.overrides.values.persistentBlocks);
				else if (std::string(descriptors[i].key) == "CODE_FOLDING_POSITION")
					value = profile.overrides.values.codeFoldingPosition;
				else if (std::string(descriptors[i].key) == "COLUMN_BLOCK_MOVE")
					value = profile.overrides.values.columnBlockMove;
				else if (std::string(descriptors[i].key) == "DEFAULT_MODE")
					value = profile.overrides.values.defaultMode;
				else if (std::string(descriptors[i].key) == "CURSOR_STATUS_COLOR")
					value = profile.overrides.values.cursorStatusColor;

				source += "MRFEPROFILE('SET', '" + escapeMrmacSingleQuotedLiteral(profile.id) + "', '" + descriptors[i].key + "', '" + escapeMrmacSingleQuotedLiteral(value) + "');\n";
			}
	}
	source += "END_MACRO;\n";
	return source;
}

std::string buildSettingsMacroSource(const MRSetupPaths &paths) {
	return buildSettingsMacroSource(captureConfiguredSettingsSnapshot(paths));
}

bool configuredSettingsDirty() {
	return configuredSettingsDirtyFlag();
}

void clearConfiguredSettingsDirty() {
	configuredSettingsDirtyFlag() = false;
}

bool persistConfiguredSettingsSnapshot(std::string *errorMessage, MRSettingsWriteReport *report) {
	MRSetupPaths paths;
	std::string settingsPath = configuredSettingsMacroFilePath();
	std::string settingsDir = directoryPartOf(settingsPath);
	std::string source;
	std::string previousSource;

	if (report != nullptr) *report = MRSettingsWriteReport();
	if (report != nullptr) report->settingsPath = settingsPath;
	if (!configuredSettingsDirty()) {
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}

	paths.settingsMacroUri = settingsPath;
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();

	if (!validateSettingsMacroFilePath(settingsPath, errorMessage)) return false;
	if (!ensureDirectoryTree(settingsDir, errorMessage)) return false;
	static_cast<void>(readTextFile(settingsPath, previousSource));
	source = configuredAutoloadWorkspace() ? buildSettingsMacroSourceWithWorkspace(paths) : buildSettingsMacroSource(paths);
	if (!writeTextFile(settingsPath, source)) return setError(errorMessage, "Unable to write settings macro file: " + settingsPath);
	populateSettingsWriteReport(settingsPath, previousSource, source, report);
	clearConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool writeSettingsMacroFile(const MRSetupPaths &paths, std::string *errorMessage, MRSettingsWriteReport *report) {
	std::string settingsPath = normalizeConfiguredPathInput(paths.settingsMacroUri);
	std::string settingsDir = directoryPartOf(settingsPath);
	std::string themePath = configuredColorThemeFile().empty() ? defaultColorThemePathForSettings(settingsPath) : configuredColorThemeFilePath();
	std::string source;
	std::string previousSource;

	if (!validateSettingsMacroFilePath(settingsPath, errorMessage)) return false;
	if (!ensureDirectoryTree(settingsDir, errorMessage)) return false;
	if (!writeColorThemeFile(themePath, errorMessage)) return false;
	if (!setConfiguredColorThemeFilePath(themePath, errorMessage)) return false;
	static_cast<void>(readTextFile(settingsPath, previousSource));
	source = configuredAutoloadWorkspace() ? buildSettingsMacroSourceWithWorkspace(paths) : buildSettingsMacroSource(paths);
	if (!writeTextFile(settingsPath, source)) return setError(errorMessage, "Unable to write settings macro file: " + settingsPath);
	populateSettingsWriteReport(settingsPath, previousSource, source, report);
	clearConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool ensureSettingsMacroFileExists(const std::string &settingsMacroUri, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(settingsMacroUri);
	struct stat st;

	if (!validateSettingsMacroFilePath(normalized, errorMessage)) return false;
	if (::stat(normalized.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode)) return setError(errorMessage, "Settings macro URI must include a filename.");
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}

	{
		MRSettingsSnapshot snapshot;
		std::string settingsDir = directoryPartOf(normalized);
		std::string themePath;
		std::string themeDir;

		if (!resetSettingsSnapshot(normalized, snapshot, errorMessage)) return false;
		if (!ensureDirectoryTree(settingsDir, errorMessage)) return false;
		themePath = normalizeConfiguredPathInput(snapshot.colorThemeFilePath);
		themeDir = directoryPartOf(themePath);
		if (!ensureDirectoryTree(themeDir, errorMessage)) return false;
		if (!writeTextFile(themePath, buildColorThemeMacroSource(snapshot.colorSettings))) return setError(errorMessage, "Unable to write color theme file: " + themePath);
		if (!writeTextFile(normalized, buildSettingsMacroSource(snapshot))) return setError(errorMessage, "Unable to write settings macro file: " + normalized);
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
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
	if (isWritableRegularFile(normalized) == false && ::stat(normalized.c_str(), &st) == 0) return setError(errorMessage, "Log file is not writable: " + normalized);
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
	if (!configured.empty()) return makeAbsolutePath(configured);
	return defaultLogFilePathForSettings(configuredSettingsMacroFilePath());
}

std::string defaultSettingsMacroFilePath() {
	return configuredSettingsMacroFilePath();
}

std::string defaultMacroDirectoryPath() {
	std::string configured = configuredMacroDirectoryPath();

	if (!configured.empty()) return configured;
	return resolveSetupPathDefaults().macroPath;
}
