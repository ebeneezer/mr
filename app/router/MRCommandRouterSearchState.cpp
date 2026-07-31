#include "MRCommandRouterSearchState.hpp"

#include "../../mrmac/MRVM.hpp"
#include "../../mrmac/mrmac.h"
#include "../../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../../mrmac/vm/MRVMValue.hpp"

#include <mutex>
#include <string>
#include <vector>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace mr::search_runtime {
namespace {

constexpr const char *kApplicationUiRoot = "APPLICATIONUI";
constexpr const char *kSearchBranch = "search";

VirtualMachine::Value searchStateBranch(MRVMRuntimeKv &runtimeKv, const char *branch) {
	VirtualMachine::Value applicationUi = runtimeKv.ensureRoot(kApplicationUiRoot);
	VirtualMachine::Value search = runtimeKv.ensureChild(applicationUi, kSearchBranch);
	return runtimeKv.ensureChild(search, branch);
}

int readSearchInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, int fallback) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, parent, key)) return fallback;
	VirtualMachine::Value stored = mrvmHashReadValue(store, store, parent, key);
	return stored.type == TYPE_INT ? stored.i : fallback;
}

std::size_t readSearchSize(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, parent, key)) return 0;
	VirtualMachine::Value stored = mrvmHashReadValue(store, store, parent, key);
	if (stored.type != TYPE_STR) return 0;
	try {
		std::size_t consumed = 0;
		const unsigned long long parsed = std::stoull(stored.s, &consumed, 10);
		return consumed == stored.s.size() ? static_cast<std::size_t>(parsed) : 0;
	} catch (...) {
		return 0;
	}
}

std::string readSearchString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, parent, key)) return std::string();
	VirtualMachine::Value stored = mrvmHashReadValue(store, store, parent, key);
	return stored.type == TYPE_STR ? stored.s : std::string();
}

void writeSearchInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeInt(value));
}

void writeSearchSize(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, std::size_t value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(std::to_string(value)));
}

void writeSearchString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(value));
}

void readSearchOptions(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, MRSearchDialogOptions &options) {
	options.textType = static_cast<MRSearchTextType>(readSearchInt(runtimeKv, parent, "textType", static_cast<int>(MRSearchTextType::Literal)));
	options.direction = static_cast<MRSearchDirection>(readSearchInt(runtimeKv, parent, "direction", static_cast<int>(MRSearchDirection::Forward)));
	options.mode = static_cast<MRSearchMode>(readSearchInt(runtimeKv, parent, "mode", static_cast<int>(MRSearchMode::StopFirst)));
	options.caseSensitive = readSearchInt(runtimeKv, parent, "caseSensitive", 0) != 0;
	options.globalSearch = readSearchInt(runtimeKv, parent, "globalSearch", 1) != 0;
	options.restrictToMarkedBlock = readSearchInt(runtimeKv, parent, "restrictToMarkedBlock", 0) != 0;
	options.searchAllWindows = readSearchInt(runtimeKv, parent, "searchAllWindows", 0) != 0;
}

void writeSearchOptions(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const MRSearchDialogOptions &options) {
	writeSearchInt(runtimeKv, parent, "textType", static_cast<int>(options.textType));
	writeSearchInt(runtimeKv, parent, "direction", static_cast<int>(options.direction));
	writeSearchInt(runtimeKv, parent, "mode", static_cast<int>(options.mode));
	writeSearchInt(runtimeKv, parent, "caseSensitive", options.caseSensitive ? 1 : 0);
	writeSearchInt(runtimeKv, parent, "globalSearch", options.globalSearch ? 1 : 0);
	writeSearchInt(runtimeKv, parent, "restrictToMarkedBlock", options.restrictToMarkedBlock ? 1 : 0);
	writeSearchInt(runtimeKv, parent, "searchAllWindows", options.searchAllWindows ? 1 : 0);
}

} // namespace

