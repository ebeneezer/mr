#include "MRKeymapResolver.hpp"

#include "../config/MRDialogPaths.hpp"

#include <array>
#include <algorithm>

namespace {
MRKeymapResolver g_runtimeKeymapResolver;

const MRKeymapProfile *findActiveProfile(std::span<const MRKeymapProfile> profiles, std::string_view activeProfileName) noexcept {
	const auto activeIt = std::ranges::find(profiles, activeProfileName, &MRKeymapProfile::name);
	if (activeIt != profiles.end()) return &*activeIt;
	const auto defaultIt = std::ranges::find(profiles, std::string_view("DEFAULT"), &MRKeymapProfile::name);
	if (defaultIt != profiles.end()) return &*defaultIt;
	return profiles.empty() ? nullptr : &profiles.front();
}
} // namespace

bool MRKeymapResolver::isAbortToken(const MRKeymapToken &token) noexcept {
	return token.baseKey() == MRKeymapBaseKey::Esc && token.modifiers() == 0;
}

std::size_t MRKeymapResolver::contextIndex(MRKeymapContext context) noexcept {
	return static_cast<std::size_t>(context);
}

std::string MRKeymapResolver::sequenceText(std::span<const MRKeymapToken> tokens) {
	std::string text;

	for (const MRKeymapToken &token : tokens)
		text += token.toString();
	return text;
}

bool MRKeymapResolver::rebuild(std::span<const MRKeymapProfile> profiles, std::string_view activeProfileName, std::string *errorMessage) {
	const MRKeymapProfile *activeProfile = findActiveProfile(profiles, activeProfileName);
	const std::span<const MRKeymapBindingRecord> bindings = activeProfile != nullptr ? std::span<const MRKeymapBindingRecord>(activeProfile->bindings) : std::span<const MRKeymapBindingRecord>();

	resetPending();
	return trie.rebuild(bindings, errorMessage);
}

MRKeymapResolver::Result MRKeymapResolver::resolve(MRKeymapContext context, const MRKeymapToken &token) {
	Result result;
	PendingState *state = nullptr;

	result.context = context;
	if (context == MRKeymapContext::None) return result;
	state = &pendingStates[contextIndex(context)];
	if (!state->tokens.empty() && isAbortToken(token)) {
		result.kind = ResultKind::Aborted;
		result.sequenceText = sequenceText(state->tokens);
		state->tokens.clear();
		return result;
	}

	state->tokens.push_back(token);
	result.sequenceText = sequenceText(state->tokens);

	const MRKeymapTrie::Decision decision = trie.decide(context, state->tokens);
	switch (decision.kind) {
		case MRKeymapTrie::DecisionKind::Matched:
			result.kind = ResultKind::Matched;
			result.target = decision.target;
			result.description = decision.description;
			state->tokens.clear();
			return result;
		case MRKeymapTrie::DecisionKind::Pending:
			result.kind = ResultKind::Pending;
			return result;
		case MRKeymapTrie::DecisionKind::NoMatch:
			if (state->tokens.size() == 1) {
				state->tokens.clear();
				result.kind = ResultKind::NoMatch;
				return result;
			}
			state->tokens.clear();
			result.kind = ResultKind::Invalid;
			return result;
	}
	state->tokens.clear();
	return result;
}

bool MRKeymapResolver::hasPending(MRKeymapContext context) const noexcept {
	if (context == MRKeymapContext::None) return false;
	return !pendingStates[contextIndex(context)].tokens.empty();
}

void MRKeymapResolver::resetPending() noexcept {
	for (PendingState &state : pendingStates)
		state.tokens.clear();
}

MRKeymapResolver &runtimeKeymapResolver() noexcept {
	return g_runtimeKeymapResolver;
}

bool rebuildRuntimeKeymapResolver(std::string *errorMessage) {
	return g_runtimeKeymapResolver.rebuild(configuredKeymapProfiles(), configuredActiveKeymapProfile(), errorMessage);
}
