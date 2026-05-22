#ifndef MRVMHASH_HPP
#define MRVMHASH_HPP

#include <map>
#include <set>
#include <string>
#include <vector>

#include "../MRVM.hpp"

class MRVMHashStore {
  public:
	void clear();
	void clearExceptRoots(const std::vector<int> &roots);
	int createHash();
	int cloneHashFrom(const MRVMHashStore &sourceStore, int sourceHandle, bool targetGlobalStorage);
	bool contains(int handle, const std::string &key) const;
	VirtualMachine::Value read(int handle, const std::string &key) const;
	void write(int handle, const std::string &key, const VirtualMachine::Value &value);
	void erase(int handle, const std::string &key);
	std::vector<std::string> keys(int handle) const;
	std::vector<VirtualMachine::Value> values(int handle) const;

  private:
	void collectReachable(int handle, std::set<int> &reachable) const;

	int nextHandle = 1;
	std::map<int, std::map<std::string, VirtualMachine::Value>> hashes;
};

#endif
