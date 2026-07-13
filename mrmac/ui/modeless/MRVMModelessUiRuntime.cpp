#include "MRVMModelessUiStorage.hpp"

#include <algorithm>

using namespace mr::modelessui;
using Value = VirtualMachine::Value;

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

int mrvmModelessUiAppendDisplay(MRVMRuntimeKv &runtimeKv, const MacroUiDisplaySpec &display) {
	Value displayHash = appendDialogSectionItem(runtimeKv, "displays");
	Value displays = ensureDialogSection(runtimeKv, "displays");

	writeDisplayHash(runtimeKv, displayHash, display);
	return hashReadInt(runtimeKv, displays, "count", 0);
}

bool mrvmModelessUiAppendStatusField(MRVMRuntimeKv &runtimeKv, const std::string &statusId, const MacroUiDisplaySpec &display) {
	Value statusFields;
	int count = 0;

	if (statusId.empty()) return false;
	statusFields = ensureDialogSection(runtimeKv, "statusFields");
	count = hashReadInt(runtimeKv, statusFields, "count", 0);
	for (int index = 1; index <= count; ++index) {
		Value item;

		if (!runtimeKv.findChild(statusFields, std::to_string(index), item)) continue;
		if (hashReadString(runtimeKv, item, "statusId") == statusId) return false;
	}
	++count;
	Value item = runtimeKv.replaceChild(statusFields, std::to_string(count));

	hashWriteInt(runtimeKv, statusFields, "count", count);
	hashWriteString(runtimeKv, item, "statusId", statusId);
	hashWriteInt(runtimeKv, item, "displayIndex", mrvmModelessUiAppendDisplay(runtimeKv, display));
	return true;
}

void mrvmModelessUiAppendCanvas(MRVMRuntimeKv &runtimeKv, const MacroUiCanvasSpec &canvas) {
	writeCanvasHash(runtimeKv, appendDialogSectionItem(runtimeKv, "canvases"), canvas);
}

bool mrvmModelessUiAppendCanvasHotspot(MRVMRuntimeKv &runtimeKv, const MacroUiCanvasHotspotSpec &hotspot) {
	Value dialog;
	Value canvases;

	if (hotspot.canvasId.empty() || !findCurrentDialog(runtimeKv, dialog) || !runtimeKv.findChild(dialog, "canvases", canvases)) return false;
	for (int index = 1; index <= hashReadInt(runtimeKv, canvases, "count", 0); ++index) {
		Value canvas;
		MacroUiCanvasSpec canvasSpec;

		if (!runtimeKv.findChild(canvases, std::to_string(index), canvas)) continue;
		canvasSpec = readCanvasHash(runtimeKv, canvas);
		if (canvasSpec.canvasId != hotspot.canvasId) continue;
		if (hotspot.x < 0 || hotspot.y < 0 || hotspot.width <= 0 || hotspot.height <= 0 || hotspot.x > canvasSpec.width - hotspot.width || hotspot.y > canvasSpec.height - hotspot.height) return false;
		writeCanvasHotspotHash(runtimeKv, appendDialogSectionItem(runtimeKv, "canvasHotspots"), hotspot);
		return true;
	}
	return false;
}

int mrvmModelessUiAppendInput(MRVMRuntimeKv &runtimeKv, const MacroUiInputSpec &input) {
	Value inputHash = appendDialogSectionItem(runtimeKv, "inputs");
	Value inputs = ensureDialogSection(runtimeKv, "inputs");

	mrvmModelessUiWriteTextValue(runtimeKv, input.id, input.text);
	writeInputHash(runtimeKv, inputHash, input);
	return hashReadInt(runtimeKv, inputs, "count", 0);
}

