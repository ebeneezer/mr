#include "MRFileEditor.hpp"

bool MRFileEditor::freeCursorMovementEnabled() const noexcept {
	return mCursorBehaviour == MRCursorBehaviour::FreeMovement;
}

bool MRFileEditor::freeCursorVirtualMovementAllowed() const noexcept {
	return freeCursorMovementEnabled() && (!mBufferModel.hasSelection() || mBlockOverlayActive || configuredPersistentBlocksSetting());
}

int MRFileEditor::actualCursorVisualColumn(std::size_t offset) const noexcept {
	return charColumn(mBufferModel.lineStart(offset), offset);
}

int MRFileEditor::displayedCursorColumn() const noexcept {
	const int actualColumn = actualCursorVisualColumn(mBufferModel.cursor());
	if (!freeCursorMovementEnabled()) return actualColumn;
	if (mBufferModel.cursor() == mBufferModel.length() && mCursorVisualLine > cachedCursorLineIndex()) return mCursorVisualColumn;
	return std::max(actualColumn, mCursorVisualColumn);
}

std::size_t MRFileEditor::displayedCursorLineIndex() const noexcept {
	const std::size_t actualLine = cachedCursorLineIndex();
	if (!freeCursorMovementEnabled()) return actualLine;
	if (mBufferModel.cursor() != mBufferModel.length() || mBufferModel.hasSelection()) return actualLine;
	return std::max(actualLine, mCursorVisualLine);
}

std::size_t MRFileEditor::blockCursorLineIndex() const noexcept {
	const std::size_t actualLine = cachedCursorLineIndex();
	if (!freeCursorMovementEnabled()) return actualLine;
	return std::max(actualLine, mCursorVisualLine);
}

std::size_t MRFileEditor::cachedCursorLineIndex() const noexcept {
	const std::size_t documentId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	const std::size_t cursor = mBufferModel.cursor();

	if (mCachedCursorLineDocumentId == documentId && mCachedCursorLineVersion == version && mCachedCursorLineOffset == cursor) return mCachedCursorLineIndexValue;

	mCachedCursorLineDocumentId = documentId;
	mCachedCursorLineVersion = version;
	mCachedCursorLineOffset = cursor;
	mCachedCursorLineIndexValue = mBufferModel.lineIndex(cursor);
	return mCachedCursorLineIndexValue;
}

void MRFileEditor::syncDisplayedCursorColumnFromCursor(bool preserveFreeColumn) noexcept {
	const int actualColumn = actualCursorVisualColumn(mBufferModel.cursor());

	if (!freeCursorMovementEnabled() || !preserveFreeColumn) {
		mCursorVisualLine = cachedCursorLineIndex();
		mCursorVisualColumn = actualColumn;
		return;
	}
	if (mCursorVisualLine < cachedCursorLineIndex()) mCursorVisualLine = cachedCursorLineIndex();
	if (mCursorVisualColumn < actualColumn) mCursorVisualColumn = actualColumn;
}

std::size_t MRFileEditor::cursorOffset() const noexcept {
	return mBufferModel.cursor();
}

std::size_t MRFileEditor::bufferLength() const noexcept {
	return mBufferModel.length();
}

std::size_t MRFileEditor::selectionStartOffset() const noexcept {
	return mBufferModel.selectionStart();
}

std::size_t MRFileEditor::selectionEndOffset() const noexcept {
	return mBufferModel.selectionEnd();
}

std::size_t MRFileEditor::selectionAnchorOffset() const noexcept {
	return mSelectionAnchor;
}

std::size_t MRFileEditor::selectionCursorOffset() const noexcept {
	return mBufferModel.cursor();
}

bool MRFileEditor::hasTextSelection() const noexcept {
	return mBufferModel.hasSelection();
}

std::size_t MRFileEditor::lineStartOffset(std::size_t pos) const noexcept {
	return mBufferModel.lineStart(pos);
}

std::size_t MRFileEditor::lineEndOffset(std::size_t pos) const noexcept {
	return mBufferModel.lineEnd(pos);
}

std::size_t MRFileEditor::nextLineOffset(std::size_t pos) const noexcept {
	return mBufferModel.nextLine(pos);
}

std::size_t MRFileEditor::prevLineOffset(std::size_t pos) const noexcept {
	return mBufferModel.prevLine(pos);
}

std::size_t MRFileEditor::lineIndexOfOffset(std::size_t pos) const noexcept {
	return mBufferModel.lineIndex(pos);
}

std::size_t MRFileEditor::columnOfOffset(std::size_t pos) const noexcept {
	return mBufferModel.column(pos);
}

