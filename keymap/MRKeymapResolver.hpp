#ifndef MRKEYMAPRESOLVER_HPP
#define MRKEYMAPRESOLVER_HPP

#include "MRKeymapTrie.hpp"

#include <string>
#include <string_view>
#include <vector>

class MRKeymapResolver final {
  public:
	enum class ResultKind : unsigned char {
		NoMatch,
		Pending,
		Matched,
		Invalid,
		Aborted
	};

	struct Result {
		ResultKind kind{ResultKind::NoMatch};
		MRKeymapContext context{MRKeymapContext::None};
		MRKeymapBindingTarget target{};
		std::string description;
		std::string sequenceText;
	};

	bool rebuild(std::span<const MRKeymapProfile> profiles, std::string_view activeProfileName, std::string *errorMessage = nullptr);
	[[nodiscard]] Result resolve(MRKeymapContext context, const MRKeymapToken &token);
	[[nodiscard]] bool hasPending(MRKeymapContext context) const;
	void resetPending();

  private:
	[[nodiscard]] static bool isAbortToken(const MRKeymapToken &token) noexcept;
	[[nodiscard]] static std::string sequenceText(std::span<const MRKeymapToken> tokens);

	MRKeymapTrie trie;
};

MRKeymapResolver &runtimeKeymapResolver() noexcept;

#endif
