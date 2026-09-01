#define Uses_TMenuBar
#define Uses_TDrawBuffer
#include <tvision/tv.h>

#include "MRMenuBar.hpp"
#include "MRMenuBarDrawingInternal.hpp"
#include "MRMessageLineController.hpp"
#include "widgets/MRNumericSlider.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

void MRMenuBar::setAutoMarqueeStatusSegments(const std::vector<MarqueeSegment> &segments, MarqueeKind kind) {
	std::string status;

	for (const MarqueeSegment &segment : segments)
		status += segment.text;
	if (mAutoMarqueeStatus != status || mAutoMarqueeKind != kind || mAutoMarqueeSegments != segments) {
		mAutoMarqueeStatus = status;
		mAutoMarqueeKind = kind;
		mAutoMarqueeSegments = segments;
		drawView();
	}
}

void MRMenuBar::setStaticProgressMode(bool active) {
	static_cast<void>(active);
	resetMarqueeState();
	drawView();
}

void MRMenuBar::setFullscreenPresentation(bool active) {
	if (mFullscreenPresentation == active) return;
	mFullscreenPresentation = active;
	resetMarqueeState();
	drawView();
}

void MRMenuBar::drawStaticProgress(TDrawBuffer &buffer, int laneStart, int laneWidth, std::size_t completed, std::size_t total, TColorAttr normalColor) {
	const TColorAttr warningAttribute = mr_menu_drawing::resolvedPaletteAttribute(mr_menu_drawing::marqueePaletteSlot(MarqueeKind::Warning), mr_menu_drawing::marqueeFallbackAttribute(MarqueeKind::Warning));
	const TColorAttr warningColor = reverseAttribute(warningAttribute);
	const std::string label = std::to_string(completed) + "/" + std::to_string(total);

	MRProgressSlider::drawProgress(buffer, laneStart, laneWidth, completed, total, label, normalColor, warningColor, MRProgressSlider::Direction::RightToLeft);
}

void MRMenuBar::activatePendingMarquee(std::chrono::steady_clock::time_point now) {
	mMarqueeActiveText = mMarqueePendingText;
	mMarqueeActiveSegments = mMarqueePendingSegments;
	mMarqueeActiveKind = mMarqueePendingKind;
	mMarqueeHasPending = false;
	mMarqueePendingText.clear();
	mMarqueePendingSegments.clear();
	mMarqueePendingKind = MarqueeKind::Info;
	mMarqueeOffset = std::max(0, static_cast<int>(mMarqueeActiveText.size()) - mMarqueeLaneWidth);
	mMarqueeDirection = -1;
	mMarqueeOutroActive = false;
	mMarqueeOutroShift = 0;
	mMarqueeOutroStartShift = 0;
	mMarqueeOutroStartedAt = std::chrono::steady_clock::time_point::min();
	if (!mMarqueeActiveText.empty()) {
		mMarqueeIntroActive = true;
		mMarqueeIntroStartShift = marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);
		mMarqueeIntroShift = mMarqueeIntroStartShift;
		mMarqueeIntroStartedAt = now;
		mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
		mMarqueeStableUntil = std::chrono::steady_clock::time_point::max();
	} else {
		mMarqueeIntroActive = false;
		mMarqueeIntroShift = 0;
		mMarqueeIntroStartShift = 0;
		mMarqueeIntroStartedAt = std::chrono::steady_clock::time_point::min();
		mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
		mMarqueeStableUntil = std::chrono::steady_clock::time_point::min();
	}
}

void MRMenuBar::beginMarqueeOutro(std::chrono::steady_clock::time_point now) {
	mMarqueeOutroActive = true;
	mMarqueeOutroStartShift = 0;
	mMarqueeOutroShift = 0;
	mMarqueeOutroStartedAt = now;
	mMarqueeIntroActive = false;
	mMarqueeIntroShift = 0;
	mMarqueeIntroStartShift = 0;
	mMarqueeIntroStartedAt = std::chrono::steady_clock::time_point::min();
	mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
	mMarqueeStableUntil = std::chrono::steady_clock::time_point::min();
}

