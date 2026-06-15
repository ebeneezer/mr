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
#include <memory>
#include <optional>
#include <poll.h>
#include <sstream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unistd.h>

#include "../dialogs/MRFileInformation.hpp"
#include "../dialogs/MRAcquireDialog.hpp"
#include "../dialogs/MRAbout.hpp"
#include "../dialogs/MRPdfExportDialog.hpp"
#include "../dialogs/setup/MRSetup.hpp"
#include "../dialogs/setup/MRSetupCommon.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsRuntimeState.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#include "../app/utils/MRStringUtils.hpp"
#include "../app/services/MRLspAppService.hpp"
#include "../keymap/MRKeymapActionCatalog.hpp"
#include "../keymap/MRKeymapSequence.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../mrmac/MRVM.hpp"
#include "../mrmac/vm/MRVMEditor.hpp"
#include "../app/commands/MRExternalCommand.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../app/commands/MRLogViewer.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../diff/MRDiff.hpp"
#include "../dialogs/MRMacroFile.hpp"
#include "../dialogs/MRWindowList.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/MRMenuBar.hpp"
#include "../ui/MRBentoBox.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "MREditorApp.hpp"
#include "MRCommands.hpp"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

namespace {
bool startExternalCommandInWindow(MREditWindow *win, const std::string &commandLine, bool replaceBuffer, bool activate, bool closeOnFailure, std::string_view titleOverride = std::string_view(), const std::string &successAudioUri = std::string(), const std::string &failureAudioUri = std::string());

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
	BlockToggleVisibility = 6,
	CursorTopOfWindow = 7,
	CursorBottomOfWindow = 8,
	CursorStartOfBlock = 9,
	CursorEndOfBlock = 10,
	ViewCenterLine = 11
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
	ExtendBlockByMotion = 26,
	SearchResultsNext = 27,
	CompilerProblemsNext = 28,
	CompilerProblemsPrevious = 29,
	DisabledBlockAction = 30
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
    KeymapActionDispatchEntry{"MRMAC_BLOCK_TOGGLE_VISIBILITY", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::BlockToggleVisibility, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_UNDENT", KeymapDispatchKind::AppCommand, cmMrBlockUndent, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_INDENT", KeymapDispatchKind::AppCommand, cmMrBlockIndent, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_COPY", KeymapDispatchKind::AppCommand, cmMrBlockCopy, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_MOVE", KeymapDispatchKind::AppCommand, cmMrBlockMove, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_DELETE", KeymapDispatchKind::AppCommand, cmMrBlockDelete, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_COPY_INTERWINDOW", KeymapDispatchKind::AppCommand, cmMrBlockWindowCopy, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_MOVE_INTERWINDOW", KeymapDispatchKind::AppCommand, cmMrBlockWindowMove, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_MOVE_TO_BUFFER", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::DisabledBlockAction},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_APPEND_TO_BUFFER", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::DisabledBlockAction},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_CUT_APPEND_TO_BUFFER", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::DisabledBlockAction},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_COPY_FROM_BUFFER", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::DisabledBlockAction},
    KeymapActionDispatchEntry{"MRMAC_BLOCK_EXTEND_BY_MOTION", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::DisabledBlockAction},
    KeymapActionDispatchEntry{"MRMAC_FILE_SAVE", KeymapDispatchKind::AppCommand, cmMrFileSave, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_BLOCK_MARK_LINES", KeymapDispatchKind::AppCommand, cmMrBlockMarkLines, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_BLOCK_TOGGLE_MARKING", KeymapDispatchKind::AppCommand, cmMrBlockToggleMarking, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_BLOCK_TOGGLE_VISIBILITY", KeymapDispatchKind::AppCommand, cmMrBlockToggleVisibility, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_FILE_SAVE_ALL", KeymapDispatchKind::AppCommand, cmMrFileSaveAll, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_FILE_REVERT", KeymapDispatchKind::AppCommand, cmMrFileRevert, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SAVE_BLOCK_TO_FILE", KeymapDispatchKind::AppCommand, cmMrBlockSaveToDisk, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_LOAD_BLOCK_FROM_FILE", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::LoadBlockFromFile},
    KeymapActionDispatchEntry{"MR_TEXT_CENTER_LINE", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::CenterLine},
    KeymapActionDispatchEntry{"MR_TEXT_FILE_COMPARE", KeymapDispatchKind::AppCommand, cmMrTextFileCompare, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_EDIT_TOGGLE_INSERT_MODE", KeymapDispatchKind::AppCommand, cmMrEditToggleInsertMode, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_TEXT_REFORMAT_PARAGRAPH", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ReformatParagraph},
    KeymapActionDispatchEntry{"MR_TEXT_REFORMAT_DOCUMENT", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ReformatDocument},
    KeymapActionDispatchEntry{"MR_TOGGLE_FORMAT_RULER", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ToggleFormatRuler},
    KeymapActionDispatchEntry{"MR_TOGGLE_WORD_WRAP", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ToggleWordWrap},
    KeymapActionDispatchEntry{"MR_SET_LEFT_MARGIN", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SetLeftMargin},
    KeymapActionDispatchEntry{"MR_SET_RIGHT_MARGIN", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SetRightMargin},
    KeymapActionDispatchEntry{"MR_JUSTIFY_PARAGRAPH", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::JustifyParagraph},
    KeymapActionDispatchEntry{"MR_SORT_COLUMN_BLOCK_TOGGLE", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::DisabledBlockAction},
    KeymapActionDispatchEntry{"MR_FILE_FORCE_SAVE", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ForceSave},
    KeymapActionDispatchEntry{"MR_EXIT_DIRTY_SAVE_ALL", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ExitDirtySaveAll},
    KeymapActionDispatchEntry{"MR_WINDOW_OPEN", KeymapDispatchKind::AppCommand, cmMrWindowOpen, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_WINDOW_NEXT", KeymapDispatchKind::AppCommand, cmMrWindowNext, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_WINDOW_PREVIOUS", KeymapDispatchKind::AppCommand, cmMrWindowPrevious, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_WINDOW_LIST", KeymapDispatchKind::AppCommand, cmMrWindowList, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_WINDOW_CLOSE", KeymapDispatchKind::AppCommand, cmMrWindowClose, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_WINDOW_ZOOM", KeymapDispatchKind::AppCommand, cmMrWindowZoom, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_WINDOW_CASCADE", KeymapDispatchKind::AppCommand, cmMrWindowCascade, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_WINDOW_TILE", KeymapDispatchKind::AppCommand, cmMrWindowTile, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_WINDOW_SPLIT_VERTICAL", KeymapDispatchKind::AppCommand, cmMrWindowSplitVertical, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_WINDOW_SPLIT_HORIZONTAL", KeymapDispatchKind::AppCommand, cmMrWindowSplitHorizontal, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_DESKTOP_VIEWPORT_LEFT", KeymapDispatchKind::AppCommand, cmMrWindowPrevDesktop, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_DESKTOP_VIEWPORT_RIGHT", KeymapDispatchKind::AppCommand, cmMrWindowNextDesktop, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_DESKTOP_MOVE_WINDOW_LEFT", KeymapDispatchKind::AppCommand, cmMrWindowMoveToPrevDesktop, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_DESKTOP_MOVE_WINDOW_RIGHT", KeymapDispatchKind::AppCommand, cmMrWindowMoveToNextDesktop, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_BUILD_CURRENT_FILE", KeymapDispatchKind::AppCommand, cmMrOtherBuildCurrentFile, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SEARCH_RESULTS_NEXT", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SearchResultsNext},
    KeymapActionDispatchEntry{"MR_SEARCH_MULTI_FILE_REPLACE", KeymapDispatchKind::AppCommand, cmMrSearchMultiFileSearchReplace, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_COMPILER_PROBLEMS_NEXT", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::CompilerProblemsNext},
    KeymapActionDispatchEntry{"MR_COMPILER_PROBLEMS_PREVIOUS", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::CompilerProblemsPrevious},
    KeymapActionDispatchEntry{"MR_MATCH_PARENTHESIS", KeymapDispatchKind::AppCommand, cmMrOtherMatchBraceOrParen, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_MACRO_TOGGLE_RECORDING", KeymapDispatchKind::AppCommand, cmMrMacroToggleRecording, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_EDIT_SETTINGS", KeymapDispatchKind::AppCommand, cmMrSetupEditSettings, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_COLOR", KeymapDispatchKind::AppCommand, cmMrSetupColorSetup, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_KEYMAP", KeymapDispatchKind::AppCommand, cmMrSetupKeyMapping, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_MOUSE_KEY_REPEAT", KeymapDispatchKind::AppCommand, cmMrSetupMouseKeyRepeat, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_FILENAME_EXTENSIONS", KeymapDispatchKind::AppCommand, cmMrSetupFilenameExtensions, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_COMPILER_PROFILES", KeymapDispatchKind::AppCommand, cmMrSetupCompilerProfiles, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_PATHS", KeymapDispatchKind::AppCommand, cmMrSetupPaths, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_BACKUPS_AUTOSAVE", KeymapDispatchKind::AppCommand, cmMrSetupBackupsAutosave, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_SEARCH_REPLACE_DEFAULTS", KeymapDispatchKind::AppCommand, cmMrSetupSearchAndReplaceDefaults, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_USER_INTERFACE", KeymapDispatchKind::AppCommand, cmMrSetupUserInterfaceSettings, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_LIVE_LOGS", KeymapDispatchKind::AppCommand, cmMrSetupLiveLogs, KeymapWindowMethod::None, KeymapCustomAction::None},
};

mr::services::MRLspAppService g_lspAppService;
std::size_t g_lspReportedDiagnosticCount = 0;
std::size_t g_lspReportedLocationCount = 0;
std::size_t g_lspReportedHoverCount = 0;
std::size_t g_lspReportedCompletionCount = 0;
std::size_t g_lspRequestCount = 0;
std::size_t g_lspRequestFailureCount = 0;
std::size_t g_lspPollFailureCount = 0;
std::string g_lspLastRequestLabel;
std::string g_lspLastRequestPath;
std::string g_lspLastRequestPosition;
std::string g_lspLastRequestState = "idle";
std::string g_lspLastServerExecutable;
std::string g_lspLastServerWorkingDirectory;
std::string g_lspLastServerArguments;
std::string g_lspLastServerConfigurationSource;
std::string g_lspLastError;
std::string g_lspLastPollError;

const char *placeholderCommandTitle(ushort command) {
	switch (command) {
		case cmMrFileOpen:
			return "File / Open";
		case cmMrFileLoad:
			return "File / Load";
		case cmMrFileOpenWorkspace:
			return "File / Open Workspace";
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
		case cmMrEditToggleInsertMode:
			return "Edit / Insert";
		case cmMrWindowClose:
			return "Window / Close";
		case cmMrWindowSplitHorizontal:
			return "Window / Split horizontal";
		case cmMrWindowSplitVertical:
			return "Window / Split vertical";
		case cmMrWindowList:
			return "Window / List";
		case cmMrWindowNext:
			return "Window / Next";
		case cmMrWindowPrevious:
			return "Window / Previous";
		case cmMrWindowHide:
			return "Window / Hide";
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
		case cmMrBlockToggleVisibility:
			return "Block / Hide/show block mark";
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
		case cmMrTextFileCompare:
			return "Text / File compare";
		case cmMrFileCompareApplyOriginalToCompare:
			return "Text / Apply original to compare";
		case cmMrFileCompareApplyCompareToOriginal:
			return "Text / Apply compare to original";
		case cmMrTextUpperCasePlaceholder:
			return "Text / Upper case";
		case cmMrTextLowerCasePlaceholder:
			return "Text / Lower case";

		case cmMrOtherMacroManager:
			return "Other / Macro manager";
		case cmMrOtherBuildCurrentFile:
			return "Other / Build current file";
		case cmMrOtherLspDefinition:
			return "Other / LSP definition";
		case cmMrOtherLspReferences:
			return "Other / LSP references";
		case cmMrOtherLspHover:
			return "Other / LSP hover";
		case cmMrOtherLspComplete:
			return "Other / LSP complete";
		case cmMrOtherLspStatus:
			return "Other / LSP status";
		case cmMrOtherLspResults:
			return "Other / LSP results";
		case cmMrOtherStopProgram:
			return "Other / Stop current program";
		case cmMrOtherRestartProgram:
			return "Other / Restart current program";
		case cmMrOtherClearOutput:
			return "Other / Clear current output";
		case cmMrOtherFindNextCompilerError:
			return "Other / Next compiler error";
		case cmMrOtherFindPreviousCompilerError:
			return "Other / Previous compiler error";
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

MRBentoBox *compilerProblemsBentoBoxForWindow(MREditWindow *targetWindow) {
	MREditWindow *window = effectiveKeymapWindow(targetWindow);

	if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window)) return bentoBox;
	return window != nullptr ? dynamic_cast<MRBentoBox *>(window->owner) : nullptr;
}

bool dispatchEditorCommandEvent(MREditWindow *targetWindow, ushort command) {
	MREditWindow *window = effectiveKeymapWindow(targetWindow);
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	TEvent event;

	if (editor == nullptr) return false;
	const bool trackFileCompareMutation = command == cmUndo || command == cmMrEditUndo || command == cmMrEditRedo;
	const std::size_t versionBefore = trackFileCompareMutation ? editor->documentVersion() : 0;
	std::memset(&event, 0, sizeof(event));
	event.what = evCommand;
	event.message.command = command;
	editor->handleEvent(event);
	if (trackFileCompareMutation && editor->documentVersion() != versionBefore) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);
		if (bentoBox == nullptr && window != nullptr) bentoBox = dynamic_cast<MRBentoBox *>(window->owner);
		if (bentoBox != nullptr) static_cast<void>(bentoBox->refreshFileCompareAfterEditorMutation(window));
	}
	if (window->hasBlock() && !window->isBlockMarking()) window->refreshBlockVisual();
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

bool runDisabledBlockAction();

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
			window->clearBlock();
			return true;
		case KeymapWindowMethod::BlockToggleVisibility:
			return window->toggleBlockVisibility();
		case KeymapWindowMethod::BlockMarkWordRight:
			return window->markWordRight();
		case KeymapWindowMethod::CursorStartOfBlock:
			return window->moveCursorToBlockStart();
		case KeymapWindowMethod::CursorEndOfBlock:
			return window->moveCursorToBlockEnd();
		case KeymapWindowMethod::CursorTopOfWindow:
			return window->moveCursorToTopOfView();
		case KeymapWindowMethod::CursorBottomOfWindow:
			return window->moveCursorToBottomOfView();
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

constexpr const char *kWindowReadOnlyMessage = "Window is read-only.";
constexpr const char *kNoExternalProgramTaskMessage = "No external program task is running in this window.";
constexpr const char *kStopProgramBeforeRestartMessage = "Stop the current program before restarting it.";
constexpr const char *kNoRestartableCommandMessage = "No restartable command is associated with this window.";

void postSearchError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, std::string(text), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

void postDialogWarning(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string(text), mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
}

bool runDisabledBlockAction() {
	postDialogWarning("Block commands are disabled.");
	return true;
}

void postDialogError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string(text), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

void postLspInfo(const std::string &text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, text, mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
	mrLogMessage(text);
}

void postLspWarning(const std::string &text) {
	postDialogWarning(text);
	mrLogMessage(text);
}

void postLspError(const std::string &text) {
	postDialogError(text);
	mrLogMessage(text);
}

std::string firstDisplayLine(const std::string &text, std::size_t maxLength) {
	std::string result;

	for (std::size_t i = 0; i < text.size() && result.size() < maxLength; ++i) {
		const char ch = text[i];
		if (ch == '\r' || ch == '\n') break;
		result.push_back(ch);
	}
	if (text.size() > result.size() && result.size() == maxLength) result += "...";
	return result;
}

bool buildLspServerProfileFromEnvironment(mr::services::MRLspServerProfile &profile) {
	const char *server = std::getenv("MR_LSP_SERVER");
	const char *arguments = std::getenv("MR_LSP_SERVER_ARGS");
	std::istringstream argumentStream(arguments != nullptr ? arguments : "");
	std::string argument;

	profile = mr::services::MRLspServerProfile();
	if (server == nullptr || trimAscii(server).empty()) return false;
	profile.profileName = "environment";
	profile.executablePath = trimAscii(server);
	profile.workingDirectory = ".";
	while (argumentStream >> argument)
		profile.arguments.push_back(argument);
	return true;
}

struct LspServerStartEntry {
	MRSyntaxLanguage language;
	const char *profileName;
	const char *executableName;
	const char *argument1;
	const char *argument2;
};

const LspServerStartEntry lspServerStartTable[] = {
	{ MRSyntaxLanguage::C, "builtin-c-clangd", "clangd", nullptr, nullptr },
	{ MRSyntaxLanguage::Cpp, "builtin-cpp-clangd", "clangd", nullptr, nullptr },
};

bool isExecutableFile(const std::string &path) {
	return !path.empty() && ::access(path.c_str(), X_OK) == 0;
}

bool executableNameHasPath(const std::string &name) {
	return name.find('/') != std::string::npos;
}

bool findExecutableOnPath(const std::string &executableName, std::string &resolvedPath) {
	const char *pathEnvironment = std::getenv("PATH");
	std::string pathText = pathEnvironment != nullptr ? pathEnvironment : "";
	std::size_t start = 0;

	resolvedPath.clear();
	if (executableName.empty()) return false;
	if (executableNameHasPath(executableName)) {
		if (!isExecutableFile(executableName)) return false;
		resolvedPath = executableName;
		return true;
	}

	while (start <= pathText.size()) {
		std::size_t end = pathText.find(':', start);
		std::string directory;
		std::string candidate;

		if (end == std::string::npos) end = pathText.size();
		directory = pathText.substr(start, end - start);
		if (directory.empty()) directory = ".";
		candidate = directory;
		if (!candidate.empty() && candidate[candidate.size() - 1] != '/') candidate += "/";
		candidate += executableName;
		if (isExecutableFile(candidate)) {
			resolvedPath = candidate;
			return true;
		}
		if (end == pathText.size()) break;
		start = end + 1;
	}
	return false;
}

const LspServerStartEntry *findBuiltInLspServerStartEntry(MRSyntaxLanguage language) {
	const std::size_t entryCount = sizeof(lspServerStartTable) / sizeof(lspServerStartTable[0]);

	for (std::size_t index = 0; index < entryCount; ++index) {
		if (lspServerStartTable[index].language == language) return &lspServerStartTable[index];
	}
	return nullptr;
}

void appendLspServerStartArguments(mr::services::MRLspServerProfile &profile, const LspServerStartEntry &entry) {
	if (entry.argument1 != nullptr && entry.argument1[0] != '\0') profile.arguments.push_back(entry.argument1);
	if (entry.argument2 != nullptr && entry.argument2[0] != '\0') profile.arguments.push_back(entry.argument2);
}

bool buildLspServerProfileFromEditor(const MRFileEditor &editor, mr::services::MRLspServerProfile &profile, std::string &configurationSource, std::string &errorMessage) {
	const LspServerStartEntry *entry = nullptr;
	std::string executablePath;

	profile = mr::services::MRLspServerProfile();
	configurationSource.clear();
	errorMessage.clear();

	if (buildLspServerProfileFromEnvironment(profile)) {
		configurationSource = "MR_LSP_SERVER";
		return true;
	}

	entry = findBuiltInLspServerStartEntry(editor.syntaxLanguage());
	if (entry == nullptr) {
		errorMessage = std::string("No built-in LSP server for language ") + editor.syntaxLanguageName() + ". Set MR_LSP_SERVER.";
		return false;
	}
	if (!findExecutableOnPath(entry->executableName, executablePath)) {
		errorMessage = std::string("Built-in LSP server for language ") + editor.syntaxLanguageName() + " is not executable or not in PATH: " + entry->executableName;
		return false;
	}

	profile.profileName = entry->profileName;
	profile.executablePath = executablePath;
	appendLspServerStartArguments(profile, *entry);
	profile.workingDirectory.clear();
	configurationSource = std::string("built-in language mapping: ") + editor.syntaxLanguageName();
	return true;
}

std::string lspServerProfileArgumentText(const mr::services::MRLspServerProfile &profile) {
	std::ostringstream argumentText;

	for (std::size_t i = 0; i < profile.arguments.size(); ++i) {
		if (i != 0) argumentText << " ";
		argumentText << profile.arguments[i];
	}
	return argumentText.str();
}

bool buildCurrentLspDocumentSnapshot(MREditWindow *win, mr::services::MRWorkspaceDocumentSnapshot &document) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

	if (win == nullptr || editor == nullptr) {
		postLspWarning("LSP requires an editor window.");
		return false;
	}
	if (!editor->hasPersistentFileName()) {
		postLspWarning("LSP requires a saved file.");
		return false;
	}
	document = mr::services::MRWorkspaceDocumentSnapshot();
	document.bufferId = win->bufferId();
	document.documentId = editor->documentId();
	document.documentVersion = editor->documentVersion();
	document.path = editor->persistentFileName();
	document.languageName = editor->syntaxLanguageName();
	document.mainFile = true;
	return true;
}

