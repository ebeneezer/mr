#include <unordered_map>
#include "../app/MRVersion.hpp"
#include "../app/utils/MRConstants.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#include "../app/utils/MRStringUtils.hpp"
#define Uses_MsgBox
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TButton
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TStaticText
#define Uses_TScrollBar
#define Uses_TListViewer
#define Uses_TStatusLine
#define Uses_TObject
#define Uses_TScreen
#define Uses_TDrawBuffer
#define Uses_TView
#define Uses_TClipboard
#include <tvision/tv.h>

#include "mrmac.h"
#include "ui/modeless/MRMacroModelessUi.hpp"
#include "MRMacroRunner.hpp"
#include "MRVM.hpp"
#include "MRVMDebugSession.hpp"
#include "vm/MRVMExecSessions.hpp"
#include "vm/MRVMExecutionInternal.hpp"
#include "ui/conventional/MRVMDeferredUi.hpp"
#include "vm/MRVMHash.hpp"
#include "vm/MRVMIntrinsics.hpp"
#include "ui/conventional/MRVMMacroDialogRuntime.hpp"
#include "ui/modeless/MRVMMacroModelessProcedures.hpp"
#include "vm/MRVMKeymapRuntime.hpp"
#include "vm/MRVMMacroSpecRuntime.hpp"
#include "ui/modeless/MRVMModelessUiRuntime.hpp"
#include "vm/MRVMProcessRuntime.hpp"
#include "vm/MRVMProcedureCatalog.hpp"
#include "vm/MRVMRuntimeCatalog.hpp"
#include "vm/MRVMRuntimeDebugger.hpp"
#include "vm/MRVMRuntimeGlobals.hpp"
#include "vm/MRVMRuntimeInternal.hpp"
#include "vm/MRVMRuntimeKv.hpp"
#include "vm/MRVMRuntimeState.hpp"
#include "vm/MRVMValue.hpp"
#include "ui/conventional/MRVMEditor.hpp"
#include "ui/conventional/MRVMScreen.hpp"
#include "vm/MRVMSettings.hpp"
#include "vm/MRVMSystemVariables.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <glob.h>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "../ui/MREditWindow.hpp"
#include "../app/MRCommandRouter.hpp"
#include "../app/MRRuntimeScheduler.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../ui/MRMenuBar.hpp"
#include "../ui/MRStatusLine.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../dialogs/setup/MRSetupCommon.hpp"
#include "../dialogs/MRWindowList.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "../keymap/MRKeymapProfile.hpp"
#include "../ui/MRWindowSupport.hpp"

using namespace mrvm_runtime;

MRMacroDebugWatchSnapshot VirtualMachine::evaluateDebugWatchExpression(const std::string &expression) {
	MRMacroDebugWatchSnapshot snapshot;
	std::vector<MRMacWatchSymbol> symbols;
	std::vector<unsigned char> bytecode;
	std::size_t bytecodeSize = 0;
	int resultType = 0;
	unsigned char *compiled = nullptr;
	const std::vector<VirtualMachine::Value> savedStack = stack;
	const std::vector<std::string> savedLog = log;
	const bool savedLogTruncated = logTruncated;
	const bool savedCancelledExecution = cancelledExecution;
	const int savedReturnInt = g_runtimeEnv.returnInt;
	const std::string savedReturnStr = g_runtimeEnv.returnStr;
	const int savedErrorLevel = g_runtimeEnv.errorLevel;
	const std::string savedParameterString = g_runtimeEnv.parameterString;

	snapshot.expression = expression;
	for (const std::pair<const std::string, VirtualMachine::Value> &entry : variables)
		symbols.push_back(MRMacWatchSymbol{entry.first.c_str(), entry.second.type});
	compiled = compile_macro_watch_expression(expression.c_str(), symbols.empty() ? nullptr : symbols.data(), symbols.size(), &bytecodeSize, &resultType);
	if (compiled == nullptr) {
		const char *error = get_last_compile_error();

		snapshot.errorText = error != nullptr && *error != '\0' ? error : "Watch expression could not be compiled.";
		return snapshot;
	}
	bytecode.assign(compiled, compiled + bytecodeSize);
	std::free(compiled);
	executeAt(bytecode.data(), bytecode.size(), 0, std::string(), std::string(), false, false, true);
	for (std::size_t index = savedLog.size(); index < log.size(); ++index)
		if (log[index].rfind("VM Error: ", 0) == 0) {
			snapshot.errorText = log[index].substr(std::strlen("VM Error: "));
			break;
		}
	if (snapshot.errorText.empty()) {
		if (stack.size() != 1) snapshot.errorText = "Watch expression did not produce one value.";
		else {
			snapshot.type = resultType;
			snapshot.valueText = macroDebugValueText(stack.back(), *mHashStore, g_runtimeEnv.runtimeKv.globalStore());
		}
	}
	stack = savedStack;
	log = savedLog;
	logTruncated = savedLogTruncated;
	cancelledExecution = savedCancelledExecution;
	g_runtimeEnv.returnInt = savedReturnInt;
	g_runtimeEnv.returnStr = savedReturnStr;
	g_runtimeEnv.errorLevel = savedErrorLevel;
	g_runtimeEnv.parameterString = savedParameterString;
	return snapshot;
}