char MRFileEditor::charAtOffset(std::size_t pos) const noexcept {
	return mBufferModel.charAt(pos);
}

std::string MRFileEditor::lineTextAtOffset(std::size_t pos) const {
	return mBufferModel.lineText(pos);
}

int MRFileEditor::charColumn(std::size_t start, std::size_t pos) const noexcept {
	std::size_t lineStart = mBufferModel.lineStart(start);
	std::string lineText = mBufferModel.lineText(lineStart);
	TStringView line(lineText.data(), lineText.size());
	const MREditSetupSettings settings = effectiveEditSetupSettings();
	std::size_t p = 0;
	std::size_t end = std::min(pos, mBufferModel.length()) - lineStart;
	int visual = 0;

	end = std::min(end, line.size());
	while (p < end) {
		std::size_t next = p;
		std::size_t width = 0;
		if (!nextDisplayChar(line, next, width, visual, settings)) break;
		if (next > end) break;
		visual += static_cast<int>(width);
		p = next;
	}
	return visual;
}

void MRFileEditor::setCursorOffset(std::size_t pos, int) {
	moveCursor(std::min(pos, mBufferModel.length()), false, false);
}

void MRFileEditor::setCursorOffsetAtVisualColumn(std::size_t pos, int visualColumn) {
	moveCursor(std::min(pos, mBufferModel.length()), false, false, visualColumn);
}

bool MRFileEditor::scrollWindowByLines(int deltaRows) {
	int maxY = vScrollBar != nullptr ? vScrollBar->maxVal : std::max(0, limit.y - size.y);
	int targetY = std::min(std::max(delta.y + deltaRows, 0), std::max(0, maxY));

	if (deltaRows == 0) return true;
	if (targetY == delta.y) return false;
	scrollTo(std::max(delta.x, 0), targetY);
	scheduleSyntaxWarmupIfNeeded();
	updateIndicator();
	drawView();
	return true;
}

bool MRFileEditor::scrollWindowByWheel(int wheel) {
	const int oldDeltaY = delta.y;
	const int cursorRow = std::max(0, std::min(static_cast<int>(visibleLineForDocumentLine(cachedCursorLineIndex())) - oldDeltaY, std::max(1, visibleTextRows()) - 1));
	const int cursorColumn = displayedCursorColumn();
	int targetX = delta.x;
	int targetY = delta.y;

	switch (wheel) {
		case mwUp:
			targetY -= 3;
			break;
		case mwDown:
			targetY += 3;
			break;
		case mwLeft:
			targetX -= 3;
			break;
		case mwRight:
			targetX += 3;
			break;
		default:
			return false;
	}

	if (hScrollBar != nullptr) targetX = std::min(std::max(targetX, hScrollBar->minVal), hScrollBar->maxVal);
	else
		targetX = std::max(targetX, 0);
	if (vScrollBar != nullptr) targetY = std::min(std::max(targetY, vScrollBar->minVal), vScrollBar->maxVal);
	else
		targetY = std::max(targetY, 0);
	if (targetX == delta.x && targetY == delta.y) return false;
	scrollTo(targetX, targetY);
	if (targetY != oldDeltaY) {
		const std::size_t targetVisibleLine = static_cast<std::size_t>(std::max(0, targetY + cursorRow));
		const std::size_t targetDocumentLine = documentLineForVisibleLine(targetVisibleLine);
		const std::size_t targetOffset = charPtrOffset(mBufferModel.lineStartByIndex(targetDocumentLine), cursorColumn);
		moveCursor(targetOffset, false, false, cursorColumn);
	} else {
		scheduleSyntaxWarmupIfNeeded();
		updateIndicator();
		drawView();
	}
	return true;
}

std::size_t MRFileEditor::offsetForGlobalPoint(TPoint where) noexcept {
	return mouseOffset(makeLocal(where));
}

bool MRFileEditor::textPointInView(TPoint where) noexcept {
	const TPoint local = makeLocal(where);
	const TextViewportGeometry viewport = textViewportGeometry();

	return viewport.containsTextPoint(local.x, local.y, visibleTextRows());
}

int MRFileEditor::currentLineNumber() const noexcept {
	return static_cast<int>(displayedCursorLineIndex()) + 1;
}

int MRFileEditor::currentColumnNumber() const noexcept {
	return displayedCursorColumn() + 1;
}

int MRFileEditor::currentViewRow() const noexcept {
	return std::max(1, static_cast<int>(visibleLineForDocumentLine(displayedCursorLineIndex())) - delta.y + 1);
}

int MRFileEditor::currentViewColumn() const noexcept {
	return std::max(1, displayedCursorColumn() - delta.x + 1);
}

