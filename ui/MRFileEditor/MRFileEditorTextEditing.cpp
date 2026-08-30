#include "MRFileEditor.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

constexpr unsigned char kLineNorth = 1;
constexpr unsigned char kLineEast = 2;
constexpr unsigned char kLineSouth = 4;
constexpr unsigned char kLineWest = 8;

struct LineDrawingGlyph {
	const char *text;
	unsigned char mask;
	unsigned char doubleMask;
};

struct LineDrawingCellEdit {
	std::size_t rangeStart;
	std::size_t rangeEnd;
	std::string replacement;
};

struct LineDrawingDirection {
	unsigned char mask;
	unsigned char oppositeMask;
	int lineDelta;
	int columnDelta;
};

constexpr LineDrawingGlyph kLineDrawingGlyphs[] = {
    {"─", static_cast<unsigned char>(kLineEast | kLineWest), 0},
    {"│", static_cast<unsigned char>(kLineNorth | kLineSouth), 0},
    {"┌", static_cast<unsigned char>(kLineEast | kLineSouth), 0},
    {"┐", static_cast<unsigned char>(kLineSouth | kLineWest), 0},
    {"└", static_cast<unsigned char>(kLineNorth | kLineEast), 0},
    {"┘", static_cast<unsigned char>(kLineNorth | kLineWest), 0},
    {"├", static_cast<unsigned char>(kLineNorth | kLineEast | kLineSouth), 0},
    {"┤", static_cast<unsigned char>(kLineNorth | kLineSouth | kLineWest), 0},
    {"┬", static_cast<unsigned char>(kLineEast | kLineSouth | kLineWest), 0},
    {"┴", static_cast<unsigned char>(kLineNorth | kLineEast | kLineWest), 0},
    {"┼", static_cast<unsigned char>(kLineNorth | kLineEast | kLineSouth | kLineWest), 0},
    {"═", static_cast<unsigned char>(kLineEast | kLineWest), static_cast<unsigned char>(kLineEast | kLineWest)},
    {"║", static_cast<unsigned char>(kLineNorth | kLineSouth), static_cast<unsigned char>(kLineNorth | kLineSouth)},
    {"╔", static_cast<unsigned char>(kLineEast | kLineSouth), static_cast<unsigned char>(kLineEast | kLineSouth)},
    {"╗", static_cast<unsigned char>(kLineSouth | kLineWest), static_cast<unsigned char>(kLineSouth | kLineWest)},
    {"╚", static_cast<unsigned char>(kLineNorth | kLineEast), static_cast<unsigned char>(kLineNorth | kLineEast)},
    {"╝", static_cast<unsigned char>(kLineNorth | kLineWest), static_cast<unsigned char>(kLineNorth | kLineWest)},
    {"╠", static_cast<unsigned char>(kLineNorth | kLineEast | kLineSouth), static_cast<unsigned char>(kLineNorth | kLineEast | kLineSouth)},
    {"╣", static_cast<unsigned char>(kLineNorth | kLineSouth | kLineWest), static_cast<unsigned char>(kLineNorth | kLineSouth | kLineWest)},
    {"╦", static_cast<unsigned char>(kLineEast | kLineSouth | kLineWest), static_cast<unsigned char>(kLineEast | kLineSouth | kLineWest)},
    {"╩", static_cast<unsigned char>(kLineNorth | kLineEast | kLineWest), static_cast<unsigned char>(kLineNorth | kLineEast | kLineWest)},
    {"╬", static_cast<unsigned char>(kLineNorth | kLineEast | kLineSouth | kLineWest), static_cast<unsigned char>(kLineNorth | kLineEast | kLineSouth | kLineWest)},
    {"╒", static_cast<unsigned char>(kLineEast | kLineSouth), kLineEast},
    {"╓", static_cast<unsigned char>(kLineEast | kLineSouth), kLineSouth},
    {"╕", static_cast<unsigned char>(kLineSouth | kLineWest), kLineWest},
    {"╖", static_cast<unsigned char>(kLineSouth | kLineWest), kLineSouth},
    {"╘", static_cast<unsigned char>(kLineNorth | kLineEast), kLineEast},
    {"╙", static_cast<unsigned char>(kLineNorth | kLineEast), kLineNorth},
    {"╛", static_cast<unsigned char>(kLineNorth | kLineWest), kLineWest},
    {"╜", static_cast<unsigned char>(kLineNorth | kLineWest), kLineNorth},
    {"╞", static_cast<unsigned char>(kLineNorth | kLineEast | kLineSouth), kLineEast},
    {"╟", static_cast<unsigned char>(kLineNorth | kLineEast | kLineSouth), static_cast<unsigned char>(kLineNorth | kLineSouth)},
    {"╡", static_cast<unsigned char>(kLineNorth | kLineSouth | kLineWest), kLineWest},
    {"╢", static_cast<unsigned char>(kLineNorth | kLineSouth | kLineWest), static_cast<unsigned char>(kLineNorth | kLineSouth)},
    {"╤", static_cast<unsigned char>(kLineEast | kLineSouth | kLineWest), static_cast<unsigned char>(kLineEast | kLineWest)},
    {"╥", static_cast<unsigned char>(kLineEast | kLineSouth | kLineWest), kLineSouth},
    {"╧", static_cast<unsigned char>(kLineNorth | kLineEast | kLineWest), static_cast<unsigned char>(kLineEast | kLineWest)},
    {"╨", static_cast<unsigned char>(kLineNorth | kLineEast | kLineWest), kLineNorth},
    {"╪", static_cast<unsigned char>(kLineNorth | kLineEast | kLineSouth | kLineWest), static_cast<unsigned char>(kLineEast | kLineWest)},
    {"╫", static_cast<unsigned char>(kLineNorth | kLineEast | kLineSouth | kLineWest), static_cast<unsigned char>(kLineNorth | kLineSouth)},
};

