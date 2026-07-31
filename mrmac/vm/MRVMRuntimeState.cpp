#include "MRVMRuntimeState.hpp"

#include "MRVMValue.hpp"
#include "../mrmac.h"

#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace {

using Value = VirtualMachine::Value;

Value runtimeStateRoot(MRVMRuntimeKv &runtimeKv) {
	return runtimeKv.ensureRoot("MRMACRUNTIME");
}

Value runtimeStateBranch(MRVMRuntimeKv &runtimeKv, const char *branch) {
	return runtimeKv.ensureChild(runtimeStateRoot(runtimeKv), branch);
}

} // namespace

int mrvmRuntimeStateInt(const char *branch, const std::string &key, int fallback) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	const Value parent = runtimeStateBranch(runtimeKv, branch);

	if (!mrvmHashContainsValue(store, store, parent, key)) return fallback;
	const Value value = mrvmHashReadValue(store, store, parent, key);
	return value.type == TYPE_INT ? value.i : fallback;
}

void mrvmStoreRuntimeStateInt(const char *branch, const std::string &key, int value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, runtimeStateBranch(runtimeKv, branch), key, mrvmMakeInt(value));
}

std::size_t mrvmRuntimeStateSize(const char *branch, const std::string &key, std::size_t fallback) {
	const std::string text = mrvmRuntimeStateString(branch, key);
	char *end = nullptr;
	const unsigned long long value = std::strtoull(text.c_str(), &end, 10);

	if (end == text.c_str() || *end != '\0') return fallback;
	return static_cast<std::size_t>(value);
}

void mrvmStoreRuntimeStateSize(const char *branch, const std::string &key, std::size_t value) {
	mrvmStoreRuntimeStateString(branch, key, std::to_string(value));
}

std::string mrvmRuntimeStateString(const char *branch, const std::string &key, const std::string &fallback) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	const Value parent = runtimeStateBranch(runtimeKv, branch);

	if (!mrvmHashContainsValue(store, store, parent, key)) return fallback;
	const Value value = mrvmHashReadValue(store, store, parent, key);
	return value.type == TYPE_STR ? value.s : fallback;
}

void mrvmStoreRuntimeStateString(const char *branch, const std::string &key, const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, runtimeStateBranch(runtimeKv, branch), key, mrvmMakeString(value));
}

std::vector<int> mrvmRuntimeStateIntList(const char *branch, const std::string &key) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	const Value parent = runtimeStateBranch(runtimeKv, branch);
	std::vector<int> values;

	if (!mrvmHashContainsValue(store, store, parent, key)) return values;
	const Value stored = mrvmHashReadValue(store, store, parent, key);
	if (stored.type != TYPE_INT_ARRAY) return values;
	values.reserve(stored.arrayValues.size());
	for (const Value &value : stored.arrayValues)
		if (value.type == TYPE_INT) values.push_back(value.i);
	return values;
}

void mrvmStoreRuntimeStateIntList(const char *branch, const std::string &key, const std::vector<int> &values) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	Value stored = mrvmMakeArrayValue(TYPE_INT);
	stored.globalStorage = true;
	stored.arrayValues.reserve(values.size());
	for (int value : values)
		stored.arrayValues.push_back(mrvmMakeInt(value));
	mrvmHashWriteValue(store, store, runtimeStateBranch(runtimeKv, branch), key, stored);
}

std::vector<std::string> mrvmRuntimeStateStringList(const char *branch, const std::string &key) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	const Value parent = runtimeStateBranch(runtimeKv, branch);
	std::vector<std::string> values;

	if (!mrvmHashContainsValue(store, store, parent, key)) return values;
	const Value stored = mrvmHashReadValue(store, store, parent, key);
	if (stored.type != TYPE_STR_ARRAY) return values;
	values.reserve(stored.arrayValues.size());
	for (const Value &value : stored.arrayValues)
		if (value.type == TYPE_STR) values.push_back(value.s);
	return values;
}

