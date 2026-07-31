#ifndef MRVM_HPP
#define MRVM_HPP

#include <cstddef>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "MRTextDocument.hpp"
#include "MRMacroExecutionSession.hpp"
#include "ui/modeless/MRMacroModelessUi.hpp"
#include "../app/MRRuntimeScheduler.hpp"
#include "vm/MRVMProcedureCatalog.hpp"
#include "vm/MRVMProfile.hpp"

class MREditWindow;
class MRVMHashStore;
struct MacroCommitConflictSnapshot;
struct MRMacroDebuggerBreakpoint;
struct MRMacroModelessWindowDesktopState;
struct ExecutionState;
struct MRMacroStagedExecutionInput;

struct MRMacroExecUiCommandRequest {
	std::string closureId;
	std::string lvalue;
	std::string target;
	std::string command;
};

enum MRMacroDebugStopReason {
	mrdStopNone = 0,
	mrdStopBreakpoint,
	mrdStopStep,
	mrdStopPaused,
	mrdStopBudget,
	mrdStopCompleted,
	mrdStopCancelled,
	mrdStopError
};

enum MRMacroDebugStepMode : int {
	mrdStepNone = 0,
	mrdStepInto,
	mrdStepOver,
	mrdStepOut
};

enum MRMacroDebugVariableScope {
	mrdVariableLocal = 0,
	mrdVariableAppGlobal,
	mrdVariableClosure,
	mrdVariableSession
};

enum MRMacroDebugValuePathKind {
	mrdValueHashKey = 0,
	mrdValueArrayIndex
};

struct MRMacroDebugValuePathComponent {
	MRMacroDebugValuePathKind kind;
	std::string key;
	int index;

	MRMacroDebugValuePathComponent() : kind(mrdValueHashKey), key(), index(0) {
	}
};

struct MRMacroDebugVariableSnapshot {
	std::string name;
	std::string displayName;
	int type;
	std::string valueText;
	MRMacroDebugVariableScope scope;
	std::vector<MRMacroDebugValuePathComponent> path;
	int depth;
	bool hasChildren;
	bool cycleReference;

	MRMacroDebugVariableSnapshot() : name(), displayName(), type(0), valueText(), scope(mrdVariableLocal), path(), depth(0), hasChildren(false), cycleReference(false) {
	}
};

enum MRMacroDebugValueMutationAction {
	mrdValueSetScalar = 0,
	mrdValueAddHashEntry,
	mrdValueAppendArrayElement,
	mrdValueEraseElement,
	mrdValueRenameHashKey
};

struct MRMacroDebugValueMutation {
	MRMacroDebugValueMutationAction action;
	MRMacroDebugVariableSnapshot target;
	std::string key;
	int valueType;
	std::string valueText;

	MRMacroDebugValueMutation() : action(mrdValueSetScalar), target(), key(), valueType(0), valueText() {
	}
};

struct MRMacroDebugWatchSnapshot {
	std::string expression;
	int type;
	std::string valueText;
	std::string errorText;
	bool enabled;

	MRMacroDebugWatchSnapshot() : expression(), type(0), valueText(), errorText(), enabled(true) {
	}
};

enum MRMacroDebugStackFrameKind {
	mrdStackFrameCurrent = 0,
	mrdStackFrameCall,
	mrdStackFrameRunMacro
};

struct MRMacroDebugStackFrame {
	std::string macroKey;
	std::string sourcePath;
	std::size_t instructionOffset;
	int line;
	int column;
	MRMacroDebugStackFrameKind kind;

	MRMacroDebugStackFrame() : macroKey(), sourcePath(), instructionOffset(0), line(0), column(0), kind(mrdStackFrameCurrent) {
	}
};

struct MRMacroDebugRunResult {
	MRMacroDebugStopReason stopReason;
	std::size_t instructionOffset;
	std::size_t stackDepth;
	std::vector<MRMacroDebugVariableSnapshot> variables;
	std::vector<MRMacroDebugStackFrame> callStack;
	std::vector<std::string> logLines;
	std::string macroKey;
	std::string sourcePath;
	bool cancelled;
	bool hadError;
	bool paused;

	MRMacroDebugRunResult() : stopReason(mrdStopNone), instructionOffset(0), stackDepth(0), variables(), callStack(), logLines(), macroKey(), sourcePath(), cancelled(false), hadError(false), paused(false) {
	}
};

class VirtualMachine {
  public:
	struct Value {
		int type;
		int i;
		double r;
		std::string s;
		unsigned char c;
		int hashHandle;
		int arrayElementType;
		std::vector<Value> arrayValues;
		bool globalStorage;

		Value();
	};

  private:
	struct MRMacroDebugChildFrame;
	class BytecodeExecution;
	class DebugExecution;
	class ProcedureExecution;
	class ConfigurationProcedures;
	class RuntimeProcedures;
	class EditorProcedures;
	class MacroProcedures;
	class DelayState final {
		friend class VirtualMachine;
		friend class BytecodeExecution;
		friend class DebugExecution;

