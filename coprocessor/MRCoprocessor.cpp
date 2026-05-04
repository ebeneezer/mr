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
	}
	return 0;
}

std::size_t laneWorkerCount(Lane lane) noexcept {
	if (lane != Lane::Macro) return 1;
	const std::size_t coreCount = availableAffinityCores().size();
	if (coreCount < 4) return 1;
	return 2;
}

void bindCurrentThreadToLaneCore(Lane lane, std::size_t workerIndex) noexcept {
	const std::vector<int> availableCores = availableAffinityCores();
	if (availableCores.empty()) return;

	unsigned int slot = laneAffinitySlot(lane);
	if (lane == Lane::Macro) slot += static_cast<unsigned int>(workerIndex);

	const int targetCore = availableCores[slot % availableCores.size()];
	cpu_set_t targetSet;
	CPU_ZERO(&targetSet);
	CPU_SET(targetCore, &targetSet);
	(void)pthread_setaffinity_np(pthread_self(), sizeof(targetSet), &targetSet);
}

struct ActiveWorkerGuard {
	std::atomic<unsigned int> &counter;

	explicit ActiveWorkerGuard(std::atomic<unsigned int> &aCounter) noexcept : counter(aCounter) {
		counter.fetch_add(1, std::memory_order_relaxed);
	}

	~ActiveWorkerGuard() {
		counter.fetch_sub(1, std::memory_order_relaxed);
	}

	ActiveWorkerGuard(const ActiveWorkerGuard &) = delete;
	ActiveWorkerGuard &operator=(const ActiveWorkerGuard &) = delete;
};

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
	return submitCoalesced(lane, kind, documentId, baseVersion, std::string_view(), label, std::move(fn));
}

std::uint64_t Coprocessor::submitCoalesced(Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, std::string_view coalescingKey, std::string_view label, TaskFn fn) {
	if (shuttingDown.load(std::memory_order_acquire)) return 0;

	Request request;
	LaneState &targetLaneState = laneState(lane);
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

	{
		std::lock_guard<std::mutex> lock(taskCancelMutex);
		taskCancelFlags[taskId] = request.task.cancelFlag;
	}

	{
		std::lock_guard<std::mutex> lock(targetLaneState.mutex);
		if (allowCoalescing) {
			for (std::deque<Request>::iterator it = targetLaneState.queue.begin(); it != targetLaneState.queue.end();) {
				if (it->task.kind == kind && it->coalescingKey == request.coalescingKey) {
					if (it->task.id != 0) removedTaskIds.push_back(it->task.id);
					it = targetLaneState.queue.erase(it);
					continue;
				}
				++it;
			}
		}
		if (lane == Lane::Macro) request.laneSequence = targetLaneState.nextSubmitSequence++;
		targetLaneState.queue.push_back(std::move(request));
	}
	for (std::uint64_t removedTaskId : removedTaskIds)
		forgetTask(removedTaskId);
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

CoprocessorSnapshot Coprocessor::snapshot() const noexcept {
	CoprocessorSnapshot out;

	{
		std::lock_guard<std::mutex> lock(ioLane.mutex);
		out.io.queueDepth = ioLane.queue.size();
	}
	out.io.activeWorkers = ioLane.activeWorkers.load(std::memory_order_relaxed);

	{
		std::lock_guard<std::mutex> lock(computeLane.mutex);
		out.compute.queueDepth = computeLane.queue.size();
	}
	out.compute.activeWorkers = computeLane.activeWorkers.load(std::memory_order_relaxed);

	{
		std::lock_guard<std::mutex> lock(miniMapLane.mutex);
		out.miniMap.queueDepth = miniMapLane.queue.size();
	}
	out.miniMap.activeWorkers = miniMapLane.activeWorkers.load(std::memory_order_relaxed);

	{
		std::lock_guard<std::mutex> lock(macroLane.mutex);
		out.macro.queueDepth = macroLane.queue.size();
	}
	out.macro.activeWorkers = macroLane.activeWorkers.load(std::memory_order_relaxed);

	return out;
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

	for (LaneState *lane : laneStates) {
		std::vector<std::uint64_t> clearedTaskIds;
		for (std::jthread &worker : lane->workers)
			if (worker.joinable()) worker.request_stop();
		{
			std::lock_guard<std::mutex> lock(lane->mutex);
			for (const Request &request : lane->queue) {
				if (request.task.id != 0) clearedTaskIds.push_back(request.task.id);
				if (lane->lane == Lane::Macro && request.laneSequence != 0) lane->skippedSequences.push_back(request.laneSequence);
			}
			lane->queue.clear();
			while (!lane->skippedSequences.empty() && lane->skippedSequences.front() == lane->nextPublishSequence) {
				lane->skippedSequences.pop_front();
				++lane->nextPublishSequence;
			}
		}
		for (std::uint64_t taskId : clearedTaskIds) forgetTask(taskId);
		lane->cv.notify_all();
	}

	{
		std::lock_guard<std::mutex> lock(taskCancelMutex);
		for (auto &cancelEntry : taskCancelFlags)
			if (cancelEntry.second != nullptr) cancelEntry.second->store(true, std::memory_order_release);
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
		std::vector<std::jthread> joinedWorkers = std::move(lane.workers);
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
	for (std::size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
		lane.workers.emplace_back([this, &lane, workerIndex](std::stop_token stopToken) { workerLoop(lane, workerIndex, stopToken); });
}

void Coprocessor::workerLoop(LaneState &lane, std::size_t workerIndex, std::stop_token stopToken) {
	bindCurrentThreadToLaneCore(lane.lane, workerIndex);

	for (;;) {
		Request request;
		Result result;
		std::uint64_t startedMicros = 0;
		std::uint64_t finishedMicros = 0;
		std::vector<Result> publishResults;

		{
			std::unique_lock<std::mutex> lock(lane.mutex);
			lane.cv.wait(lock, stopToken, [&lane]() { return !lane.queue.empty(); });
			if (stopToken.stop_requested() && lane.queue.empty()) break;
			if (lane.queue.empty()) continue;
			request = std::move(lane.queue.front());
			lane.queue.pop_front();
		}
		ActiveWorkerGuard activeWorkerGuard(lane.activeWorkers);

		startedMicros = nowMicros();
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

		forgetTask(request.task.id);
		{
			std::lock_guard<std::mutex> lock(lane.mutex);
			if (lane.lane != Lane::Macro || request.laneSequence == 0) {
				publishResults.push_back(std::move(result));
			} else {
				lane.finishedResults.emplace(request.laneSequence, std::move(result));
				for (;;) {
					while (!lane.skippedSequences.empty() && lane.skippedSequences.front() == lane.nextPublishSequence) {
						lane.skippedSequences.pop_front();
						++lane.nextPublishSequence;
					}

					std::map<std::uint64_t, Result>::iterator publishIt = lane.finishedResults.find(lane.nextPublishSequence);
					if (publishIt == lane.finishedResults.end()) break;
					publishResults.push_back(std::move(publishIt->second));
					lane.finishedResults.erase(publishIt);
					++lane.nextPublishSequence;
				}
			}
		}
		for (Result &publishResult : publishResults)
			enqueueResult(std::move(publishResult));
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
