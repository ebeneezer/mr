#ifndef MRCOPROCESSOR_HPP
#define MRCOPROCESSOR_HPP

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <atomic>

#include "MRTextDocument.hpp"

namespace mr {
namespace coprocessor {

enum class Lane : unsigned char {
	Io,
	Compute,
	Macro
};

enum class TaskKind : unsigned char {
	Custom,
	LineIndexWarmup,
	SyntaxWarmup,
	IndicatorBlink,
	ExternalIo,
	MacroJob
};

enum class TaskStatus : unsigned char {
	Completed,
	Cancelled,
	Failed
};

struct TaskInfo {
	std::uint64_t id;
	Lane lane;
	TaskKind kind;
	std::size_t documentId;
	std::size_t baseVersion;
	std::string label;

	TaskInfo() noexcept
	    : id(0), lane(Lane::Compute), kind(TaskKind::Custom), documentId(0), baseVersion(0), label() {
	}
};

class Payload {
  public:
	virtual ~Payload() = default;
};

struct IndicatorBlinkPayload final : Payload {
	std::size_t indicatorId;
	std::size_t generation;
	bool visible;

	IndicatorBlinkPayload() noexcept : indicatorId(0), generation(0), visible(true) {
	}

	IndicatorBlinkPayload(std::size_t aIndicatorId, std::size_t aGeneration, bool aVisible) noexcept
	    : indicatorId(aIndicatorId), generation(aGeneration), visible(aVisible) {
	}
};

struct LineIndexWarmupPayload final : Payload {
	mr::editor::LineIndexWarmupData warmup;

	LineIndexWarmupPayload() noexcept : warmup() {
	}

	explicit LineIndexWarmupPayload(const mr::editor::LineIndexWarmupData &aWarmup) : warmup(aWarmup) {
	}
};

struct Result {
	TaskInfo task;
	TaskStatus status;
	std::string error;
	std::shared_ptr<const Payload> payload;

	Result() noexcept : task(), status(TaskStatus::Completed), error(), payload() {
	}

	bool completed() const noexcept {
		return status == TaskStatus::Completed;
	}

	bool cancelled() const noexcept {
		return status == TaskStatus::Cancelled;
	}

	bool failed() const noexcept {
		return status == TaskStatus::Failed;
	}
};

using TaskFn = std::function<Result(const TaskInfo &, std::stop_token)>;
using ResultHandler = std::function<void(const Result &)>;

class Coprocessor {
  public:
	Coprocessor();
	~Coprocessor();

	Coprocessor(const Coprocessor &) = delete;
	Coprocessor &operator=(const Coprocessor &) = delete;

	void setResultHandler(ResultHandler handler);
	std::uint64_t submit(Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion,
	                     const std::string &label, TaskFn fn);
	std::size_t pump(std::size_t maxResults = 8);
	std::size_t pendingResults() const noexcept;
	void shutdown();
	void cancelPending();

  private:
	struct Request {
		TaskInfo task;
		TaskFn fn;
	};

	struct LaneState {
		Lane lane;
		std::mutex mutex;
		std::condition_variable_any cv;
		std::deque<Request> queue;
		std::jthread worker;

		explicit LaneState(Lane aLane) noexcept : lane(aLane), mutex(), cv(), queue(), worker() {
		}
	};

	void startLane(LaneState &lane);
	void workerLoop(LaneState &lane, std::stop_token stopToken);
	void enqueueResult(Result result);
	LaneState &laneState(Lane lane) noexcept;

	mutable std::mutex resultMutex_;
	std::deque<Result> results_;

	mutable std::mutex handlerMutex_;
	ResultHandler resultHandler_;

	std::uint64_t nextTaskId_;
	std::mutex nextTaskMutex_;
	std::atomic<bool> shuttingDown_;

	LaneState ioLane_;
	LaneState computeLane_;
	LaneState macroLane_;
};

Coprocessor &globalCoprocessor();

} // namespace coprocessor
} // namespace mr

#endif
