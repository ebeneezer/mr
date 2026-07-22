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
#include "../ui/MRWindowLayout.hpp"
#include "router/MRCommandRouterSearch.hpp"
#include "router/MRCommandRouterSearchMultiFile.hpp"
#include "router/MRCommandRouterText.hpp"
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
#include "../keymap/MRKeymapActionCatalog.hpp"
#include "../keymap/MRKeymapSequence.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../mrmac/MRVM.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/ui/conventional/MRVMEditor.hpp"
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
#include "../ui/MRBentoBox/MRBentoBox.hpp"
#include "../ui/MRBentoHexEditor/MRBentoHexEditor.hpp"
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
bool startExternalCommandInWindow(MREditWindow *win, const std::string &commandLine, bool replaceBuffer, bool activate, bool closeOnFailure, std::string_view titleOverride = std::string_view(), const std::string &successAudioUri = std::string(), const std::string &failureAudioUri = std::string(), const MRBuildHookContext &buildContext = MRBuildHookContext());

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
		if (!normalized.empty()) outEntries.push_back(GetLastEntry{normalized});
	}
}

std::vector<std::string> recentValuesForScopes(MRDialogHistoryScope firstScope, MRDialogHistoryScope secondScope, bool files, int limit) {
	std::vector<GetLastEntry> entries;
	std::vector<std::string> values;
	std::set<std::string> seen;

	appendHistoryEntries(entries, firstScope, files);
	appendHistoryEntries(entries, secondScope, files);
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
    KeymapActionDispatchEntry{"PRETTIFY_BLOCK_OR_FILE", KeymapDispatchKind::EditorCommand, cmMrTextPrettifyBlockOrFile, KeymapWindowMethod::None, KeymapCustomAction::None},
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
    KeymapActionDispatchEntry{"MR_FIND_REFERENCES", KeymapDispatchKind::AppCommand, cmMrOtherReferences, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SNIPPET_PLACEHOLDER_NEXT", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SnippetPlaceholderNext},
    KeymapActionDispatchEntry{"MR_SNIPPET_PLACEHOLDER_PREVIOUS", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::SnippetPlaceholderPrevious},
    KeymapActionDispatchEntry{"MR_RENAME", KeymapDispatchKind::AppCommand, cmMrOtherRename, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_MACRO_TOGGLE_RECORDING", KeymapDispatchKind::AppCommand, cmMrMacroToggleRecording, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_EDIT_SETTINGS", KeymapDispatchKind::AppCommand, cmMrSetupEditSettings, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_COLOR", KeymapDispatchKind::AppCommand, cmMrSetupColorSetup, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_KEYMAP", KeymapDispatchKind::AppCommand, cmMrSetupKeyMapping, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_FILENAME_EXTENSIONS", KeymapDispatchKind::AppCommand, cmMrSetupFilenameExtensions, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_COMPILER_PROFILES", KeymapDispatchKind::AppCommand, cmMrSetupCompilerProfiles, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_PATHS", KeymapDispatchKind::AppCommand, cmMrSetupPaths, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_BACKUPS_AUTOSAVE", KeymapDispatchKind::AppCommand, cmMrSetupBackupsAutosave, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_SEARCH_REPLACE_DEFAULTS", KeymapDispatchKind::AppCommand, cmMrSetupSearchAndReplaceDefaults, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_USER_INTERFACE", KeymapDispatchKind::AppCommand, cmMrSetupUserInterfaceSettings, KeymapWindowMethod::None, KeymapCustomAction::None},
    KeymapActionDispatchEntry{"MR_SETUP_LIVE_LOGS", KeymapDispatchKind::AppCommand, cmMrSetupLiveLogs, KeymapWindowMethod::None, KeymapCustomAction::None},
};

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
	const bool trackFileCompareMutation = command == cmUndo || command == cmMrEditUndo || command == cmMrEditRedo || command == cmMrTextPrettifyBlockOrFile;
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

bool handleExportToPdf();

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

void postDialogInfo(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string(text), mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
	mrLogMessage(std::string(text));
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

bool editorIdentifierByte(char ch) noexcept {
	const unsigned char uch = static_cast<unsigned char>(ch);

	return std::isalnum(uch) != 0 || ch == '_';
}

bool editorIdentifierRangeAroundOffset(MRFileEditor &editor, std::size_t offset, std::size_t &start, std::size_t &end) {
	if (editor.bufferLength() == 0) return false;
	start = std::min(offset, editor.bufferLength());
	if (start == editor.bufferLength() || !editorIdentifierByte(editor.charAtOffset(start))) {
		if (start == 0 || !editorIdentifierByte(editor.charAtOffset(start - 1))) return false;
		--start;
	}
	end = start;
	while (start > editor.lineStartOffset(start) && editorIdentifierByte(editor.charAtOffset(start - 1)))
		--start;
	const std::size_t lineEnd = editor.lineEndOffset(end);
	while (end < lineEnd && editorIdentifierByte(editor.charAtOffset(end)))
		++end;
	return end > start;
}

std::string workspaceSearchTextAroundOffset(MRFileEditor &editor, std::size_t offset) {
	std::size_t identifierStart = 0;
	std::size_t identifierEnd = 0;
	std::size_t braceStart = 0;
	std::size_t braceEnd = 0;
	const std::size_t documentLength = editor.bufferLength();
	std::string text;

	if (!editorIdentifierRangeAroundOffset(editor, offset, identifierStart, identifierEnd)) return text;
	braceStart = identifierStart;
	while (braceStart > editor.lineStartOffset(identifierStart)) {
		const char ch = editor.charAtOffset(braceStart - 1);
		if (ch == '{') break;
		if (ch == '}' || ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '"' || ch == '\'') {
			braceStart = identifierStart;
			break;
		}
		--braceStart;
	}
	if (braceStart > editor.lineStartOffset(identifierStart) && editor.charAtOffset(braceStart - 1) == '{') {
		braceEnd = identifierEnd;
		while (braceEnd < editor.lineEndOffset(identifierEnd) && braceEnd < documentLength) {
			const char ch = editor.charAtOffset(braceEnd);
			if (ch == '}') break;
			if (ch == '{' || ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '"' || ch == '\'') {
				braceEnd = identifierEnd;
				break;
			}
			++braceEnd;
		}
		if (braceEnd < documentLength && editor.charAtOffset(braceEnd) == '}' && braceEnd > braceStart && braceEnd - braceStart <= 128) {
			for (std::size_t index = braceStart; index < braceEnd; ++index)
				text.push_back(editor.charAtOffset(index));
			return trimAscii(text);
		}
	}
	for (std::size_t index = identifierStart; index < identifierEnd; ++index)
		text.push_back(editor.charAtOffset(index));
	return text;
}

struct EditorTextTarget {
	std::size_t offset = 0;
	int viewColumn = 1;
	int viewRow = 1;
};

struct ContextMenuEntry {
	std::string title;
	ushort command = 0;
	bool editSubmenu = false;
};

struct LocalOutlineMenuEntry {
	std::string label;
	MREditWindow *targetWindow = nullptr;
	std::size_t sourceOffset = 0;
	std::size_t sourceSelectionEnd = 0;
	int outlineLevel = 0;
	bool header = false;
};

bool editorTextTargetFromEditorOffset(MRFileEditor &editor, std::size_t offset, int viewColumn, int viewRow, EditorTextTarget &target) {
	target.offset = std::min(offset, editor.bufferLength());
	target.viewColumn = std::max(1, viewColumn);
	target.viewRow = std::max(1, viewRow);
	return true;
}

int editorViewColumnForOffset(MRFileEditor &editor, std::size_t offset) {
	const std::size_t lineStart = editor.lineStartOffset(offset);

	return std::max(1, editor.charColumn(lineStart, offset) - editor.delta.x + 1);
}

bool editorTextTargetFromCursor(MRFileEditor &editor, EditorTextTarget &target) {
	return editorTextTargetFromEditorOffset(editor, editor.cursorOffset(), editor.currentViewColumn(), editor.currentViewRow(), target);
}

bool editorTextTargetFromGlobalPoint(MREditWindow *win, TPoint where, EditorTextTarget &target) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

	if (editor == nullptr || !editor->textPointInView(where)) return false;
	const std::size_t offset = editor->offsetForGlobalPoint(where);
	const std::size_t lineIndex = editor->lineIndexOfOffset(offset);
	const std::size_t visibleLine = editor->visibleLineForDocumentLine(lineIndex);
	const int viewColumn = editorViewColumnForOffset(*editor, offset);
	const int viewRow = static_cast<int>(visibleLine) - editor->delta.y + 1;
	return editorTextTargetFromEditorOffset(*editor, offset, viewColumn, viewRow, target);
}

bool activateEditorTargetWindow(MREditWindow *window) {
	if (window == nullptr) return false;
	if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window)) return bentoBox->activatePaneWindow(window);
	if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window->owner)) return bentoBox->activatePaneWindow(window);
	return mrActivateEditWindow(window);
}

