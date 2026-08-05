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
#include "vm/MRVMDebugExecution.hpp"
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

VirtualMachine::DebugState::DebugState() noexcept : runActive(false), stopped(false), stopReason(mrdStopNone), stopOffset(0), stackDepth(0), breakpointOffsets(), paused(false), bytecode(), length(0), ip(0), callStack(), returnInt(0), returnStr(), errorLevel(0), savedParameterString(), macroName(), firstRun(false), skipCurrentOffset(false), pauseRequested(false), pauseSignal(), instructionBudget(0), stepMode(mrdStepNone), stepOutDepth(0), macroKey(), sourcePath(), childFrame() {
}

VirtualMachine::DebugState::~DebugState() = default;

void VirtualMachine::DebugState::capturePausedExecution(const unsigned char *sourceBytecode, std::size_t sourceLength, std::size_t sourceIp, const std::vector<std::size_t> &sourceCallStack, const ExecutionState &executionState, const std::string &sourceSavedParameterString, const std::string &sourceMacroName, bool sourceFirstRun) {
	paused = true;
	bytecode.assign(sourceBytecode, sourceBytecode + sourceLength);
	length = sourceLength;
	ip = sourceIp;
	callStack = sourceCallStack;
	returnInt = executionState.returnInt;
	returnStr = executionState.returnStr;
	errorLevel = executionState.errorLevel;
	savedParameterString = sourceSavedParameterString;
	macroName = sourceMacroName;
	firstRun = sourceFirstRun;
}

void VirtualMachine::DebugState::clearPausedExecution() noexcept {
	paused = false;
	bytecode.clear();
	length = 0;
	ip = 0;
	callStack.clear();
	returnInt = 0;
	returnStr.clear();
	errorLevel = 0;
	savedParameterString.clear();
	macroName.clear();
	firstRun = false;
	skipCurrentOffset = false;
	pauseRequested = false;
	if (pauseSignal != nullptr) pauseSignal->store(false, std::memory_order_release);
	instructionBudget = 0;
	stepMode = mrdStepNone;
}

VirtualMachine::DebugExecution::DebugExecution(VirtualMachine &machine) noexcept : vm(machine) {
}

MRMacroDebugWatchSnapshot VirtualMachine::evaluateDebugWatchExpression(const std::string &expression) {
	return DebugExecution(*this).evaluateWatchExpression(expression);
}

MRMacroDebugWatchSnapshot VirtualMachine::DebugExecution::evaluateWatchExpression(const std::string &expression) {
	MRMacroDebugWatchSnapshot snapshot;
	std::vector<MRMacWatchSymbol> symbols;
	std::vector<unsigned char> bytecode;
	std::size_t bytecodeSize = 0;
	int resultType = 0;
	unsigned char *compiled = nullptr;
	const std::vector<VirtualMachine::Value> savedStack = vm.stack;
	const std::vector<std::string> savedLog = vm.log;
	const bool savedLogTruncated = vm.logTruncated;
	const bool savedCancelledExecution = vm.cancelledExecution;
	const int savedReturnInt = runtimeReturnInt();
	const std::string savedReturnStr = runtimeReturnStr();
	const int savedErrorLevel = runtimeErrorLevel();
	const std::string savedParameterString = runtimeParameterString();

	snapshot.expression = expression;
	for (const std::pair<const std::string, VirtualMachine::Value> &entry : vm.variables)
		symbols.push_back(MRMacWatchSymbol{entry.first.c_str(), entry.second.type});
	compiled = compile_macro_watch_expression(expression.c_str(), symbols.empty() ? nullptr : symbols.data(), symbols.size(), &bytecodeSize, &resultType);
	if (compiled == nullptr) {
		const char *error = get_last_compile_error();

		snapshot.errorText = error != nullptr && *error != '\0' ? error : "Watch expression could not be compiled.";
		return snapshot;
	}
	bytecode.assign(compiled, compiled + bytecodeSize);
	std::free(compiled);
	vm.executeAt(bytecode.data(), bytecode.size(), 0, std::string(), std::string(), false, false, true);
	for (std::size_t index = savedLog.size(); index < vm.log.size(); ++index)
		if (vm.log[index].rfind("VM Error: ", 0) == 0) {
			snapshot.errorText = vm.log[index].substr(std::strlen("VM Error: "));
			break;
		}
	if (snapshot.errorText.empty()) {
		if (vm.stack.size() != 1) snapshot.errorText = "Watch expression did not produce one value.";
		else {
			snapshot.type = resultType;
			snapshot.valueText = macroDebugValueText(vm.stack.back(), *vm.mHashStore, mrvmRuntimeKv().globalStore());
		}
	}
	vm.stack = savedStack;
	vm.log = savedLog;
	vm.logTruncated = savedLogTruncated;
	vm.cancelledExecution = savedCancelledExecution;
	setRuntimeReturnInt(savedReturnInt);
	setRuntimeReturnStr(savedReturnStr);
	setRuntimeErrorLevel(savedErrorLevel);
	setRuntimeParameterString(savedParameterString);
	return snapshot;
}

