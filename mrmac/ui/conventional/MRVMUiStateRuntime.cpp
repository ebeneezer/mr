#include "../../vm/MRVMRuntimeCatalog.hpp"
#include "../../vm/MRVMRuntimeGlobals.hpp"
#include "../../vm/MRVMRuntimeState.hpp"
#include "../../vm/MRVMValue.hpp"

#include "../../mrmac.h"
#include "../../../ui/MREditWindow.hpp"

#include <cstdlib>
#include <set>

namespace {

int runtimeWindowBufferId(const void *windowKey) {
	const MREditWindow *window = static_cast<const MREditWindow *>(windowKey);
	return window != nullptr ? window->bufferId() : 0;
}

std::string runtimeWindowStateKey(const void *windowKey) {
	const int bufferId = runtimeWindowBufferId(windowKey);
	return bufferId > 0 ? std::to_string(bufferId) : std::string();
}

} // namespace

std::vector<std::size_t> mrvmUiCopyWindowMarkStack(const void *windowKey) {
	std::vector<std::size_t> out;
	const std::string key = runtimeWindowStateKey(windowKey);
	const std::vector<std::string> stored = mrvmRuntimeStateStringList("markStacks", key);

	if (key.empty()) return out;
	out.reserve(stored.size());
	for (const std::string &value : stored) {
		char *end = nullptr;
		const unsigned long long offset = std::strtoull(value.c_str(), &end, 10);
		if (end != value.c_str() && *end == '\0') out.push_back(static_cast<std::size_t>(offset));
	}
	return out;
}

bool mrvmUiCopyWindowLastSearch(const void *windowKey, const std::string &fileName, std::size_t &start, std::size_t &end, std::size_t &cursor) {
	const int bufferId = runtimeWindowBufferId(windowKey);

	start = 0;
	end = 0;
	cursor = 0;
	if (mrvmRuntimeStateInt("lastSearch", "valid") == 0 || bufferId <= 0) return false;
	if (mrvmRuntimeStateInt("lastSearch", "bufferId") != bufferId) return false;
	if (mrvmRuntimeStateString("lastSearch", "fileName") != fileName) return false;
	start = mrvmRuntimeStateSize("lastSearch", "start");
	end = mrvmRuntimeStateSize("lastSearch", "end");
	cursor = mrvmRuntimeStateSize("lastSearch", "cursor");
	return true;
}

void mrvmUiCopyGlobals(std::vector<std::string> &order, std::map<std::string, int> &ints, std::map<std::string, std::string> &strings) {
	const std::vector<std::string> orderValues = mrvmRuntimeGlobalOrderValues(mrvmRuntimeKv());

	order.clear();
	ints.clear();
	strings.clear();
	order.reserve(orderValues.size());
	for (std::size_t i = 0; i < orderValues.size(); ++i) {
		const std::string &key = orderValues[i];
		GlobalEntry entry;
		if (!mrvmRuntimeGlobalRead(mrvmRuntimeKv(), key, entry)) continue;
		order.push_back(key);
		if (entry.type == TYPE_INT) ints[key] = mrvmValueAsInt(entry.value);
		else if (entry.type == TYPE_STR)
			strings[key] = mrvmValueAsString(entry.value);
	}
}

void mrvmUiCopyLoadedMacros(std::vector<std::string> &order, std::map<std::string, std::string> &displayNames) {
	const std::vector<std::string> orderValues = mrvmRuntimeCatalogMacroOrder(mrvmRuntimeKv());

	order.clear();
	displayNames.clear();
	order.reserve(orderValues.size());
	for (std::size_t i = 0; i < orderValues.size(); ++i) {
		const std::string &key = orderValues[i];
		MacroRef macroRef;
		if (!mrvmRuntimeCatalogReadLoadedMacro(mrvmRuntimeKv(), key, macroRef)) continue;
		order.push_back(key);
		displayNames[key] = macroRef.displayName;
	}
}

void mrvmUiCopyRuntimeOptions(bool &ignoreCase, bool &tabExpand) {
	ignoreCase = mrvmRuntimeStateInt("options", "ignoreCase") != 0;
	tabExpand = mrvmRuntimeStateInt("options", "tabExpand", 1) != 0;
}

void mrvmUiReplaceWindowMarkStack(const void *windowKey, const std::vector<std::size_t> &offsets) {
	const std::string key = runtimeWindowStateKey(windowKey);
	std::vector<std::string> marks;

	if (key.empty()) return;
	if (offsets.empty()) {
		static_cast<void>(mrvmEraseRuntimeStateValue("markStacks", key));
		return;
	}
	marks.reserve(offsets.size());
	for (std::size_t offset : offsets)
		marks.push_back(std::to_string(offset));
	mrvmStoreRuntimeStateStringList("markStacks", key, marks);
}