mr::lsp::LspTextPosition currentLspTextPosition(const MRFileEditor &editor) {
	const std::size_t offset = editor.cursorOffset();
	mr::lsp::LspTextPosition position;

	position.line = static_cast<int>(editor.lineIndexOfOffset(offset));
	position.character = static_cast<int>(editor.columnOfOffset(offset));
	return position;
}

bool requestLspEditorCommand(mr::services::MRLspServiceCommandId command, const char *label) {
	MREditWindow *win = currentEditorCommandWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	mr::services::MRLspServerProfile profile;
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::lsp::LspTextPosition position;
	std::string errorMessage;
	std::string configurationSource;
	std::ostringstream positionText;

	++g_lspRequestCount;
	g_lspLastRequestLabel = label != nullptr ? label : "LSP request";
	g_lspLastRequestPath.clear();
	g_lspLastRequestPosition.clear();
	g_lspLastRequestState = "preparing";
	g_lspLastError.clear();
	g_lspLastPollError.clear();
	if (!buildCurrentLspDocumentSnapshot(win, document)) {
		g_lspLastRequestState = "failed";
		g_lspLastError = "LSP request requires a saved editor document.";
		++g_lspRequestFailureCount;
		return true;
	}
	if (editor == nullptr || !buildLspServerProfileFromEditor(*editor, profile, configurationSource, errorMessage)) {
		g_lspLastRequestState = "failed";
		g_lspLastError = errorMessage.empty() ? "LSP server not configured." : errorMessage;
		++g_lspRequestFailureCount;
		postLspWarning(g_lspLastError);
		return true;
	}
	g_lspLastServerExecutable = profile.executablePath;
	g_lspLastServerWorkingDirectory = profile.workingDirectory;
	g_lspLastServerArguments = lspServerProfileArgumentText(profile);
	g_lspLastServerConfigurationSource = configurationSource;
	position = currentLspTextPosition(*editor);
	positionText << (position.line + 1) << ":" << (position.character + 1);
	g_lspLastRequestPath = document.path;
	g_lspLastRequestPosition = positionText.str();
	g_lspAppService.setMainFileByBufferId(document.bufferId);
	if (!g_lspAppService.requestCurrentEditorCommand(profile, document, *editor, command, position, errorMessage)) {
		g_lspLastRequestState = "failed";
		g_lspLastError = errorMessage;
		++g_lspRequestFailureCount;
		postLspError(g_lspLastRequestLabel + " failed: " + errorMessage);
		return true;
	}
	g_lspLastRequestState = "requested";
	postLspInfo(g_lspLastRequestLabel + " requested.");
	return true;
}

