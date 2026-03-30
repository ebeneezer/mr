#define Uses_MsgBox
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TScrollBar
#define Uses_TListViewer
#define Uses_TObject
#include <tvision/tv.h>

#include "mrmac.h"
#include "mrvm.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <glob.h>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "../ui/TMREditWindow.hpp"
#include "../dialogs/MRWindowListDialog.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {
using Value = VirtualMachine::Value;

struct GlobalEntry {
	int type;
	Value value;
};

struct MacroRef {
	std::string fileKey;
	std::string displayName;
	std::size_t entryOffset;
	bool firstRunPending;
	bool transientAttr;
	bool dumpAttr;
	bool permAttr;

	MacroRef()
	    : fileKey(), displayName(), entryOffset(0), firstRunPending(true), transientAttr(false),
	      dumpAttr(false), permAttr(false) {
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

struct MacroStackFrame {
	std::string macroName;
	bool firstRun;

	MacroStackFrame() : macroName(), firstRun(false) {
	}
	MacroStackFrame(const std::string &aName, bool aFirstRun)
	    : macroName(aName), firstRun(aFirstRun) {
	}
};

struct RuntimeEnvironment {
	std::map<std::string, GlobalEntry> globals;
	std::vector<std::string> globalOrder;
	std::size_t globalEnumIndex;
	std::string parameterString;
	int returnInt;
	std::string returnStr;
	int errorLevel;
	std::map<std::string, LoadedMacroFile> loadedFiles;
	std::map<std::string, MacroRef> loadedMacros;
	std::vector<std::string> macroOrder;
	std::size_t macroEnumIndex;
	std::vector<MacroStackFrame> macroStack;
	std::vector<std::string> fileMatches;
	std::size_t fileMatchIndex;
	std::string lastFileName;
	std::map<const void *, std::vector<uint>> markStacks;
	std::string startupCommand;
	std::vector<std::string> processArgs;
	std::string executablePath;
	std::string executableDir;
	std::string shellPath;
	std::string shellVersion;
	bool ignoreCase;
	bool tabExpand;
	bool lastSearchValid;
	const void *lastSearchWindow;
	std::string lastSearchFileName;
	std::size_t lastSearchStart;
	std::size_t lastSearchEnd;
	std::size_t lastSearchCursor;
	std::map<const void *, int> windowLinkGroups;
	int nextWindowLinkGroupId;

	RuntimeEnvironment()
	    : globals(), globalOrder(), globalEnumIndex(0), parameterString(), returnInt(0),
	      returnStr(), errorLevel(0), loadedFiles(), loadedMacros(), macroOrder(),
	      macroEnumIndex(0), macroStack(), fileMatches(), fileMatchIndex(0), lastFileName(),
	      markStacks(), startupCommand(), processArgs(), executablePath(), executableDir(),
	      shellPath(), shellVersion(), ignoreCase(false), tabExpand(true), lastSearchValid(false),
	      lastSearchWindow(NULL), lastSearchFileName(), lastSearchStart(0), lastSearchEnd(0),
	      lastSearchCursor(0), windowLinkGroups(), nextWindowLinkGroupId(1) {
	}
};

struct SplitTextBuffer {
	std::vector<std::string> lines;
	bool trailingNewline;

	SplitTextBuffer() : lines(), trailingNewline(false) {
	}
};

struct BackgroundEditSession {
	mr::editor::TextDocument document;
	mr::editor::StagedEditTransaction transaction;
	std::size_t cursorOffset;
	std::size_t selectionStart;
	std::size_t selectionEnd;
	bool insertMode;
	int indentLevel;
	int pageLines;
	std::string fileName;
	bool fileChanged;
	std::vector<uint> markStack;

	BackgroundEditSession()
	    : document(), transaction(), cursorOffset(0), selectionStart(0), selectionEnd(0),
	      insertMode(true), indentLevel(1), pageLines(20), fileName(), fileChanged(false),
	      markStack() {
	}

	bool hasSelection() const noexcept {
		return selectionEnd > selectionStart;
	}

	void clearSelection() noexcept {
		selectionStart = cursorOffset;
		selectionEnd = cursorOffset;
	}

	void clampState() noexcept {
		std::size_t length = document.length();
		cursorOffset = std::min(cursorOffset, length);
		selectionStart = std::min(selectionStart, length);
		selectionEnd = std::min(selectionEnd, length);
		if (selectionEnd < selectionStart)
			std::swap(selectionStart, selectionEnd);
	}
};

static RuntimeEnvironment g_runtimeEnv;
static std::recursive_mutex g_vmExecutionMutex;
static thread_local BackgroundEditSession *g_backgroundEditSession = NULL;
static thread_local const std::stop_token *g_backgroundMacroStopToken = NULL;
static thread_local std::shared_ptr<std::atomic_bool> g_backgroundMacroCancelFlag;

static std::string valueAsString(const Value &value);
static int valueAsInt(const Value &value);
static void enforceStringLength(const std::string &s);
static std::string trimAscii(const std::string &value);
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
static bool loadMacroFileIntoRegistry(const std::string &spec, std::string *loadedFileKey = NULL);
static bool unloadMacroFromRegistry(const std::string &macroName);
static bool parseRunMacroSpec(const std::string &spec, std::string &filePart,
                              std::string &macroPart, std::string &paramPart);
static bool ensureLoadedFileResident(const std::string &fileKey);
static bool evictTransientFileImage(const std::string &fileKey);
static std::string expandUserPath(const std::string &path);
static bool fileExistsPath(const std::string &path);
static std::vector<TMREditWindow *> allEditWindows();
static void cleanupWindowLinkGroups();
static int windowLinkGroupOf(TMREditWindow *win);
static bool isWindowLinked(TMREditWindow *win);
static int currentLinkStatus();
static TMREditWindow *selectLinkTargetWindow(TMREditWindow *current);
static bool prepareWindowLink(TMREditWindow *current, TMREditWindow *target,
                              TMREditWindow *&source, TMREditWindow *&dest);
static bool linkCurrentEditWindow();
static bool unlinkCurrentEditWindow();
static void syncLinkedWindowsFrom(TMREditWindow *source);
static bool redrawCurrentEditWindow();
static bool redrawEntireScreen();
static bool zoomCurrentEditWindow();
static int findFirstFileMatch(const std::string &pattern);
static int findNextFileMatch();
static TMREditWindow *currentEditWindow();
static TMRFileEditor *currentEditor();
static BackgroundEditSession *currentBackgroundEditSession() noexcept;
static bool backgroundMacroCancelRequested() noexcept;
static bool backgroundReplaceRange(const mr::editor::Range &range, const std::string &text,
                                   std::size_t cursorPos);
static bool backgroundSetCursor(std::size_t target);
static std::size_t backgroundLineMoveOffset(std::size_t offset, int delta);
static std::size_t backgroundCharPtrOffset(std::size_t lineStart, int column);
static bool backgroundWordChar(char c) noexcept;
static std::size_t backgroundPrevWordOffset(std::size_t offset);
static std::size_t backgroundNextWordOffset(std::size_t offset);
static std::string snapshotEditorText(TMRFileEditor *editor);
static std::size_t searchLimitForward(const std::string &text, std::size_t start, int numLines) {
	if (numLines <= 0)
		return text.size();
	std::size_t pos = start;
	int remaining = numLines;
	while (pos < text.size()) {
		if (text[pos] == '\n') {
			--remaining;
			if (remaining == 0)
				return pos;
		}
		++pos;
	}
	return text.size();
}

static std::size_t searchLimitBackward(const std::string &text, std::size_t start, int numLines) {
	if (numLines <= 0)
		return 0;
	std::size_t pos = std::min(start, text.size());
	int remaining = numLines;
	while (pos > 0) {
		--pos;
		if (text[pos] == '\n') {
			--remaining;
			if (remaining == 0)
				return pos + 1;
		}
	}
	return 0;
}

static bool searchEditorForward(TMRFileEditor *editor, const std::string &needle, int numLines,
                                bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd);
static bool searchEditorBackward(TMRFileEditor *editor, const std::string &needle, int numLines,
                                 bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd);
static bool replaceLastSearch(TMRFileEditor *editor, const std::string &replacement);
static Value currentEditorCharValue();
static std::string currentEditorLineText(TMRFileEditor *editor);
static std::string currentEditorWord(TMRFileEditor *editor, const std::string &delimiters);
static int defaultTabWidth();
static bool isVirtualChar(char c);
static int nextTabStopColumn(int col);
static int prevTabStopColumn(int col);
static std::string makeIndentFill(int targetCol, bool preferTabs);
static std::string crunchTabsString(const std::string &value);
static std::string expandTabsString(const std::string &value, bool toVirtuals);
static std::string tabsToSpacesString(const std::string &value);
static int currentEditorIndentLevel();
static bool setCurrentEditorIndentLevel(int level);
static bool currentEditorInsertMode();
static bool setCurrentEditorInsertMode(bool on);
static bool insertEditorText(TMRFileEditor *editor, const std::string &text);
static bool replaceEditorLine(TMRFileEditor *editor, const std::string &text);
static bool deleteEditorChars(TMRFileEditor *editor, int count);
static bool deleteEditorLine(TMRFileEditor *editor);
static int currentEditorColumn(TMRFileEditor *editor);
static int currentEditorLineNumber(TMRFileEditor *editor);
static bool moveEditorLeft(TMRFileEditor *editor);
static bool moveEditorRight(TMRFileEditor *editor);
static bool moveEditorUp(TMRFileEditor *editor);
static bool moveEditorDown(TMRFileEditor *editor);
static bool moveEditorHome(TMRFileEditor *editor);
static bool moveEditorEol(TMRFileEditor *editor);
static bool moveEditorTof(TMRFileEditor *editor);
static bool moveEditorEof(TMRFileEditor *editor);
static bool moveEditorWordLeft(TMRFileEditor *editor);
static bool moveEditorWordRight(TMRFileEditor *editor);
static bool moveEditorFirstWord(TMRFileEditor *editor);
static bool gotoEditorLine(TMRFileEditor *editor, int lineNum);
static bool gotoEditorCol(TMRFileEditor *editor, int colNum);
static bool currentEditorAtEof(TMRFileEditor *editor);
static bool currentEditorAtEol(TMRFileEditor *editor);
static int currentEditorRow(TMRFileEditor *editor);
static bool markEditorPosition(TMREditWindow *win, TMRFileEditor *editor);
static bool gotoEditorMark(TMREditWindow *win, TMRFileEditor *editor);
static bool popEditorMark(TMREditWindow *win);
static bool moveEditorPageUp(TMRFileEditor *editor);
static bool moveEditorPageDown(TMRFileEditor *editor);
static bool moveEditorNextPageBreak(TMRFileEditor *editor);
static bool moveEditorLastPageBreak(TMRFileEditor *editor);
static bool replaceEditorBuffer(TMRFileEditor *editor, const std::string &text,
                                std::size_t cursorPos);
static SplitTextBuffer splitBufferLines(const std::string &text);
static std::string joinBufferLines(const SplitTextBuffer &buffer);
static std::size_t bufferOffsetForLine(const SplitTextBuffer &buffer, int lineIndex);
static std::size_t bufferOffsetForLineColumn(const SplitTextBuffer &buffer, int lineIndex,
                                             int colIndex);
static int lineIndexForPtr(TMRFileEditor *editor, uint ptr);
static bool currentBlockInfo(TMREditWindow *win, TMRFileEditor *editor, int &mode, uint &anchor,
                             uint &end);
static bool copyCurrentBlock(TMREditWindow *win, TMRFileEditor *editor);
static bool moveCurrentBlock(TMREditWindow *win, TMRFileEditor *editor);
static bool deleteCurrentBlock(TMREditWindow *win, TMRFileEditor *editor);
static bool copyBlockFromWindow(TMREditWindow *srcWin, TMRFileEditor *srcEditor,
                                TMREditWindow *destWin, TMRFileEditor *destEditor);
static bool moveBlockFromWindow(TMREditWindow *srcWin, TMRFileEditor *srcEditor,
                                TMREditWindow *destWin, TMRFileEditor *destEditor);
static bool extractCurrentBlockText(TMREditWindow *win, TMRFileEditor *editor, std::string &out);
static bool saveCurrentBlockToFile(TMREditWindow *win, TMRFileEditor *editor, const std::string &path);
static int countEditWindows();
static int currentEditWindowIndex();
static bool currentWindowGeometry(int &x1, int &y1, int &x2, int &y2);
static bool createEditWindow();
static bool switchEditWindow(int index);
static bool sizeCurrentEditWindow(int x1, int y1, int x2, int y2);
static bool deleteCurrentEditWindow();
static bool eraseCurrentEditWindow();
static bool modifyCurrentEditWindow();

static std::string upperKey(const std::string &value) {
	std::string out = value;
	for (std::string::size_type i = 0; i < out.size(); ++i)
		out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
	return out;
}

static void appendUniqueString(std::vector<std::string> &values, const std::string &value) {
	if (value.empty())
		return;
	if (std::find(values.begin(), values.end(), value) == values.end())
		values.push_back(value);
}

static void noteExecutionFlags(MRMacroExecutionProfile &profile, unsigned flags,
                               const std::string &symbol = std::string()) {
	if (flags == 0)
		return;
	profile.flags |= flags;
	if (symbol.empty())
		return;
	if ((flags & mrefStagedWrite) != 0)
		appendUniqueString(profile.stagedWriteSymbols, symbol);
	if ((flags & mrefUiAffinity) != 0)
		appendUniqueString(profile.uiAffinitySymbols, symbol);
	if ((flags & mrefExternalIo) != 0)
		appendUniqueString(profile.externalIoSymbols, symbol);
}

static bool skipBytecodeBytes(std::size_t length, std::size_t &ip, std::size_t count) {
	if (count > length || ip > length - count)
		return false;
	ip += count;
	return true;
}

static bool readBytecodeCString(const unsigned char *bytecode, std::size_t length, std::size_t &ip,
                                std::string &out) {
	std::size_t start = ip;
	if (bytecode == NULL || ip >= length)
		return false;
	while (ip < length && bytecode[ip] != '\0')
		++ip;
	if (ip >= length)
		return false;
	out.assign(reinterpret_cast<const char *>(bytecode + start), ip - start);
	++ip;
	return true;
}

static unsigned classifyPureOpcode(unsigned char opcode) {
	switch (opcode) {
		case OP_PUSH_I:
		case OP_PUSH_R:
		case OP_PUSH_S:
		case OP_STORE_VAR:
		case OP_LOAD_VAR:
		case OP_GOTO:
		case OP_DEF_VAR:
		case OP_JZ:
		case OP_CALL:
		case OP_RET:
		case OP_VAL:
		case OP_RVAL:
		case OP_FIRST_GLOBAL:
		case OP_NEXT_GLOBAL:
		case OP_HALT:
		case OP_ADD:
		case OP_SUB:
		case OP_MUL:
		case OP_DIV:
		case OP_MOD:
		case OP_NEG:
		case OP_CMP_EQ:
		case OP_CMP_NE:
		case OP_CMP_LT:
		case OP_CMP_GT:
		case OP_CMP_LE:
		case OP_CMP_GE:
		case OP_AND:
		case OP_OR:
		case OP_NOT:
		case OP_SHL:
		case OP_SHR:
			return mrefBackgroundSafe;
		default:
			return 0;
	}
}

static unsigned classifyIntrinsicName(const std::string &name) {
	if (name == "FILE_EXISTS" || name == "FIRST_FILE" || name == "NEXT_FILE" ||
	    name == "GET_ENVIRONMENT")
		return mrefExternalIo;
	if (name == "SEARCH_FWD" || name == "SEARCH_BWD" || name == "GET_WORD")
		return mrefUiAffinity;
	return mrefBackgroundSafe;
}

static unsigned classifyProcVarName(const std::string &name) {
	if (name == "CRUNCH_TABS" || name == "EXPAND_TABS" || name == "TABS_TO_SPACES")
		return mrefBackgroundSafe;
	return mrefUiAffinity;
}

static unsigned classifyProcName(const std::string &name) {
	if (name == "SET_GLOBAL_STR" || name == "SET_GLOBAL_INT" || name == "UNLOAD_MACRO")
		return mrefBackgroundSafe;
	if (name == "LOAD_MACRO_FILE" || name == "CHANGE_DIR" || name == "DEL_FILE")
		return mrefExternalIo;
	if (name == "LOAD_FILE" || name == "SAVE_FILE" || name == "SAVE_BLOCK")
		return mrefUiAffinity | mrefExternalIo;
	if (name == "REPLACE" || name == "TEXT" || name == "PUT_LINE" || name == "CR" ||
	    name == "DEL_CHAR" || name == "DEL_CHARS" || name == "DEL_LINE" || name == "INDENT" ||
	    name == "UNDENT" || name == "COPY_BLOCK" || name == "MOVE_BLOCK" ||
	    name == "DELETE_BLOCK" || name == "ERASE_WINDOW" || name == "WINDOW_COPY" ||
	    name == "WINDOW_MOVE")
		return mrefUiAffinity | mrefStagedWrite;
	if (name == "RUN_MACRO")
		return mrefUiAffinity | mrefStagedWrite;
	if (name == "SET_INDENT_LEVEL" || name == "LEFT" || name == "RIGHT" || name == "UP" ||
	    name == "DOWN" || name == "HOME" || name == "EOL" || name == "TOF" || name == "EOF" ||
	    name == "WORD_LEFT" || name == "WORD_RIGHT" || name == "FIRST_WORD" ||
	    name == "MARK_POS" || name == "GOTO_MARK" || name == "POP_MARK" ||
	    name == "PAGE_UP" || name == "PAGE_DOWN" || name == "NEXT_PAGE_BREAK" ||
	    name == "LAST_PAGE_BREAK" || name == "TAB_RIGHT" || name == "TAB_LEFT" ||
	    name == "BLOCK_BEGIN" || name == "COL_BLOCK_BEGIN" || name == "STR_BLOCK_BEGIN" ||
	    name == "BLOCK_END" || name == "BLOCK_OFF" || name == "CREATE_WINDOW" ||
	    name == "DELETE_WINDOW" || name == "MODIFY_WINDOW" || name == "LINK_WINDOW" ||
	    name == "UNLINK_WINDOW" || name == "ZOOM" || name == "REDRAW" || name == "NEW_SCREEN" ||
	    name == "GOTO_LINE" || name == "GOTO_COL" || name == "SWITCH_WINDOW" ||
	    name == "SIZE_WINDOW")
		return mrefUiAffinity;
	return mrefUiAffinity;
}

static unsigned classifyTvCallName(const std::string &name) {
	if (name == "MESSAGEBOX")
		return mrefUiAffinity;
	return mrefUiAffinity;
}

static void logMacroProfileLine(const char *prefix, const LoadedMacroFile &file) {
	if (TProgram::deskTop == NULL)
		return;
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

static std::string charToString(unsigned char c) {
	if (c == 0)
		return std::string();
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

static std::string uppercaseAscii(std::string value) {
	for (std::string::size_type i = 0; i < value.size(); ++i)
		value[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[i])));
	return value;
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
			if (!previousWasSpace)
				out.push_back(' ');
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
	if (slash == std::string::npos)
		return std::string::npos;
	return slash;
}

static std::size_t baseNameStart(const std::string &value) {
	std::size_t sep = findLastPathSeparator(value);
	if (sep != std::string::npos)
		return sep + 1;
	if (value.size() >= 2 && value[1] == ':')
		return 2;
	return 0;
}

static std::string getExtensionPart(const std::string &value) {
	std::size_t baseStart = baseNameStart(value);
	std::size_t dot = value.find_last_of('.');
	if (dot == std::string::npos || dot < baseStart)
		return std::string();
	return value.substr(dot);
}

static std::string getPathPart(const std::string &value) {
	std::size_t sep = findLastPathSeparator(value);
	if (sep != std::string::npos)
		return value.substr(0, sep + 1);
	if (value.size() >= 2 && value[1] == ':')
		return value.substr(0, 2);
	return std::string();
}

static std::string truncateExtensionPart(const std::string &value) {
	std::size_t baseStart = baseNameStart(value);
	std::size_t dot = value.find_last_of('.');
	if (dot == std::string::npos || dot < baseStart)
		return value;
	return value.substr(0, dot);
}

static std::string truncatePathPart(const std::string &value) {
	return value.substr(baseNameStart(value));
}

static double valueAsReal(const Value &value) {
	if (value.type == TYPE_REAL)
		return value.r;
	if (value.type == TYPE_INT)
		return static_cast<double>(value.i);
	throw std::runtime_error("numeric value expected");
}

static int valueAsInt(const Value &value) {
	if (value.type == TYPE_INT)
		return value.i;
	throw std::runtime_error("integer value expected");
}

static int compareValues(const Value &a, const Value &b) {
	if (isStringLike(a) && isStringLike(b)) {
		std::string as = valueAsString(a);
		std::string bs = valueAsString(b);
		if (as < bs)
			return -1;
		if (as > bs)
			return 1;
		return 0;
	}

	if (isNumeric(a) && isNumeric(b)) {
		double av = valueAsReal(a);
		double bv = valueAsReal(b);
		if (av < bv)
			return -1;
		if (av > bv)
			return 1;
		return 0;
	}

	throw std::runtime_error("type mismatch");
}

static Value defaultValueForType(int type) {
	switch (type) {
		case TYPE_INT:
			return makeInt(0);
		case TYPE_REAL:
			return makeReal(0.0);
		case TYPE_CHAR:
			return makeChar(0);
		case TYPE_STR:
		default:
			return makeString("");
	}
}

static Value coerceForStore(const Value &value, int targetType) {
	switch (targetType) {
		case TYPE_INT:
			if (value.type == TYPE_INT)
				return value;
			throw std::runtime_error("type mismatch");

		case TYPE_REAL:
			if (value.type == TYPE_REAL)
				return value;
			if (value.type == TYPE_INT)
				return makeReal(static_cast<double>(value.i));
			throw std::runtime_error("type mismatch");

		case TYPE_STR:
			if (value.type == TYPE_STR)
				return value;
			if (value.type == TYPE_CHAR)
				return makeString(charToString(value.c));
			throw std::runtime_error("type mismatch");

		case TYPE_CHAR:
			if (value.type == TYPE_CHAR)
				return value;
			if (value.type == TYPE_STR) {
				if (value.s.empty())
					return makeChar(0);
				return makeChar(static_cast<unsigned char>(value.s[0]));
			}
			throw std::runtime_error("type mismatch");

		default:
			throw std::runtime_error("unknown variable type");
	}
}

static void enforceStringLength(const std::string &s) {
	if (s.size() > 254)
		throw std::runtime_error("String length error.");
}

static int checkedStringIndex(int pos) {
	if (pos < 1 || pos > 254)
		throw std::runtime_error("Invalid string index on string copy operation.");
	return pos;
}

static int checkedInsertIndex(int pos) {
	if (pos < 0 || pos > 254)
		throw std::runtime_error("Invalid string index on string copy operation.");
	return pos;
}

static int findValErrorPosition(const std::string &text) {
	std::size_t i = 0;
	const std::size_t n = text.size();

	while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
		++i;
	if (i == n)
		return 1;

	if (text[i] == '+' || text[i] == '-')
		++i;

	{
		const std::size_t firstDigit = i;
		while (i < n && std::isdigit(static_cast<unsigned char>(text[i])))
			++i;
		if (i == firstDigit)
			return static_cast<int>(firstDigit + 1);
	}

	while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
		++i;
	if (i != n)
		return static_cast<int>(i + 1);
	return 0;
}

static int findRValErrorPosition(const std::string &text) {
	std::size_t i = 0;
	const std::size_t n = text.size();

	while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
		++i;
	if (i == n)
		return 1;

	if (text[i] == '+' || text[i] == '-')
		++i;

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
		if (!seenDigits)
			return static_cast<int>(i + 1);
	}

	if (i < n && (text[i] == 'e' || text[i] == 'E')) {
		const std::size_t expPos = i;
		++i;
		if (i < n && (text[i] == '+' || text[i] == '-'))
			++i;
		{
			const std::size_t firstExpDigit = i;
			while (i < n && std::isdigit(static_cast<unsigned char>(text[i])))
				++i;
			if (i == firstExpDigit)
				return static_cast<int>(expPos + 1);
		}
	}

	while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
		++i;
	if (i != n)
		return static_cast<int>(i + 1);
	return 0;
}

static std::string commandFirstLine(const std::string &command) {
	std::string line;
	FILE *pipe = ::popen(command.c_str(), "r");
	if (pipe == NULL)
		return std::string();
	char buffer[512];
	if (std::fgets(buffer, sizeof(buffer), pipe) != NULL)
		line = buffer;
	::pclose(pipe);
	while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
		line.pop_back();
	return line;
}

static std::string detectExecutablePathFromProc() {
	char buf[4096];
	ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n <= 0)
		return std::string();
	buf[n] = '\0';
	return std::string(buf);
}

