#ifndef MRMACRO_MODELESS_CANVAS_HPP
#define MRMACRO_MODELESS_CANVAS_HPP

#include <string>

class TRect;
class TView;

TView *createMacroModelessCanvasView(const TRect &bounds, const std::string &windowId, const std::string &canvasId);
void redrawMacroModelessCanvasView(TView *view);

#endif
