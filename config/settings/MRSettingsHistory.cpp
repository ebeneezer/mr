#include "../../app/utils/MRStringUtils.hpp"
#include "MRSettingsHistory.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "../../mrmac/mrmac.h"
#include "../../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../../mrmac/vm/MRVMValue.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <mutex>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace {

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

VirtualMachine::Value settingsHistoryRoot(MRVMRuntimeKv &runtimeKv) {
	VirtualMachine::Value settings = runtimeKv.ensureRoot("SETTINGS");
	return runtimeKv.ensureChild(settings, "history");
}

VirtualMachine::Value settingsRuntimeRoot(MRVMRuntimeKv &runtimeKv) {
	VirtualMachine::Value settings = runtimeKv.ensureRoot("SETTINGS");
	return runtimeKv.ensureChild(settings, "runtime");
}

int readInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, int fallback) {
	MRVMHashStore &store = runtimeKv.globalStore();
	if (!mrvmHashContainsValue(store, store, parent, key)) return fallback;
	VirtualMachine::Value value = mrvmHashReadValue(store, store, parent, key);
	return value.type == TYPE_INT ? value.i : fallback;
}

long long readLongLong(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, long long fallback) {
	MRVMHashStore &store = runtimeKv.globalStore();
	if (!mrvmHashContainsValue(store, store, parent, key)) return fallback;
	VirtualMachine::Value value = mrvmHashReadValue(store, store, parent, key);
	if (value.type != TYPE_STR) return fallback;
	try {
		std::size_t consumed = 0;
		const long long parsed = std::stoll(value.s, &consumed, 10);
		return consumed == value.s.size() ? parsed : fallback;
	} catch (...) {
		return fallback;
	}
}

std::string readString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key) {
	MRVMHashStore &store = runtimeKv.globalStore();
	if (!mrvmHashContainsValue(store, store, parent, key)) return std::string();
	VirtualMachine::Value value = mrvmHashReadValue(store, store, parent, key);
	return value.type == TYPE_STR ? value.s : std::string();
}

void writeInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeInt(value));
}

void writeLongLong(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, long long value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(std::to_string(value)));
}

void writeString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(value));
}

std::vector<MRDialogHistoryEntry> readHistoryEntries(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key) {
	std::vector<MRDialogHistoryEntry> entries;
	VirtualMachine::Value list;

	if (!runtimeKv.findChild(parent, key, list)) return entries;
	const int count = readInt(runtimeKv, list, "count", 0);
	for (int index = 0; index < count; ++index) {
		VirtualMachine::Value stored;
		if (!runtimeKv.findChild(list, std::to_string(index), stored)) continue;
		entries.push_back(MRDialogHistoryEntry{readString(runtimeKv, stored, "value"), readLongLong(runtimeKv, stored, "epoch", 0)});
	}
	return entries;
}

void writeHistoryEntries(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, const std::vector<MRDialogHistoryEntry> &entries) {
	VirtualMachine::Value list = runtimeKv.replaceChild(parent, key);
	writeInt(runtimeKv, list, "count", static_cast<int>(entries.size()));
	for (std::size_t index = 0; index < entries.size(); ++index) {
		VirtualMachine::Value stored = runtimeKv.ensureChild(list, std::to_string(index));
		writeString(runtimeKv, stored, "value", entries[index].value);
		writeLongLong(runtimeKv, stored, "epoch", entries[index].epoch);
	}
}

MRScopedDialogHistoryState readScopedHistory(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &scopes, std::size_t index) {
	VirtualMachine::Value stored;
	MRScopedDialogHistoryState state;

	if (!runtimeKv.findChild(scopes, std::to_string(index), stored)) return state;
	state.lastPath = readString(runtimeKv, stored, "lastPath");
	state.pathHistory = readHistoryEntries(runtimeKv, stored, "pathHistory");
	state.fileHistory = readHistoryEntries(runtimeKv, stored, "fileHistory");
	return state;
}