int MRFileEditor::visibleViewportRows() const noexcept {
	return std::max(1, visibleTextRows());
}

TRect MRFileEditor::visibleTextViewportBounds() const noexcept {
	const TextViewportGeometry viewport = textViewportGeometry();
	return TRect(viewport.textLeft, viewport.topInset, viewport.textRight, viewport.topInset + std::max(1, visibleTextRows()));
}

bool MRFileEditor::isWordByte(char ch) noexcept {
	unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalnum(uch) != 0 || ch == '_';
}

std::size_t MRFileEditor::nextCharOffset(std::size_t pos) noexcept {
	std::size_t len = mBufferModel.length();
	char bytes[4];
	std::size_t count = 0;

	if (pos >= len) return len;
	if (mBufferModel.charAt(pos) == '\r' && pos + 1 < len && mBufferModel.charAt(pos + 1) == '\n') return std::min(len, pos + 2);
	for (; count < sizeof(bytes) && pos + count < len; ++count)
		bytes[count] = mBufferModel.charAt(pos + count);
	std::size_t step = TText::next(TStringView(bytes, count));
	return std::min(len, pos + std::max<std::size_t>(step, 1));
}

std::size_t MRFileEditor::prevCharOffset(std::size_t pos) noexcept {
	char bytes[4];
	std::size_t start = 0;
	std::size_t count = 0;

	if (pos == 0) return 0;
	if (pos > 1 && mBufferModel.charAt(pos - 2) == '\r' && mBufferModel.charAt(pos - 1) == '\n') return pos - 2;
	start = pos > sizeof(bytes) ? pos - sizeof(bytes) : 0;
	count = pos - start;
	for (std::size_t i = 0; i < count; ++i)
		bytes[i] = mBufferModel.charAt(start + i);
	std::size_t step = TText::prev(TStringView(bytes, count), count);
	return pos - std::max<std::size_t>(step, 1);
}

std::size_t MRFileEditor::lineMoveOffset(std::size_t pos, int deltaLines, int targetVisualColumn) noexcept {
	const std::size_t clampedPos = std::min(pos, mBufferModel.length());
	const bool virtualFreeCursor = freeCursorMovementEnabled() && clampedPos == mBufferModel.cursor() && (!mBufferModel.hasSelection() || mCursorVisualLine > cachedCursorLineIndex());
	const std::size_t currentDocumentLine = virtualFreeCursor ? std::max(cachedCursorLineIndex(), mCursorVisualLine) : mBufferModel.lineIndex(clampedPos);
	const std::size_t currentVisibleLine = visibleLineForDocumentLine(currentDocumentLine);
	std::size_t targetVisibleLine = currentVisibleLine;
	std::size_t targetDocumentLine = currentDocumentLine;

	if (targetVisualColumn < 0) targetVisualColumn = charColumn(mBufferModel.lineStart(pos), clampedPos);
	if (deltaLines < 0) targetVisibleLine = currentVisibleLine > static_cast<std::size_t>(-deltaLines) ? currentVisibleLine - static_cast<std::size_t>(-deltaLines) : 0;
	else
		targetVisibleLine = currentVisibleLine + static_cast<std::size_t>(deltaLines);
	targetDocumentLine = documentLineForVisibleLine(targetVisibleLine);
	if (virtualFreeCursor) mCursorVisualLine = std::max(cachedCursorLineIndex(), targetDocumentLine);
	if (targetDocumentLine >= mBufferModel.lineCount()) return mBufferModel.length();
	return charPtrOffset(mBufferModel.lineStartByIndex(targetDocumentLine), targetVisualColumn);
}

std::size_t MRFileEditor::tabStopMoveOffset(std::size_t pos, bool forward) noexcept {
	const std::size_t cursor = std::min(pos, mBufferModel.length());
	const std::size_t lineStart = lineStartOffset(cursor);
	const MREditSetupSettings settings = effectiveEditSetupSettings();
	const int currentColumn = (freeCursorMovementEnabled() && cursor == mBufferModel.cursor() && !mBufferModel.hasSelection() ? displayedCursorColumn() : charColumn(lineStart, cursor)) + 1;
	const int targetColumn = forward ? nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn) : prevResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);

	return charPtrOffset(lineStart, targetColumn - 1);
}

std::size_t MRFileEditor::prevWordOffset(std::size_t pos) noexcept {
	std::size_t p = std::min(pos, mBufferModel.length());

	while (p > 0 && !isWordByte(mBufferModel.charAt(p - 1)))
		--p;
	while (p > 0 && isWordByte(mBufferModel.charAt(p - 1)))
		--p;
	return p;
}

