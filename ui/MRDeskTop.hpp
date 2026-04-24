#ifndef MRDESKTOP_HPP
#define MRDESKTOP_HPP

#define Uses_TDeskTop
#define Uses_TBackground
#define Uses_TDrawBuffer
#include <tvision/tv.h>

#include "../app/commands/MRWindowCommands.hpp"
#include "../config/MRDialogPaths.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {
class MRDesktopBackground : public TBackground {
  public:
	MRDesktopBackground(const TRect &bounds) : TBackground(bounds, '\xB0') {
	}

	void draw() override {
		TColorAttr desktopColor = resolveColor(kMrPaletteDesktop, getColor(1));
		TColorAttr markerColor = resolveColor(kMrPaletteVirtualDesktopMarker, desktopColor);
		std::vector<std::string> marker = markerLines(currentVirtualDesktop());
		const int markerRows = static_cast<int>(marker.size());
		int markerWidth = 0;
		int startX = 0;
		int startY = 0;

		for (const std::string &line : marker)
			markerWidth = std::max(markerWidth, static_cast<int>(line.size()));
		if (!marker.empty()) {
			startX = std::max(0, size.x - markerWidth - 1);
			startY = std::max(0, size.y - markerRows - 1);
		}

		for (int y = 0; y < size.y; ++y) {
			TDrawBuffer buffer;

			buffer.moveChar(0, pattern, desktopColor, size.x);
			if (!marker.empty() && y >= startY && y < startY + markerRows) {
				const std::string &markerLine = marker[static_cast<std::size_t>(y - startY)];
				for (std::size_t x = 0; x < markerLine.size() && startX + static_cast<int>(x) < size.x; ++x)
					if (markerLine[x] == '#') {
						buffer.putChar(startX + static_cast<ushort>(x), pattern);
						buffer.putAttribute(startX + static_cast<ushort>(x), markerColor);
					}
			}
			writeLine(0, y, size.x, 1, buffer);
		}
	}

  private:
	static TColorAttr resolveColor(unsigned char slot, TColorAttr fallback) {
		unsigned char biosAttr = 0;

		if (configuredColorSlotOverride(slot, biosAttr))
			return TColorAttr(biosAttr);
		return fallback;
	}

	static std::vector<std::string> markerLines(int currentVd) {
		if (configuredVirtualDesktops() <= 1)
			return {};

		switch (currentVd) {
			case 1:
				return {" # ", "## ", " # ", " # ", "###"};
			case 2:
				return {"###", "  #", "###", "#  ", "###"};
			case 3:
				return {"###", "  #", "###", "  #", "###"};
			case 4:
				return {"# #", "# #", "###", "  #", "  #"};
			case 5:
				return {"###", "#  ", "###", "  #", "###"};
			case 6:
				return {"###", "#  ", "###", "# #", "###"};
			case 7:
				return {"###", "  #", "  #", " # ", " # "};
			case 8:
				return {"###", "# #", "###", "# #", "###"};
			case 9:
				return {"###", "# #", "###", "  #", "###"};
			default:
				return {};
		}
	}
};
} // namespace

class MRDeskTop : public TDeskTop {
  public:
	MRDeskTop(const TRect &r) : TDeskInit(&MRDeskTop::initBackground), TDeskTop(r) {
	}

  private:
	static TBackground *initBackground(TRect r) {
		return new MRDesktopBackground(r);
	}
};

#endif
