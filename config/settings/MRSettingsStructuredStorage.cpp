#include "MRSettingsEditSetup.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "../../mrmac/mrmac.h"
#include "../../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../../mrmac/vm/MRVMValue.hpp"

#include <array>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace {

using Value = VirtualMachine::Value;

struct CompilerStringField {
	const char *key;
	std::string MRCompilerProfile::*member;
};

struct CompilerStringListField {
	const char *key;
	std::vector<std::string> MRCompilerProfile::*member;
};

const CompilerStringField kCompilerStringFields[] = {
    {"id", &MRCompilerProfile::id},
    {"name", &MRCompilerProfile::name},
    {"toolchain", &MRCompilerProfile::toolchain},
    {"executablePath", &MRCompilerProfile::executablePath},
    {"versionText", &MRCompilerProfile::versionText},
    {"targetTriple", &MRCompilerProfile::targetTriple},
    {"buildFlags", &MRCompilerProfile::buildFlags},
    {"preBuildCommand", &MRCompilerProfile::preBuildCommand},
    {"buildSucceededCommand", &MRCompilerProfile::buildSucceededCommand},
    {"buildFailedCommand", &MRCompilerProfile::buildFailedCommand},
    {"preBuildMacro", &MRCompilerProfile::preBuildMacro},
    {"postBuildMacro", &MRCompilerProfile::postBuildMacro},
    {"buildSuccessAudioUri", &MRCompilerProfile::buildSuccessAudioUri},
    {"buildFailureAudioUri", &MRCompilerProfile::buildFailureAudioUri},
};

const CompilerStringListField kCompilerStringListFields[] = {
    {"includePaths", &MRCompilerProfile::includePaths},
    {"libraryPaths", &MRCompilerProfile::libraryPaths},
    {"runtimePaths", &MRCompilerProfile::runtimePaths},
};

Value settingsRuntimeRoot(MRVMRuntimeKv &runtimeKv) {
	Value settings = runtimeKv.ensureRoot("SETTINGS");
	return runtimeKv.ensureChild(settings, "runtime");
}

Value settingsBranch(MRVMRuntimeKv &runtimeKv, const char *key) {
	return runtimeKv.ensureChild(settingsRuntimeRoot(runtimeKv), key);
}

Value settingsStagingRoot(MRVMRuntimeKv &runtimeKv) {
	return runtimeKv.ensureChild(runtimeKv.ensureRoot("SETTINGS"), "staging");
}

Value keymapStagingBranch(MRVMRuntimeKv &runtimeKv) {
	return runtimeKv.ensureChild(settingsStagingRoot(runtimeKv), "keymap");
}

bool contains(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key) {
	MRVMHashStore &store = runtimeKv.globalStore();
	return mrvmHashContainsValue(store, store, parent, key);
}

int readInt(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key, int fallback = 0) {
	MRVMHashStore &store = runtimeKv.globalStore();
	if (!mrvmHashContainsValue(store, store, parent, key)) return fallback;
	const Value value = mrvmHashReadValue(store, store, parent, key);
	return value.type == TYPE_INT ? value.i : fallback;
}

std::string readString(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key, const std::string &fallback = std::string()) {
	MRVMHashStore &store = runtimeKv.globalStore();
	if (!mrvmHashContainsValue(store, store, parent, key)) return fallback;
	const Value value = mrvmHashReadValue(store, store, parent, key);
	return value.type == TYPE_STR ? value.s : fallback;
}

std::vector<std::string> readStringList(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key) {
	MRVMHashStore &store = runtimeKv.globalStore();
	std::vector<std::string> result;

	if (!mrvmHashContainsValue(store, store, parent, key)) return result;
	const Value value = mrvmHashReadValue(store, store, parent, key);
	if (value.type != TYPE_STR_ARRAY) return result;
	result.reserve(value.arrayValues.size());
	for (const Value &element : value.arrayValues)
		if (element.type == TYPE_STR) result.push_back(element.s);
	return result;
}

void writeInt(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeInt(value));
}

void writeString(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(value));
}

void writeStringList(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key, const std::vector<std::string> &values) {
	MRVMHashStore &store = runtimeKv.globalStore();
	Value array = mrvmMakeArrayValue(TYPE_STR);
	array.globalStorage = true;
	array.arrayValues.reserve(values.size());
	for (const std::string &value : values)
		array.arrayValues.push_back(mrvmMakeString(value));
	mrvmHashWriteValue(store, store, parent, key, array);
}

