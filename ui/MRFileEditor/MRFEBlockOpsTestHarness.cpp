#define Uses_TKeys
#define Uses_TGroup
#define Uses_TEvent
#include <tvision/tv.h>

#include "MRFEBlockOpsTestHarness.hpp"
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

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

bool mrfeSeedMouseColumnStateForRegression(MRFileEditor &editor, int anchorColumn, int cursorColumn);
bool mrfeRenderedBlockOverlayLineRangeForRegression(const MRFileEditor &editor, std::size_t &line1, std::size_t &line2);

class MRFEBlockOpsTestPeer {
  public:
	static bool beginLine(MRFEBlockOps &ops, MRFileEditor &editor) {
		return ops.beginLine(editor);
	}

	static bool beginColumn(MRFEBlockOps &ops, MRFileEditor &editor) {
		return ops.beginColumn(editor);
	}

	static bool beginStream(MRFEBlockOps &ops, MRFileEditor &editor) {
		return ops.beginStream(editor);
	}

	static bool end(MRFEBlockOps &ops, MRFileEditor &editor) {
		return ops.end(editor);
	}

	static bool clear(MRFEBlockOps &ops, MRFileEditor &editor) {
		return ops.clear(editor);
	}

	static bool toggleVisibility(MRFEBlockOps &ops, MRFileEditor &editor) {
		return ops.toggleVisibility(editor);
	}

	static bool updateFromEditor(MRFEBlockOps &ops, MRFileEditor &editor) {
		return ops.updateFromEditor(editor);
	}

	static bool adoptMouseSelection(MRFEBlockOps &ops, MRFileEditor &editor, unsigned short modifiers) {
		return ops.adoptMouseSelection(editor, modifiers);
	}

	static bool refreshVisual(MRFEBlockOps &ops, MRFileEditor &editor) {
		return ops.refreshVisual(editor);
	}

	static bool captureCurrentBlockPayload(MRFEBlockOps &ops, MRFileEditor &editor, MRFEArenaAllocator &arena, std::string *errorText = nullptr) {
		return ops.captureCurrentBlockPayload(editor, arena, errorText);
	}

	static bool insertPayloadAsStreamBlock(MRFEBlockOps &ops, MRFileEditor &editor, const MRFEArenaAllocator &arena, std::string *errorText = nullptr) {
		return ops.insertPayloadAsStreamBlock(editor, arena, errorText);
	}

	static bool copyBlock(MRFEBlockOps &ops, MRFileEditor &editor, std::string *errorText = nullptr) {
		return ops.runBlockOperation(editor, MRFEBlockOps::BlockOperation::Copy, errorText);
	}

	static bool moveBlock(MRFEBlockOps &ops, MRFileEditor &editor, std::string *errorText = nullptr) {
		return ops.runBlockOperation(editor, MRFEBlockOps::BlockOperation::Move, errorText);
	}

	static bool deleteBlock(MRFEBlockOps &ops, MRFileEditor &editor, std::string *errorText = nullptr) {
		return ops.runBlockOperation(editor, MRFEBlockOps::BlockOperation::Delete, errorText);
	}

	static bool indentBlock(MRFEBlockOps &ops, MRFileEditor &editor, std::string *errorText = nullptr) {
		return ops.runBlockOperation(editor, MRFEBlockOps::BlockOperation::Indent, errorText);
	}

	static bool undentBlock(MRFEBlockOps &ops, MRFileEditor &editor, std::string *errorText = nullptr) {
		return ops.runBlockOperation(editor, MRFEBlockOps::BlockOperation::Undent, errorText);
	}

	static bool copyBlockTo(MRFEBlockOps &ops, MRFileEditor &sourceEditor, MRFEBlockOps &targetOps, MRFileEditor &targetEditor, std::string *errorText = nullptr) {
		return ops.runWindowBlockOperation(sourceEditor, targetOps, targetEditor, 0, 0, MRFEBlockOps::BlockOperation::Copy, errorText);
	}

	static bool moveBlockTo(MRFEBlockOps &ops, MRFileEditor &sourceEditor, MRFEBlockOps &targetOps, MRFileEditor &targetEditor, std::string *errorText = nullptr) {
		return ops.runWindowBlockOperation(sourceEditor, targetOps, targetEditor, 0, 0, MRFEBlockOps::BlockOperation::Move, errorText);
	}

	static bool loadBlockFromFile(MRFEBlockOps &ops, MRFileEditor &editor, const std::string &path, std::string *errorText = nullptr) {
		return ops.loadBlockFromFile(editor, path, errorText);
	}

	static bool saveBlockToFile(MRFEBlockOps &ops, MRFileEditor &editor, const std::string &path, std::string *errorText = nullptr) {
		return ops.saveBlockToFile(editor, path, errorText);
	}

	static bool setCommittedBlock(MRFEBlockOps &ops, MRFileEditor &editor, MRFEBlockMode mode, std::size_t anchor, std::size_t cursor, int anchorColumn = -1, int cursorColumn = -1) {
		return ops.setCommittedBlock(editor, mode, anchor, cursor, anchorColumn, cursorColumn);
	}

	static bool hasStoredBlock(const MRFEBlockOps &ops) {
		return ops.mGeometry.status != MRFEBlockStatus::Inactive;
	}

	static bool hasVisibleBlock(const MRFEBlockOps &ops) {
		return ops.hasVisibleBlock();
	}

	static MRFEBlockMode mode(const MRFEBlockOps &ops) {
		return ops.mGeometry.mode;
	}

	static const MRFEBlockGeometry &geometry(const MRFEBlockOps &ops) {
		return ops.mGeometry;
	}
};

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

std::size_t lineBlockEndpointOffset(const std::string &text, const std::vector<std::size_t> &starts, std::size_t offset) {
	if (offset == 0) return 0;
	for (std::size_t i = 1; i < starts.size(); ++i)
		if (starts[i] == offset && starts[i] == text.size()) return starts[i] - 1;
	return offset;
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
		if (input.anchor == input.cursor && input.anchor == input.text.size() && input.text.size() > 0) {
			geometry.line1 = lineIndexForOffset(input.lineStarts, input.anchor);
			geometry.line2 = geometry.line1;
			geometry.rangeStart = input.anchor;
			geometry.rangeEnd = input.anchor;
			geometry.col1 = 0;
			geometry.col2 = 0;
			return;
		}
		const std::size_t anchor = lineBlockEndpointOffset(input.text, input.lineStarts, std::min(input.anchor, input.text.size()));
		const std::size_t cursor = lineBlockEndpointOffset(input.text, input.lineStarts, std::min(input.cursor, input.text.size()));
		std::size_t anchorLine = lineIndexForOffset(input.lineStarts, anchor);
		std::size_t cursorLine = lineIndexForOffset(input.lineStarts, cursor);
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
	switch (mode) {
	case MRFEBlockMode::Line:
		return std::make_unique<LineBlockMarker>();
	case MRFEBlockMode::Column:
		return std::make_unique<ColumnBlockMarker>();
	case MRFEBlockMode::Stream:
		return std::make_unique<StreamBlockMarker>();
	case MRFEBlockMode::None:
		return nullptr;
	}
	return nullptr;
}

bool blockGeometryIsEmpty(const MRFEBlockGeometry &geometry) {
	switch (geometry.mode) {
	case MRFEBlockMode::Stream:
		return geometry.rangeStart >= geometry.rangeEnd;
	case MRFEBlockMode::Column:
		return geometry.col1 >= geometry.col2;
	case MRFEBlockMode::Line:
		return geometry.rangeStart >= geometry.rangeEnd;
	case MRFEBlockMode::None:
		return true;
	}
	return true;
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

int visualWidthOfLine(std::string_view lineText) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	std::string line(lineText);
	std::size_t offset = 0;
	int visual = 0;

	while (offset < line.size()) {
		std::size_t next = offset;
		std::size_t width = 0;

		if (!nextBlockDisplayChar(line, 0, line.size(), next, width, visual, settings)) break;
		visual += static_cast<int>(width);
		offset = next;
	}
	return visual;
}

std::string visualColumnsFromLine(std::string_view lineText, int col1, int col2) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	std::string result;
	std::string line(lineText);
	const int fromColumn = std::max(col1, 0);
	const int toColumn = std::max(col2, 0);
	std::size_t offset = 0;
	int visual = 0;
	int emitted = fromColumn;

	if (toColumn <= fromColumn) return result;
	result.reserve(static_cast<std::size_t>(toColumn - fromColumn));
	while (offset < line.size() && emitted < toColumn) {
		std::size_t next = offset;
		std::size_t width = 0;

		if (!nextBlockDisplayChar(line, 0, line.size(), next, width, visual, settings)) break;
		const int charStart = visual;
		const int charEnd = visual + static_cast<int>(width);
		if (charEnd > fromColumn && charStart < toColumn) {
			if (emitted < std::max(charStart, fromColumn)) {
				result.append(static_cast<std::size_t>(std::max(charStart, fromColumn) - emitted), ' ');
				emitted = std::max(charStart, fromColumn);
			}
			const int overlapStart = std::max(charStart, fromColumn);
			const int overlapEnd = std::min(charEnd, toColumn);
			const std::size_t byteWidth = next - offset;
			if (width == 1 && byteWidth == 1 && overlapStart == charStart && overlapEnd == charEnd && line[offset] != '\t')
				result.append(line.data() + offset, 1);
			else
				result.append(static_cast<std::size_t>(overlapEnd - overlapStart), ' ');
			emitted = overlapEnd;
		}
		visual = charEnd;
		offset = next;
	}
	if (emitted < toColumn) result.append(static_cast<std::size_t>(toColumn - emitted), ' ');
	return result;
}

std::string prefixBeforeVisualColumn(std::string_view lineText, int visualColumn) {
	const int column = std::max(visualColumn, 0);
	const int width = visualWidthOfLine(lineText);
	std::string prefix = eraseVisualColumnsFromLine(lineText, column, std::max(column, width));

	if (width < column) prefix.append(static_cast<std::size_t>(column - width), ' ');
	return prefix;
}

std::string suffixFromVisualColumn(std::string_view lineText, int visualColumn) {
	const int column = std::max(visualColumn, 0);
	const int width = visualWidthOfLine(lineText);

	if (column >= width) return std::string();
	return eraseVisualColumnsFromLine(lineText, 0, column);
}