	 private:
		bool pending;
		bool ready;
		bool enabled;
		std::vector<unsigned char> bytecode;
		std::size_t length;
		std::size_t ip;
		std::vector<std::size_t> callStack;
		int returnInt;
		std::string returnStr;
		int errorLevel;
		std::string savedParameterString;
		bool macroFramePushed;
		std::chrono::steady_clock::time_point deadline;
		std::uint64_t generation;
		int millis;

		DelayState() noexcept;
		void clear() noexcept;
	};
	class DebugState final {
		friend class VirtualMachine;
		friend class BytecodeExecution;
		friend class DebugExecution;
		friend class MacroProcedures;

	 private:
		bool runActive;
		bool stopped;
		MRMacroDebugStopReason stopReason;
		std::size_t stopOffset;
		std::size_t stackDepth;
		std::vector<std::size_t> breakpointOffsets;
		bool paused;
		std::vector<unsigned char> bytecode;
		std::size_t length;
		std::size_t ip;
		std::vector<std::size_t> callStack;
		int returnInt;
		std::string returnStr;
		int errorLevel;
		std::string savedParameterString;
		std::string macroName;
		bool firstRun;
		bool skipCurrentOffset;
		bool pauseRequested;
		std::shared_ptr<std::atomic_bool> pauseSignal;
		std::size_t instructionBudget;
		MRMacroDebugStepMode stepMode;
		std::size_t stepOutDepth;
		std::string macroKey;
		std::string sourcePath;
		std::unique_ptr<MRMacroDebugChildFrame> childFrame;

		DebugState() noexcept;
		~DebugState();
		void capturePausedExecution(const unsigned char *sourceBytecode, std::size_t sourceLength, std::size_t sourceIp, const std::vector<std::size_t> &sourceCallStack, const ExecutionState &executionState, const std::string &sourceSavedParameterString, const std::string &sourceMacroName, bool sourceFirstRun);
		void clearPausedExecution() noexcept;
	};

	std::vector<Value> stack;
	std::map<std::string, Value> variables;
	std::unique_ptr<MRVMHashStore> mHashStore;
	std::string mClosureId;
	std::set<std::string> mClosureVariableNames;
	MRMacroExecutionSessionId mExecutionSessionId;
	std::set<std::string> mSessionVariableNames;
	std::vector<MRMacroExecUiCommandRequest> mExecUiCommandRequests;
	bool verboseLogging;
	bool logTruncated;
		DelayState delayState;
		DebugState debugState;
	enum class InstructionFlow : unsigned char {
		Completed,
		SkipPostInstruction,
		FinishExecution,
	};

		void appendLogLine(const std::string &line, bool important = false);
		void appendDebugVariables(MRMacroDebugRunResult &result) const;
		std::string debugValueText(const Value &value) const;
	void clearAsyncDelayState() noexcept;
	static int normalizeDelayMillis(int millis) noexcept;

	void push(const Value &value);
	Value pop();

  public:
	std::vector<std::string> log;
	bool cancelledExecution;

	VirtualMachine();
	~VirtualMachine();
	void setVerboseLogging(bool enable) noexcept {
		verboseLogging = enable;
	}
	int hashCreate();
	MRVMHashStore &localHashStore();
	const MRVMHashStore &localHashStore() const;
	bool hashContains(int handle, const std::string &key) const;
	Value hashRead(int handle, const std::string &key) const;
	void hashWrite(int handle, const std::string &key, const Value &value);
	void hashErase(int handle, const std::string &key);
	void setClosureContext(const std::string &closureId);
	void setExecutionSessionContext(MRMacroExecutionSessionId sessionId);
	const std::vector<MRMacroExecUiCommandRequest> &execUiCommandRequests() const noexcept;
	void execute(const unsigned char *bytecode, size_t length);
	void executeAt(const unsigned char *bytecode, size_t length, size_t entryOffset, const std::string &parameterString, const std::string &macroName, bool resetState, bool firstRun);
	void executeAt(const unsigned char *bytecode, size_t length, size_t entryOffset, const std::string &parameterString, const std::string &macroName, bool resetState, bool firstRun, bool preserveExecutionState);
	MRMacroDebugRunResult executeDebugAt(const unsigned char *bytecode, size_t length, size_t entryOffset, const std::string &parameterString, const std::string &macroName, const std::vector<std::size_t> &breakpointOffsets, bool firstRun = false,
	                                     const std::string &macroKey = std::string(), const std::string &sourcePath = std::string());
	MRMacroDebugRunResult continueDebug(const std::vector<std::size_t> &breakpointOffsets, std::size_t instructionBudget = 0);
	MRMacroDebugRunResult stepDebug(const std::vector<std::size_t> &breakpointOffsets, MRMacroDebugStepMode mode = mrdStepInto);
	void requestDebugPause() noexcept {
		if (debugState.pauseSignal != nullptr)
			debugState.pauseSignal->store(true, std::memory_order_release);
		else
			debugState.pauseRequested = true;
	}
	void setDebugPauseRequest(std::shared_ptr<std::atomic_bool> request) noexcept {
		debugState.pauseSignal = std::move(request);
	}
	MRMacroDebugWatchSnapshot evaluateDebugWatchExpression(const std::string &expression);
	bool mutateDebugValue(const MRMacroDebugValueMutation &mutation, std::vector<MRMacroDebugVariableSnapshot> &updatedVariables, std::string &errorMessage);
	bool hasPausedDebug() const noexcept {
		return debugState.paused;
	}
	void setAsyncDelayEnabled(bool enabled) noexcept {
		delayState.enabled = enabled;
	}
	bool hasPendingDelay() const noexcept {
		return delayState.pending;
	}
	bool resumePendingDelay();
	bool cancelPendingDelay();
	bool wasCancelled() const noexcept {
		return cancelledExecution;
	}
};

