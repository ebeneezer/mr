#include "MRHexInspector.hpp"
#include "MRHexUtf8.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

namespace {

using InspectorLineFormatter = std::string (*)(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool littleEndian);

enum InspectorLineValueKind {
	ilvFormatted = 0,
	ilvUnsigned,
	ilvSigned
};

struct InspectorLineDescriptor {
	const char *label;
	InspectorLineValueKind valueKind;
	InspectorLineFormatter formatter;
	std::size_t byteCount;
};

bool readUnsigned(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, std::size_t byteCount, bool littleEndian, std::uint64_t &value) {
	if (byteCount == 0 || byteCount > sizeof(value) || offset > snapshot.length() || byteCount > snapshot.length() - offset) return false;
	value = 0;
	for (std::size_t index = 0; index < byteCount; ++index) {
		const std::uint64_t byte = static_cast<unsigned char>(snapshot.charAt(offset + index));

		if (littleEndian) value |= byte << (index * 8);
		else
			value = (value << 8) | byte;
	}
	return true;
}

std::int64_t signedValue(std::uint64_t value, std::size_t byteCount) noexcept {
	const unsigned bits = static_cast<unsigned>(byteCount * 8);

	if (bits < 64 && (value & (std::uint64_t(1) << (bits - 1))) != 0) value |= ~((std::uint64_t(1) << bits) - 1);
	return static_cast<std::int64_t>(value);
}

std::string unsignedText(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, std::size_t byteCount, bool littleEndian) {
	std::uint64_t value = 0;

	return readUnsigned(snapshot, offset, byteCount, littleEndian, value) ? std::to_string(value) : "End of File";
}

std::string signedText(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, std::size_t byteCount, bool littleEndian) {
	std::uint64_t value = 0;

	return readUnsigned(snapshot, offset, byteCount, littleEndian, value) ? std::to_string(signedValue(value, byteCount)) : "End of File";
}

std::string binaryText(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool) {
	if (offset >= snapshot.length()) return "End of File";
	const unsigned char value = static_cast<unsigned char>(snapshot.charAt(offset));
	char text[9] = {0};

	for (int bit = 7; bit >= 0; --bit) text[7 - bit] = (value & (1u << bit)) != 0 ? '1' : '0';
	return text;
}

std::string octalText(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool) {
	if (offset >= snapshot.length()) return "End of File";
	char text[8] = {0};

	std::snprintf(text, sizeof(text), "%03o", static_cast<unsigned>(static_cast<unsigned char>(snapshot.charAt(offset))));
	return text;
}

float halfToFloat(std::uint16_t bits) noexcept {
	const unsigned sign = (bits >> 15) & 0x1;
	const unsigned exponent = (bits >> 10) & 0x1F;
	const unsigned fraction = bits & 0x3FF;
	float value = 0.0F;

	if (exponent == 0) value = std::ldexp(static_cast<float>(fraction), -24);
	else if (exponent == 0x1F)
		value = fraction == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
	else
		value = std::ldexp(1.0F + static_cast<float>(fraction) / 1024.0F, static_cast<int>(exponent) - 15);
	return sign != 0 ? -value : value;
}

std::string floatText(float value) {
	std::ostringstream out;

	out << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
	return out.str();
}

std::string doubleText(double value) {
	std::ostringstream out;

	out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
	return out.str();
}

std::string float16Text(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool littleEndian) {
	std::uint64_t bits = 0;

	return readUnsigned(snapshot, offset, 2, littleEndian, bits) ? floatText(halfToFloat(static_cast<std::uint16_t>(bits))) : "End of File";
}

std::string bfloat16Text(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool littleEndian) {
	std::uint64_t bits = 0;
	std::uint32_t floatBits = 0;
	float value = 0.0F;

	if (!readUnsigned(snapshot, offset, 2, littleEndian, bits)) return "End of File";
	floatBits = static_cast<std::uint32_t>(bits) << 16;
	std::memcpy(&value, &floatBits, sizeof(value));
	return floatText(value);
}

std::string float32Text(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool littleEndian) {
	std::uint64_t bits = 0;
	std::uint32_t floatBits = 0;
	float value = 0.0F;

	if (!readUnsigned(snapshot, offset, 4, littleEndian, bits)) return "End of File";
	floatBits = static_cast<std::uint32_t>(bits);
	std::memcpy(&value, &floatBits, sizeof(value));
	return floatText(value);
}

std::string float64Text(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool littleEndian) {
	std::uint64_t bits = 0;
	double value = 0.0;

	if (!readUnsigned(snapshot, offset, 8, littleEndian, bits)) return "End of File";
	std::memcpy(&value, &bits, sizeof(value));
	return doubleText(value);
}

std::string uleb128Text(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool) {
	std::uint64_t value = 0;

	for (std::size_t index = 0; index < 10; ++index) {
		if (offset + index >= snapshot.length()) return "End of File";
		const unsigned char byte = static_cast<unsigned char>(snapshot.charAt(offset + index));

		if (index == 9 && (byte & 0x7E) != 0) return "Invalid";
		value |= static_cast<std::uint64_t>(index == 9 ? byte & 0x01 : byte & 0x7F) << (index * 7);
		if ((byte & 0x80) == 0) return std::to_string(value);
	}
	return "Invalid";
}

std::string sleb128Text(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool) {
	std::uint64_t value = 0;
	unsigned shift = 0;

	for (std::size_t index = 0; index < 10; ++index) {
		if (offset + index >= snapshot.length()) return "End of File";
		const unsigned char byte = static_cast<unsigned char>(snapshot.charAt(offset + index));

		if (index == 9 && byte != 0x00 && byte != 0x7F) return "Invalid";
		value |= static_cast<std::uint64_t>(index == 9 ? byte & 0x01 : byte & 0x7F) << shift;
		shift += 7;
		if ((byte & 0x80) == 0) {
			if (shift < 64 && (byte & 0x40) != 0) value |= ~((std::uint64_t(1) << shift) - 1);
			return std::to_string(static_cast<std::int64_t>(value));
		}
	}
	return "Invalid";
}

std::string guidText(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool littleEndian) {
	std::uint64_t first = 0;
	std::uint64_t second = 0;
	std::uint64_t third = 0;
	char text[48] = {0};

	if (!readUnsigned(snapshot, offset, 4, littleEndian, first) || !readUnsigned(snapshot, offset + 4, 2, littleEndian, second) || !readUnsigned(snapshot, offset + 6, 2, littleEndian, third) || offset > snapshot.length() || 16 > snapshot.length() - offset) return "End of File";
	std::snprintf(text, sizeof(text), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", static_cast<unsigned>(first), static_cast<unsigned>(second), static_cast<unsigned>(third),
	              static_cast<unsigned>(static_cast<unsigned char>(snapshot.charAt(offset + 8))), static_cast<unsigned>(static_cast<unsigned char>(snapshot.charAt(offset + 9))),
	              static_cast<unsigned>(static_cast<unsigned char>(snapshot.charAt(offset + 10))), static_cast<unsigned>(static_cast<unsigned char>(snapshot.charAt(offset + 11))),
	              static_cast<unsigned>(static_cast<unsigned char>(snapshot.charAt(offset + 12))), static_cast<unsigned>(static_cast<unsigned char>(snapshot.charAt(offset + 13))),
	              static_cast<unsigned>(static_cast<unsigned char>(snapshot.charAt(offset + 14))), static_cast<unsigned>(static_cast<unsigned char>(snapshot.charAt(offset + 15))));
	return text;
}

std::string asciiText(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool) {
	if (offset >= snapshot.length()) return "End of File";
	const unsigned char value = static_cast<unsigned char>(snapshot.charAt(offset));

	return value >= 0x20 && value <= 0x7E ? std::string(1, static_cast<char>(value)) : "<control>";
}

std::string utf8Text(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool) {
	if (offset >= snapshot.length()) return "End of File";
	const unsigned char first = static_cast<unsigned char>(snapshot.charAt(offset));
	std::size_t width = 0;

	if (first < 0x80) return asciiText(snapshot, offset, false);
	if (!mrHexUtf8CodePointAt(snapshot, offset, width)) return "<invalid>";
	std::string value;

	for (std::size_t index = 0; index < width; ++index) value.push_back(snapshot.charAt(offset + index));
	return value;
}

std::string utf16Text(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool littleEndian) {
	std::uint64_t codeUnit = 0;

	if (!readUnsigned(snapshot, offset, 2, littleEndian, codeUnit)) return "End of File";
	if (codeUnit < 0x80) return codeUnit >= 0x20 ? std::string(1, static_cast<char>(codeUnit)) : "<control>";
	if (codeUnit >= 0xD800 && codeUnit <= 0xDFFF) return "<surrogate>";
	std::string value;

	if (codeUnit < 0x800) {
		value.push_back(static_cast<char>(0xC0 | (codeUnit >> 6)));
		value.push_back(static_cast<char>(0x80 | (codeUnit & 0x3F)));
	} else {
		value.push_back(static_cast<char>(0xE0 | (codeUnit >> 12)));
		value.push_back(static_cast<char>(0x80 | ((codeUnit >> 6) & 0x3F)));
		value.push_back(static_cast<char>(0x80 | (codeUnit & 0x3F)));
	}
	return value;
}

std::string legacyEncodingText(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool) {
	if (offset >= snapshot.length()) return "End of File";
	const unsigned char value = static_cast<unsigned char>(snapshot.charAt(offset));

	return value >= 0x20 && value <= 0x7E ? std::string(1, static_cast<char>(value)) : "<profile required>";
}

constexpr InspectorLineDescriptor kInspectorLineDescriptors[] = {
	{"binary", ilvFormatted, binaryText, 0},
	{"octal", ilvFormatted, octalText, 0},
	{"uint8", ilvUnsigned, nullptr, 1},
	{"int8", ilvSigned, nullptr, 1},
	{"uint16", ilvUnsigned, nullptr, 2},
	{"int16", ilvSigned, nullptr, 2},
	{"uint24", ilvUnsigned, nullptr, 3},
	{"int24", ilvSigned, nullptr, 3},
	{"uint32", ilvUnsigned, nullptr, 4},
	{"int32", ilvSigned, nullptr, 4},
	{"uint64", ilvUnsigned, nullptr, 8},
	{"int64", ilvSigned, nullptr, 8},
	{"ULEB128", ilvFormatted, uleb128Text, 0},
	{"SLEB128", ilvFormatted, sleb128Text, 0},
	{"float16", ilvFormatted, float16Text, 0},
	{"bfloat16", ilvFormatted, bfloat16Text, 0},
	{"float32", ilvFormatted, float32Text, 0},
	{"float64", ilvFormatted, float64Text, 0},
	{"GUID", ilvFormatted, guidText, 0},
	{"ASCII", ilvFormatted, asciiText, 0},
	{"UTF-8", ilvFormatted, utf8Text, 0},
	{"UTF-16", ilvFormatted, utf16Text, 0},
	{"GB18030", ilvFormatted, legacyEncodingText, 0},
	{"BIG5", ilvFormatted, legacyEncodingText, 0},
	{"SHIFT-JIS", ilvFormatted, legacyEncodingText, 0},
};

} // namespace

void mrBuildHexInspectorLines(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool littleEndian, std::vector<MRHexInspectorLine> &lines) {
	lines.clear();
	lines.reserve(sizeof(kInspectorLineDescriptors) / sizeof(kInspectorLineDescriptors[0]) + 1);
	for (const InspectorLineDescriptor &descriptor : kInspectorLineDescriptors) {
		std::string value;

		switch (descriptor.valueKind) {
			case ilvFormatted:
				value = descriptor.formatter(snapshot, offset, littleEndian);
				break;
			case ilvUnsigned:
				value = unsignedText(snapshot, offset, descriptor.byteCount, littleEndian);
				break;
			case ilvSigned:
				value = signedText(snapshot, offset, descriptor.byteCount, littleEndian);
				break;
		}
		lines.push_back({descriptor.label, value});
	}
	lines.push_back({littleEndian ? "[x] Little endian" : "[ ] Little endian", std::string()});
}