bool workspaceSearchSeedForTarget(MREditWindow *win, const EditorTextTarget *requestTarget, std::string &seed) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	EditorTextTarget cursorTarget;
	const EditorTextTarget *target = requestTarget;

	seed.clear();
	if (editor == nullptr) {
		postDialogWarning("References/Rename requires an editor window.");
		return false;
	}
	if (target == nullptr) {
		editorTextTargetFromCursor(*editor, cursorTarget);
		target = &cursorTarget;
	}
	seed = workspaceSearchTextAroundOffset(*editor, target->offset);
	if (seed.empty()) {
		postDialogWarning("References/Rename requires an identifier.");
		return false;
	}
	return true;
}

std::string workspaceSearchStartingPath() {
	const std::string mainPathText = normalizeConfiguredPathInput(mrWorkspaceMainFilePath());

	if (!mainPathText.empty()) {
		const std::filesystem::path parent = std::filesystem::path(mainPathText).parent_path();

		if (!parent.empty()) return parent.lexically_normal().generic_string();
	}
	if (MREditWindow *win = currentEditorCommandWindow()) {
		MRFileEditor *editor = win->getEditor();

		if (editor != nullptr && editor->hasPersistentFileName()) {
			const std::filesystem::path parent = std::filesystem::path(editor->persistentFileName()).parent_path();

			if (!parent.empty()) return parent.lexically_normal().generic_string();
		}
	}
	return std::string();
}

