#include "MRVMValue.hpp"

#include "MRVMHash.hpp"

#include "../mrmac.h"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace {
using Value = VirtualMachine::Value;

Value makeIntValue(int value) {
	Value v;
	v.type = TYPE_INT;
	v.i = value;
	return v;
}

Value makeRealValue(double value) {
	Value v;
	v.type = TYPE_REAL;
	v.r = value;
	return v;
}

Value makeStringValue(const std::string &value) {
	Value v;
	v.type = TYPE_STR;
	v.s = value;
	return v;
}

Value makeCharValue(unsigned char value) {
	Value v;
	v.type = TYPE_CHAR;
	v.c = value;
	return v;
}

Value makeHashValue(int handle) {
	Value v;
	v.type = TYPE_HASH;
	v.hashHandle = handle;
	return v;
}

std::string charToString(unsigned char c) {
	if (c == 0) return std::string();
	return std::string(1, static_cast<char>(c));
}

Value coerceArrayElementForStore(const Value &value, int elementType, MRVMHashStore &localStore, MRVMHashStore &globalStore, bool targetGlobalStorage) {
	Value coerced = mrvmCoerceForStore(value, elementType);
	if (coerced.type == TYPE_HASH) {
		MRVMHashStore &targetStore = targetGlobalStorage ? globalStore : localStore;
		return mrvmHashCopyValueForStore(coerced, localStore, globalStore, targetStore, targetGlobalStorage);
	}
	coerced.globalStorage = targetGlobalStorage;
	return coerced;
}
} // namespace

bool mrvmValueIsArrayType(int type) {
	return type == TYPE_INT_ARRAY || type == TYPE_STR_ARRAY || type == TYPE_CHAR_ARRAY || type == TYPE_REAL_ARRAY || type == TYPE_HASH_ARRAY;
}

int mrvmArrayTypeForElementType(int elementType) {
	switch (elementType) {
		case TYPE_INT:
			return TYPE_INT_ARRAY;
		case TYPE_STR:
			return TYPE_STR_ARRAY;
		case TYPE_CHAR:
			return TYPE_CHAR_ARRAY;
		case TYPE_REAL:
			return TYPE_REAL_ARRAY;
		case TYPE_HASH:
			return TYPE_HASH_ARRAY;
		default:
			throw std::runtime_error("unknown array element type");
	}
}

int mrvmArrayElementTypeForArrayType(int arrayType) {
	switch (arrayType) {
		case TYPE_INT_ARRAY:
			return TYPE_INT;
		case TYPE_STR_ARRAY:
			return TYPE_STR;
		case TYPE_CHAR_ARRAY:
			return TYPE_CHAR;
		case TYPE_REAL_ARRAY:
			return TYPE_REAL;
		case TYPE_HASH_ARRAY:
			return TYPE_HASH;
		default:
			throw std::runtime_error("array value expected");
	}
}

Value mrvmMakeArrayValue(int elementType) {
	Value v;
	v.type = mrvmArrayTypeForElementType(elementType);
	v.arrayElementType = elementType;
	return v;
}

Value mrvmDefaultValueForType(int type) {
	switch (type) {
		case TYPE_INT:
			return makeIntValue(0);
		case TYPE_REAL:
			return makeRealValue(0.0);
		case TYPE_CHAR:
			return makeCharValue(0);
		case TYPE_HASH:
			return makeHashValue(0);
		case TYPE_INT_ARRAY:
		case TYPE_STR_ARRAY:
		case TYPE_CHAR_ARRAY:
		case TYPE_REAL_ARRAY:
		case TYPE_HASH_ARRAY:
			return mrvmMakeArrayValue(mrvmArrayElementTypeForArrayType(type));
		case TYPE_STR:
		default:
			return makeStringValue("");
	}
}

Value mrvmCoerceForStore(const Value &value, int targetType) {
	switch (targetType) {
		case TYPE_INT:
			if (value.type == TYPE_INT) return value;
			throw std::runtime_error("type mismatch");

		case TYPE_REAL:
			if (value.type == TYPE_REAL) return value;
			if (value.type == TYPE_INT) return makeRealValue(static_cast<double>(value.i));
			throw std::runtime_error("type mismatch");

		case TYPE_STR:
			if (value.type == TYPE_STR) return value;
			if (value.type == TYPE_CHAR) return makeStringValue(charToString(value.c));
			throw std::runtime_error("type mismatch");

		case TYPE_CHAR:
			if (value.type == TYPE_CHAR) return value;
			if (value.type == TYPE_STR) {
				if (value.s.empty()) return makeCharValue(0);
				return makeCharValue(static_cast<unsigned char>(value.s[0]));
			}
			throw std::runtime_error("type mismatch");

		case TYPE_HASH:
			if (value.type == TYPE_HASH) return value;
			throw std::runtime_error("type mismatch");

		case TYPE_INT_ARRAY:
		case TYPE_STR_ARRAY:
		case TYPE_CHAR_ARRAY:
		case TYPE_REAL_ARRAY:
		case TYPE_HASH_ARRAY:
			if (value.type == targetType) return value;
			throw std::runtime_error("type mismatch");

		default:
			throw std::runtime_error("unknown variable type");
	}
}

Value mrvmArrayReadValue(const Value &arrayValue, int index) {
	if (!mrvmValueIsArrayType(arrayValue.type)) throw std::runtime_error("array value expected");
	if (index <= 0 || static_cast<std::size_t>(index) > arrayValue.arrayValues.size()) throw std::runtime_error("array index out of range");
	return arrayValue.arrayValues[static_cast<std::size_t>(index - 1)];
}

void mrvmArrayWriteValue(Value &arrayValue, int index, const Value &value, MRVMHashStore &localStore, MRVMHashStore &globalStore) {
	Value stored;

	if (!mrvmValueIsArrayType(arrayValue.type)) throw std::runtime_error("array value expected");
	if (index <= 0) throw std::runtime_error("array index out of range");
	stored = coerceArrayElementForStore(value, arrayValue.arrayElementType, localStore, globalStore, arrayValue.globalStorage);
	if (static_cast<std::size_t>(index) > arrayValue.arrayValues.size()) {
		Value defaultElement = coerceArrayElementForStore(mrvmDefaultValueForType(arrayValue.arrayElementType), arrayValue.arrayElementType, localStore, globalStore, arrayValue.globalStorage);
		arrayValue.arrayValues.resize(static_cast<std::size_t>(index), defaultElement);
	}
	arrayValue.arrayValues[static_cast<std::size_t>(index - 1)] = stored;
}