void writeScopedHistory(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &scopes, std::size_t index, const MRScopedDialogHistoryState &state) {
	VirtualMachine::Value stored = runtimeKv.ensureChild(scopes, std::to_string(index));
	writeString(runtimeKv, stored, "lastPath", state.lastPath);
	writeHistoryEntries(runtimeKv, stored, "pathHistory", state.pathHistory);
	writeHistoryEntries(runtimeKv, stored, "fileHistory", state.fileHistory);
}

} // namespace

const std::array<MRDialogHistoryScopeSpec, static_cast<std::size_t>(MRDialogHistoryScope::Count)> kDialogHistoryScopeSpecs{
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::General, "GENERAL"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::EditorSaveAs, "EDITOR_SAVE_AS"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::LiveLogOpen, "LIVE_LOG_OPEN"},
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
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::PdfExport, "PDF_EXPORT"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::ExtensionThemeFile, "EXTENSION_THEME_FILE"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::ExtensionPostLoadMacro, "EXTENSION_POST_LOAD_MACRO"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::ExtensionPreSaveMacro, "EXTENSION_PRE_SAVE_MACRO"},
    MRDialogHistoryScopeSpec{MRDialogHistoryScope::ExtensionDefaultPath, "EXTENSION_DEFAULT_PATH"},
};

bool setConfiguredFileDialogShowHiddenFiles(bool enabled, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value runtime = settingsRuntimeRoot(runtimeKv);
	const bool previous = readInt(runtimeKv, runtime, "fileDialogShowHiddenFiles", 0) != 0;

	writeInt(runtimeKv, runtime, "fileDialogShowHiddenFiles", enabled ? 1 : 0);
	if (previous != enabled) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredFileDialogShowHiddenFiles() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();

	recordSettingsRuntimeRead();
	return readInt(runtimeKv, settingsRuntimeRoot(runtimeKv), "fileDialogShowHiddenFiles", 0) != 0;
}

std::array<MRScopedDialogHistoryState, static_cast<std::size_t>(MRDialogHistoryScope::Count)> configuredDialogHistoryStorage() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value scopes = runtimeKv.ensureChild(settingsHistoryRoot(runtimeKv), "scopes");
	std::array<MRScopedDialogHistoryState, static_cast<std::size_t>(MRDialogHistoryScope::Count)> states;

	for (std::size_t index = 0; index < states.size(); ++index)
		states[index] = readScopedHistory(runtimeKv, scopes, index);
	return states;
}

void storeConfiguredDialogHistoryStorage(const std::array<MRScopedDialogHistoryState, static_cast<std::size_t>(MRDialogHistoryScope::Count)> &states) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value history = settingsHistoryRoot(runtimeKv);
	VirtualMachine::Value scopes = runtimeKv.replaceChild(history, "scopes");

	for (std::size_t index = 0; index < states.size(); ++index)
		writeScopedHistory(runtimeKv, scopes, index, states[index]);
}

std::size_t dialogHistoryScopeIndex(MRDialogHistoryScope scope) noexcept {
	return static_cast<std::size_t>(scope);
}

MRScopedDialogHistoryState dialogHistoryState(MRDialogHistoryScope scope) {
	const auto states = configuredDialogHistoryStorage();
	const std::size_t index = dialogHistoryScopeIndex(scope);
	return index < states.size() ? states[index] : MRScopedDialogHistoryState();
}

void storeDialogHistoryState(MRDialogHistoryScope scope, const MRScopedDialogHistoryState &state) {
	auto states = configuredDialogHistoryStorage();
	const std::size_t index = dialogHistoryScopeIndex(scope);
	if (index >= states.size()) return;
	states[index] = state;
	storeConfiguredDialogHistoryStorage(states);
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

std::vector<MRDialogHistoryEntry> configuredMultiFilespecHistoryStorage() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readHistoryEntries(runtimeKv, settingsHistoryRoot(runtimeKv), "multiFilespec");
}

