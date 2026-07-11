#define Uses_Dialogs
#define Uses_TInputLine
#include "MRBentoBox.hpp"

#include "MRFrame.hpp"

#include "../app/commands/MRWindowCommands.hpp"
#include "../mrmac/MRVM.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/vm/MRVMRuntimeDebugger.hpp"

#include <array>
#include <cstring>
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

const char *macroDebuggerStopReasonText(MRMacroDebugStopReason reason) noexcept {
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

struct MacroDebuggerVariableGroup {
	MRMacroDebugVariableScope scope;
	const char *title;
};

static const MacroDebuggerVariableGroup kMacroDebuggerVariableGroups[] = {
	{mrdVariableLocal, "Locals"},
	{mrdVariableFileGlobal, "File globals"},
	{mrdVariableAppGlobal, "App globals"},
	{mrdVariableClosure, "Closure"},
	{mrdVariableSession, "Session"},
};

void appendMacroDebuggerControls(std::ostringstream &out, bool liveSession) {
	out << "\nControls:\n";
	if (liveSession) {
		out << "F5 Pause/Continue\n";
		out << "F8 Stop\n";
		out << "F10 Into\n";
		out << "F11 Over\n";
		out << "Shift+F11 Out\n";
	} else {
		out << "F5 Pause/Continue unavailable\n";
		out << "F6 Run Here\n";
		out << "F8 Stop unavailable / Reset\n";
		out << "F10 Step unavailable\n";
	}
	out << "F9 Toggle Breakpoint\n";
	out << "Shift+F9 Enable/Disable Breakpoint\n";
	out << "Alt+Shift+F9 Enable/Disable All Breakpoints\n";
	out << "Ctrl+Shift+F9 Clear All Breakpoints\n";
	out << "F7 Add Watch\n";
	out << "Shift+F7 Remove Watch\n";
}

std::string macroDebuggerOutputText(const std::string &macroName, MRMacroExecutionSessionId sessionId, const MRMacroDebugRunResult &debugResult, const std::string &errorMessage) {
	std::ostringstream out;

	out << "Macro Debugger\n";
	out << "Macro: " << macroName << "\n";
	if (sessionId != 0) out << "Session: #" << sessionId << "\n";
	else
		out << "Session: none\n";
	out << "State: " << (debugResult.hadError ? "error" : (debugResult.cancelled ? "cancelled" : (debugResult.paused ? "paused" : "completed"))) << "\n";
	out << "Stop: " << macroDebuggerStopReasonText(debugResult.stopReason) << "\n";
	out << "Instruction offset: " << debugResult.instructionOffset << "\n";
	out << "Stack depth: " << debugResult.stackDepth << "\n";
	appendMacroDebuggerControls(out, debugResult.paused);
	if (!errorMessage.empty()) {
		out << "\nError:\n";
		out << errorMessage << "\n";
	}
	if (!debugResult.logLines.empty()) {
		out << "\nLog:\n";
		for (const std::string &line : debugResult.logLines)
			out << line << "\n";
	}
	return out.str();
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
	mdfkaEraseWatch
};

struct MacroDebuggerFunctionKeyDescriptor {
	ushort keyCode;
	ushort controlKeyState;
	MacroDebuggerFunctionKeyAction action;
	bool requiresLiveSession;
};

static const MacroDebuggerFunctionKeyDescriptor kMacroDebuggerFunctionKeys[] = {
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
	appendMacroDebuggerControls(out, macroDebuggerSessionId != 0);
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
	appendMacroDebuggerControls(output, macroDebuggerSessionId != 0);
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
	appendMacroDebuggerControls(output, macroDebuggerSessionId != 0);
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
	appendMacroDebuggerControls(output, macroDebuggerSessionId != 0);
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
		text += "\n";
		for (const MacroDebuggerVariableGroup &group : kMacroDebuggerVariableGroups) {
			bool groupHasVariables = false;

			for (const MRMacroDebugVariableSnapshot &variable : projectedVariables)
				if (variable.scope == group.scope) {
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
					if (previous.scope == variable.scope && previous.type == variable.type && previous.name == variable.name) {
						valueChanged = previous.valueText != variable.valueText;
						break;
					}
				const std::size_t rowStart = text.size();
				text += "  ";
				text += variable.name;
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
		if (variable.type != TYPE_INT && variable.type != TYPE_REAL && variable.type != TYPE_STR && variable.type != TYPE_CHAR) return false;
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

		std::strncpy(value.data(), variable.valueText.c_str(), value.size() - 1);
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
	MRFileEditor *variablesEditor = macroDebuggerValueInputPane != nullptr ? macroDebuggerValueInputPane->getEditor() : nullptr;
	const std::size_t cursor = variablesEditor != nullptr ? variablesEditor->cursorOffset() : 0;
	MRMacroDebugVariableSnapshot variable;
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
	if (!mrvmWriteDebugScalarVariable(macroDebuggerSessionId, variable, value.data(), updatedVariables, &errorMessage)) {
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
	if (inputBox("MACRO DEBUGGER", "Watch expression", expression, sizeof(expression) - 1) == cmCancel) return true;
	if (!mrvmWriteDebugWatch(macroDebuggerMacroKey, expression, true, &errorMessage)) return false;
	mrMarkWorkspaceAutosaveDirty("debugger watch add", this);
	refreshMacroDebuggerWatches();
	return true;
}

bool MRBentoBox::eraseMacroDebuggerWatch() {
	char expression[256] = {};
	std::string errorMessage;

	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) return false;
	if (inputBox("MACRO DEBUGGER", "Remove watch", expression, sizeof(expression) - 1) == cmCancel) return true;
	if (!mrvmEraseDebugWatch(macroDebuggerMacroKey, expression, &errorMessage)) return false;
	mrMarkWorkspaceAutosaveDirty("debugger watch erase", this);
	refreshMacroDebuggerWatches();
	return true;
}

bool MRBentoBox::continueMacroDebuggerSession() {
	MREditWindow *outputWindow = debuggerOutputPane();
	MRMacroDebugRunResult debugResult;
	std::ostringstream log;
	std::string errorMessage;
	const std::string displayName = macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName;
	const MRMacroExecutionSessionId renderedSessionId = macroDebuggerSessionId;

	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) return false;
	cancelMacroDebuggerValueInput();
	if (macroDebuggerSessionId == 0) {
		macroDebuggerStatus = "no live session";
		if (outputWindow != nullptr) {
			std::ostringstream out;

			out << "Macro Debugger\n";
			out << "Macro: " << displayName << "\n";
			out << "Session: none\n";
			out << "State: no live session\n";
			appendMacroDebuggerControls(out, false);
			static_cast<void>(outputWindow->replaceTextBuffer(out.str().c_str(), "Debugger Output"));
			outputWindow->setReadOnly(true);
			outputWindow->setFileChanged(false);
		}
		return false;
	}
	if (macroDebuggerExecutionRunning) {
		if (!mrvmRequestDebugPause(macroDebuggerSessionId, &errorMessage)) return false;
		macroDebuggerStatus = "pause requested #" + std::to_string(macroDebuggerSessionId);
		if (outputWindow != nullptr) {
			static_cast<void>(outputWindow->replaceTextBuffer(("Macro Debugger\nMacro: " + displayName + "\nSession: #" + std::to_string(macroDebuggerSessionId) + "\nState: pause requested\n").c_str(), "Debugger Output"));
			outputWindow->setReadOnly(true);
			outputWindow->setFileChanged(false);
		}
		return true;
	}
	if (!mrvmScheduleDebugMacroContinue(macroDebuggerSessionId, macroDebuggerMacroKey, &errorMessage)) return false;
	macroDebuggerExecutionRunning = true;
	macroDebuggerStatus = "running #" + std::to_string(macroDebuggerSessionId);
	log << "MACRODBG key stage=bento-continue macro=" << displayName << " session=" << macroDebuggerSessionId << " scheduled=yes";
	if (!errorMessage.empty()) log << " error=" << errorMessage;
	mrLogMessage(log.str());
	if (outputWindow != nullptr) {
		static_cast<void>(outputWindow->replaceTextBuffer(("Macro Debugger\nMacro: " + displayName + "\nSession: #" + std::to_string(renderedSessionId) + "\nState: running\nStop: running\n\nControls:\nF5 Pause\nF8 Stop\n").c_str(), "Debugger Output"));
		outputWindow->setReadOnly(true);
		outputWindow->setFileChanged(false);
	}
	return true;
}

void MRBentoBox::pumpMacroDebuggerSession() {
	MREditWindow *outputWindow = debuggerOutputPane();
	MRMacroDebugRunResult debugResult;
	std::string errorMessage;
	const std::string displayName = macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName;
	const MRMacroExecutionSessionId renderedSessionId = macroDebuggerSessionId;

	if (!macroDebuggerExecutionRunning || macroDebuggerSessionId == 0) return;
	if (!mrvmPumpDebugSession(macroDebuggerSessionId, macroDebuggerMacroKey, debugResult, &errorMessage)) return;
	if (debugResult.stopReason == mrdStopBudget) return;
	macroDebuggerExecutionRunning = false;
	if (outputWindow != nullptr) {
		static_cast<void>(outputWindow->replaceTextBuffer(macroDebuggerOutputText(displayName, renderedSessionId, debugResult, errorMessage).c_str(), "Debugger Output"));
		outputWindow->setReadOnly(true);
		outputWindow->setFileChanged(false);
	}
	refreshMacroDebuggerVariables(debugResult.variables);
	refreshMacroDebuggerRunMarkers(debugResult);
	refreshMacroDebuggerBreakpointRanges();
	refreshMacroDebuggerWatches();
	if (!debugResult.paused && !debugResult.hadError) macroDebuggerSessionId = 0;
	bentoProjectionDirty |= bpdContent | bpdChrome;
	flushBentoProjection();
}

bool MRBentoBox::stepMacroDebuggerSession(MRMacroDebugStepMode mode) {
	MREditWindow *outputWindow = debuggerOutputPane();
	MRMacroDebugRunResult debugResult;
	std::ostringstream log;
	std::string errorMessage;
	const std::string displayName = macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName;
	const MRMacroExecutionSessionId renderedSessionId = macroDebuggerSessionId;

	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) return false;
	if (macroDebuggerExecutionRunning) return false;
	cancelMacroDebuggerValueInput();
	if (macroDebuggerSessionId == 0) {
		macroDebuggerStatus = "no live session";
		if (outputWindow != nullptr) {
			std::ostringstream out;

			out << "Macro Debugger\n";
			out << "Macro: " << displayName << "\n";
			out << "Session: none\n";
			out << "State: no live session\n";
			appendMacroDebuggerControls(out, false);
			static_cast<void>(outputWindow->replaceTextBuffer(out.str().c_str(), "Debugger Output"));
			outputWindow->setReadOnly(true);
			outputWindow->setFileChanged(false);
		}
		return false;
	}
	if (mode == mrdStepOver)
		debugResult = mrvmStepOverDebugMacroByName(macroDebuggerSessionId, macroDebuggerMacroKey, &errorMessage);
	else if (mode == mrdStepOut)
		debugResult = mrvmStepOutDebugMacroByName(macroDebuggerSessionId, macroDebuggerMacroKey, &errorMessage);
	else
		debugResult = mrvmStepDebugMacroByName(macroDebuggerSessionId, macroDebuggerMacroKey, &errorMessage);
	log << "MACRODBG key stage=bento-step-" << (mode == mrdStepOver ? "over" : (mode == mrdStepOut ? "out" : "into")) << " macro=" << displayName << " session=" << macroDebuggerSessionId << " paused=" << (debugResult.paused ? "yes" : "no") << " stop=" << macroDebuggerStopReasonText(debugResult.stopReason);
	if (!errorMessage.empty()) log << " error=" << errorMessage;
	mrLogMessage(log.str());
	if (outputWindow != nullptr) {
		static_cast<void>(outputWindow->replaceTextBuffer(macroDebuggerOutputText(displayName, renderedSessionId, debugResult, errorMessage).c_str(), "Debugger Output"));
		outputWindow->setReadOnly(true);
		outputWindow->setFileChanged(false);
	}
	refreshMacroDebuggerVariables(debugResult.variables);
	refreshMacroDebuggerRunMarkers(debugResult);
	refreshMacroDebuggerBreakpointRanges();
	refreshMacroDebuggerWatches();
	if (!debugResult.paused && !debugResult.hadError) macroDebuggerSessionId = 0;
	return !debugResult.hadError;
}

bool MRBentoBox::stopMacroDebuggerSession() {
	MREditWindow *outputWindow = debuggerOutputPane();
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
	macroDebuggerStatus = stoppedSessionId != 0 ? "stopped/no-live #" + std::to_string(stoppedSessionId) : "no live session";
	if (sourceEditor != nullptr) sourceEditor->clearDebuggerInstructionLine();
	refreshMacroDebuggerBreakpointRanges();
	log << "MACRODBG key stage=bento-stop macro=" << displayName << " session=" << stoppedSessionId << " closed=" << (closed ? "yes" : "no");
	mrLogMessage(log.str());
	if (outputWindow != nullptr) {
		std::ostringstream out;

		out << "Macro Debugger\n";
		out << "Macro: " << displayName << "\n";
		out << "Session: none\n";
		out << "State: stopped/no live session\n";
		appendMacroDebuggerControls(out, false);
		static_cast<void>(outputWindow->replaceTextBuffer(out.str().c_str(), "Debugger Output"));
		outputWindow->setReadOnly(true);
		outputWindow->setFileChanged(false);
	}
	if (variablesWindow != nullptr) {
		static_cast<void>(variablesWindow->replaceTextBuffer("Variables\n\n(no live session)\n", "Variables"));
		variablesWindow->setReadOnly(true);
		variablesWindow->setFileChanged(false);
		if (variablesWindow->getEditor() != nullptr) variablesWindow->getEditor()->clearDebuggerVariableChangedRanges();
	}
	macroDebuggerVariables.clear();
	macroDebuggerVariableRows.clear();
	refreshMacroDebuggerWatches();
	return true;
}

bool MRBentoBox::startMacroDebuggerSession(int temporaryStopLine) {
	MREditWindow *outputWindow = debuggerOutputPane();
	MRMacroExecutionSession session;
	MRMacroDebugRunResult debugResult;
	std::ostringstream log;
	std::string errorMessage;
	const std::string displayName = macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName;
	bool closed = false;

	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) return false;
	macroDebuggerExecutionRunning = false;
	cancelMacroDebuggerValueInput();
	if (macroDebuggerSessionId != 0) closed = mrvmCloseDebugSession(macroDebuggerSessionId);
	macroDebuggerSessionId = 0;
	if (macroDebuggerSourcePath.empty() || !mrvmLoadMacroFile(macroDebuggerSourcePath, &errorMessage)) {
		debugResult.stopReason = mrdStopError;
		debugResult.hadError = true;
		if (errorMessage.empty()) errorMessage = "Debug macro source is unavailable.";
	} else
		debugResult = mrvmStartDebugMacroByName(macroDebuggerMacroKey, MRMacroExecutionOwner(), &session, &errorMessage, temporaryStopLine == 0, temporaryStopLine);
	if (debugResult.paused) macroDebuggerSessionId = session.sessionId;
	if (debugResult.paused && (!macroDebuggerWorkspacePending.breakpoints.empty() || !macroDebuggerWorkspacePending.watches.empty())) {
		MRMacroDebuggerWorkspaceConfiguration remaining;
		bool rebound = false;

		remaining.macroKey = macroDebuggerWorkspacePending.macroKey;
		remaining.macroName = macroDebuggerWorkspacePending.macroName;
		for (const MRMacroDebuggerWorkspaceBreakpoint &breakpoint : macroDebuggerWorkspacePending.breakpoints) {
			std::string breakpointError;

			if (mrvmWriteDebugLineBreakpoint(breakpoint.macroKey, breakpoint.line, breakpoint.enabled, &breakpointError)) rebound = true;
			else
				remaining.breakpoints.push_back(breakpoint);
		}
		for (const MRMacroDebuggerWorkspaceWatch &watch : macroDebuggerWorkspacePending.watches) {
			std::string watchError;

			if (mrvmWriteDebugWatch(macroDebuggerMacroKey, watch.expression, watch.enabled, &watchError)) rebound = true;
			else
				remaining.watches.push_back(watch);
		}
		macroDebuggerWorkspacePending = remaining;
		if (rebound) mrMarkWorkspaceAutosaveDirty("debugger workspace rebind", this);
	}
	if (debugResult.hadError)
		macroDebuggerStatus = "error/no-live";
	else if (debugResult.paused)
		macroDebuggerStatus = "paused #" + std::to_string(macroDebuggerSessionId);
	else
		macroDebuggerStatus = "completed/no-live";
	refreshMacroDebuggerBreakpointRanges();
	log << "MACRODBG key stage=bento-" << (temporaryStopLine > 0 ? "run-here" : "reset") << " macro=" << displayName << " closed=" << (closed ? "yes" : "no") << " paused=" << (debugResult.paused ? "yes" : "no");
	if (temporaryStopLine > 0) log << " line=" << temporaryStopLine;
	if (!errorMessage.empty()) log << " error=" << errorMessage;
	mrLogMessage(log.str());
	if (outputWindow != nullptr) {
		static_cast<void>(outputWindow->replaceTextBuffer(macroDebuggerOutputText(displayName, session.sessionId, debugResult, errorMessage).c_str(), "Debugger Output"));
		outputWindow->setReadOnly(true);
		outputWindow->setFileChanged(false);
	}
	refreshMacroDebuggerVariables(debugResult.variables);
	refreshMacroDebuggerRunMarkers(debugResult);
	refreshMacroDebuggerWatches();
	return !debugResult.hadError;
}

