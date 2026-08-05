#include "MRKeymapResolver.hpp"

#include "../config/settings/MRSettingsRuntime.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../mrmac/vm/MRVMRuntimeState.hpp"
#include "../mrmac/vm/MRVMValue.hpp"

#include <algorithm>
#include <mutex>

namespace {
MRKeymapResolver g_runtimeKeymapResolver;

using Value = VirtualMachine::Value;

Value pendingKeymapRoot(MRVMRuntimeKv &runtimeKv) {
	Value keymap = runtimeKv.ensureRoot("KEYMAP");
	Value runtime = runtimeKv.ensureChild(keymap, "runtime");
	return runtimeKv.ensureChild(runtime, "pending");
}

std::vector<MRKeymapToken> pendingTokens(MRKeymapContext context) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	Value pending;
	std::vector<MRKeymapToken> tokens;
	const std::string key = std::to_string(static_cast<std::size_t>(context));

	if (!runtimeKv.findRoot("KEYMAP", pending) || !runtimeKv.findChild(pending, "runtime", pending) || !runtimeKv.findChild(pending, "pending", pending) || !mrvmHashContainsValue(store, store, pending, key)) return tokens;
	const Value stored = mrvmHashReadValue(store, store, pending, key);
	if (stored.type != TYPE_STR_ARRAY) return tokens;
	tokens.reserve(stored.arrayValues.size());
	for (const Value &value : stored.arrayValues) {
		if (value.type != TYPE_STR) continue;
		const std::optional<MRKeymapToken> token = MRKeymapToken::parse(value.s);
		if (token) tokens.push_back(*token);
	}
	return tokens;
}

void storePendingTokens(MRKeymapContext context, const std::vector<MRKeymapToken> &tokens) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	const Value pending = pendingKeymapRoot(runtimeKv);
	const std::string key = std::to_string(static_cast<std::size_t>(context));

	if (tokens.empty()) {
		if (mrvmHashContainsValue(store, store, pending, key)) mrvmHashEraseValue(store, store, pending, key);
		return;
	}
	Value stored = mrvmMakeArrayValue(TYPE_STR);
	stored.globalStorage = true;
	stored.arrayValues.reserve(tokens.size());
	for (const MRKeymapToken &token : tokens)
		stored.arrayValues.push_back(mrvmMakeString(token.toString()));
	mrvmHashWriteValue(store, store, pending, key, stored);
}

const MRKeymapProfile *findActiveProfile(std::span<const MRKeymapProfile> profiles, std::string_view activeProfileName) noexcept {
	for (const MRKeymapProfile &profile : profiles)
		if (profile.name == activeProfileName) return &profile;
	return nullptr;
}
} // namespace

bool MRKeymapResolver::isAbortToken(const MRKeymapToken &token) noexcept {
	return token.baseKey() == MRKeymapBaseKey::Esc && token.modifiers() == 0;
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
	std::vector<MRKeymapToken> tokens;

	result.context = context;
	if (context == MRKeymapContext::None) return result;
	tokens = pendingTokens(context);
	if (!tokens.empty() && isAbortToken(token)) {
		result.kind = ResultKind::Aborted;
		result.sequenceText = sequenceText(tokens);
		storePendingTokens(context, std::vector<MRKeymapToken>());
		return result;
	}

	tokens.push_back(token);
	result.sequenceText = sequenceText(tokens);

	const MRKeymapTrie::Decision decision = trie.decide(context, tokens);
	switch (decision.kind) {
		case MRKeymapTrie::DecisionKind::Matched:
			result.kind = ResultKind::Matched;
			result.target = decision.target;
			result.description = decision.description;
			storePendingTokens(context, std::vector<MRKeymapToken>());
			return result;
		case MRKeymapTrie::DecisionKind::Pending:
			result.kind = ResultKind::Pending;
			storePendingTokens(context, tokens);
			return result;
		case MRKeymapTrie::DecisionKind::NoMatch:
			if (tokens.size() == 1) {
				storePendingTokens(context, std::vector<MRKeymapToken>());
				result.kind = ResultKind::NoMatch;
				return result;
			}
			storePendingTokens(context, std::vector<MRKeymapToken>());
			result.kind = ResultKind::Invalid;
			return result;
	}
	storePendingTokens(context, std::vector<MRKeymapToken>());
	return result;
}

bool MRKeymapResolver::hasPending(MRKeymapContext context) const {
	if (context == MRKeymapContext::None) return false;
	return !pendingTokens(context).empty();
}

void MRKeymapResolver::resetPending() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value keymap;
	Value runtime;

	if (!runtimeKv.findRoot("KEYMAP", keymap) || !runtimeKv.findChild(keymap, "runtime", runtime)) return;
	static_cast<void>(runtimeKv.eraseChild(runtime, "pending"));
}

MRKeymapResolver &runtimeKeymapResolver() noexcept {
	return g_runtimeKeymapResolver;
}
