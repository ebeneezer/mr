#include "MRRuntimeScheduler.hpp"

#include "../mrmac/MRMacroRunner.hpp"
#include "../ui/MRWindowSupport.hpp"

#include <cstdlib>
#include <map>
#include <mutex>

namespace {
std::mutex runtimeSchedulerMutex;
std::mutex runtimeSchedulerSessionListenerMutex;
MRRuntimeScheduledConsumerId nextRuntimeScheduledConsumerValue = 1;
MRRuntimeSchedulerEventId nextRuntimeSchedulerEventValue = 1;
MRMacroExecutionSessionListenerId runtimeSchedulerSessionListenerId = 0;
std::map<MRRuntimeScheduledConsumerId, MRRuntimeScheduledConsumer> runtimeScheduledConsumerMap;
std::vector<MRRuntimeSchedulerEvent> runtimeSchedulerEvents;
constexpr std::size_t kRuntimeSchedulerEventHistoryLimit = 64;

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

void trimRuntimeSchedulerEvents() {
	while (runtimeSchedulerEvents.size() > kRuntimeSchedulerEventHistoryLimit)
		runtimeSchedulerEvents.erase(runtimeSchedulerEvents.begin());
}

void recordRuntimeSchedulerEventLocked(MRRuntimeScheduledConsumerId consumerId, const MRRuntimeScheduledConsumerConfig &config, MRRuntimeSchedulerEventKind kind, MRRuntimeSchedulerSkipReason skipReason, MRMacroExecutionSessionId sessionId, MRMacroExecutionSessionId blockingSessionId, std::uint64_t dueAtMs, std::uint64_t observedAtMs, const std::string &message) {
	MRRuntimeSchedulerEvent event;

	event.eventId = nextRuntimeSchedulerEventValue++;
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
	runtimeSchedulerEvents.push_back(event);
	trimRuntimeSchedulerEvents();
}

bool runtimeExecutionSessionStillActive(const MRMacroExecutionSession &session) noexcept {
	return session.sessionId != 0 && (session.state == MRMacroExecutionState::Running || session.state == MRMacroExecutionState::Yielded || session.state == MRMacroExecutionState::CancellationRequested);
}

void recordRuntimeScheduledConsumerFinishResults(const std::vector<MRMacroExecutionResult> &results) {
	std::vector<MRRuntimeSchedulerEvent> logEvents;
	{
		std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
		for (const MRMacroExecutionResult &result : results) {
			if (result.session.sessionId == 0) continue;
			for (std::map<MRRuntimeScheduledConsumerId, MRRuntimeScheduledConsumer>::iterator it = runtimeScheduledConsumerMap.begin(); it != runtimeScheduledConsumerMap.end(); ++it) {
				MRRuntimeScheduledConsumer &consumer = it->second;
				if (consumer.activeSessionId != result.session.sessionId) continue;
				consumer.activeSessionId = 0;
				recordRuntimeSchedulerEventLocked(consumer.consumerId, consumer.config, MRRuntimeSchedulerEventKind::TickFinished, MRRuntimeSchedulerSkipReason::None, result.session.sessionId, 0, 0, 0, result.message);
				logEvents.push_back(runtimeSchedulerEvents.back());
				break;
			}
		}
	}
	for (const MRRuntimeSchedulerEvent &event : logEvents)
		mrLogMessage(runtimeSchedulerEventLine(event).c_str());
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
		std::map<MRRuntimeScheduledConsumerId, MRRuntimeScheduledConsumer>::iterator found = runtimeScheduledConsumerMap.find(consumerId);

		if (found == runtimeScheduledConsumerMap.end()) return;
		if (accepted) {
			found->second.activeSessionId = runtimeExecutionSessionStillActive(session) ? session.sessionId : 0;
			recordRuntimeSchedulerEventLocked(consumerId, found->second.config, MRRuntimeSchedulerEventKind::TickStarted, MRRuntimeSchedulerSkipReason::None, session.sessionId, 0, 0, 0, message);
		} else
			recordRuntimeSchedulerEventLocked(consumerId, found->second.config, MRRuntimeSchedulerEventKind::TickStartFailed, MRRuntimeSchedulerSkipReason::None, 0, 0, 0, 0, message);
		event = runtimeSchedulerEvents.back();
		recorded = true;
	}
	if (recorded) mrLogMessage(runtimeSchedulerEventLine(event).c_str());
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
} // namespace