void MRMenuBar::setStartupFunctionKeysActive(bool active) {
	const bool editorActive = active ? false : mEditorFunctionKeysActive;
	if (mStartupFunctionKeysActive == active && mEditorFunctionKeysActive == editorActive) return;
	mStartupFunctionKeysActive = active;
	mEditorFunctionKeysActive = editorActive;
	applyFunctionKeyMenuShortcuts(mBaseMenu);
	applyFunctionKeyMenuShortcuts(menu);
	drawView();
}

void MRMenuBar::setEditorFunctionKeysActive(bool active) {
	const bool startupActive = active ? false : mStartupFunctionKeysActive;
	if (mEditorFunctionKeysActive == active && mStartupFunctionKeysActive == startupActive) return;
	mEditorFunctionKeysActive = active;
	mStartupFunctionKeysActive = startupActive;
	applyFunctionKeyMenuShortcuts(mBaseMenu);
	applyFunctionKeyMenuShortcuts(menu);
	drawView();
}

void MRMenuBar::setDebuggerFunctionKeysActive(bool active) {
	if (mDebuggerFunctionKeysActive == active) return;
	mDebuggerFunctionKeysActive = active;
	applyFunctionKeyMenuShortcuts(mBaseMenu);
	applyFunctionKeyMenuShortcuts(menu);
	drawView();
}

void MRMenuBar::tickMarquee() {
	const int textLen = static_cast<int>(mMarqueeActiveText.size());
	auto now = std::chrono::steady_clock::now();
	const int visibleSpan = marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);

	if (mr::messageline::staticModeActive()) return;
	if (mMarqueeLaneWidth <= 0 || textLen == 0) return;
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
				if (mMarqueeHasPending) activatePendingMarquee(now);
				drawView();
				return;
			}
			const long long durationMs = duration.count();
			const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
			int newShift = mMarqueeOutroStartShift;
			if (durationMs > 0) {
				newShift += static_cast<int>((static_cast<long long>(visibleSpan - mMarqueeOutroStartShift) * elapsedMs + durationMs - 1) / durationMs);
			}
			if (newShift > visibleSpan) newShift = visibleSpan;
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
			mMarqueeStableUntil = now + marqueeMinimumStableDuration();
		} else {
			const auto elapsed = now - mMarqueeIntroStartedAt;
			if (elapsed >= duration) {
				bool changed = mMarqueeIntroShift != 0;

				mMarqueeIntroActive = false;
				mMarqueeIntroShift = 0;
				mMarqueeScrollNextAt = textLen > mMarqueeLaneWidth ? now + marqueeScrollStartDelay() : std::chrono::steady_clock::time_point::min();
				mMarqueeStableUntil = now + marqueeMinimumStableDuration();
				if (changed) drawView();
				return;
			}
			const long long durationMs = duration.count();
			const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
			const long long remainingMs = durationMs - elapsedMs;
			int newShift = static_cast<int>((static_cast<long long>(mMarqueeIntroStartShift) * remainingMs + durationMs - 1) / durationMs);
			if (newShift < 0) newShift = 0;
			if (newShift != mMarqueeIntroShift) {
				mMarqueeIntroShift = newShift;
				drawView();
			}
			return;
		}
	}
	if (mMarqueeHasPending && now >= mMarqueeStableUntil) {
		beginMarqueeOutro(now);
		drawView();
		return;
	}
	if (textLen <= mMarqueeLaneWidth) return;
	if (mMarqueeScrollNextAt == std::chrono::steady_clock::time_point::min()) {
		mMarqueeScrollNextAt = now + marqueeScrollStartDelay();
		return;
	}
	if (now < mMarqueeScrollNextAt) return;

	const int maxOffset = textLen - mMarqueeLaneWidth;
	if (mMarqueeDirection >= 0) {
		if (mMarqueeOffset < maxOffset) ++mMarqueeOffset;
		else
			mMarqueeDirection = -1;
	} else {
		if (mMarqueeOffset > 0) --mMarqueeOffset;
		else
			mMarqueeDirection = 1;
	}

	mMarqueeScrollNextAt = now + marqueeScrollStepInterval();
	drawView();
}
