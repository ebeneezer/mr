#include "MRVMModelessUiStorage.hpp"

#include <algorithm>
#include <mutex>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace {
using Value = VirtualMachine::Value;
constexpr int kMaximumCanvasCommandCount = 2048;

bool findModelessWindowControlState(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const char *kind, int controlId, Value &state, bool create) {
	Value windows;
	Value window;
	Value controls;

	if (windowId.empty() || kind == nullptr || controlId <= 0) return false;
	if (create) {
		windows = mr::modelessui::ensureModelessUiChildPath(runtimeKv, {"windows"});
		window = runtimeKv.ensureChild(windows, windowId);
		controls = runtimeKv.ensureChild(window, kind);
		state = runtimeKv.ensureChild(controls, std::to_string(controlId));
		return true;
	}
	if (!mr::modelessui::findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, kind, controls)) return false;
	return runtimeKv.findChild(controls, std::to_string(controlId), state);
}
}

using namespace mr::modelessui;

void mrvmModelessUiStoreWindowDefinition(MRVMRuntimeKv &runtimeKv, const MRMacroModelessWindowDefinition &definition) {
	Value windows = ensureModelessUiChildPath(runtimeKv, {"windows"});
	Value window;
	Value labels;
	Value displays;
	Value statusFields;
	Value textFields;
	Value boolFields;
	Value intFields;
	Value progressFields;
	Value logFields;
	Value selectFields;
	Value canvases;
	Value canvasHotspots;
	Value buttons;
	Value listBoxes;
	Value grids;
	Value trees;
	Value tables;
	int index = 0;

	if (definition.windowId.empty()) return;
	window = runtimeKv.ensureChild(windows, definition.windowId);
	hashWriteString(runtimeKv, window, "windowId", definition.windowId);
	hashWriteString(runtimeKv, window, "title", definition.title);
	hashWriteInt(runtimeKv, window, "x", definition.x);
	hashWriteInt(runtimeKv, window, "y", definition.y);
	hashWriteInt(runtimeKv, window, "width", definition.width);
	hashWriteInt(runtimeKv, window, "height", definition.height);

	labels = runtimeKv.replaceChild(window, "labels");
	for (std::size_t labelIndex = 0; labelIndex < definition.labels.size(); ++labelIndex) {
		++index;
		writeModelessLabelHash(runtimeKv, runtimeKv.replaceChild(labels, std::to_string(index)), definition.labels[labelIndex]);
	}
	hashWriteInt(runtimeKv, labels, "count", index);

	index = 0;
	displays = runtimeKv.replaceChild(window, "displays");
	for (std::size_t displayIndex = 0; displayIndex < definition.displays.size(); ++displayIndex) {
		++index;
		writeModelessDisplayHash(runtimeKv, runtimeKv.replaceChild(displays, std::to_string(index)), definition.displays[displayIndex]);
	}
	hashWriteInt(runtimeKv, displays, "count", index);

	textFields = runtimeKv.replaceChild(window, "textFields");
	for (std::size_t textFieldIndex = 0; textFieldIndex < definition.textFields.size(); ++textFieldIndex) {
		const MRMacroModelessTextFieldSpec &textField = definition.textFields[textFieldIndex];
		Value field;

		if (textField.fieldId.empty()) continue;
		field = runtimeKv.replaceChild(textFields, textField.fieldId);
		hashWriteInt(runtimeKv, field, "x", textField.x);
		hashWriteInt(runtimeKv, field, "y", textField.y);
		hashWriteInt(runtimeKv, field, "width", textField.width);
		hashWriteString(runtimeKv, field, "label", textField.label);
		hashWriteString(runtimeKv, field, "text", textField.text);
	}

	boolFields = runtimeKv.replaceChild(window, "boolFields");
	for (std::size_t boolFieldIndex = 0; boolFieldIndex < definition.boolFields.size(); ++boolFieldIndex) {
		const MRMacroModelessBoolFieldSpec &boolField = definition.boolFields[boolFieldIndex];
		Value field;

		if (boolField.fieldId.empty()) continue;
		field = runtimeKv.replaceChild(boolFields, boolField.fieldId);
		hashWriteInt(runtimeKv, field, "x", boolField.x);
		hashWriteInt(runtimeKv, field, "y", boolField.y);
		hashWriteString(runtimeKv, field, "caption", boolField.caption);
		hashWriteInt(runtimeKv, field, "value", boolField.value ? 1 : 0);
	}

	intFields = runtimeKv.replaceChild(window, "intFields");
	for (std::size_t intFieldIndex = 0; intFieldIndex < definition.intFields.size(); ++intFieldIndex) {
		const MRMacroModelessIntFieldSpec &intField = definition.intFields[intFieldIndex];
		Value field;

		if (intField.fieldId.empty() || intField.label.empty() || intField.minimum > intField.maximum || intField.value < intField.minimum || intField.value > intField.maximum) continue;
		field = runtimeKv.replaceChild(intFields, intField.fieldId);
		hashWriteInt(runtimeKv, field, "x", intField.x);
		hashWriteInt(runtimeKv, field, "y", intField.y);
		hashWriteInt(runtimeKv, field, "width", intField.width);
		hashWriteString(runtimeKv, field, "label", intField.label);
		hashWriteInt(runtimeKv, field, "minimum", intField.minimum);
		hashWriteInt(runtimeKv, field, "maximum", intField.maximum);
		hashWriteInt(runtimeKv, field, "value", intField.value);
	}

	progressFields = runtimeKv.replaceChild(window, "progressFields");
	for (std::size_t progressFieldIndex = 0; progressFieldIndex < definition.progressFields.size(); ++progressFieldIndex) {
		const MRMacroModelessProgressFieldSpec &progressField = definition.progressFields[progressFieldIndex];
		Value field;

		if (progressField.fieldId.empty() || progressField.label.empty() || progressField.total <= 0 || progressField.value < 0 || progressField.value > progressField.total) continue;
		field = runtimeKv.replaceChild(progressFields, progressField.fieldId);
		hashWriteInt(runtimeKv, field, "x", progressField.x);
		hashWriteInt(runtimeKv, field, "y", progressField.y);
		hashWriteInt(runtimeKv, field, "width", progressField.width);
		hashWriteString(runtimeKv, field, "label", progressField.label);
		hashWriteInt(runtimeKv, field, "total", progressField.total);
		hashWriteInt(runtimeKv, field, "value", progressField.value);
	}

	logFields = runtimeKv.ensureChild(window, "logFields");
	for (std::size_t logFieldIndex = 0; logFieldIndex < definition.logFields.size(); ++logFieldIndex) {
		const MRMacroModelessLogFieldSpec &logField = definition.logFields[logFieldIndex];
		Value field;
		Value lines;
		bool changedCapacity = false;

		if (logField.logId.empty() || logField.label.empty() || logField.height <= 0 || logField.capacity < logField.height || logField.capacity > 256) continue;
		field = runtimeKv.ensureChild(logFields, logField.logId);
		changedCapacity = hashReadInt(runtimeKv, field, "capacity", 0) != logField.capacity;
		hashWriteInt(runtimeKv, field, "x", logField.x);
		hashWriteInt(runtimeKv, field, "y", logField.y);
		hashWriteInt(runtimeKv, field, "width", logField.width);
		hashWriteInt(runtimeKv, field, "height", logField.height);
		hashWriteString(runtimeKv, field, "label", logField.label);
		hashWriteInt(runtimeKv, field, "capacity", logField.capacity);
		if (changedCapacity) {
			hashWriteInt(runtimeKv, field, "count", 0);
			hashWriteInt(runtimeKv, field, "first", 1);
			lines = runtimeKv.replaceChild(field, "lines");
			hashWriteInt(runtimeKv, lines, "count", 0);
		}
	}

	selectFields = runtimeKv.replaceChild(window, "selectFields");
	for (std::size_t selectFieldIndex = 0; selectFieldIndex < definition.selectFields.size(); ++selectFieldIndex) {
		const MRMacroModelessSelectFieldSpec &selectField = definition.selectFields[selectFieldIndex];
		Value field;
		Value options;

		if (selectField.fieldId.empty()) continue;
		field = runtimeKv.replaceChild(selectFields, selectField.fieldId);
		hashWriteInt(runtimeKv, field, "x", selectField.x);
		hashWriteInt(runtimeKv, field, "y", selectField.y);
		hashWriteInt(runtimeKv, field, "width", selectField.width);
		hashWriteInt(runtimeKv, field, "height", selectField.height);
		hashWriteString(runtimeKv, field, "label", selectField.label);
		hashWriteString(runtimeKv, field, "value", selectField.value);
		options = runtimeKv.replaceChild(field, "options");
		for (std::size_t optionIndex = 0; optionIndex < selectField.options.size(); ++optionIndex)
			hashWriteString(runtimeKv, options, std::to_string(optionIndex + 1), selectField.options[optionIndex]);
		hashWriteInt(runtimeKv, options, "count", static_cast<int>(selectField.options.size()));
	}

	index = 0;
	statusFields = runtimeKv.replaceChild(window, "statusFields");
	for (std::map<std::string, int>::const_iterator it = definition.statusDisplayIndices.begin(); it != definition.statusDisplayIndices.end(); ++it) {
		Value item;

		if (it->first.empty() || it->second <= 0 || static_cast<std::size_t>(it->second) > definition.displays.size()) continue;
		++index;
		item = runtimeKv.replaceChild(statusFields, std::to_string(index));
		hashWriteString(runtimeKv, item, "statusId", it->first);
		hashWriteInt(runtimeKv, item, "displayIndex", it->second);
	}
	hashWriteInt(runtimeKv, statusFields, "count", index);

	canvases = runtimeKv.ensureChild(window, "canvases");
	for (std::size_t canvasIndex = 0; canvasIndex < definition.canvases.size(); ++canvasIndex) {
		const MRMacroModelessCanvasSpec &canvas = definition.canvases[canvasIndex];
		Value canvasHash;

		if (canvas.canvasId.empty()) continue;
		canvasHash = runtimeKv.ensureChild(canvases, canvas.canvasId);
		writeModelessCanvasHash(runtimeKv, runtimeKv.replaceChild(canvasHash, "definition"), canvas);
	}

	index = 0;
	canvasHotspots = runtimeKv.replaceChild(window, "canvasHotspots");
	for (std::size_t hotspotIndex = 0; hotspotIndex < definition.canvasHotspots.size(); ++hotspotIndex) {
		++index;
		writeModelessCanvasHotspotHash(runtimeKv, runtimeKv.replaceChild(canvasHotspots, std::to_string(index)), definition.canvasHotspots[hotspotIndex]);
	}
	hashWriteInt(runtimeKv, canvasHotspots, "count", index);

	index = 0;
	buttons = runtimeKv.replaceChild(window, "buttons");
	for (std::size_t buttonIndex = 0; buttonIndex < definition.buttons.size(); ++buttonIndex) {
		++index;
		writeModelessButtonHash(runtimeKv, runtimeKv.replaceChild(buttons, std::to_string(index)), definition.buttons[buttonIndex]);
	}
	hashWriteInt(runtimeKv, buttons, "count", index);

	index = 0;
	listBoxes = runtimeKv.replaceChild(window, "listBoxes");
	for (std::size_t listBoxIndex = 0; listBoxIndex < definition.listBoxes.size(); ++listBoxIndex) {
		++index;
		writeModelessListBoxHash(runtimeKv, runtimeKv.replaceChild(listBoxes, std::to_string(index)), definition.listBoxes[listBoxIndex]);
	}
	hashWriteInt(runtimeKv, listBoxes, "count", index);

	index = 0;
	grids = runtimeKv.replaceChild(window, "grids");
	for (std::size_t gridIndex = 0; gridIndex < definition.grids.size(); ++gridIndex) {
		++index;
		writeModelessGridHash(runtimeKv, runtimeKv.replaceChild(grids, std::to_string(index)), definition.grids[gridIndex]);
	}
	hashWriteInt(runtimeKv, grids, "count", index);

	index = 0;
	trees = runtimeKv.replaceChild(window, "trees");
	for (std::size_t treeIndex = 0; treeIndex < definition.trees.size(); ++treeIndex) {
		++index;
		writeModelessGridHash(runtimeKv, runtimeKv.replaceChild(trees, std::to_string(index)), definition.trees[treeIndex]);
	}
	hashWriteInt(runtimeKv, trees, "count", index);

	index = 0;
	tables = runtimeKv.replaceChild(window, "tables");
	for (std::size_t tableIndex = 0; tableIndex < definition.tables.size(); ++tableIndex) {
		++index;
		writeModelessGridHash(runtimeKv, runtimeKv.replaceChild(tables, std::to_string(index)), definition.tables[tableIndex]);
	}
	hashWriteInt(runtimeKv, tables, "count", index);
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

bool mrvmModelessUiReadWindowStatusDisplayIndex(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &statusId, int &displayIndex) {
	Value windows;
	Value window;
	Value statusFields;

	displayIndex = 0;
	if (windowId.empty() || statusId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "statusFields", statusFields)) return false;
	for (int index = 1; index <= hashReadInt(runtimeKv, statusFields, "count", 0); ++index) {
		Value item;

		if (!runtimeKv.findChild(statusFields, std::to_string(index), item)) continue;
		if (hashReadString(runtimeKv, item, "statusId") != statusId) continue;
		displayIndex = hashReadInt(runtimeKv, item, "displayIndex", 0);
		return displayIndex > 0;
	}
	return false;
}

bool mrvmModelessUiStoreWindowTextFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, const std::string &text) {
	Value windows;
	Value window;
	Value textFields;
	Value field;

	if (windowId.empty() || fieldId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "textFields", textFields)) return false;
	if (!runtimeKv.findChild(textFields, fieldId, field)) return false;
	if (hashReadString(runtimeKv, field, "text") == text) return true;
	hashWriteString(runtimeKv, field, "text", text);
	return true;
}

bool mrvmModelessUiReadWindowTextFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, std::string &text) {
	Value windows;
	Value window;
	Value textFields;
	Value field;

	text.clear();
	if (windowId.empty() || fieldId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "textFields", textFields)) return false;
	if (!runtimeKv.findChild(textFields, fieldId, field)) return false;
	text = hashReadString(runtimeKv, field, "text");
	return true;
}

bool mrvmModelessUiStoreWindowBoolFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, bool value) {
	Value windows;
	Value window;
	Value boolFields;
	Value field;

	if (windowId.empty() || fieldId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "boolFields", boolFields)) return false;
	if (!runtimeKv.findChild(boolFields, fieldId, field)) return false;
	if ((hashReadInt(runtimeKv, field, "value", 0) != 0) == value) return true;
	hashWriteInt(runtimeKv, field, "value", value ? 1 : 0);
	return true;
}

bool mrvmModelessUiReadWindowBoolFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, bool &value) {
	Value windows;
	Value window;
	Value boolFields;
	Value field;

	value = false;
	if (windowId.empty() || fieldId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "boolFields", boolFields)) return false;
	if (!runtimeKv.findChild(boolFields, fieldId, field)) return false;
	value = hashReadInt(runtimeKv, field, "value", 0) != 0;
	return true;
}

bool mrvmModelessUiStoreWindowIntFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, int value) {
	Value windows;
	Value window;
	Value intFields;
	Value field;
	int minimum = 0;
	int maximum = 0;

	if (windowId.empty() || fieldId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "intFields", intFields)) return false;
	if (!runtimeKv.findChild(intFields, fieldId, field)) return false;
	minimum = hashReadInt(runtimeKv, field, "minimum", 0);
	maximum = hashReadInt(runtimeKv, field, "maximum", 0);
	if (minimum > maximum || value < minimum || value > maximum) return false;
	if (hashReadInt(runtimeKv, field, "value", minimum) == value) return true;
	hashWriteInt(runtimeKv, field, "value", value);
	return true;
}

bool mrvmModelessUiReadWindowIntFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, int &value) {
	Value windows;
	Value window;
	Value intFields;
	Value field;

	value = 0;
	if (windowId.empty() || fieldId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "intFields", intFields)) return false;
	if (!runtimeKv.findChild(intFields, fieldId, field)) return false;
	value = hashReadInt(runtimeKv, field, "value", 0);
	return true;
}

bool mrvmModelessUiStoreWindowProgressFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, int value) {
	Value windows;
	Value window;
	Value progressFields;
	Value field;
	int total = 0;

	if (windowId.empty() || fieldId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "progressFields", progressFields)) return false;
	if (!runtimeKv.findChild(progressFields, fieldId, field)) return false;
	total = hashReadInt(runtimeKv, field, "total", 0);
	if (total <= 0 || value < 0 || value > total) return false;
	if (hashReadInt(runtimeKv, field, "value", 0) == value) return true;
	hashWriteInt(runtimeKv, field, "value", value);
	return true;
}

bool mrvmModelessUiReadWindowProgressFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, int &total, int &value) {
	Value windows;
	Value window;
	Value progressFields;
	Value field;

	total = 0;
	value = 0;
	if (windowId.empty() || fieldId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "progressFields", progressFields)) return false;
	if (!runtimeKv.findChild(progressFields, fieldId, field)) return false;
	total = hashReadInt(runtimeKv, field, "total", 0);
	value = hashReadInt(runtimeKv, field, "value", 0);
	return total > 0 && value >= 0 && value <= total;
}

bool mrvmModelessUiAppendWindowLogFieldLine(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &logId, const std::string &text) {
	Value windows;
	Value window;
	Value logFields;
	Value field;
	Value lines;
	int capacity = 0;
	int count = 0;
	int first = 0;
	int lineIndex = 0;

	if (windowId.empty() || logId.empty() || text.size() > 512 || text.find_first_of("\r\n") != std::string::npos) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "logFields", logFields)) return false;
	if (!runtimeKv.findChild(logFields, logId, field)) return false;
	capacity = hashReadInt(runtimeKv, field, "capacity", 0);
	count = hashReadInt(runtimeKv, field, "count", 0);
	first = hashReadInt(runtimeKv, field, "first", 1);
	if (capacity <= 0 || count < 0 || count > capacity || first < 1 || first > capacity) return false;
	lines = runtimeKv.ensureChild(field, "lines");
	if (count < capacity) {
		lineIndex = ((first - 1 + count) % capacity) + 1;
		++count;
	} else {
		lineIndex = first;
		first = (first % capacity) + 1;
	}
	hashWriteString(runtimeKv, lines, std::to_string(lineIndex), text);
	hashWriteInt(runtimeKv, lines, "count", count);
	hashWriteInt(runtimeKv, field, "count", count);
	hashWriteInt(runtimeKv, field, "first", first);
	return true;
}

bool mrvmModelessUiClearWindowLogField(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &logId) {
	Value windows;
	Value window;
	Value logFields;
	Value field;
	Value lines;

	if (windowId.empty() || logId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "logFields", logFields)) return false;
	if (!runtimeKv.findChild(logFields, logId, field)) return false;
	if (hashReadInt(runtimeKv, field, "capacity", 0) <= 0) return false;
	lines = runtimeKv.replaceChild(field, "lines");
	hashWriteInt(runtimeKv, lines, "count", 0);
	hashWriteInt(runtimeKv, field, "count", 0);
	hashWriteInt(runtimeKv, field, "first", 1);
	return true;
}

