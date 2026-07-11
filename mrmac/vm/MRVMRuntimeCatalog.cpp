#include "MRVMRuntimeCatalog.hpp"

#include "MRVMHash.hpp"
#include "MRVMKeymapRuntime.hpp"
#include "MRVMMacroSpecRuntime.hpp"
#include "MRVMValue.hpp"

#include "../mrmac.h"
#include "../MRVM.hpp"
#include "../../app/utils/MRFileIOUtils.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <set>
#include <utility>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace {
using Value = VirtualMachine::Value;

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
		if (ch < static_cast<unsigned char>('0') || ch > static_cast<unsigned char>('9')) return false;
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

Value ensureCatalogHash(MRVMRuntimeKv &runtimeKv) {
	return runtimeKv.ensureRoot("MACROCATALOG");
}

bool findCatalogHash(MRVMRuntimeKv &runtimeKv, Value &root) {
	return runtimeKv.findRoot("MACROCATALOG", root);
}

Value ensureCatalogChildPath(MRVMRuntimeKv &runtimeKv, std::initializer_list<const char *> keys) {
	Value current = ensureCatalogHash(runtimeKv);

	for (const char *key : keys)
		current = runtimeKv.ensureChild(current, key != nullptr ? key : "");
	return current;
}

bool findCatalogChildPath(MRVMRuntimeKv &runtimeKv, std::initializer_list<const char *> keys, Value &child) {
	Value current;

	if (!findCatalogHash(runtimeKv, current)) return false;
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

std::string bytecodeToString(const std::vector<unsigned char> &bytecode) {
	if (bytecode.empty()) return std::string();
	return std::string(reinterpret_cast<const char *>(bytecode.data()), bytecode.size());
}

std::vector<unsigned char> stringToBytecode(const std::string &value) {
	return std::vector<unsigned char>(value.begin(), value.end());
}

void writeStringVectorHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::vector<std::string> &values) {
	hashWriteInt(runtimeKv, hash, "count", static_cast<int>(values.size()));
	for (std::size_t index = 0; index < values.size(); ++index)
		hashWriteString(runtimeKv, hash, std::to_string(index + 1), values[index]);
}

std::vector<std::string> readStringVectorHash(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	std::vector<std::string> values;
	const int count = hashReadInt(runtimeKv, hash, "count", 0);

	for (int index = 1; index <= count; ++index)
		values.push_back(hashReadString(runtimeKv, hash, std::to_string(index)));
	return values;
}

std::string lineKey(int line) {
	return std::to_string(line);
}

void writeSourceMapEntryHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroSourceMapEntry &entry) {
	hashWriteUint(runtimeKv, hash, "bytecodeOffset", entry.bytecodeOffset);
	hashWriteUint(runtimeKv, hash, "sourceStartOffset", entry.sourceStartOffset);
	hashWriteUint(runtimeKv, hash, "sourceEndOffset", entry.sourceEndOffset);
	hashWriteInt(runtimeKv, hash, "line", entry.line);
	hashWriteInt(runtimeKv, hash, "column", entry.column);
	hashWriteString(runtimeKv, hash, "macroName", entry.macroName);
	hashWriteInt(runtimeKv, hash, "debuggableKind", entry.debuggableKind);
}

MRMacroSourceMapEntry readSourceMapEntryHash(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	MRMacroSourceMapEntry entry;

	entry.bytecodeOffset = static_cast<std::size_t>(hashReadUint(runtimeKv, hash, "bytecodeOffset", 0));
	entry.sourceStartOffset = static_cast<std::size_t>(hashReadUint(runtimeKv, hash, "sourceStartOffset", 0));
	entry.sourceEndOffset = static_cast<std::size_t>(hashReadUint(runtimeKv, hash, "sourceEndOffset", entry.sourceStartOffset));
	entry.line = hashReadInt(runtimeKv, hash, "line", 0);
	entry.column = hashReadInt(runtimeKv, hash, "column", 0);
	entry.macroName = hashReadString(runtimeKv, hash, "macroName");
	entry.debuggableKind = hashReadInt(runtimeKv, hash, "debuggableKind", 0);
	return entry;
}

void writeSourceMapHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::vector<MRMacroSourceMapEntry> &entries) {
	hashWriteInt(runtimeKv, hash, "count", static_cast<int>(entries.size()));
	for (std::size_t index = 0; index < entries.size(); ++index) {
		const MRMacroSourceMapEntry &entry = entries[index];

		writeSourceMapEntryHash(runtimeKv, runtimeKv.replaceChild(hash, std::to_string(index + 1)), entry);
	}
}

std::vector<MRMacroSourceMapEntry> readSourceMapHash(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	std::vector<MRMacroSourceMapEntry> entries;
	const int count = hashReadInt(runtimeKv, hash, "count", 0);

	for (int index = 1; index <= count; ++index) {
		Value entryHash;

		if (!runtimeKv.findChild(hash, std::to_string(index), entryHash)) continue;
		entries.push_back(readSourceMapEntryHash(runtimeKv, entryHash));
	}
	return entries;
}

void writeSourceLineIndexEntry(MRVMRuntimeKv &runtimeKv, const Value &indexHash, const MRMacroSourceMapEntry &entry) {
	Value byLine = runtimeKv.ensureChild(indexHash, "byLine");
	Value byBytecode = runtimeKv.ensureChild(indexHash, "byBytecode");
	Value existing;
	MRMacroSourceMapEntry indexed = entry;
	const std::string macroKey = mrvmUpperKey(entry.macroName);
	const std::string key = lineKey(entry.line);
	const std::string bytecodeKey = std::to_string(entry.bytecodeOffset);

	if (macroKey.empty() || entry.line <= 0) return;
	indexed.macroName = macroKey;
	if (!runtimeKv.findChild(byLine, key, existing)) writeSourceMapEntryHash(runtimeKv, runtimeKv.replaceChild(byLine, key), indexed);
	if (!runtimeKv.findChild(byBytecode, bytecodeKey, existing)) writeSourceMapEntryHash(runtimeKv, runtimeKv.replaceChild(byBytecode, bytecodeKey), indexed);
}

void writeSourceLineIndexHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::vector<MRMacroSourceMapEntry> &entries) {
	hashWriteInt(runtimeKv, hash, "version", 1);
	for (const MRMacroSourceMapEntry &entry : entries)
		writeSourceLineIndexEntry(runtimeKv, hash, entry);
}

void writeExecutionProfileHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroExecutionProfile &profile) {
	hashWriteUint(runtimeKv, hash, "flags", profile.flags);
	hashWriteUint(runtimeKv, hash, "opcodeCount", profile.opcodeCount);
	hashWriteUint(runtimeKv, hash, "intrinsicCount", profile.intrinsicCount);
	hashWriteUint(runtimeKv, hash, "procCount", profile.procCount);
	hashWriteUint(runtimeKv, hash, "procVarCount", profile.procVarCount);
	writeStringVectorHash(runtimeKv, runtimeKv.replaceChild(hash, "stagedWriteSymbols"), profile.stagedWriteSymbols);
	writeStringVectorHash(runtimeKv, runtimeKv.replaceChild(hash, "uiAffinitySymbols"), profile.uiAffinitySymbols);
	writeStringVectorHash(runtimeKv, runtimeKv.replaceChild(hash, "externalIoSymbols"), profile.externalIoSymbols);
}

