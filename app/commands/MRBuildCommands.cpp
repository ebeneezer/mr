#define Uses_TEvent
#include <tvision/tv.h>

#include "MRBuildCommands.hpp"

#include "../../coprocessor/MRCoprocessor.hpp"
#include "../MRDebuggerCommandRoute.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRWindowSupport.hpp"

bool mrContinueDebuggerAfterBuild(const mr::coprocessor::ExternalIoFinishedPayload &payload) {
	if (payload.debuggerContinuation == mr::coprocessor::BuildDebuggerContinuation::None || payload.buildSourceBufferId == 0) return false;
	MREditWindow *sourceWindow = findEditWindowByBufferId(payload.buildSourceBufferId);
	return mrStartGdbDebuggerForWindow(sourceWindow, payload.debuggerContinuation == mr::coprocessor::BuildDebuggerContinuation::StartAndRun);
}
