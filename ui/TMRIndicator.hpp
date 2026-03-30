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
#include <thread>
#include <unordered_map>

#include "MRCoprocessor.hpp"

class TMRIndicator : public TIndicator {
  public:
	TMRIndicator(const TRect &bounds) noexcept
	    : TIndicator(bounds), readOnly_(false), displayColumn_(0), displayLine_(0),
	      indicatorId_(allocateIndicatorId()), blinkGeneration_(0), readOnlyBlinkActive_(false),
	      readOnlyBlinkVisible_(false), readOnlyBlinkUntil_() {
		registerIndicator(this);
	}

	virtual ~TMRIndicator() override {
		cancelBlinkChain(false);
		unregisterIndicator(indicatorId_);
	}

	virtual void draw() override {
		TColorAttr color;
		char frame;
		TDrawBuffer b;
		char s[32];

		if ((state & sfDragging) == 0) {
			color = getColor(1);
			frame = kDragFrame;
		} else {
			color = getColor(2);
			frame = kNormalFrame;
		}

		b.moveChar(0, frame, color, size.x);
		if (readOnly_ && (!readOnlyBlinkActive_ || readOnlyBlinkVisible_))
			b.moveStr(0, "🔒", color, 2);
		else if (modified)
			b.putChar(0, '*');

		std::snprintf(s, sizeof(s), " %lu:%lu ", displayLine_ + 1UL, displayColumn_ + 1UL);
		b.moveStr(8 - int(std::strchr(s, ':') - s), s, color);
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

	bool isReadOnly() const {
		return readOnly_;
	}

	static bool applyBlinkUpdate(std::size_t indicatorId, std::size_t generation, bool visible) {
		TMRIndicator *indicator = lookupIndicator(indicatorId);
		if (indicator == nullptr)
			return false;
		return indicator->applyBlinkUpdateImpl(generation, visible);
	}

  private:
	static constexpr char kDragFrame = '\xCD';
	static constexpr char kNormalFrame = '\xC4';
	static constexpr auto kBlinkInterval = std::chrono::milliseconds(250);

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

	void startReadOnlyBlink() {
		cancelBlinkChain(false);
		++blinkGeneration_;
		readOnlyBlinkActive_ = true;
		readOnlyBlinkVisible_ = true;
		readOnlyBlinkUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		scheduleBlinkTick(false);
	}

	void cancelBlinkChain(bool redraw) {
		++blinkGeneration_;
		readOnlyBlinkActive_ = false;
		readOnlyBlinkVisible_ = true;
		if (redraw)
			drawView();
	}

	void stopReadOnlyBlink() {
		cancelBlinkChain(true);
	}

	void scheduleBlinkTick(bool nextVisible) {
		const std::size_t generation = blinkGeneration_;
		const std::size_t indicatorId = indicatorId_;
		mr::coprocessor::globalCoprocessor().submit(
		    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::IndicatorBlink, 0, generation,
		    "indicator-blink",
		    [indicatorId, generation, nextVisible](const mr::coprocessor::TaskInfo &info,
		                                           std::stop_token stopToken) {
			    mr::coprocessor::Result result;
			    result.task = info;
			    for (int step = 0; step < 25; ++step) {
				    if (stopToken.stop_requested()) {
					    result.status = mr::coprocessor::TaskStatus::Cancelled;
					    return result;
				    }
				    std::this_thread::sleep_for(std::chrono::milliseconds(10));
			    }
			    result.status = mr::coprocessor::TaskStatus::Completed;
			    result.payload = std::make_shared<mr::coprocessor::IndicatorBlinkPayload>(
			        indicatorId, generation, nextVisible);
			    return result;
		    });
	}

	bool applyBlinkUpdateImpl(std::size_t generation, bool visible) {
		if (generation != blinkGeneration_ || !readOnly_ || !readOnlyBlinkActive_)
			return false;
		if (std::chrono::steady_clock::now() >= readOnlyBlinkUntil_) {
			stopReadOnlyBlink();
			return true;
		}
		readOnlyBlinkVisible_ = visible;
		drawView();
		scheduleBlinkTick(!visible);
		return true;
	}

	bool readOnly_;
	unsigned long displayColumn_;
	unsigned long displayLine_;
	std::size_t indicatorId_;
	std::size_t blinkGeneration_;
	bool readOnlyBlinkActive_;
	bool readOnlyBlinkVisible_;
	std::chrono::steady_clock::time_point readOnlyBlinkUntil_;
};

#endif
