#include "MREditWindow.hpp"

#include <cctype>

bool MREditWindow::blockContainsPosition(std::size_t offset, std::size_t line, int column) const {
	if (!mBlockOps.hasVisibleBlock()) return false;
	const MRFEBlockGeometry &block = mBlockOps.mGeometry;
	switch (block.mode) {
	case MRFEBlockMode::Stream:
		return block.rangeStart <= offset && offset < block.rangeEnd;
	case MRFEBlockMode::Line:
		return block.line1 <= line && line <= block.line2;
	case MRFEBlockMode::Column:
		return block.line1 <= line && line <= block.line2 && block.col1 <= column && column < block.col2;
	case MRFEBlockMode::None:
		return false;
	}
	return false;
}

bool MREditWindow::prepareBlockForEditorInput(const TEvent &event) {
	if (event.what != evKeyDown || editor == nullptr || editor->isReadOnly() || !mBlockOps.hasVisibleBlock()) return false;
	const ushort modifiers = event.keyDown.controlKeyState;
	const unsigned char charCode = static_cast<unsigned char>(event.keyDown.charScan.charCode);
	const bool pastedText = (modifiers & kbPaste) != 0;
	if (pastedText && editor->hasPositionedClipboardPaste()) return false;
	const bool singleByteText = charCode >= 32 && charCode < 255;
	const bool newLine = ctrlToArrow(event.keyDown.keyCode) == kbEnter;
	const bool plainTab = charCode == 9 && (modifiers & (kbShift | kbCtrlShift | kbAltShift)) == 0;
	if (!pastedText && !singleByteText && !newLine && !plainTab) return false;
	const std::size_t cursor = editor->cursorOffset();
	if (blockContainsPosition(cursor, editor->displayedCursorLineIndex(), editor->displayedCursorColumn())) return !configuredPersistentBlocksSetting();
	if (mBlockOps.isMarking()) endBlock();
	if (editor->hasTextSelection()) editor->setSelectionOffsets(cursor, cursor, False);
	return false;
}

// Return true when the block owns the deletion, including a failed block operation.
bool MREditWindow::deleteBlockForEditorInput(bool backward) {
	if (editor == nullptr || editor->isReadOnly() || !mBlockOps.hasVisibleBlock()) return false;
	const std::size_t cursor = editor->cursorOffset();
	const std::size_t line = editor->displayedCursorLineIndex();
	const int column = editor->displayedCursorColumn();
	if (!backward && blockContainsPosition(cursor, line, column)) {
		std::string error;
		if (!deleteBlock(&error) && !error.empty()) mrLogMessage(error.c_str());
		return true;
	}
	if (mBlockOps.isMarking()) endBlock();
	if (editor->hasTextSelection()) editor->setSelectionOffsets(cursor, cursor, False);
	return false;
}

bool MREditWindow::doubleClickWordByte(char ch) noexcept {
	const unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalnum(uch) != 0 || ch == '_';
}

bool MREditWindow::currentWordRange(std::size_t &start, std::size_t &end) const {
	if (editor == nullptr || editor->bufferLength() == 0) return false;

	const std::size_t length = editor->bufferLength();
	std::size_t probe = std::min(editor->cursorOffset(), length);

	if (probe < length && doubleClickWordByte(editor->charAtOffset(probe))) {
	} else if (probe > 0 && doubleClickWordByte(editor->charAtOffset(probe - 1)))
		--probe;
	else
		return false;

	start = probe;
	while (start > 0 && doubleClickWordByte(editor->charAtOffset(start - 1)))
		--start;
	end = probe + 1;
	while (end < length && doubleClickWordByte(editor->charAtOffset(end)))
		++end;
	return start < end;
}

std::size_t MREditWindow::lineBlockEndForStart(std::size_t lineStart) const {
	if (editor == nullptr) return 0;
	const std::size_t length = editor->bufferLength();
	if (lineStart >= length) return length;

	std::size_t next = editor->nextLineOffset(lineStart);
	if (next <= lineStart) next = length;
	return std::min(next, length);
}

