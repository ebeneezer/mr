#include "MRVMEditor.hpp"

#include "../../vm/MRVMRuntimeInternal.hpp"
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
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mrvm_runtime {

bool setEditorCursor(MRFileEditor *editor, uint target, int requestedVisualColumn) {
	MREditWindow *win;
	if (editor == nullptr) return backgroundSetCursor(target);
	if (target > editor->bufferLength()) target = editor->bufferLength();
	if (requestedVisualColumn >= 0) editor->setCursorOffsetAtVisualColumn(target, requestedVisualColumn);
	else
		editor->setCursorOffset(target, 0);
	win = currentEditorCommandWindow();
	if (win != nullptr && win->isBlockMarking()) win->refreshBlockVisual();
	else
		editor->revealCursor(True);
	return true;
}

bool moveEditorLeft(MRFileEditor *editor) {
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
	if (editor->freeCursorMovementEnabled() && !editor->hasTextSelection() && editor->displayedCursorColumn() > editor->actualCursorVisualColumn(editor->cursorOffset())) return setEditorCursor(editor, editor->cursorOffset(), editor->displayedCursorColumn() - 1);
	if (editor->cursorOffset() > start) target = editor->prevCharOffset(editor->cursorOffset());
	else if (start > 0)
		target = editor->lineEndOffset(editor->prevLineOffset(start));
	else
		target = 0;
	return setEditorCursor(editor, target);
}

bool moveEditorRight(MRFileEditor *editor) {
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
	if (editor->freeCursorMovementEnabled() && !editor->hasTextSelection() && editor->cursorOffset() == lineEnd) return setEditorCursor(editor, editor->cursorOffset(), editor->displayedCursorColumn() + 1);
	if (editor->cursorOffset() < lineEnd) target = editor->nextCharOffset(editor->cursorOffset());
	else
		target = editor->cursorOffset();
	return setEditorCursor(editor, target);
}

bool moveEditorUp(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return false;
		return setEditorCursor(nullptr, static_cast<uint>(backgroundLineMoveOffset(session->cursorOffset, -1)));
	}
	return setEditorCursor(editor, editor->lineMoveOffset(editor->cursorOffset(), -1));
}

bool moveEditorDown(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr) return false;
		return setEditorCursor(nullptr, static_cast<uint>(backgroundLineMoveOffset(session->cursorOffset, 1)));
	}
	return setEditorCursor(editor, editor->lineMoveOffset(editor->cursorOffset(), 1));
}

