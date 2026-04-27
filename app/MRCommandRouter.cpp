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
#define Uses_THistory
#define Uses_TDrawBuffer
#define Uses_MsgBox
#define Uses_TCheckBoxes
#define Uses_TRadioButtons
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TSItem
#include <tvision/tv.h>

#include "MRCommandRouter.hpp"

#include <algorithm>
#include <array>
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
#include "../dialogs/MRAbout.hpp"
#include "../dialogs/MRSetup.hpp"
#include "../dialogs/MRSetupCommon.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#include "../app/utils/MRStringUtils.hpp"
#include "../keymap/MRKeymapActionCatalog.hpp"
#include "../keymap/MRKeymapSequence.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../mrmac/MRVM.hpp"
#include "../app/commands/MRExternalCommand.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../dialogs/MRMacroFile.hpp"
#include "../dialogs/MRWindowList.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRMenuBar.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "MREditorApp.hpp"
#include "MRCommands.hpp"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

namespace {
bool startExternalCommandInWindow(MREditWindow *win, const std::string &commandLine, bool replaceBuffer,
                                  bool activate, bool closeOnFailure);

struct SearchUiState {
	bool hasPrevious = false;
	std::string pattern;
	std::string replacement;
	std::size_t lastStart = 0;
	std::size_t lastEnd = 0;
	MRSearchDialogOptions lastOptions;
};

SearchUiState g_searchUiState;

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

struct SearchMatchEntry {
	std::size_t start = 0;
	std::size_t end = 0;
	std::size_t line = 1;
	std::size_t column = 1;
	std::string preview;
	std::size_t previewMatchOffset = 0;
	std::size_t previewMatchLength = 0;
};

struct MultiFileSearchCandidate {
	std::string normalizedPath;
	MREditWindow *window = nullptr;
	bool inMemory = false;
};

struct MultiFileSearchFileResult {
	std::string normalizedPath;
	std::string fileName;
	std::vector<SearchMatchEntry> matches;
	std::size_t selectedMatchIndex = 0;
	bool startedInMemory = false;
	bool temporaryWindow = false;
	MREditWindow *window = nullptr;
};

struct MultiFileSearchSession {
	bool valid = false;
	bool replaceMode = false;
	bool caseSensitive = false;
	bool regularExpressions = true;
	bool keepFilesOpen = false;
	std::string pattern;
	std::string replacement;
	std::vector<MultiFileSearchFileResult> files;
	std::size_t selectedFileIndex = 0;
};

MultiFileSearchSession g_lastMultiFileSearchSession;

enum class MultiFileCollectOutcome : unsigned char {
	Success = 0,
	NoHits = 1,
	Cancelled = 2,
	Error = 3
};

MultiFileCollectOutcome collectMultiFileSession(MultiFileSearchSession &session,
                                                const MRMultiSearchDialogOptions &options,
                                                const std::string &pattern,
                                                const std::string &replacement, bool replaceMode,
                                                bool keepFilesOpen, std::string &errorText);
bool showMultiFileSessionCollectionError(const std::string &errorText);

struct PendingTransientSelectionClear {
	bool active = false;
	std::string normalizedPath;
	std::size_t start = 0;
	std::size_t end = 0;
};

PendingTransientSelectionClear g_pendingTransientSelectionClear;

enum : ushort {
	cmMrMultiFileSelectionChanged = 4901,
	cmMrMultiFileMatchPrev = 4902,
	cmMrMultiFileMatchNext = 4903,
	cmMrMultiDone = 4951,
	cmMrMultiReplace = 4952,
	cmMrMultiReplaceAll = 4953,
	cmMrMultiSkip = 4954
};

enum : uchar {
	kMultiFilespecHistoryId = 243,
	kMultiPathHistoryId = 244
};

enum class PromptReplaceDecision : unsigned char {
	Replace = 0,
	Skip = 1,
	ReplaceAll = 2,
	Cancel = 3
};

enum class PromptSearchDecision : unsigned char {
	Next = 0,
	Stop = 1,
	Cancel = 2
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
	ToggleWordWrap = 7,
	SetLeftMargin = 8,
	SetRightMargin = 9,
	JustifyParagraph = 10,
	SortColumnBlockToggle = 11,
	ForceSave = 12,
	ExitDirtySaveAll = 13
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
	KeymapActionDispatchEntry{"MRMAC_CURSOR_WORD_LEFT", KeymapDispatchKind::EditorCommand, cmWordLeft, KeymapWindowMethod::None, KeymapCustomAction::None},
	KeymapActionDispatchEntry{"MRMAC_CURSOR_WORD_RIGHT", KeymapDispatchKind::EditorCommand, cmWordRight, KeymapWindowMethod::None, KeymapCustomAction::None},
	KeymapActionDispatchEntry{"MRMAC_CURSOR_TOP_OF_WINDOW", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::CursorTopOfWindow, KeymapCustomAction::None},
	KeymapActionDispatchEntry{"MRMAC_CURSOR_BOTTOM_OF_WINDOW", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::CursorBottomOfWindow, KeymapCustomAction::None},
	KeymapActionDispatchEntry{"MRMAC_CURSOR_START_OF_BLOCK", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::CursorStartOfBlock, KeymapCustomAction::None},
	KeymapActionDispatchEntry{"MRMAC_CURSOR_END_OF_BLOCK", KeymapDispatchKind::WindowMethod, 0, KeymapWindowMethod::CursorEndOfBlock, KeymapCustomAction::None},
	KeymapActionDispatchEntry{"MRMAC_CURSOR_GOTO_LINE", KeymapDispatchKind::AppCommand, cmMrSearchGotoLineNumber, KeymapWindowMethod::None, KeymapCustomAction::None},
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
	KeymapActionDispatchEntry{"MRMAC_BLOCK_PASTE_FROM_CLIPBOARD", KeymapDispatchKind::AppCommand, cmMrEditPasteFromBuffer, KeymapWindowMethod::None, KeymapCustomAction::None},
	KeymapActionDispatchEntry{"MRMAC_BLOCK_MARK_COLUMN", KeymapDispatchKind::AppCommand, cmMrBlockMarkColumns, KeymapWindowMethod::None, KeymapCustomAction::None},
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
	KeymapActionDispatchEntry{"MRMAC_FILE_SAVE", KeymapDispatchKind::AppCommand, cmMrFileSave, KeymapWindowMethod::None, KeymapCustomAction::None},
	KeymapActionDispatchEntry{"MR_SAVE_BLOCK_TO_FILE", KeymapDispatchKind::AppCommand, cmMrBlockSaveToDisk, KeymapWindowMethod::None, KeymapCustomAction::None},
	KeymapActionDispatchEntry{"MR_LOAD_BLOCK_FROM_FILE", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::LoadBlockFromFile},
	KeymapActionDispatchEntry{"MR_TEXT_CENTER_LINE", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::CenterLine},
	KeymapActionDispatchEntry{"MR_TEXT_REFORMAT_PARAGRAPH", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ReformatParagraph},
	KeymapActionDispatchEntry{"MR_TOGGLE_WORD_WRAP", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ToggleWordWrap},
	KeymapActionDispatchEntry{"MR_SET_LEFT_MARGIN", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SetLeftMargin},
	KeymapActionDispatchEntry{"MR_SET_RIGHT_MARGIN", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SetRightMargin},
	KeymapActionDispatchEntry{"MR_JUSTIFY_PARAGRAPH", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::JustifyParagraph},
	KeymapActionDispatchEntry{"MR_SORT_COLUMN_BLOCK_TOGGLE", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SortColumnBlockToggle},
	KeymapActionDispatchEntry{"MR_FILE_FORCE_SAVE", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ForceSave},
	KeymapActionDispatchEntry{"MR_EXIT_DIRTY_SAVE_ALL", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::ExitDirtySaveAll},
};

const char *placeholderCommandTitle(ushort command) {
	switch (command) {
		case cmMrFileOpen:
			return "File / Open";
		case cmMrFileLoad:
			return "File / Load";
		case cmMrFileSave:
			return "File / Save";
		case cmMrFileSaveAs:
			return "File / Save As";
		case cmMrFileInformation:
			return "File / Information";
		case cmMrFileMerge:
			return "File / Merge";
		case cmMrFilePrint:
			return "File / Print";
		case cmMrFileShellToDos:
			return "File / Shell";

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
		case cmMrHelpAbout:
			return "Help / About";

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

std::optional<int> randomAccessMarkIndexFromSequence(std::string_view sequenceText) {
	const auto sequence = MRKeymapSequence::parse(sequenceText);

	if (!sequence || sequence->empty())
		return std::nullopt;
	const MRKeymapToken &token = sequence->tokens().back();
	if (token.baseKey() != MRKeymapBaseKey::Printable || token.modifiers() != 0)
		return std::nullopt;
	if (token.printableKey() < '1' || token.printableKey() > '9')
		return std::nullopt;
	return token.printableKey() - '0';
}

MREditWindow *effectiveKeymapWindow(MREditWindow *targetWindow) {
	return targetWindow != nullptr ? targetWindow : currentEditWindow();
}

bool dispatchEditorCommandEvent(MREditWindow *targetWindow, ushort command) {
	MREditWindow *window = effectiveKeymapWindow(targetWindow);
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	TEvent event;

	if (editor == nullptr)
		return false;
	std::memset(&event, 0, sizeof(event));
	event.what = evCommand;
	event.message.command = command;
	editor->handleEvent(event);
	return true;
}

bool dispatchApplicationCommandEvent(ushort command) {
	TApplication *application = dynamic_cast<TApplication *>(TProgram::application);
	TEvent event;

	if (application == nullptr)
		return false;
	std::memset(&event, 0, sizeof(event));
	event.what = evCommand;
	event.message.command = command;
	application->handleEvent(event);
	return true;
}

bool dispatchKeymapWindowMethod(MREditWindow *targetWindow, KeymapWindowMethod method) {
	MREditWindow *window = effectiveKeymapWindow(targetWindow);

	if (window == nullptr)
		return false;
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
	if (title == nullptr)
		title = "Command";
	messageBox(mfInformation | mfOKButton, "%s\n\nPlaceholder implementation for now.", title);
}

std::string utf8FromCodepoint(std::uint32_t codepoint);

std::vector<CharacterTableEntry> buildAsciiTableEntries() {
	std::vector<CharacterTableEntry> entries;
	entries.reserve(224);
	for (int code = 0; code <= 255; ++code) {
		if (code >= 128 && code < 160)
			continue;
		CharacterTableEntry entry;
		entry.text = std::string(1, static_cast<char>(code));
		std::ostringstream detail;
		detail << "ASCII " << std::dec << std::setw(3) << std::setfill('0') << code
		       << " 0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << code
		       << " ";
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
			if (code == 160)
				detail << "NO-BREAK SPACE";
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
	static constexpr Range kEmojiRanges[] = {
	    {0x2300, 0x23FF}, {0x2600, 0x27BF}, {0x1F1E6, 0x1F1FF},
	    {0x1F300, 0x1F5FF}, {0x1F600, 0x1F64F}, {0x1F680, 0x1F6FF},
	    {0x1F780, 0x1F7FF}, {0x1F900, 0x1F9FF}, {0x1FA70, 0x1FAFF}
	};
	std::vector<CharacterTableEntry> entries;
	for (const Range &range : kEmojiRanges)
		for (std::uint32_t codepoint = range.first; codepoint <= range.last; ++codepoint) {
			CharacterTableEntry entry;
			std::ostringstream detail;
			entry.text = utf8FromCodepoint(codepoint);
			entry.label = entry.text;
			detail << "U+" << std::hex << std::uppercase << std::setw(codepoint > 0xFFFF ? 5 : 4)
			       << std::setfill('0') << codepoint;
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
	CharacterTableView(const TRect &bounds, std::vector<CharacterTableEntry> entries,
	                   int columns, int cellWidth, TScrollBar *scrollBar)
	    : TView(bounds), entries(std::move(entries)), columns(columns), cellWidth(cellWidth),
	      scrollBar(scrollBar) {
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
			if (y < 0 || y >= blankRow || x >= size.x)
				continue;
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
				if ((event.mouse.eventFlags & meDoubleClick) != 0 && owner != nullptr)
					message(owner, evCommand, cmOK, this);
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
				if (owner != nullptr)
					message(owner, evCommand, cmOK, this);
				clearEvent(event);
				return;
			}
		}
		if (event.what == evBroadcast && event.message.command == cmScrollBarChanged &&
		    event.message.infoPtr == scrollBar) {
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
		if (static_cast<int>(text.size()) + 1 < cellWidth)
			text = " " + text;
		cell.moveChar(0, ' ', attr, static_cast<ushort>(cellWidth));
		cell.moveStr(0, text.c_str(), attr, static_cast<ushort>(cellWidth));
		writeLine(x, y, static_cast<short>(std::min(cellWidth, static_cast<int>(size.x - x))), 1, cell);
	}

	void drawDetail(short y, TColorAttr attr) {
		if (entries.empty() || y < 0 || y >= size.y)
			return;
		TDrawBuffer row;
		std::string text = entries[selectedIndex].label;
		if (!entries[selectedIndex].detail.empty()) {
			if (!text.empty())
				text += " ";
			text += entries[selectedIndex].detail;
		}
		const int detailWidth = strwidth(text.c_str());
		const int start = std::max(0, (static_cast<int>(size.x) - detailWidth) / 2);
		row.moveChar(0, ' ', attr, size.x);
		row.moveStr(static_cast<ushort>(start), text.c_str(), attr,
		            static_cast<ushort>(std::max(0, size.x - start)));
		writeLine(0, y, size.x, 1, row);
	}

	bool moveSelection(ushort keyCode) {
		if (entries.empty())
			return false;
		std::size_t next = selectedIndex;
		switch (keyCode) {
			case kbLeft:
				next = selectedIndex == 0 ? entries.size() - 1 : selectedIndex - 1;
				break;
			case kbRight:
				next = (selectedIndex + 1) % entries.size();
				break;
			case kbUp:
				next = selectedIndex < static_cast<std::size_t>(columns)
				           ? selectedIndex
				           : selectedIndex - static_cast<std::size_t>(columns);
				break;
			case kbDown:
				next = std::min(entries.size() - 1,
				                selectedIndex + static_cast<std::size_t>(columns));
				break;
			case kbHome:
				next = 0;
				break;
			case kbEnd:
				next = entries.size() - 1;
				break;
			case kbPgUp:
				next = selectedIndex < static_cast<std::size_t>(columns * 4)
				           ? 0
				           : selectedIndex - static_cast<std::size_t>(columns * 4);
				break;
			case kbPgDn:
				next = std::min(entries.size() - 1,
				                selectedIndex + static_cast<std::size_t>(columns * 4));
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
		if (columns <= 0)
			return 0;
		return static_cast<int>((entries.size() + static_cast<std::size_t>(columns - 1)) /
		                        static_cast<std::size_t>(columns));
	}

	[[nodiscard]] int visibleRows() const noexcept {
		return std::max(1, static_cast<int>(size.y) - 2);
	}

	[[nodiscard]] int maxScrollOffset() const noexcept {
		return std::max(0, totalRows() - visibleRows());
	}

	void setScrollOffset(int offset) {
		const int clamped = std::max(0, std::min(offset, maxScrollOffset()));
		if (clamped == scrollOffset)
			return;
		scrollOffset = clamped;
		updateScrollBar();
		drawView();
	}

	void updateScrollBar() {
		if (scrollBar == nullptr)
			return;
		scrollBar->setParams(scrollOffset, 0, maxScrollOffset(), visibleRows(), 1);
	}

	void ensureSelectedVisible() {
		if (columns <= 0)
			return;
		const int row = static_cast<int>(selectedIndex) / columns;
		if (row < scrollOffset)
			scrollOffset = row;
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
	CharacterTableDialog(const CharacterTableLayout &layout, std::vector<CharacterTableEntry> entries)
	    : TWindowInit(&TDialog::initFrame),
	      MRDialogFoundation(mr::dialogs::centeredDialogRect(layout.width, layout.height),
	                         layout.title, layout.width, layout.height) {
		const std::array buttons{
		    mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault},
		    mr::dialogs::DialogButtonSpec{"C~a~ncel", cmCancel, bfNormal},
		    mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics =
		    mr::dialogs::measureUniformButtonRow(buttons, 2);
		const int buttonLeft = (layout.width - metrics.rowWidth) / 2;
		scrollBar = new TScrollBar(TRect(layout.width - 2, 2, layout.width - 1, layout.height - 5));
		tableView = new CharacterTableView(TRect(2, 2, layout.width - 2, layout.height - 5),
		                                    std::move(entries), layout.columns, layout.cellWidth,
		                                    scrollBar);
		insert(tableView);
		insert(scrollBar);
		mr::dialogs::insertUniformButtonRow(*this, buttonLeft, layout.height - 3, 2, buttons);
		tableView->select();
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evCommand && event.message.command == cmHelp) {
			messageBox(mfInformation | mfOKButton,
			           "Use arrow keys to select a character. Enter posts it to the focused editor window.");
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

	NumericInputDialog(const char *title, const char *label, const char *helpText, int initialValue, int minValue, int maxValue, const Layout &layout)
	    : TWindowInit(&TDialog::initFrame), MRDialogFoundation(mr::dialogs::centeredDialogRect(layout.width, layout.height), title, layout.width, layout.height),
	      mHelpText(helpText != nullptr ? helpText : ""), mMinValue(minValue), mMaxValue(maxValue) {
		char buffer[32] = {0};

			std::snprintf(buffer, sizeof(buffer), "%d", initialValue);
			mInputField = new TInputLine(TRect(layout.inputLeft, 2, layout.inputRight, 3), layout.inputRight - layout.inputLeft);
			if (label != nullptr && label[0] != '\0')
				insert(new TLabel(TRect(2, 2, layout.inputLeft, 3), label, mInputField));
			insert(mInputField);
			if (layout.showHelp) {
				const std::array buttons{
				    mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault},
				    mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal},
				    mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
				mr::dialogs::insertUniformButtonRow(*this, layout.buttonLeft, layout.buttonY,
				                                    layout.buttonGap, buttons);
			} else {
				const std::array buttons{
				    mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault},
				    mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
				mr::dialogs::insertUniformButtonRow(*this, layout.buttonLeft, layout.buttonY,
				                                    layout.buttonGap, buttons);
			}
			mInputField->setData(buffer);
		setDialogValidationHook([this]() {
			MRScrollableDialog::DialogValidationResult result;
			int ignored = 0;

			result.valid = tryReadValue(ignored);
			if (!result.valid)
				result.warningText = "Please enter an integer in range.";
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

		if (mInputField == nullptr)
			return false;
		const_cast<TInputLine *>(mInputField)->getData(buffer);
		parsed = std::strtol(buffer, &endPtr, 10);
		if (endPtr == buffer || !trimAscii(endPtr != nullptr ? endPtr : "").empty())
			return false;
		if (parsed < mMinValue || parsed > mMaxValue)
			return false;
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
	if (win == nullptr || editor == nullptr || text.empty())
		return true;
	if (win->isReadOnly()) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction,
		                               "Window is read-only.",
		                               mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return true;
	}
	if (!editor->insertBufferText(text)) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction,
		                               "Unable to insert selected character.",
		                               mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
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

	if (targetWindow == nullptr || targetWindow->getEditor() == nullptr)
		return true;
	if (targetWindow->isReadOnly()) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction,
		                               "Window is read-only.",
		                               mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
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
	if (result == cmOK)
		selectedText = dialog->selectedText();
	TObject::destroy(dialog);
	if (result == cmOK)
		return insertTextIntoWindow(targetWindow, selectedText);
	return true;
}

std::string normalizedSearchPath(const std::filesystem::path &path) {
	std::error_code ec;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);

	if (ec || normalized.empty()) {
		ec.clear();
		normalized = std::filesystem::absolute(path, ec);
	}
	if (ec || normalized.empty())
		normalized = path.lexically_normal();
	std::string result = normalized.lexically_normal().string();
	for (char &ch : result)
		if (ch == '\\')
			ch = '/';
	return result;
}

std::vector<std::string> splitFilespecTokens(std::string_view literal) {
	std::vector<std::string> tokens;
	std::istringstream in{std::string(literal)};
	std::string token;

	while (in >> token) {
		token = trimAscii(token);
		if (token.empty())
			continue;
		for (char &ch : token)
			if (ch == '\\')
				ch = '/';
		tokens.push_back(token);
	}
	if (tokens.empty())
		tokens.push_back("*.*");
	return tokens;
}

bool filespecMatchesPath(const std::filesystem::path &candidatePath, const std::filesystem::path &startingPath,
                         const std::vector<std::string> &tokens) {
	std::string baseName = candidatePath.filename().string();
	std::error_code relEc;
	std::filesystem::path relativePath = std::filesystem::relative(candidatePath, startingPath, relEc);
	std::string relativeText = relEc ? std::string() : relativePath.lexically_normal().string();
	std::string fullText = normalizedSearchPath(candidatePath);

	for (char &ch : relativeText)
		if (ch == '\\')
			ch = '/';
	for (const std::string &token : tokens) {
		const bool hasPathSeparator = token.find('/') != std::string::npos;
		const char *subject = nullptr;

		if (hasPathSeparator) {
			if (!relativeText.empty() && relativeText != "." &&
			    relativeText.rfind("../", 0) != 0 && relativeText != "..")
				subject = relativeText.c_str();
			else
				subject = fullText.c_str();
		} else
			subject = baseName.c_str();
		if (subject != nullptr && fnmatch(token.c_str(), subject, 0) == 0)
			return true;
	}
	return false;
}

void appendCandidateUnique(const std::filesystem::path &path, bool inMemory, MREditWindow *window,
                           std::vector<MultiFileSearchCandidate> &outCandidates,
                           std::map<std::string, std::size_t> &seen) {
	const std::string normalized = normalizedSearchPath(path);
	auto it = seen.find(normalized);

	if (normalized.empty())
		return;
	if (it != seen.end()) {
		MultiFileSearchCandidate &entry = outCandidates[it->second];
		if (inMemory) {
			entry.inMemory = true;
			if (entry.window == nullptr)
				entry.window = window;
		}
		return;
	}
	seen.emplace(normalized, outCandidates.size());
	outCandidates.push_back(MultiFileSearchCandidate{normalized, window, inMemory});
}

std::vector<MultiFileSearchCandidate> collectMultiFileSearchCandidates(const MRMultiSearchDialogOptions &options) {
	std::vector<MultiFileSearchCandidate> candidates;
	std::map<std::string, std::size_t> seen;
	std::filesystem::path startingPath = normalizeConfiguredPathInput(options.startingPath);
	std::vector<std::string> filespecTokens = splitFilespecTokens(options.filespec);
	std::error_code ec;

	if (startingPath.empty()) {
		startingPath = std::filesystem::current_path(ec);
		if (ec)
			startingPath = ".";
	}
	startingPath = startingPath.lexically_normal();
	if (std::filesystem::is_regular_file(startingPath, ec) && !ec) {
		if (filespecMatchesPath(startingPath, startingPath.parent_path(), filespecTokens))
			appendCandidateUnique(startingPath, false, nullptr, candidates, seen);
	} else if (std::filesystem::is_directory(startingPath, ec) && !ec) {
		if (options.searchSubdirectories) {
			std::filesystem::recursive_directory_iterator it(
			    startingPath, std::filesystem::directory_options::skip_permission_denied, ec);
			const std::filesystem::recursive_directory_iterator end;
			for (; !ec && it != end; it.increment(ec)) {
				if (!it->is_regular_file(ec) || ec) {
					ec.clear();
					continue;
				}
				if (!filespecMatchesPath(it->path(), startingPath, filespecTokens))
					continue;
				appendCandidateUnique(it->path(), false, nullptr, candidates, seen);
			}
		} else {
			std::filesystem::directory_iterator it(startingPath, std::filesystem::directory_options::skip_permission_denied,
			                                       ec);
			const std::filesystem::directory_iterator end;
			for (; !ec && it != end; it.increment(ec)) {
				if (!it->is_regular_file(ec) || ec) {
					ec.clear();
					continue;
				}
				if (!filespecMatchesPath(it->path(), startingPath, filespecTokens))
					continue;
				appendCandidateUnique(it->path(), false, nullptr, candidates, seen);
			}
		}
	}

	if (options.searchFilesInMemory) {
		std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
		for (MREditWindow *window : windows) {
			MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
			std::filesystem::path windowPath;
			if (editor == nullptr || !editor->hasPersistentFileName())
				continue;
			windowPath = editor->persistentFileName();
			if (windowPath.empty())
				continue;
			if (!filespecMatchesPath(windowPath, startingPath, filespecTokens))
				continue;
			appendCandidateUnique(windowPath, true, window, candidates, seen);
		}
	}

	std::sort(candidates.begin(), candidates.end(),
	          [](const MultiFileSearchCandidate &lhs, const MultiFileSearchCandidate &rhs) {
		          return lhs.normalizedPath < rhs.normalizedPath;
	          });
	return candidates;
}

[[maybe_unused]] TRect centeredDialogRect(short width, short height) {
	return mr::dialogs::centeredDialogRect(width, height);
}

std::string escapeRegexLiteral(std::string_view value) {
	static constexpr const char *kMetaChars = R"(\.^$|()[]{}*+?-)";
	std::string escaped;

	escaped.reserve(value.size() * 2);
	for (char ch : value) {
		if (std::strchr(kMetaChars, ch) != nullptr)
			escaped.push_back('\\');
		escaped.push_back(ch);
	}
	return escaped;
}

std::string buildSearchPatternExpression(const std::string &pattern, MRSearchTextType type) {
	if (type == MRSearchTextType::Pcre)
		return pattern;

	const std::string literal = escapeRegexLiteral(pattern);
	if (type == MRSearchTextType::Word)
		return std::string("\\b") + literal + "\\b";
	return literal;
}

const char *wrappedSearchMessage(MRSearchDirection direction) {
	return direction == MRSearchDirection::Backward ? "search wrapped to EOF"
	                                                : "search wrapped to TOF";
}

constexpr const char *kSearchTextRequiredMessage = "Search text must not be empty.";
constexpr const char *kNoMarkedBlockSelectedMessage = "No marked block selected.";
constexpr const char *kNoPreviousSearchMessage = "No previous search.";
constexpr const char *kNoPreviousMultiFileSearchListMessage = "No previous multi-file search list.";
constexpr const char *kNoCommandSpecifiedMessage = "No command specified.";
constexpr const char *kNoBackgroundMacroTasksMessage =
    "No background macro tasks are running in this window.";
constexpr const char *kWindowReadOnlyMessage = "Window is read-only.";
constexpr const char *kChooseAnotherWindowForBlockMessage =
    "Choose another window for inter-window block operation.";
constexpr const char *kNoMarkedBlockInSourceWindowMessage =
    "No block marked in the selected source window.";
constexpr const char *kNoExternalProgramTaskMessage =
    "No external program task is running in this window.";
constexpr const char *kStopProgramBeforeRestartMessage =
    "Stop the current program before restarting it.";
constexpr const char *kNoRestartableCommandMessage =
    "No restartable command is associated with this window.";

void postSearchWarning(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, std::string(text),
	                               mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
}

void postSearchError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, std::string(text),
	                               mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

void postDialogWarning(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string(text),
	                               mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
}

void postDialogError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string(text),
	                               mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

void persistSearchDialogSettingsSnapshot() {
	std::string errorText;

	if (!persistConfiguredSettingsSnapshot(&errorText) && !errorText.empty())
		postSearchError("Settings save failed: " + errorText);
}

void postNoHitsWarning() {
	mr::messageline::postTimed(mr::messageline::Owner::HeroEventFollowup, "no hits found",
	                           mr::messageline::Kind::Warning, std::chrono::seconds(5),
	                           mr::messageline::kPriorityMedium);
}

void postMultiSearchStartedWarning() {
	mr::messageline::postTimed(mr::messageline::Owner::HeroEventFollowup, "searching ...",
	                           mr::messageline::Kind::Warning, std::chrono::seconds(5),
	                           mr::messageline::kPriorityMedium);
	if (TProgram::application != nullptr)
		TProgram::application->drawView();
}

void postSearchCancelledError() {
	mr::messageline::postTimed(mr::messageline::Owner::HeroEventFollowup, "search cancelled",
	                           mr::messageline::Kind::Error, std::chrono::seconds(5),
	                           mr::messageline::kPriorityHigh);
}

void postMultiSearchProgress(std::size_t filesSearched, std::size_t totalHits) {
	if (filesSearched == 0 && totalHits == 0)
		return;
	const std::string message = "files searched: " + std::to_string(filesSearched) + ", " +
	                            std::to_string(totalHits) + " hits";
	mr::messageline::postTimed(mr::messageline::Owner::HeroEventFollowup, message,
	                           mr::messageline::Kind::Info, std::chrono::seconds(5),
	                           mr::messageline::kPriorityMedium);
}

bool shouldCancelLongRunningSearch() {
	auto pollEscFromTarget = [](TView *target) {
		TEvent event;
		if (target == nullptr)
			return false;
		while (target->eventAvail()) {
			target->getEvent(event);
			if (event.what == evKeyDown && TKey(event.keyDown) == TKey(kbEsc))
				return true;
			target->putEvent(event);
			break;
		}
		return false;
	};

	if (pollEscFromTarget(TProgram::application != nullptr ? static_cast<TView *>(TProgram::application)
	                                                       : static_cast<TView *>(TProgram::deskTop)))
		return true;
	return pollEscFromTarget(static_cast<TView *>(TProgram::deskTop));
}

[[maybe_unused]] void armTransientSelectionClear(const std::string &normalizedPath, std::size_t start,
                                                 std::size_t end) {
	g_pendingTransientSelectionClear.active = true;
	g_pendingTransientSelectionClear.normalizedPath = normalizedPath;
	g_pendingTransientSelectionClear.start = std::min(start, end);
	g_pendingTransientSelectionClear.end = std::max(start, end);
}

struct SearchPreviewParts {
	std::string text;
	std::size_t matchOffset = 0;
	std::size_t matchLength = 0;
	std::size_t matchLineOffset = 0;
	std::size_t matchLineLength = 0;
	std::string previousLine;
	std::string matchLine;
	std::string nextLine;
};

SearchPreviewParts previewForMatch(const std::string &text, std::size_t start, std::size_t end) {
	auto sanitizeLine = [](std::string value) {
		for (char &ch : value) {
			unsigned char uch = static_cast<unsigned char>(ch);
			if (ch == '\t' || ch == '\r' || ch == '\n' || uch < 32 || uch >= 127)
				ch = ' ';
		}
		return value;
	};
	const std::size_t safeStart = std::min(start, text.size());
	const std::size_t safeEnd = std::min(std::max(end, safeStart), text.size());
	std::size_t highlightStart = safeStart;
	std::size_t highlightEnd = safeEnd;

	if (highlightEnd <= highlightStart && !text.empty()) {
		if (highlightStart < text.size())
			highlightEnd = highlightStart + 1;
		else {
			highlightStart = text.size() - 1;
			highlightEnd = text.size();
		}
	}

	const std::size_t lineStart = text.rfind('\n', highlightStart == 0 ? 0 : highlightStart - 1);
	const std::size_t left = (lineStart == std::string::npos) ? 0 : lineStart + 1;
	const std::size_t lineEnd = text.find('\n', highlightStart);
	const std::size_t right = (lineEnd == std::string::npos) ? text.size() : lineEnd;
	const std::size_t windowLeft = highlightStart > 24 ? highlightStart - 24 : left;
	const std::size_t windowRight = std::min(right, highlightEnd + 24);
	std::string leftText = text.substr(windowLeft, highlightStart - windowLeft);
	std::string matchText = text.substr(highlightStart, highlightEnd - highlightStart);
	std::string rightText = text.substr(highlightEnd, windowRight - highlightEnd);
	SearchPreviewParts parts;

	parts.matchOffset = leftText.size();
	parts.matchLength = matchText.size();
	parts.matchLineOffset = highlightStart >= left ? highlightStart - left : 0;
	parts.matchLineLength = highlightEnd - highlightStart;
	parts.text = leftText + matchText + rightText;
	parts.text = sanitizeLine(parts.text);
	parts.matchLine = sanitizeLine(text.substr(left, right - left));
	if (left > 0) {
		const std::size_t prevEnd = left - 1;
		const std::size_t prevStartBreak = text.rfind('\n', prevEnd == 0 ? 0 : prevEnd - 1);
		const std::size_t prevStart = prevStartBreak == std::string::npos ? 0 : prevStartBreak + 1;
		parts.previousLine = sanitizeLine(text.substr(prevStart, prevEnd - prevStart));
	}
	if (right < text.size()) {
		const std::size_t nextStart = right + 1;
		const std::size_t nextEndBreak = text.find('\n', nextStart);
		const std::size_t nextEnd = nextEndBreak == std::string::npos ? text.size() : nextEndBreak;
		parts.nextLine = sanitizeLine(text.substr(nextStart, nextEnd - nextStart));
	}
	return parts;
}

std::size_t centeredPreviewLeft(const std::string &line, std::size_t matchOffset, std::size_t matchLength,
                                std::size_t width) {
	if (line.empty() || width == 0 || line.size() <= width)
		return 0;
	const std::size_t safeOffset = std::min(matchOffset, line.size());
	const std::size_t safeLength = std::min(matchLength, line.size() - safeOffset);
	const std::size_t center = safeOffset + safeLength / 2;
	const std::size_t maxLeft = line.size() - width;
	std::size_t left = center > width / 2 ? center - width / 2 : 0;

	if (safeLength > 0) {
		const std::size_t minLeft = (safeOffset + safeLength > width) ? safeOffset + safeLength - width : 0;
		const std::size_t maxLeftForVisibleStart = safeOffset;
		left = std::max(left, minLeft);
		left = std::min(left, maxLeftForVisibleStart);
	}
	return std::min(left, maxLeft);
}

void lineColumnForOffset(const std::string &text, std::size_t offset, std::size_t &line, std::size_t &column) {
	const std::size_t safe = std::min(offset, text.size());
	std::size_t currentLine = 1;
	std::size_t lastLineStart = 0;

	for (std::size_t i = 0; i < safe; ++i)
		if (text[i] == '\n') {
			++currentLine;
			lastLineStart = i + 1;
		}
	line = currentLine;
	column = safe - lastLineStart + 1;
}

bool collectRegexMatches(const std::string &text, pcre2_code *code, std::vector<SearchMatchEntry> &outMatches) {
	pcre2_match_data *matchData = nullptr;
	std::size_t seek = 0;

	outMatches.clear();
	matchData = pcre2_match_data_create_from_pattern(code, nullptr);
	if (matchData == nullptr)
		return false;
	while (seek <= text.size()) {
		int rc = pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(text.data()),
		                     static_cast<PCRE2_SIZE>(text.size()), static_cast<PCRE2_SIZE>(seek), 0, matchData,
		                     nullptr);
		PCRE2_SIZE *ovector = nullptr;
		std::size_t start = 0;
		std::size_t end = 0;
		SearchMatchEntry entry;

		if (rc < 0)
			break;
		ovector = pcre2_get_ovector_pointer(matchData);
		start = static_cast<std::size_t>(ovector[0]);
		end = static_cast<std::size_t>(ovector[1]);
		if (end < start || end > text.size())
			break;
		entry.start = start;
		entry.end = end;
		lineColumnForOffset(text, start, entry.line, entry.column);
		{
			SearchPreviewParts preview = previewForMatch(text, start, end);
			entry.preview = preview.text;
			entry.previewMatchOffset = preview.matchOffset;
			entry.previewMatchLength = preview.matchLength;
		}
		outMatches.push_back(entry);
		if (end > seek)
			seek = end;
		else
			++seek;
	}
	pcre2_match_data_free(matchData);
	return true;
}

void activateMatch(MREditWindow *win, const SearchMatchEntry &match, const std::string &pattern,
                   const MRSearchDialogOptions &options);

class FoundListView : public TListViewer {
  public:
	FoundListView(const TRect &bounds, TScrollBar *aScrollBar, MREditWindow *aWindow,
	              const std::vector<SearchMatchEntry> &aEntries, const std::string &aPattern,
	              const MRSearchDialogOptions &aOptions) noexcept
	    : TListViewer(bounds, 1, nullptr, aScrollBar), window(aWindow), entries(aEntries),
	      pattern(aPattern), options(aOptions) {
		setRange(static_cast<short>(entries.size()));
	}

	void getText(char *dest, short item, short maxLen) override {
		std::size_t copyLen = 0;

		if (dest == nullptr || maxLen <= 0)
			return;
		if (item < 0 || static_cast<std::size_t>(item) >= entries.size()) {
			dest[0] = EOS;
			return;
		}
		copyLen = static_cast<std::size_t>(maxLen - 1);
		std::strncpy(dest, entries[static_cast<std::size_t>(item)].preview.c_str(), copyLen);
		dest[copyLen] = EOS;
	}

	void draw() override {
		TDrawBuffer buffer;
		const bool active = (state & (sfSelected | sfActive)) == (sfSelected | sfActive);
		const TColorAttr normalColor = getColor(active ? 1 : 2);
		const TColorAttr selectedColor = getColor(4);
		const TColorAttr focusedColor =
		    active ? static_cast<TColorAttr>(getColor(3)) : selectedColor;

		for (short row = 0; row < size.y; ++row) {
			short item = topItem + row;
			bool isFocusedRow = active && item == focused && range > 0;
			TColorAttr rowColor = normalColor;
			TColorAttr matchColor = selectedColor;

			if (isFocusedRow) {
				rowColor = focusedColor;
				matchColor = selectedColor;
				setCursor(1, row);
			} else if (item < range && isSelected(item)) {
				rowColor = selectedColor;
				matchColor = focusedColor;
			}

			buffer.moveChar(0, ' ', rowColor, size.x);
			if (item < range) {
				const SearchMatchEntry &entry = entries[static_cast<std::size_t>(item)];
				ushort x = 1;
				ushort limit = static_cast<ushort>(std::max(0, size.x - 1));
				auto drawSegment = [&](const std::string &segment, TColorAttr color) {
					for (char ch : segment) {
						if (x >= limit)
							break;
						buffer.putChar(x, static_cast<uchar>(ch));
						buffer.putAttribute(x, color);
						++x;
					}
				};
				std::size_t splitA = std::min(entry.previewMatchOffset, entry.preview.size());
				std::size_t splitB =
				    std::min(splitA + entry.previewMatchLength, entry.preview.size());
				drawSegment(entry.preview.substr(0, splitA), rowColor);
				drawSegment(entry.preview.substr(splitA, splitB - splitA), matchColor);
				drawSegment(entry.preview.substr(splitB), rowColor);
			}
			writeLine(0, row, size.x, 1, buffer);
		}
	}

	void handleEvent(TEvent &event) override {
		const bool isDoubleClickActivation =
		    event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0 &&
		    (event.mouse.eventFlags & meDoubleClick) != 0;
		const short oldFocused = focused;

		TListViewer::handleEvent(event);
		if (focused != oldFocused)
			previewFocusedItem();
		if (isDoubleClickActivation && focused >= 0 && focused < range) {
			message(owner, evCommand, cmOK, nullptr);
			clearEvent(event);
		}
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter &&
		    focused >= 0 && focused < range) {
			message(owner, evCommand, cmOK, nullptr);
			clearEvent(event);
		}
	}

	void previewFocusedItem() {
		if (focused < 0 || focused >= range || static_cast<std::size_t>(focused) >= entries.size() ||
		    window == nullptr)
			return;
		activateMatch(window, entries[static_cast<std::size_t>(focused)], pattern, options);
	}

  private:
	MREditWindow *window;
	const std::vector<SearchMatchEntry> &entries;
	const std::string &pattern;
	const MRSearchDialogOptions &options;
};

bool showFoundListDialog(MREditWindow *win, const std::string &pattern,
                         const MRSearchDialogOptions &options,
                         const std::vector<SearchMatchEntry> &matches, std::size_t &selectedIndex) {
	MRDialogFoundation *dialog = nullptr;
	TScrollBar *scrollBar = nullptr;
	FoundListView *listView = nullptr;
	ushort result = cmCancel;
	const int visibleRows = std::max<int>(6, std::min<int>(static_cast<int>(matches.size()), 13));
	const short width = 92;
	const short height = static_cast<short>(visibleRows + 8);
	const short buttonY = static_cast<short>(height - 3);

	if (TProgram::deskTop == nullptr || matches.empty())
		return false;
	dialog = mr::dialogs::createScrollableDialog("FOUND LIST", width, height);
	scrollBar = new TScrollBar(TRect(width - 3, 2, width - 2, height - 4));
	dialog->insert(scrollBar);
	listView = new FoundListView(TRect(2, 2, width - 3, height - 4), scrollBar, win, matches, pattern, options);
	if (selectedIndex < matches.size())
		listView->focusItemNum(static_cast<short>(selectedIndex));
	listView->previewFocusedItem();
	dialog->insert(listView);
	{
		const std::array buttons{
		    mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault},
		    mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics =
		    mr::dialogs::measureUniformButtonRow(buttons, 1);
		mr::dialogs::insertUniformButtonRow(*dialog, (width - metrics.rowWidth) / 2, buttonY, 1,
		                                    buttons);
	}
	dialog->setDialogValidationHook([listView]() {
		MRScrollableDialog::DialogValidationResult result;
		result.valid = listView != nullptr && listView->focused >= 0;
		if (!result.valid)
			result.warningText = "Select a match.";
		return result;
	});
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	if (result == cmOK && listView->focused >= 0 &&
	    static_cast<std::size_t>(listView->focused) < matches.size()) {
		selectedIndex = static_cast<std::size_t>(listView->focused);
		TObject::destroy(dialog);
		return true;
	}
	TObject::destroy(dialog);
	return false;
}

PromptReplaceDecision promptReplaceDecisionDialog(const std::string &title, const SearchPreviewParts &preview,
                                                  const std::string &replacement) {
	class PromptPreviewView : public TView {
	  public:
		PromptPreviewView(const TRect &bounds, const SearchPreviewParts &preview,
		                  const std::string &replacement)
		    : TView(bounds), beforeText(preview.matchLine), beforeMatchOffset(preview.matchLineOffset),
		      beforeMatchLength(preview.matchLineLength), afterText(), afterMatchOffset(preview.matchLineOffset),
		      afterMatchLength(replacement.size()) {
			const std::size_t safeOffset = std::min(beforeMatchOffset, beforeText.size());
			const std::size_t safeLength = std::min(beforeMatchLength, beforeText.size() - safeOffset);

			afterText = beforeText.substr(0, safeOffset);
			afterText += replacement;
			if (safeOffset + safeLength <= beforeText.size())
				afterText += beforeText.substr(safeOffset + safeLength);
			for (char &ch : afterText)
				if (ch == '\t' || ch == '\r' || ch == '\n' ||
				    static_cast<unsigned char>(ch) < 32 ||
				    static_cast<unsigned char>(ch) >= 127)
					ch = ' ';
			beforeMatchOffset = safeOffset;
			beforeMatchLength = safeLength;
		}

		void draw() override {
			TDrawBuffer b;
			const TColorAttr normal = getColor(1);
			const TColorAttr accent = static_cast<TColorAttr>(getColor(3));
			const std::size_t width = static_cast<std::size_t>(std::max<int>(0, size.x));
			const std::size_t beforeLeft = centeredPreviewLeft(beforeText, beforeMatchOffset, beforeMatchLength, width);
			const std::size_t afterLeft = centeredPreviewLeft(afterText, afterMatchOffset, afterMatchLength, width);
			auto drawLine = [&](short y, const std::string &line, std::size_t lineLeft, std::size_t markOffset,
			                    std::size_t markLength) {
				const std::size_t effectiveLeft =
				    line.size() <= width ? 0 : std::min(lineLeft, line.size() - width);
				b.moveChar(0, ' ', normal, size.x);
				for (ushort x = 0; x < static_cast<ushort>(size.x); ++x) {
					const std::size_t source = effectiveLeft + static_cast<std::size_t>(x);
					char ch = source < line.size() ? line[source] : ' ';
					const bool inMark = source >= markOffset && source < (markOffset + markLength);
					b.putChar(x, static_cast<uchar>(ch));
					b.putAttribute(x, inMark ? accent : normal);
				}
				writeLine(0, y, size.x, 1, b);
			};

			for (short y = 0; y < size.y; ++y) {
				b.moveChar(0, ' ', normal, size.x);
				writeLine(0, y, size.x, 1, b);
			}
			const short centerY = size.y / 2;
			drawLine(static_cast<short>(std::max(0, centerY - 1)), beforeText, beforeLeft, beforeMatchOffset,
			         beforeMatchLength);
			b.moveChar(0, ' ', normal, size.x);
			b.moveStr(static_cast<ushort>(std::max(0, (size.x - 2) / 2)), "->", getColor(3), size.x);
			writeLine(0, centerY, size.x, 1, b);
			drawLine(static_cast<short>(std::min<int>(size.y - 1, centerY + 1)), afterText, afterLeft,
			         afterMatchOffset, afterMatchLength);
		}

	  private:
		std::string beforeText;
		std::size_t beforeMatchOffset;
		std::size_t beforeMatchLength;
		std::string afterText;
		std::size_t afterMatchOffset;
		std::size_t afterMatchLength;
	};

	MRDialogFoundation *dialog = nullptr;
	ushort result = cmCancel;

	if (TProgram::deskTop == nullptr)
		return PromptReplaceDecision::Cancel;
	dialog = mr::dialogs::createScrollableDialog(title.c_str(), 88, 10);
	dialog->insert(new PromptPreviewView(TRect(2, 2, 86, 5), preview, replacement));
	{
		const std::array buttons{
		    mr::dialogs::DialogButtonSpec{"~R~eplace", cmOK, bfDefault},
		    mr::dialogs::DialogButtonSpec{"~S~kip", cmNo, bfNormal},
		    mr::dialogs::DialogButtonSpec{"Replace ~A~ll", cmYes, bfNormal},
		    mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics =
		    mr::dialogs::measureUniformButtonRow(buttons, 2);
		mr::dialogs::insertUniformButtonRow(*dialog, (88 - metrics.rowWidth) / 2, 6, 2, buttons);
	}
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
	if (result == cmOK)
		return PromptReplaceDecision::Replace;
	if (result == cmNo)
		return PromptReplaceDecision::Skip;
	if (result == cmYes)
		return PromptReplaceDecision::ReplaceAll;
	return PromptReplaceDecision::Cancel;
}

PromptSearchDecision promptSearchDecisionDialog(const SearchPreviewParts &preview) {
	class SearchPromptPreviewView : public TView {
	  public:
		SearchPromptPreviewView(const TRect &bounds, const SearchPreviewParts &preview)
		    : TView(bounds), preview(preview) {
		}

		void draw() override {
			TDrawBuffer b;
			const TColorAttr normal = getColor(1);
			const TColorAttr accent = static_cast<TColorAttr>(getColor(3));
			const std::size_t width = static_cast<std::size_t>(std::max<int>(0, size.x));
			const std::size_t lineLeft =
			    centeredPreviewLeft(preview.matchLine, preview.matchLineOffset, preview.matchLineLength, width);
			auto drawLine = [&](short y, const std::string &line, bool highlightMatch) {
				const std::size_t effectiveLeft =
				    line.size() <= width ? 0 : std::min(lineLeft, line.size() - width);
				b.moveChar(0, ' ', normal, size.x);
				for (ushort x = 0; x < static_cast<ushort>(size.x); ++x) {
					const std::size_t source = effectiveLeft + static_cast<std::size_t>(x);
					char ch = source < line.size() ? line[source] : ' ';
					const bool inMatch =
					    highlightMatch && source >= preview.matchLineOffset &&
					    source < (preview.matchLineOffset + preview.matchLineLength);
					b.putChar(x, static_cast<uchar>(ch));
					b.putAttribute(x, inMatch ? accent : normal);
				}
				writeLine(0, y, size.x, 1, b);
			};

			for (short y = 0; y < size.y; ++y) {
				b.moveChar(0, ' ', normal, size.x);
				writeLine(0, y, size.x, 1, b);
			}
			const short centerY = size.y / 2;
			drawLine(static_cast<short>(std::max(0, centerY - 1)), preview.previousLine, false);
			drawLine(centerY, preview.matchLine, true);
			drawLine(static_cast<short>(std::min<int>(size.y - 1, centerY + 1)), preview.nextLine, false);
		}

	  private:
		SearchPreviewParts preview;
	};

	MRDialogFoundation *dialog = nullptr;
	ushort result = cmCancel;

	if (TProgram::deskTop == nullptr)
		return PromptSearchDecision::Cancel;
	dialog = mr::dialogs::createScrollableDialog("SEARCH", 88, 10);
	dialog->insert(new SearchPromptPreviewView(TRect(2, 2, 86, 5), preview));
	{
		const std::array buttons{
		    mr::dialogs::DialogButtonSpec{"~N~ext", cmOK, bfDefault},
		    mr::dialogs::DialogButtonSpec{"~S~top", cmNo, bfNormal},
		    mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics =
		    mr::dialogs::measureUniformButtonRow(buttons, 2);
		mr::dialogs::insertUniformButtonRow(*dialog, (88 - metrics.rowWidth) / 2, 6, 2, buttons);
	}
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
	if (result == cmOK)
		return PromptSearchDecision::Next;
	if (result == cmNo)
		return PromptSearchDecision::Stop;
	return PromptSearchDecision::Cancel;
}

bool promptSearchPattern(std::string &pattern, MRSearchDialogOptions &options) {
	enum { kInputBufferSize = 256 };
	char patternInput[kInputBufferSize];
	ushort typeChoice = 0;
	ushort directionChoice = options.direction == MRSearchDirection::Backward ? 1 : 0;
	ushort modeChoice = 0;
	ushort optionMask = 0;
	ushort result = cmCancel;
	MRDialogFoundation *dialog = nullptr;
	TInputLine *patternField = nullptr;
	TRadioButtons *typeField = nullptr;
	TRadioButtons *directionField = nullptr;
	TRadioButtons *modeField = nullptr;
	TCheckBoxes *optionsField = nullptr;

	std::memset(patternInput, 0, sizeof(patternInput));
	if (TProgram::deskTop == nullptr)
		return false;
	if (!g_searchUiState.pattern.empty())
		strnzcpy(patternInput, g_searchUiState.pattern.c_str(), sizeof(patternInput));

	switch (options.textType) {
		case MRSearchTextType::Literal:
			typeChoice = 0;
			break;
		case MRSearchTextType::Pcre:
			typeChoice = 1;
			break;
		case MRSearchTextType::Word:
			typeChoice = 2;
			break;
	}
	switch (options.mode) {
		case MRSearchMode::StopFirst:
			modeChoice = 0;
			break;
		case MRSearchMode::PromptNext:
			modeChoice = 1;
			break;
		case MRSearchMode::ListAll:
			modeChoice = 2;
			break;
	}
	if (options.caseSensitive)
		optionMask |= 0x0001;
	if (options.globalSearch)
		optionMask |= 0x0002;
	if (options.restrictToMarkedBlock)
		optionMask |= 0x0004;
	if (options.searchAllWindows)
		optionMask |= 0x0008;

	dialog = mr::dialogs::createScrollableDialog("SEARCH", 96, 22);
	patternField = new TInputLine(TRect(15, 2, 93, 3), kInputBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 2, 15, 3), "Search ~f~or:", patternField));
	dialog->insert(patternField);
	dialog->insert(new TStaticText(TRect(3, 4, 10, 5), "Type:"));
	typeField = new TRadioButtons(
	    TRect(3, 5, 40, 8),
	    new TSItem(" ~L~iteral",
	               new TSItem(" ~R~egular expressions (PCRE)",
	                          new TSItem(" ~W~ord/Phrase search", nullptr))));
	dialog->insert(typeField);
	dialog->insert(new TStaticText(TRect(42, 4, 53, 5), "Direction:"));
	directionField = new TRadioButtons(TRect(42, 5, 59, 8),
	                                   new TSItem(" ~F~orward", new TSItem(" ~B~ackward", nullptr)));
	dialog->insert(directionField);
	dialog->insert(new TStaticText(TRect(61, 4, 67, 5), "Mode:"));
	modeField = new TRadioButtons(TRect(61, 5, 93, 8),
	                              new TSItem(" ~S~top on first occurrence",
	                                         new TSItem(" ~P~rompt for next match",
	                                                    new TSItem(" L~i~st all occurrences", nullptr))));
	dialog->insert(modeField);
	dialog->insert(new TStaticText(TRect(3, 10, 12, 11), "Options:"));
	optionsField = new TCheckBoxes(
	    TRect(3, 11, 40, 16),
	    new TSItem("~C~ase sensitive",
	               new TSItem("~G~lobal search",
	                          new TSItem("~R~estrict to marked block",
	                                     new TSItem("Search all ~w~indows", nullptr)))));
	dialog->insert(optionsField);
	{
		const std::array buttons{
		    mr::dialogs::DialogButtonSpec{"~G~o", cmOK, bfDefault},
		    mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics =
		    mr::dialogs::measureUniformButtonRow(buttons, 3);
		mr::dialogs::insertUniformButtonRow(*dialog, (76 - metrics.rowWidth) / 2, 18, 3, buttons);
	}
	patternField->setData(patternInput);
	typeField->setData(&typeChoice);
	directionField->setData(&directionChoice);
	modeField->setData(&modeChoice);
	optionsField->setData(&optionMask);
	dialog->setDialogValidationHook([patternField]() {
		MRScrollableDialog::DialogValidationResult result;
		char text[kInputBufferSize] = {0};

		if (patternField != nullptr)
			patternField->getData(text);
		result.valid = !trimAscii(text).empty();
		if (!result.valid)
			result.warningText = kSearchTextRequiredMessage;
		return result;
	});
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel) {
		patternField->getData(patternInput);
		typeField->getData(&typeChoice);
		directionField->getData(&directionChoice);
		modeField->getData(&modeChoice);
		optionsField->getData(&optionMask);
		pattern = trimAscii(patternInput);
		options.textType = typeChoice == 1 ? MRSearchTextType::Pcre
		                                   : (typeChoice == 2 ? MRSearchTextType::Word : MRSearchTextType::Literal);
		options.direction = directionChoice == 1 ? MRSearchDirection::Backward : MRSearchDirection::Forward;
		options.mode = modeChoice == 1 ? MRSearchMode::PromptNext
		                               : (modeChoice == 2 ? MRSearchMode::ListAll : MRSearchMode::StopFirst);
		options.caseSensitive = (optionMask & 0x0001) != 0;
		options.globalSearch = (optionMask & 0x0002) != 0;
		options.restrictToMarkedBlock = (optionMask & 0x0004) != 0;
		options.searchAllWindows = (optionMask & 0x0008) != 0;
		static_cast<void>(setConfiguredSearchDialogOptions(options));
	}
	TObject::destroy(dialog);
	return result != cmCancel;
}

bool promptReplaceValues(std::string &pattern, std::string &replacement, MRSarDialogOptions &options) {
	enum { kPatternBufferSize = 256, kReplacementBufferSize = 256 };
	char patternInput[kPatternBufferSize];
	char replacementInput[kReplacementBufferSize];
	ushort typeChoice = 0;
	ushort directionChoice = options.direction == MRSearchDirection::Backward ? 1 : 0;
	ushort modeChoice = 0;
	ushort leaveCursorChoice =
	    options.leaveCursorAt == MRSarLeaveCursor::StartOfReplaceString ? 1 : 0;
	ushort optionMask = 0;
	ushort result = cmCancel;
	MRDialogFoundation *dialog = nullptr;
	TInputLine *patternField = nullptr;
	TInputLine *replacementField = nullptr;
	TRadioButtons *typeField = nullptr;
	TRadioButtons *directionField = nullptr;
	TRadioButtons *modeField = nullptr;
	TRadioButtons *leaveCursorField = nullptr;
	TCheckBoxes *optionsField = nullptr;

	std::memset(patternInput, 0, sizeof(patternInput));
	std::memset(replacementInput, 0, sizeof(replacementInput));
	if (TProgram::deskTop == nullptr)
		return false;
	if (!g_searchUiState.pattern.empty())
		strnzcpy(patternInput, g_searchUiState.pattern.c_str(), sizeof(patternInput));
	if (!g_searchUiState.replacement.empty())
		strnzcpy(replacementInput, g_searchUiState.replacement.c_str(), sizeof(replacementInput));

	switch (options.textType) {
		case MRSearchTextType::Literal:
			typeChoice = 0;
			break;
		case MRSearchTextType::Pcre:
			typeChoice = 1;
			break;
		case MRSearchTextType::Word:
			typeChoice = 2;
			break;
	}
	switch (options.mode) {
		case MRSarMode::ReplaceFirst:
			modeChoice = 0;
			break;
		case MRSarMode::PromptEach:
			modeChoice = 1;
			break;
		case MRSarMode::ReplaceAll:
			modeChoice = 2;
			break;
	}
	if (options.caseSensitive)
		optionMask |= 0x0001;
	if (options.globalSearch)
		optionMask |= 0x0002;
	if (options.restrictToMarkedBlock)
		optionMask |= 0x0004;
	if (options.searchAllWindows)
		optionMask |= 0x0008;

	dialog = mr::dialogs::createScrollableDialog("SEARCH AND REPLACE", 92, 24);
	patternField = new TInputLine(TRect(17, 2, 89, 3), kPatternBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 2, 16, 3), "Search ~f~or:", patternField));
	dialog->insert(patternField);
	replacementField = new TInputLine(TRect(17, 4, 89, 5), kReplacementBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 4, 16, 5), "Replace ~w~ith:", replacementField));
	dialog->insert(replacementField);
	dialog->insert(new TStaticText(TRect(3, 6, 10, 7), "Type:"));
	typeField = new TRadioButtons(
	    TRect(3, 7, 37, 10),
	    new TSItem(" ~L~iteral",
	               new TSItem(" ~R~egular expressions (PCRE)",
	                          new TSItem(" ~W~ord/Phrase search", nullptr))));
	dialog->insert(typeField);
	dialog->insert(new TStaticText(TRect(39, 6, 50, 7), "Direction:"));
	directionField = new TRadioButtons(TRect(39, 7, 56, 10),
	                                   new TSItem(" ~F~orward", new TSItem(" ~B~ackward", nullptr)));
	dialog->insert(directionField);
	dialog->insert(new TStaticText(TRect(58, 6, 64, 7), "Mode:"));
	modeField = new TRadioButtons(
	    TRect(58, 7, 89, 10),
	    new TSItem(" Replace ~f~irst occurrence",
	               new TSItem(" ~P~rompt for each replace",
	                          new TSItem(" Replace ~a~ll occurrences", nullptr))));
	dialog->insert(modeField);
	dialog->insert(new TStaticText(TRect(3, 12, 20, 13), "Leave cursor at:"));
	leaveCursorField =
	    new TRadioButtons(TRect(3, 13, 37, 15),
	                      new TSItem(" ~E~nd of replace string",
	                                 new TSItem(" ~S~tart of replace string", nullptr)));
	dialog->insert(leaveCursorField);
	dialog->insert(new TStaticText(TRect(39, 12, 48, 13), "Options:"));
	optionsField = new TCheckBoxes(
	    TRect(39, 13, 72, 18),
	    new TSItem("~C~ase sensitive",
	               new TSItem("~G~lobal search",
	                          new TSItem("~R~estrict to marked block",
	                                     new TSItem("Search all ~w~indows", nullptr)))));
	dialog->insert(optionsField);
	{
		const std::array buttons{
		    mr::dialogs::DialogButtonSpec{"~G~o", cmOK, bfDefault},
		    mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics =
		    mr::dialogs::measureUniformButtonRow(buttons, 3);
		mr::dialogs::insertUniformButtonRow(*dialog, (92 - metrics.rowWidth) / 2, 20, 3, buttons);
	}
	patternField->setData(patternInput);
	replacementField->setData(replacementInput);
	typeField->setData(&typeChoice);
	directionField->setData(&directionChoice);
	modeField->setData(&modeChoice);
	leaveCursorField->setData(&leaveCursorChoice);
	optionsField->setData(&optionMask);
	dialog->setDialogValidationHook([patternField]() {
		MRScrollableDialog::DialogValidationResult result;
		char text[kPatternBufferSize] = {0};

		if (patternField != nullptr)
			patternField->getData(text);
		result.valid = !trimAscii(text).empty();
		if (!result.valid)
			result.warningText = kSearchTextRequiredMessage;
		return result;
	});
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel) {
		patternField->getData(patternInput);
		replacementField->getData(replacementInput);
		typeField->getData(&typeChoice);
		directionField->getData(&directionChoice);
		modeField->getData(&modeChoice);
		leaveCursorField->getData(&leaveCursorChoice);
		optionsField->getData(&optionMask);
		pattern = trimAscii(patternInput);
		replacement = replacementInput;
		options.textType = typeChoice == 1 ? MRSearchTextType::Pcre
		                                   : (typeChoice == 2 ? MRSearchTextType::Word : MRSearchTextType::Literal);
		options.direction = directionChoice == 1 ? MRSearchDirection::Backward : MRSearchDirection::Forward;
		options.mode = modeChoice == 1 ? MRSarMode::PromptEach
		                               : (modeChoice == 2 ? MRSarMode::ReplaceAll : MRSarMode::ReplaceFirst);
		options.leaveCursorAt = leaveCursorChoice == 1 ? MRSarLeaveCursor::StartOfReplaceString
		                                               : MRSarLeaveCursor::EndOfReplaceString;
		options.caseSensitive = (optionMask & 0x0001) != 0;
		options.globalSearch = (optionMask & 0x0002) != 0;
		options.restrictToMarkedBlock = (optionMask & 0x0004) != 0;
		options.searchAllWindows = (optionMask & 0x0008) != 0;
		static_cast<void>(setConfiguredSarDialogOptions(options));
	}
	TObject::destroy(dialog);
	return result != cmCancel;
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
	if (!promptIntegerValue("GOTO LINE NUMBER", "", "Enter the target line number.", static_cast<int>(std::max<long>(kMinLineNumber, std::min<long>(lineNumber > 0 ? lineNumber : 1, kMaxLineNumber))), kMinLineNumber, kMaxLineNumber, value, layout))
		return false;
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

	if (dialog == nullptr)
		return false;
	if (TProgram::deskTop == nullptr) {
		TObject::destroy(dialog);
		return false;
	}
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel)
		accepted = dialog->tryReadValue(outValue);
	TObject::destroy(dialog);
	return accepted;
}