MRMacroDebugRunResult VirtualMachine::executeDebugAt(const unsigned char *bytecode, size_t length, size_t entryOffset, const std::string &parameterString, const std::string &macroName, const std::vector<std::size_t> &breakpointOffsets, bool firstRun, const std::string &macroKey, const std::string &sourcePath) {
	return DebugExecution(*this).start(bytecode, length, entryOffset, parameterString, macroName, breakpointOffsets, firstRun, macroKey, sourcePath);
}

MRMacroDebugRunResult VirtualMachine::DebugExecution::start(const unsigned char *bytecode, size_t length, size_t entryOffset, const std::string &parameterString, const std::string &macroName, const std::vector<std::size_t> &breakpointOffsets, bool firstRun, const std::string &macroKey, const std::string &sourcePath) {
	MRMacroDebugRunResult result;
	const bool savedAsyncDelayEnabled = vm.delayState.enabled;
	bool hadError = false;

	vm.debugState.runActive = true;
	vm.debugState.stopped = false;
	vm.debugState.stopReason = mrdStopNone;
	vm.debugState.stopOffset = 0;
	vm.debugState.stackDepth = 0;
	vm.debugState.paused = false;
	vm.debugState.bytecode.clear();
	vm.debugState.length = 0;
	vm.debugState.ip = 0;
	vm.debugState.callStack.clear();
	vm.debugState.skipCurrentOffset = false;
	vm.debugState.pauseRequested = false;
	if (vm.debugState.pauseSignal != nullptr) vm.debugState.pauseSignal->store(false, std::memory_order_release);
	vm.debugState.instructionBudget = 0;
	vm.debugState.stepMode = mrdStepNone;
	vm.debugState.stepOutDepth = 0;
	vm.debugState.macroKey = macroKey.empty() ? mrvmUpperKey(macroName) : macroKey;
	vm.debugState.sourcePath = sourcePath;
	vm.debugState.childFrame.reset();
	vm.debugState.breakpointOffsets = breakpointOffsets;
	std::sort(vm.debugState.breakpointOffsets.begin(), vm.debugState.breakpointOffsets.end());
	vm.debugState.breakpointOffsets.erase(std::unique(vm.debugState.breakpointOffsets.begin(), vm.debugState.breakpointOffsets.end()), vm.debugState.breakpointOffsets.end());
	vm.delayState.enabled = false;

	vm.executeAt(bytecode, length, entryOffset, parameterString, macroName, true, firstRun);
	if (vm.debugState.childFrame != nullptr) {
		result = vm.debugState.childFrame->result;
		appendParentCallStack(result, vm.debugState.childFrame->parentInstructionOffset);
		vm.debugState.childFrame->result = result;
		vm.debugState.runActive = false;
		vm.delayState.enabled = savedAsyncDelayEnabled;
		return result;
	}

	for (const std::string &line : vm.log)
		if (line.rfind("VM Error:", 0) == 0) {
			hadError = true;
			break;
		}

	if (vm.debugState.stopped) result.stopReason = vm.debugState.stopReason;
	else if (vm.cancelledExecution)
		result.stopReason = mrdStopCancelled;
	else if (hadError)
		result.stopReason = mrdStopError;
	else
		result.stopReason = mrdStopCompleted;
	result.instructionOffset = vm.debugState.stopOffset;
	result.stackDepth = vm.debugState.stackDepth;
	result.logLines = vm.log;
	result.macroKey = vm.debugState.macroKey;
	result.sourcePath = vm.debugState.sourcePath;
	result.cancelled = vm.cancelledExecution;
	result.hadError = hadError;
	result.paused = vm.debugState.paused;
	vm.appendDebugVariables(result);
	appendCallStack(result);

	vm.debugState.runActive = false;
	vm.delayState.enabled = savedAsyncDelayEnabled;
	return result;
}

