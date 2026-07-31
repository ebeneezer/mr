#include "MRVMDebugSession.hpp"

#include "vm/MRVMRuntimeGlobals.hpp"
#include "vm/MRVMRuntimeState.hpp"
#include "vm/MRVMExecSessions.hpp"
#include "vm/MRVMHash.hpp"
#include "vm/MRVMValue.hpp"
#include "mrmac.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace {

static MRMacroDebugVariableScope debugVariableScope(const std::string &name, const std::set<std::string> &closureNames, const std::set<std::string> &sessionNames) {
	if (closureNames.find(name) != closureNames.end()) return mrdVariableClosure;
	if (sessionNames.find(name) != sessionNames.end()) return mrdVariableSession;
	return mrdVariableLocal;
}

static const char *debugArrayTypeText(int type) noexcept {
	switch (type) {
		case TYPE_INT_ARRAY:
			return "int";
		case TYPE_STR_ARRAY:
			return "str";
		case TYPE_CHAR_ARRAY:
			return "char";
		case TYPE_REAL_ARRAY:
			return "real";
		case TYPE_HASH_ARRAY:
			return "hash";
		default:
			return "array";
	}
}

static std::string debugValueTextFor(const VirtualMachine::Value &value, const MRVMHashStore &localStore, const MRVMHashStore &globalStore) {
	std::ostringstream out;

	if (value.type == TYPE_HASH) {
		try {
			const MRVMHashStore &store = mrvmHashRuntimeStoreForValue(localStore, globalStore, value);

			out << "hash{" << store.keys(value.hashHandle).size() << " keys}";
		} catch (const std::exception &) {
			out << "hash{invalid}";
		}
		return out.str();
	}
	if (mrvmValueIsArrayType(value.type)) {
		out << debugArrayTypeText(value.type) << "[" << value.arrayValues.size() << "]";
		return out.str();
	}
	return mrvmValueAsString(value);
}

static std::string debugHashKeyDisplay(const std::string &key) {
	std::string display("[\"");

	for (char ch : key) {
		if (ch == '\\' || ch == '"') display.push_back('\\');
		if (ch == '\n') {
			display += "\\n";
			continue;
		}
		if (ch == '\r') {
			display += "\\r";
			continue;
		}
		if (ch == '\t') {
			display += "\\t";
			continue;
		}
		display.push_back(ch);
	}
	display += "\"]";
	return display;
}

static void appendDebugValueTree(std::vector<MRMacroDebugVariableSnapshot> &snapshots, const std::string &rootName, const std::string &displayName, const VirtualMachine::Value &value, MRMacroDebugVariableScope scope,
                                 std::vector<MRMacroDebugValuePathComponent> &path, int depth, const MRVMHashStore &localStore, const MRVMHashStore &globalStore, std::set<std::pair<bool, int>> &activeHashes) {
	MRMacroDebugVariableSnapshot snapshot;
	std::vector<std::string> hashKeys;
	bool hashValid = true;
	bool cycle = false;

	if (value.type == TYPE_HASH) {
		const std::pair<bool, int> identity(value.globalStorage, value.hashHandle);

		cycle = activeHashes.find(identity) != activeHashes.end();
		if (!cycle)
			try {
				hashKeys = mrvmHashRuntimeStoreForValue(localStore, globalStore, value).keys(value.hashHandle);
			} catch (const std::exception &) {
				hashValid = false;
			}
	}
	snapshot.name = rootName;
	snapshot.displayName = displayName;
	snapshot.type = value.type;
	snapshot.valueText = debugValueTextFor(value, localStore, globalStore);
	snapshot.scope = scope;
	snapshot.path = path;
	snapshot.depth = depth;
	snapshot.hasChildren = value.type == TYPE_HASH ? hashValid && !hashKeys.empty() : mrvmValueIsArrayType(value.type) && !value.arrayValues.empty();
	snapshot.cycleReference = cycle;
	if (cycle) snapshot.valueText += " (cycle)";
	snapshots.push_back(snapshot);
	if (cycle || !hashValid) return;

	if (value.type == TYPE_HASH) {
		const std::pair<bool, int> identity(value.globalStorage, value.hashHandle);
		const MRVMHashStore &store = mrvmHashRuntimeStoreForValue(localStore, globalStore, value);

		activeHashes.insert(identity);
		for (const std::string &key : hashKeys) {
			MRMacroDebugValuePathComponent component;

			component.kind = mrdValueHashKey;
			component.key = key;
			path.push_back(component);
			appendDebugValueTree(snapshots, rootName, debugHashKeyDisplay(key), store.read(value.hashHandle, key), scope, path, depth + 1, localStore, globalStore, activeHashes);
			path.pop_back();
		}
		activeHashes.erase(identity);
		return;
	}
	if (mrvmValueIsArrayType(value.type))
		for (std::size_t index = 0; index < value.arrayValues.size(); ++index) {
			MRMacroDebugValuePathComponent component;

			component.kind = mrdValueArrayIndex;
			component.index = static_cast<int>(index + 1);
			path.push_back(component);
			appendDebugValueTree(snapshots, rootName, "[" + std::to_string(index + 1) + "]", value.arrayValues[index], scope, path, depth + 1, localStore, globalStore, activeHashes);
			path.pop_back();
		}
}

