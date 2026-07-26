#ifndef MRVM_RUNTIME_STATE_HPP
#define MRVM_RUNTIME_STATE_HPP

#include "../MRVM.hpp"
#include "MRVMKeymapRuntime.hpp"
#include "MRVMRuntimeGlobals.hpp"
#include "MRVMRuntimeKv.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

using GlobalEntry = MRVMRuntimeGlobalEntry;

struct MacroStackFrame {
	std::string macroName;
	bool firstRun;

	MacroStackFrame() : firstRun(false) {
	}

	MacroStackFrame(const std::string &aName, bool aFirstRun) : macroName(aName), firstRun(aFirstRun) {
	}
};

struct MacroKeyCodePair {
	int key1 = 0;
	int key2 = 0;
};

struct MacroFunctionLabelFrame {
	std::array<std::string, 49> editLabels;
	std::array<std::string, 49> shellLabels;
};

struct MRVMForkedProcess {
	int pid;
	std::string sourcePath;
	std::string pdfPath;
	std::vector<int> ownerBufferIds;

	MRVMForkedProcess() : pid(0), sourcePath(), pdfPath(), ownerBufferIds() {
	}
};

enum MacroAssignableCommandId {
	macdBackSpace = 0x7001,
	macdBlockBegin = 0x7002,
	macdBlockEnd = 0x7003,
	macdBlockOff = 0x7004,
	macdColBlockBegin = 0x7005,
	macdCopyBlock = 0x7006,
	macdCr = 0x7007,
	macdDeleteBlock = 0x7008,
	macdDelChar = 0x7009,
	macdDelLine = 0x700A,
	macdDown = 0x700B,
	macdEof = 0x700C,
	macdEol = 0x700D,
	macdFirstWord = 0x700E,
	macdGotoMark = 0x700F,
	macdHome = 0x7010,
	macdIndent = 0x7011,
	macdKeyRecord = 0x7012,
	macdLastPageBreak = 0x7013,
	macdLeft = 0x7014,
	macdMarkPos = 0x7015,
	macdMoveBlock = 0x7016,
	macdNextPageBreak = 0x7017,
	macdPageDown = 0x7018,
	macdPageUp = 0x7019,
	macdRight = 0x701A,
	macdSaveFile = 0x701B,
	macdStrBlockBegin = 0x701C,
	macdTabLeft = 0x701D,
	macdTabRight = 0x701E,
	macdTof = 0x701F,
	macdUndent = 0x7020,
	macdUndo = 0x7021,
	macdUp = 0x7022,
	macdWordLeft = 0x7023,
	macdWordRight = 0x7024
};

struct RuntimeEnvironment {
	MRVMRuntimeKv runtimeKv;
	std::string parameterString;
	int returnInt;
	std::string returnStr;
	int errorLevel;
	std::vector<MacroStackFrame> macroStack;
	std::vector<std::string> fileMatches;
	std::size_t fileMatchIndex;
	std::string lastFileName;
	std::map<const void *, std::vector<unsigned int>> markStacks;
	std::map<const void *, std::array<std::optional<unsigned int>, 10>> randomAccessMarks;
	std::string startupCommand;
	std::vector<std::string> processArgs;
	std::string executablePath;
	std::string executableDir;
	std::string shellPath;
	std::string shellVersion;
	std::vector<MRVMForkedProcess> forkedProcesses;
	int docMode;
	int shadowChar;
	int refresh;
	int mouse;
	int logoScreen;
	int explosions;
	bool ignoreCase;
	int formatStat;
	int undoStat;
	int memAlloc;
	int insCursor;
	int ovrCursor;
	int ctrlHelp;
	int mouseHSense;
	int mouseVSense;
	int statusRow;
	int messageRow;
	int maxWindowRow;
	int minWindowRow;
	int nameLine;
	std::string defaultFormat;
	bool tabExpand;
	bool lastSearchValid;
	const void *lastSearchWindow;
	std::string lastSearchFileName;
	std::size_t lastSearchStart;
	std::size_t lastSearchEnd;
	std::size_t lastSearchCursor;
	int key1;
	int key2;
	std::deque<MacroKeyCodePair> pushedKeys;
	std::vector<MacroFunctionLabelFrame> functionLabelStack;
	std::vector<MRVMExplicitKeyBinding> explicitKeyBindings;
	std::map<const void *, int> windowLinkGroups;
	int nextWindowLinkGroupId;

