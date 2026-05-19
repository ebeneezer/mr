#include "MRCoprocessor.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>
#include <vector>

#include <pthread.h>
#include <sched.h>
#include <unistd.h>

namespace mr {
namespace coprocessor {
namespace {

std::uint64_t nowMicros() noexcept {
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::vector<int> availableAffinityCores() noexcept {
	std::vector<int> availableCores;
	cpu_set_t availableSet;
	CPU_ZERO(&availableSet);
	if (sched_getaffinity(0, sizeof(availableSet), &availableSet) == 0) {
		for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
			if (CPU_ISSET(cpu, &availableSet)) availableCores.push_back(cpu);
	}

	if (!availableCores.empty()) return availableCores;

	const long onlineCoreCount = sysconf(_SC_NPROCESSORS_ONLN);
	if (onlineCoreCount <= 0) return availableCores;
	availableCores.reserve(static_cast<std::size_t>(onlineCoreCount));
	for (int cpu = 0; cpu < onlineCoreCount; ++cpu)
		availableCores.push_back(cpu);
	return availableCores;
}

unsigned int laneAffinitySlot(Lane lane) noexcept {
	switch (lane) {
		case Lane::Io:
			return 0;
		case Lane::Compute:
			return 1;
		case Lane::MiniMap:
			return 2;
		case Lane::Macro:
			return 3;
		case Lane::Extern:
			return 4;
	}
	return 0;
}

void bindCurrentThreadToLaneCore(Lane lane, std::size_t workerSlot) noexcept {
	const std::vector<int> availableCores = availableAffinityCores();
	if (availableCores.empty()) return;

	const unsigned int laneSlot = laneAffinitySlot(lane);
	const int targetCore = availableCores[(laneSlot + static_cast<unsigned int>(workerSlot)) % availableCores.size()];
	cpu_set_t targetSet;
	CPU_ZERO(&targetSet);
	CPU_SET(targetCore, &targetSet);
	(void)pthread_setaffinity_np(pthread_self(), sizeof(targetSet), &targetSet);
}

} // namespace

Coprocessor::Coprocessor() : nextTaskId(1), shuttingDown(false), ioLane(Lane::Io), computeLane(Lane::Compute), miniMapLane(Lane::MiniMap), macroLane(Lane::Macro), nextExternalSourceId(1), nextExternalActivitySequence(1), externalMutex(), externalSources() {
	startLane(ioLane);
	startLane(computeLane);
	startLane(miniMapLane);
	startLane(macroLane);
}

Coprocessor::~Coprocessor() {
	shutdown();
}

void Coprocessor::setResultHandler(ResultHandler handler) {
	std::lock_guard<std::mutex> lock(handlerMutex);
	resultHandler = std::move(handler);
}

std::uint64_t Coprocessor::submit(Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, std::string_view label, TaskFn fn) {
	return submitCoalesced(lane, kind, documentId, baseVersion, std::string_view(), label, std::move(fn));
}

std::uint64_t Coprocessor::submitCoalesced(Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, std::string_view coalescingKey, std::string_view label, TaskFn fn) {
	LaneState &targetLaneState = laneState(lane);

	return submitToLaneState(targetLaneState, lane, kind, documentId, baseVersion, coalescingKey, label, std::move(fn));
}

std::uint64_t Coprocessor::submitToLaneState(LaneState &targetLaneState, Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, std::string_view coalescingKey, std::string_view label, TaskFn fn) {
	if (shuttingDown.load(std::memory_order_acquire)) return 0;

	Request request;
	const std::uint64_t taskId = nextTaskId.fetch_add(1, std::memory_order_relaxed);
	std::vector<std::uint64_t> removedTaskIds;
	const bool allowCoalescing = kind != TaskKind::MacroJob && !coalescingKey.empty();
	request.task.id = taskId;
	request.task.cancelFlag = std::make_shared<std::atomic_bool>(false);
	request.task.lane = lane;
	request.task.kind = kind;
	request.task.documentId = documentId;
	request.task.baseVersion = baseVersion;
	request.task.label = label;
	request.fn = std::move(fn);
	if (allowCoalescing) request.coalescingKey.assign(coalescingKey.data(), coalescingKey.size());
	request.submittedMicros = nowMicros();
	request.computePriority = computePriorityForTask(kind);

	{
		std::lock_guard<std::mutex> lock(taskCancelMutex);
		taskCancelFlags[taskId] = request.task.cancelFlag;
	}

	{
		std::lock_guard<std::mutex> lock(targetLaneState.mutex);
		auto eraseMatching = [&](std::deque<Request> &queue) {
			for (std::deque<Request>::iterator it = queue.begin(); it != queue.end();) {
				if (it->task.kind == kind && it->coalescingKey == request.coalescingKey) {
					if (it->task.id != 0) removedTaskIds.push_back(it->task.id);
					it = queue.erase(it);
					continue;
				}
				++it;
			}
		};
		if (allowCoalescing) {
			eraseMatching(targetLaneState.queue);
			eraseMatching(targetLaneState.highQueue);
			eraseMatching(targetLaneState.normalQueue);
			eraseMatching(targetLaneState.lowQueue);
		}
		if (lane == Lane::Compute) {
			switch (request.computePriority) {
				case ComputePriority::High:
					targetLaneState.highQueue.push_back(std::move(request));
					break;
				case ComputePriority::Low:
					targetLaneState.lowQueue.push_back(std::move(request));
					break;
				case ComputePriority::Normal:
				default:
					targetLaneState.normalQueue.push_back(std::move(request));
					break;
			}
		} else {
			targetLaneState.queue.push_back(std::move(request));
		}
	}
	for (std::uint64_t removedTaskId : removedTaskIds)
		forgetTask(removedTaskId);
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
	source.lane = std::make_unique<LaneState>(Lane::Extern, sourceId);

	{
		std::lock_guard<std::mutex> lock(externalMutex);
		externalSources.push_back(std::move(source));
	}
	return sourceId;
}

std::uint64_t Coprocessor::submitExternal(std::size_t sourceId, std::string_view label, TaskFn fn) {
	LaneState *targetLane = nullptr;
	std::uint64_t taskId;

	{
		std::lock_guard<std::mutex> lock(externalMutex);
		ExternalSourceState *source = findExternalSourceLocked(sourceId);
		if (source == nullptr || source->lane == nullptr) return 0;
		targetLane = source->lane.get();
		if (targetLane->workers.empty()) startLane(*targetLane);
	}

	taskId = submitToLaneState(*targetLane, Lane::Extern, TaskKind::ExternalIo, sourceId, 0, std::string_view(), label, std::move(fn));
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
				for (std::jthread &worker : it->lane->workers)
					if (worker.joinable()) worker.request_stop();
				it->lane->cv.notify_all();
				removedLane = std::move(it->lane);
			}
			externalSources.erase(it);
			break;
		}
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
		++drained;
	}

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
		++drained;
		if (std::chrono::steady_clock::now() - startedAt >= budget) break;
	}

	return drained;
}

