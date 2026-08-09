#define Uses_Dialogs
#define Uses_TInputLine
#include "MRBentoBox.hpp"

#include "MRBentoBoxDebuggerStatus.hpp"

#include "../MRFrame.hpp"

#include "../../app/commands/MRWindowCommands.hpp"
#include "../../dialogs/setup/MRSetupCommon.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../coprocessor/MRCoprocessor.hpp"
#include "../../mrmac/MRVM.hpp"
#include "../../mrmac/mrmac.h"
#include "../../mrmac/vm/MRVMMacroSpecRuntime.hpp"
#include "../../mrmac/vm/MRVMRuntimeDebugger.hpp"

#include <array>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <utility>

class MRMacroDebuggerValueInput : public TInputLine {
  public:
	MRMacroDebuggerValueInput(const TRect &bounds, MRBentoBox *bento) noexcept : TInputLine(bounds, 255), mBento(bento), mError(false) {
	}

	TPalette &getPalette() const override {
		static const char active[] = {static_cast<char>(kMrPaletteDebuggerInputActive), static_cast<char>(kMrPaletteDebuggerInputActive), static_cast<char>(kMrPaletteDebuggerInputActive), static_cast<char>(kMrPaletteDebuggerInputActive)};
		static const char error[] = {static_cast<char>(kMrPaletteDebuggerInputError), static_cast<char>(kMrPaletteDebuggerInputError), static_cast<char>(kMrPaletteDebuggerInputError), static_cast<char>(kMrPaletteDebuggerInputError)};
		static TPalette activePalette(active, sizeof(active));
		static TPalette errorPalette(error, sizeof(error));

		return mError ? errorPalette : activePalette;
	}

	void setError(bool error) noexcept {
		if (mError == error) return;
		mError = error;
		drawView();
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown) {
			const ushort key = ctrlToArrow(event.keyDown.keyCode);

			if (key == kbEnter) {
				if (mBento != nullptr) mBento->commitMacroDebuggerValueInput();
				clearEvent(event);
				return;
			}
			if (key == kbEsc) {
				if (mBento != nullptr) mBento->cancelMacroDebuggerValueInput();
				clearEvent(event);
				return;
			}
		}
		TInputLine::handleEvent(event);
	}

  private:
	MRBentoBox *mBento;
	bool mError;
};

namespace {

const char *macroDebuggerVariableTypeText(int type) noexcept {
	switch (type) {
		case TYPE_INT:
			return "int";
		case TYPE_STR:
			return "str";
		case TYPE_CHAR:
			return "char";
		case TYPE_REAL:
			return "real";
		case TYPE_HASH:
			return "hash";
		case TYPE_INT_ARRAY:
			return "int[]";
		case TYPE_STR_ARRAY:
			return "str[]";
		case TYPE_CHAR_ARRAY:
			return "char[]";
		case TYPE_REAL_ARRAY:
			return "real[]";
		case TYPE_HASH_ARRAY:
			return "hash[]";
		default:
			return "unknown";
	}
}

bool macroDebuggerVariableIsArray(int type) noexcept {
	return type == TYPE_INT_ARRAY || type == TYPE_STR_ARRAY || type == TYPE_CHAR_ARRAY || type == TYPE_REAL_ARRAY || type == TYPE_HASH_ARRAY;
}

bool macroDebuggerRouteUsesWorker(MRMacroExecutionRoute route) noexcept {
	return route == MRMacroExecutionRoute::Background || route == MRMacroExecutionRoute::StagedBackground;
}

struct MacroDebuggerVariableGroup {
	MRMacroDebugVariableScope scope;
	const char *title;
};

static const MacroDebuggerVariableGroup kMacroDebuggerVariableGroups[] = {
	{mrdVariableLocal, "Locals"},
	{mrdVariableAppGlobal, "App globals"},
	{mrdVariableClosure, "Closure"},
	{mrdVariableSession, "Session"},
};

struct MacroDebuggerValueTypeDescriptor {
	const char *name;
	int type;
};

static const MacroDebuggerValueTypeDescriptor kMacroDebuggerValueTypes[] = {
	{"int", TYPE_INT},
	{"real", TYPE_REAL},
	{"str", TYPE_STR},
	{"char", TYPE_CHAR},
	{"hash", TYPE_HASH},
	{"int[]", TYPE_INT_ARRAY},
	{"real[]", TYPE_REAL_ARRAY},
	{"str[]", TYPE_STR_ARRAY},
	{"char[]", TYPE_CHAR_ARRAY},
	{"hash[]", TYPE_HASH_ARRAY},
};

bool sameMacroDebuggerValuePath(const MRMacroDebugVariableSnapshot &left, const MRMacroDebugVariableSnapshot &right) noexcept {
	if (left.scope != right.scope || left.name != right.name || left.path.size() != right.path.size()) return false;
	for (std::size_t index = 0; index < left.path.size(); ++index) {
		if (left.path[index].kind != right.path[index].kind) return false;
		if (left.path[index].kind == mrdValueHashKey) {
			if (left.path[index].key != right.path[index].key) return false;
		} else if (left.path[index].index != right.path[index].index)
			return false;
	}
	return true;
}

bool parseMacroDebuggerMutation(const MRMacroDebugVariableSnapshot &variable, const std::string &text, MRMacroDebugValueMutation &mutation, std::string &errorMessage) {
	mutation = MRMacroDebugValueMutation();
	mutation.target = variable;
	errorMessage.clear();
	if (variable.type != TYPE_HASH && !macroDebuggerVariableIsArray(variable.type) && text.rfind("::", 0) == 0) {
		mutation.action = mrdValueSetScalar;
		mutation.valueText = text.substr(1);
		return true;
	}
	if (!variable.path.empty() && text == ":delete") {
		mutation.action = mrdValueEraseElement;
		return true;
	}
	if (!variable.path.empty() && text.rfind(":rename=", 0) == 0) {
		mutation.action = mrdValueRenameHashKey;
		mutation.key = text.substr(8);
		return true;
	}
	if (variable.type == TYPE_HASH) {
		std::size_t markerPosition = std::string::npos;
		const MacroDebuggerValueTypeDescriptor *valueType = nullptr;

		for (const MacroDebuggerValueTypeDescriptor &descriptor : kMacroDebuggerValueTypes) {
			const std::string marker = std::string(":") + descriptor.name + "=";
			const std::size_t position = text.find(marker);

			if (position == std::string::npos) continue;
			if (markerPosition == std::string::npos || position < markerPosition) {
				markerPosition = position;
				valueType = &descriptor;
			}
		}
		mutation.action = mrdValueAddHashEntry;
		if (valueType != nullptr) {
			const std::string marker = std::string(":") + valueType->name + "=";

			mutation.key = text.substr(0, markerPosition);
			mutation.valueType = valueType->type;
			mutation.valueText = text.substr(markerPosition + marker.size());
		} else {
			const std::size_t equals = text.find('=');

			if (equals == std::string::npos) {
				errorMessage = "Hash insertion expects key=value or key:type=value.";
				return false;
			}
			mutation.key = text.substr(0, equals);
			mutation.valueType = TYPE_STR;
			mutation.valueText = text.substr(equals + 1);
		}
		return true;
	}
	if (macroDebuggerVariableIsArray(variable.type)) {
		mutation.action = mrdValueAppendArrayElement;
		mutation.valueText = text;
		return true;
	}
	mutation.action = mrdValueSetScalar;
	mutation.valueText = text;
	return true;
}

std::string macroDebuggerWatchesText(const std::vector<MRMacroDebugWatchSnapshot> &snapshots, std::vector<std::pair<std::size_t, std::size_t>> &activeRanges, std::vector<std::pair<std::size_t, std::size_t>> &inactiveRanges,
	                                 std::vector<std::pair<std::size_t, std::size_t>> &errorRanges) {
	std::string text("Watches\n");

	activeRanges.clear();
	inactiveRanges.clear();
	errorRanges.clear();
	if (snapshots.empty()) {
		text += "\n(none)\n";
		return text;
	}
	text += "\n";
	for (const MRMacroDebugWatchSnapshot &snapshot : snapshots) {
		const std::size_t start = text.size();

		text += snapshot.expression;
		text += " ";
		if (!snapshot.enabled)
			text += "[disabled]";
		else if (!snapshot.errorText.empty())
			text += "[error] = " + snapshot.errorText;
		else
			text += std::string("[") + macroDebuggerVariableTypeText(snapshot.type) + "] = " + snapshot.valueText;
		if (!snapshot.enabled) inactiveRanges.push_back(std::pair<std::size_t, std::size_t>(start, text.size()));
		else if (!snapshot.errorText.empty())
			errorRanges.push_back(std::pair<std::size_t, std::size_t>(start, text.size()));
		else activeRanges.push_back(std::pair<std::size_t, std::size_t>(start, text.size()));
		text += "\n";
	}
	return text;
}

enum MacroDebuggerFunctionKeyAction {
	mdfkaContinue = 0,
	mdfkaRunHere,
	mdfkaStop,
	mdfkaStepInto,
	mdfkaStepOver,
	mdfkaStepOut,
	mdfkaBreakpoint,
	mdfkaBreakpointEnable,
	mdfkaBreakpointAllToggle,
	mdfkaBreakpointClearAll,
	mdfkaAddWatch,
	mdfkaEraseWatch,
	mdfkaEvaluate
};

struct MacroDebuggerFunctionKeyDescriptor {
	ushort keyCode;
	ushort controlKeyState;
	MacroDebuggerFunctionKeyAction action;
	bool requiresLiveSession;
};

static const MacroDebuggerFunctionKeyDescriptor kMacroDebuggerFunctionKeys[] = {
	{kbF4, 0, mdfkaEvaluate, true},
	{kbF5, 0, mdfkaContinue, true},
	{kbF6, 0, mdfkaRunHere, false},
	{kbF8, 0, mdfkaStop, false},
	{kbF10, 0, mdfkaStepInto, true},
	{kbF11, 0, mdfkaStepOver, true},
	{kbShiftF11, 0, mdfkaStepOut, true},
	{kbF9, 0, mdfkaBreakpoint, false},
	{kbF9, kbShift, mdfkaBreakpointEnable, false},
	{kbF9, static_cast<ushort>(kbAltShift | kbShift), mdfkaBreakpointAllToggle, false},
	{kbF9, static_cast<ushort>(kbCtrlShift | kbShift), mdfkaBreakpointClearAll, false},
	{kbF7, 0, mdfkaAddWatch, false},
	{kbF7, kbShift, mdfkaEraseWatch, false},
};

} // namespace

