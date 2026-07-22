#include "MRSyntaxDerivedState.hpp"

MRSyntaxDerivedState::MRSyntaxDerivedState() noexcept
    : MRDerivedStateBase(), mTokenCache(), mCheckpoints(), mWarmedLineRangesDocumentId(0), mWarmedLineRangesLanguage(MRSyntaxLanguage::PlainText) {
}

void MRSyntaxDerivedState::resetState(bool clearCache) noexcept {
	if (clearCache) {
		mTokenCache.clear();
		mCheckpoints.clear();
		clearWarmedLineRanges();
		clearAllRanges();
	}
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
