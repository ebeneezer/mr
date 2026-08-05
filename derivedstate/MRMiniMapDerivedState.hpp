#ifndef MRMINIMAPDERIVEDSTATE_HPP
#define MRMINIMAPDERIVEDSTATE_HPP

#include "../ui/MRFileEditor/MRMiniMap.hpp"

#include <cstddef>
#include <memory>
#include <vector>

class MRMiniMapDerivedState {
  public:
	MRMiniMapDerivedState() noexcept;

	MRMiniMapRenderer &renderer() noexcept;
	const MRMiniMapRenderer &renderer() const noexcept;

	bool shouldReportInitialRender(std::size_t documentId) const noexcept;
	void markInitialRenderReported(std::size_t documentId) noexcept;

	const MRMiniMapRenderer::OverlaySources &overlaySources() const noexcept;
	void setFindRanges(const std::vector<mr::editor::Range> &ranges);
	void setDirtyRanges(const std::vector<mr::editor::Range> &ranges);
	void adoptCompilerRanges(const std::shared_ptr<const std::vector<mr::editor::Range>> &errorRanges,
	                         const std::shared_ptr<const std::vector<mr::editor::Range>> &warningRanges);
	void adoptFileCompareRanges(const std::shared_ptr<const std::vector<unsigned char>> &lineKinds,
	                           const std::shared_ptr<const std::vector<MRFileCompareMiniMapSlice>> &slices);

  private:
	MRMiniMapRenderer mRenderer;
	std::size_t mInitialRenderReportedDocumentId;
	MRMiniMapRenderer::OverlaySources mOverlaySources;
};

#endif