bool MRBentoBox::macroDebuggerTargetsSourceIdentity(const std::string &sourcePath, const std::string &macroName) const noexcept {
	if (!macroDebuggerActive || sourcePath.empty() || macroName.empty()) return false;
	return macroDebuggerSourceIdentity == mrvmMakeMacroSourceIdentity(normalizeConfiguredPathInput(sourcePath), macroName);
}

const std::string &MRBentoBox::macroDebuggerSourceIdentityValue() const noexcept {
	return macroDebuggerSourceIdentity;
}

bool MRBentoBox::macroDebuggerObservesSourceIdentity(const std::string &sourcePath, const std::string &macroName) const noexcept {
	return macroDebuggerSessionId == 0 && !macroDebuggerExecutionRunning && macroDebuggerTargetsSourceIdentity(sourcePath, macroName);
}

bool MRBentoBox::acceptScheduledMacroDebuggerSession(MRMacroExecutionSessionId sessionId, const MRMacroDebugRunResult &debugResult) {
	if (sessionId == 0 || !debugResult.paused || !macroDebuggerObservesSourceIdentity(debugResult.sourcePath, debugResult.macroKey)) return false;
	macroDebuggerSessionId = sessionId;
	macroDebuggerExecutionRoute = mrvmDebugSessionRoute(sessionId);
	macroDebuggerExecutionRunning = debugResult.stopReason == mrdStopBudget;
	cancelMacroDebuggerValueInput();
	refreshMacroDebuggerBreakpointRanges();
	refreshMacroDebuggerWatches();
	if (debugResult.stopReason == mrdStopBudget) {
		macroDebuggerStatus = "running #" + std::to_string(sessionId);
		writeMacroDebuggerNotice("State: running\nStop: running");
		if (macroDebuggerRouteUsesWorker(macroDebuggerExecutionRoute) && !scheduleMacroDebuggerWorkerAction(mrdWorkerContinue)) {
			static_cast<void>(mrvmCloseDebugSession(sessionId));
			macroDebuggerSessionId = 0;
			macroDebuggerExecutionRoute = MRMacroExecutionRoute::Debug;
			macroDebuggerExecutionRunning = false;
			writeMacroDebuggerNotice("State: failed/no live session\nUnable to schedule debugger worker.");
			return false;
		}
	} else {
		refreshMacroDebuggerVariables(debugResult.variables);
		refreshMacroDebuggerRunMarkers(debugResult);
		writeMacroDebuggerStatus(debugResult, std::string());
	}
	bentoProjectionDirty |= bpdContent | bpdChrome;
	flushBentoProjection();
	return true;
}

bool MRBentoBox::acceptMacroDebuggerWorkerResult(MRMacroExecutionSessionId sessionId, std::uint64_t, const MRMacroDebugRunResult &debugResult, const std::string &errorMessage) {
	if (!macroDebuggerActive || sessionId == 0 || sessionId != macroDebuggerSessionId) return false;
	macroDebuggerExecutionRunning = false;
	cancelMacroDebuggerValueInput();
	if (debugResult.stopReason == mrdStopBudget && !debugResult.hadError && !debugResult.cancelled) {
		macroDebuggerExecutionRunning = true;
		macroDebuggerStatus = "running #" + std::to_string(sessionId);
		if (!scheduleMacroDebuggerWorkerAction(mrdWorkerContinue)) {
			macroDebuggerExecutionRunning = false;
			macroDebuggerStatus = "error/no-live";
			writeMacroDebuggerNotice("State: failed/no live session\nUnable to continue debugger worker.");
			static_cast<void>(mrvmCloseDebugSession(sessionId));
			macroDebuggerSessionId = 0;
			macroDebuggerExecutionRoute = MRMacroExecutionRoute::Debug;
			return false;
		}
		return true;
	}
	refreshMacroDebuggerVariables(debugResult.variables);
	refreshMacroDebuggerRunMarkers(debugResult);
	writeMacroDebuggerStatus(debugResult, errorMessage);
	refreshMacroDebuggerBreakpointRanges();
	refreshMacroDebuggerWatches();
	if (!debugResult.paused || debugResult.hadError || debugResult.cancelled) {
		macroDebuggerSessionId = 0;
		macroDebuggerExecutionRoute = MRMacroExecutionRoute::Debug;
	}
	bentoProjectionDirty |= bpdContent | bpdChrome;
	flushBentoProjection();
	return true;
}

void MRBentoBox::writeMacroDebuggerStatus(const MRMacroDebugRunResult &debugResult, const std::string &errorMessage) {
	MREditWindow *outputWindow = debuggerOutputPane();
	const std::string displayName = macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName;
	const MRMacroExecutionSessionId displayedSessionId = debugResult.paused || debugResult.hadError ? macroDebuggerSessionId : 0;

	if (outputWindow == nullptr) return;
	static_cast<void>(outputWindow->replaceTextBuffer(mrMacroDebuggerStatusText(displayName, macroDebuggerSourcePath, displayedSessionId, debugResult, errorMessage).c_str(), "Debugger Output"));
	outputWindow->setReadOnly(true);
	outputWindow->setFileChanged(false);
}

