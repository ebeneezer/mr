#include "../../app/utils/MRStringUtils.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "MRSettingsHistory.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "../../mrmac/mrmac.h"
#include "../../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../../mrmac/vm/MRVMValue.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace {

struct SettingsIoBucket {
	std::uint64_t second;
	std::uint64_t reads;
	std::uint64_t writes;

	SettingsIoBucket() noexcept : second(0), reads(0), writes(0) {
	}
};

struct SettingsIoMeter {
	std::mutex mutex;
	std::array<SettingsIoBucket, 60> buckets;
};

SettingsIoMeter &settingsIoMeter() {
	static SettingsIoMeter meter;
	return meter;
}

std::uint64_t settingsIoSecondNow() {
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void recordSettingsRuntimeIo(bool write) {
	SettingsIoMeter &meter = settingsIoMeter();
	const std::uint64_t now = settingsIoSecondNow();
	const std::size_t index = static_cast<std::size_t>(now % meter.buckets.size());
	std::lock_guard<std::mutex> lock(meter.mutex);
	SettingsIoBucket &bucket = meter.buckets[index];

	if (bucket.second != now) {
		bucket.second = now;
		bucket.reads = 0;
		bucket.writes = 0;
	}
	if (write)
		++bucket.writes;
	else
		++bucket.reads;
}

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

VirtualMachine::Value settingsRuntimeRoot(MRVMRuntimeKv &runtimeKv) {
	VirtualMachine::Value settings = runtimeKv.ensureRoot("SETTINGS");
	return runtimeKv.ensureChild(settings, "runtime");
}

VirtualMachine::Value settingsBranch(MRVMRuntimeKv &runtimeKv, const char *branch) {
	return runtimeKv.ensureChild(settingsRuntimeRoot(runtimeKv), branch);
}

int readSettingsInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, int fallback) {
	MRVMHashStore &store = runtimeKv.globalStore();
	if (!mrvmHashContainsValue(store, store, parent, key)) return fallback;
	VirtualMachine::Value value = mrvmHashReadValue(store, store, parent, key);
	return value.type == TYPE_INT ? value.i : fallback;
}

std::string readSettingsString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, const std::string &fallback = std::string()) {
	MRVMHashStore &store = runtimeKv.globalStore();
	if (!mrvmHashContainsValue(store, store, parent, key)) return fallback;
	VirtualMachine::Value value = mrvmHashReadValue(store, store, parent, key);
	return value.type == TYPE_STR ? value.s : fallback;
}

std::vector<std::string> readSettingsStringArray(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key) {
	MRVMHashStore &store = runtimeKv.globalStore();
	std::vector<std::string> values;

	if (!mrvmHashContainsValue(store, store, parent, key)) return values;
	VirtualMachine::Value value = mrvmHashReadValue(store, store, parent, key);
	if (value.type != TYPE_STR_ARRAY) return values;
	for (const VirtualMachine::Value &element : value.arrayValues)
		if (element.type == TYPE_STR) values.push_back(element.s);
	return values;
}

void writeSettingsInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeInt(value));
}

void writeSettingsString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(value));
}

void writeSettingsStringArray(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, const std::vector<std::string> &values) {
	MRVMHashStore &store = runtimeKv.globalStore();
	VirtualMachine::Value array = mrvmMakeArrayValue(TYPE_STR);
	array.globalStorage = true;
	for (const std::string &value : values)
		array.arrayValues.push_back(mrvmMakeString(value));
	mrvmHashWriteValue(store, store, parent, key, array);
}

void readSearchDialogOptions(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, MRSearchDialogOptions &options) {
	options.textType = static_cast<MRSearchTextType>(readSettingsInt(runtimeKv, parent, "textType", static_cast<int>(options.textType)));
	options.direction = static_cast<MRSearchDirection>(readSettingsInt(runtimeKv, parent, "direction", static_cast<int>(options.direction)));
	options.mode = static_cast<MRSearchMode>(readSettingsInt(runtimeKv, parent, "mode", static_cast<int>(options.mode)));
	options.caseSensitive = readSettingsInt(runtimeKv, parent, "caseSensitive", options.caseSensitive ? 1 : 0) != 0;
	options.globalSearch = readSettingsInt(runtimeKv, parent, "globalSearch", options.globalSearch ? 1 : 0) != 0;
	options.restrictToMarkedBlock = readSettingsInt(runtimeKv, parent, "restrictToMarkedBlock", options.restrictToMarkedBlock ? 1 : 0) != 0;
	options.searchAllWindows = readSettingsInt(runtimeKv, parent, "searchAllWindows", options.searchAllWindows ? 1 : 0) != 0;
}