static bool parseDebugScalarValue(int type, const std::string &text, VirtualMachine::Value &value, std::string &errorMessage) {
	char *end = nullptr;

	switch (type) {
		case TYPE_INT: {
			errno = 0;
			const long parsed = std::strtol(text.c_str(), &end, 10);

			if (end == text.c_str() || *end != '\0' || errno == ERANGE || parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
				errorMessage = "Expected an integer value.";
				return false;
			}
			value = mrvmMakeInt(static_cast<int>(parsed));
			return true;
		}
		case TYPE_REAL: {
			errno = 0;
			const double parsed = std::strtod(text.c_str(), &end);

			if (end == text.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(parsed)) {
				errorMessage = "Expected a finite real value.";
				return false;
			}
			value = mrvmMakeReal(parsed);
			return true;
		}
		case TYPE_STR:
			try {
				mrvmEnforceStringLength(text);
			} catch (const std::exception &error) {
				errorMessage = error.what();
				return false;
			}
			value = mrvmMakeString(text);
			return true;
		case TYPE_CHAR:
			if (text.size() > 1) {
				errorMessage = "Expected one character.";
				return false;
			}
			value = mrvmMakeChar(text.empty() ? 0 : static_cast<unsigned char>(text[0]));
			return true;
		default:
			errorMessage = "Expected a scalar debugger value.";
			return false;
	}
}

static bool makeDebugValue(int type, const std::string &text, bool globalStorage, MRVMHashStore &localStore, MRVMHashStore &globalStore, VirtualMachine::Value &value, std::string &errorMessage) {
	if (type == TYPE_HASH) {
		MRVMHashStore &store = globalStorage ? globalStore : localStore;

		value = mrvmMakeHash(store.createHash(), globalStorage);
		return true;
	}
	if (mrvmValueIsArrayType(type)) {
		value = mrvmMakeArrayValue(mrvmArrayElementTypeForArrayType(type));
		value.globalStorage = globalStorage;
		return true;
	}
	if (!parseDebugScalarValue(type, text, value, errorMessage)) return false;
	value.globalStorage = globalStorage;
	return true;
}

