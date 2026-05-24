#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TObject
#define Uses_TEvent
#define Uses_TRect
#define Uses_TView
#define Uses_TButton
#define Uses_TFileDialog
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TListViewer
#define Uses_TDrawBuffer
#define Uses_MsgBox
#define Uses_TCheckBoxes
#define Uses_TRadioButtons
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TClipboard
#define Uses_THardwareInfo
#define Uses_TSItem
#include <tvision/tv.h>

#include "MRCommandRouter.hpp"
#include "router/MRCommandRouterSearch.hpp"
#include "export/MRPdfTextExporter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fnmatch.h>
#include <functional>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../dialogs/MRFileInformation.hpp"
#include "../dialogs/MRAcquireDialog.hpp"
#include "../dialogs/MRAbout.hpp"
#include "../dialogs/MRPdfExportDialog.hpp"
#include "../dialogs/setup/MRSetup.hpp"
#include "../dialogs/setup/MRSetupCommon.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#include "../app/utils/MRStringUtils.hpp"
#include "../keymap/MRKeymapActionCatalog.hpp"
#include "../keymap/MRKeymapSequence.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../mrmac/MRVM.hpp"
#include "../app/commands/MRExternalCommand.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../app/commands/MRLogViewer.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../dialogs/MRMacroFile.hpp"
#include "../dialogs/MRWindowList.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/MRMenuBar.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "MREditorApp.hpp"
#include "MRCommands.hpp"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

namespace {
bool startExternalCommandInWindow(MREditWindow *win, const std::string &commandLine, bool replaceBuffer, bool activate, bool closeOnFailure);

TFrame *initMrDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

struct CharacterTableEntry {
	std::string text;
	std::string label;
	std::string detail;
};

enum class CharacterTableKind : unsigned char {
	Ascii = 0,
	Emoji = 1
};

struct CharacterTableLayout {
	const char *title;
	int width;
	int height;
	int columns;
	int cellWidth;
};

struct ClipboardFetchState {
	bool received = false;
	std::string text;
};

ClipboardFetchState g_clipboardFetchState;

enum class KeymapDispatchKind : unsigned char {
	AppCommand = 0,
	EditorCommand = 1,
	WindowMethod = 2,
	Custom = 3
};

enum class KeymapWindowMethod : unsigned char {
	None = 0,
	BlockSetBegin = 1,
	BlockSetEnd = 2,
	BlockSetColumnBegin = 3,
	BlockClear = 4,
	BlockMarkWordRight = 5,
	CursorTopOfWindow = 6,
	CursorBottomOfWindow = 7,
	CursorStartOfBlock = 8,
	CursorEndOfBlock = 9,
	ViewCenterLine = 10
};

enum class KeymapCustomAction : unsigned char {
	None = 0,
	DeleteForwardCharOrBlock = 1,
	LoadBlockFromFile = 2,
	SetRandomAccessMark = 3,
	GetRandomAccessMark = 4,
	CenterLine = 5,
	ReformatParagraph = 6,
	ReformatDocument = 7,
	ToggleFormatRuler = 8,
	ToggleWordWrap = 9,
	SetLeftMargin = 10,
	SetRightMargin = 11,
	JustifyParagraph = 12,
	SortColumnBlockToggle = 13,
	ForceSave = 14,
	ExitDirtySaveAll = 15,
	MoveCursorToNextPageBreak = 16,
	MoveCursorToPrevPageBreak = 17,
	ScrollWindowUp = 18,
	ScrollWindowDown = 19,
	CursorIndent = 20,
	CursorTabRight = 21,
	CursorTabLeft = 22,
	CursorUndent = 23,
	CopyMarkedBlockToSystemClipboard = 24,
	BlockMath = 25,
	ExtendBlockByMotion = 26,
	SearchResultsNext = 27
};

struct KeymapActionDispatchEntry {
	std::string_view actionId;
	KeymapDispatchKind kind;
	ushort command;
	KeymapWindowMethod windowMethod;
	KeymapCustomAction customAction;
};

constexpr std::array kKeymapActionDispatchTable{
    KeymapActionDispatchEntry{"MRMAC_CURSOR_LEFT", KeymapDispatchKind::EditorCommand, cmCharLeft, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_RIGHT", KeymapDispatchKind::EditorCommand, cmCharRight, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_UP", KeymapDispatchKind::EditorCommand, cmLineUp, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_DOWN", KeymapDispatchKind::EditorCommand, cmLineDown, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_HOME", KeymapDispatchKind::EditorCommand, cmLineStart, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_END_OF_LINE", KeymapDispatchKind::EditorCommand, cmLineEnd, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_VIEW_PAGE_UP", KeymapDispatchKind::EditorCommand, cmPageUp, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_VIEW_PAGE_DOWN", KeymapDispatchKind::EditorCommand, cmPageDown, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_TOP_OF_FILE", KeymapDispatchKind::EditorCommand, cmTextStart, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_BOTTOM_OF_FILE", KeymapDispatchKind::EditorCommand, cmTextEnd, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_NEXT_PAGE_BREAK", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::MoveCursorToNextPageBreak},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_PREV_PAGE_BREAK", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::MoveCursorToPrevPageBreak},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_WORD_LEFT", KeymapDispatchKind::EditorCommand, cmWordLeft, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_WORD_RIGHT", KeymapDispatchKind::EditorCommand, cmWordRight, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_TOP_OF_WINDOW", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::CursorTopOfWindow, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_BOTTOM_OF_WINDOW", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::CursorBottomOfWindow, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_VIEW_SCROLL_UP", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ScrollWindowUp},
    KeymapActionDispatchEntry{"MRMAC_VIEW_SCROLL_DOWN", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ScrollWindowDown},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_START_OF_BLOCK", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::CursorStartOfBlock, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_END_OF_BLOCK", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::CursorEndOfBlock, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_GOTO_LINE", KeymapDispatchKind::AppCommand, cmMrSearchGotoLineNumber, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_INDENT", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::CursorIndent},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_TAB_RIGHT", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::CursorTabRight},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_TAB_LEFT", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::CursorTabLeft},
    KeymapActionDispatchEntry{"MRMAC_CURSOR_UNDENT", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::CursorUndent},
    KeymapActionDispatchEntry{"MRMAC_MARK_PUSH_POSITION", KeymapDispatchKind::AppCommand, cmMrSearchPushMarker, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_MARK_POP_POSITION", KeymapDispatchKind::AppCommand, cmMrSearchGetMarker, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_MARK_SET_RANDOM_ACCESS", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SetRandomAccessMark},
    KeymapActionDispatchEntry{"MRMAC_MARK_GET_RANDOM_ACCESS", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::GetRandomAccessMark},
    KeymapActionDispatchEntry{"MRMAC_VIEW_CENTER_LINE", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::ViewCenterLine, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_DELETE_TO_EOL", KeymapDispatchKind::EditorCommand, cmDelEnd, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_DELETE_FORWARD_CHAR_OR_BLOCK", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::DeleteForwardCharOrBlock},
    KeymapActionDispatchEntry{"MRMAC_DELETE_FORWARD_WORD", KeymapDispatchKind::EditorCommand, cmDelWord, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_DELETE_BACKWARD_CHAR", KeymapDispatchKind::EditorCommand, cmBackSpace, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_DELETE_BACKWARD_WORD", KeymapDispatchKind::EditorCommand, cmDelWordLeft, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_DELETE_LINE", KeymapDispatchKind::EditorCommand, cmDelLine, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_DELETE_BACKWARD_TO_HOME", KeymapDispatchKind::EditorCommand, cmDelStart, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_UNDO", KeymapDispatchKind::AppCommand, cmMrEditUndo, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_REDO_LAST_UNDO", KeymapDispatchKind::AppCommand, cmMrEditRedo, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_SEARCH_FORWARD", KeymapDispatchKind::AppCommand, cmMrSearchFindText, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_SEARCH_REPLACE", KeymapDispatchKind::AppCommand, cmMrSearchReplace, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_SEARCH_REPEAT_LAST", KeymapDispatchKind::AppCommand, cmMrSearchRepeatPrevious, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_SEARCH_MULTI_FILE", KeymapDispatchKind::AppCommand, cmMrSearchMultiFileSearch, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_SEARCH_LIST_MATCHED_FILES", KeymapDispatchKind::AppCommand, cmMrSearchListFilesFromLastSearch, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_COPY_TO_CLIPBOARD", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::CopyMarkedBlockToSystemClipboard},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_PASTE_FROM_CLIPBOARD", KeymapDispatchKind::AppCommand, cmMrEditPasteFromBuffer, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_MARK_STREAM", KeymapDispatchKind::AppCommand, cmMrBlockMarkStream, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_SET_BEGIN", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::BlockSetBegin, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_SET_END", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::BlockSetEnd, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_SET_COLUMN_BEGIN", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::BlockSetColumnBegin, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_MARK_WORD_RIGHT", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::BlockMarkWordRight, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_CLEAR", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::BlockClear, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_UNDENT", KeymapDispatchKind::AppCommand, cmMrBlockUndent, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_INDENT", KeymapDispatchKind::AppCommand, cmMrBlockIndent, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_COPY", KeymapDispatchKind::AppCommand, cmMrBlockCopy, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_MOVE", KeymapDispatchKind::AppCommand, cmMrBlockMove, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_DELETE", KeymapDispatchKind::AppCommand, cmMrBlockDelete, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_COPY_INTERWINDOW", KeymapDispatchKind::AppCommand, cmMrBlockWindowCopy, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_MOVE_INTERWINDOW", KeymapDispatchKind::AppCommand, cmMrBlockWindowMove, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_MOVE_TO_BUFFER", KeymapDispatchKind::AppCommand, cmMrEditCutToBuffer, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_APPEND_TO_BUFFER", KeymapDispatchKind::AppCommand, cmMrEditAppendToBuffer, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_CUT_APPEND_TO_BUFFER", KeymapDispatchKind::AppCommand, cmMrEditCutAndAppendToBuffer, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_COPY_FROM_BUFFER", KeymapDispatchKind::AppCommand, cmMrEditPasteFromBuffer, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_MATH", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::BlockMath},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_EXTEND_BY_MOTION", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ExtendBlockByMotion},
    KeymapActionDispatchEntry{"MRMAC_FILE_SAVE", KeymapDispatchKind::AppCommand, cmMrFileSave, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_FILE_SAVE_ALL", KeymapDispatchKind::AppCommand, cmMrFileSaveAll, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_FILE_REVERT", KeymapDispatchKind::AppCommand, cmMrFileRevert, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SAVE_BLOCK_TO_FILE", KeymapDispatchKind::AppCommand, cmMrBlockSaveToDisk, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_LOAD_BLOCK_FROM_FILE", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::LoadBlockFromFile},
    KeymapActionDispatchEntry{"MR_TEXT_CENTER_LINE", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::CenterLine},
    KeymapActionDispatchEntry{"MR_TEXT_REFORMAT_PARAGRAPH", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ReformatParagraph},
    KeymapActionDispatchEntry{"MR_TEXT_REFORMAT_DOCUMENT", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ReformatDocument},
    KeymapActionDispatchEntry{"MR_TOGGLE_FORMAT_RULER", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ToggleFormatRuler},
    KeymapActionDispatchEntry{"MR_TOGGLE_WORD_WRAP", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ToggleWordWrap},
    KeymapActionDispatchEntry{"MR_SET_LEFT_MARGIN", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SetLeftMargin},
    KeymapActionDispatchEntry{"MR_SET_RIGHT_MARGIN", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SetRightMargin},
    KeymapActionDispatchEntry{"MR_JUSTIFY_PARAGRAPH", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::JustifyParagraph},
    KeymapActionDispatchEntry{"MR_SORT_COLUMN_BLOCK_TOGGLE", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SortColumnBlockToggle},
    KeymapActionDispatchEntry{"MR_FILE_FORCE_SAVE", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ForceSave},
    KeymapActionDispatchEntry{"MR_EXIT_DIRTY_SAVE_ALL", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ExitDirtySaveAll},
    KeymapActionDispatchEntry{"MR_SEARCH_RESULTS_NEXT", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SearchResultsNext},
};

