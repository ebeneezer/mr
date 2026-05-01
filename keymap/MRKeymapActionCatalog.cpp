#include "MRKeymapActionCatalog.hpp"

#include <algorithm>
#include <array>

namespace {
constexpr MRKeymapActionDefinition mrmac(std::string_view id, std::string_view label) noexcept {
	return {id, MRKeymapActionOrigin::MRMAC, label, label};
}

constexpr MRKeymapActionDefinition mr(std::string_view id, std::string_view label) noexcept {
	return {id, MRKeymapActionOrigin::MR, label, label};
}

constexpr std::array catalogDefinitions{
    mrmac("MRMAC_CURSOR_LEFT", "Cursor left"),
    mrmac("MRMAC_CURSOR_RIGHT", "Cursor right"),
    mrmac("MRMAC_CURSOR_UP", "Cursor up"),
    mrmac("MRMAC_CURSOR_DOWN", "Cursor down"),
    mrmac("MRMAC_CURSOR_HOME", "Cursor to home"),
    mrmac("MRMAC_CURSOR_END_OF_LINE", "Cursor to end of line"),
    mrmac("MRMAC_VIEW_PAGE_UP", "Display page up"),
    mrmac("MRMAC_VIEW_PAGE_DOWN", "Display page down"),
    mrmac("MRMAC_CURSOR_TOP_OF_FILE", "To top of the file"),
    mrmac("MRMAC_CURSOR_BOTTOM_OF_FILE", "To bottom of the file"),
    mrmac("MRMAC_CURSOR_NEXT_PAGE_BREAK", "Cursor to next page break"),
    mrmac("MRMAC_CURSOR_PREV_PAGE_BREAK", "Cursor to last page break"),
    mrmac("MRMAC_CURSOR_WORD_LEFT", "Cursor word left"),
    mrmac("MRMAC_CURSOR_WORD_RIGHT", "Cursor word right"),
    mrmac("MRMAC_CURSOR_TOP_OF_WINDOW", "Top of window"),
    mrmac("MRMAC_CURSOR_BOTTOM_OF_WINDOW", "Bottom of window"),
    mrmac("MRMAC_VIEW_SCROLL_UP", "Scroll window up"),
    mrmac("MRMAC_VIEW_SCROLL_DOWN", "Scroll window down"),
    mrmac("MRMAC_CURSOR_START_OF_BLOCK", "Cursor to start of block"),
    mrmac("MRMAC_CURSOR_END_OF_BLOCK", "Cursor to end of block"),
    mrmac("MRMAC_CURSOR_GOTO_LINE", "Move cursor to line num"),
    mrmac("MRMAC_CURSOR_INDENT", "Indent"),
    mrmac("MRMAC_CURSOR_TAB_RIGHT", "Tab right"),
    mrmac("MRMAC_CURSOR_TAB_LEFT", "Tab left"),
    mrmac("MRMAC_CURSOR_UNDENT", "Undent"),
    mrmac("MRMAC_MARK_PUSH_POSITION", "Mark position on stack"),
    mrmac("MRMAC_MARK_POP_POSITION", "Get position from stack"),
    mrmac("MRMAC_MARK_SET_RANDOM_ACCESS", "Set a random access mark"),
    mrmac("MRMAC_VIEW_CENTER_LINE", "Center line on screen"),
    mrmac("MRMAC_MARK_GET_RANDOM_ACCESS", "Get random access mark"),
    mrmac("MRMAC_DELETE_TO_EOL", "Delete to end of line"),
    mrmac("MRMAC_DELETE_FORWARD_CHAR_OR_BLOCK", "Del character (or block)"),
    mrmac("MRMAC_DELETE_FORWARD_WORD", "Delete word forward"),
    mrmac("MRMAC_DELETE_BACKWARD_CHAR", "Back space"),
    mrmac("MRMAC_DELETE_BACKWARD_WORD", "Backspace a whole word"),
    mrmac("MRMAC_DELETE_LINE", "Delete line"),
    mrmac("MRMAC_DELETE_BACKWARD_TO_HOME", "Backspace to home"),
    mrmac("MRMAC_UNDO", "Undo"),
    mrmac("MRMAC_REDO_LAST_UNDO", "Undo your last undo"),
    mrmac("MRMAC_SEARCH_FORWARD", "Search"),
    mrmac("MRMAC_SEARCH_REPLACE", "Search and replace"),
    mrmac("MRMAC_SEARCH_REPEAT_LAST", "Repeat last Search/Replace"),
    mrmac("MRMAC_SEARCH_MULTI_FILE", "Multi file search"),
    mrmac("MRMAC_SEARCH_LIST_MATCHED_FILES", "List matched files"),
    mrmac("MRMAC_BLOCK_COPY_TO_CLIPBOARD", "Copy to MS Windows"),
    mrmac("MRMAC_BLOCK_PASTE_FROM_CLIPBOARD", "Paste from MS Windows"),
    mrmac("MRMAC_BLOCK_MARK_STREAM", "Mark a stream block"),
    mrmac("MRMAC_BLOCK_SET_BEGIN", "Set block begin"),
    mrmac("MRMAC_BLOCK_SET_END", "Set block end"),
    mrmac("MRMAC_BLOCK_SET_COLUMN_BEGIN", "Set column block begin"),
    mrmac("MRMAC_BLOCK_MARK_WORD_RIGHT", "Mark word right"),
    mrmac("MRMAC_BLOCK_CLEAR", "Turn block mark off"),
    mrmac("MRMAC_BLOCK_UNDENT", "Undent block"),
    mrmac("MRMAC_BLOCK_INDENT", "Indent block"),
    mrmac("MRMAC_BLOCK_COPY", "Copy the marked block"),
    mrmac("MRMAC_BLOCK_MOVE", "Move marked block"),
    mrmac("MRMAC_BLOCK_DELETE", "Delete marked block"),
    mrmac("MRMAC_BLOCK_COPY_INTERWINDOW", "Interwindow block copy"),
    mrmac("MRMAC_BLOCK_MOVE_INTERWINDOW", "Interwindow block move"),
    mrmac("MRMAC_BLOCK_MOVE_TO_BUFFER", "Move block to buffer"),
    mrmac("MRMAC_BLOCK_APPEND_TO_BUFFER", "Append block to buffer"),
    mrmac("MRMAC_BLOCK_CUT_APPEND_TO_BUFFER", "Cut and Append Block"),
    mrmac("MRMAC_BLOCK_COPY_FROM_BUFFER", "Copy block from buffer"),
    mrmac("MRMAC_BLOCK_MATH", "Perform math on block"),
    mrmac("MRMAC_BLOCK_EXTEND_BY_MOTION", "Shift cursor block mark"),
    mrmac("MRMAC_FILE_SAVE", "Save file"),
    mr("MR_SAVE_BLOCK_TO_FILE", "Save block to file"),
    mr("MR_LOAD_BLOCK_FROM_FILE", "Load block from file"),
    mr("MR_TEXT_CENTER_LINE", "Center current line"),
    mr("MR_TEXT_REFORMAT_PARAGRAPH", "Reformat paragraph"),
    mr("MR_TEXT_REFORMAT_DOCUMENT", "Reformat document"),
    mr("MR_TOGGLE_FORMAT_RULER", "Toggle format ruler"),
    mr("MR_TOGGLE_WORD_WRAP", "Toggle word wrap"),
    mr("MR_SET_LEFT_MARGIN", "Set left margin"),
    mr("MR_SET_RIGHT_MARGIN", "Set right margin"),
    mr("MR_JUSTIFY_PARAGRAPH", "Justify paragraph"),
    mr("MR_SORT_COLUMN_BLOCK_TOGGLE", "Sort marked column block"),
    mr("MR_FILE_FORCE_SAVE", "Force save"),
    mr("MR_EXIT_DIRTY_SAVE_ALL", "Exit with save-all dialog"),
    mr("MR_SEARCH_RESULTS_NEXT", "Next search result"),
};
} // namespace

const MRKeymapActionDefinition *MRKeymapActionCatalog::findById(std::string_view id) noexcept {
	for (const MRKeymapActionDefinition &definition : catalogDefinitions)
		if (definition.id == id) return &definition;
	return nullptr;
}

bool MRKeymapActionCatalog::contains(std::string_view id) noexcept {
	return findById(id) != nullptr;
}

std::span<const MRKeymapActionDefinition> MRKeymapActionCatalog::definitions() noexcept {
	return catalogDefinitions;
}
