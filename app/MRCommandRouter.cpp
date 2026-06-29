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
#include <cctype>
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
#include <limits>
#include <map>
#include <memory>
#include <mutex>
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
#include "../config/settings/MRSettingsHistory.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsRuntimeState.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#include "../app/utils/MRStringUtils.hpp"
#include "../app/services/MRLspAppService.hpp"
#include "../app/services/MRLspServerProfile.hpp"
#include "../keymap/MRKeymapActionCatalog.hpp"
#include "../keymap/MRKeymapSequence.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../mrmac/MRVM.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/vm/MRVMEditor.hpp"
#include "../mrmac/vm/MRVMHash.hpp"
#include "../mrmac/vm/MRVMRuntimeKv.hpp"
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
#include "../ui/MRSidekickEditor.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "../ui/widgets/MRColumnListView.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "MREditorApp.hpp"
#include "MRCommands.hpp"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace {
bool startExternalCommandInWindow(MREditWindow *win, const std::string &commandLine, bool replaceBuffer, bool activate, bool closeOnFailure, std::string_view titleOverride = std::string_view(), const std::string &successAudioUri = std::string(), const std::string &failureAudioUri = std::string());
bool lspCompletionShouldShowChoiceDialog(std::size_t itemCount) noexcept;
int lspReadOnlyHoverAnchorRow(int anchorViewRow, MRReadOnlySidekickPlacement placement, bool diagnosticHover) noexcept;

TFrame *initMrDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

enum : ushort {
	cmMrGetLastFilesActivate = 3890,
	cmMrGetLastFoldersActivate,
	cmMrGetLastWorkspacesActivate
};

enum class GetLastKind : unsigned char {
	None = 0,
	File,
	Folder,
	Workspace
};

struct GetLastEntry {
	std::string value;
	long long epoch;
};

std::vector<MRColumnListView::Row> getLastRowsForValues(const std::vector<std::string> &values) {
	std::vector<MRColumnListView::Row> rows;

	rows.reserve(values.size());
	for (const std::string &value : values)
		rows.push_back(MRColumnListView::Row{value});
	return rows;
}

void appendHistoryEntries(std::vector<GetLastEntry> &outEntries, MRDialogHistoryScope scope, bool files) {
	const MRScopedDialogHistoryState &state = dialogHistoryState(scope);
	const std::vector<MRDialogHistoryEntry> &source = files ? state.fileHistory : state.pathHistory;

	for (const MRDialogHistoryEntry &entry : source) {
		const std::string normalized = normalizeConfiguredPathInput(entry.value);
		if (!normalized.empty()) outEntries.push_back(GetLastEntry{normalized, entry.epoch});
	}
}

std::vector<std::string> recentValuesForScopes(MRDialogHistoryScope firstScope, MRDialogHistoryScope secondScope, bool files, int limit) {
	std::vector<GetLastEntry> entries;
	std::vector<std::string> values;
	std::set<std::string> seen;

	appendHistoryEntries(entries, firstScope, files);
	appendHistoryEntries(entries, secondScope, files);
	std::sort(entries.begin(), entries.end(), [](const GetLastEntry &left, const GetLastEntry &right) {
		if (left.epoch != right.epoch) return left.epoch > right.epoch;
		return left.value < right.value;
	});
	for (const GetLastEntry &entry : entries) {
		if (seen.find(entry.value) != seen.end()) continue;
		seen.insert(entry.value);
		values.push_back(entry.value);
		if (values.size() >= static_cast<std::size_t>(std::max(0, limit))) break;
	}
	return values;
}

std::vector<std::string> recentWorkspaceValues() {
	std::vector<GetLastEntry> entries;
	std::vector<std::string> values;
	std::set<std::string> seen;
	const int limit = configuredMaxWorkspaceHistory();

	appendHistoryEntries(entries, MRDialogHistoryScope::WorkspaceLoad, true);
	std::sort(entries.begin(), entries.end(), [](const GetLastEntry &left, const GetLastEntry &right) {
		if (left.epoch != right.epoch) return left.epoch > right.epoch;
		return left.value < right.value;
	});
	for (const GetLastEntry &entry : entries) {
		if (seen.find(entry.value) != seen.end()) continue;
		seen.insert(entry.value);
		values.push_back(entry.value);
		if (values.size() >= static_cast<std::size_t>(std::max(0, limit))) break;
	}
	return values;
}

short getLastListHeight(std::size_t itemCount) {
	const int wanted = std::max<int>(3, static_cast<int>(itemCount));
	return static_cast<short>(std::min<int>(wanted, 8));
}

class GetLastDialog final : public MRDialogFoundation {
  public:
	GetLastDialog(const std::vector<std::string> &files, const std::vector<std::string> &folders, const std::vector<std::string> &workspaces, short width, short height, short fileRows, short folderRows, short workspaceRows)
	    : TWindowInit(initMrDialogFrame), MRDialogFoundation(mr::dialogs::centeredDialogRect(width, height), "GET LAST", width, height, initMrDialogFrame), fileValues(files), folderValues(folders), workspaceValues(workspaces) {
		short y = 1;

		fileList = insertLabeledList("Files:", cmMrGetLastFilesActivate, fileValues, y, width, fileRows);
		folderList = insertLabeledList("Folders:", cmMrGetLastFoldersActivate, folderValues, y, width, folderRows);
		workspaceList = insertLabeledList("Workspaces:", cmMrGetLastWorkspacesActivate, workspaceValues, y, width, workspaceRows);
	}