const char *placeholderCommandTitle(ushort command) {
	switch (command) {
		case cmMrFileOpen:
			return "File / Open";
		case cmMrFileLoad:
			return "File / Load";
		case cmMrFileAcquire:
			return "File / Acquire";
		case cmMrFileOpenLiveLog:
			return "File / Open Live Log";
		case cmMrFileOpenJournal:
			return "File / Open Journal";
		case cmMrFileSave:
			return "File / Save";
		case cmMrFileSaveAs:
			return "File / Save As";
		case cmMrFileSaveAll:
			return "File / Save All";
		case cmMrFileRevert:
			return "File / Revert";
		case cmMrFileInformation:
			return "File / Information";
		case cmMrFileMerge:
			return "File / Merge";
		case cmMrFilePrint:
			return "File / Export to PDF";
		case cmMrFileShellToDos:
			return "File / Shell";
		case cmMrSetupLiveLogs:
			return "Setup / Live logs";

		case cmMrEditUndo:
			return "Edit / Undo";
		case cmMrEditRedo:
			return "Edit / Redo";
		case cmMrEditCutToBuffer:
			return "Edit / Cut";
		case cmMrEditCopyToBuffer:
			return "Edit / Copy";
		case cmMrEditAppendToBuffer:
			return "Edit / Append";
		case cmMrEditCutAndAppendToBuffer:
			return "Edit / Cut & append";
		case cmMrEditPasteFromBuffer:
			return "Edit / Paste";
		case cmMrEditRepeatCommand:
			return "Edit / Repeat";

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
		case cmMrSearchMultiFileSearch:
			return "Search / Multiple file search";
		case cmMrSearchListFilesFromLastSearch:
			return "Search / List files from last search";
		case cmMrSearchMultiFileSearchReplace:
			return "Search / Multiple file search and replace";
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
		case cmMrOtherBuildCurrentFile:
			return "Other / Build current file";
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

		case cmMrHelpContents:
			return "Help / Table of contents";
		case cmMrHelpKeys:
			return "Help / Keys";
		case cmMrHelpDetailedIndex:
			return "Help / Detailed index";
		case cmMrHelpPreviousTopic:
			return "Help / Previous topic";
		case cmMrHelpPerformancePanel:
			return "Help / Performance panel";
		case cmMrHelpAbout:
			return "Help / About";

		case cmMrSetupKeyMapping:
			return "Installation / Key mapping";
		case cmMrSetupMouseKeyRepeat:
			return "Installation / Mouse / Key repeat setup";
		case cmMrSetupFilenameExtensions:
			return "Installation / Filename extensions";
		case cmMrSetupCompilerProfiles:
			return "Installation / Compiler profiles";
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

std::optional<int> randomAccessMarkIndexFromSequence(std::string_view sequenceText) {
	const auto sequence = MRKeymapSequence::parse(sequenceText);

	if (!sequence || sequence->empty()) return std::nullopt;
	const MRKeymapToken &token = sequence->tokens().back();
	if (token.baseKey() != MRKeymapBaseKey::Printable || token.modifiers() != 0) return std::nullopt;
	if (token.printableKey() < '1' || token.printableKey() > '9') return std::nullopt;
	return token.printableKey() - '0';
}

MREditWindow *effectiveKeymapWindow(MREditWindow *targetWindow) {
	return targetWindow != nullptr ? targetWindow : currentEditWindow();
}

bool dispatchEditorCommandEvent(MREditWindow *targetWindow, ushort command) {
	MREditWindow *window = effectiveKeymapWindow(targetWindow);
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	TEvent event;

	if (editor == nullptr) return false;
	std::memset(&event, 0, sizeof(event));
	event.what = evCommand;
	event.message.command = command;
	editor->handleEvent(event);
	return true;
}

bool dispatchApplicationCommandEvent(ushort command) {
	TApplication *application = dynamic_cast<TApplication *>(TProgram::application);
	TEvent event;

	if (application == nullptr) return false;
	std::memset(&event, 0, sizeof(event));
	event.what = evCommand;
	event.message.command = command;
	application->handleEvent(event);
	return true;
}

bool dispatchKeymapWindowMethod(MREditWindow *targetWindow, KeymapWindowMethod method) {
	MREditWindow *window = effectiveKeymapWindow(targetWindow);

	if (window == nullptr) return false;
	switch (method) {
		case KeymapWindowMethod::BlockSetBegin:
			window->beginStreamBlock();
			return true;
		case KeymapWindowMethod::BlockSetEnd:
			window->endBlock();
			return true;
		case KeymapWindowMethod::BlockSetColumnBegin:
			window->beginColumnBlock();
			return true;
		case KeymapWindowMethod::BlockClear:
			return window->toggleBlockVisibility();
		case KeymapWindowMethod::BlockMarkWordRight:
			return window->markWordRight();
		case KeymapWindowMethod::CursorTopOfWindow:
			return window->moveCursorToTopOfView();
		case KeymapWindowMethod::CursorBottomOfWindow:
			return window->moveCursorToBottomOfView();
		case KeymapWindowMethod::CursorStartOfBlock:
			return window->moveCursorToBlockStart();
		case KeymapWindowMethod::CursorEndOfBlock:
			return window->moveCursorToBlockEnd();
		case KeymapWindowMethod::ViewCenterLine:
			return window->centerCursorInView();
		case KeymapWindowMethod::None:
			break;
	}
	return false;
}

void showPlaceholderCommandBox(const char *title) {
	if (title == nullptr) title = "Command";
	messageBox(mfInformation | mfOKButton, "%s\n\nPlaceholder implementation for now.", title);
}

std::string utf8FromCodepoint(std::uint32_t codepoint);
bool handleExportToPdf();

std::vector<CharacterTableEntry> buildAsciiTableEntries() {
	std::vector<CharacterTableEntry> entries;
	entries.reserve(224);
	for (int code = 0; code <= 255; ++code) {
		if (code >= 128 && code < 160) continue;
		CharacterTableEntry entry;
		entry.text = std::string(1, static_cast<char>(code));
		std::ostringstream detail;
		detail << "ASCII " << std::dec << std::setw(3) << std::setfill('0') << code << " 0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << code << " ";
		if (code < 32) {
			entry.label = std::string("^") + static_cast<char>('@' + code);
			detail << entry.label;
		} else if (code == 32) {
			entry.label = "SP";
			detail << "SPACE";
		} else if (code == 127) {
			entry.label = "^?";
			detail << "DELETE";
		} else if (code >= 160) {
			entry.label = utf8FromCodepoint(static_cast<std::uint32_t>(code));
			entry.text = entry.label;
			if (code == 160) detail << "NO-BREAK SPACE";
			else
				detail << "LATIN-1 " << entry.label;
		} else {
			entry.label = entry.text;
			detail << entry.text;
		}
		entry.detail = detail.str();
		entries.push_back(std::move(entry));
	}
	return entries;
}

std::string utf8FromCodepoint(std::uint32_t codepoint) {
	std::string text;
	if (codepoint <= 0x7F) {
		text.push_back(static_cast<char>(codepoint));
	} else if (codepoint <= 0x7FF) {
		text.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
		text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
	} else if (codepoint <= 0xFFFF) {
		text.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
		text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
		text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
	} else {
		text.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
		text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
		text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
		text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
	}
	return text;
}

std::vector<CharacterTableEntry> buildEmojiTableEntries() {
	struct Range {
		std::uint32_t first;
		std::uint32_t last;
	};
	static constexpr Range kEmojiRanges[] = {{0x2300, 0x23FF}, {0x2600, 0x27BF}, {0x1F1E6, 0x1F1FF}, {0x1F300, 0x1F5FF}, {0x1F600, 0x1F64F}, {0x1F680, 0x1F6FF}, {0x1F780, 0x1F7FF}, {0x1F900, 0x1F9FF}, {0x1FA70, 0x1FAFF}};
	std::vector<CharacterTableEntry> entries;
	for (const Range &range : kEmojiRanges)
		for (std::uint32_t codepoint = range.first; codepoint <= range.last; ++codepoint) {
			CharacterTableEntry entry;
			std::ostringstream detail;
			entry.text = utf8FromCodepoint(codepoint);
			entry.label = entry.text;
			detail << "U+" << std::hex << std::uppercase << std::setw(codepoint > 0xFFFF ? 5 : 4) << std::setfill('0') << codepoint;
			entry.detail = detail.str();
			entries.push_back(std::move(entry));
		}
	return entries;
}

CharacterTableLayout layoutForCharacterTable(CharacterTableKind kind) {
	switch (kind) {
		case CharacterTableKind::Ascii:
			return {"ASCII TABLE", 67, 18, 15, 4};
		case CharacterTableKind::Emoji:
			return {"EMOJI TABLE", 67, 18, 15, 4};
	}
	return {"CHARACTER TABLE", 67, 18, 15, 4};
}

class CharacterTableView final : public TView {
  public:
	CharacterTableView(const TRect &bounds, std::vector<CharacterTableEntry> entries, int columns, int cellWidth, TScrollBar *scrollBar) : TView(bounds), entries(std::move(entries)), columns(columns), cellWidth(cellWidth), scrollBar(scrollBar) {
		options |= ofSelectable;
		eventMask |= evMouseDown | evMouseWheel | evKeyDown | evBroadcast;
	}

	void draw() override {
		TDrawBuffer row;
		const TColorAttr normal = getColor(1);
		const TColorAttr selected = getColor(3);
		const short blankRow = size.y > 1 ? size.y - 2 : 0;
		const short detailRow = size.y > 0 ? size.y - 1 : 0;
		const int rowOffset = scrollOffset;
		updateScrollBar();
		for (short y = 0; y < size.y; ++y) {
			row.moveChar(0, ' ', normal, size.x);
			writeLine(0, y, size.x, 1, row);
		}
		for (std::size_t index = 0; index < entries.size(); ++index) {
			const int rowIndex = static_cast<int>(index) / columns - rowOffset;
			const int colIndex = static_cast<int>(index) % columns;
			const short x = static_cast<short>(gridLeftOffset() + colIndex * cellWidth);
			const short y = static_cast<short>(rowIndex);
			if (y < 0 || y >= blankRow || x >= size.x) continue;
			drawCell(index, x, y, index == selectedIndex ? selected : normal);
		}
		drawDetail(detailRow, normal);
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evMouseDown && containsMouse(event)) {
			TPoint local = makeLocal(event.mouse.where);
			const int gridLeft = gridLeftOffset();
			if (local.x < gridLeft || local.x >= gridLeft + gridWidth()) {
				clearEvent(event);
				return;
			}
			const int col = cellWidth > 0 ? (local.x - gridLeft) / cellWidth : 0;
			const int row = local.y + scrollOffset;
			const std::size_t index = static_cast<std::size_t>(row * columns + col);
			if (index < entries.size()) {
				selectedIndex = index;
				ensureSelectedVisible();
				drawView();
				if ((event.mouse.eventFlags & meDoubleClick) != 0 && owner != nullptr) message(owner, evCommand, cmOK, this);
			}
			clearEvent(event);
			return;
		}
		if (event.what == evMouseWheel && containsMouse(event)) {
			const int step = event.mouse.wheel == mwUp || event.mouse.wheel == mwLeft ? -1 : 1;
			setScrollOffset(scrollOffset + step);
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown) {
			if (moveSelection(ctrlToArrow(event.keyDown.keyCode))) {
				clearEvent(event);
				return;
			}
			if (ctrlToArrow(event.keyDown.keyCode) == kbEnter) {
				if (owner != nullptr) message(owner, evCommand, cmOK, this);
				clearEvent(event);
				return;
			}
		}
		if (event.what == evBroadcast && event.message.command == cmScrollBarChanged && event.message.infoPtr == scrollBar) {
			setScrollOffset(scrollBar != nullptr ? scrollBar->value : 0);
			clearEvent(event);
			return;
		}
		TView::handleEvent(event);
	}

	[[nodiscard]] const std::string &selectedText() const noexcept {
		static const std::string empty;
		return selectedIndex < entries.size() ? entries[selectedIndex].text : empty;
	}

  private:
	[[nodiscard]] int gridWidth() const noexcept {
		return std::max(0, columns * cellWidth);
	}

	[[nodiscard]] int gridLeftOffset() const noexcept {
		return std::max(0, (static_cast<int>(size.x) - gridWidth()) / 2);
	}

	void drawCell(std::size_t index, short x, short y, TColorAttr attr) {
		TDrawBuffer cell;
		std::string text = entries[index].label;
		if (static_cast<int>(text.size()) + 1 < cellWidth) text = " " + text;
		cell.moveChar(0, ' ', attr, static_cast<ushort>(cellWidth));
		cell.moveStr(0, text.c_str(), attr, static_cast<ushort>(cellWidth));
		writeLine(x, y, static_cast<short>(std::min(cellWidth, static_cast<int>(size.x - x))), 1, cell);
	}

	void drawDetail(short y, TColorAttr attr) {
		if (entries.empty() || y < 0 || y >= size.y) return;
		TDrawBuffer row;
		std::string text = entries[selectedIndex].label;
		if (!entries[selectedIndex].detail.empty()) {
			if (!text.empty()) text += " ";
			text += entries[selectedIndex].detail;
		}
		const int detailWidth = strwidth(text.c_str());
		const int start = std::max(0, (static_cast<int>(size.x) - detailWidth) / 2);
		row.moveChar(0, ' ', attr, size.x);
		row.moveStr(static_cast<ushort>(start), text.c_str(), attr, static_cast<ushort>(std::max(0, size.x - start)));
		writeLine(0, y, size.x, 1, row);
	}

	bool moveSelection(ushort keyCode) {
		if (entries.empty()) return false;
		std::size_t next = selectedIndex;
		switch (keyCode) {
			case kbLeft:
				next = selectedIndex == 0 ? entries.size() - 1 : selectedIndex - 1;
				break;
			case kbRight:
				next = (selectedIndex + 1) % entries.size();
				break;
			case kbUp:
				next = selectedIndex < static_cast<std::size_t>(columns) ? selectedIndex : selectedIndex - static_cast<std::size_t>(columns);
				break;
			case kbDown:
				next = std::min(entries.size() - 1, selectedIndex + static_cast<std::size_t>(columns));
				break;
			case kbHome:
				next = 0;
				break;
			case kbEnd:
				next = entries.size() - 1;
				break;
			case kbPgUp:
				next = selectedIndex < static_cast<std::size_t>(columns * 4) ? 0 : selectedIndex - static_cast<std::size_t>(columns * 4);
				break;
			case kbPgDn:
				next = std::min(entries.size() - 1, selectedIndex + static_cast<std::size_t>(columns * 4));
				break;
			default:
				return false;
		}
		if (next != selectedIndex) {
			selectedIndex = next;
			ensureSelectedVisible();
			drawView();
		}
		return true;
	}

	[[nodiscard]] int totalRows() const noexcept {
		if (columns <= 0) return 0;
		return static_cast<int>((entries.size() + static_cast<std::size_t>(columns - 1)) / static_cast<std::size_t>(columns));
	}

	[[nodiscard]] int visibleRows() const noexcept {
		return std::max(1, static_cast<int>(size.y) - 2);
	}

	[[nodiscard]] int maxScrollOffset() const noexcept {
		return std::max(0, totalRows() - visibleRows());
	}

	void setScrollOffset(int offset) {
		const int clamped = std::max(0, std::min(offset, maxScrollOffset()));
		if (clamped == scrollOffset) return;
		scrollOffset = clamped;
		updateScrollBar();
		drawView();
	}

	void updateScrollBar() {
		if (scrollBar == nullptr) return;
		scrollBar->setParams(scrollOffset, 0, maxScrollOffset(), visibleRows(), 1);
	}

	void ensureSelectedVisible() {
		if (columns <= 0) return;
		const int row = static_cast<int>(selectedIndex) / columns;
		if (row < scrollOffset) scrollOffset = row;
		else if (row >= scrollOffset + visibleRows())
			scrollOffset = row - visibleRows() + 1;
		scrollOffset = std::max(0, std::min(scrollOffset, maxScrollOffset()));
		updateScrollBar();
	}

	std::vector<CharacterTableEntry> entries;
	int columns = 1;
	int cellWidth = 1;
	int scrollOffset = 0;
	TScrollBar *scrollBar = nullptr;
	std::size_t selectedIndex = 0;
};

class CharacterTableDialog final : public MRDialogFoundation {
  public:
	CharacterTableDialog(const CharacterTableLayout &layout, std::vector<CharacterTableEntry> entries) : TWindowInit(initMrDialogFrame), MRDialogFoundation(mr::dialogs::centeredDialogRect(layout.width, layout.height), layout.title, layout.width, layout.height, initMrDialogFrame) {
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"C~a~ncel", cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 2);
		const int buttonLeft = (layout.width - metrics.rowWidth) / 2;
		scrollBar = new TScrollBar(TRect(layout.width - 2, 2, layout.width - 1, layout.height - 5));
		tableView = new CharacterTableView(TRect(2, 2, layout.width - 2, layout.height - 5), std::move(entries), layout.columns, layout.cellWidth, scrollBar);
		insert(tableView);
		insert(scrollBar);
		mr::dialogs::insertUniformButtonRow(*this, buttonLeft, layout.height - 3, 2, buttons);
		tableView->select();
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evCommand && event.message.command == cmHelp) {
			messageBox(mfInformation | mfOKButton, "Use arrow keys to select a character. Enter posts it to the focused editor window.");
			clearEvent(event);
			return;
		}
		MRDialogFoundation::handleEvent(event);
	}

	[[nodiscard]] std::string selectedText() const {
		return tableView != nullptr ? tableView->selectedText() : std::string();
	}

  private:
	CharacterTableView *tableView = nullptr;
	TScrollBar *scrollBar = nullptr;
};

