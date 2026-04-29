#include "MRKeymapContext.hpp"

#include <array>
#include <cctype>
#include <string>

namespace {
struct ContextSpec {
	std::string_view name;
	MRKeymapContext context;
};

constexpr std::array contexts{
    ContextSpec{"MENU", MRKeymapContext::Menu}, ContextSpec{"DIALOG", MRKeymapContext::Dialog}, ContextSpec{"DIALOG_LIST", MRKeymapContext::DialogList}, ContextSpec{"LIST", MRKeymapContext::List}, ContextSpec{"READONLY", MRKeymapContext::ReadOnly}, ContextSpec{"EDIT", MRKeymapContext::Edit},
};

std::string upperAscii(std::string_view text) {
	std::string upper;
	upper.reserve(text.size());
	for (const unsigned char ch : text)
		upper.push_back(static_cast<char>(std::toupper(ch)));
	return upper;
}
} // namespace

std::optional<MRKeymapContext> parseKeymapContext(std::string_view text) noexcept {
	const std::string upper = upperAscii(text);
	for (const ContextSpec &entry : contexts)
		if (entry.name == upper) return entry.context;
	return std::nullopt;
}

std::string_view keymapContextName(MRKeymapContext context) noexcept {
	for (const ContextSpec &entry : contexts)
		if (entry.context == context) return entry.name;
	return "NONE";
}
