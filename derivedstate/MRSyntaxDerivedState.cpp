#include "MRSyntaxDerivedState.hpp"

MRSyntaxDerivedState::MRSyntaxDerivedState() noexcept
    : MRDerivedStateBase(), mTokenCache(), mCheckpoints(), mWarmup(), mPrefetch(), mLastScheduledTopLine(0), mLastScheduledBottomLine(0), mLastScheduledAt(),
      mWarmedLineRangesDocumentId(0), mWarmedLineRangesLanguage(MRSyntaxLanguage::PlainText) {
}

void MRSyntaxDerivedState::resetState(bool clearCache) noexcept {
	if (clearCache) {
		mTokenCache.clear();
		mCheckpoints.clear();
		clearWarmedLineRanges();
		clearAllRanges();
	}
	mWarmup = WarmupState();
	mPrefetch = PrefetchState();
	clearScheduledRequest();
}

std::map<std::size_t, MRSyntaxCacheEntry> &MRSyntaxDerivedState::tokenCache() noexcept {
	return mTokenCache;
}

const std::map<std::size_t, MRSyntaxCacheEntry> &MRSyntaxDerivedState::tokenCache() const noexcept {
	return mTokenCache;
}

std::map<std::size_t, MRSyntaxCheckpointEntry> &MRSyntaxDerivedState::checkpoints() noexcept {
	return mCheckpoints;
}

const std::map<std::size_t, MRSyntaxCheckpointEntry> &MRSyntaxDerivedState::checkpoints() const noexcept {
	return mCheckpoints;
}

MRSyntaxDerivedState::WarmupState &MRSyntaxDerivedState::warmupState() noexcept {
	return mWarmup;
}

const MRSyntaxDerivedState::WarmupState &MRSyntaxDerivedState::warmupState() const noexcept {
	return mWarmup;
}

MRSyntaxDerivedState::PrefetchState &MRSyntaxDerivedState::prefetchState() noexcept {
	return mPrefetch;
}

const MRSyntaxDerivedState::PrefetchState &MRSyntaxDerivedState::prefetchState() const noexcept {
	return mPrefetch;
}

void MRSyntaxDerivedState::clearScheduledRequest() noexcept {
	mLastScheduledTopLine = 0;
	mLastScheduledBottomLine = 0;
	mLastScheduledAt = std::chrono::steady_clock::time_point();
}

void MRSyntaxDerivedState::rememberScheduledRequest(std::size_t topLine, std::size_t bottomLine, std::chrono::steady_clock::time_point scheduledAt) noexcept {
	mLastScheduledTopLine = topLine;
	mLastScheduledBottomLine = bottomLine;
	mLastScheduledAt = scheduledAt;
}

std::size_t MRSyntaxDerivedState::lastScheduledTopLine() const noexcept {
	return mLastScheduledTopLine;
}

std::size_t MRSyntaxDerivedState::lastScheduledBottomLine() const noexcept {
	return mLastScheduledBottomLine;
}

std::chrono::steady_clock::time_point MRSyntaxDerivedState::lastScheduledAt() const noexcept {
	return mLastScheduledAt;
}

void MRSyntaxDerivedState::clearWarmedLineRanges() noexcept {
	mWarmedLineRangesDocumentId = 0;
	mWarmedLineRangesLanguage = MRSyntaxLanguage::PlainText;
	clearValidRanges();
}

void MRSyntaxDerivedState::rememberWarmedLineRange(std::size_t documentId, MRSyntaxLanguage language, std::size_t startLine, std::size_t endLine) noexcept {
	if (endLine <= startLine) return;
	ensureWarmedLineRangeOwner(documentId, language);
	rememberValidRange(startLine, endLine);
}

void MRSyntaxDerivedState::invalidateWarmedLineRangesFrom(std::size_t documentId, MRSyntaxLanguage language, std::size_t lineIndex) noexcept {
	if (!warmedLineRangesMatch(documentId, language)) {
		clearWarmedLineRanges();
		return;
	}
	invalidateValidRangesFrom(lineIndex);
}

bool MRSyntaxDerivedState::warmedLineRangeCovered(std::size_t documentId, MRSyntaxLanguage language, std::size_t startLine, std::size_t endLine) const noexcept {
	if (!warmedLineRangesMatch(documentId, language)) return false;
	return validRangeCovered(startLine, endLine);
}

bool MRSyntaxDerivedState::warmedLineRangesMatch(std::size_t documentId, MRSyntaxLanguage language) const noexcept {
	return mWarmedLineRangesDocumentId == documentId && mWarmedLineRangesLanguage == language;
}

void MRSyntaxDerivedState::ensureWarmedLineRangeOwner(std::size_t documentId, MRSyntaxLanguage language) noexcept {
	if (warmedLineRangesMatch(documentId, language)) return;
	mWarmedLineRangesDocumentId = documentId;
	mWarmedLineRangesLanguage = language;
	clearAllRanges();
}

std::size_t MRSyntaxDerivedState::warmedLineRangesDocumentId() const noexcept {
	return mWarmedLineRangesDocumentId;
}

MRSyntaxLanguage MRSyntaxDerivedState::warmedLineRangesLanguage() const noexcept {
	return mWarmedLineRangesLanguage;
}