void writeSearchDialogOptions(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const MRSearchDialogOptions &options) {
	writeSettingsInt(runtimeKv, parent, "textType", static_cast<int>(options.textType));
	writeSettingsInt(runtimeKv, parent, "direction", static_cast<int>(options.direction));
	writeSettingsInt(runtimeKv, parent, "mode", static_cast<int>(options.mode));
	writeSettingsInt(runtimeKv, parent, "caseSensitive", options.caseSensitive ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "globalSearch", options.globalSearch ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "restrictToMarkedBlock", options.restrictToMarkedBlock ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "searchAllWindows", options.searchAllWindows ? 1 : 0);
}

int configuredRuntimeInt(const char *key, int fallback) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readSettingsInt(runtimeKv, settingsBranch(runtimeKv, "general"), key, fallback);
}

void storeConfiguredRuntimeInt(const char *key, int value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeSettingsInt(runtimeKv, settingsBranch(runtimeKv, "general"), key, value);
}

std::string configuredRuntimeString(const char *key, const std::string &fallback = std::string()) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readSettingsString(runtimeKv, settingsBranch(runtimeKv, "general"), key, fallback);
}

void storeConfiguredRuntimeString(const char *key, const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeSettingsString(runtimeKv, settingsBranch(runtimeKv, "general"), key, value);
}

MRSarDialogOptions readSarDialogOptions() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value parent = settingsBranch(runtimeKv, "sarDialog");
	MRSarDialogOptions options;

	options.textType = static_cast<MRSearchTextType>(readSettingsInt(runtimeKv, parent, "textType", static_cast<int>(options.textType)));
	options.direction = static_cast<MRSearchDirection>(readSettingsInt(runtimeKv, parent, "direction", static_cast<int>(options.direction)));
	options.mode = static_cast<MRSarMode>(readSettingsInt(runtimeKv, parent, "mode", static_cast<int>(options.mode)));
	options.leaveCursorAt = static_cast<MRSarLeaveCursor>(readSettingsInt(runtimeKv, parent, "leaveCursorAt", static_cast<int>(options.leaveCursorAt)));
	options.caseSensitive = readSettingsInt(runtimeKv, parent, "caseSensitive", options.caseSensitive ? 1 : 0) != 0;
	options.globalSearch = readSettingsInt(runtimeKv, parent, "globalSearch", options.globalSearch ? 1 : 0) != 0;
	options.restrictToMarkedBlock = readSettingsInt(runtimeKv, parent, "restrictToMarkedBlock", options.restrictToMarkedBlock ? 1 : 0) != 0;
	options.searchAllWindows = readSettingsInt(runtimeKv, parent, "searchAllWindows", options.searchAllWindows ? 1 : 0) != 0;
	return options;
}

void storeSarDialogOptions(const MRSarDialogOptions &options) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value parent = settingsBranch(runtimeKv, "sarDialog");

	writeSettingsInt(runtimeKv, parent, "textType", static_cast<int>(options.textType));
	writeSettingsInt(runtimeKv, parent, "direction", static_cast<int>(options.direction));
	writeSettingsInt(runtimeKv, parent, "mode", static_cast<int>(options.mode));
	writeSettingsInt(runtimeKv, parent, "leaveCursorAt", static_cast<int>(options.leaveCursorAt));
	writeSettingsInt(runtimeKv, parent, "caseSensitive", options.caseSensitive ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "globalSearch", options.globalSearch ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "restrictToMarkedBlock", options.restrictToMarkedBlock ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "searchAllWindows", options.searchAllWindows ? 1 : 0);
}

MRMultiSearchDialogOptions readMultiSearchDialogOptions(const char *branch) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value parent = settingsBranch(runtimeKv, branch);
	MRMultiSearchDialogOptions options;

	options.searchSubdirectories = readSettingsInt(runtimeKv, parent, "searchSubdirectories", options.searchSubdirectories ? 1 : 0) != 0;
	options.caseSensitive = readSettingsInt(runtimeKv, parent, "caseSensitive", options.caseSensitive ? 1 : 0) != 0;
	options.wholeWords = readSettingsInt(runtimeKv, parent, "wholeWords", options.wholeWords ? 1 : 0) != 0;
	options.regularExpressions = readSettingsInt(runtimeKv, parent, "regularExpressions", options.regularExpressions ? 1 : 0) != 0;
	options.searchFilesInMemory = readSettingsInt(runtimeKv, parent, "searchFilesInMemory", options.searchFilesInMemory ? 1 : 0) != 0;
	options.restrictToWorkspace = readSettingsInt(runtimeKv, parent, "restrictToWorkspace", options.restrictToWorkspace ? 1 : 0) != 0;
	options.filespec = readSettingsString(runtimeKv, parent, "filespec", options.filespec);
	options.startingPath = readSettingsString(runtimeKv, parent, "startingPath");
	options.searchText = readSettingsString(runtimeKv, parent, "searchText");
	return options;
}