MRMacroDebugRunResult VirtualMachine::continueDebug(const std::vector<std::size_t> &breakpointOffsets, std::size_t instructionBudget) {
	return DebugExecution(*this).continueExecution(breakpointOffsets, instructionBudget);
}

MRMacroDebugRunResult VirtualMachine::DebugExecution::continueExecution(const std::vector<std::size_t> &breakpointOffsets, std::size_t instructionBudget) {
	MRMacroDebugRunResult result;
	const bool savedAsyncDelayEnabled = vm.delayState.enabled;
	const MRMacroDebugStopReason previousStopReason = vm.debugState.stopReason;
	bool hadError = false;

	if (!vm.debugState.paused) return result;
	const bool pauseSignalled = vm.debugState.pauseSignal != nullptr && vm.debugState.pauseSignal->exchange(false, std::memory_order_acq_rel);
	if (vm.debugState.pauseRequested || pauseSignalled) {
		vm.debugState.stopped = true;
		vm.debugState.stopReason = mrdStopPaused;
		vm.debugState.stopOffset = vm.debugState.ip;
		vm.debugState.stackDepth = vm.debugState.callStack.size();
		vm.debugState.pauseRequested = false;
		result.stopReason = mrdStopPaused;
		result.instructionOffset = vm.debugState.stopOffset;
		result.stackDepth = vm.debugState.stackDepth;
		result.logLines = vm.log;
		result.macroKey = vm.debugState.macroKey;
		result.sourcePath = vm.debugState.sourcePath;
		result.paused = true;
		vm.appendDebugVariables(result);
		appendCallStack(result);
		return result;
	}
	vm.debugState.runActive = true;
	vm.debugState.stopped = false;
	vm.debugState.stopReason = mrdStopNone;
	vm.debugState.stackDepth = 0;
	vm.debugState.breakpointOffsets = breakpointOffsets;
	std::sort(vm.debugState.breakpointOffsets.begin(), vm.debugState.breakpointOffsets.end());
	vm.debugState.breakpointOffsets.erase(std::unique(vm.debugState.breakpointOffsets.begin(), vm.debugState.breakpointOffsets.end()), vm.debugState.breakpointOffsets.end());
	vm.debugState.skipCurrentOffset = previousStopReason == mrdStopBreakpoint || previousStopReason == mrdStopPaused;
	vm.debugState.stepMode = mrdStepNone;
	vm.debugState.instructionBudget = instructionBudget;
	vm.delayState.enabled = false;
	if (vm.debugState.childFrame != nullptr) {
		std::vector<std::size_t> childBreakpointOffsets;

		static_cast<void>(mrvmCollectDebugBreakpointOffsetsForLoadedFile(vm.debugState.childFrame->macroKey, childBreakpointOffsets));
		result = vm.debugState.childFrame->vm->continueDebug(childBreakpointOffsets);
		vm.debugState.childFrame->result = result;
		if (result.paused) {
			appendParentCallStack(result, vm.debugState.childFrame->parentInstructionOffset);
			vm.debugState.childFrame->result = result;
			vm.debugState.runActive = false;
			vm.delayState.enabled = savedAsyncDelayEnabled;
			return result;
		}
		vm.log.insert(vm.log.end(), vm.debugState.childFrame->vm->log.begin(), vm.debugState.childFrame->vm->log.end());
		if (vm.debugState.childFrame->unloadAfterCompletion) unloadMacroFromRegistry(vm.debugState.childFrame->macroKey);
		else if (vm.debugState.childFrame->evictTransientAfterCompletion)
			evictTransientFileImage(vm.debugState.childFrame->fileKey);
		vm.debugState.childFrame.reset();
		vm.debugState.skipCurrentOffset = false;
	}

	vm.executeAt(nullptr, 0, 0, std::string(), std::string(), false, false);
	if (vm.debugState.childFrame != nullptr) {
		result = vm.debugState.childFrame->result;
		appendParentCallStack(result, vm.debugState.childFrame->parentInstructionOffset);
		vm.debugState.childFrame->result = result;
		vm.debugState.runActive = false;
		vm.debugState.stepMode = mrdStepNone;
		vm.delayState.enabled = savedAsyncDelayEnabled;
		return result;
	}

	for (const std::string &line : vm.log)
		if (line.rfind("VM Error:", 0) == 0) {
			hadError = true;
			break;
		}

	if (vm.debugState.stopped) result.stopReason = vm.debugState.stopReason;
	else if (vm.cancelledExecution)
		result.stopReason = mrdStopCancelled;
	else if (hadError)
		result.stopReason = mrdStopError;
	else
		result.stopReason = mrdStopCompleted;
	result.instructionOffset = vm.debugState.stopOffset;
	result.stackDepth = vm.debugState.stackDepth;
	result.logLines = vm.log;
	result.macroKey = vm.debugState.macroKey;
	result.sourcePath = vm.debugState.sourcePath;
	result.cancelled = vm.cancelledExecution;
	result.hadError = hadError;
	result.paused = vm.debugState.paused;
	vm.appendDebugVariables(result);
	appendCallStack(result);

	vm.debugState.runActive = false;
	vm.delayState.enabled = savedAsyncDelayEnabled;
	return result;
}

