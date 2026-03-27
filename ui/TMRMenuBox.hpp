#ifndef TMRMENUBOX_HPP
#define TMRMENUBOX_HPP

#define Uses_TMenuBox
#define Uses_TMenu
#define Uses_TMenuView
#define Uses_TRect
#include <tvision/tv.h>

class TMRMenuBox : public TMenuBox
{
public:
    TMRMenuBox(const TRect &bounds, TMenu *aMenu, TMenuView *aParentMenu)
        : TMenuBox(bounds, aMenu, aParentMenu) {}
};

#endif