bool syncCurrentEditorForLspResults() {
	MREditWindow *win = currentEditorCommandWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	mr::services::MRLspServerProfile profile;
	mr::services::MRWorkspaceDocumentSnapshot document;
	std::string errorMessage;
	std::string configurationSource;

	if (win == nullptr || editor == nullptr) return true;
	if (!buildCurrentLspDocumentSnapshot(win, document)) return false;
	if (!buildLspServerProfileFromEditor(*editor, profile, configurationSource, errorMessage)) {
		postLspWarning(errorMessage.empty() ? "LSP server not configured." : errorMessage);
		return false;
	}

	g_lspLastServerExecutable = profile.executablePath;
	g_lspLastServerWorkingDirectory = profile.workingDirectory;
	g_lspLastServerArguments = lspServerProfileArgumentText(profile);
	g_lspLastServerConfigurationSource = configurationSource;
	g_lspLastRequestPath = document.path;
	g_lspAppService.setMainFileByBufferId(document.bufferId);
	if (!g_lspAppService.syncCurrentEditorDocument(profile, document, *editor, errorMessage)) {
		postLspError("LSP results sync failed: " + errorMessage);
		return false;
	}
	for (int i = 0; i < 25; ++i) {
		if (!g_lspAppService.poll(errorMessage)) {
			postLspError("LSP poll failed: " + errorMessage);
			g_lspAppService.close();
			return false;
		}
		::poll(nullptr, 0, 20);
	}
	return true;
}

MREditWindow *findOpenLspTargetWindow(const std::string &path) {
	const std::string normalizedTarget = mr::services::normalizeWorkspaceServicePath(path);
	const std::vector<MREditWindow *> windows = allEditWindowsInZOrder();

	for (MREditWindow *window : windows) {
		MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;

		if (editor == nullptr || !editor->hasPersistentFileName()) continue;
		if (mr::services::normalizeWorkspaceServicePath(editor->persistentFileName()) == normalizedTarget) return window;
	}
	return nullptr;
}

bool navigateToLspLocation(const mr::services::MRServiceLocationTarget &target, std::string &errorMessage) {
	MREditWindow *window = nullptr;
	MRFileEditor *editor = nullptr;
	std::size_t line = 0;
	int column = 0;

	if (target.path.empty()) {
		errorMessage = "LSP target path is empty.";
		return false;
	}

	window = findOpenLspTargetWindow(target.path);
	if (window == nullptr) {
		window = createEditorWindow(target.path.c_str());
		if (window == nullptr) {
			errorMessage = "Unable to create editor window.";
			return false;
		}
		if (!loadResolvedFileIntoWindow(window, target.path, "LSP definition load")) {
			message(window, evCommand, cmClose, nullptr);
			errorMessage = "Unable to load LSP target: " + target.path;
			return false;
		}
	}

	editor = window->getEditor();
	if (editor == nullptr) {
		errorMessage = "LSP target window has no editor.";
		return false;
	}

	if (target.range.start.line > 0) line = static_cast<std::size_t>(target.range.start.line);
	if (target.range.start.character > 0) column = target.range.start.character;
	editor->moveCursorToDocumentLineTop(line, column);
	editor->revealCursor(True);
	static_cast<void>(mrActivateEditWindow(window));
	errorMessage.clear();
	return true;
}

std::string lspLocationDisplayText(const mr::services::MRServiceLocationTarget &target) {
	std::ostringstream line;

	line << (target.range.start.line + 1) << ":" << (target.range.start.character + 1) << " ";
	line << (!target.path.empty() ? target.path : target.uri);
	return line.str();
}

class LspReferencesListView final : public TListViewer {
  public:
	LspReferencesListView(const TRect &bounds, TScrollBar *scrollBar, const std::vector<mr::services::MRServiceLocationTarget> &locations) noexcept : TListViewer(bounds, 1, nullptr, scrollBar), locations(locations) {
		const std::size_t visibleCount = std::min<std::size_t>(locations.size(), static_cast<std::size_t>(std::numeric_limits<short>::max()));

		setRange(static_cast<short>(visibleCount));
	}

	void getText(char *dest, short item, short maxLen) override {
		std::string text;

		if (dest == nullptr || maxLen <= 0) return;
		if (item < 0 || static_cast<std::size_t>(item) >= locations.size()) {
			dest[0] = EOS;
			return;
		}

		text = firstDisplayLine(lspLocationDisplayText(locations[static_cast<std::size_t>(item)]), static_cast<std::size_t>(std::max<short>(0, maxLen - 1)));
		std::strncpy(dest, text.c_str(), static_cast<std::size_t>(maxLen - 1));
		dest[maxLen - 1] = EOS;
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter) {
			TView *target = owner != nullptr && owner->owner != nullptr ? owner->owner : owner;
			if (target != nullptr) message(target, evCommand, cmOK, this);
			clearEvent(event);
			return;
		}
		TListViewer::handleEvent(event);
	}

	void selectItem(short item) override {
		if (item >= 0 && static_cast<std::size_t>(item) < locations.size()) {
			TView *target = owner != nullptr && owner->owner != nullptr ? owner->owner : owner;
			focusItemNum(item);
			if (target != nullptr) message(target, evCommand, cmOK, this);
		}
	}

	[[nodiscard]] bool selectedIndex(std::size_t &index) const noexcept {
		if (focused < 0 || static_cast<std::size_t>(focused) >= locations.size()) return false;
		index = static_cast<std::size_t>(focused);
		return true;
	}

  private:
	const std::vector<mr::services::MRServiceLocationTarget> &locations;
};

enum class LspResultDialogAction : unsigned char {
	None = 0,
	NavigateLocation,
	ShowHover
};

struct LspResultDialogRow {
	std::string text;
	LspResultDialogAction action = LspResultDialogAction::None;
	mr::services::MRServiceLocationTarget location;
	mr::services::MRServiceHoverResult hover;
};

const char *lspResultStateText(mr::services::MRServiceResultState state) noexcept {
	switch (state) {
		case mr::services::MRServiceResultState::Current:
			return "current";
		case mr::services::MRServiceResultState::Stale:
			return "stale";
		case mr::services::MRServiceResultState::Rejected:
			return "rejected";
		case mr::services::MRServiceResultState::Error:
			return "error";
	}
	return "unknown";
}

const char *lspDiagnosticSeverityText(int severity) noexcept {
	switch (severity) {
		case 1:
			return "error";
		case 2:
			return "warning";
		case 3:
			return "info";
		case 4:
			return "hint";
	}
	return "diagnostic";
}

class LspResultsListView final : public TListViewer {
  public:
	LspResultsListView(const TRect &bounds, TScrollBar *scrollBar, const std::vector<LspResultDialogRow> &rows) noexcept : TListViewer(bounds, 1, nullptr, scrollBar), rows(rows) {
		const std::size_t visibleCount = std::min<std::size_t>(rows.size(), static_cast<std::size_t>(std::numeric_limits<short>::max()));

		setRange(static_cast<short>(visibleCount));
	}

	void getText(char *dest, short item, short maxLen) override {
		std::string text;

		if (dest == nullptr || maxLen <= 0) return;
		if (item < 0 || static_cast<std::size_t>(item) >= rows.size()) {
			dest[0] = EOS;
			return;
		}

		text = firstDisplayLine(rows[static_cast<std::size_t>(item)].text, static_cast<std::size_t>(std::max<short>(0, maxLen - 1)));
		std::strncpy(dest, text.c_str(), static_cast<std::size_t>(maxLen - 1));
		dest[maxLen - 1] = EOS;
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter) {
			TView *target = owner != nullptr && owner->owner != nullptr ? owner->owner : owner;
			if (target != nullptr) message(target, evCommand, cmOK, this);
			clearEvent(event);
			return;
		}
		TListViewer::handleEvent(event);
	}

	void selectItem(short item) override {
		if (item >= 0 && static_cast<std::size_t>(item) < rows.size()) {
			TView *target = owner != nullptr && owner->owner != nullptr ? owner->owner : owner;
			focusItemNum(item);
			if (target != nullptr) message(target, evCommand, cmOK, this);
		}
	}

	[[nodiscard]] bool selectedIndex(std::size_t &index) const noexcept {
		if (focused < 0 || static_cast<std::size_t>(focused) >= rows.size()) return false;
		index = static_cast<std::size_t>(focused);
		return true;
	}

  private:
	const std::vector<LspResultDialogRow> &rows;
};

class LspCompletionListView final : public TListViewer {
  public:
	LspCompletionListView(const TRect &bounds, TScrollBar *scrollBar, const std::vector<mr::services::MRServiceCompletionItem> &items) noexcept : TListViewer(bounds, 1, nullptr, scrollBar), items(items) {
		const std::size_t visibleCount = std::min<std::size_t>(items.size(), static_cast<std::size_t>(std::numeric_limits<short>::max()));

		setRange(static_cast<short>(visibleCount));
	}

	void getText(char *dest, short item, short maxLen) override {
		std::string text;
		const mr::services::MRServiceCompletionItem *completionItem = nullptr;

		if (dest == nullptr || maxLen <= 0) return;
		if (item < 0 || static_cast<std::size_t>(item) >= items.size()) {
			dest[0] = EOS;
			return;
		}

		completionItem = &items[static_cast<std::size_t>(item)];
		text = completionItem->label;
		if (!completionItem->detail.empty()) text += " - " + completionItem->detail;
		text = firstDisplayLine(text, static_cast<std::size_t>(std::max<short>(0, maxLen - 1)));
		std::strncpy(dest, text.c_str(), static_cast<std::size_t>(maxLen - 1));
		dest[maxLen - 1] = EOS;
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter) {
			TView *target = owner != nullptr && owner->owner != nullptr ? owner->owner : owner;
			if (target != nullptr) message(target, evCommand, cmOK, this);
			clearEvent(event);
			return;
		}
		TListViewer::handleEvent(event);
	}

	void selectItem(short item) override {
		if (item >= 0 && static_cast<std::size_t>(item) < items.size()) {
			TView *target = owner != nullptr && owner->owner != nullptr ? owner->owner : owner;
			focusItemNum(item);
			if (target != nullptr) message(target, evCommand, cmOK, this);
		}
	}

	[[nodiscard]] bool selectedIndex(std::size_t &index) const noexcept {
		if (focused < 0 || static_cast<std::size_t>(focused) >= items.size()) return false;
		index = static_cast<std::size_t>(focused);
		return true;
	}

  private:
	const std::vector<mr::services::MRServiceCompletionItem> &items;
};

bool showLspReferencesDialog(const mr::services::MRServiceLocationResult &result) {
	MRDialogFoundation *dialog = nullptr;
	TScrollBar *scrollBar = nullptr;
	LspReferencesListView *listView = nullptr;
	ushort dialogResult = cmCancel;
	std::size_t selectedIndex = 0;
	mr::services::MRServiceLocationTarget target;
	std::string navigationError;
	const int visibleRows = std::max<int>(4, std::min<int>(static_cast<int>(result.locations.size()), 12));
	const short width = 96;
	const short height = static_cast<short>(visibleRows + 6);
	const short buttonY = static_cast<short>(height - 3);

	if (TProgram::deskTop == nullptr) return false;
	if (result.locations.empty()) {
		postLspWarning("LSP references: no references found.");
		return true;
	}

	dialog = mr::dialogs::createScrollableDialog("LSP REFERENCES", width, height);
	dialog->insert(new TStaticText(TRect(2, 1, width - 2, 2), "Select reference:"));
	scrollBar = new TScrollBar(TRect(width - 3, 2, width - 2, height - 4));
	dialog->insert(scrollBar);
	listView = new LspReferencesListView(TRect(2, 2, width - 3, height - 4), scrollBar, result.locations);
	dialog->insert(listView);
	{
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~G~o", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 1);
		mr::dialogs::insertUniformButtonRow(*dialog, (width - metrics.rowWidth) / 2, buttonY, 1, buttons);
	}
	dialog->setDialogValidationHook([listView]() {
		MRScrollableDialog::DialogValidationResult validation;
		std::size_t selected = 0;

		validation.valid = listView != nullptr && listView->selectedIndex(selected);
		if (!validation.valid) validation.warningText = "Select a reference.";
		return validation;
	});
	dialog->finalizeLayout();
	dialogResult = TProgram::deskTop->execView(dialog);
	if (dialogResult == cmOK && listView != nullptr && listView->selectedIndex(selectedIndex) && selectedIndex < result.locations.size()) target = result.locations[selectedIndex];
	else
		target = mr::services::MRServiceLocationTarget();
	TObject::destroy(dialog);

	if (target.path.empty() && target.uri.empty()) return true;
	if (!navigateToLspLocation(target, navigationError)) postLspWarning("LSP references navigation failed: " + navigationError);
	return true;
}