bool requestWorkspaceReferencesCommand(MREditWindow *win = currentEditorCommandWindow(), const EditorTextTarget *target = nullptr) {
	std::string seed;
	std::string startingPath;

	if (!workspaceSearchSeedForTarget(win, target, seed)) return true;
	startingPath = workspaceSearchStartingPath();
	return handleWorkspaceMultiFileSearchDialog(seed, startingPath);
}

bool requestWorkspaceRenameCommand(MREditWindow *win = currentEditorCommandWindow(), const EditorTextTarget *target = nullptr) {
	std::string seed;
	std::string startingPath;

	if (!workspaceSearchSeedForTarget(win, target, seed)) return true;
	startingPath = workspaceSearchStartingPath();
	return handleWorkspaceMultiFileSearchReplaceDialog(seed, "", startingPath);
}

int menuDisplayWidth(const std::string &value) noexcept {
	return std::max(0, strwidth(value.c_str()));
}

short miniMenuWidthForValues(const std::vector<std::string> &values) noexcept {
	int width = 0;

	for (const std::string &value : values)
		width = std::max(width, menuDisplayWidth(value));
	width = std::max(width + 2, 12);
	return static_cast<short>(std::min(width, 40));
}

short miniMenuWidthForEntries(const std::vector<ContextMenuEntry> &entries) noexcept {
	int width = 0;

	for (const ContextMenuEntry &entry : entries)
		width = std::max(width, menuDisplayWidth(entry.title) + (entry.editSubmenu ? 2 : 0));
	width = std::max(width + 2, 12);
	return static_cast<short>(std::min(width, 40));
}

std::string miniMenuDisplayText(const ContextMenuEntry &entry, short menuWidth) {
	static const char kSubmenuArrow[] = "\342\226\266";
	const int interiorWidth = std::max(1, static_cast<int>(menuWidth) - 2);
	const int titleWidth = menuDisplayWidth(entry.title);
	const int arrowWidth = strwidth(kSubmenuArrow);
	std::string text = entry.title;

	if (!entry.editSubmenu) return text;
	if (titleWidth + arrowWidth < interiorWidth) text.append(static_cast<std::size_t>(interiorWidth - titleWidth - arrowWidth), ' ');
	else
		text.push_back(' ');
	text += kSubmenuArrow;
	return text;
}

bool miniMenuBoundsFor(TGroup &owner, MRFileEditor *editor, TPoint where, short width, short requestedRows, TRect &bounds) {
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

MRColumnListView *showMiniMenuList(TGroup &owner, MRFileEditor *editor, TPoint where, const std::vector<std::string> &values, short *menuWidth = nullptr, short forcedWidth = 0, short forcedRows = 0, bool contextMenuColors = true) {
	std::vector<MRColumnListView::Row> rows;
	MRColumnListView *listView = nullptr;
	const short width = forcedWidth > 0 ? forcedWidth : miniMenuWidthForValues(values);
	const short height = forcedRows > 0 ? forcedRows : static_cast<short>(std::min<std::size_t>(values.size(), 12));
	TRect bounds;

	if (values.empty()) return nullptr;
	if (!miniMenuBoundsFor(owner, editor, where, width, height, bounds)) return nullptr;
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

void destroyMiniMenuList(TGroup &owner, MRColumnListView *&listView) {
	if (listView == nullptr) return;
	if (owner.current == listView) owner.setCurrent(nullptr, TView::leaveSelect);
	owner.remove(listView);
	TObject::destroy(listView);
	listView = nullptr;
}

short miniMenuClickedIndex(MRColumnListView &listView, TPoint where) {
	const TPoint local = listView.makeLocal(where);
	const short clicked = static_cast<short>(listView.topItem + local.y);

	if (local.y < 0 || local.y >= listView.size.y) return -1;
	if (clicked < 0 || clicked >= listView.range) return -1;
	listView.focusItemNum(clicked);
	return clicked;
}

const char *localOutlineKindLabel(MROutlineKind kind) noexcept {
	switch (kind) {
		case mrokModule:
			return "mod";
		case mrokNamespace:
			return "ns";
		case mrokClass:
			return "type";
		case mrokMethod:
			return "meth";
		case mrokFunction:
			return "func";
		case mrokSection:
			return "sec";
		case mrokMacro:
			return "macro";
		case mrokTarget:
			return "target";
		case mrokBlock:
			return "block";
		default:
			return "sym";
	}
}

std::string localOutlineNodeName(const MROutlineSnapshot &snapshot, const MROutlineNode &node) {
	if (node.nameOffset >= snapshot.textPool.size()) return std::string();
	return std::string(snapshot.textPool.data() + node.nameOffset, std::min<std::size_t>(node.nameLength, snapshot.textPool.size() - node.nameOffset));
}

bool buildLocalOutlineMenuEntries(MRFileEditor &editor, std::vector<LocalOutlineMenuEntry> &entries, bool &complete) {
	MROutlineRequest request;
	MROutlineSnapshot snapshot;
	bool ready = false;

	entries.clear();
	complete = false;
	request.view = mrovStructure;
	request.allowPartial = true;
	static_cast<void>(editor.requestCompleteFoldOutlineWarmup());
	ready = editor.buildFoldOutlineSnapshot(request, snapshot);
	if (!ready) return false;
	complete = snapshot.complete;
	for (std::size_t i = 0; i < snapshot.nodes.size(); ++i) {
		const MROutlineNode &node = snapshot.nodes[i];
		LocalOutlineMenuEntry entry;
		std::uint32_t parent = node.parent;
		int depth = 0;
		char lineNumber[32];

		while (parent != MROutlineNode::npos && parent < snapshot.nodes.size() && depth < 20) {
			++depth;
			parent = snapshot.nodes[parent].parent;
		}
		std::snprintf(lineNumber, sizeof(lineNumber), "%4zu  ", node.selectionRange.start.line + 1);
		entry.label = lineNumber;
		for (int level = 0; level < depth; ++level)
			entry.label.append("  ");
		entry.label.append(localOutlineKindLabel(node.kind));
		entry.label.append("  ");
		entry.label.append(localOutlineNodeName(snapshot, node));
		if (node.confidence == mrocHeuristic) entry.label.append("  ?");
		entry.sourceOffset = node.selectionRange.start.offset;
		entry.sourceSelectionEnd = editor.lineEndOffset(entry.sourceOffset);
		if (entry.sourceSelectionEnd < entry.sourceOffset) entry.sourceSelectionEnd = entry.sourceOffset;
		entry.outlineLevel = depth;
		entries.push_back(entry);
	}
	return true;
}

std::string outlineHeaderForWindow(MREditWindow *win) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	const char *title = win != nullptr ? win->getTitle(0) : nullptr;

	if (editor != nullptr && editor->hasPersistentFileName()) return editor->persistentFileName();
	return title != nullptr ? std::string(title) : std::string("untitled");
}

bool appendWorkspaceOutlineEntriesForWindow(MREditWindow *win, std::vector<LocalOutlineMenuEntry> &entries, bool &complete, bool &warming, bool includeHeader) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::vector<LocalOutlineMenuEntry> localEntries;
	bool localComplete = false;

	if (editor == nullptr) return false;
	if (!buildLocalOutlineMenuEntries(*editor, localEntries, localComplete)) {
		warming = true;
		complete = false;
		return false;
	}
	if (!localComplete) complete = false;
	if (includeHeader && !localEntries.empty()) {
		LocalOutlineMenuEntry header;

		header.label = outlineHeaderForWindow(win);
		header.targetWindow = win;
		header.header = true;
		entries.push_back(std::move(header));
	}
	for (LocalOutlineMenuEntry &entry : localEntries) {
		entry.targetWindow = win;
		entries.push_back(std::move(entry));
	}
	return true;
}