bool moveEditorHome(MRFileEditor *editor) {
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

bool moveEditorEol(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session != nullptr ? setEditorCursor(nullptr, static_cast<uint>(session->document.lineEnd(session->cursorOffset))) : false;
	return setEditorCursor(editor, editor->lineEndOffset(editor->cursorOffset()));
}

bool moveEditorTof(MRFileEditor *editor) {
	if (editor == nullptr) return currentBackgroundEditSession() != nullptr ? setEditorCursor(nullptr, 0) : false;
	return setEditorCursor(editor, 0);
}

bool moveEditorEof(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session != nullptr ? setEditorCursor(nullptr, static_cast<uint>(session->document.length())) : false;
	return setEditorCursor(editor, editor->bufferLength());
}

bool moveEditorWordLeft(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session != nullptr ? setEditorCursor(nullptr, static_cast<uint>(backgroundPrevWordOffset(session->cursorOffset))) : false;
	return setEditorCursor(editor, editor->prevWordOffset(editor->cursorOffset()));
}

bool moveEditorWordRight(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session != nullptr ? setEditorCursor(nullptr, static_cast<uint>(backgroundNextWordOffset(session->cursorOffset))) : false;
	return setEditorCursor(editor, editor->nextWordOffset(editor->cursorOffset()));
}

bool moveEditorFirstWord(MRFileEditor *editor) {
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

bool gotoEditorLine(MRFileEditor *editor, int lineNum) {
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

bool gotoEditorCol(MRFileEditor *editor, int colNum) {
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

bool currentEditorAtEof(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session == nullptr || session->cursorOffset >= session->document.length();
	return editor->cursorOffset() >= editor->bufferLength();
}

bool currentEditorAtEol(MRFileEditor *editor) {
	uint lineEnd;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session == nullptr || session->cursorOffset >= session->document.lineEnd(session->cursorOffset);
	lineEnd = editor->lineEndOffset(editor->cursorOffset());
	return editor->cursorOffset() >= lineEnd;
}

int currentEditorRow(MRFileEditor *editor) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) return session != nullptr ? static_cast<int>(session->document.lineIndex(session->cursorOffset) + 1) : 1;
	return editor->currentViewRow();
}

int currentEditorPage(MRFileEditor *editor) {
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

int currentEditorPageLine(MRFileEditor *editor) {
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
	std::vector<std::size_t> marks = mrvmUiCopyWindowMarkStack(win);
	marks.push_back(editor->cursorOffset());
	mrvmUiReplaceWindowMarkStack(win, marks);
	return true;
}

static bool validRandomAccessMarkIndex(int index) noexcept {
	return index >= 1 && index <= 9;
}

bool gotoEditorMark(MREditWindow *win, MRFileEditor *editor) {
	uint pos;
	std::vector<std::size_t> marks;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor == nullptr) {
		if (session == nullptr || session->markStack.empty()) return false;
		pos = session->markStack.back();
		session->markStack.pop_back();
		return setEditorCursor(nullptr, pos);
	}
	if (win == nullptr) return false;
	marks = mrvmUiCopyWindowMarkStack(win);
	if (marks.empty()) return false;
	pos = static_cast<uint>(marks.back());
	marks.pop_back();
	mrvmUiReplaceWindowMarkStack(win, marks);
	return setEditorCursor(editor, pos);
}

bool popEditorMark(MREditWindow *win) {
	std::vector<std::size_t> marks;
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (win == nullptr) {
		if (session == nullptr || session->markStack.empty()) return false;
		session->markStack.pop_back();
		return true;
	}
	marks = mrvmUiCopyWindowMarkStack(win);
	if (marks.empty()) return false;
	marks.pop_back();
	mrvmUiReplaceWindowMarkStack(win, marks);
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
	std::vector<std::string> marks = mrvmRuntimeStateStringList("randomAccessMarks", std::to_string(win->bufferId()));
	if (marks.size() < 10) marks.resize(10);
	marks[static_cast<std::size_t>(index)] = std::to_string(editor->cursorOffset());
	mrvmStoreRuntimeStateStringList("randomAccessMarks", std::to_string(win->bufferId()), marks);
	return true;
}

bool gotoEditorRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index) {
	BackgroundEditSession *session = currentBackgroundEditSession();

	if (!validRandomAccessMarkIndex(index)) return false;
	if (editor == nullptr) {
		if (session == nullptr) return false;
		const std::optional<uint> &pos = session->randomAccessMarks[static_cast<std::size_t>(index)];
		return pos.has_value() ? setEditorCursor(nullptr, *pos) : false;
	}
	if (win == nullptr) return false;
	const std::vector<std::string> marks = mrvmRuntimeStateStringList("randomAccessMarks", std::to_string(win->bufferId()));
	if (static_cast<std::size_t>(index) >= marks.size() || marks[static_cast<std::size_t>(index)].empty()) return false;
	char *end = nullptr;
	const unsigned long pos = std::strtoul(marks[static_cast<std::size_t>(index)].c_str(), &end, 10);
	return end != marks[static_cast<std::size_t>(index)].c_str() && *end == '\0' ? setEditorCursor(editor, static_cast<uint>(pos)) : false;
}

bool moveEditorPageUp(MRFileEditor *editor) {
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

bool moveEditorPageDown(MRFileEditor *editor) {
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

bool replaceEditorBuffer(MRFileEditor *editor, const std::string &text, std::size_t cursorPos) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (editor != nullptr) return editor->replaceWholeBuffer(text, cursorPos);
	if (session == nullptr) return false;
	return backgroundReplaceRange(mr::editor::Range(0, session->document.length()), text, cursorPos);
}

int lineIndexForPtr(MRFileEditor *editor, uint ptr) {
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

int blockStatusValue(MREditWindow *win) {
	return win != nullptr ? win->blockStatus() : 0;
}

bool blockMarkingValue(MREditWindow *win) {
	return win != nullptr && win->isBlockMarking();
}

int blockLine1Value(MREditWindow *win, MRFileEditor *editor) {
	(void)editor;
	return win != nullptr ? win->blockLine1() : 0;
}

int blockLine2Value(MREditWindow *win, MRFileEditor *editor) {
	(void)editor;
	return win != nullptr ? win->blockLine2() : 0;
}

int blockCol1Value(MREditWindow *win, MRFileEditor *editor) {
	(void)editor;
	return win != nullptr ? win->blockCol1() : 0;
}

int blockCol2Value(MREditWindow *win, MRFileEditor *editor) {
	(void)editor;
	return win != nullptr ? win->blockCol2() : 0;
}

bool beginCurrentBlockMode(int mode) {
	MREditWindow *win = currentEditorCommandWindow();
	if (win == nullptr) return false;
	if (mode == MREditWindow::bmColumn) win->beginColumnBlock();
	else if (mode == MREditWindow::bmStream)
		win->beginStreamBlock();
	else
		win->beginLineBlock();
	return true;
}

bool endCurrentBlockMode() {
	MREditWindow *win = currentEditorCommandWindow();
	if (win == nullptr) return false;
	win->endBlock();
	return true;
}

bool clearCurrentBlockMode() {
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (session != nullptr) session->clearSelection();
	MREditWindow *win = currentEditorCommandWindow();
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

void collectEditWindowByIndex(TView *view, void *arg) {
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

void countEditWindowProc(TView *view, void *arg) {
	int *count = static_cast<int *>(arg);
	if (count != nullptr && dynamic_cast<MREditWindow *>(view) != nullptr) ++(*count);
}

int countEditWindows() {
	int count = 0;
	if (TProgram::deskTop == nullptr) return 0;
	TProgram::deskTop->forEach(countEditWindowProc, &count);
	return count;
}

void collectEditWindowsProc(TView *view, void *arg) {
	std::vector<MREditWindow *> *windows = static_cast<std::vector<MREditWindow *> *>(arg);
	MREditWindow *win = dynamic_cast<MREditWindow *>(view);
	if (windows != nullptr && win != nullptr) windows->push_back(win);
}

std::vector<MREditWindow *> allEditWindows() {
	std::vector<MREditWindow *> windows;
	if (TProgram::deskTop != nullptr) TProgram::deskTop->forEach(collectEditWindowsProc, &windows);
	return windows;
}

void cleanupWindowLinkGroups() {
	std::vector<MREditWindow *> windows = allEditWindows();
	std::set<std::string> live;
	std::map<int, int> counts;
	std::vector<std::string> keys = mrvmRuntimeStateKeys("windowLinkGroups");

	for (std::size_t index = 0; index < windows.size(); ++index)
		live.insert(std::to_string(windows[index]->bufferId()));

	for (const std::string &key : keys) {
		if (live.find(key) == live.end()) {
			static_cast<void>(mrvmEraseRuntimeStateValue("windowLinkGroups", key));
			continue;
		}
		++counts[mrvmRuntimeStateInt("windowLinkGroups", key)];
	}

	keys = mrvmRuntimeStateKeys("windowLinkGroups");
	for (const std::string &key : keys)
		if (counts[mrvmRuntimeStateInt("windowLinkGroups", key)] < 2) static_cast<void>(mrvmEraseRuntimeStateValue("windowLinkGroups", key));
}

int windowLinkGroupOf(MREditWindow *win) {
	if (win == nullptr) return 0;
	cleanupWindowLinkGroups();
	return mrvmRuntimeStateInt("windowLinkGroups", std::to_string(win->bufferId()));
}

bool isWindowLinked(MREditWindow *win) {
	return windowLinkGroupOf(win) != 0;
}

int currentLinkStatus() {
	return isWindowLinked(activeMacroEditWindow()) ? 1 : 0;
}

bool windowBufferIdentity(MREditWindow *win, std::string &fileName, std::string &text, bool &emptyUntitled) {
	MRFileEditor *editor;
	if (win == nullptr) return false;
	editor = win->getEditor();
	if (editor == nullptr) return false;
	fileName = win->currentFileName();
	text = snapshotEditorText(editor);
	emptyUntitled = fileName.empty() && text.empty();
	return true;
}

bool copyWindowBufferState(MREditWindow *src, MREditWindow *dest) {
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

bool assignLinkedWindows(MREditWindow *a, MREditWindow *b) {
	int groupA;
	int groupB;
	int targetGroup;

	if (a == nullptr || b == nullptr || a == b) return false;

	cleanupWindowLinkGroups();
	groupA = windowLinkGroupOf(a);
	groupB = windowLinkGroupOf(b);
	if (groupA != 0 && groupA == groupB) return true;

	targetGroup = groupA != 0 ? groupA : groupB;
	if (targetGroup == 0) {
		targetGroup = mrvmRuntimeStateInt("windowLinks", "nextGroupId", 1);
		mrvmStoreRuntimeStateInt("windowLinks", "nextGroupId", targetGroup + 1);
	}

	if (groupA != 0 && groupB != 0 && groupA != groupB) {
		for (const std::string &key : mrvmRuntimeStateKeys("windowLinkGroups"))
			if (mrvmRuntimeStateInt("windowLinkGroups", key) == groupB) mrvmStoreRuntimeStateInt("windowLinkGroups", key, targetGroup);
	}

	mrvmStoreRuntimeStateInt("windowLinkGroups", std::to_string(a->bufferId()), targetGroup);
	mrvmStoreRuntimeStateInt("windowLinkGroups", std::to_string(b->bufferId()), targetGroup);
	cleanupWindowLinkGroups();
	return true;
}

MREditWindow *selectLinkTargetWindow(MREditWindow *current) {
	return mrShowWindowListDialog(mrwlSelectLinkTarget, current);
}

bool prepareWindowLink(MREditWindow *current, MREditWindow *target, MREditWindow *&source, MREditWindow *&dest) {
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
	static_cast<void>(mrvmEraseRuntimeStateValue("windowLinkGroups", std::to_string(current->bufferId())));
	cleanupWindowLinkGroups();
	return true;
}

void syncLinkedWindowsFrom(MREditWindow *source) {
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

void currentEditWindowIndexProc(TView *view, void *arg) {
	CurrentEditWindowIndexLookup *lookup = static_cast<CurrentEditWindowIndexLookup *>(arg);
	MREditWindow *win = dynamic_cast<MREditWindow *>(view);
	if (lookup == nullptr || win == nullptr || lookup->result != 0) return;
	++lookup->index;
	if (win == lookup->current) lookup->result = lookup->index;
}

int currentEditWindowIndex() {
	CurrentEditWindowIndexLookup lookup;
	if (TProgram::deskTop == nullptr) return 0;
	lookup.current = activeMacroEditWindow();
	lookup.index = 0;
	lookup.result = 0;
	if (lookup.current == nullptr) return 0;
	TProgram::deskTop->forEach(currentEditWindowIndexProc, &lookup);
	return lookup.result;
}

bool currentWindowGeometry(int &x1, int &y1, int &x2, int &y2) {
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
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
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

bool carriageReturnEditor(MRFileEditor *editor) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const int indentLevel = resolvedEditFormatIndentColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentEditorIndentLevel());
	const std::string fill = buildEditIndentFill(settings, 1, indentLevel, currentRuntimeTabExpand());
	if (editor != nullptr) return editor->newLineWithIndent(fill);
	return insertEditorText(nullptr, std::string("\n") + fill);
}

} // namespace mrvm_runtime