std::string moveVisualColumnsWithinLine(std::string_view lineText, int sourceCol1, int sourceCol2, int destCol, std::string_view payload, bool insertMode, bool keepSpaces) {
	const int col1 = std::max(sourceCol1, 0);
	const int col2 = std::max(sourceCol2, col1);
	const int targetCol = std::max(destCol, 0);
	const int width = col2 - col1;
	const int lineWidth = visualWidthOfLine(lineText);
	std::string result;

	if (width <= 0 || targetCol == col1) return std::string(lineText);
	if (lineWidth <= col1 && std::all_of(payload.begin(), payload.end(), [](char ch) { return ch == ' '; })) return std::string(payload);
	if (targetCol < col1) {
		const int gapWidth = col1 - targetCol;

		result = prefixBeforeVisualColumn(lineText, targetCol);
		result.append(payload.data(), payload.size());
		if (keepSpaces) result.append(static_cast<std::size_t>(gapWidth), ' ');
		if (insertMode) {
			const std::string displaced = visualColumnsFromLine(lineText, targetCol, col1);
			result.append(displaced);
		}
		result.append(suffixFromVisualColumn(lineText, col2));
		return result;
	}

	if (targetCol < col2) {
		const int gapWidth = targetCol - col1;

		result = prefixBeforeVisualColumn(lineText, col1);
		if (insertMode) {
			if (keepSpaces)
				result.append(static_cast<std::size_t>(gapWidth), ' ');
			else
				result.append(visualColumnsFromLine(lineText, col2, col2 + gapWidth));
			result.append(payload.data(), payload.size());
			result.append(suffixFromVisualColumn(lineText, keepSpaces ? col2 : col2 + gapWidth));
		} else {
			result.append(static_cast<std::size_t>(gapWidth), ' ');
			result.append(payload.data(), payload.size());
			result.append(suffixFromVisualColumn(lineText, targetCol + width));
		}
		return result;
	}

	if (insertMode) {
		result = prefixBeforeVisualColumn(lineText, col1);
		if (keepSpaces) {
			result.append(static_cast<std::size_t>(width), ' ');
			result.append(visualColumnsFromLine(lineText, col2, targetCol));
			result.append(payload.data(), payload.size());
			result.append(suffixFromVisualColumn(lineText, targetCol));
		} else {
			result.append(visualColumnsFromLine(lineText, col2, targetCol + width));
			result.append(payload.data(), payload.size());
			result.append(suffixFromVisualColumn(lineText, targetCol + width));
		}
		return result;
	}

	result = prefixBeforeVisualColumn(lineText, col1);
	if (keepSpaces) result.append(static_cast<std::size_t>(width), ' ');
	result.append(visualColumnsFromLine(lineText, col2, targetCol));
	result.append(payload.data(), payload.size());
	result.append(suffixFromVisualColumn(lineText, targetCol + width));
	return result;
}

std::string moveStreamVisualColumnsWithinLine(std::string_view lineText, int sourceCol1, int sourceCol2, int destCol, std::string_view payload, bool insertMode, bool keepSpaces) {
	const int col1 = std::max(sourceCol1, 0);
	const int col2 = std::max(sourceCol2, col1);
	const int width = col2 - col1;
	int targetCol = std::max(destCol, 0);
	std::string base;
	std::string result;

	if (width <= 0 || targetCol == col1) return std::string(lineText);
	if (keepSpaces && !insertMode) {
		base = prefixBeforeVisualColumn(lineText, col1);
		base.append(static_cast<std::size_t>(width), ' ');
		base.append(suffixFromVisualColumn(lineText, col2));
	} else {
		base = eraseVisualColumnsFromLine(lineText, col1, col2);
		if (targetCol > col2) targetCol -= width;
		else if (targetCol > col1)
			targetCol = col1;
	}
	result = prefixBeforeVisualColumn(base, targetCol);
	result.append(payload.data(), payload.size());
	if (insertMode)
		result.append(suffixFromVisualColumn(base, targetCol));
	else
		result.append(suffixFromVisualColumn(base, targetCol + width));
	return result;
}

std::string indentStreamVisualColumnsWithinLine(std::string_view lineText, int sourceCol1, int sourceCol2, int destCol, std::string_view payload, bool insertMode, bool keepSpaces) {
	const int col1 = std::max(sourceCol1, 0);
	const int col2 = std::max(sourceCol2, col1);
	const int width = col2 - col1;
	const int targetCol = std::max(destCol, 0);
	std::string result;

	if (width <= 0 || targetCol == col1) return std::string(lineText);
	if (targetCol > col1) {
		const int gapWidth = targetCol - col1;

		result = prefixBeforeVisualColumn(lineText, col1);
		if (keepSpaces && !insertMode) result.append(static_cast<std::size_t>(width), ' ');
		else
			result.append(static_cast<std::size_t>(gapWidth), ' ');
		result.append(payload.data(), payload.size());
		if (insertMode)
			result.append(suffixFromVisualColumn(lineText, col2));
		else
			result.append(suffixFromVisualColumn(lineText, targetCol + width));
		return result;
	}

	result = prefixBeforeVisualColumn(lineText, targetCol);
	result.append(payload.data(), payload.size());
	if (keepSpaces && !insertMode) result.append(static_cast<std::size_t>(col1 - targetCol), ' ');
	result.append(suffixFromVisualColumn(lineText, col2));
	return result;
}

int nextBlockNumericTabColumn(int column, int tabSize) noexcept {
	const int normalizedTabSize = clampEditFormatTabSize(tabSize);
	const int safeColumn = std::max(1, column);
	return ((safeColumn - 1) / normalizedTabSize + 1) * normalizedTabSize + 1;
}

int prevBlockNumericTabColumn(int column, int tabSize) noexcept {
	const int normalizedTabSize = clampEditFormatTabSize(tabSize);
	const int safeColumn = std::max(1, column);
	if (safeColumn <= 1) return 1;
	return ((safeColumn - 2) / normalizedTabSize) * normalizedTabSize + 1;
}

int blockTabTargetColumn(const MREditSetupSettings &settings, int currentColumn, bool shiftRight) {
	std::string normalizedFormatLine;
	int resolvedLeftMargin = settings.leftMargin;
	int resolvedRightMargin = settings.rightMargin;
	const int safeColumn = std::max(1, currentColumn);

	static_cast<void>(normalizeEditFormatLine(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, normalizedFormatLine, &resolvedLeftMargin, &resolvedRightMargin, nullptr));
	if (shiftRight) {
		const int resolvedTarget = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, safeColumn);

		if (resolvedTarget > safeColumn) return resolvedTarget;
		return nextBlockNumericTabColumn(safeColumn, settings.tabSize);
	}
	if (safeColumn > resolvedRightMargin) return prevBlockNumericTabColumn(safeColumn, settings.tabSize);
	return prevResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, safeColumn);
}

int shiftedStreamTabColumn(const MREditSetupSettings &settings, int sourceColumn, bool shiftRight) {
	const int targetColumn = blockTabTargetColumn(settings, sourceColumn + 1, shiftRight);
	return std::max(0, targetColumn - 1);
}

int leadingIndentColumn(std::string_view lineText) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	int visual = 0;

	for (char ch : lineText) {
		if (ch == ' ')
			++visual;
		else if (ch == '\t')
			visual += blockTabDisplayWidth(settings, visual);
		else
			break;
	}
	return visual + 1;
}

int streamLastPartialPrefixUndentWidth(std::string_view lineText, int sourceCol2, const MREditSetupSettings &settings) {
	const int prefixWidth = std::max(sourceCol2, 0);
	const int leadingWidth = std::min(std::max(0, leadingIndentColumn(lineText) - 1), prefixWidth);

	if (leadingWidth <= 0) return 0;
	return leadingWidth - std::min(shiftedStreamTabColumn(settings, leadingWidth, false), leadingWidth);
}

std::string undentStreamLastPartialPrefix(std::string_view lineText, int sourceCol2, int removeWidth, bool insertMode, bool keepSpaces) {
	const int width = std::max(removeWidth, 0);

	if (width <= 0) return std::string(lineText);
	if (keepSpaces && !insertMode) {
		std::string result = visualColumnsFromLine(lineText, width, std::max(sourceCol2, width));

		result.append(static_cast<std::size_t>(width), ' ');
		result.append(suffixFromVisualColumn(lineText, sourceCol2));
		return result;
	}
	return eraseVisualColumnsFromLine(lineText, 0, width);
}

std::size_t leadingIndentByteCount(std::string_view lineText) noexcept {
	std::size_t count = 0;

	while (count < lineText.size() && (lineText[count] == ' ' || lineText[count] == '\t'))
		++count;
	return count;
}

std::string lineWithIndentColumn(std::string_view lineText, int targetColumn) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const std::size_t prefixBytes = leadingIndentByteCount(lineText);
	std::string result = buildEditIndentFill(settings, 1, std::max(1, targetColumn), configuredTabExpandSetting());

	result.append(lineText.data() + prefixBytes, lineText.size() - prefixBytes);
	return result;
}

int visualColumnForLineOffset(const std::string &text, const std::vector<std::size_t> &starts, std::size_t lineIndex, std::size_t targetOffset) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	if (starts.empty()) return 0;
	lineIndex = std::min(lineIndex, starts.size() - 1);
	const std::size_t lineStart = starts[lineIndex];
	const std::size_t lineEnd = lineContentEndForIndex(text, starts, lineIndex);
	std::size_t offset = lineStart;
	int visual = 0;

	targetOffset = std::min(std::max(targetOffset, lineStart), lineEnd);
	while (offset < targetOffset) {
		std::size_t next = offset;
		std::size_t width = 0;

		if (!nextBlockDisplayChar(text, lineStart, lineEnd, next, width, visual, settings)) break;
		if (next > targetOffset) break;
		visual += static_cast<int>(width);
		offset = next;
	}
	return visual;
}

void setCommittedLineGeometry(MRFEBlockGeometry &geometry, const std::string &text, const std::vector<std::size_t> &starts, std::size_t firstLine, std::size_t lastLine) {
	if (starts.empty()) return;
	firstLine = std::min(firstLine, starts.size() - 1);
	lastLine = std::min(std::max(lastLine, firstLine), starts.size() - 1);
	geometry.mode = MRFEBlockMode::Line;
	geometry.status = MRFEBlockStatus::Committed;
	geometry.hidden = false;
	geometry.line1 = firstLine;
	geometry.line2 = lastLine;
	geometry.col1 = 0;
	geometry.col2 = 0;
	geometry.anchorColumn = 0;
	geometry.cursorColumn = 0;
	geometry.rangeStart = starts[firstLine];
	geometry.rangeEnd = lastLine + 1 < starts.size() ? starts[lastLine + 1] : text.size();
	geometry.anchor = geometry.rangeStart;
	geometry.cursor = lineContentEndForIndex(text, starts, lastLine);
}