void storeMultiSearchDialogOptions(const char *branch, const MRMultiSearchDialogOptions &options) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value parent = settingsBranch(runtimeKv, branch);

	writeSettingsInt(runtimeKv, parent, "searchSubdirectories", options.searchSubdirectories ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "caseSensitive", options.caseSensitive ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "wholeWords", options.wholeWords ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "regularExpressions", options.regularExpressions ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "searchFilesInMemory", options.searchFilesInMemory ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "restrictToWorkspace", options.restrictToWorkspace ? 1 : 0);
	writeSettingsString(runtimeKv, parent, "filespec", options.filespec);
	writeSettingsString(runtimeKv, parent, "startingPath", options.startingPath);
	writeSettingsString(runtimeKv, parent, "searchText", options.searchText);
}

MRMultiSarDialogOptions readMultiSarDialogOptions() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value parent = settingsBranch(runtimeKv, "multiSarDialog");
	MRMultiSarDialogOptions options;

	options.searchSubdirectories = readSettingsInt(runtimeKv, parent, "searchSubdirectories", options.searchSubdirectories ? 1 : 0) != 0;
	options.caseSensitive = readSettingsInt(runtimeKv, parent, "caseSensitive", options.caseSensitive ? 1 : 0) != 0;
	options.wholeWords = readSettingsInt(runtimeKv, parent, "wholeWords", options.wholeWords ? 1 : 0) != 0;
	options.regularExpressions = readSettingsInt(runtimeKv, parent, "regularExpressions", options.regularExpressions ? 1 : 0) != 0;
	options.searchFilesInMemory = readSettingsInt(runtimeKv, parent, "searchFilesInMemory", options.searchFilesInMemory ? 1 : 0) != 0;
	options.keepFilesOpen = readSettingsInt(runtimeKv, parent, "keepFilesOpen", options.keepFilesOpen ? 1 : 0) != 0;
	options.restrictToWorkspace = readSettingsInt(runtimeKv, parent, "restrictToWorkspace", options.restrictToWorkspace ? 1 : 0) != 0;
	options.filespec = readSettingsString(runtimeKv, parent, "filespec", options.filespec);
	options.startingPath = readSettingsString(runtimeKv, parent, "startingPath");
	options.searchText = readSettingsString(runtimeKv, parent, "searchText");
	options.replacementText = readSettingsString(runtimeKv, parent, "replacementText");
	return options;
}

void storeMultiSarDialogOptions(const MRMultiSarDialogOptions &options) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value parent = settingsBranch(runtimeKv, "multiSarDialog");

	writeSettingsInt(runtimeKv, parent, "searchSubdirectories", options.searchSubdirectories ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "caseSensitive", options.caseSensitive ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "wholeWords", options.wholeWords ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "regularExpressions", options.regularExpressions ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "searchFilesInMemory", options.searchFilesInMemory ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "keepFilesOpen", options.keepFilesOpen ? 1 : 0);
	writeSettingsInt(runtimeKv, parent, "restrictToWorkspace", options.restrictToWorkspace ? 1 : 0);
	writeSettingsString(runtimeKv, parent, "filespec", options.filespec);
	writeSettingsString(runtimeKv, parent, "startingPath", options.startingPath);
	writeSettingsString(runtimeKv, parent, "searchText", options.searchText);
	writeSettingsString(runtimeKv, parent, "replacementText", options.replacementText);
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
				return setError(errorMessage, "May contain only M, D, L or C.");
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

} // namespace

void recordSettingsRuntimeRead() {
	recordSettingsRuntimeIo(false);
}

void recordSettingsRuntimeWrite() {
	recordSettingsRuntimeIo(true);
}

MRSettingsRuntimeIoRateSnapshot settingsRuntimeIoRateSnapshot() {
	SettingsIoMeter &meter = settingsIoMeter();
	const std::uint64_t now = settingsIoSecondNow();
	MRSettingsRuntimeIoRateSnapshot snapshot;
	std::lock_guard<std::mutex> lock(meter.mutex);

	for (const SettingsIoBucket &bucket : meter.buckets) {
		if (bucket.second == 0 || bucket.second + 60 <= now) continue;
		snapshot.readsPerMinute += bucket.reads;
		snapshot.writesPerMinute += bucket.writes;
	}
	return snapshot;
}

std::vector<std::string> configuredAutoexecMacroStorage() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readSettingsStringArray(runtimeKv, settingsBranch(runtimeKv, "autoexec"), "entries");
}

void storeConfiguredAutoexecMacroStorage(const std::vector<std::string> &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeSettingsStringArray(runtimeKv, settingsBranch(runtimeKv, "autoexec"), "entries", value);
}

bool configuredSettingsDirtyFlag() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readSettingsInt(runtimeKv, settingsRuntimeRoot(runtimeKv), "dirty", 0) != 0;
}

void storeConfiguredSettingsDirtyFlag(bool value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeSettingsInt(runtimeKv, settingsRuntimeRoot(runtimeKv), "dirty", value ? 1 : 0);
}