template <std::size_t Count>
void readColorArray(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key, std::array<MRRgbColorAttribute, Count> &values) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, parent, key)) return;
	const Value stored = mrvmHashReadValue(store, store, parent, key);
	if (stored.type != TYPE_INT_ARRAY || stored.arrayValues.size() != Count * 2) return;
	for (const Value &element : stored.arrayValues)
		if (element.type != TYPE_INT || element.i < 0 || element.i > 0xFFFFFF) return;
	for (std::size_t i = 0; i < Count; ++i) {
		values[i].foregroundRgb = static_cast<std::uint32_t>(stored.arrayValues[i * 2].i);
		values[i].backgroundRgb = static_cast<std::uint32_t>(stored.arrayValues[i * 2 + 1].i);
	}
}

template <std::size_t Count>
void writeColorArray(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key, const std::array<MRRgbColorAttribute, Count> &values) {
	MRVMHashStore &store = runtimeKv.globalStore();
	Value array = mrvmMakeArrayValue(TYPE_INT);
	array.globalStorage = true;
	array.arrayValues.reserve(values.size() * 2);
	for (const MRRgbColorAttribute &value : values) {
		array.arrayValues.push_back(mrvmMakeInt(static_cast<int>(value.foregroundRgb)));
		array.arrayValues.push_back(mrvmMakeInt(static_cast<int>(value.backgroundRgb)));
	}
	mrvmHashWriteValue(store, store, parent, key, array);
}

MREditSetupSettings readEditSettings(MRVMRuntimeKv &runtimeKv, const Value &parent) {
	MREditSetupSettings settings = resolveEditSetupDefaults();
	std::size_t count = 0;
	const MREditSettingDescriptor *descriptors = editSettingDescriptors(count);

	for (std::size_t i = 0; i < count; ++i) {
		const char *key = descriptors[i].key;
		if (!contains(runtimeKv, parent, key)) continue;
		std::string ignoredError;
		static_cast<void>(applyEditSetupValueInternal(settings, key, readString(runtimeKv, parent, key), &ignoredError));
	}
	return settings;
}

void writeEditSettings(MRVMRuntimeKv &runtimeKv, const Value &parent, const MREditSetupSettings &settings) {
	std::size_t count = 0;
	const MREditSettingDescriptor *descriptors = editSettingDescriptors(count);

	for (std::size_t i = 0; i < count; ++i)
		writeString(runtimeKv, parent, descriptors[i].key, editSetupValueLiteral(settings, descriptors[i].key));
}

unsigned long long readMask(MRVMRuntimeKv &runtimeKv, const Value &parent) {
	const std::string text = readString(runtimeKv, parent, "overrideMask");
	char *end = nullptr;
	const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
	return end != text.c_str() && *end == '\0' ? value : 0;
}

std::vector<MREditExtensionProfile> readEditProfiles(MRVMRuntimeKv &runtimeKv, const Value &parent) {
	std::vector<MREditExtensionProfile> profiles;
	const int count = readInt(runtimeKv, parent, "count");

	for (int i = 0; i < count; ++i) {
		Value profileNode;
		if (!runtimeKv.findChild(parent, std::to_string(i), profileNode)) continue;
		MREditExtensionProfile profile;
		profile.id = readString(runtimeKv, profileNode, "id");
		profile.name = readString(runtimeKv, profileNode, "name");
		profile.extensions = readStringList(runtimeKv, profileNode, "extensions");
		profile.windowColorThemeUri = readString(runtimeKv, profileNode, "windowColorThemeUri");
		profile.compilerProfileId = readString(runtimeKv, profileNode, "compilerProfileId");
		profile.overrides.mask = readMask(runtimeKv, profileNode);
		Value overrides;
		if (runtimeKv.findChild(profileNode, "overrides", overrides)) profile.overrides.values = readEditSettings(runtimeKv, overrides);
		profiles.push_back(profile);
	}
	return profiles;
}

