#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TObject
#define Uses_TEvent
#define Uses_TRect
#define Uses_MsgBox
#include <tvision/tv.h>

#include "MRCommandRouter.hpp"

#include <cstddef>
#include <sstream>
#include <string>

#include "../dialogs/MRFileInformationDialog.hpp"
#include "../dialogs/MRAboutDialog.hpp"
#include "../dialogs/MRSetupDialogs.hpp"
#include "../mrmac/mrvm.hpp"
#include "../services/MRExternalCommand.hpp"
#include "../services/MRFileCommands.hpp"
#include "../services/MRWindowCommands.hpp"
#include "../dialogs/MRMacroFileDialog.hpp"
#include "../dialogs/MRWindowListDialog.hpp"
#include "../ui/TMREditWindow.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "MRCommands.hpp"

namespace {
bool startExternalCommandInWindow(TMREditWindow *win, const std::string &commandLine, bool replaceBuffer,
                                  bool activate, bool closeOnFailure);

const char *dummyCommandTitle(ushort command) {
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
		case cmMrWindowOrganizePlaceholder:
			return "Window / Organize";
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

		case cmMrOtherKeystrokeMacros:
			return "Other / Keystroke macros";
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

		case cmMrSetupKeyMapping:
			return "Installation / Key mapping";
		case cmMrSetupMouseKeyRepeat:
			return "Installation / Mouse / Key repeat setup";
		case cmMrSetupFilenameExtensions:
			return "Installation / Filename extensions";
		case cmMrSetupSwappingEmsXms:
			return "Installation / Swapping / EMS / XMS";
		case cmMrSetupBackupsTempAutosave:
			return "Installation / Backups / Temp files / Autosave";
		case cmMrSetupUserInterfaceSettings:
			return "Installation / User interface settings";
		case cmMrSetupSearchAndReplacePlaceholder:
			return "Installation / Search and Replace defaults";

		default:
			return 0;
	}
}

void showDummyCommandBox(const char *title) {
	if (title == 0)
		title = "Command";
	messageBox(mfInformation | mfOKButton, "%s\n\nDummy implementation for now.", title);
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
	if (target == 0) {
		target = createEditorWindow("?No-File?");
		createdTarget = true;
	}
	if (!loadResolvedFileIntoWindow(target, resolvedPath, "Open file")) {
		if (createdTarget && target != 0)
			message(target, evCommand, cmClose, 0);
		if (target != 0 && isEmptyUntitledEditableWindow(target) && current != target && current != 0)
			mrActivateEditWindow(current);
		return true;
	}
	mrActivateEditWindow(target);
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
	if (target == 0) {
		target = createEditorWindow("?No-File?");
		createdTarget = true;
	} else if (!target->confirmAbandonForReload())
		return true;
	if (!loadResolvedFileIntoWindow(target, resolvedPath, "Load file")) {
		if (createdTarget && target != 0)
			message(target, evCommand, cmClose, 0);
		return true;
	}
	mrActivateEditWindow(target);
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
	if (win == 0) {
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

	if (win == 0)
		return false;
	title = shortenCommandTitle(commandLine);
	initialText = "$ " + commandLine + "\n\n";
	if (replaceBuffer) {
		if (!win->replaceTextBuffer(initialText.c_str(), title.c_str())) {
			if (closeOnFailure)
				message(win, evCommand, cmClose, 0);
			messageBox(mfError | mfOKButton, "Unable to prepare communication window.");
			return false;
		}
	}
	win->setReadOnly(true);
	win->setFileChanged(false);
	win->setWindowRole(TMREditWindow::wrCommunicationCommand, commandLine);
	if (activate)
		mrActivateEditWindow(win);

	taskId = mr::coprocessor::globalCoprocessor().submit(
	    mr::coprocessor::Lane::Io, mr::coprocessor::TaskKind::ExternalIo,
	    static_cast<std::size_t>(win->bufferId()), 0, std::string("external-io: ") + commandLine,
	    [commandLine, channelId = static_cast<std::size_t>(win->bufferId())](
	        const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
		    return runExternalCommandTask(info, stopToken, channelId, commandLine);
	    });
	if (taskId == 0) {
		if (closeOnFailure)
			message(win, evCommand, cmClose, 0);
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

	if (win == 0)
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

bool handleStopCurrentProgram() {
	TMREditWindow *win = currentEditWindow();
	std::ostringstream line;
	std::size_t taskCount;

	if (win == 0 || !win->isCommunicationWindow())
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

	if (win == 0 || win->windowRole() != TMREditWindow::wrCommunicationCommand)
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

	if (win == 0)
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
			saveCurrentEditWindow();
			return true;

		case cmMrFileSaveAs:
			saveCurrentEditWindowAs();
			return true;

		case cmMrFileInformation:
			showFileInformationDialog(currentEditWindow());
			return true;

		case cmMrWindowOpen:
			createEditorWindow("?No-File?");
			mrLogMessage("New empty window opened.");
			return true;

		case cmMrWindowClose:
			closeCurrentEditWindow();
			return true;

		case cmMrWindowList: {
			TMREditWindow *selected = mrShowWindowListDialog(mrwlActivateWindow, currentEditWindow());
			if (selected != nullptr)
				mrActivateEditWindow(selected);
			return true;
		}

		case cmMrWindowNext:
			activateRelativeEditWindow(1);
			return true;

		case cmMrWindowPrevious:
			activateRelativeEditWindow(-1);
			return true;

		case cmMrWindowHide:
			hideCurrentEditWindow();
			return true;

		case cmMrWindowZoom:
			mrvmUiZoomCurrentWindow();
			return true;

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
			mrShowProjectHelp();
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

		case cmMrDevRunMacro:
			runMacroFileDialog();
			return true;

		case cmMrDevCancelMacroTasks:
			return handleCancelBackgroundMacros();

		default: {
			const char *title = dummyCommandTitle(command);
			if (title != 0) {
				showDummyCommandBox(title);
				return true;
			}
			return false;
		}
	}
}
