#ifndef MRUPDATE_HPP
#define MRUPDATE_HPP

#include <string>
#include <cstdint>

namespace mr {
namespace coprocessor {
struct Result;
}
} // namespace mr

class MRMenuBar;

enum class MRSettingsVersionResolution : unsigned char {
	Reset,
	Updated,
	Cancel
};

MRSettingsVersionResolution mrResolveSettingsVersionConflict(std::uint64_t requiredBuild);

enum class MRUpdateInternalStartup : unsigned char {
	RunApplication,
	ParentFinished,
	Failed
};

MRUpdateInternalStartup mrStartInternalUpdateApply(int argc, char **argv, int &exitCode, std::string &error);
void mrStartAutomaticUpdateCheck();
bool mrAdoptUpdateCoprocessorResult(const mr::coprocessor::Result &result);
bool mrHandleUpdateCommand();
bool mrUpdateAvailable();
std::string mrUpdateAvailableVersion();
void mrRefreshUpdateMenu(MRMenuBar *menuBar);
bool mrUpdateForcesWorkspaceRestore();

#endif
