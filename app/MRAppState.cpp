#include "MRAppState.hpp"

#include "../services/MRWindowCommands.hpp"
#include "../ui/TMREditWindow.hpp"
#include "MRCommands.hpp"

namespace {
struct AppCommandState {
	TMREditWindow *window;
	std::size_t windowCount;
	bool hasEditableWindow;
	bool hasReadOnlyWindow;
	bool hasDirtyWindow;
	bool hasPersistentFileName;
	bool canSaveInPlace;
	bool hasSelection;
	bool hasUndo;
	bool hasBlock;
	bool hasMacroTasks;
	bool hasExternalIoTasks;
	bool isCommunicationWindow;
	bool isCommunicationCommandWindow;
	bool isLogWindow;

	AppCommandState()
	    : window(0), windowCount(0), hasEditableWindow(false), hasReadOnlyWindow(false),
	      hasDirtyWindow(false), hasPersistentFileName(false), canSaveInPlace(false), hasSelection(false),
	      hasUndo(false), hasBlock(false), hasMacroTasks(false), hasExternalIoTasks(false),
	      isCommunicationWindow(false), isCommunicationCommandWindow(false), isLogWindow(false) {
	}
};

void setCommandEnabled(ushort command, bool enabled) {
	if (enabled)
		TView::enableCommand(command);
	else
		TView::disableCommand(command);
}

AppCommandState appCommandState() {
	AppCommandState state;
	TMREditWindow *win = currentEditWindow();

	state.window = win;
	state.windowCount = allEditWindowsInZOrder().size();
	if (win == 0)
		return state;

	state.hasReadOnlyWindow = win->isReadOnly();
	state.hasEditableWindow = !state.hasReadOnlyWindow;
	state.hasDirtyWindow = win->isFileChanged();
	state.hasPersistentFileName = win->hasPersistentFileName();
	state.canSaveInPlace = win->canSaveInPlace();
	state.hasBlock = win->hasBlock();
	state.hasSelection = win->hasSelection();
	state.hasUndo = win->hasUndoHistory();
	state.hasMacroTasks = win->hasTrackedMacroTasks();
	state.hasExternalIoTasks = win->hasTrackedExternalIoTasks();
	state.isCommunicationWindow = win->isCommunicationWindow();
	state.isCommunicationCommandWindow = win->windowRole() == TMREditWindow::wrCommunicationCommand;
	state.isLogWindow = win->windowRole() == TMREditWindow::wrLog;
	return state;
}
} // namespace

