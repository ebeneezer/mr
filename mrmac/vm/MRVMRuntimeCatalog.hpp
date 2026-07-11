#ifndef MRVM_RUNTIME_CATALOG_HPP
#define MRVM_RUNTIME_CATALOG_HPP

#ifndef Uses_TKeys
#define Uses_TKeys
#endif
#include <tvision/tv.h>

#include "MRVMProfile.hpp"
#include "MRVMRuntimeKv.hpp"

#include "../../app/MRRuntimeScheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct MacroRef {
	std::string fileKey;
	std::string displayName;
	std::size_t entryOffset;
	std::string assignedKeySpec;
	std::string closureId;
	TKey assignedKey;
	int fromMode;
	bool hasAssignedKey;
	bool firstRunPending;
	bool transientAttr;
	bool dumpAttr;
	bool permAttr;
	bool closureUnit;
	std::uint64_t tickMs;
	MRRuntimeScheduledConsumerId scheduledConsumerId;

	MacroRef();
};

struct MRMacroSourceMapEntry {
	std::size_t bytecodeOffset;
	std::size_t sourceStartOffset;
	std::size_t sourceEndOffset;
	int line;
	int column;
	std::string macroName;
	int debuggableKind;

	MRMacroSourceMapEntry();
};

struct LoadedMacroFile {
	std::string fileKey;
	std::string displayName;
	std::string resolvedPath;
	std::vector<unsigned char> bytecode;
	std::vector<std::string> macroNames;
	std::vector<MRMacroSourceMapEntry> sourceMap;
	MRMacroExecutionProfile profile;
};

struct IndexedBoundMacroEntry {
	TKey key;
	std::string filePath;

	IndexedBoundMacroEntry();
	IndexedBoundMacroEntry(const TKey &aKey, std::string aFilePath);
};

bool mrvmRuntimeCatalogReadLoadedFile(MRVMRuntimeKv &runtimeKv, const std::string &fileKey, LoadedMacroFile &file);
bool mrvmRuntimeCatalogLoadedFileExists(MRVMRuntimeKv &runtimeKv, const std::string &fileKey);
void mrvmRuntimeCatalogWriteLoadedFile(MRVMRuntimeKv &runtimeKv, const LoadedMacroFile &file);
bool mrvmRuntimeCatalogEraseLoadedFile(MRVMRuntimeKv &runtimeKv, const std::string &fileKey);
std::vector<std::string> mrvmRuntimeCatalogLoadedFileKeys(MRVMRuntimeKv &runtimeKv);

bool mrvmRuntimeCatalogReadLoadedMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, MacroRef &macroRef);
bool mrvmRuntimeCatalogLoadedMacroExists(MRVMRuntimeKv &runtimeKv, const std::string &macroKey);
void mrvmRuntimeCatalogWriteLoadedMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, const MacroRef &macroRef);
bool mrvmRuntimeCatalogEraseLoadedMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey);
bool mrvmRuntimeCatalogFirstSourceMapSpanForLine(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, int line, MRMacroSourceMapEntry &entry);
bool mrvmRuntimeCatalogSourceMapSpanForBytecodeOffset(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, std::size_t bytecodeOffset, MRMacroSourceMapEntry &entry);

std::vector<std::string> mrvmRuntimeCatalogMacroOrder(MRVMRuntimeKv &runtimeKv);
void mrvmRuntimeCatalogWriteMacroOrder(MRVMRuntimeKv &runtimeKv, const std::vector<std::string> &orderValues);
void mrvmRuntimeCatalogAppendMacroOrder(MRVMRuntimeKv &runtimeKv, const std::string &macroKey);
void mrvmRuntimeCatalogRemoveMacroOrder(MRVMRuntimeKv &runtimeKv, const std::string &macroKey);
std::size_t mrvmRuntimeCatalogMacroEnumIndex(MRVMRuntimeKv &runtimeKv);
void mrvmRuntimeCatalogSetMacroEnumIndex(MRVMRuntimeKv &runtimeKv, std::size_t index);
std::size_t mrvmRuntimeCatalogLoadedMacroCount(MRVMRuntimeKv &runtimeKv);

std::vector<IndexedBoundMacroEntry> mrvmRuntimeCatalogIndexedBindings(MRVMRuntimeKv &runtimeKv);
void mrvmRuntimeCatalogWriteIndexedBindings(MRVMRuntimeKv &runtimeKv, const std::vector<IndexedBoundMacroEntry> &bindings);
std::vector<std::string> mrvmRuntimeCatalogIndexedFiles(MRVMRuntimeKv &runtimeKv);
void mrvmRuntimeCatalogWriteIndexedFiles(MRVMRuntimeKv &runtimeKv, const std::vector<std::string> &files);
std::size_t mrvmRuntimeCatalogIndexedWarmupCursor(MRVMRuntimeKv &runtimeKv);
void mrvmRuntimeCatalogSetIndexedWarmupCursor(MRVMRuntimeKv &runtimeKv, std::size_t cursor);
bool mrvmRuntimeCatalogMarkIndexedWarmupAttempted(MRVMRuntimeKv &runtimeKv, const std::string &fileKey);
void mrvmRuntimeCatalogClearIndexedWarmup(MRVMRuntimeKv &runtimeKv);
std::size_t mrvmRuntimeCatalogIndexedBindingCount(MRVMRuntimeKv &runtimeKv);

#endif
