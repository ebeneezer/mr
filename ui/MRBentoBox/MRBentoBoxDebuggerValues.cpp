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
#include "../../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../../mrmac/vm/MRVMValue.hpp"
#include <mutex>
#include <cstdlib>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

#include <array>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <unordered_map>

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

int MRBentoBox::debuggerUiState(bool create) const {
	// The caller holds the VM execution mutex while using this handle.
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	VirtualMachine::Value app, debugger, views, view;
	if (!create) {
		if (!kv.findRoot("APPLICATIONUI", app) || !kv.findChild(app, "debugger", debugger) ||
		    !kv.findChild(debugger, "views", views) || !kv.findChild(views, std::to_string(bufferId()), view)) return 0;
		return view.hashHandle;
	}
	app = kv.ensureRoot("APPLICATIONUI");
	debugger = kv.ensureChild(app, "debugger");
	views = kv.ensureChild(debugger, "views");
	return kv.ensureChild(views, std::to_string(bufferId())).hashHandle;
}

void MRBentoBox::clearDebuggerUiState() noexcept {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	VirtualMachine::Value app, debugger, views;
	if (kv.findRoot("APPLICATIONUI", app) && kv.findChild(app, "debugger", debugger) && kv.findChild(debugger, "views", views))
		kv.eraseChild(views, std::to_string(bufferId()));
}

std::vector<MRBentoBox::GdbDebuggerVariableRow> MRBentoBox::readGdbDebuggerRows(bool watches) const {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	MRVMHashStore &store = kv.globalStore();
	VirtualMachine::Value entries;
	std::vector<GdbDebuggerVariableRow> rows;
	const int state = debuggerUiState();
	if (state == 0 || !kv.findChild(mrvmMakeHash(state, true), watches ? "gdbWatches" : "gdbVariables", entries)) return rows;
	const int count = store.read(entries.hashHandle, "count").i;
	rows.reserve(count);
	for (int index = 0; index < count; ++index) {
		VirtualMachine::Value item;
		if (!kv.findChild(entries, std::to_string(index), item)) continue;
		GdbDebuggerVariableRow row;
		row.start = std::strtoull(store.read(item.hashHandle, "start").s.c_str(), nullptr, 10);
		row.end = std::strtoull(store.read(item.hashHandle, "end").s.c_str(), nullptr, 10);
		row.valueStart = std::strtoull(store.read(item.hashHandle, "valueStart").s.c_str(), nullptr, 10);
		row.arrayOwner = std::strtoull(store.read(item.hashHandle, "arrayOwner").s.c_str(), nullptr, 10);
		row.byteArray = store.contains(item.hashHandle, "byteArray") && store.read(item.hashHandle, "byteArray").i != 0;
		row.label = store.read(item.hashHandle, "label").s;
		row.expression = store.read(item.hashHandle, "expression").s;
		row.objectName = store.read(item.hashHandle, "objectName").s;
		row.value = store.read(item.hashHandle, "value").s;
		row.changed = store.read(item.hashHandle, "changed").i != 0;
		rows.push_back(std::move(row));
	}
	return rows;
}

void MRBentoBox::writeGdbDebuggerRows(bool watches, const std::vector<GdbDebuggerVariableRow> &rows, bool positionsOnly) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	MRVMHashStore &store = kv.globalStore();
	if (rows.empty() && debuggerUiState() == 0) return;
	const VirtualMachine::Value root = mrvmMakeHash(debuggerUiState(true), true);
	const char *branch = watches ? "gdbWatches" : "gdbVariables";
	const VirtualMachine::Value entries = positionsOnly ? kv.ensureChild(root, branch) : kv.replaceChild(root, branch);
	store.write(entries.hashHandle, "count", mrvmMakeInt(static_cast<int>(rows.size())));
	for (std::size_t index = 0; index < rows.size(); ++index) {
		const VirtualMachine::Value item = kv.ensureChild(entries, std::to_string(index));
		const GdbDebuggerVariableRow &row = rows[index];
		store.write(item.hashHandle, "start", mrvmMakeString(std::to_string(row.start)));
		store.write(item.hashHandle, "end", mrvmMakeString(std::to_string(row.end)));
		store.write(item.hashHandle, "valueStart", mrvmMakeString(std::to_string(row.valueStart)));
		if (positionsOnly) continue;
		store.write(item.hashHandle, "arrayOwner", mrvmMakeString(std::to_string(row.arrayOwner)));
		if (row.byteArray) store.write(item.hashHandle, "byteArray", mrvmMakeInt(true));
		store.write(item.hashHandle, "label", mrvmMakeString(row.label));
		store.write(item.hashHandle, "expression", mrvmMakeString(row.expression));
		store.write(item.hashHandle, "objectName", mrvmMakeString(row.objectName));
		store.write(item.hashHandle, "value", mrvmMakeString(row.value));
		store.write(item.hashHandle, "changed", mrvmMakeInt(row.changed));
	}
}