MRMacroExecutionProfile readExecutionProfileHash(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	MRMacroExecutionProfile profile;
	Value values;

	profile.flags = static_cast<unsigned>(hashReadUint(runtimeKv, hash, "flags", 0));
	profile.opcodeCount = static_cast<std::size_t>(hashReadUint(runtimeKv, hash, "opcodeCount", 0));
	profile.intrinsicCount = static_cast<std::size_t>(hashReadUint(runtimeKv, hash, "intrinsicCount", 0));
	profile.procCount = static_cast<std::size_t>(hashReadUint(runtimeKv, hash, "procCount", 0));
	profile.procVarCount = static_cast<std::size_t>(hashReadUint(runtimeKv, hash, "procVarCount", 0));
	if (runtimeKv.findChild(hash, "stagedWriteSymbols", values)) profile.stagedWriteSymbols = readStringVectorHash(runtimeKv, values);
	if (runtimeKv.findChild(hash, "uiAffinitySymbols", values)) profile.uiAffinitySymbols = readStringVectorHash(runtimeKv, values);
	if (runtimeKv.findChild(hash, "externalIoSymbols", values)) profile.externalIoSymbols = readStringVectorHash(runtimeKv, values);
	return profile;
}

void writeKeyHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const TKey &key) {
	hashWriteInt(runtimeKv, hash, "code", key.code);
	hashWriteInt(runtimeKv, hash, "mods", key.mods);
}

TKey readKeyHash(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	return TKey(static_cast<ushort>(hashReadInt(runtimeKv, hash, "code", 0)), static_cast<ushort>(hashReadInt(runtimeKv, hash, "mods", 0)));
}

void writeMacroRefHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MacroRef &macroRef) {
	hashWriteString(runtimeKv, hash, "fileKey", macroRef.fileKey);
	hashWriteString(runtimeKv, hash, "displayName", macroRef.displayName);
	hashWriteUint(runtimeKv, hash, "entryOffset", macroRef.entryOffset);
	hashWriteString(runtimeKv, hash, "assignedKeySpec", macroRef.assignedKeySpec);
	hashWriteString(runtimeKv, hash, "closureId", macroRef.closureId);
	writeKeyHash(runtimeKv, runtimeKv.replaceChild(hash, "assignedKey"), macroRef.assignedKey);
	hashWriteInt(runtimeKv, hash, "fromMode", macroRef.fromMode);
	hashWriteInt(runtimeKv, hash, "hasAssignedKey", macroRef.hasAssignedKey ? 1 : 0);
	hashWriteInt(runtimeKv, hash, "firstRunPending", macroRef.firstRunPending ? 1 : 0);
	hashWriteInt(runtimeKv, hash, "transientAttr", macroRef.transientAttr ? 1 : 0);
	hashWriteInt(runtimeKv, hash, "dumpAttr", macroRef.dumpAttr ? 1 : 0);
	hashWriteInt(runtimeKv, hash, "permAttr", macroRef.permAttr ? 1 : 0);
	hashWriteInt(runtimeKv, hash, "closureUnit", macroRef.closureUnit ? 1 : 0);
	hashWriteUint(runtimeKv, hash, "tickMs", macroRef.tickMs);
	hashWriteUint(runtimeKv, hash, "scheduledConsumerId", macroRef.scheduledConsumerId);
}

