#include "MRExecSessionStatus.hpp"

#include "../ui/MRWindowSupport.hpp"

#include <cstdlib>
#include <mutex>

namespace {
std::mutex execSessionStatusConsumerMutex;
MRMacroExecutionSessionListenerId execSessionStatusConsumerListenerId = 0;
std::uint64_t execSessionStatusConsumerGenerationValue = 0;

bool execSessionStatusEnabled() noexcept {
	const char *value = std::getenv("MR_EXEC_SESSION_STATUS");
	return value != nullptr && value[0] == '1' && value[1] == '\0';
}

void noteExecSessionStatusConsumerChanged() {
	std::lock_guard<std::mutex> lock(execSessionStatusConsumerMutex);
	++execSessionStatusConsumerGenerationValue;
}

const char *execSessionRouteText(MRMacroExecutionRoute route) noexcept {
	switch (route) {
		case MRMacroExecutionRoute::UiThread:
			return "ui-thread";
		case MRMacroExecutionRoute::ForegroundDelay:
			return "foreground-delay";
		case MRMacroExecutionRoute::Background:
			return "background";
		case MRMacroExecutionRoute::StagedBackground:
			return "staged-background";
		case MRMacroExecutionRoute::Unknown:
		default:
			return "unknown";
	}
}

const char *execSessionStateText(MRMacroExecutionState state) noexcept {
	switch (state) {
		case MRMacroExecutionState::Running:
			return "running";
		case MRMacroExecutionState::Yielded:
			return "yielded";
		case MRMacroExecutionState::CancellationRequested:
			return "cancellation-requested";
		case MRMacroExecutionState::Completed:
			return "completed";
		case MRMacroExecutionState::Cancelled:
			return "cancelled";
		case MRMacroExecutionState::Failed:
			return "failed";
		case MRMacroExecutionState::Rejected:
			return "rejected";
		case MRMacroExecutionState::Created:
		default:
			return "created";
	}
}

std::string execSessionLinePrefix(const char *prefix, const MRMacroExecutionSession &session) {
	std::string line = prefix != nullptr ? prefix : "session";

	line += " #";
	line += std::to_string(session.sessionId);
	line += " route=";
	line += execSessionRouteText(session.route);
	line += " state=";
	line += execSessionStateText(session.state);
	if (session.taskId != 0) {
		line += " task #";
		line += std::to_string(session.taskId);
	}
	if (session.owner.hasBuffer) {
		line += " buffer #";
		line += std::to_string(session.owner.bufferId);
	}
	if (!session.label.empty()) {
		line += " '";
		line += session.label;
		line += "'";
	}
	return line;
}
} // namespace

MRMacroExecutionSessionListenerId installExecSessionStatusConsumer() {
	std::lock_guard<std::mutex> lock(execSessionStatusConsumerMutex);

	if (execSessionStatusConsumerListenerId != 0) return execSessionStatusConsumerListenerId;
	execSessionStatusConsumerListenerId = addMacroExecutionSessionListener(noteExecSessionStatusConsumerChanged);
	return execSessionStatusConsumerListenerId;
}

void installExecSessionStatusConsumerIfEnabled() {
	if (!execSessionStatusEnabled()) return;
	const MRMacroExecutionSessionListenerId listenerId = installExecSessionStatusConsumer();
	mrLogMessage(("MRMac exec session status consumer installed listener=" + std::to_string(listenerId) + ".").c_str());
}

std::uint64_t execSessionStatusConsumerGeneration() {
	std::lock_guard<std::mutex> lock(execSessionStatusConsumerMutex);
	return execSessionStatusConsumerGenerationValue;
}

MRExecSessionStatusSnapshot execSessionStatusSnapshot() {
	MRExecSessionStatusSnapshot snapshot;

	snapshot.generation = execSessionStatusConsumerGeneration();
	snapshot.activeCount = activeMacroExecutionSessions().size();
	snapshot.pendingDelayCount = pendingForegroundMacroExecutionSessions().size();
	snapshot.recentResultCount = recentMacroExecutionResults().size();
	return snapshot;
}

std::vector<std::string> execSessionStatusLines(std::size_t maxRecentResults) {
	const std::vector<MRMacroExecutionSession> activeSessions = activeMacroExecutionSessions();
	const std::vector<MRMacroExecutionSession> pendingSessions = pendingForegroundMacroExecutionSessions();
	const std::vector<MRMacroExecutionResult> recentResults = recentMacroExecutionResults();
	const std::size_t recentStart = recentResults.size() > maxRecentResults ? recentResults.size() - maxRecentResults : 0;
	std::vector<std::string> lines;

	lines.push_back("MRMac exec sessions: active=" + std::to_string(activeSessions.size()) + ", pending-delay=" + std::to_string(pendingSessions.size()) + ", recent-results=" + std::to_string(recentResults.size()) + ", generation=" + std::to_string(execSessionStatusConsumerGeneration()) + ".");
	for (const MRMacroExecutionSession &session : activeSessions)
		lines.push_back(execSessionLinePrefix("active session", session));
	for (const MRMacroExecutionSession &session : pendingSessions)
		lines.push_back(execSessionLinePrefix("pending session", session));
	for (std::size_t i = recentStart; i < recentResults.size(); ++i) {
		std::string line = execSessionLinePrefix("result session", recentResults[i].session);
		line += " result-state=";
		line += execSessionStateText(recentResults[i].state);
		if (!recentResults[i].message.empty()) {
			line += " message=\"";
			line += recentResults[i].message;
			line += "\"";
		}
		lines.push_back(line);
	}
	return lines;
}

void logExecSessionStatusSnapshotIfEnabled() {
	if (!execSessionStatusEnabled()) return;
	for (const std::string &line : execSessionStatusLines(8))
		mrLogMessage(line.c_str());
}