void mrvmUiReplaceWindowLastSearch(const void *windowKey, const std::string &fileName, bool valid, std::size_t start, std::size_t end, std::size_t cursor) {
	const int bufferId = runtimeWindowBufferId(windowKey);

	if (!valid) {
		if (mrvmRuntimeStateInt("lastSearch", "bufferId") == bufferId) mrvmClearRuntimeStateBranch("lastSearch");
		return;
	}
	mrvmStoreRuntimeStateInt("lastSearch", "valid", 1);
	mrvmStoreRuntimeStateInt("lastSearch", "bufferId", bufferId);
	mrvmStoreRuntimeStateString("lastSearch", "fileName", fileName);
	mrvmStoreRuntimeStateSize("lastSearch", "start", start);
	mrvmStoreRuntimeStateSize("lastSearch", "end", end);
	mrvmStoreRuntimeStateSize("lastSearch", "cursor", cursor);
}

void mrvmUiReplaceGlobals(const std::vector<std::string> &order, const std::map<std::string, int> &ints, const std::map<std::string, std::string> &strings) {
	std::map<std::string, GlobalEntry> preservedHashes;
	std::vector<std::string> preservedHashOrder;
	std::set<std::string> finalKeys;
	std::set<std::string> seen;
	const std::vector<std::string> oldOrder = mrvmRuntimeGlobalOrderValues(mrvmRuntimeKv());

	for (const std::string &key : oldOrder) {
		GlobalEntry entry;
		if (!mrvmRuntimeGlobalRead(mrvmRuntimeKv(), key, entry) || entry.type != TYPE_HASH) continue;
		if (ints.find(key) != ints.end() || strings.find(key) != strings.end()) continue;
		preservedHashOrder.push_back(key);
		preservedHashes[key] = entry;
		finalKeys.insert(key);
	}

	for (const auto &i : order) {
		const std::string key = mrvmUpperKey(i);
		if (ints.find(key) != ints.end() || strings.find(key) != strings.end()) finalKeys.insert(key);
	}
	for (const auto &i : ints)
		finalKeys.insert(mrvmUpperKey(i.first));
	for (const auto &string : strings)
		finalKeys.insert(mrvmUpperKey(string.first));

	for (const std::string &key : oldOrder) {
		if (finalKeys.find(key) == finalKeys.end()) static_cast<void>(mrvmRuntimeGlobalErase(mrvmRuntimeKv(), key));
	}
	mrvmRuntimeGlobalClearOrderAndEnumeration(mrvmRuntimeKv());

	for (const auto &i : order) {
		const std::string key = mrvmUpperKey(i);
		std::map<std::string, int>::const_iterator intIt;
		std::map<std::string, std::string>::const_iterator strIt;
		if (!seen.insert(key).second) continue;
		intIt = ints.find(key);
		if (intIt != ints.end()) {
			mrvmRuntimeGlobalWrite(mrvmRuntimeKv(), key, TYPE_INT, mrvmMakeInt(intIt->second));
		} else {
			strIt = strings.find(key);
			if (strIt == strings.end()) continue;
			mrvmRuntimeGlobalWrite(mrvmRuntimeKv(), key, TYPE_STR, mrvmMakeString(strIt->second));
		}
	}

	for (const auto &i : ints) {
		const std::string key = mrvmUpperKey(i.first);
		if (!seen.insert(key).second) continue;
		mrvmRuntimeGlobalWrite(mrvmRuntimeKv(), key, TYPE_INT, mrvmMakeInt(i.second));
	}
	for (const auto &string : strings) {
		const std::string key = mrvmUpperKey(string.first);
		if (!seen.insert(key).second) continue;
		mrvmRuntimeGlobalWrite(mrvmRuntimeKv(), key, TYPE_STR, mrvmMakeString(string.second));
	}

	for (const std::string &key : preservedHashOrder) {
		std::map<std::string, GlobalEntry>::const_iterator hashIt;
		if (!seen.insert(key).second) continue;
		hashIt = preservedHashes.find(key);
		if (hashIt == preservedHashes.end()) continue;
		mrvmRuntimeGlobalWrite(mrvmRuntimeKv(), key, hashIt->second.type, hashIt->second.value);
	}
}

void mrvmUiReplaceRuntimeOptions(bool ignoreCase, bool tabExpand) {
	mrvmStoreRuntimeStateInt("options", "ignoreCase", ignoreCase ? 1 : 0);
	mrvmStoreRuntimeStateInt("options", "tabExpand", tabExpand ? 1 : 0);
}
