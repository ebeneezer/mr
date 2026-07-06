#ifndef MRCOPROCESSOR_HPP
#define MRCOPROCESSOR_HPP

#include <condition_variable>
#include <chrono>
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
#include "MRSyntax.hpp"
#include "MRVM.hpp"
#include "MRDiff.hpp"

namespace mr {
namespace coprocessor {

enum class Lane : unsigned char {
	Io,
	Compute,
	MiniMap,
	Macro,
	Extern
};

enum class ExternalSourceKind : unsigned char {
	File,
	Journal,
	Device,
	Network,
	Pipe
};

enum class TaskKind : unsigned char {
	Custom,
	LineIndexWarmup,
	SyntaxWarmup,
	FoldWarmup,
	MiniMapWarmup,
	SaveNormalizationWarmup,
	FileCompare,
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

	TaskInfo() noexcept : id(0), lane(Lane::Compute), kind(TaskKind::Custom), documentId(0), baseVersion(0), label(), cancelFlag() {
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
	Insert,
	WordWrap,
	TaskMarker,
	StatusNotice
};

struct IndicatorBlinkPayload final : Payload {
	std::size_t indicatorId;
	std::size_t generation;
	bool visible;
	IndicatorBlinkChannel channel;

	IndicatorBlinkPayload() noexcept : indicatorId(0), generation(0), visible(true), channel(IndicatorBlinkChannel::ReadOnly) {
	}

	IndicatorBlinkPayload(std::size_t aIndicatorId, std::size_t aGeneration, bool aVisible, IndicatorBlinkChannel aChannel = IndicatorBlinkChannel::ReadOnly) noexcept : indicatorId(aIndicatorId), generation(aGeneration), visible(aVisible), channel(aChannel) {
	}
};

struct FileComparePayload final : Payload {
	std::size_t originalDocumentId;
	std::size_t originalBaseVersion;
	std::size_t compareDocumentId;
	std::size_t compareBaseVersion;
	std::size_t originalLineCount;
	std::size_t compareLineCount;
	std::vector<mr::diff::MRDiffHunk> hunks;

	FileComparePayload() noexcept : originalDocumentId(0), originalBaseVersion(0), compareDocumentId(0), compareBaseVersion(0), originalLineCount(0), compareLineCount(0), hunks() {
	}

	FileComparePayload(std::size_t aOriginalDocumentId, std::size_t aOriginalBaseVersion, std::size_t aCompareDocumentId, std::size_t aCompareBaseVersion, std::size_t aOriginalLineCount, std::size_t aCompareLineCount, std::vector<mr::diff::MRDiffHunk> aHunks) : originalDocumentId(aOriginalDocumentId), originalBaseVersion(aOriginalBaseVersion), compareDocumentId(aCompareDocumentId), compareBaseVersion(aCompareBaseVersion), originalLineCount(aOriginalLineCount), compareLineCount(aCompareLineCount), hunks(std::move(aHunks)) {
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
	MRSyntaxLineResult syntaxLine;

	SyntaxWarmLine() noexcept : lineStart(0), syntaxLine() {
	}

	SyntaxWarmLine(std::size_t aLineStart, MRSyntaxLineResult aSyntaxLine) : lineStart(aLineStart), syntaxLine(std::move(aSyntaxLine)) {
	}
};

struct SyntaxWarmupPayload final : Payload {
	MRSyntaxLanguage language;
	std::vector<SyntaxWarmLine> lines;

	SyntaxWarmupPayload() noexcept : language(MRSyntaxLanguage::PlainText), lines() {
	}

	SyntaxWarmupPayload(MRSyntaxLanguage aLanguage, std::vector<SyntaxWarmLine> aLines) : language(aLanguage), lines(std::move(aLines)) {
	}
};

struct MiniMapWarmupPayload final : Payload {
	bool braille;
	int rowCount;
	int bodyWidth;
	std::size_t totalLines;
	std::size_t windowStartLine;
	std::size_t windowLineCount;
	int viewportWidth;
	std::vector<unsigned char> rowPatterns;
	std::vector<std::size_t> rowLineStarts;
	std::vector<std::size_t> rowLineEnds;

	MiniMapWarmupPayload() noexcept : braille(true), rowCount(0), bodyWidth(0), totalLines(1), windowStartLine(0), windowLineCount(1), viewportWidth(1), rowPatterns(), rowLineStarts(), rowLineEnds() {
	}