bool buildWorkspaceOutlineMenuEntries(MREditWindow *preferredWindow, std::vector<LocalOutlineMenuEntry> &entries, bool &complete, bool &warming) {
	std::vector<MREditWindow *> windows = allEditWindowsAndBentoPanesInZOrder();
	std::vector<MREditWindow *> sourceWindows;
	std::set<int> seenBufferIds;

	entries.clear();
	complete = true;
	warming = false;
	if (preferredWindow != nullptr) {
		MRFileEditor *editor = preferredWindow->getEditor();

		if (editor != nullptr) {
			seenBufferIds.insert(preferredWindow->bufferId());
			sourceWindows.push_back(preferredWindow);
		}
	}
	for (MREditWindow *win : windows) {
		if (win == nullptr || win->getEditor() == nullptr) continue;
		if (seenBufferIds.find(win->bufferId()) != seenBufferIds.end()) continue;
		seenBufferIds.insert(win->bufferId());
		sourceWindows.push_back(win);
	}
	for (MREditWindow *win : sourceWindows)
		appendWorkspaceOutlineEntriesForWindow(win, entries, complete, warming, sourceWindows.size() > 1);
	if (seenBufferIds.empty()) complete = false;
	return !entries.empty();
}

bool jumpToLocalOutlineEntry(MREditWindow *win, const LocalOutlineMenuEntry &entry) {
	MREditWindow *targetWindow = entry.targetWindow != nullptr ? entry.targetWindow : win;
	MRFileEditor *editor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;

	if (entry.header) return true;
	if (editor == nullptr) return false;
	editor->setCursorOffset(entry.sourceOffset);
	editor->setSelectionOffsets(entry.sourceOffset, entry.sourceSelectionEnd);
	editor->revealCursor(True);
	static_cast<void>(activateEditorTargetWindow(targetWindow));
	postDialogInfo("Outline: " + firstDisplayLine(entry.label, 80));
	return true;
}

TPoint localOutlineMenuPoint(MRFileEditor &editor, const TPoint *where) {
	if (where != nullptr) return *where;
	return editor.makeGlobal(TPoint(static_cast<short>(std::max(0, editor.currentViewColumn() - 1)), static_cast<short>(std::max(0, editor.currentViewRow() - 1))));
}