	void selectInitialList() {
		if (fileList != nullptr && !fileValues.empty()) {
			fileList->select();
			return;
		}
		if (folderList != nullptr && !folderValues.empty()) {
			folderList->select();
			return;
		}
		if (workspaceList != nullptr && !workspaceValues.empty()) workspaceList->select();
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evMouseWheel && handleListWheel(event)) return;
		if (event.what == evKeyDown) {
			const ushort keyCode = event.keyDown.keyCode;

			if (keyCode == kbTab || keyCode == kbCtrlI || keyCode == kbShiftTab) {
				selectAdjacentList(keyCode == kbShiftTab);
				clearEvent(event);
				return;
			}
		}
		if (event.what == evCommand) {
			switch (event.message.command) {
				case cmMrGetLastFilesActivate:
					acceptSelection(GetLastKind::File, fileList, fileValues);
					clearEvent(event);
					return;
				case cmMrGetLastFoldersActivate:
					acceptSelection(GetLastKind::Folder, folderList, folderValues);
					clearEvent(event);
					return;
				case cmMrGetLastWorkspacesActivate:
					acceptSelection(GetLastKind::Workspace, workspaceList, workspaceValues);
					clearEvent(event);
					return;
				default:
					break;
			}
		}
		MRDialogFoundation::handleEvent(event);
	}

	[[nodiscard]] GetLastKind acceptedKind() const noexcept {
		return selectedKind;
	}

	[[nodiscard]] const std::string &acceptedValue() const noexcept {
		return selectedValue;
	}

  private:
	struct ListSlot {
		MRColumnListView *list;
		const std::vector<std::string> *values;
	};

	std::array<ListSlot, 3> listSlots() const {
		return std::array<ListSlot, 3>{ListSlot{fileList, &fileValues}, ListSlot{folderList, &folderValues}, ListSlot{workspaceList, &workspaceValues}};
	}

	static bool listSlotSelectable(const ListSlot &slot) {
		return slot.list != nullptr && slot.values != nullptr && !slot.values->empty();
	}

	bool handleListWheel(TEvent &event) {
		const std::array<ListSlot, 3> slots = listSlots();

		for (const ListSlot &slot : slots) {
			if (slot.list == nullptr || !slot.list->containsMouse(event)) continue;
			static_cast<void>(slot.list->handleWheel(event));
			clearEvent(event);
			return true;
		}
		clearEvent(event);
		return true;
	}

	int currentListIndex(const std::array<ListSlot, 3> &slots) const {
		TGroup *content = managedContent();
		TView *currentView = content != nullptr ? content->current : nullptr;

		for (std::size_t i = 0; i < slots.size(); ++i)
			if (slots[i].list == currentView) return static_cast<int>(i);
		for (std::size_t i = 0; i < slots.size(); ++i)
			if (slots[i].list != nullptr && (slots[i].list->state & sfSelected) != 0) return static_cast<int>(i);
		return -1;
	}

	void selectAdjacentList(bool reverse) {
		const std::array<ListSlot, 3> slots = listSlots();
		const int currentIndex = currentListIndex(slots);
		const int count = static_cast<int>(slots.size());

		for (int step = 1; step <= count; ++step) {
			int index = currentIndex >= 0 ? currentIndex : (reverse ? 0 : count - 1);
			index += reverse ? -step : step;
			while (index < 0)
				index += count;
			while (index >= count)
				index -= count;
			if (listSlotSelectable(slots[static_cast<std::size_t>(index)])) {
				slots[static_cast<std::size_t>(index)].list->select();
				return;
			}
		}
	}

	MRColumnListView *insertLabeledList(const char *label, ushort command, const std::vector<std::string> &values, short &y, short width, short rows) {
		TScrollBar *scrollBar = nullptr;
		MRColumnListView *list = nullptr;

		insert(new TStaticText(TRect(2, y, width - 2, y + 1), label));
		++y;
		scrollBar = new TScrollBar(TRect(width - 3, y, width - 2, y + rows));
		insert(scrollBar);
		list = new MRColumnListView(TRect(2, y, width - 3, y + rows), scrollBar, this, 0, command);
		list->setRows(getLastRowsForValues(values));
		insert(list);
		y = static_cast<short>(y + rows + 1);
		return list;
	}

	void acceptSelection(GetLastKind kind, MRColumnListView *list, const std::vector<std::string> &values) {
		const short selected = list != nullptr ? list->selectedIndex() : -1;

		if (selected < 0 || static_cast<std::size_t>(selected) >= values.size()) return;
		selectedKind = kind;
		selectedValue = values[static_cast<std::size_t>(selected)];
		endModal(cmOK);
	}

	std::vector<std::string> fileValues;
	std::vector<std::string> folderValues;
	std::vector<std::string> workspaceValues;
	MRColumnListView *fileList = nullptr;
	MRColumnListView *folderList = nullptr;
	MRColumnListView *workspaceList = nullptr;
	GetLastKind selectedKind = GetLastKind::None;
	std::string selectedValue;
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
	DisabledBlockAction = 30,
	SnippetPlaceholderNext = 31,
	SnippetPlaceholderPrevious = 32
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
    KeymapActionDispatchEntry{"MR_EDIT_MARK_ALL", KeymapDispatchKind::AppCommand, cmMrEditMarkAll, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_EDIT_TOGGLE_INSERT_MODE", KeymapDispatchKind::AppCommand, cmMrEditToggleInsertMode, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_TEXT_TOGGLE_LINE_DRAWING", KeymapDispatchKind::AppCommand, cmMrTextToggleLineDrawing, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_TEXT_TOGGLE_DOUBLE_LINES", KeymapDispatchKind::AppCommand, cmMrTextToggleDoubleLines, KeymapWindowMethod::None, KeymapCustomAction::None},
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
    KeymapActionDispatchEntry{"MR_LSP_GOTO_DEFINITION", KeymapDispatchKind::AppCommand, cmMrOtherLspDefinition, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_LSP_FIND_REFERENCES", KeymapDispatchKind::AppCommand, cmMrOtherLspReferences, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_LSP_SHOW_HOVER", KeymapDispatchKind::AppCommand, cmMrOtherLspHover, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_LSP_COMPLETE", KeymapDispatchKind::AppCommand, cmMrOtherLspComplete, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SNIPPET_PLACEHOLDER_NEXT", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SnippetPlaceholderNext},
    KeymapActionDispatchEntry{"MR_SNIPPET_PLACEHOLDER_PREVIOUS", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SnippetPlaceholderPrevious},
    KeymapActionDispatchEntry{"MR_LSP_DOCUMENT_HIGHLIGHT", KeymapDispatchKind::AppCommand, cmMrOtherLspDocumentHighlight, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_LSP_DOCUMENT_SYMBOLS", KeymapDispatchKind::AppCommand, cmMrOtherLspDocumentSymbols, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_LSP_WORKSPACE_SYMBOLS", KeymapDispatchKind::AppCommand, cmMrOtherLspWorkspaceSymbols, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_LSP_SIGNATURE_HELP", KeymapDispatchKind::AppCommand, cmMrOtherLspSignatureHelp, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_LSP_RENAME", KeymapDispatchKind::AppCommand, cmMrOtherLspRename, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_LSP_CODE_ACTIONS", KeymapDispatchKind::AppCommand, cmMrOtherLspCodeActions, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_MACRO_TOGGLE_RECORDING", KeymapDispatchKind::AppCommand, cmMrMacroToggleRecording, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_EDIT_SETTINGS", KeymapDispatchKind::AppCommand, cmMrSetupEditSettings, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_COLOR", KeymapDispatchKind::AppCommand, cmMrSetupColorSetup, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_KEYMAP", KeymapDispatchKind::AppCommand, cmMrSetupKeyMapping, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_LSP_SUPPORT", KeymapDispatchKind::AppCommand, cmMrSetupLspSupport, KeymapWindowMethod::None, KeymapCustomAction::None},
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
std::size_t g_lspReportedCodeActionCount = 0;
std::size_t g_lspReportedDocumentHighlightCount = 0;
std::size_t g_lspReportedDocumentSymbolCount = 0;
std::size_t g_lspReportedSignatureHelpCount = 0;
std::string g_lspReportedDiagnosticSignature;
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
std::string g_lspLastServerProfileName;
std::string g_lspLastError;
std::string g_lspLastPollError;

struct LspAutoHoverState {
	int bufferId = 0;
	std::size_t documentId = 0;
	std::size_t documentVersion = 0;
	std::size_t cursorOffset = 0;
	int line = -1;
	int character = -1;
	int mouseX = -1;
	int mouseY = -1;
	bool mouseValid = false;
	bool valid = false;
	bool requested = false;
	std::string requestId;
	std::string retiredRequestId;
	bool sidekickOpen = false;
	bool dismissedForKey = false;
	std::chrono::steady_clock::time_point stableSince;
	std::chrono::steady_clock::time_point quietUntil;
};

struct LspEditorRequestTarget {
	std::size_t offset = 0;
	mr::lsp::LspTextPosition position;
	int viewColumn = 1;
	int viewRow = 1;
};

struct LspHoverViewAnchor {
	std::string requestId;
	int bufferId = 0;
	std::size_t documentVersion = 0;
	LspEditorRequestTarget target;
};

struct LspSignatureCallContext {
	std::size_t openParenOffset = 0;
	LspEditorRequestTarget requestTarget;
	LspEditorRequestTarget anchorTarget;
};

struct LspSignatureHelpState {
	bool active = false;
	bool requestPending = false;
	int bufferId = 0;
	std::size_t documentId = 0;
	std::size_t openParenOffset = 0;
	std::size_t lastRequestOffset = 0;
	std::size_t lastRequestDocumentVersion = 0;
	std::string requestId;
};

constexpr std::chrono::milliseconds kLspHoverPumpInterval(75);
constexpr std::chrono::milliseconds kLspServicePumpInterval(25);
constexpr std::chrono::milliseconds kLspDocumentSyncCheckInterval(100);
LspAutoHoverState g_lspAutoHover;
LspHoverViewAnchor g_lspHoverViewAnchor;
LspHoverViewAnchor g_lspSignatureHelpViewAnchor;
LspSignatureHelpState g_lspSignatureHelp;
bool g_lspContextMiniMenuOpen = false;
bool g_lspMousePositionKnown = false;
TPoint g_lspLastMousePosition;
std::chrono::steady_clock::time_point g_lspLastHoverPumpAt;
std::chrono::steady_clock::time_point g_lspLastServicePumpAt;
std::chrono::steady_clock::time_point g_lspLastDocumentSyncCheckAt;
std::chrono::steady_clock::time_point g_lspDocumentChangeObservedAt;
int g_lspObservedBufferId = 0;
std::size_t g_lspObservedDocumentId = 0;
std::size_t g_lspObservedDocumentVersion = 0;
int g_lspSyncedBufferId = 0;
std::size_t g_lspSyncedDocumentId = 0;
std::size_t g_lspSyncedDocumentVersion = 0;

const char *placeholderCommandTitle(ushort command) {
	switch (command) {
		case cmMrFileOpen:
			return "File / Open";
		case cmMrFileLoad:
			return "File / Load";
		case cmMrFileGetLast:
			return "File / Get Last";
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
		case cmMrEditMarkAll:
			return "Edit / Mark all";
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
		case cmMrTextToggleLineDrawing:
			return "Text / Line drawing";
		case cmMrTextToggleDoubleLines:
			return "Text / Double lines";
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

		case cmMrOtherMacroLibrary:
			return "Other / Macro library";
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
		case cmMrOtherLspDocumentHighlight:
			return "Other / LSP document highlight";
		case cmMrOtherLspDocumentSymbols:
			return "Other / LSP document symbols";
		case cmMrOtherLspWorkspaceSymbols:
			return "Other / LSP workspace symbols";
		case cmMrOtherLspSignatureHelp:
			return "Other / LSP signature help";
		case cmMrOtherLspRename:
			return "Other / LSP rename";
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
		case cmMrSetupLspSupport:
			return "Installation / LSP support";
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

bool handleExportToPdf();

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

class TextInputDialog final : public MRDialogFoundation {
  public:
	TextInputDialog(const char *title, const char *label, const std::string &initialValue) : TWindowInit(initMrDialogFrame), MRDialogFoundation(mr::dialogs::centeredDialogRect(62, 8), title, 62, 8, initMrDialogFrame) {
		char initialBuffer[256] = {0};

		mInputField = new TInputLine(TRect(12, 2, 58, 3), 255);
		insert(new TLabel(TRect(2, 2, 12, 3), label, mInputField));
		insert(mInputField);
		std::snprintf(initialBuffer, sizeof(initialBuffer), "%s", initialValue.c_str());
		mInputField->setData(initialBuffer);
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		mr::dialogs::insertUniformButtonRow(*this, 14, 5, 2, buttons);
		setDialogValidationHook([this]() {
			MRScrollableDialog::DialogValidationResult result;
			char buffer[256] = {0};

			if (mInputField != nullptr) mInputField->getData(buffer);
			result.valid = !trimAscii(buffer).empty();
			if (!result.valid) result.warningText = "Query must not be empty.";
			return result;
		});
		finalizeLayout();
	}

	[[nodiscard]] std::string value() const {
		char buffer[256] = {0};

		if (mInputField != nullptr) const_cast<TInputLine *>(mInputField)->getData(buffer);
		return trimAscii(buffer);
	}

  private:
	TInputLine *mInputField = nullptr;
};

bool promptTextValue(const char *title, const char *label, const std::string &initialValue, std::string &outValue) {
	TextInputDialog dialog(title, label, initialValue);
	const ushort result = TProgram::deskTop->execView(&dialog);

	if (result != cmOK) return false;
	outValue = dialog.value();
	return !outValue.empty();
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

std::string lspDiagnosticChar(char ch) {
	switch (ch) {
		case '\n':
			return "\\n";
		case '\r':
			return "\\r";
		case '\t':
			return "\\t";
		case '\\':
			return "\\\\";
		default:
			break;
	}
	if (static_cast<unsigned char>(ch) < 32) return "?";
	return std::string(1, ch);
}

std::string lspCompletionTextWindow(MRFileEditor &editor, std::size_t offset, std::size_t radius) {
	const std::size_t length = editor.bufferLength();
	const std::size_t start = offset > radius ? offset - radius : 0;
	const std::size_t end = std::min(length, offset + radius);
	std::string text;

	for (std::size_t index = start; index < end; ++index) {
		if (index == offset) text += "|";
		text += lspDiagnosticChar(editor.charAtOffset(index));
	}
	if (offset == end) text += "|";
	return text;
}

bool buildLspServerProfileFromEditor(const MRFileEditor &editor, mr::services::MRLspServerProfile &profile, std::string &configurationSource, std::string &errorMessage) {
	MRCompilerProfile compilerProfile;
	std::string matchedProfileName;

	if (editor.hasPersistentFileName() && effectiveCompilerProfileForPath(editor.persistentFileName(), compilerProfile, &matchedProfileName, nullptr))
		return mr::services::buildLspServerProfileForLanguageWithCompilerProfile(editor.syntaxLanguage(), editor.syntaxLanguageName(), compilerProfile, profile, configurationSource, errorMessage);
	return mr::services::buildLspServerProfileForLanguage(editor.syntaxLanguage(), editor.syntaxLanguageName(), profile, configurationSource, errorMessage);
}

bool buildLspDocumentSnapshotForWindow(MREditWindow *win, mr::services::MRWorkspaceDocumentSnapshot &document, bool reportWarnings) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

	if (win == nullptr || editor == nullptr) {
		if (reportWarnings) postLspWarning("LSP requires an editor window.");
		return false;
	}
	if (!editor->hasPersistentFileName()) {
		if (reportWarnings) postLspWarning("LSP requires a saved file.");
		return false;
	}
	document = mr::services::MRWorkspaceDocumentSnapshot();
	document.bufferId = win->bufferId();
	document.documentId = editor->documentId();
	document.documentVersion = editor->documentVersion();
	document.path = editor->persistentFileName();
	document.languageName = editor->syntaxLanguageName();
	document.mainFile = mrIsWorkspaceMainFile(win);
	return true;
}

bool buildCurrentLspDocumentSnapshot(MREditWindow *win, mr::services::MRWorkspaceDocumentSnapshot &document) {
	return buildLspDocumentSnapshotForWindow(win, document, true);
}

bool lspIdentifierByte(char ch) noexcept {
	const unsigned char uch = static_cast<unsigned char>(ch);

	return std::isalnum(uch) != 0 || ch == '_';
}

bool lspIdentifierRangeAroundOffset(MRFileEditor &editor, std::size_t offset, std::size_t &start, std::size_t &end) {
	if (editor.bufferLength() == 0) return false;
	start = std::min(offset, editor.bufferLength());
	if (start == editor.bufferLength() || !lspIdentifierByte(editor.charAtOffset(start))) {
		if (start == 0 || !lspIdentifierByte(editor.charAtOffset(start - 1))) return false;
		--start;
	}
	end = start;
	while (start > editor.lineStartOffset(start) && lspIdentifierByte(editor.charAtOffset(start - 1)))
		--start;
	const std::size_t lineEnd = editor.lineEndOffset(end);
	while (end < lineEnd && lspIdentifierByte(editor.charAtOffset(end)))
		++end;
	return end > start;
}

std::string lspIdentifierTextAroundOffset(MRFileEditor &editor, std::size_t offset) {
	std::size_t start = 0;
	std::size_t end = 0;
	std::string text;

	if (!lspIdentifierRangeAroundOffset(editor, offset, start, end)) return text;
	for (std::size_t index = start; index < end; ++index)
		text.push_back(editor.charAtOffset(index));
	return text;
}

mr::lsp::LspTextPosition currentLspTextPosition(const MRFileEditor &editor) {
	const std::size_t offset = editor.cursorOffset();
	mr::lsp::LspTextPosition position;

	position.line = static_cast<int>(editor.lineIndexOfOffset(offset));
	position.character = static_cast<int>(editor.columnOfOffset(offset));
	return position;
}

bool lspRequestTargetFromEditorOffset(MRFileEditor &editor, std::size_t offset, int viewColumn, int viewRow, LspEditorRequestTarget &target) {
	const std::size_t documentOffset = std::min(offset, editor.bufferLength());

	target.offset = documentOffset;
	target.position.line = static_cast<int>(editor.lineIndexOfOffset(documentOffset));
	target.position.character = static_cast<int>(editor.columnOfOffset(documentOffset));
	target.viewColumn = std::max(1, viewColumn);
	target.viewRow = std::max(1, viewRow);
	return true;
}

bool lspRequestTargetFromCursor(MRFileEditor &editor, LspEditorRequestTarget &target) {
	return lspRequestTargetFromEditorOffset(editor, editor.cursorOffset(), editor.currentViewColumn(), editor.currentViewRow(), target);
}

int lspViewColumnForOffset(MRFileEditor &editor, std::size_t offset) {
	const std::size_t lineStart = editor.lineStartOffset(offset);

	return std::max(1, editor.charColumn(lineStart, offset) - editor.delta.x + 1);
}

bool lspCompletionRequestTargetFromSource(MRFileEditor &editor, const LspEditorRequestTarget &sourceTarget, LspEditorRequestTarget &requestTarget, std::size_t &triggerOffset) {
	std::size_t identifierStart = 0;
	std::size_t identifierEnd = 0;
	std::size_t openParenOffset = 0;
	std::size_t nameEnd = 0;
	std::size_t lineStart = 0;

	requestTarget = sourceTarget;
	triggerOffset = sourceTarget.offset > 0 ? sourceTarget.offset - 1 : 0;
	if (sourceTarget.offset < editor.bufferLength() && editor.charAtOffset(sourceTarget.offset) == '(')
		openParenOffset = sourceTarget.offset;
	else if (sourceTarget.offset > 0 && editor.charAtOffset(sourceTarget.offset - 1) == '(')
		openParenOffset = sourceTarget.offset - 1;
	else
		openParenOffset = editor.bufferLength();
	if (openParenOffset < editor.bufferLength()) {
		nameEnd = openParenOffset;
		lineStart = editor.lineStartOffset(openParenOffset);
		while (nameEnd > lineStart && std::isspace(static_cast<unsigned char>(editor.charAtOffset(nameEnd - 1))) != 0)
			--nameEnd;
		if (nameEnd > lineStart && lspIdentifierByte(editor.charAtOffset(nameEnd - 1))) {
			triggerOffset = editor.bufferLength();
			return lspRequestTargetFromEditorOffset(editor, nameEnd, lspViewColumnForOffset(editor, nameEnd), sourceTarget.viewRow, requestTarget);
		}
	}
	if (sourceTarget.offset < editor.bufferLength() && !lspIdentifierByte(editor.charAtOffset(sourceTarget.offset)) && editor.charAtOffset(sourceTarget.offset) != '\n' && editor.charAtOffset(sourceTarget.offset) != '\r' && sourceTarget.offset + 1 < editor.bufferLength() &&
	    editor.charAtOffset(sourceTarget.offset + 1) != '\n' && editor.charAtOffset(sourceTarget.offset + 1) != '\r' &&
	    lspIdentifierRangeAroundOffset(editor, sourceTarget.offset + 1, identifierStart, identifierEnd)) {
		triggerOffset = sourceTarget.offset;
		return lspRequestTargetFromEditorOffset(editor, identifierEnd, lspViewColumnForOffset(editor, identifierEnd), sourceTarget.viewRow, requestTarget);
	}
	if (!lspIdentifierRangeAroundOffset(editor, sourceTarget.offset, identifierStart, identifierEnd)) {
		if (sourceTarget.offset + 1 < editor.bufferLength() && editor.charAtOffset(sourceTarget.offset + 1) != '\n' && editor.charAtOffset(sourceTarget.offset + 1) != '\r' &&
		    lspIdentifierRangeAroundOffset(editor, sourceTarget.offset + 1, identifierStart, identifierEnd)) {
			triggerOffset = sourceTarget.offset;
			return lspRequestTargetFromEditorOffset(editor, identifierEnd, lspViewColumnForOffset(editor, identifierEnd), sourceTarget.viewRow, requestTarget);
		}
		return true;
	}
	if (identifierStart > 0) triggerOffset = identifierStart - 1;
	return lspRequestTargetFromEditorOffset(editor, identifierEnd, lspViewColumnForOffset(editor, identifierEnd), sourceTarget.viewRow, requestTarget);
}

bool lspCompletionEditRange(MRFileEditor &editor, const mr::services::MRServiceCompletionResult &result, const mr::services::MRServiceCompletionItem &item, std::size_t &start, std::size_t &end, std::string &errorMessage);
void suppressLspAutoHoverForCompletion(const mr::services::MRWorkspaceDocumentSnapshot &document, MREditWindow *win, const LspEditorRequestTarget &target);
bool lspDiagnosticSidekickAnchor(const mr::services::MRLspPositionServiceSnapshot &snapshot, MRFileEditor &editor, int &viewColumn, int &viewRow);

bool lspCompletionTargetSelfTestForRegression(std::string &failureReason) {
	MRFileEditor editor(TRect(0, 0, 80, 12), nullptr, nullptr, nullptr, "completion-target.tex");
	const std::string text = "\\begin{tab\n\\begin{tabular\nobject.ve\nswitch(\nplain\n";
	const std::size_t latexStart = text.find("tab");
	const std::size_t latexEnd = latexStart + 3;
	const std::size_t latexLongStart = text.find("tabular");
	const std::size_t latexLongEnd = latexLongStart + 7;
	const std::size_t cppStart = text.find("ve");
	const std::size_t cppEnd = cppStart + 2;
	const std::size_t switchStart = text.find("switch(");
	const std::size_t switchEnd = switchStart + 6;
	const std::size_t switchParenEnd = switchStart + 7;
	if (latexStart == std::string::npos || latexLongStart == std::string::npos || cppStart == std::string::npos || switchStart == std::string::npos) {
		failureReason = "completion target self-test seed text is invalid.";
		return false;
	}
	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = "completion target self-test could not seed editor text.";
		return false;
	}
	{
		LspEditorRequestTarget sourceTarget;
		LspEditorRequestTarget requestTarget;
		std::size_t triggerOffset = 0;

		lspRequestTargetFromEditorOffset(editor, latexStart - 1, lspViewColumnForOffset(editor, latexStart - 1), 1, sourceTarget);
		if (!lspCompletionRequestTargetFromSource(editor, sourceTarget, requestTarget, triggerOffset)) {
			failureReason = "latex opening brace target calculation failed.";
			return false;
		}
		if (requestTarget.offset != latexEnd) {
			failureReason = "latex opening brace must request completion at literal end.";
			return false;
		}
		if (triggerOffset >= editor.bufferLength() || editor.charAtOffset(triggerOffset) != '{') {
			failureReason = "latex opening brace trigger mismatch.";
			return false;
		}
	}
	for (std::size_t offset = latexStart; offset <= latexEnd; ++offset) {
		LspEditorRequestTarget sourceTarget;
		LspEditorRequestTarget requestTarget;
		std::size_t triggerOffset = 0;

		lspRequestTargetFromEditorOffset(editor, offset, lspViewColumnForOffset(editor, offset), 1, sourceTarget);
		if (!lspCompletionRequestTargetFromSource(editor, sourceTarget, requestTarget, triggerOffset)) {
			failureReason = "latex literal offset " + std::to_string(offset - latexStart) + ": target calculation failed.";
			return false;
		}
		if (requestTarget.offset != latexEnd) {
			failureReason = "latex literal offset " + std::to_string(offset - latexStart) + ": request offset mismatch.";
			return false;
		}
		if (requestTarget.viewColumn != lspViewColumnForOffset(editor, latexEnd)) {
			failureReason = "latex literal offset " + std::to_string(offset - latexStart) + ": request view column mismatch.";
			return false;
		}
		if (triggerOffset >= editor.bufferLength() || editor.charAtOffset(triggerOffset) != '{') {
			failureReason = "latex literal offset " + std::to_string(offset - latexStart) + ": trigger mismatch.";
			return false;
		}
	}
	for (std::size_t offset = latexLongStart; offset <= latexLongEnd; ++offset) {
		LspEditorRequestTarget sourceTarget;
		LspEditorRequestTarget requestTarget;
		std::size_t triggerOffset = 0;

		lspRequestTargetFromEditorOffset(editor, offset, lspViewColumnForOffset(editor, offset), 2, sourceTarget);
		if (!lspCompletionRequestTargetFromSource(editor, sourceTarget, requestTarget, triggerOffset)) {
			failureReason = "latex long literal offset " + std::to_string(offset - latexLongStart) + ": target calculation failed.";
			return false;
		}
		if (requestTarget.offset != latexLongEnd) {
			failureReason = "latex long literal offset " + std::to_string(offset - latexLongStart) + ": request offset mismatch.";
			return false;
		}
		if (requestTarget.viewColumn != lspViewColumnForOffset(editor, latexLongEnd)) {
			failureReason = "latex long literal offset " + std::to_string(offset - latexLongStart) + ": request view column mismatch.";
			return false;
		}
		if (triggerOffset >= editor.bufferLength() || editor.charAtOffset(triggerOffset) != '{') {
			failureReason = "latex long literal offset " + std::to_string(offset - latexLongStart) + ": trigger mismatch.";
			return false;
		}
	}
	for (std::size_t offset = cppStart; offset <= cppEnd; ++offset) {
		LspEditorRequestTarget sourceTarget;
		LspEditorRequestTarget requestTarget;
		std::size_t triggerOffset = 0;

		lspRequestTargetFromEditorOffset(editor, offset, lspViewColumnForOffset(editor, offset), 2, sourceTarget);
		if (!lspCompletionRequestTargetFromSource(editor, sourceTarget, requestTarget, triggerOffset)) {
			failureReason = "dotted literal offset " + std::to_string(offset - cppStart) + ": target calculation failed.";
			return false;
		}
		if (requestTarget.offset != cppEnd) {
			failureReason = "dotted literal offset " + std::to_string(offset - cppStart) + ": request offset mismatch.";
			return false;
		}
		if (requestTarget.viewColumn != lspViewColumnForOffset(editor, cppEnd)) {
			failureReason = "dotted literal offset " + std::to_string(offset - cppStart) + ": request view column mismatch.";
			return false;
		}
		if (triggerOffset >= editor.bufferLength() || editor.charAtOffset(triggerOffset) != '.') {
			failureReason = "dotted literal offset " + std::to_string(offset - cppStart) + ": trigger mismatch.";
			return false;
		}
	}
	{
		LspEditorRequestTarget sourceTarget;
		LspEditorRequestTarget requestTarget;
		std::size_t triggerOffset = 0;
		std::size_t replaceStart = 0;
		std::size_t replaceEnd = 0;
		std::string errorMessage;
		mr::services::MRServiceCompletionResult result;
		mr::services::MRServiceCompletionItem item;

		lspRequestTargetFromEditorOffset(editor, switchParenEnd, lspViewColumnForOffset(editor, switchParenEnd), 4, sourceTarget);
		if (!lspCompletionRequestTargetFromSource(editor, sourceTarget, requestTarget, triggerOffset)) {
			failureReason = "open paren target calculation failed.";
			return false;
		}
		if (requestTarget.offset != switchEnd) {
			failureReason = "open paren request offset mismatch.";
			return false;
		}
		if (triggerOffset < editor.bufferLength()) {
			failureReason = "open paren completion must not use typed paren as trigger.";
			return false;
		}
		result.header.identity.documentVersion = editor.documentVersion();
		result.hasRequestPosition = true;
		result.requestPosition.line = requestTarget.position.line;
		result.requestPosition.character = requestTarget.position.character;
		item.label = "switch (condition) {cases}";
		item.insertText = "switch (${1:condition}) {\n$0\n}";
		item.hasInsertTextFormat = true;
		item.insertTextFormat = 2;
		if (!lspCompletionEditRange(editor, result, item, replaceStart, replaceEnd, errorMessage)) {
			failureReason = "open paren snippet range failed: " + errorMessage;
			return false;
		}
		if (replaceStart != switchStart || replaceEnd != switchParenEnd) {
			failureReason = "open paren snippet range mismatch.";
			return false;
		}
	}
	{
		MREditWindow window(TRect(0, 0, 80, 12), "completion-hover", 4104);
		LspEditorRequestTarget sourceTarget;
		mr::services::MRWorkspaceDocumentSnapshot document;

		g_lspAutoHover = LspAutoHoverState();
		document.bufferId = window.bufferId();
		document.documentId = 1001;
		document.documentVersion = editor.documentVersion();
		g_lspAutoHover.bufferId = window.bufferId();
		g_lspAutoHover.documentId = document.documentId;
		g_lspAutoHover.documentVersion = document.documentVersion;
		g_lspAutoHover.requested = true;
		g_lspAutoHover.requestId = "hover-before-complete";
		if (!lspRequestTargetFromEditorOffset(editor, switchParenEnd, lspViewColumnForOffset(editor, switchParenEnd), 4, sourceTarget)) {
			failureReason = "completion hover suppression target calculation failed.";
			g_lspAutoHover = LspAutoHoverState();
			return false;
		}
		suppressLspAutoHoverForCompletion(document, &window, sourceTarget);
		if (g_lspAutoHover.requested || !g_lspAutoHover.requestId.empty()) {
			failureReason = "completion hover suppression left request pending.";
			g_lspAutoHover = LspAutoHoverState();
			return false;
		}
		if (g_lspAutoHover.retiredRequestId != "hover-before-complete") {
			failureReason = "completion hover suppression did not retire prior request.";
			g_lspAutoHover = LspAutoHoverState();
			return false;
		}
		if (!g_lspAutoHover.dismissedForKey || g_lspAutoHover.quietUntil <= std::chrono::steady_clock::now()) {
			failureReason = "completion hover suppression did not arm quiet period.";
			g_lspAutoHover = LspAutoHoverState();
			return false;
		}
		g_lspAutoHover = LspAutoHoverState();
	}
	if (lspCompletionShouldShowChoiceDialog(0) || lspCompletionShouldShowChoiceDialog(1) || !lspCompletionShouldShowChoiceDialog(2)) {
		failureReason = "single completion choice dialog predicate mismatch.";
		return false;
	}
	if (lspReadOnlyHoverAnchorRow(9, MRReadOnlySidekickPlacement::UnderCode, false) != 9) {
		failureReason = "normal hover anchor row must stay on the reported row for under-code placement.";
		return false;
	}
	if (lspReadOnlyHoverAnchorRow(9, MRReadOnlySidekickPlacement::UnderCode, true) != 9) {
		failureReason = "diagnostic hover anchor row must stay on the reported row for under-code placement.";
		return false;
	}
	if (lspReadOnlyHoverAnchorRow(9, MRReadOnlySidekickPlacement::RightMargin, false) != 9) {
		failureReason = "right-margin hover anchor row must not shift.";
		return false;
	}
	{
		MRFileEditor diagnosticEditor(TRect(0, 0, 80, 12), nullptr, nullptr, nullptr, "diagnostic-anchor.c");
		mr::services::MRLspPositionServiceSnapshot snapshot;
		mr::services::MRServiceDiagnosticResult result;
		mr::services::MRServiceDiagnosticEntry diagnostic;
		int viewColumn = 12;
		int viewRow = 1;

		if (!diagnosticEditor.replaceBufferText("int main() {\n    int i;\n}\n")) {
			failureReason = "diagnostic anchor self-test could not seed editor text.";
			return false;
		}
		diagnostic.reportedRange.start.line = 1;
		diagnostic.reportedRange.start.character = 160;
		diagnostic.reportedRange.end = diagnostic.reportedRange.start;
		result.diagnostics.push_back(diagnostic);
		snapshot.results.diagnostics.push_back(result);
		if (!lspDiagnosticSidekickAnchor(snapshot, diagnosticEditor, viewColumn, viewRow)) {
			failureReason = "diagnostic anchor must tolerate out-of-line reported column.";
			return false;
		}
		if (viewColumn != 12) {
			failureReason = "diagnostic anchor must preserve request column when reported column is outside the line.";
			return false;
		}
		if (viewRow != 2) {
			failureReason = "diagnostic anchor must keep the reported diagnostic line when reported column is outside the line.";
			return false;
		}
	}
	if (!MRKeymapActionCatalog::contains("MR_SNIPPET_PLACEHOLDER_NEXT") || !MRKeymapActionCatalog::contains("MR_SNIPPET_PLACEHOLDER_PREVIOUS")) {
		failureReason = "snippet placeholder actions missing from action catalog.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool lspSignatureHelpRequestTargetFromCallToken(MRFileEditor &editor, const LspEditorRequestTarget &sourceTarget, LspEditorRequestTarget &requestTarget) {
	std::size_t identifierStart = 0;
	std::size_t identifierEnd = 0;
	std::size_t probe = 0;
	const std::size_t lineEnd = editor.lineEndOffset(sourceTarget.offset);

	if (!lspIdentifierRangeAroundOffset(editor, sourceTarget.offset, identifierStart, identifierEnd)) return false;
	probe = identifierEnd;
	while (probe < lineEnd && std::isspace(static_cast<unsigned char>(editor.charAtOffset(probe))) != 0)
		++probe;
	if (probe >= lineEnd || editor.charAtOffset(probe) != '(') return false;
	return lspRequestTargetFromEditorOffset(editor, probe + 1, sourceTarget.viewColumn, sourceTarget.viewRow, requestTarget);
}

bool lspSignatureCallContextFromOpenParen(MRFileEditor &editor, std::size_t openParenOffset, const LspEditorRequestTarget &sourceTarget, LspSignatureCallContext &context) {
	std::size_t nameEnd = openParenOffset;
	std::size_t nameStart = 0;
	LspEditorRequestTarget requestTarget;

	while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(editor.charAtOffset(nameEnd - 1))) != 0)
		--nameEnd;
	nameStart = nameEnd;
	while (nameStart > editor.lineStartOffset(nameStart) && lspIdentifierByte(editor.charAtOffset(nameStart - 1)))
		--nameStart;
	if (nameStart == nameEnd) return false;
	if (!lspRequestTargetFromEditorOffset(editor, sourceTarget.offset, sourceTarget.viewColumn, sourceTarget.viewRow, requestTarget)) return false;
	context.openParenOffset = openParenOffset;
	context.requestTarget = requestTarget;
	context.anchorTarget = sourceTarget;
	return true;
}

bool lspSignatureCallContextAroundTarget(MRFileEditor &editor, const LspEditorRequestTarget &sourceTarget, LspSignatureCallContext &context) {
	LspEditorRequestTarget requestTarget;
	std::size_t scan = sourceTarget.offset;
	int nesting = 0;

	if (lspSignatureHelpRequestTargetFromCallToken(editor, sourceTarget, requestTarget)) {
		context.openParenOffset = requestTarget.offset - 1;
		context.requestTarget = requestTarget;
		context.anchorTarget = sourceTarget;
		return true;
	}
	while (scan > 0) {
		const char ch = editor.charAtOffset(scan - 1);

		--scan;
		if (ch == ')') {
			++nesting;
			continue;
		}
		if (ch == '(') {
			if (nesting == 0) return lspSignatureCallContextFromOpenParen(editor, scan, sourceTarget, context);
			--nesting;
			continue;
		}
		if (nesting == 0 && (ch == ';' || ch == '{' || ch == '}')) break;
	}
	return false;
}

bool lspSignatureRefreshTrigger(MRFileEditor &editor, const LspSignatureCallContext &context, const mr::services::MRWorkspaceDocumentSnapshot &document) {
	if (!g_lspSignatureHelp.active) return true;
	if (g_lspSignatureHelp.requestPending) return false;
	if (context.requestTarget.offset == g_lspSignatureHelp.lastRequestOffset && document.documentVersion == g_lspSignatureHelp.lastRequestDocumentVersion) return false;
	if (document.documentVersion == g_lspSignatureHelp.lastRequestDocumentVersion) return true;
	if (context.requestTarget.offset == 0) return false;
	const char previous = editor.charAtOffset(context.requestTarget.offset - 1);
	return previous == '(' || previous == ',';
}

MRReadOnlySidekickPlacement configuredLspReadOnlySidekickPlacement() noexcept {
	return configuredLanguageServerSidekickPlacement() == MRLanguageServerSidekickPlacement::AtCode ? MRReadOnlySidekickPlacement::UnderCode : MRReadOnlySidekickPlacement::RightMargin;
}

bool lspRequestTargetFromGlobalPoint(MREditWindow *win, TPoint where, LspEditorRequestTarget &target) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

	if (editor == nullptr || !editor->textPointInView(where)) return false;
	const TPoint local = editor->makeLocal(where);
	const TRect viewport = editor->visibleTextViewportBounds();
	return lspRequestTargetFromEditorOffset(*editor, editor->offsetForGlobalPoint(where), local.x - viewport.a.x + 1, local.y - viewport.a.y + 1, target);
}

MREditWindow *lspEditorWindowAtGlobalPoint(TPoint where, LspEditorRequestTarget &target) {
	const std::vector<MREditWindow *> windows = allEditWindowsAndBentoPanesInZOrder();

	for (MREditWindow *window : windows)
		if (lspRequestTargetFromGlobalPoint(window, where, target)) return window;
	return nullptr;
}

mr::services::MRServiceTextPosition serviceTextPositionFromLsp(const mr::lsp::LspTextPosition &position) noexcept {
	mr::services::MRServiceTextPosition servicePosition;

	servicePosition.line = position.line;
	servicePosition.character = position.character;
	return servicePosition;
}

void suppressLspAutoHoverForExplicitSidekick(const mr::services::MRWorkspaceDocumentSnapshot &document, MREditWindow *win, const LspEditorRequestTarget &target, int quietMilliseconds);
void suppressLspAutoHoverForCompletion(const mr::services::MRWorkspaceDocumentSnapshot &document, MREditWindow *win, const LspEditorRequestTarget &target);
void activateLspSignatureHelp(const mr::services::MRWorkspaceDocumentSnapshot &document, MREditWindow *win, const LspSignatureCallContext &context, const std::string &requestId);
void clearLspSignatureHelpState(MREditWindow *win);

bool languageServerCommandChannelEnabled(mr::services::MRLspServiceCommandId command) {
	const MRLanguageServerChannelSettings channels = configuredLanguageServerChannelSettings();

	switch (command) {
		case mr::services::MRLspServiceCommandId::GoToDefinition:
			return channels.definition;
		case mr::services::MRLspServiceCommandId::FindReferences:
			return channels.references;
		case mr::services::MRLspServiceCommandId::ShowHover:
			return channels.hover;
		case mr::services::MRLspServiceCommandId::Complete:
			return channels.completion;
		case mr::services::MRLspServiceCommandId::DocumentHighlight:
			return channels.documentHighlight;
		case mr::services::MRLspServiceCommandId::DocumentSymbols:
			return channels.documentSymbols;
		case mr::services::MRLspServiceCommandId::WorkspaceSymbols:
			return channels.workspaceSymbols;
		case mr::services::MRLspServiceCommandId::SignatureHelp:
			return channels.signatureHelp;
		case mr::services::MRLspServiceCommandId::Rename:
			return channels.rename;
	}
	return true;
}

bool reportDisabledLanguageServerChannel(const char *label, bool reportMessages) {
	if (reportMessages) postLspWarning(std::string(label != nullptr ? label : "LSP channel") + " channel is disabled.");
	g_lspLastRequestState = "disabled";
	g_lspLastError = std::string(label != nullptr ? label : "LSP channel") + " channel is disabled.";
	++g_lspRequestFailureCount;
	return true;
}

