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
	}
	return 0;
}

void bindCurrentThreadToLaneCore(Lane lane, std::size_t workerSlot) noexcept {
	std::vector<int> availableCores;
	cpu_set_t availableSet;
	CPU_ZERO(&availableSet);
	if (sched_getaffinity(0, sizeof(availableSet), &availableSet) == 0) {
		for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
			if (CPU_ISSET(cpu, &availableSet)) availableCores.push_back(cpu);
	}

	if (availableCores.empty()) {
		const long onlineCoreCount = sysconf(_SC_NPROCESSORS_ONLN);
		if (onlineCoreCount <= 0) return;
		availableCores.reserve(static_cast<std::size_t>(onlineCoreCount));
		for (int cpu = 0; cpu < onlineCoreCount; ++cpu)
			availableCores.push_back(cpu);
	}

	const unsigned int laneSlot = laneAffinitySlot(lane);
	const int targetCore = availableCores[(laneSlot + static_cast<unsigned int>(workerSlot)) % availableCores.size()];
	cpu_set_t targetSet;
	CPU_ZERO(&targetSet);
	CPU_SET(targetCore, &targetSet);
	(void)pthread_setaffinity_np(pthread_self(), sizeof(targetSet), &targetSet);
}

} // namespace

Coprocessor::Coprocessor() : nextTaskId(1), shuttingDown(false), ioLane(Lane::Io), computeLane(Lane::Compute), miniMapLane(Lane::MiniMap), macroLane(Lane::Macro) {
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
	if (shuttingDown.load(std::memory_order_acquire)) return 0;

	Request request;
	LaneState &targetLaneState = laneState(lane);
	std::uint64_t taskId = 0;

	{
		std::lock_guard<std::mutex> idLock(nextTaskMutex);
		taskId = nextTaskId++;
		request.task.id = taskId;
	}
	request.task.cancelFlag = std::make_shared<std::atomic_bool>(false);
	request.task.lane = lane;
	request.task.kind = kind;
	request.task.documentId = documentId;
	request.task.baseVersion = baseVersion;
	request.task.label = label;
	request.fn = std::move(fn);
	request.submittedMicros = nowMicros();
	request.computePriority = computePriorityForTask(kind);

	{
		std::lock_guard<std::mutex> lock(taskCancelMutex);
		taskCancelFlags[taskId] = request.task.cancelFlag;
	}

	{
		std::lock_guard<std::mutex> lock(targetLaneState.mutex);
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
	targetLaneState.cv.notify_one();
	return taskId;
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
	return true;
}

void Coprocessor::cancelPending() {
	LaneState *laneStates[] = {&ioLane, &computeLane, &miniMapLane, &macroLane};

	{
		std::lock_guard<std::mutex> lock(taskCancelMutex);
		for (auto &cancelEntry : taskCancelFlags)
			if (cancelEntry.second != nullptr) cancelEntry.second->store(true, std::memory_order_release);
	}

	for (LaneState *lane : laneStates) {
		for (std::size_t workerIndex = 0; workerIndex < lane->workers.size(); ++workerIndex)
			if (lane->workers[workerIndex].joinable()) lane->workers[workerIndex].request_stop();
		{
			std::lock_guard<std::mutex> lock(lane->mutex);
			lane->queue.clear();
			lane->highQueue.clear();
			lane->normalQueue.clear();
			lane->lowQueue.clear();
			lane->activeTasks.clear();
		}
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
	std::lock_guard<std::mutex> lock(resultMutex);
	results.push_back(std::move(result));
}

void Coprocessor::forgetTask(std::uint64_t taskId) {
	std::lock_guard<std::mutex> lock(taskCancelMutex);
	taskCancelFlags.erase(taskId);
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