static std::string normalizeDirPath(const std::string &path) {
	if (path.empty())
		return std::string("./");
	std::string out = path;
	if (out.back() != '/')
		out.push_back('/');
	return out;
}

static std::string detectExecutableDir(const std::string &argv0) {
	std::string path = detectExecutablePathFromProc();
	if (path.empty())
		path = argv0;
	if (path.empty()) {
		char cwd[4096];
		if (::getcwd(cwd, sizeof(cwd)) != NULL)
			return normalizeDirPath(std::string(cwd));
		return std::string("./");
	}
	std::size_t sep = path.find_last_of("/");
	if (sep == std::string::npos) {
		char cwd[4096];
		if (::getcwd(cwd, sizeof(cwd)) != NULL)
			return normalizeDirPath(std::string(cwd));
		return std::string("./");
	}
	return normalizeDirPath(path.substr(0, sep));
}

static std::string detectShellPath() {
	const char *comspec = std::getenv("COMSPEC");
	if (comspec != NULL && *comspec != '\0')
		return std::string(comspec);
	const char *shell = std::getenv("SHELL");
	if (shell != NULL && *shell != '\0')
		return std::string(shell);
	return std::string("/bin/sh");
}

static std::string detectShellVersion(const std::string &shellPath) {
	if (shellPath.empty())
		return std::string();
	const char *bashVersion = std::getenv("BASH_VERSION");
	const char *zshVersion = std::getenv("ZSH_VERSION");
	const char *fishVersion = std::getenv("FISH_VERSION");
	std::string base = shellPath.substr(
	    shellPath.find_last_of('/') == std::string::npos ? 0 : shellPath.find_last_of('/') + 1);
	if (base == "bash" && bashVersion != NULL && *bashVersion != '\0')
		return std::string("bash ") + bashVersion;
	if (base == "zsh" && zshVersion != NULL && *zshVersion != '\0')
		return std::string("zsh ") + zshVersion;
	if (base == "fish" && fishVersion != NULL && *fishVersion != '\0')
		return std::string("fish ") + fishVersion;
	std::string command = "'";
	for (std::string::size_type i = 0; i < shellPath.size(); ++i) {
		if (shellPath[i] == '\'')
			command += "'\\''";
		else
			command.push_back(shellPath[i]);
	}
	command += "' --version 2>/dev/null";
	std::string line = commandFirstLine(command);
	if (!line.empty())
		return line;
	return base;
}

static int detectCpuCode() {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
	return 3;
#elif defined(__aarch64__) || defined(__arm__) || defined(__riscv) || defined(__powerpc__) ||      \
    defined(__ppc64__)
	return 3;
#else
	return 3;
#endif
}

static std::string getenvValue(const std::string &name) {
	const char *value = std::getenv(name.c_str());
	if (value == NULL)
		return std::string();
	return std::string(value);
}

static std::string getEnvironmentValue(const std::string &entryName) {
	std::string key = trimAscii(entryName);
	std::size_t pos = key.find('=');
	if (pos != std::string::npos)
		key = key.substr(0, pos);
	if (key.empty())
		return std::string();
	std::string direct = getenvValue(key);
	if (!direct.empty())
		return direct;
	std::string up = upperKey(key);
	if (up == "MR_PATH")
		return g_runtimeEnv.executableDir;
	if (up == "COMSPEC")
		return g_runtimeEnv.shellPath;
	if (up == "OS_VERSION")
		return g_runtimeEnv.shellVersion;
	return std::string();
}

static bool changeDirectoryPath(const std::string &path) {
	std::string expanded = expandUserPath(trimAscii(path));
	if (expanded.empty())
		return false;
	return ::chdir(expanded.c_str()) == 0;
}

static bool deleteFilePath(const std::string &path) {
	std::string expanded = expandUserPath(trimAscii(path));
	if (expanded.empty())
		return false;
	return std::remove(expanded.c_str()) == 0;
}

static std::string expandUserPath(const std::string &path) {
	if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != NULL && *home != '\0')
			return std::string(home) + path.substr(1);
	}
	return path;
}

static bool fileExistsPath(const std::string &path) {
	struct stat st;
	std::string expanded = expandUserPath(trimAscii(path));
	if (expanded.empty())
		return false;
	return ::stat(expanded.c_str(), &st) == 0;
}

static int findFirstFileMatch(const std::string &pattern) {
	glob_t g;
	std::string expanded = expandUserPath(trimAscii(pattern));
	int rc;

	g_runtimeEnv.fileMatches.clear();
	g_runtimeEnv.fileMatchIndex = 0;
	g_runtimeEnv.lastFileName.clear();

	std::memset(&g, 0, sizeof(g));
	rc = ::glob(expanded.c_str(), 0, NULL, &g);
	if (rc == 0) {
		for (std::size_t i = 0; i < g.gl_pathc; ++i)
			g_runtimeEnv.fileMatches.push_back(std::string(g.gl_pathv[i]));
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
	if (g_runtimeEnv.fileMatches.empty())
		return 18;
	if (g_runtimeEnv.fileMatchIndex + 1 >= g_runtimeEnv.fileMatches.size())
		return 18;
	++g_runtimeEnv.fileMatchIndex;
	g_runtimeEnv.lastFileName = g_runtimeEnv.fileMatches[g_runtimeEnv.fileMatchIndex];
	return 0;
}

static TMREditWindow *currentEditWindow() {
	if (TProgram::deskTop == 0 || TProgram::deskTop->current == 0)
		return NULL;
	return dynamic_cast<TMREditWindow *>(TProgram::deskTop->current);
}

static TMRFileEditor *currentEditor() {
	TMREditWindow *win = currentEditWindow();
	return win != NULL ? win->getEditor() : NULL;
}

static TMREditWindow *currentEditWindow();

static BackgroundEditSession *currentBackgroundEditSession() noexcept {
	return g_backgroundEditSession;
}

static bool backgroundMacroCancelRequested() noexcept {
	return (g_backgroundMacroStopToken != NULL && g_backgroundMacroStopToken->stop_requested()) ||
	       (g_backgroundMacroCancelFlag != nullptr &&
	        g_backgroundMacroCancelFlag->load(std::memory_order_acquire));
}

static Value loadCurrentFileState(const std::string &key) {
	TMREditWindow *win = currentEditWindow();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (key == "FIRST_SAVE")
		return makeInt(win != NULL && win->hasBeenSavedInSession() ? 1 : 0);
	if (key == "EOF_IN_MEM")
		return makeInt(win != NULL && win->eofInMemory() ? 1 : 0);
	if (key == "BUFFER_ID")
		return makeInt(win != NULL ? win->bufferId() : 0);
	if (key == "TMP_FILE")
		return makeInt(win != NULL && win->isTemporaryFile() ? 1 : 0);
	if (key == "TMP_FILE_NAME")
		return makeString(win != NULL ? win->temporaryFileName() : "");
	if (key == "FILE_CHANGED")
		return makeInt(win != NULL ? (win->isFileChanged() ? 1 : 0)
		                          : (session != NULL && session->fileChanged ? 1 : 0));
	if (key == "FILE_NAME")
		return makeString(win != NULL ? win->currentFileName() : (session != NULL ? session->fileName : ""));
	return makeInt(0);
}

static std::string snapshotEditorText(TMRFileEditor *editor) {
	return editor != NULL ? editor->snapshotText() : std::string();
}

static bool searchEditorForward(TMRFileEditor *editor, const std::string &needle, int numLines,
                                bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd) {
	std::string text;
	std::string haystack;
	std::string query;
	std::size_t startPos;
	std::size_t endPos;
	std::size_t found;

	matchStart = matchEnd = 0;
	if (editor == NULL || needle.empty())
		return false;

	text = snapshotEditorText(editor);
	startPos = std::min<std::size_t>(editor->cursorOffset(), text.size());
	endPos = searchLimitForward(text, startPos, numLines);
	if (endPos < startPos)
		endPos = startPos;

	haystack = text.substr(startPos, endPos - startPos);
	query = needle;
	if (ignoreCase) {
		haystack = uppercaseAscii(haystack);
		query = uppercaseAscii(query);
	}

	found = haystack.find(query);
	if (found == std::string::npos)
		return false;

	matchStart = startPos + found;
	matchEnd = matchStart + needle.size();
	if (matchEnd > text.size())
		return false;
	return true;
}

static bool searchEditorBackward(TMRFileEditor *editor, const std::string &needle, int numLines,
                                 bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd) {
	std::string text;
	std::string haystack;
	std::string query;
	std::size_t startPos;
	std::size_t endPos;
	std::size_t found;

	matchStart = matchEnd = 0;
	if (editor == NULL || needle.empty())
		return false;

	text = snapshotEditorText(editor);
	endPos = std::min<std::size_t>(editor->cursorOffset(), text.size());
	startPos = searchLimitBackward(text, endPos, numLines);
	if (endPos < startPos)
		endPos = startPos;

	haystack = text.substr(
	    startPos, endPos - startPos + std::min<std::size_t>(needle.size(), text.size() - endPos));
	query = needle;
	if (ignoreCase) {
		haystack = uppercaseAscii(haystack);
		query = uppercaseAscii(query);
	}

	found = haystack.rfind(query, endPos - startPos);
	if (found == std::string::npos)
		return false;

	matchStart = startPos + found;
	matchEnd = matchStart + needle.size();
	if (matchEnd > text.size())
		return false;
	return true;
}

static bool replaceLastSearch(TMRFileEditor *editor, const std::string &replacement) {
	TMREditWindow *win = currentEditWindow();
	const char *fileName;
	if (editor == NULL || !g_runtimeEnv.lastSearchValid)
		return false;
	if (win == NULL || g_runtimeEnv.lastSearchWindow != win)
		return false;
	fileName = win->currentFileName();
	if (g_runtimeEnv.lastSearchFileName != std::string(fileName != NULL ? fileName : ""))
		return false;
	if (editor->cursorOffset() != g_runtimeEnv.lastSearchCursor)
		return false;
	if (g_runtimeEnv.lastSearchEnd < g_runtimeEnv.lastSearchStart ||
	    g_runtimeEnv.lastSearchEnd > editor->bufferLength())
		return false;

	if (!editor->replaceRangeAndSelect(static_cast<uint>(g_runtimeEnv.lastSearchStart),
	                                   static_cast<uint>(g_runtimeEnv.lastSearchEnd),
	                                   replacement.c_str(),
	                                   static_cast<uint>(replacement.size())))
		return false;

	g_runtimeEnv.lastSearchEnd = g_runtimeEnv.lastSearchStart + replacement.size();
	g_runtimeEnv.lastSearchCursor = g_runtimeEnv.lastSearchStart;
	g_runtimeEnv.lastSearchValid = false;
	return true;
}

static bool backgroundReplaceRange(const mr::editor::Range &range, const std::string &text,
                                   std::size_t cursorPos) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (session == NULL)
		return false;
	session->transaction.replace(range, text);
	session->document.replace(range, text);
	session->cursorOffset = std::min(cursorPos, session->document.length());
	session->fileChanged = true;
	session->clearSelection();
	return true;
}

static bool backgroundSetCursor(std::size_t target) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (session == NULL)
		return false;
	session->cursorOffset = session->document.clampOffset(target);
	session->clearSelection();
	return true;
}

static std::size_t backgroundCharPtrOffset(std::size_t lineStart, int column) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	std::size_t pos;
	std::size_t lineEnd;
	int target;

	if (session == NULL)
		return 0;
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

	if (session == NULL)
		return 0;
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

	if (session == NULL)
		return 0;
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

	if (session == NULL)
		return 0;
	pos = session->document.clampOffset(offset);
	len = session->document.length();
	while (pos < len && backgroundWordChar(session->document.charAt(pos)))
		++pos;
	while (pos < len && !backgroundWordChar(session->document.charAt(pos)))
		++pos;
	return pos;
}

static Value currentEditorCharValue() {
	TMRFileEditor *editor = currentEditor();
	uint lineEnd;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return makeChar(static_cast<char>(255));
		lineEnd = static_cast<uint>(session->document.lineEnd(session->cursorOffset));
		if (session->cursorOffset >= session->document.length() || session->cursorOffset >= lineEnd)
			return makeChar(static_cast<char>(255));
		return makeChar(session->document.charAt(session->cursorOffset));
	}
	lineEnd = editor->lineEndOffset(editor->cursorOffset());
	if (editor->cursorOffset() >= editor->bufferLength() || editor->cursorOffset() >= lineEnd)
		return makeChar(static_cast<char>(255));
	return makeChar(editor->charAtOffset(editor->cursorOffset()));
}

static int defaultTabWidth() {
	return 8;
}

static bool isVirtualChar(char c) {
	return static_cast<unsigned char>(c) == 255;
}

static int nextTabStopColumn(int col) {
	int width = defaultTabWidth();
	if (col < 1)
		col = 1;
	return ((col - 1) / width + 1) * width + 1;
}

static int prevTabStopColumn(int col) {
	int width = defaultTabWidth();
	if (col <= 1)
		return 1;
	return ((col - 2) / width) * width + 1;
}

static std::string makeIndentFill(int targetCol, bool preferTabs) {
	std::string out;
	int col = 1;
	if (targetCol < 1)
		targetCol = 1;
	while (col < targetCol) {
		int next = nextTabStopColumn(col);
		if (preferTabs && next <= targetCol) {
			out.push_back('	');
			col = next;
		} else {
			out.push_back(' ');
			++col;
		}
	}
	return out;
}

static std::string crunchTabsString(const std::string &value) {
	std::string out;
	out.reserve(value.size());
	for (std::string::size_type i = 0; i < value.size(); ++i)
		if (!isVirtualChar(value[i]))
			out.push_back(value[i]);
	return out;
}

static std::string expandTabsString(const std::string &value, bool toVirtuals) {
	std::string out;
	int col = 1;
	out.reserve(value.size());
	for (std::string::size_type i = 0; i < value.size(); ++i) {
		unsigned char ch = static_cast<unsigned char>(value[i]);
		if (ch == '	') {
			int next = nextTabStopColumn(col);
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
			out.push_back(value[i]);
			if (ch == '\n' || ch == '\r')
				col = 1;
			else
				++col;
		}
	}
	enforceStringLength(out);
	return out;
}

static std::string tabsToSpacesString(const std::string &value) {
	std::string out;
	int col = 1;
	out.reserve(value.size());
	for (std::string::size_type i = 0; i < value.size(); ++i) {
		unsigned char ch = static_cast<unsigned char>(value[i]);
		if (ch == '	') {
			int next = nextTabStopColumn(col);
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
			if (ch == '\n' || ch == '\r')
				col = 1;
			else
				++col;
		}
	}
	enforceStringLength(out);
	return out;
}

static int currentEditorIndentLevel() {
	TMREditWindow *win = currentEditWindow();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (win != NULL)
		return win->indentLevel();
	return session != NULL ? session->indentLevel : 1;
}

static bool setCurrentEditorIndentLevel(int level) {
	TMREditWindow *win = currentEditWindow();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (win != NULL) {
		win->setIndentLevel(level);
		return true;
	}
	if (session == NULL)
		return false;
	if (level < 1)
		level = 1;
	if (level > 254)
		level = 254;
	session->indentLevel = level;
	return true;
}

static bool currentEditorInsertMode() {
	TMRFileEditor *editor = currentEditor();
	BackgroundEditSession *session = currentBackgroundEditSession();
	return editor != NULL ? editor->insertModeEnabled()
	                      : (session != NULL ? session->insertMode : true);
}

static bool setCurrentEditorInsertMode(bool on) {
	TMRFileEditor *editor = currentEditor();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != NULL) {
		editor->setInsertModeEnabled(on);
		return true;
	}
	if (session == NULL)
		return false;
	session->insertMode = on;
	return true;
}

static std::string currentEditorLineText(TMRFileEditor *editor) {
	std::string out;
	uint start;
	uint end;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return out;
		return session->document.lineText(session->cursorOffset);
	}
	start = editor->lineStartOffset(editor->cursorOffset());
	end = editor->lineEndOffset(editor->cursorOffset());
	out.reserve(end >= start ? end - start : 0);
	for (uint p = start; p < end; ++p)
		out.push_back(editor->charAtOffset(p));
	return out;
}

