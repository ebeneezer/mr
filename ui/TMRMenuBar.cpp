#define Uses_TMenuBar
#define Uses_TDrawBuffer
#define Uses_TMenu
#define Uses_TMenuItem
#define Uses_TRect
#define Uses_TSubMenu
#include <tvision/tv.h>

#include "TMRMenuBar.hpp"

#include <algorithm>
#include <thread>

#include "../coprocessor/MRCoprocessor.hpp"
#include "../config/MRDialogPaths.hpp"

TMRMenuBar *TMRMenuBar::activeMenuBar_ = nullptr;

void TMRMenuBar::tickMarquee() {
	scheduleMarqueeTickIfNeeded();
}

void TMRMenuBar::scheduleMarqueeTickIfNeeded() {
	const std::string &activeText = manualMarqueeStatus_.empty() ? heroStatus_ : manualMarqueeStatus_;
	const int textLen = static_cast<int>(activeText.size());

	if (activeText.empty() || marqueeLaneWidth_ <= 0 || textLen <= marqueeLaneWidth_) {
		marqueeTaskScheduled_ = false;
		return;
	}
	if (marqueeTaskScheduled_)
		return;

	const std::size_t generation = marqueeGeneration_;
	auto now = std::chrono::steady_clock::now();
	auto dueAt = marqueeScrollNextAt_;
	if (dueAt == std::chrono::steady_clock::time_point::min())
		dueAt = now + heroScrollStartDelay();
	auto delay = dueAt > now ? std::chrono::duration_cast<std::chrono::milliseconds>(dueAt - now)
	                         : std::chrono::milliseconds(0);

	marqueeTaskScheduled_ = true;
	mr::coprocessor::globalCoprocessor().submit(
	    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::Custom, 0, generation,
	    "menu-marquee-tick", [generation, delay](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
		    mr::coprocessor::Result result;
		    result.task = info;
		    std::chrono::milliseconds elapsed(0);
		    static constexpr std::chrono::milliseconds kSlice(25);

		    while (elapsed < delay) {
			    if (stopToken.stop_requested()) {
				    result.status = mr::coprocessor::TaskStatus::Cancelled;
				    return result;
			    }
			    std::this_thread::sleep_for(kSlice);
			    elapsed += kSlice;
		    }
		    result.status = mr::coprocessor::TaskStatus::Completed;
		    result.payload = std::make_shared<mr::coprocessor::MarqueeTickPayload>(generation);
		    return result;
	    });
}

bool TMRMenuBar::applyMarqueeTick(std::size_t generation) {
	if (activeMenuBar_ == nullptr)
		return false;
	return activeMenuBar_->applyMarqueeTickImpl(generation);
}

bool TMRMenuBar::applyMarqueeTickImpl(std::size_t generation) {
	marqueeTaskScheduled_ = false;
	if (generation != marqueeGeneration_)
		return false;

	const std::string &activeText = manualMarqueeStatus_.empty() ? heroStatus_ : manualMarqueeStatus_;
	const int textLen = static_cast<int>(activeText.size());
	if (activeText.empty() || marqueeLaneWidth_ <= 0 || textLen <= marqueeLaneWidth_)
		return false;

	const int maxOffset = textLen - marqueeLaneWidth_;
	if (marqueeDirection_ >= 0) {
		if (marqueeOffset_ < maxOffset)
			++marqueeOffset_;
		else
			marqueeDirection_ = -1;
	} else {
		if (marqueeOffset_ > 0)
			--marqueeOffset_;
		else
			marqueeDirection_ = 1;
	}

	marqueeScrollNextAt_ = std::chrono::steady_clock::now() + heroScrollStepInterval();
	drawView();
	scheduleMarqueeTickIfNeeded();
	return true;
}

