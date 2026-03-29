#define Uses_TKeys
#define Uses_TApplication
#define Uses_TEvent
#define Uses_TRect
#define Uses_TMenuBar
#define Uses_TMenu
#define Uses_TSubMenu
#define Uses_TMenuItem
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_TDeskTop
#define Uses_TObject
#define Uses_MsgBox
#define Uses_TWindow
#define Uses_TEditWindow
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TFileDialog
#include <tvision/tv.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

#include "mrmac/mrmac.h"
#include "mrmac/mrvm.hpp"
#include "ui/TMRDeskTop.hpp"
#include "ui/TMREditWindow.hpp"
#include "ui/TMRMenuBar.hpp"
#include "ui/TMRStatusLine.hpp"
#include "ui/mrmacrotest.hpp"
#include "ui/mrpalette.hpp"
#include "ui/mrwindowlist.hpp"

namespace {
enum : ushort {
	cmMrFileOpen = 1000,
	cmMrFileLoad,
	cmMrFileSave,
	cmMrFileSaveAs,
	cmMrFileInformation,
	cmMrFileMerge,
	cmMrFilePrint,
	cmMrFileShellToDos,

	cmMrEditUndo,
	cmMrEditCutToBuffer,
	cmMrEditCopyToBuffer,
	cmMrEditAppendToBuffer,
	cmMrEditCutAndAppendToBuffer,
	cmMrEditPasteFromBuffer,
	cmMrEditRepeatCommand,

	cmMrWindowOpen,
	cmMrWindowClose,
	cmMrWindowSplit,
	cmMrWindowList,
	cmMrWindowNext,
	cmMrWindowPrevious,
	cmMrWindowAdjacent,
	cmMrWindowHide,
	cmMrWindowModifySize,
	cmMrWindowZoom,
	cmMrWindowMinimize,
	cmMrWindowOrganize,
	cmMrWindowLink,
	cmMrWindowUnlink,
	cmMrWindowOrganizePlaceholder,

	cmMrBlockCopy,
	cmMrBlockMove,
	cmMrBlockDelete,
	cmMrBlockSaveToDisk,
	cmMrBlockIndent,
	cmMrBlockUndent,
	cmMrBlockWindowCopy,
	cmMrBlockWindowMove,
	cmMrBlockMarkLines,
	cmMrBlockMarkColumns,
	cmMrBlockMarkStream,
	cmMrBlockEndMarking,
	cmMrBlockTurnMarkingOff,
	cmMrBlockPersistent,

	cmMrSearchFindText,
	cmMrSearchReplace,
	cmMrSearchRepeatPrevious,
	cmMrSearchPushMarker,
	cmMrSearchGetMarker,
	cmMrSearchSetRandomAccessMark,
	cmMrSearchRetrieveRandomAccessMark,
	cmMrSearchGotoLineNumber,

	cmMrTextLayout,
	cmMrTextUpperCaseMenu,
	cmMrTextLowerCaseMenu,
	cmMrTextCenterLine,
	cmMrTextTimeDateStamp,
	cmMrTextReformatParagraph,
	cmMrTextUpperCasePlaceholder,
	cmMrTextLowerCasePlaceholder,

	cmMrOtherInstallationAndSetup,
	cmMrOtherKeystrokeMacros,
	cmMrOtherExecuteProgram,
	cmMrOtherFindNextCompilerError,
	cmMrOtherMatchBraceOrParen,
	cmMrOtherAsciiTable,

	cmMrHelpContents,
	cmMrHelpKeys,
	cmMrHelpDetailedIndex,
	cmMrHelpPreviousTopic,
	cmMrHelpAbout,

	cmMrSetupEditSettings,
	cmMrSetupDisplaySetup,
	cmMrSetupColorSetup,
	cmMrSetupKeyMapping,
	cmMrSetupMouseKeyRepeat,
	cmMrSetupFilenameExtensions,
	cmMrSetupSwappingEmsXms,
	cmMrSetupBackupsTempAutosave,
	cmMrSetupSearchAndReplaceDefaults,
	cmMrSetupUserInterfaceSettings,
	cmMrSetupSaveConfigurationAndExit,
	cmMrSetupSearchAndReplacePlaceholder,

	cmMrColorWindowColors,
	cmMrColorMenuDialogColors,
	cmMrColorHelpColors,
	cmMrColorOtherColors,

	cmMrDevRunMacro
};


TRect centeredRect(int width, int height) {
	TRect r = TProgram::deskTop->getExtent();
	int left = r.a.x + (r.b.x - r.a.x - width) / 2;
	int top = r.a.y + (r.b.y - r.a.y - height) / 2;
	return TRect(left, top, left + width, top + height);
}

void insertStaticLine(TDialog *dialog, int x, int y, const char *text) {
	dialog->insert(new TStaticText(TRect(x, y, x + std::strlen(text) + 1, y + 1), text));
}

ushort execDialog(TDialog *dialog) {
	ushort result = cmCancel;
	if (dialog != 0) {
		result = TProgram::deskTop->execView(dialog);
		TObject::destroy(dialog);
		if (result == cmHelp)
			mrShowProjectHelp();
	}
	return result;
}

std::vector<TMREditWindow *> allEditWindowsInZOrder();
TDialog *createSimplePreviewDialog(const char *title, int width, int height,
                                   const std::vector<std::string> &lines,
                                   bool showOkCancelHelp = false);

ushort execDialogWithData(TDialog *dialog, void *data) {
	ushort result = cmCancel;
	if (dialog == 0)
		return cmCancel;
	if (data != 0)
		dialog->setData(data);
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel && data != 0)
		dialog->getData(data);
	TObject::destroy(dialog);
	if (result == cmHelp)
		mrShowProjectHelp();
	return result;
}

