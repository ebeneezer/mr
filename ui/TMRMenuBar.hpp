#ifndef TMRMENUBAR_HPP
#define TMRMENUBAR_HPP
#define Uses_TMenuBar
#include "mrpalette.hpp"
#include <tvision/tv.h>

class TMRMenuBar : public TMenuBar {
  public:
	TMRMenuBar(const TRect &r, TSubMenu &aMenu) : TMenuBar(r, aMenu) {
	}
};

#endif