bool readMacroRefHash(MRVMRuntimeKv &runtimeKv, const Value &hash, MacroRef &macroRef) {
	Value keyHash;

	macroRef.fileKey = hashReadString(runtimeKv, hash, "fileKey");
	macroRef.displayName = hashReadString(runtimeKv, hash, "displayName");
	macroRef.entryOffset = static_cast<std::size_t>(hashReadUint(runtimeKv, hash, "entryOffset", 0));
	macroRef.assignedKeySpec = hashReadString(runtimeKv, hash, "assignedKeySpec");
	macroRef.closureId = hashReadString(runtimeKv, hash, "closureId");
	if (runtimeKv.findChild(hash, "assignedKey", keyHash)) macroRef.assignedKey = readKeyHash(runtimeKv, keyHash);
	macroRef.fromMode = hashReadInt(runtimeKv, hash, "fromMode", MACRO_MODE_EDIT);
	macroRef.hasAssignedKey = hashReadInt(runtimeKv, hash, "hasAssignedKey", 0) != 0;
	macroRef.firstRunPending = hashReadInt(runtimeKv, hash, "firstRunPending", 0) != 0;
	macroRef.transientAttr = hashReadInt(runtimeKv, hash, "transientAttr", 0) != 0;
	macroRef.dumpAttr = hashReadInt(runtimeKv, hash, "dumpAttr", 0) != 0;
	macroRef.permAttr = hashReadInt(runtimeKv, hash, "permAttr", 0) != 0;
	macroRef.closureUnit = hashReadInt(runtimeKv, hash, "closureUnit", 0) != 0;
	macroRef.tickMs = hashReadUint(runtimeKv, hash, "tickMs", 0);
	macroRef.scheduledConsumerId = static_cast<MRRuntimeScheduledConsumerId>(hashReadUint(runtimeKv, hash, "scheduledConsumerId", 0));
	return !macroRef.displayName.empty() && !macroRef.fileKey.empty();
}

void writeLoadedFileHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const LoadedMacroFile &file) {
	hashWriteString(runtimeKv, hash, "fileKey", file.fileKey);
	hashWriteString(runtimeKv, hash, "displayName", file.displayName);
	hashWriteString(runtimeKv, hash, "resolvedPath", file.resolvedPath);
	hashWriteString(runtimeKv, hash, "bytecode", bytecodeToString(file.bytecode));
	writeStringVectorHash(runtimeKv, runtimeKv.replaceChild(hash, "macroNames"), file.macroNames);
	writeSourceMapHash(runtimeKv, runtimeKv.replaceChild(hash, "sourceMap"), file.sourceMap);
	writeSourceLineIndexHash(runtimeKv, runtimeKv.replaceChild(hash, "sourceLineIndex"), file.sourceMap);
	writeExecutionProfileHash(runtimeKv, runtimeKv.replaceChild(hash, "profile"), file.profile);
}

bool readLoadedFileHash(MRVMRuntimeKv &runtimeKv, const Value &hash, LoadedMacroFile &file) {
	Value macroNames;
	Value sourceMap;
	Value profile;

	file.fileKey = hashReadString(runtimeKv, hash, "fileKey");
	file.displayName = hashReadString(runtimeKv, hash, "displayName");
	file.resolvedPath = hashReadString(runtimeKv, hash, "resolvedPath");
	file.bytecode = stringToBytecode(hashReadString(runtimeKv, hash, "bytecode"));
	if (runtimeKv.findChild(hash, "macroNames", macroNames)) file.macroNames = readStringVectorHash(runtimeKv, macroNames);
	if (runtimeKv.findChild(hash, "sourceMap", sourceMap)) file.sourceMap = readSourceMapHash(runtimeKv, sourceMap);
	if (runtimeKv.findChild(hash, "profile", profile)) file.profile = readExecutionProfileHash(runtimeKv, profile);
	return !file.fileKey.empty();
}

bool findLoadedFileHash(MRVMRuntimeKv &runtimeKv, const std::string &fileKey, Value &fileHash) {
	Value files;

	if (fileKey.empty() || !findCatalogChildPath(runtimeKv, {"files", "byKey"}, files)) return false;
	return runtimeKv.findChild(files, fileKey, fileHash);
}

bool findLoadedMacroHash(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, Value &macroHash) {
	Value macros;

	if (macroKey.empty() || !findCatalogChildPath(runtimeKv, {"macros", "byName"}, macros)) return false;
	return runtimeKv.findChild(macros, macroKey, macroHash);
}
} // namespace

MacroRef::MacroRef() : entryOffset(0), fromMode(MACRO_MODE_EDIT), hasAssignedKey(false), firstRunPending(true), transientAttr(false), dumpAttr(false), permAttr(false), closureUnit(false), tickMs(0), scheduledConsumerId(0) {
}

