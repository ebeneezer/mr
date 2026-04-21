#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TObject
#define Uses_TEvent
#define Uses_TRect
#define Uses_TFileDialog
#define Uses_MsgBox
#include <tvision/tv.h>

#include "MRCommandRouter.hpp"

#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "../dialogs/MRFileInformationDialog.hpp"
#include "../dialogs/MRAboutDialog.hpp"
#include "../dialogs/MRSetupDialogs.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../app/utils/MRStringUtils.hpp"
#include "../mrmac/mrvm.hpp"
#include "../app/commands/MRExternalCommand.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../dialogs/MRMacroFileDialog.hpp"
#include "../dialogs/MRWindowListDialog.hpp"
#include "../ui/TMREditWindow.hpp"
#include "../ui/TMRMenuBar.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "TMREditorApp.hpp"
#include "MRCommands.hpp"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

namespace {
bool startExternalCommandInWindow(TMREditWindow *win, const std::string &commandLine, bool replaceBuffer,
                                  bool activate, bool closeOnFailure);

struct SearchUiState {
	bool hasPrevious = false;
	std::string pattern;
	std::string replacement;
	std::size_t lastStart = 0;
	std::size_t lastEnd = 0;
};

SearchUiState g_searchUiState;

const char *placeholderCommandTitle(ushort command) {
	switch (command) {
		case cmMrFileOpen:
			return "File / Open";
		case cmMrFileLoad:
			return "File / Load";
		case cmMrFileSave:
			return "File / Save";
		case cmMrFileSaveAs:
			return "File / Save File As";
		case cmMrFileInformation:
			return "File / Information";
		case cmMrFileMerge:
			return "File / Merge";
		case cmMrFilePrint:
			return "File / Print";
		case cmMrFileShellToDos:
			return "File / Shell to DOS";

		case cmMrEditUndo:
			return "Edit / Undo";
		case cmMrEditRedo:
			return "Edit / Redo";
		case cmMrEditCutToBuffer:
			return "Edit / Cut to buffer";
		case cmMrEditCopyToBuffer:
			return "Edit / Copy to buffer";
		case cmMrEditAppendToBuffer:
			return "Edit / Append to buffer";
		case cmMrEditCutAndAppendToBuffer:
			return "Edit / Cut and append to buffer";
		case cmMrEditPasteFromBuffer:
			return "Edit / Paste from buffer";
		case cmMrEditRepeatCommand:
			return "Edit / Repeat command";

		case cmMrWindowClose:
			return "Window / Close";
		case cmMrWindowSplit:
			return "Window / Split";
		case cmMrWindowList:
			return "Window / List";
		case cmMrWindowNext:
			return "Window / Next";
		case cmMrWindowPrevious:
			return "Window / Previous";
		case cmMrWindowAdjacent:
			return "Window / Adjacent";
		case cmMrWindowHide:
			return "Window / Hide";
		case cmMrWindowModifySize:
			return "Window / Modify size";
		case cmMrWindowZoom:
			return "Window / Zoom";
		case cmMrWindowMinimize:
			return "Window / Minimize";
		case cmMrWindowLink:
			return "Window / Link";
		case cmMrWindowUnlink:
			return "Window / Unlink";

		case cmMrBlockCopy:
			return "Block / Copy block";
		case cmMrBlockMove:
			return "Block / Move block";
		case cmMrBlockDelete:
			return "Block / Delete block";
		case cmMrBlockSaveToDisk:
			return "Block / Save block to disk";
		case cmMrBlockIndent:
			return "Block / Indent block";
		case cmMrBlockUndent:
			return "Block / Undent block";
		case cmMrBlockWindowCopy:
			return "Block / Window copy";
		case cmMrBlockWindowMove:
			return "Block / Window move";
		case cmMrBlockMarkLines:
			return "Block / Mark lines of text";
		case cmMrBlockMarkColumns:
			return "Block / Mark columns of text";
		case cmMrBlockMarkStream:
			return "Block / Mark stream of text";
		case cmMrBlockEndMarking:
			return "Block / End marking";
		case cmMrBlockTurnMarkingOff:
			return "Block / Turn marking off";
		case cmMrBlockPersistent:
			return "Block / Persistent blocks";

		case cmMrSearchFindText:
			return "Search / Search for text";
		case cmMrSearchReplace:
			return "Search / Search and Replace";
		case cmMrSearchRepeatPrevious:
			return "Search / Repeat previous search";
		case cmMrSearchPushMarker:
			return "Search / Push position onto marker stack";
		case cmMrSearchGetMarker:
			return "Search / Get position from marker stack";
		case cmMrSearchSetRandomAccessMark:
			return "Search / Set random access mark";
		case cmMrSearchRetrieveRandomAccessMark:
			return "Search / Retrieve random access mark";
		case cmMrSearchGotoLineNumber:
			return "Search / Goto line number";

		case cmMrTextLayout:
			return "Text / Layout";
		case cmMrTextUpperCaseMenu:
			return "Text / Upper case";
		case cmMrTextLowerCaseMenu:
			return "Text / Lower case";
		case cmMrTextCenterLine:
			return "Text / Center line";
		case cmMrTextTimeDateStamp:
			return "Text / Time/Date stamp";
		case cmMrTextReformatParagraph:
			return "Text / Re-format paragraph";
		case cmMrTextUpperCasePlaceholder:
			return "Text / Upper case";
		case cmMrTextLowerCasePlaceholder:
			return "Text / Lower case";

		case cmMrOtherMacroManager:
			return "Other / Macro manager";
		case cmMrOtherExecuteProgram:
			return "Other / Execute program";
		case cmMrOtherStopProgram:
			return "Other / Stop current program";
		case cmMrOtherRestartProgram:
			return "Other / Restart current program";
		case cmMrOtherClearOutput:
			return "Other / Clear current output";
		case cmMrOtherFindNextCompilerError:
			return "Other / Find next compiler error";
		case cmMrOtherMatchBraceOrParen:
			return "Other / Match brace or paren";
		case cmMrOtherAsciiTable:
			return "Other / Ascii table";

		case cmMrHelpContents:
			return "Help / Table of contents";
		case cmMrHelpKeys:
			return "Help / Keys";
		case cmMrHelpDetailedIndex:
			return "Help / Detailed index";
		case cmMrHelpPreviousTopic:
			return "Help / Previous topic";
		case cmMrHelpAbout:
			return "Help / About";

		case cmMrDevCancelMacroTasks:
			return "Dev / Cancel background macros";
		case cmMrDevHeroEventProbe:
			return "Dev / Test hero event";

		case cmMrSetupKeyMapping:
			return "Installation / Key mapping";
		case cmMrSetupMouseKeyRepeat:
			return "Installation / Mouse / Key repeat setup";
		case cmMrSetupFilenameExtensions:
			return "Installation / Filename extensions";
		case cmMrSetupPaths:
			return "Installation / Paths";
		case cmMrSetupBackupsAutosave:
			return "Installation / Backups / Autosave";
		case cmMrSetupUserInterfaceSettings:
			return "Installation / User interface settings";
		case cmMrSetupSearchAndReplaceDefaults:
			return "Installation / Search and Replace defaults";

		default:
			return nullptr;
	}
}

void showPlaceholderCommandBox(const char *title) {
	if (title == nullptr)
		title = "Command";
	messageBox(mfInformation | mfOKButton, "%s\n\nPlaceholder implementation for now.", title);
}

bool promptSearchPattern(std::string &pattern) {
	enum { kInputBufferSize = 256 };
	char input[kInputBufferSize];
	uchar limit = static_cast<uchar>(kInputBufferSize - 1);

	std::memset(input, 0, sizeof(input));
	if (!g_searchUiState.pattern.empty())
		strnzcpy(input, g_searchUiState.pattern.c_str(), sizeof(input));
	if (inputBox("SEARCH", "~T~ext", input, limit) == cmCancel)
		return false;
	pattern = trimAscii(input);
	return true;
}

bool promptReplaceValues(std::string &pattern, std::string &replacement) {
	enum { kPatternBufferSize = 256, kReplacementBufferSize = 256 };
	char patternInput[kPatternBufferSize];
	char replacementInput[kReplacementBufferSize];
	uchar patternLimit = static_cast<uchar>(kPatternBufferSize - 1);
	uchar replacementLimit = static_cast<uchar>(kReplacementBufferSize - 1);

	std::memset(patternInput, 0, sizeof(patternInput));
	std::memset(replacementInput, 0, sizeof(replacementInput));
	if (!g_searchUiState.pattern.empty())
		strnzcpy(patternInput, g_searchUiState.pattern.c_str(), sizeof(patternInput));
	if (!g_searchUiState.replacement.empty())
		strnzcpy(replacementInput, g_searchUiState.replacement.c_str(), sizeof(replacementInput));
	if (inputBox("SEARCH AND REPLACE", "Search ~t~ext", patternInput, patternLimit) == cmCancel)
		return false;
	if (inputBox("SEARCH AND REPLACE", "~R~eplace with", replacementInput, replacementLimit) == cmCancel)
		return false;
	pattern = trimAscii(patternInput);
	replacement = replacementInput;
	return true;
}

bool promptGotoLineNumber(long &lineNumber) {
	enum { kInputBufferSize = 32 };
	char input[kInputBufferSize];
	char *endPtr = nullptr;
	long parsed = 0;
	std::string tail;
	uchar limit = static_cast<uchar>(kInputBufferSize - 1);

	std::memset(input, 0, sizeof(input));
	if (inputBox("GOTO LINE NUMBER", "~L~ine", input, limit) == cmCancel)
		return false;
	parsed = std::strtol(input, &endPtr, 10);
	tail = trimAscii(endPtr != nullptr ? endPtr : "");
	if (endPtr == input || !tail.empty() || parsed < 1) {
		messageBox(mfError | mfOKButton, "Please enter a valid line number.");
		return false;
	}
	lineNumber = parsed;
	return true;
}

bool compileSearchRegex(const std::string &pattern, bool ignoreCase, pcre2_code **outCode,
                        std::string &errorText) {
	int errorCode = 0;
	PCRE2_SIZE errorOffset = 0;
	uint32_t options = PCRE2_UTF | PCRE2_UCP;
	char errorBuffer[256];
	int messageLength = 0;

	*outCode = nullptr;
	if (ignoreCase)
		options |= PCRE2_CASELESS;
	*outCode = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.c_str()),
	                         static_cast<PCRE2_SIZE>(pattern.size()), options, &errorCode,
	                         &errorOffset, nullptr);
	if (*outCode != nullptr) {
		errorText.clear();
		return true;
	}
	std::memset(errorBuffer, 0, sizeof(errorBuffer));
	messageLength = static_cast<int>(
	    pcre2_get_error_message(errorCode, reinterpret_cast<PCRE2_UCHAR *>(errorBuffer), sizeof(errorBuffer)));
	if (messageLength < 0)
		errorText = "Regex compile error.";
	else
		errorText = std::string(errorBuffer, static_cast<std::size_t>(messageLength));
	errorText += " (offset ";
	errorText += std::to_string(static_cast<unsigned long long>(errorOffset));
	errorText += ")";
	return false;
}