void MRBentoBox::refreshGdbDebuggerValues(const MRGdbEvent &event) {
	const bool watches = event.kind == MRGdbEventKind::Watches;
	std::vector<GdbDebuggerVariableRow> rows;
	const std::vector<GdbDebuggerVariableRow> previous = readGdbDebuggerRows(watches);
	std::unordered_map<std::string, std::size_t> previousPaths;
	for (std::size_t i = 0; i < previous.size(); ++i) previousPaths.emplace(previous[i].expression, i);
	std::vector<std::size_t> parents;
	std::vector<bool> parentArrays;
	rows.reserve(event.variables.size());
	for (const MRGdbMiVariable &variable : event.variables) {
		const std::size_t depth = static_cast<std::size_t>(std::max(0, variable.depth));
		parents.resize(depth + 1, std::string::npos);
		parentArrays.resize(depth + 1, false);
		const std::size_t parent = depth > 0 ? parents[depth - 1] : std::string::npos;
		GdbDebuggerVariableRow row;
		row.start = row.end = row.valueStart = std::string::npos;
		row.byteArray = variable.childCount > 0 &&
		                (variable.type.starts_with("char [") || variable.type.starts_with("signed char [") ||
		                 variable.type.starts_with("unsigned char [") || variable.type.starts_with("const char [") ||
		                 variable.type.starts_with("const signed char [") || variable.type.starts_with("const unsigned char ["));
		row.arrayOwner = parent != std::string::npos && parentArrays[depth - 1] && variable.childCount == 0 &&
		                 !variable.value.starts_with("{") && (variable.type.empty() || variable.type.back() != ']') ? parent : std::string::npos;
		// GDB objects are transient; retain the thread/frame and expression identity.
		row.expression = parent == std::string::npos ? (event.text + ":" + (watches ? variable.identity : variable.name)) : rows[parent].expression + "/" + variable.name;
		row.objectName = variable.objectName;
		row.value = variable.value;
		if (row.arrayOwner != std::string::npos) {
			const std::size_t quote = row.value.find(" '");
			if (quote != std::string::npos && quote > 0 && row.value.find_first_not_of("-0123456789") == quote)
				row.value.resize(quote);
		}
		row.changed = false;
		if (row.arrayOwner == std::string::npos) {
			row.label.assign(depth * 2, ' ');
			if (watches && depth == 0) row.label += variable.identity + ": ";
			row.label += variable.name;
			if (!variable.type.empty()) row.label += " [" + variable.type + "]";
			row.label += " = ";
		}
		const auto found = previousPaths.find(row.expression);
		if (found != previousPaths.end()) {
			const GdbDebuggerVariableRow &old = previous[found->second];
			row.start = old.start;
			row.end = old.end;
			row.valueStart = old.valueStart;
			row.changed = old.value != row.value;
		}
		parents[depth] = rows.size();
		parentArrays[depth] = variable.childCount > 0 && !variable.type.empty() && variable.type.back() == ']';
		rows.push_back(std::move(row));
	}
	writeGdbDebuggerRows(watches, rows);
	layoutGdbDebuggerValues(watches, true);
}

