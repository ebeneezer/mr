#include "MRVMModelessUiRuntime.hpp"

#include "MRVMHash.hpp"

#include "../mrmac.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <mutex>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace {
using Value = VirtualMachine::Value;

std::string upperKey(std::string value) {
	for (char &c : value)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return value;
}

std::string trimAscii(const std::string &value) {
	std::size_t begin = 0;
	std::size_t end = value.size();

	while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0)
		++begin;
	while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
		--end;
	return value.substr(begin, end - begin);
}

Value makeIntValue(int value) {
	Value result;
	result.type = TYPE_INT;
	result.i = value;
	return result;
}

Value makeStringValue(const std::string &value) {
	Value result;
	result.type = TYPE_STR;
	result.s = value;
	return result;
}

bool parseUint64Text(const std::string &text, std::uint64_t &value) {
	value = 0;
	if (text.empty()) return false;
	for (std::size_t index = 0; index < text.size(); ++index) {
		const unsigned char ch = static_cast<unsigned char>(text[index]);
		std::uint64_t digit;
		if (ch < static_cast<unsigned char>('0') || ch > static_cast<unsigned char>('9')) return false;
		digit = static_cast<std::uint64_t>(ch - static_cast<unsigned char>('0'));
		if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
		value = value * 10 + digit;
	}
	return true;
}

int valueAsInt(const Value &value, int fallback) {
	std::uint64_t parsed = 0;

	if (value.type == TYPE_INT) return value.i;
	if (value.type == TYPE_STR && parseUint64Text(value.s, parsed)) return static_cast<int>(parsed);
	return fallback;
}

std::string valueAsString(const Value &value) {
	if (value.type == TYPE_STR) return value.s;
	if (value.type == TYPE_INT) return std::to_string(value.i);
	return std::string();
}

void hashWriteInt(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	mrvmHashWriteValue(store, store, hash, key, makeIntValue(value));
}

void hashWriteString(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	mrvmHashWriteValue(store, store, hash, key, makeStringValue(value));
}

int hashReadInt(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, int fallback = 0) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, hash, key)) return fallback;
	return valueAsInt(mrvmHashReadValue(store, store, hash, key), fallback);
}

std::string hashReadString(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, hash, key)) return std::string();
	return valueAsString(mrvmHashReadValue(store, store, hash, key));
}

Value ensureModelessUiRoot(MRVMRuntimeKv &runtimeKv) {
	return runtimeKv.ensureRoot("MODELESSUI");
}

bool findModelessUiRoot(MRVMRuntimeKv &runtimeKv, Value &root) {
	return runtimeKv.findRoot("MODELESSUI", root);
}

Value ensureModelessUiChildPath(MRVMRuntimeKv &runtimeKv, std::initializer_list<const char *> keys) {
	Value current = ensureModelessUiRoot(runtimeKv);

	for (const char *key : keys)
		current = runtimeKv.ensureChild(current, key != nullptr ? key : "");
	return current;
}

bool findModelessUiChildPath(MRVMRuntimeKv &runtimeKv, std::initializer_list<const char *> keys, Value &child) {
	Value current;

	if (!findModelessUiRoot(runtimeKv, current)) return false;
	for (const char *key : keys) {
		if (!runtimeKv.findChild(current, key != nullptr ? key : "", child)) return false;
		current = child;
	}
	return true;
}

Value ensureCurrentDialog(MRVMRuntimeKv &runtimeKv) {
	return ensureModelessUiChildPath(runtimeKv, {"staging", "currentDialog"});
}

Value ensureDialogSection(MRVMRuntimeKv &runtimeKv, const char *sectionName) {
	return runtimeKv.ensureChild(ensureCurrentDialog(runtimeKv), sectionName != nullptr ? sectionName : "");
}

