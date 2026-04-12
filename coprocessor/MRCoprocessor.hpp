#ifndef MRCOPROCESSOR_HPP
#define MRCOPROCESSOR_HPP

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <atomic>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "MRTextDocument.hpp"
#include "TMRSyntax.hpp"
#include "mrvm.hpp"

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
	MiniMapWarmup,
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
	std::shared_ptr<std::atomic_bool> cancelFlag;

	TaskInfo() noexcept
	    : id(0), lane(Lane::Compute), kind(TaskKind::Custom), documentId(0), baseVersion(0), label(),
	      cancelFlag() {
	}

	[[nodiscard]] bool cancelRequested() const noexcept {
		return cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire);
	}
};

class Payload {
  public:
	virtual ~Payload() = default;
};

enum class IndicatorBlinkChannel : unsigned char {
	ReadOnly,
	TaskMarker,
	StatusNotice
};

struct IndicatorBlinkPayload final : Payload {
	std::size_t indicatorId;
	std::size_t generation;
	bool visible;
	IndicatorBlinkChannel channel;

	IndicatorBlinkPayload() noexcept
	    : indicatorId(0), generation(0), visible(true), channel(IndicatorBlinkChannel::ReadOnly) {
	}

	IndicatorBlinkPayload(std::size_t aIndicatorId, std::size_t aGeneration, bool aVisible,
	                      IndicatorBlinkChannel aChannel = IndicatorBlinkChannel::ReadOnly) noexcept
	    : indicatorId(aIndicatorId), generation(aGeneration), visible(aVisible), channel(aChannel) {
	}
};

struct LineIndexWarmupPayload final : Payload {
	mr::editor::LineIndexWarmupData warmup;

	LineIndexWarmupPayload() noexcept : warmup() {
	}

	explicit LineIndexWarmupPayload(const mr::editor::LineIndexWarmupData &aWarmup) : warmup(aWarmup) {
	}
};

struct SyntaxWarmLine {
	std::size_t lineStart;
	TMRSyntaxTokenMap tokens;

	SyntaxWarmLine() noexcept : lineStart(0), tokens() {
	}

	SyntaxWarmLine(std::size_t aLineStart, TMRSyntaxTokenMap aTokens)
	    : lineStart(aLineStart), tokens(std::move(aTokens)) {
	}
};

struct SyntaxWarmupPayload final : Payload {
	TMRSyntaxLanguage language;
	std::vector<SyntaxWarmLine> lines;

	SyntaxWarmupPayload() noexcept : language(TMRSyntaxLanguage::PlainText), lines() {
	}

	SyntaxWarmupPayload(TMRSyntaxLanguage aLanguage, std::vector<SyntaxWarmLine> aLines)
	    : language(aLanguage), lines(std::move(aLines)) {
	}
};

struct MiniMapWarmupPayload final : Payload {
	bool braille;
	int rowCount;
	int bodyWidth;
	std::size_t totalLines;
	int viewportWidth;
	std::vector<unsigned char> rowPatterns;
	std::vector<std::size_t> rowLineStarts;
	std::vector<std::size_t> rowLineEnds;

	MiniMapWarmupPayload() noexcept
	    : braille(true), rowCount(0), bodyWidth(0), totalLines(1), viewportWidth(1), rowPatterns(),
	      rowLineStarts(), rowLineEnds() {
	}

	MiniMapWarmupPayload(bool aBraille, int aRowCount, int aBodyWidth, std::size_t aTotalLines,
	                     int aViewportWidth, std::vector<unsigned char> aRowPatterns,
	                     std::vector<std::size_t> aRowLineStarts, std::vector<std::size_t> aRowLineEnds)
	    : braille(aBraille), rowCount(aRowCount), bodyWidth(aBodyWidth), totalLines(aTotalLines),
	      viewportWidth(aViewportWidth), rowPatterns(std::move(aRowPatterns)),
	      rowLineStarts(std::move(aRowLineStarts)), rowLineEnds(std::move(aRowLineEnds)) {
	}
};

struct ExternalIoChunkPayload final : Payload {
	std::size_t channelId;
	std::string text;

	ExternalIoChunkPayload() noexcept : channelId(0), text() {
	}

	ExternalIoChunkPayload(std::size_t aChannelId, std::string aText)
	    : channelId(aChannelId), text(std::move(aText)) {
	}
};

struct ExternalIoFinishedPayload final : Payload {
	std::size_t channelId;
	int exitCode;
	bool signaled;
	int signalNumber;

	ExternalIoFinishedPayload() noexcept : channelId(0), exitCode(0), signaled(false), signalNumber(0) {
	}

	ExternalIoFinishedPayload(std::size_t aChannelId, int aExitCode, bool aSignaled,
	                          int aSignalNumber) noexcept
	    : channelId(aChannelId), exitCode(aExitCode), signaled(aSignaled), signalNumber(aSignalNumber) {
	}
};

struct MacroJobFinishedPayload final : Payload {
	std::string displayName;
	std::vector<std::string> logLines;
	bool hadError;

	MacroJobFinishedPayload() noexcept : displayName(), logLines(), hadError(false) {
	}

	MacroJobFinishedPayload(std::string aDisplayName, std::vector<std::string> aLogLines, bool aHadError)
	    : displayName(std::move(aDisplayName)), logLines(std::move(aLogLines)), hadError(aHadError) {
	}
};