bool VirtualMachine::writeDebugScalarVariable(const MRMacroDebugVariableSnapshot &variable, const std::string &valueText, std::vector<MRMacroDebugVariableSnapshot> &updatedVariables, std::string &errorMessage) {
	Value replacement;
	Value previous;
	std::map<std::string, Value>::iterator local;
	MRMacroDebugVariableScope actualScope;
	char *end = nullptr;

	updatedVariables.clear();
	errorMessage.clear();
	if (!mDebugPaused) {
		errorMessage = "Debug session is not paused.";
		return false;
	}
	switch (variable.type) {
		case TYPE_INT: {
			errno = 0;
			const long parsed = std::strtol(valueText.c_str(), &end, 10);
			if (end == valueText.c_str() || *end != '\0' || errno == ERANGE || parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
				errorMessage = "Expected an integer value.";
				return false;
			}
			replacement = mrvmMakeInt(static_cast<int>(parsed));
			break;
		}
		case TYPE_REAL: {
			errno = 0;
			const double parsed = std::strtod(valueText.c_str(), &end);
			if (end == valueText.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(parsed)) {
				errorMessage = "Expected a finite real value.";
				return false;
			}
			replacement = mrvmMakeReal(parsed);
			break;
		}
		case TYPE_STR:
			try {
				mrvmEnforceStringLength(valueText);
			} catch (const std::exception &error) {
				errorMessage = error.what();
				return false;
			}
			replacement = mrvmMakeString(valueText);
			break;
		case TYPE_CHAR:
			if (valueText.size() > 1) {
				errorMessage = "Expected one character.";
				return false;
			}
			replacement = mrvmMakeChar(valueText.empty() ? 0 : static_cast<unsigned char>(valueText[0]));
			break;
		default:
			errorMessage = "Only scalar variables can be changed.";
			return false;
	}

	if (variable.scope == mrdVariableAppGlobal) {
		GlobalEntry global;

		if (!readRuntimeGlobalValueDirect(variable.name, global) || global.type != variable.type) {
			errorMessage = "App global no longer matches the debugger projection.";
			return false;
		}
		mrvmRuntimeGlobalWrite(g_runtimeEnv.runtimeKv, variable.name, variable.type, replacement);
	} else {
		local = variables.find(variable.name);
		if (local == variables.end()) {
			errorMessage = "Variable no longer exists in the paused debug session.";
			return false;
		}
		actualScope = macroDebugVariableScope(local->first, local->second, mClosureVariableNames, mSessionVariableNames);
		if (actualScope != variable.scope || local->second.type != variable.type) {
			errorMessage = "Variable no longer matches the debugger projection.";
			return false;
		}
		previous = local->second;
		replacement.globalStorage = previous.globalStorage;
		local->second = replacement;
		if (actualScope == mrdVariableClosure && !mClosureId.empty()) {
			if (!mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, mClosureId, variable.name, replacement, *mHashStore)) {
				local->second = previous;
				errorMessage = "Closure variable could not be stored.";
				return false;
			}
		} else if (actualScope == mrdVariableSession && mExecutionSessionId != 0) {
			if (!mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, mExecutionSessionId, variable.name, replacement, *mHashStore)) {
				local->second = previous;
				errorMessage = "Session variable could not be stored.";
				return false;
			}
		}
	}
	{
		MRMacroDebugRunResult snapshot;

		appendMacroDebugVariableSnapshots(snapshot, variables, mClosureVariableNames, mSessionVariableNames, *mHashStore, g_runtimeEnv.runtimeKv.globalStore());
		appendMacroDebugAppGlobalSnapshots(snapshot, g_runtimeEnv.runtimeKv.globalStore());
		updatedVariables = std::move(snapshot.variables);
	}
	return true;
}