void MRBentoBox::writeMacroDebuggerNotice(const std::string &message) {
	MREditWindow *outputWindow = debuggerOutputPane();
	const std::string displayName = macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName;

	if (outputWindow == nullptr) return;
	static_cast<void>(outputWindow->replaceTextBuffer(mrMacroDebuggerNoticeText(displayName, macroDebuggerSourcePath, macroDebuggerSessionId, message).c_str(), "Debugger Output"));
	outputWindow->setReadOnly(true);
	outputWindow->setFileChanged(false);
}

bool MRBentoBox::toggleMacroDebuggerBreakpointAtCursor() {
	MRFileEditor *sourceEditor = getEditor();
	MREditWindow *outputWindow = debuggerOutputPane();
	std::vector<MRMacroDebuggerBreakpoint> breakpoints;
	std::ostringstream out;
	std::ostringstream log;
	std::string errorMessage;
	bool haveBreakpoints = false;
	bool enabled = false;
	bool toggled;
	int line;
	int activeBreakpointCount = 0;

	if (!macroDebuggerActive || macroDebuggerMacroKey.empty() || sourceEditor == nullptr) {
		log << "MACRODBG key stage=bento-toggle skipped active=" << (macroDebuggerActive ? "yes" : "no") << " macroKey=" << (macroDebuggerMacroKey.empty() ? "empty" : "set") << " sourceEditor=" << (sourceEditor != nullptr ? "yes" : "no");
		mrLogMessage(log.str());
		return false;
	}
	line = static_cast<int>(sourceEditor->lineIndexOfOffset(sourceEditor->cursorOffset()) + 1);
	const std::string breakpointMacroKey = macroDebuggerProjectedMacroKey.empty() ? macroDebuggerMacroKey : macroDebuggerProjectedMacroKey;

	toggled = mrvmToggleDebugLineBreakpoint(breakpointMacroKey, line, &enabled, &errorMessage);
	log << "MACRODBG key stage=bento-toggle macro=" << breakpointMacroKey << " line=" << line << " toggled=" << (toggled ? "yes" : "no") << " enabled=" << (enabled ? "yes" : "no");
	if (!errorMessage.empty()) log << " error=" << errorMessage;
	mrLogMessage(log.str());
	out << "Macro Debugger\n";
	out << "Macro: " << (macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName) << "\n";
	out << "Source line: " << line << "\n";
	if (toggled)
		out << "Breakpoint: " << (enabled ? "set" : "cleared") << "\n";
	else
		out << "Breakpoint: " << (errorMessage.empty() ? "not changed" : errorMessage) << "\n";
	if (toggled) mrMarkWorkspaceAutosaveDirty("debugger breakpoint", this);
	refreshMacroDebuggerBreakpointRanges();
	haveBreakpoints = mrvmDebugLineBreakpointsForMacro(breakpointMacroKey, breakpoints);
	out << "\nBreakpoints:\n";
	if (haveBreakpoints) {
		for (const MRMacroDebuggerBreakpoint &breakpoint : breakpoints) {
			if (breakpoint.enabled) ++activeBreakpointCount;
			out << "  " << breakpoint.macroKey << " #" << breakpoint.line << " bytecode " << breakpoint.bytecodeOffset;
			if (!breakpoint.enabled) out << " [disabled]";
			out << "\n";
		}
	}
	if (activeBreakpointCount == 0) out << "  none\n";
	if (outputWindow != nullptr) {
		static_cast<void>(outputWindow->replaceTextBuffer(out.str().c_str(), "Debugger Output"));
		outputWindow->setReadOnly(true);
		outputWindow->setFileChanged(false);
	}
	return toggled;
}

bool MRBentoBox::toggleMacroDebuggerBreakpointEnabledAtCursor() {
	MRFileEditor *sourceEditor = getEditor();
	MREditWindow *outputWindow = debuggerOutputPane();
	std::ostringstream output;
	std::string errorMessage;
	bool enabled = false;
	int line;
	const std::string macroKey = macroDebuggerProjectedMacroKey.empty() ? macroDebuggerMacroKey : macroDebuggerProjectedMacroKey;

	if (!macroDebuggerActive || macroKey.empty() || sourceEditor == nullptr) return false;
	line = static_cast<int>(sourceEditor->lineIndexOfOffset(sourceEditor->cursorOffset()) + 1);
	if (!mrvmToggleDebugLineBreakpointEnabled(macroKey, line, &enabled, &errorMessage)) return false;
	mrMarkWorkspaceAutosaveDirty("debugger breakpoint enabled", this);
	refreshMacroDebuggerBreakpointRanges();
	output << "Macro Debugger\nMacro: " << (macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName) << "\nSource line: " << line << "\nBreakpoint: " << (enabled ? "enabled" : "disabled") << "\n";
	if (outputWindow != nullptr) {
		static_cast<void>(outputWindow->replaceTextBuffer(output.str().c_str(), "Debugger Output"));
		outputWindow->setReadOnly(true);
		outputWindow->setFileChanged(false);
	}
	return true;
}

bool MRBentoBox::toggleMacroDebuggerBreakpointsEnabled() {
	MREditWindow *outputWindow = debuggerOutputPane();
	std::ostringstream output;
	std::string errorMessage;
	bool enabled = false;
	const std::string macroKey = macroDebuggerProjectedMacroKey.empty() ? macroDebuggerMacroKey : macroDebuggerProjectedMacroKey;

	if (!macroDebuggerActive || macroKey.empty()) return false;
	if (!mrvmToggleDebugLineBreakpointsEnabledForMacroFile(macroKey, &enabled, &errorMessage)) return false;
	mrMarkWorkspaceAutosaveDirty("debugger breakpoint toggle all", this);
	refreshMacroDebuggerBreakpointRanges();
	output << "Macro Debugger\nMacro: " << (macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName) << "\nBreakpoints: all " << (enabled ? "enabled" : "disabled") << "\n";
	if (outputWindow != nullptr) {
		static_cast<void>(outputWindow->replaceTextBuffer(output.str().c_str(), "Debugger Output"));
		outputWindow->setReadOnly(true);
		outputWindow->setFileChanged(false);
	}
	return true;
}

bool MRBentoBox::eraseMacroDebuggerBreakpoints() {
	MREditWindow *outputWindow = debuggerOutputPane();
	std::ostringstream output;
	std::string errorMessage;
	const std::string macroKey = macroDebuggerProjectedMacroKey.empty() ? macroDebuggerMacroKey : macroDebuggerProjectedMacroKey;

	if (!macroDebuggerActive || macroKey.empty()) return false;
	if (!mrvmEraseDebugLineBreakpointsForMacroFile(macroKey, &errorMessage)) return false;
	mrMarkWorkspaceAutosaveDirty("debugger breakpoint clear all", this);
	refreshMacroDebuggerBreakpointRanges();
	output << "Macro Debugger\nMacro: " << (macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName) << "\nBreakpoints: all cleared\n";
	if (outputWindow != nullptr) {
		static_cast<void>(outputWindow->replaceTextBuffer(output.str().c_str(), "Debugger Output"));
		outputWindow->setReadOnly(true);
		outputWindow->setFileChanged(false);
	}
	return true;
}

