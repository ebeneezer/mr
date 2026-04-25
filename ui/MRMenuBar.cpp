#define Uses_TMenuBar
#define Uses_TDrawBuffer
#define Uses_TMenu
#define Uses_TMenuItem
#define Uses_TRect
#define Uses_TSubMenu
#include <tvision/tv.h>

#include "MRMenuBar.hpp"

#include <algorithm>
#include <string>

#include "../app/MRCommands.hpp"
#include "../config/MRDialogPaths.hpp"

void mrvmUiInvalidateScreenBase() noexcept;

namespace {
TMenuItem *findMenuItemByCommand(TMenu *menu, ushort command) {
	for (TMenuItem *item = menu != nullptr ? menu->items : nullptr; item != nullptr; item = item->next) {
		if (item->command == command)
			return item;
		if (item->command == 0) {
			TMenuItem *match = findMenuItemByCommand(item->subMenu, command);
			if (match != nullptr)
				return match;
		}
	}
	return nullptr;
}
} // namespace

void MRMenuBar::setPersistentBlocksMenuState(bool enabled) {
	const std::string wantedLabel = enabled ? "~P~ersistent blocks [ON]" : "~P~ersistent blocks [OFF]";
	TMenuItem *item = findMenuItemByCommand(menu, cmMrBlockPersistent);

	if (item == nullptr || item->command != cmMrBlockPersistent)
		return;
	if (item->name != nullptr && wantedLabel == item->name)
		return;
	delete[] const_cast<char *>(item->name);
	item->name = newStr(wantedLabel.c_str());
	drawView();
}

void MRMenuBar::tickMarquee() {
	const int textLen = static_cast<int>(mMarqueeActiveText.size());
	auto now = std::chrono::steady_clock::now();
	const int visibleSpan = marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);

	if (mMarqueeLaneWidth <= 0 || textLen == 0)
		return;
	if (mMarqueeOutroActive) {
		const auto duration = marqueeIntroDuration();
		if (mMarqueeOutroStartedAt == std::chrono::steady_clock::time_point::min()) {
			mMarqueeOutroActive = false;
			mMarqueeOutroShift = 0;
		} else {
			const auto elapsed = now - mMarqueeOutroStartedAt;
			if (elapsed >= duration) {
				mMarqueeOutroActive = false;
				mMarqueeOutroShift = 0;
				mMarqueeOutroStartShift = 0;
				mMarqueeOutroStartedAt = std::chrono::steady_clock::time_point::min();
				if (mMarqueeHasPending) {
					mMarqueeActiveText = mMarqueePendingText;
					mMarqueeActiveKind = mMarqueePendingKind;
					mMarqueeHasPending = false;
					mMarqueePendingText.clear();
					mMarqueePendingKind = MarqueeKind::Info;
					mMarqueeOffset =
					    std::max(0, static_cast<int>(mMarqueeActiveText.size()) - mMarqueeLaneWidth);
					mMarqueeDirection = -1;
					if (!mMarqueeActiveText.empty()) {
						mMarqueeIntroActive = true;
						mMarqueeIntroStartShift =
						    marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);
						mMarqueeIntroShift = mMarqueeIntroStartShift;
						mMarqueeIntroStartedAt = now;
						mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
					} else {
						mMarqueeIntroActive = false;
						mMarqueeIntroShift = 0;
						mMarqueeIntroStartShift = 0;
						mMarqueeIntroStartedAt = std::chrono::steady_clock::time_point::min();
						mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
					}
				}
				drawView();
				return;
			}
			const long long durationMs = duration.count();
			const long long elapsedMs =
			    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
			int newShift = mMarqueeOutroStartShift;
			if (durationMs > 0) {
				newShift += static_cast<int>(
				    (static_cast<long long>(visibleSpan - mMarqueeOutroStartShift) * elapsedMs +
				     durationMs - 1) /
				    durationMs);
			}
			if (newShift > visibleSpan)
				newShift = visibleSpan;
			if (newShift != mMarqueeOutroShift) {
				mMarqueeOutroShift = newShift;
				drawView();
			}
			return;
		}
	}
	if (mMarqueeIntroActive) {
		const auto duration = marqueeIntroDuration();
		if (mMarqueeIntroStartedAt == std::chrono::steady_clock::time_point::min()) {
			mMarqueeIntroActive = false;
			mMarqueeIntroShift = 0;
		} else {
			const auto elapsed = now - mMarqueeIntroStartedAt;
			if (elapsed >= duration) {
				bool changed = mMarqueeIntroShift != 0;
				mMarqueeIntroActive = false;
				mMarqueeIntroShift = 0;
				mMarqueeScrollNextAt =
				    textLen > mMarqueeLaneWidth ? now + marqueeScrollStartDelay()
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
			    (static_cast<long long>(mMarqueeIntroStartShift) * remainingMs + durationMs - 1) /
			    durationMs);
			if (newShift < 0)
				newShift = 0;
			if (newShift != mMarqueeIntroShift) {
				mMarqueeIntroShift = newShift;
				drawView();
			}
			return;
		}
	}
	if (textLen <= mMarqueeLaneWidth)
		return;
	if (mMarqueeScrollNextAt == std::chrono::steady_clock::time_point::min()) {
		mMarqueeScrollNextAt = now + marqueeScrollStartDelay();
		return;
	}
	if (now < mMarqueeScrollNextAt)
		return;

	const int maxOffset = textLen - mMarqueeLaneWidth;
	if (mMarqueeDirection >= 0) {
		if (mMarqueeOffset < maxOffset)
			++mMarqueeOffset;
		else
			mMarqueeDirection = -1;
	} else {
		if (mMarqueeOffset > 0)
			--mMarqueeOffset;
		else
			mMarqueeDirection = 1;
	}

	mMarqueeScrollNextAt = now + marqueeScrollStepInterval();
	drawView();
}

