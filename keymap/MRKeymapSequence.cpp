#include "MRKeymapSequence.hpp"

#include <cctype>

namespace {
std::string_view trimView(std::string_view text) noexcept {
	std::size_t first = 0;
	while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])) != 0)
		++first;
	std::size_t last = text.size();
	while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])) != 0)
		--last;
	return text.substr(first, last - first);
}
} // namespace

std::optional<MRKeymapSequence> MRKeymapSequence::parse(std::string_view text) {
	text = trimView(text);
	if (text.empty()) return std::nullopt;

	std::vector<MRKeymapToken> tokens;
	std::size_t pos = 0;
	while (pos < text.size()) {
		while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0)
			++pos;
		if (pos >= text.size()) break;
		if (text[pos] != '<') return std::nullopt;

		const std::size_t tokenEnd = text.find('>', pos + 1);
		if (tokenEnd == std::string_view::npos) return std::nullopt;
		const std::string_view tokenText = text.substr(pos, tokenEnd - pos + 1);
		const auto token = MRKeymapToken::parse(tokenText);
		if (!token) return std::nullopt;
		tokens.push_back(*token);
		pos = tokenEnd + 1;
		if (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) == 0 && text[pos] != '<') return std::nullopt;
	}

	if (tokens.empty()) return std::nullopt;
	return MRKeymapSequence(std::move(tokens));
}

std::string MRKeymapSequence::toString() const {
	std::string text;
	for (std::size_t i = 0; i < tokenValues.size(); ++i) {
		if (i != 0) text += ' ';
		text += tokenValues[i].toString();
	}
	return text;
}
