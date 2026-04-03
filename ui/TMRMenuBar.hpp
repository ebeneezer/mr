#ifndef TMRMENUBAR_HPP
#define TMRMENUBAR_HPP
#define Uses_TMenuBar
#define Uses_TDrawBuffer
#include "MRPalette.hpp"
#include <tvision/tv.h>

#include <chrono>
#include <string>

class TMRMenuBar : public TMenuBar {
  public:
	enum class HeroKind : unsigned char {
		Info,
		Success,
		Warning,
		Error
	};

	TMRMenuBar(const TRect &r, TSubMenu &aMenu)
	    : TMenuBar(r, aMenu), rightStatus_(), heroStatus_(), manualMarqueeStatus_(),
	      heroKind_(HeroKind::Info) {
		activeMenuBar_ = this;
	}

	~TMRMenuBar() override {
		if (activeMenuBar_ == this)
			activeMenuBar_ = nullptr;
	}

	virtual void draw() override;
	void tickMarquee();
	static bool applyMarqueeTick(std::size_t generation);

	void setRightStatus(const std::string &status) {
		if (rightStatus_ != status) {
			rightStatus_ = status;
			drawView();
		}
	}

	const std::string &rightStatus() const noexcept {
		return rightStatus_;
	}

	void setHeroStatus(const std::string &status, HeroKind kind = HeroKind::Info) {
		if (heroStatus_ != status || heroKind_ != kind) {
			heroStatus_ = status;
			heroKind_ = kind;
			resetMarqueeState();
			drawView();
		}
	}

	void setManualMarqueeStatus(const std::string &status) {
		setManualMarqueeStatus(status, HeroKind::Info);
	}

	void setManualMarqueeStatus(const std::string &status, HeroKind kind) {
		if (manualMarqueeStatus_ != status || manualMarqueeKind_ != kind) {
			manualMarqueeStatus_ = status;
			manualMarqueeKind_ = kind;
			resetMarqueeState();
			drawView();
		}
	}

	const std::string &heroStatus() const noexcept {
		return heroStatus_;
	}
	const std::string &manualMarqueeStatus() const noexcept {
		return manualMarqueeStatus_;
	}

  private:
	static constexpr std::chrono::milliseconds heroScrollStepInterval() {
		return std::chrono::milliseconds(180);
	}
	static constexpr std::chrono::milliseconds heroScrollStartDelay() {
		return std::chrono::milliseconds(3000);
	}
	void resetMarqueeState() {
		++marqueeGeneration_;
		marqueeTaskScheduled_ = false;
		marqueeOffset_ = 0;
		marqueeDirection_ = -1;
		marqueeLaneWidth_ = 0;
		marqueeActiveText_.clear();
		marqueeScrollNextAt_ = std::chrono::steady_clock::time_point::min();
	}
	void scheduleMarqueeTickIfNeeded();
	bool applyMarqueeTickImpl(std::size_t generation);

	std::string rightStatus_;
	std::string heroStatus_;
	std::string manualMarqueeStatus_;
	HeroKind manualMarqueeKind_ = HeroKind::Info;
	HeroKind heroKind_;
	std::size_t marqueeGeneration_ = 1;
	bool marqueeTaskScheduled_ = false;
	int marqueeOffset_ = 0;
	int marqueeDirection_ = -1;
	int marqueeLaneWidth_ = 0;
	std::string marqueeActiveText_;
	std::chrono::steady_clock::time_point marqueeScrollNextAt_ = std::chrono::steady_clock::time_point::min();
	static TMRMenuBar *activeMenuBar_;
};

#endif
