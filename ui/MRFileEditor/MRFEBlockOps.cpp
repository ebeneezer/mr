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
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>


namespace {

struct ColumnLineReplacement {
	MRTextBufferModel::Range range;
	std::string text;
};

bool columnBlockTraceEnabled() noexcept {
	const char *value = std::getenv("MR_COLUMN_BLOCK_TRACE");
	return value != nullptr && value[0] != '\0' && value[0] != '0';
}

void appendColumnBlockTrace(std::string_view message) {
	if (!columnBlockTraceEnabled()) return;
	std::ofstream out(configuredLogFilePath(), std::ios::out | std::ios::app | std::ios::binary);
	if (out) out << "COLBLOCK ops " << message << '\n';
}

const char *blockModeName(MRFEBlockMode mode) noexcept {
	switch (mode) {
	case MRFEBlockMode::None:
		return "none";
	case MRFEBlockMode::Line:
		return "line";
	case MRFEBlockMode::Column:
		return "column";
	case MRFEBlockMode::Stream:
		return "stream";
	}
	return "unknown";
}

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

std::size_t lineBlockEndpointOffset(MRFileEditor &editor, std::size_t offset) {
	const std::size_t length = editor.bufferLength();
	offset = std::min(offset, length);
	if (offset != 0 && offset == length && editor.lineStartOffset(offset) == offset) return offset - 1;
	return offset;
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

enum class BlockOffsetAffinity {
	Start,
	End,
	EndWithinReplacement
};

std::size_t remapBlockOffsetForDocumentChange(std::size_t offset, std::size_t oldLength, std::size_t newLength, std::size_t editStart, std::size_t oldEditEnd, long long delta, BlockOffsetAffinity affinity) noexcept {
	offset = std::min(offset, oldLength);
	if (offset <= editStart) return std::min(offset, newLength);
	if (offset >= oldEditEnd) {
		const long long shifted = static_cast<long long>(offset) + delta;
		if (shifted <= 0) return 0;
		return std::min(static_cast<std::size_t>(shifted), newLength);
	}
	if (affinity != BlockOffsetAffinity::Start) {
		const long long shiftedEnd = static_cast<long long>(oldEditEnd) + delta;
		if (shiftedEnd <= 0) return 0;
		std::size_t mapped = std::min(static_cast<std::size_t>(shiftedEnd), newLength);
		if (affinity == BlockOffsetAffinity::EndWithinReplacement && mapped > editStart) --mapped;
		return mapped;
	}
	return std::min(editStart, newLength);
}

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

} // namespace


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
	return true;
}

std::vector<char> MRFEArenaAllocator::release() noexcept {
	std::vector<char> released;

	released.swap(mStorage);
	return released;
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
	mGeometry.anchorLine = mode == MRFEBlockMode::Column ? editor.blockCursorLineIndex() : editor.displayedCursorLineIndex();
	mGeometry.cursorLine = mGeometry.anchorLine;
	mGeometry.documentVersion = editor.documentVersion();
	switch (mode) {
	case MRFEBlockMode::Column:
		mGeometry.anchorColumn = std::max(0, editor.displayedCursorColumn());
		break;
	case MRFEBlockMode::Line:
	case MRFEBlockMode::Stream:
	case MRFEBlockMode::None:
		mGeometry.anchorColumn = std::max(0, static_cast<int>(editor.columnOfOffset(mGeometry.anchor)));
		break;
	}
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
	if (blockGeometryIsEmpty(mGeometry)) {
		clear(editor);
		return true;
	}
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
	if (mGeometry.status == MRFEBlockStatus::Inactive) return false;
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
	mGeometry.cursorLine = mGeometry.mode == MRFEBlockMode::Column ? editor.blockCursorLineIndex() : editor.displayedCursorLineIndex();
	switch (mGeometry.mode) {
	case MRFEBlockMode::Column:
		mGeometry.cursorColumn = std::max(0, editor.displayedCursorColumn());
		break;
	case MRFEBlockMode::Line:
	case MRFEBlockMode::Stream:
	case MRFEBlockMode::None:
		mGeometry.cursorColumn = std::max(0, static_cast<int>(editor.columnOfOffset(mGeometry.cursor)));
		break;
	}
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
	if (columnBlockTraceEnabled()) {
		std::ostringstream trace;
		trace << "adopt-input modifiers=" << modifiers << " mode=" << (column ? "column" : line ? "line" : "stream")
		      << " selectionAnchor=" << editor.selectionAnchorOffset() << " selectionCursor=" << editor.selectionCursorOffset()
		      << " cursor=" << editor.cursorOffset() << " lineCount=" << editor.bufferModel().lineCount() << " length=" << editor.bufferModel().length();
		appendColumnBlockTrace(trace.str());
	}

	mGeometry = MRFEBlockGeometry();
	mGeometry.mode = column ? MRFEBlockMode::Column : line ? MRFEBlockMode::Line : MRFEBlockMode::Stream;
	mGeometry.status = MRFEBlockStatus::Committed;
	mGeometry.hidden = false;
	mGeometry.documentVersion = editor.documentVersion();
	mGeometry.anchor = editor.selectionAnchorOffset();
	mGeometry.cursor = editor.selectionCursorOffset();
	mGeometry.anchorLine = editor.lineIndexOfOffset(mGeometry.anchor);
	mGeometry.cursorLine = editor.lineIndexOfOffset(mGeometry.cursor);
	if (column) {
		int anchorColumn = 0;
		int cursorColumn = 0;
		std::size_t anchorLine = 0;
		std::size_t cursorLine = 0;
		if (editor.lastMouseSelectionColumns(anchorColumn, cursorColumn)) {
			mGeometry.anchorColumn = std::max(anchorColumn, 0);
			mGeometry.cursorColumn = std::max(cursorColumn, 0);
		}
		if (editor.lastMouseSelectionLines(anchorLine, cursorLine)) {
			mGeometry.anchorLine = anchorLine;
			mGeometry.cursorLine = cursorLine;
		}
	} else {
		if (line && mGeometry.anchor != mGeometry.cursor) {
			if (mGeometry.anchor > mGeometry.cursor)
				mGeometry.anchor = lineBlockEndpointOffset(editor, mGeometry.anchor);
			else
				mGeometry.cursor = lineBlockEndpointOffset(editor, mGeometry.cursor);
		}
		mGeometry.anchorColumn = std::max(0, static_cast<int>(editor.columnOfOffset(mGeometry.anchor)));
		mGeometry.cursorColumn = std::max(0, static_cast<int>(editor.columnOfOffset(mGeometry.cursor)));
	}
	normalize(editor);
	if (columnBlockTraceEnabled()) {
		std::ostringstream trace;
		trace << "adopt-normalized mode=" << blockModeName(mGeometry.mode) << " anchor=" << mGeometry.anchor << " cursor=" << mGeometry.cursor
		      << " anchorColumn=" << mGeometry.anchorColumn << " cursorColumn=" << mGeometry.cursorColumn << " line1=" << mGeometry.line1 << " line2=" << mGeometry.line2
		      << " col1=" << mGeometry.col1 << " col2=" << mGeometry.col2 << " rangeStart=" << mGeometry.rangeStart << " rangeEnd=" << mGeometry.rangeEnd;
		appendColumnBlockTrace(trace.str());
	}
	if (blockGeometryIsEmpty(mGeometry)) {
		clear(editor);
		return true;
	}
	applySelection(editor);
	applyOverlay(editor);
	return true;
}