void mrvmSetProcessContext(int argc, char **argv);
std::vector<std::string> mrvmProcessArguments();
void mrvmSetStartupSettingsMode(bool enabled) noexcept;
bool mrvmIsStartupSettingsMode() noexcept;
void mrvmBeginConfiguredKeymapBatch() noexcept;
bool mrvmEndConfiguredKeymapBatch(std::string *errorMessage);
bool mrvmFlushPendingStartupKeymapBatch(std::string *errorMessage);

struct MRMacroJobResult {
	std::vector<std::string> logLines;
	std::vector<MRMacroExecUiCommandRequest> execUiCommandRequests;
	bool hadError;
	bool cancelled;

	MRMacroJobResult() noexcept : logLines(), hadError(false), cancelled(false) {
	}
};

MRMacroJobResult mrvmRunBytecodeBackground(const unsigned char *bytecode, std::size_t length, std::shared_ptr<std::atomic_bool> cancelFlag = nullptr);
MRMacroJobResult mrvmRunBytecodeBackgroundAt(const unsigned char *bytecode, std::size_t length, std::size_t entryOffset, const std::string &macroName, const std::string &closureId, MRMacroExecutionSessionId sessionId = 0, std::shared_ptr<std::atomic_bool> cancelFlag = nullptr);
MRMacroDebugRunResult mrvmRunBytecodeDebugAt(const unsigned char *bytecode, std::size_t length, std::size_t entryOffset, const std::string &macroName, const std::vector<std::size_t> &breakpointOffsets);
MRMacroDebugRunResult mrvmStartDebugSessionAt(const unsigned char *bytecode, std::size_t length, std::size_t entryOffset, const std::string &macroName, const MRMacroExecutionOwner &owner, const std::vector<std::size_t> &breakpointOffsets, MRMacroExecutionSession *sessionOut = nullptr, bool firstRun = false,
	                                             const std::string &macroKey = std::string(), const std::string &sourcePath = std::string(), const std::string &parameterString = std::string(), MRMacroExecutionRoute route = MRMacroExecutionRoute::Debug,
	                                             const MRMacroStagedExecutionInput *stagedInput = nullptr, const MacroCommitConflictSnapshot *conflictSnapshot = nullptr, bool automaticContinueFromEntry = false,
	                                             bool hasTemporaryBreakpoint = false, std::size_t temporaryBreakpointOffset = 0);
