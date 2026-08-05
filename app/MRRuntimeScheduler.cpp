#include "MRRuntimeScheduler.hpp"

#include "MRMacroDebuggerCommandRoute.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../mrmac/MRVM.hpp"
#include "../ui/MRWindowSupport.hpp"

#include <cstdlib>
#include <mutex>

namespace {
std::mutex runtimeSchedulerMutex;
std::mutex runtimeSchedulerSessionListenerMutex;
MRMacroExecutionSessionListenerId runtimeSchedulerSessionListenerId = 0;

struct RuntimeSchedulerDueConsumer {
	MRRuntimeScheduledConsumerId consumerId = 0;
	MRRuntimeScheduledConsumerConfig config;
	std::uint64_t dueAtMs = 0;
	std::uint64_t observedAtMs = 0;
};

bool runtimeSchedulerSmokeEnabled() noexcept {
	const char *value = std::getenv("MR_RUNTIME_SCHEDULER_SMOKE");
	return value != nullptr && value[0] == '1' && value[1] == '\0';
}

std::string runtimeSchedulerSmokeSource() {
	return "$MACRO RuntimeSchedulerSmoke;\nDEF_INT(ProbeValue);\nProbeValue := 40 + 2;\nEND_MACRO;\n";
}

std::string runtimeSchedulerEventLine(const MRRuntimeSchedulerEvent &event);

void noteRuntimeSchedulerObservedConsumersLocked(std::size_t count) {
	if (count == 0) mrvmStoreRuntimeSchedulerNextPumpMs(0);
}

void noteRuntimeSchedulerNextDue(std::uint64_t &nextDueCandidate, std::uint64_t nextDueMs) noexcept {
	if (nextDueMs == 0) return;
	if (nextDueCandidate == 0 || nextDueMs < nextDueCandidate) nextDueCandidate = nextDueMs;
}

const char *runtimeSchedulerEventKindText(MRRuntimeSchedulerEventKind kind) noexcept {
	switch (kind) {
		case MRRuntimeSchedulerEventKind::ConsumerRemoved:
			return "consumer-removed";
		case MRRuntimeSchedulerEventKind::TickDue:
			return "tick-due";
		case MRRuntimeSchedulerEventKind::TickStarted:
			return "tick-started";
		case MRRuntimeSchedulerEventKind::TickFinished:
			return "tick-finished";
		case MRRuntimeSchedulerEventKind::TickStartFailed:
			return "tick-start-failed";
		case MRRuntimeSchedulerEventKind::TickSkipped:
			return "tick-skipped";
		case MRRuntimeSchedulerEventKind::ConsumerRegistered:
		default:
			return "consumer-registered";
	}
}

const char *runtimeSchedulerSkipReasonText(MRRuntimeSchedulerSkipReason reason) noexcept {
	switch (reason) {
		case MRRuntimeSchedulerSkipReason::PreviousSessionStillActive:
			return "previous-session-still-active";
		case MRRuntimeSchedulerSkipReason::ConsumerNotFound:
			return "consumer-not-found";
		case MRRuntimeSchedulerSkipReason::None:
		default:
			return "none";
	}
}

MRRuntimeSchedulerEvent recordRuntimeSchedulerEventLocked(MRRuntimeScheduledConsumerId consumerId, const MRRuntimeScheduledConsumerConfig &config, MRRuntimeSchedulerEventKind kind, MRRuntimeSchedulerSkipReason skipReason, MRMacroExecutionSessionId sessionId, MRMacroExecutionSessionId blockingSessionId, std::uint64_t dueAtMs, std::uint64_t observedAtMs, const std::string &message) {
	MRRuntimeSchedulerEvent event;

	event.eventId = mrvmNextRuntimeSchedulerEventId();
	event.consumerId = consumerId;
	event.owner = config.owner;
	event.kind = kind;
	event.skipReason = skipReason;
	event.sessionId = sessionId;
	event.blockingSessionId = blockingSessionId;
	event.dueAtMs = dueAtMs;
	event.observedAtMs = observedAtMs;
	event.macroSpec = config.macroSpec;
	event.message = message;
	mrvmRecordRuntimeSchedulerEvent(event);
	return event;
}

bool runtimeExecutionSessionStillActive(const MRMacroExecutionSession &session) noexcept {
	return session.sessionId != 0 && (session.state == MRMacroExecutionState::Running || session.state == MRMacroExecutionState::Yielded || session.state == MRMacroExecutionState::CancellationRequested);
}

bool runtimeSchedulerEventShouldEnterVisibleLog(const MRRuntimeSchedulerEvent &event) noexcept {
	switch (event.kind) {
		case MRRuntimeSchedulerEventKind::TickDue:
		case MRRuntimeSchedulerEventKind::TickStarted:
			return false;
		case MRRuntimeSchedulerEventKind::TickFinished:
		case MRRuntimeSchedulerEventKind::TickStartFailed:
		case MRRuntimeSchedulerEventKind::TickSkipped:
		case MRRuntimeSchedulerEventKind::ConsumerRemoved:
		case MRRuntimeSchedulerEventKind::ConsumerRegistered:
		default:
			return true;
	}
}

void recordRuntimeScheduledConsumerFinishResults(const std::vector<MRMacroExecutionResult> &results) {
	std::vector<MRRuntimeSchedulerEvent> logEvents;
	{
		std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
		std::vector<MRRuntimeScheduledConsumer> consumers = mrvmRuntimeScheduledConsumers();
		for (std::size_t resultIndex = 0; resultIndex < results.size(); ++resultIndex) {
			const MRMacroExecutionResult &result = results[resultIndex];

			if (result.session.sessionId == 0) continue;
			for (std::size_t consumerIndex = 0; consumerIndex < consumers.size(); ++consumerIndex) {
				MRRuntimeScheduledConsumer &consumer = consumers[consumerIndex];

				if (consumer.activeSessionId != result.session.sessionId) continue;
				consumer.activeSessionId = 0;
				static_cast<void>(mrvmUpdateRuntimeScheduledConsumerActiveSession(consumer.consumerId, consumer.activeSessionId));
				MRRuntimeSchedulerEvent event = recordRuntimeSchedulerEventLocked(consumer.consumerId, consumer.config, MRRuntimeSchedulerEventKind::TickFinished, MRRuntimeSchedulerSkipReason::None, result.session.sessionId, 0, 0, 0, result.message);
				if (result.state != MRMacroExecutionState::Completed) logEvents.push_back(event);
				break;
			}
		}
	}
	for (std::size_t index = 0; index < logEvents.size(); ++index) {
		const MRRuntimeSchedulerEvent &event = logEvents[index];

		mrLogMessage(runtimeSchedulerEventLine(event).c_str());
	}
}

void noteRuntimeSchedulerExecutionSessionChanged() {
	recordRuntimeScheduledConsumerFinishResults(recentMacroExecutionResults());
}

void installRuntimeSchedulerExecutionSessionListener() {
	std::lock_guard<std::mutex> lock(runtimeSchedulerSessionListenerMutex);

	if (runtimeSchedulerSessionListenerId != 0) return;
	runtimeSchedulerSessionListenerId = addMacroExecutionSessionListener(noteRuntimeSchedulerExecutionSessionChanged);
}

void recordRuntimeScheduledConsumerStartResult(MRRuntimeScheduledConsumerId consumerId, const MRMacroExecutionSession &session, bool accepted, const std::string &message) {
	MRRuntimeSchedulerEvent event;
	bool recorded = false;
	{
		std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
		MRRuntimeScheduledConsumer consumer;

		if (!mrvmReadRuntimeScheduledConsumer(consumerId, consumer)) return;
		if (accepted) {
			consumer.activeSessionId = runtimeExecutionSessionStillActive(session) ? session.sessionId : 0;
			static_cast<void>(mrvmUpdateRuntimeScheduledConsumerActiveSession(consumer.consumerId, consumer.activeSessionId));
			event = recordRuntimeSchedulerEventLocked(consumerId, consumer.config, MRRuntimeSchedulerEventKind::TickStarted, MRRuntimeSchedulerSkipReason::None, session.sessionId, 0, 0, 0, message);
		} else
			event = recordRuntimeSchedulerEventLocked(consumerId, consumer.config, MRRuntimeSchedulerEventKind::TickStartFailed, MRRuntimeSchedulerSkipReason::None, 0, 0, 0, 0, message);
		recorded = true;
	}
	if (recorded && !accepted) mrLogMessage(runtimeSchedulerEventLine(event).c_str());
	if (accepted) recordRuntimeScheduledConsumerFinishResults(recentMacroExecutionResults());
}

std::string runtimeSchedulerEventLine(const MRRuntimeSchedulerEvent &event) {
	std::string line = "MRMac runtime scheduler: ";

	line += runtimeSchedulerEventKindText(event.kind);
	line += " consumer #";
	line += std::to_string(event.consumerId);
	if (!event.macroSpec.empty()) {
		line += " macro='";
		line += event.macroSpec;
		line += "'";
	}
	if (event.owner.hasBuffer) {
		line += " buffer #";
		line += std::to_string(event.owner.bufferId);
	}
	if (!event.owner.modelessWindowId.empty()) {
		line += " modeless-window '";
		line += event.owner.modelessWindowId;
		line += "'";
	}
	if (event.sessionId != 0) {
		line += " session #";
		line += std::to_string(event.sessionId);
	}
	if (event.blockingSessionId != 0) {
		line += " blocking-session #";
		line += std::to_string(event.blockingSessionId);
	}
	if (event.dueAtMs != 0) {
		line += " due-ms=";
		line += std::to_string(event.dueAtMs);
	}
	if (event.observedAtMs != 0) {
		line += " observed-ms=";
		line += std::to_string(event.observedAtMs);
	}
	if (event.skipReason != MRRuntimeSchedulerSkipReason::None) {
		line += " reason=";
		line += runtimeSchedulerSkipReasonText(event.skipReason);
	}
	if (!event.message.empty()) {
		line += " ";
		line += event.message;
	}
	line += ".";
	return line;
}

std::size_t removeRuntimeScheduledConsumersLocked(const std::string *macroSpec, const MRMacroExecutionOwner *owner, const std::string *consumerKey) {
	std::vector<MRRuntimeScheduledConsumer> consumers = mrvmRuntimeScheduledConsumers();
	std::size_t removed = 0;

	for (std::size_t index = 0; index < consumers.size(); ++index) {
		const MRRuntimeScheduledConsumer &consumer = consumers[index];

		if (macroSpec != nullptr && consumer.config.macroSpec != *macroSpec) continue;
		if (owner != nullptr && !macroExecutionOwnerMatches(consumer.config.owner, *owner)) continue;
		if (consumerKey != nullptr && consumer.config.consumerKey != *consumerKey) continue;
		recordRuntimeSchedulerEventLocked(consumer.consumerId, consumer.config, MRRuntimeSchedulerEventKind::ConsumerRemoved, MRRuntimeSchedulerSkipReason::None, consumer.activeSessionId, 0, 0, 0, std::string());
		mrvmRemoveRuntimeScheduledConsumer(consumer.consumerId);
		++removed;
	}
	mrvmStoreRuntimeSchedulerNextPumpMs(0);
	return removed;
}
} // namespace