bool requestLspEditorCommandForWindow(MREditWindow *win, mr::services::MRLspServiceCommandId command, const char *label, bool reportMessages, bool *requestSent = nullptr, const LspEditorRequestTarget *requestTarget = nullptr, bool completionTriggerEnabled = true) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	mr::services::MRLspServerProfile profile;
	mr::services::MRWorkspaceDocumentSnapshot document;
	LspEditorRequestTarget cursorTarget;
	LspEditorRequestTarget completionTarget;
	LspSignatureCallContext signatureHelpContext;
	mr::lsp::LspTextPosition position;
	const LspEditorRequestTarget *target = requestTarget;
	const LspEditorRequestTarget *serviceTarget = nullptr;
	std::size_t completionTriggerOffset = 0;
	std::string errorMessage;
	std::string configurationSource;
	std::string completionTriggerCandidate;
	std::ostringstream positionText;
	bool hasSignatureHelpContext = false;

	if (requestSent != nullptr) *requestSent = false;
	if (reportMessages && command == mr::services::MRLspServiceCommandId::ShowHover) {
		g_lspAutoHover.requested = false;
		g_lspAutoHover.requestId.clear();
	}
	++g_lspRequestCount;
	g_lspLastRequestLabel = label != nullptr ? label : "LSP request";
	g_lspLastRequestPath.clear();
	g_lspLastRequestPosition.clear();
	g_lspLastRequestState = "preparing";
	g_lspLastError.clear();
	g_lspLastPollError.clear();
	if (!configuredLanguageServerSpawnDaemon()) {
		g_lspLastRequestState = "disabled";
		g_lspLastError = "LSP support is disabled.";
		++g_lspRequestFailureCount;
		if (reportMessages) postLspWarning(g_lspLastError);
		return true;
	}
	if (!languageServerCommandChannelEnabled(command)) return reportDisabledLanguageServerChannel(label, reportMessages);
	if (!buildLspDocumentSnapshotForWindow(win, document, reportMessages)) {
		g_lspLastRequestState = "failed";
		g_lspLastError = "LSP request requires a saved editor document.";
		++g_lspRequestFailureCount;
		return true;
	}
	if (editor == nullptr || !buildLspServerProfileFromEditor(*editor, profile, configurationSource, errorMessage)) {
		g_lspLastRequestState = "failed";
		g_lspLastError = errorMessage.empty() ? "LSP server not configured." : errorMessage;
		++g_lspRequestFailureCount;
		if (reportMessages) postLspWarning(g_lspLastError);
		return true;
	}
	if (target == nullptr) {
		lspRequestTargetFromCursor(*editor, cursorTarget);
		target = &cursorTarget;
	}
	serviceTarget = target;
	if (command == mr::services::MRLspServiceCommandId::SignatureHelp) {
		hasSignatureHelpContext = lspSignatureCallContextAroundTarget(*editor, *target, signatureHelpContext);
		if (hasSignatureHelpContext) serviceTarget = &signatureHelpContext.requestTarget;
	}
	if (command == mr::services::MRLspServiceCommandId::Complete) {
		lspCompletionRequestTargetFromSource(*editor, *serviceTarget, completionTarget, completionTriggerOffset);
		serviceTarget = &completionTarget;
	}
	g_lspLastServerExecutable = profile.executablePath;
	g_lspLastServerWorkingDirectory = profile.workingDirectory;
	g_lspLastServerArguments = mr::services::lspServerProfileArgumentText(profile);
	g_lspLastServerConfigurationSource = configurationSource;
	g_lspLastServerProfileName = profile.profileName;
	position = serviceTarget->position;
	if (command == mr::services::MRLspServiceCommandId::Complete && completionTriggerEnabled && serviceTarget->offset > 0 && completionTriggerOffset < editor->bufferLength()) {
		completionTriggerCandidate.push_back(editor->charAtOffset(completionTriggerOffset));
	}
	positionText << (position.line + 1) << ":" << (position.character + 1);
	if (command == mr::services::MRLspServiceCommandId::Complete) {
		std::ostringstream completionContext;

		completionContext << "LSP completion request context:"
		                  << " path=" << document.path
		                  << " version=" << document.documentVersion
		                  << " position=" << positionText.str()
		                  << " trigger=" << (completionTriggerCandidate.empty() ? "<none>" : lspDiagnosticChar(completionTriggerCandidate[0]))
		                  << " text=\"" << lspCompletionTextWindow(*editor, serviceTarget->offset, 24) << "\"";
		mrLogMessage(completionContext.str());
	}
	g_lspLastRequestPath = document.path;
	g_lspLastRequestPosition = positionText.str();
	{
		const std::size_t requestOffset = serviceTarget->offset;
		const std::string lineText = editor->lineTextAtOffset(requestOffset);
		std::string escapedLine;
		std::string escapedPrefix;
		const std::size_t prefixLength = std::min(lineText.size(), static_cast<std::size_t>(std::max(0, position.character)));

		for (std::size_t index = 0; index < lineText.size(); ++index) {
			const char ch = lineText[index];
			if (ch == '\t')
				escapedLine += "\\t";
			else if (ch == '\r')
				escapedLine += "\\r";
			else if (ch == '\n')
				escapedLine += "\\n";
			else
				escapedLine.push_back(ch);
		}
		for (std::size_t index = 0; index < prefixLength; ++index) {
			const char ch = lineText[index];
			if (ch == '\t')
				escapedPrefix += "\\t";
			else if (ch == '\r')
				escapedPrefix += "\\r";
			else if (ch == '\n')
				escapedPrefix += "\\n";
			else
				escapedPrefix.push_back(ch);
		}
		mrLogMessage("LSP request dispatch: " + g_lspLastRequestLabel + " path=" + document.path + " version=" + std::to_string(document.documentVersion) + " position=" + g_lspLastRequestPosition + " offset=" + std::to_string(requestOffset) + " textColumn=" + std::to_string(editor->columnOfOffset(requestOffset)) + " viewColumn=" + std::to_string(serviceTarget->viewColumn) + " lineLength=" + std::to_string(lineText.size()) + " prefix=\"" + escapedPrefix + "\" line=\"" + escapedLine + "\"");
	}
	if (command == mr::services::MRLspServiceCommandId::WorkspaceSymbols) {
		const std::string seed = lspIdentifierTextAroundOffset(*editor, target->offset);
		std::string query = seed;

		if (query.empty() && !promptTextValue("LSP WORKSPACE SYMBOLS", "Query:", seed, query)) {
			g_lspLastRequestState = "cancelled";
			return true;
		}
		g_lspAppService.clearMainFile();
		mr::services::MRWorkspaceServiceSnapshot workspace = g_lspAppService.buildCurrentWorkspaceSnapshot();
		mrLogMessage("LSP workspace symbols query: \"" + query + "\"");
		if (!g_lspAppService.requestWorkspaceSymbols(profile, workspace, document, *editor, query, errorMessage)) {
			g_lspLastRequestState = "failed";
			g_lspLastError = errorMessage;
			++g_lspRequestFailureCount;
			if (reportMessages) postLspError(g_lspLastRequestLabel + " failed: " + errorMessage);
			return true;
		}
		g_lspLastRequestState = "requested";
		if (requestSent != nullptr) *requestSent = true;
		if (reportMessages) postLspInfo(g_lspLastRequestLabel + " requested.");
		return true;
	}
	g_lspAppService.clearMainFile();
	if (command == mr::services::MRLspServiceCommandId::Complete) suppressLspAutoHoverForCompletion(document, win, *serviceTarget);
	if (!g_lspAppService.requestCurrentEditorCommand(profile, document, *editor, command, position, completionTriggerCandidate, errorMessage)) {
		g_lspLastRequestState = "failed";
		g_lspLastError = errorMessage;
		++g_lspRequestFailureCount;
		if (reportMessages) postLspError(g_lspLastRequestLabel + " failed: " + errorMessage);
		return true;
	}
	g_lspLastRequestState = "requested";
	if (requestSent != nullptr) *requestSent = true;
	if (command == mr::services::MRLspServiceCommandId::ShowHover) {
		g_lspHoverViewAnchor.requestId = g_lspAppService.activeHoverRequestId();
		g_lspHoverViewAnchor.bufferId = document.bufferId;
		g_lspHoverViewAnchor.documentVersion = document.documentVersion;
		g_lspHoverViewAnchor.target = *target;
	}
	if (command == mr::services::MRLspServiceCommandId::SignatureHelp) {
		g_lspSignatureHelpViewAnchor.requestId = g_lspAppService.activeSignatureHelpRequestId();
		g_lspSignatureHelpViewAnchor.bufferId = document.bufferId;
		g_lspSignatureHelpViewAnchor.documentVersion = document.documentVersion;
		g_lspSignatureHelpViewAnchor.target = hasSignatureHelpContext ? signatureHelpContext.anchorTarget : *target;
		if (hasSignatureHelpContext) activateLspSignatureHelp(document, win, signatureHelpContext, g_lspSignatureHelpViewAnchor.requestId);
	}
	if (reportMessages && (command == mr::services::MRLspServiceCommandId::ShowHover || command == mr::services::MRLspServiceCommandId::SignatureHelp)) {
		const int quietMilliseconds = command == mr::services::MRLspServiceCommandId::SignatureHelp ? configuredLanguageServerSignatureQuietMs() : 0;
		suppressLspAutoHoverForExplicitSidekick(document, win, *target, quietMilliseconds);
	}
	if (reportMessages) postLspInfo(g_lspLastRequestLabel + " requested.");
	return true;
}

bool requestLspEditorCommand(mr::services::MRLspServiceCommandId command, const char *label, bool *requestSent = nullptr) {
	return requestLspEditorCommandForWindow(currentEditorCommandWindow(), command, label, true, requestSent);
}

void retirePendingLspAutoHoverRequest() {
	if (!g_lspAutoHover.requested || g_lspAutoHover.requestId.empty()) return;
	g_lspAutoHover.retiredRequestId = g_lspAutoHover.requestId;
	g_lspAutoHover.requestId.clear();
	g_lspAutoHover.requested = false;
}

void forgetLspAutoHoverForWindow(MREditWindow *win, bool retirePendingRequest) {
	if (retirePendingRequest) retirePendingLspAutoHoverRequest();
	if (win != nullptr && (g_lspAutoHover.sidekickOpen || mrHasReadOnlySidekickForParent(win)) && win->bufferId() == g_lspAutoHover.bufferId) mrDropSidekickForParent(win);
	g_lspAutoHover.valid = false;
	g_lspAutoHover.requested = false;
	g_lspAutoHover.requestId.clear();
	g_lspAutoHover.sidekickOpen = false;
	g_lspAutoHover.dismissedForKey = false;
	g_lspAutoHover.quietUntil = std::chrono::steady_clock::time_point();
}

void forgetLspAutoHover(bool retirePendingRequest) {
	MREditWindow *window = g_lspAutoHover.bufferId != 0 ? findEditWindowByBufferId(g_lspAutoHover.bufferId) : nullptr;

	forgetLspAutoHoverForWindow(window, retirePendingRequest);
}

void clearLspSignatureHelpState(MREditWindow *win) {
	if (win != nullptr && g_lspSignatureHelp.active && win->bufferId() == g_lspSignatureHelp.bufferId) mrDropSidekickForParent(win);
	g_lspSignatureHelp = LspSignatureHelpState();
}

void activateLspSignatureHelp(const mr::services::MRWorkspaceDocumentSnapshot &document, MREditWindow *win, const LspSignatureCallContext &context, const std::string &requestId) {
	if (win == nullptr) return;
	g_lspSignatureHelp.active = true;
	g_lspSignatureHelp.requestPending = !requestId.empty();
	g_lspSignatureHelp.bufferId = win->bufferId();
	g_lspSignatureHelp.documentId = document.documentId;
	g_lspSignatureHelp.openParenOffset = context.openParenOffset;
	g_lspSignatureHelp.lastRequestOffset = context.requestTarget.offset;
	g_lspSignatureHelp.lastRequestDocumentVersion = document.documentVersion;
	g_lspSignatureHelp.requestId = requestId;
}

bool lspAutoHoverMatches(const mr::services::MRWorkspaceDocumentSnapshot &document, int bufferId, std::size_t cursorOffset, const mr::lsp::LspTextPosition &position) noexcept {
	if (!g_lspAutoHover.valid) return false;
	if (g_lspAutoHover.bufferId != bufferId) return false;
	if (g_lspAutoHover.documentId != document.documentId) return false;
	if (g_lspAutoHover.documentVersion != document.documentVersion) return false;
	if (g_lspAutoHover.cursorOffset != cursorOffset) return false;
	if (g_lspAutoHover.line != position.line) return false;
	if (g_lspAutoHover.character != position.character) return false;
	return true;
}

bool currentLspHoverMousePosition(TPoint &where) noexcept {
	if (!g_lspMousePositionKnown) return false;
	where = g_lspLastMousePosition;
	return true;
}

bool lspModalViewActive() noexcept {
	TView *current = TProgram::deskTop != nullptr ? TProgram::deskTop->current : nullptr;

	return current != nullptr && (current->state & sfModal) != 0;
}

bool lspAutoHoverMouseMatches(TPoint where) noexcept {
	if (!g_lspAutoHover.mouseValid) return false;
	return g_lspAutoHover.mouseX == where.x && g_lspAutoHover.mouseY == where.y;
}

void armLspAutoHoverForPosition(const mr::services::MRWorkspaceDocumentSnapshot &document, int bufferId, std::size_t cursorOffset, const mr::lsp::LspTextPosition &position, TPoint mouse, bool mouseValid, std::chrono::steady_clock::time_point now) {
	g_lspAutoHover.bufferId = bufferId;
	g_lspAutoHover.documentId = document.documentId;
	g_lspAutoHover.documentVersion = document.documentVersion;
	g_lspAutoHover.cursorOffset = cursorOffset;
	g_lspAutoHover.line = position.line;
	g_lspAutoHover.character = position.character;
	g_lspAutoHover.mouseX = mouseValid ? mouse.x : -1;
	g_lspAutoHover.mouseY = mouseValid ? mouse.y : -1;
	g_lspAutoHover.mouseValid = mouseValid;
	g_lspAutoHover.valid = true;
	g_lspAutoHover.requested = false;
	g_lspAutoHover.requestId.clear();
	g_lspAutoHover.sidekickOpen = false;
	g_lspAutoHover.dismissedForKey = false;
	g_lspAutoHover.stableSince = now;
	g_lspAutoHover.quietUntil = std::chrono::steady_clock::time_point();
}

void suppressLspAutoHoverForExplicitSidekick(const mr::services::MRWorkspaceDocumentSnapshot &document, MREditWindow *win, const LspEditorRequestTarget &target, int quietMilliseconds) {
	TPoint mousePosition;
	const bool mouseValid = currentLspHoverMousePosition(mousePosition);
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	if (win == nullptr) return;
	if (g_lspAutoHover.sidekickOpen && g_lspAutoHover.bufferId == win->bufferId()) mrDropSidekickForParent(win);
	retirePendingLspAutoHoverRequest();
	armLspAutoHoverForPosition(document, win->bufferId(), target.offset, target.position, mousePosition, mouseValid, now);
	g_lspAutoHover.dismissedForKey = true;
	g_lspAutoHover.quietUntil = quietMilliseconds > 0 ? now + std::chrono::milliseconds(quietMilliseconds) : now;
	g_lspAutoHover.sidekickOpen = false;
}

void suppressLspAutoHoverForCompletion(const mr::services::MRWorkspaceDocumentSnapshot &document, MREditWindow *win, const LspEditorRequestTarget &target) {
	TPoint mousePosition;
	const bool mouseValid = currentLspHoverMousePosition(mousePosition);
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	int quietMilliseconds = configuredLanguageServerHoverDwellMs();

	if (quietMilliseconds < 250) quietMilliseconds = 250;
	if (win == nullptr) return;
	if ((g_lspAutoHover.sidekickOpen || mrHasReadOnlySidekickForParent(win)) && g_lspAutoHover.bufferId == win->bufferId()) mrDropSidekickForParent(win);
	retirePendingLspAutoHoverRequest();
	armLspAutoHoverForPosition(document, win->bufferId(), target.offset, target.position, mousePosition, mouseValid, now);
	g_lspAutoHover.dismissedForKey = true;
	g_lspAutoHover.quietUntil = now + std::chrono::milliseconds(quietMilliseconds);
	g_lspAutoHover.sidekickOpen = false;
}

bool syncCurrentEditorForLspResults() {
	MREditWindow *win = currentEditorCommandWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	mr::services::MRLspServerProfile profile;
	mr::services::MRWorkspaceDocumentSnapshot document;
	bool diagnosticsReceived = false;
	std::string errorMessage;
	std::string configurationSource;

	if (!configuredLanguageServerSpawnDaemon()) {
		postLspWarning("LSP support is disabled.");
		return false;
	}
	if (win == nullptr || editor == nullptr) return true;
	if (!buildCurrentLspDocumentSnapshot(win, document)) return false;
	if (!buildLspServerProfileFromEditor(*editor, profile, configurationSource, errorMessage)) {
		postLspWarning(errorMessage.empty() ? "LSP server not configured." : errorMessage);
		return false;
	}

	g_lspLastServerExecutable = profile.executablePath;
	g_lspLastServerWorkingDirectory = profile.workingDirectory;
	g_lspLastServerArguments = mr::services::lspServerProfileArgumentText(profile);
	g_lspLastServerConfigurationSource = configurationSource;
	g_lspLastServerProfileName = profile.profileName;
	g_lspLastRequestPath = document.path;
	g_lspAppService.clearMainFile();
	if (!g_lspAppService.syncCurrentEditorDocument(profile, document, *editor, errorMessage)) {
		postLspError("LSP results sync failed: " + errorMessage);
		return false;
	}
	for (int i = 0; i < 100; ++i) {
		if (!g_lspAppService.poll(errorMessage)) {
			postLspError("LSP poll failed: " + errorMessage);
			g_lspAppService.close();
			return false;
		}
		for (const mr::services::MRServiceDiagnosticResult &result : g_lspAppService.results().diagnosticResults()) {
			const mr::services::MRServiceDocumentIdentity &identity = result.header.identity;

			if (result.header.state != mr::services::MRServiceResultState::Current) continue;
			if (identity.path != document.path) continue;
			if (identity.documentId != document.documentId) continue;
			if (identity.documentVersion != document.documentVersion) continue;
			diagnosticsReceived = true;
			break;
		}
		if (diagnosticsReceived) return true;
		::poll(nullptr, 0, 20);
	}
	postLspWarning("LSP diagnostics for current editor state not received yet.");
	if (!g_lspAppService.results().diagnosticResults().empty()) return true;
	if (!g_lspAppService.results().locationResults().empty()) return true;
	if (!g_lspAppService.results().hoverResults().empty()) return true;
	if (!g_lspAppService.results().completionResults().empty()) return true;
	return false;
}

MREditWindow *findOpenLspTargetWindow(const std::string &path) {
	const std::string normalizedTarget = mr::services::normalizeWorkspaceServicePath(path);
	const std::vector<MREditWindow *> windows = allEditWindowsAndBentoPanesInZOrder();

	for (MREditWindow *window : windows) {
		MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;

		if (editor == nullptr || !editor->hasPersistentFileName()) continue;
		if (mr::services::normalizeWorkspaceServicePath(editor->persistentFileName()) == normalizedTarget) return window;
	}
	return nullptr;
}

MREditWindow *findLspResultTargetWindow(const mr::services::MRServiceDocumentIdentity &identity) {
	if (identity.bufferId != 0) {
		MREditWindow *window = findEditWindowByBufferId(identity.bufferId);

		if (window != nullptr) return window;
	}
	if (!identity.path.empty()) return findOpenLspTargetWindow(identity.path);
	return nullptr;
}

bool activateLspTargetWindow(MREditWindow *window) {
	if (window == nullptr) return false;
	if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window)) return bentoBox->activatePaneWindow(window);
	if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window->owner)) return bentoBox->activatePaneWindow(window);
	return mrActivateEditWindow(window);
}

bool lspVisualColumnForTarget(MRFileEditor &editor, const mr::services::MRServiceTextPosition &position, int &visualColumn);
bool lspTextOffsetForPosition(const std::string &text, const mr::services::MRServiceTextPosition &position, std::size_t &offset);

std::string lspIdentifierAtOffset(const std::string &text, std::size_t offset) {
	std::size_t start = std::min(offset, text.size());
	std::size_t end = start;

	if (start == text.size() || !lspIdentifierByte(text[start])) {
		if (start == 0 || !lspIdentifierByte(text[start - 1])) return std::string();
		--start;
	}
	while (start > 0 && lspIdentifierByte(text[start - 1]))
		--start;
	end = start;
	while (end < text.size() && lspIdentifierByte(text[end]))
		++end;
	return text.substr(start, end - start);
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
	if (!lspVisualColumnForTarget(*editor, target.range.start, column) && target.range.start.character > 0) column = target.range.start.character;
	editor->moveCursorToDocumentLineTop(line, column);
	editor->revealCursor(True);
	static_cast<void>(activateLspTargetWindow(window));
	errorMessage.clear();
	return true;
}

std::string lspLocationDisplayText(const mr::services::MRServiceLocationTarget &target) {
	std::ostringstream line;
	int visualColumn = target.range.start.character;

	if (!target.path.empty()) {
		MREditWindow *window = findOpenLspTargetWindow(target.path);
		MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;

		if (editor != nullptr) static_cast<void>(lspVisualColumnForTarget(*editor, target.range.start, visualColumn));
	}
	line << (target.range.start.line + 1) << ":" << (visualColumn + 1) << " ";
	line << (!target.path.empty() ? target.path : target.uri);
	return line.str();
}

bool lspLocationContainsPosition(const mr::services::MRServiceLocationTarget &target, const std::string &path, const mr::services::MRServiceTextPosition &position) {
	if (target.path.empty() || mr::services::normalizeWorkspaceServicePath(target.path) != mr::services::normalizeWorkspaceServicePath(path)) return false;
	if (position.line < target.range.start.line || position.line > target.range.end.line) return false;
	if (position.line == target.range.start.line && position.character < target.range.start.character) return false;
	if (position.line == target.range.end.line && position.character > target.range.end.character) return false;
	return true;
}

bool lspLocationMatchesDocumentSymbol(const mr::services::MRServiceLocationTarget &target, const std::string &path, const std::string &symbol) {
	MREditWindow *window = nullptr;
	MRFileEditor *editor = nullptr;
	std::size_t offset = 0;
	std::string text;

	if (symbol.empty()) return true;
	if (target.path.empty() || mr::services::normalizeWorkspaceServicePath(target.path) != mr::services::normalizeWorkspaceServicePath(path)) return true;
	window = findOpenLspTargetWindow(target.path);
	editor = window != nullptr ? window->getEditor() : nullptr;
	if (editor == nullptr) return true;
	text = editor->snapshotText();
	if (!lspTextOffsetForPosition(text, target.range.start, offset)) return false;
	return lspIdentifierAtOffset(text, offset) == symbol;
}

MREditWindow *findLspDiagnosticTargetWindow(const mr::services::MRServiceDocumentIdentity &identity) {
	if (identity.bufferId != 0) {
		MREditWindow *window = findEditWindowByBufferId(identity.bufferId);

		if (window != nullptr) return window;
	}
	if (!identity.path.empty()) return findOpenLspTargetWindow(identity.path);
	return nullptr;
}

bool lspServiceResultMatchesEditor(const mr::services::MRServiceResultHeader &header, const MRFileEditor &editor) noexcept {
	if (header.state != mr::services::MRServiceResultState::Current) return false;
	if (header.identity.documentId != editor.documentId()) return false;
	if (header.identity.documentVersion != editor.documentVersion()) return false;
	return true;
}

void appendLspDiagnosticInformationRange(const std::string &text, const mr::services::MRServiceTextRange &range, std::vector<std::pair<std::size_t, std::size_t>> &ranges) {
	std::size_t start = 0;
	std::size_t end = 0;

	if (!lspTextOffsetForPosition(text, range.start, start)) return;
	if (!lspTextOffsetForPosition(text, range.end, end)) return;
	ranges.push_back(std::make_pair(start, end));
}

