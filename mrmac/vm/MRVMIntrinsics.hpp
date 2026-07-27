#ifndef MRVM_INTRINSICS_HPP
#define MRVM_INTRINSICS_HPP

#include "../MRVM.hpp"

#include <string>
#include <vector>

class MRVMIntrinsics final {
 public:
	explicit MRVMIntrinsics(VirtualMachine &vm) noexcept;
	VirtualMachine::Value apply(const std::string &name, const std::vector<VirtualMachine::Value> &args);

 private:
	VirtualMachine &vm;

	bool applyHash(const std::string &name, const std::vector<VirtualMachine::Value> &args, VirtualMachine::Value &out);
};

#endif