void MRBentoBox::refreshMacroDebuggerVariables(const std::vector<MRMacroDebugVariableSnapshot> &variables) {
	MREditWindow *variablesWindow = variablesPane();
	const std::vector<MRMacroDebugVariableSnapshot> previousVariables = macroDebuggerVariables;
	const std::vector<MRMacroDebugVariableSnapshot> &projectedVariables = &variables == &macroDebuggerVariables ? previousVariables : variables;
	std::vector<std::pair<std::size_t, std::size_t>> changedRanges;
	std::string text("Variables\n");
	bool wroteGroup = false;

	macroDebuggerVariables.clear();
	macroDebuggerVariableRows.clear();
	macroDebuggerVariables.reserve(projectedVariables.size());
	macroDebuggerVariableRows.reserve(projectedVariables.size());
	if (projectedVariables.empty())
		text += "\n(none)\n";
	else {
		text += "Edit: click value | hash: key:type=value | array: append value | :delete | :rename=newKey\n\n";
		for (const MacroDebuggerVariableGroup &group : kMacroDebuggerVariableGroups) {
			bool groupHasVariables = false;

			for (const MRMacroDebugVariableSnapshot &variable : projectedVariables)
				if (variable.scope == group.scope && variable.depth == 0) {
					groupHasVariables = true;
					break;
				}
			if (!groupHasVariables) continue;
			if (wroteGroup) text += "\n";
			text += group.title;
			text += "\n";
			for (const MRMacroDebugVariableSnapshot &variable : projectedVariables) {
				bool valueChanged = false;

				if (variable.scope != group.scope) continue;
				for (const MRMacroDebugVariableSnapshot &previous : previousVariables)
					if (previous.type == variable.type && sameMacroDebuggerValuePath(previous, variable)) {
						valueChanged = previous.valueText != variable.valueText;
						break;
					}
				const std::size_t rowStart = text.size();
				text += "  ";
				for (int depth = 0; depth < variable.depth; ++depth)
					text += "  ";
				if (variable.depth > 0) text += "- ";
				text += variable.displayName.empty() ? variable.name : variable.displayName;
				text += " [";
				text += macroDebuggerVariableTypeText(variable.type);
				text += "] = ";
				text += variable.valueText;
				macroDebuggerVariableRows.push_back(std::pair<std::size_t, std::size_t>(rowStart, text.size()));
				macroDebuggerVariables.push_back(variable);
				if (valueChanged) changedRanges.push_back(std::pair<std::size_t, std::size_t>(rowStart, text.size()));
				text += "\n";
			}
			wroteGroup = true;
		}
	}
	if (variablesWindow == nullptr) return;
	static_cast<void>(variablesWindow->replaceTextBuffer(text.c_str(), "Variables"));
	variablesWindow->setReadOnly(true);
	variablesWindow->setFileChanged(false);
	if (variablesWindow->getEditor() != nullptr) {
		if (changedRanges.empty()) variablesWindow->getEditor()->clearDebuggerVariableChangedRanges();
		else
			variablesWindow->getEditor()->setDebuggerVariableChangedRanges(changedRanges);
	}
}

bool MRBentoBox::showMacroDebuggerValueInputAtCursor() {
	MRPaneEditWindow *variablesWindow = dynamic_cast<MRPaneEditWindow *>(variablesPane());
	MRFileEditor *variablesEditor = variablesWindow != nullptr ? variablesWindow->getEditor() : nullptr;
	const std::size_t cursor = variablesEditor != nullptr ? variablesEditor->cursorOffset() : 0;

	if (macroDebuggerValueInput != nullptr || variablesEditor == nullptr || macroDebuggerSessionId == 0) return false;
	for (std::size_t index = 0; index < macroDebuggerVariableRows.size() && index < macroDebuggerVariables.size(); ++index) {
		const std::pair<std::size_t, std::size_t> row = macroDebuggerVariableRows[index];
		const MRMacroDebugVariableSnapshot &variable = macroDebuggerVariables[index];

		if (cursor < row.first || cursor >= row.second) continue;
		const std::string text = variablesEditor->snapshotText();
		const std::size_t valueStart = text.find("= ", row.first);

		if (valueStart == std::string::npos || valueStart + 2 > row.second) return false;
		variablesEditor->setCursorOffset(valueStart + 2);
		const TRect viewport = variablesEditor->visibleTextViewportBounds();
		const int left = viewport.a.x + variablesEditor->currentViewColumn() - 1;
		const int top = viewport.a.y + variablesEditor->currentViewRow() - 1;

		if (left >= viewport.b.x || top < viewport.a.y || top >= viewport.b.y) return false;
		macroDebuggerValueInput = new MRMacroDebuggerValueInput(TRect(left, top, viewport.b.x, top + 1), this);
		macroDebuggerValueInputPane = variablesWindow;
		std::array<char, 255> value{};

		if (variable.type != TYPE_HASH && !macroDebuggerVariableIsArray(variable.type)) std::strncpy(value.data(), variable.valueText.c_str(), value.size() - 1);
		macroDebuggerValueInput->setData(value.data());
		variablesWindow->insert(macroDebuggerValueInput);
		macroDebuggerValueInput->selectAll(True);
		macroDebuggerValueInput->select();
		return true;
	}
	return false;
}

bool MRBentoBox::macroDebuggerValueInputContains(const TPoint &point) const noexcept {
	return macroDebuggerValueInput != nullptr && macroDebuggerValueInput->mouseInView(point);
}

void MRBentoBox::commitMacroDebuggerValueInput() {
	std::array<char, 255> value{};
	std::vector<MRMacroDebugVariableSnapshot> updatedVariables;
	std::string errorMessage;
	std::string parseError;
	MRFileEditor *variablesEditor = macroDebuggerValueInputPane != nullptr ? macroDebuggerValueInputPane->getEditor() : nullptr;
	const std::size_t cursor = variablesEditor != nullptr ? variablesEditor->cursorOffset() : 0;
	MRMacroDebugVariableSnapshot variable;
	MRMacroDebugValueMutation mutation;
	bool found = false;

	if (macroDebuggerValueInput == nullptr) return;
	for (std::size_t index = 0; index < macroDebuggerVariableRows.size() && index < macroDebuggerVariables.size(); ++index)
		if (cursor >= macroDebuggerVariableRows[index].first && cursor < macroDebuggerVariableRows[index].second) {
			variable = macroDebuggerVariables[index];
			found = true;
			break;
		}
	if (!found) {
		macroDebuggerValueInput->setError(true);
		return;
	}
	macroDebuggerValueInput->getData(value.data());
	if (!parseMacroDebuggerMutation(variable, value.data(), mutation, parseError)) {
		mrLogMessage("MACRODBG mutate rejected name=" + variable.name + " error=" + parseError);
		macroDebuggerValueInput->setError(true);
		return;
	}
	if (!mrvmMutateDebugValue(macroDebuggerSessionId, mutation, updatedVariables, &errorMessage)) {
		mrLogMessage("MACRODBG mutate rejected name=" + variable.name + " error=" + errorMessage);
		macroDebuggerValueInput->setError(true);
		return;
	}
	mrLogMessage("MACRODBG mutate applied name=" + variable.name);
	cancelMacroDebuggerValueInput();
	refreshMacroDebuggerVariables(updatedVariables);
	refreshMacroDebuggerWatches();
}

void MRBentoBox::cancelMacroDebuggerValueInput() noexcept {
	if (macroDebuggerValueInput != nullptr && macroDebuggerValueInputPane != nullptr) {
		macroDebuggerValueInputPane->remove(macroDebuggerValueInput);
		TObject::destroy(macroDebuggerValueInput);
	}
	macroDebuggerValueInput = nullptr;
	macroDebuggerValueInputPane = nullptr;
}

void MRBentoBox::refreshMacroDebuggerWatches() {
	MREditWindow *watchesWindow = watchesPane();
	std::vector<MRMacroDebugWatchSnapshot> snapshots;
	std::vector<std::pair<std::size_t, std::size_t>> activeRanges;
	std::vector<std::pair<std::size_t, std::size_t>> inactiveRanges;
	std::vector<std::pair<std::size_t, std::size_t>> errorRanges;
	MRFileEditor *watchesEditor;

	if (watchesWindow == nullptr) return;
	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) snapshots.clear();
	else static_cast<void>(mrvmDebugWatchSnapshots(macroDebuggerSessionId, macroDebuggerMacroKey, snapshots));
	const std::string text = macroDebuggerWatchesText(snapshots, activeRanges, inactiveRanges, errorRanges);
	static_cast<void>(watchesWindow->replaceTextBuffer(text.c_str(), "Watches"));
	watchesWindow->setReadOnly(true);
	watchesWindow->setFileChanged(false);
	watchesEditor = watchesWindow->getEditor();
	if (watchesEditor == nullptr || (activeRanges.empty() && inactiveRanges.empty() && errorRanges.empty())) {
		if (watchesEditor != nullptr) watchesEditor->clearDebuggerWatchpointRanges();
		return;
	}
	watchesEditor->setDebuggerWatchpointRanges(activeRanges, inactiveRanges, errorRanges);
}