MRMacroSourceMapEntry::MRMacroSourceMapEntry() : bytecodeOffset(0), sourceStartOffset(0), sourceEndOffset(0), line(0), column(0), macroName(), debuggableKind(0) {
}

IndexedBoundMacroEntry::IndexedBoundMacroEntry() {
}

IndexedBoundMacroEntry::IndexedBoundMacroEntry(const TKey &aKey, std::string aFilePath) : key(aKey), filePath(std::move(aFilePath)) {
}

bool mrvmRuntimeCatalogReadLoadedFile(MRVMRuntimeKv &runtimeKv, const std::string &fileKey, LoadedMacroFile &file) {
	Value fileHash;

	if (!findLoadedFileHash(runtimeKv, fileKey, fileHash)) return false;
	return readLoadedFileHash(runtimeKv, fileHash, file);
}

bool mrvmRuntimeCatalogLoadedFileExists(MRVMRuntimeKv &runtimeKv, const std::string &fileKey) {
	Value fileHash;

	return findLoadedFileHash(runtimeKv, fileKey, fileHash);
}

void mrvmRuntimeCatalogWriteLoadedFile(MRVMRuntimeKv &runtimeKv, const LoadedMacroFile &file) {
	Value files = ensureCatalogChildPath(runtimeKv, {"files", "byKey"});

	if (file.fileKey.empty()) return;
	writeLoadedFileHash(runtimeKv, runtimeKv.replaceChild(files, file.fileKey), file);
}

bool mrvmRuntimeCatalogEraseLoadedFile(MRVMRuntimeKv &runtimeKv, const std::string &fileKey) {
	Value files;

	if (fileKey.empty() || !findCatalogChildPath(runtimeKv, {"files", "byKey"}, files)) return false;
	return runtimeKv.eraseChild(files, fileKey);
}

std::vector<std::string> mrvmRuntimeCatalogLoadedFileKeys(MRVMRuntimeKv &runtimeKv) {
	Value files;

	if (!findCatalogChildPath(runtimeKv, {"files", "byKey"}, files)) return std::vector<std::string>();
	return runtimeKv.globalStore().keys(files.hashHandle);
}

bool mrvmRuntimeCatalogReadLoadedMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, MacroRef &macroRef) {
	Value macroHash;

	if (!findLoadedMacroHash(runtimeKv, macroKey, macroHash)) return false;
	return readMacroRefHash(runtimeKv, macroHash, macroRef);
}

bool mrvmRuntimeCatalogLoadedMacroExists(MRVMRuntimeKv &runtimeKv, const std::string &macroKey) {
	Value macroHash;

	return findLoadedMacroHash(runtimeKv, macroKey, macroHash);
}

void mrvmRuntimeCatalogWriteLoadedMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, const MacroRef &macroRef) {
	Value macros = ensureCatalogChildPath(runtimeKv, {"macros", "byName"});

	if (macroKey.empty()) return;
	writeMacroRefHash(runtimeKv, runtimeKv.replaceChild(macros, macroKey), macroRef);
}

bool mrvmRuntimeCatalogEraseLoadedMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey) {
	Value macros;

	if (macroKey.empty() || !findCatalogChildPath(runtimeKv, {"macros", "byName"}, macros)) return false;
	return runtimeKv.eraseChild(macros, macroKey);
}

bool mrvmRuntimeCatalogFirstSourceMapSpanForLine(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, int line, MRMacroSourceMapEntry &entry) {
	MacroRef macroRef;
	Value fileHash;
	Value sourceLineIndex;
	Value byLine;
	Value entryHash;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	if (normalizedMacroKey.empty() || line <= 0) return false;
	if (!mrvmRuntimeCatalogReadLoadedMacro(runtimeKv, normalizedMacroKey, macroRef)) return false;
	if (!findLoadedFileHash(runtimeKv, macroRef.fileKey, fileHash)) return false;
	if (!runtimeKv.findChild(fileHash, "sourceLineIndex", sourceLineIndex)) return false;
	if (!runtimeKv.findChild(sourceLineIndex, "byLine", byLine)) return false;
	if (!runtimeKv.findChild(byLine, lineKey(line), entryHash)) return false;
	entry = readSourceMapEntryHash(runtimeKv, entryHash);
	return entry.line > 0 && !entry.macroName.empty();
}