MRMacroDebugRunResult VirtualMachine::stepDebug(const std::vector<std::size_t> &breakpointOffsets, MRMacroDebugStepMode mode) {
	return DebugExecution(*this).step(breakpointOffsets, mode);
}

MRMacroDebugRunResult VirtualMachine::DebugExecution::step(const std::vector<std::size_t> &breakpointOffsets, MRMacroDebugStepMode mode) {
	MRMacroDebugRunResult result;
	const bool savedAsyncDelayEnabled = vm.delayState.enabled;
	const MRMacroDebugStopReason previousStopReason = vm.debugState.stopReason;
	bool hadError = false;

	if (!vm.debugState.paused) return result;
	vm.debugState.runActive = true;
	vm.debugState.stopped = false;
	vm.debugState.stopReason = mrdStopNone;
	vm.debugState.stackDepth = 0;
	vm.debugState.breakpointOffsets = breakpointOffsets;
	std::sort(vm.debugState.breakpointOffsets.begin(), vm.debugState.breakpointOffsets.end());
	vm.debugState.breakpointOffsets.erase(std::unique(vm.debugState.breakpointOffsets.begin(), vm.debugState.breakpointOffsets.end()), vm.debugState.breakpointOffsets.end());
	vm.debugState.skipCurrentOffset = previousStopReason == mrdStopBreakpoint;
	vm.debugState.stepMode = mode;
	vm.debugState.instructionBudget = 0;
	vm.debugState.stepOutDepth = vm.debugState.callStack.size();
	vm.delayState.enabled = false;
	if (vm.debugState.childFrame != nullptr) {
		std::vector<std::size_t> childBreakpointOffsets;

		static_cast<void>(mrvmCollectDebugBreakpointOffsetsForLoadedFile(vm.debugState.childFrame->macroKey, childBreakpointOffsets));
		result = vm.debugState.childFrame->vm->stepDebug(childBreakpointOffsets, mode);
		vm.debugState.childFrame->result = result;
		if (result.paused) {
			appendParentCallStack(result, vm.debugState.childFrame->parentInstructionOffset);
			vm.debugState.childFrame->result = result;
			vm.debugState.runActive = false;
			vm.debugState.stepMode = mrdStepNone;
			vm.delayState.enabled = savedAsyncDelayEnabled;
			return result;
		}
		vm.log.insert(vm.log.end(), vm.debugState.childFrame->vm->log.begin(), vm.debugState.childFrame->vm->log.end());
		if (vm.debugState.childFrame->unloadAfterCompletion) unloadMacroFromRegistry(vm.debugState.childFrame->macroKey);
		else if (vm.debugState.childFrame->evictTransientAfterCompletion)
			evictTransientFileImage(vm.debugState.childFrame->fileKey);
		vm.debugState.childFrame.reset();
		vm.debugState.stopped = true;
		vm.debugState.stopReason = mrdStopStep;
		vm.debugState.stopOffset = vm.debugState.ip;
		vm.debugState.stackDepth = vm.debugState.callStack.size();
		vm.debugState.paused = true;
		result.stopReason = mrdStopStep;
		result.instructionOffset = vm.debugState.stopOffset;
		result.stackDepth = vm.debugState.stackDepth;
		result.logLines = vm.log;
		result.macroKey = vm.debugState.macroKey;
		result.sourcePath = vm.debugState.sourcePath;
		result.cancelled = false;
		result.hadError = false;
		result.paused = true;
		vm.appendDebugVariables(result);
		appendCallStack(result);
		vm.debugState.runActive = false;
		vm.debugState.stepMode = mrdStepNone;
		vm.delayState.enabled = savedAsyncDelayEnabled;
		return result;
	}

	vm.executeAt(nullptr, 0, 0, std::string(), std::string(), false, false);
	if (vm.debugState.childFrame != nullptr) {
		result = vm.debugState.childFrame->result;
		appendParentCallStack(result, vm.debugState.childFrame->parentInstructionOffset);
		vm.debugState.childFrame->result = result;
		vm.debugState.runActive = false;
		vm.debugState.stepMode = mrdStepNone;
		vm.delayState.enabled = savedAsyncDelayEnabled;
		return result;
	}

	for (const std::string &line : vm.log)
		if (line.rfind("VM Error:", 0) == 0) {
			hadError = true;
			break;
		}

	if (vm.debugState.stopped) result.stopReason = vm.debugState.stopReason;
	else if (vm.cancelledExecution)
		result.stopReason = mrdStopCancelled;
	else if (hadError)
		result.stopReason = mrdStopError;
	else
		result.stopReason = mrdStopCompleted;
	result.instructionOffset = vm.debugState.stopOffset;
	result.stackDepth = vm.debugState.stackDepth;
	result.logLines = vm.log;
	result.macroKey = vm.debugState.macroKey;
	result.sourcePath = vm.debugState.sourcePath;
	result.cancelled = vm.cancelledExecution;
	result.hadError = hadError;
	result.paused = vm.debugState.paused;
	vm.appendDebugVariables(result);
	appendCallStack(result);

	vm.debugState.runActive = false;
	vm.debugState.stepMode = mrdStepNone;
	vm.delayState.enabled = savedAsyncDelayEnabled;
	return result;
}

