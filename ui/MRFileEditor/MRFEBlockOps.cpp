#define Uses_TKeys
#define Uses_TGroup
#define Uses_TEvent
#include <tvision/tv.h>

#include "MRFEBlockOps.hpp"
#include "MRFileEditor.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

bool mrfeSeedMouseColumnStateForRegression(MRFileEditor &editor, int anchorColumn, int cursorColumn);

namespace {

struct MarkerInput {
	std::string text;
	std::vector<std::size_t> lineStarts;
	std::size_t anchor = 0;
	std::size_t cursor = 0;
	int anchorColumn = 0;
	int cursorColumn = 0;
};

std::vector<std::size_t> lineStartsForText(const std::string &text) {
	std::vector<std::size_t> starts;
	starts.push_back(0);
	for (std::size_t i = 0; i < text.size(); ++i)
		if (text[i] == '\n') starts.push_back(i + 1);
	return starts;
}

std::size_t lineIndexForOffset(const std::vector<std::size_t> &starts, std::size_t offset) {
	std::size_t line = 0;
	for (std::size_t i = 0; i < starts.size(); ++i) {
		if (starts[i] <= offset) line = i;
		else
			break;
	}
	return line;
}

std::size_t nextLineForIndex(const MarkerInput &input, std::size_t lineIndex) {
	if (lineIndex + 1 < input.lineStarts.size()) return input.lineStarts[lineIndex + 1];
	return input.text.size();
}

class BlockMarker {
  public:
	virtual ~BlockMarker() = default;
	virtual void normalize(const MarkerInput &input, MRFEBlockGeometry &geometry) const = 0;
};

class StreamBlockMarker final : public BlockMarker {
  public:
	void normalize(const MarkerInput &input, MRFEBlockGeometry &geometry) const override {
		geometry.rangeStart = std::min(input.anchor, input.cursor);
		geometry.rangeEnd = std::max(input.anchor, input.cursor);
		geometry.line1 = lineIndexForOffset(input.lineStarts, geometry.rangeStart);
		geometry.line2 = lineIndexForOffset(input.lineStarts, geometry.rangeEnd);
		geometry.col1 = std::min(input.anchorColumn, input.cursorColumn);
		geometry.col2 = std::max(input.anchorColumn, input.cursorColumn);
	}
};

class LineBlockMarker final : public BlockMarker {
  public:
	void normalize(const MarkerInput &input, MRFEBlockGeometry &geometry) const override {
		std::size_t anchorLine = lineIndexForOffset(input.lineStarts, std::min(input.anchor, input.text.size()));
		std::size_t cursorLine = lineIndexForOffset(input.lineStarts, std::min(input.cursor, input.text.size()));
		geometry.line1 = std::min(anchorLine, cursorLine);
		geometry.line2 = std::max(anchorLine, cursorLine);
		geometry.rangeStart = input.lineStarts[geometry.line1];
		geometry.rangeEnd = nextLineForIndex(input, geometry.line2);
		geometry.col1 = 0;
		geometry.col2 = 0;
	}
};

class ColumnBlockMarker final : public BlockMarker {
  public:
	void normalize(const MarkerInput &input, MRFEBlockGeometry &geometry) const override {
		std::size_t anchorLine = lineIndexForOffset(input.lineStarts, std::min(input.anchor, input.text.size()));
		std::size_t cursorLine = lineIndexForOffset(input.lineStarts, std::min(input.cursor, input.text.size()));
		geometry.line1 = std::min(anchorLine, cursorLine);
		geometry.line2 = std::max(anchorLine, cursorLine);
		geometry.rangeStart = input.lineStarts[geometry.line1];
		geometry.rangeEnd = nextLineForIndex(input, geometry.line2);
		geometry.col1 = std::min(input.anchorColumn, input.cursorColumn);
		geometry.col2 = std::max(input.anchorColumn, input.cursorColumn);
	}
};

std::unique_ptr<BlockMarker> markerForMode(MRFEBlockMode mode) {
	if (mode == MRFEBlockMode::Line) return std::make_unique<LineBlockMarker>();
	if (mode == MRFEBlockMode::Column) return std::make_unique<ColumnBlockMarker>();
	if (mode == MRFEBlockMode::Stream) return std::make_unique<StreamBlockMarker>();
	return nullptr;
}

bool checkGeometry(const MRFEBlockGeometry &geometry, MRFEBlockMode mode, std::size_t start, std::size_t end, std::size_t line1, std::size_t line2, int col1, int col2, const char *phase, std::string &failureReason) {
	if (geometry.mode != mode || geometry.rangeStart != start || geometry.rangeEnd != end || geometry.line1 != line1 || geometry.line2 != line2 || geometry.col1 != col1 || geometry.col2 != col2) {
		failureReason = std::string("Block marker geometry mismatch in ") + phase + ".";
		return false;
	}
	return true;
}

MRFEBlockGeometry harnessNormalize(MRFEBlockMode mode, const std::string &text, std::size_t anchor, std::size_t cursor, int anchorColumn, int cursorColumn) {
	MRFEBlockGeometry geometry;
	MarkerInput input;
	input.text = text;
	input.lineStarts = lineStartsForText(text);
	input.anchor = std::min(anchor, text.size());
	input.cursor = std::min(cursor, text.size());
	input.anchorColumn = std::max(anchorColumn, 0);
	input.cursorColumn = std::max(cursorColumn, 0);
	geometry.mode = mode;
	geometry.status = MRFEBlockStatus::Committed;
	geometry.anchor = input.anchor;
	geometry.cursor = input.cursor;
	geometry.anchorColumn = input.anchorColumn;
	geometry.cursorColumn = input.cursorColumn;
	std::unique_ptr<BlockMarker> marker = markerForMode(mode);
	if (marker != nullptr) marker->normalize(input, geometry);
	return geometry;
}

class ScopedCursorBehaviour {
  public:
	explicit ScopedCursorBehaviour(MRCursorBehaviour behaviour) : mPrevious(configuredCursorBehaviour()) {
		static_cast<void>(setConfiguredCursorBehaviour(behaviour));
	}