void applyLspDiagnosticInformationRanges(const mr::services::MRServiceDocumentIdentity &identity) {
	MREditWindow *window = findLspDiagnosticTargetWindow(identity);
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	std::vector<std::pair<std::size_t, std::size_t>> ranges;
	std::string text;

	if (editor == nullptr) return;
	if (identity.documentId != editor->documentId()) return;
	if (identity.documentVersion != editor->documentVersion()) return;
	text = editor->snapshotText();
	for (const mr::services::MRServiceDiagnosticResult &result : g_lspAppService.results().diagnosticResults()) {
		if (!lspServiceResultMatchesEditor(result.header, *editor)) continue;
		for (const mr::services::MRServiceDiagnosticEntry &diagnostic : result.diagnostics)
			appendLspDiagnosticInformationRange(text, diagnostic.reportedRange, ranges);
	}
	if (ranges.empty())
		editor->clearLspDiagnosticInformationRanges();
	else
		editor->setLspDiagnosticInformationRanges(ranges);
}

void applyLspDocumentHighlightRanges(const mr::services::MRServiceDocumentHighlightResult &result) {
	MREditWindow *window = findLspResultTargetWindow(result.header.identity);
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	std::vector<std::pair<std::size_t, std::size_t>> ranges;
	std::string text;

	if (editor == nullptr) return;
	if (result.header.identity.documentId != editor->documentId()) return;
	if (result.header.identity.documentVersion != editor->documentVersion()) return;
	text = editor->snapshotText();
	for (const mr::services::MRServiceDocumentHighlightEntry &highlight : result.highlights) {
		std::size_t start = 0;
		std::size_t end = 0;

		if (!lspTextOffsetForPosition(text, highlight.range.start, start)) continue;
		if (!lspTextOffsetForPosition(text, highlight.range.end, end)) continue;
		if (end > start) ranges.push_back(std::make_pair(start, end));
	}
	if (ranges.empty())
		editor->clearLspDocumentHighlightRanges();
	else
		editor->setLspDocumentHighlightRanges(ranges);
}

std::vector<mr::services::MRServiceLocationTarget> lspReferenceDialogLocations(const mr::services::MRServiceLocationResult &result) {
	MREditWindow *window = currentEditorCommandWindow();
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	std::vector<mr::services::MRServiceLocationTarget> locations;
	mr::services::MRServiceTextPosition currentPosition;
	std::string currentPath;
	std::string currentSymbol;

	if (editor != nullptr && editor->hasPersistentFileName()) {
		std::size_t currentOffset = 0;
		const std::string currentText = editor->snapshotText();

		currentPath = editor->persistentFileName();
		currentPosition = serviceTextPositionFromLsp(currentLspTextPosition(*editor));
		if (lspTextOffsetForPosition(currentText, currentPosition, currentOffset)) currentSymbol = lspIdentifierAtOffset(currentText, currentOffset);
	}
	locations.reserve(result.locations.size());
	for (const mr::services::MRServiceLocationTarget &target : result.locations) {
		if (!currentPath.empty() && lspLocationContainsPosition(target, currentPath, currentPosition)) continue;
		if (!currentPath.empty() && !lspLocationMatchesDocumentSymbol(target, currentPath, currentSymbol)) continue;
		locations.push_back(target);
	}
	return locations;
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
	ShowHover,
	ApplyCodeAction
};

struct LspResultDialogRow {
	std::string text;
	LspResultDialogAction action = LspResultDialogAction::None;
	mr::services::MRServiceLocationTarget location;
	mr::services::MRServiceHoverResult hover;
	const mr::services::MRServiceCodeActionResult *codeActionResult = nullptr;
	const mr::services::MRServiceCodeActionItem *codeActionItem = nullptr;
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
		if (!completionItem->documentation.empty()) text += " - " + firstDisplayLine(completionItem->documentation, 64);
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

struct LspResolvedTextEdit {
	std::size_t start = 0;
	std::size_t end = 0;
	std::string text;
};

struct LspResolvedWorkspaceTextEdit {
	MREditWindow *window = nullptr;
	MRFileEditor *editor = nullptr;
	std::string path;
	std::size_t start = 0;
	std::size_t end = 0;
	std::string text;
};

struct LspMiniMenuEntry {
	std::string title;
	ushort command = 0;
	bool editSubmenu = false;
};

bool lspCodeActionEditTargetsCurrentDocument(const mr::services::MRServiceCodeActionItem &item, const mr::services::MRServiceDocumentIdentity &identity) {
	if (!item.hasEdit || item.edits.empty()) return false;
	if (identity.path.empty()) return false;
	for (const mr::services::MRServiceTextEdit &edit : item.edits) {
		if (edit.path.empty()) return false;
		if (edit.path != identity.path) return false;
	}
	return true;
}

bool lspTextOffsetForPosition(const std::string &text, const mr::services::MRServiceTextPosition &position, std::size_t &offset) {
	std::size_t line = 0;
	std::size_t lineStart = 0;
	std::size_t lineEnd = 0;

	offset = 0;
	if (position.line < 0 || position.character < 0) return false;
	while (line < static_cast<std::size_t>(position.line)) {
		const std::size_t nextBreak = text.find('\n', lineStart);

		if (nextBreak == std::string::npos) return false;
		lineStart = nextBreak + 1;
		++line;
	}
	lineEnd = text.find('\n', lineStart);
	if (lineEnd == std::string::npos) lineEnd = text.size();
	if (static_cast<std::size_t>(position.character) > lineEnd - lineStart) return false;
	offset = lineStart + static_cast<std::size_t>(position.character);
	return true;
}

bool lspVisualColumnForTarget(MRFileEditor &editor, const mr::services::MRServiceTextPosition &position, int &visualColumn) {
	std::size_t offset = 0;
	const std::string currentText = editor.snapshotText();

	if (!lspTextOffsetForPosition(currentText, position, offset)) return false;
	visualColumn = editor.charColumn(editor.lineStartOffset(offset), offset);
	return true;
}

bool applyLspCodeActionEdits(MRFileEditor &editor, const mr::services::MRServiceCodeActionResult &result, const mr::services::MRServiceCodeActionItem &item, std::string &errorMessage) {
	std::vector<LspResolvedTextEdit> resolvedEdits;
	const std::string currentText = editor.snapshotText();

	if (result.header.state != mr::services::MRServiceResultState::Current) {
		errorMessage = "LSP code action result is not current.";
		return false;
	}
	if (result.header.identity.documentVersion != editor.documentVersion()) {
		errorMessage = "LSP code action belongs to an older document version.";
		return false;
	}
	if (!lspCodeActionEditTargetsCurrentDocument(item, result.header.identity)) {
		errorMessage = "LSP code action has no applicable same-document edit.";
		return false;
	}
	for (const mr::services::MRServiceTextEdit &edit : item.edits) {
		LspResolvedTextEdit resolved;

		if (!lspTextOffsetForPosition(currentText, edit.range.start, resolved.start) || !lspTextOffsetForPosition(currentText, edit.range.end, resolved.end)) {
			errorMessage = "LSP code action edit range is outside the document.";
			return false;
		}
		if (resolved.end < resolved.start) {
			errorMessage = "LSP code action edit range is reversed.";
			return false;
		}
		resolved.text = edit.newText;
		resolvedEdits.push_back(resolved);
	}
	std::sort(resolvedEdits.begin(), resolvedEdits.end(), [](const LspResolvedTextEdit &left, const LspResolvedTextEdit &right) {
		if (left.start != right.start) return left.start < right.start;
		return left.end < right.end;
	});
	for (std::size_t i = 1; i < resolvedEdits.size(); ++i) {
		if (resolvedEdits[i].start < resolvedEdits[i - 1].end) {
			errorMessage = "LSP code action edits overlap.";
			return false;
		}
	}
	for (std::size_t i = resolvedEdits.size(); i > 0; --i) {
		const LspResolvedTextEdit &edit = resolvedEdits[i - 1];

		if (edit.start > static_cast<std::size_t>(std::numeric_limits<uint>::max()) || edit.end > static_cast<std::size_t>(std::numeric_limits<uint>::max())) {
			errorMessage = "LSP code action edit range is too large.";
			return false;
		}
		if (!editor.replaceRangeAndSelect(static_cast<uint>(edit.start), static_cast<uint>(edit.end), edit.text.data(), static_cast<uint>(edit.text.size()))) {
			errorMessage = "LSP code action editor replace failed.";
			return false;
		}
	}
	errorMessage.clear();
	return true;
}

bool applyLspRenameEdits(const mr::services::MRServiceRenameResult &result, const mr::services::MRWorkspaceServiceSnapshot &workspace, std::string &errorMessage) {
	std::vector<LspResolvedWorkspaceTextEdit> resolvedEdits;

	if (result.header.state != mr::services::MRServiceResultState::Current) {
		errorMessage = "LSP rename result is not current.";
		return false;
	}
	if (result.edits.empty()) {
		errorMessage = "LSP rename returned no edits.";
		return false;
	}
	for (const mr::services::MRServiceTextEdit &edit : result.edits) {
		MREditWindow *window = nullptr;
		MRFileEditor *editor = nullptr;
		std::string currentText;
		bool targetLoaded = false;
		LspResolvedWorkspaceTextEdit resolved;

		if (edit.path.empty()) {
			errorMessage = "LSP rename edit has no file path.";
			return false;
		}
		for (const mr::services::MRWorkspaceDocumentSnapshot &document : workspace.documents) {
			if (document.path != edit.path) continue;
			window = findEditWindowByBufferId(document.bufferId);
			targetLoaded = window != nullptr;
			break;
		}
		if (!targetLoaded || window == nullptr) {
			errorMessage = "LSP rename edit targets an unloaded document: " + edit.path;
			return false;
		}
		editor = window->getEditor();
		if (editor == nullptr) {
			errorMessage = "LSP rename edit target has no editor: " + edit.path;
			return false;
		}
		if (window->isReadOnly() || editor->isReadOnly()) {
			errorMessage = "LSP rename edit target is read-only: " + edit.path;
			return false;
		}
		if (edit.path == result.header.identity.path && result.header.identity.documentVersion != editor->documentVersion()) {
			errorMessage = "LSP rename belongs to an older document version.";
			return false;
		}
		currentText = editor->snapshotText();
		if (!lspTextOffsetForPosition(currentText, edit.range.start, resolved.start) || !lspTextOffsetForPosition(currentText, edit.range.end, resolved.end)) {
			errorMessage = "LSP rename edit range is outside the document: " + edit.path;
			return false;
		}
		if (resolved.end < resolved.start) {
			errorMessage = "LSP rename edit range is reversed: " + edit.path;
			return false;
		}
		if (resolved.start > static_cast<std::size_t>(std::numeric_limits<uint>::max()) || resolved.end > static_cast<std::size_t>(std::numeric_limits<uint>::max()) || edit.newText.size() > static_cast<std::size_t>(std::numeric_limits<uint>::max())) {
			errorMessage = "LSP rename edit range is too large: " + edit.path;
			return false;
		}
		resolved.window = window;
		resolved.editor = editor;
		resolved.path = edit.path;
		resolved.text = edit.newText;
		resolvedEdits.push_back(resolved);
	}
	std::sort(resolvedEdits.begin(), resolvedEdits.end(), [](const LspResolvedWorkspaceTextEdit &left, const LspResolvedWorkspaceTextEdit &right) {
		if (left.path != right.path) return left.path < right.path;
		if (left.start != right.start) return left.start < right.start;
		return left.end < right.end;
	});
	for (std::size_t i = 1; i < resolvedEdits.size(); ++i) {
		if (resolvedEdits[i].path != resolvedEdits[i - 1].path) continue;
		if (resolvedEdits[i].start < resolvedEdits[i - 1].end) {
			errorMessage = "LSP rename edits overlap: " + resolvedEdits[i].path;
			return false;
		}
	}
	for (std::size_t i = resolvedEdits.size(); i > 0; --i) {
		const LspResolvedWorkspaceTextEdit &edit = resolvedEdits[i - 1];

		if (!edit.editor->replaceRangeAndSelect(static_cast<uint>(edit.start), static_cast<uint>(edit.end), edit.text.data(), static_cast<uint>(edit.text.size()))) {
			errorMessage = "LSP rename editor replace failed: " + edit.path;
			return false;
		}
	}
	errorMessage.clear();
	return true;
}

bool showLspReferencesDialog(const mr::services::MRServiceLocationResult &result) {
	MRDialogFoundation *dialog = nullptr;
	TScrollBar *scrollBar = nullptr;
	LspReferencesListView *listView = nullptr;
	ushort dialogResult = cmCancel;
	std::size_t selectedIndex = 0;
	mr::services::MRServiceLocationTarget target;
	std::string navigationError;
	const std::vector<mr::services::MRServiceLocationTarget> locations = lspReferenceDialogLocations(result);
	const int visibleRows = std::max<int>(4, std::min<int>(static_cast<int>(locations.size()), 12));
	const short width = 96;
	const short height = static_cast<short>(visibleRows + 6);
	const short buttonY = static_cast<short>(height - 3);

	if (TProgram::deskTop == nullptr) return false;
	if (locations.empty()) {
		postLspWarning("LSP references: no references found.");
		return true;
	}

	dialog = mr::dialogs::createScrollableDialog("LSP REFERENCES", width, height);
	dialog->insert(new TStaticText(TRect(2, 1, width - 2, 2), "Select reference:"));
	scrollBar = new TScrollBar(TRect(width - 3, 2, width - 2, height - 4));
	dialog->insert(scrollBar);
	listView = new LspReferencesListView(TRect(2, 2, width - 3, height - 4), scrollBar, locations);
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
	if (dialogResult == cmOK && listView != nullptr && listView->selectedIndex(selectedIndex) && selectedIndex < locations.size()) target = locations[selectedIndex];
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

std::string lspHoverStripInlineMarkdown(const std::string &line) {
	std::string cleaned;
	std::string linkStripped;

	linkStripped.reserve(line.size());
	for (std::size_t i = 0; i < line.size();) {
		if (line[i] == '[') {
			const std::size_t labelEnd = line.find(']', i + 1);

			if (labelEnd != std::string::npos && labelEnd + 1 < line.size() && line[labelEnd + 1] == '(') {
				const std::size_t targetEnd = line.find(')', labelEnd + 2);

				if (targetEnd != std::string::npos) {
					linkStripped.append(line, i + 1, labelEnd - i - 1);
					i = targetEnd + 1;
					continue;
				}
			}
		}
		linkStripped.push_back(line[i]);
		++i;
	}

	cleaned.reserve(linkStripped.size());
	for (std::size_t i = 0; i < linkStripped.size(); ++i) {
		const char ch = linkStripped[i];

		if (ch == '`' || ch == '*') continue;
		cleaned.push_back(ch);
	}
	return trimAscii(cleaned);
}

std::string lspHoverPlainText(const std::string &text) {
	std::istringstream input(text);
	std::ostringstream output;
	std::string line;
	bool inFence = false;
	bool previousBlank = true;

	while (std::getline(input, line)) {
		std::string trimmed = trimAscii(line);

		if (trimmed.rfind("```", 0) == 0 || trimmed.rfind("~~~", 0) == 0) {
			inFence = !inFence;
			continue;
		}
		if (!inFence && (trimmed == "---" || trimmed == "***")) continue;
		while (!trimmed.empty() && trimmed[0] == '#')
			trimmed.erase(trimmed.begin());
		trimmed = lspHoverStripInlineMarkdown(trimmed);
		if (trimmed.empty()) {
			if (!previousBlank) output << '\n';
			previousBlank = true;
			continue;
		}
		if (!previousBlank) output << '\n';
		output << trimmed;
		previousBlank = false;
	}
	return output.str();
}

std::string buildLspHoverSidekickText(const mr::services::MRServiceHoverResult &result) {
	std::vector<std::string> lines;
	const std::size_t textWidth = 72;
	const std::size_t maxLines = 8;
	std::ostringstream text;

	lines = buildLspHoverDisplayLines(lspHoverPlainText(result.hover.value), textWidth, maxLines);
	for (std::size_t i = 0; i < lines.size(); ++i) {
		if (i != 0) text << '\n';
		text << lines[i];
	}
	return text.str();
}

std::string buildLspDiagnosticSidekickText(const mr::services::MRLspPositionServiceSnapshot &snapshot) {
	std::ostringstream text;
	std::size_t count = 0;

	for (const mr::services::MRServiceDiagnosticResult &result : snapshot.results.diagnostics) {
		for (const mr::services::MRServiceDiagnosticEntry &diagnostic : result.diagnostics) {
			if (count != 0) text << '\n';
			text << lspDiagnosticSeverityText(diagnostic.severity) << " ";
			text << (diagnostic.reportedRange.start.line + 1) << ":" << (diagnostic.reportedRange.start.character + 1);
			text << " - " << firstDisplayLine(diagnostic.message, 72);
			++count;
			if (count >= 4) return text.str();
		}
	}
	return text.str();
}

bool lspDiagnosticSidekickAnchor(const mr::services::MRLspPositionServiceSnapshot &snapshot, MRFileEditor &editor, int &viewColumn, int &viewRow) {
	const std::string currentText = editor.snapshotText();

	for (const mr::services::MRServiceDiagnosticResult &result : snapshot.results.diagnostics) {
		for (const mr::services::MRServiceDiagnosticEntry &diagnostic : result.diagnostics) {
			std::size_t offset = 0;
			std::size_t lineStart = 0;
			const int requestedViewColumn = viewColumn;
			if (diagnostic.reportedRange.start.line < 0) continue;
			const std::size_t lineIndex = static_cast<std::size_t>(diagnostic.reportedRange.start.line);
			if (lineIndex >= editor.bufferModel().lineCount()) continue;
			const std::size_t visibleLine = editor.visibleLineForDocumentLine(lineIndex);

			if (!lspTextOffsetForPosition(currentText, diagnostic.reportedRange.start, offset)) {
				viewColumn = std::max(1, requestedViewColumn);
				viewRow = static_cast<int>(visibleLine) - editor.delta.y + 1;
				return true;
			}
			lineStart = editor.bufferModel().lineStartByIndex(lineIndex);
			viewColumn = editor.charColumn(lineStart, offset) - editor.delta.x + 1;
			viewRow = static_cast<int>(visibleLine) - editor.delta.y + 1;
			return true;
		}
	}
	return false;
}

bool currentWindowMatchesLspHoverResult(MREditWindow *win, const mr::services::MRServiceHoverResult &result) {
	mr::services::MRWorkspaceDocumentSnapshot document;
	const mr::services::MRServiceDocumentIdentity &identity = result.header.identity;

	if (win == nullptr) return false;
	if (!buildLspDocumentSnapshotForWindow(win, document, false)) return false;
	if (identity.bufferId != 0 && identity.bufferId != document.bufferId) return false;
	if (!identity.path.empty() && mr::services::normalizeWorkspaceServicePath(identity.path) != mr::services::normalizeWorkspaceServicePath(document.path)) return false;
	if (identity.documentId != 0 && document.documentId != 0 && identity.documentId != document.documentId) return false;
	if (identity.documentVersion != document.documentVersion) return false;
	return true;
}

bool lspHoverResultBelongsToRetiredAutoHover(const mr::services::MRServiceHoverResult &result) {
	if (result.header.requestId.empty() || result.header.requestId != g_lspAutoHover.retiredRequestId) return false;
	g_lspAutoHover.retiredRequestId.clear();
	return true;
}

bool lspHoverResultMatchesPendingAutoHover(const mr::services::MRServiceHoverResult &result) {
	if (!g_lspAutoHover.requested) return false;
	if (g_lspAutoHover.requestId.empty() || result.header.requestId != g_lspAutoHover.requestId) return false;
	if (result.header.identity.bufferId != 0 && g_lspAutoHover.bufferId != 0 && result.header.identity.bufferId != g_lspAutoHover.bufferId) return false;
	if (result.header.identity.documentId != 0 && g_lspAutoHover.documentId != 0 && result.header.identity.documentId != g_lspAutoHover.documentId) return false;
	if (result.header.identity.documentVersion != g_lspAutoHover.documentVersion) return false;
	g_lspAutoHover.requested = false;
	g_lspAutoHover.requestId.clear();
	return true;
}

bool lspAutoHoverResultStillAtRequestAnchor(const mr::services::MRServiceHoverResult &result) {
	TPoint where;
	LspEditorRequestTarget target;
	MREditWindow *win = nullptr;
	mr::services::MRWorkspaceDocumentSnapshot document;

	if (!currentLspHoverMousePosition(where)) return false;
	win = lspEditorWindowAtGlobalPoint(where, target);
	if (win == nullptr) return false;
	if (g_lspHoverViewAnchor.bufferId != 0 && win->bufferId() != g_lspHoverViewAnchor.bufferId) return false;
	if (!buildLspDocumentSnapshotForWindow(win, document, false)) return false;
	if (result.header.identity.documentVersion != 0 && document.documentVersion != result.header.identity.documentVersion) return false;
	if (target.offset != g_lspHoverViewAnchor.target.offset) return false;
	if (target.position.line != g_lspHoverViewAnchor.target.position.line) return false;
	if (target.position.character != g_lspHoverViewAnchor.target.position.character) return false;
	return true;
}

bool lspHoverTargetForResult(const mr::services::MRServiceHoverResult &result, MREditWindow *win, LspEditorRequestTarget &target) {
	if (win == nullptr) return false;
	if (result.header.requestId.empty() || result.header.requestId != g_lspHoverViewAnchor.requestId) return false;
	if (g_lspHoverViewAnchor.bufferId != 0 && win->bufferId() != g_lspHoverViewAnchor.bufferId) return false;
	if (result.header.identity.documentVersion != 0 && g_lspHoverViewAnchor.documentVersion != result.header.identity.documentVersion) return false;
	target = g_lspHoverViewAnchor.target;
	return true;
}

bool currentWindowMatchesLspSignatureHelpResult(MREditWindow *win, const mr::services::MRServiceSignatureHelpResult &result) {
	mr::services::MRWorkspaceDocumentSnapshot document;
	const mr::services::MRServiceDocumentIdentity &identity = result.header.identity;

	if (win == nullptr) return false;
	if (!buildLspDocumentSnapshotForWindow(win, document, false)) return false;
	if (identity.bufferId != 0 && identity.bufferId != document.bufferId) return false;
	if (!identity.path.empty() && mr::services::normalizeWorkspaceServicePath(identity.path) != mr::services::normalizeWorkspaceServicePath(document.path)) return false;
	if (identity.documentId != 0 && document.documentId != 0 && identity.documentId != document.documentId) return false;
	if (identity.documentVersion != document.documentVersion) return false;
	return true;
}

bool lspSignatureHelpTargetForResult(const mr::services::MRServiceSignatureHelpResult &result, MREditWindow *win, LspEditorRequestTarget &target) {
	if (win == nullptr) return false;
	if (result.header.requestId.empty() || result.header.requestId != g_lspSignatureHelpViewAnchor.requestId) return false;
	if (g_lspSignatureHelpViewAnchor.bufferId != 0 && win->bufferId() != g_lspSignatureHelpViewAnchor.bufferId) return false;
	if (result.header.identity.documentVersion != 0 && g_lspSignatureHelpViewAnchor.documentVersion != result.header.identity.documentVersion) return false;
	target = g_lspSignatureHelpViewAnchor.target;
	return true;
}

int lspReadOnlyHoverAnchorRow(int anchorViewRow, MRReadOnlySidekickPlacement, bool) noexcept {
	return anchorViewRow;
}

std::string buildLspSignatureHelpSidekickText(const mr::services::MRServiceSignatureHelpResult &result) {
	std::vector<std::string> lines;
	const std::size_t textWidth = 72;
	const std::size_t maxLines = 8;
	std::ostringstream rawText;
	std::ostringstream text;

	if (result.activeSignature < 0 || static_cast<std::size_t>(result.activeSignature) >= result.signatures.size()) return std::string();
	const mr::services::MRServiceSignatureInformation &signature = result.signatures[static_cast<std::size_t>(result.activeSignature)];
	rawText << signature.label;
	if (result.activeParameter >= 0 && static_cast<std::size_t>(result.activeParameter) < signature.parameters.size()) rawText << "\nparameter: " << signature.parameters[static_cast<std::size_t>(result.activeParameter)].label;
	if (!signature.documentation.empty()) rawText << "\n" << signature.documentation;
	lines = buildLspHoverDisplayLines(lspHoverPlainText(rawText.str()), textWidth, maxLines);
	for (std::size_t i = 0; i < lines.size(); ++i) {
		if (i != 0) text << '\n';
		text << lines[i];
	}
	return text.str();
}

bool showLspHoverSidekick(const mr::services::MRServiceHoverResult &result) {
	MREditWindow *win = findLspResultTargetWindow(result.header.identity);
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	mr::services::MRWorkspaceDocumentSnapshot document;
	LspEditorRequestTarget target;
	std::string text;
	int anchorViewColumn = 1;
	int anchorViewRow = 1;
	int preferredViewColumn = 1;
	bool diagnosticHover = false;
	const MRReadOnlySidekickPlacement placement = configuredLspReadOnlySidekickPlacement();

	if (lspModalViewActive()) return false;
	if (TProgram::deskTop == nullptr || editor == nullptr) return false;
	if (!currentWindowMatchesLspHoverResult(win, result)) return false;
	if (!lspHoverTargetForResult(result, win, target)) return false;
	anchorViewColumn = target.viewColumn;
	anchorViewRow = target.viewRow;
	if (buildLspDocumentSnapshotForWindow(win, document, false)) {
		const mr::services::MRLspPositionServiceSnapshot snapshot = g_lspAppService.currentDocumentPositionServiceSnapshot(document, serviceTextPositionFromLsp(target.position));

		if (!snapshot.results.diagnostics.empty()) {
			text = buildLspDiagnosticSidekickText(snapshot);
			diagnosticHover = !text.empty();
		}
	}
	if (text.empty()) text = buildLspHoverSidekickText(result);
	if (text.empty()) {
		postLspWarning("LSP hover: empty hover.");
		return true;
	}
	preferredViewColumn = anchorViewColumn;
	anchorViewRow = lspReadOnlyHoverAnchorRow(anchorViewRow, placement, diagnosticHover);
	if (mrOpenReadOnlySidekickAt(win, text, "LSP hover", anchorViewColumn, anchorViewRow, preferredViewColumn, placement)) {
		g_lspAutoHover.sidekickOpen = true;
		g_lspAutoHover.bufferId = win->bufferId();
		return true;
	}
	return false;
}

bool showLspSignatureHelpSidekick(const mr::services::MRServiceSignatureHelpResult &result) {
	MREditWindow *win = findLspResultTargetWindow(result.header.identity);
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	LspEditorRequestTarget target;
	const std::string text = buildLspSignatureHelpSidekickText(result);
	int anchorViewColumn = 1;
	int anchorViewRow = 1;
	int preferredViewColumn = 1;
	const MRReadOnlySidekickPlacement placement = configuredLspReadOnlySidekickPlacement();

	if (TProgram::deskTop == nullptr || editor == nullptr) return false;
	if (!currentWindowMatchesLspSignatureHelpResult(win, result)) return false;
	if (!lspSignatureHelpTargetForResult(result, win, target)) return false;
	if (text.empty()) {
		postLspWarning("LSP signature help: no signature.");
		return true;
	}
	anchorViewColumn = target.viewColumn;
	anchorViewRow = lspReadOnlyHoverAnchorRow(target.viewRow, placement, false);
	preferredViewColumn = anchorViewColumn;
	return mrOpenReadOnlySidekickAt(win, text, "LSP signature", anchorViewColumn, anchorViewRow, preferredViewColumn, placement);
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

std::string lspCompletionRawReplacementTextForItem(const mr::services::MRServiceCompletionItem &item) {
	std::string text;

	if (item.hasTextEdit) text = item.textEditNewText;
	else
		text = !item.insertText.empty() ? item.insertText : item.label;
	return text;
}

bool lspCompletionSnippetConsumesOpenParen(MRFileEditor &editor, const mr::services::MRServiceCompletionItem &item, std::size_t start, std::size_t end) {
	std::string identifier;
	std::string replacement;
	std::size_t pos = 0;

	if (!item.hasInsertTextFormat || item.insertTextFormat != 2) return false;
	if (end >= editor.bufferLength() || editor.charAtOffset(end) != '(') return false;
	if (start >= end) return false;
	for (std::size_t index = start; index < end; ++index)
		identifier.push_back(editor.charAtOffset(index));
	replacement = lspCompletionRawReplacementTextForItem(item);
	if (replacement.compare(0, identifier.size(), identifier) != 0) return false;
	pos = identifier.size();
	while (pos < replacement.size() && std::isspace(static_cast<unsigned char>(replacement[pos])) != 0)
		++pos;
	return pos < replacement.size() && replacement[pos] == '(';
}

VirtualMachine::Value mrSnippetIntValue(int value) {
	VirtualMachine::Value result;

	result.type = TYPE_INT;
	result.i = value;
	return result;
}

VirtualMachine::Value mrSnippetStringValue(const std::string &value) {
	VirtualMachine::Value result;

	result.type = TYPE_STR;
	result.s = value;
	return result;
}

void mrSnippetWriteInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const std::string &key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	mrvmHashWriteValue(store, store, hash, key, mrSnippetIntValue(value));
}

void mrSnippetWriteString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const std::string &key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	mrvmHashWriteValue(store, store, hash, key, mrSnippetStringValue(value));
}

bool mrSnippetReadValue(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const std::string &key, VirtualMachine::Value &value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, hash, key)) return false;
	value = mrvmHashReadValue(store, store, hash, key);
	return true;
}