class NumericInputDialog final : public MRDialogFoundation {
  public:
	struct Layout {
		short width = 52;
		short height = 10;
		short inputLeft = 18;
		short inputRight = 48;
		short buttonY = 6;
		short buttonLeft = 8;
		short buttonGap = 2;
		bool showHelp = true;
	};

	NumericInputDialog(const char *title, const char *label, const char *helpText, int initialValue, int minValue, int maxValue, const Layout &layout) : TWindowInit(initMrDialogFrame), MRDialogFoundation(mr::dialogs::centeredDialogRect(layout.width, layout.height), title, layout.width, layout.height, initMrDialogFrame), mHelpText(helpText != nullptr ? helpText : ""), mMinValue(minValue), mMaxValue(maxValue) {
		char buffer[32] = {0};

		std::snprintf(buffer, sizeof(buffer), "%d", initialValue);
		mInputField = new TInputLine(TRect(layout.inputLeft, 2, layout.inputRight, 3), layout.inputRight - layout.inputLeft);
		if (label != nullptr && label[0] != '\0') insert(new TLabel(TRect(2, 2, layout.inputLeft, 3), label, mInputField));
		insert(mInputField);
		if (layout.showHelp) {
			const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
			mr::dialogs::insertUniformButtonRow(*this, layout.buttonLeft, layout.buttonY, layout.buttonGap, buttons);
		} else {
			const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
			mr::dialogs::insertUniformButtonRow(*this, layout.buttonLeft, layout.buttonY, layout.buttonGap, buttons);
		}
		mInputField->setData(buffer);
		setDialogValidationHook([this]() {
			MRScrollableDialog::DialogValidationResult result;
			int ignored = 0;

			result.valid = tryReadValue(ignored);
			if (!result.valid) result.warningText = "Please enter an integer in range.";
			return result;
		});
		finalizeLayout();
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evCommand && event.message.command == cmHelp) {
			messageBox(mfInformation | mfOKButton, "%s", mHelpText.c_str());
			clearEvent(event);
			return;
		}
		MRDialogFoundation::handleEvent(event);
	}

	[[nodiscard]] bool tryReadValue(int &outValue) const {
		char buffer[32] = {0};
		char *endPtr = nullptr;
		long parsed = 0;

		if (mInputField == nullptr) return false;
		const_cast<TInputLine *>(mInputField)->getData(buffer);
		parsed = std::strtol(buffer, &endPtr, 10);
		if (endPtr == buffer || !trimAscii(endPtr != nullptr ? endPtr : "").empty()) return false;
		if (parsed < mMinValue || parsed > mMaxValue) return false;
		outValue = static_cast<int>(parsed);
		return true;
	}

  private:
	std::string mHelpText;
	TInputLine *mInputField = nullptr;
	int mMinValue = 0;
	int mMaxValue = 0;
};

