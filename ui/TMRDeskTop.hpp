#ifndef TMRDESKTOP_HPP
#define TMRDESKTOP_HPP
#define Uses_TDeskTop
#define Uses_TBackground
#include "MRPalette.hpp"
#include <tvision/tv.h>

class TMRDeskTop : public TDeskTop {
  public:
	TMRDeskTop(const TRect &r) : TDeskInit(&TDeskTop::initBackground), TDeskTop(r) {
	}
};
#endif
