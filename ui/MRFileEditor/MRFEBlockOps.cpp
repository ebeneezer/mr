#define Uses_TKeys
#define Uses_TGroup
#define Uses_TEvent
#include <tvision/tv.h>

#include "MRFEBlockOps.hpp"
#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"
#include "../MRWindowSupport.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
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

struct ColumnLineReplacement {
	MRTextBufferModel::Range range;
	std::string text;
};

std::vector<std::size_t> lineStartsForText(const std::string &text) {
	std::vector<std::size_t> starts;
	starts.push_back(0);
	for (std::size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\r') {
			if (i + 1 < text.size() && text[i + 1] == '\n') {
				starts.push_back(i + 2);
				++i;
			} else
				starts.push_back(i + 1);
		} else if (text[i] == '\n')
			starts.push_back(i + 1);
	}
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

int blockTabDisplayWidth(const MREditSetupSettings &settings, int visualColumn) noexcept {
	const int currentColumn = std::max(1, visualColumn + 1);
	const int targetColumn = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);
	return std::max(1, targetColumn - currentColumn);
}

bool nextBlockDisplayChar(const std::string &text, std::size_t lineStart, std::size_t lineEnd, std::size_t &offset, std::size_t &width, int visualColumn, const MREditSetupSettings &settings) noexcept {
	if (offset >= lineEnd) return false;
	if (text[offset] == '\t') {
		++offset;
		width = static_cast<std::size_t>(blockTabDisplayWidth(settings, visualColumn));
		return true;
	}

	TStringView line(text.data() + lineStart, lineEnd - lineStart);
	std::size_t local = offset - lineStart;
	if (!TText::next(line, local, width)) return false;
	offset = lineStart + local;
	return true;
}

std::size_t offsetAtLineVisualColumn(const std::string &text, const std::vector<std::size_t> &starts, std::size_t lineIndex, int visualColumn) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	if (starts.empty()) return 0;
	lineIndex = std::min(lineIndex, starts.size() - 1);
	const std::size_t lineStart = starts[lineIndex];
	const std::size_t lineEnd = lineContentEndForIndex(text, starts, lineIndex);
	const int target = std::max(visualColumn, 0);
	std::size_t offset = lineStart;
	int visual = 0;

	while (offset < lineEnd) {
		std::size_t next = offset;
		std::size_t width = 0;

		if (!nextBlockDisplayChar(text, lineStart, lineEnd, next, width, visual, settings)) break;
		if (visual + static_cast<int>(width) > target) break;
		visual += static_cast<int>(width);
		offset = next;
	}
	return offset;
}

bool appendColumnVisualCells(const std::string &text, const std::vector<std::size_t> &starts, std::size_t lineIndex, int col1, int col2, MRFEArenaAllocator &arena) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	if (starts.empty() || col2 <= col1) return true;
	lineIndex = std::min(lineIndex, starts.size() - 1);
	const std::size_t lineStart = starts[lineIndex];
	const std::size_t lineEnd = lineContentEndForIndex(text, starts, lineIndex);
	const int fromColumn = std::max(col1, 0);
	const int toColumn = std::max(col2, 0);
	std::size_t offset = lineStart;
	int visual = 0;
	int emitted = fromColumn;

	while (offset < lineEnd && emitted < toColumn) {
		std::size_t next = offset;
		std::size_t width = 0;

		if (!nextBlockDisplayChar(text, lineStart, lineEnd, next, width, visual, settings)) break;
		const int charStart = visual;
		const int charEnd = visual + static_cast<int>(width);
		if (charEnd > fromColumn && charStart < toColumn) {
			if (emitted < std::max(charStart, fromColumn)) {
				if (!arena.appendFill(static_cast<std::size_t>(std::max(charStart, fromColumn) - emitted), ' ')) return false;
				emitted = std::max(charStart, fromColumn);
			}
			const int overlapStart = std::max(charStart, fromColumn);
			const int overlapEnd = std::min(charEnd, toColumn);
			const std::size_t byteWidth = next - offset;
			if (width == 1 && byteWidth == 1 && overlapStart == charStart && overlapEnd == charEnd && text[offset] != '\t') {
				if (!arena.append(std::string_view(text.data() + offset, 1))) return false;
			} else if (!arena.appendFill(static_cast<std::size_t>(overlapEnd - overlapStart), ' '))
				return false;
			emitted = overlapEnd;
		}
		visual = charEnd;
		offset = next;
	}
	if (emitted < toColumn) return arena.appendFill(static_cast<std::size_t>(toColumn - emitted), ' ');
	return true;
}

std::string eraseVisualColumnsFromLine(std::string_view lineText, int col1, int col2) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	std::string result;
	std::string line(lineText);
	const int fromColumn = std::max(col1, 0);
	const int toColumn = std::max(col2, 0);
	std::size_t offset = 0;
	int visual = 0;

	result.reserve(line.size());
	while (offset < line.size()) {
		std::size_t next = offset;
		std::size_t width = 0;

		if (!nextBlockDisplayChar(line, 0, line.size(), next, width, visual, settings)) break;
		const int charStart = visual;
		const int charEnd = visual + static_cast<int>(width);
		if (charEnd <= fromColumn || charStart >= toColumn)
			result.append(line.data() + offset, next - offset);
		else {
			const int leftKeep = std::max(0, fromColumn - charStart);
			const int rightKeep = std::max(0, charEnd - toColumn);

			if (leftKeep > 0) result.append(static_cast<std::size_t>(leftKeep), ' ');
			if (rightKeep > 0) result.append(static_cast<std::size_t>(rightKeep), ' ');
		}
		visual = charEnd;
		offset = next;
	}
	return result;
}

std::string insertVisualColumnsIntoLine(std::string_view lineText, int destCol, std::string_view payload) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	std::string result;
	std::string line(lineText);
	const int targetColumn = std::max(destCol, 0);
	std::size_t offset = 0;
	int visual = 0;
	bool inserted = false;

	result.reserve(line.size() + payload.size());
	while (offset < line.size()) {
		std::size_t next = offset;
		std::size_t width = 0;

		if (!nextBlockDisplayChar(line, 0, line.size(), next, width, visual, settings)) break;
		const int charStart = visual;
		const int charEnd = visual + static_cast<int>(width);
		if (!inserted && targetColumn <= charStart) {
			if (visual < targetColumn) result.append(static_cast<std::size_t>(targetColumn - visual), ' ');
			result.append(payload.data(), payload.size());
			inserted = true;
		}
		if (!inserted && targetColumn > charStart && targetColumn < charEnd) {
			result.append(static_cast<std::size_t>(targetColumn - charStart), ' ');
			result.append(payload.data(), payload.size());
			result.append(static_cast<std::size_t>(charEnd - targetColumn), ' ');
			inserted = true;
		} else
			result.append(line.data() + offset, next - offset);
		visual = charEnd;
		offset = next;
	}
	if (!inserted) {
		if (visual < targetColumn) result.append(static_cast<std::size_t>(targetColumn - visual), ' ');
		result.append(payload.data(), payload.size());
	}
	return result;
}