MRRuntimeScheduledConsumerId registerRuntimeScheduledConsumer(const MRRuntimeScheduledConsumerConfig &config) {
	installRuntimeSchedulerExecutionSessionListener();

	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
	MRRuntimeScheduledConsumer consumer;

	if (config.intervalMs == 0 || (config.macroSpec.empty() && config.macroSource.empty())) return 0;
	consumer.consumerId = mrvmNextRuntimeScheduledConsumerId();
	consumer.config = config;
	mrvmStoreRuntimeScheduledConsumer(consumer);
	mrvmStoreRuntimeSchedulerNextPumpMs(0);
	recordRuntimeSchedulerEventLocked(consumer.consumerId, consumer.config, MRRuntimeSchedulerEventKind::ConsumerRegistered, MRRuntimeSchedulerSkipReason::None, 0, 0, 0, 0, std::string());
	return consumer.consumerId;
}

bool removeRuntimeScheduledConsumer(MRRuntimeScheduledConsumerId consumerId) {
	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
	MRRuntimeScheduledConsumer consumer;

	if (!mrvmReadRuntimeScheduledConsumer(consumerId, consumer)) return false;
	recordRuntimeSchedulerEventLocked(consumerId, consumer.config, MRRuntimeSchedulerEventKind::ConsumerRemoved, MRRuntimeSchedulerSkipReason::None, consumer.activeSessionId, 0, 0, 0, std::string());
	mrvmRemoveRuntimeScheduledConsumer(consumerId);
	mrvmStoreRuntimeSchedulerNextPumpMs(0);
	return true;
}

