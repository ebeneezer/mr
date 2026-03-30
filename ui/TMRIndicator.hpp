#ifndef TMRINDICATOR_HPP
#define TMRINDICATOR_HPP

#define Uses_TIndicator
#define Uses_TDrawBuffer
#define Uses_TEvent
#include <tvision/tv.h>

#include <chrono>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "MRCoprocessor.hpp"

class TMRIndicator : public TIndicator {
  public:
	TMRIndicator(const TRect &bounds) noexcept
	    : TIndicator(bounds), readOnly_(false), displayColumn_(0), displayLine_(0),
	      taskCount_(0), taskDisplayCount_(0), indicatorId_(allocateIndicatorId()), blinkGeneration_(0),
	      taskBlinkGeneration_(0), readOnlyBlinkActive_(false), readOnlyBlinkVisible_(false),
	      taskBlinkActive_(false), taskBlinkVisible_(false), readOnlyBlinkUntil_(), taskBlinkUntil_() {
		registerIndicator(this);
	}

	virtual ~TMRIndicator() override {
		cancelReadOnlyBlinkChain(false);
		cancelTaskBlinkChain(false);
		unregisterIndicator(indicatorId_);
	}

	virtual void draw() override {
		TColorAttr color;
		char frame;
		TDrawBuffer b;
		char cursorText[32];
		char taskCountText[16];
		int cursorX;
		int cursorMinX;

		if ((state & sfDragging) == 0) {
			color = getColor(1);
			frame = kDragFrame;
		} else {
			color = getColor(2);
			frame = kNormalFrame;
		}

		b.moveChar(0, frame, color, size.x);
			if (modified)
				b.moveStr(0, "✎", color, 2);
		if (readOnly_ && (!readOnlyBlinkActive_ || readOnlyBlinkVisible_))
			b.moveStr(2, "🔒", color, 2);
		drawTaskMarkers(b, color, taskCountText);

		std::snprintf(cursorText, sizeof(cursorText), " %lu:%lu ", displayLine_ + 1UL, displayColumn_ + 1UL);
		cursorX = static_cast<int>(size.x) - static_cast<int>(std::strlen(cursorText));
		cursorMinX = taskMarkerEndColumn();
		if (cursorMinX < 12)
			cursorMinX = 12;
		if (cursorX < cursorMinX)
			cursorX = cursorMinX;
		b.moveStr(cursorX, cursorText, color);
		writeBuf(0, 0, size.x, 1, b);
	}

	void setDisplayValue(unsigned long column, unsigned long line, Boolean aModified) {
		if (displayColumn_ != column || displayLine_ != line || modified != aModified) {
			displayColumn_ = column;
			displayLine_ = line;
			modified = aModified;
			location.x = short(column > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : column);
			location.y = short(line > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : line);
			drawView();
		}
	}

	void setReadOnly(bool readOnly) {
		if (readOnly_ != readOnly) {
			bool wasReadOnly = readOnly_;
			readOnly_ = readOnly;
			if (!wasReadOnly && readOnly_)
				startReadOnlyBlink();
			else if (!readOnly_)
				stopReadOnlyBlink();
			drawView();
		}
	}

	void setTaskCount(std::size_t taskCount) {
		if (taskCount_ != taskCount) {
			std::size_t previousDisplayedCount = taskDisplayCount_;
			taskCount_ = taskCount;
			if (taskCount_ == 0 && previousDisplayedCount == 0) {
				drawView();
				return;
			}
			taskDisplayCount_ = taskCount_ != 0 ? taskCount_ : previousDisplayedCount;
			startTaskBlink();
		}
	}

	bool isReadOnly() const {
		return readOnly_;
	}

	static bool applyBlinkUpdate(std::size_t indicatorId, mr::coprocessor::IndicatorBlinkChannel channel,
	                             std::size_t generation, bool visible) {
		TMRIndicator *indicator = lookupIndicator(indicatorId);
		if (indicator == nullptr)
			return false;
		return indicator->applyBlinkUpdateImpl(channel, generation, visible);
	}

  private:
	static constexpr char kDragFrame = '\xCD';
	static constexpr char kNormalFrame = '\xC4';
		static constexpr char kTaskMarkerIcon[] = "🧠";
	static constexpr auto kBlinkSlice = std::chrono::milliseconds(10);
	static constexpr int kBlinkSlicesPerTick = 25;

