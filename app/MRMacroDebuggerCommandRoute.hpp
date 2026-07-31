#ifndef MRMACRODEBUGGERCOMMANDROUTE_HPP
#define MRMACRODEBUGGERCOMMANDROUTE_HPP

#include "../mrmac/MRMacroExecutionSession.hpp"

#include <cstdint>
#include <string>

class MRBentoBox;
struct MRMacroDebugRunResult;
struct TEvent;

bool mrHandleMacroDebuggerFunctionKey(MRBentoBox *bentoBox, TEvent &event);
bool mrHandleMacroDebuggerCommand(MRBentoBox *bentoBox, TEvent &event);
MRBentoBox *mrMacroDebuggerForSourcePath(const std::string &sourcePath, const MRBentoBox *excluded = nullptr);
bool mrMacroDebuggerObservesSourcePath(const std::string &sourcePath);
bool mrAttachScheduledMacroDebuggerSession(MRMacroExecutionSessionId sessionId, const MRMacroDebugRunResult &debugResult);
bool mrApplyMacroDebuggerWorkerResult(MRMacroExecutionSessionId sessionId, std::uint64_t taskId, const MRMacroDebugRunResult &debugResult, const std::string &errorMessage);

#endif