void collectColumnEraseReplacements(const std::string &text, const std::vector<std::size_t> &starts, const MRFEBlockGeometry &geometry, std::vector<ColumnLineReplacement> &replacements) {
	const std::size_t firstLine = std::min(geometry.line1, starts.empty() ? 0 : starts.size() - 1);
	const std::size_t lastLine = std::min(geometry.line2, starts.empty() ? 0 : starts.size() - 1);

	replacements.clear();
	if (starts.empty() || geometry.col2 <= geometry.col1) return;
	for (std::size_t line = firstLine; line <= lastLine; ++line) {
		const std::size_t lineStart = starts[line];
		const std::size_t lineEnd = lineContentEndForIndex(text, starts, line);
		const std::string replacement = eraseVisualColumnsFromLine(std::string_view(text.data() + lineStart, lineEnd - lineStart), geometry.col1, geometry.col2);

		if (replacement.size() != lineEnd - lineStart || replacement != std::string_view(text.data() + lineStart, lineEnd - lineStart))
			replacements.push_back(ColumnLineReplacement{MRTextBufferModel::Range(lineStart, lineEnd), replacement});
	}
}

std::string_view payloadView(const std::vector<char> &payload) {
	if (payload.empty()) return std::string_view();
	return std::string_view(payload.data(), payload.size());
}

std::string_view payloadRowView(const std::vector<char> &payload, std::size_t row, std::size_t width) {
	const std::size_t start = row * width;
	if (width == 0 || start >= payload.size()) return std::string_view();
	return std::string_view(payload.data() + start, std::min(width, payload.size() - start));
}

std::string escapedArenaPayload(std::string_view payload) {
	static constexpr char kHex[] = "0123456789ABCDEF";
	std::string escaped;

	escaped.reserve(payload.size());
	for (char raw : payload) {
		const unsigned char ch = static_cast<unsigned char>(raw);

		if (ch == '\\') escaped += "\\\\";
		else if (ch == '"') escaped += "\\\"";
		else if (ch == '\n') escaped += "\\n";
		else if (ch == '\r') escaped += "\\r";
		else if (ch == '\t') escaped += "\\t";
		else if (ch >= 32 && ch <= 126) escaped.push_back(static_cast<char>(ch));
		else {
			escaped += "\\x";
			escaped.push_back(kHex[(ch >> 4) & 0x0F]);
			escaped.push_back(kHex[ch & 0x0F]);
		}
	}
	return escaped;
}

std::string lineSeparatorForText(const std::string &text) {
	for (std::size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\r') {
			if (i + 1 < text.size() && text[i + 1] == '\n') return "\r\n";
			return "\r";
		}
		if (text[i] == '\n') return "\n";
	}
	return "\n";
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

bool runEditorMouseClickPreservesExistingBlockCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = "alpha\n\nbeta\n";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	QueuedMouseOwner owner(TRect(0, 0, 80, 8));
	MRFileEditor *editor = new MRFileEditor(TRect(0, 0, 80, 8), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;
	owner.insert(editor);

	if (!editor->replaceBufferText(text.c_str())) {
		failureReason = "Unable to seed editor text in mouse click preservation case.";
		return false;
	}
	placeEditorCursor(*editor, text, starts, 0, 1);
	if (!ops.beginColumn(*editor)) {
		failureReason = "Unable to begin existing column block in mouse click preservation case.";
		return false;
	}
	placeEditorCursor(*editor, text, starts, 2, 4);
	if (!ops.end(*editor)) {
		failureReason = "Unable to commit existing column block in mouse click preservation case.";
		return false;
	}
	if (!checkEditorBlock(*editor, ops, MRFEBlockMode::Column, MRFEBlockStatus::Committed, 0, 2, 1, 4, "existing column block before simple mouse click", failureReason)) return false;

	owner.queueMouseEvent(makeMouseEvent(evMouseUp, localXForEditorColumn(0), 0, 0, 0));
	TEvent event = makeMouseEvent(evMouseDown, localXForEditorColumn(0), 0, 0, mbLeftButton);
	editor->handleEvent(event);
	if (ops.adoptMouseSelection(*editor, editor->lastMouseSelectionModifiers())) {
		failureReason = "Simple mouse click must not be adoptable as a new block.";
		return false;
	}
	return checkEditorBlock(*editor, ops, MRFEBlockMode::Column, MRFEBlockStatus::Committed, 0, 2, 1, 4, "existing column block after simple mouse click", failureReason);
}

TEvent makeKeyEvent(ushort keyCode, ushort modifiers = 0) {
	TEvent event{};
	event.what = evKeyDown;
	event.keyDown.keyCode = keyCode;
	event.keyDown.controlKeyState = modifiers;
	return event;
}

void sendEditorCommand(MRFileEditor &editor, ushort command) {
	TEvent event{};
	event.what = evCommand;
	event.message.command = command;
	editor.handleEvent(event);
}

bool checkBlockDeleteUndoRedo(MRFileEditor &editor, MRFEBlockOps &ops, const std::string &originalText, const std::string &deletedText, const char *phase, std::string &failureReason) {
	std::string errorText;

	if (!ops.deleteCurrentBlock(editor, &errorText)) {
		failureReason = std::string("Unable to delete block in ") + phase + ": " + errorText;
		return false;
	}
	if (editor.snapshotText() != deletedText) {
		failureReason = std::string("Deleted text mismatch in ") + phase + ".";
		return false;
	}
	if (ops.hasVisibleBlock()) {
		failureReason = std::string("Deleted block must not remain visible in ") + phase + ".";
		return false;
	}
	sendEditorCommand(editor, cmMrEditUndo);
	if (editor.snapshotText() != originalText) {
		failureReason = std::string("Single undo must restore full block delete in ") + phase + ".";
		return false;
	}
	sendEditorCommand(editor, cmMrEditRedo);
	if (editor.snapshotText() != deletedText) {
		failureReason = std::string("Single redo must reapply full block delete in ") + phase + ".";
		return false;
	}
	return true;
}

bool checkBlockCopyUndoRedo(MRFileEditor &editor, MRFEBlockOps &ops, MRFEBlockMode mode, std::size_t expectedLine1, std::size_t expectedLine2, int expectedCol1, int expectedCol2, const std::string &originalText, const std::string &copiedText, const char *phase, std::string &failureReason) {
	std::string errorText;

	if (!ops.copyCurrentBlockToCursor(editor, &errorText)) {
		failureReason = std::string("Unable to copy block in ") + phase + ": " + errorText;
		return false;
	}
	if (editor.snapshotText() != copiedText) {
		failureReason = std::string("Copied text mismatch in ") + phase + ".";
		return false;
	}
	if (mode == MRFEBlockMode::Column && !checkEditorBlock(editor, ops, MRFEBlockMode::Column, MRFEBlockStatus::Committed, expectedLine1, expectedLine2, expectedCol1, expectedCol2, phase, failureReason)) return false;
	sendEditorCommand(editor, cmMrEditUndo);
	if (editor.snapshotText() != originalText) {
		failureReason = std::string("Single undo must restore full block copy in ") + phase + ".";
		return false;
	}
	sendEditorCommand(editor, cmMrEditRedo);
	if (editor.snapshotText() != copiedText) {
		failureReason = std::string("Single redo must reapply full block copy in ") + phase + ".";
		return false;
	}
	return true;
}