	RuntimeEnvironment() : returnInt(0), errorLevel(0), fileMatchIndex(0), docMode(0), shadowChar(176), refresh(1), mouse(1), logoScreen(0), explosions(0), ignoreCase(false), formatStat(0), undoStat(1), memAlloc(0), insCursor(0), ovrCursor(3), ctrlHelp(0), mouseHSense(8), mouseVSense(8), statusRow(-1), messageRow(-1), maxWindowRow(-1), minWindowRow(-1), nameLine(0), tabExpand(true), lastSearchValid(false), lastSearchWindow(nullptr), lastSearchStart(0), lastSearchEnd(0), lastSearchCursor(0), key1(0), key2(0), nextWindowLinkGroupId(1) {
		functionLabelStack.emplace_back();
	}
};

struct BackgroundEditSession {
	mr::editor::TextDocument document;
	mr::editor::StagedEditTransaction transaction;
	std::size_t cursorOffset;
	std::size_t selectionStart;
	std::size_t selectionEnd;
	bool lastSearchValid;
	std::size_t lastSearchStart;
	std::size_t lastSearchEnd;
	std::size_t lastSearchCursor;
	bool ignoreCase;
	bool tabExpand;
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
	std::map<std::string, GlobalEntry> globals;
	std::vector<std::string> globalOrder;
	std::size_t globalEnumIndex;
	std::map<std::string, std::string> loadedMacroDisplayNames;
	std::vector<std::string> macroOrder;
	std::size_t macroEnumIndex;
	std::vector<MRMacroDeferredUiCommand> deferredUiCommands;
	bool insertMode;
	int indentLevel;
	int pageLines;
	std::string fileName;
	bool fileChanged;
	int screenWidth;
	int screenHeight;
	int screenCursorX;
	int screenCursorY;
	std::vector<unsigned int> markStack;
	std::array<std::optional<unsigned int>, 10> randomAccessMarks;

	BackgroundEditSession() : document(), cursorOffset(0), selectionStart(0), selectionEnd(0), lastSearchValid(false), lastSearchStart(0), lastSearchEnd(0), lastSearchCursor(0), ignoreCase(false), tabExpand(true), blockMode(0), blockMarkingOn(false), blockAnchor(0), blockEnd(0), firstSave(false), eofInMemory(false), bufferId(0), temporaryFile(false), currentWindow(0), linkStatus(0), windowCount(0), windowGeometryValid(false), windowX1(0), windowY1(0), windowX2(0), windowY2(0), globalEnumIndex(0), macroEnumIndex(0), insertMode(true), indentLevel(1), pageLines(20), fileChanged(false), screenWidth(0), screenHeight(0), screenCursorX(1), screenCursorY(1) {
	}

	bool hasSelection() const noexcept {
		return selectionEnd > selectionStart;
	}

	void clearSelection() noexcept {
		selectionStart = cursorOffset;
		selectionEnd = cursorOffset;
	}

	void clearLastSearch() noexcept {
		lastSearchValid = false;
		lastSearchStart = 0;
		lastSearchEnd = 0;
		lastSearchCursor = 0;
	}

	void clampState() noexcept {
		std::size_t length = document.length();
		cursorOffset = std::min(cursorOffset, length);
		selectionStart = std::min(selectionStart, length);
		selectionEnd = std::min(selectionEnd, length);
		if (selectionEnd < selectionStart) std::swap(selectionStart, selectionEnd);
	}
};

struct ExecutionState {
	std::string parameterString;
	int returnInt;
	std::string returnStr;
	int errorLevel;

	ExecutionState() : returnInt(0), errorLevel(0) {
	}
};

extern RuntimeEnvironment g_runtimeEnv;
extern std::recursive_mutex g_vmExecutionMutex;
extern thread_local BackgroundEditSession *g_backgroundEditSession;
extern thread_local std::shared_ptr<std::atomic_bool> g_backgroundMacroCancelFlag;
extern thread_local ExecutionState *g_executionState;
extern thread_local MRMacroExecutionSessionId g_executionSessionId;

#endif