void MRMenuBar::draw() {
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
	MarqueeKind targetMarqueeKind = mManualMarqueeStatus.empty() ? mAutoMarqueeKind : mManualMarqueeKind;
	int rightLen = mRightStatus.empty() ? 0 : static_cast<int>(mRightStatus.size());
	const std::string &targetText = mManualMarqueeStatus.empty() ? mAutoMarqueeStatus : mManualMarqueeStatus;
	int rightStart = size.x;
	int menuEnd = 0;

	{
		const MRColorSetupSettings colors = configuredColorSetupSettings();
		unsigned char biosAttr = colors.otherColors[8];
		(void)configuredColorSlotOverride(kMrPaletteCursorPositionMarker, biosAttr);
		cStatus = TColorAttr(biosAttr);
	}
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
	mMarqueeLaneWidth = 0;
	if (laneStart <= laneEnd) {
		const int newLaneWidth = laneEnd - laneStart + 1;
		auto now = std::chrono::steady_clock::now();
		mMarqueeLaneWidth = newLaneWidth;
		if (targetText == mMarqueeActiveText && targetMarqueeKind == mMarqueeActiveKind) {
			if (mMarqueeHasPending) {
				mMarqueeHasPending = false;
				mMarqueePendingText.clear();
				mMarqueePendingKind = MarqueeKind::Info;
			}
			if (mMarqueeOutroActive) {
				mMarqueeOutroActive = false;
				mMarqueeOutroShift = 0;
				mMarqueeOutroStartShift = 0;
				mMarqueeOutroStartedAt = std::chrono::steady_clock::time_point::min();
				mMarqueeScrollNextAt =
				    !mMarqueeActiveText.empty() &&
				            static_cast<int>(mMarqueeActiveText.size()) > mMarqueeLaneWidth
				        ? now + marqueeScrollStartDelay()
				        : std::chrono::steady_clock::time_point::min();
			}
		} else {
			mMarqueeHasPending = true;
			mMarqueePendingText = targetText;
			mMarqueePendingKind = targetMarqueeKind;
			// No outgoing animation when there is no currently visible text.
			// Start the incoming animation immediately.
			if (mMarqueeActiveText.empty()) {
				mMarqueeActiveText = mMarqueePendingText;
				mMarqueeActiveKind = mMarqueePendingKind;
				mMarqueeHasPending = false;
				mMarqueePendingText.clear();
				mMarqueePendingKind = MarqueeKind::Info;
				mMarqueeOffset =
				    std::max(0, static_cast<int>(mMarqueeActiveText.size()) - mMarqueeLaneWidth);
				mMarqueeDirection = -1;
				mMarqueeOutroActive = false;
					mMarqueeOutroShift = 0;
					mMarqueeOutroStartShift = 0;
					mMarqueeOutroStartedAt = std::chrono::steady_clock::time_point::min();
					if (!mMarqueeActiveText.empty()) {
						mMarqueeIntroActive = true;
						mMarqueeIntroStartShift =
						    marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);
						mMarqueeIntroShift = mMarqueeIntroStartShift;
						mMarqueeIntroStartedAt = now;
						mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
					} else {
						mMarqueeIntroActive = false;
						mMarqueeIntroShift = 0;
						mMarqueeIntroStartShift = 0;
						mMarqueeIntroStartedAt = std::chrono::steady_clock::time_point::min();
						mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
					}
				} else if (!mMarqueeOutroActive) {
					const int visibleShiftSpan =
					    marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);
					mMarqueeOutroActive = true;
					mMarqueeOutroStartShift = mMarqueeIntroActive ? mMarqueeIntroShift : 0;
					if (mMarqueeOutroStartShift < 0)
						mMarqueeOutroStartShift = 0;
					if (mMarqueeOutroStartShift > visibleShiftSpan)
						mMarqueeOutroStartShift = visibleShiftSpan;
					mMarqueeOutroShift = mMarqueeOutroStartShift;
					mMarqueeOutroStartedAt = now;
					mMarqueeIntroActive = false;
					mMarqueeIntroShift = 0;
					mMarqueeIntroStartShift = 0;
					mMarqueeIntroStartedAt = std::chrono::steady_clock::time_point::min();
					mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
				}
			}
		if (mMarqueeActiveText.empty() && mMarqueeHasPending && !mMarqueeOutroActive) {
			mMarqueeActiveText = mMarqueePendingText;
			mMarqueeActiveKind = mMarqueePendingKind;
			mMarqueeHasPending = false;
			mMarqueePendingText.clear();
			mMarqueePendingKind = MarqueeKind::Info;
			mMarqueeOffset =
			    std::max(0, static_cast<int>(mMarqueeActiveText.size()) - mMarqueeLaneWidth);
			mMarqueeDirection = -1;
			if (!mMarqueeActiveText.empty()) {
				mMarqueeIntroActive = true;
				mMarqueeIntroStartShift =
				    marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);
				mMarqueeIntroShift = mMarqueeIntroStartShift;
				mMarqueeIntroStartedAt = now;
				mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
			}
		}
		{
			const MRColorSetupSettings colors = configuredColorSetupSettings();
			unsigned char biosAttr = colors.otherColors[5]; // "message"
			unsigned char slot = kMrPaletteMessage;
			switch (mMarqueeActiveKind) {
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
		int marqueeTextLen = static_cast<int>(mMarqueeActiveText.size());
		int drawStart = laneStart;
		const char *drawPtr = mMarqueeActiveText.c_str();
		int drawLen = marqueeTextLen;

		if (marqueeTextLen <= 0) {
			// no-op
		} else if (marqueeTextLen <= mMarqueeLaneWidth) {
			drawStart = laneEnd - marqueeTextLen + 1;
		} else {
			const int maxOffset = marqueeTextLen - mMarqueeLaneWidth;
			if (mMarqueeOffset < 0)
				mMarqueeOffset = 0;
			if (mMarqueeOffset > maxOffset)
				mMarqueeOffset = maxOffset;
			drawPtr = mMarqueeActiveText.c_str() + mMarqueeOffset;
			drawLen = mMarqueeLaneWidth;
		}
		if (mMarqueeIntroActive)
			drawStart += mMarqueeIntroShift;
		else if (mMarqueeOutroActive)
			drawStart += mMarqueeOutroShift;
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
		b.moveStr(static_cast<ushort>(start), mRightStatus.c_str(), cStatus,
		          static_cast<ushort>(std::min(rightLen, size.x - start)));
	}

	writeBuf(0, 0, size.x, 1, b);
	mrvmUiInvalidateScreenBase();
}
