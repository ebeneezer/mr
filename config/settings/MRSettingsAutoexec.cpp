#include "../../app/utils/MRStringUtils.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "../../mrmac/mrmac.h"
#include "../../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../../mrmac/vm/MRVMValue.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace {

VirtualMachine::Value autoexecBranch(MRVMRuntimeKv &runtimeKv) {
	VirtualMachine::Value settings = runtimeKv.ensureRoot("SETTINGS");
	VirtualMachine::Value runtime = runtimeKv.ensureChild(settings, "runtime");
	return runtimeKv.ensureChild(runtime, "autoexec");
}

void writeString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const std::string &key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(value));
}

} // namespace

void configuredAutoexecMacroEntries(std::vector<std::string> &outValues) {
	recordSettingsRuntimeRead();
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
	storeConfiguredAutoexecMacroStorage(normalizedValues);
	if (previousValues != normalizedValues) markConfiguredSettingsDirty();
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
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value autoexec = autoexecBranch(runtimeKv);
	static_cast<void>(runtimeKv.replaceChild(autoexec, "diagnostics"));
}

void rememberConfiguredAutoexecMacroDiagnostic(const std::string &fileName, const std::string &errorText) {
	std::string key = trimAscii(fileName);
	for (char &ch : key)
		ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
	if (key.empty()) return;
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value diagnostics = runtimeKv.ensureChild(autoexecBranch(runtimeKv), "diagnostics");
	writeString(runtimeKv, diagnostics, key, errorText);
}

bool configuredAutoexecMacroDiagnosticForFile(const std::string &fileName, std::string &errorText) {
	std::string key = trimAscii(fileName);
	for (char &ch : key)
		ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value diagnostics = runtimeKv.ensureChild(autoexecBranch(runtimeKv), "diagnostics");
	MRVMHashStore &store = runtimeKv.globalStore();

	recordSettingsRuntimeRead();
	errorText.clear();
	if (!mrvmHashContainsValue(store, store, diagnostics, key)) return false;
	VirtualMachine::Value stored = mrvmHashReadValue(store, store, diagnostics, key);
	if (stored.type != TYPE_STR) return false;
	errorText = stored.s;
	return true;
}