MRMacroDebugRunResult VirtualMachine::executeDebugAt(const unsigned char *bytecode, size_t length, size_t entryOffset, const std::string &parameterString, const std::string &macroName, const std::vector<std::size_t> &breakpointOffsets, bool firstRun, const std::string &macroKey, const std::string &sourcePath) {
	MRMacroDebugRunResult result;
	const bool savedAsyncDelayEnabled = mAsyncDelayEnabled;
	bool hadError = false;

	mDebugRunActive = true;
	mDebugStopped = false;
	mDebugStopReason = mrdStopNone;
	mDebugStopOffset = 0;
	mDebugStackDepth = 0;
	mDebugPaused = false;
	mDebugBytecode.clear();
	mDebugLength = 0;
	mDebugIp = 0;
	mDebugCallStack.clear();
	mDebugSkipCurrentOffset = false;
	mDebugPauseRequested = false;
	mDebugInstructionBudget = 0;
	mDebugStepMode = mrdStepNone;
	mDebugStepOutDepth = 0;
	mDebugMacroKey = macroKey.empty() ? mrvmUpperKey(macroName) : macroKey;
	mDebugSourcePath = sourcePath;
	mDebugChildFrame.reset();
	mDebugBreakpointOffsets = breakpointOffsets;
	std::sort(mDebugBreakpointOffsets.begin(), mDebugBreakpointOffsets.end());
	mDebugBreakpointOffsets.erase(std::unique(mDebugBreakpointOffsets.begin(), mDebugBreakpointOffsets.end()), mDebugBreakpointOffsets.end());
	mAsyncDelayEnabled = false;

	executeAt(bytecode, length, entryOffset, parameterString, macroName, true, firstRun);
	if (mDebugChildFrame != nullptr) {
		result = mDebugChildFrame->result;
		appendDebugParentCallStack(result, mDebugChildFrame->parentInstructionOffset);
		mDebugChildFrame->result = result;
		mDebugRunActive = false;
		mAsyncDelayEnabled = savedAsyncDelayEnabled;
		return result;
	}

	for (const std::string &line : log)
		if (line.rfind("VM Error:", 0) == 0) {
			hadError = true;
			break;
		}

	if (mDebugStopped) result.stopReason = mDebugStopReason;
	else if (cancelledExecution)
		result.stopReason = mrdStopCancelled;
	else if (hadError)
		result.stopReason = mrdStopError;
	else
		result.stopReason = mrdStopCompleted;
	result.instructionOffset = mDebugStopOffset;
	result.stackDepth = mDebugStackDepth;
	result.logLines = log;
	result.macroKey = mDebugMacroKey;
	result.sourcePath = mDebugSourcePath;
	result.cancelled = cancelledExecution;
	result.hadError = hadError;
	result.paused = mDebugPaused;
	appendMacroDebugVariableSnapshots(result, variables, mClosureVariableNames, mSessionVariableNames, *mHashStore, g_runtimeEnv.runtimeKv.globalStore());
	appendMacroDebugAppGlobalSnapshots(result, g_runtimeEnv.runtimeKv.globalStore());
	appendDebugCallStack(result);

	mDebugRunActive = false;
	mAsyncDelayEnabled = savedAsyncDelayEnabled;
	return result;
}

