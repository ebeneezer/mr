#ifndef MRMENUBAR_HPP
#define MRMENUBAR_HPP
#define Uses_TMenuBar
#define Uses_TDrawBuffer
#include "MRPalette.hpp"
#include <tvision/tv.h>

#include <chrono>
#include <string>

class MRMenuBar : public TMenuBar {
  public:
	enum class MarqueeKind : unsigned char {
		Info,
		Success,
		Warning,
		Error,
		Hero
	};

	MRMenuBar(const TRect &r, TSubMenu &aMenu)
	    : TMenuBar(r, aMenu), mRightStatus(), mAutoMarqueeStatus(), mManualMarqueeStatus(),
	      mAutoMarqueeKind(MarqueeKind::Info) {
	}

	virtual void draw() override;
	void tickMarquee();
	void setPersistentBlocksMenuState(bool enabled);

	void setRightStatus(const std::string &status) {
		if (mRightStatus != status) {
			mRightStatus = status;
			drawView();
		}
	}

	const std::string &rightStatus() const noexcept {
		return mRightStatus;
	}

	void setAutoMarqueeStatus(const std::string &status, MarqueeKind kind = MarqueeKind::Info) {
		if (mAutoMarqueeStatus != status || mAutoMarqueeKind != kind) {
			mAutoMarqueeStatus = status;
			mAutoMarqueeKind = kind;
			drawView();
		}
	}

	void setManualMarqueeStatus(const std::string &status) {
		setManualMarqueeStatus(status, MarqueeKind::Info);
	}

	void setManualMarqueeStatus(const std::string &status, MarqueeKind kind) {
		if (mManualMarqueeStatus != status || mManualMarqueeKind != kind) {
			mManualMarqueeStatus = status;
			mManualMarqueeKind = kind;
			drawView();
		}
	}

	const std::string &autoMarqueeStatus() const noexcept {
		return mAutoMarqueeStatus;
	}
	const std::string &manualMarqueeStatus() const noexcept {
		return mManualMarqueeStatus;
	}

 private:
	static int marqueeVisibleSpanFor(const std::string &text, int laneWidth) noexcept {
		const int textLen = static_cast<int>(text.size());
		if (textLen <= 0 || laneWidth <= 0)
			return 0;
		return std::min(textLen, laneWidth);
	}
	static constexpr std::chrono::milliseconds marqueeScrollStepInterval() {
		return std::chrono::milliseconds(180);
	}
	static constexpr std::chrono::milliseconds marqueeIntroDuration() {
		return std::chrono::milliseconds(1000);
	}
	static constexpr std::chrono::milliseconds marqueeScrollStartDelay() {
		return std::chrono::milliseconds(3000);
	}
	void resetMarqueeState() {
		mMarqueeOffset = 0;
		mMarqueeDirection = -1;
		mMarqueeLaneWidth = 0;
		mMarqueeActiveText.clear();
		mMarqueeActiveKind = MarqueeKind::Info;
		mMarqueeHasPending = false;
		mMarqueePendingText.clear();
		mMarqueePendingKind = MarqueeKind::Info;
		mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
		mMarqueeIntroActive = false;
		mMarqueeIntroShift = 0;
		mMarqueeIntroStartShift = 0;
		mMarqueeIntroStartedAt = std::chrono::steady_clock::time_point::min();
		mMarqueeOutroActive = false;
		mMarqueeOutroShift = 0;
		mMarqueeOutroStartShift = 0;
		mMarqueeOutroStartedAt = std::chrono::steady_clock::time_point::min();
	}

	std::string mRightStatus;
	std::string mAutoMarqueeStatus;
	std::string mManualMarqueeStatus;
	MarqueeKind mManualMarqueeKind = MarqueeKind::Info;
	MarqueeKind mAutoMarqueeKind;
	int mMarqueeOffset = 0;
	int mMarqueeDirection = -1;
	int mMarqueeLaneWidth = 0;
	std::string mMarqueeActiveText;
	MarqueeKind mMarqueeActiveKind = MarqueeKind::Info;
	bool mMarqueeHasPending = false;
	std::string mMarqueePendingText;
	MarqueeKind mMarqueePendingKind = MarqueeKind::Info;
	std::chrono::steady_clock::time_point mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
	bool mMarqueeIntroActive = false;
	int mMarqueeIntroShift = 0;
	int mMarqueeIntroStartShift = 0;
	std::chrono::steady_clock::time_point mMarqueeIntroStartedAt = std::chrono::steady_clock::time_point::min();
	bool mMarqueeOutroActive = false;
	int mMarqueeOutroShift = 0;
	int mMarqueeOutroStartShift = 0;
	std::chrono::steady_clock::time_point mMarqueeOutroStartedAt = std::chrono::steady_clock::time_point::min();
};

#endif
