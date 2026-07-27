#ifndef MRVM_SYSTEM_VARIABLES_HPP
#define MRVM_SYSTEM_VARIABLES_HPP

#include "../MRVM.hpp"

#include <string>

class MRVMSystemVariables final {
 public:
	static VirtualMachine::Value load(const std::string &name, bool &handled);
	static bool store(const std::string &name, const VirtualMachine::Value &value);

	MRVMSystemVariables() = delete;
};

#endif
