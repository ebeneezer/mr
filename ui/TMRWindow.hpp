#ifndef TMRWINDOW_HPP
#define TMRWINDOW_HPP

#define Uses_TWindow
#define Uses_TEvent
#define Uses_TPalette
#include "mrtheme.hpp"
#include <tvision/tv.h>

class TMRWindow : public TWindow {
  protected:
	MRTheme currentTheme;

  public:
	TMRWindow(const TRect &bounds, const char *title, const std::string &classIdentifier)
	    : TWindowInit(&TMRWindow::initFrame), TWindow(bounds, title, wnNoNumber) {

		if ((flags & wfClose) == 0) {
			flags |= wfClose;
		}

		currentTheme = MRThemeRegistry::instance().getTheme(classIdentifier);
	}

	virtual TPalette &getPalette() const {
		static TPalette palette(currentTheme.paletteData, 16);
		return palette;
	}

	virtual void handleEvent(TEvent &event) {
		if (event.what == evCommand && event.message.command == cmClose) {
			destroy(this);
			clearEvent(event);
		} else {
			TWindow::handleEvent(event);
		}
	}
};

#endif