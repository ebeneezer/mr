#define Uses_TMenuBar
#define Uses_TDrawBuffer
#define Uses_TMenu
#define Uses_TMenuItem
#define Uses_TRect
#define Uses_TSubMenu
#include <tvision/tv.h>

#include "TMRMenuBar.hpp"

#include <algorithm>

#include "../config/MRDialogPaths.hpp"

void TMRMenuBar::tickMarquee() {
	const int textLen = static_cast<int>(marqueeActiveText_.size());
	auto now = std::chrono::steady_clock::now();
	const int visibleSpan = marqueeVisibleSpanFor(marqueeActiveText_, marqueeLaneWidth_);

	if (marqueeLaneWidth_ <= 0 || textLen == 0)
		return;
	if (marqueeOutroActive_) {
		const auto duration = marqueeIntroDuration();
		if (marqueeOutroStartedAt_ == std::chrono::steady_clock::time_point::min()) {
			marqueeOutroActive_ = false;
			marqueeOutroShift_ = 0;
		} else {
			const auto elapsed = now - marqueeOutroStartedAt_;
			if (elapsed >= duration) {
				marqueeOutroActive_ = false;
				marqueeOutroShift_ = 0;
				marqueeOutroStartShift_ = 0;
				marqueeOutroStartedAt_ = std::chrono::steady_clock::time_point::min();
				if (marqueeHasPending_) {
					marqueeActiveText_ = marqueePendingText_;
					marqueeActiveKind_ = marqueePendingKind_;
					marqueeHasPending_ = false;
					marqueePendingText_.clear();
					marqueePendingKind_ = MarqueeKind::Info;
					marqueeOffset_ =
					    std::max(0, static_cast<int>(marqueeActiveText_.size()) - marqueeLaneWidth_);
					marqueeDirection_ = -1;
					if (!marqueeActiveText_.empty()) {
						marqueeIntroActive_ = true;
						marqueeIntroStartShift_ =
						    marqueeVisibleSpanFor(marqueeActiveText_, marqueeLaneWidth_);
						marqueeIntroShift_ = marqueeIntroStartShift_;
						marqueeIntroStartedAt_ = now;
						marqueeScrollNextAt_ = std::chrono::steady_clock::time_point::min();
					} else {
						marqueeIntroActive_ = false;
						marqueeIntroShift_ = 0;
						marqueeIntroStartShift_ = 0;
						marqueeIntroStartedAt_ = std::chrono::steady_clock::time_point::min();
						marqueeScrollNextAt_ = std::chrono::steady_clock::time_point::min();
					}
				}
				drawView();
				return;
			}
			const long long durationMs = duration.count();
			const long long elapsedMs =
			    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
			int newShift = marqueeOutroStartShift_;
			if (durationMs > 0) {
				newShift += static_cast<int>(
				    (static_cast<long long>(visibleSpan - marqueeOutroStartShift_) * elapsedMs +
				     durationMs - 1) /
				    durationMs);
			}
			if (newShift > visibleSpan)
				newShift = visibleSpan;
			if (newShift != marqueeOutroShift_) {
				marqueeOutroShift_ = newShift;
				drawView();
			}
			return;
		}
	}
	if (marqueeIntroActive_) {
		const auto duration = marqueeIntroDuration();
		if (marqueeIntroStartedAt_ == std::chrono::steady_clock::time_point::min()) {
			marqueeIntroActive_ = false;
			marqueeIntroShift_ = 0;
		} else {
			const auto elapsed = now - marqueeIntroStartedAt_;
			if (elapsed >= duration) {
				bool changed = marqueeIntroShift_ != 0;
				marqueeIntroActive_ = false;
				marqueeIntroShift_ = 0;
				marqueeScrollNextAt_ =
				    textLen > marqueeLaneWidth_ ? now + marqueeScrollStartDelay()
				                                : std::chrono::steady_clock::time_point::min();
				if (changed)
					drawView();
				return;
			}
			const long long durationMs = duration.count();
			const long long elapsedMs =
			    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
			const long long remainingMs = durationMs - elapsedMs;
			int newShift = static_cast<int>(
			    (static_cast<long long>(marqueeIntroStartShift_) * remainingMs + durationMs - 1) /
			    durationMs);
			if (newShift < 0)
				newShift = 0;
			if (newShift != marqueeIntroShift_) {
				marqueeIntroShift_ = newShift;
				drawView();
			}
			return;
		}
	}
	if (textLen <= marqueeLaneWidth_)
		return;
	if (marqueeScrollNextAt_ == std::chrono::steady_clock::time_point::min()) {
		marqueeScrollNextAt_ = now + marqueeScrollStartDelay();
		return;
	}
	if (now < marqueeScrollNextAt_)
		return;

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

	marqueeScrollNextAt_ = now + marqueeScrollStepInterval();
	drawView();
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
	TColorAttr cMarquee = TColorAttr(cNormal);
	MarqueeKind targetMarqueeKind = manualMarqueeStatus_.empty() ? autoMarqueeKind_ : manualMarqueeKind_;
	int rightLen = rightStatus_.empty() ? 0 : static_cast<int>(rightStatus_.size());
	const std::string &targetText = manualMarqueeStatus_.empty() ? autoMarqueeStatus_ : manualMarqueeStatus_;
	int rightStart = size.x;
	int menuEnd = 0;

	if (rightStatusColorOverrideEnabled_)
		cStatus = rightStatusColorOverride_;
	setStyle(cStatus, getStyle(cStatus) | slBold);

	b.moveChar(0, ' ', cNormal, size.x);
	if (rightLen != 0)
		rightStart = size.x - rightLen - 1;

	// Keep one blank column between the dynamic message lane and cursor status.
	const int menuLimit = std::max(1, rightStart - 2);

	if (menu != nullptr) {
		x = 1;
		p = menu->items;
		while (p != nullptr) {
			if (p->name != nullptr) {
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
	marqueeLaneWidth_ = 0;
	if (laneStart <= laneEnd) {
		const int newLaneWidth = laneEnd - laneStart + 1;
		auto now = std::chrono::steady_clock::now();
		marqueeLaneWidth_ = newLaneWidth;
		if (targetText == marqueeActiveText_ && targetMarqueeKind == marqueeActiveKind_) {
			if (marqueeHasPending_) {
				marqueeHasPending_ = false;
				marqueePendingText_.clear();
				marqueePendingKind_ = MarqueeKind::Info;
			}
			if (marqueeOutroActive_) {
				marqueeOutroActive_ = false;
				marqueeOutroShift_ = 0;
				marqueeOutroStartShift_ = 0;
				marqueeOutroStartedAt_ = std::chrono::steady_clock::time_point::min();
				marqueeScrollNextAt_ =
				    !marqueeActiveText_.empty() &&
				            static_cast<int>(marqueeActiveText_.size()) > marqueeLaneWidth_
				        ? now + marqueeScrollStartDelay()
				        : std::chrono::steady_clock::time_point::min();
			}
		} else {
			marqueeHasPending_ = true;
			marqueePendingText_ = targetText;
			marqueePendingKind_ = targetMarqueeKind;
			// No outgoing animation when there is no currently visible text.
			// Start the incoming animation immediately.
			if (marqueeActiveText_.empty()) {
				marqueeActiveText_ = marqueePendingText_;
				marqueeActiveKind_ = marqueePendingKind_;
				marqueeHasPending_ = false;
				marqueePendingText_.clear();
				marqueePendingKind_ = MarqueeKind::Info;
				marqueeOffset_ =
				    std::max(0, static_cast<int>(marqueeActiveText_.size()) - marqueeLaneWidth_);
				marqueeDirection_ = -1;
				marqueeOutroActive_ = false;
					marqueeOutroShift_ = 0;
					marqueeOutroStartShift_ = 0;
					marqueeOutroStartedAt_ = std::chrono::steady_clock::time_point::min();
					if (!marqueeActiveText_.empty()) {
						marqueeIntroActive_ = true;
						marqueeIntroStartShift_ =
						    marqueeVisibleSpanFor(marqueeActiveText_, marqueeLaneWidth_);
						marqueeIntroShift_ = marqueeIntroStartShift_;
						marqueeIntroStartedAt_ = now;
						marqueeScrollNextAt_ = std::chrono::steady_clock::time_point::min();
					} else {
						marqueeIntroActive_ = false;
						marqueeIntroShift_ = 0;
						marqueeIntroStartShift_ = 0;
						marqueeIntroStartedAt_ = std::chrono::steady_clock::time_point::min();
						marqueeScrollNextAt_ = std::chrono::steady_clock::time_point::min();
					}
				} else if (!marqueeOutroActive_) {
					const int visibleShiftSpan =
					    marqueeVisibleSpanFor(marqueeActiveText_, marqueeLaneWidth_);
					marqueeOutroActive_ = true;
					marqueeOutroStartShift_ = marqueeIntroActive_ ? marqueeIntroShift_ : 0;
					if (marqueeOutroStartShift_ < 0)
						marqueeOutroStartShift_ = 0;
					if (marqueeOutroStartShift_ > visibleShiftSpan)
						marqueeOutroStartShift_ = visibleShiftSpan;
					marqueeOutroShift_ = marqueeOutroStartShift_;
					marqueeOutroStartedAt_ = now;
					marqueeIntroActive_ = false;
					marqueeIntroShift_ = 0;
					marqueeIntroStartShift_ = 0;
					marqueeIntroStartedAt_ = std::chrono::steady_clock::time_point::min();
					marqueeScrollNextAt_ = std::chrono::steady_clock::time_point::min();
				}
			}
		if (marqueeActiveText_.empty() && marqueeHasPending_ && !marqueeOutroActive_) {
			marqueeActiveText_ = marqueePendingText_;
			marqueeActiveKind_ = marqueePendingKind_;
			marqueeHasPending_ = false;
			marqueePendingText_.clear();
			marqueePendingKind_ = MarqueeKind::Info;
			marqueeOffset_ =
			    std::max(0, static_cast<int>(marqueeActiveText_.size()) - marqueeLaneWidth_);
			marqueeDirection_ = -1;
			if (!marqueeActiveText_.empty()) {
				marqueeIntroActive_ = true;
				marqueeIntroStartShift_ =
				    marqueeVisibleSpanFor(marqueeActiveText_, marqueeLaneWidth_);
				marqueeIntroShift_ = marqueeIntroStartShift_;
				marqueeIntroStartedAt_ = now;
				marqueeScrollNextAt_ = std::chrono::steady_clock::time_point::min();
			}
		}
		{
			const MRColorSetupSettings colors = configuredColorSetupSettings();
			unsigned char biosAttr = colors.otherColors[5]; // "message"
			unsigned char slot = kMrPaletteMessage;
			switch (marqueeActiveKind_) {
				case MarqueeKind::Warning:
					slot = kMrPaletteMessageWarning;
					biosAttr = colors.otherColors[6];
					break;
				case MarqueeKind::Error:
					slot = kMrPaletteMessageError;
					biosAttr = colors.otherColors[4];
					break;
				case MarqueeKind::Hero:
					slot = kMrPaletteMessageHero;
					biosAttr = colors.otherColors[7];
					break;
				case MarqueeKind::Success:
				case MarqueeKind::Info:
				default:
					slot = kMrPaletteMessage;
					biosAttr = colors.otherColors[5];
					break;
			}
			// Primary source required by regression guard; array value remains fallback.
			if (configuredColorSlotOverride(slot, biosAttr))
				cMarquee = TColorAttr(biosAttr);
			else
				cMarquee = TColorAttr(biosAttr);
		}
		int marqueeTextLen = static_cast<int>(marqueeActiveText_.size());
		int drawStart = laneStart;
		const char *drawPtr = marqueeActiveText_.c_str();
		int drawLen = marqueeTextLen;

		if (marqueeTextLen <= 0) {
			// no-op
		} else if (marqueeTextLen <= marqueeLaneWidth_) {
			drawStart = laneEnd - marqueeTextLen + 1;
		} else {
			const int maxOffset = marqueeTextLen - marqueeLaneWidth_;
			if (marqueeOffset_ < 0)
				marqueeOffset_ = 0;
			if (marqueeOffset_ > maxOffset)
				marqueeOffset_ = maxOffset;
			drawPtr = marqueeActiveText_.c_str() + marqueeOffset_;
			drawLen = marqueeLaneWidth_;
		}
		if (marqueeIntroActive_)
			drawStart += marqueeIntroShift_;
		else if (marqueeOutroActive_)
			drawStart += marqueeOutroShift_;
		if (drawStart <= laneEnd) {
			int visibleLen = laneEnd - drawStart + 1;
			if (visibleLen > drawLen)
				visibleLen = drawLen;
			if (visibleLen > 0)
				b.moveStr(static_cast<ushort>(drawStart), drawPtr, cMarquee,
				          static_cast<ushort>(visibleLen));
		}
	} else {
		resetMarqueeState();
	}

	if (rightLen != 0) {
		int start = rightStart;
		if (start < 1)
			start = 1;
		b.moveStr(static_cast<ushort>(start), rightStatus_.c_str(), cStatus,
		          static_cast<ushort>(std::min(rightLen, size.x - start)));
	}

	writeBuf(0, 0, size.x, 1, b);
}