bool checkInterWindowCopyUndoRedo(MREditWindow &source, MREditWindow &target, const std::string &targetOriginalText, const std::string &targetCopiedText, const char *phase, std::string &failureReason) {
	std::string errorText;
	MRFileEditor *targetEditor = target.getEditor();

	if (targetEditor == nullptr) {
		failureReason = std::string("Missing target editor in ") + phase + ".";
		return false;
	}
	if (!source.copyBlockTo(target, &errorText)) {
		failureReason = std::string("Unable to copy block between windows in ") + phase + ": " + errorText;
		return false;
	}
	if (targetEditor->snapshotText() != targetCopiedText) {
		failureReason = std::string("Inter-window copied text mismatch in ") + phase + ": got=\"" + escapedArenaPayload(targetEditor->snapshotText()) + "\" expected=\"" + escapedArenaPayload(targetCopiedText) + "\".";
		return false;
	}
	sendEditorCommand(*targetEditor, cmMrEditUndo);
	if (targetEditor->snapshotText() != targetOriginalText) {
		failureReason = std::string("Single undo must restore full inter-window block copy in ") + phase + ".";
		return false;
	}
	sendEditorCommand(*targetEditor, cmMrEditRedo);
	if (targetEditor->snapshotText() != targetCopiedText) {
		failureReason = std::string("Single redo must reapply full inter-window block copy in ") + phase + ".";
		return false;
	}
	return true;
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

bool runBlockDeleteCase(MRFEBlockMode mode, const std::string &text, std::size_t anchorLine, int anchorColumn, std::size_t cursorLine, int cursorColumn, const std::string &deletedText, const char *phase, std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, anchorLine, anchorColumn);
	if (!beginBlockForMode(ops, editor, mode)) {
		failureReason = std::string("Unable to begin delete block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, cursorLine, cursorColumn);
	if (!ops.updateFromEditor(editor) || !ops.end(editor)) {
		failureReason = std::string("Unable to commit delete block in ") + phase + ".";
		return false;
	}
	return checkBlockDeleteUndoRedo(editor, ops, text, deletedText, phase, failureReason);
}

bool runBlockCopyCase(MRFEBlockMode mode, const std::string &text, std::size_t anchorLine, int anchorColumn, std::size_t cursorLine, int cursorColumn, std::size_t targetLine, int targetColumn, const std::string &copiedText, const char *phase, std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, anchorLine, anchorColumn);
	if (!beginBlockForMode(ops, editor, mode)) {
		failureReason = std::string("Unable to begin copy block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, cursorLine, cursorColumn);
	if (!ops.updateFromEditor(editor) || !ops.end(editor)) {
		failureReason = std::string("Unable to commit copy block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, targetLine, targetColumn);
	if (mode == MRFEBlockMode::Column) {
		const std::size_t expectedLine2 = targetLine + std::max(cursorLine, anchorLine) - std::min(cursorLine, anchorLine);
		const int expectedCol1 = std::max(targetColumn, 0);
		const int expectedCol2 = expectedCol1 + std::max(cursorColumn, anchorColumn) - std::min(cursorColumn, anchorColumn);

		return checkBlockCopyUndoRedo(editor, ops, mode, targetLine, expectedLine2, expectedCol1, expectedCol2, text, copiedText, phase, failureReason);
	}
	return checkBlockCopyUndoRedo(editor, ops, mode, 0, 0, 0, 0, text, copiedText, phase, failureReason);
}

bool runInterWindowCopyCase(MRFEBlockMode mode, const std::string &sourceText, std::size_t anchorLine, int anchorColumn, std::size_t cursorLine, int cursorColumn, const std::string &targetText, std::size_t targetLine, int targetColumn, const std::string &copiedText, const char *phase, std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::vector<std::size_t> sourceStarts = lineStartsForText(sourceText);
	const std::vector<std::size_t> targetStarts = lineStartsForText(targetText);
	MREditWindow sourceWindow(TRect(0, 0, 80, 16), "source", 2001);
	MREditWindow targetWindow(TRect(0, 0, 80, 16), "target", 2002);
	MRFileEditor *sourceEditor = sourceWindow.getEditor();
	MRFileEditor *targetEditor = targetWindow.getEditor();

	if (sourceEditor == nullptr || targetEditor == nullptr) {
		failureReason = std::string("Unable to create editors in ") + phase + ".";
		return false;
	}
	if (!sourceEditor->replaceBufferText(sourceText.c_str()) || (!targetText.empty() && !targetEditor->replaceBufferText(targetText.c_str()))) {
		failureReason = std::string("Unable to seed inter-window copy editors in ") + phase + ".";
		return false;
	}
	placeEditorCursor(*sourceEditor, sourceText, sourceStarts, anchorLine, anchorColumn);
	if (mode == MRFEBlockMode::Line) sourceWindow.beginLineBlock();
	else if (mode == MRFEBlockMode::Column)
		sourceWindow.beginColumnBlock();
	else
		sourceWindow.beginStreamBlock();
	placeEditorCursor(*sourceEditor, sourceText, sourceStarts, cursorLine, cursorColumn);
	sourceWindow.endBlock();
	placeEditorCursor(*targetEditor, targetText, targetStarts, targetLine, targetColumn);
	return checkInterWindowCopyUndoRedo(sourceWindow, targetWindow, targetText, copiedText, phase, failureReason);
}

bool runStreamBlockLoadSaveCase(std::string &failureReason) {
	const std::string sourcePath = "/tmp/mrfe-blockops-load-source.tmp";
	const std::string savePath = "/tmp/mrfe-blockops-save-target.tmp";
	const std::string replaceSourcePath = "/tmp/mrfe-blockops-load-replace-source.tmp";
	const std::string blockText = "HELLO\n\nworld";
	const std::string replacementText = "X";
	MRFEArenaAllocator arena;
	MRFEArenaAllocator copiedPayload;
	MRFEBlockOps ops;
	MRFEBlockOps targetOps;
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
	MRFileEditor targetEditor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
	std::string errorText;
	std::string savedText;

	static_cast<void>(arena.assign(blockText));
	if (!arena.writeFile(sourcePath, &errorText)) {
		failureReason = "Unable to seed stream block load file: " + errorText;
		return false;
	}
	if (!editor.replaceBufferText("prefix suffix")) {
		failureReason = "Unable to seed editor for stream block load/save case.";
		return false;
	}
	editor.setCursorOffset(7);
	if (!ops.loadStreamBlockFromFile(editor, sourcePath, &errorText)) {
		failureReason = "Unable to load stream block from file: " + errorText;
		return false;
	}
	if (editor.snapshotText() != "prefix HELLO\n\nworldsuffix") {
		failureReason = "Loaded stream block text mismatch.";
		return false;
	}
	if (ops.geometry().mode != MRFEBlockMode::Stream || ops.geometry().status != MRFEBlockStatus::Committed || ops.geometry().rangeStart != 7 || ops.geometry().rangeEnd != 7 + blockText.size()) {
		failureReason = "Loaded stream block geometry mismatch.";
		return false;
	}
	if (!ops.saveStreamBlockToFile(editor, savePath, &errorText)) {
		failureReason = "Unable to save loaded stream block: " + errorText;
		return false;
	}
	if (!arena.loadFile(savePath, &errorText)) {
		failureReason = "Unable to read saved stream block: " + errorText;
		return false;
	}
	savedText.assign(arena.view());
	if (savedText != blockText) {
		failureReason = "Saved stream block content mismatch.";
		return false;
	}
	if (!ops.captureCurrentBlockPayload(editor, copiedPayload, &errorText)) {
		failureReason = "Unable to capture stream block payload through arena: " + errorText;
		return false;
	}
	if (!targetEditor.replaceBufferText("copy:")) {
		failureReason = "Unable to seed editor for arena copy primitive case.";
		return false;
	}
	targetEditor.setCursorOffset(5);
	if (!targetOps.insertPayloadAsStreamBlock(targetEditor, copiedPayload, &errorText)) {
		failureReason = "Unable to insert captured arena payload as stream block: " + errorText;
		return false;
	}
	if (targetEditor.snapshotText() != "copy:" + blockText || targetOps.geometry().mode != MRFEBlockMode::Stream || targetOps.geometry().rangeStart != 5 || targetOps.geometry().rangeEnd != 5 + copiedPayload.size()) {
		failureReason = "Arena capture/insert primitive mismatch.";
		return false;
	}

	static_cast<void>(arena.assign(replacementText));
	if (!arena.writeFile(replaceSourcePath, &errorText)) {
		failureReason = "Unable to seed stream block replacement load file: " + errorText;
		return false;
	}
	if (!editor.replaceBufferText("aa DELETE zz")) {
		failureReason = "Unable to seed editor for stream block replacement case.";
		return false;
	}
	editor.setSelectionOffsets(3, 9, False);
	editor.setCursorOffset(9);
	if (!ops.loadStreamBlockFromFile(editor, replaceSourcePath, &errorText)) {
		failureReason = "Unable to load stream block over selection: " + errorText;
		return false;
	}
	if (editor.snapshotText() != "aa X zz" || ops.geometry().rangeStart != 3 || ops.geometry().rangeEnd != 4) {
		failureReason = "Loaded stream block replacement mismatch.";
		return false;
	}

	if (!ops.beginColumn(editor) || !ops.end(editor)) {
		failureReason = "Unable to prepare non-stream block save rejection case.";
		return false;
	}
	if (ops.saveStreamBlockToFile(editor, savePath, &errorText) || errorText.find("Only stream blocks") == std::string::npos) {
		failureReason = "Column block save must be rejected.";
		return false;
	}
	static_cast<void>(std::remove(sourcePath.c_str()));
	static_cast<void>(std::remove(savePath.c_str()));
	static_cast<void>(std::remove(replaceSourcePath.c_str()));
	return true;
}

} // namespace

bool mrfeSeedMouseColumnStateForRegression(MRFileEditor &editor, int anchorColumn, int cursorColumn) {
	editor.mMouseSelectionColumnsValid = true;
	editor.mMouseSelectionAnchorColumn = anchorColumn;
	editor.mMouseSelectionCursorColumn = cursorColumn;
	return true;
}

MRFEArenaAllocator::MRFEArenaAllocator() : mStorage() {
}

void MRFEArenaAllocator::clear() noexcept {
	mStorage.clear();
}

bool MRFEArenaAllocator::assign(std::string_view text) {
	mStorage.assign(text.begin(), text.end());
	return true;
}

bool MRFEArenaAllocator::append(std::string_view text) {
	mStorage.insert(mStorage.end(), text.begin(), text.end());
	return true;
}

bool MRFEArenaAllocator::appendFill(std::size_t count, char value) {
	mStorage.insert(mStorage.end(), count, value);
	return true;
}

bool MRFEArenaAllocator::loadFile(const std::string &path, std::string *errorText) {
	std::ifstream in(path, std::ios::in | std::ios::binary | std::ios::ate);
	std::streamoff length = 0;

	clear();
	if (errorText != nullptr) errorText->clear();
	if (!in.is_open()) {
		if (errorText != nullptr) *errorText = "Unable to open block file: " + path;
		return false;
	}
	length = in.tellg();
	if (length < 0) {
		if (errorText != nullptr) *errorText = "Unable to read block file size: " + path;
		return false;
	}
	in.seekg(0, std::ios::beg);
	mStorage.resize(static_cast<std::size_t>(length));
	if (!mStorage.empty()) in.read(mStorage.data(), static_cast<std::streamsize>(mStorage.size()));
	if (!in.good() && !in.eof()) {
		clear();
		if (errorText != nullptr) *errorText = "Error while reading block file: " + path;
		return false;
	}
	logContents("load-file " + path);
	return true;
}

bool MRFEArenaAllocator::writeFile(const std::string &path, std::string *errorText) const {
	std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);

	if (errorText != nullptr) errorText->clear();
	if (!out.is_open()) {
		if (errorText != nullptr) *errorText = "Unable to create block file: " + path;
		return false;
	}
	if (!mStorage.empty()) out.write(mStorage.data(), static_cast<std::streamsize>(mStorage.size()));
	if (!out.good()) {
		if (errorText != nullptr) *errorText = "Error while writing block file: " + path;
		return false;
	}
	logContents("write-file " + path);
	return true;
}

std::vector<char> MRFEArenaAllocator::release() noexcept {
	std::vector<char> released;

	logContents("release");
	released.swap(mStorage);
	return released;
}

void MRFEArenaAllocator::logContents(std::string_view label) const {
	std::string line = "MRFE arena";

	if (!label.empty()) {
		line += " ";
		line += label;
	}
	line += " size=";
	line += std::to_string(mStorage.size());
	line += " payload=\"";
	line += escapedArenaPayload(view());
	line += "\"";
	mrLogMessage(line);
}

const char *MRFEArenaAllocator::data() const noexcept {
	return mStorage.empty() ? nullptr : mStorage.data();
}

std::size_t MRFEArenaAllocator::size() const noexcept {
	return mStorage.size();
}

bool MRFEArenaAllocator::empty() const noexcept {
	return mStorage.empty();
}

std::string_view MRFEArenaAllocator::view() const noexcept {
	if (mStorage.empty()) return std::string_view();
	return std::string_view(mStorage.data(), mStorage.size());
}

MRFEBlockOps::MRFEBlockOps() : mGeometry(), mArena() {
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

bool MRFEBlockOps::captureCurrentBlockPayload(MRFileEditor &editor, MRFEArenaAllocator &arena, std::string *errorText) {
	std::string text;

	if (errorText != nullptr) errorText->clear();
	arena.clear();
	if (!hasVisibleBlock()) {
		if (errorText != nullptr) *errorText = "No visible block marked.";
		return false;
	}
	if (mGeometry.mode != MRFEBlockMode::Stream) {
		if (errorText != nullptr) *errorText = "Only stream blocks can be saved to a file.";
		return false;
	}
	normalize(editor);
	text = editor.snapshotText();
	if (mGeometry.rangeStart > text.size() || mGeometry.rangeEnd > text.size() || mGeometry.rangeStart > mGeometry.rangeEnd) {
		if (errorText != nullptr) *errorText = "Stream block range is outside the editor buffer.";
		return false;
	}
	if (!arena.assign(std::string_view(text.data() + mGeometry.rangeStart, mGeometry.rangeEnd - mGeometry.rangeStart))) return false;
	arena.logContents("capture-current-stream-block");
	return true;
}

bool MRFEBlockOps::insertPayloadAsStreamBlock(MRFileEditor &editor, const MRFEArenaAllocator &arena, std::string *errorText) {
	std::size_t start = editor.selectionStartOffset();
	std::size_t end = editor.selectionEndOffset();
	const int paddingColumns = editor.hasTextSelection() ? 0 : editor.paddingColumnsBeforeInsertAtCursor();
	MRFEArenaAllocator insertArena;
	MRTextBufferModel::StagedTransaction transaction(editor.readSnapshot(), "load-stream-block");

	if (errorText != nullptr) errorText->clear();
	if (editor.isReadOnly()) {
		if (errorText != nullptr) *errorText = "Editor is read-only.";
		return false;
	}
	if (paddingColumns > 0) static_cast<void>(insertArena.appendFill(static_cast<std::size_t>(paddingColumns), ' '));
	static_cast<void>(insertArena.append(arena.view()));
	arena.logContents("insert-stream-source");
	insertArena.logContents("insert-stream-payload");
	if (end < start) std::swap(start, end);
	transaction.replace(MRTextBufferModel::Range(start, end), insertArena.view());
	if (!editor.applyStagedTransaction(transaction, start + insertArena.size(), start + insertArena.size(), start + insertArena.size(), true).applied()) {
		if (errorText != nullptr) *errorText = "Unable to insert block payload.";
		return false;
	}
	start += static_cast<std::size_t>(std::max(0, paddingColumns));
	return setCommittedStream(editor, start, start + arena.size());
}

bool MRFEBlockOps::copyCurrentBlockToCursor(MRFileEditor &editor, std::string *errorText) {
	return copyCurrentBlockToEditor(editor, *this, editor, 0, 0, errorText);
}

bool MRFEBlockOps::copyCurrentBlockToEditor(MRFileEditor &sourceEditor, MRFEBlockOps &targetOps, MRFileEditor &targetEditor, int sourceWindowId, int targetWindowId, std::string *errorText) {
	MRFEArenaAllocator transferArena;
	TransferMessage message;

	if (!prepareTransferMessage(sourceEditor, sourceWindowId, targetWindowId, TransferMode::Copy, transferArena, message, errorText)) return false;
	return targetOps.insertTransferMessage(targetEditor, message, errorText);
}

bool MRFEBlockOps::deleteCurrentBlock(MRFileEditor &editor, std::string *errorText) {
	std::string text;
	std::vector<std::size_t> starts;
	std::vector<MRTextBufferModel::Range> ranges;
	std::vector<ColumnLineReplacement> columnReplacements;
	std::size_t cursor = 0;
	MRTextBufferModel::StagedTransaction transaction(editor.readSnapshot(), "delete-block");

	if (errorText != nullptr) errorText->clear();
	if (editor.isReadOnly()) {
		if (errorText != nullptr) *errorText = "Editor is read-only.";
		return false;
	}
	if (!hasVisibleBlock()) {
		if (errorText != nullptr) *errorText = "No visible block marked.";
		return false;
	}
	normalize(editor);
	text = editor.snapshotText();
	starts = lineStartsForText(text);
	if (mGeometry.rangeStart > text.size() || mGeometry.rangeEnd > text.size() || mGeometry.rangeStart > mGeometry.rangeEnd) {
		if (errorText != nullptr) *errorText = "Block range is outside the editor buffer.";
		return false;
	}
	if (mGeometry.mode == MRFEBlockMode::Stream || mGeometry.mode == MRFEBlockMode::Line) {
		if (mGeometry.rangeStart < mGeometry.rangeEnd) ranges.push_back(MRTextBufferModel::Range(mGeometry.rangeStart, mGeometry.rangeEnd));
		cursor = mGeometry.rangeStart;
	} else if (mGeometry.mode == MRFEBlockMode::Column) {
		collectColumnEraseReplacements(text, starts, mGeometry, columnReplacements);
		cursor = mGeometry.rangeStart;
		if (!columnReplacements.empty()) cursor = columnReplacements.front().range.start;
	} else {
		if (errorText != nullptr) *errorText = "No block mode selected.";
		return false;
	}
	if (ranges.empty() && columnReplacements.empty()) {
		clear(editor);
		editor.setCursorOffset(cursor);
		return true;
	}
	for (std::size_t index = ranges.size(); index > 0; --index)
		transaction.erase(ranges[index - 1]);
	for (std::size_t index = columnReplacements.size(); index > 0; --index) {
		const ColumnLineReplacement &replacement = columnReplacements[index - 1];
		transaction.replace(replacement.range, replacement.text);
	}
	if (!editor.applyStagedTransaction(transaction, cursor, cursor, cursor, true).applied()) {
		if (errorText != nullptr) *errorText = "Unable to delete block.";
		return false;
	}
	mGeometry = MRFEBlockGeometry();
	deactivateVisual(editor);
	editor.setCursorOffset(cursor);
	return true;
}

bool MRFEBlockOps::loadStreamBlockFromFile(MRFileEditor &editor, const std::string &path, std::string *errorText) {
	if (errorText != nullptr) errorText->clear();
	if (!mArena.loadFile(path, errorText)) return false;
	return insertPayloadAsStreamBlock(editor, mArena, errorText);
}

bool MRFEBlockOps::saveStreamBlockToFile(MRFileEditor &editor, const std::string &path, std::string *errorText) {
	MRFEArenaAllocator arena;

	if (!captureCurrentBlockPayload(editor, arena, errorText)) return false;
	return arena.writeFile(path, errorText);
}

bool MRFEBlockOps::setCommittedStream(MRFileEditor &editor, std::size_t start, std::size_t end) {
	mGeometry = MRFEBlockGeometry();
	mGeometry.mode = MRFEBlockMode::Stream;
	mGeometry.status = MRFEBlockStatus::Committed;
	mGeometry.hidden = false;
	mGeometry.anchor = start;
	mGeometry.cursor = end;
	mGeometry.anchorColumn = std::max(0, static_cast<int>(editor.columnOfOffset(start)));
	mGeometry.cursorColumn = std::max(0, static_cast<int>(editor.columnOfOffset(end)));
	normalize(editor);
	applySelection(editor);
	applyOverlay(editor);
	return true;
}

bool MRFEBlockOps::captureTransferPayload(MRFileEditor &editor, MRFEArenaAllocator &arena, std::string *errorText) {
	std::string text;

	if (errorText != nullptr) errorText->clear();
	arena.clear();
	if (!hasVisibleBlock()) {
		if (errorText != nullptr) *errorText = "No visible block marked.";
		return false;
	}
	normalize(editor);
	text = editor.snapshotText();
	if (mGeometry.rangeStart > text.size() || mGeometry.rangeEnd > text.size() || mGeometry.rangeStart > mGeometry.rangeEnd) {
		if (errorText != nullptr) *errorText = "Block range is outside the editor buffer.";
		return false;
	}
	if (mGeometry.mode == MRFEBlockMode::Stream || mGeometry.mode == MRFEBlockMode::Line) {
		if (!arena.assign(std::string_view(text.data() + mGeometry.rangeStart, mGeometry.rangeEnd - mGeometry.rangeStart))) return false;
		arena.logContents(mGeometry.mode == MRFEBlockMode::Stream ? "capture-transfer-stream-block" : "capture-transfer-line-block");
		return true;
	}
	if (mGeometry.mode == MRFEBlockMode::Column) {
		const std::vector<std::size_t> starts = lineStartsForText(text);

		for (std::size_t line = mGeometry.line1; line <= mGeometry.line2 && line < starts.size(); ++line) {
			if (!appendColumnVisualCells(text, starts, line, mGeometry.col1, mGeometry.col2, arena)) return false;
		}
		arena.logContents("capture-transfer-column-block");
		return true;
	}
	if (errorText != nullptr) *errorText = "No block mode selected.";
	return false;
}

bool MRFEBlockOps::prepareTransferMessage(MRFileEditor &editor, int sourceWindowId, int targetWindowId, TransferMode mode, MRFEArenaAllocator &arena, TransferMessage &message, std::string *errorText) {
	message = TransferMessage();
	if (!captureTransferPayload(editor, arena, errorText)) return false;
	message.sourceWindowId = sourceWindowId;
	message.targetWindowId = targetWindowId;
	message.mode = mode;
	message.blockMode = mGeometry.mode;
	message.geometry = mGeometry;
	message.rowCount = (mGeometry.mode == MRFEBlockMode::Column || mGeometry.mode == MRFEBlockMode::Line) && mGeometry.line2 >= mGeometry.line1 ? mGeometry.line2 - mGeometry.line1 + 1 : 0;
	message.columnWidth = mGeometry.mode == MRFEBlockMode::Column && mGeometry.col2 > mGeometry.col1 ? static_cast<std::size_t>(mGeometry.col2 - mGeometry.col1) : 0;
	message.payload = arena.release();
	if (errorText != nullptr) errorText->clear();
	return true;
}

bool MRFEBlockOps::insertTransferMessage(MRFileEditor &editor, const TransferMessage &message, std::string *errorText) {
	const std::string_view payload = payloadView(message.payload);
	MRTextBufferModel::StagedTransaction transaction(editor.readSnapshot(), "copy-block");
	std::string working;
	std::vector<std::size_t> starts;
	std::size_t cursor = editor.cursorOffset();
	std::size_t rangeStart = cursor;
	std::size_t rangeEnd = cursor;

	if (errorText != nullptr) errorText->clear();
	if (editor.isReadOnly()) {
		if (errorText != nullptr) *errorText = "Editor is read-only.";
		return false;
	}
	if (message.blockMode == MRFEBlockMode::None) {
		if (errorText != nullptr) *errorText = "No block mode selected.";
		return false;
	}
	if (payload.empty()) {
		if (errorText != nullptr) *errorText = "Block payload is empty.";
		return false;
	}
	if (message.blockMode == MRFEBlockMode::Stream) {
		const int paddingColumns = editor.paddingColumnsBeforeInsertAtCursor();
		MRFEArenaAllocator insertArena;

		rangeStart = editor.cursorOffset();
		if (paddingColumns > 0) static_cast<void>(insertArena.appendFill(static_cast<std::size_t>(paddingColumns), ' '));
		static_cast<void>(insertArena.append(payload));
		transaction.insert(rangeStart, insertArena.view());
		cursor = rangeStart + insertArena.size();
		if (!editor.applyStagedTransaction(transaction, cursor, cursor, cursor, true).applied()) {
			if (errorText != nullptr) *errorText = "Unable to copy block.";
			return false;
		}
		rangeStart += static_cast<std::size_t>(std::max(0, paddingColumns));
		return setCommittedStream(editor, rangeStart, rangeStart + payload.size());
	}
	if (message.blockMode == MRFEBlockMode::Line) {
		working = editor.snapshotText();
		starts = lineStartsForText(working);
		const std::size_t targetLine = lineIndexForOffset(starts, editor.cursorOffset());
		rangeStart = starts.empty() ? 0 : starts[std::min(targetLine, starts.size() - 1)];
		rangeEnd = rangeStart + payload.size();
		transaction.insert(rangeStart, payload);
		cursor = rangeEnd;
		if (!editor.applyStagedTransaction(transaction, cursor, cursor, cursor, true).applied()) {
			if (errorText != nullptr) *errorText = "Unable to copy block.";
			return false;
		}
		mGeometry = MRFEBlockGeometry();
		mGeometry.mode = MRFEBlockMode::Line;
		mGeometry.status = MRFEBlockStatus::Committed;
		mGeometry.anchor = rangeStart;
		mGeometry.cursor = rangeEnd;
		mGeometry.rangeStart = rangeStart;
		mGeometry.rangeEnd = rangeEnd;
		mGeometry.line1 = targetLine;
		mGeometry.line2 = targetLine + (message.rowCount == 0 ? 0 : message.rowCount - 1);
		applySelection(editor);
		applyOverlay(editor);
		return true;
	}
	if (message.blockMode == MRFEBlockMode::Column) {
		working = editor.snapshotText();
		starts = lineStartsForText(working);
		const std::size_t targetLine = lineIndexForOffset(starts, editor.cursorOffset());
		const std::size_t rowCount = message.rowCount;
		const std::size_t width = message.columnWidth;
		const int destCol = std::max(0, editor.displayedCursorColumn());
		const std::string lineSeparator = lineSeparatorForText(working);

		if (rowCount == 0 || width == 0 || message.payload.size() < rowCount * width) {
			if (errorText != nullptr) *errorText = "Column block payload geometry is invalid.";
			return false;
		}
		for (std::size_t row = 0; row < rowCount; ++row) {
			std::size_t line = targetLine + row;
			starts = lineStartsForText(working);
			while (line >= starts.size()) {
				transaction.insert(working.size(), lineSeparator);
				working.insert(working.size(), lineSeparator);
				starts = lineStartsForText(working);
			}

			const std::size_t lineStart = starts[line];
			const std::size_t lineEnd = lineContentEndForIndex(working, starts, line);
			const std::string_view rowPayload = payloadRowView(message.payload, row, width);
			const std::string replacement = insertVisualColumnsIntoLine(std::string_view(working.data() + lineStart, lineEnd - lineStart), destCol, rowPayload);
			transaction.replace(MRTextBufferModel::Range(lineStart, lineEnd), replacement);
			working.replace(lineStart, lineEnd - lineStart, replacement);
		}
		starts = lineStartsForText(working);
		rangeStart = offsetAtLineVisualColumn(working, starts, targetLine, destCol);
		const std::size_t lastLine = targetLine + rowCount - 1;
		rangeEnd = offsetAtLineVisualColumn(working, starts, lastLine, destCol + static_cast<int>(width));
		cursor = rangeStart;
		if (!editor.applyStagedTransaction(transaction, cursor, cursor, cursor, true).applied()) {
			if (errorText != nullptr) *errorText = "Unable to copy block.";
			return false;
		}
		mGeometry = MRFEBlockGeometry();
		mGeometry.mode = MRFEBlockMode::Column;
		mGeometry.status = MRFEBlockStatus::Committed;
		mGeometry.anchor = rangeStart;
		mGeometry.cursor = rangeEnd;
		mGeometry.rangeStart = rangeStart;
		mGeometry.rangeEnd = rangeEnd;
		mGeometry.line1 = targetLine;
		mGeometry.line2 = lastLine;
		mGeometry.col1 = destCol;
		mGeometry.col2 = destCol + static_cast<int>(width);
		mGeometry.anchorColumn = destCol;
		mGeometry.cursorColumn = destCol + static_cast<int>(width);
		applySelection(editor);
		applyOverlay(editor);
		return true;
	}
	if (errorText != nullptr) *errorText = "No block mode selected.";
	return false;
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

	geometry = harnessNormalize(MRFEBlockMode::Line, "alpha\r\r_beta\rlast", 8, 1, 0, 0);
	if (!checkGeometry(geometry, MRFEBlockMode::Line, 0, 13, 0, 2, 0, 0, "CR-only line selection over empty line", failureReason)) return false;

	geometry = harnessNormalize(MRFEBlockMode::Line, "alpha\r\n\r\n_beta\r\nlast", 9, 1, 0, 0);
	if (!checkGeometry(geometry, MRFEBlockMode::Line, 0, 16, 0, 2, 0, 0, "CRLF line selection over empty line", failureReason)) return false;

	geometry = harnessNormalize(MRFEBlockMode::Column, "alpha\r\r_beta\rlast", 1, 8, 9, 3);
	if (!checkGeometry(geometry, MRFEBlockMode::Column, 0, 13, 0, 2, 3, 9, "CR-only column selection over empty line", failureReason)) return false;

	geometry = harnessNormalize(MRFEBlockMode::Column, "alpha\r\n\r\n_beta\r\nlast", 1, 9, 9, 3);
	if (!checkGeometry(geometry, MRFEBlockMode::Column, 0, 16, 0, 2, 3, 9, "CRLF column selection over empty line", failureReason)) return false;

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
	if (!runBlockCopyCase(MRFEBlockMode::Stream, "aa DELETE\n\nzz", 0, 3, 2, 0, 2, 2, "aa DELETE\n\nzzDELETE\n\n", "stream copy over empty line", failureReason)) return false;
	if (!runBlockCopyCase(MRFEBlockMode::Line, "one\n\ntwo\nlast", 0, 0, 1, 0, 3, 0, "one\n\ntwo\none\n\nlast", "line copy including empty line", failureReason)) return false;
	if (!runBlockCopyCase(MRFEBlockMode::Column, "012345\n\nabcdef\nXYZ", 0, 1, 2, 4, 3, 1, "012345\n\nabcdef\nX123YZ\n    \n bcd", "column copy over empty line with target extension", failureReason)) return false;
	if (!runBlockCopyCase(MRFEBlockMode::Column, "012345\n\nabcdef", 0, 1, 2, 4, 1, 0, "012345\n123\n   abcdef\nbcd", "column copy into LF target extension", failureReason)) return false;
	if (!runBlockCopyCase(MRFEBlockMode::Column, "012345\r\n\r\nabcdef", 0, 1, 2, 4, 1, 0, "012345\r\n123\r\n   abcdef\r\nbcd", "column copy into CRLF target extension", failureReason)) return false;
	if (!runInterWindowCopyCase(MRFEBlockMode::Stream, "aa COPY zz", 0, 3, 0, 7, "target:", 0, 7, "target:COPY", "inter-window stream copy", failureReason)) return false;
	if (!runInterWindowCopyCase(MRFEBlockMode::Line, "one\n\ntwo\nlast", 0, 0, 1, 0, "target\nend", 1, 0, "target\none\n\nend", "inter-window line copy", failureReason)) return false;
	if (!runInterWindowCopyCase(MRFEBlockMode::Column, "012345\n\nabcdef", 0, 1, 2, 4, "T0\nT1\nT2", 0, 1, "T1230\nT   1\nTbcd2", "inter-window column copy", failureReason)) return false;
	if (!runInterWindowCopyCase(MRFEBlockMode::Column, "ABCD\n\n12\nxyz987\n\nQ\nlast", 0, 1, 6, 5, "", 0, 0, "BCD \n    \n2   \nyz98\n    \n    \nast ", "inter-window tall sparse column copy into empty target", failureReason)) return false;
	if (!runInterWindowCopyCase(MRFEBlockMode::Column,
	                            "#include <stdio.h>\n\nint main() {\n\n\tint unused;\n         \n   puts(\"Hello world\");\n\n   puts(\"Hello world\");\n\t\n\t\n\t\n   \n\t\n\ti=\"dumm\";\n\treturn(0);\n}\r\n",
	                            2,
	                            4,
	                            15,
	                            20,
	                            "",
	                            0,
	                            0,
	                            "main() {        \n                \n int unused;    \n                \nuts(\"Hello world\n                \nuts(\"Hello world\n                \n                \n                \n                \n                \n i=\"dumm\";      \n return(0);     ",
	                            "inter-window column copy from tabs and mixed line endings",
	                            failureReason))
		return false;
	if (!runBlockDeleteCase(MRFEBlockMode::Stream, "aa DELETE\n\nzz", 0, 3, 2, 0, "aa zz", "stream delete over empty line", failureReason)) return false;
	if (!runBlockDeleteCase(MRFEBlockMode::Line, "one\n\ntwo\nlast", 0, 0, 1, 0, "two\nlast", "line delete including empty line", failureReason)) return false;
	if (!runBlockDeleteCase(MRFEBlockMode::Column, "012345\n\nabcdef\nXYZ", 0, 1, 2, 4, "045\n\naef\nXYZ", "LF column delete over empty line", failureReason)) return false;
	if (!runBlockDeleteCase(MRFEBlockMode::Column, "\tint unused;\n\treturn(0);", 0, 4, 1, 20, "    \n    ", "column delete across tab indentation", failureReason)) return false;
	if (!runBlockDeleteCase(MRFEBlockMode::Column, "012345\r\rabcdef\rXYZ", 0, 1, 2, 4, "045\r\raef\rXYZ", "CR-only column delete over empty line", failureReason)) return false;
	if (!runBlockDeleteCase(MRFEBlockMode::Column, "012345\r\n\r\nabcdef\r\nXYZ", 0, 1, 2, 4, "045\r\n\r\naef\r\nXYZ", "CRLF column delete over empty line", failureReason)) return false;
	if (!runStreamBlockLoadSaveCase(failureReason)) return false;
	{
		MRFEArenaAllocator releaseArena;
		std::vector<char> released;

		static_cast<void>(releaseArena.assign("owned"));
		released = releaseArena.release();
		if (!releaseArena.empty() || std::string(released.begin(), released.end()) != "owned") {
			failureReason = "Arena release must move payload ownership and leave the arena empty.";
			return false;
		}
	}
	{
		const std::string streamText = "HELLO\n\nworld";
		MRFileEditor streamEditor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
		MRFEBlockOps streamOps;
		MRFEArenaAllocator transferArena;
		MRFEBlockOps::TransferMessage message;
		std::string errorText;

		if (!streamEditor.replaceBufferText(streamText.c_str())) {
			failureReason = "Unable to seed stream transfer editor.";
			return false;
		}
		streamEditor.setCursorOffset(0);
		if (!streamOps.beginStream(streamEditor)) {
			failureReason = "Unable to begin stream transfer block.";
			return false;
		}
		streamEditor.setCursorOffset(streamText.size());
		if (!streamOps.end(streamEditor)) {
			failureReason = "Unable to commit stream transfer block.";
			return false;
		}
		if (!streamOps.prepareTransferMessage(streamEditor, 10, 20, MRFEBlockOps::TransferMode::Copy, transferArena, message, &errorText)) {
			failureReason = "Unable to prepare stream transfer message: " + errorText;
			return false;
		}
		if (!transferArena.empty() || message.sourceWindowId != 10 || message.targetWindowId != 20 || message.mode != MRFEBlockOps::TransferMode::Copy || message.blockMode != MRFEBlockMode::Stream || std::string(message.payload.begin(), message.payload.end()) != streamText) {
			failureReason = "Stream transfer message must own arena payload after handoff.";
			return false;
		}
	}
	{
		const std::string columnText = "ab\n\nabcdef";
		const std::vector<std::size_t> columnStarts = lineStartsForText(columnText);
		MRFileEditor columnEditor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
		MRFEBlockOps columnOps;
		MRFEArenaAllocator transferArena;
		MRFEBlockOps::TransferMessage message;
		std::string errorText;

		if (!columnEditor.replaceBufferText(columnText.c_str())) {
			failureReason = "Unable to seed column transfer editor.";
			return false;
		}
		placeEditorCursor(columnEditor, columnText, columnStarts, 0, 1);
		if (!columnOps.beginColumn(columnEditor)) {
			failureReason = "Unable to begin column transfer block.";
			return false;
		}
		placeEditorCursor(columnEditor, columnText, columnStarts, 2, 4);
		if (!columnOps.end(columnEditor)) {
			failureReason = "Unable to commit column transfer block.";
			return false;
		}
		if (!columnOps.prepareTransferMessage(columnEditor, 30, 40, MRFEBlockOps::TransferMode::Move, transferArena, message, &errorText)) {
			failureReason = "Unable to prepare column transfer message: " + errorText;
			return false;
		}
		if (!transferArena.empty() || message.sourceWindowId != 30 || message.targetWindowId != 40 || message.mode != MRFEBlockOps::TransferMode::Move || message.blockMode != MRFEBlockMode::Column || message.rowCount != 3 || message.columnWidth != 3 || std::string(message.payload.begin(), message.payload.end()) != "b     bcd") {
			failureReason = "Column transfer message must own fixed-width arena payload including empty lines.";
			return false;
		}
	}
	{
		const std::string columnText = "ab\r\rabcdef";
		const std::vector<std::size_t> columnStarts = lineStartsForText(columnText);
		MRFileEditor columnEditor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
		MRFEBlockOps columnOps;
		MRFEArenaAllocator transferArena;
		MRFEBlockOps::TransferMessage message;
		std::string errorText;

		if (!columnEditor.replaceBufferText(columnText.c_str())) {
			failureReason = "Unable to seed CR-only column transfer editor.";
			return false;
		}
		placeEditorCursor(columnEditor, columnText, columnStarts, 0, 1);
		if (!columnOps.beginColumn(columnEditor)) {
			failureReason = "Unable to begin CR-only column transfer block.";
			return false;
		}
		placeEditorCursor(columnEditor, columnText, columnStarts, 2, 4);
		if (!columnOps.end(columnEditor)) {
			failureReason = "Unable to commit CR-only column transfer block.";
			return false;
		}
		if (!columnOps.prepareTransferMessage(columnEditor, 31, 41, MRFEBlockOps::TransferMode::Copy, transferArena, message, &errorText)) {
			failureReason = "Unable to prepare CR-only column transfer message: " + errorText;
			return false;
		}
		if (!transferArena.empty() || message.rowCount != 3 || message.columnWidth != 3 || std::string(message.payload.begin(), message.payload.end()) != "b     bcd") {
			failureReason = "CR-only column transfer message must ignore separator bytes as visible cells.";
			return false;
		}
	}
	{
		const std::string columnText = "ab\r\n\r\nabcdef";
		const std::vector<std::size_t> columnStarts = lineStartsForText(columnText);
		MRFileEditor columnEditor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
		MRFEBlockOps columnOps;
		MRFEArenaAllocator transferArena;
		MRFEBlockOps::TransferMessage message;
		std::string errorText;

		if (!columnEditor.replaceBufferText(columnText.c_str())) {
			failureReason = "Unable to seed CRLF column transfer editor.";
			return false;
		}
		placeEditorCursor(columnEditor, columnText, columnStarts, 0, 1);
		if (!columnOps.beginColumn(columnEditor)) {
			failureReason = "Unable to begin CRLF column transfer block.";
			return false;
		}
		placeEditorCursor(columnEditor, columnText, columnStarts, 2, 4);
		if (!columnOps.end(columnEditor)) {
			failureReason = "Unable to commit CRLF column transfer block.";
			return false;
		}
		if (!columnOps.prepareTransferMessage(columnEditor, 32, 42, MRFEBlockOps::TransferMode::Copy, transferArena, message, &errorText)) {
			failureReason = "Unable to prepare CRLF column transfer message: " + errorText;
			return false;
		}
		if (!transferArena.empty() || message.rowCount != 3 || message.columnWidth != 3 || std::string(message.payload.begin(), message.payload.end()) != "b     bcd") {
			failureReason = "CRLF column transfer message must ignore separator bytes as visible cells.";
			return false;
		}
	}
	if (!runEditorMouseAdoptionCase(failureReason)) return false;
	if (!runEditorMouseDragCaseForMode(MRFEBlockMode::Stream, "raw mouse left-button drag stream over empty line", failureReason)) return false;
	if (!runEditorMouseDragCaseForMode(MRFEBlockMode::Stream, "raw mouse both-buttons drag stream over empty line", failureReason, static_cast<uchar>(mbLeftButton | mbRightButton), 0)) return false;
	if (!runEditorMouseDragCaseForMode(MRFEBlockMode::Column, "raw mouse right-button drag column over empty line", failureReason)) return false;
	if (!runEditorMouseDragCaseForMode(MRFEBlockMode::Line, "raw mouse ctrl-alt drag line over empty line", failureReason)) return false;
	if (!runEditorMouseDragReplacesExistingBlockCase(failureReason)) return false;
	if (!runEditorMouseClickPreservesExistingBlockCase(failureReason)) return false;

	failureReason.clear();
	return true;
}
