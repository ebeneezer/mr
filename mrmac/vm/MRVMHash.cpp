#include "MRVMHash.hpp"

#include "MRVMValue.hpp"

#include <stdexcept>

#include "../mrmac.h"

void MRVMHashStore::clear() {
	hashes.clear();
	nextHandle = 1;
}

void MRVMHashStore::collectReachable(int handle, std::set<int> &reachable) const {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::const_iterator hashIt;

	if (handle <= 0 || reachable.find(handle) != reachable.end()) return;
	hashIt = hashes.find(handle);
	if (hashIt == hashes.end()) return;
	reachable.insert(handle);
	for (const std::pair<const std::string, VirtualMachine::Value> &entry : hashIt->second) {
		if (entry.second.type == TYPE_HASH && !entry.second.globalStorage) collectReachable(entry.second.hashHandle, reachable);
		for (const VirtualMachine::Value &arrayValue : entry.second.arrayValues) {
			if (arrayValue.type == TYPE_HASH && !arrayValue.globalStorage) collectReachable(arrayValue.hashHandle, reachable);
		}
	}
}

void MRVMHashStore::clearExceptRoots(const std::vector<int> &roots) {
	std::set<int> reachable;
	std::map<int, std::map<std::string, VirtualMachine::Value>>::iterator it;

	for (int root : roots)
		collectReachable(root, reachable);
	for (it = hashes.begin(); it != hashes.end();) {
		if (reachable.find(it->first) == reachable.end())
			it = hashes.erase(it);
		else
			++it;
	}
}

int MRVMHashStore::createHash() {
	int handle = nextHandle++;
	hashes[handle] = std::map<std::string, VirtualMachine::Value>();
	return handle;
}

int MRVMHashStore::cloneHashFrom(const MRVMHashStore &sourceStore, int sourceHandle, bool targetGlobalStorage) {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::const_iterator sourceIt = sourceStore.hashes.find(sourceHandle);
	if (sourceIt == sourceStore.hashes.end()) throw std::runtime_error("Invalid hash value.");

	int targetHandle = createHash();
	std::map<std::string, VirtualMachine::Value> &targetHash = hashes[targetHandle];
	for (const std::pair<const std::string, VirtualMachine::Value> &entry : sourceIt->second) {
		VirtualMachine::Value value = entry.second;
		if (value.type == TYPE_HASH) {
			value.hashHandle = cloneHashFrom(sourceStore, value.hashHandle, targetGlobalStorage);
			value.globalStorage = targetGlobalStorage;
		} else {
			for (VirtualMachine::Value &arrayValue : value.arrayValues) {
				if (arrayValue.type == TYPE_HASH) {
					arrayValue.hashHandle = cloneHashFrom(sourceStore, arrayValue.hashHandle, targetGlobalStorage);
				}
				arrayValue.globalStorage = targetGlobalStorage;
			}
			value.globalStorage = targetGlobalStorage;
		}
		targetHash[entry.first] = value;
	}
	return targetHandle;
}

void MRVMHashStore::eraseHashTree(int handle, bool targetGlobalStorage, std::set<int> &erased) {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::iterator hashIt;
	std::map<std::string, VirtualMachine::Value> hashValues;

	if (handle <= 0 || erased.find(handle) != erased.end()) return;
	hashIt = hashes.find(handle);
	if (hashIt == hashes.end()) return;
	erased.insert(handle);
	hashValues = hashIt->second;
	for (const std::pair<const std::string, VirtualMachine::Value> &entry : hashValues)
		eraseValueTrees(entry.second, targetGlobalStorage, erased);
	hashes.erase(handle);
}

void MRVMHashStore::eraseValueTrees(const VirtualMachine::Value &value, bool targetGlobalStorage, std::set<int> &erased) {
	if (value.type == TYPE_HASH && value.globalStorage == targetGlobalStorage) {
		eraseHashTree(value.hashHandle, targetGlobalStorage, erased);
		return;
	}
	if (mrvmValueIsArrayType(value.type)) {
		for (const VirtualMachine::Value &arrayValue : value.arrayValues)
			eraseValueTrees(arrayValue, targetGlobalStorage, erased);
	}
}

void MRVMHashStore::eraseValueTrees(const VirtualMachine::Value &value, bool targetGlobalStorage) {
	std::set<int> erased;

	eraseValueTrees(value, targetGlobalStorage, erased);
}

bool MRVMHashStore::contains(int handle, const std::string &key) const {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::const_iterator hashIt = hashes.find(handle);
	if (hashIt == hashes.end()) throw std::runtime_error("Invalid hash value.");
	return hashIt->second.find(key) != hashIt->second.end();
}