static std::string currentEditorWord(TMRFileEditor *editor, const std::string &delimiters) {
	std::string out;
	uint pos;
	uint end;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return out;
		pos = static_cast<uint>(session->cursorOffset);
		end = static_cast<uint>(session->document.lineEnd(session->cursorOffset));
		while (pos < end) {
			char c = session->document.charAt(pos);
			if (delimiters.find(c) != std::string::npos)
				break;
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
		if (delimiters.find(c) != std::string::npos)
			break;
		out.push_back(c);
		pos = editor->nextCharOffset(pos);
	}
	editor->setCursorOffset(pos, 0);
	editor->revealCursor(True);
	enforceStringLength(out);
	return out;
}

static bool insertEditorText(TMRFileEditor *editor, const std::string &text) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != NULL)
		return editor->insertBufferText(text);
	if (session == NULL)
		return false;

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

static bool replaceEditorLine(TMRFileEditor *editor, const std::string &text) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != NULL)
		return editor->replaceCurrentLineText(text);
	if (session == NULL)
		return false;
	std::size_t start = session->document.lineStart(session->cursorOffset);
	std::size_t end = session->document.lineEnd(session->cursorOffset);
	return backgroundReplaceRange(mr::editor::Range(start, end), text, start);
}

static bool deleteEditorChars(TMRFileEditor *editor, int count) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != NULL)
		return editor->deleteCharsAtCursor(count);
	if (session == NULL)
		return false;
	if (count <= 0)
		return true;
	std::size_t start = session->cursorOffset;
	std::size_t end = std::min(session->document.length(), start + static_cast<std::size_t>(count));
	return backgroundReplaceRange(mr::editor::Range(start, end), std::string(), start);
}

static bool deleteEditorLine(TMRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != NULL)
		return editor->deleteCurrentLineText();
	if (session == NULL)
		return false;
	std::size_t start = session->document.lineStart(session->cursorOffset);
	std::size_t end = session->document.nextLine(session->cursorOffset);
	return backgroundReplaceRange(mr::editor::Range(start, end), std::string(), start);
}

static int currentEditorColumn(TMRFileEditor *editor) {
	uint lineStart;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL)
		return session != NULL ? static_cast<int>(session->document.column(session->cursorOffset) + 1) : 1;
	lineStart = editor->lineStartOffset(editor->cursorOffset());
	return editor->charColumn(lineStart, editor->cursorOffset()) + 1;
}

static int currentEditorLineNumber(TMRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL)
		return session != NULL ? static_cast<int>(session->document.lineIndex(session->cursorOffset) + 1) : 1;
	return editor->currentLineNumber();
}

static bool setEditorCursor(TMRFileEditor *editor, uint target) {
	TMREditWindow *win;
	if (editor == NULL)
		return backgroundSetCursor(target);
	if (target > editor->bufferLength())
		target = editor->bufferLength();
	editor->setCursorOffset(target, 0);
	win = currentEditWindow();
	if (win != NULL && win->isBlockMarking())
		win->refreshBlockVisual();
	else
		editor->revealCursor(True);
	return true;
}

static bool moveEditorLeft(TMRFileEditor *editor) {
	uint start;
	uint target;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return false;
		start = static_cast<uint>(session->document.lineStart(session->cursorOffset));
		if (session->cursorOffset > start)
			target = static_cast<uint>(session->cursorOffset - 1);
		else if (start > 0)
			target = static_cast<uint>(session->document.lineEnd(session->document.prevLine(start)));
		else
			target = 0;
		return setEditorCursor(NULL, target);
	}
	start = editor->lineStartOffset(editor->cursorOffset());
	if (editor->cursorOffset() > start)
		target = editor->prevCharOffset(editor->cursorOffset());
	else if (start > 0)
		target = editor->lineEndOffset(editor->prevLineOffset(start));
	else
		target = 0;
	return setEditorCursor(editor, target);
}

static bool moveEditorRight(TMRFileEditor *editor) {
	uint lineEnd;
	uint target;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return false;
		lineEnd = static_cast<uint>(session->document.lineEnd(session->cursorOffset));
		if (session->cursorOffset < lineEnd)
			target = static_cast<uint>(std::min(session->document.length(), session->cursorOffset + 1));
		else
			target = static_cast<uint>(session->cursorOffset);
		return setEditorCursor(NULL, target);
	}
	lineEnd = editor->lineEndOffset(editor->cursorOffset());
	if (editor->cursorOffset() < lineEnd)
		target = editor->nextCharOffset(editor->cursorOffset());
	else
		target = editor->cursorOffset();
	return setEditorCursor(editor, target);
}

static bool moveEditorUp(TMRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return false;
		return setEditorCursor(NULL, static_cast<uint>(backgroundLineMoveOffset(session->cursorOffset, -1)));
	}
	return setEditorCursor(editor, editor->lineMoveOffset(editor->cursorOffset(), -1));
}

static bool moveEditorDown(TMRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return false;
		return setEditorCursor(NULL, static_cast<uint>(backgroundLineMoveOffset(session->cursorOffset, 1)));
	}
	return setEditorCursor(editor, editor->lineMoveOffset(editor->cursorOffset(), 1));
}

static bool moveEditorHome(TMRFileEditor *editor) {
	uint start;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return false;
		start = static_cast<uint>(session->document.lineStart(session->cursorOffset));
		return setEditorCursor(NULL,
		                       static_cast<uint>(backgroundCharPtrOffset(start, currentEditorIndentLevel() - 1)));
	}
	start = editor->lineStartOffset(editor->cursorOffset());
	return setEditorCursor(editor, editor->charPtrOffset(start, currentEditorIndentLevel() - 1));
}

static bool moveEditorEol(TMRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL)
		return session != NULL ? setEditorCursor(NULL, static_cast<uint>(session->document.lineEnd(session->cursorOffset)))
		                       : false;
	return setEditorCursor(editor, editor->lineEndOffset(editor->cursorOffset()));
}

static bool moveEditorTof(TMRFileEditor *editor) {
	if (editor == NULL)
		return currentBackgroundEditSession() != NULL ? setEditorCursor(NULL, 0) : false;
	return setEditorCursor(editor, 0);
}

static bool moveEditorEof(TMRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL)
		return session != NULL ? setEditorCursor(NULL, static_cast<uint>(session->document.length())) : false;
	return setEditorCursor(editor, editor->bufferLength());
}

static bool moveEditorWordLeft(TMRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL)
		return session != NULL ? setEditorCursor(NULL, static_cast<uint>(backgroundPrevWordOffset(session->cursorOffset)))
		                       : false;
	return setEditorCursor(editor, editor->prevWordOffset(editor->cursorOffset()));
}

static bool moveEditorWordRight(TMRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL)
		return session != NULL ? setEditorCursor(NULL, static_cast<uint>(backgroundNextWordOffset(session->cursorOffset)))
		                       : false;
	return setEditorCursor(editor, editor->nextWordOffset(editor->cursorOffset()));
}

static bool moveEditorFirstWord(TMRFileEditor *editor) {
	uint pos;
	uint end;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return false;
		pos = static_cast<uint>(session->document.lineStart(session->cursorOffset));
		end = static_cast<uint>(session->document.lineEnd(session->cursorOffset));
		while (pos < end) {
			char c = session->document.charAt(pos);
			if (c != ' ' && c != '\t')
				break;
			++pos;
		}
		return setEditorCursor(NULL, pos);
	}
	pos = editor->lineStartOffset(editor->cursorOffset());
	end = editor->lineEndOffset(editor->cursorOffset());
	while (pos < end) {
		char c = editor->charAtOffset(pos);
		if (c != ' ' && c != '	')
			break;
		pos = editor->nextCharOffset(pos);
	}
	return setEditorCursor(editor, pos);
}

static bool gotoEditorLine(TMRFileEditor *editor, int lineNum) {
	uint pos = 0;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (lineNum < 1)
		return false;
	if (editor == NULL) {
		if (session == NULL)
			return false;
		pos = static_cast<uint>(session->document.lineStartByIndex(static_cast<std::size_t>(lineNum - 1)));
		return setEditorCursor(NULL, pos);
	}
	for (int i = 1; i < lineNum && pos < editor->bufferLength(); ++i)
		pos = editor->nextLineOffset(pos);
	return setEditorCursor(editor, pos);
}

static bool gotoEditorCol(TMRFileEditor *editor, int colNum) {
	uint start;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (colNum < 1)
		return false;
	if (editor == NULL) {
		if (session == NULL)
			return false;
		start = static_cast<uint>(session->document.lineStart(session->cursorOffset));
		return setEditorCursor(NULL, static_cast<uint>(backgroundCharPtrOffset(start, colNum - 1)));
	}
	start = editor->lineStartOffset(editor->cursorOffset());
	return setEditorCursor(editor, editor->charPtrOffset(start, colNum - 1));
}

static bool currentEditorAtEof(TMRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL)
		return session == NULL || session->cursorOffset >= session->document.length();
	return editor->cursorOffset() >= editor->bufferLength();
}

static bool currentEditorAtEol(TMRFileEditor *editor) {
	uint lineEnd;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL)
		return session == NULL || session->cursorOffset >= session->document.lineEnd(session->cursorOffset);
	lineEnd = editor->lineEndOffset(editor->cursorOffset());
	return editor->cursorOffset() >= lineEnd;
}

static int currentEditorRow(TMRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL)
		return session != NULL ? static_cast<int>(session->document.lineIndex(session->cursorOffset) + 1) : 1;
	return editor->currentViewRow();
}

static bool markEditorPosition(TMREditWindow *win, TMRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return false;
		session->markStack.push_back(static_cast<uint>(session->cursorOffset));
		return true;
	}
	if (win == NULL)
		return false;
	g_runtimeEnv.markStacks[win].push_back(editor->cursorOffset());
	return true;
}

static bool gotoEditorMark(TMREditWindow *win, TMRFileEditor *editor) {
	std::map<const void *, std::vector<uint>>::iterator it;
	uint pos;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL || session->markStack.empty())
			return false;
		pos = session->markStack.back();
		session->markStack.pop_back();
		return setEditorCursor(NULL, pos);
	}
	if (win == NULL)
		return false;
	it = g_runtimeEnv.markStacks.find(win);
	if (it == g_runtimeEnv.markStacks.end() || it->second.empty())
		return false;
	pos = it->second.back();
	it->second.pop_back();
	return setEditorCursor(editor, pos);
}

static bool popEditorMark(TMREditWindow *win) {
	std::map<const void *, std::vector<uint>>::iterator it;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (win == NULL) {
		if (session == NULL || session->markStack.empty())
			return false;
		session->markStack.pop_back();
		return true;
	}
	it = g_runtimeEnv.markStacks.find(win);
	if (it == g_runtimeEnv.markStacks.end() || it->second.empty())
		return false;
	it->second.pop_back();
	return true;
}

static bool moveEditorPageUp(TMRFileEditor *editor) {
	int pageLines;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return false;
		pageLines = std::max(1, session->pageLines);
		return setEditorCursor(NULL, static_cast<uint>(backgroundLineMoveOffset(session->cursorOffset, -pageLines)));
	}
	pageLines = std::max(1, editor->size.y - 1);
	return setEditorCursor(editor, editor->lineMoveOffset(editor->cursorOffset(), -pageLines));
}

static bool moveEditorPageDown(TMRFileEditor *editor) {
	int pageLines;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return false;
		pageLines = std::max(1, session->pageLines);
		return setEditorCursor(NULL, static_cast<uint>(backgroundLineMoveOffset(session->cursorOffset, pageLines)));
	}
	pageLines = std::max(1, editor->size.y - 1);
	return setEditorCursor(editor, editor->lineMoveOffset(editor->cursorOffset(), pageLines));
}

static bool moveEditorNextPageBreak(TMRFileEditor *editor) {
	std::string text;
	std::string::size_type pos;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return false;
		pos = static_cast<std::string::size_type>(session->cursorOffset);
		while (pos < session->document.length() && session->document.charAt(pos) != '\f')
			++pos;
		if (pos >= session->document.length())
			return false;
		return setEditorCursor(NULL, static_cast<uint>(session->document.nextLine(pos)));
	}
	text = snapshotEditorText(editor);
	pos = text.find('\f', std::min<std::size_t>(editor->cursorOffset(), text.size()));
	if (pos == std::string::npos)
		return false;
	return setEditorCursor(editor, editor->nextLineOffset(static_cast<uint>(pos)));
}

static bool moveEditorLastPageBreak(TMRFileEditor *editor) {
	std::string text;
	std::string::size_type pos;
	std::size_t start;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return false;
		start = std::min<std::size_t>(session->cursorOffset, session->document.length());
		if (start == 0)
			return false;
		pos = start - 1;
		for (;;) {
			if (session->document.charAt(pos) == '\f')
				return setEditorCursor(NULL, static_cast<uint>(session->document.nextLine(pos)));
			if (pos == 0)
				break;
			--pos;
		}
		return false;
	}
	text = snapshotEditorText(editor);
	start = std::min<std::size_t>(editor->cursorOffset(), text.size());
	if (start == 0)
		return false;
	pos = text.rfind('\f', start - 1);
	if (pos == std::string::npos)
		return false;
	return setEditorCursor(editor, editor->nextLineOffset(static_cast<uint>(pos)));
}

static bool replaceEditorBuffer(TMRFileEditor *editor, const std::string &text,
                                std::size_t cursorPos) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != NULL)
		return editor->replaceWholeBuffer(text, cursorPos);
	if (session == NULL)
		return false;
	return backgroundReplaceRange(mr::editor::Range(0, session->document.length()), text, cursorPos);
}
static SplitTextBuffer splitBufferLines(const std::string &text) {
	SplitTextBuffer out;
	std::string current;
	for (std::size_t i = 0; i < text.size(); ++i) {
		char c = text[i];
		if (c == '\r' || c == '\n') {
			out.lines.push_back(current);
			current.clear();
			out.trailingNewline = true;
			if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
				++i;
		} else {
			current.push_back(c);
			out.trailingNewline = false;
		}
	}
	if (!current.empty() || !out.trailingNewline || out.lines.empty())
		out.lines.push_back(current);
	return out;
}

static std::string joinBufferLines(const SplitTextBuffer &buffer) {
	std::string out;
	for (std::size_t i = 0; i < buffer.lines.size(); ++i) {
		if (i > 0)
			out.push_back('\n');
		out += buffer.lines[i];
	}
	if (buffer.trailingNewline && !buffer.lines.empty())
		out.push_back('\n');
	return out;
}

static std::size_t bufferOffsetForLine(const SplitTextBuffer &buffer, int lineIndex) {
	std::size_t offset = 0;
	int limit = std::max(0, std::min(lineIndex, static_cast<int>(buffer.lines.size())));
	for (int i = 0; i < limit; ++i) {
		offset += buffer.lines[static_cast<std::size_t>(i)].size();
		if (static_cast<std::size_t>(i + 1) < buffer.lines.size() || buffer.trailingNewline)
			++offset;
	}
	return offset;
}

static std::size_t bufferOffsetForLineColumn(const SplitTextBuffer &buffer, int lineIndex,
                                             int colIndex) {
	std::size_t offset;
	std::size_t col;
	if (buffer.lines.empty())
		return 0;
	lineIndex = std::max(0, std::min(lineIndex, static_cast<int>(buffer.lines.size()) - 1));
	col = static_cast<std::size_t>(std::max(0, colIndex));
	offset = bufferOffsetForLine(buffer, lineIndex);
	if (col > buffer.lines[static_cast<std::size_t>(lineIndex)].size())
		col = buffer.lines[static_cast<std::size_t>(lineIndex)].size();
	return offset + col;
}

static int lineIndexForPtr(TMRFileEditor *editor, uint ptr) {
	uint pos = 0;
	int line = 0;
	if (editor == NULL)
		return 0;
	if (ptr > editor->bufferLength())
		ptr = editor->bufferLength();
	while (pos < ptr && pos < editor->bufferLength()) {
		uint next = editor->nextLineOffset(pos);
		if (next <= pos || next > ptr)
			break;
		pos = next;
		++line;
	}
	return line;
}

static bool currentBlockInfo(TMREditWindow *win, TMRFileEditor *editor, int &mode, uint &anchor,
                             uint &end) {
	if (win == NULL || editor == NULL || !win->hasBlock())
		return false;
	mode = win->blockStatus();
	anchor = win->blockAnchorPtr();
	end = win->blockEffectiveEndPtr();
	if (anchor > editor->bufferLength())
		anchor = editor->bufferLength();
	if (end > editor->bufferLength())
		end = editor->bufferLength();
	return mode != 0;
}

struct EditWindowLookup {
	int targetIndex;
	int currentIndex;
	TMREditWindow *result;

	EditWindowLookup() : targetIndex(0), currentIndex(0), result(NULL) {
	}
};

static void collectEditWindowByIndex(TView *view, void *arg) {
	EditWindowLookup *lookup = static_cast<EditWindowLookup *>(arg);
	TMREditWindow *win = dynamic_cast<TMREditWindow *>(view);
	if (lookup == NULL || win == NULL || lookup->result != NULL)
		return;
	++lookup->currentIndex;
	if (lookup->currentIndex == lookup->targetIndex)
		lookup->result = win;
}

static TMREditWindow *editWindowByIndex(int index) {
	EditWindowLookup lookup;
	if (index <= 0 || TProgram::deskTop == NULL)
		return NULL;
	lookup.targetIndex = index;
	TProgram::deskTop->forEach(collectEditWindowByIndex, &lookup);
	return lookup.result;
}

static void countEditWindowProc(TView *view, void *arg) {
	int *count = static_cast<int *>(arg);
	if (count != NULL && dynamic_cast<TMREditWindow *>(view) != NULL)
		++(*count);
}

static int countEditWindows() {
	int count = 0;
	if (TProgram::deskTop == NULL)
		return 0;
	TProgram::deskTop->forEach(countEditWindowProc, &count);
	return count;
}

static void collectEditWindowsProc(TView *view, void *arg) {
	std::vector<TMREditWindow *> *windows = static_cast<std::vector<TMREditWindow *> *>(arg);
	TMREditWindow *win = dynamic_cast<TMREditWindow *>(view);
	if (windows != NULL && win != NULL)
		windows->push_back(win);
}

static std::vector<TMREditWindow *> allEditWindows() {
	std::vector<TMREditWindow *> windows;
	if (TProgram::deskTop != NULL)
		TProgram::deskTop->forEach(collectEditWindowsProc, &windows);
	return windows;
}

static void cleanupWindowLinkGroups() {
	std::vector<TMREditWindow *> windows = allEditWindows();
	std::set<const void *> live;
	std::map<int, int> counts;
	std::map<const void *, int>::iterator it;

	for (std::size_t i = 0; i < windows.size(); ++i)
		live.insert(windows[i]);

	for (it = g_runtimeEnv.windowLinkGroups.begin(); it != g_runtimeEnv.windowLinkGroups.end();) {
		if (live.find(it->first) == live.end())
			it = g_runtimeEnv.windowLinkGroups.erase(it);
		else {
			++counts[it->second];
			++it;
		}
	}

	for (it = g_runtimeEnv.windowLinkGroups.begin(); it != g_runtimeEnv.windowLinkGroups.end();) {
		if (counts[it->second] < 2)
			it = g_runtimeEnv.windowLinkGroups.erase(it);
		else
			++it;
	}
}

static int windowLinkGroupOf(TMREditWindow *win) {
	std::map<const void *, int>::const_iterator it;
	if (win == NULL)
		return 0;
	cleanupWindowLinkGroups();
	it = g_runtimeEnv.windowLinkGroups.find(win);
	if (it == g_runtimeEnv.windowLinkGroups.end())
		return 0;
	return it->second;
}

static bool isWindowLinked(TMREditWindow *win) {
	return windowLinkGroupOf(win) != 0;
}

static int currentLinkStatus() {
	return isWindowLinked(currentEditWindow()) ? 1 : 0;
}

static bool windowBufferIdentity(TMREditWindow *win, std::string &fileName, std::string &text,
                                 bool &emptyUntitled) {
	TMRFileEditor *editor;
	if (win == NULL)
		return false;
	editor = win->getEditor();
	if (editor == NULL)
		return false;
	fileName = win->currentFileName();
	text = snapshotEditorText(editor);
	emptyUntitled = fileName.empty() && text.empty();
	return true;
}

static bool copyWindowBufferState(TMREditWindow *src, TMREditWindow *dest) {
	TMRFileEditor *srcEditor;
	TMRFileEditor *destEditor;
	std::string text;
	std::size_t cursorPos;
	if (src == NULL || dest == NULL)
		return false;
	srcEditor = src->getEditor();
	destEditor = dest->getEditor();
	if (srcEditor == NULL || destEditor == NULL)
		return false;
	text = snapshotEditorText(srcEditor);
	cursorPos = std::min<std::size_t>(destEditor->cursorOffset(), text.size());
	if (!replaceEditorBuffer(destEditor, text, cursorPos))
		return false;
	dest->setCurrentFileName(src->currentFileName());
	dest->setFileChanged(src->isFileChanged());
	return true;
}