void markConfiguredSettingsDirty() {
	recordSettingsRuntimeWrite();
	storeConfiguredSettingsDirtyFlag(true);
}

std::string configuredSettingsMacroFile() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readSettingsString(runtimeKv, settingsBranch(runtimeKv, "paths"), "settingsMacroFile");
}

void storeConfiguredSettingsMacroFile(const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeSettingsString(runtimeKv, settingsBranch(runtimeKv, "paths"), "settingsMacroFile", value);
}

std::string configuredMacroDirectory() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readSettingsString(runtimeKv, settingsBranch(runtimeKv, "paths"), "macroDirectory");
}

void storeConfiguredMacroDirectory(const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeSettingsString(runtimeKv, settingsBranch(runtimeKv, "paths"), "macroDirectory", value);
}

std::string configuredHelpFile() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readSettingsString(runtimeKv, settingsBranch(runtimeKv, "paths"), "helpFile");
}

void storeConfiguredHelpFile(const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeSettingsString(runtimeKv, settingsBranch(runtimeKv, "paths"), "helpFile", value);
}

std::string configuredTempDirectory() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readSettingsString(runtimeKv, settingsBranch(runtimeKv, "paths"), "tempDirectory");
}

void storeConfiguredTempDirectory(const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeSettingsString(runtimeKv, settingsBranch(runtimeKv, "paths"), "tempDirectory", value);
}

std::string configuredShellExecutable() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readSettingsString(runtimeKv, settingsBranch(runtimeKv, "paths"), "shellExecutable");
}

void storeConfiguredShellExecutable(const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeSettingsString(runtimeKv, settingsBranch(runtimeKv, "paths"), "shellExecutable", value);
}

std::string configuredAudioPlayer() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readSettingsString(runtimeKv, settingsBranch(runtimeKv, "paths"), "audioPlayer");
}

void storeConfiguredAudioPlayer(const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeSettingsString(runtimeKv, settingsBranch(runtimeKv, "paths"), "audioPlayer", value);
}

std::string configuredLogFile() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readSettingsString(runtimeKv, settingsBranch(runtimeKv, "paths"), "logFile");
}

void storeConfiguredLogFile(const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeSettingsString(runtimeKv, settingsBranch(runtimeKv, "paths"), "logFile", value);
}