Value appendDialogSectionItem(MRVMRuntimeKv &runtimeKv, const char *sectionName) {
	Value section = ensureDialogSection(runtimeKv, sectionName);
	int count = hashReadInt(runtimeKv, section, "count", 0) + 1;

	hashWriteInt(runtimeKv, section, "count", count);
	return runtimeKv.replaceChild(section, std::to_string(count));
}

bool findStagingChild(MRVMRuntimeKv &runtimeKv, const std::string &key, Value &child) {
	return findModelessUiChildPath(runtimeKv, {"staging", key.c_str()}, child);
}

bool findCurrentDialog(MRVMRuntimeKv &runtimeKv, Value &dialog) {
	Value staging;

	if (!findStagingChild(runtimeKv, "currentDialog", staging)) return false;
	dialog = staging;
	return true;
}

void writeLabelHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MacroUiLabelSpec &label) {
	hashWriteInt(runtimeKv, hash, "x", label.x);
	hashWriteInt(runtimeKv, hash, "y", label.y);
	hashWriteString(runtimeKv, hash, "text", label.text);
}

void writeButtonHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MacroUiButtonSpec &button) {
	hashWriteInt(runtimeKv, hash, "x", button.x);
	hashWriteInt(runtimeKv, hash, "y", button.y);
	hashWriteInt(runtimeKv, hash, "width", button.width);
	hashWriteInt(runtimeKv, hash, "id", button.id);
	hashWriteString(runtimeKv, hash, "text", button.text);
}

void writeDisplayHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MacroUiDisplaySpec &display) {
	hashWriteInt(runtimeKv, hash, "x", display.x);
	hashWriteInt(runtimeKv, hash, "y", display.y);
	hashWriteInt(runtimeKv, hash, "width", display.width);
	hashWriteString(runtimeKv, hash, "text", display.text);
}

void writeInputHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MacroUiInputSpec &input) {
	hashWriteInt(runtimeKv, hash, "x", input.x);
	hashWriteInt(runtimeKv, hash, "y", input.y);
	hashWriteInt(runtimeKv, hash, "width", input.width);
	hashWriteInt(runtimeKv, hash, "id", input.id);
	hashWriteString(runtimeKv, hash, "label", input.label);
	hashWriteString(runtimeKv, hash, "text", input.text);
}

void writeListBoxHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MacroUiListBoxSpec &listBox) {
	hashWriteInt(runtimeKv, hash, "x", listBox.x);
	hashWriteInt(runtimeKv, hash, "y", listBox.y);
	hashWriteInt(runtimeKv, hash, "width", listBox.width);
	hashWriteInt(runtimeKv, hash, "height", listBox.height);
	hashWriteInt(runtimeKv, hash, "id", listBox.id);
	hashWriteString(runtimeKv, hash, "label", listBox.label);
	hashWriteString(runtimeKv, hash, "itemSpec", listBox.itemSpec);
	hashWriteInt(runtimeKv, hash, "start", listBox.start);
}

void writeGridHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MacroUiGridSpec &grid) {
	hashWriteInt(runtimeKv, hash, "x", grid.x);
	hashWriteInt(runtimeKv, hash, "y", grid.y);
	hashWriteInt(runtimeKv, hash, "width", grid.width);
	hashWriteInt(runtimeKv, hash, "height", grid.height);
	hashWriteInt(runtimeKv, hash, "id", grid.id);
	hashWriteString(runtimeKv, hash, "label", grid.label);
	hashWriteString(runtimeKv, hash, "itemSpec", grid.itemSpec);
	hashWriteInt(runtimeKv, hash, "start", grid.start);
}

MacroUiLabelSpec readLabelHash(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	MacroUiLabelSpec label;

	label.x = hashReadInt(runtimeKv, hash, "x", 0);
	label.y = hashReadInt(runtimeKv, hash, "y", 0);
	label.text = hashReadString(runtimeKv, hash, "text");
	return label;
}