std::size_t Coprocessor::pendingResults() const noexcept {
	std::lock_guard<std::mutex> lock(resultMutex);
	return results.size();
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
	ioLane.cv.notify_all();
	computeLane.cv.notify_all();
	miniMapLane.cv.notify_all();
	macroLane.cv.notify_all();
	{
		std::lock_guard<std::mutex> lock(externalMutex);
		for (ExternalSourceState &source : externalSources)
			if (source.lane != nullptr) source.lane->cv.notify_all();
	}
	return true;
}

void Coprocessor::cancelPending() {
	std::vector<LaneState *> laneStates = {&ioLane, &computeLane, &miniMapLane, &macroLane};

	{
		std::lock_guard<std::mutex> lock(taskCancelMutex);
		for (auto &cancelEntry : taskCancelFlags)
			if (cancelEntry.second != nullptr) cancelEntry.second->store(true, std::memory_order_release);
	}
	{
		std::lock_guard<std::mutex> lock(externalMutex);
		for (ExternalSourceState &source : externalSources)
			if (source.lane != nullptr) laneStates.push_back(source.lane.get());
	}

	for (LaneState *lane : laneStates) {
		std::vector<std::uint64_t> clearedTaskIds;
		for (std::size_t workerIndex = 0; workerIndex < lane->workers.size(); ++workerIndex)
			if (lane->workers[workerIndex].joinable()) lane->workers[workerIndex].request_stop();
		{
			std::lock_guard<std::mutex> lock(lane->mutex);
			for (const Request &request : lane->queue)
				if (request.task.id != 0) clearedTaskIds.push_back(request.task.id);
			for (const Request &request : lane->highQueue)
				if (request.task.id != 0) clearedTaskIds.push_back(request.task.id);
			for (const Request &request : lane->normalQueue)
				if (request.task.id != 0) clearedTaskIds.push_back(request.task.id);
			for (const Request &request : lane->lowQueue)
				if (request.task.id != 0) clearedTaskIds.push_back(request.task.id);
			lane->queue.clear();
			lane->highQueue.clear();
			lane->normalQueue.clear();
			lane->lowQueue.clear();
			lane->activeTasks.clear();
		}
		for (std::uint64_t taskId : clearedTaskIds) forgetTask(taskId);
		lane->cv.notify_all();
	}
}

