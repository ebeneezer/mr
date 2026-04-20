#ifndef TMRDESKTOP_HPP
#define TMRDESKTOP_HPP
#define Uses_TDeskTop
#define Uses_TBackground
#include "MRPalette.hpp"
#include <tvision/tv.h>

#include "../config/MRDialogPaths.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include <string>

class TMRDeskTop : public TDeskTop {
  public:
	TMRDeskTop(const TRect &r) : TDeskInit(&TDeskTop::initBackground), TDeskTop(r) {
		if (background != nullptr)
			background->pattern = '\xB0';
	}

	virtual void draw() override {
		TDeskTop::draw();
		int maxVd = configuredVirtualDesktops();
		if (maxVd > 1) {
			int currentVd = currentVirtualDesktop();
			std::string vdStr = std::to_string(currentVd);
			TDrawBuffer b;
			TColorAttr color = background != nullptr ? background->mapColor(1) : mapColor(1);
			b.moveStr(0, vdStr, color);
			writeLine(size.x - 1, size.y - 1, 1, 1, b);
		}
	}
};
#endif
