#include "MRMacroExecutionSession.hpp"

#include "MRVM.hpp"
#include "../ui/MRWindowSupport.hpp"

#include <cstdlib>
#include <mutex>

namespace {
struct MacroExecutionSessionListener {
	MRMacroExecutionSessionListenerId listenerId = 0;
	MRMacroExecutionSessionHook hook = nullptr;
};

std::mutex macroExecutionSessionListenersMutex;
std::vector<MacroExecutionSessionListener> macroExecutionSessionListeners;

void noteMacroExecutionSessionStatusChanged() {
	mrvmNoteMacroExecutionSessionStatusChanged();
}

bool macroExecutionSessionTraceEnabled() noexcept;
const char *macroExecutionRouteName(MRMacroExecutionRoute route) noexcept;
const char *macroExecutionStateName(MRMacroExecutionState state) noexcept;
std::string describeMacroExecutionSession(const MRMacroExecutionSession &session);
std::vector<std::string> describeMacroExecutionSessionTraceStatus();
} // namespace

MRMacroExecutionSession createMacroExecutionSession(const std::string &label, MRMacroExecutionRoute route, const MRMacroExecutionOwner &owner) {
	MRMacroExecutionSession session;

	session.sessionId = mrvmNextMacroExecutionSessionId();
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
	mrvmStoreActiveMacroExecutionSession(session);
}

std::vector<MRMacroExecutionSession> activeMacroExecutionSessions() {
	return mrvmActiveMacroExecutionSessions();
}

std::vector<MRMacroExecutionSession> activeMacroExecutionSessionsForOwner(const MRMacroExecutionOwner &owner) {
	return mrvmActiveMacroExecutionSessionsForOwner(owner);
}

void publishMacroExecutionResult(MRMacroExecutionSession session, MRMacroExecutionState state, const std::string &message) {
	mrvmRecordMacroExecutionResult(session, state, message);
	notifyMacroExecutionSessionChanged();
}

std::vector<MRMacroExecutionResult> recentMacroExecutionResults() {
	return mrvmRecentMacroExecutionResults();
}

bool markMacroExecutionSessionCancellationRequestedForTask(std::uint64_t taskId) {
	if (taskId == 0) return false;
	if (!mrvmMarkMacroExecutionSessionCancellationRequestedForTask(taskId)) return false;
	notifyMacroExecutionSessionChanged();
	return true;
}

static bool noteMacroExecutionResultForTask(std::uint64_t taskId, MRMacroExecutionState state, const std::string &message) {
	MRMacroExecutionSession session;

	if (taskId == 0) return false;
	if (!mrvmTakeActiveMacroExecutionSessionForTask(taskId, session)) return false;
	mrvmRecordMacroExecutionResult(session, state, message);
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

	listener.listenerId = mrvmNextMacroExecutionSessionListenerId();
	listener.hook = hook;
	macroExecutionSessionListeners.push_back(listener);
	mrvmRegisterMacroExecutionSessionListener(listener.listenerId);
	return listener.listenerId;
}

bool removeMacroExecutionSessionListener(MRMacroExecutionSessionListenerId listenerId) {
	if (listenerId == 0) return false;

	std::lock_guard<std::mutex> lock(macroExecutionSessionListenersMutex);
	for (std::vector<MacroExecutionSessionListener>::iterator it = macroExecutionSessionListeners.begin(); it != macroExecutionSessionListeners.end(); ++it)
		if (it->listenerId == listenerId) {
			macroExecutionSessionListeners.erase(it);
			mrvmRemoveMacroExecutionSessionListener(listenerId);
			return true;
		}
	return false;
}

void notifyMacroExecutionSessionChanged() {
	std::vector<MRMacroExecutionSessionHook> hooks;
	{
		std::lock_guard<std::mutex> lock(macroExecutionSessionListenersMutex);
		hooks.reserve(macroExecutionSessionListeners.size());
		for (std::size_t index = 0; index < macroExecutionSessionListeners.size(); ++index) {
			const MacroExecutionSessionListener &listener = macroExecutionSessionListeners[index];

			if (listener.hook != nullptr) hooks.push_back(listener.hook);
		}
	}
	for (std::size_t index = 0; index < hooks.size(); ++index)
		hooks[index]();
	if (!macroExecutionSessionTraceEnabled()) return;
	const std::vector<std::string> traceLines = describeMacroExecutionSessionTraceStatus();
	for (std::size_t index = 0; index < traceLines.size(); ++index) {
		const std::string &line = traceLines[index];

		mrLogMessage(line.c_str());
	}
}

MRMacroExecutionSessionListenerId installMacroExecutionSessionStatusHook() {
	return addMacroExecutionSessionListener(noteMacroExecutionSessionStatusChanged);
}

std::uint64_t macroExecutionSessionStatusGeneration() {
	return mrvmMacroExecutionSessionStatusGeneration();
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
	for (std::size_t index = 0; index < activeSessions.size(); ++index) {
		const MRMacroExecutionSession &session = activeSessions[index];

		lines.push_back("active " + describeMacroExecutionSession(session));
	}
	for (std::size_t index = 0; index < pendingSessions.size(); ++index) {
		const MRMacroExecutionSession &session = pendingSessions[index];

		lines.push_back("pending " + describeMacroExecutionSession(session));
	}
	return lines;
}
} // namespace
