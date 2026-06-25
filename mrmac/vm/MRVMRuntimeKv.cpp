#include "MRVMRuntimeKv.hpp"

#include "../mrmac.h"

#include <cctype>

namespace {
using Value = VirtualMachine::Value;
}

MRVMRuntimeKv::MRVMRuntimeKv() : rootHandle(store.createHash()) {
}

MRVMHashStore &MRVMRuntimeKv::globalStore() noexcept {
	return store;
}

const MRVMHashStore &MRVMRuntimeKv::globalStore() const noexcept {
	return store;
}

Value MRVMRuntimeKv::ensureRoot(const std::string &name) {
	Value runtimeRoot = makeGlobalHashValue(rootHandle);
	const std::string key = upperKey(name);

	if (mrvmHashContainsValue(store, store, runtimeRoot, key)) {
		Value existing = mrvmHashReadValue(store, store, runtimeRoot, key);
		if (existing.type == TYPE_HASH) return existing;
	}

	Value root = makeGlobalHashValue(store.createHash());
	mrvmHashWriteValue(store, store, runtimeRoot, key, root);
	return root;
}

bool MRVMRuntimeKv::findRoot(const std::string &name, Value &root) {
	Value runtimeRoot = makeGlobalHashValue(rootHandle);
	const std::string key = upperKey(name);

	if (!mrvmHashContainsValue(store, store, runtimeRoot, key)) return false;
	root = mrvmHashReadValue(store, store, runtimeRoot, key);
	return root.type == TYPE_HASH;
}

Value MRVMRuntimeKv::ensureChild(const Value &parent, const std::string &key) {
	if (mrvmHashContainsValue(store, store, parent, key)) {
		Value child = mrvmHashReadValue(store, store, parent, key);
		if (child.type == TYPE_HASH) return child;
	}

	Value child = makeGlobalHashValue(store.createHash());
	mrvmHashWriteValue(store, store, parent, key, child);
	return child;
}

bool MRVMRuntimeKv::findChild(const Value &parent, const std::string &key, Value &child) {
	if (!mrvmHashContainsValue(store, store, parent, key)) return false;
	child = mrvmHashReadValue(store, store, parent, key);
	return child.type == TYPE_HASH;
}

bool MRVMRuntimeKv::eraseChild(const Value &parent, const std::string &key) {
	Value child;

	if (!mrvmHashContainsValue(store, store, parent, key)) return false;
	child = mrvmHashReadValue(store, store, parent, key);
	mrvmHashEraseValue(store, store, parent, key);
	store.eraseValueTrees(child, true);
	return true;
}

Value MRVMRuntimeKv::replaceChild(const Value &parent, const std::string &key) {
	static_cast<void>(eraseChild(parent, key));
	return ensureChild(parent, key);
}

std::string MRVMRuntimeKv::upperKey(std::string value) {
	for (char &c : value)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return value;
}

Value MRVMRuntimeKv::makeGlobalHashValue(int handle) {
	Value value;
	value.type = TYPE_HASH;
	value.hashHandle = handle;
	value.globalStorage = true;
	return value;
}