bool mrvmModelessUiReadWindowLogFieldLines(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &logId, std::vector<std::string> &lines) {
	Value windows;
	Value window;
	Value logFields;
	Value field;
	Value storedLines;
	int capacity = 0;
	int count = 0;
	int first = 0;

	lines.clear();
	if (windowId.empty() || logId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "logFields", logFields)) return false;
	if (!runtimeKv.findChild(logFields, logId, field)) return false;
	capacity = hashReadInt(runtimeKv, field, "capacity", 0);
	count = hashReadInt(runtimeKv, field, "count", 0);
	first = hashReadInt(runtimeKv, field, "first", 1);
	if (capacity <= 0 || count < 0 || count > capacity || first < 1 || first > capacity) return false;
	if (count == 0) return true;
	if (!runtimeKv.findChild(field, "lines", storedLines)) return false;
	for (int index = 0; index < count; ++index) {
		const int lineIndex = ((first - 1 + index) % capacity) + 1;

		lines.push_back(hashReadString(runtimeKv, storedLines, std::to_string(lineIndex)));
	}
	return true;
}

bool mrvmModelessUiReadWindowLogFieldCount(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &logId, int &count) {
	Value windows;
	Value window;
	Value logFields;
	Value field;
	int capacity = 0;

	count = 0;
	if (windowId.empty() || logId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "logFields", logFields)) return false;
	if (!runtimeKv.findChild(logFields, logId, field)) return false;
	capacity = hashReadInt(runtimeKv, field, "capacity", 0);
	count = hashReadInt(runtimeKv, field, "count", 0);
	if (capacity <= 0 || count < 0 || count > capacity) {
		count = 0;
		return false;
	}
	return true;
}

bool mrvmModelessUiStoreWindowSelectFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, const std::string &value) {
	Value windows;
	Value window;
	Value selectFields;
	Value field;
	Value options;

	if (windowId.empty() || fieldId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "selectFields", selectFields)) return false;
	if (!runtimeKv.findChild(selectFields, fieldId, field) || !runtimeKv.findChild(field, "options", options)) return false;
	for (int index = 1; index <= hashReadInt(runtimeKv, options, "count", 0); ++index)
		if (hashReadString(runtimeKv, options, std::to_string(index)) == value) {
			if (hashReadString(runtimeKv, field, "value") == value) return true;
			hashWriteString(runtimeKv, field, "value", value);
			return true;
		}
	return false;
}

bool mrvmModelessUiReadWindowSelectFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, std::string &value) {
	Value windows;
	Value window;
	Value selectFields;
	Value field;

	value.clear();
	if (windowId.empty() || fieldId.empty()) return false;
	if (!findModelessUiChildPath(runtimeKv, {"windows"}, windows)) return false;
	if (!runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "selectFields", selectFields)) return false;
	if (!runtimeKv.findChild(selectFields, fieldId, field)) return false;
	value = hashReadString(runtimeKv, field, "value");
	return true;
}

bool mrvmModelessUiStoreWindowTreeSelection(MRVMRuntimeKv &runtimeKv, const std::string &windowId, int controlId, const std::string &nodeId) {
	Value state;

	if (nodeId.empty() || !findModelessWindowControlState(runtimeKv, windowId, "treeState", controlId, state, true)) return false;
	if (hashReadString(runtimeKv, state, "selection") == nodeId) return true;
	hashWriteString(runtimeKv, state, "selection", nodeId);
	return true;
}

bool mrvmModelessUiReadWindowTreeSelection(MRVMRuntimeKv &runtimeKv, const std::string &windowId, int controlId, std::string &nodeId) {
	Value state;

	nodeId.clear();
	if (!findModelessWindowControlState(runtimeKv, windowId, "treeState", controlId, state, false)) return false;
	nodeId = hashReadString(runtimeKv, state, "selection");
	return !nodeId.empty();
}

bool mrvmModelessUiStoreWindowTreeExpansion(MRVMRuntimeKv &runtimeKv, const std::string &windowId, int controlId, const std::string &nodeId, bool expanded) {
	Value state;
	Value expandedNodes;

	if (nodeId.empty() || !findModelessWindowControlState(runtimeKv, windowId, "treeState", controlId, state, true)) return false;
	expandedNodes = runtimeKv.ensureChild(state, "expanded");
	hashWriteInt(runtimeKv, expandedNodes, nodeId, expanded ? 1 : 0);
	return true;
}