MacroUiButtonSpec readButtonHash(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	MacroUiButtonSpec button;

	button.x = hashReadInt(runtimeKv, hash, "x", 0);
	button.y = hashReadInt(runtimeKv, hash, "y", 0);
	button.width = hashReadInt(runtimeKv, hash, "width", 8);
	button.id = hashReadInt(runtimeKv, hash, "id", 0);
	button.text = hashReadString(runtimeKv, hash, "text");
	return button;
}

MacroUiDisplaySpec readDisplayHash(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	MacroUiDisplaySpec display;

	display.x = hashReadInt(runtimeKv, hash, "x", 0);
	display.y = hashReadInt(runtimeKv, hash, "y", 0);
	display.width = hashReadInt(runtimeKv, hash, "width", 20);
	display.text = hashReadString(runtimeKv, hash, "text");
	return display;
}

MacroUiInputSpec readInputHash(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	MacroUiInputSpec input;

	input.x = hashReadInt(runtimeKv, hash, "x", 0);
	input.y = hashReadInt(runtimeKv, hash, "y", 0);
	input.width = hashReadInt(runtimeKv, hash, "width", 20);
	input.id = hashReadInt(runtimeKv, hash, "id", 0);
	input.label = hashReadString(runtimeKv, hash, "label");
	input.text = hashReadString(runtimeKv, hash, "text");
	return input;
}

MacroUiListBoxSpec readListBoxHash(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	MacroUiListBoxSpec listBox;

	listBox.x = hashReadInt(runtimeKv, hash, "x", 0);
	listBox.y = hashReadInt(runtimeKv, hash, "y", 0);
	listBox.width = hashReadInt(runtimeKv, hash, "width", 20);
	listBox.height = hashReadInt(runtimeKv, hash, "height", 4);
	listBox.id = hashReadInt(runtimeKv, hash, "id", 0);
	listBox.label = hashReadString(runtimeKv, hash, "label");
	listBox.itemSpec = hashReadString(runtimeKv, hash, "itemSpec");
	listBox.start = hashReadInt(runtimeKv, hash, "start", 1);
	return listBox;
}

MacroUiGridSpec readGridHash(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	MacroUiGridSpec grid;

	grid.x = hashReadInt(runtimeKv, hash, "x", 0);
	grid.y = hashReadInt(runtimeKv, hash, "y", 0);
	grid.width = hashReadInt(runtimeKv, hash, "width", 20);
	grid.height = hashReadInt(runtimeKv, hash, "height", 4);
	grid.id = hashReadInt(runtimeKv, hash, "id", 0);
	grid.label = hashReadString(runtimeKv, hash, "label");
	grid.itemSpec = hashReadString(runtimeKv, hash, "itemSpec");
	grid.start = hashReadInt(runtimeKv, hash, "start", 1);
	return grid;
}

void readLabelSection(MRVMRuntimeKv &runtimeKv, const Value &dialog, const char *sectionName, std::vector<MacroUiLabelSpec> &items) {
	Value section;

	if (!runtimeKv.findChild(dialog, sectionName, section)) return;
	for (int index = 1; index <= hashReadInt(runtimeKv, section, "count", 0); ++index) {
		Value item;
		if (runtimeKv.findChild(section, std::to_string(index), item)) items.push_back(readLabelHash(runtimeKv, item));
	}
}

void readButtonSection(MRVMRuntimeKv &runtimeKv, const Value &dialog, const char *sectionName, std::vector<MacroUiButtonSpec> &items) {
	Value section;

	if (!runtimeKv.findChild(dialog, sectionName, section)) return;
	for (int index = 1; index <= hashReadInt(runtimeKv, section, "count", 0); ++index) {
		Value item;
		if (runtimeKv.findChild(section, std::to_string(index), item)) items.push_back(readButtonHash(runtimeKv, item));
	}
}

void readDisplaySection(MRVMRuntimeKv &runtimeKv, const Value &dialog, const char *sectionName, std::vector<MacroUiDisplaySpec> &items) {
	Value section;

	if (!runtimeKv.findChild(dialog, sectionName, section)) return;
	for (int index = 1; index <= hashReadInt(runtimeKv, section, "count", 0); ++index) {
		Value item;
		if (runtimeKv.findChild(section, std::to_string(index), item)) items.push_back(readDisplayHash(runtimeKv, item));
	}
}