bool mrSnippetReadInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const std::string &key, int &value) {
	VirtualMachine::Value stored;

	if (!mrSnippetReadValue(runtimeKv, hash, key, stored) || stored.type != TYPE_INT) return false;
	value = stored.i;
	return true;
}

bool mrSnippetReadString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const std::string &key, std::string &value) {
	VirtualMachine::Value stored;

	if (!mrSnippetReadValue(runtimeKv, hash, key, stored) || stored.type != TYPE_STR) return false;
	value = stored.s;
	return true;
}

bool lspCompletionEditRange(MRFileEditor &editor, const mr::services::MRServiceCompletionResult &result, const mr::services::MRServiceCompletionItem &item, std::size_t &start, std::size_t &end, std::string &errorMessage) {
	std::size_t requestOffset = 0;
	std::size_t identifierStart = 0;
	std::size_t identifierEnd = 0;
	const std::string currentText = editor.snapshotText();

	if (result.header.identity.documentVersion != editor.documentVersion()) {
		errorMessage = "LSP completion belongs to an older document version.";
		return false;
	}
	if (!result.hasRequestPosition || !lspTextOffsetForPosition(currentText, result.requestPosition, requestOffset)) {
		errorMessage = "LSP completion request position is outside the document.";
		return false;
	}
	const bool hasIdentifierRange = lspIdentifierRangeAroundOffset(editor, requestOffset, identifierStart, identifierEnd);
	if (item.hasTextEdit) {
		if (!lspTextOffsetForPosition(currentText, item.textEditRange.start, start) || !lspTextOffsetForPosition(currentText, item.textEditRange.end, end)) {
			errorMessage = "LSP completion edit range is outside the document.";
			return false;
		}
		if (hasIdentifierRange && start >= identifierStart && end <= identifierEnd) {
			start = identifierStart;
			end = identifierEnd;
		}
	} else {
		if (!hasIdentifierRange) {
			errorMessage = "LSP completion request is not on an identifier.";
			return false;
		}
		start = identifierStart;
		end = identifierEnd;
	}
	if (end < start) {
		errorMessage = "LSP completion edit range is reversed.";
		return false;
	}
	if (start > static_cast<std::size_t>(std::numeric_limits<uint>::max()) || end > static_cast<std::size_t>(std::numeric_limits<uint>::max())) {
		errorMessage = "LSP completion edit range is too large.";
		return false;
	}
	if (lspCompletionSnippetConsumesOpenParen(editor, item, start, end)) ++end;
	errorMessage.clear();
	return true;
}

bool applyLspCompletionItem(MREditWindow *targetWindow, MRFileEditor &editor, const mr::services::MRServiceCompletionResult &result, const mr::services::MRServiceCompletionItem &item, const std::string &insertText, std::string &errorMessage) {
	std::size_t start = 0;
	std::size_t end = 0;

	static_cast<void>(targetWindow);
	if (!lspCompletionEditRange(editor, result, item, start, end, errorMessage)) return false;
	if (insertText.size() > static_cast<std::size_t>(std::numeric_limits<uint>::max())) {
		errorMessage = "LSP completion edit text is too large.";
		return false;
	}
	if (!editor.replaceRangeAndSelect(static_cast<uint>(start), static_cast<uint>(end), insertText.data(), static_cast<uint>(insertText.size()))) {
		errorMessage = "LSP completion editor replace failed.";
		return false;
	}
	errorMessage.clear();
	return true;
}

bool writeMacroSnippetRequest(MRFileEditor &editor, const mr::services::MRServiceCompletionResult &result, const mr::services::MRServiceCompletionItem &item, const std::string &insertText, std::size_t replaceStart, std::size_t replaceEnd, std::string &errorMessage) {
	std::size_t requestOffset = 0;
	const std::string currentText = editor.snapshotText();

	if (!result.hasRequestPosition || !lspTextOffsetForPosition(currentText, result.requestPosition, requestOffset)) {
		errorMessage = "LSP completion request position is outside the document.";
		return false;
	}
	if (replaceStart > static_cast<std::size_t>(std::numeric_limits<int>::max()) || replaceEnd > static_cast<std::size_t>(std::numeric_limits<int>::max()) || requestOffset > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		errorMessage = "LSP snippet request range is too large.";
		return false;
	}
	{
		std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
		MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
		VirtualMachine::Value root = runtimeKv.ensureRoot("MACROSNIPPETS");
		VirtualMachine::Value request = runtimeKv.replaceChild(root, "request");
		VirtualMachine::Value sidekick = runtimeKv.replaceChild(root, "sidekick");

		mrSnippetWriteInt(runtimeKv, request, "available", 1);
		mrSnippetWriteString(runtimeKv, request, "label", item.label);
		mrSnippetWriteString(runtimeKv, request, "detail", item.detail);
		mrSnippetWriteString(runtimeKv, request, "documentation", item.documentation);
		mrSnippetWriteString(runtimeKv, request, "insertText", item.insertText);
		mrSnippetWriteString(runtimeKv, request, "textEditNewText", item.textEditNewText);
		mrSnippetWriteString(runtimeKv, request, "replacementText", insertText);
		mrSnippetWriteString(runtimeKv, request, "token", lspIdentifierTextAroundOffset(editor, requestOffset));
		mrSnippetWriteString(runtimeKv, request, "rawLspCompletionItemJson", item.rawLspCompletionItemJson);
		mrSnippetWriteInt(runtimeKv, request, "hasInsertTextFormat", item.hasInsertTextFormat ? 1 : 0);
		mrSnippetWriteInt(runtimeKv, request, "insertTextFormat", item.hasInsertTextFormat ? item.insertTextFormat : 1);
		mrSnippetWriteInt(runtimeKv, request, "hasTextEdit", item.hasTextEdit ? 1 : 0);
		mrSnippetWriteInt(runtimeKv, request, "replaceStart", static_cast<int>(replaceStart));
		mrSnippetWriteInt(runtimeKv, request, "replaceEnd", static_cast<int>(replaceEnd));
		mrSnippetWriteInt(runtimeKv, request, "offset", static_cast<int>(requestOffset));
		mrSnippetWriteInt(runtimeKv, request, "line", result.requestPosition.line);
		mrSnippetWriteInt(runtimeKv, request, "character", result.requestPosition.character);
		mrSnippetWriteInt(runtimeKv, sidekick, "available", 0);
		mrSnippetWriteString(runtimeKv, sidekick, "text", std::string());
		mrSnippetWriteInt(runtimeKv, sidekick, "placeholderCount", 0);
	}
	errorMessage.clear();
	return true;
}

bool readMacroSnippetSidekick(std::string &text, std::vector<MRSidekickSpan> &placeholders, std::string &errorMessage) {
	int available = 0;
	int placeholderCount = 0;
	VirtualMachine::Value root;

	text.clear();
	placeholders.clear();
	{
		std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
		MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();

		if (!runtimeKv.findRoot("MACROSNIPPETS", root)) {
			errorMessage = "snippet middleware did not publish a sidekick.";
			return false;
		}
		VirtualMachine::Value sidekick;
		if (!runtimeKv.findChild(root, "sidekick", sidekick)) {
			errorMessage = "snippet middleware did not publish a sidekick.";
			return false;
		}
		if (!mrSnippetReadInt(runtimeKv, sidekick, "available", available) || available == 0) {
			errorMessage = "snippet middleware did not provide an available sidekick.";
			return false;
		}
		if (!mrSnippetReadString(runtimeKv, sidekick, "text", text)) {
			errorMessage = "snippet middleware did not provide sidekick text.";
			return false;
		}
		static_cast<void>(mrSnippetReadInt(runtimeKv, sidekick, "placeholderCount", placeholderCount));
		for (int index = 1; index <= placeholderCount; ++index) {
			int start = 0;
			int end = 0;
			const std::string suffix = std::to_string(index);

			if (!mrSnippetReadInt(runtimeKv, sidekick, "placeholderStart" + suffix, start)) continue;
			if (!mrSnippetReadInt(runtimeKv, sidekick, "placeholderEnd" + suffix, end)) continue;
			if (start < 0 || end < start || static_cast<std::size_t>(end) > text.size()) continue;
			placeholders.push_back(MRSidekickSpan{static_cast<std::size_t>(start), static_cast<std::size_t>(end)});
		}
	}
	errorMessage.clear();
	return true;
}

bool runLspSnippetMiddleware(MRFileEditor &editor, const mr::services::MRServiceCompletionResult &result, const mr::services::MRServiceCompletionItem &item, const std::string &insertText, std::size_t replaceStart, std::size_t replaceEnd, std::string &sidekickText, std::vector<MRSidekickSpan> &placeholders, std::string &errorMessage) {
	if (result.lspMiddlewarePath.empty()) {
		errorMessage = "no LSP snippet middleware configured.";
		return false;
	}
	if (!writeMacroSnippetRequest(editor, result, item, insertText, replaceStart, replaceEnd, errorMessage)) return false;
	if (!runMacroFileByPathOnUiThread(result.lspMiddlewarePath.c_str(), &errorMessage, false)) {
		if (errorMessage.empty()) errorMessage = "snippet middleware execution failed.";
		return false;
	}
	return readMacroSnippetSidekick(sidekickText, placeholders, errorMessage);
}

bool lspSnippetSidekickAnchor(MRFileEditor &editor, const mr::services::MRServiceCompletionResult &result, int &viewColumn, int &viewRow) {
	std::size_t offset = 0;
	const std::string currentText = editor.snapshotText();

	if (!result.hasRequestPosition || !lspTextOffsetForPosition(currentText, result.requestPosition, offset)) return false;
	const std::size_t lineStart = editor.lineStartOffset(offset);
	const std::size_t lineIndex = editor.lineIndexOfOffset(offset);
	const std::size_t visibleLine = editor.visibleLineForDocumentLine(lineIndex);
	viewColumn = editor.charColumn(lineStart, offset) - editor.delta.x + 1;
	viewRow = static_cast<int>(visibleLine) - editor.delta.y + 1;
	return true;
}

bool lspCompletionShouldShowChoiceDialog(std::size_t itemCount) noexcept {
	return itemCount > 1;
}

bool showLspCompletionDialog(const mr::services::MRServiceCompletionResult &result) {
	MRDialogFoundation *dialog = nullptr;
	TScrollBar *scrollBar = nullptr;
	LspCompletionListView *listView = nullptr;
	MREditWindow *targetWindow = nullptr;
	MRFileEditor *targetEditor = nullptr;
	ushort dialogResult = cmCancel;
	std::size_t selectedIndex = 0;
	std::size_t replaceStart = 0;
	std::size_t replaceEnd = 0;
	std::string insertText;
	std::string sidekickText;
	std::string errorMessage;
	std::vector<MRSidekickSpan> placeholders;
	mr::services::MRServiceCompletionItem selectedItem;
	bool snippetCommitted = false;
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
		std::ostringstream line;

		line << "LSP completion: no items";
		if (result.hasRequestPosition) line << " at " << (result.requestPosition.line + 1) << ":" << (result.requestPosition.character + 1);
		if (result.hasTriggerCharacter) line << " trigger " << lspDiagnosticChar(result.triggerCharacter.empty() ? '\0' : result.triggerCharacter[0]);
		line << ".";
		postLspWarning(line.str());
		return true;
	}

	if (!lspCompletionShouldShowChoiceDialog(result.items.size())) {
		selectedItem = result.items.front();
		dialogResult = cmOK;
	} else {
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

			selectedItem = item;
		}
		TObject::destroy(dialog);
	}

	if (dialogResult != cmOK || selectedItem.label.empty()) return true;
	{
		mr::services::MRServiceCompletionItem resolvedItem;

		if (g_lspAppService.resolveCompletionItem(selectedItem, resolvedItem, errorMessage)) {
			selectedItem = resolvedItem;
		} else if (!errorMessage.empty()) {
			postLspWarning("LSP completion resolve failed: " + errorMessage);
			errorMessage.clear();
		}
	}
	targetWindow = findLspCompletionTargetWindow(result);
	targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	if (targetEditor == nullptr) {
		postLspWarning("LSP completion insert failed: target editor is not open.");
		return true;
	}
	insertText = lspCompletionRawReplacementTextForItem(selectedItem);
	if (insertText.empty()) return true;
	static_cast<void>(activateLspTargetWindow(targetWindow));
	if (!lspCompletionEditRange(*targetEditor, result, selectedItem, replaceStart, replaceEnd, errorMessage)) {
		postLspWarning("LSP completion insert failed: " + errorMessage);
		return true;
	}
	if (selectedItem.hasInsertTextFormat && selectedItem.insertTextFormat == 2) {
		int anchorViewColumn = targetEditor->currentViewColumn();
		int anchorViewRow = targetEditor->currentViewRow();

		if (!runLspSnippetMiddleware(*targetEditor, result, selectedItem, insertText, replaceStart, replaceEnd, sidekickText, placeholders, errorMessage)) {
			postLspWarning("LSP snippet middleware failed: " + errorMessage);
			return true;
		}
		static_cast<void>(lspSnippetSidekickAnchor(*targetEditor, result, anchorViewColumn, anchorViewRow));
		if (!mrOpenSnippetSidekickAt(targetWindow, sidekickText, "Snippet SideKick", replaceStart, replaceEnd, placeholders, anchorViewColumn, anchorViewRow, snippetCommitted)) {
			postLspWarning("LSP snippet sidekick failed.");
			return true;
		}
		if (snippetCommitted) forgetLspAutoHoverForWindow(targetWindow, true);
		postLspInfo(snippetCommitted ? "LSP snippet inserted." : "LSP snippet cancelled.");
		return true;
	}
	if (!applyLspCompletionItem(targetWindow, *targetEditor, result, selectedItem, insertText, errorMessage)) {
		postLspWarning("LSP completion insert failed: " + errorMessage);
		return true;
	}
	forgetLspAutoHoverForWindow(targetWindow, true);
	postLspInfo("LSP completion inserted: " + firstDisplayLine(insertText, 80));
	return true;
}