void storeConfiguredMultiFilespecHistoryStorage(const std::vector<MRDialogHistoryEntry> &entries) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeHistoryEntries(runtimeKv, settingsHistoryRoot(runtimeKv), "multiFilespec", entries);
}

std::vector<MRDialogHistoryEntry> configuredMultiPathHistoryStorage() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readHistoryEntries(runtimeKv, settingsHistoryRoot(runtimeKv), "multiPath");
}

void storeConfiguredMultiPathHistoryStorage(const std::vector<MRDialogHistoryEntry> &entries) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeHistoryEntries(runtimeKv, settingsHistoryRoot(runtimeKv), "multiPath", entries);
}

int configuredPathHistoryLimit() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readInt(runtimeKv, settingsHistoryRoot(runtimeKv), "pathLimit", kHistoryLimitDefault);
}

void storeConfiguredPathHistoryLimit(int value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeInt(runtimeKv, settingsHistoryRoot(runtimeKv), "pathLimit", value);
}

int configuredFileHistoryLimit() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readInt(runtimeKv, settingsHistoryRoot(runtimeKv), "fileLimit", kHistoryLimitDefault);
}

void storeConfiguredFileHistoryLimit(int value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeInt(runtimeKv, settingsHistoryRoot(runtimeKv), "fileLimit", value);
}

int configuredWorkspaceHistoryLimit() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readInt(runtimeKv, settingsHistoryRoot(runtimeKv), "workspaceLimit", kHistoryLimitDefault);
}

void storeConfiguredWorkspaceHistoryLimit(int value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeInt(runtimeKv, settingsHistoryRoot(runtimeKv), "workspaceLimit", value);
}

int configuredFileHistoryLimitForScope(MRDialogHistoryScope scope) {
	switch (scope) {
		case MRDialogHistoryScope::WorkspaceLoad:
		case MRDialogHistoryScope::WorkspaceSave:
			return configuredWorkspaceHistoryLimit();
		default:
			return configuredFileHistoryLimit();
	}
}

long long configuredHistoryEpochCounter() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readLongLong(runtimeKv, settingsHistoryRoot(runtimeKv), "epoch", 0);
}

void storeConfiguredHistoryEpochCounter(long long value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeLongLong(runtimeKv, settingsHistoryRoot(runtimeKv), "epoch", value);
}

