#include "MRVMDebugSession.hpp"

#include "vm/MRVMDebugExecution.hpp"
#include "vm/MRVMRuntimeCatalog.hpp"
#include "vm/MRVMRuntimeDebugger.hpp"
#include "vm/MRVMRuntimeGlobals.hpp"
#include "vm/MRVMRuntimeState.hpp"
#include "vm/MRVMExecSessions.hpp"
#include "vm/MRVMHash.hpp"
#include "vm/MRVMValue.hpp"
#include "mrmac.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <utility>

namespace {

struct DebugSessionVmHandle {
	std::unique_ptr<VirtualMachine> vm;
	MRMacroExecutionSession session;
	MRMacroDebugRunResult lastResult;
	MRVMDebugSessionCleanup cleanup;
	MRMacroExecutionRoute route;
	std::unique_ptr<BackgroundEditSession> stagedSession;
	MacroCommitConflictSnapshot conflictSnapshot;
	std::shared_ptr<std::atomic_bool> cancelFlag;
	std::shared_ptr<std::atomic_bool> pauseFlag;
	std::shared_ptr<std::atomic_bool> closeCleanupFlag;
	bool hasTemporaryBreakpoint;
	std::size_t temporaryBreakpointOffset;

	DebugSessionVmHandle()
	    : vm(), session(), lastResult(), cleanup(), route(MRMacroExecutionRoute::Debug), stagedSession(), conflictSnapshot(),
	      cancelFlag(std::make_shared<std::atomic_bool>(false)), pauseFlag(std::make_shared<std::atomic_bool>(false)), closeCleanupFlag(std::make_shared<std::atomic_bool>(false)),
	      hasTemporaryBreakpoint(false), temporaryBreakpointOffset(0) {
	}
};

struct DebugSessionControls {
	std::shared_ptr<std::atomic_bool> cancelFlag;
	std::shared_ptr<std::atomic_bool> pauseFlag;
	std::shared_ptr<std::atomic_bool> closeCleanupFlag;
};

static std::map<MRMacroExecutionSessionId, DebugSessionVmHandle> g_debugSessionVmHandles;
static std::mutex g_debugSessionControlMutex;
static std::map<MRMacroExecutionSessionId, DebugSessionControls> g_debugSessionControls;

struct DebugExecutionGuard {
	BackgroundEditSession *previousSession;
	std::shared_ptr<std::atomic_bool> previousCancelFlag;

	explicit DebugExecutionGuard(DebugSessionVmHandle &handle) noexcept
	    : previousSession(g_backgroundEditSession), previousCancelFlag(g_backgroundMacroCancelFlag) {
		if (handle.route == MRMacroExecutionRoute::StagedBackground) g_backgroundEditSession = handle.stagedSession.get();
		if (handle.route == MRMacroExecutionRoute::Background || handle.route == MRMacroExecutionRoute::StagedBackground) g_backgroundMacroCancelFlag = handle.cancelFlag;
	}