MRMacroDebugRunResult mrvmStartDebugMacroByName(const std::string &macroKey, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut = nullptr, std::string *errorMessage = nullptr, bool stopAtEntry = false, int temporaryStopLine = 0);
MRMacroDebugRunResult mrvmStartDebugMacroBySpec(const std::string &spec, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut = nullptr, std::string *errorMessage = nullptr);
bool mrvmMacroSpecHasEnabledDebugBreakpoint(const std::string &spec, std::string *sourcePath = nullptr, std::string *macroKeyOut = nullptr);
bool mrvmPrepareDebugMacroSourceMap(const std::string &macroKey, const std::string &sourcePath, std::string *errorMessage = nullptr);
MRMacroDebugRunResult mrvmContinueDebugSession(MRMacroExecutionSessionId sessionId, const std::vector<std::size_t> &breakpointOffsets);
MRMacroDebugRunResult mrvmContinueDebugMacroByName(MRMacroExecutionSessionId sessionId, const std::string &macroKey, std::string *errorMessage = nullptr);
MRMacroDebugRunResult mrvmStepDebugSession(MRMacroExecutionSessionId sessionId, const std::vector<std::size_t> &breakpointOffsets, MRMacroDebugStepMode mode = mrdStepInto);
MRMacroDebugRunResult mrvmStepDebugMacroByName(MRMacroExecutionSessionId sessionId, const std::string &macroKey, std::string *errorMessage = nullptr);
MRMacroDebugRunResult mrvmStepOverDebugMacroByName(MRMacroExecutionSessionId sessionId, const std::string &macroKey, std::string *errorMessage = nullptr);
MRMacroDebugRunResult mrvmStepOutDebugMacroByName(MRMacroExecutionSessionId sessionId, const std::string &macroKey, std::string *errorMessage = nullptr);
bool mrvmDebugSourceLineForInstruction(const std::string &macroKey, std::size_t bytecodeOffset, int *lineOut = nullptr, std::size_t *sourceStartOut = nullptr, std::size_t *sourceEndOut = nullptr);
bool mrvmCloseDebugSession(MRMacroExecutionSessionId sessionId, bool eraseDebuggerRuntimeOnDeferredClose = false);
bool mrvmScheduleDebugMacroContinue(MRMacroExecutionSessionId sessionId, const std::string &macroKey, std::string *errorMessage = nullptr);
bool mrvmRequestDebugPause(MRMacroExecutionSessionId sessionId, std::string *errorMessage = nullptr);
bool mrvmPumpDebugSession(MRMacroExecutionSessionId sessionId, const std::string &macroKey, MRMacroDebugRunResult &result, std::string *errorMessage = nullptr);
bool mrvmToggleDebugLineBreakpoint(const std::string &macroKey, int line, bool *enabledOut = nullptr, std::string *errorMessage = nullptr);
bool mrvmToggleDebugLineBreakpointEnabled(const std::string &macroKey, int line, bool *enabledOut = nullptr, std::string *errorMessage = nullptr);
bool mrvmWriteDebugLineBreakpoint(const std::string &macroKey, int line, bool enabled, std::string *errorMessage = nullptr, const std::string &conditionText = std::string());
bool mrvmDebugLineBreakpointsForMacro(const std::string &macroKey, std::vector<MRMacroDebuggerBreakpoint> &breakpoints);
bool mrvmToggleDebugLineBreakpointsEnabledForMacroFile(const std::string &macroKey, bool *enabledOut = nullptr, std::string *errorMessage = nullptr);
bool mrvmEraseDebugLineBreakpointsForMacroFile(const std::string &macroKey, std::string *errorMessage = nullptr);
bool mrvmEraseDebugRuntimeForMacroFile(const std::string &macroKey, std::string *errorMessage = nullptr);
bool mrvmEraseDebugRuntimeForMacro(const std::string &macroKey, std::string *errorMessage = nullptr);
bool mrvmWriteDebugWatch(const std::string &macroKey, const std::string &expression, bool enabled = true, std::string *errorMessage = nullptr);
bool mrvmValidateDebugWatchExpression(const std::string &expression, std::string *errorMessage = nullptr);
bool mrvmEraseDebugWatch(const std::string &macroKey, const std::string &expression, std::string *errorMessage = nullptr);
bool mrvmDebugWatchSnapshots(MRMacroExecutionSessionId sessionId, const std::string &macroKey, std::vector<MRMacroDebugWatchSnapshot> &snapshots);
bool mrvmEvaluateDebugExpression(MRMacroExecutionSessionId sessionId, const std::string &expression, MRMacroDebugWatchSnapshot &snapshot, std::string *errorMessage = nullptr);
bool mrvmWriteDebugScalarVariable(MRMacroExecutionSessionId sessionId, const MRMacroDebugVariableSnapshot &variable, const std::string &valueText, std::vector<MRMacroDebugVariableSnapshot> &updatedVariables, std::string *errorMessage = nullptr);
bool mrvmMutateDebugValue(MRMacroExecutionSessionId sessionId, const MRMacroDebugValueMutation &mutation, std::vector<MRMacroDebugVariableSnapshot> &updatedVariables, std::string *errorMessage = nullptr);
bool mrvmStoreExecSessionClosureInt(const std::string &closureId, const std::string &lvalue, int value);
bool mrvmApplyExecUiCommandRequest(const MRMacroExecUiCommandRequest &request);

MRMacroExecutionSessionId mrvmNextMacroExecutionSessionId();
void mrvmStoreActiveMacroExecutionSession(const MRMacroExecutionSession &session);
std::vector<MRMacroExecutionSession> mrvmActiveMacroExecutionSessions();
std::vector<MRMacroExecutionSession> mrvmActiveMacroExecutionSessionsForOwner(const MRMacroExecutionOwner &owner);
bool mrvmMarkMacroExecutionSessionCancellationRequestedForTask(std::uint64_t taskId);
bool mrvmTakeActiveMacroExecutionSessionForTask(std::uint64_t taskId, MRMacroExecutionSession &session);
void mrvmRecordMacroExecutionResult(MRMacroExecutionSession session, MRMacroExecutionState state, const std::string &message);
std::vector<MRMacroExecutionResult> mrvmRecentMacroExecutionResults();
void mrvmStorePendingForegroundMacroExecutionSession(const MRMacroExecutionSession &session);
bool mrvmReadPendingForegroundMacroExecutionSession(MRMacroExecutionSessionId sessionId, MRMacroExecutionSession &session);
bool mrvmRemovePendingForegroundMacroExecutionSession(MRMacroExecutionSessionId sessionId);
std::vector<MRMacroExecutionSession> mrvmPendingForegroundMacroExecutionSessions();
MRMacroExecutionSessionListenerId mrvmNextMacroExecutionSessionListenerId();
void mrvmRegisterMacroExecutionSessionListener(MRMacroExecutionSessionListenerId listenerId);
void mrvmRemoveMacroExecutionSessionListener(MRMacroExecutionSessionListenerId listenerId);
void mrvmNoteMacroExecutionSessionStatusChanged();
std::uint64_t mrvmMacroExecutionSessionStatusGeneration();