long long nextHistoryEpoch() {
	long long nowEpoch = static_cast<long long>(std::time(nullptr));
	long long counter = configuredHistoryEpochCounter();
	counter = std::max(counter + 1, nowEpoch);
	storeConfiguredHistoryEpochCounter(counter);
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

namespace {

std::string defaultRememberedLoadDirectory(MRDialogHistoryScope scope) {
	switch (scope) {
		case MRDialogHistoryScope::MacroFile:
		case MRDialogHistoryScope::SetupMacroDirectory:
		case MRDialogHistoryScope::ExtensionPostLoadMacro:
		case MRDialogHistoryScope::ExtensionPreSaveMacro: {
			const std::string macroDirectory = makeAbsolutePath(configuredMacroDirectory());
			if (isReadableDirectory(macroDirectory)) return macroDirectory;
			break;
		}
		default:
			break;
	}
	return fallbackRememberedLoadDirectory();
}

} // namespace

std::string effectiveRememberedLoadDirectory(MRDialogHistoryScope scope) {
	const MRScopedDialogHistoryState &state = dialogHistoryState(scope);
	std::string remembered = normalizeConfiguredPathInput(state.lastPath);
	if (!remembered.empty() && isReadableDirectory(remembered)) return remembered;
	remembered = latestReadableHistoryPath(state.pathHistory);
	if (!remembered.empty()) return remembered;
	remembered = latestReadableHistoryFileDirectory(state.fileHistory);
	if (!remembered.empty()) return remembered;
	return defaultRememberedLoadDirectory(scope);
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
	auto states = configuredDialogHistoryStorage();
	const auto previousStates = states;

	if (value < kHistoryLimitMin || value > kHistoryLimitMax) return setError(errorMessage, "MAX_PATH_HISTORY must be within 5..50.");
	storeConfiguredPathHistoryLimit(value);
	for (MRScopedDialogHistoryState &state : states)
		trimHistoryToLimit(state.pathHistory, value);
	storeConfiguredDialogHistoryStorage(states);
	if (previousLimit != value || previousStates != states) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setConfiguredFileHistoryLimitValue(int value, std::string *errorMessage) {
	const int previousLimit = configuredFileHistoryLimit();
	auto states = configuredDialogHistoryStorage();
	const auto previousStates = states;

	if (value < kHistoryLimitMin || value > kHistoryLimitMax) return setError(errorMessage, "MAX_FILE_HISTORY must be within 5..50.");
	storeConfiguredFileHistoryLimit(value);
	for (std::size_t i = 0; i < states.size(); ++i) {
		const MRDialogHistoryScope scope = static_cast<MRDialogHistoryScope>(i);
		if (scope != MRDialogHistoryScope::WorkspaceLoad && scope != MRDialogHistoryScope::WorkspaceSave) trimHistoryToLimit(states[i].fileHistory, value);
	}
	storeConfiguredDialogHistoryStorage(states);
	if (previousLimit != value || previousStates != states) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setConfiguredWorkspaceHistoryLimitValue(int value, std::string *errorMessage) {
	const int previousLimit = configuredWorkspaceHistoryLimit();
	auto states = configuredDialogHistoryStorage();
	const auto previousStates = states;

	if (value < kHistoryLimitMin || value > kHistoryLimitMax) return setError(errorMessage, "MAX_WORKSPACE_HISTORY must be within 5..50.");
	storeConfiguredWorkspaceHistoryLimit(value);
	trimHistoryToLimit(states[dialogHistoryScopeIndex(MRDialogHistoryScope::WorkspaceLoad)].fileHistory, value);
	trimHistoryToLimit(states[dialogHistoryScopeIndex(MRDialogHistoryScope::WorkspaceSave)].fileHistory, value);
	storeConfiguredDialogHistoryStorage(states);
	if (previousLimit != value || previousStates != states) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

int configuredMaxPathHistory() {
	return configuredPathHistoryLimit();
}

int configuredMaxFileHistory() {
	return configuredFileHistoryLimit();
}

int configuredMaxWorkspaceHistory() {
	return configuredWorkspaceHistoryLimit();
}

void configuredPathHistoryEntries(std::vector<std::string> &outValues) {
	const MRScopedDialogHistoryState state = dialogHistoryState(MRDialogHistoryScope::General);
	outValues.clear();
	for (const MRDialogHistoryEntry &entry : state.pathHistory)
		outValues.push_back(entry.value);
}

void configuredFileHistoryEntries(std::vector<std::string> &outValues) {
	const MRScopedDialogHistoryState state = dialogHistoryState(MRDialogHistoryScope::General);
	outValues.clear();
	for (const MRDialogHistoryEntry &entry : state.fileHistory)
		outValues.push_back(entry.value);
}

void configuredMultiFilespecHistoryEntries(std::vector<std::string> &outValues) {
	const std::vector<MRDialogHistoryEntry> entries = configuredMultiFilespecHistoryStorage();
	outValues.clear();
	for (const MRDialogHistoryEntry &entry : entries)
		outValues.push_back(entry.value);
}

void configuredMultiPathHistoryEntries(std::vector<std::string> &outValues) {
	const std::vector<MRDialogHistoryEntry> entries = configuredMultiPathHistoryStorage();
	outValues.clear();
	for (const MRDialogHistoryEntry &entry : entries)
		outValues.push_back(entry.value);
}

bool addConfiguredMultiFilespecHistoryEntry(const std::string &value, std::string *errorMessage) {
	std::vector<MRDialogHistoryEntry> entries = configuredMultiFilespecHistoryStorage();
	const auto previous = entries;

	addHistoryEntry(entries, trimAscii(value), configuredFileHistoryLimit());
	storeConfiguredMultiFilespecHistoryStorage(entries);
	if (previous != entries) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool addConfiguredMultiPathHistoryEntry(const std::string &value, std::string *errorMessage) {
	std::vector<MRDialogHistoryEntry> entries = configuredMultiPathHistoryStorage();
	const auto previous = entries;

	addHistoryEntry(entries, normalizeConfiguredPathInput(value), configuredPathHistoryLimit());
	storeConfiguredMultiPathHistoryStorage(entries);
	if (previous != entries) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setScopedDialogLastPath(MRDialogHistoryScope scope, const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	std::string directory;
	MRScopedDialogHistoryState state = dialogHistoryState(scope);
	const MRScopedDialogHistoryState previous = state;

	if (!normalized.empty() && isReadableDirectory(normalized)) {
		state.lastPath = normalized;
		addHistoryEntry(state.pathHistory, normalized, configuredPathHistoryLimit());
	} else if (!normalized.empty()) {
		addHistoryEntry(state.fileHistory, normalized, configuredFileHistoryLimitForScope(scope));
		directory = normalizedDialogDirectoryFromPath(normalized);
		if (!directory.empty()) {
			state.lastPath = directory;
			addHistoryEntry(state.pathHistory, directory, configuredPathHistoryLimit());
		}
	} else if (state.lastPath.empty()) {
		directory = defaultRememberedLoadDirectory(scope);
		if (!directory.empty()) {
			state.lastPath = directory;
			addHistoryEntry(state.pathHistory, directory, configuredPathHistoryLimit());
		}
	}
	storeDialogHistoryState(scope, state);
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
	MRScopedDialogHistoryState state = dialogHistoryState(scope);
	const MRScopedDialogHistoryState previous = state;

	if (normalized.empty()) return;
	state.fileHistory.erase(std::remove_if(state.fileHistory.begin(), state.fileHistory.end(), [&](const MRDialogHistoryEntry &entry) { return normalizeConfiguredPathInput(entry.value) == normalized; }), state.fileHistory.end());
	storeDialogHistoryState(scope, state);
	if (previous != state) markConfiguredSettingsDirty();
}

std::string configuredLastFileDialogFilePath(MRDialogHistoryScope scope) {
	const MRScopedDialogHistoryState state = dialogHistoryState(scope);
	return latestHistoryValue(state.fileHistory);
}

std::string configuredLastFileDialogPath(MRDialogHistoryScope scope) {
	return effectiveRememberedLoadDirectory(scope);
}

void configuredScopedDialogFileHistoryEntries(MRDialogHistoryScope scope, std::vector<std::string> &outValues) {
	const MRScopedDialogHistoryState state = dialogHistoryState(scope);
	outValues.clear();
	for (const MRDialogHistoryEntry &entry : state.fileHistory)
		outValues.push_back(entry.value);
}

void configuredScopedDialogPathHistoryEntries(MRDialogHistoryScope scope, std::vector<std::string> &outValues) {
	const MRScopedDialogHistoryState state = dialogHistoryState(scope);
	outValues.clear();
	for (const MRDialogHistoryEntry &entry : state.pathHistory)
		outValues.push_back(entry.value);
}

bool setConfiguredLastFileDialogPath(const std::string &path, std::string *errorMessage) {
	return setScopedDialogLastPath(MRDialogHistoryScope::General, path, errorMessage);
}

std::string configuredLastFileDialogPath() {
	return effectiveRememberedLoadDirectory(MRDialogHistoryScope::General);
}