SearchUiState searchUiState() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value state = searchStateBranch(runtimeKv, "dialog");
	SearchUiState value;

	value.hasPrevious = readSearchInt(runtimeKv, state, "hasPrevious", 0) != 0;
	value.hasAcceptedPattern = readSearchInt(runtimeKv, state, "hasAcceptedPattern", 0) != 0;
	value.pattern = readSearchString(runtimeKv, state, "pattern");
	value.acceptedPattern = readSearchString(runtimeKv, state, "acceptedPattern");
	value.replacement = readSearchString(runtimeKv, state, "replacement");
	value.lastStart = readSearchSize(runtimeKv, state, "lastStart");
	value.lastEnd = readSearchSize(runtimeKv, state, "lastEnd");
	VirtualMachine::Value accepted = runtimeKv.ensureChild(state, "acceptedOptions");
	VirtualMachine::Value last = runtimeKv.ensureChild(state, "lastOptions");
	readSearchOptions(runtimeKv, accepted, value.acceptedOptions);
	readSearchOptions(runtimeKv, last, value.lastOptions);
	return value;
}

void storeSearchUiState(const SearchUiState &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value state = searchStateBranch(runtimeKv, "dialog");

	writeSearchInt(runtimeKv, state, "hasPrevious", value.hasPrevious ? 1 : 0);
	writeSearchInt(runtimeKv, state, "hasAcceptedPattern", value.hasAcceptedPattern ? 1 : 0);
	writeSearchString(runtimeKv, state, "pattern", value.pattern);
	writeSearchString(runtimeKv, state, "acceptedPattern", value.acceptedPattern);
	writeSearchString(runtimeKv, state, "replacement", value.replacement);
	writeSearchSize(runtimeKv, state, "lastStart", value.lastStart);
	writeSearchSize(runtimeKv, state, "lastEnd", value.lastEnd);
	writeSearchOptions(runtimeKv, runtimeKv.ensureChild(state, "acceptedOptions"), value.acceptedOptions);
	writeSearchOptions(runtimeKv, runtimeKv.ensureChild(state, "lastOptions"), value.lastOptions);
}

SearchResultsContext searchResultsContext() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value state = searchStateBranch(runtimeKv, "results");
	SearchResultsContext value;

	value.kind = static_cast<SearchResultsContextKind>(readSearchInt(runtimeKv, state, "kind", static_cast<int>(SearchResultsContextKind::None)));
	value.bufferId = readSearchInt(runtimeKv, state, "bufferId", 0);
	return value;
}

void storeSearchResultsContext(const SearchResultsContext &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value state = searchStateBranch(runtimeKv, "results");
	writeSearchInt(runtimeKv, state, "kind", static_cast<int>(value.kind));
	writeSearchInt(runtimeKv, state, "bufferId", value.bufferId);
}

PendingTransientSelectionClear pendingTransientSelectionClear() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value state = searchStateBranch(runtimeKv, "pendingSelectionClear");
	PendingTransientSelectionClear value;

	value.active = readSearchInt(runtimeKv, state, "active", 0) != 0;
	value.normalizedPath = readSearchString(runtimeKv, state, "normalizedPath");
	value.start = readSearchSize(runtimeKv, state, "start");
	value.end = readSearchSize(runtimeKv, state, "end");
	return value;
}

void storePendingTransientSelectionClear(const PendingTransientSelectionClear &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value state = searchStateBranch(runtimeKv, "pendingSelectionClear");
	writeSearchInt(runtimeKv, state, "active", value.active ? 1 : 0);
	writeSearchString(runtimeKv, state, "normalizedPath", value.normalizedPath);
	writeSearchSize(runtimeKv, state, "start", value.start);
	writeSearchSize(runtimeKv, state, "end", value.end);
}

std::string normalizedSearchPath(const std::filesystem::path &path) {
	std::error_code ec;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);

	if (ec || normalized.empty()) {
		ec.clear();
		normalized = std::filesystem::absolute(path, ec);
	}
	if (ec || normalized.empty()) normalized = path.lexically_normal();
	std::string result = normalized.lexically_normal().string();
	for (char &ch : result)
		if (ch == '\\') ch = '/';
	return result;
}

} // namespace mr::search_runtime