	~DebugExecutionGuard() {
		g_backgroundEditSession = previousSession;
		g_backgroundMacroCancelFlag = previousCancelFlag;
	}
};

static void registerDebugSessionControls(MRMacroExecutionSessionId sessionId, const DebugSessionVmHandle &handle) {
	std::lock_guard<std::mutex> lock(g_debugSessionControlMutex);
	DebugSessionControls controls;

	controls.cancelFlag = handle.cancelFlag;
	controls.pauseFlag = handle.pauseFlag;
	controls.closeCleanupFlag = handle.closeCleanupFlag;
	g_debugSessionControls[sessionId] = controls;
}

static void eraseDebugSessionControls(MRMacroExecutionSessionId sessionId) {
	std::lock_guard<std::mutex> lock(g_debugSessionControlMutex);

	g_debugSessionControls.erase(sessionId);
}

static bool sameSourceMapSpan(const MRMacroSourceMapEntry &left, const MRMacroSourceMapEntry &right) noexcept {
	return left.bytecodeOffset == right.bytecodeOffset && left.sourceStartOffset == right.sourceStartOffset && left.sourceEndOffset == right.sourceEndOffset && left.line == right.line && left.debuggableKind == right.debuggableKind;
}

static void appendTemporaryBreakpoint(const DebugSessionVmHandle &handle, std::vector<std::size_t> &breakpointOffsets) {
	if (!handle.hasTemporaryBreakpoint) return;
	breakpointOffsets.push_back(handle.temporaryBreakpointOffset);
	std::sort(breakpointOffsets.begin(), breakpointOffsets.end());
	breakpointOffsets.erase(std::unique(breakpointOffsets.begin(), breakpointOffsets.end()), breakpointOffsets.end());
}

static void consumeTemporaryBreakpoint(DebugSessionVmHandle &handle, const MRMacroDebugRunResult &result) noexcept {
	if (handle.hasTemporaryBreakpoint && result.paused && result.stopReason == mrdStopBreakpoint && result.instructionOffset == handle.temporaryBreakpointOffset)
		handle.hasTemporaryBreakpoint = false;
}

static void completeDebugSession(std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle, const MRMacroDebugRunResult &result) {
	MRMacroExecutionSession session = sessionHandle->second.session;
	const MRVMDebugSessionCleanup cleanup = sessionHandle->second.cleanup;
	const bool eraseDebuggerRuntime = sessionHandle->second.closeCleanupFlag != nullptr && sessionHandle->second.closeCleanupFlag->load(std::memory_order_acquire);

	eraseDebugSessionControls(session.sessionId);
	g_debugSessionVmHandles.erase(sessionHandle);
	if (eraseDebuggerRuntime && !cleanup.macroKey.empty()) static_cast<void>(mrvmEraseDebugRuntimeForMacro(cleanup.macroKey, nullptr));
	mrvmFinalizeDebugSession(session, result, cleanup);
}

static void appendDebugStackFrame(std::vector<MRMacroDebugStackFrame> &callStack, const std::string &macroKey, const std::string &sourcePath, std::size_t instructionOffset, MRMacroDebugStackFrameKind kind) {
	MRMacroDebugStackFrame frame;
	MRMacroSourceMapEntry sourceMapEntry;

	frame.macroKey = macroKey;
	frame.sourcePath = sourcePath;
	frame.instructionOffset = instructionOffset;
	frame.kind = kind;
	if (!macroKey.empty() && mrvmRuntimeCatalogSourceMapSpanForBytecodeOffset(g_runtimeEnv.runtimeKv, macroKey, instructionOffset, sourceMapEntry)) {
		frame.line = sourceMapEntry.line;
		frame.column = sourceMapEntry.column;
	}
	callStack.push_back(std::move(frame));
}

static MRMacroDebugVariableScope debugVariableScope(const std::string &name, const std::set<std::string> &closureNames, const std::set<std::string> &sessionNames) {
	if (closureNames.find(name) != closureNames.end()) return mrdVariableClosure;
	if (sessionNames.find(name) != sessionNames.end()) return mrdVariableSession;
	return mrdVariableLocal;
}

static const char *debugArrayTypeText(int type) noexcept {
	switch (type) {
		case TYPE_INT_ARRAY:
			return "int";
		case TYPE_STR_ARRAY:
			return "str";
		case TYPE_CHAR_ARRAY:
			return "char";
		case TYPE_REAL_ARRAY:
			return "real";
		case TYPE_HASH_ARRAY:
			return "hash";
		default:
			return "array";
	}
}

static std::string debugValueTextFor(const VirtualMachine::Value &value, const MRVMHashStore &localStore, const MRVMHashStore &globalStore) {
	std::ostringstream out;

	if (value.type == TYPE_HASH) {
		try {
			const MRVMHashStore &store = mrvmHashRuntimeStoreForValue(localStore, globalStore, value);

			out << "hash{" << store.keys(value.hashHandle).size() << " keys}";
		} catch (const std::exception &) {
			out << "hash{invalid}";
		}
		return out.str();
	}
	if (mrvmValueIsArrayType(value.type)) {
		out << debugArrayTypeText(value.type) << "[" << value.arrayValues.size() << "]";
		return out.str();
	}
	return mrvmValueAsString(value);
}

static std::string debugHashKeyDisplay(const std::string &key) {
	std::string display("[\"");

	for (char ch : key) {
		if (ch == '\\' || ch == '"') display.push_back('\\');
		if (ch == '\n') {
			display += "\\n";
			continue;
		}
		if (ch == '\r') {
			display += "\\r";
			continue;
		}
		if (ch == '\t') {
			display += "\\t";
			continue;
		}
		display.push_back(ch);
	}
	display += "\"]";
	return display;
}

static void appendDebugValueTree(std::vector<MRMacroDebugVariableSnapshot> &snapshots, const std::string &rootName, const std::string &displayName, const VirtualMachine::Value &value, MRMacroDebugVariableScope scope,
                                 std::vector<MRMacroDebugValuePathComponent> &path, int depth, const MRVMHashStore &localStore, const MRVMHashStore &globalStore, std::set<std::pair<bool, int>> &activeHashes) {
	MRMacroDebugVariableSnapshot snapshot;
	std::vector<std::string> hashKeys;
	bool hashValid = true;
	bool cycle = false;

	if (value.type == TYPE_HASH) {
		const std::pair<bool, int> identity(value.globalStorage, value.hashHandle);

		cycle = activeHashes.find(identity) != activeHashes.end();
		if (!cycle)
			try {
				hashKeys = mrvmHashRuntimeStoreForValue(localStore, globalStore, value).keys(value.hashHandle);
			} catch (const std::exception &) {
				hashValid = false;
			}
	}
	snapshot.name = rootName;
	snapshot.displayName = displayName;
	snapshot.type = value.type;
	snapshot.valueText = debugValueTextFor(value, localStore, globalStore);
	snapshot.scope = scope;
	snapshot.path = path;
	snapshot.depth = depth;
	snapshot.hasChildren = value.type == TYPE_HASH ? hashValid && !hashKeys.empty() : mrvmValueIsArrayType(value.type) && !value.arrayValues.empty();
	snapshot.cycleReference = cycle;
	if (cycle) snapshot.valueText += " (cycle)";
	snapshots.push_back(snapshot);
	if (cycle || !hashValid) return;

	if (value.type == TYPE_HASH) {
		const std::pair<bool, int> identity(value.globalStorage, value.hashHandle);
		const MRVMHashStore &store = mrvmHashRuntimeStoreForValue(localStore, globalStore, value);

		activeHashes.insert(identity);
		for (const std::string &key : hashKeys) {
			MRMacroDebugValuePathComponent component;

			component.kind = mrdValueHashKey;
			component.key = key;
			path.push_back(component);
			appendDebugValueTree(snapshots, rootName, debugHashKeyDisplay(key), store.read(value.hashHandle, key), scope, path, depth + 1, localStore, globalStore, activeHashes);
			path.pop_back();
		}
		activeHashes.erase(identity);
		return;
	}
	if (mrvmValueIsArrayType(value.type))
		for (std::size_t index = 0; index < value.arrayValues.size(); ++index) {
			MRMacroDebugValuePathComponent component;

			component.kind = mrdValueArrayIndex;
			component.index = static_cast<int>(index + 1);
			path.push_back(component);
			appendDebugValueTree(snapshots, rootName, "[" + std::to_string(index + 1) + "]", value.arrayValues[index], scope, path, depth + 1, localStore, globalStore, activeHashes);
			path.pop_back();
		}
}

static bool parseDebugScalarValue(int type, const std::string &text, VirtualMachine::Value &value, std::string &errorMessage) {
	char *end = nullptr;

	switch (type) {
		case TYPE_INT: {
			errno = 0;
			const long parsed = std::strtol(text.c_str(), &end, 10);

			if (end == text.c_str() || *end != '\0' || errno == ERANGE || parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
				errorMessage = "Expected an integer value.";
				return false;
			}
			value = mrvmMakeInt(static_cast<int>(parsed));
			return true;
		}
		case TYPE_REAL: {
			errno = 0;
			const double parsed = std::strtod(text.c_str(), &end);

			if (end == text.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(parsed)) {
				errorMessage = "Expected a finite real value.";
				return false;
			}
			value = mrvmMakeReal(parsed);
			return true;
		}
		case TYPE_STR:
			try {
				mrvmEnforceStringLength(text);
			} catch (const std::exception &error) {
				errorMessage = error.what();
				return false;
			}
			value = mrvmMakeString(text);
			return true;
		case TYPE_CHAR:
			if (text.size() > 1) {
				errorMessage = "Expected one character.";
				return false;
			}
			value = mrvmMakeChar(text.empty() ? 0 : static_cast<unsigned char>(text[0]));
			return true;
		default:
			errorMessage = "Expected a scalar debugger value.";
			return false;
	}
}

static bool makeDebugValue(int type, const std::string &text, bool globalStorage, MRVMHashStore &localStore, MRVMHashStore &globalStore, VirtualMachine::Value &value, std::string &errorMessage) {
	if (type == TYPE_HASH) {
		MRVMHashStore &store = globalStorage ? globalStore : localStore;

		value = mrvmMakeHash(store.createHash(), globalStorage);
		return true;
	}
	if (mrvmValueIsArrayType(type)) {
		value = mrvmMakeArrayValue(mrvmArrayElementTypeForArrayType(type));
		value.globalStorage = globalStorage;
		return true;
	}
	if (!parseDebugScalarValue(type, text, value, errorMessage)) return false;
	value.globalStorage = globalStorage;
	return true;
}

static bool applyDebugMutationAtValue(VirtualMachine::Value &value, const MRMacroDebugValueMutation &mutation, std::size_t pathIndex, MRVMHashStore &localStore, MRVMHashStore &globalStore, std::string &errorMessage) {
	if (pathIndex == mutation.target.path.size()) {
		VirtualMachine::Value replacement;

		if (value.type != mutation.target.type) {
			errorMessage = "Value no longer matches the debugger projection.";
			return false;
		}
		switch (mutation.action) {
			case mrdValueSetScalar:
				if (!parseDebugScalarValue(value.type, mutation.valueText, replacement, errorMessage)) return false;
				replacement.globalStorage = value.globalStorage;
				value = replacement;
				return true;
			case mrdValueAddHashEntry:
				if (value.type != TYPE_HASH) {
					errorMessage = "Hash insertion requires a hash.";
					return false;
				}
				try {
					if (mrvmHashContainsValue(localStore, globalStore, value, mutation.key)) {
						errorMessage = "Hash key already exists.";
						return false;
					}
					if (!makeDebugValue(mutation.valueType, mutation.valueText, value.globalStorage, localStore, globalStore, replacement, errorMessage)) return false;
					mrvmHashWriteValue(localStore, globalStore, value, mutation.key, replacement);
				} catch (const std::exception &error) {
					errorMessage = error.what();
					return false;
				}
				return true;
			case mrdValueAppendArrayElement:
				if (!mrvmValueIsArrayType(value.type)) {
					errorMessage = "Array append requires an array.";
					return false;
				}
				if (!makeDebugValue(value.arrayElementType, mutation.valueText, value.globalStorage, localStore, globalStore, replacement, errorMessage)) return false;
				try {
					mrvmArrayWriteValue(value, static_cast<int>(value.arrayValues.size() + 1), replacement, localStore, globalStore);
				} catch (const std::exception &error) {
					errorMessage = error.what();
					return false;
				}
				return true;
			case mrdValueEraseElement:
				errorMessage = "A root variable cannot be erased.";
				return false;
			case mrdValueRenameHashKey:
				errorMessage = "A root variable cannot be renamed.";
				return false;
		}
	}

	const MRMacroDebugValuePathComponent &component = mutation.target.path[pathIndex];
	const bool directTarget = pathIndex + 1 == mutation.target.path.size();
	if (component.kind == mrdValueHashKey) {
		VirtualMachine::Value child;

		if (value.type != TYPE_HASH) {
			errorMessage = "Debugger path no longer refers to a hash.";
			return false;
		}
		try {
			if (!mrvmHashContainsValue(localStore, globalStore, value, component.key)) {
				errorMessage = "Hash key no longer exists.";
				return false;
			}
			if (directTarget && mutation.action == mrdValueEraseElement) {
				mrvmHashEraseValue(localStore, globalStore, value, component.key);
				return true;
			}
			if (directTarget && mutation.action == mrdValueRenameHashKey) {
				if (mutation.key != component.key && mrvmHashContainsValue(localStore, globalStore, value, mutation.key)) {
					errorMessage = "Hash key already exists.";
					return false;
				}
				if (mutation.key == component.key) return true;
				child = mrvmHashReadValue(localStore, globalStore, value, component.key);
				mrvmHashWriteValue(localStore, globalStore, value, mutation.key, child);
				mrvmHashEraseValue(localStore, globalStore, value, component.key);
				return true;
			}
			child = mrvmHashReadValue(localStore, globalStore, value, component.key);
			if (!applyDebugMutationAtValue(child, mutation, pathIndex + 1, localStore, globalStore, errorMessage)) return false;
			mrvmHashWriteValue(localStore, globalStore, value, component.key, child);
			return true;
		} catch (const std::exception &error) {
			errorMessage = error.what();
			return false;
		}
	}
	if (!mrvmValueIsArrayType(value.type) || component.index <= 0 || static_cast<std::size_t>(component.index) > value.arrayValues.size()) {
		errorMessage = "Debugger path no longer refers to an array element.";
		return false;
	}
	if (directTarget && mutation.action == mrdValueEraseElement) {
		value.arrayValues.erase(value.arrayValues.begin() + component.index - 1);
		return true;
	}
	if (directTarget && mutation.action == mrdValueRenameHashKey) {
		errorMessage = "Array elements cannot be renamed.";
		return false;
	}
	return applyDebugMutationAtValue(value.arrayValues[static_cast<std::size_t>(component.index - 1)], mutation, pathIndex + 1, localStore, globalStore, errorMessage);
}

}