bool mrvmRuntimeCatalogSourceMapSpanForBytecodeOffset(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, std::size_t bytecodeOffset, MRMacroSourceMapEntry &entry) {
	MacroRef macroRef;
	Value fileHash;
	Value sourceLineIndex;
	Value byBytecode;
	Value entryHash;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	if (normalizedMacroKey.empty()) return false;
	if (!mrvmRuntimeCatalogReadLoadedMacro(runtimeKv, normalizedMacroKey, macroRef)) return false;
	if (!findLoadedFileHash(runtimeKv, macroRef.fileKey, fileHash)) return false;
	if (!runtimeKv.findChild(fileHash, "sourceLineIndex", sourceLineIndex)) return false;
	if (!runtimeKv.findChild(sourceLineIndex, "byBytecode", byBytecode)) return false;
	if (!runtimeKv.findChild(byBytecode, std::to_string(bytecodeOffset), entryHash)) return false;
	entry = readSourceMapEntryHash(runtimeKv, entryHash);
	return entry.line > 0 && !entry.macroName.empty();
}

std::vector<std::string> mrvmRuntimeCatalogMacroOrder(MRVMRuntimeKv &runtimeKv) {
	Value order;

	if (!findCatalogChildPath(runtimeKv, {"macros", "order"}, order)) return std::vector<std::string>();
	return readStringVectorHash(runtimeKv, order);
}

void mrvmRuntimeCatalogWriteMacroOrder(MRVMRuntimeKv &runtimeKv, const std::vector<std::string> &orderValues) {
	writeStringVectorHash(runtimeKv, runtimeKv.replaceChild(ensureCatalogChildPath(runtimeKv, {"macros"}), "order"), orderValues);
}

void mrvmRuntimeCatalogAppendMacroOrder(MRVMRuntimeKv &runtimeKv, const std::string &macroKey) {
	Value order = ensureCatalogChildPath(runtimeKv, {"macros", "order"});
	const int count = hashReadInt(runtimeKv, order, "count", 0) + 1;

	if (macroKey.empty()) return;
	hashWriteInt(runtimeKv, order, "count", count);
	hashWriteString(runtimeKv, order, std::to_string(count), macroKey);
}

void mrvmRuntimeCatalogRemoveMacroOrder(MRVMRuntimeKv &runtimeKv, const std::string &macroKey) {
	std::vector<std::string> orderValues = mrvmRuntimeCatalogMacroOrder(runtimeKv);

	orderValues.erase(std::remove(orderValues.begin(), orderValues.end(), macroKey), orderValues.end());
	mrvmRuntimeCatalogWriteMacroOrder(runtimeKv, orderValues);
}

std::size_t mrvmRuntimeCatalogMacroEnumIndex(MRVMRuntimeKv &runtimeKv) {
	Value enumeration;

	if (!findCatalogChildPath(runtimeKv, {"enumeration"}, enumeration)) return 0;
	return static_cast<std::size_t>(hashReadUint(runtimeKv, enumeration, "macroIndex", 0));
}

void mrvmRuntimeCatalogSetMacroEnumIndex(MRVMRuntimeKv &runtimeKv, std::size_t index) {
	hashWriteUint(runtimeKv, ensureCatalogChildPath(runtimeKv, {"enumeration"}), "macroIndex", index);
}

