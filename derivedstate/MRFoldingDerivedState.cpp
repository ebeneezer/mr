#include "MRFoldingDerivedState.hpp"

#include <algorithm>
#include <limits>

MRFoldOpenBlockState::MRFoldOpenBlockState() noexcept
    : startLine(0), indent(0), level(0), sourceKind(MRFoldSourceKind::Generic), closer(0), marker(0), markerLength(0), headingLevel(0), languageBlockKind(0),
      siblingContinuation(false), lastContentLine(std::numeric_limits<std::size_t>::max()), xmlTagName() {
}

bool MRFoldOpenBlockState::operator==(const MRFoldOpenBlockState &other) const noexcept {
	return startLine == other.startLine && indent == other.indent && level == other.level && sourceKind == other.sourceKind && closer == other.closer && marker == other.marker &&
	       markerLength == other.markerLength && headingLevel == other.headingLevel && languageBlockKind == other.languageBlockKind && siblingContinuation == other.siblingContinuation &&
	       lastContentLine == other.lastContentLine && xmlTagName == other.xmlTagName;
}

bool MRFoldAnalysisState::operator==(const MRFoldAnalysisState &other) const noexcept {
	return openBlocks == other.openBlocks && syntaxState == other.syntaxState && previousLineText == other.previousLineText && previousUpperLine == other.previousUpperLine &&
	       previousPreviousLineText == other.previousPreviousLineText && previousPreviousUpperLine == other.previousPreviousUpperLine && recentLineTexts == other.recentLineTexts;
}

bool MRFoldAnalysisState::operator!=(const MRFoldAnalysisState &other) const noexcept {
	return !(*this == other);
}

MRFoldClosedProjection::MRFoldClosedProjection() noexcept : spans(), hiddenLinePrefix() {
}

void MRFoldClosedProjection::finalize() {
	std::sort(spans.begin(), spans.end(), [](const MRFoldSpan &left, const MRFoldSpan &right) {
		if (left.startLine != right.startLine) return left.startLine < right.startLine;
		return left.endLine > right.endLine;
	});
	std::size_t writeIndex = 0;
	for (MRFoldSpan span : spans) {
		if (span.endLine <= span.startLine) continue;
		span.open = false;
		if (writeIndex != 0 && span.startLine <= spans[writeIndex - 1].endLine) continue;
		spans[writeIndex] = span;
		++writeIndex;
	}
	spans.resize(writeIndex);
	hiddenLinePrefix.assign(spans.size() + 1, 0);
	for (std::size_t index = 0; index < spans.size(); ++index)
		hiddenLinePrefix[index + 1] = hiddenLinePrefix[index] + (spans[index].endLine - spans[index].startLine);
}

std::size_t MRFoldClosedProjection::hiddenLineCount() const noexcept {
	return hiddenLinePrefix.empty() ? 0 : hiddenLinePrefix.back();
}

std::size_t MRFoldClosedProjection::hiddenLineCountBefore(std::size_t documentLine) const noexcept {
	const std::vector<MRFoldSpan>::const_iterator firstNotBefore = std::lower_bound(
	    spans.begin(), spans.end(), documentLine, [](const MRFoldSpan &span, std::size_t line) noexcept { return span.endLine < line; });
	const std::size_t count = static_cast<std::size_t>(std::distance(spans.begin(), firstNotBefore));
	return count < hiddenLinePrefix.size() ? hiddenLinePrefix[count] : hiddenLineCount();
}

std::size_t MRFoldClosedProjection::hiddenLineCountInRange(std::size_t startLine, std::size_t endLine) const noexcept {
	if (endLine <= startLine || spans.empty()) return 0;
	const std::vector<MRFoldSpan>::const_iterator first = std::lower_bound(
	    spans.begin(), spans.end(), startLine, [](const MRFoldSpan &span, std::size_t line) noexcept { return span.startLine < line; });
	const std::vector<MRFoldSpan>::const_iterator last = std::upper_bound(
	    first, spans.end(), endLine, [](std::size_t line, const MRFoldSpan &span) noexcept { return line < span.endLine; });
	const std::size_t firstIndex = static_cast<std::size_t>(std::distance(spans.begin(), first));
	const std::size_t lastIndex = static_cast<std::size_t>(std::distance(spans.begin(), last));
	if (lastIndex <= firstIndex || lastIndex >= hiddenLinePrefix.size()) return 0;
	return hiddenLinePrefix[lastIndex] - hiddenLinePrefix[firstIndex];
}