void MRBentoBox::restoreMacroDebuggerWorkspaceConfiguration(const MRMacroDebuggerWorkspaceConfiguration &configuration) {
	MREditWindow *outputWindow = debuggerOutputPane();
	MREditWindow *variablesWindow = variablesPane();
	std::ostringstream output;

	macroDebuggerWorkspacePending = configuration;
	setMacroDebuggerTarget(configuration.macroKey, configuration.macroName);
	macroDebuggerSessionId = 0;
	macroDebuggerStatus = "config restored/no-live";
	output << "Macro Debugger\n";
	output << "Macro: " << (macroDebuggerMacroName.empty() ? macroDebuggerMacroKey : macroDebuggerMacroName) << "\n";
	output << "Session: none\n";
	output << "State: debug config restored, no live session\n";
	appendMacroDebuggerControls(output, false);
	if (outputWindow != nullptr) {
		static_cast<void>(outputWindow->replaceTextBuffer(output.str().c_str(), "Debugger Output"));
		outputWindow->setReadOnly(true);
		outputWindow->setFileChanged(false);
	}
	if (variablesWindow != nullptr) {
		static_cast<void>(variablesWindow->replaceTextBuffer("Variables\n\n(no live session)\n", "Variables"));
		variablesWindow->setReadOnly(true);
		variablesWindow->setFileChanged(false);
	}
	refreshMacroDebuggerWatches();
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
			status << macroDebuggerStopReasonText(debugResult.stopReason);
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

	if (sourceEditor == nullptr) return;
	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) {
		sourceEditor->clearDebuggerBreakpointRanges();
		return;
	}
	const std::string breakpointMacroKey = macroDebuggerProjectedMacroKey.empty() ? macroDebuggerMacroKey : macroDebuggerProjectedMacroKey;

	if (!mrvmDebugLineBreakpointsForMacro(breakpointMacroKey, breakpoints)) {
		sourceEditor->clearDebuggerBreakpointRanges();
		return;
	}
	activeRanges.reserve(breakpoints.size());
	inactiveRanges.reserve(breakpoints.size());
	for (const MRMacroDebuggerBreakpoint &breakpoint : breakpoints) {
		if (breakpoint.enabled) activeRanges.push_back(std::pair<std::size_t, std::size_t>(breakpoint.sourceStartOffset, breakpoint.sourceEndOffset));
		else
			inactiveRanges.push_back(std::pair<std::size_t, std::size_t>(breakpoint.sourceStartOffset, breakpoint.sourceEndOffset));
	}
	if (activeRanges.empty() && inactiveRanges.empty()) sourceEditor->clearDebuggerBreakpointRanges();
	else
		sourceEditor->setDebuggerBreakpointRanges(activeRanges, inactiveRanges);
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