bool compileSearchRegex(const std::string &patternExpression, bool ignoreCase, pcre2_code **outCode,
                        std::string &errorText) {
	int errorCode = 0;
	int jitCode = 0;
	PCRE2_SIZE errorOffset = 0;
	uint32_t options = PCRE2_UTF | PCRE2_UCP;
	char errorBuffer[256];
	int messageLength = 0;

	*outCode = nullptr;
	if (ignoreCase)
		options |= PCRE2_CASELESS;
	*outCode = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(patternExpression.c_str()),
	                         static_cast<PCRE2_SIZE>(patternExpression.size()), options, &errorCode,
	                         &errorOffset, nullptr);
	if (*outCode != nullptr) {
		jitCode = pcre2_jit_compile(*outCode, PCRE2_JIT_COMPLETE);
		if (jitCode < 0 && jitCode != PCRE2_ERROR_JIT_BADOPTION && jitCode != PCRE2_ERROR_NOMEMORY) {
			errorText = "Regex JIT compile error: " + std::to_string(jitCode);
			pcre2_code_free(*outCode);
			*outCode = nullptr;
			return false;
		}
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

bool findRegexForwardInRange(const std::string &text, pcre2_code *code, std::size_t startOffset,
                             std::size_t rangeStart, std::size_t rangeEnd,
                             std::size_t &matchStart, std::size_t &matchEnd) {
	const std::size_t safeStart = std::min(std::max(startOffset, rangeStart), text.size());
	const std::size_t safeEnd = std::min(rangeEnd, text.size());
	std::size_t seek = safeStart;

	if (safeStart >= safeEnd)
		return false;
	while (seek < safeEnd) {
		std::size_t nextStart = 0;
		std::size_t nextEnd = 0;
		if (!findRegexForward(text, code, seek, nextStart, nextEnd))
			return false;
		if (nextStart >= safeEnd)
			return false;
		if (nextStart >= rangeStart && nextEnd <= safeEnd) {
			matchStart = nextStart;
			matchEnd = nextEnd;
			return true;
		}
		if (nextEnd > seek)
			seek = nextEnd;
		else
			++seek;
	}
	return false;
}

bool findLastRegexBeforeLimit(const std::string &text, pcre2_code *code, std::size_t limitOffset,
                              std::size_t rangeStart, std::size_t rangeEnd,
                              std::size_t &matchStart, std::size_t &matchEnd) {
	const std::size_t boundedStart = std::min(rangeStart, text.size());
	const std::size_t boundedEnd = std::min(rangeEnd, text.size());
	const std::size_t limit = std::min(std::max(limitOffset, boundedStart), boundedEnd);
	std::size_t candidateStart = 0;
	std::size_t candidateEnd = 0;
	std::size_t seek = boundedStart;
	bool found = false;

	if (boundedStart >= boundedEnd)
		return false;
	while (seek < boundedEnd) {
		std::size_t nextStart = 0;
		std::size_t nextEnd = 0;

		if (!findRegexForwardInRange(text, code, seek, boundedStart, boundedEnd, nextStart, nextEnd))
			break;
		if (nextStart >= limit)
			break;
		candidateStart = nextStart;
		candidateEnd = nextEnd;
		found = true;
		if (nextEnd > seek)
			seek = nextEnd;
		else
			++seek;
	}
	if (!found)
		return false;
	matchStart = candidateStart;
	matchEnd = candidateEnd;
	return true;
}

bool findRegexWithWrap(const std::string &text, pcre2_code *code, std::size_t startOffset,
                       MRSearchDirection direction, std::size_t rangeStart, std::size_t rangeEnd,
                       bool allowWrap, std::size_t &matchStart, std::size_t &matchEnd, bool &wrapped) {
	const std::size_t safeRangeStart = std::min(rangeStart, text.size());
	const std::size_t safeRangeEnd = std::min(rangeEnd, text.size());
	const std::size_t safeStart = std::min(std::max(startOffset, safeRangeStart), safeRangeEnd);

	wrapped = false;
	if (direction == MRSearchDirection::Forward) {
		if (findRegexForwardInRange(text, code, safeStart, safeRangeStart, safeRangeEnd, matchStart, matchEnd))
			return true;
		if (!allowWrap)
			return false;
		if (safeStart <= safeRangeStart)
			return false;
		if (findRegexForwardInRange(text, code, safeRangeStart, safeRangeStart, safeRangeEnd, matchStart, matchEnd)) {
			wrapped = true;
			return true;
		}
		return false;
	}
	if (findLastRegexBeforeLimit(text, code, safeStart, safeRangeStart, safeRangeEnd, matchStart, matchEnd))
		return true;
	if (!allowWrap)
		return false;
	if (safeStart >= safeRangeEnd)
		return false;
	if (findLastRegexBeforeLimit(text, code, safeRangeEnd, safeRangeStart, safeRangeEnd, matchStart, matchEnd)) {
		wrapped = true;
		return true;
	}
	return false;
}

void syncVmLastSearch(MREditWindow *win, bool valid, std::size_t start, std::size_t end,
                      std::size_t cursor) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::string fileName;
	if (win == nullptr)
		return;
	if (editor != nullptr && editor->hasPersistentFileName())
		fileName = editor->persistentFileName();
	mrvmUiReplaceWindowLastSearch(win, fileName, valid, start, end, cursor);
}

