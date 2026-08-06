#ifndef MRRUNTIMESCHEDULER_HPP
#define MRRUNTIMESCHEDULER_HPP

#include "../mrmac/MRMacroExecutionSession.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using MRRuntimeScheduledConsumerId = std::uint64_t;
using MRRuntimeSchedulerEventId = std::uint64_t;

enum class MRRuntimeScheduleOverrunPolicy {
	Skip = 0
};

enum class MRRuntimeSchedulerEventKind {
	ConsumerRegistered = 0,
	ConsumerRemoved,
	TickDue,
	TickStarted,
	TickFinished,
	TickStartFailed,
	TickSkipped
};

enum class MRRuntimeSchedulerSkipReason {
	None = 0,
	PreviousSessionStillActive,
	ConsumerNotFound
};

struct MRRuntimeScheduledConsumerConfig {
	MRMacroExecutionOwner owner;
	std::uint64_t intervalMs = 0;
	std::string macroSpec;
	std::string macroSource;
	std::string entryName;
	std::string closureId;
	std::string consumerKey;
	MRRuntimeScheduleOverrunPolicy overrunPolicy = MRRuntimeScheduleOverrunPolicy::Skip;
};

struct MRRuntimeScheduledConsumer {
	MRRuntimeScheduledConsumerId consumerId = 0;
	MRRuntimeScheduledConsumerConfig config;
	MRMacroExecutionSessionId activeSessionId = 0;
	std::uint64_t nextDueMs = 0;
};

struct MRRuntimeSchedulerEvent {
	MRRuntimeSchedulerEventId eventId = 0;
	MRRuntimeScheduledConsumerId consumerId = 0;
	MRMacroExecutionOwner owner;
	MRRuntimeSchedulerEventKind kind = MRRuntimeSchedulerEventKind::ConsumerRegistered;
	MRRuntimeSchedulerSkipReason skipReason = MRRuntimeSchedulerSkipReason::None;
	MRMacroExecutionSessionId sessionId = 0;
	MRMacroExecutionSessionId blockingSessionId = 0;
	std::uint64_t dueAtMs = 0;
	std::uint64_t observedAtMs = 0;
	std::string macroSpec;
	std::string message;
};

MRRuntimeScheduledConsumerId registerRuntimeScheduledConsumer(const MRRuntimeScheduledConsumerConfig &config);
bool removeRuntimeScheduledConsumer(MRRuntimeScheduledConsumerId consumerId);
std::size_t removeRuntimeScheduledConsumersForOwner(const MRMacroExecutionOwner &owner);
std::size_t removeRuntimeScheduledConsumersForOwnerAndKey(const MRMacroExecutionOwner &owner, const std::string &consumerKey);
std::vector<MRRuntimeScheduledConsumer> runtimeScheduledConsumers();
std::vector<MRRuntimeSchedulerEvent> recentRuntimeSchedulerEvents();
bool runtimeScheduledConsumerTickMayStart(MRRuntimeScheduledConsumerId consumerId, MRMacroExecutionSessionId *blockingSessionId);
bool noteRuntimeScheduledConsumerStarted(MRRuntimeScheduledConsumerId consumerId, MRMacroExecutionSessionId sessionId);
bool noteRuntimeScheduledConsumerFinished(MRRuntimeScheduledConsumerId consumerId, MRMacroExecutionSessionId sessionId);
std::size_t pumpRuntimeScheduler(std::uint64_t nowMs);
std::vector<std::string> runtimeSchedulerStatusLines(std::size_t maxEvents);

#endif