std::size_t mrvmRuntimeCatalogLoadedMacroCount(MRVMRuntimeKv &runtimeKv) {
	Value macros;

	if (!findCatalogChildPath(runtimeKv, {"macros", "byName"}, macros)) return 0;
	return runtimeKv.globalStore().keys(macros.hashHandle).size();
}

std::vector<IndexedBoundMacroEntry> mrvmRuntimeCatalogIndexedBindings(MRVMRuntimeKv &runtimeKv) {
	Value bindings;
	std::vector<IndexedBoundMacroEntry> result;
	const int count = findCatalogChildPath(runtimeKv, {"indexed", "bindings", "order"}, bindings) ? hashReadInt(runtimeKv, bindings, "count", 0) : 0;

	for (int index = 1; index <= count; ++index) {
		Value entryHash;
		Value keyHash;
		IndexedBoundMacroEntry entry;
		if (!runtimeKv.findChild(bindings, std::to_string(index), entryHash)) continue;
		if (!runtimeKv.findChild(entryHash, "key", keyHash)) continue;
		entry.key = readKeyHash(runtimeKv, keyHash);
		entry.filePath = hashReadString(runtimeKv, entryHash, "filePath");
		if (!entry.filePath.empty()) result.push_back(entry);
	}
	return result;
}

void mrvmRuntimeCatalogWriteIndexedBindings(MRVMRuntimeKv &runtimeKv, const std::vector<IndexedBoundMacroEntry> &bindings) {
	Value order = runtimeKv.replaceChild(ensureCatalogChildPath(runtimeKv, {"indexed", "bindings"}), "order");

	hashWriteInt(runtimeKv, order, "count", static_cast<int>(bindings.size()));
	for (std::size_t index = 0; index < bindings.size(); ++index) {
		Value entry = runtimeKv.replaceChild(order, std::to_string(index + 1));
		writeKeyHash(runtimeKv, runtimeKv.replaceChild(entry, "key"), bindings[index].key);
		hashWriteString(runtimeKv, entry, "filePath", bindings[index].filePath);
	}
}

std::vector<std::string> mrvmRuntimeCatalogIndexedFiles(MRVMRuntimeKv &runtimeKv) {
	Value files;

	if (!findCatalogChildPath(runtimeKv, {"indexed", "files", "order"}, files)) return std::vector<std::string>();
	return readStringVectorHash(runtimeKv, files);
}

void mrvmRuntimeCatalogWriteIndexedFiles(MRVMRuntimeKv &runtimeKv, const std::vector<std::string> &files) {
	writeStringVectorHash(runtimeKv, runtimeKv.replaceChild(ensureCatalogChildPath(runtimeKv, {"indexed", "files"}), "order"), files);
}

std::size_t mrvmRuntimeCatalogIndexedWarmupCursor(MRVMRuntimeKv &runtimeKv) {
	Value warmup;

	if (!findCatalogChildPath(runtimeKv, {"indexed", "warmup"}, warmup)) return 0;
	return static_cast<std::size_t>(hashReadUint(runtimeKv, warmup, "cursor", 0));
}

void mrvmRuntimeCatalogSetIndexedWarmupCursor(MRVMRuntimeKv &runtimeKv, std::size_t cursor) {
	hashWriteUint(runtimeKv, ensureCatalogChildPath(runtimeKv, {"indexed", "warmup"}), "cursor", cursor);
}

bool mrvmRuntimeCatalogMarkIndexedWarmupAttempted(MRVMRuntimeKv &runtimeKv, const std::string &fileKey) {
	Value attempted = ensureCatalogChildPath(runtimeKv, {"indexed", "warmup", "attemptedFiles"});
	MRVMHashStore &store = runtimeKv.globalStore();

	if (fileKey.empty()) return false;
	if (mrvmHashContainsValue(store, store, attempted, fileKey)) return false;
	hashWriteInt(runtimeKv, attempted, fileKey, 1);
	return true;
}

