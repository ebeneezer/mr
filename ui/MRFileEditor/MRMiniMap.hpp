#pragma once

#define Uses_TText
#define Uses_TDrawBuffer
#include <tvision/tv.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../coprocessor/MRCoprocessor.hpp"
#include "../../piecetable/MRTextDocument.hpp"

enum MRFileCompareLineKind : unsigned char {
	mrfclkNone = 0,
	mrfclkEqual,
	mrfclkMissing,
	mrfclkInsert,
	mrfclkOffset
};

struct MRFileCompareMiniMapSlice {
	std::size_t lineIndex = 0;
	std::size_t sliceStart = 0;
	std::size_t sliceEnd = 0;
	unsigned char lineKind = mrfclkNone;
	bool fullLine = false;
};

class MRMiniMapRenderer {
  public:
	enum OverlayComponentMask : unsigned int {
		overlayFind = 1U << 0,
		overlayDirty = 1U << 1,
		overlayError = 1U << 2,
		overlayWarning = 1U << 3,
		overlayDiff = 1U << 4,
		overlayAll = overlayFind | overlayDirty | overlayError | overlayWarning | overlayDiff
	};

	struct Palette {
		TColorAttr normal = 0;
		TColorAttr viewport = 0;
		TColorAttr changed = 0;
		TColorAttr findMarker = 0;
		TColorAttr errorMarker = 0;
		TColorAttr warningMarker = 0;
		TColorAttr diffEqual = 0;
		TColorAttr diffMissing = 0;
		TColorAttr diffInsert = 0;
		TColorAttr diffOffset = 0;
	};

	struct OverlayState {
		struct LineMask {
			std::size_t lineIndex = 0;
			std::uint64_t dotColumnBits = 0;
		};
		using LineMasks = std::vector<LineMask>;

		std::shared_ptr<const LineMasks> errorLineMasks;
		std::shared_ptr<const LineMasks> warningLineMasks;
		std::shared_ptr<const LineMasks> findLineMasks;
		std::shared_ptr<const LineMasks> dirtyLineMasks;
		std::shared_ptr<const LineMasks> diffEqualLineMasks;
		std::shared_ptr<const LineMasks> diffMissingLineMasks;
		std::shared_ptr<const LineMasks> diffInsertLineMasks;
		std::shared_ptr<const LineMasks> diffOffsetLineMasks;

		OverlayState();
	};

	struct OverlaySources {
		std::uint64_t revision;
		std::shared_ptr<const std::vector<mr::editor::Range>> findRanges;
		std::shared_ptr<const std::vector<mr::editor::Range>> dirtyRanges;
		std::shared_ptr<const std::vector<mr::editor::Range>> errorRanges;
		std::shared_ptr<const std::vector<mr::editor::Range>> warningRanges;
		std::shared_ptr<const std::vector<unsigned char>> fileCompareLineKinds;
		std::shared_ptr<const std::vector<MRFileCompareMiniMapSlice>> fileCompareMiniMapSlices;

		OverlaySources();
	};

	struct Viewport {
		int width = 1;
		int bodyX = -1;
		int bodyWidth = 0;
		int infoX = -1;
		int separatorX = -1;
	};

	struct Signals {
		bool notifyTaskStateChanged = false;
		bool redraw = false;

		void merge(const Signals &other) noexcept {
			notifyTaskStateChanged = notifyTaskStateChanged || other.notifyTaskStateChanged;
			redraw = redraw || other.redraw;
		}
	};

	struct ApplyWarmupResult {
		bool applied = false;
		Signals signals;
	};

	MRMiniMapRenderer() noexcept;
	~MRMiniMapRenderer() noexcept;

	MRMiniMapRenderer(const MRMiniMapRenderer &) = delete;
	MRMiniMapRenderer &operator=(const MRMiniMapRenderer &) = delete;

	static bool useBrailleRenderer() noexcept;
	static std::string normalizedViewportMarkerGlyph(const std::string &configuredGlyph);

	std::uint64_t pendingWarmupTaskId() const noexcept;
	std::size_t pendingWarmupTaskCount() const noexcept;
	bool ownsWarmupTask(std::uint64_t taskId) const noexcept;
	bool hasProjection(int rowCount, int bodyWidth) const noexcept;
	bool hasAnyProjection() const noexcept;
	Signals clearWarmupTask(std::uint64_t expectedTaskId) noexcept;
	Signals invalidate(bool cancelTask, std::size_t documentId) noexcept;
	ApplyWarmupResult applyWarmup(const mr::coprocessor::Payload &payload, const mr::coprocessor::Result &result, std::size_t documentId, std::size_t version) noexcept;
	Signals scheduleWarmupIfNeeded(const Viewport &viewport, int rowCount, bool useBraille, std::size_t totalLinesHint, std::size_t topLine, std::size_t documentId, std::size_t version,
	                              mr::coprocessor::ExecutionOwnerKind executionOwnerKind, std::size_t executionOwnerLocalId, const mr::editor::ReadSnapshot &snapshot,
	                              const MREditSetupSettings &settings, const OverlaySources &overlaySources, const mr::editor::Range &selection);
	const OverlayState &overlayProjection() const noexcept;
	void drawGutter(TDrawBuffer &buffer, int y, int miniMapRows, int viewWidth, const Viewport &viewport, std::size_t totalLines, std::size_t topLine, bool useBraille, const std::string &viewportMarkerGlyph, const Palette &palette, const OverlayState &overlay) const;

	static std::shared_ptr<const OverlayState> computeOverlayState(const mr::editor::ReadSnapshot &snapshot, const mr::editor::Range &selection, const OverlaySources &sources,
	                                                               std::size_t totalLines, int viewportWidth, int miniMapBodyWidth, bool useBraille,
	                                                               const MREditSetupSettings &settings, unsigned int componentMask);

  private:
	struct RendererState;
	std::unique_ptr<RendererState> mState;
};

struct MRMiniMapOverlayPacketPayload final : mr::coprocessor::Payload {
	std::uint64_t generation;
	unsigned int componentMask;
	std::shared_ptr<const MRMiniMapRenderer::OverlayState> projection;

	MRMiniMapOverlayPacketPayload(std::uint64_t aGeneration, unsigned int aComponentMask, std::shared_ptr<const MRMiniMapRenderer::OverlayState> aProjection) noexcept
	    : generation(aGeneration), componentMask(aComponentMask), projection(std::move(aProjection)) {
	}
};
