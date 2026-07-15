#include "../../vm/MRVMRuntimeCatalog.hpp"
#include "../../vm/MRVMRuntimeGlobals.hpp"
#include "../../vm/MRVMRuntimeState.hpp"
#include "../../vm/MRVMValue.hpp"

#include "../../mrmac.h"

#include <set>

std::vector<std::size_t> mrvmUiCopyWindowMarkStack(const void *windowKey) {
	std::vector<std::size_t> out;
	std::map<const void *, std::vector<unsigned int>>::const_iterator it;

	if (windowKey == nullptr) return out;
	it = g_runtimeEnv.markStacks.find(windowKey);
	if (it == g_runtimeEnv.markStacks.end()) return out;
	out.reserve(it->second.size());
	for (unsigned int i : it->second)
		out.push_back(static_cast<std::size_t>(i));
	return out;
}

bool mrvmUiCopyWindowLastSearch(const void *windowKey, const std::string &fileName, std::size_t &start, std::size_t &end, std::size_t &cursor) {
	start = 0;
	end = 0;
	cursor = 0;
	if (!g_runtimeEnv.lastSearchValid || windowKey == nullptr) return false;
	if (g_runtimeEnv.lastSearchWindow != windowKey) return false;
	if (g_runtimeEnv.lastSearchFileName != fileName) return false;
	start = g_runtimeEnv.lastSearchStart;
	end = g_runtimeEnv.lastSearchEnd;
	cursor = g_runtimeEnv.lastSearchCursor;
	return true;
}

void mrvmUiCopyGlobals(std::vector<std::string> &order, std::map<std::string, int> &ints, std::map<std::string, std::string> &strings) {
	const std::vector<std::string> orderValues = mrvmRuntimeGlobalOrderValues(g_runtimeEnv.runtimeKv);

	order.clear();
	ints.clear();
	strings.clear();
	order.reserve(orderValues.size());
	for (std::size_t i = 0; i < orderValues.size(); ++i) {
		const std::string &key = orderValues[i];
		GlobalEntry entry;
		if (!mrvmRuntimeGlobalRead(g_runtimeEnv.runtimeKv, key, entry)) continue;
		order.push_back(key);
		if (entry.type == TYPE_INT) ints[key] = mrvmValueAsInt(entry.value);
		else if (entry.type == TYPE_STR)
			strings[key] = mrvmValueAsString(entry.value);
	}
}

void mrvmUiCopyLoadedMacros(std::vector<std::string> &order, std::map<std::string, std::string> &displayNames) {
	const std::vector<std::string> orderValues = mrvmRuntimeCatalogMacroOrder(g_runtimeEnv.runtimeKv);

	order.clear();
	displayNames.clear();
	order.reserve(orderValues.size());
	for (std::size_t i = 0; i < orderValues.size(); ++i) {
		const std::string &key = orderValues[i];
		MacroRef macroRef;
		if (!mrvmRuntimeCatalogReadLoadedMacro(g_runtimeEnv.runtimeKv, key, macroRef)) continue;
		order.push_back(key);
		displayNames[key] = macroRef.displayName;
	}
}

void mrvmUiCopyRuntimeOptions(bool &ignoreCase, bool &tabExpand) {
	ignoreCase = g_runtimeEnv.ignoreCase;
	tabExpand = g_runtimeEnv.tabExpand;
}

void mrvmUiReplaceWindowMarkStack(const void *windowKey, const std::vector<std::size_t> &offsets) {
	std::vector<unsigned int> marks;

	if (windowKey == nullptr) return;
	if (offsets.empty()) {
		g_runtimeEnv.markStacks.erase(windowKey);
		return;
	}
	marks.reserve(offsets.size());
	for (unsigned long offset : offsets)
		marks.push_back(static_cast<unsigned int>(offset));
	g_runtimeEnv.markStacks[windowKey] = marks;
}