constexpr LineDrawingDirection kLineDrawingDirections[] = {
    {kLineNorth, kLineSouth, -1, 0},
    {kLineEast, kLineWest, 0, 1},
    {kLineSouth, kLineNorth, 1, 0},
    {kLineWest, kLineEast, 0, -1},
};

bool decodeLineDrawingGlyph(const std::string &text, unsigned char &mask, unsigned char &doubleMask) noexcept {
	for (const LineDrawingGlyph &glyph : kLineDrawingGlyphs) {
		if (text == glyph.text) {
			mask = glyph.mask;
			doubleMask = glyph.doubleMask;
			return true;
		}
	}
	mask = 0;
	doubleMask = 0;
	return false;
}

const char *sameStyleLineDrawingGlyph(unsigned char mask, bool doubleLine) noexcept {
	const unsigned char wantedDoubleMask = doubleLine ? mask : 0;

	for (const LineDrawingGlyph &glyph : kLineDrawingGlyphs)
		if (glyph.mask == mask && glyph.doubleMask == wantedDoubleMask) return glyph.text;
	if ((mask & (kLineEast | kLineWest)) != 0) return doubleLine ? "═" : "─";
	if ((mask & (kLineNorth | kLineSouth)) != 0) return doubleLine ? "║" : "│";
	return " ";
}

const char *mixedLineDrawingGlyph(unsigned char mask, unsigned char doubleMask, bool preferredDoubleLine) noexcept {
	for (const LineDrawingGlyph &glyph : kLineDrawingGlyphs)
		if (glyph.mask == mask && glyph.doubleMask == doubleMask) return glyph.text;
	return sameStyleLineDrawingGlyph(mask, preferredDoubleLine);
}

const char *lineDrawingGlyphFor(unsigned char mask, unsigned char doubleMask, bool preferredDoubleLine) noexcept {
	mask &= static_cast<unsigned char>(kLineNorth | kLineEast | kLineSouth | kLineWest);
	doubleMask &= mask;
	if (mask == 0) return " ";
	if (doubleMask == 0) return sameStyleLineDrawingGlyph(mask, false);
	if (doubleMask == mask) return sameStyleLineDrawingGlyph(mask, true);
	return mixedLineDrawingGlyph(mask, doubleMask, preferredDoubleLine);
}

unsigned char styledMask(unsigned char mask, bool doubleLine) noexcept {
	return doubleLine ? mask : 0;
}

bool lineDrawingTraceEnabled() noexcept {
	const char *value = std::getenv("MR_COLUMN_BLOCK_TRACE");
	return value != nullptr && *value != '\0';
}

void appendLineDrawingTrace(std::string_view message) {
	if (!lineDrawingTraceEnabled()) return;
	std::ofstream out("/tmp/mr-column-block-trace.log", std::ios::app);
	if (!out) return;
	out << "line-drawing " << message << '\n';
}

} // namespace

bool MRFileEditor::replaceBufferData(const char *data, uint length) {
	std::string text;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "replace-buffer-data");

	if (data != nullptr && length != 0) text.assign(data, length);
	transaction.setText(text);
	MRTextBufferModel::CommitResult result = applyStagedTransaction(transaction, 0, 0, 0, false);
	if (result.applied()) mBufferModel.clearUndoRedo();
	return result.applied();
}

bool MRFileEditor::replaceBufferText(const char *text) {
	uint length = text != nullptr ? static_cast<uint>(std::strlen(text)) : 0;
	return replaceBufferData(text, length);
}

bool MRFileEditor::appendBufferData(const char *data, uint length) {
	std::string text;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "append-buffer-data");
	std::size_t endPtr = mBufferModel.length();

	if (length == 0) return true;
	if (data != nullptr) text.assign(data, length);
	transaction.insert(endPtr, text);
	return applyStagedTransaction(transaction, endPtr + text.size(), endPtr + text.size(), endPtr + text.size(), false).applied();
}

bool MRFileEditor::appendBufferText(const char *text) {
	uint length = text != nullptr ? static_cast<uint>(std::strlen(text)) : 0;
	return appendBufferData(text, length);
}