MRRuntimeScheduledConsumerId mrvmNextRuntimeScheduledConsumerId();
void mrvmStoreRuntimeScheduledConsumer(const MRRuntimeScheduledConsumer &consumer);
bool mrvmReadRuntimeScheduledConsumer(MRRuntimeScheduledConsumerId consumerId, MRRuntimeScheduledConsumer &consumer);
bool mrvmUpdateRuntimeScheduledConsumerActiveSession(MRRuntimeScheduledConsumerId consumerId, MRMacroExecutionSessionId activeSessionId);
bool mrvmUpdateRuntimeScheduledConsumerNextDue(MRRuntimeScheduledConsumerId consumerId, std::uint64_t nextDueMs);
bool mrvmRemoveRuntimeScheduledConsumer(MRRuntimeScheduledConsumerId consumerId);
std::vector<MRRuntimeScheduledConsumerId> mrvmRuntimeScheduledConsumerIds();
bool mrvmReadRuntimeScheduledConsumerSchedule(MRRuntimeScheduledConsumerId consumerId, std::uint64_t &intervalMs, MRMacroExecutionSessionId &activeSessionId, std::uint64_t &nextDueMs);
std::vector<MRRuntimeScheduledConsumer> mrvmRuntimeScheduledConsumers();
void mrvmRecordRuntimeSchedulerEvent(const MRRuntimeSchedulerEvent &event);
std::vector<MRRuntimeSchedulerEvent> mrvmRecentRuntimeSchedulerEvents();
MRRuntimeSchedulerEventId mrvmNextRuntimeSchedulerEventId();

void mrvmStoreModelessWindowDefinition(const MRMacroModelessWindowDefinition &definition);
bool mrvmStoreModelessWindowDisplay(const std::string &windowId, int displayIndex, const std::string &text);
void mrvmStoreModelessWindowLiveGeometry(const std::string &windowId, int x, int y, int width, int height);
bool mrvmReadModelessCanvasScene(const std::string &windowId, const std::string &canvasId, MRMacroModelessCanvasScene &scene);
void mrvmStoreModelessWindowDesktopState(const std::string &windowId, const MRMacroModelessWindowDesktopState &state);
bool mrvmReadModelessWindowDesktopState(const std::string &windowId, MRMacroModelessWindowDesktopState &state);
void mrvmRemoveModelessWindowDefinition(const std::string &windowId);

enum MRMacroDeferredUiCommandType {
	mrducNone = 0,
	mrducCreateWindow,
	mrducDeleteWindow,
	mrducModifyWindow,
	mrducLinkWindow,
	mrducUnlinkWindow,
	mrducZoom,
	mrducRedraw,
	mrducNewScreen,
	mrducSwitchWindow,
	mrducSizeWindow,
	mrducMarqueeInfo,
	mrducMarqueeWarning,
	mrducMarqueeError,
	mrducMakeMessage,
	mrducBrain,
	mrducPutBox,
	mrducWrite,
	mrducClrLine,
	mrducGotoxy,
	mrducPutLineNum,
	mrducPutColNum,
	mrducScrollBoxUp,
	mrducScrollBoxDn,
	mrducClearScreen,
	mrducKillBox,
	mrducRegisterMenuItem,
	mrducRemoveMenuItem,
	mrducMessageBox,
	mrducDelay
};

struct MRMacroDeferredUiCommand {
	int type;
	int a1;
	int a2;
	int a3;
	int a4;
	int a5;
	int a6;
	int a7;
	int a8;
	std::string text;
	std::string text2;
	std::string text3;
	std::string text4;

	MRMacroDeferredUiCommand() noexcept : type(mrducNone), a1(0), a2(0), a3(0), a4(0), a5(0), a6(0), a7(0), a8(0), text(), text2(), text3(), text4() {
	}

	MRMacroDeferredUiCommand(int aType, int arg1 = 0, int arg2 = 0, int arg3 = 0, int arg4 = 0) noexcept : type(aType), a1(arg1), a2(arg2), a3(arg3), a4(arg4), a5(0), a6(0), a7(0), a8(0), text(), text2(), text3(), text4() {
	}