std::size_t MRFileEditor::nextWordOffset(std::size_t pos) noexcept {
	std::size_t p = std::min(pos, mBufferModel.length());
	std::size_t len = mBufferModel.length();

	while (p < len && isWordByte(mBufferModel.charAt(p)))
		++p;
	while (p < len && !isWordByte(mBufferModel.charAt(p)))
		++p;
	return p;
}

std::size_t MRFileEditor::charPtrOffset(std::size_t start, int pos) noexcept {
	std::size_t lineStart = mBufferModel.lineStart(start);
	std::string lineText = mBufferModel.lineText(lineStart);
	TStringView line(lineText.data(), lineText.size());
	const MREditSetupSettings settings = effectiveEditSetupSettings();
	std::size_t p = 0;
	int visual = 0;
	int target = std::max(pos, 0);

	while (p < line.size()) {
		std::size_t next = p;
		std::size_t width = 0;
		if (!nextDisplayChar(line, next, width, visual, settings)) break;
		if (visual + static_cast<int>(width) > target) break;
		visual += static_cast<int>(width);
		p = next;
	}
	return lineStart + p;
}

void MRFileEditor::ensureCursorVisible(bool centerCursor) {
	int visualColumn = displayedCursorColumn();
	int line = static_cast<int>(visibleLineForDocumentLine(displayedCursorLineIndex()));
	int targetX = delta.x;
	int targetY = delta.y;
	int viewportWidth = textViewportWidth();
	int textRows = std::max(1, visibleTextRows());

	if (visualColumn < targetX) targetX = visualColumn;
	else if (visualColumn >= targetX + viewportWidth)
		targetX = visualColumn - viewportWidth + 1;
	if (centerCursor) targetY = std::max(0, line - textRows / 2);
	else if (line < targetY)
		targetY = line;
	else if (line >= targetY + textRows)
		targetY = line - textRows + 1;
	if (targetX != delta.x || targetY != delta.y) scrollTo(targetX, targetY);
}

void MRFileEditor::moveCursor(std::size_t target, bool extendSelection, bool centerCursor, int requestedVisualColumn) {
	target = canonicalCursorOffset(std::min(target, mBufferModel.length()));
	if (extendSelection) {
		std::size_t anchor = mBufferModel.hasSelection() ? mBufferModel.selection().anchor : mBufferModel.cursor();
		mSelectionAnchor = anchor;
		mBufferModel.setCursorAndSelection(target, anchor, target);
	} else {
		if (configuredPersistentBlocksSetting() && mBufferModel.hasSelection()) mBufferModel.setCursor(target);
		else
			mBufferModel.setCursorAndSelection(target, target, target);
		mSelectionAnchor = target;
	}
	if (!freeCursorMovementEnabled() || target != mBufferModel.length()) mCursorVisualLine = cachedCursorLineIndex();
	else
		mCursorVisualLine = std::max(mCursorVisualLine, cachedCursorLineIndex());
	if (freeCursorMovementEnabled() && requestedVisualColumn >= 0 && target == mBufferModel.length())
		mCursorVisualColumn = requestedVisualColumn;
	else if (freeCursorMovementEnabled() && requestedVisualColumn >= 0)
		mCursorVisualColumn = std::max(actualCursorVisualColumn(target), requestedVisualColumn);
	else
		mCursorVisualColumn = actualCursorVisualColumn(target);
	if (useApproximateLargeFileMetrics()) {
		updateMetrics();
	}
	ensureCursorVisible(centerCursor);
	scheduleSyntaxWarmupIfNeeded();
	updateIndicator();
	drawView();
}

std::size_t MRFileEditor::mouseOffset(TPoint local, int *visualColumnOut) noexcept {
	TextViewportGeometry viewport = textViewportGeometry();
	const int textRows = std::max(1, visibleTextRows());
	int clampedY = std::max(0, std::min(local.y - viewport.topInset, textRows - 1));
	int row = clampedY + delta.y;
	int column = viewport.textColumnFromLocalX(local.x);
	std::size_t start = mBufferModel.lineStartByIndex(documentLineForVisibleLine(static_cast<std::size_t>(std::max(row, 0))));
	if (visualColumnOut != nullptr) *visualColumnOut = column;
	return canonicalCursorOffset(charPtrOffset(start, column));
}

std::size_t MRFileEditor::canonicalCursorOffset(std::size_t pos) const noexcept {
	pos = std::min(pos, mBufferModel.length());
	if (pos > 0 && pos < mBufferModel.length() && mBufferModel.charAt(pos) == '\n' && mBufferModel.charAt(pos - 1) == '\r') return pos - 1;
	return pos;
}