static bool assignLinkedWindows(TMREditWindow *a, TMREditWindow *b) {
	int groupA;
	int groupB;
	int targetGroup;
	std::map<const void *, int>::iterator it;

	if (a == NULL || b == NULL || a == b)
		return false;

	cleanupWindowLinkGroups();
	groupA = windowLinkGroupOf(a);
	groupB = windowLinkGroupOf(b);
	if (groupA != 0 && groupA == groupB)
		return true;

	targetGroup = groupA != 0 ? groupA : groupB;
	if (targetGroup == 0)
		targetGroup = g_runtimeEnv.nextWindowLinkGroupId++;

	if (groupA != 0 && groupB != 0 && groupA != groupB) {
		for (it = g_runtimeEnv.windowLinkGroups.begin(); it != g_runtimeEnv.windowLinkGroups.end(); ++it)
			if (it->second == groupB)
				it->second = targetGroup;
	}

	g_runtimeEnv.windowLinkGroups[a] = targetGroup;
	g_runtimeEnv.windowLinkGroups[b] = targetGroup;
	cleanupWindowLinkGroups();
	return true;
}


static TMREditWindow *selectLinkTargetWindow(TMREditWindow *current) {
	return mrShowWindowListDialog(mrwlSelectLinkTarget, current);
}