void clearSearchSelection(MREditWindow *win) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	const std::size_t cursor = editor != nullptr ? editor->cursorOffset() : 0;

	if (editor == nullptr)
		return;
	editor->setSelectionOffsets(cursor, cursor);
	syncVmLastSearch(win, false, 0, 0, cursor);
	editor->clearFindMarkerRanges();
	editor->refreshViewState();
}

void updateMiniMapFindMarkers(MREditWindow *win, const std::string &pattern,
                              const MRSearchDialogOptions &options) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::vector<std::pair<std::size_t, std::size_t>> ranges;
	pcre2_code *code = nullptr;
	std::string regexError;
	std::string text;
	std::vector<SearchMatchEntry> matches;
	std::size_t rangeStart = 0;
	std::size_t rangeEnd = 0;

	if (editor == nullptr)
		return;
	if (pattern.empty()) {
		editor->clearFindMarkerRanges();
		return;
	}
	if (!compileSearchRegex(buildSearchPatternExpression(pattern, options.textType), !options.caseSensitive,
	                        &code, regexError)) {
		editor->clearFindMarkerRanges();
		return;
	}
	text = editor->snapshotText();
	rangeStart = 0;
	rangeEnd = text.size();
	if (options.restrictToMarkedBlock) {
		rangeStart = std::min(editor->selectionStartOffset(), editor->selectionEndOffset());
		rangeEnd = std::max(editor->selectionStartOffset(), editor->selectionEndOffset());
		if (rangeStart >= rangeEnd) {
			pcre2_code_free(code);
			editor->clearFindMarkerRanges();
			return;
		}
	}
	static_cast<void>(collectRegexMatches(text, code, matches));
	pcre2_code_free(code);
	for (const SearchMatchEntry &match : matches) {
		if (match.start < rangeStart || match.end > rangeEnd)
			continue;
		std::size_t start = std::min(match.start, text.size());
		std::size_t end = std::min(std::max(match.end, start), text.size());
		if (end == start) {
			if (end < text.size())
				++end;
			else if (start > 0)
				--start;
		}
		if (end > start)
			ranges.push_back(std::make_pair(start, end));
	}
	editor->setFindMarkerRanges(ranges);
}