bool MRFileEditor::appendLogViewerData(const char *data, uint length, const std::vector<std::pair<std::size_t, std::size_t>> *chunkFindRanges) {
	std::string text;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "append-log-viewer-data");
	const std::size_t oldCursor = mBufferModel.cursor();
	const std::size_t oldSelectionStart = mBufferModel.selectionStart();
	const std::size_t oldSelectionEnd = mBufferModel.selectionEnd();
	const std::size_t endPtr = mBufferModel.length();
	const int oldDeltaX = delta.x;
	const int oldDeltaY = delta.y;
	const int visibleRows = std::max(1, visibleTextRows());
	const std::size_t oldLineCount = std::max<std::size_t>(1, mBufferModel.lineCount());
	const std::size_t oldVisibleTop = static_cast<std::size_t>(std::max(0, oldDeltaY));
	const std::size_t oldVisibleBottom = oldVisibleTop + static_cast<std::size_t>(visibleRows - 1);
	const bool follow = oldVisibleBottom >= oldLineCount - 1;

	if (data == nullptr || length == 0) return true;
	text.assign(data, length);
	transaction.insert(endPtr, text);
	if (!applyStagedTransaction(transaction, follow ? endPtr + text.size() : oldCursor, follow ? endPtr + text.size() : oldSelectionStart, follow ? endPtr + text.size() : oldSelectionEnd, false).applied()) return false;
	if (chunkFindRanges != nullptr) {
		mFindMarkerRanges.clear();
		for (const auto &rangePair : *chunkFindRanges) {
			const std::size_t start = std::min(endPtr + rangePair.first, mBufferModel.length());
			const std::size_t end = std::min(endPtr + rangePair.second, mBufferModel.length());

			if (end > start) mFindMarkerRanges.push_back(MRTextBufferModel::Range(start, end));
		}
		normalizeRangeList(mFindMarkerRanges);
		mMiniMapState.setFindRanges(mFindMarkerRanges);
	}
	if (follow) {
		const int maxY = std::max(0, static_cast<int>(std::max<std::size_t>(1, mBufferModel.lineCount())) - visibleRows);
		scrollTo(oldDeltaX, maxY);
	} else
		scrollTo(oldDeltaX, oldDeltaY);
	drawView();
	return true;
}

bool MRFileEditor::prependLogViewerData(const char *data, uint length, const std::vector<std::pair<std::size_t, std::size_t>> *chunkFindRanges) {
	std::string text;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "prepend-log-viewer-data");
	const std::size_t oldCursor = mBufferModel.cursor();
	const std::size_t oldSelectionStart = mBufferModel.selectionStart();
	const std::size_t oldSelectionEnd = mBufferModel.selectionEnd();
	const int oldDeltaX = delta.x;
	const int oldDeltaY = delta.y;
	const bool follow = delta.y <= 0;
	int insertedLines = 0;

	if (data == nullptr || length == 0) return true;
	text.assign(data, length);
	for (char ch : text)
		if (ch == '\n') ++insertedLines;
	transaction.insert(0, text);
	if (!applyStagedTransaction(transaction, follow ? 0 : oldCursor + text.size(), follow ? 0 : oldSelectionStart + text.size(), follow ? 0 : oldSelectionEnd + text.size(), false).applied()) return false;
	if (chunkFindRanges != nullptr) {
		mFindMarkerRanges.clear();
		for (const auto &rangePair : *chunkFindRanges) {
			const std::size_t start = std::min(rangePair.first, mBufferModel.length());
			const std::size_t end = std::min(rangePair.second, mBufferModel.length());

			if (end > start) mFindMarkerRanges.push_back(MRTextBufferModel::Range(start, end));
		}
		normalizeRangeList(mFindMarkerRanges);
		mMiniMapState.setFindRanges(mFindMarkerRanges);
	}
	if (follow) scrollTo(oldDeltaX, 0);
	else
		scrollTo(oldDeltaX, std::max(0, oldDeltaY + insertedLines));
	drawView();
	return true;
}

void MRFileEditor::setBlockOverlayState(int mode, std::size_t anchor, std::size_t end, bool active, bool trackCursor, int columnAnchor, int columnEnd, bool lineRangeValid, std::size_t line1, std::size_t line2, bool redraw) {
	mBlockOverlayMode = mode;
	mBlockOverlayAnchor = std::min(anchor, mBufferModel.length());
	mBlockOverlayEnd = std::min(end, mBufferModel.length());
	mBlockOverlayTrackCursor = trackCursor;
	if (!mBlockOverlayTrackCursor && mBlockOverlayAnchor > mBlockOverlayEnd) std::swap(mBlockOverlayAnchor, mBlockOverlayEnd);
	mBlockOverlayActive = active && mode != 0;
	mBlockOverlayColumnAnchor = columnAnchor;
	mBlockOverlayColumnEnd = columnEnd;
	mBlockOverlayLineRangeValid = lineRangeValid;
	mBlockOverlayLine1 = std::min(line1, line2);
	mBlockOverlayLine2 = std::max(line1, line2);
	if (redraw) drawView();
}

MRFileEditor::BlockOverlayState MRFileEditor::blockOverlayState() const noexcept {
	BlockOverlayState state;
	state.active = mBlockOverlayActive;
	state.mode = mBlockOverlayMode;
	state.anchor = mBlockOverlayAnchor;
	state.end = mBlockOverlayEnd;
	state.trackCursor = mBlockOverlayTrackCursor;
	state.columnAnchor = mBlockOverlayColumnAnchor;
	state.columnEnd = mBlockOverlayColumnEnd;
	state.lineRangeValid = mBlockOverlayLineRangeValid;
	state.line1 = mBlockOverlayLine1;
	state.line2 = mBlockOverlayLine2;
	return state;
}