bool findRegexForward(const std::string &text, pcre2_code *code, std::size_t startOffset,
                      std::size_t &matchStart, std::size_t &matchEnd) {
	pcre2_match_data *matchData = nullptr;
	PCRE2_SIZE *ovector = nullptr;
	int rc = 0;
	const std::size_t safeStart = std::min(startOffset, text.size());

	matchData = pcre2_match_data_create_from_pattern(code, nullptr);
	if (matchData == nullptr)
		return false;
	rc = pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(text.data()), static_cast<PCRE2_SIZE>(text.size()),
	                 static_cast<PCRE2_SIZE>(safeStart), 0, matchData, nullptr);
	if (rc < 0) {
		pcre2_match_data_free(matchData);
		return false;
	}
	ovector = pcre2_get_ovector_pointer(matchData);
	matchStart = static_cast<std::size_t>(ovector[0]);
	matchEnd = static_cast<std::size_t>(ovector[1]);
	pcre2_match_data_free(matchData);
	return matchEnd >= matchStart;
}

bool findRegexWithWrap(const std::string &text, pcre2_code *code, std::size_t startOffset,
                       std::size_t &matchStart, std::size_t &matchEnd, bool &wrapped) {
	wrapped = false;
	if (findRegexForward(text, code, startOffset, matchStart, matchEnd))
		return true;
	if (startOffset == 0)
		return false;
	if (findRegexForward(text, code, 0, matchStart, matchEnd)) {
		wrapped = true;
		return true;
	}
	return false;
}

