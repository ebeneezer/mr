#define Uses_TMenuBar
#define Uses_TDrawBuffer
#include <tvision/tv.h>

#include "MRMenuBar.hpp"
#include "MRMessageLineController.hpp"

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
				if (mMarqueeHasPending) {
					mMarqueeActiveText = mMarqueePendingText;
					mMarqueeActiveSegments = mMarqueePendingSegments;
					mMarqueeActiveKind = mMarqueePendingKind;
					mMarqueeHasPending = false;
					mMarqueePendingText.clear();
					mMarqueePendingSegments.clear();
					mMarqueePendingKind = MarqueeKind::Info;
					mMarqueeOffset = std::max(0, static_cast<int>(mMarqueeActiveText.size()) - mMarqueeLaneWidth);
					mMarqueeDirection = -1;
					if (!mMarqueeActiveText.empty()) {
						mMarqueeIntroActive = true;
						mMarqueeIntroStartShift = marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);
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
		} else {
			const auto elapsed = now - mMarqueeIntroStartedAt;
			if (elapsed >= duration) {
				bool changed = mMarqueeIntroShift != 0;
				mMarqueeIntroActive = false;
				mMarqueeIntroShift = 0;
				mMarqueeScrollNextAt = textLen > mMarqueeLaneWidth ? now + marqueeScrollStartDelay() : std::chrono::steady_clock::time_point::min();
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
