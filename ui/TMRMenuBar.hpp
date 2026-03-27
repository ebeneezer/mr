#ifndef TMRMENUBAR_HPP
#define TMRMENUBAR_HPP
#define Uses_TMenuBar
#include <tvision/tv.h>
#include "mrpalette.hpp"

class TMRMenuBar : public TMenuBar
{
public:
    TMRMenuBar(const TRect &r, TSubMenu &aMenu) : TMenuBar(r, aMenu) {}
};

#endif
