#ifndef MRVM_HPP
#define MRVM_HPP

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

#include "MRTextDocument.hpp"

class TMREditWindow;

class VirtualMachine {
  public:
	struct Value {
		int type;
		int i;
		double r;
		std::string s;
		unsigned char c;

		Value();
	};

  private:
	std::vector<Value> stack;
	std::map<std::string, Value> variables;
	bool verboseLogging;
	bool logTruncated;
	bool asyncDelayPending_;
	bool asyncDelayReady_;
	std::vector<unsigned char> asyncBytecode_;
	std::size_t asyncLength_;
	std::size_t asyncIp_;
	std::vector<std::size_t> asyncCallStack_;
	int asyncReturnInt_;
	std::string asyncReturnStr_;
	int asyncErrorLevel_;
	std::string asyncSavedParameterString_;
	bool asyncMacroFramePushed_;
	std::shared_ptr<std::atomic_bool> asyncDelayReadyFlag_;
	std::shared_ptr<std::atomic_bool> asyncDelayCancelledFlag_;
	std::uint64_t asyncDelayTaskId_;
	std::uint64_t asyncDelayGeneration_;
	int asyncDelayMillis_;

	void appendLogLine(const std::string &line, bool important = false);
	void clearAsyncDelayState() noexcept;
	static int normalizeDelayMillis(int millis) noexcept;

	void push(const Value &value);
	Value pop();

 public:
	std::vector<std::string> log;
	bool cancelledExecution;

	VirtualMachine();
	void setVerboseLogging(bool enable) noexcept {
		verboseLogging = enable;
	}
	void execute(const unsigned char *bytecode, size_t length);
	void executeAt(const unsigned char *bytecode, size_t length, size_t entryOffset,
	               const std::string &parameterString, const std::string &macroName,
	               bool resetState, bool firstRun);
	bool hasPendingDelay() const noexcept {
		return asyncDelayPending_;
	}
	bool resumePendingDelay();
	bool cancelPendingDelay();
	bool wasCancelled() const noexcept {
		return cancelledExecution;
	}
};

	enum MRMacroExecutionFlags {
		mrefBackgroundSafe = 1u << 0,
		mrefStagedWrite = 1u << 1,
		mrefUiAffinity = 1u << 2,
		mrefExternalIo = 1u << 3
	};

	struct MRMacroExecutionProfile {
		unsigned flags;
		std::size_t opcodeCount;
		std::size_t intrinsicCount;
		std::size_t procCount;
		std::size_t procVarCount;
		std::size_t tvCallCount;
		std::vector<std::string> stagedWriteSymbols;
		std::vector<std::string> uiAffinitySymbols;
		std::vector<std::string> externalIoSymbols;

		MRMacroExecutionProfile() noexcept
		    : flags(0), opcodeCount(0), intrinsicCount(0), procCount(0), procVarCount(0),
		      tvCallCount(0), stagedWriteSymbols(), uiAffinitySymbols(), externalIoSymbols() {
		}

		bool has(unsigned mask) const noexcept {
			return (flags & mask) != 0;
		}
	};

void mrvmSetProcessContext(int argc, char **argv);
void mrvmSetStartupSettingsMode(bool enabled) noexcept;
bool mrvmIsStartupSettingsMode() noexcept;
MRMacroExecutionProfile mrvmAnalyzeBytecode(const unsigned char *bytecode, std::size_t length);
std::string mrvmDescribeExecutionProfile(const MRMacroExecutionProfile &profile);
bool mrvmCanRunInBackground(const MRMacroExecutionProfile &profile) noexcept;
bool mrvmCanRunStagedInBackground(const MRMacroExecutionProfile &profile) noexcept;
std::vector<std::string> mrvmUnsupportedStagedSymbols(const MRMacroExecutionProfile &profile);

struct MRMacroJobResult {
	std::vector<std::string> logLines;
	bool hadError;
	bool cancelled;

	MRMacroJobResult() noexcept : logLines(), hadError(false), cancelled(false) {
	}
};

MRMacroJobResult mrvmRunBytecodeBackground(const unsigned char *bytecode, std::size_t length,
                                           std::stop_token stopToken = std::stop_token(),
                                           std::shared_ptr<std::atomic_bool> cancelFlag = nullptr);

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
	mrducSizeWindow
};

struct MRMacroDeferredUiCommand {
	int type;
	int a1;
	int a2;
	int a3;
	int a4;

	MRMacroDeferredUiCommand() noexcept : type(mrducNone), a1(0), a2(0), a3(0), a4(0) {
	}

