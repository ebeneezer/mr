#include "MRVMHash.hpp"

#include "MRVMValue.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <stdexcept>

#include "../mrmac.h"

namespace {

struct HashIoBucket {
	std::uint64_t second;
	std::uint64_t reads;
	std::uint64_t writes;

	HashIoBucket() noexcept : second(0), reads(0), writes(0) {
	}
};

struct HashIoMeter {
	std::mutex mutex;
	std::array<HashIoBucket, 60> buckets;
};

HashIoMeter &hashIoMeter() {
	static HashIoMeter meter;
	return meter;
}

std::uint64_t hashIoSecondNow() {
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void recordHashIo(bool write) {
	HashIoMeter &meter = hashIoMeter();
	const std::uint64_t now = hashIoSecondNow();
	const std::size_t index = static_cast<std::size_t>(now % meter.buckets.size());
	std::lock_guard<std::mutex> lock(meter.mutex);
	HashIoBucket &bucket = meter.buckets[index];

	if (bucket.second != now) {
		bucket.second = now;
		bucket.reads = 0;
		bucket.writes = 0;
	}
	if (write)
		++bucket.writes;
	else
		++bucket.reads;
}

} // namespace

void MRVMHashStore::setIoTrackingEnabled(bool enabled) noexcept {
	ioTrackingEnabled = enabled;
}

void MRVMHashStore::clear() {
	if (ioTrackingEnabled) recordHashIo(true);
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

	if (ioTrackingEnabled) recordHashIo(true);
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
	if (ioTrackingEnabled) recordHashIo(true);
	int handle = nextHandle++;
	hashes[handle] = std::map<std::string, VirtualMachine::Value>();
	return handle;
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

	if (ioTrackingEnabled) recordHashIo(true);
	eraseValueTrees(value, targetGlobalStorage, erased);
}

bool MRVMHashStore::contains(int handle, const std::string &key) const {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::const_iterator hashIt = hashes.find(handle);
	if (ioTrackingEnabled) recordHashIo(false);
	if (hashIt == hashes.end()) throw std::runtime_error("Invalid hash value.");
	return hashIt->second.find(key) != hashIt->second.end();
}

VirtualMachine::Value MRVMHashStore::read(int handle, const std::string &key) const {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::const_iterator hashIt = hashes.find(handle);
	if (ioTrackingEnabled) recordHashIo(false);
	if (hashIt == hashes.end()) throw std::runtime_error("Invalid hash value.");

	std::map<std::string, VirtualMachine::Value>::const_iterator valueIt = hashIt->second.find(key);
	if (valueIt == hashIt->second.end()) throw std::runtime_error("Hash key not found.");
	return valueIt->second;
}

void MRVMHashStore::write(int handle, const std::string &key, const VirtualMachine::Value &value) {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::iterator hashIt = hashes.find(handle);
	if (ioTrackingEnabled) recordHashIo(true);
	if (hashIt == hashes.end()) throw std::runtime_error("Invalid hash value.");
	hashIt->second[key] = value;
}

void MRVMHashStore::erase(int handle, const std::string &key) {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::iterator hashIt = hashes.find(handle);
	if (ioTrackingEnabled) recordHashIo(true);
	if (hashIt == hashes.end()) throw std::runtime_error("Invalid hash value.");
	hashIt->second.erase(key);
}

std::vector<std::string> MRVMHashStore::keys(int handle) const {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::const_iterator hashIt = hashes.find(handle);
	std::vector<std::string> result;

	if (ioTrackingEnabled) recordHashIo(false);
	if (hashIt == hashes.end()) throw std::runtime_error("Invalid hash value.");
	result.reserve(hashIt->second.size());
	for (const std::pair<const std::string, VirtualMachine::Value> &entry : hashIt->second)
		result.push_back(entry.first);
	return result;
}

std::vector<VirtualMachine::Value> MRVMHashStore::values(int handle) const {
	std::map<int, std::map<std::string, VirtualMachine::Value>>::const_iterator hashIt = hashes.find(handle);
	std::vector<VirtualMachine::Value> result;

	if (ioTrackingEnabled) recordHashIo(false);
	if (hashIt == hashes.end()) throw std::runtime_error("Invalid hash value.");
	result.reserve(hashIt->second.size());
	for (const std::pair<const std::string, VirtualMachine::Value> &entry : hashIt->second)
		result.push_back(entry.second);
	return result;
}

MRVMHashIoRateSnapshot mrvmHashIoRateSnapshot() {
	HashIoMeter &meter = hashIoMeter();
	const std::uint64_t now = hashIoSecondNow();
	MRVMHashIoRateSnapshot snapshot{0, 0};
	std::lock_guard<std::mutex> lock(meter.mutex);

	for (const HashIoBucket &bucket : meter.buckets) {
		if (bucket.second == 0 || bucket.second + 60 <= now) continue;
		snapshot.readsPerMinute += bucket.reads;
		snapshot.writesPerMinute += bucket.writes;
	}
	return snapshot;
}

MRVMHashStore &mrvmHashRuntimeStoreForValue(MRVMHashStore &localStore, MRVMHashStore &globalStore, const VirtualMachine::Value &hashValue) {
	return hashValue.globalStorage ? globalStore : localStore;
}

const MRVMHashStore &mrvmHashRuntimeStoreForValue(const MRVMHashStore &localStore, const MRVMHashStore &globalStore, const VirtualMachine::Value &hashValue) {
	return hashValue.globalStorage ? globalStore : localStore;
}

VirtualMachine::Value mrvmHashCopyValueForStore(const VirtualMachine::Value &value, MRVMHashStore &localStore, MRVMHashStore &globalStore, MRVMHashStore &targetStore, bool targetGlobalStorage) {
	using SourceHash = std::pair<const MRVMHashStore *, int>;
	std::map<SourceHash, int> transferred;
	std::vector<int> createdHandles;

	auto copyGraph = [&](auto &self, const VirtualMachine::Value &source, bool root) -> VirtualMachine::Value {
		VirtualMachine::Value copied = source;

		if (root && source.type == TYPE_HASH && source.globalStorage == targetGlobalStorage && &mrvmHashRuntimeStoreForValue(localStore, globalStore, source) == &targetStore) return source;
		if (source.type == TYPE_HASH) {
			MRVMHashStore &sourceStore = mrvmHashRuntimeStoreForValue(localStore, globalStore, source);
			const SourceHash sourceHash(&sourceStore, source.hashHandle);
			std::map<SourceHash, int>::const_iterator transferredIt;

			transferredIt = transferred.find(sourceHash);
			if (transferredIt != transferred.end()) return mrvmMakeHash(transferredIt->second, targetGlobalStorage);
			copied.hashHandle = targetStore.createHash();
			copied.globalStorage = targetGlobalStorage;
			transferred[sourceHash] = copied.hashHandle;
			createdHandles.push_back(copied.hashHandle);
			for (const std::string &key : sourceStore.keys(source.hashHandle))
				targetStore.write(copied.hashHandle, key, self(self, sourceStore.read(source.hashHandle, key), false));
			return copied;
		}
		if (mrvmValueIsArrayType(source.type))
			for (VirtualMachine::Value &arrayValue : copied.arrayValues)
				arrayValue = self(self, arrayValue, false);
		copied.globalStorage = targetGlobalStorage;
		return copied;
	};

	try {
		return copyGraph(copyGraph, value, true);
	} catch (...) {
		for (int handle : createdHandles)
			targetStore.eraseValueTrees(mrvmMakeHash(handle, targetGlobalStorage), targetGlobalStorage);
		throw;
	}
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