void MRFileEditor::setSelectionOffsets(std::size_t start, std::size_t end, Boolean) {
	const bool preserveFreeCursor = freeCursorMovementEnabled() && mBufferModel.cursor() == mBufferModel.length() && mCursorVisualLine > cachedCursorLineIndex();
	const std::size_t preservedLine = preserveFreeCursor ? mCursorVisualLine : 0;
	const int preservedColumn = preserveFreeCursor ? mCursorVisualColumn : 0;

	start = std::min(start, mBufferModel.length());
	end = std::min(end, mBufferModel.length());
	mSelectionAnchor = start;
	mBufferModel.setSelection(start, end);
	syncFromEditorState(false);
	if (preserveFreeCursor) {
		mCursorVisualLine = preservedLine;
		mCursorVisualColumn = preservedColumn;
		updateIndicator();
	}
}

bool MRFileEditor::lastMouseSelectionColumns(int &anchorColumn, int &cursorColumn) const noexcept {
	if (!mMouseSelectionColumnsValid) return false;
	anchorColumn = mMouseSelectionAnchorColumn;
	cursorColumn = mMouseSelectionCursorColumn;
	return true;
}

bool MRFileEditor::lastMouseSelectionLines(std::size_t &anchorLine, std::size_t &cursorLine) const noexcept {
	if (!mMouseSelectionLinesValid) return false;
	anchorLine = mMouseSelectionAnchorLine;
	cursorLine = mMouseSelectionCursorLine;
	return true;
}

unsigned short MRFileEditor::lastMouseSelectionModifiers() const noexcept {
	return mMouseSelectionModifiers;
}

bool MRFileEditor::replaceRangeAndSelect(std::size_t start, std::size_t end, const char *data, std::size_t length) {
	std::string text;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "replace-range-select");
	MRTextBufferModel::Range range;

	if (mReadOnly) return false;
	if (end < start) std::swap(start, end);
	range = MRTextBufferModel::Range(start, end).clamped(mBufferModel.length());
	if (data != nullptr && length != 0) text.assign(data, length);
	transaction.replace(range, text);
	return applyStagedTransaction(transaction, range.start, range.start, range.start + text.size(), true).applied();
}

bool MRFileEditor::replaceRangesAndCollapse(const std::vector<MRTextBufferModel::Range> &ranges, const char *data, std::size_t length) {
	if (mReadOnly || ranges.empty() || (data == nullptr && length != 0)) return false;

	const std::size_t expectedVersion = mBufferModel.version();
	const std::string_view replacement(data != nullptr ? data : "", length);
	pushUndoSnapshot();
	MRTextBufferModel::CommitResult result = mBufferModel.document().tryApplyReplacements(ranges, replacement, expectedVersion);
	if (!result.applied()) {
		mBufferModel.popUndoSnapshot();
		return false;
	}

	mBufferModel.updateUndoTopChangeSet(result.change);
	const std::size_t cursorEnd = ranges.front().start + length;
	return syncAfterCommittedDocument(cursorEnd, cursorEnd, cursorEnd, true, &result.change);
}

int MRFileEditor::paddingColumnsBeforeInsertAtCursor() const noexcept {
	const std::size_t cursor = mBufferModel.cursor();
	const std::size_t lineEnd = lineEndOffset(cursor);

	if (!freeCursorMovementEnabled() || mBufferModel.hasSelection() || cursor != lineEnd) return 0;
	return std::max(0, displayedCursorColumn() - actualCursorVisualColumn(cursor));
}

bool MRFileEditor::insertBufferText(const std::string &text) {
	std::string insertedText = text;
	std::size_t start = mBufferModel.cursor();
	std::size_t end = start;
	MRTextBufferModel::Range range;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "insert-buffer-text");
	const std::size_t selectionStart = mBufferModel.selectionStart();
	const std::size_t selectionEnd = mBufferModel.selectionEnd();
	const bool replaceSelection = mBufferModel.hasSelection() && mBufferModel.cursor() >= selectionStart && mBufferModel.cursor() < selectionEnd;

	if (mReadOnly) return false;
	if (!insertedText.empty()) {
		const int paddingColumns = paddingColumnsBeforeInsertAtCursor();
		if (paddingColumns > 0) insertedText.insert(0, static_cast<std::size_t>(paddingColumns), ' ');
	}
	if (replaceSelection) {
		range = mBufferModel.selection().range();
		start = range.start;
		end = range.end;
	} else if (!mInsertMode) {
		std::size_t endSel = mBufferModel.cursor();
		for (std::string::size_type i = 0; i < insertedText.size() && endSel < lineEndOffset(start); ++i)
			endSel = nextCharOffset(endSel);
		end = endSel;
	}
	range = MRTextBufferModel::Range(start, end).clamped(mBufferModel.length());
	transaction.replace(range, insertedText);
	const std::size_t cursorAfterInsert = range.start + insertedText.size();
	std::size_t selectionStartAfterInsert = cursorAfterInsert;
	std::size_t selectionEndAfterInsert = cursorAfterInsert;
	if (mBufferModel.hasSelection() && !replaceSelection) {
		const long long delta = static_cast<long long>(insertedText.size()) - static_cast<long long>(range.length());
		selectionStartAfterInsert = selectionStart;
		selectionEndAfterInsert = selectionEnd;
		if (selectionStart > range.start) {
			if (selectionStart >= range.end) selectionStartAfterInsert = static_cast<std::size_t>(static_cast<long long>(selectionStart) + delta);
			else
				selectionStartAfterInsert = range.start;
		}
		if (selectionEnd > range.start) {
			if (selectionEnd >= range.end) selectionEndAfterInsert = static_cast<std::size_t>(static_cast<long long>(selectionEnd) + delta);
			else
				selectionEndAfterInsert = range.start;
		}
	}
	return applyStagedTransaction(transaction, cursorAfterInsert, selectionStartAfterInsert, selectionEndAfterInsert, true).applied();
}

