#ifndef TMRSTATUSLINE_HPP
#define TMRSTATUSLINE_HPP
#define Uses_TStatusLine
#include "MRPalette.hpp"
#include <tvision/tv.h>

class TMRStatusLine : public TStatusLine {
  public:
	TMRStatusLine(const TRect &r, TStatusDef &aDef) : TStatusLine(r, aDef) {
	}
};
#endif