std::size_t removeRuntimeScheduledConsumersForMacroSpec(const std::string &macroSpec) {
	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);

	return removeRuntimeScheduledConsumersLocked(&macroSpec, nullptr, nullptr);
}

std::size_t removeRuntimeScheduledConsumersForOwner(const MRMacroExecutionOwner &owner) {
	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);

	return removeRuntimeScheduledConsumersLocked(nullptr, &owner, nullptr);
}

std::size_t removeRuntimeScheduledConsumersForOwnerAndKey(const MRMacroExecutionOwner &owner, const std::string &consumerKey) {
	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);

	return removeRuntimeScheduledConsumersLocked(nullptr, &owner, &consumerKey);
}

std::vector<MRRuntimeScheduledConsumer> runtimeScheduledConsumers() {
	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
	return mrvmRuntimeScheduledConsumers();
}

std::vector<MRRuntimeSchedulerEvent> recentRuntimeSchedulerEvents() {
	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
	return mrvmRecentRuntimeSchedulerEvents();
}

bool runtimeScheduledConsumerTickMayStart(MRRuntimeScheduledConsumerId consumerId, MRMacroExecutionSessionId *blockingSessionId) {
	MRRuntimeSchedulerEvent skippedEvent;
	bool skipped = false;
	{
		std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
		MRRuntimeScheduledConsumer consumer;

		if (blockingSessionId != nullptr) *blockingSessionId = 0;
		if (!mrvmReadRuntimeScheduledConsumer(consumerId, consumer)) return false;
		if (consumer.activeSessionId == 0) return true;
		if (blockingSessionId != nullptr) *blockingSessionId = consumer.activeSessionId;
		if (consumer.config.overrunPolicy != MRRuntimeScheduleOverrunPolicy::Skip) return false;
		skippedEvent = recordRuntimeSchedulerEventLocked(consumerId, consumer.config, MRRuntimeSchedulerEventKind::TickSkipped, MRRuntimeSchedulerSkipReason::PreviousSessionStillActive, 0, consumer.activeSessionId, 0, 0, "tick skipped; previous session is still active");
		skipped = true;
	}
	if (skipped) mrLogMessage(runtimeSchedulerEventLine(skippedEvent).c_str());
	return false;
}