void MRFileEditor::restoreLineDrawingCursor(std::size_t visualLine, int visualColumn) {
	std::size_t target = mBufferModel.length();

	if (mBufferModel.lineCount() > 0 && visualLine < mBufferModel.lineCount()) target = charPtrOffset(mBufferModel.lineStartByIndex(visualLine), visualColumn);
	target = canonicalCursorOffset(std::min(target, mBufferModel.length()));
	mBufferModel.setCursorAndSelection(target, target, target);
	mSelectionAnchor = target;
	mCursorVisualLine = visualLine;
	mCursorVisualColumn = std::max(actualCursorVisualColumn(target), visualColumn);
}

bool MRFileEditor::materializeLineDrawingRows(std::size_t line1, std::size_t line2, int rightVisualColumn) {
	std::size_t lineStart = 0;
	std::size_t lineEnd = 0;
	std::string lineText;
	TStringView lineView;
	const MREditSetupSettings settings = effectiveEditSetupSettings();
	std::size_t local = 0;
	std::size_t prefixEnd = 0;
	int visualColumn = 0;
	bool expandedTab = false;
	bool paddedLine = false;
	std::string replacement;
	std::vector<LineDrawingCellEdit> edits;
	const bool preserveFreeCursor = freeCursorMovementEnabled() && !mBufferModel.hasSelection();
	const std::size_t preservedLine = preserveFreeCursor ? displayedCursorLineIndex() : 0;
	const int preservedColumn = preserveFreeCursor ? displayedCursorColumn() : 0;

	if (mReadOnly || rightVisualColumn < 0) return false;
	if (line2 < line1) std::swap(line1, line2);
	if (lineDrawingTraceEnabled()) {
		std::ostringstream trace;
		trace << "materialize begin line1=" << line1 << " line2=" << line2 << " right=" << rightVisualColumn << " lineCount=" << mBufferModel.lineCount() << " length=" << mBufferModel.length();
		appendLineDrawingTrace(trace.str());
	}
	if (line2 >= mBufferModel.lineCount()) {
		const std::size_t missingLines = line2 - mBufferModel.lineCount() + 1;
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "line-drawing-grid-lines");
		MRTextBufferModel::CommitResult result;

		transaction.insert(mBufferModel.length(), std::string(missingLines, '\n'));
		result = applyStagedTransaction(transaction, cursorOffset(), selectionStartOffset(), selectionEndOffset(), true);
		if (result.applied() && preserveFreeCursor) restoreLineDrawingCursor(preservedLine, preservedColumn);
		if (lineDrawingTraceEnabled()) {
			std::ostringstream trace;
			trace << "materialize lines missing=" << missingLines << " applied=" << result.applied() << " conflict=" << result.conflicted() << " lineCount=" << mBufferModel.lineCount()
			      << " length=" << mBufferModel.length();
			appendLineDrawingTrace(trace.str());
		}
		if (!result.applied()) return false;
	}
	lineStart = mBufferModel.lineStartByIndex(line1);
	for (std::size_t line = line1; line <= line2 && line < mBufferModel.lineCount(); ++line) {
		const std::size_t nextLineStart = mBufferModel.nextLine(lineStart);
		lineEnd = lineEndOffset(lineStart);
		lineText.clear();
		lineText.reserve(lineEnd - lineStart);
		for (std::size_t offset = lineStart; offset < lineEnd; ++offset)
			lineText.push_back(mBufferModel.charAt(offset));
		lineView = TStringView(lineText.data(), lineText.size());
		local = 0;
		prefixEnd = 0;
		visualColumn = 0;
		expandedTab = false;
		paddedLine = false;
		replacement.clear();
		while (local < lineView.size() && visualColumn <= rightVisualColumn) {
			if (lineView[local] == '\t') {
				const int width = tabDisplayWidth(settings, visualColumn);
				replacement.append(static_cast<std::size_t>(std::max(width, 1)), ' ');
				++local;
				visualColumn += std::max(width, 1);
				prefixEnd = local;
				expandedTab = true;
				continue;
			}
			std::size_t next = local;
			std::size_t width = 0;
			if (!nextDisplayChar(lineView, next, width, visualColumn, settings)) break;
			replacement.append(lineText.data() + local, next - local);
			visualColumn += static_cast<int>(width);
			local = next;
			prefixEnd = local;
		}
		if (visualColumn < rightVisualColumn) {
			replacement.append(static_cast<std::size_t>(rightVisualColumn - visualColumn), ' ');
			paddedLine = true;
		}
		if (!expandedTab && !paddedLine) {
			lineStart = nextLineStart;
			continue;
		}
		edits.push_back(LineDrawingCellEdit{lineStart, lineStart + prefixEnd, replacement});
		if (lineDrawingTraceEnabled()) {
			std::ostringstream trace;
			trace << "materialize queued line=" << line << " right=" << rightVisualColumn << " prefixEnd=" << prefixEnd << " visual=" << visualColumn
			      << " replacementSize=" << replacement.size() << " expandedTab=" << expandedTab << " paddedLine=" << paddedLine;
			appendLineDrawingTrace(trace.str());
		}
		lineStart = nextLineStart;
	}
	if (edits.empty()) return false;
	std::sort(edits.begin(), edits.end(), [](const LineDrawingCellEdit &lhs, const LineDrawingCellEdit &rhs) {
		return lhs.rangeStart > rhs.rangeStart;
	});
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "line-drawing-tab-grid");
	MRTextBufferModel::CommitResult result;
	for (const LineDrawingCellEdit &edit : edits)
		transaction.replace(MRTextBufferModel::Range(edit.rangeStart, edit.rangeEnd), edit.replacement);
	result = applyStagedTransaction(transaction, cursorOffset(), selectionStartOffset(), selectionEndOffset(), true);
	if (result.applied() && preserveFreeCursor) restoreLineDrawingCursor(preservedLine, preservedColumn);
	if (lineDrawingTraceEnabled()) {
		std::ostringstream trace;
		trace << "materialize rows edits=" << edits.size() << " right=" << rightVisualColumn << " applied=" << result.applied() << " conflict=" << result.conflicted();
		appendLineDrawingTrace(trace.str());
	}
	return result.applied();
}