void readInputSection(MRVMRuntimeKv &runtimeKv, const Value &dialog, const char *sectionName, std::vector<MacroUiInputSpec> &items) {
	Value section;

	if (!runtimeKv.findChild(dialog, sectionName, section)) return;
	for (int index = 1; index <= hashReadInt(runtimeKv, section, "count", 0); ++index) {
		Value item;
		if (runtimeKv.findChild(section, std::to_string(index), item)) items.push_back(readInputHash(runtimeKv, item));
	}
}

void readListBoxSection(MRVMRuntimeKv &runtimeKv, const Value &dialog, const char *sectionName, std::vector<MacroUiListBoxSpec> &items) {
	Value section;

	if (!runtimeKv.findChild(dialog, sectionName, section)) return;
	for (int index = 1; index <= hashReadInt(runtimeKv, section, "count", 0); ++index) {
		Value item;
		if (runtimeKv.findChild(section, std::to_string(index), item)) items.push_back(readListBoxHash(runtimeKv, item));
	}
}

void readGridSection(MRVMRuntimeKv &runtimeKv, const Value &dialog, const char *sectionName, std::vector<MacroUiGridSpec> &items) {
	Value section;

	if (!runtimeKv.findChild(dialog, sectionName, section)) return;
	for (int index = 1; index <= hashReadInt(runtimeKv, section, "count", 0); ++index) {
		Value item;
		if (runtimeKv.findChild(section, std::to_string(index), item)) items.push_back(readGridHash(runtimeKv, item));
	}
}

void writeModelessLabelHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroModelessLabelSpec &label) {
	hashWriteInt(runtimeKv, hash, "x", label.x);
	hashWriteInt(runtimeKv, hash, "y", label.y);
	hashWriteString(runtimeKv, hash, "text", label.text);
}

void writeModelessDisplayHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroModelessDisplaySpec &display) {
	hashWriteInt(runtimeKv, hash, "x", display.x);
	hashWriteInt(runtimeKv, hash, "y", display.y);
	hashWriteInt(runtimeKv, hash, "width", display.width);
	hashWriteString(runtimeKv, hash, "text", display.text);
}

void writeModelessButtonHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroModelessButtonSpec &button) {
	hashWriteInt(runtimeKv, hash, "x", button.x);
	hashWriteInt(runtimeKv, hash, "y", button.y);
	hashWriteInt(runtimeKv, hash, "width", button.width);
	hashWriteInt(runtimeKv, hash, "id", button.id);
	hashWriteString(runtimeKv, hash, "text", button.text);
	hashWriteString(runtimeKv, hash, "macroSpec", button.macroSpec);
}

void writeModelessListBoxHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroModelessListBoxSpec &listBox) {
	hashWriteInt(runtimeKv, hash, "x", listBox.x);
	hashWriteInt(runtimeKv, hash, "y", listBox.y);
	hashWriteInt(runtimeKv, hash, "width", listBox.width);
	hashWriteInt(runtimeKv, hash, "height", listBox.height);
	hashWriteInt(runtimeKv, hash, "id", listBox.id);
	hashWriteString(runtimeKv, hash, "label", listBox.label);
	hashWriteString(runtimeKv, hash, "itemSpec", listBox.itemSpec);
	hashWriteInt(runtimeKv, hash, "start", listBox.start);
}

void writeModelessGridHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroModelessGridSpec &grid) {
	hashWriteInt(runtimeKv, hash, "x", grid.x);
	hashWriteInt(runtimeKv, hash, "y", grid.y);
	hashWriteInt(runtimeKv, hash, "width", grid.width);
	hashWriteInt(runtimeKv, hash, "height", grid.height);
	hashWriteInt(runtimeKv, hash, "id", grid.id);
	hashWriteString(runtimeKv, hash, "label", grid.label);
	hashWriteString(runtimeKv, hash, "itemSpec", grid.itemSpec);
	hashWriteString(runtimeKv, hash, "macroSpec", grid.macroSpec);
	hashWriteInt(runtimeKv, hash, "start", grid.start);
}
} // namespace

std::string mrvmModelessUiListKey(const std::string &name) {
	return upperKey(trimAscii(name));
}

void mrvmModelessUiBeginDialog(MRVMRuntimeKv &runtimeKv, int x, int y, int width, int height, const std::string &title) {
	Value staging = ensureModelessUiChildPath(runtimeKv, {"staging"});
	Value dialog = runtimeKv.replaceChild(staging, "currentDialog");

	hashWriteInt(runtimeKv, dialog, "x", x);
	hashWriteInt(runtimeKv, dialog, "y", y);
	hashWriteInt(runtimeKv, dialog, "width", width);
	hashWriteInt(runtimeKv, dialog, "height", height);
	hashWriteString(runtimeKv, dialog, "title", title);
}

void mrvmModelessUiWriteTextValue(MRVMRuntimeKv &runtimeKv, int id, const std::string &text) {
	if (id <= 0) return;
	hashWriteString(runtimeKv, ensureDialogSection(runtimeKv, "textValues"), std::to_string(id), text);
}

void mrvmModelessUiWriteIndexValue(MRVMRuntimeKv &runtimeKv, int id, int index) {
	if (id <= 0) return;
	hashWriteInt(runtimeKv, ensureDialogSection(runtimeKv, "indexValues"), std::to_string(id), index);
}

std::string mrvmModelessUiReadTextValue(MRVMRuntimeKv &runtimeKv, int id) {
	Value dialog;
	Value values;

	if (id <= 0 || !findCurrentDialog(runtimeKv, dialog) || !runtimeKv.findChild(dialog, "textValues", values)) return std::string();
	return hashReadString(runtimeKv, values, std::to_string(id));
}

int mrvmModelessUiReadIndexValue(MRVMRuntimeKv &runtimeKv, int id) {
	Value dialog;
	Value values;

	if (id <= 0 || !findCurrentDialog(runtimeKv, dialog) || !runtimeKv.findChild(dialog, "indexValues", values)) return 0;
	return hashReadInt(runtimeKv, values, std::to_string(id), 0);
}

void mrvmModelessUiAppendLabel(MRVMRuntimeKv &runtimeKv, const MacroUiLabelSpec &label) {
	writeLabelHash(runtimeKv, appendDialogSectionItem(runtimeKv, "labels"), label);
}

void mrvmModelessUiAppendButton(MRVMRuntimeKv &runtimeKv, const MacroUiButtonSpec &button) {
	writeButtonHash(runtimeKv, appendDialogSectionItem(runtimeKv, "buttons"), button);
}

void mrvmModelessUiAppendDisplay(MRVMRuntimeKv &runtimeKv, const MacroUiDisplaySpec &display) {
	writeDisplayHash(runtimeKv, appendDialogSectionItem(runtimeKv, "displays"), display);
}

void mrvmModelessUiAppendInput(MRVMRuntimeKv &runtimeKv, const MacroUiInputSpec &input) {
	mrvmModelessUiWriteTextValue(runtimeKv, input.id, input.text);
	writeInputHash(runtimeKv, appendDialogSectionItem(runtimeKv, "inputs"), input);
}

void mrvmModelessUiAppendListBox(MRVMRuntimeKv &runtimeKv, const MacroUiListBoxSpec &listBox) {
	writeListBoxHash(runtimeKv, appendDialogSectionItem(runtimeKv, "listBoxes"), listBox);
}

void mrvmModelessUiAppendGrid(MRVMRuntimeKv &runtimeKv, const MacroUiGridSpec &grid) {
	writeGridHash(runtimeKv, appendDialogSectionItem(runtimeKv, "grids"), grid);
}

