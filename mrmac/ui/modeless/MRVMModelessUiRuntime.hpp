#ifndef MRVM_MODELESS_UI_RUNTIME_HPP
#define MRVM_MODELESS_UI_RUNTIME_HPP

#include "../../vm/MRVMRuntimeKv.hpp"

#include "MRMacroModelessUi.hpp"

#include <map>
#include <string>
#include <vector>

struct MacroUiLabelSpec {
	int x = 0;
	int y = 0;
	std::string text;
};

struct MacroUiButtonSpec {
	int x = 0;
	int y = 0;
	int width = 8;
	int id = 0;
	std::string text;
};

struct MacroUiDisplaySpec {
	int x = 0;
	int y = 0;
	int width = 20;
	std::string text;
};

struct MacroUiCanvasSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	int height = 6;
	std::string canvasId;
};

struct MacroUiCanvasHotspotSpec {
	std::string canvasId;
	int x = 0;
	int y = 0;
	int width = 1;
	int height = 1;
	int id = 0;
	std::string macroSpec;
};

struct MRMacroModelessWindowGeometry {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
};

struct MRMacroModelessWindowDesktopState {
	int virtualDesktop = 1;
	bool manuallyHidden = false;
	bool minimized = false;
	bool bufferedBeforeMinimize = false;
	int restoreX = 0;
	int restoreY = 0;
	int restoreWidth = 0;
	int restoreHeight = 0;
	int lastMinimizedX = 0;
	int lastMinimizedY = 0;
	int lastMinimizedWidth = 0;
	int lastMinimizedHeight = 0;
	bool assigned = false;
};

struct MacroUiInputSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	int id = 0;
	std::string label;
	std::string text;
};

struct MacroUiTextFieldSpec {
	std::string fieldId;
	MacroUiInputSpec input;
};

struct MacroUiBoolFieldSpec {
	int x = 0;
	int y = 0;
	std::string fieldId;
	std::string caption;
	bool value = false;
};

struct MacroUiIntFieldSpec {
	int x = 0;
	int y = 0;
	int width = 8;
	std::string fieldId;
	std::string label;
	int minimum = 0;
	int maximum = 100;
	int value = 0;
};

struct MacroUiProgressFieldSpec {
	int x = 0;
	int y = 0;
	int width = 16;
	std::string fieldId;
	std::string label;
	int total = 100;
	int value = 0;
};

struct MacroUiLogFieldSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	int height = 4;
	std::string logId;
	std::string label;
	int capacity = 16;
};

struct MacroUiSelectFieldSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	int height = 4;
	std::string fieldId;
	std::string label;
	std::string value;
	std::vector<std::string> options;
};

struct MacroUiListBoxSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	int height = 4;
	int id = 0;
	std::string label;
	std::string itemSpec;
	int start = 1;
};

struct MacroUiGridSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	int height = 4;
	int id = 0;
	std::string label;
	std::string itemSpec;
	int start = 1;
};

struct MacroUiDialogDefinition {
	int x = 0;
	int y = 0;
	int width = 40;
	int height = 12;
	std::string title;
	std::vector<MacroUiLabelSpec> labels;
	std::vector<MacroUiButtonSpec> buttons;
	std::vector<MacroUiDisplaySpec> displays;
	std::map<std::string, int> statusDisplayIndices;
	std::vector<MacroUiCanvasSpec> canvases;
	std::vector<MacroUiCanvasHotspotSpec> canvasHotspots;
	std::vector<MacroUiInputSpec> inputs;
	std::vector<MacroUiTextFieldSpec> textFields;
	std::vector<MacroUiBoolFieldSpec> boolFields;
	std::vector<MacroUiIntFieldSpec> intFields;
	std::vector<MacroUiProgressFieldSpec> progressFields;
	std::vector<MacroUiLogFieldSpec> logFields;
	std::vector<MacroUiSelectFieldSpec> selectFields;
	std::vector<MacroUiListBoxSpec> listBoxes;
	std::vector<MacroUiGridSpec> grids;
	std::map<int, std::string> modelessButtonMacros;
};

