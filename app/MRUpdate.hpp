#ifndef MRUPDATE_HPP
#define MRUPDATE_HPP

#include <string>

namespace mr {
namespace coprocessor {
struct Result;
}
} // namespace mr

class MRMenuBar;

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
