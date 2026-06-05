#include "MRFileEditor.hpp"

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
	const int oldMaxY = std::max(0, static_cast<int>(std::max<std::size_t>(1, mBufferModel.lineCount())) - visibleRows);
	const bool follow = oldDeltaY >= oldMaxY;

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
	}
	if (follow) scrollTo(oldDeltaX, 0);
	else
		scrollTo(oldDeltaX, std::max(0, oldDeltaY + insertedLines));
	drawView();
	return true;
}

void MRFileEditor::setBlockOverlayState(int mode, std::size_t anchor, std::size_t end, bool active, bool trackCursor, int columnAnchor, int columnEnd) {
	mBlockOverlayMode = mode;
	mBlockOverlayAnchor = std::min(anchor, mBufferModel.length());
	mBlockOverlayEnd = std::min(end, mBufferModel.length());
	if (mBlockOverlayAnchor > mBlockOverlayEnd) std::swap(mBlockOverlayAnchor, mBlockOverlayEnd);
	mBlockOverlayActive = active && mode != 0;
	mBlockOverlayTrackCursor = trackCursor;
	mBlockOverlayColumnAnchor = columnAnchor;
	mBlockOverlayColumnEnd = columnEnd;
	drawView();
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
	return state;
}

void MRFileEditor::setSelectionOffsets(std::size_t start, std::size_t end, Boolean) {
	start = std::min(start, mBufferModel.length());
	end = std::min(end, mBufferModel.length());
	mSelectionAnchor = start;
	mBufferModel.setSelection(start, end);
	syncFromEditorState(false);
}

bool MRFileEditor::lastMouseSelectionColumns(int &anchorColumn, int &cursorColumn) const noexcept {
	if (!mMouseSelectionColumnsValid) return false;
	anchorColumn = mMouseSelectionAnchorColumn;
	cursorColumn = mMouseSelectionCursorColumn;
	return true;
}

unsigned short MRFileEditor::lastMouseSelectionModifiers() const noexcept {
	return mMouseSelectionModifiers;
}

bool MRFileEditor::replaceRangeAndSelect(uint start, uint end, const char *data, uint length) {
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

	if (mReadOnly) return false;
	if (!insertedText.empty()) {
		const int paddingColumns = paddingColumnsBeforeInsertAtCursor();
		if (paddingColumns > 0) insertedText.insert(0, static_cast<std::size_t>(paddingColumns), ' ');
	}
	if (mBufferModel.hasSelection()) {
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
	start = range.start + insertedText.size();
	return applyStagedTransaction(transaction, start, start, start, true).applied();
}