MRRuntimeScheduledConsumerId registerRuntimeScheduledConsumer(const MRRuntimeScheduledConsumerConfig &config) {
	installRuntimeSchedulerExecutionSessionListener();

	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
	MRRuntimeScheduledConsumer consumer;

	if (config.intervalMs == 0 || (config.macroSpec.empty() && config.macroSource.empty())) return 0;
	consumer.consumerId = nextRuntimeScheduledConsumerValue++;
	consumer.config = config;
	runtimeScheduledConsumerMap[consumer.consumerId] = consumer;
	recordRuntimeSchedulerEventLocked(consumer.consumerId, consumer.config, MRRuntimeSchedulerEventKind::ConsumerRegistered, MRRuntimeSchedulerSkipReason::None, 0, 0, 0, 0, std::string());
	return consumer.consumerId;
}

bool removeRuntimeScheduledConsumer(MRRuntimeScheduledConsumerId consumerId) {
	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
	std::map<MRRuntimeScheduledConsumerId, MRRuntimeScheduledConsumer>::iterator found = runtimeScheduledConsumerMap.find(consumerId);

	if (found == runtimeScheduledConsumerMap.end()) return false;
	recordRuntimeSchedulerEventLocked(consumerId, found->second.config, MRRuntimeSchedulerEventKind::ConsumerRemoved, MRRuntimeSchedulerSkipReason::None, found->second.activeSessionId, 0, 0, 0, std::string());
	runtimeScheduledConsumerMap.erase(found);
	return true;
}

std::size_t removeRuntimeScheduledConsumersForMacroSpec(const std::string &macroSpec) {
	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
	std::size_t removed = 0;

	for (std::map<MRRuntimeScheduledConsumerId, MRRuntimeScheduledConsumer>::iterator it = runtimeScheduledConsumerMap.begin(); it != runtimeScheduledConsumerMap.end();) {
		if (it->second.config.macroSpec != macroSpec) {
			++it;
			continue;
		}
		recordRuntimeSchedulerEventLocked(it->first, it->second.config, MRRuntimeSchedulerEventKind::ConsumerRemoved, MRRuntimeSchedulerSkipReason::None, it->second.activeSessionId, 0, 0, 0, std::string());
		it = runtimeScheduledConsumerMap.erase(it);
		++removed;
	}
	return removed;
}

std::vector<MRRuntimeScheduledConsumer> runtimeScheduledConsumers() {
	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
	std::vector<MRRuntimeScheduledConsumer> consumers;

	consumers.reserve(runtimeScheduledConsumerMap.size());
	for (const auto &entry : runtimeScheduledConsumerMap)
		consumers.push_back(entry.second);
	return consumers;
}

std::vector<MRRuntimeSchedulerEvent> recentRuntimeSchedulerEvents() {
	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
	return runtimeSchedulerEvents;
}

bool runtimeScheduledConsumerTickMayStart(MRRuntimeScheduledConsumerId consumerId, MRMacroExecutionSessionId *blockingSessionId) {
	MRRuntimeSchedulerEvent skippedEvent;
	bool skipped = false;
	{
		std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
		std::map<MRRuntimeScheduledConsumerId, MRRuntimeScheduledConsumer>::iterator found = runtimeScheduledConsumerMap.find(consumerId);

		if (blockingSessionId != nullptr) *blockingSessionId = 0;
		if (found == runtimeScheduledConsumerMap.end()) return false;
		if (found->second.activeSessionId == 0) return true;
		if (blockingSessionId != nullptr) *blockingSessionId = found->second.activeSessionId;
		if (found->second.config.overrunPolicy != MRRuntimeScheduleOverrunPolicy::Skip) return false;
		recordRuntimeSchedulerEventLocked(consumerId, found->second.config, MRRuntimeSchedulerEventKind::TickSkipped, MRRuntimeSchedulerSkipReason::PreviousSessionStillActive, 0, found->second.activeSessionId, 0, 0, "tick skipped; previous session is still active");
		skippedEvent = runtimeSchedulerEvents.back();
		skipped = true;
	}
	if (skipped) mrLogMessage(runtimeSchedulerEventLine(skippedEvent).c_str());
	return false;
}