MREditWindow *findLspCodeActionTargetWindow(const mr::services::MRServiceCodeActionResult &result) {
	MREditWindow *window = nullptr;

	if (result.header.identity.bufferId != 0) {
		window = findEditWindowByBufferId(result.header.identity.bufferId);
		if (window != nullptr) return window;
	}
	if (!result.header.identity.path.empty()) return findOpenLspTargetWindow(result.header.identity.path);
	return nullptr;
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
	std::string serverCandidates;
	bool configured = false;
	short width = 96;
	short height = 0;
	short buttonY = 0;

	if (TProgram::deskTop == nullptr) return false;
	if (currentEditor != nullptr) configured = buildLspServerProfileFromEditor(*currentEditor, profile, configurationSource, profileError);
	else if (mr::services::buildLspServerProfileFromEnvironment(profile)) {
		configured = true;
		configurationSource = "MR_LSP_SERVER";
	} else
		profileError = "No current editor and MR_LSP_SERVER is empty.";

	lines.push_back(std::string("Support: ") + (configuredLanguageServerSpawnDaemon() ? "enabled" : "disabled"));
	lines.push_back(std::string("Runtime: ") + (g_lspAppService.runtimeActive() ? "active" : "inactive"));
	if (!configuredLanguageServerSpawnDaemon()) {
		lines.push_back("Server configured: disabled by LSP support setup");
	}
	if (currentEditor != nullptr) {
		serverCandidates = mr::services::lspServerExecutableCandidatesForLanguage(currentEditor->syntaxLanguage());
		lines.push_back(std::string("Editor language: ") + currentEditor->syntaxLanguageName());
		if (!serverCandidates.empty()) lines.push_back("Built-in candidates: " + serverCandidates);
		else
			lines.push_back("Built-in candidates: none");
	} else
		lines.push_back("Editor language: none");
	lines.push_back(std::string("Server configured: ") + (configured ? "yes" : "no"));
	if (configured) {
		lines.push_back("Configuration source: " + configurationSource);
		if (!profile.profileName.empty()) lines.push_back("Profile: " + profile.profileName);
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
	if (!g_lspLastServerProfileName.empty()) lines.push_back("Last profile: " + g_lspLastServerProfileName);
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
	{
		const mr::services::MRServiceResultCounts counts = results.resultCounts();

		line << "Results: diagnostics " << counts.diagnostics << ", locations " << results.locationResults().size() << ", hovers " << counts.hovers << ", completions " << counts.completions << ", code actions " << counts.codeActions << ", document symbols " << counts.documentSymbols << ", workspace symbols " << counts.workspaceSymbols << ", signatures " << counts.signatureHelps << ", renames " << counts.renames;
	}
	lines.push_back(line.str());
	line.str(std::string());
	line.clear();
	line << "Reported: diagnostics " << g_lspReportedDiagnosticCount << ", locations " << g_lspReportedLocationCount << ", hovers " << g_lspReportedHoverCount << ", completions " << g_lspReportedCompletionCount << ", code actions " << g_lspReportedCodeActionCount << ", document highlights " << g_lspReportedDocumentHighlightCount << ", document symbols " << g_lspReportedDocumentSymbolCount << ", signatures " << g_lspReportedSignatureHelpCount;
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

void appendLspDocumentSymbolRows(const mr::services::MRServiceDocumentSymbolsResult &result, std::vector<LspResultDialogRow> &rows) {
	std::ostringstream rowText;

	if (result.header.state != mr::services::MRServiceResultState::Current) return;
	for (const mr::services::MRServiceDocumentSymbol &symbol : result.symbols) {
		LspResultDialogRow row;
		const int depth = std::max(0, std::min(symbol.depth, 8));
		const char *kind = result.header.kind == mr::services::MRServiceResultKind::WorkspaceSymbols ? "WSYM" : "SYM";

		if (symbol.target.path.empty()) continue;
		rowText.str(std::string());
		rowText.clear();
		rowText << kind << " [" << lspResultStateText(result.header.state) << "] ";
		rowText << (symbol.target.range.start.line + 1) << ":" << (symbol.target.range.start.character + 1) << " ";
		rowText << std::string(static_cast<std::size_t>(depth * 2), ' ');
		rowText << firstDisplayLine(symbol.name, 50);
		if (!symbol.detail.empty()) rowText << " - " << firstDisplayLine(symbol.detail, 36);
		rowText << " " << (!symbol.target.path.empty() ? symbol.target.path : symbol.target.uri);
		row.text = rowText.str();
		row.action = LspResultDialogAction::NavigateLocation;
		row.location = symbol.target;
		rows.push_back(row);
	}
}

bool showLspWorkspaceSymbolsPicker(const mr::services::MRServiceDocumentSymbolsResult &result);

bool showLspDocumentSymbolsDialog(const mr::services::MRServiceDocumentSymbolsResult &result) {
	MRDialogFoundation *dialog = nullptr;
	TScrollBar *scrollBar = nullptr;
	LspResultsListView *listView = nullptr;
	ushort dialogResult = cmCancel;
	std::size_t selectedIndex = 0;
	std::vector<LspResultDialogRow> rows;
	std::string navigationError;
	const short width = 100;
	short visibleRows = 0;
	short height = 0;
	short buttonY = 0;

	if (TProgram::deskTop == nullptr) return false;
	if (result.header.state != mr::services::MRServiceResultState::Current) {
		const char *kindText = result.header.kind == mr::services::MRServiceResultKind::WorkspaceSymbols ? "workspace symbols" : "document symbols";

		if (!result.header.errorMessage.empty()) postLspWarning(std::string("LSP ") + kindText + " unavailable: " + result.header.errorMessage);
		else
			postLspWarning(std::string("LSP ") + kindText + " unavailable.");
		return true;
	}
	if (result.header.kind == mr::services::MRServiceResultKind::WorkspaceSymbols) return showLspWorkspaceSymbolsPicker(result);
	appendLspDocumentSymbolRows(result, rows);
	if (rows.empty()) {
		postLspWarning("LSP document symbols: no symbols.");
		return true;
	}

	visibleRows = static_cast<short>(std::max<int>(5, std::min<int>(static_cast<int>(rows.size()), 16)));
	height = static_cast<short>(visibleRows + 6);
	buttonY = static_cast<short>(height - 3);
	dialog = mr::dialogs::createScrollableDialog("LSP SYMBOLS", width, height);
	dialog->insert(new TStaticText(TRect(2, 1, width - 2, 2), "Select symbol:"));
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

		validation.valid = listView != nullptr && listView->selectedIndex(selected) && selected < rows.size() && rows[selected].action == LspResultDialogAction::NavigateLocation;
		if (!validation.valid) validation.warningText = "Select a symbol.";
		return validation;
	});
	dialog->finalizeLayout();
	dialogResult = TProgram::deskTop->execView(dialog);
	if (dialogResult == cmOK && listView != nullptr && listView->selectedIndex(selectedIndex) && selectedIndex < rows.size()) {
		if (!navigateToLspLocation(rows[selectedIndex].location, navigationError)) postLspWarning("LSP symbol navigation failed: " + navigationError);
	}
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
	std::string codeActionError;
	const mr::services::MRServiceResultStore &results = g_lspAppService.results();
	const short width = 108;
	short visibleRows = 0;
	short height = 0;
	short buttonY = 0;

	if (TProgram::deskTop == nullptr) return false;

	for (const mr::services::MRServiceDiagnosticResult &result : results.diagnosticResults()) {
		if (result.header.state != mr::services::MRServiceResultState::Current) continue;
		if (result.header.identity.path.empty()) continue;
		for (const mr::services::MRServiceDiagnosticEntry &diagnostic : result.diagnostics) {
			LspResultDialogRow row;

			rowText.str(std::string());
			rowText.clear();
			rowText << "DIAG " << lspDiagnosticSeverityText(diagnostic.severity) << " [" << lspResultStateText(result.header.state) << "] ";
			rowText << (diagnostic.reportedRange.start.line + 1) << ":" << (diagnostic.reportedRange.start.character + 1) << " ";
			if (!result.header.identity.path.empty()) rowText << result.header.identity.path << " - ";
			rowText << firstDisplayLine(diagnostic.message, 80);
			row.text = rowText.str();
			row.action = LspResultDialogAction::NavigateLocation;
			row.location.path = result.header.identity.path;
			row.location.uri = result.header.identity.uri;
			row.location.range = diagnostic.navigationRange;
			rows.push_back(row);
		}
	}

	for (const mr::services::MRServiceLocationResult &result : results.locationResults()) {
		const char *kind = result.header.kind == mr::services::MRServiceResultKind::References ? "REF" : "DEF";

		if (result.header.state != mr::services::MRServiceResultState::Current) continue;
		for (const mr::services::MRServiceLocationTarget &target : result.locations) {
			LspResultDialogRow row;

			if (target.path.empty()) continue;
			rowText.str(std::string());
			rowText.clear();
			rowText << kind << " [" << lspResultStateText(result.header.state) << "] ";
			rowText << (target.range.start.line + 1) << ":" << (target.range.start.character + 1) << " ";
			rowText << (!target.path.empty() ? target.path : target.uri);
			row.text = rowText.str();
			row.action = LspResultDialogAction::NavigateLocation;
			row.location = target;
			rows.push_back(row);
		}
	}

	for (const mr::services::MRServiceDocumentSymbolsResult &result : results.documentSymbolResults()) {
		appendLspDocumentSymbolRows(result, rows);
	}

	for (const mr::services::MRServiceCodeActionResult &result : results.codeActionResults()) {
		if (result.header.state != mr::services::MRServiceResultState::Current) continue;
		for (const mr::services::MRServiceCodeActionItem &item : result.items) {
			LspResultDialogRow row;
			const mr::services::MRServiceTextEdit *firstEdit = nullptr;

			if (!lspCodeActionEditTargetsCurrentDocument(item, result.header.identity)) continue;
			firstEdit = &item.edits[0];
			rowText.str(std::string());
			rowText.clear();
			rowText << "ACTION [" << lspResultStateText(result.header.state) << "] ";
			rowText << (firstEdit->range.start.line + 1) << ":" << (firstEdit->range.start.character + 1) << " ";
			if (!result.header.identity.path.empty()) rowText << result.header.identity.path << " - ";
			rowText << firstDisplayLine(item.title, 90);
			if (!item.kind.empty()) rowText << " - " << item.kind;
			row.text = rowText.str();
			row.action = LspResultDialogAction::ApplyCodeAction;
			row.codeActionResult = &result;
			row.codeActionItem = &item;
			rows.push_back(row);
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
		if (!validation.valid) validation.warningText = "Select a navigable, hover or action result.";
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
	if (selectedRow.action == LspResultDialogAction::ShowHover) return showLspHoverSidekick(selectedRow.hover);
	if (selectedRow.action == LspResultDialogAction::ApplyCodeAction && selectedRow.codeActionResult != nullptr && selectedRow.codeActionItem != nullptr) {
		MREditWindow *targetWindow = findLspCodeActionTargetWindow(*selectedRow.codeActionResult);
		MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;

		if (targetEditor == nullptr) {
			postLspWarning("LSP code action apply failed: target editor is not open.");
			return true;
		}
		static_cast<void>(activateLspTargetWindow(targetWindow));
		if (!applyLspCodeActionEdits(*targetEditor, *selectedRow.codeActionResult, *selectedRow.codeActionItem, codeActionError)) {
			postLspWarning("LSP code action apply failed: " + codeActionError);
			return true;
		}
		postLspInfo("LSP code action applied: " + firstDisplayLine(selectedRow.codeActionItem->title, 80));
		return true;
	}
	return true;
}

void reportNewLspDiagnostics(const std::vector<mr::services::MRServiceDiagnosticResult> &diagnostics) {
	std::ostringstream signature;

	signature << diagnostics.size() << '\n';
	for (const mr::services::MRServiceDiagnosticResult &result : diagnostics) {
		signature << static_cast<int>(result.header.state) << '|';
		signature << result.header.identity.bufferId << '|';
		signature << result.header.identity.documentId << '|';
		signature << result.header.identity.documentVersion << '|';
		signature << result.header.identity.path << '|';
		signature << result.diagnostics.size() << '\n';
		for (const mr::services::MRServiceDiagnosticEntry &diagnostic : result.diagnostics) {
			signature << diagnostic.severity << '|';
			signature << diagnostic.reportedRange.start.line << ':' << diagnostic.reportedRange.start.character << '-';
			signature << diagnostic.reportedRange.end.line << ':' << diagnostic.reportedRange.end.character << '|';
			signature << diagnostic.message << '\n';
		}
	}
	const std::string nextSignature = signature.str();

	if (g_lspReportedDiagnosticSignature == nextSignature) return;
	g_lspReportedDiagnosticSignature = nextSignature;
	g_lspReportedDiagnosticCount = diagnostics.size();
	for (const mr::services::MRServiceDiagnosticResult &result : diagnostics) {
		std::ostringstream line;

		applyLspDiagnosticInformationRanges(result.header.identity);
		g_lspLastRequestState = "diagnostics received";
		line << "LSP diagnostics: " << result.diagnostics.size();
		if (!result.header.identity.path.empty()) line << " " << result.header.identity.path;
		if (!result.diagnostics.empty()) {
			const mr::services::MRServiceDiagnosticEntry &diagnostic = result.diagnostics[0];
			line << ":" << (diagnostic.reportedRange.start.line + 1) << ":" << (diagnostic.reportedRange.start.character + 1);
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
		std::string text = firstDisplayLine(lspHoverPlainText(result.hover.value), 120);
		const bool retiredAutoHover = lspHoverResultBelongsToRetiredAutoHover(result);
		const bool autoHover = !retiredAutoHover && lspHoverResultMatchesPendingAutoHover(result);
		const bool currentAutoHover = autoHover && lspAutoHoverResultStillAtRequestAnchor(result);

		++g_lspReportedHoverCount;
		if (retiredAutoHover) continue;
		if (autoHover && !currentAutoHover) {
			mrLogMessage("LSP auto hover ignored: request anchor moved before result arrived.");
			continue;
		}
		g_lspLastRequestState = "hover received";
		if (text.empty()) text = "empty hover";
		if (autoHover) {
			if (result.header.state == mr::services::MRServiceResultState::Current) mrLogMessage("LSP auto hover received: " + text);
			else if (!result.header.errorMessage.empty())
				mrLogMessage("LSP auto hover unavailable: " + result.header.errorMessage);
			else
				mrLogMessage("LSP auto hover unavailable.");
		}
		if (!autoHover) postLspInfo("LSP hover: " + text);
		if (result.header.state == mr::services::MRServiceResultState::Current) {
			if (!lspModalViewActive() && (!autoHover || !result.hover.value.empty())) static_cast<void>(showLspHoverSidekick(result));
		} else if (!result.header.errorMessage.empty()) {
			if (!autoHover) postLspWarning("LSP hover unavailable: " + result.header.errorMessage);
		} else {
			if (!autoHover) postLspWarning("LSP hover unavailable.");
		}
	}
}

void reportLspCompletionResult(const mr::services::MRServiceCompletionResult &result) {
	std::ostringstream line;

	g_lspLastRequestState = "completion received";
	line << "LSP completion: " << result.items.size();
	if (!result.header.identity.path.empty()) line << " " << result.header.identity.path;
	if (result.hasRequestPosition) line << ":" << (result.requestPosition.line + 1) << ":" << (result.requestPosition.character + 1);
	postLspInfo(line.str());
	static_cast<void>(showLspCompletionDialog(result));
}

void reportNewLspCompletions(const std::vector<mr::services::MRServiceCompletionResult> &completions) {
	while (g_lspReportedCompletionCount < completions.size()) {
		const mr::services::MRServiceCompletionResult result = completions[g_lspReportedCompletionCount];

		++g_lspReportedCompletionCount;
		reportLspCompletionResult(result);
	}
}

void reportNewLspCodeActions(const std::vector<mr::services::MRServiceCodeActionResult> &codeActions) {
	while (g_lspReportedCodeActionCount < codeActions.size()) {
		const mr::services::MRServiceCodeActionResult result = codeActions[g_lspReportedCodeActionCount];

		++g_lspReportedCodeActionCount;
		applyLspDiagnosticInformationRanges(result.header.identity);
	}
}

void reportNewLspDocumentHighlights(const std::vector<mr::services::MRServiceDocumentHighlightResult> &documentHighlights) {
	while (g_lspReportedDocumentHighlightCount < documentHighlights.size()) {
		const mr::services::MRServiceDocumentHighlightResult result = documentHighlights[g_lspReportedDocumentHighlightCount];
		std::ostringstream line;

		++g_lspReportedDocumentHighlightCount;
		g_lspLastRequestState = "document highlight received";
		if (result.header.state == mr::services::MRServiceResultState::Current) {
			applyLspDocumentHighlightRanges(result);
			line << "LSP document highlight: " << result.highlights.size();
			if (!result.header.identity.path.empty()) line << " " << result.header.identity.path;
			postLspInfo(line.str());
		} else if (!result.header.errorMessage.empty()) {
			postLspWarning("LSP document highlight unavailable: " + result.header.errorMessage);
		} else {
			postLspWarning("LSP document highlight unavailable.");
		}
	}
}

void reportNewLspDocumentSymbols(const std::vector<mr::services::MRServiceDocumentSymbolsResult> &documentSymbols) {
	while (g_lspReportedDocumentSymbolCount < documentSymbols.size()) {
		const mr::services::MRServiceDocumentSymbolsResult result = documentSymbols[g_lspReportedDocumentSymbolCount];
		std::ostringstream line;

		++g_lspReportedDocumentSymbolCount;
		if (result.header.kind == mr::services::MRServiceResultKind::WorkspaceSymbols) {
			g_lspLastRequestState = "workspace symbols received";
			line << "LSP workspace symbols: " << result.symbols.size();
		} else {
			g_lspLastRequestState = "document symbols received";
			line << "LSP document symbols: " << result.symbols.size();
		}
		if (!result.header.identity.path.empty()) line << " " << result.header.identity.path;
		postLspInfo(line.str());
		if (result.header.state == mr::services::MRServiceResultState::Current)
			static_cast<void>(showLspDocumentSymbolsDialog(result));
		else if (!result.header.errorMessage.empty())
			postLspWarning("LSP document symbols unavailable: " + result.header.errorMessage);
		else
			postLspWarning("LSP document symbols unavailable.");
	}
}

void reportNewLspSignatureHelps(const std::vector<mr::services::MRServiceSignatureHelpResult> &signatureHelps) {
	while (g_lspReportedSignatureHelpCount < signatureHelps.size()) {
		const mr::services::MRServiceSignatureHelpResult result = signatureHelps[g_lspReportedSignatureHelpCount];
		std::ostringstream line;

		++g_lspReportedSignatureHelpCount;
		if (g_lspSignatureHelp.active && !result.header.requestId.empty() && result.header.requestId == g_lspSignatureHelp.requestId) g_lspSignatureHelp.requestPending = false;
		g_lspLastRequestState = "signature help received";
		line << "LSP signature help: " << result.signatures.size();
		if (!result.header.identity.path.empty()) line << " " << result.header.identity.path;
		if (result.header.state == mr::services::MRServiceResultState::Current && result.activeSignature >= 0 && static_cast<std::size_t>(result.activeSignature) < result.signatures.size())
			line << " - " << firstDisplayLine(result.signatures[static_cast<std::size_t>(result.activeSignature)].label, 100);
		postLspInfo(line.str());
		if (result.header.state == mr::services::MRServiceResultState::Current)
			static_cast<void>(showLspSignatureHelpSidekick(result));
		else if (!result.header.errorMessage.empty())
			postLspWarning("LSP signature help unavailable: " + result.header.errorMessage);
		else
			postLspWarning("LSP signature help unavailable.");
	}
}

bool lspCompletionRequestIdKnown(const std::vector<std::string> &requestIds, const std::string &requestId) {
	for (const std::string &knownRequestId : requestIds)
		if (knownRequestId == requestId) return true;
	return false;
}

bool lspServerProfilesEqual(const mr::services::MRLspServerProfile &left, const mr::services::MRLspServerProfile &right) {
	return left.profileName == right.profileName && left.executablePath == right.executablePath && left.arguments == right.arguments && left.workingDirectory == right.workingDirectory;
}

bool syncLoadedWorkspaceDocumentsForLspRename(
	const mr::services::MRLspServerProfile &profile,
	const mr::services::MRWorkspaceServiceSnapshot &workspace,
	const mr::services::MRWorkspaceDocumentSnapshot &activeDocument,
	MRFileEditor &activeEditor,
	std::string &errorMessage) {
	for (const mr::services::MRWorkspaceDocumentSnapshot &document : workspace.documents) {
		MREditWindow *window = nullptr;
		MRFileEditor *editor = nullptr;
		mr::services::MRLspServerProfile documentProfile;
		std::string configurationSource;

		if (document.path == activeDocument.path && document.documentId == activeDocument.documentId) continue;
		window = findEditWindowByBufferId(document.bufferId);
		editor = window != nullptr ? window->getEditor() : nullptr;
		if (editor == nullptr) continue;
		if (!buildLspServerProfileFromEditor(*editor, documentProfile, configurationSource, errorMessage)) return false;
		if (!lspServerProfilesEqual(profile, documentProfile)) continue;
		if (!g_lspAppService.syncEditorDocument(profile, workspace, document, *editor, errorMessage)) return false;
	}
	return g_lspAppService.syncEditorDocument(profile, workspace, activeDocument, activeEditor, errorMessage);
}

bool requestLspRenameCommand(MREditWindow *win = currentEditorCommandWindow(), const LspEditorRequestTarget *requestTarget = nullptr) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	mr::services::MRLspServerProfile profile;
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::services::MRWorkspaceServiceSnapshot workspace;
	LspEditorRequestTarget cursorTarget;
	const LspEditorRequestTarget *target = requestTarget;
	std::string configurationSource;
	std::string errorMessage;
	std::string oldName;
	std::string newName;
	std::vector<std::string> knownRequestIds;
	const std::vector<mr::services::MRServiceRenameResult> &initialRenames = g_lspAppService.results().renameResults();

	for (const mr::services::MRServiceRenameResult &rename : initialRenames)
		knownRequestIds.push_back(rename.header.requestId);
	if (!configuredLanguageServerSpawnDaemon()) {
		postLspWarning("LSP support is disabled.");
		return true;
	}
	if (!configuredLanguageServerChannelSettings().rename) {
		postLspWarning("LSP rename channel is disabled.");
		return true;
	}
	if (editor == nullptr || win == nullptr) {
		postLspWarning("LSP rename requires an editor window.");
		return true;
	}
	if (win->isReadOnly()) {
		postLspWarning(kWindowReadOnlyMessage);
		return true;
	}
	if (!buildLspDocumentSnapshotForWindow(win, document, true)) return true;
	if (!buildLspServerProfileFromEditor(*editor, profile, configurationSource, errorMessage)) {
		postLspWarning(errorMessage.empty() ? "LSP server not configured." : errorMessage);
		return true;
	}
	if (target == nullptr) {
		lspRequestTargetFromCursor(*editor, cursorTarget);
		target = &cursorTarget;
	}
	oldName = lspIdentifierTextAroundOffset(*editor, target->offset);
	if (oldName.empty()) {
		postLspWarning("LSP rename requires an identifier.");
		return true;
	}
	newName = oldName;
	if (!promptTextValue("LSP RENAME", "New name:", oldName, newName)) return true;
	if (newName.empty()) {
		postLspWarning("LSP rename new name is empty.");
		return true;
	}
	if (newName == oldName) {
		postLspInfo("LSP rename unchanged.");
		return true;
	}
	workspace = g_lspAppService.buildCurrentWorkspaceSnapshot();
	if (!syncLoadedWorkspaceDocumentsForLspRename(profile, workspace, document, *editor, errorMessage)) {
		g_lspLastRequestState = "failed";
		g_lspLastError = errorMessage;
		++g_lspRequestFailureCount;
		postLspError("LSP rename workspace sync failed: " + errorMessage);
		return true;
	}
	g_lspLastRequestLabel = "LSP rename";
	g_lspLastRequestPath = document.path;
	g_lspLastRequestPosition = std::to_string(target->position.line + 1) + ":" + std::to_string(target->position.character + 1);
	g_lspLastRequestState = "preparing";
	g_lspLastError.clear();
	if (!g_lspAppService.requestRename(profile, workspace, document, *editor, target->position, newName, errorMessage)) {
		g_lspLastRequestState = "failed";
		g_lspLastError = errorMessage;
		++g_lspRequestFailureCount;
		postLspError("LSP rename failed: " + errorMessage);
		return true;
	}
	++g_lspRequestCount;
	g_lspLastRequestState = "requested";
	for (int i = 0; i < 100; ++i) {
		if (!g_lspAppService.poll(errorMessage)) {
			++g_lspPollFailureCount;
			g_lspLastRequestState = "poll failed";
			g_lspLastPollError = errorMessage;
			g_lspLastError = errorMessage;
			postLspError("LSP poll failed: " + errorMessage);
			g_lspAppService.close();
			return true;
		}
		const std::vector<mr::services::MRServiceRenameResult> &renames = g_lspAppService.results().renameResults();
		for (const mr::services::MRServiceRenameResult &rename : renames) {
			if (lspCompletionRequestIdKnown(knownRequestIds, rename.header.requestId)) continue;
			knownRequestIds.push_back(rename.header.requestId);
			if (!applyLspRenameEdits(rename, workspace, errorMessage)) {
				g_lspLastRequestState = "apply failed";
				g_lspLastError = errorMessage;
				postLspError("LSP rename apply failed: " + errorMessage);
				return true;
			}
			g_lspLastRequestState = "applied";
			postLspInfo("LSP rename applied.");
			return true;
		}
		::poll(nullptr, 0, 20);
	}
	postLspInfo("LSP rename requested.");
	return true;
}

bool requestLspCompletionCommand(MREditWindow *win = currentEditorCommandWindow(), const LspEditorRequestTarget *target = nullptr) {
	bool requestSent = false;
	std::string errorMessage;
	std::vector<std::string> knownRequestIds;
	const std::vector<mr::services::MRServiceCompletionResult> &initialCompletions = g_lspAppService.results().completionResults();

	for (const mr::services::MRServiceCompletionResult &completion : initialCompletions)
		knownRequestIds.push_back(completion.header.requestId);
	if (g_lspReportedCompletionCount < initialCompletions.size()) g_lspReportedCompletionCount = initialCompletions.size();
	for (int attempt = 0; attempt < 2; ++attempt) {
		bool retryAfterEmptyCompletion = false;
		const bool completionTriggerEnabled = attempt == 0;

		requestSent = false;
		if (!requestLspEditorCommandForWindow(win, mr::services::MRLspServiceCommandId::Complete, "LSP completion", true, &requestSent, target, completionTriggerEnabled)) return true;
		if (!requestSent) return true;
		for (int i = 0; i < 100; ++i) {
			if (!g_lspAppService.poll(errorMessage)) {
				++g_lspPollFailureCount;
				g_lspLastRequestState = "poll failed";
				g_lspLastPollError = errorMessage;
				g_lspLastError = errorMessage;
				postLspError("LSP poll failed: " + errorMessage);
				g_lspAppService.close();
				return true;
			}
			const std::vector<mr::services::MRServiceCompletionResult> &completions = g_lspAppService.results().completionResults();
			for (std::size_t index = 0; index < completions.size(); ++index) {
				if (lspCompletionRequestIdKnown(knownRequestIds, completions[index].header.requestId)) continue;
				knownRequestIds.push_back(completions[index].header.requestId);
				g_lspReportedCompletionCount = completions.size();
				mrLogMessage("LSP completion response: requestId=" + completions[index].header.requestId + " items=" + std::to_string(completions[index].items.size()) + " attempt=" + std::to_string(attempt + 1));
				if (completions[index].items.empty() && !completions[index].rawLspResponseJson.empty())
					mrLogMessage("LSP completion empty raw response: " + completions[index].rawLspResponseJson);
				if (attempt == 0 && completions[index].items.empty()) {
					mrLogMessage("LSP completion empty response ignored for one retry without trigger.");
					for (int waitIndex = 0; waitIndex < 15; ++waitIndex) {
						if (!g_lspAppService.poll(errorMessage)) {
							++g_lspPollFailureCount;
							g_lspLastRequestState = "poll failed";
							g_lspLastPollError = errorMessage;
							g_lspLastError = errorMessage;
							postLspError("LSP poll failed: " + errorMessage);
							g_lspAppService.close();
							return true;
						}
						::poll(nullptr, 0, 20);
					}
					retryAfterEmptyCompletion = true;
					break;
				}
				reportLspCompletionResult(completions[index]);
				return true;
			}
			if (retryAfterEmptyCompletion) break;
			::poll(nullptr, 0, 20);
		}
	}
	postLspInfo("LSP completion requested.");
	return true;
}

void reportNewLspResults() {
	const mr::services::MRServiceResultStore &results = g_lspAppService.results();
	const MRLanguageServerChannelSettings channels = configuredLanguageServerChannelSettings();

	if (channels.diagnostics) reportNewLspDiagnostics(results.diagnosticResults());
	if (channels.definition || channels.references) reportNewLspLocations(results.locationResults());
	if (channels.hover) reportNewLspHovers(results.hoverResults());
	if (channels.completion) reportNewLspCompletions(results.completionResults());
	if (channels.codeActions) reportNewLspCodeActions(results.codeActionResults());
	if (channels.documentHighlight) reportNewLspDocumentHighlights(results.documentHighlightResults());
	if (channels.documentSymbols || channels.workspaceSymbols) reportNewLspDocumentSymbols(results.documentSymbolResults());
	if (channels.signatureHelp) reportNewLspSignatureHelps(results.signatureHelpResults());
}

std::vector<LspMiniMenuEntry> buildLspContextMenuItems(MREditWindow *win, const LspEditorRequestTarget *target) {
	struct ContextCommand {
		const char *title;
		ushort command;
		bool enabled;
	};

	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::services::MRLspServerProfile profile;
	mr::services::MRLspDocumentServiceSnapshot snapshot;
	std::string configurationSource;
	std::string errorMessage;
	std::vector<LspMiniMenuEntry> entries;
	bool serverConfigured = false;

	if (editor == nullptr) return entries;
	entries.push_back(LspMiniMenuEntry{"Edit", 0, true});
	if (!configuredLanguageServerSpawnDaemon()) return entries;
	if (target != nullptr && buildLspDocumentSnapshotForWindow(win, document, false)) {
		const MRLanguageServerChannelSettings channels = configuredLanguageServerChannelSettings();
		serverConfigured = buildLspServerProfileFromEditor(*editor, profile, configurationSource, errorMessage);
		snapshot = g_lspAppService.currentDocumentServiceSnapshot(document);

		const ContextCommand serviceCommands[] = {
		    {"Definition", cmMrOtherLspDefinition, serverConfigured && snapshot.commands.requestDefinition && channels.definition},
		    {"References", cmMrOtherLspReferences, serverConfigured && snapshot.commands.requestReferences && channels.references},
		    {"Hover", cmMrOtherLspHover, serverConfigured && snapshot.commands.requestHover && channels.hover},
		    {"Complete", cmMrOtherLspComplete, serverConfigured && snapshot.commands.requestCompletion && channels.completion},
		    {"Highlight", cmMrOtherLspDocumentHighlight, serverConfigured && snapshot.commands.requestDocumentHighlight && channels.documentHighlight},
		    {"Signature", cmMrOtherLspSignatureHelp, serverConfigured && snapshot.commands.requestSignatureHelp && channels.signatureHelp},
		    {"Rename", cmMrOtherLspRename, serverConfigured && snapshot.commands.requestRename && channels.rename},
		    {"Code Actions", cmMrOtherLspCodeActions, serverConfigured && snapshot.commands.requestCodeActions && channels.codeActions},
		};

		for (const ContextCommand &entry : serviceCommands)
			if (entry.enabled) entries.push_back(LspMiniMenuEntry{entry.title, entry.command, false});
	}
	return entries;
}

bool requestLspCodeActionsAtPosition(MREditWindow *win, const LspEditorRequestTarget *target) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::services::MRLspPositionServiceSnapshot snapshot;
	mr::lsp::LspTextPosition lspPosition;
	std::string errorMessage;
	const std::size_t initialCodeActionCount = g_lspAppService.results().codeActionResults().size();

	if (!configuredLanguageServerSpawnDaemon()) {
		postLspWarning("LSP support is disabled.");
		return true;
	}
	if (!configuredLanguageServerChannelSettings().codeActions) {
		postLspWarning("LSP code actions channel is disabled.");
		return true;
	}
	if (editor == nullptr || !buildCurrentLspDocumentSnapshot(win, document)) return true;
	if (target != nullptr)
		lspPosition = target->position;
	else
		lspPosition = currentLspTextPosition(*editor);
	snapshot = g_lspAppService.currentDocumentPositionServiceSnapshot(document, serviceTextPositionFromLsp(lspPosition));
	if (snapshot.results.diagnostics.empty() || snapshot.results.diagnostics[0].diagnostics.empty()) {
		postLspWarning("LSP code actions: no diagnostic at cursor.");
		return true;
	}
	if (!g_lspAppService.requestCodeActionsForDiagnostic(snapshot.results.diagnostics[0], snapshot.results.diagnostics[0].diagnostics[0], errorMessage)) {
		postLspError("LSP code actions failed: " + errorMessage);
		return true;
	}
	postLspInfo("LSP code actions requested.");
	for (int i = 0; i < 100; ++i) {
		if (!g_lspAppService.poll(errorMessage)) {
			++g_lspPollFailureCount;
			g_lspLastRequestState = "poll failed";
			g_lspLastPollError = errorMessage;
			g_lspLastError = errorMessage;
			postLspError("LSP poll failed: " + errorMessage);
			g_lspAppService.close();
			return true;
		}
		if (g_lspAppService.results().codeActionResults().size() > initialCodeActionCount) return showLspResultsDialog();
		::poll(nullptr, 0, 20);
	}
	return true;
}

bool requestLspCodeActionsAtCurrentPosition() {
	return requestLspCodeActionsAtPosition(currentEditorCommandWindow(), nullptr);
}

int lspDisplayWidth(const std::string &value) noexcept {
	return std::max(0, strwidth(value.c_str()));
}

short lspMiniMenuWidthForValues(const std::vector<std::string> &values) noexcept {
	int width = 0;

	for (const std::string &value : values)
		width = std::max(width, lspDisplayWidth(value));
	width = std::max(width + 2, 12);
	return static_cast<short>(std::min(width, 40));
}

short lspMiniMenuWidthForEntries(const std::vector<LspMiniMenuEntry> &entries) noexcept {
	int width = 0;

	for (const LspMiniMenuEntry &entry : entries)
		width = std::max(width, lspDisplayWidth(entry.title) + (entry.editSubmenu ? 2 : 0));
	width = std::max(width + 2, 12);
	return static_cast<short>(std::min(width, 40));
}

std::string lspMiniMenuDisplayText(const LspMiniMenuEntry &entry, short menuWidth) {
	static const char kSubmenuArrow[] = "\342\226\266";
	const int interiorWidth = std::max(1, static_cast<int>(menuWidth) - 2);
	const int titleWidth = lspDisplayWidth(entry.title);
	const int arrowWidth = strwidth(kSubmenuArrow);
	std::string text = entry.title;

	if (!entry.editSubmenu) return text;
	if (titleWidth + arrowWidth < interiorWidth) text.append(static_cast<std::size_t>(interiorWidth - titleWidth - arrowWidth), ' ');
	else
		text.push_back(' ');
	text += kSubmenuArrow;
	return text;
}

bool lspMiniMenuBoundsFor(TGroup &owner, MRFileEditor *editor, TPoint where, short width, short requestedRows, TRect &bounds) {
	TPoint local = owner.makeLocal(where);
	const short safeWidth = std::max<short>(1, width);
	TRect constraint(0, 0, owner.size.x, owner.size.y);
	short safeRows = std::max<short>(1, requestedRows);
	int x;
	int y;

	if (editor != nullptr) {
		const TRect viewport = editor->visibleTextViewportBounds();
		const TPoint editorGlobal = editor->makeGlobal(TPoint(0, 0));
		const TPoint viewportTopLeft = owner.makeLocal(TPoint(editorGlobal.x + viewport.a.x, editorGlobal.y + viewport.a.y));
		const TPoint viewportBottomRight = owner.makeLocal(TPoint(editorGlobal.x + viewport.b.x, editorGlobal.y + viewport.b.y));

		constraint = TRect(
		    std::max<short>(0, viewportTopLeft.x),
		    std::max<short>(0, viewportTopLeft.y),
		    std::min<short>(owner.size.x, viewportBottomRight.x),
		    std::min<short>(owner.size.y, viewportBottomRight.y));
	}
	if (constraint.b.x - constraint.a.x < 12 || constraint.b.y - constraint.a.y < 2) return false;
	if (safeWidth > constraint.b.x - constraint.a.x) return false;
	safeRows = std::min<short>(safeRows, constraint.b.y - constraint.a.y);
	x = std::max<int>(constraint.a.x, std::min<int>(local.x, constraint.b.x - safeWidth));
	y = std::max<int>(constraint.a.y, std::min<int>(local.y, constraint.b.y - safeRows));
	bounds = TRect(static_cast<short>(x), static_cast<short>(y), static_cast<short>(x + safeWidth), static_cast<short>(y + safeRows));
	return true;
}

MRColumnListView *showLspMiniMenuList(TGroup &owner, MRFileEditor *editor, TPoint where, const std::vector<std::string> &values, short *menuWidth = nullptr, short forcedWidth = 0, bool contextMenuColors = true) {
	std::vector<MRColumnListView::Row> rows;
	MRColumnListView *listView = nullptr;
	const short width = forcedWidth > 0 ? forcedWidth : lspMiniMenuWidthForValues(values);
	const short height = static_cast<short>(std::min<std::size_t>(values.size(), 12));
	TRect bounds;

	if (values.empty()) return nullptr;
	if (!lspMiniMenuBoundsFor(owner, editor, where, width, height, bounds)) return nullptr;
	if (menuWidth != nullptr) *menuWidth = width;
	rows.reserve(values.size());
	for (const std::string &value : values)
		rows.push_back(MRColumnListView::Row{value});

	listView = new MRColumnListView(bounds, nullptr, nullptr, 0, 0, true);
	if (contextMenuColors) listView->setContextMenuColors(true);
	listView->setActivateOnSingleClick(true);
	owner.insert(listView);
	listView->setRows(rows, 0);
	listView->select();
	listView->drawView();
	return listView;
}

void destroyLspMiniMenuList(TGroup &owner, MRColumnListView *&listView) {
	if (listView == nullptr) return;
	if (owner.current == listView) owner.setCurrent(nullptr, TView::leaveSelect);
	owner.remove(listView);
	TObject::destroy(listView);
	listView = nullptr;
}

short lspMiniMenuClickedIndex(MRColumnListView &listView, TPoint where) {
	const TPoint local = listView.makeLocal(where);
	const short clicked = static_cast<short>(listView.topItem + local.y);

	if (local.y < 0 || local.y >= listView.size.y) return -1;
	if (clicked < 0 || clicked >= listView.range) return -1;
	listView.focusItemNum(clicked);
	return clicked;
}

bool showLspWorkspaceSymbolsPicker(const mr::services::MRServiceDocumentSymbolsResult &result) {
	struct WorkspaceSymbolDisplayEntry {
		int rank = 3;
		LspResultDialogRow resultRow;
		MRColumnListView::Row displayRow;
	};

	MRDialogFoundation *dialog = nullptr;
	TScrollBar *verticalScrollBar = nullptr;
	TScrollBar *horizontalScrollBar = nullptr;
	MRColumnListView *listView = nullptr;
	mr::services::MRWorkspaceServiceSnapshot workspace = g_lspAppService.buildCurrentWorkspaceSnapshot();
	std::set<std::string> loadedWorkspacePaths;
	std::vector<WorkspaceSymbolDisplayEntry> entries;
	std::vector<LspResultDialogRow> resultRows;
	std::vector<MRColumnListView::Row> displayRows;
	std::string navigationError;
	std::string mainPath;
	std::string rootPath;
	ushort dialogResult = cmCancel;
	const short width = 112;
	short visibleRows = 0;
	short height = 0;
	short selected = -1;

	if (TProgram::deskTop == nullptr) return false;
	if (result.header.state != mr::services::MRServiceResultState::Current) {
		if (!result.header.errorMessage.empty()) postLspWarning("LSP workspace symbols unavailable: " + result.header.errorMessage);
		else
			postLspWarning("LSP workspace symbols unavailable.");
		return true;
	}
	if (result.symbols.empty()) {
		postLspWarning("LSP workspace symbols: no symbols.");
		return true;
	}

	if (workspace.mainFile.hasMainFile) mainPath = mr::services::normalizeWorkspaceServicePath(workspace.mainFile.path);
	if (workspace.root.hasRoot) rootPath = mr::services::normalizeWorkspaceServicePath(workspace.root.rootPath);
	for (const mr::services::MRWorkspaceDocumentSnapshot &document : workspace.documents) {
		if (!document.path.empty()) loadedWorkspacePaths.insert(mr::services::normalizeWorkspaceServicePath(document.path));
	}

	for (const mr::services::MRServiceDocumentSymbol &symbol : result.symbols) {
		WorkspaceSymbolDisplayEntry entry;
		LspResultDialogRow resultRow;
		MRColumnListView::Row displayRow;
		std::ostringstream positionText;
		const std::string &path = !symbol.target.path.empty() ? symbol.target.path : symbol.target.uri;
		const std::string normalizedPath = mr::services::normalizeWorkspaceServicePath(path);
		std::string baseName = path;
		const std::size_t slash = baseName.find_last_of("/\\");

		if (path.empty()) continue;
		if (!mainPath.empty() && normalizedPath == mainPath)
			entry.rank = 0;
		else if (loadedWorkspacePaths.find(normalizedPath) != loadedWorkspacePaths.end())
			entry.rank = 1;
		else if (!rootPath.empty() && (normalizedPath == rootPath || (normalizedPath.size() > rootPath.size() && normalizedPath.compare(0, rootPath.size(), rootPath) == 0 && normalizedPath[rootPath.size()] == '/')))
			entry.rank = 2;
		if (slash != std::string::npos) baseName.erase(0, slash + 1);
		positionText << (symbol.target.range.start.line + 1) << ":" << (symbol.target.range.start.character + 1);

		displayRow.push_back(firstDisplayLine(symbol.name, 220));
		if (!symbol.detail.empty()) displayRow.push_back(firstDisplayLine(symbol.detail, 160));
		displayRow.push_back(positionText.str());
		displayRow.push_back(baseName);
		displayRow.push_back(firstDisplayLine(path, 220));

		resultRow.action = LspResultDialogAction::NavigateLocation;
		resultRow.location = symbol.target;
		entry.resultRow = resultRow;
		entry.displayRow = displayRow;
		entries.push_back(entry);
	}
	if (entries.empty()) {
		postLspWarning("LSP workspace symbols: no navigable symbols.");
		return true;
	}
	std::stable_sort(entries.begin(), entries.end(), [](const WorkspaceSymbolDisplayEntry &left, const WorkspaceSymbolDisplayEntry &right) {
		return left.rank < right.rank;
	});
	for (const WorkspaceSymbolDisplayEntry &entry : entries) {
		resultRows.push_back(entry.resultRow);
		displayRows.push_back(entry.displayRow);
	}

	visibleRows = static_cast<short>(std::max<int>(5, std::min<int>(static_cast<int>(resultRows.size()), 14)));
	height = static_cast<short>(visibleRows + 5);
	dialog = mr::dialogs::createScrollableDialog("LSP WORKSPACE SYMBOLS", width, height);
	dialog->insert(new TStaticText(TRect(2, 1, width - 2, 2), "Select symbol:"));
	verticalScrollBar = new TScrollBar(TRect(width - 3, 2, width - 2, static_cast<short>(2 + visibleRows)));
	horizontalScrollBar = new TScrollBar(TRect(2, static_cast<short>(2 + visibleRows), width - 3, static_cast<short>(3 + visibleRows)));
	dialog->insert(verticalScrollBar);
	dialog->insert(horizontalScrollBar);
	listView = new MRColumnListView(TRect(2, 2, width - 3, static_cast<short>(2 + visibleRows)), verticalScrollBar, horizontalScrollBar, dialog, 0, cmOK);
	listView->setActivateOnSingleClick(true);
	dialog->insert(listView);
	listView->setRows(displayRows, 0);
	dialog->setDialogValidationHook([listView, &resultRows]() {
		MRScrollableDialog::DialogValidationResult validation;
		const short selectedIndex = listView != nullptr ? listView->selectedIndex() : -1;

		validation.valid = selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < resultRows.size();
		if (!validation.valid) validation.warningText = "Select a symbol.";
		return validation;
	});
	dialog->finalizeLayout();
	dialogResult = TProgram::deskTop->execView(dialog);
	if (dialogResult == cmOK && listView != nullptr) selected = listView->selectedIndex();
	TObject::destroy(dialog);

	if (dialogResult == cmOK && selected >= 0 && static_cast<std::size_t>(selected) < resultRows.size()) {
		if (!navigateToLspLocation(resultRows[static_cast<std::size_t>(selected)].location, navigationError)) postLspWarning("LSP workspace symbol navigation failed: " + navigationError);
	}
	return true;
}

std::vector<LspMiniMenuEntry> buildLspEditMiniMenuItems(MREditWindow *targetWindow) {
	std::vector<LspMiniMenuEntry> entries;
	const bool hasMarkedText = targetWindow != nullptr && (targetWindow->hasSelection() || targetWindow->hasBlock());

	if (hasMarkedText) entries.push_back(LspMiniMenuEntry{"Cut", cmMrEditCutToBuffer, false});
	if (hasMarkedText) entries.push_back(LspMiniMenuEntry{"Copy", cmMrEditCopyToBuffer, false});
	entries.push_back(LspMiniMenuEntry{"Paste", cmMrEditPasteFromBuffer, false});
	return entries;
}

bool chooseLspMiniMenuCommand(TGroup &owner, MREditWindow *targetWindow, TPoint where, const LspEditorRequestTarget *target, ushort &command) {
	const std::vector<LspMiniMenuEntry> entries = buildLspContextMenuItems(targetWindow, target);
	const std::vector<LspMiniMenuEntry> editEntries = buildLspEditMiniMenuItems(targetWindow);
	MRFileEditor *editor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	std::vector<std::string> values;
	std::vector<std::string> editValues;
	MRColumnListView *parentList = nullptr;
	MRColumnListView *editList = nullptr;
	MRColumnListView *activeList = nullptr;
	short selected = -1;
	short editSelected = -1;
	const short menuWidth = lspMiniMenuWidthForEntries(entries);
	bool done = false;

	command = 0;
	values.reserve(entries.size());
	for (const LspMiniMenuEntry &entry : entries)
		values.push_back(lspMiniMenuDisplayText(entry, menuWidth));
	editValues.reserve(editEntries.size());
	for (const LspMiniMenuEntry &entry : editEntries)
		editValues.push_back(entry.title);

	parentList = showLspMiniMenuList(owner, editor, where, values, nullptr, menuWidth);
	activeList = parentList;
	if (parentList == nullptr) return false;
	g_lspContextMiniMenuOpen = true;
	while (!done) {
		TEvent event{};

		activeList->getEvent(event);
		if (event.what == evMouseDown && parentList != nullptr && parentList->mouseInView(event.mouse.where)) {
			selected = lspMiniMenuClickedIndex(*parentList, event.mouse.where);
			parentList->handleEvent(event);
			event.what = evNothing;
			if (selected < 0 || static_cast<std::size_t>(selected) >= entries.size()) {
				done = true;
				continue;
			}
			if (entries[static_cast<std::size_t>(selected)].editSubmenu) {
				TPoint editWhere = where;

				editWhere.x += menuWidth;
				editWhere.y += selected;
				if (editList == nullptr) editList = showLspMiniMenuList(owner, editor, editWhere, editValues);
				activeList = editList != nullptr ? editList : parentList;
				continue;
			}
			command = entries[static_cast<std::size_t>(selected)].command;
			done = true;
			continue;
		}
		if (event.what == evMouseDown && editList != nullptr && editList->mouseInView(event.mouse.where)) {
			editSelected = lspMiniMenuClickedIndex(*editList, event.mouse.where);
			editList->handleEvent(event);
			event.what = evNothing;
			if (editSelected >= 0 && static_cast<std::size_t>(editSelected) < editEntries.size()) command = editEntries[static_cast<std::size_t>(editSelected)].command;
			done = true;
			continue;
		}
		if (event.what == evMouseDown) {
			event.what = evNothing;
			done = true;
			continue;
		}
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEsc) {
			event.what = evNothing;
			done = true;
			continue;
		}
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter) {
			if (activeList == editList && editList != nullptr) {
				editSelected = editList->selectedIndex();
				if (editSelected >= 0 && static_cast<std::size_t>(editSelected) < editEntries.size()) command = editEntries[static_cast<std::size_t>(editSelected)].command;
				event.what = evNothing;
				done = true;
				continue;
			}
			selected = parentList->selectedIndex();
			event.what = evNothing;
			if (selected < 0 || static_cast<std::size_t>(selected) >= entries.size()) {
				done = true;
				continue;
			}
			if (entries[static_cast<std::size_t>(selected)].editSubmenu) {
				TPoint editWhere = where;

				editWhere.x += menuWidth;
				editWhere.y += selected;
				if (editList == nullptr) editList = showLspMiniMenuList(owner, editor, editWhere, editValues);
				activeList = editList != nullptr ? editList : parentList;
				continue;
			}
			command = entries[static_cast<std::size_t>(selected)].command;
			done = true;
			continue;
		}
		activeList->handleEvent(event);
	}
	g_lspContextMiniMenuOpen = false;
	destroyLspMiniMenuList(owner, editList);
	destroyLspMiniMenuList(owner, parentList);
	return command != 0;
}

