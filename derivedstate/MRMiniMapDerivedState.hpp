#ifndef MRMINIMAPDERIVEDSTATE_HPP
#define MRMINIMAPDERIVEDSTATE_HPP

#include "MRDerivedStateBase.hpp"

#include "../ui/MRFileEditor/MRMiniMap.hpp"

#include <chrono>
#include <cstddef>

class MRMiniMapDerivedState : public MRDerivedStateBase {
  public:
	struct OverlayCache {
		std::size_t documentId = 0;
		std::size_t documentVersion = 0;
		std::size_t totalLines = 0;
		int viewportWidth = 0;
		int bodyWidth = 0;
		bool braille = true;
		std::size_t selectionStart = 0;
		std::size_t selectionEnd = 0;
		std::uint64_t findSignature = 0;
		std::uint64_t dirtySignature = 0;
		std::uint64_t errorSignature = 0;
		std::uint64_t warningSignature = 0;
		MRMiniMapRenderer::OverlayState overlay;
	};

	MRMiniMapDerivedState() noexcept;

	MRMiniMapRenderer &renderer() noexcept;
	const MRMiniMapRenderer &renderer() const noexcept;

	bool shouldReportInitialRender(std::size_t documentId) const noexcept;
	void markInitialRenderReported(std::size_t documentId) noexcept;

	std::chrono::steady_clock::time_point lastEditAt() const noexcept;
	void setLastEditAt(std::chrono::steady_clock::time_point value) noexcept;
	void clearLastEditAt() noexcept;

	OverlayCache &overlayCache() noexcept;
	const OverlayCache &overlayCache() const noexcept;
	void clearOverlayCache() noexcept;

  private:
	MRMiniMapRenderer mRenderer;
	std::size_t mInitialRenderReportedDocumentId;
	std::chrono::steady_clock::time_point mLastEditAt;
	OverlayCache mOverlayCache;
};

#endif
