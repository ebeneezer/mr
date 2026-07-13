#ifndef MRDESKTOPWINDOW_HPP
#define MRDESKTOPWINDOW_HPP

#define Uses_TRect
#include <tvision/tv.h>

class TWindow;

struct MRDesktopMinimizedState {
	bool minimized = false;
	bool bufferedBeforeMinimize = false;
	TRect restoreBounds = TRect(0, 0, 1, 1);
	TRect lastMinimizedBounds = TRect(0, 0, 0, 0);
};

class MRDesktopWindow {
  public:
	virtual ~MRDesktopWindow() = default;
	virtual TWindow *desktopNativeWindow() = 0;
	virtual const TWindow *desktopNativeWindow() const = 0;
	virtual int desktopIndex() const = 0;
	virtual void setDesktopIndex(int index) = 0;
	virtual bool desktopManuallyHidden() const = 0;
	virtual void setDesktopManuallyHidden(bool hidden) = 0;
	virtual bool desktopMinimized() const = 0;
	virtual void readDesktopMinimizedState(MRDesktopMinimizedState &state) const = 0;
	virtual void storeDesktopMinimizedState(const MRDesktopMinimizedState &state) = 0;
	virtual const char *desktopMinimizedTitle() const = 0;
	virtual void layoutDesktopContents() = 0;
	virtual void synchronizeDesktopContents() = 0;
	virtual void restoreDesktopWindow() = 0;
	virtual void applyDesktopBounds(const TRect &bounds) = 0;
	virtual bool desktopShowsFrameGrowHandle() const = 0;
};

#endif