std::string normalizeTvPath(const std::string &path) {
	std::string result = path;
	std::size_t i;

	for (i = 0; i < result.size(); ++i)
		if (result[i] == '\\')
			result[i] = '/';
	#ifdef __unix__
	if (result.size() >= 2 && ((result[0] >= 'A' && result[0] <= 'Z') ||
	                           (result[0] >= 'a' && result[0] <= 'z')) &&
	    result[1] == ':')
		result.erase(0, 2);
	#endif
	return result;
}

std::string expandUserPath(const char *path) {
	std::string result;

	if (path == 0)
		return std::string();
	result = normalizeTvPath(path);
	if (result.size() >= 2 && result[0] == '~' && result[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != 0 && *home != '\0')
			return std::string(home) + result.substr(1);
	}
	return result;
}

bool isEmptyUntitledEditableWindow(TMREditWindow *win) {
	TFileEditor *editor;
	if (win == 0 || win->isReadOnly() || win->currentFileName()[0] != '\0' || win->isFileChanged())
		return false;
	editor = win->getEditor();
	return editor != 0 && editor->bufLen == 0;
}

TMREditWindow *findReusableEmptyWindow(TMREditWindow *preferred) {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	if (preferred != 0 && isEmptyUntitledEditableWindow(preferred))
		return preferred;
	for (std::size_t i = 0; i < windows.size(); ++i) {
		if (isEmptyUntitledEditableWindow(windows[i]))
			return windows[i];
	}
	return 0;
}

bool promptForPath(const char *title, char *fileName, std::size_t fileNameSize) {
	if (fileName == 0 || fileNameSize == 0)
		return false;
	std::memset(fileName, 0, fileNameSize);
	strnzcpy(fileName, "*.*", fileNameSize);
	return execDialogWithData(new TFileDialog("*.*", title, "~N~ame", fdOpenButton, 100), fileName) !=
	       cmCancel;
}

bool loadFileIntoWindow(TMREditWindow *win, const char *path) {
	std::string resolvedPath = expandUserPath(path);
	if (win == 0)
		return false;
	if (resolvedPath.empty()) {
		messageBox(mfError | mfOKButton, "No file name specified.");
		return false;
	}
	if (access(resolvedPath.c_str(), F_OK) != 0) {
		messageBox(mfError | mfOKButton, "File does not exist:\n%s", resolvedPath.c_str());
		return false;
	}
	if (access(resolvedPath.c_str(), R_OK) != 0) {
		messageBox(mfError | mfOKButton, "File is not readable:\n%s", resolvedPath.c_str());
		return false;
	}
	if (!win->loadFromFile(resolvedPath.c_str())) {
		messageBox(mfError | mfOKButton, "Unable to load file:\n%s", resolvedPath.c_str());
		return false;
	}
	return true;
}

void collectEditWindowsInZOrder(TView *view, void *arg) {
	std::vector<TMREditWindow *> *windows = static_cast<std::vector<TMREditWindow *> *>(arg);
	TMREditWindow *win = dynamic_cast<TMREditWindow *>(view);

	if (windows != 0 && win != 0)
		windows->push_back(win);
}

std::vector<TMREditWindow *> allEditWindowsInZOrder() {
	std::vector<TMREditWindow *> windows;

	if (TProgram::deskTop == 0)
		return windows;

	/* TGroup uses a ring list; iterate via forEach instead of walking until nullptr. */
	TProgram::deskTop->forEach(collectEditWindowsInZOrder, &windows);
	return windows;
}

TMREditWindow *currentEditWindow() {
	if (TProgram::deskTop == 0 || TProgram::deskTop->current == 0)
		return 0;
	return dynamic_cast<TMREditWindow *>(TProgram::deskTop->current);
}

bool closeCurrentEditWindow() {
	TMREditWindow *win = currentEditWindow();
	if (win == 0)
		return false;
	message(win, evCommand, cmClose, 0);
	return mrEnsureUsableWorkWindow();
}

bool activateRelativeEditWindow(int delta) {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	TMREditWindow *current = currentEditWindow();
	std::size_t index;

	if (windows.empty())
		return false;
	if (current == 0)
		return mrActivateEditWindow(windows.front());

	for (index = 0; index < windows.size(); ++index) {
		if (windows[index] == current) {
			int nextIndex = static_cast<int>(index) + delta;
			int count = static_cast<int>(windows.size());

			while (nextIndex < 0)
				nextIndex += count;
			nextIndex %= count;
			return mrActivateEditWindow(windows[static_cast<std::size_t>(nextIndex)]);
		}
	}
	return mrActivateEditWindow(windows.front());
}

bool hideCurrentEditWindow() {
	TMREditWindow *win = currentEditWindow();
	if (win == 0)
		return false;
	win->hide();
	return mrEnsureUsableWorkWindow();
}