bool showLspContextMenuForWindow(MREditWindow *targetWindow, TPoint where) {
	ushort command = 0;
	TGroup *owner = targetWindow != nullptr ? static_cast<TGroup *>(targetWindow) : TProgram::deskTop;
	LspEditorRequestTarget target;

	if (owner == nullptr) return false;
	if (targetWindow != nullptr) static_cast<void>(activateLspTargetWindow(targetWindow));
	forgetLspAutoHoverForWindow(targetWindow, true);
	if (!lspRequestTargetFromGlobalPoint(targetWindow, where, target)) return true;
	if (!chooseLspMiniMenuCommand(*owner, targetWindow, where, &target, command)) return true;
	switch (command) {
		case cmMrOtherLspDefinition:
			return requestLspEditorCommandForWindow(targetWindow, mr::services::MRLspServiceCommandId::GoToDefinition, "LSP definition", true, nullptr, &target);
		case cmMrOtherLspReferences:
			return requestLspEditorCommandForWindow(targetWindow, mr::services::MRLspServiceCommandId::FindReferences, "LSP references", true, nullptr, &target);
		case cmMrOtherLspHover:
			return requestLspEditorCommandForWindow(targetWindow, mr::services::MRLspServiceCommandId::ShowHover, "LSP hover", true, nullptr, &target);
		case cmMrOtherLspComplete:
			return requestLspCompletionCommand(targetWindow, &target);
		case cmMrOtherLspDocumentHighlight:
			return requestLspEditorCommandForWindow(targetWindow, mr::services::MRLspServiceCommandId::DocumentHighlight, "LSP document highlight", true, nullptr, &target);
		case cmMrOtherLspDocumentSymbols:
			return requestLspEditorCommandForWindow(targetWindow, mr::services::MRLspServiceCommandId::DocumentSymbols, "LSP document symbols", true, nullptr, &target);
		case cmMrOtherLspWorkspaceSymbols:
			return requestLspEditorCommandForWindow(targetWindow, mr::services::MRLspServiceCommandId::WorkspaceSymbols, "LSP workspace symbols", true, nullptr, &target);
		case cmMrOtherLspSignatureHelp:
			return requestLspEditorCommandForWindow(targetWindow, mr::services::MRLspServiceCommandId::SignatureHelp, "LSP signature help", true, nullptr, &target);
		case cmMrOtherLspRename:
			return requestLspRenameCommand(targetWindow, &target);
		case cmMrOtherLspCodeActions:
			return requestLspCodeActionsAtPosition(targetWindow, &target);
		default:
			break;
	}
	return handleMRCommand(command);
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

bool openRecentFileValue(const std::string &value) {
	std::string resolvedPath;

	if (!resolveReadableExistingPath(MRDialogHistoryScope::OpenFile, value.c_str(), resolvedPath)) {
		forgetLoadDialogPath(MRDialogHistoryScope::OpenFile, value.c_str());
		return true;
	}
	if (!openResolvedFilesIntoWindows(std::vector<std::string>{resolvedPath})) forgetLoadDialogPath(MRDialogHistoryScope::OpenFile, resolvedPath.c_str());
	return true;
}

bool openLoadDialogForRecentFolder(const std::string &value) {
	char fileName[MAXPATH];
	std::string folder = normalizeConfiguredPathInput(value);
	std::string resolvedPath;
	ushort result = cmCancel;

	if (folder.empty() || !isReadableDirectory(folder)) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Folder is not readable: " + folder, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		forgetLoadDialogPath(MRDialogHistoryScope::LoadFile, folder.c_str());
		return true;
	}

	std::memset(fileName, 0, sizeof(fileName));
	mr::dialogs::writeRecordField(fileName, sizeof(fileName), folder);
	result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::LoadFile, "*.*", "LOAD FILE", "~N~ame", fdOpenButton, fileName);
	if (result == cmCancel) return true;
	if (!resolveReadableExistingPath(MRDialogHistoryScope::LoadFile, fileName, resolvedPath)) {
		forgetLoadDialogPath(MRDialogHistoryScope::LoadFile, fileName);
		return true;
	}
	if (!loadResolvedFilesIntoWindows(std::vector<std::string>{resolvedPath})) forgetLoadDialogPath(MRDialogHistoryScope::LoadFile, resolvedPath.c_str());
	return true;
}