void Coprocessor::shutdown(bool drainResults) {
	if (shuttingDown.exchange(true, std::memory_order_acq_rel)) {
		if (drainResults)
			while (pump(64) != 0)
				;
		return;
	}

	cancelPending();

	auto joinLaneWorkers = [](LaneState &lane) {
		for (std::size_t workerIndex = 0; workerIndex < lane.workers.size(); ++workerIndex) {
			if (!lane.workers[workerIndex].joinable()) continue;
			std::jthread joinedWorker = std::move(lane.workers[workerIndex]);
		}
		lane.workers.clear();
	};

	joinLaneWorkers(ioLane);
	joinLaneWorkers(computeLane);
	joinLaneWorkers(miniMapLane);
	joinLaneWorkers(macroLane);
	{
		std::vector<LaneState *> externalLaneStates;
		{
			std::lock_guard<std::mutex> lock(externalMutex);
			externalLaneStates.reserve(externalSources.size());
			for (ExternalSourceState &source : externalSources)
				if (source.lane != nullptr) externalLaneStates.push_back(source.lane.get());
		}
		for (LaneState *lane : externalLaneStates)
			if (lane != nullptr) joinLaneWorkers(*lane);
		std::lock_guard<std::mutex> lock(externalMutex);
		externalSources.clear();
	}

	if (drainResults)
		while (pump(64) != 0)
			;

	{
		std::lock_guard<std::mutex> lock(resultMutex);
		results.clear();
	}
	{
		std::lock_guard<std::mutex> lock(handlerMutex);
		resultHandler = ResultHandler();
	}
	{
		std::lock_guard<std::mutex> lock(taskCancelMutex);
		taskCancelFlags.clear();
	}
}

void Coprocessor::startLane(LaneState &lane) {
	const std::size_t workerCount = laneWorkerCount(lane.lane);

	lane.workers.clear();
	lane.workers.reserve(workerCount);
	for (std::size_t workerSlot = 0; workerSlot < workerCount; ++workerSlot)
		lane.workers.push_back(std::jthread([this, &lane, workerSlot](std::stop_token stopToken) { workerLoop(lane, workerSlot, stopToken); }));
}