struct AppCommandState {
	TMREditWindow *window;
	TFileEditor *editor;
	std::size_t windowCount;
	bool hasEditableWindow;
	bool hasReadOnlyWindow;
	bool hasDirtyWindow;
	bool hasPersistentFileName;
	bool canSaveInPlace;
	bool hasSelection;
	bool hasUndo;
	bool hasBlock;

	AppCommandState()
	    : window(0), editor(0), windowCount(0), hasEditableWindow(false), hasReadOnlyWindow(false),
	      hasDirtyWindow(false), hasPersistentFileName(false), canSaveInPlace(false),
	      hasSelection(false), hasUndo(false), hasBlock(false) {
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

	state.editor = win->getEditor();
	state.hasReadOnlyWindow = win->isReadOnly();
	state.hasEditableWindow = !state.hasReadOnlyWindow;
	state.hasDirtyWindow = win->isFileChanged();
	state.hasPersistentFileName = win->hasPersistentFileName();
	state.canSaveInPlace = win->canSaveInPlace();
	state.hasBlock = win->hasBlock();
	if (state.editor != 0) {
		state.hasSelection = state.editor->hasSelection() == True;
		state.hasUndo = state.editor->delCount != 0 || state.editor->insCount != 0;
	}
	return state;
}

bool saveCurrentEditWindow() {
	TMREditWindow *win = currentEditWindow();

	if (win == 0)
		return false;
	if (win->isReadOnly()) {
		messageBox(mfInformation | mfOKButton, "Window is read-only.");
		mrLogMessage("Save rejected for read-only window.");
		return false;
	}
	if (!win->canSaveInPlace()) {
		messageBox(mfInformation | mfOKButton, "Save As is not available yet for this window.");
		mrLogMessage("Save rejected because the window has no persistent file name.");
		return false;
	}
	if (!win->isFileChanged())
		return true;
	if (!win->saveCurrentFile()) {
		mrLogMessage("Save failed.");
		return false;
	}
	mrLogMessage("Window saved.");
	return true;
}

void updateAppCommandState() {
	AppCommandState state = appCommandState();
	bool hasWindow = state.window != 0;
	bool hasEditor = state.editor != 0;
	bool canModify = hasEditor && state.hasEditableWindow;
	bool hasMultipleWindows = state.windowCount > 1;

	setCommandEnabled(cmMrFileOpen, true);
	setCommandEnabled(cmMrFileLoad, true);
	setCommandEnabled(cmMrFileSave, hasEditor && state.canSaveInPlace && state.hasDirtyWindow);
	setCommandEnabled(cmMrFileSaveAs, false);
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
}

std::string shortenForDialog(const std::string &value, std::size_t maxLen) {
	if (value.size() <= maxLen)
		return value;
	if (maxLen <= 3)
		return value.substr(0, maxLen);
	return value.substr(0, maxLen - 3) + "...";
}

std::string yesNo(bool value) {
	return value ? "Yes" : "No";
}

std::size_t countEditorLines(TFileEditor *editor) {
	std::size_t lines = 1;
	uint i;

	if (editor == 0 || editor->bufLen == 0)
		return 1;
	for (i = 0; i < editor->bufLen; ++i) {
		if (editor->bufChar(i) == '\n')
			++lines;
	}
	return lines;
}

const char *blockModeLabel(TMREditWindow *win) {
	if (win == 0)
		return "None";
	switch (win->blockStatus()) {
		case TMREditWindow::bmLine:
			return "Line";
		case TMREditWindow::bmColumn:
			return "Column";
		case TMREditWindow::bmStream:
			return "Stream";
		default:
			return "None";
	}
}

std::vector<std::string> buildFileInformationLines(TMREditWindow *win) {
	std::vector<std::string> lines;
	TFileEditor *editor = win != 0 ? win->getEditor() : 0;
	std::string path = win != 0 ? win->currentFileName() : std::string();
	std::string title = win != 0 && win->getTitle(0) != 0 ? win->getTitle(0) : "?No-File?";
	struct stat st;
	bool hasStat = !path.empty() && ::stat(path.c_str(), &st) == 0;
	char buffer[128];

	lines.push_back(std::string("Title         : ") + shortenForDialog(title, 56));
	lines.push_back(std::string("Path          : ") +
	                shortenForDialog(path.empty() ? std::string("<none>") : path, 56));
	lines.push_back(std::string("Read-only     : ") + yesNo(win != 0 && win->isReadOnly()));
	lines.push_back(std::string("Modified      : ") + yesNo(win != 0 && win->isFileChanged()));
	lines.push_back(std::string("Save in place : ") + yesNo(win != 0 && win->canSaveInPlace()));
	std::snprintf(buffer, sizeof(buffer), "%u bytes", editor != 0 ? editor->bufLen : 0);
	lines.push_back(std::string("Buffer size   : ") + buffer);
	if (hasStat)
		std::snprintf(buffer, sizeof(buffer), "%lld bytes", static_cast<long long>(st.st_size));
	else
		std::snprintf(buffer, sizeof(buffer), "<n/a>");
	lines.push_back(std::string("On disk       : ") + buffer);
	std::snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(countEditorLines(editor)));
	lines.push_back(std::string("Lines         : ") + buffer);
	if (editor != 0)
		std::snprintf(buffer, sizeof(buffer), "line %d, column %d", editor->curPos.y + 1, editor->curPos.x + 1);
	else
		std::snprintf(buffer, sizeof(buffer), "<n/a>");
	lines.push_back(std::string("Cursor        : ") + buffer);
	std::snprintf(buffer, sizeof(buffer), "%d", win != 0 ? win->bufferId() : 0);
	lines.push_back(std::string("Buffer id     : ") + buffer);
	lines.push_back(std::string("Block mode    : ") + blockModeLabel(win));
	lines.push_back(std::string("Visible       : ") + yesNo(win != 0 && (win->state & sfVisible) != 0));
	return lines;
}

