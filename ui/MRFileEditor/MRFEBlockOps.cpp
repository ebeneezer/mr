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

class ScopedEditSetupSettings {
  public:
	explicit ScopedEditSetupSettings(const MREditSetupSettings &settings) : mPrevious(configuredEditSetupSettings()) {
		static_cast<void>(setConfiguredEditSetupSettings(settings));
	}

	~ScopedEditSetupSettings() {
		static_cast<void>(setConfiguredEditSetupSettings(mPrevious));
	}

  private:
	MREditSetupSettings mPrevious;
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

std::string clearVisualColumnsFromLine(std::string_view lineText, int col1, int col2) {
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
			const int emittedStart = charStart;
			const int emittedEnd = charEnd;

			result.append(static_cast<std::size_t>(emittedEnd - emittedStart), ' ');
		}
		visual = charEnd;
		offset = next;
	}
	if (visual < toColumn) {
		if (visual < fromColumn) {
			result.append(static_cast<std::size_t>(fromColumn - visual), ' ');
			visual = fromColumn;
		}
		result.append(static_cast<std::size_t>(toColumn - visual), ' ');
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

std::string replaceVisualColumnsInLine(std::string_view lineText, int destCol, std::size_t width, std::string_view payload) {
	const int targetColumn = std::max(destCol, 0);
	std::string erased = eraseVisualColumnsFromLine(lineText, targetColumn, targetColumn + static_cast<int>(width));
	return insertVisualColumnsIntoLine(erased, targetColumn, payload);
}

std::size_t nextTextCharOffset(const std::string &text, std::size_t pos) noexcept {
	char bytes[4];
	std::size_t count = 0;

	if (pos >= text.size()) return text.size();
	if (text[pos] == '\r' && pos + 1 < text.size() && text[pos + 1] == '\n') return std::min(text.size(), pos + 2);
	for (; count < sizeof(bytes) && pos + count < text.size(); ++count)
		bytes[count] = text[pos + count];
	const std::size_t step = TText::next(TStringView(bytes, count));
	return std::min(text.size(), pos + std::max<std::size_t>(step, 1));
}

std::size_t overwriteEndForStreamPayload(const std::string &text, const std::vector<std::size_t> &starts, std::size_t start, std::size_t payloadSize) {
	const std::size_t line = lineIndexForOffset(starts, start);
	const std::size_t lineEnd = lineContentEndForIndex(text, starts, line);
	std::size_t end = std::min(start, text.size());

	for (std::size_t i = 0; i < payloadSize && end < lineEnd; ++i)
		end = nextTextCharOffset(text, end);
	return end;
}

std::size_t overwriteEndForLinePayload(const std::string &text, const std::vector<std::size_t> &starts, std::size_t targetLine, std::size_t rowCount) {
	if (starts.empty() || targetLine >= starts.size() || rowCount == 0) return text.size();
	const std::size_t afterLine = targetLine + rowCount;
	if (afterLine < starts.size()) return starts[afterLine];
	return text.size();
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

void collectColumnClearReplacements(const std::string &text, const std::vector<std::size_t> &starts, const MRFEBlockGeometry &geometry, std::vector<ColumnLineReplacement> &replacements) {
	const std::size_t firstLine = std::min(geometry.line1, starts.empty() ? 0 : starts.size() - 1);
	const std::size_t lastLine = std::min(geometry.line2, starts.empty() ? 0 : starts.size() - 1);

	replacements.clear();
	if (starts.empty() || geometry.col2 <= geometry.col1) return;
	for (std::size_t line = firstLine; line <= lastLine; ++line) {
		const std::size_t lineStart = starts[line];
		const std::size_t lineEnd = lineContentEndForIndex(text, starts, line);
		const std::string replacement = clearVisualColumnsFromLine(std::string_view(text.data() + lineStart, lineEnd - lineStart), geometry.col1, geometry.col2);

		if (replacement.size() != lineEnd - lineStart || replacement != std::string_view(text.data() + lineStart, lineEnd - lineStart))
			replacements.push_back(ColumnLineReplacement{MRTextBufferModel::Range(lineStart, lineEnd), replacement});
	}
}

void stageColumnLineReplacements(MRTextBufferModel::StagedTransaction &transaction, const std::vector<ColumnLineReplacement> &replacements) {
	for (std::size_t index = replacements.size(); index > 0; --index) {
		const ColumnLineReplacement &replacement = replacements[index - 1];
		transaction.replace(replacement.range, replacement.text);
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

bool checkEditorVisibleBlockMode(MRFileEditor &editor, const MRFEBlockOps &ops, MRFEBlockMode mode, const char *phase, std::string &failureReason) {
	const MRFileEditor::BlockOverlayState overlay = editor.blockOverlayState();

	if (!ops.hasVisibleBlock() || ops.mode() != mode) {
		failureReason = std::string("Visible block mode mismatch in ") + phase + ": got visible=" + std::to_string(ops.hasVisibleBlock() ? 1 : 0) + " mode=" + std::to_string(static_cast<int>(ops.mode())) + ".";
		return false;
	}
	if (!overlay.active || overlay.mode != static_cast<int>(mode)) {
		failureReason = std::string("Visible block overlay mode mismatch in ") + phase + ".";
		return false;
	}
	if (editor.hasTextSelection()) {
		failureReason = std::string("Visible block must not leave editor text selection active in ") + phase + ".";
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

void sendWindowKeyEvent(MREditWindow &window, ushort keyCode, ushort modifiers = 0) {
	TEvent event = makeKeyEvent(keyCode, modifiers);
	window.handleEvent(event);
}

void sendEditorTextInput(MRFileEditor &editor, char ch) {
	TEvent event{};
	event.what = evKeyDown;
	event.keyDown.keyCode = static_cast<ushort>(ch);
	event.keyDown.charScan.charCode = ch;
	event.keyDown.textLength = 1;
	event.keyDown.text[0] = ch;
	editor.handleEvent(event);
}

void sendWindowTextInput(MREditWindow &window, char ch) {
	TEvent event{};
	event.what = evKeyDown;
	event.keyDown.keyCode = static_cast<ushort>(ch);
	event.keyDown.charScan.charCode = ch;
	event.keyDown.textLength = 1;
	event.keyDown.text[0] = ch;
	window.handleEvent(event);
}

bool checkBlockDeleteUndoRedo(MRFileEditor &editor, MRFEBlockOps &ops, const std::string &originalText, const std::string &deletedText, const char *phase, std::string &failureReason) {
	std::string errorText;

	if (!ops.deleteCurrentBlock(editor, &errorText)) {
		failureReason = std::string("Unable to delete block in ") + phase + ": " + errorText;
		return false;
	}
	if (editor.snapshotText() != deletedText) {
		failureReason = std::string("Deleted text mismatch in ") + phase + ": got=\"" + escapedArenaPayload(editor.snapshotText()) + "\" expected=\"" + escapedArenaPayload(deletedText) + "\".";
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
	if (!checkEditorVisibleBlockMode(editor, ops, mode, phase, failureReason)) return false;
	if (mode == MRFEBlockMode::Column && !checkEditorBlock(editor, ops, MRFEBlockMode::Column, MRFEBlockStatus::Committed, expectedLine1, expectedLine2, expectedCol1, expectedCol2, phase, failureReason)) return false;
	sendEditorCommand(editor, cmMrEditUndo);
	if (editor.snapshotText() != originalText) {
		failureReason = std::string("Single undo must restore full block copy in ") + phase + ".";
		return false;
	}
	if (!checkEditorVisibleBlockMode(editor, ops, mode, phase, failureReason)) return false;
	sendEditorCommand(editor, cmMrEditRedo);
	if (editor.snapshotText() != copiedText) {
		failureReason = std::string("Single redo must reapply full block copy in ") + phase + ".";
		return false;
	}
	if (!checkEditorVisibleBlockMode(editor, ops, mode, phase, failureReason)) return false;
	return true;
}

bool checkBlockMoveUndoRedo(MRFileEditor &editor, MRFEBlockOps &ops, MRFEBlockMode mode, std::size_t expectedLine1, std::size_t expectedLine2, int expectedCol1, int expectedCol2, const std::string &originalText, const std::string &movedText, const char *phase, std::string &failureReason) {
	std::string errorText;

	if (!ops.moveCurrentBlockToCursor(editor, &errorText)) {
		failureReason = std::string("Unable to move block in ") + phase + ": " + errorText;
		return false;
	}
	if (editor.snapshotText() != movedText) {
		failureReason = std::string("Moved text mismatch in ") + phase + ": got=\"" + escapedArenaPayload(editor.snapshotText()) + "\" expected=\"" + escapedArenaPayload(movedText) + "\".";
		return false;
	}
	if (!checkEditorVisibleBlockMode(editor, ops, mode, phase, failureReason)) return false;
	if (mode == MRFEBlockMode::Column && !checkEditorBlock(editor, ops, MRFEBlockMode::Column, MRFEBlockStatus::Committed, expectedLine1, expectedLine2, expectedCol1, expectedCol2, phase, failureReason)) return false;
	sendEditorCommand(editor, cmMrEditUndo);
	if (editor.snapshotText() != originalText) {
		failureReason = std::string("Single undo must restore full block move in ") + phase + ".";
		return false;
	}
	if (!checkEditorVisibleBlockMode(editor, ops, mode, phase, failureReason)) return false;
	sendEditorCommand(editor, cmMrEditRedo);
	if (editor.snapshotText() != movedText) {
		failureReason = std::string("Single redo must reapply full block move in ") + phase + ".";
		return false;
	}
	if (!checkEditorVisibleBlockMode(editor, ops, mode, phase, failureReason)) return false;
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

bool checkInterWindowMoveUndoRedo(MREditWindow &source, MREditWindow &target, const std::string &sourceOriginalText, const std::string &sourceMovedText, const std::string &targetOriginalText, const std::string &targetMovedText, const char *phase, std::string &failureReason) {
	std::string errorText;
	MRFileEditor *sourceEditor = source.getEditor();
	MRFileEditor *targetEditor = target.getEditor();

	if (sourceEditor == nullptr || targetEditor == nullptr) {
		failureReason = std::string("Missing editor in ") + phase + ".";
		return false;
	}
	if (!source.moveBlockTo(target, &errorText)) {
		failureReason = std::string("Unable to move block between windows in ") + phase + ": " + errorText;
		return false;
	}
	if (sourceEditor->snapshotText() != sourceMovedText || targetEditor->snapshotText() != targetMovedText) {
		failureReason = std::string("Inter-window moved text mismatch in ") + phase + ": source=\"" + escapedArenaPayload(sourceEditor->snapshotText()) + "\" target=\"" + escapedArenaPayload(targetEditor->snapshotText()) + "\".";
		return false;
	}
	sendEditorCommand(*targetEditor, cmMrEditUndo);
	if (targetEditor->snapshotText() != targetOriginalText) {
		failureReason = std::string("Single target undo must restore inter-window move target in ") + phase + ".";
		return false;
	}
	sendEditorCommand(*sourceEditor, cmMrEditUndo);
	if (sourceEditor->snapshotText() != sourceOriginalText) {
		failureReason = std::string("Single source undo must restore inter-window move source in ") + phase + ".";
		return false;
	}
	sendEditorCommand(*sourceEditor, cmMrEditRedo);
	sendEditorCommand(*targetEditor, cmMrEditRedo);
	if (sourceEditor->snapshotText() != sourceMovedText || targetEditor->snapshotText() != targetMovedText) {
		failureReason = std::string("Single redo per window must reapply inter-window move in ") + phase + ".";
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

bool runWindowCopyUndoPreservesBlockStateCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = "aa COPY zz\nend";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MREditWindow window(TRect(0, 0, 80, 16), "window", 2201);
	MRFileEditor *editor = window.getEditor();
	std::string errorText;

	if (editor == nullptr) {
		failureReason = "Unable to create editor in window copy undo block-state case.";
		return false;
	}
	if (!editor->replaceBufferText(text.c_str())) {
		failureReason = "Unable to seed editor in window copy undo block-state case.";
		return false;
	}
	placeEditorCursor(*editor, text, starts, 0, 3);
	window.beginStreamBlock();
	placeEditorCursor(*editor, text, starts, 0, 7);
	window.endBlock();
	placeEditorCursor(*editor, text, starts, 1, 3);
	if (!window.copyBlock(&errorText)) {
		failureReason = "Unable to copy block in window copy undo block-state case: " + errorText;
		return false;
	}
	if (!window.hasBlock() || !editor->blockOverlayState().active) {
		failureReason = "Copied block must be visible before undo in window copy undo block-state case.";
		return false;
	}
	sendEditorCommand(*editor, cmMrEditUndo);
	if (editor->snapshotText() != text) {
		failureReason = "Undo must restore text after window copy block operation.";
		return false;
	}
	window.refreshBlockVisual();
	if (!window.hasBlock() || !editor->blockOverlayState().active) {
		failureReason = "Undo after window copy must preserve stored BlockOps state and overlay.";
		return false;
	}
	return true;
}

bool runWindowDeleteUndoPreservesBlockStateCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	std::string errorText;

	{
		const std::string text = "aa DELETE zz\nend";
		const std::vector<std::size_t> starts = lineStartsForText(text);
		MREditWindow window(TRect(0, 0, 80, 16), "window", 2202);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr) {
			failureReason = "Unable to create editor in window stream delete undo block-state case.";
			return false;
		}
		if (!editor->replaceBufferText(text.c_str())) {
			failureReason = "Unable to seed editor in window stream delete undo block-state case.";
			return false;
		}
		placeEditorCursor(*editor, text, starts, 0, 3);
		window.beginStreamBlock();
		placeEditorCursor(*editor, text, starts, 0, 9);
		window.endBlock();
		if (!window.deleteBlock(&errorText)) {
			failureReason = "Unable to delete block in window stream delete undo block-state case: " + errorText;
			return false;
		}
		if (editor->snapshotText() != "aa  zz\nend") {
			failureReason = "Deleted text mismatch in window stream delete undo block-state case.";
			return false;
		}
		sendEditorCommand(*editor, cmMrEditUndo);
		if (editor->snapshotText() != text) {
			failureReason = "Undo must restore text after window stream delete block operation: got=\"" + escapedArenaPayload(editor->snapshotText()) + "\" expected=\"" + escapedArenaPayload(text) + "\".";
			return false;
		}
		window.refreshBlockVisual();
		if (!window.hasBlock() || window.blockStatus() != MREditWindow::bmStream || !editor->blockOverlayState().active || editor->hasTextSelection()) {
			failureReason = "Undo after window stream delete must preserve stored BlockOps state and overlay while keeping editor text selection collapsed.";
			return false;
		}
	}
	{
		const std::string text = "one\ntwo\nlast";
		MREditWindow window(TRect(0, 0, 80, 16), "window-line-delete", 2207);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr) {
			failureReason = "Unable to create editor in window line delete undo block-state case.";
			return false;
		}
		if (!editor->replaceBufferText(text.c_str())) {
			failureReason = "Unable to seed editor in window line delete undo block-state case.";
			return false;
		}
		editor->setCursorOffset(0);
		window.beginLineBlock();
		window.endBlock();
		if (!window.deleteBlock(&errorText)) {
			failureReason = "Unable to delete line block in window delete undo block-state case: " + errorText;
			return false;
		}
		if (editor->snapshotText() != "two\nlast") {
			failureReason = "Deleted line text mismatch in window delete undo block-state case.";
			return false;
		}
		sendEditorCommand(*editor, cmMrEditUndo);
		if (editor->snapshotText() != text) {
			failureReason = "Undo must restore text after window line delete block operation: got=\"" + escapedArenaPayload(editor->snapshotText()) + "\" expected=\"" + escapedArenaPayload(text) + "\".";
			return false;
		}
		window.refreshBlockVisual();
		if (!window.hasBlock() || window.blockStatus() != MREditWindow::bmLine || !editor->blockOverlayState().active || editor->hasTextSelection()) {
			failureReason = "Undo after window line delete must preserve stored BlockOps state and overlay while keeping editor text selection collapsed.";
			return false;
		}
	}
	{
		const std::string text = "012345\nabcdef\nXYZ";
		const std::vector<std::size_t> starts = lineStartsForText(text);
		MREditWindow window(TRect(0, 0, 80, 16), "window-column-delete", 2208);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr) {
			failureReason = "Unable to create editor in window column delete undo block-state case.";
			return false;
		}
		if (!editor->replaceBufferText(text.c_str())) {
			failureReason = "Unable to seed editor in window column delete undo block-state case.";
			return false;
		}
		placeEditorCursor(*editor, text, starts, 0, 1);
		window.beginColumnBlock();
		placeEditorCursor(*editor, text, starts, 1, 4);
		window.endBlock();
		if (!window.deleteBlock(&errorText)) {
			failureReason = "Unable to delete column block in window delete undo block-state case: " + errorText;
			return false;
		}
		if (editor->snapshotText() == text) {
			failureReason = "Column delete text did not change in window delete undo block-state case.";
			return false;
		}
		sendEditorCommand(*editor, cmMrEditUndo);
		if (editor->snapshotText() != text) {
			failureReason = "Undo must restore text after window column delete block operation: got=\"" + escapedArenaPayload(editor->snapshotText()) + "\" expected=\"" + escapedArenaPayload(text) + "\".";
			return false;
		}
		window.refreshBlockVisual();
		if (!window.hasBlock() || window.blockStatus() != MREditWindow::bmColumn || !editor->blockOverlayState().active || editor->hasTextSelection()) {
			failureReason = "Undo after window column delete must preserve stored BlockOps state and overlay while keeping editor text selection collapsed.";
			return false;
		}
	}
	{
		const std::string text =
		    "#include <stdio.h>\n\nint main() {\n\n\tint unused;\n         \n   puts(\"Hello world\");\n\n   puts(\"Hello world\");\n\t\n\t\n\t\n   \n\t\n\ti=\"dumm\";\n\treturn(0);\n}\r\n";
		const std::vector<std::size_t> starts = lineStartsForText(text);
		MREditWindow window(TRect(0, 0, 80, 24), "window-column-delete-ctrlz-testsnippet", 2209);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr) {
			failureReason = "Unable to create editor in testsnippet column delete Ctrl-Z block-state case.";
			return false;
		}
		if (!window.replaceTextBuffer(text.c_str(), "window-column-delete-ctrlz-testsnippet")) {
			failureReason = "Unable to seed editor in testsnippet column delete Ctrl-Z block-state case.";
			return false;
		}
		placeEditorCursor(*editor, text, starts, 2, 4);
		window.beginColumnBlock();
		placeEditorCursor(*editor, text, starts, 15, 20);
		window.endBlock();
		if (!window.hasBlock() || window.blockStatus() != MREditWindow::bmColumn || window.blockLine1() != 3 || window.blockLine2() != 16 || window.blockCol1() != 5 || window.blockCol2() != 21) {
			failureReason = "Testsnippet column block geometry mismatch before delete.";
			return false;
		}
		if (!window.deleteBlock(&errorText)) {
			failureReason = "Unable to delete testsnippet column block before Ctrl-Z: " + errorText;
			return false;
		}
		if (editor->snapshotText() == text) {
			failureReason = "Testsnippet column delete must mutate text before Ctrl-Z.";
			return false;
		}
		sendWindowKeyEvent(window, kbCtrlZ);
		if (editor->snapshotText() != text) {
			failureReason = "Ctrl-Z must restore testsnippet text after column delete: got=\"" + escapedArenaPayload(editor->snapshotText()) + "\" expected=\"" + escapedArenaPayload(text) + "\".";
			return false;
		}
		window.refreshBlockVisual();
		if (!window.hasBlock() || window.blockStatus() != MREditWindow::bmColumn || window.blockLine1() != 3 || window.blockLine2() != 16 || window.blockCol1() != 5 || window.blockCol2() != 21 || !editor->blockOverlayState().active || editor->hasTextSelection()) {
			failureReason = "Ctrl-Z after testsnippet column delete must preserve the visible column block without text selection.";
			return false;
		}
	}
	return true;
}

bool runWindowCtrlZBlockOpsMatrixCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	MREditSetupSettings settings = configuredEditSetupSettings();
	settings.columnBlockMove = "DELETE_SPACE";
	ScopedEditSetupSettings scopedSettings(settings);
	std::string errorText;

	{
		const std::string text = "aa COPY zz\nend";
		const std::vector<std::size_t> starts = lineStartsForText(text);
		MREditWindow window(TRect(0, 0, 80, 16), "ctrlz-stream-copy", 2301);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr || !window.replaceTextBuffer(text.c_str(), "ctrlz-stream-copy")) {
			failureReason = "Unable to seed stream copy Ctrl-Z matrix case.";
			return false;
		}
		placeEditorCursor(*editor, text, starts, 0, 3);
		window.beginStreamBlock();
		placeEditorCursor(*editor, text, starts, 0, 7);
		window.endBlock();
		placeEditorCursor(*editor, text, starts, 1, 3);
		if (!window.copyBlock(&errorText)) {
			failureReason = "Unable to copy stream block in Ctrl-Z matrix: " + errorText;
			return false;
		}
		if (editor->snapshotText() != "aa COPY zz\nendCOPY") {
			failureReason = "Stream copy Ctrl-Z matrix produced wrong pre-undo text.";
			return false;
		}
		sendWindowKeyEvent(window, kbCtrlZ);
		if (editor->snapshotText() != text || !window.hasBlock() || window.blockStatus() != MREditWindow::bmStream || editor->hasTextSelection()) {
			failureReason = "Ctrl-Z after stream copy must restore text and preserve stream block.";
			return false;
		}
		sendEditorCommand(*editor, cmMrEditRedo);
		if (editor->snapshotText() != "aa COPY zz\nendCOPY" || !window.hasBlock() || window.blockStatus() != MREditWindow::bmStream || editor->hasTextSelection()) {
			failureReason = "Redo after stream copy undo must restore copied text and preserve stream block.";
			return false;
		}
	}
	{
		const std::string text = "one\ntwo\nlast";
		MREditWindow window(TRect(0, 0, 80, 16), "ctrlz-line-copy", 2302);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr || !window.replaceTextBuffer(text.c_str(), "ctrlz-line-copy")) {
			failureReason = "Unable to seed line copy Ctrl-Z matrix case.";
			return false;
		}
		editor->setCursorOffset(0);
		window.beginLineBlock();
		window.endBlock();
		editor->setCursorOffset(editor->nextLineOffset(editor->nextLineOffset(0)));
		if (!window.copyBlock(&errorText)) {
			failureReason = "Unable to copy line block in Ctrl-Z matrix: " + errorText;
			return false;
		}
		if (editor->snapshotText() != "one\ntwo\none\nlast") {
			failureReason = "Line copy Ctrl-Z matrix produced wrong pre-undo text.";
			return false;
		}
		sendWindowKeyEvent(window, kbCtrlZ);
		if (editor->snapshotText() != text || !window.hasBlock() || window.blockStatus() != MREditWindow::bmLine || editor->hasTextSelection()) {
			failureReason = "Ctrl-Z after line copy must restore text and preserve line block.";
			return false;
		}
		sendEditorCommand(*editor, cmMrEditRedo);
		if (editor->snapshotText() != "one\ntwo\none\nlast" || !window.hasBlock() || window.blockStatus() != MREditWindow::bmLine || editor->hasTextSelection()) {
			failureReason = "Redo after line copy undo must restore copied text and preserve line block.";
			return false;
		}
	}
	{
		const std::string text = "012345\nabcdef\nXYZ";
		MREditWindow window(TRect(0, 0, 80, 16), "ctrlz-column-copy", 2303);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr || !window.replaceTextBuffer(text.c_str(), "ctrlz-column-copy")) {
			failureReason = "Unable to seed column copy Ctrl-Z matrix case.";
			return false;
		}
		const std::size_t secondLine = editor->nextLineOffset(0);
		const std::size_t thirdLine = editor->nextLineOffset(secondLine);
		editor->setCursorOffsetAtVisualColumn(1, 1);
		window.beginColumnBlock();
		editor->setCursorOffsetAtVisualColumn(secondLine + 4, 4);
		window.endBlock();
		editor->setCursorOffsetAtVisualColumn(thirdLine + 1, 1);
		if (!window.copyBlock(&errorText)) {
			failureReason = "Unable to copy column block in Ctrl-Z matrix: " + errorText;
			return false;
		}
		if (editor->snapshotText() != "012345\nabcdef\nX123YZ\n bcd") {
			failureReason = "Column copy Ctrl-Z matrix produced wrong pre-undo text: " + escapedArenaPayload(editor->snapshotText());
			return false;
		}
		sendWindowKeyEvent(window, kbCtrlZ);
		if (editor->snapshotText() != text || !window.hasBlock() || window.blockStatus() != MREditWindow::bmColumn || editor->hasTextSelection()) {
			failureReason = "Ctrl-Z after column copy must restore text and preserve column block.";
			return false;
		}
		sendEditorCommand(*editor, cmMrEditRedo);
		if (editor->snapshotText() != "012345\nabcdef\nX123YZ\n bcd" || !window.hasBlock() || window.blockStatus() != MREditWindow::bmColumn || editor->hasTextSelection()) {
			failureReason = "Redo after column copy undo must restore copied text and preserve column block.";
			return false;
		}
	}
	{
		const std::string text = "aa MOVE zz\nend";
		const std::vector<std::size_t> starts = lineStartsForText(text);
		MREditWindow window(TRect(0, 0, 80, 16), "ctrlz-stream-move", 2304);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr || !window.replaceTextBuffer(text.c_str(), "ctrlz-stream-move")) {
			failureReason = "Unable to seed stream move Ctrl-Z matrix case.";
			return false;
		}
		placeEditorCursor(*editor, text, starts, 0, 3);
		window.beginStreamBlock();
		placeEditorCursor(*editor, text, starts, 0, 7);
		window.endBlock();
		placeEditorCursor(*editor, text, starts, 1, 3);
		if (!window.moveBlock(&errorText)) {
			failureReason = "Unable to move stream block in Ctrl-Z matrix: " + errorText;
			return false;
		}
		if (editor->snapshotText() != "aa  zz\nendMOVE") {
			failureReason = "Stream move Ctrl-Z matrix produced wrong pre-undo text.";
			return false;
		}
		sendWindowKeyEvent(window, kbCtrlZ);
		if (editor->snapshotText() != text || !window.hasBlock() || window.blockStatus() != MREditWindow::bmStream || editor->hasTextSelection()) {
			failureReason = "Ctrl-Z after stream move must restore text and preserve stream block.";
			return false;
		}
		sendEditorCommand(*editor, cmMrEditRedo);
		if (editor->snapshotText() != "aa  zz\nendMOVE" || !window.hasBlock() || window.blockStatus() != MREditWindow::bmStream || editor->hasTextSelection()) {
			failureReason = "Redo after stream move undo must restore moved text and preserve stream block.";
			return false;
		}
	}
	{
		const std::string text = "one\ntwo\nlast";
		MREditWindow window(TRect(0, 0, 80, 16), "ctrlz-line-move", 2305);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr || !window.replaceTextBuffer(text.c_str(), "ctrlz-line-move")) {
			failureReason = "Unable to seed line move Ctrl-Z matrix case.";
			return false;
		}
		editor->setCursorOffset(0);
		window.beginLineBlock();
		window.endBlock();
		editor->setCursorOffset(editor->nextLineOffset(editor->nextLineOffset(0)));
		if (!window.moveBlock(&errorText)) {
			failureReason = "Unable to move line block in Ctrl-Z matrix: " + errorText;
			return false;
		}
		if (editor->snapshotText() != "two\none\nlast") {
			failureReason = "Line move Ctrl-Z matrix produced wrong pre-undo text: " + escapedArenaPayload(editor->snapshotText());
			return false;
		}
		sendWindowKeyEvent(window, kbCtrlZ);
		if (editor->snapshotText() != text || !window.hasBlock() || window.blockStatus() != MREditWindow::bmLine || editor->hasTextSelection()) {
			failureReason = "Ctrl-Z after line move must restore text and preserve line block.";
			return false;
		}
		sendEditorCommand(*editor, cmMrEditRedo);
		if (editor->snapshotText() != "two\none\nlast" || !window.hasBlock() || window.blockStatus() != MREditWindow::bmLine || editor->hasTextSelection()) {
			failureReason = "Redo after line move undo must restore moved text and preserve line block.";
			return false;
		}
	}
	{
		const std::string text = "012345\nabcdef\nXYZ";
		MREditWindow window(TRect(0, 0, 80, 16), "ctrlz-column-move", 2306);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr || !window.replaceTextBuffer(text.c_str(), "ctrlz-column-move")) {
			failureReason = "Unable to seed column move Ctrl-Z matrix case.";
			return false;
		}
		const std::size_t secondLine = editor->nextLineOffset(0);
		const std::size_t thirdLine = editor->nextLineOffset(secondLine);
		editor->setCursorOffsetAtVisualColumn(1, 1);
		window.beginColumnBlock();
		editor->setCursorOffsetAtVisualColumn(secondLine + 4, 4);
		window.endBlock();
		editor->setCursorOffsetAtVisualColumn(thirdLine + 1, 1);
		if (!window.moveBlock(&errorText)) {
			failureReason = "Unable to move column block in Ctrl-Z matrix: " + errorText;
			return false;
		}
		if (editor->snapshotText() != "045\naef\nX123YZ\n bcd") {
			failureReason = "Column move Ctrl-Z matrix produced wrong pre-undo text: " + escapedArenaPayload(editor->snapshotText());
			return false;
		}
		sendWindowKeyEvent(window, kbCtrlZ);
		if (editor->snapshotText() != text || !window.hasBlock() || window.blockStatus() != MREditWindow::bmColumn || editor->hasTextSelection()) {
			failureReason = "Ctrl-Z after column move must restore text and preserve column block.";
			return false;
		}
		sendEditorCommand(*editor, cmMrEditRedo);
		if (editor->snapshotText() != "045\naef\nX123YZ\n bcd" || !window.hasBlock() || window.blockStatus() != MREditWindow::bmColumn || editor->hasTextSelection()) {
			failureReason = "Redo after column move undo must restore moved text and preserve column block.";
			return false;
		}
	}
	{
		const std::string text = "aa DELETE zz\nend";
		const std::vector<std::size_t> starts = lineStartsForText(text);
		MREditWindow window(TRect(0, 0, 80, 16), "ctrlz-stream-delete", 2307);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr || !window.replaceTextBuffer(text.c_str(), "ctrlz-stream-delete")) {
			failureReason = "Unable to seed stream delete Ctrl-Z matrix case.";
			return false;
		}
		placeEditorCursor(*editor, text, starts, 0, 3);
		window.beginStreamBlock();
		placeEditorCursor(*editor, text, starts, 0, 9);
		window.endBlock();
		if (!window.deleteBlock(&errorText)) {
			failureReason = "Unable to delete stream block in Ctrl-Z matrix: " + errorText;
			return false;
		}
		if (editor->snapshotText() != "aa  zz\nend") {
			failureReason = "Stream delete Ctrl-Z matrix produced wrong pre-undo text.";
			return false;
		}
		sendWindowKeyEvent(window, kbCtrlZ);
		if (editor->snapshotText() != text || !window.hasBlock() || window.blockStatus() != MREditWindow::bmStream || editor->hasTextSelection()) {
			failureReason = "Ctrl-Z after stream delete must restore text and preserve stream block.";
			return false;
		}
		sendEditorCommand(*editor, cmMrEditRedo);
		if (editor->snapshotText() != "aa  zz\nend" || window.hasBlock() || editor->blockOverlayState().active || editor->hasTextSelection()) {
			failureReason = "Redo after stream delete undo must restore deleted text state and clear the post-delete block visual state.";
			return false;
		}
	}
	{
		const std::string text = "one\ntwo\nlast";
		MREditWindow window(TRect(0, 0, 80, 16), "ctrlz-line-delete", 2308);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr || !window.replaceTextBuffer(text.c_str(), "ctrlz-line-delete")) {
			failureReason = "Unable to seed line delete Ctrl-Z matrix case.";
			return false;
		}
		editor->setCursorOffset(0);
		window.beginLineBlock();
		window.endBlock();
		if (!window.deleteBlock(&errorText)) {
			failureReason = "Unable to delete line block in Ctrl-Z matrix: " + errorText;
			return false;
		}
		if (editor->snapshotText() != "two\nlast") {
			failureReason = "Line delete Ctrl-Z matrix produced wrong pre-undo text.";
			return false;
		}
		sendWindowKeyEvent(window, kbCtrlZ);
		if (editor->snapshotText() != text || !window.hasBlock() || window.blockStatus() != MREditWindow::bmLine || editor->hasTextSelection()) {
			failureReason = "Ctrl-Z after line delete must restore text and preserve line block.";
			return false;
		}
		sendEditorCommand(*editor, cmMrEditRedo);
		if (editor->snapshotText() != "two\nlast" || window.hasBlock() || editor->blockOverlayState().active || editor->hasTextSelection()) {
			failureReason = "Redo after line delete undo must restore deleted text state and clear the post-delete block visual state.";
			return false;
		}
	}
	{
		const std::string text = "012345\nabcdef\nXYZ";
		MREditWindow window(TRect(0, 0, 80, 16), "ctrlz-column-delete", 2309);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr || !window.replaceTextBuffer(text.c_str(), "ctrlz-column-delete")) {
			failureReason = "Unable to seed column delete Ctrl-Z matrix case.";
			return false;
		}
		const std::size_t secondLine = editor->nextLineOffset(0);
		editor->setCursorOffsetAtVisualColumn(1, 1);
		window.beginColumnBlock();
		editor->setCursorOffsetAtVisualColumn(secondLine + 4, 4);
		window.endBlock();
		if (!window.deleteBlock(&errorText)) {
			failureReason = "Unable to delete column block in Ctrl-Z matrix: " + errorText;
			return false;
		}
		if (editor->snapshotText() != "045\naef\nXYZ") {
			failureReason = "Column delete Ctrl-Z matrix produced wrong pre-undo text: " + escapedArenaPayload(editor->snapshotText());
			return false;
		}
		sendWindowKeyEvent(window, kbCtrlZ);
		if (editor->snapshotText() != text || !window.hasBlock() || window.blockStatus() != MREditWindow::bmColumn || editor->hasTextSelection()) {
			failureReason = "Ctrl-Z after column delete must restore text and preserve column block.";
			return false;
		}
		sendEditorCommand(*editor, cmMrEditRedo);
		if (editor->snapshotText() != "045\naef\nXYZ" || window.hasBlock() || editor->blockOverlayState().active || editor->hasTextSelection()) {
			failureReason = "Redo after column delete undo must restore deleted text state and clear the post-delete block visual state.";
			return false;
		}
	}
	return true;
}

bool runWindowMoveUndoClearsBlockStateCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = "aa MOVE zz\nend";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MREditWindow window(TRect(0, 0, 80, 16), "window", 2203);
	MRFileEditor *editor = window.getEditor();
	std::string errorText;

	if (editor == nullptr) {
		failureReason = "Unable to create editor in window move undo block-state case.";
		return false;
	}
	if (!editor->replaceBufferText(text.c_str())) {
		failureReason = "Unable to seed editor in window move undo block-state case.";
		return false;
	}
	placeEditorCursor(*editor, text, starts, 0, 3);
	window.beginStreamBlock();
	placeEditorCursor(*editor, text, starts, 0, 7);
	window.endBlock();
	placeEditorCursor(*editor, text, starts, 1, 3);
	if (!window.moveBlock(&errorText)) {
		failureReason = "Unable to move block in window move undo block-state case: " + errorText;
		return false;
	}
	if (editor->snapshotText() != "aa  zz\nendMOVE") {
		failureReason = "Moved text mismatch in window move undo block-state case: got=\"" + escapedArenaPayload(editor->snapshotText()) + "\".";
		return false;
	}
	sendEditorCommand(*editor, cmMrEditUndo);
	if (editor->snapshotText() != text) {
		failureReason = "Undo must restore text after window move block operation: got=\"" + escapedArenaPayload(editor->snapshotText()) + "\" expected=\"" + escapedArenaPayload(text) + "\".";
		return false;
	}
	window.refreshBlockVisual();
	if (!window.hasBlock() || !editor->blockOverlayState().active || editor->hasTextSelection()) {
		failureReason = "Undo after window move must preserve stored BlockOps state and overlay while keeping editor text selection collapsed.";
		return false;
	}
	return true;
}

bool runWindowMouseMarkedMoveUndoCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = "aa MOVE zz\nend";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	QueuedMouseOwner owner(TRect(0, 0, 80, 16));
	MREditWindow *window = new MREditWindow(TRect(0, 0, 80, 16), "window-mouse-move-undo", 2204);
	MRFileEditor *editor = nullptr;
	std::string errorText;

	owner.insert(window);
	editor = window->getEditor();
	if (editor == nullptr) {
		failureReason = "Unable to create editor in window mouse-marked move undo case.";
		return false;
	}
	if (!window->replaceTextBuffer(text.c_str(), "window-mouse-move-undo")) {
		failureReason = "Unable to seed editor in window mouse-marked move undo case.";
		return false;
	}

	owner.queueMouseEvent(makeMouseEvent(evMouseMove, editor->makeGlobal(TPoint(localXForEditorColumn(8), 0)).x, editor->makeGlobal(TPoint(localXForEditorColumn(8), 0)).y, 0, mbLeftButton));
	owner.queueMouseEvent(makeMouseEvent(evMouseUp, editor->makeGlobal(TPoint(localXForEditorColumn(8), 0)).x, editor->makeGlobal(TPoint(localXForEditorColumn(8), 0)).y, 0, 0));
	TEvent mouseDown = makeMouseEvent(evMouseDown, editor->makeGlobal(TPoint(localXForEditorColumn(4), 0)).x, editor->makeGlobal(TPoint(localXForEditorColumn(4), 0)).y, 0, mbLeftButton);
	window->handleEvent(mouseDown);
	if (!window->hasBlock()) {
		failureReason = "Mouse drag must commit a block before window mouse-marked move undo case.";
		return false;
	}
	placeEditorCursor(*editor, text, starts, 1, 3);
	if (!window->moveBlock(&errorText)) {
		failureReason = "Unable to move mouse-marked block in window mouse-marked move undo case: " + errorText;
		return false;
	}
	if (editor->snapshotText() != "aa  zz\nendMOVE") {
		failureReason = "Moved text mismatch in window mouse-marked move undo case: got=\"" + escapedArenaPayload(editor->snapshotText()) + "\".";
		return false;
	}
	sendEditorCommand(*editor, cmMrEditUndo);
	if (editor->snapshotText() != text) {
		failureReason = "Undo must restore text after window mouse-marked move block operation: got=\"" + escapedArenaPayload(editor->snapshotText()) + "\" expected=\"" + escapedArenaPayload(text) + "\".";
		return false;
	}
	window->refreshBlockVisual();
	if (!window->hasBlock() || !editor->blockOverlayState().active || editor->hasTextSelection()) {
		failureReason = "Undo after window mouse-marked move must preserve stored BlockOps state and overlay while keeping editor text selection collapsed.";
		return false;
	}
	return true;
}

bool runWindowMoveLegacyUndoCommandCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = "aa MOVE zz\nend";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MREditWindow window(TRect(0, 0, 80, 16), "window-legacy-undo", 2205);
	MRFileEditor *editor = window.getEditor();
	std::string errorText;

	if (editor == nullptr) {
		failureReason = "Unable to create editor in window legacy undo command case.";
		return false;
	}
	if (!window.replaceTextBuffer(text.c_str(), "window-legacy-undo")) {
		failureReason = "Unable to seed editor in window legacy undo command case.";
		return false;
	}
	placeEditorCursor(*editor, text, starts, 0, 3);
	window.beginStreamBlock();
	placeEditorCursor(*editor, text, starts, 0, 7);
	window.endBlock();
	placeEditorCursor(*editor, text, starts, 1, 3);
	if (!window.moveBlock(&errorText)) {
		failureReason = "Unable to move block in window legacy undo command case: " + errorText;
		return false;
	}
	if (editor->snapshotText() != "aa  zz\nendMOVE") {
		failureReason = "Moved text mismatch in window legacy undo command case: got=\"" + escapedArenaPayload(editor->snapshotText()) + "\".";
		return false;
	}
	sendEditorCommand(*editor, cmUndo);
	if (editor->snapshotText() != text) {
		failureReason = "Legacy cmUndo after block move must route to MR undo, got=\"" + escapedArenaPayload(editor->snapshotText()) + "\" expected=\"" + escapedArenaPayload(text) + "\".";
		return false;
	}
	return true;
}

bool runWindowMoveCtrlZTypingUndoCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = "aa MOVE zz\nend";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MREditWindow window(TRect(0, 0, 80, 16), "window-ctrl-z-undo", 2206);
	MRFileEditor *editor = window.getEditor();
	std::string errorText;

	if (editor == nullptr) {
		failureReason = "Unable to create editor in window Ctrl-Z undo typing case.";
		return false;
	}
	if (!window.replaceTextBuffer(text.c_str(), "window-ctrl-z-undo")) {
		failureReason = "Unable to seed editor in window Ctrl-Z undo typing case.";
		return false;
	}
	placeEditorCursor(*editor, text, starts, 0, 3);
	window.beginStreamBlock();
	placeEditorCursor(*editor, text, starts, 0, 7);
	window.endBlock();
	placeEditorCursor(*editor, text, starts, 1, 3);
	if (!window.moveBlock(&errorText)) {
		failureReason = "Unable to move block in window Ctrl-Z undo typing case: " + errorText;
		return false;
	}
	if (editor->snapshotText() != "aa  zz\nendMOVE") {
		failureReason = "Moved text mismatch before Ctrl-Z undo typing case: got=\"" + escapedArenaPayload(editor->snapshotText()) + "\".";
		return false;
	}
	sendWindowKeyEvent(window, kbCtrlZ);
	if (editor->snapshotText() != text) {
		failureReason = "Ctrl-Z after block move must restore original text, got=\"" + escapedArenaPayload(editor->snapshotText()) + "\" expected=\"" + escapedArenaPayload(text) + "\".";
		return false;
	}
	if (editor->bufferModel().length() != text.size()) {
		failureReason = "Ctrl-Z after block move must restore document length, got=" + std::to_string(editor->bufferModel().length()) + " expected=" + std::to_string(text.size()) + ".";
		return false;
	}
	if (editor->hasTextSelection()) {
		failureReason = "Ctrl-Z after block move must clear text selection before further typing, selectionStart=" + std::to_string(editor->bufferModel().selectionStart()) + " selectionEnd=" + std::to_string(editor->bufferModel().selectionEnd()) + ".";
		return false;
	}
	editor->setCursorOffset(0);
	if (editor->hasTextSelection()) {
		failureReason = "Cursor placement after Ctrl-Z block undo must not create text selection, selectionStart=" + std::to_string(editor->bufferModel().selectionStart()) + " selectionEnd=" + std::to_string(editor->bufferModel().selectionEnd()) + ".";
		return false;
	}
	sendWindowTextInput(window, 's');
	if (editor->snapshotText() != "saa MOVE zz\nend") {
		failureReason = "Typing after Ctrl-Z block undo must mutate only the cursor line, got=\"" + escapedArenaPayload(editor->snapshotText()) + "\" cursor=" + std::to_string(editor->cursorOffset()) +
		                " lineEnd0=" + std::to_string(editor->lineEndOffset(0)) + " len=" + std::to_string(editor->bufferModel().length()) + " insert=" + std::to_string(editor->insertModeEnabled() ? 1 : 0) + ".";
		failureReason += " pieces=" + std::to_string(editor->bufferModel().document().pieceCount()) + " add=" + std::to_string(editor->bufferModel().document().addBufferLength()) + ".";
		return false;
	}
	if (editor->bufferModel().lineCount() != 2) {
		failureReason = "Typing after Ctrl-Z block undo must preserve line geometry, got lineCount=" + std::to_string(editor->bufferModel().lineCount()) + ".";
		return false;
	}
	return true;
}