void mrvmFinalizeDebugSession(MRMacroExecutionSession &session, const MRMacroDebugRunResult &result, const MRVMDebugSessionCleanup &cleanup) {
	session.state = result.cancelled ? MRMacroExecutionState::Cancelled : (result.hadError ? MRMacroExecutionState::Failed : MRMacroExecutionState::Completed);
	if (cleanup.unloadAfterCompletion && !cleanup.macroKey.empty()) unloadMacroFromRegistry(cleanup.macroKey);
	else if (cleanup.evictTransientAfterCompletion && !cleanup.fileKey.empty())
		evictTransientFileImage(cleanup.fileKey);
	publishMacroExecutionResult(session, session.state, result.hadError ? "Debug macro failed." : "Debug macro completed.");
}

void mrvmRejectDebugSession(MRMacroExecutionSession &session, const MRVMDebugSessionCleanup &cleanup, const std::string &message) {
	session.state = MRMacroExecutionState::Rejected;
	if (cleanup.unloadAfterCompletion && !cleanup.macroKey.empty()) unloadMacroFromRegistry(cleanup.macroKey);
	else if (cleanup.evictTransientAfterCompletion && !cleanup.fileKey.empty())
		evictTransientFileImage(cleanup.fileKey);
	publishMacroExecutionResult(session, session.state, message.empty() ? "Staged debug macro result rejected." : message);
}