bool MRFileEditor::lineDrawingCellMaskAt(std::size_t lineIndex, int visualColumn, unsigned char &mask, unsigned char &doubleMask) {
	std::size_t lineStart = 0;
	std::size_t lineEnd = 0;
	std::size_t rangeStart = 0;
	std::size_t existingEnd = 0;
	int actualColumn = 0;
	std::string existingText;

	mask = 0;
	doubleMask = 0;
	if (visualColumn < 0 || lineIndex >= mBufferModel.lineCount()) return false;
	lineStart = mBufferModel.lineStartByIndex(lineIndex);
	lineEnd = lineEndOffset(lineStart);
	rangeStart = charPtrOffset(lineStart, visualColumn);
	actualColumn = charColumn(lineStart, rangeStart);
	if (rangeStart >= lineEnd || actualColumn != visualColumn) return false;
	existingEnd = std::min(nextCharOffset(rangeStart), lineEnd);
	for (std::size_t offset = rangeStart; offset < existingEnd; ++offset)
		existingText.push_back(mBufferModel.charAt(offset));
	return decodeLineDrawingGlyph(existingText, mask, doubleMask);
}

unsigned char MRFileEditor::supportedLineDrawingMask(std::size_t lineIndex, int visualColumn, unsigned char existingMask, unsigned char connectionMask) {
	unsigned char supported = 0;
	unsigned char neighborMask = 0;
	unsigned char neighborDoubleMask = 0;
	const std::size_t lineCount = mBufferModel.lineCount();

	existingMask &= static_cast<unsigned char>(kLineNorth | kLineEast | kLineSouth | kLineWest);
	connectionMask &= static_cast<unsigned char>(kLineNorth | kLineEast | kLineSouth | kLineWest);
	for (const LineDrawingDirection &direction : kLineDrawingDirections) {
		std::size_t neighborLine = lineIndex;
		int neighborColumn = visualColumn + direction.columnDelta;
		bool neighborValid = true;

		if ((existingMask & direction.mask) == 0) continue;
		if ((connectionMask & direction.mask) != 0) {
			supported |= direction.mask;
			continue;
		}
		if (direction.lineDelta < 0) {
			if (lineIndex == 0) neighborValid = false;
			else
				neighborLine = lineIndex - 1;
		} else if (direction.lineDelta > 0) {
			if (lineIndex + 1 >= lineCount) neighborValid = false;
			else
				neighborLine = lineIndex + 1;
		}
		if (neighborColumn < 0) neighborValid = false;
		if (neighborValid && lineDrawingCellMaskAt(neighborLine, neighborColumn, neighborMask, neighborDoubleMask) && (neighborMask & direction.oppositeMask) != 0) supported |= direction.mask;
	}
	return supported;
}

