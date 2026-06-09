#include "MRAppState.hpp"

#include "router/MRCommandRouterSearchMultiFile.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../ui/MRBentoBox.hpp"
#include "../ui/MREditWindow.hpp"
#include "MRCommands.hpp"

namespace {
struct AppCommandState {
	MREditWindow *window;
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

	AppCommandState() : window(nullptr), windowCount(0), isMinimizedWindow(false), hasEditableWindow(false), hasReadOnlyWindow(false), hasDirtyWindow(false), hasAnyDirtyWindow(false), hasPersistentFileName(false), hasBuildSourceFile(false), canSaveInPlace(false), hasSelection(false), hasUndo(false), hasRedo(false), hasBlock(false), blockMarking(false), hasMacroTasks(false), hasExternalIoTasks(false), isCommunicationWindow(false), isCommunicationCommandWindow(false), isLogWindow(false), hasExternalCommandDetail(false), hasCompilerProblems(false), hasFileCompareWindow(false) {
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
	state.windowCount = allEditWindowsInZOrder().size();
	for (MREditWindow *window : allEditWindowsInZOrder())
		if (window != nullptr && window->isFileChanged() && !window->isReadOnly()) {
			state.hasAnyDirtyWindow = true;
			break;
		}
	if (win == nullptr) return state;
	MREditWindow *externalWin = win;
	for (TView *view = win; view != nullptr; view = view->owner) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(view);
		if (bentoBox != nullptr && bentoBox->isFileCompareBox()) {
			state.hasFileCompareWindow = true;
			break;
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

	state.isMinimizedWindow = win->isMinimized();
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

void updateAppCommandState() {
	AppCommandState state = appCommandState();
	bool hasWindow = state.window != nullptr;
	bool hasEditor = hasWindow;
	bool canModify = hasEditor && state.hasEditableWindow;
	bool canSaveAs = hasEditor && (state.hasEditableWindow || state.isLogWindow);
	bool hasMultipleWindows = state.windowCount > 1;

	setCommandEnabled(cmMrToggleFullscreen, true);

	setCommandEnabled(cmMrFileOpen, true);
	setCommandEnabled(cmMrFileLoad, true);
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
	setCommandEnabled(cmMrEditCutToBuffer, canModify && state.hasSelection);
	setCommandEnabled(cmMrEditCopyToBuffer, hasEditor && state.hasSelection);
	setCommandEnabled(cmMrEditAppendToBuffer, false);
	setCommandEnabled(cmMrEditCutAndAppendToBuffer, false);
	setCommandEnabled(cmMrEditPasteFromBuffer, canModify);
	setCommandEnabled(cmMrEditToggleInsertMode, hasEditor);

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
	setCommandEnabled(cmMrWindowSplitHorizontal, hasWindow);
	setCommandEnabled(cmMrWindowSplitVertical, hasWindow);
	{
		const int desktopCount = configuredVirtualDesktops();
		const int currentDesktop = currentVirtualDesktop();
		const bool hasMultipleDesktops = desktopCount > 1;
		const bool cyclicViewport = hasMultipleDesktops && configuredCyclicVirtualDesktops();
		const int windowDesktop = hasWindow ? state.window->mVirtualDesktop : 1;

		setCommandEnabled(cmMrWindowNextDesktop, hasMultipleDesktops && (currentDesktop < desktopCount || cyclicViewport));
		setCommandEnabled(cmMrWindowPrevDesktop, hasMultipleDesktops && (currentDesktop > 1 || cyclicViewport));
		setCommandEnabled(cmMrWindowMoveToNextDesktop, hasWindow && hasMultipleDesktops && windowDesktop < desktopCount);
		setCommandEnabled(cmMrWindowMoveToPrevDesktop, hasWindow && hasMultipleDesktops && windowDesktop > 1);
	}
	setCommandEnabled(cmMrWindowLink, hasMultipleWindows && hasEditor);
	setCommandEnabled(cmMrWindowUnlink, hasWindow);

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
	setCommandEnabled(cmMrTextFileCompare, hasEditor && hasMultipleWindows);
	setCommandEnabled(cmMrOtherBuildCurrentFile, hasEditor && state.hasBuildSourceFile);
	setCommandEnabled(cmMrOtherStopProgram, hasWindow && state.hasExternalIoTasks);
	setCommandEnabled(cmMrOtherRestartProgram, hasWindow && state.isCommunicationCommandWindow && !state.hasExternalIoTasks && state.hasExternalCommandDetail);
	setCommandEnabled(cmMrOtherClearOutput, hasWindow && ((state.isCommunicationWindow && !state.hasExternalIoTasks) || state.isLogWindow));
	setCommandEnabled(cmMrOtherFindNextCompilerError, state.hasCompilerProblems);
	setCommandEnabled(cmMrOtherFindPreviousCompilerError, state.hasCompilerProblems);
	setCommandEnabled(cmMrFileCompareNextChange, state.hasFileCompareWindow);
	setCommandEnabled(cmMrFileComparePreviousChange, state.hasFileCompareWindow);
	setCommandEnabled(cmMrOtherMatchBraceOrParen, hasEditor);
	setCommandEnabled(cmMrOtherAsciiTable, canModify);
	setCommandEnabled(cmMrOtherEmojiTable, canModify);
	setCommandEnabled(cmMrMacroToggleRecording, hasEditor);
	setCommandEnabled(cmMrHelpContents, true);
	setCommandEnabled(cmMrSetupUserInterfaceSettings, true);
	setCommandEnabled(cmMrHelpPerformancePanel, true);
}
