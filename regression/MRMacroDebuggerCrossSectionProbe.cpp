#include "../mrmac/MRVM.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/vm/MRVMRuntimeDebugger.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

static const char kMainMacroName[] = "DebuggerVariablesProbe";
static const char kChildMacroKey[] = "DEBUGGERVARIABLESFUNCTION";
static const int kMutationLine = 12;
static const int kRunMacroLine = 20;

bool findVariable(const std::vector<MRMacroDebugVariableSnapshot> &variables, const char *name, int type, MRMacroDebugVariableScope scope, MRMacroDebugVariableSnapshot &variable) {
	for (const MRMacroDebugVariableSnapshot &candidate : variables)
		if (candidate.name == name && candidate.type == type && candidate.scope == scope) {
			variable = candidate;
			return true;
		}
	return false;
}

bool variableHasValue(const std::vector<MRMacroDebugVariableSnapshot> &variables, const char *name, int type, MRMacroDebugVariableScope scope, const char *valueText) {
	MRMacroDebugVariableSnapshot variable;

	return findVariable(variables, name, type, scope, variable) && variable.valueText == valueText;
}

bool findWatch(const std::vector<MRMacroDebugWatchSnapshot> &watches, const char *expression, MRMacroDebugWatchSnapshot &watch) {
	for (const MRMacroDebugWatchSnapshot &candidate : watches)
		if (candidate.expression == expression) {
			watch = candidate;
			return true;
		}
	return false;
}

bool startAtSourceLine(int line, MRMacroExecutionSession &session, MRMacroDebugRunResult &result, std::string &errorMessage) {
	result = mrvmStartDebugMacroByName(kMainMacroName, MRMacroExecutionOwner(), &session, &errorMessage, false, line);
	return session.sessionId != 0 && result.stopReason == mrdStopBreakpoint && result.paused && !result.hadError && !result.cancelled;
}

bool completedDebugSessionWasPublished(MRMacroExecutionSessionId sessionId) {
	const std::vector<MRMacroExecutionResult> results = recentMacroExecutionResults();

	for (const MRMacroExecutionResult &result : results)
		if (result.session.sessionId == sessionId && result.state == MRMacroExecutionState::Completed) return true;
	return false;
}

bool cancelledDebugSessionWasPublished(MRMacroExecutionSessionId sessionId) {
	const std::vector<MRMacroExecutionResult> results = recentMacroExecutionResults();

	for (const MRMacroExecutionResult &result : results)
		if (result.session.sessionId == sessionId && result.state == MRMacroExecutionState::Cancelled) return true;
	return false;
}

}