std::string VirtualMachine::debugValueText(const Value &value) const {
	return debugValueTextFor(value, *mHashStore, g_runtimeEnv.runtimeKv.globalStore());
}

void VirtualMachine::appendDebugVariables(MRMacroDebugRunResult &result) const {
	MRVMHashStore &globalStore = g_runtimeEnv.runtimeKv.globalStore();

	for (const std::pair<const std::string, Value> &entry : variables) {
		std::vector<MRMacroDebugValuePathComponent> path;
		std::set<std::pair<bool, int>> activeHashes;

		appendDebugValueTree(result.variables, entry.first, entry.first, entry.second, debugVariableScope(entry.first, mClosureVariableNames, mSessionVariableNames), path, 0, *mHashStore, globalStore, activeHashes);
	}
	for (const std::string &key : mrvmRuntimeGlobalOrderValues(g_runtimeEnv.runtimeKv)) {
		MRVMRuntimeGlobalEntry entry;
		std::vector<MRMacroDebugValuePathComponent> path;
		std::set<std::pair<bool, int>> activeHashes;

		if (!mrvmRuntimeGlobalRead(g_runtimeEnv.runtimeKv, key, entry)) continue;
		appendDebugValueTree(result.variables, key, key, entry.value, mrdVariableAppGlobal, path, 0, globalStore, globalStore, activeHashes);
	}
}

