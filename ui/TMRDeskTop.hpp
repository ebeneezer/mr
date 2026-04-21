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

			const char* figlet1[] = {
				"  _ ",
				" / |",
				" | |",
				" | |",
				" |_|"
			};
			const char* figlet2[] = {
				" ____  ",
				"|___ \\ ",
				"  __) |",
				" / __/ ",
				"|_____|"
			};
			const char* figlet3[] = {
				" _____ ",
				"|___ / ",
				"  |_ \\ ",
				" ___) |",
				"|____/ "
			};
			const char* figlet4[] = {
				" _  _   ",
				"| || |  ",
				"| || |_ ",
				"|__   _|",
				"   |_|  "
			};
			const char* figlet5[] = {
				" _____ ",
				"| ____|",
				"| |__  ",
				"|___ \\ ",
				" ___) |",
				"|____/ "
			};
			const char* figlet6[] = {
				"  __   ",
				" / /_  ",
				"| '_ \\ ",
				"| (_) |",
				" \\___/ "
			};
			const char* figlet7[] = {
				" _____ ",
				"|___  |",
				"   / / ",
				"  / /  ",
				" /_/   "
			};
			const char* figlet8[] = {
				"  ___  ",
				" ( _ ) ",
				" / _ \\ ",
				"| (_) |",
				" \\___/ "
			};
			const char* figlet9[] = {
				"  ___  ",
				" / _ \\ ",
				"| (_) |",
				" \\__, |",
				"   /_/ "
			};

			const char** currentFiglet = nullptr;
			int rows = 5;
			if (currentVd == 1) currentFiglet = figlet1;
			else if (currentVd == 2) currentFiglet = figlet2;
			else if (currentVd == 3) currentFiglet = figlet3;
			else if (currentVd == 4) currentFiglet = figlet4;
			else if (currentVd == 5) currentFiglet = figlet5;
			else if (currentVd == 6) currentFiglet = figlet6;
			else if (currentVd == 7) currentFiglet = figlet7;
			else if (currentVd == 8) currentFiglet = figlet8;
			else if (currentVd == 9) currentFiglet = figlet9;

			if (currentFiglet != nullptr) {
				TColorAttr color = background != nullptr ? background->mapColor(1) : mapColor(1);
				int maxWidth = 0;
				for (int i = 0; i < rows; ++i) {
					int len = std::string(currentFiglet[i]).length();
					if (len > maxWidth) maxWidth = len;
				}

				int startX = size.x - maxWidth - 1;
				int startY = size.y - rows - 1;
				if (startX < 0) startX = 0;
				if (startY < 0) startY = 0;

				for (int i = 0; i < rows; ++i) {
					TDrawBuffer b;
					std::string lineStr = currentFiglet[i];
					b.moveStr(0, lineStr, color);
					writeLine(startX, startY + i, lineStr.length(), 1, b);
				}
			}
		}
	}
};
#endif