MRMacroDebugRunResult VirtualMachine::continueDebug(const std::vector<std::size_t> &breakpointOffsets, std::size_t instructionBudget) {
	MRMacroDebugRunResult result;
	const bool savedAsyncDelayEnabled = mAsyncDelayEnabled;
	const MRMacroDebugStopReason previousStopReason = mDebugStopReason;
	bool hadError = false;

	if (!mDebugPaused) return result;
	if (mDebugPauseRequested) {
		mDebugStopped = true;
		mDebugStopReason = mrdStopPaused;
		mDebugStopOffset = mDebugIp;
		mDebugStackDepth = mDebugCallStack.size();
		mDebugPauseRequested = false;
		result.stopReason = mrdStopPaused;
		result.instructionOffset = mDebugStopOffset;
		result.stackDepth = mDebugStackDepth;
		result.logLines = log;
		result.macroKey = mDebugMacroKey;
		result.sourcePath = mDebugSourcePath;
		result.paused = true;
		appendMacroDebugVariableSnapshots(result, variables, mClosureVariableNames, mSessionVariableNames, *mHashStore, g_runtimeEnv.runtimeKv.globalStore());
		appendMacroDebugAppGlobalSnapshots(result, g_runtimeEnv.runtimeKv.globalStore());
		appendDebugCallStack(result);
		return result;
	}
	mDebugRunActive = true;
	mDebugStopped = false;
	mDebugStopReason = mrdStopNone;
	mDebugStackDepth = 0;
	mDebugBreakpointOffsets = breakpointOffsets;
	std::sort(mDebugBreakpointOffsets.begin(), mDebugBreakpointOffsets.end());
	mDebugBreakpointOffsets.erase(std::unique(mDebugBreakpointOffsets.begin(), mDebugBreakpointOffsets.end()), mDebugBreakpointOffsets.end());
	mDebugSkipCurrentOffset = previousStopReason == mrdStopBreakpoint || previousStopReason == mrdStopPaused;
	mDebugStepMode = mrdStepNone;
	mDebugInstructionBudget = instructionBudget;
	mAsyncDelayEnabled = false;
	if (mDebugChildFrame != nullptr) {
		std::vector<std::size_t> childBreakpointOffsets;

		static_cast<void>(mrvmCollectDebugBreakpointOffsetsForLoadedFile(mDebugChildFrame->macroKey, childBreakpointOffsets));
		result = mDebugChildFrame->vm->continueDebug(childBreakpointOffsets);
		mDebugChildFrame->result = result;
		if (result.paused) {
			appendDebugParentCallStack(result, mDebugChildFrame->parentInstructionOffset);
			mDebugChildFrame->result = result;
			mDebugRunActive = false;
			mAsyncDelayEnabled = savedAsyncDelayEnabled;
			return result;
		}
		log.insert(log.end(), mDebugChildFrame->vm->log.begin(), mDebugChildFrame->vm->log.end());
		if (mDebugChildFrame->unloadAfterCompletion) unloadMacroFromRegistry(mDebugChildFrame->macroKey);
		else if (mDebugChildFrame->evictTransientAfterCompletion)
			evictTransientFileImage(mDebugChildFrame->fileKey);
		mDebugChildFrame.reset();
		mDebugSkipCurrentOffset = false;
	}

	executeAt(nullptr, 0, 0, std::string(), std::string(), false, false);
	if (mDebugChildFrame != nullptr) {
		result = mDebugChildFrame->result;
		appendDebugParentCallStack(result, mDebugChildFrame->parentInstructionOffset);
		mDebugChildFrame->result = result;
		mDebugRunActive = false;
		mDebugStepMode = mrdStepNone;
		mAsyncDelayEnabled = savedAsyncDelayEnabled;
		return result;
	}

	for (const std::string &line : log)
		if (line.rfind("VM Error:", 0) == 0) {
			hadError = true;
			break;
		}

	if (mDebugStopped) result.stopReason = mDebugStopReason;
	else if (cancelledExecution)
		result.stopReason = mrdStopCancelled;
	else if (hadError)
		result.stopReason = mrdStopError;
	else
		result.stopReason = mrdStopCompleted;
	result.instructionOffset = mDebugStopOffset;
	result.stackDepth = mDebugStackDepth;
	result.logLines = log;
	result.macroKey = mDebugMacroKey;
	result.sourcePath = mDebugSourcePath;
	result.cancelled = cancelledExecution;
	result.hadError = hadError;
	result.paused = mDebugPaused;
	appendMacroDebugVariableSnapshots(result, variables, mClosureVariableNames, mSessionVariableNames, *mHashStore, g_runtimeEnv.runtimeKv.globalStore());
	appendMacroDebugAppGlobalSnapshots(result, g_runtimeEnv.runtimeKv.globalStore());
	appendDebugCallStack(result);

	mDebugRunActive = false;
	mAsyncDelayEnabled = savedAsyncDelayEnabled;
	return result;
}