void setCommittedStreamGeometry(MRFEBlockGeometry &geometry, const std::string &text, const std::vector<std::size_t> &starts, std::size_t rangeStart, std::size_t rangeEnd) {
	geometry = MRFEBlockGeometry();
	geometry.mode = MRFEBlockMode::Stream;
	geometry.status = MRFEBlockStatus::Committed;
	geometry.hidden = false;
	geometry.rangeStart = std::min(rangeStart, text.size());
	geometry.rangeEnd = std::min(std::max(rangeEnd, geometry.rangeStart), text.size());
	geometry.anchor = geometry.rangeStart;
	geometry.cursor = geometry.rangeEnd;
	geometry.line1 = lineIndexForOffset(starts, geometry.rangeStart);
	geometry.line2 = lineIndexForOffset(starts, geometry.rangeEnd);
	geometry.anchorColumn = visualColumnForLineOffset(text, starts, geometry.line1, geometry.rangeStart);
	geometry.cursorColumn = visualColumnForLineOffset(text, starts, geometry.line2, geometry.rangeEnd);
	geometry.col1 = std::min(geometry.anchorColumn, geometry.cursorColumn);
	geometry.col2 = std::max(geometry.anchorColumn, geometry.cursorColumn);
}

std::string streamKeepSpaceText(std::string_view payload) {
	std::string replacement;

	replacement.reserve(payload.size());
	for (char ch : payload) {
		if (ch == '\r' || ch == '\n') replacement.push_back(ch);
		else
			replacement.push_back(' ');
	}
	return replacement;
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
	const MRFEBlockGeometry &geometry = MRFEBlockOpsTestPeer::geometry(ops);
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

	if (!MRFEBlockOpsTestPeer::hasVisibleBlock(ops) || MRFEBlockOpsTestPeer::mode(ops) != mode) {
		failureReason = std::string("Visible block mode mismatch in ") + phase + ": got visible=" + std::to_string(MRFEBlockOpsTestPeer::hasVisibleBlock(ops) ? 1 : 0) + " mode=" + std::to_string(static_cast<int>(MRFEBlockOpsTestPeer::mode(ops))) + ".";
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
		if (!MRFEBlockOpsTestPeer::beginLine(ops, editor)) {
			failureReason = std::string("Unable to begin line block in ") + phase + ".";
			return false;
		}
	} else if (mode == MRFEBlockMode::Column) {
		if (!MRFEBlockOpsTestPeer::beginColumn(ops, editor)) {
			failureReason = std::string("Unable to begin column block in ") + phase + ".";
			return false;
		}
	} else if (mode == MRFEBlockMode::Stream) {
		if (!MRFEBlockOpsTestPeer::beginStream(ops, editor)) {
			failureReason = std::string("Unable to begin stream block in ") + phase + ".";
			return false;
		}
	} else {
		failureReason = std::string("Invalid block mode in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, cursorLine, cursorColumn);
	if (!MRFEBlockOpsTestPeer::updateFromEditor(ops, editor) || !MRFEBlockOpsTestPeer::end(ops, editor)) {
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
	if (!MRFEBlockOpsTestPeer::beginColumn(ops, editor)) {
		failureReason = "Unable to begin column block in menu column stale mouse case.";
		return false;
	}
	placeEditorCursor(editor, text, starts, 2, 4);
	if (!MRFEBlockOpsTestPeer::updateFromEditor(ops, editor) || !MRFEBlockOpsTestPeer::end(ops, editor)) {
		failureReason = "Unable to update/end column block in menu column stale mouse case.";
		return false;
	}
	return checkEditorBlock(editor, ops, MRFEBlockMode::Column, MRFEBlockStatus::Committed, 0, 2, 1, 4, "menu column ignores stale mouse state", failureReason);
}

bool beginBlockForMode(MRFEBlockOps &ops, MRFileEditor &editor, MRFEBlockMode mode) {
	if (mode == MRFEBlockMode::Line) return MRFEBlockOpsTestPeer::beginLine(ops, editor);
	if (mode == MRFEBlockMode::Column) return MRFEBlockOpsTestPeer::beginColumn(ops, editor);
	if (mode == MRFEBlockMode::Stream) return MRFEBlockOpsTestPeer::beginStream(ops, editor);
	return false;
}

bool runEmptyBlockCommitTurnsOffCase(MRFEBlockMode mode, const std::string &text, std::size_t line, int column, const char *phase, std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 8), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, line, column);
	if (!beginBlockForMode(ops, editor, mode)) {
		failureReason = std::string("Unable to begin empty block in ") + phase + ".";
		return false;
	}
	if (!MRFEBlockOpsTestPeer::end(ops, editor)) {
		failureReason = std::string("Unable to end empty block in ") + phase + ".";
		return false;
	}
	if (MRFEBlockOpsTestPeer::hasStoredBlock(ops) || editor.blockOverlayState().active || editor.hasTextSelection()) {
		failureReason = std::string("Empty block commit must turn marking off in ") + phase + ".";
		return false;
	}
	return true;
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
	if (!MRFEBlockOpsTestPeer::end(ops, editor)) {
		failureReason = std::string("Unable to end block in ") + phase + ".";
		return false;
	}
	if (!MRFEBlockOpsTestPeer::toggleVisibility(ops, editor)) {
		failureReason = std::string("Unable to hide block in ") + phase + ".";
		return false;
	}
	if (MRFEBlockOpsTestPeer::hasVisibleBlock(ops) || !MRFEBlockOpsTestPeer::hasStoredBlock(ops) || editor.blockOverlayState().active) {
		failureReason = std::string("Hidden block must be stored but not visible in ") + phase + ".";
		return false;
	}
	if (!MRFEBlockOpsTestPeer::toggleVisibility(ops, editor)) {
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
	if (!MRFEBlockOpsTestPeer::end(ops, editor)) {
		failureReason = std::string("Unable to end old block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, 3, 2);
	if (!beginBlockForMode(ops, editor, newMode)) {
		failureReason = std::string("Unable to begin replacement block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, 3, 5);
	if (!MRFEBlockOpsTestPeer::end(ops, editor)) {
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
	if (!MRFEBlockOpsTestPeer::adoptMouseSelection(ops, editor, kbCtrlShift)) {
		failureReason = "Unable to adopt ctrl mouse selection as stream block.";
		return false;
	}
	if (!checkEditorBlock(editor, ops, MRFEBlockMode::Stream, MRFEBlockStatus::Committed, 0, 2, 1, 4, "ctrl mouse stream adoption over empty line", failureReason)) return false;

	MRFEBlockOpsTestPeer::clear(ops, editor);
	seedEditorSelection(editor, text, starts, 0, 1, 2, 4);
	if (!MRFEBlockOpsTestPeer::adoptMouseSelection(ops, editor, kbShift)) {
		failureReason = "Unable to adopt shift mouse selection as fallback stream block.";
		return false;
	}
	if (!checkEditorBlock(editor, ops, MRFEBlockMode::Stream, MRFEBlockStatus::Committed, 0, 2, 1, 4, "shift fallback mouse stream adoption over empty line", failureReason)) return false;

	MRFEBlockOpsTestPeer::clear(ops, editor);
	seedEditorSelection(editor, text, starts, 0, 1, 2, 4);
	if (!seedMouseColumnState(editor, 1, 4)) {
		failureReason = "Unable to seed mouse column state for alt adoption.";
		return false;
	}
	if (!MRFEBlockOpsTestPeer::adoptMouseSelection(ops, editor, kbAltShift)) {
		failureReason = "Unable to adopt alt mouse selection as column block.";
		return false;
	}
	if (MRFEBlockOpsTestPeer::geometry(ops).mode != MRFEBlockMode::Column) {
		failureReason = "Alt mouse selection must adopt a column block.";
		return false;
	}
	if (!checkEditorBlock(editor, ops, MRFEBlockMode::Column, MRFEBlockStatus::Committed, 0, 2, 1, 4, "alt mouse column adoption over empty line", failureReason)) return false;

	MRFEBlockOpsTestPeer::clear(ops, editor);
	seedEditorSelection(editor, text, starts, 2, 3, 0, 1);
	if (!MRFEBlockOpsTestPeer::adoptMouseSelection(ops, editor, static_cast<unsigned short>(kbCtrlShift | kbAltShift))) {
		failureReason = "Unable to adopt ctrl-alt mouse selection as line block.";
		return false;
	}
	return checkEditorBlock(editor, ops, MRFEBlockMode::Line, MRFEBlockStatus::Committed, 0, 2, 0, 0, "ctrl-alt mouse line adoption over empty line", failureReason);
}

bool runEditorLineMouseTrailingEmptyCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = "alpha\nbeta\n";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 8), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = "Unable to seed editor text in trailing empty line mouse adoption case.";
		return false;
	}
	seedEditorSelection(editor, text, starts, 0, 0, 2, 0);
	if (!MRFEBlockOpsTestPeer::adoptMouseSelection(ops, editor, static_cast<unsigned short>(kbCtrlShift | kbAltShift))) {
		failureReason = "Unable to adopt forward line selection ending at trailing empty line start.";
		return false;
	}
	if (!checkEditorBlock(editor, ops, MRFEBlockMode::Line, MRFEBlockStatus::Committed, 0, 1, 0, 0, "forward line selection ending before trailing empty line", failureReason)) return false;

	MRFEBlockOpsTestPeer::clear(ops, editor);
	seedEditorSelection(editor, text, starts, 2, 0, 0, 0);
	if (!MRFEBlockOpsTestPeer::adoptMouseSelection(ops, editor, static_cast<unsigned short>(kbCtrlShift | kbAltShift))) {
		failureReason = "Unable to adopt reverse line selection starting at trailing empty line start.";
		return false;
	}
	return checkEditorBlock(editor, ops, MRFEBlockMode::Line, MRFEBlockStatus::Committed, 0, 1, 0, 0, "reverse line selection ending before trailing empty line", failureReason);
}

std::string blockModeRegressionText() {
	MRFEArenaAllocator arena;
	std::string errorText;

	if (arena.loadFile("misc/blockmode.txt", &errorText)) return std::string(arena.view());
	return "#include <stdio.h>\r\n\r\nint main() {\r\n\r\n\tint unused;\r\n\r\n\tputs(\"Hello world\");\r\n\r\n\tputs(\"Hello world\");\r\n\r\n\ti=\"dummy\";\r\n\treturn(0);\r\n}\r\n";
}

bool runEditorLineMouseBlockModeFileCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = blockModeRegressionText();
	const std::vector<std::size_t> starts = lineStartsForText(text);
	if (starts.size() < 2 || starts.back() != text.size()) {
		failureReason = "Blockmode regression text must expose a trailing virtual empty line.";
		return false;
	}
	const std::size_t lastContentLine = starts.size() - 2;
	const std::size_t trailingVirtualLine = starts.size() - 1;

	{
		MRFileEditor editor(TRect(0, 0, 90, 12), nullptr, nullptr, nullptr, "");
		std::size_t renderedLine1 = 0;
		std::size_t renderedLine2 = 0;

		if (!editor.replaceBufferText(text.c_str())) {
			failureReason = "Unable to seed editor text in blockmode raw overlay rendering case.";
			return false;
		}
		editor.setBlockOverlayState(static_cast<int>(MRFEBlockMode::Line), 0, text.size(), true, false, 0, 0);
		if (!mrfeRenderedBlockOverlayLineRangeForRegression(editor, renderedLine1, renderedLine2)) {
			failureReason = "Unable to inspect rendered blockmode line overlay range.";
			return false;
		}
		if (renderedLine1 != 0 || renderedLine2 != lastContentLine) {
			failureReason = "Rendered blockmode line overlay must not include the trailing virtual empty line: renderedLine1=" + std::to_string(renderedLine1) + " renderedLine2=" + std::to_string(renderedLine2) +
			                " eofLineIndex=" + std::to_string(editor.lineIndexOfOffset(text.size())) + " eofLineStart=" + std::to_string(editor.lineStartOffset(text.size())) + " eofLineEnd=" + std::to_string(editor.lineEndOffset(text.size())) +
			                " textSize=" + std::to_string(text.size()) + ".";
			return false;
		}
	}

	{
		MRFileEditor editor(TRect(0, 0, 90, 12), nullptr, nullptr, nullptr, "");
		MRFEBlockOps ops;

		if (!editor.replaceBufferText(text.c_str())) {
			failureReason = "Unable to seed editor text in blockmode cursor line case.";
			return false;
		}
		placeEditorCursor(editor, text, starts, 0, 0);
		if (!MRFEBlockOpsTestPeer::beginLine(ops, editor)) {
			failureReason = "Unable to begin forward blockmode cursor line case.";
			return false;
		}
		placeEditorCursor(editor, text, starts, trailingVirtualLine, 0);
		if (!MRFEBlockOpsTestPeer::updateFromEditor(ops, editor) || !MRFEBlockOpsTestPeer::end(ops, editor)) {
			failureReason = "Unable to commit forward blockmode cursor line case.";
			return false;
		}
		if (!checkEditorBlock(editor, ops, MRFEBlockMode::Line, MRFEBlockStatus::Committed, 0, lastContentLine, 0, 0, "forward blockmode cursor line selection before trailing empty line", failureReason)) return false;

		MRFEBlockOpsTestPeer::clear(ops, editor);
		placeEditorCursor(editor, text, starts, trailingVirtualLine, 0);
		if (!MRFEBlockOpsTestPeer::beginLine(ops, editor)) {
			failureReason = "Unable to begin reverse blockmode cursor line case.";
			return false;
		}
		placeEditorCursor(editor, text, starts, 0, 0);
		if (!MRFEBlockOpsTestPeer::updateFromEditor(ops, editor) || !MRFEBlockOpsTestPeer::end(ops, editor)) {
			failureReason = "Unable to commit reverse blockmode cursor line case.";
			return false;
		}
		if (!checkEditorBlock(editor, ops, MRFEBlockMode::Line, MRFEBlockStatus::Committed, 0, lastContentLine, 0, 0, "reverse blockmode cursor line selection before trailing empty line", failureReason)) return false;
	}

	{
		MRFileEditor editor(TRect(0, 0, 90, 12), nullptr, nullptr, nullptr, "");
		MRFEBlockOps ops;

		if (!editor.replaceBufferText(text.c_str())) {
			failureReason = "Unable to seed editor text in blockmode line mouse adoption case.";
			return false;
		}
		seedEditorSelection(editor, text, starts, 0, 0, trailingVirtualLine, 0);
		if (!MRFEBlockOpsTestPeer::adoptMouseSelection(ops, editor, static_cast<unsigned short>(kbCtrlShift | kbAltShift))) {
			failureReason = "Unable to adopt forward blockmode line selection.";
			return false;
		}
		if (!checkEditorBlock(editor, ops, MRFEBlockMode::Line, MRFEBlockStatus::Committed, 0, lastContentLine, 0, 0, "forward blockmode line selection before trailing empty line", failureReason)) return false;

		MRFEBlockOpsTestPeer::clear(ops, editor);
		seedEditorSelection(editor, text, starts, trailingVirtualLine, 0, 0, 0);
		if (!MRFEBlockOpsTestPeer::adoptMouseSelection(ops, editor, static_cast<unsigned short>(kbCtrlShift | kbAltShift))) {
			failureReason = "Unable to adopt reverse blockmode line selection.";
			return false;
		}
		if (!checkEditorBlock(editor, ops, MRFEBlockMode::Line, MRFEBlockStatus::Committed, 0, lastContentLine, 0, 0, "reverse blockmode line selection before trailing empty line", failureReason)) return false;
	}

	return true;
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
	if (!MRFEBlockOpsTestPeer::adoptMouseSelection(ops, *editor, editor->lastMouseSelectionModifiers())) {
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

bool runEditorMouseDragBlockModeFileLineCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	const std::string text = blockModeRegressionText();
	const std::vector<std::size_t> starts = lineStartsForText(text);
	if (starts.size() < 2 || starts.back() != text.size()) {
		failureReason = "Blockmode regression text must expose a trailing virtual empty line for raw mouse case.";
		return false;
	}
	const std::size_t lastContentLine = starts.size() - 2;
	const std::size_t trailingVirtualLine = starts.size() - 1;

	QueuedMouseOwner owner(TRect(0, 0, 90, 12));
	MRFileEditor *editor = new MRFileEditor(TRect(0, 0, 90, 12), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;
	owner.insert(editor);

	if (!editor->replaceBufferText(text.c_str())) {
		failureReason = "Unable to seed editor text in blockmode raw mouse line case.";
		return false;
	}
	owner.queueMouseEvent(makeMouseEvent(evMouseMove, localXForEditorColumn(0), static_cast<int>(trailingVirtualLine), 0, static_cast<uchar>(mbLeftButton | mbRightButton)));
	owner.queueMouseEvent(makeMouseEvent(evMouseUp, localXForEditorColumn(0), static_cast<int>(trailingVirtualLine), 0, 0));
	TEvent event = makeMouseEvent(evMouseDown, localXForEditorColumn(0), 0, 0, static_cast<uchar>(mbLeftButton | mbRightButton));
	editor->handleEvent(event);
	const MRFileEditor::BlockOverlayState overlay = editor->blockOverlayState();
	if (!overlay.active || overlay.mode != static_cast<int>(MRFEBlockMode::Line) || overlayVisualLine2(*editor, overlay) != lastContentLine) {
		failureReason = "Raw blockmode mouse drag must show live line overlay only through the last touched content line.";
		return false;
	}
	if (!MRFEBlockOpsTestPeer::adoptMouseSelection(ops, *editor, editor->lastMouseSelectionModifiers())) {
		failureReason = "Unable to adopt raw blockmode mouse line selection.";
		return false;
	}
	return checkEditorBlock(*editor, ops, MRFEBlockMode::Line, MRFEBlockStatus::Committed, 0, lastContentLine, 0, 0, "raw blockmode both-button line selection", failureReason);
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
	if (!MRFEBlockOpsTestPeer::beginLine(ops, *editor)) {
		failureReason = "Unable to begin existing line block in mouse replacement case.";
		return false;
	}
	placeEditorCursor(*editor, text, starts, 2, 1);
	if (!MRFEBlockOpsTestPeer::end(ops, *editor)) {
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
	if (!MRFEBlockOpsTestPeer::adoptMouseSelection(ops, *editor, editor->lastMouseSelectionModifiers())) {
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
	if (!MRFEBlockOpsTestPeer::beginColumn(ops, *editor)) {
		failureReason = "Unable to begin existing column block in mouse click preservation case.";
		return false;
	}
	placeEditorCursor(*editor, text, starts, 2, 4);
	if (!MRFEBlockOpsTestPeer::end(ops, *editor)) {
		failureReason = "Unable to commit existing column block in mouse click preservation case.";
		return false;
	}
	if (!checkEditorBlock(*editor, ops, MRFEBlockMode::Column, MRFEBlockStatus::Committed, 0, 2, 1, 4, "existing column block before simple mouse click", failureReason)) return false;

	owner.queueMouseEvent(makeMouseEvent(evMouseUp, localXForEditorColumn(0), 0, 0, 0));
	TEvent event = makeMouseEvent(evMouseDown, localXForEditorColumn(0), 0, 0, mbLeftButton);
	editor->handleEvent(event);
	if (MRFEBlockOpsTestPeer::adoptMouseSelection(ops, *editor, editor->lastMouseSelectionModifiers())) {
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

	if (!MRFEBlockOpsTestPeer::deleteBlock(ops, editor, &errorText)) {
		failureReason = std::string("Unable to delete block in ") + phase + ": " + errorText;
		return false;
	}
	if (editor.snapshotText() != deletedText) {
		failureReason = std::string("Deleted text mismatch in ") + phase + ": got=\"" + escapedArenaPayload(editor.snapshotText()) + "\" expected=\"" + escapedArenaPayload(deletedText) + "\".";
		return false;
	}
	if (MRFEBlockOpsTestPeer::hasVisibleBlock(ops)) {
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

	if (!MRFEBlockOpsTestPeer::copyBlock(ops, editor, &errorText)) {
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

	if (!MRFEBlockOpsTestPeer::moveBlock(ops, editor, &errorText)) {
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
	if (!MRFEBlockOpsTestPeer::end(ops, editor)) {
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
	MREditSetupSettings settings = configuredEditSetupSettings();
	std::string errorText;

	settings.columnBlockMove = "DELETE_SPACE";
	ScopedEditSetupSettings scopedSettings(settings);

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
	MREditSetupSettings settings = configuredEditSetupSettings();
	const std::string text = "aa MOVE zz\nend";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MREditWindow window(TRect(0, 0, 80, 16), "window", 2203);
	MRFileEditor *editor = window.getEditor();
	std::string errorText;

	settings.columnBlockMove = "DELETE_SPACE";
	ScopedEditSetupSettings scopedSettings(settings);

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
	MREditSetupSettings settings = configuredEditSetupSettings();
	const std::string text = "aa MOVE zz\nend";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	QueuedMouseOwner owner(TRect(0, 0, 80, 16));
	MREditWindow *window = new MREditWindow(TRect(0, 0, 80, 16), "window-mouse-move-undo", 2204);
	MRFileEditor *editor = nullptr;
	std::string errorText;

	settings.columnBlockMove = "DELETE_SPACE";
	ScopedEditSetupSettings scopedSettings(settings);

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
	MREditSetupSettings settings = configuredEditSetupSettings();
	const std::string text = "aa MOVE zz\nend";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MREditWindow window(TRect(0, 0, 80, 16), "window-legacy-undo", 2205);
	MRFileEditor *editor = window.getEditor();
	std::string errorText;

	settings.columnBlockMove = "DELETE_SPACE";
	ScopedEditSetupSettings scopedSettings(settings);

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
	MREditSetupSettings settings = configuredEditSetupSettings();
	const std::string text = "aa MOVE zz\nend";
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MREditWindow window(TRect(0, 0, 80, 16), "window-ctrl-z-undo", 2206);
	MRFileEditor *editor = window.getEditor();
	std::string errorText;

	settings.columnBlockMove = "DELETE_SPACE";
	ScopedEditSetupSettings scopedSettings(settings);

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
	if (!MRFEBlockOpsTestPeer::updateFromEditor(ops, editor) || !MRFEBlockOpsTestPeer::end(ops, editor)) {
		failureReason = std::string("Unable to commit delete block in ") + phase + ".";
		return false;
	}
	return checkBlockDeleteUndoRedo(editor, ops, text, deletedText, phase, failureReason);
}

bool runColumnIndentCase(const std::string &text, std::size_t anchorLine, int anchorColumn, std::size_t cursorLine, int cursorColumn, int tabSize, bool initialInsertMode, const std::string &indentedText, int expectedCol1, int expectedCol2, const char *phase, std::string &failureReason, int rightMargin = -1) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	MREditSetupSettings settings = configuredEditSetupSettings();
	settings.tabSize = tabSize;
	if (rightMargin >= 1) {
		settings.leftMargin = 1;
		settings.rightMargin = rightMargin;
		settings.tabExpand = false;
	}
	settings.formatLine = defaultEditFormatLineForTabSize(settings.tabSize, settings.leftMargin, settings.rightMargin);
	ScopedEditSetupSettings scopedSettings(settings);
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;
	std::string errorText;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}
	editor.setInsertModeEnabled(initialInsertMode);
	placeEditorCursor(editor, text, starts, anchorLine, anchorColumn);
	if (!MRFEBlockOpsTestPeer::beginColumn(ops, editor)) {
		failureReason = std::string("Unable to begin column indent block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, cursorLine, cursorColumn);
	if (!MRFEBlockOpsTestPeer::updateFromEditor(ops, editor) || !MRFEBlockOpsTestPeer::end(ops, editor)) {
		failureReason = std::string("Unable to commit column indent block in ") + phase + ".";
		return false;
	}
	if (!MRFEBlockOpsTestPeer::indentBlock(ops, editor, &errorText)) {
		failureReason = std::string("Unable to indent column block in ") + phase + ": " + errorText;
		return false;
	}
	if (editor.snapshotText() != indentedText) {
		failureReason = std::string("Column indent text mismatch in ") + phase + ": got=\"" + escapedArenaPayload(editor.snapshotText()) + "\" expected=\"" + escapedArenaPayload(indentedText) + "\".";
		return false;
	}
	if (editor.insertModeEnabled() != initialInsertMode) {
		failureReason = std::string("Column indent must not mutate insert mode in ") + phase + ".";
		return false;
	}
	if (!checkEditorBlock(editor, ops, MRFEBlockMode::Column, MRFEBlockStatus::Committed, std::min(anchorLine, cursorLine), std::max(anchorLine, cursorLine), expectedCol1, expectedCol2, phase, failureReason)) return false;
	sendEditorCommand(editor, cmMrEditUndo);
	if (editor.snapshotText() != text || editor.insertModeEnabled() != initialInsertMode || !MRFEBlockOpsTestPeer::hasVisibleBlock(ops)) {
		failureReason = std::string("Undo after column indent must restore text, preserve insert mode and keep block visible in ") + phase + ".";
		return false;
	}
	sendEditorCommand(editor, cmMrEditRedo);
	if (editor.snapshotText() != indentedText || editor.insertModeEnabled() != initialInsertMode || !MRFEBlockOpsTestPeer::hasVisibleBlock(ops)) {
		failureReason = std::string("Redo after column indent must restore indented text, preserve insert mode and keep block visible in ") + phase + ".";
		return false;
	}
	return true;
}

bool runColumnUndentCase(const std::string &text, std::size_t anchorLine, int anchorColumn, std::size_t cursorLine, int cursorColumn, int tabSize, bool initialInsertMode, const std::string &undentedText, int expectedCol1, int expectedCol2, const char *phase, std::string &failureReason, int rightMargin = -1) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	MREditSetupSettings settings = configuredEditSetupSettings();
	settings.tabSize = tabSize;
	if (rightMargin >= 1) {
		settings.leftMargin = 1;
		settings.rightMargin = rightMargin;
		settings.tabExpand = false;
	}
	settings.formatLine = defaultEditFormatLineForTabSize(settings.tabSize, settings.leftMargin, settings.rightMargin);
	ScopedEditSetupSettings scopedSettings(settings);
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;
	std::string errorText;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}
	editor.setInsertModeEnabled(initialInsertMode);
	placeEditorCursor(editor, text, starts, anchorLine, anchorColumn);
	if (!MRFEBlockOpsTestPeer::beginColumn(ops, editor)) {
		failureReason = std::string("Unable to begin column undent block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, cursorLine, cursorColumn);
	if (!MRFEBlockOpsTestPeer::updateFromEditor(ops, editor) || !MRFEBlockOpsTestPeer::end(ops, editor)) {
		failureReason = std::string("Unable to commit column undent block in ") + phase + ".";
		return false;
	}
	if (!MRFEBlockOpsTestPeer::undentBlock(ops, editor, &errorText)) {
		failureReason = std::string("Unable to undent column block in ") + phase + ": " + errorText;
		return false;
	}
	if (editor.snapshotText() != undentedText) {
		failureReason = std::string("Column undent text mismatch in ") + phase + ": got=\"" + escapedArenaPayload(editor.snapshotText()) + "\" expected=\"" + escapedArenaPayload(undentedText) + "\".";
		return false;
	}
	if (editor.insertModeEnabled() != initialInsertMode) {
		failureReason = std::string("Column undent must not mutate insert mode in ") + phase + ".";
		return false;
	}
	if (!checkEditorBlock(editor, ops, MRFEBlockMode::Column, MRFEBlockStatus::Committed, std::min(anchorLine, cursorLine), std::max(anchorLine, cursorLine), expectedCol1, expectedCol2, phase, failureReason)) return false;
	sendEditorCommand(editor, cmMrEditUndo);
	if (editor.snapshotText() != text || editor.insertModeEnabled() != initialInsertMode || !MRFEBlockOpsTestPeer::hasVisibleBlock(ops)) {
		failureReason = std::string("Undo after column undent must restore text, preserve insert mode and keep block visible in ") + phase + ".";
		return false;
	}
	sendEditorCommand(editor, cmMrEditRedo);
	if (editor.snapshotText() != undentedText || editor.insertModeEnabled() != initialInsertMode || !MRFEBlockOpsTestPeer::hasVisibleBlock(ops)) {
		failureReason = std::string("Redo after column undent must restore undented text, preserve insert mode and keep block visible in ") + phase + ".";
		return false;
	}
	return true;
}

bool runLineIndentCase(const std::string &text, std::size_t anchorLine, std::size_t cursorLine, int tabSize, bool initialInsertMode, const std::string &indentedText, const char *phase, std::string &failureReason, int rightMargin = -1) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	MREditSetupSettings settings = configuredEditSetupSettings();
	settings.tabSize = tabSize;
	if (rightMargin >= 1) {
		settings.leftMargin = 1;
		settings.rightMargin = rightMargin;
		settings.tabExpand = false;
	}
	settings.formatLine = defaultEditFormatLineForTabSize(settings.tabSize, settings.leftMargin, settings.rightMargin);
	ScopedEditSetupSettings scopedSettings(settings);
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;
	std::string errorText;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}
	editor.setInsertModeEnabled(initialInsertMode);
	placeEditorCursor(editor, text, starts, anchorLine, 0);
	if (!MRFEBlockOpsTestPeer::beginLine(ops, editor)) {
		failureReason = std::string("Unable to begin line indent block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, cursorLine, 0);
	if (!MRFEBlockOpsTestPeer::updateFromEditor(ops, editor) || !MRFEBlockOpsTestPeer::end(ops, editor)) {
		failureReason = std::string("Unable to commit line indent block in ") + phase + ".";
		return false;
	}
	if (!MRFEBlockOpsTestPeer::indentBlock(ops, editor, &errorText)) {
		failureReason = std::string("Unable to indent line block in ") + phase + ": " + errorText;
		return false;
	}
	if (editor.snapshotText() != indentedText) {
		failureReason = std::string("Line indent text mismatch in ") + phase + ": got=\"" + escapedArenaPayload(editor.snapshotText()) + "\" expected=\"" + escapedArenaPayload(indentedText) + "\".";
		return false;
	}
	if (editor.insertModeEnabled() != initialInsertMode) {
		failureReason = std::string("Line indent must not mutate insert mode in ") + phase + ".";
		return false;
	}
	if (!MRFEBlockOpsTestPeer::refreshVisual(ops, editor)) {
		failureReason = std::string("Unable to refresh line indent block visual in ") + phase + ".";
		return false;
	}
	if (!checkEditorBlock(editor, ops, MRFEBlockMode::Line, MRFEBlockStatus::Committed, std::min(anchorLine, cursorLine), std::max(anchorLine, cursorLine), 0, 0, phase, failureReason)) return false;
	sendEditorCommand(editor, cmMrEditUndo);
	if (editor.snapshotText() != text || editor.insertModeEnabled() != initialInsertMode || !MRFEBlockOpsTestPeer::hasVisibleBlock(ops)) {
		failureReason = std::string("Undo after line indent must restore text, preserve insert mode and keep block visible in ") + phase + ".";
		return false;
	}
	sendEditorCommand(editor, cmMrEditRedo);
	if (editor.snapshotText() != indentedText || editor.insertModeEnabled() != initialInsertMode || !MRFEBlockOpsTestPeer::hasVisibleBlock(ops)) {
		failureReason = std::string("Redo after line indent must restore indented text, preserve insert mode and keep block visible in ") + phase + ".";
		return false;
	}
	return true;
}

bool runLineUndentCase(const std::string &text, std::size_t anchorLine, std::size_t cursorLine, int tabSize, bool initialInsertMode, const std::string &undentedText, const char *phase, std::string &failureReason, int rightMargin = -1) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	MREditSetupSettings settings = configuredEditSetupSettings();
	settings.tabSize = tabSize;
	if (rightMargin >= 1) {
		settings.leftMargin = 1;
		settings.rightMargin = rightMargin;
		settings.tabExpand = false;
	}
	settings.formatLine = defaultEditFormatLineForTabSize(settings.tabSize, settings.leftMargin, settings.rightMargin);
	ScopedEditSetupSettings scopedSettings(settings);
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;
	std::string errorText;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}
	editor.setInsertModeEnabled(initialInsertMode);
	placeEditorCursor(editor, text, starts, anchorLine, 0);
	if (!MRFEBlockOpsTestPeer::beginLine(ops, editor)) {
		failureReason = std::string("Unable to begin line undent block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, cursorLine, 0);
	if (!MRFEBlockOpsTestPeer::updateFromEditor(ops, editor) || !MRFEBlockOpsTestPeer::end(ops, editor)) {
		failureReason = std::string("Unable to commit line undent block in ") + phase + ".";
		return false;
	}
	if (!MRFEBlockOpsTestPeer::undentBlock(ops, editor, &errorText)) {
		failureReason = std::string("Unable to undent line block in ") + phase + ": " + errorText;
		return false;
	}
	if (editor.snapshotText() != undentedText) {
		failureReason = std::string("Line undent text mismatch in ") + phase + ": got=\"" + escapedArenaPayload(editor.snapshotText()) + "\" expected=\"" + escapedArenaPayload(undentedText) + "\".";
		return false;
	}
	if (editor.insertModeEnabled() != initialInsertMode) {
		failureReason = std::string("Line undent must not mutate insert mode in ") + phase + ".";
		return false;
	}
	if (!MRFEBlockOpsTestPeer::refreshVisual(ops, editor)) {
		failureReason = std::string("Unable to refresh line undent block visual in ") + phase + ".";
		return false;
	}
	if (!checkEditorBlock(editor, ops, MRFEBlockMode::Line, MRFEBlockStatus::Committed, std::min(anchorLine, cursorLine), std::max(anchorLine, cursorLine), 0, 0, phase, failureReason)) return false;
	sendEditorCommand(editor, cmMrEditUndo);
	if (editor.snapshotText() != text || editor.insertModeEnabled() != initialInsertMode || !MRFEBlockOpsTestPeer::hasVisibleBlock(ops)) {
		failureReason = std::string("Undo after line undent must restore text, preserve insert mode and keep block visible in ") + phase + ".";
		return false;
	}
	sendEditorCommand(editor, cmMrEditRedo);
	if (editor.snapshotText() != undentedText || editor.insertModeEnabled() != initialInsertMode || !MRFEBlockOpsTestPeer::hasVisibleBlock(ops)) {
		failureReason = std::string("Redo after line undent must restore undented text, preserve insert mode and keep block visible in ") + phase + ".";
		return false;
	}
	return true;
}

bool runStreamIndentCase(const std::string &text, std::size_t anchorLine, int anchorColumn, std::size_t cursorLine, int cursorColumn, int tabSize, bool initialInsertMode, bool indent, const std::string &shiftedText, std::size_t expectedLine1, std::size_t expectedLine2, int expectedCol1, int expectedCol2, const char *phase, std::string &failureReason, int rightMargin = -1) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	MREditSetupSettings settings = configuredEditSetupSettings();
	settings.tabSize = tabSize;
	if (rightMargin >= 1) {
		settings.leftMargin = 1;
		settings.rightMargin = rightMargin;
	}
	settings.formatLine = defaultEditFormatLineForTabSize(settings.tabSize, settings.leftMargin, settings.rightMargin);
	ScopedEditSetupSettings scopedSettings(settings);
	const std::vector<std::size_t> starts = lineStartsForText(text);
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
	MRFEBlockOps ops;
	std::string errorText;

	if (!editor.replaceBufferText(text.c_str())) {
		failureReason = std::string("Unable to seed editor text in ") + phase + ".";
		return false;
	}
	editor.setInsertModeEnabled(initialInsertMode);
	placeEditorCursor(editor, text, starts, anchorLine, anchorColumn);
	if (!MRFEBlockOpsTestPeer::beginStream(ops, editor)) {
		failureReason = std::string("Unable to begin stream indent block in ") + phase + ".";
		return false;
	}
	placeEditorCursor(editor, text, starts, cursorLine, cursorColumn);
	if (!MRFEBlockOpsTestPeer::updateFromEditor(ops, editor) || !MRFEBlockOpsTestPeer::end(ops, editor)) {
		failureReason = std::string("Unable to commit stream indent block in ") + phase + ".";
		return false;
	}
	if (indent) {
		if (!MRFEBlockOpsTestPeer::indentBlock(ops, editor, &errorText)) {
			failureReason = std::string("Unable to indent stream block in ") + phase + ": " + errorText;
			return false;
		}
	} else if (!MRFEBlockOpsTestPeer::undentBlock(ops, editor, &errorText)) {
		failureReason = std::string("Unable to undent stream block in ") + phase + ": " + errorText;
		return false;
	}
	if (editor.snapshotText() != shiftedText) {
		failureReason = std::string("Stream ") + (indent ? "indent" : "undent") + " text mismatch in " + phase + ": got=\"" + escapedArenaPayload(editor.snapshotText()) + "\" expected=\"" + escapedArenaPayload(shiftedText) + "\".";
		return false;
	}
	if (editor.insertModeEnabled() != initialInsertMode) {
		failureReason = std::string("Stream indent must not mutate insert mode in ") + phase + ".";
		return false;
	}
	if (!checkEditorBlock(editor, ops, MRFEBlockMode::Stream, MRFEBlockStatus::Committed, expectedLine1, expectedLine2, expectedCol1, expectedCol2, phase, failureReason)) return false;
	sendEditorCommand(editor, cmMrEditUndo);
	if (editor.snapshotText() != text || editor.insertModeEnabled() != initialInsertMode || !MRFEBlockOpsTestPeer::hasVisibleBlock(ops)) {
		failureReason = std::string("Undo after stream ") + (indent ? "indent" : "undent") + " must restore text, preserve insert mode and keep block visible in " + phase + ".";
		return false;
	}
	sendEditorCommand(editor, cmMrEditRedo);
	if (editor.snapshotText() != shiftedText || editor.insertModeEnabled() != initialInsertMode || !MRFEBlockOpsTestPeer::hasVisibleBlock(ops)) {
		failureReason = std::string("Redo after stream ") + (indent ? "indent" : "undent") + " must restore shifted text, preserve insert mode and keep block visible in " + phase + ".";
		return false;
	}
	return true;
}

bool runWindowBlockTabIndentCase(std::string &failureReason) {
	ScopedCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	MREditSetupSettings settings = configuredEditSetupSettings();
	settings.tabSize = 4;
	settings.formatLine = defaultEditFormatLineForTabSize(settings.tabSize, settings.leftMargin, settings.rightMargin);
	ScopedEditSetupSettings scopedSettings(settings);

	{
		const std::string text = "alpha\n\nbeta\nomega";
		MREditWindow window(TRect(0, 0, 80, 16), "line-tab-indent", 2401);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr || !window.replaceTextBuffer(text.c_str(), "line-tab-indent")) {
			failureReason = "Unable to seed line block Tab indent case.";
			return false;
		}
		editor->setCursorOffset(0);
		window.beginLineBlock();
		editor->setCursorOffset(editor->nextLineOffset(editor->nextLineOffset(0)));
		window.endBlock();
		sendWindowKeyEvent(window, kbTab);
		if (editor->snapshotText() != "   alpha\n   \n   beta\nomega" || !window.hasBlock() || window.blockStatus() != MREditWindow::bmLine || window.blockLine1() != 1 || window.blockLine2() != 3) {
			failureReason = "Tab must indent visible line blocks through the window key path without expanding the block.";
			return false;
		}
		sendWindowKeyEvent(window, kbShiftTab);
		if (editor->snapshotText() != text || !window.hasBlock() || window.blockStatus() != MREditWindow::bmLine || window.blockLine1() != 1 || window.blockLine2() != 3) {
			failureReason = "Shift-Tab must undent visible line blocks through the window key path without expanding the block.";
			return false;
		}
	}
	{
		const std::string text = "  ABC\n  DEF";
		const std::vector<std::size_t> starts = lineStartsForText(text);
		MREditWindow window(TRect(0, 0, 80, 16), "column-shifttab-undent", 2402);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr || !window.replaceTextBuffer(text.c_str(), "column-shifttab-undent")) {
			failureReason = "Unable to seed column block Shift-Tab undent case.";
			return false;
		}
		editor->setInsertModeEnabled(false);
		placeEditorCursor(*editor, text, starts, 0, 2);
		window.beginColumnBlock();
		placeEditorCursor(*editor, text, starts, 1, 5);
		window.endBlock();
		sendWindowKeyEvent(window, kbShiftTab);
		if (editor->snapshotText() != "ABC  \nDEF  " || !window.hasBlock() || window.blockStatus() != MREditWindow::bmColumn) {
			failureReason = "Shift-Tab must undent visible column blocks through the window key path.";
			return false;
		}
	}
	{
		const std::string text = "012345";
		const std::vector<std::size_t> starts = lineStartsForText(text);
		MREditWindow window(TRect(0, 0, 80, 16), "stream-tab-indent", 2403);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr || !window.replaceTextBuffer(text.c_str(), "stream-tab-indent")) {
			failureReason = "Unable to seed stream block Tab indent case.";
			return false;
		}
		editor->setInsertModeEnabled(true);
		placeEditorCursor(*editor, text, starts, 0, 1);
		window.beginStreamBlock();
		placeEditorCursor(*editor, text, starts, 0, 2);
		window.endBlock();
		sendWindowKeyEvent(window, kbTab);
		if (editor->snapshotText() != "0  12345" || !window.hasBlock() || window.blockStatus() != MREditWindow::bmStream || !editor->blockOverlayState().active || editor->hasTextSelection()) {
			failureReason = "Tab must indent visible stream blocks through the window key path without losing the persistent block.";
			return false;
		}
		sendWindowKeyEvent(window, kbShiftTab);
		if (editor->snapshotText() != "12345" || !window.hasBlock() || window.blockStatus() != MREditWindow::bmStream || !editor->blockOverlayState().active || editor->hasTextSelection()) {
			failureReason = "Shift-Tab must undent visible stream blocks through the window key path without losing the persistent block.";
			return false;
		}
	}
	{
		const std::string text = "aa MOVE zz\n0123456789";
		const std::vector<std::size_t> starts = lineStartsForText(text);
		MREditWindow window(TRect(0, 0, 80, 16), "stream-tab-multiline-no-rotate", 2404);
		MRFileEditor *editor = window.getEditor();

		if (editor == nullptr || !window.replaceTextBuffer(text.c_str(), "stream-tab-multiline-no-rotate")) {
			failureReason = "Unable to seed multiline stream block Tab indent case.";
			return false;
		}
		editor->setInsertModeEnabled(true);
		placeEditorCursor(*editor, text, starts, 0, 3);
		window.beginStreamBlock();
		placeEditorCursor(*editor, text, starts, 1, 5);
		window.endBlock();
		sendWindowKeyEvent(window, kbTab);
		if (editor->snapshotText() != "aa     MOVE zz\n   0123456789" || !window.hasBlock() || window.blockStatus() != MREditWindow::bmStream || !editor->blockOverlayState().active || editor->hasTextSelection()) {
			failureReason = "Tab must indent multiline visible stream blocks without rotating unmarked text into the last partial line.";
			return false;
		}
		sendWindowKeyEvent(window, kbShiftTab);
		if (editor->snapshotText() != text || !window.hasBlock() || window.blockStatus() != MREditWindow::bmStream || !editor->blockOverlayState().active || editor->hasTextSelection()) {
			failureReason = "Shift-Tab must undent the last partial line of a visible stream block through the window key path.";
			return false;
		}
	}
	return true;
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
	if (!MRFEBlockOpsTestPeer::updateFromEditor(ops, editor) || !MRFEBlockOpsTestPeer::end(ops, editor)) {
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
	if (!MRFEBlockOpsTestPeer::updateFromEditor(ops, editor) || !MRFEBlockOpsTestPeer::end(ops, editor)) {
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
	if (!MRFEBlockOpsTestPeer::loadBlockFromFile(ops, editor, sourcePath, &errorText)) {
		failureReason = "Unable to load stream block from file: " + errorText;
		return false;
	}
	if (editor.snapshotText() != "prefix HELLO\n\nworldsuffix") {
		failureReason = "Loaded stream block text mismatch.";
		return false;
	}
	if (MRFEBlockOpsTestPeer::geometry(ops).mode != MRFEBlockMode::Stream || MRFEBlockOpsTestPeer::geometry(ops).status != MRFEBlockStatus::Committed || MRFEBlockOpsTestPeer::geometry(ops).rangeStart != 7 || MRFEBlockOpsTestPeer::geometry(ops).rangeEnd != 7 + blockText.size()) {
		failureReason = "Loaded stream block geometry mismatch.";
		return false;
	}
	if (!MRFEBlockOpsTestPeer::saveBlockToFile(ops, editor, savePath, &errorText)) {
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
	if (!MRFEBlockOpsTestPeer::captureCurrentBlockPayload(ops, editor, copiedPayload, &errorText)) {
		failureReason = "Unable to capture stream block payload through arena: " + errorText;
		return false;
	}
	if (!targetEditor.replaceBufferText("copy:")) {
		failureReason = "Unable to seed editor for arena copy primitive case.";
		return false;
	}
	targetEditor.setCursorOffset(5);
	if (!MRFEBlockOpsTestPeer::insertPayloadAsStreamBlock(targetOps, targetEditor, copiedPayload, &errorText)) {
		failureReason = "Unable to insert captured arena payload as stream block: " + errorText;
		return false;
	}
	if (targetEditor.snapshotText() != "copy:" + blockText || MRFEBlockOpsTestPeer::geometry(targetOps).mode != MRFEBlockMode::Stream || MRFEBlockOpsTestPeer::geometry(targetOps).rangeStart != 5 || MRFEBlockOpsTestPeer::geometry(targetOps).rangeEnd != 5 + copiedPayload.size()) {
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
	if (!MRFEBlockOpsTestPeer::loadBlockFromFile(ops, editor, replaceSourcePath, &errorText)) {
		failureReason = "Unable to load stream block over selection: " + errorText;
		return false;
	}
	if (editor.snapshotText() != "aa X zz" || MRFEBlockOpsTestPeer::geometry(ops).rangeStart != 3 || MRFEBlockOpsTestPeer::geometry(ops).rangeEnd != 4) {
		failureReason = "Loaded stream block replacement mismatch.";
		return false;
	}

	if (!MRFEBlockOpsTestPeer::setCommittedBlock(ops, editor, MRFEBlockMode::Column, 0, 2, 1, 3)) {
		failureReason = "Unable to prepare visible non-stream block save rejection case.";
		return false;
	}
	if (MRFEBlockOpsTestPeer::saveBlockToFile(ops, editor, savePath, &errorText) || errorText.find("Only stream blocks") == std::string::npos) {
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

	if (!runEmptyBlockCommitTurnsOffCase(MRFEBlockMode::Stream, "alpha\n", 0, 2, "empty stream commit turns marking off", failureReason)) return false;
	if (!runEmptyBlockCommitTurnsOffCase(MRFEBlockMode::Column, "alpha\n", 0, 2, "empty column commit turns marking off", failureReason)) return false;
	if (!runEmptyBlockCommitTurnsOffCase(MRFEBlockMode::Line, "alpha\n", 1, 0, "empty line commit on trailing virtual line turns marking off", failureReason)) return false;

	const std::string editorText = "alpha\n\nbeta\nomega";
	if (!runEditorMarkingCase(MRFEBlockMode::Line, editorText, 0, 2, 2, 1, 0, 2, 0, 0, "editor line forward over empty line", failureReason)) return false;
	if (!runEditorMarkingCase(MRFEBlockMode::Line, editorText, 3, 2, 1, 0, 1, 3, 0, 0, "editor line reverse from empty line", failureReason)) return false;
	if (!runEditorMarkingCase(MRFEBlockMode::Stream, editorText, 0, 1, 0, 4, 0, 0, 1, 4, "editor stream same-line forward", failureReason)) return false;
	if (!runEditorMarkingCase(MRFEBlockMode::Stream, editorText, 2, 2, 0, 4, 0, 2, 2, 4, "editor stream reverse over empty line", failureReason)) return false;
	if (!runEditorMarkingCase(MRFEBlockMode::Stream, editorText, 0, 4, 1, 0, 0, 1, 0, 4, "editor stream forward into empty line", failureReason)) return false;
	if (!runEditorMarkingCase(MRFEBlockMode::Stream, editorText, 1, 0, 0, 4, 0, 1, 0, 4, "editor stream reverse from empty line", failureReason)) return false;
	if (!runEmptyBlockCommitTurnsOffCase(MRFEBlockMode::Stream, editorText, 1, 0, "editor empty stream on empty line turns marking off", failureReason)) return false;
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
	if (!runColumnIndentCase("012345\n\nabcdef", 0, 2, 2, 5, 4, true, "01 2345\n   \nab cdef", 3, 6, "column indent moves right and inserts over empty line", failureReason)) return false;
	if (!runColumnIndentCase("012345\nabcdef", 0, 2, 1, 5, 4, false, "01 234\nab cde", 3, 6, "column indent moves right and overwrites in overwrite mode", failureReason)) return false;
	if (!runColumnIndentCase("0123456789abcdef\nABCDEFGHIJKLMNOP", 0, 9, 1, 10, 4, true, "012345678 ab9cdef\nABCDEFGHI KLJMNOP", 12, 13, "column indent continues past right margin", failureReason, 8)) return false;
	if (!runColumnUndentCase("0123456789abcdef\nABCDEFGHIJKLMNOP", 0, 12, 1, 13, 4, true, "01234567c    89abdef\nABCDEFGHM    IJKLNOP", 8, 9, "column undent steps back numerically past right margin", failureReason, 8)) return false;
	if (!runLineIndentCase("alpha\n\nbeta\nomega", 0, 2, 4, true, "   alpha\n   \n   beta\nomega", "line indent moves full lines to next tab stop", failureReason)) return false;
	if (!runLineUndentCase("   alpha\r\n   \r\n   beta\r\nomega", 0, 2, 4, false, "alpha\r\n\r\nbeta\r\nomega", "CRLF line undent moves full lines to previous tab stop", failureReason)) return false;
	if (!runLineIndentCase("        alpha\n        beta", 0, 1, 4, true, "            alpha\n            beta", "line indent continues past right margin", failureReason, 8)) return false;
	if (!runLineUndentCase("                alpha\n                beta", 0, 1, 4, false, "            alpha\n            beta", "line undent steps back numerically past right margin", failureReason, 8)) return false;
	{
		MREditSetupSettings settings = configuredEditSetupSettings();
		settings.columnBlockMove = "DELETE_SPACE";
		ScopedEditSetupSettings scopedSettings(settings);

		if (!runStreamIndentCase("012345", 0, 1, 0, 2, 4, true, true, "0  12345", 0, 0, 3, 4, "single-cell stream indent shifts right without rotation in insert mode", failureReason)) return false;
		if (!runStreamIndentCase("012345", 0, 1, 0, 2, 4, false, true, "0  145", 0, 0, 3, 4, "single-cell stream indent overwrites right without rotation", failureReason)) return false;
		if (!runStreamIndentCase("012345", 0, 2, 0, 5, 4, true, false, "2345", 0, 0, 0, 3, "same-line stream undent shifts left without rotation in insert mode", failureReason)) return false;
		if (!runStreamIndentCase("aa MOVE zz\nmiddle\nend!!!!", 0, 3, 2, 3, 4, true, true, "aa     MOVE zz\n   middle\n   end!!!!", 0, 2, 6, 7, "multiline stream indent shifts partial edges without rotation", failureReason)) return false;
		if (!runStreamIndentCase("aa MOVE zz\nend", 0, 3, 1, 3, 4, true, true, "aa     MOVE zz\n   end", 0, 1, 6, 7, "multiline stream indent includes last partial line ending at EOL", failureReason)) return false;
		if (!runStreamIndentCase("aa MOVE zz\n\nend!!!!", 0, 3, 2, 3, 4, true, true, "aa     MOVE zz\n   \n   end!!!!", 0, 2, 6, 7, "multiline stream indent over empty middle line without rotation", failureReason)) return false;
		if (!runStreamIndentCase("aa     MOVE zz\n   \n!!!end!", 0, 7, 2, 6, 4, true, false, "aa MOVE zz\n\n!!!end!", 0, 2, 3, 6, "multiline stream undent over empty middle line without rotation", failureReason)) return false;
		if (!runStreamIndentCase("aa     MOVE zz\n   middle\n   0123456789", 0, 7, 2, 7, 4, true, false, "aa MOVE zz\nmiddle\n0123456789", 0, 2, 3, 4, "multiline stream undent mutates last partial line", failureReason)) return false;
		if (!runStreamIndentCase("aa MOVE zz\r\nmiddle\r\nend!!!!", 0, 3, 2, 3, 4, false, true, "aa     MOVE zz\r\n   middle\r\n   end!", 0, 2, 6, 7, "CRLF multiline stream indent overwrites partial edges without rotation", failureReason)) return false;
		if (!runStreamIndentCase("0123456789abcdef", 0, 9, 0, 10, 4, true, true, "012345678   9abcdef", 0, 0, 12, 13, "single-cell stream indent past right margin without rotation", failureReason, 8)) return false;
		if (!runStreamIndentCase("aa MOVE zz\n        middle\nend!!!!", 0, 3, 2, 3, 4, true, true, "aa  MOVE zz\n            middle\n   end!!!!", 0, 2, 4, 6, "multiline stream indent continues whole middle line past right margin without rotation", failureReason, 8)) return false;
	}
	if (!runWindowBlockTabIndentCase(failureReason)) return false;
	{
		MREditSetupSettings settings = configuredEditSetupSettings();
		settings.columnBlockMove = "DELETE_SPACE";
		ScopedEditSetupSettings scopedSettings(settings);

		if (!runColumnIndentCase("012345\nabcdef", 0, 1, 1, 2, 4, true, "023145\nacdbef", 3, 4, "column indent moves right and rotates with delete-space", failureReason)) return false;
		if (!runColumnIndentCase("012345\nabcdef", 0, 1, 1, 2, 4, false, "02145\nacbef", 3, 4, "column indent moves right and overwrites with delete-space", failureReason)) return false;
		if (!runColumnUndentCase("012345\nabcdef\nXYZ", 0, 2, 1, 5, 4, true, "234015\ncdeabf\nXYZ", 0, 3, "column undent inserts displaced left text with delete-space", failureReason)) return false;
		if (!runStreamIndentCase("012345", 0, 3, 0, 4, 4, true, false, "345", 0, 0, 0, 1, "single-cell stream undent shifts left without rotation with delete-space", failureReason)) return false;
	}
	if (!runColumnUndentCase("  ABC\r\n  DEF", 0, 2, 1, 5, 4, false, "ABC  \r\nDEF  ", 0, 3, "CRLF column undent overwrites regardless of overwrite mode", failureReason)) return false;
	if (!runColumnUndentCase("ABC\n\nDEF", 0, 0, 2, 3, 4, true, "ABC\n\nDEF", 0, 3, "column undent at left edge is no-op", failureReason)) return false;
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
		if (!runBlockMoveCase(MRFEBlockMode::Stream, "aa MOVE zz", 0, 3, 0, 7, 0, 0, "MOVEaa  zz", "stream partial first line move left insert", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Stream, "aa MOVE zz", 0, 3, 0, 7, 0, 0, "MOVEzz", "stream partial first line move left overwrite", failureReason, false)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Stream, "aa MOVE zz", 0, 3, 0, 7, 0, 10, "aa  zzMOVE", "stream partial last line move right insert", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Stream, "aa MOVE zz", 0, 3, 0, 7, 0, 10, "aa  zzMOVE", "stream partial last line move right overwrite", failureReason, false)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Stream, "aa MOVE\n\nzz", 0, 3, 2, 0, 2, 2, "aa zzMOVE\n\n", "stream multiline move delete-space insert", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Stream, "aa MOVE\r\n\r\nzz", 0, 3, 2, 0, 2, 2, "aa zzMOVE\r\n\r\n", "CRLF stream multiline move delete-space insert", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Line, "one\ntwo\nthree\nfour", 0, 0, 0, 0, 3, 0, "two\nthree\none\nfour", "line move", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Column, "012345\nabcdef", 0, 1, 1, 4, 0, 3, "045123\naefbcd", "column horizontal move right insert", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Column, "012345\nabcdef", 0, 1, 1, 4, 0, 3, "0  123\na  bcd", "column horizontal move right overwrite", failureReason, false)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Column, "012345\nabcdef", 0, 2, 1, 5, 0, 0, "234015\ncdeabf", "column horizontal move left insert delete-space", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Column, "012345\nabcdef", 0, 2, 1, 5, 0, 0, "2345\ncdef", "column horizontal move left overwrite delete-space", failureReason, false)) return false;
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

		if (!runColumnIndentCase("012345\nabcdef", 0, 1, 1, 2, 4, true, "0 21345\na cbdef", 3, 4, "column indent moves right and inserts with leave-space", failureReason)) return false;
		if (!runColumnIndentCase("012345\nabcdef", 0, 1, 1, 2, 4, false, "0 2145\na cbef", 3, 4, "column indent moves right and overwrites with leave-space", failureReason)) return false;
		if (!runColumnUndentCase("012345\nabcdef", 0, 2, 1, 5, 4, true, "234  015\ncde  abf", 0, 3, "column undent inserts displaced left text with leave-space", failureReason)) return false;
		if (!runColumnUndentCase("012345\nabcdef", 0, 2, 1, 5, 4, false, "234  5\ncde  f", 0, 3, "column undent overwrites with leave-space", failureReason)) return false;
		if (!runStreamIndentCase("012345", 0, 1, 0, 2, 4, true, true, "0  12345", 0, 0, 3, 4, "single-cell stream indent insert shifts without rotation and ignores leave-space origin", failureReason)) return false;
		if (!runStreamIndentCase("012345", 0, 1, 0, 2, 4, false, true, "0 145", 0, 0, 3, 4, "single-cell stream indent overwrite leaves source space without rotation", failureReason)) return false;
		if (!runStreamIndentCase("012345", 0, 2, 0, 5, 4, false, false, "234  5", 0, 0, 0, 3, "same-line stream undent overwrite keeps source spaces", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Stream, "aa MOVE zz\nend", 0, 3, 0, 7, 1, 3, "aa      zz\nendMOVE", "stream move leave-space insert", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Stream, "aa MOVE zz\nend!!!!", 0, 3, 0, 7, 1, 0, "aa      zz\nMOVE!!!", "stream move leave-space overwrite", failureReason, false)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Stream, "aa MOVE zz", 0, 3, 0, 7, 0, 0, "MOVEaa  zz", "stream horizontal left insert ignores leave-space", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Stream, "aa MOVE zz", 0, 3, 0, 7, 0, 0, "MOVE    zz", "stream horizontal left overwrite leave-space", failureReason, false)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Stream, "aa MOVE zz", 0, 3, 0, 7, 0, 10, "aa  zzMOVE", "stream horizontal right insert ignores leave-space", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Stream, "aa MOVE zz", 0, 3, 0, 7, 0, 10, "aa      zzMOVE", "stream horizontal right overwrite leave-space", failureReason, false)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Column, "012345\nabcdef\nXYZ", 0, 1, 1, 4, 2, 1, "0   45\na   ef\nX123YZ\n bcd", "column move leave-space", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Column, "012345\nabcdef", 0, 2, 1, 5, 0, 0, "234  015\ncde  abf", "column horizontal move left insert leave-space", failureReason)) return false;
		if (!runBlockMoveCase(MRFEBlockMode::Column, "012345\nabcdef", 0, 2, 1, 5, 0, 0, "234  5\ncde  f", "column horizontal move left overwrite leave-space", failureReason, false)) return false;
		if (!runInterWindowMoveCase(MRFEBlockMode::Column, "012345\nabcdef", 0, 1, 1, 4, "T0\nT1", 0, 1, "0   45\na   ef", "T1230\nTbcd1", "inter-window column move leave-space", failureReason)) return false;
		if (!runInterWindowMoveCase(MRFEBlockMode::Stream, "aa MOVE zz", 0, 3, 0, 7, "target:", 0, 7, "aa      zz", "target:MOVE", "inter-window stream move leave-space", failureReason)) return false;
		if (!runBlockDeleteCase(MRFEBlockMode::Stream, "aa DELETE\n\nzz", 0, 3, 2, 0, "aa       \n\nzz", "LF stream delete leave-space over empty line", failureReason)) return false;
		if (!runBlockDeleteCase(MRFEBlockMode::Stream, "aa DELETE\r\n\r\nzz", 0, 3, 2, 0, "aa       \r\n\r\nzz", "CRLF stream delete leave-space over empty line", failureReason)) return false;
		if (!runBlockDeleteCase(MRFEBlockMode::Column, "012345\n\nabcdef\nXYZ", 0, 1, 2, 4, "0   45\n    \na   ef\nXYZ", "LF column delete leave-space over empty line", failureReason)) return false;
		if (!runBlockDeleteCase(MRFEBlockMode::Column, "012345\r\n\r\nabcdef\r\nXYZ", 0, 1, 2, 4, "0   45\r\n    \r\na   ef\r\nXYZ", "CRLF column delete leave-space over empty line", failureReason)) return false;
	}
	{
		MREditSetupSettings settings = configuredEditSetupSettings();
		settings.columnBlockMove = "DELETE_SPACE";
		ScopedEditSetupSettings scopedSettings(settings);

		if (!runBlockDeleteCase(MRFEBlockMode::Stream, "aa DELETE\n\nzz", 0, 3, 2, 0, "aa zz", "stream delete over empty line", failureReason)) return false;
		if (!runBlockDeleteCase(MRFEBlockMode::Stream, "aa DELETE\r\n\r\nzz", 0, 3, 2, 0, "aa zz", "CRLF stream delete over empty line", failureReason)) return false;
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
		if (!MRFEBlockOpsTestPeer::beginStream(streamOps, streamEditor)) {
			failureReason = "Unable to begin stream transfer block.";
			return false;
		}
		streamEditor.setCursorOffset(streamText.size());
		if (!MRFEBlockOpsTestPeer::end(streamOps, streamEditor)) {
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
		if (!MRFEBlockOpsTestPeer::beginColumn(columnOps, columnEditor)) {
			failureReason = "Unable to begin column transfer block.";
			return false;
		}
		placeEditorCursor(columnEditor, columnText, columnStarts, 2, 4);
		if (!MRFEBlockOpsTestPeer::end(columnOps, columnEditor)) {
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
		if (!MRFEBlockOpsTestPeer::beginColumn(columnOps, columnEditor)) {
			failureReason = "Unable to begin CR-only column transfer block.";
			return false;
		}
		placeEditorCursor(columnEditor, columnText, columnStarts, 2, 4);
		if (!MRFEBlockOpsTestPeer::end(columnOps, columnEditor)) {
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
		if (!MRFEBlockOpsTestPeer::beginColumn(columnOps, columnEditor)) {
			failureReason = "Unable to begin CRLF column transfer block.";
			return false;
		}
		placeEditorCursor(columnEditor, columnText, columnStarts, 2, 4);
		if (!MRFEBlockOpsTestPeer::end(columnOps, columnEditor)) {
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
	if (!runEditorLineMouseTrailingEmptyCase(failureReason)) return false;
	if (!runEditorLineMouseBlockModeFileCase(failureReason)) return false;
	if (!runEditorMouseDragCaseForMode(MRFEBlockMode::Stream, "raw mouse left-button drag stream over empty line", failureReason)) return false;
	if (!runEditorMouseDragCaseForMode(MRFEBlockMode::Line, "raw mouse both-buttons drag line over empty line", failureReason, static_cast<uchar>(mbLeftButton | mbRightButton), 0)) return false;
	if (!runEditorMouseDragCaseForMode(MRFEBlockMode::Column, "raw mouse right-button drag column over empty line", failureReason)) return false;
	if (!runEditorMouseDragCaseForMode(MRFEBlockMode::Line, "raw mouse ctrl-alt drag line over empty line", failureReason)) return false;
	if (!runEditorMouseDragBlockModeFileLineCase(failureReason)) return false;
	if (!runEditorMouseDragReplacesExistingBlockCase(failureReason)) return false;
	if (!runEditorMouseClickPreservesExistingBlockCase(failureReason)) return false;

	failureReason.clear();
	return true;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