std::vector<std::string> buildLspHoverDisplayLines(const std::string &text, std::size_t width, std::size_t maxLines) {
	std::vector<std::string> lines;
	std::string sanitized;
	std::size_t paragraphStart = 0;
	bool truncated = false;

	if (width == 0 || maxLines == 0) return lines;
	sanitized.reserve(text.size());
	for (char ch : text) {
		unsigned char uch = static_cast<unsigned char>(ch);

		if (ch == '\r') continue;
		if (ch == '\t') sanitized.push_back(' ');
		else if (ch == '\n')
			sanitized.push_back('\n');
		else if (uch < 32)
			sanitized.push_back(' ');
		else
			sanitized.push_back(ch);
	}

	while (paragraphStart <= sanitized.size() && lines.size() < maxLines) {
		std::size_t paragraphEnd = sanitized.find('\n', paragraphStart);
		std::string paragraph;

		if (paragraphEnd == std::string::npos) paragraphEnd = sanitized.size();
		paragraph = sanitized.substr(paragraphStart, paragraphEnd - paragraphStart);
		while (!paragraph.empty() && paragraph.front() == ' ')
			paragraph.erase(paragraph.begin());
		while (!paragraph.empty() && lines.size() < maxLines) {
			std::size_t take = std::min(width, paragraph.size());
			std::size_t breakPos = paragraph.rfind(' ', take);

			if (take < paragraph.size() && breakPos != std::string::npos && breakPos > 0) take = breakPos;
			lines.push_back(paragraph.substr(0, take));
			paragraph.erase(0, take);
			while (!paragraph.empty() && paragraph.front() == ' ')
				paragraph.erase(paragraph.begin());
		}
		if (paragraph.empty() && paragraphStart < sanitized.size() && lines.size() < maxLines && paragraphEnd == paragraphStart) lines.push_back(std::string());
		if (paragraphEnd == sanitized.size()) break;
		paragraphStart = paragraphEnd + 1;
	}
	if (paragraphStart < sanitized.size() && lines.size() >= maxLines) truncated = true;
	if (truncated && !lines.empty()) {
		std::string &last = lines.back();

		if (last.size() + 3 <= width) last += "...";
		else if (width >= 3)
			last = last.substr(0, width - 3) + "...";
	}
	return lines;
}

bool showLspHoverDialog(const mr::services::MRServiceHoverResult &result) {
	MRDialogFoundation *dialog = nullptr;
	ushort dialogResult = cmCancel;
	std::vector<std::string> lines;
	const std::size_t textWidth = 76;
	const std::size_t maxLines = 16;
	short width = 82;
	short height = 0;
	short buttonY = 0;

	if (TProgram::deskTop == nullptr) return false;
	lines = buildLspHoverDisplayLines(result.hover.value, textWidth, maxLines);
	if (lines.empty()) {
		postLspWarning("LSP hover: empty hover.");
		return true;
	}

	height = static_cast<short>(lines.size() + 5);
	buttonY = static_cast<short>(height - 3);
	dialog = mr::dialogs::createScrollableDialog("LSP HOVER", width, height);
	for (std::size_t i = 0; i < lines.size(); ++i) {
		const std::string &line = lines[i];

		dialog->insert(new TStaticText(TRect(3, static_cast<short>(2 + i), width - 3, static_cast<short>(3 + i)), line.c_str()));
	}
	{
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 0);
		mr::dialogs::insertUniformButtonRow(*dialog, (width - metrics.rowWidth) / 2, buttonY, 0, buttons);
	}
	dialog->finalizeLayout();
	dialogResult = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
	return dialogResult == cmOK || dialogResult == cmCancel;
}

MREditWindow *findLspCompletionTargetWindow(const mr::services::MRServiceCompletionResult &result) {
	MREditWindow *window = nullptr;

	if (result.header.identity.bufferId != 0) {
		window = findEditWindowByBufferId(result.header.identity.bufferId);
		if (window != nullptr) return window;
	}
	if (!result.header.identity.path.empty()) return findOpenLspTargetWindow(result.header.identity.path);
	return nullptr;
}

