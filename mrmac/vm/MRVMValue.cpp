#include "MRVMValue.hpp"

#include "MRVMHash.hpp"

#include "../mrmac.h"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace {
using Value = VirtualMachine::Value;

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

std::string mrvmUpperKey(const std::string &value) {
	std::string out = value;
	for (char &i : out)
		i = static_cast<char>(std::toupper(static_cast<unsigned char>(i)));
	return out;
}

Value mrvmMakeInt(int value) {
	Value v;
	v.type = TYPE_INT;
	v.i = value;
	return v;
}

Value mrvmMakeReal(double value) {
	Value v;
	v.type = TYPE_REAL;
	v.r = value;
	return v;
}

Value mrvmMakeString(const std::string &value) {
	Value v;
	v.type = TYPE_STR;
	v.s = value;
	return v;
}

Value mrvmMakeChar(unsigned char value) {
	Value v;
	v.type = TYPE_CHAR;
	v.c = value;
	return v;
}

Value mrvmMakeHash(int handle, bool globalStorage) {
	Value v;
	v.type = TYPE_HASH;
	v.hashHandle = handle;
	v.globalStorage = globalStorage;
	return v;
}

std::string mrvmCharToString(unsigned char c) {
	if (c == 0) return std::string();
	return std::string(1, static_cast<char>(c));
}

bool mrvmIsStringLike(const Value &value) {
	return value.type == TYPE_STR || value.type == TYPE_CHAR;
}

bool mrvmIsNumeric(const Value &value) {
	return value.type == TYPE_INT || value.type == TYPE_REAL;
}

std::string mrvmValueAsString(const Value &value) {
	char buf[128];

	switch (value.type) {
		case TYPE_STR:
			return value.s;
		case TYPE_CHAR:
			return mrvmCharToString(value.c);
		case TYPE_INT:
			std::snprintf(buf, sizeof(buf), "%d", value.i);
			return std::string(buf);
		case TYPE_REAL:
			std::snprintf(buf, sizeof(buf), "%.11g", value.r);
			return std::string(buf);
		default:
			return std::string();
	}
}

double mrvmValueAsReal(const Value &value) {
	if (value.type == TYPE_REAL) return value.r;
	if (value.type == TYPE_INT) return static_cast<double>(value.i);
	throw std::runtime_error("numeric value expected");
}

int mrvmValueAsInt(const Value &value) {
	if (value.type == TYPE_INT) return value.i;
	throw std::runtime_error("integer value expected");
}

bool mrvmValueHasContent(const Value &value) {
	switch (value.type) {
		case TYPE_INT:
			return value.i != 0;
		case TYPE_REAL:
			return value.r != 0.0;
		case TYPE_CHAR:
			return value.c != 0;
		case TYPE_STR:
			return !value.s.empty();
		case TYPE_HASH:
			return value.hashHandle > 0;
		case TYPE_INT_ARRAY:
		case TYPE_STR_ARRAY:
		case TYPE_CHAR_ARRAY:
		case TYPE_REAL_ARRAY:
		case TYPE_HASH_ARRAY:
			return !value.arrayValues.empty();
		default:
			return false;
	}
}

int mrvmCompareValues(const Value &a, const Value &b) {
	if (mrvmIsStringLike(a) && mrvmIsStringLike(b)) {
		std::string as = mrvmValueAsString(a);
		std::string bs = mrvmValueAsString(b);
		if (as < bs) return -1;
		if (as > bs) return 1;
		return 0;
	}

	if (mrvmIsNumeric(a) && mrvmIsNumeric(b)) {
		double av = mrvmValueAsReal(a);
		double bv = mrvmValueAsReal(b);
		if (av < bv) return -1;
		if (av > bv) return 1;
		return 0;
	}

	throw std::runtime_error("type mismatch");
}

std::string mrvmRemoveSpaceAscii(const std::string &value) {
	std::string out;
	bool previousWasSpace = false;
	std::size_t i = 0;
	std::size_t end = value.size();

	while (i < end && value[i] == ' ')
		++i;
	while (end > i && value[end - 1] == ' ')
		--end;

	for (; i < end; ++i) {
		char ch = value[i];
		if (ch == ' ') {
			if (!previousWasSpace) out.push_back(' ');
			previousWasSpace = true;
		} else {
			out.push_back(ch);
			previousWasSpace = false;
		}
	}
	return out;
}

static std::size_t findLastPathSeparator(const std::string &value) {
	std::size_t slash = value.find_last_of("\\/");
	if (slash == std::string::npos) return std::string::npos;
	return slash;
}

static std::size_t baseNameStart(const std::string &value) {
	std::size_t sep = findLastPathSeparator(value);
	if (sep != std::string::npos) return sep + 1;
	if (value.size() >= 2 && value[1] == ':') return 2;
	return 0;
}

std::string mrvmGetExtensionPart(const std::string &value) {
	std::size_t baseStart = baseNameStart(value);
	std::size_t dot = value.find_last_of('.');
	if (dot == std::string::npos || dot < baseStart) return std::string();
	return value.substr(dot);
}