void mrvmUiReplaceWindowLastSearch(const void *windowKey, const std::string &fileName, bool valid, std::size_t start, std::size_t end, std::size_t cursor) {
	if (!valid) {
		if (g_runtimeEnv.lastSearchWindow == windowKey) {
			g_runtimeEnv.lastSearchValid = false;
			g_runtimeEnv.lastSearchWindow = nullptr;
			g_runtimeEnv.lastSearchFileName.clear();
			g_runtimeEnv.lastSearchStart = 0;
			g_runtimeEnv.lastSearchEnd = 0;
			g_runtimeEnv.lastSearchCursor = 0;
		}
		return;
	}
	g_runtimeEnv.lastSearchValid = true;
	g_runtimeEnv.lastSearchWindow = windowKey;
	g_runtimeEnv.lastSearchFileName = fileName;
	g_runtimeEnv.lastSearchStart = start;
	g_runtimeEnv.lastSearchEnd = end;
	g_runtimeEnv.lastSearchCursor = cursor;
}

void mrvmUiReplaceGlobals(const std::vector<std::string> &order, const std::map<std::string, int> &ints, const std::map<std::string, std::string> &strings) {
	std::map<std::string, GlobalEntry> preservedHashes;
	std::vector<std::string> preservedHashOrder;
	std::set<std::string> finalKeys;
	std::set<std::string> seen;
	const std::vector<std::string> oldOrder = mrvmRuntimeGlobalOrderValues(g_runtimeEnv.runtimeKv);

	for (const std::string &key : oldOrder) {
		GlobalEntry entry;
		if (!mrvmRuntimeGlobalRead(g_runtimeEnv.runtimeKv, key, entry) || entry.type != TYPE_HASH) continue;
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
		if (finalKeys.find(key) == finalKeys.end()) static_cast<void>(mrvmRuntimeGlobalErase(g_runtimeEnv.runtimeKv, key));
	}
	mrvmRuntimeGlobalClearOrderAndEnumeration(g_runtimeEnv.runtimeKv);

	for (const auto &i : order) {
		const std::string key = mrvmUpperKey(i);
		std::map<std::string, int>::const_iterator intIt;
		std::map<std::string, std::string>::const_iterator strIt;
		if (!seen.insert(key).second) continue;
		intIt = ints.find(key);
		if (intIt != ints.end()) {
			mrvmRuntimeGlobalWrite(g_runtimeEnv.runtimeKv, key, TYPE_INT, mrvmMakeInt(intIt->second));
		} else {
			strIt = strings.find(key);
			if (strIt == strings.end()) continue;
			mrvmRuntimeGlobalWrite(g_runtimeEnv.runtimeKv, key, TYPE_STR, mrvmMakeString(strIt->second));
		}
	}

	for (const auto &i : ints) {
		const std::string key = mrvmUpperKey(i.first);
		if (!seen.insert(key).second) continue;
		mrvmRuntimeGlobalWrite(g_runtimeEnv.runtimeKv, key, TYPE_INT, mrvmMakeInt(i.second));
	}
	for (const auto &string : strings) {
		const std::string key = mrvmUpperKey(string.first);
		if (!seen.insert(key).second) continue;
		mrvmRuntimeGlobalWrite(g_runtimeEnv.runtimeKv, key, TYPE_STR, mrvmMakeString(string.second));
	}

	for (const std::string &key : preservedHashOrder) {
		std::map<std::string, GlobalEntry>::const_iterator hashIt;
		if (!seen.insert(key).second) continue;
		hashIt = preservedHashes.find(key);
		if (hashIt == preservedHashes.end()) continue;
		mrvmRuntimeGlobalWrite(g_runtimeEnv.runtimeKv, key, hashIt->second.type, hashIt->second.value);
	}
}

void mrvmUiReplaceRuntimeOptions(bool ignoreCase, bool tabExpand) {
	g_runtimeEnv.ignoreCase = ignoreCase;
	g_runtimeEnv.tabExpand = tabExpand;
}