MRMacroDebugRunResult VirtualMachine::stepDebug(const std::vector<std::size_t> &breakpointOffsets, MRMacroDebugStepMode mode) {
	MRMacroDebugRunResult result;
	const bool savedAsyncDelayEnabled = mAsyncDelayEnabled;
	const MRMacroDebugStopReason previousStopReason = mDebugStopReason;
	bool hadError = false;

	if (!mDebugPaused) return result;
	mDebugRunActive = true;
	mDebugStopped = false;
	mDebugStopReason = mrdStopNone;
	mDebugStackDepth = 0;
	mDebugBreakpointOffsets = breakpointOffsets;
	std::sort(mDebugBreakpointOffsets.begin(), mDebugBreakpointOffsets.end());
	mDebugBreakpointOffsets.erase(std::unique(mDebugBreakpointOffsets.begin(), mDebugBreakpointOffsets.end()), mDebugBreakpointOffsets.end());
	mDebugSkipCurrentOffset = previousStopReason == mrdStopBreakpoint;
	mDebugStepMode = mode;
	mDebugInstructionBudget = 0;
	mDebugStepOutDepth = mDebugCallStack.size();
	mAsyncDelayEnabled = false;
	if (mDebugChildFrame != nullptr) {
		std::vector<std::size_t> childBreakpointOffsets;

		static_cast<void>(mrvmCollectDebugBreakpointOffsetsForLoadedFile(mDebugChildFrame->macroKey, childBreakpointOffsets));
		result = mDebugChildFrame->vm->stepDebug(childBreakpointOffsets, mode);
		mDebugChildFrame->result = result;
		if (result.paused) {
			appendDebugParentCallStack(result, mDebugChildFrame->parentInstructionOffset);
			mDebugChildFrame->result = result;
			mDebugRunActive = false;
			mDebugStepMode = mrdStepNone;
			mAsyncDelayEnabled = savedAsyncDelayEnabled;
			return result;
		}
		log.insert(log.end(), mDebugChildFrame->vm->log.begin(), mDebugChildFrame->vm->log.end());
		if (mDebugChildFrame->unloadAfterCompletion) unloadMacroFromRegistry(mDebugChildFrame->macroKey);
		else if (mDebugChildFrame->evictTransientAfterCompletion)
			evictTransientFileImage(mDebugChildFrame->fileKey);
		mDebugChildFrame.reset();
		mDebugStopped = true;
		mDebugStopReason = mrdStopStep;
		mDebugStopOffset = mDebugIp;
		mDebugStackDepth = mDebugCallStack.size();
		mDebugPaused = true;
		result.stopReason = mrdStopStep;
		result.instructionOffset = mDebugStopOffset;
		result.stackDepth = mDebugStackDepth;
		result.logLines = log;
		result.macroKey = mDebugMacroKey;
		result.sourcePath = mDebugSourcePath;
		result.cancelled = false;
		result.hadError = false;
		result.paused = true;
		appendMacroDebugVariableSnapshots(result, variables, mClosureVariableNames, mSessionVariableNames, *mHashStore, g_runtimeEnv.runtimeKv.globalStore());
		appendMacroDebugAppGlobalSnapshots(result, g_runtimeEnv.runtimeKv.globalStore());
		appendDebugCallStack(result);
		mDebugRunActive = false;
		mDebugStepMode = mrdStepNone;
		mAsyncDelayEnabled = savedAsyncDelayEnabled;
		return result;
	}

	executeAt(nullptr, 0, 0, std::string(), std::string(), false, false);
	if (mDebugChildFrame != nullptr) {
		result = mDebugChildFrame->result;
		appendDebugParentCallStack(result, mDebugChildFrame->parentInstructionOffset);
		mDebugChildFrame->result = result;
		mDebugRunActive = false;
		mDebugStepMode = mrdStepNone;
		mAsyncDelayEnabled = savedAsyncDelayEnabled;
		return result;
	}

	for (const std::string &line : log)
		if (line.rfind("VM Error:", 0) == 0) {
			hadError = true;
			break;
		}

	if (mDebugStopped) result.stopReason = mDebugStopReason;
	else if (cancelledExecution)
		result.stopReason = mrdStopCancelled;
	else if (hadError)
		result.stopReason = mrdStopError;
	else
		result.stopReason = mrdStopCompleted;
	result.instructionOffset = mDebugStopOffset;
	result.stackDepth = mDebugStackDepth;
	result.logLines = log;
	result.macroKey = mDebugMacroKey;
	result.sourcePath = mDebugSourcePath;
	result.cancelled = cancelledExecution;
	result.hadError = hadError;
	result.paused = mDebugPaused;
	appendMacroDebugVariableSnapshots(result, variables, mClosureVariableNames, mSessionVariableNames, *mHashStore, g_runtimeEnv.runtimeKv.globalStore());
	appendMacroDebugAppGlobalSnapshots(result, g_runtimeEnv.runtimeKv.globalStore());
	appendDebugCallStack(result);

	mDebugRunActive = false;
	mDebugStepMode = mrdStepNone;
	mAsyncDelayEnabled = savedAsyncDelayEnabled;
	return result;
}

void mrvmFinalizeDebugSession(MRMacroExecutionSession &session, const MRMacroDebugRunResult &result, const MRVMDebugSessionCleanup &cleanup) {
	session.state = result.cancelled ? MRMacroExecutionState::Cancelled : (result.hadError ? MRMacroExecutionState::Failed : MRMacroExecutionState::Completed);
	if (cleanup.unloadAfterCompletion && !cleanup.macroKey.empty()) unloadMacroFromRegistry(cleanup.macroKey);
	else if (cleanup.evictTransientAfterCompletion && !cleanup.fileKey.empty())
		evictTransientFileImage(cleanup.fileKey);
	publishMacroExecutionResult(session, session.state, result.hadError ? "Debug macro failed." : "Debug macro completed.");
}