int runMacroDebuggerCrossSectionProbeMode() {
	MRMacroExecutionSession sourceMapSession;
	MRMacroExecutionSession mutationSession;
	MRMacroExecutionSession stepIntoSession;
	MRMacroExecutionSession stepOverSession;
	MRMacroExecutionSession stoppedSession;
	MRMacroDebugRunResult result;
	MRMacroDebugVariableSnapshot mainCounter;
	MRMacroDebugVariableSnapshot mainRatio;
	MRMacroDebugVariableSnapshot mainText;
	MRMacroDebugVariableSnapshot mainFlag;
	MRMacroDebugWatchSnapshot watch;
	std::vector<MRMacroDebugVariableSnapshot> updatedVariables;
	std::vector<MRMacroDebugWatchSnapshot> watches;
	std::vector<MRMacroDebuggerBreakpoint> breakpoints;
	std::string errorMessage;
	bool enabled = false;

	if (!mrvmLoadMacroFile("mrmac/macros/macro_debugger_variables_probe.mrmac", &errorMessage)) {
		std::cerr << "Macro debugger cross-section probe could not load its test macro: " << errorMessage << "\n";
		return 1;
	}
	result = mrvmStartDebugMacroByName(kMainMacroName, MRMacroExecutionOwner(), &sourceMapSession, &errorMessage, true);
	if (sourceMapSession.sessionId == 0 || !result.paused || result.stopReason != mrdStopBreakpoint || !mrvmCloseDebugSession(sourceMapSession.sessionId)) {
		std::cerr << "Macro debugger cross-section probe could not generate the source map: " << errorMessage << "\n";
		return 1;
	}

	if (!mrvmWriteDebugLineBreakpoint(kMainMacroName, kMutationLine, true, &errorMessage) || !mrvmDebugLineBreakpointsForMacro(kMainMacroName, breakpoints) || breakpoints.size() != 1 || !breakpoints[0].enabled) {
		std::cerr << "Macro debugger cross-section probe could not write the source breakpoint: " << errorMessage << "\n";
		return 1;
	}
	if (!mrvmToggleDebugLineBreakpointEnabled(kMainMacroName, kMutationLine, &enabled, &errorMessage) || enabled || !mrvmToggleDebugLineBreakpointEnabled(kMainMacroName, kMutationLine, &enabled, &errorMessage) || !enabled) {
		std::cerr << "Macro debugger cross-section probe could not toggle the source breakpoint: " << errorMessage << "\n";
		return 1;
	}
	if (!mrvmToggleDebugLineBreakpointsEnabledForMacroFile(kMainMacroName, &enabled, &errorMessage) || enabled || !mrvmToggleDebugLineBreakpointsEnabledForMacroFile(kMainMacroName, &enabled, &errorMessage) || !enabled ||
	    !mrvmEraseDebugLineBreakpointsForMacroFile(kMainMacroName, &errorMessage)) {
		std::cerr << "Macro debugger cross-section probe could not toggle or clear all breakpoints: " << errorMessage << "\n";
		return 1;
	}
	breakpoints.clear();
	if (mrvmDebugLineBreakpointsForMacro(kMainMacroName, breakpoints) || !breakpoints.empty()) {
		std::cerr << "Macro debugger cross-section probe did not clear all breakpoints.\n";
		return 1;
	}

	if (!startAtSourceLine(kMutationLine, mutationSession, result, errorMessage) ||
	    !findVariable(result.variables, "MainCounter", TYPE_INT, mrdVariableLocal, mainCounter) ||
	    !findVariable(result.variables, "MainRatio", TYPE_REAL, mrdVariableLocal, mainRatio) ||
	    !findVariable(result.variables, "MainText", TYPE_STR, mrdVariableLocal, mainText) ||
	    !findVariable(result.variables, "MainFlag", TYPE_CHAR, mrdVariableLocal, mainFlag)) {
		std::cerr << "Macro debugger cross-section probe did not expose all scalar variable types: " << errorMessage << "\n";
		return 1;
	}
	if (!mrvmWriteDebugScalarVariable(mutationSession.sessionId, mainCounter, "41", updatedVariables, &errorMessage) || !variableHasValue(updatedVariables, "MainCounter", TYPE_INT, mrdVariableLocal, "41") ||
	    !mrvmWriteDebugScalarVariable(mutationSession.sessionId, mainRatio, "2.5", updatedVariables, &errorMessage) || !variableHasValue(updatedVariables, "MainRatio", TYPE_REAL, mrdVariableLocal, "2.5") ||
	    !mrvmWriteDebugScalarVariable(mutationSession.sessionId, mainText, "edited", updatedVariables, &errorMessage) || !variableHasValue(updatedVariables, "MainText", TYPE_STR, mrdVariableLocal, "edited") ||
	    !mrvmWriteDebugScalarVariable(mutationSession.sessionId, mainFlag, "Q", updatedVariables, &errorMessage) || !variableHasValue(updatedVariables, "MainFlag", TYPE_CHAR, mrdVariableLocal, "Q")) {
		std::cerr << "Macro debugger cross-section probe could not mutate every scalar type: " << errorMessage << "\n";
		return 1;
	}
	if (mrvmWriteDebugScalarVariable(mutationSession.sessionId, mainCounter, "not-an-int", updatedVariables, &errorMessage) || errorMessage.empty() ||
	    mrvmWriteDebugScalarVariable(mutationSession.sessionId, mainFlag, "too-wide", updatedVariables, &errorMessage) || errorMessage.empty()) {
		std::cerr << "Macro debugger cross-section probe accepted invalid scalar mutation input.\n";
		return 1;
	}

	if (!mrvmWriteDebugWatch(kMainMacroName, "MainCounter", true, &errorMessage) || !mrvmWriteDebugWatch(kMainMacroName, "MainText", true, &errorMessage) ||
	    !mrvmWriteDebugWatch(kMainMacroName, "MainRatio", false, &errorMessage) || !mrvmDebugWatchSnapshots(mutationSession.sessionId, kMainMacroName, watches) ||
	    !findWatch(watches, "MainCounter", watch) || watch.valueText != "41" || !watch.errorText.empty() ||
	    !findWatch(watches, "MainText", watch) || watch.valueText != "edited" || !watch.errorText.empty() ||
	    !findWatch(watches, "MainRatio", watch) || watch.errorText != "Watch is disabled.") {
		std::cerr << "Macro debugger cross-section probe did not preserve active and disabled watches: " << errorMessage << "\n";
		return 1;
	}
	if (!mrvmEraseDebugWatch(kMainMacroName, "MainText", &errorMessage) || !mrvmDebugWatchSnapshots(mutationSession.sessionId, kMainMacroName, watches) || watches.size() != 2) {
		std::cerr << "Macro debugger cross-section probe could not erase one watch: " << errorMessage << "\n";
		return 1;
	}

	if (!mrvmScheduleDebugMacroContinue(mutationSession.sessionId, kMainMacroName, &errorMessage) || !mrvmRequestDebugPause(mutationSession.sessionId, &errorMessage) ||
	    !mrvmPumpDebugSession(mutationSession.sessionId, kMainMacroName, result, &errorMessage) || result.stopReason != mrdStopPaused || !result.paused) {
		std::cerr << "Macro debugger cross-section probe did not pause a scheduled continue: " << errorMessage << "\n";
		return 1;
	}
	result = mrvmContinueDebugMacroByName(mutationSession.sessionId, kMainMacroName, &errorMessage);
	if (result.stopReason != mrdStopCompleted || result.paused || result.hadError || result.cancelled || !completedDebugSessionWasPublished(mutationSession.sessionId) ||
	    !variableHasValue(result.variables, "MainCounter", TYPE_INT, mrdVariableLocal, "108") || !variableHasValue(result.variables, "MainRatio", TYPE_REAL, mrdVariableLocal, "2.75") ||
	    !variableHasValue(result.variables, "MainText", TYPE_STR, mrdVariableLocal, "edited:function-local::edited") || !variableHasValue(result.variables, "MainFlag", TYPE_CHAR, mrdVariableLocal, "Q") ||
	    !variableHasValue(result.variables, "DBG_FINAL_COUNTER", TYPE_INT, mrdVariableAppGlobal, "108") || !variableHasValue(result.variables, "DBG_FINAL_TEXT", TYPE_STR, mrdVariableAppGlobal, "edited:function-local::edited")) {
		std::cerr << "Macro debugger cross-section probe did not preserve scalar mutations through completion: " << errorMessage << "\n";
		return 1;
	}
	if (!mrvmEraseDebugWatch(kMainMacroName, "MainCounter", &errorMessage) || !mrvmEraseDebugWatch(kMainMacroName, "MainRatio", &errorMessage)) {
		std::cerr << "Macro debugger cross-section probe could not clear its watches: " << errorMessage << "\n";
		return 1;
	}

	if (!startAtSourceLine(kRunMacroLine, stepIntoSession, result, errorMessage)) {
		std::cerr << "Macro debugger cross-section probe could not stop at RUN_MACRO: " << errorMessage << "\n";
		return 1;
	}
	result = mrvmStepDebugMacroByName(stepIntoSession.sessionId, kMainMacroName, &errorMessage);
	if (!result.paused || result.macroKey != kChildMacroKey || result.hadError || result.cancelled) {
		std::cerr << "Macro debugger cross-section probe did not step into RUN_MACRO: " << errorMessage << "\n";
		return 1;
	}
	result = mrvmStepOutDebugMacroByName(stepIntoSession.sessionId, kMainMacroName, &errorMessage);
	if (!result.paused || result.macroKey != "DEBUGGERVARIABLESPROBE" || result.hadError || result.cancelled || !mrvmCloseDebugSession(stepIntoSession.sessionId)) {
		std::cerr << "Macro debugger cross-section probe did not step out of RUN_MACRO: " << errorMessage << "\n";
		return 1;
	}

	if (!startAtSourceLine(kRunMacroLine, stepOverSession, result, errorMessage)) {
		std::cerr << "Macro debugger cross-section probe could not restart at RUN_MACRO: " << errorMessage << "\n";
		return 1;
	}
	result = mrvmStepOverDebugMacroByName(stepOverSession.sessionId, kMainMacroName, &errorMessage);
	if (!result.paused || result.macroKey != "DEBUGGERVARIABLESPROBE" || result.hadError || result.cancelled || !mrvmCloseDebugSession(stepOverSession.sessionId)) {
		std::cerr << "Macro debugger cross-section probe did not step over RUN_MACRO: " << errorMessage << "\n";
		return 1;
	}

	if (!startAtSourceLine(kMutationLine, stoppedSession, result, errorMessage) || !mrvmCloseDebugSession(stoppedSession.sessionId) || !cancelledDebugSessionWasPublished(stoppedSession.sessionId)) {
		std::cerr << "Macro debugger cross-section probe did not stop and publish cancellation: " << errorMessage << "\n";
		return 1;
	}

	std::cout << "macro-debugger-cross-section scalar=4 watches=3 pause=1 steps=3 stop=1\n";
	return 0;
}