void mrvmStoreRuntimeStateStringList(const char *branch, const std::string &key, const std::vector<std::string> &values) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	Value stored = mrvmMakeArrayValue(TYPE_STR);
	stored.globalStorage = true;
	stored.arrayValues.reserve(values.size());
	for (const std::string &value : values)
		stored.arrayValues.push_back(mrvmMakeString(value));
	mrvmHashWriteValue(store, store, runtimeStateBranch(runtimeKv, branch), key, stored);
}

std::vector<std::string> mrvmRuntimeStateKeys(const char *branch) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	const Value parent = runtimeStateBranch(runtimeKv, branch);
	return runtimeKv.globalStore().keys(parent.hashHandle);
}

bool mrvmEraseRuntimeStateValue(const char *branch, const std::string &key) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	const Value parent = runtimeStateBranch(runtimeKv, branch);

	if (!mrvmHashContainsValue(store, store, parent, key)) return false;
	const Value value = mrvmHashReadValue(store, store, parent, key);
	mrvmHashEraseValue(store, store, parent, key);
	store.eraseValueTrees(value, true);
	return true;
}

void mrvmClearRuntimeStateBranch(const char *branch) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	static_cast<void>(runtimeKv.replaceChild(runtimeStateRoot(runtimeKv), branch));
}

std::vector<MacroStackFrame> mrvmRuntimeMacroStack() {
	const std::vector<std::string> names = mrvmRuntimeStateStringList("macroStack", "names");
	const std::vector<int> firstRun = mrvmRuntimeStateIntList("macroStack", "firstRun");
	std::vector<MacroStackFrame> frames;

	frames.reserve(names.size());
	for (std::size_t i = 0; i < names.size(); ++i)
		frames.emplace_back(names[i], i < firstRun.size() && firstRun[i] != 0);
	return frames;
}

void mrvmStoreRuntimeMacroStack(const std::vector<MacroStackFrame> &frames) {
	std::vector<std::string> names;
	std::vector<int> firstRun;

	names.reserve(frames.size());
	firstRun.reserve(frames.size());
	for (const MacroStackFrame &frame : frames) {
		names.push_back(frame.macroName);
		firstRun.push_back(frame.firstRun ? 1 : 0);
	}
	mrvmStoreRuntimeStateStringList("macroStack", "names", names);
	mrvmStoreRuntimeStateIntList("macroStack", "firstRun", firstRun);
}

void mrvmPushRuntimeMacroFrame(const std::string &macroName, bool firstRun) {
	std::vector<MacroStackFrame> frames = mrvmRuntimeMacroStack();
	frames.emplace_back(macroName, firstRun);
	mrvmStoreRuntimeMacroStack(frames);
}

void mrvmPopRuntimeMacroFrame() {
	std::vector<MacroStackFrame> frames = mrvmRuntimeMacroStack();
	if (frames.empty()) return;
	frames.pop_back();
	mrvmStoreRuntimeMacroStack(frames);
}

std::vector<MRVMExplicitKeyBinding> mrvmRuntimeExplicitKeyBindings() {
	const std::vector<int> codes = mrvmRuntimeStateIntList("explicitKeyBindings", "codes");
	const std::vector<int> modifiers = mrvmRuntimeStateIntList("explicitKeyBindings", "modifiers");
	const std::vector<int> modes = mrvmRuntimeStateIntList("explicitKeyBindings", "modes");
	const std::vector<int> kinds = mrvmRuntimeStateIntList("explicitKeyBindings", "kinds");
	const std::vector<int> commandIds = mrvmRuntimeStateIntList("explicitKeyBindings", "commandIds");
	const std::vector<std::string> macroSpecs = mrvmRuntimeStateStringList("explicitKeyBindings", "macroSpecs");
	std::vector<MRVMExplicitKeyBinding> bindings;

	bindings.reserve(codes.size());
	for (std::size_t i = 0; i < codes.size(); ++i) {
		MRVMExplicitKeyBinding binding;
		binding.key.code = static_cast<ushort>(codes[i]);
		if (i < modifiers.size()) binding.key.mods = static_cast<ushort>(modifiers[i]);
		if (i < modes.size()) binding.mode = modes[i];
		if (i < kinds.size()) binding.kind = static_cast<MRVMExplicitBindingKind>(kinds[i]);
		if (i < commandIds.size()) binding.commandId = commandIds[i];
		if (i < macroSpecs.size()) binding.macroSpec = macroSpecs[i];
		bindings.push_back(binding);
	}
	return bindings;
}