VirtualMachine::Value MRVMHashStore::read(int handle, const std::string &key) const {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::const_iterator hashIt = hashes.find(handle);
	if (hashIt == hashes.end()) throw std::runtime_error("Invalid hash value.");

	std::map<std::string, VirtualMachine::Value>::const_iterator valueIt = hashIt->second.find(key);
	if (valueIt == hashIt->second.end()) throw std::runtime_error("Hash key not found.");
	return valueIt->second;
}

void MRVMHashStore::write(int handle, const std::string &key, const VirtualMachine::Value &value) {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::iterator hashIt = hashes.find(handle);
	if (hashIt == hashes.end()) throw std::runtime_error("Invalid hash value.");
	hashIt->second[key] = value;
}

void MRVMHashStore::erase(int handle, const std::string &key) {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::iterator hashIt = hashes.find(handle);
	if (hashIt == hashes.end()) throw std::runtime_error("Invalid hash value.");
	hashIt->second.erase(key);
}

std::vector<std::string> MRVMHashStore::keys(int handle) const {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::const_iterator hashIt = hashes.find(handle);
	std::vector<std::string> result;

	if (hashIt == hashes.end()) throw std::runtime_error("Invalid hash value.");
	result.reserve(hashIt->second.size());
	for (const std::pair<const std::string, VirtualMachine::Value> &entry : hashIt->second)
		result.push_back(entry.first);
	return result;
}

std::vector<VirtualMachine::Value> MRVMHashStore::values(int handle) const {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::const_iterator hashIt = hashes.find(handle);
	std::vector<VirtualMachine::Value> result;

	if (hashIt == hashes.end()) throw std::runtime_error("Invalid hash value.");
	result.reserve(hashIt->second.size());
	for (const std::pair<const std::string, VirtualMachine::Value> &entry : hashIt->second)
		result.push_back(entry.second);
	return result;
}

MRVMHashStore &mrvmHashRuntimeStoreForValue(MRVMHashStore &localStore, MRVMHashStore &globalStore, const VirtualMachine::Value &hashValue) {
	return hashValue.globalStorage ? globalStore : localStore;
}

const MRVMHashStore &mrvmHashRuntimeStoreForValue(const MRVMHashStore &localStore, const MRVMHashStore &globalStore, const VirtualMachine::Value &hashValue) {
	return hashValue.globalStorage ? globalStore : localStore;
}

VirtualMachine::Value mrvmHashCopyValueForStore(const VirtualMachine::Value &value, MRVMHashStore &localStore, MRVMHashStore &globalStore, MRVMHashStore &targetStore, bool targetGlobalStorage) {
	VirtualMachine::Value copied = value;

	if (copied.type == TYPE_HASH) {
		MRVMHashStore &sourceStore = mrvmHashRuntimeStoreForValue(localStore, globalStore, copied);
		if (&sourceStore == &targetStore && copied.globalStorage == targetGlobalStorage) return copied;
		copied.hashHandle = targetStore.cloneHashFrom(sourceStore, copied.hashHandle, targetGlobalStorage);
		copied.globalStorage = targetGlobalStorage;
		return copied;
	}
	if (mrvmValueIsArrayType(copied.type)) {
		for (VirtualMachine::Value &arrayValue : copied.arrayValues)
			arrayValue = mrvmHashCopyValueForStore(arrayValue, localStore, globalStore, targetStore, targetGlobalStorage);
	}
	copied.globalStorage = targetGlobalStorage;
	return copied;
}

bool mrvmHashContainsValue(const MRVMHashStore &localStore, const MRVMHashStore &globalStore, const VirtualMachine::Value &hashValue, const std::string &key) {
	return mrvmHashRuntimeStoreForValue(localStore, globalStore, hashValue).contains(hashValue.hashHandle, key);
}

VirtualMachine::Value mrvmHashReadValue(const MRVMHashStore &localStore, const MRVMHashStore &globalStore, const VirtualMachine::Value &hashValue, const std::string &key) {
	return mrvmHashRuntimeStoreForValue(localStore, globalStore, hashValue).read(hashValue.hashHandle, key);
}

void mrvmHashWriteValue(MRVMHashStore &localStore, MRVMHashStore &globalStore, const VirtualMachine::Value &hashValue, const std::string &key, const VirtualMachine::Value &value) {
	MRVMHashStore &targetStore = mrvmHashRuntimeStoreForValue(localStore, globalStore, hashValue);
	targetStore.write(hashValue.hashHandle, key, mrvmHashCopyValueForStore(value, localStore, globalStore, targetStore, hashValue.globalStorage));
}

void mrvmHashEraseValue(MRVMHashStore &localStore, MRVMHashStore &globalStore, const VirtualMachine::Value &hashValue, const std::string &key) {
	mrvmHashRuntimeStoreForValue(localStore, globalStore, hashValue).erase(hashValue.hashHandle, key);
}
