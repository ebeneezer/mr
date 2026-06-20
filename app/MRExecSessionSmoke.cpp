#include "MRExecSessionSmoke.hpp"

#include "MRExecSessionStatus.hpp"
#include "../mrmac/MRMacroExecutionSession.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../ui/MRWindowSupport.hpp"

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace {
bool execSessionSmokePackageEnabled() noexcept {
	const char *value = std::getenv("MR_EXEC_SESSION_SMOKE");
	return value != nullptr && value[0] == '1' && value[1] == '\0';
}
} // namespace

void installExecSessionSmokePackageIfEnabled() {
	static MRMacroExecutionSessionListenerId listenerId = 0;

	if (!execSessionSmokePackageEnabled()) return;

	if (listenerId == 0) listenerId = installMacroExecutionSessionStatusHook();
	{
		std::ostringstream line;
		line << "MRMac exec session smoke package installed listener=" << listenerId << " generation=" << macroExecutionSessionStatusGeneration() << ".";
		mrLogMessage(line.str().c_str());
	}
}

void runExecSessionSmokeRoutedMacroIfEnabled() {
	if (!execSessionSmokePackageEnabled()) return;

	const char *macroFile = std::getenv("MR_EXEC_SESSION_SMOKE_RUN_MACRO");
	if (macroFile == nullptr || macroFile[0] == '\0') return;

	std::string errorText;
	if (runMacroFileByPath(macroFile, &errorText, false)) {
		mrLogMessage((std::string("MRMac exec session smoke routed macro accepted: ") + macroFile).c_str());
		return;
	}
	if (errorText.empty()) errorText = "Macro execution failed.";
	mrLogMessage((std::string("MRMac exec session smoke routed macro failed: ") + macroFile + ": " + errorText).c_str());
}

void logExecSessionSmokeSnapshotIfEnabled() {
	if (!execSessionSmokePackageEnabled()) return;

	const MRExecSessionStatusSnapshot snapshot = execSessionStatusSnapshot();
	mrLogMessage(("MRMac exec session smoke snapshot generation=" + std::to_string(macroExecutionSessionStatusGeneration()) + " active=" + std::to_string(snapshot.activeCount) + " pending-delay=" + std::to_string(snapshot.pendingDelayCount) + " recent-results=" + std::to_string(snapshot.recentResultCount) + ".").c_str());
}
