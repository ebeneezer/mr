#ifndef MRDEBUGGERCOMMANDROUTE_HPP
#define MRDEBUGGERCOMMANDROUTE_HPP

#include <tvision/tv.h>

#include <cstddef>

class MRBentoBox;
class MREditWindow;

bool mrHandleDebuggerFunctionKey(MRBentoBox *bentoBox, TEvent &event);
bool mrHandleDebuggerCommand(MRBentoBox *bentoBox, TEvent &event);
bool mrToggleDebuggerBreakpointForWindowAtOffset(MREditWindow *sourceWindow, std::size_t sourceOffset);
bool mrCanStartGdbDebuggerForWindow(MREditWindow *sourceWindow);
bool mrStartGdbDebuggerForCurrentFile();
bool mrStartGdbDebuggerForWindow(MREditWindow *sourceWindow);

#endif