void writeEditProfiles(MRVMRuntimeKv &runtimeKv, const Value &runtimeRoot, const std::vector<MREditExtensionProfile> &profiles) {
	Value parent = runtimeKv.replaceChild(runtimeRoot, "editProfiles");
	writeInt(runtimeKv, parent, "count", static_cast<int>(profiles.size()));
	for (std::size_t i = 0; i < profiles.size(); ++i) {
		const MREditExtensionProfile &profile = profiles[i];
		Value profileNode = runtimeKv.ensureChild(parent, std::to_string(i));
		writeString(runtimeKv, profileNode, "id", profile.id);
		writeString(runtimeKv, profileNode, "name", profile.name);
		writeStringList(runtimeKv, profileNode, "extensions", profile.extensions);
		writeString(runtimeKv, profileNode, "windowColorThemeUri", profile.windowColorThemeUri);
		writeString(runtimeKv, profileNode, "compilerProfileId", profile.compilerProfileId);
		writeString(runtimeKv, profileNode, "overrideMask", std::to_string(profile.overrides.mask));
		writeEditSettings(runtimeKv, runtimeKv.ensureChild(profileNode, "overrides"), profile.overrides.values);
	}
}

std::vector<MRCompilerProfile> readCompilerProfiles(MRVMRuntimeKv &runtimeKv, const Value &parent) {
	std::vector<MRCompilerProfile> profiles;
	const int count = readInt(runtimeKv, parent, "count");

	for (int i = 0; i < count; ++i) {
		Value profileNode;
		if (!runtimeKv.findChild(parent, std::to_string(i), profileNode)) continue;
		MRCompilerProfile profile;
		for (const CompilerStringField &field : kCompilerStringFields)
			profile.*(field.member) = readString(runtimeKv, profileNode, field.key);
		for (const CompilerStringListField &field : kCompilerStringListFields)
			profile.*(field.member) = readStringList(runtimeKv, profileNode, field.key);
		profiles.push_back(profile);
	}
	return profiles;
}

void writeCompilerProfiles(MRVMRuntimeKv &runtimeKv, const Value &runtimeRoot, const char *key, const std::vector<MRCompilerProfile> &profiles) {
	Value parent = runtimeKv.replaceChild(runtimeRoot, key);
	writeInt(runtimeKv, parent, "count", static_cast<int>(profiles.size()));
	for (std::size_t i = 0; i < profiles.size(); ++i) {
		const MRCompilerProfile &profile = profiles[i];
		Value profileNode = runtimeKv.ensureChild(parent, std::to_string(i));
		for (const CompilerStringField &field : kCompilerStringFields)
			writeString(runtimeKv, profileNode, field.key, profile.*(field.member));
		for (const CompilerStringListField &field : kCompilerStringListFields)
			writeStringList(runtimeKv, profileNode, field.key, profile.*(field.member));
	}
}

std::vector<MRKeymapProfile> readKeymapProfiles(MRVMRuntimeKv &runtimeKv, const Value &parent) {
	std::vector<MRKeymapProfile> profiles;
	const int count = readInt(runtimeKv, parent, "count");

	for (int i = 0; i < count; ++i) {
		Value profileNode;
		if (!runtimeKv.findChild(parent, std::to_string(i), profileNode)) continue;
		MRKeymapProfile profile;
		profile.name = readString(runtimeKv, profileNode, "name");
		profile.description = readString(runtimeKv, profileNode, "description");
		Value bindings;
		if (runtimeKv.findChild(profileNode, "bindings", bindings)) {
			const int bindingCount = readInt(runtimeKv, bindings, "count");
			for (int j = 0; j < bindingCount; ++j) {
				Value bindingNode;
				if (!runtimeKv.findChild(bindings, std::to_string(j), bindingNode)) continue;
				MRKeymapBindingRecord binding;
				binding.profileName = readString(runtimeKv, bindingNode, "profileName");
				binding.context = static_cast<MRKeymapContext>(readInt(runtimeKv, bindingNode, "context"));
				binding.target.type = static_cast<MRKeymapBindingType>(readInt(runtimeKv, bindingNode, "targetType"));
				binding.target.target = readString(runtimeKv, bindingNode, "target");
				binding.description = readString(runtimeKv, bindingNode, "description");
				const std::optional<MRKeymapSequence> sequence = MRKeymapSequence::parse(readString(runtimeKv, bindingNode, "sequence"));
				if (sequence) binding.sequence = *sequence;
				profile.bindings.push_back(binding);
			}
		}
		profiles.push_back(profile);
	}
	return profiles;
}

