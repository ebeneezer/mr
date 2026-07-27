#include "MRVMRuntimeInternal.hpp"

#include "MRVMExecSessions.hpp"
#include "MRVMKeymapRuntime.hpp"
#include "MRVMMacroSpecRuntime.hpp"
#include "MRVMProfile.hpp"
#include "MRVMRuntimeDebugger.hpp"
#include "MRVMValue.hpp"
#include "../MRVMDebugSession.hpp"
#include "../mrmac.h"
#include "../../app/MRRuntimeScheduler.hpp"
#include "../../app/utils/MRFileIOUtils.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../ui/MRWindowSupport.hpp"

#define Uses_TProgram
#include <tvision/tv.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

void collectSourceMapEntry(void *context, const MRMacSourceMapEntry *entry) {
	std::vector<MRMacroSourceMapEntry> *entries = static_cast<std::vector<MRMacroSourceMapEntry> *>(context);
	MRMacroSourceMapEntry mapped;

	if (entries == nullptr || entry == nullptr) return;
	mapped.bytecodeOffset = entry->bytecodeOffset;
	mapped.sourceStartOffset = entry->sourceStartOffset;
	mapped.sourceEndOffset = entry->sourceEndOffset;
	mapped.line = entry->line;
	mapped.column = entry->column;
	mapped.macroName = entry->macroName != nullptr ? entry->macroName : std::string();
	mapped.debuggableKind = entry->debuggableKind;
	entries->push_back(mapped);
}

void logMacroProfileLine(const char *prefix, const LoadedMacroFile &file) {
	if (TProgram::deskTop == nullptr) return;
	std::string label = !file.displayName.empty() ? file.displayName : file.resolvedPath;
	std::string line = std::string(prefix) + " '" + label + "': " + mrvmDescribeExecutionProfile(file.profile);
	mrLogMessage(line.c_str());
}

} // namespace

