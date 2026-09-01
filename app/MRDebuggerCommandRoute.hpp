#ifndef MRDEBUGGERCOMMANDROUTE_HPP
#define MRDEBUGGERCOMMANDROUTE_HPP

#include <tvision/tv.h>

class MRBentoBox;

bool mrHandleDebuggerFunctionKey(MRBentoBox *bentoBox, TEvent &event);
bool mrHandleDebuggerCommand(MRBentoBox *bentoBox, TEvent &event);
bool mrStartGdbDebuggerForCurrentFile();

#endif