bool MRBentoBox::addMacroDebuggerWatch() {
	char expression[256] = {};
	std::string errorMessage;

	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) return false;
	if (mr::dialogs::execTextInputDialog("MACRO DEBUGGER", "Watch expression", expression, sizeof(expression) - 1) == cmCancel) return true;
	if (!mrvmWriteDebugWatch(macroDebuggerMacroKey, expression, true, &errorMessage)) return false;
	mrMarkWorkspaceAutosaveDirty("debugger watch add", this);
	refreshMacroDebuggerWatches();
	return true;
}

bool MRBentoBox::eraseMacroDebuggerWatch() {
	char expression[256] = {};
	std::string errorMessage;

	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) return false;
	if (mr::dialogs::execTextInputDialog("MACRO DEBUGGER", "Remove watch", expression, sizeof(expression) - 1) == cmCancel) return true;
	if (!mrvmEraseDebugWatch(macroDebuggerMacroKey, expression, &errorMessage)) return false;
	mrMarkWorkspaceAutosaveDirty("debugger watch erase", this);
	refreshMacroDebuggerWatches();
	return true;
}

bool MRBentoBox::evaluateMacroDebuggerExpression() {
	char expression[256] = {};
	MRMacroDebugWatchSnapshot snapshot;
	std::ostringstream output;
	std::string errorMessage;

	if (!macroDebuggerActive || macroDebuggerSessionId == 0) return false;
	if (macroDebuggerExecutionRunning) {
		writeMacroDebuggerNotice("Evaluate requires a paused session.");
		return false;
	}
	if (mr::dialogs::execTextInputDialog("MACRO DEBUGGER", "Evaluate expression", expression, sizeof(expression) - 1) == cmCancel) return true;
	if (!mrvmEvaluateDebugExpression(macroDebuggerSessionId, expression, snapshot, &errorMessage)) {
		writeMacroDebuggerNotice("Evaluate:\n" + (errorMessage.empty() ? "Debug session is not paused." : errorMessage));
		return false;
	}
	output << "Evaluate:\n" << snapshot.expression << " = ";
	if (!snapshot.errorText.empty())
		output << "[error] " << snapshot.errorText;
	else
		output << "[" << macroDebuggerVariableTypeText(snapshot.type) << "] " << snapshot.valueText;
	writeMacroDebuggerNotice(output.str());
	return snapshot.errorText.empty();
}

bool MRBentoBox::scheduleMacroDebuggerWorkerAction(MRMacroDebugWorkerAction action) {
	MRMacroExecutionRoute route;
	MREditWindow *targetWindow;
	std::size_t baseVersion;
	std::uint64_t taskId;
	int targetBufferId;
	const MRMacroExecutionSessionId sessionId = macroDebuggerSessionId;
	const std::string macroKey = macroDebuggerMacroKey;
	const std::string displayName = macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName;

	if (sessionId == 0 || macroKey.empty() || !mrvmDebugSessionWorkerTaskContext(sessionId, route, targetBufferId, baseVersion) || !macroDebuggerRouteUsesWorker(route)) return false;
	macroDebuggerExecutionRoute = route;
	taskId = mr::coprocessor::globalCoprocessor().submit(
	    mr::coprocessor::Lane::Macro, mr::coprocessor::TaskKind::MacroJob, targetBufferId > 0 ? static_cast<std::size_t>(targetBufferId) : 0, baseVersion,
	    mr::coprocessor::ExecutionOwnerKind::MacroSession, static_cast<std::size_t>(sessionId), std::string("macro-debug: ") + displayName,
	    [sessionId, macroKey, displayName, action](const mr::coprocessor::TaskInfo &info) {
		    mr::coprocessor::Result result;
		    MRMacroDebugWorkerResult workerResult;

		    result.task = info;
		    static_cast<void>(mrvmAssignDebugSessionWorkerTask(sessionId, info.id));
		    workerResult = mrvmRunDebugSessionWorkerAction(sessionId, macroKey, action, 8192, info.cancelFlag);
		    if (!workerResult.accepted) {
			    workerResult.debugResult.stopReason = mrdStopError;
			    workerResult.debugResult.hadError = true;
			    workerResult.debugResult.paused = false;
			    if (workerResult.errorMessage.empty()) workerResult.errorMessage = "Debugger worker rejected the execution request.";
			    workerResult.debugResult.logLines.push_back("VM Error: " + workerResult.errorMessage);
		    }
		    result.status = mr::coprocessor::TaskStatus::Completed;
		    if (workerResult.debugResult.stopReason == mrdStopBudget || workerResult.debugResult.paused || workerResult.debugResult.cancelled || !workerResult.accepted) {
			    result.payload = std::make_shared<mr::coprocessor::MacroDebugWorkerPausedPayload>(sessionId, std::move(workerResult.debugResult), std::move(workerResult.errorMessage));
			    return result;
		    }
		    if (workerResult.hasStagedResult) {
			    std::shared_ptr<mr::coprocessor::MacroJobStagedPayload> payload = std::make_shared<mr::coprocessor::MacroJobStagedPayload>();
			    MRMacroStagedJobResult &staged = workerResult.stagedResult;

			    payload->displayName = displayName;
			    payload->logLines = std::move(staged.logLines);
			    payload->hadError = staged.hadError;
			    payload->conflictSnapshot = std::move(staged.conflictSnapshot);
			    payload->transaction = std::move(staged.transaction);
			    payload->cursorOffset = staged.cursorOffset;
			    payload->selectionStart = staged.selectionStart;
			    payload->selectionEnd = staged.selectionEnd;
			    payload->blockMode = staged.blockMode;
			    payload->blockMarkingOn = staged.blockMarkingOn;
			    payload->blockAnchor = staged.blockAnchor;
			    payload->blockEnd = staged.blockEnd;
			    payload->globalOrder = std::move(staged.globalOrder);
			    payload->globalInts = std::move(staged.globalInts);
			    payload->globalStrings = std::move(staged.globalStrings);
			    payload->deferredUiCommands = std::move(staged.deferredUiCommands);
			    payload->lastSearchValid = staged.lastSearchValid;
			    payload->lastSearchStart = staged.lastSearchStart;
			    payload->lastSearchEnd = staged.lastSearchEnd;
			    payload->lastSearchCursor = staged.lastSearchCursor;
			    payload->ignoreCase = staged.ignoreCase;
			    payload->tabExpand = staged.tabExpand;
			    payload->markStack = std::move(staged.markStack);
			    payload->insertMode = staged.insertMode;
			    payload->indentLevel = staged.indentLevel;
			    payload->fileName = std::move(staged.fileName);
			    payload->fileChanged = staged.fileChanged;
			    payload->debugSessionId = sessionId;
			    payload->debugResult = std::move(workerResult.debugResult);
			    result.payload = std::move(payload);
			    return result;
		    }
		    {
			    std::shared_ptr<mr::coprocessor::MacroJobFinishedPayload> payload = std::make_shared<mr::coprocessor::MacroJobFinishedPayload>();

			    payload->displayName = displayName;
			    payload->logLines = workerResult.debugResult.logLines;
			    payload->hadError = workerResult.debugResult.hadError;
			    payload->debugSessionId = sessionId;
			    payload->debugResult = std::move(workerResult.debugResult);
			    result.payload = std::move(payload);
		    }
		    return result;
	    });
	if (taskId == 0) return false;
	static_cast<void>(mrvmAssignDebugSessionWorkerTask(sessionId, taskId));
	targetWindow = targetBufferId > 0 ? findEditWindowByBufferId(targetBufferId) : nullptr;
	if (targetWindow != nullptr) targetWindow->trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::MacroJob, displayName);
	return true;
}

