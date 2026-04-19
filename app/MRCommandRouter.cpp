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
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>

#include "../dialogs/MRFileInformationDialog.hpp"
#include "../dialogs/MRAboutDialog.hpp"
#include "../dialogs/MRSetupDialogs.hpp"
#include "../config/MRDialogPaths.hpp"
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

namespace {
bool startExternalCommandInWindow(TMREditWindow *win, const std::string &commandLine, bool replaceBuffer,
                                  bool activate, bool closeOnFailure);

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
		case cmMrWindowOrganizeWindowManager:
			return "Window / Organize / Window manager";
		case cmMrWindowOrganizeTile:
			return "Window / Organize / Tile";
		case cmMrWindowOrganizeCascade:
			return "Window / Organize / Cascade";
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

void syncWindowManagerMenuState() {
	if (auto *mrMenuBar = dynamic_cast<TMRMenuBar *>(TProgram::menuBar))
		mrMenuBar->setWindowManagerMenuState(configuredWindowManager());
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

bool toggleWindowManagerSetting() {
	std::string errorText;
	bool enabled = !configuredWindowManager();

	if (!setConfiguredWindowManager(enabled, &errorText)) {
		messageBox(mfError | mfOKButton, "Window manager update failed:\n%s", errorText.c_str());
		return true;
	}
	if (!persistConfiguredSettingsSnapshot(&errorText)) {
		messageBox(mfError | mfOKButton, "Settings save failed:\n%s", errorText.c_str());
		return true;
	}

	mrLogMessage(enabled ? "Window manager enabled." : "Window manager disabled.");
	syncWindowManagerMenuState();
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction,
	                               enabled ? "Window manager: ON" : "Window manager: OFF",
	                               mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool togglePersistentBlocksSetting() {
	TMREditorApp *app = dynamic_cast<TMREditorApp *>(TProgram::application);
	MREditSetupSettings settings = configuredEditSetupSettings();
	MRSetupPaths paths;
	MRSettingsWriteReport writeReport;
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

		case cmMrWindowOrganizeWindowManager:
			return toggleWindowManagerSetting();
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