MacroUiDialogDefinition mrvmModelessUiReadDialogDefinition(MRVMRuntimeKv &runtimeKv) {
	MacroUiDialogDefinition definition;
	Value dialog;
	Value macros;

	if (!findCurrentDialog(runtimeKv, dialog)) return definition;
	definition.x = hashReadInt(runtimeKv, dialog, "x", 0);
	definition.y = hashReadInt(runtimeKv, dialog, "y", 0);
	definition.width = hashReadInt(runtimeKv, dialog, "width", 40);
	definition.height = hashReadInt(runtimeKv, dialog, "height", 12);
	definition.title = hashReadString(runtimeKv, dialog, "title");
	readLabelSection(runtimeKv, dialog, "labels", definition.labels);
	readButtonSection(runtimeKv, dialog, "buttons", definition.buttons);
	readDisplaySection(runtimeKv, dialog, "displays", definition.displays);
	readInputSection(runtimeKv, dialog, "inputs", definition.inputs);
	readListBoxSection(runtimeKv, dialog, "listBoxes", definition.listBoxes);
	readGridSection(runtimeKv, dialog, "grids", definition.grids);
	if (runtimeKv.findChild(dialog, "modelessButtonMacros", macros)) {
		for (int index = 1; index <= hashReadInt(runtimeKv, macros, "count", 0); ++index) {
			Value item;
			if (!runtimeKv.findChild(macros, std::to_string(index), item)) continue;
			definition.modelessButtonMacros[hashReadInt(runtimeKv, item, "id", 0)] = hashReadString(runtimeKv, item, "macroSpec");
		}
	}
	return definition;
}

void mrvmModelessUiWriteModelessMacro(MRVMRuntimeKv &runtimeKv, int controlId, const std::string &macroSpec) {
	Value macros;
	Value item;
	int count = 0;

	if (controlId <= 0) return;
	macros = ensureDialogSection(runtimeKv, "modelessButtonMacros");
	count = hashReadInt(runtimeKv, macros, "count", 0);
	for (int index = 1; index <= count; ++index) {
		if (!runtimeKv.findChild(macros, std::to_string(index), item)) continue;
		if (hashReadInt(runtimeKv, item, "id", 0) == controlId) {
			hashWriteString(runtimeKv, item, "macroSpec", macroSpec);
			return;
		}
	}
	++count;
	hashWriteInt(runtimeKv, macros, "count", count);
	item = runtimeKv.replaceChild(macros, std::to_string(count));
	hashWriteInt(runtimeKv, item, "id", controlId);
	hashWriteString(runtimeKv, item, "macroSpec", macroSpec);
}

bool mrvmModelessUiReadItemList(MRVMRuntimeKv &runtimeKv, const std::string &key, std::vector<std::string> &values) {
	Value lists;
	Value list;

	values.clear();
	if (!findStagingChild(runtimeKv, "itemLists", lists)) return false;
	if (!runtimeKv.findChild(lists, key, list)) return false;
	for (int index = 1; index <= hashReadInt(runtimeKv, list, "count", 0); ++index)
		values.push_back(hashReadString(runtimeKv, list, std::to_string(index)));
	return true;
}

void mrvmModelessUiClearItemList(MRVMRuntimeKv &runtimeKv, const std::string &key) {
	Value itemLists = ensureModelessUiChildPath(runtimeKv, {"staging", "itemLists"});
	Value list = runtimeKv.replaceChild(itemLists, key);

	hashWriteInt(runtimeKv, list, "count", 0);
}

void mrvmModelessUiAddItemListValue(MRVMRuntimeKv &runtimeKv, const std::string &key, const std::string &value) {
	Value list = runtimeKv.ensureChild(ensureModelessUiChildPath(runtimeKv, {"staging", "itemLists"}), key);
	int count = hashReadInt(runtimeKv, list, "count", 0) + 1;

	hashWriteInt(runtimeKv, list, "count", count);
	hashWriteString(runtimeKv, list, std::to_string(count), value);
}

