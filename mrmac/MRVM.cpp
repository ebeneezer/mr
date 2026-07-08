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
#define Uses_TClipboard
#include <tvision/tv.h>

#include "mrmac.h"
#include "MRMacroModelessUi.hpp"
#include "MRVM.hpp"
#include "vm/MRVMExecSessions.hpp"
#include "vm/MRVMDeferredUi.hpp"
#include "vm/MRVMHash.hpp"
#include "vm/MRVMMacroDialogRuntime.hpp"
#include "vm/MRVMKeymapRuntime.hpp"
#include "vm/MRVMMacroSpecRuntime.hpp"
#include "vm/MRVMModelessUiRuntime.hpp"
#include "vm/MRVMProcessRuntime.hpp"
#include "vm/MRVMRuntimeCatalog.hpp"
#include "vm/MRVMRuntimeGlobals.hpp"
#include "vm/MRVMRuntimeKv.hpp"
#include "vm/MRVMRuntimeState.hpp"
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
#include <deque>
#include <fstream>
#include <glob.h>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "../ui/MREditWindow.hpp"
#include "../app/MRCommandRouter.hpp"
#include "../app/MRRuntimeScheduler.hpp"
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

MREditWindow *createEditorWindow(const char *title);
std::vector<MREditWindow *> allEditWindowsInZOrder();
bool moveToNextVirtualDesktop();
bool moveToPrevVirtualDesktop();
bool viewportRight();
bool viewportLeft();
void mrSaveWorkspace(const std::string &filename);
void mrLoadWorkspace(const std::string &filename);
void applyVirtualDesktopConfigurationChange(int count);

RuntimeEnvironment g_runtimeEnv;
std::recursive_mutex g_vmExecutionMutex;
thread_local BackgroundEditSession *g_backgroundEditSession = nullptr;
thread_local const std::stop_token *g_backgroundMacroStopToken = nullptr;
thread_local std::shared_ptr<std::atomic_bool> g_backgroundMacroCancelFlag;
thread_local ExecutionState *g_executionState = nullptr;
thread_local MRMacroExecutionSessionId g_executionSessionId = 0;

namespace {
using Value = VirtualMachine::Value;

static void setGlobalValue(const std::string &name, int type, const Value &value);
static void setGlobalValueFromStore(const std::string &name, int type, const Value &value, MRVMHashStore &localStore);
static bool readGlobalValue(const std::string &name, GlobalEntry &entry);

static std::string getEnvironmentValue(const std::string &entryName);
static bool loadMacroFileIntoRegistry(const std::string &spec, std::string *loadedFileKey = nullptr);
static bool unloadMacroFromRegistry(const std::string &macroName);
static bool ensureLoadedFileResident(const std::string &fileKey);
static bool evictTransientFileImage(const std::string &fileKey);
static bool currentBackgroundChildMacroAllowed(const LoadedMacroFile &file) noexcept;
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
static MRMacroExecutionSessionId currentExecutionSessionId() noexcept;
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
bool createEditWindow();
bool switchEditWindow(int index);
bool sizeCurrentEditWindow(int x1, int y1, int x2, int y2);
bool deleteCurrentEditWindow();
bool eraseCurrentEditWindow();
bool modifyCurrentEditWindow();
static int currentUiMacroMode();
static bool macroAllowsUiMode(const MacroRef &macroRef, int mode) noexcept;
static bool executeLoadedMacro(const std::string &macroKey, const std::string &paramPart, std::vector<std::string> *logSink);
static bool executeLoadedMacroWithConfiguredKeymapBatch(const std::string &macroKey, const std::string &paramPart, std::vector<std::string> *logSink);
static bool readLoadedMacroByKey(const std::string &macroKey, MacroRef &macroRef);
static std::vector<std::string> macroCatalogMacroOrder();
static std::size_t macroCatalogMacroEnumIndex();
static void setMacroCatalogMacroEnumIndex(std::size_t index);
static bool tryLoadIndexedMacroForKey(const TKey &pressed);
static bool currentExecutingMacroSpecFromRuntimeStack(std::string &macroSpec);
static bool composeLoadedMacroSpec(const MacroRef &macroRef, std::string &macroSpec);
static bool macroSpecTargetsLoadedMacro(const std::string &spec, const std::string &targetFileKey, const std::string &targetMacroKey);

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

static std::string getEnvironmentValue(const std::string &entryName) {
	std::string key = trimAscii(entryName);
	std::size_t pos = key.find('=');
	if (pos != std::string::npos) key = key.substr(0, pos);
	if (key.empty()) return std::string();
	std::string direct = mrvmGetenvValue(key);
	if (!direct.empty()) return direct;
	std::string up = mrvmUpperKey(key);
	if (up == "MR_PATH") return g_runtimeEnv.executableDir;
	if (up == "COMSPEC") return g_runtimeEnv.shellPath;
	if (up == "OS_VERSION") return g_runtimeEnv.shellVersion;
	return std::string();
}

static int findFirstFileMatch(const std::string &pattern) {
	glob_t g;
	std::string expanded = mrvmProcessExpandUserPath(trimAscii(pattern));
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

	if (mrvmFileExistsPath(expanded)) {
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

static BackgroundEditSession *currentBackgroundEditSession() noexcept {
	return g_backgroundEditSession;
}

static ExecutionState *currentExecutionState() noexcept {
	return g_executionState;
}

static MRMacroExecutionSessionId currentExecutionSessionId() noexcept {
	return g_executionSessionId;
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
		if (win != nullptr) return mrvmMakeInt(win->hasBeenSavedInSession() ? 1 : 0);
		return mrvmMakeInt(session != nullptr && session->firstSave ? 1 : 0);
	}
	if (key == "BUFFER_ID") {
		if (win != nullptr) return mrvmMakeInt(win->bufferId());
		if (session != nullptr) return mrvmMakeInt(session->bufferId);
		return mrvmMakeInt(0);
	}
	if (key == "TMP_FILE") {
		if (win != nullptr) return mrvmMakeInt(win->isTemporaryFile() ? 1 : 0);
		return mrvmMakeInt(session != nullptr && session->temporaryFile ? 1 : 0);
	}
	if (key == "TMP_FILE_NAME") {
		if (win != nullptr) return mrvmMakeString(win->temporaryFileName());
		if (session != nullptr) return mrvmMakeString(session->temporaryFileName);
		return mrvmMakeString("");
	}
	if (key == "FILE_CHANGED") {
		if (win != nullptr) return mrvmMakeInt(win->isFileChanged() ? 1 : 0);
		return mrvmMakeInt(session != nullptr && session->fileChanged ? 1 : 0);
	}
	if (key == "FILE_NAME") {
		if (win != nullptr) return mrvmMakeString(win->currentFileName());
		if (session != nullptr) return mrvmMakeString(session->fileName);
		return mrvmMakeString("");
	}
	if (key == "CUR_FILE_ATTR") {
		int attr = 0;
		std::string path = win != nullptr ? std::string(win->currentFileName()) : (session != nullptr ? session->fileName : std::string());
		if (!mrvmReadFileMetadata(path, &attr, nullptr, nullptr)) return mrvmMakeInt(0);
		return mrvmMakeInt(attr);
	}
	if (key == "CUR_FILE_SIZE") {
		int size = 0;
		std::string path = win != nullptr ? std::string(win->currentFileName()) : (session != nullptr ? session->fileName : std::string());
		if (!mrvmReadFileMetadata(path, nullptr, &size, nullptr)) return mrvmMakeInt(0);
		return mrvmMakeInt(size);
	}
	if (key == "READ_ONLY") {
		if (win != nullptr) return mrvmMakeInt(win->isReadOnly() ? 1 : 0);
		return mrvmMakeInt(0);
	}
	return mrvmMakeInt(0);
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
		haystack = mrvmUpperKey(haystack);
		query = mrvmUpperKey(query);
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
		haystack = mrvmUpperKey(haystack);
		query = mrvmUpperKey(query);
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
		if (session == nullptr) return mrvmMakeChar(static_cast<char>(255));
		lineEnd = static_cast<uint>(session->document.lineEnd(session->cursorOffset));
		if (session->cursorOffset >= session->document.length() || session->cursorOffset >= lineEnd) return mrvmMakeChar(static_cast<char>(255));
		return mrvmMakeChar(session->document.charAt(session->cursorOffset));
	}
	lineEnd = editor->lineEndOffset(editor->cursorOffset());
	if (editor->cursorOffset() >= editor->bufferLength() || editor->cursorOffset() >= lineEnd) return mrvmMakeChar(static_cast<char>(255));
	return mrvmMakeChar(editor->charAtOffset(editor->cursorOffset()));
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
	mrvmEnforceStringLength(out);
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
	mrvmEnforceStringLength(out);
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
		mrvmEnforceStringLength(out);
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
	mrvmEnforceStringLength(out);
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

	for (std::size_t index = 0; index < windows.size(); ++index)
		live.insert(windows[index]);

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
	for (std::size_t index = 0; index < windows.size(); ++index) {
		MREditWindow *window = windows[index];
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
	for (std::size_t index = 0; index < windows.size(); ++index)
		windows[index]->drawView();
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
	const std::string key = mrvmUpperKey(style);
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
	const std::string key = mrvmUpperKey(method);
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
	std::string key = mrvmUpperKey(name);
	handled = true;
	if (key == "RETURN_INT") return mrvmMakeInt(runtimeReturnInt());
	if (key == "RETURN_STR") return mrvmMakeString(runtimeReturnStr());
	if (key == "ERROR_LEVEL") return mrvmMakeInt(runtimeErrorLevel());
	if (key == "IGNORE_CASE") return mrvmMakeInt(currentRuntimeIgnoreCase() ? 1 : 0);
	if (key == "REG_EXP_STAT") return mrvmMakeInt(currentRegexStatusValue());
	if (key == "TAB_EXPAND") return mrvmMakeInt(currentRuntimeTabExpand() ? 1 : 0);
	if (key == "DISPLAY_TABS") return mrvmMakeInt(configuredDisplayTabsSetting() ? 1 : 0);
	if (key == "SHADOW_CHAR") return mrvmMakeInt(g_runtimeEnv.shadowChar);
	if (key == "REFRESH") return mrvmMakeInt(g_runtimeEnv.refresh);
	if (key == "MESSAGES") return mrvmMakeInt(configuredMenulineMessages() ? 1 : 0);
	if (key == "MOUSE") return mrvmMakeInt(g_runtimeEnv.mouse);
	if (key == "LOGO_SCREEN") return mrvmMakeInt(g_runtimeEnv.logoScreen);
	if (key == "EXPLOSIONS") return mrvmMakeInt(g_runtimeEnv.explosions);
	if (key == "TRUNCATE_SPACES") return mrvmMakeInt(configuredEditSetupSettings().truncateSpaces ? 1 : 0);
	if (key == "BACKUPS") {
		const MREditSetupSettings settings = configuredEditSetupSettings();
		if (!settings.backupFiles) return mrvmMakeInt(0);
		return mrvmMakeInt(encodeBackupMode(settings.backupMethod));
	}
	if (key == "AUTOSAVE") {
		const MREditSetupSettings settings = configuredEditSetupSettings();
		return mrvmMakeInt((settings.autosaveInactivitySeconds > 0 || settings.autosaveIntervalSeconds > 0) ? 1 : 0);
	}
	if (key == "UNDO_STAT") return mrvmMakeInt(g_runtimeEnv.undoStat);
	if (key == "FORMAT_STAT") return mrvmMakeInt(g_runtimeEnv.formatStat);
	if (key == "WRAP_STAT") return mrvmMakeInt(configuredEditSetupSettings().wordWrap ? 1 : 0);
	if (key == "MEM_ALLOC") return mrvmMakeInt(g_runtimeEnv.memAlloc);
	if (key == "LEFT_MARGIN") return mrvmMakeInt(configuredEditSetupSettings().leftMargin);
	if (key == "RIGHT_MARGIN") return mrvmMakeInt(configuredEditSetupSettings().rightMargin);
	if (key == "FORMAT_RULER") return mrvmMakeInt(configuredEditSetupSettings().formatRuler ? 1 : 0);
	if (key == "INDENT_STYLE") return mrvmMakeInt(encodeIndentStyle(configuredEditSetupSettings().indentStyle));
	if (key == "INS_CURSOR") return mrvmMakeInt(g_runtimeEnv.insCursor);
	if (key == "OVR_CURSOR") return mrvmMakeInt(g_runtimeEnv.ovrCursor);
	if (key == "CTRL_HELP") return mrvmMakeInt(g_runtimeEnv.ctrlHelp);
	if (key == "MOUSE_H_SENSE") return mrvmMakeInt(g_runtimeEnv.mouseHSense);
	if (key == "MOUSE_V_SENSE") return mrvmMakeInt(g_runtimeEnv.mouseVSense);
	if (key == "WINDOW_ATTR") return mrvmMakeInt(currentWindowAttrValue());
	if (key == "TEXT_COLOR") return mrvmMakeInt(readWindowColorValue(0));
	if (key == "CHANGE_COLOR") return mrvmMakeInt(readWindowColorValue(1));
	if (key == "BACK_COLOR") return mrvmMakeInt(readOtherColorValue(9));
	if (key == "MENU_COLOR") return mrvmMakeInt(readMenuDialogColorValue(0));
	if (key == "STAT_COLOR") return mrvmMakeInt(readOtherColorValue(0));
	if (key == "ERROR_COLOR") return mrvmMakeInt(readOtherColorValue(4));
	if (key == "SHADOW_COLOR") return mrvmMakeInt(readMenuDialogColorValue(7));
	if (key == "STATUS_ROW") return mrvmMakeInt(currentStatusRowValue());
	if (key == "MESSAGE_ROW") return mrvmMakeInt(currentMessageRowValue());
	if (key == "MAX_WINDOW_ROW") return mrvmMakeInt(currentMaxWindowRowValue());
	if (key == "MIN_WINDOW_ROW") return mrvmMakeInt(currentMinWindowRowValue());
	if (key == "NAME_LINE") return mrvmMakeInt(g_runtimeEnv.nameLine);
	if (key == "INSERT_MODE") return mrvmMakeInt(currentEditorInsertMode() ? 1 : 0);
	if (key == "INDENT_LEVEL") return mrvmMakeInt(currentEditorIndentLevel());
	if (key == "MPARM_STR") return mrvmMakeString(runtimeParameterString());
	if (key == "DATE") return mrvmMakeString(formatCurrentDate());
	if (key == "TIME") return mrvmMakeString(formatCurrentTime());
	if (key == "COMSPEC") return mrvmMakeString(g_runtimeEnv.shellPath);
	if (key == "TEMP_PATH") return mrvmMakeString(configuredTempDirectoryPath());
	if (key == "MR_PATH") return mrvmMakeString(g_runtimeEnv.executableDir);
	if (key == "OS_VERSION") return mrvmMakeString(g_runtimeEnv.shellVersion);
	if (key == "PARAM_COUNT") return mrvmMakeInt(static_cast<int>(g_runtimeEnv.processArgs.size()));
	if (key == "CPU") return mrvmMakeInt(mrvmDetectCpuCode());
	if (key == "DOC_MODE") return mrvmMakeInt(g_runtimeEnv.docMode);
	if (key == "PRINT_MARGIN") return mrvmMakeInt(g_runtimeEnv.printMargin);
	if (key == "C_COL") return mrvmMakeInt(currentEditorColumn(currentEditor()));
	if (key == "C_LINE") return mrvmMakeInt(currentEditorLineNumber(currentEditor()));
	if (key == "C_ROW") return mrvmMakeInt(currentEditorRow(currentEditor()));
	if (key == "C_PAGE") return mrvmMakeInt(currentEditorPage(currentEditor()));
	if (key == "PG_LINE") return mrvmMakeInt(currentEditorPageLine(currentEditor()));
	if (key == "AT_EOF") return mrvmMakeInt(currentEditorAtEof(currentEditor()) ? 1 : 0);
	if (key == "AT_EOL") return mrvmMakeInt(currentEditorAtEol(currentEditor()) ? 1 : 0);
	if (key == "CUR_WINDOW") return mrvmMakeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->currentWindow : currentEditWindowIndex());
	if (key == "LINK_STAT") return mrvmMakeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->linkStatus : currentLinkStatus());
	if (key == "WINDOW_COUNT") return mrvmMakeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->windowCount : countEditWindows());
	if (key == "KEY1") return mrvmMakeInt(g_runtimeEnv.key1);
	if (key == "KEY2") return mrvmMakeInt(g_runtimeEnv.key2);
	if (key == "LAST_FILE_ATTR" || key == "LAST_FILE_SIZE" || key == "LAST_FILE_TIME") {
		int attr = 0;
		int size = 0;
		int packedTime = 0;
		if (!mrvmReadFileMetadata(g_runtimeEnv.lastFileName, &attr, &size, &packedTime)) return mrvmMakeInt(0);
		if (key == "LAST_FILE_ATTR") return mrvmMakeInt(attr);
		if (key == "LAST_FILE_SIZE") return mrvmMakeInt(size);
		return mrvmMakeInt(packedTime);
	}
	if (key == "VIRTUAL_DESKTOPS") return mrvmMakeInt(configuredVirtualDesktops());
	if (key == "CYCLIC_VIRTUAL_DESKTOPS") return mrvmMakeInt(configuredCyclicVirtualDesktops() ? 1 : 0);
	if (key == "WIN_X1" || key == "WIN_Y1" || key == "WIN_X2" || key == "WIN_Y2") {
		BackgroundEditSession *session = currentBackgroundEditSession();
		int x1;
		int y1;
		int x2;
		int y2;
		if (session != nullptr) {
			if (!session->windowGeometryValid) return mrvmMakeInt(0);
			x1 = session->windowX1;
			y1 = session->windowY1;
			x2 = session->windowX2;
			y2 = session->windowY2;
		} else {
			if (!currentWindowGeometry(x1, y1, x2, y2)) return mrvmMakeInt(0);
		}
		if (key == "WIN_X1") return mrvmMakeInt(x1);
		if (key == "WIN_Y1") return mrvmMakeInt(y1);
		if (key == "WIN_X2") return mrvmMakeInt(x2);
		return mrvmMakeInt(y2);
	}
	if (key == "BLOCK_STAT") {
		MREditWindow *win = activeMacroEditWindow();
		return mrvmMakeInt(blockStatusValue(win));
	}
	if (key == "BLOCK_LINE1") {
		MREditWindow *win = activeMacroEditWindow();
		return mrvmMakeInt(blockLine1Value(win, currentEditor()));
	}
	if (key == "BLOCK_LINE2") {
		MREditWindow *win = activeMacroEditWindow();
		return mrvmMakeInt(blockLine2Value(win, currentEditor()));
	}
	if (key == "BLOCK_COL1") {
		MREditWindow *win = activeMacroEditWindow();
		return mrvmMakeInt(blockCol1Value(win, currentEditor()));
	}
	if (key == "BLOCK_COL2") {
		MREditWindow *win = activeMacroEditWindow();
		return mrvmMakeInt(blockCol2Value(win, currentEditor()));
	}
	if (key == "MARKING") {
		MREditWindow *win = activeMacroEditWindow();
		return mrvmMakeInt(blockMarkingValue(win) ? 1 : 0);
	}
	if (key == "LAST_FILE_NAME") return mrvmMakeString(g_runtimeEnv.lastFileName);
	if (key == "FOUND_STR" || key == "SEARCH_FILE" || key == "FOUND_X" || key == "FOUND_Y") {
		const SearchMatchSnapshot snapshot = currentSearchMatchSnapshot();
		if (!snapshot.valid) {
			if (key == "FOUND_STR" || key == "SEARCH_FILE") return mrvmMakeString("");
			return mrvmMakeInt(0);
		}
		if (key == "FOUND_STR") return mrvmMakeString(snapshot.foundText);
		if (key == "SEARCH_FILE") return mrvmMakeString(snapshot.fileName);
		if (key == "FOUND_X") return mrvmMakeInt(snapshot.foundX);
		return mrvmMakeInt(snapshot.foundY);
	}
	if (key == "GET_LINE") return mrvmMakeString(currentEditorLineText(currentEditor()));
	if (key == "FORMAT_LINE") return mrvmMakeString(configuredEditSetupSettings().formatLine);
	if (key == "DEFAULT_FORMAT") return mrvmMakeString(defaultFormatLineValue());
	if (key == "PAGE_STR") return mrvmMakeString(configuredEditSetupSettings().pageBreak);
	if (key == "WORD_DELIMITS") return mrvmMakeString(configuredEditSetupSettings().wordDelimiters);
	if (key == "CUR_CHAR") return currentEditorCharValue();
	if (key == "FIRST_SAVE" || key == "BUFFER_ID" || key == "TMP_FILE" || key == "TMP_FILE_NAME" || key == "FILE_CHANGED" || key == "FILE_NAME" || key == "CUR_FILE_ATTR" || key == "CUR_FILE_SIZE" || key == "READ_ONLY") return loadCurrentFileState(key);
	if (key == "FIRST_RUN") {
		if (!g_runtimeEnv.macroStack.empty()) return mrvmMakeInt(g_runtimeEnv.macroStack.back().firstRun ? 1 : 0);
		return mrvmMakeInt(0);
	}
	if (key == "FIRST_MACRO") {
		BackgroundEditSession *session = currentBackgroundEditSession();
		if (session != nullptr) {
			session->macroEnumIndex = 0;
			while (session->macroEnumIndex < session->macroOrder.size()) {
				const std::string &macroKey = session->macroOrder[session->macroEnumIndex++];
				std::map<std::string, std::string>::const_iterator it = session->loadedMacroDisplayNames.find(macroKey);
				if (it != session->loadedMacroDisplayNames.end()) return mrvmMakeString(it->second);
			}
		} else {
			const std::vector<std::string> orderValues = macroCatalogMacroOrder();
			std::size_t enumIndex = 0;
			setMacroCatalogMacroEnumIndex(enumIndex);
			while (enumIndex < orderValues.size()) {
				const std::string macroKey = orderValues[enumIndex++];
				MacroRef macroRef;
				setMacroCatalogMacroEnumIndex(enumIndex);
				if (readLoadedMacroByKey(macroKey, macroRef)) return mrvmMakeString(macroRef.displayName);
			}
		}
		return mrvmMakeString("");
	}
	if (key == "NEXT_MACRO") {
		BackgroundEditSession *session = currentBackgroundEditSession();
		if (session != nullptr) {
			while (session->macroEnumIndex < session->macroOrder.size()) {
				const std::string &macroKey = session->macroOrder[session->macroEnumIndex++];
				std::map<std::string, std::string>::const_iterator it = session->loadedMacroDisplayNames.find(macroKey);
				if (it != session->loadedMacroDisplayNames.end()) return mrvmMakeString(it->second);
			}
		} else {
			const std::vector<std::string> orderValues = macroCatalogMacroOrder();
			std::size_t enumIndex = macroCatalogMacroEnumIndex();
			while (enumIndex < orderValues.size()) {
				const std::string macroKey = orderValues[enumIndex++];
				MacroRef macroRef;
				setMacroCatalogMacroEnumIndex(enumIndex);
				if (readLoadedMacroByKey(macroKey, macroRef)) return mrvmMakeString(macroRef.displayName);
			}
		}
		return mrvmMakeString("");
	}
	handled = false;
	return mrvmMakeInt(0);
}