bool mrvmModelessUiAppendTextField(MRVMRuntimeKv &runtimeKv, const std::string &fieldId, const MacroUiInputSpec &input) {
	Value textFields;
	int count = 0;

	if (fieldId.empty()) return false;
	textFields = ensureDialogSection(runtimeKv, "textFields");
	count = hashReadInt(runtimeKv, textFields, "count", 0);
	for (int index = 1; index <= count; ++index) {
		Value item;

		if (!runtimeKv.findChild(textFields, std::to_string(index), item)) continue;
		if (hashReadString(runtimeKv, item, "fieldId") == fieldId) return false;
	}
	++count;
	Value item = runtimeKv.replaceChild(textFields, std::to_string(count));

	hashWriteInt(runtimeKv, textFields, "count", count);
	hashWriteString(runtimeKv, item, "fieldId", fieldId);
	hashWriteInt(runtimeKv, item, "inputIndex", mrvmModelessUiAppendInput(runtimeKv, input));
	return true;
}

bool mrvmModelessUiAppendBoolField(MRVMRuntimeKv &runtimeKv, const MacroUiBoolFieldSpec &field) {
	Value boolFields;
	int count = 0;

	if (field.fieldId.empty() || field.caption.empty()) return false;
	boolFields = ensureDialogSection(runtimeKv, "boolFields");
	count = hashReadInt(runtimeKv, boolFields, "count", 0);
	for (int index = 1; index <= count; ++index) {
		Value item;

		if (!runtimeKv.findChild(boolFields, std::to_string(index), item)) continue;
		if (hashReadString(runtimeKv, item, "fieldId") == field.fieldId) return false;
	}
	++count;
	Value item = runtimeKv.replaceChild(boolFields, std::to_string(count));

	hashWriteInt(runtimeKv, boolFields, "count", count);
	hashWriteInt(runtimeKv, item, "x", field.x);
	hashWriteInt(runtimeKv, item, "y", field.y);
	hashWriteString(runtimeKv, item, "fieldId", field.fieldId);
	hashWriteString(runtimeKv, item, "caption", field.caption);
	hashWriteInt(runtimeKv, item, "value", field.value ? 1 : 0);
	return true;
}

bool mrvmModelessUiAppendIntField(MRVMRuntimeKv &runtimeKv, const MacroUiIntFieldSpec &field) {
	Value intFields;
	int count = 0;

	if (field.fieldId.empty() || field.label.empty() || field.minimum > field.maximum || field.value < field.minimum || field.value > field.maximum) return false;
	intFields = ensureDialogSection(runtimeKv, "intFields");
	count = hashReadInt(runtimeKv, intFields, "count", 0);
	for (int index = 1; index <= count; ++index) {
		Value item;

		if (!runtimeKv.findChild(intFields, std::to_string(index), item)) continue;
		if (hashReadString(runtimeKv, item, "fieldId") == field.fieldId) return false;
	}
	++count;
	Value item = runtimeKv.replaceChild(intFields, std::to_string(count));

	hashWriteInt(runtimeKv, intFields, "count", count);
	hashWriteInt(runtimeKv, item, "x", field.x);
	hashWriteInt(runtimeKv, item, "y", field.y);
	hashWriteInt(runtimeKv, item, "width", std::max(4, field.width));
	hashWriteString(runtimeKv, item, "fieldId", field.fieldId);
	hashWriteString(runtimeKv, item, "label", field.label);
	hashWriteInt(runtimeKv, item, "minimum", field.minimum);
	hashWriteInt(runtimeKv, item, "maximum", field.maximum);
	hashWriteInt(runtimeKv, item, "value", field.value);
	return true;
}

bool mrvmModelessUiAppendProgressField(MRVMRuntimeKv &runtimeKv, const MacroUiProgressFieldSpec &field) {
	Value progressFields;
	int count = 0;

	if (field.fieldId.empty() || field.label.empty() || field.total <= 0 || field.value < 0 || field.value > field.total) return false;
	progressFields = ensureDialogSection(runtimeKv, "progressFields");
	count = hashReadInt(runtimeKv, progressFields, "count", 0);
	for (int index = 1; index <= count; ++index) {
		Value item;

		if (!runtimeKv.findChild(progressFields, std::to_string(index), item)) continue;
		if (hashReadString(runtimeKv, item, "fieldId") == field.fieldId) return false;
	}
	++count;
	Value item = runtimeKv.replaceChild(progressFields, std::to_string(count));

	hashWriteInt(runtimeKv, progressFields, "count", count);
	hashWriteInt(runtimeKv, item, "x", field.x);
	hashWriteInt(runtimeKv, item, "y", field.y);
	hashWriteInt(runtimeKv, item, "width", std::max(8, field.width));
	hashWriteString(runtimeKv, item, "fieldId", field.fieldId);
	hashWriteString(runtimeKv, item, "label", field.label);
	hashWriteInt(runtimeKv, item, "total", field.total);
	hashWriteInt(runtimeKv, item, "value", field.value);
	return true;
}

