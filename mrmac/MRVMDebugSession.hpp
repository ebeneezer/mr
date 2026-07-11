#ifndef MRVM_DEBUG_SESSION_HPP
#define MRVM_DEBUG_SESSION_HPP

#include "MRVM.hpp"

struct MRVMDebugSessionCleanup {
	std::string macroKey;
	std::string fileKey;
	bool unloadAfterCompletion;
	bool evictTransientAfterCompletion;

	MRVMDebugSessionCleanup() : macroKey(), fileKey(), unloadAfterCompletion(false), evictTransientAfterCompletion(false) {
	}
};

bool mrvmCollectDebugBreakpointOffsetsForLoadedFile(const std::string &macroKey, std::vector<std::size_t> &breakpointOffsets);
bool mrvmConfigureDebugSessionCleanup(MRMacroExecutionSessionId sessionId, const MRVMDebugSessionCleanup &cleanup);
void mrvmFinalizeDebugSession(MRMacroExecutionSession &session, const MRMacroDebugRunResult &result, const MRVMDebugSessionCleanup &cleanup);

#endif