static MRMacroDebugRunResult startDebugMacroByKey(const std::string &macroKey, const std::string &parameterString, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut, std::string *errorMessage, bool stopAtEntry, int temporaryStopLine) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MRMacroDebugRunResult result;
	MacroRef macroRef;
	LoadedMacroFile file;
	MRMacroExecutionSession debugSession;
	MRMacroSourceMapEntry temporaryStopSpan;
	MRMacroStagedExecutionInput stagedInput;
	MacroCommitConflictSnapshot conflictSnapshot;
	std::vector<std::size_t> breakpointOffsets;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);
	MRMacroExecutionRoute route = MRMacroExecutionRoute::Debug;
	bool firstRun = false;
	bool automaticContinueFromEntry = false;
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
		if (!mrvmRuntimeCatalogFirstSourceMapSpanForLine(mrvmRuntimeKv(), normalizedMacroKey, temporaryStopLine, temporaryStopSpan)) {
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
	if (mrvmCanRunInBackground(file.profile))
		route = MRMacroExecutionRoute::Background;
	else if (mrvmCanRunStagedInBackground(file.profile))
		route = MRMacroExecutionRoute::StagedBackground;
	if (route == MRMacroExecutionRoute::StagedBackground && !owner.hasBuffer) route = MRMacroExecutionRoute::Debug;
	if (route == MRMacroExecutionRoute::StagedBackground) {
		MREditWindow *targetWindow = owner.hasBuffer ? findEditWindowByBufferId(owner.bufferId) : nullptr;

		if (!captureMacroStagedExecutionInput(targetWindow, stagedInput, conflictSnapshot)) {
			result.stopReason = mrdStopError;
			result.hadError = true;
			result.logLines.push_back("VM Error: staged debug session has no live editor owner.");
			if (errorMessage != nullptr) *errorMessage = "Staged debug session requires a live editor owner.";
			return result;
		}
	}
	if (!stopAtEntry && std::find(breakpointOffsets.begin(), breakpointOffsets.end(), macroRef.entryOffset) == breakpointOffsets.end()) {
		breakpointOffsets.push_back(macroRef.entryOffset);
		std::sort(breakpointOffsets.begin(), breakpointOffsets.end());
		automaticContinueFromEntry = true;
	}
	result = mrvmStartDebugSessionAt(file.bytecode.data(), file.bytecode.size(), macroRef.entryOffset, macroRef.displayName, owner, breakpointOffsets, &debugSession, firstRun, normalizedMacroKey, file.resolvedPath, parameterString, route,
	                                route == MRMacroExecutionRoute::StagedBackground ? &stagedInput : nullptr, route == MRMacroExecutionRoute::StagedBackground ? &conflictSnapshot : nullptr, automaticContinueFromEntry,
	                                temporaryStopLine > 0, temporaryStopSpan.bytecodeOffset);
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

bool mrvmMacroSpecHasEnabledDebugBreakpoint(const std::string &spec, std::string *sourcePath, std::string *macroKeyOut) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	LoadedMacroFile file;
	std::vector<MRMacroDebuggerBreakpoint> breakpoints;
	std::string macroKey;
	std::string parameterString;
	std::string resolutionError;

	if (sourcePath != nullptr) sourcePath->clear();
	if (macroKeyOut != nullptr) macroKeyOut->clear();
	if (!resolveDebugMacroSpec(spec, macroKey, parameterString, file, resolutionError)) return false;
	if (sourcePath != nullptr) *sourcePath = file.resolvedPath;
	if (macroKeyOut != nullptr) *macroKeyOut = macroKey;
	if (!mrvmRuntimeDebuggerLineBreakpointsForMacro(mrvmRuntimeKv(), macroKey, breakpoints)) return false;
	for (const MRMacroDebuggerBreakpoint &breakpoint : breakpoints)
		if (breakpoint.enabled) return true;
	return false;
}