	MiniMapWarmupPayload(bool aBraille, int aRowCount, int aBodyWidth, std::size_t aTotalLines, std::size_t aWindowStartLine, std::size_t aWindowLineCount, int aViewportWidth, std::vector<unsigned char> aRowPatterns, std::vector<std::size_t> aRowLineStarts, std::vector<std::size_t> aRowLineEnds) : braille(aBraille), rowCount(aRowCount), bodyWidth(aBodyWidth), totalLines(aTotalLines), windowStartLine(aWindowStartLine), windowLineCount(aWindowLineCount), viewportWidth(aViewportWidth), rowPatterns(std::move(aRowPatterns)), rowLineStarts(std::move(aRowLineStarts)), rowLineEnds(std::move(aRowLineEnds)) {
	}
};

struct SaveNormalizationWarmupPayload final : Payload {
	std::size_t optionsHash;
	std::size_t sourceBytes;

	SaveNormalizationWarmupPayload() noexcept : optionsHash(0), sourceBytes(0) {
	}

	SaveNormalizationWarmupPayload(std::size_t aOptionsHash, std::size_t aSourceBytes) noexcept : optionsHash(aOptionsHash), sourceBytes(aSourceBytes) {
	}
};

struct ExternalIoChunkPayload final : Payload {
	std::size_t channelId;
	std::size_t targetBufferId;
	std::size_t searchHitCount;
	std::string text;

	ExternalIoChunkPayload() noexcept : channelId(0), targetBufferId(0), searchHitCount(0), text() {
	}

	ExternalIoChunkPayload(std::size_t aChannelId, std::string aText, std::size_t aTargetBufferId = 0, std::size_t aSearchHitCount = 0) : channelId(aChannelId), targetBufferId(aTargetBufferId), searchHitCount(aSearchHitCount), text(std::move(aText)) {
	}
};

struct ExternalIoFinishedPayload final : Payload {
	std::size_t channelId;
	std::size_t targetBufferId;
	int exitCode;
	bool signaled;
	int signalNumber;
	std::string successAudioUri;
	std::string failureAudioUri;
	std::string buildSourcePath;
	std::string buildSourceDir;
	std::string buildSourceFile;
	std::string buildSourceStem;
	std::string buildOutputPath;
	std::string buildPdfPath;
	std::string buildProfileId;
	std::string buildProfileName;
	std::string buildToolchain;
	std::string postBuildMacro;

	ExternalIoFinishedPayload() noexcept : channelId(0), targetBufferId(0), exitCode(0), signaled(false), signalNumber(0), successAudioUri(), failureAudioUri(), buildSourcePath(), buildSourceDir(), buildSourceFile(), buildSourceStem(), buildOutputPath(), buildPdfPath(), buildProfileId(), buildProfileName(), buildToolchain(), postBuildMacro() {
	}

	ExternalIoFinishedPayload(std::size_t aChannelId, int aExitCode, bool aSignaled, int aSignalNumber, std::size_t aTargetBufferId = 0, std::string aSuccessAudioUri = std::string(), std::string aFailureAudioUri = std::string()) : channelId(aChannelId), targetBufferId(aTargetBufferId), exitCode(aExitCode), signaled(aSignaled), signalNumber(aSignalNumber), successAudioUri(std::move(aSuccessAudioUri)), failureAudioUri(std::move(aFailureAudioUri)), buildSourcePath(), buildSourceDir(), buildSourceFile(), buildSourceStem(), buildOutputPath(), buildPdfPath(), buildProfileId(), buildProfileName(), buildToolchain(), postBuildMacro() {
	}
};

struct MacroJobFinishedPayload final : Payload {
	std::string displayName;
	std::vector<std::string> logLines;
	std::vector<MRMacroExecUiCommandRequest> execUiCommandRequests;
	bool hadError;

	MacroJobFinishedPayload() noexcept : displayName(), logLines(), execUiCommandRequests(), hadError(false) {
	}

	MacroJobFinishedPayload(std::string aDisplayName, std::vector<std::string> aLogLines, bool aHadError) : displayName(std::move(aDisplayName)), logLines(std::move(aLogLines)), execUiCommandRequests(), hadError(aHadError) {
	}

	MacroJobFinishedPayload(std::string aDisplayName, std::vector<std::string> aLogLines, std::vector<MRMacroExecUiCommandRequest> requests, bool aHadError) : displayName(std::move(aDisplayName)), logLines(std::move(aLogLines)), execUiCommandRequests(std::move(requests)), hadError(aHadError) {
	}
};

struct MacroJobStagedPayload final : Payload {
	std::string displayName;
	std::vector<std::string> logLines;
	bool hadError;
	MacroCommitConflictSnapshot conflictSnapshot;
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

