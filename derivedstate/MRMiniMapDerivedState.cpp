#include "MRMiniMapDerivedState.hpp"

MRMiniMapDerivedState::MRMiniMapDerivedState() noexcept : mRenderer(), mInitialRenderReportedDocumentId(0), mOverlaySources() {
}

MRMiniMapRenderer &MRMiniMapDerivedState::renderer() noexcept {
	return mRenderer;
}

const MRMiniMapRenderer &MRMiniMapDerivedState::renderer() const noexcept {
	return mRenderer;
}

bool MRMiniMapDerivedState::shouldReportInitialRender(std::size_t documentId) const noexcept {
	return mInitialRenderReportedDocumentId != documentId;
}

void MRMiniMapDerivedState::markInitialRenderReported(std::size_t documentId) noexcept {
	mInitialRenderReportedDocumentId = documentId;
}

const MRMiniMapRenderer::OverlaySources &MRMiniMapDerivedState::overlaySources() const noexcept {
	return mOverlaySources;
}

void MRMiniMapDerivedState::setFindRanges(const std::vector<mr::editor::Range> &ranges) {
	mOverlaySources.findRanges = std::make_shared<const std::vector<mr::editor::Range>>(ranges);
	++mOverlaySources.revision;
}

void MRMiniMapDerivedState::setDirtyRanges(const std::vector<mr::editor::Range> &ranges) {
	mOverlaySources.dirtyRanges = std::make_shared<const std::vector<mr::editor::Range>>(ranges);
	++mOverlaySources.revision;
}

void MRMiniMapDerivedState::adoptCompilerRanges(const std::shared_ptr<const std::vector<mr::editor::Range>> &errorRanges,
	                                             const std::shared_ptr<const std::vector<mr::editor::Range>> &warningRanges) {
	mOverlaySources.errorRanges = errorRanges != nullptr ? errorRanges : std::make_shared<const std::vector<mr::editor::Range>>();
	mOverlaySources.warningRanges = warningRanges != nullptr ? warningRanges : std::make_shared<const std::vector<mr::editor::Range>>();
	++mOverlaySources.revision;
}

void MRMiniMapDerivedState::adoptFileCompareRanges(const std::shared_ptr<const std::vector<unsigned char>> &lineKinds,
	                                               const std::shared_ptr<const std::vector<MRFileCompareMiniMapSlice>> &slices) {
	mOverlaySources.fileCompareLineKinds = lineKinds != nullptr ? lineKinds : std::make_shared<const std::vector<unsigned char>>();
	mOverlaySources.fileCompareMiniMapSlices = slices != nullptr ? slices : std::make_shared<const std::vector<MRFileCompareMiniMapSlice>>();
	++mOverlaySources.revision;
}
