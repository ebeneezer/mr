#include "MRCoprocessor.hpp"

#include <exception>
#include <utility>

namespace mr {
namespace coprocessor {

Coprocessor::Coprocessor()
    : resultMutex_(), results_(), handlerMutex_(), resultHandler_(), nextTaskId_(1), nextTaskMutex_(),
      shuttingDown_(false), ioLane_(Lane::Io), computeLane_(Lane::Compute), macroLane_(Lane::Macro) {
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
	request.task.lane = lane;
	request.task.kind = kind;
	request.task.documentId = documentId;
	request.task.baseVersion = baseVersion;
	request.task.label = label;
	request.fn = std::move(fn);

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

void Coprocessor::cancelPending() {
	LaneState *lanes[] = {&ioLane_, &computeLane_, &macroLane_};

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

void Coprocessor::shutdown() {
	if (shuttingDown_.exchange(true, std::memory_order_acq_rel))
		return;

	cancelPending();

	auto stopLane = [](LaneState &lane) {
		if (!lane.worker.joinable())
			return;
		std::jthread worker = std::move(lane.worker);
	};

	stopLane(ioLane_);
	stopLane(computeLane_);
	stopLane(macroLane_);

	{
		std::lock_guard<std::mutex> lock(resultMutex_);
		results_.clear();
	}
	{
		std::lock_guard<std::mutex> lock(handlerMutex_);
		resultHandler_ = ResultHandler();
	}
}

void Coprocessor::startLane(LaneState &lane) {
	lane.worker = std::jthread([this, &lane](std::stop_token stopToken) { workerLoop(lane, stopToken); });
}

void Coprocessor::workerLoop(LaneState &lane, std::stop_token stopToken) {
	for (;;) {
		Request request;
		Result result;

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

		result.task = request.task;
		try {
			if (stopToken.stop_requested()) {
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

		enqueueResult(std::move(result));
	}
}

void Coprocessor::enqueueResult(Result result) {
	std::lock_guard<std::mutex> lock(resultMutex_);
	results_.push_back(std::move(result));
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