NumericInputDialog::Layout defaultNumericInputDialogLayout() {
	return NumericInputDialog::Layout{};
}

bool promptIntegerValue(const char *title, const char *label, const char *helpText, int initialValue, int minValue, int maxValue, int &outValue);
bool promptIntegerValue(const char *title, const char *label, const char *helpText, int initialValue, int minValue, int maxValue, int &outValue, const NumericInputDialog::Layout &layout);

bool insertTextIntoWindow(MREditWindow *win, const std::string &text) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	if (win == nullptr || editor == nullptr || text.empty()) return true;
	if (win->isReadOnly()) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Window is read-only.", mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return true;
	}
	if (!editor->insertBufferText(text)) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Unable to insert selected character.", mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return true;
	}
	editor->revealCursor(True);
	editor->drawView();
	return true;
}

bool handleCharacterTable(CharacterTableKind kind) {
	MREditWindow *targetWindow = currentEditWindow();
	CharacterTableDialog *dialog = nullptr;
	const CharacterTableLayout layout = layoutForCharacterTable(kind);
	std::string selectedText;
	ushort result = cmCancel;

	if (targetWindow == nullptr || targetWindow->getEditor() == nullptr) return true;
	if (targetWindow->isReadOnly()) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Window is read-only.", mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return true;
	}

	switch (kind) {
		case CharacterTableKind::Ascii:
			dialog = new CharacterTableDialog(layout, buildAsciiTableEntries());
			break;
		case CharacterTableKind::Emoji:
			dialog = new CharacterTableDialog(layout, buildEmojiTableEntries());
			break;
	}
	if (dialog == nullptr || TProgram::deskTop == nullptr) {
		TObject::destroy(dialog);
		return true;
	}
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	if (result == cmOK) selectedText = dialog->selectedText();
	TObject::destroy(dialog);
	if (result == cmOK) return insertTextIntoWindow(targetWindow, selectedText);
	return true;
}

constexpr const char *kNoCommandSpecifiedMessage = "No command specified.";
constexpr const char *kWindowReadOnlyMessage = "Window is read-only.";
constexpr const char *kChooseAnotherWindowForBlockMessage = "Choose another window for inter-window block operation.";
constexpr const char *kNoMarkedBlockInSourceWindowMessage = "No block marked in the selected source window.";
constexpr const char *kNoExternalProgramTaskMessage = "No external program task is running in this window.";
constexpr const char *kStopProgramBeforeRestartMessage = "Stop the current program before restarting it.";
constexpr const char *kNoRestartableCommandMessage = "No restartable command is associated with this window.";

void postSearchError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, std::string(text), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

void postDialogWarning(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string(text), mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
}

void postDialogError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string(text), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

bool parseIntFieldInRange(std::string_view text, int minValue, int maxValue, int &value) {
	char *end = nullptr;
	const std::string trimmed = trimAscii(text);
	long parsed = 0;

	if (trimmed.empty()) return false;
	errno = 0;
	parsed = std::strtol(trimmed.c_str(), &end, 10);
	if (errno != 0 || end == nullptr || *end != '\0' || parsed < minValue || parsed > maxValue || parsed > std::numeric_limits<int>::max()) return false;
	value = static_cast<int>(parsed);
	return true;
}

bool decodePdfExportSeparatorLiteral(std::string_view input, std::string &decoded, std::string &errorText) {
	decoded.clear();
	for (std::size_t i = 0; i < input.size(); ++i) {
		const char ch = input[i];
		if (ch != '\\') {
			decoded.push_back(ch);
			continue;
		}
		if (i + 1 >= input.size()) {
			errorText = "Page separator literal ends with a dangling backslash.";
			return false;
		}
		const char next = input[++i];
		switch (next) {
			case 'f':
				decoded.push_back('\f');
				break;
			case 'n':
				decoded.push_back('\n');
				break;
			case 'r':
				decoded.push_back('\r');
				break;
			case 't':
				decoded.push_back('\t');
				break;
			case '\\':
				decoded.push_back('\\');
				break;
			default:
				errorText = "Unsupported page separator escape sequence.";
				return false;
		}
	}
	if (decoded.empty()) {
		errorText = "Page separator literal must not be empty.";
		return false;
	}
	errorText.clear();
	return true;
}

std::string defaultPdfExportPathForWindow(const MREditWindow *window) {
	std::filesystem::path path;

	if (window != nullptr && window->currentFileName()[0] != '\0') {
		path = std::filesystem::path(normalizeConfiguredPathInput(window->currentFileName()));
		path.replace_extension(".pdf");
		return path.string();
	}
	path = std::filesystem::path("mr-export.pdf");
	return path.string();
}

MRPdfExportSettings pdfExportSettingsFromDialogData(const MRPdfExportDialogData &data) {
	MRPdfExportSettings settings;

	settings.outputPath = data.outputPath;
	settings.pageSeparatorLiteral = data.pageSeparatorLiteral;
	settings.fontFamily = data.fontFamily;
	settings.fontSizePoints = std::clamp<int>(data.fontSizePoints, 1, 40);
	settings.headerLine = data.headerLine;
	settings.footerLine = data.footerLine;
	settings.textWidth = data.textWidth;
	settings.leftMarginPoints = data.leftMarginPoints;
	settings.rightMarginPoints = data.rightMarginPoints;
	settings.topMarginPoints = data.topMarginPoints;
	settings.bottomMarginPoints = data.bottomMarginPoints;
	return settings;
}

void loadPdfExportDialogData(MRPdfExportDialogData &dialogData, const MRPdfExportSettings &settings, const MREditWindow *window, const MREditSetupSettings &editSettings) {
	mr::dialogs::writeRecordField(dialogData.outputPath, sizeof(dialogData.outputPath), settings.outputPath.empty() ? defaultPdfExportPathForWindow(window) : settings.outputPath);
	mr::dialogs::writeRecordField(dialogData.pageSeparatorLiteral, sizeof(dialogData.pageSeparatorLiteral), settings.pageSeparatorLiteral);
	mr::dialogs::writeRecordField(dialogData.fontFamily, sizeof(dialogData.fontFamily), settings.fontFamily);
	dialogData.fontSizePoints = std::clamp<int>(settings.fontSizePoints, 1, 40);
	mr::dialogs::writeRecordField(dialogData.headerLine, sizeof(dialogData.headerLine), settings.headerLine);
	mr::dialogs::writeRecordField(dialogData.footerLine, sizeof(dialogData.footerLine), settings.footerLine);
	mr::dialogs::writeRecordField(dialogData.textWidth, sizeof(dialogData.textWidth), settings.textWidth.empty() ? std::to_string(editSettings.rightMargin > 0 ? editSettings.rightMargin : 78) : settings.textWidth);
	mr::dialogs::writeRecordField(dialogData.leftMarginPoints, sizeof(dialogData.leftMarginPoints), settings.leftMarginPoints);
	mr::dialogs::writeRecordField(dialogData.rightMarginPoints, sizeof(dialogData.rightMarginPoints), settings.rightMarginPoints);
	mr::dialogs::writeRecordField(dialogData.topMarginPoints, sizeof(dialogData.topMarginPoints), settings.topMarginPoints);
	mr::dialogs::writeRecordField(dialogData.bottomMarginPoints, sizeof(dialogData.bottomMarginPoints), settings.bottomMarginPoints);
}

bool persistPdfExportDialogState(const MRPdfExportDialogData &dialogData, std::string &errorText) {
	if (!setConfiguredPdfExportSettings(pdfExportSettingsFromDialogData(dialogData), &errorText)) return false;
	if (!persistConfiguredSettingsSnapshot(&errorText)) return false;
	errorText.clear();
	return true;
}