bool mrvmPrepareDebugMacroSourceMap(const std::string &macroKey, const std::string &sourcePath, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MacroRef macroRef;
	LoadedMacroFile file;
	std::string localError;

	if (errorMessage != nullptr) errorMessage->clear();
	if (!prepareDebugMacroSourceMapByKey(macroKey, sourcePath, macroRef, file, localError)) {
		if (errorMessage != nullptr) *errorMessage = localError;
		return false;
	}
	return true;
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
	if (mrvmRuntimeDebuggerReadLineBreakpoint(mrvmRuntimeKv(), normalizedMacroKey, line, breakpoint)) {
		if (!mrvmRuntimeDebuggerEraseLineBreakpoint(mrvmRuntimeKv(), normalizedMacroKey, line)) {
			if (errorMessage != nullptr) *errorMessage = "Debug breakpoint could not be cleared.";
			return false;
		}
		return true;
	}
	if (!mrvmRuntimeDebuggerWriteLineBreakpoint(mrvmRuntimeKv(), normalizedMacroKey, line, true, std::string())) {
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
	if (!mrvmRuntimeDebuggerReadLineBreakpoint(mrvmRuntimeKv(), normalizedMacroKey, line, breakpoint)) {
		if (!mrvmRuntimeDebuggerWriteLineBreakpoint(mrvmRuntimeKv(), normalizedMacroKey, line, true, std::string())) {
			if (errorMessage != nullptr) *errorMessage = "No debuggable source span for breakpoint line.";
			return false;
		}
		if (enabledOut != nullptr) *enabledOut = true;
		return true;
	}
	if (!mrvmRuntimeDebuggerWriteLineBreakpoint(mrvmRuntimeKv(), normalizedMacroKey, line, !breakpoint.enabled, breakpoint.conditionText)) {
		if (errorMessage != nullptr) *errorMessage = "Debug breakpoint could not be updated.";
		return false;
	}
	if (enabledOut != nullptr) *enabledOut = !breakpoint.enabled;
	return true;
}

bool mrvmWriteDebugLineBreakpoint(const std::string &macroKey, int line, bool enabled, std::string *errorMessage, const std::string &conditionText) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	if (errorMessage != nullptr) errorMessage->clear();
	if (normalizedMacroKey.empty() || line <= 0) {
		if (errorMessage != nullptr) *errorMessage = "Debug breakpoint is invalid.";
		return false;
	}
	if (!mrvmRuntimeDebuggerWriteLineBreakpoint(mrvmRuntimeKv(), normalizedMacroKey, line, enabled, conditionText)) {
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

		if (!mrvmRuntimeDebuggerLineBreakpointsForMacro(mrvmRuntimeKv(), fileMacroKey, macroBreakpoints)) continue;
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

		if (!mrvmRuntimeDebuggerLineBreakpointsForMacro(mrvmRuntimeKv(), fileMacroKey, macroBreakpoints)) continue;
		breakpoints.insert(breakpoints.end(), macroBreakpoints.begin(), macroBreakpoints.end());
	}
	for (const MRMacroDebuggerBreakpoint &breakpoint : breakpoints)
		if (breakpoint.enabled) {
			enable = false;
			break;
		}
	for (const std::string &fileMacroKey : file.macroNames)
		if (!mrvmRuntimeDebuggerSetLineBreakpointsEnabledForMacro(mrvmRuntimeKv(), fileMacroKey, enable)) {
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
		if (!mrvmRuntimeDebuggerEraseLineBreakpointsForMacro(mrvmRuntimeKv(), fileMacroKey)) {
			if (errorMessage != nullptr) *errorMessage = "Debug breakpoints could not be cleared.";
			return false;
		}
	return true;
}

bool mrvmEraseDebugRuntimeForMacro(const std::string &macroKey, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	if (errorMessage != nullptr) errorMessage->clear();
	if (normalizedMacroKey.empty()) {
		if (errorMessage != nullptr) *errorMessage = "Debug macro name is empty.";
		return false;
	}
	if (!mrvmRuntimeDebuggerEraseLineBreakpointsForMacro(mrvmRuntimeKv(), normalizedMacroKey) || !mrvmRuntimeDebuggerEraseWatchesForMacro(mrvmRuntimeKv(), normalizedMacroKey)) {
		if (errorMessage != nullptr) *errorMessage = "Debug runtime state could not be cleared.";
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
	if (!validate_macro_watch_expression(expression.c_str())) {
		const char *compileError = get_last_compile_error();

		if (errorMessage != nullptr) *errorMessage = compileError != nullptr && *compileError != '\0' ? compileError : "Watch expression is invalid.";
		return false;
	}
	if (!mrvmRuntimeDebuggerWriteWatch(mrvmRuntimeKv(), normalizedMacroKey, expression, enabled)) {
		if (errorMessage != nullptr) *errorMessage = "Watch expression could not be stored.";
		return false;
	}
	return true;
}

bool mrvmValidateDebugWatchExpression(const std::string &expression, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);

	if (errorMessage != nullptr) errorMessage->clear();
	if (validate_macro_watch_expression(expression.c_str())) return true;
	if (errorMessage != nullptr) {
		const char *compileError = get_last_compile_error();

		*errorMessage = compileError != nullptr && *compileError != '\0' ? compileError : "Watch expression is invalid.";
	}
	return false;
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
	if (!mrvmRuntimeDebuggerEraseWatch(mrvmRuntimeKv(), normalizedMacroKey, expression)) {
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
	if (!mrvmRuntimeCatalogSourceMapSpanForBytecodeOffset(mrvmRuntimeKv(), normalizedMacroKey, bytecodeOffset, entry)) return false;
	if (lineOut != nullptr) *lineOut = entry.line;
	if (sourceStartOut != nullptr) *sourceStartOut = entry.sourceStartOffset;
	if (sourceEndOut != nullptr) *sourceEndOut = entry.sourceEndOffset;
	return true;
}
