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
MRBentoBox *mrMacroDebuggerForSourceIdentity(const std::string &sourcePath, const std::string &macroName, const MRBentoBox *excluded = nullptr);
bool mrMacroDebuggerObservesSourceIdentity(const std::string &sourcePath, const std::string &macroName);
bool mrAttachScheduledMacroDebuggerSession(MRMacroExecutionSessionId sessionId, const MRMacroDebugRunResult &debugResult);
bool mrApplyMacroDebuggerWorkerResult(MRMacroExecutionSessionId sessionId, std::uint64_t taskId, const MRMacroDebugRunResult &debugResult, const std::string &errorMessage);

#endif