void showFileInformationDialog(TMREditWindow *win) {
	if (win == 0) {
		messageBox(mfInformation | mfOKButton, "No active file window.");
		return;
	}
	execDialog(createSimplePreviewDialog("FILE INFORMATION", 74, 17, buildFileInformationLines(win)));
}

TDialog *createSimplePreviewDialog(const char *title, int width, int height,
                                   const std::vector<std::string> &lines,
                                   bool showOkCancelHelp) {
	TDialog *dialog = new TDialog(centeredRect(width, height), title);
	int y = 2;

	for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it, ++y)
		insertStaticLine(dialog, 2, y, it->c_str());

	if (showOkCancelHelp) {
		dialog->insert(new TButton(TRect(width - 34, height - 3, width - 24, height - 1), "OK",
		                           cmOK, bfDefault));
		dialog->insert(new TButton(TRect(width - 23, height - 3, width - 10, height - 1), "Cancel",
		                           cmCancel, bfNormal));
		dialog->insert(new TButton(TRect(width - 9, height - 3, width - 2, height - 1), "Help",
		                           cmHelp, bfNormal));
	} else
		dialog->insert(new TButton(TRect(width / 2 - 4, height - 3, width / 2 + 4, height - 1),
		                           "Done", cmOK, bfDefault));

	return dialog;
}

TDialog *createInstallationAndSetupDialog() {
	TDialog *dialog = new TDialog(centeredRect(56, 21), "INSTALLATION AND SETUP");

	insertStaticLine(dialog, 2, 2, "DOS-5.0   CPU=80486");
	insertStaticLine(dialog, 2, 3, "Video Card = VGA Color");
	insertStaticLine(dialog, 2, 4, "ME Path = C:\\");
	insertStaticLine(dialog, 2, 5, "Serial #  Test");

	dialog->insert(
	    new TButton(TRect(2, 7, 28, 9), "Edit settings...", cmMrSetupEditSettings, bfNormal));
	dialog->insert(
	    new TButton(TRect(2, 9, 28, 11), "Display setup...", cmMrSetupDisplaySetup, bfNormal));
	dialog->insert(
	    new TButton(TRect(2, 11, 28, 13), "Color setup...", cmMrSetupColorSetup, bfNormal));
	dialog->insert(
	    new TButton(TRect(2, 13, 28, 15), "Key mapping...", cmMrSetupKeyMapping, bfNormal));
	dialog->insert(new TButton(TRect(29, 7, 54, 9), "Mouse / Key repeat...",
	                           cmMrSetupMouseKeyRepeat, bfNormal));
	dialog->insert(new TButton(TRect(29, 9, 54, 11), "Filename extensions...",
	                           cmMrSetupFilenameExtensions, bfNormal));
	dialog->insert(new TButton(TRect(29, 11, 54, 13), "Swapping / EMS / XMS...",
	                           cmMrSetupSwappingEmsXms, bfNormal));
	dialog->insert(new TButton(TRect(29, 13, 54, 15), "Backups / Temp / Autosave...",
	                           cmMrSetupBackupsTempAutosave, bfNormal));
	dialog->insert(new TButton(TRect(2, 15, 28, 17), "Search and Replace...",
	                           cmMrSetupSearchAndReplaceDefaults, bfNormal));
	dialog->insert(new TButton(TRect(29, 15, 54, 17), "User interface settings...",
	                           cmMrSetupUserInterfaceSettings, bfNormal));
	dialog->insert(new TButton(TRect(2, 18, 28, 20), "Exit Setup", cmCancel, bfNormal));
	dialog->insert(new TButton(TRect(29, 18, 54, 20), "Save configuration",
	                           cmMrSetupSaveConfigurationAndExit, bfDefault));

	return dialog;
}

TDialog *createColorSetupDialog() {
	TDialog *dialog = new TDialog(centeredRect(32, 11), "COLOR SETUP");
	dialog->insert(
	    new TButton(TRect(2, 2, 29, 4), "Window colors", cmMrColorWindowColors, bfNormal));
	dialog->insert(
	    new TButton(TRect(2, 4, 29, 6), "Menu/Dialog colors", cmMrColorMenuDialogColors, bfNormal));
	dialog->insert(new TButton(TRect(2, 6, 29, 8), "Help colors", cmMrColorHelpColors, bfNormal));
	dialog->insert(
	    new TButton(TRect(2, 8, 29, 10), "Other colors", cmMrColorOtherColors, bfNormal));
	return dialog;
}

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