void syncVmLastSearch(TMREditWindow *win, bool valid, std::size_t start, std::size_t end,
                      std::size_t cursor) {
	std::string fileName;
	if (win == nullptr)
		return;
	fileName = win->currentFileName();
	mrvmUiReplaceWindowLastSearch(win, fileName, valid, start, end, cursor);
}

bool performSearch(TMREditWindow *win, const std::string &pattern, std::size_t startOffset, bool updateState,
                   bool showNotFoundMessage, bool *outWrapped = nullptr) {
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	bool ignoreCase = false;
	bool tabExpandIgnored = false;
	pcre2_code *code = nullptr;
	std::string regexError;
	std::string text;
	std::size_t matchStart = 0;
	std::size_t matchEnd = 0;
	bool wrapped = false;

	if (outWrapped != nullptr)
		*outWrapped = false;
	if (editor == nullptr)
		return false;
	if (pattern.empty()) {
		messageBox(mfError | mfOKButton, "Search text must not be empty.");
		return false;
	}
	mrvmUiCopyRuntimeOptions(ignoreCase, tabExpandIgnored);
	if (!compileSearchRegex(pattern, ignoreCase, &code, regexError)) {
		messageBox(mfError | mfOKButton, "Invalid search pattern:\n%s", regexError.c_str());
		return false;
	}
	text = editor->snapshotText();
	if (!findRegexWithWrap(text, code, startOffset, matchStart, matchEnd, wrapped)) {
		pcre2_code_free(code);
		if (showNotFoundMessage)
			messageBox(mfInformation | mfOKButton, "No match found.");
		syncVmLastSearch(win, false, 0, 0, 0);
		return false;
	}
	if (matchEnd == matchStart) {
		if (matchEnd < text.size())
			++matchEnd;
		else if (matchStart > 0)
			--matchStart;
	}
	editor->setCursorOffset(matchStart);
	editor->setSelectionOffsets(matchStart, matchEnd);
	editor->revealCursor(True);
	syncVmLastSearch(win, true, matchStart, matchEnd, matchStart);
	if (updateState) {
		g_searchUiState.hasPrevious = true;
		g_searchUiState.pattern = pattern;
		g_searchUiState.lastStart = matchStart;
		g_searchUiState.lastEnd = matchEnd;
	}
	pcre2_code_free(code);
	if (outWrapped != nullptr)
		*outWrapped = wrapped;
	return true;
}

