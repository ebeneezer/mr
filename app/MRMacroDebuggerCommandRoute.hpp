#ifndef MRMACRODEBUGGERCOMMANDROUTE_HPP
#define MRMACRODEBUGGERCOMMANDROUTE_HPP

#include "../mrmac/MRMacroExecutionSession.hpp"

#include <string>

class MRBentoBox;
struct MRMacroDebugRunResult;
struct TEvent;

bool mrHandleMacroDebuggerFunctionKey(MRBentoBox *bentoBox, TEvent &event);
bool mrHandleMacroDebuggerCommand(MRBentoBox *bentoBox, TEvent &event);
bool mrMacroDebuggerObservesSourcePath(const std::string &sourcePath);
bool mrAttachScheduledMacroDebuggerSession(MRMacroExecutionSessionId sessionId, const MRMacroDebugRunResult &debugResult);

#endif