bool VirtualMachine::mutateDebugValue(const MRMacroDebugValueMutation &mutation, std::vector<MRMacroDebugVariableSnapshot> &updatedVariables, std::string &errorMessage) {
	Value rootValue;
	Value storedValue;
	Value *root = nullptr;
	std::map<std::string, Value>::iterator local;
	MRVMRuntimeGlobalEntry global;
	MRMacroDebugVariableScope actualScope = mrdVariableLocal;

	updatedVariables.clear();
	errorMessage.clear();
	if (!debugState.paused) {
		errorMessage = "Debug session is not paused.";
		return false;
	}
	if (mutation.target.name.empty()) {
		errorMessage = "Debugger value has no root variable.";
		return false;
	}
	if (mutation.target.scope == mrdVariableAppGlobal) {
		if (!mrvmRuntimeGlobalRead(g_runtimeEnv.runtimeKv, mutation.target.name, global)) {
			errorMessage = "App global no longer exists.";
			return false;
		}
		rootValue = global.value;
		root = &rootValue;
		actualScope = mrdVariableAppGlobal;
	} else {
		local = variables.find(mutation.target.name);
		if (local == variables.end()) {
			errorMessage = "Variable no longer exists in the paused debug session.";
			return false;
		}
		actualScope = debugVariableScope(local->first, mClosureVariableNames, mSessionVariableNames);
		root = &local->second;
	}
	if (actualScope != mutation.target.scope) {
		errorMessage = "Variable no longer matches the debugger scope.";
		return false;
	}
	if (actualScope == mrdVariableClosure || actualScope == mrdVariableSession) {
		MRVMHashStore stagedStore;
		Value stagedValue;

		try {
			stagedValue = mrvmHashCopyValueForStore(*root, *mHashStore, g_runtimeEnv.runtimeKv.globalStore(), stagedStore, false);
		} catch (const std::exception &error) {
			errorMessage = error.what();
			return false;
		}
		if (!applyDebugMutationAtValue(stagedValue, mutation, 0, stagedStore, stagedStore, errorMessage)) return false;
		if (actualScope == mrdVariableClosure && !mClosureId.empty()) {
			if (!mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, mClosureId, mutation.target.name, stagedValue, stagedStore, &storedValue)) {
				errorMessage = "Closure variable could not be stored.";
				return false;
			}
		} else if (actualScope == mrdVariableSession && mExecutionSessionId != 0) {
			if (!mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, mExecutionSessionId, mutation.target.name, stagedValue, stagedStore, &storedValue)) {
				errorMessage = "Session variable could not be stored.";
				return false;
			}
		} else {
			errorMessage = "Debugger variable has no persistent execution context.";
			return false;
		}
		*root = std::move(storedValue);
	} else if (!applyDebugMutationAtValue(*root, mutation, 0, *mHashStore, g_runtimeEnv.runtimeKv.globalStore(), errorMessage))
		return false;
	if (actualScope == mrdVariableAppGlobal)
		mrvmRuntimeGlobalWrite(g_runtimeEnv.runtimeKv, mutation.target.name, global.type, *root);
	{
		MRMacroDebugRunResult snapshot;

		appendDebugVariables(snapshot);
		updatedVariables = std::move(snapshot.variables);
	}
	return true;
}

void VirtualMachine::DebugExecution::appendCallStack(MRMacroDebugRunResult &result) const {
	static constexpr std::size_t kCallInstructionSize = sizeof(unsigned char) + sizeof(int);

	result.callStack.clear();
	if (!result.paused) return;
	appendDebugStackFrame(result.callStack, vm.debugState.macroKey, vm.debugState.sourcePath, result.instructionOffset, mrdStackFrameCurrent);
	for (std::vector<std::size_t>::const_reverse_iterator frame = vm.debugState.callStack.rbegin(); frame != vm.debugState.callStack.rend(); ++frame) {
		const std::size_t callInstructionOffset = *frame >= kCallInstructionSize ? *frame - kCallInstructionSize : *frame;

		appendDebugStackFrame(result.callStack, vm.debugState.macroKey, vm.debugState.sourcePath, callInstructionOffset, mrdStackFrameCall);
	}
}

void VirtualMachine::DebugExecution::appendParentCallStack(MRMacroDebugRunResult &result, std::size_t parentInstructionOffset) const {
	static constexpr std::size_t kCallInstructionSize = sizeof(unsigned char) + sizeof(int);

	if (!result.paused) return;
	appendDebugStackFrame(result.callStack, vm.debugState.macroKey, vm.debugState.sourcePath, parentInstructionOffset, mrdStackFrameRunMacro);
	for (std::vector<std::size_t>::const_reverse_iterator frame = vm.debugState.callStack.rbegin(); frame != vm.debugState.callStack.rend(); ++frame) {
		const std::size_t callInstructionOffset = *frame >= kCallInstructionSize ? *frame - kCallInstructionSize : *frame;

		appendDebugStackFrame(result.callStack, vm.debugState.macroKey, vm.debugState.sourcePath, callInstructionOffset, mrdStackFrameCall);
	}
}

MRMacroDebugRunResult mrvmRunBytecodeDebugAt(const unsigned char *bytecode, std::size_t length, std::size_t entryOffset, const std::string &macroName, const std::vector<std::size_t> &breakpointOffsets) {
	VirtualMachine vm;

	return vm.executeDebugAt(bytecode, length, entryOffset, std::string(), macroName, breakpointOffsets);
}