TDialog *createEditSettingsDialog() {
	std::vector<std::string> lines;
	lines.push_back("Page break string........ ?");
	lines.push_back("Word delimits........... .()'\",#$012%^&*+-/[]?");
	lines.push_back("Max undo count.......... 32000");
	lines.push_back("Default file extension(s) C:PAS;ASM;BAT;TXT;DO");
	lines.push_back("");
	lines.push_back("Cursor: Insert / Overwrite / Underline / 1/2 block");
	lines.push_back("        2/3 block / Full block");
	lines.push_back("Options: [X] Truncate spaces");
	lines.push_back("         [ ] Control-Z at EOF");
	lines.push_back("         [ ] CR/LF at EOF");
	lines.push_back("Tab expand: Spaces");
	lines.push_back("Column block move style: Delete space");
	lines.push_back("Default mode: Insert");
	return createSimplePreviewDialog("EDIT SETTINGS", 76, 20, lines, true);
}

TDialog *createDisplaySetupDialog() {
	std::vector<std::string> lines;
	lines.push_back("Video mode");
	lines.push_back("  (*) 25 lines");
	lines.push_back("  ( ) 30/33 lines");
	lines.push_back("  ( ) 43/50 lines");
	lines.push_back("  ( ) UltraVision");
	lines.push_back("F-key labels delay (1/10 secs): 3");
	lines.push_back("UltraVision mode (hex): 00");
	lines.push_back("");
	lines.push_back("Screen layout");
	lines.push_back("  [X] Status/message line");
	lines.push_back("  [X] Menu bar");
	lines.push_back("  [X] Function key labels");
	lines.push_back("  [X] Left-hand border");
	lines.push_back("  [X] Right-hand border");
	lines.push_back("  [X] Bottom border");
	return createSimplePreviewDialog("DISPLAY SETUP", 60, 20, lines, true);
}

TDialog *createWindowColorsDialog() {
	std::vector<std::string> lines;
	lines.push_back("Text");
	lines.push_back("Changed-Text");
	lines.push_back("Highlighted-Text");
	lines.push_back("End-Of-File");
	lines.push_back("Window-border");
	lines.push_back("window-Bold");
	lines.push_back("cUrrent line");
	lines.push_back("cuRrent line in block");
	lines.push_back("");
	lines.push_back("Preview:");
	lines.push_back("Normal text");
	lines.push_back("Changed text");
	lines.push_back("Highlighted text");
	lines.push_back("Current line");
	lines.push_back("Current line in block");
	return createSimplePreviewDialog("WINDOW COLORS", 34, 20, lines, false);
}

TDialog *createMenuDialogColorsDialog() {
	std::vector<std::string> lines;
	lines.push_back("Menu-Text");
	lines.push_back("menu-Highlight");
	lines.push_back("menu-Bold");
	lines.push_back("menu-skip");
	lines.push_back("Menu-border");
	lines.push_back("bUtton");
	lines.push_back("button-Key");
	lines.push_back("button-shAdow");
	lines.push_back("Select");
	lines.push_back("Not-select");
	lines.push_back("Checkbox bold");
	lines.push_back("");
	lines.push_back("Preview:");
	lines.push_back("Window  Menu  File  Block");
	lines.push_back("(*) Select   ( ) Not-select");
	lines.push_back("Button<KEY>");
	return createSimplePreviewDialog("MENU / DIALOG COLORS", 38, 21, lines, false);
}

TDialog *createHelpColorsDialog() {
	std::vector<std::string> lines;
	lines.push_back("Help-Text");
	lines.push_back("help-Highlight");
	lines.push_back("help-Chapter");
	lines.push_back("help-Border");
	lines.push_back("help-Link");
	lines.push_back("help-F-keys");
	lines.push_back("help-attr-1");
	lines.push_back("help-attr-2");
	lines.push_back("help-attr-3");
	lines.push_back("");
	lines.push_back("SAMPLE HELP");
	lines.push_back("CHAPTER HEADING");
	lines.push_back("This is help text.");
	lines.push_back("This is a LINK");
	lines.push_back("Attr1, Attr2, Attr3");
	return createSimplePreviewDialog("HELP COLORS", 36, 20, lines, false);
}

TDialog *createOtherColorsDialog() {
	std::vector<std::string> lines;
	lines.push_back("statusline");
	lines.push_back("statusline-Bold");
	lines.push_back("Fkey-Labels");
	lines.push_back("fkey-Numbers");
	lines.push_back("Error");
	lines.push_back("Message");
	lines.push_back("Working");
	lines.push_back("shadow");
	lines.push_back("shadow-Character");
	lines.push_back("Background color");
	lines.push_back("");
	lines.push_back("Message  WORKING  L:333 C:10");
	lines.push_back("ERROR BOX");
	lines.push_back("1Help 2Save 3Load 4Indent");
	return createSimplePreviewDialog("OTHER COLORS", 36, 19, lines, false);
}

TMenuItem *createOrganizeMenuItem() {
	return new TMenuItem("or~G~anize", kbNoKey,
	                     new TMenu(*new TMenuItem("Placeholder", cmMrWindowOrganizePlaceholder,
	                                              kbNoKey, hcNoContext)),
	                     hcNoContext);
}

TMenuItem *createUpperCaseItem() {
	return new TMenuItem("upper ~C~ase", cmMrTextUpperCaseMenu, kbNoKey, hcNoContext);
}

