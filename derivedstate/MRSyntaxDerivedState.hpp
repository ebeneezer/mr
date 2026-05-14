#ifndef MRSYNTAXDERIVEDSTATE_HPP
#define MRSYNTAXDERIVEDSTATE_HPP

#include "MRDerivedStateBase.hpp"

#include "../ui/MRSyntax.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
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
	struct WarmupState {
		std::uint64_t taskId = 0;
		std::size_t documentId = 0;
		std::size_t version = 0;
		std::size_t topLine = 0;
		std::size_t bottomLine = 0;
		MRSyntaxLanguage language = MRSyntaxLanguage::PlainText;
	};

	struct PrefetchState {
		std::size_t documentId = 0;
		std::size_t version = 0;
		std::size_t targetBottomLine = 0;
		std::size_t reachedBottomLine = 0;
		MRSyntaxLanguage language = MRSyntaxLanguage::PlainText;
	};

	MRSyntaxDerivedState() noexcept;

	void resetState(bool clearCache) noexcept;

	std::map<std::size_t, MRSyntaxCacheEntry> &tokenCache() noexcept;
	const std::map<std::size_t, MRSyntaxCacheEntry> &tokenCache() const noexcept;

	std::map<std::size_t, MRSyntaxCheckpointEntry> &checkpoints() noexcept;
	const std::map<std::size_t, MRSyntaxCheckpointEntry> &checkpoints() const noexcept;

	WarmupState &warmupState() noexcept;
	const WarmupState &warmupState() const noexcept;

	PrefetchState &prefetchState() noexcept;
	const PrefetchState &prefetchState() const noexcept;

	void clearScheduledRequest() noexcept;
	void rememberScheduledRequest(std::size_t topLine, std::size_t bottomLine, std::chrono::steady_clock::time_point scheduledAt) noexcept;
	std::size_t lastScheduledTopLine() const noexcept;
	std::size_t lastScheduledBottomLine() const noexcept;
	std::chrono::steady_clock::time_point lastScheduledAt() const noexcept;

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
	WarmupState mWarmup;
	PrefetchState mPrefetch;
	std::size_t mLastScheduledTopLine;
	std::size_t mLastScheduledBottomLine;
	std::chrono::steady_clock::time_point mLastScheduledAt;
	std::size_t mWarmedLineRangesDocumentId;
	MRSyntaxLanguage mWarmedLineRangesLanguage;
};

#endif
