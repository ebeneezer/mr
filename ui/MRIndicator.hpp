#ifndef MRINDICATOR_HPP
#define MRINDICATOR_HPP

#define Uses_TIndicator
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TView
#define Uses_TGroup
#define Uses_TPalette
#define Uses_TFrame
#define Uses_TWindow
#include <tvision/tv.h>

#include <chrono>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../config/MRDialogPaths.hpp"
#include "MRCoprocessor.hpp"

void mrvmUiInvalidateScreenBase() noexcept;

class MRTaskOverviewPopup : public TView {
  public:
	MRTaskOverviewPopup(const TRect &bounds) noexcept : TView(bounds), lines_() {
		eventMask = 0;
		options |= ofBuffered;
	}

	void setLines(const std::vector<std::string> &lines) {
		lines_ = lines;
		drawView();
	}

	virtual void draw() override {
		TDrawBuffer b;
		TColorAttr border = getColor(1);
		TColorAttr text = getColor(2);
		std::size_t i;

		for (int y = 0; y < size.y; ++y) {
			b.moveChar(0, ' ', text, size.x);
			if (y == 0 || y == size.y - 1) {
				b.moveChar(0, '-', border, size.x);
				b.moveChar(0, '+', border, 1);
				b.moveChar(size.x - 1, '+', border, 1);
			} else {
				b.moveChar(0, '|', border, 1);
				b.moveChar(size.x - 1, '|', border, 1);
			}

			if (y == 0)
				b.moveStr(2, " Tasks ", border);
			else if (y - 1 < static_cast<int>(lines_.size())) {
				std::string textLine = lines_[static_cast<std::size_t>(y - 1)];
				if (textLine.size() > static_cast<std::size_t>(std::max(0, size.x - 3)))
					textLine.resize(static_cast<std::size_t>(std::max(0, size.x - 4)));
				for (i = 0; i < textLine.size(); ++i)
					if (static_cast<unsigned char>(textLine[i]) < 32)
						textLine[i] = ' ';
				b.moveStr(2, textLine.c_str(), text);
			}

			writeBuf(0, y, size.x, 1, b);
		}
		mrvmUiInvalidateScreenBase();
	}

	virtual TPalette &getPalette() const override {
		static const TColorAttr paletteData[] = {0x1F, 0x1E};
		static TPalette palette(paletteData, 2);
		return palette;
	}

  private:
	std::vector<std::string> lines_;
};

class MRIndicator : public TIndicator {
  public:
	enum class NoticeKind : unsigned char {
		Info,
		Success,
		Warning,
		Error
	};

	using TaskOverviewProvider = std::function<std::vector<std::string>()>;

	MRIndicator(const TRect &bounds) noexcept
	    : TIndicator(bounds), readOnly_(false), displayColumn_(0), displayLine_(0),
	      taskCount_(0), taskDisplayCount_(0), indicatorId_(allocateIndicatorId()), blinkGeneration_(0),
	      taskBlinkGeneration_(0), readOnlyBlinkActive_(false), readOnlyBlinkVisible_(false),
	      taskBlinkActive_(false), taskBlinkVisible_(false), statusNoticeGeneration_(0),
	      statusNoticeActive_(false), statusNoticeText_(), statusNoticeKind_(NoticeKind::Info),
	      taskOverviewProvider_(), taskOverviewPopup_(nullptr), readOnlyBlinkUntil_(), taskBlinkUntil_(),
	      taskBlinkTaskId_(0), readOnlyBlinkTaskId_(0), statusNoticeTaskId_(0) {
		registerIndicator(this);
	}

	virtual ~MRIndicator() override {
		hideTaskOverview();
		cancelReadOnlyBlinkChain(false);
		cancelTaskBlinkChain(false);
		unregisterIndicator(indicatorId_);
	}