TMenuItem *createLowerCaseItem() {
	return new TMenuItem("lower c~a~se", cmMrTextLowerCaseMenu, kbNoKey, hcNoContext);
}

TSubMenu *createFileMenu() {
	return &(*new TSubMenu("~F~ile", kbAltF) +
	         *new TMenuItem("~O~pen...", cmMrFileOpen, kbF3, hcNoContext, "F3") +
	         *new TMenuItem("~L~oad...", cmMrFileLoad, kbNoKey, hcNoContext) +
	         *new TMenuItem("~S~ave", cmMrFileSave, kbF2, hcNoContext, "F2") +
	         *new TMenuItem("save file ~A~s...", cmMrFileSaveAs, kbCtrlF2, hcNoContext, "CtrlF2") +
	         *new TMenuItem("~I~nformation...", cmMrFileInformation, kbNoKey, hcNoContext) +
	         newLine() + *new TMenuItem("~M~erge...", cmMrFileMerge, kbNoKey, hcNoContext) +
	         *new TMenuItem("~P~rint...", cmMrFilePrint, kbNoKey, hcNoContext) + newLine() +
	         *new TMenuItem("~D~OS shell", cmMrFileShellToDos, kbAltF9, hcNoContext, "AltF9") +
	         newLine() + *new TMenuItem("E~x~it", cmQuit, kbAltX, hcNoContext, "Alt-X"));
}

TSubMenu *createEditMenu() {
	return &(
	    *new TSubMenu("~E~dit", kbAltE) +
	    *new TMenuItem("~U~ndo", cmMrEditUndo, kbCtrlU, hcNoContext, "CtrlU") + newLine() +
	    *new TMenuItem("~C~ut to buffer", cmMrEditCutToBuffer, kbCtrlIns, hcNoContext, "CtrlIns") +
	    *new TMenuItem("co~P~y to buffer", cmMrEditCopyToBuffer, kbNoKey, hcNoContext,
	                   "CtrlGrey+") +
	    *new TMenuItem("~A~ppend to buffer", cmMrEditAppendToBuffer, kbCtrlDel, hcNoContext,
	                   "CtrlDel") +
	    *new TMenuItem("cut and ap~P~end to buffer", cmMrEditCutAndAppendToBuffer, kbNoKey,
	                   hcNoContext, "CtrlGrey-") +
	    *new TMenuItem("~P~aste from buffer", cmMrEditPasteFromBuffer, kbShiftIns, hcNoContext,
	                   "ShiftIns") +
	    newLine() +
	    *new TMenuItem("re~P~eat command", cmMrEditRepeatCommand, kbCtrlR, hcNoContext, "CtrlR"));
}

TSubMenu *createWindowMenu() {
	return &(
	    *new TSubMenu("~W~indow", kbAltW) +
	    *new TMenuItem("~O~pen...", cmMrWindowOpen, kbNoKey, hcNoContext) +
	    *new TMenuItem("~C~lose", cmMrWindowClose, kbNoKey, hcNoContext) +
	    *new TMenuItem("~S~plit...", cmMrWindowSplit, kbNoKey, hcNoContext) +
	    *new TMenuItem("~L~ist...", cmMrWindowList, kbCtrlF6, hcNoContext, "CtrlF6") + newLine() +
	    *new TMenuItem("~N~ext", cmMrWindowNext, kbF6, hcNoContext, "F6") +
	    *new TMenuItem("~P~revious", cmMrWindowPrevious, kbShiftF6, hcNoContext, "ShiftF6") +
	    *new TMenuItem("~A~djacent...", cmMrWindowAdjacent, kbNoKey, hcNoContext) + newLine() +
	    *new TMenuItem("~H~ide", cmMrWindowHide, kbNoKey, hcNoContext) +
	    *new TMenuItem("~M~odify size", cmMrWindowModifySize, kbNoKey, hcNoContext, "ScrollLock") +
	    *new TMenuItem("~Z~oom", cmMrWindowZoom, kbAltF6, hcNoContext, "AltF6") +
	    *new TMenuItem("m~I~nimize", cmMrWindowMinimize, kbNoKey, hcNoContext) +
	    *createOrganizeMenuItem() + newLine() +
	    *new TMenuItem("lin~K~...", cmMrWindowLink, kbNoKey, hcNoContext) +
	    *new TMenuItem("~U~nlink", cmMrWindowUnlink, kbNoKey, hcNoContext));
}