void activateMatch(MREditWindow *win, const SearchMatchEntry &match, const std::string &pattern,
                   const MRSearchDialogOptions &options) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::size_t end = std::max(match.end, match.start);

	if (editor == nullptr)
		return;
	if (end == match.start) {
		if (end < editor->bufferLength())
			++end;
		else if (match.start > 0)
			--end;
	}
	editor->setCursorOffset(match.start);
	editor->setSelectionOffsets(match.start, end);
	editor->revealCursor(True);
	syncVmLastSearch(win, true, match.start, end, match.start);
	g_searchUiState.hasPrevious = true;
	g_searchUiState.pattern = pattern;
	g_searchUiState.lastStart = match.start;
	g_searchUiState.lastEnd = end;
	g_searchUiState.lastOptions = options;
}

bool performSearch(MREditWindow *win, const std::string &pattern, std::size_t startOffset, bool updateState,
                   const MRSearchDialogOptions &options, bool showNotFoundMessage,
                   bool *outWrapped = nullptr) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	pcre2_code *code = nullptr;
	std::string regexError;
	std::string patternExpression;
	std::string text;
	std::size_t selectionStart = 0;
	std::size_t selectionEnd = 0;
	std::size_t rangeStart = 0;
	std::size_t rangeEnd = 0;
	std::size_t matchStart = 0;
	std::size_t matchEnd = 0;
	bool wrapped = false;

	if (outWrapped != nullptr)
		*outWrapped = false;
	if (editor == nullptr)
		return false;
	if (pattern.empty()) {
		postSearchError(kSearchTextRequiredMessage);
		return false;
	}
	patternExpression = buildSearchPatternExpression(pattern, options.textType);
	if (!compileSearchRegex(patternExpression, !options.caseSensitive, &code, regexError)) {
		postSearchError("Invalid search pattern: " + regexError);
		return false;
	}
	text = editor->snapshotText();
	rangeStart = 0;
	rangeEnd = text.size();
	if (options.restrictToMarkedBlock) {
		selectionStart = editor->selectionStartOffset();
		selectionEnd = editor->selectionEndOffset();
		rangeStart = std::min(selectionStart, selectionEnd);
		rangeEnd = std::max(selectionStart, selectionEnd);
		if (rangeStart >= rangeEnd) {
			pcre2_code_free(code);
			postDialogWarning(kNoMarkedBlockSelectedMessage);
			return false;
		}
	}
	if (!findRegexWithWrap(text, code, startOffset, options.direction, rangeStart, rangeEnd,
	                       options.globalSearch, matchStart, matchEnd, wrapped)) {
		pcre2_code_free(code);
		if (showNotFoundMessage)
			postSearchWarning("No match found.");
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
		g_searchUiState.lastOptions = options;
	}
	pcre2_code_free(code);
	if (outWrapped != nullptr)
		*outWrapped = wrapped;
	return true;
}

void seedHistoryOnce(uchar historyId, const std::vector<std::string> &entries) {
	static std::set<uchar> seededIds;

	if (!seededIds.insert(historyId).second)
		return;
	for (auto it = entries.rbegin(); it != entries.rend(); ++it)
		historyAdd(historyId, *it);
}

class SubmitInterceptDialog : public MRDialogFoundation {
  public:
	using SubmitHook = std::function<void()>;

	SubmitInterceptDialog(const char *title, int virtualWidth, int virtualHeight)
	    : TWindowInit(&TDialog::initFrame),
	      MRDialogFoundation(mr::dialogs::centeredDialogRect(virtualWidth, virtualHeight), title,
	                         virtualWidth, virtualHeight) {
	}

	void setSubmitHook(SubmitHook hook) {
		submitHook = std::move(hook);
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evCommand && event.message.command == cmOK) {
			clearEvent(event);
			if (submitHook)
				submitHook();
			return;
		}
		MRDialogFoundation::handleEvent(event);
	}

  private:
	SubmitHook submitHook;
};