void Coprocessor::workerLoop(LaneState &lane, std::size_t workerSlot, std::stop_token stopToken) {
	bindCurrentThreadToLaneCore(lane.lane, workerSlot);

	for (;;) {
		Request request;
		Result result;
		std::uint64_t startedMicros = 0;
		std::uint64_t finishedMicros = 0;

		{
			std::unique_lock<std::mutex> lock(lane.mutex);
			lane.cv.wait(lock, stopToken, [this, &lane]() { return laneHasQueuedWorkLocked(lane); });
			if (stopToken.stop_requested() && !laneHasQueuedWorkLocked(lane)) break;
			if (!popNextRequestLocked(lane, request)) continue;
			startedMicros = nowMicros();
			ActiveTaskState activeTask;

			activeTask.workerSlot = workerSlot;
			activeTask.task = request.task;
			activeTask.submittedMicros = request.submittedMicros;
			activeTask.startedMicros = startedMicros;
			activeTask.computePriority = request.computePriority;
			lane.activeTasks.push_back(std::move(activeTask));
		}
		result.task = request.task;
		try {
			if (stopToken.stop_requested() || request.task.cancelRequested()) {
				result.status = TaskStatus::Cancelled;
			} else if (request.fn) {
				result = request.fn(request.task, stopToken);
				if (result.task.id == 0) result.task = request.task;
			} else {
				result.status = TaskStatus::Failed;
				result.error = "No task function provided.";
			}
		} catch (const std::exception &ex) {
			result.task = request.task;
			result.status = TaskStatus::Failed;
			result.error = ex.what();
		} catch (...) {
			result.task = request.task;
			result.status = TaskStatus::Failed;
			result.error = "Unknown coprocessor failure.";
		}
		finishedMicros = nowMicros();
		if (startedMicros >= request.submittedMicros) result.timing.queueMicros = startedMicros - request.submittedMicros;
		if (finishedMicros >= startedMicros) result.timing.runMicros = finishedMicros - startedMicros;
		if (finishedMicros >= request.submittedMicros) result.timing.totalMicros = finishedMicros - request.submittedMicros;

		{
			std::lock_guard<std::mutex> lock(lane.mutex);
			for (std::vector<ActiveTaskState>::iterator it = lane.activeTasks.begin(); it != lane.activeTasks.end(); ++it) {
				if (it->workerSlot != workerSlot || it->task.id != request.task.id) continue;
				lane.activeTasks.erase(it);
				break;
			}
		}
		forgetTask(request.task.id);
		enqueueResult(std::move(result));
	}
}