TSubMenu *createBlockMenu() {
	return &(
	    *new TSubMenu("~B~lock", kbAltB) +
	    *new TMenuItem("~C~opy block", cmMrBlockCopy, kbF8, hcNoContext, "F8") +
	    *new TMenuItem("~M~ove block", cmMrBlockMove, kbShiftF8, hcNoContext, "ShiftF8") +
	    *new TMenuItem("~D~elete block", cmMrBlockDelete, kbCtrlF8, hcNoContext, "CtrlF8") +
	    newLine() +
	    *new TMenuItem("save ~B~lock to disk", cmMrBlockSaveToDisk, kbShiftF2, hcNoContext,
	                   "ShiftF2") +
	    *new TMenuItem("~I~ndent block", cmMrBlockIndent, kbAltF3, hcNoContext, "AltF3") +
	    *new TMenuItem("~U~ndent block", cmMrBlockUndent, kbAltF2, hcNoContext, "AltF2") +
	    newLine() +
	    *new TMenuItem("~W~indow copy...", cmMrBlockWindowCopy, kbAltF8, hcNoContext, "AltF8") +
	    *new TMenuItem("w~i~ndow move...", cmMrBlockWindowMove, kbAltF7, hcNoContext, "AltF7") +
	    newLine() +
	    *new TMenuItem("mark ~L~ines of text", cmMrBlockMarkLines, kbF7, hcNoContext, "F7") +
	    *new TMenuItem("mark c~O~lumns of text", cmMrBlockMarkColumns, kbShiftF7, hcNoContext,
	                   "ShiftF7") +
	    *new TMenuItem("mark ~S~tream of text", cmMrBlockMarkStream, kbCtrlF7, hcNoContext,
	                   "CtrlF7") +
	    *new TMenuItem("~E~nd marking", cmMrBlockEndMarking, kbF7, hcNoContext, "F7") +
	    *new TMenuItem("~t~urn marking off", cmMrBlockTurnMarkingOff, kbNoKey, hcNoContext) +
	    newLine() +
	    *new TMenuItem("~P~ersistent blocks", cmMrBlockPersistent, kbNoKey, hcNoContext));
}

TSubMenu *createSearchMenu() {
	return &(*new TSubMenu("~S~earch", kbAltS) +
	         *new TMenuItem("Search for ~t~ext...", cmMrSearchFindText, kbF5, hcNoContext, "F5") +
	         *new TMenuItem("search and ~R~eplace...", cmMrSearchReplace, kbShiftF5, hcNoContext,
	                        "ShiftF5") +
	         *new TMenuItem("repeat ~P~revious search", cmMrSearchRepeatPrevious, kbCtrlF5,
	                        hcNoContext, "CtrlF5") +
	         newLine() +
	         *new TMenuItem("p~U~sh position onto marker stack", cmMrSearchPushMarker, kbF4,
	                        hcNoContext, "F4") +
	         *new TMenuItem("~G~et position from marker stack", cmMrSearchGetMarker, kbShiftF4,
	                        hcNoContext, "ShiftF4") +
	         newLine() +
	         *new TMenuItem("set random ~A~ccess mark...", cmMrSearchSetRandomAccessMark, kbNoKey,
	                        hcNoContext) +
	         *new TMenuItem("re~T~rieve random access mark...", cmMrSearchRetrieveRandomAccessMark,
	                        kbNoKey, hcNoContext) +
	         newLine() +
	         *new TMenuItem("goto line ~N~umber...", cmMrSearchGotoLineNumber, kbAltF5, hcNoContext,
	                        "AltF5"));
}

TSubMenu *createTextMenu() {
	return &(*new TSubMenu("~T~ext", kbAltT) +
	         *new TMenuItem("~L~ayout...", cmMrTextLayout, kbCtrlF3, hcNoContext, "CtrlF3") +
	         newLine() + *createUpperCaseItem() + *createLowerCaseItem() +
	         *new TMenuItem("cen~T~er line", cmMrTextCenterLine, kbNoKey, hcNoContext) +
	         *new TMenuItem("time/~D~ate stamp", cmMrTextTimeDateStamp, kbNoKey, hcNoContext) +
	         newLine() +
	         *new TMenuItem("re-~F~ormat paragraph", cmMrTextReformatParagraph, kbAltR, hcNoContext,
	                        "AltR"));
}

TSubMenu *createOtherMenu() {
	return &(
	    *new TSubMenu("~O~ther", kbAltO) +
	    *new TMenuItem("~I~nstallation and setup", cmMrOtherInstallationAndSetup, kbNoKey,
	                   hcNoContext) +
	    newLine() +
	    *new TMenuItem("~K~eystroke macros...", cmMrOtherKeystrokeMacros, kbNoKey, hcNoContext) +
	    newLine() +
	    *new TMenuItem("~E~xecute program...", cmMrOtherExecuteProgram, kbF9, hcNoContext, "F9") +
	    *new TMenuItem("find ne~X~t compiler error", cmMrOtherFindNextCompilerError, kbShiftF9,
	                   hcNoContext, "ShiftF9") +
	    *new TMenuItem("~M~atch brace or paren", cmMrOtherMatchBraceOrParen, kbCtrlF5, hcNoContext,
	                   "CtrlF5") +
	    newLine() +
	    *new TMenuItem("~A~scii table", cmMrOtherAsciiTable, kbAltA, hcNoContext, "AltA"));
}

TSubMenu *createHelpMenu() {
	return &(
	    *new TSubMenu("~H~elp", kbAltH) +
	    *new TMenuItem("~T~able of contents", cmMrHelpContents, kbF1, hcNoContext, "F1") +
	    *new TMenuItem("~K~eys", cmMrHelpKeys, kbF1, hcNoContext, "F1") +
	    *new TMenuItem("detailed ~I~ndex", cmMrHelpDetailedIndex, kbShiftF1, hcNoContext,
	                   "ShiftF1") +
	    *new TMenuItem("~P~revious topic", cmMrHelpPreviousTopic, kbAltF1, hcNoContext, "AltF1") +
	    *new TMenuItem("~A~bout...", cmMrHelpAbout, kbNoKey, hcNoContext));
}