	MRMacroDeferredUiCommand(int aType, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8, const std::string &aText = std::string()) : type(aType), a1(arg1), a2(arg2), a3(arg3), a4(arg4), a5(arg5), a6(arg6), a7(arg7), a8(arg8), text(aText), text2(), text3(), text4() {
	}
};

struct MacroCommitConflictSnapshot {
	std::size_t cursorOffset;
	std::size_t selectionStart;
	std::size_t selectionEnd;
	int blockMode;
	bool blockMarkingOn;
	std::size_t blockAnchor;
	std::size_t blockEnd;
	bool insertMode;
	int indentLevel;
	std::string fileName;
	bool fileChanged;
	std::vector<std::string> globalOrder;
	std::map<std::string, int> globalInts;
	std::map<std::string, std::string> globalStrings;
	bool lastSearchValid;
	std::size_t lastSearchStart;
	std::size_t lastSearchEnd;
	std::size_t lastSearchCursor;
	bool ignoreCase;
	bool tabExpand;
	std::vector<std::size_t> markStack;
	int bufferId;
	int linkStatus;
	int windowCount;
	bool windowGeometryValid;
	int windowX1;
	int windowY1;
	int windowX2;
	int windowY2;

	MacroCommitConflictSnapshot() noexcept
	    : cursorOffset(0), selectionStart(0), selectionEnd(0), blockMode(0), blockMarkingOn(false), blockAnchor(0), blockEnd(0), insertMode(true), indentLevel(1), fileName(), fileChanged(false), globalOrder(),
	      globalInts(), globalStrings(), lastSearchValid(false), lastSearchStart(0), lastSearchEnd(0), lastSearchCursor(0), ignoreCase(false), tabExpand(true), markStack(), bufferId(0), linkStatus(0), windowCount(0),
	      windowGeometryValid(false), windowX1(0), windowY1(0), windowX2(0), windowY2(0) {
	}
};

struct MRMacroStagedExecutionInput {
	mr::editor::TextDocument document;
	std::size_t baseVersion;
	std::size_t cursorOffset;
	std::size_t selectionStart;
	std::size_t selectionEnd;
	int blockMode;
	bool blockMarkingOn;
	std::size_t blockAnchor;
	std::size_t blockEnd;
	bool firstSave;
	bool eofInMemory;
	int bufferId;
	bool temporaryFile;
	std::string temporaryFileName;
	int currentWindow;
	int linkStatus;
	int windowCount;
	bool windowGeometryValid;
	int windowX1;
	int windowY1;
	int windowX2;
	int windowY2;
	std::vector<std::string> globalOrder;
	std::map<std::string, int> globalInts;
	std::map<std::string, std::string> globalStrings;
	std::vector<std::string> macroOrder;
	std::map<std::string, std::string> macroDisplayNames;
	bool lastSearchValid;
	std::size_t lastSearchStart;
	std::size_t lastSearchEnd;
	std::size_t lastSearchCursor;
	bool ignoreCase;
	bool tabExpand;
	std::vector<std::size_t> markStack;
	bool insertMode;
	int indentLevel;
	int pageLines;
	std::string fileName;
	bool fileChanged;
	int screenWidth;
	int screenHeight;
	int screenCursorX;
	int screenCursorY;

	MRMacroStagedExecutionInput() noexcept : document(), baseVersion(0), cursorOffset(0), selectionStart(0), selectionEnd(0), blockMode(0), blockMarkingOn(false), blockAnchor(0), blockEnd(0), firstSave(false), eofInMemory(false), bufferId(0), temporaryFile(false), temporaryFileName(), currentWindow(0), linkStatus(0), windowCount(0), windowGeometryValid(false), windowX1(0), windowY1(0), windowX2(0), windowY2(0), globalOrder(), globalInts(), globalStrings(), macroOrder(), macroDisplayNames(), lastSearchValid(false), lastSearchStart(0), lastSearchEnd(0), lastSearchCursor(0), ignoreCase(false), tabExpand(true), markStack(), insertMode(true), indentLevel(1), pageLines(20), fileName(), fileChanged(false), screenWidth(0), screenHeight(0), screenCursorX(1), screenCursorY(1) {
	}
};

struct MRMacroStagedJobResult {
	std::vector<std::string> logLines;
	bool hadError;
	bool cancelled;
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
	std::vector<std::string> macroOrder;
	std::map<std::string, std::string> macroDisplayNames;
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

	MRMacroStagedJobResult() noexcept : logLines(), hadError(false), cancelled(false), conflictSnapshot(), transaction(), cursorOffset(0), selectionStart(0), selectionEnd(0), blockMode(0), blockMarkingOn(false), blockAnchor(0), blockEnd(0), globalOrder(), globalInts(), globalStrings(), macroOrder(), macroDisplayNames(), deferredUiCommands(), lastSearchValid(false), lastSearchStart(0), lastSearchEnd(0), lastSearchCursor(0), ignoreCase(false), tabExpand(true), markStack(), insertMode(true), indentLevel(1), fileName(), fileChanged(false) {
	}
};

