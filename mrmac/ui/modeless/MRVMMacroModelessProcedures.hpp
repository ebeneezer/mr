#ifndef MRVM_MACRO_MODELESS_PROCEDURES_HPP
#define MRVM_MACRO_MODELESS_PROCEDURES_HPP

#include "../../vm/MRVMRuntimeKv.hpp"

#include "MRMacroModelessUi.hpp"
#include "../../MRVM.hpp"

#include <string>
#include <vector>

bool mrvmDispatchMacroModelessProcedure(MRVMRuntimeKv &runtimeKv, const std::string &name, const std::vector<VirtualMachine::Value> &args, int &returnValue, std::string &errorText);
bool mrvmDispatchMacroModelessIntrinsic(MRVMRuntimeKv &runtimeKv, const std::string &name, const std::vector<VirtualMachine::Value> &args, VirtualMachine::Value &result);
MRMacroModelessWindowDefinition mrvmBuildMacroModelessDefinition(MRVMRuntimeKv &runtimeKv, const std::string &windowId);

#endif