void writeKeymapBinding(MRVMRuntimeKv &runtimeKv, const Value &parent, const MRKeymapBindingRecord &binding) {
	writeString(runtimeKv, parent, "profileName", binding.profileName);
	writeInt(runtimeKv, parent, "context", static_cast<int>(binding.context));
	writeInt(runtimeKv, parent, "targetType", static_cast<int>(binding.target.type));
	writeString(runtimeKv, parent, "target", binding.target.target);
	writeString(runtimeKv, parent, "sequence", binding.sequence.toString());
	writeString(runtimeKv, parent, "description", binding.description);
}

void writeKeymapProfile(MRVMRuntimeKv &runtimeKv, const Value &parent, const MRKeymapProfile &profile) {
	Value bindings = runtimeKv.ensureChild(parent, "bindings");
	writeString(runtimeKv, parent, "name", profile.name);
	writeString(runtimeKv, parent, "description", profile.description);
	writeInt(runtimeKv, bindings, "count", static_cast<int>(profile.bindings.size()));
	for (std::size_t i = 0; i < profile.bindings.size(); ++i)
		writeKeymapBinding(runtimeKv, runtimeKv.ensureChild(bindings, std::to_string(i)), profile.bindings[i]);
}

void writeKeymapProfiles(MRVMRuntimeKv &runtimeKv, const Value &runtimeRoot, const std::vector<MRKeymapProfile> &profiles) {
	Value parent = runtimeKv.replaceChild(runtimeRoot, "keymapProfiles");
	writeInt(runtimeKv, parent, "count", static_cast<int>(profiles.size()));
	for (std::size_t i = 0; i < profiles.size(); ++i) {
		Value profileNode = runtimeKv.ensureChild(parent, std::to_string(i));
		writeKeymapProfile(runtimeKv, profileNode, profiles[i]);
	}
}

} // namespace

MREditSetupSettings configuredEditSettings() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value parent = settingsBranch(runtimeKv, "editSettings");

	if (readInt(runtimeKv, parent, "initialized") == 0) {
		const MREditSetupSettings defaults = resolveEditSetupDefaults();
		writeEditSettings(runtimeKv, parent, defaults);
		writeInt(runtimeKv, parent, "initialized", 1);
		return defaults;
	}
	return readEditSettings(runtimeKv, parent);
}

void storeConfiguredEditSettings(const MREditSetupSettings &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value parent = runtimeKv.replaceChild(settingsRuntimeRoot(runtimeKv), "editSettings");
	writeEditSettings(runtimeKv, parent, value);
	writeInt(runtimeKv, parent, "initialized", 1);
}

std::vector<MREditExtensionProfile> configuredEditProfiles() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readEditProfiles(runtimeKv, settingsBranch(runtimeKv, "editProfiles"));
}

void storeConfiguredEditProfiles(const std::vector<MREditExtensionProfile> &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeEditProfiles(runtimeKv, settingsRuntimeRoot(runtimeKv), value);
}

std::vector<MRCompilerProfile> configuredCompilerProfilesValue() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readCompilerProfiles(runtimeKv, settingsBranch(runtimeKv, "compilerProfiles"));
}

void storeConfiguredCompilerProfilesValue(const std::vector<MRCompilerProfile> &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeCompilerProfiles(runtimeKv, settingsRuntimeRoot(runtimeKv), "compilerProfiles", value);
}

std::string detectedCompilerProfilesCacheKey() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readString(runtimeKv, settingsBranch(runtimeKv, "compilerDetection"), "cacheKey");
}

std::vector<MRCompilerProfile> detectedCompilerProfilesCacheValue() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value parent = settingsBranch(runtimeKv, "compilerDetection");
	Value profiles;

	if (!runtimeKv.findChild(parent, "profiles", profiles)) return std::vector<MRCompilerProfile>();
	return readCompilerProfiles(runtimeKv, profiles);
}

void storeDetectedCompilerProfilesCache(const std::string &key, const std::vector<MRCompilerProfile> &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value parent = settingsBranch(runtimeKv, "compilerDetection");

	writeString(runtimeKv, parent, "cacheKey", key);
	writeCompilerProfiles(runtimeKv, parent, "profiles", value);
}

std::vector<MRKeymapProfile> configuredKeymapProfilesValue() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readKeymapProfiles(runtimeKv, settingsBranch(runtimeKv, "keymapProfiles"));
}