enum MRMacroDebugWorkerAction : int {
	mrdWorkerContinue = 0,
	mrdWorkerStepInto,
	mrdWorkerStepOver,
	mrdWorkerStepOut
};

struct MRMacroDebugWorkerResult {
	MRMacroExecutionRoute route;
	MRMacroDebugRunResult debugResult;
	MRMacroStagedJobResult stagedResult;
	bool hasStagedResult;
	bool accepted;
	std::string errorMessage;

	MRMacroDebugWorkerResult() : route(MRMacroExecutionRoute::Unknown), debugResult(), stagedResult(), hasStagedResult(false), accepted(false), errorMessage() {
	}
};

MRMacroStagedJobResult mrvmRunBytecodeStagedBackground(const unsigned char *bytecode, std::size_t length, const MRMacroStagedExecutionInput &input, MRMacroExecutionSessionId sessionId = 0, std::shared_ptr<std::atomic_bool> cancelFlag = nullptr);
MRMacroExecutionRoute mrvmDebugSessionRoute(MRMacroExecutionSessionId sessionId);
bool mrvmDebugSessionWorkerTaskContext(MRMacroExecutionSessionId sessionId, MRMacroExecutionRoute &route, int &bufferId, std::size_t &baseVersion);
bool mrvmAssignDebugSessionWorkerTask(MRMacroExecutionSessionId sessionId, std::uint64_t taskId);
bool mrvmAcceptDebugSessionWorkerTaskResult(MRMacroExecutionSessionId sessionId, std::uint64_t taskId);
MRMacroDebugWorkerResult mrvmRunDebugSessionWorkerAction(MRMacroExecutionSessionId sessionId, const std::string &macroKey, MRMacroDebugWorkerAction action, std::size_t instructionBudget = 8192,
	                                                     std::shared_ptr<std::atomic_bool> workerCancelFlag = nullptr);
bool mrvmFinalizeStagedDebugSession(MRMacroExecutionSessionId sessionId, const MRMacroDebugRunResult &debugResult, bool accepted, const std::string &message);

std::vector<std::size_t> mrvmUiCopyWindowMarkStack(const void *windowKey);
void mrvmUiReplaceWindowMarkStack(const void *windowKey, const std::vector<std::size_t> &offsets);
bool mrvmUiCopyWindowLastSearch(const void *windowKey, const std::string &fileName, std::size_t &start, std::size_t &end, std::size_t &cursor);
void mrvmUiReplaceWindowLastSearch(const void *windowKey, const std::string &fileName, bool valid, std::size_t start, std::size_t end, std::size_t cursor);
void mrvmUiCopyGlobals(std::vector<std::string> &order, std::map<std::string, int> &ints, std::map<std::string, std::string> &strings);
void mrvmUiCopyLoadedMacros(std::vector<std::string> &order, std::map<std::string, std::string> &displayNames);
void mrvmUiReplaceGlobals(const std::vector<std::string> &order, const std::map<std::string, int> &ints, const std::map<std::string, std::string> &strings);
void mrvmUiCopyRuntimeOptions(bool &ignoreCase, bool &tabExpand);
void mrvmUiReplaceRuntimeOptions(bool ignoreCase, bool tabExpand);
void mrvmUiSyncLinkedWindowsFrom(MREditWindow *window);
int mrvmUiCurrentWindowIndex(const void *windowKey);
int mrvmUiWindowCount();
int mrvmUiLinkStatus(const void *windowKey);
bool mrvmUiWindowGeometry(const void *windowKey, int &x1, int &y1, int &x2, int &y2);
int mrvmUiScreenWidth();
int mrvmUiScreenHeight();
bool mrvmUiCursorPosition(int &x, int &y);
std::uint64_t mrvmUiScreenMutationEpoch() noexcept;
void mrvmUiInvalidateScreenBase() noexcept;
void mrvmUiTouchScreenMutationEpoch() noexcept;
void mrvmUiBeginMacroScreenBatch() noexcept;
void mrvmUiEndMacroScreenBatch() noexcept;
std::uint64_t mrvmUiMacroScreenFlushCount() noexcept;
void mrvmUiResetMacroScreenFlushCount() noexcept;
bool mrvmUiSetCurrentWindow(const void *windowKey);
bool mrvmUiCreateWindow();
bool mrvmUiDeleteCurrentWindow();
bool mrvmUiEraseCurrentWindow();
bool mrvmUiModifyCurrentWindow();
bool mrvmUiSwitchWindow(int index);
bool mrvmUiSizeCurrentWindow(int x1, int y1, int x2, int y2);
bool mrvmUiPushMarker();
bool mrvmUiGetMarker();
bool mrvmUiSetRandomAccessMark(int index);
bool mrvmUiGetRandomAccessMark(int index);
bool mrvmUiBlockBeginLine();
bool mrvmUiBlockBeginColumn();
bool mrvmUiBlockBeginStream();
bool mrvmUiBlockEndMarking();
bool mrvmUiBlockTurnMarkingOff();
bool mrvmUiBlockToggleVisibility();
bool mrvmUiCopyBlock();
bool mrvmUiMoveBlock();
bool mrvmUiDeleteBlock();
bool mrvmUiExtractCurrentBlockText(std::string &out);
bool mrvmUiIndentBlock();
bool mrvmUiUndentBlock();
bool mrvmUiMoveCursorToNextPageBreak();
bool mrvmUiMoveCursorToPrevPageBreak();
bool mrvmUiCursorTabRight();
bool mrvmUiCursorTabLeft();
bool mrvmUiCursorIndent();
bool mrvmUiCursorUndent();
bool mrvmUiWindowCopyBlock(int sourceWindowIndex);
bool mrvmUiWindowMoveBlock(int sourceWindowIndex);
bool mrvmUiWindowCopyBlockFromWindow(const void *sourceWindowKey);
bool mrvmUiWindowMoveBlockFromWindow(const void *sourceWindowKey);
bool mrvmUiWindowCopyBlockBetween(const void *sourceWindowKey, const void *targetWindowKey);
bool mrvmUiWindowMoveBlockBetween(const void *sourceWindowKey, const void *targetWindowKey);
bool mrvmUiSaveBlockToFile(const std::string &pathSpec);