bool handleSearchFindText() {
	TMREditWindow *win = currentEditWindow();
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::string pattern;
	bool wrapped = false;

	if (editor == nullptr)
		return true;
	if (!promptSearchPattern(pattern))
		return true;
	if (!performSearch(win, pattern, editor->cursorOffset(), true, true, &wrapped))
		return true;
	if (wrapped)
		mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup,
		                               "Search wrapped to start of document.",
		                               mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleSearchRepeatPrevious() {
	TMREditWindow *win = currentEditWindow();
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::size_t startOffset = 0;
	bool wrapped = false;

	if (editor == nullptr)
		return true;
	if (!g_searchUiState.hasPrevious || g_searchUiState.pattern.empty()) {
		messageBox(mfInformation | mfOKButton, "No previous search.");
		return true;
	}
	startOffset = g_searchUiState.lastEnd;
	if (g_searchUiState.lastEnd <= g_searchUiState.lastStart)
		startOffset = std::min(editor->bufferLength(), g_searchUiState.lastStart + 1);
	if (!performSearch(win, g_searchUiState.pattern, startOffset, true, true, &wrapped))
		return true;
	if (wrapped)
		mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup,
		                               "Search wrapped to start of document.",
		                               mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleSearchReplace() {
	TMREditWindow *win = currentEditWindow();
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::string pattern;
	std::string replacement;
	std::size_t start = 0;
	std::size_t end = 0;
	bool found = false;

	if (editor == nullptr || win == nullptr)
		return true;
	if (!promptReplaceValues(pattern, replacement))
		return true;
	if (pattern.empty()) {
		messageBox(mfError | mfOKButton, "Search text must not be empty.");
		return true;
	}
	found = performSearch(win, pattern, editor->cursorOffset(), false, true, nullptr);
	if (!found)
		return true;
	start = editor->selectionStartOffset();
	end = editor->selectionEndOffset();
	if (end < start)
		std::swap(start, end);
	if (!editor->replaceRangeAndSelect(static_cast<uint>(start), static_cast<uint>(end),
	                                   replacement.data(), static_cast<uint>(replacement.size()))) {
		messageBox(mfError | mfOKButton, "Replace failed.");
		return true;
	}
	editor->setCursorOffset(start);
	editor->setSelectionOffsets(start, start + replacement.size());
	editor->revealCursor(True);
	g_searchUiState.hasPrevious = true;
	g_searchUiState.pattern = pattern;
	g_searchUiState.replacement = replacement;
	g_searchUiState.lastStart = start;
	g_searchUiState.lastEnd = start + replacement.size();
	syncVmLastSearch(win, true, g_searchUiState.lastStart, g_searchUiState.lastEnd,
	                 g_searchUiState.lastStart);
	return true;
}

bool handleSearchGotoLineNumber() {
	TMREditWindow *win = currentEditWindow();
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	long lineNumber = 0;
	std::size_t offset = 0;
	std::size_t line = 1;
	std::size_t length = 0;

	if (editor == nullptr)
		return true;
	if (!promptGotoLineNumber(lineNumber))
		return true;

	length = editor->bufferLength();
	offset = 0;
	line = 1;
	while (line < static_cast<std::size_t>(lineNumber) && offset < length) {
		std::size_t next = editor->nextLineOffset(offset);
		if (next <= offset)
			break;
		offset = next;
		++line;
	}
	editor->setCursorOffset(offset);
	editor->setSelectionOffsets(offset, offset);
	editor->revealCursor(True);
	return true;
}

bool handleFileOpen() {
	enum { FileNameBufferSize = MAXPATH };
	char fileName[FileNameBufferSize];
	TMREditWindow *target;
	TMREditWindow *current = currentEditWindow();
	std::string resolvedPath;
	std::string logLine;
	bool createdTarget = false;

	if (!promptForPath("Open File", fileName, sizeof(fileName)))
		return true;
	if (!resolveReadableExistingPath(fileName, resolvedPath))
		return true;

	target = findReusableEmptyWindow(current);
	if (target == nullptr) {
		target = createEditorWindow("?No-File?");
		createdTarget = true;
	}
	if (!loadResolvedFileIntoWindow(target, resolvedPath, "Open file")) {
		if (createdTarget && target != nullptr)
			message(target, evCommand, cmClose, nullptr);
		if (target != nullptr && isEmptyUntitledEditableWindow(target) && current != target && current != nullptr)
			static_cast<void>(mrActivateEditWindow(current));
		return true;
	}
	static_cast<void>(mrActivateEditWindow(target));
	logLine = "Opened file: ";
	logLine += target->currentFileName();
	if (target->isReadOnly())
		logLine += " [read-only]";
	mrLogMessage(logLine.c_str());
	return true;
}

bool handleFileLoad() {
	enum { FileNameBufferSize = MAXPATH };
	char fileName[FileNameBufferSize];
	TMREditWindow *target = currentEditWindow();
	std::string resolvedPath;
	std::string logLine;
	bool createdTarget = false;

	if (!promptForPath("Load File", fileName, sizeof(fileName)))
		return true;
	if (!resolveReadableExistingPath(fileName, resolvedPath))
		return true;
	if (target == nullptr) {
		target = createEditorWindow("?No-File?");
		createdTarget = true;
	} else if (!target->confirmAbandonForReload())
		return true;
	if (!loadResolvedFileIntoWindow(target, resolvedPath, "Load file")) {
		if (createdTarget && target != nullptr)
			message(target, evCommand, cmClose, nullptr);
		return true;
	}
	static_cast<void>(mrActivateEditWindow(target));
	logLine = "Loaded file into active window: ";
	logLine += target->currentFileName();
	if (target->isReadOnly())
		logLine += " [read-only]";
	mrLogMessage(logLine.c_str());
	return true;
}

bool handleExecuteProgram() {
	std::string commandLine;
	TMREditWindow *win;

	if (!promptForCommandLine(commandLine))
		return true;
	if (commandLine.empty()) {
		messageBox(mfInformation | mfOKButton, "No command specified.");
		return true;
	}

	win = createEditorWindow(shortenCommandTitle(commandLine).c_str());
	if (win == nullptr) {
		messageBox(mfError | mfOKButton, "Unable to create communication window.");
		return true;
	}
	startExternalCommandInWindow(win, commandLine, true, true, true);
	return true;
}

bool startExternalCommandInWindow(TMREditWindow *win, const std::string &commandLine, bool replaceBuffer,
                                  bool activate, bool closeOnFailure) {
	std::string title;
	std::string initialText;
	std::ostringstream logLine;
	std::uint64_t taskId;

	if (win == nullptr)
		return false;
	title = shortenCommandTitle(commandLine);
	initialText = "$ " + commandLine + "\n\n";
	if (replaceBuffer) {
		if (!win->replaceTextBuffer(initialText.c_str(), title.c_str())) {
			if (closeOnFailure)
				message(win, evCommand, cmClose, nullptr);
			messageBox(mfError | mfOKButton, "Unable to prepare communication window.");
			return false;
		}
	}
	win->setReadOnly(true);
	win->setFileChanged(false);
	win->setWindowRole(TMREditWindow::wrCommunicationCommand, commandLine);
	if (activate)
		static_cast<void>(mrActivateEditWindow(win));

	taskId = mr::coprocessor::globalCoprocessor().submit(
	    mr::coprocessor::Lane::Io, mr::coprocessor::TaskKind::ExternalIo,
	    static_cast<std::size_t>(win->bufferId()), 0, std::string("external-io: ") + commandLine,
	    [commandLine, channelId = static_cast<std::size_t>(win->bufferId())](
	        const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
		    return runExternalCommandTask(info, stopToken, channelId, commandLine);
	    });
	if (taskId == 0) {
		if (closeOnFailure)
			message(win, evCommand, cmClose, nullptr);
		messageBox(mfError | mfOKButton, "Unable to start external command worker.");
		return false;
	}
	win->trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::ExternalIo, commandLine);

	logLine << "Started external command in communication window #" << win->bufferId() << ": "
	        << commandLine << " [task #" << taskId << "]";
	mrLogMessage(logLine.str().c_str());
	return true;
}

bool handleCancelBackgroundMacros() {
	TMREditWindow *win = currentEditWindow();
	std::ostringstream line;
	std::size_t taskCount;

	if (win == nullptr)
		return true;
	taskCount = win->trackedMacroTaskCount();
	if (taskCount == 0) {
		messageBox(mfInformation | mfOKButton, "No background macro tasks are running in this window.");
		return true;
	}
	if (!win->cancelTrackedMacroTasks())
		return true;
	line << "Requested cancel of " << taskCount << " background macro task";
	if (taskCount != 1)
		line << "s";
	line << " in window #" << win->bufferId() << ".";
	mrLogMessage(line.str().c_str());
	return true;
}

bool handleHeroEventProbe() {
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEvent, "Hero event probe",
	                              mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool dispatchEditorCommand(ushort editorCommand, bool requiresWritable) {
	TMREditWindow *win = currentEditWindow();
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

	if (win == nullptr || editor == nullptr)
		return true;
	if (requiresWritable && win->isReadOnly()) {
		messageBox(mfInformation | mfOKButton, "Window is read-only.");
		return true;
	}
	message(editor, evCommand, editorCommand, nullptr);
	return true;
}

bool dispatchEditorClipboardCommand(ushort editorCommand, bool requiresWritable) {
	return dispatchEditorCommand(editorCommand, requiresWritable);
}

ushort execDialogWithDataLocal(TDialog *dialog, void *data) {
	ushort result = cmCancel;

	if (dialog == nullptr || TProgram::deskTop == nullptr)
		return cmCancel;
	if (data != nullptr)
		dialog->setData(data);
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel && data != nullptr)
		dialog->getData(data);
	TObject::destroy(dialog);
	return result;
}

void syncPersistentBlocksMenuState() {
	if (auto *mrMenuBar = dynamic_cast<TMRMenuBar *>(TProgram::menuBar))
		mrMenuBar->setPersistentBlocksMenuState(configuredPersistentBlocksSetting());
}

bool handleBlockAction(bool ok, const char *failureText) {
	if (!ok && failureText != nullptr && *failureText != '\0')
		messageBox(mfInformation | mfOKButton, "%s", failureText);
	return true;
}

bool hasMarkedTextForBlockOperation(TMREditWindow *win) {
	if (win == nullptr || !win->hasBlock())
		return false;
	if (win->blockStatus() == TMREditWindow::bmStream &&
	    win->blockAnchorPtr() == win->blockEffectiveEndPtr())
		return false;
	return true;
}

bool promptBlockSavePath(std::string &outPath) {
	char buffer[MAXPATH] = {0};
	ushort result = cmCancel;
	static constexpr ushort kBlockSaveDialogHistoryId = 45;
	TMREditWindow *win = currentEditWindow();

	outPath.clear();
	if (win != nullptr && win->currentFileName() != nullptr && *win->currentFileName() != '\0')
		strnzcpy(buffer, win->currentFileName(), sizeof(buffer));
	else
		initRememberedLoadDialogPath(buffer, sizeof(buffer), "*.*");
	result = execDialogWithDataLocal(
	    new TFileDialog("*.*", "Save block as", "~N~ame", fdOKButton, kBlockSaveDialogHistoryId), buffer);
	if (result == cmCancel)
		return false;
	fexpand(buffer);
	rememberLoadDialogPath(buffer);
	outPath = buffer;
	if (outPath.find('*') != std::string::npos || outPath.find('?') != std::string::npos)
		return false;
	return !outPath.empty();
}

bool chooseInterWindowBlockTarget(int &sourceWindowIndex) {
	TMREditWindow *targetWin = currentEditWindow();
	TMREditWindow *sourceWin = nullptr;

	sourceWindowIndex = 0;
	if (targetWin == nullptr)
		return false;
	sourceWin = mrShowWindowListDialog(mrwlActivateWindow, targetWin);
	if (sourceWin == nullptr)
		return false;
	if (sourceWin == targetWin) {
		messageBox(mfInformation | mfOKButton, "Choose another window for inter-window block operation.");
		return false;
	}
	if (!hasMarkedTextForBlockOperation(sourceWin)) {
		messageBox(mfInformation | mfOKButton, "No block marked in the selected source window.");
		return false;
	}
	sourceWindowIndex = mrvmUiCurrentWindowIndex(sourceWin);
	static_cast<void>(mrActivateEditWindow(targetWin));
	return true;
}

bool togglePersistentBlocksSetting() {
	MREditSetupSettings settings = configuredEditSetupSettings();
	MRSetupPaths paths;
	MRSettingsWriteReport writeReport;
	TMREditorApp *app = dynamic_cast<TMREditorApp *>(TProgram::application);
	std::string errorText;
	bool enabled;

	settings.persistentBlocks = !settings.persistentBlocks;
	if (!setConfiguredEditSetupSettings(settings, &errorText)) {
		messageBox(mfError | mfOKButton, "Persistent blocks update failed:\n%s", errorText.c_str());
		return true;
	}

	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	if (!writeSettingsMacroFile(paths, &errorText, &writeReport)) {
		messageBox(mfError | mfOKButton, "Settings save failed:\n%s", errorText.c_str());
		return true;
	}
	mrLogSettingsWriteReport("persistent blocks toggle", writeReport);
	if (app != nullptr && !app->reloadSettingsMacroFromPath(paths.settingsMacroUri, &errorText)) {
		messageBox(mfError | mfOKButton, "Settings reload failed:\n%s", errorText.c_str());
		return true;
	}

	enabled = configuredPersistentBlocksSetting();
	mrLogMessage(enabled ? "Persistent blocks enabled." : "Persistent blocks disabled.");
	syncPersistentBlocksMenuState();
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction,
	                               enabled ? "Persistent blocks: ON" : "Persistent blocks: OFF",
	                               mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleStopCurrentProgram() {
	TMREditWindow *win = currentEditWindow();
	std::ostringstream line;
	std::size_t taskCount;

	if (win == nullptr || !win->isCommunicationWindow())
		return true;
	taskCount = win->trackedTaskCount(mr::coprocessor::TaskKind::ExternalIo);
	if (taskCount == 0) {
		messageBox(mfInformation | mfOKButton, "No external program task is running in this window.");
		return true;
	}
	if (!win->cancelTrackedExternalIoTasks())
		return true;
	line << "Requested stop of " << taskCount << " external program task";
	if (taskCount != 1)
		line << "s";
	line << " in communication window #" << win->bufferId() << ".";
	mrLogMessage(line.str().c_str());
	return true;
}

bool handleRestartCurrentProgram() {
	TMREditWindow *win = currentEditWindow();

	if (win == nullptr || win->windowRole() != TMREditWindow::wrCommunicationCommand)
		return true;
	if (win->hasTrackedExternalIoTasks()) {
		messageBox(mfInformation | mfOKButton, "Stop the current program before restarting it.");
		return true;
	}
	if (win->windowRoleDetail().empty()) {
		messageBox(mfInformation | mfOKButton, "No restartable command is associated with this window.");
		return true;
	}
	startExternalCommandInWindow(win, win->windowRoleDetail(), true, true, false);
	return true;
}

bool handleClearCurrentOutput() {
	TMREditWindow *win = currentEditWindow();
	std::ostringstream line;

	if (win == nullptr)
		return true;
	if (win->windowRole() == TMREditWindow::wrLog) {
		if (!mrClearLogWindow()) {
			messageBox(mfError | mfOKButton, "Unable to clear log window.");
			return true;
		}
		mrLogMessage("Log window cleared.");
		return true;
	}
	if (!win->isCommunicationWindow())
		return true;
	if (win->hasTrackedExternalIoTasks()) {
		messageBox(mfInformation | mfOKButton, "Stop the current program before clearing its output.");
		return true;
	}
	if (!win->replaceTextBuffer("", win->getTitle(0))) {
		messageBox(mfError | mfOKButton, "Unable to clear communication window.");
		return true;
	}
	win->setReadOnly(true);
	win->setFileChanged(false);
	line << "Cleared communication window #" << win->bufferId() << ".";
	mrLogMessage(line.str().c_str());
	return true;
}

} // namespace

bool handleMRCommand(ushort command) {
	switch (command) {
		case cmMrFileOpen:
			return handleFileOpen();

		case cmMrFileLoad:
			return handleFileLoad();

		case cmMrFileSave:
			static_cast<void>(saveCurrentEditWindow());
			return true;

		case cmMrFileSaveAs:
			static_cast<void>(saveCurrentEditWindowAs());
			return true;

		case cmMrFileInformation:
			showFileInformationDialog(currentEditWindow());
			return true;

		case cmMrEditUndo:
			return dispatchEditorCommand(cmMrEditUndo, true);

		case cmMrEditRedo:
			return dispatchEditorCommand(cmMrEditRedo, true);

		case cmMrEditCutToBuffer:
			return dispatchEditorClipboardCommand(cmCut, true);

		case cmMrEditCopyToBuffer:
			return dispatchEditorClipboardCommand(cmCopy, false);

		case cmMrEditPasteFromBuffer:
			return dispatchEditorClipboardCommand(cmPaste, true);

		case cmMrSearchPushMarker:
			return handleBlockAction(mrvmUiPushMarker(), "Unable to push position onto marker stack.");

		case cmMrSearchGetMarker:
			return handleBlockAction(mrvmUiGetMarker(), "No marker position on stack.");

		case cmMrSearchFindText:
			return handleSearchFindText();

		case cmMrSearchReplace:
			return handleSearchReplace();

		case cmMrSearchRepeatPrevious:
			return handleSearchRepeatPrevious();

		case cmMrSearchGotoLineNumber:
			return handleSearchGotoLineNumber();

		case cmMrBlockCopy:
			return handleBlockAction(mrvmUiCopyBlock(), "No block marked.");

		case cmMrBlockMove:
			return handleBlockAction(mrvmUiMoveBlock(), "Unable to move block.");

		case cmMrBlockDelete:
			return handleBlockAction(mrvmUiDeleteBlock(), "Unable to delete block.");

		case cmMrBlockSaveToDisk: {
			std::string savePath;
			if (!promptBlockSavePath(savePath))
				return true;
			return handleBlockAction(mrvmUiSaveBlockToFile(savePath), "Unable to save block.");
		}

		case cmMrBlockIndent:
			return handleBlockAction(mrvmUiIndentBlock(), "Unable to indent block.");

		case cmMrBlockUndent:
			return handleBlockAction(mrvmUiUndentBlock(), "Unable to undent block.");

		case cmMrBlockWindowCopy: {
			bool ok = false;
			int sourceWindowIndex = 0;
			if (!chooseInterWindowBlockTarget(sourceWindowIndex))
				return true;
			if (sourceWindowIndex > 0)
				ok = mrvmUiWindowCopyBlock(sourceWindowIndex);
			return handleBlockAction(ok, "Inter-window block copy failed.");
		}

		case cmMrBlockWindowMove: {
			bool ok = false;
			int sourceWindowIndex = 0;
			if (!chooseInterWindowBlockTarget(sourceWindowIndex))
				return true;
			if (sourceWindowIndex > 0)
				ok = mrvmUiWindowMoveBlock(sourceWindowIndex);
			return handleBlockAction(ok, "Inter-window block move failed.");
		}

		case cmMrBlockMarkLines:
			return handleBlockAction(mrvmUiBlockBeginLine(), "Unable to start line block marking.");

		case cmMrBlockMarkColumns:
			return handleBlockAction(mrvmUiBlockBeginColumn(), "Unable to start column block marking.");

		case cmMrBlockMarkStream:
			return handleBlockAction(mrvmUiBlockBeginStream(), "Unable to start stream block marking.");

		case cmMrBlockEndMarking:
			return handleBlockAction(mrvmUiBlockEndMarking(), "No active block marking.");

		case cmMrBlockTurnMarkingOff:
			return handleBlockAction(mrvmUiBlockTurnMarkingOff(), "No block marked.");

		case cmMrBlockPersistent:
			return togglePersistentBlocksSetting();

		case cmMrWindowOpen:
			static_cast<void>(createEditorWindow("?No-File?"));
			mrLogMessage("New empty window opened.");
			return true;

		case cmMrWindowClose:
			static_cast<void>(closeCurrentEditWindow());
			return true;

		case cmMrWindowList: {
			TMREditWindow *selected = mrShowWindowListDialog(mrwlActivateWindow, currentEditWindow());
			if (selected != nullptr)
				static_cast<void>(mrActivateEditWindow(selected));
			return true;
		}

		case cmMrWindowNext:
			static_cast<void>(activateRelativeEditWindow(1));
			return true;

		case cmMrWindowPrevious:
			static_cast<void>(activateRelativeEditWindow(-1));
			return true;

		case cmMrWindowHide:
			static_cast<void>(hideCurrentEditWindow());
			return true;

		case cmMrWindowZoom:
			mrvmUiZoomCurrentWindow();
			return true;

		case cmMrWindowCascade:
			return handleWindowCascade();

		case cmMrWindowTile:
			return handleWindowTile();

		case cmMrTextUpperCaseMenu:
			return dispatchEditorCommand(cmMrTextUpperCaseMenu, true);

		case cmMrTextLowerCaseMenu:
			return dispatchEditorCommand(cmMrTextLowerCaseMenu, true);

		case cmMrWindowLink:
			mrvmUiLinkCurrentWindow();
			return true;

		case cmMrWindowUnlink:
			mrvmUiUnlinkCurrentWindow();
			return true;

		case cmMrHelpContents:
		case cmMrHelpKeys:
		case cmMrHelpDetailedIndex:
		case cmMrHelpPreviousTopic:
		case cmHelp:
			static_cast<void>(mrShowProjectHelp());
			return true;

		case cmMrOtherInstallationAndSetup:
			runInstallationAndSetupDialogFlow();
			return true;

		case cmMrHelpAbout:
			showAboutDialog();
			return true;

		case cmMrOtherExecuteProgram:
			return handleExecuteProgram();

		case cmMrOtherStopProgram:
			return handleStopCurrentProgram();

		case cmMrOtherRestartProgram:
			return handleRestartCurrentProgram();

		case cmMrOtherClearOutput:
			return handleClearCurrentOutput();

		case cmMrOtherMacroManager:
			return runMacroManagerDialog();

		case cmMrDevCancelMacroTasks:
			return handleCancelBackgroundMacros();

		case cmMrDevHeroEventProbe:
			return handleHeroEventProbe();

				default: {
					const char *title = placeholderCommandTitle(command);
				if (title != nullptr) {
					showPlaceholderCommandBox(title);
					return true;
				}
			return false;
		}
	}
}
