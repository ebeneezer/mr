#ifndef MRBENTOWORKSPACECODEC_HPP
#define MRBENTOWORKSPACECODEC_HPP

#include <string>
#include <vector>

struct MRBentoWorkspaceSnapshot;

namespace mr {
namespace workspace {

std::string encodeBentoSnapshot(const MRBentoWorkspaceSnapshot &snapshot);
bool parseBentoSnapshot(const std::string &token, MRBentoWorkspaceSnapshot &snapshot, std::vector<std::string> *bootstrapLogMessages = nullptr);

} // namespace workspace
} // namespace mr

#endif