namespace mrvm_runtime {

bool refreshLoadedFileBytecode(const std::string &fileKey) {
	LoadedMacroFile file;
	std::string source;
	unsigned char *compiled = nullptr;
	size_t compiledSize = 0;
	int macroCount;

	if (!readLoadedMacroFileByKey(fileKey, file)) return false;
	if (file.resolvedPath.empty() || !readTextFile(file.resolvedPath, source)) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	file.sourceMap.clear();
	compiled = compile_macro_code(source.c_str(), &compiledSize);
	if (compiled == nullptr) {
		runtimeErrorLevel() = 5005;
		return false;
	}

	file.bytecode.assign(compiled, compiled + compiledSize);
	std::free(compiled);
	file.profile = mrvmAnalyzeBytecode(file.bytecode.data(), file.bytecode.size());

	macroCount = get_compiled_macro_count();
	for (int i = 0; i < macroCount; ++i) {
		const char *macroNameText = get_compiled_macro_name(i);
		int entry = get_compiled_macro_entry(i);
		int flags = get_compiled_macro_flags(i);
		const char *keyspecText = get_compiled_macro_keyspec(i);
		int mode = get_compiled_macro_mode(i);
		int unitKind = get_compiled_macro_unit_kind(i);
		int tickMs = get_compiled_macro_tick_ms(i);
		std::string displayName = macroNameText != nullptr ? macroNameText : std::string();
		std::string macroKey = mrvmUpperKey(displayName);
		MacroRef macroRef;

		if (displayName.empty() || entry < 0) continue;
		if (!readLoadedMacroByKey(macroKey, macroRef) || macroRef.fileKey != fileKey) continue;

		macroRef.displayName = displayName;
		macroRef.entryOffset = static_cast<std::size_t>(entry);
		macroRef.transientAttr = (flags & MACRO_ATTR_TRANS) != 0;
		macroRef.dumpAttr = (flags & MACRO_ATTR_DUMP) != 0;
		macroRef.permAttr = (flags & MACRO_ATTR_PERM) != 0;
		macroRef.assignedKeySpec = keyspecText != nullptr ? keyspecText : std::string();
		macroRef.fromMode = (mode == MACRO_MODE_DOS_SHELL || mode == MACRO_MODE_ALL) ? mode : MACRO_MODE_EDIT;
		macroRef.closureUnit = unitKind == MRMAC_UNIT_CLOSURE;
		macroRef.tickMs = tickMs > 0 ? static_cast<std::uint64_t>(tickMs) : 0;
		macroRef.closureId.clear();
		if (macroRef.scheduledConsumerId != 0) {
			removeRuntimeScheduledConsumer(macroRef.scheduledConsumerId);
			macroRef.scheduledConsumerId = 0;
		}
		if (macroRef.closureUnit && macroRef.tickMs != 0) {
			MRRuntimeScheduledConsumerConfig config;
			const std::string macroSpec = file.displayName + "^" + displayName;
			config.intervalMs = macroRef.tickMs;
			config.macroSpec = macroSpec;
			config.entryName = displayName;
			config.closureId = macroSpec;
			macroRef.closureId = macroSpec;
			mrvmExecSessionsEnsureClosureState(g_runtimeEnv.runtimeKv, config.closureId, static_cast<int>(macroRef.tickMs));
			macroRef.scheduledConsumerId = registerRuntimeScheduledConsumer(config);
		}
		macroRef.hasAssignedKey = false;
		if (!macroRef.assignedKeySpec.empty()) macroRef.hasAssignedKey = mrvmParseAssignedKeySpec(macroRef.assignedKeySpec, macroRef.assignedKey);
		writeLoadedMacroByKey(macroKey, macroRef);
	}

	writeLoadedMacroFileByKey(file);
	runtimeErrorLevel() = 0;
	logMacroProfileLine("Refreshed macro file", file);
	return true;
}

bool ensureLoadedFileResident(const std::string &fileKey) {
	LoadedMacroFile file;
	if (!readLoadedMacroFileByKey(fileKey, file)) return false;
	if (!file.bytecode.empty()) return true;
	return refreshLoadedFileBytecode(fileKey);
}

bool evictTransientFileImage(const std::string &fileKey) {
	LoadedMacroFile file;
	if (!readLoadedMacroFileByKey(fileKey, file)) return false;
	if (!fileContainsOnlyTransientMacros(file)) return false;
	for (const auto &macroName : file.macroNames)
		if (macroIsRunning(macroName)) return false;
	file.bytecode.clear();
	file.bytecode.shrink_to_fit();
	writeLoadedMacroFileByKey(file);
	return true;
}

bool currentBackgroundChildMacroAllowed(const LoadedMacroFile &file) noexcept {
	if (currentBackgroundEditSession() != nullptr) return mrvmCanRunInBackground(file.profile) || mrvmCanRunStagedInBackground(file.profile);
	return false;
}

bool loadMacroFileIntoRegistry(const std::string &spec, std::string *loadedFileKey) {
	std::string resolvedPath = mrvmResolveMacroFilePath(spec, normalizeConfiguredPathInput(configuredMacroDirectoryPath()));
	std::string fileKey = mrvmMakeMacroFileKey(spec);
	std::string source;
	LoadedMacroFile newFile;
	LoadedMacroFile existingFile;
	unsigned char *compiled = nullptr;
	size_t compiledSize = 0;
	int macroCount;

	if (loadedFileKey != nullptr) loadedFileKey->clear();

	if (resolvedPath.empty() || !readTextFile(resolvedPath, source)) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	const bool hasExistingFile = readLoadedMacroFileByKey(fileKey, existingFile);
	if (hasExistingFile) {
		for (const auto &macroName : existingFile.macroNames)
			if (macroIsRunning(macroName)) {
				runtimeErrorLevel() = 5006;
				return false;
			}
	}

	newFile.sourceMap.clear();
	compiled = compile_macro_code(source.c_str(), &compiledSize);
	if (compiled == nullptr) {
		runtimeErrorLevel() = 5005;
		return false;
	}

	macroCount = get_compiled_macro_count();
	for (int i = 0; i < macroCount; ++i) {
		const char *macroNameText = get_compiled_macro_name(i);
		std::string displayName = macroNameText != nullptr ? macroNameText : std::string();
		std::string macroKey = mrvmUpperKey(displayName);
		MacroRef existingMacro;

		if (displayName.empty()) continue;

		if (readLoadedMacroByKey(macroKey, existingMacro)) {
			if (macroIsRunning(macroKey) || existingMacro.permAttr) {
				std::free(compiled);
				runtimeErrorLevel() = 5006;
				return false;
			}
		}
	}

	newFile.fileKey = fileKey;
	newFile.displayName = trimAscii(spec);
	newFile.resolvedPath = resolvedPath;
	newFile.bytecode.assign(compiled, compiled + compiledSize);
	std::free(compiled);
	newFile.profile = mrvmAnalyzeBytecode(newFile.bytecode.data(), newFile.bytecode.size());

	if (hasExistingFile) {
		std::vector<std::string> oldNames = existingFile.macroNames;
		for (const auto &oldName : oldNames)
			removeMacroFromRegistryByKey(oldName);
	}

	for (int i = 0; i < macroCount; ++i) {
		const char *macroNameText = get_compiled_macro_name(i);
		int entry = get_compiled_macro_entry(i);
		int flags = get_compiled_macro_flags(i);
		const char *keyspecText = get_compiled_macro_keyspec(i);
		int mode = get_compiled_macro_mode(i);
		int unitKind = get_compiled_macro_unit_kind(i);
		int tickMs = get_compiled_macro_tick_ms(i);
		std::string displayName = macroNameText != nullptr ? macroNameText : std::string();
		std::string macroKey = mrvmUpperKey(displayName);
		MacroRef ref;

		if (displayName.empty() || entry < 0) continue;
		removeMacroFromRegistryByKey(macroKey);

		ref.fileKey = fileKey;
		ref.displayName = displayName;
		ref.entryOffset = static_cast<std::size_t>(entry);
		ref.firstRunPending = true;
		ref.transientAttr = (flags & MACRO_ATTR_TRANS) != 0;
		ref.dumpAttr = (flags & MACRO_ATTR_DUMP) != 0;
		ref.permAttr = (flags & MACRO_ATTR_PERM) != 0;
		ref.assignedKeySpec = keyspecText != nullptr ? keyspecText : std::string();
		ref.fromMode = (mode == MACRO_MODE_DOS_SHELL || mode == MACRO_MODE_ALL) ? mode : MACRO_MODE_EDIT;
		ref.closureUnit = unitKind == MRMAC_UNIT_CLOSURE;
		ref.tickMs = tickMs > 0 ? static_cast<std::uint64_t>(tickMs) : 0;
		ref.hasAssignedKey = false;
		if (!ref.assignedKeySpec.empty()) ref.hasAssignedKey = mrvmParseAssignedKeySpec(ref.assignedKeySpec, ref.assignedKey);
		if (ref.closureUnit && ref.tickMs != 0) {
			MRRuntimeScheduledConsumerConfig config;
			const std::string macroSpec = newFile.displayName + "^" + displayName;
			config.intervalMs = ref.tickMs;
			config.macroSpec = macroSpec;
			config.entryName = displayName;
			config.closureId = macroSpec;
			ref.closureId = macroSpec;
			mrvmExecSessionsEnsureClosureState(g_runtimeEnv.runtimeKv, config.closureId, static_cast<int>(ref.tickMs));
			ref.scheduledConsumerId = registerRuntimeScheduledConsumer(config);
		}
		writeLoadedMacroByKey(macroKey, ref);
		appendMacroCatalogMacroOrder(macroKey);
		newFile.macroNames.push_back(macroKey);
	}

	writeLoadedMacroFileByKey(newFile);
	runtimeErrorLevel() = 0;
	logMacroProfileLine("Loaded macro file", newFile);
	if (loadedFileKey != nullptr) *loadedFileKey = fileKey;
	return true;
}

bool resolveDebugMacroSpec(const std::string &spec, std::string &macroKey, std::string &parameterString, LoadedMacroFile &file, std::string &errorMessage) {
	std::string filePart;
	std::string macroPart;
	std::string targetFileKey;
	MacroRef macroRef;

	macroKey.clear();
	parameterString.clear();
	errorMessage.clear();
	if (!mrvmParseRunMacroSpec(spec, filePart, macroPart, parameterString)) {
		errorMessage = "Debug macro specification is invalid.";
		return false;
	}
	macroKey = mrvmUpperKey(macroPart);
	if (macroKey.empty()) {
		errorMessage = "Debug macro name is empty.";
		return false;
	}
	if (!filePart.empty()) targetFileKey = resolveLoadedFileKeyForSpec(filePart);
	if (!filePart.empty() && targetFileKey.empty()) targetFileKey = mrvmMakeMacroFileKey(filePart);
	if (!readLoadedMacroByKey(macroKey, macroRef) || (!targetFileKey.empty() && macroRef.fileKey != targetFileKey)) {
		if (!filePart.empty()) {
			if (!loadMacroFileIntoRegistry(filePart, &targetFileKey)) {
				errorMessage = "Debug macro file could not be loaded: " + filePart;
				return false;
			}
		} else if (!loadMacroFileIntoRegistry(macroPart, &targetFileKey)) {
			errorMessage = "Debug macro is not loaded: " + macroKey;
			return false;
		}
		static_cast<void>(readLoadedMacroByKey(macroKey, macroRef));
	}
	if (macroRef.displayName.empty() || (!targetFileKey.empty() && macroRef.fileKey != targetFileKey)) {
		errorMessage = "Debug macro is not loaded: " + macroKey;
		return false;
	}
	if (!readLoadedMacroFileByKey(macroRef.fileKey, file)) {
		errorMessage = "Debug macro file is not loaded: " + macroRef.fileKey;
		return false;
	}
	return true;
}

bool prepareDebugMacroByKey(const std::string &macroKey, bool stopAtEntry, MacroRef &macroRef, LoadedMacroFile &file, std::vector<std::size_t> &breakpointOffsets, bool &firstRun, std::string &errorMessage) {
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);
	std::string source;
	std::vector<MRMacroSourceMapEntry> sourceMap;
	unsigned char *compiled = nullptr;
	size_t compiledSize = 0;

