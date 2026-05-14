#include "MRDerivedStateBase.hpp"

#include <algorithm>

MRDerivedStateBase::MRDerivedStateBase() noexcept : mDocumentId(0), mVersion(0), mValidRanges(), mInvalidRanges() {
}

MRDerivedStateBase::~MRDerivedStateBase() = default;

void MRDerivedStateBase::resetBaseState() noexcept {
	mDocumentId = 0;
	mVersion = 0;
	mValidRanges.clear();
	mInvalidRanges.clear();
}

void MRDerivedStateBase::setBaseDocument(std::size_t documentId, std::size_t version) noexcept {
	mDocumentId = documentId;
	mVersion = version;
}

std::size_t MRDerivedStateBase::baseDocumentId() const noexcept {
	return mDocumentId;
}

std::size_t MRDerivedStateBase::baseVersion() const noexcept {
	return mVersion;
}

void MRDerivedStateBase::clearValidRanges() noexcept {
	mValidRanges.clear();
}

void MRDerivedStateBase::clearInvalidRanges() noexcept {
	mInvalidRanges.clear();
}

void MRDerivedStateBase::clearAllRanges() noexcept {
	mValidRanges.clear();
	mInvalidRanges.clear();
}

void MRDerivedStateBase::rememberValidRange(std::size_t startLine, std::size_t endLine) noexcept {
	if (endLine <= startLine) return;
	mValidRanges.push_back(std::make_pair(startLine, endLine));
	normalizeRanges(mValidRanges);
}

void MRDerivedStateBase::rememberInvalidRange(std::size_t startLine, std::size_t endLine) noexcept {
	if (endLine <= startLine) return;
	mInvalidRanges.push_back(std::make_pair(startLine, endLine));
	normalizeRanges(mInvalidRanges);
}

void MRDerivedStateBase::invalidateValidRangesFrom(std::size_t lineIndex) noexcept {
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
	rememberInvalidRange(lineIndex, std::numeric_limits<std::size_t>::max());
}

bool MRDerivedStateBase::validRangeCovered(std::size_t startLine, std::size_t endLine) const noexcept {
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

const std::vector<MRDerivedStateBase::LineRange> &MRDerivedStateBase::validRanges() const noexcept {
	return mValidRanges;
}

const std::vector<MRDerivedStateBase::LineRange> &MRDerivedStateBase::invalidRanges() const noexcept {
	return mInvalidRanges;
}

void MRDerivedStateBase::normalizeRanges(std::vector<LineRange> &ranges) {
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