void Coprocessor::enqueueResult(Result result) {
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

Snapshot Coprocessor::snapshot() const {
	Snapshot snapshot;
	const LaneState *laneStates[] = {&ioLane, &computeLane, &miniMapLane, &macroLane};
	const std::uint64_t currentMicros = nowMicros();

	snapshot.pendingResults = pendingResults();
	snapshot.lanes.reserve(4);
	for (std::size_t laneIndex = 0; laneIndex < 4; ++laneIndex) {
		const LaneState &lane = *laneStates[laneIndex];
		LaneSnapshot laneSnapshot;

		laneSnapshot.lane = lane.lane;
		{
			std::lock_guard<std::mutex> lock(lane.mutex);
			laneSnapshot.workerCount = lane.workers.empty() ? 1 : lane.workers.size();
			laneSnapshot.activeTasks.reserve(lane.activeTasks.size());
			for (std::size_t activeIndex = 0; activeIndex < lane.activeTasks.size(); ++activeIndex) {
				ActiveTaskSnapshot activeSnapshot;
				const ActiveTaskState &activeTask = lane.activeTasks[activeIndex];

				activeSnapshot.workerSlot = activeTask.workerSlot;
				activeSnapshot.task = activeTask.task;
				if (activeTask.startedMicros >= activeTask.submittedMicros) activeSnapshot.queueMicros = activeTask.startedMicros - activeTask.submittedMicros;
				if (currentMicros >= activeTask.startedMicros) activeSnapshot.runMicros = currentMicros - activeTask.startedMicros;
				laneSnapshot.activeTasks.push_back(std::move(activeSnapshot));
			}
			laneSnapshot.active = !laneSnapshot.activeTasks.empty();
			if (laneSnapshot.active) {
				laneSnapshot.activeTask = laneSnapshot.activeTasks.front().task;
				laneSnapshot.activeQueueMicros = laneSnapshot.activeTasks.front().queueMicros;
				laneSnapshot.activeRunMicros = laneSnapshot.activeTasks.front().runMicros;
			}
			laneSnapshot.queuedTasks.reserve(lane.queue.size() + lane.highQueue.size() + lane.normalQueue.size() + lane.lowQueue.size());
			for (std::size_t taskIndex = 0; taskIndex < lane.highQueue.size(); ++taskIndex)
				laneSnapshot.queuedTasks.push_back(lane.highQueue[taskIndex].task);
			for (std::size_t taskIndex = 0; taskIndex < lane.normalQueue.size(); ++taskIndex)
				laneSnapshot.queuedTasks.push_back(lane.normalQueue[taskIndex].task);
			for (std::size_t taskIndex = 0; taskIndex < lane.lowQueue.size(); ++taskIndex)
				laneSnapshot.queuedTasks.push_back(lane.lowQueue[taskIndex].task);
			for (std::size_t taskIndex = 0; taskIndex < lane.queue.size(); ++taskIndex)
				laneSnapshot.queuedTasks.push_back(lane.queue[taskIndex].task);
		}
		snapshot.lanes.push_back(std::move(laneSnapshot));
	}
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

Coprocessor::ComputePriority Coprocessor::computePriorityForTask(TaskKind kind) const noexcept {
	switch (kind) {
		case TaskKind::LineIndexWarmup:
		case TaskKind::IndicatorBlink:
			return ComputePriority::High;
		case TaskKind::SyntaxWarmup:
		case TaskKind::FoldWarmup:
		case TaskKind::SaveNormalizationWarmup:
			return ComputePriority::Normal;
		case TaskKind::Custom:
		default:
			return ComputePriority::Low;
	}
}

bool Coprocessor::laneHasQueuedWorkLocked(const LaneState &lane) const noexcept {
	if (lane.lane == Lane::Compute) return !lane.highQueue.empty() || !lane.normalQueue.empty() || !lane.lowQueue.empty();
	return !lane.queue.empty();
}

bool Coprocessor::popNextRequestLocked(LaneState &lane, Request &request) noexcept {
	if (lane.lane != Lane::Compute) {
		if (lane.queue.empty()) return false;
		request = std::move(lane.queue.front());
		lane.queue.pop_front();
		return true;
	}

	std::size_t activeHighCount = 0;
	for (std::size_t activeIndex = 0; activeIndex < lane.activeTasks.size(); ++activeIndex)
		if (lane.activeTasks[activeIndex].computePriority == ComputePriority::High) ++activeHighCount;

	if (!lane.highQueue.empty()) {
		if (activeHighCount == 0 || lane.normalQueue.empty() || lane.activeTasks.size() <= 1) {
			request = std::move(lane.highQueue.front());
			lane.highQueue.pop_front();
			return true;
		}
	}
	if (!lane.normalQueue.empty()) {
		request = std::move(lane.normalQueue.front());
		lane.normalQueue.pop_front();
		return true;
	}
	if (!lane.highQueue.empty()) {
		request = std::move(lane.highQueue.front());
		lane.highQueue.pop_front();
		return true;
	}
	if (!lane.lowQueue.empty()) {
		request = std::move(lane.lowQueue.front());
		lane.lowQueue.pop_front();
		return true;
	}
	return false;
}

std::size_t Coprocessor::laneWorkerCount(Lane lane) const noexcept {
	const unsigned int hardwareThreads = std::max(1u, std::thread::hardware_concurrency());

	if (lane != Lane::Compute) return 1;
	if (hardwareThreads >= 8) return 3;
	if (hardwareThreads >= 4) return 2;
	return 1;
}

Coprocessor::LaneState &Coprocessor::laneState(Lane lane) noexcept {
	switch (lane) {
		case Lane::Io:
			return ioLane;
		case Lane::MiniMap:
			return miniMapLane;
		case Lane::Macro:
			return macroLane;
		case Lane::Extern:
		case Lane::Compute:
		default:
			return computeLane;
	}
}

Coprocessor &globalCoprocessor() {
	static Coprocessor instance;
	return instance;
}

} // namespace coprocessor
} // namespace mr
