#include "MRMacroExecutionSession.hpp"

#include "../ui/MRWindowSupport.hpp"

#include <cstdlib>
#include <map>
#include <mutex>

namespace {
struct MacroExecutionSessionListener {
	MRMacroExecutionSessionListenerId listenerId = 0;
	MRMacroExecutionSessionHook hook = nullptr;
};

std::mutex macroExecutionSessionMutex;
MRMacroExecutionSessionId nextMacroExecutionSessionValue = 1;
std::mutex macroExecutionResultsMutex;
std::vector<MRMacroExecutionResult> macroExecutionResults;
constexpr std::size_t kMacroExecutionResultHistoryLimit = 32;
std::mutex activeMacroExecutionSessionsMutex;
std::map<std::uint64_t, MRMacroExecutionSession> activeMacroExecutionSessionMap;
std::mutex macroExecutionSessionListenersMutex;
MRMacroExecutionSessionListenerId nextMacroExecutionSessionListenerValue = 1;
std::vector<MacroExecutionSessionListener> macroExecutionSessionListeners;
std::mutex macroExecutionSessionStatusMutex;
std::uint64_t macroExecutionSessionStatusGenerationValue = 0;

MRMacroExecutionSessionId nextMacroExecutionSessionId() {
	std::lock_guard<std::mutex> lock(macroExecutionSessionMutex);
	return nextMacroExecutionSessionValue++;
}

void noteMacroExecutionSessionStatusChanged() {
	std::lock_guard<std::mutex> lock(macroExecutionSessionStatusMutex);
	++macroExecutionSessionStatusGenerationValue;
}

bool macroExecutionSessionTraceEnabled() noexcept;
const char *macroExecutionRouteName(MRMacroExecutionRoute route) noexcept;
const char *macroExecutionStateName(MRMacroExecutionState state) noexcept;
std::string describeMacroExecutionSession(const MRMacroExecutionSession &session);
std::vector<std::string> describeMacroExecutionSessionTraceStatus();
} // namespace

MRMacroExecutionSession createMacroExecutionSession(const std::string &label, MRMacroExecutionRoute route, const MRMacroExecutionOwner &owner) {
	MRMacroExecutionSession session;

	session.sessionId = nextMacroExecutionSessionId();
	session.owner = owner;
	session.route = route;
	session.state = MRMacroExecutionState::Running;
	session.label = label;
	return session;
}

bool macroExecutionOwnerMatches(const MRMacroExecutionOwner &sessionOwner, const MRMacroExecutionOwner &owner) noexcept {
	if (owner.hasBuffer) return sessionOwner.hasBuffer && sessionOwner.bufferId == owner.bufferId;
	return !sessionOwner.hasBuffer;
}

void trackMacroExecutionSession(const MRMacroExecutionSession &session) {
	if (session.taskId == 0) return;

	std::lock_guard<std::mutex> lock(activeMacroExecutionSessionsMutex);
	activeMacroExecutionSessionMap[session.taskId] = session;
}

std::vector<MRMacroExecutionSession> activeMacroExecutionSessions() {
	std::lock_guard<std::mutex> lock(activeMacroExecutionSessionsMutex);
	std::vector<MRMacroExecutionSession> sessions;

	sessions.reserve(activeMacroExecutionSessionMap.size());
	for (const auto &entry : activeMacroExecutionSessionMap)
		sessions.push_back(entry.second);
	return sessions;
}

std::vector<MRMacroExecutionSession> activeMacroExecutionSessionsForOwner(const MRMacroExecutionOwner &owner) {
	std::lock_guard<std::mutex> lock(activeMacroExecutionSessionsMutex);
	std::vector<MRMacroExecutionSession> sessions;

	for (const auto &entry : activeMacroExecutionSessionMap)
		if (macroExecutionOwnerMatches(entry.second.owner, owner)) sessions.push_back(entry.second);
	return sessions;
}

static void recordMacroExecutionResult(MRMacroExecutionSession session, MRMacroExecutionState state, const std::string &message) {
	std::lock_guard<std::mutex> lock(macroExecutionResultsMutex);
	MRMacroExecutionResult result;

	session.state = state;
	result.session = session;
	result.state = state;
	result.message = message;
	macroExecutionResults.push_back(result);
	while (macroExecutionResults.size() > kMacroExecutionResultHistoryLimit)
		macroExecutionResults.erase(macroExecutionResults.begin());
}

void publishMacroExecutionResult(MRMacroExecutionSession session, MRMacroExecutionState state, const std::string &message) {
	recordMacroExecutionResult(session, state, message);
	notifyMacroExecutionSessionChanged();
}

std::vector<MRMacroExecutionResult> recentMacroExecutionResults() {
	std::lock_guard<std::mutex> lock(macroExecutionResultsMutex);
	return macroExecutionResults;
}

bool markMacroExecutionSessionCancellationRequestedForTask(std::uint64_t taskId) {
	if (taskId == 0) return false;
	{
		std::lock_guard<std::mutex> lock(activeMacroExecutionSessionsMutex);
		std::map<std::uint64_t, MRMacroExecutionSession>::iterator found = activeMacroExecutionSessionMap.find(taskId);
		if (found == activeMacroExecutionSessionMap.end()) return false;
		found->second.state = MRMacroExecutionState::CancellationRequested;
	}
	notifyMacroExecutionSessionChanged();
	return true;
}