	errorMessage.clear();
	breakpointOffsets.clear();
	firstRun = false;
	if (normalizedMacroKey.empty()) {
		errorMessage = "Debug macro name is empty.";
		return false;
	}
	if (!readLoadedMacroByKey(normalizedMacroKey, macroRef)) {
		if (!loadMacroFileIntoRegistry(normalizedMacroKey, nullptr) || !readLoadedMacroByKey(normalizedMacroKey, macroRef)) {
			errorMessage = "Debug macro is not loaded: " + normalizedMacroKey;
			return false;
		}
	}
	if (!readLoadedMacroFileByKey(macroRef.fileKey, file)) {
		errorMessage = "Debug macro file is not loaded: " + macroRef.fileKey;
		return false;
	}
	if (!ensureLoadedFileResident(macroRef.fileKey) || !readLoadedMacroFileByKey(macroRef.fileKey, file) || file.bytecode.empty()) {
		errorMessage = "Debug macro bytecode is not resident: " + macroRef.fileKey;
		return false;
	}
	if (file.resolvedPath.empty() || !readTextFile(file.resolvedPath, source)) {
		errorMessage = "Debug macro source is not readable: " + macroRef.fileKey;
		return false;
	}
	compiled = compile_macro_code_with_source_map(source.c_str(), &compiledSize, collectSourceMapEntry, &sourceMap);
	if (compiled == nullptr) {
		const char *compileError = get_last_compile_error();

		errorMessage = compileError != nullptr ? compileError : "Debug macro source-map compile failed.";
		return false;
	}
	file.bytecode.assign(compiled, compiled + compiledSize);
	std::free(compiled);
	file.sourceMap = sourceMap;
	file.profile = mrvmAnalyzeBytecode(file.bytecode.data(), file.bytecode.size());
	writeLoadedMacroFileByKey(file);
	static_cast<void>(mrvmCollectDebugBreakpointOffsetsForLoadedFile(normalizedMacroKey, breakpointOffsets));
	if (stopAtEntry) breakpointOffsets.push_back(macroRef.entryOffset);
	std::sort(breakpointOffsets.begin(), breakpointOffsets.end());
	breakpointOffsets.erase(std::unique(breakpointOffsets.begin(), breakpointOffsets.end()), breakpointOffsets.end());
	firstRun = macroRef.firstRunPending;
	macroRef.firstRunPending = false;
	writeLoadedMacroByKey(normalizedMacroKey, macroRef);
	return true;
}