bool promptMultiFileSearchValues(std::string &pattern, MRMultiSearchDialogOptions &options,
                                 MultiFileSearchSession &outSession) {
	enum { kFilespecBufferSize = 256, kSearchBufferSize = 256, kPathBufferSize = 256 };
	char filespecInput[kFilespecBufferSize];
	char searchInput[kSearchBufferSize];
	char pathInput[kPathBufferSize];
	ushort optionMask = 0;
	ushort result = cmCancel;
	SubmitInterceptDialog *dialog = nullptr;
	TInputLine *filespecField = nullptr;
	TInputLine *searchField = nullptr;
	TInputLine *pathField = nullptr;
	TCheckBoxes *optionsField = nullptr;
	std::vector<std::string> filespecHistory;
	std::vector<std::string> pathHistory;

	outSession = MultiFileSearchSession();
	std::memset(filespecInput, 0, sizeof(filespecInput));
	std::memset(searchInput, 0, sizeof(searchInput));
	std::memset(pathInput, 0, sizeof(pathInput));
	if (TProgram::deskTop == nullptr)
		return false;
	configuredMultiFilespecHistoryEntries(filespecHistory);
	configuredMultiPathHistoryEntries(pathHistory);
	seedHistoryOnce(kMultiFilespecHistoryId, filespecHistory);
	seedHistoryOnce(kMultiPathHistoryId, pathHistory);

	strnzcpy(filespecInput, options.filespec.empty() ? "*.*" : options.filespec.c_str(), sizeof(filespecInput));
	if (!options.searchText.empty())
		strnzcpy(searchInput, options.searchText.c_str(), sizeof(searchInput));
	else if (!g_searchUiState.pattern.empty())
		strnzcpy(searchInput, g_searchUiState.pattern.c_str(), sizeof(searchInput));
	{
		std::string initialPath = normalizeConfiguredPathInput(options.startingPath);
		if (initialPath.empty())
			initialPath = normalizeConfiguredPathInput(configuredMultiSearchDialogOptions().startingPath);
		if (initialPath.empty() && !pathHistory.empty())
			initialPath = normalizeConfiguredPathInput(pathHistory.front());
		if (initialPath.empty())
			initialPath = normalizeConfiguredPathInput(configuredLastFileDialogPath());
		if (initialPath.empty())
			initialPath = ".";
		strnzcpy(pathInput, initialPath.c_str(), sizeof(pathInput));
	}

	if (options.searchSubdirectories)
		optionMask |= 0x0001;
	if (options.caseSensitive)
		optionMask |= 0x0002;
	if (options.regularExpressions)
		optionMask |= 0x0004;
	if (options.searchFilesInMemory)
		optionMask |= 0x0008;

	dialog = new SubmitInterceptDialog("MULTIPLE FILE SEARCH", 102, 17);
	filespecField = new TInputLine(TRect(14, 2, 96, 3), kFilespecBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 2, 14, 3), "~F~ilespecs:", filespecField));
	dialog->insert(filespecField);
	dialog->insert(new THistory(TRect(96, 2, 99, 3), filespecField, kMultiFilespecHistoryId));
	searchField = new TInputLine(TRect(14, 4, 96, 5), kSearchBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 4, 14, 5), "Se~a~rch:", searchField));
	dialog->insert(searchField);
	dialog->insert(new TStaticText(TRect(3, 6, 13, 7), "Options:"));
	optionsField = new TCheckBoxes(
	    TRect(3, 7, 30, 11),
	    new TSItem("recursive ~S~earch",
	               new TSItem("~C~ase sensitive",
	                          new TSItem("~R~egular expressions",
	                                     new TSItem("search editor ~w~indows", nullptr)))));
	dialog->insert(optionsField);
	pathField = new TInputLine(TRect(14, 12, 96, 13), kPathBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 12, 14, 13), "Start a~t~:", pathField));
	dialog->insert(pathField);
	dialog->insert(new THistory(TRect(96, 12, 99, 13), pathField, kMultiPathHistoryId));
	{
		const std::array buttons{
		    mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault},
		    mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics =
		    mr::dialogs::measureUniformButtonRow(buttons, 3);
		mr::dialogs::insertUniformButtonRow(*dialog, (102 - metrics.rowWidth) / 2, 14, 3,
		                                    buttons);
	}
	filespecField->setData(filespecInput);
	searchField->setData(searchInput);
	pathField->setData(pathInput);
	optionsField->setData(&optionMask);
	dialog->setDialogValidationHook([searchField]() {
		MRScrollableDialog::DialogValidationResult result;
		char text[kSearchBufferSize] = {0};

		if (searchField != nullptr)
			searchField->getData(text);
		result.valid = !trimAscii(text).empty();
		if (!result.valid)
			result.warningText = kSearchTextRequiredMessage;
		return result;
	});
	dialog->setSubmitHook([&]() {
		char currentFilespec[kFilespecBufferSize] = {0};
		char currentSearch[kSearchBufferSize] = {0};
		char currentPath[kPathBufferSize] = {0};
		ushort currentMask = 0;
		MRMultiSearchDialogOptions currentOptions = options;
		MultiFileSearchSession session;
		std::string errorText;

		if (filespecField == nullptr || searchField == nullptr || pathField == nullptr || optionsField == nullptr)
			return;

		filespecField->getData(currentFilespec);
		searchField->getData(currentSearch);
		pathField->getData(currentPath);
		optionsField->getData(&currentMask);

		currentOptions.filespec = trimAscii(currentFilespec);
		if (currentOptions.filespec.empty())
			currentOptions.filespec = "*.*";
		currentOptions.searchSubdirectories = (currentMask & 0x0001) != 0;
		currentOptions.caseSensitive = (currentMask & 0x0002) != 0;
		currentOptions.regularExpressions = (currentMask & 0x0004) != 0;
		currentOptions.searchFilesInMemory = (currentMask & 0x0008) != 0;
		currentOptions.startingPath = normalizeConfiguredPathInput(currentPath);
		if (currentOptions.startingPath.empty())
			currentOptions.startingPath = ".";
		currentOptions.searchText = trimAscii(currentSearch);

		static_cast<void>(setConfiguredMultiSearchDialogOptions(currentOptions));
		static_cast<void>(setConfiguredLastFileDialogPath(currentOptions.startingPath));
		static_cast<void>(addConfiguredMultiFilespecHistoryEntry(currentOptions.filespec));
		static_cast<void>(addConfiguredMultiPathHistoryEntry(currentOptions.startingPath));
		historyAdd(kMultiFilespecHistoryId, currentOptions.filespec);
		historyAdd(kMultiPathHistoryId, currentOptions.startingPath);
		persistSearchDialogSettingsSnapshot();

		postMultiSearchStartedWarning();
		switch (collectMultiFileSession(session, currentOptions, currentOptions.searchText, "", false, false,
		                                errorText)) {
			case MultiFileCollectOutcome::Error:
				static_cast<void>(showMultiFileSessionCollectionError(errorText));
				return;
			case MultiFileCollectOutcome::NoHits:
				postNoHitsWarning();
				return;
			case MultiFileCollectOutcome::Cancelled:
				if (session.files.empty())
					return;
				break;
			case MultiFileCollectOutcome::Success:
				break;
		}

		options = currentOptions;
		pattern = currentOptions.searchText;
		outSession = session;
		dialog->endModal(cmOK);
	});
	dialog->finalizeLayout();
	dialog->runDialogValidation();
	result = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
	return result == cmOK;
}

bool promptMultiFileSarValues(std::string &pattern, std::string &replacement,
                              MRMultiSarDialogOptions &options, MultiFileSearchSession &outSession) {
	enum {
		kFilespecBufferSize = 256,
		kSearchBufferSize = 256,
		kReplacementBufferSize = 256,
		kPathBufferSize = 256
	};
	char filespecInput[kFilespecBufferSize];
	char searchInput[kSearchBufferSize];
	char replacementInput[kReplacementBufferSize];
	char pathInput[kPathBufferSize];
	ushort optionMask = 0;
	ushort result = cmCancel;
	SubmitInterceptDialog *dialog = nullptr;
	TInputLine *filespecField = nullptr;
	TInputLine *searchField = nullptr;
	TInputLine *replacementField = nullptr;
	TInputLine *pathField = nullptr;
	TCheckBoxes *optionsField = nullptr;
	std::vector<std::string> filespecHistory;
	std::vector<std::string> pathHistory;

	outSession = MultiFileSearchSession();
	std::memset(filespecInput, 0, sizeof(filespecInput));
	std::memset(searchInput, 0, sizeof(searchInput));
	std::memset(replacementInput, 0, sizeof(replacementInput));
	std::memset(pathInput, 0, sizeof(pathInput));
	if (TProgram::deskTop == nullptr)
		return false;
	configuredMultiFilespecHistoryEntries(filespecHistory);
	configuredMultiPathHistoryEntries(pathHistory);
	seedHistoryOnce(kMultiFilespecHistoryId, filespecHistory);
	seedHistoryOnce(kMultiPathHistoryId, pathHistory);

	strnzcpy(filespecInput, options.filespec.empty() ? "*.*" : options.filespec.c_str(), sizeof(filespecInput));
	if (!options.searchText.empty())
		strnzcpy(searchInput, options.searchText.c_str(), sizeof(searchInput));
	else if (!g_searchUiState.pattern.empty())
		strnzcpy(searchInput, g_searchUiState.pattern.c_str(), sizeof(searchInput));
	if (!options.replacementText.empty())
		strnzcpy(replacementInput, options.replacementText.c_str(), sizeof(replacementInput));
	else if (!g_searchUiState.replacement.empty())
		strnzcpy(replacementInput, g_searchUiState.replacement.c_str(), sizeof(replacementInput));
	{
		std::string initialPath = normalizeConfiguredPathInput(options.startingPath);
		if (initialPath.empty())
			initialPath = normalizeConfiguredPathInput(configuredMultiSarDialogOptions().startingPath);
		if (initialPath.empty() && !pathHistory.empty())
			initialPath = normalizeConfiguredPathInput(pathHistory.front());
		if (initialPath.empty())
			initialPath = normalizeConfiguredPathInput(configuredLastFileDialogPath());
		if (initialPath.empty())
			initialPath = ".";
		strnzcpy(pathInput, initialPath.c_str(), sizeof(pathInput));
	}

	if (options.searchSubdirectories)
		optionMask |= 0x0001;
	if (options.caseSensitive)
		optionMask |= 0x0002;
	if (options.regularExpressions)
		optionMask |= 0x0004;
	if (options.searchFilesInMemory)
		optionMask |= 0x0008;
	if (options.keepFilesOpen)
		optionMask |= 0x0010;

	dialog = new SubmitInterceptDialog("MULTIPLE FILE SEARCH AND REPLACE", 102, 20);
	filespecField = new TInputLine(TRect(14, 2, 96, 3), kFilespecBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 2, 14, 3), "~F~ilespecs:", filespecField));
	dialog->insert(filespecField);
	dialog->insert(new THistory(TRect(96, 2, 99, 3), filespecField, kMultiFilespecHistoryId));
	searchField = new TInputLine(TRect(14, 4, 96, 5), kSearchBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 4, 14, 5), "Se~a~rch:", searchField));
	dialog->insert(searchField);
	replacementField = new TInputLine(TRect(14, 6, 96, 7), kReplacementBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 6, 14, 7), "Replac~e~:", replacementField));
	dialog->insert(replacementField);
	dialog->insert(new TStaticText(TRect(3, 8, 13, 9), "Options:"));
	optionsField = new TCheckBoxes(
	    TRect(3, 9, 32, 14),
	    new TSItem("recursive ~S~earch",
	               new TSItem("~C~ase sensitive",
	                          new TSItem("~R~egular expressions",
	                                     new TSItem("search files in ~m~emory",
	                                                new TSItem("~K~eep all files open", nullptr))))));
	dialog->insert(optionsField);
	pathField = new TInputLine(TRect(14, 15, 96, 16), kPathBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 15, 16, 16), "Start ~a~t:", pathField));
	dialog->insert(pathField);
	dialog->insert(new THistory(TRect(96, 15, 99, 16), pathField, kMultiPathHistoryId));
	{
		const std::array buttons{
		    mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault},
		    mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics =
		    mr::dialogs::measureUniformButtonRow(buttons, 3);
		mr::dialogs::insertUniformButtonRow(*dialog, (102 - metrics.rowWidth) / 2, 17, 3,
		                                    buttons);
	}
	filespecField->setData(filespecInput);
	searchField->setData(searchInput);
	replacementField->setData(replacementInput);
	pathField->setData(pathInput);
	optionsField->setData(&optionMask);
	dialog->setDialogValidationHook([searchField]() {
		MRScrollableDialog::DialogValidationResult result;
		char text[kSearchBufferSize] = {0};

		if (searchField != nullptr)
			searchField->getData(text);
		result.valid = !trimAscii(text).empty();
		if (!result.valid)
			result.warningText = kSearchTextRequiredMessage;
		return result;
	});
	dialog->setSubmitHook([&]() {
		char currentFilespec[kFilespecBufferSize] = {0};
		char currentSearch[kSearchBufferSize] = {0};
		char currentReplacement[kReplacementBufferSize] = {0};
		char currentPath[kPathBufferSize] = {0};
		ushort currentMask = 0;
		MRMultiSarDialogOptions currentOptions = options;
		MRMultiSearchDialogOptions searchOptions;
		MultiFileSearchSession session;
		std::string errorText;

		if (filespecField == nullptr || searchField == nullptr || replacementField == nullptr ||
		    pathField == nullptr || optionsField == nullptr)
			return;

		filespecField->getData(currentFilespec);
		searchField->getData(currentSearch);
		replacementField->getData(currentReplacement);
		pathField->getData(currentPath);
		optionsField->getData(&currentMask);

		currentOptions.filespec = trimAscii(currentFilespec);
		if (currentOptions.filespec.empty())
			currentOptions.filespec = "*.*";
		currentOptions.searchSubdirectories = (currentMask & 0x0001) != 0;
		currentOptions.caseSensitive = (currentMask & 0x0002) != 0;
		currentOptions.regularExpressions = (currentMask & 0x0004) != 0;
		currentOptions.searchFilesInMemory = (currentMask & 0x0008) != 0;
		currentOptions.keepFilesOpen = (currentMask & 0x0010) != 0;
		currentOptions.startingPath = normalizeConfiguredPathInput(currentPath);
		if (currentOptions.startingPath.empty())
			currentOptions.startingPath = ".";
		currentOptions.searchText = trimAscii(currentSearch);
		currentOptions.replacementText = currentReplacement;

		static_cast<void>(setConfiguredMultiSarDialogOptions(currentOptions));
		static_cast<void>(setConfiguredLastFileDialogPath(currentOptions.startingPath));
		static_cast<void>(addConfiguredMultiFilespecHistoryEntry(currentOptions.filespec));
		static_cast<void>(addConfiguredMultiPathHistoryEntry(currentOptions.startingPath));
		historyAdd(kMultiFilespecHistoryId, currentOptions.filespec);
		historyAdd(kMultiPathHistoryId, currentOptions.startingPath);
		persistSearchDialogSettingsSnapshot();

		searchOptions.searchSubdirectories = currentOptions.searchSubdirectories;
		searchOptions.caseSensitive = currentOptions.caseSensitive;
		searchOptions.regularExpressions = currentOptions.regularExpressions;
		searchOptions.searchFilesInMemory = currentOptions.searchFilesInMemory;
		searchOptions.filespec = currentOptions.filespec;
		searchOptions.startingPath = currentOptions.startingPath;

		postMultiSearchStartedWarning();
		switch (collectMultiFileSession(session, searchOptions, currentOptions.searchText,
		                                currentOptions.replacementText, true,
		                                currentOptions.keepFilesOpen, errorText)) {
			case MultiFileCollectOutcome::Error:
				static_cast<void>(showMultiFileSessionCollectionError(errorText));
				return;
			case MultiFileCollectOutcome::NoHits:
				postNoHitsWarning();
				return;
			case MultiFileCollectOutcome::Cancelled:
				if (session.files.empty())
					return;
				break;
			case MultiFileCollectOutcome::Success:
				break;
		}

		options = currentOptions;
		pattern = currentOptions.searchText;
		replacement = currentOptions.replacementText;
		outSession = session;
		dialog->endModal(cmOK);
	});
	dialog->finalizeLayout();
	dialog->runDialogValidation();
	result = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
	return result == cmOK;
}