void mrvmRuntimeCatalogClearIndexedWarmup(MRVMRuntimeKv &runtimeKv) {
	Value indexed = ensureCatalogChildPath(runtimeKv, {"indexed"});

	static_cast<void>(runtimeKv.eraseChild(indexed, "files"));
	static_cast<void>(runtimeKv.eraseChild(indexed, "bindings"));
	static_cast<void>(runtimeKv.eraseChild(indexed, "warmup"));
	mrvmRuntimeCatalogSetIndexedWarmupCursor(runtimeKv, 0);
}

std::size_t mrvmRuntimeCatalogIndexedBindingCount(MRVMRuntimeKv &runtimeKv) {
	Value bindings;

	if (!findCatalogChildPath(runtimeKv, {"indexed", "bindings", "order"}, bindings)) return 0;
	return static_cast<std::size_t>(hashReadInt(runtimeKv, bindings, "count", 0));
}

void mrvmBootstrapBoundMacroIndex(const std::string &directoryPath, std::size_t *fileCount, std::size_t *bindingCount) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	std::vector<std::string> files = mrvmListMrmacFilesInDirectory(directoryPath);
	std::vector<std::string> indexedFiles;
	std::vector<IndexedBoundMacroEntry> indexedBindings;
	std::set<std::string> dedupe;

	mrvmRuntimeCatalogClearIndexedWarmup(runtimeKv);

	for (const auto &file : files) {
		std::string source;
		std::vector<TKey> keys;
		std::string fileKey;

		if (!readTextFile(file, source)) continue;
		if (!mrvmParseIndexedBindingHeaders(source, keys) || keys.empty()) continue;
		fileKey = mrvmMakeMacroFileKey(file);
		if (dedupe.insert(fileKey).second) indexedFiles.push_back(file);
		for (auto key : keys)
			indexedBindings.emplace_back(key, file);
	}

	mrvmRuntimeCatalogWriteIndexedFiles(runtimeKv, indexedFiles);
	mrvmRuntimeCatalogWriteIndexedBindings(runtimeKv, indexedBindings);

	if (fileCount != nullptr) *fileCount = indexedFiles.size();
	if (bindingCount != nullptr) *bindingCount = indexedBindings.size();
}

bool mrvmWarmLoadNextIndexedMacroFile(std::string *loadedFilePath, std::string *failedFilePath, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();

	if (loadedFilePath != nullptr) loadedFilePath->clear();
	if (failedFilePath != nullptr) failedFilePath->clear();
	if (errorMessage != nullptr) errorMessage->clear();

	{
		std::vector<std::string> indexedFiles = mrvmRuntimeCatalogIndexedFiles(runtimeKv);
		std::size_t cursor = mrvmRuntimeCatalogIndexedWarmupCursor(runtimeKv);

		while (cursor < indexedFiles.size()) {
			const std::string filePath = indexedFiles[cursor++];
			std::string fileKey = mrvmMakeMacroFileKey(filePath);
			std::string localError;

			mrvmRuntimeCatalogSetIndexedWarmupCursor(runtimeKv, cursor);
			if (!mrvmRuntimeCatalogMarkIndexedWarmupAttempted(runtimeKv, fileKey)) continue;
			if (mrvmRuntimeCatalogLoadedFileExists(runtimeKv, fileKey)) continue;
			if (mrvmLoadMacroFile(filePath, &localError)) {
				if (loadedFilePath != nullptr) *loadedFilePath = filePath;
				return true;
			}
			if (failedFilePath != nullptr) *failedFilePath = filePath;
			if (localError.empty()) localError = "Unable to load macro file.";
			if (errorMessage != nullptr) *errorMessage = localError;
			return false;
		}
	}
	return false;
}

bool mrvmHasPendingIndexedMacroWarmup() {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();

	return mrvmRuntimeCatalogIndexedWarmupCursor(runtimeKv) < mrvmRuntimeCatalogIndexedFiles(runtimeKv).size();
}