bool setConfiguredWindowManager(bool enabled, std::string *errorMessage) {
	if (configuredWindowManager() != enabled) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("windowManagerEnabled", enabled ? 1 : 0);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredWindowManager() {
	recordSettingsRuntimeRead();
	return configuredRuntimeInt("windowManagerEnabled", 1) != 0;
}

bool setConfiguredMenulineMessages(bool enabled, std::string *errorMessage) {
	const bool previous = configuredMenulineMessages();

	if (!enabled) {
		mr::messageline::clearOwner(mr::messageline::Owner::HeroEvent);
		mr::messageline::clearOwner(mr::messageline::Owner::HeroEventFollowup);
		mr::messageline::clearOwner(mr::messageline::Owner::MacroMessage);
		mr::messageline::clearOwner(mr::messageline::Owner::MacroMarquee);
		mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
		mr::messageline::clearOwner(mr::messageline::Owner::DialogInteraction);
	}
	storeConfiguredRuntimeInt("menulineMessagesEnabled", enabled ? 1 : 0);
	if (previous != enabled) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredMenulineMessages() {
	recordSettingsRuntimeRead();
	return configuredRuntimeInt("menulineMessagesEnabled", 1) != 0;
}

bool setConfiguredAutoDetectBinaryFiles(bool enabled, std::string *errorMessage) {
	if (configuredAutoDetectBinaryFiles() != enabled) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("autoDetectBinaryFiles", enabled ? 1 : 0);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredAutoDetectBinaryFiles() {
	recordSettingsRuntimeRead();
	return configuredRuntimeInt("autoDetectBinaryFiles", 1) != 0;
}

bool setConfiguredSearchDialogOptions(const MRSearchDialogOptions &options, std::string *errorMessage) {
	if (configuredSearchDialogOptions() != options) markConfiguredSettingsDirty();
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
		writeSearchDialogOptions(runtimeKv, settingsBranch(runtimeKv, "searchDialog"), options);
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRSearchDialogOptions configuredSearchDialogOptions() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRSearchDialogOptions options;

	recordSettingsRuntimeRead();
	readSearchDialogOptions(runtimeKv, settingsBranch(runtimeKv, "searchDialog"), options);
	return options;
}

bool setConfiguredSarDialogOptions(const MRSarDialogOptions &options, std::string *errorMessage) {
	if (configuredSarDialogOptions() != options) markConfiguredSettingsDirty();
	storeSarDialogOptions(options);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRSarDialogOptions configuredSarDialogOptions() {
	recordSettingsRuntimeRead();
	return readSarDialogOptions();
}

bool setConfiguredMultiSearchDialogOptions(const MRMultiSearchDialogOptions &options, std::string *errorMessage) {
	if (configuredMultiSearchDialogOptions() != options) markConfiguredSettingsDirty();
	storeMultiSearchDialogOptions("multiSearchDialog", options);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRMultiSearchDialogOptions configuredMultiSearchDialogOptions() {
	recordSettingsRuntimeRead();
	return readMultiSearchDialogOptions("multiSearchDialog");
}

bool setConfiguredMultiSarDialogOptions(const MRMultiSarDialogOptions &options, std::string *errorMessage) {
	if (configuredMultiSarDialogOptions() != options) markConfiguredSettingsDirty();
	storeMultiSarDialogOptions(options);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRMultiSarDialogOptions configuredMultiSarDialogOptions() {
	recordSettingsRuntimeRead();
	return readMultiSarDialogOptions();
}

bool setConfiguredPdfExportSettings(const MRPdfExportSettings &settings, std::string *errorMessage) {
	const MRPdfExportSettings previousSettings = configuredPdfExportSettings();
	const MRScopedDialogHistoryState previousHistory = dialogHistoryState(MRDialogHistoryScope::PdfExport);

	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
		VirtualMachine::Value parent = settingsBranch(runtimeKv, "pdfExport");
		writeSettingsString(runtimeKv, parent, "outputPath", settings.outputPath);
		writeSettingsString(runtimeKv, parent, "pageSeparatorLiteral", settings.pageSeparatorLiteral);
		writeSettingsString(runtimeKv, parent, "fontFamily", settings.fontFamily);
		writeSettingsInt(runtimeKv, parent, "fontSizePoints", settings.fontSizePoints);
		writeSettingsString(runtimeKv, parent, "headerLine", settings.headerLine);
		writeSettingsString(runtimeKv, parent, "footerLine", settings.footerLine);
		writeSettingsString(runtimeKv, parent, "textWidth", settings.textWidth);
		writeSettingsString(runtimeKv, parent, "leftMarginPoints", settings.leftMarginPoints);
		writeSettingsString(runtimeKv, parent, "rightMarginPoints", settings.rightMarginPoints);
		writeSettingsString(runtimeKv, parent, "topMarginPoints", settings.topMarginPoints);
		writeSettingsString(runtimeKv, parent, "bottomMarginPoints", settings.bottomMarginPoints);
	}
	if (!trimAscii(settings.outputPath).empty()) static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::PdfExport, settings.outputPath, nullptr));
	if (previousSettings != settings || previousHistory != dialogHistoryState(MRDialogHistoryScope::PdfExport)) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRPdfExportSettings configuredPdfExportSettings() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value parent = settingsBranch(runtimeKv, "pdfExport");
	MRPdfExportSettings settings;

	settings.outputPath = readSettingsString(runtimeKv, parent, "outputPath");
	settings.pageSeparatorLiteral = readSettingsString(runtimeKv, parent, "pageSeparatorLiteral", settings.pageSeparatorLiteral);
	settings.fontFamily = readSettingsString(runtimeKv, parent, "fontFamily", settings.fontFamily);
	settings.fontSizePoints = readSettingsInt(runtimeKv, parent, "fontSizePoints", settings.fontSizePoints);
	settings.headerLine = readSettingsString(runtimeKv, parent, "headerLine");
	settings.footerLine = readSettingsString(runtimeKv, parent, "footerLine");
	settings.textWidth = readSettingsString(runtimeKv, parent, "textWidth", settings.textWidth);
	settings.leftMarginPoints = readSettingsString(runtimeKv, parent, "leftMarginPoints", settings.leftMarginPoints);
	settings.rightMarginPoints = readSettingsString(runtimeKv, parent, "rightMarginPoints", settings.rightMarginPoints);
	settings.topMarginPoints = readSettingsString(runtimeKv, parent, "topMarginPoints", settings.topMarginPoints);
	settings.bottomMarginPoints = readSettingsString(runtimeKv, parent, "bottomMarginPoints", settings.bottomMarginPoints);
	recordSettingsRuntimeRead();
	return settings;
}

bool setConfiguredAcquireSettings(const MRAcquireSettings &settings, std::string *errorMessage) {
	const MRAcquireSettings previousSettings = configuredAcquireSettings();
	MRAcquireSettings normalized = settings;

	normalized.commandLine = trimAscii(normalized.commandLine);
	normalizeAcquireCommandHistory(normalized.commandHistory);
	if (!normalized.commandLine.empty()) {
		normalized.commandHistory.erase(std::remove(normalized.commandHistory.begin(), normalized.commandHistory.end(), normalized.commandLine), normalized.commandHistory.end());
		normalized.commandHistory.insert(normalized.commandHistory.begin(), normalized.commandLine);
		normalizeAcquireCommandHistory(normalized.commandHistory);
	}
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
		VirtualMachine::Value parent = settingsBranch(runtimeKv, "acquire");
		writeSettingsString(runtimeKv, parent, "commandLine", normalized.commandLine);
		writeSettingsStringArray(runtimeKv, parent, "commandHistory", normalized.commandHistory);
	}
	if (previousSettings != normalized) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRAcquireSettings configuredAcquireSettings() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value parent = settingsBranch(runtimeKv, "acquire");
	MRAcquireSettings settings;

	settings.commandLine = readSettingsString(runtimeKv, parent, "commandLine");
	settings.commandHistory = readSettingsStringArray(runtimeKv, parent, "commandHistory");
	recordSettingsRuntimeRead();
	return settings;
}

bool setConfiguredLiveLogSettings(const MRLiveLogSettings &settings, std::string *errorMessage) {
	MRLiveLogSettings normalized = settings;

	normalizeLiveLogSettings(normalized);
	if (configuredLiveLogSettings() != normalized) markConfiguredSettingsDirty();
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
		VirtualMachine::Value parent = settingsBranch(runtimeKv, "liveLog");
		writeSettingsInt(runtimeKv, parent, "reportSearchHitsOnMessageLine", normalized.reportSearchHitsOnMessageLine ? 1 : 0);
		writeSettingsInt(runtimeKv, parent, "reportSearchHitsWithSystemBeep", normalized.reportSearchHitsWithSystemBeep ? 1 : 0);
		writeSettingsInt(runtimeKv, parent, "reportSearchHitsWithAudioSignal", normalized.reportSearchHitsWithAudioSignal ? 1 : 0);
		writeSettingsInt(runtimeKv, parent, "scrollDirection", static_cast<int>(normalized.scrollDirection));
		writeSettingsInt(runtimeKv, parent, "showLineNumbers", normalized.showLineNumbers ? 1 : 0);
		writeSettingsInt(runtimeKv, parent, "showTimestamps", normalized.showTimestamps ? 1 : 0);
		writeSettingsInt(runtimeKv, parent, "syntaxHighlighting", normalized.syntaxHighlighting ? 1 : 0);
		writeSettingsString(runtimeKv, parent, "audioSignalUri", normalized.audioSignalUri);
		writeSettingsStringArray(runtimeKv, parent, "journalAppTagHistory", normalized.journalAppTagHistory);
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRLiveLogSettings configuredLiveLogSettings() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value parent = settingsBranch(runtimeKv, "liveLog");
	MRLiveLogSettings settings;

	settings.reportSearchHitsOnMessageLine = readSettingsInt(runtimeKv, parent, "reportSearchHitsOnMessageLine", settings.reportSearchHitsOnMessageLine ? 1 : 0) != 0;
	settings.reportSearchHitsWithSystemBeep = readSettingsInt(runtimeKv, parent, "reportSearchHitsWithSystemBeep", settings.reportSearchHitsWithSystemBeep ? 1 : 0) != 0;
	settings.reportSearchHitsWithAudioSignal = readSettingsInt(runtimeKv, parent, "reportSearchHitsWithAudioSignal", settings.reportSearchHitsWithAudioSignal ? 1 : 0) != 0;
	settings.scrollDirection = static_cast<MRLiveLogScrollDirection>(readSettingsInt(runtimeKv, parent, "scrollDirection", static_cast<int>(settings.scrollDirection)));
	settings.showLineNumbers = readSettingsInt(runtimeKv, parent, "showLineNumbers", settings.showLineNumbers ? 1 : 0) != 0;
	settings.showTimestamps = readSettingsInt(runtimeKv, parent, "showTimestamps", settings.showTimestamps ? 1 : 0) != 0;
	settings.syntaxHighlighting = readSettingsInt(runtimeKv, parent, "syntaxHighlighting", settings.syntaxHighlighting ? 1 : 0) != 0;
	settings.audioSignalUri = readSettingsString(runtimeKv, parent, "audioSignalUri");
	settings.journalAppTagHistory = readSettingsStringArray(runtimeKv, parent, "journalAppTagHistory");
	recordSettingsRuntimeRead();
	return settings;
}

bool setConfiguredAudioPlayerPath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	const std::string previousPath = configuredAudioPlayer();

	const std::string configured = normalized.empty() ? std::string() : makeAbsolutePath(normalized);
	storeConfiguredAudioPlayer(configured);
	if (previousPath != configured) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredAudioPlayerPath() {
	const std::string configured = configuredAudioPlayer();

	recordSettingsRuntimeRead();
	if (!configured.empty() && isExecutableFile(configured)) return makeAbsolutePath(configured);
	return std::string();
}

bool setConfiguredVirtualDesktops(int count, std::string *errorMessage) {
	if (count < 1) count = 1;
	if (count > 9) count = 9;
	if (configuredVirtualDesktops() != count) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("virtualDesktops", count);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

int configuredVirtualDesktops() {
	recordSettingsRuntimeRead();
	return configuredRuntimeInt("virtualDesktops", 1);
}

bool setConfiguredCyclicVirtualDesktops(bool enabled, std::string *errorMessage) {
	if (configuredCyclicVirtualDesktops() != enabled) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("cyclicVirtualDesktops", enabled ? 1 : 0);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredCyclicVirtualDesktops() {
	recordSettingsRuntimeRead();
	return configuredRuntimeInt("cyclicVirtualDesktops", 0) != 0;
}

bool setConfiguredCursorBehaviour(MRCursorBehaviour behaviour, std::string *errorMessage) {
	if (configuredCursorBehaviour() != behaviour) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("cursorBehaviour", static_cast<int>(behaviour));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRCursorBehaviour configuredCursorBehaviour() {
	recordSettingsRuntimeRead();
	return static_cast<MRCursorBehaviour>(configuredRuntimeInt("cursorBehaviour", static_cast<int>(MRCursorBehaviour::BoundToText)));
}

bool setConfiguredCompilerErrorMessagePlacement(MRCompilerErrorMessagePlacement placement, std::string *errorMessage) {
	if (configuredCompilerErrorMessagePlacement() != placement) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("compilerErrorMessagePlacement", static_cast<int>(placement));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRCompilerErrorMessagePlacement configuredCompilerErrorMessagePlacement() {
	recordSettingsRuntimeRead();
	return static_cast<MRCompilerErrorMessagePlacement>(configuredRuntimeInt("compilerErrorMessagePlacement", static_cast<int>(MRCompilerErrorMessagePlacement::RightMargin)));
}

bool setConfiguredScrollbarVisibility(MRScrollbarVisibility visibility, std::string *errorMessage) {
	if (configuredScrollbarVisibility() != visibility) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("scrollbarVisibility", static_cast<int>(visibility));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRScrollbarVisibility configuredScrollbarVisibility() {
	recordSettingsRuntimeRead();
	return static_cast<MRScrollbarVisibility>(configuredRuntimeInt("scrollbarVisibility", static_cast<int>(MRScrollbarVisibility::Smart)));
}

bool setConfiguredColorOutputMode(MRColorOutputMode mode, std::string *errorMessage) {
	if (mode != MRColorOutputMode::RgbAutomatic && mode != MRColorOutputMode::TerminalPalette) mode = MRColorOutputMode::RgbAutomatic;
	if (configuredColorOutputModeValue() != mode) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("colorOutputMode", static_cast<int>(mode));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRColorOutputMode configuredColorOutputMode() {
	recordSettingsRuntimeRead();
	return configuredColorOutputModeValue();
}

MRColorOutputMode configuredColorOutputModeValue() {
	const int stored = configuredRuntimeInt("colorOutputMode", static_cast<int>(MRColorOutputMode::RgbAutomatic));

	if (stored == static_cast<int>(MRColorOutputMode::TerminalPalette)) return MRColorOutputMode::TerminalPalette;
	return MRColorOutputMode::RgbAutomatic;
}

bool setConfiguredTrackCompilerWarnings(bool enabled, std::string *errorMessage) {
	if (configuredTrackCompilerWarnings() != enabled) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("trackCompilerWarnings", enabled ? 1 : 0);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredTrackCompilerWarnings() {
	recordSettingsRuntimeRead();
	return configuredRuntimeInt("trackCompilerWarnings", 0) != 0;
}

bool setConfiguredTrackCompilerNotes(bool enabled, std::string *errorMessage) {
	if (configuredTrackCompilerNotes() != enabled) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("trackCompilerNotes", enabled ? 1 : 0);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredTrackCompilerNotes() {
	recordSettingsRuntimeRead();
	return configuredRuntimeInt("trackCompilerNotes", 0) != 0;
}

bool setConfiguredUiIndentStyle(MRUiIndentStyle style, std::string *errorMessage) {
	if (configuredUiIndentStyle() != style) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("uiIndentStyle", static_cast<int>(style));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRUiIndentStyle configuredUiIndentStyle() {
	recordSettingsRuntimeRead();
	return static_cast<MRUiIndentStyle>(configuredRuntimeInt("uiIndentStyle", static_cast<int>(MRUiIndentStyle::KandR)));
}

bool setConfiguredCursorPositionMarker(const std::string &value, std::string *errorMessage) {
	std::string normalized;

	if (!normalizeCursorPositionMarker(value, normalized, errorMessage)) return false;
	if (configuredCursorPositionMarker() != normalized) markConfiguredSettingsDirty();
	storeConfiguredRuntimeString("cursorPositionMarker", normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredCursorPositionMarker() {
	recordSettingsRuntimeRead();
	return configuredRuntimeString("cursorPositionMarker", "R:C");
}

bool setConfiguredFileCompareOriginalLeadingGutters(const std::string &value, std::string *errorMessage) {
	std::string normalized;

	if (!normalizeFileCompareGutters(value, normalized, errorMessage)) return false;
	if (configuredFileCompareOriginalLeadingGutters() != normalized) markConfiguredSettingsDirty();
	storeConfiguredRuntimeString("fileCompareOriginalLeadingGutters", normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredFileCompareOriginalLeadingGutters() {
	recordSettingsRuntimeRead();
	return configuredRuntimeString("fileCompareOriginalLeadingGutters", "L");
}

bool setConfiguredFileCompareOriginalTrailingGutters(const std::string &value, std::string *errorMessage) {
	std::string normalized;

	if (!normalizeFileCompareGutters(value, normalized, errorMessage)) return false;
	if (configuredFileCompareOriginalTrailingGutters() != normalized) markConfiguredSettingsDirty();
	storeConfiguredRuntimeString("fileCompareOriginalTrailingGutters", normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredFileCompareOriginalTrailingGutters() {
	recordSettingsRuntimeRead();
	return configuredRuntimeString("fileCompareOriginalTrailingGutters", "M");
}

bool setConfiguredFileCompareCompareLeadingGutters(const std::string &value, std::string *errorMessage) {
	std::string normalized;

	if (!normalizeFileCompareGutters(value, normalized, errorMessage)) return false;
	if (configuredFileCompareCompareLeadingGutters() != normalized) markConfiguredSettingsDirty();
	storeConfiguredRuntimeString("fileCompareCompareLeadingGutters", normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredFileCompareCompareLeadingGutters() {
	recordSettingsRuntimeRead();
	return configuredRuntimeString("fileCompareCompareLeadingGutters", "LD");
}

bool setConfiguredFileCompareCompareTrailingGutters(const std::string &value, std::string *errorMessage) {
	std::string normalized;

	if (!normalizeFileCompareGutters(value, normalized, errorMessage)) return false;
	if (configuredFileCompareCompareTrailingGutters() != normalized) markConfiguredSettingsDirty();
	storeConfiguredRuntimeString("fileCompareCompareTrailingGutters", normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredFileCompareCompareTrailingGutters() {
	recordSettingsRuntimeRead();
	return configuredRuntimeString("fileCompareCompareTrailingGutters");
}

bool setConfiguredFileCompareStartConfiguration(MRFileCompareStartConfiguration configuration, std::string *errorMessage) {
	if (configuredFileCompareStartConfiguration() != configuration) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("fileCompareStartConfiguration", static_cast<int>(configuration));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRFileCompareStartConfiguration configuredFileCompareStartConfiguration() {
	recordSettingsRuntimeRead();
	return static_cast<MRFileCompareStartConfiguration>(configuredRuntimeInt("fileCompareStartConfiguration", static_cast<int>(MRFileCompareStartConfiguration::OriginalCompare)));
}

bool setConfiguredFileCompareComparePanelReadOnly(bool enabled, std::string *errorMessage) {
	if (configuredFileCompareComparePanelReadOnly() != enabled) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("fileCompareComparePanelReadOnly", enabled ? 1 : 0);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredFileCompareComparePanelReadOnly() {
	recordSettingsRuntimeRead();
	return configuredRuntimeInt("fileCompareComparePanelReadOnly", 1) != 0;
}

bool setConfiguredAutosaveWorkspace(bool enabled, std::string *errorMessage) {
	if (configuredAutosaveWorkspace() != enabled) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("autosaveWorkspace", enabled ? 1 : 0);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredAutosaveWorkspace() {
	recordSettingsRuntimeRead();
	return configuredRuntimeInt("autosaveWorkspace", 0) != 0;
}

void setRuntimePreserveAutosavedWorkspace(bool enabled) {
	storeConfiguredRuntimeInt("preserveAutosavedWorkspace", enabled ? 1 : 0);
}

bool runtimePreserveAutosavedWorkspace() {
	recordSettingsRuntimeRead();
	return configuredRuntimeInt("preserveAutosavedWorkspace", 0) != 0;
}

bool setConfiguredAutoloadWorkspace(bool enabled, std::string *errorMessage) {
	if (configuredAutoloadWorkspace() != enabled) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("autoloadWorkspace", enabled ? 1 : 0);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredAutoloadWorkspace() {
	recordSettingsRuntimeRead();
	return configuredRuntimeInt("autoloadWorkspace", 0) != 0;
}

bool setConfiguredLogHandling(MRLogHandling handling, std::string *errorMessage) {
	if (configuredLogHandling() != handling) markConfiguredSettingsDirty();
	storeConfiguredRuntimeInt("logHandling", static_cast<int>(handling));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRLogHandling configuredLogHandling() {
	recordSettingsRuntimeRead();
	return static_cast<MRLogHandling>(configuredRuntimeInt("logHandling", static_cast<int>(MRLogHandling::Volatile)));
}
