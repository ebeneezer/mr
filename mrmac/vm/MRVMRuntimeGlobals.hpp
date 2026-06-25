#ifndef MRVM_RUNTIME_GLOBALS_HPP
#define MRVM_RUNTIME_GLOBALS_HPP

#include "MRVMRuntimeKv.hpp"

#include <cstddef>
#include <string>
#include <vector>

struct MRVMRuntimeGlobalEntry {
	int type;
	VirtualMachine::Value value;
};

std::vector<std::string> mrvmRuntimeGlobalOrderValues(MRVMRuntimeKv &runtimeKv);
std::size_t mrvmRuntimeGlobalEnumIndex(MRVMRuntimeKv &runtimeKv);
void mrvmRuntimeGlobalSetEnumIndex(MRVMRuntimeKv &runtimeKv, std::size_t index);
bool mrvmRuntimeGlobalRead(MRVMRuntimeKv &runtimeKv, const std::string &name, MRVMRuntimeGlobalEntry &entry);
void mrvmRuntimeGlobalWrite(MRVMRuntimeKv &runtimeKv, const std::string &name, int type, const VirtualMachine::Value &value);
bool mrvmRuntimeGlobalErase(MRVMRuntimeKv &runtimeKv, const std::string &name);
void mrvmRuntimeGlobalClearOrderAndEnumeration(MRVMRuntimeKv &runtimeKv);

#endif