MRMacroDebugRunResult mrvmStartDebugSessionAt(const unsigned char *bytecode, std::size_t length, std::size_t entryOffset, const std::string &macroName, const MRMacroExecutionOwner &owner, const std::vector<std::size_t> &breakpointOffsets, MRMacroExecutionSession *sessionOut, bool firstRun,
	                                           const std::string &macroKey, const std::string &sourcePath, const std::string &parameterString, MRMacroExecutionRoute route, const MRMacroStagedExecutionInput *stagedInput,
	                                           const MacroCommitConflictSnapshot *conflictSnapshot, bool automaticContinueFromEntry, bool hasTemporaryBreakpoint, std::size_t temporaryBreakpointOffset) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MRMacroExecutionSession session = createMacroExecutionSession(macroName, route, owner);
	DebugSessionVmHandle handle;
	MRMacroDebugRunResult result;

	handle.route = route;
	handle.hasTemporaryBreakpoint = hasTemporaryBreakpoint;
	handle.temporaryBreakpointOffset = temporaryBreakpointOffset;
	handle.session = session;
	handle.vm = std::make_unique<VirtualMachine>();
	handle.vm->setExecutionSessionContext(session.sessionId);
	handle.vm->setDebugPauseRequest(handle.pauseFlag);
	if (route == MRMacroExecutionRoute::StagedBackground) {
		if (stagedInput == nullptr || conflictSnapshot == nullptr) {
			result.stopReason = mrdStopError;
			result.hadError = true;
			result.logLines.push_back("VM Error: staged debug session has no captured edit state.");
			mrvmFinalizeDebugSession(session, result, handle.cleanup);
			if (sessionOut != nullptr) *sessionOut = session;
			return result;
		}
		handle.stagedSession = std::make_unique<BackgroundEditSession>();
		mrvmInitializeBackgroundEditSession(*handle.stagedSession, *stagedInput);
		handle.conflictSnapshot = *conflictSnapshot;
	}
	{
		DebugExecutionGuard executionGuard(handle);

		result = handle.vm->executeDebugAt(bytecode, length, entryOffset, parameterString, macroName, breakpointOffsets, firstRun, macroKey, sourcePath);
	}
	if (automaticContinueFromEntry && result.paused && result.stopReason == mrdStopBreakpoint && result.instructionOffset == entryOffset) result.stopReason = mrdStopBudget;
	consumeTemporaryBreakpoint(handle, result);

	if (result.paused) {
		session.state = result.stopReason == mrdStopBudget ? MRMacroExecutionState::Running : MRMacroExecutionState::Yielded;
		handle.session = session;
		handle.lastResult = result;
		registerDebugSessionControls(session.sessionId, handle);
		g_debugSessionVmHandles[session.sessionId] = std::move(handle);
		notifyMacroExecutionSessionChanged();
	} else {
		mrvmFinalizeDebugSession(session, result, handle.cleanup);
	}
	if (sessionOut != nullptr) *sessionOut = session;
	return result;
}

bool mrvmConfigureDebugSessionCleanup(MRMacroExecutionSessionId sessionId, const MRVMDebugSessionCleanup &cleanup) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);

	if (sessionHandle == g_debugSessionVmHandles.end()) return false;
	sessionHandle->second.cleanup = cleanup;
	return true;
}

bool mrvmDebugWatchSnapshots(MRMacroExecutionSessionId sessionId, const std::string &macroKey, std::vector<MRMacroDebugWatchSnapshot> &snapshots) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	std::vector<MRMacroDebuggerWatch> watches;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle;

	snapshots.clear();
	if (normalizedMacroKey.empty()) return false;
	mrvmRuntimeDebuggerWatchesForMacro(g_runtimeEnv.runtimeKv, normalizedMacroKey, watches);
	if (watches.empty()) return false;
	sessionHandle = g_debugSessionVmHandles.find(sessionId);
	for (const MRMacroDebuggerWatch &watch : watches) {
		MRMacroDebugWatchSnapshot snapshot;

		snapshot.expression = watch.expression;
		if (!watch.enabled)
			snapshot.errorText = "Watch is disabled.";
		else if (sessionId == 0 || sessionHandle == g_debugSessionVmHandles.end() || sessionHandle->second.vm == nullptr || !sessionHandle->second.vm->hasPausedDebug())
			snapshot.errorText = "No live debug session.";
		else
			snapshot = sessionHandle->second.vm->evaluateDebugWatchExpression(watch.expression);
		snapshot.enabled = watch.enabled;
		snapshots.push_back(snapshot);
	}
	return true;
}

bool mrvmEvaluateDebugExpression(MRMacroExecutionSessionId sessionId, const std::string &expression, MRMacroDebugWatchSnapshot &snapshot, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle;

	snapshot = MRMacroDebugWatchSnapshot();
	if (errorMessage != nullptr) errorMessage->clear();
	if (sessionId == 0) {
		if (errorMessage != nullptr) *errorMessage = "No live debug session.";
		return false;
	}
	sessionHandle = g_debugSessionVmHandles.find(sessionId);
	if (sessionHandle == g_debugSessionVmHandles.end() || sessionHandle->second.vm == nullptr || !sessionHandle->second.vm->hasPausedDebug()) {
		if (errorMessage != nullptr) *errorMessage = "Debug session is not paused.";
		return false;
	}
	snapshot = sessionHandle->second.vm->evaluateDebugWatchExpression(expression);
	return true;
}

bool mrvmWriteDebugScalarVariable(MRMacroExecutionSessionId sessionId, const MRMacroDebugVariableSnapshot &variable, const std::string &valueText, std::vector<MRMacroDebugVariableSnapshot> &updatedVariables, std::string *errorMessage) {
	MRMacroDebugValueMutation mutation;

	mutation.action = mrdValueSetScalar;
	mutation.target = variable;
	mutation.valueText = valueText;
	return mrvmMutateDebugValue(sessionId, mutation, updatedVariables, errorMessage);
}

bool mrvmMutateDebugValue(MRMacroExecutionSessionId sessionId, const MRMacroDebugValueMutation &mutation, std::vector<MRMacroDebugVariableSnapshot> &updatedVariables, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle;
	std::string localError;

	updatedVariables.clear();
	if (errorMessage != nullptr) errorMessage->clear();
	if (sessionId == 0) {
		if (errorMessage != nullptr) *errorMessage = "No live debug session.";
		return false;
	}
	sessionHandle = g_debugSessionVmHandles.find(sessionId);
	if (sessionHandle == g_debugSessionVmHandles.end() || sessionHandle->second.vm == nullptr) {
		if (errorMessage != nullptr) *errorMessage = "No live debug session.";
		return false;
	}
	if (!sessionHandle->second.vm->mutateDebugValue(mutation, updatedVariables, localError)) {
		if (errorMessage != nullptr) *errorMessage = localError;
		return false;
	}
	sessionHandle->second.lastResult.variables = updatedVariables;
	notifyMacroExecutionSessionChanged();
	return true;
}

MRMacroDebugRunResult mrvmContinueDebugSession(MRMacroExecutionSessionId sessionId, const std::vector<std::size_t> &breakpointOffsets) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MRMacroDebugRunResult result;
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);
	std::vector<std::size_t> effectiveBreakpointOffsets = breakpointOffsets;

	if (sessionHandle == g_debugSessionVmHandles.end() || sessionHandle->second.vm == nullptr) return result;
	appendTemporaryBreakpoint(sessionHandle->second, effectiveBreakpointOffsets);
	sessionHandle->second.session.state = MRMacroExecutionState::Running;
	result = sessionHandle->second.vm->continueDebug(effectiveBreakpointOffsets);
	consumeTemporaryBreakpoint(sessionHandle->second, result);
	if (result.paused) {
		sessionHandle->second.session.state = MRMacroExecutionState::Yielded;
		sessionHandle->second.lastResult = result;
		notifyMacroExecutionSessionChanged();
		return result;
	}
	completeDebugSession(sessionHandle, result);
	return result;
}