bool mrvmUiLinkCurrentWindow();
bool mrvmUiUnlinkCurrentWindow();
bool mrvmUiZoomCurrentWindow();
bool mrvmUiRedrawCurrentWindow();
bool mrvmUiNewScreen();
bool mrvmUiMarquee(int kind, const std::string &text);
bool mrvmUiBrain(bool enabled);
bool mrvmUiPutBox(int x1, int y1, int x2, int y2, int bgColor, int fgColor, const std::string &title, int shadow);
bool mrvmUiWrite(const std::string &text, int x, int y, int bgColor, int fgColor);
bool mrvmUiClrLine(int col = 0, int row = 0, int count = 0);
bool mrvmUiGotoxy(int x, int y);
bool mrvmUiPutLineNum(int line);
bool mrvmUiPutColNum(int col);
bool mrvmUiScrollBoxUp(int x1, int y1, int x2, int y2, int attr);
bool mrvmUiScrollBoxDn(int x1, int y1, int x2, int y2, int attr);
bool mrvmUiClearScreen(int attr = 0x07);
bool mrvmUiKillBox();
bool mrvmUiRegisterMenuItem(const std::string &menuTitle, const std::string &itemTitle, const std::string &macroSpec, const std::string &ownerSpec, std::string *errorMessage = nullptr);
bool mrvmUiRemoveMenuItem(const std::string &menuTitle, const std::string &itemTitle, const std::string &ownerSpec, std::string *errorMessage = nullptr);
bool mrvmUiRemoveRuntimeMenusOwnedByMacroSpec(const std::string &ownerSpec, std::string *errorMessage = nullptr);
bool mrvmUiRemoveRuntimeMenusOwnedByFile(const std::string &fileSpec, std::string *errorMessage = nullptr);
bool mrvmUiSetRuntimeMenuKeyLabelForMacroSpec(const std::string &macroSpec, const std::string &keyLabel, std::string *errorMessage = nullptr);
bool mrvmUiClearRuntimeMenuKeyLabels(std::string *errorMessage = nullptr);
std::string mrvmUiMenuKeyLabelForMacroSpec(const std::string &macroSpec);
bool mrvmUiRefreshRuntimeMenus(std::string *errorMessage = nullptr);
bool mrvmUiMessageBox(const std::string &text);
bool mrvmUiRenderFacadeRenderDeferredCommand(const MRMacroDeferredUiCommand &command);
bool mrvmUiRenderDeferredCommand(const MRMacroDeferredUiCommand &command);
bool mrvmLoadMacroFile(const std::string &spec, std::string *errorMessage = nullptr);
bool mrvmRunMacroSpec(const std::string &spec, std::string *errorMessage = nullptr, std::vector<std::string> *logLines = nullptr);
void mrvmBootstrapBoundMacroIndex(const std::string &directoryPath, std::size_t *fileCount = nullptr, std::size_t *bindingCount = nullptr);
bool mrvmWarmLoadNextIndexedMacroFile(std::string *loadedFilePath = nullptr, std::string *failedFilePath = nullptr, std::string *errorMessage = nullptr);
bool mrvmHasPendingIndexedMacroWarmup();
bool mrvmRunAssignedMacroForKey(unsigned short keyCode, unsigned short controlKeyState, std::string &executedMacroName, std::vector<std::string> *logLines = nullptr);

#endif