bool showLspCompletionDialog(const mr::services::MRServiceCompletionResult &result) {
	MRDialogFoundation *dialog = nullptr;
	TScrollBar *scrollBar = nullptr;
	LspCompletionListView *listView = nullptr;
	MREditWindow *targetWindow = nullptr;
	MRFileEditor *targetEditor = nullptr;
	ushort dialogResult = cmCancel;
	std::size_t selectedIndex = 0;
	std::string insertText;
	const short width = 96;
	short visibleRows = 0;
	short height = 0;
	short buttonY = 0;

	if (TProgram::deskTop == nullptr) return false;
	if (result.header.state != mr::services::MRServiceResultState::Current) {
		if (!result.header.errorMessage.empty()) postLspWarning("LSP completion unavailable: " + result.header.errorMessage);
		else
			postLspWarning("LSP completion unavailable.");
		return true;
	}
	if (result.items.empty()) {
		postLspWarning("LSP completion: no items.");
		return true;
	}

	visibleRows = static_cast<short>(std::max<int>(5, std::min<int>(static_cast<int>(result.items.size()), 14)));
	height = static_cast<short>(visibleRows + 6);
	buttonY = static_cast<short>(height - 3);
	dialog = mr::dialogs::createScrollableDialog("LSP COMPLETION", width, height);
	dialog->insert(new TStaticText(TRect(2, 1, width - 2, 2), "Select completion:"));
	scrollBar = new TScrollBar(TRect(width - 3, 2, width - 2, height - 4));
	dialog->insert(scrollBar);
	listView = new LspCompletionListView(TRect(2, 2, width - 3, height - 4), scrollBar, result.items);
	listView->focusItemNum(0);
	dialog->insert(listView);
	{
		const std::array<mr::dialogs::DialogButtonSpec, 2> buttons = {mr::dialogs::DialogButtonSpec{"~I~nsert", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~D~one", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 1);
		mr::dialogs::insertUniformButtonRow(*dialog, (width - metrics.rowWidth) / 2, buttonY, 1, buttons);
	}
	dialog->finalizeLayout();
	dialogResult = TProgram::deskTop->execView(dialog);
	if (dialogResult == cmOK && listView != nullptr && listView->selectedIndex(selectedIndex) && selectedIndex < result.items.size()) {
		const mr::services::MRServiceCompletionItem &item = result.items[selectedIndex];

		insertText = !item.insertText.empty() ? item.insertText : item.label;
	}
	TObject::destroy(dialog);

	if (dialogResult != cmOK || insertText.empty()) return true;
	targetWindow = findLspCompletionTargetWindow(result);
	targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	if (targetEditor == nullptr) {
		postLspWarning("LSP completion insert failed: target editor is not open.");
		return true;
	}
	static_cast<void>(mrActivateEditWindow(targetWindow));
	if (!targetEditor->insertBufferText(insertText)) {
		postLspWarning("LSP completion insert failed.");
		return true;
	}
	postLspInfo("LSP completion inserted: " + firstDisplayLine(insertText, 80));
	return true;
}

bool showLspStatusDialog() {
	MRDialogFoundation *dialog = nullptr;
	MREditWindow *currentWindow = currentEditorCommandWindow();
	MRFileEditor *currentEditor = currentWindow != nullptr ? currentWindow->getEditor() : nullptr;
	mr::services::MRLspServerProfile profile;
	mr::services::MRWorkspaceServiceSnapshot workspace = g_lspAppService.buildCurrentWorkspaceSnapshot();
	mr::services::MRWorkspaceMainFileState mainFile = g_lspAppService.configuredMainFile();
	const mr::services::MRServiceResultStore &results = g_lspAppService.results();
	std::vector<std::string> lines;
	std::vector<std::string> displayLines;
	std::ostringstream line;
	std::string configurationSource;
	std::string profileError;
	bool configured = false;
	short width = 96;
	short height = 0;
	short buttonY = 0;

	if (TProgram::deskTop == nullptr) return false;
	if (currentEditor != nullptr) configured = buildLspServerProfileFromEditor(*currentEditor, profile, configurationSource, profileError);
	else if (buildLspServerProfileFromEnvironment(profile)) {
		configured = true;
		configurationSource = "MR_LSP_SERVER";
	} else
		profileError = "No current editor and MR_LSP_SERVER is empty.";

	lines.push_back(std::string("Runtime: ") + (g_lspAppService.runtimeActive() ? "active" : "inactive"));
	lines.push_back(std::string("Server configured: ") + (configured ? "yes" : "no"));
	if (configured) {
		lines.push_back("Configuration source: " + configurationSource);
		lines.push_back("Executable: " + profile.executablePath);
		if (!profile.arguments.empty()) {
			line.str(std::string());
			line.clear();
			line << "Arguments:";
			for (std::size_t i = 0; i < profile.arguments.size(); ++i)
				line << " " << profile.arguments[i];
			lines.push_back(line.str());
		}
		if (!profile.workingDirectory.empty()) lines.push_back("Working directory: " + profile.workingDirectory);
		else
			lines.push_back("Working directory: workspace root");
	} else
		lines.push_back("Configuration error: " + profileError);

	line.str(std::string());
	line.clear();
	line << "Requests: " << g_lspRequestCount << " total, " << g_lspRequestFailureCount << " failed, " << g_lspPollFailureCount << " poll failed";
	lines.push_back(line.str());
	lines.push_back("Last request: " + (g_lspLastRequestLabel.empty() ? std::string("none") : g_lspLastRequestLabel));
	lines.push_back("Last state: " + g_lspLastRequestState);
	if (!g_lspLastRequestPath.empty()) lines.push_back("Last document: " + g_lspLastRequestPath);
	if (!g_lspLastRequestPosition.empty()) lines.push_back("Last position: " + g_lspLastRequestPosition);
	if (!g_lspLastServerConfigurationSource.empty()) lines.push_back("Last configuration source: " + g_lspLastServerConfigurationSource);
	if (!g_lspLastServerExecutable.empty()) lines.push_back("Last executable: " + g_lspLastServerExecutable);
	if (!g_lspLastServerArguments.empty()) lines.push_back("Last arguments: " + g_lspLastServerArguments);
	if (!g_lspLastServerWorkingDirectory.empty()) lines.push_back("Last working directory: " + g_lspLastServerWorkingDirectory);
	if (!g_lspLastError.empty()) lines.push_back("Last error: " + g_lspLastError);
	if (!g_lspLastPollError.empty()) lines.push_back("Last poll error: " + g_lspLastPollError);

	line.str(std::string());
	line.clear();
	line << "Workspace documents: " << workspace.documents.size();
	lines.push_back(line.str());
	if (workspace.root.hasRoot) lines.push_back("Workspace root: " + workspace.root.rootPath);
	else
		lines.push_back("Workspace root: none (" + workspace.root.reason + ")");
	if (mainFile.hasMainFile) {
		line.str(std::string());
		line.clear();
		line << "Main file: ";
		if (mainFile.bufferId != 0) line << "buffer #" << mainFile.bufferId;
		else
			line << mainFile.path;
		lines.push_back(line.str());
	} else
		lines.push_back("Main file: none");

	line.str(std::string());
	line.clear();
	line << "Results: diagnostics " << results.diagnosticResults().size() << ", locations " << results.locationResults().size() << ", hovers " << results.hoverResults().size() << ", completions " << results.completionResults().size();
	lines.push_back(line.str());
	line.str(std::string());
	line.clear();
	line << "Reported: diagnostics " << g_lspReportedDiagnosticCount << ", locations " << g_lspReportedLocationCount << ", hovers " << g_lspReportedHoverCount << ", completions " << g_lspReportedCompletionCount;
	lines.push_back(line.str());
	for (std::size_t i = 0; i < lines.size(); ++i)
		displayLines.push_back(firstDisplayLine(lines[i], static_cast<std::size_t>(width - 6)));

	height = static_cast<short>(displayLines.size() + 5);
	buttonY = static_cast<short>(height - 3);
	dialog = mr::dialogs::createScrollableDialog("LSP STATUS", width, height);
	for (std::size_t i = 0; i < displayLines.size(); ++i)
		dialog->insert(new TStaticText(TRect(3, static_cast<short>(2 + i), width - 3, static_cast<short>(3 + i)), displayLines[i].c_str()));
	{
		const std::array<mr::dialogs::DialogButtonSpec, 1> buttons = {mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 0);
		mr::dialogs::insertUniformButtonRow(*dialog, (width - metrics.rowWidth) / 2, buttonY, 0, buttons);
	}
	dialog->finalizeLayout();
	TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
	return true;
}

bool showLspResultsDialog() {
	MRDialogFoundation *dialog = nullptr;
	TScrollBar *scrollBar = nullptr;
	LspResultsListView *listView = nullptr;
	ushort dialogResult = cmCancel;
	std::size_t selectedIndex = 0;
	LspResultDialogRow selectedRow;
	std::vector<LspResultDialogRow> rows;
	std::ostringstream rowText;
	std::string navigationError;
	const mr::services::MRServiceResultStore &results = g_lspAppService.results();
	const short width = 108;
	short visibleRows = 0;
	short height = 0;
	short buttonY = 0;

	if (TProgram::deskTop == nullptr) return false;

	for (const mr::services::MRServiceDiagnosticResult &result : results.diagnosticResults()) {
		if (result.diagnostics.empty()) {
			LspResultDialogRow row;

			rowText.str(std::string());
			rowText.clear();
			rowText << "DIAG [" << lspResultStateText(result.header.state) << "] 0 diagnostics";
			if (!result.header.identity.path.empty()) rowText << " " << result.header.identity.path;
			if (!result.header.errorMessage.empty()) rowText << " - " << result.header.errorMessage;
			row.text = rowText.str();
			rows.push_back(row);
			continue;
		}
		for (const mr::services::MRServiceDiagnosticEntry &diagnostic : result.diagnostics) {
			LspResultDialogRow row;

			rowText.str(std::string());
			rowText.clear();
			rowText << "DIAG " << lspDiagnosticSeverityText(diagnostic.severity) << " [" << lspResultStateText(result.header.state) << "] ";
			rowText << (diagnostic.range.start.line + 1) << ":" << (diagnostic.range.start.character + 1) << " ";
			if (!result.header.identity.path.empty()) rowText << result.header.identity.path << " - ";
			rowText << firstDisplayLine(diagnostic.message, 80);
			row.text = rowText.str();
			if (!result.header.identity.path.empty()) {
				row.action = LspResultDialogAction::NavigateLocation;
				row.location.path = result.header.identity.path;
				row.location.uri = result.header.identity.uri;
				row.location.range = diagnostic.range;
			}
			rows.push_back(row);
		}
	}

	for (const mr::services::MRServiceLocationResult &result : results.locationResults()) {
		const char *kind = result.header.kind == mr::services::MRServiceResultKind::References ? "REF" : "DEF";

		if (result.locations.empty()) {
			LspResultDialogRow row;

			rowText.str(std::string());
			rowText.clear();
			rowText << kind << " [" << lspResultStateText(result.header.state) << "] 0 locations";
			if (!result.header.identity.path.empty()) rowText << " " << result.header.identity.path;
			if (!result.header.errorMessage.empty()) rowText << " - " << result.header.errorMessage;
			row.text = rowText.str();
			rows.push_back(row);
			continue;
		}
		for (const mr::services::MRServiceLocationTarget &target : result.locations) {
			LspResultDialogRow row;

			rowText.str(std::string());
			rowText.clear();
			rowText << kind << " [" << lspResultStateText(result.header.state) << "] ";
			rowText << (target.range.start.line + 1) << ":" << (target.range.start.character + 1) << " ";
			rowText << (!target.path.empty() ? target.path : target.uri);
			row.text = rowText.str();
			if (!target.path.empty()) {
				row.action = LspResultDialogAction::NavigateLocation;
				row.location = target;
			}
			rows.push_back(row);
		}
	}

	for (const mr::services::MRServiceHoverResult &result : results.hoverResults()) {
		LspResultDialogRow row;
		std::string text = firstDisplayLine(result.hover.value, 90);

		if (text.empty()) text = "empty hover";
		rowText.str(std::string());
		rowText.clear();
		rowText << "HOVER [" << lspResultStateText(result.header.state) << "] ";
		if (!result.header.identity.path.empty()) rowText << result.header.identity.path << " - ";
		rowText << text;
		if (!result.header.errorMessage.empty()) rowText << " - " << result.header.errorMessage;
		row.text = rowText.str();
		if (result.header.state == mr::services::MRServiceResultState::Current && !result.hover.value.empty()) {
			row.action = LspResultDialogAction::ShowHover;
			row.hover = result;
		}
		rows.push_back(row);
	}

	for (const mr::services::MRServiceCompletionResult &result : results.completionResults()) {
		std::size_t visibleCompletionItems = std::min<std::size_t>(result.items.size(), 30);
		LspResultDialogRow summaryRow;

		rowText.str(std::string());
		rowText.clear();
		rowText << "COMPL [" << lspResultStateText(result.header.state) << "] " << result.items.size() << " items";
		if (!result.header.identity.path.empty()) rowText << " " << result.header.identity.path;
		if (!result.header.errorMessage.empty()) rowText << " - " << result.header.errorMessage;
		summaryRow.text = rowText.str();
		rows.push_back(summaryRow);
		for (std::size_t i = 0; i < visibleCompletionItems; ++i) {
			const mr::services::MRServiceCompletionItem &item = result.items[i];
			LspResultDialogRow itemRow;

			rowText.str(std::string());
			rowText.clear();
			rowText << "COMPL item [" << lspResultStateText(result.header.state) << "] " << item.label;
			if (!item.detail.empty()) rowText << " - " << firstDisplayLine(item.detail, 70);
			itemRow.text = rowText.str();
			rows.push_back(itemRow);
		}
		if (visibleCompletionItems < result.items.size()) {
			LspResultDialogRow tailRow;

			rowText.str(std::string());
			rowText.clear();
			rowText << "COMPL item [" << lspResultStateText(result.header.state) << "] " << (result.items.size() - visibleCompletionItems) << " more items";
			tailRow.text = rowText.str();
			rows.push_back(tailRow);
		}
	}

	if (rows.empty()) {
		postLspWarning("LSP results: no results available.");
		return true;
	}

	visibleRows = static_cast<short>(std::max<int>(5, std::min<int>(static_cast<int>(rows.size()), 16)));
	height = static_cast<short>(visibleRows + 6);
	buttonY = static_cast<short>(height - 3);
	dialog = mr::dialogs::createScrollableDialog("LSP RESULTS", width, height);
	dialog->insert(new TStaticText(TRect(2, 1, width - 2, 2), "Select result:"));
	scrollBar = new TScrollBar(TRect(width - 3, 2, width - 2, height - 4));
	dialog->insert(scrollBar);
	listView = new LspResultsListView(TRect(2, 2, width - 3, height - 4), scrollBar, rows);
	dialog->insert(listView);
	{
		const std::array<mr::dialogs::DialogButtonSpec, 2> buttons = {mr::dialogs::DialogButtonSpec{"~G~o", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~D~one", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 1);
		mr::dialogs::insertUniformButtonRow(*dialog, (width - metrics.rowWidth) / 2, buttonY, 1, buttons);
	}
	dialog->setDialogValidationHook([listView, &rows]() {
		MRScrollableDialog::DialogValidationResult validation;
		std::size_t selected = 0;

		validation.valid = listView != nullptr && listView->selectedIndex(selected) && selected < rows.size() && rows[selected].action != LspResultDialogAction::None;
		if (!validation.valid) validation.warningText = "Select a navigable or hover result.";
		return validation;
	});
	dialog->finalizeLayout();
	dialogResult = TProgram::deskTop->execView(dialog);
	if (dialogResult == cmOK && listView != nullptr && listView->selectedIndex(selectedIndex) && selectedIndex < rows.size()) selectedRow = rows[selectedIndex];
	TObject::destroy(dialog);

	if (dialogResult != cmOK) return true;
	if (selectedRow.action == LspResultDialogAction::NavigateLocation) {
		if (!navigateToLspLocation(selectedRow.location, navigationError)) postLspWarning("LSP result navigation failed: " + navigationError);
		return true;
	}
	if (selectedRow.action == LspResultDialogAction::ShowHover) return showLspHoverDialog(selectedRow.hover);
	return true;
}

void reportNewLspDiagnostics(const std::vector<mr::services::MRServiceDiagnosticResult> &diagnostics) {
	while (g_lspReportedDiagnosticCount < diagnostics.size()) {
		const mr::services::MRServiceDiagnosticResult result = diagnostics[g_lspReportedDiagnosticCount];
		std::ostringstream line;

		++g_lspReportedDiagnosticCount;
		g_lspLastRequestState = "diagnostics received";
		line << "LSP diagnostics: " << result.diagnostics.size();
		if (!result.header.identity.path.empty()) line << " " << result.header.identity.path;
		if (!result.diagnostics.empty()) {
			const mr::services::MRServiceDiagnosticEntry &diagnostic = result.diagnostics[0];
			line << ":" << (diagnostic.range.start.line + 1) << ":" << (diagnostic.range.start.character + 1);
			line << " " << lspDiagnosticSeverityText(diagnostic.severity);
			line << " - " << firstDisplayLine(diagnostic.message, 120);
		}
		postLspInfo(line.str());
	}
}

void reportNewLspLocations(const std::vector<mr::services::MRServiceLocationResult> &locations) {
	while (g_lspReportedLocationCount < locations.size()) {
		const mr::services::MRServiceLocationResult result = locations[g_lspReportedLocationCount];
		std::ostringstream line;
		const char *kind = result.header.kind == mr::services::MRServiceResultKind::References ? "references" : "definition";
		std::string navigationError;

		++g_lspReportedLocationCount;
		g_lspLastRequestState = result.header.kind == mr::services::MRServiceResultKind::References ? "references received" : "definition received";
		line << "LSP " << kind << ": " << result.locations.size();
		if (!result.locations.empty()) {
			const mr::services::MRServiceLocationTarget &target = result.locations[0];
			line << " " << (!target.path.empty() ? target.path : target.uri);
			line << ":" << (target.range.start.line + 1) << ":" << (target.range.start.character + 1);
		}
		postLspInfo(line.str());
		if (result.header.state == mr::services::MRServiceResultState::Current && result.header.kind == mr::services::MRServiceResultKind::Definition && result.locations.size() == 1) {
			if (!navigateToLspLocation(result.locations[0], navigationError)) postLspWarning("LSP definition navigation failed: " + navigationError);
		}
		if (result.header.kind == mr::services::MRServiceResultKind::References) {
			if (result.header.state == mr::services::MRServiceResultState::Current) {
				static_cast<void>(showLspReferencesDialog(result));
			} else if (!result.header.errorMessage.empty())
				postLspWarning("LSP references unavailable: " + result.header.errorMessage);
			else
				postLspWarning("LSP references unavailable.");
		}
	}
}

void reportNewLspHovers(const std::vector<mr::services::MRServiceHoverResult> &hovers) {
	while (g_lspReportedHoverCount < hovers.size()) {
		const mr::services::MRServiceHoverResult result = hovers[g_lspReportedHoverCount];
		std::string text = firstDisplayLine(result.hover.value, 140);

		++g_lspReportedHoverCount;
		g_lspLastRequestState = "hover received";
		if (text.empty()) text = "empty hover";
		postLspInfo("LSP hover: " + text);
		if (result.header.state == mr::services::MRServiceResultState::Current) {
			static_cast<void>(showLspHoverDialog(result));
		} else if (!result.header.errorMessage.empty())
			postLspWarning("LSP hover unavailable: " + result.header.errorMessage);
		else
			postLspWarning("LSP hover unavailable.");
	}
}

void reportNewLspCompletions(const std::vector<mr::services::MRServiceCompletionResult> &completions) {
	while (g_lspReportedCompletionCount < completions.size()) {
		const mr::services::MRServiceCompletionResult result = completions[g_lspReportedCompletionCount];
		std::ostringstream line;

		++g_lspReportedCompletionCount;
		g_lspLastRequestState = "completion received";
		line << "LSP completion: " << result.items.size();
		if (!result.header.identity.path.empty()) line << " " << result.header.identity.path;
		postLspInfo(line.str());
		static_cast<void>(showLspCompletionDialog(result));
	}
}

void reportNewLspResults() {
	const mr::services::MRServiceResultStore &results = g_lspAppService.results();

	reportNewLspDiagnostics(results.diagnosticResults());
	reportNewLspLocations(results.locationResults());
	reportNewLspHovers(results.hoverResults());
	reportNewLspCompletions(results.completionResults());
}

struct ParenthesisPair {
	char open;
	char close;
};

std::optional<ParenthesisPair> parenthesisPairFor(char ch) noexcept {
	switch (ch) {
		case '(':
		case ')':
			return ParenthesisPair{'(', ')'};
		case '[':
		case ']':
			return ParenthesisPair{'[', ']'};
		case '{':
		case '}':
			return ParenthesisPair{'{', '}'};
		default:
			return std::nullopt;
	}
}

bool isOpeningParenthesis(char ch) noexcept {
	return ch == '(' || ch == '[' || ch == '{';
}

bool findMatchingParenthesis(MRFileEditor &editor, std::size_t origin, std::size_t &match) noexcept {
	const std::size_t length = editor.bufferLength();
	const char originChar = editor.charAtOffset(origin);
	const std::optional<ParenthesisPair> pair = parenthesisPairFor(originChar);
	int depth = 1;

	if (!pair) return false;
	if (isOpeningParenthesis(originChar)) {
		for (std::size_t pos = origin + 1; pos < length; ++pos) {
			const char ch = editor.charAtOffset(pos);
			if (ch == pair->open) ++depth;
			if (ch == pair->close && --depth == 0) {
				match = pos;
				return true;
			}
		}
		return false;
	}
	for (std::size_t pos = origin; pos > 0; --pos) {
		const std::size_t probe = pos - 1;
		const char ch = editor.charAtOffset(probe);
		if (ch == pair->close) ++depth;
		if (ch == pair->open && --depth == 0) {
			match = probe;
			return true;
		}
	}
	return false;
}

bool handleMatchParenthesis() {
	MREditWindow *win = currentEditWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::size_t origin = 0;
	std::size_t match = 0;

	if (editor == nullptr) return true;
	const std::size_t length = editor->bufferLength();
	if (length == 0) {
		postDialogWarning("No parenthesis at cursor.");
		return true;
	}
	origin = std::min(editor->cursorOffset(), length - 1);
	if (!parenthesisPairFor(editor->charAtOffset(origin))) {
		const std::size_t cursor = std::min(editor->cursorOffset(), length);
		if (cursor == 0 || !parenthesisPairFor(editor->charAtOffset(cursor - 1))) {
			postDialogWarning("No parenthesis at cursor.");
			return true;
		}
		origin = cursor - 1;
	}
	if (!findMatchingParenthesis(*editor, origin, match)) {
		postDialogWarning("No matching parenthesis found.");
		return true;
	}
	editor->setCursorOffset(match);
	editor->setSelectionOffsets(match, match);
	editor->revealCursor(True);
	return true;
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

	if (!promptForPath("OPEN FILE", fileName, sizeof(fileName))) return true;
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

	if (!promptForPath("LOAD FILE", fileName, sizeof(fileName))) return true;
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

MRBentoBox *findBuildBentoBoxForSource(const std::string &sourcePath) {
	for (MREditWindow *candidate : allEditWindowsInZOrder()) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(candidate);

		if (bentoBox != nullptr && !bentoBox->allowsDocumentViewportSplit() && sourcePath == bentoBox->currentFileName()) return bentoBox;
	}
	return nullptr;
}

MREditWindow *currentExternalOutputWindow() {
	MREditWindow *win = currentEditWindow();
	MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(win);

	if (bentoBox != nullptr && bentoBox->buildOutputPane() != nullptr) return bentoBox->buildOutputPane();
	return win;
}

void setSplitDiagnosticsStatusForOutput(MREditWindow *outputWindow, const char *status) {
	MRBentoBox *split = outputWindow != nullptr ? dynamic_cast<MRBentoBox *>(outputWindow->owner) : nullptr;

	if (split != nullptr && split->buildOutputPane() == outputWindow) split->setCompilerOutputStatus(status);
}

std::string pathBaseName(const std::string &path) {
	std::filesystem::path fsPath(path);
	std::string base = fsPath.filename().string();

	return base.empty() ? path : base;
}

std::string buildCompilerOutputTitle(const MRCompilerProfile &compilerProfile, const std::string &matchedProfileName, const std::string &sourcePath) {
	std::string profileName = compilerProfile.name;
	const std::string sourceName = pathBaseName(sourcePath);
	std::string title;

	if (profileName.empty()) profileName = !matchedProfileName.empty() ? matchedProfileName : compilerProfile.toolchain;
	if (profileName.empty()) profileName = "Compiler";
	title = "Build: " + profileName;
	if (!sourceName.empty()) title += " - " + sourceName;
	if (title.size() > 70) title = title.substr(0, 67) + "...";
	return title;
}

bool handleCompilerErrorNavigation(void *commandInfo, bool forward) {
	MREditWindow *win = currentEditWindow();
	MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(win);

	(void)commandInfo;
	if (bentoBox == nullptr) {
		postDialogWarning("Compiler error navigation requires an active Bento build window.");
		return true;
	}
	static_cast<void>(bentoBox->refreshCompilerDiagnosticsFromOutput());
	if (!(forward ? bentoBox->jumpToNextProblem() : bentoBox->jumpToPreviousProblem())) {
		postDialogWarning("No compiler diagnostic location found.");
		return true;
	}
	return true;
}

bool isFileCompareWindowCandidate(MREditWindow *window) {
	if (window == nullptr || window->getEditor() == nullptr || window->hasTrackedExternalIoTasks()) return false;
	switch (window->windowRole()) {
		case MREditWindow::wrCommunicationCommand:
		case MREditWindow::wrCommunicationPipe:
		case MREditWindow::wrCommunicationDevice:
		case MREditWindow::wrLog:
		case MREditWindow::wrHelp:
			return false;
		default:
			break;
	}
	MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);
	return bentoBox == nullptr || bentoBox->allowsDocumentViewportSplit();
}

std::string fileCompareSourceTitle(MREditWindow *window) {
	if (window == nullptr) return "?No-File";
	std::string fileName = window->currentFileName();
	if (!fileName.empty()) return pathBaseName(fileName);
	const char *title = window->getTitle(0);
	return title != nullptr && *title != '\0' ? std::string(title) : std::string("?No-File");
}

MRBentoCompareSource captureFileCompareSource(MREditWindow *window) {
	MRBentoCompareSource source;

	if (window == nullptr) return source;
	source.window = window;
	source.bufferId = window->bufferId();
	source.documentId = window->documentId();
	source.version = window->documentVersion();
	source.wasVisible = (window->state & sfVisible) != 0;
	source.wasManuallyHidden = isWindowManuallyHidden(window);
	source.title = fileCompareSourceTitle(window);
	if (window->getEditor() != nullptr) source.text = window->getEditor()->snapshotText();
	return source;
}

std::string fileCompareWindowTitle(const MRBentoCompareSetup &setup) {
	std::string title = "Compare: " + setup.original.title + " / " + setup.compare.title;

	if (title.size() > 72) title = title.substr(0, 69) + "...";
	return title;
}

std::uint64_t submitFileCompareTask(MRBentoBox *bentoBox, const MRBentoCompareSetup &setup) {
	std::vector<std::string> originalLines;
	std::vector<std::string> compareLines;

	if (bentoBox == nullptr) return 0;
	mr::diff::mrSplitTextLinesForDiff(setup.original.text, originalLines);
	mr::diff::mrSplitTextLinesForDiff(setup.compare.text, compareLines);

	return mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FileCompare, setup.original.documentId, setup.original.version, "file compare", [originalLines, compareLines, originalDocumentId = setup.original.documentId, originalVersion = setup.original.version, compareDocumentId = setup.compare.documentId, compareVersion = setup.compare.version](const mr::coprocessor::TaskInfo &task, std::stop_token stopToken) {
		mr::coprocessor::Result result;
		std::vector<mr::diff::MRDiffHunk> hunks;
		std::string errorText;

		result.task = task;
		if (!mr::diff::mrComputeMyersDiff(originalLines, compareLines, hunks, &errorText, stopToken)) {
			result.status = stopToken.stop_requested() ? mr::coprocessor::TaskStatus::Cancelled : mr::coprocessor::TaskStatus::Failed;
			result.error = errorText;
			return result;
		}
		result.status = mr::coprocessor::TaskStatus::Completed;
		result.payload = std::make_shared<mr::coprocessor::FileComparePayload>(originalDocumentId, originalVersion, compareDocumentId, compareVersion, originalLines.size(), compareLines.size(), std::move(hunks));
		return result;
	});
}

bool handleTextFileCompare() {
	MREditWindow *originalWindow = currentEditWindow();
	MREditWindow *compareWindow;
	MRBentoBox *compareBento;
	MRBentoCompareSetup setup;
	std::uint64_t taskId;
	std::string title;

	if (!isFileCompareWindowCandidate(originalWindow)) {
		postDialogWarning("File compare requires an editor window.");
		return true;
	}
	compareWindow = mrShowWindowListDialog(mrwlSelectFileCompareTarget, originalWindow);
	if (compareWindow == nullptr) return true;
	if (compareWindow == originalWindow || !isFileCompareWindowCandidate(compareWindow)) {
		postDialogWarning("Selected window cannot be used for file compare.");
		return true;
	}

	setup.original = captureFileCompareSource(originalWindow);
	setup.compare = captureFileCompareSource(compareWindow);
	title = fileCompareWindowTitle(setup);
	compareBento = createFileCompareBentoBoxWindow(title.c_str());
	if (compareBento == nullptr) {
		postDialogWarning("Unable to create file compare BentoBox.");
		return true;
	}
	if (!compareBento->initializeFileCompare(setup)) {
		message(compareBento, evCommand, cmClose, nullptr);
		postDialogWarning("Unable to initialize file compare BentoBox.");
		return true;
	}

	taskId = submitFileCompareTask(compareBento, setup);
	if (taskId == 0) {
		message(compareBento, evCommand, cmClose, nullptr);
		postDialogWarning("Unable to start file compare worker.");
		return true;
	}
	compareBento->setFileCompareTask(taskId);
	compareBento->trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::FileCompare, "file compare");
	static_cast<void>(mrActivateEditWindow(compareBento));
	return true;
}

bool handleBuildCurrentFile() {
	MREditWindow *win = currentEditWindow();
	std::string sourcePath;
	std::string matchedProfileName;
	std::string errorText;
	std::string commandLine;
	std::string outputTitle;
	MRCompilerProfile compilerProfile;
	MRBentoBox *bentoBox;
	MRBentoBox *sourceBentoBox;
	MREditWindow *outputWindow;
	MREditWindow *problemsWindow;
	MREditWindow *sourceWindowToClose = nullptr;
	bool createdBentoBox = false;

	if (win == nullptr) return true;
	sourceBentoBox = dynamic_cast<MRBentoBox *>(win);
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
	outputTitle = buildCompilerOutputTitle(compilerProfile, matchedProfileName, sourcePath);

	bentoBox = sourceBentoBox != nullptr ? sourceBentoBox : findBuildBentoBoxForSource(sourcePath);
	if (bentoBox == nullptr) {
		bentoBox = createBentoBoxWindow(sourcePath.c_str());
		createdBentoBox = true;
	}
	if (bentoBox == nullptr) {
		postSearchError("Unable to create build BentoBox window.");
		return true;
	}
	if (!bentoBox->ensureBuildDiagnosticsPanes(outputWindow, problemsWindow)) {
		if (createdBentoBox) message(bentoBox, evCommand, cmClose, nullptr);
		postSearchError("Unable to create build diagnostics panes.");
		return true;
	}
	if (outputWindow->hasTrackedExternalIoTasks()) {
		bentoBox->setCompilerOutputStatus("busy");
		postDialogWarning("Build output is still running; stop it before rebuilding.");
		static_cast<void>(mrActivateEditWindow(bentoBox));
		return true;
	}
	if (bentoBox->isFileChanged() && !bentoBox->confirmAbandonForReload()) return true;
	if (!loadResolvedFileIntoWindow(bentoBox, sourcePath, "Build current file")) {
		if (createdBentoBox) message(bentoBox, evCommand, cmClose, nullptr);
		postSearchError("Unable to load source file into build BentoBox window.");
		return true;
	}
	if (sourceBentoBox == nullptr && win != bentoBox && win->currentFileName() == sourcePath && !win->isFileChanged()) sourceWindowToClose = win;
	static_cast<void>(mrActivateEditWindow(bentoBox));
	if (sourceWindowToClose != nullptr) message(sourceWindowToClose, evCommand, cmClose, nullptr);
	bentoBox->clearCompilerDiagnostics();
	startExternalCommandInWindow(outputWindow, commandLine, true, false, false, outputTitle, compilerProfile.buildSuccessAudioUri, compilerProfile.buildFailureAudioUri);
	bentoBox->activatePrimaryPane();
	return true;
}

bool startExternalCommandInWindow(MREditWindow *win, const std::string &commandLine, bool replaceBuffer, bool activate, bool closeOnFailure, std::string_view titleOverride, const std::string &successAudioUri, const std::string &failureAudioUri) {
	std::string title;
	std::string initialText;
	std::ostringstream logLine;
	std::uint64_t taskId;

	if (win == nullptr) return false;
	title = titleOverride.empty() ? shortenCommandTitle(commandLine) : std::string(titleOverride);
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

	taskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Io, mr::coprocessor::TaskKind::ExternalIo, static_cast<std::size_t>(win->bufferId()), 0, std::string("external-io: ") + commandLine, [commandLine, channelId = static_cast<std::size_t>(win->bufferId()), successAudioUri, failureAudioUri](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) { return runExternalCommandTask(info, stopToken, channelId, commandLine, successAudioUri, failureAudioUri); });
	if (taskId == 0) {
		if (closeOnFailure) message(win, evCommand, cmClose, nullptr);
		postSearchError("Unable to start external command worker.");
		return false;
	}
	win->trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::ExternalIo, commandLine);
	setSplitDiagnosticsStatusForOutput(win, "running");

	logLine << "Started external command in communication window #" << win->bufferId() << ": " << commandLine << " [task #" << taskId << "]";
	mrLogMessage(logLine.str().c_str());
	return true;
}

