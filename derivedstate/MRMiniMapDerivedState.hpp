#ifndef MRMINIMAPDERIVEDSTATE_HPP
#define MRMINIMAPDERIVEDSTATE_HPP

#include "MRDerivedStateBase.hpp"

#include "../ui/MRFileEditor/MRMiniMap.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <vector>

class MRMiniMapDerivedState : public MRDerivedStateBase {
  public:
	MRMiniMapDerivedState() noexcept;

	MRMiniMapRenderer &renderer() noexcept;
	const MRMiniMapRenderer &renderer() const noexcept;

	bool shouldReportInitialRender(std::size_t documentId) const noexcept;
	void markInitialRenderReported(std::size_t documentId) noexcept;

	std::chrono::steady_clock::time_point lastEditAt() const noexcept;
	void setLastEditAt(std::chrono::steady_clock::time_point value) noexcept;
	void clearLastEditAt() noexcept;

	const MRMiniMapRenderer::OverlaySources &overlaySources() const noexcept;
	void setFindRanges(const std::vector<mr::editor::Range> &ranges);
	void setDirtyRanges(const std::vector<mr::editor::Range> &ranges);
	void setCompilerRanges(const std::vector<mr::editor::Range> &errorRanges, const std::vector<mr::editor::Range> &warningRanges);
	void adoptCompilerRanges(const std::shared_ptr<const std::vector<mr::editor::Range>> &errorRanges,
	                         const std::shared_ptr<const std::vector<mr::editor::Range>> &warningRanges);
	void adoptFileCompareRanges(const std::shared_ptr<const std::vector<unsigned char>> &lineKinds,
	                           const std::shared_ptr<const std::vector<MRFileCompareMiniMapSlice>> &slices);

  private:
	MRMiniMapRenderer mRenderer;
	std::size_t mInitialRenderReportedDocumentId;
	std::chrono::steady_clock::time_point mLastEditAt;
	MRMiniMapRenderer::OverlaySources mOverlaySources;
};

#endif