bool MRBentoBox::continueMacroDebuggerSession() {
	MRMacroDebugRunResult debugResult;
	std::ostringstream log;
	std::string errorMessage;
	const std::string displayName = macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName;

	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) return false;
	cancelMacroDebuggerValueInput();
	if (macroDebuggerSessionId == 0) {
		macroDebuggerStatus = "no live session";
		writeMacroDebuggerNotice("State: no live session");
		return false;
	}
	if (macroDebuggerExecutionRunning) {
		if (!mrvmRequestDebugPause(macroDebuggerSessionId, &errorMessage)) return false;
		macroDebuggerStatus = "pause requested #" + std::to_string(macroDebuggerSessionId);
		writeMacroDebuggerNotice("State: pause requested");
		return true;
	}
	if (macroDebuggerRouteUsesWorker(macroDebuggerExecutionRoute)) {
		if (!scheduleMacroDebuggerWorkerAction(mrdWorkerContinue)) return false;
	} else if (!mrvmScheduleDebugMacroContinue(macroDebuggerSessionId, macroDebuggerMacroKey, &errorMessage))
		return false;
	macroDebuggerExecutionRunning = true;
	macroDebuggerStatus = "running #" + std::to_string(macroDebuggerSessionId);
	log << "MACRODBG key stage=bento-continue macro=" << displayName << " session=" << macroDebuggerSessionId << " scheduled=yes";
	if (!errorMessage.empty()) log << " error=" << errorMessage;
	mrLogMessage(log.str());
	writeMacroDebuggerNotice("State: running\nStop: running");
	return true;
}

void MRBentoBox::pumpMacroDebuggerSession() {
	MRMacroDebugRunResult debugResult;
	std::string errorMessage;

	if (!macroDebuggerExecutionRunning || macroDebuggerSessionId == 0 || macroDebuggerRouteUsesWorker(macroDebuggerExecutionRoute)) return;
	if (!mrvmPumpDebugSession(macroDebuggerSessionId, macroDebuggerMacroKey, debugResult, &errorMessage)) return;
	if (debugResult.stopReason == mrdStopBudget) return;
	macroDebuggerExecutionRunning = false;
	refreshMacroDebuggerVariables(debugResult.variables);
	refreshMacroDebuggerRunMarkers(debugResult);
	writeMacroDebuggerStatus(debugResult, errorMessage);
	refreshMacroDebuggerBreakpointRanges();
	refreshMacroDebuggerWatches();
	if (!debugResult.paused && !debugResult.hadError) macroDebuggerSessionId = 0;
	bentoProjectionDirty |= bpdContent | bpdChrome;
	flushBentoProjection();
}

bool MRBentoBox::stepMacroDebuggerSession(MRMacroDebugStepMode mode) {
	MRMacroDebugRunResult debugResult;
	std::ostringstream log;
	std::string errorMessage;
	const std::string displayName = macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName;

	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) return false;
	if (macroDebuggerExecutionRunning) return false;
	cancelMacroDebuggerValueInput();
	if (macroDebuggerSessionId == 0) {
		macroDebuggerStatus = "no live session";
		writeMacroDebuggerNotice("State: no live session");
		return false;
	}
	if (macroDebuggerRouteUsesWorker(macroDebuggerExecutionRoute)) {
		const MRMacroDebugWorkerAction action = mode == mrdStepOver ? mrdWorkerStepOver : (mode == mrdStepOut ? mrdWorkerStepOut : mrdWorkerStepInto);

		if (!scheduleMacroDebuggerWorkerAction(action)) return false;
		macroDebuggerExecutionRunning = true;
		macroDebuggerStatus = "running #" + std::to_string(macroDebuggerSessionId);
		writeMacroDebuggerNotice("State: running\nStop: stepping");
		return true;
	}
	if (mode == mrdStepOver)
		debugResult = mrvmStepOverDebugMacroByName(macroDebuggerSessionId, macroDebuggerMacroKey, &errorMessage);
	else if (mode == mrdStepOut)
		debugResult = mrvmStepOutDebugMacroByName(macroDebuggerSessionId, macroDebuggerMacroKey, &errorMessage);
	else
		debugResult = mrvmStepDebugMacroByName(macroDebuggerSessionId, macroDebuggerMacroKey, &errorMessage);
	log << "MACRODBG key stage=bento-step-" << (mode == mrdStepOver ? "over" : (mode == mrdStepOut ? "out" : "into")) << " macro=" << displayName << " session=" << macroDebuggerSessionId << " paused=" << (debugResult.paused ? "yes" : "no") << " stop=" << mrMacroDebuggerStopReasonText(debugResult.stopReason);
	if (!errorMessage.empty()) log << " error=" << errorMessage;
	mrLogMessage(log.str());
	refreshMacroDebuggerVariables(debugResult.variables);
	refreshMacroDebuggerRunMarkers(debugResult);
	writeMacroDebuggerStatus(debugResult, errorMessage);
	refreshMacroDebuggerBreakpointRanges();
	refreshMacroDebuggerWatches();
	if (!debugResult.paused && !debugResult.hadError) macroDebuggerSessionId = 0;
	return !debugResult.hadError;
}

bool MRBentoBox::stopMacroDebuggerSession() {
	MREditWindow *variablesWindow = variablesPane();
	MRFileEditor *sourceEditor = getEditor();
	std::ostringstream log;
	const std::string displayName = macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName;
	const MRMacroExecutionSessionId stoppedSessionId = macroDebuggerSessionId;
	bool closed = false;

	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) return false;
	macroDebuggerExecutionRunning = false;
	cancelMacroDebuggerValueInput();
	if (macroDebuggerSessionId != 0) closed = mrvmCloseDebugSession(macroDebuggerSessionId);
	macroDebuggerSessionId = 0;
	macroDebuggerExecutionRoute = MRMacroExecutionRoute::Debug;
	macroDebuggerStatus = stoppedSessionId != 0 ? "stopped/no-live #" + std::to_string(stoppedSessionId) : "no live session";
	if (sourceEditor != nullptr) sourceEditor->clearDebuggerInstructionLine();
	if (closed || stoppedSessionId == 0) refreshMacroDebuggerBreakpointRanges();
	log << "MACRODBG key stage=bento-stop macro=" << displayName << " session=" << stoppedSessionId << " closed=" << (closed ? "yes" : "no");
	mrLogMessage(log.str());
	writeMacroDebuggerNotice("State: stopped/no live session");
	if (variablesWindow != nullptr) {
		static_cast<void>(variablesWindow->replaceTextBuffer("Variables\n\n(no live session)\n", "Variables"));
		variablesWindow->setReadOnly(true);
		variablesWindow->setFileChanged(false);
		if (variablesWindow->getEditor() != nullptr) variablesWindow->getEditor()->clearDebuggerVariableChangedRanges();
	}
	macroDebuggerVariables.clear();
	macroDebuggerVariableRows.clear();
	if (closed || stoppedSessionId == 0) refreshMacroDebuggerWatches();
	return true;
}

void MRBentoBox::invalidateMacroDebuggerRuntime() {
	std::string errorMessage;
	const MRMacroExecutionSessionId sessionId = macroDebuggerSessionId;
	const std::string macroKey = macroDebuggerMacroKey;
	bool sessionClosed = true;

	if (!macroDebuggerActive) return;
	cancelMacroDebuggerValueInput();
	if (sessionId != 0) sessionClosed = mrvmCloseDebugSession(sessionId, true);
	if (sessionClosed && !macroKey.empty()) static_cast<void>(mrvmEraseDebugRuntimeForMacro(macroKey, &errorMessage));
	macroDebuggerSessionId = 0;
	macroDebuggerExecutionRoute = MRMacroExecutionRoute::Debug;
	macroDebuggerExecutionRunning = false;
	macroDebuggerActive = false;
	macroDebuggerVariables.clear();
	macroDebuggerVariableRows.clear();
	if (getEditor() != nullptr) {
		getEditor()->clearDebuggerInstructionLine();
		getEditor()->clearDebuggerBreakpointRanges();
	}
	std::ostringstream log;
	log << "MACRODBG lifecycle stage=bento-close source=" << macroDebuggerSourceIdentity << " session=" << sessionId;
	if (!sessionClosed) log << " runtime_cleanup=deferred-to-worker";
	if (!errorMessage.empty()) log << " runtime_cleanup=" << errorMessage;
	mrLogMessage(log.str());
}

