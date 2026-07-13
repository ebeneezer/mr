#ifndef MRVM_MACRO_DIALOG_RUNTIME_HPP
#define MRVM_MACRO_DIALOG_RUNTIME_HPP

#include "../../vm/MRVMRuntimeKv.hpp"

#include "../../MRVM.hpp"

#include <string>
#include <vector>

int mrvmRunMacroMenuIntrinsic(const std::string &name, const std::vector<VirtualMachine::Value> &args);
std::string mrvmRunMacroStringInputIntrinsic(const std::vector<VirtualMachine::Value> &args);
int mrvmRunMacroUiDialogDefinition(MRVMRuntimeKv &runtimeKv);
std::vector<std::string> mrvmResolveMacroUiListItems(MRVMRuntimeKv &runtimeKv, const std::string &itemSpec);
std::string mrvmMacroUiGridItemText(const std::string &source);
void mrvmBeginMacroUiDialog(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmAddMacroUiLabel(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmAddMacroUiButton(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmAddMacroUiDisplay(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmAddMacroUiInput(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmAddMacroUiListBox(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmAddMacroUiGrid(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmAddMacroUiTree(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmAddMacroUiTable(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmClearMacroUiItemList(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmAddMacroUiItemListValue(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmClearMacroUiTree(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmAddMacroUiTreeNode(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmClearMacroUiTable(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmAddMacroUiTableColumn(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmAddMacroUiTableRow(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);
void mrvmBindMacroModelessButton(MRVMRuntimeKv &runtimeKv, const std::vector<VirtualMachine::Value> &args);

#endif