bool tryLoadIndexedMacroForKey(const TKey &pressed) {
	mrvmLogCalculatorHotkeyState("vm-indexed-enter", pressed);
	const std::vector<IndexedBoundMacroEntry> indexed = macroCatalogIndexedBindings();
	for (std::size_t i = 0; i < indexed.size(); ++i) {
		const IndexedBoundMacroEntry &entry = indexed[i];
		std::string fileKey;

		if (!mrvmBindingKeysEqual(entry.key, pressed)) continue;
		mrvmLogCalculatorHotkeyState("vm-indexed-match", pressed, entry.filePath);
		fileKey = mrvmMakeMacroFileKey(entry.filePath);
		if (loadedMacroFileExists(fileKey)) return true;
		static_cast<void>(markMacroCatalogIndexedWarmupAttempted(fileKey));
		if (loadMacroFileIntoRegistry(entry.filePath, nullptr)) return true;
	}
	return false;
}

bool unloadMacroFromRegistry(const std::string &macroName) {
	std::string macroKey = mrvmUpperKey(trimAscii(macroName));
	if (macroKey.empty()) return false;
	if (macroIsRunning(macroKey)) {
		runtimeErrorLevel() = 5006;
		return false;
	}
	if (!removeMacroFromRegistryByKey(macroKey)) return false;
	runtimeErrorLevel() = 0;
	return true;
}

} // namespace mrvm_runtime

bool mrvmCollectDebugBreakpointOffsetsForLoadedFile(const std::string &macroKey, std::vector<std::size_t> &breakpointOffsets) {
	MacroRef macroRef;
	LoadedMacroFile file;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	breakpointOffsets.clear();
	if (normalizedMacroKey.empty()) return false;
	if (!mrvm_runtime::readLoadedMacroByKey(normalizedMacroKey, macroRef)) return false;
	if (!mrvm_runtime::readLoadedMacroFileByKey(macroRef.fileKey, file)) return false;
	for (const std::string &fileMacroKey : file.macroNames) {
		std::vector<std::size_t> macroOffsets;

		if (!mrvmRuntimeDebuggerEnabledBreakpointOffsetsForMacro(g_runtimeEnv.runtimeKv, fileMacroKey, macroOffsets)) continue;
		breakpointOffsets.insert(breakpointOffsets.end(), macroOffsets.begin(), macroOffsets.end());
	}
	std::sort(breakpointOffsets.begin(), breakpointOffsets.end());
	breakpointOffsets.erase(std::unique(breakpointOffsets.begin(), breakpointOffsets.end()), breakpointOffsets.end());
	return !breakpointOffsets.empty();
}