MRMacroDebugRunResult mrvmStepDebugSession(MRMacroExecutionSessionId sessionId, const std::vector<std::size_t> &breakpointOffsets, MRMacroDebugStepMode mode) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MRMacroDebugRunResult result;
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);
	std::vector<std::size_t> effectiveBreakpointOffsets = breakpointOffsets;

	if (sessionHandle == g_debugSessionVmHandles.end() || sessionHandle->second.vm == nullptr) return result;
	appendTemporaryBreakpoint(sessionHandle->second, effectiveBreakpointOffsets);
	sessionHandle->second.session.state = MRMacroExecutionState::Running;
	result = sessionHandle->second.vm->stepDebug(effectiveBreakpointOffsets, mode);
	consumeTemporaryBreakpoint(sessionHandle->second, result);
	if (result.paused) {
		sessionHandle->second.session.state = MRMacroExecutionState::Yielded;
		sessionHandle->second.lastResult = result;
		notifyMacroExecutionSessionChanged();
		return result;
	}
	completeDebugSession(sessionHandle, result);
	return result;
}

MRMacroDebugRunResult mrvmContinueDebugMacroByName(MRMacroExecutionSessionId sessionId, const std::string &macroKey, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MRMacroDebugRunResult result;
	std::vector<std::size_t> breakpointOffsets;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	if (errorMessage != nullptr) errorMessage->clear();
	if (sessionId == 0) {
		result.stopReason = mrdStopError;
		result.hadError = true;
		result.logLines.push_back("VM Error: debug session is not active.");
		if (errorMessage != nullptr) *errorMessage = "Debug session is not active.";
		return result;
	}
	if (normalizedMacroKey.empty()) {
		result.stopReason = mrdStopError;
		result.hadError = true;
		result.logLines.push_back("VM Error: debug macro name is empty.");
		if (errorMessage != nullptr) *errorMessage = "Debug macro name is empty.";
		return result;
	}
	{
		std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::const_iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);

		if (sessionHandle == g_debugSessionVmHandles.end()) {
			result.stopReason = mrdStopError;
			result.hadError = true;
			result.logLines.push_back("VM Error: debug session is not paused.");
			if (errorMessage != nullptr) *errorMessage = "Debug session is not paused.";
			return result;
		}
		const std::string currentMacroKey = !sessionHandle->second.lastResult.macroKey.empty() ? sessionHandle->second.lastResult.macroKey : normalizedMacroKey;

		if (!mrvmCollectDebugBreakpointOffsetsForLoadedFile(currentMacroKey, breakpointOffsets)) breakpointOffsets.clear();
	}
	return mrvmContinueDebugSession(sessionId, breakpointOffsets);
}

bool mrvmScheduleDebugMacroContinue(MRMacroExecutionSessionId sessionId, const std::string &macroKey, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);

	if (errorMessage != nullptr) errorMessage->clear();
	if (sessionId == 0 || macroKey.empty() || sessionHandle == g_debugSessionVmHandles.end() || sessionHandle->second.vm == nullptr || sessionHandle->second.session.state != MRMacroExecutionState::Yielded) {
		if (errorMessage != nullptr) *errorMessage = "Debug session is not paused.";
		return false;
	}
	sessionHandle->second.session.state = MRMacroExecutionState::Running;
	sessionHandle->second.lastResult.stopReason = mrdStopBudget;
	sessionHandle->second.lastResult.paused = true;
	notifyMacroExecutionSessionChanged();
	return true;
}

bool mrvmRequestDebugPause(MRMacroExecutionSessionId sessionId, std::string *errorMessage) {
	std::shared_ptr<std::atomic_bool> pauseFlag;

	if (errorMessage != nullptr) errorMessage->clear();
	{
		std::lock_guard<std::mutex> lock(g_debugSessionControlMutex);
		const std::map<MRMacroExecutionSessionId, DebugSessionControls>::const_iterator controls = g_debugSessionControls.find(sessionId);

		if (controls != g_debugSessionControls.end()) pauseFlag = controls->second.pauseFlag;
	}
	if (sessionId == 0 || pauseFlag == nullptr) {
		if (errorMessage != nullptr) *errorMessage = "Debug session is not running.";
		return false;
	}
	pauseFlag->store(true, std::memory_order_release);
	return true;
}

bool mrvmPumpDebugSession(MRMacroExecutionSessionId sessionId, const std::string &macroKey, MRMacroDebugRunResult &result, std::string *errorMessage) {
	static constexpr std::size_t kDebugIdleInstructionBudget = 256;
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);
	std::vector<std::size_t> breakpointOffsets;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	result = MRMacroDebugRunResult();
	if (errorMessage != nullptr) errorMessage->clear();
	if (sessionId == 0 || normalizedMacroKey.empty() || sessionHandle == g_debugSessionVmHandles.end() || sessionHandle->second.vm == nullptr || sessionHandle->second.session.state != MRMacroExecutionState::Running) return false;
	if (!mrvmCollectDebugBreakpointOffsetsForLoadedFile(!sessionHandle->second.lastResult.macroKey.empty() ? sessionHandle->second.lastResult.macroKey : normalizedMacroKey, breakpointOffsets)) breakpointOffsets.clear();
	appendTemporaryBreakpoint(sessionHandle->second, breakpointOffsets);
	result = sessionHandle->second.vm->continueDebug(breakpointOffsets, kDebugIdleInstructionBudget);
	consumeTemporaryBreakpoint(sessionHandle->second, result);
	if (result.stopReason == mrdStopBudget) {
		sessionHandle->second.lastResult = result;
		return true;
	}
	if (result.paused) {
		sessionHandle->second.session.state = MRMacroExecutionState::Yielded;
		sessionHandle->second.lastResult = result;
		notifyMacroExecutionSessionChanged();
		return true;
	}
	completeDebugSession(sessionHandle, result);
	return true;
}

MRMacroExecutionRoute mrvmDebugSessionRoute(MRMacroExecutionSessionId sessionId) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	const std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::const_iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);

	return sessionHandle == g_debugSessionVmHandles.end() ? MRMacroExecutionRoute::Unknown : sessionHandle->second.route;
}

bool mrvmDebugSessionWorkerTaskContext(MRMacroExecutionSessionId sessionId, MRMacroExecutionRoute &route, int &bufferId, std::size_t &baseVersion) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	const std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::const_iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);

	route = MRMacroExecutionRoute::Unknown;
	bufferId = 0;
	baseVersion = 0;
	if (sessionHandle == g_debugSessionVmHandles.end()) return false;
	route = sessionHandle->second.route;
	if (sessionHandle->second.session.owner.hasBuffer) bufferId = sessionHandle->second.session.owner.bufferId;
	if (sessionHandle->second.stagedSession != nullptr) baseVersion = sessionHandle->second.stagedSession->transaction.baseVersion();
	return true;
}

