#ifndef TMRFRAME_HPP
#define TMRFRAME_HPP

#define Uses_TFrame
#define Uses_TRect
#include <tvision/tv.h>

class TMRFrame : public TFrame {
  public:
	TMRFrame(const TRect &bounds) : TFrame(bounds) {
	}
};

#endif