std::string baseNameFromPath(const std::string &path) {
	const std::size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool collectMatchesForMultiFileText(const std::string &text, const MultiFileSearchSession &session,
                                    std::vector<SearchMatchEntry> &outMatches, std::string &errorText) {
	pcre2_code *code = nullptr;
	const MRSearchTextType textType = session.regularExpressions ? MRSearchTextType::Pcre
	                                                              : MRSearchTextType::Literal;

	outMatches.clear();
	if (!compileSearchRegex(buildSearchPatternExpression(session.pattern, textType),
	                        !session.caseSensitive, &code, errorText))
		return false;
	static_cast<void>(collectRegexMatches(text, code, outMatches));
	pcre2_code_free(code);
	errorText.clear();
	return true;
}

MREditWindow *findOpenWindowForNormalizedPath(const std::string &normalizedPath) {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();

	for (MREditWindow *window : windows) {
		MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
		if (editor == nullptr || !editor->hasPersistentFileName())
			continue;
		if (normalizedSearchPath(editor->persistentFileName()) == normalizedPath)
			return window;
	}
	return nullptr;
}

bool ensureWindowLoadedForSessionFile(MultiFileSearchFileResult &file, bool activate, std::string &errorText) {
	MREditWindow *window = file.window;

	if (window == nullptr || window->getEditor() == nullptr ||
	    !window->getEditor()->hasPersistentFileName() ||
	    normalizedSearchPath(window->getEditor()->persistentFileName()) != file.normalizedPath) {
		window = findOpenWindowForNormalizedPath(file.normalizedPath);
		file.temporaryWindow = false;
	}
	if (window == nullptr) {
		window = createEditorWindow(file.normalizedPath.c_str());
		if (window == nullptr) {
			errorText = "Unable to create editor window.";
			return false;
		}
		if (!loadResolvedFileIntoWindow(window, file.normalizedPath, "Multi-file search load")) {
			message(window, evCommand, cmClose, nullptr);
			errorText = "Unable to load file: " + file.normalizedPath;
			return false;
		}
		file.temporaryWindow = true;
	}
	if (activate)
		static_cast<void>(mrActivateEditWindow(window));
	file.window = window;
	errorText.clear();
	return true;
}

void closeTemporarySessionWindow(MultiFileSearchFileResult &file, bool keepFilesOpen) {
	if (keepFilesOpen || !file.temporaryWindow || file.window == nullptr)
		return;
	message(file.window, evCommand, cmClose, nullptr);
	file.window = nullptr;
	file.temporaryWindow = false;
}

bool loadSessionFileText(const MultiFileSearchFileResult &file, std::string &outText,
                         std::string &errorText) {
	MREditWindow *window = file.window;
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;

	if (editor != nullptr && editor->hasPersistentFileName() &&
	    normalizedSearchPath(editor->persistentFileName()) == file.normalizedPath) {
		outText = editor->snapshotText();
		errorText.clear();
		return true;
	}
	if (!readTextFile(file.normalizedPath, outText, errorText)) {
		if (errorText.empty())
			errorText = "Unable to read file: " + file.normalizedPath;
		return false;
	}
	errorText.clear();
	return true;
}

MultiFileSearchFileResult *currentSessionFile(MultiFileSearchSession &session) {
	if (session.files.empty())
		return nullptr;
	if (session.selectedFileIndex >= session.files.size())
		session.selectedFileIndex = session.files.size() - 1;
	return &session.files[session.selectedFileIndex];
}

SearchMatchEntry *currentSessionMatch(MultiFileSearchSession &session) {
	MultiFileSearchFileResult *file = currentSessionFile(session);
	if (file == nullptr || file->matches.empty())
		return nullptr;
	if (file->selectedMatchIndex >= file->matches.size())
		file->selectedMatchIndex = file->matches.size() - 1;
	return &file->matches[file->selectedMatchIndex];
}

std::size_t sessionTotalMatchCount(const MultiFileSearchSession &session) {
	std::size_t total = 0;
	for (const MultiFileSearchFileResult &file : session.files)
		total += file.matches.size();
	return total;
}

std::size_t sessionCurrentMatchOrdinal(const MultiFileSearchSession &session) {
	if (session.files.empty() || session.selectedFileIndex >= session.files.size())
		return 0;

	std::size_t ordinal = 0;
	for (std::size_t i = 0; i < session.files.size(); ++i) {
		const MultiFileSearchFileResult &file = session.files[i];
		if (file.matches.empty())
			continue;
		if (i == session.selectedFileIndex) {
			const std::size_t index = std::min(file.selectedMatchIndex + 1, file.matches.size());
			return ordinal + index;
		}
		ordinal += file.matches.size();
	}
	return 0;
}

bool moveSessionMatch(MultiFileSearchSession &session, int direction, bool wrap) {
	if (session.files.empty())
		return false;
	if (direction == 0)
		return true;
	MultiFileSearchFileResult *file = currentSessionFile(session);
	if (file == nullptr || file->matches.empty())
		return false;
	if (direction > 0) {
		if (file->selectedMatchIndex + 1 < file->matches.size()) {
			++file->selectedMatchIndex;
			return true;
		}
		for (std::size_t i = session.selectedFileIndex + 1; i < session.files.size(); ++i)
			if (!session.files[i].matches.empty()) {
				session.selectedFileIndex = i;
				session.files[i].selectedMatchIndex = 0;
				return true;
			}
		if (wrap)
			for (std::size_t i = 0; i <= session.selectedFileIndex && i < session.files.size(); ++i)
				if (!session.files[i].matches.empty()) {
					session.selectedFileIndex = i;
					session.files[i].selectedMatchIndex = 0;
					return true;
				}
		return false;
	}

	if (file->selectedMatchIndex > 0) {
		--file->selectedMatchIndex;
		return true;
	}
	for (std::size_t i = session.selectedFileIndex; i > 0; --i)
		if (!session.files[i - 1].matches.empty()) {
			session.selectedFileIndex = i - 1;
			session.files[i - 1].selectedMatchIndex = session.files[i - 1].matches.size() - 1;
			return true;
		}
	if (wrap)
		for (std::size_t i = session.files.size(); i > session.selectedFileIndex + 1; --i)
			if (!session.files[i - 1].matches.empty()) {
				session.selectedFileIndex = i - 1;
				session.files[i - 1].selectedMatchIndex = session.files[i - 1].matches.size() - 1;
				return true;
			}
	return false;
}

struct MultiPreviewLine {
	std::string text;
	std::size_t highlightStart = 0;
	std::size_t highlightEnd = 0;
};

struct MultiPreviewBlock {
	std::vector<MultiPreviewLine> lines;
	std::size_t matchLineIndex = 0;
};

std::string sanitizePreviewLine(std::string value) {
	for (char &ch : value) {
		unsigned char uch = static_cast<unsigned char>(ch);
		if (ch == '\t' || ch == '\r' || ch == '\n' || uch < 32 || uch >= 127)
			ch = ' ';
	}
	return value;
}

bool buildMultiPreviewBlock(const std::string &text, const SearchMatchEntry &match, std::size_t rowCount,
                            MultiPreviewBlock &out) {
	const std::size_t safeStart = std::min(match.start, text.size());
	const std::size_t safeEnd = std::min(std::max(match.end, safeStart), text.size());
	std::size_t lineStart = safeStart;
	std::size_t lineEnd = text.find('\n', safeStart);
	std::vector<std::pair<std::size_t, std::size_t>> ranges;

	out.lines.clear();
	out.matchLineIndex = 0;
	if (lineEnd == std::string::npos)
		lineEnd = text.size();
	if (lineStart > 0) {
		std::size_t pos = safeStart;
		std::size_t breakPos = text.rfind('\n', pos == 0 ? 0 : pos - 1);
		lineStart = breakPos == std::string::npos ? 0 : breakPos + 1;
	}

	{
		std::vector<std::pair<std::size_t, std::size_t>> before;
		std::size_t cursor = lineStart;
		while (cursor > 0 && before.size() < rowCount / 2) {
			std::size_t prevEnd = cursor - 1;
			std::size_t prevBreak = text.rfind('\n', prevEnd == 0 ? 0 : prevEnd - 1);
			std::size_t prevStart = prevBreak == std::string::npos ? 0 : prevBreak + 1;
			before.push_back(std::make_pair(prevStart, prevEnd));
			cursor = prevStart;
		}
		ranges.assign(before.rbegin(), before.rend());
	}
	ranges.push_back(std::make_pair(lineStart, lineEnd));
	while (ranges.size() < rowCount && lineEnd < text.size()) {
		std::size_t nextStart = lineEnd + 1;
		std::size_t nextEnd = text.find('\n', nextStart);
		if (nextEnd == std::string::npos)
			nextEnd = text.size();
		ranges.push_back(std::make_pair(nextStart, nextEnd));
		lineEnd = nextEnd;
	}

	out.matchLineIndex = ranges.size() > rowCount / 2 ? rowCount / 2 : ranges.size() - 1;
	for (std::size_t i = 0; i < ranges.size(); ++i) {
		MultiPreviewLine line;
		std::size_t start = ranges[i].first;
		std::size_t end = ranges[i].second;
		line.text = sanitizePreviewLine(text.substr(start, end - start));
		if (start <= safeStart && safeStart <= end) {
			line.highlightStart = safeStart - start;
			line.highlightEnd = std::min(end, safeEnd) - start;
			out.matchLineIndex = i;
		}
		out.lines.push_back(line);
	}
	return !out.lines.empty();
}

class MultiFileListView : public TListViewer {
  public:
	MultiFileListView(const TRect &bounds, TScrollBar *aScrollBar, MultiFileSearchSession &session) noexcept
	    : TListViewer(bounds, 1, nullptr, aScrollBar), session(session) {
		setRange(static_cast<short>(session.files.size()));
	}

	void getText(char *dest, short item, short maxLen) override {
		auto trimFileNameForWidth = [](const std::string &name, std::size_t width) {
			if (name.size() <= width)
				return name;
			if (width <= 3)
				return name.substr(0, width);
			return name.substr(0, width - 3) + "...";
		};
		auto hitColumnWidth = [this]() {
			std::size_t width = 0;
			for (const MultiFileSearchFileResult &file : session.files) {
				const std::size_t hitTotal = file.matches.size();
				const std::size_t hitIndex = hitTotal == 0 ? 0 : std::min(file.selectedMatchIndex + 1, hitTotal);
				const std::size_t labelLen =
				    3 + std::to_string(hitIndex).size() + std::to_string(hitTotal).size();
				width = std::max(width, labelLen);
			}
			return std::max<std::size_t>(7, width);
		};

		if (dest == nullptr || maxLen <= 0)
			return;
		if (item < 0 || static_cast<std::size_t>(item) >= session.files.size()) {
			dest[0] = EOS;
			return;
		}
		const MultiFileSearchFileResult &file = session.files[static_cast<std::size_t>(item)];
		const std::size_t hitTotal = file.matches.size();
		const std::size_t hitIndex = hitTotal == 0 ? 0 : std::min(file.selectedMatchIndex + 1, hitTotal);
		const std::string hitLabel = "[" + std::to_string(hitIndex) + "/" + std::to_string(hitTotal) + "]";
		const std::size_t width = static_cast<std::size_t>(maxLen - 1);
		const std::size_t firstColWidth = std::min<std::size_t>(hitColumnWidth(), width);
		const std::string fileName =
		    file.fileName.empty() ? baseNameFromPath(file.normalizedPath) : file.fileName;
		std::string label = hitLabel.substr(0, firstColWidth);

		if (label.size() < firstColWidth)
			label.append(firstColWidth - label.size(), ' ');
		if (label.size() < width) {
			const std::size_t secondColWidth = width - label.size();
			label += trimFileNameForWidth(fileName, secondColWidth);
		}
		std::strncpy(dest, label.c_str(), static_cast<std::size_t>(maxLen - 1));
		dest[maxLen - 1] = EOS;
	}

	void focusItemNum(short item) override {
		TListViewer::focusItemNum(item);
		if (item >= 0 && static_cast<std::size_t>(item) < session.files.size())
			session.selectedFileIndex = static_cast<std::size_t>(item);
		message(owner, evBroadcast, cmMrMultiFileSelectionChanged, this);
	}

  private:
	MultiFileSearchSession &session;
};

class MultiPreviewHeaderView : public TView {
  public:
	MultiPreviewHeaderView(const TRect &bounds, MultiFileSearchSession &session) noexcept
	    : TView(bounds), session(session) {
	}

	void draw() override {
		TDrawBuffer b;
		const TColorAttr color = getColor(1);
		const std::size_t width = static_cast<std::size_t>(std::max<short>(0, size.x));
		const std::size_t totalMatches = sessionTotalMatchCount(session);
		const std::size_t currentMatch = sessionCurrentMatchOrdinal(session);
		const MultiFileSearchFileResult *file =
		    (session.selectedFileIndex < session.files.size()) ? &session.files[session.selectedFileIndex] : nullptr;
		const std::string path =
		    file == nullptr ? std::string() : (file->normalizedPath.empty() ? file->fileName : file->normalizedPath);
		std::string header = "[" + std::to_string(currentMatch) + "/" + std::to_string(totalMatches) + "]";

		if (!path.empty())
			header += " " + path;
		if (header.size() > width) {
			if (width <= 3)
				header = header.substr(0, width);
			else
				header = "..." + header.substr(header.size() - (width - 3));
		}
		b.moveChar(0, ' ', color, size.x);
		b.moveStr(0, header.c_str(), color, size.x);
		writeLine(0, 0, size.x, 1, b);
	}

  private:
	MultiFileSearchSession &session;
};

class MultiPreviewView : public TView {
  public:
	MultiPreviewView(const TRect &bounds, MultiFileSearchSession &session) noexcept
	    : TView(bounds), session(session) {
		options |= ofSelectable;
		eventMask |= evMouseWheel | evMouseDown | evKeyDown;
	}

	void draw() override {
		TDrawBuffer b;
		unsigned char editorTextAttr = 0;
		unsigned char editorHighlightAttr = 0;
		const TColorAttr normal =
		    configuredColorSlotOverride(13, editorTextAttr) ? TAttrPair(editorTextAttr) : getColor(1);
		const TColorAttr highlight =
		    configuredColorSlotOverride(14, editorHighlightAttr) ? TAttrPair(editorHighlightAttr) : getColor(3);
		MultiFileSearchFileResult *file = currentSessionFile(session);
		SearchMatchEntry *match = currentSessionMatch(session);
		std::string text;
		std::string error;
		MultiPreviewBlock block;
		std::size_t width = static_cast<std::size_t>(std::max<short>(0, size.x));
		std::size_t textWidth = width;
		std::size_t left = 0;

		for (short y = 0; y < size.y; ++y) {
			b.moveChar(0, ' ', normal, size.x);
			writeLine(0, y, size.x, 1, b);
		}
		if (file == nullptr || match == nullptr)
			return;
		if (!loadSessionFileText(*file, text, error) || !buildMultiPreviewBlock(text, *match, static_cast<std::size_t>(size.y), block)) {
			b.moveChar(0, ' ', normal, size.x);
			b.moveStr(0, error.empty() ? "No preview." : error.c_str(), highlight, size.x);
			writeLine(0, std::max<short>(0, size.y / 2), size.x, 1, b);
			return;
		}
		{
			const MultiPreviewLine &matchLine = block.lines[block.matchLineIndex];
			left = centeredPreviewLeft(matchLine.text, matchLine.highlightStart,
			                           matchLine.highlightEnd > matchLine.highlightStart
			                               ? matchLine.highlightEnd - matchLine.highlightStart
			                               : 1,
			                           textWidth);
		}
		for (short y = 0; y < size.y; ++y) {
			b.moveChar(0, ' ', normal, size.x);
			if (static_cast<std::size_t>(y) < block.lines.size()) {
				const MultiPreviewLine &line = block.lines[static_cast<std::size_t>(y)];
				for (ushort x = 0; x < static_cast<ushort>(textWidth); ++x) {
					const std::size_t source = left + static_cast<std::size_t>(x);
					const bool inHighlight =
					    source >= line.highlightStart && source < line.highlightEnd &&
					    line.highlightEnd > line.highlightStart;
					const char ch = source < line.text.size() ? line.text[source] : ' ';
					b.putChar(x, static_cast<uchar>(ch));
					b.putAttribute(x, inHighlight ? highlight : normal);
				}
			}
			writeLine(0, y, size.x, 1, b);
		}
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evMouseDown && containsMouse(event) &&
		    (event.mouse.buttons & mbLeftButton) != 0 &&
		    (event.mouse.eventFlags & meDoubleClick) != 0) {
			message(owner, evCommand, session.replaceMode ? cmMrMultiReplace : cmMrMultiDone, this);
			clearEvent(event);
			return;
		}
		if (event.what == evMouseWheel && containsMouse(event)) {
			if (event.mouse.wheel == mwUp || event.mouse.wheel == mwLeft) {
				message(owner, evCommand, cmMrMultiFileMatchPrev, this);
				clearEvent(event);
				return;
			}
			if (event.mouse.wheel == mwDown || event.mouse.wheel == mwRight) {
				message(owner, evCommand, cmMrMultiFileMatchNext, this);
				clearEvent(event);
				return;
			}
		}
		if (event.what == evKeyDown) {
			ushort keyCode = ctrlToArrow(event.keyDown.keyCode);
			if (keyCode == kbUp || keyCode == kbPgUp) {
				message(owner, evCommand, cmMrMultiFileMatchPrev, this);
				clearEvent(event);
				return;
			}
			if (keyCode == kbDown || keyCode == kbPgDn) {
				message(owner, evCommand, cmMrMultiFileMatchNext, this);
				clearEvent(event);
				return;
			}
		}
		TView::handleEvent(event);
	}

  private:
	MultiFileSearchSession &session;
};

enum class MultiDialogAction : unsigned char {
	Cancel = 0,
	Done = 1,
	Replace = 2,
	ReplaceAll = 3,
	Skip = 4
};

