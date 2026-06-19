#include <unordered_map>
#include "../app/MRVersion.hpp"
#include "../app/utils/MRConstants.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#include "../app/utils/MRStringUtils.hpp"
#define Uses_MsgBox
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TButton
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TStaticText
#define Uses_TScrollBar
#define Uses_TListViewer
#define Uses_TStatusLine
#define Uses_TObject
#define Uses_TScreen
#define Uses_TDrawBuffer
#define Uses_TView
#include <tvision/tv.h>

#include "mrmac.h"
#include "MRVM.hpp"
#include "vm/MRVMDeferredUi.hpp"
#include "vm/MRVMHash.hpp"
#include "vm/MRVMSnippet.hpp"
#include "vm/MRVMValue.hpp"
#include "vm/MRVMScreen.hpp"
#include "vm/MRVMSettings.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <glob.h>
#include <limits>
#include <map>
#include <optional>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "../ui/MREditWindow.hpp"
#include "../app/MRCommandRouter.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../ui/MRMenuBar.hpp"
#include "../ui/MRStatusLine.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../dialogs/setup/MRSetupCommon.hpp"
#include "../dialogs/MRWindowList.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "../keymap/MRKeymapProfile.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../app/MRVersion.hpp"

MREditWindow *createEditorWindow(const char *title);
std::vector<MREditWindow *> allEditWindowsInZOrder();
bool moveToNextVirtualDesktop();
bool moveToPrevVirtualDesktop();
bool viewportRight();
bool viewportLeft();
void mrSaveWorkspace(const std::string &filename);
void mrLoadWorkspace(const std::string &filename);
void applyVirtualDesktopConfigurationChange(int count);

namespace {
using Value = VirtualMachine::Value;

struct GlobalEntry {
	int type;
	Value value;
};

static void setGlobalValue(const std::string &name, int type, const Value &value);
static void setGlobalValueFromStore(const std::string &name, int type, const Value &value, MRVMHashStore &localStore);
static bool readGlobalValue(const std::string &name, GlobalEntry &entry);

struct MacroRef {
	std::string fileKey;
	std::string displayName;
	std::size_t entryOffset;
	std::string assignedKeySpec;
	TKey assignedKey;
	int fromMode;
	bool hasAssignedKey;
	bool firstRunPending;
	bool transientAttr;
	bool dumpAttr;
	bool permAttr;

	MacroRef() : entryOffset(0), fromMode(MACRO_MODE_EDIT), hasAssignedKey(false), firstRunPending(true), transientAttr(false), dumpAttr(false), permAttr(false) {
	}
};

struct LoadedMacroFile {
	std::string fileKey;
	std::string displayName;
	std::string resolvedPath;
	std::vector<unsigned char> bytecode;
	std::vector<std::string> macroNames;
	MRMacroExecutionProfile profile;
};

struct IndexedBoundMacroEntry {
	TKey key;
	std::string filePath;

	IndexedBoundMacroEntry() {
	}
	IndexedBoundMacroEntry(const TKey &aKey, std::string aFilePath) : key(aKey), filePath(std::move(aFilePath)) {
	}
};

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

enum class ExplicitBindingKind : unsigned char {
	MacroSpec,
	Command
};

struct ExplicitKeyBinding {
	TKey key;
	int mode = MACRO_MODE_EDIT;
	ExplicitBindingKind kind = ExplicitBindingKind::MacroSpec;
	std::string macroSpec;
	int commandId = 0;
};

struct RuntimeEnvironment {
	std::map<std::string, GlobalEntry> globals;
	std::vector<std::string> globalOrder;
	std::size_t globalEnumIndex;
	MRVMHashStore globalHashStore;
	std::string parameterString;
	int returnInt;
	std::string returnStr;
	int errorLevel;
	std::map<std::string, LoadedMacroFile> loadedFiles;
	std::map<std::string, MacroRef> loadedMacros;
	std::vector<std::string> macroOrder;
	std::vector<IndexedBoundMacroEntry> indexedBoundMacros;
	std::vector<std::string> indexedBoundFiles;
	std::size_t indexedWarmupCursor;
	std::set<std::string> indexedWarmupAttemptedFiles;
	std::size_t macroEnumIndex;
	std::vector<MacroStackFrame> macroStack;
	std::vector<std::string> fileMatches;
	std::size_t fileMatchIndex;
	std::string lastFileName;
	std::map<const void *, std::vector<uint>> markStacks;
	std::map<const void *, std::array<std::optional<uint>, 10>> randomAccessMarks;
	std::string startupCommand;
	std::vector<std::string> processArgs;
	std::string executablePath;
	std::string executableDir;
	std::string shellPath;
	std::string shellVersion;
	int docMode;
	int shadowChar;
	int refresh;
	int mouse;
	int logoScreen;
	int explosions;
	bool ignoreCase;
	int printMargin;
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
	std::vector<MacroKeyCodePair> pushedKeys;
	std::vector<MacroFunctionLabelFrame> functionLabelStack;
	std::vector<ExplicitKeyBinding> explicitKeyBindings;
	std::map<const void *, int> windowLinkGroups;
	int nextWindowLinkGroupId;

	RuntimeEnvironment() : globalEnumIndex(0), returnInt(0), errorLevel(0), indexedWarmupCursor(0), macroEnumIndex(0), fileMatchIndex(0), docMode(0), shadowChar(176), refresh(1), mouse(1), logoScreen(0), explosions(0), ignoreCase(false), printMargin(0), formatStat(0), undoStat(1), memAlloc(0), insCursor(0), ovrCursor(3), ctrlHelp(0), mouseHSense(8), mouseVSense(8), statusRow(-1), messageRow(-1), maxWindowRow(-1), minWindowRow(-1), nameLine(0), tabExpand(true), lastSearchValid(false), lastSearchWindow(nullptr), lastSearchStart(0), lastSearchEnd(0), lastSearchCursor(0), key1(0), key2(0), nextWindowLinkGroupId(1) {
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
	std::vector<uint> markStack;
	std::array<std::optional<uint>, 10> randomAccessMarks;

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

static RuntimeEnvironment g_runtimeEnv;
static std::recursive_mutex g_vmExecutionMutex;
static thread_local BackgroundEditSession *g_backgroundEditSession = nullptr;
static thread_local const std::stop_token *g_backgroundMacroStopToken = nullptr;
static thread_local std::shared_ptr<std::atomic_bool> g_backgroundMacroCancelFlag;
static thread_local ExecutionState *g_executionState = nullptr;
static thread_local int g_keyReplayDepth = 0;
static std::string valueAsString(const Value &value);
static int valueAsInt(const Value &value);

static bool isStringLike(const Value &value);
static bool isNumeric(const Value &value);
static void enforceStringLength(const std::string &s);
static std::string commandFirstLine(const std::string &command);
static std::string detectExecutablePathFromProc();
static std::string normalizeDirPath(const std::string &path);
static std::string detectExecutableDir(const std::string &argv0);
static std::string detectShellPath();
static std::string detectShellVersion(const std::string &shellPath);
static int detectCpuCode();
static std::string getenvValue(const std::string &name);
static std::string getEnvironmentValue(const std::string &entryName);
static bool changeDirectoryPath(const std::string &path);
static bool deleteFilePath(const std::string &path);
static bool loadMacroFileIntoRegistry(const std::string &spec, std::string *loadedFileKey = nullptr);
static bool unloadMacroFromRegistry(const std::string &macroName);
static bool parseRunMacroSpec(const std::string &spec, std::string &filePart, std::string &macroPart, std::string &paramPart);
static bool ensureLoadedFileResident(const std::string &fileKey);
static bool evictTransientFileImage(const std::string &fileKey);
static bool currentBackgroundChildMacroAllowed(const LoadedMacroFile &file) noexcept;
std::string expandUserPath(const std::string &path);
static bool fileExistsPath(const std::string &path);
static std::vector<MREditWindow *> allEditWindows();
static void cleanupWindowLinkGroups();
static int windowLinkGroupOf(MREditWindow *win);
static bool isWindowLinked(MREditWindow *win);
static int currentLinkStatus();
static MREditWindow *selectLinkTargetWindow(MREditWindow *current);
static bool prepareWindowLink(MREditWindow *current, MREditWindow *target, MREditWindow *&source, MREditWindow *&dest);
bool linkCurrentEditWindow();
bool unlinkCurrentEditWindow();
static void syncLinkedWindowsFrom(MREditWindow *source);
bool redrawCurrentEditWindow();
bool redrawEntireScreen();
bool zoomCurrentEditWindow();
static int findFirstFileMatch(const std::string &pattern);
static int findNextFileMatch();
MREditWindow *activeMacroEditWindow();
MRFileEditor *currentEditor();
static BackgroundEditSession *currentBackgroundEditSession() noexcept;
static ExecutionState *currentExecutionState() noexcept;
static bool backgroundMacroCancelRequested() noexcept;
static bool backgroundReplaceRange(const mr::editor::Range &range, const std::string &text, std::size_t cursorPos);
static bool backgroundSetCursor(std::size_t target);
static std::size_t backgroundLineMoveOffset(std::size_t offset, int delta);
static std::size_t backgroundCharPtrOffset(std::size_t lineStart, int column);
static bool backgroundWordChar(char c) noexcept;
static std::size_t backgroundPrevWordOffset(std::size_t offset);
static std::size_t backgroundNextWordOffset(std::size_t offset);
static std::string snapshotEditorText(MRFileEditor *editor);
static std::size_t searchLimitForward(const std::string &text, std::size_t start, int numLines) {
	if (numLines <= 0) return text.size();
	std::size_t pos = start;
	int remaining = numLines;
	while (pos < text.size()) {
		if (text[pos] == '\n') {
			--remaining;
			if (remaining == 0) return pos;
		}
		++pos;
	}
	return text.size();
}

static std::size_t searchLimitBackward(const std::string &text, std::size_t start, int numLines) {
	if (numLines <= 0) return 0;
	std::size_t pos = std::min(start, text.size());
	int remaining = numLines;
	while (pos > 0) {
		--pos;
		if (text[pos] == '\n') {
			--remaining;
			if (remaining == 0) return pos + 1;
		}
	}
	return 0;
}

static bool searchEditorForward(MRFileEditor *editor, const std::string &needle, int numLines, bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd);
static bool searchEditorBackward(MRFileEditor *editor, const std::string &needle, int numLines, bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd);
static bool replaceLastSearch(MRFileEditor *editor, const std::string &replacement);
static bool replaceLastSearchBackground(const std::string &replacement);
static Value currentEditorCharValue();
static std::string currentEditorLineText(MRFileEditor *editor);
static std::string currentEditorWord(MRFileEditor *editor, const std::string &delimiters);
static bool isVirtualChar(char c);
static int nextResolvedTabDisplayColumn(const MREditSetupSettings &settings, int col);
static std::string expandTabsString(const std::string &value, bool toVirtuals);
static std::string tabsToSpacesString(const std::string &value);
static int expandedTabsAdjustedIndex(const std::string &value, int index);
static int currentEditorIndentLevel();
static bool setCurrentEditorIndentLevel(int level);
static bool currentEditorInsertMode();
static bool setCurrentEditorInsertMode(bool on);
static bool insertEditorText(MRFileEditor *editor, const std::string &text);
static bool replaceEditorLine(MRFileEditor *editor, const std::string &text);
static bool deleteEditorChars(MRFileEditor *editor, int count);
static bool deleteEditorLine(MRFileEditor *editor);
static int currentEditorColumn(MRFileEditor *editor);
static bool currentUiCursorPosition(int &x, int &y);
static int currentEditorLineNumber(MRFileEditor *editor);
static bool moveEditorLeft(MRFileEditor *editor);
static bool moveEditorRight(MRFileEditor *editor);
static bool moveEditorUp(MRFileEditor *editor);
static bool moveEditorDown(MRFileEditor *editor);
static bool moveEditorHome(MRFileEditor *editor);
static bool moveEditorEol(MRFileEditor *editor);
static bool moveEditorTof(MRFileEditor *editor);
static bool moveEditorEof(MRFileEditor *editor);
static bool moveEditorWordLeft(MRFileEditor *editor);
static bool moveEditorWordRight(MRFileEditor *editor);
static bool moveEditorFirstWord(MRFileEditor *editor);
static bool gotoEditorLine(MRFileEditor *editor, int lineNum);
static bool gotoEditorCol(MRFileEditor *editor, int colNum);
static bool currentEditorAtEof(MRFileEditor *editor);
static bool currentEditorAtEol(MRFileEditor *editor);
static int currentEditorRow(MRFileEditor *editor);
static int currentEditorPage(MRFileEditor *editor);
static int currentEditorPageLine(MRFileEditor *editor);
bool markEditorPosition(MREditWindow *win, MRFileEditor *editor);
bool gotoEditorMark(MREditWindow *win, MRFileEditor *editor);
static bool popEditorMark(MREditWindow *win);
bool setEditorRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index);
bool gotoEditorRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index);
static bool moveEditorPageUp(MRFileEditor *editor);
static bool moveEditorPageDown(MRFileEditor *editor);
bool moveEditorNextPageBreak(MRFileEditor *editor);
bool moveEditorLastPageBreak(MRFileEditor *editor);
static bool replaceEditorBuffer(MRFileEditor *editor, const std::string &text, std::size_t cursorPos);
static int lineIndexForPtr(MRFileEditor *editor, uint ptr);
static int countEditWindows();
static int currentEditWindowIndex();
static bool currentWindowGeometry(int &x1, int &y1, int &x2, int &y2);
static bool queueDeferredUiProcedureImpl(const std::string &name, const std::vector<Value> &args, int &errorCode);
static bool enqueueDeferredUiCommandImpl(const MRMacroDeferredUiCommand &command, int &errorCode);
bool createEditWindow();
bool switchEditWindow(int index);
bool sizeCurrentEditWindow(int x1, int y1, int x2, int y2);
bool deleteCurrentEditWindow();
bool eraseCurrentEditWindow();
bool modifyCurrentEditWindow();
static bool parseIndexedBindingHeaders(const std::string &source, std::vector<TKey> &keys);
static std::vector<std::string> listMrmacFilesInDirectory(const std::string &directoryPath);
static std::string normalizeKeySpecToken(const std::string &spec);
static bool parseAssignedKeySpec(const std::string &spec, TKey &outKey);
static bool dispatchSyntheticKeyToUi(const TKey &key, const char *text = nullptr, std::size_t textLength = 0);
static bool replayKeyInputSequence(const std::string &sequence);
static int currentUiMacroMode();
static bool macroAllowsUiMode(const MacroRef &macroRef, int mode) noexcept;
static const char *keymapActionIdForMacroCommand(const std::string &name) noexcept;
static bool executeLoadedMacro(std::map<std::string, MacroRef>::iterator macroIt, const std::string &macroKey, const std::string &paramPart, std::vector<std::string> *logSink);
static bool executeLoadedMacroWithConfiguredKeymapBatch(std::map<std::string, MacroRef>::iterator macroIt, const std::string &macroKey, const std::string &paramPart, std::vector<std::string> *logSink);
static bool tryLoadIndexedMacroForKey(const TKey &pressed);
static bool currentExecutingMacroSpecImpl(std::string &macroSpec);
static bool composeLoadedMacroSpec(const MacroRef &macroRef, std::string &macroSpec);
static std::string menuLabelFromBindingKey(const TKey &key);
static std::string normalizeMenuKeySpec(std::string keySpec);
static bool macroSpecTargetsLoadedMacro(const std::string &spec, const std::string &targetFileKey, const std::string &targetMacroKey);

static std::string upperKey(const std::string &value) {
	std::string out = value;
	for (char &i : out)
		i = static_cast<char>(std::toupper(static_cast<unsigned char>(i)));
	return out;
}

static constexpr const char *kMacroWorkingMessageText = "working...";

struct MacroKeymapActionCommand {
	const char *name;
	const char *actionId;
};

static const char *keymapActionIdForMacroCommand(const std::string &name) noexcept {
	static constexpr std::array commands{
	    MacroKeymapActionCommand{"APPEND_BLOCK", "MRMAC_BLOCK_APPEND_TO_BUFFER"},
	    MacroKeymapActionCommand{"BACK_HOME", "MRMAC_DELETE_BACKWARD_TO_HOME"},
	    MacroKeymapActionCommand{"BACK_WORD", "MRMAC_DELETE_BACKWARD_WORD"},
	    MacroKeymapActionCommand{"BLOCK_MARK_LINES", "MR_BLOCK_MARK_LINES"},
	    MacroKeymapActionCommand{"BLOCK_TOGGLE_MARKING", "MR_BLOCK_TOGGLE_MARKING"},
	    MacroKeymapActionCommand{"BLOCK_TOGGLE_VISIBILITY", "MRMAC_BLOCK_TOGGLE_VISIBILITY"},
	    MacroKeymapActionCommand{"BOTTOM_OF_WINDOW", "MRMAC_CURSOR_BOTTOM_OF_WINDOW"},
	    MacroKeymapActionCommand{"CENTER_LINE", "MR_TEXT_CENTER_LINE"},
	    MacroKeymapActionCommand{"CENTER_LINE_ON_SCREEN", "MRMAC_VIEW_CENTER_LINE"},
	    MacroKeymapActionCommand{"COPY_BLOCK_TO_CLIPBOARD", "MRMAC_BLOCK_COPY_TO_CLIPBOARD"},
	    MacroKeymapActionCommand{"CUT_APPEND_BLOCK", "MRMAC_BLOCK_CUT_APPEND_TO_BUFFER"},
	    MacroKeymapActionCommand{"CUT_BLOCK", "MRMAC_BLOCK_MOVE_TO_BUFFER"},
	    MacroKeymapActionCommand{"DEL_CHAR_OR_BLOCK", "MRMAC_DELETE_FORWARD_CHAR_OR_BLOCK"},
	    MacroKeymapActionCommand{"DEL_EOL", "MRMAC_DELETE_TO_EOL"},
	    MacroKeymapActionCommand{"DEL_WORD", "MRMAC_DELETE_FORWARD_WORD"},
	    MacroKeymapActionCommand{"DESKTOP_MOVE_WINDOW_LEFT", "MR_DESKTOP_MOVE_WINDOW_LEFT"},
	    MacroKeymapActionCommand{"DESKTOP_MOVE_WINDOW_RIGHT", "MR_DESKTOP_MOVE_WINDOW_RIGHT"},
	    MacroKeymapActionCommand{"DESKTOP_VIEWPORT_LEFT", "MR_DESKTOP_VIEWPORT_LEFT"},
	    MacroKeymapActionCommand{"DESKTOP_VIEWPORT_RIGHT", "MR_DESKTOP_VIEWPORT_RIGHT"},
	    MacroKeymapActionCommand{"END_OF_BLOCK", "MRMAC_CURSOR_END_OF_BLOCK"},
	    MacroKeymapActionCommand{"FORCE_SAVE", "MR_FILE_FORCE_SAVE"},
	    MacroKeymapActionCommand{"INDENT_BLOCK", "MRMAC_BLOCK_INDENT"},
	    MacroKeymapActionCommand{"JUSTIFY_PARAGRAPH", "MR_JUSTIFY_PARAGRAPH"},
	    MacroKeymapActionCommand{"LIST_MATCHED_FILES", "MRMAC_SEARCH_LIST_MATCHED_FILES"},
	    MacroKeymapActionCommand{"LSP_CODE_ACTIONS", "MR_LSP_CODE_ACTIONS"},
	    MacroKeymapActionCommand{"LSP_COMPLETE", "MR_LSP_COMPLETE"},
	    MacroKeymapActionCommand{"LSP_DEFINITION", "MR_LSP_GOTO_DEFINITION"},
	    MacroKeymapActionCommand{"LSP_HIGHLIGHT", "MR_LSP_DOCUMENT_HIGHLIGHT"},
	    MacroKeymapActionCommand{"LSP_HOVER", "MR_LSP_SHOW_HOVER"},
	    MacroKeymapActionCommand{"LSP_REFERENCES", "MR_LSP_FIND_REFERENCES"},
	    MacroKeymapActionCommand{"LSP_RENAME", "MR_LSP_RENAME"},
	    MacroKeymapActionCommand{"LSP_SIGNATURE", "MR_LSP_SIGNATURE_HELP"},
	    MacroKeymapActionCommand{"LSP_SYMBOLS", "MR_LSP_DOCUMENT_SYMBOLS"},
	    MacroKeymapActionCommand{"LSP_WORKSPACE_SYMBOLS", "MR_LSP_WORKSPACE_SYMBOLS"},
	    MacroKeymapActionCommand{"LOAD_BLOCK", "MR_LOAD_BLOCK_FROM_FILE"},
	    MacroKeymapActionCommand{"MARK_WORD_RIGHT", "MRMAC_BLOCK_MARK_WORD_RIGHT"},
	    MacroKeymapActionCommand{"MULTI_FILE_SEARCH", "MRMAC_SEARCH_MULTI_FILE"},
	    MacroKeymapActionCommand{"MULTI_FILE_SEARCH_REPLACE", "MR_SEARCH_MULTI_FILE_REPLACE"},
	    MacroKeymapActionCommand{"NEXT_SEARCH_RESULT", "MR_SEARCH_RESULTS_NEXT"},
	    MacroKeymapActionCommand{"PASTE_BLOCK", "MRMAC_BLOCK_COPY_FROM_BUFFER"},
	    MacroKeymapActionCommand{"PASTE_FROM_CLIPBOARD", "MRMAC_BLOCK_PASTE_FROM_CLIPBOARD"},
	    MacroKeymapActionCommand{"REDO", "MRMAC_REDO_LAST_UNDO"},
	    MacroKeymapActionCommand{"REFORMAT_DOCUMENT", "MR_TEXT_REFORMAT_DOCUMENT"},
	    MacroKeymapActionCommand{"REFORMAT_PARAGRAPH", "MR_TEXT_REFORMAT_PARAGRAPH"},
	    MacroKeymapActionCommand{"REVERT_FILE", "MR_FILE_REVERT"},
	    MacroKeymapActionCommand{"REPEAT_SEARCH", "MRMAC_SEARCH_REPEAT_LAST"},
	    MacroKeymapActionCommand{"SAVE_ALL", "MR_FILE_SAVE_ALL"},
	    MacroKeymapActionCommand{"SCROLL_DOWN", "MRMAC_VIEW_SCROLL_DOWN"},
	    MacroKeymapActionCommand{"SCROLL_UP", "MRMAC_VIEW_SCROLL_UP"},
	    MacroKeymapActionCommand{"SEARCH", "MRMAC_SEARCH_FORWARD"},
	    MacroKeymapActionCommand{"SEARCH_REPLACE", "MRMAC_SEARCH_REPLACE"},
	    MacroKeymapActionCommand{"SET_LEFT_MARGIN", "MR_SET_LEFT_MARGIN"},
	    MacroKeymapActionCommand{"SET_RIGHT_MARGIN", "MR_SET_RIGHT_MARGIN"},
	    MacroKeymapActionCommand{"SORT_COLUMN_BLOCK_TOGGLE", "MR_SORT_COLUMN_BLOCK_TOGGLE"},
	    MacroKeymapActionCommand{"START_OF_BLOCK", "MRMAC_CURSOR_START_OF_BLOCK"},
	    MacroKeymapActionCommand{"TOGGLE_FORMAT_RULER", "MR_TOGGLE_FORMAT_RULER"},
	    MacroKeymapActionCommand{"TOGGLE_WORD_WRAP", "MR_TOGGLE_WORD_WRAP"},
	    MacroKeymapActionCommand{"TOP_OF_WINDOW", "MRMAC_CURSOR_TOP_OF_WINDOW"},
	    MacroKeymapActionCommand{"UNDO", "MRMAC_UNDO"},
	    MacroKeymapActionCommand{"UNDENT_BLOCK", "MRMAC_BLOCK_UNDENT"},
	    MacroKeymapActionCommand{"WINDOW_CASCADE", "MR_WINDOW_CASCADE"},
	    MacroKeymapActionCommand{"WINDOW_CLOSE", "MR_WINDOW_CLOSE"},
	    MacroKeymapActionCommand{"WINDOW_COPY_BLOCK", "MRMAC_BLOCK_COPY_INTERWINDOW"},
	    MacroKeymapActionCommand{"WINDOW_LIST", "MR_WINDOW_LIST"},
	    MacroKeymapActionCommand{"WINDOW_MOVE_BLOCK", "MRMAC_BLOCK_MOVE_INTERWINDOW"},
	    MacroKeymapActionCommand{"WINDOW_OPEN", "MR_WINDOW_OPEN"},
	    MacroKeymapActionCommand{"WINDOW_NEXT", "MR_WINDOW_NEXT"},
	    MacroKeymapActionCommand{"WINDOW_PREVIOUS", "MR_WINDOW_PREVIOUS"},
	    MacroKeymapActionCommand{"WINDOW_SPLIT_HORIZONTAL", "MR_WINDOW_SPLIT_HORIZONTAL"},
	    MacroKeymapActionCommand{"WINDOW_SPLIT_VERTICAL", "MR_WINDOW_SPLIT_VERTICAL"},
	    MacroKeymapActionCommand{"WINDOW_TILE", "MR_WINDOW_TILE"},
	    MacroKeymapActionCommand{"WINDOW_ZOOM", "MR_WINDOW_ZOOM"},
	    MacroKeymapActionCommand{"MATCH_PARENTHESIS", "MR_MATCH_PARENTHESIS"},
	    MacroKeymapActionCommand{"MACRO_TOGGLE_RECORDING", "MR_MACRO_TOGGLE_RECORDING"},
	    MacroKeymapActionCommand{"SETUP_EDIT_SETTINGS", "MR_SETUP_EDIT_SETTINGS"},
	    MacroKeymapActionCommand{"SETUP_COLOR", "MR_SETUP_COLOR"},
	    MacroKeymapActionCommand{"SETUP_KEYMAP", "MR_SETUP_KEYMAP"},
	    MacroKeymapActionCommand{"SETUP_LSP_SUPPORT", "MR_SETUP_LSP_SUPPORT"},
	    MacroKeymapActionCommand{"SETUP_FILENAME_EXTENSIONS", "MR_SETUP_FILENAME_EXTENSIONS"},
	    MacroKeymapActionCommand{"SETUP_COMPILER_PROFILES", "MR_SETUP_COMPILER_PROFILES"},
	    MacroKeymapActionCommand{"SETUP_PATHS", "MR_SETUP_PATHS"},
	    MacroKeymapActionCommand{"SETUP_BACKUPS_AUTOSAVE", "MR_SETUP_BACKUPS_AUTOSAVE"},
	    MacroKeymapActionCommand{"SETUP_SEARCH_REPLACE_DEFAULTS", "MR_SETUP_SEARCH_REPLACE_DEFAULTS"},
	    MacroKeymapActionCommand{"SETUP_USER_INTERFACE", "MR_SETUP_USER_INTERFACE"},
	    MacroKeymapActionCommand{"SETUP_LIVE_LOGS", "MR_SETUP_LIVE_LOGS"},
	    MacroKeymapActionCommand{"EXIT_SAVE_ALL", "MR_EXIT_DIRTY_SAVE_ALL"}};

	for (const MacroKeymapActionCommand &command : commands)
		if (name == command.name) return command.actionId;
	return nullptr;
}

static bool startsWithTokenInsensitive(const std::string &text, std::size_t pos, const char *token) {
	std::size_t i = 0;
	if (token == nullptr) return false;
	while (token[i] != '\0') {
		if (pos + i >= text.size()) return false;
		if (std::toupper(static_cast<unsigned char>(text[pos + i])) != std::toupper(static_cast<unsigned char>(token[i]))) return false;
		++i;
	}
	if (pos + i < text.size()) {
		unsigned char ch = static_cast<unsigned char>(text[pos + i]);
		if (std::isalnum(ch) != 0 || ch == '_') return false;
	}
	return true;
}

static void appendUniqueString(std::vector<std::string> &values, const std::string &value) {
	if (value.empty()) return;
	if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
}

static void logMacroProfileLine(const char *prefix, const LoadedMacroFile &file) {
	if (TProgram::deskTop == nullptr) return;
	std::string label = !file.displayName.empty() ? file.displayName : file.resolvedPath;
	std::string line = std::string(prefix) + " '" + label + "': " + mrvmDescribeExecutionProfile(file.profile);
	mrLogMessage(line.c_str());
}

static Value makeInt(int value) {
	Value v;
	v.type = TYPE_INT;
	v.i = value;
	return v;
}

static Value makeReal(double value) {
	Value v;
	v.type = TYPE_REAL;
	v.r = value;
	return v;
}

static Value makeString(const std::string &value) {
	Value v;
	v.type = TYPE_STR;
	v.s = value;
	return v;
}

static Value makeChar(unsigned char value) {
	Value v;
	v.type = TYPE_CHAR;
	v.c = value;
	return v;
}

static Value makeHash(int handle, bool globalStorage = false) {
	Value v;
	v.type = TYPE_HASH;
	v.hashHandle = handle;
	v.globalStorage = globalStorage;
	return v;
}

static std::string charToString(unsigned char c) {
	if (c == 0) return std::string();
	return std::string(1, static_cast<char>(c));
}

static bool isStringLike(const Value &value) {
	return value.type == TYPE_STR || value.type == TYPE_CHAR;
}

static bool isNumeric(const Value &value) {
	return value.type == TYPE_INT || value.type == TYPE_REAL;
}

static std::string valueAsString(const Value &value) {
	char buf[128];

	switch (value.type) {
		case TYPE_STR:
			return value.s;
		case TYPE_CHAR:
			return charToString(value.c);
		case TYPE_INT:
			std::snprintf(buf, sizeof(buf), "%d", value.i);
			return std::string(buf);
		case TYPE_REAL:
			std::snprintf(buf, sizeof(buf), "%.11g", value.r);
			return std::string(buf);
		default:
			return std::string();
	}
}

static std::string removeSpaceAscii(const std::string &value) {
	std::string out;
	bool previousWasSpace = false;
	std::size_t i = 0;
	std::size_t end = value.size();

	while (i < end && value[i] == ' ')
		++i;
	while (end > i && value[end - 1] == ' ')
		--end;

	for (; i < end; ++i) {
		char ch = value[i];
		if (ch == ' ') {
			if (!previousWasSpace) out.push_back(' ');
			previousWasSpace = true;
		} else {
			out.push_back(ch);
			previousWasSpace = false;
		}
	}
	return out;
}

static std::size_t findLastPathSeparator(const std::string &value) {
	std::size_t slash = value.find_last_of("\\/");
	if (slash == std::string::npos) return std::string::npos;
	return slash;
}

static std::size_t baseNameStart(const std::string &value) {
	std::size_t sep = findLastPathSeparator(value);
	if (sep != std::string::npos) return sep + 1;
	if (value.size() >= 2 && value[1] == ':') return 2;
	return 0;
}

static std::string getExtensionPart(const std::string &value) {
	std::size_t baseStart = baseNameStart(value);
	std::size_t dot = value.find_last_of('.');
	if (dot == std::string::npos || dot < baseStart) return std::string();
	return value.substr(dot);
}

static std::string getPathPart(const std::string &value) {
	std::size_t sep = findLastPathSeparator(value);
	if (sep != std::string::npos) return value.substr(0, sep + 1);
	if (value.size() >= 2 && value[1] == ':') return value.substr(0, 2);
	return std::string();
}

static std::string truncateExtensionPart(const std::string &value) {
	std::size_t baseStart = baseNameStart(value);
	std::size_t dot = value.find_last_of('.');
	if (dot == std::string::npos || dot < baseStart) return value;
	return value.substr(0, dot);
}

static std::string truncatePathPart(const std::string &value) {
	return value.substr(baseNameStart(value));
}

static double valueAsReal(const Value &value) {
	if (value.type == TYPE_REAL) return value.r;
	if (value.type == TYPE_INT) return static_cast<double>(value.i);
	throw std::runtime_error("numeric value expected");
}

static int valueAsInt(const Value &value) {
	if (value.type == TYPE_INT) return value.i;
	throw std::runtime_error("integer value expected");
}

static bool valueHasContent(const Value &value) {
	switch (value.type) {
		case TYPE_INT:
			return value.i != 0;
		case TYPE_REAL:
			return value.r != 0.0;
		case TYPE_CHAR:
			return value.c != 0;
		case TYPE_STR:
			return !value.s.empty();
		case TYPE_HASH:
			return value.hashHandle > 0;
		case TYPE_INT_ARRAY:
		case TYPE_STR_ARRAY:
		case TYPE_CHAR_ARRAY:
		case TYPE_REAL_ARRAY:
		case TYPE_HASH_ARRAY:
			return !value.arrayValues.empty();
		default:
			return false;
	}
}

static int compareValues(const Value &a, const Value &b) {
	if (isStringLike(a) && isStringLike(b)) {
		std::string as = valueAsString(a);
		std::string bs = valueAsString(b);
		if (as < bs) return -1;
		if (as > bs) return 1;
		return 0;
	}

	if (isNumeric(a) && isNumeric(b)) {
		double av = valueAsReal(a);
		double bv = valueAsReal(b);
		if (av < bv) return -1;
		if (av > bv) return 1;
		return 0;
	}

	throw std::runtime_error("type mismatch");
}

static void enforceStringLength(const std::string &s) {
	if (s.size() > 254) throw std::runtime_error("String length error.");
}

static std::string utf8FromCodepoint(std::uint32_t codepoint) {
	std::string text;
	int byteCount = 0;

	if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) throw std::runtime_error("UTF8 expects a valid Unicode codepoint.");
	byteCount = codepoint <= 0x7F ? 1 : (codepoint <= 0x7FF ? 2 : (codepoint <= 0xFFFF ? 3 : 4));
	switch (byteCount) {
		case 1:
			text.push_back(static_cast<char>(codepoint));
			break;
		case 2:
			text.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
			text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			break;
		case 3:
			text.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
			text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
			text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			break;
		case 4:
			text.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
			text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
			text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
			text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			break;
	}
	return text;
}

static int checkedStringIndex(int pos) {
	if (pos < 1 || pos > 254) throw std::runtime_error("Invalid string index on string copy operation.");
	return pos;
}

static int checkedInsertIndex(int pos) {
	if (pos < 0 || pos > 254) throw std::runtime_error("Invalid string index on string copy operation.");
	return pos;
}

static int findValErrorPosition(const std::string &text) {
	std::size_t i = 0;
	const std::size_t n = text.size();

	while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
		++i;
	if (i == n) return 1;

	if (text[i] == '+' || text[i] == '-') ++i;

	{
		const std::size_t firstDigit = i;
		while (i < n && std::isdigit(static_cast<unsigned char>(text[i])))
			++i;
		if (i == firstDigit) return static_cast<int>(firstDigit + 1);
	}

	while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
		++i;
	if (i != n) return static_cast<int>(i + 1);
	return 0;
}

static int findRValErrorPosition(const std::string &text) {
	std::size_t i = 0;
	const std::size_t n = text.size();

	while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
		++i;
	if (i == n) return 1;

	if (text[i] == '+' || text[i] == '-') ++i;

	{
		bool seenDigits = false;
		while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) {
			seenDigits = true;
			++i;
		}
		if (i < n && text[i] == '.') {
			++i;
			while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) {
				seenDigits = true;
				++i;
			}
		}
		if (!seenDigits) return static_cast<int>(i + 1);
	}

	if (i < n && (text[i] == 'e' || text[i] == 'E')) {
		const std::size_t expPos = i;
		++i;
		if (i < n && (text[i] == '+' || text[i] == '-')) ++i;
		{
			const std::size_t firstExpDigit = i;
			while (i < n && std::isdigit(static_cast<unsigned char>(text[i])))
				++i;
			if (i == firstExpDigit) return static_cast<int>(expPos + 1);
		}
	}

	while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
		++i;
	if (i != n) return static_cast<int>(i + 1);
	return 0;
}

static std::string commandFirstLine(const std::string &command) {
	std::string line;
	FILE *pipe = ::popen(command.c_str(), "r");
	if (pipe == nullptr) return std::string();
	char buffer[512];
	if (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) line = buffer;
	::pclose(pipe);
	while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
		line.pop_back();
	return line;
}

static std::string detectExecutablePathFromProc() {
	char buf[4096];
	ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n <= 0) return std::string();
	buf[n] = '\0';
	return std::string(buf);
}

static std::string normalizeDirPath(const std::string &path) {
	if (path.empty()) return std::string("./");
	std::string out = path;
	if (out.back() != '/') out.push_back('/');
	return out;
}

static std::string detectExecutableDir(const std::string &argv0) {
	std::string path = detectExecutablePathFromProc();
	if (path.empty()) path = argv0;
	if (path.empty()) {
		char cwd[4096];
		if (::getcwd(cwd, sizeof(cwd)) != nullptr) return normalizeDirPath(std::string(cwd));
		return std::string("./");
	}
	std::size_t sep = path.find_last_of("/");
	if (sep == std::string::npos) {
		char cwd[4096];
		if (::getcwd(cwd, sizeof(cwd)) != nullptr) return normalizeDirPath(std::string(cwd));
		return std::string("./");
	}
	return normalizeDirPath(path.substr(0, sep));
}

static std::string detectShellPath() {
	const char *comspec = std::getenv("COMSPEC");
	if (comspec != nullptr && *comspec != '\0') return std::string(comspec);
	const char *shell = std::getenv("SHELL");
	if (shell != nullptr && *shell != '\0') return std::string(shell);
	return std::string("/bin/sh");
}

static std::string detectShellVersion(const std::string &shellPath) {
	if (shellPath.empty()) return std::string();
	const char *bashVersion = std::getenv("BASH_VERSION");
	const char *zshVersion = std::getenv("ZSH_VERSION");
	const char *fishVersion = std::getenv("FISH_VERSION");
	std::string base = shellPath.substr(shellPath.find_last_of('/') == std::string::npos ? 0 : shellPath.find_last_of('/') + 1);
	if (base == "bash" && bashVersion != nullptr && *bashVersion != '\0') return std::string("bash ") + bashVersion;
	if (base == "zsh" && zshVersion != nullptr && *zshVersion != '\0') return std::string("zsh ") + zshVersion;
	if (base == "fish" && fishVersion != nullptr && *fishVersion != '\0') return std::string("fish ") + fishVersion;
	std::string command = "'";
	for (char i : shellPath) {
		if (i == '\'') command += "'\\''";
		else
			command.push_back(i);
	}
	command += "' --version 2>/dev/null";
	std::string line = commandFirstLine(command);
	if (!line.empty()) return line;
	return base;
}

static int detectCpuCode() {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
	return 3;
#elif defined(__aarch64__) || defined(__arm__) || defined(__riscv) || defined(__powerpc__) || defined(__ppc64__)
	return 3;
#else
	return 3;
#endif
}

static std::string getenvValue(const std::string &name) {
	const char *value = std::getenv(name.c_str());
	if (value == nullptr) return std::string();
	return std::string(value);
}

static std::string getEnvironmentValue(const std::string &entryName) {
	std::string key = trimAscii(entryName);
	std::size_t pos = key.find('=');
	if (pos != std::string::npos) key = key.substr(0, pos);
	if (key.empty()) return std::string();
	std::string direct = getenvValue(key);
	if (!direct.empty()) return direct;
	std::string up = upperKey(key);
	if (up == "MR_PATH") return g_runtimeEnv.executableDir;
	if (up == "COMSPEC") return g_runtimeEnv.shellPath;
	if (up == "OS_VERSION") return g_runtimeEnv.shellVersion;
	return std::string();
}

static bool changeDirectoryPath(const std::string &path) {
	std::string expanded = expandUserPath(trimAscii(path));
	if (expanded.empty()) return false;
	return ::chdir(expanded.c_str()) == 0;
}

static bool deleteFilePath(const std::string &path) {
	std::string expanded = expandUserPath(trimAscii(path));
	if (expanded.empty()) return false;
	return std::remove(expanded.c_str()) == 0;
}

std::string expandUserPath(const std::string &path) {
	if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != nullptr && *home != '\0') return std::string(home) + path.substr(1);
	}
	return path;
}

static bool fileExistsPath(const std::string &path) {
	struct stat st;
	std::string expanded = expandUserPath(trimAscii(path));
	if (expanded.empty()) return false;
	return ::stat(expanded.c_str(), &st) == 0;
}

static int inferDosFileAttributes(const std::string &path, const struct stat &st) {
	int attr = 0;
	std::string name = truncatePathPart(path);

	if (!name.empty() && name.front() == '.') attr |= 0x02;
	if (S_ISDIR(st.st_mode)) attr |= 0x10;
	else
		attr |= 0x20;
	if (::access(path.c_str(), W_OK) != 0) attr |= 0x01;
	return attr;
}

static bool readFileMetadata(const std::string &path, int *attrOut, int *sizeOut, int *timeOut) {
	struct stat st;
	std::string expanded = expandUserPath(trimAscii(path));

	if (expanded.empty() || ::stat(expanded.c_str(), &st) != 0) return false;

	if (attrOut != nullptr) *attrOut = inferDosFileAttributes(expanded, st);
	if (sizeOut != nullptr) {
		long long size = static_cast<long long>(st.st_size);
		if (size < 0) size = 0;
		if (size > std::numeric_limits<int>::max()) size = std::numeric_limits<int>::max();
		*sizeOut = static_cast<int>(size);
	}
	if (timeOut != nullptr) {
		std::tm localTime{};
		if (::localtime_r(&st.st_mtime, &localTime) == nullptr) *timeOut = 0;
		else {
			const int dosDate = ((std::max(0, localTime.tm_year + 1900 - 1980) & 0x7F) << 9) | (((localTime.tm_mon + 1) & 0x0F) << 5) | (localTime.tm_mday & 0x1F);
			const int dosTime = ((localTime.tm_hour & 0x1F) << 11) | ((localTime.tm_min & 0x3F) << 5) | ((localTime.tm_sec / 2) & 0x1F);
			*timeOut = (dosDate << 16) | dosTime;
		}
	}
	return true;
}

static int findFirstFileMatch(const std::string &pattern) {
	glob_t g;
	std::string expanded = expandUserPath(trimAscii(pattern));
	int rc;

	g_runtimeEnv.fileMatches.clear();
	g_runtimeEnv.fileMatchIndex = 0;
	g_runtimeEnv.lastFileName.clear();

	std::memset(&g, 0, sizeof(g));
	rc = ::glob(expanded.c_str(), 0, nullptr, &g);
	if (rc == 0) {
		for (std::size_t i = 0; i < g.gl_pathc; ++i)
			g_runtimeEnv.fileMatches.emplace_back(g.gl_pathv[i]);
		::globfree(&g);
		if (!g_runtimeEnv.fileMatches.empty()) {
			g_runtimeEnv.lastFileName = g_runtimeEnv.fileMatches[0];
			return 0;
		}
	} else
		::globfree(&g);

	if (fileExistsPath(expanded)) {
		g_runtimeEnv.fileMatches.push_back(expanded);
		g_runtimeEnv.lastFileName = expanded;
		return 0;
	}

	return 18;
}

static int findNextFileMatch() {
	if (g_runtimeEnv.fileMatches.empty()) return 18;
	if (g_runtimeEnv.fileMatchIndex + 1 >= g_runtimeEnv.fileMatches.size()) return 18;
	++g_runtimeEnv.fileMatchIndex;
	g_runtimeEnv.lastFileName = g_runtimeEnv.fileMatches[g_runtimeEnv.fileMatchIndex];
	return 0;
}

MREditWindow *activeMacroEditWindow() {
	if (TProgram::deskTop == nullptr || TProgram::deskTop->current == nullptr) return nullptr;
	return dynamic_cast<MREditWindow *>(TProgram::deskTop->current);
}

MRFileEditor *currentEditor() {
	MREditWindow *win = activeMacroEditWindow();
	return win != nullptr ? win->getEditor() : nullptr;
}

static bool openSnippetSidekickFromActiveEditor(MRVMHashStore &store) {
	GlobalEntry rootEntry;

	if (!readGlobalValue("SNIPPETS", rootEntry) || rootEntry.type != TYPE_HASH || rootEntry.value.type != TYPE_HASH) return false;
	return mrvmSnippetOpenSidekickFromActiveEditor(activeMacroEditWindow(), store, g_runtimeEnv.globalHashStore, rootEntry.value);
}

static BackgroundEditSession *currentBackgroundEditSession() noexcept {
	return g_backgroundEditSession;
}

static ExecutionState *currentExecutionState() noexcept {
	return g_executionState;
}

static std::string &runtimeParameterString() noexcept {
	ExecutionState *state = currentExecutionState();
	return state != nullptr ? state->parameterString : g_runtimeEnv.parameterString;
}

static int &runtimeReturnInt() noexcept {
	ExecutionState *state = currentExecutionState();
	return state != nullptr ? state->returnInt : g_runtimeEnv.returnInt;
}

static std::string &runtimeReturnStr() noexcept {
	ExecutionState *state = currentExecutionState();
	return state != nullptr ? state->returnStr : g_runtimeEnv.returnStr;
}

static int &runtimeErrorLevel() noexcept {
	ExecutionState *state = currentExecutionState();
	return state != nullptr ? state->errorLevel : g_runtimeEnv.errorLevel;
}

struct SubshellResult {
	std::string output;
	int errorLevel;
};

static SubshellResult runSubshellCapture(const std::string &command, int timeoutMs) {
	SubshellResult result;
	int pipeFds[2] = {-1, -1};
	pid_t childPid = -1;
	int waitStatus = 0;
	bool pipeOpen = true;
	bool childExited = false;
	bool timedOut = false;
	int timeoutPolls = 0;
	std::array<char, 4096> buffer{};
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

	result.errorLevel = 0;
	if (timeoutMs <= 0) {
		result.errorLevel = EINVAL;
		return result;
	}
	if (::pipe(pipeFds) != 0) {
		result.errorLevel = errno != 0 ? errno : 1;
		return result;
	}
	childPid = ::fork();
	if (childPid < 0) {
		result.errorLevel = errno != 0 ? errno : 1;
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		return result;
	}
	if (childPid == 0) {
		std::string shellPath = configuredShellExecutablePath();
		::setpgid(0, 0);
		::dup2(pipeFds[1], STDOUT_FILENO);
		::dup2(pipeFds[1], STDERR_FILENO);
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		::execl(shellPath.c_str(), shellPath.c_str(), "-lc", command.c_str(), static_cast<char *>(nullptr));
		::_exit(127);
	}
	::close(pipeFds[1]);
	::setpgid(childPid, childPid);
	{
		const int flags = ::fcntl(pipeFds[0], F_GETFL, 0);
		if (flags >= 0) ::fcntl(pipeFds[0], F_SETFL, flags | O_NONBLOCK);
	}
	while (pipeOpen || !childExited) {
		struct pollfd pfd;
		int pollResult = 0;

		if (!childExited && !timedOut && std::chrono::steady_clock::now() >= deadline) {
			timedOut = true;
			::kill(-childPid, SIGTERM);
		}
		if (!childExited && timedOut) {
			if (timeoutPolls > 10) ::kill(-childPid, SIGKILL);
			++timeoutPolls;
		}
		pfd.fd = pipeFds[0];
		pfd.events = POLLIN | POLLHUP;
		pfd.revents = 0;
		pollResult = pipeOpen ? ::poll(&pfd, 1, 50) : 0;
		if (pollResult < 0 && errno != EINTR) {
			result.errorLevel = errno != 0 ? errno : 1;
			break;
		}
		if (pipeOpen && pollResult > 0) {
			for (;;) {
				const ssize_t count = ::read(pipeFds[0], buffer.data(), buffer.size());
				if (count > 0) {
					result.output.append(buffer.data(), static_cast<std::size_t>(count));
					continue;
				}
				if (count == 0) {
					pipeOpen = false;
					break;
				}
				if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
				result.errorLevel = errno != 0 ? errno : 1;
				pipeOpen = false;
				break;
			}
		}
		if (!pipeOpen && !childExited) std::this_thread::sleep_for(std::chrono::milliseconds(50));
		if (!childExited) {
			const pid_t waited = ::waitpid(childPid, &waitStatus, WNOHANG);
			if (waited == childPid) childExited = true;
			else if (waited < 0 && errno != EINTR) {
				result.errorLevel = errno != 0 ? errno : 1;
				break;
			}
		}
		if (childExited && pipeOpen && pollResult == 0) pipeOpen = false;
	}
	if (pipeFds[0] >= 0) ::close(pipeFds[0]);
	if (!childExited && childPid > 0) {
		::kill(-childPid, SIGKILL);
		while (::waitpid(childPid, &waitStatus, 0) < 0 && errno == EINTR)
			;
		childExited = true;
	}
	if (result.errorLevel != 0) return result;
	if (timedOut) {
		result.errorLevel = 124;
		return result;
	}
	if (WIFEXITED(waitStatus)) result.errorLevel = WEXITSTATUS(waitStatus);
	else if (WIFSIGNALED(waitStatus))
		result.errorLevel = 128 + WTERMSIG(waitStatus);
	else
		result.errorLevel = 1;
	return result;
}

static char normalizeSearchChar(char c, bool ignoreCase) noexcept {
	if (!ignoreCase) return c;
	return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

static bool backgroundMacroCancelRequested() noexcept {
	return (g_backgroundMacroStopToken != nullptr && g_backgroundMacroStopToken->stop_requested()) || (g_backgroundMacroCancelFlag != nullptr && g_backgroundMacroCancelFlag->load(std::memory_order_acquire));
}

static bool currentRuntimeIgnoreCase() noexcept {
	BackgroundEditSession *session = currentBackgroundEditSession();
	return session != nullptr ? session->ignoreCase : g_runtimeEnv.ignoreCase;
}

static int currentRegexStatusValue() {
	const MRSearchDialogOptions searchOptions = configuredSearchDialogOptions();
	const MRSarDialogOptions sarOptions = configuredSarDialogOptions();

	return searchOptions.textType == MRSearchTextType::Pcre || sarOptions.textType == MRSearchTextType::Pcre ? 1 : 0;
}

static bool setCurrentRegexStatus(bool enabled) {
	std::string errorText;
	MRSearchDialogOptions searchOptions = configuredSearchDialogOptions();
	MRSarDialogOptions sarOptions = configuredSarDialogOptions();
	MRMultiSearchDialogOptions multiSearchOptions = configuredMultiSearchDialogOptions();
	MRMultiSarDialogOptions multiSarOptions = configuredMultiSarDialogOptions();

	searchOptions.textType = enabled ? MRSearchTextType::Pcre : MRSearchTextType::Literal;
	sarOptions.textType = enabled ? MRSearchTextType::Pcre : MRSearchTextType::Literal;
	multiSearchOptions.regularExpressions = enabled;
	multiSarOptions.regularExpressions = enabled;

	if (!setConfiguredSearchDialogOptions(searchOptions, &errorText)) return false;
	if (!setConfiguredSarDialogOptions(sarOptions, &errorText)) return false;
	if (!setConfiguredMultiSearchDialogOptions(multiSearchOptions, &errorText)) return false;
	if (!setConfiguredMultiSarDialogOptions(multiSarOptions, &errorText)) return false;
	return true;
}

static bool currentRuntimeTabExpand() noexcept {
	BackgroundEditSession *session = currentBackgroundEditSession();
	return session != nullptr ? session->tabExpand : g_runtimeEnv.tabExpand;
}

struct SearchMatchSnapshot {
	bool valid = false;
	std::string fileName;
	std::string foundText;
	int foundX = 0;
	int foundY = 0;
};

static void computeLineColumnForOffset(const std::string &text, std::size_t offset, int &line, int &column) {
	line = 1;
	column = 1;
	offset = std::min(offset, text.size());
	for (std::size_t i = 0; i < offset; ++i) {
		if (text[i] == '\n') {
			++line;
			column = 1;
		} else
			++column;
	}
}

static SearchMatchSnapshot currentSearchMatchSnapshot() {
	SearchMatchSnapshot snapshot;
	BackgroundEditSession *session = currentBackgroundEditSession();

	if (session != nullptr) {
		const std::string text = session->document.text();
		if (!session->lastSearchValid || session->lastSearchEnd < session->lastSearchStart || session->lastSearchEnd > text.size()) return snapshot;
		snapshot.valid = true;
		snapshot.fileName = session->fileName;
		snapshot.foundText = text.substr(session->lastSearchStart, session->lastSearchEnd - session->lastSearchStart);
		computeLineColumnForOffset(text, session->lastSearchStart, snapshot.foundY, snapshot.foundX);
		return snapshot;
	}

	MREditWindow *win = const_cast<MREditWindow *>(static_cast<const MREditWindow *>(g_runtimeEnv.lastSearchWindow));
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	if (!g_runtimeEnv.lastSearchValid || editor == nullptr) return snapshot;

	const std::string text = editor->snapshotText();
	if (g_runtimeEnv.lastSearchEnd < g_runtimeEnv.lastSearchStart || g_runtimeEnv.lastSearchEnd > text.size()) return snapshot;

	snapshot.valid = true;
	snapshot.fileName = g_runtimeEnv.lastSearchFileName;
	snapshot.foundText = text.substr(g_runtimeEnv.lastSearchStart, g_runtimeEnv.lastSearchEnd - g_runtimeEnv.lastSearchStart);
	computeLineColumnForOffset(text, g_runtimeEnv.lastSearchStart, snapshot.foundY, snapshot.foundX);
	return snapshot;
}

static Value loadCurrentFileState(const std::string &key) {
	MREditWindow *win = activeMacroEditWindow();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (key == "FIRST_SAVE") {
		if (win != nullptr) return makeInt(win->hasBeenSavedInSession() ? 1 : 0);
		return makeInt(session != nullptr && session->firstSave ? 1 : 0);
	}
	if (key == "BUFFER_ID") {
		if (win != nullptr) return makeInt(win->bufferId());
		if (session != nullptr) return makeInt(session->bufferId);
		return makeInt(0);
	}
	if (key == "TMP_FILE") {
		if (win != nullptr) return makeInt(win->isTemporaryFile() ? 1 : 0);
		return makeInt(session != nullptr && session->temporaryFile ? 1 : 0);
	}
	if (key == "TMP_FILE_NAME") {
		if (win != nullptr) return makeString(win->temporaryFileName());
		if (session != nullptr) return makeString(session->temporaryFileName);
		return makeString("");
	}
	if (key == "FILE_CHANGED") {
		if (win != nullptr) return makeInt(win->isFileChanged() ? 1 : 0);
		return makeInt(session != nullptr && session->fileChanged ? 1 : 0);
	}
	if (key == "FILE_NAME") {
		if (win != nullptr) return makeString(win->currentFileName());
		if (session != nullptr) return makeString(session->fileName);
		return makeString("");
	}
	if (key == "CUR_FILE_ATTR") {
		int attr = 0;
		std::string path = win != nullptr ? std::string(win->currentFileName()) : (session != nullptr ? session->fileName : std::string());
		if (!readFileMetadata(path, &attr, nullptr, nullptr)) return makeInt(0);
		return makeInt(attr);
	}
	if (key == "CUR_FILE_SIZE") {
		int size = 0;
		std::string path = win != nullptr ? std::string(win->currentFileName()) : (session != nullptr ? session->fileName : std::string());
		if (!readFileMetadata(path, nullptr, &size, nullptr)) return makeInt(0);
		return makeInt(size);
	}
	if (key == "READ_ONLY") {
		if (win != nullptr) return makeInt(win->isReadOnly() ? 1 : 0);
		return makeInt(0);
	}
	return makeInt(0);
}

static std::string snapshotEditorText(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->snapshotText();
	return session != nullptr ? session->document.text() : std::string();
}

static std::size_t backgroundSearchLimitForward(const mr::editor::TextDocument &document, std::size_t start, int numLines) {
	if (numLines <= 0) return document.length();

	std::size_t pos = document.clampOffset(start);
	int remaining = numLines;
	while (pos < document.length()) {
		if (document.charAt(pos) == '\n') {
			--remaining;
			if (remaining == 0) return pos;
		}
		++pos;
	}
	return document.length();
}

static std::size_t backgroundSearchLimitBackward(const mr::editor::TextDocument &document, std::size_t start, int numLines) {
	if (numLines <= 0) return 0;

	std::size_t pos = document.clampOffset(start);
	int remaining = numLines;
	while (pos > 0) {
		--pos;
		if (document.charAt(pos) == '\n') {
			--remaining;
			if (remaining == 0) return pos + 1;
		}
	}
	return 0;
}

static bool searchEditorForward(MRFileEditor *editor, const std::string &needle, int numLines, bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd) {
	std::string text;
	std::string haystack;
	std::string query;
	std::size_t startPos;
	std::size_t endPos;
	std::size_t found;

	matchStart = matchEnd = 0;
	if (needle.empty()) return false;
	if (editor == nullptr) {
		BackgroundEditSession *session = currentBackgroundEditSession();
		std::size_t startPos;
		std::size_t endPos;
		std::size_t needleLen;
		if (session == nullptr) return false;
		startPos = std::min<std::size_t>(session->cursorOffset, session->document.length());
		endPos = backgroundSearchLimitForward(session->document, startPos, numLines);
		needleLen = needle.size();
		if (needleLen == 0 || endPos < startPos || startPos + needleLen > endPos) return false;
		for (std::size_t pos = startPos; pos + needleLen <= endPos; ++pos) {
			bool ok = true;
			for (std::size_t i = 0; i < needleLen; ++i)
				if (normalizeSearchChar(session->document.charAt(pos + i), ignoreCase) != normalizeSearchChar(needle[i], ignoreCase)) {
					ok = false;
					break;
				}
			if (ok) {
				matchStart = pos;
				matchEnd = pos + needleLen;
				return true;
			}
		}
		return false;
	}

	text = snapshotEditorText(editor);
	startPos = std::min<std::size_t>(editor->cursorOffset(), text.size());
	endPos = searchLimitForward(text, startPos, numLines);
	if (endPos < startPos) endPos = startPos;

	haystack = text.substr(startPos, endPos - startPos);
	query = needle;
	if (ignoreCase) {
		haystack = upperKey(haystack);
		query = upperKey(query);
	}

	found = haystack.find(query);
	if (found == std::string::npos) return false;

	matchStart = startPos + found;
	matchEnd = matchStart + needle.size();
	return matchEnd <= text.size();
}

static bool searchEditorBackward(MRFileEditor *editor, const std::string &needle, int numLines, bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd) {
	std::string text;
	std::string haystack;
	std::string query;
	std::size_t startPos;
	std::size_t endPos;
	std::size_t found;

	matchStart = matchEnd = 0;
	if (needle.empty()) return false;
	if (editor == nullptr) {
		BackgroundEditSession *session = currentBackgroundEditSession();
		std::size_t startPos;
		std::size_t endPos;
		std::size_t needleLen;
		std::size_t pos;
		if (session == nullptr) return false;
		endPos = std::min<std::size_t>(session->cursorOffset, session->document.length());
		startPos = backgroundSearchLimitBackward(session->document, endPos, numLines);
		needleLen = needle.size();
		if (needleLen == 0 || session->document.length() == 0) return false;
		pos = std::min(endPos, session->document.length() - 1);
		while (true) {
			if (pos >= startPos && pos + needleLen <= session->document.length()) {
				bool ok = true;
				for (std::size_t i = 0; i < needleLen; ++i)
					if (normalizeSearchChar(session->document.charAt(pos + i), ignoreCase) != normalizeSearchChar(needle[i], ignoreCase)) {
						ok = false;
						break;
					}
				if (ok) {
					matchStart = pos;
					matchEnd = pos + needleLen;
					return true;
				}
			}
			if (pos == 0 || pos == startPos) break;
			--pos;
		}
		return false;
	}

	text = snapshotEditorText(editor);
	endPos = std::min<std::size_t>(editor->cursorOffset(), text.size());
	startPos = searchLimitBackward(text, endPos, numLines);
	if (endPos < startPos) endPos = startPos;

	haystack = text.substr(startPos, endPos - startPos + std::min<std::size_t>(needle.size(), text.size() - endPos));
	query = needle;
	if (ignoreCase) {
		haystack = upperKey(haystack);
		query = upperKey(query);
	}

	found = haystack.rfind(query, endPos - startPos);
	if (found == std::string::npos) return false;

	matchStart = startPos + found;
	matchEnd = matchStart + needle.size();
	return matchEnd <= text.size();
}

static bool replaceLastSearch(MRFileEditor *editor, const std::string &replacement) {
	MREditWindow *win = activeMacroEditWindow();
	const char *fileName;
	if (editor == nullptr || !g_runtimeEnv.lastSearchValid) return false;
	if (win == nullptr || g_runtimeEnv.lastSearchWindow != win) return false;
	fileName = win->currentFileName();
	if (g_runtimeEnv.lastSearchFileName != std::string(fileName != nullptr ? fileName : "")) return false;
	if (editor->cursorOffset() != g_runtimeEnv.lastSearchCursor) return false;
	if (g_runtimeEnv.lastSearchEnd < g_runtimeEnv.lastSearchStart || g_runtimeEnv.lastSearchEnd > editor->bufferLength()) return false;

	if (!editor->replaceRangeAndSelect(static_cast<uint>(g_runtimeEnv.lastSearchStart), static_cast<uint>(g_runtimeEnv.lastSearchEnd), replacement.c_str(), static_cast<uint>(replacement.size()))) return false;

	g_runtimeEnv.lastSearchEnd = g_runtimeEnv.lastSearchStart + replacement.size();
	g_runtimeEnv.lastSearchCursor = g_runtimeEnv.lastSearchStart;
	g_runtimeEnv.lastSearchValid = false;
	return true;
}

static bool replaceLastSearchBackground(const std::string &replacement) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (session == nullptr || !session->lastSearchValid) return false;
	if (session->cursorOffset != session->lastSearchCursor) return false;
	if (session->lastSearchEnd < session->lastSearchStart || session->lastSearchEnd > session->document.length()) return false;
	if (!backgroundReplaceRange(mr::editor::Range(session->lastSearchStart, session->lastSearchEnd), replacement, session->lastSearchStart)) return false;

	session->lastSearchEnd = session->lastSearchStart + replacement.size();
	session->lastSearchCursor = session->lastSearchStart;
	session->lastSearchValid = false;
	return true;
}

static bool backgroundReplaceRange(const mr::editor::Range &range, const std::string &text, std::size_t cursorPos) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (session == nullptr) return false;
	session->transaction.replace(range, text);
	session->document.replace(range, text);
	session->cursorOffset = std::min(cursorPos, session->document.length());
	session->fileChanged = true;
	session->clearSelection();
	session->clearLastSearch();
	return true;
}

static bool backgroundSetCursor(std::size_t target) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (session == nullptr) return false;
	session->cursorOffset = session->document.clampOffset(target);
	session->clearSelection();
	session->clearLastSearch();
	return true;
}

static std::size_t backgroundCharPtrOffset(std::size_t lineStart, int column) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	std::size_t pos;
	std::size_t lineEnd;
	int target;

	if (session == nullptr) return 0;
	pos = session->document.lineStart(lineStart);
	lineEnd = session->document.lineEnd(pos);
	target = std::max(column, 0);
	while (pos < lineEnd && target > 0) {
		++pos;
		--target;
	}
	return pos;
}

static std::size_t backgroundLineMoveOffset(std::size_t offset, int delta) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	std::size_t targetLine;
	std::size_t currentLine;
	std::size_t targetLineStart;
	int visualColumn;

	if (session == nullptr) return 0;
	currentLine = session->document.lineIndex(offset);
	visualColumn = static_cast<int>(session->document.column(offset));
	if (delta < 0) {
		std::size_t distance = static_cast<std::size_t>(-delta);
		targetLine = currentLine > distance ? currentLine - distance : 0;
	} else {
		targetLine = currentLine + static_cast<std::size_t>(delta);
	}
	targetLineStart = session->document.lineStartByIndex(targetLine);
	return backgroundCharPtrOffset(targetLineStart, visualColumn);
}

static bool backgroundWordChar(char c) noexcept {
	unsigned char uc = static_cast<unsigned char>(c);
	return std::isalnum(uc) != 0 || c == '_';
}

static std::size_t backgroundPrevWordOffset(std::size_t offset) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	std::size_t pos;

	if (session == nullptr) return 0;
	pos = session->document.clampOffset(offset);
	while (pos > 0 && !backgroundWordChar(session->document.charAt(pos - 1)))
		--pos;
	while (pos > 0 && backgroundWordChar(session->document.charAt(pos - 1)))
		--pos;
	return pos;
}

static std::size_t backgroundNextWordOffset(std::size_t offset) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	std::size_t pos;
	std::size_t len;

	if (session == nullptr) return 0;
	pos = session->document.clampOffset(offset);
	len = session->document.length();
	while (pos < len && backgroundWordChar(session->document.charAt(pos)))
		++pos;
	while (pos < len && !backgroundWordChar(session->document.charAt(pos)))
		++pos;
	return pos;
}

static Value currentEditorCharValue() {
	MRFileEditor *editor = currentEditor();
	uint lineEnd;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return makeChar(static_cast<char>(255));
		lineEnd = static_cast<uint>(session->document.lineEnd(session->cursorOffset));
		if (session->cursorOffset >= session->document.length() || session->cursorOffset >= lineEnd) return makeChar(static_cast<char>(255));
		return makeChar(session->document.charAt(session->cursorOffset));
	}
	lineEnd = editor->lineEndOffset(editor->cursorOffset());
	if (editor->cursorOffset() >= editor->bufferLength() || editor->cursorOffset() >= lineEnd) return makeChar(static_cast<char>(255));
	return makeChar(editor->charAtOffset(editor->cursorOffset()));
}

static bool isVirtualChar(char c) {
	return static_cast<unsigned char>(c) == 255;
}

static int nextResolvedTabDisplayColumn(const MREditSetupSettings &settings, int col) {
	return resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, col);
}

static std::string expandTabsString(const std::string &value, bool toVirtuals) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	std::string out;
	int col = 1;
	out.reserve(value.size());
	for (char i : value) {
		unsigned char ch = static_cast<unsigned char>(i);
		if (ch == '	') {
			int next = nextResolvedTabDisplayColumn(settings, col);
			int width = next - col;
			if (toVirtuals) {
				out.push_back('	');
				for (int n = 1; n < width; ++n)
					out.push_back(static_cast<char>(255));
			} else {
				for (int n = 0; n < width; ++n)
					out.push_back(' ');
			}
			col = next;
		} else {
			out.push_back(i);
			if (ch == '\n' || ch == '\r') col = 1;
			else
				++col;
		}
	}
	enforceStringLength(out);
	return out;
}

static std::string tabsToSpacesString(const std::string &value) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	std::string out;
	int col = 1;
	out.reserve(value.size());
	for (std::string::size_type i = 0; i < value.size(); ++i) {
		unsigned char ch = static_cast<unsigned char>(value[i]);
		if (ch == '	') {
			int next = nextResolvedTabDisplayColumn(settings, col);
			int width = next - col;
			for (int n = 0; n < width; ++n)
				out.push_back(' ');
			col = next;
			while (i + 1 < value.size() && isVirtualChar(value[i + 1]))
				++i;
		} else if (isVirtualChar(value[i])) {
			out.push_back(' ');
			++col;
		} else {
			out.push_back(value[i]);
			if (ch == '\n' || ch == '\r') col = 1;
			else
				++col;
		}
	}
	enforceStringLength(out);
	return out;
}

static int expandedTabsAdjustedIndex(const std::string &value, int index) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	int sourcePos = 1;
	int mappedPos = 1;
	int col = 1;
	int clampedIndex = std::max(1, std::min(index, 255));

	for (char i : value) {
		unsigned char ch = static_cast<unsigned char>(i);
		if (sourcePos >= clampedIndex) break;
		if (ch == '\t') {
			int next = nextResolvedTabDisplayColumn(settings, col);
			mappedPos += next - col;
			col = next;
		} else {
			++mappedPos;
			if (ch == '\n' || ch == '\r') col = 1;
			else
				++col;
		}
		++sourcePos;
	}
	if (clampedIndex > sourcePos) mappedPos += clampedIndex - sourcePos;
	return std::max(1, std::min(mappedPos, 255));
}

static int currentEditorIndentLevel() {
	MREditWindow *win = activeMacroEditWindow();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (win != nullptr) return win->indentLevel();
	return session != nullptr ? session->indentLevel : 1;
}

static bool setCurrentEditorIndentLevel(int level) {
	MREditWindow *win = activeMacroEditWindow();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (win != nullptr) {
		win->setIndentLevel(level);
		return true;
	}
	if (session == nullptr) return false;
	if (level < 1) level = 1;
	if (level > 254) level = 254;
	session->indentLevel = level;
	return true;
}

static bool currentEditorInsertMode() {
	MRFileEditor *editor = currentEditor();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->insertModeEnabled();
	if (session != nullptr) return session->insertMode;
	return true;
}

static bool setCurrentEditorInsertMode(bool on) {
	MRFileEditor *editor = currentEditor();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) {
		editor->setInsertModeEnabled(on);
		return true;
	}
	if (session == nullptr) return false;
	session->insertMode = on;
	return true;
}

static std::string currentEditorLineText(MRFileEditor *editor) {
	std::string out;
	uint start;
	uint end;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return out;
		return session->document.lineText(session->cursorOffset);
	}
	start = editor->lineStartOffset(editor->cursorOffset());
	end = editor->lineEndOffset(editor->cursorOffset());
	out.reserve(end >= start ? end - start : 0);
	for (uint p = start; p < end; ++p)
		out.push_back(editor->charAtOffset(p));
	return out;
}

static std::string currentEditorWord(MRFileEditor *editor, const std::string &delimiters) {
	std::string out;
	uint pos;
	uint end;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return out;
		pos = static_cast<uint>(session->cursorOffset);
		end = static_cast<uint>(session->document.lineEnd(session->cursorOffset));
		while (pos < end) {
			char c = session->document.charAt(pos);
			if (delimiters.find(c) != std::string::npos) break;
			out.push_back(c);
			++pos;
		}
		session->cursorOffset = pos;
		session->clearSelection();
		enforceStringLength(out);
		return out;
	}
	pos = editor->cursorOffset();
	end = editor->lineEndOffset(pos);
	while (pos < end) {
		char c = editor->charAtOffset(pos);
		if (delimiters.find(c) != std::string::npos) break;
		out.push_back(c);
		pos = editor->nextCharOffset(pos);
	}
	editor->setCursorOffset(pos, 0);
	editor->revealCursor(True);
	enforceStringLength(out);
	return out;
}

static bool insertEditorText(MRFileEditor *editor, const std::string &text) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->insertBufferText(text);
	if (session == nullptr) return false;

	std::size_t start = session->cursorOffset;
	std::size_t end = start;
	if (session->hasSelection()) {
		start = session->selectionStart;
		end = session->selectionEnd;
	} else if (!session->insertMode) {
		std::size_t lineEnd = session->document.lineEnd(start);
		end = std::min(lineEnd, start + text.size());
	}
	return backgroundReplaceRange(mr::editor::Range(start, end), text, start + text.size());
}

static bool replaceEditorLine(MRFileEditor *editor, const std::string &text) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->replaceCurrentLineText(text);
	if (session == nullptr) return false;
	std::size_t start = session->document.lineStart(session->cursorOffset);
	std::size_t end = session->document.lineEnd(session->cursorOffset);
	return backgroundReplaceRange(mr::editor::Range(start, end), text, start);
}

static bool wordWrapEditorLine(MRFileEditor *editor) {
	MREditSetupSettings settings = configuredEditSetupSettings();
	std::string normalized;
	int leftMargin = settings.leftMargin;
	int rightMargin = settings.rightMargin;

	if (!normalizeEditFormatLine(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, normalized, &leftMargin, &rightMargin, nullptr)) {
		leftMargin = settings.leftMargin > 0 ? settings.leftMargin : 1;
		rightMargin = settings.rightMargin > 0 ? settings.rightMargin : 78;
	}

	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) {
		return editor->formatParagraph(leftMargin, rightMargin);
	}

	if (session == nullptr) return false;

	// In background sessions, WORD_WRAP_LINE is technically supported
	// but it is extremely complex to reimplement paragraph reformatting correctly via BackgroundEditSession methods.
	// For background safety we just break the current line if it's too long as a fallback.
	std::size_t cursor = session->cursorOffset;
	std::size_t start = session->document.lineStart(cursor);
	std::string line = session->document.lineText(cursor);

	if (line.length() <= static_cast<std::size_t>(rightMargin)) return true;

	std::size_t breakPos = static_cast<std::size_t>(rightMargin);
	while (breakPos > 0 && line[breakPos] != ' ' && line[breakPos] != '\t')
		breakPos--;

	if (breakPos == 0) breakPos = static_cast<std::size_t>(rightMargin);

	if (breakPos < line.length() && (line[breakPos] == ' ' || line[breakPos] == '\t')) {
		backgroundReplaceRange(mr::editor::Range(start + breakPos, start + breakPos + 1), "\n", start + breakPos + 1);
	} else {
		backgroundReplaceRange(mr::editor::Range(start + breakPos, start + breakPos), "\n", start + breakPos + 1);
	}

	return true;
}

static std::size_t prevCharOffsetFallback(const mr::editor::TextDocument &document, std::size_t pos) {
	if (pos == 0) return 0;
	if (pos > 1 && document.charAt(pos - 2) == '\r' && document.charAt(pos - 1) == '\n') return pos - 2;

	std::size_t step = 1;
	char lastChar = document.charAt(pos - 1);

	if ((lastChar & 0x80) == 0) {
		step = 1;
	} else if ((lastChar & 0xC0) == 0x80) {
		std::size_t maxCheck = std::min<std::size_t>(pos, 4);
		step = 1;
		for (std::size_t i = 1; i < maxCheck; ++i) {
			char ch = document.charAt(pos - 1 - i);
			if ((ch & 0xC0) != 0x80) {
				step = i + 1;
				break;
			}
		}
	}

	return pos - std::max<std::size_t>(step, 1);
}

static bool backspaceEditor(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	bool insertMode = currentEditorInsertMode();

	if (editor != nullptr) {
		std::size_t offset = editor->cursorOffset();
		std::size_t lineStart = editor->lineStartOffset(offset);
		if (offset == 0) return true;
		if (insertMode) {
			editor->setCursorOffset(editor->prevCharOffset(offset), 0);
			editor->deleteCharsAtCursor(1);
		} else {
			if (offset > lineStart) {
				editor->setCursorOffset(editor->prevCharOffset(offset), 0);
				editor->deleteCharsAtCursor(1);
				editor->insertBufferText(" ");
				editor->setCursorOffset(editor->prevCharOffset(editor->cursorOffset()), 0);
			} else {
				editor->setCursorOffset(editor->prevCharOffset(offset), 0);
			}
		}
		return true;
	}

	if (session == nullptr) return false;

	std::size_t offset = session->cursorOffset;
	std::size_t lineStart = session->document.lineStart(offset);
	if (offset == 0) return true;

	std::size_t target = prevCharOffsetFallback(session->document, offset);

	if (insertMode) {
		backgroundReplaceRange(mr::editor::Range(target, offset), std::string(), target);
	} else {
		if (offset > lineStart) {
			backgroundReplaceRange(mr::editor::Range(target, offset), " ", target);
		} else {
			backgroundSetCursor(target);
		}
	}
	return true;
}

static bool deleteEditorChars(MRFileEditor *editor, int count) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->deleteCharsAtCursor(count);
	if (session == nullptr) return false;
	if (count <= 0) return true;
	std::size_t start = session->cursorOffset;
	std::size_t end = std::min(session->document.length(), start + static_cast<std::size_t>(count));
	return backgroundReplaceRange(mr::editor::Range(start, end), std::string(), start);
}

static bool deleteEditorLine(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->deleteCurrentLineText();
	if (session == nullptr) return false;
	std::size_t start = session->document.lineStart(session->cursorOffset);
	std::size_t end = session->document.nextLine(session->cursorOffset);
	return backgroundReplaceRange(mr::editor::Range(start, end), std::string(), start);
}

static int currentEditorColumn(MRFileEditor *editor) {
	uint lineStart;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session != nullptr ? static_cast<int>(session->document.column(session->cursorOffset) + 1) : 1;
	if (editor->freeCursorMovementEnabled()) return editor->currentColumnNumber();
	lineStart = editor->lineStartOffset(editor->cursorOffset());
	return editor->charColumn(lineStart, editor->cursorOffset()) + 1;
}

static bool currentUiCursorPosition(int &x, int &y) {
	MRFileEditor *editor = currentEditor();
	if (editor != nullptr) {
		TRect viewport = editor->visibleTextViewportBounds();
		TPoint local = {static_cast<short>(viewport.a.x + editor->currentViewColumn() - 1), static_cast<short>(viewport.a.y + editor->currentViewRow() - 1)};
		TPoint point = editor->makeGlobal(local);
		x = point.x + 1;
		y = point.y + 1;
		return true;
	}
	if (TApplication *app = dynamic_cast<TApplication *>(TProgram::application); app != nullptr) {
		x = app->cursor.x + 1;
		y = app->cursor.y + 1;
		return true;
	}
	x = 0;
	y = 0;
	return false;
}

static int currentEditorLineNumber(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session != nullptr ? static_cast<int>(session->document.lineIndex(session->cursorOffset) + 1) : 1;
	return editor->currentLineNumber();
}

static std::size_t currentEditorCursorOffset(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->cursorOffset();
	return session != nullptr ? session->cursorOffset : 0;
}

static bool setEditorCursor(MRFileEditor *editor, uint target, int requestedVisualColumn = -1) {
	MREditWindow *win;
	if (editor == nullptr) return backgroundSetCursor(target);
	if (target > editor->bufferLength()) target = editor->bufferLength();
	if (requestedVisualColumn >= 0) editor->setCursorOffsetAtVisualColumn(target, requestedVisualColumn);
	else
		editor->setCursorOffset(target, 0);
	win = activeMacroEditWindow();
	if (win != nullptr && win->isBlockMarking()) win->refreshBlockVisual();
	else
		editor->revealCursor(True);
	return true;
}

static bool moveEditorLeft(MRFileEditor *editor) {
	uint start;
	uint target;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return false;
		start = static_cast<uint>(session->document.lineStart(session->cursorOffset));
		if (session->cursorOffset > start) target = static_cast<uint>(session->cursorOffset - 1);
		else if (start > 0)
			target = static_cast<uint>(session->document.lineEnd(session->document.prevLine(start)));
		else
			target = 0;
		return setEditorCursor(nullptr, target);
	}
	start = editor->lineStartOffset(editor->cursorOffset());
	if (editor->freeCursorMovementEnabled() && !editor->hasTextSelection() && editor->displayedCursorColumn() > editor->actualCursorVisualColumn(editor->cursorOffset()))
		return setEditorCursor(editor, editor->cursorOffset(), editor->displayedCursorColumn() - 1);
	if (editor->cursorOffset() > start) target = editor->prevCharOffset(editor->cursorOffset());
	else if (start > 0)
		target = editor->lineEndOffset(editor->prevLineOffset(start));
	else
		target = 0;
	return setEditorCursor(editor, target);
}

static bool moveEditorRight(MRFileEditor *editor) {
	uint lineEnd;
	uint target;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return false;
		lineEnd = static_cast<uint>(session->document.lineEnd(session->cursorOffset));
		if (session->cursorOffset < lineEnd) target = static_cast<uint>(std::min(session->document.length(), session->cursorOffset + 1));
		else
			target = static_cast<uint>(session->cursorOffset);
		return setEditorCursor(nullptr, target);
	}
	lineEnd = editor->lineEndOffset(editor->cursorOffset());
	if (editor->freeCursorMovementEnabled() && !editor->hasTextSelection() && editor->cursorOffset() == lineEnd)
		return setEditorCursor(editor, editor->cursorOffset(), editor->displayedCursorColumn() + 1);
	if (editor->cursorOffset() < lineEnd) target = editor->nextCharOffset(editor->cursorOffset());
	else
		target = editor->cursorOffset();
	return setEditorCursor(editor, target);
}

static bool moveEditorUp(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return false;
		return setEditorCursor(nullptr, static_cast<uint>(backgroundLineMoveOffset(session->cursorOffset, -1)));
	}
	return setEditorCursor(editor, editor->lineMoveOffset(editor->cursorOffset(), -1));
}

static bool moveEditorDown(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return false;
		return setEditorCursor(nullptr, static_cast<uint>(backgroundLineMoveOffset(session->cursorOffset, 1)));
	}
	return setEditorCursor(editor, editor->lineMoveOffset(editor->cursorOffset(), 1));
}

static bool moveEditorHome(MRFileEditor *editor) {
	uint start;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return false;
		start = static_cast<uint>(session->document.lineStart(session->cursorOffset));
		return setEditorCursor(nullptr, static_cast<uint>(backgroundCharPtrOffset(start, currentEditorIndentLevel() - 1)));
	}
	start = editor->lineStartOffset(editor->cursorOffset());
	return setEditorCursor(editor, editor->charPtrOffset(start, currentEditorIndentLevel() - 1));
}

static bool moveEditorEol(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session != nullptr ? setEditorCursor(nullptr, static_cast<uint>(session->document.lineEnd(session->cursorOffset))) : false;
	return setEditorCursor(editor, editor->lineEndOffset(editor->cursorOffset()));
}

static bool moveEditorTof(MRFileEditor *editor) {
	if (editor == nullptr) return currentBackgroundEditSession() != nullptr ? setEditorCursor(nullptr, 0) : false;
	return setEditorCursor(editor, 0);
}

static bool moveEditorEof(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session != nullptr ? setEditorCursor(nullptr, static_cast<uint>(session->document.length())) : false;
	return setEditorCursor(editor, editor->bufferLength());
}

static bool moveEditorWordLeft(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session != nullptr ? setEditorCursor(nullptr, static_cast<uint>(backgroundPrevWordOffset(session->cursorOffset))) : false;
	return setEditorCursor(editor, editor->prevWordOffset(editor->cursorOffset()));
}

static bool moveEditorWordRight(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session != nullptr ? setEditorCursor(nullptr, static_cast<uint>(backgroundNextWordOffset(session->cursorOffset))) : false;
	return setEditorCursor(editor, editor->nextWordOffset(editor->cursorOffset()));
}

static bool moveEditorFirstWord(MRFileEditor *editor) {
	uint pos;
	uint end;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return false;
		pos = static_cast<uint>(session->document.lineStart(session->cursorOffset));
		end = static_cast<uint>(session->document.lineEnd(session->cursorOffset));
		while (pos < end) {
			char c = session->document.charAt(pos);
			if (c != ' ' && c != '\t') break;
			++pos;
		}
		return setEditorCursor(nullptr, pos);
	}
	pos = editor->lineStartOffset(editor->cursorOffset());
	end = editor->lineEndOffset(editor->cursorOffset());
	while (pos < end) {
		char c = editor->charAtOffset(pos);
		if (c != ' ' && c != '	') break;
		pos = editor->nextCharOffset(pos);
	}
	return setEditorCursor(editor, pos);
}

static bool gotoEditorLine(MRFileEditor *editor, int lineNum) {
	uint pos = 0;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (lineNum < 1) return false;
	if (editor == nullptr) {
		if (session == nullptr) return false;
		pos = static_cast<uint>(session->document.lineStartByIndex(static_cast<std::size_t>(lineNum - 1)));
		return setEditorCursor(nullptr, pos);
	}
	for (int i = 1; i < lineNum && pos < editor->bufferLength(); ++i)
		pos = editor->nextLineOffset(pos);
	return setEditorCursor(editor, pos);
}

static bool gotoEditorCol(MRFileEditor *editor, int colNum) {
	uint start;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (colNum < 1) return false;
	if (editor == nullptr) {
		if (session == nullptr) return false;
		start = static_cast<uint>(session->document.lineStart(session->cursorOffset));
		return setEditorCursor(nullptr, static_cast<uint>(backgroundCharPtrOffset(start, colNum - 1)));
	}
	start = editor->lineStartOffset(editor->cursorOffset());
	return setEditorCursor(editor, editor->charPtrOffset(start, colNum - 1), colNum - 1);
}

static bool currentEditorAtEof(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session == nullptr || session->cursorOffset >= session->document.length();
	return editor->cursorOffset() >= editor->bufferLength();
}

static bool currentEditorAtEol(MRFileEditor *editor) {
	uint lineEnd;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session == nullptr || session->cursorOffset >= session->document.lineEnd(session->cursorOffset);
	lineEnd = editor->lineEndOffset(editor->cursorOffset());
	return editor->cursorOffset() >= lineEnd;
}

static int currentEditorRow(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session != nullptr ? static_cast<int>(session->document.lineIndex(session->cursorOffset) + 1) : 1;
	return editor->currentViewRow();
}

static int currentEditorPage(MRFileEditor *editor) {
	std::string text = snapshotEditorText(editor);
	std::size_t end = currentEditorCursorOffset(editor);
	std::size_t pos = 0;
	int page = 1;
	char pageBreak = configuredPageBreakCharacter();

	if (end > text.size()) end = text.size();

	while ((pos = text.find(pageBreak, pos)) != std::string::npos && pos < end) {
		++page;
		++pos;
	}
	return page;
}

static int currentEditorPageLine(MRFileEditor *editor) {
	std::string text = snapshotEditorText(editor);
	std::size_t end = currentEditorCursorOffset(editor);
	std::size_t pos = 0;
	std::size_t lastBreak = std::string::npos;
	char pageBreak = configuredPageBreakCharacter();
	int currentLine = currentEditorLineNumber(editor);

	if (end > text.size()) end = text.size();

	while ((pos = text.find(pageBreak, pos)) != std::string::npos && pos < end) {
		lastBreak = pos;
		++pos;
	}

	if (lastBreak == std::string::npos) return currentLine;

	return currentLine - lineIndexForPtr(editor, static_cast<uint>(lastBreak));
}

bool markEditorPosition(MREditWindow *win, MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return false;
		session->markStack.push_back(static_cast<uint>(session->cursorOffset));
		return true;
	}
	if (win == nullptr) return false;
	g_runtimeEnv.markStacks[win].push_back(editor->cursorOffset());
	return true;
}

static bool validRandomAccessMarkIndex(int index) noexcept {
	return index >= 1 && index <= 9;
}

bool gotoEditorMark(MREditWindow *win, MRFileEditor *editor) {
	std::map<const void *, std::vector<uint>>::iterator it;
	uint pos;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr || session->markStack.empty()) return false;
		pos = session->markStack.back();
		session->markStack.pop_back();
		return setEditorCursor(nullptr, pos);
	}
	if (win == nullptr) return false;
	it = g_runtimeEnv.markStacks.find(win);
	if (it == g_runtimeEnv.markStacks.end() || it->second.empty()) return false;
	pos = it->second.back();
	it->second.pop_back();
	return setEditorCursor(editor, pos);
}

static bool popEditorMark(MREditWindow *win) {
	std::map<const void *, std::vector<uint>>::iterator it;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (win == nullptr) {
		if (session == nullptr || session->markStack.empty()) return false;
		session->markStack.pop_back();
		return true;
	}
	it = g_runtimeEnv.markStacks.find(win);
	if (it == g_runtimeEnv.markStacks.end() || it->second.empty()) return false;
	it->second.pop_back();
	return true;
}

bool setEditorRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index) {
	BackgroundEditSession *session = currentBackgroundEditSession();

	if (!validRandomAccessMarkIndex(index)) return false;
	if (editor == nullptr) {
		if (session == nullptr) return false;
		session->randomAccessMarks[static_cast<std::size_t>(index)] = static_cast<uint>(session->cursorOffset);
		return true;
	}
	if (win == nullptr) return false;
	g_runtimeEnv.randomAccessMarks[win][static_cast<std::size_t>(index)] = editor->cursorOffset();
	return true;
}

bool gotoEditorRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	std::map<const void *, std::array<std::optional<uint>, 10>>::iterator it;

	if (!validRandomAccessMarkIndex(index)) return false;
	if (editor == nullptr) {
		if (session == nullptr) return false;
		const std::optional<uint> &pos = session->randomAccessMarks[static_cast<std::size_t>(index)];
		return pos.has_value() ? setEditorCursor(nullptr, *pos) : false;
	}
	if (win == nullptr) return false;
	it = g_runtimeEnv.randomAccessMarks.find(win);
	if (it == g_runtimeEnv.randomAccessMarks.end()) return false;
	const std::optional<uint> &pos = it->second[static_cast<std::size_t>(index)];
	return pos.has_value() ? setEditorCursor(editor, *pos) : false;
}

static bool moveEditorPageUp(MRFileEditor *editor) {
	int pageLines;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return false;
		pageLines = std::max(1, session->pageLines);
		return setEditorCursor(nullptr, static_cast<uint>(backgroundLineMoveOffset(session->cursorOffset, -pageLines)));
	}
	pageLines = std::max(1, editor->size.y - 1);
	return setEditorCursor(editor, editor->lineMoveOffset(editor->cursorOffset(), -pageLines));
}

static bool moveEditorPageDown(MRFileEditor *editor) {
	int pageLines;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return false;
		pageLines = std::max(1, session->pageLines);
		return setEditorCursor(nullptr, static_cast<uint>(backgroundLineMoveOffset(session->cursorOffset, pageLines)));
	}
	pageLines = std::max(1, editor->size.y - 1);
	return setEditorCursor(editor, editor->lineMoveOffset(editor->cursorOffset(), pageLines));
}

bool moveEditorNextPageBreak(MRFileEditor *editor) {
	std::string text;
	std::string::size_type pos;
	char pageBreak = configuredPageBreakCharacter();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return false;
		pos = static_cast<std::string::size_type>(session->cursorOffset);
		while (pos < session->document.length() && session->document.charAt(pos) != pageBreak)
			++pos;
		if (pos >= session->document.length()) return false;
		return setEditorCursor(nullptr, static_cast<uint>(session->document.nextLine(pos)));
	}
	text = snapshotEditorText(editor);
	pos = text.find(pageBreak, std::min<std::size_t>(editor->cursorOffset(), text.size()));
	if (pos == std::string::npos) return false;
	return setEditorCursor(editor, editor->nextLineOffset(static_cast<uint>(pos)));
}

bool moveEditorLastPageBreak(MRFileEditor *editor) {
	std::string text;
	std::string::size_type pos;
	std::size_t start;
	char pageBreak = configuredPageBreakCharacter();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return false;
		start = std::min<std::size_t>(session->cursorOffset, session->document.length());
		if (start == 0) return false;
		pos = start - 1;
		for (;;) {
			if (session->document.charAt(pos) == pageBreak) return setEditorCursor(nullptr, static_cast<uint>(session->document.nextLine(pos)));
			if (pos == 0) break;
			--pos;
		}
		return false;
	}
	text = snapshotEditorText(editor);
	start = std::min<std::size_t>(editor->cursorOffset(), text.size());
	if (start == 0) return false;
	pos = text.rfind(pageBreak, start - 1);
	if (pos == std::string::npos) return false;
	return setEditorCursor(editor, editor->nextLineOffset(static_cast<uint>(pos)));
}

static bool replaceEditorBuffer(MRFileEditor *editor, const std::string &text, std::size_t cursorPos) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->replaceWholeBuffer(text, cursorPos);
	if (session == nullptr) return false;
	return backgroundReplaceRange(mr::editor::Range(0, session->document.length()), text, cursorPos);
}

static int lineIndexForPtr(MRFileEditor *editor, uint ptr) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	uint pos = 0;
	int line = 0;
	if (editor == nullptr) {
		if (session == nullptr) return 0;
		return static_cast<int>(session->document.lineIndex(ptr));
	}
	if (ptr > editor->bufferLength()) ptr = editor->bufferLength();
	while (pos < ptr && pos < editor->bufferLength()) {
		uint next = editor->nextLineOffset(pos);
		if (next <= pos || next > ptr) break;
		pos = next;
		++line;
	}
	return line;
}

static int blockStatusValue(MREditWindow *win) {
	return win != nullptr ? win->blockStatus() : 0;
}

static bool blockMarkingValue(MREditWindow *win) {
	return win != nullptr && win->isBlockMarking();
}

static int blockLine1Value(MREditWindow *win, MRFileEditor *editor) {
	(void)editor;
	return win != nullptr ? win->blockLine1() : 0;
}

static int blockLine2Value(MREditWindow *win, MRFileEditor *editor) {
	(void)editor;
	return win != nullptr ? win->blockLine2() : 0;
}

static int blockCol1Value(MREditWindow *win, MRFileEditor *editor) {
	(void)editor;
	return win != nullptr ? win->blockCol1() : 0;
}

static int blockCol2Value(MREditWindow *win, MRFileEditor *editor) {
	(void)editor;
	return win != nullptr ? win->blockCol2() : 0;
}

bool beginCurrentBlockMode(int mode) {
	MREditWindow *win = activeMacroEditWindow();
	if (win == nullptr) return false;
	if (mode == MREditWindow::bmColumn) win->beginColumnBlock();
	else if (mode == MREditWindow::bmStream)
		win->beginStreamBlock();
	else
		win->beginLineBlock();
	return true;
}

bool endCurrentBlockMode() {
	MREditWindow *win = activeMacroEditWindow();
	if (win == nullptr) return false;
	win->endBlock();
	return true;
}

bool clearCurrentBlockMode() {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (session != nullptr) session->clearSelection();
	MREditWindow *win = activeMacroEditWindow();
	if (win != nullptr) win->clearBlock();
	return true;
}

struct EditWindowLookup {
	int targetIndex;
	int currentIndex;
	MREditWindow *result;

	EditWindowLookup() : targetIndex(0), currentIndex(0), result(nullptr) {
	}
};

static void collectEditWindowByIndex(TView *view, void *arg) {
	EditWindowLookup *lookup = static_cast<EditWindowLookup *>(arg);
	MREditWindow *win = dynamic_cast<MREditWindow *>(view);
	if (lookup == nullptr || win == nullptr || lookup->result != nullptr) return;
	++lookup->currentIndex;
	if (lookup->currentIndex == lookup->targetIndex) lookup->result = win;
}

MREditWindow *editWindowByIndex(int index) {
	EditWindowLookup lookup;
	if (index <= 0 || TProgram::deskTop == nullptr) return nullptr;
	lookup.targetIndex = index;
	TProgram::deskTop->forEach(collectEditWindowByIndex, &lookup);
	return lookup.result;
}

static void countEditWindowProc(TView *view, void *arg) {
	int *count = static_cast<int *>(arg);
	if (count != nullptr && dynamic_cast<MREditWindow *>(view) != nullptr) ++(*count);
}

static int countEditWindows() {
	int count = 0;
	if (TProgram::deskTop == nullptr) return 0;
	TProgram::deskTop->forEach(countEditWindowProc, &count);
	return count;
}

static void collectEditWindowsProc(TView *view, void *arg) {
	std::vector<MREditWindow *> *windows = static_cast<std::vector<MREditWindow *> *>(arg);
	MREditWindow *win = dynamic_cast<MREditWindow *>(view);
	if (windows != nullptr && win != nullptr) windows->push_back(win);
}

static std::vector<MREditWindow *> allEditWindows() {
	std::vector<MREditWindow *> windows;
	if (TProgram::deskTop != nullptr) TProgram::deskTop->forEach(collectEditWindowsProc, &windows);
	return windows;
}

static void cleanupWindowLinkGroups() {
	std::vector<MREditWindow *> windows = allEditWindows();
	std::set<const void *> live;
	std::map<int, int> counts;
	std::map<const void *, int>::iterator it;

	for (auto &window : windows)
		live.insert(window);

	for (it = g_runtimeEnv.windowLinkGroups.begin(); it != g_runtimeEnv.windowLinkGroups.end();) {
		if (live.find(it->first) == live.end()) it = g_runtimeEnv.windowLinkGroups.erase(it);
		else {
			++counts[it->second];
			++it;
		}
	}

	for (it = g_runtimeEnv.windowLinkGroups.begin(); it != g_runtimeEnv.windowLinkGroups.end();) {
		if (counts[it->second] < 2) it = g_runtimeEnv.windowLinkGroups.erase(it);
		else
			++it;
	}
}

static int windowLinkGroupOf(MREditWindow *win) {
	std::map<const void *, int>::const_iterator it;
	if (win == nullptr) return 0;
	cleanupWindowLinkGroups();
	it = g_runtimeEnv.windowLinkGroups.find(win);
	if (it == g_runtimeEnv.windowLinkGroups.end()) return 0;
	return it->second;
}

static bool isWindowLinked(MREditWindow *win) {
	return windowLinkGroupOf(win) != 0;
}

static int currentLinkStatus() {
	return isWindowLinked(activeMacroEditWindow()) ? 1 : 0;
}

static bool windowBufferIdentity(MREditWindow *win, std::string &fileName, std::string &text, bool &emptyUntitled) {
	MRFileEditor *editor;
	if (win == nullptr) return false;
	editor = win->getEditor();
	if (editor == nullptr) return false;
	fileName = win->currentFileName();
	text = snapshotEditorText(editor);
	emptyUntitled = fileName.empty() && text.empty();
	return true;
}

static bool copyWindowBufferState(MREditWindow *src, MREditWindow *dest) {
	MRFileEditor *srcEditor;
	MRFileEditor *destEditor;
	std::string text;
	std::size_t cursorPos;
	if (src == nullptr || dest == nullptr) return false;
	srcEditor = src->getEditor();
	destEditor = dest->getEditor();
	if (srcEditor == nullptr || destEditor == nullptr) return false;
	text = snapshotEditorText(srcEditor);
	cursorPos = std::min<std::size_t>(destEditor->cursorOffset(), text.size());
	if (!replaceEditorBuffer(destEditor, text, cursorPos)) return false;
	dest->setCurrentFileName(src->currentFileName());
	dest->setFileChanged(src->isFileChanged());
	return true;
}

static bool assignLinkedWindows(MREditWindow *a, MREditWindow *b) {
	int groupA;
	int groupB;
	int targetGroup;
	std::map<const void *, int>::iterator it;

	if (a == nullptr || b == nullptr || a == b) return false;

	cleanupWindowLinkGroups();
	groupA = windowLinkGroupOf(a);
	groupB = windowLinkGroupOf(b);
	if (groupA != 0 && groupA == groupB) return true;

	targetGroup = groupA != 0 ? groupA : groupB;
	if (targetGroup == 0) targetGroup = g_runtimeEnv.nextWindowLinkGroupId++;

	if (groupA != 0 && groupB != 0 && groupA != groupB) {
		for (it = g_runtimeEnv.windowLinkGroups.begin(); it != g_runtimeEnv.windowLinkGroups.end(); ++it)
			if (it->second == groupB) it->second = targetGroup;
	}

	g_runtimeEnv.windowLinkGroups[a] = targetGroup;
	g_runtimeEnv.windowLinkGroups[b] = targetGroup;
	cleanupWindowLinkGroups();
	return true;
}

static MREditWindow *selectLinkTargetWindow(MREditWindow *current) {
	return mrShowWindowListDialog(mrwlSelectLinkTarget, current);
}

static bool prepareWindowLink(MREditWindow *current, MREditWindow *target, MREditWindow *&source, MREditWindow *&dest) {
	std::string currentFile;
	std::string currentText;
	std::string targetFile;
	std::string targetText;
	bool currentEmptyUntitled = false;
	bool targetEmptyUntitled = false;

	if (current == nullptr || target == nullptr || current == target) return false;
	if (!windowBufferIdentity(current, currentFile, currentText, currentEmptyUntitled) || !windowBufferIdentity(target, targetFile, targetText, targetEmptyUntitled)) return false;

	if (!currentEmptyUntitled && !targetEmptyUntitled) {
		if (currentFile != targetFile || currentText != targetText) return false;
		source = current;
		dest = target;
	} else if (currentEmptyUntitled && !targetEmptyUntitled) {
		source = target;
		dest = current;
	} else {
		source = current;
		dest = target;
	}
	return true;
}

bool linkCurrentEditWindow() {
	MREditWindow *current = activeMacroEditWindow();
	MREditWindow *target;
	MREditWindow *source = nullptr;
	MREditWindow *dest = nullptr;

	if (current == nullptr) return false;
	target = selectLinkTargetWindow(current);
	if (target == nullptr) return false;
	if (!prepareWindowLink(current, target, source, dest)) return false;
	if (source != dest && !copyWindowBufferState(source, dest)) return false;
	if (!assignLinkedWindows(current, target)) return false;
	syncLinkedWindowsFrom(source);
	return true;
}

bool unlinkCurrentEditWindow() {
	MREditWindow *current = activeMacroEditWindow();
	if (current == nullptr) return false;
	cleanupWindowLinkGroups();
	g_runtimeEnv.windowLinkGroups.erase(current);
	cleanupWindowLinkGroups();
	return true;
}

static void syncLinkedWindowsFrom(MREditWindow *source) {
	std::vector<MREditWindow *> windows = allEditWindows();
	int group;
	if (source == nullptr) return;
	group = windowLinkGroupOf(source);
	if (group == 0) return;
	for (auto &window : windows) {
		if (window == source) continue;
		if (windowLinkGroupOf(window) == group) copyWindowBufferState(source, window);
	}
}

bool redrawCurrentEditWindow() {
	MREditWindow *win = activeMacroEditWindow();
	MRFileEditor *editor = currentEditor();
	if (win == nullptr) return false;
	if (editor != nullptr) editor->refreshViewState();
	win->drawView();
	return true;
}

bool redrawEntireScreen() {
	std::vector<MREditWindow *> windows = allEditWindows();
	if (TProgram::deskTop == nullptr) return false;
	TProgram::deskTop->drawView();
	for (auto &window : windows)
		window->drawView();
	return true;
}

bool zoomCurrentEditWindow() {
	MREditWindow *win = activeMacroEditWindow();
	if (win == nullptr) return false;
	message(win, evCommand, cmZoom, nullptr);
	return true;
}

struct CurrentEditWindowIndexLookup {
	MREditWindow *current;
	int index;
	int result;
};

static void currentEditWindowIndexProc(TView *view, void *arg) {
	CurrentEditWindowIndexLookup *lookup = static_cast<CurrentEditWindowIndexLookup *>(arg);
	MREditWindow *win = dynamic_cast<MREditWindow *>(view);
	if (lookup == nullptr || win == nullptr || lookup->result != 0) return;
	++lookup->index;
	if (win == lookup->current) lookup->result = lookup->index;
}

static int currentEditWindowIndex() {
	CurrentEditWindowIndexLookup lookup;
	if (TProgram::deskTop == nullptr) return 0;
	lookup.current = activeMacroEditWindow();
	lookup.index = 0;
	lookup.result = 0;
	if (lookup.current == nullptr) return 0;
	TProgram::deskTop->forEach(currentEditWindowIndexProc, &lookup);
	return lookup.result;
}

static bool currentWindowGeometry(int &x1, int &y1, int &x2, int &y2) {
	MREditWindow *win = activeMacroEditWindow();
	TRect bounds;
	if (win == nullptr) return false;
	bounds = win->getBounds();
	x1 = bounds.a.x + 1;
	y1 = bounds.a.y + 1;
	x2 = bounds.b.x;
	y2 = bounds.b.y;
	return true;
}

static int encodeIndentStyle(const std::string &style) {
	const std::string key = upperKey(style);
	if (key == "AUTOMATIC") return 1;
	if (key == "SMART") return 2;
	return 0;
}

static std::string decodeIndentStyle(int value) {
	switch (value) {
		case 1:
			return "AUTOMATIC";
		case 2:
			return "SMART";
		default:
			return "OFF";
	}
}

static int encodeBackupMode(const std::string &method) {
	const std::string key = upperKey(method);
	if (key == "BAK_FILE") return 1;
	if (key == "DIRECTORY") return 2;
	return 0;
}

static std::string defaultFormatLineValue() {
	if (!g_runtimeEnv.defaultFormat.empty()) return g_runtimeEnv.defaultFormat;
	return resolveEditSetupDefaults().formatLine;
}

static int readWindowColorValue(std::size_t index) {
	const MRColorSetupSettings colors = configuredColorSetupSettings();
	if (index >= colors.windowColors.size()) return 0;
	return colors.windowColors[index];
}

static int readMenuDialogColorValue(std::size_t index) {
	const MRColorSetupSettings colors = configuredColorSetupSettings();
	if (index >= colors.menuDialogColors.size()) return 0;
	return colors.menuDialogColors[index];
}

static int readOtherColorValue(std::size_t index) {
	const MRColorSetupSettings colors = configuredColorSetupSettings();
	if (index >= colors.otherColors.size()) return 0;
	return colors.otherColors[index];
}

static bool writeWindowColorValue(std::size_t index, int value) {
	MRColorSetupSettings colors = configuredColorSetupSettings();
	if (index >= colors.windowColors.size()) return false;
	colors.windowColors[index] = static_cast<unsigned char>(std::clamp(value, 0, 255));
	return setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, colors.windowColors.data(), colors.windowColors.size(), nullptr);
}

static bool writeMenuDialogColorValue(std::size_t index, int value) {
	MRColorSetupSettings colors = configuredColorSetupSettings();
	if (index >= colors.menuDialogColors.size()) return false;
	colors.menuDialogColors[index] = static_cast<unsigned char>(std::clamp(value, 0, 255));
	return setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, colors.menuDialogColors.data(), colors.menuDialogColors.size(), nullptr);
}

static bool writeOtherColorValue(std::size_t index, int value) {
	MRColorSetupSettings colors = configuredColorSetupSettings();
	if (index >= colors.otherColors.size()) return false;
	colors.otherColors[index] = static_cast<unsigned char>(std::clamp(value, 0, 255));
	return setConfiguredColorSetupGroupValues(MRColorSetupGroup::Other, colors.otherColors.data(), colors.otherColors.size(), nullptr);
}

static int currentStatusRowValue() {
	if (g_runtimeEnv.statusRow >= 0) return g_runtimeEnv.statusRow;
	if (TProgram::statusLine == nullptr) return 0;
	return TProgram::statusLine->getBounds().a.y + 1;
}

static int currentMessageRowValue() {
	if (g_runtimeEnv.messageRow >= 0) return g_runtimeEnv.messageRow;
	if (TProgram::menuBar == nullptr) return 0;
	return TProgram::menuBar->getBounds().a.y + 1;
}

static int currentMaxWindowRowValue() {
	if (g_runtimeEnv.maxWindowRow >= 0) return g_runtimeEnv.maxWindowRow;
	if (TProgram::deskTop == nullptr) return 0;
	return TProgram::deskTop->getBounds().b.y;
}

static int currentMinWindowRowValue() {
	if (g_runtimeEnv.minWindowRow >= 0) return g_runtimeEnv.minWindowRow;
	if (TProgram::deskTop == nullptr) return 0;
	return TProgram::deskTop->getBounds().a.y + 1;
}

static int currentWindowAttrValue() {
	MREditWindow *win = activeMacroEditWindow();
	int value = 0;
	if (win == nullptr) return 0;
	if (isWindowManuallyHidden(win) || (win->state & sfVisible) == 0) value |= 0x01;
	return value;
}

static bool setCurrentWindowAttrValue(int value) {
	MREditWindow *win = activeMacroEditWindow();
	const bool hidden = (value & 0x01) != 0;
	if (win == nullptr || TProgram::deskTop == nullptr) return false;
	setWindowManuallyHidden(win, hidden);
	if (hidden) {
		if ((win->state & sfVisible) != 0) win->hide();
		return true;
	}
	if ((win->state & sfVisible) == 0) win->show();
	TProgram::deskTop->setCurrent(win, TView::normalSelect);
	return true;
}

bool createEditWindow() {
	MREditWindow *win;

	win = createEditorWindow("?No-File?");
	if (win == nullptr || TProgram::deskTop == nullptr) return false;
	TProgram::deskTop->setCurrent(win, TView::normalSelect);
	return true;
}

bool switchEditWindow(int index) {
	int count;
	MREditWindow *win;
	if (TProgram::deskTop == nullptr) return false;
	count = countEditWindows();
	if (count <= 0) return false;
	if (index <= 0) index = 1;
	if (index > count) index = ((index - 1) % count) + 1;
	win = editWindowByIndex(index);
	if (win == nullptr) return false;
	TProgram::deskTop->setCurrent(win, TView::normalSelect);
	return true;
}

bool sizeCurrentEditWindow(int x1, int y1, int x2, int y2) {
	MREditWindow *win = activeMacroEditWindow();
	TRect desk;
	TRect bounds;
	if (win == nullptr || TProgram::deskTop == nullptr) return false;
	if (x2 < x1 || y2 < y1) return false;
	desk = TProgram::deskTop->getExtent();
	x1 = std::max(1, x1);
	y1 = std::max(1, y1);
	x2 = std::min(desk.b.x, x2);
	y2 = std::min(desk.b.y, y2);
	if (x2 <= x1) x2 = std::min(desk.b.x, x1 + 3);
	if (y2 <= y1) y2 = std::min(desk.b.y, y1 + 3);
	bounds = TRect(x1 - 1, y1 - 1, x2, y2);
	win->changeBounds(bounds);
	win->drawView();
	return true;
}

bool deleteCurrentEditWindow() {
	MREditWindow *win = activeMacroEditWindow();
	if (win == nullptr) return false;
	win->close();
	return true;
}

bool eraseCurrentEditWindow() {
	MREditWindow *win = activeMacroEditWindow();
	MRFileEditor *editor = currentEditor();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr && session == nullptr) return false;
	if (!replaceEditorBuffer(editor, std::string(), 0)) return false;
	if (win != nullptr) {
		win->clearBlock();
		win->setCurrentFileName("");
		win->setFileChanged(false);
	} else if (session != nullptr) {
		session->blockMode = 0;
		session->blockMarkingOn = false;
		session->blockAnchor = 0;
		session->blockEnd = 0;
		session->fileName.clear();
		session->fileChanged = false;
		session->clearSelection();
		session->clearLastSearch();
	}
	return true;
}

bool modifyCurrentEditWindow() {
	MREditWindow *win = activeMacroEditWindow();
	if (win == nullptr) return false;
	message(win, evCommand, cmResize, nullptr);
	return true;
}

static bool queueDeferredUiProcedureImpl(const std::string &name, const std::vector<Value> &args, int &errorCode) {
	BackgroundEditSession *session = currentBackgroundEditSession();

	errorCode = 0;
	if (session == nullptr) return false;

	if (name == "MARQUEE" || name == "MARQUEE_WARNING" || name == "MARQUEE_ERROR") {
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error(name + " expects one string argument.");
		if (name == "MARQUEE") session->deferredUiCommands.emplace_back(mrducMarqueeInfo, 0, 0, 0, 0, 0, 0, 0, 0, valueAsString(args[0]));
		else if (name == "MARQUEE_WARNING")
			session->deferredUiCommands.emplace_back(mrducMarqueeWarning, 0, 0, 0, 0, 0, 0, 0, 0, valueAsString(args[0]));
		else
			session->deferredUiCommands.emplace_back(mrducMarqueeError, 0, 0, 0, 0, 0, 0, 0, 0, valueAsString(args[0]));
		return true;
	}
	if (name == "MAKE_MESSAGE") {
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("MAKE_MESSAGE expects one string argument.");
		session->deferredUiCommands.emplace_back(mrducMakeMessage, 0, 0, 0, 0, 0, 0, 0, 0, valueAsString(args[0]));
		return true;
	}
	if (name == "WORKING") {
		if (!args.empty()) throw std::runtime_error("WORKING expects no arguments.");
		session->deferredUiCommands.emplace_back(mrducMarqueeWarning, 0, 0, 0, 0, 0, 0, 0, 0, kMacroWorkingMessageText);
		return true;
	}
	if (name == "BRAIN") {
		if (args.size() != 1 || !isNumeric(args[0])) throw std::runtime_error("BRAIN expects one integer argument.");
		session->deferredUiCommands.emplace_back(mrducBrain, valueAsInt(args[0]) != 0 ? 1 : 0);
		return true;
	}
	if (name == "REGISTER_MENU_ITEM" || name == "REMOVE_MENU_ITEM") throw std::runtime_error(name + " is not allowed during staged background execution.");

	if (name == "PUT_BOX") {
		if (args.size() != 8 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT || args[5].type != TYPE_INT || !isStringLike(args[6]) || args[7].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int, int, int, int, int, string, int).");
		session->deferredUiCommands.emplace_back(mrducPutBox, valueAsInt(args[0]), valueAsInt(args[1]), valueAsInt(args[2]), valueAsInt(args[3]), valueAsInt(args[4]), valueAsInt(args[5]), valueAsInt(args[7]), 0, valueAsString(args[6]));
		return true;
	}
	if (name == "WRITE") {
		if (args.size() != 5 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT) throw std::runtime_error(name + " expects (string, int, int, int, int).");
		session->deferredUiCommands.emplace_back(mrducWrite, valueAsInt(args[1]), valueAsInt(args[2]), valueAsInt(args[3]), valueAsInt(args[4]), 0, 0, 0, 0, valueAsString(args[0]));
		return true;
	}
	if (name == "CLR_LINE") {
		if (args.empty()) {
			session->deferredUiCommands.emplace_back(mrducClrLine);
			return true;
		}
		if (args.size() != 3 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("CLR_LINE expects no arguments or (int, int, int).");
		session->deferredUiCommands.emplace_back(mrducClrLine, valueAsInt(args[0]), valueAsInt(args[1]), valueAsInt(args[2]));
		return true;
	}
	if (name == "GOTOXY") {
		int x;
		int y;
		if (args.size() != 2 || args[0].type != TYPE_INT || args[1].type != TYPE_INT) throw std::runtime_error("GOTOXY expects (int, int).");
		x = valueAsInt(args[0]);
		y = valueAsInt(args[1]);
		if (session->screenWidth > 0) x = std::max(1, std::min(x, session->screenWidth));
		if (session->screenHeight > 0) y = std::max(1, std::min(y, session->screenHeight));
		session->screenCursorX = x;
		session->screenCursorY = y;
		session->deferredUiCommands.emplace_back(mrducGotoxy, x, y);
		return true;
	}
	if (name == "PUT_LINE_NUM") {
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("PUT_LINE_NUM expects one integer argument.");
		session->deferredUiCommands.emplace_back(mrducPutLineNum, valueAsInt(args[0]));
		return true;
	}
	if (name == "PUT_COL_NUM") {
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("PUT_COL_NUM expects one integer argument.");
		session->deferredUiCommands.emplace_back(mrducPutColNum, valueAsInt(args[0]));
		return true;
	}
	if (name == "SCROLL_BOX_UP" || name == "SCROLL_BOX_DN") {
		if (args.size() != 5 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int, int, int, int).");
		session->deferredUiCommands.emplace_back(name == "SCROLL_BOX_UP" ? mrducScrollBoxUp : mrducScrollBoxDn, valueAsInt(args[0]), valueAsInt(args[1]), valueAsInt(args[2]), valueAsInt(args[3]), valueAsInt(args[4]), 0, 0, 0);
		return true;
	}
	if (name == "CLEAR_SCREEN") {
		if (!(args.empty() || (args.size() == 1 && args[0].type == TYPE_INT))) throw std::runtime_error("CLEAR_SCREEN expects no arguments or one integer argument.");
		session->screenCursorX = 1;
		session->screenCursorY = 1;
		session->deferredUiCommands.emplace_back(mrducClearScreen, args.empty() ? 0x07 : valueAsInt(args[0]));
		return true;
	}
	if (name == "KILL_BOX") {
		if (!args.empty()) throw std::runtime_error("KILL_BOX expects no arguments.");
		session->deferredUiCommands.emplace_back(mrducKillBox);
		return true;
	}

	if (name == "CREATE_WINDOW") {
		session->deferredUiCommands.emplace_back(mrducCreateWindow);
		if (session->windowCount < std::numeric_limits<int>::max()) ++session->windowCount;
		session->currentWindow = std::max(1, session->windowCount);
		session->linkStatus = 0;
		return true;
	}
	if (name == "DELETE_WINDOW") {
		session->deferredUiCommands.emplace_back(mrducDeleteWindow);
		if (session->windowCount > 0) --session->windowCount;
		if (session->windowCount <= 0) {
			session->windowCount = 0;
			session->currentWindow = 0;
			session->linkStatus = 0;
		} else if (session->currentWindow > session->windowCount)
			session->currentWindow = session->windowCount;
		return true;
	}
	if (name == "MODIFY_WINDOW") {
		session->deferredUiCommands.emplace_back(mrducModifyWindow);
		return true;
	}
	if (name == "LINK_WINDOW") {
		session->deferredUiCommands.emplace_back(mrducLinkWindow);
		session->linkStatus = 1;
		return true;
	}
	if (name == "UNLINK_WINDOW") {
		session->deferredUiCommands.emplace_back(mrducUnlinkWindow);
		session->linkStatus = 0;
		return true;
	}
	if (name == "ZOOM") {
		session->deferredUiCommands.emplace_back(mrducZoom);
		return true;
	}
	if (name == "REDRAW") {
		session->deferredUiCommands.emplace_back(mrducRedraw);
		return true;
	}
	if (name == "NEW_SCREEN") {
		session->deferredUiCommands.emplace_back(mrducNewScreen);
		return true;
	}
	if (name == "SWITCH_WINDOW") {
		int index;
		int count;
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("SWITCH_WINDOW expects one integer argument.");
		index = valueAsInt(args[0]);
		count = session->windowCount;
		if (count > 0) {
			if (index <= 0) index = 1;
			if (index > count) index = ((index - 1) % count) + 1;
			session->currentWindow = index;
		}
		session->deferredUiCommands.emplace_back(mrducSwitchWindow, index);
		return true;
	}
	if (name == "SIZE_WINDOW") {
		int x1;
		int y1;
		int x2;
		int y2;
		if (args.size() != 4 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT) throw std::runtime_error("SIZE_WINDOW expects four integer arguments.");
		x1 = valueAsInt(args[0]);
		y1 = valueAsInt(args[1]);
		x2 = valueAsInt(args[2]);
		y2 = valueAsInt(args[3]);
		session->windowGeometryValid = true;
		session->windowX1 = x1;
		session->windowY1 = y1;
		session->windowX2 = x2;
		session->windowY2 = y2;
		session->deferredUiCommands.emplace_back(mrducSizeWindow, x1, y1, x2, y2);
		return true;
	}
	return false;
}

static bool enqueueDeferredUiCommandImpl(const MRMacroDeferredUiCommand &command, int &errorCode) {
	BackgroundEditSession *session = currentBackgroundEditSession();

	errorCode = 0;
	if (session == nullptr) return false;
	session->deferredUiCommands.push_back(command);
	return true;
}


bool moveEditorTabRight(MRFileEditor *editor) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	int col;
	int targetCol;
	uint lineStart;
	bool tabExpand = currentRuntimeTabExpand();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr && session == nullptr) return false;
	col = currentEditorColumn(editor);
	targetCol = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, col);
	if (targetCol <= col) return false;
	if (currentEditorInsertMode()) {
		return insertEditorText(editor, buildEditIndentFill(settings, col, targetCol, tabExpand));
	}
	if (editor == nullptr && tabExpand) {
		return insertEditorText(nullptr, buildEditIndentFill(settings, col, targetCol, true));
	}
	if (editor == nullptr) {
		lineStart = static_cast<uint>(session->document.lineStart(session->cursorOffset));
		return setEditorCursor(nullptr, static_cast<uint>(backgroundCharPtrOffset(lineStart, targetCol - 1)));
	}
	lineStart = editor->lineStartOffset(editor->cursorOffset());
	return setEditorCursor(editor, editor->charPtrOffset(lineStart, targetCol - 1));
}

bool moveEditorTabLeft(MRFileEditor *editor) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const int currentColumn = currentEditorColumn(editor);
	uint lineStart;
	const int targetCol = prevResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (targetCol >= currentColumn) return false;
	if (editor == nullptr) {
		if (session == nullptr) return false;
		lineStart = static_cast<uint>(session->document.lineStart(session->cursorOffset));
		return setEditorCursor(nullptr, static_cast<uint>(backgroundCharPtrOffset(lineStart, targetCol - 1)));
	}
	lineStart = editor->lineStartOffset(editor->cursorOffset());
	return setEditorCursor(editor, editor->charPtrOffset(lineStart, targetCol - 1));
}

bool indentEditor(MRFileEditor *editor) {
	if (!moveEditorTabRight(editor)) return false;
	return setCurrentEditorIndentLevel(currentEditorColumn(editor));
}

bool undentEditor(MRFileEditor *editor) {
	if (!moveEditorTabLeft(editor)) return false;
	return setCurrentEditorIndentLevel(currentEditorColumn(editor));
}

static bool carriageReturnEditor(MRFileEditor *editor) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const int indentLevel = resolvedEditFormatIndentColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentEditorIndentLevel());
	const std::string fill = buildEditIndentFill(settings, 1, indentLevel, currentRuntimeTabExpand());
	if (editor != nullptr) return editor->newLineWithIndent(fill);
	return insertEditorText(nullptr, std::string("\n") + fill);
}

static std::string formatCurrentDate() {
	char buf[32];
	std::time_t now = std::time(nullptr);
	std::tm *tmv = std::localtime(&now);
	if (tmv == nullptr) return std::string();
	std::strftime(buf, sizeof(buf), "%m/%d/%y", tmv);
	return std::string(buf);
}

static std::string formatCurrentTime() {
	char buf[32];
	std::time_t now = std::time(nullptr);
	std::tm *tmv = std::localtime(&now);
	if (tmv == nullptr) return std::string();
	std::strftime(buf, sizeof(buf), "%I:%M:%S%p", tmv);
	std::string out(buf);
	for (char &i : out)
		i = static_cast<char>(std::tolower(static_cast<unsigned char>(i)));
	return out;
}

static Value loadSpecialVariable(const std::string &name, bool &handled) {
	std::string key = upperKey(name);
	handled = true;
	if (key == "RETURN_INT") return makeInt(runtimeReturnInt());
	if (key == "RETURN_STR") return makeString(runtimeReturnStr());
	if (key == "ERROR_LEVEL") return makeInt(runtimeErrorLevel());
	if (key == "IGNORE_CASE") return makeInt(currentRuntimeIgnoreCase() ? 1 : 0);
	if (key == "REG_EXP_STAT") return makeInt(currentRegexStatusValue());
	if (key == "TAB_EXPAND") return makeInt(currentRuntimeTabExpand() ? 1 : 0);
	if (key == "DISPLAY_TABS") return makeInt(configuredDisplayTabsSetting() ? 1 : 0);
	if (key == "SHADOW_CHAR") return makeInt(g_runtimeEnv.shadowChar);
	if (key == "REFRESH") return makeInt(g_runtimeEnv.refresh);
	if (key == "MESSAGES") return makeInt(configuredMenulineMessages() ? 1 : 0);
	if (key == "MOUSE") return makeInt(g_runtimeEnv.mouse);
	if (key == "LOGO_SCREEN") return makeInt(g_runtimeEnv.logoScreen);
	if (key == "EXPLOSIONS") return makeInt(g_runtimeEnv.explosions);
	if (key == "TRUNCATE_SPACES") return makeInt(configuredEditSetupSettings().truncateSpaces ? 1 : 0);
	if (key == "BACKUPS") {
		const MREditSetupSettings settings = configuredEditSetupSettings();
		if (!settings.backupFiles) return makeInt(0);
		return makeInt(encodeBackupMode(settings.backupMethod));
	}
	if (key == "AUTOSAVE") {
		const MREditSetupSettings settings = configuredEditSetupSettings();
		return makeInt((settings.autosaveInactivitySeconds > 0 || settings.autosaveIntervalSeconds > 0) ? 1 : 0);
	}
	if (key == "UNDO_STAT") return makeInt(g_runtimeEnv.undoStat);
	if (key == "FORMAT_STAT") return makeInt(g_runtimeEnv.formatStat);
	if (key == "WRAP_STAT") return makeInt(configuredEditSetupSettings().wordWrap ? 1 : 0);
	if (key == "MEM_ALLOC") return makeInt(g_runtimeEnv.memAlloc);
	if (key == "LEFT_MARGIN") return makeInt(configuredEditSetupSettings().leftMargin);
	if (key == "RIGHT_MARGIN") return makeInt(configuredEditSetupSettings().rightMargin);
	if (key == "FORMAT_RULER") return makeInt(configuredEditSetupSettings().formatRuler ? 1 : 0);
	if (key == "INDENT_STYLE") return makeInt(encodeIndentStyle(configuredEditSetupSettings().indentStyle));
	if (key == "INS_CURSOR") return makeInt(g_runtimeEnv.insCursor);
	if (key == "OVR_CURSOR") return makeInt(g_runtimeEnv.ovrCursor);
	if (key == "CTRL_HELP") return makeInt(g_runtimeEnv.ctrlHelp);
	if (key == "MOUSE_H_SENSE") return makeInt(g_runtimeEnv.mouseHSense);
	if (key == "MOUSE_V_SENSE") return makeInt(g_runtimeEnv.mouseVSense);
	if (key == "WINDOW_ATTR") return makeInt(currentWindowAttrValue());
	if (key == "TEXT_COLOR") return makeInt(readWindowColorValue(0));
	if (key == "CHANGE_COLOR") return makeInt(readWindowColorValue(1));
	if (key == "BACK_COLOR") return makeInt(readOtherColorValue(9));
	if (key == "MENU_COLOR") return makeInt(readMenuDialogColorValue(0));
	if (key == "STAT_COLOR") return makeInt(readOtherColorValue(0));
	if (key == "ERROR_COLOR") return makeInt(readOtherColorValue(4));
	if (key == "SHADOW_COLOR") return makeInt(readMenuDialogColorValue(7));
	if (key == "STATUS_ROW") return makeInt(currentStatusRowValue());
	if (key == "MESSAGE_ROW") return makeInt(currentMessageRowValue());
	if (key == "MAX_WINDOW_ROW") return makeInt(currentMaxWindowRowValue());
	if (key == "MIN_WINDOW_ROW") return makeInt(currentMinWindowRowValue());
	if (key == "NAME_LINE") return makeInt(g_runtimeEnv.nameLine);
	if (key == "INSERT_MODE") return makeInt(currentEditorInsertMode() ? 1 : 0);
	if (key == "INDENT_LEVEL") return makeInt(currentEditorIndentLevel());
	if (key == "MPARM_STR") return makeString(runtimeParameterString());
	if (key == "DATE") return makeString(formatCurrentDate());
	if (key == "TIME") return makeString(formatCurrentTime());
	if (key == "COMSPEC") return makeString(g_runtimeEnv.shellPath);
	if (key == "TEMP_PATH") return makeString(configuredTempDirectoryPath());
	if (key == "MR_PATH") return makeString(g_runtimeEnv.executableDir);
	if (key == "OS_VERSION") return makeString(g_runtimeEnv.shellVersion);
	if (key == "PARAM_COUNT") return makeInt(static_cast<int>(g_runtimeEnv.processArgs.size()));
	if (key == "CPU") return makeInt(detectCpuCode());
	if (key == "DOC_MODE") return makeInt(g_runtimeEnv.docMode);
	if (key == "PRINT_MARGIN") return makeInt(g_runtimeEnv.printMargin);
	if (key == "C_COL") return makeInt(currentEditorColumn(currentEditor()));
	if (key == "C_LINE") return makeInt(currentEditorLineNumber(currentEditor()));
	if (key == "C_ROW") return makeInt(currentEditorRow(currentEditor()));
	if (key == "C_PAGE") return makeInt(currentEditorPage(currentEditor()));
	if (key == "PG_LINE") return makeInt(currentEditorPageLine(currentEditor()));
	if (key == "AT_EOF") return makeInt(currentEditorAtEof(currentEditor()) ? 1 : 0);
	if (key == "AT_EOL") return makeInt(currentEditorAtEol(currentEditor()) ? 1 : 0);
	if (key == "CUR_WINDOW") return makeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->currentWindow : currentEditWindowIndex());
	if (key == "LINK_STAT") return makeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->linkStatus : currentLinkStatus());
	if (key == "WINDOW_COUNT") return makeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->windowCount : countEditWindows());
	if (key == "KEY1") return makeInt(g_runtimeEnv.key1);
	if (key == "KEY2") return makeInt(g_runtimeEnv.key2);
	if (key == "LAST_FILE_ATTR" || key == "LAST_FILE_SIZE" || key == "LAST_FILE_TIME") {
		int attr = 0;
		int size = 0;
		int packedTime = 0;
		if (!readFileMetadata(g_runtimeEnv.lastFileName, &attr, &size, &packedTime)) return makeInt(0);
		if (key == "LAST_FILE_ATTR") return makeInt(attr);
		if (key == "LAST_FILE_SIZE") return makeInt(size);
		return makeInt(packedTime);
	}
	if (key == "VIRTUAL_DESKTOPS") return makeInt(configuredVirtualDesktops());
	if (key == "CYCLIC_VIRTUAL_DESKTOPS") return makeInt(configuredCyclicVirtualDesktops() ? 1 : 0);
	if (key == "WIN_X1" || key == "WIN_Y1" || key == "WIN_X2" || key == "WIN_Y2") {
		BackgroundEditSession *session = currentBackgroundEditSession();
		int x1;
		int y1;
		int x2;
		int y2;
		if (session != nullptr) {
			if (!session->windowGeometryValid) return makeInt(0);
			x1 = session->windowX1;
			y1 = session->windowY1;
			x2 = session->windowX2;
			y2 = session->windowY2;
		} else {
			if (!currentWindowGeometry(x1, y1, x2, y2)) return makeInt(0);
		}
		if (key == "WIN_X1") return makeInt(x1);
		if (key == "WIN_Y1") return makeInt(y1);
		if (key == "WIN_X2") return makeInt(x2);
		return makeInt(y2);
	}
	if (key == "BLOCK_STAT") {
		MREditWindow *win = activeMacroEditWindow();
		return makeInt(blockStatusValue(win));
	}
	if (key == "BLOCK_LINE1") {
		MREditWindow *win = activeMacroEditWindow();
		return makeInt(blockLine1Value(win, currentEditor()));
	}
	if (key == "BLOCK_LINE2") {
		MREditWindow *win = activeMacroEditWindow();
		return makeInt(blockLine2Value(win, currentEditor()));
	}
	if (key == "BLOCK_COL1") {
		MREditWindow *win = activeMacroEditWindow();
		return makeInt(blockCol1Value(win, currentEditor()));
	}
	if (key == "BLOCK_COL2") {
		MREditWindow *win = activeMacroEditWindow();
		return makeInt(blockCol2Value(win, currentEditor()));
	}
	if (key == "MARKING") {
		MREditWindow *win = activeMacroEditWindow();
		return makeInt(blockMarkingValue(win) ? 1 : 0);
	}
	if (key == "LAST_FILE_NAME") return makeString(g_runtimeEnv.lastFileName);
	if (key == "FOUND_STR" || key == "SEARCH_FILE" || key == "FOUND_X" || key == "FOUND_Y") {
		const SearchMatchSnapshot snapshot = currentSearchMatchSnapshot();
		if (!snapshot.valid) {
			if (key == "FOUND_STR" || key == "SEARCH_FILE") return makeString("");
			return makeInt(0);
		}
		if (key == "FOUND_STR") return makeString(snapshot.foundText);
		if (key == "SEARCH_FILE") return makeString(snapshot.fileName);
		if (key == "FOUND_X") return makeInt(snapshot.foundX);
		return makeInt(snapshot.foundY);
	}
	if (key == "GET_LINE") return makeString(currentEditorLineText(currentEditor()));
	if (key == "FORMAT_LINE") return makeString(configuredEditSetupSettings().formatLine);
	if (key == "DEFAULT_FORMAT") return makeString(defaultFormatLineValue());
	if (key == "PAGE_STR") return makeString(configuredEditSetupSettings().pageBreak);
	if (key == "WORD_DELIMITS") return makeString(configuredEditSetupSettings().wordDelimiters);
	if (key == "CUR_CHAR") return currentEditorCharValue();
	if (key == "FIRST_SAVE" || key == "BUFFER_ID" || key == "TMP_FILE" || key == "TMP_FILE_NAME" || key == "FILE_CHANGED" || key == "FILE_NAME" || key == "CUR_FILE_ATTR" || key == "CUR_FILE_SIZE" || key == "READ_ONLY") return loadCurrentFileState(key);
	if (key == "FIRST_RUN") {
		if (!g_runtimeEnv.macroStack.empty()) return makeInt(g_runtimeEnv.macroStack.back().firstRun ? 1 : 0);
		return makeInt(0);
	}
	if (key == "FIRST_MACRO") {
		BackgroundEditSession *session = currentBackgroundEditSession();
		if (session != nullptr) {
			session->macroEnumIndex = 0;
			while (session->macroEnumIndex < session->macroOrder.size()) {
				const std::string &macroKey = session->macroOrder[session->macroEnumIndex++];
				std::map<std::string, std::string>::const_iterator it = session->loadedMacroDisplayNames.find(macroKey);
				if (it != session->loadedMacroDisplayNames.end()) return makeString(it->second);
			}
		} else {
			g_runtimeEnv.macroEnumIndex = 0;
			while (g_runtimeEnv.macroEnumIndex < g_runtimeEnv.macroOrder.size()) {
				const std::string &macroKey = g_runtimeEnv.macroOrder[g_runtimeEnv.macroEnumIndex++];
				std::map<std::string, MacroRef>::const_iterator it = g_runtimeEnv.loadedMacros.find(macroKey);
				if (it != g_runtimeEnv.loadedMacros.end()) return makeString(it->second.displayName);
			}
		}
		return makeString("");
	}
	if (key == "NEXT_MACRO") {
		BackgroundEditSession *session = currentBackgroundEditSession();
		if (session != nullptr) {
			while (session->macroEnumIndex < session->macroOrder.size()) {
				const std::string &macroKey = session->macroOrder[session->macroEnumIndex++];
				std::map<std::string, std::string>::const_iterator it = session->loadedMacroDisplayNames.find(macroKey);
				if (it != session->loadedMacroDisplayNames.end()) return makeString(it->second);
			}
		} else {
			while (g_runtimeEnv.macroEnumIndex < g_runtimeEnv.macroOrder.size()) {
				const std::string &macroKey = g_runtimeEnv.macroOrder[g_runtimeEnv.macroEnumIndex++];
				std::map<std::string, MacroRef>::const_iterator it = g_runtimeEnv.loadedMacros.find(macroKey);
				if (it != g_runtimeEnv.loadedMacros.end()) return makeString(it->second.displayName);
			}
		}
		return makeString("");
	}
	handled = false;
	return makeInt(0);
}

static bool storeSpecialVariable(const std::string &name, const Value &value) {
	std::string key = upperKey(name);
	if (key == "RETURN_INT") {
		runtimeReturnInt() = valueAsInt(value);
		return true;
	}
	if (key == "RETURN_STR") {
		runtimeReturnStr() = valueAsString(value);
		enforceStringLength(runtimeReturnStr());
		return true;
	}
	if (key == "ERROR_LEVEL") {
		runtimeErrorLevel() = valueAsInt(value);
		return true;
	}
	if (key == "IGNORE_CASE") {
		BackgroundEditSession *session = currentBackgroundEditSession();
		if (session != nullptr) session->ignoreCase = valueAsInt(value) != 0;
		else
			g_runtimeEnv.ignoreCase = valueAsInt(value) != 0;
		return true;
	}
	if (key == "REG_EXP_STAT") return setCurrentRegexStatus(valueAsInt(value) != 0);
	if (key == "TAB_EXPAND") {
		BackgroundEditSession *session = currentBackgroundEditSession();
		if (session != nullptr) session->tabExpand = valueAsInt(value) != 0;
		else
			g_runtimeEnv.tabExpand = valueAsInt(value) != 0;
		return true;
	}
	if (key == "SHADOW_CHAR") {
		g_runtimeEnv.shadowChar = std::clamp(valueAsInt(value), 0, 255);
		return true;
	}
	if (key == "REFRESH") {
		g_runtimeEnv.refresh = valueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "MESSAGES") return setConfiguredMenulineMessages(valueAsInt(value) != 0, nullptr);
	if (key == "MOUSE") {
		g_runtimeEnv.mouse = valueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "LOGO_SCREEN") {
		g_runtimeEnv.logoScreen = valueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "EXPLOSIONS") {
		g_runtimeEnv.explosions = valueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "TRUNCATE_SPACES") return applyConfiguredEditSetupValue("TRUNCATE_SPACES", valueAsInt(value) != 0 ? "TRUE" : "FALSE", nullptr);
	if (key == "BACKUPS") {
		MREditSetupSettings settings = configuredEditSetupSettings();
		switch (valueAsInt(value)) {
			case 2:
				settings.backupMethod = "DIRECTORY";
				settings.backupFiles = true;
				break;
			case 1:
				settings.backupMethod = "BAK_FILE";
				settings.backupFiles = true;
				break;
			default:
				settings.backupMethod = "OFF";
				settings.backupFiles = false;
				break;
		}
		return setConfiguredEditSetupSettings(settings, nullptr);
	}
	if (key == "AUTOSAVE") {
		MREditSetupSettings settings = configuredEditSetupSettings();
		if (valueAsInt(value) != 0) {
			const MREditSetupSettings defaults = resolveEditSetupDefaults();
			if (settings.autosaveInactivitySeconds <= 0) settings.autosaveInactivitySeconds = defaults.autosaveInactivitySeconds;
			if (settings.autosaveIntervalSeconds <= 0) settings.autosaveIntervalSeconds = defaults.autosaveIntervalSeconds;
		} else {
			settings.autosaveInactivitySeconds = 0;
			settings.autosaveIntervalSeconds = 0;
		}
		return setConfiguredEditSetupSettings(settings, nullptr);
	}
	if (key == "UNDO_STAT") {
		g_runtimeEnv.undoStat = valueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "FORMAT_STAT") {
		g_runtimeEnv.formatStat = valueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "WRAP_STAT") return applyConfiguredEditSetupValue("WORD_WRAP", valueAsInt(value) != 0 ? "TRUE" : "FALSE", nullptr);
	if (key == "MEM_ALLOC") {
		g_runtimeEnv.memAlloc = std::max(0, valueAsInt(value));
		return true;
	}
	if (key == "LEFT_MARGIN") return applyConfiguredEditSetupValue("LEFT_MARGIN", std::to_string(valueAsInt(value)), nullptr);
	if (key == "RIGHT_MARGIN") return applyConfiguredEditSetupValue("RIGHT_MARGIN", std::to_string(valueAsInt(value)), nullptr);
	if (key == "FORMAT_RULER") return applyConfiguredEditSetupValue("FORMAT_RULER", valueAsInt(value) != 0 ? "TRUE" : "FALSE", nullptr);
	if (key == "INDENT_STYLE") return applyConfiguredEditSetupValue("INDENT_STYLE", decodeIndentStyle(valueAsInt(value)), nullptr);
	if (key == "INS_CURSOR") {
		g_runtimeEnv.insCursor = std::clamp(valueAsInt(value), 0, 3);
		return true;
	}
	if (key == "OVR_CURSOR") {
		g_runtimeEnv.ovrCursor = std::clamp(valueAsInt(value), 0, 3);
		return true;
	}
	if (key == "CTRL_HELP") {
		g_runtimeEnv.ctrlHelp = valueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "MOUSE_H_SENSE") {
		g_runtimeEnv.mouseHSense = std::max(0, valueAsInt(value));
		return true;
	}
	if (key == "MOUSE_V_SENSE") {
		g_runtimeEnv.mouseVSense = std::max(0, valueAsInt(value));
		return true;
	}
	if (key == "WINDOW_ATTR") return setCurrentWindowAttrValue(valueAsInt(value));
	if (key == "TEXT_COLOR") return writeWindowColorValue(0, valueAsInt(value));
	if (key == "CHANGE_COLOR") return writeWindowColorValue(1, valueAsInt(value));
	if (key == "BACK_COLOR") return writeOtherColorValue(9, valueAsInt(value));
	if (key == "MENU_COLOR") return writeMenuDialogColorValue(0, valueAsInt(value));
	if (key == "STAT_COLOR") return writeOtherColorValue(0, valueAsInt(value));
	if (key == "ERROR_COLOR") return writeOtherColorValue(4, valueAsInt(value));
	if (key == "SHADOW_COLOR") return writeMenuDialogColorValue(7, valueAsInt(value));
	if (key == "STATUS_ROW") {
		g_runtimeEnv.statusRow = std::max(0, valueAsInt(value));
		return true;
	}
	if (key == "MESSAGE_ROW") {
		g_runtimeEnv.messageRow = std::max(0, valueAsInt(value));
		return true;
	}
	if (key == "MAX_WINDOW_ROW") {
		g_runtimeEnv.maxWindowRow = std::max(0, valueAsInt(value));
		return true;
	}
	if (key == "MIN_WINDOW_ROW") {
		g_runtimeEnv.minWindowRow = std::max(0, valueAsInt(value));
		return true;
	}
	if (key == "NAME_LINE") {
		g_runtimeEnv.nameLine = valueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "INSERT_MODE") return setCurrentEditorInsertMode(valueAsInt(value) != 0);
	if (key == "INDENT_LEVEL") return setCurrentEditorIndentLevel(valueAsInt(value));
	if (key == "MPARM_STR") {
		runtimeParameterString() = valueAsString(value);
		enforceStringLength(runtimeParameterString());
		return true;
	}
	if (key == "DOC_MODE") {
		g_runtimeEnv.docMode = valueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "PRINT_MARGIN") {
		g_runtimeEnv.printMargin = std::max(0, valueAsInt(value));
		return true;
	}
	if (key == "FORMAT_LINE") return applyConfiguredEditSetupValue("FORMAT_LINE", valueAsString(value), nullptr);
	if (key == "DEFAULT_FORMAT") {
		g_runtimeEnv.defaultFormat = valueAsString(value);
		enforceStringLength(g_runtimeEnv.defaultFormat);
		return true;
	}
	if (key == "PAGE_STR") return applyConfiguredEditSetupValue("PAGE_BREAK", valueAsString(value), nullptr);
	if (key == "WORD_DELIMITS") return applyConfiguredEditSetupValue("WORD_DELIMITERS", valueAsString(value), nullptr);
	if (key == "FILE_CHANGED") {
		MREditWindow *win = activeMacroEditWindow();
		BackgroundEditSession *session = currentBackgroundEditSession();
		if (win != nullptr) win->setFileChanged(valueAsInt(value) != 0);
		else if (session != nullptr)
			session->fileChanged = valueAsInt(value) != 0;
		else
			return false;
		return true;
	}
	if (key == "FILE_NAME") {
		MREditWindow *win = activeMacroEditWindow();
		BackgroundEditSession *session = currentBackgroundEditSession();
		if (win != nullptr) win->setCurrentFileName(valueAsString(value).c_str());
		else if (session != nullptr)
			session->fileName = valueAsString(value);
		else
			return false;
		return true;
	}
	if (key == "VIRTUAL_DESKTOPS") {
		if (currentBackgroundEditSession() != nullptr) throw std::runtime_error("VIRTUAL_DESKTOPS cannot be changed in background mode.");
		applyVirtualDesktopConfigurationChange(valueAsInt(value));
		return true;
	}
	if (key == "CYCLIC_VIRTUAL_DESKTOPS") {
		if (currentBackgroundEditSession() != nullptr) throw std::runtime_error("CYCLIC_VIRTUAL_DESKTOPS cannot be changed in background mode.");
		setConfiguredCyclicVirtualDesktops(valueAsInt(value) != 0, nullptr);
		return true;
	}
	if (key == "FIRST_RUN" || key == "FIRST_MACRO" || key == "NEXT_MACRO" || key == "LAST_FILE_NAME" || key == "GET_LINE" || key == "CUR_CHAR" || key == "C_COL" || key == "C_LINE" || key == "C_ROW" || key == "C_PAGE" || key == "PG_LINE" || key == "AT_EOF" || key == "AT_EOL" || key == "CUR_WINDOW" || key == "LINK_STAT" || key == "WIN_X1" || key == "WIN_Y1" || key == "WIN_X2" || key == "WIN_Y2" || key == "WINDOW_COUNT" || key == "KEY1" || key == "KEY2" || key == "BLOCK_STAT" || key == "BLOCK_LINE1" || key == "BLOCK_LINE2" || key == "BLOCK_COL1" || key == "BLOCK_COL2" || key == "MARKING" || key == "FIRST_SAVE" || key == "BUFFER_ID" || key == "TMP_FILE" || key == "TMP_FILE_NAME" || key == "LAST_FILE_ATTR" || key == "LAST_FILE_SIZE" || key == "LAST_FILE_TIME" || key == "CUR_FILE_ATTR" || key == "CUR_FILE_SIZE" || key == "READ_ONLY" || key == "FOUND_STR" || key == "SEARCH_FILE" || key == "FOUND_X" || key == "FOUND_Y" || key == "COMSPEC" || key == "TEMP_PATH" || key == "MR_PATH" ||
	    key == "OS_VERSION" || key == "PARAM_COUNT" || key == "CPU" || key == "DISPLAY_TABS")
		throw std::runtime_error("Attempt to assign to read-only system variable.");
	return false;
}

static std::string parseNamedValue(const std::string &needle, const std::string &source) {
	std::size_t pos = source.find(needle);
	if (pos == std::string::npos) return std::string();
	pos += needle.size();

	// Optimization: In GCC/libstdc++, multiple find(char) calls are significantly
	// faster than a single find_first_of() due to SIMD/memchr acceleration.
	std::size_t endSpace = source.find(' ', pos);
	std::size_t endTab = source.find('\t', pos);
	std::size_t endCr = source.find('\r', pos);
	std::size_t endLf = source.find('\n', pos);
	std::size_t end = std::min({endSpace, endTab, endCr, endLf});

	if (end == std::string::npos) end = source.size();
	return source.substr(pos, end - pos);
}

static void setGlobalValueDirect(const std::string &name, int type, const Value &value) {
	std::string key = upperKey(name);
	GlobalEntry entry;

	entry.type = type;
	entry.value = value;
	entry.value.globalStorage = true;
	if (g_runtimeEnv.globals.find(key) == g_runtimeEnv.globals.end()) g_runtimeEnv.globalOrder.push_back(key);
	g_runtimeEnv.globals[key] = entry;
}

static void setSessionGlobalValueDirect(BackgroundEditSession &session, const std::string &name, int type, const Value &value) {
	std::string key = upperKey(name);
	GlobalEntry entry;

	entry.type = type;
	entry.value = value;
	entry.value.globalStorage = true;
	if (session.globals.find(key) == session.globals.end()) appendUniqueString(session.globalOrder, key);
	session.globals[key] = entry;
}

static void setGlobalValue(const std::string &name, int type, const Value &value) {
	BackgroundEditSession *session = currentBackgroundEditSession();

	if (session != nullptr) {
		setSessionGlobalValueDirect(*session, name, type, value);
		return;
	}
	setGlobalValueDirect(name, type, value);
}

static void setGlobalValueFromStore(const std::string &name, int type, const Value &value, MRVMHashStore &localStore) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	Value stored = value;

	if (type == TYPE_HASH || mrvmValueIsArrayType(type)) stored = mrvmHashCopyValueForStore(value, localStore, g_runtimeEnv.globalHashStore, g_runtimeEnv.globalHashStore, true);
	else
		stored.globalStorage = true;
	if (session != nullptr && type != TYPE_HASH && !mrvmValueIsArrayType(type)) {
		setSessionGlobalValueDirect(*session, name, type, stored);
		return;
	}
	setGlobalValueDirect(name, type, stored);
}

static std::vector<int> currentGlobalHashRoots() {
	std::vector<int> roots;

	for (const std::pair<const std::string, GlobalEntry> &entry : g_runtimeEnv.globals) {
		if (entry.second.type == TYPE_HASH && entry.second.value.type == TYPE_HASH && !entry.second.value.globalStorage) roots.push_back(entry.second.value.hashHandle);
	}
	return roots;
}

static bool readGlobalValue(const std::string &name, GlobalEntry &entry) {
	const std::string key = upperKey(name);
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (session != nullptr) {
		std::map<std::string, GlobalEntry>::const_iterator sessionIt = session->globals.find(key);
		if (sessionIt != session->globals.end()) {
			entry = sessionIt->second;
			return true;
		}
	}
	std::map<std::string, GlobalEntry>::const_iterator it = g_runtimeEnv.globals.find(key);

	if (it == g_runtimeEnv.globals.end()) return false;
	entry = it->second;
	return true;
}

static bool fileExists(const std::string &path) {
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	return in.good();
}

static std::string stripMrmacExtension(const std::string &value) {
	std::string upper = upperKey(value);
	if (upper.size() >= 6 && upper.substr(upper.size() - 6) == ".MRMAC") return value.substr(0, value.size() - 6);
	return value;
}

static std::string makeFileKey(const std::string &value) {
	return upperKey(stripMrmacExtension(trimAscii(value)));
}

static std::string loadedFileBasenameKey(const LoadedMacroFile &file) {
	std::string source = !file.resolvedPath.empty() ? file.resolvedPath : file.displayName;

	if (source.empty()) source = file.fileKey;
	return makeFileKey(truncatePathPart(source));
}

static std::string resolveLoadedFileKeyForSpec(const std::string &fileSpec) {
	const std::string exactKey = makeFileKey(fileSpec);
	std::string matchedKey;

	if (fileSpec.empty()) return std::string();
	if (g_runtimeEnv.loadedFiles.find(exactKey) != g_runtimeEnv.loadedFiles.end()) return exactKey;
	for (const auto &entry : g_runtimeEnv.loadedFiles) {
		if (loadedFileBasenameKey(entry.second) != exactKey) continue;
		if (!matchedKey.empty() && matchedKey != entry.first) return std::string();
		matchedKey = entry.first;
	}
	return matchedKey;
}

static bool fileSpecMatchesLoadedFileKey(const std::string &fileSpec, const std::string &targetFileKey) {
	const std::string resolvedKey = resolveLoadedFileKeyForSpec(fileSpec);

	if (targetFileKey.empty()) return fileSpec.empty();
	if (!resolvedKey.empty()) return resolvedKey == targetFileKey;
	return makeFileKey(fileSpec) == targetFileKey;
}

static bool hasMrmacExtension(const std::string &path) {
	std::size_t dotPos = path.rfind('.');
	if (dotPos == std::string::npos) return false;
	return upperKey(path.substr(dotPos)) == ".MRMAC";
}

static bool isBootstrapIndexedMacroFile(const std::string &path) {
	const std::string baseName = upperKey(truncatePathPart(path));

	// Regression fixtures in mrmac/macros/test*.mrmac should not auto-bind at app startup.
	if (baseName.size() >= 4 && baseName.compare(0, 4, "TEST") == 0) return false;
	return true;
}

static std::vector<std::string> listMrmacFilesInDirectory(const std::string &directoryPath) {
	std::vector<std::string> files;
	std::string dir = trimAscii(directoryPath);
	std::string pattern;
	glob_t matches;

	if (dir.empty()) return files;
	if (!dir.empty() && dir.back() == '/') pattern = dir + "*";
	else
		pattern = dir + "/*";

	std::memset(&matches, 0, sizeof(matches));
	if (::glob(pattern.c_str(), 0, nullptr, &matches) != 0) {
		::globfree(&matches);
		return files;
	}
	for (std::size_t i = 0; i < matches.gl_pathc; ++i) {
		const char *pathText = matches.gl_pathv != nullptr ? matches.gl_pathv[i] : nullptr;
		if (pathText == nullptr || *pathText == '\0') continue;
		std::string path = pathText;
		if (!hasMrmacExtension(path)) continue;
		if (!isBootstrapIndexedMacroFile(path)) continue;
		if (!fileExists(path)) continue;
		files.push_back(path);
	}
	::globfree(&matches);
	std::sort(files.begin(), files.end());
	return files;
}

static std::string resolveMacroFilePath(const std::string &spec) {
	std::string trimmed = trimAscii(spec);
	std::string macroDirectory = normalizeConfiguredPathInput(configuredMacroDirectoryPath());
	auto tryMacroDirectory = [&](const std::string &candidate) -> std::string {
		std::string joined;

		if (macroDirectory.empty() || candidate.empty()) return std::string();
		joined = macroDirectory;
		if (joined.back() != '/') joined += '/';
		joined += candidate;
		return fileExists(joined) ? joined : std::string();
	};
	if (trimmed.empty()) return std::string();
	if (fileExists(trimmed)) return trimmed;
	if (std::string fromMacroDirectory = tryMacroDirectory(trimmed); !fromMacroDirectory.empty()) return fromMacroDirectory;
	if (upperKey(trimmed).size() < 6 || upperKey(trimmed).substr(upperKey(trimmed).size() - 6) != ".MRMAC") {
		std::string withExt = trimmed + ".mrmac";
		if (fileExists(withExt)) return withExt;
		if (std::string withExtFromMacroDirectory = tryMacroDirectory(withExt); !withExtFromMacroDirectory.empty()) return withExtFromMacroDirectory;
	}
	return trimmed;
}

static bool macroIsRunning(const std::string &macroKey) {
	for (auto &i : g_runtimeEnv.macroStack)
		if (upperKey(i.macroName) == macroKey) return true;
	return false;
}

static bool removeMacroFromRegistryByKey(const std::string &macroKey) {
	std::map<std::string, MacroRef>::iterator it = g_runtimeEnv.loadedMacros.find(macroKey);
	std::string ownerSpec;

	if (it == g_runtimeEnv.loadedMacros.end()) return false;
	static_cast<void>(composeLoadedMacroSpec(it->second, ownerSpec));
	if (!ownerSpec.empty()) static_cast<void>(mrvmUiRemoveRuntimeMenusOwnedByMacroSpec(ownerSpec));

	const std::string fileKey = it->second.fileKey;
	g_runtimeEnv.loadedMacros.erase(it);
	g_runtimeEnv.macroOrder.erase(std::remove(g_runtimeEnv.macroOrder.begin(), g_runtimeEnv.macroOrder.end(), macroKey), g_runtimeEnv.macroOrder.end());

	std::map<std::string, LoadedMacroFile>::iterator fit = g_runtimeEnv.loadedFiles.find(fileKey);
	if (fit != g_runtimeEnv.loadedFiles.end()) {
		fit->second.macroNames.erase(std::remove(fit->second.macroNames.begin(), fit->second.macroNames.end(), macroKey), fit->second.macroNames.end());
		if (fit->second.macroNames.empty()) g_runtimeEnv.loadedFiles.erase(fit);
	}
	return true;
}

static std::string normalizeKeySpecToken(const std::string &spec) {
	std::string trimmed = trimAscii(spec);
	std::string normalized;

	if (trimmed.size() < 3 || trimmed.front() != '<' || trimmed.back() != '>') return std::string();
	trimmed = trimmed.substr(1, trimmed.size() - 2);
	for (char i : trimmed) {
		unsigned char ch = static_cast<unsigned char>(i);
		if (std::isspace(ch) != 0 || ch == '_' || ch == '+') continue;
		normalized.push_back(static_cast<char>(std::toupper(ch)));
	}
	return normalized;
}

static bool parseAssignedKeySpec(const std::string &spec, TKey &outKey) {
	std::string token = normalizeKeySpecToken(spec);
	bool wantShift = false;
	bool wantCtrl = false;
	bool wantAlt = false;
	bool wantSuper = false;
	bool changed = true;
	ushort baseCode = 0;
	ushort modifiers = 0;

	if (token.empty()) return false;

	while (changed) {
		changed = false;
		if (token.rfind("SHIFT", 0) == 0) {
			wantShift = true;
			token.erase(0, 5);
			changed = true;
			continue;
		}
		if (token.rfind("SHFT", 0) == 0) {
			wantShift = true;
			token.erase(0, 4);
			changed = true;
			continue;
		}
		if (token.rfind("CTRL", 0) == 0) {
			wantCtrl = true;
			token.erase(0, 4);
			changed = true;
			continue;
		}
		if (token.rfind("ALT", 0) == 0) {
			wantAlt = true;
			token.erase(0, 3);
			changed = true;
			continue;
		}
		if (token.rfind("SUPER", 0) == 0) {
			wantSuper = true;
			token.erase(0, 5);
			changed = true;
			continue;
		}
	}

	if (token.empty()) return false;

	if (token.size() >= 2 && token[0] == 'F' && std::all_of(token.begin() + 1, token.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
		int number = std::atoi(token.c_str() + 1);
		switch (number) {
			case 1:
				baseCode = kbF1;
				break;
			case 2:
				baseCode = kbF2;
				break;
			case 3:
				baseCode = kbF3;
				break;
			case 4:
				baseCode = kbF4;
				break;
			case 5:
				baseCode = kbF5;
				break;
			case 6:
				baseCode = kbF6;
				break;
			case 7:
				baseCode = kbF7;
				break;
			case 8:
				baseCode = kbF8;
				break;
			case 9:
				baseCode = kbF9;
				break;
			case 10:
				baseCode = kbF10;
				break;
			case 11:
				baseCode = kbF11;
				break;
			case 12:
				baseCode = kbF12;
				break;
			default:
				return false;
		}
	} else if (token == "ENTER" || token == "RETURN")
		baseCode = kbEnter;
	else {
		static const std::unordered_map<std::string_view, ushort> tokenToBaseCode = {{"TAB", kbTab}, {"ESC", kbEsc}, {"BS", kbBack}, {"BACK", kbBack}, {"BACKSPACE", kbBack}, {"UP", kbUp}, {"DN", kbDown}, {"DOWN", kbDown}, {"LF", kbLeft}, {"LEFT", kbLeft}, {"RT", kbRight}, {"RIGHT", kbRight}, {"PGUP", kbPgUp}, {"PGDN", kbPgDn}, {"HOME", kbHome}, {"END", kbEnd}, {"INS", kbIns}, {"DEL", kbDel}, {"GREY-", kbGrayMinus}, {"GREY+", kbGrayPlus}, {"GREY*", static_cast<ushort>('*')}, {"SPACE", static_cast<ushort>(' ')}, {"MINUS", static_cast<ushort>('-')}, {"EQUAL", static_cast<ushort>('=')}};
		auto it = tokenToBaseCode.find(token);
		if (it != tokenToBaseCode.end()) {
			baseCode = it->second;
		} else if (token.size() == 1) {
			baseCode = static_cast<ushort>(token[0]);
		} else {
			return false;
		}
	}

	if (wantShift) modifiers |= kbShift;
	if (wantCtrl) modifiers |= kbCtrlShift;
	if (wantAlt) modifiers |= kbAltShift;
	if (wantSuper) modifiers |= kbSuperShift;

	outKey = TKey(baseCode, modifiers);
	return true;
}

static bool parseIndexedBindingHeaders(const std::string &source, std::vector<TKey> &keys) {
	std::size_t i = 0;
	bool foundAny = false;

	keys.clear();
	while (i < source.size()) {
		std::size_t macroPos = source.find('$', i);
		if (macroPos == std::string::npos) break;
		i = macroPos + 1;
		if (!startsWithTokenInsensitive(source, macroPos, "$MACRO")) continue;

		std::size_t p = macroPos + 6;
		while (p < source.size() && std::isspace(static_cast<unsigned char>(source[p])) != 0)
			++p;
		if (p >= source.size()) break;
		std::size_t nameStart = p;
		while (p < source.size()) {
			unsigned char ch = static_cast<unsigned char>(source[p]);
			if (std::isalnum(ch) == 0 && ch != '_') break;
			++p;
		}
		if (p == nameStart) continue;

		std::size_t semicolon = source.find(';', p);
		if (semicolon == std::string::npos) break;

		std::istringstream header(source.substr(p, semicolon - p));
		std::vector<std::string> tokens;
		std::string token;
		while (header >> token)
			tokens.push_back(token);
		for (std::size_t t = 0; t + 1 < tokens.size(); ++t) {
			TKey parsed;
			if (upperKey(tokens[t]) != "TO") continue;
			if (!parseAssignedKeySpec(tokens[t + 1], parsed)) continue;
			bool duplicate = false;
			for (const auto &key : keys)
				if (key == parsed) {
					duplicate = true;
					break;
				}
			if (!duplicate) {
				keys.push_back(parsed);
				foundAny = true;
			}
		}
		i = semicolon + 1;
	}
	return foundAny;
}

static bool dispatchSyntheticKeyToUi(const TKey &key, const char *text, std::size_t textLength) {
	MREditWindow *win = activeMacroEditWindow();
	TEvent event;

	if (win == nullptr || win->getEditor() == nullptr) return false;

	std::memset(&event, 0, sizeof(event));
	event.what = evKeyDown;
	event.keyDown.keyCode = key.code;
	event.keyDown.controlKeyState = key.mods;
	if (text != nullptr && textLength > 0) {
		if (textLength > sizeof(event.keyDown.text)) textLength = sizeof(event.keyDown.text);
		std::memcpy(event.keyDown.text, text, textLength);
		event.keyDown.textLength = static_cast<uchar>(textLength);
	}
	win->handleEvent(event);
	return true;
}

static bool replayKeyInputSequence(const std::string &sequence) {
	struct KeyReplayGuard {
		KeyReplayGuard() noexcept {
			++g_keyReplayDepth;
		}
		~KeyReplayGuard() {
			--g_keyReplayDepth;
		}
	} replayGuard;

	std::size_t i = 0;

	if (currentBackgroundEditSession() != nullptr) return false;
	if (activeMacroEditWindow() == nullptr || currentEditor() == nullptr) return false;

	while (i < sequence.size()) {
		unsigned char ch = static_cast<unsigned char>(sequence[i]);
		if (ch == '<') {
			std::size_t closePos = sequence.find('>', i + 1);
			if (closePos != std::string::npos) {
				TKey key;
				std::string token = sequence.substr(i, closePos - i + 1);
				if (parseAssignedKeySpec(token, key)) {
					if (!dispatchSyntheticKeyToUi(key)) return false;
					i = closePos + 1;
					continue;
				}
			}
		}

		if (ch == '\r') {
			if (i + 1 < sequence.size() && sequence[i + 1] == '\n') ++i;
			if (!dispatchSyntheticKeyToUi(TKey(kbEnter))) return false;
			++i;
			continue;
		}
		if (ch == '\n') {
			if (!dispatchSyntheticKeyToUi(TKey(kbEnter))) return false;
			++i;
			continue;
		}
		if (ch == '\t') {
			if (!dispatchSyntheticKeyToUi(TKey(kbTab))) return false;
			++i;
			continue;
		}
		if (ch == '\b') {
			if (!dispatchSyntheticKeyToUi(TKey(kbBack))) return false;
			++i;
			continue;
		}
		if (ch == 27) {
			if (!dispatchSyntheticKeyToUi(TKey(kbEsc))) return false;
			++i;
			continue;
		}
		if (ch == 127) {
			if (!dispatchSyntheticKeyToUi(TKey(kbDel))) return false;
			++i;
			continue;
		}

		char textByte = static_cast<char>(ch);
		if (!dispatchSyntheticKeyToUi(TKey(static_cast<ushort>(ch)), &textByte, 1)) return false;
		++i;
	}
	return true;
}

static void storeLastKeyPair(int key1, int key2) noexcept {
	g_runtimeEnv.key1 = key1;
	g_runtimeEnv.key2 = key2;
}

static bool keyPairFromEvent(const TEvent &event, int &key1, int &key2) noexcept {
	if (event.what != evKeyDown) return false;
	key1 = static_cast<unsigned char>(event.keyDown.charScan.charCode);
	key2 = static_cast<unsigned char>(event.keyDown.charScan.scanCode);
	return true;
}

static bool popQueuedKeyPair(int &key1, int &key2) noexcept {
	if (g_runtimeEnv.pushedKeys.empty()) return false;
	const MacroKeyCodePair pair = g_runtimeEnv.pushedKeys.front();
	g_runtimeEnv.pushedKeys.erase(g_runtimeEnv.pushedKeys.begin());
	key1 = pair.key1;
	key2 = pair.key2;
	storeLastKeyPair(key1, key2);
	return true;
}

static bool pushQueuedKeyPair(int key1, int key2) noexcept {
	static constexpr std::size_t maxQueuedKeys = 16;
	if (g_runtimeEnv.pushedKeys.size() >= maxQueuedKeys) return false;
	g_runtimeEnv.pushedKeys.push_back({key1, key2});
	return true;
}

static bool pollUiForKeyPair(bool blocking, int &key1, int &key2) {
	TApplication *app = dynamic_cast<TApplication *>(TProgram::application);
	TEvent event;

	if (app == nullptr || currentBackgroundEditSession() != nullptr) return false;
	for (;;) {
		static_cast<TView *>(app)->getEvent(event, blocking ? 100 : 0);
		if (event.what == evNothing) return false;
		if (keyPairFromEvent(event, key1, key2)) {
			storeLastKeyPair(key1, key2);
			return true;
		}
		app->handleEvent(event);
		if (!blocking) return false;
	}
}

static bool readMacroKeyPair(bool blocking, int &key1, int &key2) {
	if (popQueuedKeyPair(key1, key2)) return true;
	return pollUiForKeyPair(blocking, key1, key2);
}

static bool keyPairToTKey(int key1, int key2, TKey &key, const char *&text, std::size_t &textLength, char &textByte) {
	static const std::unordered_map<int, ushort> scanCodeToKey = {{59, kbF1}, {60, kbF2}, {61, kbF3}, {62, kbF4}, {63, kbF5}, {64, kbF6}, {65, kbF7}, {66, kbF8}, {67, kbF9}, {68, kbF10}, {133, kbF11}, {134, kbF12}, {72, kbUp}, {80, kbDown}, {75, kbLeft}, {77, kbRight}, {73, kbPgUp}, {81, kbPgDn}, {71, kbHome}, {79, kbEnd}, {82, kbIns}, {83, kbDel}, {15, kbShiftTab}};

	text = nullptr;
	textLength = 0;
	if (key1 != 0) {
		switch (key1) {
			case 13:
				key = TKey(kbEnter);
				return true;
			case 9:
				key = TKey(kbTab);
				return true;
			case 8:
				key = TKey(kbBack);
				return true;
			case 27:
				key = TKey(kbEsc);
				return true;
			case 127:
				key = TKey(kbDel);
				return true;
			default:
				key = TKey(static_cast<ushort>(static_cast<unsigned char>(key1)));
				if (key1 >= 32 && key1 <= 255) {
					textByte = static_cast<char>(static_cast<unsigned char>(key1));
					text = &textByte;
					textLength = 1;
				}
				return true;
		}
	}
	auto it = scanCodeToKey.find(key2);
	if (it == scanCodeToKey.end()) return false;
	key = TKey(it->second);
	return true;
}

static bool passMacroKeyPairToUi(int key1, int key2) {
	TKey key;
	const char *text = nullptr;
	std::size_t textLength = 0;
	char textByte = '\0';

	if (currentBackgroundEditSession() != nullptr) return false;
	if (!keyPairToTKey(key1, key2, key, text, textLength, textByte)) return false;
	return dispatchSyntheticKeyToUi(key, text, textLength);
}

static MacroFunctionLabelFrame &currentFunctionLabelFrame() {
	if (g_runtimeEnv.functionLabelStack.empty()) g_runtimeEnv.functionLabelStack.emplace_back();
	return g_runtimeEnv.functionLabelStack.back();
}

static std::vector<std::string> visibleFunctionLabelsForMode(int mode) {
	static constexpr std::array<int, 13> supportedKeys = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 41, 42};
	const MacroFunctionLabelFrame &frame = currentFunctionLabelFrame();
	const auto &source = mode == MACRO_MODE_DOS_SHELL ? frame.shellLabels : frame.editLabels;
	std::vector<std::string> labels(source.size());

	for (int keyNumber : supportedKeys)
		if (keyNumber > 0 && keyNumber < static_cast<int>(source.size())) labels[static_cast<std::size_t>(keyNumber)] = source[static_cast<std::size_t>(keyNumber)];
	return labels;
}

static void applyFunctionLabelState() {
	TApplication *app = dynamic_cast<TApplication *>(TProgram::application);
	MRStatusLine *statusLine;

	if (app == nullptr) return;
	statusLine = dynamic_cast<MRStatusLine *>(app->statusLine);
	if (statusLine == nullptr) return;
	statusLine->setMacroFunctionLabels(visibleFunctionLabelsForMode(currentUiMacroMode()));
	mrvmUiInvalidateScreenBase();
}

struct MacroMenuRequest {
	int x = 0;
	int y = 0;
	int start = 1;
	std::string title;
	std::string menuSpec;
	bool horizontal = false;
};

struct MacroStringInputRequest {
	int x = 0;
	int y = 0;
	int width = 40;
	std::string title;
	std::string initialValue;
};

struct MacroUiLabelSpec {
	int x = 0;
	int y = 0;
	std::string text;
};

struct MacroUiButtonSpec {
	int x = 0;
	int y = 0;
	int width = 8;
	int id = 0;
	std::string text;
};

struct MacroUiDisplaySpec {
	int x = 0;
	int y = 0;
	int width = 20;
	std::string text;
};

struct MacroUiInputSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	int id = 0;
	std::string label;
	std::string text;
};

struct MacroUiListBoxSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	int height = 4;
	int id = 0;
	std::string label;
	std::string itemSpec;
	int start = 1;
};

struct MacroUiDialogDefinition {
	int x = 0;
	int y = 0;
	int width = 40;
	int height = 12;
	std::string title;
	std::vector<MacroUiLabelSpec> labels;
	std::vector<MacroUiButtonSpec> buttons;
	std::vector<MacroUiDisplaySpec> displays;
	std::vector<MacroUiInputSpec> inputs;
	std::vector<MacroUiListBoxSpec> listBoxes;
	std::map<int, std::string> textValues;
	std::map<int, int> indexValues;
	int lastCommandId = 0;
	bool active = false;

	void reset() {
		x = 0;
		y = 0;
		width = 40;
		height = 12;
		title.clear();
		labels.clear();
		buttons.clear();
		displays.clear();
		inputs.clear();
		listBoxes.clear();
		textValues.clear();
		indexValues.clear();
		lastCommandId = 0;
		active = false;
	}
};

struct MacroUiButtonCaption {
	std::string displayLabel;
	std::vector<ushort> hotKeys;
};

static ushort macroNamedHotKeyCode(const std::string &name) noexcept {
	struct NamedKey {
		const char *name;
		ushort code;
	};
	static const NamedKey kNamedKeys[] = {
	    {"ENTER", kbEnter}, {"ESC", kbEsc}, {"ESCAPE", kbEsc}, {"TAB", kbTab}, {"F1", kbF1}, {"F2", kbF2}, {"F3", kbF3}, {"F4", kbF4}, {"F5", kbF5}, {"F6", kbF6}, {"F7", kbF7}, {"F8", kbF8}, {"F9", kbF9}, {"F10", kbF10}, {"F11", kbF11}, {"F12", kbF12},
	};

	for (const NamedKey &entry : kNamedKeys)
		if (upperKey(name) == entry.name) return entry.code;
	return 0;
}

static MacroUiButtonCaption parseMacroUiButtonCaption(const std::string &text) {
	MacroUiButtonCaption entry;
	std::size_t index = 0;

	const std::string label = trimAscii(text);
	while (index < label.size()) {
		if (label[index] == '~') {
			const std::size_t close = label.find('~', index + 1);
			if (close != std::string::npos && close > index + 1) {
				entry.displayLabel += label.substr(index + 1, close - index - 1);
				const char hotChar = static_cast<char>(std::toupper(static_cast<unsigned char>(label[index + 1])));
				entry.hotKeys.push_back(static_cast<ushort>(hotChar));
				index = close + 1;
				continue;
			}
		}
		if (label[index] == '<') {
			const std::size_t close = label.find('>', index + 1);
			if (close != std::string::npos) {
				const ushort keyCode = macroNamedHotKeyCode(trimAscii(label.substr(index + 1, close - index - 1)));
				if (keyCode != 0) entry.hotKeys.push_back(keyCode);
				index = close + 1;
				continue;
			}
		}
		entry.displayLabel.push_back(label[index]);
		++index;
	}
	if (entry.displayLabel.size() == 1) {
		const char hotChar = static_cast<char>(std::toupper(static_cast<unsigned char>(entry.displayLabel.front())));
		entry.hotKeys.push_back(static_cast<ushort>(hotChar));
	}
	return entry;
}

static std::vector<std::string> parseMacroMenuItems(const std::string &menuSpec) {
	std::vector<std::string> items;
	std::size_t pos = 0;
	std::size_t last = 0;

	while (pos < menuSpec.size()) {
		std::size_t openPos = menuSpec.find('(', pos);
		if (openPos == std::string::npos) break;
		std::string item = trimAscii(menuSpec.substr(last, openPos - last));
		std::size_t closePos = menuSpec.find(')', openPos + 1);
		if (!item.empty()) items.push_back(item);
		if (closePos == std::string::npos) return items;
		pos = closePos + 1;
		last = pos;
	}
	if (items.empty()) {
		std::size_t start = 0;
		while (start <= menuSpec.size()) {
			std::size_t sep = menuSpec.find('|', start);
			std::string item = trimAscii(menuSpec.substr(start, sep == std::string::npos ? sep : sep - start));
			if (!item.empty()) items.push_back(item);
			if (sep == std::string::npos) break;
			start = sep + 1;
		}
	}
	return items;
}

static std::string macroUiListKey(const std::string &name) {
	return upperKey(trimAscii(name));
}

static std::unordered_map<std::string, std::vector<std::string>> g_macroUiItemLists;

static std::vector<std::string> resolveMacroUiListItems(const std::string &itemSpec) {
	const std::string key = macroUiListKey(itemSpec);
	const auto it = g_macroUiItemLists.find(key);

	if (it != g_macroUiItemLists.end()) return it->second;
	return parseMacroMenuItems(itemSpec);
}

static int macroMenuDialogWidth(const MacroMenuRequest &request) noexcept {
	return std::max(36, std::min(72, static_cast<int>(request.menuSpec.size()) + 8));
}

static int macroMenuDialogHeight(const MacroMenuRequest &request) {
	return std::min(22, std::max(10, static_cast<int>(parseMacroMenuItems(request.menuSpec).size()) + 7));
}

static TRect macroDialogBounds(int width, int height, int x, int y) {
	TRect desk = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, 80, 25);
	int dialogWidth = std::min(width, desk.b.x - desk.a.x - 2);
	int dialogHeight = std::min(height, desk.b.y - desk.a.y - 2);
	int left = desk.a.x + (desk.b.x - desk.a.x - dialogWidth) / 2;
	int top = desk.a.y + (desk.b.y - desk.a.y - dialogHeight) / 2;

	if (x > 0) left = std::clamp(desk.a.x + x - 1, desk.a.x, desk.b.x - dialogWidth);
	if (y > 0) top = std::clamp(desk.a.y + y - 1, desk.a.y, desk.b.y - dialogHeight);
	return TRect(left, top, left + dialogWidth, top + dialogHeight);
}

static MacroUiDialogDefinition g_macroUiDialog;

class MacroMenuListView final : public TListViewer {
  public:
	MacroMenuListView(const TRect &bounds, TScrollBar *scrollBar, const std::vector<std::string> &menuItems) noexcept : TListViewer(bounds, 1, nullptr, scrollBar), items(menuItems) {
		setRange(static_cast<short>(items.size()));
	}

	void getText(char *dest, short item, short maxLen) override {
		if (dest == nullptr || maxLen <= 0) return;
		if (item < 0 || static_cast<std::size_t>(item) >= items.size()) {
			dest[0] = EOS;
			return;
		}
		std::strncpy(dest, items[static_cast<std::size_t>(item)].c_str(), static_cast<std::size_t>(maxLen - 1));
		dest[maxLen - 1] = EOS;
	}

	void handleEvent(TEvent &event) override {
		TListViewer::handleEvent(event);
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter && owner != nullptr) {
			message(owner, evCommand, cmOK, this);
			clearEvent(event);
		}
	}

  private:
	std::vector<std::string> items;
};

static int runMacroMenuDialog(const MacroMenuRequest &request) {
	class MacroMenuDialog final : public MRDialogFoundation {
	  public:
		MacroMenuDialog(const MacroMenuRequest &menuRequest) : TWindowInit(&TDialog::initFrame), MRDialogFoundation(macroDialogBounds(macroMenuDialogWidth(menuRequest), macroMenuDialogHeight(menuRequest), menuRequest.x, menuRequest.y), menuRequest.title.empty() ? (menuRequest.horizontal ? "BAR MENU" : "V MENU") : menuRequest.title.c_str(), macroMenuDialogWidth(menuRequest), macroMenuDialogHeight(menuRequest)), menuRequestItems(parseMacroMenuItems(menuRequest.menuSpec)) {
			int width = size.x;
			int height = size.y;

			scrollBar = new TScrollBar(TRect(width - 3, 2, width - 2, height - 4));
			insert(scrollBar);
			listView = new MacroMenuListView(TRect(2, 2, width - 3, height - 4), scrollBar, menuRequestItems);
			insert(listView);
			insert(new TButton(TRect(width - 30, height - 3, width - 21, height - 1), "~D~one", cmOK, bfDefault));
			insert(new TButton(TRect(width - 20, height - 3, width - 9, height - 1), "~C~ancel", cmCancel, bfNormal));
			insert(new TButton(TRect(width - 8, height - 3, width - 2, height - 1), "~H~elp", cmHelp, bfNormal));
			if (!menuRequestItems.empty()) {
				int index = std::clamp(menuRequest.start, 1, static_cast<int>(menuRequestItems.size())) - 1;
				listView->focusItemNum(static_cast<short>(index));
			}
		}

		void handleEvent(TEvent &event) override {
			MRDialogFoundation::handleEvent(event);
			if (event.what == evCommand && event.message.command == cmHelp) {
				static_cast<void>(mrShowProjectHelp());
				clearEvent(event);
			}
		}

		int selectedIndex() const noexcept {
			if (listView == nullptr || listView->focused < 0) return 0;
			return listView->focused + 1;
		}

	  private:
		std::vector<std::string> menuRequestItems;
		TScrollBar *scrollBar = nullptr;
		MacroMenuListView *listView = nullptr;
	};

	MacroMenuDialog *dialog = new MacroMenuDialog(request);
	ushort result = cmCancel;
	int selected = 0;

	if (dialog == nullptr) return 0;
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	selected = result == cmOK ? dialog->selectedIndex() : 0;
	TObject::destroy(dialog);
	return selected;
}

static std::string runMacroStringInputDialog(const MacroStringInputRequest &request) {
	class MacroStringInputDialog final : public MRDialogFoundation {
	  public:
		MacroStringInputDialog(const MacroStringInputRequest &inputRequest) : TWindowInit(&TDialog::initFrame), MRDialogFoundation(macroDialogBounds(std::max(34, inputRequest.width + 10), 9, inputRequest.x, inputRequest.y), inputRequest.title.empty() ? "STRING INPUT" : inputRequest.title.c_str(), std::max(34, inputRequest.width + 10), 9) {
			int width = size.x;
			char *buffer = newStr(inputRequest.initialValue.c_str());

			inputLine = new TInputLine(TRect(13, 2, width - 3, 3), std::max(1, inputRequest.width));
			insert(new TLabel(TRect(2, 2, 12, 3), "Value:", inputLine));
			insert(inputLine);
			inputLine->setData(buffer);
			delete[] buffer;
			insert(new TButton(TRect(width - 30, 6, width - 21, 8), "~D~one", cmOK, bfDefault));
			insert(new TButton(TRect(width - 20, 6, width - 9, 8), "~C~ancel", cmCancel, bfNormal));
			insert(new TButton(TRect(width - 8, 6, width - 2, 8), "~H~elp", cmHelp, bfNormal));
			selectNext(False);
		}

		void handleEvent(TEvent &event) override {
			MRDialogFoundation::handleEvent(event);
			if (event.what == evCommand && event.message.command == cmHelp) {
				static_cast<void>(mrShowProjectHelp());
				clearEvent(event);
			}
		}

		std::string value() const {
			char buffer[512] = {0};
			if (inputLine != nullptr) inputLine->getData(buffer);
			return std::string(buffer);
		}

	  private:
		TInputLine *inputLine = nullptr;
	};

	MacroStringInputDialog *dialog = new MacroStringInputDialog(request);
	ushort result = cmCancel;
	std::string value;

	if (dialog == nullptr) return std::string();
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	value = result == cmOK ? dialog->value() : std::string();
	TObject::destroy(dialog);
	return value;
}

class MacroUiListView final : public TListViewer {
  public:
	MacroUiListView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> items, ushort command) noexcept : TListViewer(bounds, 1, nullptr, scrollBar), items(std::move(items)), command(command) {
		setRange(static_cast<short>(this->items.size()));
	}

	void getText(char *dest, short item, short maxLen) override {
		if (dest == nullptr || maxLen <= 0) return;
		if (item < 0 || static_cast<std::size_t>(item) >= items.size()) {
			dest[0] = EOS;
			return;
		}
		std::strncpy(dest, items[static_cast<std::size_t>(item)].c_str(), static_cast<std::size_t>(maxLen - 1));
		dest[maxLen - 1] = EOS;
	}

	void handleEvent(TEvent &event) override {
		TListViewer::handleEvent(event);
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter && owner != nullptr) {
			message(owner, evCommand, command, this);
			clearEvent(event);
			return;
		}
		if (event.what == evMouseDown && (event.mouse.eventFlags & meDoubleClick) != 0 && owner != nullptr) {
			message(owner, evCommand, command, this);
			clearEvent(event);
		}
	}

	const std::vector<std::string> &values() const noexcept {
		return items;
	}

  private:
	std::vector<std::string> items;
	ushort command = 0;
};

class MacroUiDisplayLine final : public TView {
  public:
	MacroUiDisplayLine(const TRect &bounds, std::string text) noexcept : TView(bounds), text(std::move(text)) {
		options &= ~(ofSelectable | ofFirstClick);
		eventMask &= static_cast<ushort>(~(evMouseDown | evMouseUp | evMouseMove | evKeyDown));
	}

	void draw() override {
		TDrawBuffer buffer;
		unsigned char configuredAttr = 0;
		TColorAttr color = getColor(1);
		const int width = size.x;
		const std::string value = text.empty() ? std::string("0") : text;
		const int start = std::max(0, width - static_cast<int>(value.size()));

		if (configuredColorSlotOverride(9, configuredAttr)) color = TColorAttr(configuredAttr);
		buffer.moveChar(0, ' ', color, static_cast<ushort>(width));
		if (start < width) buffer.moveStr(static_cast<ushort>(start), value.c_str(), color, width - start);
		writeLine(0, 0, width, 1, buffer);
	}

  private:
	std::string text;
};

static void beginMacroUiDialog(const std::vector<Value> &args) {
	g_macroUiDialog.reset();
	g_macroUiDialog.x = valueAsInt(args[0]);
	g_macroUiDialog.y = valueAsInt(args[1]);
	g_macroUiDialog.width = std::max(24, valueAsInt(args[2]));
	g_macroUiDialog.height = std::max(8, valueAsInt(args[3]));
	g_macroUiDialog.title = valueAsString(args[4]);
	g_macroUiDialog.active = true;
}

static void addMacroUiLabel(const std::vector<Value> &args) {
	MacroUiLabelSpec spec;

	spec.x = valueAsInt(args[0]);
	spec.y = valueAsInt(args[1]);
	spec.text = valueAsString(args[2]);
	g_macroUiDialog.labels.push_back(std::move(spec));
}

static void addMacroUiButton(const std::vector<Value> &args) {
	MacroUiButtonSpec spec;

	spec.x = valueAsInt(args[0]);
	spec.y = valueAsInt(args[1]);
	spec.width = std::max(6, valueAsInt(args[2]));
	spec.id = valueAsInt(args[3]);
	spec.text = valueAsString(args[4]);
	g_macroUiDialog.buttons.push_back(std::move(spec));
}

static void addMacroUiDisplay(const std::vector<Value> &args) {
	MacroUiDisplaySpec spec;

	spec.x = valueAsInt(args[0]);
	spec.y = valueAsInt(args[1]);
	spec.width = std::max(4, valueAsInt(args[2]));
	spec.text = valueAsString(args[3]);
	g_macroUiDialog.displays.push_back(std::move(spec));
}

static void addMacroUiInput(const std::vector<Value> &args) {
	MacroUiInputSpec spec;

	spec.x = valueAsInt(args[0]);
	spec.y = valueAsInt(args[1]);
	spec.width = std::max(4, valueAsInt(args[2]));
	spec.id = valueAsInt(args[3]);
	spec.label = valueAsString(args[4]);
	spec.text = valueAsString(args[5]);
	g_macroUiDialog.textValues[spec.id] = spec.text;
	g_macroUiDialog.inputs.push_back(std::move(spec));
}

static void addMacroUiListBox(const std::vector<Value> &args) {
	MacroUiListBoxSpec spec;
	const std::vector<std::string> items = resolveMacroUiListItems(valueAsString(args[6]));

	spec.x = valueAsInt(args[0]);
	spec.y = valueAsInt(args[1]);
	spec.width = std::max(8, valueAsInt(args[2]));
	spec.height = std::max(2, valueAsInt(args[3]));
	spec.id = valueAsInt(args[4]);
	spec.label = valueAsString(args[5]);
	spec.itemSpec = valueAsString(args[6]);
	spec.start = std::max(1, valueAsInt(args[7]));
	g_macroUiDialog.indexValues[spec.id] = items.empty() ? 0 : std::min(static_cast<int>(items.size()), spec.start);
	g_macroUiDialog.textValues[spec.id] = items.empty() ? std::string() : items[static_cast<std::size_t>(g_macroUiDialog.indexValues[spec.id] - 1)];
	g_macroUiDialog.listBoxes.push_back(std::move(spec));
}

static void clearMacroUiItemList(const std::vector<Value> &args) {
	const std::string key = macroUiListKey(valueAsString(args[0]));

	if (key.empty()) throw std::runtime_error("UI_LIST_CLEAR expects a non-empty list name.");
	g_macroUiItemLists[key].clear();
}

static void addMacroUiItemListValue(const std::vector<Value> &args) {
	const std::string key = macroUiListKey(valueAsString(args[0]));

	if (key.empty()) throw std::runtime_error("UI_LIST_ADD expects a non-empty list name.");
	g_macroUiItemLists[key].push_back(valueAsString(args[1]));
}

static int runMacroUiDialogDefinition() {
	class MacroUiDialog final : public MRDialogFoundation {
	  public:
		explicit MacroUiDialog(const MacroUiDialogDefinition &definition) : TWindowInit(&TDialog::initFrame), MRDialogFoundation(macroDialogBounds(definition.width, definition.height, definition.x, definition.y), definition.title.empty() ? "DIALOG" : definition.title.c_str(), definition.width, definition.height) {
			ushort nextCommand = 41000;

			for (const auto &label : definition.labels)
				insert(new TStaticText(TRect(label.x, label.y, label.x + strwidth(label.text.c_str()), label.y + 1), label.text.c_str()));

			for (const auto &display : definition.displays)
				insert(new MacroUiDisplayLine(TRect(display.x, display.y, display.x + display.width, display.y + 1), display.text));

			for (const auto &input : definition.inputs) {
				const std::string labelText = input.label + ":";
				char *buffer = newStr(input.text.c_str());
				TInputLine *inputLine = new TInputLine(TRect(input.x + strwidth(labelText.c_str()) + 1, input.y, input.x + strwidth(labelText.c_str()) + 1 + input.width, input.y + 1), input.width);
				insert(new TLabel(TRect(input.x, input.y, input.x + strwidth(labelText.c_str()), input.y + 1), labelText.c_str(), inputLine));
				insert(inputLine);
				inputLine->setData(buffer);
				delete[] buffer;
				inputLines.emplace_back(input.id, inputLine);
			}

			for (const auto &listBox : definition.listBoxes) {
				const std::vector<std::string> items = resolveMacroUiListItems(listBox.itemSpec);
				const int listTop = listBox.label.empty() ? listBox.y : listBox.y + 1;
				TScrollBar *scrollBar = nullptr;
				MacroUiListView *listView = nullptr;

				if (!listBox.label.empty()) insert(new TStaticText(TRect(listBox.x, listBox.y, listBox.x + strwidth(listBox.label.c_str()), listBox.y + 1), listBox.label.c_str()));
				scrollBar = new TScrollBar(TRect(listBox.x + listBox.width - 1, listTop, listBox.x + listBox.width, listTop + listBox.height));
				insert(scrollBar);
				listView = new MacroUiListView(TRect(listBox.x, listTop, listBox.x + listBox.width - 1, listTop + listBox.height), scrollBar, items, nextCommand);
				insert(listView);
				if (!items.empty()) listView->focusItemNum(static_cast<short>(std::clamp(listBox.start, 1, static_cast<int>(items.size())) - 1));
				commandToId[nextCommand] = listBox.id;
				listViews.emplace_back(listBox.id, listView);
				++nextCommand;
			}

			for (const auto &button : definition.buttons) {
				const MacroUiButtonCaption caption = parseMacroUiButtonCaption(button.text);
				commandToId[nextCommand] = button.id;
				for (ushort hotKey : caption.hotKeys)
					buttonHotKeys.emplace_back(hotKey, nextCommand);
				insert(new TButton(TRect(button.x, button.y, button.x + button.width, button.y + 2), caption.displayLabel.c_str(), nextCommand, bfNormal));
				++nextCommand;
			}
		}

		void handleEvent(TEvent &event) override {
			if (event.what == evKeyDown) {
				const unsigned char typedChar = static_cast<unsigned char>(event.keyDown.charScan.charCode);
				const ushort hotKey = typedChar >= 32 ? static_cast<ushort>(std::toupper(typedChar)) : event.keyDown.keyCode;
				for (const auto &[registeredKey, command] : buttonHotKeys)
					if (registeredKey == hotKey) {
						endModal(command);
						clearEvent(event);
						return;
					}
				if (ctrlToArrow(event.keyDown.keyCode) == kbEsc) {
					endModal(cmCancel);
					clearEvent(event);
					return;
				}
			}

			MRDialogFoundation::handleEvent(event);
			if (event.what == evCommand) {
				if (commandToId.find(event.message.command) != commandToId.end()) {
					endModal(event.message.command);
					clearEvent(event);
					return;
				}
			}
		}

		int selectedControlId(ushort result) const noexcept {
			const auto it = commandToId.find(result);
			return it != commandToId.end() ? it->second : 0;
		}

		void collectValues(std::map<int, std::string> &textValues, std::map<int, int> &indexValues) const {
			char buffer[512] = {0};

			for (const auto &[id, inputLine] : inputLines) {
				std::memset(buffer, 0, sizeof(buffer));
				if (inputLine != nullptr) inputLine->getData(buffer);
				textValues[id] = buffer;
			}
			for (const auto &[id, listView] : listViews) {
				const int index = listView != nullptr ? listView->focused + 1 : 0;
				indexValues[id] = std::max(0, index);
				if (listView != nullptr && index > 0 && static_cast<std::size_t>(index - 1) < listView->values().size()) textValues[id] = listView->values()[static_cast<std::size_t>(index - 1)];
				else
					textValues[id].clear();
			}
		}

	  private:
		std::map<ushort, int> commandToId;
		std::vector<std::pair<ushort, ushort>> buttonHotKeys;
		std::vector<std::pair<int, TInputLine *>> inputLines;
		std::vector<std::pair<int, MacroUiListView *>> listViews;
	};

	MacroUiDialog *dialog = new MacroUiDialog(g_macroUiDialog);
	ushort result = cmCancel;

	if (dialog == nullptr) return 0;
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	dialog->collectValues(g_macroUiDialog.textValues, g_macroUiDialog.indexValues);
	g_macroUiDialog.lastCommandId = result == cmCancel ? 0 : dialog->selectedControlId(result);
	TObject::destroy(dialog);
	g_macroUiDialog.active = false;
	return g_macroUiDialog.lastCommandId;
}

static int runMacroMenuIntrinsic(const std::string &name, const std::vector<Value> &args) {
	MacroMenuRequest request;

	request.horizontal = name == "BAR_MENU";
	if (args.size() == 1) {
		request.menuSpec = valueAsString(args[0]);
	} else if (args.size() == 2) {
		request.title = valueAsString(args[0]);
		request.menuSpec = valueAsString(args[1]);
	} else if (args.size() == 3) {
		request.start = valueAsInt(args[0]);
		request.title = valueAsString(args[1]);
		request.menuSpec = valueAsString(args[2]);
	} else if (args.size() == 5) {
		request.x = valueAsInt(args[0]);
		request.y = valueAsInt(args[1]);
		request.start = valueAsInt(args[2]);
		request.title = valueAsString(args[3]);
		request.menuSpec = valueAsString(args[4]);
	} else {
		throw std::runtime_error(name + " expects 1, 2, 3 or 5 arguments.");
	}
	return runMacroMenuDialog(request);
}

static std::string runMacroStringInputIntrinsic(const std::vector<Value> &args) {
	MacroStringInputRequest request;

	request.title = "STRING INPUT";
	if (args.size() == 1) {
		request.initialValue = valueAsString(args[0]);
		request.width = std::max(20, static_cast<int>(request.initialValue.size()) + 2);
	} else if (args.size() == 2) {
		request.title = valueAsString(args[0]);
		request.initialValue = valueAsString(args[1]);
		request.width = std::max(20, static_cast<int>(request.initialValue.size()) + 2);
	} else if (args.size() == 3) {
		request.title = valueAsString(args[0]);
		request.initialValue = valueAsString(args[1]);
		request.width = std::max(8, valueAsInt(args[2]));
	} else if (args.size() == 5) {
		request.x = valueAsInt(args[0]);
		request.y = valueAsInt(args[1]);
		request.width = std::max(8, valueAsInt(args[2]));
		request.title = valueAsString(args[3]);
		request.initialValue = valueAsString(args[4]);
	} else {
		throw std::runtime_error("STRING_IN expects 1, 2, 3 or 5 arguments.");
	}
	return runMacroStringInputDialog(request);
}

static int currentUiMacroMode() {
	MREditWindow *win = activeMacroEditWindow();
	if (win != nullptr && win->isCommunicationWindow()) return MACRO_MODE_DOS_SHELL;
	return MACRO_MODE_EDIT;
}

static bool macroAllowsUiMode(const MacroRef &macroRef, int mode) noexcept {
	return macroRef.fromMode == MACRO_MODE_ALL || macroRef.fromMode == mode;
}

static bool executeLoadedMacro(std::map<std::string, MacroRef>::iterator macroIt, const std::string &macroKey, const std::string &paramPart, std::vector<std::string> *logSink) {
	std::map<std::string, LoadedMacroFile>::iterator fit;
	VirtualMachine childVm;
	bool backgroundStaged = currentBackgroundEditSession() != nullptr;
	bool childFirstRun;
	bool childDump;
	bool childTransient;
	std::string childFileKey;

	if (macroIt == g_runtimeEnv.loadedMacros.end()) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	fit = g_runtimeEnv.loadedFiles.find(macroIt->second.fileKey);
	if (fit == g_runtimeEnv.loadedFiles.end()) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	if (backgroundStaged) {
		if (fit->second.bytecode.empty() || !currentBackgroundChildMacroAllowed(fit->second)) {
			runtimeErrorLevel() = 5001;
			return false;
		}
	} else if (!ensureLoadedFileResident(macroIt->second.fileKey))
		return false;

	fit = g_runtimeEnv.loadedFiles.find(macroIt->second.fileKey);
	if (fit == g_runtimeEnv.loadedFiles.end() || fit->second.bytecode.empty()) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	childFirstRun = macroIt->second.firstRunPending;
	childDump = macroIt->second.dumpAttr;
	childTransient = macroIt->second.transientAttr;
	childFileKey = macroIt->second.fileKey;
	macroIt->second.firstRunPending = false;

	childVm.executeAt(fit->second.bytecode.data(), fit->second.bytecode.size(), macroIt->second.entryOffset, paramPart, macroIt->second.displayName, false, childFirstRun);
	if (logSink != nullptr) logSink->insert(logSink->end(), childVm.log.begin(), childVm.log.end());
	if (childDump) unloadMacroFromRegistry(macroKey);
	else if (childTransient)
		evictTransientFileImage(childFileKey);
	runtimeErrorLevel() = 0;
	return true;
}

static bool executeLoadedMacroWithConfiguredKeymapBatch(std::map<std::string, MacroRef>::iterator macroIt, const std::string &macroKey, const std::string &paramPart, std::vector<std::string> *logSink) {
	bool configuredKeymapBatch = false;
	std::string keymapBatchError;
	const auto fileIt = g_runtimeEnv.loadedFiles.find(macroIt->second.fileKey);

	if (fileIt != g_runtimeEnv.loadedFiles.end()) {
		const std::vector<std::string> unsupported = mrvmUnsupportedStagedSymbols(fileIt->second.profile);
		for (const std::string &symbol : unsupported)
			if (symbol == "KEYMAP_RESET" || symbol == "KEYMAP_PROFILE" || symbol == "KEYMAP_BIND" || symbol == "ACTIVE_KEYMAP_PROFILE") {
				configuredKeymapBatch = true;
				break;
			}
	}
	if (configuredKeymapBatch) mrvmBeginConfiguredKeymapBatch();
	const bool executed = executeLoadedMacro(macroIt, macroKey, paramPart, logSink);
	if (configuredKeymapBatch && !mrvmEndConfiguredKeymapBatch(&keymapBatchError)) {
		runtimeErrorLevel() = 1001;
		if (logSink != nullptr) logSink->push_back("VM Error: keymap batch flush failed: " + (keymapBatchError.empty() ? std::string("invalid keymap batch.") : keymapBatchError));
		return false;
	}
	return executed;
}

static bool parseRunMacroSpec(const std::string &spec, std::string &filePart, std::string &macroPart, std::string &paramPart) {
	std::string trimmed = trimAscii(spec);
	std::size_t spacePos;
	std::string head;
	std::size_t caretPos;

	filePart.clear();
	macroPart.clear();
	paramPart.clear();

	if (trimmed.empty()) return false;

	spacePos = trimmed.find_first_of(" \t\r\n");
	if (spacePos == std::string::npos) head = trimmed;
	else {
		head = trimmed.substr(0, spacePos);
		paramPart = trimAscii(trimmed.substr(spacePos + 1));
	}

	caretPos = head.find('^');
	if (caretPos == std::string::npos) macroPart = head;
	else {
		filePart = head.substr(0, caretPos);
		macroPart = head.substr(caretPos + 1);
	}
	return !macroPart.empty();
}

static bool parseBindingModeValue(int rawMode, int &mode) noexcept {
	if (rawMode == MACRO_MODE_EDIT || rawMode == MACRO_MODE_DOS_SHELL || rawMode == MACRO_MODE_ALL) {
		mode = rawMode;
		return true;
	}
	return false;
}

static bool bindingModeMatches(int bindingMode, int currentMode) noexcept {
	return bindingMode == MACRO_MODE_ALL || bindingMode == currentMode;
}

static bool isAsciiLetterKeyCode(ushort code) noexcept {
	return code >= 'A' && code <= 'Z';
}

static bool isCalculatorHotkey(const TKey &key) noexcept {
	return (key.mods & kbAltShift) != 0 && key.code == 'C';
}

static void logCalculatorHotkeyState(const char *stage, const TKey &key, std::string_view detail = {}) {
	if (!isCalculatorHotkey(key)) return;
	char line[320];
	if (detail.empty()) std::snprintf(line, sizeof(line), "KEYDBG calc stage=%s code=0x%04X mods=0x%04X", stage, static_cast<unsigned>(key.code), static_cast<unsigned>(key.mods));
	else
		std::snprintf(line, sizeof(line), "KEYDBG calc stage=%s code=0x%04X mods=0x%04X %.*s", stage, static_cast<unsigned>(key.code), static_cast<unsigned>(key.mods), static_cast<int>(detail.size()), detail.data());
	mrLogMessage(line);
}

static bool bindingKeysEqual(const TKey &lhs, const TKey &rhs) noexcept {
	const ushort lhsModsSansShift = lhs.mods & static_cast<ushort>(~kbShift);
	const ushort rhsModsSansShift = rhs.mods & static_cast<ushort>(~kbShift);

	if (lhs == rhs) return true;
	if (lhs.code != rhs.code || !isAsciiLetterKeyCode(lhs.code)) return false;
	if ((lhs.mods & kbAltShift) == 0 || (rhs.mods & kbAltShift) == 0) return false;
	return lhsModsSansShift == rhsModsSansShift;
}

static bool parseBindingKeyValue(const Value &value, TKey &key) {
	if (value.type == TYPE_INT) {
		const int encoded = valueAsInt(value);
		const ushort code = static_cast<ushort>(encoded & 0xFFFF);
		const ushort mods = static_cast<ushort>((static_cast<unsigned int>(encoded) >> 16) & 0xFFFF);
		key = TKey(code, mods);
		return code != kbNoKey;
	}
	if (!isStringLike(value)) return false;
	return parseAssignedKeySpec(valueAsString(value), key);
}

static void removeExplicitBindingsForKey(const TKey &key, int mode) {
	auto &bindings = g_runtimeEnv.explicitKeyBindings;
	bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [&](const ExplicitKeyBinding &binding) { return binding.mode == mode && bindingKeysEqual(binding.key, key); }), bindings.end());
}

static void clearRegisteredBindingsForKey(const TKey *key, int mode, bool clearAllModes) {
	for (auto &entry : g_runtimeEnv.loadedMacros) {
		MacroRef &macroRef = entry.second;
		if (!macroRef.hasAssignedKey) continue;
		if (!clearAllModes && macroRef.fromMode != mode) continue;
		if (key != nullptr && !bindingKeysEqual(macroRef.assignedKey, *key)) continue;
		macroRef.hasAssignedKey = false;
		macroRef.assignedKeySpec.clear();
	}
	g_runtimeEnv.indexedBoundMacros.erase(std::remove_if(g_runtimeEnv.indexedBoundMacros.begin(), g_runtimeEnv.indexedBoundMacros.end(),
	                                                     [&](const IndexedBoundMacroEntry &entry) {
		                                                     if (key != nullptr && !bindingKeysEqual(entry.key, *key)) return false;
		                                                     return clearAllModes || mode == MACRO_MODE_ALL || mode == MACRO_MODE_EDIT || mode == MACRO_MODE_DOS_SHELL;
	                                                     }),
	                                      g_runtimeEnv.indexedBoundMacros.end());
}

static bool executeRuntimeMacroSpec(const std::string &spec, std::vector<std::string> *logLines) {
	std::string filePart;
	std::string macroPart;
	std::string paramPart;
	std::string targetFileKey;
	std::string macroKey;
	std::map<std::string, MacroRef>::iterator macroIt;

	if (!parseRunMacroSpec(spec, filePart, macroPart, paramPart)) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	macroKey = upperKey(macroPart);
	if (!filePart.empty()) targetFileKey = resolveLoadedFileKeyForSpec(filePart);
	if (!filePart.empty() && targetFileKey.empty()) targetFileKey = makeFileKey(filePart);

	macroIt = g_runtimeEnv.loadedMacros.find(macroKey);
	if (macroIt == g_runtimeEnv.loadedMacros.end() || (!targetFileKey.empty() && macroIt->second.fileKey != targetFileKey)) {
		if (!filePart.empty()) {
			if (!loadMacroFileIntoRegistry(filePart, &targetFileKey)) return false;
		} else {
			if (!loadMacroFileIntoRegistry(macroPart, &targetFileKey)) return false;
		}
		macroIt = g_runtimeEnv.loadedMacros.find(macroKey);
	}

	if (macroIt == g_runtimeEnv.loadedMacros.end() || (!targetFileKey.empty() && macroIt->second.fileKey != targetFileKey)) {
		runtimeErrorLevel() = 5001;
		return false;
	}
	return executeLoadedMacroWithConfiguredKeymapBatch(macroIt, macroKey, paramPart, logLines);
}

static bool currentExecutingMacroSpecImpl(std::string &macroSpec) {
	const std::string macroDisplayName = !g_runtimeEnv.macroStack.empty() ? trimAscii(g_runtimeEnv.macroStack.back().macroName) : std::string();
	const auto macroIt = g_runtimeEnv.loadedMacros.find(upperKey(macroDisplayName));
	std::string fileDisplayName;

	macroSpec.clear();
	if (macroDisplayName.empty() || macroIt == g_runtimeEnv.loadedMacros.end()) return false;

	{
		const auto fileIt = g_runtimeEnv.loadedFiles.find(macroIt->second.fileKey);

		if (fileIt == g_runtimeEnv.loadedFiles.end()) return false;
		fileDisplayName = !fileIt->second.displayName.empty() ? fileIt->second.displayName : fileIt->second.resolvedPath;
	}
	if (fileDisplayName.empty()) return false;
	macroSpec = fileDisplayName + "^" + macroIt->second.displayName;
	return true;
}


static bool composeLoadedMacroSpec(const MacroRef &macroRef, std::string &macroSpec) {
	std::string fileDisplayName;

	macroSpec.clear();
	{
		const auto fileIt = g_runtimeEnv.loadedFiles.find(macroRef.fileKey);

		if (fileIt == g_runtimeEnv.loadedFiles.end()) return false;
		fileDisplayName = !fileIt->second.displayName.empty() ? fileIt->second.displayName : fileIt->second.resolvedPath;
	}
	if (fileDisplayName.empty() || macroRef.displayName.empty()) return false;
	macroSpec = fileDisplayName + "^" + macroRef.displayName;
	return true;
}

static std::string normalizeMenuKeySpec(std::string keySpec) {
	keySpec = trimAscii(keySpec);
	if (keySpec.size() >= 2 && keySpec.front() == '<' && keySpec.back() == '>') keySpec = keySpec.substr(1, keySpec.size() - 2);
	return keySpec;
}

static std::string menuLabelFromBindingKey(const TKey &key) {
	struct ComboSpec {
		const char *prefix;
		ushort mods;
	};
	struct NamedKeySpec {
		const char *token;
		ushort code;
	};
	static const ComboSpec combos[] = {{"", 0}, {"Shft", kbShift}, {"Ctrl", kbCtrlShift}, {"Alt", kbAltShift}, {"CtrlShft", static_cast<ushort>(kbCtrlShift | kbShift)}, {"AltShft", static_cast<ushort>(kbAltShift | kbShift)}, {"CtrlAlt", static_cast<ushort>(kbCtrlShift | kbAltShift)}, {"CtrlAltShft", static_cast<ushort>(kbCtrlShift | kbAltShift | kbShift)}, {"Super", kbSuperShift}, {"SuperShft", static_cast<ushort>(kbSuperShift | kbShift)}, {"SuperCtrl", static_cast<ushort>(kbSuperShift | kbCtrlShift)}, {"SuperAlt", static_cast<ushort>(kbSuperShift | kbAltShift)}, {"SuperCtrlShft", static_cast<ushort>(kbSuperShift | kbCtrlShift | kbShift)}, {"SuperAltShft", static_cast<ushort>(kbSuperShift | kbAltShift | kbShift)}, {"SuperCtrlAlt", static_cast<ushort>(kbSuperShift | kbCtrlShift | kbAltShift)}, {"SuperCtrlAltShft", static_cast<ushort>(kbSuperShift | kbCtrlShift | kbAltShift | kbShift)}};
	static const NamedKeySpec named[] = {{"Enter", kbEnter}, {"Tab", kbTab}, {"Esc", kbEsc}, {"Backspace", kbBack}, {"Up", kbUp}, {"Down", kbDown}, {"Left", kbLeft}, {"Right", kbRight}, {"PgUp", kbPgUp}, {"PgDn", kbPgDn}, {"Home", kbHome}, {"End", kbEnd}, {"Ins", kbIns}, {"Del", kbDel}, {"Grey-", kbGrayMinus}, {"Grey+", kbGrayPlus}, {"Grey*", static_cast<ushort>('*')}, {"Space", static_cast<ushort>(' ')}, {"Minus", static_cast<ushort>('-')}, {"Equal", static_cast<ushort>('=')}, {"F1", kbF1}, {"F2", kbF2}, {"F3", kbF3}, {"F4", kbF4}, {"F5", kbF5}, {"F6", kbF6}, {"F7", kbF7}, {"F8", kbF8}, {"F9", kbF9}, {"F10", kbF10}, {"F11", kbF11}, {"F12", kbF12}};
	for (const ComboSpec &combo : combos)
		for (const NamedKeySpec &entry : named)
			if (key == TKey(entry.code, combo.mods)) return std::string(combo.prefix) + entry.token;
	for (const ComboSpec &combo : combos) {
		for (char c = 'A'; c <= 'Z'; ++c)
			if (key == TKey(static_cast<ushort>(c), combo.mods)) return std::string(combo.prefix) + c;
		for (char c = '0'; c <= '9'; ++c)
			if (key == TKey(static_cast<ushort>(c), combo.mods)) return std::string(combo.prefix) + c;
	}
	if (key.code != kbNoKey && key.code < 256 && std::isprint(static_cast<unsigned char>(key.code)) != 0) return std::string(1, static_cast<char>(key.code));
	return std::string();
}

static bool macroSpecTargetsLoadedMacro(const std::string &spec, const std::string &targetFileKey, const std::string &targetMacroKey) {
	std::string filePart;
	std::string macroPart;
	std::string paramPart;
	const bool parsed = parseRunMacroSpec(spec, filePart, macroPart, paramPart);

	if (!parsed || upperKey(macroPart) != targetMacroKey) return false;
	if (targetFileKey.empty()) return true;
	if (filePart.empty()) return false;
	return fileSpecMatchesLoadedFileKey(filePart, targetFileKey);
}

static bool dispatchEditorCommandEvent(ushort command) {
	MRFileEditor *editor = currentEditor();
	TEvent event;

	if (editor == nullptr) return false;
	std::memset(&event, 0, sizeof(event));
	event.what = evCommand;
	event.message.command = command;
	editor->handleEvent(event);
	return true;
}

static bool dispatchApplicationCommandEvent(ushort command) {
	TApplication *app = dynamic_cast<TApplication *>(TProgram::application);
	TEvent event;

	if (app == nullptr) return false;
	std::memset(&event, 0, sizeof(event));
	event.what = evCommand;
	event.message.command = command;
	app->handleEvent(event);
	return true;
}

static bool executeBoundCommand(int commandId) {
	MRFileEditor *editor = currentEditor();

	switch (commandId) {
		case macdBackSpace:
			backspaceEditor(editor);
			return true;
		case macdBlockBegin:
		case macdBlockEnd:
		case macdBlockOff:
		case macdColBlockBegin:
		case macdCopyBlock:
			return true;
		case macdCr:
			carriageReturnEditor(editor);
			return true;
		case macdDeleteBlock:
			return true;
		case macdDelChar:
			deleteEditorChars(editor, 1);
			return true;
		case macdDelLine:
			deleteEditorLine(editor);
			return true;
		case macdDown:
			return moveEditorDown(editor);
		case macdEof:
			return moveEditorEof(editor);
		case macdEol:
			return moveEditorEol(editor);
		case macdFirstWord:
			return moveEditorFirstWord(editor);
		case macdGotoMark:
			return gotoEditorMark(activeMacroEditWindow(), editor);
		case macdHome:
			return moveEditorHome(editor);
		case macdIndent:
			return indentEditor(editor);
		case macdKeyRecord:
			return dispatchApplicationCommandEvent(cmMrMacroToggleRecording);
		case macdLastPageBreak:
			return moveEditorLastPageBreak(editor);
		case macdLeft:
			return moveEditorLeft(editor);
		case macdMarkPos:
			return markEditorPosition(activeMacroEditWindow(), editor);
		case macdMoveBlock:
			return true;
		case macdNextPageBreak:
			return moveEditorNextPageBreak(editor);
		case macdPageDown:
			return moveEditorPageDown(editor);
		case macdPageUp:
			return moveEditorPageUp(editor);
		case macdRight:
			return moveEditorRight(editor);
		case macdSaveFile:
			return activeMacroEditWindow() != nullptr && activeMacroEditWindow()->saveCurrentFile();
		case macdStrBlockBegin:
			return true;
		case macdTabLeft:
			return moveEditorTabLeft(editor);
		case macdTabRight:
			return moveEditorTabRight(editor);
		case macdTof:
			return moveEditorTof(editor);
		case macdUndent:
			return undentEditor(editor);
		case macdUndo:
			return dispatchEditorCommandEvent(cmMrEditUndo);
		case macdUp:
			return moveEditorUp(editor);
		case macdWordLeft:
			return moveEditorWordLeft(editor);
		case macdWordRight:
			return moveEditorWordRight(editor);
		default:
			return false;
	}
}

static bool executeExplicitKeyBinding(const TKey &pressed, int mode, std::vector<std::string> *logLines) {
	logCalculatorHotkeyState("vm-explicit-enter", pressed);
	for (std::size_t i = g_runtimeEnv.explicitKeyBindings.size(); i > 0; --i) {
		const ExplicitKeyBinding &binding = g_runtimeEnv.explicitKeyBindings[i - 1];
		if (!bindingKeysEqual(binding.key, pressed) || !bindingModeMatches(binding.mode, mode)) continue;
		if (binding.kind == ExplicitBindingKind::MacroSpec) logCalculatorHotkeyState("vm-explicit-match", pressed, binding.macroSpec);
		else
			logCalculatorHotkeyState("vm-explicit-match-cmd", pressed);
		if (binding.kind == ExplicitBindingKind::MacroSpec) return executeRuntimeMacroSpec(binding.macroSpec, logLines);
		runtimeErrorLevel() = executeBoundCommand(binding.commandId) ? 0 : 1001;
		return runtimeErrorLevel() == 0;
	}
	return false;
}

static bool fileContainsOnlyTransientMacros(const LoadedMacroFile &file) {
	if (file.macroNames.empty()) return false;
	for (const auto &macroName : file.macroNames) {
		std::map<std::string, MacroRef>::const_iterator mit = g_runtimeEnv.loadedMacros.find(macroName);
		if (mit == g_runtimeEnv.loadedMacros.end() || !mit->second.transientAttr) return false;
	}
	return true;
}

static bool refreshLoadedFileBytecode(const std::string &fileKey) {
	std::map<std::string, LoadedMacroFile>::iterator fit = g_runtimeEnv.loadedFiles.find(fileKey);
	std::string source;
	unsigned char *compiled = nullptr;
	size_t compiledSize = 0;
	int macroCount;

	if (fit == g_runtimeEnv.loadedFiles.end()) return false;
	if (fit->second.resolvedPath.empty() || !readTextFile(fit->second.resolvedPath, source)) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	compiled = compile_macro_code(source.c_str(), &compiledSize);
	if (compiled == nullptr) {
		runtimeErrorLevel() = 5005;
		return false;
	}

	fit->second.bytecode.assign(compiled, compiled + compiledSize);
	std::free(compiled);
	fit->second.profile = mrvmAnalyzeBytecode(fit->second.bytecode.data(), fit->second.bytecode.size());

	macroCount = get_compiled_macro_count();
	for (int i = 0; i < macroCount; ++i) {
		const char *macroNameText = get_compiled_macro_name(i);
		int entry = get_compiled_macro_entry(i);
		int flags = get_compiled_macro_flags(i);
		const char *keyspecText = get_compiled_macro_keyspec(i);
		int mode = get_compiled_macro_mode(i);
		std::string displayName = macroNameText != nullptr ? macroNameText : std::string();
		std::string macroKey = upperKey(displayName);
		std::map<std::string, MacroRef>::iterator mit = g_runtimeEnv.loadedMacros.find(macroKey);

		if (displayName.empty() || entry < 0) continue;
		if (mit == g_runtimeEnv.loadedMacros.end() || mit->second.fileKey != fileKey) continue;

		mit->second.displayName = displayName;
		mit->second.entryOffset = static_cast<std::size_t>(entry);
		mit->second.transientAttr = (flags & MACRO_ATTR_TRANS) != 0;
		mit->second.dumpAttr = (flags & MACRO_ATTR_DUMP) != 0;
		mit->second.permAttr = (flags & MACRO_ATTR_PERM) != 0;
		mit->second.assignedKeySpec = keyspecText != nullptr ? keyspecText : std::string();
		mit->second.fromMode = (mode == MACRO_MODE_DOS_SHELL || mode == MACRO_MODE_ALL) ? mode : MACRO_MODE_EDIT;
		mit->second.hasAssignedKey = false;
		if (!mit->second.assignedKeySpec.empty()) mit->second.hasAssignedKey = parseAssignedKeySpec(mit->second.assignedKeySpec, mit->second.assignedKey);
	}

	runtimeErrorLevel() = 0;
	logMacroProfileLine("Refreshed macro file", fit->second);
	return true;
}

static bool ensureLoadedFileResident(const std::string &fileKey) {
	std::map<std::string, LoadedMacroFile>::iterator fit = g_runtimeEnv.loadedFiles.find(fileKey);
	if (fit == g_runtimeEnv.loadedFiles.end()) return false;
	if (!fit->second.bytecode.empty()) return true;
	return refreshLoadedFileBytecode(fileKey);
}

static bool evictTransientFileImage(const std::string &fileKey) {
	std::map<std::string, LoadedMacroFile>::iterator fit = g_runtimeEnv.loadedFiles.find(fileKey);
	if (fit == g_runtimeEnv.loadedFiles.end()) return false;
	if (!fileContainsOnlyTransientMacros(fit->second)) return false;
	for (const auto &macroName : fit->second.macroNames)
		if (macroIsRunning(macroName)) return false;
	fit->second.bytecode.clear();
	fit->second.bytecode.shrink_to_fit();
	return true;
}

static bool currentBackgroundChildMacroAllowed(const LoadedMacroFile &file) noexcept {
	if (currentBackgroundEditSession() != nullptr) return mrvmCanRunInBackground(file.profile) || mrvmCanRunStagedInBackground(file.profile);
	return false;
}

static bool loadMacroFileIntoRegistry(const std::string &spec, std::string *loadedFileKey) {
	std::string resolvedPath = resolveMacroFilePath(spec);
	std::string fileKey = makeFileKey(spec);
	std::string source;
	LoadedMacroFile newFile;
	unsigned char *compiled = nullptr;
	size_t compiledSize = 0;
	int macroCount;

	if (loadedFileKey != nullptr) loadedFileKey->clear();

	if (resolvedPath.empty() || !readTextFile(resolvedPath, source)) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	std::map<std::string, LoadedMacroFile>::iterator existingFile = g_runtimeEnv.loadedFiles.find(fileKey);
	if (existingFile != g_runtimeEnv.loadedFiles.end()) {
		for (const auto &macroName : existingFile->second.macroNames)
			if (macroIsRunning(macroName)) {
				runtimeErrorLevel() = 5006;
				return false;
			}
	}

	compiled = compile_macro_code(source.c_str(), &compiledSize);
	if (compiled == nullptr) {
		runtimeErrorLevel() = 5005;
		return false;
	}

	macroCount = get_compiled_macro_count();
	for (int i = 0; i < macroCount; ++i) {
		const char *macroNameText = get_compiled_macro_name(i);
		std::string displayName = macroNameText != nullptr ? macroNameText : std::string();
		std::string macroKey = upperKey(displayName);
		std::map<std::string, MacroRef>::iterator mit;

		if (displayName.empty()) continue;

		mit = g_runtimeEnv.loadedMacros.find(macroKey);
		if (mit != g_runtimeEnv.loadedMacros.end()) {
			if (macroIsRunning(macroKey) || mit->second.permAttr) {
				std::free(compiled);
				runtimeErrorLevel() = 5006;
				return false;
			}
		}
	}

	newFile.fileKey = fileKey;
	newFile.displayName = trimAscii(spec);
	newFile.resolvedPath = resolvedPath;
	newFile.bytecode.assign(compiled, compiled + compiledSize);
	std::free(compiled);
	newFile.profile = mrvmAnalyzeBytecode(newFile.bytecode.data(), newFile.bytecode.size());

	if (existingFile != g_runtimeEnv.loadedFiles.end()) {
		std::vector<std::string> oldNames = existingFile->second.macroNames;
		for (const auto &oldName : oldNames)
			removeMacroFromRegistryByKey(oldName);
	}

	for (int i = 0; i < macroCount; ++i) {
		const char *macroNameText = get_compiled_macro_name(i);
		int entry = get_compiled_macro_entry(i);
		int flags = get_compiled_macro_flags(i);
		const char *keyspecText = get_compiled_macro_keyspec(i);
		int mode = get_compiled_macro_mode(i);
		std::string displayName = macroNameText != nullptr ? macroNameText : std::string();
		std::string macroKey = upperKey(displayName);
		MacroRef ref;

		if (displayName.empty() || entry < 0) continue;
		removeMacroFromRegistryByKey(macroKey);

		ref.fileKey = fileKey;
		ref.displayName = displayName;
		ref.entryOffset = static_cast<std::size_t>(entry);
		ref.firstRunPending = true;
		ref.transientAttr = (flags & MACRO_ATTR_TRANS) != 0;
		ref.dumpAttr = (flags & MACRO_ATTR_DUMP) != 0;
		ref.permAttr = (flags & MACRO_ATTR_PERM) != 0;
		ref.assignedKeySpec = keyspecText != nullptr ? keyspecText : std::string();
		ref.fromMode = (mode == MACRO_MODE_DOS_SHELL || mode == MACRO_MODE_ALL) ? mode : MACRO_MODE_EDIT;
		ref.hasAssignedKey = false;
		if (!ref.assignedKeySpec.empty()) ref.hasAssignedKey = parseAssignedKeySpec(ref.assignedKeySpec, ref.assignedKey);
		g_runtimeEnv.loadedMacros[macroKey] = ref;
		g_runtimeEnv.macroOrder.push_back(macroKey);
		newFile.macroNames.push_back(macroKey);
	}

	g_runtimeEnv.loadedFiles[fileKey] = newFile;
	runtimeErrorLevel() = 0;
	logMacroProfileLine("Loaded macro file", newFile);
	static_cast<void>(mrvmUiRefreshRuntimeMenus(nullptr));
	if (loadedFileKey != nullptr) *loadedFileKey = fileKey;
	return true;
}

static bool tryLoadIndexedMacroForKey(const TKey &pressed) {
	logCalculatorHotkeyState("vm-indexed-enter", pressed);
	for (std::size_t i = 0; i < g_runtimeEnv.indexedBoundMacros.size(); ++i) {
		const IndexedBoundMacroEntry &entry = g_runtimeEnv.indexedBoundMacros[i];
		std::string fileKey;

		if (!bindingKeysEqual(entry.key, pressed)) continue;
		logCalculatorHotkeyState("vm-indexed-match", pressed, entry.filePath);
		fileKey = makeFileKey(entry.filePath);
		if (g_runtimeEnv.loadedFiles.find(fileKey) != g_runtimeEnv.loadedFiles.end()) return true;
		g_runtimeEnv.indexedWarmupAttemptedFiles.insert(fileKey);
		if (loadMacroFileIntoRegistry(entry.filePath, nullptr)) return true;
	}
	return false;
}

static bool unloadMacroFromRegistry(const std::string &macroName) {
	std::string macroKey = upperKey(trimAscii(macroName));
	if (macroKey.empty()) return false;
	if (macroIsRunning(macroKey)) {
		runtimeErrorLevel() = 5006;
		return false;
	}
	if (!removeMacroFromRegistryByKey(macroKey)) return false;
	runtimeErrorLevel() = 0;
	return true;
}

static bool applyHashIntrinsic(VirtualMachine &vm, const std::string &name, const std::vector<Value> &args, Value &out) {
	if (name == "GLOBAL_HASH") {
		GlobalEntry entry;
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("GLOBAL_HASH expects one string argument.");
		if (!readGlobalValue(valueAsString(args[0]), entry) || entry.type != TYPE_HASH || entry.value.type != TYPE_HASH) {
			Value value = makeHash(g_runtimeEnv.globalHashStore.createHash(), true);
			setGlobalValue(valueAsString(args[0]), TYPE_HASH, value);
			out = value;
			return true;
		}
		out = entry.value;
		return true;
	}
	if (name == "EXISTS") {
		if (args.size() != 2 || args[0].type != TYPE_HASH || !isStringLike(args[1])) throw std::runtime_error("EXISTS expects (hash, string).");
		out = makeInt(mrvmHashContainsValue(vm.localHashStore(), g_runtimeEnv.globalHashStore, args[0], valueAsString(args[1])) ? 1 : 0);
		return true;
	}
	if (name == "HAS_VALUE") {
		if (args.size() != 2 || args[0].type != TYPE_HASH || !isStringLike(args[1])) throw std::runtime_error("HAS_VALUE expects (hash, string).");
		const std::string key = valueAsString(args[1]);
		if (!mrvmHashContainsValue(vm.localHashStore(), g_runtimeEnv.globalHashStore, args[0], key)) {
			out = makeInt(0);
			return true;
		}
		out = makeInt(valueHasContent(mrvmHashReadValue(vm.localHashStore(), g_runtimeEnv.globalHashStore, args[0], key)) ? 1 : 0);
		return true;
	}
	if (name == "KEYS") {
		Value result = mrvmMakeArrayValue(TYPE_STR);
		if (args.size() != 1 || args[0].type != TYPE_HASH) throw std::runtime_error("KEYS expects one hash argument.");
		for (const std::string &key : mrvmHashRuntimeStoreForValue(vm.localHashStore(), g_runtimeEnv.globalHashStore, args[0]).keys(args[0].hashHandle))
			result.arrayValues.push_back(makeString(key));
		out = result;
		return true;
	}
	if (name == "VALUES") {
		Value result = mrvmMakeArrayValue(TYPE_STR);
		if (args.size() != 1 || args[0].type != TYPE_HASH) throw std::runtime_error("VALUES expects one hash argument.");
		for (const Value &value : mrvmHashRuntimeStoreForValue(vm.localHashStore(), g_runtimeEnv.globalHashStore, args[0]).values(args[0].hashHandle))
			result.arrayValues.push_back(makeString(valueAsString(value)));
		out = result;
		return true;
	}
	return false;
}

static Value applyIntrinsic(VirtualMachine &vm, const std::string &name, const std::vector<Value> &args) {
	if (name == "STR") {
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("STR expects one integer argument.");
		return makeString(valueAsString(args[0]));
	}
	if (name == "CHAR") {
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("CHAR expects one integer argument.");
		return makeChar(static_cast<unsigned char>(args[0].i & 0xFF));
	}
	if (name == "UTF8") {
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("UTF8 expects one integer argument.");
		return makeString(utf8FromCodepoint(static_cast<std::uint32_t>(args[0].i)));
	}
	if (name == "ASCII") {
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("ASCII expects one string argument.");
		std::string s = valueAsString(args[0]);
		return makeInt(s.empty() ? 0 : static_cast<unsigned char>(s[0]));
	}
	if (name == "CAPS") {
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("CAPS expects one string argument.");
		return makeString(upperKey(valueAsString(args[0])));
	}
	if (name == "COPY") {
		std::string s;
		int pos;
		int count;
		std::size_t start;
		if (args.size() != 3 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("COPY expects (string, int, int).");
		s = valueAsString(args[0]);
		pos = checkedStringIndex(args[1].i);
		count = args[2].i;
		if (count < 0) throw std::runtime_error("Invalid string index on string copy operation.");
		if (static_cast<std::size_t>(pos) > s.size()) return makeString("");
		start = static_cast<std::size_t>(pos - 1);
		return makeString(s.substr(start, static_cast<std::size_t>(count)));
	}
		if (name == "LENGTH") {
			if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("LENGTH expects one string argument.");
			return makeInt(static_cast<int>(valueAsString(args[0]).size()));
		}
		if (name == "LEN") {
			if (args.size() != 1 || (!isStringLike(args[0]) && !mrvmValueIsArrayType(args[0].type))) throw std::runtime_error("LEN expects one string or array argument.");
			if (mrvmValueIsArrayType(args[0].type)) return makeInt(static_cast<int>(args[0].arrayValues.size()));
			return makeInt(static_cast<int>(valueAsString(args[0]).size()));
		}
		if (name == "POS") {
			std::string needle;
			std::string haystack;
		std::size_t pos;
		if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1])) throw std::runtime_error("POS expects (substring, string).");
		needle = valueAsString(args[0]);
		haystack = valueAsString(args[1]);
		if (needle.empty()) return makeInt(1);
		pos = haystack.find(needle);
		return makeInt(pos == std::string::npos ? 0 : static_cast<int>(pos + 1));
	}
	if (name == "XPOS") {
		std::string needle;
		std::string haystack;
		int startPos;
		std::size_t pos;
		if (args.size() != 3 || !isStringLike(args[0]) || !isStringLike(args[1]) || args[2].type != TYPE_INT) throw std::runtime_error("XPOS expects (substring, string, int).");
		needle = valueAsString(args[0]);
		haystack = valueAsString(args[1]);
		startPos = checkedStringIndex(args[2].i);
		if (needle.empty()) return makeInt(startPos <= static_cast<int>(haystack.size()) + 1 ? startPos : 0);
		if (static_cast<std::size_t>(startPos) > haystack.size()) return makeInt(0);
		pos = haystack.find(needle, static_cast<std::size_t>(startPos - 1));
		return makeInt(pos == std::string::npos ? 0 : static_cast<int>(pos + 1));
	}
	if (name == "STR_DEL") {
		std::string s;
		int pos;
		int count;
		std::size_t start;
		if (args.size() != 3 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("STR_DEL expects (string, int, int).");
		s = valueAsString(args[0]);
		pos = checkedStringIndex(args[1].i);
		count = args[2].i;
		if (count < 0) throw std::runtime_error("Invalid string index on string copy operation.");
		if (static_cast<std::size_t>(pos) > s.size()) return makeString(s);
		start = static_cast<std::size_t>(pos - 1);
		s.erase(start, static_cast<std::size_t>(count));
		return makeString(s);
	}
	if (name == "STR_INS") {
		std::string target;
		std::string dest;
		int location;
		std::size_t insertPos;
		if (args.size() != 3 || !isStringLike(args[0]) || !isStringLike(args[1]) || args[2].type != TYPE_INT) throw std::runtime_error("STR_INS expects (string, string, int).");
		target = valueAsString(args[0]);
		dest = valueAsString(args[1]);
		location = checkedInsertIndex(args[2].i);
		insertPos = static_cast<std::size_t>(location);
		if (insertPos > dest.size()) insertPos = dest.size();
		dest.insert(insertPos, target);
		enforceStringLength(dest);
		return makeString(dest);
	}
	if (name == "REAL_I") {
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("REAL_I expects one integer argument.");
		return makeReal(static_cast<double>(args[0].i));
	}
	if (name == "INT_R") {
		if (args.size() != 1 || args[0].type != TYPE_REAL) throw std::runtime_error("INT_R expects one real argument.");
		if (args[0].r < static_cast<double>(std::numeric_limits<int>::min()) || args[0].r > static_cast<double>(std::numeric_limits<int>::max())) throw std::runtime_error("Real to Integer conversion out of range.");
		return makeInt(static_cast<int>(args[0].r));
	}
	if (name == "RSTR") {
		char fmt[32];
		char buf[256];
		int width;
		int precision;

		if (args.size() != 3 || args[0].type != TYPE_REAL || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("RSTR expects (real, int, int).");

		width = args[1].i;
		precision = args[2].i;
		if (width < 0) width = 0;
		if (precision < 0) precision = 0;
		if (precision > 20) precision = 20;

		std::snprintf(fmt, sizeof(fmt), "%%%d.%df", width, precision);
		std::snprintf(buf, sizeof(buf), fmt, args[0].r);
		enforceStringLength(buf);
		return makeString(buf);
	}
	if (name == "REMOVE_SPACE") {
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("REMOVE_SPACE expects one string argument.");
		return makeString(removeSpaceAscii(valueAsString(args[0])));
	}
	if (name == "GET_EXTENSION") {
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("GET_EXTENSION expects one string argument.");
		return makeString(getExtensionPart(valueAsString(args[0])));
	}
	if (name == "GET_PATH") {
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("GET_PATH expects one string argument.");
		return makeString(getPathPart(valueAsString(args[0])));
	}
	if (name == "TRUNCATE_EXTENSION") {
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("TRUNCATE_EXTENSION expects one string argument.");
		return makeString(truncateExtensionPart(valueAsString(args[0])));
	}
	if (name == "TRUNCATE_PATH") {
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("TRUNCATE_PATH expects one string argument.");
		return makeString(truncatePathPart(valueAsString(args[0])));
	}
	if (name == "FILE_EXISTS") {
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("FILE_EXISTS expects one string argument.");
		return makeInt(fileExistsPath(valueAsString(args[0])) ? 1 : 0);
	}
	if (name == "FILE_ATTR") {
		int attr = 0;
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("FILE_ATTR expects one string argument.");
		if (!readFileMetadata(valueAsString(args[0]), &attr, nullptr, nullptr)) {
			runtimeErrorLevel() = errno != 0 ? errno : 1;
			return makeInt(0);
		}
		runtimeErrorLevel() = 0;
		return makeInt(attr);
	}
	if (name == "FIRST_FILE") {
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("FIRST_FILE expects one string argument.");
		return makeInt(findFirstFileMatch(valueAsString(args[0])));
	}
	if (name == "NEXT_FILE") {
		if (!args.empty()) throw std::runtime_error("NEXT_FILE expects no arguments.");
		return makeInt(findNextFileMatch());
	}
	if (name == "SUBSHELL") {
		SubshellResult subshell;
		if (args.size() != 2 || !isStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SUBSHELL expects (string, int).");
		subshell = runSubshellCapture(valueAsString(args[0]), valueAsInt(args[1]));
		runtimeErrorLevel() = subshell.errorLevel;
		return makeString(subshell.output);
	}
	if (name == "SEARCH_FWD") {
		MRFileEditor *editor;
		std::size_t matchStart = 0;
		std::size_t matchEnd = 0;
		MREditWindow *win;
		BackgroundEditSession *session;
		if (args.size() != 2 || !isStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SEARCH_FWD expects (string, int).");
		if (valueAsString(args[0]).empty()) {
			runtimeErrorLevel() = 1010;
			return makeInt(0);
		}
		editor = currentEditor();
		session = currentBackgroundEditSession();
		if (editor == nullptr && session == nullptr) return makeInt(0);
		if (!searchEditorForward(editor, valueAsString(args[0]), valueAsInt(args[1]), currentRuntimeIgnoreCase(), matchStart, matchEnd)) {
			if (session != nullptr) session->clearLastSearch();
			else
				g_runtimeEnv.lastSearchValid = false;
			runtimeErrorLevel() = 0;
			return makeInt(0);
		}
		if (editor != nullptr) {
			editor->setCursorOffset(static_cast<uint>(matchStart), 0);
			editor->setSelectionOffsets(static_cast<uint>(matchStart), static_cast<uint>(matchEnd), False);
			editor->revealCursor(True);
		} else {
			session->cursorOffset = matchStart;
			session->selectionStart = matchStart;
			session->selectionEnd = matchEnd;
		}
		win = activeMacroEditWindow();
		if (session != nullptr) {
			session->lastSearchValid = true;
			session->lastSearchStart = matchStart;
			session->lastSearchEnd = matchEnd;
			session->lastSearchCursor = matchStart;
		} else {
			g_runtimeEnv.lastSearchValid = true;
			g_runtimeEnv.lastSearchWindow = win;
			g_runtimeEnv.lastSearchFileName = win != nullptr ? std::string(win->currentFileName()) : std::string();
			g_runtimeEnv.lastSearchStart = matchStart;
			g_runtimeEnv.lastSearchEnd = matchEnd;
			g_runtimeEnv.lastSearchCursor = matchStart;
		}
		runtimeErrorLevel() = 0;
		return makeInt(1);
	}
	if (name == "SEARCH_BWD") {
		MRFileEditor *editor;
		std::size_t matchStart = 0;
		std::size_t matchEnd = 0;
		MREditWindow *win;
		BackgroundEditSession *session;
		if (args.size() != 2 || !isStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SEARCH_BWD expects (string, int).");
		if (valueAsString(args[0]).empty()) {
			runtimeErrorLevel() = 1010;
			return makeInt(0);
		}
		editor = currentEditor();
		session = currentBackgroundEditSession();
		if (editor == nullptr && session == nullptr) return makeInt(0);
		if (!searchEditorBackward(editor, valueAsString(args[0]), valueAsInt(args[1]), currentRuntimeIgnoreCase(), matchStart, matchEnd)) {
			if (session != nullptr) session->clearLastSearch();
			else
				g_runtimeEnv.lastSearchValid = false;
			runtimeErrorLevel() = 0;
			return makeInt(0);
		}
		if (editor != nullptr) {
			editor->setCursorOffset(static_cast<uint>(matchStart), 0);
			editor->setSelectionOffsets(static_cast<uint>(matchStart), static_cast<uint>(matchEnd), False);
			editor->revealCursor(True);
		} else {
			session->cursorOffset = matchStart;
			session->selectionStart = matchStart;
			session->selectionEnd = matchEnd;
		}
		win = activeMacroEditWindow();
		if (session != nullptr) {
			session->lastSearchValid = true;
			session->lastSearchStart = matchStart;
			session->lastSearchEnd = matchEnd;
			session->lastSearchCursor = matchStart;
		} else {
			g_runtimeEnv.lastSearchValid = true;
			g_runtimeEnv.lastSearchWindow = win;
			g_runtimeEnv.lastSearchFileName = win != nullptr ? std::string(win->currentFileName()) : std::string();
			g_runtimeEnv.lastSearchStart = matchStart;
			g_runtimeEnv.lastSearchEnd = matchEnd;
			g_runtimeEnv.lastSearchCursor = matchStart;
		}
		runtimeErrorLevel() = 0;
		return makeInt(1);
	}
	if (name == "GET_ENVIRONMENT") {
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("GET_ENVIRONMENT expects one string argument.");
		return makeString(getEnvironmentValue(valueAsString(args[0])));
	}
	if (name == "GET_WORD") {
		MRFileEditor *editor;
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("GET_WORD expects one string argument.");
		editor = currentEditor();
		if (editor == nullptr && currentBackgroundEditSession() == nullptr) return makeString("");
		return makeString(currentEditorWord(editor, valueAsString(args[0])));
	}
	if (name == "PARAM_STR") {
		int index;
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("PARAM_STR expects one integer argument.");
		index = valueAsInt(args[0]);
		if (index == 0) return makeString(g_runtimeEnv.startupCommand);
		if (index < 0 || static_cast<std::size_t>(index) > g_runtimeEnv.processArgs.size()) return makeString("");
		return makeString(g_runtimeEnv.processArgs[static_cast<std::size_t>(index - 1)]);
	}
	if (name == "GLOBAL_STR") {
		std::map<std::string, GlobalEntry>::const_iterator it;
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("GLOBAL_STR expects one string argument.");
		std::string key = upperKey(valueAsString(args[0]));
		it = g_runtimeEnv.globals.find(key);
		if (it == g_runtimeEnv.globals.end() || it->second.type != TYPE_STR) return makeString("");
		return it->second.value;
	}
	if (name == "GLOBAL_INT") {
		std::map<std::string, GlobalEntry>::const_iterator it;
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("GLOBAL_INT expects one string argument.");
		std::string key = upperKey(valueAsString(args[0]));
		it = g_runtimeEnv.globals.find(key);
		if (it == g_runtimeEnv.globals.end() || it->second.type != TYPE_INT) return makeInt(0);
		return it->second.value;
	}
	{
		Value result;
		if (applyHashIntrinsic(vm, name, args, result)) return result;
	}
	if (name == "CHECK_KEY") {
		int key1 = 0;
		int key2 = 0;
		if (!args.empty()) throw std::runtime_error("CHECK_KEY expects no arguments.");
		if (readMacroKeyPair(false, key1, key2)) return makeInt(1);
		return makeInt(0);
	}
	if (name == "VERSION") {
		if (!args.empty()) throw std::runtime_error("VERSION expects no arguments.");
		return makeString(mrDisplayVersion());
	}
	if (name == "OS_BACK") {
		if (!args.empty()) throw std::runtime_error("OS_BACK expects no arguments.");
		return makeInt(0);
	}
	if (name == "OS_COLOR") {
		if (!args.empty()) throw std::runtime_error("OS_COLOR expects no arguments.");
		return makeInt(7);
	}
	if (name == "PARSE_STR") {
		if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1])) throw std::runtime_error("PARSE_STR expects (string, string).");
		return makeString(parseNamedValue(valueAsString(args[0]), valueAsString(args[1])));
	}
	if (name == "PARSE_INT") {
		std::string parsed;
		int errorPos;
		if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1])) throw std::runtime_error("PARSE_INT expects (string, string).");
		parsed = parseNamedValue(valueAsString(args[0]), valueAsString(args[1]));
		if (parsed.empty()) return makeInt(0);
		errorPos = findValErrorPosition(parsed);
		if (errorPos != 0) return makeInt(0);
		return makeInt(static_cast<int>(std::strtol(parsed.c_str(), nullptr, 10)));
	}
	if (name == "INQ_MACRO") {
		BackgroundEditSession *session;
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("INQ_MACRO expects one string argument.");
		session = currentBackgroundEditSession();
		if (session != nullptr) return makeInt(session->loadedMacroDisplayNames.find(upperKey(valueAsString(args[0]))) != session->loadedMacroDisplayNames.end() ? 1 : 0);
		return makeInt(g_runtimeEnv.loadedMacros.find(upperKey(valueAsString(args[0]))) != g_runtimeEnv.loadedMacros.end() ? 1 : 0);
	}
	if (name == "COPY_FILE") {
		std::string source;
		std::string target;
		const bool append = args.size() == 3 && valueAsInt(args[2]) != 0;
		std::ifstream in;
		std::ofstream out;

		if ((args.size() != 2 && args.size() != 3) || !isStringLike(args[0]) || !isStringLike(args[1]) || (args.size() == 3 && args[2].type != TYPE_INT)) throw std::runtime_error("COPY_FILE expects (string, string[, int]).");
		source = expandUserPath(valueAsString(args[0]));
		target = expandUserPath(valueAsString(args[1]));
		in.open(source.c_str(), std::ios::in | std::ios::binary);
		out.open(target.c_str(), (append ? (std::ios::out | std::ios::binary | std::ios::app) : (std::ios::out | std::ios::binary | std::ios::trunc)));
		if (!in || !out) {
			runtimeErrorLevel() = errno != 0 ? errno : 1;
			return makeInt(runtimeErrorLevel());
		}
		out << in.rdbuf();
		runtimeErrorLevel() = (in.good() || in.eof()) && out.good() ? 0 : (errno != 0 ? errno : 1);
		return makeInt(runtimeErrorLevel());
	}
	if (name == "RENAME_FILE") {
		std::string source;
		std::string target;
		if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1])) throw std::runtime_error("RENAME_FILE expects (string, string).");
		source = expandUserPath(valueAsString(args[0]));
		target = expandUserPath(valueAsString(args[1]));
		runtimeErrorLevel() = ::rename(source.c_str(), target.c_str()) == 0 ? 0 : (errno != 0 ? errno : 1);
		return makeInt(runtimeErrorLevel());
	}
	if (name == "SWITCH_FILE") {
		const std::string target = expandUserPath(valueAsString(args[0]));
		if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("SWITCH_FILE expects one string argument.");
		if (currentBackgroundEditSession() != nullptr) return makeInt(0);
		for (MREditWindow *window : allEditWindowsInZOrder()) {
			if (window == nullptr) continue;
			if (target != expandUserPath(window->currentFileName())) continue;
			runtimeErrorLevel() = mrActivateEditWindow(window) ? 0 : 1001;
			return makeInt(runtimeErrorLevel() == 0 ? 1 : 0);
		}
		runtimeErrorLevel() = 0;
		return makeInt(0);
	}
	if (name == "SCREEN_LENGTH") return makeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->screenHeight : static_cast<int>(TDisplay::getRows()));
	if (name == "SCREEN_WIDTH") return makeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->screenWidth : static_cast<int>(TDisplay::getCols()));
	if (name == "WHEREX") {
		BackgroundEditSession *session = currentBackgroundEditSession();
		int x = 0;
		int y = 0;
		MRFileEditor *editor = currentEditor();
		if (editor != nullptr && currentUiCursorPosition(x, y)) return makeInt(x);
		if (session != nullptr) return makeInt(session->screenCursorX);
		return makeInt(currentUiCursorPosition(x, y) ? x : 0);
	}
	if (name == "WHEREY") {
		BackgroundEditSession *session = currentBackgroundEditSession();
		int x = 0;
		int y = 0;
		if (currentEditor() != nullptr && currentUiCursorPosition(x, y)) return makeInt(y);
		if (session != nullptr) return makeInt(session->screenCursorY);
		return makeInt(currentUiCursorPosition(x, y) ? y : 0);
	}
	if (name == "BLOCK_TEXT") {
		return makeString(std::string());
	}
	if (name == "BAR_MENU" || name == "V_MENU") {
		if (currentBackgroundEditSession() != nullptr) throw std::runtime_error(name + " is not available in background mode.");
		return makeInt(runMacroMenuIntrinsic(name, args));
	}
	if (name == "UI_EXEC") return makeInt(runMacroUiDialogDefinition());
	if (name == "UI_TEXT") {
		const auto it = g_macroUiDialog.textValues.find(valueAsInt(args[0]));
		return makeString(it != g_macroUiDialog.textValues.end() ? it->second : std::string());
	}
	if (name == "UI_INDEX") {
		const auto it = g_macroUiDialog.indexValues.find(valueAsInt(args[0]));
		return makeInt(it != g_macroUiDialog.indexValues.end() ? it->second : 0);
	}
	if (name == "STRING_IN") {
		if (currentBackgroundEditSession() != nullptr) throw std::runtime_error("STRING_IN is not available in background mode.");
		return makeString(runMacroStringInputIntrinsic(args));
	}

	throw std::runtime_error("Unknown intrinsic: " + name);
}
} // namespace

bool queueDeferredUiProcedure(const std::string &name, const std::vector<VirtualMachine::Value> &args, int &errorCode) {
	return queueDeferredUiProcedureImpl(name, args, errorCode);
}

bool enqueueDeferredUiCommand(const MRMacroDeferredUiCommand &command, int &errorCode) {
	return enqueueDeferredUiCommandImpl(command, errorCode);
}

bool currentExecutingMacroSpec(std::string &macroSpec) {
	return currentExecutingMacroSpecImpl(macroSpec);
}

std::string mrvmEditorExpandUserPath(const std::string &path) {
	return expandUserPath(path);
}

MREditWindow *mrvmEditorActiveWindow() {
	return activeMacroEditWindow();
}

MRFileEditor *mrvmEditorCurrentEditor() {
	return currentEditor();
}

bool mrvmEditorMarkPosition(MREditWindow *win, MRFileEditor *editor) {
	return markEditorPosition(win, editor);
}

bool mrvmEditorGotoMark(MREditWindow *win, MRFileEditor *editor) {
	return gotoEditorMark(win, editor);
}

bool mrvmEditorSetRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index) {
	return setEditorRandomAccessMark(win, editor, index);
}

bool mrvmEditorGotoRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index) {
	return gotoEditorRandomAccessMark(win, editor, index);
}

bool mrvmEditorBeginBlockMode(int mode) {
	return beginCurrentBlockMode(mode);
}

bool mrvmEditorEndBlockMode() {
	return endCurrentBlockMode();
}

bool mrvmEditorClearBlockMode() {
	return clearCurrentBlockMode();
}

bool mrvmEditorMoveCursorToNextPageBreak(MRFileEditor *editor) {
	return moveEditorNextPageBreak(editor);
}

bool mrvmEditorMoveCursorToPrevPageBreak(MRFileEditor *editor) {
	return moveEditorLastPageBreak(editor);
}

bool mrvmEditorMoveCursorTabRight(MRFileEditor *editor) {
	return moveEditorTabRight(editor);
}

bool mrvmEditorMoveCursorTabLeft(MRFileEditor *editor) {
	return moveEditorTabLeft(editor);
}

bool mrvmEditorIndentCursor(MRFileEditor *editor) {
	return indentEditor(editor);
}

bool mrvmEditorUndentCursor(MRFileEditor *editor) {
	return undentEditor(editor);
}

bool mrvmEditorCopyCurrentBlock(MREditWindow *win, MRFileEditor *editor) {
	(void)win;
	(void)editor;
	return true;
}

bool mrvmEditorMoveCurrentBlock(MREditWindow *win, MRFileEditor *editor) {
	(void)win;
	(void)editor;
	return true;
}

bool mrvmEditorDeleteCurrentBlock(MREditWindow *win, MRFileEditor *editor, bool leaveColumnSpace) {
	(void)win;
	(void)editor;
	(void)leaveColumnSpace;
	return true;
}

bool mrvmEditorExtractCurrentBlockText(MREditWindow *win, MRFileEditor *editor, std::string &out) {
	(void)win;
	(void)editor;
	out.clear();
	return false;
}

bool mrvmEditorIndentBlock(MREditWindow *win, MRFileEditor *editor) {
	(void)win;
	(void)editor;
	return true;
}

bool mrvmEditorUndentBlock(MREditWindow *win, MRFileEditor *editor) {
	(void)win;
	(void)editor;
	return true;
}

MREditWindow *mrvmEditorWindowByIndex(int index) {
	return editWindowByIndex(index);
}

bool mrvmEditorCopyBlockFromWindow(MREditWindow *srcWin, MRFileEditor *srcEditor, MREditWindow *destWin, MRFileEditor *destEditor) {
	(void)srcWin;
	(void)srcEditor;
	(void)destWin;
	(void)destEditor;
	return true;
}

bool mrvmEditorMoveBlockFromWindow(MREditWindow *srcWin, MRFileEditor *srcEditor, MREditWindow *destWin, MRFileEditor *destEditor) {
	(void)srcWin;
	(void)srcEditor;
	(void)destWin;
	(void)destEditor;
	return true;
}

bool mrvmEditorShouldLeaveColumnSpaceForDelete(MREditWindow *win) {
	(void)win;
	return false;
}

bool mrvmEditorLoadBlockFromFile(MREditWindow *win, const std::string &path) {
	return win != nullptr && win->loadStreamBlockFromFile(path);
}

bool mrvmEditorSaveCurrentBlockToFile(MREditWindow *win, MRFileEditor *editor, const std::string &path) {
	(void)editor;
	return win != nullptr && win->saveStreamBlockToFile(path);
}

bool mrvmEditorLinkCurrentWindow() {
	return linkCurrentEditWindow();
}

bool mrvmEditorUnlinkCurrentWindow() {
	return unlinkCurrentEditWindow();
}

bool mrvmEditorRedrawCurrentWindow() {
	return redrawCurrentEditWindow();
}

bool mrvmEditorRedrawEntireScreen() {
	return redrawEntireScreen();
}

bool mrvmEditorZoomCurrentWindow() {
	return zoomCurrentEditWindow();
}

bool mrvmEditorCreateWindow() {
	return createEditWindow();
}

bool mrvmEditorSwitchWindow(int index) {
	return switchEditWindow(index);
}

bool mrvmEditorSizeCurrentWindow(int x1, int y1, int x2, int y2) {
	return sizeCurrentEditWindow(x1, y1, x2, y2);
}

bool mrvmEditorDeleteCurrentWindow() {
	return deleteCurrentEditWindow();
}

bool mrvmEditorEraseCurrentWindow() {
	return eraseCurrentEditWindow();
}

bool mrvmEditorModifyCurrentWindow() {
	return modifyCurrentEditWindow();
}

MRMacroJobResult mrvmRunBytecodeBackground(const unsigned char *bytecode, std::size_t length, std::stop_token stopToken, std::shared_ptr<std::atomic_bool> cancelFlag) {
	MRMacroJobResult result;
	VirtualMachine vm;
	struct CancelGuard {
		const std::stop_token *savedToken;
		std::shared_ptr<std::atomic_bool> savedFlag;

		CancelGuard(const std::stop_token *token, std::shared_ptr<std::atomic_bool> flag) : savedToken(g_backgroundMacroStopToken), savedFlag(g_backgroundMacroCancelFlag) {
			g_backgroundMacroStopToken = token;
			g_backgroundMacroCancelFlag = std::move(flag);
		}

		~CancelGuard() {
			g_backgroundMacroStopToken = savedToken;
			g_backgroundMacroCancelFlag = savedFlag;
		}
	} cancelGuard(&stopToken, std::move(cancelFlag));

	vm.setVerboseLogging(false);
	vm.execute(bytecode, length);
	result.logLines = vm.log;
	result.cancelled = vm.wasCancelled();
	for (std::size_t i = 0; i < result.logLines.size(); ++i) {
		if (result.logLines[i].rfind("VM Error:", 0) == 0) {
			result.hadError = true;
			break;
		}
	}
	return result;
}

struct MRVMStagedExecutionContext {
	const MRMacroStagedExecutionInput &input;
	const std::stop_token &stopToken;
	std::shared_ptr<std::atomic_bool> cancelFlag;
	BackgroundEditSession session;

	struct InstallGuard {
		BackgroundEditSession *previousSession;
		const std::stop_token *previousStopToken;
		std::shared_ptr<std::atomic_bool> previousCancelFlag;

		explicit InstallGuard(MRVMStagedExecutionContext &context) noexcept
		    : previousSession(g_backgroundEditSession), previousStopToken(g_backgroundMacroStopToken), previousCancelFlag(g_backgroundMacroCancelFlag) {
			g_backgroundEditSession = &context.session;
			g_backgroundMacroStopToken = &context.stopToken;
			g_backgroundMacroCancelFlag = context.cancelFlag;
		}

		~InstallGuard() {
			g_backgroundEditSession = previousSession;
			g_backgroundMacroStopToken = previousStopToken;
			g_backgroundMacroCancelFlag = previousCancelFlag;
		}
	};

	MRVMStagedExecutionContext(const MRMacroStagedExecutionInput &stagedInput, const std::stop_token &stagedStopToken, std::shared_ptr<std::atomic_bool> stagedCancelFlag)
	    : input(stagedInput), stopToken(stagedStopToken), cancelFlag(std::move(stagedCancelFlag)), session() {
	}

	void initializeSession() {
		session.document = input.document;
		session.transaction = mr::editor::StagedEditTransaction(input.baseVersion, "macro-staged-write");
		session.cursorOffset = input.cursorOffset;
		session.selectionStart = input.selectionStart;
		session.selectionEnd = input.selectionEnd;
		session.blockMode = input.blockMode;
		session.blockMarkingOn = input.blockMarkingOn;
		session.blockAnchor = input.blockAnchor;
		session.blockEnd = input.blockEnd;
		session.firstSave = input.firstSave;
		session.eofInMemory = input.eofInMemory;
		session.bufferId = input.bufferId;
		session.temporaryFile = input.temporaryFile;
		session.temporaryFileName = input.temporaryFileName;
		session.currentWindow = input.currentWindow;
		session.linkStatus = input.linkStatus;
		session.windowCount = input.windowCount;
		session.windowGeometryValid = input.windowGeometryValid;
		session.windowX1 = input.windowX1;
		session.windowY1 = input.windowY1;
		session.windowX2 = input.windowX2;
		session.windowY2 = input.windowY2;
		session.globals.clear();
		session.globalOrder.clear();
		session.globalEnumIndex = 0;
		for (const auto &i : input.globalOrder)
			appendUniqueString(session.globalOrder, upperKey(i));
		for (const auto &globalInt : input.globalInts) {
			GlobalEntry entry;
			std::string key = upperKey(globalInt.first);
			entry.type = TYPE_INT;
			entry.value = makeInt(globalInt.second);
			if (session.globals.find(key) == session.globals.end()) appendUniqueString(session.globalOrder, key);
			session.globals[key] = entry;
		}
		for (const auto &globalString : input.globalStrings) {
			GlobalEntry entry;
			std::string key = upperKey(globalString.first);
			entry.type = TYPE_STR;
			entry.value = makeString(globalString.second);
			if (session.globals.find(key) == session.globals.end()) appendUniqueString(session.globalOrder, key);
			session.globals[key] = entry;
		}
		session.loadedMacroDisplayNames.clear();
		session.macroOrder.clear();
		session.macroEnumIndex = 0;
		session.deferredUiCommands.clear();
		for (const auto &i : input.macroOrder)
			appendUniqueString(session.macroOrder, upperKey(i));
		for (const auto &macroDisplayName : input.macroDisplayNames) {
			std::string key = upperKey(macroDisplayName.first);
			session.loadedMacroDisplayNames[key] = macroDisplayName.second;
			if (std::find(session.macroOrder.begin(), session.macroOrder.end(), key) == session.macroOrder.end()) session.macroOrder.push_back(key);
		}
		session.lastSearchValid = input.lastSearchValid;
		session.lastSearchStart = input.lastSearchStart;
		session.lastSearchEnd = input.lastSearchEnd;
		session.lastSearchCursor = input.lastSearchCursor;
		session.ignoreCase = input.ignoreCase;
		session.tabExpand = input.tabExpand;
		session.markStack.clear();
		for (unsigned long i : input.markStack)
			session.markStack.push_back(static_cast<uint>(i));
		session.insertMode = input.insertMode;
		session.indentLevel = input.indentLevel;
		session.pageLines = std::max(1, input.pageLines);
		session.fileName = input.fileName;
		session.fileChanged = input.fileChanged;
		session.screenWidth = input.screenWidth;
		session.screenHeight = input.screenHeight;
		session.screenCursorX = input.screenCursorX;
		session.screenCursorY = input.screenCursorY;
		session.clampState();
	}

	MRMacroStagedJobResult buildResult(const VirtualMachine &vm) const {
		MRMacroStagedJobResult result;

		result.logLines = vm.log;
		result.cancelled = vm.wasCancelled();
		for (std::size_t i = 0; i < result.logLines.size(); ++i) {
			if (result.logLines[i].rfind("VM Error:", 0) == 0) {
				result.hadError = true;
				break;
			}
		}
		result.transaction = session.transaction;
		result.cursorOffset = session.cursorOffset;
		result.selectionStart = session.selectionStart;
		result.selectionEnd = session.selectionEnd;
		result.blockMode = session.blockMode;
		result.blockMarkingOn = session.blockMarkingOn;
		result.blockAnchor = session.blockAnchor;
		result.blockEnd = session.blockEnd;
		result.globalOrder.clear();
		result.globalInts.clear();
		result.globalStrings.clear();
		result.globalOrder.reserve(session.globalOrder.size());
		for (std::size_t i = 0; i < session.globalOrder.size(); ++i) {
			const std::string &key = session.globalOrder[i];
			std::map<std::string, GlobalEntry>::const_iterator it = session.globals.find(key);
			if (it == session.globals.end()) continue;
			result.globalOrder.push_back(key);
			if (it->second.type == TYPE_INT) result.globalInts[key] = valueAsInt(it->second.value);
			else if (it->second.type == TYPE_STR)
				result.globalStrings[key] = valueAsString(it->second.value);
		}
		result.macroOrder = session.macroOrder;
		result.macroDisplayNames = session.loadedMacroDisplayNames;
		result.deferredUiCommands = session.deferredUiCommands;
		result.lastSearchValid = session.lastSearchValid;
		result.lastSearchStart = session.lastSearchStart;
		result.lastSearchEnd = session.lastSearchEnd;
		result.lastSearchCursor = session.lastSearchCursor;
		result.ignoreCase = session.ignoreCase;
		result.tabExpand = session.tabExpand;
		result.markStack.reserve(session.markStack.size());
		for (unsigned int i : session.markStack)
			result.markStack.push_back(static_cast<std::size_t>(i));
		result.insertMode = session.insertMode;
		result.indentLevel = session.indentLevel;
		result.fileName = session.fileName;
		result.fileChanged = session.fileChanged;
		return result;
	}
};

MRMacroStagedJobResult mrvmRunBytecodeStagedBackground(const unsigned char *bytecode, std::size_t length, const MRMacroStagedExecutionInput &input, std::stop_token stopToken, std::shared_ptr<std::atomic_bool> cancelFlag) {
	VirtualMachine vm;
	MRVMStagedExecutionContext context(input, stopToken, std::move(cancelFlag));

	context.initializeSession();
	MRVMStagedExecutionContext::InstallGuard installGuard(context);
	vm.setVerboseLogging(false);
	vm.execute(bytecode, length);
	return context.buildResult(vm);
}

void mrvmSetProcessContext(int argc, char **argv) {
	g_runtimeEnv.startupCommand.clear();
	g_runtimeEnv.processArgs.clear();
	if (argc > 0 && argv != nullptr && argv[0] != nullptr) g_runtimeEnv.startupCommand = argv[0];
	for (int i = 1; argv != nullptr && i < argc; ++i)
		g_runtimeEnv.processArgs.push_back(argv[i] != nullptr ? std::string(argv[i]) : std::string());
	g_runtimeEnv.executablePath = detectExecutablePathFromProc();
	if (g_runtimeEnv.executablePath.empty() && !g_runtimeEnv.startupCommand.empty()) g_runtimeEnv.executablePath = g_runtimeEnv.startupCommand;
	g_runtimeEnv.executableDir = detectExecutableDir(g_runtimeEnv.startupCommand);
	g_runtimeEnv.shellPath = detectShellPath();
	g_runtimeEnv.shellVersion = detectShellVersion(g_runtimeEnv.shellPath);
}

std::vector<std::string> mrvmProcessArguments() {
	return g_runtimeEnv.processArgs;
}

VirtualMachine::Value::Value() : type(TYPE_INT), i(0), r(0.0), c(0), hashHandle(0), arrayElementType(TYPE_INT), arrayValues(), globalStorage(false) {
}

VirtualMachine::VirtualMachine() : mHashStore(std::make_unique<MRVMHashStore>()), verboseLogging(true), logTruncated(false), mAsyncDelayPending(false), mAsyncDelayReady(false), mAsyncDelayEnabled(true), mAsyncLength(0), mAsyncIp(0), mAsyncReturnInt(0), mAsyncErrorLevel(0), mAsyncMacroFramePushed(false), mAsyncDelayTaskId(0), mAsyncDelayGeneration(0), mAsyncDelayMillis(0), cancelledExecution(false) {
}

VirtualMachine::~VirtualMachine() = default;

MRVMHashStore &VirtualMachine::localHashStore() {
	return *mHashStore;
}

const MRVMHashStore &VirtualMachine::localHashStore() const {
	return *mHashStore;
}

bool VirtualMachine::hashContains(int handle, const std::string &key) const {
	return mHashStore->contains(handle, key);
}

int VirtualMachine::hashCreate() {
	return mHashStore->createHash();
}

VirtualMachine::Value VirtualMachine::hashRead(int handle, const std::string &key) const {
	return mHashStore->read(handle, key);
}

void VirtualMachine::hashWrite(int handle, const std::string &key, const Value &value) {
	mHashStore->write(handle, key, value);
}

void VirtualMachine::hashErase(int handle, const std::string &key) {
	mHashStore->erase(handle, key);
}

void VirtualMachine::appendLogLine(const std::string &line, bool important) {
	static const std::size_t kMaxLogLines = 256;

	if (!important && !verboseLogging) return;
	if (log.size() < kMaxLogLines) {
		log.push_back(line);
		return;
	}
	if (!logTruncated) {
		log.emplace_back("VM Notice: execution log truncated.");
		logTruncated = true;
	}
}

void VirtualMachine::push(const Value &value) {
	stack.push_back(value);
}

VirtualMachine::Value VirtualMachine::pop() {
	if (!stack.empty()) {
		Value value = stack.back();
		stack.pop_back();
		return value;
	}

	appendLogLine("VM Error: Stack underflow.", true);
	return makeInt(0);
}

int VirtualMachine::normalizeDelayMillis(int millis) noexcept {
	static const int kMaxDelayMillis = 60 * 60 * 1000; // 1 hour hard cap.
	if (millis <= 0) return 0;
	if (millis > kMaxDelayMillis) return kMaxDelayMillis;
	return millis;
}

void VirtualMachine::clearAsyncDelayState() noexcept {
	mAsyncDelayPending = false;
	mAsyncDelayReady = false;
	mAsyncBytecode.clear();
	mAsyncCallStack.clear();
	mAsyncLength = 0;
	mAsyncIp = 0;
	mAsyncReturnInt = 0;
	mAsyncReturnStr.clear();
	mAsyncErrorLevel = 0;
	mAsyncSavedParameterString.clear();
	mAsyncMacroFramePushed = false;
	mAsyncDelayReadyFlag.reset();
	mAsyncDelayCancelledFlag.reset();
	mAsyncDelayTaskId = 0;
	mAsyncDelayMillis = 0;
}

namespace {
struct VmDelayYield {
	int millis;
	explicit VmDelayYield(int ms) noexcept : millis(ms) {
	}
};

static bool sleepDelayBlocking(int millis) {
	if (millis <= 0) return true;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(millis);
	while (std::chrono::steady_clock::now() < deadline) {
		if (backgroundMacroCancelRequested()) return false;
		auto remaining = deadline - std::chrono::steady_clock::now();
		auto slice = std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
		if (slice > std::chrono::milliseconds(10)) slice = std::chrono::milliseconds(10);
		if (slice.count() <= 0) break;
		std::this_thread::sleep_for(slice);
	}
	return true;
}
} // namespace

void VirtualMachine::execute(const unsigned char *bytecode, size_t length) {
	cancelPendingDelay();
	clearAsyncDelayState();
	executeAt(bytecode, length, 0, std::string(), std::string(), true, false);
}

bool VirtualMachine::resumePendingDelay() {
	if (!mAsyncDelayPending) return false;
	if (!mAsyncDelayReady || mAsyncDelayReadyFlag == nullptr || !mAsyncDelayReadyFlag->load(std::memory_order_acquire)) return true;
	if (mAsyncDelayCancelledFlag != nullptr && mAsyncDelayCancelledFlag->load(std::memory_order_acquire)) {
		cancelledExecution = true;
		appendLogLine("VM Notice: DELAY cancelled before resume.", true);
		runtimeErrorLevel() = 5007;
		if (mAsyncMacroFramePushed && !g_runtimeEnv.macroStack.empty()) g_runtimeEnv.macroStack.pop_back();
		clearAsyncDelayState();
		return false;
	}
	executeAt(nullptr, 0, 0, std::string(), std::string(), false, false);
	return mAsyncDelayPending;
}

bool VirtualMachine::cancelPendingDelay() {
	bool hadPending = mAsyncDelayPending;

	if (!hadPending) return false;
	if (mAsyncDelayCancelledFlag != nullptr) mAsyncDelayCancelledFlag->store(true, std::memory_order_release);
	if (mAsyncDelayTaskId != 0) (void)mr::coprocessor::globalCoprocessor().cancelTask(mAsyncDelayTaskId);
	if (mAsyncMacroFramePushed && !g_runtimeEnv.macroStack.empty()) g_runtimeEnv.macroStack.pop_back();
	cancelledExecution = true;
	runtimeErrorLevel() = 5007;
	appendLogLine("VM Notice: pending DELAY cancelled.", true);
	clearAsyncDelayState();
	return true;
}

void VirtualMachine::executeAt(const unsigned char *bytecode, size_t length, size_t entryOffset, const std::string &parameterString, const std::string &macroName, bool resetState, bool firstRun) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	bool resumeFromDelay = (bytecode == nullptr && length == 0 && mAsyncDelayPending && mAsyncDelayReady && !mAsyncBytecode.empty() && mAsyncIp <= mAsyncLength);
	std::uint64_t resumeGeneration = mAsyncDelayGeneration;
	size_t ip = resumeFromDelay ? mAsyncIp : entryOffset;
	std::vector<size_t> call_stack;
	ExecutionState state;
	ExecutionState *parentState = currentExecutionState();
	std::string savedParameterString;
	bool pushedMacroFrame = false;
	bool allowAsyncDelay = false;
	struct ExecutionStateGuard {
		ExecutionState *previous;

		explicit ExecutionStateGuard(ExecutionState *next) noexcept : previous(g_executionState) {
			g_executionState = next;
		}

		~ExecutionStateGuard() {
			g_executionState = previous;
		}
	} executionStateGuard(&state);

	if (resumeFromDelay) {
		bytecode = mAsyncBytecode.data();
		length = mAsyncLength;
		call_stack = mAsyncCallStack;
		savedParameterString = mAsyncSavedParameterString;
		state.parameterString = mAsyncSavedParameterString;
		state.returnInt = mAsyncReturnInt;
		state.returnStr = mAsyncReturnStr;
		state.errorLevel = mAsyncErrorLevel;
		pushedMacroFrame = mAsyncMacroFramePushed;
		mAsyncDelayReady = false;
		mAsyncDelayTaskId = 0;
	} else {
		if (bytecode == nullptr || length == 0 || entryOffset >= length) return;
		savedParameterString = parentState != nullptr ? parentState->parameterString : g_runtimeEnv.parameterString;
		state.parameterString = savedParameterString;
		if (parentState != nullptr) {
			state.returnInt = parentState->returnInt;
			state.returnStr = parentState->returnStr;
			state.errorLevel = parentState->errorLevel;
		} else {
			state.returnInt = g_runtimeEnv.returnInt;
			state.returnStr = g_runtimeEnv.returnStr;
			state.errorLevel = g_runtimeEnv.errorLevel;
		}

		variables.clear();
		mHashStore->clearExceptRoots(currentGlobalHashRoots());
		stack.clear();
		cancelledExecution = false;
		if (resetState) {
			log.clear();
			logTruncated = false;
			g_runtimeEnv.globalEnumIndex = 0;
			g_runtimeEnv.macroEnumIndex = 0;
			state.parameterString.clear();
			state.returnInt = 0;
			state.returnStr.clear();
			state.errorLevel = 0;
		}

		if (!macroName.empty()) {
			g_runtimeEnv.macroStack.emplace_back(macroName, firstRun);
			pushedMacroFrame = true;
		}
		state.parameterString = parameterString;
	}
	allowAsyncDelay = (mAsyncDelayEnabled && parentState == nullptr && currentBackgroundEditSession() == nullptr && g_backgroundMacroStopToken == nullptr);
	if (allowAsyncDelay && !resumeFromDelay) {
		mAsyncBytecode.assign(bytecode, bytecode + length);
		mAsyncLength = length;
	}

	auto readInt = [&](int &value) {
		std::memcpy(&value, &bytecode[ip], sizeof(int));
		ip += sizeof(int);
	};

	auto readDouble = [&](double &value) {
		std::memcpy(&value, &bytecode[ip], sizeof(double));
		ip += sizeof(double);
	};

	auto readCString = [&](std::string &value) {
		const char *textp = reinterpret_cast<const char *>(&bytecode[ip]);
		value = textp;
		ip += value.size() + 1;
	};

	auto popArgs = [&](unsigned char count) {
		std::vector<Value> args;
		args.reserve(count);
		for (unsigned char i = 0; i < count; ++i)
			args.push_back(pop());
		std::reverse(args.begin(), args.end());
		return args;
	};

	try {
		while (ip < length) {
			if (backgroundMacroCancelRequested()) {
				cancelledExecution = true;
				appendLogLine("VM Notice: Background macro cancelled.", true);
				runtimeErrorLevel() = 5007;
				break;
			}
			unsigned char opcode = bytecode[ip++];

			if (opcode == OP_PUSH_I) {
				int val;
				readInt(val);
				push(makeInt(val));
				appendLogLine("Push integer: " + std::to_string(val));
			} else if (opcode == OP_PUSH_R) {
				double val;
				readDouble(val);
				push(makeReal(val));
				appendLogLine("Push real: " + valueAsString(makeReal(val)));
			} else if (opcode == OP_PUSH_S) {
				std::string str;
				readCString(str);
				enforceStringLength(str);
				push(makeString(str));
				appendLogLine("Push string: " + str);
			} else if (opcode == OP_DEF_VAR) {
				std::string varName;
				int varType = static_cast<int>(bytecode[ip++]);
				readCString(varName);
				if (varType == TYPE_HASH)
					variables[varName] = makeHash(mHashStore->createHash());
				else
					variables[varName] = mrvmDefaultValueForType(varType);
				appendLogLine("Define variable: " + varName);
			} else if (opcode == OP_LOAD_VAR) {
				std::string varName;
				bool handled = false;
				readCString(varName);

				Value special = loadSpecialVariable(varName, handled);
				if (handled) push(special);
				else {
					std::map<std::string, Value>::const_iterator it = variables.find(varName);
					if (it == variables.end()) variables[varName] = makeInt(0);
					push(variables[varName]);
				}
				appendLogLine("Load variable: " + varName);
			} else if (opcode == OP_STORE_VAR) {
				std::string varName;
				int targetType = static_cast<int>(bytecode[ip++]);
				readCString(varName);
				Value value = mrvmCoerceForStore(pop(), targetType);
				if (value.type == TYPE_STR) enforceStringLength(value.s);
				if (!storeSpecialVariable(varName, value)) variables[varName] = value;
				appendLogLine("Store variable: " + varName);
			} else if (opcode == OP_HASH_LOAD) {
				std::string varName;
				Value key;
				std::map<std::string, Value>::const_iterator it;
				readCString(varName);
				key = pop();
				if (!isStringLike(key)) throw std::runtime_error("type mismatch");
				it = variables.find(varName);
				if (it == variables.end() || it->second.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
				push(mrvmHashReadValue(*mHashStore, g_runtimeEnv.globalHashStore, it->second, valueAsString(key)));
				appendLogLine("Load hash value: " + varName);
			} else if (opcode == OP_HASH_LOAD_VALUE) {
				Value key;
				Value hash;
				key = pop();
				hash = pop();
				if (hash.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
				if (!isStringLike(key)) throw std::runtime_error("type mismatch");
				push(mrvmHashReadValue(*mHashStore, g_runtimeEnv.globalHashStore, hash, valueAsString(key)));
				appendLogLine("Load hash value from expression.");
			} else if (opcode == OP_HASH_STORE) {
				std::string varName;
				Value value;
				Value key;
				std::map<std::string, Value>::const_iterator it;
				readCString(varName);
				value = pop();
				key = pop();
				if (!isStringLike(key)) throw std::runtime_error("type mismatch");
				it = variables.find(varName);
				if (it == variables.end() || it->second.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
				if (value.type == TYPE_STR) enforceStringLength(value.s);
				mrvmHashWriteValue(*mHashStore, g_runtimeEnv.globalHashStore, it->second, valueAsString(key), value);
				appendLogLine("Store hash value: " + varName);
			} else if (opcode == OP_HASH_STORE_VALUE) {
				Value value;
				Value key;
				Value hash;
				value = pop();
				key = pop();
				hash = pop();
				if (hash.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
				if (!isStringLike(key)) throw std::runtime_error("type mismatch");
				if (value.type == TYPE_STR) enforceStringLength(value.s);
				mrvmHashWriteValue(*mHashStore, g_runtimeEnv.globalHashStore, hash, valueAsString(key), value);
				appendLogLine("Store hash value from expression.");
			} else if (opcode == OP_ARRAY_LOAD) {
				std::string varName;
				Value index;
				std::map<std::string, Value>::const_iterator it;
				readCString(varName);
				index = pop();
				if (index.type != TYPE_INT) throw std::runtime_error("type mismatch");
				it = variables.find(varName);
				if (it == variables.end() || !mrvmValueIsArrayType(it->second.type)) throw std::runtime_error("Invalid array value.");
				push(mrvmArrayReadValue(it->second, index.i));
				appendLogLine("Load array value: " + varName);
			} else if (opcode == OP_ARRAY_LOAD_VALUE) {
				Value index;
				Value arrayValue;
				index = pop();
				arrayValue = pop();
				if (index.type != TYPE_INT) throw std::runtime_error("type mismatch");
				push(mrvmArrayReadValue(arrayValue, index.i));
				appendLogLine("Load array value from expression.");
			} else if (opcode == OP_ARRAY_STORE) {
				std::string varName;
				Value value;
				Value index;
				std::map<std::string, Value>::iterator it;
				readCString(varName);
				value = pop();
				index = pop();
				if (index.type != TYPE_INT) throw std::runtime_error("type mismatch");
				it = variables.find(varName);
				if (it == variables.end() || !mrvmValueIsArrayType(it->second.type)) throw std::runtime_error("Invalid array value.");
				mrvmArrayWriteValue(it->second, index.i, value, *mHashStore, g_runtimeEnv.globalHashStore);
				appendLogLine("Store array value: " + varName);
			} else if (opcode == OP_GOTO) {
				int target;
				readInt(target);
				if (target < 0 || static_cast<size_t>(target) >= length) throw std::runtime_error("Invalid jump target in GOTO.");
				ip = static_cast<size_t>(target);
			} else if (opcode == OP_CALL) {
				int target;
				readInt(target);
				if (target < 0 || static_cast<size_t>(target) >= length) throw std::runtime_error("Invalid jump target in CALL.");
				call_stack.push_back(ip);
				ip = static_cast<size_t>(target);
			} else if (opcode == OP_RET) {
				if (call_stack.empty()) throw std::runtime_error("RET without matching CALL.");
				ip = call_stack.back();
				call_stack.pop_back();
			} else if (opcode == OP_JZ) {
				int target;
				Value cond;
				readInt(target);
				cond = pop();
				if (cond.type != TYPE_INT) throw std::runtime_error("IF/WHILE expression must be integer.");
				if (target < 0 || static_cast<size_t>(target) >= length) throw std::runtime_error("Invalid jump target in JZ.");
				if (cond.i == 0) ip = static_cast<size_t>(target);
			} else if (opcode == OP_ADD) {
				Value b = pop();
				Value a = pop();
				if (isStringLike(a) && isStringLike(b)) {
					std::string s = valueAsString(a) + valueAsString(b);
					enforceStringLength(s);
					push(makeString(s));
				} else if (isNumeric(a) && isNumeric(b)) {
					if (a.type == TYPE_REAL || b.type == TYPE_REAL) push(makeReal(valueAsReal(a) + valueAsReal(b)));
					else
						push(makeInt(a.i + b.i));
				} else
					throw std::runtime_error(MRConstants::kErrorTypeMismatch);
			} else if (opcode == OP_SUB) {
				Value b = pop();
				Value a = pop();
				if (!isNumeric(a) || !isNumeric(b)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
				if (a.type == TYPE_REAL || b.type == TYPE_REAL) push(makeReal(valueAsReal(a) - valueAsReal(b)));
				else
					push(makeInt(a.i - b.i));
			} else if (opcode == OP_MUL) {
				Value b = pop();
				Value a = pop();
				if (!isNumeric(a) || !isNumeric(b)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
				if (a.type == TYPE_REAL || b.type == TYPE_REAL) push(makeReal(valueAsReal(a) * valueAsReal(b)));
				else
					push(makeInt(a.i * b.i));
			} else if (opcode == OP_DIV) {
				Value b = pop();
				Value a = pop();
				if (!isNumeric(a) || !isNumeric(b)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
				if ((b.type == TYPE_REAL && b.r == 0.0) || (b.type == TYPE_INT && b.i == 0)) throw std::runtime_error("Division by zero.");
				if (a.type == TYPE_REAL || b.type == TYPE_REAL) push(makeReal(valueAsReal(a) / valueAsReal(b)));
				else
					push(makeInt(a.i / b.i));
			} else if (opcode == OP_MOD) {
				Value b = pop();
				Value a = pop();
				if (a.type != TYPE_INT || b.type != TYPE_INT) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
				if (b.i == 0) throw std::runtime_error("Modulo by zero.");
				push(makeInt(a.i % b.i));
			} else if (opcode == OP_NEG) {
				Value a = pop();
				if (!isNumeric(a)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
				if (a.type == TYPE_REAL) push(makeReal(-a.r));
				else
					push(makeInt(-a.i));
			} else if (opcode == OP_CMP_EQ || opcode == OP_CMP_NE || opcode == OP_CMP_LT || opcode == OP_CMP_GT || opcode == OP_CMP_LE || opcode == OP_CMP_GE) {
				Value b = pop();
				Value a = pop();
				int cmp = compareValues(a, b);
				int result = 0;
				switch (opcode) {
					case OP_CMP_EQ:
						result = (cmp == 0);
						break;
					case OP_CMP_NE:
						result = (cmp != 0);
						break;
					case OP_CMP_LT:
						result = (cmp < 0);
						break;
					case OP_CMP_GT:
						result = (cmp > 0);
						break;
					case OP_CMP_LE:
						result = (cmp <= 0);
						break;
					case OP_CMP_GE:
						result = (cmp >= 0);
						break;
				}
				push(makeInt(result));
			} else if (opcode == OP_AND) {
				Value b = pop();
				Value a = pop();
				push(makeInt((valueAsInt(a) != 0 && valueAsInt(b) != 0) ? 1 : 0));
			} else if (opcode == OP_OR) {
				Value b = pop();
				Value a = pop();
				push(makeInt((valueAsInt(a) != 0 || valueAsInt(b) != 0) ? 1 : 0));
			} else if (opcode == OP_NOT) {
				Value a = pop();
				push(makeInt(valueAsInt(a) == 0 ? 1 : 0));
			} else if (opcode == OP_SHL) {
				Value b = pop();
				Value a = pop();
				push(makeInt(valueAsInt(a) << valueAsInt(b)));
			} else if (opcode == OP_SHR) {
				Value b = pop();
				Value a = pop();
				push(makeInt(valueAsInt(a) >> valueAsInt(b)));
			} else if (opcode == OP_BIT_AND) {
				Value b = pop();
				Value a = pop();
				push(makeInt(valueAsInt(a) & valueAsInt(b)));
			} else if (opcode == OP_BIT_OR) {
				Value b = pop();
				Value a = pop();
				push(makeInt(valueAsInt(a) | valueAsInt(b)));
			} else if (opcode == OP_BIT_XOR) {
				Value b = pop();
				Value a = pop();
				push(makeInt(valueAsInt(a) ^ valueAsInt(b)));
			} else if (opcode == OP_INTRINSIC) {
				std::string name;
				readCString(name);
				unsigned char argc = bytecode[ip++];
				std::vector<Value> args = popArgs(argc);
				push(applyIntrinsic(*this, name, args));
			} else if (opcode == OP_VAL || opcode == OP_RVAL) {
				std::string varName;
				Value source;
				int resultCode = 0;
				readCString(varName);
				source = pop();
				if (!isStringLike(source)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);

				std::string textValue = valueAsString(source);
				if (opcode == OP_VAL) {
					int errorPos = findValErrorPosition(textValue);
					if (errorPos == 0) {
						long long parsed = std::strtoll(textValue.c_str(), nullptr, 10);
						if (parsed < static_cast<long long>(std::numeric_limits<int>::min()) || parsed > static_cast<long long>(std::numeric_limits<int>::max())) throw std::runtime_error("Real to Integer conversion out of range.");
						variables[varName] = makeInt(static_cast<int>(parsed));
					} else
						resultCode = errorPos;
				} else {
					int errorPos = findRValErrorPosition(textValue);
					if (errorPos == 0) {
						char *endPtr = nullptr;
						double parsed = std::strtod(textValue.c_str(), &endPtr);
						(void)endPtr;
						variables[varName] = makeReal(parsed);
					} else
						resultCode = errorPos;
				}
				push(makeInt(resultCode));
			} else if (opcode == OP_FIRST_GLOBAL || opcode == OP_NEXT_GLOBAL) {
				std::string targetVar;
				readCString(targetVar);
				BackgroundEditSession *session = currentBackgroundEditSession();

				if (session != nullptr) {
					if (opcode == OP_FIRST_GLOBAL) session->globalEnumIndex = 0;
					while (session->globalEnumIndex < session->globalOrder.size()) {
						const std::string &key = session->globalOrder[session->globalEnumIndex++];
						std::map<std::string, GlobalEntry>::const_iterator it = session->globals.find(key);
						if (it == session->globals.end()) continue;
						variables[targetVar] = makeInt(it->second.type == TYPE_INT ? 1 : 0);
						push(makeString(key));
						goto handled_global_enum;
					}
					variables[targetVar] = makeInt(0);
					push(makeString(""));
					goto handled_global_enum;
				}

				if (opcode == OP_FIRST_GLOBAL) g_runtimeEnv.globalEnumIndex = 0;
				while (g_runtimeEnv.globalEnumIndex < g_runtimeEnv.globalOrder.size()) {
					const std::string &key = g_runtimeEnv.globalOrder[g_runtimeEnv.globalEnumIndex++];
					std::map<std::string, GlobalEntry>::const_iterator it = g_runtimeEnv.globals.find(key);
					if (it == g_runtimeEnv.globals.end()) continue;
					variables[targetVar] = makeInt(it->second.type == TYPE_INT ? 1 : 0);
					push(makeString(key));
					goto handled_global_enum;
				}
				variables[targetVar] = makeInt(0);
				push(makeString(""));
			handled_global_enum:;
			} else if (opcode == OP_PROC_VAR) {
				std::string name;
				std::string varName;
				std::string indexVarName;
				unsigned char varArgc = 0;
				std::map<std::string, Value>::iterator it;
				readCString(name);
				varArgc = bytecode[ip++];
				if (varArgc == 0 || varArgc > 2) throw std::runtime_error("Malformed variable procedure call.");
				readCString(varName);
				if (varArgc > 1) readCString(indexVarName);
				it = variables.find(varName);
				if (it == variables.end()) throw std::runtime_error("Variable expected.");
				if (it->second.type != TYPE_STR) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
				if (name == "EXPAND_TABS") {
					std::string source = valueAsString(it->second);
					bool toVirtuals = currentRuntimeTabExpand();
					it->second = makeString(expandTabsString(source, toVirtuals));
					if (varArgc > 1) {
						std::map<std::string, Value>::iterator indexIt = variables.find(indexVarName);
						if (indexIt == variables.end()) throw std::runtime_error("Variable expected.");
						if (indexIt->second.type != TYPE_INT) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
						indexIt->second = makeInt(expandedTabsAdjustedIndex(source, indexIt->second.i));
					}
				} else if (name == "TABS_TO_SPACES") {
					if (varArgc != 1) throw std::runtime_error("TABS_TO_SPACES expects one variable argument.");
					it->second = makeString(tabsToSpacesString(valueAsString(it->second)));
				} else
					throw std::runtime_error("Unknown variable procedure.");
			} else if (opcode == OP_PROC) {
				std::string name;
				readCString(name);
				unsigned char argc = bytecode[ip++];
				std::vector<Value> args = popArgs(argc);
					if (name == "KEYMAP_RESET") {
						if (!args.empty()) throw std::runtime_error("KEYMAP_RESET expects no arguments.");
						if (!setConfiguredKeymapProfiles(std::vector<MRKeymapProfile>(), nullptr)) throw std::runtime_error("KEYMAP_RESET failed: invalid keymap state.");
						if (!setConfiguredActiveKeymapProfile("", nullptr)) throw std::runtime_error("KEYMAP_RESET failed: invalid active keymap profile.");
						runtimeErrorLevel() = 0;
					} else if (name == "KEYMAP_VERSION" || name == "THEME_VERSION") {
						const std::string versionLiteral = args.size() == 1 && isStringLike(args[0]) ? trimAscii(valueAsString(args[0])) : std::string();
						std::uint64_t parsedVersion = 0;
						const std::string artifactLabel = name == "KEYMAP_VERSION" ? "Keymap" : "Theme";

						if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error(name + " expects (string).");
						if (!mrParsePersistenceVersion(versionLiteral, parsedVersion))
							throw std::runtime_error(name + " failed: invalid persistence version.");
						if (parsedVersion > mrCurrentPersistenceVersion())
							throw std::runtime_error(name + " failed: future build version " + versionLiteral + " is not supported.");
						if (parsedVersion < mrCurrentPersistenceVersion()) mrLogMessage((artifactLabel + " file version upgrade required: " + versionLiteral).c_str());
						runtimeErrorLevel() = 0;
					} else if (name == "KEYMAP_PROFILE") {
						std::string errorText;
						if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("KEYMAP_PROFILE expects (string).");
						if (!mrvmApplyConfiguredKeymapProfilePayload(valueAsString(args[0]), &errorText)) throw std::runtime_error("KEYMAP_PROFILE failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
						runtimeErrorLevel() = 0;
					} else if (name == "KEYMAP_BIND") {
						std::string errorText;
						if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("KEYMAP_BIND expects (string).");
						if (!mrvmApplyConfiguredKeymapBindingPayload(valueAsString(args[0]), &errorText)) throw std::runtime_error("KEYMAP_BIND failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
						runtimeErrorLevel() = 0;
					} else if (name == "ACTIVE_KEYMAP_PROFILE") {
						std::string errorText;
						if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("ACTIVE_KEYMAP_PROFILE expects (string).");
						if (!mrvmApplyConfiguredActiveKeymapProfilePayload(valueAsString(args[0]), &errorText)) throw std::runtime_error("ACTIVE_KEYMAP_PROFILE failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
						runtimeErrorLevel() = 0;
					} else if (name == "THEME_RESET") {
						std::string errorText;
						const MRColorSetupSettings defaults = resolveColorSetupDefaults();
						if (!args.empty()) throw std::runtime_error("THEME_RESET expects no arguments.");
						if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, defaults.windowColors.data(), defaults.windowColors.size(), &errorText))
							throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid window colors.") : errorText));
						if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, defaults.menuDialogColors.data(), defaults.menuDialogColors.size(), &errorText))
							throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid menu/dialog colors.") : errorText));
						if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Help, defaults.helpColors.data(), defaults.helpColors.size(), &errorText))
							throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid help colors.") : errorText));
						if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Other, defaults.otherColors.data(), defaults.otherColors.size(), &errorText))
							throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid other colors.") : errorText));
						if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MiniMap, defaults.miniMapColors.data(), defaults.miniMapColors.size(), &errorText))
							throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid minimap colors.") : errorText));
						if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::FileCompareMiniMap, defaults.fileCompareMiniMapColors.data(), defaults.fileCompareMiniMapColors.size(), &errorText))
							throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid file compare minimap colors.") : errorText));
						if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Code, defaults.codeColors.data(), defaults.codeColors.size(), &errorText))
							throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid code colors.") : errorText));
						if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::FileCompare, defaults.fileCompareColors.data(), defaults.fileCompareColors.size(), &errorText))
							throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid file compare colors.") : errorText));
						if (!setConfiguredColorThemeDisplayName("", &errorText)) throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid theme display name.") : errorText));
						runtimeErrorLevel() = 0;
					} else if (name == "THEME_NAME" || name == "WINDOWCOLORS" || name == "MENUDIALOGCOLORS" || name == "HELPCOLORS" || name == "OTHERCOLORS" || name == "MINIMAPCOLORS" || name == "FILECOMPAREMINIMAPCOLORS" || name == "CODECOLORS" || name == "FILECOMPARECOLORS") {
						std::string errorText;
						if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error(name + " expects (string).");
						if (name == "THEME_NAME") {
							if (!setConfiguredColorThemeDisplayName(valueAsString(args[0]), &errorText)) throw std::runtime_error("THEME_NAME failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
						} else if (!applyConfiguredColorSetupValue(name, valueAsString(args[0]), &errorText, false))
							throw std::runtime_error(name + " failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
						runtimeErrorLevel() = 0;
					} else if (name == "MRSETUP") {
						std::string setupKey;
						std::string errorText;
						MRSetupPaths activePaths = resolveSetupPathDefaults();

						if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1])) throw std::runtime_error("MRSETUP expects (string, string).");
						setupKey = upperKey(trimAscii(valueAsString(args[0])));
						if (setupKey == "FILECOMPAREMINIMAPCOLORS" && !mrvmIsStartupSettingsMode()) {
							if (!applyConfiguredColorSetupValue(setupKey, valueAsString(args[1]), &errorText, false))
								throw std::runtime_error("MRSETUP(" + setupKey + ") failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
							runtimeErrorLevel() = 0;
							break;
						}
						if (!mrvmIsStartupSettingsMode()) throw std::runtime_error("MRSETUP is only allowed in settings.mrmac during startup.");
						if (setupKey == "SETTINGS_VERSION") {
							const std::string versionLiteral = trimAscii(valueAsString(args[1]));
							std::uint64_t parsedVersion = 0;

							if (!mrParsePersistenceVersion(versionLiteral, parsedVersion))
								throw std::runtime_error("MRSETUP(" + setupKey + ") failed: invalid persistence version.");
							if (parsedVersion > mrCurrentPersistenceVersion())
								throw std::runtime_error("MRSETUP(" + setupKey + ") failed: future build version " + versionLiteral + " is not supported.");
						} else if (setupKey == "SETTINGSPATH") {
							if (!setConfiguredSettingsMacroFilePath(valueAsString(args[1]), &errorText)) throw std::runtime_error("MRSETUP(SETTINGSPATH) failed: " + (errorText.empty() ? std::string("invalid path.") : errorText));
							activePaths.settingsMacroUri = configuredSettingsMacroFilePath();
						} else
						switch (classifySettingsKey(setupKey)) {
								case MRSettingsKeyClass::Unknown:
									throw std::runtime_error("MRSETUP supports keys: SETTINGS_VERSION, MACROPATH, SETTINGSPATH, HELPPATH, TEMPDIR, "
									                         "SHELLPATH, WINDOW_MANAGER, MESSAGES, SEARCH_TEXT_TYPE, SEARCH_DIRECTION, "
									                         "SEARCH_MODE, SEARCH_CASE_SENSITIVE, SEARCH_GLOBAL_SEARCH, "
									                         "SEARCH_RESTRICT_MARKED_BLOCK, SEARCH_ALL_WINDOWS, "
								                         "SAR_TEXT_TYPE, SAR_DIRECTION, SAR_MODE, SAR_LEAVE_CURSOR_AT, "
								                         "SAR_CASE_SENSITIVE, SAR_GLOBAL_SEARCH, SAR_RESTRICT_MARKED_BLOCK, "
								                         "SAR_ALL_WINDOWS, "
								                         "MULTI_SEARCH_FILESPEC, MULTI_SEARCH_TEXT, MULTI_SEARCH_STARTING_PATH, "
								                         "MULTI_SEARCH_SUBDIRECTORIES, MULTI_SEARCH_CASE_SENSITIVE, "
								                         "MULTI_SEARCH_REGULAR_EXPRESSIONS, MULTI_SEARCH_FILES_IN_MEMORY, "
								                         "MULTI_SEARCH_RESTRICT_WORKSPACE, "
								                         "MULTI_SAR_FILESPEC, MULTI_SAR_TEXT, MULTI_SAR_REPLACEMENT, "
								                         "MULTI_SAR_STARTING_PATH, MULTI_SAR_SUBDIRECTORIES, "
								                         "MULTI_SAR_CASE_SENSITIVE, MULTI_SAR_REGULAR_EXPRESSIONS, "
									                         "MULTI_SAR_FILES_IN_MEMORY, MULTI_SAR_KEEP_FILES_OPEN, "
									                         "MULTI_SAR_RESTRICT_WORKSPACE, "
									                         "ACQUIRE_COMMAND, ACQUIRE_COMMAND_HISTORY, "
									                         "LIVE_LOG_REPORT_MESSAGE_LINE, LIVE_LOG_REPORT_BEEP, LIVE_LOG_REPORT_AUDIO, "
									                         "LIVE_LOG_SCROLL_DIRECTION, LIVE_LOG_LINE_NUMBERS, LIVE_LOG_TIMESTAMPS, "
									                         "LIVE_LOG_SYNTAX_HIGHLIGHTING, LIVE_LOG_AUDIO_URI, LIVE_LOG_JOURNAL_TAG_HISTORY, "
									                         "VIRTUAL_DESKTOPS, CYCLIC_VIRTUAL_DESKTOPS, CURSOR_BEHAVIOUR, "
										                         "COMPILER_ERROR_MESSAGE_PLACEMENT, SCROLLBAR_VISIBILITY, TRACK_COMPILER_WARNINGS, TRACK_COMPILER_NOTES, "
										                         "UI_INDENT_STYLE, CURSOR_POSITION_MARKER, WINDOW_COLORTHEME_URI, "
									                         "FILE_COMPARE_ORIGINAL_LEADING_GUTTERS, FILE_COMPARE_ORIGINAL_TRAILING_GUTTERS, FILE_COMPARE_COMPARE_LEADING_GUTTERS, FILE_COMPARE_COMPARE_TRAILING_GUTTERS, FILE_COMPARE_START_CONFIGURATION, FILE_COMPARE_COMPARE_PANEL_READ_ONLY, "
									                         "AUTOSAVE_WORKSPACE, AUTOLOAD_WORKSPACE, LOG_HANDLING, LOGFILE, AUTOEXEC_MACRO, "
									                         "LASTFILEDIALOGPATH, "
									                         "MAX_PATH_HISTORY, MAX_FILE_HISTORY, PATH_HISTORY, FILE_HISTORY, "
									                         "DIALOG_LAST_PATH, DIALOG_PATH_HISTORY, DIALOG_FILE_HISTORY, "
									                         "MULTI_FILESPEC_HISTORY, MULTI_PATH_HISTORY, "
									                         "DEFAULT_PROFILE_DESCRIPTION, PAGE_BREAK, WORD_DELIMITERS, DEFAULT_EXTENSIONS, "
									                         "TRUNCATE_SPACES, EOF_CTRL_Z, EOF_CR_LF, TAB_EXPAND, DISPLAY_TABS, TAB_SIZE, LEFT_MARGIN, RIGHT_MARGIN, FORMAT_RULER, WORD_WRAP, "
								                         "INDENT_STYLE, CODE_LANGUAGE, CODE_COLORING, CODE_FOLDING, FILE_TYPE, BINARY_RECORD_LENGTH, POST_LOAD_MACRO, PRE_SAVE_MACRO, DEFAULT_PATH, "
								                         "FORMAT_LINE, BACKUP_METHOD, BACKUP_FREQUENCY, BACKUP_EXTENSION, BACKUP_DIRECTORY, "
								                         "AUTOSAVE_INACTIVITY_SECONDS, AUTOSAVE_INTERVAL_SECONDS, BACKUP_FILES, SHOW_EOF_MARKER, "
								                         "SHOW_EOF_MARKER_EMOJI, LINE_NUMBERS_POSITION, LINE_NUM_ZERO_FILL, "
								                         "MINIMAP_POSITION, MINIMAP_WIDTH, MINIMAP_MARKER_GLYPH, GUTTERS, PERSISTENT_BLOCKS, "
								                         "CODE_FOLDING_POSITION, "
								                         "BLOCK_MOVE, DEFAULT_MODE, CURSOR_STATUS_COLOR.");
							case MRSettingsKeyClass::Version:
							case MRSettingsKeyClass::Path:
							case MRSettingsKeyClass::Global:
								if (!applyConfiguredSettingsAssignment(setupKey, valueAsString(args[1]), activePaths, &errorText))
									throw std::runtime_error("MRSETUP(" + setupKey + ") failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
								break;
							case MRSettingsKeyClass::Edit:
								if (!applyConfiguredEditSetupValue(setupKey, valueAsString(args[1]), &errorText))
									throw std::runtime_error("MRSETUP(" + setupKey + ") failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
								if (setupKey == "TAB_EXPAND") {
									BackgroundEditSession *session = currentBackgroundEditSession();
									if (session != nullptr) session->tabExpand = configuredTabExpandSetting();
									else
										g_runtimeEnv.tabExpand = configuredTabExpandSetting();
								}
								break;
							case MRSettingsKeyClass::ColorInline:
								if (!applyConfiguredColorSetupValue(setupKey, valueAsString(args[1]), &errorText))
									throw std::runtime_error("MRSETUP(" + setupKey + ") failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
								break;
						}
					runtimeErrorLevel() = 0;
				} else if (name == "MRFEPROFILE") {
					std::string errorText;
					if (!mrvmIsStartupSettingsMode()) throw std::runtime_error("MRFEPROFILE is only allowed in settings.mrmac during startup.");
					if (args.size() != 4 || !isStringLike(args[0]) || !isStringLike(args[1]) || !isStringLike(args[2]) || !isStringLike(args[3])) throw std::runtime_error("MRFEPROFILE expects (string, string, string, string).");
					if (!applyConfiguredEditExtensionProfileDirective(valueAsString(args[0]), valueAsString(args[1]), valueAsString(args[2]), valueAsString(args[3]), &errorText)) throw std::runtime_error("MRFEPROFILE failed: " + (errorText.empty() ? std::string("invalid directive.") : errorText));
					runtimeErrorLevel() = 0;
				} else if (name == "MRCOMPILERPROFILE") {
					std::string errorText;
					if (!mrvmIsStartupSettingsMode()) throw std::runtime_error("MRCOMPILERPROFILE is only allowed in settings.mrmac during startup.");
					if (args.size() != 4 || !isStringLike(args[0]) || !isStringLike(args[1]) || !isStringLike(args[2]) || !isStringLike(args[3])) throw std::runtime_error("MRCOMPILERPROFILE expects (string, string, string, string).");
					if (!applyConfiguredCompilerProfileDirective(valueAsString(args[0]), valueAsString(args[1]), valueAsString(args[2]), valueAsString(args[3]), &errorText)) throw std::runtime_error("MRCOMPILERPROFILE failed: " + (errorText.empty() ? std::string("invalid directive.") : errorText));
					runtimeErrorLevel() = 0;
				} else if (name == "UI_DIALOG") {
					beginMacroUiDialog(args);
					runtimeErrorLevel() = 0;
				} else if (name == "UI_LABEL") {
					addMacroUiLabel(args);
					runtimeErrorLevel() = 0;
				} else if (name == "UI_BUTTON") {
					addMacroUiButton(args);
					runtimeErrorLevel() = 0;
				} else if (name == "UI_DISPLAY") {
					addMacroUiDisplay(args);
					runtimeErrorLevel() = 0;
				} else if (name == "UI_INPUT") {
					addMacroUiInput(args);
					runtimeErrorLevel() = 0;
				} else if (name == "UI_LISTBOX") {
					addMacroUiListBox(args);
					runtimeErrorLevel() = 0;
				} else if (name == "UI_LIST_CLEAR") {
					clearMacroUiItemList(args);
					runtimeErrorLevel() = 0;
				} else if (name == "UI_LIST_ADD") {
					addMacroUiItemListValue(args);
					runtimeErrorLevel() = 0;
				} else if (name == "CREATE_GLOBAL_STR" || name == "SET_GLOBAL_STR") {
					if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1])) throw std::runtime_error(name + " expects (string, string).");
					setGlobalValue(valueAsString(args[0]), TYPE_STR, makeString(valueAsString(args[1])));
				} else if (name == "SET_GLOBAL_INT") {
					if (args.size() != 2 || !isStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SET_GLOBAL_INT expects (string, int).");
					setGlobalValue(valueAsString(args[0]), TYPE_INT, makeInt(args[1].i));
				} else if (name == "SET_GLOBAL_HASH") {
					if (args.size() != 2 || !isStringLike(args[0]) || args[1].type != TYPE_HASH) throw std::runtime_error("SET_GLOBAL_HASH expects (string, hash).");
					setGlobalValueFromStore(valueAsString(args[0]), TYPE_HASH, args[1], *mHashStore);
				} else if (name == "SNIPPET_START") {
					if (!args.empty()) throw std::runtime_error("SNIPPET_START expects no arguments.");
					runtimeErrorLevel() = openSnippetSidekickFromActiveEditor(*mHashStore) ? 0 : 0;
				} else if (name == "SNIPPETS_UNLOAD") {
					GlobalEntry rootEntry;
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("SNIPPETS_UNLOAD expects one string argument.");
					if (readGlobalValue("SNIPPETS", rootEntry) && rootEntry.type == TYPE_HASH && rootEntry.value.type == TYPE_HASH) {
						mrvmSnippetUnloadLanguage(*mHashStore, g_runtimeEnv.globalHashStore, rootEntry.value, valueAsString(args[0]));
					}
					runtimeErrorLevel() = 0;
				} else if (name == "SNIPPET_NEXT_PLACEHOLDER") {
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("SNIPPET_NEXT_PLACEHOLDER expects (string).");
					runtimeErrorLevel() = 0;
				} else if (name == "SNIPPET_PREV_PLACEHOLDER") {
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("SNIPPET_PREV_PLACEHOLDER expects (string).");
					runtimeErrorLevel() = 0;
				} else if (name == "MARQUEE" || name == "MARQUEE_WARNING" || name == "MARQUEE_ERROR" || name == "MAKE_MESSAGE") {
					int deferredError = 0;
					if (dispatchDeferredVisualUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
				} else if (name == "WORKING") {
					int deferredError = 0;
					if (dispatchDeferredVisualUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
				} else if (name == "BRAIN") {
					int deferredError = 0;
					if (dispatchDeferredVisualUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
				} else if (name == "PUT_BOX") {
					int deferredError = 0;
					if (dispatchDeferredVisualUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
				} else if (name == "WRITE") {
					int deferredError = 0;
					if (dispatchDeferredVisualUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
				} else if (name == "CLR_LINE") {
					int deferredError = 0;
					if (dispatchDeferredVisualUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
				} else if (name == "GOTOXY") {
					int deferredError = 0;
					if (dispatchDeferredVisualUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
				} else if (name == "PUT_LINE_NUM" || name == "PUT_COL_NUM") {
					int deferredError = 0;
					if (dispatchDeferredVisualUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
				} else if (name == "SCROLL_BOX_UP") {
					int deferredError = 0;
					if (dispatchDeferredVisualUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
				} else if (name == "SCROLL_BOX_DN") {
					int deferredError = 0;
					if (dispatchDeferredVisualUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
				} else if (name == "CLEAR_SCREEN") {
					int deferredError = 0;
					if (dispatchDeferredVisualUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
				} else if (name == "KILL_BOX") {
					int deferredError = 0;
					if (dispatchDeferredVisualUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
				} else if (name == "REGISTER_MENU_ITEM" || name == "REMOVE_MENU_ITEM") {
					int deferredError = 0;
					if (dispatchDeferredMenuUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
				} else if (name == "DELAY") {
					int millis = 0;
					BackgroundEditSession *session = nullptr;
					if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("DELAY expects one integer argument.");
					millis = normalizeDelayMillis(valueAsInt(args[0]));
					if (millis == 0) {
						runtimeErrorLevel() = 0;
						continue;
					}
					session = currentBackgroundEditSession();
					if (session != nullptr) {
						session->deferredUiCommands.emplace_back(mrducDelay, millis);
						runtimeErrorLevel() = 0;
						continue;
					}
					if (allowAsyncDelay) throw VmDelayYield(millis);
					if (!sleepDelayBlocking(millis)) {
						cancelledExecution = true;
						appendLogLine("VM Notice: DELAY interrupted by cancellation.", true);
						runtimeErrorLevel() = 5007;
						break;
					}
					runtimeErrorLevel() = 0;
				} else if (name == "BEEP") {
					if (!args.empty()) throw std::runtime_error("BEEP expects no arguments.");
					static_cast<void>(::write(STDOUT_FILENO, "\a", 1));
					static_cast<void>(::fsync(STDOUT_FILENO));
					runtimeErrorLevel() = 0;
				} else if (name == "LOAD_MACRO_FILE") {
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("LOAD_MACRO_FILE expects one string argument.");
					loadMacroFileIntoRegistry(valueAsString(args[0]), nullptr);
				} else if (name == "UNLOAD_MACRO") {
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("UNLOAD_MACRO expects one string argument.");
					unloadMacroFromRegistry(valueAsString(args[0]));
				} else if (name == "CHANGE_DIR") {
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("CHANGE_DIR expects one string argument.");
					if (changeDirectoryPath(valueAsString(args[0]))) runtimeErrorLevel() = 0;
					else
						runtimeErrorLevel() = errno != 0 ? errno : 1;
				} else if (name == "DEL_FILE") {
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("DEL_FILE expects one string argument.");
					if (deleteFilePath(valueAsString(args[0]))) runtimeErrorLevel() = 0;
					else
						runtimeErrorLevel() = errno != 0 ? errno : 1;
				} else if (name == "SET_FILE_ATTR") {
					struct stat st;
					mode_t modeBits;
					std::string path;
					if (args.size() != 2 || !isStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SET_FILE_ATTR expects (string, int).");
					path = expandUserPath(valueAsString(args[0]));
					if (::stat(path.c_str(), &st) != 0) {
						runtimeErrorLevel() = errno != 0 ? errno : 1;
						continue;
					}
					modeBits = st.st_mode;
					if ((valueAsInt(args[1]) & 0x01) != 0) modeBits &= static_cast<mode_t>(~(S_IWUSR | S_IWGRP | S_IWOTH));
					else
						modeBits |= static_cast<mode_t>(S_IWUSR);
					runtimeErrorLevel() = ::chmod(path.c_str(), modeBits) == 0 ? 0 : (errno != 0 ? errno : 1);
				} else if (name == "SHELL_TO_OS") {
					int exitCode = 0;
					if (args.size() != 2 || !isStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SHELL_TO_OS expects (string, int).");
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					(void)mrvmUiNewScreen();
					exitCode = std::system(valueAsString(args[0]).c_str());
					(void)mrvmUiNewScreen();
					runtimeErrorLevel() = exitCode;
				} else if (name == "WRITE_SOD") {
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("WRITE_SOD expects one string argument.");
					mrLogMessage(valueAsString(args[0]));
					runtimeErrorLevel() = 0;
				} else if (name == "SAVE_OS_SCREEN") {
					if (!args.empty()) throw std::runtime_error("SAVE_OS_SCREEN expects no arguments.");
					runtimeErrorLevel() = 0;
				} else if (name == "REST_OS_SCREEN") {
					if (!args.empty()) throw std::runtime_error("REST_OS_SCREEN expects no arguments.");
					(void)mrvmUiNewScreen();
					runtimeErrorLevel() = 0;
				} else if (name == "QUIT") {
					int returnCode = 0;
					if (args.size() > 1 || (args.size() == 1 && args[0].type != TYPE_INT)) throw std::runtime_error("QUIT expects zero or one integer argument.");
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					if (!args.empty()) returnCode = valueAsInt(args[0]);
					runtimeErrorLevel() = returnCode;
					(void)dispatchApplicationCommandEvent(cmQuit);
				} else if (name == "LOAD_FILE") {
					MREditWindow *win;
					std::string path;
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("LOAD_FILE expects one string argument.");
					path = expandUserPath(valueAsString(args[0]));
					win = activeMacroEditWindow();
					if (win == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					if (!fileExistsPath(path)) {
						runtimeErrorLevel() = 3002;
						continue;
					}
					if (!win->loadFromFile(path.c_str())) {
						runtimeErrorLevel() = 3002;
						continue;
					}
					g_runtimeEnv.lastFileName = win->currentFileName();
					runtimeErrorLevel() = 0;
				} else if (name == "LOAD_BLOCK") {
					MREditWindow *win = activeMacroEditWindow();
					std::string path;
					if (args.empty()) {
						if (currentBackgroundEditSession() != nullptr) {
							runtimeErrorLevel() = 1001;
							continue;
						}
						runtimeErrorLevel() = dispatchMRKeymapAction("MR_LOAD_BLOCK_FROM_FILE") ? 0 : 1001;
						continue;
					}
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("LOAD_BLOCK expects zero or one string argument.");
					if (win == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					path = expandUserPath(valueAsString(args[0]));
					if (!fileExistsPath(path) || !mrvmEditorLoadBlockFromFile(win, path)) {
						runtimeErrorLevel() = 3002;
						continue;
					}
					g_runtimeEnv.lastFileName = path;
					runtimeErrorLevel() = 0;
				} else if (name == "SAVE_FILE") {
					MREditWindow *win = activeMacroEditWindow();
					if (!args.empty()) throw std::runtime_error("SAVE_FILE expects no arguments.");
					if (win == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					if (!win->saveCurrentFile()) {
						runtimeErrorLevel() = 2002;
						continue;
					}
					g_runtimeEnv.lastFileName = win->currentFileName();
					runtimeErrorLevel() = 0;
				} else if (name == "SAVE_BLOCK") {
					MREditWindow *win = activeMacroEditWindow();
					MRFileEditor *editor = currentEditor();
					std::string path;
					if (args.empty()) {
						if (currentBackgroundEditSession() != nullptr) {
							runtimeErrorLevel() = 1001;
							continue;
						}
						runtimeErrorLevel() = dispatchMRKeymapAction("MR_SAVE_BLOCK_TO_FILE") ? 0 : 1001;
						continue;
					}
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("SAVE_BLOCK expects zero or one string argument.");
					if (win == nullptr || editor == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					path = expandUserPath(valueAsString(args[0]));
					if (!mrvmEditorSaveCurrentBlockToFile(win, editor, path)) {
						runtimeErrorLevel() = errno != 0 ? errno : 1010;
						continue;
					}
					g_runtimeEnv.lastFileName = path;
					runtimeErrorLevel() = 0;
				} else if (name == "SET_INDENT_LEVEL") {
					if (!args.empty()) throw std::runtime_error("SET_INDENT_LEVEL expects no arguments.");
					runtimeErrorLevel() = setCurrentEditorIndentLevel(currentEditorColumn(currentEditor())) ? 0 : 1001;
				} else if (name == "REPLACE") {
					MRFileEditor *editor;
					bool replaced;
					BackgroundEditSession *session;
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("REPLACE expects one string argument.");
					editor = currentEditor();
					session = currentBackgroundEditSession();
					if (editor == nullptr && session == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					if (editor != nullptr) replaced = replaceLastSearch(editor, valueAsString(args[0]));
					else
						replaced = replaceLastSearchBackground(valueAsString(args[0]));
					runtimeErrorLevel() = replaced ? 0 : 1010;
				} else if (name == "TEXT") {
					MRFileEditor *editor;
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("TEXT expects one string argument.");
					editor = currentEditor();
					if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					insertEditorText(editor, valueAsString(args[0]));
					runtimeErrorLevel() = 0;
				} else if (name == "KEY_IN") {
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("KEY_IN expects one string argument.");
					if (!replayKeyInputSequence(valueAsString(args[0]))) {
						runtimeErrorLevel() = currentBackgroundEditSession() != nullptr ? 1010 : 1001;
						continue;
					}
					runtimeErrorLevel() = 0;
				} else if (name == "READ_KEY") {
					int key1 = 0;
					int key2 = 0;
					if (!args.empty()) throw std::runtime_error("READ_KEY expects no arguments.");
					if (!readMacroKeyPair(true, key1, key2)) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					runtimeErrorLevel() = 0;
				} else if (name == "PUSH_KEY") {
					if (args.size() != 2 || args[0].type != TYPE_INT || args[1].type != TYPE_INT) throw std::runtime_error("PUSH_KEY expects two integer arguments.");
					runtimeErrorLevel() = pushQueuedKeyPair(valueAsInt(args[0]), valueAsInt(args[1])) ? 0 : 1010;
				} else if (name == "PASS_KEY") {
					if (args.size() != 2 || args[0].type != TYPE_INT || args[1].type != TYPE_INT) throw std::runtime_error("PASS_KEY expects two integer arguments.");
					runtimeErrorLevel() = passMacroKeyPairToUi(valueAsInt(args[0]), valueAsInt(args[1])) ? 0 : 1010;
				} else if (name == "PUSH_LABELS") {
					if (!args.empty()) throw std::runtime_error("PUSH_LABELS expects no arguments.");
					g_runtimeEnv.functionLabelStack.emplace_back();
					applyFunctionLabelState();
					runtimeErrorLevel() = 0;
				} else if (name == "POP_LABELS") {
					if (!args.empty()) throw std::runtime_error("POP_LABELS expects no arguments.");
					if (g_runtimeEnv.functionLabelStack.size() > 1) g_runtimeEnv.functionLabelStack.pop_back();
					applyFunctionLabelState();
					runtimeErrorLevel() = 0;
				} else if (name == "FLABEL") {
					int keyNumber;
					int mode;
					MacroFunctionLabelFrame &frame = currentFunctionLabelFrame();
					if (args.size() != 3 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("FLABEL expects (string, int, int).");
					keyNumber = valueAsInt(args[1]);
					mode = valueAsInt(args[2]);
					if (keyNumber <= 0 || keyNumber >= 49) {
						runtimeErrorLevel() = 1010;
						continue;
					}
					if (mode == 255) mode = currentUiMacroMode();
					if (mode == MACRO_MODE_DOS_SHELL) frame.shellLabels[static_cast<std::size_t>(keyNumber)] = valueAsString(args[0]);
					else
						frame.editLabels[static_cast<std::size_t>(keyNumber)] = valueAsString(args[0]);
					applyFunctionLabelState();
					runtimeErrorLevel() = 0;
				} else if (name == "MACRO_TO_KEY") {
					TKey key;
					int mode = MACRO_MODE_EDIT;
					ExplicitKeyBinding binding;
					std::string refreshError;
					if (args.size() != 3 || !isStringLike(args[1]) || args[2].type != TYPE_INT) throw std::runtime_error("MACRO_TO_KEY expects (key, string, int).");
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					if (!parseBindingKeyValue(args[0], key) || !parseBindingModeValue(valueAsInt(args[2]), mode)) {
						runtimeErrorLevel() = 1010;
						continue;
					}
					removeExplicitBindingsForKey(key, mode);
					binding.key = key;
					binding.mode = mode;
					binding.kind = ExplicitBindingKind::MacroSpec;
					binding.macroSpec = valueAsString(args[1]);
					g_runtimeEnv.explicitKeyBindings.push_back(binding);
					if (!mrvmUiRefreshRuntimeMenus(&refreshError)) throw std::runtime_error("MACRO_TO_KEY could not refresh runtime menus: " + (refreshError.empty() ? std::string("unknown error.") : refreshError));
					runtimeErrorLevel() = 0;
				} else if (name == "CMD_TO_KEY") {
					TKey key;
					int mode = MACRO_MODE_EDIT;
					ExplicitKeyBinding binding;
					if (args.size() != 3 || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("CMD_TO_KEY expects (key, int, int).");
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					if (!parseBindingKeyValue(args[0], key) || !parseBindingModeValue(valueAsInt(args[2]), mode)) {
						runtimeErrorLevel() = 1010;
						continue;
					}
					removeExplicitBindingsForKey(key, mode);
					binding.key = key;
					binding.mode = mode;
					binding.kind = ExplicitBindingKind::Command;
					binding.commandId = valueAsInt(args[1]);
					g_runtimeEnv.explicitKeyBindings.push_back(binding);
					runtimeErrorLevel() = 0;
				} else if (name == "UNASSIGN_KEY") {
					TKey key;
					int mode = MACRO_MODE_EDIT;
					std::string refreshError;
					if (args.size() != 2 || args[1].type != TYPE_INT) throw std::runtime_error("UNASSIGN_KEY expects (key, int).");
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					if (!parseBindingKeyValue(args[0], key) || !parseBindingModeValue(valueAsInt(args[1]), mode)) {
						runtimeErrorLevel() = 1010;
						continue;
					}
					removeExplicitBindingsForKey(key, mode);
					clearRegisteredBindingsForKey(&key, mode, mode == MACRO_MODE_ALL);
					if (!mrvmUiRefreshRuntimeMenus(&refreshError)) throw std::runtime_error("UNASSIGN_KEY could not refresh runtime menus: " + (refreshError.empty() ? std::string("unknown error.") : refreshError));
					runtimeErrorLevel() = 0;
				} else if (name == "UNASSIGN_ALL_KEYS") {
					std::string refreshError;
					if (!args.empty()) throw std::runtime_error("UNASSIGN_ALL_KEYS expects no arguments.");
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					g_runtimeEnv.explicitKeyBindings.clear();
					clearRegisteredBindingsForKey(nullptr, MACRO_MODE_ALL, true);
					if (!mrvmUiRefreshRuntimeMenus(&refreshError)) throw std::runtime_error("UNASSIGN_ALL_KEYS could not refresh runtime menus: " + (refreshError.empty() ? std::string("unknown error.") : refreshError));
					runtimeErrorLevel() = 0;
				} else if (name == "KEY_RECORD") {
					if (!args.empty()) throw std::runtime_error("KEY_RECORD expects no arguments.");
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					runtimeErrorLevel() = dispatchApplicationCommandEvent(cmMrMacroToggleRecording) ? 0 : 1001;
				} else if (name == "PLAY_KEY_MACRO") {
					TKey key;
					const char *text = nullptr;
					std::size_t textLength = 0;
					char textByte = '\0';
					int mode = currentUiMacroMode();
					if ((args.size() != 2 && args.size() != 3) || args[0].type != TYPE_INT || args[1].type != TYPE_INT || (args.size() == 3 && args[2].type != TYPE_INT)) throw std::runtime_error("PLAY_KEY_MACRO expects (int, int[, int]).");
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					if (args.size() == 3 && !parseBindingModeValue(valueAsInt(args[2]), mode)) {
						runtimeErrorLevel() = 1010;
						continue;
					}
					if (!keyPairToTKey(valueAsInt(args[0]), valueAsInt(args[1]), key, text, textLength, textByte)) {
						runtimeErrorLevel() = 1010;
						continue;
					}
					if (executeExplicitKeyBinding(key, mode, &log)) {
						runtimeErrorLevel() = 0;
						continue;
					}
					runtimeErrorLevel() = 1001;
				} else if (name == "PUT_LINE") {
					MRFileEditor *editor;
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("PUT_LINE expects one string argument.");
					editor = currentEditor();
					if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					replaceEditorLine(editor, valueAsString(args[0]));
					runtimeErrorLevel() = 0;
				} else if (name == "CR") {
					MRFileEditor *editor = currentEditor();
					if (!args.empty()) throw std::runtime_error("CR expects no arguments.");
					if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					carriageReturnEditor(editor);
					runtimeErrorLevel() = 0;
				} else if (name == "DEL_CHAR") {
					MRFileEditor *editor = currentEditor();
					if (!args.empty()) throw std::runtime_error("DEL_CHAR expects no arguments.");
					if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					deleteEditorChars(editor, 1);
					runtimeErrorLevel() = 0;
				} else if (name == "DEL_CHARS") {
					MRFileEditor *editor = currentEditor();
					if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("DEL_CHARS expects one integer argument.");
					if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					deleteEditorChars(editor, valueAsInt(args[0]));
					runtimeErrorLevel() = 0;
				} else if (name == "DEL_LINE") {
					MRFileEditor *editor = currentEditor();
					if (!args.empty()) throw std::runtime_error("DEL_LINE expects no arguments.");
					if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					deleteEditorLine(editor);
					runtimeErrorLevel() = 0;
				} else if (name == "BACK_SPACE") {
					MRFileEditor *editor = currentEditor();
					if (!args.empty()) throw std::runtime_error("BACK_SPACE expects no arguments.");
					if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					backspaceEditor(editor);
					runtimeErrorLevel() = 0;
				} else if (name == "WORD_WRAP_LINE") {
					MRFileEditor *editor = currentEditor();
					if (!args.empty()) throw std::runtime_error("WORD_WRAP_LINE expects no arguments.");
					if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					wordWrapEditorLine(editor);
					runtimeErrorLevel() = 0;
				} else if (const char *actionId = keymapActionIdForMacroCommand(name)) {
					if (!args.empty()) throw std::runtime_error((name + " expects no arguments.").c_str());
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					runtimeErrorLevel() = dispatchMRKeymapAction(actionId) ? 0 : 1001;
				} else if (name == "SET_RANDOM_MARK" || name == "GET_RANDOM_MARK") {
					if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error((name + " expects one integer argument.").c_str());
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					const std::string sequenceText = "<" + std::to_string(args[0].i) + ">";
					runtimeErrorLevel() = dispatchMRKeymapAction(name == "SET_RANDOM_MARK" ? "MRMAC_MARK_SET_RANDOM_ACCESS" : "MRMAC_MARK_GET_RANDOM_ACCESS", sequenceText) ? 0 : 1001;
				} else if (name == "EXTEND_BLOCK_BY_MOTION") {
					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("EXTEND_BLOCK_BY_MOTION expects one key sequence string argument.");
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					runtimeErrorLevel() = dispatchMRKeymapAction("MRMAC_BLOCK_EXTEND_BY_MOTION", valueAsString(args[0])) ? 0 : 1001;
				} else if (name == "LEFT" || name == "RIGHT" || name == "UP" || name == "DOWN" || name == "HOME" || name == "EOL" || name == "TOF" || name == "EOF" || name == "WORD_LEFT" || name == "WORD_RIGHT" || name == "FIRST_WORD" || name == "MARK_POS" || name == "GOTO_MARK" || name == "POP_MARK" || name == "PAGE_UP" || name == "PAGE_DOWN" || name == "NEXT_PAGE_BREAK" || name == "LAST_PAGE_BREAK" || name == "TAB_RIGHT" || name == "TAB_LEFT" || name == "INDENT" || name == "UNDENT" || name == "BLOCK_BEGIN" || name == "BLOCK_LINE" || name == "COL_BLOCK_BEGIN" || name == "BLOCK_COL" || name == "STR_BLOCK_BEGIN" || name == "BLOCK_END" || name == "BLOCK_OFF" || name == "BLOCK_TOGGLE_VISIBILITY" || name == "BLOCK_STAT" || name == "COPY_BLOCK" || name == "MOVE_BLOCK" || name == "DELETE_BLOCK" || name == "CREATE_WINDOW" || name == "DELETE_WINDOW" || name == "ERASE_WINDOW" || name == "MODIFY_WINDOW" || name == "LINK_WINDOW" || name == "UNLINK_WINDOW" || name == "ZOOM" || name == "REDRAW" || name == "NEW_SCREEN" ||
				           name == "MOVE_WIN_TO_NEXT_DESKTOP" || name == "MOVE_WIN_TO_PREV_DESKTOP" || name == "MOVE_VIEWPORT_RIGHT" || name == "MOVE_VIEWPORT_LEFT" || name == "SAVE_WORKSPACE" || name == "LOAD_WORKSPACE" || name == "SAVE_SETTINGS") {
					MRFileEditor *editor = currentEditor();
					bool ok = false;
					int deferredError = 0;
					if (!args.empty()) throw std::runtime_error((name + " expects no arguments.").c_str());
					if (queueDeferredUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
					if (editor == nullptr && currentBackgroundEditSession() == nullptr && name != "CREATE_WINDOW" && name != "BLOCK_STAT" && name != "SAVE_SETTINGS") {
						runtimeErrorLevel() = 1001;
						continue;
					}
					if (name == "LEFT") ok = moveEditorLeft(editor);
					else if (name == "RIGHT")
						ok = moveEditorRight(editor);
					else if (name == "UP")
						ok = moveEditorUp(editor);
					else if (name == "DOWN")
						ok = moveEditorDown(editor);
					else if (name == "HOME")
						ok = moveEditorHome(editor);
					else if (name == "EOL")
						ok = moveEditorEol(editor);
					else if (name == "TOF")
						ok = moveEditorTof(editor);
					else if (name == "EOF")
						ok = moveEditorEof(editor);
					else if (name == "WORD_LEFT")
						ok = moveEditorWordLeft(editor);
					else if (name == "WORD_RIGHT")
						ok = moveEditorWordRight(editor);
					else if (name == "FIRST_WORD")
						ok = moveEditorFirstWord(editor);
					else if (name == "MARK_POS")
						ok = markEditorPosition(activeMacroEditWindow(), editor);
					else if (name == "GOTO_MARK")
						ok = gotoEditorMark(activeMacroEditWindow(), editor);
					else if (name == "POP_MARK")
						ok = popEditorMark(activeMacroEditWindow());
					else if (name == "PAGE_UP")
						ok = moveEditorPageUp(editor);
					else if (name == "PAGE_DOWN")
						ok = moveEditorPageDown(editor);
					else if (name == "NEXT_PAGE_BREAK")
						ok = moveEditorNextPageBreak(editor);
					else if (name == "LAST_PAGE_BREAK")
						ok = moveEditorLastPageBreak(editor);
					else if (name == "TAB_RIGHT")
						ok = moveEditorTabRight(editor);
					else if (name == "TAB_LEFT")
						ok = moveEditorTabLeft(editor);
					else if (name == "INDENT")
						ok = indentEditor(editor);
					else if (name == "UNDENT")
						ok = undentEditor(editor);
					else if (name == "BLOCK_BEGIN" || name == "BLOCK_LINE") {
						ok = mrvmUiBlockBeginLine();
					} else if (name == "COL_BLOCK_BEGIN" || name == "BLOCK_COL") {
						ok = mrvmUiBlockBeginColumn();
					} else if (name == "STR_BLOCK_BEGIN") {
						ok = mrvmUiBlockBeginStream();
					} else if (name == "BLOCK_END") {
						ok = mrvmUiBlockEndMarking();
					} else if (name == "BLOCK_OFF") {
						ok = mrvmUiBlockTurnMarkingOff();
					} else if (name == "BLOCK_TOGGLE_VISIBILITY") {
						ok = mrvmUiBlockToggleVisibility();
					} else if (name == "BLOCK_STAT") {
						ok = true;
						runtimeReturnInt() = blockStatusValue(activeMacroEditWindow());
					} else if (name == "COPY_BLOCK")
						ok = true;
					else if (name == "MOVE_BLOCK")
						ok = true;
					else if (name == "DELETE_BLOCK")
						ok = mrvmUiDeleteBlock();
					else if (name == "CREATE_WINDOW")
						ok = mrvmUiCreateWindow();
					else if (name == "DELETE_WINDOW")
						ok = mrvmUiDeleteCurrentWindow();
					else if (name == "ERASE_WINDOW")
						ok = mrvmUiEraseCurrentWindow();
					else if (name == "MODIFY_WINDOW")
						ok = mrvmUiModifyCurrentWindow();
					else if (name == "LINK_WINDOW")
						ok = mrvmUiLinkCurrentWindow();
					else if (name == "UNLINK_WINDOW")
						ok = mrvmUiUnlinkCurrentWindow();
					else if (name == "ZOOM")
						ok = mrvmUiZoomCurrentWindow();
					else if (name == "REDRAW")
						ok = mrvmUiRedrawCurrentWindow();
					else if (name == "NEW_SCREEN")
						ok = mrvmUiNewScreen();
					else if (name == "MOVE_WIN_TO_NEXT_DESKTOP")
						ok = returnWithDirectScreenMutation(moveToNextVirtualDesktop());
					else if (name == "MOVE_WIN_TO_PREV_DESKTOP")
						ok = returnWithDirectScreenMutation(moveToPrevVirtualDesktop());
					else if (name == "MOVE_VIEWPORT_RIGHT")
						ok = returnWithDirectScreenMutation(viewportRight());
					else if (name == "MOVE_VIEWPORT_LEFT")
						ok = returnWithDirectScreenMutation(viewportLeft());
					else if (name == "SAVE_WORKSPACE") {
						mrSaveWorkspace("");
						ok = returnWithDirectScreenMutation(true);
					} else if (name == "LOAD_WORKSPACE") {
						mrLoadWorkspace("");
						ok = returnWithDirectScreenMutation(true);
					} else if (name == "SAVE_SETTINGS") {
						std::string errorText;
						ok = mrvmPersistConfiguredSettingsSnapshot(&errorText);
						if (!ok) throw std::runtime_error("SAVE_SETTINGS failed: " + (errorText.empty() ? std::string("Unable to persist settings snapshot.") : errorText));
					}
					runtimeErrorLevel() = ok ? 0 : 1001;
				} else if (name == "GOTO_LINE") {
					MRFileEditor *editor = currentEditor();
					if (args.empty()) {
						if (currentBackgroundEditSession() != nullptr) {
							runtimeErrorLevel() = 1001;
							continue;
						}
						runtimeErrorLevel() = dispatchMRKeymapAction("MRMAC_CURSOR_GOTO_LINE") ? 0 : 1001;
						continue;
					}
					if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("GOTO_LINE expects zero or one integer argument.");
					if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					runtimeErrorLevel() = gotoEditorLine(editor, valueAsInt(args[0])) ? 0 : 1010;
				} else if (name == "GOTO_COL") {
					MRFileEditor *editor = currentEditor();
					if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("GOTO_COL expects one integer argument.");
					if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					runtimeErrorLevel() = gotoEditorCol(editor, valueAsInt(args[0])) ? 0 : 1010;
				} else if (name == "SWITCH_WINDOW") {
					int deferredError = 0;
					if (queueDeferredUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
					if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("SWITCH_WINDOW expects one integer argument.");
					runtimeErrorLevel() = mrvmUiSwitchWindow(valueAsInt(args[0])) ? 0 : 1001;
				} else if (name == "SIZE_WINDOW") {
					int deferredError = 0;
					if (queueDeferredUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
					if (args.size() != 4 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT) throw std::runtime_error("SIZE_WINDOW expects four integer arguments.");
					runtimeErrorLevel() = mrvmUiSizeCurrentWindow(valueAsInt(args[0]), valueAsInt(args[1]), valueAsInt(args[2]), valueAsInt(args[3])) ? 0 : 1010;
				} else if (name == "WINDOW_COPY" || name == "WINDOW_MOVE") {
					if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error((name + " expects one integer argument.").c_str());
					runtimeErrorLevel() = 0;
				} else if (name == "RUN_MACRO") {
					std::string spec;
					std::string filePart;
					std::string macroPart;
					std::string paramPart;
					std::string targetFileKey;
					std::string macroKey;
					std::map<std::string, MacroRef>::iterator mit;
					bool backgroundStaged = currentBackgroundEditSession() != nullptr;

					if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("RUN_MACRO expects one string argument.");

					spec = valueAsString(args[0]);
					if (!parseRunMacroSpec(spec, filePart, macroPart, paramPart)) {
						runtimeErrorLevel() = 5001;
						continue;
					}

					macroKey = upperKey(macroPart);
					if (!filePart.empty()) targetFileKey = resolveLoadedFileKeyForSpec(filePart);
					if (!filePart.empty() && targetFileKey.empty()) targetFileKey = makeFileKey(filePart);

					mit = g_runtimeEnv.loadedMacros.find(macroKey);
					if (mit == g_runtimeEnv.loadedMacros.end() || (!targetFileKey.empty() && mit->second.fileKey != targetFileKey)) {
						if (backgroundStaged) {
							runtimeErrorLevel() = 5001;
							continue;
						}
						if (!filePart.empty()) {
							if (!loadMacroFileIntoRegistry(filePart, &targetFileKey)) continue;
						} else {
							if (!loadMacroFileIntoRegistry(macroPart, &targetFileKey)) continue;
						}
						mit = g_runtimeEnv.loadedMacros.find(macroKey);
					}

					if (mit == g_runtimeEnv.loadedMacros.end() || (!targetFileKey.empty() && mit->second.fileKey != targetFileKey)) {
						runtimeErrorLevel() = 5001;
						continue;
					}
					if (!executeLoadedMacroWithConfiguredKeymapBatch(mit, macroKey, paramPart, &log)) continue;
				} else {
					throw std::runtime_error("Unknown procedure: " + name);
				}
			} else if (opcode == OP_TVCALL) {
				std::string funcName;
				std::string funcNameUpper;
				int deferredError = 0;
				readCString(funcName);
				unsigned char argc = bytecode[ip++];
				std::vector<Value> args = popArgs(argc);
				funcNameUpper = upperKey(funcName);

				appendLogLine("TVCALL: " + funcName + " (" + std::to_string(argc) + " params)");

				if (dispatchDeferredUiTvCall(funcNameUpper, args, deferredError)) {
					runtimeErrorLevel() = 0;
					continue;
				}
				runtimeErrorLevel() = 0;
			} else if (opcode == OP_HALT) {
				appendLogLine("Program end reached.");
				break;
			} else {
				char hexOp[10];
				std::snprintf(hexOp, sizeof(hexOp), "0x%02X", opcode);
				throw std::runtime_error(std::string("Unknown opcode ") + hexOp);
			}

			if (g_backgroundMacroStopToken == nullptr && currentBackgroundEditSession() == nullptr) syncLinkedWindowsFrom(activeMacroEditWindow());
		}
	} catch (const VmDelayYield &yield) {
		int millis = normalizeDelayMillis(yield.millis);
		std::uint64_t taskId = 0;
		std::shared_ptr<std::atomic_bool> ready = std::make_shared<std::atomic_bool>(false);
		std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
		std::uint64_t generation = mAsyncDelayGeneration + 1;

		taskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::Custom, 0, generation, "macro-delay", [ready, cancelled, millis](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
			mr::coprocessor::Result result;
			result.task = info;
			if (millis > 0) {
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(millis);
				while (std::chrono::steady_clock::now() < deadline) {
					if (stopToken.stop_requested() || info.cancelRequested()) {
						cancelled->store(true, std::memory_order_release);
						ready->store(true, std::memory_order_release);
						result.status = mr::coprocessor::TaskStatus::Cancelled;
						return result;
					}
					auto remaining = deadline - std::chrono::steady_clock::now();
					auto slice = std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
					if (slice > std::chrono::milliseconds(10)) slice = std::chrono::milliseconds(10);
					if (slice.count() <= 0) break;
					std::this_thread::sleep_for(slice);
				}
			}
			ready->store(true, std::memory_order_release);
			result.status = mr::coprocessor::TaskStatus::Completed;
			return result;
		});
		if (taskId == 0 || ready == nullptr || cancelled == nullptr) throw std::runtime_error("DELAY scheduling failed.");
		appendLogLine("VM Notice: DELAY(" + std::to_string(millis) + ") yielded [gen " + std::to_string(generation) + "].", true);
		mAsyncDelayPending = true;
		mAsyncDelayReady = true;
		mAsyncIp = ip;
		mAsyncCallStack = call_stack;
		mAsyncReturnInt = state.returnInt;
		mAsyncReturnStr = state.returnStr;
		mAsyncErrorLevel = state.errorLevel;
		mAsyncSavedParameterString = savedParameterString;
		mAsyncMacroFramePushed = pushedMacroFrame;
		mAsyncDelayReadyFlag = ready;
		mAsyncDelayCancelledFlag = cancelled;
		mAsyncDelayTaskId = taskId;
		mAsyncDelayGeneration = generation;
		mAsyncDelayMillis = millis;
		if (parentState != nullptr) {
			parentState->returnInt = state.returnInt;
			parentState->returnStr = state.returnStr;
			parentState->errorLevel = state.errorLevel;
			parentState->parameterString = savedParameterString;
		} else {
			g_runtimeEnv.returnInt = state.returnInt;
			g_runtimeEnv.returnStr = state.returnStr;
			g_runtimeEnv.errorLevel = state.errorLevel;
			g_runtimeEnv.parameterString = savedParameterString;
		}
		return;
	} catch (const std::exception &ex) {
		appendLogLine(std::string("VM Error: ") + ex.what(), true);
	}

	if (resumeFromDelay && resumeGeneration != mAsyncDelayGeneration) {
		appendLogLine("VM Notice: stale DELAY resume generation ignored.", true);
	}

	if (parentState != nullptr) {
		parentState->returnInt = state.returnInt;
		parentState->returnStr = state.returnStr;
		parentState->errorLevel = state.errorLevel;
		parentState->parameterString = savedParameterString;
	} else {
		g_runtimeEnv.returnInt = state.returnInt;
		g_runtimeEnv.returnStr = state.returnStr;
		g_runtimeEnv.errorLevel = state.errorLevel;
		g_runtimeEnv.parameterString = savedParameterString;
	}
	clearAsyncDelayState();
	if (pushedMacroFrame) g_runtimeEnv.macroStack.pop_back();
}

std::vector<std::size_t> mrvmUiCopyWindowMarkStack(const void *windowKey) {
	std::vector<std::size_t> out;
	std::map<const void *, std::vector<uint>>::const_iterator it;

	if (windowKey == nullptr) return out;
	it = g_runtimeEnv.markStacks.find(windowKey);
	if (it == g_runtimeEnv.markStacks.end()) return out;
	out.reserve(it->second.size());
	for (unsigned int i : it->second)
		out.push_back(static_cast<std::size_t>(i));
	return out;
}

bool mrvmUiCopyWindowLastSearch(const void *windowKey, const std::string &fileName, std::size_t &start, std::size_t &end, std::size_t &cursor) {
	start = 0;
	end = 0;
	cursor = 0;
	if (!g_runtimeEnv.lastSearchValid || windowKey == nullptr) return false;
	if (g_runtimeEnv.lastSearchWindow != windowKey) return false;
	if (g_runtimeEnv.lastSearchFileName != fileName) return false;
	start = g_runtimeEnv.lastSearchStart;
	end = g_runtimeEnv.lastSearchEnd;
	cursor = g_runtimeEnv.lastSearchCursor;
	return true;
}

void mrvmUiCopyGlobals(std::vector<std::string> &order, std::map<std::string, int> &ints, std::map<std::string, std::string> &strings) {
	order.clear();
	ints.clear();
	strings.clear();
	order.reserve(g_runtimeEnv.globalOrder.size());
	for (std::size_t i = 0; i < g_runtimeEnv.globalOrder.size(); ++i) {
		const std::string &key = g_runtimeEnv.globalOrder[i];
		std::map<std::string, GlobalEntry>::const_iterator it = g_runtimeEnv.globals.find(key);
		if (it == g_runtimeEnv.globals.end()) continue;
		order.push_back(key);
		if (it->second.type == TYPE_INT) ints[key] = valueAsInt(it->second.value);
		else if (it->second.type == TYPE_STR)
			strings[key] = valueAsString(it->second.value);
	}
}

void mrvmUiCopyLoadedMacros(std::vector<std::string> &order, std::map<std::string, std::string> &displayNames) {
	order.clear();
	displayNames.clear();
	order.reserve(g_runtimeEnv.macroOrder.size());
	for (std::size_t i = 0; i < g_runtimeEnv.macroOrder.size(); ++i) {
		const std::string &key = g_runtimeEnv.macroOrder[i];
		std::map<std::string, MacroRef>::const_iterator it = g_runtimeEnv.loadedMacros.find(key);
		if (it == g_runtimeEnv.loadedMacros.end()) continue;
		order.push_back(key);
		displayNames[key] = it->second.displayName;
	}
}

void mrvmUiCopyRuntimeOptions(bool &ignoreCase, bool &tabExpand) {
	ignoreCase = g_runtimeEnv.ignoreCase;
	tabExpand = g_runtimeEnv.tabExpand;
}

int mrvmUiCurrentWindowIndex(const void *windowKey) {
	std::vector<MREditWindow *> windows;

	if (windowKey == nullptr) return currentEditWindowIndex();
	windows = allEditWindows();
	for (std::size_t i = 0; i < windows.size(); ++i)
		if (windows[i] == windowKey) return static_cast<int>(i) + 1;
	return 0;
}

int mrvmUiWindowCount() {
	return countEditWindows();
}

int mrvmUiLinkStatus(const void *windowKey) {
	const MREditWindow *win = static_cast<const MREditWindow *>(windowKey);

	if (windowKey == nullptr) return currentLinkStatus();
	return isWindowLinked(const_cast<MREditWindow *>(win)) ? 1 : 0;
}

bool mrvmUiWindowGeometry(const void *windowKey, int &x1, int &y1, int &x2, int &y2) {
	MREditWindow *win;
	TRect bounds;

	if (windowKey == nullptr) return currentWindowGeometry(x1, y1, x2, y2);
	win = const_cast<MREditWindow *>(static_cast<const MREditWindow *>(windowKey));
	if (win == nullptr) return false;
	bounds = win->getBounds();
	x1 = bounds.a.x + 1;
	y1 = bounds.a.y + 1;
	x2 = bounds.b.x;
	y2 = bounds.b.y;
	return true;
}

int mrvmUiScreenWidth() {
	return static_cast<int>(TDisplay::getCols());
}

int mrvmUiScreenHeight() {
	return static_cast<int>(TDisplay::getRows());
}

bool mrvmUiCursorPosition(int &x, int &y) {
	return currentUiCursorPosition(x, y);
}

void mrvmUiReplaceWindowMarkStack(const void *windowKey, const std::vector<std::size_t> &offsets) {
	std::vector<uint> marks;

	if (windowKey == nullptr) return;
	if (offsets.empty()) {
		g_runtimeEnv.markStacks.erase(windowKey);
		return;
	}
	marks.reserve(offsets.size());
	for (unsigned long offset : offsets)
		marks.push_back(static_cast<uint>(offset));
	g_runtimeEnv.markStacks[windowKey] = marks;
}

void mrvmUiReplaceWindowLastSearch(const void *windowKey, const std::string &fileName, bool valid, std::size_t start, std::size_t end, std::size_t cursor) {
	if (!valid) {
		if (g_runtimeEnv.lastSearchWindow == windowKey) {
			g_runtimeEnv.lastSearchValid = false;
			g_runtimeEnv.lastSearchWindow = nullptr;
			g_runtimeEnv.lastSearchFileName.clear();
			g_runtimeEnv.lastSearchStart = 0;
			g_runtimeEnv.lastSearchEnd = 0;
			g_runtimeEnv.lastSearchCursor = 0;
		}
		return;
	}
	g_runtimeEnv.lastSearchValid = true;
	g_runtimeEnv.lastSearchWindow = windowKey;
	g_runtimeEnv.lastSearchFileName = fileName;
	g_runtimeEnv.lastSearchStart = start;
	g_runtimeEnv.lastSearchEnd = end;
	g_runtimeEnv.lastSearchCursor = cursor;
}

void mrvmUiReplaceGlobals(const std::vector<std::string> &order, const std::map<std::string, int> &ints, const std::map<std::string, std::string> &strings) {
	std::map<std::string, GlobalEntry> preservedHashes;
	std::vector<std::string> preservedHashOrder;
	std::set<std::string> seen;

	for (const std::string &key : g_runtimeEnv.globalOrder) {
		std::map<std::string, GlobalEntry>::const_iterator it = g_runtimeEnv.globals.find(key);
		if (it == g_runtimeEnv.globals.end() || it->second.type != TYPE_HASH) continue;
		if (ints.find(key) != ints.end() || strings.find(key) != strings.end()) continue;
		preservedHashOrder.push_back(key);
		preservedHashes[key] = it->second;
	}

	g_runtimeEnv.globals.clear();
	g_runtimeEnv.globalOrder.clear();
	g_runtimeEnv.globalEnumIndex = 0;

	for (const auto &i : order) {
		const std::string key = upperKey(i);
		GlobalEntry entry;
		std::map<std::string, int>::const_iterator intIt;
		std::map<std::string, std::string>::const_iterator strIt;
		if (!seen.insert(key).second) continue;
		intIt = ints.find(key);
		if (intIt != ints.end()) {
			entry.type = TYPE_INT;
			entry.value = makeInt(intIt->second);
		} else {
			strIt = strings.find(key);
			if (strIt == strings.end()) continue;
			entry.type = TYPE_STR;
			entry.value = makeString(strIt->second);
		}
		entry.value.globalStorage = true;
		g_runtimeEnv.globalOrder.push_back(key);
		g_runtimeEnv.globals[key] = entry;
	}

	for (const auto &it : ints) {
		const std::string key = upperKey(it.first);
		GlobalEntry entry;
		if (!seen.insert(key).second) continue;
		entry.type = TYPE_INT;
		entry.value = makeInt(it.second);
		entry.value.globalStorage = true;
		g_runtimeEnv.globalOrder.push_back(key);
		g_runtimeEnv.globals[key] = entry;
	}
	for (const auto &string : strings) {
		const std::string key = upperKey(string.first);
		GlobalEntry entry;
		if (!seen.insert(key).second) continue;
		entry.type = TYPE_STR;
		entry.value = makeString(string.second);
		entry.value.globalStorage = true;
		g_runtimeEnv.globalOrder.push_back(key);
		g_runtimeEnv.globals[key] = entry;
	}

	for (const std::string &key : preservedHashOrder) {
		std::map<std::string, GlobalEntry>::const_iterator hashIt;
		if (!seen.insert(key).second) continue;
		hashIt = preservedHashes.find(key);
		if (hashIt == preservedHashes.end()) continue;
		g_runtimeEnv.globalOrder.push_back(key);
		g_runtimeEnv.globals[key] = hashIt->second;
	}
}

void mrvmUiReplaceRuntimeOptions(bool ignoreCase, bool tabExpand) {
	g_runtimeEnv.ignoreCase = ignoreCase;
	g_runtimeEnv.tabExpand = tabExpand;
}

void mrvmUiSyncLinkedWindowsFrom(MREditWindow *window) {
	syncLinkedWindowsFrom(window);
}

bool mrvmUiRegisterMenuItem(const std::string &menuTitle, const std::string &itemTitle, const std::string &macroSpec, const std::string &ownerSpec, std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) {
		if (errorMessage != nullptr) *errorMessage = "REGISTER_MENU_ITEM requires an active MRMenuBar.";
		return false;
	}
	return returnWithDirectScreenMutation(menuBar->registerRuntimeMenuItem(menuTitle, itemTitle, macroSpec, ownerSpec, errorMessage));
}

bool mrvmUiRemoveMenuItem(const std::string &menuTitle, const std::string &itemTitle, const std::string &ownerSpec, std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) {
		if (errorMessage != nullptr) *errorMessage = "REMOVE_MENU_ITEM requires an active MRMenuBar.";
		return false;
	}
	return returnWithDirectScreenMutation(menuBar->removeRuntimeMenuItem(menuTitle, itemTitle, ownerSpec, errorMessage));
}

bool mrvmUiRemoveRuntimeMenusOwnedByMacroSpec(const std::string &ownerSpec, std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) return true;
	return returnWithDirectScreenMutation(menuBar->removeRuntimeNodesOwnedByMacroSpec(ownerSpec, errorMessage));
}

bool mrvmUiRemoveRuntimeMenusOwnedByFile(const std::string &fileSpec, std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) return true;
	return returnWithDirectScreenMutation(menuBar->removeRuntimeNodesOwnedByFile(fileSpec, errorMessage));
}

std::string mrvmUiMenuKeyLabelForMacroSpec(const std::string &macroSpec) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	std::string filePart;
	std::string macroPart;
	std::string paramPart;
	std::string targetFileKey;
	const std::string targetMacroKey = [&]() {
		parseRunMacroSpec(macroSpec, filePart, macroPart, paramPart);
		return upperKey(macroPart);
	}();
	const int mode = currentUiMacroMode();

	if (targetMacroKey.empty()) return std::string();
	if (!filePart.empty()) targetFileKey = resolveLoadedFileKeyForSpec(filePart);
	if (!filePart.empty() && targetFileKey.empty()) targetFileKey = makeFileKey(filePart);
	for (auto it = g_runtimeEnv.explicitKeyBindings.rbegin(); it != g_runtimeEnv.explicitKeyBindings.rend(); ++it) {
		if (it->kind != ExplicitBindingKind::MacroSpec || !bindingModeMatches(it->mode, mode)) continue;
		if (!macroSpecTargetsLoadedMacro(it->macroSpec, targetFileKey, targetMacroKey)) continue;
		return menuLabelFromBindingKey(it->key);
	}
	{
		const auto macroIt = g_runtimeEnv.loadedMacros.find(targetMacroKey);
		if (macroIt == g_runtimeEnv.loadedMacros.end()) return std::string();
		if (!targetFileKey.empty() && macroIt->second.fileKey != targetFileKey) return std::string();
		if (!macroAllowsUiMode(macroIt->second, mode) || !macroIt->second.hasAssignedKey) return std::string();
		if (!macroIt->second.assignedKeySpec.empty()) return normalizeMenuKeySpec(macroIt->second.assignedKeySpec);
		return menuLabelFromBindingKey(macroIt->second.assignedKey);
	}
}

bool mrvmUiRefreshRuntimeMenus(std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) return true;
	return returnWithDirectScreenMutation(menuBar->refreshRuntimeMenus(errorMessage));
}

bool mrvmUiMessageBox(const std::string &text) {
	try {
		messageBox(mfInformation | mfOKButton, "%s", text.c_str());
		return returnWithDirectScreenMutation(true);
	} catch (...) {
		return false;
	}
}

struct UiRenderFacade {
	static bool renderDeferredCommand(const MRMacroDeferredUiCommand &command) {
		switch (command.type) {
			case mrducCreateWindow:
				return mrvmUiCreateWindow();
			case mrducDeleteWindow:
				return mrvmUiDeleteCurrentWindow();
			case mrducModifyWindow:
				return mrvmUiModifyCurrentWindow();
			case mrducLinkWindow:
				return mrvmUiLinkCurrentWindow();
			case mrducUnlinkWindow:
				return mrvmUiUnlinkCurrentWindow();
			case mrducZoom:
				return mrvmUiZoomCurrentWindow();
			case mrducRedraw:
				return mrvmUiRedrawCurrentWindow();
			case mrducNewScreen:
				return mrvmUiNewScreen();
			case mrducSwitchWindow:
				return mrvmUiSwitchWindow(command.a1);
			case mrducSizeWindow:
				return mrvmUiSizeCurrentWindow(command.a1, command.a2, command.a3, command.a4);
			case mrducMarqueeInfo:
				return mrvmUiMarquee(0, command.text);
			case mrducMarqueeWarning:
				return mrvmUiMarquee(1, command.text);
			case mrducMarqueeError:
				return mrvmUiMarquee(2, command.text);
			case mrducMakeMessage:
				return applyMakeMessageProc(std::vector<Value>{makeString(command.text)});
			case mrducBrain:
				return mrvmUiBrain(command.a1 != 0);
			case mrducPutBox:
				return mrvmUiPutBox(command.a1, command.a2, command.a3, command.a4, command.a5, command.a6, command.text, command.a7);
			case mrducWrite:
				return mrvmUiWrite(command.text, command.a1, command.a2, command.a3, command.a4);
			case mrducClrLine:
				return mrvmUiClrLine(command.a1, command.a2, command.a3);
			case mrducGotoxy:
				return mrvmUiGotoxy(command.a1, command.a2);
			case mrducPutLineNum:
				return mrvmUiPutLineNum(command.a1);
			case mrducPutColNum:
				return mrvmUiPutColNum(command.a1);
			case mrducScrollBoxUp:
				return mrvmUiScrollBoxUp(command.a1, command.a2, command.a3, command.a4, command.a5);
			case mrducScrollBoxDn:
				return mrvmUiScrollBoxDn(command.a1, command.a2, command.a3, command.a4, command.a5);
			case mrducClearScreen:
				return mrvmUiClearScreen(command.a1);
			case mrducKillBox:
				return mrvmUiKillBox();
			case mrducRegisterMenuItem:
				return mrvmUiRegisterMenuItem(command.text, command.text2, command.text3, command.text4);
			case mrducRemoveMenuItem:
				return mrvmUiRemoveMenuItem(command.text, command.text2, command.text3);
			case mrducMessageBox:
				return mrvmUiMessageBox(command.text);
			case mrducDelay:
				return true;
			default:
				return false;
		}
	}
};

bool mrvmUiRenderFacadeRenderDeferredCommand(const MRMacroDeferredUiCommand &command) {
	return UiRenderFacade::renderDeferredCommand(command);
}

bool mrvmUiRenderDeferredCommand(const MRMacroDeferredUiCommand &command) {
	return mrvmUiRenderFacadeRenderDeferredCommand(command);
}

void mrvmBootstrapBoundMacroIndex(const std::string &directoryPath, std::size_t *fileCount, std::size_t *bindingCount) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	std::vector<std::string> files = listMrmacFilesInDirectory(directoryPath);
	std::set<std::string> dedupe;

	g_runtimeEnv.indexedBoundMacros.clear();
	g_runtimeEnv.indexedBoundFiles.clear();
	g_runtimeEnv.indexedWarmupCursor = 0;
	g_runtimeEnv.indexedWarmupAttemptedFiles.clear();

	for (const auto &file : files) {
		std::string source;
		std::vector<TKey> keys;
		std::string fileKey;

		if (!readTextFile(file, source)) continue;
		if (!parseIndexedBindingHeaders(source, keys) || keys.empty()) continue;
		fileKey = makeFileKey(file);
		if (dedupe.insert(fileKey).second) g_runtimeEnv.indexedBoundFiles.push_back(file);
		for (auto key : keys)
			g_runtimeEnv.indexedBoundMacros.emplace_back(key, file);
	}

	if (fileCount != nullptr) *fileCount = g_runtimeEnv.indexedBoundFiles.size();
	if (bindingCount != nullptr) *bindingCount = g_runtimeEnv.indexedBoundMacros.size();
}

bool mrvmWarmLoadNextIndexedMacroFile(std::string *loadedFilePath, std::string *failedFilePath, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);

	if (loadedFilePath != nullptr) loadedFilePath->clear();
	if (failedFilePath != nullptr) failedFilePath->clear();
	if (errorMessage != nullptr) errorMessage->clear();

	while (g_runtimeEnv.indexedWarmupCursor < g_runtimeEnv.indexedBoundFiles.size()) {
		const std::string &filePath = g_runtimeEnv.indexedBoundFiles[g_runtimeEnv.indexedWarmupCursor++];
		std::string fileKey = makeFileKey(filePath);
		std::string localError;

		if (!g_runtimeEnv.indexedWarmupAttemptedFiles.insert(fileKey).second) continue;
		if (g_runtimeEnv.loadedFiles.find(fileKey) != g_runtimeEnv.loadedFiles.end()) continue;
		if (loadMacroFileIntoRegistry(filePath, nullptr)) {
			if (loadedFilePath != nullptr) *loadedFilePath = filePath;
			return true;
		}
		if (failedFilePath != nullptr) *failedFilePath = filePath;
		{
			const char *compileError = get_last_compile_error();
			if (compileError != nullptr) localError = compileError;
		}
		if (localError.empty()) localError = "Unable to load macro file.";
		if (errorMessage != nullptr) *errorMessage = localError;
		return false;
	}
	return false;
}

bool mrvmHasPendingIndexedMacroWarmup() {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	return g_runtimeEnv.indexedWarmupCursor < g_runtimeEnv.indexedBoundFiles.size();
}

bool mrvmLoadMacroFile(const std::string &spec, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);

	if (!loadMacroFileIntoRegistry(spec, nullptr)) {
		if (errorMessage != nullptr) {
			const char *compileError = get_last_compile_error();
			if (compileError != nullptr && *compileError != '\0') *errorMessage = compileError;
			else
				*errorMessage = "Unable to load macro file.";
		}
		return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool mrvmRunMacroSpec(const std::string &spec, std::string *errorMessage, std::vector<std::string> *logLines) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);

	if (logLines != nullptr) logLines->clear();
	if (executeRuntimeMacroSpec(spec, logLines)) {
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (errorMessage == nullptr) return false;

	switch (g_runtimeEnv.errorLevel) {
		case 5001:
			*errorMessage = "Macro specification could not be resolved.";
			break;
		case 5005:
			*errorMessage = "Macro file could not be compiled.";
			break;
		case 5006:
			*errorMessage = "Macro conflicts with a loaded or running macro.";
			break;
		case 5007:
			*errorMessage = "Macro execution stack could not be completed.";
			break;
		default:
			*errorMessage = "Macro execution failed.";
			break;
	}
	return false;
}

bool mrvmRunAssignedMacroForKey(unsigned short keyCode, unsigned short controlKeyState, std::string &executedMacroName, std::vector<std::string> *logLines) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	TKey pressed(keyCode, controlKeyState);
	int mode = currentUiMacroMode();
	const bool rawCtrlSpace = keyCode == kbNoKey && (controlKeyState & kbCtrlShift) != 0 && (controlKeyState & (kbAltShift | kbSuperShift | kbPaste)) == 0;
	bool traceSnippetKey = false;
	auto dispatchLoadedBinding = [&]() -> bool {
		for (std::size_t i = g_runtimeEnv.macroOrder.size(); i > 0; --i) {
			const std::string &macroKey = g_runtimeEnv.macroOrder[i - 1];
			std::map<std::string, MacroRef>::iterator it = g_runtimeEnv.loadedMacros.find(macroKey);

			if (it == g_runtimeEnv.loadedMacros.end()) continue;
			if (!it->second.hasAssignedKey) continue;
			if (!macroAllowsUiMode(it->second, mode)) continue;
			if (!bindingKeysEqual(pressed, it->second.assignedKey)) continue;

			logCalculatorHotkeyState("vm-loaded-match", pressed, it->second.displayName);
			if (traceSnippetKey) mrLogMessage(("KEYDBG vm loaded binding match macro=" + it->second.displayName).c_str());
			executedMacroName = it->second.displayName;
			executeLoadedMacroWithConfiguredKeymapBatch(it, macroKey, std::string(), logLines);
			return true;
		}
		return false;
	};

	if (rawCtrlSpace) pressed = TKey(static_cast<ushort>(' '), kbCtrlShift);
	traceSnippetKey = rawCtrlSpace || (pressed.code == static_cast<ushort>(' ') && (pressed.mods & kbCtrlShift) != 0) || (pressed.code == static_cast<ushort>('@') && (pressed.mods & kbCtrlShift) != 0);
	executedMacroName.clear();
	if (logLines != nullptr) logLines->clear();
	if (g_keyReplayDepth > 0) return false;
	if (traceSnippetKey) {
		char line[512];
		std::snprintf(line, sizeof(line), "KEYDBG vm assigned-key rawCode=0x%04X rawMods=0x%04X code=0x%04X mods=0x%04X mode=%d explicit=%zu loaded=%zu indexed=%zu", static_cast<unsigned>(keyCode), static_cast<unsigned>(controlKeyState), static_cast<unsigned>(pressed.code), static_cast<unsigned>(pressed.mods), mode, g_runtimeEnv.explicitKeyBindings.size(), g_runtimeEnv.loadedMacros.size(), g_runtimeEnv.indexedBoundMacros.size());
		mrLogMessage(line);
	}
	logCalculatorHotkeyState("vm-enter", pressed);
	if (executeExplicitKeyBinding(pressed, mode, logLines)) {
		executedMacroName = "<bound>";
		logCalculatorHotkeyState("vm-explicit-consumed", pressed);
		if (traceSnippetKey) mrLogMessage("KEYDBG vm assigned-key consumed by explicit binding");
		return true;
	}
	if (traceSnippetKey) mrLogMessage("KEYDBG vm assigned-key no explicit binding");
	if (dispatchLoadedBinding()) return true;
	if (traceSnippetKey) mrLogMessage("KEYDBG vm assigned-key no loaded binding");
	if (!tryLoadIndexedMacroForKey(pressed)) {
		if (traceSnippetKey) mrLogMessage("KEYDBG vm assigned-key no indexed binding");
		return false;
	}
	logCalculatorHotkeyState("vm-indexed-loaded", pressed);
	if (traceSnippetKey) mrLogMessage("KEYDBG vm assigned-key indexed binding loaded");
	return dispatchLoadedBinding();
}