bool showLocalOutlineForWindow(MREditWindow *win, const TPoint *where = nullptr) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	TGroup *owner = win != nullptr ? static_cast<TGroup *>(win) : TProgram::deskTop;
	std::vector<LocalOutlineMenuEntry> entries;
	std::vector<std::string> values;
	std::vector<MRColumnListView::RowStyle> rowStyles;
	MRColumnListView *listView = nullptr;
	TPoint menuPoint;
	short selected = -1;
	bool complete = false;
	bool warming = false;
	bool done = false;
	short outlineWidth = 0;
	short outlineRows = 0;

	if (owner == nullptr || editor == nullptr) {
		postDialogWarning("Outline requires an editor window.");
		return true;
	}
	if (!buildWorkspaceOutlineMenuEntries(win, entries, complete, warming)) {
		if (warming) {
			postDialogInfo("Workspace outline warming up.");
			return true;
		}
		postDialogWarning(complete ? "Workspace outline: no outline." : "Workspace outline: no outline in warmed range.");
		return true;
	}
	if (entries.empty()) {
		postDialogWarning(complete ? "Workspace outline: no outline." : "Workspace outline: no outline in warmed range.");
		return true;
	}
	if (entries.size() == 1) return jumpToLocalOutlineEntry(win, entries.front());
	values.reserve(entries.size());
	rowStyles.reserve(entries.size());
	for (const LocalOutlineMenuEntry &entry : entries) {
		values.push_back(entry.label);
		if (entry.header)
			rowStyles.push_back(MRColumnListView::RowStyle::OutlineHeader);
		else {
			const int level = std::clamp(entry.outlineLevel, 0, 9);
			rowStyles.push_back(static_cast<MRColumnListView::RowStyle>(static_cast<unsigned char>(MRColumnListView::RowStyle::OutlineLevel0) + level));
		}
	}
	menuPoint = localOutlineMenuPoint(*editor, where);
	{
		const TRect viewport = editor->visibleTextViewportBounds();
		const short viewportWidth = static_cast<short>(std::max<short>(12, viewport.b.x - viewport.a.x));
		const short viewportRows = static_cast<short>(std::max<short>(1, viewport.b.y - viewport.a.y));

		outlineWidth = static_cast<short>(std::max<short>(12, (viewportWidth * 3) / 4));
		outlineRows = static_cast<short>(std::max<short>(1, std::min<short>(static_cast<short>((viewportRows * 3) / 4), static_cast<short>(values.size()))));
	}
	listView = showMiniMenuList(*owner, editor, menuPoint, values, nullptr, outlineWidth, outlineRows, true);
	if (listView == nullptr) {
		postDialogWarning("Outline menu cannot be shown.");
		return true;
	}
	listView->setRowStyles(rowStyles);
	auto selectableOutlineRow = [&entries](short index) {
		return index >= 0 && static_cast<std::size_t>(index) < entries.size() && !entries[static_cast<std::size_t>(index)].header;
	};
	auto focusSelectableOutlineRow = [&entries, listView, &selectableOutlineRow](short requested, int direction) {
		const short lastIndex = static_cast<short>(entries.size() - 1);
		short index = static_cast<short>(std::clamp<int>(requested, 0, lastIndex));

		if (direction == 0) direction = 1;
		for (; index >= 0 && index <= lastIndex; index = static_cast<short>(index + direction))
			if (selectableOutlineRow(index)) {
				listView->focusItemNum(index);
				return;
			}
		index = static_cast<short>(std::clamp<int>(requested, 0, lastIndex));
		direction = -direction;
		for (; index >= 0 && index <= lastIndex; index = static_cast<short>(index + direction))
			if (selectableOutlineRow(index)) {
				listView->focusItemNum(index);
				return;
			}
	};
	focusSelectableOutlineRow(0, 1);
	while (!done) {
		TEvent event{};

		owner->getEvent(event);
		if (event.what == evMouseWheel) {
			const short before = listView->selectedIndex();

			if (listView->handleWheel(event)) {
				const short after = listView->selectedIndex();

				if (!selectableOutlineRow(after)) focusSelectableOutlineRow(after, after >= before ? 1 : -1);
				continue;
			}
		}
		if (event.what == evMouseDown && listView->mouseInView(event.mouse.where)) {
			selected = miniMenuClickedIndex(*listView, event.mouse.where);
			listView->handleEvent(event);
			event.what = evNothing;
			if (!selectableOutlineRow(selected)) {
				focusSelectableOutlineRow(selected, selected >= listView->selectedIndex() ? 1 : -1);
				selected = -1;
				continue;
			}
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
			selected = listView->selectedIndex();
			event.what = evNothing;
			if (!selectableOutlineRow(selected)) {
				focusSelectableOutlineRow(selected, 1);
				selected = -1;
				continue;
			}
			done = true;
			continue;
		}
		const short before = listView->selectedIndex();

		listView->handleEvent(event);
		if (event.what == evNothing) {
			const short after = listView->selectedIndex();

			if (!selectableOutlineRow(after)) focusSelectableOutlineRow(after, after >= before ? 1 : -1);
		}
	}
	destroyMiniMenuList(*owner, listView);
	if (selected < 0 || static_cast<std::size_t>(selected) >= entries.size()) return true;
	return jumpToLocalOutlineEntry(win, entries[static_cast<std::size_t>(selected)]);
}

std::vector<ContextMenuEntry> buildEditorContextMenuItems(MREditWindow *win, const EditorTextTarget *target) {
	std::vector<ContextMenuEntry> entries;
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

	if (editor == nullptr) return entries;
	entries.push_back(ContextMenuEntry{"Edit", 0, true});
	entries.push_back(ContextMenuEntry{"Outline", cmMrOtherLocalOutline, false});
	if (target != nullptr && !workspaceSearchTextAroundOffset(*editor, target->offset).empty()) {
		entries.push_back(ContextMenuEntry{"References", cmMrOtherReferences, false});
		entries.push_back(ContextMenuEntry{"Rename", cmMrOtherRename, false});
	}
	return entries;
}