MultiDialogAction runMultiFileResultsDialog(MultiFileSearchSession &session) {
	class MultiFileResultsDialog : public MRScrollableDialog {
	  public:
		MultiFileResultsDialog(MultiFileSearchSession &session)
		    : TWindowInit(&TDialog::initFrame),
		      MRScrollableDialog(centeredSetupDialogRect(118, 28),
		                         session.replaceMode ? "MULTIPLE FILE SEARCH AND REPLACE"
		                                             : "MULTIPLE FILE SEARCH",
		                         118, 28),
		      session(session) {
			const short buttonTop = 24;
			const short rows = static_cast<short>(buttonTop - 4);
			const short listTop = 2;
			const short listBottom = static_cast<short>(listTop + rows);
			const int gap = 2;
				listScrollBar = new TScrollBar(TRect(29, listTop, 30, listBottom));
				addManaged(listScrollBar, TRect(29, listTop, 30, listBottom));
				listView = new MultiFileListView(TRect(2, listTop, 29, listBottom), listScrollBar, session);
				addManaged(listView, TRect(2, listTop, 29, listBottom));
				previewView = new MultiPreviewView(TRect(32, listTop, 116, listBottom), session);
				addManaged(previewView, TRect(32, listTop, 116, listBottom));
				previewHeaderView = new MultiPreviewHeaderView(TRect(32, 1, 116, 2), session);
				addManaged(previewHeaderView, TRect(32, 1, 116, 2));
				addManaged(new TLabel(TRect(2, 1, 16, 2), "Fi~l~es:", listView), TRect(2, 1, 16, 2));
				if (session.replaceMode) {
					const std::array buttons{
					    mr::dialogs::DialogButtonSpec{"~R~eplace", cmMrMultiReplace, bfDefault},
					    mr::dialogs::DialogButtonSpec{"Replace ~A~ll", cmMrMultiReplaceAll, bfNormal},
					    mr::dialogs::DialogButtonSpec{"~S~kip", cmMrMultiSkip, bfNormal},
					    mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
					const mr::dialogs::DialogButtonRowMetrics metrics =
					    mr::dialogs::measureUniformButtonRow(buttons, gap);
					mr::dialogs::addManagedUniformButtonRow(*this, (118 - metrics.rowWidth) / 2,
					                                        buttonTop, gap, buttons);
				} else {
					const std::array buttons{
					    mr::dialogs::DialogButtonSpec{"~D~one", cmMrMultiDone, bfDefault},
					    mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
					const mr::dialogs::DialogButtonRowMetrics metrics =
					    mr::dialogs::measureUniformButtonRow(buttons, gap);
					mr::dialogs::addManagedUniformButtonRow(*this, (118 - metrics.rowWidth) / 2,
					                                        buttonTop, gap, buttons);
				}
			initScrollIfNeeded();
			selectContent();
			if (session.selectedFileIndex < session.files.size())
				listView->focusItemNum(static_cast<short>(session.selectedFileIndex));
			else
				listView->focusItemNum(0);
			setDialogValidationHook([this]() {
				MRScrollableDialog::DialogValidationResult result;
				result.valid = currentSessionMatch(this->session) != nullptr;
				if (!result.valid)
					result.warningText = "Select a match.";
				return result;
			});
		}

		void handleEvent(TEvent &event) override {
			if (event.what == evCommand) {
				switch (event.message.command) {
					case cmMrMultiDone:
					case cmMrMultiReplace:
					case cmMrMultiReplaceAll:
					case cmMrMultiSkip:
						endModal(event.message.command);
						clearEvent(event);
						return;
				}
			}
			if (event.what == evMouseWheel) {
				if (event.mouse.wheel == mwUp || event.mouse.wheel == mwLeft)
					message(this, evCommand, cmMrMultiFileMatchPrev, this);
				else if (event.mouse.wheel == mwDown || event.mouse.wheel == mwRight)
					message(this, evCommand, cmMrMultiFileMatchNext, this);
				clearEvent(event);
				return;
			}
				MRScrollableDialog::handleEvent(event);
				if (event.what == evBroadcast && event.message.command == cmMrMultiFileSelectionChanged) {
					if (previewHeaderView != nullptr)
						previewHeaderView->drawView();
					previewView->drawView();
					clearEvent(event);
					return;
				}
				if (event.what == evCommand && event.message.command == cmMrMultiFileMatchPrev) {
					if (moveSessionMatch(session, -1, true)) {
						if (listView != nullptr)
							listView->focusItemNum(static_cast<short>(session.selectedFileIndex));
						if (previewHeaderView != nullptr)
							previewHeaderView->drawView();
						previewView->drawView();
					}
					clearEvent(event);
					return;
				}
				if (event.what == evCommand && event.message.command == cmMrMultiFileMatchNext) {
					if (moveSessionMatch(session, 1, true)) {
						if (listView != nullptr)
							listView->focusItemNum(static_cast<short>(session.selectedFileIndex));
						if (previewHeaderView != nullptr)
							previewHeaderView->drawView();
						previewView->drawView();
					}
					clearEvent(event);
					return;
			}
		}

	  private:
		MultiFileSearchSession &session;
			TScrollBar *listScrollBar = nullptr;
			MultiFileListView *listView = nullptr;
			MultiPreviewHeaderView *previewHeaderView = nullptr;
			MultiPreviewView *previewView = nullptr;
		};

	ushort result = cmCancel;
	MultiFileResultsDialog *dialog = nullptr;

	if (session.files.empty() || TProgram::deskTop == nullptr)
		return MultiDialogAction::Cancel;
	dialog = new MultiFileResultsDialog(session);
	result = mr::dialogs::execDialog(dialog);
	if (result == cmMrMultiDone)
		return MultiDialogAction::Done;
	if (result == cmMrMultiReplace)
		return MultiDialogAction::Replace;
	if (result == cmMrMultiReplaceAll)
		return MultiDialogAction::ReplaceAll;
	if (result == cmMrMultiSkip)
		return MultiDialogAction::Skip;
	return MultiDialogAction::Cancel;
}

bool activateSessionCurrentMatch(MultiFileSearchSession &session) {
	MultiFileSearchFileResult *file = currentSessionFile(session);
	SearchMatchEntry *match = currentSessionMatch(session);
	std::string error;
	std::size_t end = 0;
	MRFileEditor *editor = nullptr;
	MRSearchDialogOptions searchOptions;

	if (file == nullptr || match == nullptr)
		return false;
	if (!ensureWindowLoadedForSessionFile(*file, true, error)) {
		postSearchError(error);
		return false;
	}
	editor = file->window != nullptr ? file->window->getEditor() : nullptr;
	if (editor == nullptr)
		return false;
	end = std::max(match->end, match->start);
	if (end == match->start) {
		if (end < editor->bufferLength())
			++end;
		else if (match->start > 0)
			--end;
	}
	editor->setCursorOffset(match->start);
	file->window->applyCommittedBlockState(MREditWindow::bmStream, false, static_cast<uint>(match->start),
	                                       static_cast<uint>(end));
	editor->revealCursor(True);
	searchOptions.textType = session.regularExpressions ? MRSearchTextType::Pcre : MRSearchTextType::Literal;
	searchOptions.caseSensitive = session.caseSensitive;
	updateMiniMapFindMarkers(file->window, session.pattern, searchOptions);
	g_searchUiState.hasPrevious = true;
	g_searchUiState.pattern = session.pattern;
	g_searchUiState.lastStart = match->start;
	g_searchUiState.lastEnd = end;
	g_searchUiState.lastOptions = searchOptions;
	return true;
}

bool removeSessionFileAt(MultiFileSearchSession &session, std::size_t index) {
	if (index >= session.files.size())
		return false;
	closeTemporarySessionWindow(session.files[index], session.keepFilesOpen);
	session.files.erase(session.files.begin() + static_cast<std::ptrdiff_t>(index));
	if (session.files.empty()) {
		session.selectedFileIndex = 0;
		return true;
	}
	if (session.selectedFileIndex >= session.files.size())
		session.selectedFileIndex = session.files.size() - 1;
	return true;
}

bool refreshMatchesForSessionFile(MultiFileSearchSession &session, std::size_t fileIndex, std::string &errorText) {
	std::string text;
	std::vector<SearchMatchEntry> matches;

	if (fileIndex >= session.files.size())
		return false;
	if (!loadSessionFileText(session.files[fileIndex], text, errorText))
		return false;
	if (!collectMatchesForMultiFileText(text, session, matches, errorText))
		return false;
	session.files[fileIndex].matches.swap(matches);
	if (session.files[fileIndex].selectedMatchIndex >= session.files[fileIndex].matches.size())
		session.files[fileIndex].selectedMatchIndex = session.files[fileIndex].matches.empty()
		                                                  ? 0
		                                                  : session.files[fileIndex].matches.size() - 1;
	return true;
}

bool replaceCurrentSessionMatch(MultiFileSearchSession &session, bool advanceAfterReplace,
                                std::string &errorText) {
	MultiFileSearchFileResult *file = currentSessionFile(session);
	SearchMatchEntry *match = currentSessionMatch(session);
	MRFileEditor *editor = nullptr;
	std::size_t start = 0;
	std::size_t end = 0;
	std::size_t nextOffset = 0;
	std::size_t fileIndex = session.selectedFileIndex;

	if (file == nullptr || match == nullptr)
		return false;
	if (!ensureWindowLoadedForSessionFile(*file, false, errorText))
		return false;
	editor = file->window != nullptr ? file->window->getEditor() : nullptr;
	if (editor == nullptr) {
		errorText = "No editor available for replacement.";
		return false;
	}
	start = match->start;
	end = match->end;
	nextOffset = start + std::max<std::size_t>(1, session.replacement.size());
	if (!editor->replaceRangeAndSelect(static_cast<uint>(start), static_cast<uint>(end),
	                                   session.replacement.c_str(),
	                                   static_cast<uint>(session.replacement.size()))) {
		errorText = "Replace failed.";
		return false;
	}
	if (!file->window->saveCurrentFile()) {
		errorText = "Save failed for: " + file->normalizedPath;
		return false;
	}
	if (!refreshMatchesForSessionFile(session, fileIndex, errorText))
		return false;
	if (fileIndex >= session.files.size())
		return false;
	if (session.files[fileIndex].matches.empty()) {
		removeSessionFileAt(session, fileIndex);
		errorText.clear();
		return true;
	}
	{
		std::size_t nextIndex = 0;
		while (nextIndex < session.files[fileIndex].matches.size() &&
		       session.files[fileIndex].matches[nextIndex].start < nextOffset)
			++nextIndex;
		if (nextIndex >= session.files[fileIndex].matches.size())
			nextIndex = session.files[fileIndex].matches.size() - 1;
		session.files[fileIndex].selectedMatchIndex = nextIndex;
		session.selectedFileIndex = fileIndex;
	}
	if (advanceAfterReplace)
		static_cast<void>(moveSessionMatch(session, 1, false));
	errorText.clear();
	return true;
}

MultiFileCollectOutcome collectMultiFileSession(MultiFileSearchSession &session,
                                                const MRMultiSearchDialogOptions &options,
                                                const std::string &pattern,
                                                const std::string &replacement, bool replaceMode,
                                                bool keepFilesOpen, std::string &errorText) {
	std::vector<MultiFileSearchCandidate> candidates = collectMultiFileSearchCandidates(options);
	pcre2_code *code = nullptr;
	std::string compileError;
	MRSearchTextType textType = options.regularExpressions ? MRSearchTextType::Pcre : MRSearchTextType::Literal;
	std::size_t filesSearched = 0;
	std::size_t totalHits = 0;
	auto lastProgressAt = std::chrono::steady_clock::now();
	bool cancelled = false;

	session = MultiFileSearchSession();
	session.valid = false;
	session.replaceMode = replaceMode;
	session.caseSensitive = options.caseSensitive;
	session.regularExpressions = options.regularExpressions;
	session.pattern = pattern;
	session.replacement = replacement;
	session.keepFilesOpen = keepFilesOpen;
	if (pattern.empty()) {
		errorText = kSearchTextRequiredMessage;
		return MultiFileCollectOutcome::Error;
	}
	if (!compileSearchRegex(buildSearchPatternExpression(pattern, textType), !options.caseSensitive, &code,
	                        compileError)) {
		errorText = "Invalid search pattern: " + compileError;
		return MultiFileCollectOutcome::Error;
	}
	postMultiSearchProgress(filesSearched, totalHits);
	for (const MultiFileSearchCandidate &candidate : candidates) {
		std::string text;
		MultiFileSearchFileResult file;
		std::vector<SearchMatchEntry> matches;
		MREditWindow *window = candidate.window;
		MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
		bool loaded = false;

		if (shouldCancelLongRunningSearch()) {
			cancelled = true;
			break;
		}

		if (editor != nullptr && editor->hasPersistentFileName() &&
		    normalizedSearchPath(editor->persistentFileName()) == candidate.normalizedPath) {
			text = editor->snapshotText();
			loaded = true;
		} else if (readTextFile(candidate.normalizedPath, text))
			loaded = true;
		if (loaded)
			static_cast<void>(collectRegexMatches(text, code, matches));
		++filesSearched;
		totalHits += matches.size();
		const auto now = std::chrono::steady_clock::now();
		if (now - lastProgressAt >= std::chrono::seconds(5)) {
			postMultiSearchProgress(filesSearched, totalHits);
			lastProgressAt = now;
		}
		if (matches.empty())
			continue;
		file.normalizedPath = candidate.normalizedPath;
		file.fileName = baseNameFromPath(candidate.normalizedPath);
		file.matches.swap(matches);
		file.selectedMatchIndex = 0;
		file.startedInMemory = candidate.inMemory;
		file.window = candidate.window;
		session.files.push_back(file);
	}
	if (code != nullptr)
		pcre2_code_free(code);
	postMultiSearchProgress(filesSearched, totalHits);
	if (cancelled)
		postSearchCancelledError();
	if (session.files.empty()) {
		errorText.clear();
		return cancelled ? MultiFileCollectOutcome::Cancelled : MultiFileCollectOutcome::NoHits;
	}
	session.valid = true;
	session.selectedFileIndex = 0;
	errorText.clear();
	return cancelled ? MultiFileCollectOutcome::Cancelled : MultiFileCollectOutcome::Success;
}

void closeTemporaryWindowsForSession(MultiFileSearchSession &session) {
	for (MultiFileSearchFileResult &file : session.files)
		closeTemporarySessionWindow(file, session.keepFilesOpen);
}

bool showMultiFileSessionCollectionError(const std::string &errorText) {
	if (!errorText.empty())
		postSearchError(errorText);
	return true;
}

bool handleSearchFindText() {
	MREditWindow *win = currentEditWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	MRSearchDialogOptions options = configuredSearchDialogOptions();
	std::string pattern;

	if (editor == nullptr)
		return true;
	if (!promptSearchPattern(pattern, options)) {
		editor->clearFindMarkerRanges();
		clearSearchSelection(win);
		return true;
	}
	updateMiniMapFindMarkers(win, pattern, options);
	if (options.mode == MRSearchMode::ListAll) {
		pcre2_code *code = nullptr;
		std::string regexError;
		std::vector<SearchMatchEntry> matches;
		std::size_t selectedIndex = 0;
		std::string text = editor->snapshotText();
			const std::string patternExpression = buildSearchPatternExpression(pattern, options.textType);
			std::size_t rangeStart = 0;
			std::size_t rangeEnd = text.size();

			if (!compileSearchRegex(patternExpression, !options.caseSensitive, &code, regexError)) {
				postSearchError("Invalid search pattern: " + regexError);
				return true;
			}
		static_cast<void>(collectRegexMatches(text, code, matches));
		pcre2_code_free(code);
		if (options.restrictToMarkedBlock) {
			rangeStart = std::min(editor->selectionStartOffset(), editor->selectionEndOffset());
			rangeEnd = std::max(editor->selectionStartOffset(), editor->selectionEndOffset());
			if (rangeStart >= rangeEnd) {
				postDialogWarning(kNoMarkedBlockSelectedMessage);
				clearSearchSelection(win);
				return true;
			}
			matches.erase(
			    std::remove_if(matches.begin(), matches.end(),
			                   [&](const SearchMatchEntry &entry) {
				                   return entry.start < rangeStart || entry.end > rangeEnd;
			                   }),
			    matches.end());
		}
		if (matches.empty()) {
			postSearchWarning("No match found.");
			clearSearchSelection(win);
			return true;
		}
		if (matches.size() == 1) {
			activateMatch(win, matches[0], pattern, options);
			return true;
		}
		if (showFoundListDialog(win, pattern, options, matches, selectedIndex))
			activateMatch(win, matches[selectedIndex], pattern, options);
		else
			clearSearchSelection(win);
		return true;
	}

	if (options.searchAllWindows && options.mode == MRSearchMode::StopFirst) {
		std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
		auto it = std::find(windows.begin(), windows.end(), win);
		bool found = false;
		if (it != windows.end())
			std::rotate(windows.begin(), it, windows.end());
		for (std::size_t i = 0; i < windows.size(); ++i) {
			MREditWindow *candidate = windows[i];
			MRFileEditor *candidateEditor = candidate != nullptr ? candidate->getEditor() : nullptr;
			std::size_t startOffset = 0;
			bool wrapped = false;

			if (candidateEditor == nullptr)
				continue;
			if (options.direction == MRSearchDirection::Backward)
				startOffset = (i == 0) ? candidateEditor->cursorOffset() : candidateEditor->bufferLength();
			else
				startOffset = (i == 0) ? candidateEditor->cursorOffset() : 0;
			if (!performSearch(candidate, pattern, startOffset, true, options, false, &wrapped))
				continue;
			if (candidate != win)
				static_cast<void>(mrActivateEditWindow(candidate));
			if (wrapped)
				mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup,
				                               wrappedSearchMessage(options.direction),
				                               mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
			found = true;
			break;
		}
		if (!found) {
			postSearchWarning("No match found.");
			clearSearchSelection(win);
		}
		return true;
	}

	{
		std::size_t startOffset = editor->cursorOffset();
		bool wrapped = false;

		if (!performSearch(win, pattern, startOffset, true, options, true, &wrapped))
			return true;
		if (wrapped)
			mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup,
			                               wrappedSearchMessage(options.direction),
			                               mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
		if (options.mode == MRSearchMode::PromptNext) {
			while (true) {
				std::size_t currentStart = editor->selectionStartOffset();
				std::size_t currentEnd = editor->selectionEndOffset();
				std::size_t nextStartOffset = 0;
				SearchPreviewParts preview = previewForMatch(editor->snapshotText(), currentStart, currentEnd);
				PromptSearchDecision decision = promptSearchDecisionDialog(preview);

				if (decision == PromptSearchDecision::Cancel) {
					clearSearchSelection(win);
					break;
				}
				if (decision == PromptSearchDecision::Stop)
					break;
				if (options.direction == MRSearchDirection::Backward)
					nextStartOffset = currentStart;
				else {
					nextStartOffset = currentEnd;
					if (currentEnd <= currentStart)
						nextStartOffset = std::min(editor->bufferLength(), currentStart + 1);
				}
				if (!performSearch(win, pattern, nextStartOffset, true, options, false, &wrapped))
					break;
				if (wrapped)
					mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup,
					                               wrappedSearchMessage(options.direction),
					                               mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
			}
		}
	}
	return true;
}

bool handleSearchRepeatPrevious() {
	MREditWindow *win = currentEditWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::size_t startOffset = 0;
	bool wrapped = false;

	if (editor == nullptr)
		return true;
	if (!g_searchUiState.hasPrevious || g_searchUiState.pattern.empty()) {
		postDialogWarning(kNoPreviousSearchMessage);
		return true;
	}
	if (g_searchUiState.lastOptions.direction == MRSearchDirection::Backward)
		startOffset = g_searchUiState.lastStart;
	else {
		startOffset = g_searchUiState.lastEnd;
		if (g_searchUiState.lastEnd <= g_searchUiState.lastStart)
			startOffset = std::min(editor->bufferLength(), g_searchUiState.lastStart + 1);
	}
	if (!performSearch(win, g_searchUiState.pattern, startOffset, true, g_searchUiState.lastOptions,
	                   true, &wrapped))
		return true;
	updateMiniMapFindMarkers(win, g_searchUiState.pattern, g_searchUiState.lastOptions);
	if (wrapped)
		mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup,
		                               wrappedSearchMessage(g_searchUiState.lastOptions.direction),
		                               mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleSearchReplace() {
	MREditWindow *win = currentEditWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	MRSarDialogOptions options = configuredSarDialogOptions();
	std::string pattern;
	std::string replacement;
	std::size_t start = 0;
	std::size_t end = 0;
	std::size_t cursorTargetStart = 0;
	std::size_t cursorTargetEnd = 0;
	std::size_t replacedCount = 0;
	bool cancelledByUser = false;

	if (editor == nullptr || win == nullptr)
		return true;
	if (!promptReplaceValues(pattern, replacement, options)) {
		editor->clearFindMarkerRanges();
		clearSearchSelection(win);
		return true;
	}
	if (pattern.empty()) {
		postSearchError(kSearchTextRequiredMessage);
		return true;
	}
	{
		MRSearchDialogOptions searchOptions;
		searchOptions.textType = options.textType;
		searchOptions.direction = options.direction;
		searchOptions.mode = MRSearchMode::StopFirst;
		searchOptions.caseSensitive = options.caseSensitive;
		searchOptions.globalSearch = options.globalSearch;
		searchOptions.restrictToMarkedBlock = options.restrictToMarkedBlock;
		searchOptions.searchAllWindows = options.searchAllWindows;
		updateMiniMapFindMarkers(win, pattern, searchOptions);

		if (options.mode == MRSarMode::ReplaceFirst) {
			if (!performSearch(win, pattern, editor->cursorOffset(), false, searchOptions, true, nullptr))
				return true;
			start = editor->selectionStartOffset();
			end = editor->selectionEndOffset();
				if (end < start)
					std::swap(start, end);
				if (!editor->replaceRangeAndSelect(static_cast<uint>(start), static_cast<uint>(end),
				                                   replacement.data(), static_cast<uint>(replacement.size()))) {
					postSearchError("Replace failed.");
					return true;
				}
			cursorTargetStart = start;
			cursorTargetEnd = start + replacement.size();
			replacedCount = 1;
		} else {
			pcre2_code *code = nullptr;
			std::string regexError;
			std::string text = editor->snapshotText();
			std::vector<SearchMatchEntry> matches;
			long long delta = 0;
			const std::size_t initialCursor = editor->cursorOffset();
			std::size_t rangeStart = 0;
			std::size_t rangeEnd = text.size();
			bool forceReplaceAll = options.mode == MRSarMode::ReplaceAll;
			const bool forwardOrder = options.direction != MRSearchDirection::Backward;

				if (!compileSearchRegex(buildSearchPatternExpression(pattern, options.textType), !options.caseSensitive, &code,
				                        regexError)) {
					postSearchError("Invalid search pattern: " + regexError);
					return true;
				}
			static_cast<void>(collectRegexMatches(text, code, matches));
			pcre2_code_free(code);
			if (options.restrictToMarkedBlock) {
				rangeStart = std::min(editor->selectionStartOffset(), editor->selectionEndOffset());
				rangeEnd = std::max(editor->selectionStartOffset(), editor->selectionEndOffset());
				if (rangeStart >= rangeEnd) {
					postDialogWarning(kNoMarkedBlockSelectedMessage);
					clearSearchSelection(win);
					return true;
				}
			}
			matches.erase(std::remove_if(matches.begin(), matches.end(),
			                             [&](const SearchMatchEntry &entry) {
				                             if (entry.start < rangeStart || entry.end > rangeEnd)
					                             return true;
				                             if (options.globalSearch)
					                             return false;
				                             if (options.direction == MRSearchDirection::Backward)
					                             return entry.start >= initialCursor;
				                             return entry.start < initialCursor;
			                             }),
			              matches.end());
			if (matches.empty()) {
				postSearchWarning("No match found.");
				clearSearchSelection(win);
				return true;
			}
			if (options.direction == MRSearchDirection::Backward)
				std::reverse(matches.begin(), matches.end());
			for (const SearchMatchEntry &match : matches) {
				std::size_t currentStart = match.start;
				std::size_t currentEnd = match.end;

				if (forwardOrder) {
					currentStart = static_cast<std::size_t>(static_cast<long long>(match.start) + delta);
					currentEnd = static_cast<std::size_t>(static_cast<long long>(match.end) + delta);
				}

				if (!forceReplaceAll) {
					std::size_t promptStart = currentStart;
					std::size_t promptEnd = currentEnd;
					SearchPreviewParts promptPreview;
					if (promptEnd <= promptStart) {
						if (promptEnd < editor->bufferLength())
							++promptEnd;
						else if (promptStart > 0)
							--promptStart;
					}
					editor->setCursorOffset(promptStart);
					editor->setSelectionOffsets(promptStart, promptEnd);
					editor->revealCursor(True);
					syncVmLastSearch(win, true, promptStart, promptEnd, promptStart);
					promptPreview = previewForMatch(editor->snapshotText(), promptStart, promptEnd);
					PromptReplaceDecision decision =
					    promptReplaceDecisionDialog("SEARCH AND REPLACE", promptPreview, replacement);
					if (decision == PromptReplaceDecision::Cancel) {
						cancelledByUser = true;
						break;
					}
					if (decision == PromptReplaceDecision::Skip)
						continue;
					if (decision == PromptReplaceDecision::ReplaceAll)
						forceReplaceAll = true;
				}

					if (!editor->replaceRangeAndSelect(static_cast<uint>(currentStart), static_cast<uint>(currentEnd),
					                                   replacement.data(), static_cast<uint>(replacement.size()))) {
						postSearchError("Replace failed.");
						return true;
					}
				cursorTargetStart = currentStart;
				cursorTargetEnd = currentStart + replacement.size();
				++replacedCount;
				if (forwardOrder)
					delta += static_cast<long long>(replacement.size()) -
					         static_cast<long long>(match.end - match.start);
			}
			if (replacedCount == 0) {
				if (cancelledByUser) {
					clearSearchSelection(win);
				}
				postSearchWarning("No replacements.");
				return true;
			}
		}
		if (options.leaveCursorAt == MRSarLeaveCursor::StartOfReplaceString) {
			editor->setCursorOffset(cursorTargetStart);
			editor->setSelectionOffsets(cursorTargetStart, cursorTargetStart);
		} else {
			editor->setCursorOffset(cursorTargetEnd);
			editor->setSelectionOffsets(cursorTargetEnd, cursorTargetEnd);
		}
		editor->revealCursor(True);

		g_searchUiState.hasPrevious = true;
		g_searchUiState.pattern = pattern;
		g_searchUiState.replacement = replacement;
		g_searchUiState.lastStart = cursorTargetStart;
		g_searchUiState.lastEnd = cursorTargetEnd;
		g_searchUiState.lastOptions = searchOptions;
		syncVmLastSearch(win, true, cursorTargetStart, cursorTargetEnd, editor->cursorOffset());
		if (cancelledByUser) {
			clearSearchSelection(win);
		} else
			updateMiniMapFindMarkers(win, pattern, searchOptions);
		postSearchWarning(std::to_string(replacedCount) + " replacements");
	}
	return true;
}

bool handleSearchMultiFileSearch() {
	MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
	std::string pattern;
	for (;;) {
		MultiFileSearchSession session;
		if (!promptMultiFileSearchValues(pattern, options, session))
			return true;
		g_lastMultiFileSearchSession = session;
		switch (runMultiFileResultsDialog(g_lastMultiFileSearchSession)) {
			case MultiDialogAction::Done:
				static_cast<void>(activateSessionCurrentMatch(g_lastMultiFileSearchSession));
				return true;
			case MultiDialogAction::Cancel:
				continue;
			default:
				return true;
		}
	}
	return true;
}

bool handleSearchListFilesFromLastSearch() {
	MultiDialogAction action = MultiDialogAction::Cancel;

	if (!g_lastMultiFileSearchSession.valid || g_lastMultiFileSearchSession.files.empty()) {
		postDialogWarning(kNoPreviousMultiFileSearchListMessage);
		return true;
	}
	action = runMultiFileResultsDialog(g_lastMultiFileSearchSession);
	if (action == MultiDialogAction::Done)
		static_cast<void>(activateSessionCurrentMatch(g_lastMultiFileSearchSession));
	return true;
}

bool handleSearchMultiFileSearchReplace() {
	MRMultiSarDialogOptions sarOptions = configuredMultiSarDialogOptions();
	std::string pattern;
	std::string replacement;
	for (;;) {
		MultiFileSearchSession session;
		std::string errorText;
		std::size_t replacedCount = 0;
		bool returnToSearchDialog = false;

		if (!promptMultiFileSarValues(pattern, replacement, sarOptions, session))
			return true;

		while (!session.files.empty()) {
			const MultiDialogAction action = runMultiFileResultsDialog(session);
			if (action == MultiDialogAction::Cancel) {
				returnToSearchDialog = true;
				break;
			}
			if (action == MultiDialogAction::Skip) {
				if (!moveSessionMatch(session, 1, false))
					break;
				continue;
			}
				if (action == MultiDialogAction::ReplaceAll) {
					while (!session.files.empty()) {
						if (!replaceCurrentSessionMatch(session, false, errorText)) {
							if (!errorText.empty())
								postSearchError(errorText);
							closeTemporaryWindowsForSession(session);
							return true;
						}
					++replacedCount;
				}
				break;
			}
				if (action == MultiDialogAction::Replace) {
					if (!replaceCurrentSessionMatch(session, true, errorText)) {
						if (!errorText.empty())
							postSearchError(errorText);
						closeTemporaryWindowsForSession(session);
						return true;
					}
				++replacedCount;
			}
		}
		closeTemporaryWindowsForSession(session);
		if (returnToSearchDialog)
			continue;
		if (replacedCount == 0)
			postSearchWarning("No replacements.");
		else
			postSearchWarning(std::to_string(replacedCount) + " replacements");
		return true;
	}
	return true;
}

bool handleSearchGotoLineNumber() {
	MREditWindow *win = currentEditWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
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
	MREditWindow *target;
	MREditWindow *current = currentEditWindow();
	std::string resolvedPath;
	std::string logLine;
	bool createdTarget = false;

	if (!promptForPath("Open File", fileName, sizeof(fileName)))
		return true;
	if (!resolveReadableExistingPath(MRDialogHistoryScope::OpenFile, fileName, resolvedPath))
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
	MREditWindow *target = currentEditWindow();
	std::string resolvedPath;
	std::string logLine;
	bool createdTarget = false;

	if (!promptForPath("Load File", fileName, sizeof(fileName)))
		return true;
	if (!resolveReadableExistingPath(MRDialogHistoryScope::LoadFile, fileName, resolvedPath))
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
	MREditWindow *win;

	if (!promptForCommandLine(commandLine))
		return true;
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

bool startExternalCommandInWindow(MREditWindow *win, const std::string &commandLine, bool replaceBuffer,
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
			postSearchError("Unable to prepare communication window.");
			return false;
		}
	}
	win->setReadOnly(true);
	win->setFileChanged(false);
	win->setWindowRole(MREditWindow::wrCommunicationCommand, commandLine);
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
		postSearchError("Unable to start external command worker.");
		return false;
	}
	win->trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::ExternalIo, commandLine);

	logLine << "Started external command in communication window #" << win->bufferId() << ": "
	        << commandLine << " [task #" << taskId << "]";
	mrLogMessage(logLine.str().c_str());
	return true;
}

