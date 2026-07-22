#include "MRCoprocessor.hpp"

#include <chrono>
#include <exception>
#include <utility>
#include <vector>

namespace mr {
namespace coprocessor {
namespace {

std::uint64_t workerNowMicros() noexcept {
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

std::uint64_t Coprocessor::registerWorker(Lane lane, ExecutionOwnerKind ownerKind, std::size_t ownerLocalId) {
	std::lock_guard<std::mutex> lock(workerMutex);
	if (shuttingDown.load(std::memory_order_acquire)) return kInvalidWorkerOrdinal;

	std::unique_ptr<LaneState> worker = std::make_unique<LaneState>(lane, false, ownerKind, ownerLocalId);
	startLane(*worker);
	const std::uint64_t workerOrdinal = worker->workerOrdinal;
	workers.push_back(std::move(worker));
	return workerOrdinal;
}

void Coprocessor::unregisterWorker(std::uint64_t workerOrdinal) {
	if (workerOrdinal == kInvalidWorkerOrdinal) return;

	std::lock_guard<std::mutex> lock(workerMutex);
	for (std::vector<std::unique_ptr<LaneState>>::iterator it = workers.begin(); it != workers.end(); ++it) {
		if (*it == nullptr || (*it)->workerOrdinal != workerOrdinal) continue;
		requestLaneStop(**it);
		retiringWorkers.push_back(std::move(*it));
		workers.erase(it);
		break;
	}
}

void Coprocessor::requestLaneStop(LaneState &lane) {
	lane.stopRequested.store(true, std::memory_order_release);
	{
		std::lock_guard<std::mutex> lock(lane.mutex);
		if (lane.currentTask.cancelFlag != nullptr) lane.currentTask.cancelFlag->store(true, std::memory_order_release);
		for (Request &request : lane.queue)
			if (request.task.cancelFlag != nullptr) request.task.cancelFlag->store(true, std::memory_order_release);
	}
	markLaneStopping(lane);
	lane.cv.notify_all();
}

void Coprocessor::cancelPending() {
	std::vector<LaneState *> laneStates;

	{
		std::lock_guard<std::mutex> lock(taskCancelMutex);
		for (std::unordered_map<std::uint64_t, std::shared_ptr<std::atomic_bool>>::value_type &cancelEntry : taskCancelFlags)
			if (cancelEntry.second != nullptr) cancelEntry.second->store(true, std::memory_order_release);
	}
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		for (const std::unique_ptr<LaneState> &worker : workers)
			if (worker != nullptr) laneStates.push_back(worker.get());
		for (const std::unique_ptr<LaneState> &worker : retiringWorkers)
			if (worker != nullptr) laneStates.push_back(worker.get());
	}
	{
		std::lock_guard<std::mutex> lock(externalMutex);
		for (ExternalSourceState &source : externalSources)
			if (source.lane != nullptr) laneStates.push_back(source.lane.get());
	}
	for (LaneState *lane : laneStates) {
		std::vector<TaskInfo> clearedTasks;
		requestLaneStop(*lane);
		{
			std::lock_guard<std::mutex> lock(lane->mutex);
			for (const Request &request : lane->queue)
				if (request.task.id != 0) clearedTasks.push_back(request.task);
			lane->queue.clear();
			lane->activeTasks.clear();
		}
		for (const TaskInfo &task : clearedTasks) {
			recordTaskLifecycle(WorkerLifecycleState::Discarded, task, TaskStatus::Cancelled, nullptr, LifecycleReason::TaskCancelled);
			forgetTask(task.id);
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

	std::vector<std::unique_ptr<LaneState>> laneStates;
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		laneStates.reserve(workers.size() + retiringWorkers.size());
		for (std::unique_ptr<LaneState> &worker : workers)
			if (worker != nullptr) laneStates.push_back(std::move(worker));
		workers.clear();
		for (std::unique_ptr<LaneState> &worker : retiringWorkers)
			if (worker != nullptr) laneStates.push_back(std::move(worker));
		retiringWorkers.clear();
	}
	{
		std::lock_guard<std::mutex> lock(externalMutex);
		for (ExternalSourceState &source : externalSources)
			if (source.lane != nullptr) laneStates.push_back(std::move(source.lane));
		externalSources.clear();
	}
	for (std::unique_ptr<LaneState> &lane : laneStates)
		if (lane != nullptr && lane->worker.joinable()) lane->worker.join();

	if (drainResults)
		while (pump(64) != 0)
			;

	{
		std::lock_guard<std::mutex> lock(resultMutex);
		for (const Result &result : results)
			noteResultAdoption(result, false);
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
	lane.stopRequested.store(false, std::memory_order_release);
	lane.workerOrdinal = nextWorkerOrdinal.fetch_add(1, std::memory_order_relaxed);
	lane.assignedCore = allowedCoreIds.empty() ? -1 : allowedCoreIds[static_cast<std::size_t>(lane.workerOrdinal % allowedCoreIds.size())];
	lane.affinityResult = -1;
	lane.lifecycleState = WorkerLifecycleState::Created;
	recordWorkerLifecycle(WorkerLifecycleState::Created, lane, nullptr, TaskStatus::Completed);
	const std::uint64_t workerOrdinal = lane.workerOrdinal;
	lane.worker = std::thread([this, &lane, workerOrdinal]() { workerLoop(lane, workerOrdinal); });
}

void Coprocessor::workerLoop(LaneState &lane, std::uint64_t workerOrdinal) {
	int assignedCore = -1;
	std::uint64_t osThreadId = 0;
	const int affinityResult = assignCurrentWorkerCore(workerOrdinal, assignedCore, osThreadId);
	{
		std::lock_guard<std::mutex> lock(lane.mutex);
		lane.osThreadId = osThreadId;
		lane.assignedCore = assignedCore;
		lane.affinityResult = affinityResult;
		lane.lifecycleState = WorkerLifecycleState::Assigned;
	}
	recordWorkerLifecycle(WorkerLifecycleState::Assigned, lane, nullptr, TaskStatus::Completed);
	{
		bool becameIdle = false;
		{
			std::lock_guard<std::mutex> lock(lane.mutex);
			if (lane.lifecycleState == WorkerLifecycleState::Assigned && lane.queue.empty()) {
				lane.lifecycleState = WorkerLifecycleState::Idle;
				becameIdle = true;
			}
		}
		if (becameIdle) recordWorkerLifecycle(WorkerLifecycleState::Idle, lane, nullptr, TaskStatus::Completed);
	}
	for (;;) {
		Request request;
		Result result;
		std::uint64_t startedMicros = 0;
		std::uint64_t finishedMicros = 0;

		{
			std::unique_lock<std::mutex> lock(lane.mutex);
			lane.cv.wait(lock, [this, &lane]() { return lane.stopRequested.load(std::memory_order_acquire) || laneHasQueuedWorkLocked(lane); });
			if (lane.stopRequested.load(std::memory_order_acquire) && !laneHasQueuedWorkLocked(lane)) break;
			if (!popNextRequestLocked(lane, request)) continue;
			startedMicros = workerNowMicros();
			request.task.workerOrdinal = lane.workerOrdinal;
			request.task.osThreadId = lane.osThreadId;
			request.task.assignedCore = lane.assignedCore;
			request.task.affinityResult = lane.affinityResult;
			ActiveTaskState activeTask;

			activeTask.workerSlot = 0;
			activeTask.task = request.task;
			activeTask.submittedMicros = request.submittedMicros;
			activeTask.startedMicros = startedMicros;
			lane.activeTasks.push_back(std::move(activeTask));
			lane.lifecycleState = WorkerLifecycleState::Running;
			lane.currentTask = request.task;
			lane.submittedMicros = request.submittedMicros;
			lane.startedMicros = startedMicros;
			lane.finishedMicros = 0;
		}
		recordWorkerLifecycle(WorkerLifecycleState::Running, lane, &request.task, TaskStatus::Completed);
		result.task = request.task;
		try {
			if (lane.stopRequested.load(std::memory_order_acquire) || request.task.cancelRequested()) {
				result.status = TaskStatus::Cancelled;
			} else if (request.fn) {
				result = request.fn(request.task);
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
		finishedMicros = workerNowMicros();
		if (startedMicros >= request.submittedMicros) result.timing.queueMicros = startedMicros - request.submittedMicros;
		if (finishedMicros >= startedMicros) result.timing.runMicros = finishedMicros - startedMicros;
		if (finishedMicros >= request.submittedMicros) result.timing.totalMicros = finishedMicros - request.submittedMicros;

		{
			std::lock_guard<std::mutex> lock(lane.mutex);
			for (std::vector<ActiveTaskState>::iterator it = lane.activeTasks.begin(); it != lane.activeTasks.end(); ++it) {
				if (it->task.id != request.task.id) continue;
				lane.activeTasks.erase(it);
				break;
			}
			lane.lifecycleState = WorkerLifecycleState::ResultReady;
			lane.currentTask = result.task;
			lane.finishedMicros = finishedMicros;
		}
		const TaskStatus completedTaskStatus = result.status;
		enqueueResult(std::move(result));
		forgetTask(request.task.id);
		if (lane.retireAfterTask) break;
		{
			TaskInfo idleTask;
			bool becameIdle = false;
			{
				std::lock_guard<std::mutex> lock(lane.mutex);
				if (lane.lifecycleState == WorkerLifecycleState::ResultReady && lane.queue.empty()) {
					lane.lifecycleState = WorkerLifecycleState::Idle;
					idleTask = lane.currentTask;
					becameIdle = true;
				}
			}
			if (becameIdle) recordWorkerLifecycle(WorkerLifecycleState::Idle, lane, &idleTask, completedTaskStatus);
		}
	}
	TaskInfo finishedTask;
	{
		std::lock_guard<std::mutex> lock(lane.mutex);
		lane.retired = true;
		lane.lifecycleState = WorkerLifecycleState::Finished;
		finishedTask = lane.currentTask;
	}
	if (lane.retireAfterTask && finishedTask.id != 0) recordOneShotWorkerFinished(finishedTask.kind, finishedTask.executionOwnerKind);
	recordWorkerLifecycle(WorkerLifecycleState::Finished, lane, &finishedTask, TaskStatus::Completed, nullptr,
	                      lane.stopRequested.load(std::memory_order_acquire) ? LifecycleReason::StopRequested : LifecycleReason::WorkerFinished);
}

void Coprocessor::reapRetiredWorkers() {
	std::vector<std::unique_ptr<LaneState>> retiredLanes;
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		for (std::vector<std::unique_ptr<LaneState>>::iterator it = workers.begin(); it != workers.end();) {
			bool retired = false;
			if (*it != nullptr) {
				std::lock_guard<std::mutex> laneLock((*it)->mutex);
				retired = (*it)->retired;
			}
			if (!retired) {
				++it;
				continue;
			}
			retiredLanes.push_back(std::move(*it));
			it = workers.erase(it);
		}
		for (std::vector<std::unique_ptr<LaneState>>::iterator it = retiringWorkers.begin(); it != retiringWorkers.end();) {
			bool retired = false;
			if (*it != nullptr) {
				std::lock_guard<std::mutex> laneLock((*it)->mutex);
				retired = (*it)->retired;
			}
			if (!retired) {
				++it;
				continue;
			}
			retiredLanes.push_back(std::move(*it));
			it = retiringWorkers.erase(it);
		}
	}
	for (std::unique_ptr<LaneState> &lane : retiredLanes)
		if (lane != nullptr && lane->worker.joinable()) lane->worker.join();
}

} // namespace coprocessor
} // namespace mr
