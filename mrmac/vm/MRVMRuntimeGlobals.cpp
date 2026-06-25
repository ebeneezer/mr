#include "MRVMRuntimeGlobals.hpp"

#include "MRVMHash.hpp"

#include "../mrmac.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <limits>

namespace {
using Value = VirtualMachine::Value;

std::string upperKey(std::string value) {
	for (char &c : value)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return value;
}

Value makeIntValue(int value) {
	Value result;
	result.type = TYPE_INT;
	result.i = value;
	return result;
}

Value makeStringValue(const std::string &value) {
	Value result;
	result.type = TYPE_STR;
	result.s = value;
	return result;
}

bool parseUint64Text(const std::string &text, std::uint64_t &value) {
	value = 0;
	if (text.empty()) return false;
	for (std::size_t index = 0; index < text.size(); ++index) {
		const unsigned char ch = static_cast<unsigned char>(text[index]);
		std::uint64_t digit;
		if (std::isdigit(ch) == 0) return false;
		digit = static_cast<std::uint64_t>(ch - static_cast<unsigned char>('0'));
		if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
		value = value * 10 + digit;
	}
	return true;
}

int valueAsInt(const Value &value, int fallback) {
	std::uint64_t parsed = 0;

	if (value.type == TYPE_INT) return value.i;
	if (value.type == TYPE_STR && parseUint64Text(value.s, parsed)) return static_cast<int>(parsed);
	return fallback;
}

std::uint64_t valueAsUint64(const Value &value, std::uint64_t fallback) {
	std::uint64_t parsed = 0;

	if (value.type == TYPE_INT) return value.i > 0 ? static_cast<std::uint64_t>(value.i) : 0;
	if (value.type == TYPE_STR && parseUint64Text(value.s, parsed)) return parsed;
	return fallback;
}

std::string valueAsString(const Value &value) {
	if (value.type == TYPE_STR) return value.s;
	if (value.type == TYPE_INT) return std::to_string(value.i);
	return std::string();
}

Value ensureMacroGlobalsHash(MRVMRuntimeKv &runtimeKv) {
	return runtimeKv.ensureRoot("MACROGLOBALS");
}

bool findMacroGlobalsHash(MRVMRuntimeKv &runtimeKv, Value &root) {
	return runtimeKv.findRoot("MACROGLOBALS", root);
}

Value ensureMacroGlobalsChildPath(MRVMRuntimeKv &runtimeKv, std::initializer_list<const char *> keys) {
	Value current = ensureMacroGlobalsHash(runtimeKv);

	for (const char *key : keys)
		current = runtimeKv.ensureChild(current, key != nullptr ? key : "");
	return current;
}

bool findMacroGlobalsChildPath(MRVMRuntimeKv &runtimeKv, std::initializer_list<const char *> keys, Value &child) {
	Value current;

	if (!findMacroGlobalsHash(runtimeKv, current)) return false;
	for (const char *key : keys) {
		if (!runtimeKv.findChild(current, key != nullptr ? key : "", child)) return false;
		current = child;
	}
	return true;
}

void hashWriteInt(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	mrvmHashWriteValue(store, store, hash, key, makeIntValue(value));
}

void hashWriteUint(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, std::uint64_t value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	mrvmHashWriteValue(store, store, hash, key, makeStringValue(std::to_string(value)));
}

void hashWriteString(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	mrvmHashWriteValue(store, store, hash, key, makeStringValue(value));
}

int hashReadInt(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, int fallback) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, hash, key)) return fallback;
	return valueAsInt(mrvmHashReadValue(store, store, hash, key), fallback);
}

std::uint64_t hashReadUint(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, std::uint64_t fallback) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, hash, key)) return fallback;
	return valueAsUint64(mrvmHashReadValue(store, store, hash, key), fallback);
}

std::string hashReadString(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, hash, key)) return std::string();
	return valueAsString(mrvmHashReadValue(store, store, hash, key));
}

void writeGlobalEntryHash(MRVMRuntimeKv &runtimeKv, const Value &hash, int type, const Value &value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	hashWriteInt(runtimeKv, hash, "type", type);
	mrvmHashWriteValue(store, store, hash, "value", value);
}