bool MRFEBlockOps::refreshVisual(MRFileEditor &editor) {
	if (mGeometry.status == MRFEBlockStatus::Inactive) return false;
	if (mGeometry.hidden) deactivateVisual(editor);
	else {
		normalize(editor);
		applySelection(editor);
		applyOverlay(editor);
	}
	return true;
}

bool MRFEBlockOps::remapAfterEditorChange(MRFileEditor &editor) {
	const MRTextBufferModel::DocumentChangeSet &change = editor.lastDocumentChangeSet();
	const std::size_t oldLength = change.oldLength;
	const std::size_t newLength = change.newLength;
	const MRTextBufferModel::Range touched = change.touchedRange.normalized();
	const long long delta = static_cast<long long>(newLength) - static_cast<long long>(oldLength);
	const std::size_t touchedLength = touched.length();
	const std::size_t editStart = std::min(touched.start, oldLength);
	std::size_t replacedOldLength = touchedLength;

	if (mGeometry.status == MRFEBlockStatus::Inactive || !change.changed || mGeometry.documentVersion != change.oldVersion || editor.documentVersion() != change.newVersion) return false;
	if (delta >= 0) {
		const std::size_t deltaUnsigned = static_cast<std::size_t>(delta);
		replacedOldLength = touchedLength > deltaUnsigned ? touchedLength - deltaUnsigned : 0;
	}
	if (replacedOldLength > oldLength - editStart) replacedOldLength = oldLength - editStart;
	const std::size_t oldEditEnd = editStart + replacedOldLength;

	const bool emptyEndpoints = mGeometry.anchor == mGeometry.cursor;
	const bool anchorStartsBlock = mGeometry.anchor < mGeometry.cursor;
	const BlockOffsetAffinity endAffinity = mGeometry.mode == MRFEBlockMode::Stream ? BlockOffsetAffinity::End : BlockOffsetAffinity::EndWithinReplacement;
	const BlockOffsetAffinity anchorAffinity = !emptyEndpoints && !anchorStartsBlock ? endAffinity : BlockOffsetAffinity::Start;
	const BlockOffsetAffinity cursorAffinity = !emptyEndpoints && anchorStartsBlock ? endAffinity : BlockOffsetAffinity::Start;
	mGeometry.anchor = remapBlockOffsetForDocumentChange(mGeometry.anchor, oldLength, newLength, editStart, oldEditEnd, delta, anchorAffinity);
	mGeometry.cursor = remapBlockOffsetForDocumentChange(mGeometry.cursor, oldLength, newLength, editStart, oldEditEnd, delta, cursorAffinity);
	if (mGeometry.mode != MRFEBlockMode::Column) {
		mGeometry.anchorColumn = std::max(0, static_cast<int>(editor.columnOfOffset(mGeometry.anchor)));
		mGeometry.cursorColumn = std::max(0, static_cast<int>(editor.columnOfOffset(mGeometry.cursor)));
	} else {
		const std::size_t realLineCount = std::max<std::size_t>(1, editor.bufferModel().lineCount());
		if (mGeometry.anchorLine < realLineCount && mGeometry.cursorLine < realLineCount) {
			mGeometry.anchorLine = editor.lineIndexOfOffset(mGeometry.anchor);
			mGeometry.cursorLine = editor.lineIndexOfOffset(mGeometry.cursor);
		}
	}
	mGeometry.documentVersion = change.newVersion;
	if (mGeometry.hidden) return true;
	normalize(editor);
	applySelection(editor);
	applyOverlay(editor);
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
	if (end < start) std::swap(start, end);
	transaction.replace(MRTextBufferModel::Range(start, end), insertArena.view());
	if (!editor.applyStagedTransaction(transaction, start + insertArena.size(), start + insertArena.size(), start + insertArena.size(), true).applied()) {
		if (errorText != nullptr) *errorText = "Unable to insert block payload.";
		return false;
	}
	start += static_cast<std::size_t>(std::max(0, paddingColumns));
	return setCommittedStream(editor, start, start + arena.size());
}

bool MRFEBlockOps::runBlockOperation(MRFileEditor &editor, BlockOperation operation, std::string *errorText) {
	switch (operation) {
	case BlockOperation::Copy:
		return runWindowBlockOperation(editor, *this, editor, 0, 0, BlockOperation::Copy, errorText);
	case BlockOperation::Move:
		return executeCursorMove(editor, errorText);
	case BlockOperation::Delete:
		return executeDelete(editor, errorText);
	case BlockOperation::Indent:
		return shiftCurrentBlockToTab(editor, true, errorText);
	case BlockOperation::Undent:
		return shiftCurrentBlockToTab(editor, false, errorText);
	}
	if (errorText != nullptr) *errorText = "No block operation selected.";
	return false;
}