std::size_t MREditWindow::lineBlockCursorEndForStart(std::size_t lineStart) const {
	if (editor == nullptr) return 0;
	return editor->lineEndOffset(lineStart);
}

std::size_t MREditWindow::wholeFileLineBlockCursorEnd() const {
	if (editor == nullptr || editor->bufferLength() == 0) return 0;

	std::size_t probe = editor->bufferLength();
	if (probe > 0 && editor->charAtOffset(probe - 1) == '\n') --probe;
	return editor->lineEndOffset(probe);
}

bool MREditWindow::lineIsBlank(std::size_t lineStart) const {
	if (editor == nullptr) return true;

	const std::size_t lineEnd = editor->lineEndOffset(lineStart);
	for (std::size_t pos = lineStart; pos < lineEnd; ++pos) {
		const unsigned char ch = static_cast<unsigned char>(editor->charAtOffset(pos));
		if (std::isspace(ch) == 0) return false;
	}
	return true;
}

bool MREditWindow::currentParagraphRange(std::size_t &start, std::size_t &end, std::size_t &cursorEnd) const {
	if (editor == nullptr || editor->bufferLength() == 0) return false;

	start = editor->lineStartOffset(editor->cursorOffset());
	if (lineIsBlank(start)) return false;

	while (start > 0) {
		const std::size_t previous = editor->lineStartOffset(editor->prevLineOffset(start));
		if (previous == start || lineIsBlank(previous)) break;
		start = previous;
	}

	std::size_t lineStart = editor->lineStartOffset(editor->cursorOffset());
	std::size_t lastContentLineStart = lineStart;
	end = lineBlockEndForStart(lastContentLineStart);
	while (end < editor->bufferLength()) {
		lineStart = end;
		if (lineIsBlank(lineStart)) break;
		lastContentLineStart = lineStart;
		end = lineBlockEndForStart(lastContentLineStart);
	}
	cursorEnd = lineBlockCursorEndForStart(lastContentLineStart);
	return start < end;
}

bool MREditWindow::visibleBlockMatches(MRFEBlockMode mode, std::size_t start, std::size_t end) const {
	return mBlockOps.hasVisibleBlock() && mBlockOps.mGeometry.mode == mode && mBlockOps.mGeometry.rangeStart == start && mBlockOps.mGeometry.rangeEnd == end;
}

bool MREditWindow::handleEditorDoubleClickBlockExpansion() {
	if (editor == nullptr) return false;

	std::size_t wordStart = 0;
	std::size_t wordEnd = 0;
	if (!currentWordRange(wordStart, wordEnd)) return false;

	const std::size_t lineStart = editor->lineStartOffset(editor->cursorOffset());
	const std::size_t lineRangeEnd = lineBlockEndForStart(lineStart);
	const std::size_t lineCursorEnd = lineBlockCursorEndForStart(lineStart);
	std::size_t paragraphStart = 0;
	std::size_t paragraphEnd = 0;
	std::size_t paragraphCursorEnd = 0;
	const bool hasParagraph = currentParagraphRange(paragraphStart, paragraphEnd, paragraphCursorEnd);

	if (hasParagraph && visibleBlockMatches(MRFEBlockMode::Line, paragraphStart, paragraphEnd)) return mBlockOps.setCommittedBlock(*editor, MRFEBlockMode::Line, 0, wholeFileLineBlockCursorEnd());
	if (hasParagraph && visibleBlockMatches(MRFEBlockMode::Line, lineStart, lineRangeEnd)) return mBlockOps.setCommittedBlock(*editor, MRFEBlockMode::Line, paragraphStart, paragraphCursorEnd);
	if (visibleBlockMatches(MRFEBlockMode::Stream, wordStart, wordEnd)) return mBlockOps.setCommittedBlock(*editor, MRFEBlockMode::Line, lineStart, lineCursorEnd);
	return mBlockOps.setCommittedBlock(*editor, MRFEBlockMode::Stream, wordStart, wordEnd);
}
