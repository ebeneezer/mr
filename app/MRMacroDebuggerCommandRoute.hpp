#ifndef MRMACRODEBUGGERCOMMANDROUTE_HPP
#define MRMACRODEBUGGERCOMMANDROUTE_HPP

#include <tvision/tv.h>

class MRBentoBox;

bool mrHandleMacroDebuggerFunctionKey(MRBentoBox *bentoBox, TEvent &event);
bool mrHandleMacroDebuggerCommand(MRBentoBox *bentoBox, TEvent &event);

#endif