bool mrvmModelessUiReadWindowTreeExpansion(MRVMRuntimeKv &runtimeKv, const std::string &windowId, int controlId, const std::string &nodeId, bool &expanded) {
	Value state;
	Value expandedNodes;
	int storedValue = -1;

	expanded = false;
	if (nodeId.empty() || !findModelessWindowControlState(runtimeKv, windowId, "treeState", controlId, state, false)) return false;
	if (!runtimeKv.findChild(state, "expanded", expandedNodes)) return false;
	storedValue = hashReadInt(runtimeKv, expandedNodes, nodeId, -1);
	if (storedValue < 0) return false;
	expanded = storedValue != 0;
	return true;
}

bool mrvmModelessUiStoreWindowTableSelection(MRVMRuntimeKv &runtimeKv, const std::string &windowId, int controlId, const std::string &rowId) {
	Value state;

	if (rowId.empty() || !findModelessWindowControlState(runtimeKv, windowId, "tableState", controlId, state, true)) return false;
	if (hashReadString(runtimeKv, state, "selection") == rowId) return true;
	hashWriteString(runtimeKv, state, "selection", rowId);
	return true;
}

bool mrvmModelessUiReadWindowTableSelection(MRVMRuntimeKv &runtimeKv, const std::string &windowId, int controlId, std::string &rowId) {
	Value state;

	rowId.clear();
	if (!findModelessWindowControlState(runtimeKv, windowId, "tableState", controlId, state, false)) return false;
	rowId = hashReadString(runtimeKv, state, "selection");
	return !rowId.empty();
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

bool mrvmModelessUiReadWindowGeometry(MRVMRuntimeKv &runtimeKv, const std::string &windowId, MRMacroModelessWindowGeometry &geometry) {
	Value windows;
	Value window;
	Value liveGeometry;

	geometry = MRMacroModelessWindowGeometry();
	if (windowId.empty() || !findModelessUiChildPath(runtimeKv, {"windows"}, windows) || !runtimeKv.findChild(windows, windowId, window)) return false;
	geometry.x = hashReadInt(runtimeKv, window, "x", 0);
	geometry.y = hashReadInt(runtimeKv, window, "y", 0);
	geometry.width = hashReadInt(runtimeKv, window, "width", 0);
	geometry.height = hashReadInt(runtimeKv, window, "height", 0);
	if (!runtimeKv.findChild(window, "liveGeometry", liveGeometry)) return true;
	geometry.x = hashReadInt(runtimeKv, liveGeometry, "x", geometry.x);
	geometry.y = hashReadInt(runtimeKv, liveGeometry, "y", geometry.y);
	geometry.width = hashReadInt(runtimeKv, liveGeometry, "width", geometry.width);
	geometry.height = hashReadInt(runtimeKv, liveGeometry, "height", geometry.height);
	return true;
}

void mrvmModelessUiStoreWindowDesktopState(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const MRMacroModelessWindowDesktopState &state) {
	Value windows;
	Value window;
	Value desktop;

	if (windowId.empty()) return;
	windows = ensureModelessUiChildPath(runtimeKv, {"windows"});
	window = runtimeKv.ensureChild(windows, windowId);
	desktop = runtimeKv.ensureChild(window, "desktop");
	hashWriteInt(runtimeKv, desktop, "number", std::max(1, state.virtualDesktop));
	hashWriteInt(runtimeKv, desktop, "manuallyHidden", state.manuallyHidden ? 1 : 0);
	hashWriteInt(runtimeKv, desktop, "minimized", state.minimized ? 1 : 0);
	hashWriteInt(runtimeKv, desktop, "bufferedBeforeMinimize", state.bufferedBeforeMinimize ? 1 : 0);
	hashWriteInt(runtimeKv, desktop, "restoreX", state.restoreX);
	hashWriteInt(runtimeKv, desktop, "restoreY", state.restoreY);
	hashWriteInt(runtimeKv, desktop, "restoreWidth", state.restoreWidth);
	hashWriteInt(runtimeKv, desktop, "restoreHeight", state.restoreHeight);
	hashWriteInt(runtimeKv, desktop, "lastMinimizedX", state.lastMinimizedX);
	hashWriteInt(runtimeKv, desktop, "lastMinimizedY", state.lastMinimizedY);
	hashWriteInt(runtimeKv, desktop, "lastMinimizedWidth", state.lastMinimizedWidth);
	hashWriteInt(runtimeKv, desktop, "lastMinimizedHeight", state.lastMinimizedHeight);
	hashWriteInt(runtimeKv, desktop, "assigned", 1);
}

bool mrvmModelessUiReadWindowDesktopState(MRVMRuntimeKv &runtimeKv, const std::string &windowId, MRMacroModelessWindowDesktopState &state) {
	Value windows;
	Value window;
	Value desktop;

	state = MRMacroModelessWindowDesktopState();
	if (windowId.empty() || !findModelessUiChildPath(runtimeKv, {"windows"}, windows) || !runtimeKv.findChild(windows, windowId, window)) return false;
	if (!runtimeKv.findChild(window, "desktop", desktop)) return true;
	state.virtualDesktop = std::max(1, hashReadInt(runtimeKv, desktop, "number", 1));
	state.manuallyHidden = hashReadInt(runtimeKv, desktop, "manuallyHidden", 0) != 0;
	state.minimized = hashReadInt(runtimeKv, desktop, "minimized", 0) != 0;
	state.bufferedBeforeMinimize = hashReadInt(runtimeKv, desktop, "bufferedBeforeMinimize", 0) != 0;
	state.restoreX = hashReadInt(runtimeKv, desktop, "restoreX", 0);
	state.restoreY = hashReadInt(runtimeKv, desktop, "restoreY", 0);
	state.restoreWidth = hashReadInt(runtimeKv, desktop, "restoreWidth", 0);
	state.restoreHeight = hashReadInt(runtimeKv, desktop, "restoreHeight", 0);
	state.lastMinimizedX = hashReadInt(runtimeKv, desktop, "lastMinimizedX", 0);
	state.lastMinimizedY = hashReadInt(runtimeKv, desktop, "lastMinimizedY", 0);
	state.lastMinimizedWidth = hashReadInt(runtimeKv, desktop, "lastMinimizedWidth", 0);
	state.lastMinimizedHeight = hashReadInt(runtimeKv, desktop, "lastMinimizedHeight", 0);
	state.assigned = hashReadInt(runtimeKv, desktop, "assigned", 0) != 0;
	return true;
}

std::string mrvmModelessUiCreateWindowInstanceId(MRVMRuntimeKv &runtimeKv, const std::string &prefix) {
	Value counters = ensureModelessUiChildPath(runtimeKv, {"counters"});
	const int next = hashReadInt(runtimeKv, counters, "windowInstances", 0) + 1;

	hashWriteInt(runtimeKv, counters, "windowInstances", next);
	return prefix + "#" + std::to_string(next);
}

bool mrvmModelessUiWindowExists(MRVMRuntimeKv &runtimeKv, const std::string &windowId) {
	Value windows;
	Value window;

	return !windowId.empty() && findModelessUiChildPath(runtimeKv, {"windows"}, windows) && runtimeKv.findChild(windows, windowId, window);
}

bool mrvmModelessUiCanvasClear(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &canvasId, int style) {
	MRMacroModelessCanvasCommand command;
	Value canvas;
	Value scene;

	if (!findModelessCanvas(runtimeKv, windowId, canvasId, canvas)) return false;
	command.type = MRMacroModelessCanvasCommandType::Clear;
	command.style = style;
	scene = runtimeKv.replaceChild(canvas, "scene");
	hashWriteInt(runtimeKv, scene, "count", 1);
	writeCanvasCommand(runtimeKv, runtimeKv.replaceChild(scene, "1"), command);
	return true;
}

bool mrvmModelessUiCanvasAppendCommand(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &canvasId, const MRMacroModelessCanvasCommand &command) {
	Value canvas;
	Value scene;
	int count = 0;

	if (!findModelessCanvas(runtimeKv, windowId, canvasId, canvas)) return false;
	scene = runtimeKv.ensureChild(canvas, "scene");
	count = hashReadInt(runtimeKv, scene, "count", 0);
	if (count >= kMaximumCanvasCommandCount) return false;
	++count;
	hashWriteInt(runtimeKv, scene, "count", count);
	writeCanvasCommand(runtimeKv, runtimeKv.replaceChild(scene, std::to_string(count)), command);
	return true;
}

bool mrvmModelessUiReadCanvasScene(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &canvasId, MRMacroModelessCanvasScene &scene) {
	Value canvas;
	Value canvasScene;

	scene = MRMacroModelessCanvasScene();
	if (!findModelessCanvas(runtimeKv, windowId, canvasId, canvas)) return false;
	scene.generation = static_cast<unsigned long>(hashReadInt(runtimeKv, canvas, "generation", 0));
	if (!runtimeKv.findChild(canvas, "scene", canvasScene)) return true;
	for (int index = 1; index <= hashReadInt(runtimeKv, canvasScene, "count", 0); ++index) {
		Value command;
		if (runtimeKv.findChild(canvasScene, std::to_string(index), command)) scene.commands.push_back(readCanvasCommand(runtimeKv, command));
	}
	return true;
}

bool mrvmModelessUiCommitCanvas(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &canvasId) {
	Value canvas;
	int generation = 0;

	if (!findModelessCanvas(runtimeKv, windowId, canvasId, canvas)) return false;
	generation = hashReadInt(runtimeKv, canvas, "generation", 0) + 1;
	hashWriteInt(runtimeKv, canvas, "generation", generation);
	return true;
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

bool mrvmStoreModelessWindowTextFieldValue(const std::string &windowId, const std::string &fieldId, const std::string &text) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiStoreWindowTextFieldValue(mrvmRuntimeKv(), windowId, fieldId, text);
}

bool mrvmReadModelessWindowTextFieldValue(const std::string &windowId, const std::string &fieldId, std::string &text) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiReadWindowTextFieldValue(mrvmRuntimeKv(), windowId, fieldId, text);
}

