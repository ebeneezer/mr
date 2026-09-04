#define Uses_Dialogs
#define Uses_TInputLine
#include "MRBentoBox.hpp"

#include "MRBentoBoxDebuggerStatus.hpp"

#include "../MRFrame.hpp"

#include "../../app/commands/MRWindowCommands.hpp"
#include "../../app/services/MRGdbSession.hpp"
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

class MRDebuggerValueInput : public TInputLine {
  public:
	MRDebuggerValueInput(const TRect &bounds, MRBentoBox *bento) noexcept : TInputLine(bounds, 255), mBento(bento), mError(false) {
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
				if (mBento != nullptr) mBento->commitDebuggerValueInput();
				clearEvent(event);
				return;
			}
			if (key == kbEsc) {
				if (mBento != nullptr) mBento->cancelDebuggerValueInput();
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

} // namespace

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

	if (debuggerValueInput != nullptr || variablesEditor == nullptr || macroDebuggerSessionId == 0) return false;
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
		debuggerValueInput = new MRDebuggerValueInput(TRect(left, top, viewport.b.x, top + 1), this);
		debuggerValueInputPane = variablesWindow;
		gdbDebuggerValueInputObjectName.clear();
		std::array<char, 255> value{};

		if (variable.type != TYPE_HASH && !macroDebuggerVariableIsArray(variable.type)) std::strncpy(value.data(), variable.valueText.c_str(), value.size() - 1);
		debuggerValueInput->setData(value.data());
		variablesWindow->insert(debuggerValueInput);
		debuggerValueInput->selectAll(True);
		debuggerValueInput->select();
		return true;
	}
	return false;
}

bool MRBentoBox::showGdbDebuggerValueInputAtCursor() {
	MRPaneEditWindow *variablesWindow = dynamic_cast<MRPaneEditWindow *>(variablesPane());
	MRFileEditor *variablesEditor = variablesWindow != nullptr ? variablesWindow->getEditor() : nullptr;
	const std::size_t cursor = variablesEditor != nullptr ? variablesEditor->cursorOffset() : 0;

	if (debuggerValueInput != nullptr || variablesEditor == nullptr || !gdbDebuggerActive() || gdbDebuggerRunning()) return false;
	for (const GdbDebuggerVariableRow &row : gdbDebuggerVariableRows) {
		if (cursor < row.start || cursor >= row.end) continue;
		const std::string text = variablesEditor->snapshotText();
		const std::size_t valueStart = text.find("= ", row.start);

		if (valueStart == std::string::npos || valueStart + 2 > row.end) return false;
		variablesEditor->setCursorOffset(valueStart + 2);
		const TRect viewport = variablesEditor->visibleTextViewportBounds();
		const int left = viewport.a.x + variablesEditor->currentViewColumn() - 1;
		const int top = viewport.a.y + variablesEditor->currentViewRow() - 1;

		if (left >= viewport.b.x || top < viewport.a.y || top >= viewport.b.y) return false;
		debuggerValueInput = new MRDebuggerValueInput(TRect(left, top, viewport.b.x, top + 1), this);
		debuggerValueInputPane = variablesWindow;
		gdbDebuggerValueInputObjectName = row.objectName;
		std::array<char, 255> value{};

		std::strncpy(value.data(), row.value.c_str(), value.size() - 1);
		debuggerValueInput->setData(value.data());
		variablesWindow->insert(debuggerValueInput);
		debuggerValueInput->selectAll(True);
		debuggerValueInput->select();
		return true;
	}
	return false;
}

bool MRBentoBox::debuggerValueInputContains(const TPoint &point) const noexcept {
	return debuggerValueInput != nullptr && debuggerValueInput->mouseInView(point);
}

void MRBentoBox::commitDebuggerValueInput() {
	std::array<char, 255> value{};
	std::vector<MRMacroDebugVariableSnapshot> updatedVariables;
	std::string errorMessage;
	std::string parseError;
	MRFileEditor *variablesEditor = debuggerValueInputPane != nullptr ? debuggerValueInputPane->getEditor() : nullptr;
	const std::size_t cursor = variablesEditor != nullptr ? variablesEditor->cursorOffset() : 0;
	MRMacroDebugVariableSnapshot variable;
	MRMacroDebugValueMutation mutation;
	bool found = false;

	if (debuggerValueInput == nullptr) return;
	debuggerValueInput->getData(value.data());
	if (!gdbDebuggerValueInputObjectName.empty()) {
		if (!sendGdbCommand(MRGdbCommandKind::AssignVariable, value.data(), gdbDebuggerValueInputObjectName)) {
			debuggerValueInput->setError(true);
			return;
		}
		cancelDebuggerValueInput();
		return;
	}
	for (std::size_t index = 0; index < macroDebuggerVariableRows.size() && index < macroDebuggerVariables.size(); ++index)
		if (cursor >= macroDebuggerVariableRows[index].first && cursor < macroDebuggerVariableRows[index].second) {
			variable = macroDebuggerVariables[index];
			found = true;
			break;
		}
	if (!found) {
		debuggerValueInput->setError(true);
		return;
	}
	if (!parseMacroDebuggerMutation(variable, value.data(), mutation, parseError)) {
		mrLogMessage("MACRODBG mutate rejected name=" + variable.name + " error=" + parseError);
		debuggerValueInput->setError(true);
		return;
	}
	if (!mrvmMutateDebugValue(macroDebuggerSessionId, mutation, updatedVariables, &errorMessage)) {
		mrLogMessage("MACRODBG mutate rejected name=" + variable.name + " error=" + errorMessage);
		debuggerValueInput->setError(true);
		return;
	}
	mrLogMessage("MACRODBG mutate applied name=" + variable.name);
	cancelDebuggerValueInput();
	refreshMacroDebuggerVariables(updatedVariables);
	refreshMacroDebuggerWatches();
}

void MRBentoBox::cancelDebuggerValueInput() noexcept {
	if (debuggerValueInput != nullptr && debuggerValueInputPane != nullptr) {
		debuggerValueInputPane->remove(debuggerValueInput);
		TObject::destroy(debuggerValueInput);
	}
	debuggerValueInput = nullptr;
	debuggerValueInputPane = nullptr;
	gdbDebuggerValueInputObjectName.clear();
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
