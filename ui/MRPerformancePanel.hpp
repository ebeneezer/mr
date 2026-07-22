#ifndef MRPERFORMANCEPANEL_HPP
#define MRPERFORMANCEPANEL_HPP

#define Uses_TDrawBuffer
#define Uses_TPalette
#define Uses_TRect
#define Uses_TView
#include <tvision/tv.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace mr {
namespace coprocessor {
struct WorkerTelemetrySnapshot;
}
}

class MRPerformancePanel : public TView {
  public:
	static constexpr int kPreferredHeight = 7;

	explicit MRPerformancePanel(const TRect &bounds) noexcept;

	void refresh() noexcept;
	virtual void draw() override;
	virtual TPalette &getPalette() const override;

  private:
	static constexpr std::size_t kWorkerEventSlotCount = 4;

	struct WorkerEventSlot {
		bool occupied;
		std::uint64_t workerOrdinal;
		std::uint64_t eventSequence;
		std::string line;
		TColorAttr color;

		WorkerEventSlot() noexcept;
	};

	void updateActivityScale(std::size_t peakValue, TColorAttr peakColor, std::chrono::steady_clock::time_point now) noexcept;
	void updateWorkerEventLog(const mr::coprocessor::WorkerTelemetrySnapshot &telemetry);
	void drawActivityMeter(std::size_t activeCount, std::size_t queuedCount, std::size_t resultCount);
	void writePanelLine(int y, const char *label, const std::string &line, TColorAttr color);
	bool mRefreshInitialized;
	std::uint64_t mLastRefreshSignature;
	std::size_t mActivityScale;
	TColorAttr mActivityScaleColor;
	std::chrono::steady_clock::time_point mScaleDecreaseAt;
	std::uint64_t mLastWorkerEventSequence;
	std::uint64_t mWorkerEventTimeOriginMicros;
	std::array<WorkerEventSlot, kWorkerEventSlotCount> mWorkerEventSlots;
};

#endif
