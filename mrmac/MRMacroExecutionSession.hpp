#ifndef MRMACROEXECUTIONSESSION_HPP
#define MRMACROEXECUTIONSESSION_HPP

#include <cstdint>
#include <string>
#include <vector>

using MRMacroExecutionSessionId = std::uint64_t;
using MRMacroExecutionSessionListenerId = std::uint64_t;

enum class MRMacroExecutionRoute {
	Unknown = 0,
	UiThread,
	ForegroundDelay,
	Background,
	StagedBackground,
	Debug
};

enum class MRMacroExecutionState {
	Created = 0,
	Running,
	Yielded,
	CancellationRequested,
	Completed,
	Cancelled,
	Failed,
	Rejected
};

struct MRMacroExecutionOwner {
	bool hasBuffer = false;
	int bufferId = 0;
	std::string modelessWindowId;
};

struct MRMacroExecutionSession {
	MRMacroExecutionSessionId sessionId = 0;
	MRMacroExecutionOwner owner;
	MRMacroExecutionRoute route = MRMacroExecutionRoute::Unknown;
	MRMacroExecutionState state = MRMacroExecutionState::Created;
	std::uint64_t taskId = 0;
	std::string label;
};

struct MRMacroExecutionResult {
	MRMacroExecutionSession session;
	MRMacroExecutionState state = MRMacroExecutionState::Created;
	std::string message;
};

using MRMacroExecutionSessionHook = void (*)(void);

MRMacroExecutionSession createMacroExecutionSession(const std::string &label, MRMacroExecutionRoute route, const MRMacroExecutionOwner &owner);
bool macroExecutionOwnerMatches(const MRMacroExecutionOwner &sessionOwner, const MRMacroExecutionOwner &owner) noexcept;
void trackMacroExecutionSession(const MRMacroExecutionSession &session);
void publishMacroExecutionResult(MRMacroExecutionSession session, MRMacroExecutionState state, const std::string &message);
std::vector<MRMacroExecutionSession> activeMacroExecutionSessions();
std::vector<MRMacroExecutionSession> activeMacroExecutionSessionsForOwner(const MRMacroExecutionOwner &owner);
std::vector<MRMacroExecutionSession> pendingForegroundMacroExecutionSessions();
std::vector<MRMacroExecutionResult> recentMacroExecutionResults();
bool markMacroExecutionSessionCancellationRequestedForTask(std::uint64_t taskId);
bool publishMacroExecutionResultForTask(std::uint64_t taskId, MRMacroExecutionState state, const std::string &message);
MRMacroExecutionSessionListenerId addMacroExecutionSessionListener(MRMacroExecutionSessionHook hook);
bool removeMacroExecutionSessionListener(MRMacroExecutionSessionListenerId listenerId);
void notifyMacroExecutionSessionChanged();
MRMacroExecutionSessionListenerId installMacroExecutionSessionStatusHook();
std::uint64_t macroExecutionSessionStatusGeneration();

#endif
