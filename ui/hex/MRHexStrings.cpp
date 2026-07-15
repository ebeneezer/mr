#include "MRHexStrings.hpp"

#include "MRHexUtf8.hpp"

namespace {

constexpr std::size_t kMaximumStringProbe = 512;

bool isPrintableAscii(unsigned char value) noexcept {
	return value >= 0x20 && value <= 0x7E;
}

MRHexStringCell hiddenCell() noexcept {
	MRHexStringCell cell;

	cell.kind = MRHexStringSpanKind::Hidden;
	cell.text[0] = '\0';
	return cell;
}

bool utf8SpanAt(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, std::size_t &width) noexcept {
	std::size_t first = offset;
	std::size_t last = offset;
	std::size_t codePointWidth = 0;
	std::size_t codePointCount = 0;

	if (!mrHexUtf8CodePointAt(snapshot, offset, width)) return false;
	for (std::size_t distance = 1; distance <= 4 && distance <= first; ++distance) {
		std::size_t previousWidth = 0;
		const std::size_t candidate = first - distance;

		if (mrHexUtf8CodePointAt(snapshot, candidate, previousWidth) && candidate + previousWidth == first) first = candidate;
	}
	last = first;
	while (last < snapshot.length() && last - first < kMaximumStringProbe && mrHexUtf8CodePointAt(snapshot, last, codePointWidth)) {
		last += codePointWidth;
		++codePointCount;
	}
	return codePointCount >= 2 && offset >= first && offset < last;
}

bool asciiSpanAt(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, std::size_t &first, std::size_t &last) noexcept {
	const std::size_t length = snapshot.length();

	if (offset >= length || !isPrintableAscii(static_cast<unsigned char>(snapshot.charAt(offset)))) return false;
	first = offset;
	last = offset + 1;
	while (first > 0 && offset - first < kMaximumStringProbe && isPrintableAscii(static_cast<unsigned char>(snapshot.charAt(first - 1)))) --first;
	while (last < length && last - offset < kMaximumStringProbe && isPrintableAscii(static_cast<unsigned char>(snapshot.charAt(last)))) ++last;
	return last - first >= 4;
}

} // namespace

MRHexStringCell mrHexStringCellAt(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset) noexcept {
	MRHexStringCell cell = hiddenCell();
	std::size_t first = 0;
	std::size_t last = 0;
	std::size_t utf8Width = 0;

	if (asciiSpanAt(snapshot, offset, first, last)) {
		cell.kind = last < snapshot.length() && snapshot.charAt(last) == '\0' ? MRHexStringSpanKind::CString : MRHexStringSpanKind::Ascii;
		cell.text[0] = snapshot.charAt(offset);
		cell.text[1] = '\0';
		return cell;
	}
	if (!utf8SpanAt(snapshot, offset, utf8Width)) return cell;
	cell.kind = MRHexStringSpanKind::Utf8;
	for (std::size_t index = 0; index < utf8Width; ++index)
		cell.text[index] = snapshot.charAt(offset + index);
	cell.text[utf8Width] = '\0';
	return cell;
}
