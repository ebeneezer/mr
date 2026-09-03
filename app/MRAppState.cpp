#include "MRAppState.hpp"

#include "router/MRCommandRouterSearchMultiFile.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../ui/MRBentoBox/MRBentoBox.hpp"
#include "../ui/MREditWindow.hpp"
#include "MRCommands.hpp"
#include "MRUpdate.hpp"

namespace {
struct AppCommandState {
	MREditWindow *window;
	MRDesktopWindow *desktopWindow;
	std::size_t windowCount;
	bool isMinimizedWindow;
	bool hasEditableWindow;
	bool hasReadOnlyWindow;
	bool hasDirtyWindow;
	bool hasAnyDirtyWindow;
	bool hasPersistentFileName;
	bool hasBuildSourceFile;
	bool canSaveInPlace;
	bool hasSelection;
	bool hasUndo;
	bool hasRedo;
	bool hasBlock;
	bool blockMarking;
	bool hasMacroTasks;
	bool hasExternalIoTasks;
	bool isCommunicationWindow;
	bool isCommunicationCommandWindow;
	bool isLogWindow;
	bool hasExternalCommandDetail;
	bool hasCompilerProblems;
	bool hasFileCompareWindow;
	bool hasGdbDebugger;

	AppCommandState() : window(nullptr), desktopWindow(nullptr), windowCount(0), isMinimizedWindow(false), hasEditableWindow(false), hasReadOnlyWindow(false), hasDirtyWindow(false), hasAnyDirtyWindow(false), hasPersistentFileName(false), hasBuildSourceFile(false), canSaveInPlace(false), hasSelection(false), hasUndo(false), hasRedo(false), hasBlock(false), blockMarking(false), hasMacroTasks(false), hasExternalIoTasks(false), isCommunicationWindow(false), isCommunicationCommandWindow(false), isLogWindow(false), hasExternalCommandDetail(false), hasCompilerProblems(false), hasFileCompareWindow(false), hasGdbDebugger(false) {
	}
};

void setCommandEnabled(ushort command, bool enabled) {
	if (enabled) TView::enableCommand(command);
	else
		TView::disableCommand(command);
}

AppCommandState appCommandState() {
	AppCommandState state;
	MREditWindow *win = currentEditWindow();
	MREditWindow *editorWin = currentEditorCommandWindow();

	state.window = win;
	state.desktopWindow = currentDesktopWindow();
	state.windowCount = allDesktopWindowsInZOrder().size();
	for (MREditWindow *window : allEditWindowsInZOrder())
		if (window != nullptr && window->isFileChanged() && !window->isReadOnly()) {
			state.hasAnyDirtyWindow = true;
			break;
		}
	state.isMinimizedWindow = state.desktopWindow != nullptr && state.desktopWindow->desktopMinimized();
	if (win == nullptr) return state;
	MREditWindow *externalWin = win;
	for (TView *view = win; view != nullptr; view = view->owner) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(view);
		if (bentoBox != nullptr) {
			state.hasGdbDebugger = state.hasGdbDebugger || bentoBox->gdbDebuggerActive();
			if (bentoBox->isFileCompareBox()) state.hasFileCompareWindow = true;
		}
	}
	if (!state.hasFileCompareWindow) {
		for (MREditWindow *window : allEditWindowsInZOrder()) {
			MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);
			if (bentoBox != nullptr && bentoBox->isFileCompareBox() && bentoBox->containsFileCompareSourceWindow(win)) {
				state.hasFileCompareWindow = true;
				break;
			}
		}
	}
	if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(win); bentoBox != nullptr) {
		state.hasCompilerProblems = bentoBox->problemsPane() != nullptr && bentoBox->hasCompilerProblems();
		if (bentoBox->buildOutputPane() != nullptr) externalWin = bentoBox->buildOutputPane();
	}

	state.hasReadOnlyWindow = editorWin == nullptr || editorWin->isReadOnly();
	state.hasEditableWindow = !state.hasReadOnlyWindow;
	state.hasDirtyWindow = editorWin != nullptr && editorWin->isFileChanged();
	state.hasPersistentFileName = editorWin != nullptr && editorWin->hasPersistentFileName();
	state.hasBuildSourceFile = win->hasPersistentFileName();
	state.canSaveInPlace = editorWin != nullptr && editorWin->canSaveInPlace();
	state.hasBlock = editorWin != nullptr && editorWin->hasBlock();
	state.blockMarking = editorWin != nullptr && editorWin->isBlockMarking();
	state.hasSelection = editorWin != nullptr && editorWin->hasSelection();
	state.hasUndo = editorWin != nullptr && editorWin->hasUndoHistory();
	state.hasRedo = editorWin != nullptr && editorWin->hasRedoHistory();
	state.hasMacroTasks = editorWin != nullptr && editorWin->hasTrackedMacroTasks();
	state.hasExternalIoTasks = externalWin->hasTrackedExternalIoTasks();
	state.isCommunicationWindow = externalWin->isCommunicationWindow();
	state.isCommunicationCommandWindow = externalWin->windowRole() == MREditWindow::wrCommunicationCommand;
	state.isLogWindow = externalWin->windowRole() == MREditWindow::wrLog;
	state.hasExternalCommandDetail = !externalWin->windowRoleDetail().empty();
	return state;
}
} // namespace