bool dispatchEditorCommand(ushort editorCommand, bool requiresWritable) {
	MREditWindow *win = currentEditWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

	if (win == nullptr || editor == nullptr)
		return true;
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

ushort execDialogWithDataLocal(TDialog *dialog, void *data) {
	return mr::dialogs::execDialogWithData(dialog, data);
}

void syncPersistentBlocksMenuState() {
	if (auto *mrMenuBar = dynamic_cast<MRMenuBar *>(TProgram::menuBar))
		mrMenuBar->setPersistentBlocksMenuState(configuredPersistentBlocksSetting());
}

bool handleBlockAction(bool ok, const char *failureText) {
	if (!ok && failureText != nullptr && *failureText != '\0')
		messageBox(mfInformation | mfOKButton, "%s", failureText);
	return true;
}

bool hasMarkedTextForBlockOperation(MREditWindow *win) {
	if (win == nullptr || !win->hasBlock())
		return false;
	if (win->blockStatus() == MREditWindow::bmStream &&
	    win->blockAnchorPtr() == win->blockEffectiveEndPtr())
		return false;
	return true;
}

bool promptBlockSavePath(std::string &outPath) {
	char buffer[MAXPATH] = {0};
	ushort result = cmCancel;
	MREditWindow *win = currentEditWindow();

	outPath.clear();
	mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::BlockSave, buffer, sizeof(buffer), "*.*",
	                                win != nullptr && win->currentFileName() != nullptr
	                                    ? std::string_view(win->currentFileName())
	                                    : std::string_view());
	result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::BlockSave, "*.*",
	                                                        "Save block as", "~N~ame", fdOKButton,
	                                                        buffer);
	if (result == cmCancel)
		return false;
	fexpand(buffer);
	outPath = buffer;
	if (outPath.find('*') != std::string::npos || outPath.find('?') != std::string::npos)
		return false;
	return !outPath.empty();
}

bool promptBlockLoadPath(std::string &outPath) {
	char buffer[MAXPATH] = {0};
	ushort result = cmCancel;
	MREditWindow *win = currentEditWindow();

	outPath.clear();
	mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::BlockLoad, buffer, sizeof(buffer), "*.*",
	                                win != nullptr && win->currentFileName() != nullptr
	                                    ? std::string_view(win->currentFileName())
	                                    : std::string_view());
	result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::BlockLoad, "*.*",
	                                                        "Load block from", "~N~ame", fdOpenButton,
	                                                        buffer);
	if (result == cmCancel)
		return false;
	fexpand(buffer);
	outPath = buffer;
	if (outPath.find('*') != std::string::npos || outPath.find('?') != std::string::npos)
		return false;
	return !outPath.empty();
}

bool chooseInterWindowBlockTarget(int &sourceWindowIndex) {
	MREditWindow *targetWin = currentEditWindow();
	MREditWindow *sourceWin = nullptr;

	sourceWindowIndex = 0;
	if (targetWin == nullptr)
		return false;
	sourceWin = mrShowWindowListDialog(mrwlActivateWindow, targetWin);
	if (sourceWin == nullptr)
		return false;
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

bool togglePersistentBlocksSetting() {
	MREditSetupSettings settings = configuredEditSetupSettings();
	MRSetupPaths paths;
	MRSettingsWriteReport writeReport;
	MREditorApp *app = dynamic_cast<MREditorApp *>(TProgram::application);
	std::string errorText;
	bool enabled;

	settings.persistentBlocks = !settings.persistentBlocks;
	if (!setConfiguredEditSetupSettings(settings, &errorText)) {
		postSearchError("Persistent blocks update failed: " + errorText);
		return true;
	}

	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	if (!writeSettingsMacroFile(paths, &errorText, &writeReport)) {
		postSearchError("Settings save failed: " + errorText);
		return true;
	}
	mrLogSettingsWriteReport("persistent blocks toggle", writeReport);
	if (app != nullptr && !app->applyConfiguredSettingsFromModel(&errorText)) {
		postSearchError("Settings reload failed: " + errorText);
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

bool persistEditSetupSettingsWithFeedback(const MREditSetupSettings &settings, const std::string &errorPrefix) {
	MRSetupPaths paths;
	MRSettingsWriteReport writeReport;
	MREditorApp *app = dynamic_cast<MREditorApp *>(TProgram::application);
	std::string errorText;

	if (!setConfiguredEditSetupSettings(settings, &errorText)) {
		postDialogError(errorPrefix + errorText);
		return false;
	}
	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	if (!writeSettingsMacroFile(paths, &errorText, &writeReport)) {
		postDialogError("Settings save failed: " + errorText);
		return false;
	}
	mrLogSettingsWriteReport("wordstar edit setup update", writeReport);
	if (app != nullptr && !app->applyConfiguredSettingsFromModel(&errorText)) {
		postDialogError("Settings reload failed: " + errorText);
		return false;
	}
	return true;
}

bool handleWordstarSetRightMargin() {
	MREditSetupSettings settings = configuredEditSetupSettings();
	NumericInputDialog::Layout layout = defaultNumericInputDialogLayout();
	int margin = settings.rightMargin > 0 ? settings.rightMargin : 78;

	layout.width = 30;
	layout.height = 8;
	layout.inputLeft = 4;
	layout.inputRight = 26;
	layout.buttonY = 4;
	layout.buttonLeft = 4;
	layout.buttonGap = 2;
	layout.showHelp = false;
	if (!promptIntegerValue("SET RIGHT MARGIN", "", "Set the global RIGHT_MARGIN used for paragraph formatting and wrap handling.", margin, 1, 999, margin, layout))
		return true;
	settings.rightMargin = margin;
	if (!persistEditSetupSettingsWithFeedback(settings, "Right margin update failed: "))
		return true;
	mrLogMessage(("Right margin set to " + std::to_string(margin) + ".").c_str());
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Right margin updated.", mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleWordstarSetLeftMargin(MREditWindow *window) {
	int margin = window != nullptr ? window->indentLevel() : 1;

	if (window == nullptr)
		return false;
	if (!promptIntegerValue("SET LEFT MARGIN", "~M~argin:", "Set the current window left margin used by WordStar paragraph commands.", margin, 1, 254, margin))
		return true;
	window->setIndentLevel(margin);
	mrLogMessage(("Left margin set to " + std::to_string(margin) + ".").c_str());
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Left margin updated.", mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleWordstarToggleWordWrap() {
	MREditSetupSettings settings = configuredEditSetupSettings();

	settings.wordWrap = !settings.wordWrap;
	if (!persistEditSetupSettingsWithFeedback(settings, "Word wrap update failed: "))
		return true;
	mrLogMessage(settings.wordWrap ? "Word wrap enabled." : "Word wrap disabled.");
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, settings.wordWrap ? "Word wrap: ON" : "Word wrap: OFF", mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleWordstarReformatParagraph(MREditWindow *window) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	const int rightMargin = std::max(1, configuredEditSetupSettings().rightMargin);

	if (editor == nullptr || window == nullptr || window->isReadOnly())
		return false;
	return editor->formatParagraph(window->indentLevel(), rightMargin);
}

bool handleWordstarJustifyParagraph(MREditWindow *window) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	const int rightMargin = std::max(1, configuredEditSetupSettings().rightMargin);

	if (editor == nullptr || window == nullptr || window->isReadOnly())
		return false;
	return editor->justifyParagraph(window->indentLevel(), rightMargin);
}

bool handleWordstarCenterLine(MREditWindow *window) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	const int rightMargin = std::max(1, configuredEditSetupSettings().rightMargin);

	if (editor == nullptr || window == nullptr || window->isReadOnly())
		return false;
	return editor->centerCurrentLine(window->indentLevel(), rightMargin);
}

bool handleWordstarLoadBlockFromFile(MREditWindow *window) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	std::string path;
	std::string text;
	std::string errorText;
	std::size_t start = 0;
	std::size_t end = 0;

	if (window == nullptr || editor == nullptr || window->isReadOnly())
		return false;
	if (!promptBlockLoadPath(path))
		return true;
	if (!readTextFile(path, text, errorText)) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return true;
	}
	start = editor->selectionStartOffset();
	end = editor->selectionEndOffset();
	if (end < start)
		std::swap(start, end);
	if (!editor->replaceRangeAndSelect(static_cast<uint>(start), static_cast<uint>(end), text.data(), static_cast<uint>(text.size())))
		return true;
	window->applyCommittedBlockState(static_cast<int>(MREditWindow::bmStream), false, static_cast<uint>(start), static_cast<uint>(start + text.size()));
	return true;
}

bool handleWordstarForceSave(MREditWindow *window) {
	if (window == nullptr)
		return false;
	if (window->isReadOnly()) {
		postDialogWarning(kWindowReadOnlyMessage);
		return true;
	}
	if (!window->isFileChanged())
		return true;
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
		if (window == nullptr || !window->isFileChanged())
			continue;
		dirtyWindows.push_back(window);
		dirtyItems.push_back(window->currentFileName()[0] != '\0' ? window->currentFileName() : window->getTitle(0));
	}
	if (dirtyWindows.empty())
		return dispatchApplicationCommandEvent(cmQuit);
	switch (mr::dialogs::runDialogDirtyListGating("EXIT MR", "Unsaved windows exist.", "Dirty windows:", dirtyItems, "Save all")) {
		case mr::dialogs::UnsavedChangesChoice::Save:
			for (MREditWindow *window : dirtyWindows) {
				static_cast<void>(mrActivateEditWindow(window));
				if (!saveCurrentEditWindow())
					return true;
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

	if (win == nullptr || !win->isCommunicationWindow())
		return true;
	taskCount = win->trackedTaskCount(mr::coprocessor::TaskKind::ExternalIo);
	if (taskCount == 0) {
		postDialogWarning(kNoExternalProgramTaskMessage);
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
	MREditWindow *win = currentEditWindow();

	if (win == nullptr || win->windowRole() != MREditWindow::wrCommunicationCommand)
		return true;
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

	if (win == nullptr)
		return true;
	if (win->windowRole() == MREditWindow::wrLog) {
		if (!mrClearLogWindow()) {
			postSearchError("Unable to clear log window.");
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
		postSearchError("Unable to clear communication window.");
		return true;
	}
	win->setReadOnly(true);
	win->setFileChanged(false);
	line << "Cleared communication window #" << win->bufferId() << ".";
	mrLogMessage(line.str().c_str());
	return true;
}

void clearTransientSelectionIfPending(const TEvent &event) {
	if (!g_pendingTransientSelectionClear.active || event.what != evKeyDown)
		return;
	g_pendingTransientSelectionClear.active = false;
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
		if (editor == nullptr || !editor->hasPersistentFileName())
			continue;
		if (normalizedSearchPath(editor->persistentFileName()) != g_pendingTransientSelectionClear.normalizedPath)
			continue;
		{
			std::size_t selStart = editor->selectionStartOffset();
			std::size_t selEnd = editor->selectionEndOffset();
			if (selEnd < selStart)
				std::swap(selStart, selEnd);
			if (selStart != g_pendingTransientSelectionClear.start ||
			    selEnd != g_pendingTransientSelectionClear.end)
				break;
		}
		{
			const std::size_t cursor = editor->cursorOffset();
			editor->setSelectionOffsets(cursor, cursor);
		}
		break;
	}
}

} // namespace

bool dispatchMRKeymapAction(std::string_view actionId, std::string_view sequenceText, MREditWindow *targetWindow) {
	const auto it = std::ranges::find(kKeymapActionDispatchTable, actionId, &KeymapActionDispatchEntry::actionId);
	MREditWindow *window = effectiveKeymapWindow(targetWindow);
	const std::optional<int> markIndex = randomAccessMarkIndexFromSequence(sequenceText);

	if (it == kKeymapActionDispatchTable.end())
		return false;
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
					if (window != nullptr && window->hasBlock())
						return dispatchApplicationCommandEvent(cmMrBlockDelete);
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
				case KeymapCustomAction::None:
					return false;
			}
	}
	return false;
}

bool dispatchMRKeymapMacro(std::string_view macroSpec) {
	std::string errorText;

	if (macroSpec.empty())
		return false;
	return runMacroSpecByName(std::string(macroSpec).c_str(), &errorText, true);
}

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
			MREditWindow *selected = mrShowWindowListDialog(mrwlManageWindows, currentEditWindow());
			if (selected != nullptr)
				mrScheduleWindowActivation(selected);
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
		case cmMrSetupPaths:
		case cmMrSetupBackupsAutosave:
		case cmMrSetupUserInterfaceSettings:
		case cmMrSetupSearchAndReplaceDefaults: {
			if (runSetupDialogCommand(command))
				return true;
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
