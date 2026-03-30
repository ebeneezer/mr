#ifndef TMRMENUBAR_HPP
#define TMRMENUBAR_HPP
#define Uses_TMenuBar
#define Uses_TDrawBuffer
#include "MRPalette.hpp"
#include <tvision/tv.h>

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
	    : TMenuBar(r, aMenu), rightStatus_(), heroStatus_(), heroKind_(HeroKind::Info) {
	}

	virtual void draw() override;

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
			drawView();
		}
	}

	const std::string &heroStatus() const noexcept {
		return heroStatus_;
	}

  private:
	std::string rightStatus_;
	std::string heroStatus_;
	HeroKind heroKind_;
};

#endif
