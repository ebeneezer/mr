#include "MRVMModelessUiStorage.hpp"

#include "../../vm/MRVMHash.hpp"

#include "../../mrmac.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <limits>

namespace mr {
namespace modelessui {
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

int hashReadInt(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, int fallback) {
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

void writeCanvasHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MacroUiCanvasSpec &canvas) {
	hashWriteInt(runtimeKv, hash, "x", canvas.x);
	hashWriteInt(runtimeKv, hash, "y", canvas.y);
	hashWriteInt(runtimeKv, hash, "width", canvas.width);
	hashWriteInt(runtimeKv, hash, "height", canvas.height);
	hashWriteString(runtimeKv, hash, "canvasId", canvas.canvasId);
}

void writeCanvasHotspotHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MacroUiCanvasHotspotSpec &hotspot) {
	hashWriteString(runtimeKv, hash, "canvasId", hotspot.canvasId);
	hashWriteInt(runtimeKv, hash, "x", hotspot.x);
	hashWriteInt(runtimeKv, hash, "y", hotspot.y);
	hashWriteInt(runtimeKv, hash, "width", hotspot.width);
	hashWriteInt(runtimeKv, hash, "height", hotspot.height);
	hashWriteInt(runtimeKv, hash, "id", hotspot.id);
	hashWriteString(runtimeKv, hash, "macroSpec", hotspot.macroSpec);
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

MacroUiCanvasSpec readCanvasHash(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	MacroUiCanvasSpec canvas;

	canvas.x = hashReadInt(runtimeKv, hash, "x", 0);
	canvas.y = hashReadInt(runtimeKv, hash, "y", 0);
	canvas.width = hashReadInt(runtimeKv, hash, "width", 20);
	canvas.height = hashReadInt(runtimeKv, hash, "height", 6);
	canvas.canvasId = hashReadString(runtimeKv, hash, "canvasId");
	return canvas;
}

MacroUiCanvasHotspotSpec readCanvasHotspotHash(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	MacroUiCanvasHotspotSpec hotspot;

	hotspot.canvasId = hashReadString(runtimeKv, hash, "canvasId");
	hotspot.x = hashReadInt(runtimeKv, hash, "x", 0);
	hotspot.y = hashReadInt(runtimeKv, hash, "y", 0);
	hotspot.width = hashReadInt(runtimeKv, hash, "width", 1);
	hotspot.height = hashReadInt(runtimeKv, hash, "height", 1);
	hotspot.id = hashReadInt(runtimeKv, hash, "id", 0);
	hotspot.macroSpec = hashReadString(runtimeKv, hash, "macroSpec");
	return hotspot;
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

void readCanvasSection(MRVMRuntimeKv &runtimeKv, const Value &dialog, const char *sectionName, std::vector<MacroUiCanvasSpec> &items) {
	Value section;

	if (!runtimeKv.findChild(dialog, sectionName, section)) return;
	for (int index = 1; index <= hashReadInt(runtimeKv, section, "count", 0); ++index) {
		Value item;
		if (runtimeKv.findChild(section, std::to_string(index), item)) items.push_back(readCanvasHash(runtimeKv, item));
	}
}

void readCanvasHotspotSection(MRVMRuntimeKv &runtimeKv, const Value &dialog, const char *sectionName, std::vector<MacroUiCanvasHotspotSpec> &items) {
	Value section;

	if (!runtimeKv.findChild(dialog, sectionName, section)) return;
	for (int index = 1; index <= hashReadInt(runtimeKv, section, "count", 0); ++index) {
		Value item;
		if (runtimeKv.findChild(section, std::to_string(index), item)) items.push_back(readCanvasHotspotHash(runtimeKv, item));
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

void writeModelessCanvasHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroModelessCanvasSpec &canvas) {
	hashWriteInt(runtimeKv, hash, "x", canvas.x);
	hashWriteInt(runtimeKv, hash, "y", canvas.y);
	hashWriteInt(runtimeKv, hash, "width", canvas.width);
	hashWriteInt(runtimeKv, hash, "height", canvas.height);
	hashWriteString(runtimeKv, hash, "canvasId", canvas.canvasId);
}

void writeModelessCanvasHotspotHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroModelessCanvasHotspotSpec &hotspot) {
	hashWriteString(runtimeKv, hash, "canvasId", hotspot.canvasId);
	hashWriteInt(runtimeKv, hash, "x", hotspot.x);
	hashWriteInt(runtimeKv, hash, "y", hotspot.y);
	hashWriteInt(runtimeKv, hash, "width", hotspot.width);
	hashWriteInt(runtimeKv, hash, "height", hotspot.height);
	hashWriteInt(runtimeKv, hash, "id", hotspot.id);
	hashWriteString(runtimeKv, hash, "macroSpec", hotspot.macroSpec);
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

bool findModelessCanvas(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &canvasId, Value &canvas) {
	Value windows;
	Value window;
	Value canvases;

	if (windowId.empty() || canvasId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "canvases", canvases)) return false;
	return runtimeKv.findChild(canvases, canvasId, canvas);
}

void writeCanvasCommand(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroModelessCanvasCommand &command) {
	hashWriteInt(runtimeKv, hash, "type", static_cast<int>(command.type));
	hashWriteInt(runtimeKv, hash, "x", command.x);
	hashWriteInt(runtimeKv, hash, "y", command.y);
	hashWriteInt(runtimeKv, hash, "width", command.width);
	hashWriteInt(runtimeKv, hash, "height", command.height);
	hashWriteInt(runtimeKv, hash, "x2", command.x2);
	hashWriteInt(runtimeKv, hash, "y2", command.y2);
	hashWriteInt(runtimeKv, hash, "style", command.style);
	hashWriteString(runtimeKv, hash, "text", command.text);
}

MRMacroModelessCanvasCommand readCanvasCommand(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	MRMacroModelessCanvasCommand command;

	command.type = static_cast<MRMacroModelessCanvasCommandType>(hashReadInt(runtimeKv, hash, "type", static_cast<int>(MRMacroModelessCanvasCommandType::Clear)));
	command.x = hashReadInt(runtimeKv, hash, "x", 0);
	command.y = hashReadInt(runtimeKv, hash, "y", 0);
	command.width = hashReadInt(runtimeKv, hash, "width", 0);
	command.height = hashReadInt(runtimeKv, hash, "height", 0);
	command.x2 = hashReadInt(runtimeKv, hash, "x2", 0);
	command.y2 = hashReadInt(runtimeKv, hash, "y2", 0);
	command.style = hashReadInt(runtimeKv, hash, "style", 0);
	command.text = hashReadString(runtimeKv, hash, "text");
	return command;
}
} // namespace modelessui
} // namespace mr