bool readGlobalEntryHash(MRVMRuntimeKv &runtimeKv, const Value &hash, MRVMRuntimeGlobalEntry &entry) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, hash, "type")) return false;
	if (!mrvmHashContainsValue(store, store, hash, "value")) return false;
	entry.type = hashReadInt(runtimeKv, hash, "type", TYPE_INT);
	entry.value = mrvmHashReadValue(store, store, hash, "value");
	return true;
}

bool findRuntimeGlobalEntryHash(MRVMRuntimeKv &runtimeKv, const std::string &key, Value &entryHash) {
	Value byName;

	if (!findMacroGlobalsChildPath(runtimeKv, {"runtime", "byName"}, byName)) return false;
	return runtimeKv.findChild(byName, key, entryHash);
}

bool macroGlobalOrderContains(MRVMRuntimeKv &runtimeKv, const std::string &key) {
	const std::vector<std::string> values = mrvmRuntimeGlobalOrderValues(runtimeKv);

	return std::find(values.begin(), values.end(), key) != values.end();
}

void appendMacroGlobalOrderKeyIfMissing(MRVMRuntimeKv &runtimeKv, const std::string &key) {
	if (macroGlobalOrderContains(runtimeKv, key)) return;

	Value order = ensureMacroGlobalsChildPath(runtimeKv, {"runtime", "order"});
	const int count = hashReadInt(runtimeKv, order, "count", 0) + 1;
	hashWriteInt(runtimeKv, order, "count", count);
	hashWriteString(runtimeKv, order, std::to_string(count), key);
}
} // namespace

std::vector<std::string> mrvmRuntimeGlobalOrderValues(MRVMRuntimeKv &runtimeKv) {
	std::vector<std::string> values;
	Value order;
	const int count = findMacroGlobalsChildPath(runtimeKv, {"runtime", "order"}, order) ? hashReadInt(runtimeKv, order, "count", 0) : 0;

	values.reserve(count > 0 ? static_cast<std::size_t>(count) : 0);
	for (int index = 1; index <= count; ++index) {
		std::string key = hashReadString(runtimeKv, order, std::to_string(index));
		if (!key.empty()) values.push_back(key);
	}
	return values;
}

std::size_t mrvmRuntimeGlobalEnumIndex(MRVMRuntimeKv &runtimeKv) {
	Value enumeration;

	if (!findMacroGlobalsChildPath(runtimeKv, {"runtime", "enumeration"}, enumeration)) return 0;
	return static_cast<std::size_t>(hashReadUint(runtimeKv, enumeration, "globalIndex", 0));
}

void mrvmRuntimeGlobalSetEnumIndex(MRVMRuntimeKv &runtimeKv, std::size_t index) {
	hashWriteUint(runtimeKv, ensureMacroGlobalsChildPath(runtimeKv, {"runtime", "enumeration"}), "globalIndex", index);
}

bool mrvmRuntimeGlobalRead(MRVMRuntimeKv &runtimeKv, const std::string &name, MRVMRuntimeGlobalEntry &entry) {
	Value entryHash;
	const std::string key = upperKey(name);

	if (!findRuntimeGlobalEntryHash(runtimeKv, key, entryHash)) return false;
	return readGlobalEntryHash(runtimeKv, entryHash, entry);
}

void mrvmRuntimeGlobalWrite(MRVMRuntimeKv &runtimeKv, const std::string &name, int type, const Value &value) {
	Value byName = ensureMacroGlobalsChildPath(runtimeKv, {"runtime", "byName"});
	Value entryHash = runtimeKv.ensureChild(byName, upperKey(name));

	writeGlobalEntryHash(runtimeKv, entryHash, type, value);
	appendMacroGlobalOrderKeyIfMissing(runtimeKv, upperKey(name));
}

bool mrvmRuntimeGlobalErase(MRVMRuntimeKv &runtimeKv, const std::string &name) {
	Value byName;

	if (!findMacroGlobalsChildPath(runtimeKv, {"runtime", "byName"}, byName)) return false;
	return runtimeKv.eraseChild(byName, upperKey(name));
}

void mrvmRuntimeGlobalClearOrderAndEnumeration(MRVMRuntimeKv &runtimeKv) {
	Value runtime = ensureMacroGlobalsChildPath(runtimeKv, {"runtime"});

	static_cast<void>(runtimeKv.eraseChild(runtime, "order"));
	static_cast<void>(runtimeKv.eraseChild(runtime, "enumeration"));
}