	~ScopedCursorBehaviour() {
		static_cast<void>(setConfiguredCursorBehaviour(mPrevious));
	}

  private:
	MRCursorBehaviour mPrevious;
};

std::size_t lineContentEndForIndex(const std::string &text, const std::vector<std::size_t> &starts, std::size_t lineIndex) {
	if (lineIndex + 1 < starts.size()) {
		std::size_t end = starts[lineIndex + 1];
		if (end > starts[lineIndex] && text[end - 1] == '\n') --end;
		if (end > starts[lineIndex] && text[end - 1] == '\r') --end;
		return end;
	}
	return text.size();
}

std::size_t offsetAtLineVisualColumn(const std::string &text, const std::vector<std::size_t> &starts, std::size_t lineIndex, int visualColumn) {
	if (starts.empty()) return 0;
	lineIndex = std::min(lineIndex, starts.size() - 1);
	const std::size_t start = starts[lineIndex];
	const std::size_t end = lineContentEndForIndex(text, starts, lineIndex);
	const std::size_t column = static_cast<std::size_t>(std::max(visualColumn, 0));
	return std::min(end, start + column);
}

void placeEditorCursor(MRFileEditor &editor, const std::string &text, const std::vector<std::size_t> &starts, std::size_t lineIndex, int visualColumn) {
	editor.setCursorOffsetAtVisualColumn(offsetAtLineVisualColumn(text, starts, lineIndex, visualColumn), std::max(visualColumn, 0));
}

std::size_t overlayVisualLine2(MRFileEditor &editor, MRFileEditor::BlockOverlayState overlay) {
	std::size_t start = overlay.anchor;
	std::size_t end = overlay.trackCursor ? editor.cursorOffset() : overlay.end;
	if (start > end) std::swap(start, end);
	return editor.lineIndexOfOffset(end);
}

bool checkEditorBlock(MRFileEditor &editor, const MRFEBlockOps &ops, MRFEBlockMode mode, MRFEBlockStatus status, std::size_t line1, std::size_t line2, int col1, int col2, const char *phase, std::string &failureReason) {
	const MRFEBlockGeometry &geometry = ops.geometry();
	const MRFileEditor::BlockOverlayState overlay = editor.blockOverlayState();
	const std::size_t overlayLine1 = editor.lineIndexOfOffset(overlay.anchor);
	const std::size_t overlayLine2 = overlayVisualLine2(editor, overlay);
	const int expectedMode = static_cast<int>(mode);

	if (geometry.mode != mode || geometry.status != status || geometry.line1 != line1 || geometry.line2 != line2 || geometry.col1 != col1 || geometry.col2 != col2) {
		failureReason = std::string("Editor block geometry mismatch in ") + phase + ": got mode=" + std::to_string(static_cast<int>(geometry.mode)) + " status=" +
		                std::to_string(static_cast<int>(geometry.status)) + " line1=" + std::to_string(geometry.line1) + " line2=" + std::to_string(geometry.line2) + " col1=" +
		                std::to_string(geometry.col1) + " col2=" + std::to_string(geometry.col2) + ".";
		return false;
	}
	if (!overlay.active || overlay.mode != expectedMode) {
		failureReason = std::string("Editor block overlay inactive or wrong mode in ") + phase + ".";
		return false;
	}
	if (status == MRFEBlockStatus::Marking) {
		if (editor.selectionStartOffset() != geometry.rangeStart || editor.selectionEndOffset() != geometry.rangeEnd) {
			failureReason = std::string("Editor live selection range mismatch in ") + phase + ".";
			return false;
		}
	} else {
		if (editor.hasTextSelection()) {
			failureReason = std::string("Committed editor block must not leave a text selection active in ") + phase + ".";
			return false;
		}
	}
	if (overlayLine1 != line1 || overlayLine2 != line2) {
		failureReason = std::string("Editor overlay line range mismatch in ") + phase + ".";
		return false;
	}
	if (mode == MRFEBlockMode::Column && (overlay.columnAnchor != col1 || overlay.columnEnd != col2)) {
		failureReason = std::string("Editor column overlay range mismatch in ") + phase + ".";
		return false;
	}
	return true;
}

bool runEditorMarkingCase(MRFEBlockMode mode, const std::string &text, std::size_t anchorLine, int anchorColumn, std::size_t cursorLine, int cursorColumn, std::size_t expectedLine1, std::size_t expectedLine2, int expectedCol1, int expectedCol2, const char *phase, std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;
	const std::vector<std::size_t> starts = lineStartsForText(text);

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, anchorLine, anchorColumn);
	if (mode == MRFEBlockMode::Line) {
		if (!ops.beginLine(editor)) {
			failureReason = std::string("Unable to begin line block in ") + phase + ".";
			return false;
		}
	} else if (mode == MRFEBlockMode::Column) {
		if (!ops.beginColumn(editor)) {
			failureReason = std::string("Unable to begin column block in ") + phase + ".";
			return false;
		}
	} else if (mode == MRFEBlockMode::Stream) {
		if (!ops.beginStream(editor)) {
			failureReason = std::string("Unable to begin stream block in ") + phase + ".";
			return false;
		}
	} else {
		failureReason = std::string("Invalid block mode in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, cursorLine, cursorColumn);
	if (!ops.updateFromEditor(editor) || !ops.end(editor)) {
		failureReason = std::string("Unable to update/end block in ") + phase + ".";
		return false;
	}
	return checkEditorBlock(editor, ops, mode, MRFEBlockStatus::Committed, expectedLine1, expectedLine2, expectedCol1, expectedCol2, phase, failureReason);
}

bool seedMouseColumnState(MRFileEditor &editor, int anchorColumn, int cursorColumn) {
	return mrfeSeedMouseColumnStateForRegression(editor, anchorColumn, cursorColumn);
}

void seedEditorSelection(MRFileEditor &editor, const std::string &text, const std::vector<std::size_t> &starts, std::size_t anchorLine, int anchorColumn, std::size_t cursorLine, int cursorColumn) {
	const std::size_t anchor = offsetAtLineVisualColumn(text, starts, anchorLine, anchorColumn);
	const std::size_t cursor = offsetAtLineVisualColumn(text, starts, cursorLine, cursorColumn);
	editor.setCursorOffsetAtVisualColumn(cursor, std::max(cursorColumn, 0));
	editor.setSelectionOffsets(anchor, cursor, False);
}

bool runMenuColumnIgnoresStaleMouseStateCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = "alpha\n\nbeta\nomega";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = "Unable to seed editor text in menu column stale mouse case.";
		return false;
	}
	if (!seedMouseColumnState(editor, 30, 30)) {
		failureReason = "Unable to seed stale mouse column state.";
		return false;
	}
	placeEditorCursor(editor, text, starts, 0, 1);
	if (!ops.beginColumn(editor)) {
		failureReason = "Unable to begin column block in menu column stale mouse case.";
		return false;
	}
	placeEditorCursor(editor, text, starts, 2, 4);
	if (!ops.updateFromEditor(editor) || !ops.end(editor)) {
		failureReason = "Unable to update/end column block in menu column stale mouse case.";
		return false;
	}
	return checkEditorBlock(editor, ops, MRFEBlockMode::Column, MRFEBlockStatus::Committed, 0, 2, 1, 4, "menu column ignores stale mouse state", failureReason);
}

