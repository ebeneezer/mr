#include "MRVMMacroModelessProcedures.hpp"

#include "../conventional/MRVMMacroDialogRuntime.hpp"
#include "MRVMModelessUiRuntime.hpp"
#include "../../vm/MRVMValue.hpp"

#include "MRMacroModelessUi.hpp"
#include "../../mrmac.h"
#include "../../../app/MRRuntimeScheduler.hpp"

#include <algorithm>
#include <stdexcept>

namespace {

using Value = VirtualMachine::Value;

std::string modelessWindowKey(const Value &value) {
	return mrvmModelessUiListKey(mrvmValueAsString(value));
}

int canvasStyle(const Value &value, const std::string &name) {
	const int style = mrvmValueAsInt(value);

	if (style < 0 || style > 3) throw std::runtime_error(name + " expects an MMP canvas style from 0 through 3.");
	return style;
}

void requireCanvasTarget(const std::string &name, const std::vector<Value> &args, std::string &windowId, std::string &canvasId) {
	if (args.size() < 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error(name + " expects a window id and canvas id.");
	windowId = modelessWindowKey(args[0]);
	canvasId = modelessWindowKey(args[1]);
	if (windowId.empty() || canvasId.empty()) throw std::runtime_error(name + " expects non-empty window and canvas ids.");
}

bool canvasCommand(MRVMRuntimeKv &runtimeKv, const std::string &name, const std::vector<Value> &args, int &returnValue, std::string &errorText) {
	MRMacroModelessCanvasCommand command;
	std::string windowId;
	std::string canvasId;
	bool changed = false;

	if (name == "MMP_CANVAS_CLEAR") {
		if (args.size() != 3 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || args[2].type != TYPE_INT) throw std::runtime_error("MMP_CANVAS_CLEAR expects (string, string, int).");
		requireCanvasTarget(name, args, windowId, canvasId);
		changed = mrvmModelessUiCanvasClear(runtimeKv, windowId, canvasId, canvasStyle(args[2], name));
	} else if (name == "MMP_CANVAS_TEXT" || name == "MMP_CANVAS_GLYPH") {
		if (args.size() != 6 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT || !mrvmIsStringLike(args[5])) throw std::runtime_error(name + " expects (string, string, int, int, int, string).");
		requireCanvasTarget(name, args, windowId, canvasId);
		command.type = name == "MMP_CANVAS_TEXT" ? MRMacroModelessCanvasCommandType::Text : MRMacroModelessCanvasCommandType::Glyph;
		command.x = mrvmValueAsInt(args[2]);
		command.y = mrvmValueAsInt(args[3]);
		command.style = canvasStyle(args[4], name);
		command.text = mrvmValueAsString(args[5]);
		if (command.type == MRMacroModelessCanvasCommandType::Glyph && command.text.empty()) throw std::runtime_error("MMP_CANVAS_GLYPH expects a non-empty glyph.");
		changed = mrvmModelessUiCanvasAppendCommand(runtimeKv, windowId, canvasId, command);
	} else if (name == "MMP_CANVAS_LINE") {
		if (args.size() != 8 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT || args[5].type != TYPE_INT || args[6].type != TYPE_INT || !mrvmIsStringLike(args[7])) throw std::runtime_error("MMP_CANVAS_LINE expects (string, string, int, int, int, int, int, string).");
		requireCanvasTarget(name, args, windowId, canvasId);
		command.type = MRMacroModelessCanvasCommandType::Line;
		command.x = mrvmValueAsInt(args[2]);
		command.y = mrvmValueAsInt(args[3]);
		command.x2 = mrvmValueAsInt(args[4]);
		command.y2 = mrvmValueAsInt(args[5]);
		command.style = canvasStyle(args[6], name);
		command.text = mrvmValueAsString(args[7]);
		changed = mrvmModelessUiCanvasAppendCommand(runtimeKv, windowId, canvasId, command);
	} else if (name == "MMP_CANVAS_BOX" || name == "MMP_CANVAS_FILL") {
		if (args.size() != 7 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT || args[5].type != TYPE_INT || args[6].type != TYPE_INT) throw std::runtime_error(name + " expects (string, string, int, int, int, int, int).");
		requireCanvasTarget(name, args, windowId, canvasId);
		command.type = name == "MMP_CANVAS_BOX" ? MRMacroModelessCanvasCommandType::Box : MRMacroModelessCanvasCommandType::Fill;
		command.x = mrvmValueAsInt(args[2]);
		command.y = mrvmValueAsInt(args[3]);
		command.width = std::max(0, mrvmValueAsInt(args[4]));
		command.height = std::max(0, mrvmValueAsInt(args[5]));
		command.style = canvasStyle(args[6], name);
		changed = mrvmModelessUiCanvasAppendCommand(runtimeKv, windowId, canvasId, command);
	} else
		return false;

	returnValue = changed ? 1 : 0;
	if (!changed) errorText = name + " target does not exist or the canvas scene is full.";
	return true;
}

} // namespace

bool mrvmDispatchMacroModelessProcedure(MRVMRuntimeKv &runtimeKv, const std::string &name, const std::vector<Value> &args, int &returnValue, std::string &errorText) {
	MacroUiCanvasSpec canvas;
	MacroUiCanvasHotspotSpec hotspot;
	std::string windowId;
	std::string canvasId;

	returnValue = 0;
	errorText.clear();
	if (name == "MMP_CANVAS") {
		if (args.size() != 5 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || !mrvmIsStringLike(args[4])) throw std::runtime_error("MMP_CANVAS expects (int, int, int, int, string).");
		canvas.x = mrvmValueAsInt(args[0]);
		canvas.y = mrvmValueAsInt(args[1]);
		canvas.width = std::max(1, mrvmValueAsInt(args[2]));
		canvas.height = std::max(1, mrvmValueAsInt(args[3]));
		canvas.canvasId = mrvmModelessUiListKey(mrvmValueAsString(args[4]));
		if (canvas.canvasId.empty()) throw std::runtime_error("MMP_CANVAS expects a non-empty canvas id.");
		mrvmModelessUiAppendCanvas(runtimeKv, canvas);
		returnValue = 1;
		return true;
	}
	if (name == "MMP_CANVAS_HOTSPOT") {
		if (args.size() != 7 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT || args[5].type != TYPE_INT || !mrvmIsStringLike(args[6])) throw std::runtime_error("MMP_CANVAS_HOTSPOT expects (string, int, int, int, int, int, string).");
		hotspot.canvasId = mrvmModelessUiListKey(mrvmValueAsString(args[0]));
		hotspot.x = mrvmValueAsInt(args[1]);
		hotspot.y = mrvmValueAsInt(args[2]);
		hotspot.width = mrvmValueAsInt(args[3]);
		hotspot.height = mrvmValueAsInt(args[4]);
		hotspot.id = mrvmValueAsInt(args[5]);
		hotspot.macroSpec = mrvmValueAsString(args[6]);
		if (hotspot.canvasId.empty() || hotspot.x < 0 || hotspot.y < 0 || hotspot.width <= 0 || hotspot.height <= 0 || hotspot.id <= 0 || hotspot.macroSpec.empty()) throw std::runtime_error("MMP_CANVAS_HOTSPOT expects a declared canvas, non-negative position, positive size and id, and a macro spec.");
		if (!mrvmModelessUiAppendCanvasHotspot(runtimeKv, hotspot)) throw std::runtime_error("MMP_CANVAS_HOTSPOT must fit a declared canvas in the current modeless definition.");
		returnValue = 1;
		return true;
	}
	if (name == "MMP_ACTION_BUTTON") {
		std::vector<Value> bindingArgs;

		if (args.size() != 6 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || !mrvmIsStringLike(args[4]) || !mrvmIsStringLike(args[5])) throw std::runtime_error("MMP_ACTION_BUTTON expects (int, int, int, int, string, string).");
		if (mrvmValueAsInt(args[3]) <= 0 || mrvmValueAsString(args[4]).empty() || mrvmValueAsString(args[5]).empty()) throw std::runtime_error("MMP_ACTION_BUTTON expects a positive control id, a caption and a macro spec.");
		mrvmAddMacroUiButton(runtimeKv, args);
		bindingArgs.push_back(args[3]);
		bindingArgs.push_back(args[5]);
		mrvmBindMacroModelessButton(runtimeKv, bindingArgs);
		returnValue = 1;
		return true;
	}
	if (name == "MMP_MENU_CLEAR") {
		std::vector<Value> clearArgs;

		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("MMP_MENU_CLEAR expects one string menu id.");
		if (modelessWindowKey(args[0]).empty()) throw std::runtime_error("MMP_MENU_CLEAR expects a non-empty menu id.");
		clearArgs.push_back(args[0]);
		mrvmClearMacroUiItemList(runtimeKv, clearArgs);
		returnValue = 1;
		return true;
	}
	if (name == "MMP_MENU_ITEM") {
		std::vector<Value> itemArgs;
		std::string label;
		std::string value;
		std::string detail;
		std::string item;

		if (args.size() != 4 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || !mrvmIsStringLike(args[2]) || !mrvmIsStringLike(args[3])) throw std::runtime_error("MMP_MENU_ITEM expects (string, string, string, string).");
		label = mrvmValueAsString(args[1]);
		value = mrvmValueAsString(args[2]);
		detail = mrvmValueAsString(args[3]);
		if (modelessWindowKey(args[0]).empty() || label.empty() || value.empty() || label.find('\t') != std::string::npos || value.find('\t') != std::string::npos || detail.find('\t') != std::string::npos) throw std::runtime_error("MMP_MENU_ITEM expects non-empty menu id, label and value without tab characters.");
		item = label + "\t" + value;
		if (!detail.empty()) item += "\t" + detail;
		itemArgs.push_back(args[0]);
		itemArgs.push_back(mrvmMakeString(item));
		mrvmAddMacroUiItemListValue(runtimeKv, itemArgs);
		returnValue = 1;
		return true;
	}
	if (name == "MMP_ACTION_MENU") {
		std::vector<Value> gridArgs;
		std::vector<Value> bindingArgs;

		if (args.size() != 8 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT || !mrvmIsStringLike(args[5]) || !mrvmIsStringLike(args[6]) || !mrvmIsStringLike(args[7])) throw std::runtime_error("MMP_ACTION_MENU expects (int, int, int, int, int, string, string, string).");
		if (mrvmValueAsInt(args[4]) <= 0 || modelessWindowKey(args[6]).empty() || mrvmValueAsString(args[7]).empty()) throw std::runtime_error("MMP_ACTION_MENU expects a positive control id, a menu id and a macro spec.");
		gridArgs.push_back(args[0]);
		gridArgs.push_back(args[1]);
		gridArgs.push_back(args[2]);
		gridArgs.push_back(args[3]);
		gridArgs.push_back(args[4]);
		gridArgs.push_back(args[5]);
		gridArgs.push_back(args[6]);
		gridArgs.push_back(mrvmMakeInt(1));
		mrvmAddMacroUiGrid(runtimeKv, gridArgs);
		bindingArgs.push_back(args[4]);
		bindingArgs.push_back(args[7]);
		mrvmBindMacroModelessButton(runtimeKv, bindingArgs);
		returnValue = 1;
		return true;
	}
	if (name == "MMP_TEXT_FIELD") {
		MacroUiInputSpec input;
		std::string fieldId;

		if (args.size() != 6 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || !mrvmIsStringLike(args[3]) || !mrvmIsStringLike(args[4]) || !mrvmIsStringLike(args[5])) throw std::runtime_error("MMP_TEXT_FIELD expects (int, int, int, string, string, string).");
		fieldId = modelessWindowKey(args[3]);
		if (fieldId.empty() || mrvmValueAsString(args[4]).empty()) throw std::runtime_error("MMP_TEXT_FIELD expects non-empty field id and label.");
		input.x = mrvmValueAsInt(args[0]);
		input.y = mrvmValueAsInt(args[1]);
		input.width = std::max(4, mrvmValueAsInt(args[2]));
		input.label = mrvmValueAsString(args[4]);
		input.text = mrvmValueAsString(args[5]);
		if (!mrvmModelessUiAppendTextField(runtimeKv, fieldId, input)) throw std::runtime_error("MMP_TEXT_FIELD field id already exists in the current modeless definition.");
		returnValue = 1;
		return true;
	}
	if (name == "MMP_TEXT_SET") {
		std::string fieldId;
		std::string currentText;

		if (args.size() != 3 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || !mrvmIsStringLike(args[2])) throw std::runtime_error("MMP_TEXT_SET expects (string, string, string).");
		windowId = modelessWindowKey(args[0]);
		fieldId = modelessWindowKey(args[1]);
		if (windowId.empty() || fieldId.empty()) throw std::runtime_error("MMP_TEXT_SET expects non-empty window and field ids.");
		if (!mrvmModelessUiReadWindowTextFieldValue(runtimeKv, windowId, fieldId, currentText)) {
			errorText = "MMP_TEXT_SET target does not exist.";
			return true;
		}
		returnValue = updateMacroModelessTextField(windowId, fieldId, mrvmValueAsString(args[2])) ? 1 : 0;
		if (returnValue == 0) errorText = "MMP_TEXT_SET window does not exist.";
		return true;
	}
	if (name == "MMP_BOOL_FIELD") {
		MacroUiBoolFieldSpec boolField;

		if (args.size() != 5 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || !mrvmIsStringLike(args[2]) || !mrvmIsStringLike(args[3]) || args[4].type != TYPE_INT) throw std::runtime_error("MMP_BOOL_FIELD expects (int, int, string, string, int).");
		boolField.fieldId = modelessWindowKey(args[2]);
		boolField.caption = mrvmValueAsString(args[3]);
		if (boolField.fieldId.empty() || boolField.caption.empty()) throw std::runtime_error("MMP_BOOL_FIELD expects non-empty field id and caption.");
		boolField.x = mrvmValueAsInt(args[0]);
		boolField.y = mrvmValueAsInt(args[1]);
		boolField.value = mrvmValueAsInt(args[4]) != 0;
		if (!mrvmModelessUiAppendBoolField(runtimeKv, boolField)) throw std::runtime_error("MMP_BOOL_FIELD field id already exists in the current modeless definition.");
		returnValue = 1;
		return true;
	}
	if (name == "MMP_BOOL_SET") {
		bool currentValue = false;
		std::string fieldId;

		if (args.size() != 3 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || args[2].type != TYPE_INT) throw std::runtime_error("MMP_BOOL_SET expects (string, string, int).");
		windowId = modelessWindowKey(args[0]);
		fieldId = modelessWindowKey(args[1]);
		if (windowId.empty() || fieldId.empty()) throw std::runtime_error("MMP_BOOL_SET expects non-empty window and field ids.");
		if (!mrvmModelessUiReadWindowBoolFieldValue(runtimeKv, windowId, fieldId, currentValue)) {
			errorText = "MMP_BOOL_SET target does not exist.";
			return true;
		}
		returnValue = updateMacroModelessBoolField(windowId, fieldId, mrvmValueAsInt(args[2]) != 0) ? 1 : 0;
		if (returnValue == 0) errorText = "MMP_BOOL_SET window does not exist.";
		return true;
	}
	if (name == "MMP_INT_FIELD") {
		MacroUiIntFieldSpec intField;

		if (args.size() != 8 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || !mrvmIsStringLike(args[3]) || !mrvmIsStringLike(args[4]) || args[5].type != TYPE_INT || args[6].type != TYPE_INT || args[7].type != TYPE_INT) throw std::runtime_error("MMP_INT_FIELD expects (int, int, int, string, string, int, int, int).");
		intField.fieldId = modelessWindowKey(args[3]);
		intField.label = mrvmValueAsString(args[4]);
		if (intField.fieldId.empty() || intField.label.empty()) throw std::runtime_error("MMP_INT_FIELD expects non-empty field id and label.");
		intField.x = mrvmValueAsInt(args[0]);
		intField.y = mrvmValueAsInt(args[1]);
		intField.width = std::max(4, mrvmValueAsInt(args[2]));
		intField.value = mrvmValueAsInt(args[5]);
		intField.minimum = mrvmValueAsInt(args[6]);
		intField.maximum = mrvmValueAsInt(args[7]);
		if (intField.minimum > intField.maximum || intField.value < intField.minimum || intField.value > intField.maximum) throw std::runtime_error("MMP_INT_FIELD expects an initial value within its inclusive range.");
		if (!mrvmModelessUiAppendIntField(runtimeKv, intField)) throw std::runtime_error("MMP_INT_FIELD field id already exists in the current modeless definition.");
		returnValue = 1;
		return true;
	}
	if (name == "MMP_INT_SET") {
		int currentValue = 0;
		std::string fieldId;
		int value = 0;

		if (args.size() != 3 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || args[2].type != TYPE_INT) throw std::runtime_error("MMP_INT_SET expects (string, string, int).");
		windowId = modelessWindowKey(args[0]);
		fieldId = modelessWindowKey(args[1]);
		value = mrvmValueAsInt(args[2]);
		if (windowId.empty() || fieldId.empty()) throw std::runtime_error("MMP_INT_SET expects non-empty window and field ids.");
		if (!mrvmModelessUiReadWindowIntFieldValue(runtimeKv, windowId, fieldId, currentValue)) {
			errorText = "MMP_INT_SET target does not exist.";
			return true;
		}
		if (!mrvmModelessUiStoreWindowIntFieldValue(runtimeKv, windowId, fieldId, value)) {
			errorText = "MMP_INT_SET value is outside the declared range.";
			return true;
		}
		returnValue = updateMacroModelessIntField(windowId, fieldId, value) ? 1 : 0;
		if (returnValue == 0) errorText = "MMP_INT_SET window does not exist.";
		return true;
	}
	if (name == "MMP_PROGRESS_FIELD") {
		MacroUiProgressFieldSpec progressField;

		if (args.size() != 7 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || !mrvmIsStringLike(args[3]) || !mrvmIsStringLike(args[4]) || args[5].type != TYPE_INT || args[6].type != TYPE_INT) throw std::runtime_error("MMP_PROGRESS_FIELD expects (int, int, int, string, string, int, int).");
		progressField.fieldId = modelessWindowKey(args[3]);
		progressField.label = mrvmValueAsString(args[4]);
		if (progressField.fieldId.empty() || progressField.label.empty()) throw std::runtime_error("MMP_PROGRESS_FIELD expects non-empty field id and label.");
		progressField.x = mrvmValueAsInt(args[0]);
		progressField.y = mrvmValueAsInt(args[1]);
		progressField.width = std::max(8, mrvmValueAsInt(args[2]));
		progressField.total = mrvmValueAsInt(args[5]);
		progressField.value = mrvmValueAsInt(args[6]);
		if (progressField.total <= 0 || progressField.value < 0 || progressField.value > progressField.total) throw std::runtime_error("MMP_PROGRESS_FIELD expects a positive total and an initial value within its range.");
		if (!mrvmModelessUiAppendProgressField(runtimeKv, progressField)) throw std::runtime_error("MMP_PROGRESS_FIELD field id already exists in the current modeless definition.");
		returnValue = 1;
		return true;
	}
	if (name == "MMP_PROGRESS_SET") {
		int total = 0;
		int currentValue = 0;
		std::string fieldId;
		int value = 0;

		if (args.size() != 3 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || args[2].type != TYPE_INT) throw std::runtime_error("MMP_PROGRESS_SET expects (string, string, int).");
		windowId = modelessWindowKey(args[0]);
		fieldId = modelessWindowKey(args[1]);
		value = mrvmValueAsInt(args[2]);
		if (windowId.empty() || fieldId.empty()) throw std::runtime_error("MMP_PROGRESS_SET expects non-empty window and field ids.");
		if (!mrvmModelessUiReadWindowProgressFieldValue(runtimeKv, windowId, fieldId, total, currentValue)) {
			errorText = "MMP_PROGRESS_SET target does not exist.";
			return true;
		}
		if (!mrvmModelessUiStoreWindowProgressFieldValue(runtimeKv, windowId, fieldId, value)) {
			errorText = "MMP_PROGRESS_SET value is outside the declared range.";
			return true;
		}
		returnValue = updateMacroModelessProgressField(windowId, fieldId) ? 1 : 0;
		if (returnValue == 0) errorText = "MMP_PROGRESS_SET window does not exist.";
		return true;
	}
	if (name == "MMP_LOG_FIELD") {
		MacroUiLogFieldSpec logField;

		if (args.size() != 7 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || !mrvmIsStringLike(args[4]) || !mrvmIsStringLike(args[5]) || args[6].type != TYPE_INT) throw std::runtime_error("MMP_LOG_FIELD expects (int, int, int, int, string, string, int).");
		logField.logId = modelessWindowKey(args[4]);
		logField.label = mrvmValueAsString(args[5]);
		if (logField.logId.empty() || logField.label.empty()) throw std::runtime_error("MMP_LOG_FIELD expects non-empty log id and label.");
		logField.x = mrvmValueAsInt(args[0]);
		logField.y = mrvmValueAsInt(args[1]);
		logField.width = std::max(8, mrvmValueAsInt(args[2]));
		logField.height = mrvmValueAsInt(args[3]);
		logField.capacity = mrvmValueAsInt(args[6]);
		if (logField.height <= 0 || logField.capacity < logField.height || logField.capacity > 256) throw std::runtime_error("MMP_LOG_FIELD expects a positive height and a capacity from height through 256.");
		if (!mrvmModelessUiAppendLogField(runtimeKv, logField)) throw std::runtime_error("MMP_LOG_FIELD log id already exists in the current modeless definition.");
		returnValue = 1;
		return true;
	}
	if (name == "MMP_LOG_APPEND") {
		int count = 0;
		std::string logId;
		std::string text;

		if (args.size() != 3 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || !mrvmIsStringLike(args[2])) throw std::runtime_error("MMP_LOG_APPEND expects (string, string, string).");
		windowId = modelessWindowKey(args[0]);
		logId = modelessWindowKey(args[1]);
		text = mrvmValueAsString(args[2]);
		if (windowId.empty() || logId.empty()) throw std::runtime_error("MMP_LOG_APPEND expects non-empty window and log ids.");
		if (text.size() > 512 || text.find_first_of("\r\n") != std::string::npos) throw std::runtime_error("MMP_LOG_APPEND expects one text line up to 512 bytes.");
		if (!mrvmModelessUiReadWindowLogFieldCount(runtimeKv, windowId, logId, count)) {
			errorText = "MMP_LOG_APPEND target does not exist.";
			return true;
		}
		if (!mrvmModelessUiAppendWindowLogFieldLine(runtimeKv, windowId, logId, text)) {
			errorText = "MMP_LOG_APPEND target does not exist.";
			return true;
		}
		returnValue = updateMacroModelessLogField(windowId, logId) ? 1 : 0;
		if (returnValue == 0) errorText = "MMP_LOG_APPEND window does not exist.";
		return true;
	}
	if (name == "MMP_LOG_CLEAR") {
		int count = 0;
		std::string logId;

		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("MMP_LOG_CLEAR expects (string, string).");
		windowId = modelessWindowKey(args[0]);
		logId = modelessWindowKey(args[1]);
		if (windowId.empty() || logId.empty()) throw std::runtime_error("MMP_LOG_CLEAR expects non-empty window and log ids.");
		if (!mrvmModelessUiReadWindowLogFieldCount(runtimeKv, windowId, logId, count)) {
			errorText = "MMP_LOG_CLEAR target does not exist.";
			return true;
		}
		if (!mrvmModelessUiClearWindowLogField(runtimeKv, windowId, logId)) {
			errorText = "MMP_LOG_CLEAR target does not exist.";
			return true;
		}
		returnValue = updateMacroModelessLogField(windowId, logId) ? 1 : 0;
		if (returnValue == 0) errorText = "MMP_LOG_CLEAR window does not exist.";
		return true;
	}
	if (name == "MMP_SELECT_FIELD") {
		MacroUiSelectFieldSpec selectField;

		if (args.size() != 7 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || !mrvmIsStringLike(args[4]) || !mrvmIsStringLike(args[5]) || !mrvmIsStringLike(args[6])) throw std::runtime_error("MMP_SELECT_FIELD expects (int, int, int, int, string, string, string).");
		selectField.fieldId = modelessWindowKey(args[4]);
		selectField.label = mrvmValueAsString(args[5]);
		if (selectField.fieldId.empty() || selectField.label.empty()) throw std::runtime_error("MMP_SELECT_FIELD expects non-empty field id and label.");
		selectField.x = mrvmValueAsInt(args[0]);
		selectField.y = mrvmValueAsInt(args[1]);
		selectField.width = std::max(8, mrvmValueAsInt(args[2]));
		selectField.height = std::max(2, mrvmValueAsInt(args[3]));
		selectField.value = mrvmValueAsString(args[6]);
		if (!mrvmModelessUiAppendSelectField(runtimeKv, selectField)) throw std::runtime_error("MMP_SELECT_FIELD field id already exists in the current modeless definition.");
		returnValue = 1;
		return true;
	}
	if (name == "MMP_SELECT_OPTION") {
		std::string fieldId;
		std::string option;

		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("MMP_SELECT_OPTION expects (string, string).");
		fieldId = modelessWindowKey(args[0]);
		option = mrvmValueAsString(args[1]);
		if (fieldId.empty() || option.empty()) throw std::runtime_error("MMP_SELECT_OPTION expects non-empty field id and option.");
		if (!mrvmModelessUiAppendSelectOption(runtimeKv, fieldId, option)) throw std::runtime_error("MMP_SELECT_OPTION requires a declared field and a unique option.");
		returnValue = 1;
		return true;
	}
	if (name == "MMP_SELECT_SET") {
		std::string currentValue;
		std::string fieldId;

		if (args.size() != 3 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || !mrvmIsStringLike(args[2])) throw std::runtime_error("MMP_SELECT_SET expects (string, string, string).");
		windowId = modelessWindowKey(args[0]);
		fieldId = modelessWindowKey(args[1]);
		if (windowId.empty() || fieldId.empty()) throw std::runtime_error("MMP_SELECT_SET expects non-empty window and field ids.");
		if (!mrvmModelessUiReadWindowSelectFieldValue(runtimeKv, windowId, fieldId, currentValue)) {
			errorText = "MMP_SELECT_SET target does not exist.";
			return true;
		}
		if (!mrvmModelessUiStoreWindowSelectFieldValue(runtimeKv, windowId, fieldId, mrvmValueAsString(args[2]))) {
			errorText = "MMP_SELECT_SET value is not a declared option.";
			return true;
		}
		returnValue = updateMacroModelessSelectField(windowId, fieldId, mrvmValueAsString(args[2])) ? 1 : 0;
		if (returnValue == 0) errorText = "MMP_SELECT_SET window does not exist.";
		return true;
	}
	if (name == "MMP_STATUS_FIELD") {
		MacroUiDisplaySpec display;
		std::string statusId;

		if (args.size() != 5 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || !mrvmIsStringLike(args[3]) || !mrvmIsStringLike(args[4])) throw std::runtime_error("MMP_STATUS_FIELD expects (int, int, int, string, string).");
		statusId = modelessWindowKey(args[3]);
		if (statusId.empty()) throw std::runtime_error("MMP_STATUS_FIELD expects a non-empty status id.");
		display.x = mrvmValueAsInt(args[0]);
		display.y = mrvmValueAsInt(args[1]);
		display.width = std::max(4, mrvmValueAsInt(args[2]));
		display.text = mrvmValueAsString(args[4]);
		if (!mrvmModelessUiAppendStatusField(runtimeKv, statusId, display)) throw std::runtime_error("MMP_STATUS_FIELD status id already exists in the current modeless definition.");
		returnValue = 1;
		return true;
	}
	if (name == "MMP_STATUS_SET") {
		int displayIndex = 0;
		std::string statusId;

		if (args.size() != 3 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || !mrvmIsStringLike(args[2])) throw std::runtime_error("MMP_STATUS_SET expects (string, string, string).");
		windowId = modelessWindowKey(args[0]);
		statusId = modelessWindowKey(args[1]);
		if (windowId.empty() || statusId.empty()) throw std::runtime_error("MMP_STATUS_SET expects non-empty window and status ids.");
		if (!mrvmModelessUiReadWindowStatusDisplayIndex(runtimeKv, windowId, statusId, displayIndex)) {
			errorText = "MMP_STATUS_SET target does not exist.";
			return true;
		}
		returnValue = updateMacroModelessDisplay(windowId, displayIndex, mrvmValueAsString(args[2])) ? 1 : 0;
		if (returnValue == 0) errorText = "MMP_STATUS_SET window does not exist.";
		return true;
	}
	if (name == "MMP_TIMER_START") {
		MRRuntimeScheduledConsumerConfig config;
		std::string timerId;
		int intervalMs = 0;

		if (args.size() != 4 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || args[2].type != TYPE_INT || !mrvmIsStringLike(args[3])) throw std::runtime_error("MMP_TIMER_START expects (string, string, int, string).");
		windowId = modelessWindowKey(args[0]);
		timerId = modelessWindowKey(args[1]);
		intervalMs = mrvmValueAsInt(args[2]);
		config.macroSpec = mrvmValueAsString(args[3]);
		if (windowId.empty() || timerId.empty() || config.macroSpec.empty() || intervalMs < 100) throw std::runtime_error("MMP_TIMER_START expects non-empty window and timer ids, a macro spec, and an interval of at least 100 ms.");
		if (!mrvmModelessUiWindowExists(runtimeKv, windowId)) {
			errorText = "MMP_TIMER_START target does not exist.";
			return true;
		}
		config.intervalMs = static_cast<std::uint64_t>(intervalMs);
		config.owner.modelessWindowId = windowId;
		config.consumerKey = timerId;
		removeRuntimeScheduledConsumersForOwnerAndKey(config.owner, config.consumerKey);
		returnValue = registerRuntimeScheduledConsumer(config) != 0 ? 1 : 0;
		if (returnValue == 0) errorText = "MMP_TIMER_START could not register the scheduler consumer.";
		return true;
	}
	if (name == "MMP_TIMER_STOP") {
		MRMacroExecutionOwner owner;
		std::string timerId;

		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("MMP_TIMER_STOP expects (string, string).");
		windowId = modelessWindowKey(args[0]);
		timerId = modelessWindowKey(args[1]);
		if (windowId.empty() || timerId.empty()) throw std::runtime_error("MMP_TIMER_STOP expects non-empty window and timer ids.");
		owner.modelessWindowId = windowId;
		returnValue = removeRuntimeScheduledConsumersForOwnerAndKey(owner, timerId) != 0 ? 1 : 0;
		return true;
	}
	if (canvasCommand(runtimeKv, name, args, returnValue, errorText)) return true;
	if (name != "MMP_CANVAS_COMMIT") return false;
	if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("MMP_CANVAS_COMMIT expects (string, string).");
	requireCanvasTarget(name, args, windowId, canvasId);
	if (!mrvmModelessUiCommitCanvas(runtimeKv, windowId, canvasId)) {
		errorText = "MMP_CANVAS_COMMIT target does not exist.";
		return true;
	}
	returnValue = commitMacroModelessCanvas(windowId, canvasId) ? 1 : 0;
	if (returnValue == 0) errorText = "MMP_CANVAS_COMMIT canvas view does not exist.";
	return true;
}

bool mrvmDispatchMacroModelessIntrinsic(MRVMRuntimeKv &runtimeKv, const std::string &name, const std::vector<Value> &args, Value &result) {
	MRMacroModelessWindowGeometry geometry;
	const std::string windowId = args.empty() ? std::string() : modelessWindowKey(args[0]);
	int style = 0;

	if (name == "MMP_STYLE_SURFACE" || name == "MMP_STYLE_TEXT" || name == "MMP_STYLE_ACCENT" || name == "MMP_STYLE_MUTED") {
		if (!args.empty()) throw std::runtime_error(name + " expects no arguments.");
		if (name == "MMP_STYLE_TEXT") style = 1;
		else if (name == "MMP_STYLE_ACCENT")
			style = 2;
		else if (name == "MMP_STYLE_MUTED")
			style = 3;
		result = mrvmMakeInt(style);
		return true;
	}
	if (name == "MMP_WINDOW_INSTANCE") {
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("MMP_WINDOW_INSTANCE expects one string prefix.");
		const std::string prefix = modelessWindowKey(args[0]);

		if (prefix.empty()) throw std::runtime_error("MMP_WINDOW_INSTANCE expects a non-empty prefix.");
		result = mrvmMakeString(mrvmModelessUiCreateWindowInstanceId(runtimeKv, prefix));
		return true;
	}
	if (name == "MMP_TEXT_VALUE") {
		std::string fieldId;
		std::string text;

		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("MMP_TEXT_VALUE expects a window id and field id.");
		fieldId = modelessWindowKey(args[1]);
		if (windowId.empty() || fieldId.empty()) throw std::runtime_error("MMP_TEXT_VALUE expects non-empty window and field ids.");
		if (!mrvmModelessUiReadWindowTextFieldValue(runtimeKv, windowId, fieldId, text)) throw std::runtime_error("MMP_TEXT_VALUE target does not exist.");
		result = mrvmMakeString(text);
		return true;
	}
	if (name == "MMP_BOOL_VALUE") {
		bool value = false;
		std::string fieldId;

		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("MMP_BOOL_VALUE expects a window id and field id.");
		fieldId = modelessWindowKey(args[1]);
		if (windowId.empty() || fieldId.empty()) throw std::runtime_error("MMP_BOOL_VALUE expects non-empty window and field ids.");
		if (!mrvmModelessUiReadWindowBoolFieldValue(runtimeKv, windowId, fieldId, value)) throw std::runtime_error("MMP_BOOL_VALUE target does not exist.");
		result = mrvmMakeInt(value ? 1 : 0);
		return true;
	}
	if (name == "MMP_INT_VALUE") {
		int value = 0;
		std::string fieldId;

		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("MMP_INT_VALUE expects a window id and field id.");
		fieldId = modelessWindowKey(args[1]);
		if (windowId.empty() || fieldId.empty()) throw std::runtime_error("MMP_INT_VALUE expects non-empty window and field ids.");
		if (!mrvmModelessUiReadWindowIntFieldValue(runtimeKv, windowId, fieldId, value)) throw std::runtime_error("MMP_INT_VALUE target does not exist.");
		result = mrvmMakeInt(value);
		return true;
	}
	if (name == "MMP_PROGRESS_VALUE") {
		int total = 0;
		int value = 0;
		std::string fieldId;

		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("MMP_PROGRESS_VALUE expects a window id and field id.");
		fieldId = modelessWindowKey(args[1]);
		if (windowId.empty() || fieldId.empty()) throw std::runtime_error("MMP_PROGRESS_VALUE expects non-empty window and field ids.");
		if (!mrvmModelessUiReadWindowProgressFieldValue(runtimeKv, windowId, fieldId, total, value)) throw std::runtime_error("MMP_PROGRESS_VALUE target does not exist.");
		result = mrvmMakeInt(value);
		return true;
	}
	if (name == "MMP_LOG_COUNT") {
		int count = 0;
		std::string logId;

		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("MMP_LOG_COUNT expects a window id and log id.");
		logId = modelessWindowKey(args[1]);
		if (windowId.empty() || logId.empty()) throw std::runtime_error("MMP_LOG_COUNT expects non-empty window and log ids.");
		if (!mrvmModelessUiReadWindowLogFieldCount(runtimeKv, windowId, logId, count)) throw std::runtime_error("MMP_LOG_COUNT target does not exist.");
		result = mrvmMakeInt(count);
		return true;
	}
	if (name == "MMP_SELECT_VALUE") {
		std::string fieldId;
		std::string value;

		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("MMP_SELECT_VALUE expects a window id and field id.");
		fieldId = modelessWindowKey(args[1]);
		if (windowId.empty() || fieldId.empty()) throw std::runtime_error("MMP_SELECT_VALUE expects non-empty window and field ids.");
		if (!mrvmModelessUiReadWindowSelectFieldValue(runtimeKv, windowId, fieldId, value)) throw std::runtime_error("MMP_SELECT_VALUE target does not exist.");
		result = mrvmMakeString(value);
		return true;
	}

	if (name != "MMP_WINDOW_EXISTS" && name != "MMP_WINDOW_X" && name != "MMP_WINDOW_Y" && name != "MMP_WINDOW_WIDTH" && name != "MMP_WINDOW_HEIGHT") return false;
	if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error(name + " expects one window id.");
	if (name == "MMP_WINDOW_EXISTS") {
		result = mrvmMakeInt(mrvmModelessUiWindowExists(runtimeKv, windowId) ? 1 : 0);
		return true;
	}
	if (!mrvmModelessUiReadWindowGeometry(runtimeKv, windowId, geometry)) {
		result = mrvmMakeInt(0);
		return true;
	}
	if (name == "MMP_WINDOW_X") result = mrvmMakeInt(geometry.x);
	else if (name == "MMP_WINDOW_Y") result = mrvmMakeInt(geometry.y);
	else if (name == "MMP_WINDOW_WIDTH") result = mrvmMakeInt(geometry.width);
	else result = mrvmMakeInt(geometry.height);
	return true;
}

static MRMacroModelessSelectionSpec modelessSelectionSpec(const MacroUiSelectionSpec &source, const std::map<int, std::string> &macros) {
	MRMacroModelessSelectionSpec target;
	std::map<int, std::string>::const_iterator macroIt = macros.find(source.id);

	target.x = source.x;
	target.y = source.y;
	target.width = source.width;
	target.height = source.height;
	target.id = source.id;
	target.label = source.label;
	target.itemSpec = source.itemSpec;
	target.macroSpec = macroIt != macros.end() ? macroIt->second : std::string();
	target.start = source.start;
	return target;
}

MRMacroModelessWindowDefinition mrvmBuildMacroModelessDefinition(MRVMRuntimeKv &runtimeKv, const std::string &windowId) {
	MRMacroModelessWindowDefinition definition;
	MacroUiDialogDefinition dialog = mrvmModelessUiReadDialogDefinition(runtimeKv);

	definition.x = dialog.x;
	definition.y = dialog.y;
	definition.width = dialog.width;
	definition.height = dialog.height;
	definition.windowId = windowId;
	definition.title = dialog.title;
	definition.statusDisplayIndices = dialog.statusDisplayIndices;
	for (std::size_t index = 0; index < dialog.labels.size(); ++index) {
		const MacroUiLabelSpec &source = dialog.labels[index];
		MRMacroModelessLabelSpec target;

		target.x = source.x;
		target.y = source.y;
		target.text = source.text;
		definition.labels.push_back(target);
	}
	for (std::size_t index = 0; index < dialog.displays.size(); ++index) {
		const MacroUiDisplaySpec &source = dialog.displays[index];
		MRMacroModelessDisplaySpec target;

		target.x = source.x;
		target.y = source.y;
		target.width = source.width;
		target.text = source.text;
		definition.displays.push_back(target);
	}
	for (std::size_t index = 0; index < dialog.textFields.size(); ++index) {
		const MacroUiTextFieldSpec &source = dialog.textFields[index];
		MRMacroModelessTextFieldSpec target;

		target.x = source.input.x;
		target.y = source.input.y;
		target.width = source.input.width;
		target.fieldId = source.fieldId;
		target.label = source.input.label;
		target.text = source.input.text;
		definition.textFields.push_back(target);
	}
	for (std::size_t index = 0; index < dialog.boolFields.size(); ++index) {
		const MacroUiBoolFieldSpec &source = dialog.boolFields[index];
		MRMacroModelessBoolFieldSpec target;

		target.x = source.x;
		target.y = source.y;
		target.fieldId = source.fieldId;
		target.caption = source.caption;
		target.value = source.value;
		definition.boolFields.push_back(target);
	}
	for (std::size_t index = 0; index < dialog.intFields.size(); ++index) {
		const MacroUiIntFieldSpec &source = dialog.intFields[index];
		MRMacroModelessIntFieldSpec target;

		target.x = source.x;
		target.y = source.y;
		target.width = source.width;
		target.fieldId = source.fieldId;
		target.label = source.label;
		target.minimum = source.minimum;
		target.maximum = source.maximum;
		target.value = source.value;
		definition.intFields.push_back(target);
	}
	for (std::size_t index = 0; index < dialog.progressFields.size(); ++index) {
		const MacroUiProgressFieldSpec &source = dialog.progressFields[index];
		MRMacroModelessProgressFieldSpec target;

		target.x = source.x;
		target.y = source.y;
		target.width = source.width;
		target.fieldId = source.fieldId;
		target.label = source.label;
		target.total = source.total;
		target.value = source.value;
		definition.progressFields.push_back(target);
	}
	for (std::size_t index = 0; index < dialog.logFields.size(); ++index) {
		const MacroUiLogFieldSpec &source = dialog.logFields[index];
		MRMacroModelessLogFieldSpec target;

		target.x = source.x;
		target.y = source.y;
		target.width = source.width;
		target.height = source.height;
		target.logId = source.logId;
		target.label = source.label;
		target.capacity = source.capacity;
		definition.logFields.push_back(target);
	}
	for (std::size_t index = 0; index < dialog.selectFields.size(); ++index) {
		const MacroUiSelectFieldSpec &source = dialog.selectFields[index];
		MRMacroModelessSelectFieldSpec target;

		target.x = source.x;
		target.y = source.y;
		target.width = source.width;
		target.height = source.height;
		target.fieldId = source.fieldId;
		target.label = source.label;
		target.options = source.options;
		target.value = source.value;
		if (std::find(target.options.begin(), target.options.end(), target.value) == target.options.end())
			target.value = target.options.empty() ? std::string() : target.options.front();
		definition.selectFields.push_back(target);
	}
	for (std::size_t index = 0; index < dialog.canvases.size(); ++index) {
		const MacroUiCanvasSpec &source = dialog.canvases[index];
		MRMacroModelessCanvasSpec target;

		target.x = source.x;
		target.y = source.y;
		target.width = source.width;
		target.height = source.height;
		target.canvasId = source.canvasId;
		definition.canvases.push_back(target);
	}
	for (std::size_t index = 0; index < dialog.canvasHotspots.size(); ++index) {
		const MacroUiCanvasHotspotSpec &source = dialog.canvasHotspots[index];
		MRMacroModelessCanvasHotspotSpec target;

		target.canvasId = source.canvasId;
		target.x = source.x;
		target.y = source.y;
		target.width = source.width;
		target.height = source.height;
		target.id = source.id;
		target.macroSpec = source.macroSpec;
		definition.canvasHotspots.push_back(target);
	}
	for (std::size_t index = 0; index < dialog.listBoxes.size(); ++index) {
		const MacroUiListBoxSpec &source = dialog.listBoxes[index];
		MRMacroModelessListBoxSpec target;

		target.x = source.x;
		target.y = source.y;
		target.width = source.width;
		target.height = source.height;
		target.id = source.id;
		target.label = source.label;
		target.itemSpec = source.itemSpec;
		target.start = source.start;
		definition.listBoxes.push_back(target);
	}
	for (std::size_t index = 0; index < dialog.grids.size(); ++index) {
		const MacroUiGridSpec &source = dialog.grids[index];
		MRMacroModelessGridSpec target;
		std::map<int, std::string>::const_iterator macroIt = dialog.modelessButtonMacros.find(source.id);

		target.x = source.x;
		target.y = source.y;
		target.width = source.width;
		target.height = source.height;
		target.id = source.id;
		target.label = source.label;
		target.itemSpec = source.itemSpec;
		target.macroSpec = macroIt != dialog.modelessButtonMacros.end() ? macroIt->second : std::string();
		target.start = source.start;
		definition.grids.push_back(target);
	}
	for (std::size_t index = 0; index < dialog.trees.size(); ++index)
		definition.trees.push_back(modelessSelectionSpec(dialog.trees[index], dialog.modelessButtonMacros));
	for (std::size_t index = 0; index < dialog.tables.size(); ++index)
		definition.tables.push_back(modelessSelectionSpec(dialog.tables[index], dialog.modelessButtonMacros));
	for (std::size_t index = 0; index < dialog.buttons.size(); ++index) {
		const MacroUiButtonSpec &source = dialog.buttons[index];
		MRMacroModelessButtonSpec target;
		std::map<int, std::string>::const_iterator macroIt = dialog.modelessButtonMacros.find(source.id);

		target.x = source.x;
		target.y = source.y;
		target.width = source.width;
		target.id = source.id;
		target.text = source.text;
		target.macroSpec = macroIt != dialog.modelessButtonMacros.end() ? macroIt->second : std::string();
		definition.buttons.push_back(target);
	}
	return definition;
}