const MRFoldSpan *MRFoldClosedProjection::spanStartingAt(std::size_t documentLine) const noexcept {
	const std::vector<MRFoldSpan>::const_iterator found = std::lower_bound(
	    spans.begin(), spans.end(), documentLine, [](const MRFoldSpan &span, std::size_t line) noexcept { return span.startLine < line; });
	return found != spans.end() && found->startLine == documentLine ? &*found : nullptr;
}

const MRFoldSpan *MRFoldClosedProjection::spanEndingAt(std::size_t documentLine) const noexcept {
	const std::vector<MRFoldSpan>::const_iterator found = std::lower_bound(
	    spans.begin(), spans.end(), documentLine, [](const MRFoldSpan &span, std::size_t line) noexcept { return span.endLine < line; });
	return found != spans.end() && found->endLine == documentLine ? &*found : nullptr;
}

const MRFoldSpan *MRFoldClosedProjection::spanContaining(std::size_t documentLine) const noexcept {
	const std::vector<MRFoldSpan>::const_iterator after = std::upper_bound(
	    spans.begin(), spans.end(), documentLine, [](std::size_t line, const MRFoldSpan &span) noexcept { return line < span.startLine; });
	if (after == spans.begin()) return nullptr;
	const MRFoldSpan &candidate = *(after - 1);
	return documentLine >= candidate.startLine && documentLine <= candidate.endLine ? &candidate : nullptr;
}