bool MRFEBlockOps::executeCursorMove(MRFileEditor &editor, std::string *errorText) {
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
	switch (sourceGeometry.mode) {
	case MRFEBlockMode::Column: {
		text = editor.snapshotText();
		starts = lineStartsForText(text);
		const std::size_t targetLine = lineIndexForOffset(starts, targetCursor);
		const int destCol = std::max(0, editor.displayedCursorColumn());

		if (targetLine == sourceGeometry.line1) return shiftCurrentColumnBlockHorizontally(editor, destCol, ColumnHorizontalShiftMode::UseEditorInsertMode, errorText);
		break;
	}
	case MRFEBlockMode::Stream: {
		text = editor.snapshotText();
		starts = lineStartsForText(text);
		const std::size_t targetLine = lineIndexForOffset(starts, targetCursor);
		const int destCol = std::max(0, editor.displayedCursorColumn());

		if (sourceGeometry.line1 == sourceGeometry.line2 && targetLine == sourceGeometry.line1) return shiftCurrentStreamBlockHorizontally(editor, destCol, StreamHorizontalShiftMode::MoveToCursor, errorText);
		break;
	}
	case MRFEBlockMode::Line:
	case MRFEBlockMode::None:
		break;
	}
	if (!captureTransferPayload(editor, transferArena, errorText)) return false;
	payloadStorage = transferArena.release();
	const std::string_view payload = payloadView(payloadStorage);
	if (payload.empty()) {
		if (errorText != nullptr) *errorText = "Block payload is empty.";
		return false;
	}

	text = editor.snapshotText();
	finalText = text;
	switch (sourceGeometry.mode) {
	case MRFEBlockMode::Stream: {
		const std::size_t eraseStart = sourceGeometry.rangeStart;
		const std::size_t eraseEnd = sourceGeometry.rangeEnd;
		const std::size_t eraseLength = eraseEnd - eraseStart;
		const int paddingColumns = editor.paddingColumnsBeforeInsertAtCursor();
		const bool keepSpaces = configuredEditSetupSettings().columnBlockMove == "LEAVE_SPACE";
		std::string insertion;
		std::string sourceReplacement;
		std::size_t insertOffset = targetCursor;

		if (targetCursor > eraseStart && targetCursor < eraseEnd) {
			if (errorText != nullptr) *errorText = "Move target is inside the block.";
			return false;
		}
		if (targetCursor >= eraseEnd && !keepSpaces) insertOffset = targetCursor - eraseLength;
		else if (targetCursor >= eraseStart && !keepSpaces)
			insertOffset = eraseStart;
		if (paddingColumns > 0) insertion.append(static_cast<std::size_t>(paddingColumns), ' ');
		insertion.append(payload.data(), payload.size());
		if (keepSpaces) {
			sourceReplacement = streamKeepSpaceText(std::string_view(text.data() + eraseStart, eraseLength));
			finalText.replace(eraseStart, eraseLength, sourceReplacement);
		} else
			finalText.erase(eraseStart, eraseLength);
		if (editor.insertModeEnabled())
			finalText.insert(insertOffset, insertion);
		else {
			const std::size_t overwriteEnd = std::min(finalText.size(), insertOffset + insertion.size());
			finalText.replace(insertOffset, overwriteEnd - insertOffset, insertion);
		}
		starts = lineStartsForText(finalText);
		setCommittedStreamGeometry(targetGeometry, finalText, starts, insertOffset + static_cast<std::size_t>(std::max(0, paddingColumns)), insertOffset + insertion.size());
		cursor = targetGeometry.rangeStart;
		break;
	}
	case MRFEBlockMode::Line: {
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
		setCommittedLineGeometry(targetGeometry, finalText, starts, lineIndexForOffset(starts, insertOffset), lineIndexForOffset(starts, insertOffset) + (sourceGeometry.line2 >= sourceGeometry.line1 ? sourceGeometry.line2 - sourceGeometry.line1 : 0));
		cursor = targetGeometry.rangeStart;
		break;
	}
	case MRFEBlockMode::Column: {
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
		break;
	}
	case MRFEBlockMode::None:
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

bool MRFEBlockOps::runWindowBlockOperation(MRFileEditor &sourceEditor, MRFEBlockOps &targetOps, MRFileEditor &targetEditor, int sourceWindowId, int targetWindowId, BlockOperation operation, std::string *errorText) {
	MRFEArenaAllocator transferArena;
	TransferMessage message;

	if (operation == BlockOperation::Copy) {
		if (!prepareTransferMessage(sourceEditor, sourceWindowId, targetWindowId, TransferMode::Copy, transferArena, message, errorText)) return false;
		return targetOps.insertTransferMessage(targetEditor, message, errorText);
	}
	if (operation != BlockOperation::Move) {
		if (errorText != nullptr) *errorText = "Window block operation must be copy or move.";
		return false;
	}
	if (&sourceEditor == &targetEditor) return executeCursorMove(sourceEditor, errorText);
	if (sourceEditor.isReadOnly()) {
		if (errorText != nullptr) *errorText = "Source editor is read-only.";
		return false;
	}
	if (!prepareTransferMessage(sourceEditor, sourceWindowId, targetWindowId, TransferMode::Move, transferArena, message, errorText)) return false;
	if (!targetOps.insertTransferMessage(targetEditor, message, errorText)) return false;
	return removeCurrentBlockForMove(sourceEditor, errorText);
}

bool MRFEBlockOps::executeDelete(MRFileEditor &editor, std::string *errorText) {
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
	switch (mGeometry.mode) {
	case MRFEBlockMode::Stream:
	case MRFEBlockMode::Line:
		if (mGeometry.rangeStart < mGeometry.rangeEnd) ranges.push_back(MRTextBufferModel::Range(mGeometry.rangeStart, mGeometry.rangeEnd));
		cursor = mGeometry.rangeStart;
		break;
	case MRFEBlockMode::Column:
		if (configuredEditSetupSettings().columnBlockMove == "LEAVE_SPACE") collectColumnClearReplacements(text, starts, mGeometry, columnReplacements);
		else
			collectColumnEraseReplacements(text, starts, mGeometry, columnReplacements);
		cursor = mGeometry.rangeStart;
		if (!columnReplacements.empty()) cursor = columnReplacements.front().range.start;
		break;
	case MRFEBlockMode::None:
		if (errorText != nullptr) *errorText = "No block mode selected.";
		return false;
	}
	if (ranges.empty() && columnReplacements.empty()) {
		clear(editor);
		editor.setCursorOffset(cursor);
		return true;
	}
	switch (mGeometry.mode) {
	case MRFEBlockMode::Stream:
		if (configuredEditSetupSettings().columnBlockMove == "LEAVE_SPACE") {
			const std::string replacement = streamKeepSpaceText(std::string_view(text.data() + mGeometry.rangeStart, mGeometry.rangeEnd - mGeometry.rangeStart));

			transaction.replace(MRTextBufferModel::Range(mGeometry.rangeStart, mGeometry.rangeEnd), replacement);
			break;
		}
		for (std::size_t index = ranges.size(); index > 0; --index)
			transaction.erase(ranges[index - 1]);
		break;
	case MRFEBlockMode::Line:
		for (std::size_t index = ranges.size(); index > 0; --index)
			transaction.erase(ranges[index - 1]);
		break;
	case MRFEBlockMode::Column:
		stageColumnLineReplacements(transaction, columnReplacements);
		break;
	case MRFEBlockMode::None:
		break;
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

bool MRFEBlockOps::shiftCurrentColumnBlockToTab(MRFileEditor &editor, bool indent, std::string *errorText) {
	const MREditSetupSettings settings = configuredEditSetupSettings();

	if (errorText != nullptr) errorText->clear();
	normalize(editor);
	if (mGeometry.mode != MRFEBlockMode::Column) {
		if (errorText != nullptr) *errorText = "Column block required.";
		return false;
	}
	const int targetColumn = blockTabTargetColumn(settings, mGeometry.col1 + 1, indent);
	const int destCol = std::max(0, targetColumn - 1);
	return shiftCurrentColumnBlockHorizontally(editor, destCol, ColumnHorizontalShiftMode::UseEditorInsertMode, errorText);
}

bool MRFEBlockOps::shiftCurrentLineBlockToTab(MRFileEditor &editor, bool indent, std::string *errorText) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	std::string text;
	std::vector<std::size_t> starts;
	std::vector<ColumnLineReplacement> replacements;
	MRTextBufferModel::StagedTransaction transaction(editor.readSnapshot(), indent ? "indent-line-block" : "undent-line-block");

	if (errorText != nullptr) errorText->clear();
	if (editor.isReadOnly()) {
		if (errorText != nullptr) *errorText = "Editor is read-only.";
		return false;
	}
	normalize(editor);
	if (mGeometry.mode != MRFEBlockMode::Line) {
		if (errorText != nullptr) *errorText = "Line block required.";
		return false;
	}
	text = editor.snapshotText();
	starts = lineStartsForText(text);
	if (starts.empty() || mGeometry.line1 >= starts.size()) {
		if (errorText != nullptr) *errorText = "Line block range is outside the editor buffer.";
		return false;
	}
	const std::size_t firstLine = mGeometry.line1;
	const std::size_t lastLine = std::min(mGeometry.line2, starts.size() - 1);
	int blockIndentColumn = 0;
	for (std::size_t line = firstLine; line <= lastLine; ++line) {
		const std::size_t lineStart = starts[line];
		const std::size_t lineEnd = lineContentEndForIndex(text, starts, line);
		const std::string_view lineText(text.data() + lineStart, lineEnd - lineStart);
		if (lineText.find_first_not_of(" \t") == std::string_view::npos) continue;
		const int currentColumn = leadingIndentColumn(lineText);
		if (blockIndentColumn == 0 || currentColumn < blockIndentColumn) blockIndentColumn = currentColumn;
	}
	const int blockTargetColumn = blockIndentColumn == 0 ? 0 : blockTabTargetColumn(settings, blockIndentColumn, indent);
	const int blockIndentDelta = blockTargetColumn - blockIndentColumn;
	for (std::size_t line = firstLine; line <= lastLine; ++line) {
		const std::size_t lineStart = starts[line];
		const std::size_t lineEnd = lineContentEndForIndex(text, starts, line);
		const std::string_view lineText(text.data() + lineStart, lineEnd - lineStart);
		if (lineText.find_first_not_of(" \t") == std::string_view::npos) continue;
		const int currentColumn = leadingIndentColumn(lineText);
		const int targetColumn = std::max(1, currentColumn + blockIndentDelta);
		const std::string replacement = lineWithIndentColumn(lineText, targetColumn);

		if ((indent || targetColumn < currentColumn) && (replacement.size() != lineEnd - lineStart || replacement != lineText)) replacements.push_back(ColumnLineReplacement{MRTextBufferModel::Range(lineStart, lineEnd), replacement});
	}
	if (!replacements.empty()) {
		stageColumnLineReplacements(transaction, replacements);
		if (!editor.applyStagedTransaction(transaction, mGeometry.rangeStart, mGeometry.rangeStart, mGeometry.rangeStart, true).applied()) {
			if (errorText != nullptr) *errorText = indent ? "Unable to indent line block." : "Unable to undent line block.";
			return false;
		}
		text = editor.snapshotText();
		starts = lineStartsForText(text);
	}
	setCommittedLineGeometry(mGeometry, text, starts, firstLine, lastLine);
	mGeometry.documentVersion = editor.documentVersion();
	applySelection(editor);
	applyOverlay(editor);
	editor.setCursorOffset(mGeometry.rangeStart);
	return true;
}

bool MRFEBlockOps::shiftCurrentStreamBlockToTab(MRFileEditor &editor, bool indent, std::string *errorText) {
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
	if (mGeometry.mode != MRFEBlockMode::Stream) {
		if (errorText != nullptr) *errorText = "Stream block required.";
		return false;
	}
	if (mGeometry.rangeStart >= mGeometry.rangeEnd || !setCommittedBlock(editor, MRFEBlockMode::Line, mGeometry.rangeStart, mGeometry.rangeEnd - 1)) {
		if (errorText != nullptr) *errorText = "Unable to normalize stream block to lines.";
		return false;
	}
	return shiftCurrentLineBlockToTab(editor, indent, errorText);
}

bool MRFEBlockOps::shiftCurrentStreamBlockHorizontally(MRFileEditor &editor, int destCol, StreamHorizontalShiftMode mode, std::string *errorText) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	std::string text;
	std::vector<std::size_t> starts;
	std::vector<ColumnLineReplacement> replacements;
	MRFEBlockGeometry sourceGeometry;
	MRFEBlockGeometry targetGeometry;
	MRTextBufferModel::StagedTransaction transaction(editor.readSnapshot(), destCol > mGeometry.col1 ? "indent-stream-block" : "shift-stream-block");

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
	if (mGeometry.mode != MRFEBlockMode::Stream) {
		if (errorText != nullptr) *errorText = "Stream block required.";
		return false;
	}
	sourceGeometry = mGeometry;
	text = editor.snapshotText();
	starts = lineStartsForText(text);
	if (starts.empty() || sourceGeometry.rangeStart > text.size() || sourceGeometry.rangeEnd > text.size() || sourceGeometry.rangeStart >= sourceGeometry.rangeEnd) {
		if (errorText != nullptr) *errorText = "Stream block range is outside the editor buffer.";
		return false;
	}
	const bool insertMode = editor.insertModeEnabled();
	const bool keepSpaces = configuredEditSetupSettings().columnBlockMove == "LEAVE_SPACE";
	const bool indentToTab = mode == StreamHorizontalShiftMode::IndentToTab;
	const bool shiftRight = destCol > sourceGeometry.col1;
	const std::size_t firstLine = sourceGeometry.line1;
	const std::size_t lastLine = std::min(sourceGeometry.line2, starts.size() - 1);
	const std::size_t firstLineStart = starts[std::min(firstLine, starts.size() - 1)];
	const std::size_t lastLineStart = starts[lastLine];
	const std::size_t lastLineBlockEnd = lastLine + 1 < starts.size() ? starts[lastLine + 1] : text.size();
	const bool targetStartAtLineStart = sourceGeometry.rangeStart <= firstLineStart;
	const bool targetEndAtLineStart = sourceGeometry.rangeEnd <= lastLineStart;
	const bool targetEndAfterWholeLine = !targetEndAtLineStart && sourceGeometry.rangeEnd >= lastLineBlockEnd;
	int targetStartColumn = 0;
	int targetEndColumn = 0;
	int wholeLineIndentColumn = 0;
	for (std::size_t line = firstLine; line <= lastLine; ++line) {
		const std::size_t lineStart = starts[line];
		const std::size_t lineContentEnd = lineContentEndForIndex(text, starts, line);
		const std::size_t lineBlockEnd = line + 1 < starts.size() ? starts[line + 1] : text.size();
		if (sourceGeometry.rangeStart > lineStart || sourceGeometry.rangeEnd < lineBlockEnd) continue;
		const std::string_view lineText(text.data() + lineStart, lineContentEnd - lineStart);
		if (lineText.find_first_not_of(" \t") == std::string_view::npos) continue;
		const int currentColumn = leadingIndentColumn(lineText);
		if (wholeLineIndentColumn == 0 || currentColumn < wholeLineIndentColumn) wholeLineIndentColumn = currentColumn;
	}
	const int wholeLineTargetColumn = wholeLineIndentColumn == 0 ? 0 : blockTabTargetColumn(settings, wholeLineIndentColumn, shiftRight);
	const int wholeLineIndentDelta = wholeLineTargetColumn - wholeLineIndentColumn;

	for (std::size_t line = firstLine; line <= lastLine; ++line) {
		const std::size_t lineStart = starts[line];
		const std::size_t lineContentEnd = lineContentEndForIndex(text, starts, line);
		const std::size_t lineBlockEnd = line + 1 < starts.size() ? starts[line + 1] : text.size();
		const bool wholeLine = sourceGeometry.rangeStart <= lineStart && sourceGeometry.rangeEnd >= lineBlockEnd;
		const bool firstPartial = line == firstLine && sourceGeometry.rangeStart > lineStart && sourceGeometry.rangeStart < lineContentEnd;
		const bool lastPartial = line == lastLine && sourceGeometry.rangeEnd > lineStart && sourceGeometry.rangeEnd <= lineContentEnd;

		if (wholeLine) {
			const std::string_view lineText(text.data() + lineStart, lineContentEnd - lineStart);
			if (lineText.find_first_not_of(" \t") == std::string_view::npos) continue;
			const int currentColumn = leadingIndentColumn(lineText);
			const int targetColumn = std::max(1, currentColumn + wholeLineIndentDelta);
			const std::string replacement = lineWithIndentColumn(lineText, targetColumn);

			if (replacement.size() != lineContentEnd - lineStart || replacement != lineText) replacements.push_back(ColumnLineReplacement{MRTextBufferModel::Range(lineStart, lineContentEnd), replacement});
		} else if (firstPartial || (firstLine == lastLine && sourceGeometry.rangeStart < sourceGeometry.rangeEnd)) {
			const std::string_view lineText(text.data() + lineStart, lineContentEnd - lineStart);
			const int sourceCol1 = visualColumnForLineOffset(text, starts, line, sourceGeometry.rangeStart);
			const int sourceCol2 = firstLine == lastLine ? visualColumnForLineOffset(text, starts, line, sourceGeometry.rangeEnd) : visualWidthOfLine(lineText);
			int targetCol = indentToTab ? shiftedStreamTabColumn(settings, sourceCol1, shiftRight) : std::max(0, destCol);
			int targetGeometryCol = targetCol;
			const std::string payload = visualColumnsFromLine(lineText, sourceCol1, sourceCol2);

			if (!indentToTab && !(keepSpaces && !insertMode)) {
				const int width = std::max(0, sourceCol2 - sourceCol1);

				if (targetGeometryCol > sourceCol2) targetGeometryCol -= width;
				else if (targetGeometryCol > sourceCol1)
					targetGeometryCol = sourceCol1;
			}
			const std::string replacement = indentToTab ? indentStreamVisualColumnsWithinLine(lineText, sourceCol1, sourceCol2, targetCol, payload, insertMode, keepSpaces)
			                                            : moveStreamVisualColumnsWithinLine(lineText, sourceCol1, sourceCol2, targetCol, payload, insertMode, keepSpaces);

			if (line == firstLine) targetStartColumn = targetGeometryCol;
			if (line == lastLine) targetEndColumn = targetGeometryCol + std::max(0, sourceCol2 - sourceCol1);
			if (replacement.size() != lineContentEnd - lineStart || replacement != lineText) replacements.push_back(ColumnLineReplacement{MRTextBufferModel::Range(lineStart, lineContentEnd), replacement});
			if (firstLine == lastLine) break;
		} else if (lastPartial) {
			const std::string_view lineText(text.data() + lineStart, lineContentEnd - lineStart);
			const int sourceCol1 = 0;
			const int sourceCol2 = visualColumnForLineOffset(text, starts, line, sourceGeometry.rangeEnd);
			int targetCol = indentToTab ? shiftedStreamTabColumn(settings, sourceCol1, shiftRight) : std::max(0, destCol);
			int targetGeometryCol = targetCol;
			int targetWidth = std::max(0, sourceCol2 - sourceCol1);
			const std::string payload = visualColumnsFromLine(lineText, sourceCol1, sourceCol2);
			std::string replacement;

			if (!indentToTab && !(keepSpaces && !insertMode)) {
				if (targetGeometryCol > sourceCol2) targetGeometryCol -= targetWidth;
				else if (targetGeometryCol > sourceCol1)
					targetGeometryCol = sourceCol1;
			}
			if (indentToTab && !shiftRight) {
				const int removedWidth = streamLastPartialPrefixUndentWidth(lineText, sourceCol2, settings);

				replacement = undentStreamLastPartialPrefix(lineText, sourceCol2, removedWidth, insertMode, keepSpaces);
				if (!(keepSpaces && !insertMode)) targetWidth = std::max(0, targetWidth - removedWidth);
			} else
				replacement = indentToTab ? indentStreamVisualColumnsWithinLine(lineText, sourceCol1, sourceCol2, targetCol, payload, insertMode, keepSpaces)
				                          : moveStreamVisualColumnsWithinLine(lineText, sourceCol1, sourceCol2, targetCol, payload, insertMode, keepSpaces);

			targetEndColumn = targetGeometryCol + targetWidth;
			if (replacement.size() != lineContentEnd - lineStart || replacement != lineText) replacements.push_back(ColumnLineReplacement{MRTextBufferModel::Range(lineStart, lineContentEnd), replacement});
		}
	}
	if (!replacements.empty()) {
		stageColumnLineReplacements(transaction, replacements);
		if (!editor.applyStagedTransaction(transaction, sourceGeometry.rangeStart, sourceGeometry.rangeStart, sourceGeometry.rangeStart, true).applied()) {
			if (errorText != nullptr) *errorText = "Unable to shift stream block.";
			return false;
		}
		text = editor.snapshotText();
		starts = lineStartsForText(text);
	}
	if (starts.empty()) {
		if (errorText != nullptr) *errorText = "Stream block range is outside the editor buffer.";
		return false;
	}
	const std::size_t targetFirstLine = std::min(firstLine, starts.size() - 1);
	const std::size_t targetLastLine = std::min(lastLine, starts.size() - 1);
	const std::size_t targetRangeStart = targetStartAtLineStart ? starts[targetFirstLine] : offsetAtLineVisualColumn(text, starts, targetFirstLine, targetStartColumn);
	std::size_t targetRangeEnd = starts[targetLastLine];

	if (targetEndAtLineStart)
		targetRangeEnd = starts[targetLastLine];
	else if (targetEndAfterWholeLine)
		targetRangeEnd = targetLastLine + 1 < starts.size() ? starts[targetLastLine + 1] : text.size();
	else
		targetRangeEnd = offsetAtLineVisualColumn(text, starts, targetLastLine, targetEndColumn);
	setCommittedStreamGeometry(targetGeometry, text, starts, targetRangeStart, targetRangeEnd);
	mGeometry = targetGeometry;
	applySelection(editor);
	applyOverlay(editor);
	editor.setCursorOffset(mGeometry.rangeStart);
	return true;
}

bool MRFEBlockOps::shiftCurrentColumnBlockHorizontally(MRFileEditor &editor, int destCol, ColumnHorizontalShiftMode mode, std::string *errorText) {
	std::string text;
	std::vector<std::size_t> starts;
	std::vector<ColumnLineReplacement> columnReplacements;
	MRFEArenaAllocator payloadArena;
	std::vector<char> payloadStorage;
	MRFEBlockGeometry targetGeometry;
	std::size_t cursor = 0;
	MRTextBufferModel::StagedTransaction transaction(editor.readSnapshot(), destCol > mGeometry.col1 ? "indent-column-block" : "shift-column-block");

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
	if (mGeometry.mode != MRFEBlockMode::Column) {
		if (errorText != nullptr) *errorText = "Column block required.";
		return false;
	}
	const std::size_t rowCount = mGeometry.line2 >= mGeometry.line1 ? mGeometry.line2 - mGeometry.line1 + 1 : 0;
	const std::size_t width = mGeometry.col2 > mGeometry.col1 ? static_cast<std::size_t>(mGeometry.col2 - mGeometry.col1) : 0;
	const int targetCol = std::max(0, destCol);
	if (rowCount == 0 || width == 0) {
		if (errorText != nullptr) *errorText = "Column block geometry is invalid.";
		return false;
	}
	if (targetCol == mGeometry.col1) {
		applySelection(editor);
		applyOverlay(editor);
		editor.setCursorOffset(mGeometry.rangeStart);
		return true;
	}
	if (!captureTransferPayload(editor, payloadArena, errorText)) return false;
	payloadStorage = payloadArena.release();
	if (payloadStorage.size() < rowCount * width) {
		if (errorText != nullptr) *errorText = "Column block payload geometry is invalid.";
		return false;
	}
	text = editor.snapshotText();
	starts = lineStartsForText(text);
	if (starts.empty() || mGeometry.line1 >= starts.size()) {
		if (errorText != nullptr) *errorText = "Column block range is outside the editor buffer.";
		return false;
	}
	const std::size_t firstLine = mGeometry.line1;
	const std::size_t lastLine = std::min(mGeometry.line2, starts.size() - 1);
	for (std::size_t line = firstLine; line <= lastLine; ++line) {
		const std::size_t row = line - firstLine;
		const std::size_t lineStart = starts[line];
		const std::size_t lineEnd = lineContentEndForIndex(text, starts, line);
		const std::string_view rowPayload = payloadRowView(payloadStorage, row, width);
		const bool insertMode = mode == ColumnHorizontalShiftMode::UseEditorInsertMode && editor.insertModeEnabled();
		const bool keepSpaces = configuredEditSetupSettings().columnBlockMove == "LEAVE_SPACE";
		const std::string replacement = moveVisualColumnsWithinLine(std::string_view(text.data() + lineStart, lineEnd - lineStart), mGeometry.col1, mGeometry.col2, targetCol, rowPayload, insertMode, keepSpaces);

		if (replacement.size() != lineEnd - lineStart || replacement != std::string_view(text.data() + lineStart, lineEnd - lineStart))
			columnReplacements.push_back(ColumnLineReplacement{MRTextBufferModel::Range(lineStart, lineEnd), replacement});
	}
	if (!columnReplacements.empty()) {
		stageColumnLineReplacements(transaction, columnReplacements);
		cursor = mGeometry.rangeStart;
		if (!editor.applyStagedTransaction(transaction, cursor, cursor, cursor, true).applied()) {
			if (errorText != nullptr) *errorText = "Unable to shift column block.";
			return false;
		}
		text = editor.snapshotText();
		starts = lineStartsForText(text);
	} else
		text = editor.snapshotText();
	targetGeometry = mGeometry;
	targetGeometry.col1 = targetCol;
	targetGeometry.col2 = targetCol + static_cast<int>(width);
	targetGeometry.anchorColumn = targetGeometry.col1;
	targetGeometry.cursorColumn = targetGeometry.col2;
	targetGeometry.rangeStart = offsetAtLineVisualColumn(text, starts, targetGeometry.line1, targetGeometry.col1);
	targetGeometry.rangeEnd = offsetAtLineVisualColumn(text, starts, targetGeometry.line2, targetGeometry.col2);
	targetGeometry.anchor = targetGeometry.rangeStart;
	targetGeometry.cursor = targetGeometry.rangeEnd;
	mGeometry = targetGeometry;
	applySelection(editor);
	applyOverlay(editor);
	editor.setCursorOffset(mGeometry.rangeStart);
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
	switch (mGeometry.mode) {
	case MRFEBlockMode::Stream:
	case MRFEBlockMode::Line:
		if (mGeometry.rangeStart < mGeometry.rangeEnd) ranges.push_back(MRTextBufferModel::Range(mGeometry.rangeStart, mGeometry.rangeEnd));
		cursor = mGeometry.rangeStart;
		break;
	case MRFEBlockMode::Column:
		if (configuredEditSetupSettings().columnBlockMove == "LEAVE_SPACE") collectColumnClearReplacements(text, starts, mGeometry, columnReplacements);
		else
			collectColumnEraseReplacements(text, starts, mGeometry, columnReplacements);
		cursor = mGeometry.rangeStart;
		if (!columnReplacements.empty()) cursor = columnReplacements.front().range.start;
		break;
	case MRFEBlockMode::None:
		if (errorText != nullptr) *errorText = "No block mode selected.";
		return false;
	}
	if (ranges.empty() && columnReplacements.empty()) {
		clear(editor);
		editor.setCursorOffset(cursor);
		return true;
	}
	switch (mGeometry.mode) {
	case MRFEBlockMode::Stream:
		if (configuredEditSetupSettings().columnBlockMove == "LEAVE_SPACE") {
			const std::string replacement = streamKeepSpaceText(std::string_view(text.data() + mGeometry.rangeStart, mGeometry.rangeEnd - mGeometry.rangeStart));

			transaction.replace(MRTextBufferModel::Range(mGeometry.rangeStart, mGeometry.rangeEnd), replacement);
			break;
		}
		for (std::size_t index = ranges.size(); index > 0; --index)
			transaction.erase(ranges[index - 1]);
		break;
	case MRFEBlockMode::Line:
		for (std::size_t index = ranges.size(); index > 0; --index)
			transaction.erase(ranges[index - 1]);
		break;
	case MRFEBlockMode::Column:
		stageColumnLineReplacements(transaction, columnReplacements);
		break;
	case MRFEBlockMode::None:
		break;
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

bool MRFEBlockOps::loadBlockFromFile(MRFileEditor &editor, const std::string &path, std::string *errorText) {
	if (errorText != nullptr) errorText->clear();
	if (!mArena.loadFile(path, errorText)) return false;
	return insertPayloadAsStreamBlock(editor, mArena, errorText);
}

bool MRFEBlockOps::saveBlockToFile(MRFileEditor &editor, const std::string &path, std::string *errorText) {
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
	mGeometry.documentVersion = editor.documentVersion();
	mGeometry.anchor = anchor;
	mGeometry.cursor = cursor;
	mGeometry.anchorLine = editor.lineIndexOfOffset(anchor);
	mGeometry.cursorLine = editor.lineIndexOfOffset(cursor);
	mGeometry.anchorColumn = anchorColumn >= 0 ? anchorColumn : std::max(0, static_cast<int>(editor.columnOfOffset(anchor)));
	mGeometry.cursorColumn = cursorColumn >= 0 ? cursorColumn : std::max(0, static_cast<int>(editor.columnOfOffset(cursor)));
	normalize(editor);
	if (blockGeometryIsEmpty(mGeometry)) {
		clear(editor);
		return false;
	}
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
	switch (mGeometry.mode) {
	case MRFEBlockMode::Stream:
		if (!arena.assign(std::string_view(text.data() + mGeometry.rangeStart, mGeometry.rangeEnd - mGeometry.rangeStart))) return false;
		return true;
	case MRFEBlockMode::Line:
		if (!arena.assign(std::string_view(text.data() + mGeometry.rangeStart, mGeometry.rangeEnd - mGeometry.rangeStart))) return false;
		return true;
	case MRFEBlockMode::Column: {
		const std::vector<std::size_t> starts = lineStartsForText(text);

		for (std::size_t line = mGeometry.line1; line <= mGeometry.line2 && line < starts.size(); ++line) {
			if (!appendColumnVisualCells(text, starts, line, mGeometry.col1, mGeometry.col2, arena)) return false;
		}
		return true;
	}
	case MRFEBlockMode::None:
		if (errorText != nullptr) *errorText = "No block mode selected.";
		return false;
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
	switch (mGeometry.mode) {
	case MRFEBlockMode::Column:
		message.rowCount = mGeometry.line2 >= mGeometry.line1 ? mGeometry.line2 - mGeometry.line1 + 1 : 0;
		message.columnWidth = mGeometry.col2 > mGeometry.col1 ? static_cast<std::size_t>(mGeometry.col2 - mGeometry.col1) : 0;
		break;
	case MRFEBlockMode::Line:
		message.rowCount = mGeometry.line2 >= mGeometry.line1 ? mGeometry.line2 - mGeometry.line1 + 1 : 0;
		break;
	case MRFEBlockMode::Stream:
	case MRFEBlockMode::None:
		break;
	}
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
	switch (message.blockMode) {
	case MRFEBlockMode::Stream: {
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
	case MRFEBlockMode::Line: {
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
		working = editor.snapshotText();
		starts = lineStartsForText(working);
		setCommittedLineGeometry(mGeometry, working, starts, targetLine, targetLine + (message.rowCount == 0 ? 0 : message.rowCount - 1));
		mGeometry.documentVersion = editor.documentVersion();
		applySelection(editor);
		applyOverlay(editor);
		return true;
	}
	case MRFEBlockMode::Column: {
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
		mGeometry.documentVersion = editor.documentVersion();
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
	case MRFEBlockMode::None:
		if (errorText != nullptr) *errorText = "No block mode selected.";
		return false;
	}
	if (errorText != nullptr) *errorText = "No block mode selected.";
	return false;
}

bool MRFEBlockOps::hasVisibleBlock() const noexcept {
	return mGeometry.status != MRFEBlockStatus::Inactive && !mGeometry.hidden;
}

bool MRFEBlockOps::isMarking() const noexcept {
	return mGeometry.status == MRFEBlockStatus::Marking && !mGeometry.hidden;
}

void MRFEBlockOps::normalize(MRFileEditor &editor) {
	const std::size_t length = editor.bufferLength();
	const int anchorColumn = std::max(mGeometry.anchorColumn, 0);
	const int cursorColumn = std::max(mGeometry.cursorColumn, 0);

	mGeometry.anchor = std::min(mGeometry.anchor, length);
	mGeometry.cursor = std::min(mGeometry.cursor, length);
	mGeometry.anchorColumn = anchorColumn;
	mGeometry.cursorColumn = cursorColumn;
	switch (mGeometry.mode) {
	case MRFEBlockMode::Stream:
		mGeometry.rangeStart = std::min(mGeometry.anchor, mGeometry.cursor);
		mGeometry.rangeEnd = std::max(mGeometry.anchor, mGeometry.cursor);
		mGeometry.line1 = editor.lineIndexOfOffset(mGeometry.rangeStart);
		mGeometry.line2 = editor.lineIndexOfOffset(mGeometry.rangeEnd);
		mGeometry.col1 = std::min(anchorColumn, cursorColumn);
		mGeometry.col2 = std::max(anchorColumn, cursorColumn);
		break;
	case MRFEBlockMode::Line: {
		if (mGeometry.anchor == mGeometry.cursor && mGeometry.anchor == length && length > 0) {
			mGeometry.line1 = editor.lineIndexOfOffset(mGeometry.anchor);
			mGeometry.line2 = mGeometry.line1;
			mGeometry.rangeStart = mGeometry.anchor;
			mGeometry.rangeEnd = mGeometry.anchor;
			mGeometry.col1 = 0;
			mGeometry.col2 = 0;
			break;
		}
		const std::size_t anchor = lineBlockEndpointOffset(editor, mGeometry.anchor);
		const std::size_t cursor = lineBlockEndpointOffset(editor, mGeometry.cursor);
		const std::size_t anchorLine = editor.lineIndexOfOffset(anchor);
		const std::size_t cursorLine = editor.lineIndexOfOffset(cursor);
		mGeometry.line1 = std::min(anchorLine, cursorLine);
		mGeometry.line2 = std::max(anchorLine, cursorLine);
		mGeometry.rangeStart = editor.bufferModel().lineStartByIndex(mGeometry.line1);
		mGeometry.rangeEnd = editor.nextLineOffset(editor.bufferModel().lineStartByIndex(mGeometry.line2));
		mGeometry.col1 = 0;
		mGeometry.col2 = 0;
		break;
	}
	case MRFEBlockMode::Column: {
		const std::size_t realLineCount = std::max<std::size_t>(1, editor.bufferModel().lineCount());
		const std::size_t anchorLine = mGeometry.anchorLine;
		const std::size_t cursorLine = mGeometry.cursorLine;
		const std::size_t realLine1 = std::min(std::min(anchorLine, cursorLine), realLineCount - 1);
		const std::size_t realLine2 = std::min(std::max(anchorLine, cursorLine), realLineCount - 1);
		mGeometry.line1 = std::min(anchorLine, cursorLine);
		mGeometry.line2 = std::max(anchorLine, cursorLine);
		mGeometry.rangeStart = editor.bufferModel().lineStartByIndex(realLine1);
		mGeometry.rangeEnd = editor.nextLineOffset(editor.bufferModel().lineStartByIndex(realLine2));
		mGeometry.col1 = std::min(anchorColumn, cursorColumn);
		mGeometry.col2 = std::max(anchorColumn, cursorColumn);
		break;
	}
	case MRFEBlockMode::None:
		break;
	}
}

void MRFEBlockOps::applySelection(MRFileEditor &editor) {
	if (!hasVisibleBlock()) return;
	if (mGeometry.status == MRFEBlockStatus::Marking) editor.setSelectionOffsets(mGeometry.rangeStart, mGeometry.rangeEnd, False);
	else
		editor.setSelectionOffsets(editor.cursorOffset(), editor.cursorOffset(), False);
}

void MRFEBlockOps::applyOverlay(MRFileEditor &editor) {
	if (!hasVisibleBlock()) return;
	const bool trackCursor = mGeometry.status == MRFEBlockStatus::Marking;
	const std::size_t visualAnchor = trackCursor && mGeometry.mode == MRFEBlockMode::Stream ? mGeometry.anchor : mGeometry.rangeStart;
	std::size_t visualEnd = mGeometry.rangeEnd;
	switch (mGeometry.mode) {
	case MRFEBlockMode::Line:
		if (visualEnd > mGeometry.rangeStart) --visualEnd;
		break;
	case MRFEBlockMode::Column:
		if (visualEnd > mGeometry.rangeStart) {
			const MRTextBufferModel &model = editor.bufferModel();
			const bool endsAtLastKnownLine = mGeometry.line2 + 1 >= model.lineCount();
			if (visualEnd != model.length() || !endsAtLastKnownLine) --visualEnd;
		}
		break;
	case MRFEBlockMode::Stream:
	case MRFEBlockMode::None:
		break;
	}
	if (columnBlockTraceEnabled() && mGeometry.mode == MRFEBlockMode::Column) {
		const MRTextBufferModel &model = editor.bufferModel();
		std::ostringstream trace;
		trace << "apply-overlay mode=" << blockModeName(mGeometry.mode) << " line1=" << mGeometry.line1 << " line2=" << mGeometry.line2
		      << " col1=" << mGeometry.col1 << " col2=" << mGeometry.col2 << " rangeStart=" << mGeometry.rangeStart << " rangeEnd=" << mGeometry.rangeEnd
		      << " visualEnd=" << visualEnd << " lineCount=" << model.lineCount() << " length=" << model.length();
		appendColumnBlockTrace(trace.str());
	}
	editor.setBlockOverlayState(static_cast<int>(mGeometry.mode), visualAnchor, visualEnd, true, trackCursor, mGeometry.col1, mGeometry.col2, mGeometry.mode == MRFEBlockMode::Column, mGeometry.line1, mGeometry.line2);
}

void MRFEBlockOps::deactivateVisual(MRFileEditor &editor) {
	editor.setBlockOverlayState(0, 0, 0, false);
	editor.setSelectionOffsets(editor.cursorOffset(), editor.cursorOffset(), False);
}