bool buildPdfExportSettings(const MRPdfExportDialogData &data, MRPdfTextExporter::Settings &settings, std::string &errorText) {
	std::string separatorLiteral;
	std::filesystem::path outputPath;

	settings = MRPdfTextExporter::Settings();
	settings.outputPath = normalizeConfiguredPathInput(trimAscii(data.outputPath));
	if (settings.outputPath.empty()) {
		errorText = "Output path must not be empty.";
		return false;
	}
	outputPath = std::filesystem::path(settings.outputPath);
	if (!outputPath.has_extension()) outputPath.replace_extension(".pdf");
	settings.outputPath = outputPath.string();
	if (std::filesystem::exists(outputPath) && std::filesystem::is_directory(outputPath)) {
		errorText = "Output path points to a directory.";
		return false;
	}
	if (outputPath.has_parent_path() && !std::filesystem::exists(outputPath.parent_path())) {
		errorText = "Output directory does not exist.";
		return false;
	}
	if (!decodePdfExportSeparatorLiteral(trimAscii(data.pageSeparatorLiteral), separatorLiteral, errorText)) return false;
	settings.pageSeparatorLiteral = separatorLiteral;
	settings.fontFamily = trimAscii(data.fontFamily);
	if (settings.fontFamily.empty()) {
		errorText = "Font family must not be empty.";
		return false;
	}
	settings.fontSizePoints = std::clamp<int>(data.fontSizePoints, 1, 40);
	settings.headerLine = data.headerLine;
	settings.footerLine = data.footerLine;
	if (!parseIntFieldInRange(data.textWidth, 0, 9999, settings.textWidthColumns)) {
		errorText = "Text width must be an integer within 0..9999.";
		return false;
	}
	int marginValue = 0;
	if (!parseIntFieldInRange(data.leftMarginPoints, 0, 9999, marginValue)) {
		errorText = "Margins must be integers within 0..9999.";
		return false;
	}
	settings.leftMarginPoints = static_cast<double>(marginValue);
	if (!parseIntFieldInRange(data.rightMarginPoints, 0, 9999, marginValue)) {
		errorText = "Margins must be integers within 0..9999.";
		return false;
	}
	settings.rightMarginPoints = static_cast<double>(marginValue);
	if (!parseIntFieldInRange(data.topMarginPoints, 0, 9999, marginValue)) {
		errorText = "Margins must be integers within 0..9999.";
		return false;
	}
	settings.topMarginPoints = static_cast<double>(marginValue);
	if (!parseIntFieldInRange(data.bottomMarginPoints, 0, 9999, marginValue)) {
		errorText = "Margins must be integers within 0..9999.";
		return false;
	}
	settings.bottomMarginPoints = static_cast<double>(marginValue);
	errorText.clear();
	return true;
}

bool confirmPdfExportOverwrite(const std::string &path) {
	return !std::filesystem::exists(path) || messageBox(mfConfirmation | mfYesButton | mfNoButton, "PDF file exists.\nOverwrite?\n%s", path.c_str()) == cmYes;
}

bool handleExportToPdf() {
	MREditWindow *window = currentEditWindow();
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	MRPdfTextExporter exporter;
	MRPdfTextExporter::Settings settings;
	MRPdfExportDialogData dialogData;
	MRPdfExportSettings persistedDialogSettings = configuredPdfExportSettings();
	const MREditSetupSettings editSettings = configuredEditSetupSettings();
	std::string documentText;
	std::string errorText;

	if (window == nullptr || editor == nullptr) {
		postDialogError("No active file window.");
		return true;
	}

	loadPdfExportDialogData(dialogData, persistedDialogSettings, window, editSettings);
	documentText = editor->snapshotText();

	while (true) {
		const ushort result = runPdfExportDialog(dialogData);
		if (!persistPdfExportDialogState(dialogData, errorText)) {
			postDialogError("PDF export settings save failed: " + errorText);
			return true;
		}
		if (result == cmCancel || result == cmClose) return true;
		if (!buildPdfExportSettings(dialogData, settings, errorText)) {
			postDialogError(errorText);
			continue;
		}
		if (!confirmPdfExportOverwrite(settings.outputPath)) continue;
		if (!exporter.exportText(documentText, settings, &errorText)) {
			postDialogError(errorText);
			continue;
		}
		mr::dialogs::writeRecordField(dialogData.outputPath, sizeof(dialogData.outputPath), settings.outputPath);
		if (!persistPdfExportDialogState(dialogData, errorText)) {
			postDialogError("PDF export settings save failed: " + errorText);
			return true;
		}
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "PDF exported: " + settings.outputPath, mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
		return true;
	}
}

bool promptGotoLineNumber(long &lineNumber) {
	NumericInputDialog::Layout layout;
	const int kMinLineNumber = 1;
	const int kMaxLineNumber = 999999999;
	int value = 0;

	layout.width = 30;
	layout.height = 8;
	layout.inputLeft = 4;
	layout.inputRight = 26;
	layout.buttonY = 4;
	layout.buttonLeft = 4;
	layout.buttonGap = 2;
	layout.showHelp = false;
	if (!promptIntegerValue("GOTO LINE NUMBER", "", "Enter the target line number.", static_cast<int>(std::max<long>(kMinLineNumber, std::min<long>(lineNumber > 0 ? lineNumber : 1, kMaxLineNumber))), kMinLineNumber, kMaxLineNumber, value, layout)) return false;
	lineNumber = value;
	return true;
}

bool promptIntegerValue(const char *title, const char *label, const char *helpText, int initialValue, int minValue, int maxValue, int &outValue) {
	return promptIntegerValue(title, label, helpText, initialValue, minValue, maxValue, outValue, defaultNumericInputDialogLayout());
}

bool promptIntegerValue(const char *title, const char *label, const char *helpText, int initialValue, int minValue, int maxValue, int &outValue, const NumericInputDialog::Layout &layout) {
	NumericInputDialog *dialog = new NumericInputDialog(title, label, helpText, initialValue, minValue, maxValue, layout);
	bool accepted = false;
	ushort result = cmCancel;

	if (dialog == nullptr) return false;
	if (TProgram::deskTop == nullptr) {
		TObject::destroy(dialog);
		return false;
	}
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel) accepted = dialog->tryReadValue(outValue);
	TObject::destroy(dialog);
	return accepted;
}

bool handleSearchGotoLineNumber() {
	MREditWindow *win = currentEditWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	long lineNumber = 0;
	std::size_t offset = 0;
	std::size_t line = 1;
	std::size_t length = 0;

	if (editor == nullptr) return true;
	if (!promptGotoLineNumber(lineNumber)) return true;

	length = editor->bufferLength();
	offset = 0;
	line = 1;
	while (line < static_cast<std::size_t>(lineNumber) && offset < length) {
		std::size_t next = editor->nextLineOffset(offset);
		if (next <= offset) break;
		offset = next;
		++line;
	}
	editor->setCursorOffset(offset);
	editor->setSelectionOffsets(offset, offset);
	editor->revealCursor(True);
	return true;
}

bool handleFileOpen() {
	enum {
		FileNameBufferSize = MAXPATH
	};
	char fileName[FileNameBufferSize];
	MREditWindow *target;
	MREditWindow *current = currentEditWindow();
	std::string resolvedPath;
	std::string logLine;
	bool createdTarget = false;

	if (!promptForPath("Open File", fileName, sizeof(fileName))) return true;
	if (!resolveReadableExistingPath(MRDialogHistoryScope::OpenFile, fileName, resolvedPath)) {
		forgetLoadDialogPath(MRDialogHistoryScope::OpenFile, fileName);
		return true;
	}

	target = findReusableEmptyWindow(current);
	if (target == nullptr) {
		target = createEditorWindow("?No-File?");
		createdTarget = true;
	}
	if (!loadResolvedFileIntoWindow(target, resolvedPath, "Open file")) {
		forgetLoadDialogPath(MRDialogHistoryScope::OpenFile, resolvedPath.c_str());
		if (createdTarget && target != nullptr) message(target, evCommand, cmClose, nullptr);
		if (target != nullptr && isEmptyUntitledEditableWindow(target) && current != target && current != nullptr) static_cast<void>(mrActivateEditWindow(current));
		return true;
	}
	rememberLoadDialogPath(MRDialogHistoryScope::OpenFile, resolvedPath.c_str());
	static_cast<void>(mrActivateEditWindow(target));
	logLine = "Opened file: ";
	logLine += target->currentFileName();
	if (target->isReadOnly()) logLine += " [read-only]";
	mrLogMessage(logLine.c_str());
	return true;
}

bool handleFileLoad() {
	enum {
		FileNameBufferSize = MAXPATH
	};
	char fileName[FileNameBufferSize];
	MREditWindow *target = currentEditWindow();
	std::string resolvedPath;
	std::string logLine;
	bool createdTarget = false;

	if (!promptForPath("Load File", fileName, sizeof(fileName))) return true;
	if (!resolveReadableExistingPath(MRDialogHistoryScope::LoadFile, fileName, resolvedPath)) {
		forgetLoadDialogPath(MRDialogHistoryScope::LoadFile, fileName);
		return true;
	}
	if (target == nullptr) {
		target = createEditorWindow("?No-File?");
		createdTarget = true;
	} else if (!target->confirmAbandonForReload())
		return true;
	if (!loadResolvedFileIntoWindow(target, resolvedPath, "Load file")) {
		forgetLoadDialogPath(MRDialogHistoryScope::LoadFile, resolvedPath.c_str());
		if (createdTarget && target != nullptr) message(target, evCommand, cmClose, nullptr);
		return true;
	}
	rememberLoadDialogPath(MRDialogHistoryScope::LoadFile, resolvedPath.c_str());
	static_cast<void>(mrActivateEditWindow(target));
	logLine = "Loaded file into active window: ";
	logLine += target->currentFileName();
	if (target->isReadOnly()) logLine += " [read-only]";
	mrLogMessage(logLine.c_str());
	return true;
}

bool handleFileAcquire() {
	static_cast<void>(runAcquireDialog(MRAcquireMode::OpenFile));
	return true;
}

bool handleExecuteProgram() {
	std::string commandLine;
	MREditWindow *win;

	if (!promptForCommandLine(commandLine)) return true;
	if (commandLine.empty()) {
		postDialogWarning(kNoCommandSpecifiedMessage);
		return true;
	}

	win = createEditorWindow(shortenCommandTitle(commandLine).c_str());
	if (win == nullptr) {
		postSearchError("Unable to create communication window.");
		return true;
	}
	startExternalCommandInWindow(win, commandLine, true, true, true);
	return true;
}

bool handleBuildCurrentFile() {
	MREditWindow *win = currentEditWindow();
	std::string sourcePath;
	std::string matchedProfileName;
	std::string errorText;
	std::string commandLine;
	MRCompilerProfile compilerProfile;
	MREditWindow *outputWindow;

	if (win == nullptr) return true;
	sourcePath = win->currentFileName();
	if (sourcePath.empty()) {
		postDialogWarning("Build current file requires a named source file.");
		return true;
	}
	if (win->isFileChanged() && !win->saveCurrentFile()) {
		postDialogWarning("Unable to save current file before build.");
		return true;
	}
	if (!effectiveCompilerProfileForPath(sourcePath, compilerProfile, &matchedProfileName, &errorText)) {
		postDialogWarning(errorText.empty() ? "No compiler profile for current file." : errorText);
		return true;
	}
	if (!buildCompilerProfileCommandLine(compilerProfile, sourcePath, commandLine, &errorText)) {
		postDialogWarning(errorText.empty() ? "Unable to build compiler command line." : errorText);
		return true;
	}

	outputWindow = createEditorWindow(shortenCommandTitle(commandLine).c_str());
	if (outputWindow == nullptr) {
		postSearchError("Unable to create build output window.");
		return true;
	}
	startExternalCommandInWindow(outputWindow, commandLine, true, true, true);
	return true;
}