bool dispatchEditorCommand(ushort editorCommand, bool requiresWritable) {
	MREditWindow *win = currentEditorCommandWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

	if (win == nullptr || editor == nullptr) return true;
	if (requiresWritable && win->isReadOnly()) {
		postDialogWarning(kWindowReadOnlyMessage);
		return true;
	}
	const bool trackFileCompareMutation = editorCommand == cmUndo || editorCommand == cmMrEditUndo || editorCommand == cmMrEditRedo;
	const std::size_t versionBefore = trackFileCompareMutation ? editor->documentVersion() : 0;
	message(editor, evCommand, editorCommand, nullptr);
	if (trackFileCompareMutation && editor->documentVersion() != versionBefore) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(win);
		if (bentoBox == nullptr) bentoBox = dynamic_cast<MRBentoBox *>(win->owner);
		if (bentoBox != nullptr) static_cast<void>(bentoBox->refreshFileCompareAfterEditorMutation(win));
	}
	if (win->hasBlock() && !win->isBlockMarking()) win->refreshBlockVisual();
	return true;
}

bool handleBlockAction(bool ok, const char *failureText) {
	if (!ok && failureText != nullptr && *failureText != '\0') messageBox(mfInformation | mfOKButton, "%s", failureText);
	return true;
}

bool promptBlockSavePath(std::string &outPath) {
	char fileName[MAXPATH] = {0};
	ushort result = cmCancel;

	outPath.clear();
	mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::BlockSave, fileName, sizeof(fileName), "*.*");
	result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::BlockSave, "*.*", "SAVE BLOCK", "~N~ame", fdOKButton, fileName);
	if (result == cmCancel) return false;
	outPath = expandUserPath(fileName);
	if (outPath.empty()) {
		postDialogWarning("No file name specified.");
		return false;
	}
	return true;
}