bool noteRuntimeScheduledConsumerStarted(MRRuntimeScheduledConsumerId consumerId, MRMacroExecutionSessionId sessionId) {
	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
	MRRuntimeScheduledConsumer consumer;

	if (!mrvmReadRuntimeScheduledConsumer(consumerId, consumer) || sessionId == 0) return false;
	consumer.activeSessionId = sessionId;
	static_cast<void>(mrvmUpdateRuntimeScheduledConsumerActiveSession(consumer.consumerId, consumer.activeSessionId));
	recordRuntimeSchedulerEventLocked(consumerId, consumer.config, MRRuntimeSchedulerEventKind::TickStarted, MRRuntimeSchedulerSkipReason::None, sessionId, 0, 0, 0, std::string());
	return true;
}

bool noteRuntimeScheduledConsumerFinished(MRRuntimeScheduledConsumerId consumerId, MRMacroExecutionSessionId sessionId) {
	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
	MRRuntimeScheduledConsumer consumer;

	if (!mrvmReadRuntimeScheduledConsumer(consumerId, consumer) || sessionId == 0 || consumer.activeSessionId != sessionId) return false;
	consumer.activeSessionId = 0;
	static_cast<void>(mrvmUpdateRuntimeScheduledConsumerActiveSession(consumer.consumerId, consumer.activeSessionId));
	return true;
}