bool startExternalCommandInWindow(MREditWindow *win, const std::string &commandLine, bool replaceBuffer, bool activate, bool closeOnFailure) {
	std::string title;
	std::string initialText;
	std::ostringstream logLine;
	std::uint64_t taskId;

	if (win == nullptr) return false;
	title = shortenCommandTitle(commandLine);
	initialText = "$ " + commandLine + "\n\n";
	if (replaceBuffer) {
		if (!win->replaceTextBuffer(initialText.c_str(), title.c_str())) {
			if (closeOnFailure) message(win, evCommand, cmClose, nullptr);
			postSearchError("Unable to prepare communication window.");
			return false;
		}
	}
	win->setReadOnly(true);
	win->setFileChanged(false);
	win->setWindowRole(MREditWindow::wrCommunicationCommand, commandLine);
	if (activate) static_cast<void>(mrActivateEditWindow(win));

	taskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Io, mr::coprocessor::TaskKind::ExternalIo, static_cast<std::size_t>(win->bufferId()), 0, std::string("external-io: ") + commandLine, [commandLine, channelId = static_cast<std::size_t>(win->bufferId())](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) { return runExternalCommandTask(info, stopToken, channelId, commandLine); });
	if (taskId == 0) {
		if (closeOnFailure) message(win, evCommand, cmClose, nullptr);
		postSearchError("Unable to start external command worker.");
		return false;
	}
	win->trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::ExternalIo, commandLine);

	logLine << "Started external command in communication window #" << win->bufferId() << ": " << commandLine << " [task #" << taskId << "]";
	mrLogMessage(logLine.str().c_str());
	return true;
}

bool dispatchEditorCommand(ushort editorCommand, bool requiresWritable) {
	MREditWindow *win = currentEditWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

	if (win == nullptr || editor == nullptr) return true;
	if (requiresWritable && win->isReadOnly()) {
		postDialogWarning(kWindowReadOnlyMessage);
		return true;
	}
	message(editor, evCommand, editorCommand, nullptr);
	return true;
}

bool dispatchEditorClipboardCommand(ushort editorCommand, bool requiresWritable) {
	return dispatchEditorCommand(editorCommand, requiresWritable);
}

void syncPersistentBlocksMenuState() {
	if (auto *mrMenuBar = dynamic_cast<MRMenuBar *>(TProgram::menuBar)) mrMenuBar->setPersistentBlocksMenuState(configuredPersistentBlocksSetting());
}

bool handleBlockAction(bool ok, const char *failureText) {
	if (!ok && failureText != nullptr && *failureText != '\0') messageBox(mfInformation | mfOKButton, "%s", failureText);
	return true;
}

bool hasMarkedTextForBlockOperation(MREditWindow *win) {
	if (win == nullptr || !win->hasBlock()) return false;
	if (win->blockStatus() == MREditWindow::bmStream && win->blockAnchorPtr() == win->blockEffectiveEndPtr()) return false;
	return true;
}

bool promptBlockSavePath(std::string &outPath) {
	char buffer[MAXPATH] = {0};
	ushort result = cmCancel;
	MREditWindow *win = currentEditWindow();

	outPath.clear();
	mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::BlockSave, buffer, sizeof(buffer), "*.*");
	mr::dialogs::suggestFileDialogName(buffer, sizeof(buffer), win != nullptr && win->currentFileName() != nullptr ? std::string_view(win->currentFileName()) : std::string_view());
	result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::BlockSave, "*.*", "Save block as", "~N~ame", fdOKButton, buffer);
	if (result == cmCancel) return false;
	fexpand(buffer);
	outPath = buffer;
	if (outPath.find('*') != std::string::npos || outPath.find('?') != std::string::npos) return false;
	return !outPath.empty();
}

bool promptBlockLoadPath(std::string &outPath) {
	char buffer[MAXPATH] = {0};
	ushort result = cmCancel;

	outPath.clear();
	mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::BlockLoad, buffer, sizeof(buffer), "*.*");
	result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::BlockLoad, "*.*", "Load block from", "~N~ame", fdOpenButton, buffer);
	if (result == cmCancel) return false;
	fexpand(buffer);
	outPath = buffer;
	if (outPath.find('*') != std::string::npos || outPath.find('?') != std::string::npos) return false;
	return !outPath.empty();
}

void acceptFetchedClipboardText(TStringView text) {
	g_clipboardFetchState.received = true;
	g_clipboardFetchState.text.assign(text.data(), text.size());
}

bool readSystemClipboardText(std::string &outText) {
	g_clipboardFetchState = ClipboardFetchState();
	if (!THardwareInfo::requestClipboardText(acceptFetchedClipboardText) || !g_clipboardFetchState.received) return false;
	outText = g_clipboardFetchState.text;
	return true;
}

void writeSystemClipboardText(std::string_view text) {
	TClipboard::setText(TStringView(text.data(), text.size()));
}

bool copyCurrentBlockToSystemClipboard(bool append, bool cut, const char *failureText) {
	std::string blockText;
	std::string clipboardText;

	if (!mrvmUiExtractCurrentBlockText(blockText)) return handleBlockAction(false, failureText);
	if (append) {
		if (!readSystemClipboardText(clipboardText)) {
			postDialogWarning("Unable to read system clipboard.");
			return true;
		}
		clipboardText += blockText;
	} else
		clipboardText = blockText;
	writeSystemClipboardText(clipboardText);
	if (cut) return handleBlockAction(mrvmUiDeleteBlock(), failureText);
	return true;
}

bool copyCurrentBlockToSystemClipboard() {
	return copyCurrentBlockToSystemClipboard(false, false, "No block marked.");
}

bool promptBlockMathOperation(char &operation) {
	char operationBuffer[8] = {0};

	operation = '\0';
	if (inputBox("BLOCK MATH", "~O~peration (+,-,*,/)", operationBuffer, static_cast<ushort>(sizeof(operationBuffer) - 1)) == cmCancel) return false;
	switch (operationBuffer[0]) {
		case '+':
		case '-':
		case '*':
		case '/':
			operation = operationBuffer[0];
			return true;
		default:
			postDialogWarning("Operation must be one of: + - * /");
			return false;
	}
}

bool parseBlockMathValues(std::string_view text, std::vector<double> &values) {
	std::string token;
	auto flushToken = [&]() {
		bool sawDigit = false;
		std::string normalized;
		char *endPtr = nullptr;
		double value = 0.0;

		if (token.empty()) return;
		for (const char ch : token) {
			if (std::isdigit(static_cast<unsigned char>(ch)) != 0) sawDigit = true;
			if (ch != ',') normalized.push_back(ch);
		}
		token.clear();
		if (!sawDigit || normalized.empty()) return;
		value = std::strtod(normalized.c_str(), &endPtr);
		if (endPtr != nullptr && *endPtr == '\0') values.push_back(value);
	};

	values.clear();
	for (const char ch : text) {
		if (std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '.' || ch == ',' || ch == '-') token.push_back(ch);
		else
			flushToken();
	}
	flushToken();
	return !values.empty();
}

std::string formatBlockMathResult(double value) {
	std::ostringstream out;
	std::string text;

	out << std::setprecision(15) << value;
	text = out.str();
	if (text.find('.') != std::string::npos) {
		while (!text.empty() && text.back() == '0')
			text.pop_back();
		if (!text.empty() && text.back() == '.') text.pop_back();
	}
	return text.empty() ? "0" : text;
}

bool runBlockMathOperation() {
	MREditWindow *win = currentEditWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::string blockText;
	std::vector<double> values;
	char operation = '\0';
	double result = 0.0;

	if (editor == nullptr) return true;
	if (!promptBlockMathOperation(operation)) return true;
	if (!mrvmUiExtractCurrentBlockText(blockText)) {
		postDialogWarning("No block marked.");
		return true;
	}
	if (!parseBlockMathValues(blockText, values)) {
		postDialogWarning("Marked block contains no numbers.");
		return true;
	}
	result = values.front();
	for (std::size_t i = 1; i < values.size(); ++i)
		switch (operation) {
			case '+':
				result += values[i];
				break;
			case '-':
				result -= values[i];
				break;
			case '*':
				result *= values[i];
				break;
			case '/':
				if (values[i] == 0.0) {
					postDialogWarning("Division by zero in marked block.");
					return true;
				}
				result /= values[i];
				break;
			default:
				postDialogWarning("Unsupported math operation.");
				return true;
		}
	if (win->isReadOnly()) {
		postDialogWarning(kWindowReadOnlyMessage);
		return true;
	}
	return handleBlockAction(editor->insertBufferText(formatBlockMathResult(result)), "Unable to insert block math result.");
}


bool chooseInterWindowBlockTarget(int &sourceWindowIndex) {
	MREditWindow *targetWin = currentEditWindow();
	MREditWindow *sourceWin = nullptr;

	sourceWindowIndex = 0;
	if (targetWin == nullptr) return false;
	sourceWin = mrShowWindowListDialog(mrwlActivateWindow, targetWin);
	if (sourceWin == nullptr) return false;
	if (sourceWin == targetWin) {
		postDialogWarning(kChooseAnotherWindowForBlockMessage);
		return false;
	}
	if (!hasMarkedTextForBlockOperation(sourceWin)) {
		postDialogWarning(kNoMarkedBlockInSourceWindowMessage);
		return false;
	}
	sourceWindowIndex = mrvmUiCurrentWindowIndex(sourceWin);
	static_cast<void>(mrActivateEditWindow(targetWin));
	return true;
}

bool persistVisibleEditSetupSettingsWithFeedback(const MREditSetupSettings &settings, const std::string &errorPrefix);

