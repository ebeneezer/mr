#include "MRKeymapTrie.hpp"

#include <array>
#include <algorithm>

namespace {
constexpr std::size_t kContextCount = static_cast<std::size_t>(MRKeymapContext::Edit) + 1;

bool setTrieError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}
} // namespace

std::size_t MRKeymapTrie::contextIndex(MRKeymapContext context) noexcept {
	return static_cast<std::size_t>(context);
}

bool MRKeymapTrie::rebuild(std::span<const MRKeymapBindingRecord> bindings, std::string *errorMessage) {
	nodes.clear();
	nodes.emplace_back();
	roots.assign(kContextCount, 0);

	for (const MRKeymapBindingRecord &binding : bindings) {
		if (binding.context == MRKeymapContext::None) return setTrieError(errorMessage, "Keymap trie build failed: binding without context.");
		if (binding.sequence.empty()) return setTrieError(errorMessage, "Keymap trie build failed: binding without sequence.");
		if (binding.target.target.empty()) return setTrieError(errorMessage, "Keymap trie build failed: binding without target.");

		std::size_t nodeIndex = roots[contextIndex(binding.context)];
		if (nodeIndex == 0) {
			nodeIndex = nodes.size();
			nodes.emplace_back();
			roots[contextIndex(binding.context)] = nodeIndex;
		}

		for (const MRKeymapToken &token : binding.sequence.tokens()) {
			auto &edges = nodes[nodeIndex].edges;
			const auto it = std::ranges::find(edges, token, &Edge::token);
			if (it != edges.end()) {
				nodeIndex = it->nextIndex;
				continue;
			}
			const std::size_t nextIndex = nodes.size();
			edges.push_back(Edge{token, nextIndex});
			nodes.emplace_back();
			nodeIndex = nextIndex;
		}

		Node &node = nodes[nodeIndex];
		if (node.terminal) return setTrieError(errorMessage, "Keymap trie build failed: duplicate sequence '" + binding.sequence.toString() + "'.");
		if (!node.edges.empty()) return setTrieError(errorMessage, "Keymap trie build failed: terminal/prefix collision at '" + binding.sequence.toString() + "'.");

		for (std::size_t i = 1; i < binding.sequence.size(); ++i) {
			std::vector<MRKeymapToken> prefixTokens(binding.sequence.tokens().begin(), binding.sequence.tokens().begin() + static_cast<std::ptrdiff_t>(i));
			Decision prefixDecision = decide(binding.context, prefixTokens);
			if (prefixDecision.kind == DecisionKind::Matched) return setTrieError(errorMessage, "Keymap trie build failed: prefix/terminal collision at '" + binding.sequence.toString() + "'.");
		}

		node.terminal = true;
		node.target = binding.target;
		node.description = binding.description;
	}

	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRKeymapTrie::Decision MRKeymapTrie::decide(MRKeymapContext context, std::span<const MRKeymapToken> sequence) const {
	Decision decision;

	if (context == MRKeymapContext::None || sequence.empty()) return decision;
	const std::size_t rootIndex = roots[contextIndex(context)];
	if (rootIndex == 0) return decision;

	std::size_t nodeIndex = rootIndex;
	for (const MRKeymapToken &token : sequence) {
		const auto &edges = nodes[nodeIndex].edges;
		const auto it = std::ranges::find(edges, token, &Edge::token);
		if (it == edges.end()) return decision;
		nodeIndex = it->nextIndex;
	}

	const Node &node = nodes[nodeIndex];
	if (node.terminal) {
		decision.kind = DecisionKind::Matched;
		decision.target = node.target;
		decision.description = node.description;
		return decision;
	}
	if (!node.edges.empty()) decision.kind = DecisionKind::Pending;
	return decision;
}
