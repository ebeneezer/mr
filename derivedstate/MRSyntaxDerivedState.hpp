#ifndef MRSYNTAXDERIVEDSTATE_HPP
#define MRSYNTAXDERIVEDSTATE_HPP

#include "MRDerivedStateBase.hpp"

#include "../ui/MRSyntax.hpp"

#include <cstddef>
#include <map>
#include <utility>
#include <vector>

struct MRSyntaxCacheEntry {
	MRSyntaxLineState stateIn;
	MRSyntaxLineResult syntaxLine;

	MRSyntaxCacheEntry() noexcept : stateIn(), syntaxLine() {
	}

	MRSyntaxCacheEntry(MRSyntaxLineState aStateIn, MRSyntaxLineResult aSyntaxLine) : stateIn(aStateIn), syntaxLine(std::move(aSyntaxLine)) {
	}
};

struct MRSyntaxCheckpointEntry {
	std::size_t lineStart;
	std::size_t lineIndex;
	MRSyntaxLineState stateIn;

	MRSyntaxCheckpointEntry() noexcept : lineStart(0), lineIndex(0), stateIn() {
	}

	MRSyntaxCheckpointEntry(std::size_t aLineStart, std::size_t aLineIndex, MRSyntaxLineState aStateIn) noexcept : lineStart(aLineStart), lineIndex(aLineIndex), stateIn(aStateIn) {
	}
};

class MRSyntaxDerivedState : public MRDerivedStateBase {
  public:
	MRSyntaxDerivedState() noexcept;

	void resetState(bool clearCache) noexcept;

	std::map<std::size_t, MRSyntaxCacheEntry> &tokenCache() noexcept;
	const std::map<std::size_t, MRSyntaxCacheEntry> &tokenCache() const noexcept;

	std::map<std::size_t, MRSyntaxCheckpointEntry> &checkpoints() noexcept;
	const std::map<std::size_t, MRSyntaxCheckpointEntry> &checkpoints() const noexcept;

	void clearWarmedLineRanges() noexcept;
	void rememberWarmedLineRange(std::size_t documentId, MRSyntaxLanguage language, std::size_t startLine, std::size_t endLine) noexcept;
	void invalidateWarmedLineRangesFrom(std::size_t documentId, MRSyntaxLanguage language, std::size_t lineIndex) noexcept;
	bool warmedLineRangeCovered(std::size_t documentId, MRSyntaxLanguage language, std::size_t startLine, std::size_t endLine) const noexcept;
	bool warmedLineRangesMatch(std::size_t documentId, MRSyntaxLanguage language) const noexcept;
	void ensureWarmedLineRangeOwner(std::size_t documentId, MRSyntaxLanguage language) noexcept;

	std::size_t warmedLineRangesDocumentId() const noexcept;
	MRSyntaxLanguage warmedLineRangesLanguage() const noexcept;

  private:
	std::map<std::size_t, MRSyntaxCacheEntry> mTokenCache;
	std::map<std::size_t, MRSyntaxCheckpointEntry> mCheckpoints;
	std::size_t mWarmedLineRangesDocumentId;
	MRSyntaxLanguage mWarmedLineRangesLanguage;
};

#endif
