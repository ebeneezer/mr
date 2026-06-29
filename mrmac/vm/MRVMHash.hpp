#ifndef MRVMHASH_HPP
#define MRVMHASH_HPP

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../MRVM.hpp"

class MRVMHashStore {
  public:
	void setIoTrackingEnabled(bool enabled) noexcept;
	void clear();
	void clearExceptRoots(const std::vector<int> &roots);
	int createHash();
	int cloneHashFrom(const MRVMHashStore &sourceStore, int sourceHandle, bool targetGlobalStorage);
	void eraseValueTrees(const VirtualMachine::Value &value, bool targetGlobalStorage);
	bool contains(int handle, const std::string &key) const;
	VirtualMachine::Value read(int handle, const std::string &key) const;
	void write(int handle, const std::string &key, const VirtualMachine::Value &value);
	void erase(int handle, const std::string &key);
	std::vector<std::string> keys(int handle) const;
	std::vector<VirtualMachine::Value> values(int handle) const;

  private:
	void collectReachable(int handle, std::set<int> &reachable) const;
	void eraseValueTrees(const VirtualMachine::Value &value, bool targetGlobalStorage, std::set<int> &erased);
	void eraseHashTree(int handle, bool targetGlobalStorage, std::set<int> &erased);

	int nextHandle = 1;
	bool ioTrackingEnabled = false;
	std::map<int, std::map<std::string, VirtualMachine::Value>> hashes;
};

struct MRVMHashIoRateSnapshot {
	std::uint64_t readsPerMinute;
	std::uint64_t writesPerMinute;
};

struct MRVMHashIoHotspot {
	std::string label;
	std::uint64_t readsPerMinute;
	std::uint64_t writesPerMinute;
};

[[nodiscard]] MRVMHashIoRateSnapshot mrvmHashIoRateSnapshot();
[[nodiscard]] std::vector<MRVMHashIoHotspot> mrvmHashIoHotspotsSnapshot(std::size_t limit);

MRVMHashStore &mrvmHashRuntimeStoreForValue(MRVMHashStore &localStore, MRVMHashStore &globalStore, const VirtualMachine::Value &hashValue);
const MRVMHashStore &mrvmHashRuntimeStoreForValue(const MRVMHashStore &localStore, const MRVMHashStore &globalStore, const VirtualMachine::Value &hashValue);
VirtualMachine::Value mrvmHashCopyValueForStore(const VirtualMachine::Value &value, MRVMHashStore &localStore, MRVMHashStore &globalStore, MRVMHashStore &targetStore, bool targetGlobalStorage);
bool mrvmHashContainsValue(const MRVMHashStore &localStore, const MRVMHashStore &globalStore, const VirtualMachine::Value &hashValue, const std::string &key);
VirtualMachine::Value mrvmHashReadValue(const MRVMHashStore &localStore, const MRVMHashStore &globalStore, const VirtualMachine::Value &hashValue, const std::string &key);
void mrvmHashWriteValue(MRVMHashStore &localStore, MRVMHashStore &globalStore, const VirtualMachine::Value &hashValue, const std::string &key, const VirtualMachine::Value &value);
void mrvmHashEraseValue(MRVMHashStore &localStore, MRVMHashStore &globalStore, const VirtualMachine::Value &hashValue, const std::string &key);

#endif
