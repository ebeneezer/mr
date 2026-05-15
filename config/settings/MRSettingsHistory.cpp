#include "../../app/utils/MRStringUtils.hpp"
#include "MRSettingsHistory.hpp"
#include "MRSettingsRuntimeState.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>

namespace {

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

} // namespace

const std::array<MRDialogHistoryScopeSpec, static_cast<std::size_t>(MRDialogHistoryScope::Count)> kDialogHistoryScopeSpecs{
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