bool mrvmModelessUiAppendLogField(MRVMRuntimeKv &runtimeKv, const MacroUiLogFieldSpec &field) {
	Value logFields;
	int count = 0;

	if (field.logId.empty() || field.label.empty() || field.height <= 0 || field.capacity < field.height || field.capacity > 256) return false;
	logFields = ensureDialogSection(runtimeKv, "logFields");
	count = hashReadInt(runtimeKv, logFields, "count", 0);
	for (int index = 1; index <= count; ++index) {
		Value item;

		if (!runtimeKv.findChild(logFields, std::to_string(index), item)) continue;
		if (hashReadString(runtimeKv, item, "logId") == field.logId) return false;
	}
	++count;
	Value item = runtimeKv.replaceChild(logFields, std::to_string(count));

	hashWriteInt(runtimeKv, logFields, "count", count);
	hashWriteInt(runtimeKv, item, "x", field.x);
	hashWriteInt(runtimeKv, item, "y", field.y);
	hashWriteInt(runtimeKv, item, "width", std::max(8, field.width));
	hashWriteInt(runtimeKv, item, "height", field.height);
	hashWriteString(runtimeKv, item, "logId", field.logId);
	hashWriteString(runtimeKv, item, "label", field.label);
	hashWriteInt(runtimeKv, item, "capacity", field.capacity);
	return true;
}

bool mrvmModelessUiAppendSelectField(MRVMRuntimeKv &runtimeKv, const MacroUiSelectFieldSpec &field) {
	Value selectFields;
	int count = 0;

	if (field.fieldId.empty() || field.label.empty()) return false;
	selectFields = ensureDialogSection(runtimeKv, "selectFields");
	count = hashReadInt(runtimeKv, selectFields, "count", 0);
	for (int index = 1; index <= count; ++index) {
		Value item;

		if (!runtimeKv.findChild(selectFields, std::to_string(index), item)) continue;
		if (hashReadString(runtimeKv, item, "fieldId") == field.fieldId) return false;
	}
	++count;
	Value item = runtimeKv.replaceChild(selectFields, std::to_string(count));

	hashWriteInt(runtimeKv, selectFields, "count", count);
	hashWriteInt(runtimeKv, item, "x", field.x);
	hashWriteInt(runtimeKv, item, "y", field.y);
	hashWriteInt(runtimeKv, item, "width", std::max(8, field.width));
	hashWriteInt(runtimeKv, item, "height", std::max(2, field.height));
	hashWriteString(runtimeKv, item, "fieldId", field.fieldId);
	hashWriteString(runtimeKv, item, "label", field.label);
	hashWriteString(runtimeKv, item, "value", field.value);
	hashWriteInt(runtimeKv, runtimeKv.replaceChild(item, "options"), "count", 0);
	return true;
}

