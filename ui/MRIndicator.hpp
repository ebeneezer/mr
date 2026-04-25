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
	MRTaskOverviewPopup(const TRect &bounds) noexcept : TView(bounds), mLines() {
		eventMask = 0;
		options |= ofBuffered;
	}

	void setLines(const std::vector<std::string> &lines) {
		mLines = lines;
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
			else if (y - 1 < static_cast<int>(mLines.size())) {
				std::string textLine = mLines[static_cast<std::size_t>(y - 1)];
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
	std::vector<std::string> mLines;
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
	    : TIndicator(bounds), mReadOnly(false), mDisplayColumn(0), mDisplayLine(0),
	      mTaskCount(0), mTaskDisplayCount(0), mIndicatorId(allocateIndicatorId()), mBlinkGeneration(0),
	      mTaskBlinkGeneration(0), mReadOnlyBlinkActive(false), mReadOnlyBlinkVisible(false),
	      mTaskBlinkActive(false), mTaskBlinkVisible(false), mStatusNoticeGeneration(0),
	      mStatusNoticeActive(false), mStatusNoticeText(), mStatusNoticeKind(NoticeKind::Info),
	      mTaskOverviewProvider(), mTaskOverviewPopup(nullptr), mReadOnlyBlinkUntil(), mTaskBlinkUntil(),
	      mTaskBlinkTaskId(0), mReadOnlyBlinkTaskId(0), mStatusNoticeTaskId(0) {
		registerIndicator(this);
	}

	virtual ~MRIndicator() override {
		hideTaskOverview();
		cancelReadOnlyBlinkChain(false);
		cancelTaskBlinkChain(false);
		unregisterIndicator(mIndicatorId);
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
		if (mStatusNoticeActive) {
			drawStatusNotice(b, noticeStartX, color);
			noticeEndX = noticeStartX + static_cast<int>(mStatusNoticeText.size()) + 1;
		}

		cursorTextBuffer = resolvedCursorPositionMarkerText(mDisplayLine + 1UL, mDisplayColumn + 1UL);
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
		if (mDisplayColumn != column || mDisplayLine != line || modified != aModified) {
			mDisplayColumn = column;
			mDisplayLine = line;
			modified = aModified;
			location.x = short(column > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : column);
			location.y = short(line > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : line);
			drawView();
			redrawFrame();
		}
	}

	void setReadOnly(bool readOnly) {
		if (mReadOnly != readOnly) {
			bool wasReadOnly = mReadOnly;
			mReadOnly = readOnly;
			if (!wasReadOnly && mReadOnly)
				startReadOnlyBlink();
			else if (!mReadOnly)
				stopReadOnlyBlink();
			drawView();
			redrawFrame();
		}
	}

	void setTaskCount(std::size_t taskCount) {
		if (mTaskCount != taskCount) {
			std::size_t previousDisplayedCount = mTaskDisplayCount;
			mTaskCount = taskCount;
			if (mTaskCount == 0 && previousDisplayedCount == 0) {
				hideTaskOverview();
				drawView();
				return;
			}
			mTaskDisplayCount = mTaskCount != 0 ? mTaskCount : previousDisplayedCount;
			if (mTaskCount == 0)
				hideTaskOverview();
			else if (mTaskOverviewPopup != nullptr)
				showTaskOverview();
			startTaskBlink();
			redrawFrame();
		}
	}

	void setTaskOverviewProvider(TaskOverviewProvider provider) {
		mTaskOverviewProvider = std::move(provider);
	}

	void updateTaskHover(TPoint globalMouse, bool forceHide = false) {
		if (forceHide || mTaskCount == 0 || !mTaskOverviewProvider) {
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
		++mStatusNoticeGeneration;
		mStatusNoticeActive = true;
		mStatusNoticeText = text;
		mStatusNoticeKind = kind;
		drawView();
		scheduleStatusNoticeClear(duration);
	}

	bool isReadOnly() const {
		return mReadOnly;
	}

	bool shouldDrawReadOnlyMarker() const noexcept {
		return mReadOnly && (!mReadOnlyBlinkActive || mReadOnlyBlinkVisible);
	}

	bool shouldDrawTaskMarker() const noexcept {
		return mTaskDisplayCount != 0 && (!mTaskBlinkActive || mTaskBlinkVisible);
	}

	bool hasReadOnlyMarkerSlot() const noexcept {
		return mReadOnly;
	}

	bool hasTaskMarkerSlot() const noexcept {
		return mTaskDisplayCount != 0;
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
		indicatorRegistry()[indicator->mIndicatorId] = indicator;
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
		if (mTaskDisplayCount == 0 || (mTaskBlinkActive && !mTaskBlinkVisible))
			return;
		b.moveStr(5, kTaskMarkerIcon, taskMarkerColor(baseColor), 2);
	}

	int taskMarkerEndColumn() const noexcept {
		if (mTaskDisplayCount == 0 || (mTaskBlinkActive && !mTaskBlinkVisible))
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

		if (group == nullptr || !mTaskOverviewProvider)
			return;
		lines = mTaskOverviewProvider();
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

		if (mTaskOverviewPopup != nullptr) {
			group->remove(mTaskOverviewPopup);
			TObject::destroy(mTaskOverviewPopup);
			mTaskOverviewPopup = nullptr;
		}
		mTaskOverviewPopup = new MRTaskOverviewPopup(bounds);
		mTaskOverviewPopup->setLines(lines);
		group->insert(mTaskOverviewPopup);
		mTaskOverviewPopup->makeFirst();
	}

	void hideTaskOverview() {
		TGroup *group = owner;
		if (mTaskOverviewPopup == nullptr)
			return;
		if (group != nullptr)
			group->remove(mTaskOverviewPopup);
		TObject::destroy(mTaskOverviewPopup);
		mTaskOverviewPopup = nullptr;
	}

	void drawStatusNotice(TDrawBuffer &b, int x, TColorAttr baseColor) const {
		TColorAttr noticeColor = baseColor;
		std::string text;

		if (!mStatusNoticeActive || x >= size.x)
			return;

		text = " ";
		text += mStatusNoticeText;
		text += " ";
		setStyle(noticeColor, getStyle(noticeColor) | slBold);
		switch (mStatusNoticeKind) {
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
		++mBlinkGeneration;
		mReadOnlyBlinkActive = true;
		mReadOnlyBlinkVisible = true;
		mReadOnlyBlinkUntil = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		scheduleReadOnlyBlinkTick(false);
	}

	void cancelReadOnlyBlinkChain(bool redraw) {
		++mBlinkGeneration;
		mReadOnlyBlinkActive = false;
		mReadOnlyBlinkVisible = true;
		if (mReadOnlyBlinkTaskId != 0) {
			mr::coprocessor::globalCoprocessor().cancelTask(mReadOnlyBlinkTaskId);
			mReadOnlyBlinkTaskId = 0;
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
		if (mTaskBlinkTaskId != 0) {
			mr::coprocessor::globalCoprocessor().cancelTask(mTaskBlinkTaskId);
			mTaskBlinkTaskId = 0;
		}
		++mTaskBlinkGeneration;
		mTaskBlinkActive = true;
		mTaskBlinkVisible = true;
		mTaskBlinkUntil = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		drawView();
		redrawFrame();
		scheduleTaskBlinkTick(false);
	}

	void cancelTaskBlinkChain(bool redraw) {
		++mTaskBlinkGeneration;
		mTaskBlinkActive = false;
		mTaskBlinkVisible = true;
		if (mTaskBlinkTaskId != 0) {
			mr::coprocessor::globalCoprocessor().cancelTask(mTaskBlinkTaskId);
			mTaskBlinkTaskId = 0;
		}
		if (mTaskCount == 0)
			mTaskDisplayCount = 0;
		else
			mTaskDisplayCount = mTaskCount;
		if (redraw) {
			drawView();
			redrawFrame();
		}
	}

	void stopTaskBlink() {
		cancelTaskBlinkChain(true);
	}

	void scheduleStatusNoticeClear(std::chrono::milliseconds duration) {
		if (mStatusNoticeTaskId != 0) {
			mr::coprocessor::globalCoprocessor().cancelTask(mStatusNoticeTaskId);
			mStatusNoticeTaskId = 0;
		}
		const std::size_t generation = mStatusNoticeGeneration;
		const std::size_t indicatorId = mIndicatorId;

		mStatusNoticeTaskId = mr::coprocessor::globalCoprocessor().submit(
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
		++mStatusNoticeGeneration;
		mStatusNoticeActive = false;
		mStatusNoticeText.clear();
		if (mStatusNoticeTaskId != 0) {
			mr::coprocessor::globalCoprocessor().cancelTask(mStatusNoticeTaskId);
			mStatusNoticeTaskId = 0;
		}
		if (redraw) {
			drawView();
			redrawFrame();
		}
	}

	void scheduleReadOnlyBlinkTick(bool nextVisible) {
		const std::size_t generation = mBlinkGeneration;
		const std::size_t indicatorId = mIndicatorId;
		mReadOnlyBlinkTaskId = mr::coprocessor::globalCoprocessor().submit(
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
		const std::size_t generation = mTaskBlinkGeneration;
		const std::size_t indicatorId = mIndicatorId;
		mTaskBlinkTaskId = mr::coprocessor::globalCoprocessor().submit(
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
				if (generation != mTaskBlinkGeneration || !mTaskBlinkActive)
					return false;
				mTaskBlinkTaskId = 0;
				if (std::chrono::steady_clock::now() >= mTaskBlinkUntil) {
					stopTaskBlink();
					return true;
				}
				mTaskBlinkVisible = visible;
				drawView();
				redrawFrame();
				scheduleTaskBlinkTick(!visible);
				return true;
			case mr::coprocessor::IndicatorBlinkChannel::StatusNotice:
				if (generation != mStatusNoticeGeneration || !mStatusNoticeActive)
					return false;
				mStatusNoticeTaskId = 0;
				cancelStatusNotice(true);
				return true;
			case mr::coprocessor::IndicatorBlinkChannel::ReadOnly:
			default:
				if (generation != mBlinkGeneration || !mReadOnly || !mReadOnlyBlinkActive)
					return false;
				mReadOnlyBlinkTaskId = 0;
				if (std::chrono::steady_clock::now() >= mReadOnlyBlinkUntil) {
					stopReadOnlyBlink();
					return true;
				}
				mReadOnlyBlinkVisible = visible;
				drawView();
				redrawFrame();
				scheduleReadOnlyBlinkTick(!visible);
				return true;
		}
	}

	bool mReadOnly;
	unsigned long mDisplayColumn;
	unsigned long mDisplayLine;
	std::size_t mTaskCount;
	std::size_t mTaskDisplayCount;
	std::size_t mIndicatorId;
	std::size_t mBlinkGeneration;
	std::size_t mTaskBlinkGeneration;
	bool mReadOnlyBlinkActive;
	bool mReadOnlyBlinkVisible;
	bool mTaskBlinkActive;
	bool mTaskBlinkVisible;
	std::size_t mStatusNoticeGeneration;
	bool mStatusNoticeActive;
	std::string mStatusNoticeText;
	NoticeKind mStatusNoticeKind;
	TaskOverviewProvider mTaskOverviewProvider;
	MRTaskOverviewPopup *mTaskOverviewPopup;
	std::chrono::steady_clock::time_point mReadOnlyBlinkUntil;
	std::chrono::steady_clock::time_point mTaskBlinkUntil;
	std::uint64_t mTaskBlinkTaskId;
	std::uint64_t mReadOnlyBlinkTaskId;
	std::uint64_t mStatusNoticeTaskId;
};

#endif
