#ifndef MRWINDOWMANAGER_HPP
#define MRWINDOWMANAGER_HPP

#define Uses_TEvent
#define Uses_TRect
#define Uses_TPoint
#include <tvision/tv.h>

class MREditWindow;

class MRWindowManager {
  public:
	static void handleDragView(MREditWindow *window, TEvent &event, uchar mode, TRect &limits, TPoint minSize, TPoint maxSize);
};

#endif