void storeConfiguredKeymapProfilesValue(const std::vector<MRKeymapProfile> &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeKeymapProfiles(runtimeKv, settingsRuntimeRoot(runtimeKv), value);
}

std::string configuredDefaultProfileDescriptionValue() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readString(runtimeKv, settingsBranch(runtimeKv, "keymap"), "defaultProfileDescription");
}

void storeConfiguredDefaultProfileDescriptionValue(const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeString(runtimeKv, settingsBranch(runtimeKv, "keymap"), "defaultProfileDescription", value);
}

std::string configuredKeymapFileValue() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readString(runtimeKv, settingsBranch(runtimeKv, "keymap"), "file");
}

void storeConfiguredKeymapFileValue(const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeString(runtimeKv, settingsBranch(runtimeKv, "keymap"), "file", value);
}

std::string configuredActiveKeymapProfileValue() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readString(runtimeKv, settingsBranch(runtimeKv, "keymap"), "activeProfile");
}

void storeConfiguredActiveKeymapProfileValue(const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeString(runtimeKv, settingsBranch(runtimeKv, "keymap"), "activeProfile", value);
}

MRColorSetupSettings configuredColorSettings() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value parent = settingsBranch(runtimeKv, "colors");
	MRColorSetupSettings colors = resolveColorSetupDefaults();
	readColorArray(runtimeKv, parent, "window", colors.windowColors);
	readColorArray(runtimeKv, parent, "menuDialog", colors.menuDialogColors);
	readColorArray(runtimeKv, parent, "help", colors.helpColors);
	readColorArray(runtimeKv, parent, "other", colors.otherColors);
	readColorArray(runtimeKv, parent, "miniMap", colors.miniMapColors);
	readColorArray(runtimeKv, parent, "fileCompareMiniMap", colors.fileCompareMiniMapColors);
	readColorArray(runtimeKv, parent, "code", colors.codeColors);
	readColorArray(runtimeKv, parent, "fileCompare", colors.fileCompareColors);
	readColorArray(runtimeKv, parent, "debugger", colors.debuggerColors);
	return colors;
}

void storeConfiguredColorSettings(const MRColorSetupSettings &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value parent = runtimeKv.replaceChild(settingsRuntimeRoot(runtimeKv), "colors");
	writeColorArray(runtimeKv, parent, "window", value.windowColors);
	writeColorArray(runtimeKv, parent, "menuDialog", value.menuDialogColors);
	writeColorArray(runtimeKv, parent, "help", value.helpColors);
	writeColorArray(runtimeKv, parent, "other", value.otherColors);
	writeColorArray(runtimeKv, parent, "miniMap", value.miniMapColors);
	writeColorArray(runtimeKv, parent, "fileCompareMiniMap", value.fileCompareMiniMapColors);
	writeColorArray(runtimeKv, parent, "code", value.codeColors);
	writeColorArray(runtimeKv, parent, "fileCompare", value.fileCompareColors);
	writeColorArray(runtimeKv, parent, "debugger", value.debuggerColors);
	writeInt(runtimeKv, parent, "initialized", 1);
}

bool configuredColorSettingsInitialized() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readInt(runtimeKv, settingsBranch(runtimeKv, "colors"), "initialized") != 0;
}

void storeConfiguredColorSettingsInitialized(bool value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeInt(runtimeKv, settingsBranch(runtimeKv, "colors"), "initialized", value ? 1 : 0);
}

bool settingsStartupModeValue() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readInt(runtimeKv, keymapStagingBranch(runtimeKv), "startupMode") != 0;
}

void storeSettingsStartupModeValue(bool value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeInt(runtimeKv, keymapStagingBranch(runtimeKv), "startupMode", value ? 1 : 0);
}

int settingsKeymapBatchDepthValue() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readInt(runtimeKv, keymapStagingBranch(runtimeKv), "batchDepth");
}

void storeSettingsKeymapBatchDepthValue(int value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeInt(runtimeKv, keymapStagingBranch(runtimeKv), "batchDepth", value);
}

bool configuredKeymapBatchInitializedValue() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readInt(runtimeKv, keymapStagingBranch(runtimeKv), "initialized") != 0;
}