bool togglePersistentBlocksSetting() {
	MREditSetupSettings settings = configuredEditSetupSettings();
	bool enabled;

	settings.persistentBlocks = !settings.persistentBlocks;
	if (!persistVisibleEditSetupSettingsWithFeedback(settings, "Persistent blocks update failed: ")) return true;

	enabled = configuredPersistentBlocksSetting();
	mrLogMessage(enabled ? "Persistent blocks enabled." : "Persistent blocks disabled.");
	syncPersistentBlocksMenuState();
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, enabled ? "Persistent blocks: ON" : "Persistent blocks: OFF", mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool persistVisibleEditSetupSettingsWithFeedback(const MREditSetupSettings &settings, const std::string &errorPrefix) {
	MRSettingsWriteReport writeReport;
	std::string errorText;

	if (!setConfiguredEditSetupSettings(settings, &errorText)) {
		postDialogError(errorPrefix + errorText);
		return false;
	}
	if (!persistConfiguredSettingsSnapshot(&errorText, &writeReport)) {
		postDialogError("Settings save failed: " + errorText);
		return false;
	}
	mrLogSettingsWriteReport("visible edit setup update", writeReport);
	for (MREditWindow *window : allEditWindowsInZOrder())
		if (window != nullptr && window->getEditor() != nullptr) window->getEditor()->refreshConfiguredVisualSettings();
	return true;
}

bool handleWordstarSetRightMargin() {
	MREditSetupSettings settings = configuredEditSetupSettings();
	NumericInputDialog::Layout layout = defaultNumericInputDialogLayout();
	int minimumMargin = std::min(999, std::max(1, settings.leftMargin + 1));
	int margin = settings.rightMargin > 0 ? settings.rightMargin : 78;

	layout.width = 30;
	layout.height = 8;
	layout.inputLeft = 4;
	layout.inputRight = 26;
	layout.buttonY = 4;
	layout.buttonLeft = 4;
	layout.buttonGap = 2;
	layout.showHelp = false;
	if (margin < minimumMargin) margin = minimumMargin;
	if (!promptIntegerValue("SET RIGHT MARGIN", "", "Set the global RIGHT_MARGIN used for editor formatting.", margin, minimumMargin, 999, margin, layout)) return true;
	settings.rightMargin = margin;
	settings.formatLine = synchronizeEditFormatLineMargins(settings.formatLine, settings.leftMargin, settings.rightMargin, settings.tabSize);
	if (!persistVisibleEditSetupSettingsWithFeedback(settings, "Right margin update failed: ")) return true;
	mrLogMessage(("Right margin set to " + std::to_string(margin) + ".").c_str());
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Right margin updated.", mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleWordstarSetLeftMargin(MREditWindow *window) {
	MREditSetupSettings settings = configuredEditSetupSettings();
	int maximumMargin = std::max(1, settings.rightMargin - 1);
	int margin = settings.leftMargin > 0 ? settings.leftMargin : 1;

	static_cast<void>(window);
	if (!promptIntegerValue("SET LEFT MARGIN", "~M~argin:", "Set the global LEFT_MARGIN used for editor formatting.", margin, 1, maximumMargin, margin)) return true;
	settings.leftMargin = margin;
	settings.formatLine = synchronizeEditFormatLineMargins(settings.formatLine, settings.leftMargin, settings.rightMargin, settings.tabSize);
	if (!persistVisibleEditSetupSettingsWithFeedback(settings, "Left margin update failed: ")) return true;
	mrLogMessage(("Left margin set to " + std::to_string(margin) + ".").c_str());
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Left margin updated.", mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleWordstarToggleWordWrap() {
	MREditSetupSettings settings = configuredEditSetupSettings();

	settings.wordWrap = !settings.wordWrap;
	if (!persistVisibleEditSetupSettingsWithFeedback(settings, "Word wrap update failed: ")) return true;
	return true;
}

bool handleToggleFormatRuler() {
	MREditSetupSettings settings = configuredEditSetupSettings();

	settings.formatRuler = !settings.formatRuler;
	if (!persistVisibleEditSetupSettingsWithFeedback(settings, "Format ruler update failed: ")) return true;
	mrLogMessage(settings.formatRuler ? "Format ruler enabled." : "Format ruler disabled.");
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, settings.formatRuler ? "Format ruler: ON" : "Format ruler: OFF", mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleWordstarReformatParagraph(MREditWindow *window) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	const MREditSetupSettings settings = configuredEditSetupSettings();

	if (editor == nullptr || window == nullptr || window->isReadOnly()) return false;
	return editor->formatParagraph(settings.leftMargin, settings.rightMargin);
}

bool handleReformatDocument(MREditWindow *window) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	const MREditSetupSettings settings = configuredEditSetupSettings();

	if (editor == nullptr || window == nullptr || window->isReadOnly()) return false;
	return editor->formatDocument(settings.leftMargin, settings.rightMargin);
}

bool handleWordstarJustifyParagraph(MREditWindow *window) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	const MREditSetupSettings settings = configuredEditSetupSettings();

	if (editor == nullptr || window == nullptr || window->isReadOnly()) return false;
	return editor->justifyParagraph(settings.leftMargin, settings.rightMargin);
}

bool handleWordstarCenterLine(MREditWindow *window) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	const MREditSetupSettings settings = configuredEditSetupSettings();

	if (editor == nullptr || window == nullptr || window->isReadOnly()) return false;
	return editor->centerCurrentLine(settings.leftMargin, settings.rightMargin);
}

bool handleWordstarLoadBlockFromFile(MREditWindow *window) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	std::string path;
	std::string text;
	std::string errorText;
	std::size_t start = 0;
	std::size_t end = 0;

	if (window == nullptr || editor == nullptr || window->isReadOnly()) return false;
	if (!promptBlockLoadPath(path)) return true;
	if (!readTextFile(path, text, errorText)) {
		forgetLoadDialogPath(MRDialogHistoryScope::BlockLoad, path.c_str());
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return true;
	}
	start = editor->selectionStartOffset();
	end = editor->selectionEndOffset();
	if (end < start) std::swap(start, end);
	if (!editor->replaceRangeAndSelect(static_cast<uint>(start), static_cast<uint>(end), text.data(), static_cast<uint>(text.size()))) return true;
	window->applyCommittedBlockState(static_cast<int>(MREditWindow::bmStream), false, static_cast<uint>(start), static_cast<uint>(start + text.size()));
	rememberLoadDialogPath(MRDialogHistoryScope::BlockLoad, path.c_str());
	return true;
}

bool handleWordstarForceSave(MREditWindow *window) {
	if (window == nullptr) return false;
	if (window->isReadOnly()) {
		postDialogWarning(kWindowReadOnlyMessage);
		return true;
	}
	if (!window->isFileChanged()) return true;
	if (!window->saveCurrentFileWithoutOverwritePrompt()) {
		mrLogMessage("Force save failed.");
		return true;
	}
	mrLogMessage("Window force-saved.");
	return true;
}

bool handleWordstarExitDirtySaveAll() {
	std::vector<MREditWindow *> dirtyWindows;
	std::vector<std::string> dirtyItems;

	for (MREditWindow *window : allEditWindowsInZOrder()) {
		if (window == nullptr || !window->isFileChanged()) continue;
		dirtyWindows.push_back(window);
		dirtyItems.push_back(window->currentFileName()[0] != '\0' ? window->currentFileName() : window->getTitle(0));
	}
	if (dirtyWindows.empty()) return dispatchApplicationCommandEvent(cmQuit);
	switch (mr::dialogs::runDialogDirtyListGating("EXIT MR", "Unsaved windows exist.", "Dirty windows:", dirtyItems, "Save all")) {
		case mr::dialogs::UnsavedChangesChoice::Save:
			for (MREditWindow *window : dirtyWindows) {
				static_cast<void>(mrActivateEditWindow(window));
				if (!saveCurrentEditWindow()) return true;
			}
			return dispatchApplicationCommandEvent(cmQuit);
		case mr::dialogs::UnsavedChangesChoice::Discard:
			return dispatchApplicationCommandEvent(cmQuit);
		case mr::dialogs::UnsavedChangesChoice::Cancel:
		default:
			return true;
	}
}

bool handleStopCurrentProgram() {
	MREditWindow *win = currentEditWindow();
	std::ostringstream line;
	std::size_t taskCount;

	if (win == nullptr || !win->isCommunicationWindow()) return true;
	taskCount = win->trackedTaskCount(mr::coprocessor::TaskKind::ExternalIo);
	if (taskCount == 0) {
		postDialogWarning(kNoExternalProgramTaskMessage);
		return true;
	}
	if (!win->cancelTrackedExternalIoTasks()) return true;
	line << "Requested stop of " << taskCount << " external program task";
	if (taskCount != 1) line << "s";
	line << " in communication window #" << win->bufferId() << ".";
	mrLogMessage(line.str().c_str());
	return true;
}

bool handleRestartCurrentProgram() {
	MREditWindow *win = currentEditWindow();

	if (win == nullptr || win->windowRole() != MREditWindow::wrCommunicationCommand) return true;
	if (win->hasTrackedExternalIoTasks()) {
		postDialogWarning(kStopProgramBeforeRestartMessage);
		return true;
	}
	if (win->windowRoleDetail().empty()) {
		postDialogWarning(kNoRestartableCommandMessage);
		return true;
	}
	startExternalCommandInWindow(win, win->windowRoleDetail(), true, true, false);
	return true;
}

bool handleClearCurrentOutput() {
	MREditWindow *win = currentEditWindow();
	std::ostringstream line;

	if (win == nullptr) return true;
	if (win->windowRole() == MREditWindow::wrLog) {
		if (!mrClearLogWindow()) {
			postSearchError("Unable to clear log window.");
			return true;
		}
		mrLogMessage("Log window cleared.");
		return true;
	}
	if (!win->isCommunicationWindow()) return true;
	if (win->hasTrackedExternalIoTasks()) {
		messageBox(mfInformation | mfOKButton, "Stop the current program before clearing its output.");
		return true;
	}
	if (!win->replaceTextBuffer("", win->getTitle(0))) {
		postSearchError("Unable to clear communication window.");
		return true;
	}
	win->setReadOnly(true);
	win->setFileChanged(false);
	line << "Cleared communication window #" << win->bufferId() << ".";
	mrLogMessage(line.str().c_str());
	return true;
}


} // namespace