static bool prepareWindowLink(TMREditWindow *current, TMREditWindow *target,
                              TMREditWindow *&source, TMREditWindow *&dest) {
	std::string currentFile;
	std::string currentText;
	std::string targetFile;
	std::string targetText;
	bool currentEmptyUntitled = false;
	bool targetEmptyUntitled = false;

	if (current == NULL || target == NULL || current == target)
		return false;
	if (!windowBufferIdentity(current, currentFile, currentText, currentEmptyUntitled) ||
	    !windowBufferIdentity(target, targetFile, targetText, targetEmptyUntitled))
		return false;

	if (!currentEmptyUntitled && !targetEmptyUntitled) {
		if (currentFile != targetFile || currentText != targetText)
			return false;
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

static bool linkCurrentEditWindow() {
	TMREditWindow *current = currentEditWindow();
	TMREditWindow *target;
	TMREditWindow *source = NULL;
	TMREditWindow *dest = NULL;

	if (current == NULL)
		return false;
	target = selectLinkTargetWindow(current);
	if (target == NULL)
		return false;
	if (!prepareWindowLink(current, target, source, dest))
		return false;
	if (source != dest && !copyWindowBufferState(source, dest))
		return false;
	if (!assignLinkedWindows(current, target))
		return false;
	syncLinkedWindowsFrom(source);
	return true;
}

static bool unlinkCurrentEditWindow() {
	TMREditWindow *current = currentEditWindow();
	if (current == NULL)
		return false;
	cleanupWindowLinkGroups();
	g_runtimeEnv.windowLinkGroups.erase(current);
	cleanupWindowLinkGroups();
	return true;
}

static void syncLinkedWindowsFrom(TMREditWindow *source) {
	std::vector<TMREditWindow *> windows = allEditWindows();
	int group;
	if (source == NULL)
		return;
	group = windowLinkGroupOf(source);
	if (group == 0)
		return;
	for (std::size_t i = 0; i < windows.size(); ++i) {
		if (windows[i] == source)
			continue;
		if (windowLinkGroupOf(windows[i]) == group)
			copyWindowBufferState(source, windows[i]);
	}
}

static bool redrawCurrentEditWindow() {
	TMREditWindow *win = currentEditWindow();
	TMRFileEditor *editor = currentEditor();
	if (win == NULL)
		return false;
	if (editor != NULL)
		editor->refreshViewState();
	win->drawView();
	return true;
}

static bool redrawEntireScreen() {
	std::vector<TMREditWindow *> windows = allEditWindows();
	if (TProgram::deskTop == NULL)
		return false;
	TProgram::deskTop->drawView();
	for (std::size_t i = 0; i < windows.size(); ++i)
		windows[i]->drawView();
	return true;
}

static bool zoomCurrentEditWindow() {
	TMREditWindow *win = currentEditWindow();
	if (win == NULL)
		return false;
	message(win, evCommand, cmZoom, 0);
	return true;
}

struct CurrentEditWindowIndexLookup {
	TMREditWindow *current;
	int index;
	int result;
};

static void currentEditWindowIndexProc(TView *view, void *arg) {
	CurrentEditWindowIndexLookup *lookup = static_cast<CurrentEditWindowIndexLookup *>(arg);
	TMREditWindow *win = dynamic_cast<TMREditWindow *>(view);
	if (lookup == NULL || win == NULL || lookup->result != 0)
		return;
	++lookup->index;
	if (win == lookup->current)
		lookup->result = lookup->index;
}

static int currentEditWindowIndex() {
	CurrentEditWindowIndexLookup lookup;
	if (TProgram::deskTop == NULL)
		return 0;
	lookup.current = currentEditWindow();
	lookup.index = 0;
	lookup.result = 0;
	if (lookup.current == NULL)
		return 0;
	TProgram::deskTop->forEach(currentEditWindowIndexProc, &lookup);
	return lookup.result;
}

static bool currentWindowGeometry(int &x1, int &y1, int &x2, int &y2) {
	TMREditWindow *win = currentEditWindow();
	TRect bounds;
	if (win == NULL)
		return false;
	bounds = win->getBounds();
	x1 = bounds.a.x + 1;
	y1 = bounds.a.y + 1;
	x2 = bounds.b.x;
	y2 = bounds.b.y;
	return true;
}

static bool createEditWindow() {
	TRect r;
	int number;
	TMREditWindow *win;
	if (TProgram::deskTop == NULL)
		return false;
	r = TProgram::deskTop->getExtent();
	r.grow(-2, -1);
	number = countEditWindows() + 1;
	win = new TMREditWindow(r, "?No-File?", number);
	if (win == NULL)
		return false;
	TProgram::deskTop->insert(win);
	TProgram::deskTop->setCurrent(win, TView::normalSelect);
	return true;
}

static bool switchEditWindow(int index) {
	int count;
	TMREditWindow *win;
	if (TProgram::deskTop == NULL)
		return false;
	count = countEditWindows();
	if (count <= 0)
		return false;
	if (index <= 0)
		index = 1;
	if (index > count)
		index = ((index - 1) % count) + 1;
	win = editWindowByIndex(index);
	if (win == NULL)
		return false;
	TProgram::deskTop->setCurrent(win, TView::normalSelect);
	return true;
}

static bool sizeCurrentEditWindow(int x1, int y1, int x2, int y2) {
	TMREditWindow *win = currentEditWindow();
	TRect desk;
	TRect bounds;
	if (win == NULL || TProgram::deskTop == NULL)
		return false;
	if (x2 < x1 || y2 < y1)
		return false;
	desk = TProgram::deskTop->getExtent();
	x1 = std::max(1, x1);
	y1 = std::max(1, y1);
	x2 = std::min(desk.b.x, x2);
	y2 = std::min(desk.b.y, y2);
	if (x2 <= x1)
		x2 = std::min(desk.b.x, x1 + 3);
	if (y2 <= y1)
		y2 = std::min(desk.b.y, y1 + 3);
	bounds = TRect(x1 - 1, y1 - 1, x2, y2);
	win->changeBounds(bounds);
	win->drawView();
	return true;
}

static bool deleteCurrentEditWindow() {
	TMREditWindow *win = currentEditWindow();
	if (win == NULL)
		return false;
	win->close();
	return true;
}

static bool eraseCurrentEditWindow() {
	TMREditWindow *win = currentEditWindow();
	TMRFileEditor *editor = currentEditor();
	if (win == NULL || editor == NULL)
		return false;
	if (!replaceEditorBuffer(editor, std::string(), 0))
		return false;
	win->clearBlock();
	win->setCurrentFileName("");
	win->setFileChanged(false);
	return true;
}

static bool modifyCurrentEditWindow() {
	TMREditWindow *win = currentEditWindow();
	if (win == NULL)
		return false;
	message(win, evCommand, cmResize, 0);
	return true;
}

static bool copyBlockFromWindow(TMREditWindow *srcWin, TMRFileEditor *srcEditor,
                                TMREditWindow *destWin, TMRFileEditor *destEditor) {
	int mode;
	uint anchor;
	uint end;
	std::string sourceText;
	if (srcWin == NULL || srcEditor == NULL || destEditor == NULL)
		return false;
	if (srcWin == destWin)
		return copyCurrentBlock(srcWin, destEditor);
	if (!currentBlockInfo(srcWin, srcEditor, mode, anchor, end))
		return false;
	sourceText = snapshotEditorText(srcEditor);
	if (mode == TMREditWindow::bmStream) {
		std::size_t start = std::min<std::size_t>(anchor, end);
		std::size_t finish = std::max<std::size_t>(anchor, end);
		std::size_t dest = std::min<std::size_t>(destEditor->cursorOffset(), destEditor->bufferLength());
		std::string destText = snapshotEditorText(destEditor);
		std::string blockText = sourceText.substr(start, finish - start);
		destText.insert(dest, blockText);
		return replaceEditorBuffer(destEditor, destText, dest + blockText.size());
	}
	if (mode == TMREditWindow::bmLine) {
		SplitTextBuffer srcBuf = splitBufferLines(sourceText);
		SplitTextBuffer destBuf = splitBufferLines(snapshotEditorText(destEditor));
		int line1 = std::min(lineIndexForPtr(srcEditor, anchor), lineIndexForPtr(srcEditor, end));
		int line2 = std::max(lineIndexForPtr(srcEditor, anchor), lineIndexForPtr(srcEditor, end));
		int destLine = lineIndexForPtr(destEditor, destEditor->cursorOffset());
		std::vector<std::string> blockLines;
		if (srcBuf.lines.empty())
			return false;
		line1 = std::max(0, std::min(line1, static_cast<int>(srcBuf.lines.size()) - 1));
		line2 = std::max(line1, std::min(line2, static_cast<int>(srcBuf.lines.size()) - 1));
		destLine = std::max(0, std::min(destLine, static_cast<int>(destBuf.lines.size())));
		blockLines.assign(srcBuf.lines.begin() + line1, srcBuf.lines.begin() + line2 + 1);
		destBuf.lines.insert(destBuf.lines.begin() + destLine, blockLines.begin(),
		                     blockLines.end());
		return replaceEditorBuffer(destEditor, joinBufferLines(destBuf),
		                           bufferOffsetForLine(destBuf, destLine));
	}
	if (mode == TMREditWindow::bmColumn) {
		SplitTextBuffer srcBuf = splitBufferLines(sourceText);
		SplitTextBuffer destBuf = splitBufferLines(snapshotEditorText(destEditor));
		int row1 = std::min(lineIndexForPtr(srcEditor, anchor), lineIndexForPtr(srcEditor, end));
		int row2 = std::max(lineIndexForPtr(srcEditor, anchor), lineIndexForPtr(srcEditor, end));
		int col1 = std::min(srcWin->blockCol1(), srcWin->blockCol2());
		int col2 = std::max(srcWin->blockCol1(), srcWin->blockCol2());
		int width = std::max(1, col2 - col1);
		int destRow = lineIndexForPtr(destEditor, destEditor->cursorOffset());
		int destCol = std::max(0, currentEditorColumn(destEditor) - 1);
		std::vector<std::string> slices;
		if (srcBuf.lines.empty())
			return false;
		row1 = std::max(0, std::min(row1, static_cast<int>(srcBuf.lines.size()) - 1));
		row2 = std::max(row1, std::min(row2, static_cast<int>(srcBuf.lines.size()) - 1));
		for (int row = row1; row <= row2; ++row) {
			const std::string &line = srcBuf.lines[static_cast<std::size_t>(row)];
			std::string slice(static_cast<std::size_t>(width), ' ');
			std::size_t startCol = static_cast<std::size_t>(std::max(0, col1 - 1));
			if (startCol < line.size()) {
				std::size_t avail =
				    std::min<std::size_t>(static_cast<std::size_t>(width), line.size() - startCol);
				slice.replace(0, avail, line.substr(startCol, avail));
			}
			slices.push_back(slice);
		}
		while (static_cast<int>(destBuf.lines.size()) < destRow + static_cast<int>(slices.size()))
			destBuf.lines.push_back(std::string());
		for (std::size_t i = 0; i < slices.size(); ++i) {
			std::string &line = destBuf.lines[static_cast<std::size_t>(destRow) + i];
			if (line.size() < static_cast<std::size_t>(destCol))
				line.append(static_cast<std::size_t>(destCol) - line.size(), ' ');
			line.insert(static_cast<std::size_t>(destCol), slices[i]);
		}
		return replaceEditorBuffer(destEditor, joinBufferLines(destBuf),
		                           bufferOffsetForLineColumn(destBuf, destRow, destCol));
	}
	return false;
}

static bool moveBlockFromWindow(TMREditWindow *srcWin, TMRFileEditor *srcEditor,
                                TMREditWindow *destWin, TMRFileEditor *destEditor) {
	if (srcWin == NULL || srcEditor == NULL || destEditor == NULL)
		return false;
	if (srcWin == destWin)
		return moveCurrentBlock(srcWin, destEditor);
	if (!copyBlockFromWindow(srcWin, srcEditor, destWin, destEditor))
		return false;
	if (!deleteCurrentBlock(srcWin, srcEditor))
		return false;
	srcWin->clearBlock();
	return true;
}

static bool extractCurrentBlockText(TMREditWindow *win, TMRFileEditor *editor, std::string &out) {
	int mode;
	uint anchor;
	uint end;
	std::string text;
	out.clear();
	if (!currentBlockInfo(win, editor, mode, anchor, end))
		return false;
	text = snapshotEditorText(editor);
	if (mode == TMREditWindow::bmStream) {
		std::size_t start = std::min<std::size_t>(anchor, end);
		std::size_t finish = std::max<std::size_t>(anchor, end);
		out = text.substr(start, finish - start);
		return true;
	}
	if (mode == TMREditWindow::bmLine) {
		SplitTextBuffer buf = splitBufferLines(text);
		int line1 = std::min(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		int line2 = std::max(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		if (buf.lines.empty())
			return false;
		line1 = std::max(0, std::min(line1, static_cast<int>(buf.lines.size()) - 1));
		line2 = std::max(line1, std::min(line2, static_cast<int>(buf.lines.size()) - 1));
		for (int line = line1; line <= line2; ++line) {
			if (!out.empty())
				out.push_back('\n');
			out += buf.lines[static_cast<std::size_t>(line)];
		}
		return true;
	}
	if (mode == TMREditWindow::bmColumn) {
		SplitTextBuffer buf = splitBufferLines(text);
		int row1 = std::min(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		int row2 = std::max(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		int col1 = std::min(win->blockCol1(), win->blockCol2());
		int col2 = std::max(win->blockCol1(), win->blockCol2());
		int width = std::max(1, col2 - col1);
		if (buf.lines.empty())
			return false;
		row1 = std::max(0, std::min(row1, static_cast<int>(buf.lines.size()) - 1));
		row2 = std::max(row1, std::min(row2, static_cast<int>(buf.lines.size()) - 1));
		for (int row = row1; row <= row2; ++row) {
			const std::string &line = buf.lines[static_cast<std::size_t>(row)];
			std::string slice(static_cast<std::size_t>(width), ' ');
			std::size_t startCol = static_cast<std::size_t>(std::max(0, col1 - 1));
			if (startCol < line.size()) {
				std::size_t avail =
				    std::min<std::size_t>(static_cast<std::size_t>(width), line.size() - startCol);
				slice.replace(0, avail, line.substr(startCol, avail));
			}
			if (!out.empty())
				out.push_back('\n');
			out += slice;
		}
		return true;
	}
	return false;
}

static bool saveCurrentBlockToFile(TMREditWindow *win, TMRFileEditor *editor, const std::string &path) {
	std::ofstream outFile;
	std::string blockText;
	if (!extractCurrentBlockText(win, editor, blockText))
		return false;
	outFile.open(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!outFile.is_open())
		return false;
	outFile.write(blockText.data(), static_cast<std::streamsize>(blockText.size()));
	outFile.close();
	return outFile.good();
}

static bool copyCurrentBlock(TMREditWindow *win, TMRFileEditor *editor) {
	int mode;
	uint anchor;
	uint end;
	std::string text;
	if (!currentBlockInfo(win, editor, mode, anchor, end))
		return false;
	text = snapshotEditorText(editor);
	if (mode == TMREditWindow::bmStream) {
		std::size_t start = std::min<std::size_t>(anchor, end);
		std::size_t finish = std::max<std::size_t>(anchor, end);
		std::size_t dest = std::min<std::size_t>(editor->cursorOffset(), text.size());
		std::string blockText = text.substr(start, finish - start);
		text.insert(dest, blockText);
		if (!replaceEditorBuffer(editor, text, dest + blockText.size()))
			return false;
		win->clearBlock();
		return true;
	} else if (mode == TMREditWindow::bmLine) {
		SplitTextBuffer buf = splitBufferLines(text);
		int line1 = std::min(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		int line2 = std::max(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		int destLine = lineIndexForPtr(editor, editor->cursorOffset());
		std::vector<std::string> blockLines;
		if (buf.lines.empty())
			return false;
		line1 = std::max(0, std::min(line1, static_cast<int>(buf.lines.size()) - 1));
		line2 = std::max(line1, std::min(line2, static_cast<int>(buf.lines.size()) - 1));
		destLine = std::max(0, std::min(destLine, static_cast<int>(buf.lines.size())));
		blockLines.assign(buf.lines.begin() + line1, buf.lines.begin() + line2 + 1);
		buf.lines.insert(buf.lines.begin() + destLine, blockLines.begin(), blockLines.end());
		if (!replaceEditorBuffer(editor, joinBufferLines(buf), bufferOffsetForLine(buf, destLine)))
			return false;
		win->clearBlock();
		return true;
	} else if (mode == TMREditWindow::bmColumn) {
		SplitTextBuffer buf = splitBufferLines(text);
		int row1 = std::min(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		int row2 = std::max(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		int col1 = std::min(win->blockCol1(), win->blockCol2());
		int col2 = std::max(win->blockCol1(), win->blockCol2());
		int width = std::max(1, col2 - col1);
		int destRow = lineIndexForPtr(editor, editor->cursorOffset());
		int destCol = std::max(0, currentEditorColumn(editor) - 1);
		std::vector<std::string> slices;
		if (buf.lines.empty())
			return false;
		row1 = std::max(0, std::min(row1, static_cast<int>(buf.lines.size()) - 1));
		row2 = std::max(row1, std::min(row2, static_cast<int>(buf.lines.size()) - 1));
		for (int row = row1; row <= row2; ++row) {
			const std::string &line = buf.lines[static_cast<std::size_t>(row)];
			std::string slice(static_cast<std::size_t>(width), ' ');
			std::size_t startCol = static_cast<std::size_t>(std::max(0, col1 - 1));
			if (startCol < line.size()) {
				std::size_t avail =
				    std::min<std::size_t>(static_cast<std::size_t>(width), line.size() - startCol);
				slice.replace(0, avail, line.substr(startCol, avail));
			}
			slices.push_back(slice);
		}
		while (static_cast<int>(buf.lines.size()) < destRow + static_cast<int>(slices.size()))
			buf.lines.push_back(std::string());
		for (std::size_t i = 0; i < slices.size(); ++i) {
			std::string &line = buf.lines[static_cast<std::size_t>(destRow) + i];
			if (line.size() < static_cast<std::size_t>(destCol))
				line.append(static_cast<std::size_t>(destCol) - line.size(), ' ');
			line.insert(static_cast<std::size_t>(destCol), slices[i]);
		}
		if (!replaceEditorBuffer(editor, joinBufferLines(buf),
		                         bufferOffsetForLineColumn(buf, destRow, destCol)))
			return false;
		win->clearBlock();
		return true;
	}
	return false;
}

static bool moveCurrentBlock(TMREditWindow *win, TMRFileEditor *editor) {
	int mode;
	uint anchor;
	uint end;
	std::string text;
	if (!currentBlockInfo(win, editor, mode, anchor, end))
		return false;
	text = snapshotEditorText(editor);
	if (mode == TMREditWindow::bmStream) {
		std::size_t start = std::min<std::size_t>(anchor, end);
		std::size_t finish = std::max<std::size_t>(anchor, end);
		std::size_t dest = std::min<std::size_t>(editor->cursorOffset(), text.size());
		std::string blockText = text.substr(start, finish - start);
		if (dest >= start && dest <= finish)
			return true;
		text.erase(start, finish - start);
		if (dest > finish)
			dest -= (finish - start);
		text.insert(dest, blockText);
		if (!replaceEditorBuffer(editor, text, dest + blockText.size()))
			return false;
		win->clearBlock();
		return true;
	} else if (mode == TMREditWindow::bmLine) {
		SplitTextBuffer buf = splitBufferLines(text);
		int line1 = std::min(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		int line2 = std::max(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		int destLine = lineIndexForPtr(editor, editor->cursorOffset());
		int count;
		std::vector<std::string> blockLines;
		if (buf.lines.empty())
			return false;
		line1 = std::max(0, std::min(line1, static_cast<int>(buf.lines.size()) - 1));
		line2 = std::max(line1, std::min(line2, static_cast<int>(buf.lines.size()) - 1));
		count = line2 - line1 + 1;
		if (destLine >= line1 && destLine <= line2 + 1)
			return true;
		blockLines.assign(buf.lines.begin() + line1, buf.lines.begin() + line2 + 1);
		buf.lines.erase(buf.lines.begin() + line1, buf.lines.begin() + line2 + 1);
		if (buf.lines.empty())
			buf.lines.push_back(std::string());
		if (destLine > line2)
			destLine -= count;
		destLine = std::max(0, std::min(destLine, static_cast<int>(buf.lines.size())));
		buf.lines.insert(buf.lines.begin() + destLine, blockLines.begin(), blockLines.end());
		if (!replaceEditorBuffer(editor, joinBufferLines(buf), bufferOffsetForLine(buf, destLine)))
			return false;
		win->clearBlock();
		return true;
	} else if (mode == TMREditWindow::bmColumn) {
		SplitTextBuffer buf = splitBufferLines(text);
		int row1 = std::min(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		int row2 = std::max(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		int col1 = std::min(win->blockCol1(), win->blockCol2());
		int col2 = std::max(win->blockCol1(), win->blockCol2());
		int width = std::max(1, col2 - col1);
		int destRow = lineIndexForPtr(editor, editor->cursorOffset());
		int destCol = std::max(0, currentEditorColumn(editor) - 1);
		std::vector<std::string> slices;
		int height;
		if (buf.lines.empty())
			return false;
		row1 = std::max(0, std::min(row1, static_cast<int>(buf.lines.size()) - 1));
		row2 = std::max(row1, std::min(row2, static_cast<int>(buf.lines.size()) - 1));
		height = row2 - row1 + 1;
		if (destRow >= row1 && destRow <= row2 && destCol >= col1 - 1 &&
		    destCol <= col1 - 1 + width)
			return true;
		for (int row = row1; row <= row2; ++row) {
			std::string &line = buf.lines[static_cast<std::size_t>(row)];
			std::size_t startCol = static_cast<std::size_t>(std::max(0, col1 - 1));
			std::string slice(static_cast<std::size_t>(width), ' ');
			if (startCol < line.size()) {
				std::size_t avail =
				    std::min<std::size_t>(static_cast<std::size_t>(width), line.size() - startCol);
				slice.replace(0, avail, line.substr(startCol, avail));
				line.erase(startCol, avail);
			}
			slices.push_back(slice);
		}
		if (!(destRow + height - 1 < row1 || destRow > row2) && destCol > col1 - 1)
			destCol = std::max(0, destCol - width);
		while (static_cast<int>(buf.lines.size()) < destRow + static_cast<int>(slices.size()))
			buf.lines.push_back(std::string());
		for (std::size_t i = 0; i < slices.size(); ++i) {
			std::string &line = buf.lines[static_cast<std::size_t>(destRow) + i];
			if (line.size() < static_cast<std::size_t>(destCol))
				line.append(static_cast<std::size_t>(destCol) - line.size(), ' ');
			line.insert(static_cast<std::size_t>(destCol), slices[i]);
		}
		if (!replaceEditorBuffer(editor, joinBufferLines(buf),
		                         bufferOffsetForLineColumn(buf, destRow, destCol)))
			return false;
		win->clearBlock();
		return true;
	}
	return false;
}

static bool deleteCurrentBlock(TMREditWindow *win, TMRFileEditor *editor) {
	int mode;
	uint anchor;
	uint end;
	std::string text;
	if (!currentBlockInfo(win, editor, mode, anchor, end))
		return false;
	text = snapshotEditorText(editor);
	if (mode == TMREditWindow::bmStream) {
		std::size_t start = std::min<std::size_t>(anchor, end);
		std::size_t finish = std::max<std::size_t>(anchor, end);
		text.erase(start, finish - start);
		if (!replaceEditorBuffer(editor, text, start))
			return false;
		win->clearBlock();
		return true;
	} else if (mode == TMREditWindow::bmLine) {
		SplitTextBuffer buf = splitBufferLines(text);
		int line1 = std::min(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		int line2 = std::max(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		if (buf.lines.empty())
			return false;
		line1 = std::max(0, std::min(line1, static_cast<int>(buf.lines.size()) - 1));
		line2 = std::max(line1, std::min(line2, static_cast<int>(buf.lines.size()) - 1));
		buf.lines.erase(buf.lines.begin() + line1, buf.lines.begin() + line2 + 1);
		if (buf.lines.empty()) {
			buf.lines.push_back(std::string());
			buf.trailingNewline = false;
		}
		if (!replaceEditorBuffer(
		        editor, joinBufferLines(buf),
		        bufferOffsetForLine(buf, std::min(line1, static_cast<int>(buf.lines.size()) - 1))))
			return false;
		win->clearBlock();
		return true;
	} else if (mode == TMREditWindow::bmColumn) {
		SplitTextBuffer buf = splitBufferLines(text);
		int row1 = std::min(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		int row2 = std::max(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
		int col1 = std::min(win->blockCol1(), win->blockCol2());
		int col2 = std::max(win->blockCol1(), win->blockCol2());
		int width = std::max(1, col2 - col1);
		if (buf.lines.empty())
			return false;
		row1 = std::max(0, std::min(row1, static_cast<int>(buf.lines.size()) - 1));
		row2 = std::max(row1, std::min(row2, static_cast<int>(buf.lines.size()) - 1));
		for (int row = row1; row <= row2; ++row) {
			std::string &line = buf.lines[static_cast<std::size_t>(row)];
			std::size_t startCol = static_cast<std::size_t>(std::max(0, col1 - 1));
			if (startCol < line.size())
				line.erase(startCol, std::min<std::size_t>(static_cast<std::size_t>(width),
				                                           line.size() - startCol));
		}
		if (!replaceEditorBuffer(editor, joinBufferLines(buf),
		                         bufferOffsetForLineColumn(buf, row1, std::max(0, col1 - 1))))
			return false;
		win->clearBlock();
		return true;
	}
	return false;
}

static bool moveEditorTabRight(TMRFileEditor *editor) {
	int col;
	int targetCol;
	uint lineStart;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL && session == NULL)
		return false;
	col = currentEditorColumn(editor);
	targetCol = nextTabStopColumn(col);
	if (currentEditorInsertMode()) {
		if (g_runtimeEnv.tabExpand)
			return insertEditorText(editor, std::string(1, '	'));
		return insertEditorText(editor,
		                        std::string(static_cast<std::size_t>(targetCol - col), ' '));
	}
	if (editor == NULL) {
		lineStart = static_cast<uint>(session->document.lineStart(session->cursorOffset));
		return setEditorCursor(NULL, static_cast<uint>(backgroundCharPtrOffset(lineStart, targetCol - 1)));
	}
	lineStart = editor->lineStartOffset(editor->cursorOffset());
	return setEditorCursor(editor, editor->charPtrOffset(lineStart, targetCol - 1));
}

static bool moveEditorTabLeft(TMRFileEditor *editor) {
	uint lineStart;
	int targetCol;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == NULL) {
		if (session == NULL)
			return false;
		lineStart = static_cast<uint>(session->document.lineStart(session->cursorOffset));
		targetCol = prevTabStopColumn(currentEditorColumn(NULL));
		return setEditorCursor(NULL, static_cast<uint>(backgroundCharPtrOffset(lineStart, targetCol - 1)));
	}
	lineStart = editor->lineStartOffset(editor->cursorOffset());
	targetCol = prevTabStopColumn(currentEditorColumn(editor));
	return setEditorCursor(editor, editor->charPtrOffset(lineStart, targetCol - 1));
}

static bool indentEditor(TMRFileEditor *editor) {
	if (!moveEditorTabRight(editor))
		return false;
	return setCurrentEditorIndentLevel(currentEditorColumn(editor));
}

static bool undentEditor(TMRFileEditor *editor) {
	if (!moveEditorTabLeft(editor))
		return false;
	return setCurrentEditorIndentLevel(currentEditorColumn(editor));
}

static bool carriageReturnEditor(TMRFileEditor *editor) {
	int indentLevel;
	std::string fill;
	indentLevel = currentEditorIndentLevel();
	fill = makeIndentFill(indentLevel, g_runtimeEnv.tabExpand);
	if (editor != NULL)
		return editor->newLineWithIndent(fill);
	return insertEditorText(NULL, std::string("\n") + fill);
}

static std::string formatCurrentDate() {
	char buf[32];
	std::time_t now = std::time(NULL);
	std::tm *tmv = std::localtime(&now);
	if (tmv == NULL)
		return std::string();
	std::strftime(buf, sizeof(buf), "%m/%d/%y", tmv);
	return std::string(buf);
}

static std::string formatCurrentTime() {
	char buf[32];
	std::time_t now = std::time(NULL);
	std::tm *tmv = std::localtime(&now);
	if (tmv == NULL)
		return std::string();
	std::strftime(buf, sizeof(buf), "%I:%M:%S%p", tmv);
	std::string out(buf);
	for (std::string::size_type i = 0; i < out.size(); ++i)
		out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
	return out;
}

static Value loadSpecialVariable(const std::string &name, bool &handled) {
	std::string key = upperKey(name);
	handled = true;
	if (key == "RETURN_INT")
		return makeInt(g_runtimeEnv.returnInt);
	if (key == "RETURN_STR")
		return makeString(g_runtimeEnv.returnStr);
	if (key == "ERROR_LEVEL")
		return makeInt(g_runtimeEnv.errorLevel);
	if (key == "IGNORE_CASE")
		return makeInt(g_runtimeEnv.ignoreCase ? 1 : 0);
	if (key == "TAB_EXPAND")
		return makeInt(g_runtimeEnv.tabExpand ? 1 : 0);
	if (key == "INSERT_MODE")
		return makeInt(currentEditorInsertMode() ? 1 : 0);
	if (key == "INDENT_LEVEL")
		return makeInt(currentEditorIndentLevel());
	if (key == "MPARM_STR")
		return makeString(g_runtimeEnv.parameterString);
	if (key == "DATE")
		return makeString(formatCurrentDate());
	if (key == "TIME")
		return makeString(formatCurrentTime());
	if (key == "COMSPEC")
		return makeString(g_runtimeEnv.shellPath);
	if (key == "MR_PATH")
		return makeString(g_runtimeEnv.executableDir);
	if (key == "OS_VERSION")
		return makeString(g_runtimeEnv.shellVersion);
	if (key == "PARAM_COUNT")
		return makeInt(static_cast<int>(g_runtimeEnv.processArgs.size()));
	if (key == "CPU")
		return makeInt(detectCpuCode());
	if (key == "C_COL")
		return makeInt(currentEditorColumn(currentEditor()));
	if (key == "C_LINE")
		return makeInt(currentEditorLineNumber(currentEditor()));
	if (key == "C_ROW")
		return makeInt(currentEditorRow(currentEditor()));
	if (key == "AT_EOF")
		return makeInt(currentEditorAtEof(currentEditor()) ? 1 : 0);
	if (key == "AT_EOL")
		return makeInt(currentEditorAtEol(currentEditor()) ? 1 : 0);
	if (key == "CUR_WINDOW")
		return makeInt(currentEditWindowIndex());
	if (key == "LINK_STAT")
		return makeInt(currentLinkStatus());
	if (key == "WINDOW_COUNT")
		return makeInt(countEditWindows());
	if (key == "WIN_X1" || key == "WIN_Y1" || key == "WIN_X2" || key == "WIN_Y2") {
		int x1;
		int y1;
		int x2;
		int y2;
		if (!currentWindowGeometry(x1, y1, x2, y2))
			return makeInt(0);
		if (key == "WIN_X1")
			return makeInt(x1);
		if (key == "WIN_Y1")
			return makeInt(y1);
		if (key == "WIN_X2")
			return makeInt(x2);
		return makeInt(y2);
	}
	if (key == "BLOCK_STAT") {
		TMREditWindow *win = currentEditWindow();
		return makeInt(win != NULL ? win->blockStatus() : 0);
	}
	if (key == "BLOCK_LINE1") {
		TMREditWindow *win = currentEditWindow();
		return makeInt(win != NULL ? win->blockLine1() : 0);
	}
	if (key == "BLOCK_LINE2") {
		TMREditWindow *win = currentEditWindow();
		return makeInt(win != NULL ? win->blockLine2() : 0);
	}
	if (key == "BLOCK_COL1") {
		TMREditWindow *win = currentEditWindow();
		return makeInt(win != NULL ? win->blockCol1() : 0);
	}
	if (key == "BLOCK_COL2") {
		TMREditWindow *win = currentEditWindow();
		return makeInt(win != NULL ? win->blockCol2() : 0);
	}
	if (key == "MARKING") {
		TMREditWindow *win = currentEditWindow();
		return makeInt(win != NULL && win->isBlockMarking() ? 1 : 0);
	}
	if (key == "LAST_FILE_NAME")
		return makeString(g_runtimeEnv.lastFileName);
	if (key == "GET_LINE")
		return makeString(currentEditorLineText(currentEditor()));
	if (key == "CUR_CHAR")
		return currentEditorCharValue();
	if (key == "FIRST_SAVE" || key == "EOF_IN_MEM" || key == "BUFFER_ID" || key == "TMP_FILE" ||
	    key == "TMP_FILE_NAME" || key == "FILE_CHANGED" || key == "FILE_NAME")
		return loadCurrentFileState(key);
	if (key == "FIRST_RUN") {
		if (!g_runtimeEnv.macroStack.empty())
			return makeInt(g_runtimeEnv.macroStack.back().firstRun ? 1 : 0);
		return makeInt(0);
	}
	if (key == "FIRST_MACRO") {
		g_runtimeEnv.macroEnumIndex = 0;
		while (g_runtimeEnv.macroEnumIndex < g_runtimeEnv.macroOrder.size()) {
			const std::string &macroKey = g_runtimeEnv.macroOrder[g_runtimeEnv.macroEnumIndex++];
			std::map<std::string, MacroRef>::const_iterator it =
			    g_runtimeEnv.loadedMacros.find(macroKey);
			if (it != g_runtimeEnv.loadedMacros.end())
				return makeString(it->second.displayName);
		}
		return makeString("");
	}
	if (key == "NEXT_MACRO") {
		while (g_runtimeEnv.macroEnumIndex < g_runtimeEnv.macroOrder.size()) {
			const std::string &macroKey = g_runtimeEnv.macroOrder[g_runtimeEnv.macroEnumIndex++];
			std::map<std::string, MacroRef>::const_iterator it =
			    g_runtimeEnv.loadedMacros.find(macroKey);
			if (it != g_runtimeEnv.loadedMacros.end())
				return makeString(it->second.displayName);
		}
		return makeString("");
	}
	handled = false;
	return makeInt(0);
}

static bool storeSpecialVariable(const std::string &name, const Value &value) {
	std::string key = upperKey(name);
	if (key == "RETURN_INT") {
		g_runtimeEnv.returnInt = valueAsInt(value);
		return true;
	}
	if (key == "RETURN_STR") {
		g_runtimeEnv.returnStr = valueAsString(value);
		enforceStringLength(g_runtimeEnv.returnStr);
		return true;
	}
	if (key == "ERROR_LEVEL") {
		g_runtimeEnv.errorLevel = valueAsInt(value);
		return true;
	}
	if (key == "IGNORE_CASE") {
		g_runtimeEnv.ignoreCase = valueAsInt(value) != 0;
		return true;
	}
	if (key == "TAB_EXPAND") {
		g_runtimeEnv.tabExpand = valueAsInt(value) != 0;
		return true;
	}
	if (key == "INSERT_MODE")
		return setCurrentEditorInsertMode(valueAsInt(value) != 0);
	if (key == "INDENT_LEVEL")
		return setCurrentEditorIndentLevel(valueAsInt(value));
	if (key == "MPARM_STR") {
		g_runtimeEnv.parameterString = valueAsString(value);
		enforceStringLength(g_runtimeEnv.parameterString);
		return true;
	}
	if (key == "FILE_CHANGED") {
		TMREditWindow *win = currentEditWindow();
		BackgroundEditSession *session = currentBackgroundEditSession();
		if (win != NULL)
			win->setFileChanged(valueAsInt(value) != 0);
		else if (session != NULL)
			session->fileChanged = valueAsInt(value) != 0;
		return true;
	}
	if (key == "FILE_NAME") {
		TMREditWindow *win = currentEditWindow();
		BackgroundEditSession *session = currentBackgroundEditSession();
		if (win != NULL)
			win->setCurrentFileName(valueAsString(value).c_str());
		else if (session != NULL)
			session->fileName = valueAsString(value);
		return true;
	}
	if (key == "FIRST_RUN" || key == "FIRST_MACRO" || key == "NEXT_MACRO" ||
	    key == "LAST_FILE_NAME" || key == "GET_LINE" || key == "CUR_CHAR" || key == "C_COL" ||
	    key == "C_LINE" || key == "C_ROW" || key == "AT_EOF" || key == "AT_EOL" ||
	    key == "CUR_WINDOW" || key == "LINK_STAT" || key == "WIN_X1" || key == "WIN_Y1" || key == "WIN_X2" ||
	    key == "WIN_Y2" || key == "WINDOW_COUNT" || key == "BLOCK_STAT" ||
	    key == "BLOCK_LINE1" || key == "BLOCK_LINE2" || key == "BLOCK_COL1" ||
	    key == "BLOCK_COL2" || key == "MARKING" || key == "FIRST_SAVE" ||
	    key == "EOF_IN_MEM" || key == "BUFFER_ID" || key == "TMP_FILE" || key == "TMP_FILE_NAME" ||
	    key == "COMSPEC" || key == "MR_PATH" || key == "OS_VERSION" || key == "PARAM_COUNT" ||
	    key == "CPU")
		throw std::runtime_error("Attempt to assign to read-only system variable.");
	return false;
}

static std::string parseNamedValue(const std::string &needle, const std::string &source) {
	std::size_t pos = source.find(needle);
	if (pos == std::string::npos)
		return std::string();
	pos += needle.size();
	std::size_t end = source.find_first_of(" \t\r\n", pos);
	if (end == std::string::npos)
		end = source.size();
	return source.substr(pos, end - pos);
}

static void setGlobalValue(const std::string &name, int type, const Value &value) {
	std::string key = upperKey(name);
	if (g_runtimeEnv.globals.find(key) == g_runtimeEnv.globals.end())
		g_runtimeEnv.globalOrder.push_back(key);
	GlobalEntry entry;
	entry.type = type;
	entry.value = value;
	g_runtimeEnv.globals[key] = entry;
}

static bool fileExists(const std::string &path) {
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	return in.good();
}

static std::string trimAscii(const std::string &value) {
	std::size_t start = 0;
	std::size_t end = value.size();
	while (start < end && std::isspace(static_cast<unsigned char>(value[start])))
		++start;
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
		--end;
	return value.substr(start, end - start);
}

static std::string stripMrmacExtension(const std::string &value) {
	std::string upper = upperKey(value);
	if (upper.size() >= 6 && upper.substr(upper.size() - 6) == ".MRMAC")
		return value.substr(0, value.size() - 6);
	return value;
}

static std::string makeFileKey(const std::string &value) {
	return upperKey(stripMrmacExtension(trimAscii(value)));
}

static bool readTextFile(const std::string &path, std::string &outContent) {
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream buffer;
	if (!in)
		return false;
	buffer << in.rdbuf();
	if (!in.good() && !in.eof())
		return false;
	outContent = buffer.str();
	return true;
}

static std::string resolveMacroFilePath(const std::string &spec) {
	std::string trimmed = trimAscii(spec);
	if (trimmed.empty())
		return std::string();
	if (fileExists(trimmed))
		return trimmed;
	if (upperKey(trimmed).size() < 6 ||
	    upperKey(trimmed).substr(upperKey(trimmed).size() - 6) != ".MRMAC") {
		std::string withExt = trimmed + ".mrmac";
		if (fileExists(withExt))
			return withExt;
	}
	return trimmed;
}

static bool macroIsRunning(const std::string &macroKey) {
	for (std::size_t i = 0; i < g_runtimeEnv.macroStack.size(); ++i)
		if (upperKey(g_runtimeEnv.macroStack[i].macroName) == macroKey)
			return true;
	return false;
}

static bool removeMacroFromRegistryByKey(const std::string &macroKey) {
	std::map<std::string, MacroRef>::iterator it = g_runtimeEnv.loadedMacros.find(macroKey);
	if (it == g_runtimeEnv.loadedMacros.end())
		return false;

	const std::string fileKey = it->second.fileKey;
	g_runtimeEnv.loadedMacros.erase(it);
	g_runtimeEnv.macroOrder.erase(
	    std::remove(g_runtimeEnv.macroOrder.begin(), g_runtimeEnv.macroOrder.end(), macroKey),
	    g_runtimeEnv.macroOrder.end());

	std::map<std::string, LoadedMacroFile>::iterator fit = g_runtimeEnv.loadedFiles.find(fileKey);
	if (fit != g_runtimeEnv.loadedFiles.end()) {
		fit->second.macroNames.erase(
		    std::remove(fit->second.macroNames.begin(), fit->second.macroNames.end(), macroKey),
		    fit->second.macroNames.end());
		if (fit->second.macroNames.empty())
			g_runtimeEnv.loadedFiles.erase(fit);
	}
	return true;
}

static bool parseRunMacroSpec(const std::string &spec, std::string &filePart,
                              std::string &macroPart, std::string &paramPart) {
	std::string trimmed = trimAscii(spec);
	std::size_t spacePos;
	std::string head;
	std::size_t caretPos;

	filePart.clear();
	macroPart.clear();
	paramPart.clear();

	if (trimmed.empty())
		return false;

	spacePos = trimmed.find_first_of(" \t\r\n");
	if (spacePos == std::string::npos)
		head = trimmed;
	else {
		head = trimmed.substr(0, spacePos);
		paramPart = trimAscii(trimmed.substr(spacePos + 1));
	}

	caretPos = head.find('^');
	if (caretPos == std::string::npos)
		macroPart = head;
	else {
		filePart = head.substr(0, caretPos);
		macroPart = head.substr(caretPos + 1);
	}
	return !macroPart.empty();
}

static bool fileContainsOnlyTransientMacros(const LoadedMacroFile &file) {
	if (file.macroNames.empty())
		return false;
	for (std::size_t i = 0; i < file.macroNames.size(); ++i) {
		std::map<std::string, MacroRef>::const_iterator mit =
		    g_runtimeEnv.loadedMacros.find(file.macroNames[i]);
		if (mit == g_runtimeEnv.loadedMacros.end() || !mit->second.transientAttr)
			return false;
	}
	return true;
}

static bool refreshLoadedFileBytecode(const std::string &fileKey) {
	std::map<std::string, LoadedMacroFile>::iterator fit = g_runtimeEnv.loadedFiles.find(fileKey);
	std::string source;
	unsigned char *compiled = NULL;
	size_t compiledSize = 0;
	int macroCount;

	if (fit == g_runtimeEnv.loadedFiles.end())
		return false;
	if (fit->second.resolvedPath.empty() || !readTextFile(fit->second.resolvedPath, source)) {
		g_runtimeEnv.errorLevel = 5001;
		return false;
	}

	compiled = compile_macro_code(source.c_str(), &compiledSize);
	if (compiled == NULL) {
		g_runtimeEnv.errorLevel = 5005;
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
		std::string displayName = macroNameText != NULL ? macroNameText : std::string();
		std::string macroKey = upperKey(displayName);
		std::map<std::string, MacroRef>::iterator mit = g_runtimeEnv.loadedMacros.find(macroKey);

		if (displayName.empty() || entry < 0)
			continue;
		if (mit == g_runtimeEnv.loadedMacros.end() || mit->second.fileKey != fileKey)
			continue;

		mit->second.displayName = displayName;
		mit->second.entryOffset = static_cast<std::size_t>(entry);
		mit->second.transientAttr = (flags & MACRO_ATTR_TRANS) != 0;
		mit->second.dumpAttr = (flags & MACRO_ATTR_DUMP) != 0;
		mit->second.permAttr = (flags & MACRO_ATTR_PERM) != 0;
	}

	g_runtimeEnv.errorLevel = 0;
	logMacroProfileLine("Refreshed macro file", fit->second);
	return true;
}

static bool ensureLoadedFileResident(const std::string &fileKey) {
	std::map<std::string, LoadedMacroFile>::iterator fit = g_runtimeEnv.loadedFiles.find(fileKey);
	if (fit == g_runtimeEnv.loadedFiles.end())
		return false;
	if (!fit->second.bytecode.empty())
		return true;
	return refreshLoadedFileBytecode(fileKey);
}

static bool evictTransientFileImage(const std::string &fileKey) {
	std::map<std::string, LoadedMacroFile>::iterator fit = g_runtimeEnv.loadedFiles.find(fileKey);
	if (fit == g_runtimeEnv.loadedFiles.end())
		return false;
	if (!fileContainsOnlyTransientMacros(fit->second))
		return false;
	for (std::size_t i = 0; i < fit->second.macroNames.size(); ++i)
		if (macroIsRunning(fit->second.macroNames[i]))
			return false;
	fit->second.bytecode.clear();
	fit->second.bytecode.shrink_to_fit();
	return true;
}

static bool currentBackgroundChildMacroAllowed(const LoadedMacroFile &file) noexcept {
	if (currentBackgroundEditSession() != NULL)
		return mrvmCanRunInBackground(file.profile) || mrvmCanRunStagedInBackground(file.profile);
	return false;
}

static bool loadMacroFileIntoRegistry(const std::string &spec, std::string *loadedFileKey) {
	std::string resolvedPath = resolveMacroFilePath(spec);
	std::string fileKey = makeFileKey(spec);
	std::string source;
	LoadedMacroFile newFile;
	unsigned char *compiled = NULL;
	size_t compiledSize = 0;
	int macroCount;

	if (loadedFileKey != NULL)
		loadedFileKey->clear();

	if (resolvedPath.empty() || !readTextFile(resolvedPath, source)) {
		g_runtimeEnv.errorLevel = 5001;
		return false;
	}

	std::map<std::string, LoadedMacroFile>::iterator existingFile =
	    g_runtimeEnv.loadedFiles.find(fileKey);
	if (existingFile != g_runtimeEnv.loadedFiles.end()) {
		for (std::size_t i = 0; i < existingFile->second.macroNames.size(); ++i)
			if (macroIsRunning(existingFile->second.macroNames[i])) {
				g_runtimeEnv.errorLevel = 5006;
				return false;
			}
	}

	compiled = compile_macro_code(source.c_str(), &compiledSize);
	if (compiled == NULL) {
		g_runtimeEnv.errorLevel = 5005;
		return false;
	}

	macroCount = get_compiled_macro_count();
	for (int i = 0; i < macroCount; ++i) {
		const char *macroNameText = get_compiled_macro_name(i);
		std::string displayName = macroNameText != NULL ? macroNameText : std::string();
		std::string macroKey = upperKey(displayName);
		std::map<std::string, MacroRef>::iterator mit;

		if (displayName.empty())
			continue;

		mit = g_runtimeEnv.loadedMacros.find(macroKey);
		if (mit != g_runtimeEnv.loadedMacros.end()) {
			if (macroIsRunning(macroKey) || mit->second.permAttr) {
				std::free(compiled);
				g_runtimeEnv.errorLevel = 5006;
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
		for (std::size_t i = 0; i < oldNames.size(); ++i)
			removeMacroFromRegistryByKey(oldNames[i]);
	}

	for (int i = 0; i < macroCount; ++i) {
		const char *macroNameText = get_compiled_macro_name(i);
		int entry = get_compiled_macro_entry(i);
		int flags = get_compiled_macro_flags(i);
		std::string displayName = macroNameText != NULL ? macroNameText : std::string();
		std::string macroKey = upperKey(displayName);
		MacroRef ref;

		if (displayName.empty() || entry < 0)
			continue;
		removeMacroFromRegistryByKey(macroKey);

		ref.fileKey = fileKey;
		ref.displayName = displayName;
		ref.entryOffset = static_cast<std::size_t>(entry);
		ref.firstRunPending = true;
		ref.transientAttr = (flags & MACRO_ATTR_TRANS) != 0;
		ref.dumpAttr = (flags & MACRO_ATTR_DUMP) != 0;
		ref.permAttr = (flags & MACRO_ATTR_PERM) != 0;
		g_runtimeEnv.loadedMacros[macroKey] = ref;
		g_runtimeEnv.macroOrder.push_back(macroKey);
		newFile.macroNames.push_back(macroKey);
	}

	g_runtimeEnv.loadedFiles[fileKey] = newFile;
	g_runtimeEnv.errorLevel = 0;
	logMacroProfileLine("Loaded macro file", newFile);
	if (loadedFileKey != NULL)
		*loadedFileKey = fileKey;
	return true;
}

static bool unloadMacroFromRegistry(const std::string &macroName) {
	std::string macroKey = upperKey(trimAscii(macroName));
	if (macroKey.empty())
		return false;
	if (macroIsRunning(macroKey)) {
		g_runtimeEnv.errorLevel = 5006;
		return false;
	}
	if (!removeMacroFromRegistryByKey(macroKey))
		return false;
	g_runtimeEnv.errorLevel = 0;
	return true;
}

static Value applyIntrinsic(const std::string &name, const std::vector<Value> &args) {
	if (name == "STR") {
		if (args.size() != 1 || args[0].type != TYPE_INT)
			throw std::runtime_error("STR expects one integer argument.");
		return makeString(valueAsString(args[0]));
	}
	if (name == "CHAR") {
		if (args.size() != 1 || args[0].type != TYPE_INT)
			throw std::runtime_error("CHAR expects one integer argument.");
		return makeChar(static_cast<unsigned char>(args[0].i & 0xFF));
	}
	if (name == "ASCII") {
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("ASCII expects one string argument.");
		std::string s = valueAsString(args[0]);
		return makeInt(s.empty() ? 0 : static_cast<unsigned char>(s[0]));
	}
	if (name == "CAPS") {
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("CAPS expects one string argument.");
		return makeString(uppercaseAscii(valueAsString(args[0])));
	}
	if (name == "COPY") {
		std::string s;
		int pos;
		int count;
		std::size_t start;
		if (args.size() != 3 || !isStringLike(args[0]) || args[1].type != TYPE_INT ||
		    args[2].type != TYPE_INT)
			throw std::runtime_error("COPY expects (string, int, int).");
		s = valueAsString(args[0]);
		pos = checkedStringIndex(args[1].i);
		count = args[2].i;
		if (count < 0)
			throw std::runtime_error("Invalid string index on string copy operation.");
		if (static_cast<std::size_t>(pos) > s.size())
			return makeString("");
		start = static_cast<std::size_t>(pos - 1);
		return makeString(s.substr(start, static_cast<std::size_t>(count)));
	}
	if (name == "LENGTH") {
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("LENGTH expects one string argument.");
		return makeInt(static_cast<int>(valueAsString(args[0]).size()));
	}
	if (name == "POS") {
		std::string needle;
		std::string haystack;
		std::size_t pos;
		if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1]))
			throw std::runtime_error("POS expects (substring, string).");
		needle = valueAsString(args[0]);
		haystack = valueAsString(args[1]);
		if (needle.empty())
			return makeInt(1);
		pos = haystack.find(needle);
		return makeInt(pos == std::string::npos ? 0 : static_cast<int>(pos + 1));
	}
	if (name == "XPOS") {
		std::string needle;
		std::string haystack;
		int startPos;
		std::size_t pos;
		if (args.size() != 3 || !isStringLike(args[0]) || !isStringLike(args[1]) ||
		    args[2].type != TYPE_INT)
			throw std::runtime_error("XPOS expects (substring, string, int).");
		needle = valueAsString(args[0]);
		haystack = valueAsString(args[1]);
		startPos = checkedStringIndex(args[2].i);
		if (needle.empty())
			return makeInt(startPos <= static_cast<int>(haystack.size()) + 1 ? startPos : 0);
		if (static_cast<std::size_t>(startPos) > haystack.size())
			return makeInt(0);
		pos = haystack.find(needle, static_cast<std::size_t>(startPos - 1));
		return makeInt(pos == std::string::npos ? 0 : static_cast<int>(pos + 1));
	}
	if (name == "STR_DEL") {
		std::string s;
		int pos;
		int count;
		std::size_t start;
		if (args.size() != 3 || !isStringLike(args[0]) || args[1].type != TYPE_INT ||
		    args[2].type != TYPE_INT)
			throw std::runtime_error("STR_DEL expects (string, int, int).");
		s = valueAsString(args[0]);
		pos = checkedStringIndex(args[1].i);
		count = args[2].i;
		if (count < 0)
			throw std::runtime_error("Invalid string index on string copy operation.");
		if (static_cast<std::size_t>(pos) > s.size())
			return makeString(s);
		start = static_cast<std::size_t>(pos - 1);
		s.erase(start, static_cast<std::size_t>(count));
		return makeString(s);
	}
	if (name == "STR_INS") {
		std::string target;
		std::string dest;
		int location;
		std::size_t insertPos;
		if (args.size() != 3 || !isStringLike(args[0]) || !isStringLike(args[1]) ||
		    args[2].type != TYPE_INT)
			throw std::runtime_error("STR_INS expects (string, string, int).");
		target = valueAsString(args[0]);
		dest = valueAsString(args[1]);
		location = checkedInsertIndex(args[2].i);
		insertPos = static_cast<std::size_t>(location);
		if (insertPos > dest.size())
			insertPos = dest.size();
		dest.insert(insertPos, target);
		enforceStringLength(dest);
		return makeString(dest);
	}
	if (name == "REAL_I") {
		if (args.size() != 1 || args[0].type != TYPE_INT)
			throw std::runtime_error("REAL_I expects one integer argument.");
		return makeReal(static_cast<double>(args[0].i));
	}
	if (name == "INT_R") {
		if (args.size() != 1 || args[0].type != TYPE_REAL)
			throw std::runtime_error("INT_R expects one real argument.");
		if (args[0].r < static_cast<double>(std::numeric_limits<int>::min()) ||
		    args[0].r > static_cast<double>(std::numeric_limits<int>::max()))
			throw std::runtime_error("Real to Integer conversion out of range.");
		return makeInt(static_cast<int>(args[0].r));
	}
	if (name == "RSTR") {
		char fmt[32];
		char buf[256];
		int width;
		int precision;

		if (args.size() != 3 || args[0].type != TYPE_REAL || args[1].type != TYPE_INT ||
		    args[2].type != TYPE_INT)
			throw std::runtime_error("RSTR expects (real, int, int).");

		width = args[1].i;
		precision = args[2].i;
		if (width < 0)
			width = 0;
		if (precision < 0)
			precision = 0;
		if (precision > 20)
			precision = 20;

		std::snprintf(fmt, sizeof(fmt), "%%%d.%df", width, precision);
		std::snprintf(buf, sizeof(buf), fmt, args[0].r);
		enforceStringLength(buf);
		return makeString(buf);
	}
	if (name == "REMOVE_SPACE") {
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("REMOVE_SPACE expects one string argument.");
		return makeString(removeSpaceAscii(valueAsString(args[0])));
	}
	if (name == "GET_EXTENSION") {
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("GET_EXTENSION expects one string argument.");
		return makeString(getExtensionPart(valueAsString(args[0])));
	}
	if (name == "GET_PATH") {
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("GET_PATH expects one string argument.");
		return makeString(getPathPart(valueAsString(args[0])));
	}
	if (name == "TRUNCATE_EXTENSION") {
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("TRUNCATE_EXTENSION expects one string argument.");
		return makeString(truncateExtensionPart(valueAsString(args[0])));
	}
	if (name == "TRUNCATE_PATH") {
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("TRUNCATE_PATH expects one string argument.");
		return makeString(truncatePathPart(valueAsString(args[0])));
	}
	if (name == "FILE_EXISTS") {
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("FILE_EXISTS expects one string argument.");
		return makeInt(fileExistsPath(valueAsString(args[0])) ? 1 : 0);
	}
	if (name == "FIRST_FILE") {
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("FIRST_FILE expects one string argument.");
		return makeInt(findFirstFileMatch(valueAsString(args[0])));
	}
	if (name == "NEXT_FILE") {
		if (!args.empty())
			throw std::runtime_error("NEXT_FILE expects no arguments.");
		return makeInt(findNextFileMatch());
	}
	if (name == "SEARCH_FWD") {
		TMRFileEditor *editor;
		std::size_t matchStart = 0;
		std::size_t matchEnd = 0;
		TMREditWindow *win;
		if (args.size() != 2 || !isStringLike(args[0]) || args[1].type != TYPE_INT)
			throw std::runtime_error("SEARCH_FWD expects (string, int).");
		if (valueAsString(args[0]).empty()) {
			g_runtimeEnv.errorLevel = 1010;
			return makeInt(0);
		}
		editor = currentEditor();
		if (editor == NULL)
			return makeInt(0);
		if (!searchEditorForward(editor, valueAsString(args[0]), valueAsInt(args[1]),
		                         g_runtimeEnv.ignoreCase, matchStart, matchEnd)) {
			g_runtimeEnv.lastSearchValid = false;
			g_runtimeEnv.errorLevel = 0;
			return makeInt(0);
		}
		editor->setCursorOffset(static_cast<uint>(matchStart), 0);
		editor->setSelectionOffsets(static_cast<uint>(matchStart), static_cast<uint>(matchEnd), False);
		editor->revealCursor(True);
		win = currentEditWindow();
		g_runtimeEnv.lastSearchValid = true;
		g_runtimeEnv.lastSearchWindow = win;
		g_runtimeEnv.lastSearchFileName =
		    win != NULL ? std::string(win->currentFileName()) : std::string();
		g_runtimeEnv.lastSearchStart = matchStart;
		g_runtimeEnv.lastSearchEnd = matchEnd;
		g_runtimeEnv.lastSearchCursor = matchStart;
		g_runtimeEnv.errorLevel = 0;
		return makeInt(1);
	}
	if (name == "SEARCH_BWD") {
		TMRFileEditor *editor;
		std::size_t matchStart = 0;
		std::size_t matchEnd = 0;
		TMREditWindow *win;
		if (args.size() != 2 || !isStringLike(args[0]) || args[1].type != TYPE_INT)
			throw std::runtime_error("SEARCH_BWD expects (string, int).");
		if (valueAsString(args[0]).empty()) {
			g_runtimeEnv.errorLevel = 1010;
			return makeInt(0);
		}
		editor = currentEditor();
		if (editor == NULL)
			return makeInt(0);
		if (!searchEditorBackward(editor, valueAsString(args[0]), valueAsInt(args[1]),
		                          g_runtimeEnv.ignoreCase, matchStart, matchEnd)) {
			g_runtimeEnv.lastSearchValid = false;
			g_runtimeEnv.errorLevel = 0;
			return makeInt(0);
		}
		editor->setCursorOffset(static_cast<uint>(matchStart), 0);
		editor->setSelectionOffsets(static_cast<uint>(matchStart), static_cast<uint>(matchEnd), False);
		editor->revealCursor(True);
		win = currentEditWindow();
		g_runtimeEnv.lastSearchValid = true;
		g_runtimeEnv.lastSearchWindow = win;
		g_runtimeEnv.lastSearchFileName =
		    win != NULL ? std::string(win->currentFileName()) : std::string();
		g_runtimeEnv.lastSearchStart = matchStart;
		g_runtimeEnv.lastSearchEnd = matchEnd;
		g_runtimeEnv.lastSearchCursor = matchStart;
		g_runtimeEnv.errorLevel = 0;
		return makeInt(1);
	}
	if (name == "GET_ENVIRONMENT") {
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("GET_ENVIRONMENT expects one string argument.");
		return makeString(getEnvironmentValue(valueAsString(args[0])));
	}
	if (name == "GET_WORD") {
		TMRFileEditor *editor;
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("GET_WORD expects one string argument.");
		editor = currentEditor();
		if (editor == NULL && currentBackgroundEditSession() == NULL)
			return makeString("");
		return makeString(currentEditorWord(editor, valueAsString(args[0])));
	}
	if (name == "PARAM_STR") {
		int index;
		if (args.size() != 1 || args[0].type != TYPE_INT)
			throw std::runtime_error("PARAM_STR expects one integer argument.");
		index = valueAsInt(args[0]);
		if (index == 0)
			return makeString(g_runtimeEnv.startupCommand);
		if (index < 0 || static_cast<std::size_t>(index) > g_runtimeEnv.processArgs.size())
			return makeString("");
		return makeString(g_runtimeEnv.processArgs[static_cast<std::size_t>(index - 1)]);
	}
	if (name == "GLOBAL_STR") {
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("GLOBAL_STR expects one string argument.");
		std::string key = upperKey(valueAsString(args[0]));
		std::map<std::string, GlobalEntry>::const_iterator it = g_runtimeEnv.globals.find(key);
		if (it == g_runtimeEnv.globals.end() || it->second.type != TYPE_STR)
			return makeString("");
		return makeString(valueAsString(it->second.value));
	}
	if (name == "GLOBAL_INT") {
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("GLOBAL_INT expects one string argument.");
		std::string key = upperKey(valueAsString(args[0]));
		std::map<std::string, GlobalEntry>::const_iterator it = g_runtimeEnv.globals.find(key);
		if (it == g_runtimeEnv.globals.end() || it->second.type != TYPE_INT)
			return makeInt(0);
		return makeInt(valueAsInt(it->second.value));
	}
	if (name == "PARSE_STR") {
		if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1]))
			throw std::runtime_error("PARSE_STR expects (string, string).");
		return makeString(parseNamedValue(valueAsString(args[0]), valueAsString(args[1])));
	}
	if (name == "PARSE_INT") {
		std::string parsed;
		int errorPos;
		if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1]))
			throw std::runtime_error("PARSE_INT expects (string, string).");
		parsed = parseNamedValue(valueAsString(args[0]), valueAsString(args[1]));
		if (parsed.empty())
			return makeInt(0);
		errorPos = findValErrorPosition(parsed);
		if (errorPos != 0)
			return makeInt(0);
		return makeInt(static_cast<int>(std::strtol(parsed.c_str(), NULL, 10)));
	}
	if (name == "INQ_MACRO") {
		if (args.size() != 1 || !isStringLike(args[0]))
			throw std::runtime_error("INQ_MACRO expects one string argument.");
		return makeInt(g_runtimeEnv.loadedMacros.find(upperKey(valueAsString(args[0]))) !=
		                       g_runtimeEnv.loadedMacros.end()
		                   ? 1
		                   : 0);
	}

	throw std::runtime_error("Unknown intrinsic: " + name);
}
} // namespace

MRMacroExecutionProfile mrvmAnalyzeBytecode(const unsigned char *bytecode, std::size_t length) {
	MRMacroExecutionProfile profile;
	std::size_t ip = 0;

	if (bytecode == NULL || length == 0)
		return profile;

	while (ip < length) {
		unsigned char opcode = bytecode[ip++];
		++profile.opcodeCount;
		noteExecutionFlags(profile, classifyPureOpcode(opcode));

		switch (opcode) {
			case OP_PUSH_I:
				if (!skipBytecodeBytes(length, ip, sizeof(int)))
					return profile;
				break;
			case OP_PUSH_R:
				if (!skipBytecodeBytes(length, ip, sizeof(double)))
					return profile;
				break;
			case OP_PUSH_S:
			case OP_STORE_VAR:
			case OP_LOAD_VAR:
			case OP_DEF_VAR:
			case OP_VAL:
			case OP_RVAL:
			case OP_FIRST_GLOBAL:
			case OP_NEXT_GLOBAL: {
				std::string ignored;
				if (!readBytecodeCString(bytecode, length, ip, ignored))
					return profile;
				break;
			}
			case OP_GOTO:
			case OP_CALL:
			case OP_JZ:
				if (!skipBytecodeBytes(length, ip, sizeof(int)))
					return profile;
				break;
			case OP_INTRINSIC: {
				std::string name;
				if (!readBytecodeCString(bytecode, length, ip, name) ||
				    !skipBytecodeBytes(length, ip, sizeof(unsigned char)))
					return profile;
				++profile.intrinsicCount;
				name = upperKey(name);
				noteExecutionFlags(profile, classifyIntrinsicName(name), name);
				break;
			}
			case OP_PROC_VAR: {
				std::string name;
				std::string variableName;
				if (!readBytecodeCString(bytecode, length, ip, name) ||
				    !readBytecodeCString(bytecode, length, ip, variableName))
					return profile;
				++profile.procVarCount;
				name = upperKey(name);
				noteExecutionFlags(profile, classifyProcVarName(name), name);
				break;
			}
			case OP_PROC: {
				std::string name;
				if (!readBytecodeCString(bytecode, length, ip, name) ||
				    !skipBytecodeBytes(length, ip, sizeof(unsigned char)))
					return profile;
				++profile.procCount;
				name = upperKey(name);
				noteExecutionFlags(profile, classifyProcName(name), name);
				break;
			}
			case OP_TVCALL: {
				std::string name;
				if (!readBytecodeCString(bytecode, length, ip, name) ||
				    !skipBytecodeBytes(length, ip, sizeof(unsigned char)))
					return profile;
				++profile.tvCallCount;
				name = upperKey(name);
				noteExecutionFlags(profile, classifyTvCallName(name), name);
				break;
			}
			case OP_RET:
			case OP_HALT:
			case OP_ADD:
			case OP_SUB:
			case OP_MUL:
			case OP_DIV:
			case OP_MOD:
			case OP_NEG:
			case OP_CMP_EQ:
			case OP_CMP_NE:
			case OP_CMP_LT:
			case OP_CMP_GT:
			case OP_CMP_LE:
			case OP_CMP_GE:
			case OP_AND:
			case OP_OR:
			case OP_NOT:
			case OP_SHL:
			case OP_SHR:
				break;
			default: {
				char unknownOp[32];
				std::snprintf(unknownOp, sizeof(unknownOp), "UNKNOWN_OPCODE_%02X", opcode);
				noteExecutionFlags(profile, mrefUiAffinity, unknownOp);
				return profile;
			}
		}
	}

	return profile;
}

std::string mrvmDescribeExecutionProfile(const MRMacroExecutionProfile &profile) {
	std::vector<std::string> parts;
	std::ostringstream out;

	if (profile.has(mrefBackgroundSafe))
		parts.push_back("background-safe");
	if (profile.has(mrefStagedWrite))
		parts.push_back("staged-write");
	if (profile.has(mrefUiAffinity))
		parts.push_back("ui-affin");
	if (profile.has(mrefExternalIo))
		parts.push_back("external-io");
	if (parts.empty())
		parts.push_back("unclassified");

	for (std::size_t i = 0; i < parts.size(); ++i) {
		if (i != 0)
			out << ", ";
		out << parts[i];
	}

	out << " [ops=" << profile.opcodeCount << ", intr=" << profile.intrinsicCount
	    << ", proc=" << profile.procCount << ", procvar=" << profile.procVarCount
	    << ", tv=" << profile.tvCallCount << "]";
	return out.str();
}

bool mrvmCanRunInBackground(const MRMacroExecutionProfile &profile) noexcept {
	return profile.has(mrefBackgroundSafe) &&
	       !profile.has(mrefStagedWrite | mrefUiAffinity | mrefExternalIo);
}

namespace {
bool containsOnlySupportedStagedSymbols(const std::vector<std::string> &values) noexcept {
	static const char *const kAllowed[] = {
	    "TEXT",       "PUT_LINE",  "CR",         "DEL_CHAR",   "DEL_CHARS",
	    "DEL_LINE",   "GET_LINE",  "CUR_CHAR",   "GET_WORD",   "C_COL",
	    "C_LINE",     "AT_EOF",    "AT_EOL",     "SET_INDENT_LEVEL",
	    "LEFT",       "RIGHT",     "UP",         "DOWN",       "HOME",
	    "EOL",        "TOF",       "EOF",        "WORD_LEFT",  "WORD_RIGHT",
	    "FIRST_WORD", "GOTO_LINE", "GOTO_COL",   "TAB_RIGHT",  "TAB_LEFT",
	    "INDENT",     "UNDENT",    "MARK_POS",   "GOTO_MARK",  "POP_MARK",
	    "PAGE_UP",    "PAGE_DOWN", "NEXT_PAGE_BREAK", "LAST_PAGE_BREAK",
	    "RUN_MACRO"};

	for (std::size_t i = 0; i < values.size(); ++i) {
		bool allowed = false;
		for (const char *symbol : kAllowed)
			if (values[i] == symbol) {
				allowed = true;
				break;
			}
		if (!allowed)
			return false;
	}
	return true;
}
} // namespace

bool mrvmCanRunStagedInBackground(const MRMacroExecutionProfile &profile) noexcept {
	if (profile.has(mrefExternalIo))
		return false;
	if (!profile.has(mrefStagedWrite | mrefUiAffinity))
		return false;
	if (!containsOnlySupportedStagedSymbols(profile.stagedWriteSymbols))
		return false;
	if (!containsOnlySupportedStagedSymbols(profile.uiAffinitySymbols))
		return false;
	return true;
}

MRMacroJobResult mrvmRunBytecodeBackground(const unsigned char *bytecode, std::size_t length,
                                           std::stop_token stopToken,
                                           std::shared_ptr<std::atomic_bool> cancelFlag) {
	MRMacroJobResult result;
	VirtualMachine vm;
	struct CancelGuard {
		const std::stop_token *savedToken;
		std::shared_ptr<std::atomic_bool> savedFlag;

		CancelGuard(const std::stop_token *token, std::shared_ptr<std::atomic_bool> flag)
		    : savedToken(g_backgroundMacroStopToken), savedFlag(g_backgroundMacroCancelFlag) {
			g_backgroundMacroStopToken = token;
			g_backgroundMacroCancelFlag = std::move(flag);
		}

		~CancelGuard() {
			g_backgroundMacroStopToken = savedToken;
			g_backgroundMacroCancelFlag = savedFlag;
		}
	} cancelGuard(&stopToken, std::move(cancelFlag));

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

MRMacroStagedJobResult mrvmRunBytecodeStagedBackground(const unsigned char *bytecode,
                                                       std::size_t length,
                                                       const MRMacroStagedExecutionInput &input,
                                                       std::stop_token stopToken,
                                                       std::shared_ptr<std::atomic_bool> cancelFlag) {
	MRMacroStagedJobResult result;
	VirtualMachine vm;
	BackgroundEditSession session;
	struct SessionGuard {
		BackgroundEditSession *previous;
		const std::stop_token *savedToken;
		std::shared_ptr<std::atomic_bool> savedFlag;

		SessionGuard(BackgroundEditSession *next, const std::stop_token *token,
		             std::shared_ptr<std::atomic_bool> flag) noexcept
		    : previous(g_backgroundEditSession), savedToken(g_backgroundMacroStopToken),
		      savedFlag(g_backgroundMacroCancelFlag) {
			g_backgroundEditSession = next;
			g_backgroundMacroStopToken = token;
			g_backgroundMacroCancelFlag = std::move(flag);
		}

		~SessionGuard() {
			g_backgroundEditSession = previous;
			g_backgroundMacroStopToken = savedToken;
			g_backgroundMacroCancelFlag = savedFlag;
		}
	};

	session.document = input.document;
	session.transaction = mr::editor::StagedEditTransaction(input.baseVersion, "macro-staged-write");
	session.cursorOffset = input.cursorOffset;
	session.selectionStart = input.selectionStart;
	session.selectionEnd = input.selectionEnd;
	session.insertMode = input.insertMode;
	session.indentLevel = input.indentLevel;
	session.pageLines = std::max(1, input.pageLines);
	session.fileName = input.fileName;
	session.fileChanged = input.fileChanged;
	session.clampState();
	SessionGuard sessionGuard(&session, &stopToken, std::move(cancelFlag));
	vm.execute(bytecode, length);

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
	result.insertMode = session.insertMode;
	result.indentLevel = session.indentLevel;
	result.fileName = session.fileName;
	result.fileChanged = session.fileChanged;
	return result;
}

void mrvmSetProcessContext(int argc, char **argv) {
	g_runtimeEnv.startupCommand.clear();
	g_runtimeEnv.processArgs.clear();
	if (argc > 0 && argv != NULL && argv[0] != NULL)
		g_runtimeEnv.startupCommand = argv[0];
	for (int i = 1; argv != NULL && i < argc; ++i)
		g_runtimeEnv.processArgs.push_back(argv[i] != NULL ? std::string(argv[i]) : std::string());
	g_runtimeEnv.executablePath = detectExecutablePathFromProc();
	if (g_runtimeEnv.executablePath.empty() && !g_runtimeEnv.startupCommand.empty())
		g_runtimeEnv.executablePath = g_runtimeEnv.startupCommand;
	g_runtimeEnv.executableDir = detectExecutableDir(g_runtimeEnv.startupCommand);
	g_runtimeEnv.shellPath = detectShellPath();
	g_runtimeEnv.shellVersion = detectShellVersion(g_runtimeEnv.shellPath);
}

VirtualMachine::Value::Value() : type(TYPE_INT), i(0), r(0.0), s(), c(0) {
}

VirtualMachine::VirtualMachine() : cancelledExecution(false) {
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

	log.push_back("VM Error: Stack underflow.");
	return makeInt(0);
}

void VirtualMachine::execute(const unsigned char *bytecode, size_t length) {
	executeAt(bytecode, length, 0, std::string(), std::string(), true, false);
}

void VirtualMachine::executeAt(const unsigned char *bytecode, size_t length, size_t entryOffset,
                               const std::string &parameterString, const std::string &macroName,
                               bool resetState, bool firstRun) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	size_t ip = entryOffset;
	std::vector<size_t> call_stack;
	std::string savedParameterString = g_runtimeEnv.parameterString;
	bool pushedMacroFrame = false;

	variables.clear();
	stack.clear();
	cancelledExecution = false;
	if (resetState) {
		log.clear();
		g_runtimeEnv.globalEnumIndex = 0;
		g_runtimeEnv.macroEnumIndex = 0;
		g_runtimeEnv.parameterString.clear();
		g_runtimeEnv.returnInt = 0;
		g_runtimeEnv.returnStr.clear();
		g_runtimeEnv.errorLevel = 0;
	}

	if (!macroName.empty()) {
		g_runtimeEnv.macroStack.push_back(MacroStackFrame(macroName, firstRun));
		pushedMacroFrame = true;
	}
	g_runtimeEnv.parameterString = parameterString;

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
				log.push_back("VM Notice: Background macro cancelled.");
				g_runtimeEnv.errorLevel = 5007;
				break;
			}
			unsigned char opcode = bytecode[ip++];

			if (opcode == OP_PUSH_I) {
				int val;
				readInt(val);
				push(makeInt(val));
				log.push_back("Push integer: " + std::to_string(val));
			} else if (opcode == OP_PUSH_R) {
				double val;
				readDouble(val);
				push(makeReal(val));
				log.push_back("Push real: " + valueAsString(makeReal(val)));
			} else if (opcode == OP_PUSH_S) {
				std::string str;
				readCString(str);
				enforceStringLength(str);
				push(makeString(str));
				log.push_back("Push string: " + str);
			} else if (opcode == OP_DEF_VAR) {
				std::string varName;
				int varType = static_cast<int>(bytecode[ip++]);
				readCString(varName);
				variables[varName] = defaultValueForType(varType);
				log.push_back("Define variable: " + varName);
			} else if (opcode == OP_LOAD_VAR) {
				std::string varName;
				bool handled = false;
				readCString(varName);

				Value special = loadSpecialVariable(varName, handled);
				if (handled)
					push(special);
				else {
					std::map<std::string, Value>::const_iterator it = variables.find(varName);
					if (it == variables.end())
						variables[varName] = makeInt(0);
					push(variables[varName]);
				}
				log.push_back("Load variable: " + varName);
			} else if (opcode == OP_STORE_VAR) {
				std::string varName;
				int targetType = static_cast<int>(bytecode[ip++]);
				readCString(varName);
				Value value = coerceForStore(pop(), targetType);
				if (value.type == TYPE_STR)
					enforceStringLength(value.s);
				if (!storeSpecialVariable(varName, value))
					variables[varName] = value;
				log.push_back("Store variable: " + varName);
			} else if (opcode == OP_GOTO) {
				int target;
				readInt(target);
				if (target < 0 || static_cast<size_t>(target) >= length)
					throw std::runtime_error("Invalid jump target in GOTO.");
				ip = static_cast<size_t>(target);
			} else if (opcode == OP_CALL) {
				int target;
				readInt(target);
				if (target < 0 || static_cast<size_t>(target) >= length)
					throw std::runtime_error("Invalid jump target in CALL.");
				call_stack.push_back(ip);
				ip = static_cast<size_t>(target);
			} else if (opcode == OP_RET) {
				if (call_stack.empty())
					throw std::runtime_error("RET without matching CALL.");
				ip = call_stack.back();
				call_stack.pop_back();
			} else if (opcode == OP_JZ) {
				int target;
				Value cond;
				readInt(target);
				cond = pop();
				if (cond.type != TYPE_INT)
					throw std::runtime_error("IF/WHILE expression must be integer.");
				if (target < 0 || static_cast<size_t>(target) >= length)
					throw std::runtime_error("Invalid jump target in JZ.");
				if (cond.i == 0)
					ip = static_cast<size_t>(target);
			} else if (opcode == OP_ADD) {
				Value b = pop();
				Value a = pop();
				if (isStringLike(a) && isStringLike(b)) {
					std::string s = valueAsString(a) + valueAsString(b);
					enforceStringLength(s);
					push(makeString(s));
				} else if (isNumeric(a) && isNumeric(b)) {
					if (a.type == TYPE_REAL || b.type == TYPE_REAL)
						push(makeReal(valueAsReal(a) + valueAsReal(b)));
					else
						push(makeInt(a.i + b.i));
				} else
					throw std::runtime_error("Type mismatch or syntax error.");
			} else if (opcode == OP_SUB) {
				Value b = pop();
				Value a = pop();
				if (!isNumeric(a) || !isNumeric(b))
					throw std::runtime_error("Type mismatch or syntax error.");
				if (a.type == TYPE_REAL || b.type == TYPE_REAL)
					push(makeReal(valueAsReal(a) - valueAsReal(b)));
				else
					push(makeInt(a.i - b.i));
			} else if (opcode == OP_MUL) {
				Value b = pop();
				Value a = pop();
				if (!isNumeric(a) || !isNumeric(b))
					throw std::runtime_error("Type mismatch or syntax error.");
				if (a.type == TYPE_REAL || b.type == TYPE_REAL)
					push(makeReal(valueAsReal(a) * valueAsReal(b)));
				else
					push(makeInt(a.i * b.i));
			} else if (opcode == OP_DIV) {
				Value b = pop();
				Value a = pop();
				if (!isNumeric(a) || !isNumeric(b))
					throw std::runtime_error("Type mismatch or syntax error.");
				if ((b.type == TYPE_REAL && b.r == 0.0) || (b.type == TYPE_INT && b.i == 0))
					throw std::runtime_error("Division by zero.");
				if (a.type == TYPE_REAL || b.type == TYPE_REAL)
					push(makeReal(valueAsReal(a) / valueAsReal(b)));
				else
					push(makeInt(a.i / b.i));
			} else if (opcode == OP_MOD) {
				Value b = pop();
				Value a = pop();
				if (a.type != TYPE_INT || b.type != TYPE_INT)
					throw std::runtime_error("Type mismatch or syntax error.");
				if (b.i == 0)
					throw std::runtime_error("Modulo by zero.");
				push(makeInt(a.i % b.i));
			} else if (opcode == OP_NEG) {
				Value a = pop();
				if (!isNumeric(a))
					throw std::runtime_error("Type mismatch or syntax error.");
				if (a.type == TYPE_REAL)
					push(makeReal(-a.r));
				else
					push(makeInt(-a.i));
			} else if (opcode == OP_CMP_EQ || opcode == OP_CMP_NE || opcode == OP_CMP_LT ||
			           opcode == OP_CMP_GT || opcode == OP_CMP_LE || opcode == OP_CMP_GE) {
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
			} else if (opcode == OP_INTRINSIC) {
				std::string name;
				readCString(name);
				unsigned char argc = bytecode[ip++];
				std::vector<Value> args = popArgs(argc);
				push(applyIntrinsic(name, args));
			} else if (opcode == OP_VAL || opcode == OP_RVAL) {
				std::string varName;
				Value source;
				int resultCode = 0;
				readCString(varName);
				source = pop();
				if (!isStringLike(source))
					throw std::runtime_error("Type mismatch or syntax error.");

				std::string textValue = valueAsString(source);
				if (opcode == OP_VAL) {
					int errorPos = findValErrorPosition(textValue);
					if (errorPos == 0) {
						long long parsed = std::strtoll(textValue.c_str(), NULL, 10);
						if (parsed < static_cast<long long>(std::numeric_limits<int>::min()) ||
						    parsed > static_cast<long long>(std::numeric_limits<int>::max()))
							throw std::runtime_error("Real to Integer conversion out of range.");
						variables[varName] = makeInt(static_cast<int>(parsed));
					} else
						resultCode = errorPos;
				} else {
					int errorPos = findRValErrorPosition(textValue);
					if (errorPos == 0) {
						char *endPtr = NULL;
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
				if (opcode == OP_FIRST_GLOBAL)
					g_runtimeEnv.globalEnumIndex = 0;

				while (g_runtimeEnv.globalEnumIndex < g_runtimeEnv.globalOrder.size()) {
					const std::string &key =
					    g_runtimeEnv.globalOrder[g_runtimeEnv.globalEnumIndex++];
					std::map<std::string, GlobalEntry>::const_iterator it =
					    g_runtimeEnv.globals.find(key);
					if (it == g_runtimeEnv.globals.end())
						continue;
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
				std::map<std::string, Value>::iterator it;
				readCString(name);
				readCString(varName);
				it = variables.find(varName);
				if (it == variables.end())
					throw std::runtime_error("Variable expected.");
				if (it->second.type != TYPE_STR)
					throw std::runtime_error("Type mismatch or syntax error.");
				if (name == "CRUNCH_TABS")
					it->second = makeString(crunchTabsString(valueAsString(it->second)));
				else if (name == "EXPAND_TABS")
					it->second = makeString(
					    expandTabsString(valueAsString(it->second), g_runtimeEnv.tabExpand));
				else if (name == "TABS_TO_SPACES")
					it->second = makeString(tabsToSpacesString(valueAsString(it->second)));
				else
					throw std::runtime_error("Unknown variable procedure.");
			} else if (opcode == OP_PROC) {
				std::string name;
				readCString(name);
				unsigned char argc = bytecode[ip++];
				std::vector<Value> args = popArgs(argc);
				if (name == "SET_GLOBAL_STR") {
					if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1]))
						throw std::runtime_error("SET_GLOBAL_STR expects (string, string).");
					setGlobalValue(valueAsString(args[0]), TYPE_STR,
					               makeString(valueAsString(args[1])));
				} else if (name == "SET_GLOBAL_INT") {
					if (args.size() != 2 || !isStringLike(args[0]) || args[1].type != TYPE_INT)
						throw std::runtime_error("SET_GLOBAL_INT expects (string, int).");
					setGlobalValue(valueAsString(args[0]), TYPE_INT, makeInt(args[1].i));
				} else if (name == "LOAD_MACRO_FILE") {
					if (args.size() != 1 || !isStringLike(args[0]))
						throw std::runtime_error("LOAD_MACRO_FILE expects one string argument.");
					loadMacroFileIntoRegistry(valueAsString(args[0]), NULL);
				} else if (name == "UNLOAD_MACRO") {
					if (args.size() != 1 || !isStringLike(args[0]))
						throw std::runtime_error("UNLOAD_MACRO expects one string argument.");
					unloadMacroFromRegistry(valueAsString(args[0]));
				} else if (name == "CHANGE_DIR") {
					if (args.size() != 1 || !isStringLike(args[0]))
						throw std::runtime_error("CHANGE_DIR expects one string argument.");
					if (changeDirectoryPath(valueAsString(args[0])))
						g_runtimeEnv.errorLevel = 0;
					else
						g_runtimeEnv.errorLevel = errno != 0 ? errno : 1;
				} else if (name == "DEL_FILE") {
					if (args.size() != 1 || !isStringLike(args[0]))
						throw std::runtime_error("DEL_FILE expects one string argument.");
					if (deleteFilePath(valueAsString(args[0])))
						g_runtimeEnv.errorLevel = 0;
					else
						g_runtimeEnv.errorLevel = errno != 0 ? errno : 1;
				} else if (name == "LOAD_FILE") {
					TMREditWindow *win;
					std::string path;
					if (args.size() != 1 || !isStringLike(args[0]))
						throw std::runtime_error("LOAD_FILE expects one string argument.");
					path = expandUserPath(valueAsString(args[0]));
					win = currentEditWindow();
					if (win == NULL) {
						g_runtimeEnv.errorLevel = 1001;
						continue;
					}
					if (!fileExistsPath(path)) {
						g_runtimeEnv.errorLevel = 3002;
						continue;
					}
					if (!win->loadFromFile(path.c_str())) {
						g_runtimeEnv.errorLevel = 3002;
						continue;
					}
					g_runtimeEnv.lastFileName = win->currentFileName();
					g_runtimeEnv.errorLevel = 0;
				} else if (name == "SAVE_FILE") {
					TMREditWindow *win = currentEditWindow();
					if (!args.empty())
						throw std::runtime_error("SAVE_FILE expects no arguments.");
					if (win == NULL) {
						g_runtimeEnv.errorLevel = 1001;
						continue;
					}
					if (!win->saveCurrentFile()) {
						g_runtimeEnv.errorLevel = 2002;
						continue;
					}
					g_runtimeEnv.lastFileName = win->currentFileName();
					g_runtimeEnv.errorLevel = 0;
				} else if (name == "SAVE_BLOCK") {
					TMREditWindow *win = currentEditWindow();
					TMRFileEditor *editor = currentEditor();
					std::string path;
					if (args.size() != 1 || !isStringLike(args[0]))
						throw std::runtime_error("SAVE_BLOCK expects one string argument.");
					if (win == NULL || editor == NULL) {
						g_runtimeEnv.errorLevel = 1001;
						continue;
					}
					path = expandUserPath(valueAsString(args[0]));
					if (!saveCurrentBlockToFile(win, editor, path)) {
						g_runtimeEnv.errorLevel = errno != 0 ? errno : 1010;
						continue;
					}
					g_runtimeEnv.lastFileName = path;
					g_runtimeEnv.errorLevel = 0;
				} else if (name == "SET_INDENT_LEVEL") {
					if (!args.empty())
						throw std::runtime_error("SET_INDENT_LEVEL expects no arguments.");
					g_runtimeEnv.errorLevel =
					    setCurrentEditorIndentLevel(currentEditorColumn(currentEditor())) ? 0
					                                                                      : 1001;
				} else if (name == "REPLACE") {
					TMRFileEditor *editor;
					if (args.size() != 1 || !isStringLike(args[0]))
						throw std::runtime_error("REPLACE expects one string argument.");
					editor = currentEditor();
					if (editor == NULL) {
						g_runtimeEnv.errorLevel = 1001;
						continue;
					}
					replaceLastSearch(editor, valueAsString(args[0]));
					g_runtimeEnv.errorLevel = 0;
				} else if (name == "TEXT") {
					TMRFileEditor *editor;
					if (args.size() != 1 || !isStringLike(args[0]))
						throw std::runtime_error("TEXT expects one string argument.");
					editor = currentEditor();
					if (editor == NULL && currentBackgroundEditSession() == NULL) {
						g_runtimeEnv.errorLevel = 1001;
						continue;
					}
					insertEditorText(editor, valueAsString(args[0]));
					g_runtimeEnv.errorLevel = 0;
				} else if (name == "PUT_LINE") {
					TMRFileEditor *editor;
					if (args.size() != 1 || !isStringLike(args[0]))
						throw std::runtime_error("PUT_LINE expects one string argument.");
					editor = currentEditor();
					if (editor == NULL && currentBackgroundEditSession() == NULL) {
						g_runtimeEnv.errorLevel = 1001;
						continue;
					}
					replaceEditorLine(editor, valueAsString(args[0]));
					g_runtimeEnv.errorLevel = 0;
				} else if (name == "CR") {
					TMRFileEditor *editor = currentEditor();
					if (!args.empty())
						throw std::runtime_error("CR expects no arguments.");
					if (editor == NULL && currentBackgroundEditSession() == NULL) {
						g_runtimeEnv.errorLevel = 1001;
						continue;
					}
					carriageReturnEditor(editor);
					g_runtimeEnv.errorLevel = 0;
				} else if (name == "DEL_CHAR") {
					TMRFileEditor *editor = currentEditor();
					if (!args.empty())
						throw std::runtime_error("DEL_CHAR expects no arguments.");
					if (editor == NULL && currentBackgroundEditSession() == NULL) {
						g_runtimeEnv.errorLevel = 1001;
						continue;
					}
					deleteEditorChars(editor, 1);
					g_runtimeEnv.errorLevel = 0;
				} else if (name == "DEL_CHARS") {
					TMRFileEditor *editor = currentEditor();
					if (args.size() != 1 || args[0].type != TYPE_INT)
						throw std::runtime_error("DEL_CHARS expects one integer argument.");
					if (editor == NULL && currentBackgroundEditSession() == NULL) {
						g_runtimeEnv.errorLevel = 1001;
						continue;
					}
					deleteEditorChars(editor, valueAsInt(args[0]));
					g_runtimeEnv.errorLevel = 0;
				} else if (name == "DEL_LINE") {
					TMRFileEditor *editor = currentEditor();
					if (!args.empty())
						throw std::runtime_error("DEL_LINE expects no arguments.");
					if (editor == NULL && currentBackgroundEditSession() == NULL) {
						g_runtimeEnv.errorLevel = 1001;
						continue;
					}
					deleteEditorLine(editor);
					g_runtimeEnv.errorLevel = 0;
				} else if (name == "LEFT" || name == "RIGHT" || name == "UP" || name == "DOWN" ||
				           name == "HOME" || name == "EOL" || name == "TOF" || name == "EOF" ||
				           name == "WORD_LEFT" || name == "WORD_RIGHT" || name == "FIRST_WORD" ||
				           name == "MARK_POS" || name == "GOTO_MARK" || name == "POP_MARK" ||
				           name == "PAGE_UP" || name == "PAGE_DOWN" || name == "NEXT_PAGE_BREAK" ||
				           name == "LAST_PAGE_BREAK" || name == "TAB_RIGHT" || name == "TAB_LEFT" ||
				           name == "INDENT" || name == "UNDENT" || name == "BLOCK_BEGIN" ||
				           name == "COL_BLOCK_BEGIN" || name == "STR_BLOCK_BEGIN" ||
				           name == "BLOCK_END" || name == "BLOCK_OFF" || name == "COPY_BLOCK" ||
				           name == "MOVE_BLOCK" || name == "DELETE_BLOCK" ||
				           name == "CREATE_WINDOW" || name == "DELETE_WINDOW" ||
				           name == "ERASE_WINDOW" || name == "MODIFY_WINDOW" ||
				           name == "LINK_WINDOW" || name == "UNLINK_WINDOW" ||
				           name == "ZOOM" || name == "REDRAW" || name == "NEW_SCREEN") {
					TMRFileEditor *editor = currentEditor();
					bool ok = false;
					if (!args.empty())
						throw std::runtime_error((name + " expects no arguments.").c_str());
					if (editor == NULL && currentBackgroundEditSession() == NULL && name != "CREATE_WINDOW") {
						g_runtimeEnv.errorLevel = 1001;
						continue;
					}
					if (name == "LEFT")
						ok = moveEditorLeft(editor);
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
						ok = markEditorPosition(currentEditWindow(), editor);
					else if (name == "GOTO_MARK")
						ok = gotoEditorMark(currentEditWindow(), editor);
					else if (name == "POP_MARK")
						ok = popEditorMark(currentEditWindow());
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
					else if (name == "BLOCK_BEGIN") {
						currentEditWindow()->beginLineBlock();
						ok = true;
					} else if (name == "COL_BLOCK_BEGIN") {
						currentEditWindow()->beginColumnBlock();
						ok = true;
					} else if (name == "STR_BLOCK_BEGIN") {
						currentEditWindow()->beginStreamBlock();
						ok = true;
					} else if (name == "BLOCK_END") {
						currentEditWindow()->endBlock();
						ok = true;
					} else if (name == "BLOCK_OFF") {
						currentEditWindow()->clearBlock();
						ok = true;
					} else if (name == "COPY_BLOCK")
						ok = copyCurrentBlock(currentEditWindow(), editor);
					else if (name == "MOVE_BLOCK")
						ok = moveCurrentBlock(currentEditWindow(), editor);
					else if (name == "DELETE_BLOCK")
						ok = deleteCurrentBlock(currentEditWindow(), editor);
					else if (name == "CREATE_WINDOW")
						ok = createEditWindow();
					else if (name == "DELETE_WINDOW")
						ok = deleteCurrentEditWindow();
					else if (name == "ERASE_WINDOW")
						ok = eraseCurrentEditWindow();
					else if (name == "MODIFY_WINDOW")
						ok = modifyCurrentEditWindow();
					else if (name == "LINK_WINDOW")
						ok = linkCurrentEditWindow();
					else if (name == "UNLINK_WINDOW")
						ok = unlinkCurrentEditWindow();
					else if (name == "ZOOM")
						ok = zoomCurrentEditWindow();
					else if (name == "REDRAW")
						ok = redrawCurrentEditWindow();
					else if (name == "NEW_SCREEN")
						ok = redrawEntireScreen();
					g_runtimeEnv.errorLevel = ok ? 0 : 1001;
				} else if (name == "GOTO_LINE") {
					TMRFileEditor *editor = currentEditor();
					if (args.size() != 1 || args[0].type != TYPE_INT)
						throw std::runtime_error("GOTO_LINE expects one integer argument.");
					if (editor == NULL && currentBackgroundEditSession() == NULL) {
						g_runtimeEnv.errorLevel = 1001;
						continue;
					}
					g_runtimeEnv.errorLevel =
					    gotoEditorLine(editor, valueAsInt(args[0])) ? 0 : 1010;
				} else if (name == "GOTO_COL") {
					TMRFileEditor *editor = currentEditor();
					if (args.size() != 1 || args[0].type != TYPE_INT)
						throw std::runtime_error("GOTO_COL expects one integer argument.");
					if (editor == NULL && currentBackgroundEditSession() == NULL) {
						g_runtimeEnv.errorLevel = 1001;
						continue;
					}
					g_runtimeEnv.errorLevel = gotoEditorCol(editor, valueAsInt(args[0])) ? 0 : 1010;
				} else if (name == "SWITCH_WINDOW") {
					if (args.size() != 1 || args[0].type != TYPE_INT)
						throw std::runtime_error("SWITCH_WINDOW expects one integer argument.");
					g_runtimeEnv.errorLevel = switchEditWindow(valueAsInt(args[0])) ? 0 : 1001;
				} else if (name == "SIZE_WINDOW") {
					if (args.size() != 4 || args[0].type != TYPE_INT || args[1].type != TYPE_INT ||
					    args[2].type != TYPE_INT || args[3].type != TYPE_INT)
						throw std::runtime_error("SIZE_WINDOW expects four integer arguments.");
					g_runtimeEnv.errorLevel =
					    sizeCurrentEditWindow(valueAsInt(args[0]), valueAsInt(args[1]),
					                          valueAsInt(args[2]), valueAsInt(args[3]))
					        ? 0
					        : 1010;
				} else if (name == "WINDOW_COPY" || name == "WINDOW_MOVE") {
					TMREditWindow *destWin = currentEditWindow();
					TMRFileEditor *destEditor = currentEditor();
					TMREditWindow *srcWin;
					TMRFileEditor *srcEditor;
					int windowNum;
					bool ok;
					if (args.size() != 1 || args[0].type != TYPE_INT)
						throw std::runtime_error((name + " expects one integer argument.").c_str());
					if (destWin == NULL || destEditor == NULL) {
						g_runtimeEnv.errorLevel = 1001;
						continue;
					}
					windowNum = valueAsInt(args[0]);
					srcWin = editWindowByIndex(windowNum);
					srcEditor = srcWin != NULL ? srcWin->getEditor() : NULL;
					if (srcWin == NULL || srcEditor == NULL) {
						g_runtimeEnv.errorLevel = 1010;
						continue;
					}
					ok = (name == "WINDOW_COPY")
					         ? copyBlockFromWindow(srcWin, srcEditor, destWin, destEditor)
					         : moveBlockFromWindow(srcWin, srcEditor, destWin, destEditor);
					g_runtimeEnv.errorLevel = ok ? 0 : 1001;
				} else if (name == "RUN_MACRO") {
					std::string spec;
					std::string filePart;
					std::string macroPart;
					std::string paramPart;
					std::string targetFileKey;
					std::string macroKey;
					std::map<std::string, MacroRef>::iterator mit;
					std::map<std::string, LoadedMacroFile>::iterator fit;
					VirtualMachine childVm;
					bool backgroundStaged = currentBackgroundEditSession() != NULL;

					if (args.size() != 1 || !isStringLike(args[0]))
						throw std::runtime_error("RUN_MACRO expects one string argument.");

					spec = valueAsString(args[0]);
					if (!parseRunMacroSpec(spec, filePart, macroPart, paramPart)) {
						g_runtimeEnv.errorLevel = 5001;
						continue;
					}

					macroKey = upperKey(macroPart);
					if (!filePart.empty())
						targetFileKey = makeFileKey(filePart);

					mit = g_runtimeEnv.loadedMacros.find(macroKey);
					if (mit == g_runtimeEnv.loadedMacros.end() ||
					    (!targetFileKey.empty() && mit->second.fileKey != targetFileKey)) {
						if (backgroundStaged) {
							g_runtimeEnv.errorLevel = 5001;
							continue;
						}
						if (!filePart.empty()) {
							if (!loadMacroFileIntoRegistry(filePart, &targetFileKey))
								continue;
						} else {
							if (!loadMacroFileIntoRegistry(macroPart, &targetFileKey))
								continue;
						}
						mit = g_runtimeEnv.loadedMacros.find(macroKey);
					}

					if (mit == g_runtimeEnv.loadedMacros.end() ||
					    (!targetFileKey.empty() && mit->second.fileKey != targetFileKey)) {
						g_runtimeEnv.errorLevel = 5001;
						continue;
					}

					fit = g_runtimeEnv.loadedFiles.find(mit->second.fileKey);
					if (fit == g_runtimeEnv.loadedFiles.end()) {
						g_runtimeEnv.errorLevel = 5001;
						continue;
					}
					if (backgroundStaged) {
						if (fit->second.bytecode.empty() || !currentBackgroundChildMacroAllowed(fit->second)) {
							g_runtimeEnv.errorLevel = 5001;
							continue;
						}
					} else if (!ensureLoadedFileResident(mit->second.fileKey))
						continue;
					fit = g_runtimeEnv.loadedFiles.find(mit->second.fileKey);
					if (fit == g_runtimeEnv.loadedFiles.end() || fit->second.bytecode.empty()) {
						g_runtimeEnv.errorLevel = 5001;
						continue;
					}

					{
						bool childFirstRun = mit->second.firstRunPending;
						bool childDump = mit->second.dumpAttr;
						bool childTransient = mit->second.transientAttr;
						std::string childMacroKey = macroKey;
						std::string childFileKey = mit->second.fileKey;
						mit->second.firstRunPending = false;
						childVm.executeAt(fit->second.bytecode.data(), fit->second.bytecode.size(),
						                  mit->second.entryOffset, paramPart,
						                  mit->second.displayName, false, childFirstRun);
						log.insert(log.end(), childVm.log.begin(), childVm.log.end());
						if (childDump)
							unloadMacroFromRegistry(childMacroKey);
						else if (childTransient)
							evictTransientFileImage(childFileKey);
					}
					g_runtimeEnv.errorLevel = 0;
				} else {
					throw std::runtime_error("Unknown procedure: " + name);
				}
			} else if (opcode == OP_TVCALL) {
				std::string funcName;
				readCString(funcName);
				unsigned char argc = bytecode[ip++];
				std::vector<Value> args = popArgs(argc);

				log.push_back("TVCALL: " + funcName + " (" + std::to_string(argc) + " params)");

				if (funcName == "MessageBox") {
					if (args.empty())
						messageBox(mfInformation | mfOKButton, "%s", "");
					else {
						std::string text;
						for (size_t i = 0; i < args.size(); ++i) {
							if (i != 0)
								text += ' ';
							text += valueAsString(args[i]);
						}
						messageBox(mfInformation | mfOKButton, "%s", text.c_str());
					}
				}
			} else if (opcode == OP_HALT) {
				log.push_back("Program end reached.");
				break;
			} else {
				char hexOp[10];
				std::snprintf(hexOp, sizeof(hexOp), "0x%02X", opcode);
				throw std::runtime_error(std::string("Unknown opcode ") + hexOp);
			}

			syncLinkedWindowsFrom(currentEditWindow());
		}
	} catch (const std::exception &ex) {
		log.push_back(std::string("VM Error: ") + ex.what());
	}

	g_runtimeEnv.parameterString = savedParameterString;
	if (pushedMacroFrame)
		g_runtimeEnv.macroStack.pop_back();
}

bool mrvmUiLinkCurrentWindow() {
	return linkCurrentEditWindow();
}

bool mrvmUiUnlinkCurrentWindow() {
	return unlinkCurrentEditWindow();
}

bool mrvmUiZoomCurrentWindow() {
	return zoomCurrentEditWindow();
}

bool mrvmUiRedrawCurrentWindow() {
	return redrawCurrentEditWindow();
}

bool mrvmUiNewScreen() {
	return redrawEntireScreen();
}