void initializeConfiguredKeymapBatchStateValue(const std::vector<MRKeymapProfile> &profiles, const std::string &activeProfile) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value parent = keymapStagingBranch(runtimeKv);

	writeKeymapProfiles(runtimeKv, parent, profiles);
	writeString(runtimeKv, parent, "activeProfile", activeProfile);
	writeInt(runtimeKv, parent, "profilesDirty", 0);
	writeInt(runtimeKv, parent, "activeDirty", 0);
	writeInt(runtimeKv, parent, "initialized", 1);
}

void storeConfiguredKeymapBatchActiveProfileValue(const std::string &activeProfile) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value parent = keymapStagingBranch(runtimeKv);

	writeString(runtimeKv, parent, "activeProfile", activeProfile);
	writeInt(runtimeKv, parent, "activeDirty", 1);
}

void storeConfiguredKeymapBatchProfileValue(const MRKeymapProfile &profile) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value parent = keymapStagingBranch(runtimeKv);
	Value profiles = runtimeKv.ensureChild(parent, "keymapProfiles");
	const int count = readInt(runtimeKv, profiles, "count");
	int index = count;

	for (int i = 0; i < count; ++i) {
		Value profileNode;
		if (!runtimeKv.findChild(profiles, std::to_string(i), profileNode)) continue;
		if (readString(runtimeKv, profileNode, "name") != profile.name) continue;
		index = i;
		break;
	}
	if (index == count) writeInt(runtimeKv, profiles, "count", count + 1);
	writeKeymapProfile(runtimeKv, runtimeKv.replaceChild(profiles, std::to_string(index)), profile);
	writeInt(runtimeKv, parent, "profilesDirty", 1);
}

bool appendConfiguredKeymapBatchBindingValue(const MRKeymapBindingRecord &binding) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value parent = keymapStagingBranch(runtimeKv);
	Value profiles = runtimeKv.ensureChild(parent, "keymapProfiles");
	const int count = readInt(runtimeKv, profiles, "count");

	for (int i = 0; i < count; ++i) {
		Value profileNode;
		if (!runtimeKv.findChild(profiles, std::to_string(i), profileNode)) continue;
		if (readString(runtimeKv, profileNode, "name") != binding.profileName) continue;
		Value bindings = runtimeKv.ensureChild(profileNode, "bindings");
		const int bindingCount = readInt(runtimeKv, bindings, "count");
		writeKeymapBinding(runtimeKv, runtimeKv.ensureChild(bindings, std::to_string(bindingCount)), binding);
		writeInt(runtimeKv, bindings, "count", bindingCount + 1);
		writeInt(runtimeKv, parent, "profilesDirty", 1);
		return true;
	}
	return false;
}

MRConfiguredKeymapBatchState configuredKeymapBatchStateValue() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value parent = keymapStagingBranch(runtimeKv);
	MRConfiguredKeymapBatchState state;

	state.initialized = readInt(runtimeKv, parent, "initialized") != 0;
	state.profilesDirty = readInt(runtimeKv, parent, "profilesDirty") != 0;
	state.activeDirty = readInt(runtimeKv, parent, "activeDirty") != 0;
	state.profiles = readKeymapProfiles(runtimeKv, runtimeKv.ensureChild(parent, "keymapProfiles"));
	state.activeProfile = readString(runtimeKv, parent, "activeProfile");
	return state;
}

void clearConfiguredKeymapBatchStateValue() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value parent = keymapStagingBranch(runtimeKv);
	const int startupMode = readInt(runtimeKv, parent, "startupMode");
	const int batchDepth = readInt(runtimeKv, parent, "batchDepth");

	parent = runtimeKv.replaceChild(settingsStagingRoot(runtimeKv), "keymap");
	writeInt(runtimeKv, parent, "startupMode", startupMode);
	writeInt(runtimeKv, parent, "batchDepth", batchDepth);
}

std::string configuredColorThemeFile() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readString(runtimeKv, settingsBranch(runtimeKv, "theme"), "file");
}

void storeConfiguredColorThemeFile(const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeString(runtimeKv, settingsBranch(runtimeKv, "theme"), "file", value);
}

std::string configuredColorThemeDisplayNameValue() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	return readString(runtimeKv, settingsBranch(runtimeKv, "theme"), "displayName");
}

void storeConfiguredColorThemeDisplayNameValue(const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	writeString(runtimeKv, settingsBranch(runtimeKv, "theme"), "displayName", value);
}