void updateAppCommandState(int desktopCount, bool cyclicVirtualDesktops) {
	AppCommandState state = appCommandState();
	bool hasWindow = state.desktopWindow != nullptr;
	bool hasEditor = state.window != nullptr;
	bool canModify = hasEditor && state.hasEditableWindow;
	bool canSaveAs = hasEditor && (state.hasEditableWindow || state.isLogWindow);
	bool hasMultipleWindows = state.windowCount > 1;

	setCommandEnabled(cmMrToggleFullscreen, true);

	setCommandEnabled(cmMrFileOpen, true);
	setCommandEnabled(cmMrFileLoad, true);
	setCommandEnabled(cmMrFileGetLast, true);
	setCommandEnabled(cmMrFileOpenWorkspace, true);
	setCommandEnabled(cmMrFileAcquire, true);
	setCommandEnabled(cmMrFileOpenLiveLog, true);
	setCommandEnabled(cmMrFileOpenJournal, true);
	setCommandEnabled(cmMrFileSave, canModify && state.hasDirtyWindow);
	setCommandEnabled(cmMrFileSaveAs, canSaveAs);
	setCommandEnabled(cmMrFileSaveAll, state.hasAnyDirtyWindow);
	setCommandEnabled(cmMrFileRevert, hasEditor && state.hasPersistentFileName);
	setCommandEnabled(cmMrFileInformation, hasEditor);
	setCommandEnabled(cmMrFileMerge, hasEditor);
	setCommandEnabled(cmMrFilePrint, hasEditor);
	setCommandEnabled(cmMrFileShellToDos, true);

	setCommandEnabled(cmMrEditUndo, canModify && state.hasUndo);
	setCommandEnabled(cmMrEditRedo, canModify && state.hasRedo);
	setCommandEnabled(cmMrEditMarkAll, hasEditor);
	setCommandEnabled(cmMrEditCutToBuffer, canModify && (state.hasSelection || state.hasBlock));
	setCommandEnabled(cmMrEditCopyToBuffer, hasEditor && (state.hasSelection || state.hasBlock));
	setCommandEnabled(cmMrEditAppendToBuffer, false);
	setCommandEnabled(cmMrEditCutAndAppendToBuffer, false);
	setCommandEnabled(cmMrEditPasteFromBuffer, canModify);
	setCommandEnabled(cmMrEditToggleInsertMode, hasEditor);
	setCommandEnabled(cmMrTextToggleLineDrawing, hasEditor);
	setCommandEnabled(cmMrTextToggleDoubleLines, hasEditor && state.window != nullptr && state.window->lineDrawingEnabled());

	setCommandEnabled(cmMrWindowOpen, true);
	setCommandEnabled(cmMrWindowClose, hasWindow);
	setCommandEnabled(cmMrWindowList, state.windowCount > 0);
	setCommandEnabled(cmMrWindowNext, hasMultipleWindows);
	setCommandEnabled(cmMrWindowPrevious, hasMultipleWindows);
	setCommandEnabled(cmMrWindowHide, hasWindow);
	setCommandEnabled(cmMrWindowZoom, hasWindow);
	setCommandEnabled(cmMrWindowMinimize, hasWindow);
	setCommandEnabled(cmMrWindowRestore, hasWindow && state.isMinimizedWindow);
	setCommandEnabled(cmMrWindowCascade, hasWindow);
	setCommandEnabled(cmMrWindowTile, hasWindow);
	setCommandEnabled(cmMrWindowSplitHorizontal, hasEditor);
	setCommandEnabled(cmMrWindowSplitVertical, hasEditor);
	{
		const int currentDesktop = currentVirtualDesktop();
		const bool hasMultipleDesktops = desktopCount > 1;
		const bool cyclicViewport = hasMultipleDesktops && cyclicVirtualDesktops;
		const int windowDesktop = hasWindow ? state.desktopWindow->desktopIndex() : 1;

		setCommandEnabled(cmMrWindowNextDesktop, hasMultipleDesktops && (currentDesktop < desktopCount || cyclicViewport));
		setCommandEnabled(cmMrWindowPrevDesktop, hasMultipleDesktops && (currentDesktop > 1 || cyclicViewport));
		setCommandEnabled(cmMrWindowMoveToNextDesktop, hasWindow && hasMultipleDesktops && windowDesktop < desktopCount);
		setCommandEnabled(cmMrWindowMoveToPrevDesktop, hasWindow && hasMultipleDesktops && windowDesktop > 1);
	}
	setCommandEnabled(cmMrWindowLink, hasMultipleWindows && hasEditor);
	setCommandEnabled(cmMrWindowUnlink, hasEditor);

	setCommandEnabled(cmMrBlockCopy, hasEditor && state.hasBlock);
	setCommandEnabled(cmMrBlockMove, canModify && state.hasBlock);
	setCommandEnabled(cmMrBlockDelete, canModify && state.hasBlock);
	setCommandEnabled(cmMrBlockLoadFromDisk, canModify);
	setCommandEnabled(cmMrBlockSaveToDisk, hasEditor && state.hasBlock);
	setCommandEnabled(cmMrBlockIndent, canModify && state.hasBlock);
	setCommandEnabled(cmMrBlockUndent, canModify && state.hasBlock);
	setCommandEnabled(cmMrBlockWindowCopy, hasEditor && state.hasBlock && hasMultipleWindows);
	setCommandEnabled(cmMrBlockWindowMove, canModify && state.hasBlock && hasMultipleWindows);
	setCommandEnabled(cmMrBlockMarkLines, canModify && !state.blockMarking);
	setCommandEnabled(cmMrBlockMarkColumns, canModify);
	setCommandEnabled(cmMrBlockMarkStream, canModify);
	setCommandEnabled(cmMrBlockToggleMarking, (canModify && !state.blockMarking) || (hasEditor && state.blockMarking));
	setCommandEnabled(cmMrBlockToggleVisibility, canModify);
	setCommandEnabled(cmMrBlockEndMarking, hasEditor && state.blockMarking);
	setCommandEnabled(cmMrBlockTurnMarkingOff, hasEditor && state.hasBlock);
	setCommandEnabled(cmMrBlockPersistent, hasEditor);

	setCommandEnabled(cmMrSearchFindText, hasEditor);
	setCommandEnabled(cmMrSearchReplace, canModify);
	setCommandEnabled(cmMrSearchRepeatPrevious, hasEditor);
	setCommandEnabled(cmMrSearchMultiFileSearch, true);
	setCommandEnabled(cmMrSearchListFilesFromLastSearch, hasPreviousMultiFileSearchResults());
	setCommandEnabled(cmMrSearchMultiFileSearchReplace, true);
	setCommandEnabled(cmMrSearchPushMarker, hasEditor);
	setCommandEnabled(cmMrSearchGetMarker, hasEditor);
	setCommandEnabled(cmMrSearchSetRandomAccessMark, hasEditor);
	setCommandEnabled(cmMrSearchRetrieveRandomAccessMark, hasEditor);
	setCommandEnabled(cmMrSearchGotoLineNumber, hasEditor);

	setCommandEnabled(cmMrTextUpperCaseMenu, canModify && state.hasSelection);
	setCommandEnabled(cmMrTextLowerCaseMenu, canModify && state.hasSelection);
	setCommandEnabled(cmMrTextCenterLine, canModify);
	setCommandEnabled(cmMrTextTimeDateStamp, canModify);
	setCommandEnabled(cmMrTextReformatParagraph, canModify);
	setCommandEnabled(cmMrTextPrettifyBlockOrFile, canModify);
	setCommandEnabled(cmMrTextHexEditor, hasEditor && state.window->getEditor() != nullptr && state.window->allowsDocumentViewportSplit() && !state.window->hasTrackedExternalIoTasks());
	setCommandEnabled(cmMrTextFileCompare, hasEditor && hasMultipleWindows);
	setCommandEnabled(cmMrOtherBuildCurrentFile, hasEditor && state.hasBuildSourceFile);
	setCommandEnabled(cmMrDebuggerStart, hasEditor && state.hasBuildSourceFile && !state.hasGdbDebugger);
	setCommandEnabled(cmMrOtherGitChanges, hasEditor && state.hasPersistentFileName);
	setCommandEnabled(cmMrOtherStopProgram, hasWindow && state.hasExternalIoTasks);
	setCommandEnabled(cmMrOtherRestartProgram, hasWindow && state.isCommunicationCommandWindow && !state.hasExternalIoTasks && state.hasExternalCommandDetail);
	setCommandEnabled(cmMrOtherClearOutput, hasWindow && ((state.isCommunicationWindow && !state.hasExternalIoTasks) || state.isLogWindow));
	setCommandEnabled(cmMrOtherFindNextCompilerError, state.hasCompilerProblems);
	setCommandEnabled(cmMrOtherFindPreviousCompilerError, state.hasCompilerProblems);
	setCommandEnabled(cmMrFileCompareApplyOriginalToCompare, state.hasFileCompareWindow);
	setCommandEnabled(cmMrFileCompareApplyCompareToOriginal, state.hasFileCompareWindow);
	setCommandEnabled(cmMrFileCompareNextChange, state.hasFileCompareWindow);
	setCommandEnabled(cmMrFileComparePreviousChange, state.hasFileCompareWindow);
	setCommandEnabled(cmMrMacroDebuggerContinue, hasEditor);
	setCommandEnabled(cmMrMacroDebuggerStep, hasEditor);
	setCommandEnabled(cmMrMacroDebuggerStepOver, hasEditor);
	setCommandEnabled(cmMrMacroDebuggerStepOut, hasEditor);
	setCommandEnabled(cmMrMacroDebuggerStop, hasEditor);
	setCommandEnabled(cmMrMacroDebuggerAddWatch, hasEditor);
	setCommandEnabled(cmMrMacroDebuggerEraseWatch, hasEditor);
	setCommandEnabled(cmMrMacroDebuggerRunHere, hasEditor);
	setCommandEnabled(cmMrMacroDebuggerEvaluate, hasEditor);
	setCommandEnabled(cmMrMacroDebuggerToggleBreakpointEnabled, hasEditor);
	setCommandEnabled(cmMrMacroDebuggerToggleAllBreakpoints, hasEditor);
	setCommandEnabled(cmMrMacroDebuggerClearAllBreakpoints, hasEditor);
	setCommandEnabled(cmMrDebuggerClearProgramTerminal, state.hasGdbDebugger);
	setCommandEnabled(cmMrOtherMatchBraceOrParen, hasEditor);
	setCommandEnabled(cmMrOtherLocalOutline, hasEditor);
	setCommandEnabled(cmMrMacroToggleRecording, hasEditor);
	setCommandEnabled(cmMrHelpContents, true);
	setCommandEnabled(cmMrSetupUserInterfaceSettings, true);
	setCommandEnabled(cmMrHelpPerformancePanel, true);
	setCommandEnabled(cmMrHelpUpdate, mrUpdateAvailable());
}

void updateAppCommandState() {
	updateAppCommandState(configuredVirtualDesktops(), configuredCyclicVirtualDesktops());
}
