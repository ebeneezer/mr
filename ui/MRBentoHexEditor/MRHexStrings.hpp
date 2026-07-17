#ifndef MRHEXSTRINGS_HPP
#define MRHEXSTRINGS_HPP

#include "../MRTextBufferModel.hpp"

#include <cstddef>

enum class MRHexStringSpanKind {
	Hidden,
	Ascii,
	CString,
	Utf8
};

struct MRHexStringCell {
	MRHexStringSpanKind kind;
	char text[5];
};

[[nodiscard]] MRHexStringCell mrHexStringCellAt(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset) noexcept;

#endif
