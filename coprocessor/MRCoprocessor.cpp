#include "MRCoprocessor.hpp"

#include <chrono>
#include <exception>
#include <utility>

namespace mr {
namespace coprocessor {
namespace {

std::uint64_t nowMicros() noexcept {
	return static_cast<std::uint64_t>(
	    std::chrono::duration_cast<std::chrono::microseconds>(
	        std::chrono::steady_clock::now().time_since_epoch())
	        .count());
}

} // namespace

Coprocessor::Coprocessor()
    :  nextTaskId_(1),  shuttingDown_(false), ioLane_(Lane::Io),
      computeLane_(Lane::Compute), macroLane_(Lane::Macro) {
	startLane(ioLane_);
	startLane(computeLane_);
	startLane(macroLane_);
}

Coprocessor::~Coprocessor() {
	shutdown();
}

void Coprocessor::setResultHandler(ResultHandler handler) {
	std::lock_guard<std::mutex> lock(handlerMutex_);
	resultHandler_ = std::move(handler);
}

std::uint64_t Coprocessor::submit(Lane lane, TaskKind kind, std::size_t documentId,
                                  std::size_t baseVersion, const std::string &label, TaskFn fn) {
	if (shuttingDown_.load(std::memory_order_acquire))
		return 0;

	Request request;
	LaneState &target = laneState(lane);
	std::uint64_t assignedId = 0;

	{
		std::lock_guard<std::mutex> idLock(nextTaskMutex_);
		assignedId = nextTaskId_++;
		request.task.id = assignedId;
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
		std::lock_guard<std::mutex> lock(taskCancelMutex_);
		taskCancelFlags_[assignedId] = request.task.cancelFlag;
	}

	{
		std::lock_guard<std::mutex> lock(target.mutex);
		target.queue.push_back(std::move(request));
	}
	target.cv.notify_one();
	return assignedId;
}

std::size_t Coprocessor::pump(std::size_t maxResults) {
	std::size_t drained = 0;

	while (drained < maxResults) {
		Result result;
		ResultHandler handler;

		{
			std::lock_guard<std::mutex> lock(resultMutex_);
			if (results_.empty())
				break;
			result = std::move(results_.front());
			results_.pop_front();
		}

		{
			std::lock_guard<std::mutex> lock(handlerMutex_);
			handler = resultHandler_;
		}

		if (handler)
			handler(result);
		++drained;
	}

	return drained;
}

std::size_t Coprocessor::pendingResults() const noexcept {
	std::lock_guard<std::mutex> lock(resultMutex_);
	return results_.size();
}

void Coprocessor::post(Result result) {
	enqueueResult(std::move(result));
}

bool Coprocessor::cancelTask(std::uint64_t taskId) {
	std::shared_ptr<std::atomic_bool> cancelFlag;

	{
		std::lock_guard<std::mutex> lock(taskCancelMutex_);
		std::unordered_map<std::uint64_t, std::shared_ptr<std::atomic_bool>>::iterator found =
		    taskCancelFlags_.find(taskId);
		if (found == taskCancelFlags_.end())
			return false;
		cancelFlag = found->second;
	}
	if (cancelFlag != nullptr)
		cancelFlag->store(true, std::memory_order_release);
	ioLane_.cv.notify_all();
	computeLane_.cv.notify_all();
	macroLane_.cv.notify_all();
	return true;
}

void Coprocessor::cancelPending() {
	LaneState *lanes[] = {&ioLane_, &computeLane_, &macroLane_};

	{
		std::lock_guard<std::mutex> lock(taskCancelMutex_);
		for (auto & taskCancelFlag : taskCancelFlags_)
			if (taskCancelFlag.second != nullptr)
				taskCancelFlag.second->store(true, std::memory_order_release);
	}

	for (LaneState *lane : lanes) {
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
	if (shuttingDown_.exchange(true, std::memory_order_acq_rel)) {
		if (drainResults)
			while (pump(64) != 0)
				;
		return;
	}

	cancelPending();

	auto stopLane = [](LaneState &lane) {
		if (!lane.worker.joinable())
			return;
		std::jthread worker = std::move(lane.worker);
	};

	stopLane(ioLane_);
	stopLane(computeLane_);
	stopLane(macroLane_);

	if (drainResults)
		while (pump(64) != 0)
			;

	{
		std::lock_guard<std::mutex> lock(resultMutex_);
		results_.clear();
	}
	{
		std::lock_guard<std::mutex> lock(handlerMutex_);
		resultHandler_ = ResultHandler();
	}
	{
		std::lock_guard<std::mutex> lock(taskCancelMutex_);
		taskCancelFlags_.clear();
	}
}

void Coprocessor::startLane(LaneState &lane) {
	lane.worker = std::jthread([this, &lane](std::stop_token stopToken) { workerLoop(lane, stopToken); });
}

void Coprocessor::workerLoop(LaneState &lane, std::stop_token stopToken) {
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
	std::lock_guard<std::mutex> lock(resultMutex_);
	results_.push_back(std::move(result));
}

void Coprocessor::forgetTask(std::uint64_t taskId) {
	std::lock_guard<std::mutex> lock(taskCancelMutex_);
	taskCancelFlags_.erase(taskId);
}

Coprocessor::LaneState &Coprocessor::laneState(Lane lane) noexcept {
	switch (lane) {
		case Lane::Io:
			return ioLane_;
		case Lane::Macro:
			return macroLane_;
		case Lane::Compute:
		default:
			return computeLane_;
	}
}

Coprocessor &globalCoprocessor() {
	static Coprocessor instance;
	return instance;
}

} // namespace coprocessor
} // namespace mr
