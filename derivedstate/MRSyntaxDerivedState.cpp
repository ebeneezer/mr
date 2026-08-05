#include "MRSyntaxDerivedState.hpp"

#include <algorithm>

MRSyntaxDerivedState::MRSyntaxDerivedState() noexcept
    : mTokenCache(), mCheckpoints(), mValidRanges(), mWarmedLineRangesDocumentId(0), mWarmedLineRangesLanguage(MRSyntaxLanguage::PlainText) {
}

void MRSyntaxDerivedState::resetState(bool clearCache) noexcept {
	if (clearCache) {
		mTokenCache.clear();
		mCheckpoints.clear();
		clearWarmedLineRanges();
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
	mValidRanges.clear();
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
	mValidRanges.clear();
}

const std::vector<MRSyntaxDerivedState::LineRange> &MRSyntaxDerivedState::validRanges() const noexcept {
	return mValidRanges;
}

void MRSyntaxDerivedState::rememberValidRange(std::size_t startLine, std::size_t endLine) noexcept {
	if (endLine <= startLine) return;
	mValidRanges.push_back(std::make_pair(startLine, endLine));
	normalizeRanges(mValidRanges);
}

void MRSyntaxDerivedState::invalidateValidRangesFrom(std::size_t lineIndex) noexcept {
	std::vector<LineRange> kept;

	kept.reserve(mValidRanges.size());
	for (const LineRange &range : mValidRanges) {
		if (range.second <= lineIndex) {
			kept.push_back(range);
			continue;
		}
		if (range.first < lineIndex) kept.push_back(std::make_pair(range.first, lineIndex));
	}
	mValidRanges.swap(kept);
}

bool MRSyntaxDerivedState::validRangeCovered(std::size_t startLine, std::size_t endLine) const noexcept {
	if (endLine <= startLine) return true;
	std::size_t coveredUntil = startLine;

	for (const LineRange &range : mValidRanges) {
		if (range.second <= coveredUntil) continue;
		if (range.first > coveredUntil) return false;
		coveredUntil = std::max(coveredUntil, range.second);
		if (coveredUntil >= endLine) return true;
	}
	return coveredUntil >= endLine;
}

void MRSyntaxDerivedState::normalizeRanges(std::vector<LineRange> &ranges) {
	std::sort(ranges.begin(), ranges.end(), [](const LineRange &a, const LineRange &b) { return a.first < b.first || (a.first == b.first && a.second < b.second); });
	std::vector<LineRange> merged;
	for (const LineRange &item : ranges) {
		if (item.second <= item.first) continue;
		if (merged.empty() || item.first > merged.back().second) merged.push_back(item);
		else if (item.second > merged.back().second)
			merged.back().second = item.second;
	}
	ranges.swap(merged);
}