std::size_t pumpRuntimeScheduler(std::uint64_t nowMs) {
	std::vector<MRRuntimeSchedulerEvent> logEvents;
	std::vector<RuntimeSchedulerDueConsumer> dueConsumers;
	{
		std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
		std::vector<MRRuntimeScheduledConsumerId> consumerIds = mrvmRuntimeScheduledConsumerIds();
		const std::uint64_t nextPumpMs = mrvmRuntimeSchedulerNextPumpMs();
		std::uint64_t nextDueCandidate = 0;

		if (consumerIds.empty()) {
			noteRuntimeSchedulerObservedConsumersLocked(0);
			return 0;
		}
		if (nextPumpMs != 0 && nowMs < nextPumpMs) return 0;
		noteRuntimeSchedulerObservedConsumersLocked(consumerIds.size());
		for (std::size_t consumerIndex = 0; consumerIndex < consumerIds.size(); ++consumerIndex) {
			const MRRuntimeScheduledConsumerId consumerId = consumerIds[consumerIndex];
			std::uint64_t intervalMs = 0;
			MRMacroExecutionSessionId activeSessionId = 0;
			std::uint64_t nextDueMs = 0;
			MRRuntimeScheduledConsumer consumer;

			if (!mrvmReadRuntimeScheduledConsumerSchedule(consumerId, intervalMs, activeSessionId, nextDueMs)) continue;
			if (nextDueMs == 0) nextDueMs = nowMs;
			if (nowMs < nextDueMs) {
				noteRuntimeSchedulerNextDue(nextDueCandidate, nextDueMs);
				continue;
			}
			if (!mrvmReadRuntimeScheduledConsumer(consumerId, consumer)) continue;

			const std::uint64_t dueAtMs = nextDueMs;
			consumer.nextDueMs = nowMs + intervalMs;
			static_cast<void>(mrvmUpdateRuntimeScheduledConsumerNextDue(consumerId, consumer.nextDueMs));
			noteRuntimeSchedulerNextDue(nextDueCandidate, consumer.nextDueMs);
			if (activeSessionId != 0) {
				logEvents.push_back(recordRuntimeSchedulerEventLocked(consumer.consumerId, consumer.config, MRRuntimeSchedulerEventKind::TickSkipped, MRRuntimeSchedulerSkipReason::PreviousSessionStillActive, 0, activeSessionId, dueAtMs, nowMs, "tick skipped; previous session is still active"));
				continue;
			}
			logEvents.push_back(recordRuntimeSchedulerEventLocked(consumer.consumerId, consumer.config, MRRuntimeSchedulerEventKind::TickDue, MRRuntimeSchedulerSkipReason::None, 0, 0, dueAtMs, nowMs, "tick due; requesting execution session"));
			RuntimeSchedulerDueConsumer dueConsumer;
			dueConsumer.consumerId = consumer.consumerId;
			dueConsumer.config = consumer.config;
			dueConsumer.dueAtMs = dueAtMs;
			dueConsumer.observedAtMs = nowMs;
			dueConsumers.push_back(dueConsumer);
		}
		mrvmStoreRuntimeSchedulerNextPumpMs(nextDueCandidate);
	}
	for (std::size_t eventIndex = 0; eventIndex < logEvents.size(); ++eventIndex) {
		const MRRuntimeSchedulerEvent &event = logEvents[eventIndex];

		if (runtimeSchedulerEventShouldEnterVisibleLog(event)) mrLogMessage(runtimeSchedulerEventLine(event).c_str());
	}
	for (std::size_t dueIndex = 0; dueIndex < dueConsumers.size(); ++dueIndex) {
		const RuntimeSchedulerDueConsumer &dueConsumer = dueConsumers[dueIndex];
		MRMacroExecutionSession session;
		std::string errorText;
		bool accepted = false;
		if (dueConsumer.config.macroSource.empty()) {
			std::string debugSourcePath;
			std::string debugMacroKey;

			if (mrvmMacroSpecHasEnabledDebugBreakpoint(dueConsumer.config.macroSpec, &debugSourcePath, &debugMacroKey) && mrMacroDebuggerObservesSourceIdentity(debugSourcePath, debugMacroKey)) {
				const MRMacroDebugRunResult debugResult = mrvmStartDebugMacroBySpec(dueConsumer.config.macroSpec, dueConsumer.config.owner, &session, &errorText);

				accepted = !debugResult.hadError && !debugResult.cancelled;
				if (debugResult.paused && !mrAttachScheduledMacroDebuggerSession(session.sessionId, debugResult)) {
					static_cast<void>(mrvmCloseDebugSession(session.sessionId));
					errorText = "Scheduled debug session has no observing debugger.";
					accepted = false;
				}
			} else if (dueConsumer.config.owner.modelessWindowId.empty())
				accepted = runMacroSpecByNameAsExecutionSessionForOwner(dueConsumer.config.macroSpec.c_str(), dueConsumer.config.owner, &session, &errorText, false);
			else
				accepted = runMacroSpecByNameAsExecutionSessionForOwnerOnUiThread(dueConsumer.config.macroSpec.c_str(), dueConsumer.config.owner, &session, &errorText, false);
		}
		else if (dueConsumer.config.entryName.empty())
			accepted = runMacroSourceTextAsExecutionSessionForOwner(dueConsumer.config.macroSpec.c_str(), dueConsumer.config.macroSource.c_str(), dueConsumer.config.owner, &session, &errorText, false);
		else
			accepted = runMacroSourceUnitAsExecutionSessionForOwner(dueConsumer.config.macroSpec.c_str(), dueConsumer.config.macroSource.c_str(), dueConsumer.config.entryName.c_str(), dueConsumer.config.closureId.c_str(), dueConsumer.config.owner, &session, &errorText, false);
		std::string message = accepted ? "scheduled macro accepted" : errorText;
		if (message.empty()) message = "scheduled macro execution failed";
		recordRuntimeScheduledConsumerStartResult(dueConsumer.consumerId, session, accepted, message);
	}
	return logEvents.size();
}

