#include "MRCoprocessor.hpp"

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

namespace mr {
namespace coprocessor {
namespace {

std::uint64_t nowMicros() noexcept {
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

Coprocessor::Coprocessor()
    : resultMutex(), results(), handlerMutex(), resultHandler(), nextTaskId(1), nextWorkerOrdinal(0), taskCancelMutex(), taskCancelFlags(), shuttingDown(false), allowedCoreIds(detectAllowedCoreIds()), workerMutex(), workers(), retiringWorkers(), nextExternalSourceId(1), nextExternalActivitySequence(1), externalMutex(), externalSources(), telemetryMutex(), lifecycleEvents(), nextLifecycleSequence(1), createdWorkerCount(0), finishedWorkerCount(0), affinityFailureCount(0), oneShotCreatedByTaskKind(), oneShotFinishedByTaskKind(), oneShotCreatedByOwnerKind(), oneShotFinishedByOwnerKind(), telemetryActiveTaskCount(0), telemetryQueuedTaskCount(0), telemetryResultCount(0) {
}

Coprocessor::~Coprocessor() {
	shutdown();
}

void Coprocessor::setResultHandler(ResultHandler handler) {
	std::lock_guard<std::mutex> lock(handlerMutex);
	resultHandler = std::move(handler);
}

std::uint64_t Coprocessor::submit(Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, ExecutionOwnerKind ownerKind, std::size_t ownerLocalId, std::string_view label, TaskFn fn) {
	return submitOneShot(lane, kind, documentId, baseVersion, ownerKind, ownerLocalId, 0, WorkDirection::None, false, 0, 0, label, std::move(fn));
}

std::uint64_t Coprocessor::submitPacket(Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, ExecutionOwnerKind ownerKind, std::size_t ownerLocalId, std::uint64_t generation, WorkDirection direction, std::uint64_t packetStart, std::uint64_t packetEnd, std::string_view label, TaskFn fn) {
	return submitOneShot(lane, kind, documentId, baseVersion, ownerKind, ownerLocalId, generation, direction, true, packetStart, packetEnd, label, std::move(fn));
}

std::uint64_t Coprocessor::submitWorker(std::uint64_t workerOrdinal, TaskKind kind, std::size_t documentId, std::size_t baseVersion, std::string_view label, TaskFn fn) {
	if (workerOrdinal == kInvalidWorkerOrdinal) return 0;
	std::lock_guard<std::mutex> lock(workerMutex);
	if (shuttingDown.load(std::memory_order_acquire)) return 0;
	LaneState *worker = findWorkerLocked(workerOrdinal);
	if (worker == nullptr) return 0;
	return submitToLaneState(*worker, worker->lane, kind, documentId, baseVersion, 0, WorkDirection::None, false, 0, 0, label, std::move(fn));
}

std::uint64_t Coprocessor::submitOneShot(Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, ExecutionOwnerKind ownerKind, std::size_t ownerLocalId, std::uint64_t generation, WorkDirection direction, bool hasPacketSpan, std::uint64_t packetStart, std::uint64_t packetEnd, std::string_view label, TaskFn fn) {
	std::uint64_t workerOrdinal = kInvalidWorkerOrdinal;
	std::uint64_t taskId = 0;
	reapRetiredWorkers();
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		if (shuttingDown.load(std::memory_order_acquire)) return 0;
		std::unique_ptr<LaneState> worker = std::make_unique<LaneState>(lane, true, ownerKind, ownerLocalId);
		startLane(*worker);
		workerOrdinal = worker->workerOrdinal;
		LaneState *registeredWorker = worker.get();
		workers.push_back(std::move(worker));
		recordOneShotWorkerCreated(kind, ownerKind);
		if (registeredWorker != nullptr)
			taskId = submitToLaneState(*registeredWorker, lane, kind, documentId, baseVersion, generation, direction, hasPacketSpan, packetStart, packetEnd, label, std::move(fn));
	}
	if (taskId == 0) unregisterWorker(workerOrdinal);
	return taskId;
}

std::uint64_t Coprocessor::submitToLaneState(LaneState &targetLaneState, Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, std::uint64_t generation, WorkDirection direction, bool hasPacketSpan, std::uint64_t packetStart, std::uint64_t packetEnd, std::string_view label, TaskFn fn) {
	if (shuttingDown.load(std::memory_order_acquire)) return 0;

	Request request;
	const std::uint64_t taskId = nextTaskId.fetch_add(1, std::memory_order_relaxed);
	TaskInfo queuedTask;
	request.task.id = taskId;
	request.task.cancelFlag = std::make_shared<std::atomic_bool>(false);
	request.task.lane = lane;
	request.task.kind = kind;
	request.task.documentId = documentId;
	request.task.baseVersion = baseVersion;
	request.task.label = label;
	request.task.generation = generation;
	request.task.direction = direction;
	request.task.hasPacketSpan = hasPacketSpan;
	request.task.packetStart = packetStart;
	request.task.packetEnd = packetEnd;
	request.fn = std::move(fn);
	request.submittedMicros = nowMicros();

	{
		std::lock_guard<std::mutex> lock(taskCancelMutex);
		taskCancelFlags[taskId] = request.task.cancelFlag;
	}

	{
		std::lock_guard<std::mutex> lock(targetLaneState.mutex);
		request.task.workerOrdinal = targetLaneState.workerOrdinal;
		request.task.osThreadId = targetLaneState.osThreadId;
		request.task.assignedCore = targetLaneState.assignedCore;
		request.task.affinityResult = targetLaneState.affinityResult;
		request.task.executionOwnerKind = targetLaneState.executionOwnerKind;
		request.task.executionOwnerLocalId = targetLaneState.executionOwnerLocalId;
		request.task.finiteWorker = targetLaneState.retireAfterTask;
		queuedTask = request.task;
		if (targetLaneState.lifecycleState != WorkerLifecycleState::Running) {
			targetLaneState.lifecycleState = WorkerLifecycleState::Queued;
			targetLaneState.currentTask = request.task;
			targetLaneState.submittedMicros = request.submittedMicros;
			targetLaneState.startedMicros = 0;
			targetLaneState.finishedMicros = 0;
		}
		targetLaneState.queue.push_back(std::move(request));
		recordTaskLifecycle(WorkerLifecycleState::Queued, queuedTask, TaskStatus::Completed);
	}
	targetLaneState.cv.notify_one();
	return taskId;
}

std::size_t Coprocessor::registerExternalSource(ExternalSourceKind kind, std::string_view displayName) {
	static constexpr unsigned char kSourceColors[] = {0x30, 0x20, 0x60, 0x70, 0x50};
	const std::size_t sourceId = static_cast<std::size_t>(nextExternalSourceId.fetch_add(1, std::memory_order_relaxed));
	ExternalSourceState source;

	source.sourceId = sourceId;
	source.kind = kind;
	switch (kind) {
		case ExternalSourceKind::Journal:
			source.tag = "JOU";
			break;
		case ExternalSourceKind::Device:
			source.tag = "DEV";
			break;
		case ExternalSourceKind::Network:
			source.tag = "NET";
			break;
		case ExternalSourceKind::Pipe:
			source.tag = "PIP";
			break;
		case ExternalSourceKind::File:
		default:
			source.tag = "FIL";
			break;
	}
	source.displayName.assign(displayName.data(), displayName.size());
	source.colorIndex = kSourceColors[(sourceId - 1) % (sizeof(kSourceColors) / sizeof(kSourceColors[0]))];
	source.running = false;
	source.active = true;
	source.taskId = 0;
	source.receivedBytes = 0;
	source.lane = std::make_unique<LaneState>(Lane::Extern, false, ExecutionOwnerKind::ExternalSource, sourceId);

	{
		std::lock_guard<std::mutex> lock(externalMutex);
		externalSources.push_back(std::move(source));
	}
	return sourceId;
}

std::uint64_t Coprocessor::submitExternal(std::size_t sourceId, std::string_view label, TaskFn fn) {
	LaneState *targetLane = nullptr;
	{
		std::lock_guard<std::mutex> lock(externalMutex);
		ExternalSourceState *source = findExternalSourceLocked(sourceId);
		if (source == nullptr || source->lane == nullptr) return 0;
		targetLane = source->lane.get();
		if (!targetLane->worker.joinable()) startLane(*targetLane);
	}

	const std::uint64_t taskId = submitToLaneState(*targetLane, Lane::Extern, TaskKind::ExternalIo, sourceId, 0, 0, WorkDirection::None, false, 0, 0, label, std::move(fn));
	if (taskId != 0) {
		std::lock_guard<std::mutex> lock(externalMutex);
		ExternalSourceState *source = findExternalSourceLocked(sourceId);
		if (source != nullptr) {
			source->running = true;
			source->taskId = taskId;
		}
	}
	return taskId;
}

bool Coprocessor::cancelExternalSource(std::size_t sourceId) {
	std::uint64_t taskId = 0;

	{
		std::lock_guard<std::mutex> lock(externalMutex);
		ExternalSourceState *source = findExternalSourceLocked(sourceId);
		if (source == nullptr) return false;
		taskId = source->taskId;
	}
	return taskId != 0 && cancelTask(taskId);
}

void Coprocessor::unregisterExternalSource(std::size_t sourceId) {
	std::unique_ptr<LaneState> removedLane;

	cancelExternalSource(sourceId);
	{
		std::lock_guard<std::mutex> lock(externalMutex);
		for (std::vector<ExternalSourceState>::iterator it = externalSources.begin(); it != externalSources.end(); ++it) {
			if (it->sourceId != sourceId) continue;
			if (it->lane != nullptr) {
				requestLaneStop(*it->lane);
				removedLane = std::move(it->lane);
			}
			externalSources.erase(it);
			break;
		}
	}
	if (removedLane != nullptr) {
		std::lock_guard<std::mutex> lock(workerMutex);
		retiringWorkers.push_back(std::move(removedLane));
	}
}

std::size_t Coprocessor::pump(std::size_t maxResults) {
	std::size_t drained = 0;

	while (drained < maxResults) {
		Result result;
		ResultHandler handler;

		{
			std::lock_guard<std::mutex> lock(resultMutex);
			if (results.empty()) break;
			result = std::move(results.front());
			results.pop_front();
		}

		{
			std::lock_guard<std::mutex> lock(handlerMutex);
			handler = resultHandler;
		}

		if (handler) handler(result);
		noteResultAdoption(result, handler && result.completed());
		++drained;
	}
	reapRetiredWorkers();
	return drained;
}

std::size_t Coprocessor::pumpFor(std::chrono::microseconds budget) {
	if (budget <= std::chrono::microseconds::zero()) return 0;

	const std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
	std::size_t drained = 0;

	for (;;) {
		Result result;
		ResultHandler handler;

		{
			std::lock_guard<std::mutex> lock(resultMutex);
			if (results.empty()) break;
			result = std::move(results.front());
			results.pop_front();
		}

		{
			std::lock_guard<std::mutex> lock(handlerMutex);
			handler = resultHandler;
		}

		if (handler) handler(result);
		noteResultAdoption(result, handler && result.completed());
		++drained;
		if (std::chrono::steady_clock::now() - startedAt >= budget) break;
	}
	reapRetiredWorkers();
	return drained;
}

std::size_t Coprocessor::pendingResults() const noexcept {
	std::lock_guard<std::mutex> lock(resultMutex);
	return results.size();
}

bool Coprocessor::hasTaskState(std::uint64_t taskId) noexcept {
	if (taskId == 0) return false;
	{
		std::lock_guard<std::mutex> lock(taskCancelMutex);
		if (taskCancelFlags.find(taskId) != taskCancelFlags.end()) return true;
	}
	{
		std::lock_guard<std::mutex> lock(resultMutex);
		for (const Result &result : results)
			if (result.task.id == taskId) return true;
	}
	return false;
}

void Coprocessor::post(Result result) {
	enqueueResult(std::move(result));
}

bool Coprocessor::cancelTask(std::uint64_t taskId) {
	std::shared_ptr<std::atomic_bool> cancelFlag;

	{
		std::lock_guard<std::mutex> lock(taskCancelMutex);
		std::unordered_map<std::uint64_t, std::shared_ptr<std::atomic_bool>>::iterator cancelFlagIt = taskCancelFlags.find(taskId);
		if (cancelFlagIt == taskCancelFlags.end()) return false;
		cancelFlag = cancelFlagIt->second;
	}
	if (cancelFlag != nullptr) cancelFlag->store(true, std::memory_order_release);
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		for (const std::unique_ptr<LaneState> &worker : workers)
			if (worker != nullptr) worker->cv.notify_all();
	}
	{
		std::lock_guard<std::mutex> lock(externalMutex);
		for (ExternalSourceState &source : externalSources)
			if (source.lane != nullptr) source.lane->cv.notify_all();
	}
	return true;
}

void Coprocessor::enqueueResult(Result result) {
	LifecycleReason reason = LifecycleReason::TaskCompleted;

	result.resultReadyMicros = nowMicros();
	if (dynamic_cast<const ExternalIoChunkPayload *>(result.payload.get()) != nullptr || dynamic_cast<const TaskProgressPayload *>(result.payload.get()) != nullptr) {
		reason = LifecycleReason::StreamChunk;
	} else {
		switch (result.status) {
			case TaskStatus::Cancelled:
				reason = LifecycleReason::TaskCancelled;
				break;
			case TaskStatus::Failed:
				reason = LifecycleReason::TaskFailed;
				break;
			case TaskStatus::Completed:
			default:
				break;
		}
	}
	recordTaskLifecycle(WorkerLifecycleState::ResultReady, result.task, result.status, &result.timing, reason);
	noteExternalResult(result);
	std::lock_guard<std::mutex> lock(resultMutex);
	results.push_back(std::move(result));
}

void Coprocessor::forgetTask(std::uint64_t taskId) {
	std::lock_guard<std::mutex> lock(taskCancelMutex);
	taskCancelFlags.erase(taskId);
}

void Coprocessor::noteExternalResult(const Result &result) {
	const ExternalIoChunkPayload *chunk = dynamic_cast<const ExternalIoChunkPayload *>(result.payload.get());
	const std::size_t sourceId = chunk != nullptr ? chunk->channelId : result.task.documentId;

	if (sourceId == 0) return;
	std::lock_guard<std::mutex> lock(externalMutex);
	ExternalSourceState *source = findExternalSourceLocked(sourceId);
	if (source == nullptr) return;

	if (chunk != nullptr) {
		static constexpr std::size_t kMaxStreamSample = 240;
		std::string incoming;

		source->active = true;
		source->running = true;
		source->receivedBytes += chunk->text.size();
		source->activitySequence = nextExternalActivitySequence.fetch_add(1, std::memory_order_relaxed);
		incoming.reserve(std::min<std::size_t>(kMaxStreamSample, chunk->text.size()));
		for (char ch : chunk->text) {
			unsigned char byte = static_cast<unsigned char>(ch);
			if (byte < 32 || byte == 127) ch = ' ';
			incoming.push_back(ch);
			if (incoming.size() >= kMaxStreamSample) break;
		}
		source->streamSample.insert(0, incoming);
		if (source->streamSample.size() > kMaxStreamSample) source->streamSample.resize(kMaxStreamSample);
		return;
	}

	if (result.task.lane == Lane::Extern && result.task.kind == TaskKind::ExternalIo) {
		source->running = false;
		source->taskId = 0;
	}
}

Coprocessor::ExternalSourceState *Coprocessor::findExternalSourceLocked(std::size_t sourceId) noexcept {
	for (ExternalSourceState &source : externalSources)
		if (source.sourceId == sourceId) return &source;
	return nullptr;
}

const Coprocessor::ExternalSourceState *Coprocessor::findExternalSourceLocked(std::size_t sourceId) const noexcept {
	for (const ExternalSourceState &source : externalSources)
		if (source.sourceId == sourceId) return &source;
	return nullptr;
}

Coprocessor::LaneState *Coprocessor::findWorkerLocked(std::uint64_t workerOrdinal) noexcept {
	for (const std::unique_ptr<LaneState> &worker : workers)
		if (worker != nullptr && worker->workerOrdinal == workerOrdinal) return worker.get();
	return nullptr;
}

Snapshot Coprocessor::snapshot() const {
	Snapshot snapshot;
	const std::uint64_t currentMicros = nowMicros();
	auto appendLaneState = [&](LaneSnapshot &laneSnapshot, const LaneState &lane, std::size_t workerSlotBase) {
		std::lock_guard<std::mutex> lock(lane.mutex);
		if (lane.worker.joinable()) ++laneSnapshot.workerCount;
		for (const ActiveTaskState &activeTask : lane.activeTasks) {
			ActiveTaskSnapshot activeSnapshot;
			activeSnapshot.workerSlot = workerSlotBase + activeTask.workerSlot;
			activeSnapshot.task = activeTask.task;
			if (activeTask.startedMicros >= activeTask.submittedMicros) activeSnapshot.queueMicros = activeTask.startedMicros - activeTask.submittedMicros;
			if (currentMicros >= activeTask.startedMicros) activeSnapshot.runMicros = currentMicros - activeTask.startedMicros;
			laneSnapshot.activeTasks.push_back(std::move(activeSnapshot));
		}
		for (const Request &request : lane.queue)
			laneSnapshot.queuedTasks.push_back(request.task);
	};
	auto finishLaneSnapshot = [](LaneSnapshot &laneSnapshot) noexcept {
		laneSnapshot.active = !laneSnapshot.activeTasks.empty();
		if (!laneSnapshot.active) return;
		laneSnapshot.activeTask = laneSnapshot.activeTasks.front().task;
		laneSnapshot.activeQueueMicros = laneSnapshot.activeTasks.front().queueMicros;
		laneSnapshot.activeRunMicros = laneSnapshot.activeTasks.front().runMicros;
	};
	snapshot.pendingResults = pendingResults();
	snapshot.lanes.resize(4);
	snapshot.lanes[0].lane = Lane::Io;
	snapshot.lanes[1].lane = Lane::Compute;
	snapshot.lanes[2].lane = Lane::MiniMap;
	snapshot.lanes[3].lane = Lane::Macro;
	for (LaneSnapshot &laneSnapshot : snapshot.lanes)
		laneSnapshot.workerCount = 0;
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		for (const std::unique_ptr<LaneState> &worker : workers) {
			if (worker == nullptr) continue;
			std::size_t laneIndex = 0;
			switch (worker->lane) {
				case Lane::Io:
					laneIndex = 0;
					break;
				case Lane::Compute:
					laneIndex = 1;
					break;
				case Lane::MiniMap:
					laneIndex = 2;
					break;
				case Lane::Macro:
					laneIndex = 3;
					break;
				case Lane::Extern:
				default:
					continue;
			}
			LaneSnapshot &laneSnapshot = snapshot.lanes[laneIndex];
			const std::size_t workerSlotBase = laneSnapshot.workerCount;
			appendLaneState(laneSnapshot, *worker, workerSlotBase);
		}
	}
	for (LaneSnapshot &laneSnapshot : snapshot.lanes)
		finishLaneSnapshot(laneSnapshot);
	{
		std::lock_guard<std::mutex> lock(externalMutex);
		snapshot.externalSources.reserve(externalSources.size());
		for (const ExternalSourceState &source : externalSources) {
			ExternalSourceSnapshot sourceSnapshot;

			sourceSnapshot.sourceId = source.sourceId;
			sourceSnapshot.kind = source.kind;
			sourceSnapshot.tag = source.tag;
			sourceSnapshot.displayName = source.displayName;
			sourceSnapshot.colorIndex = source.colorIndex;
			sourceSnapshot.running = source.running;
			sourceSnapshot.active = source.active;
			sourceSnapshot.taskId = source.taskId;
			sourceSnapshot.receivedBytes = source.receivedBytes;
			sourceSnapshot.activitySequence = source.activitySequence;
			sourceSnapshot.streamSample = source.streamSample;
			snapshot.externalSources.push_back(std::move(sourceSnapshot));
		}
	}
	return snapshot;
}

bool Coprocessor::laneHasQueuedWorkLocked(const LaneState &lane) const noexcept {
	return !lane.queue.empty();
}

bool Coprocessor::popNextRequestLocked(LaneState &lane, Request &request) noexcept {
	if (lane.queue.empty()) return false;
	request = std::move(lane.queue.front());
	lane.queue.pop_front();
	return true;
}

Coprocessor &globalCoprocessor() {
	static Coprocessor instance;
	return instance;
}

} // namespace coprocessor
} // namespace mr