void mrvmStoreRuntimeExplicitKeyBindings(const std::vector<MRVMExplicitKeyBinding> &bindings) {
	std::vector<int> codes;
	std::vector<int> modifiers;
	std::vector<int> modes;
	std::vector<int> kinds;
	std::vector<int> commandIds;
	std::vector<std::string> macroSpecs;

	codes.reserve(bindings.size());
	modifiers.reserve(bindings.size());
	modes.reserve(bindings.size());
	kinds.reserve(bindings.size());
	commandIds.reserve(bindings.size());
	macroSpecs.reserve(bindings.size());
	for (const MRVMExplicitKeyBinding &binding : bindings) {
		codes.push_back(binding.key.code);
		modifiers.push_back(binding.key.mods);
		modes.push_back(binding.mode);
		kinds.push_back(static_cast<int>(binding.kind));
		commandIds.push_back(binding.commandId);
		macroSpecs.push_back(binding.macroSpec);
	}
	mrvmStoreRuntimeStateIntList("explicitKeyBindings", "codes", codes);
	mrvmStoreRuntimeStateIntList("explicitKeyBindings", "modifiers", modifiers);
	mrvmStoreRuntimeStateIntList("explicitKeyBindings", "modes", modes);
	mrvmStoreRuntimeStateIntList("explicitKeyBindings", "kinds", kinds);
	mrvmStoreRuntimeStateIntList("explicitKeyBindings", "commandIds", commandIds);
	mrvmStoreRuntimeStateStringList("explicitKeyBindings", "macroSpecs", macroSpecs);
}

std::vector<MacroFunctionLabelFrame> mrvmRuntimeFunctionLabelStack() {
	const std::vector<std::string> editLabels = mrvmRuntimeStateStringList("functionLabels", "edit");
	const std::vector<std::string> shellLabels = mrvmRuntimeStateStringList("functionLabels", "shell");
	const int storedCount = mrvmRuntimeStateInt("functionLabels", "count", 1);
	const std::size_t frameCount = storedCount > 0 ? static_cast<std::size_t>(storedCount) : 1;
	std::vector<MacroFunctionLabelFrame> frames(frameCount);

	for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
		for (std::size_t labelIndex = 0; labelIndex < 49; ++labelIndex) {
			const std::size_t flatIndex = frameIndex * 49 + labelIndex;
			if (flatIndex < editLabels.size()) frames[frameIndex].editLabels[labelIndex] = editLabels[flatIndex];
			if (flatIndex < shellLabels.size()) frames[frameIndex].shellLabels[labelIndex] = shellLabels[flatIndex];
		}
	return frames;
}

void mrvmStoreRuntimeFunctionLabelStack(const std::vector<MacroFunctionLabelFrame> &inputFrames) {
	const std::vector<MacroFunctionLabelFrame> frames = inputFrames.empty() ? std::vector<MacroFunctionLabelFrame>(1) : inputFrames;
	std::vector<std::string> editLabels;
	std::vector<std::string> shellLabels;

	editLabels.reserve(frames.size() * 49);
	shellLabels.reserve(frames.size() * 49);
	for (const MacroFunctionLabelFrame &frame : frames)
		for (std::size_t labelIndex = 0; labelIndex < 49; ++labelIndex) {
			editLabels.push_back(frame.editLabels[labelIndex]);
			shellLabels.push_back(frame.shellLabels[labelIndex]);
		}
	mrvmStoreRuntimeStateInt("functionLabels", "count", static_cast<int>(frames.size()));
	mrvmStoreRuntimeStateStringList("functionLabels", "edit", editLabels);
	mrvmStoreRuntimeStateStringList("functionLabels", "shell", shellLabels);
}