bool mrvmAssignDebugSessionWorkerTask(MRMacroExecutionSessionId sessionId, std::uint64_t taskId) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);

	if (sessionId == 0 || taskId == 0 || sessionHandle == g_debugSessionVmHandles.end()) return false;
	if (sessionHandle->second.session.taskId == taskId) return true;
	if (sessionHandle->second.session.taskId != 0) return false;
	sessionHandle->second.session.taskId = taskId;
	trackMacroExecutionSession(sessionHandle->second.session);
	notifyMacroExecutionSessionChanged();
	return true;
}

bool mrvmAcceptDebugSessionWorkerTaskResult(MRMacroExecutionSessionId sessionId, std::uint64_t taskId) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MRMacroExecutionSession activeSession;
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle;

	if (sessionId == 0 || taskId == 0 || !mrvmTakeActiveMacroExecutionSessionForTask(taskId, activeSession)) return false;
	if (activeSession.sessionId != sessionId) {
		mrvmStoreActiveMacroExecutionSession(activeSession);
		return false;
	}
	sessionHandle = g_debugSessionVmHandles.find(sessionId);
	if (sessionHandle == g_debugSessionVmHandles.end()) {
		notifyMacroExecutionSessionChanged();
		return true;
	}
	if (sessionHandle->second.session.taskId != taskId) {
		mrvmStoreActiveMacroExecutionSession(activeSession);
		return false;
	}
	sessionHandle->second.session.taskId = 0;
	notifyMacroExecutionSessionChanged();
	return true;
}

MRMacroDebugWorkerResult mrvmRunDebugSessionWorkerAction(MRMacroExecutionSessionId sessionId, const std::string &macroKey, MRMacroDebugWorkerAction action, std::size_t instructionBudget,
	                                                     std::shared_ptr<std::atomic_bool> workerCancelFlag) {
	static constexpr int kMaxStatementStepOpcodes = 4096;
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MRMacroDebugWorkerResult workerResult;
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);
	std::vector<std::size_t> breakpointOffsets;
	MRMacroSourceMapEntry startingSpan;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	if (sessionHandle == g_debugSessionVmHandles.end() || sessionHandle->second.vm == nullptr || normalizedMacroKey.empty()) {
		workerResult.errorMessage = "Debug session is not active.";
		return workerResult;
	}
	DebugSessionVmHandle &handle = sessionHandle->second;
	workerResult.route = handle.route;
	if (handle.route != MRMacroExecutionRoute::Background && handle.route != MRMacroExecutionRoute::StagedBackground) {
		workerResult.errorMessage = "Debug session is not assigned to a worker route.";
		return workerResult;
	}
	if (workerCancelFlag != nullptr) {
		std::lock_guard<std::mutex> controlsLock(g_debugSessionControlMutex);
		const std::map<MRMacroExecutionSessionId, DebugSessionControls>::iterator controls = g_debugSessionControls.find(sessionId);

		if (handle.cancelFlag != nullptr && handle.cancelFlag->load(std::memory_order_acquire)) workerCancelFlag->store(true, std::memory_order_release);
		handle.cancelFlag = workerCancelFlag;
		if (controls != g_debugSessionControls.end()) controls->second.cancelFlag = workerCancelFlag;
	}
	const std::string currentMacroKey = !handle.lastResult.macroKey.empty() ? handle.lastResult.macroKey : normalizedMacroKey;
	if (!mrvmCollectDebugBreakpointOffsetsForLoadedFile(currentMacroKey, breakpointOffsets)) breakpointOffsets.clear();
	appendTemporaryBreakpoint(handle, breakpointOffsets);
	handle.session.state = MRMacroExecutionState::Running;
	{
		DebugExecutionGuard executionGuard(handle);

		if (action == mrdWorkerContinue)
			workerResult.debugResult = handle.vm->continueDebug(breakpointOffsets, std::max<std::size_t>(instructionBudget, 1));
		else {
			const MRMacroDebugStepMode mode = action == mrdWorkerStepOver ? mrdStepOver : (action == mrdWorkerStepOut ? mrdStepOut : mrdStepInto);
			const bool statementStep = action != mrdWorkerStepOut;
			const bool haveStartingSpan = statementStep && handle.lastResult.paused &&
			                              mrvmRuntimeCatalogSourceMapSpanForBytecodeOffset(g_runtimeEnv.runtimeKv, currentMacroKey, handle.lastResult.instructionOffset, startingSpan);

			if (!statementStep || !haveStartingSpan)
				workerResult.debugResult = handle.vm->stepDebug(breakpointOffsets, mode);
			else
				for (int stepCount = 0; stepCount < kMaxStatementStepOpcodes; ++stepCount) {
					MRMacroSourceMapEntry currentSpan;

					workerResult.debugResult = handle.vm->stepDebug(breakpointOffsets, mode);
					if (!workerResult.debugResult.paused || workerResult.debugResult.hadError || workerResult.debugResult.cancelled || workerResult.debugResult.stopReason == mrdStopBreakpoint) break;
					if (!mrvmRuntimeCatalogSourceMapSpanForBytecodeOffset(g_runtimeEnv.runtimeKv, currentMacroKey, workerResult.debugResult.instructionOffset, currentSpan)) continue;
					if (!sameSourceMapSpan(startingSpan, currentSpan)) break;
					if (stepCount + 1 == kMaxStatementStepOpcodes) {
						workerResult.debugResult.stopReason = mrdStopError;
						workerResult.debugResult.hadError = true;
						workerResult.debugResult.paused = true;
						workerResult.debugResult.logLines.push_back("VM Error: debug statement step did not leave the current source span.");
						workerResult.errorMessage = "Debug statement step did not leave the current source span.";
					}
				}
		}
	}
	consumeTemporaryBreakpoint(handle, workerResult.debugResult);
	workerResult.accepted = true;
	if (workerResult.debugResult.stopReason == mrdStopBudget) {
		handle.session.state = MRMacroExecutionState::Running;
		handle.lastResult = workerResult.debugResult;
		return workerResult;
	}
	if (workerResult.debugResult.paused) {
		handle.session.state = MRMacroExecutionState::Yielded;
		handle.lastResult = workerResult.debugResult;
		notifyMacroExecutionSessionChanged();
		return workerResult;
	}
	if (handle.route == MRMacroExecutionRoute::StagedBackground && handle.stagedSession != nullptr && !workerResult.debugResult.cancelled) {
		workerResult.stagedResult = mrvmBuildStagedJobResult(*handle.vm, *handle.stagedSession);
		workerResult.stagedResult.conflictSnapshot = handle.conflictSnapshot;
		workerResult.hasStagedResult = true;
		handle.lastResult = workerResult.debugResult;
		notifyMacroExecutionSessionChanged();
		return workerResult;
	}
	completeDebugSession(sessionHandle, workerResult.debugResult);
	return workerResult;
}

