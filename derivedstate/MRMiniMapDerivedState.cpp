#include "MRMiniMapDerivedState.hpp"

MRMiniMapDerivedState::MRMiniMapDerivedState() noexcept : MRDerivedStateBase(), mRenderer(), mInitialRenderReportedDocumentId(0), mLastEditAt(), mOverlayCache() {
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

std::chrono::steady_clock::time_point MRMiniMapDerivedState::lastEditAt() const noexcept {
	return mLastEditAt;
}

void MRMiniMapDerivedState::setLastEditAt(std::chrono::steady_clock::time_point value) noexcept {
	mLastEditAt = value;
}

void MRMiniMapDerivedState::clearLastEditAt() noexcept {
	mLastEditAt = std::chrono::steady_clock::time_point();
}

MRMiniMapDerivedState::OverlayCache &MRMiniMapDerivedState::overlayCache() noexcept {
	return mOverlayCache;
}

const MRMiniMapDerivedState::OverlayCache &MRMiniMapDerivedState::overlayCache() const noexcept {
	return mOverlayCache;
}

void MRMiniMapDerivedState::clearOverlayCache() noexcept {
	mOverlayCache = OverlayCache();
}