bool restoreRecentWorkspaceValue(const std::string &value) {
	const std::string selectedPath = mr::dialogs::ensureMrmacExtension(normalizeConfiguredPathInput(value));
	const bool readable = ::access(selectedPath.c_str(), F_OK) == 0 && ::access(selectedPath.c_str(), R_OK) == 0;

	mrLoadWorkspace(selectedPath);
	if (readable) rememberLoadDialogPath(MRDialogHistoryScope::WorkspaceLoad, selectedPath.c_str());
	else
		forgetLoadDialogPath(MRDialogHistoryScope::WorkspaceLoad, selectedPath.c_str());
	return true;
}

bool handleFileGetLast() {
	GetLastDialog *dialog = nullptr;
	ushort result = cmCancel;
	GetLastKind acceptedKind = GetLastKind::None;
	std::string acceptedValue;
	std::vector<std::string> files = recentValuesForScopes(MRDialogHistoryScope::OpenFile, MRDialogHistoryScope::LoadFile, true, configuredMaxFileHistory());
	std::vector<std::string> folders = recentValuesForScopes(MRDialogHistoryScope::OpenFile, MRDialogHistoryScope::LoadFile, false, configuredMaxPathHistory());
	std::vector<std::string> workspaces = recentWorkspaceValues();
	const short width = 96;
	const short fileRows = getLastListHeight(files.size());
	const short folderRows = getLastListHeight(folders.size());
	const short workspaceRows = getLastListHeight(workspaces.size());
	const short height = static_cast<short>(fileRows + folderRows + workspaceRows + 8);

	if (TProgram::deskTop == nullptr) return true;
	dialog = new GetLastDialog(files, folders, workspaces, width, height, fileRows, folderRows, workspaceRows);
	dialog->finalizeLayout();
	dialog->selectInitialList();
	result = TProgram::deskTop->execView(dialog);
	if (result == cmOK) {
		acceptedKind = dialog->acceptedKind();
		acceptedValue = dialog->acceptedValue();
	}
	TObject::destroy(dialog);

	if (result != cmOK || acceptedValue.empty()) return true;
	switch (acceptedKind) {
		case GetLastKind::File:
			return openRecentFileValue(acceptedValue);
		case GetLastKind::Folder:
			return openLoadDialogForRecentFolder(acceptedValue);
		case GetLastKind::Workspace:
			return restoreRecentWorkspaceValue(acceptedValue);
		case GetLastKind::None:
		default:
			break;
	}
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

bool handleEditCopyToSystemClipboard(MREditWindow *targetWindow);
bool handleEditCutToSystemClipboard(MREditWindow *targetWindow);

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
			return handleEditCutToSystemClipboard(window);
		case cmMrEditMarkAll:
			if (window == nullptr) return false;
			return window->markAllLines();
		case cmMrEditCopyToBuffer:
			if (window == nullptr) return false;
			return handleEditCopyToSystemClipboard(window);
		case cmMrEditPasteFromBuffer:
			if (window == nullptr) return false;
			if (window->isReadOnly()) {
				postDialogWarning(kWindowReadOnlyMessage);
				return true;
			}
			if (window->hasBlock()) return handleCopyBlock(window);
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
		case cmMrTextToggleLineDrawing:
			if (window == nullptr || window->getEditor() == nullptr) return false;
			window->getEditor()->toggleLineDrawing();
			return true;
		case cmMrTextToggleDoubleLines:
			if (window == nullptr || window->getEditor() == nullptr) return false;
			window->getEditor()->toggleLineDrawingDoubleLines();
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

bool handleEditCopyToSystemClipboard(MREditWindow *targetWindow) {
	MREditWindow *window = effectiveKeymapWindow(targetWindow);

	if (window == nullptr) return false;
	if (window->hasSelection()) return dispatchEditorCommandEvent(window, cmCopy);
	if (window->hasBlock()) return copyMarkedBlockToSystemClipboard(window);
	return dispatchEditorCommandEvent(window, cmCopy);
}

bool handleEditCutToSystemClipboard(MREditWindow *targetWindow) {
	MREditWindow *window = effectiveKeymapWindow(targetWindow);
	std::string text;
	std::string errorText;

	if (window == nullptr) return false;
	if (window->isReadOnly()) {
		postDialogWarning(kWindowReadOnlyMessage);
		return true;
	}
	if (window->hasSelection()) return dispatchEditorCommandEvent(window, cmCut);
	if (!window->hasBlock()) return dispatchEditorCommandEvent(window, cmCut);
	if (!window->captureBlockPayload(text, &errorText)) {
		postDialogWarning(errorText.empty() ? "No block marked." : errorText);
		return true;
	}
	TClipboard::setText(TStringView(text.data(), text.size()));
	if (!window->deleteBlock(&errorText)) postDialogWarning(errorText.empty() ? "Unable to delete block." : errorText);
	return true;
}

} // namespace

bool mrLspCompletionTargetSelfTestForRegression(std::string &failureReason) {
	return lspCompletionTargetSelfTestForRegression(failureReason);
}

bool showMRLspContextMenu(MREditWindow *targetWindow, TPoint where) {
	return showLspContextMenuForWindow(targetWindow, where);
}

void notifyMRLspMouseActivity(TPoint where) noexcept {
	g_lspLastMousePosition = where;
	g_lspMousePositionKnown = true;
}

void notifyMRLspBlockMouseActivity() noexcept {
	g_lspMousePositionKnown = false;
	forgetLspAutoHover(true);
}

void notifyMRLspKeyboardActivity() noexcept {
	g_lspMousePositionKnown = false;
	forgetLspAutoHover(true);
}

bool dispatchMRKeymapAction(std::string_view actionId, std::string_view sequenceText, MREditWindow *targetWindow) {
	const KeymapActionDispatchEntry *entry = nullptr;
	MREditWindow *window = effectiveKeymapWindow(targetWindow);
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	const std::optional<int> markIndex = randomAccessMarkIndexFromSequence(sequenceText);

	for (const KeymapActionDispatchEntry &candidate : kKeymapActionDispatchTable) {
		if (candidate.actionId == actionId) {
			entry = &candidate;
			break;
		}
	}
	if (entry == nullptr) return false;
	switch (entry->kind) {
		case KeymapDispatchKind::AppCommand:
			return dispatchTargetedKeymapAppCommand(window, entry->command);
		case KeymapDispatchKind::EditorCommand:
			return dispatchEditorCommandEvent(window, entry->command);
		case KeymapDispatchKind::WindowMethod:
			return dispatchKeymapWindowMethod(window, entry->windowMethod);
		case KeymapDispatchKind::Custom:
			switch (entry->customAction) {
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
				case KeymapCustomAction::SnippetPlaceholderNext:
					if (mrMoveSnippetPlaceholderForParent(window, 1)) return true;
					postLspWarning("No active snippet sidekick.");
					return true;
				case KeymapCustomAction::SnippetPlaceholderPrevious:
					if (mrMoveSnippetPlaceholderForParent(window, -1)) return true;
					postLspWarning("No active snippet sidekick.");
					return true;
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

void pumpLspAutoHoverDwell() {
	MREditWindow *win = nullptr;
	MRFileEditor *editor = nullptr;
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::services::MRLspServerProfile profile;
	LspEditorRequestTarget target;
	std::string configurationSource;
	std::string errorMessage;
	bool requestSent = false;
	TPoint mousePosition;
	const bool mouseValid = currentLspHoverMousePosition(mousePosition);
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	const MRLanguageServerChannelSettings channels = configuredLanguageServerChannelSettings();

	if (now - g_lspLastHoverPumpAt < kLspHoverPumpInterval) return;
	g_lspLastHoverPumpAt = now;
	if (g_lspContextMiniMenuOpen) return;
	if (g_lspSignatureHelp.active) return;
	if (lspModalViewActive()) {
		forgetLspAutoHover(true);
		return;
	}
	if (!channels.diagnostics && !channels.hover) {
		forgetLspAutoHover(true);
		return;
	}
	if (!mouseValid) {
		forgetLspAutoHover(true);
		return;
	}
	win = lspEditorWindowAtGlobalPoint(mousePosition, target);
	editor = win != nullptr ? win->getEditor() : nullptr;
	if (editor == nullptr) {
		forgetLspAutoHover(true);
		return;
	}
	if (!buildLspDocumentSnapshotForWindow(win, document, false)) {
		forgetLspAutoHoverForWindow(win, true);
		return;
	}
	if (!buildLspServerProfileFromEditor(*editor, profile, configurationSource, errorMessage)) {
		forgetLspAutoHoverForWindow(win, true);
		return;
	}

	if (!lspAutoHoverMatches(document, win->bufferId(), target.offset, target.position)) {
		if ((g_lspAutoHover.sidekickOpen || mrHasReadOnlySidekickForParent(win)) && g_lspAutoHover.bufferId == win->bufferId()) mrDropSidekickForParent(win);
		retirePendingLspAutoHoverRequest();
		armLspAutoHoverForPosition(document, win->bufferId(), target.offset, target.position, mousePosition, mouseValid, now);
		mrLogMessage("LSP hover dwell armed at cursor/mouse position.");
		return;
	}
	if (mouseValid && !lspAutoHoverMouseMatches(mousePosition)) {
		if ((g_lspAutoHover.sidekickOpen || mrHasReadOnlySidekickForParent(win)) && g_lspAutoHover.bufferId == win->bufferId()) {
			mrDropSidekickForParent(win);
			mrLogMessage("LSP hover sidekick dismissed after mouse move.");
		}
		retirePendingLspAutoHoverRequest();
		armLspAutoHoverForPosition(document, win->bufferId(), target.offset, target.position, mousePosition, true, now);
		mrLogMessage("LSP hover dwell re-armed after mouse move.");
		return;
	}
	if (mrConsumeReadOnlySidekickDismissedForParent(win)) {
		g_lspAutoHover.sidekickOpen = false;
		g_lspAutoHover.dismissedForKey = true;
		g_lspAutoHover.quietUntil = std::chrono::steady_clock::time_point::max();
	}
	if (g_lspAutoHover.sidekickOpen) return;
	if (g_lspAutoHover.dismissedForKey) {
		if (now < g_lspAutoHover.quietUntil) return;
		g_lspAutoHover.dismissedForKey = false;
	}
	if (g_lspAutoHover.requested) return;
	if (now - g_lspAutoHover.stableSince < std::chrono::milliseconds(configuredLanguageServerHoverDwellMs())) return;
		if (channels.diagnostics) {
			const mr::services::MRLspPositionServiceSnapshot snapshot = g_lspAppService.currentDocumentPositionServiceSnapshot(document, serviceTextPositionFromLsp(target.position));
			const std::string diagnosticText = buildLspDiagnosticSidekickText(snapshot);
			int diagnosticViewColumn = target.viewColumn;
			int diagnosticViewRow = target.viewRow;
			const MRReadOnlySidekickPlacement placement = configuredLspReadOnlySidekickPlacement();
			diagnosticViewRow = lspReadOnlyHoverAnchorRow(diagnosticViewRow, placement, true);
			if (!diagnosticText.empty() &&
			    mrOpenReadOnlySidekickAt(win, diagnosticText, "LSP hover", diagnosticViewColumn, diagnosticViewRow, diagnosticViewColumn, placement)) {
				g_lspAutoHover.sidekickOpen = true;
				g_lspAutoHover.bufferId = win->bufferId();
			mrLogMessage("LSP diagnostic hover sidekick opened after dwell.");
			return;
		}
	}

	if (!channels.hover) return;
	if (!requestLspEditorCommandForWindow(win, mr::services::MRLspServiceCommandId::ShowHover, "LSP hover", false, &requestSent, &target)) return;
	if (requestSent) {
		g_lspAutoHover.requested = true;
		g_lspAutoHover.requestId = g_lspAppService.activeHoverRequestId();
		mrLogMessage("LSP hover auto request sent after dwell.");
	} else {
		g_lspAutoHover.dismissedForKey = true;
		g_lspAutoHover.quietUntil = std::chrono::steady_clock::time_point::max();
	}
}

void pumpLspCurrentDocumentSync() {
	MREditWindow *win = currentEditorCommandWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::services::MRLspServerProfile profile;
	std::string configurationSource;
	std::string errorMessage;
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	if (!g_lspAppService.runtimeActive()) return;
	if (now - g_lspLastDocumentSyncCheckAt < kLspDocumentSyncCheckInterval) return;
	g_lspLastDocumentSyncCheckAt = now;
	if (!configuredLanguageServerChannelSettings().diagnostics) return;
	if (editor == nullptr) return;
	if (!buildLspDocumentSnapshotForWindow(win, document, false)) return;
	if (document.bufferId != g_lspObservedBufferId || document.documentId != g_lspObservedDocumentId || document.documentVersion != g_lspObservedDocumentVersion) {
		g_lspObservedBufferId = document.bufferId;
		g_lspObservedDocumentId = document.documentId;
		g_lspObservedDocumentVersion = document.documentVersion;
		g_lspDocumentChangeObservedAt = now;
		return;
	}
	if (document.bufferId == g_lspSyncedBufferId && document.documentId == g_lspSyncedDocumentId && document.documentVersion == g_lspSyncedDocumentVersion) return;
	if (now - g_lspDocumentChangeObservedAt < std::chrono::milliseconds(configuredLanguageServerDocumentSyncDelayMs())) return;
	if (!buildLspServerProfileFromEditor(*editor, profile, configurationSource, errorMessage)) return;
	if (!g_lspAppService.syncCurrentEditorDocument(profile, document, *editor, errorMessage)) {
		g_lspLastRequestState = "sync failed";
		g_lspLastError = errorMessage;
		mrLogMessage("LSP document sync failed: " + errorMessage);
		return;
	}
	g_lspSyncedBufferId = document.bufferId;
	g_lspSyncedDocumentId = document.documentId;
	g_lspSyncedDocumentVersion = document.documentVersion;
	mrLogMessage("LSP document synced after edit debounce.");
}

bool lspSignatureTypedTrigger(MRFileEditor &editor, const LspSignatureCallContext &context) {
	if (context.requestTarget.offset == 0) return false;
	const char previous = editor.charAtOffset(context.requestTarget.offset - 1);
	return previous == '(' || previous == ',';
}

void pumpLspSignatureHelpLifecycle() {
	MREditWindow *win = currentEditorCommandWindow();
	MREditWindow *activeWindow = g_lspSignatureHelp.active ? findEditWindowByBufferId(g_lspSignatureHelp.bufferId) : nullptr;
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	mr::services::MRWorkspaceDocumentSnapshot document;
	LspEditorRequestTarget cursorTarget;
	LspSignatureCallContext context;
	bool requestSent = false;

	if (!configuredLanguageServerChannelSettings().signatureHelp) {
		if (g_lspSignatureHelp.active) clearLspSignatureHelpState(activeWindow);
		return;
	}
	if (editor == nullptr) {
		if (g_lspSignatureHelp.active) clearLspSignatureHelpState(activeWindow);
		return;
	}
	if (!buildLspDocumentSnapshotForWindow(win, document, false)) {
		if (g_lspSignatureHelp.active) clearLspSignatureHelpState(activeWindow);
		return;
	}
	if (g_lspSignatureHelp.active && (win->bufferId() != g_lspSignatureHelp.bufferId || document.documentId != g_lspSignatureHelp.documentId)) {
		clearLspSignatureHelpState(activeWindow);
		return;
	}
		lspRequestTargetFromCursor(*editor, cursorTarget);
	if (g_lspSignatureHelp.active && mrConsumeReadOnlySidekickDismissedForParent(win)) {
		g_lspSignatureHelp = LspSignatureHelpState();
		return;
	}
	if (!lspSignatureCallContextAroundTarget(*editor, cursorTarget, context)) {
		if (g_lspSignatureHelp.active) clearLspSignatureHelpState(win);
		return;
	}
	if (g_lspSignatureHelp.active && context.openParenOffset != g_lspSignatureHelp.openParenOffset) {
		clearLspSignatureHelpState(win);
		return;
	}
	if (!g_lspSignatureHelp.active && !lspSignatureTypedTrigger(*editor, context)) return;
	if (!lspSignatureRefreshTrigger(*editor, context, document)) return;
	static_cast<void>(requestLspEditorCommandForWindow(win, mr::services::MRLspServiceCommandId::SignatureHelp, "LSP signature help", false, &requestSent, &context.requestTarget));
	if (!requestSent && g_lspSignatureHelp.active) g_lspSignatureHelp.requestPending = false;
}

void pumpMRLspService() {
	std::string errorMessage;
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	if (!configuredLanguageServerSpawnDaemon()) {
		if (g_lspAppService.runtimeActive()) mrApplyLspSupportSettingsChange();
		return;
	}
	if (now - g_lspLastServicePumpAt < kLspServicePumpInterval) return;
	g_lspLastServicePumpAt = now;
	pumpLspSignatureHelpLifecycle();
	pumpLspAutoHoverDwell();
	if (!g_lspAppService.runtimeActive()) return;
	pumpLspCurrentDocumentSync();
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

void mrApplyLspSupportSettingsChange() {
	std::string errorMessage;

	if (configuredLanguageServerSpawnDaemon()) return;
	forgetLspAutoHover(true);
	clearLspSignatureHelpState(g_lspSignatureHelp.active ? findEditWindowByBufferId(g_lspSignatureHelp.bufferId) : nullptr);
	if (!g_lspAppService.runtimeActive()) return;
	if (!g_lspAppService.shutdown(errorMessage)) {
		if (!errorMessage.empty()) mrLogMessage("LSP shutdown after disabling support failed: " + errorMessage);
		g_lspAppService.close();
		return;
	}
	mrLogMessage("LSP runtime shut down after disabling support.");
}

bool handleMRCommand(ushort command, void *commandInfo) {
	switch (command) {
		case cmMrFileOpen:
			return handleFileOpen();

		case cmMrFileLoad:
			return handleFileLoad();

		case cmMrFileGetLast:
			return handleFileGetLast();

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

		case cmMrEditMarkAll: {
			MREditWindow *win = currentEditWindow();
			return win != nullptr && win->markAllLines();
		}

		case cmMrEditCutToBuffer:
			return handleEditCutToSystemClipboard(currentEditorCommandWindow());

		case cmMrEditCopyToBuffer:
			return handleEditCopyToSystemClipboard(currentEditorCommandWindow());

		case cmMrEditAppendToBuffer:
		case cmMrEditCutAndAppendToBuffer:
			return runDisabledBlockAction();

		case cmMrEditPasteFromBuffer:
			if (currentEditorCommandWindow() != nullptr && currentEditorCommandWindow()->hasBlock()) return handleCopyBlock(currentEditorCommandWindow());
			return dispatchEditorCommand(cmPaste, true);

		case cmMrEditToggleInsertMode: {
			MREditWindow *win = currentEditWindow();
			if (win != nullptr && win->getEditor() != nullptr) win->getEditor()->setInsertModeEnabled(!win->getEditor()->insertModeEnabled());
			return true;
		}

		case cmMrTextToggleLineDrawing: {
			MREditWindow *win = currentEditWindow();
			if (win != nullptr && win->getEditor() != nullptr) win->getEditor()->toggleLineDrawing();
			return true;
		}

		case cmMrTextToggleDoubleLines: {
			MREditWindow *win = currentEditWindow();
			if (win != nullptr && win->getEditor() != nullptr) win->getEditor()->toggleLineDrawingDoubleLines();
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
		case cmMrSetupLspSupport:
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
			return requestLspCompletionCommand();

		case cmMrOtherLspDocumentHighlight:
			return requestLspEditorCommand(mr::services::MRLspServiceCommandId::DocumentHighlight, "LSP document highlight");

		case cmMrOtherLspDocumentSymbols:
			return requestLspEditorCommand(mr::services::MRLspServiceCommandId::DocumentSymbols, "LSP document symbols");

		case cmMrOtherLspWorkspaceSymbols:
			return requestLspEditorCommand(mr::services::MRLspServiceCommandId::WorkspaceSymbols, "LSP workspace symbols");

		case cmMrOtherLspSignatureHelp:
			return requestLspEditorCommand(mr::services::MRLspServiceCommandId::SignatureHelp, "LSP signature help");

		case cmMrOtherLspRename:
			return requestLspRenameCommand();

		case cmMrOtherLspCodeActions:
			return requestLspCodeActionsAtCurrentPosition();

		case cmMrOtherLspStatus:
			return showLspStatusDialog();

		case cmMrOtherLspResults:
			if (!syncCurrentEditorForLspResults()) return true;
			return showLspResultsDialog();

		case cmMrOtherMacroLibrary:
			return runMacroLibraryDialog();

		case cmMrOtherMatchBraceOrParen:
			return handleMatchParenthesis();

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