void TMRMenuBar::draw() {
	TAttrPair color;
	short x, l;
	TMenuItem *p;
	TDrawBuffer b;

	TAttrPair cNormal = getColor(0x0301);
	TAttrPair cSelect = getColor(0x0604);
	TAttrPair cNormDisabled = getColor(0x0202);
	TAttrPair cSelDisabled = getColor(0x0505);
	TColorAttr cStatus = TColorAttr(cNormal);
	TColorAttr cHero = TColorAttr(cNormal);
	HeroKind activeHeroKind = manualMarqueeStatus_.empty() ? heroKind_ : manualMarqueeKind_;
	int rightLen = rightStatus_.empty() ? 0 : static_cast<int>(rightStatus_.size());
	const std::string &activeText = manualMarqueeStatus_.empty() ? heroStatus_ : manualMarqueeStatus_;
	int heroLen = activeText.empty() ? 0 : static_cast<int>(activeText.size());
	int rightStart = size.x;
	int menuEnd = 0;

	setStyle(cStatus, getStyle(cStatus) | slBold);
	setStyle(cHero, getStyle(cHero) | slBold);
	{
		unsigned char biosAttr = 0;
		unsigned char slot = 43; // "message"
		switch (activeHeroKind) {
			case HeroKind::Warning:
				slot = 44; // "warning message"
				break;
			case HeroKind::Error:
				slot = 42; // "error message"
				break;
			case HeroKind::Success:
			case HeroKind::Info:
			default:
				slot = 43; // "message"
				break;
		}
		if (configuredColorSlotOverride(slot, biosAttr))
			cHero = TColorAttr(biosAttr);
	}
	setStyle(cHero, getStyle(cHero) | slBold);

	b.moveChar(0, ' ', cNormal, size.x);
	if (rightLen != 0)
		rightStart = size.x - rightLen - 1;

	// Keep one blank column between the dynamic message lane and cursor status.
	const int menuLimit = std::max(1, rightStart - 2);

	if (menu != 0) {
		x = 1;
		p = menu->items;
		while (p != 0) {
			if (p->name != 0) {
				l = cstrlen(p->name);
				if (x + l < menuLimit) {
					if (p->disabled)
						if (p == current)
							color = cSelDisabled;
						else
							color = cNormDisabled;
					else if (p == current)
						color = cSelect;
					else
						color = cNormal;

					b.moveChar(x, ' ', color, 1);
					b.moveCStr(x + 1, p->name, color);
					b.moveChar(x + l + 1, ' ', color, 1);
					menuEnd = x + l + 1;
				}
				x += l + 2;
			}
			p = p->next;
		}
	}

	// Dynamic top-line message lane between left menus and right cursor status.
	// We keep one blank before right status and render a marquee when text is wider than lane.
	int laneStart = std::max(1, menuEnd + 1);
	int laneEnd = rightStart - 2;
	const int previousLaneWidth = marqueeLaneWidth_;
	marqueeLaneWidth_ = 0;
	if (heroLen != 0 && laneStart <= laneEnd) {
		const int newLaneWidth = laneEnd - laneStart + 1;
		marqueeLaneWidth_ = newLaneWidth;
		if (activeText != marqueeActiveText_ || previousLaneWidth != newLaneWidth) {
			marqueeActiveText_ = activeText;
			marqueeOffset_ = std::max(0, heroLen - marqueeLaneWidth_);
			marqueeDirection_ = -1;
			marqueeScrollNextAt_ = std::chrono::steady_clock::now() + heroScrollStartDelay();
			++marqueeGeneration_;
			marqueeTaskScheduled_ = false;
		}
		if (heroLen <= marqueeLaneWidth_) {
			marqueeOffset_ = 0;
			const int alignedStart = laneEnd - heroLen + 1;
			b.moveStr(static_cast<ushort>(alignedStart), activeText.c_str(), cHero,
			          static_cast<ushort>(std::min(heroLen, marqueeLaneWidth_)));
		} else {
			const int maxOffset = heroLen - marqueeLaneWidth_;
			if (marqueeOffset_ < 0)
				marqueeOffset_ = 0;
			if (marqueeOffset_ > maxOffset)
				marqueeOffset_ = maxOffset;
			const char *startPtr = activeText.c_str() + marqueeOffset_;
			b.moveStr(static_cast<ushort>(laneStart), startPtr, cHero, static_cast<ushort>(marqueeLaneWidth_));
		}
	} else {
		marqueeLaneWidth_ = 0;
		marqueeTaskScheduled_ = false;
	}

	if (rightLen != 0) {
		int start = rightStart;
		if (start < 1)
			start = 1;
		b.moveStr(static_cast<ushort>(start), rightStatus_.c_str(), cStatus,
		          static_cast<ushort>(std::min(rightLen, size.x - start)));
	}

	writeBuf(0, 0, size.x, 1, b);
	scheduleMarqueeTickIfNeeded();
}