bool mrvmStoreModelessWindowBoolFieldValue(const std::string &windowId, const std::string &fieldId, bool value) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiStoreWindowBoolFieldValue(mrvmRuntimeKv(), windowId, fieldId, value);
}

bool mrvmReadModelessWindowBoolFieldValue(const std::string &windowId, const std::string &fieldId, bool &value) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiReadWindowBoolFieldValue(mrvmRuntimeKv(), windowId, fieldId, value);
}

bool mrvmStoreModelessWindowIntFieldValue(const std::string &windowId, const std::string &fieldId, int value) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiStoreWindowIntFieldValue(mrvmRuntimeKv(), windowId, fieldId, value);
}

bool mrvmReadModelessWindowIntFieldValue(const std::string &windowId, const std::string &fieldId, int &value) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiReadWindowIntFieldValue(mrvmRuntimeKv(), windowId, fieldId, value);
}

bool mrvmStoreModelessWindowProgressFieldValue(const std::string &windowId, const std::string &fieldId, int value) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiStoreWindowProgressFieldValue(mrvmRuntimeKv(), windowId, fieldId, value);
}

bool mrvmReadModelessWindowProgressFieldValue(const std::string &windowId, const std::string &fieldId, int &total, int &value) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiReadWindowProgressFieldValue(mrvmRuntimeKv(), windowId, fieldId, total, value);
}

