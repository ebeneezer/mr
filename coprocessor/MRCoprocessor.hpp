#ifndef MRCOPROCESSOR_HPP
#define MRCOPROCESSOR_HPP

#include <array>
#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
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
	DisplayWidthWarmup,
	SyntaxWarmup,
	FoldWarmup,
	MiniMapWarmup,
	FileCompare,
	ExternalIo,
	MacroJob,
	HexPaneProjection,
	BentoDiagnosticsProjection,
	BentoOutlineProjection
};

constexpr std::size_t kTaskKindCount = static_cast<std::size_t>(TaskKind::BentoOutlineProjection) + 1;

enum class TaskStatus : unsigned char {
	Completed,
	Cancelled,
	Failed
};

enum class ExecutionOwnerKind : unsigned char {
	Unspecified,
	EditorWindow,
	BentoPane,
	HexPane,
	ExternalSource,
	MacroSession,
	ProcessChannel,
	Worker
};

constexpr std::size_t kExecutionOwnerKindCount = static_cast<std::size_t>(ExecutionOwnerKind::Worker) + 1;

enum class WorkDirection : unsigned char {
	None,
	Bof,
	Eof
};

enum class WorkerLifecycleState : unsigned char {
	Created,
	Assigned,
	Idle,
	Queued,
	Running,
	ResultReady,
	Accepted,
	Adopted,
	Discarded,
	Stopping,
	Finished
};

enum class LifecycleReason : unsigned char {
	None,
	TaskCompleted,
	TaskCancelled,
	TaskFailed,
	ResultAccepted,
	ResultAdopted,
	ResultRejected,
	AcceptedResultAdopted,
	AcceptedResultRejected,
	StreamChunk,
	StopRequested,
	WorkerFinished
};

constexpr std::uint64_t kInvalidWorkerOrdinal = static_cast<std::uint64_t>(-1);

struct TaskInfo {
	std::uint64_t id;
	Lane lane;
	TaskKind kind;
	std::size_t documentId;
	std::size_t baseVersion;
	std::string label;
	std::shared_ptr<std::atomic_bool> cancelFlag;
	std::uint64_t workerOrdinal;
	std::uint64_t osThreadId;
	int assignedCore;
	int affinityResult;
	ExecutionOwnerKind executionOwnerKind;
	std::size_t executionOwnerLocalId;
	bool finiteWorker;
	std::uint64_t generation;
	WorkDirection direction;
	bool hasPacketSpan;
	std::uint64_t packetStart;
	std::uint64_t packetEnd;

