#ifndef MRKEYMAPTRIE_HPP
#define MRKEYMAPTRIE_HPP

#include "MRKeymapProfile.hpp"

#include <span>
#include <string>
#include <vector>

class MRKeymapTrie final {
  public:
	enum class DecisionKind : unsigned char {
		NoMatch,
		Pending,
		Matched
	};

	struct Decision {
		DecisionKind kind{DecisionKind::NoMatch};
		MRKeymapBindingTarget target{};
		std::string description;
	};

	bool rebuild(std::span<const MRKeymapBindingRecord> bindings, std::string *errorMessage = nullptr);
	[[nodiscard]] Decision decide(MRKeymapContext context, std::span<const MRKeymapToken> sequence) const;

  private:
	struct Edge {
		MRKeymapToken token;
		std::size_t nextIndex{0};
	};

	struct Node {
		std::vector<Edge> edges;
		bool terminal{false};
		MRKeymapBindingTarget target{};
		std::string description;
	};

	[[nodiscard]] static std::size_t contextIndex(MRKeymapContext context) noexcept;

	std::vector<Node> nodes;
	std::vector<std::size_t> roots;
};

#endif