void mrvmModelessUiStoreWindowDefinition(MRVMRuntimeKv &runtimeKv, const MRMacroModelessWindowDefinition &definition) {
	Value windows = ensureModelessUiChildPath(runtimeKv, {"windows"});
	Value window;
	Value previousWindow;
	Value previousLiveGeometry;
	Value labels;
	Value displays;
	Value buttons;
	Value listBoxes;
	Value grids;
	bool hasLiveGeometry = false;
	int liveX = 0;
	int liveY = 0;
	int liveWidth = 0;
	int liveHeight = 0;
	int liveGeometryVersion = 0;
	int index = 0;

	if (definition.windowId.empty()) return;
	if (runtimeKv.findChild(windows, definition.windowId, previousWindow) && runtimeKv.findChild(previousWindow, "liveGeometry", previousLiveGeometry)) {
		hasLiveGeometry = hashReadInt(runtimeKv, previousLiveGeometry, "geometryVersion", 0) > 0;
		liveX = hashReadInt(runtimeKv, previousLiveGeometry, "x", 0);
		liveY = hashReadInt(runtimeKv, previousLiveGeometry, "y", 0);
		liveWidth = hashReadInt(runtimeKv, previousLiveGeometry, "width", 0);
		liveHeight = hashReadInt(runtimeKv, previousLiveGeometry, "height", 0);
		liveGeometryVersion = hashReadInt(runtimeKv, previousLiveGeometry, "geometryVersion", 0);
	}
	window = runtimeKv.replaceChild(windows, definition.windowId);
	hashWriteString(runtimeKv, window, "windowId", definition.windowId);
	hashWriteString(runtimeKv, window, "title", definition.title);
	hashWriteInt(runtimeKv, window, "x", definition.x);
	hashWriteInt(runtimeKv, window, "y", definition.y);
	hashWriteInt(runtimeKv, window, "width", definition.width);
	hashWriteInt(runtimeKv, window, "height", definition.height);

	if (hasLiveGeometry) {
		Value liveGeometry = runtimeKv.ensureChild(window, "liveGeometry");
		hashWriteInt(runtimeKv, liveGeometry, "x", liveX);
		hashWriteInt(runtimeKv, liveGeometry, "y", liveY);
		hashWriteInt(runtimeKv, liveGeometry, "width", liveWidth);
		hashWriteInt(runtimeKv, liveGeometry, "height", liveHeight);
		hashWriteInt(runtimeKv, liveGeometry, "geometryVersion", liveGeometryVersion);
	}

	labels = runtimeKv.ensureChild(window, "labels");
	for (std::size_t labelIndex = 0; labelIndex < definition.labels.size(); ++labelIndex) {
		++index;
		writeModelessLabelHash(runtimeKv, runtimeKv.replaceChild(labels, std::to_string(index)), definition.labels[labelIndex]);
	}
	hashWriteInt(runtimeKv, labels, "count", index);

	index = 0;
	displays = runtimeKv.ensureChild(window, "displays");
	for (std::size_t displayIndex = 0; displayIndex < definition.displays.size(); ++displayIndex) {
		++index;
		writeModelessDisplayHash(runtimeKv, runtimeKv.replaceChild(displays, std::to_string(index)), definition.displays[displayIndex]);
	}
	hashWriteInt(runtimeKv, displays, "count", index);

	index = 0;
	buttons = runtimeKv.ensureChild(window, "buttons");
	for (std::size_t buttonIndex = 0; buttonIndex < definition.buttons.size(); ++buttonIndex) {
		++index;
		writeModelessButtonHash(runtimeKv, runtimeKv.replaceChild(buttons, std::to_string(index)), definition.buttons[buttonIndex]);
	}
	hashWriteInt(runtimeKv, buttons, "count", index);

	index = 0;
	listBoxes = runtimeKv.ensureChild(window, "listBoxes");
	for (std::size_t listBoxIndex = 0; listBoxIndex < definition.listBoxes.size(); ++listBoxIndex) {
		++index;
		writeModelessListBoxHash(runtimeKv, runtimeKv.replaceChild(listBoxes, std::to_string(index)), definition.listBoxes[listBoxIndex]);
	}
	hashWriteInt(runtimeKv, listBoxes, "count", index);

	index = 0;
	grids = runtimeKv.ensureChild(window, "grids");
	for (std::size_t gridIndex = 0; gridIndex < definition.grids.size(); ++gridIndex) {
		++index;
		writeModelessGridHash(runtimeKv, runtimeKv.replaceChild(grids, std::to_string(index)), definition.grids[gridIndex]);
	}
	hashWriteInt(runtimeKv, grids, "count", index);
}