void MRBentoBox::layoutGdbDebuggerValues(bool watches, bool valuesChanged) {
	MREditWindow *window = watches ? watchesPane() : variablesPane();
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	if (editor == nullptr) return;
	int &previousWidth = watches ? gdbDebuggerWatchesWidth : gdbDebuggerVariablesWidth;
	const TRect viewport = editor->visibleTextViewportBounds();
	const int width = std::max(1, viewport.b.x - viewport.a.x);
	if (!valuesChanged && previousWidth == width) return;
	std::vector<GdbDebuggerVariableRow> rows = readGdbDebuggerRows(watches);
	if (!valuesChanged && rows.empty()) return;
	previousWidth = width;
	std::vector<int> arrayValueWidths(rows.size(), 0);
	std::vector<std::size_t> arrayIndexWidths(rows.size(), 0);
	for (const GdbDebuggerVariableRow &row : rows) {
		if (row.arrayOwner == std::string::npos) continue;
		arrayValueWidths[row.arrayOwner] = std::max(arrayValueWidths[row.arrayOwner], strwidth(row.value.c_str()));
		const std::size_t index = row.expression.rfind('/');
		arrayIndexWidths[row.arrayOwner] = std::max(arrayIndexWidths[row.arrayOwner], row.expression.size() - index - 1);
	}
	for (std::size_t &indexWidth : arrayIndexWidths)
		if (indexWidth != 0) indexWidth = indexWidth * 2 + 5;
	const TPoint scroll = editor->delta;
	const std::size_t cursor = editor->cursorOffset();
	const std::size_t topOffset = editor->bufferModel().lineStartByIndex(static_cast<std::size_t>(std::max(0, scroll.y)));
	std::size_t cursorRow = 0;
	std::size_t topRow = 0;
	std::size_t cursorStart = 0;
	std::size_t topStart = 0;
	for (std::size_t i = 0; i < rows.size(); ++i) {
		if (rows[i].start <= cursor && rows[i].start >= cursorStart) { cursorRow = i; cursorStart = rows[i].start; }
		if (rows[i].start <= topOffset && rows[i].start >= topStart) { topRow = i; topStart = rows[i].start; }
	}
	const bool cursorInValue = !rows.empty() && rows[cursorRow].valueStart <= cursor;
	const std::size_t column = rows.empty() || rows[cursorRow].start == std::string::npos ? 0 : cursor - (cursorInValue ? rows[cursorRow].valueStart : rows[cursorRow].start);
	std::vector<std::pair<std::size_t, std::size_t>> changedRanges;
	std::string text;
	for (std::size_t i = 0; i < rows.size();) {
		GdbDebuggerVariableRow &row = rows[i];
		if (row.arrayOwner == std::string::npos) {
			row.start = text.size();
			text += row.label;
			row.valueStart = text.size();
			text += row.value;
			row.end = text.size();
			if (row.changed) changedRanges.emplace_back(row.valueStart, row.end);
			text += '\n';
			if (row.byteArray) {
				text += std::string(row.label.find_first_not_of(' ') + 2, ' ') + "Text: ";
				for (std::size_t member = i + 1; member < rows.size() && rows[member].arrayOwner == i; ++member) {
					const std::string &value = rows[member].value;
					char *end = nullptr;
					const long byte = std::strtol(value.c_str(), &end, 10);
					text += end != value.c_str() && *end == '\0' && byte >= 32 && byte <= 126 ? static_cast<char>(byte) : '.';
				}
				text += '\n';
			}
			++i;
			continue;
		}
		const std::size_t owner = row.arrayOwner;
		const std::size_t indent = rows[owner].label.find_first_not_of(' ') + 2;
		const std::string firstIndex = row.expression.substr(row.expression.rfind('/') + 1);
		std::size_t end = i;
		int valuesWidth = 0;
		while (end < rows.size() && rows[end].arrayOwner == owner) {
			const int nextWidth = valuesWidth + (end == i ? 0 : 2) + arrayValueWidths[owner];
			if (end != i && indent + arrayIndexWidths[owner] + nextWidth > static_cast<std::size_t>(width)) break;
			valuesWidth = nextWidth;
			++end;
		}
		const std::size_t lineStart = text.size();
		const std::string lastIndex = rows[end - 1].expression.substr(rows[end - 1].expression.rfind('/') + 1);
		const std::string range = "[" + firstIndex + (end == i + 1 ? "" : ".." + lastIndex) + "] ";
		text += std::string(indent, ' ') + range;
		text.append(arrayIndexWidths[owner] - range.size(), ' ');
		for (std::size_t member = i; member < end; ++member) {
			GdbDebuggerVariableRow &element = rows[member];
			if (member != i) text += "  ";
			element.start = member == i ? lineStart : text.size();
			text.append(arrayValueWidths[owner] - strwidth(element.value.c_str()), ' ');
			element.valueStart = text.size();
			text += element.value;
			element.end = text.size();
			if (element.changed) changedRanges.emplace_back(element.valueStart, element.end);
		}
		text += '\n';
		i = end;
	}
	if (rows.empty()) text = watches ? "(no watches)\n" : "(no variables in current frame)\n";
	if (editor->snapshotText() != text) {
		window->lock();
		static_cast<void>(window->replaceTextBuffer(text.c_str(), watches ? "Watches" : "Variables"));
		if (!rows.empty()) {
			const GdbDebuggerVariableRow &row = rows[cursorRow];
			const std::size_t start = cursorInValue ? row.valueStart : row.start;
			const std::size_t position = start + std::min(column, row.end - start);
			editor->setCursorOffsetAtVisualColumn(position, editor->actualCursorVisualColumn(position), true);
			editor->scrollTo(scroll.x, static_cast<int>(editor->lineIndexOfOffset(rows[topRow].start)));
		}
		window->unlock();
	}
	window->setReadOnly(true);
	window->setFileChanged(false);
	editor->setDebuggerVariableChangedRanges(changedRanges);
	writeGdbDebuggerRows(watches, rows, true);
}

