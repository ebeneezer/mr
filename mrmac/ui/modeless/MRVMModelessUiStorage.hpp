#ifndef MRVM_MODELESS_UI_STORAGE_HPP
#define MRVM_MODELESS_UI_STORAGE_HPP

#include "MRVMModelessUiRuntime.hpp"

#include <initializer_list>
#include <string>
#include <vector>

namespace mr {
namespace modelessui {

std::string upperKey(std::string value);
std::string trimAscii(const std::string &value);

void hashWriteInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const std::string &key, int value);
void hashWriteString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const std::string &key, const std::string &value);
int hashReadInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const std::string &key, int fallback = 0);
std::string hashReadString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const std::string &key);

VirtualMachine::Value ensureModelessUiChildPath(MRVMRuntimeKv &runtimeKv, std::initializer_list<const char *> keys);
bool findModelessUiChildPath(MRVMRuntimeKv &runtimeKv, std::initializer_list<const char *> keys, VirtualMachine::Value &child);
VirtualMachine::Value ensureDialogSection(MRVMRuntimeKv &runtimeKv, const char *sectionName);
VirtualMachine::Value appendDialogSectionItem(MRVMRuntimeKv &runtimeKv, const char *sectionName);
bool findStagingChild(MRVMRuntimeKv &runtimeKv, const std::string &key, VirtualMachine::Value &child);
bool findCurrentDialog(MRVMRuntimeKv &runtimeKv, VirtualMachine::Value &dialog);

void writeLabelHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MacroUiLabelSpec &label);
void writeButtonHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MacroUiButtonSpec &button);
void writeDisplayHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MacroUiDisplaySpec &display);
void writeCanvasHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MacroUiCanvasSpec &canvas);
void writeCanvasHotspotHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MacroUiCanvasHotspotSpec &hotspot);
void writeInputHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MacroUiInputSpec &input);
void writeListBoxHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MacroUiListBoxSpec &listBox);
void writeGridHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MacroUiGridSpec &grid);
MacroUiCanvasSpec readCanvasHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash);

void readLabelSection(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &dialog, const char *sectionName, std::vector<MacroUiLabelSpec> &items);
void readButtonSection(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &dialog, const char *sectionName, std::vector<MacroUiButtonSpec> &items);
void readDisplaySection(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &dialog, const char *sectionName, std::vector<MacroUiDisplaySpec> &items);
void readCanvasSection(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &dialog, const char *sectionName, std::vector<MacroUiCanvasSpec> &items);
void readCanvasHotspotSection(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &dialog, const char *sectionName, std::vector<MacroUiCanvasHotspotSpec> &items);
void readInputSection(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &dialog, const char *sectionName, std::vector<MacroUiInputSpec> &items);
void readListBoxSection(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &dialog, const char *sectionName, std::vector<MacroUiListBoxSpec> &items);
void readGridSection(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &dialog, const char *sectionName, std::vector<MacroUiGridSpec> &items);

void writeModelessLabelHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MRMacroModelessLabelSpec &label);
void writeModelessDisplayHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MRMacroModelessDisplaySpec &display);
void writeModelessCanvasHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MRMacroModelessCanvasSpec &canvas);
void writeModelessCanvasHotspotHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MRMacroModelessCanvasHotspotSpec &hotspot);
void writeModelessButtonHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MRMacroModelessButtonSpec &button);
void writeModelessListBoxHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MRMacroModelessListBoxSpec &listBox);
void writeModelessGridHash(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MRMacroModelessGridSpec &grid);

bool findModelessCanvas(MRVMRuntimeKv &runtimeKv, const std::string &windowId, const std::string &canvasId, VirtualMachine::Value &canvas);
void writeCanvasCommand(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MRMacroModelessCanvasCommand &command);
MRMacroModelessCanvasCommand readCanvasCommand(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash);

} // namespace modelessui
} // namespace mr

#endif