static bool storeSpecialVariable(const std::string &name, const Value &value) {
	std::string key = mrvmUpperKey(name);
	if (key == "RETURN_INT") {
		runtimeReturnInt() = mrvmValueAsInt(value);
		return true;
	}
	if (key == "RETURN_STR") {
		runtimeReturnStr() = mrvmValueAsString(value);
		mrvmEnforceStringLength(runtimeReturnStr());
		return true;
	}
	if (key == "ERROR_LEVEL") {
		runtimeErrorLevel() = mrvmValueAsInt(value);
		return true;
	}
	if (key == "IGNORE_CASE") {
		BackgroundEditSession *session = currentBackgroundEditSession();
		if (session != nullptr) session->ignoreCase = mrvmValueAsInt(value) != 0;
		else
			g_runtimeEnv.ignoreCase = mrvmValueAsInt(value) != 0;
		return true;
	}
	if (key == "REG_EXP_STAT") return setCurrentRegexStatus(mrvmValueAsInt(value) != 0);
	if (key == "TAB_EXPAND") {
		BackgroundEditSession *session = currentBackgroundEditSession();
		if (session != nullptr) session->tabExpand = mrvmValueAsInt(value) != 0;
		else
			g_runtimeEnv.tabExpand = mrvmValueAsInt(value) != 0;
		return true;
	}
	if (key == "SHADOW_CHAR") {
		g_runtimeEnv.shadowChar = std::clamp(mrvmValueAsInt(value), 0, 255);
		return true;
	}
	if (key == "REFRESH") {
		g_runtimeEnv.refresh = mrvmValueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "MESSAGES") return setConfiguredMenulineMessages(mrvmValueAsInt(value) != 0, nullptr);
	if (key == "MOUSE") {
		g_runtimeEnv.mouse = mrvmValueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "LOGO_SCREEN") {
		g_runtimeEnv.logoScreen = mrvmValueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "EXPLOSIONS") {
		g_runtimeEnv.explosions = mrvmValueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "TRUNCATE_SPACES") return applyConfiguredEditSetupValue("TRUNCATE_SPACES", mrvmValueAsInt(value) != 0 ? "TRUE" : "FALSE", nullptr);
	if (key == "BACKUPS") {
		MREditSetupSettings settings = configuredEditSetupSettings();
		switch (mrvmValueAsInt(value)) {
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
		if (mrvmValueAsInt(value) != 0) {
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
		g_runtimeEnv.undoStat = mrvmValueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "FORMAT_STAT") {
		g_runtimeEnv.formatStat = mrvmValueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "WRAP_STAT") return applyConfiguredEditSetupValue("WORD_WRAP", mrvmValueAsInt(value) != 0 ? "TRUE" : "FALSE", nullptr);
	if (key == "MEM_ALLOC") {
		g_runtimeEnv.memAlloc = std::max(0, mrvmValueAsInt(value));
		return true;
	}
	if (key == "LEFT_MARGIN") return applyConfiguredEditSetupValue("LEFT_MARGIN", std::to_string(mrvmValueAsInt(value)), nullptr);
	if (key == "RIGHT_MARGIN") return applyConfiguredEditSetupValue("RIGHT_MARGIN", std::to_string(mrvmValueAsInt(value)), nullptr);
	if (key == "FORMAT_RULER") return applyConfiguredEditSetupValue("FORMAT_RULER", mrvmValueAsInt(value) != 0 ? "TRUE" : "FALSE", nullptr);
	if (key == "INDENT_STYLE") return applyConfiguredEditSetupValue("INDENT_STYLE", decodeIndentStyle(mrvmValueAsInt(value)), nullptr);
	if (key == "INS_CURSOR") {
		g_runtimeEnv.insCursor = std::clamp(mrvmValueAsInt(value), 0, 3);
		return true;
	}
	if (key == "OVR_CURSOR") {
		g_runtimeEnv.ovrCursor = std::clamp(mrvmValueAsInt(value), 0, 3);
		return true;
	}
	if (key == "CTRL_HELP") {
		g_runtimeEnv.ctrlHelp = mrvmValueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "MOUSE_H_SENSE") {
		g_runtimeEnv.mouseHSense = std::max(0, mrvmValueAsInt(value));
		return true;
	}
	if (key == "MOUSE_V_SENSE") {
		g_runtimeEnv.mouseVSense = std::max(0, mrvmValueAsInt(value));
		return true;
	}
	if (key == "WINDOW_ATTR") return setCurrentWindowAttrValue(mrvmValueAsInt(value));
	if (key == "TEXT_COLOR") return writeWindowColorValue(0, mrvmValueAsInt(value));
	if (key == "CHANGE_COLOR") return writeWindowColorValue(1, mrvmValueAsInt(value));
	if (key == "BACK_COLOR") return writeOtherColorValue(9, mrvmValueAsInt(value));
	if (key == "MENU_COLOR") return writeMenuDialogColorValue(0, mrvmValueAsInt(value));
	if (key == "STAT_COLOR") return writeOtherColorValue(0, mrvmValueAsInt(value));
	if (key == "ERROR_COLOR") return writeOtherColorValue(4, mrvmValueAsInt(value));
	if (key == "SHADOW_COLOR") return writeMenuDialogColorValue(7, mrvmValueAsInt(value));
	if (key == "STATUS_ROW") {
		g_runtimeEnv.statusRow = std::max(0, mrvmValueAsInt(value));
		return true;
	}
	if (key == "MESSAGE_ROW") {
		g_runtimeEnv.messageRow = std::max(0, mrvmValueAsInt(value));
		return true;
	}
	if (key == "MAX_WINDOW_ROW") {
		g_runtimeEnv.maxWindowRow = std::max(0, mrvmValueAsInt(value));
		return true;
	}
	if (key == "MIN_WINDOW_ROW") {
		g_runtimeEnv.minWindowRow = std::max(0, mrvmValueAsInt(value));
		return true;
	}
	if (key == "NAME_LINE") {
		g_runtimeEnv.nameLine = mrvmValueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "INSERT_MODE") return setCurrentEditorInsertMode(mrvmValueAsInt(value) != 0);
	if (key == "INDENT_LEVEL") return setCurrentEditorIndentLevel(mrvmValueAsInt(value));
	if (key == "MPARM_STR") {
		runtimeParameterString() = mrvmValueAsString(value);
		mrvmEnforceStringLength(runtimeParameterString());
		return true;
	}
	if (key == "DOC_MODE") {
		g_runtimeEnv.docMode = mrvmValueAsInt(value) != 0 ? 1 : 0;
		return true;
	}
	if (key == "PRINT_MARGIN") {
		g_runtimeEnv.printMargin = std::max(0, mrvmValueAsInt(value));
		return true;
	}
	if (key == "FORMAT_LINE") return applyConfiguredEditSetupValue("FORMAT_LINE", mrvmValueAsString(value), nullptr);
	if (key == "DEFAULT_FORMAT") {
		g_runtimeEnv.defaultFormat = mrvmValueAsString(value);
		mrvmEnforceStringLength(g_runtimeEnv.defaultFormat);
		return true;
	}
	if (key == "PAGE_STR") return applyConfiguredEditSetupValue("PAGE_BREAK", mrvmValueAsString(value), nullptr);
	if (key == "WORD_DELIMITS") return applyConfiguredEditSetupValue("WORD_DELIMITERS", mrvmValueAsString(value), nullptr);
	if (key == "FILE_CHANGED") {
		MREditWindow *win = activeMacroEditWindow();
		BackgroundEditSession *session = currentBackgroundEditSession();
		if (win != nullptr) win->setFileChanged(mrvmValueAsInt(value) != 0);
		else if (session != nullptr)
			session->fileChanged = mrvmValueAsInt(value) != 0;
		else
			return false;
		return true;
	}
	if (key == "FILE_NAME") {
		MREditWindow *win = activeMacroEditWindow();
		BackgroundEditSession *session = currentBackgroundEditSession();
		if (win != nullptr) win->setCurrentFileName(mrvmValueAsString(value).c_str());
		else if (session != nullptr)
			session->fileName = mrvmValueAsString(value);
		else
			return false;
		return true;
	}
	if (key == "VIRTUAL_DESKTOPS") {
		if (currentBackgroundEditSession() != nullptr) throw std::runtime_error("VIRTUAL_DESKTOPS cannot be changed in background mode.");
		applyVirtualDesktopConfigurationChange(mrvmValueAsInt(value));
		return true;
	}
	if (key == "CYCLIC_VIRTUAL_DESKTOPS") {
		if (currentBackgroundEditSession() != nullptr) throw std::runtime_error("CYCLIC_VIRTUAL_DESKTOPS cannot be changed in background mode.");
		setConfiguredCyclicVirtualDesktops(mrvmValueAsInt(value) != 0, nullptr);
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

static std::vector<int> currentGlobalHashRoots() {
	return std::vector<int>();
}

static Value ensureGlobalHashRoot(const std::string &name) {
	return g_runtimeEnv.runtimeKv.ensureRoot(name);
}

static Value ensureGlobalHashChild(const Value &parent, const std::string &key) {
	return g_runtimeEnv.runtimeKv.ensureChild(parent, key);
}

static Value replaceGlobalHashChild(const Value &parent, const std::string &key) {
	return g_runtimeEnv.runtimeKv.replaceChild(parent, key);
}

static bool readRuntimeGlobalValueDirect(const std::string &name, GlobalEntry &entry) {
	return mrvmRuntimeGlobalRead(g_runtimeEnv.runtimeKv, name, entry);
}

static std::string runtimeGlobalStringValue(const std::string &name) {
	GlobalEntry entry;

	if (!readRuntimeGlobalValueDirect(name, entry) || entry.type != TYPE_STR) return std::string();
	return mrvmValueAsString(entry.value);
}

static int runtimeGlobalIntValue(const std::string &name) {
	GlobalEntry entry;

	if (!readRuntimeGlobalValueDirect(name, entry) || entry.type != TYPE_INT) return 0;
	return mrvmValueAsInt(entry.value);
}

static std::vector<std::string> macroGlobalOrderValues() {
	return mrvmRuntimeGlobalOrderValues(g_runtimeEnv.runtimeKv);
}

static std::size_t macroGlobalEnumIndex() {
	return mrvmRuntimeGlobalEnumIndex(g_runtimeEnv.runtimeKv);
}

static void setMacroGlobalEnumIndex(std::size_t index) {
	mrvmRuntimeGlobalSetEnumIndex(g_runtimeEnv.runtimeKv, index);
}

static void writeRuntimeGlobalValueDirect(const std::string &name, int type, const Value &value) {
	mrvmRuntimeGlobalWrite(g_runtimeEnv.runtimeKv, name, type, value);
}

static bool isMacroVisibleRuntimeRootName(const std::string &key) {
	return key == "EXECSESSIONS" || key == "MODELESSUI" || key == "MACROCATALOG" || key == "MACROGLOBALS" || key == "MACROSNIPPETS";
}

static void setSessionGlobalValueDirect(BackgroundEditSession &session, const std::string &name, int type, const Value &value) {
	std::string key = mrvmUpperKey(name);
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
	writeRuntimeGlobalValueDirect(name, type, value);
}

static void setGlobalValueFromStore(const std::string &name, int type, const Value &value, MRVMHashStore &localStore) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	Value stored = value;

	if (type == TYPE_HASH || mrvmValueIsArrayType(type)) stored = mrvmHashCopyValueForStore(value, localStore, g_runtimeEnv.runtimeKv.globalStore(), g_runtimeEnv.runtimeKv.globalStore(), true);
	else
		stored.globalStorage = true;
	if (session != nullptr && type != TYPE_HASH && !mrvmValueIsArrayType(type)) {
		setSessionGlobalValueDirect(*session, name, type, stored);
		return;
	}
	writeRuntimeGlobalValueDirect(name, type, stored);
}

static bool readGlobalValue(const std::string &name, GlobalEntry &entry) {
	const std::string key = mrvmUpperKey(name);
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (session != nullptr) {
		std::map<std::string, GlobalEntry>::const_iterator sessionIt = session->globals.find(key);
		if (sessionIt != session->globals.end()) {
			entry = sessionIt->second;
			return true;
		}
	}
	if (readRuntimeGlobalValueDirect(key, entry)) return true;
	if (isMacroVisibleRuntimeRootName(key)) {
		Value root = ensureGlobalHashRoot(key);
		entry.type = TYPE_HASH;
		entry.value = root;
		return true;
	}
	return false;
}

static void hashWriteInt(const Value &hash, const std::string &key, int value) {
	mrvmHashWriteValue(g_runtimeEnv.runtimeKv.globalStore(), g_runtimeEnv.runtimeKv.globalStore(), hash, key, mrvmMakeInt(value));
}

static Value ensureExecSessionsChildPath(std::initializer_list<const char *> keys) {
	return mrvmExecSessionsEnsureChildPath(g_runtimeEnv.runtimeKv, keys);
}

static bool findExecSessionsChildPath(std::initializer_list<const char *> keys, Value &child) {
	return mrvmExecSessionsFindChildPath(g_runtimeEnv.runtimeKv, keys, child);
}

static bool readLoadedMacroFileByKey(const std::string &fileKey, LoadedMacroFile &file) {
	return mrvmRuntimeCatalogReadLoadedFile(g_runtimeEnv.runtimeKv, fileKey, file);
}

static bool loadedMacroFileExists(const std::string &fileKey) {
	return mrvmRuntimeCatalogLoadedFileExists(g_runtimeEnv.runtimeKv, fileKey);
}

static void writeLoadedMacroFileByKey(const LoadedMacroFile &file) {
	mrvmRuntimeCatalogWriteLoadedFile(g_runtimeEnv.runtimeKv, file);
}

static bool eraseLoadedMacroFileByKey(const std::string &fileKey) {
	return mrvmRuntimeCatalogEraseLoadedFile(g_runtimeEnv.runtimeKv, fileKey);
}

static std::vector<std::string> loadedMacroFileKeys() {
	return mrvmRuntimeCatalogLoadedFileKeys(g_runtimeEnv.runtimeKv);
}

static bool readLoadedMacroByKey(const std::string &macroKey, MacroRef &macroRef) {
	return mrvmRuntimeCatalogReadLoadedMacro(g_runtimeEnv.runtimeKv, macroKey, macroRef);
}

static bool loadedMacroExists(const std::string &macroKey) {
	return mrvmRuntimeCatalogLoadedMacroExists(g_runtimeEnv.runtimeKv, macroKey);
}

static void writeLoadedMacroByKey(const std::string &macroKey, const MacroRef &macroRef) {
	mrvmRuntimeCatalogWriteLoadedMacro(g_runtimeEnv.runtimeKv, macroKey, macroRef);
}

static bool eraseLoadedMacroByKey(const std::string &macroKey) {
	return mrvmRuntimeCatalogEraseLoadedMacro(g_runtimeEnv.runtimeKv, macroKey);
}

static std::vector<std::string> macroCatalogMacroOrder() {
	return mrvmRuntimeCatalogMacroOrder(g_runtimeEnv.runtimeKv);
}

static void appendMacroCatalogMacroOrder(const std::string &macroKey) {
	mrvmRuntimeCatalogAppendMacroOrder(g_runtimeEnv.runtimeKv, macroKey);
}

static void removeMacroCatalogMacroOrder(const std::string &macroKey) {
	mrvmRuntimeCatalogRemoveMacroOrder(g_runtimeEnv.runtimeKv, macroKey);
}

static std::size_t macroCatalogMacroEnumIndex() {
	return mrvmRuntimeCatalogMacroEnumIndex(g_runtimeEnv.runtimeKv);
}

static void setMacroCatalogMacroEnumIndex(std::size_t index) {
	mrvmRuntimeCatalogSetMacroEnumIndex(g_runtimeEnv.runtimeKv, index);
}

static std::size_t macroCatalogLoadedMacroCount() {
	return mrvmRuntimeCatalogLoadedMacroCount(g_runtimeEnv.runtimeKv);
}

static std::vector<IndexedBoundMacroEntry> macroCatalogIndexedBindings() {
	return mrvmRuntimeCatalogIndexedBindings(g_runtimeEnv.runtimeKv);
}

static void writeMacroCatalogIndexedBindings(const std::vector<IndexedBoundMacroEntry> &bindings) {
	mrvmRuntimeCatalogWriteIndexedBindings(g_runtimeEnv.runtimeKv, bindings);
}

static bool markMacroCatalogIndexedWarmupAttempted(const std::string &fileKey) {
	return mrvmRuntimeCatalogMarkIndexedWarmupAttempted(g_runtimeEnv.runtimeKv, fileKey);
}

static std::size_t macroCatalogIndexedBindingCount() {
	return mrvmRuntimeCatalogIndexedBindingCount(g_runtimeEnv.runtimeKv);
}

static std::string loadedFileBasenameKey(const LoadedMacroFile &file) {
	std::string source = !file.resolvedPath.empty() ? file.resolvedPath : file.displayName;

	if (source.empty()) source = file.fileKey;
	return mrvmMakeMacroFileKey(mrvmTruncatePathPart(source));
}

static std::string resolveLoadedFileKeyForSpec(const std::string &fileSpec) {
	const std::string exactKey = mrvmMakeMacroFileKey(fileSpec);
	std::string matchedKey;
	const std::vector<std::string> fileKeys = loadedMacroFileKeys();

	if (fileSpec.empty()) return std::string();
	if (loadedMacroFileExists(exactKey)) return exactKey;
	for (const std::string &fileKey : fileKeys) {
		LoadedMacroFile file;
		if (!readLoadedMacroFileByKey(fileKey, file)) continue;
		if (loadedFileBasenameKey(file) != exactKey) continue;
		if (!matchedKey.empty() && matchedKey != fileKey) return std::string();
		matchedKey = fileKey;
	}
	return matchedKey;
}

static bool fileSpecMatchesLoadedFileKey(const std::string &fileSpec, const std::string &targetFileKey) {
	const std::string resolvedKey = resolveLoadedFileKeyForSpec(fileSpec);

	if (targetFileKey.empty()) return fileSpec.empty();
	if (!resolvedKey.empty()) return resolvedKey == targetFileKey;
	return mrvmMakeMacroFileKey(fileSpec) == targetFileKey;
}

static bool macroIsRunning(const std::string &macroKey) {
	for (auto &i : g_runtimeEnv.macroStack)
		if (mrvmUpperKey(i.macroName) == macroKey) return true;
	return false;
}

static bool removeMacroFromRegistryByKey(const std::string &macroKey) {
	MacroRef macroRef;
	LoadedMacroFile file;
	std::string ownerSpec;

	if (!readLoadedMacroByKey(macroKey, macroRef)) return false;
	if (macroRef.scheduledConsumerId != 0) {
		removeRuntimeScheduledConsumer(macroRef.scheduledConsumerId);
		macroRef.scheduledConsumerId = 0;
	}
	static_cast<void>(composeLoadedMacroSpec(macroRef, ownerSpec));
	if (!ownerSpec.empty()) static_cast<void>(mrvmUiRemoveRuntimeMenusOwnedByMacroSpec(ownerSpec));

	const std::string fileKey = macroRef.fileKey;
	static_cast<void>(eraseLoadedMacroByKey(macroKey));
	removeMacroCatalogMacroOrder(macroKey);

	if (readLoadedMacroFileByKey(fileKey, file)) {
		file.macroNames.erase(std::remove(file.macroNames.begin(), file.macroNames.end(), macroKey), file.macroNames.end());
		if (file.macroNames.empty()) static_cast<void>(eraseLoadedMacroFileByKey(fileKey));
		else
			writeLoadedMacroFileByKey(file);
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
	g_runtimeEnv.pushedKeys.pop_front();
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

static std::vector<std::string> resolveMacroUiListItems(const std::string &itemSpec) {
	const std::string key = mrvmModelessUiListKey(itemSpec);
	std::vector<std::string> values;

	if (mrvmModelessUiReadItemList(g_runtimeEnv.runtimeKv, key, values)) return values;
	return mrvmResolveMacroUiListItems(g_runtimeEnv.runtimeKv, itemSpec);
}

static void runMacroModelessCommand(const std::string &, int, const MRMacroModelessSelection &selection, const std::string &macroSpec) {
	if (selection.controlId != 0) {
		mrvmModelessUiWriteIndexValue(g_runtimeEnv.runtimeKv, selection.controlId, selection.index);
		mrvmModelessUiWriteTextValue(g_runtimeEnv.runtimeKv, selection.controlId, selection.text);
	}
	if (!macroSpec.empty()) {
		std::string errorText;
		if (!mrvmRunMacroSpec(macroSpec, &errorText)) {
			runtimeErrorLevel() = 1001;
			if (!errorText.empty()) static_cast<void>(mrvmUiMarquee(2, errorText));
		}
	}
}

static void showMacroModelessDialog(const std::vector<Value> &args) {
	const std::string windowId = mrvmModelessUiListKey(mrvmValueAsString(args[0]));

	if (windowId.empty()) throw std::runtime_error("UI_MODELESS_SHOW expects a non-empty window id.");
	setMacroModelessListResolver(resolveMacroUiListItems);
	setMacroModelessCommandRunner(runMacroModelessCommand);
	runtimeReturnInt() = showMacroModelessWindow(mrvmBuildMacroModelessDefinition(g_runtimeEnv.runtimeKv, windowId)) ? 1 : 0;
	runtimeErrorLevel() = runtimeReturnInt() == 1 ? 0 : 1001;
}

static void updateMacroModelessDialog(const std::vector<Value> &args) {
	const std::string windowId = mrvmModelessUiListKey(mrvmValueAsString(args[0]));

	if (windowId.empty()) throw std::runtime_error("UI_MODELESS_UPDATE expects a non-empty window id.");
	setMacroModelessListResolver(resolveMacroUiListItems);
	setMacroModelessCommandRunner(runMacroModelessCommand);
	runtimeReturnInt() = updateMacroModelessWindow(mrvmBuildMacroModelessDefinition(g_runtimeEnv.runtimeKv, windowId)) ? 1 : 0;
	runtimeErrorLevel() = runtimeReturnInt() == 1 ? 0 : 1001;
}

static void updateMacroModelessDisplayLine(const std::vector<Value> &args) {
	const std::string windowId = mrvmModelessUiListKey(mrvmValueAsString(args[0]));
	const int displayIndex = mrvmValueAsInt(args[1]);

	if (windowId.empty()) throw std::runtime_error("UI_MODELESS_DISPLAY expects a non-empty window id.");
	if (displayIndex <= 0) throw std::runtime_error("UI_MODELESS_DISPLAY expects a positive display index.");
	runtimeReturnInt() = updateMacroModelessDisplay(windowId, displayIndex, mrvmValueAsString(args[2])) ? 1 : 0;
	runtimeErrorLevel() = runtimeReturnInt() == 1 ? 0 : 1001;
}

static void closeMacroModelessDialog(const std::vector<Value> &args) {
	const std::string windowId = mrvmModelessUiListKey(mrvmValueAsString(args[0]));

	if (windowId.empty()) throw std::runtime_error("UI_MODELESS_CLOSE expects a non-empty window id.");
	runtimeReturnInt() = closeMacroModelessWindow(windowId) ? 1 : 0;
	runtimeErrorLevel() = runtimeReturnInt() == 1 ? 0 : 1001;
}

static void listExecSessionClosures(const std::vector<Value> &args) {
	const std::string key = mrvmModelessUiListKey(mrvmValueAsString(args[0]));
	const std::vector<MRRuntimeScheduledConsumer> consumers = runtimeScheduledConsumers();
	Value consoleRoot;
	Value consoleList;
	Value ids;
	int row = 0;

	if (currentBackgroundEditSession() != nullptr) throw std::runtime_error("EXEC_SESSION_LIST is not available in background mode.");
	if (key.empty()) throw std::runtime_error("EXEC_SESSION_LIST expects a non-empty list name.");

	mrvmModelessUiClearItemList(g_runtimeEnv.runtimeKv, key);
	if (!findExecSessionsChildPath({"console"}, consoleRoot)) consoleRoot = ensureExecSessionsChildPath({"console"});
	consoleList = replaceGlobalHashChild(consoleRoot, key);
	ids = ensureGlobalHashChild(consoleList, "ids");
	for (std::size_t index = 0; index < consumers.size(); ++index) {
		const MRRuntimeScheduledConsumer &consumer = consumers[index];
		if (consumer.config.closureId.empty()) continue;
		if (consumer.consumerId > static_cast<MRRuntimeScheduledConsumerId>(std::numeric_limits<int>::max())) continue;
		++row;
		hashWriteInt(ids, std::to_string(row), static_cast<int>(consumer.consumerId));

		std::string line = std::to_string(row);
		line += "  #";
		line += std::to_string(consumer.consumerId);
		line += "  ";
		line += consumer.activeSessionId != 0 ? "running " : "waiting ";
		line += "tick=";
		line += std::to_string(consumer.config.intervalMs);
		line += "  ";
		line += consumer.config.entryName.empty() ? consumer.config.closureId : consumer.config.entryName;
		mrvmModelessUiAddItemListValue(g_runtimeEnv.runtimeKv, key, line);
	}
	hashWriteInt(consoleList, "count", row);
	runtimeReturnInt() = row;
	runtimeErrorLevel() = 0;
}

static void stopExecSessionClosure(const std::vector<Value> &args) {
	const int requestedId = mrvmValueAsInt(args[0]);
	const std::vector<MRRuntimeScheduledConsumer> consumers = runtimeScheduledConsumers();
	std::string closureId;
	bool removed = false;

	if (currentBackgroundEditSession() != nullptr) throw std::runtime_error("EXEC_SESSION_STOP is not available in background mode.");
	if (requestedId > 0) {
		for (std::size_t index = 0; index < consumers.size(); ++index) {
			const MRRuntimeScheduledConsumer &consumer = consumers[index];
			if (consumer.consumerId != static_cast<MRRuntimeScheduledConsumerId>(requestedId)) continue;
			closureId = consumer.config.closureId;
			break;
		}
		removed = removeRuntimeScheduledConsumer(static_cast<MRRuntimeScheduledConsumerId>(requestedId));
	}
	if (removed && !closureId.empty()) static_cast<void>(mrvmExecSessionsEraseClosureState(g_runtimeEnv.runtimeKv, closureId));
	runtimeReturnInt() = removed ? 1 : 0;
	runtimeErrorLevel() = removed ? 0 : 1001;
}

static int currentUiMacroMode() {
	MREditWindow *win = activeMacroEditWindow();
	if (win != nullptr && win->isCommunicationWindow()) return MACRO_MODE_DOS_SHELL;
	return MACRO_MODE_EDIT;
}

static bool macroAllowsUiMode(const MacroRef &macroRef, int mode) noexcept {
	return macroRef.fromMode == MACRO_MODE_ALL || macroRef.fromMode == mode;
}

static bool executeLoadedMacro(const std::string &macroKey, const std::string &paramPart, std::vector<std::string> *logSink) {
	MacroRef macroRef;
	LoadedMacroFile file;
	VirtualMachine childVm;
	bool backgroundStaged = currentBackgroundEditSession() != nullptr;
	bool childFirstRun;
	bool childDump;
	bool childTransient;
	std::string childFileKey;

	if (!readLoadedMacroByKey(macroKey, macroRef)) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	if (!readLoadedMacroFileByKey(macroRef.fileKey, file)) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	if (backgroundStaged) {
		if (file.bytecode.empty() || !currentBackgroundChildMacroAllowed(file)) {
			runtimeErrorLevel() = 5001;
			return false;
		}
	} else if (!ensureLoadedFileResident(macroRef.fileKey))
		return false;

	if (!readLoadedMacroFileByKey(macroRef.fileKey, file) || file.bytecode.empty()) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	childFirstRun = macroRef.firstRunPending;
	childDump = macroRef.dumpAttr;
	childTransient = macroRef.transientAttr;
	childFileKey = macroRef.fileKey;
	macroRef.firstRunPending = false;
	writeLoadedMacroByKey(macroKey, macroRef);

	childVm.setExecutionSessionContext(currentExecutionSessionId());
	if (macroRef.closureUnit) childVm.setClosureContext(macroRef.closureId);
	childVm.executeAt(file.bytecode.data(), file.bytecode.size(), macroRef.entryOffset, paramPart, macroRef.displayName, false, childFirstRun);
	if (logSink != nullptr) logSink->insert(logSink->end(), childVm.log.begin(), childVm.log.end());
	if (childDump) unloadMacroFromRegistry(macroKey);
	else if (childTransient)
		evictTransientFileImage(childFileKey);
	runtimeErrorLevel() = 0;
	return true;
}

static bool executeLoadedMacroWithConfiguredKeymapBatch(const std::string &macroKey, const std::string &paramPart, std::vector<std::string> *logSink) {
	MacroRef macroRef;
	LoadedMacroFile file;
	bool configuredKeymapBatch = false;
	std::string keymapBatchError;

	if (readLoadedMacroByKey(macroKey, macroRef) && readLoadedMacroFileByKey(macroRef.fileKey, file)) {
		const std::vector<std::string> unsupported = mrvmUnsupportedStagedSymbols(file.profile);
		for (const std::string &symbol : unsupported)
			if (symbol == "KEYMAP_RESET" || symbol == "KEYMAP_PROFILE" || symbol == "KEYMAP_BIND" || symbol == "ACTIVE_KEYMAP_PROFILE") {
				configuredKeymapBatch = true;
				break;
			}
	}
	if (configuredKeymapBatch) mrvmBeginConfiguredKeymapBatch();
	const bool executed = executeLoadedMacro(macroKey, paramPart, logSink);
	if (configuredKeymapBatch && !mrvmEndConfiguredKeymapBatch(&keymapBatchError)) {
		runtimeErrorLevel() = 1001;
		if (logSink != nullptr) logSink->push_back("VM Error: keymap batch flush failed: " + (keymapBatchError.empty() ? std::string("invalid keymap batch.") : keymapBatchError));
		return false;
	}
	return executed;
}

static void clearRegisteredBindingsForKey(const TKey *key, int mode, bool clearAllModes) {
	const std::vector<std::string> orderValues = macroCatalogMacroOrder();
	for (const std::string &macroKey : orderValues) {
		MacroRef macroRef;
		if (!readLoadedMacroByKey(macroKey, macroRef)) continue;
		if (!macroRef.hasAssignedKey) continue;
		if (!clearAllModes && macroRef.fromMode != mode) continue;
		if (key != nullptr && !mrvmBindingKeysEqual(macroRef.assignedKey, *key)) continue;
		macroRef.hasAssignedKey = false;
		macroRef.assignedKeySpec.clear();
		writeLoadedMacroByKey(macroKey, macroRef);
	}
	{
		std::vector<IndexedBoundMacroEntry> indexed = macroCatalogIndexedBindings();
		indexed.erase(std::remove_if(indexed.begin(), indexed.end(),
		                             [&](const IndexedBoundMacroEntry &entry) {
			                             if (key != nullptr && !mrvmBindingKeysEqual(entry.key, *key)) return false;
			                             return clearAllModes || mode == MACRO_MODE_ALL || mode == MACRO_MODE_EDIT || mode == MACRO_MODE_DOS_SHELL;
		                             }),
		              indexed.end());
		writeMacroCatalogIndexedBindings(indexed);
	}
}

static bool executeRuntimeMacroSpec(const std::string &spec, std::vector<std::string> *logLines) {
	std::string filePart;
	std::string macroPart;
	std::string paramPart;
	std::string targetFileKey;
	std::string macroKey;
	MacroRef macroRef;

	if (!mrvmParseRunMacroSpec(spec, filePart, macroPart, paramPart)) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	macroKey = mrvmUpperKey(macroPart);
	if (!filePart.empty()) targetFileKey = resolveLoadedFileKeyForSpec(filePart);
	if (!filePart.empty() && targetFileKey.empty()) targetFileKey = mrvmMakeMacroFileKey(filePart);

	if (!readLoadedMacroByKey(macroKey, macroRef) || (!targetFileKey.empty() && macroRef.fileKey != targetFileKey)) {
		if (!filePart.empty()) {
			if (!loadMacroFileIntoRegistry(filePart, &targetFileKey)) return false;
		} else {
			if (!loadMacroFileIntoRegistry(macroPart, &targetFileKey)) return false;
		}
		static_cast<void>(readLoadedMacroByKey(macroKey, macroRef));
	}

	if (macroRef.displayName.empty() || (!targetFileKey.empty() && macroRef.fileKey != targetFileKey)) {
		runtimeErrorLevel() = 5001;
		return false;
	}
	return executeLoadedMacroWithConfiguredKeymapBatch(macroKey, paramPart, logLines);
}

static bool currentExecutingMacroSpecFromRuntimeStack(std::string &macroSpec) {
	const std::string macroDisplayName = !g_runtimeEnv.macroStack.empty() ? trimAscii(g_runtimeEnv.macroStack.back().macroName) : std::string();
	MacroRef macroRef;
	LoadedMacroFile file;
	std::string fileDisplayName;

	macroSpec.clear();
	if (macroDisplayName.empty() || !readLoadedMacroByKey(mrvmUpperKey(macroDisplayName), macroRef)) return false;
	if (!readLoadedMacroFileByKey(macroRef.fileKey, file)) return false;
	fileDisplayName = !file.displayName.empty() ? file.displayName : file.resolvedPath;
	if (fileDisplayName.empty()) return false;
	macroSpec = fileDisplayName + "^" + macroRef.displayName;
	return true;
}


static bool composeLoadedMacroSpec(const MacroRef &macroRef, std::string &macroSpec) {
	LoadedMacroFile file;
	std::string fileDisplayName;

	macroSpec.clear();
	if (!readLoadedMacroFileByKey(macroRef.fileKey, file)) return false;
	fileDisplayName = !file.displayName.empty() ? file.displayName : file.resolvedPath;
	if (fileDisplayName.empty() || macroRef.displayName.empty()) return false;
	macroSpec = fileDisplayName + "^" + macroRef.displayName;
	return true;
}

static bool macroSpecTargetsLoadedMacro(const std::string &spec, const std::string &targetFileKey, const std::string &targetMacroKey) {
	std::string filePart;
	std::string macroPart;
	std::string paramPart;
	const bool parsed = mrvmParseRunMacroSpec(spec, filePart, macroPart, paramPart);

	if (!parsed || mrvmUpperKey(macroPart) != targetMacroKey) return false;
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
	mrvmLogCalculatorHotkeyState("vm-explicit-enter", pressed);
	for (std::size_t i = g_runtimeEnv.explicitKeyBindings.size(); i > 0; --i) {
		const MRVMExplicitKeyBinding &binding = g_runtimeEnv.explicitKeyBindings[i - 1];
		if (!mrvmBindingKeysEqual(binding.key, pressed) || !mrvmBindingModeMatches(binding.mode, mode)) continue;
		if (binding.kind == MRVMExplicitBindingKind::MacroSpec) mrvmLogCalculatorHotkeyState("vm-explicit-match", pressed, binding.macroSpec);
		else
			mrvmLogCalculatorHotkeyState("vm-explicit-match-cmd", pressed);
		if (binding.kind == MRVMExplicitBindingKind::MacroSpec) return executeRuntimeMacroSpec(binding.macroSpec, logLines);
		runtimeErrorLevel() = executeBoundCommand(binding.commandId) ? 0 : 1001;
		return runtimeErrorLevel() == 0;
	}
	return false;
}

static bool fileContainsOnlyTransientMacros(const LoadedMacroFile &file) {
	if (file.macroNames.empty()) return false;
	for (const auto &macroName : file.macroNames) {
		MacroRef macroRef;
		if (!readLoadedMacroByKey(macroName, macroRef) || !macroRef.transientAttr) return false;
	}
	return true;
}

static bool refreshLoadedFileBytecode(const std::string &fileKey) {
	LoadedMacroFile file;
	std::string source;
	unsigned char *compiled = nullptr;
	size_t compiledSize = 0;
	int macroCount;

	if (!readLoadedMacroFileByKey(fileKey, file)) return false;
	if (file.resolvedPath.empty() || !readTextFile(file.resolvedPath, source)) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	compiled = compile_macro_code(source.c_str(), &compiledSize);
	if (compiled == nullptr) {
		runtimeErrorLevel() = 5005;
		return false;
	}

	file.bytecode.assign(compiled, compiled + compiledSize);
	std::free(compiled);
	file.profile = mrvmAnalyzeBytecode(file.bytecode.data(), file.bytecode.size());

	macroCount = get_compiled_macro_count();
	for (int i = 0; i < macroCount; ++i) {
		const char *macroNameText = get_compiled_macro_name(i);
		int entry = get_compiled_macro_entry(i);
		int flags = get_compiled_macro_flags(i);
		const char *keyspecText = get_compiled_macro_keyspec(i);
		int mode = get_compiled_macro_mode(i);
		int unitKind = get_compiled_macro_unit_kind(i);
		int tickMs = get_compiled_macro_tick_ms(i);
		std::string displayName = macroNameText != nullptr ? macroNameText : std::string();
		std::string macroKey = mrvmUpperKey(displayName);
		MacroRef macroRef;

		if (displayName.empty() || entry < 0) continue;
		if (!readLoadedMacroByKey(macroKey, macroRef) || macroRef.fileKey != fileKey) continue;

		macroRef.displayName = displayName;
		macroRef.entryOffset = static_cast<std::size_t>(entry);
		macroRef.transientAttr = (flags & MACRO_ATTR_TRANS) != 0;
		macroRef.dumpAttr = (flags & MACRO_ATTR_DUMP) != 0;
		macroRef.permAttr = (flags & MACRO_ATTR_PERM) != 0;
		macroRef.assignedKeySpec = keyspecText != nullptr ? keyspecText : std::string();
		macroRef.fromMode = (mode == MACRO_MODE_DOS_SHELL || mode == MACRO_MODE_ALL) ? mode : MACRO_MODE_EDIT;
		macroRef.closureUnit = unitKind == MRMAC_UNIT_CLOSURE;
		macroRef.tickMs = tickMs > 0 ? static_cast<std::uint64_t>(tickMs) : 0;
		macroRef.closureId.clear();
		if (macroRef.scheduledConsumerId != 0) {
			removeRuntimeScheduledConsumer(macroRef.scheduledConsumerId);
			macroRef.scheduledConsumerId = 0;
		}
		if (macroRef.closureUnit && macroRef.tickMs != 0) {
			MRRuntimeScheduledConsumerConfig config;
			const std::string macroSpec = file.displayName + "^" + displayName;
			config.intervalMs = macroRef.tickMs;
			config.macroSpec = macroSpec;
			config.entryName = displayName;
			config.closureId = macroSpec;
			macroRef.closureId = macroSpec;
			mrvmExecSessionsEnsureClosureState(g_runtimeEnv.runtimeKv, config.closureId, static_cast<int>(macroRef.tickMs));
			macroRef.scheduledConsumerId = registerRuntimeScheduledConsumer(config);
		}
		macroRef.hasAssignedKey = false;
		if (!macroRef.assignedKeySpec.empty()) macroRef.hasAssignedKey = mrvmParseAssignedKeySpec(macroRef.assignedKeySpec, macroRef.assignedKey);
		writeLoadedMacroByKey(macroKey, macroRef);
	}

	writeLoadedMacroFileByKey(file);
	runtimeErrorLevel() = 0;
	logMacroProfileLine("Refreshed macro file", file);
	return true;
}

static bool ensureLoadedFileResident(const std::string &fileKey) {
	LoadedMacroFile file;
	if (!readLoadedMacroFileByKey(fileKey, file)) return false;
	if (!file.bytecode.empty()) return true;
	return refreshLoadedFileBytecode(fileKey);
}

static bool evictTransientFileImage(const std::string &fileKey) {
	LoadedMacroFile file;
	if (!readLoadedMacroFileByKey(fileKey, file)) return false;
	if (!fileContainsOnlyTransientMacros(file)) return false;
	for (const auto &macroName : file.macroNames)
		if (macroIsRunning(macroName)) return false;
	file.bytecode.clear();
	file.bytecode.shrink_to_fit();
	writeLoadedMacroFileByKey(file);
	return true;
}

static bool currentBackgroundChildMacroAllowed(const LoadedMacroFile &file) noexcept {
	if (currentBackgroundEditSession() != nullptr) return mrvmCanRunInBackground(file.profile) || mrvmCanRunStagedInBackground(file.profile);
	return false;
}

static bool loadMacroFileIntoRegistry(const std::string &spec, std::string *loadedFileKey) {
	std::string resolvedPath = mrvmResolveMacroFilePath(spec, normalizeConfiguredPathInput(configuredMacroDirectoryPath()));
	std::string fileKey = mrvmMakeMacroFileKey(spec);
	std::string source;
	LoadedMacroFile newFile;
	LoadedMacroFile existingFile;
	unsigned char *compiled = nullptr;
	size_t compiledSize = 0;
	int macroCount;

	if (loadedFileKey != nullptr) loadedFileKey->clear();

	if (resolvedPath.empty() || !readTextFile(resolvedPath, source)) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	const bool hasExistingFile = readLoadedMacroFileByKey(fileKey, existingFile);
	if (hasExistingFile) {
		for (const auto &macroName : existingFile.macroNames)
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
		std::string macroKey = mrvmUpperKey(displayName);
		MacroRef existingMacro;

		if (displayName.empty()) continue;

		if (readLoadedMacroByKey(macroKey, existingMacro)) {
			if (macroIsRunning(macroKey) || existingMacro.permAttr) {
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

	if (hasExistingFile) {
		std::vector<std::string> oldNames = existingFile.macroNames;
		for (const auto &oldName : oldNames)
			removeMacroFromRegistryByKey(oldName);
	}

	for (int i = 0; i < macroCount; ++i) {
		const char *macroNameText = get_compiled_macro_name(i);
		int entry = get_compiled_macro_entry(i);
		int flags = get_compiled_macro_flags(i);
		const char *keyspecText = get_compiled_macro_keyspec(i);
		int mode = get_compiled_macro_mode(i);
		int unitKind = get_compiled_macro_unit_kind(i);
		int tickMs = get_compiled_macro_tick_ms(i);
		std::string displayName = macroNameText != nullptr ? macroNameText : std::string();
		std::string macroKey = mrvmUpperKey(displayName);
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
		ref.closureUnit = unitKind == MRMAC_UNIT_CLOSURE;
		ref.tickMs = tickMs > 0 ? static_cast<std::uint64_t>(tickMs) : 0;
		ref.hasAssignedKey = false;
		if (!ref.assignedKeySpec.empty()) ref.hasAssignedKey = mrvmParseAssignedKeySpec(ref.assignedKeySpec, ref.assignedKey);
		if (ref.closureUnit && ref.tickMs != 0) {
			MRRuntimeScheduledConsumerConfig config;
			const std::string macroSpec = newFile.displayName + "^" + displayName;
			config.intervalMs = ref.tickMs;
			config.macroSpec = macroSpec;
			config.entryName = displayName;
			config.closureId = macroSpec;
			ref.closureId = macroSpec;
			mrvmExecSessionsEnsureClosureState(g_runtimeEnv.runtimeKv, config.closureId, static_cast<int>(ref.tickMs));
			ref.scheduledConsumerId = registerRuntimeScheduledConsumer(config);
		}
		writeLoadedMacroByKey(macroKey, ref);
		appendMacroCatalogMacroOrder(macroKey);
		newFile.macroNames.push_back(macroKey);
	}

	writeLoadedMacroFileByKey(newFile);
	runtimeErrorLevel() = 0;
	logMacroProfileLine("Loaded macro file", newFile);
	static_cast<void>(mrvmUiRefreshRuntimeMenus(nullptr));
	if (loadedFileKey != nullptr) *loadedFileKey = fileKey;
	return true;
}

static bool tryLoadIndexedMacroForKey(const TKey &pressed) {
	mrvmLogCalculatorHotkeyState("vm-indexed-enter", pressed);
	const std::vector<IndexedBoundMacroEntry> indexed = macroCatalogIndexedBindings();
	for (std::size_t i = 0; i < indexed.size(); ++i) {
		const IndexedBoundMacroEntry &entry = indexed[i];
		std::string fileKey;

		if (!mrvmBindingKeysEqual(entry.key, pressed)) continue;
		mrvmLogCalculatorHotkeyState("vm-indexed-match", pressed, entry.filePath);
		fileKey = mrvmMakeMacroFileKey(entry.filePath);
		if (loadedMacroFileExists(fileKey)) return true;
		static_cast<void>(markMacroCatalogIndexedWarmupAttempted(fileKey));
		if (loadMacroFileIntoRegistry(entry.filePath, nullptr)) return true;
	}
	return false;
}

static bool unloadMacroFromRegistry(const std::string &macroName) {
	std::string macroKey = mrvmUpperKey(trimAscii(macroName));
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
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("GLOBAL_HASH expects one string argument.");
		if (!readGlobalValue(mrvmValueAsString(args[0]), entry) || entry.type != TYPE_HASH || entry.value.type != TYPE_HASH) {
			Value value = mrvmMakeHash(g_runtimeEnv.runtimeKv.globalStore().createHash(), true);
			setGlobalValue(mrvmValueAsString(args[0]), TYPE_HASH, value);
			out = value;
			return true;
		}
		out = entry.value;
		return true;
	}
	if (name == "EXISTS") {
		if (args.size() != 2 || args[0].type != TYPE_HASH || !mrvmIsStringLike(args[1])) throw std::runtime_error("EXISTS expects (hash, string).");
		out = mrvmMakeInt(mrvmHashContainsValue(vm.localHashStore(), g_runtimeEnv.runtimeKv.globalStore(), args[0], mrvmValueAsString(args[1])) ? 1 : 0);
		return true;
	}
	if (name == "HAS_VALUE") {
		if (args.size() != 2 || args[0].type != TYPE_HASH || !mrvmIsStringLike(args[1])) throw std::runtime_error("HAS_VALUE expects (hash, string).");
		const std::string key = mrvmValueAsString(args[1]);
		if (!mrvmHashContainsValue(vm.localHashStore(), g_runtimeEnv.runtimeKv.globalStore(), args[0], key)) {
			out = mrvmMakeInt(0);
			return true;
		}
		out = mrvmMakeInt(mrvmValueHasContent(mrvmHashReadValue(vm.localHashStore(), g_runtimeEnv.runtimeKv.globalStore(), args[0], key)) ? 1 : 0);
		return true;
	}
	if (name == "KEYS") {
		Value result = mrvmMakeArrayValue(TYPE_STR);
		if (args.size() != 1 || args[0].type != TYPE_HASH) throw std::runtime_error("KEYS expects one hash argument.");
		for (const std::string &key : mrvmHashRuntimeStoreForValue(vm.localHashStore(), g_runtimeEnv.runtimeKv.globalStore(), args[0]).keys(args[0].hashHandle))
			result.arrayValues.push_back(mrvmMakeString(key));
		out = result;
		return true;
	}
	if (name == "VALUES") {
		Value result = mrvmMakeArrayValue(TYPE_STR);
		if (args.size() != 1 || args[0].type != TYPE_HASH) throw std::runtime_error("VALUES expects one hash argument.");
		for (const Value &value : mrvmHashRuntimeStoreForValue(vm.localHashStore(), g_runtimeEnv.runtimeKv.globalStore(), args[0]).values(args[0].hashHandle))
			result.arrayValues.push_back(mrvmMakeString(mrvmValueAsString(value)));
		out = result;
		return true;
	}
	return false;
}

static Value applyIntrinsic(VirtualMachine &vm, const std::string &name, const std::vector<Value> &args) {
	if (name == "STR") {
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("STR expects one integer argument.");
		return mrvmMakeString(mrvmValueAsString(args[0]));
	}
	if (name == "CHAR") {
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("CHAR expects one integer argument.");
		return mrvmMakeChar(static_cast<unsigned char>(args[0].i & 0xFF));
	}
	if (name == "UTF8") {
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("UTF8 expects one integer argument.");
		return mrvmMakeString(mrvmUtf8FromCodepoint(static_cast<std::uint32_t>(args[0].i)));
	}
	if (name == "ASCII") {
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("ASCII expects one string argument.");
		std::string s = mrvmValueAsString(args[0]);
		return mrvmMakeInt(s.empty() ? 0 : static_cast<unsigned char>(s[0]));
	}
	if (name == "CAPS") {
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("CAPS expects one string argument.");
		return mrvmMakeString(mrvmUpperKey(mrvmValueAsString(args[0])));
	}
	if (name == "COPY") {
		std::string s;
		int pos;
		int count;
		std::size_t start;
		if (args.size() != 3 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("COPY expects (string, int, int).");
		s = mrvmValueAsString(args[0]);
		pos = mrvmCheckedStringIndex(args[1].i);
		count = args[2].i;
		if (count < 0) throw std::runtime_error("Invalid string index on string copy operation.");
		if (static_cast<std::size_t>(pos) > s.size()) return mrvmMakeString("");
		start = static_cast<std::size_t>(pos - 1);
		return mrvmMakeString(s.substr(start, static_cast<std::size_t>(count)));
	}
		if (name == "LENGTH") {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("LENGTH expects one string argument.");
			return mrvmMakeInt(static_cast<int>(mrvmValueAsString(args[0]).size()));
		}
		if (name == "LEN") {
			if (args.size() != 1 || (!mrvmIsStringLike(args[0]) && !mrvmValueIsArrayType(args[0].type))) throw std::runtime_error("LEN expects one string or array argument.");
			if (mrvmValueIsArrayType(args[0].type)) return mrvmMakeInt(static_cast<int>(args[0].arrayValues.size()));
			return mrvmMakeInt(static_cast<int>(mrvmValueAsString(args[0]).size()));
		}
		if (name == "POS") {
			std::string needle;
			std::string haystack;
		std::size_t pos;
		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("POS expects (substring, string).");
		needle = mrvmValueAsString(args[0]);
		haystack = mrvmValueAsString(args[1]);
		if (needle.empty()) return mrvmMakeInt(1);
		pos = haystack.find(needle);
		return mrvmMakeInt(pos == std::string::npos ? 0 : static_cast<int>(pos + 1));
	}
	if (name == "XPOS") {
		std::string needle;
		std::string haystack;
		int startPos;
		std::size_t pos;
		if (args.size() != 3 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || args[2].type != TYPE_INT) throw std::runtime_error("XPOS expects (substring, string, int).");
		needle = mrvmValueAsString(args[0]);
		haystack = mrvmValueAsString(args[1]);
		startPos = mrvmCheckedStringIndex(args[2].i);
		if (needle.empty()) return mrvmMakeInt(startPos <= static_cast<int>(haystack.size()) + 1 ? startPos : 0);
		if (static_cast<std::size_t>(startPos) > haystack.size()) return mrvmMakeInt(0);
		pos = haystack.find(needle, static_cast<std::size_t>(startPos - 1));
		return mrvmMakeInt(pos == std::string::npos ? 0 : static_cast<int>(pos + 1));
	}
	if (name == "STR_DEL") {
		std::string s;
		int pos;
		int count;
		std::size_t start;
		if (args.size() != 3 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("STR_DEL expects (string, int, int).");
		s = mrvmValueAsString(args[0]);
		pos = mrvmCheckedStringIndex(args[1].i);
		count = args[2].i;
		if (count < 0) throw std::runtime_error("Invalid string index on string copy operation.");
		if (static_cast<std::size_t>(pos) > s.size()) return mrvmMakeString(s);
		start = static_cast<std::size_t>(pos - 1);
		s.erase(start, static_cast<std::size_t>(count));
		return mrvmMakeString(s);
	}
	if (name == "STR_INS") {
		std::string target;
		std::string dest;
		int location;
		std::size_t insertPos;
		if (args.size() != 3 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || args[2].type != TYPE_INT) throw std::runtime_error("STR_INS expects (string, string, int).");
		target = mrvmValueAsString(args[0]);
		dest = mrvmValueAsString(args[1]);
		location = mrvmCheckedInsertIndex(args[2].i);
		insertPos = static_cast<std::size_t>(location);
		if (insertPos > dest.size()) insertPos = dest.size();
		dest.insert(insertPos, target);
		mrvmEnforceStringLength(dest);
		return mrvmMakeString(dest);
	}
	if (name == "REAL_I") {
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("REAL_I expects one integer argument.");
		return mrvmMakeReal(static_cast<double>(args[0].i));
	}
	if (name == "INT_R") {
		if (args.size() != 1 || args[0].type != TYPE_REAL) throw std::runtime_error("INT_R expects one real argument.");
		if (args[0].r < static_cast<double>(std::numeric_limits<int>::min()) || args[0].r > static_cast<double>(std::numeric_limits<int>::max())) throw std::runtime_error("Real to Integer conversion out of range.");
		return mrvmMakeInt(static_cast<int>(args[0].r));
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
		mrvmEnforceStringLength(buf);
		return mrvmMakeString(buf);
	}
	if (name == "REMOVE_SPACE") {
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("REMOVE_SPACE expects one string argument.");
		return mrvmMakeString(mrvmRemoveSpaceAscii(mrvmValueAsString(args[0])));
	}
	if (name == "GET_EXTENSION") {
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("GET_EXTENSION expects one string argument.");
		return mrvmMakeString(mrvmGetExtensionPart(mrvmValueAsString(args[0])));
	}
	if (name == "GET_PATH") {
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("GET_PATH expects one string argument.");
		return mrvmMakeString(mrvmGetPathPart(mrvmValueAsString(args[0])));
	}
	if (name == "TRUNCATE_EXTENSION") {
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("TRUNCATE_EXTENSION expects one string argument.");
		return mrvmMakeString(mrvmTruncateExtensionPart(mrvmValueAsString(args[0])));
	}
	if (name == "TRUNCATE_PATH") {
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("TRUNCATE_PATH expects one string argument.");
		return mrvmMakeString(mrvmTruncatePathPart(mrvmValueAsString(args[0])));
	}
	if (name == "FILE_EXISTS") {
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("FILE_EXISTS expects one string argument.");
		return mrvmMakeInt(mrvmFileExistsPath(mrvmValueAsString(args[0])) ? 1 : 0);
	}
	if (name == "FILE_ATTR") {
		int attr = 0;
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("FILE_ATTR expects one string argument.");
		if (!mrvmReadFileMetadata(mrvmValueAsString(args[0]), &attr, nullptr, nullptr)) {
			runtimeErrorLevel() = errno != 0 ? errno : 1;
			return mrvmMakeInt(0);
		}
		runtimeErrorLevel() = 0;
		return mrvmMakeInt(attr);
	}
	if (name == "FIRST_FILE") {
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("FIRST_FILE expects one string argument.");
		return mrvmMakeInt(findFirstFileMatch(mrvmValueAsString(args[0])));
	}
	if (name == "NEXT_FILE") {
		if (!args.empty()) throw std::runtime_error("NEXT_FILE expects no arguments.");
		return mrvmMakeInt(findNextFileMatch());
	}
	if (name == "SUBSHELL") {
		MRVMSubshellResult subshell;
		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SUBSHELL expects (string, int).");
		subshell = mrvmRunSubshellCapture(mrvmValueAsString(args[0]), mrvmValueAsInt(args[1]), configuredShellExecutablePath());
		runtimeErrorLevel() = subshell.errorLevel;
		return mrvmMakeString(subshell.output);
	}
	if (name == "SEARCH_FWD") {
		MRFileEditor *editor;
		std::size_t matchStart = 0;
		std::size_t matchEnd = 0;
		MREditWindow *win;
		BackgroundEditSession *session;
		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SEARCH_FWD expects (string, int).");
		if (mrvmValueAsString(args[0]).empty()) {
			runtimeErrorLevel() = 1010;
			return mrvmMakeInt(0);
		}
		editor = currentEditor();
		session = currentBackgroundEditSession();
		if (editor == nullptr && session == nullptr) return mrvmMakeInt(0);
		if (!searchEditorForward(editor, mrvmValueAsString(args[0]), mrvmValueAsInt(args[1]), currentRuntimeIgnoreCase(), matchStart, matchEnd)) {
			if (session != nullptr) session->clearLastSearch();
			else
				g_runtimeEnv.lastSearchValid = false;
			runtimeErrorLevel() = 0;
			return mrvmMakeInt(0);
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
		return mrvmMakeInt(1);
	}
	if (name == "SEARCH_BWD") {
		MRFileEditor *editor;
		std::size_t matchStart = 0;
		std::size_t matchEnd = 0;
		MREditWindow *win;
		BackgroundEditSession *session;
		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SEARCH_BWD expects (string, int).");
		if (mrvmValueAsString(args[0]).empty()) {
			runtimeErrorLevel() = 1010;
			return mrvmMakeInt(0);
		}
		editor = currentEditor();
		session = currentBackgroundEditSession();
		if (editor == nullptr && session == nullptr) return mrvmMakeInt(0);
		if (!searchEditorBackward(editor, mrvmValueAsString(args[0]), mrvmValueAsInt(args[1]), currentRuntimeIgnoreCase(), matchStart, matchEnd)) {
			if (session != nullptr) session->clearLastSearch();
			else
				g_runtimeEnv.lastSearchValid = false;
			runtimeErrorLevel() = 0;
			return mrvmMakeInt(0);
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
		return mrvmMakeInt(1);
	}
	if (name == "GET_ENVIRONMENT") {
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("GET_ENVIRONMENT expects one string argument.");
		return mrvmMakeString(getEnvironmentValue(mrvmValueAsString(args[0])));
	}
	if (name == "GET_WORD") {
		MRFileEditor *editor;
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("GET_WORD expects one string argument.");
		editor = currentEditor();
		if (editor == nullptr && currentBackgroundEditSession() == nullptr) return mrvmMakeString("");
		return mrvmMakeString(currentEditorWord(editor, mrvmValueAsString(args[0])));
	}
	if (name == "PARAM_STR") {
		int index;
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("PARAM_STR expects one integer argument.");
		index = mrvmValueAsInt(args[0]);
		if (index == 0) return mrvmMakeString(g_runtimeEnv.startupCommand);
		if (index < 0 || static_cast<std::size_t>(index) > g_runtimeEnv.processArgs.size()) return mrvmMakeString("");
		return mrvmMakeString(g_runtimeEnv.processArgs[static_cast<std::size_t>(index - 1)]);
	}
	if (name == "GLOBAL_STR") {
		GlobalEntry entry;
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("GLOBAL_STR expects one string argument.");
		if (!readGlobalValue(mrvmValueAsString(args[0]), entry) || entry.type != TYPE_STR) return mrvmMakeString("");
		return entry.value;
	}
	if (name == "GLOBAL_INT") {
		GlobalEntry entry;
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("GLOBAL_INT expects one string argument.");
		if (!readGlobalValue(mrvmValueAsString(args[0]), entry) || entry.type != TYPE_INT) return mrvmMakeInt(0);
		return entry.value;
	}
	{
		Value result;
		if (applyHashIntrinsic(vm, name, args, result)) return result;
	}
	if (name == "CHECK_KEY") {
		int key1 = 0;
		int key2 = 0;
		if (!args.empty()) throw std::runtime_error("CHECK_KEY expects no arguments.");
		if (readMacroKeyPair(false, key1, key2)) return mrvmMakeInt(1);
		return mrvmMakeInt(0);
	}
	if (name == "VERSION") {
		if (!args.empty()) throw std::runtime_error("VERSION expects no arguments.");
		return mrvmMakeString(mrDisplayVersion());
	}
	if (name == "OS_BACK") {
		if (!args.empty()) throw std::runtime_error("OS_BACK expects no arguments.");
		return mrvmMakeInt(0);
	}
	if (name == "OS_COLOR") {
		if (!args.empty()) throw std::runtime_error("OS_COLOR expects no arguments.");
		return mrvmMakeInt(7);
	}
	if (name == "PARSE_STR") {
		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("PARSE_STR expects (string, string).");
		return mrvmMakeString(parseNamedValue(mrvmValueAsString(args[0]), mrvmValueAsString(args[1])));
	}
	if (name == "PARSE_INT") {
		std::string parsed;
		int errorPos;
		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("PARSE_INT expects (string, string).");
		parsed = parseNamedValue(mrvmValueAsString(args[0]), mrvmValueAsString(args[1]));
		if (parsed.empty()) return mrvmMakeInt(0);
		errorPos = mrvmFindValErrorPosition(parsed);
		if (errorPos != 0) return mrvmMakeInt(0);
		return mrvmMakeInt(static_cast<int>(std::strtol(parsed.c_str(), nullptr, 10)));
	}
	if (name == "INQ_MACRO") {
		BackgroundEditSession *session;
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("INQ_MACRO expects one string argument.");
		session = currentBackgroundEditSession();
		if (session != nullptr) return mrvmMakeInt(session->loadedMacroDisplayNames.find(mrvmUpperKey(mrvmValueAsString(args[0]))) != session->loadedMacroDisplayNames.end() ? 1 : 0);
		return mrvmMakeInt(loadedMacroExists(mrvmUpperKey(mrvmValueAsString(args[0]))) ? 1 : 0);
	}
	if (name == "COPY_FILE") {
		std::string source;
		std::string target;
		const bool append = args.size() == 3 && mrvmValueAsInt(args[2]) != 0;
		std::ifstream in;
		std::ofstream out;

		if ((args.size() != 2 && args.size() != 3) || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || (args.size() == 3 && args[2].type != TYPE_INT)) throw std::runtime_error("COPY_FILE expects (string, string[, int]).");
		source = mrvmProcessExpandUserPath(mrvmValueAsString(args[0]));
		target = mrvmProcessExpandUserPath(mrvmValueAsString(args[1]));
		in.open(source.c_str(), std::ios::in | std::ios::binary);
		out.open(target.c_str(), (append ? (std::ios::out | std::ios::binary | std::ios::app) : (std::ios::out | std::ios::binary | std::ios::trunc)));
		if (!in || !out) {
			runtimeErrorLevel() = errno != 0 ? errno : 1;
			return mrvmMakeInt(runtimeErrorLevel());
		}
		out << in.rdbuf();
		runtimeErrorLevel() = (in.good() || in.eof()) && out.good() ? 0 : (errno != 0 ? errno : 1);
		return mrvmMakeInt(runtimeErrorLevel());
	}
	if (name == "RENAME_FILE") {
		std::string source;
		std::string target;
		if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("RENAME_FILE expects (string, string).");
		source = mrvmProcessExpandUserPath(mrvmValueAsString(args[0]));
		target = mrvmProcessExpandUserPath(mrvmValueAsString(args[1]));
		runtimeErrorLevel() = ::rename(source.c_str(), target.c_str()) == 0 ? 0 : (errno != 0 ? errno : 1);
		return mrvmMakeInt(runtimeErrorLevel());
	}
	if (name == "SWITCH_FILE") {
		const std::string target = mrvmProcessExpandUserPath(mrvmValueAsString(args[0]));
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("SWITCH_FILE expects one string argument.");
		if (currentBackgroundEditSession() != nullptr) return mrvmMakeInt(0);
		for (MREditWindow *window : allEditWindowsInZOrder()) {
			if (window == nullptr) continue;
			if (target != mrvmProcessExpandUserPath(window->currentFileName())) continue;
			runtimeErrorLevel() = mrActivateEditWindow(window) ? 0 : 1001;
			return mrvmMakeInt(runtimeErrorLevel() == 0 ? 1 : 0);
		}
		runtimeErrorLevel() = 0;
		return mrvmMakeInt(0);
	}
	if (name == "SCREEN_LENGTH") return mrvmMakeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->screenHeight : static_cast<int>(TDisplay::getRows()));
	if (name == "SCREEN_WIDTH") return mrvmMakeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->screenWidth : static_cast<int>(TDisplay::getCols()));
	if (name == "WHEREX") {
		BackgroundEditSession *session = currentBackgroundEditSession();
		int x = 0;
		int y = 0;
		MRFileEditor *editor = currentEditor();
		if (editor != nullptr && currentUiCursorPosition(x, y)) return mrvmMakeInt(x);
		if (session != nullptr) return mrvmMakeInt(session->screenCursorX);
		return mrvmMakeInt(currentUiCursorPosition(x, y) ? x : 0);
	}
	if (name == "WHEREY") {
		BackgroundEditSession *session = currentBackgroundEditSession();
		int x = 0;
		int y = 0;
		if (currentEditor() != nullptr && currentUiCursorPosition(x, y)) return mrvmMakeInt(y);
		if (session != nullptr) return mrvmMakeInt(session->screenCursorY);
		return mrvmMakeInt(currentUiCursorPosition(x, y) ? y : 0);
	}
	if (name == "BLOCK_TEXT") {
		return mrvmMakeString(std::string());
	}
	if (name == "BAR_MENU" || name == "V_MENU") {
		if (currentBackgroundEditSession() != nullptr) throw std::runtime_error(name + " is not available in background mode.");
		return mrvmMakeInt(mrvmRunMacroMenuIntrinsic(name, args));
	}
	if (name == "UI_EXEC") return mrvmMakeInt(mrvmRunMacroUiDialogDefinition(g_runtimeEnv.runtimeKv));
	if (name == "UI_TEXT") return mrvmMakeString(mrvmModelessUiReadTextValue(g_runtimeEnv.runtimeKv, mrvmValueAsInt(args[0])));
	if (name == "UI_INDEX") return mrvmMakeInt(mrvmModelessUiReadIndexValue(g_runtimeEnv.runtimeKv, mrvmValueAsInt(args[0])));
	if (name == "STRING_IN") {
		if (currentBackgroundEditSession() != nullptr) throw std::runtime_error("STRING_IN is not available in background mode.");
		return mrvmMakeString(mrvmRunMacroStringInputIntrinsic(args));
	}

	throw std::runtime_error("Unknown intrinsic: " + name);
}
} // namespace

bool currentExecutingMacroSpec(std::string &macroSpec) {
	return currentExecutingMacroSpecFromRuntimeStack(macroSpec);
}

MRVMRuntimeKv &mrvmRuntimeKv() noexcept {
	return g_runtimeEnv.runtimeKv;
}

std::recursive_mutex &mrvmExecutionMutex() noexcept {
	return g_vmExecutionMutex;
}

bool mrvmHasActiveBackgroundEditSession() noexcept {
	return currentBackgroundEditSession() != nullptr;
}

std::string mrvmEditorExpandUserPath(const std::string &path) {
	return mrvmProcessExpandUserPath(path);
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

void mrvmSetProcessContext(int argc, char **argv) {
	mrvmProcessRuntimeSetContext(argc, argv);
}

std::vector<std::string> mrvmProcessArguments() {
	return mrvmProcessRuntimeArguments();
}

VirtualMachine::Value::Value() : type(TYPE_INT), i(0), r(0.0), c(0), hashHandle(0), arrayElementType(TYPE_INT), arrayValues(), globalStorage(false) {
}

VirtualMachine::VirtualMachine() : mHashStore(std::make_unique<MRVMHashStore>()), mClosureId(), mClosureVariableNames(), mExecutionSessionId(0), mSessionVariableNames(), verboseLogging(true), logTruncated(false), mAsyncDelayPending(false), mAsyncDelayReady(false), mAsyncDelayEnabled(true), mAsyncLength(0), mAsyncIp(0), mAsyncReturnInt(0), mAsyncErrorLevel(0), mAsyncMacroFramePushed(false), mAsyncDelayTaskId(0), mAsyncDelayGeneration(0), mAsyncDelayMillis(0), cancelledExecution(false) {
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

void VirtualMachine::setClosureContext(const std::string &closureId) {
	mClosureId = closureId;
	mClosureVariableNames.clear();
	mExecUiCommandRequests.clear();
}

void VirtualMachine::setExecutionSessionContext(MRMacroExecutionSessionId sessionId) {
	mExecutionSessionId = sessionId;
	mSessionVariableNames.clear();
}

const std::vector<MRMacroExecUiCommandRequest> &VirtualMachine::execUiCommandRequests() const noexcept {
	return mExecUiCommandRequests;
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
	return mrvmMakeInt(0);
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
	struct ExecutionSessionGuard {
		MRMacroExecutionSessionId previous;

		explicit ExecutionSessionGuard(MRMacroExecutionSessionId next) noexcept : previous(g_executionSessionId) {
			if (next != 0) g_executionSessionId = next;
		}

		~ExecutionSessionGuard() {
			g_executionSessionId = previous;
		}
	} executionSessionGuard(mExecutionSessionId != 0 ? mExecutionSessionId : g_executionSessionId);

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
		mSessionVariableNames.clear();
		mHashStore->clearExceptRoots(currentGlobalHashRoots());
		stack.clear();
		cancelledExecution = false;
		if (resetState) {
			log.clear();
			logTruncated = false;
			setMacroGlobalEnumIndex(0);
			setMacroCatalogMacroEnumIndex(0);
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
				push(mrvmMakeInt(val));
				appendLogLine("Push integer: " + std::to_string(val));
			} else if (opcode == OP_PUSH_R) {
				double val;
				readDouble(val);
				push(mrvmMakeReal(val));
				appendLogLine("Push real: " + mrvmValueAsString(mrvmMakeReal(val)));
			} else if (opcode == OP_PUSH_S) {
				std::string str;
				readCString(str);
				mrvmEnforceStringLength(str);
				push(mrvmMakeString(str));
				appendLogLine("Push string: " + str);
			} else if (opcode == OP_DEF_VAR) {
				std::string varName;
				int varType = static_cast<int>(bytecode[ip++]);
				Value value;
				readCString(varName);
				if (!mClosureId.empty()) {
					bool restored = false;
					mClosureVariableNames.insert(varName);
					if (mrvmExecSessionsReadClosureVariable(g_runtimeEnv.runtimeKv, mClosureId, varName, value)) {
						variables[varName] = mrvmCoerceForStore(value, varType);
						restored = true;
					} else if (varType == TYPE_HASH)
						variables[varName] = mrvmMakeHash(mHashStore->createHash());
					else
						variables[varName] = mrvmDefaultValueForType(varType);
					if (!restored) static_cast<void>(mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, mClosureId, varName, variables[varName], *mHashStore));
				} else if (currentExecutionSessionId() != 0) {
					bool restored = false;
					mSessionVariableNames.insert(varName);
					if (mrvmExecSessionsReadSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, value)) {
						variables[varName] = mrvmCoerceForStore(value, varType);
						restored = true;
					} else if (varType == TYPE_HASH)
						variables[varName] = mrvmMakeHash(mHashStore->createHash());
					else
						variables[varName] = mrvmDefaultValueForType(varType);
					if (!restored) static_cast<void>(mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, variables[varName], *mHashStore));
				} else if (varType == TYPE_HASH)
					variables[varName] = mrvmMakeHash(mHashStore->createHash());
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
					if (it == variables.end()) variables[varName] = mrvmMakeInt(0);
					push(variables[varName]);
				}
				appendLogLine("Load variable: " + varName);
			} else if (opcode == OP_STORE_VAR) {
				std::string varName;
				int targetType = static_cast<int>(bytecode[ip++]);
				readCString(varName);
				Value value = mrvmCoerceForStore(pop(), targetType);
				if (value.type == TYPE_STR) mrvmEnforceStringLength(value.s);
				if (!storeSpecialVariable(varName, value)) variables[varName] = value;
				if (!mClosureId.empty() && mClosureVariableNames.find(varName) != mClosureVariableNames.end()) mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, mClosureId, varName, value, *mHashStore);
				else if (currentExecutionSessionId() != 0 && mSessionVariableNames.find(varName) != mSessionVariableNames.end())
					mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, value, *mHashStore);
				appendLogLine("Store variable: " + varName);
			} else if (opcode == OP_HASH_LOAD) {
				std::string varName;
				Value key;
				std::map<std::string, Value>::const_iterator it;
				readCString(varName);
				key = pop();
				if (!mrvmIsStringLike(key)) throw std::runtime_error("type mismatch");
				it = variables.find(varName);
				if (it == variables.end() || it->second.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
				push(mrvmHashReadValue(*mHashStore, g_runtimeEnv.runtimeKv.globalStore(), it->second, mrvmValueAsString(key)));
				appendLogLine("Load hash value: " + varName);
			} else if (opcode == OP_HASH_LOAD_VALUE) {
				Value key;
				Value hash;
				key = pop();
				hash = pop();
				if (hash.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
				if (!mrvmIsStringLike(key)) throw std::runtime_error("type mismatch");
				push(mrvmHashReadValue(*mHashStore, g_runtimeEnv.runtimeKv.globalStore(), hash, mrvmValueAsString(key)));
				appendLogLine("Load hash value from expression.");
			} else if (opcode == OP_HASH_STORE) {
				std::string varName;
				Value value;
				Value key;
				std::map<std::string, Value>::const_iterator it;
				readCString(varName);
				value = pop();
				key = pop();
				if (!mrvmIsStringLike(key)) throw std::runtime_error("type mismatch");
				it = variables.find(varName);
				if (it == variables.end() || it->second.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
				if (value.type == TYPE_STR) mrvmEnforceStringLength(value.s);
				mrvmHashWriteValue(*mHashStore, g_runtimeEnv.runtimeKv.globalStore(), it->second, mrvmValueAsString(key), value);
				if (!mClosureId.empty() && mClosureVariableNames.find(varName) != mClosureVariableNames.end()) mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, mClosureId, varName, it->second, *mHashStore);
				else if (currentExecutionSessionId() != 0 && mSessionVariableNames.find(varName) != mSessionVariableNames.end())
					mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, it->second, *mHashStore);
				appendLogLine("Store hash value: " + varName);
			} else if (opcode == OP_HASH_STORE_VALUE) {
				Value value;
				Value key;
				Value hash;
				value = pop();
				key = pop();
				hash = pop();
				if (hash.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
				if (!mrvmIsStringLike(key)) throw std::runtime_error("type mismatch");
				if (value.type == TYPE_STR) mrvmEnforceStringLength(value.s);
				mrvmHashWriteValue(*mHashStore, g_runtimeEnv.runtimeKv.globalStore(), hash, mrvmValueAsString(key), value);
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
				mrvmArrayWriteValue(it->second, index.i, value, *mHashStore, g_runtimeEnv.runtimeKv.globalStore());
				if (!mClosureId.empty() && mClosureVariableNames.find(varName) != mClosureVariableNames.end()) mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, mClosureId, varName, it->second, *mHashStore);
				else if (currentExecutionSessionId() != 0 && mSessionVariableNames.find(varName) != mSessionVariableNames.end())
					mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, it->second, *mHashStore);
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
				if (mrvmIsStringLike(a) && mrvmIsStringLike(b)) {
					std::string s = mrvmValueAsString(a) + mrvmValueAsString(b);
					mrvmEnforceStringLength(s);
					push(mrvmMakeString(s));
				} else if (mrvmIsNumeric(a) && mrvmIsNumeric(b)) {
					if (a.type == TYPE_REAL || b.type == TYPE_REAL) push(mrvmMakeReal(mrvmValueAsReal(a) + mrvmValueAsReal(b)));
					else
						push(mrvmMakeInt(a.i + b.i));
				} else
					throw std::runtime_error(MRConstants::kErrorTypeMismatch);
			} else if (opcode == OP_SUB) {
				Value b = pop();
				Value a = pop();
				if (!mrvmIsNumeric(a) || !mrvmIsNumeric(b)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
				if (a.type == TYPE_REAL || b.type == TYPE_REAL) push(mrvmMakeReal(mrvmValueAsReal(a) - mrvmValueAsReal(b)));
				else
					push(mrvmMakeInt(a.i - b.i));
			} else if (opcode == OP_MUL) {
				Value b = pop();
				Value a = pop();
				if (!mrvmIsNumeric(a) || !mrvmIsNumeric(b)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
				if (a.type == TYPE_REAL || b.type == TYPE_REAL) push(mrvmMakeReal(mrvmValueAsReal(a) * mrvmValueAsReal(b)));
				else
					push(mrvmMakeInt(a.i * b.i));
			} else if (opcode == OP_DIV) {
				Value b = pop();
				Value a = pop();
				if (!mrvmIsNumeric(a) || !mrvmIsNumeric(b)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
				if ((b.type == TYPE_REAL && b.r == 0.0) || (b.type == TYPE_INT && b.i == 0)) throw std::runtime_error("Division by zero.");
				if (a.type == TYPE_REAL || b.type == TYPE_REAL) push(mrvmMakeReal(mrvmValueAsReal(a) / mrvmValueAsReal(b)));
				else
					push(mrvmMakeInt(a.i / b.i));
			} else if (opcode == OP_MOD) {
				Value b = pop();
				Value a = pop();
				if (a.type != TYPE_INT || b.type != TYPE_INT) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
				if (b.i == 0) throw std::runtime_error("Modulo by zero.");
				push(mrvmMakeInt(a.i % b.i));
			} else if (opcode == OP_NEG) {
				Value a = pop();
				if (!mrvmIsNumeric(a)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
				if (a.type == TYPE_REAL) push(mrvmMakeReal(-a.r));
				else
					push(mrvmMakeInt(-a.i));
			} else if (opcode == OP_CMP_EQ || opcode == OP_CMP_NE || opcode == OP_CMP_LT || opcode == OP_CMP_GT || opcode == OP_CMP_LE || opcode == OP_CMP_GE) {
				Value b = pop();
				Value a = pop();
				int cmp = mrvmCompareValues(a, b);
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
				push(mrvmMakeInt(result));
			} else if (opcode == OP_AND) {
				Value b = pop();
				Value a = pop();
				push(mrvmMakeInt((mrvmValueAsInt(a) != 0 && mrvmValueAsInt(b) != 0) ? 1 : 0));
			} else if (opcode == OP_OR) {
				Value b = pop();
				Value a = pop();
				push(mrvmMakeInt((mrvmValueAsInt(a) != 0 || mrvmValueAsInt(b) != 0) ? 1 : 0));
			} else if (opcode == OP_NOT) {
				Value a = pop();
				push(mrvmMakeInt(mrvmValueAsInt(a) == 0 ? 1 : 0));
			} else if (opcode == OP_SHL) {
				Value b = pop();
				Value a = pop();
				push(mrvmMakeInt(mrvmValueAsInt(a) << mrvmValueAsInt(b)));
			} else if (opcode == OP_SHR) {
				Value b = pop();
				Value a = pop();
				push(mrvmMakeInt(mrvmValueAsInt(a) >> mrvmValueAsInt(b)));
			} else if (opcode == OP_BIT_AND) {
				Value b = pop();
				Value a = pop();
				push(mrvmMakeInt(mrvmValueAsInt(a) & mrvmValueAsInt(b)));
			} else if (opcode == OP_BIT_OR) {
				Value b = pop();
				Value a = pop();
				push(mrvmMakeInt(mrvmValueAsInt(a) | mrvmValueAsInt(b)));
			} else if (opcode == OP_BIT_XOR) {
				Value b = pop();
				Value a = pop();
				push(mrvmMakeInt(mrvmValueAsInt(a) ^ mrvmValueAsInt(b)));
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
				if (!mrvmIsStringLike(source)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);

				std::string textValue = mrvmValueAsString(source);
				if (opcode == OP_VAL) {
					int errorPos = mrvmFindValErrorPosition(textValue);
					if (errorPos == 0) {
						long long parsed = std::strtoll(textValue.c_str(), nullptr, 10);
						if (parsed < static_cast<long long>(std::numeric_limits<int>::min()) || parsed > static_cast<long long>(std::numeric_limits<int>::max())) throw std::runtime_error("Real to Integer conversion out of range.");
						variables[varName] = mrvmMakeInt(static_cast<int>(parsed));
					} else
						resultCode = errorPos;
				} else {
					int errorPos = mrvmFindRValErrorPosition(textValue);
					if (errorPos == 0) {
						char *endPtr = nullptr;
						double parsed = std::strtod(textValue.c_str(), &endPtr);
						(void)endPtr;
						variables[varName] = mrvmMakeReal(parsed);
					} else
						resultCode = errorPos;
				}
				push(mrvmMakeInt(resultCode));
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
						variables[targetVar] = mrvmMakeInt(it->second.type == TYPE_INT ? 1 : 0);
						push(mrvmMakeString(key));
						goto handled_global_enum;
					}
					variables[targetVar] = mrvmMakeInt(0);
					push(mrvmMakeString(""));
					goto handled_global_enum;
				}

				if (opcode == OP_FIRST_GLOBAL) setMacroGlobalEnumIndex(0);
				{
					const std::vector<std::string> order = macroGlobalOrderValues();
					std::size_t index = macroGlobalEnumIndex();
					while (index < order.size()) {
						const std::string key = order[index++];
						GlobalEntry entry;
						setMacroGlobalEnumIndex(index);
						if (!readRuntimeGlobalValueDirect(key, entry)) continue;
						variables[targetVar] = mrvmMakeInt(entry.type == TYPE_INT ? 1 : 0);
						push(mrvmMakeString(key));
						goto handled_global_enum;
					}
				}
				variables[targetVar] = mrvmMakeInt(0);
				push(mrvmMakeString(""));
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
					std::string source = mrvmValueAsString(it->second);
					bool toVirtuals = currentRuntimeTabExpand();
					it->second = mrvmMakeString(expandTabsString(source, toVirtuals));
					if (varArgc > 1) {
						std::map<std::string, Value>::iterator indexIt = variables.find(indexVarName);
						if (indexIt == variables.end()) throw std::runtime_error("Variable expected.");
						if (indexIt->second.type != TYPE_INT) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
						indexIt->second = mrvmMakeInt(expandedTabsAdjustedIndex(source, indexIt->second.i));
					}
				} else if (name == "TABS_TO_SPACES") {
					if (varArgc != 1) throw std::runtime_error("TABS_TO_SPACES expects one variable argument.");
					it->second = mrvmMakeString(tabsToSpacesString(mrvmValueAsString(it->second)));
				} else
					throw std::runtime_error("Unknown variable procedure.");
			} else if (opcode == OP_PROC) {
				std::string name;
				readCString(name);
				unsigned char argc = bytecode[ip++];
				std::vector<Value> args = popArgs(argc);
					if (name == "EXEC_ASSIGN") {
						MRMacroExecUiCommandRequest request;
						bool accepted = false;

						if (args.size() != 3 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || !mrvmIsStringLike(args[2])) throw std::runtime_error("EXEC expects (target, command).");
						request.closureId = mClosureId;
						request.target = mrvmValueAsString(args[0]);
						request.command = mrvmValueAsString(args[1]);
						request.lvalue = mrvmValueAsString(args[2]);
						if (currentBackgroundEditSession() != nullptr || g_backgroundMacroStopToken != nullptr) {
							mExecUiCommandRequests.push_back(request);
							runtimeErrorLevel() = 0;
						} else {
							accepted = mrvmApplyExecUiCommandRequest(request);
							variables[request.lvalue] = mrvmMakeInt(accepted ? 1 : 0);
							if (!mClosureId.empty() && mClosureVariableNames.find(request.lvalue) != mClosureVariableNames.end()) mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, mClosureId, request.lvalue, variables[request.lvalue], *mHashStore);
							else if (currentExecutionSessionId() != 0 && mSessionVariableNames.find(request.lvalue) != mSessionVariableNames.end())
								mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), request.lvalue, variables[request.lvalue], *mHashStore);
							runtimeErrorLevel() = accepted ? 0 : 1001;
						}
					} else if (name == "KEYMAP_RESET") {
						if (!args.empty()) throw std::runtime_error("KEYMAP_RESET expects no arguments.");
						if (!setConfiguredKeymapProfiles(std::vector<MRKeymapProfile>(), nullptr)) throw std::runtime_error("KEYMAP_RESET failed: invalid keymap state.");
						if (!setConfiguredActiveKeymapProfile("", nullptr)) throw std::runtime_error("KEYMAP_RESET failed: invalid active keymap profile.");
						runtimeErrorLevel() = 0;
					} else if (name == "KEYMAP_VERSION" || name == "THEME_VERSION") {
						const std::string versionLiteral = args.size() == 1 && mrvmIsStringLike(args[0]) ? trimAscii(mrvmValueAsString(args[0])) : std::string();
						std::uint64_t parsedVersion = 0;
						const std::string artifactLabel = name == "KEYMAP_VERSION" ? "Keymap" : "Theme";

						if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error(name + " expects (string).");
						if (!mrParsePersistenceVersion(versionLiteral, parsedVersion))
							throw std::runtime_error(name + " failed: invalid persistence version.");
						if (parsedVersion > mrCurrentPersistenceVersion())
							throw std::runtime_error(name + " failed: future build version " + versionLiteral + " is not supported.");
						if (parsedVersion < mrCurrentPersistenceVersion()) mrLogMessage((artifactLabel + " file version upgrade required: " + versionLiteral).c_str());
						runtimeErrorLevel() = 0;
					} else if (name == "KEYMAP_PROFILE") {
						std::string errorText;
						if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("KEYMAP_PROFILE expects (string).");
						if (!mrvmApplyConfiguredKeymapProfilePayload(mrvmValueAsString(args[0]), &errorText)) throw std::runtime_error("KEYMAP_PROFILE failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
						runtimeErrorLevel() = 0;
					} else if (name == "KEYMAP_BIND") {
						std::string errorText;
						if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("KEYMAP_BIND expects (string).");
						if (!mrvmApplyConfiguredKeymapBindingPayload(mrvmValueAsString(args[0]), &errorText)) throw std::runtime_error("KEYMAP_BIND failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
						runtimeErrorLevel() = 0;
					} else if (name == "ACTIVE_KEYMAP_PROFILE") {
						std::string errorText;
						if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("ACTIVE_KEYMAP_PROFILE expects (string).");
						if (!mrvmApplyConfiguredActiveKeymapProfilePayload(mrvmValueAsString(args[0]), &errorText)) throw std::runtime_error("ACTIVE_KEYMAP_PROFILE failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
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
						if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error(name + " expects (string).");
						if (name == "THEME_NAME") {
							if (!setConfiguredColorThemeDisplayName(mrvmValueAsString(args[0]), &errorText)) throw std::runtime_error("THEME_NAME failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
						} else if (!applyConfiguredColorSetupValue(name, mrvmValueAsString(args[0]), &errorText, false))
							throw std::runtime_error(name + " failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
						runtimeErrorLevel() = 0;
					} else if (name == "MRSETUP") {
						std::string setupKey;
						std::string errorText;
						MRSetupPaths activePaths = resolveSetupPathDefaults();

						if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("MRSETUP expects (string, string).");
						setupKey = mrvmUpperKey(trimAscii(mrvmValueAsString(args[0])));
						if (setupKey == "FILECOMPAREMINIMAPCOLORS" && !mrvmIsStartupSettingsMode()) {
							if (!applyConfiguredColorSetupValue(setupKey, mrvmValueAsString(args[1]), &errorText, false))
								throw std::runtime_error("MRSETUP(" + setupKey + ") failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
							runtimeErrorLevel() = 0;
							break;
						}
						if (!mrvmIsStartupSettingsMode()) throw std::runtime_error("MRSETUP is only allowed in settings.mrmac during startup.");
						if (setupKey == "SETTINGS_VERSION") {
							const std::string versionLiteral = trimAscii(mrvmValueAsString(args[1]));
							std::uint64_t parsedVersion = 0;

							if (!mrParsePersistenceVersion(versionLiteral, parsedVersion))
								throw std::runtime_error("MRSETUP(" + setupKey + ") failed: invalid persistence version.");
							if (parsedVersion > mrCurrentPersistenceVersion())
								throw std::runtime_error("MRSETUP(" + setupKey + ") failed: future build version " + versionLiteral + " is not supported.");
						} else if (setupKey == "SETTINGSPATH") {
							if (!setConfiguredSettingsMacroFilePath(mrvmValueAsString(args[1]), &errorText)) throw std::runtime_error("MRSETUP(SETTINGSPATH) failed: " + (errorText.empty() ? std::string("invalid path.") : errorText));
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
									                         "AUDIO_PLAYER, "
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
								                         "INDENT_STYLE, CODE_LANGUAGE, CODE_COLORING, FILE_TYPE, BINARY_RECORD_LENGTH, POST_LOAD_MACRO, PRE_SAVE_MACRO, DEFAULT_PATH, "
								                         "FORMAT_LINE, BACKUP_METHOD, BACKUP_FREQUENCY, BACKUP_EXTENSION, BACKUP_DIRECTORY, "
								                         "AUTOSAVE_INACTIVITY_SECONDS, AUTOSAVE_INTERVAL_SECONDS, BACKUP_FILES, SHOW_EOF_MARKER, "
								                         "SHOW_EOF_MARKER_EMOJI, LINE_NUMBERS_POSITION, LINE_NUM_ZERO_FILL, "
								                         "MINIMAP_POSITION, MINIMAP_WIDTH, MINIMAP_MARKER_GLYPH, GUTTERS, PERSISTENT_BLOCKS, "
								                         "CODE_FOLDING_POSITION, "
								                         "BLOCK_MOVE, DEFAULT_MODE, CURSOR_STATUS_COLOR.");
							case MRSettingsKeyClass::Version:
							case MRSettingsKeyClass::Path:
							case MRSettingsKeyClass::Global:
								if (!applyConfiguredSettingsAssignment(setupKey, mrvmValueAsString(args[1]), activePaths, &errorText))
									throw std::runtime_error("MRSETUP(" + setupKey + ") failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
								break;
							case MRSettingsKeyClass::Edit:
								if (!applyConfiguredEditSetupValue(setupKey, mrvmValueAsString(args[1]), &errorText))
									throw std::runtime_error("MRSETUP(" + setupKey + ") failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
								if (setupKey == "TAB_EXPAND") {
									BackgroundEditSession *session = currentBackgroundEditSession();
									if (session != nullptr) session->tabExpand = configuredTabExpandSetting();
									else
										g_runtimeEnv.tabExpand = configuredTabExpandSetting();
								}
								break;
							case MRSettingsKeyClass::ColorInline:
								if (!applyConfiguredColorSetupValue(setupKey, mrvmValueAsString(args[1]), &errorText))
									throw std::runtime_error("MRSETUP(" + setupKey + ") failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
								break;
						}
					runtimeErrorLevel() = 0;
				} else if (name == "MRFEPROFILE") {
					std::string errorText;
					if (!mrvmIsStartupSettingsMode()) throw std::runtime_error("MRFEPROFILE is only allowed in settings.mrmac during startup.");
					if (args.size() != 4 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || !mrvmIsStringLike(args[2]) || !mrvmIsStringLike(args[3])) throw std::runtime_error("MRFEPROFILE expects (string, string, string, string).");
					if (!applyConfiguredEditExtensionProfileDirective(mrvmValueAsString(args[0]), mrvmValueAsString(args[1]), mrvmValueAsString(args[2]), mrvmValueAsString(args[3]), &errorText)) throw std::runtime_error("MRFEPROFILE failed: " + (errorText.empty() ? std::string("invalid directive.") : errorText));
					runtimeErrorLevel() = 0;
				} else if (name == "MRCOMPILERPROFILE") {
					std::string errorText;
					if (!mrvmIsStartupSettingsMode()) throw std::runtime_error("MRCOMPILERPROFILE is only allowed in settings.mrmac during startup.");
					if (args.size() != 4 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || !mrvmIsStringLike(args[2]) || !mrvmIsStringLike(args[3])) throw std::runtime_error("MRCOMPILERPROFILE expects (string, string, string, string).");
					if (!applyConfiguredCompilerProfileDirective(mrvmValueAsString(args[0]), mrvmValueAsString(args[1]), mrvmValueAsString(args[2]), mrvmValueAsString(args[3]), &errorText)) throw std::runtime_error("MRCOMPILERPROFILE failed: " + (errorText.empty() ? std::string("invalid directive.") : errorText));
					runtimeErrorLevel() = 0;
					} else if (name == "UI_DIALOG") {
						mrvmBeginMacroUiDialog(g_runtimeEnv.runtimeKv, args);
						runtimeErrorLevel() = 0;
					} else if (name == "UI_LABEL") {
						mrvmAddMacroUiLabel(g_runtimeEnv.runtimeKv, args);
						runtimeErrorLevel() = 0;
					} else if (name == "UI_BUTTON") {
						mrvmAddMacroUiButton(g_runtimeEnv.runtimeKv, args);
						runtimeErrorLevel() = 0;
					} else if (name == "UI_DISPLAY") {
						mrvmAddMacroUiDisplay(g_runtimeEnv.runtimeKv, args);
						runtimeErrorLevel() = 0;
					} else if (name == "UI_INPUT") {
						mrvmAddMacroUiInput(g_runtimeEnv.runtimeKv, args);
						runtimeErrorLevel() = 0;
					} else if (name == "UI_LISTBOX") {
						mrvmAddMacroUiListBox(g_runtimeEnv.runtimeKv, args);
						runtimeErrorLevel() = 0;
					} else if (name == "UI_GRID") {
						mrvmAddMacroUiGrid(g_runtimeEnv.runtimeKv, args);
						runtimeErrorLevel() = 0;
					} else if (name == "UI_LIST_CLEAR") {
						mrvmClearMacroUiItemList(g_runtimeEnv.runtimeKv, args);
						runtimeErrorLevel() = 0;
					} else if (name == "UI_LIST_ADD") {
						mrvmAddMacroUiItemListValue(g_runtimeEnv.runtimeKv, args);
						runtimeErrorLevel() = 0;
					} else if (name == "UI_MODELESS_ON") {
						mrvmBindMacroModelessButton(g_runtimeEnv.runtimeKv, args);
						runtimeErrorLevel() = 0;
				} else if (name == "UI_MODELESS_SHOW") {
					showMacroModelessDialog(args);
				} else if (name == "UI_MODELESS_UPDATE") {
					updateMacroModelessDialog(args);
				} else if (name == "UI_MODELESS_DISPLAY") {
					updateMacroModelessDisplayLine(args);
				} else if (name == "UI_MODELESS_CLOSE") {
					closeMacroModelessDialog(args);
				} else if (name == "EXEC_SESSION_LIST") {
					listExecSessionClosures(args);
				} else if (name == "EXEC_SESSION_STOP") {
					stopExecSessionClosure(args);
				} else if (name == "CREATE_GLOBAL_STR" || name == "SET_GLOBAL_STR") {
					if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error(name + " expects (string, string).");
					setGlobalValue(mrvmValueAsString(args[0]), TYPE_STR, mrvmMakeString(mrvmValueAsString(args[1])));
				} else if (name == "SET_GLOBAL_INT") {
					if (args.size() != 2 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SET_GLOBAL_INT expects (string, int).");
					setGlobalValue(mrvmValueAsString(args[0]), TYPE_INT, mrvmMakeInt(args[1].i));
				} else if (name == "SET_GLOBAL_HASH") {
					if (args.size() != 2 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_HASH) throw std::runtime_error("SET_GLOBAL_HASH expects (string, hash).");
					setGlobalValueFromStore(mrvmValueAsString(args[0]), TYPE_HASH, args[1], *mHashStore);
				} else if (name == "MARQUEE" || name == "MARQUEE_WARNING" || name == "MARQUEE_ERROR" || name == "MAKE_MESSAGE" || name == "UI_MESSAGEBOX") {
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
					millis = normalizeDelayMillis(mrvmValueAsInt(args[0]));
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
					if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("LOAD_MACRO_FILE expects one string argument.");
					loadMacroFileIntoRegistry(mrvmValueAsString(args[0]), nullptr);
				} else if (name == "UNLOAD_MACRO") {
					if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("UNLOAD_MACRO expects one string argument.");
					unloadMacroFromRegistry(mrvmValueAsString(args[0]));
				} else if (name == "CHANGE_DIR") {
					if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("CHANGE_DIR expects one string argument.");
					if (mrvmChangeDirectoryPath(mrvmValueAsString(args[0]))) runtimeErrorLevel() = 0;
					else
						runtimeErrorLevel() = errno != 0 ? errno : 1;
				} else if (name == "DEL_FILE") {
					if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("DEL_FILE expects one string argument.");
					if (mrvmDeleteFilePath(mrvmValueAsString(args[0]))) runtimeErrorLevel() = 0;
					else
						runtimeErrorLevel() = errno != 0 ? errno : 1;
				} else if (name == "SET_FILE_ATTR") {
					struct stat st;
					mode_t modeBits;
					std::string path;
					if (args.size() != 2 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SET_FILE_ATTR expects (string, int).");
					path = mrvmProcessExpandUserPath(mrvmValueAsString(args[0]));
					if (::stat(path.c_str(), &st) != 0) {
						runtimeErrorLevel() = errno != 0 ? errno : 1;
						continue;
					}
					modeBits = st.st_mode;
					if ((mrvmValueAsInt(args[1]) & 0x01) != 0) modeBits &= static_cast<mode_t>(~(S_IWUSR | S_IWGRP | S_IWOTH));
					else
						modeBits |= static_cast<mode_t>(S_IWUSR);
					runtimeErrorLevel() = ::chmod(path.c_str(), modeBits) == 0 ? 0 : (errno != 0 ? errno : 1);
				} else if (name == "SHELL_TO_OS") {
					int exitCode = 0;
					if (args.size() != 2 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SHELL_TO_OS expects (string, int).");
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					(void)mrvmUiNewScreen();
					exitCode = mrvmRunShellCommand(mrvmValueAsString(args[0]), configuredShellExecutablePath());
					(void)mrvmUiNewScreen();
					runtimeErrorLevel() = exitCode;
				} else if (name == "FORK") {
					std::vector<std::string> forkArguments;
					if (args.empty()) throw std::runtime_error("FORK expects at least one string argument.");
					forkArguments.reserve(args.size());
					for (const Value &arg : args) {
						if (!mrvmIsStringLike(arg)) throw std::runtime_error("FORK expects string arguments.");
						forkArguments.push_back(mrvmValueAsString(arg));
					}
					runtimeErrorLevel() = mrvmForkProcess(forkArguments, runtimeGlobalIntValue("MR_BUILD_SOURCE_BUFFER_ID"), runtimeGlobalStringValue("MR_BUILD_SOURCE_PATH"), runtimeGlobalStringValue("MR_BUILD_PDF_PATH"));
				} else if (name == "WRITE_SOD") {
					if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("WRITE_SOD expects one string argument.");
					mrLogMessage(mrvmValueAsString(args[0]));
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
					if (!args.empty()) returnCode = mrvmValueAsInt(args[0]);
					runtimeErrorLevel() = returnCode;
					(void)dispatchApplicationCommandEvent(cmQuit);
				} else if (name == "LOAD_FILE") {
					MREditWindow *win;
					std::string path;
					if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("LOAD_FILE expects one string argument.");
					path = mrvmProcessExpandUserPath(mrvmValueAsString(args[0]));
					win = activeMacroEditWindow();
					if (win == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					if (!mrvmFileExistsPath(path)) {
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
					if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("LOAD_BLOCK expects zero or one string argument.");
					if (win == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					path = mrvmProcessExpandUserPath(mrvmValueAsString(args[0]));
					if (!mrvmFileExistsPath(path) || !mrvmEditorLoadBlockFromFile(win, path)) {
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
					if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("SAVE_BLOCK expects zero or one string argument.");
					if (win == nullptr || editor == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					path = mrvmProcessExpandUserPath(mrvmValueAsString(args[0]));
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
					if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("REPLACE expects one string argument.");
					editor = currentEditor();
					session = currentBackgroundEditSession();
					if (editor == nullptr && session == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					if (editor != nullptr) replaced = replaceLastSearch(editor, mrvmValueAsString(args[0]));
					else
						replaced = replaceLastSearchBackground(mrvmValueAsString(args[0]));
					runtimeErrorLevel() = replaced ? 0 : 1010;
					} else if (name == "TEXT") {
						MRFileEditor *editor;
						if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("TEXT expects one string argument.");
						editor = currentEditor();
						if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
							runtimeErrorLevel() = 1001;
							continue;
						}
						insertEditorText(editor, mrvmValueAsString(args[0]));
						runtimeErrorLevel() = 0;
					} else if (name == "SET_CLIPBOARD_TEXT") {
						std::string text;
						if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("SET_CLIPBOARD_TEXT expects one string argument.");
						text = mrvmValueAsString(args[0]);
						TClipboard::setText(TStringView(text.data(), text.size()));
						runtimeErrorLevel() = 0;
					} else if (name == "KEY_IN") {
					if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("KEY_IN expects one string argument.");
					if (!mrvmReplayKeyInputSequence(mrvmValueAsString(args[0]))) {
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
					runtimeErrorLevel() = pushQueuedKeyPair(mrvmValueAsInt(args[0]), mrvmValueAsInt(args[1])) ? 0 : 1010;
				} else if (name == "PASS_KEY") {
					if (args.size() != 2 || args[0].type != TYPE_INT || args[1].type != TYPE_INT) throw std::runtime_error("PASS_KEY expects two integer arguments.");
					runtimeErrorLevel() = mrvmPassMacroKeyPairToUi(mrvmValueAsInt(args[0]), mrvmValueAsInt(args[1])) ? 0 : 1010;
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
					if (args.size() != 3 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("FLABEL expects (string, int, int).");
					keyNumber = mrvmValueAsInt(args[1]);
					mode = mrvmValueAsInt(args[2]);
					if (keyNumber <= 0 || keyNumber >= 49) {
						runtimeErrorLevel() = 1010;
						continue;
					}
					if (mode == 255) mode = currentUiMacroMode();
					if (mode == MACRO_MODE_DOS_SHELL) frame.shellLabels[static_cast<std::size_t>(keyNumber)] = mrvmValueAsString(args[0]);
					else
						frame.editLabels[static_cast<std::size_t>(keyNumber)] = mrvmValueAsString(args[0]);
					applyFunctionLabelState();
					runtimeErrorLevel() = 0;
				} else if (name == "MACRO_TO_KEY") {
					TKey key;
					int mode = MACRO_MODE_EDIT;
					MRVMExplicitKeyBinding binding;
					std::string refreshError;
					if (args.size() != 3 || !mrvmIsStringLike(args[1]) || args[2].type != TYPE_INT) throw std::runtime_error("MACRO_TO_KEY expects (key, string, int).");
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					if (!mrvmParseBindingKeyValue(args[0], key) || !mrvmParseBindingModeValue(mrvmValueAsInt(args[2]), mode)) {
						runtimeErrorLevel() = 1010;
						continue;
					}
					mrvmRemoveExplicitBindingsForKey(g_runtimeEnv.explicitKeyBindings, key, mode);
					binding.key = key;
					binding.mode = mode;
					binding.kind = MRVMExplicitBindingKind::MacroSpec;
					binding.macroSpec = mrvmValueAsString(args[1]);
					g_runtimeEnv.explicitKeyBindings.push_back(binding);
					if (!mrvmUiRefreshRuntimeMenus(&refreshError)) throw std::runtime_error("MACRO_TO_KEY could not refresh runtime menus: " + (refreshError.empty() ? std::string("unknown error.") : refreshError));
					runtimeErrorLevel() = 0;
				} else if (name == "CMD_TO_KEY") {
					TKey key;
					int mode = MACRO_MODE_EDIT;
					MRVMExplicitKeyBinding binding;
					if (args.size() != 3 || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("CMD_TO_KEY expects (key, int, int).");
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					if (!mrvmParseBindingKeyValue(args[0], key) || !mrvmParseBindingModeValue(mrvmValueAsInt(args[2]), mode)) {
						runtimeErrorLevel() = 1010;
						continue;
					}
					mrvmRemoveExplicitBindingsForKey(g_runtimeEnv.explicitKeyBindings, key, mode);
					binding.key = key;
					binding.mode = mode;
					binding.kind = MRVMExplicitBindingKind::Command;
					binding.commandId = mrvmValueAsInt(args[1]);
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
					if (!mrvmParseBindingKeyValue(args[0], key) || !mrvmParseBindingModeValue(mrvmValueAsInt(args[1]), mode)) {
						runtimeErrorLevel() = 1010;
						continue;
					}
					mrvmRemoveExplicitBindingsForKey(g_runtimeEnv.explicitKeyBindings, key, mode);
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
					if (args.size() == 3 && !mrvmParseBindingModeValue(mrvmValueAsInt(args[2]), mode)) {
						runtimeErrorLevel() = 1010;
						continue;
					}
					if (!mrvmKeyPairToTKey(mrvmValueAsInt(args[0]), mrvmValueAsInt(args[1]), key, text, textLength, textByte)) {
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
					if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("PUT_LINE expects one string argument.");
					editor = currentEditor();
					if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					replaceEditorLine(editor, mrvmValueAsString(args[0]));
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
					deleteEditorChars(editor, mrvmValueAsInt(args[0]));
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
				} else if (const char *actionId = mrvmKeymapActionIdForMacroCommand(name)) {
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
					if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("EXTEND_BLOCK_BY_MOTION expects one key sequence string argument.");
					if (currentBackgroundEditSession() != nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					runtimeErrorLevel() = dispatchMRKeymapAction("MRMAC_BLOCK_EXTEND_BY_MOTION", mrvmValueAsString(args[0])) ? 0 : 1001;
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
					runtimeErrorLevel() = gotoEditorLine(editor, mrvmValueAsInt(args[0])) ? 0 : 1010;
				} else if (name == "GOTO_COL") {
					MRFileEditor *editor = currentEditor();
					if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("GOTO_COL expects one integer argument.");
					if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
						runtimeErrorLevel() = 1001;
						continue;
					}
					runtimeErrorLevel() = gotoEditorCol(editor, mrvmValueAsInt(args[0])) ? 0 : 1010;
				} else if (name == "SWITCH_WINDOW") {
					int deferredError = 0;
					if (queueDeferredUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
					if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("SWITCH_WINDOW expects one integer argument.");
					runtimeErrorLevel() = mrvmUiSwitchWindow(mrvmValueAsInt(args[0])) ? 0 : 1001;
				} else if (name == "SIZE_WINDOW") {
					int deferredError = 0;
					if (queueDeferredUiProcedure(name, args, deferredError)) {
						runtimeErrorLevel() = deferredError;
						continue;
					}
					if (args.size() != 4 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT) throw std::runtime_error("SIZE_WINDOW expects four integer arguments.");
					runtimeErrorLevel() = mrvmUiSizeCurrentWindow(mrvmValueAsInt(args[0]), mrvmValueAsInt(args[1]), mrvmValueAsInt(args[2]), mrvmValueAsInt(args[3])) ? 0 : 1010;
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
					MacroRef macroRef;
					bool backgroundStaged = currentBackgroundEditSession() != nullptr;

					if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("RUN_MACRO expects one string argument.");

					spec = mrvmValueAsString(args[0]);
					if (!mrvmParseRunMacroSpec(spec, filePart, macroPart, paramPart)) {
						runtimeErrorLevel() = 5001;
						continue;
					}

					macroKey = mrvmUpperKey(macroPart);
					if (!filePart.empty()) targetFileKey = resolveLoadedFileKeyForSpec(filePart);
					if (!filePart.empty() && targetFileKey.empty()) targetFileKey = mrvmMakeMacroFileKey(filePart);

					if (!readLoadedMacroByKey(macroKey, macroRef) || (!targetFileKey.empty() && macroRef.fileKey != targetFileKey)) {
						if (backgroundStaged) {
							runtimeErrorLevel() = 5001;
							continue;
						}
						if (!filePart.empty()) {
							if (!loadMacroFileIntoRegistry(filePart, &targetFileKey)) continue;
						} else {
							if (!loadMacroFileIntoRegistry(macroPart, &targetFileKey)) continue;
						}
						static_cast<void>(readLoadedMacroByKey(macroKey, macroRef));
					}

					if (macroRef.displayName.empty() || (!targetFileKey.empty() && macroRef.fileKey != targetFileKey)) {
						runtimeErrorLevel() = 5001;
						continue;
					}
					if (!executeLoadedMacroWithConfiguredKeymapBatch(macroKey, paramPart, &log)) continue;
				} else {
					throw std::runtime_error("Unknown procedure: " + name);
				}
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

void mrvmUiSyncLinkedWindowsFrom(MREditWindow *window) {
	syncLinkedWindowsFrom(window);
}

std::string mrvmUiMenuKeyLabelForMacroSpec(const std::string &macroSpec) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	std::string filePart;
	std::string macroPart;
	std::string paramPart;
	std::string targetFileKey;
	const std::string targetMacroKey = [&]() {
		mrvmParseRunMacroSpec(macroSpec, filePart, macroPart, paramPart);
		return mrvmUpperKey(macroPart);
	}();
	const int mode = currentUiMacroMode();

	if (targetMacroKey.empty()) return std::string();
	if (!filePart.empty()) targetFileKey = resolveLoadedFileKeyForSpec(filePart);
	if (!filePart.empty() && targetFileKey.empty()) targetFileKey = mrvmMakeMacroFileKey(filePart);
	for (auto it = g_runtimeEnv.explicitKeyBindings.rbegin(); it != g_runtimeEnv.explicitKeyBindings.rend(); ++it) {
		if (it->kind != MRVMExplicitBindingKind::MacroSpec || !mrvmBindingModeMatches(it->mode, mode)) continue;
		if (!macroSpecTargetsLoadedMacro(it->macroSpec, targetFileKey, targetMacroKey)) continue;
		return mrvmMenuLabelFromBindingKey(it->key);
	}
	{
		MacroRef macroRef;
		if (!readLoadedMacroByKey(targetMacroKey, macroRef)) return std::string();
		if (!targetFileKey.empty() && macroRef.fileKey != targetFileKey) return std::string();
		if (!macroAllowsUiMode(macroRef, mode) || !macroRef.hasAssignedKey) return std::string();
		if (!macroRef.assignedKeySpec.empty()) return mrvmNormalizeMenuKeySpec(macroRef.assignedKeySpec);
		return mrvmMenuLabelFromBindingKey(macroRef.assignedKey);
	}
}

struct UiRenderFacade {
	static bool renderDeferredCommand(const MRMacroDeferredUiCommand &command) {
		return mrvmUiScreenRenderDeferredCommand(command);
	}
};

bool mrvmUiRenderFacadeRenderDeferredCommand(const MRMacroDeferredUiCommand &command) {
	return UiRenderFacade::renderDeferredCommand(command);
}

bool mrvmUiRenderDeferredCommand(const MRMacroDeferredUiCommand &command) {
	return mrvmUiRenderFacadeRenderDeferredCommand(command);
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
		const std::vector<std::string> orderValues = macroCatalogMacroOrder();
		for (std::size_t i = orderValues.size(); i > 0; --i) {
			const std::string &macroKey = orderValues[i - 1];
			MacroRef macroRef;

			if (!readLoadedMacroByKey(macroKey, macroRef)) continue;
			if (!macroRef.hasAssignedKey) continue;
			if (!macroAllowsUiMode(macroRef, mode)) continue;
			if (!mrvmBindingKeysEqual(pressed, macroRef.assignedKey)) continue;

			mrvmLogCalculatorHotkeyState("vm-loaded-match", pressed, macroRef.displayName);
			if (traceSnippetKey) mrLogMessage(("KEYDBG vm loaded binding match macro=" + macroRef.displayName).c_str());
			executedMacroName = macroRef.displayName;
			executeLoadedMacroWithConfiguredKeymapBatch(macroKey, std::string(), logLines);
			return true;
		}
		return false;
	};

	if (rawCtrlSpace) pressed = TKey(static_cast<ushort>(' '), kbCtrlShift);
	traceSnippetKey = rawCtrlSpace || (pressed.code == static_cast<ushort>(' ') && (pressed.mods & kbCtrlShift) != 0) || (pressed.code == static_cast<ushort>('@') && (pressed.mods & kbCtrlShift) != 0);
	executedMacroName.clear();
	if (logLines != nullptr) logLines->clear();
	if (mrvmKeyReplayActive()) return false;
	if (traceSnippetKey) {
		char line[512];
		std::snprintf(line, sizeof(line), "KEYDBG vm assigned-key rawCode=0x%04X rawMods=0x%04X code=0x%04X mods=0x%04X mode=%d explicit=%zu loaded=%zu indexed=%zu", static_cast<unsigned>(keyCode), static_cast<unsigned>(controlKeyState), static_cast<unsigned>(pressed.code), static_cast<unsigned>(pressed.mods), mode, g_runtimeEnv.explicitKeyBindings.size(), macroCatalogLoadedMacroCount(), macroCatalogIndexedBindingCount());
		mrLogMessage(line);
	}
	mrvmLogCalculatorHotkeyState("vm-enter", pressed);
	if (executeExplicitKeyBinding(pressed, mode, logLines)) {
		executedMacroName = "<bound>";
		mrvmLogCalculatorHotkeyState("vm-explicit-consumed", pressed);
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
	mrvmLogCalculatorHotkeyState("vm-indexed-loaded", pressed);
	if (traceSnippetKey) mrLogMessage("KEYDBG vm assigned-key indexed binding loaded");
	return dispatchLoadedBinding();
}
