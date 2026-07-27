#include "MRVMEditor.hpp"

#include "../../vm/MRVMRuntimeInternal.hpp"
#include "../../vm/MRVMProcessRuntime.hpp"
#include "../../vm/MRVMValue.hpp"
#include "MRVMScreen.hpp"

#include "../../../app/commands/MRWindowCommands.hpp"
#include "../../../config/settings/MRSettingsRuntime.hpp"
#include "../../../ui/MREditWindow.hpp"
#include "../../../ui/MRFileEditor/MRFileEditor.hpp"
#include "../../../ui/MRWindowSupport.hpp"

#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TProgram
#define Uses_TView
#include <tvision/tv.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mrvm_runtime {

MREditWindow *activeMacroEditWindow() {
	if (TProgram::deskTop == nullptr || TProgram::deskTop->current == nullptr) return nullptr;
	return dynamic_cast<MREditWindow *>(TProgram::deskTop->current);
}

MRFileEditor *currentEditor() {
	return mrvmEditorCurrentEditor();
}

BackgroundEditSession *currentBackgroundEditSession() noexcept {
	return g_backgroundEditSession;
}

ExecutionState *currentExecutionState() noexcept {
	return g_executionState;
}

MRMacroExecutionSessionId currentExecutionSessionId() noexcept {
	return g_executionSessionId;
}

std::string &runtimeParameterString() noexcept {
	ExecutionState *state = currentExecutionState();
	return state != nullptr ? state->parameterString : g_runtimeEnv.parameterString;
}

int &runtimeReturnInt() noexcept {
	ExecutionState *state = currentExecutionState();
	return state != nullptr ? state->returnInt : g_runtimeEnv.returnInt;
}

std::string &runtimeReturnStr() noexcept {
	ExecutionState *state = currentExecutionState();
	return state != nullptr ? state->returnStr : g_runtimeEnv.returnStr;
}

int &runtimeErrorLevel() noexcept {
	ExecutionState *state = currentExecutionState();
	return state != nullptr ? state->errorLevel : g_runtimeEnv.errorLevel;
}