bool MRFileEditor::drawLineDrawingSegment(std::size_t fromLine, int fromColumn, std::size_t toLine, int toColumn) {
	bool changed = false;
	std::vector<LineDrawingCellEdit> edits;
	const bool preserveFreeCursor = freeCursorMovementEnabled() && !mBufferModel.hasSelection();
	const std::size_t preservedLine = preserveFreeCursor ? displayedCursorLineIndex() : 0;
	const int preservedColumn = preserveFreeCursor ? displayedCursorColumn() : 0;
	std::size_t finalCursor = cursorOffset();
	auto queueCell = [this, &edits, &finalCursor](std::size_t lineIndex, int visualColumn, unsigned char connectionMask) {
		std::size_t lineStart = 0;
		std::size_t lineEnd = 0;
		std::size_t rangeStart = 0;
		std::size_t rangeEnd = 0;
		std::size_t existingEnd = 0;
		int actualColumn = 0;
		unsigned char existingMask = 0;
		unsigned char existingDoubleMask = 0;
		unsigned char supportedMask = 0;
		unsigned char supportedDoubleMask = 0;
		unsigned char mergedMask = 0;
		unsigned char mergedDoubleMask = 0;
		std::string existingText;
		std::string replacement;

		if (visualColumn < 0 || lineIndex >= mBufferModel.lineCount()) return false;
		connectionMask &= static_cast<unsigned char>(kLineNorth | kLineEast | kLineSouth | kLineWest);
		if (connectionMask == 0) return false;
		lineStart = mBufferModel.lineStartByIndex(lineIndex);
		lineEnd = lineEndOffset(lineStart);
		rangeStart = charPtrOffset(lineStart, visualColumn);
		actualColumn = charColumn(lineStart, rangeStart);
		rangeEnd = rangeStart;
		if (rangeStart < lineEnd && actualColumn == visualColumn) {
			existingEnd = std::min(nextCharOffset(rangeStart), lineEnd);
			for (std::size_t offset = rangeStart; offset < existingEnd; ++offset)
				existingText.push_back(mBufferModel.charAt(offset));
			static_cast<void>(decodeLineDrawingGlyph(existingText, existingMask, existingDoubleMask));
			rangeEnd = existingEnd;
		}
		supportedMask = supportedLineDrawingMask(lineIndex, visualColumn, existingMask, connectionMask);
		supportedDoubleMask = static_cast<unsigned char>(existingDoubleMask & supportedMask);
		mergedMask = static_cast<unsigned char>(supportedMask | connectionMask);
		mergedDoubleMask = static_cast<unsigned char>((supportedDoubleMask | styledMask(connectionMask, mLineDrawingDoubleLines)) & mergedMask);
		if (actualColumn < visualColumn && rangeStart == lineEnd) replacement.assign(static_cast<std::size_t>(visualColumn - actualColumn), ' ');
		replacement += lineDrawingGlyphFor(mergedMask, mergedDoubleMask, mLineDrawingDoubleLines);
		if (rangeStart > static_cast<std::size_t>(UINT_MAX) || rangeEnd > static_cast<std::size_t>(UINT_MAX)) return false;
		if (rangeEnd - rangeStart == replacement.size() && existingText == replacement) return false;
		edits.push_back(LineDrawingCellEdit{rangeStart, rangeEnd, replacement});
		finalCursor = rangeStart + replacement.size();
		if (lineDrawingTraceEnabled()) {
			std::ostringstream trace;
			trace << "cell queued line=" << lineIndex << " col=" << visualColumn << " connection=" << static_cast<int>(connectionMask) << " existing=" << static_cast<int>(existingMask)
			      << " supported=" << static_cast<int>(supportedMask) << " merged=" << static_cast<int>(mergedMask) << " range=(" << rangeStart << ',' << rangeEnd << ") replacement='"
			      << replacement << "'";
			appendLineDrawingTrace(trace.str());
		}
		return true;
	};

	if (fromColumn < 0 || toColumn < 0) return false;
	static_cast<void>(materializeLineDrawingRows(std::min(fromLine, toLine), std::max(fromLine, toLine), std::max(fromColumn, toColumn)));
	if (fromLine >= mBufferModel.lineCount() || toLine >= mBufferModel.lineCount()) return false;
	if (lineDrawingTraceEnabled()) {
		std::ostringstream trace;
		trace << "segment from=(" << fromLine << ',' << fromColumn << ") to=(" << toLine << ',' << toColumn << ") lineCount=" << mBufferModel.lineCount() << " length=" << mBufferModel.length();
		appendLineDrawingTrace(trace.str());
	}
	if (fromLine == toLine) {
		const int left = std::min(fromColumn, toColumn);
		const int right = std::max(fromColumn, toColumn);
		for (int column = left; column <= right; ++column) {
			unsigned char mask = static_cast<unsigned char>(kLineEast | kLineWest);
			if (column == fromColumn) mask = fromColumn < toColumn ? kLineEast : kLineWest;
			else if (column == toColumn)
				mask = fromColumn < toColumn ? kLineWest : kLineEast;
			changed = queueCell(fromLine, column, mask) || changed;
		}
	} else if (fromLine < toLine) {
		for (std::size_t line = fromLine; line <= toLine; ++line) {
			unsigned char mask = static_cast<unsigned char>(kLineNorth | kLineSouth);
			if (line == fromLine) mask = kLineSouth;
			else if (line == toLine)
				mask = kLineNorth;
			changed = queueCell(line, fromColumn, mask) || changed;
		}
	} else {
		for (std::size_t line = fromLine + 1; line-- > toLine;) {
			unsigned char mask = static_cast<unsigned char>(kLineNorth | kLineSouth);
			if (line == fromLine) mask = kLineNorth;
			else if (line == toLine)
				mask = kLineSouth;
			changed = queueCell(line, fromColumn, mask) || changed;
			if (line == 0) break;
		}
	}
	if (!edits.empty()) {
		std::sort(edits.begin(), edits.end(), [](const LineDrawingCellEdit &lhs, const LineDrawingCellEdit &rhs) {
			if (lhs.rangeStart != rhs.rangeStart) return lhs.rangeStart > rhs.rangeStart;
			return lhs.rangeEnd > rhs.rangeEnd;
		});
		MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "line-drawing-segment");
		for (const LineDrawingCellEdit &edit : edits)
			transaction.replace(MRTextBufferModel::Range(edit.rangeStart, edit.rangeEnd), edit.replacement);
		MRTextBufferModel::CommitResult result = applyStagedTransaction(transaction, finalCursor, finalCursor, finalCursor, true);
		changed = result.applied() || changed;
		if (result.applied() && preserveFreeCursor) restoreLineDrawingCursor(preservedLine, preservedColumn);
		if (lineDrawingTraceEnabled()) {
			std::ostringstream trace;
			trace << "segment applied edits=" << edits.size() << " applied=" << result.applied() << " conflict=" << result.conflicted() << " lineCount=" << mBufferModel.lineCount()
			      << " length=" << mBufferModel.length();
			appendLineDrawingTrace(trace.str());
		}
	}
	if (fromLine != toLine && fromColumn != toColumn) changed = drawLineDrawingSegment(toLine, fromColumn, toLine, toColumn) || changed;
	return changed;
}

