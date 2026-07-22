#include "MRCoprocessor.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>

#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace mr {
namespace coprocessor {
namespace {

constexpr std::size_t kLifecycleEventCapacity = 4096;

std::uint64_t telemetryNowMicros() noexcept {
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

std::vector<int> Coprocessor::detectAllowedCoreIds() noexcept {
	std::vector<int> coreIds;
	cpu_set_t availableSet;

	CPU_ZERO(&availableSet);
	if (sched_getaffinity(0, sizeof(availableSet), &availableSet) == 0) {
		for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
			if (CPU_ISSET(cpu, &availableSet)) coreIds.push_back(cpu);
	}
	if (!coreIds.empty()) return coreIds;

	const long onlineCoreCount = sysconf(_SC_NPROCESSORS_ONLN);
	if (onlineCoreCount <= 0) return coreIds;
	coreIds.reserve(static_cast<std::size_t>(onlineCoreCount));
	for (int cpu = 0; cpu < onlineCoreCount; ++cpu)
		coreIds.push_back(cpu);
	return coreIds;
}

std::size_t Coprocessor::allowedCoreCount() const noexcept {
	return allowedCoreIds.size();
}

int Coprocessor::assignCurrentWorkerCore(std::uint64_t workerOrdinal, int &assignedCore, std::uint64_t &osThreadId) const noexcept {
	const long threadId = syscall(SYS_gettid);
	osThreadId = threadId > 0 ? static_cast<std::uint64_t>(threadId) : 0;
	assignedCore = -1;
	if (allowedCoreIds.empty()) return ENODEV;

	assignedCore = allowedCoreIds[static_cast<std::size_t>(workerOrdinal % allowedCoreIds.size())];
	cpu_set_t targetSet;
	CPU_ZERO(&targetSet);
	CPU_SET(assignedCore, &targetSet);
	return pthread_setaffinity_np(pthread_self(), sizeof(targetSet), &targetSet);
}

void Coprocessor::recordWorkerLifecycle(WorkerLifecycleState state, const LaneState &lane, const TaskInfo *task, TaskStatus taskStatus, const TaskTiming *timing, LifecycleReason reason) {
	WorkerLifecycleEvent event;

	event.monotonicMicros = telemetryNowMicros();
	event.state = state;
	event.reason = reason;
	event.taskStatus = taskStatus;
	{
		std::lock_guard<std::mutex> laneLock(lane.mutex);
		event.workerOrdinal = lane.workerOrdinal;
		event.osThreadId = lane.osThreadId;
		event.assignedCore = lane.assignedCore;
		event.affinityResult = lane.affinityResult;
		event.executionOwnerKind = lane.executionOwnerKind;
		event.executionOwnerLocalId = lane.executionOwnerLocalId;
		event.lane = lane.lane;
		if (task != nullptr) {
			event.executionOwnerKind = task->executionOwnerKind;
			event.executionOwnerLocalId = task->executionOwnerLocalId;
			event.taskKind = task->kind;
			event.taskId = task->id;
			event.documentId = task->documentId;
			event.baseVersion = task->baseVersion;
			event.generation = task->generation;
			event.direction = task->direction;
			event.hasPacketSpan = task->hasPacketSpan;
			event.packetStart = task->packetStart;
			event.packetEnd = task->packetEnd;
		}
	}
	if (timing != nullptr) {
		event.queueMicros = timing->queueMicros;
		event.runMicros = timing->runMicros;
		event.totalMicros = timing->totalMicros;
	}

	std::lock_guard<std::mutex> lock(telemetryMutex);
	event.sequence = nextLifecycleSequence++;
	if (state == WorkerLifecycleState::Created) ++createdWorkerCount;
	if (state == WorkerLifecycleState::Finished) ++finishedWorkerCount;
	if (state == WorkerLifecycleState::Assigned && event.affinityResult != 0) ++affinityFailureCount;
	if (state == WorkerLifecycleState::Running) {
		if (telemetryQueuedTaskCount != 0) --telemetryQueuedTaskCount;
		++telemetryActiveTaskCount;
	}
	lifecycleEvents.push_back(std::move(event));
	if (lifecycleEvents.size() > kLifecycleEventCapacity) lifecycleEvents.pop_front();
}

void Coprocessor::recordTaskLifecycle(WorkerLifecycleState state, const TaskInfo &task, TaskStatus taskStatus, const TaskTiming *timing, LifecycleReason reason, std::uint64_t decisionMicros) {
	WorkerLifecycleEvent event;

	event.monotonicMicros = telemetryNowMicros();
	event.state = state;
	event.reason = reason;
	event.taskStatus = taskStatus;
	event.workerOrdinal = task.workerOrdinal;
	event.osThreadId = task.osThreadId;
	event.assignedCore = task.assignedCore;
	event.affinityResult = task.affinityResult;
	event.executionOwnerKind = task.executionOwnerKind;
	event.executionOwnerLocalId = task.executionOwnerLocalId;
	event.lane = task.lane;
	event.taskKind = task.kind;
	event.taskId = task.id;
	event.documentId = task.documentId;
	event.baseVersion = task.baseVersion;
	event.generation = task.generation;
	event.direction = task.direction;
	event.hasPacketSpan = task.hasPacketSpan;
	event.packetStart = task.packetStart;
	event.packetEnd = task.packetEnd;
	if (timing != nullptr) {
		event.queueMicros = timing->queueMicros;
		event.runMicros = timing->runMicros;
		event.totalMicros = timing->totalMicros + decisionMicros;
	}
	if (state == WorkerLifecycleState::Accepted) event.acceptanceMicros = decisionMicros;
	else if (state == WorkerLifecycleState::Adopted || state == WorkerLifecycleState::Discarded)
		event.adoptionMicros = decisionMicros;

	std::lock_guard<std::mutex> lock(telemetryMutex);
	event.sequence = nextLifecycleSequence++;
	switch (state) {
		case WorkerLifecycleState::Queued:
			++telemetryQueuedTaskCount;
			break;
		case WorkerLifecycleState::ResultReady:
			if (reason != LifecycleReason::StreamChunk && telemetryActiveTaskCount != 0) --telemetryActiveTaskCount;
			++telemetryResultCount;
			break;
		case WorkerLifecycleState::Accepted:
			if (telemetryResultCount != 0) --telemetryResultCount;
			break;
		case WorkerLifecycleState::Adopted:
			if (reason == LifecycleReason::ResultAdopted && telemetryResultCount != 0) --telemetryResultCount;
			break;
		case WorkerLifecycleState::Discarded:
			switch (reason) {
				case LifecycleReason::ResultRejected:
					if (telemetryResultCount != 0) --telemetryResultCount;
					break;
				case LifecycleReason::TaskCancelled:
					if (telemetryQueuedTaskCount != 0) --telemetryQueuedTaskCount;
					break;
				default:
					break;
			}
			break;
		default:
			break;
	}
	lifecycleEvents.push_back(std::move(event));
	if (lifecycleEvents.size() > kLifecycleEventCapacity) lifecycleEvents.pop_front();
}

void Coprocessor::recordOneShotWorkerCreated(TaskKind taskKind, ExecutionOwnerKind ownerKind) noexcept {
	const std::size_t taskIndex = static_cast<std::size_t>(taskKind);
	const std::size_t ownerIndex = static_cast<std::size_t>(ownerKind);
	std::lock_guard<std::mutex> lock(telemetryMutex);
	if (taskIndex < oneShotCreatedByTaskKind.size()) ++oneShotCreatedByTaskKind[taskIndex];
	if (ownerIndex < oneShotCreatedByOwnerKind.size()) ++oneShotCreatedByOwnerKind[ownerIndex];
}

void Coprocessor::recordOneShotWorkerFinished(TaskKind taskKind, ExecutionOwnerKind ownerKind) noexcept {
	const std::size_t taskIndex = static_cast<std::size_t>(taskKind);
	const std::size_t ownerIndex = static_cast<std::size_t>(ownerKind);
	std::lock_guard<std::mutex> lock(telemetryMutex);
	if (taskIndex < oneShotFinishedByTaskKind.size()) ++oneShotFinishedByTaskKind[taskIndex];
	if (ownerIndex < oneShotFinishedByOwnerKind.size()) ++oneShotFinishedByOwnerKind[ownerIndex];
}

void Coprocessor::markLaneStopping(LaneState &lane) {
	bool changed = false;
	TaskInfo task;
	{
		std::lock_guard<std::mutex> lock(lane.mutex);
		if (lane.lifecycleState != WorkerLifecycleState::Stopping && lane.lifecycleState != WorkerLifecycleState::Finished) {
			lane.lifecycleState = WorkerLifecycleState::Stopping;
			task = lane.currentTask;
			changed = true;
		}
	}
	if (changed) recordWorkerLifecycle(WorkerLifecycleState::Stopping, lane, &task, TaskStatus::Cancelled, nullptr, LifecycleReason::StopRequested);
}

void Coprocessor::noteResultAdoption(const Result &result, bool adopted) {
	std::uint64_t adoptionMicros = 0;
	const std::uint64_t currentMicros = telemetryNowMicros();

	if (result.dispositionRecorded) return;
	result.dispositionRecorded = true;
	if (result.resultReadyMicros != 0 && currentMicros >= result.resultReadyMicros) adoptionMicros = currentMicros - result.resultReadyMicros;
	recordTaskLifecycle(adopted ? WorkerLifecycleState::Adopted : WorkerLifecycleState::Discarded, result.task, result.status, &result.timing, adopted ? LifecycleReason::ResultAdopted : LifecycleReason::ResultRejected, adoptionMicros);
}

DeferredResultLifecycle Coprocessor::acceptResultForDeferredAdoption(const Result &result) {
	DeferredResultLifecycle lifecycle;
	std::uint64_t acceptanceMicros = 0;
	const std::uint64_t currentMicros = telemetryNowMicros();

	if (result.dispositionRecorded) return lifecycle;
	result.dispositionRecorded = true;
	lifecycle.task = result.task;
	lifecycle.task.label.clear();
	lifecycle.task.cancelFlag.reset();
	lifecycle.taskStatus = result.status;
	lifecycle.timing = result.timing;
	lifecycle.resultReadyMicros = result.resultReadyMicros;
	lifecycle.valid = true;
	if (result.resultReadyMicros != 0 && currentMicros >= result.resultReadyMicros) acceptanceMicros = currentMicros - result.resultReadyMicros;
	recordTaskLifecycle(WorkerLifecycleState::Accepted, lifecycle.task, lifecycle.taskStatus, &lifecycle.timing, LifecycleReason::ResultAccepted, acceptanceMicros);
	return lifecycle;
}

void Coprocessor::resolveDeferredResultAdoption(DeferredResultLifecycle &lifecycle, bool adopted) {
	std::uint64_t adoptionMicros = 0;
	const std::uint64_t currentMicros = telemetryNowMicros();

	if (!lifecycle.valid) return;
	if (lifecycle.resultReadyMicros != 0 && currentMicros >= lifecycle.resultReadyMicros) adoptionMicros = currentMicros - lifecycle.resultReadyMicros;
	recordTaskLifecycle(adopted ? WorkerLifecycleState::Adopted : WorkerLifecycleState::Discarded, lifecycle.task, lifecycle.taskStatus, &lifecycle.timing,
	                    adopted ? LifecycleReason::AcceptedResultAdopted : LifecycleReason::AcceptedResultRejected, adoptionMicros);
	lifecycle.valid = false;
}

WorkerTelemetrySnapshot Coprocessor::telemetrySnapshot(std::size_t maxEvents) const {
	WorkerTelemetrySnapshot snapshot;
	const std::uint64_t currentMicros = telemetryNowMicros();
	auto appendLane = [&snapshot, currentMicros](const LaneState &lane) {
		WorkerSnapshot worker;
		std::lock_guard<std::mutex> lock(lane.mutex);
		worker.workerOrdinal = lane.workerOrdinal;
		worker.osThreadId = lane.osThreadId;
		worker.assignedCore = lane.assignedCore;
		worker.affinityResult = lane.affinityResult;
		worker.executionOwnerKind = lane.executionOwnerKind;
		worker.executionOwnerLocalId = lane.executionOwnerLocalId;
		worker.lane = lane.lane;
		worker.state = lane.lifecycleState;
		worker.task = lane.currentTask;
		worker.queuedTaskCount = lane.queue.size();
		if (lane.startedMicros != 0 && lane.startedMicros >= lane.submittedMicros) worker.queueMicros = lane.startedMicros - lane.submittedMicros;
		if (lane.lifecycleState == WorkerLifecycleState::Running && currentMicros >= lane.startedMicros) worker.runMicros = currentMicros - lane.startedMicros;
		else if (lane.finishedMicros != 0 && lane.finishedMicros >= lane.startedMicros)
			worker.runMicros = lane.finishedMicros - lane.startedMicros;
		if (worker.task.id != 0) {
			worker.executionOwnerKind = worker.task.executionOwnerKind;
			worker.executionOwnerLocalId = worker.task.executionOwnerLocalId;
		}
		snapshot.workers.push_back(std::move(worker));
	};

	snapshot.allowedCoreIds = allowedCoreIds;
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		for (const std::unique_ptr<LaneState> &worker : workers)
			if (worker != nullptr) appendLane(*worker);
		for (const std::unique_ptr<LaneState> &lane : retiringWorkers)
			if (lane != nullptr) appendLane(*lane);
	}
	{
		std::lock_guard<std::mutex> lock(externalMutex);
		for (const ExternalSourceState &source : externalSources)
			if (source.lane != nullptr) appendLane(*source.lane);
	}
	std::sort(snapshot.workers.begin(), snapshot.workers.end(), [](const WorkerSnapshot &left, const WorkerSnapshot &right) { return left.workerOrdinal < right.workerOrdinal; });

	{
		std::lock_guard<std::mutex> lock(telemetryMutex);
		const std::uint64_t activityCutoff = currentMicros >= snapshot.recentActivity.windowMicros ? currentMicros - snapshot.recentActivity.windowMicros : 0;
		std::uint64_t historicalActiveCount = telemetryActiveTaskCount;
		std::uint64_t historicalQueuedCount = telemetryQueuedTaskCount;
		std::uint64_t historicalResultCount = telemetryResultCount;
		snapshot.recentActivity.peakActiveCount = historicalActiveCount;
		snapshot.recentActivity.peakQueuedCount = historicalQueuedCount;
		snapshot.recentActivity.peakResultCount = historicalResultCount;
		snapshot.latestEventSequence = nextLifecycleSequence > 0 ? nextLifecycleSequence - 1 : 0;
		snapshot.createdCount = createdWorkerCount;
		snapshot.finishedCount = finishedWorkerCount;
		snapshot.affinityFailureCount = affinityFailureCount;
		snapshot.oneShotCreatedByTaskKind = oneShotCreatedByTaskKind;
		snapshot.oneShotFinishedByTaskKind = oneShotFinishedByTaskKind;
		snapshot.oneShotCreatedByOwnerKind = oneShotCreatedByOwnerKind;
		snapshot.oneShotFinishedByOwnerKind = oneShotFinishedByOwnerKind;
		for (std::deque<WorkerLifecycleEvent>::const_reverse_iterator it = lifecycleEvents.rbegin(); it != lifecycleEvents.rend(); ++it) {
			if (it->monotonicMicros < activityCutoff) break;
			switch (it->state) {
				case WorkerLifecycleState::Created:
					++snapshot.recentActivity.createdCount;
					break;
				case WorkerLifecycleState::Queued:
					++snapshot.recentActivity.queuedCount;
					if (historicalQueuedCount != 0) --historicalQueuedCount;
					break;
				case WorkerLifecycleState::Running:
					if (historicalActiveCount != 0) --historicalActiveCount;
					++historicalQueuedCount;
					break;
				case WorkerLifecycleState::ResultReady:
					snapshot.recentActivity.queueMicros += it->queueMicros;
					snapshot.recentActivity.runMicros += it->runMicros;
					if (it->reason != LifecycleReason::StreamChunk) ++historicalActiveCount;
					if (historicalResultCount != 0) --historicalResultCount;
					switch (it->taskStatus) {
						case TaskStatus::Cancelled:
							++snapshot.recentActivity.cancelledCount;
							break;
						case TaskStatus::Failed:
							++snapshot.recentActivity.failedCount;
							break;
						case TaskStatus::Completed:
						default:
							++snapshot.recentActivity.completedCount;
							break;
					}
					break;
				case WorkerLifecycleState::Accepted:
					++snapshot.recentActivity.acceptedCount;
					snapshot.recentActivity.acceptanceMicros += it->acceptanceMicros;
					++historicalResultCount;
					break;
				case WorkerLifecycleState::Adopted:
					++snapshot.recentActivity.adoptedCount;
					snapshot.recentActivity.adoptionMicros += it->adoptionMicros;
					if (it->reason == LifecycleReason::ResultAdopted) ++historicalResultCount;
					break;
				case WorkerLifecycleState::Discarded:
					++snapshot.recentActivity.discardedCount;
					snapshot.recentActivity.adoptionMicros += it->adoptionMicros;
					switch (it->reason) {
						case LifecycleReason::ResultRejected:
							++historicalResultCount;
							break;
						case LifecycleReason::TaskCancelled:
							++historicalQueuedCount;
							break;
						default:
							break;
					}
					break;
				case WorkerLifecycleState::Finished:
					++snapshot.recentActivity.finishedCount;
					break;
				case WorkerLifecycleState::Assigned:
				case WorkerLifecycleState::Idle:
				case WorkerLifecycleState::Stopping:
				default:
					break;
			}
			snapshot.recentActivity.peakActiveCount = std::max(snapshot.recentActivity.peakActiveCount, historicalActiveCount);
			snapshot.recentActivity.peakQueuedCount = std::max(snapshot.recentActivity.peakQueuedCount, historicalQueuedCount);
			snapshot.recentActivity.peakResultCount = std::max(snapshot.recentActivity.peakResultCount, historicalResultCount);
		}
		const std::size_t eventCount = std::min(maxEvents, lifecycleEvents.size());
		snapshot.recentEvents.reserve(eventCount);
		for (std::deque<WorkerLifecycleEvent>::const_reverse_iterator it = lifecycleEvents.rbegin(); it != lifecycleEvents.rend() && snapshot.recentEvents.size() < eventCount; ++it)
			snapshot.recentEvents.push_back(*it);
	}
	return snapshot;
}

} // namespace coprocessor
} // namespace mr