bool MRBentoBox::showGdbDebuggerValueInputAtCursor() {
	MRPaneEditWindow *variablesWindow = dynamic_cast<MRPaneEditWindow *>(variablesPane());
	MRFileEditor *variablesEditor = variablesWindow != nullptr ? variablesWindow->getEditor() : nullptr;
	const std::size_t cursor = variablesEditor != nullptr ? variablesEditor->cursorOffset() : 0;

	if (debuggerValueInput != nullptr || variablesEditor == nullptr || !gdbDebuggerContextReady(true)) return false;
	for (const GdbDebuggerVariableRow &row : readGdbDebuggerRows(false)) {
		if (cursor < row.start || cursor >= row.end) continue;
		variablesEditor->setCursorOffset(row.valueStart);
		const TRect viewport = variablesEditor->visibleTextViewportBounds();
		const int left = viewport.a.x + variablesEditor->currentViewColumn() - 1;
		const int top = viewport.a.y + variablesEditor->currentViewRow() - 1;

		if (left >= viewport.b.x || top < viewport.a.y || top >= viewport.b.y) return false;
		debuggerValueInput = new MRDebuggerValueInput(TRect(left, top, viewport.b.x, top + 1), this);
		debuggerValueInputPane = variablesWindow;
		{
			std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
			MRVMRuntimeKv &kv = mrvmRuntimeKv();
			const VirtualMachine::Value input = kv.replaceChild(mrvmMakeHash(debuggerUiState(true), true), "valueInput");
			kv.globalStore().write(input.hashHandle, "objectName", mrvmMakeString(row.objectName));
			kv.globalStore().write(input.hashHandle, "threadId", mrvmMakeString(gdbThreadForRole(bprVariables)));
		}
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
	if (gdbDebuggerActive()) {
		std::string objectName, threadId;
		{
			std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
			MRVMRuntimeKv &kv = mrvmRuntimeKv();
			VirtualMachine::Value input;
			const int state = debuggerUiState();
			if (state != 0 && kv.findChild(mrvmMakeHash(state, true), "valueInput", input)) {
				objectName = kv.globalStore().read(input.hashHandle, "objectName").s;
				threadId = kv.globalStore().read(input.hashHandle, "threadId").s;
			}
		}
		if (objectName.empty() || threadId != gdbThreadForRole(bprVariables) || !sendGdbCommand(MRGdbCommandKind::AssignVariable, value.data(), objectName)) {
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
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	const int state = debuggerUiState();
	if (state != 0) mrvmRuntimeKv().eraseChild(mrvmMakeHash(state, true), "valueInput");
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