	TaskInfo() noexcept : id(0), lane(Lane::Compute), kind(TaskKind::Custom), documentId(0), baseVersion(0), label(), cancelFlag(), workerOrdinal(kInvalidWorkerOrdinal), osThreadId(0), assignedCore(-1), affinityResult(-1), executionOwnerKind(ExecutionOwnerKind::Unspecified), executionOwnerLocalId(0), finiteWorker(false), generation(0), direction(WorkDirection::None), hasPacketSpan(false), packetStart(0), packetEnd(0) {
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

struct FileComparePayload : Payload {
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
	mr::editor::LineIndexScanPacket packet;

	LineIndexWarmupPayload() noexcept : packet() {
	}

	explicit LineIndexWarmupPayload(mr::editor::LineIndexScanPacket aPacket) : packet(std::move(aPacket)) {
	}
};

struct DisplayWidthWarmupPayload final : Payload {
	std::uint64_t generation;
	std::uint64_t startLine;
	std::uint64_t endLine;
	int maximumWidth;

	DisplayWidthWarmupPayload() noexcept : generation(0), startLine(0), endLine(0), maximumWidth(1) {
	}

	DisplayWidthWarmupPayload(std::uint64_t aGeneration, std::uint64_t aStartLine, std::uint64_t aEndLine, int aMaximumWidth) noexcept : generation(aGeneration), startLine(aStartLine), endLine(aEndLine), maximumWidth(aMaximumWidth) {
	}
};

struct SyntaxWarmLine {
	std::size_t lineStart;
	std::size_t lineIndex;
	MRSyntaxLineState stateIn;
	MRSyntaxLineResult syntaxLine;

	SyntaxWarmLine() noexcept : lineStart(0), lineIndex(0), stateIn(), syntaxLine() {
	}

	SyntaxWarmLine(std::size_t aLineStart, std::size_t aLineIndex, MRSyntaxLineState aStateIn, MRSyntaxLineResult aSyntaxLine)
	    : lineStart(aLineStart), lineIndex(aLineIndex), stateIn(aStateIn), syntaxLine(std::move(aSyntaxLine)) {
	}
};

struct SyntaxWarmCheckpoint {
	std::size_t lineStart;
	std::size_t lineIndex;
	MRSyntaxLineState stateIn;

	SyntaxWarmCheckpoint() noexcept : lineStart(0), lineIndex(0), stateIn() {
	}

	SyntaxWarmCheckpoint(std::size_t aLineStart, std::size_t aLineIndex, MRSyntaxLineState aStateIn) noexcept : lineStart(aLineStart), lineIndex(aLineIndex), stateIn(aStateIn) {
	}
};

struct SyntaxWarmupPayload final : Payload {
	std::uint64_t generation;
	MRSyntaxLanguage language;
	std::size_t startLine;
	std::size_t endLine;
	std::size_t materializedStartLine;
	std::size_t materializedEndLine;
	MRSyntaxLineState stateIn;
	std::vector<SyntaxWarmCheckpoint> checkpoints;
	std::vector<SyntaxWarmLine> lines;

	SyntaxWarmupPayload() noexcept
	    : generation(0), language(MRSyntaxLanguage::PlainText), startLine(0), endLine(0), materializedStartLine(0), materializedEndLine(0), stateIn(), checkpoints(), lines() {
	}

	SyntaxWarmupPayload(std::uint64_t aGeneration, MRSyntaxLanguage aLanguage, std::size_t aStartLine, std::size_t aEndLine, std::size_t aMaterializedStartLine,
	                    std::size_t aMaterializedEndLine, MRSyntaxLineState aStateIn, std::vector<SyntaxWarmCheckpoint> aCheckpoints,
	                    std::vector<SyntaxWarmLine> aLines)
	    : generation(aGeneration), language(aLanguage), startLine(aStartLine), endLine(aEndLine), materializedStartLine(aMaterializedStartLine), materializedEndLine(aMaterializedEndLine),
	      stateIn(aStateIn), checkpoints(std::move(aCheckpoints)), lines(std::move(aLines)) {
	}
};

struct MiniMapWarmupPayload final : Payload {
	std::uint64_t generation;
	bool braille;
	int rowCount;
	int bodyWidth;
	int packetRowStart;
	int packetRowEnd;
	std::size_t totalLines;
	std::size_t windowStartLine;
	std::size_t windowLineCount;
	int viewportWidth;
	std::vector<unsigned char> rowPatterns;
	std::vector<std::size_t> rowLineStarts;
	std::vector<std::size_t> rowLineEnds;

	MiniMapWarmupPayload() noexcept
	    : generation(0), braille(true), rowCount(0), bodyWidth(0), packetRowStart(0), packetRowEnd(0), totalLines(1), windowStartLine(0), windowLineCount(1), viewportWidth(1),
	      rowPatterns(), rowLineStarts(), rowLineEnds() {
	}

	MiniMapWarmupPayload(std::uint64_t aGeneration, bool aBraille, int aRowCount, int aBodyWidth, int aPacketRowStart, int aPacketRowEnd, std::size_t aTotalLines,
	                     std::size_t aWindowStartLine, std::size_t aWindowLineCount, int aViewportWidth, std::vector<unsigned char> aRowPatterns,
	                     std::vector<std::size_t> aRowLineStarts, std::vector<std::size_t> aRowLineEnds)
	    : generation(aGeneration), braille(aBraille), rowCount(aRowCount), bodyWidth(aBodyWidth), packetRowStart(aPacketRowStart), packetRowEnd(aPacketRowEnd), totalLines(aTotalLines),
	      windowStartLine(aWindowStartLine), windowLineCount(aWindowLineCount), viewportWidth(aViewportWidth), rowPatterns(std::move(aRowPatterns)),
	      rowLineStarts(std::move(aRowLineStarts)), rowLineEnds(std::move(aRowLineEnds)) {
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
	mutable bool dispositionRecorded;
	std::uint64_t resultReadyMicros;

	Result() noexcept : task(), status(TaskStatus::Completed), error(), payload(), timing(), dispositionRecorded(false), resultReadyMicros(0) {
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

struct DeferredResultLifecycle {
	TaskInfo task;
	TaskStatus taskStatus;
	TaskTiming timing;
	std::uint64_t resultReadyMicros;
	bool valid;

	DeferredResultLifecycle() noexcept : task(), taskStatus(TaskStatus::Completed), timing(), resultReadyMicros(0), valid(false) {
	}
};

struct WorkerLifecycleEvent {
	std::uint64_t sequence;
	std::uint64_t monotonicMicros;
	WorkerLifecycleState state;
	LifecycleReason reason;
	TaskStatus taskStatus;
	std::uint64_t workerOrdinal;
	std::uint64_t osThreadId;
	int assignedCore;
	int affinityResult;
	ExecutionOwnerKind executionOwnerKind;
	std::size_t executionOwnerLocalId;
	Lane lane;
	TaskKind taskKind;
	std::uint64_t taskId;
	std::size_t documentId;
	std::size_t baseVersion;
	std::uint64_t generation;
	WorkDirection direction;
	bool hasPacketSpan;
	std::uint64_t packetStart;
	std::uint64_t packetEnd;
	std::uint64_t queueMicros;
	std::uint64_t runMicros;
	std::uint64_t totalMicros;
	std::uint64_t acceptanceMicros;
	std::uint64_t adoptionMicros;

	WorkerLifecycleEvent() noexcept : sequence(0), monotonicMicros(0), state(WorkerLifecycleState::Created), reason(LifecycleReason::None), taskStatus(TaskStatus::Completed), workerOrdinal(kInvalidWorkerOrdinal), osThreadId(0), assignedCore(-1), affinityResult(-1), executionOwnerKind(ExecutionOwnerKind::Unspecified), executionOwnerLocalId(0), lane(Lane::Compute), taskKind(TaskKind::Custom), taskId(0), documentId(0), baseVersion(0), generation(0), direction(WorkDirection::None), hasPacketSpan(false), packetStart(0), packetEnd(0), queueMicros(0), runMicros(0), totalMicros(0), acceptanceMicros(0), adoptionMicros(0) {
	}
};

struct WorkerSnapshot {
	std::uint64_t workerOrdinal;
	std::uint64_t osThreadId;
	int assignedCore;
	int affinityResult;
	ExecutionOwnerKind executionOwnerKind;
	std::size_t executionOwnerLocalId;
	Lane lane;
	WorkerLifecycleState state;
	TaskInfo task;
	std::size_t queuedTaskCount;
	std::uint64_t queueMicros;
	std::uint64_t runMicros;

	WorkerSnapshot() noexcept : workerOrdinal(kInvalidWorkerOrdinal), osThreadId(0), assignedCore(-1), affinityResult(-1), executionOwnerKind(ExecutionOwnerKind::Unspecified), executionOwnerLocalId(0), lane(Lane::Compute), state(WorkerLifecycleState::Created), task(), queuedTaskCount(0), queueMicros(0), runMicros(0) {
	}
};

struct WorkerActivitySnapshot {
	std::uint64_t windowMicros;
	std::uint64_t peakActiveCount;
	std::uint64_t peakQueuedCount;
	std::uint64_t peakResultCount;
	std::uint64_t createdCount;
	std::uint64_t finishedCount;
	std::uint64_t queuedCount;
	std::uint64_t completedCount;
	std::uint64_t cancelledCount;
	std::uint64_t failedCount;
	std::uint64_t acceptedCount;
	std::uint64_t adoptedCount;
	std::uint64_t discardedCount;
	std::uint64_t queueMicros;
	std::uint64_t runMicros;
	std::uint64_t acceptanceMicros;
	std::uint64_t adoptionMicros;

	WorkerActivitySnapshot() noexcept
	    : windowMicros(1000000), peakActiveCount(0), peakQueuedCount(0), peakResultCount(0), createdCount(0), finishedCount(0), queuedCount(0), completedCount(0), cancelledCount(0), failedCount(0), acceptedCount(0), adoptedCount(0), discardedCount(0), queueMicros(0), runMicros(0), acceptanceMicros(0), adoptionMicros(0) {
	}
};

struct WorkerTelemetrySnapshot {
	std::vector<int> allowedCoreIds;
	std::vector<WorkerSnapshot> workers;
	std::vector<WorkerLifecycleEvent> recentEvents;
	WorkerActivitySnapshot recentActivity;
	std::uint64_t latestEventSequence;
	std::uint64_t createdCount;
	std::uint64_t finishedCount;
	std::uint64_t affinityFailureCount;
	std::array<std::uint64_t, kTaskKindCount> oneShotCreatedByTaskKind;
	std::array<std::uint64_t, kTaskKindCount> oneShotFinishedByTaskKind;
	std::array<std::uint64_t, kExecutionOwnerKindCount> oneShotCreatedByOwnerKind;
	std::array<std::uint64_t, kExecutionOwnerKindCount> oneShotFinishedByOwnerKind;

	WorkerTelemetrySnapshot() noexcept
	    : allowedCoreIds(), workers(), recentEvents(), recentActivity(), latestEventSequence(0), createdCount(0), finishedCount(0), affinityFailureCount(0), oneShotCreatedByTaskKind(),
	      oneShotFinishedByTaskKind(), oneShotCreatedByOwnerKind(), oneShotFinishedByOwnerKind() {
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

using TaskFn = std::function<Result(const TaskInfo &)>;
using ResultHandler = std::function<void(const Result &)>;

class Coprocessor {
  public:
	Coprocessor();
	~Coprocessor();

	Coprocessor(const Coprocessor &) = delete;
	Coprocessor &operator=(const Coprocessor &) = delete;

	void setResultHandler(ResultHandler handler);
	std::uint64_t submit(Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, ExecutionOwnerKind ownerKind, std::size_t ownerLocalId, std::string_view label, TaskFn fn);
	std::uint64_t submitPacket(Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, ExecutionOwnerKind ownerKind, std::size_t ownerLocalId, std::uint64_t generation, WorkDirection direction, std::uint64_t packetStart, std::uint64_t packetEnd, std::string_view label, TaskFn fn);
	std::uint64_t registerWorker(Lane lane, ExecutionOwnerKind ownerKind, std::size_t ownerLocalId);
	void unregisterWorker(std::uint64_t workerOrdinal);
	std::uint64_t submitWorker(std::uint64_t workerOrdinal, TaskKind kind, std::size_t documentId, std::size_t baseVersion, std::string_view label, TaskFn fn);
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
	[[nodiscard]] WorkerTelemetrySnapshot telemetrySnapshot(std::size_t maxEvents = 32) const;
	[[nodiscard]] std::size_t allowedCoreCount() const noexcept;
	void noteResultAdoption(const Result &result, bool adopted);
	DeferredResultLifecycle acceptResultForDeferredAdoption(const Result &result);
	void resolveDeferredResultAdoption(DeferredResultLifecycle &lifecycle, bool adopted);

  private:
	struct Request {
		TaskInfo task;
		TaskFn fn;
		std::uint64_t submittedMicros;

		Request() noexcept : task(), fn(), submittedMicros(0) {
		}
	};

	struct ActiveTaskState {
		std::size_t workerSlot;
		TaskInfo task;
		std::uint64_t submittedMicros;
		std::uint64_t startedMicros;

		ActiveTaskState() noexcept : workerSlot(0), task(), submittedMicros(0), startedMicros(0) {
		}
	};

	struct LaneState {
		Lane lane;
		bool retireAfterTask;
		bool retired;
		std::atomic_bool stopRequested;
		std::uint64_t workerOrdinal;
		std::uint64_t osThreadId;
		int assignedCore;
		int affinityResult;
		ExecutionOwnerKind executionOwnerKind;
		std::size_t executionOwnerLocalId;
		WorkerLifecycleState lifecycleState;
		TaskInfo currentTask;
		std::uint64_t submittedMicros;
		std::uint64_t startedMicros;
		std::uint64_t finishedMicros;
		mutable std::mutex mutex;
		std::condition_variable cv;
		std::deque<Request> queue;
		std::vector<ActiveTaskState> activeTasks;
		std::thread worker;

		explicit LaneState(Lane aLane, bool retire, ExecutionOwnerKind ownerKind, std::size_t ownerLocalId) noexcept : lane(aLane), retireAfterTask(retire), retired(false), stopRequested(false), workerOrdinal(kInvalidWorkerOrdinal), osThreadId(0), assignedCore(-1), affinityResult(-1), executionOwnerKind(ownerKind), executionOwnerLocalId(ownerLocalId), lifecycleState(WorkerLifecycleState::Created), currentTask(), submittedMicros(0), startedMicros(0), finishedMicros(0), mutex(), cv(), queue(), activeTasks(), worker() {
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
	void workerLoop(LaneState &lane, std::uint64_t workerOrdinal);
	void requestLaneStop(LaneState &lane);
	std::uint64_t submitToLaneState(LaneState &targetLaneState, Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, std::uint64_t generation, WorkDirection direction, bool hasPacketSpan, std::uint64_t packetStart, std::uint64_t packetEnd, std::string_view label, TaskFn fn);
	std::uint64_t submitOneShot(Lane lane, TaskKind kind, std::size_t documentId, std::size_t baseVersion, ExecutionOwnerKind ownerKind, std::size_t ownerLocalId, std::uint64_t generation, WorkDirection direction, bool hasPacketSpan, std::uint64_t packetStart, std::uint64_t packetEnd, std::string_view label, TaskFn fn);
	void reapRetiredWorkers();
	void enqueueResult(Result result);
	void forgetTask(std::uint64_t taskId);
	void noteExternalResult(const Result &result);
	void recordWorkerLifecycle(WorkerLifecycleState state, const LaneState &lane, const TaskInfo *task, TaskStatus taskStatus, const TaskTiming *timing = nullptr, LifecycleReason reason = LifecycleReason::None);
	void recordTaskLifecycle(WorkerLifecycleState state, const TaskInfo &task, TaskStatus taskStatus, const TaskTiming *timing = nullptr, LifecycleReason reason = LifecycleReason::None, std::uint64_t decisionMicros = 0);
	void recordOneShotWorkerCreated(TaskKind taskKind, ExecutionOwnerKind ownerKind) noexcept;
	void recordOneShotWorkerFinished(TaskKind taskKind, ExecutionOwnerKind ownerKind) noexcept;
	void markLaneStopping(LaneState &lane);
	[[nodiscard]] static std::vector<int> detectAllowedCoreIds() noexcept;
	[[nodiscard]] int assignCurrentWorkerCore(std::uint64_t workerOrdinal, int &assignedCore, std::uint64_t &osThreadId) const noexcept;
	ExternalSourceState *findExternalSourceLocked(std::size_t sourceId) noexcept;
	const ExternalSourceState *findExternalSourceLocked(std::size_t sourceId) const noexcept;
	LaneState *findWorkerLocked(std::uint64_t workerOrdinal) noexcept;
	bool laneHasQueuedWorkLocked(const LaneState &lane) const noexcept;
	bool popNextRequestLocked(LaneState &lane, Request &request) noexcept;

	mutable std::mutex resultMutex;
	std::deque<Result> results;

	mutable std::mutex handlerMutex;
	ResultHandler resultHandler;

	std::atomic<std::uint64_t> nextTaskId;
	std::atomic<std::uint64_t> nextWorkerOrdinal;
	std::mutex taskCancelMutex;
	std::unordered_map<std::uint64_t, std::shared_ptr<std::atomic_bool>> taskCancelFlags;
	std::atomic<bool> shuttingDown;

	std::vector<int> allowedCoreIds;
	mutable std::mutex workerMutex;
	std::vector<std::unique_ptr<LaneState>> workers;
	std::vector<std::unique_ptr<LaneState>> retiringWorkers;
	std::atomic<std::uint64_t> nextExternalSourceId;
	std::atomic<std::uint64_t> nextExternalActivitySequence;
	mutable std::mutex externalMutex;
	std::vector<ExternalSourceState> externalSources;

	mutable std::mutex telemetryMutex;
	std::deque<WorkerLifecycleEvent> lifecycleEvents;
	std::uint64_t nextLifecycleSequence;
	std::uint64_t createdWorkerCount;
	std::uint64_t finishedWorkerCount;
	std::uint64_t affinityFailureCount;
	std::array<std::uint64_t, kTaskKindCount> oneShotCreatedByTaskKind;
	std::array<std::uint64_t, kTaskKindCount> oneShotFinishedByTaskKind;
	std::array<std::uint64_t, kExecutionOwnerKindCount> oneShotCreatedByOwnerKind;
	std::array<std::uint64_t, kExecutionOwnerKindCount> oneShotFinishedByOwnerKind;
	std::uint64_t telemetryActiveTaskCount;
	std::uint64_t telemetryQueuedTaskCount;
	std::uint64_t telemetryResultCount;
};

Coprocessor &globalCoprocessor();

} // namespace coprocessor
} // namespace mr

#endif
