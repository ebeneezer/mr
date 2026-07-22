#include "MRBentoBoxDebuggerStatus.hpp"

#include "../../mrmac/vm/MRVMRuntimeDebugger.hpp"

#include <array>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char *debuggerStateText(const MRMacroDebugRunResult &debugResult) noexcept {
	if (debugResult.hadError) return "error";
	if (debugResult.cancelled) return "cancelled";
	if (debugResult.paused) return "paused";
	return "completed";
}

const char *debuggerFrameKindText(MRMacroDebugStackFrameKind kind) noexcept {
	switch (kind) {
		case mrdStackFrameCall:
			return "CALL";
		case mrdStackFrameRunMacro:
			return "RUN_MACRO";
		case mrdStackFrameCurrent:
		default:
			return "current";
	}
}

void appendDebuggerLocation(std::ostringstream &out, const MRMacroDebugStackFrame &frame) {
	out << (frame.sourcePath.empty() ? "<source unavailable>" : frame.sourcePath);
	if (frame.line > 0) {
		out << ":" << frame.line;
		if (frame.column > 0) out << ":" << frame.column;
	}
}

void appendVariableCounts(std::ostringstream &out, const std::vector<MRMacroDebugVariableSnapshot> &variables) {
	struct ScopeCount {
		MRMacroDebugVariableScope scope;
		const char *label;
		int count;
	};
	std::array<ScopeCount, 5> counts{{
	    {mrdVariableLocal, "locals", 0},
	    {mrdVariableFileGlobal, "file", 0},
	    {mrdVariableAppGlobal, "app", 0},
	    {mrdVariableClosure, "closure", 0},
	    {mrdVariableSession, "session", 0},
	}};

	for (const MRMacroDebugVariableSnapshot &variable : variables)
		for (ScopeCount &count : counts)
			if (variable.scope == count.scope) {
				++count.count;
				break;
			}
	out << "Variables:";
	for (const ScopeCount &count : counts)
		out << " " << count.label << " " << count.count;
	out << "\n";
}

void appendBreakpointCounts(std::ostringstream &out, const std::string &macroKey) {
	std::vector<MRMacroDebuggerBreakpoint> breakpoints;
	int enabled = 0;
	int disabled = 0;

	if (!macroKey.empty()) static_cast<void>(mrvmDebugLineBreakpointsForMacro(macroKey, breakpoints));
	for (const MRMacroDebuggerBreakpoint &breakpoint : breakpoints)
		if (breakpoint.enabled)
			++enabled;
		else
			++disabled;
	out << "Breakpoints: " << enabled << " enabled, " << disabled << " disabled\n";
}

void appendCallStack(std::ostringstream &out, const std::vector<MRMacroDebugStackFrame> &callStack) {
	if (callStack.empty()) return;
	out << "\nCall stack:\n";
	for (std::size_t index = 0; index < callStack.size(); ++index) {
		const MRMacroDebugStackFrame &frame = callStack[index];

		out << "  #" << index << " " << (frame.macroKey.empty() ? "<macro>" : frame.macroKey) << " ";
		appendDebuggerLocation(out, frame);
		out << " [" << debuggerFrameKindText(frame.kind) << "]\n";
	}
}

void appendLogTail(std::ostringstream &out, const std::vector<std::string> &lines) {
	static constexpr std::size_t kMaximumLines = 12;
	const std::size_t first = lines.size() > kMaximumLines ? lines.size() - kMaximumLines : 0;

	if (lines.empty()) return;
	out << "\nLog:";
	if (first > 0) out << " last " << kMaximumLines << " lines";
	out << "\n";
	for (std::size_t index = first; index < lines.size(); ++index)
		out << lines[index] << "\n";
}

}

const char *mrMacroDebuggerStopReasonText(MRMacroDebugStopReason reason) noexcept {
	switch (reason) {
		case mrdStopBreakpoint:
			return "breakpoint";
		case mrdStopStep:
			return "step";
		case mrdStopPaused:
			return "paused";
		case mrdStopBudget:
			return "running";
		case mrdStopCompleted:
			return "completed";
		case mrdStopCancelled:
			return "cancelled";
		case mrdStopError:
			return "error";
		case mrdStopNone:
		default:
			return "none";
	}
}

std::string mrMacroDebuggerStatusText(const std::string &macroName, MRMacroExecutionSessionId sessionId, const MRMacroDebugRunResult &debugResult, const std::string &errorMessage) {
	std::ostringstream out;
	const std::string macroKey = debugResult.macroKey.empty() ? macroName : debugResult.macroKey;

	out << "Macro Debugger\n";
	out << "Macro: " << macroName << "\n";
	if (sessionId != 0) out << "Session: #" << sessionId << "\n";
	else
		out << "Session: none\n";
	out << "State: " << debuggerStateText(debugResult) << "\n";
	out << "Stop: " << mrMacroDebuggerStopReasonText(debugResult.stopReason) << "\n";
	out << "Instruction: " << debugResult.instructionOffset << "  Frames: " << (debugResult.callStack.empty() ? debugResult.stackDepth : debugResult.callStack.size()) << "\n";
	if (!debugResult.callStack.empty()) {
		out << "Location: ";
		appendDebuggerLocation(out, debugResult.callStack.front());
		out << "\n";
	}
	appendVariableCounts(out, debugResult.variables);
	appendBreakpointCounts(out, macroKey);
	appendCallStack(out, debugResult.callStack);
	if (!errorMessage.empty()) out << "\nError:\n" << errorMessage << "\n";
	appendLogTail(out, debugResult.logLines);
	return out.str();
}

std::string mrMacroDebuggerNoticeText(const std::string &macroName, MRMacroExecutionSessionId sessionId, const std::string &message) {
	std::ostringstream out;

	out << "Macro Debugger\n";
	out << "Macro: " << macroName << "\n";
	if (sessionId != 0) out << "Session: #" << sessionId << "\n";
	else
		out << "Session: none\n";
	out << "\n" << message << "\n";
	return out.str();
}
