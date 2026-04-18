#include "MRAppState.hpp"

#include "../app/commands/MRWindowCommands.hpp"
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
	bool blockMarking;
	bool hasMacroTasks;
	bool hasExternalIoTasks;
	bool isCommunicationWindow;
	bool isCommunicationCommandWindow;
	bool isLogWindow;

	AppCommandState()
	    : window(nullptr), windowCount(0), hasEditableWindow(false), hasReadOnlyWindow(false),
	      hasDirtyWindow(false), hasPersistentFileName(false), canSaveInPlace(false), hasSelection(false),
	      hasUndo(false), hasBlock(false), blockMarking(false), hasMacroTasks(false), hasExternalIoTasks(false),
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
	if (win == nullptr)
		return state;

	state.hasReadOnlyWindow = win->isReadOnly();
	state.hasEditableWindow = !state.hasReadOnlyWindow;
	state.hasDirtyWindow = win->isFileChanged();
	state.hasPersistentFileName = win->hasPersistentFileName();
	state.canSaveInPlace = win->canSaveInPlace();
	state.hasBlock = win->hasBlock();
	state.blockMarking = win->isBlockMarking();
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
	bool hasWindow = state.window != nullptr;
	bool hasEditor = hasWindow;
	bool canModify = hasEditor && state.hasEditableWindow;
	bool canSaveAs = hasEditor && (state.hasEditableWindow || state.isLogWindow);
	bool hasMultipleWindows = state.windowCount > 1;

	setCommandEnabled(cmMrFileOpen, true);
	setCommandEnabled(cmMrFileLoad, true);
	setCommandEnabled(cmMrFileSave, canModify && state.hasDirtyWindow);
	setCommandEnabled(cmMrFileSaveAs, canSaveAs);
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

	setCommandEnabled(cmMrWindowLink, hasMultipleWindows && hasEditor);
	setCommandEnabled(cmMrWindowUnlink, hasWindow);

	setCommandEnabled(cmMrBlockCopy, hasEditor && state.hasBlock);
	setCommandEnabled(cmMrBlockMove, canModify && state.hasBlock);
	setCommandEnabled(cmMrBlockDelete, canModify && state.hasBlock);
	setCommandEnabled(cmMrBlockSaveToDisk, hasEditor && state.hasBlock);
	setCommandEnabled(cmMrBlockIndent, canModify && state.hasBlock);
	setCommandEnabled(cmMrBlockUndent, canModify && state.hasBlock);
	setCommandEnabled(cmMrBlockWindowCopy, hasEditor && hasMultipleWindows);
	setCommandEnabled(cmMrBlockWindowMove, canModify && hasMultipleWindows);
	setCommandEnabled(cmMrBlockMarkLines, canModify && !state.blockMarking);
	setCommandEnabled(cmMrBlockMarkColumns, canModify);
	setCommandEnabled(cmMrBlockMarkStream, canModify);
	setCommandEnabled(cmMrBlockEndMarking, hasEditor && state.blockMarking);
	setCommandEnabled(cmMrBlockTurnMarkingOff, hasEditor && state.hasBlock);
	setCommandEnabled(cmMrBlockPersistent, hasEditor);

	setCommandEnabled(cmMrWindowOrganizeCascade, hasEditor);
	std::size_t numWindows = allEditWindowsInZOrder().size();
	setCommandEnabled(cmMrWindowOrganizeTile, hasEditor && numWindows < 10);
	setCommandEnabled(cmMrWindowOrganizeWindowManager, true);

	setCommandEnabled(cmMrSearchFindText, hasEditor);
	setCommandEnabled(cmMrSearchReplace, canModify);
	setCommandEnabled(cmMrSearchRepeatPrevious, hasEditor);
	setCommandEnabled(cmMrSearchPushMarker, hasEditor);
	setCommandEnabled(cmMrSearchGetMarker, hasEditor);
	setCommandEnabled(cmMrSearchSetRandomAccessMark, hasEditor);
	setCommandEnabled(cmMrSearchRetrieveRandomAccessMark, hasEditor);
	setCommandEnabled(cmMrSearchGotoLineNumber, hasEditor);

	setCommandEnabled(cmMrTextLayout, hasEditor);
	setCommandEnabled(cmMrTextUpperCaseMenu, canModify && state.hasSelection);
	setCommandEnabled(cmMrTextLowerCaseMenu, canModify && state.hasSelection);
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
	setCommandEnabled(cmMrDevCancelMacroTasks, hasWindow && state.hasMacroTasks);
	setCommandEnabled(cmMrDevHeroEventProbe, true);
}
