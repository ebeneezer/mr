#include "MRHexUtf8.hpp"

#include <cstdint>

namespace {

struct Utf8LeadingByteRange {
	unsigned char first;
	unsigned char last;
	unsigned char width;
	unsigned char payloadMask;
	std::uint32_t minimumCodePoint;
};

constexpr Utf8LeadingByteRange kUtf8LeadingByteRanges[] = {
	{0xC2, 0xDF, 2, 0x1F, 0x80},
	{0xE0, 0xEF, 3, 0x0F, 0x800},
	{0xF0, 0xF4, 4, 0x07, 0x10000},
};

const Utf8LeadingByteRange *utf8LeadingByteRange(unsigned char value) noexcept {
	for (const Utf8LeadingByteRange &range : kUtf8LeadingByteRanges)
		if (value >= range.first && value <= range.last) return &range;
	return nullptr;
}

} // namespace

bool mrHexUtf8CodePointAt(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, std::size_t &width) noexcept {
	if (offset >= snapshot.length()) return false;
	const unsigned char first = static_cast<unsigned char>(snapshot.charAt(offset));
	const Utf8LeadingByteRange *range = utf8LeadingByteRange(first);
	if (range == nullptr) return false;
	width = range->width;
	if (width > snapshot.length() - offset) return false;

	std::uint32_t codePoint = first & range->payloadMask;
	for (std::size_t index = 1; index < width; ++index) {
		const unsigned char byte = static_cast<unsigned char>(snapshot.charAt(offset + index));
		if ((byte & 0xC0) != 0x80) return false;
		codePoint = (codePoint << 6) | (byte & 0x3F);
	}
	return codePoint >= range->minimumCodePoint && codePoint <= 0x10FFFF && (codePoint < 0xD800 || codePoint > 0xDFFF);
}