MRFoldingDerivedState::MRFoldingDerivedState() noexcept
		: mVisible(), mClosedFoldSpans(), mEffectiveClosedFoldSpans(), mDocumentFoldLevelActive(false), mDocumentFoldLevel(0),
		  mDocumentFoldCanonicalEndLine(0), mDocumentFoldCanonicalProjections(), mDocumentFoldCanonicalLastStartLines(), mDocumentFoldCanonicalLastEndLines(),
		  mDocumentFoldCanonicalHiddenPrefix(), mDocumentFoldViewportProjection(), mDocumentFoldDescendantProjection(), mDocumentFoldOpenSpans() {
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

void MRFoldingDerivedState::clearVisibleState(bool preserveProjection) noexcept {
	++mVisible.revision;
	if (mVisible.revision == 0) ++mVisible.revision;
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
	clearDocumentFoldLevel();
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

void MRFoldingDerivedState::beginDocumentFoldLevel(unsigned short level, std::shared_ptr<const MRFoldClosedProjection> preview) {
	mDocumentFoldLevelActive = true;
	mDocumentFoldLevel = level;
	mDocumentFoldCanonicalEndLine = 0;
	mDocumentFoldCanonicalProjections.clear();
	mDocumentFoldCanonicalLastStartLines.clear();
	mDocumentFoldCanonicalLastEndLines.clear();
	mDocumentFoldCanonicalHiddenPrefix.clear();
	mDocumentFoldCanonicalHiddenPrefix.push_back(0);
	mDocumentFoldViewportProjection = std::move(preview);
	mDocumentFoldOpenSpans.clear();
	static_cast<void>(refreshDocumentFoldLevelViewportProjection());
}

void MRFoldingDerivedState::adoptDocumentFoldLevelProjection(unsigned short level, std::size_t canonicalEndLine, bool complete,
                                                             std::shared_ptr<const MRFoldClosedProjection> projection) {
	if (!mDocumentFoldLevelActive || mDocumentFoldLevel != level) {
		mDocumentFoldCanonicalEndLine = 0;
		mDocumentFoldCanonicalProjections.clear();
		mDocumentFoldCanonicalLastStartLines.clear();
		mDocumentFoldCanonicalLastEndLines.clear();
		mDocumentFoldCanonicalHiddenPrefix.clear();
		mDocumentFoldCanonicalHiddenPrefix.push_back(0);
		mDocumentFoldOpenSpans.clear();
	}
	mDocumentFoldLevelActive = true;
	mDocumentFoldLevel = level;
	if (canonicalEndLine > mDocumentFoldCanonicalEndLine) {
		mDocumentFoldCanonicalEndLine = canonicalEndLine;
		if (projection != nullptr && !projection->spans.empty()) {
			mDocumentFoldCanonicalProjections.push_back(std::move(projection));
			const MRFoldClosedProjection &adopted = *mDocumentFoldCanonicalProjections.back();
			mDocumentFoldCanonicalLastStartLines.push_back(adopted.spans.back().startLine);
			mDocumentFoldCanonicalLastEndLines.push_back(adopted.spans.back().endLine);
			if (mDocumentFoldCanonicalHiddenPrefix.empty()) mDocumentFoldCanonicalHiddenPrefix.push_back(0);
			mDocumentFoldCanonicalHiddenPrefix.push_back(mDocumentFoldCanonicalHiddenPrefix.back() + adopted.hiddenLineCount());
		}
	}
	if (complete) {
		mDocumentFoldViewportProjection.reset();
	} else if (mDocumentFoldViewportProjection != nullptr) {
		std::shared_ptr<MRFoldClosedProjection> remainingPreview = std::make_shared<MRFoldClosedProjection>();
		remainingPreview->spans.reserve(mDocumentFoldViewportProjection->spans.size());
		for (const MRFoldSpan &span : mDocumentFoldViewportProjection->spans)
			if (span.startLine >= canonicalEndLine) remainingPreview->spans.push_back(span);
		remainingPreview->finalize();
		mDocumentFoldViewportProjection = std::move(remainingPreview);
	}
	for (std::vector<MRFoldSpan>::iterator open = mDocumentFoldOpenSpans.begin(); open != mDocumentFoldOpenSpans.end();) {
		const MRFoldSpan *canonical = documentCanonicalProjectionSpanStartingAt(open->startLine);
		const MRFoldSpan *preview = mDocumentFoldViewportProjection != nullptr ? mDocumentFoldViewportProjection->spanStartingAt(open->startLine) : nullptr;
		bool matchingSpan = (canonical != nullptr && canonical->endLine == open->endLine && canonical->sourceKind == open->sourceKind) ||
		                    (preview != nullptr && preview->endLine == open->endLine && preview->sourceKind == open->sourceKind);
		if (!matchingSpan && canonical == nullptr && preview == nullptr)
			for (const MRFoldSpan &visible : mVisible.spans)
				if (visible.startLine == open->startLine && visible.endLine == open->endLine && visible.sourceKind == open->sourceKind) {
					matchingSpan = true;
					break;
				}
		if (matchingSpan) ++open;
		else
			open = mDocumentFoldOpenSpans.erase(open);
	}
	static_cast<void>(refreshDocumentFoldLevelViewportProjection());
}

bool MRFoldingDerivedState::refreshDocumentFoldLevelViewportProjection() {
	if (!mDocumentFoldLevelActive) {
		mDocumentFoldDescendantProjection.reset();
		refreshVisibleFoldOpenStates();
		return false;
	}
	std::shared_ptr<MRFoldClosedProjection> viewportProjection = std::make_shared<MRFoldClosedProjection>();
	viewportProjection->spans.reserve(mVisible.spans.size());
	for (const MRFoldSpan &span : mVisible.spans)
		if (span.level == mDocumentFoldLevel && span.endLine > span.startLine && span.startLine >= mDocumentFoldCanonicalEndLine) viewportProjection->spans.push_back(span);
	viewportProjection->finalize();
	bool viewportChanged = mDocumentFoldViewportProjection == nullptr || mDocumentFoldViewportProjection->spans.size() != viewportProjection->spans.size();
	if (!viewportChanged)
		for (std::size_t index = 0; index < viewportProjection->spans.size(); ++index) {
			const MRFoldSpan &current = mDocumentFoldViewportProjection->spans[index];
			const MRFoldSpan &next = viewportProjection->spans[index];
			if (current.startLine == next.startLine && current.endLine == next.endLine && current.level == next.level && current.sourceKind == next.sourceKind &&
			    current.siblingContinuation == next.siblingContinuation)
				continue;
			viewportChanged = true;
			break;
		}
	if (viewportChanged) mDocumentFoldViewportProjection = std::move(viewportProjection);

	std::shared_ptr<MRFoldClosedProjection> projection = std::make_shared<MRFoldClosedProjection>();
	projection->spans.reserve(mVisible.spans.size());
	for (const MRFoldSpan &span : mVisible.spans) {
		if (span.endLine <= span.startLine || documentProjectionSpanOpen(span.startLine)) continue;
		for (const MRFoldSpan &openParent : mDocumentFoldOpenSpans) {
			if (span.startLine <= openParent.startLine || span.endLine > openParent.endLine) continue;
			projection->spans.push_back(span);
			break;
		}
	}
	projection->finalize();
	bool descendantChanged = mDocumentFoldDescendantProjection == nullptr || mDocumentFoldDescendantProjection->spans.size() != projection->spans.size();
	if (!descendantChanged)
		for (std::size_t index = 0; index < projection->spans.size(); ++index) {
			const MRFoldSpan &current = mDocumentFoldDescendantProjection->spans[index];
			const MRFoldSpan &next = projection->spans[index];
			if (current.startLine == next.startLine && current.endLine == next.endLine && current.level == next.level && current.sourceKind == next.sourceKind &&
			    current.siblingContinuation == next.siblingContinuation)
				continue;
			descendantChanged = true;
			break;
		}
	if (descendantChanged) mDocumentFoldDescendantProjection = std::move(projection);
	refreshVisibleFoldOpenStates();
	return viewportChanged || descendantChanged;
}

void MRFoldingDerivedState::clearDocumentFoldLevel() noexcept {
	mDocumentFoldLevelActive = false;
	mDocumentFoldLevel = 0;
	mDocumentFoldCanonicalEndLine = 0;
	mDocumentFoldCanonicalProjections.clear();
	mDocumentFoldCanonicalLastStartLines.clear();
	mDocumentFoldCanonicalLastEndLines.clear();
	mDocumentFoldCanonicalHiddenPrefix.clear();
	mDocumentFoldViewportProjection.reset();
	mDocumentFoldDescendantProjection.reset();
	mDocumentFoldOpenSpans.clear();
	refreshVisibleFoldOpenStates();
}

bool MRFoldingDerivedState::documentFoldLevelActive() const noexcept {
	return mDocumentFoldLevelActive;
}

unsigned short MRFoldingDerivedState::documentFoldLevel() const noexcept {
	return mDocumentFoldLevel;
}

bool MRFoldingDerivedState::documentFoldLevelContains(std::size_t startLine) const noexcept {
	if (!mDocumentFoldLevelActive) return false;
	if (documentProjectionSpanStartingAt(startLine) != nullptr) return true;
	if (documentCanonicalProjectionSpanStartingAt(startLine) != nullptr) return true;
	if (mDocumentFoldViewportProjection != nullptr && mDocumentFoldViewportProjection->spanStartingAt(startLine) != nullptr) return true;
	return documentProjectionSpanOpen(startLine);
}

bool MRFoldingDerivedState::documentProjectionSpanOpen(std::size_t startLine) const noexcept {
	const std::vector<MRFoldSpan>::const_iterator found = std::lower_bound(
	    mDocumentFoldOpenSpans.begin(), mDocumentFoldOpenSpans.end(), startLine, [](const MRFoldSpan &span, std::size_t line) noexcept { return span.startLine < line; });
	return found != mDocumentFoldOpenSpans.end() && found->startLine == startLine;
}

const MRFoldSpan *MRFoldingDerivedState::documentCanonicalProjectionSpanStartingAt(std::size_t documentLine) const noexcept {
	const std::vector<std::size_t>::const_iterator found = std::lower_bound(mDocumentFoldCanonicalLastStartLines.begin(), mDocumentFoldCanonicalLastStartLines.end(), documentLine);
	if (found == mDocumentFoldCanonicalLastStartLines.end()) return nullptr;
	const std::size_t index = static_cast<std::size_t>(std::distance(mDocumentFoldCanonicalLastStartLines.begin(), found));
	return mDocumentFoldCanonicalProjections[index]->spanStartingAt(documentLine);
}

const MRFoldSpan *MRFoldingDerivedState::documentCanonicalProjectionSpanEndingAt(std::size_t documentLine) const noexcept {
	const std::vector<std::size_t>::const_iterator found = std::lower_bound(mDocumentFoldCanonicalLastEndLines.begin(), mDocumentFoldCanonicalLastEndLines.end(), documentLine);
	if (found == mDocumentFoldCanonicalLastEndLines.end()) return nullptr;
	const std::size_t index = static_cast<std::size_t>(std::distance(mDocumentFoldCanonicalLastEndLines.begin(), found));
	return mDocumentFoldCanonicalProjections[index]->spanEndingAt(documentLine);
}

const MRFoldSpan *MRFoldingDerivedState::documentCanonicalProjectionSpanContaining(std::size_t documentLine) const noexcept {
	const std::vector<std::size_t>::const_iterator found = std::lower_bound(mDocumentFoldCanonicalLastEndLines.begin(), mDocumentFoldCanonicalLastEndLines.end(), documentLine);
	if (found == mDocumentFoldCanonicalLastEndLines.end()) return nullptr;
	const std::size_t index = static_cast<std::size_t>(std::distance(mDocumentFoldCanonicalLastEndLines.begin(), found));
	return mDocumentFoldCanonicalProjections[index]->spanContaining(documentLine);
}

std::size_t MRFoldingDerivedState::documentCanonicalProjectionHiddenBefore(std::size_t documentLine) const noexcept {
	if (mDocumentFoldCanonicalProjections.empty()) return 0;
	const std::vector<std::size_t>::const_iterator found = std::lower_bound(mDocumentFoldCanonicalLastEndLines.begin(), mDocumentFoldCanonicalLastEndLines.end(), documentLine);
	const std::size_t index = static_cast<std::size_t>(std::distance(mDocumentFoldCanonicalLastEndLines.begin(), found));
	if (index >= mDocumentFoldCanonicalProjections.size()) return mDocumentFoldCanonicalHiddenPrefix.back();
	return mDocumentFoldCanonicalHiddenPrefix[index] + mDocumentFoldCanonicalProjections[index]->hiddenLineCountBefore(documentLine);
}

std::size_t MRFoldingDerivedState::documentCanonicalProjectionHiddenInRange(std::size_t startLine, std::size_t endLine) const noexcept {
	if (endLine <= startLine || mDocumentFoldCanonicalProjections.empty()) return 0;
	const std::size_t afterStart = startLine == std::numeric_limits<std::size_t>::max() ? startLine : startLine + 1;
	const std::size_t afterEnd = endLine == std::numeric_limits<std::size_t>::max() ? endLine : endLine + 1;
	const std::size_t beforeStart = documentCanonicalProjectionHiddenBefore(afterStart);
	const std::size_t beforeEnd = documentCanonicalProjectionHiddenBefore(afterEnd);
	std::size_t hidden = beforeEnd >= beforeStart ? beforeEnd - beforeStart : 0;
	const MRFoldSpan *crossingStart = documentCanonicalProjectionSpanContaining(startLine);
	if (crossingStart != nullptr && crossingStart->startLine < startLine && crossingStart->endLine <= endLine) {
		const std::size_t crossingHidden = crossingStart->endLine - crossingStart->startLine;
		hidden -= std::min(hidden, crossingHidden);
	}
	return hidden;
}

std::size_t MRFoldingDerivedState::documentCanonicalProjectionHiddenLineCount() const noexcept {
	return mDocumentFoldCanonicalHiddenPrefix.empty() ? 0 : mDocumentFoldCanonicalHiddenPrefix.back();
}

bool MRFoldingDerivedState::documentFoldLevelCloses(std::size_t startLine) const noexcept {
	return mDocumentFoldLevelActive && documentProjectionSpanStartingAt(startLine) != nullptr;
}

bool MRFoldingDerivedState::toggleDocumentFoldLevelSpan(std::size_t startLine) {
	if (!documentFoldLevelContains(startLine)) return false;
	std::vector<MRFoldSpan>::iterator found = std::lower_bound(mDocumentFoldOpenSpans.begin(), mDocumentFoldOpenSpans.end(), startLine,
	                                                          [](const MRFoldSpan &span, std::size_t line) noexcept { return span.startLine < line; });
	if (found != mDocumentFoldOpenSpans.end() && found->startLine == startLine) {
		const MRFoldSpan closingParent = *found;
		found = mDocumentFoldOpenSpans.erase(found);
		while (found != mDocumentFoldOpenSpans.end() && found->startLine <= closingParent.endLine) {
			if (found->endLine <= closingParent.endLine) found = mDocumentFoldOpenSpans.erase(found);
			else
				++found;
		}
	} else {
		const MRFoldSpan *span = documentProjectionSpanStartingAt(startLine);
		if (span == nullptr)
			for (const MRFoldSpan &visible : mVisible.spans)
				if (visible.startLine == startLine) {
					span = &visible;
					break;
				}
		if (span == nullptr) return false;
		mDocumentFoldOpenSpans.insert(found, *span);
	}
	static_cast<void>(refreshDocumentFoldLevelViewportProjection());
	return true;
}

void MRFoldingDerivedState::refreshVisibleFoldOpenStates() noexcept {
	for (MRFoldSpan &span : mVisible.spans) {
		const bool explicitlyClosed = mClosedFoldSpans.find(span.startLine) != mClosedFoldSpans.end();
		span.open = !explicitlyClosed && !documentFoldLevelCloses(span.startLine);
	}
}

const MRFoldSpan *MRFoldingDerivedState::documentProjectionSpanStartingAt(std::size_t documentLine) const noexcept {
	const MRFoldSpan *base = documentBaseProjectionSpanStartingAt(documentLine);
	const MRFoldSpan *descendant = mDocumentFoldDescendantProjection != nullptr ? mDocumentFoldDescendantProjection->spanStartingAt(documentLine) : nullptr;
	if (base != nullptr && documentProjectionSpanOpen(base->startLine)) base = nullptr;
	if (descendant != nullptr && documentProjectionSpanOpen(descendant->startLine)) descendant = nullptr;
	if (base == nullptr) return descendant;
	if (descendant == nullptr) return base;
	return base->endLine >= descendant->endLine ? base : descendant;
}

const MRFoldSpan *MRFoldingDerivedState::documentProjectionSpanEndingAt(std::size_t documentLine) const noexcept {
	const MRFoldSpan *canonical = documentCanonicalProjectionSpanEndingAt(documentLine);
	const MRFoldSpan *viewport = mDocumentFoldViewportProjection != nullptr ? mDocumentFoldViewportProjection->spanEndingAt(documentLine) : nullptr;
	const MRFoldSpan *descendant = mDocumentFoldDescendantProjection != nullptr ? mDocumentFoldDescendantProjection->spanEndingAt(documentLine) : nullptr;
	const MRFoldSpan *result = nullptr;
	const MRFoldSpan *candidates[] = {canonical, viewport, descendant};
	for (const MRFoldSpan *candidate : candidates) {
		if (candidate == nullptr || documentProjectionSpanOpen(candidate->startLine)) continue;
		if (result == nullptr || candidate->startLine < result->startLine) result = candidate;
	}
	return result;
}

const MRFoldSpan *MRFoldingDerivedState::documentProjectionSpanContaining(std::size_t documentLine) const noexcept {
	const MRFoldSpan *canonical = documentCanonicalProjectionSpanContaining(documentLine);
	const MRFoldSpan *viewport = mDocumentFoldViewportProjection != nullptr ? mDocumentFoldViewportProjection->spanContaining(documentLine) : nullptr;
	const MRFoldSpan *descendant = mDocumentFoldDescendantProjection != nullptr ? mDocumentFoldDescendantProjection->spanContaining(documentLine) : nullptr;
	const MRFoldSpan *result = nullptr;
	const MRFoldSpan *candidates[] = {canonical, viewport, descendant};
	for (const MRFoldSpan *candidate : candidates) {
		if (candidate == nullptr || documentProjectionSpanOpen(candidate->startLine)) continue;
		if (result == nullptr || candidate->startLine < result->startLine || (candidate->startLine == result->startLine && candidate->endLine > result->endLine)) result = candidate;
	}
	return result;
}

const MRFoldSpan *MRFoldingDerivedState::documentBaseProjectionSpanStartingAt(std::size_t documentLine) const noexcept {
	const MRFoldSpan *canonical = documentCanonicalProjectionSpanStartingAt(documentLine);
	const MRFoldSpan *viewport = mDocumentFoldViewportProjection != nullptr ? mDocumentFoldViewportProjection->spanStartingAt(documentLine) : nullptr;
	if (canonical == nullptr) return viewport;
	if (viewport == nullptr) return canonical;
	return canonical->endLine >= viewport->endLine ? canonical : viewport;
}

std::size_t MRFoldingDerivedState::documentProjectionHiddenBefore(std::size_t documentLine) const noexcept {
	if (!mDocumentFoldLevelActive) return 0;
	std::size_t hidden = documentCanonicalProjectionHiddenBefore(documentLine);
	if (mDocumentFoldViewportProjection != nullptr) hidden += mDocumentFoldViewportProjection->hiddenLineCountBefore(documentLine);
	if (mDocumentFoldDescendantProjection != nullptr) hidden += mDocumentFoldDescendantProjection->hiddenLineCountBefore(documentLine);
	for (const MRFoldSpan &open : mDocumentFoldOpenSpans) {
		const MRFoldSpan *base = documentBaseProjectionSpanStartingAt(open.startLine);
		if (base != nullptr && base->endLine == open.endLine && base->endLine < documentLine) hidden -= base->endLine - base->startLine;
	}
	return hidden;
}

std::size_t MRFoldingDerivedState::documentProjectionHiddenInRange(std::size_t startLine, std::size_t endLine) const noexcept {
	if (!mDocumentFoldLevelActive) return 0;
	std::size_t hidden = documentCanonicalProjectionHiddenInRange(startLine, endLine);
	if (mDocumentFoldViewportProjection != nullptr) hidden += mDocumentFoldViewportProjection->hiddenLineCountInRange(startLine, endLine);
	if (mDocumentFoldDescendantProjection != nullptr) hidden += mDocumentFoldDescendantProjection->hiddenLineCountInRange(startLine, endLine);
	for (const MRFoldSpan &open : mDocumentFoldOpenSpans) {
		const MRFoldSpan *base = documentBaseProjectionSpanStartingAt(open.startLine);
		if (base != nullptr && base->endLine == open.endLine && base->startLine >= startLine && base->endLine <= endLine) hidden -= base->endLine - base->startLine;
	}
	return hidden;
}

bool MRFoldingDerivedState::documentProjectionCovers(const MRFoldSpan &span) const noexcept {
	if (!mDocumentFoldLevelActive) return false;
	const MRFoldSpan *cover = documentProjectionSpanContaining(span.startLine);
	return cover != nullptr && cover->endLine >= span.endLine;
}

std::size_t MRFoldingDerivedState::effectiveHiddenBefore(std::size_t documentLine) const noexcept {
	std::size_t hidden = documentProjectionHiddenBefore(documentLine);
	for (const MRFoldSpan &span : mEffectiveClosedFoldSpans) {
		if (span.endLine >= documentLine) break;
		if (documentProjectionCovers(span)) continue;
		const std::size_t spanHidden = span.endLine > span.startLine ? span.endLine - span.startLine : 0;
		const std::size_t projectedInside = documentProjectionHiddenInRange(span.startLine, span.endLine);
		hidden += spanHidden - std::min(spanHidden, projectedInside);
	}
	return hidden;
}

bool MRFoldingDerivedState::hasEffectiveClosedFolds() const noexcept {
	if (!mEffectiveClosedFoldSpans.empty()) return true;
	if (!mDocumentFoldLevelActive) return false;
	std::size_t hidden = documentCanonicalProjectionHiddenLineCount();
	if (mDocumentFoldViewportProjection != nullptr) hidden += mDocumentFoldViewportProjection->hiddenLineCount();
	if (mDocumentFoldDescendantProjection != nullptr) hidden += mDocumentFoldDescendantProjection->hiddenLineCount();
	for (const MRFoldSpan &open : mDocumentFoldOpenSpans) {
		const MRFoldSpan *base = documentBaseProjectionSpanStartingAt(open.startLine);
		if (base != nullptr && base->endLine == open.endLine) hidden -= base->endLine - base->startLine;
	}
	return hidden != 0;
}

std::size_t MRFoldingDerivedState::foldedLineCount(std::size_t totalLines) const noexcept {
	if (totalLines == 0) return 1;
	const std::size_t hidden = effectiveHiddenBefore(totalLines);
	return std::max<std::size_t>(1, totalLines - std::min(totalLines - 1, hidden));
}

std::size_t MRFoldingDerivedState::visibleLineForDocumentLine(std::size_t documentLine) const noexcept {
	const MRFoldSpan *closed = effectiveClosedFoldContaining(documentLine);
	if (closed != nullptr && documentLine > closed->startLine) return closed->startLine - effectiveHiddenBefore(closed->startLine);
	return documentLine - std::min(documentLine, effectiveHiddenBefore(documentLine));
}

std::size_t MRFoldingDerivedState::documentLineForVisibleLine(std::size_t visibleLine, std::size_t totalLines) const noexcept {
	if (!hasEffectiveClosedFolds() || totalLines == 0) return visibleLine;
	std::size_t left = 0;
	std::size_t right = totalLines;
	while (left < right) {
		const std::size_t middle = left + (right - left) / 2;
		if (visibleLineForDocumentLine(middle) < visibleLine) left = middle + 1;
		else
			right = middle;
	}
	return left < totalLines ? left : totalLines - 1;
}

const MRFoldSpan *MRFoldingDerivedState::effectiveClosedFoldStartingAt(std::size_t documentLine) const noexcept {
	const MRFoldSpan *projection = mDocumentFoldLevelActive ? documentProjectionSpanStartingAt(documentLine) : nullptr;
	const std::vector<MRFoldSpan>::const_iterator explicitFold = std::lower_bound(
	    mEffectiveClosedFoldSpans.begin(), mEffectiveClosedFoldSpans.end(), documentLine,
	    [](const MRFoldSpan &span, std::size_t line) noexcept { return span.startLine < line; });
	const MRFoldSpan *explicitSpan = explicitFold != mEffectiveClosedFoldSpans.end() && explicitFold->startLine == documentLine ? &*explicitFold : nullptr;
	if (projection == nullptr) return explicitSpan;
	if (explicitSpan == nullptr) return projection;
	return explicitSpan->endLine > projection->endLine ? explicitSpan : projection;
}

const MRFoldSpan *MRFoldingDerivedState::effectiveClosedFoldEndingAt(std::size_t documentLine) const noexcept {
	const MRFoldSpan *projection = mDocumentFoldLevelActive ? documentProjectionSpanEndingAt(documentLine) : nullptr;
	const std::vector<MRFoldSpan>::const_iterator explicitFold = std::lower_bound(
	    mEffectiveClosedFoldSpans.begin(), mEffectiveClosedFoldSpans.end(), documentLine,
	    [](const MRFoldSpan &span, std::size_t line) noexcept { return span.endLine < line; });
	const MRFoldSpan *explicitSpan = explicitFold != mEffectiveClosedFoldSpans.end() && explicitFold->endLine == documentLine ? &*explicitFold : nullptr;
	if (projection == nullptr) return explicitSpan;
	if (explicitSpan == nullptr) return projection;
	return explicitSpan->startLine < projection->startLine ? explicitSpan : projection;
}

const MRFoldSpan *MRFoldingDerivedState::effectiveClosedFoldContaining(std::size_t documentLine) const noexcept {
	const MRFoldSpan *projection = mDocumentFoldLevelActive ? documentProjectionSpanContaining(documentLine) : nullptr;
	const std::vector<MRFoldSpan>::const_iterator after = std::upper_bound(
	    mEffectiveClosedFoldSpans.begin(), mEffectiveClosedFoldSpans.end(), documentLine,
	    [](std::size_t line, const MRFoldSpan &span) noexcept { return line < span.startLine; });
	const MRFoldSpan *explicitSpan = nullptr;
	if (after != mEffectiveClosedFoldSpans.begin()) {
		const MRFoldSpan &candidate = *(after - 1);
		if (documentLine >= candidate.startLine && documentLine <= candidate.endLine) explicitSpan = &candidate;
	}
	if (projection == nullptr) return explicitSpan;
	if (explicitSpan == nullptr) return projection;
	return explicitSpan->startLine < projection->startLine ? explicitSpan : projection;
}