std::vector<std::string> runtimeSchedulerStatusLines(std::size_t maxEvents) {
	const std::vector<MRRuntimeScheduledConsumer> consumers = runtimeScheduledConsumers();
	const std::vector<MRRuntimeSchedulerEvent> events = recentRuntimeSchedulerEvents();
	const std::size_t eventStart = events.size() > maxEvents ? events.size() - maxEvents : 0;
	std::vector<std::string> lines;

	lines.push_back("MRMac runtime scheduler: consumers=" + std::to_string(consumers.size()) + ", recent-events=" + std::to_string(events.size()) + ".");
	for (std::size_t index = 0; index < consumers.size(); ++index) {
		const MRRuntimeScheduledConsumer &consumer = consumers[index];
		std::string line = "scheduled consumer #";

		line += std::to_string(consumer.consumerId);
		line += " interval-ms=";
		line += std::to_string(consumer.config.intervalMs);
		if (!consumer.config.macroSpec.empty()) {
			line += " macro='";
			line += consumer.config.macroSpec;
			line += "'";
		}
		if (!consumer.config.macroSource.empty()) line += " source-package";
		if (!consumer.config.closureId.empty()) {
			line += " closure='";
			line += consumer.config.closureId;
			line += "'";
		}
		if (!consumer.config.consumerKey.empty()) {
			line += " key='";
			line += consumer.config.consumerKey;
			line += "'";
		}
		if (consumer.config.owner.hasBuffer) {
			line += " buffer #";
			line += std::to_string(consumer.config.owner.bufferId);
		}
		if (!consumer.config.owner.modelessWindowId.empty()) {
			line += " modeless-window '";
			line += consumer.config.owner.modelessWindowId;
			line += "'";
		}
		if (consumer.activeSessionId != 0) {
			line += " active-session #";
			line += std::to_string(consumer.activeSessionId);
		}
		lines.push_back(line);
	}
	for (std::size_t i = eventStart; i < events.size(); ++i)
		lines.push_back(runtimeSchedulerEventLine(events[i]));
	return lines;
}

void installRuntimeSchedulerSmokeIfEnabled() {
	std::vector<MRRuntimeScheduledConsumer> consumers;
	MRRuntimeScheduledConsumerConfig config;
	MRRuntimeScheduledConsumerId consumerId = 0;

	if (!runtimeSchedulerSmokeEnabled()) return;
	consumers = runtimeScheduledConsumers();
	for (std::size_t index = 0; index < consumers.size(); ++index) {
		const MRRuntimeScheduledConsumer &consumer = consumers[index];

		if (consumer.config.macroSpec != "RuntimeSchedulerSmoke") continue;
		consumerId = consumer.consumerId;
		break;
	}
	if (consumerId == 0) {
		config.intervalMs = 1000;
		config.macroSpec = "RuntimeSchedulerSmoke";
		config.macroSource = runtimeSchedulerSmokeSource();
		consumerId = registerRuntimeScheduledConsumer(config);
	}
	mrLogMessage(("MRMac runtime scheduler smoke installed consumer=" + std::to_string(consumerId) + ".").c_str());
}

void logRuntimeSchedulerStatusIfEnabled() {
	if (!runtimeSchedulerSmokeEnabled()) return;
	const std::vector<std::string> lines = runtimeSchedulerStatusLines(8);

	for (std::size_t index = 0; index < lines.size(); ++index) {
		const std::string &line = lines[index];

		mrLogMessage(line.c_str());
	}
}