static bool applyDebugMutationAtValue(VirtualMachine::Value &value, const MRMacroDebugValueMutation &mutation, std::size_t pathIndex, MRVMHashStore &localStore, MRVMHashStore &globalStore, std::string &errorMessage) {
	if (pathIndex == mutation.target.path.size()) {
		VirtualMachine::Value replacement;

		if (value.type != mutation.target.type) {
			errorMessage = "Value no longer matches the debugger projection.";
			return false;
		}
		switch (mutation.action) {
			case mrdValueSetScalar:
				if (!parseDebugScalarValue(value.type, mutation.valueText, replacement, errorMessage)) return false;
				replacement.globalStorage = value.globalStorage;
				value = replacement;
				return true;
			case mrdValueAddHashEntry:
				if (value.type != TYPE_HASH) {
					errorMessage = "Hash insertion requires a hash.";
					return false;
				}
				try {
					if (mrvmHashContainsValue(localStore, globalStore, value, mutation.key)) {
						errorMessage = "Hash key already exists.";
						return false;
					}
					if (!makeDebugValue(mutation.valueType, mutation.valueText, value.globalStorage, localStore, globalStore, replacement, errorMessage)) return false;
					mrvmHashWriteValue(localStore, globalStore, value, mutation.key, replacement);
				} catch (const std::exception &error) {
					errorMessage = error.what();
					return false;
				}
				return true;
			case mrdValueAppendArrayElement:
				if (!mrvmValueIsArrayType(value.type)) {
					errorMessage = "Array append requires an array.";
					return false;
				}
				if (!makeDebugValue(value.arrayElementType, mutation.valueText, value.globalStorage, localStore, globalStore, replacement, errorMessage)) return false;
				try {
					mrvmArrayWriteValue(value, static_cast<int>(value.arrayValues.size() + 1), replacement, localStore, globalStore);
				} catch (const std::exception &error) {
					errorMessage = error.what();
					return false;
				}
				return true;
			case mrdValueEraseElement:
				errorMessage = "A root variable cannot be erased.";
				return false;
			case mrdValueRenameHashKey:
				errorMessage = "A root variable cannot be renamed.";
				return false;
		}
	}

	const MRMacroDebugValuePathComponent &component = mutation.target.path[pathIndex];
	const bool directTarget = pathIndex + 1 == mutation.target.path.size();
	if (component.kind == mrdValueHashKey) {
		VirtualMachine::Value child;

		if (value.type != TYPE_HASH) {
			errorMessage = "Debugger path no longer refers to a hash.";
			return false;
		}
		try {
			if (!mrvmHashContainsValue(localStore, globalStore, value, component.key)) {
				errorMessage = "Hash key no longer exists.";
				return false;
			}
			if (directTarget && mutation.action == mrdValueEraseElement) {
				mrvmHashEraseValue(localStore, globalStore, value, component.key);
				return true;
			}
			if (directTarget && mutation.action == mrdValueRenameHashKey) {
				if (mutation.key != component.key && mrvmHashContainsValue(localStore, globalStore, value, mutation.key)) {
					errorMessage = "Hash key already exists.";
					return false;
				}
				if (mutation.key == component.key) return true;
				child = mrvmHashReadValue(localStore, globalStore, value, component.key);
				mrvmHashWriteValue(localStore, globalStore, value, mutation.key, child);
				mrvmHashEraseValue(localStore, globalStore, value, component.key);
				return true;
			}
			child = mrvmHashReadValue(localStore, globalStore, value, component.key);
			if (!applyDebugMutationAtValue(child, mutation, pathIndex + 1, localStore, globalStore, errorMessage)) return false;
			mrvmHashWriteValue(localStore, globalStore, value, component.key, child);
			return true;
		} catch (const std::exception &error) {
			errorMessage = error.what();
			return false;
		}
	}
	if (!mrvmValueIsArrayType(value.type) || component.index <= 0 || static_cast<std::size_t>(component.index) > value.arrayValues.size()) {
		errorMessage = "Debugger path no longer refers to an array element.";
		return false;
	}
	if (directTarget && mutation.action == mrdValueEraseElement) {
		value.arrayValues.erase(value.arrayValues.begin() + component.index - 1);
		return true;
	}
	if (directTarget && mutation.action == mrdValueRenameHashKey) {
		errorMessage = "Array elements cannot be renamed.";
		return false;
	}
	return applyDebugMutationAtValue(value.arrayValues[static_cast<std::size_t>(component.index - 1)], mutation, pathIndex + 1, localStore, globalStore, errorMessage);
}

}

std::string VirtualMachine::debugValueText(const Value &value) const {
	return debugValueTextFor(value, *mHashStore, mrvmRuntimeKv().globalStore());
}