	static std::size_t allocateIndicatorId() noexcept {
		static std::atomic<std::size_t> nextId(1);
		return nextId.fetch_add(1, std::memory_order_relaxed);
	}

	static std::unordered_map<std::size_t, TMRIndicator *> &indicatorRegistry() {
		static std::unordered_map<std::size_t, TMRIndicator *> registry;
		return registry;
	}

	static std::mutex &indicatorRegistryMutex() {
		static std::mutex mutex;
		return mutex;
	}

	static void registerIndicator(TMRIndicator *indicator) {
		if (indicator == nullptr)
			return;
		std::lock_guard<std::mutex> lock(indicatorRegistryMutex());
		indicatorRegistry()[indicator->indicatorId_] = indicator;
	}

	static void unregisterIndicator(std::size_t indicatorId) {
		std::lock_guard<std::mutex> lock(indicatorRegistryMutex());
		indicatorRegistry().erase(indicatorId);
	}

	static TMRIndicator *lookupIndicator(std::size_t indicatorId) {
		std::lock_guard<std::mutex> lock(indicatorRegistryMutex());
		std::unordered_map<std::size_t, TMRIndicator *>::iterator it = indicatorRegistry().find(indicatorId);
		return it != indicatorRegistry().end() ? it->second : nullptr;
	}

	void drawTaskMarkers(TDrawBuffer &b, TColorAttr baseColor, char *taskCountText) const {
		if (taskDisplayCount_ == 0 || (taskBlinkActive_ && !taskBlinkVisible_))
			return;
		if (taskDisplayCount_ > 10) {
			TColorAttr markerColor = taskMarkerColor(0, baseColor);
			std::snprintf(taskCountText, 16, "%zu", taskDisplayCount_);
			b.moveStr(5, kTaskMarkerIcon, markerColor, 2);
			b.moveStr(7, taskCountText, markerColor);
			return;
		}
		for (std::size_t i = 0; i < taskDisplayCount_; ++i)
			b.moveStr(static_cast<ushort>(5 + i * 2), kTaskMarkerIcon, taskMarkerColor(i, baseColor), 2);
	}

	int taskMarkerEndColumn() const noexcept {
		if (taskDisplayCount_ == 0 || (taskBlinkActive_ && !taskBlinkVisible_))
			return 5;
		if (taskDisplayCount_ > 10) {
			char taskCountText[16];
			std::snprintf(taskCountText, sizeof(taskCountText), "%zu", taskDisplayCount_);
			return 8 + static_cast<int>(std::strlen(taskCountText));
		}
		return 6 + static_cast<int>(taskDisplayCount_ * 2);
	}

	TColorAttr taskMarkerColor(std::size_t index, TColorAttr baseColor) const {
		static const TColorRGB taskColors[] = {
		    TColorRGB(0x4D, 0xD8, 0xFF), TColorRGB(0xFF, 0xD3, 0x4D), TColorRGB(0x7D, 0xFF, 0x7A),
		    TColorRGB(0xFF, 0x8A, 0x65), TColorRGB(0xFF, 0x79, 0xC6), TColorRGB(0xA8, 0xFF, 0x60)};
		TColorAttr taskColor = baseColor;
		setFore(taskColor, TColorDesired(taskColors[index % (sizeof(taskColors) / sizeof(taskColors[0]))]));
		setStyle(taskColor, getStyle(taskColor) | slBold);
		return taskColor;
	}

	void startReadOnlyBlink() {
		cancelReadOnlyBlinkChain(false);
		++blinkGeneration_;
		readOnlyBlinkActive_ = true;
		readOnlyBlinkVisible_ = true;
		readOnlyBlinkUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		scheduleReadOnlyBlinkTick(false);
	}

	void cancelReadOnlyBlinkChain(bool redraw) {
		++blinkGeneration_;
		readOnlyBlinkActive_ = false;
		readOnlyBlinkVisible_ = true;
		if (redraw)
			drawView();
	}

	void stopReadOnlyBlink() {
		cancelReadOnlyBlinkChain(true);
	}

	void startTaskBlink() {
		++taskBlinkGeneration_;
		taskBlinkActive_ = true;
		taskBlinkVisible_ = true;
		taskBlinkUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		drawView();
		scheduleTaskBlinkTick(false);
	}

