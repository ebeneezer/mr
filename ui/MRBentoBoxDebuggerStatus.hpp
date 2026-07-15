#ifndef MRBENTOBOXDEBUGGERSTATUS_HPP
#define MRBENTOBOXDEBUGGERSTATUS_HPP

#include <string>

#include "../mrmac/MRVM.hpp"

const char *mrMacroDebuggerStopReasonText(MRMacroDebugStopReason reason) noexcept;
std::string mrMacroDebuggerStatusText(const std::string &macroName, MRMacroExecutionSessionId sessionId, const MRMacroDebugRunResult &debugResult, const std::string &errorMessage);
std::string mrMacroDebuggerNoticeText(const std::string &macroName, MRMacroExecutionSessionId sessionId, const std::string &message);

#endif