	MacroJobStagedPayload() noexcept : displayName(), logLines(), hadError(false), conflictSnapshot(), transaction(), cursorOffset(0), selectionStart(0), selectionEnd(0), blockMode(0), blockMarkingOn(false), blockAnchor(0), blockEnd(0), globalOrder(), globalInts(), globalStrings(), deferredUiCommands(), lastSearchValid(false), lastSearchStart(0), lastSearchEnd(0), lastSearchCursor(0), ignoreCase(false), tabExpand(true), markStack(), insertMode(true), indentLevel(1), fileName(), fileChanged(false) {
	}

	MacroJobStagedPayload(std::string aDisplayName, std::vector<std::string> aLogLines, bool aHadError, MacroCommitConflictSnapshot aConflictSnapshot, mr::editor::StagedEditTransaction aTransaction, std::size_t aCursorOffset, std::size_t aSelectionStart, std::size_t aSelectionEnd, int aBlockMode, bool aBlockMarkingOn, std::size_t aBlockAnchor, std::size_t aBlockEnd, std::vector<std::string> aGlobalOrder, std::map<std::string, int> aGlobalInts, std::map<std::string, std::string> aGlobalStrings, std::vector<MRMacroDeferredUiCommand> aDeferredUiCommands, bool aLastSearchValid, std::size_t aLastSearchStart, std::size_t aLastSearchEnd, std::size_t aLastSearchCursor, bool anIgnoreCase, bool aTabExpand, std::vector<std::size_t> aMarkStack, bool aInsertMode, int anIndentLevel, std::string aFileName, bool aFileChanged)
	    : displayName(std::move(aDisplayName)), logLines(std::move(aLogLines)), hadError(aHadError), conflictSnapshot(std::move(aConflictSnapshot)), transaction(std::move(aTransaction)), cursorOffset(aCursorOffset), selectionStart(aSelectionStart), selectionEnd(aSelectionEnd), blockMode(aBlockMode), blockMarkingOn(aBlockMarkingOn), blockAnchor(aBlockAnchor), blockEnd(aBlockEnd), globalOrder(std::move(aGlobalOrder)), globalInts(std::move(aGlobalInts)), globalStrings(std::move(aGlobalStrings)), deferredUiCommands(std::move(aDeferredUiCommands)), lastSearchValid(aLastSearchValid), lastSearchStart(aLastSearchStart), lastSearchEnd(aLastSearchEnd), lastSearchCursor(aLastSearchCursor), ignoreCase(anIgnoreCase), tabExpand(aTabExpand), markStack(std::move(aMarkStack)), insertMode(aInsertMode), indentLevel(anIndentLevel), fileName(std::move(aFileName)), fileChanged(aFileChanged) {
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

struct ActiveTaskSnapshot {
	std::size_t workerSlot;
	TaskInfo task;
	std::uint64_t queueMicros;
	std::uint64_t runMicros;

	ActiveTaskSnapshot() noexcept : workerSlot(0), task(), queueMicros(0), runMicros(0) {
	}
};

struct LaneSnapshot {
	Lane lane;
	std::size_t workerCount;
	bool active;
	TaskInfo activeTask;
	std::uint64_t activeQueueMicros;
	std::uint64_t activeRunMicros;
	std::vector<ActiveTaskSnapshot> activeTasks;
	std::vector<TaskInfo> queuedTasks;

	LaneSnapshot() noexcept : lane(Lane::Compute), workerCount(1), active(false), activeTask(), activeQueueMicros(0), activeRunMicros(0), activeTasks(), queuedTasks() {
	}
};

struct ExternalSourceSnapshot {
	std::size_t sourceId;
	ExternalSourceKind kind;
	std::string tag;
	std::string displayName;
	unsigned char colorIndex;
	bool running;
	bool active;
	std::uint64_t taskId;
	std::uint64_t receivedBytes;
	std::uint64_t activitySequence;
	std::string streamSample;

	ExternalSourceSnapshot() noexcept : sourceId(0), kind(ExternalSourceKind::File), tag(), displayName(), colorIndex(0), running(false), active(false), taskId(0), receivedBytes(0), activitySequence(0), streamSample() {
	}
};

struct Snapshot {
	std::size_t pendingResults;
	std::vector<LaneSnapshot> lanes;
	std::vector<ExternalSourceSnapshot> externalSources;

	Snapshot() noexcept : pendingResults(0), lanes(), externalSources() {
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
	std::uint64_t submit(Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, std::string_view label, TaskFn fn);
	std::uint64_t submitCoalesced(Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, std::string_view coalescingKey, std::string_view label, TaskFn fn);
	std::size_t registerExternalSource(ExternalSourceKind kind, std::string_view displayName);
	std::uint64_t submitExternal(std::size_t sourceId, std::string_view label, TaskFn fn);
	bool cancelExternalSource(std::size_t sourceId);
	void unregisterExternalSource(std::size_t sourceId);
	std::size_t pump(std::size_t maxResults = 8);
	std::size_t pumpFor(std::chrono::microseconds budget);
	[[nodiscard]] std::size_t pendingResults() const noexcept;
	[[nodiscard]] bool hasTaskState(std::uint64_t taskId) noexcept;
	void post(Result result);
	bool cancelTask(std::uint64_t taskId);
	void shutdown(bool drainResults = false);
	void cancelPending();
	[[nodiscard]] Snapshot snapshot() const;

  private:
	enum class ComputePriority : unsigned char {
		High,
		Normal,
		Low
	};
	struct Request {
		TaskInfo task;
		TaskFn fn;
		std::string coalescingKey;
		std::uint64_t submittedMicros;
		ComputePriority computePriority;

		Request() noexcept : task(), fn(), coalescingKey(), submittedMicros(0), computePriority(ComputePriority::Normal) {
		}
	};

	struct ActiveTaskState {
		std::size_t workerSlot;
		TaskInfo task;
		std::uint64_t submittedMicros;
		std::uint64_t startedMicros;
		ComputePriority computePriority;

		ActiveTaskState() noexcept : workerSlot(0), task(), submittedMicros(0), startedMicros(0), computePriority(ComputePriority::Normal) {
		}
	};

	struct LaneState {
		Lane lane;
		std::size_t externalSourceId;
		mutable std::mutex mutex;
		std::condition_variable_any cv;
		std::deque<Request> queue;
		std::deque<Request> highQueue;
		std::deque<Request> normalQueue;
		std::deque<Request> lowQueue;
		std::vector<ActiveTaskState> activeTasks;
		std::vector<std::jthread> workers;

		explicit LaneState(Lane aLane, std::size_t aExternalSourceId = 0) noexcept : lane(aLane), externalSourceId(aExternalSourceId), mutex(), cv(), queue(), highQueue(), normalQueue(), lowQueue(), activeTasks(), workers() {
		}
	};

	struct ExternalSourceState {
		std::size_t sourceId;
		ExternalSourceKind kind;
		std::string tag;
		std::string displayName;
		unsigned char colorIndex;
		bool running;
		bool active;
		std::uint64_t taskId;
		std::uint64_t receivedBytes;
		std::uint64_t activitySequence;
		std::string streamSample;
		std::unique_ptr<LaneState> lane;

		ExternalSourceState() noexcept : sourceId(0), kind(ExternalSourceKind::File), tag(), displayName(), colorIndex(0), running(false), active(false), taskId(0), receivedBytes(0), activitySequence(0), streamSample(), lane() {
		}
	};

	void startLane(LaneState &lane);
	void workerLoop(LaneState &lane, std::size_t workerSlot, std::stop_token stopToken);
	std::uint64_t submitToLaneState(LaneState &targetLaneState, Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, std::string_view coalescingKey, std::string_view label, TaskFn fn);
	void enqueueResult(Result result);
	void forgetTask(std::uint64_t taskId);
	void noteExternalResult(const Result &result);
	ExternalSourceState *findExternalSourceLocked(std::size_t sourceId) noexcept;
	const ExternalSourceState *findExternalSourceLocked(std::size_t sourceId) const noexcept;
	LaneState &laneState(Lane lane) noexcept;
	ComputePriority computePriorityForTask(TaskKind kind) const noexcept;
	bool laneHasQueuedWorkLocked(const LaneState &lane) const noexcept;
	bool popNextRequestLocked(LaneState &lane, Request &request) noexcept;
	std::size_t laneWorkerCount(Lane lane) const noexcept;

	mutable std::mutex resultMutex;
	std::deque<Result> results;

	mutable std::mutex handlerMutex;
	ResultHandler resultHandler;

	std::atomic<std::uint64_t> nextTaskId;
	std::mutex taskCancelMutex;
	std::unordered_map<std::uint64_t, std::shared_ptr<std::atomic_bool>> taskCancelFlags;
	std::atomic<bool> shuttingDown;

	LaneState ioLane;
	LaneState computeLane;
	LaneState miniMapLane;
	LaneState macroLane;
	std::atomic<std::uint64_t> nextExternalSourceId;
	std::atomic<std::uint64_t> nextExternalActivitySequence;
	mutable std::mutex externalMutex;
	std::vector<ExternalSourceState> externalSources;
};

Coprocessor &globalCoprocessor();

} // namespace coprocessor
} // namespace mr

#endif
