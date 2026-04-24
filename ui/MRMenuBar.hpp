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
	    : TMenuBar(r, aMenu), rightStatus_(), autoMarqueeStatus_(), manualMarqueeStatus_(),
	      autoMarqueeKind_(MarqueeKind::Info) {
	}

	virtual void draw() override;
	void tickMarquee();
	void setPersistentBlocksMenuState(bool enabled);

	void setRightStatus(const std::string &status) {
		if (rightStatus_ != status) {
			rightStatus_ = status;
			drawView();
		}
	}

	const std::string &rightStatus() const noexcept {
		return rightStatus_;
	}

	void setAutoMarqueeStatus(const std::string &status, MarqueeKind kind = MarqueeKind::Info) {
		if (autoMarqueeStatus_ != status || autoMarqueeKind_ != kind) {
			autoMarqueeStatus_ = status;
			autoMarqueeKind_ = kind;
			drawView();
		}
	}

	void setManualMarqueeStatus(const std::string &status) {
		setManualMarqueeStatus(status, MarqueeKind::Info);
	}

	void setManualMarqueeStatus(const std::string &status, MarqueeKind kind) {
		if (manualMarqueeStatus_ != status || manualMarqueeKind_ != kind) {
			manualMarqueeStatus_ = status;
			manualMarqueeKind_ = kind;
			drawView();
		}
	}

	const std::string &autoMarqueeStatus() const noexcept {
		return autoMarqueeStatus_;
	}
	const std::string &manualMarqueeStatus() const noexcept {
		return manualMarqueeStatus_;
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
		marqueeOffset_ = 0;
		marqueeDirection_ = -1;
		marqueeLaneWidth_ = 0;
		marqueeActiveText_.clear();
		marqueeActiveKind_ = MarqueeKind::Info;
		marqueeHasPending_ = false;
		marqueePendingText_.clear();
		marqueePendingKind_ = MarqueeKind::Info;
		marqueeScrollNextAt_ = std::chrono::steady_clock::time_point::min();
		marqueeIntroActive_ = false;
		marqueeIntroShift_ = 0;
		marqueeIntroStartShift_ = 0;
		marqueeIntroStartedAt_ = std::chrono::steady_clock::time_point::min();
		marqueeOutroActive_ = false;
		marqueeOutroShift_ = 0;
		marqueeOutroStartShift_ = 0;
		marqueeOutroStartedAt_ = std::chrono::steady_clock::time_point::min();
	}

	std::string rightStatus_;
	std::string autoMarqueeStatus_;
	std::string manualMarqueeStatus_;
	MarqueeKind manualMarqueeKind_ = MarqueeKind::Info;
	MarqueeKind autoMarqueeKind_;
	int marqueeOffset_ = 0;
	int marqueeDirection_ = -1;
	int marqueeLaneWidth_ = 0;
	std::string marqueeActiveText_;
	MarqueeKind marqueeActiveKind_ = MarqueeKind::Info;
	bool marqueeHasPending_ = false;
	std::string marqueePendingText_;
	MarqueeKind marqueePendingKind_ = MarqueeKind::Info;
	std::chrono::steady_clock::time_point marqueeScrollNextAt_ = std::chrono::steady_clock::time_point::min();
	bool marqueeIntroActive_ = false;
	int marqueeIntroShift_ = 0;
	int marqueeIntroStartShift_ = 0;
	std::chrono::steady_clock::time_point marqueeIntroStartedAt_ = std::chrono::steady_clock::time_point::min();
	bool marqueeOutroActive_ = false;
	int marqueeOutroShift_ = 0;
	int marqueeOutroStartShift_ = 0;
	std::chrono::steady_clock::time_point marqueeOutroStartedAt_ = std::chrono::steady_clock::time_point::min();
};

#endif