bool MRFileEditor::drawLineDrawingMouseSegment(std::size_t fromLine, int fromColumn, std::size_t toLine, int toColumn, int &lastAxis) {
	static constexpr int horizontalAxis = 1;
	static constexpr int verticalAxis = 2;
	const int columnDistance = std::abs(toColumn - fromColumn);
	const std::size_t lineDistance = fromLine < toLine ? toLine - fromLine : fromLine - toLine;
	bool changed = false;
	int firstAxis = lastAxis;

	if (fromLine == toLine && fromColumn == toColumn) return false;
	if (fromLine == toLine) {
		changed = drawLineDrawingSegment(fromLine, fromColumn, toLine, toColumn);
		lastAxis = horizontalAxis;
		return changed;
	}
	if (fromColumn == toColumn) {
		changed = drawLineDrawingSegment(fromLine, fromColumn, toLine, toColumn);
		lastAxis = verticalAxis;
		return changed;
	}
	if (firstAxis != horizontalAxis && firstAxis != verticalAxis) firstAxis = static_cast<std::size_t>(columnDistance) >= lineDistance ? horizontalAxis : verticalAxis;
	if (firstAxis == horizontalAxis) {
		changed = drawLineDrawingSegment(fromLine, fromColumn, fromLine, toColumn) || changed;
		changed = drawLineDrawingSegment(fromLine, toColumn, toLine, toColumn) || changed;
		lastAxis = verticalAxis;
	} else {
		changed = drawLineDrawingSegment(fromLine, fromColumn, toLine, fromColumn) || changed;
		changed = drawLineDrawingSegment(toLine, fromColumn, toLine, toColumn) || changed;
		lastAxis = horizontalAxis;
	}
	return changed;
}

bool MRFileEditor::drawLineDrawingCursorMotion(ushort key) {
	const std::size_t fromLine = displayedCursorLineIndex();
	const int fromColumn = displayedCursorColumn();
	std::size_t targetOffset = cursorOffset();
	std::size_t toLine = fromLine;
	int toColumn = fromColumn;

	switch (key) {
	case kbLeft:
		toColumn = std::max(0, fromColumn - 1);
		break;
	case kbRight:
		toColumn = fromColumn + 1;
		break;
	case kbUp:
		targetOffset = lineMoveOffset(cursorOffset(), -1, fromColumn);
		toLine = targetOffset >= mBufferModel.length() && freeCursorMovementEnabled() ? mCursorVisualLine : lineIndexOfOffset(targetOffset);
		break;
	case kbDown:
		targetOffset = lineMoveOffset(cursorOffset(), 1, fromColumn);
		toLine = targetOffset >= mBufferModel.length() && freeCursorMovementEnabled() ? mCursorVisualLine : lineIndexOfOffset(targetOffset);
		break;
	default:
		return false;
	}
	if (toLine == fromLine && toColumn == fromColumn) return false;
	static_cast<void>(drawLineDrawingSegment(fromLine, fromColumn, toLine, toColumn));
	targetOffset = charPtrOffset(mBufferModel.lineStartByIndex(toLine), toColumn);
	moveCursor(targetOffset, false, false, toColumn);
	return true;
}

bool MRFileEditor::drawLineDrawingBoxForColumnBlock(std::size_t line1, std::size_t line2, int col1, int col2) {
	bool changed = false;

	if (mReadOnly || !mLineDrawingEnabled) return false;
	if (lineDrawingTraceEnabled()) {
		std::ostringstream trace;
		trace << "box input line1=" << line1 << " line2=" << line2 << " col1=" << col1 << " col2=" << col2 << " lineCount=" << mBufferModel.lineCount() << " length=" << mBufferModel.length();
		appendLineDrawingTrace(trace.str());
	}
	if (line2 < line1) std::swap(line1, line2);
	if (col2 < col1) std::swap(col1, col2);
	if (col1 < 0) col1 = 0;
	if (col2 < 0) col2 = 0;
	static_cast<void>(materializeLineDrawingRows(line2, line2, col2));
	if (line1 == line2) return drawLineDrawingSegment(line1, col1, line1, col2);
	changed = drawLineDrawingSegment(line1, col1, line1, col2) || changed;
	changed = drawLineDrawingSegment(line2, col1, line2, col2) || changed;
	changed = drawLineDrawingSegment(line1, col1, line2, col1) || changed;
	changed = drawLineDrawingSegment(line1, col2, line2, col2) || changed;
	if (lineDrawingTraceEnabled()) {
		std::ostringstream trace;
		trace << "box output line1=" << line1 << " line2=" << line2 << " col1=" << col1 << " col2=" << col2 << " changed=" << changed << " lineCount=" << mBufferModel.lineCount()
		      << " length=" << mBufferModel.length();
		appendLineDrawingTrace(trace.str());
	}
	return changed;
}