std::vector<ContextMenuEntry> buildEditMiniMenuItems(MREditWindow *targetWindow) {
	std::vector<ContextMenuEntry> entries;
	const bool hasMarkedText = targetWindow != nullptr && (targetWindow->hasSelection() || targetWindow->hasBlock());

	if (hasMarkedText) entries.push_back(ContextMenuEntry{"Cut", cmMrEditCutToBuffer, false});
	if (hasMarkedText) entries.push_back(ContextMenuEntry{"Copy", cmMrEditCopyToBuffer, false});
	entries.push_back(ContextMenuEntry{"Mark all", cmMrEditMarkAll, false});
	entries.push_back(ContextMenuEntry{"Paste", cmMrEditPasteFromBuffer, false});
	return entries;
}

bool chooseMiniMenuCommand(TGroup &owner, MREditWindow *targetWindow, TPoint where, const EditorTextTarget *target, ushort &command) {
	const std::vector<ContextMenuEntry> entries = buildEditorContextMenuItems(targetWindow, target);
	const std::vector<ContextMenuEntry> editEntries = buildEditMiniMenuItems(targetWindow);
	MRFileEditor *editor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	std::vector<std::string> values;
	std::vector<std::string> editValues;
	MRColumnListView *parentList = nullptr;
	MRColumnListView *editList = nullptr;
	MRColumnListView *activeList = nullptr;
	short selected = -1;
	short editSelected = -1;
	const short menuWidth = miniMenuWidthForEntries(entries);
	bool done = false;

	command = 0;
	if (entries.empty()) return false;
	if (entries.size() == 1 && !entries.front().editSubmenu) {
		command = entries.front().command;
		return command != 0;
	}
	values.reserve(entries.size());
	for (const ContextMenuEntry &entry : entries)
		values.push_back(miniMenuDisplayText(entry, menuWidth));
	editValues.reserve(editEntries.size());
	for (const ContextMenuEntry &entry : editEntries)
		editValues.push_back(entry.title);

	parentList = showMiniMenuList(owner, editor, where, values, nullptr, menuWidth);
	activeList = parentList;
	if (parentList == nullptr) return false;
	while (!done) {
		TEvent event{};

		activeList->getEvent(event);
		if (event.what == evMouseDown && parentList != nullptr && parentList->mouseInView(event.mouse.where)) {
			selected = miniMenuClickedIndex(*parentList, event.mouse.where);
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
				if (editList == nullptr) editList = showMiniMenuList(owner, editor, editWhere, editValues);
				activeList = editList != nullptr ? editList : parentList;
				continue;
			}
			command = entries[static_cast<std::size_t>(selected)].command;
			done = true;
			continue;
		}
		if (event.what == evMouseDown && editList != nullptr && editList->mouseInView(event.mouse.where)) {
			editSelected = miniMenuClickedIndex(*editList, event.mouse.where);
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
				if (editList == nullptr) editList = showMiniMenuList(owner, editor, editWhere, editValues);
				activeList = editList != nullptr ? editList : parentList;
				continue;
			}
			command = entries[static_cast<std::size_t>(selected)].command;
			done = true;
			continue;
		}
		activeList->handleEvent(event);
	}
	destroyMiniMenuList(owner, editList);
	destroyMiniMenuList(owner, parentList);
	return command != 0;
}