std::string mrvmGetPathPart(const std::string &value) {
	std::size_t sep = findLastPathSeparator(value);
	if (sep != std::string::npos) return value.substr(0, sep + 1);
	if (value.size() >= 2 && value[1] == ':') return value.substr(0, 2);
	return std::string();
}

std::string mrvmTruncateExtensionPart(const std::string &value) {
	std::size_t baseStart = baseNameStart(value);
	std::size_t dot = value.find_last_of('.');
	if (dot == std::string::npos || dot < baseStart) return value;
	return value.substr(0, dot);
}

std::string mrvmTruncatePathPart(const std::string &value) {
	return value.substr(baseNameStart(value));
}

void mrvmEnforceStringLength(const std::string &value) {
	if (value.size() > 254) throw std::runtime_error("String length error.");
}

std::string mrvmUtf8FromCodepoint(std::uint32_t codepoint) {
	std::string text;
	int byteCount = 0;

	if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) throw std::runtime_error("UTF8 expects a valid Unicode codepoint.");
	byteCount = codepoint <= 0x7F ? 1 : (codepoint <= 0x7FF ? 2 : (codepoint <= 0xFFFF ? 3 : 4));
	switch (byteCount) {
		case 1:
			text.push_back(static_cast<char>(codepoint));
			break;
		case 2:
			text.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
			text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			break;
		case 3:
			text.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
			text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
			text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			break;
		case 4:
			text.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
			text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
			text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
			text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			break;
	}
	return text;
}

int mrvmCheckedStringIndex(int pos) {
	if (pos < 1 || pos > 254) throw std::runtime_error("Invalid string index on string copy operation.");
	return pos;
}

int mrvmCheckedInsertIndex(int pos) {
	if (pos < 0 || pos > 254) throw std::runtime_error("Invalid string index on string copy operation.");
	return pos;
}

int mrvmFindValErrorPosition(const std::string &text) {
	std::size_t i = 0;
	const std::size_t n = text.size();

	while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
		++i;
	if (i == n) return 1;

	if (text[i] == '+' || text[i] == '-') ++i;

	{
		const std::size_t firstDigit = i;
		while (i < n && std::isdigit(static_cast<unsigned char>(text[i])))
			++i;
		if (i == firstDigit) return static_cast<int>(firstDigit + 1);
	}

	while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
		++i;
	if (i != n) return static_cast<int>(i + 1);
	return 0;
}

int mrvmFindRValErrorPosition(const std::string &text) {
	std::size_t i = 0;
	const std::size_t n = text.size();

	while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
		++i;
	if (i == n) return 1;

	if (text[i] == '+' || text[i] == '-') ++i;

	{
		bool seenDigits = false;
		while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) {
			seenDigits = true;
			++i;
		}
		if (i < n && text[i] == '.') {
			++i;
			while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) {
				seenDigits = true;
				++i;
			}
		}
		if (!seenDigits) return static_cast<int>(i + 1);
	}

	if (i < n && (text[i] == 'e' || text[i] == 'E')) {
		const std::size_t expPos = i;
		++i;
		if (i < n && (text[i] == '+' || text[i] == '-')) ++i;
		{
			const std::size_t firstExpDigit = i;
			while (i < n && std::isdigit(static_cast<unsigned char>(text[i])))
				++i;
			if (i == firstExpDigit) return static_cast<int>(expPos + 1);
		}
	}

	while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
		++i;
	if (i != n) return static_cast<int>(i + 1);
	return 0;
}

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
			return mrvmMakeInt(0);
		case TYPE_REAL:
			return mrvmMakeReal(0.0);
		case TYPE_CHAR:
			return mrvmMakeChar(0);
		case TYPE_HASH:
			return mrvmMakeHash(0);
		case TYPE_INT_ARRAY:
		case TYPE_STR_ARRAY:
		case TYPE_CHAR_ARRAY:
		case TYPE_REAL_ARRAY:
		case TYPE_HASH_ARRAY:
			return mrvmMakeArrayValue(mrvmArrayElementTypeForArrayType(type));
		case TYPE_STR:
		default:
			return mrvmMakeString("");
	}
}

Value mrvmCoerceForStore(const Value &value, int targetType) {
	switch (targetType) {
		case TYPE_INT:
			if (value.type == TYPE_INT) return value;
			throw std::runtime_error("type mismatch");

		case TYPE_REAL:
			if (value.type == TYPE_REAL) return value;
			if (value.type == TYPE_INT) return mrvmMakeReal(static_cast<double>(value.i));
			throw std::runtime_error("type mismatch");

		case TYPE_STR:
			if (value.type == TYPE_STR) return value;
			if (value.type == TYPE_CHAR) return mrvmMakeString(mrvmCharToString(value.c));
			throw std::runtime_error("type mismatch");

		case TYPE_CHAR:
			if (value.type == TYPE_CHAR) return value;
			if (value.type == TYPE_STR) {
				if (value.s.empty()) return mrvmMakeChar(0);
				return mrvmMakeChar(static_cast<unsigned char>(value.s[0]));
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