bool noteRuntimeScheduledConsumerStarted(MRRuntimeScheduledConsumerId consumerId, MRMacroExecutionSessionId sessionId) {
	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
	std::map<MRRuntimeScheduledConsumerId, MRRuntimeScheduledConsumer>::iterator found = runtimeScheduledConsumerMap.find(consumerId);

	if (found == runtimeScheduledConsumerMap.end() || sessionId == 0) return false;
	found->second.activeSessionId = sessionId;
	recordRuntimeSchedulerEventLocked(consumerId, found->second.config, MRRuntimeSchedulerEventKind::TickStarted, MRRuntimeSchedulerSkipReason::None, sessionId, 0, 0, 0, std::string());
	return true;
}

bool noteRuntimeScheduledConsumerFinished(MRRuntimeScheduledConsumerId consumerId, MRMacroExecutionSessionId sessionId) {
	std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);
	std::map<MRRuntimeScheduledConsumerId, MRRuntimeScheduledConsumer>::iterator found = runtimeScheduledConsumerMap.find(consumerId);

	if (found == runtimeScheduledConsumerMap.end() || sessionId == 0 || found->second.activeSessionId != sessionId) return false;
	found->second.activeSessionId = 0;
	return true;
}

std::size_t pumpRuntimeScheduler(std::uint64_t nowMs) {
	std::vector<MRRuntimeSchedulerEvent> logEvents;
	std::vector<RuntimeSchedulerDueConsumer> dueConsumers;
	{
		std::lock_guard<std::mutex> lock(runtimeSchedulerMutex);

		for (std::map<MRRuntimeScheduledConsumerId, MRRuntimeScheduledConsumer>::iterator it = runtimeScheduledConsumerMap.begin(); it != runtimeScheduledConsumerMap.end(); ++it) {
			MRRuntimeScheduledConsumer &consumer = it->second;
			if (consumer.nextDueMs == 0) consumer.nextDueMs = nowMs;
			if (nowMs < consumer.nextDueMs) continue;

			const std::uint64_t dueAtMs = consumer.nextDueMs;
			consumer.nextDueMs = nowMs + consumer.config.intervalMs;
			if (consumer.activeSessionId != 0) {
				recordRuntimeSchedulerEventLocked(consumer.consumerId, consumer.config, MRRuntimeSchedulerEventKind::TickSkipped, MRRuntimeSchedulerSkipReason::PreviousSessionStillActive, 0, consumer.activeSessionId, dueAtMs, nowMs, "tick skipped; previous session is still active");
				logEvents.push_back(runtimeSchedulerEvents.back());
				continue;
			}
			recordRuntimeSchedulerEventLocked(consumer.consumerId, consumer.config, MRRuntimeSchedulerEventKind::TickDue, MRRuntimeSchedulerSkipReason::None, 0, 0, dueAtMs, nowMs, "tick due; requesting execution session");
			logEvents.push_back(runtimeSchedulerEvents.back());
			RuntimeSchedulerDueConsumer dueConsumer;
			dueConsumer.consumerId = consumer.consumerId;
			dueConsumer.config = consumer.config;
			dueConsumer.dueAtMs = dueAtMs;
			dueConsumer.observedAtMs = nowMs;
			dueConsumers.push_back(dueConsumer);
		}
	}
	for (const MRRuntimeSchedulerEvent &event : logEvents)
		mrLogMessage(runtimeSchedulerEventLine(event).c_str());
	for (const RuntimeSchedulerDueConsumer &dueConsumer : dueConsumers) {
		MRMacroExecutionSession session;
		std::string errorText;
		bool accepted = false;
		if (dueConsumer.config.macroSource.empty())
			accepted = runMacroSpecByNameAsExecutionSessionForOwner(dueConsumer.config.macroSpec.c_str(), dueConsumer.config.owner, &session, &errorText, false);
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
	for (const MRRuntimeScheduledConsumer &consumer : consumers) {
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
		if (consumer.config.owner.hasBuffer) {
			line += " buffer #";
			line += std::to_string(consumer.config.owner.bufferId);
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
	static MRRuntimeScheduledConsumerId consumerId = 0;
	MRRuntimeScheduledConsumerConfig config;

	if (!runtimeSchedulerSmokeEnabled()) return;
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
	for (const std::string &line : runtimeSchedulerStatusLines(8))
		mrLogMessage(line.c_str());
}