static MRMacroDebugRunResult startDebugMacroByKey(const std::string &macroKey, const std::string &parameterString, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut, std::string *errorMessage, bool stopAtEntry, int temporaryStopLine) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MRMacroDebugRunResult result;
	MacroRef macroRef;
	LoadedMacroFile file;
	MRMacroExecutionSession debugSession;
	MRMacroSourceMapEntry temporaryStopSpan;
	std::vector<std::size_t> breakpointOffsets;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);
	bool firstRun = false;
	std::string preparationError;

	if (sessionOut != nullptr) *sessionOut = MRMacroExecutionSession();
	if (errorMessage != nullptr) errorMessage->clear();
	if (!prepareDebugMacroByKey(normalizedMacroKey, stopAtEntry, macroRef, file, breakpointOffsets, firstRun, preparationError)) {
		result.stopReason = mrdStopError;
		result.hadError = true;
		result.logLines.push_back("VM Error: " + preparationError);
		if (errorMessage != nullptr) *errorMessage = preparationError;
		return result;
	}
	if (temporaryStopLine > 0) {
		if (!mrvmRuntimeCatalogFirstSourceMapSpanForLine(g_runtimeEnv.runtimeKv, normalizedMacroKey, temporaryStopLine, temporaryStopSpan)) {
			result.stopReason = mrdStopError;
			result.hadError = true;
			result.logLines.push_back("VM Error: debug source line is not debuggable: " + std::to_string(temporaryStopLine));
			if (errorMessage != nullptr) *errorMessage = "Cursor line is not debuggable.";
			return result;
		}
		breakpointOffsets.push_back(temporaryStopSpan.bytecodeOffset);
		std::sort(breakpointOffsets.begin(), breakpointOffsets.end());
		breakpointOffsets.erase(std::unique(breakpointOffsets.begin(), breakpointOffsets.end()), breakpointOffsets.end());
	}
	result = mrvmStartDebugSessionAt(file.bytecode.data(), file.bytecode.size(), macroRef.entryOffset, macroRef.displayName, owner, breakpointOffsets, &debugSession, firstRun, normalizedMacroKey, file.resolvedPath, parameterString);
	if (sessionOut != nullptr) *sessionOut = debugSession;
	if (result.paused) {
		MRVMDebugSessionCleanup cleanup;

		cleanup.macroKey = normalizedMacroKey;
		cleanup.fileKey = macroRef.fileKey;
		cleanup.unloadAfterCompletion = macroRef.dumpAttr;
		cleanup.evictTransientAfterCompletion = macroRef.transientAttr;
		static_cast<void>(mrvmConfigureDebugSessionCleanup(debugSession.sessionId, cleanup));
	} else if (macroRef.dumpAttr)
		unloadMacroFromRegistry(normalizedMacroKey);
	else if (macroRef.transientAttr)
		evictTransientFileImage(macroRef.fileKey);
	return result;
}

MRMacroDebugRunResult mrvmStartDebugMacroByName(const std::string &macroKey, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut, std::string *errorMessage, bool stopAtEntry, int temporaryStopLine) {
	return startDebugMacroByKey(macroKey, std::string(), owner, sessionOut, errorMessage, stopAtEntry, temporaryStopLine);
}

MRMacroDebugRunResult mrvmStartDebugMacroBySpec(const std::string &spec, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MRMacroDebugRunResult result;
	LoadedMacroFile file;
	std::string macroKey;
	std::string parameterString;
	std::string resolutionError;

	if (sessionOut != nullptr) *sessionOut = MRMacroExecutionSession();
	if (errorMessage != nullptr) errorMessage->clear();
	if (!resolveDebugMacroSpec(spec, macroKey, parameterString, file, resolutionError)) {
		result.stopReason = mrdStopError;
		result.hadError = true;
		result.logLines.push_back("VM Error: " + resolutionError);
		if (errorMessage != nullptr) *errorMessage = resolutionError;
		return result;
	}
	return startDebugMacroByKey(macroKey, parameterString, owner, sessionOut, errorMessage, false, 0);
}

bool mrvmMacroSpecHasEnabledDebugBreakpoint(const std::string &spec, std::string *sourcePath) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	LoadedMacroFile file;
	std::vector<MRMacroDebuggerBreakpoint> breakpoints;
	std::string macroKey;
	std::string parameterString;
	std::string resolutionError;

	if (sourcePath != nullptr) sourcePath->clear();
	if (!resolveDebugMacroSpec(spec, macroKey, parameterString, file, resolutionError)) return false;
	if (sourcePath != nullptr) *sourcePath = file.resolvedPath;
	if (!mrvmRuntimeDebuggerLineBreakpointsForMacro(g_runtimeEnv.runtimeKv, macroKey, breakpoints)) return false;
	for (const MRMacroDebuggerBreakpoint &breakpoint : breakpoints)
		if (breakpoint.enabled) return true;
	return false;
}

