#ifndef MRVM_DEFERRED_UI_HPP
#define MRVM_DEFERRED_UI_HPP

#include <string>
#include <vector>

#include "../MRVM.hpp"

bool dispatchDeferredVisualUiProcedure(const std::string &name, const std::vector<VirtualMachine::Value> &args, int &errorCode);
bool dispatchDeferredMenuUiProcedure(const std::string &name, const std::vector<VirtualMachine::Value> &args, int &errorCode);
bool dispatchDeferredUiTvCall(const std::string &nameUpper, const std::vector<VirtualMachine::Value> &args, int &errorCode);

#endif