bool mrvmAppendModelessWindowLogFieldLine(const std::string &windowId, const std::string &logId, const std::string &text) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiAppendWindowLogFieldLine(mrvmRuntimeKv(), windowId, logId, text);
}

bool mrvmReadModelessWindowLogFieldLines(const std::string &windowId, const std::string &logId, std::vector<std::string> &lines) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiReadWindowLogFieldLines(mrvmRuntimeKv(), windowId, logId, lines);
}

bool mrvmStoreModelessWindowSelectFieldValue(const std::string &windowId, const std::string &fieldId, const std::string &value) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiStoreWindowSelectFieldValue(mrvmRuntimeKv(), windowId, fieldId, value);
}

bool mrvmReadModelessWindowSelectFieldValue(const std::string &windowId, const std::string &fieldId, std::string &value) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiReadWindowSelectFieldValue(mrvmRuntimeKv(), windowId, fieldId, value);
}

bool mrvmStoreModelessWindowTreeSelection(const std::string &windowId, int controlId, const std::string &nodeId) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiStoreWindowTreeSelection(mrvmRuntimeKv(), windowId, controlId, nodeId);
}

bool mrvmReadModelessWindowTreeSelection(const std::string &windowId, int controlId, std::string &nodeId) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiReadWindowTreeSelection(mrvmRuntimeKv(), windowId, controlId, nodeId);
}

bool mrvmStoreModelessWindowTreeExpansion(const std::string &windowId, int controlId, const std::string &nodeId, bool expanded) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiStoreWindowTreeExpansion(mrvmRuntimeKv(), windowId, controlId, nodeId, expanded);
}

bool mrvmReadModelessWindowTreeExpansion(const std::string &windowId, int controlId, const std::string &nodeId, bool &expanded) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiReadWindowTreeExpansion(mrvmRuntimeKv(), windowId, controlId, nodeId, expanded);
}

bool mrvmStoreModelessWindowTableSelection(const std::string &windowId, int controlId, const std::string &rowId) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiStoreWindowTableSelection(mrvmRuntimeKv(), windowId, controlId, rowId);
}

bool mrvmReadModelessWindowTableSelection(const std::string &windowId, int controlId, std::string &rowId) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiReadWindowTableSelection(mrvmRuntimeKv(), windowId, controlId, rowId);
}

void mrvmStoreModelessWindowLiveGeometry(const std::string &windowId, int x, int y, int width, int height) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	mrvmModelessUiStoreWindowLiveGeometry(mrvmRuntimeKv(), windowId, x, y, width, height);
}

bool mrvmReadModelessCanvasScene(const std::string &windowId, const std::string &canvasId, MRMacroModelessCanvasScene &scene) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiReadCanvasScene(mrvmRuntimeKv(), windowId, canvasId, scene);
}

void mrvmStoreModelessWindowDesktopState(const std::string &windowId, const MRMacroModelessWindowDesktopState &state) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	mrvmModelessUiStoreWindowDesktopState(mrvmRuntimeKv(), windowId, state);
}

bool mrvmReadModelessWindowDesktopState(const std::string &windowId, MRMacroModelessWindowDesktopState &state) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmModelessUiReadWindowDesktopState(mrvmRuntimeKv(), windowId, state);
}

void mrvmRemoveModelessWindowDefinition(const std::string &windowId) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	mrvmModelessUiRemoveWindowDefinition(mrvmRuntimeKv(), windowId);
}
