#ifndef MRDEBUGGERCOMMANDROUTE_HPP
#define MRDEBUGGERCOMMANDROUTE_HPP

#include <tvision/tv.h>

class MRBentoBox;
class MREditWindow;

bool mrHandleDebuggerFunctionKey(MRBentoBox *bentoBox, TEvent &event);
bool mrHandleDebuggerCommand(MRBentoBox *bentoBox, TEvent &event);
bool mrStartGdbDebuggerForCurrentFile();
bool mrStartGdbDebuggerForWindow(MREditWindow *sourceWindow, bool runInferior = false);

#endif