bool mrvmToggleDebugLineBreakpoint(const std::string &macroKey, int line, bool *enabledOut, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);
	MRMacroDebuggerBreakpoint breakpoint;

	if (enabledOut != nullptr) *enabledOut = false;
	if (errorMessage != nullptr) errorMessage->clear();
	if (normalizedMacroKey.empty()) {
		if (errorMessage != nullptr) *errorMessage = "Debug macro name is empty.";
		return false;
	}
	if (line <= 0) {
		if (errorMessage != nullptr) *errorMessage = "Debug breakpoint line is invalid.";
		return false;
	}
	if (mrvmRuntimeDebuggerReadLineBreakpoint(g_runtimeEnv.runtimeKv, normalizedMacroKey, line, breakpoint)) {
		if (!mrvmRuntimeDebuggerEraseLineBreakpoint(g_runtimeEnv.runtimeKv, normalizedMacroKey, line)) {
			if (errorMessage != nullptr) *errorMessage = "Debug breakpoint could not be cleared.";
			return false;
		}
		return true;
	}
	if (!mrvmRuntimeDebuggerWriteLineBreakpoint(g_runtimeEnv.runtimeKv, normalizedMacroKey, line, true, std::string())) {
		if (errorMessage != nullptr) *errorMessage = "No debuggable source span for breakpoint line.";
		return false;
	}
	if (enabledOut != nullptr) *enabledOut = true;
	return true;
}

bool mrvmToggleDebugLineBreakpointEnabled(const std::string &macroKey, int line, bool *enabledOut, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);
	MRMacroDebuggerBreakpoint breakpoint;

	if (enabledOut != nullptr) *enabledOut = false;
	if (errorMessage != nullptr) errorMessage->clear();
	if (normalizedMacroKey.empty() || line <= 0) {
		if (errorMessage != nullptr) *errorMessage = "Debug breakpoint is invalid.";
		return false;
	}
	if (!mrvmRuntimeDebuggerReadLineBreakpoint(g_runtimeEnv.runtimeKv, normalizedMacroKey, line, breakpoint)) {
		if (!mrvmRuntimeDebuggerWriteLineBreakpoint(g_runtimeEnv.runtimeKv, normalizedMacroKey, line, true, std::string())) {
			if (errorMessage != nullptr) *errorMessage = "No debuggable source span for breakpoint line.";
			return false;
		}
		if (enabledOut != nullptr) *enabledOut = true;
		return true;
	}
	if (!mrvmRuntimeDebuggerWriteLineBreakpoint(g_runtimeEnv.runtimeKv, normalizedMacroKey, line, !breakpoint.enabled, breakpoint.conditionText)) {
		if (errorMessage != nullptr) *errorMessage = "Debug breakpoint could not be updated.";
		return false;
	}
	if (enabledOut != nullptr) *enabledOut = !breakpoint.enabled;
	return true;
}

bool mrvmWriteDebugLineBreakpoint(const std::string &macroKey, int line, bool enabled, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	if (errorMessage != nullptr) errorMessage->clear();
	if (normalizedMacroKey.empty() || line <= 0) {
		if (errorMessage != nullptr) *errorMessage = "Debug breakpoint is invalid.";
		return false;
	}
	if (!mrvmRuntimeDebuggerWriteLineBreakpoint(g_runtimeEnv.runtimeKv, normalizedMacroKey, line, enabled, std::string())) {
		if (errorMessage != nullptr) *errorMessage = "No debuggable source span for breakpoint line.";
		return false;
	}
	return true;
}

bool mrvmDebugLineBreakpointsForMacro(const std::string &macroKey, std::vector<MRMacroDebuggerBreakpoint> &breakpoints) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MacroRef macroRef;
	LoadedMacroFile file;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	breakpoints.clear();
	if (normalizedMacroKey.empty()) return false;
	if (!readLoadedMacroByKey(normalizedMacroKey, macroRef)) return false;
	if (!readLoadedMacroFileByKey(macroRef.fileKey, file)) return false;
	for (const std::string &fileMacroKey : file.macroNames) {
		std::vector<MRMacroDebuggerBreakpoint> macroBreakpoints;

		if (!mrvmRuntimeDebuggerLineBreakpointsForMacro(g_runtimeEnv.runtimeKv, fileMacroKey, macroBreakpoints)) continue;
		breakpoints.insert(breakpoints.end(), macroBreakpoints.begin(), macroBreakpoints.end());
	}
	std::sort(breakpoints.begin(), breakpoints.end(), [](const MRMacroDebuggerBreakpoint &left, const MRMacroDebuggerBreakpoint &right) {
		if (left.line != right.line) return left.line < right.line;
		if (left.macroKey != right.macroKey) return left.macroKey < right.macroKey;
		return left.bytecodeOffset < right.bytecodeOffset;
	});
	return !breakpoints.empty();
}