std::string mrvmModelessUiListKey(const std::string &name);

void mrvmModelessUiBeginDialog(MRVMRuntimeKv &runtimeKv, int x, int y, int width, int height, const std::string &title);
void mrvmModelessUiWriteTextValue(MRVMRuntimeKv &runtimeKv, int id, const std::string &text);
void mrvmModelessUiWriteIndexValue(MRVMRuntimeKv &runtimeKv, int id, int index);
std::string mrvmModelessUiReadTextValue(MRVMRuntimeKv &runtimeKv, int id);
int mrvmModelessUiReadIndexValue(MRVMRuntimeKv &runtimeKv, int id);
void mrvmModelessUiAppendLabel(MRVMRuntimeKv &runtimeKv, const MacroUiLabelSpec &label);
void mrvmModelessUiAppendButton(MRVMRuntimeKv &runtimeKv, const MacroUiButtonSpec &button);
int mrvmModelessUiAppendDisplay(MRVMRuntimeKv &runtimeKv, const MacroUiDisplaySpec &display);
bool mrvmModelessUiAppendStatusField(MRVMRuntimeKv &runtimeKv, const std::string &statusId, const MacroUiDisplaySpec &display);
void mrvmModelessUiAppendCanvas(MRVMRuntimeKv &runtimeKv, const MacroUiCanvasSpec &canvas);
bool mrvmModelessUiAppendCanvasHotspot(MRVMRuntimeKv &runtimeKv, const MacroUiCanvasHotspotSpec &hotspot);
int mrvmModelessUiAppendInput(MRVMRuntimeKv &runtimeKv, const MacroUiInputSpec &input);
bool mrvmModelessUiAppendTextField(MRVMRuntimeKv &runtimeKv, const std::string &fieldId, const MacroUiInputSpec &input);
bool mrvmModelessUiAppendBoolField(MRVMRuntimeKv &runtimeKv, const MacroUiBoolFieldSpec &field);
bool mrvmModelessUiAppendIntField(MRVMRuntimeKv &runtimeKv, const MacroUiIntFieldSpec &field);
bool mrvmModelessUiAppendProgressField(MRVMRuntimeKv &runtimeKv, const MacroUiProgressFieldSpec &field);
bool mrvmModelessUiAppendLogField(MRVMRuntimeKv &runtimeKv, const MacroUiLogFieldSpec &field);
bool mrvmModelessUiAppendSelectField(MRVMRuntimeKv &runtimeKv, const MacroUiSelectFieldSpec &field);
bool mrvmModelessUiAppendSelectOption(MRVMRuntimeKv &runtimeKv, const std::string &fieldId, const std::string &option);
void mrvmModelessUiAppendListBox(MRVMRuntimeKv &runtimeKv, const MacroUiListBoxSpec &listBox);
void mrvmModelessUiAppendGrid(MRVMRuntimeKv &runtimeKv, const MacroUiGridSpec &grid);
MacroUiDialogDefinition mrvmModelessUiReadDialogDefinition(MRVMRuntimeKv &runtimeKv);
void mrvmModelessUiWriteModelessMacro(MRVMRuntimeKv &runtimeKv, int controlId, const std::string &macroSpec);
bool mrvmModelessUiReadItemList(MRVMRuntimeKv &runtimeKv, const std::string &key, std::vector<std::string> &values);
void mrvmModelessUiClearItemList(MRVMRuntimeKv &runtimeKv, const std::string &key);
void mrvmModelessUiAddItemListValue(MRVMRuntimeKv &runtimeKv, const std::string &key, const std::string &value);