void VirtualMachine::appendDebugVariables(MRMacroDebugRunResult &result) const {
	MRVMHashStore &globalStore = mrvmRuntimeKv().globalStore();

	for (const std::pair<const std::string, Value> &entry : variables) {
		std::vector<MRMacroDebugValuePathComponent> path;
		std::set<std::pair<bool, int>> activeHashes;

		appendDebugValueTree(result.variables, entry.first, entry.first, entry.second, debugVariableScope(entry.first, mClosureVariableNames, mSessionVariableNames), path, 0, *mHashStore, globalStore, activeHashes);
	}
	for (const std::string &key : mrvmRuntimeGlobalOrderValues(mrvmRuntimeKv())) {
		MRVMRuntimeGlobalEntry entry;
		std::vector<MRMacroDebugValuePathComponent> path;
		std::set<std::pair<bool, int>> activeHashes;

		if (!mrvmRuntimeGlobalRead(mrvmRuntimeKv(), key, entry)) continue;
		appendDebugValueTree(result.variables, key, key, entry.value, mrdVariableAppGlobal, path, 0, globalStore, globalStore, activeHashes);
	}
}

bool VirtualMachine::mutateDebugValue(const MRMacroDebugValueMutation &mutation, std::vector<MRMacroDebugVariableSnapshot> &updatedVariables, std::string &errorMessage) {
	Value rootValue;
	Value storedValue;
	Value *root = nullptr;
	std::map<std::string, Value>::iterator local;
	MRVMRuntimeGlobalEntry global;
	MRMacroDebugVariableScope actualScope = mrdVariableLocal;

	updatedVariables.clear();
	errorMessage.clear();
	if (!debugState.paused) {
		errorMessage = "Debug session is not paused.";
		return false;
	}
	if (mutation.target.name.empty()) {
		errorMessage = "Debugger value has no root variable.";
		return false;
	}
	if (mutation.target.scope == mrdVariableAppGlobal) {
		if (!mrvmRuntimeGlobalRead(mrvmRuntimeKv(), mutation.target.name, global)) {
			errorMessage = "App global no longer exists.";
			return false;
		}
		rootValue = global.value;
		root = &rootValue;
		actualScope = mrdVariableAppGlobal;
	} else {
		local = variables.find(mutation.target.name);
		if (local == variables.end()) {
			errorMessage = "Variable no longer exists in the paused debug session.";
			return false;
		}
		actualScope = debugVariableScope(local->first, mClosureVariableNames, mSessionVariableNames);
		root = &local->second;
	}
	if (actualScope != mutation.target.scope) {
		errorMessage = "Variable no longer matches the debugger scope.";
		return false;
	}
	if (actualScope == mrdVariableClosure || actualScope == mrdVariableSession) {
		MRVMHashStore stagedStore;
		Value stagedValue;

		try {
			stagedValue = mrvmHashCopyValueForStore(*root, *mHashStore, mrvmRuntimeKv().globalStore(), stagedStore, false);
		} catch (const std::exception &error) {
			errorMessage = error.what();
			return false;
		}
		if (!applyDebugMutationAtValue(stagedValue, mutation, 0, stagedStore, stagedStore, errorMessage)) return false;
		if (actualScope == mrdVariableClosure && !mClosureId.empty()) {
			if (!mrvmExecSessionsWriteClosureVariable(mrvmRuntimeKv(), mClosureId, mutation.target.name, stagedValue, stagedStore, &storedValue)) {
				errorMessage = "Closure variable could not be stored.";
				return false;
			}
		} else if (actualScope == mrdVariableSession && mExecutionSessionId != 0) {
			if (!mrvmExecSessionsWriteSessionVariable(mrvmRuntimeKv(), mExecutionSessionId, mutation.target.name, stagedValue, stagedStore, &storedValue)) {
				errorMessage = "Session variable could not be stored.";
				return false;
			}
		} else {
			errorMessage = "Debugger variable has no persistent execution context.";
			return false;
		}
		*root = std::move(storedValue);
	} else if (!applyDebugMutationAtValue(*root, mutation, 0, *mHashStore, mrvmRuntimeKv().globalStore(), errorMessage))
		return false;
	if (actualScope == mrdVariableAppGlobal)
		mrvmRuntimeGlobalWrite(mrvmRuntimeKv(), mutation.target.name, global.type, *root);
	{
		MRMacroDebugRunResult snapshot;

		appendDebugVariables(snapshot);
		updatedVariables = std::move(snapshot.variables);
	}
	return true;
}