bool handleLoadBlockFromFile(MREditWindow *window) {
	char fileName[MAXPATH] = {0};
	std::string resolvedPath;
	std::string errorText;

	if (window == nullptr) return false;
	if (window->isReadOnly()) {
		postDialogWarning(kWindowReadOnlyMessage);
		return true;
	}
	if (!promptForPath(MRDialogHistoryScope::BlockLoad, "LOAD BLOCK", fileName, sizeof(fileName))) return true;
	if (!resolveReadableExistingPath(MRDialogHistoryScope::BlockLoad, fileName, resolvedPath)) {
		forgetLoadDialogPath(MRDialogHistoryScope::BlockLoad, fileName);
		return true;
	}
	if (!window->loadBlockFromFile(resolvedPath, &errorText)) {
		forgetLoadDialogPath(MRDialogHistoryScope::BlockLoad, resolvedPath.c_str());
		postDialogWarning(errorText.empty() ? "Unable to load block." : errorText);
		return true;
	}
	rememberLoadDialogPath(MRDialogHistoryScope::BlockLoad, resolvedPath.c_str());
	return true;
}

bool handleSaveBlockToFile(MREditWindow *window) {
	std::string savePath;
	std::string errorText;

	if (window == nullptr) return false;
	if (!promptBlockSavePath(savePath)) return true;
	if (!window->saveBlockToFile(savePath, &errorText)) {
		postDialogWarning(errorText.empty() ? "Unable to save stream block." : errorText);
		return true;
	}
	rememberLoadDialogPath(MRDialogHistoryScope::BlockSave, savePath.c_str());
	return true;
}

bool handleCopyBlock(MREditWindow *window) {
	std::string errorText;

	if (window == nullptr) return false;
	if (!window->copyBlock(&errorText)) postDialogWarning(errorText.empty() ? "Unable to copy block." : errorText);
	return true;
}

bool handleDeleteBlock(MREditWindow *window) {
	std::string errorText;

	if (window == nullptr) return false;
	if (!window->deleteBlock(&errorText)) postDialogWarning(errorText.empty() ? "Unable to delete block." : errorText);
	return true;
}

bool handleIndentBlock(MREditWindow *window) {
	std::string errorText;

	if (window == nullptr) return false;
	if (!window->indentBlock(&errorText)) postDialogWarning(errorText.empty() ? "Unable to indent block." : errorText);
	return true;
}

bool handleUndentBlock(MREditWindow *window) {
	std::string errorText;

	if (window == nullptr) return false;
	if (!window->undentBlock(&errorText)) postDialogWarning(errorText.empty() ? "Unable to undent block." : errorText);
	return true;
}

bool handleWindowCopyBlock(MREditWindow *window) {
	MREditWindow *selected = nullptr;
	MREditWindow *target = nullptr;
	std::string errorText;

	if (window == nullptr) return false;
	selected = mrShowWindowListDialog(mrwlSelectLinkTarget, window);
	if (selected == nullptr) return true;
	target = selected->editorCommandTarget();
	if (target == nullptr || target == window) {
		postDialogWarning("Select a different target window.");
		return true;
	}
	if (target->isReadOnly()) {
		postDialogWarning(kWindowReadOnlyMessage);
		return true;
	}
	if (!window->copyBlockTo(*target, &errorText)) postDialogWarning(errorText.empty() ? "Unable to copy block to target window." : errorText);
	return true;
}

bool handleMoveBlock(MREditWindow *window) {
	std::string errorText;

	if (window == nullptr) return false;
	if (!window->moveBlock(&errorText)) postDialogWarning(errorText.empty() ? "Unable to move block." : errorText);
	return true;
}

bool handleWindowMoveBlock(MREditWindow *window) {
	MREditWindow *selected = nullptr;
	MREditWindow *target = nullptr;
	std::string errorText;

	if (window == nullptr) return false;
	selected = mrShowWindowListDialog(mrwlSelectLinkTarget, window);
	if (selected == nullptr) return true;
	target = selected->editorCommandTarget();
	if (target == nullptr || target == window) {
		postDialogWarning("Select a different target window.");
		return true;
	}
	if (target->isReadOnly()) {
		postDialogWarning(kWindowReadOnlyMessage);
		return true;
	}
	if (!window->moveBlockTo(*target, &errorText)) postDialogWarning(errorText.empty() ? "Unable to move block to target window." : errorText);
	return true;
}

bool dispatchTargetedKeymapAppCommand(MREditWindow *window, ushort command) {
	switch (command) {
		case cmMrEditUndo:
		case cmMrEditRedo:
			if (window == nullptr) return false;
			if (window->isReadOnly()) {
				postDialogWarning(kWindowReadOnlyMessage);
				return true;
			}
			return dispatchEditorCommandEvent(window, command);
		case cmMrEditCutToBuffer:
			if (window == nullptr) return false;
			if (window->isReadOnly()) {
				postDialogWarning(kWindowReadOnlyMessage);
				return true;
			}
			return dispatchEditorCommandEvent(window, cmCut);
		case cmMrEditCopyToBuffer:
			if (window == nullptr) return false;
			return dispatchEditorCommandEvent(window, cmCopy);
		case cmMrEditPasteFromBuffer:
			if (window == nullptr) return false;
			if (window->isReadOnly()) {
				postDialogWarning(kWindowReadOnlyMessage);
				return true;
			}
			return dispatchEditorCommandEvent(window, cmPaste);
		case cmMrBlockMarkStream:
			if (window == nullptr) return false;
			window->beginStreamBlock();
			return true;
		case cmMrBlockCopy:
			return handleCopyBlock(window);
		case cmMrBlockMove:
			return handleMoveBlock(window);
		case cmMrBlockDelete:
			return handleDeleteBlock(window);
		case cmMrBlockIndent:
			return handleIndentBlock(window);
		case cmMrBlockUndent:
			return handleUndentBlock(window);
		case cmMrBlockWindowCopy:
			return handleWindowCopyBlock(window);
		case cmMrBlockWindowMove:
			return handleWindowMoveBlock(window);
		case cmMrEditToggleInsertMode:
			if (window == nullptr || window->getEditor() == nullptr) return false;
			window->getEditor()->setInsertModeEnabled(!window->getEditor()->insertModeEnabled());
			return true;
		default:
			return dispatchApplicationCommandEvent(command);
	}
}