void mrvmModelessUiStoreWindowDefinition(MRVMRuntimeKv &runtimeKv, const MRMacroModelessWindowDefinition &definition);
bool mrvmModelessUiStoreWindowDisplay(MRVMRuntimeKv &runtimeKv, const std::string &windowId, int displayIndex, const std::string &text);
bool mrvmModelessUiReadWindowStatusDisplayIndex(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &statusId, int &displayIndex);
bool mrvmModelessUiStoreWindowTextFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, const std::string &text);
bool mrvmModelessUiReadWindowTextFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, std::string &text);
bool mrvmModelessUiStoreWindowBoolFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, bool value);
bool mrvmModelessUiReadWindowBoolFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, bool &value);
bool mrvmModelessUiStoreWindowIntFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, int value);
bool mrvmModelessUiReadWindowIntFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, int &value);
bool mrvmModelessUiStoreWindowProgressFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, int value);
bool mrvmModelessUiReadWindowProgressFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, int &total, int &value);
bool mrvmModelessUiAppendWindowLogFieldLine(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &logId, const std::string &text);
bool mrvmModelessUiClearWindowLogField(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &logId);
bool mrvmModelessUiReadWindowLogFieldLines(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &logId, std::vector<std::string> &lines);
bool mrvmModelessUiReadWindowLogFieldCount(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &logId, int &count);
bool mrvmModelessUiStoreWindowSelectFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, const std::string &value);
bool mrvmModelessUiReadWindowSelectFieldValue(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &fieldId, std::string &value);
void mrvmModelessUiStoreWindowLiveGeometry(MRVMRuntimeKv &runtimeKv, const std::string &windowId, int x, int y, int width, int height);
bool mrvmModelessUiReadWindowGeometry(MRVMRuntimeKv &runtimeKv, const std::string &windowId, MRMacroModelessWindowGeometry &geometry);
void mrvmModelessUiStoreWindowDesktopState(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const MRMacroModelessWindowDesktopState &state);
bool mrvmModelessUiReadWindowDesktopState(MRVMRuntimeKv &runtimeKv, const std::string &windowId, MRMacroModelessWindowDesktopState &state);
std::string mrvmModelessUiCreateWindowInstanceId(MRVMRuntimeKv &runtimeKv, const std::string &prefix);
bool mrvmModelessUiWindowExists(MRVMRuntimeKv &runtimeKv, const std::string &windowId);
bool mrvmModelessUiCanvasClear(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &canvasId, int style);
bool mrvmModelessUiCanvasAppendCommand(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &canvasId, const MRMacroModelessCanvasCommand &command);
bool mrvmModelessUiReadCanvasScene(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &canvasId, MRMacroModelessCanvasScene &scene);
bool mrvmModelessUiCommitCanvas(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &canvasId);
void mrvmModelessUiRemoveWindowDefinition(MRVMRuntimeKv &runtimeKv, const std::string &windowId);

bool mrvmStoreModelessWindowTextFieldValue(const std::string &windowId, const std::string &fieldId, const std::string &text);
bool mrvmReadModelessWindowTextFieldValue(const std::string &windowId, const std::string &fieldId, std::string &text);
bool mrvmStoreModelessWindowBoolFieldValue(const std::string &windowId, const std::string &fieldId, bool value);
bool mrvmReadModelessWindowBoolFieldValue(const std::string &windowId, const std::string &fieldId, bool &value);
bool mrvmStoreModelessWindowIntFieldValue(const std::string &windowId, const std::string &fieldId, int value);
bool mrvmReadModelessWindowIntFieldValue(const std::string &windowId, const std::string &fieldId, int &value);
bool mrvmStoreModelessWindowProgressFieldValue(const std::string &windowId, const std::string &fieldId, int value);
bool mrvmReadModelessWindowProgressFieldValue(const std::string &windowId, const std::string &fieldId, int &total, int &value);
bool mrvmAppendModelessWindowLogFieldLine(const std::string &windowId, const std::string &logId, const std::string &text);
bool mrvmClearModelessWindowLogField(const std::string &windowId, const std::string &logId);
bool mrvmReadModelessWindowLogFieldLines(const std::string &windowId, const std::string &logId, std::vector<std::string> &lines);
bool mrvmStoreModelessWindowSelectFieldValue(const std::string &windowId, const std::string &fieldId, const std::string &value);
bool mrvmReadModelessWindowSelectFieldValue(const std::string &windowId, const std::string &fieldId, std::string &value);

#endif