TSubMenu *createDevMenu() {
	return &(*new TSubMenu("De~V~", kbAltV) +
	         *new TMenuItem("~R~un macro file...", cmMrDevRunMacro, kbCtrlT, hcNoContext, "CtrlT"));
}
} // namespace

class TMREditorApp : public TApplication {
  public:
	static TMenuBar *initMRMenuBar(TRect r) {
		r.b.y = r.a.y + 1;

		return new TMRMenuBar(r, *createFileMenu() + *createEditMenu() + *createWindowMenu() +
		                             *createBlockMenu() + *createSearchMenu() + *createTextMenu() +
		                             *createOtherMenu() + *createHelpMenu() + *createDevMenu());
	}

	static TStatusLine *initMRStatusLine(TRect r) {
		r.a.y = r.b.y - 1;
		return new TMRStatusLine(r, *new TStatusDef(0, 0xFFFF) +
		                                *new TStatusItem("~F1~ Help", kbF1, cmMrHelpContents) +
		                                *new TStatusItem("~F10~ Menu", kbF10, cmMenu) +
		                                *new TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit));
	}

	static TDeskTop *initMRDeskTop(TRect r) {
		r.a.y++;
		r.b.y--;
		return new TMRDeskTop(r);
	}

	TMREditorApp()
	    : TProgInit(&TMREditorApp::initMRStatusLine, &TMREditorApp::initMRMenuBar,
	                &TMREditorApp::initMRDeskTop) {
		createEditorWindow("?No-File?");
		mrEnsureLogWindow(false);
		mrLogMessage("Editor session started.");
		updateAppCommandState();
	}

	virtual void handleEvent(TEvent &event) override {
		TApplication::handleEvent(event);

		if (event.what != evCommand)
			return;

		if (handleMRCommand(event.message.command))
			clearEvent(event);
	}


	virtual void idle() override {
		TApplication::idle();
		updateAppCommandState();
	}

	virtual TPalette &getPalette() const override {
		static TPalette palette(cpAppColor, sizeof(cpAppColor) - 1);
		return palette;
	}

  private:

	TMREditWindow *createEditorWindow(const char *title) {
		TRect r = deskTop->getExtent();
		TMREditWindow *win;
		r.grow(-2, -1);
		win = new TMREditWindow(r, title, 1);
		deskTop->insert(win);
		return win;
	}

	bool handleFileOpen() {
		enum { FileNameBufferSize = MAXPATH };
		char fileName[FileNameBufferSize];
		TMREditWindow *target;
		TMREditWindow *current = currentEditWindow();
		std::string logLine;

		if (!promptForPath("Open File", fileName, sizeof(fileName)))
			return true;

		target = findReusableEmptyWindow(current);
		if (target == 0)
			target = createEditorWindow("?No-File?");
		if (!loadFileIntoWindow(target, fileName)) {
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
		std::string logLine;

		if (!promptForPath("Load File", fileName, sizeof(fileName)))
			return true;
		if (target == 0)
			target = createEditorWindow("?No-File?");
		else if (!target->confirmAbandonForReload())
			return true;
		if (!loadFileIntoWindow(target, fileName))
			return true;
		mrActivateEditWindow(target);
		logLine = "Loaded file into active window: ";
		logLine += target->currentFileName();
		if (target->isReadOnly())
			logLine += " [read-only]";
		mrLogMessage(logLine.c_str());
		return true;
	}

	void runInstallationAndSetup() {
		bool running = true;

		while (running) {
			ushort result = execDialog(createInstallationAndSetupDialog());
			switch (result) {
				case cmMrSetupEditSettings:
					execDialog(createEditSettingsDialog());
					break;

				case cmMrSetupDisplaySetup:
					execDialog(createDisplaySetupDialog());
					break;

				case cmMrSetupColorSetup:
					runColorSetup();
					break;

				case cmMrSetupSearchAndReplaceDefaults:
					showDummyCommandBox("Installation / Search and Replace defaults");
					break;

				case cmMrSetupSaveConfigurationAndExit:
					showDummyCommandBox("Installation / Save configuration and exit");
					running = false;
					break;

				case cmCancel:
				default:
					running = false;
					break;
			}
		}
	}

	void runColorSetup() {
		bool running = true;

		while (running) {
			ushort result = execDialog(createColorSetupDialog());
			switch (result) {
				case cmMrColorWindowColors:
					execDialog(createWindowColorsDialog());
					break;

				case cmMrColorMenuDialogColors:
					execDialog(createMenuDialogColorsDialog());
					break;

				case cmMrColorHelpColors:
					execDialog(createHelpColorsDialog());
					break;

				case cmMrColorOtherColors:
					execDialog(createOtherColorsDialog());
					break;

				case cmCancel:
				default:
					running = false;
					break;
			}
		}
	}

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
				messageBox(mfInformation | mfOKButton, "Save As is not available yet.");
				mrLogMessage("Save As requested but not implemented yet.");
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
				runInstallationAndSetup();
				return true;

			case cmMrDevRunMacro:
				runMacroFileDialog();
				return true;

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
};

int main(int argc, char **argv) {
	mrvmSetProcessContext(argc, argv);
	loadDefaultMultiEditPalette();
	TMREditorApp app;
	app.run();
	return 0;
}