bool showEditorContextMenuForWindow(MREditWindow *targetWindow, TPoint where) {
	ushort command = 0;
	TGroup *owner = targetWindow != nullptr ? static_cast<TGroup *>(targetWindow) : TProgram::deskTop;
	EditorTextTarget target;

	if (owner == nullptr) return false;
	if (targetWindow != nullptr) static_cast<void>(activateEditorTargetWindow(targetWindow));
	if (!editorTextTargetFromGlobalPoint(targetWindow, where, target)) return true;
	if (!chooseMiniMenuCommand(*owner, targetWindow, where, &target, command)) return true;
	switch (command) {
		case cmMrOtherReferences:
			return requestWorkspaceReferencesCommand(targetWindow, &target);
		case cmMrOtherRename:
			return requestWorkspaceRenameCommand(targetWindow, &target);
		case cmMrOtherLocalOutline:
			return showLocalOutlineForWindow(targetWindow, &where);
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
	MRRouterIntegerInputLayout layout;
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
	if (!promptRouterIntegerValue("GOTO LINE NUMBER", "", "Enter the target line number.", static_cast<int>(std::max<long>(kMinLineNumber, std::min<long>(lineNumber > 0 ? lineNumber : 1, kMaxLineNumber))), kMinLineNumber, kMaxLineNumber, value, layout)) return false;
	lineNumber = value;
	return true;
}

bool handleSearchGotoLineNumber() {
	MREditWindow *win = currentEditWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	long lineNumber = 0;

	if (editor == nullptr) return true;
	if (!promptGotoLineNumber(lineNumber)) return true;
	editor->requestDocumentLineNavigation(static_cast<std::size_t>(lineNumber - 1));
	return true;
}

bool handleFileOpen() {
	enum {
		FileNameBufferSize = MAXPATH
	};
	char fileName[FileNameBufferSize];
	std::string resolvedPath;

	if (!promptForPath("OPEN FILE", fileName, sizeof(fileName))) return true;
	if (!resolveReadableExistingPath(MRDialogHistoryScope::OpenFile, fileName, resolvedPath)) {
		forgetLoadDialogPath(MRDialogHistoryScope::OpenFile, fileName);
		return true;
	}
	if (!openResolvedFilesIntoWindows(std::vector<std::string>{resolvedPath})) forgetLoadDialogPath(MRDialogHistoryScope::OpenFile, resolvedPath.c_str());
	return true;
}

bool handleFileLoad() {
	enum {
		FileNameBufferSize = MAXPATH
	};
	char fileName[FileNameBufferSize];
	std::string resolvedPath;

	if (!promptForPath("LOAD FILE", fileName, sizeof(fileName))) return true;
	if (!resolveReadableExistingPath(MRDialogHistoryScope::LoadFile, fileName, resolvedPath)) {
		forgetLoadDialogPath(MRDialogHistoryScope::LoadFile, fileName);
		return true;
	}
	if (!loadResolvedFilesIntoWindows(std::vector<std::string>{resolvedPath})) forgetLoadDialogPath(MRDialogHistoryScope::LoadFile, resolvedPath.c_str());
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
	static_cast<void>(bentoBox->requestCompilerProblemNavigation(forward));
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
	if (window->getEditor() != nullptr) source.snapshot = window->getEditor()->readSnapshot();
	return source;
}

std::string fileCompareWindowTitle(const MRBentoCompareSetup &setup) {
	std::string title = "Compare: " + setup.original.title + " / " + setup.compare.title;

	if (title.size() > 72) title = title.substr(0, 69) + "...";
	return title;
}

bool handleTextFileCompare() {
	MREditWindow *originalWindow = currentEditWindow();
	MREditWindow *compareWindow;
	MRBentoBox *compareBento;
	MRBentoCompareSetup setup;
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
	if (!compareBento->initializeFileCompare(std::move(setup))) {
		message(compareBento, evCommand, cmClose, nullptr);
		postDialogWarning("Unable to initialize file compare BentoBox.");
		return true;
	}

	if (!compareBento->startFileCompareProjection()) {
		message(compareBento, evCommand, cmClose, nullptr);
		postDialogWarning("Unable to start file compare pipeline.");
		return true;
	}
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
	MRBuildHookContext buildContext;
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
	buildContext = buildCompilerProfileHookContext(compilerProfile, sourcePath, win->bufferId());
	if (!runBuildHookMacro(compilerProfile.preBuildMacro, buildContext, 0, "PRE_BUILD", std::string(), &errorText)) {
		std::string postError;

		static_cast<void>(runBuildHookMacro(compilerProfile.postBuildMacro, buildContext, -1, "FAILED", errorText.empty() ? "Pre build macro failed." : errorText, &postError));
		postDialogWarning(errorText.empty() ? "Pre build macro failed." : errorText);
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
	buildContext.sourceBufferId = bentoBox->bufferId();
	bentoBox->clearCompilerDiagnostics();
	startExternalCommandInWindow(outputWindow, commandLine, true, false, false, outputTitle, compilerProfile.buildSuccessAudioUri, compilerProfile.buildFailureAudioUri, buildContext);
	bentoBox->activatePrimaryPane();
	return true;
}

bool startExternalCommandInWindow(MREditWindow *win, const std::string &commandLine, bool replaceBuffer, bool activate, bool closeOnFailure, std::string_view titleOverride, const std::string &successAudioUri, const std::string &failureAudioUri, const MRBuildHookContext &buildContext) {
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

	taskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Io, mr::coprocessor::TaskKind::ExternalIo, static_cast<std::size_t>(win->bufferId()), 0, mr::coprocessor::ExecutionOwnerKind::ProcessChannel, static_cast<std::size_t>(win->bufferId()), std::string("external-io: ") + commandLine, [commandLine, channelId = static_cast<std::size_t>(win->bufferId()), successAudioUri, failureAudioUri, buildContext](const mr::coprocessor::TaskInfo &info) { return runExternalCommandTask(info, channelId, commandLine, buildContext, successAudioUri, failureAudioUri); });
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
	const bool trackFileCompareMutation = editorCommand == cmUndo || editorCommand == cmMrEditUndo || editorCommand == cmMrEditRedo || editorCommand == cmMrTextPrettifyBlockOrFile;
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

bool handleForceSave(MREditWindow *window) {
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

bool handleExitDirtySaveAll() {
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
	MRBentoBox *diagnosticsBento = win != nullptr ? dynamic_cast<MRBentoBox *>(win->owner) : nullptr;
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
	if (diagnosticsBento != nullptr && diagnosticsBento->buildOutputPane() == win) diagnosticsBento->clearCompilerDiagnostics();
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
	if (MRBentoBox *split = dynamic_cast<MRBentoBox *>(win->owner); split != nullptr && split->buildOutputPane() == win) {
		split->clearCompilerDiagnostics();
		static_cast<void>(split->refreshCompilerDiagnosticsFromOutput());
	}
	setSplitDiagnosticsStatusForOutput(win, "idle");
	line << "Cleared communication window #" << win->bufferId() << ".";
	mrLogMessage(line.str().c_str());
	return true;
}

bool handleCompilerProblemsNavigation(MREditWindow *targetWindow, bool forward) {
	MRBentoBox *bentoBox = compilerProblemsBentoBoxForWindow(targetWindow);

	if (bentoBox == nullptr)
		postDialogWarning("No compiler problems available.");
	else
		static_cast<void>(bentoBox->requestCompilerProblemNavigation(forward));
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

bool showMREditorContextMenu(MREditWindow *targetWindow, TPoint where) {
	return showEditorContextMenuForWindow(targetWindow, where);
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
					return handleCenterLine(window);
				case KeymapCustomAction::ReformatParagraph:
					return handleReformatParagraph(window);
				case KeymapCustomAction::ReformatDocument:
					return handleReformatDocument(window);
				case KeymapCustomAction::ToggleFormatRuler:
					return handleToggleFormatRuler();
				case KeymapCustomAction::ToggleWordWrap:
					return handleToggleWordWrap();
				case KeymapCustomAction::SetLeftMargin:
					return handleSetLeftMargin(window);
				case KeymapCustomAction::SetRightMargin:
					return handleSetRightMargin();
				case KeymapCustomAction::JustifyParagraph:
					return handleJustifyParagraph(window);
				case KeymapCustomAction::SortColumnBlockToggle:
					return runDisabledBlockAction();
				case KeymapCustomAction::ForceSave:
					return handleForceSave(window);
				case KeymapCustomAction::ExitDirtySaveAll:
					return handleExitDirtySaveAll();
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
					postDialogWarning("No active snippet sidekick.");
					return true;
				case KeymapCustomAction::SnippetPlaceholderPrevious:
					if (mrMoveSnippetPlaceholderForParent(window, -1)) return true;
					postDialogWarning("No active snippet sidekick.");
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
			MREditWindow *win = currentEditorCommandWindow();
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
			static_cast<void>(closeCurrentDesktopWindow());
			return true;

		case cmMrWindowList: {
			const auto startedAt = std::chrono::steady_clock::now();
			MREditWindow *selected = mrShowWindowListDialog(mrwlManageWindows, currentEditWindow());
			if (selected != nullptr) mrScheduleWindowActivation(selected);
			{
				const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
				mrLogMessage("Window List command timing took_us=" + std::to_string(tookUs));
			}
			return true;
		}

		case cmMrWindowNext:
			static_cast<void>(activateRelativeDesktopWindow(1));
			return true;

		case cmMrWindowPrevious:
			static_cast<void>(activateRelativeDesktopWindow(-1));
			return true;

		case cmMrWindowHide:
			static_cast<void>(hideCurrentDesktopWindow());
			return true;

		case cmMrWindowZoom:
			static_cast<void>(zoomCurrentDesktopWindow());
			return true;

		case cmMrWindowMinimize:
			if (currentDesktopWindow() != nullptr) {
				const auto startedAt = std::chrono::steady_clock::now();
				MRDesktopWindow *window = currentDesktopWindow();
				const bool restore = window->desktopMinimized();
				MRWindowLayout::toggleMinimizedWindow(window);
				{
					const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
					mrLogMessage(std::string("Window minimize command timing took_us=") + std::to_string(tookUs) + " restore=" + (restore ? "1" : "0"));
				}
			}
			return true;

		case cmMrWindowRestore:
			if (currentDesktopWindow() != nullptr && currentDesktopWindow()->desktopMinimized()) currentDesktopWindow()->restoreDesktopWindow();
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

		case cmMrTextHexEditor: {
			MREditWindow *window = currentEditWindow();

			if (window == nullptr || convertEditWindowToHexEditor(window) == nullptr) postDialogWarning("Hex editor requires an editable document window without running external I/O.");
			return true;
		}

		case cmMrDeferredWindowClose:
			return mrDispatchDeferredWindowClose(static_cast<MREditWindow *>(commandInfo));

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
		case cmMrSetupFilenameExtensions:
		case cmMrSetupCompilerProfiles:
		case cmMrSetupPaths:
		case cmMrSetupBackupsAutosave:
		case cmMrSetupUserInterfaceSettings:
		case cmMrSetupLiveLogs:
		case cmMrSetupSearchAndReplaceDefaults: {
			if (runSetupDialogCommand(command)) {
				mrRefreshEditorApplicationUiSettingsSnapshot();
				mrRefreshAllHexEditorProjections();
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

		case cmMrTextPrettifyBlockOrFile:
			return dispatchEditorCommand(cmMrTextPrettifyBlockOrFile, true);

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

		case cmMrOtherMacroLibrary:
			return runMacroLibraryDialog();

		case cmMrOtherFindNextCompilerError:
			return handleCompilerErrorNavigation(commandInfo, true);

		case cmMrOtherFindPreviousCompilerError:
			return handleCompilerErrorNavigation(commandInfo, false);

		case cmMrOtherReferences:
			return requestWorkspaceReferencesCommand();

		case cmMrOtherRename:
			return requestWorkspaceRenameCommand();

		case cmMrOtherLocalOutline:
			return showLocalOutlineForWindow(currentEditorCommandWindow());

		case cmMrOtherMatchBraceOrParen:
			return handleMatchParenthesis();

		default:
			return false;
	}
}

void clearTransientSearchSelectionOnUserInput(const TEvent &event) {
	clearTransientSelectionIfPending(event);
}