bool mrvmFinalizeStagedDebugSession(MRMacroExecutionSessionId sessionId, const MRMacroDebugRunResult &debugResult, bool accepted, const std::string &message) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	const std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);

	if (sessionHandle == g_debugSessionVmHandles.end() || sessionHandle->second.route != MRMacroExecutionRoute::StagedBackground) return false;
	MRMacroExecutionSession session = sessionHandle->second.session;
	const MRVMDebugSessionCleanup cleanup = sessionHandle->second.cleanup;

	eraseDebugSessionControls(sessionId);
	g_debugSessionVmHandles.erase(sessionHandle);
	if (accepted)
		mrvmFinalizeDebugSession(session, debugResult, cleanup);
	else
		mrvmRejectDebugSession(session, cleanup, message);
	return true;
}

static MRMacroDebugRunResult stepDebugMacroByName(MRMacroExecutionSessionId sessionId, const std::string &macroKey, MRMacroDebugStepMode mode, bool statementStep, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MRMacroDebugRunResult result;
	MRMacroSourceMapEntry startingSpan;
	std::vector<std::size_t> breakpointOffsets;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);
	static constexpr int kMaxStatementStepOpcodes = 4096;

	if (errorMessage != nullptr) errorMessage->clear();
	if (sessionId == 0) {
		result.stopReason = mrdStopError;
		result.hadError = true;
		result.logLines.push_back("VM Error: debug session is not active.");
		if (errorMessage != nullptr) *errorMessage = "Debug session is not active.";
		return result;
	}
	if (normalizedMacroKey.empty()) {
		result.stopReason = mrdStopError;
		result.hadError = true;
		result.logLines.push_back("VM Error: debug macro name is empty.");
		if (errorMessage != nullptr) *errorMessage = "Debug macro name is empty.";
		return result;
	}
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);
	if (sessionHandle == g_debugSessionVmHandles.end()) {
		result.stopReason = mrdStopError;
		result.hadError = true;
		result.logLines.push_back("VM Error: debug session is not paused.");
		if (errorMessage != nullptr) *errorMessage = "Debug session is not paused.";
		return result;
	}
	const std::string currentMacroKey = !sessionHandle->second.lastResult.macroKey.empty() ? sessionHandle->second.lastResult.macroKey : normalizedMacroKey;

	if (!mrvmCollectDebugBreakpointOffsetsForLoadedFile(currentMacroKey, breakpointOffsets)) breakpointOffsets.clear();
	if (!statementStep) return mrvmStepDebugSession(sessionId, breakpointOffsets, mode);
	if (sessionHandle->second.lastResult.paused) {
		bool haveStartingSpan = mrvmRuntimeCatalogSourceMapSpanForBytecodeOffset(g_runtimeEnv.runtimeKv, currentMacroKey, sessionHandle->second.lastResult.instructionOffset, startingSpan);

		if (!haveStartingSpan) return mrvmStepDebugSession(sessionId, breakpointOffsets, mode);
	} else
		return mrvmStepDebugSession(sessionId, breakpointOffsets, mode);
	for (int stepCount = 0; stepCount < kMaxStatementStepOpcodes; ++stepCount) {
		MRMacroSourceMapEntry currentSpan;
		bool haveCurrentSpan;

		result = mrvmStepDebugSession(sessionId, breakpointOffsets, mode);
		if (!result.paused || result.hadError || result.cancelled || result.stopReason == mrdStopBreakpoint) return result;
		haveCurrentSpan = mrvmRuntimeCatalogSourceMapSpanForBytecodeOffset(g_runtimeEnv.runtimeKv, currentMacroKey, result.instructionOffset, currentSpan);
		if (!haveCurrentSpan) continue;
		if (!sameSourceMapSpan(startingSpan, currentSpan)) return result;
	}
	result.stopReason = mrdStopError;
	result.hadError = true;
	result.paused = true;
	result.logLines.push_back("VM Error: debug statement step did not leave the current source span.");
	if (errorMessage != nullptr) *errorMessage = "Debug statement step did not leave the current source span.";
	return result;
}

MRMacroDebugRunResult mrvmStepDebugMacroByName(MRMacroExecutionSessionId sessionId, const std::string &macroKey, std::string *errorMessage) {
	return stepDebugMacroByName(sessionId, macroKey, mrdStepInto, true, errorMessage);
}

MRMacroDebugRunResult mrvmStepOverDebugMacroByName(MRMacroExecutionSessionId sessionId, const std::string &macroKey, std::string *errorMessage) {
	return stepDebugMacroByName(sessionId, macroKey, mrdStepOver, true, errorMessage);
}

MRMacroDebugRunResult mrvmStepOutDebugMacroByName(MRMacroExecutionSessionId sessionId, const std::string &macroKey, std::string *errorMessage) {
	return stepDebugMacroByName(sessionId, macroKey, mrdStepOut, false, errorMessage);
}

bool mrvmCloseDebugSession(MRMacroExecutionSessionId sessionId, bool eraseDebuggerRuntimeOnDeferredClose) {
	if (!g_vmExecutionMutex.try_lock()) {
		std::lock_guard<std::mutex> controlsLock(g_debugSessionControlMutex);
		const std::map<MRMacroExecutionSessionId, DebugSessionControls>::iterator controls = g_debugSessionControls.find(sessionId);

		if (controls == g_debugSessionControls.end()) return false;
		if (eraseDebuggerRuntimeOnDeferredClose && controls->second.closeCleanupFlag != nullptr) controls->second.closeCleanupFlag->store(true, std::memory_order_release);
		if (controls->second.cancelFlag != nullptr) controls->second.cancelFlag->store(true, std::memory_order_release);
		return false;
	}
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex, std::adopt_lock);
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);

	if (sessionHandle == g_debugSessionVmHandles.end()) return false;
	{
		MRMacroExecutionSession session = sessionHandle->second.session;

		session.state = MRMacroExecutionState::Cancelled;
		eraseDebugSessionControls(session.sessionId);
		g_debugSessionVmHandles.erase(sessionHandle);
		publishMacroExecutionResult(session, session.state, "Debug macro closed.");
	}
	return true;
}