bool persistVisibleEditSetupSettingsWithFeedback(const MREditSetupSettings &settings, const std::string &errorPrefix);

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
	MREditWindow *win = currentExternalOutputWindow();
	std::ostringstream line;
	std::size_t taskCount;

	if (win == nullptr || !win->isCommunicationWindow()) return true;
	taskCount = win->trackedTaskCount(mr::coprocessor::TaskKind::ExternalIo);
	if (taskCount == 0) {
		setSplitDiagnosticsStatusForOutput(win, "idle");
		postDialogWarning(kNoExternalProgramTaskMessage);
		return true;
	}
	if (!win->cancelTrackedExternalIoTasks()) return true;
	setSplitDiagnosticsStatusForOutput(win, "stopping");
	line << "Requested stop of " << taskCount << " external program task";
	if (taskCount != 1) line << "s";
	line << " in communication window #" << win->bufferId() << ".";
	mrLogMessage(line.str().c_str());
	return true;
}

bool handleRestartCurrentProgram() {
	MREditWindow *current = currentEditWindow();
	MREditWindow *win = currentExternalOutputWindow();
	const bool splitOutputTarget = win != nullptr && win != current;
	std::string titleOverride;

	if (win == nullptr || win->windowRole() != MREditWindow::wrCommunicationCommand) return true;
	if (win->hasTrackedExternalIoTasks()) {
		setSplitDiagnosticsStatusForOutput(win, "busy");
		postDialogWarning(kStopProgramBeforeRestartMessage);
		return true;
	}
	if (win->windowRoleDetail().empty()) {
		postDialogWarning(kNoRestartableCommandMessage);
		return true;
	}
	if (splitOutputTarget && win->getTitle(0) != nullptr) titleOverride = win->getTitle(0);
	startExternalCommandInWindow(win, win->windowRoleDetail(), true, !splitOutputTarget, false, titleOverride);
	if (splitOutputTarget && current != nullptr) static_cast<void>(mrActivateEditWindow(current));
	return true;
}

bool handleClearCurrentOutput() {
	MREditWindow *win = currentExternalOutputWindow();
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
		setSplitDiagnosticsStatusForOutput(win, "busy");
		messageBox(mfInformation | mfOKButton, "Stop the current program before clearing its output.");
		return true;
	}
	if (!win->replaceTextBuffer("", win->getTitle(0))) {
		postSearchError("Unable to clear communication window.");
		return true;
	}
	win->setReadOnly(true);
	win->setFileChanged(false);
	if (MRBentoBox *split = dynamic_cast<MRBentoBox *>(win->owner); split != nullptr && split->buildOutputPane() == win) split->clearCompilerDiagnostics();
	setSplitDiagnosticsStatusForOutput(win, "idle");
	line << "Cleared communication window #" << win->bufferId() << ".";
	mrLogMessage(line.str().c_str());
	return true;
}

bool handleCompilerProblemsNavigation(MREditWindow *targetWindow, bool forward) {
	MRBentoBox *bentoBox = compilerProblemsBentoBoxForWindow(targetWindow);
	const bool handled = bentoBox != nullptr && (forward ? bentoBox->jumpToNextProblem() : bentoBox->jumpToPreviousProblem());

	if (!handled) postDialogWarning("No compiler problems available.");
	return true;
}

bool copyMarkedBlockToSystemClipboard(MREditWindow *targetWindow) {
	std::string text;
	std::string errorText;

	if (targetWindow == nullptr) return false;
	if (!targetWindow->captureBlockPayload(text, &errorText)) {
		postDialogWarning(errorText.empty() ? "No block marked." : errorText);
		return true;
	}
	TClipboard::setText(TStringView(text.data(), text.size()));
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
			return dispatchTargetedKeymapAppCommand(window, it->command);
		case KeymapDispatchKind::EditorCommand:
			return dispatchEditorCommandEvent(window, it->command);
		case KeymapDispatchKind::WindowMethod:
			return dispatchKeymapWindowMethod(window, it->windowMethod);
		case KeymapDispatchKind::Custom:
			switch (it->customAction) {
				case KeymapCustomAction::DeleteForwardCharOrBlock:
					return dispatchEditorCommandEvent(window, cmDelChar);
				case KeymapCustomAction::LoadBlockFromFile:
					return handleLoadBlockFromFile(window);
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
					return runDisabledBlockAction();
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
					return copyMarkedBlockToSystemClipboard(window);
				case KeymapCustomAction::ExtendBlockByMotion:
				case KeymapCustomAction::DisabledBlockAction:
					return runDisabledBlockAction();
				case KeymapCustomAction::SearchResultsNext:
					return handleSearchResultsNext();
				case KeymapCustomAction::CompilerProblemsNext:
					return handleCompilerProblemsNavigation(window, true);
				case KeymapCustomAction::CompilerProblemsPrevious:
					return handleCompilerProblemsNavigation(window, false);
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

void pumpMRLspService() {
	std::string errorMessage;

	if (!g_lspAppService.runtimeActive()) return;
	if (!g_lspAppService.poll(errorMessage)) {
		++g_lspPollFailureCount;
		g_lspLastRequestState = "poll failed";
		g_lspLastPollError = errorMessage;
		g_lspLastError = errorMessage;
		postLspError("LSP poll failed: " + errorMessage);
		g_lspAppService.close();
		return;
	}
	reportNewLspResults();
}

bool handleMRCommand(ushort command, void *commandInfo) {
	switch (command) {
		case cmMrFileOpen:
			return handleFileOpen();

		case cmMrFileLoad:
			return handleFileLoad();

		case cmMrFileOpenWorkspace:
			return mrLoadWorkspaceWithDialog();

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
			return dispatchEditorCommand(cmCut, true);

		case cmMrEditCopyToBuffer:
			return dispatchEditorCommand(cmCopy, false);

		case cmMrEditAppendToBuffer:
		case cmMrEditCutAndAppendToBuffer:
			return runDisabledBlockAction();

		case cmMrEditPasteFromBuffer:
			return dispatchEditorCommand(cmPaste, true);

		case cmMrEditToggleInsertMode: {
			MREditWindow *win = currentEditWindow();
			if (win != nullptr && win->getEditor() != nullptr) win->getEditor()->setInsertModeEnabled(!win->getEditor()->insertModeEnabled());
			return true;
		}

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

		case cmMrBlockCopy: {
			return handleCopyBlock(currentEditorCommandWindow());
		}

		case cmMrBlockMove: {
			return handleMoveBlock(currentEditorCommandWindow());
		}

		case cmMrBlockDelete: {
			return handleDeleteBlock(currentEditorCommandWindow());
		}

		case cmMrBlockLoadFromDisk:
			return handleLoadBlockFromFile(currentEditorCommandWindow());

		case cmMrBlockSaveToDisk: {
			return handleSaveBlockToFile(currentEditorCommandWindow());
		}

		case cmMrBlockIndent: {
			return handleIndentBlock(currentEditorCommandWindow());
		}

		case cmMrBlockUndent: {
			return handleUndentBlock(currentEditorCommandWindow());
		}

		case cmMrBlockWindowCopy: {
			return handleWindowCopyBlock(currentEditorCommandWindow());
		}

		case cmMrBlockWindowMove: {
			return handleWindowMoveBlock(currentEditorCommandWindow());
		}

		case cmMrBlockMarkLines: {
			MREditWindow *win = currentEditWindow();
			if (win != nullptr) win->beginLineBlock();
			return true;
		}

		case cmMrBlockMarkColumns: {
			MREditWindow *win = currentEditWindow();
			if (win != nullptr) win->beginColumnBlock();
			return true;
		}

		case cmMrBlockMarkStream: {
			MREditWindow *win = currentEditWindow();
			if (win != nullptr) win->beginStreamBlock();
			return true;
		}

		case cmMrBlockToggleMarking: {
			MREditWindow *win = currentEditWindow();
			if (win != nullptr) {
				if (win->isBlockMarking()) win->endBlock();
				else
					win->beginLineBlock();
			}
			return true;
		}

		case cmMrBlockToggleVisibility: {
			MREditWindow *win = currentEditWindow();
			if (win != nullptr && !win->toggleBlockVisibility()) postDialogWarning("No block marked.");
			return true;
		}

		case cmMrBlockEndMarking: {
			MREditWindow *win = currentEditWindow();
			if (win != nullptr) win->endBlock();
			return true;
		}

		case cmMrBlockTurnMarkingOff: {
			MREditWindow *win = currentEditWindow();
			if (win != nullptr) win->clearBlock();
			return true;
		}

		case cmMrBlockPersistent:
			return runDisabledBlockAction();

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

		case cmMrWindowSplitHorizontal: {
			MREditWindow *window = currentEditWindow();
			MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);
			if (window == nullptr || !window->allowsDocumentViewportSplit()) {
				postDialogWarning("Split window requires a document editor window.");
				return true;
			}
			if (bentoBox == nullptr) {
				bentoBox = convertEditWindowToBentoBox(window);
			}
			if (bentoBox == nullptr) {
				postDialogWarning("Split window requires an editable window without running external I/O.");
				return true;
			}
			return bentoBox->splitActiveEditorPane(bppSplitDown);
		}

		case cmMrWindowSplitVertical: {
			MREditWindow *window = currentEditWindow();
			MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);
			if (window == nullptr || !window->allowsDocumentViewportSplit()) {
				postDialogWarning("Split window requires a document editor window.");
				return true;
			}
			if (bentoBox == nullptr) {
				bentoBox = convertEditWindowToBentoBox(window);
			}
			if (bentoBox == nullptr) {
				postDialogWarning("Split window requires an editable window without running external I/O.");
				return true;
			}
			return bentoBox->splitActiveEditorPane(bppSplitRight);
		}

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

		case cmMrTextFileCompare:
			return handleTextFileCompare();

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

		case cmMrOtherBuildCurrentFile:
			return handleBuildCurrentFile();

		case cmMrOtherStopProgram:
			return handleStopCurrentProgram();

		case cmMrOtherRestartProgram:
			return handleRestartCurrentProgram();

		case cmMrOtherClearOutput:
			return handleClearCurrentOutput();

		case cmMrOtherFindNextCompilerError:
			return handleCompilerErrorNavigation(commandInfo, true);

		case cmMrOtherFindPreviousCompilerError:
			return handleCompilerErrorNavigation(commandInfo, false);

		case cmMrOtherLspDefinition:
			return requestLspEditorCommand(mr::services::MRLspServiceCommandId::GoToDefinition, "LSP definition");

		case cmMrOtherLspReferences:
			return requestLspEditorCommand(mr::services::MRLspServiceCommandId::FindReferences, "LSP references");

		case cmMrOtherLspHover:
			return requestLspEditorCommand(mr::services::MRLspServiceCommandId::ShowHover, "LSP hover");

		case cmMrOtherLspComplete:
			return requestLspEditorCommand(mr::services::MRLspServiceCommandId::Complete, "LSP completion");

		case cmMrOtherLspStatus:
			return showLspStatusDialog();

		case cmMrOtherLspResults:
			if (!syncCurrentEditorForLspResults()) return true;
			return showLspResultsDialog();

		case cmMrOtherMacroManager:
			return runMacroManagerDialog();

		case cmMrOtherMatchBraceOrParen:
			return handleMatchParenthesis();

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