	MRMacroDeferredUiCommand(int aType, int arg1 = 0, int arg2 = 0, int arg3 = 0,
	                         int arg4 = 0) noexcept
	    : type(aType), a1(arg1), a2(arg2), a3(arg3), a4(arg4) {
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

		MRMacroStagedExecutionInput() noexcept
	    : document(), baseVersion(0), cursorOffset(0), selectionStart(0), selectionEnd(0),
	      blockMode(0), blockMarkingOn(false), blockAnchor(0), blockEnd(0), firstSave(false),
	      eofInMemory(false), bufferId(0), temporaryFile(false), temporaryFileName(),
	      currentWindow(0), linkStatus(0), windowCount(0), windowGeometryValid(false),
	      windowX1(0), windowY1(0), windowX2(0), windowY2(0), globalOrder(),
	      globalInts(), globalStrings(), macroOrder(), macroDisplayNames(),
	      lastSearchValid(false), lastSearchStart(0), lastSearchEnd(0), lastSearchCursor(0),
	      ignoreCase(false), tabExpand(true), markStack(), insertMode(true), indentLevel(1),
	      pageLines(20), fileName(), fileChanged(false) {
	}
};

struct MRMacroStagedJobResult {
	std::vector<std::string> logLines;
	bool hadError;
	bool cancelled;
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

	MRMacroStagedJobResult() noexcept
	    : logLines(), hadError(false), cancelled(false), transaction(), cursorOffset(0), selectionStart(0),
	      selectionEnd(0), blockMode(0), blockMarkingOn(false), blockAnchor(0), blockEnd(0),
	      globalOrder(), globalInts(), globalStrings(), macroOrder(), macroDisplayNames(),
	      deferredUiCommands(),
	      lastSearchValid(false), lastSearchStart(0), lastSearchEnd(0), lastSearchCursor(0),
	      ignoreCase(false), tabExpand(true), markStack(), insertMode(true), indentLevel(1),
	      fileName(), fileChanged(false) {
	}
};

MRMacroStagedJobResult mrvmRunBytecodeStagedBackground(const unsigned char *bytecode,
                                                       std::size_t length,
                                                       const MRMacroStagedExecutionInput &input,
                                                       std::stop_token stopToken = std::stop_token(),
                                                       std::shared_ptr<std::atomic_bool> cancelFlag = nullptr);

std::vector<std::size_t> mrvmUiCopyWindowMarkStack(const void *windowKey);
void mrvmUiReplaceWindowMarkStack(const void *windowKey, const std::vector<std::size_t> &offsets);
bool mrvmUiCopyWindowLastSearch(const void *windowKey, const std::string &fileName, std::size_t &start,
                                std::size_t &end, std::size_t &cursor);
void mrvmUiReplaceWindowLastSearch(const void *windowKey, const std::string &fileName, bool valid,
                                   std::size_t start, std::size_t end, std::size_t cursor);
void mrvmUiCopyGlobals(std::vector<std::string> &order, std::map<std::string, int> &ints,
                       std::map<std::string, std::string> &strings);
void mrvmUiCopyLoadedMacros(std::vector<std::string> &order,
                            std::map<std::string, std::string> &displayNames);
void mrvmUiReplaceGlobals(const std::vector<std::string> &order,
                          const std::map<std::string, int> &ints,
                          const std::map<std::string, std::string> &strings);
void mrvmUiCopyRuntimeOptions(bool &ignoreCase, bool &tabExpand);
void mrvmUiReplaceRuntimeOptions(bool ignoreCase, bool tabExpand);
void mrvmUiSyncLinkedWindowsFrom(TMREditWindow *window);
int mrvmUiCurrentWindowIndex(const void *windowKey);
int mrvmUiWindowCount();
int mrvmUiLinkStatus(const void *windowKey);
bool mrvmUiWindowGeometry(const void *windowKey, int &x1, int &y1, int &x2, int &y2);
bool mrvmUiSetCurrentWindow(const void *windowKey);
bool mrvmUiCreateWindow();
bool mrvmUiDeleteCurrentWindow();
bool mrvmUiModifyCurrentWindow();
bool mrvmUiSwitchWindow(int index);
bool mrvmUiSizeCurrentWindow(int x1, int y1, int x2, int y2);

bool mrvmUiLinkCurrentWindow();
bool mrvmUiUnlinkCurrentWindow();
bool mrvmUiZoomCurrentWindow();
bool mrvmUiRedrawCurrentWindow();
bool mrvmUiNewScreen();
bool mrvmUiDispatchMacro(const std::string &name, const std::string &args);
bool mrvmLoadMacroFile(const std::string &spec, std::string *errorMessage = nullptr);
void mrvmBootstrapBoundMacroIndex(const std::string &directoryPath, std::size_t *fileCount = nullptr,
                                  std::size_t *bindingCount = nullptr);
bool mrvmWarmLoadNextIndexedMacroFile(std::string *loadedFilePath = nullptr,
                                      std::string *failedFilePath = nullptr,
                                      std::string *errorMessage = nullptr);
bool mrvmHasPendingIndexedMacroWarmup();

bool mrvmRunAssignedMacroForKey(unsigned short keyCode, unsigned short controlKeyState,
                                std::string &executedMacroName,
                                std::vector<std::string> *logLines = nullptr);

#endif
