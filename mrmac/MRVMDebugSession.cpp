#include "MRVMDebugSession.hpp"

#include "vm/MRVMDebugExecution.hpp"
#include "vm/MRVMRuntimeCatalog.hpp"
#include "vm/MRVMRuntimeDebugger.hpp"
#include "vm/MRVMRuntimeState.hpp"
#include "vm/MRVMValue.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace {

struct DebugSessionVmHandle {
	std::unique_ptr<VirtualMachine> vm;
	MRMacroExecutionSession session;
	MRMacroDebugRunResult lastResult;
	MRVMDebugSessionCleanup cleanup;
};

static std::map<MRMacroExecutionSessionId, DebugSessionVmHandle> g_debugSessionVmHandles;

static bool sameSourceMapSpan(const MRMacroSourceMapEntry &left, const MRMacroSourceMapEntry &right) noexcept {
	return left.bytecodeOffset == right.bytecodeOffset && left.sourceStartOffset == right.sourceStartOffset && left.sourceEndOffset == right.sourceEndOffset && left.line == right.line && left.debuggableKind == right.debuggableKind;
}

static void completeDebugSession(std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle, const MRMacroDebugRunResult &result) {
	MRMacroExecutionSession session = sessionHandle->second.session;
	const MRVMDebugSessionCleanup cleanup = sessionHandle->second.cleanup;

	g_debugSessionVmHandles.erase(sessionHandle);
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
	                                           const std::string &macroKey, const std::string &sourcePath, const std::string &parameterString) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	MRMacroExecutionSession session = createMacroExecutionSession(macroName, MRMacroExecutionRoute::Debug, owner);
	std::unique_ptr<VirtualMachine> vm = std::make_unique<VirtualMachine>();
	MRMacroDebugRunResult result = vm->executeDebugAt(bytecode, length, entryOffset, parameterString, macroName, breakpointOffsets, firstRun, macroKey, sourcePath);

	if (result.paused) {
		DebugSessionVmHandle handle;

		session.state = MRMacroExecutionState::Yielded;
		handle.vm = std::move(vm);
		handle.session = session;
		handle.lastResult = result;
		g_debugSessionVmHandles[session.sessionId] = std::move(handle);
		notifyMacroExecutionSessionChanged();
	} else {
		MRVMDebugSessionCleanup cleanup;

		mrvmFinalizeDebugSession(session, result, cleanup);
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
	if (!sessionHandle->second.vm->writeDebugScalarVariable(variable, valueText, updatedVariables, localError)) {
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

	if (sessionHandle == g_debugSessionVmHandles.end() || sessionHandle->second.vm == nullptr) return result;
	sessionHandle->second.session.state = MRMacroExecutionState::Running;
	result = sessionHandle->second.vm->continueDebug(breakpointOffsets);
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

	if (sessionHandle == g_debugSessionVmHandles.end() || sessionHandle->second.vm == nullptr) return result;
	sessionHandle->second.session.state = MRMacroExecutionState::Running;
	result = sessionHandle->second.vm->stepDebug(breakpointOffsets, mode);
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
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);

	if (errorMessage != nullptr) errorMessage->clear();
	if (sessionId == 0 || sessionHandle == g_debugSessionVmHandles.end() || sessionHandle->second.vm == nullptr || sessionHandle->second.session.state != MRMacroExecutionState::Running) {
		if (errorMessage != nullptr) *errorMessage = "Debug session is not running.";
		return false;
	}
	sessionHandle->second.vm->requestDebugPause();
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
	result = sessionHandle->second.vm->continueDebug(breakpointOffsets, kDebugIdleInstructionBudget);
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

bool mrvmCloseDebugSession(MRMacroExecutionSessionId sessionId) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	std::map<MRMacroExecutionSessionId, DebugSessionVmHandle>::iterator sessionHandle = g_debugSessionVmHandles.find(sessionId);

	if (sessionHandle == g_debugSessionVmHandles.end()) return false;
	{
		MRMacroExecutionSession session = sessionHandle->second.session;

		session.state = MRMacroExecutionState::Cancelled;
		g_debugSessionVmHandles.erase(sessionHandle);
		publishMacroExecutionResult(session, session.state, "Debug macro closed.");
	}
	return true;
}
