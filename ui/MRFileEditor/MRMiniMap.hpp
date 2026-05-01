#pragma once

#define Uses_TText
#define Uses_TDrawBuffer
#include <tvision/tv.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../config/MRDialogPaths.hpp"
#include "../../coprocessor/MRCoprocessor.hpp"
#include "../../piecetable/MRTextDocument.hpp"

class MRMiniMapRenderer {
  public:
	struct Palette {
		TColorAttr normal = 0;
		TColorAttr viewport = 0;
		TColorAttr changed = 0;
		TColorAttr findMarker = 0;
		TColorAttr errorMarker = 0;
	};

	struct OverlayState {
		struct LineMask {
			std::size_t lineIndex = 0;
			std::uint64_t dotColumnBits = 0;
		};
		std::vector<LineMask> findLineMasks;
		std::vector<LineMask> dirtyLineMasks;
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
	Signals clearWarmupTask(std::uint64_t expectedTaskId) noexcept;
	Signals invalidate(bool cancelTask, std::size_t documentId) noexcept;
	ApplyWarmupResult applyWarmup(const mr::coprocessor::MiniMapWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId, std::size_t documentId, std::size_t version) noexcept;
	Signals scheduleWarmupIfNeeded(const Viewport &viewport, int rowCount, bool useBraille, std::size_t totalLinesHint, std::size_t topLine, std::size_t documentId, std::size_t version, const mr::editor::ReadSnapshot &snapshot, const MREditSetupSettings &settings);
	OverlayState computeOverlayState(const mr::editor::ReadSnapshot &snapshot, const mr::editor::Range &selection, const std::vector<mr::editor::Range> &findRanges, const std::vector<mr::editor::Range> &dirtyRanges, std::size_t totalLines, int viewportWidth, int miniMapBodyWidth, bool useBraille, const MREditSetupSettings &settings) const;
	void drawGutter(TDrawBuffer &buffer, int y, int miniMapRows, int viewWidth, const Viewport &viewport, std::size_t totalLines, std::size_t topLine, bool useBraille, const std::string &viewportMarkerGlyph, const Palette &palette, const OverlayState &overlay) const;

  private:
	struct Impl;
	std::unique_ptr<Impl> mImpl;
};