bool MRBentoBox::startMacroDebuggerSession(int temporaryStopLine) {
	MRMacroExecutionSession session;
	MRMacroExecutionOwner owner;
	MRMacroDebugRunResult debugResult;
	std::ostringstream log;
	std::string errorMessage;
	const std::string displayName = macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName;
	bool closed = false;

	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) return false;
	macroDebuggerExecutionRunning = false;
	cancelMacroDebuggerValueInput();
	if (macroDebuggerSessionId != 0) closed = mrvmCloseDebugSession(macroDebuggerSessionId);
	if (macroDebuggerSessionId != 0 && !closed) {
		macroDebuggerStatus = "cancellation requested #" + std::to_string(macroDebuggerSessionId);
		writeMacroDebuggerNotice("State: cancellation requested\nReset waits for worker termination.");
		return false;
	}
	macroDebuggerSessionId = 0;
	macroDebuggerExecutionRoute = MRMacroExecutionRoute::Debug;
	if (macroDebuggerSourcePath.empty() || !mrvmLoadMacroFile(macroDebuggerSourcePath, &errorMessage)) {
		debugResult.stopReason = mrdStopError;
		debugResult.hadError = true;
		if (errorMessage.empty()) errorMessage = "Debug macro source is unavailable.";
	} else {
		owner.hasBuffer = true;
		owner.bufferId = bufferId();
		debugResult = mrvmStartDebugMacroByName(macroDebuggerMacroKey, owner, &session, &errorMessage, temporaryStopLine == 0, temporaryStopLine);
	}
	if (debugResult.paused) {
		macroDebuggerSessionId = session.sessionId;
		macroDebuggerExecutionRoute = mrvmDebugSessionRoute(session.sessionId);
	}
	if (debugResult.hadError)
		macroDebuggerStatus = "error/no-live";
	else if (debugResult.stopReason == mrdStopBudget)
		macroDebuggerStatus = "running #" + std::to_string(macroDebuggerSessionId);
	else if (debugResult.paused)
		macroDebuggerStatus = "paused #" + std::to_string(macroDebuggerSessionId);
	else
		macroDebuggerStatus = "completed/no-live";
	refreshMacroDebuggerBreakpointRanges();
	log << "MACRODBG key stage=bento-" << (temporaryStopLine > 0 ? "run-here" : "reset") << " macro=" << displayName << " closed=" << (closed ? "yes" : "no") << " paused=" << (debugResult.paused ? "yes" : "no");
	if (temporaryStopLine > 0) log << " line=" << temporaryStopLine;
	if (!errorMessage.empty()) log << " error=" << errorMessage;
	mrLogMessage(log.str());
	refreshMacroDebuggerVariables(debugResult.variables);
	refreshMacroDebuggerRunMarkers(debugResult);
	writeMacroDebuggerStatus(debugResult, errorMessage);
	refreshMacroDebuggerWatches();
	if (debugResult.stopReason == mrdStopBudget) {
		macroDebuggerExecutionRunning = true;
		if (macroDebuggerRouteUsesWorker(macroDebuggerExecutionRoute) && !scheduleMacroDebuggerWorkerAction(mrdWorkerContinue)) {
			static_cast<void>(mrvmCloseDebugSession(macroDebuggerSessionId));
			macroDebuggerSessionId = 0;
			macroDebuggerExecutionRoute = MRMacroExecutionRoute::Debug;
			macroDebuggerExecutionRunning = false;
			writeMacroDebuggerNotice("State: failed/no live session\nUnable to schedule debugger worker.");
			return false;
		}
	}
	return !debugResult.hadError;
}

void MRBentoBox::restoreMacroDebuggerWorkspaceConfiguration(const MRMacroDebuggerWorkspaceConfiguration &configuration) {
	MREditWindow *variablesWindow = variablesPane();
	std::set<std::string> clearedMacroKeys;
	std::string preparationError;
	bool discarded = false;
	bool prepared;

	setMacroDebuggerTarget(configuration.macroKey, configuration.macroName);
	if (macroDebuggerSourcePath.empty() && !configuration.sourcePath.empty()) {
		macroDebuggerSourcePath = normalizeConfiguredPathInput(configuration.sourcePath);
		macroDebuggerSourceIdentity = mrvmMakeMacroSourceIdentity(macroDebuggerSourcePath, macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName);
	}
	macroDebuggerSessionId = 0;
	macroDebuggerExecutionRoute = MRMacroExecutionRoute::Debug;
	macroDebuggerExecutionRunning = false;
	clearedMacroKeys.insert(macroDebuggerMacroKey);
	for (const MRMacroDebuggerWorkspaceBreakpoint &breakpoint : configuration.breakpoints)
		clearedMacroKeys.insert(breakpoint.macroKey);
	for (const std::string &macroKey : clearedMacroKeys)
		if (!macroKey.empty()) static_cast<void>(mrvmEraseDebugRuntimeForMacro(macroKey, nullptr));
	const bool sourceIdentityMatches = configuration.sourceIdentity.empty() || configuration.sourceIdentity == macroDebuggerSourceIdentity;
	prepared = sourceIdentityMatches;
	if (!prepared) preparationError = "Debugger source identity does not match the restored editor.";
	if (prepared)
		prepared = !macroDebuggerSourcePath.empty() && mrvmLoadMacroFile(macroDebuggerSourcePath, &preparationError) &&
		           mrvmPrepareDebugMacroSourceMap(macroDebuggerMacroKey, macroDebuggerSourcePath, &preparationError);
	if (!prepared && preparationError.empty()) preparationError = "Debug macro source is unavailable.";
	if (!prepared) {
		discarded = !configuration.breakpoints.empty() || !configuration.watches.empty();
		std::ostringstream detail;

		detail << "MACRODBG workspace stage=restore-cold-unbound macro=" << macroDebuggerMacroKey;
		if (!preparationError.empty()) detail << " error=" << preparationError;
		mrLogMessage(detail.str());
	} else {
		for (const MRMacroDebuggerWorkspaceBreakpoint &breakpoint : configuration.breakpoints) {
			std::string breakpointError;
			std::string conditionError;
			const std::string expectedIdentity = mrvmMakeMacroSourceIdentity(macroDebuggerSourcePath, breakpoint.macroKey);
			const bool sourceMatches = breakpoint.sourceIdentity.empty() || breakpoint.sourceIdentity == expectedIdentity;
			const bool conditionValid = breakpoint.conditionText.empty() || mrvmValidateDebugWatchExpression(breakpoint.conditionText, &conditionError);

			if (sourceMatches && conditionValid && mrvmWriteDebugLineBreakpoint(breakpoint.macroKey, breakpoint.line, breakpoint.enabled, &breakpointError, breakpoint.conditionText)) continue;
			discarded = true;
			std::ostringstream detail;

			detail << "MACRODBG breakpoint stage=restore-cold-dropped source=" << breakpoint.sourceIdentity << " expected=" << expectedIdentity << " line=" << breakpoint.line;
			if (!conditionError.empty()) detail << " condition_error=" << conditionError;
			if (!breakpointError.empty()) detail << " error=" << breakpointError;
			mrLogMessage(detail.str());
		}
		for (const MRMacroDebuggerWorkspaceWatch &watch : configuration.watches) {
			std::string watchError;

			if (mrvmWriteDebugWatch(macroDebuggerMacroKey, watch.expression, watch.enabled, &watchError)) continue;
			discarded = true;
			std::ostringstream detail;

			detail << "MACRODBG watch stage=restore-cold-dropped macro=" << macroDebuggerMacroKey << " expression=" << watch.expression;
			if (!watchError.empty()) detail << " error=" << watchError;
			mrLogMessage(detail.str());
		}
	}
	macroDebuggerStatus = "config restored/no-live";
	writeMacroDebuggerNotice("State: debug config restored, no live session");
	if (variablesWindow != nullptr) {
		static_cast<void>(variablesWindow->replaceTextBuffer("Variables\n\n(no live session)\n", "Variables"));
		variablesWindow->setReadOnly(true);
		variablesWindow->setFileChanged(false);
	}
	refreshMacroDebuggerWatches();
	if (discarded) mrMarkWorkspaceAutosaveDirty("debugger workspace invalid entries dropped", this);
}