	virtual void draw() override {
		TColorAttr color;
		TColorAttr cursorColor;
		char frame;
		TDrawBuffer b;
		std::string cursorTextBuffer;
		const char *cursorText;
		int cursorX;
		int cursorMinX;
		int noticeStartX;
		int noticeEndX;

		if ((state & sfDragging) == 0) {
			color = getColor(1);
			frame = kDragFrame;
		} else {
			color = getColor(2);
			frame = kNormalFrame;
		}
		cursorColor = getColor(3);

		b.moveChar(0, frame, color, size.x);
		noticeStartX = 1;
		noticeEndX = noticeStartX;
		if (statusNoticeActive_) {
			drawStatusNotice(b, noticeStartX, color);
			noticeEndX = noticeStartX + static_cast<int>(statusNoticeText_.size()) + 1;
		}

		cursorTextBuffer = resolvedCursorPositionMarkerText(displayLine_ + 1UL, displayColumn_ + 1UL);
		cursorText = cursorTextBuffer.c_str();
		cursorX = static_cast<int>(size.x) - static_cast<int>(std::strlen(cursorText));
		cursorMinX = 1;
		if (noticeEndX > cursorMinX)
			cursorMinX = noticeEndX;
		if (cursorX < cursorMinX)
			cursorX = cursorMinX;
		b.moveStr(cursorX, cursorText, cursorColor);
		writeBuf(0, 0, size.x, 1, b);
		mrvmUiInvalidateScreenBase();
	}

	virtual TPalette &getPalette() const override {
		// 1..2 keep TIndicator semantics (frame passive/active).
		// 3 maps cursor line/column text to window-local slot 12.
		static TPalette palette("\x02\x03\x0C", 3);
		return palette;
	}

	void setDisplayValue(unsigned long column, unsigned long line, Boolean aModified) {
		if (displayColumn_ != column || displayLine_ != line || modified != aModified) {
			displayColumn_ = column;
			displayLine_ = line;
			modified = aModified;
			location.x = short(column > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : column);
			location.y = short(line > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : line);
			drawView();
			redrawFrame();
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
			redrawFrame();
		}
	}

	void setTaskCount(std::size_t taskCount) {
		if (taskCount_ != taskCount) {
			std::size_t previousDisplayedCount = taskDisplayCount_;
			taskCount_ = taskCount;
			if (taskCount_ == 0 && previousDisplayedCount == 0) {
				hideTaskOverview();
				drawView();
				return;
			}
			taskDisplayCount_ = taskCount_ != 0 ? taskCount_ : previousDisplayedCount;
			if (taskCount_ == 0)
				hideTaskOverview();
			else if (taskOverviewPopup_ != nullptr)
				showTaskOverview();
			startTaskBlink();
			redrawFrame();
		}
	}

	void setTaskOverviewProvider(TaskOverviewProvider provider) {
		taskOverviewProvider_ = std::move(provider);
	}

	void updateTaskHover(TPoint globalMouse, bool forceHide = false) {
		if (forceHide || taskCount_ == 0 || !taskOverviewProvider_) {
			hideTaskOverview();
			return;
		}
		if (!mouseInView(globalMouse)) {
			hideTaskOverview();
			return;
		}
		TPoint local = makeLocal(globalMouse);
		if (local.y != 0 || local.x < 5 || local.x >= 7) {
			hideTaskOverview();
			return;
		}
		showTaskOverview();
	}

	void showStatusNotice(const std::string &text, NoticeKind kind,
	                      std::chrono::milliseconds duration = std::chrono::seconds(5)) {
		if (text.empty()) {
			cancelStatusNotice(true);
			return;
		}
		++statusNoticeGeneration_;
		statusNoticeActive_ = true;
		statusNoticeText_ = text;
		statusNoticeKind_ = kind;
		drawView();
		scheduleStatusNoticeClear(duration);
	}

	bool isReadOnly() const {
		return readOnly_;
	}

	bool shouldDrawReadOnlyMarker() const noexcept {
		return readOnly_ && (!readOnlyBlinkActive_ || readOnlyBlinkVisible_);
	}

	bool shouldDrawTaskMarker() const noexcept {
		return taskDisplayCount_ != 0 && (!taskBlinkActive_ || taskBlinkVisible_);
	}

	bool hasReadOnlyMarkerSlot() const noexcept {
		return readOnly_;
	}

	bool hasTaskMarkerSlot() const noexcept {
		return taskDisplayCount_ != 0;
	}

