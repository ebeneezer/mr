#ifndef MRKEYMAPCONTEXT_HPP
#define MRKEYMAPCONTEXT_HPP

#include <optional>
#include <string_view>

enum class MRKeymapContext : unsigned char {
	None,
	Menu,
	Dialog,
	DialogList,
	List,
	ReadOnly,
	Edit
};

[[nodiscard]] std::optional<MRKeymapContext> parseKeymapContext(std::string_view text) noexcept;
[[nodiscard]] std::string_view keymapContextName(MRKeymapContext context) noexcept;

#endif