void updateAppCommandState() {
	AppCommandState state = appCommandState();
	bool hasWindow = state.window != 0;
	bool hasEditor = hasWindow;
	bool canModify = hasEditor && state.hasEditableWindow;
	bool hasMultipleWindows = state.windowCount > 1;

	setCommandEnabled(cmMrFileOpen, true);
	setCommandEnabled(cmMrFileLoad, true);
	setCommandEnabled(cmMrFileSave, canModify && state.hasDirtyWindow);
	setCommandEnabled(cmMrFileSaveAs, canModify);
	setCommandEnabled(cmMrFileInformation, hasEditor);
	setCommandEnabled(cmMrFileMerge, hasEditor);
	setCommandEnabled(cmMrFilePrint, hasEditor);
	setCommandEnabled(cmMrFileShellToDos, true);

	setCommandEnabled(cmMrEditUndo, canModify && state.hasUndo);
	setCommandEnabled(cmMrEditCutToBuffer, canModify && state.hasSelection);
	setCommandEnabled(cmMrEditCopyToBuffer, hasEditor && state.hasSelection);
	setCommandEnabled(cmMrEditAppendToBuffer, canModify && state.hasSelection);
	setCommandEnabled(cmMrEditCutAndAppendToBuffer, canModify && state.hasSelection);
	setCommandEnabled(cmMrEditPasteFromBuffer, canModify);
	setCommandEnabled(cmMrEditRepeatCommand, hasEditor);

	setCommandEnabled(cmMrWindowClose, hasWindow);
	setCommandEnabled(cmMrWindowSplit, false);
	setCommandEnabled(cmMrWindowList, state.windowCount > 0);
	setCommandEnabled(cmMrWindowNext, hasMultipleWindows);
	setCommandEnabled(cmMrWindowPrevious, hasMultipleWindows);
	setCommandEnabled(cmMrWindowAdjacent, false);
	setCommandEnabled(cmMrWindowHide, hasWindow);
	setCommandEnabled(cmMrWindowModifySize, hasWindow);
	setCommandEnabled(cmMrWindowZoom, hasWindow);
	setCommandEnabled(cmMrWindowMinimize, false);
	setCommandEnabled(cmMrWindowOrganizePlaceholder, false);
	setCommandEnabled(cmMrWindowLink, hasMultipleWindows && hasEditor);
	setCommandEnabled(cmMrWindowUnlink, hasWindow);

	setCommandEnabled(cmMrBlockCopy, hasEditor && state.hasBlock);
	setCommandEnabled(cmMrBlockMove, canModify && state.hasBlock);
	setCommandEnabled(cmMrBlockDelete, canModify && state.hasBlock);
	setCommandEnabled(cmMrBlockSaveToDisk, hasEditor && state.hasBlock);
	setCommandEnabled(cmMrBlockIndent, canModify && state.hasBlock);
	setCommandEnabled(cmMrBlockUndent, canModify && state.hasBlock);
	setCommandEnabled(cmMrBlockWindowCopy, hasEditor && state.hasBlock && hasMultipleWindows);
	setCommandEnabled(cmMrBlockWindowMove, canModify && state.hasBlock && hasMultipleWindows);
	setCommandEnabled(cmMrBlockMarkLines, canModify);
	setCommandEnabled(cmMrBlockMarkColumns, canModify);
	setCommandEnabled(cmMrBlockMarkStream, canModify);
	setCommandEnabled(cmMrBlockEndMarking, hasEditor && state.hasBlock);
	setCommandEnabled(cmMrBlockTurnMarkingOff, hasEditor && state.hasBlock);
	setCommandEnabled(cmMrBlockPersistent, false);

	setCommandEnabled(cmMrSearchFindText, hasEditor);
	setCommandEnabled(cmMrSearchReplace, canModify);
	setCommandEnabled(cmMrSearchRepeatPrevious, hasEditor);
	setCommandEnabled(cmMrSearchPushMarker, hasEditor);
	setCommandEnabled(cmMrSearchGetMarker, hasEditor);
	setCommandEnabled(cmMrSearchSetRandomAccessMark, hasEditor);
	setCommandEnabled(cmMrSearchRetrieveRandomAccessMark, hasEditor);
	setCommandEnabled(cmMrSearchGotoLineNumber, hasEditor);

	setCommandEnabled(cmMrTextLayout, hasEditor);
	setCommandEnabled(cmMrTextUpperCaseMenu, canModify);
	setCommandEnabled(cmMrTextLowerCaseMenu, canModify);
	setCommandEnabled(cmMrTextCenterLine, canModify);
	setCommandEnabled(cmMrTextTimeDateStamp, canModify);
	setCommandEnabled(cmMrTextReformatParagraph, canModify);
	setCommandEnabled(cmMrOtherStopProgram, hasWindow && state.hasExternalIoTasks);
	setCommandEnabled(cmMrOtherRestartProgram,
	                  hasWindow && state.isCommunicationCommandWindow && !state.hasExternalIoTasks &&
	                      !state.window->windowRoleDetail().empty());
	setCommandEnabled(cmMrOtherClearOutput,
	                  hasWindow && ((state.isCommunicationWindow && !state.hasExternalIoTasks) || state.isLogWindow));
	setCommandEnabled(cmMrMacroToggleRecording, hasEditor);
	setCommandEnabled(cmMrDevRunMacro, true);
	setCommandEnabled(cmMrDevCancelMacroTasks, hasWindow && state.hasMacroTasks);
}