struct MacroJobStagedPayload final : Payload {
	std::string displayName;
	std::vector<std::string> logLines;
	bool hadError;
	mr::editor::StagedEditTransaction transaction;
	std::size_t cursorOffset;
	std::size_t selectionStart;
	std::size_t selectionEnd;
	int blockMode;
	bool blockMarkingOn;
	std::size_t blockAnchor;
	std::size_t blockEnd;
	std::vector<std::string> globalOrder;
	std::map<std::string, int> globalInts;
	std::map<std::string, std::string> globalStrings;
	std::vector<MRMacroDeferredUiCommand> deferredUiCommands;
	bool lastSearchValid;
	std::size_t lastSearchStart;
	std::size_t lastSearchEnd;
	std::size_t lastSearchCursor;
	bool ignoreCase;
	bool tabExpand;
	std::vector<std::size_t> markStack;
	bool insertMode;
	int indentLevel;
	std::string fileName;
	bool fileChanged;

	MacroJobStagedPayload() noexcept
	    : displayName(), logLines(), hadError(false), transaction(), cursorOffset(0), selectionStart(0),
	      selectionEnd(0), blockMode(0), blockMarkingOn(false), blockAnchor(0), blockEnd(0),
	      globalOrder(), globalInts(), globalStrings(),
	      deferredUiCommands(),
	      lastSearchValid(false), lastSearchStart(0), lastSearchEnd(0), lastSearchCursor(0),
	      ignoreCase(false), tabExpand(true), markStack(), insertMode(true), indentLevel(1),
	      fileName(), fileChanged(false) {
	}

	MacroJobStagedPayload(std::string aDisplayName, std::vector<std::string> aLogLines, bool aHadError,
	                      mr::editor::StagedEditTransaction aTransaction, std::size_t aCursorOffset,
	                      std::size_t aSelectionStart, std::size_t aSelectionEnd, int aBlockMode,
	                      bool aBlockMarkingOn, std::size_t aBlockAnchor, std::size_t aBlockEnd,
	                      std::vector<std::string> aGlobalOrder, std::map<std::string, int> aGlobalInts,
	                      std::map<std::string, std::string> aGlobalStrings,
	                      std::vector<MRMacroDeferredUiCommand> aDeferredUiCommands,
	                      bool aLastSearchValid, std::size_t aLastSearchStart,
	                      std::size_t aLastSearchEnd, std::size_t aLastSearchCursor,
	                      bool anIgnoreCase, bool aTabExpand, std::vector<std::size_t> aMarkStack,
	                      bool aInsertMode, int anIndentLevel, std::string aFileName,
	                      bool aFileChanged)
	    : displayName(std::move(aDisplayName)), logLines(std::move(aLogLines)), hadError(aHadError),
	      transaction(std::move(aTransaction)), cursorOffset(aCursorOffset),
	      selectionStart(aSelectionStart), selectionEnd(aSelectionEnd), blockMode(aBlockMode),
	      blockMarkingOn(aBlockMarkingOn), blockAnchor(aBlockAnchor), blockEnd(aBlockEnd),
	      globalOrder(std::move(aGlobalOrder)), globalInts(std::move(aGlobalInts)),
	      globalStrings(std::move(aGlobalStrings)),
	      deferredUiCommands(std::move(aDeferredUiCommands)),
	      lastSearchValid(aLastSearchValid), lastSearchStart(aLastSearchStart),
	      lastSearchEnd(aLastSearchEnd), lastSearchCursor(aLastSearchCursor),
	      ignoreCase(anIgnoreCase), tabExpand(aTabExpand), markStack(std::move(aMarkStack)),
	      insertMode(aInsertMode), indentLevel(anIndentLevel), fileName(std::move(aFileName)),
	      fileChanged(aFileChanged) {
	}
};

struct TaskTiming {
	std::uint64_t queueMicros;
	std::uint64_t runMicros;
	std::uint64_t totalMicros;

	TaskTiming() noexcept : queueMicros(0), runMicros(0), totalMicros(0) {
	}

	double queueMs() const noexcept {
		return static_cast<double>(queueMicros) / 1000.0;
	}

	double runMs() const noexcept {
		return static_cast<double>(runMicros) / 1000.0;
	}

	double totalMs() const noexcept {
		return static_cast<double>(totalMicros) / 1000.0;
	}
};

struct Result {
	TaskInfo task;
	TaskStatus status;
	std::string error;
	std::shared_ptr<const Payload> payload;
	TaskTiming timing;

	Result() noexcept : task(), status(TaskStatus::Completed), error(), payload(), timing() {
	}

	[[nodiscard]] bool completed() const noexcept {
		return status == TaskStatus::Completed;
	}

	[[nodiscard]] bool cancelled() const noexcept {
		return status == TaskStatus::Cancelled;
	}

	[[nodiscard]] bool failed() const noexcept {
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
	                     std::string_view label, TaskFn fn);
	std::size_t pump(std::size_t maxResults = 8);
	[[nodiscard]] std::size_t pendingResults() const noexcept;
	void post(Result result);
	bool cancelTask(std::uint64_t taskId);
	void shutdown(bool drainResults = false);
	void cancelPending();

  private:
	struct Request {
		TaskInfo task;
		TaskFn fn;
		std::uint64_t submittedMicros;
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
	void forgetTask(std::uint64_t taskId);
	LaneState &laneState(Lane lane) noexcept;

	mutable std::mutex resultMutex;
	std::deque<Result> results;

	mutable std::mutex handlerMutex;
	ResultHandler resultHandler;

	std::uint64_t nextTaskId;
	std::mutex nextTaskMutex;
	std::mutex taskCancelMutex;
	std::unordered_map<std::uint64_t, std::shared_ptr<std::atomic_bool>> taskCancelFlags;
	std::atomic<bool> shuttingDown;

	LaneState ioLane;
	LaneState computeLane;
	LaneState macroLane;
};

Coprocessor &globalCoprocessor();

} // namespace coprocessor
} // namespace mr

#endif
