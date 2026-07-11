#include "MRFoldingDerivedState.hpp"

#include <algorithm>

MRFoldingDerivedState::MRFoldingDerivedState() noexcept : MRDerivedStateBase(), mWarmup(), mVisible(), mClosedFoldSpans(), mEffectiveClosedFoldSpans() {
}

MRFoldingDerivedState::WarmupState &MRFoldingDerivedState::warmupState() noexcept {
	return mWarmup;
}

const MRFoldingDerivedState::WarmupState &MRFoldingDerivedState::warmupState() const noexcept {
	return mWarmup;
}

MRFoldingDerivedState::VisibleState &MRFoldingDerivedState::visibleState() noexcept {
	return mVisible;
}

const MRFoldingDerivedState::VisibleState &MRFoldingDerivedState::visibleState() const noexcept {
	return mVisible;
}

std::map<std::size_t, MRFoldSpan> &MRFoldingDerivedState::closedFoldSpans() noexcept {
	return mClosedFoldSpans;
}

const std::map<std::size_t, MRFoldSpan> &MRFoldingDerivedState::closedFoldSpans() const noexcept {
	return mClosedFoldSpans;
}

std::vector<MRFoldSpan> &MRFoldingDerivedState::effectiveClosedFoldSpans() noexcept {
	return mEffectiveClosedFoldSpans;
}

const std::vector<MRFoldSpan> &MRFoldingDerivedState::effectiveClosedFoldSpans() const noexcept {
	return mEffectiveClosedFoldSpans;
}

void MRFoldingDerivedState::clearWarmupState() noexcept {
	mWarmup = WarmupState();
}

void MRFoldingDerivedState::clearVisibleState(bool preserveProjection) noexcept {
	if (!preserveProjection) {
		mVisible.spans.clear();
		mVisible.branches.clear();
		mVisible.displayLevels.clear();
		mVisible.lineTexts.clear();
		mVisible.topLine = 0;
		mVisible.bottomLine = 0;
		mVisible.language = MRSyntaxLanguage::PlainText;
		mVisible.gutterColumns = 1;
	} else
		mVisible.lineTexts.clear();
	mVisible.documentId = 0;
	mVisible.version = 0;
}

void MRFoldingDerivedState::clearClosedFolds() noexcept {
	mClosedFoldSpans.clear();
	mEffectiveClosedFoldSpans.clear();
}

void MRFoldingDerivedState::rebuildEffectiveClosedFolds() noexcept {
	mEffectiveClosedFoldSpans.clear();
	std::size_t coveredUntil = 0;
	bool haveCovered = false;

	for (const std::pair<const std::size_t, MRFoldSpan> &entry : mClosedFoldSpans) {
		const MRFoldSpan &closedSpan = entry.second;
		if (haveCovered && closedSpan.startLine <= coveredUntil) continue;
		mEffectiveClosedFoldSpans.push_back(closedSpan);
		coveredUntil = closedSpan.endLine;
		haveCovered = true;
	}
}

int MRFoldingDerivedState::visibleGutterColumns() const noexcept {
	return std::max(1, mVisible.gutterColumns);
}
