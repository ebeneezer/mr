#include "MRWindowCommandsInternal.hpp"

#include "../../mrmac/mrmac.h"
#include "../../mrmac/vm/MRVMValue.hpp"

#include <mutex>
#include <string>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace mr::window_commands {
namespace {
constexpr const char *kApplicationUiRoot = "APPLICATIONUI";
}

VirtualMachine::Value applicationUiBranch(MRVMRuntimeKv &runtimeKv, const char *branch) {
	VirtualMachine::Value applicationUi = runtimeKv.ensureRoot(kApplicationUiRoot);
	return runtimeKv.ensureChild(applicationUi, branch);
}

int applicationUiInt(MRVMRuntimeKv &runtimeKv, const char *branch, const char *key, int fallback) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMHashStore &store = runtimeKv.globalStore();
	VirtualMachine::Value parent = applicationUiBranch(runtimeKv, branch);

	if (!mrvmHashContainsValue(store, store, parent, key)) return fallback;
	VirtualMachine::Value stored = mrvmHashReadValue(store, store, parent, key);
	return stored.type == TYPE_INT ? stored.i : fallback;
}

void storeApplicationUiInt(MRVMRuntimeKv &runtimeKv, const char *branch, const char *key, int value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMHashStore &store = runtimeKv.globalStore();
	VirtualMachine::Value parent = applicationUiBranch(runtimeKv, branch);
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeInt(value));
}

std::uint64_t applicationUiUnsigned(MRVMRuntimeKv &runtimeKv, const char *branch, const char *key, std::uint64_t fallback) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMHashStore &store = runtimeKv.globalStore();
	VirtualMachine::Value parent = applicationUiBranch(runtimeKv, branch);

	if (!mrvmHashContainsValue(store, store, parent, key)) return fallback;
	VirtualMachine::Value stored = mrvmHashReadValue(store, store, parent, key);
	if (stored.type != TYPE_STR) return fallback;
	try {
		std::size_t consumed = 0;
		const unsigned long long parsed = std::stoull(stored.s, &consumed, 10);
		return consumed == stored.s.size() ? static_cast<std::uint64_t>(parsed) : fallback;
	} catch (...) {
		return fallback;
	}
}

void storeApplicationUiUnsigned(MRVMRuntimeKv &runtimeKv, const char *branch, const char *key, std::uint64_t value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMHashStore &store = runtimeKv.globalStore();
	VirtualMachine::Value parent = applicationUiBranch(runtimeKv, branch);
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(std::to_string(value)));
}

std::string applicationUiString(MRVMRuntimeKv &runtimeKv, const char *branch, const char *key) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMHashStore &store = runtimeKv.globalStore();
	VirtualMachine::Value parent = applicationUiBranch(runtimeKv, branch);

	if (!mrvmHashContainsValue(store, store, parent, key)) return std::string();
	VirtualMachine::Value stored = mrvmHashReadValue(store, store, parent, key);
	return stored.type == TYPE_STR ? stored.s : std::string();
}

void storeApplicationUiString(MRVMRuntimeKv &runtimeKv, const char *branch, const char *key, const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMHashStore &store = runtimeKv.globalStore();
	VirtualMachine::Value parent = applicationUiBranch(runtimeKv, branch);
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(value));
}

std::uint64_t steadyClockMilliseconds(std::chrono::steady_clock::time_point value) {
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count());
}

int normalizedVirtualDesktopCount(int count) {
	if (count < 1) return 1;
	if (count > 9) return 9;
	return count;
}

} // namespace mr::window_commands
