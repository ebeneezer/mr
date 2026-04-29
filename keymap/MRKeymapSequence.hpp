#ifndef MRKEYMAPSEQUENCE_HPP
#define MRKEYMAPSEQUENCE_HPP

#include "MRKeymapToken.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class MRKeymapSequence final {
  public:
	MRKeymapSequence() noexcept = default;

	[[nodiscard]] static std::optional<MRKeymapSequence> parse(std::string_view text);
	[[nodiscard]] std::string toString() const;
	[[nodiscard]] std::span<const MRKeymapToken> tokens() const noexcept {
		return tokenValues;
	}
	[[nodiscard]] bool empty() const noexcept {
		return tokenValues.empty();
	}
	[[nodiscard]] std::size_t size() const noexcept {
		return tokenValues.size();
	}

	auto operator==(const MRKeymapSequence &) const noexcept -> bool = default;

  private:
	std::vector<MRKeymapToken> tokenValues;

	explicit MRKeymapSequence(std::vector<MRKeymapToken> tokens) noexcept : tokenValues(std::move(tokens)) {
	}
};

#endif
