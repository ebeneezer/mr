#include "MRCoprocessor.hpp"

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
	return static_cast<std::uint64_t>(
	    std::chrono::duration_cast<std::chrono::microseconds>(
	        std::chrono::steady_clock::now().time_since_epoch())
	        .count());
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

void bindCurrentThreadToLaneCore(Lane lane) noexcept {
	std::vector<int> availableCores;
	cpu_set_t availableSet;
	CPU_ZERO(&availableSet);
	if (sched_getaffinity(0, sizeof(availableSet), &availableSet) == 0) {
		for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
			if (CPU_ISSET(cpu, &availableSet))
				availableCores.push_back(cpu);
	}

	if (availableCores.empty()) {
		const long onlineCoreCount = sysconf(_SC_NPROCESSORS_ONLN);
		if (onlineCoreCount <= 0)
			return;
		availableCores.reserve(static_cast<std::size_t>(onlineCoreCount));
		for (int cpu = 0; cpu < onlineCoreCount; ++cpu)
			availableCores.push_back(cpu);
	}

	const int targetCore = availableCores[laneAffinitySlot(lane) % availableCores.size()];
	cpu_set_t targetSet;
	CPU_ZERO(&targetSet);
	CPU_SET(targetCore, &targetSet);
	(void)pthread_setaffinity_np(pthread_self(), sizeof(targetSet), &targetSet);
}

} // namespace

Coprocessor::Coprocessor()
    :  nextTaskId(1),  shuttingDown(false), ioLane(Lane::Io),
      computeLane(Lane::Compute), miniMapLane(Lane::MiniMap), macroLane(Lane::Macro) {
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

std::uint64_t Coprocessor::submit(Lane lane, TaskKind kind, std::size_t documentId,
                                  std::size_t baseVersion, std::string_view label, TaskFn fn) {
	if (shuttingDown.load(std::memory_order_acquire))
		return 0;

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

	{
		std::lock_guard<std::mutex> lock(taskCancelMutex);
		taskCancelFlags[taskId] = request.task.cancelFlag;
	}

	{
		std::lock_guard<std::mutex> lock(targetLaneState.mutex);
		targetLaneState.queue.push_back(std::move(request));
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
			if (results.empty())
				break;
			result = std::move(results.front());
			results.pop_front();
		}

		{
			std::lock_guard<std::mutex> lock(handlerMutex);
			handler = resultHandler;
		}

		if (handler)
			handler(result);
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
		std::unordered_map<std::uint64_t, std::shared_ptr<std::atomic_bool>>::iterator cancelFlagIt =
		    taskCancelFlags.find(taskId);
		if (cancelFlagIt == taskCancelFlags.end())
			return false;
		cancelFlag = cancelFlagIt->second;
	}
	if (cancelFlag != nullptr)
		cancelFlag->store(true, std::memory_order_release);
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
		for (auto & cancelEntry : taskCancelFlags)
			if (cancelEntry.second != nullptr)
				cancelEntry.second->store(true, std::memory_order_release);
	}

	for (LaneState *lane : laneStates) {
		if (lane->worker.joinable())
			lane->worker.request_stop();
		{
			std::lock_guard<std::mutex> lock(lane->mutex);
			lane->queue.clear();
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

	auto joinLaneWorker = [](LaneState &lane) {
		if (!lane.worker.joinable())
			return;
		std::jthread joinedWorker = std::move(lane.worker);
	};

	joinLaneWorker(ioLane);
	joinLaneWorker(computeLane);
	joinLaneWorker(miniMapLane);
	joinLaneWorker(macroLane);

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
	lane.worker = std::jthread([this, &lane](std::stop_token stopToken) { workerLoop(lane, stopToken); });
}

void Coprocessor::workerLoop(LaneState &lane, std::stop_token stopToken) {
	bindCurrentThreadToLaneCore(lane.lane);

	for (;;) {
		Request request;
		Result result;
		std::uint64_t startedMicros = 0;
		std::uint64_t finishedMicros = 0;

		{
			std::unique_lock<std::mutex> lock(lane.mutex);
			lane.cv.wait(lock, stopToken, [&lane]() { return !lane.queue.empty(); });
			if (stopToken.stop_requested() && lane.queue.empty())
				break;
			if (lane.queue.empty())
				continue;
			request = std::move(lane.queue.front());
			lane.queue.pop_front();
		}

		startedMicros = nowMicros();
		result.task = request.task;
		try {
			if (stopToken.stop_requested() || request.task.cancelRequested()) {
				result.status = TaskStatus::Cancelled;
			} else if (request.fn) {
				result = request.fn(request.task, stopToken);
				if (result.task.id == 0)
					result.task = request.task;
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
		if (startedMicros >= request.submittedMicros)
			result.timing.queueMicros = startedMicros - request.submittedMicros;
		if (finishedMicros >= startedMicros)
			result.timing.runMicros = finishedMicros - startedMicros;
		if (finishedMicros >= request.submittedMicros)
			result.timing.totalMicros = finishedMicros - request.submittedMicros;

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