bool mrvmToggleDebugLineBreakpointsEnabledForMacroFile(const std::string &macroKey, bool *enabledOut, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MacroRef macroRef;
	LoadedMacroFile file;
	std::vector<MRMacroDebuggerBreakpoint> breakpoints;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);
	bool enable = true;

	if (enabledOut != nullptr) *enabledOut = false;
	if (errorMessage != nullptr) errorMessage->clear();
	if (normalizedMacroKey.empty() || !readLoadedMacroByKey(normalizedMacroKey, macroRef) || !readLoadedMacroFileByKey(macroRef.fileKey, file)) {
		if (errorMessage != nullptr) *errorMessage = "Debug macro is not loaded.";
		return false;
	}
	for (const std::string &fileMacroKey : file.macroNames) {
		std::vector<MRMacroDebuggerBreakpoint> macroBreakpoints;

		if (!mrvmRuntimeDebuggerLineBreakpointsForMacro(g_runtimeEnv.runtimeKv, fileMacroKey, macroBreakpoints)) continue;
		breakpoints.insert(breakpoints.end(), macroBreakpoints.begin(), macroBreakpoints.end());
	}
	for (const MRMacroDebuggerBreakpoint &breakpoint : breakpoints)
		if (breakpoint.enabled) {
			enable = false;
			break;
		}
	for (const std::string &fileMacroKey : file.macroNames)
		if (!mrvmRuntimeDebuggerSetLineBreakpointsEnabledForMacro(g_runtimeEnv.runtimeKv, fileMacroKey, enable)) {
			if (errorMessage != nullptr) *errorMessage = "Debug breakpoints could not be updated.";
			return false;
		}
	if (enabledOut != nullptr) *enabledOut = enable;
	return true;
}

bool mrvmEraseDebugLineBreakpointsForMacroFile(const std::string &macroKey, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MacroRef macroRef;
	LoadedMacroFile file;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	if (errorMessage != nullptr) errorMessage->clear();
	if (normalizedMacroKey.empty() || !readLoadedMacroByKey(normalizedMacroKey, macroRef) || !readLoadedMacroFileByKey(macroRef.fileKey, file)) {
		if (errorMessage != nullptr) *errorMessage = "Debug macro is not loaded.";
		return false;
	}
	for (const std::string &fileMacroKey : file.macroNames)
		if (!mrvmRuntimeDebuggerEraseLineBreakpointsForMacro(g_runtimeEnv.runtimeKv, fileMacroKey)) {
			if (errorMessage != nullptr) *errorMessage = "Debug breakpoints could not be cleared.";
			return false;
		}
	return true;
}

bool mrvmWriteDebugWatch(const std::string &macroKey, const std::string &expression, bool enabled, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	if (errorMessage != nullptr) errorMessage->clear();
	if (normalizedMacroKey.empty()) {
		if (errorMessage != nullptr) *errorMessage = "Debug macro name is empty.";
		return false;
	}
	if (expression.empty()) {
		if (errorMessage != nullptr) *errorMessage = "Watch expression is empty.";
		return false;
	}
	if (!mrvmRuntimeDebuggerWriteWatch(g_runtimeEnv.runtimeKv, normalizedMacroKey, expression, enabled)) {
		if (errorMessage != nullptr) *errorMessage = "Watch expression could not be stored.";
		return false;
	}
	return true;
}

bool mrvmEraseDebugWatch(const std::string &macroKey, const std::string &expression, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	if (errorMessage != nullptr) errorMessage->clear();
	if (normalizedMacroKey.empty()) {
		if (errorMessage != nullptr) *errorMessage = "Debug macro name is empty.";
		return false;
	}
	if (expression.empty()) {
		if (errorMessage != nullptr) *errorMessage = "Watch expression is empty.";
		return false;
	}
	if (!mrvmRuntimeDebuggerEraseWatch(g_runtimeEnv.runtimeKv, normalizedMacroKey, expression)) {
		if (errorMessage != nullptr) *errorMessage = "Watch expression was not found.";
		return false;
	}
	return true;
}

bool mrvmDebugSourceLineForInstruction(const std::string &macroKey, std::size_t bytecodeOffset, int *lineOut, std::size_t *sourceStartOut, std::size_t *sourceEndOut) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MRMacroSourceMapEntry entry;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	if (lineOut != nullptr) *lineOut = 0;
	if (sourceStartOut != nullptr) *sourceStartOut = 0;
	if (sourceEndOut != nullptr) *sourceEndOut = 0;
	if (normalizedMacroKey.empty()) return false;
	if (!mrvmRuntimeCatalogSourceMapSpanForBytecodeOffset(g_runtimeEnv.runtimeKv, normalizedMacroKey, bytecodeOffset, entry)) return false;
	if (lineOut != nullptr) *lineOut = entry.line;
	if (sourceStartOut != nullptr) *sourceStartOut = entry.sourceStartOffset;
	if (sourceEndOut != nullptr) *sourceEndOut = entry.sourceEndOffset;
	return true;
}