	void cancelTaskBlinkChain(bool redraw) {
		++taskBlinkGeneration_;
		taskBlinkActive_ = false;
		taskBlinkVisible_ = true;
		if (taskCount_ == 0)
			taskDisplayCount_ = 0;
		else
			taskDisplayCount_ = taskCount_;
		if (redraw)
			drawView();
	}

	void stopTaskBlink() {
		cancelTaskBlinkChain(true);
	}

	void scheduleReadOnlyBlinkTick(bool nextVisible) {
		const std::size_t generation = blinkGeneration_;
		const std::size_t indicatorId = indicatorId_;
		mr::coprocessor::globalCoprocessor().submit(
		    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::IndicatorBlink, 0, generation,
		    "indicator-blink-readonly",
		    [indicatorId, generation, nextVisible](const mr::coprocessor::TaskInfo &info,
		                                           std::stop_token stopToken) {
			    mr::coprocessor::Result result;
			    result.task = info;
			    for (int step = 0; step < kBlinkSlicesPerTick; ++step) {
				    if (stopToken.stop_requested()) {
					    result.status = mr::coprocessor::TaskStatus::Cancelled;
					    return result;
				    }
				    std::this_thread::sleep_for(kBlinkSlice);
			    }
			    result.status = mr::coprocessor::TaskStatus::Completed;
			    result.payload = std::make_shared<mr::coprocessor::IndicatorBlinkPayload>(
			        indicatorId, generation, nextVisible, mr::coprocessor::IndicatorBlinkChannel::ReadOnly);
			    return result;
		    });
	}

	void scheduleTaskBlinkTick(bool nextVisible) {
		const std::size_t generation = taskBlinkGeneration_;
		const std::size_t indicatorId = indicatorId_;
		mr::coprocessor::globalCoprocessor().submit(
		    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::IndicatorBlink, 0, generation,
		    "indicator-blink-task",
		    [indicatorId, generation, nextVisible](const mr::coprocessor::TaskInfo &info,
		                                           std::stop_token stopToken) {
			    mr::coprocessor::Result result;
			    result.task = info;
			    for (int step = 0; step < kBlinkSlicesPerTick; ++step) {
				    if (stopToken.stop_requested()) {
					    result.status = mr::coprocessor::TaskStatus::Cancelled;
					    return result;
				    }
				    std::this_thread::sleep_for(kBlinkSlice);
			    }
			    result.status = mr::coprocessor::TaskStatus::Completed;
			    result.payload = std::make_shared<mr::coprocessor::IndicatorBlinkPayload>(
			        indicatorId, generation, nextVisible, mr::coprocessor::IndicatorBlinkChannel::TaskMarker);
			    return result;
		    });
	}

	bool applyBlinkUpdateImpl(mr::coprocessor::IndicatorBlinkChannel channel, std::size_t generation,
	                          bool visible) {
		switch (channel) {
			case mr::coprocessor::IndicatorBlinkChannel::TaskMarker:
				if (generation != taskBlinkGeneration_ || !taskBlinkActive_)
					return false;
				if (std::chrono::steady_clock::now() >= taskBlinkUntil_) {
					stopTaskBlink();
					return true;
				}
				taskBlinkVisible_ = visible;
				drawView();
				scheduleTaskBlinkTick(!visible);
				return true;
			case mr::coprocessor::IndicatorBlinkChannel::ReadOnly:
			default:
				if (generation != blinkGeneration_ || !readOnly_ || !readOnlyBlinkActive_)
					return false;
				if (std::chrono::steady_clock::now() >= readOnlyBlinkUntil_) {
					stopReadOnlyBlink();
					return true;
				}
				readOnlyBlinkVisible_ = visible;
				drawView();
				scheduleReadOnlyBlinkTick(!visible);
				return true;
		}
	}

	bool readOnly_;
	unsigned long displayColumn_;
	unsigned long displayLine_;
	std::size_t taskCount_;
	std::size_t taskDisplayCount_;
	std::size_t indicatorId_;
	std::size_t blinkGeneration_;
	std::size_t taskBlinkGeneration_;
	bool readOnlyBlinkActive_;
	bool readOnlyBlinkVisible_;
	bool taskBlinkActive_;
	bool taskBlinkVisible_;
	std::chrono::steady_clock::time_point readOnlyBlinkUntil_;
	std::chrono::steady_clock::time_point taskBlinkUntil_;
};

#endif