	static bool applyBlinkUpdate(std::size_t indicatorId, mr::coprocessor::IndicatorBlinkChannel channel,
	                             std::size_t generation, bool visible) {
		MRIndicator *indicator = lookupIndicator(indicatorId);
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

	static std::unordered_map<std::size_t, MRIndicator *> &indicatorRegistry() {
		static std::unordered_map<std::size_t, MRIndicator *> registry;
		return registry;
	}

	static std::mutex &indicatorRegistryMutex() {
		static std::mutex mutex;
		return mutex;
	}

	static void registerIndicator(MRIndicator *indicator) {
		if (indicator == nullptr)
			return;
		std::lock_guard<std::mutex> lock(indicatorRegistryMutex());
		indicatorRegistry()[indicator->indicatorId_] = indicator;
	}

	static void unregisterIndicator(std::size_t indicatorId) {
		std::lock_guard<std::mutex> lock(indicatorRegistryMutex());
		indicatorRegistry().erase(indicatorId);
	}

	static MRIndicator *lookupIndicator(std::size_t indicatorId) {
		std::lock_guard<std::mutex> lock(indicatorRegistryMutex());
		std::unordered_map<std::size_t, MRIndicator *>::iterator it = indicatorRegistry().find(indicatorId);
		return it != indicatorRegistry().end() ? it->second : nullptr;
	}

	void redrawFrame() {
		TWindow *window = owner != nullptr ? static_cast<TWindow *>(owner) : nullptr;
		if (window != nullptr && window->frame != nullptr)
			window->frame->drawView();
	}

	void drawTaskMarkers(TDrawBuffer &b, TColorAttr baseColor) const {
		if (taskDisplayCount_ == 0 || (taskBlinkActive_ && !taskBlinkVisible_))
			return;
		b.moveStr(5, kTaskMarkerIcon, taskMarkerColor(baseColor), 2);
	}

	int taskMarkerEndColumn() const noexcept {
		if (taskDisplayCount_ == 0 || (taskBlinkActive_ && !taskBlinkVisible_))
			return 5;
		return 7;
	}

	TColorAttr taskMarkerColor(TColorAttr baseColor) const {
		TColorAttr taskColor = baseColor;
		setFore(taskColor, TColorDesired(TColorRGB(0xFF, 0x79, 0xC6)));
		setStyle(taskColor, getStyle(taskColor) | slBold);
		return taskColor;
	}

	void showTaskOverview() {
		TGroup *group = owner;
		std::vector<std::string> lines;
		TRect bounds;
		int width = 14;
		int height;
		int left;
		int top;

		if (group == nullptr || !taskOverviewProvider_)
			return;
		lines = taskOverviewProvider_();
		if (lines.empty()) {
			hideTaskOverview();
			return;
		}
		for (std::size_t i = 0; i < lines.size(); ++i)
			if (static_cast<int>(lines[i].size()) + 4 > width)
				width = static_cast<int>(lines[i].size()) + 4;
		if (width > group->size.x - 2)
			width = std::max(12, group->size.x - 2);
		height = static_cast<int>(lines.size()) + 2;
		if (height > group->size.y - 2)
			height = std::max(3, group->size.y - 2);
		left = 1;
		top = group->size.y - 2 - height;
		if (top < 1)
			top = 1;
		bounds = TRect(left, top, left + width, top + height);

		if (taskOverviewPopup_ != nullptr) {
			group->remove(taskOverviewPopup_);
			TObject::destroy(taskOverviewPopup_);
			taskOverviewPopup_ = nullptr;
		}
		taskOverviewPopup_ = new MRTaskOverviewPopup(bounds);
		taskOverviewPopup_->setLines(lines);
		group->insert(taskOverviewPopup_);
		taskOverviewPopup_->makeFirst();
	}

	void hideTaskOverview() {
		TGroup *group = owner;
		if (taskOverviewPopup_ == nullptr)
			return;
		if (group != nullptr)
			group->remove(taskOverviewPopup_);
		TObject::destroy(taskOverviewPopup_);
		taskOverviewPopup_ = nullptr;
	}

	void drawStatusNotice(TDrawBuffer &b, int x, TColorAttr baseColor) const {
		TColorAttr noticeColor = baseColor;
		std::string text;

		if (!statusNoticeActive_ || x >= size.x)
			return;

		text = " ";
		text += statusNoticeText_;
		text += " ";
		setStyle(noticeColor, getStyle(noticeColor) | slBold);
		switch (statusNoticeKind_) {
			case NoticeKind::Success:
				setFore(noticeColor, TColorDesired(TColorRGB(0x7D, 0xFF, 0x7A)));
				break;
			case NoticeKind::Warning:
				setFore(noticeColor, TColorDesired(TColorRGB(0xFF, 0xD3, 0x4D)));
				break;
			case NoticeKind::Error:
				setFore(noticeColor, TColorDesired(TColorRGB(0xFF, 0x8A, 0x65)));
				break;
			case NoticeKind::Info:
			default:
				setFore(noticeColor, TColorDesired(TColorRGB(0x4D, 0xD8, 0xFF)));
				break;
		}
		if (x + static_cast<int>(text.size()) > size.x)
			text.resize(std::max(0, size.x - x));
		if (!text.empty())
			b.moveStr(static_cast<ushort>(x), text.c_str(), noticeColor);
	}

	static std::string resolvedCursorPositionMarkerText(unsigned long row,
	                                                    unsigned long column) {
		std::string format = configuredCursorPositionMarker();
		std::string out;
		const std::string rowText = std::to_string(row);
		const std::string colText = std::to_string(column);

		if (format.empty())
			format = "R:C";
		out.reserve(format.size() + rowText.size() + colText.size() + 2);
		out.push_back(' ');
		for (char ch : format) {
			if (ch == 'R')
				out += rowText;
			else if (ch == 'C')
				out += colText;
			else
				out.push_back(ch);
		}
		out.push_back(' ');
		return out;
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
		if (readOnlyBlinkTaskId_ != 0) {
			mr::coprocessor::globalCoprocessor().cancelTask(readOnlyBlinkTaskId_);
			readOnlyBlinkTaskId_ = 0;
		}
		if (redraw) {
			drawView();
			redrawFrame();
		}
	}

	void stopReadOnlyBlink() {
		cancelReadOnlyBlinkChain(true);
	}

	void startTaskBlink() {
		if (taskBlinkTaskId_ != 0) {
			mr::coprocessor::globalCoprocessor().cancelTask(taskBlinkTaskId_);
			taskBlinkTaskId_ = 0;
		}
		++taskBlinkGeneration_;
		taskBlinkActive_ = true;
		taskBlinkVisible_ = true;
		taskBlinkUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		drawView();
		redrawFrame();
		scheduleTaskBlinkTick(false);
	}

	void cancelTaskBlinkChain(bool redraw) {
		++taskBlinkGeneration_;
		taskBlinkActive_ = false;
		taskBlinkVisible_ = true;
		if (taskBlinkTaskId_ != 0) {
			mr::coprocessor::globalCoprocessor().cancelTask(taskBlinkTaskId_);
			taskBlinkTaskId_ = 0;
		}
		if (taskCount_ == 0)
			taskDisplayCount_ = 0;
		else
			taskDisplayCount_ = taskCount_;
		if (redraw) {
			drawView();
			redrawFrame();
		}
	}

	void stopTaskBlink() {
		cancelTaskBlinkChain(true);
	}

	void scheduleStatusNoticeClear(std::chrono::milliseconds duration) {
		if (statusNoticeTaskId_ != 0) {
			mr::coprocessor::globalCoprocessor().cancelTask(statusNoticeTaskId_);
			statusNoticeTaskId_ = 0;
		}
		const std::size_t generation = statusNoticeGeneration_;
		const std::size_t indicatorId = indicatorId_;

		statusNoticeTaskId_ = mr::coprocessor::globalCoprocessor().submit(
		    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::IndicatorBlink, 0, generation,
		    "indicator-status-notice",
		    [indicatorId, generation, duration](const mr::coprocessor::TaskInfo &info,
		                                        std::stop_token stopToken) {
			    mr::coprocessor::Result result;
			    std::chrono::milliseconds elapsed(0);

			    result.task = info;
			    while (elapsed < duration) {
				    if (stopToken.stop_requested() || info.cancelRequested()) {
					    result.status = mr::coprocessor::TaskStatus::Cancelled;
					    return result;
				    }
				    std::this_thread::sleep_for(kBlinkSlice);
				    elapsed += kBlinkSlice;
			    }
			    result.status = mr::coprocessor::TaskStatus::Completed;
			    result.payload = std::make_shared<mr::coprocessor::IndicatorBlinkPayload>(
			        indicatorId, generation, false, mr::coprocessor::IndicatorBlinkChannel::StatusNotice);
			    return result;
		    });
	}

	void cancelStatusNotice(bool redraw) {
		++statusNoticeGeneration_;
		statusNoticeActive_ = false;
		statusNoticeText_.clear();
		if (statusNoticeTaskId_ != 0) {
			mr::coprocessor::globalCoprocessor().cancelTask(statusNoticeTaskId_);
			statusNoticeTaskId_ = 0;
		}
		if (redraw) {
			drawView();
			redrawFrame();
		}
	}

	void scheduleReadOnlyBlinkTick(bool nextVisible) {
		const std::size_t generation = blinkGeneration_;
		const std::size_t indicatorId = indicatorId_;
		readOnlyBlinkTaskId_ = mr::coprocessor::globalCoprocessor().submit(
		    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::IndicatorBlink, 0, generation,
		    "indicator-blink-readonly",
		    [indicatorId, generation, nextVisible](const mr::coprocessor::TaskInfo &info,
		                                           std::stop_token stopToken) {
			    mr::coprocessor::Result result;
			    result.task = info;
			    for (int step = 0; step < kBlinkSlicesPerTick; ++step) {
				    if (stopToken.stop_requested() || info.cancelRequested()) {
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
		taskBlinkTaskId_ = mr::coprocessor::globalCoprocessor().submit(
		    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::IndicatorBlink, 0, generation,
		    "indicator-blink-task",
		    [indicatorId, generation, nextVisible](const mr::coprocessor::TaskInfo &info,
		                                           std::stop_token stopToken) {
			    mr::coprocessor::Result result;
			    result.task = info;
			    for (int step = 0; step < kBlinkSlicesPerTick; ++step) {
				    if (stopToken.stop_requested() || info.cancelRequested()) {
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
				taskBlinkTaskId_ = 0;
				if (std::chrono::steady_clock::now() >= taskBlinkUntil_) {
					stopTaskBlink();
					return true;
				}
				taskBlinkVisible_ = visible;
				drawView();
				redrawFrame();
				scheduleTaskBlinkTick(!visible);
				return true;
			case mr::coprocessor::IndicatorBlinkChannel::StatusNotice:
				if (generation != statusNoticeGeneration_ || !statusNoticeActive_)
					return false;
				statusNoticeTaskId_ = 0;
				cancelStatusNotice(true);
				return true;
			case mr::coprocessor::IndicatorBlinkChannel::ReadOnly:
			default:
				if (generation != blinkGeneration_ || !readOnly_ || !readOnlyBlinkActive_)
					return false;
				readOnlyBlinkTaskId_ = 0;
				if (std::chrono::steady_clock::now() >= readOnlyBlinkUntil_) {
					stopReadOnlyBlink();
					return true;
				}
				readOnlyBlinkVisible_ = visible;
				drawView();
				redrawFrame();
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
	std::size_t statusNoticeGeneration_;
	bool statusNoticeActive_;
	std::string statusNoticeText_;
	NoticeKind statusNoticeKind_;
	TaskOverviewProvider taskOverviewProvider_;
	MRTaskOverviewPopup *taskOverviewPopup_;
	std::chrono::steady_clock::time_point readOnlyBlinkUntil_;
	std::chrono::steady_clock::time_point taskBlinkUntil_;
	std::uint64_t taskBlinkTaskId_;
	std::uint64_t readOnlyBlinkTaskId_;
	std::uint64_t statusNoticeTaskId_;
};

#endif
