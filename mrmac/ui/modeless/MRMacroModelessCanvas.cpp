#include "MRMacroModelessCanvas.hpp"

#include "MRMacroModelessUi.hpp"
#include "../../MRVM.hpp"

#define Uses_TDrawBuffer
#define Uses_TView
#include <tvision/tv.h>

#include <algorithm>
#include <cstdlib>

namespace {

TColorAttr canvasStyle(TView &view, int style) noexcept {
	switch (style) {
		case 1:
			return view.getColor(2);
		case 2:
			return view.getColor(3);
		case 3:
			return view.getColor(4);
		default:
			return view.getColor(1);
	}
}

void drawCanvasText(TView &view, TDrawBuffer &buffer, int row, int canvasWidth, const MRMacroModelessCanvasCommand &command) {
	if (command.y != row || command.x < 0 || command.x >= canvasWidth || command.text.empty()) return;
	buffer.moveStr(static_cast<ushort>(command.x), command.text.c_str(), canvasStyle(view, command.style), static_cast<ushort>(canvasWidth - command.x));
}

void drawCanvasFill(TView &view, TDrawBuffer &buffer, int row, int canvasWidth, const MRMacroModelessCanvasCommand &command) {
	int left = std::max(0, command.x);
	int right = std::min(canvasWidth, command.x + command.width);

	if (row < command.y || row >= command.y + command.height || right <= left) return;
	buffer.moveChar(static_cast<ushort>(left), ' ', canvasStyle(view, command.style), static_cast<ushort>(right - left));
}

void drawCanvasLine(TView &view, TDrawBuffer &buffer, int row, int canvasWidth, const MRMacroModelessCanvasCommand &command) {
	int x = command.x;
	int y = command.y;
	const int stepX = command.x < command.x2 ? 1 : -1;
	const int stepY = command.y < command.y2 ? 1 : -1;
	const int deltaX = std::abs(command.x2 - command.x);
	const int deltaY = -std::abs(command.y2 - command.y);
	int error = deltaX + deltaY;
	const char glyph = command.text.empty() ? '*' : command.text.front();

	for (;;) {
		if (y == row && x >= 0 && x < canvasWidth) buffer.moveChar(static_cast<ushort>(x), glyph, canvasStyle(view, command.style), 1);
		if (x == command.x2 && y == command.y2) break;
		const int doubledError = error * 2;

		if (doubledError >= deltaY) {
			error += deltaY;
			x += stepX;
		}
		if (doubledError <= deltaX) {
			error += deltaX;
			y += stepY;
		}
	}
}

void drawCanvasBox(TView &view, TDrawBuffer &buffer, int row, int canvasWidth, const MRMacroModelessCanvasCommand &command) {
	const int left = command.x;
	const int top = command.y;
	const int right = command.x + command.width - 1;
	const int bottom = command.y + command.height - 1;
	const TColorAttr attribute = canvasStyle(view, command.style);

	if (command.width <= 0 || command.height <= 0 || row < top || row > bottom) return;
	if (row == top || row == bottom) {
		const int visibleLeft = std::max(0, left);
		const int visibleRight = std::min(canvasWidth - 1, right);

		if (visibleRight < visibleLeft) return;
		buffer.moveChar(static_cast<ushort>(visibleLeft), '-', attribute, static_cast<ushort>(visibleRight - visibleLeft + 1));
		if (left >= 0 && left < canvasWidth) buffer.moveChar(static_cast<ushort>(left), '+', attribute, 1);
		if (right >= 0 && right < canvasWidth) buffer.moveChar(static_cast<ushort>(right), '+', attribute, 1);
		return;
	}
	if (left >= 0 && left < canvasWidth) buffer.moveChar(static_cast<ushort>(left), '|', attribute, 1);
	if (right >= 0 && right < canvasWidth) buffer.moveChar(static_cast<ushort>(right), '|', attribute, 1);
}

class MRMacroModelessCanvasView final : public TView {
  public:
	MRMacroModelessCanvasView(const TRect &bounds, std::string ownerWindowId, std::string ownerCanvasId) : TView(bounds), windowId(std::move(ownerWindowId)), canvasId(std::move(ownerCanvasId)) {
		options &= ~(ofSelectable | ofFirstClick);
		eventMask &= static_cast<ushort>(~(evMouseDown | evMouseUp | evMouseMove | evKeyDown));
	}

	void draw() override {
		MRMacroModelessCanvasScene scene;
		const int canvasWidth = size.x;
		const int canvasHeight = size.y;

		static_cast<void>(mrvmReadModelessCanvasScene(windowId, canvasId, scene));
		for (int row = 0; row < canvasHeight; ++row) {
			TDrawBuffer buffer;

			buffer.moveChar(0, ' ', getColor(1), static_cast<ushort>(canvasWidth));
			for (std::size_t index = 0; index < scene.commands.size(); ++index) {
				const MRMacroModelessCanvasCommand &command = scene.commands[index];

				switch (command.type) {
					case MRMacroModelessCanvasCommandType::Clear:
					buffer.moveChar(0, ' ', canvasStyle(*this, command.style), static_cast<ushort>(canvasWidth));
					break;
				case MRMacroModelessCanvasCommandType::Text:
				case MRMacroModelessCanvasCommandType::Glyph:
					drawCanvasText(*this, buffer, row, canvasWidth, command);
					break;
				case MRMacroModelessCanvasCommandType::Line:
					drawCanvasLine(*this, buffer, row, canvasWidth, command);
					break;
				case MRMacroModelessCanvasCommandType::Box:
					drawCanvasBox(*this, buffer, row, canvasWidth, command);
					break;
				case MRMacroModelessCanvasCommandType::Fill:
					drawCanvasFill(*this, buffer, row, canvasWidth, command);
						break;
				}
			}
			writeLine(0, row, canvasWidth, 1, buffer);
		}
	}

  private:
	std::string windowId;
	std::string canvasId;
};

} // namespace

TView *createMacroModelessCanvasView(const TRect &bounds, const std::string &windowId, const std::string &canvasId) {
	return new MRMacroModelessCanvasView(bounds, windowId, canvasId);
}

void redrawMacroModelessCanvasView(TView *view) {
	if (view != nullptr) view->drawView();
}
