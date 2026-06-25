#ifndef MRVM_MODELESS_UI_RUNTIME_HPP
#define MRVM_MODELESS_UI_RUNTIME_HPP

#include "MRVMRuntimeKv.hpp"

#include "../MRMacroModelessUi.hpp"

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

struct MacroUiInputSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	int id = 0;
	std::string label;
	std::string text;
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
	std::vector<MacroUiInputSpec> inputs;
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
void mrvmModelessUiAppendDisplay(MRVMRuntimeKv &runtimeKv, const MacroUiDisplaySpec &display);
void mrvmModelessUiAppendInput(MRVMRuntimeKv &runtimeKv, const MacroUiInputSpec &input);
void mrvmModelessUiAppendListBox(MRVMRuntimeKv &runtimeKv, const MacroUiListBoxSpec &listBox);
void mrvmModelessUiAppendGrid(MRVMRuntimeKv &runtimeKv, const MacroUiGridSpec &grid);
MacroUiDialogDefinition mrvmModelessUiReadDialogDefinition(MRVMRuntimeKv &runtimeKv);
void mrvmModelessUiWriteModelessMacro(MRVMRuntimeKv &runtimeKv, int controlId, const std::string &macroSpec);
bool mrvmModelessUiReadItemList(MRVMRuntimeKv &runtimeKv, const std::string &key, std::vector<std::string> &values);
void mrvmModelessUiClearItemList(MRVMRuntimeKv &runtimeKv, const std::string &key);
void mrvmModelessUiAddItemListValue(MRVMRuntimeKv &runtimeKv, const std::string &key, const std::string &value);

void mrvmModelessUiStoreWindowDefinition(MRVMRuntimeKv &runtimeKv, const MRMacroModelessWindowDefinition &definition);
bool mrvmModelessUiStoreWindowDisplay(MRVMRuntimeKv &runtimeKv, const std::string &windowId, int displayIndex, const std::string &text);
void mrvmModelessUiStoreWindowLiveGeometry(MRVMRuntimeKv &runtimeKv, const std::string &windowId, int x, int y, int width, int height);
void mrvmModelessUiRemoveWindowDefinition(MRVMRuntimeKv &runtimeKv, const std::string &windowId);

#endif