bool mrvmModelessUiAppendSelectOption(MRVMRuntimeKv &runtimeKv, const std::string &fieldId, const std::string &option) {
	Value dialog;
	Value selectFields;

	if (fieldId.empty() || option.empty() || !findCurrentDialog(runtimeKv, dialog) || !runtimeKv.findChild(dialog, "selectFields", selectFields)) return false;
	for (int index = 1; index <= hashReadInt(runtimeKv, selectFields, "count", 0); ++index) {
		Value field;
		Value options;
		int optionCount = 0;

		if (!runtimeKv.findChild(selectFields, std::to_string(index), field) || hashReadString(runtimeKv, field, "fieldId") != fieldId) continue;
		options = runtimeKv.ensureChild(field, "options");
		optionCount = hashReadInt(runtimeKv, options, "count", 0);
		for (int optionIndex = 1; optionIndex <= optionCount; ++optionIndex)
			if (hashReadString(runtimeKv, options, std::to_string(optionIndex)) == option) return false;
		++optionCount;
		hashWriteInt(runtimeKv, options, "count", optionCount);
		hashWriteString(runtimeKv, options, std::to_string(optionCount), option);
		return true;
	}
	return false;
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
	Value statusFields;
	Value textFields;
	Value boolFields;
	Value intFields;
	Value progressFields;
	Value logFields;
	Value selectFields;

	if (!findCurrentDialog(runtimeKv, dialog)) return definition;
	definition.x = hashReadInt(runtimeKv, dialog, "x", 0);
	definition.y = hashReadInt(runtimeKv, dialog, "y", 0);
	definition.width = hashReadInt(runtimeKv, dialog, "width", 40);
	definition.height = hashReadInt(runtimeKv, dialog, "height", 12);
	definition.title = hashReadString(runtimeKv, dialog, "title");
	readLabelSection(runtimeKv, dialog, "labels", definition.labels);
	readButtonSection(runtimeKv, dialog, "buttons", definition.buttons);
	readDisplaySection(runtimeKv, dialog, "displays", definition.displays);
	if (runtimeKv.findChild(dialog, "statusFields", statusFields)) {
		for (int index = 1; index <= hashReadInt(runtimeKv, statusFields, "count", 0); ++index) {
			Value item;
			const std::string statusId = runtimeKv.findChild(statusFields, std::to_string(index), item) ? hashReadString(runtimeKv, item, "statusId") : std::string();
			const int displayIndex = statusId.empty() ? 0 : hashReadInt(runtimeKv, item, "displayIndex", 0);

			if (displayIndex > 0 && static_cast<std::size_t>(displayIndex) <= definition.displays.size()) definition.statusDisplayIndices[statusId] = displayIndex;
		}
	}
	readCanvasSection(runtimeKv, dialog, "canvases", definition.canvases);
	readCanvasHotspotSection(runtimeKv, dialog, "canvasHotspots", definition.canvasHotspots);
	readInputSection(runtimeKv, dialog, "inputs", definition.inputs);
	if (runtimeKv.findChild(dialog, "textFields", textFields)) {
		for (int index = 1; index <= hashReadInt(runtimeKv, textFields, "count", 0); ++index) {
			Value item;
			MacroUiTextFieldSpec textField;
			const std::string fieldId = runtimeKv.findChild(textFields, std::to_string(index), item) ? hashReadString(runtimeKv, item, "fieldId") : std::string();
			const int inputIndex = fieldId.empty() ? 0 : hashReadInt(runtimeKv, item, "inputIndex", 0);

			if (inputIndex <= 0 || static_cast<std::size_t>(inputIndex) > definition.inputs.size()) continue;
			textField.fieldId = fieldId;
			textField.input = definition.inputs[static_cast<std::size_t>(inputIndex - 1)];
			definition.textFields.push_back(textField);
		}
	}
	if (runtimeKv.findChild(dialog, "boolFields", boolFields)) {
		for (int index = 1; index <= hashReadInt(runtimeKv, boolFields, "count", 0); ++index) {
			Value item;
			MacroUiBoolFieldSpec boolField;

			if (!runtimeKv.findChild(boolFields, std::to_string(index), item)) continue;
			boolField.fieldId = hashReadString(runtimeKv, item, "fieldId");
			boolField.caption = hashReadString(runtimeKv, item, "caption");
			if (boolField.fieldId.empty() || boolField.caption.empty()) continue;
			boolField.x = hashReadInt(runtimeKv, item, "x", 0);
			boolField.y = hashReadInt(runtimeKv, item, "y", 0);
			boolField.value = hashReadInt(runtimeKv, item, "value", 0) != 0;
			definition.boolFields.push_back(boolField);
		}
	}
	if (runtimeKv.findChild(dialog, "intFields", intFields)) {
		for (int index = 1; index <= hashReadInt(runtimeKv, intFields, "count", 0); ++index) {
			Value item;
			MacroUiIntFieldSpec intField;

			if (!runtimeKv.findChild(intFields, std::to_string(index), item)) continue;
			intField.fieldId = hashReadString(runtimeKv, item, "fieldId");
			intField.label = hashReadString(runtimeKv, item, "label");
			if (intField.fieldId.empty() || intField.label.empty()) continue;
			intField.x = hashReadInt(runtimeKv, item, "x", 0);
			intField.y = hashReadInt(runtimeKv, item, "y", 0);
			intField.width = hashReadInt(runtimeKv, item, "width", 8);
			intField.minimum = hashReadInt(runtimeKv, item, "minimum", 0);
			intField.maximum = hashReadInt(runtimeKv, item, "maximum", 100);
			intField.value = hashReadInt(runtimeKv, item, "value", intField.minimum);
			if (intField.minimum > intField.maximum || intField.value < intField.minimum || intField.value > intField.maximum) continue;
			definition.intFields.push_back(intField);
		}
	}
	if (runtimeKv.findChild(dialog, "progressFields", progressFields)) {
		for (int index = 1; index <= hashReadInt(runtimeKv, progressFields, "count", 0); ++index) {
			Value item;
			MacroUiProgressFieldSpec progressField;

			if (!runtimeKv.findChild(progressFields, std::to_string(index), item)) continue;
			progressField.fieldId = hashReadString(runtimeKv, item, "fieldId");
			progressField.label = hashReadString(runtimeKv, item, "label");
			if (progressField.fieldId.empty() || progressField.label.empty()) continue;
			progressField.x = hashReadInt(runtimeKv, item, "x", 0);
			progressField.y = hashReadInt(runtimeKv, item, "y", 0);
			progressField.width = hashReadInt(runtimeKv, item, "width", 16);
			progressField.total = hashReadInt(runtimeKv, item, "total", 100);
			progressField.value = hashReadInt(runtimeKv, item, "value", 0);
			if (progressField.total <= 0 || progressField.value < 0 || progressField.value > progressField.total) continue;
			definition.progressFields.push_back(progressField);
		}
	}
	if (runtimeKv.findChild(dialog, "logFields", logFields)) {
		for (int index = 1; index <= hashReadInt(runtimeKv, logFields, "count", 0); ++index) {
			Value item;
			MacroUiLogFieldSpec logField;

			if (!runtimeKv.findChild(logFields, std::to_string(index), item)) continue;
			logField.logId = hashReadString(runtimeKv, item, "logId");
			logField.label = hashReadString(runtimeKv, item, "label");
			if (logField.logId.empty() || logField.label.empty()) continue;
			logField.x = hashReadInt(runtimeKv, item, "x", 0);
			logField.y = hashReadInt(runtimeKv, item, "y", 0);
			logField.width = hashReadInt(runtimeKv, item, "width", 20);
			logField.height = hashReadInt(runtimeKv, item, "height", 4);
			logField.capacity = hashReadInt(runtimeKv, item, "capacity", 16);
			if (logField.height <= 0 || logField.capacity < logField.height || logField.capacity > 256) continue;
			definition.logFields.push_back(logField);
		}
	}
	if (runtimeKv.findChild(dialog, "selectFields", selectFields)) {
		for (int index = 1; index <= hashReadInt(runtimeKv, selectFields, "count", 0); ++index) {
			Value item;
			Value options;
			MacroUiSelectFieldSpec selectField;

			if (!runtimeKv.findChild(selectFields, std::to_string(index), item)) continue;
			selectField.fieldId = hashReadString(runtimeKv, item, "fieldId");
			selectField.label = hashReadString(runtimeKv, item, "label");
			if (selectField.fieldId.empty() || selectField.label.empty()) continue;
			selectField.x = hashReadInt(runtimeKv, item, "x", 0);
			selectField.y = hashReadInt(runtimeKv, item, "y", 0);
			selectField.width = hashReadInt(runtimeKv, item, "width", 20);
			selectField.height = hashReadInt(runtimeKv, item, "height", 4);
			selectField.value = hashReadString(runtimeKv, item, "value");
			if (runtimeKv.findChild(item, "options", options))
				for (int optionIndex = 1; optionIndex <= hashReadInt(runtimeKv, options, "count", 0); ++optionIndex)
					selectField.options.push_back(hashReadString(runtimeKv, options, std::to_string(optionIndex)));
			definition.selectFields.push_back(selectField);
		}
	}
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