bool mrvmModelessUiStoreWindowDisplay(MRVMRuntimeKv &runtimeKv, const std::string &windowId, int displayIndex, const std::string &text) {
	Value windows;
	Value window;
	Value displays;
	Value display;

	if (windowId.empty() || displayIndex <= 0) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "displays", displays)) return false;
	if (!runtimeKv.findChild(displays, std::to_string(displayIndex), display)) return false;
	if (hashReadString(runtimeKv, display, "text") == text) return true;
	hashWriteString(runtimeKv, display, "text", text);
	return true;
}

void mrvmModelessUiStoreWindowLiveGeometry(MRVMRuntimeKv &runtimeKv, const std::string &windowId, int x, int y, int width, int height) {
	Value windows;
	Value window;
	Value liveGeometry;
	int version = 0;

	if (windowId.empty()) return;
	windows = ensureModelessUiChildPath(runtimeKv, {"windows"});
	window = runtimeKv.ensureChild(windows, windowId);
	liveGeometry = runtimeKv.ensureChild(window, "liveGeometry");

	if (hashReadInt(runtimeKv, liveGeometry, "x", x) == x && hashReadInt(runtimeKv, liveGeometry, "y", y) == y && hashReadInt(runtimeKv, liveGeometry, "width", width) == width && hashReadInt(runtimeKv, liveGeometry, "height", height) == height && hashReadInt(runtimeKv, liveGeometry, "geometryVersion", 0) > 0) return;
	version = hashReadInt(runtimeKv, liveGeometry, "geometryVersion", 0) + 1;
	hashWriteInt(runtimeKv, liveGeometry, "x", x);
	hashWriteInt(runtimeKv, liveGeometry, "y", y);
	hashWriteInt(runtimeKv, liveGeometry, "width", width);
	hashWriteInt(runtimeKv, liveGeometry, "height", height);
	hashWriteInt(runtimeKv, liveGeometry, "geometryVersion", version);
}

void mrvmModelessUiRemoveWindowDefinition(MRVMRuntimeKv &runtimeKv, const std::string &windowId) {
	Value windows;

	if (windowId.empty()) return;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return;
	static_cast<void>(runtimeKv.eraseChild(windows, windowId));
}

void mrvmStoreModelessWindowDefinition(const MRMacroModelessWindowDefinition &definition) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	mrvmModelessUiStoreWindowDefinition(mrvmRuntimeKv(), definition);
}

bool mrvmStoreModelessWindowDisplay(const std::string &windowId, int displayIndex, const std::string &text) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiStoreWindowDisplay(mrvmRuntimeKv(), windowId, displayIndex, text);
}

void mrvmStoreModelessWindowLiveGeometry(const std::string &windowId, int x, int y, int width, int height) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	mrvmModelessUiStoreWindowLiveGeometry(mrvmRuntimeKv(), windowId, x, y, width, height);
}

void mrvmRemoveModelessWindowDefinition(const std::string &windowId) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	mrvmModelessUiRemoveWindowDefinition(mrvmRuntimeKv(), windowId);
}
