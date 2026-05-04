#define Uses_TRect
#ifndef MRMENUFACTORY_HPP
#define MRMENUFACTORY_HPP

#include <tvision/tv.h>

class TMenuBar;
class TMenuItem;

TMenuBar *createMRMenuBar(TRect r);
TMenuItem *createMRWindowMenuPopupItems();

#endif