static bool noteMacroExecutionResultForTask(std::uint64_t taskId, MRMacroExecutionState state, const std::string &message) {
	MRMacroExecutionSession session;

	if (taskId == 0) return false;
	{
		std::lock_guard<std::mutex> lock(activeMacroExecutionSessionsMutex);
		std::map<std::uint64_t, MRMacroExecutionSession>::iterator found = activeMacroExecutionSessionMap.find(taskId);
		if (found == activeMacroExecutionSessionMap.end()) return false;
		session = found->second;
		activeMacroExecutionSessionMap.erase(found);
	}
	recordMacroExecutionResult(session, state, message);
	return true;
}

bool publishMacroExecutionResultForTask(std::uint64_t taskId, MRMacroExecutionState state, const std::string &message) {
	if (!noteMacroExecutionResultForTask(taskId, state, message)) return false;
	notifyMacroExecutionSessionChanged();
	return true;
}

MRMacroExecutionSessionListenerId addMacroExecutionSessionListener(MRMacroExecutionSessionHook hook) {
	if (hook == nullptr) return 0;

	std::lock_guard<std::mutex> lock(macroExecutionSessionListenersMutex);
	MacroExecutionSessionListener listener;

	listener.listenerId = nextMacroExecutionSessionListenerValue++;
	listener.hook = hook;
	macroExecutionSessionListeners.push_back(listener);
	return listener.listenerId;
}

bool removeMacroExecutionSessionListener(MRMacroExecutionSessionListenerId listenerId) {
	if (listenerId == 0) return false;

	std::lock_guard<std::mutex> lock(macroExecutionSessionListenersMutex);
	for (std::vector<MacroExecutionSessionListener>::iterator it = macroExecutionSessionListeners.begin(); it != macroExecutionSessionListeners.end(); ++it)
		if (it->listenerId == listenerId) {
			macroExecutionSessionListeners.erase(it);
			return true;
		}
	return false;
}

void notifyMacroExecutionSessionChanged() {
	std::vector<MRMacroExecutionSessionHook> hooks;
	{
		std::lock_guard<std::mutex> lock(macroExecutionSessionListenersMutex);
		hooks.reserve(macroExecutionSessionListeners.size());
		for (const MacroExecutionSessionListener &listener : macroExecutionSessionListeners)
			if (listener.hook != nullptr) hooks.push_back(listener.hook);
	}
	for (MRMacroExecutionSessionHook hook : hooks)
		hook();
	if (!macroExecutionSessionTraceEnabled()) return;
	for (const std::string &line : describeMacroExecutionSessionTraceStatus())
		mrLogMessage(line.c_str());
}

MRMacroExecutionSessionListenerId installMacroExecutionSessionStatusHook() {
	return addMacroExecutionSessionListener(noteMacroExecutionSessionStatusChanged);
}

std::uint64_t macroExecutionSessionStatusGeneration() {
	std::lock_guard<std::mutex> lock(macroExecutionSessionStatusMutex);
	return macroExecutionSessionStatusGenerationValue;
}

namespace {
bool macroExecutionSessionTraceEnabled() noexcept {
	static const bool enabled = []() noexcept {
		const char *value = std::getenv("MR_TRACE_MACRO_EXEC_SESSIONS");
		return value != nullptr && value[0] == '1' && value[1] == '\0';
	}();
	return enabled;
}

const char *macroExecutionRouteName(MRMacroExecutionRoute route) noexcept {
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

const char *macroExecutionStateName(MRMacroExecutionState state) noexcept {
	switch (state) {
		case MRMacroExecutionState::CancellationRequested:
			return "cancellation-requested";
		case MRMacroExecutionState::Running:
			return "running";
		case MRMacroExecutionState::Yielded:
			return "yielded";
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

std::string describeMacroExecutionSession(const MRMacroExecutionSession &session) {
	std::string line = "session #";

	line += std::to_string(session.sessionId);
	line += " route=";
	line += macroExecutionRouteName(session.route);
	line += " state=";
	line += macroExecutionStateName(session.state);
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

std::vector<std::string> describeMacroExecutionSessionTraceStatus() {
	const std::vector<MRMacroExecutionSession> activeSessions = activeMacroExecutionSessions();
	const std::vector<MRMacroExecutionSession> pendingSessions = pendingForegroundMacroExecutionSessions();
	const std::vector<MRMacroExecutionResult> recentResults = recentMacroExecutionResults();
	std::vector<std::string> lines;

	lines.push_back("MRMac execution sessions: active=" + std::to_string(activeSessions.size()) + ", pending-delay=" + std::to_string(pendingSessions.size()) + ", recent-results=" + std::to_string(recentResults.size()) + ".");
	for (const auto &session : activeSessions)
		lines.push_back("active " + describeMacroExecutionSession(session));
	for (const auto &session : pendingSessions)
		lines.push_back("pending " + describeMacroExecutionSession(session));
	return lines;
}
} // namespace