bool runWindowLoadClearsUndoStackCase(std::string &failureReason) {
	static const char *const path = "/tmp/mr-regression-load-clears-undo.txt";
	const std::string loadedText = "loaded\nfile\ncontent\n";
	MREditWindow window(TRect(0, 0, 80, 16), "window-load-clears-undo", 2207);
	MRFileEditor *editor = window.getEditor();

	if (editor == nullptr) {
		failureReason = "Unable to create editor in load clears undo stack case.";
		return false;
	}
	if (!window.replaceTextBuffer("old\ntext\n", "window-load-clears-undo")) {
		failureReason = "Unable to seed editor in load clears undo stack case.";
		return false;
	}
	if (editor->bufferModel().undoStackDepth() != 0) {
		failureReason = "Whole-buffer text load must not leave an undo snapshot.";
		return false;
	}
	editor->setCursorOffset(0);
	sendEditorTextInput(*editor, 'x');
	if (editor->bufferModel().undoStackDepth() == 0) {
		failureReason = "Regular text input must create undo history before load clears undo stack case.";
		return false;
	}
	{
		std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
		if (!out) {
			failureReason = "Unable to create temporary load file in load clears undo stack case.";
			return false;
		}
		out << loadedText;
	}
	if (!window.loadFromFile(path)) {
		std::remove(path);
		failureReason = "Unable to load temporary file in load clears undo stack case.";
		return false;
	}
	std::remove(path);
	if (editor->snapshotText() != loadedText) {
		failureReason = "Loaded file content mismatch in load clears undo stack case.";
		return false;
	}
	if (editor->bufferModel().undoStackDepth() != 0 || editor->bufferModel().redoStackDepth() != 0) {
		failureReason = "File load must clear undo/redo history.";
		return false;
	}
	sendWindowKeyEvent(window, kbCtrlZ);
	if (editor->snapshotText() != loadedText) {
		failureReason = "Ctrl-Z after file load must not restore pre-load content, got=\"" + escapedArenaPayload(editor->snapshotText()) + "\".";
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

bool runBlockCopyCase(MRFEBlockMode mode, const std::string &text, std::size_t anchorLine, int anchorColumn, std::size_t cursorLine, int cursorColumn, std::size_t targetLine, int targetColumn, const std::string &copiedText, const char *phase, std::string &failureReason, bool targetInsertMode = true) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}
	editor.setInsertModeEnabled(targetInsertMode);
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

bool runBlockMoveCase(MRFEBlockMode mode, const std::string &text, std::size_t anchorLine, int anchorColumn, std::size_t cursorLine, int cursorColumn, std::size_t targetLine, int targetColumn, const std::string &movedText, const char *phase, std::string &failureReason, bool targetInsertMode = true) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}
	editor.setInsertModeEnabled(targetInsertMode);
	placeEditorCursor(editor, text, starts, anchorLine, anchorColumn);
	if (!beginBlockForMode(ops, editor, mode)) {
		failureReason = std::string("Unable to begin move block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, cursorLine, cursorColumn);
	if (!ops.updateFromEditor(editor) || !ops.end(editor)) {
		failureReason = std::string("Unable to commit move block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, targetLine, targetColumn);
	if (mode == MRFEBlockMode::Column) {
		const std::size_t expectedLine2 = targetLine + std::max(cursorLine, anchorLine) - std::min(cursorLine, anchorLine);
		const int width = std::max(cursorColumn, anchorColumn) - std::min(cursorColumn, anchorColumn);
		const bool leaveColumnSpace = configuredEditSetupSettings().columnBlockMove == "LEAVE_SPACE";
		int expectedCol1 = std::max(targetColumn, 0);
		if (!leaveColumnSpace && targetLine <= std::max(cursorLine, anchorLine) && expectedLine2 >= std::min(cursorLine, anchorLine) && expectedCol1 >= std::max(cursorColumn, anchorColumn)) expectedCol1 -= width;

		return checkBlockMoveUndoRedo(editor, ops, mode, targetLine, expectedLine2, expectedCol1, expectedCol1 + width, text, movedText, phase, failureReason);
	}
	return checkBlockMoveUndoRedo(editor, ops, mode, 0, 0, 0, 0, text, movedText, phase, failureReason);
}

bool runInterWindowCopyCase(MRFEBlockMode mode, const std::string &sourceText, std::size_t anchorLine, int anchorColumn, std::size_t cursorLine, int cursorColumn, const std::string &targetText, std::size_t targetLine, int targetColumn, const std::string &copiedText, const char *phase, std::string &failureReason, bool targetInsertMode = true) {
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
	targetEditor->setInsertModeEnabled(targetInsertMode);
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

bool runInterWindowMoveCase(MRFEBlockMode mode, const std::string &sourceText, std::size_t anchorLine, int anchorColumn, std::size_t cursorLine, int cursorColumn, const std::string &targetText, std::size_t targetLine, int targetColumn, const std::string &sourceMovedText, const std::string &targetMovedText, const char *phase, std::string &failureReason, bool targetInsertMode = true) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::vector<std::size_t> sourceStarts = lineStartsForText(sourceText);
	const std::vector<std::size_t> targetStarts = lineStartsForText(targetText);
	MREditWindow sourceWindow(TRect(0, 0, 80, 16), "source", 2101);
	MREditWindow targetWindow(TRect(0, 0, 80, 16), "target", 2102);
	MRFileEditor *sourceEditor = sourceWindow.getEditor();
	MRFileEditor *targetEditor = targetWindow.getEditor();

	if (sourceEditor == nullptr || targetEditor == nullptr) {
		failureReason = std::string("Unable to create editors in ") + phase + ".";
		return false;
	}
	if (!sourceEditor->replaceBufferText(sourceText.c_str()) || (!targetText.empty() && !targetEditor->replaceBufferText(targetText.c_str()))) {
		failureReason = std::string("Unable to seed inter-window move editors in ") + phase + ".";
		return false;
	}
	targetEditor->setInsertModeEnabled(targetInsertMode);
	placeEditorCursor(*sourceEditor, sourceText, sourceStarts, anchorLine, anchorColumn);
	if (mode == MRFEBlockMode::Line) sourceWindow.beginLineBlock();
	else if (mode == MRFEBlockMode::Column)
		sourceWindow.beginColumnBlock();
	else
		sourceWindow.beginStreamBlock();
	placeEditorCursor(*sourceEditor, sourceText, sourceStarts, cursorLine, cursorColumn);
	sourceWindow.endBlock();
	placeEditorCursor(*targetEditor, targetText, targetStarts, targetLine, targetColumn);
	return checkInterWindowMoveUndoRedo(sourceWindow, targetWindow, sourceText, sourceMovedText, targetText, targetMovedText, phase, failureReason);
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

bool MRFEBlockOps::moveCurrentBlockToCursor(MRFileEditor &editor, std::string *errorText) {
	std::string text;
	std::string finalText;
	std::vector<std::size_t> starts;
	std::vector<ColumnLineReplacement> columnReplacements;
	MRFEArenaAllocator transferArena;
	std::vector<char> payloadStorage;
	MRFEBlockGeometry sourceGeometry;
	MRFEBlockGeometry targetGeometry;
	std::size_t targetCursor = 0;
	std::size_t cursor = 0;
	MRTextBufferModel::StagedTransaction transaction(editor.readSnapshot(), "move-block");

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
	sourceGeometry = mGeometry;
	targetCursor = editor.cursorOffset();
	if (!captureTransferPayload(editor, transferArena, errorText)) return false;
	payloadStorage = transferArena.release();
	const std::string_view payload = payloadView(payloadStorage);
	if (payload.empty()) {
		if (errorText != nullptr) *errorText = "Block payload is empty.";
		return false;
	}

	text = editor.snapshotText();
	finalText = text;
	if (sourceGeometry.mode == MRFEBlockMode::Stream) {
		const std::size_t eraseStart = sourceGeometry.rangeStart;
		const std::size_t eraseEnd = sourceGeometry.rangeEnd;
		const std::size_t eraseLength = eraseEnd - eraseStart;
		const int paddingColumns = editor.paddingColumnsBeforeInsertAtCursor();
		std::string insertion;
		std::size_t insertOffset = targetCursor;

		if (targetCursor > eraseStart && targetCursor < eraseEnd) {
			if (errorText != nullptr) *errorText = "Move target is inside the block.";
			return false;
		}
			if (targetCursor >= eraseEnd) insertOffset = targetCursor - eraseLength;
			else if (targetCursor >= eraseStart)
				insertOffset = eraseStart;
			if (paddingColumns > 0) insertion.append(static_cast<std::size_t>(paddingColumns), ' ');
			insertion.append(payload.data(), payload.size());
			finalText.erase(eraseStart, eraseLength);
			if (editor.insertModeEnabled())
				finalText.insert(insertOffset, insertion);
			else {
				starts = lineStartsForText(finalText);
				const std::size_t overwriteEnd = overwriteEndForStreamPayload(finalText, starts, insertOffset, insertion.size());
				finalText.replace(insertOffset, overwriteEnd - insertOffset, insertion);
			}
			targetGeometry = MRFEBlockGeometry();
		targetGeometry.mode = MRFEBlockMode::Stream;
		targetGeometry.status = MRFEBlockStatus::Committed;
		targetGeometry.rangeStart = insertOffset + static_cast<std::size_t>(std::max(0, paddingColumns));
		targetGeometry.rangeEnd = targetGeometry.rangeStart + payload.size();
		targetGeometry.anchor = targetGeometry.rangeStart;
		targetGeometry.cursor = targetGeometry.rangeEnd;
		starts = lineStartsForText(finalText);
		targetGeometry.line1 = lineIndexForOffset(starts, targetGeometry.rangeStart);
		targetGeometry.line2 = lineIndexForOffset(starts, targetGeometry.rangeEnd);
		targetGeometry.anchorColumn = std::max(0, static_cast<int>(editor.columnOfOffset(sourceGeometry.rangeStart)));
		targetGeometry.cursorColumn = targetGeometry.anchorColumn + static_cast<int>(payload.size());
		cursor = targetGeometry.rangeStart;
	} else if (sourceGeometry.mode == MRFEBlockMode::Line) {
		starts = lineStartsForText(text);
		const std::size_t targetLine = lineIndexForOffset(starts, targetCursor);
		const std::size_t insertStart = starts.empty() ? 0 : starts[std::min(targetLine, starts.size() - 1)];
		const std::size_t eraseStart = sourceGeometry.rangeStart;
		const std::size_t eraseEnd = sourceGeometry.rangeEnd;
		const std::size_t eraseLength = eraseEnd - eraseStart;
		std::size_t insertOffset = insertStart;

		if (insertStart > eraseStart && insertStart < eraseEnd) {
			if (errorText != nullptr) *errorText = "Move target is inside the block.";
			return false;
		}
		if (insertStart >= eraseEnd) insertOffset = insertStart - eraseLength;
		else if (insertStart >= eraseStart)
			insertOffset = eraseStart;
		finalText.erase(eraseStart, eraseLength);
		starts = lineStartsForText(finalText);
		if (editor.insertModeEnabled())
			finalText.insert(insertOffset, payload);
		else {
			const std::size_t overwrittenLine = lineIndexForOffset(starts, insertOffset);
			const std::size_t overwriteEnd = overwriteEndForLinePayload(finalText, starts, overwrittenLine, sourceGeometry.line2 >= sourceGeometry.line1 ? sourceGeometry.line2 - sourceGeometry.line1 + 1 : 0);
			finalText.replace(insertOffset, overwriteEnd - insertOffset, payload);
		}
		starts = lineStartsForText(finalText);
		targetGeometry = MRFEBlockGeometry();
		targetGeometry.mode = MRFEBlockMode::Line;
		targetGeometry.status = MRFEBlockStatus::Committed;
		targetGeometry.anchor = insertOffset;
		targetGeometry.cursor = insertOffset + payload.size();
		targetGeometry.rangeStart = targetGeometry.anchor;
		targetGeometry.rangeEnd = targetGeometry.cursor;
		targetGeometry.line1 = lineIndexForOffset(starts, targetGeometry.rangeStart);
		targetGeometry.line2 = targetGeometry.line1 + (sourceGeometry.line2 >= sourceGeometry.line1 ? sourceGeometry.line2 - sourceGeometry.line1 : 0);
		cursor = targetGeometry.rangeStart;
	} else if (sourceGeometry.mode == MRFEBlockMode::Column) {
		starts = lineStartsForText(text);
		const std::size_t targetLine = lineIndexForOffset(starts, targetCursor);
		const std::size_t rowCount = sourceGeometry.line2 >= sourceGeometry.line1 ? sourceGeometry.line2 - sourceGeometry.line1 + 1 : 0;
		const std::size_t width = sourceGeometry.col2 > sourceGeometry.col1 ? static_cast<std::size_t>(sourceGeometry.col2 - sourceGeometry.col1) : 0;
		const bool rowsOverlap = targetLine <= sourceGeometry.line2 && targetLine + (rowCount == 0 ? 0 : rowCount - 1) >= sourceGeometry.line1;
		const bool leaveColumnSpace = configuredEditSetupSettings().columnBlockMove == "LEAVE_SPACE";
		int destCol = std::max(0, editor.displayedCursorColumn());
		const std::string lineSeparator = lineSeparatorForText(finalText);

		if (rowCount == 0 || width == 0 || payloadStorage.size() < rowCount * width) {
			if (errorText != nullptr) *errorText = "Column block payload geometry is invalid.";
			return false;
		}
		if (rowsOverlap && destCol > sourceGeometry.col1 && destCol < sourceGeometry.col2) {
			if (errorText != nullptr) *errorText = "Move target overlaps the column block.";
			return false;
		}
		if (!leaveColumnSpace && rowsOverlap && destCol >= sourceGeometry.col2) destCol -= static_cast<int>(width);
		if (leaveColumnSpace) collectColumnClearReplacements(finalText, starts, sourceGeometry, columnReplacements);
		else
			collectColumnEraseReplacements(finalText, starts, sourceGeometry, columnReplacements);
		for (std::size_t index = columnReplacements.size(); index > 0; --index) {
			const ColumnLineReplacement &replacement = columnReplacements[index - 1];
			finalText.replace(replacement.range.start, replacement.range.length(), replacement.text);
		}
		for (std::size_t row = 0; row < rowCount; ++row) {
			const std::size_t line = targetLine + row;
			starts = lineStartsForText(finalText);
			while (line >= starts.size()) {
				finalText.insert(finalText.size(), lineSeparator);
				starts = lineStartsForText(finalText);
			}

			const std::size_t lineStart = starts[line];
			const std::size_t lineEnd = lineContentEndForIndex(finalText, starts, line);
			const std::string_view rowPayload = payloadRowView(payloadStorage, row, width);
			const std::string replacement = editor.insertModeEnabled() ? insertVisualColumnsIntoLine(std::string_view(finalText.data() + lineStart, lineEnd - lineStart), destCol, rowPayload) : replaceVisualColumnsInLine(std::string_view(finalText.data() + lineStart, lineEnd - lineStart), destCol, width, rowPayload);
			finalText.replace(lineStart, lineEnd - lineStart, replacement);
		}
		starts = lineStartsForText(finalText);
		targetGeometry = MRFEBlockGeometry();
		targetGeometry.mode = MRFEBlockMode::Column;
		targetGeometry.status = MRFEBlockStatus::Committed;
		targetGeometry.line1 = targetLine;
		targetGeometry.line2 = targetLine + rowCount - 1;
		targetGeometry.col1 = destCol;
		targetGeometry.col2 = destCol + static_cast<int>(width);
		targetGeometry.rangeStart = offsetAtLineVisualColumn(finalText, starts, targetGeometry.line1, targetGeometry.col1);
		targetGeometry.rangeEnd = offsetAtLineVisualColumn(finalText, starts, targetGeometry.line2, targetGeometry.col2);
		targetGeometry.anchor = targetGeometry.rangeStart;
		targetGeometry.cursor = targetGeometry.rangeEnd;
		targetGeometry.anchorColumn = targetGeometry.col1;
		targetGeometry.cursorColumn = targetGeometry.col2;
		cursor = targetGeometry.rangeStart;
	} else {
		if (errorText != nullptr) *errorText = "No block mode selected.";
		return false;
	}

	if (finalText != text) {
		transaction.replace(MRTextBufferModel::Range(0, text.size()), finalText);
		if (!editor.applyStagedTransaction(transaction, cursor, cursor, cursor, true).applied()) {
			if (errorText != nullptr) *errorText = "Unable to move block.";
			return false;
		}
	} else
		editor.setCursorOffset(cursor);
	mGeometry = targetGeometry;
	applySelection(editor);
	applyOverlay(editor);
	return true;
}

bool MRFEBlockOps::moveCurrentBlockToEditor(MRFileEditor &sourceEditor, MRFEBlockOps &targetOps, MRFileEditor &targetEditor, int sourceWindowId, int targetWindowId, std::string *errorText) {
	MRFEArenaAllocator transferArena;
	TransferMessage message;

	if (&sourceEditor == &targetEditor) return moveCurrentBlockToCursor(sourceEditor, errorText);
	if (sourceEditor.isReadOnly()) {
		if (errorText != nullptr) *errorText = "Source editor is read-only.";
		return false;
	}
	if (!prepareTransferMessage(sourceEditor, sourceWindowId, targetWindowId, TransferMode::Move, transferArena, message, errorText)) return false;
	if (!targetOps.insertTransferMessage(targetEditor, message, errorText)) return false;
	return removeCurrentBlockForMove(sourceEditor, errorText);
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
		if (configuredEditSetupSettings().columnBlockMove == "LEAVE_SPACE") collectColumnClearReplacements(text, starts, mGeometry, columnReplacements);
		else
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
	if (!columnReplacements.empty()) {
		stageColumnLineReplacements(transaction, columnReplacements);
	} else {
		for (std::size_t index = ranges.size(); index > 0; --index)
			transaction.erase(ranges[index - 1]);
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

bool MRFEBlockOps::removeCurrentBlockForMove(MRFileEditor &editor, std::string *errorText) {
	std::string text;
	std::vector<std::size_t> starts;
	std::vector<MRTextBufferModel::Range> ranges;
	std::vector<ColumnLineReplacement> columnReplacements;
	std::size_t cursor = 0;
	MRTextBufferModel::StagedTransaction transaction(editor.readSnapshot(), "move-block");

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
		if (configuredEditSetupSettings().columnBlockMove == "LEAVE_SPACE") collectColumnClearReplacements(text, starts, mGeometry, columnReplacements);
		else
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
	if (!columnReplacements.empty()) {
		stageColumnLineReplacements(transaction, columnReplacements);
	} else {
		for (std::size_t index = ranges.size(); index > 0; --index)
			transaction.erase(ranges[index - 1]);
	}
	if (!editor.applyStagedTransaction(transaction, cursor, cursor, cursor, true).applied()) {
		if (errorText != nullptr) *errorText = "Unable to move block.";
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
	return setCommittedBlock(editor, MRFEBlockMode::Stream, start, end);
}

bool MRFEBlockOps::setCommittedBlock(MRFileEditor &editor, MRFEBlockMode mode, std::size_t anchor, std::size_t cursor, int anchorColumn, int cursorColumn) {
	if (mode == MRFEBlockMode::None) return false;
	mGeometry = MRFEBlockGeometry();
	mGeometry.mode = mode;
	mGeometry.status = MRFEBlockStatus::Committed;
	mGeometry.hidden = false;
	mGeometry.anchor = anchor;
	mGeometry.cursor = cursor;
	mGeometry.anchorColumn = anchorColumn >= 0 ? anchorColumn : std::max(0, static_cast<int>(editor.columnOfOffset(anchor)));
	mGeometry.cursorColumn = cursorColumn >= 0 ? cursorColumn : std::max(0, static_cast<int>(editor.columnOfOffset(cursor)));
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
	const bool isMove = message.mode == TransferMode::Move;
	const char *operationError = isMove ? "Unable to move block." : "Unable to copy block.";
	MRTextBufferModel::StagedTransaction transaction(editor.readSnapshot(), isMove ? "move-block" : "copy-block");
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
		if (editor.insertModeEnabled())
			transaction.insert(rangeStart, insertArena.view());
		else {
			working = editor.snapshotText();
			starts = lineStartsForText(working);
			rangeEnd = overwriteEndForStreamPayload(working, starts, rangeStart, insertArena.size());
			transaction.replace(MRTextBufferModel::Range(rangeStart, rangeEnd), insertArena.view());
		}
		cursor = rangeStart + insertArena.size();
		if (!editor.applyStagedTransaction(transaction, cursor, cursor, cursor, true).applied()) {
			if (errorText != nullptr) *errorText = operationError;
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
		if (editor.insertModeEnabled())
			transaction.insert(rangeStart, payload);
		else {
			rangeEnd = overwriteEndForLinePayload(working, starts, targetLine, message.rowCount);
			transaction.replace(MRTextBufferModel::Range(rangeStart, rangeEnd), payload);
		}
		rangeEnd = rangeStart + payload.size();
		cursor = rangeEnd;
		if (!editor.applyStagedTransaction(transaction, cursor, cursor, cursor, true).applied()) {
			if (errorText != nullptr) *errorText = operationError;
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
			const std::string replacement = editor.insertModeEnabled() ? insertVisualColumnsIntoLine(std::string_view(working.data() + lineStart, lineEnd - lineStart), destCol, rowPayload) : replaceVisualColumnsInLine(std::string_view(working.data() + lineStart, lineEnd - lineStart), destCol, width, rowPayload);
			transaction.replace(MRTextBufferModel::Range(lineStart, lineEnd), replacement);
			working.replace(lineStart, lineEnd - lineStart, replacement);
		}
		starts = lineStartsForText(working);
		rangeStart = offsetAtLineVisualColumn(working, starts, targetLine, destCol);
		const std::size_t lastLine = targetLine + rowCount - 1;
		rangeEnd = offsetAtLineVisualColumn(working, starts, lastLine, destCol + static_cast<int>(width));
		cursor = rangeStart;
		if (!editor.applyStagedTransaction(transaction, cursor, cursor, cursor, true).applied()) {
			if (errorText != nullptr) *errorText = operationError;
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
	if (!runWindowCopyUndoPreservesBlockStateCase(failureReason)) return false;
	if (!runWindowDeleteUndoPreservesBlockStateCase(failureReason)) return false;
	if (!runWindowCtrlZBlockOpsMatrixCase(failureReason)) return false;
	if (!runWindowMoveUndoClearsBlockStateCase(failureReason)) return false;
	if (!runWindowMouseMarkedMoveUndoCase(failureReason)) return false;
	if (!runWindowMoveLegacyUndoCommandCase(failureReason)) return false;
	if (!runWindowMoveCtrlZTypingUndoCase(failureReason)) return false;
	if (!runWindowLoadClearsUndoStackCase(failureReason)) return false;
	if (!runBlockCopyCase(MRFEBlockMode::Stream, "aa DELETE\n\nzz", 0, 3, 2, 0, 2, 2, "aa DELETE\n\nzzDELETE\n\n", "stream copy over empty line", failureReason)) return false;
	if (!runBlockCopyCase(MRFEBlockMode::Line, "one\n\ntwo\nlast", 0, 0, 1, 0, 3, 0, "one\n\ntwo\none\n\nlast", "line copy including empty line", failureReason)) return false;
	if (!runBlockCopyCase(MRFEBlockMode::Column, "012345\n\nabcdef\nXYZ", 0, 1, 2, 4, 3, 1, "012345\n\nabcdef\nX123YZ\n    \n bcd", "column copy over empty line with target extension", failureReason)) return false;
	if (!runBlockCopyCase(MRFEBlockMode::Column, "012345\n\nabcdef", 0, 1, 2, 4, 1, 0, "012345\n123\n   abcdef\nbcd", "column copy into LF target extension", failureReason)) return false;
	if (!runBlockCopyCase(MRFEBlockMode::Column, "012345\r\n\r\nabcdef", 0, 1, 2, 4, 1, 0, "012345\r\n123\r\n   abcdef\r\nbcd", "column copy into CRLF target extension", failureReason)) return false;
	if (!runBlockCopyCase(MRFEBlockMode::Stream, "aa COPY zz\nend!!!!", 0, 3, 0, 7, 1, 0, "aa COPY zz\nCOPY!!!", "stream copy overwrite", failureReason, false)) return false;
	if (!runBlockCopyCase(MRFEBlockMode::Line, "one\ntwo\nlast\nend", 0, 0, 0, 0, 2, 0, "one\ntwo\none\nend", "line copy overwrite", failureReason, false)) return false;
	if (!runBlockCopyCase(MRFEBlockMode::Column, "012345\nabcdef\nXaaaaZ\nYbbbbZ", 0, 1, 1, 4, 2, 1, "012345\nabcdef\nX123aZ\nYbcdbZ", "column copy overwrite", failureReason, false)) return false;
	if (!runInterWindowCopyCase(MRFEBlockMode::Stream, "aa COPY zz", 0, 3, 0, 7, "target:", 0, 7, "target:COPY", "inter-window stream copy", failureReason)) return false;
	if (!runInterWindowCopyCase(MRFEBlockMode::Line, "one\n\ntwo\nlast", 0, 0, 1, 0, "target\nend", 1, 0, "target\none\n\nend", "inter-window line copy", failureReason)) return false;
	if (!runInterWindowCopyCase(MRFEBlockMode::Column, "012345\n\nabcdef", 0, 1, 2, 4, "T0\nT1\nT2", 0, 1, "T1230\nT   1\nTbcd2", "inter-window column copy", failureReason)) return false;
	if (!runInterWindowCopyCase(MRFEBlockMode::Stream, "aa COPY zz", 0, 3, 0, 7, "end!!!!", 0, 0, "COPY!!!", "inter-window stream copy overwrite", failureReason, false)) return false;
	if (!runInterWindowCopyCase(MRFEBlockMode::Line, "one\ntwo", 0, 0, 0, 0, "target\nend\nfinal", 1, 0, "target\none\nfinal", "inter-window line copy overwrite", failureReason, false)) return false;
	if (!runInterWindowCopyCase(MRFEBlockMode::Column, "012345\nabcdef", 0, 1, 1, 4, "T000Z\nU111Z", 0, 1, "T123Z\nUbcdZ", "inter-window column copy overwrite", failureReason, false)) return false;
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
	{
		MREditSetupSettings settings = configuredEditSetupSettings();
		settings.columnBlockMove = "DELETE_SPACE";
		ScopedEditSetupSettings scopedSettings(settings);

		if (!runBlockMoveCase(MRFEBlockMode::Stream, "aa MOVE zz\nend", 0, 3, 0, 7, 1, 3, "aa  zz\nendMOVE", "stream move", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Line, "one\ntwo\nthree\nfour", 0, 0, 0, 0, 3, 0, "two\nthree\none\nfour", "line move", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Column, "012345\nabcdef\nXYZ", 0, 1, 1, 4, 2, 1, "045\naef\nX123YZ\n bcd", "column move delete-space", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Column, "012345\r\nabcdef\r\nXYZ", 0, 1, 1, 4, 2, 1, "045\r\naef\r\nX123YZ\r\n bcd", "CRLF column move delete-space", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Stream, "aa MOVE zz\nend!!!!", 0, 3, 0, 7, 1, 0, "aa  zz\nMOVE!!!", "stream move overwrite", failureReason, false)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Line, "one\ntwo\nthree\nfour\nfive", 0, 0, 0, 0, 3, 0, "two\nthree\none\nfive", "line move overwrite", failureReason, false)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Column, "012345\nabcdef\nXaaaaZ\nYbbbbZ", 0, 1, 1, 4, 2, 1, "045\naef\nX123aZ\nYbcdbZ", "column move overwrite", failureReason, false)) return false;
		if (!runInterWindowMoveCase(MRFEBlockMode::Stream, "aa MOVE zz", 0, 3, 0, 7, "target:", 0, 7, "aa  zz", "target:MOVE", "inter-window stream move", failureReason)) return false;
		if (!runInterWindowMoveCase(MRFEBlockMode::Line, "one\ntwo\nlast", 0, 0, 0, 0, "target\nend", 1, 0, "two\nlast", "target\none\nend", "inter-window line move", failureReason)) return false;
		if (!runInterWindowMoveCase(MRFEBlockMode::Column, "012345\nabcdef", 0, 1, 1, 4, "T0\nT1", 0, 1, "045\naef", "T1230\nTbcd1", "inter-window column move", failureReason)) return false;
		if (!runInterWindowMoveCase(MRFEBlockMode::Stream, "aa MOVE zz", 0, 3, 0, 7, "end!!!!", 0, 0, "aa  zz", "MOVE!!!", "inter-window stream move overwrite", failureReason, false)) return false;
		if (!runInterWindowMoveCase(MRFEBlockMode::Line, "one\ntwo", 0, 0, 0, 0, "target\nend\nfinal", 1, 0, "two", "target\none\nfinal", "inter-window line move overwrite", failureReason, false)) return false;
		if (!runInterWindowMoveCase(MRFEBlockMode::Column, "012345\nabcdef", 0, 1, 1, 4, "T000Z\nU111Z", 0, 1, "045\naef", "T123Z\nUbcdZ", "inter-window column move overwrite", failureReason, false)) return false;
	}
	{
		MREditSetupSettings settings = configuredEditSetupSettings();
		settings.columnBlockMove = "LEAVE_SPACE";
		ScopedEditSetupSettings scopedSettings(settings);

		if (!runBlockMoveCase(MRFEBlockMode::Column, "012345\nabcdef\nXYZ", 0, 1, 1, 4, 2, 1, "0   45\na   ef\nX123YZ\n bcd", "column move leave-space", failureReason)) return false;
		if (!runInterWindowMoveCase(MRFEBlockMode::Column, "012345\nabcdef", 0, 1, 1, 4, "T0\nT1", 0, 1, "0   45\na   ef", "T1230\nTbcd1", "inter-window column move leave-space", failureReason)) return false;
		if (!runBlockDeleteCase(MRFEBlockMode::Column, "012345\n\nabcdef\nXYZ", 0, 1, 2, 4, "0   45\n    \na   ef\nXYZ", "LF column delete leave-space over empty line", failureReason)) return false;
		if (!runBlockDeleteCase(MRFEBlockMode::Column, "012345\r\n\r\nabcdef\r\nXYZ", 0, 1, 2, 4, "0   45\r\n    \r\na   ef\r\nXYZ", "CRLF column delete leave-space over empty line", failureReason)) return false;
	}
	{
		MREditSetupSettings settings = configuredEditSetupSettings();
		settings.columnBlockMove = "DELETE_SPACE";
		ScopedEditSetupSettings scopedSettings(settings);

		if (!runBlockDeleteCase(MRFEBlockMode::Stream, "aa DELETE\n\nzz", 0, 3, 2, 0, "aa zz", "stream delete over empty line", failureReason)) return false;
		if (!runBlockDeleteCase(MRFEBlockMode::Line, "one\n\ntwo\nlast", 0, 0, 1, 0, "two\nlast", "line delete including empty line", failureReason)) return false;
		if (!runBlockDeleteCase(MRFEBlockMode::Column, "012345\n\nabcdef\nXYZ", 0, 1, 2, 4, "045\n\naef\nXYZ", "LF column delete over empty line", failureReason)) return false;
		if (!runBlockDeleteCase(MRFEBlockMode::Column, "\tint unused;\n\treturn(0);", 0, 4, 1, 20, "    \n    ", "column delete across tab indentation", failureReason)) return false;
		if (!runBlockDeleteCase(MRFEBlockMode::Column, "012345\r\rabcdef\rXYZ", 0, 1, 2, 4, "045\r\raef\rXYZ", "CR-only column delete over empty line", failureReason)) return false;
		if (!runBlockDeleteCase(MRFEBlockMode::Column, "012345\r\n\r\nabcdef\r\nXYZ", 0, 1, 2, 4, "045\r\n\r\naef\r\nXYZ", "CRLF column delete over empty line", failureReason)) return false;
	}
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
