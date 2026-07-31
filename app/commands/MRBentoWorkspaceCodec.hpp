#ifndef MRBENTOWORKSPACECODEC_HPP
#define MRBENTOWORKSPACECODEC_HPP

#include <string>
#include <vector>

struct MRBentoWorkspaceSnapshot;
struct MRMacroDebuggerWorkspaceConfiguration;

namespace mr {
namespace workspace {

std::string encodeBentoSnapshot(const MRBentoWorkspaceSnapshot &snapshot);
bool parseBentoSnapshot(const std::string &token, MRBentoWorkspaceSnapshot &snapshot, std::vector<std::string> *bootstrapLogMessages = nullptr);
std::string encodeMacroDebuggerConfiguration(const MRMacroDebuggerWorkspaceConfiguration &configuration);
bool parseMacroDebuggerConfiguration(const std::string &token, MRMacroDebuggerWorkspaceConfiguration &configuration);

} // namespace workspace
} // namespace mr

#endif