bool MRBentoBox::resetMacroDebuggerSession() {
	return startMacroDebuggerSession(0);
}

bool MRBentoBox::runMacroDebuggerToCursor() {
	MRFileEditor *sourceEditor = getEditor();

	if (!macroDebuggerActive || sourceEditor == nullptr) return false;
	return startMacroDebuggerSession(static_cast<int>(sourceEditor->lineIndexOfOffset(sourceEditor->cursorOffset()) + 1));
}

void MRBentoBox::refreshMacroDebuggerRunMarkers(const MRMacroDebugRunResult &debugResult) {
	MRFileEditor *sourceEditor = getEditor();
	const std::string projectedMacroKey = debugResult.macroKey.empty() ? macroDebuggerMacroKey : debugResult.macroKey;
	int line = 0;

	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) {
		macroDebuggerStatus.clear();
		if (sourceEditor != nullptr) sourceEditor->clearDebuggerInstructionLine();
		return;
	}
	if (macroDebuggerSessionId > 0) {
		std::ostringstream status;

		if (debugResult.paused)
			status << "paused";
		else if (debugResult.hadError)
			status << "error/no-live";
		else if (debugResult.cancelled)
			status << "cancelled/no-live";
		else if (debugResult.stopReason == mrdStopCompleted)
			status << "completed/no-live";
		else {
			status << mrMacroDebuggerStopReasonText(debugResult.stopReason);
			status << "/no-live";
		}
		status << " #" << macroDebuggerSessionId;
		macroDebuggerStatus = status.str();
	}
	if (sourceEditor == nullptr) return;
	const char *currentSourcePath = sourceEditor->persistentFileName();

	if (!debugResult.sourcePath.empty() && (currentSourcePath == nullptr || debugResult.sourcePath != currentSourcePath)) {
		if (!loadFromFile(debugResult.sourcePath.c_str())) return;
		sourceEditor = getEditor();
		if (sourceEditor == nullptr) return;
	}
	macroDebuggerProjectedMacroKey = projectedMacroKey;
	if (!macroDebuggerActive || macroDebuggerMacroKey.empty() || !debugResult.paused) {
		sourceEditor->clearDebuggerInstructionLine();
		return;
	}
	if (!mrvmDebugSourceLineForInstruction(projectedMacroKey, debugResult.instructionOffset, &line) || line <= 0) {
		sourceEditor->clearDebuggerInstructionLine();
		return;
	}
	const std::size_t instructionLine = static_cast<std::size_t>(line - 1);
	const int cursorColumn = sourceEditor->displayedCursorColumn();

	sourceEditor->setDebuggerInstructionLine(instructionLine);
	sourceEditor->centerDocumentLocationInView(instructionLine, cursorColumn);
	sourceEditor->setCursorOffsetAtVisualColumn(sourceEditor->bufferModel().lineStartByIndex(instructionLine), cursorColumn);
}

void MRBentoBox::refreshMacroDebuggerBreakpointRanges() {
	MRFileEditor *sourceEditor = getEditor();
	std::vector<MRMacroDebuggerBreakpoint> breakpoints;
	std::vector<std::pair<std::size_t, std::size_t>> activeRanges;
	std::vector<std::pair<std::size_t, std::size_t>> inactiveRanges;
	std::vector<std::pair<std::size_t, std::size_t>> unboundRanges;
	std::vector<std::size_t> unboundLines;

	if (sourceEditor == nullptr) return;
	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) {
		sourceEditor->clearDebuggerBreakpointRanges();
		return;
	}
	const std::string breakpointMacroKey = macroDebuggerProjectedMacroKey.empty() ? macroDebuggerMacroKey : macroDebuggerProjectedMacroKey;

	activeRanges.reserve(breakpoints.size());
	inactiveRanges.reserve(breakpoints.size());
	if (mrvmDebugLineBreakpointsForMacro(breakpointMacroKey, breakpoints))
		for (const MRMacroDebuggerBreakpoint &breakpoint : breakpoints) {
			if (breakpoint.enabled) activeRanges.push_back(std::pair<std::size_t, std::size_t>(breakpoint.sourceStartOffset, breakpoint.sourceEndOffset));
			else
				inactiveRanges.push_back(std::pair<std::size_t, std::size_t>(breakpoint.sourceStartOffset, breakpoint.sourceEndOffset));
		}
	if (activeRanges.empty() && inactiveRanges.empty()) sourceEditor->clearDebuggerBreakpointRanges();
	else
		sourceEditor->setDebuggerBreakpointRanges(activeRanges, inactiveRanges, unboundRanges, unboundLines);
}

bool MRBentoBox::handleMacroDebuggerFunctionKey(TEvent &event) {
	const MacroDebuggerFunctionKeyDescriptor *descriptor = nullptr;

	if (event.what != evKeyDown) return false;
	const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);
	for (const MacroDebuggerFunctionKeyDescriptor &candidate : kMacroDebuggerFunctionKeys)
		if (normalized == TKey(candidate.keyCode, candidate.controlKeyState)) {
			descriptor = &candidate;
			break;
		}
	if (descriptor == nullptr || !macroDebuggerActive) return false;
	if (descriptor->requiresLiveSession && macroDebuggerSessionId == 0) {
		clearEvent(event);
		return true;
	}
	switch (descriptor->action) {
		case mdfkaEvaluate:
			static_cast<void>(evaluateMacroDebuggerExpression());
			break;
		case mdfkaContinue:
			static_cast<void>(continueMacroDebuggerSession());
			break;
		case mdfkaRunHere:
			static_cast<void>(runMacroDebuggerToCursor());
			break;
		case mdfkaStop:
			static_cast<void>(macroDebuggerSessionId == 0 ? resetMacroDebuggerSession() : stopMacroDebuggerSession());
			break;
		case mdfkaStepInto:
			static_cast<void>(stepMacroDebuggerSession(mrdStepInto));
			break;
		case mdfkaStepOver:
			static_cast<void>(stepMacroDebuggerSession(mrdStepOver));
			break;
		case mdfkaStepOut:
			static_cast<void>(stepMacroDebuggerSession(mrdStepOut));
			break;
		case mdfkaBreakpoint:
			static_cast<void>(toggleMacroDebuggerBreakpointAtCursor());
			break;
		case mdfkaBreakpointEnable:
			static_cast<void>(toggleMacroDebuggerBreakpointEnabledAtCursor());
			break;
		case mdfkaBreakpointAllToggle:
			static_cast<void>(toggleMacroDebuggerBreakpointsEnabled());
			break;
		case mdfkaBreakpointClearAll:
			static_cast<void>(eraseMacroDebuggerBreakpoints());
			break;
		case mdfkaAddWatch:
			static_cast<void>(addMacroDebuggerWatch());
			break;
		case mdfkaEraseWatch:
			static_cast<void>(eraseMacroDebuggerWatch());
			break;
	}
	mrLogMessage("MACRODBG key stage=bento-fkey");
	clearEvent(event);
	bentoProjectionDirty |= bpdContent | bpdChrome;
	flushBentoProjection();
	return true;
}
