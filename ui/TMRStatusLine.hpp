#ifndef TMRSTATUSLINE_HPP
#define TMRSTATUSLINE_HPP
#define Uses_TStatusLine
#include <tvision/tv.h>
#include "mrpalette.hpp"

class TMRStatusLine : public TStatusLine
{
public:
	TMRStatusLine(const TRect &r, TStatusDef &aDef) : TStatusLine(r, aDef) {}

};
#endif