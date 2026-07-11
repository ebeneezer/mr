#ifndef MRSYNTAXBASIC_HPP
#define MRSYNTAXBASIC_HPP

#include <string_view>

enum class MRBasicBlockKind : unsigned char {
	None,
	Conditional,
	Select,
	Loop,
	Procedure,
	Type,
	With,
	Try
};

enum class MRBasicBlockDisposition : unsigned char {
	None,
	Open,
	Continue,
	Close
};

struct MRBasicBlockLine {
	MRBasicBlockKind kind;
	MRBasicBlockDisposition disposition;
};

[[nodiscard]] MRBasicBlockLine mrBasicClassifyBlockLine(std::string_view line) noexcept;
[[nodiscard]] bool mrBasicLineIsComment(std::string_view line) noexcept;

#endif