static char normalizeSearchChar(char c, bool ignoreCase) noexcept {
	if (!ignoreCase) return c;
	return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

bool backgroundMacroCancelRequested() noexcept {
	return g_backgroundMacroCancelFlag != nullptr && g_backgroundMacroCancelFlag->load(std::memory_order_acquire);
}

bool currentRuntimeIgnoreCase() noexcept {
	BackgroundEditSession *session = currentBackgroundEditSession();
	return session != nullptr ? session->ignoreCase : g_runtimeEnv.ignoreCase;
}

int currentRegexStatusValue() {
	const MRSearchDialogOptions searchOptions = configuredSearchDialogOptions();
	const MRSarDialogOptions sarOptions = configuredSarDialogOptions();

	return searchOptions.textType == MRSearchTextType::Pcre || sarOptions.textType == MRSearchTextType::Pcre ? 1 : 0;
}

bool setCurrentRegexStatus(bool enabled) {
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

bool currentRuntimeTabExpand() noexcept {
	BackgroundEditSession *session = currentBackgroundEditSession();
	return session != nullptr ? session->tabExpand : g_runtimeEnv.tabExpand;
}

void computeLineColumnForOffset(const std::string &text, std::size_t offset, int &line, int &column) {
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

SearchMatchSnapshot currentSearchMatchSnapshot() {
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

Value loadCurrentFileState(const std::string &key) {
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

std::string snapshotEditorText(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->snapshotText();
	return session != nullptr ? session->document.text() : std::string();
}

std::size_t backgroundSearchLimitForward(const mr::editor::TextDocument &document, std::size_t start, int numLines) {
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

std::size_t backgroundSearchLimitBackward(const mr::editor::TextDocument &document, std::size_t start, int numLines) {
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

bool searchEditorForward(MRFileEditor *editor, const std::string &needle, int numLines, bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd) {
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

bool searchEditorBackward(MRFileEditor *editor, const std::string &needle, int numLines, bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd) {
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

bool replaceLastSearch(MRFileEditor *editor, const std::string &replacement) {
	MREditWindow *win = currentEditorCommandWindow();
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

bool replaceLastSearchBackground(const std::string &replacement) {
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

bool backgroundReplaceRange(const mr::editor::Range &range, const std::string &text, std::size_t cursorPos) {
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

bool backgroundSetCursor(std::size_t target) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (session == nullptr) return false;
	session->cursorOffset = session->document.clampOffset(target);
	session->clearSelection();
	session->clearLastSearch();
	return true;
}

std::size_t backgroundCharPtrOffset(std::size_t lineStart, int column) {
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

std::size_t backgroundLineMoveOffset(std::size_t offset, int delta) {
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

std::size_t backgroundPrevWordOffset(std::size_t offset) {
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

std::size_t backgroundNextWordOffset(std::size_t offset) {
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

Value currentEditorCharValue() {
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

bool isVirtualChar(char c) {
	return static_cast<unsigned char>(c) == 255;
}

int nextResolvedTabDisplayColumn(const MREditSetupSettings &settings, int col) {
	return resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, col);
}

std::string expandTabsString(const std::string &value, bool toVirtuals) {
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

std::string tabsToSpacesString(const std::string &value) {
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

int expandedTabsAdjustedIndex(const std::string &value, int index) {
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

int currentEditorIndentLevel() {
	MREditWindow *win = currentEditorCommandWindow();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (win != nullptr) return win->indentLevel();
	return session != nullptr ? session->indentLevel : 1;
}

bool setCurrentEditorIndentLevel(int level) {
	MREditWindow *win = currentEditorCommandWindow();
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

bool currentEditorInsertMode() {
	MRFileEditor *editor = currentEditor();
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->insertModeEnabled();
	if (session != nullptr) return session->insertMode;
	return true;
}

bool setCurrentEditorInsertMode(bool on) {
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

std::string currentEditorLineText(MRFileEditor *editor) {
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

std::string currentEditorWord(MRFileEditor *editor, const std::string &delimiters) {
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

bool insertEditorText(MRFileEditor *editor, const std::string &text) {
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

bool replaceEditorLine(MRFileEditor *editor, const std::string &text) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->replaceCurrentLineText(text);
	if (session == nullptr) return false;
	std::size_t start = session->document.lineStart(session->cursorOffset);
	std::size_t end = session->document.lineEnd(session->cursorOffset);
	return backgroundReplaceRange(mr::editor::Range(start, end), text, start);
}

bool wordWrapEditorLine(MRFileEditor *editor) {
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

std::size_t prevCharOffsetFallback(const mr::editor::TextDocument &document, std::size_t pos) {
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

bool backspaceEditor(MRFileEditor *editor) {
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

bool deleteEditorChars(MRFileEditor *editor, int count) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->deleteCharsAtCursor(count);
	if (session == nullptr) return false;
	if (count <= 0) return true;
	std::size_t start = session->cursorOffset;
	std::size_t end = std::min(session->document.length(), start + static_cast<std::size_t>(count));
	return backgroundReplaceRange(mr::editor::Range(start, end), std::string(), start);
}

bool deleteEditorLine(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->deleteCurrentLineText();
	if (session == nullptr) return false;
	std::size_t start = session->document.lineStart(session->cursorOffset);
	std::size_t end = session->document.nextLine(session->cursorOffset);
	return backgroundReplaceRange(mr::editor::Range(start, end), std::string(), start);
}

int currentEditorColumn(MRFileEditor *editor) {
	uint lineStart;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session != nullptr ? static_cast<int>(session->document.column(session->cursorOffset) + 1) : 1;
	if (editor->freeCursorMovementEnabled()) return editor->currentColumnNumber();
	lineStart = editor->lineStartOffset(editor->cursorOffset());
	return editor->charColumn(lineStart, editor->cursorOffset()) + 1;
}

bool currentUiCursorPosition(int &x, int &y) {
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

int currentEditorLineNumber(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session != nullptr ? static_cast<int>(session->document.lineIndex(session->cursorOffset) + 1) : 1;
	return editor->currentLineNumber();
}

std::size_t currentEditorCursorOffset(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->cursorOffset();
	return session != nullptr ? session->cursorOffset : 0;
}

} // namespace mrvm_runtime
