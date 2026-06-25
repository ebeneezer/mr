#ifndef MRVM_RUNTIME_KV_HPP
#define MRVM_RUNTIME_KV_HPP

#include "MRVMHash.hpp"

#include <string>

class MRVMRuntimeKv {
  public:
	MRVMRuntimeKv();

	MRVMHashStore &globalStore() noexcept;
	const MRVMHashStore &globalStore() const noexcept;

	VirtualMachine::Value ensureRoot(const std::string &name);
	bool findRoot(const std::string &name, VirtualMachine::Value &root);

	VirtualMachine::Value ensureChild(const VirtualMachine::Value &parent, const std::string &key);
	bool findChild(const VirtualMachine::Value &parent, const std::string &key, VirtualMachine::Value &child);
	bool eraseChild(const VirtualMachine::Value &parent, const std::string &key);
	VirtualMachine::Value replaceChild(const VirtualMachine::Value &parent, const std::string &key);

  private:
	static std::string upperKey(std::string value);
	static VirtualMachine::Value makeGlobalHashValue(int handle);

	MRVMHashStore store;
	int rootHandle;
};

#endif