bool dispatchMRKeymapAction(std::string_view actionId, std::string_view sequenceText, MREditWindow *targetWindow) {
	const auto it = std::ranges::find(kKeymapActionDispatchTable, actionId, &KeymapActionDispatchEntry::actionId);
	MREditWindow *window = effectiveKeymapWindow(targetWindow);
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	const std::optional<int> markIndex = randomAccessMarkIndexFromSequence(sequenceText);

	if (it == kKeymapActionDispatchTable.end()) return false;
	switch (it->kind) {
		case KeymapDispatchKind::AppCommand:
			return dispatchApplicationCommandEvent(it->command);
		case KeymapDispatchKind::EditorCommand:
			return dispatchEditorCommandEvent(window, it->command);
		case KeymapDispatchKind::WindowMethod:
			return dispatchKeymapWindowMethod(window, it->windowMethod);
		case KeymapDispatchKind::Custom:
			switch (it->customAction) {
				case KeymapCustomAction::DeleteForwardCharOrBlock:
					if (window != nullptr && window->hasBlock()) return dispatchApplicationCommandEvent(cmMrBlockDelete);
					return dispatchEditorCommandEvent(window, cmDelChar);
				case KeymapCustomAction::LoadBlockFromFile:
					return handleWordstarLoadBlockFromFile(window);
				case KeymapCustomAction::SetRandomAccessMark:
					return markIndex && mrvmUiSetRandomAccessMark(*markIndex);
				case KeymapCustomAction::GetRandomAccessMark:
					return markIndex && mrvmUiGetRandomAccessMark(*markIndex);
				case KeymapCustomAction::CenterLine:
					return handleWordstarCenterLine(window);
				case KeymapCustomAction::ReformatParagraph:
					return handleWordstarReformatParagraph(window);
				case KeymapCustomAction::ReformatDocument:
					return handleReformatDocument(window);
				case KeymapCustomAction::ToggleFormatRuler:
					return handleToggleFormatRuler();
				case KeymapCustomAction::ToggleWordWrap:
					return handleWordstarToggleWordWrap();
				case KeymapCustomAction::SetLeftMargin:
					return handleWordstarSetLeftMargin(window);
				case KeymapCustomAction::SetRightMargin:
					return handleWordstarSetRightMargin();
				case KeymapCustomAction::JustifyParagraph:
					return handleWordstarJustifyParagraph(window);
				case KeymapCustomAction::SortColumnBlockToggle:
					return window != nullptr && window->sortColumnBlockToggleOrder();
				case KeymapCustomAction::ForceSave:
					return handleWordstarForceSave(window);
				case KeymapCustomAction::ExitDirtySaveAll:
					return handleWordstarExitDirtySaveAll();
				case KeymapCustomAction::MoveCursorToNextPageBreak:
					return handleBlockAction(mrvmUiMoveCursorToNextPageBreak(), "No next page break found.");
				case KeymapCustomAction::MoveCursorToPrevPageBreak:
					return handleBlockAction(mrvmUiMoveCursorToPrevPageBreak(), "No previous page break found.");
				case KeymapCustomAction::ScrollWindowUp:
					return window != nullptr && editor != nullptr && editor->scrollWindowByLines(1);
				case KeymapCustomAction::ScrollWindowDown:
					return window != nullptr && editor != nullptr && editor->scrollWindowByLines(-1);
				case KeymapCustomAction::CursorIndent:
					return handleBlockAction(mrvmUiCursorIndent(), "Unable to indent cursor position.");
				case KeymapCustomAction::CursorTabRight:
					return handleBlockAction(mrvmUiCursorTabRight(), "Unable to move to next tab stop.");
				case KeymapCustomAction::CursorTabLeft:
					return handleBlockAction(mrvmUiCursorTabLeft(), "Unable to move to previous tab stop.");
				case KeymapCustomAction::CursorUndent:
					return handleBlockAction(mrvmUiCursorUndent(), "Unable to undent cursor position.");
				case KeymapCustomAction::CopyMarkedBlockToSystemClipboard:
					return copyCurrentBlockToSystemClipboard();
				case KeymapCustomAction::BlockMath:
					return runBlockMathOperation();
				case KeymapCustomAction::ExtendBlockByMotion: {
					const auto sequence = MRKeymapSequence::parse(sequenceText);
					return window != nullptr && sequence && sequence->size() == 1 && window->shiftCursorBlockMark(sequence->tokens().front());
				}
				case KeymapCustomAction::SearchResultsNext:
					return handleSearchResultsNext();
				case KeymapCustomAction::None:
					return false;
			}
	}
	return false;
}

bool dispatchMRKeymapMacro(std::string_view macroSpec) {
	std::string errorText;

	if (macroSpec.empty()) return false;
	return runMacroSpecByName(std::string(macroSpec).c_str(), &errorText, true);
}

bool handleMRCommand(ushort command) {
	switch (command) {
		case cmMrFileOpen:
			return handleFileOpen();

		case cmMrFileLoad:
			return handleFileLoad();

		case cmMrFileAcquire:
			return handleFileAcquire();

		case cmMrFileOpenLiveLog:
			return openLiveLogViewer();

		case cmMrFileOpenJournal:
			return openJournalViewer();

		case cmMrFileSave:
			static_cast<void>(saveCurrentEditWindow());
			return true;

		case cmMrFileSaveAs:
			static_cast<void>(saveCurrentEditWindowAs());
			return true;

		case cmMrFileSaveAll:
			static_cast<void>(saveAllDirtyEditWindows());
			return true;

		case cmMrFileRevert:
			static_cast<void>(revertEditWindow(currentEditWindow()));
			return true;

		case cmMrFileInformation:
			showFileInformationDialog(currentEditWindow());
			return true;

		case cmMrFilePrint:
			return handleExportToPdf();

		case cmMrEditUndo:
			return dispatchEditorCommand(cmMrEditUndo, true);

		case cmMrEditRedo:
			return dispatchEditorCommand(cmMrEditRedo, true);

		case cmMrEditCutToBuffer:
			return copyCurrentBlockToSystemClipboard(false, true, "Unable to move block to buffer.");

		case cmMrEditCopyToBuffer:
			return copyCurrentBlockToSystemClipboard(false, false, "No block marked.");

		case cmMrEditAppendToBuffer:
			return copyCurrentBlockToSystemClipboard(true, false, "No block marked.");

		case cmMrEditCutAndAppendToBuffer:
			return copyCurrentBlockToSystemClipboard(true, true, "Unable to cut and append block.");

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

		case cmMrSearchMultiFileSearch:
			return handleSearchMultiFileSearch();

		case cmMrSearchListFilesFromLastSearch:
			return handleSearchListFilesFromLastSearch();

		case cmMrSearchMultiFileSearchReplace:
			return handleSearchMultiFileSearchReplace();

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
			if (!promptBlockSavePath(savePath)) return true;
			return handleBlockAction(mrvmUiSaveBlockToFile(savePath), "Unable to save block.");
		}

		case cmMrBlockIndent:
			return handleBlockAction(mrvmUiIndentBlock(), "Unable to indent block.");

		case cmMrBlockUndent:
			return handleBlockAction(mrvmUiUndentBlock(), "Unable to undent block.");

		case cmMrBlockWindowCopy: {
			bool ok = false;
			int sourceWindowIndex = 0;
			if (!chooseInterWindowBlockTarget(sourceWindowIndex)) return true;
			if (sourceWindowIndex > 0) ok = mrvmUiWindowCopyBlock(sourceWindowIndex);
			return handleBlockAction(ok, "Inter-window block copy failed.");
		}

		case cmMrBlockWindowMove: {
			bool ok = false;
			int sourceWindowIndex = 0;
			if (!chooseInterWindowBlockTarget(sourceWindowIndex)) return true;
			if (sourceWindowIndex > 0) ok = mrvmUiWindowMoveBlock(sourceWindowIndex);
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
			MREditWindow *selected = mrShowWindowListDialog(mrwlManageWindows, currentEditWindow());
			if (selected != nullptr) mrScheduleWindowActivation(selected);
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

		case cmMrWindowMinimize:
			if (currentEditWindow() != nullptr) currentEditWindow()->isMinimized() ? currentEditWindow()->restoreWindow() : currentEditWindow()->minimizeWindow();
			return true;

		case cmMrWindowRestore:
			if (currentEditWindow() != nullptr && currentEditWindow()->isMinimized()) currentEditWindow()->restoreWindow();
			return true;

		case cmMrWindowCascade:
			return handleWindowCascade();

		case cmMrWindowTile:
			return handleWindowTile();

		case cmMrWindowNextDesktop:
			return viewportRight();

		case cmMrWindowPrevDesktop:
			return viewportLeft();

		case cmMrWindowMoveToNextDesktop:
			return moveToNextVirtualDesktop();

		case cmMrWindowMoveToPrevDesktop:
			return moveToPrevVirtualDesktop();

		case cmMrSetupEditSettings:
		case cmMrSetupColorSetup:
		case cmMrSetupKeyMapping:
		case cmMrSetupMouseKeyRepeat:
		case cmMrSetupFilenameExtensions:
		case cmMrSetupCompilerProfiles:
		case cmMrSetupPaths:
		case cmMrSetupBackupsAutosave:
		case cmMrSetupUserInterfaceSettings:
		case cmMrSetupLiveLogs:
		case cmMrSetupSearchAndReplaceDefaults: {
			if (runSetupDialogCommand(command)) return true;
			const char *title = placeholderCommandTitle(command);
			if (title != nullptr) {
				showPlaceholderCommandBox(title);
				return true;
			}
			return false;
		}

		case cmMrTextUpperCaseMenu:
			return dispatchEditorCommand(cmMrTextUpperCaseMenu, true);

		case cmMrTextLowerCaseMenu:
			return dispatchEditorCommand(cmMrTextLowerCaseMenu, true);

		case cmMrTextCenterLine:
			return dispatchEditorCommand(cmMrTextCenterLine, true);

		case cmMrTextReformatParagraph:
			return dispatchEditorCommand(cmMrTextReformatParagraph, true);

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

		case cmMrHelpAbout:
			showAboutDialog();
			return true;

		case cmMrOtherExecuteProgram:
			return handleExecuteProgram();

		case cmMrOtherBuildCurrentFile:
			return handleBuildCurrentFile();

		case cmMrOtherStopProgram:
			return handleStopCurrentProgram();

		case cmMrOtherRestartProgram:
			return handleRestartCurrentProgram();

		case cmMrOtherClearOutput:
			return handleClearCurrentOutput();

		case cmMrOtherMacroManager:
			return runMacroManagerDialog();

		case cmMrOtherAsciiTable:
			return handleCharacterTable(CharacterTableKind::Ascii);

		case cmMrOtherEmojiTable:
			return handleCharacterTable(CharacterTableKind::Emoji);

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

void clearTransientSearchSelectionOnUserInput(const TEvent &event) {
	clearTransientSelectionIfPending(event);
}