bool beginBlockForMode(MRFEBlockOps &ops, MRFileEditor &editor, MRFEBlockMode mode) {
	if (mode == MRFEBlockMode::Line) return ops.beginLine(editor);
	if (mode == MRFEBlockMode::Column) return ops.beginColumn(editor);
	if (mode == MRFEBlockMode::Stream) return ops.beginStream(editor);
	return false;
}

bool runEditorToggleCase(MRFEBlockMode mode, const char *phase, std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = "alpha\nbeta\n";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 8), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, 0, 1);
	if (!beginBlockForMode(ops, editor, mode)) {
		failureReason = std::string("Unable to begin block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, mode == MRFEBlockMode::Line ? 1 : 0, 4);
	if (!ops.end(editor)) {
		failureReason = std::string("Unable to end block in ") + phase + ".";
		return false;
	}
	if (!ops.toggleVisibility(editor)) {
		failureReason = std::string("Unable to hide block in ") + phase + ".";
		return false;
	}
	if (ops.hasVisibleBlock() || !ops.hasStoredBlock() || editor.blockOverlayState().active) {
		failureReason = std::string("Hidden block must be stored but not visible in ") + phase + ".";
		return false;
	}
	if (!ops.toggleVisibility(editor)) {
		failureReason = std::string("Unable to show block in ") + phase + ".";
		return false;
	}
	if (mode == MRFEBlockMode::Line) return checkEditorBlock(editor, ops, mode, MRFEBlockStatus::Committed, 0, 1, 0, 0, phase, failureReason);
	return checkEditorBlock(editor, ops, mode, MRFEBlockStatus::Committed, 0, 0, 1, 4, phase, failureReason);
}

bool runEditorReplaceCase(MRFEBlockMode oldMode, MRFEBlockMode newMode, const char *phase, std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = "alpha\n\nbeta\nomega";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, 0, 1);
	if (!beginBlockForMode(ops, editor, oldMode)) {
		failureReason = std::string("Unable to begin old block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, 2, 3);
	if (!ops.end(editor)) {
		failureReason = std::string("Unable to end old block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, 3, 2);
	if (!beginBlockForMode(ops, editor, newMode)) {
		failureReason = std::string("Unable to begin replacement block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, 3, 5);
	if (!ops.end(editor)) {
		failureReason = std::string("Unable to end replacement block in ") + phase + ".";
		return false;
	}
	if (newMode == MRFEBlockMode::Line) return checkEditorBlock(editor, ops, newMode, MRFEBlockStatus::Committed, 3, 3, 0, 0, phase, failureReason);
	return checkEditorBlock(editor, ops, newMode, MRFEBlockStatus::Committed, 3, 3, 2, 5, phase, failureReason);
}

bool runEditorMouseAdoptionCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = "alpha\n\nbeta\n";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 8), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = "Unable to seed editor text in mouse adoption case.";
		return false;
	}
	seedEditorSelection(editor, text, starts, 0, 1, 2, 4);
	if (!ops.adoptMouseSelection(editor, kbCtrlShift)) {
		failureReason = "Unable to adopt ctrl mouse selection as stream block.";
		return false;
	}
	if (!checkEditorBlock(editor, ops, MRFEBlockMode::Stream, MRFEBlockStatus::Committed, 0, 2, 1, 4, "ctrl mouse stream adoption over empty line", failureReason)) return false;

	ops.clear(editor);
	seedEditorSelection(editor, text, starts, 0, 1, 2, 4);
	if (!ops.adoptMouseSelection(editor, kbShift)) {
		failureReason = "Unable to adopt shift mouse selection as fallback stream block.";
		return false;
	}
	if (!checkEditorBlock(editor, ops, MRFEBlockMode::Stream, MRFEBlockStatus::Committed, 0, 2, 1, 4, "shift fallback mouse stream adoption over empty line", failureReason)) return false;

	ops.clear(editor);
	seedEditorSelection(editor, text, starts, 0, 1, 2, 4);
	if (!seedMouseColumnState(editor, 1, 4)) {
		failureReason = "Unable to seed mouse column state for alt adoption.";
		return false;
	}
	if (!ops.adoptMouseSelection(editor, kbAltShift)) {
		failureReason = "Unable to adopt alt mouse selection as column block.";
		return false;
	}
	if (ops.geometry().mode != MRFEBlockMode::Column) {
		failureReason = "Alt mouse selection must adopt a column block.";
		return false;
	}
	if (!checkEditorBlock(editor, ops, MRFEBlockMode::Column, MRFEBlockStatus::Committed, 0, 2, 1, 4, "alt mouse column adoption over empty line", failureReason)) return false;

	ops.clear(editor);
	seedEditorSelection(editor, text, starts, 2, 3, 0, 1);
	if (!ops.adoptMouseSelection(editor, static_cast<unsigned short>(kbCtrlShift | kbAltShift))) {
		failureReason = "Unable to adopt ctrl-alt mouse selection as line block.";
		return false;
	}
	return checkEditorBlock(editor, ops, MRFEBlockMode::Line, MRFEBlockStatus::Committed, 0, 2, 0, 0, "ctrl-alt mouse line adoption over empty line", failureReason);
}

TEvent makeMouseEvent(ushort what, int x, int y, ushort modifiers, uchar buttons) {
	TEvent event{};
	event.what = what;
	event.mouse.where = TPoint(x, y);
	event.mouse.controlKeyState = modifiers;
	event.mouse.buttons = buttons;
	return event;
}

class QueuedMouseOwner final : public TGroup {
  public:
	explicit QueuedMouseOwner(const TRect &bounds) : TGroup(bounds), mEvents(), mNextEvent(0) {
	}

	void queueMouseEvent(TEvent event) {
		mEvents.push_back(event);
	}

	void getEvent(TEvent &event) override {
		if (mNextEvent < mEvents.size()) {
			event = mEvents[mNextEvent++];
			return;
		}
		event = makeMouseEvent(evMouseUp, 0, 0, 0, 0);
	}

	void putEvent(TEvent &event) override {
		if (event.what != evNothing) mEvents.push_back(event);
	}

  private:
	std::vector<TEvent> mEvents;
	std::size_t mNextEvent;
};

int localXForEditorColumn(int column) {
	static constexpr int regressionTextLeft = 1;
	return regressionTextLeft + std::max(0, column);
}

uchar mouseButtonsForMode(MRFEBlockMode mode) {
	if (mode == MRFEBlockMode::Column) return static_cast<uchar>(mbRightButton);
	if (mode == MRFEBlockMode::Line) return static_cast<uchar>(mbLeftButton | mbRightButton);
	return static_cast<uchar>(mbLeftButton);
}

ushort mouseModifiersForMode(MRFEBlockMode mode) {
	if (mode == MRFEBlockMode::Line) return static_cast<ushort>(kbCtrlShift | kbAltShift);
	return 0;
}

bool runEditorMouseDragCaseForMode(MRFEBlockMode mode, const char *phase, std::string &failureReason, uchar forcedButtons = 0, ushort forcedModifiers = 0) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = "alpha\n\nbeta\n";
	QueuedMouseOwner owner(TRect(0, 0, 80, 8));
	MRFileEditor *editor = new MRFileEditor(TRect(0, 0, 80, 8), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;
	owner.insert(editor);

	if (!editor->replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}

	const int startX = localXForEditorColumn(1);
	const int endX = localXForEditorColumn(4);
	const int startY = 0;
	const int endY = 2;
	const uchar buttons = forcedButtons != 0 ? forcedButtons : mouseButtonsForMode(mode);
	const ushort modifiers = forcedButtons != 0 ? forcedModifiers : mouseModifiersForMode(mode);
	owner.queueMouseEvent(makeMouseEvent(evMouseMove, endX, endY, modifiers, buttons));
	owner.queueMouseEvent(makeMouseEvent(evMouseUp, endX, endY, 0, 0));
	TEvent event = makeMouseEvent(evMouseDown, startX, startY, modifiers, buttons);
	editor->handleEvent(event);
	if (!ops.adoptMouseSelection(*editor, editor->lastMouseSelectionModifiers())) {
		failureReason = std::string("Unable to adopt editor mouse drag in ") + phase + ".";
		return false;
	}

	if (mode == MRFEBlockMode::Line) {
		if (!checkEditorBlock(*editor, ops, mode, MRFEBlockStatus::Committed, 0, 2, 0, 0, phase, failureReason)) return false;
	} else if (!checkEditorBlock(*editor, ops, mode, MRFEBlockStatus::Committed, 0, 2, 1, 4, phase, failureReason))
		return false;

	TEvent right{};
	right.what = evKeyDown;
	right.keyDown.keyCode = kbRight;
	editor->handleEvent(right);
	if (mode == MRFEBlockMode::Line) return checkEditorBlock(*editor, ops, mode, MRFEBlockStatus::Committed, 0, 2, 0, 0, phase, failureReason);
	return checkEditorBlock(*editor, ops, mode, MRFEBlockStatus::Committed, 0, 2, 1, 4, phase, failureReason);
}

bool runEditorMouseDragReplacesExistingBlockCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = "alpha\n\nbeta\n";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	QueuedMouseOwner owner(TRect(0, 0, 80, 8));
	MRFileEditor *editor = new MRFileEditor(TRect(0, 0, 80, 8), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;
	owner.insert(editor);

	if (!editor->replaceBufferText(text.c_str())) {
		failureReason = "Unable to seed editor text in mouse replacement case.";
		return false;
	}
	placeEditorCursor(*editor, text, starts, 0, 1);
	if (!ops.beginLine(*editor)) {
		failureReason = "Unable to begin existing line block in mouse replacement case.";
		return false;
	}
	placeEditorCursor(*editor, text, starts, 2, 1);
	if (!ops.end(*editor)) {
		failureReason = "Unable to end existing line block in mouse replacement case.";
		return false;
	}
	if (!checkEditorBlock(*editor, ops, MRFEBlockMode::Line, MRFEBlockStatus::Committed, 0, 2, 0, 0, "existing line block before mouse replacement", failureReason)) return false;

	const int startX = localXForEditorColumn(1);
	const int endX = localXForEditorColumn(4);
	owner.queueMouseEvent(makeMouseEvent(evMouseMove, endX, 2, 0, mbRightButton));
	owner.queueMouseEvent(makeMouseEvent(evMouseUp, endX, 2, 0, 0));
	TEvent event = makeMouseEvent(evMouseDown, startX, 0, 0, mbRightButton);
	editor->handleEvent(event);
	if (!ops.adoptMouseSelection(*editor, editor->lastMouseSelectionModifiers())) {
		failureReason = "Unable to adopt replacement mouse column block.";
		return false;
	}
	return checkEditorBlock(*editor, ops, MRFEBlockMode::Column, MRFEBlockStatus::Committed, 0, 2, 1, 4, "mouse column replaces existing block", failureReason);
}

TEvent makeKeyEvent(ushort keyCode, ushort modifiers = 0) {
	TEvent event{};
	event.what = evKeyDown;
	event.keyDown.keyCode = keyCode;
	event.keyDown.controlKeyState = modifiers;
	return event;
}

bool runEditorFreeCursorAfterCommittedBlockCase(MRFEBlockMode mode, const char *phase, std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = "abc";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 8), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, 0, 0);
	if (!beginBlockForMode(ops, editor, mode)) {
		failureReason = std::string("Unable to begin block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, 0, mode == MRFEBlockMode::Line ? 0 : 2);
	if (!ops.end(editor)) {
		failureReason = std::string("Unable to end block in ") + phase + ".";
		return false;
	}
	if (editor.hasTextSelection()) {
		failureReason = std::string("Committed block left text selection active before free-cursor check in ") + phase + ".";
		return false;
	}
	const std::size_t lineEnd = lineContentEndForIndex(text, starts, 0);
	editor.setCursorOffsetAtVisualColumn(lineEnd, 3);
	const int before = editor.displayedCursorColumn();
	TEvent right = makeKeyEvent(kbRight);
	editor.handleEvent(right);
	if (editor.cursorOffset() != lineEnd || editor.displayedCursorColumn() != before + 1) {
		failureReason = std::string("Free cursor must advance beyond EOL after committed block in ") + phase + ".";
		return false;
	}
	right = makeKeyEvent(kbRight);
	editor.handleEvent(right);
	if (editor.cursorOffset() != lineEnd || editor.displayedCursorColumn() != before + 2) {
		failureReason = std::string("Free cursor must continue beyond EOL after committed block in ") + phase + ".";
		return false;
	}
	return true;
}

} // namespace

bool mrfeSeedMouseColumnStateForRegression(MRFileEditor &editor, int anchorColumn, int cursorColumn) {
	editor.mMouseSelectionColumnsValid = true;
	editor.mMouseSelectionAnchorColumn = anchorColumn;
	editor.mMouseSelectionCursorColumn = cursorColumn;
	return true;
}

MRFEBlockOps::MRFEBlockOps() : mGeometry() {
}

bool MRFEBlockOps::begin(MRFileEditor &editor, MRFEBlockMode mode) {
	mGeometry = MRFEBlockGeometry();
	mGeometry.mode = mode;
	mGeometry.status = MRFEBlockStatus::Marking;
	mGeometry.hidden = false;
	mGeometry.anchor = editor.cursorOffset();
	mGeometry.cursor = mGeometry.anchor;
	mGeometry.anchorColumn = mode == MRFEBlockMode::Column ? std::max(0, editor.displayedCursorColumn()) : std::max(0, static_cast<int>(editor.columnOfOffset(mGeometry.anchor)));
	mGeometry.cursorColumn = mGeometry.anchorColumn;
	normalize(editor);
	applySelection(editor);
	applyOverlay(editor);
	return true;
}

bool MRFEBlockOps::beginLine(MRFileEditor &editor) {
	return begin(editor, MRFEBlockMode::Line);
}

bool MRFEBlockOps::beginColumn(MRFileEditor &editor) {
	return begin(editor, MRFEBlockMode::Column);
}

bool MRFEBlockOps::beginStream(MRFileEditor &editor) {
	return begin(editor, MRFEBlockMode::Stream);
}

bool MRFEBlockOps::end(MRFileEditor &editor) {
	if (mGeometry.status == MRFEBlockStatus::Inactive) return false;
	updateFromEditor(editor);
	mGeometry.status = MRFEBlockStatus::Committed;
	applySelection(editor);
	applyOverlay(editor);
	return true;
}

bool MRFEBlockOps::clear(MRFileEditor &editor) {
	mGeometry = MRFEBlockGeometry();
	deactivateVisual(editor);
	return true;
}

bool MRFEBlockOps::toggleVisibility(MRFileEditor &editor) {
	if (!hasStoredBlock()) return false;
	mGeometry.hidden = !mGeometry.hidden;
	if (mGeometry.hidden) deactivateVisual(editor);
	else {
		applySelection(editor);
		applyOverlay(editor);
	}
	return true;
}

bool MRFEBlockOps::updateFromEditor(MRFileEditor &editor) {
	if (mGeometry.status == MRFEBlockStatus::Inactive) return false;
	mGeometry.cursor = editor.cursorOffset();
	if (mGeometry.mode == MRFEBlockMode::Column)
		mGeometry.cursorColumn = std::max(0, editor.displayedCursorColumn());
	else
		mGeometry.cursorColumn = std::max(0, static_cast<int>(editor.columnOfOffset(mGeometry.cursor)));
	normalize(editor);
	if (!mGeometry.hidden) {
		applySelection(editor);
		applyOverlay(editor);
	}
	return true;
}

bool MRFEBlockOps::adoptMouseSelection(MRFileEditor &editor, unsigned short modifiers) {
	const bool ctrl = (modifiers & kbCtrlShift) != 0;
	const bool alt = (modifiers & kbAltShift) != 0;
	const bool line = ctrl && alt;
	const bool column = !line && alt;
	const bool stream = !line && !column && (ctrl || (modifiers & kbShift) != 0);
	if (!column && !line && !stream) return false;

	mGeometry = MRFEBlockGeometry();
	mGeometry.mode = column ? MRFEBlockMode::Column : line ? MRFEBlockMode::Line : MRFEBlockMode::Stream;
	mGeometry.status = MRFEBlockStatus::Committed;
	mGeometry.hidden = false;
	mGeometry.anchor = editor.selectionAnchorOffset();
	mGeometry.cursor = editor.selectionCursorOffset();
	if (column) {
		int anchorColumn = 0;
		int cursorColumn = 0;
		if (editor.lastMouseSelectionColumns(anchorColumn, cursorColumn)) {
			mGeometry.anchorColumn = std::max(anchorColumn, 0);
			mGeometry.cursorColumn = std::max(cursorColumn, 0);
		}
	} else {
		mGeometry.anchorColumn = std::max(0, static_cast<int>(editor.columnOfOffset(mGeometry.anchor)));
		mGeometry.cursorColumn = std::max(0, static_cast<int>(editor.columnOfOffset(mGeometry.cursor)));
	}
	normalize(editor);
	applySelection(editor);
	applyOverlay(editor);
	return true;
}

bool MRFEBlockOps::refreshVisual(MRFileEditor &editor) {
	if (!hasStoredBlock()) return false;
	if (mGeometry.hidden) deactivateVisual(editor);
	else {
		normalize(editor);
		applySelection(editor);
		applyOverlay(editor);
	}
	return true;
}

bool MRFEBlockOps::moveCursorToStart(MRFileEditor &editor) {
	if (!hasVisibleBlock()) return false;
	editor.setCursorOffset(mGeometry.rangeStart);
	return true;
}

bool MRFEBlockOps::moveCursorToEnd(MRFileEditor &editor) {
	if (!hasVisibleBlock()) return false;
	editor.setCursorOffset(mGeometry.rangeEnd);
	return true;
}

bool MRFEBlockOps::hasStoredBlock() const noexcept {
	return mGeometry.status != MRFEBlockStatus::Inactive;
}

bool MRFEBlockOps::hasVisibleBlock() const noexcept {
	return hasStoredBlock() && !mGeometry.hidden;
}

bool MRFEBlockOps::isMarking() const noexcept {
	return mGeometry.status == MRFEBlockStatus::Marking && !mGeometry.hidden;
}

bool MRFEBlockOps::isHidden() const noexcept {
	return mGeometry.hidden;
}

MRFEBlockMode MRFEBlockOps::mode() const noexcept {
	return mGeometry.mode;
}

MRFEBlockStatus MRFEBlockOps::status() const noexcept {
	return mGeometry.status;
}

const MRFEBlockGeometry &MRFEBlockOps::geometry() const noexcept {
	return mGeometry;
}

void MRFEBlockOps::normalize(MRFileEditor &editor) {
	MarkerInput input;
	input.text = editor.snapshotText();
	input.lineStarts = lineStartsForText(input.text);
	input.anchor = std::min(mGeometry.anchor, input.text.size());
	input.cursor = std::min(mGeometry.cursor, input.text.size());
	input.anchorColumn = std::max(mGeometry.anchorColumn, 0);
	input.cursorColumn = std::max(mGeometry.cursorColumn, 0);
	mGeometry.anchor = input.anchor;
	mGeometry.cursor = input.cursor;
	std::unique_ptr<BlockMarker> marker = markerForMode(mGeometry.mode);
	if (marker != nullptr) marker->normalize(input, mGeometry);
}

void MRFEBlockOps::applySelection(MRFileEditor &editor) {
	if (!hasVisibleBlock()) return;
	if (mGeometry.status == MRFEBlockStatus::Marking) editor.setSelectionOffsets(mGeometry.rangeStart, mGeometry.rangeEnd, False);
	else
		editor.setSelectionOffsets(editor.cursorOffset(), editor.cursorOffset(), False);
}

void MRFEBlockOps::applyOverlay(MRFileEditor &editor) {
	if (!hasVisibleBlock()) return;
	std::size_t visualEnd = mGeometry.rangeEnd;
	if (mGeometry.mode != MRFEBlockMode::Stream && visualEnd > mGeometry.rangeStart) --visualEnd;
	editor.setBlockOverlayState(static_cast<int>(mGeometry.mode), mGeometry.rangeStart, visualEnd, true, mGeometry.status == MRFEBlockStatus::Marking, mGeometry.col1, mGeometry.col2);
}

void MRFEBlockOps::deactivateVisual(MRFileEditor &editor) {
	editor.setBlockOverlayState(0, 0, 0, false);
	editor.setSelectionOffsets(editor.cursorOffset(), editor.cursorOffset(), False);
}

bool mrfeBlockOpsRegressionHarness(std::string &failureReason) {
	const std::string text = "alpha\n\nbeta\nlast";
	MRFEBlockGeometry geometry;

	geometry = harnessNormalize(MRFEBlockMode::Line, text, 8, 1, 0, 0);
	if (!checkGeometry(geometry, MRFEBlockMode::Line, 0, 12, 0, 2, 0, 0, "reverse line selection over empty line", failureReason)) return false;

	geometry = harnessNormalize(MRFEBlockMode::Stream, text, 6, 6, 0, 0);
	if (!checkGeometry(geometry, MRFEBlockMode::Stream, 6, 6, 1, 1, 0, 0, "empty stream selection on empty line", failureReason)) return false;

	geometry = harnessNormalize(MRFEBlockMode::Column, text, 1, 8, 9, 3);
	if (!checkGeometry(geometry, MRFEBlockMode::Column, 0, 12, 0, 2, 3, 9, "reverse column selection over empty line", failureReason)) return false;

	geometry = harnessNormalize(MRFEBlockMode::Stream, text, 12, 2, 0, 0);
	if (!checkGeometry(geometry, MRFEBlockMode::Stream, 2, 12, 0, 3, 0, 0, "reverse stream selection", failureReason)) return false;

	const std::string editorText = "alpha\n\nbeta\nomega";
	if (!runEditorMarkingCase(MRFEBlockMode::Line, editorText, 0, 2, 2, 1, 0, 2, 0, 0, "editor line forward over empty line", failureReason)) return false;
	if (!runEditorMarkingCase(MRFEBlockMode::Line, editorText, 3, 2, 1, 0, 1, 3, 0, 0, "editor line reverse from empty line", failureReason)) return false;
	if (!runEditorMarkingCase(MRFEBlockMode::Stream, editorText, 0, 1, 0, 4, 0, 0, 1, 4, "editor stream same-line forward", failureReason)) return false;
	if (!runEditorMarkingCase(MRFEBlockMode::Stream, editorText, 2, 2, 0, 4, 0, 2, 2, 4, "editor stream reverse over empty line", failureReason)) return false;
	if (!runEditorMarkingCase(MRFEBlockMode::Stream, editorText, 0, 4, 1, 0, 0, 1, 0, 4, "editor stream forward into empty line", failureReason)) return false;
	if (!runEditorMarkingCase(MRFEBlockMode::Stream, editorText, 1, 0, 0, 4, 0, 1, 0, 4, "editor stream reverse from empty line", failureReason)) return false;
	if (!runEditorMarkingCase(MRFEBlockMode::Stream, editorText, 1, 0, 1, 0, 1, 1, 0, 0, "editor empty stream on empty line", failureReason)) return false;
	if (!runEditorMarkingCase(MRFEBlockMode::Column, editorText, 0, 1, 0, 4, 0, 0, 1, 4, "editor column single line", failureReason)) return false;
	if (!runEditorMarkingCase(MRFEBlockMode::Column, editorText, 2, 3, 0, 1, 0, 2, 1, 3, "editor column reverse over empty line", failureReason)) return false;
	if (!runEditorMarkingCase(MRFEBlockMode::Column, editorText, 1, 2, 1, 6, 1, 1, 2, 6, "editor column empty line virtual range", failureReason)) return false;
	if (!runMenuColumnIgnoresStaleMouseStateCase(failureReason)) return false;
	if (!runEditorToggleCase(MRFEBlockMode::Line, "line toggle hide/show", failureReason)) return false;
	if (!runEditorToggleCase(MRFEBlockMode::Column, "column toggle hide/show", failureReason)) return false;
	if (!runEditorToggleCase(MRFEBlockMode::Stream, "stream toggle hide/show", failureReason)) return false;
	if (!runEditorReplaceCase(MRFEBlockMode::Line, MRFEBlockMode::Stream, "replace line with stream", failureReason)) return false;
	if (!runEditorReplaceCase(MRFEBlockMode::Stream, MRFEBlockMode::Line, "replace stream with line", failureReason)) return false;
	if (!runEditorReplaceCase(MRFEBlockMode::Line, MRFEBlockMode::Column, "replace line with column", failureReason)) return false;
	if (!runEditorReplaceCase(MRFEBlockMode::Column, MRFEBlockMode::Line, "replace column with line", failureReason)) return false;
	if (!runEditorReplaceCase(MRFEBlockMode::Stream, MRFEBlockMode::Column, "replace stream with column", failureReason)) return false;
	if (!runEditorReplaceCase(MRFEBlockMode::Column, MRFEBlockMode::Stream, "replace column with stream", failureReason)) return false;
	if (!runEditorFreeCursorAfterCommittedBlockCase(MRFEBlockMode::Line, "free cursor after committed line block", failureReason)) return false;
	if (!runEditorFreeCursorAfterCommittedBlockCase(MRFEBlockMode::Column, "free cursor after committed column block", failureReason)) return false;
		if (!runEditorFreeCursorAfterCommittedBlockCase(MRFEBlockMode::Stream, "free cursor after committed stream block", failureReason)) return false;
		if (!runEditorMouseAdoptionCase(failureReason)) return false;
		if (!runEditorMouseDragCaseForMode(MRFEBlockMode::Stream, "raw mouse left-button drag stream over empty line", failureReason)) return false;
		if (!runEditorMouseDragCaseForMode(MRFEBlockMode::Stream, "raw mouse both-buttons drag stream over empty line", failureReason, static_cast<uchar>(mbLeftButton | mbRightButton), 0)) return false;
		if (!runEditorMouseDragCaseForMode(MRFEBlockMode::Column, "raw mouse right-button drag column over empty line", failureReason)) return false;
		if (!runEditorMouseDragCaseForMode(MRFEBlockMode::Line, "raw mouse ctrl-alt drag line over empty line", failureReason)) return false;
	if (!runEditorMouseDragReplacesExistingBlockCase(failureReason)) return false;

	failureReason.clear();
	return true;
}
