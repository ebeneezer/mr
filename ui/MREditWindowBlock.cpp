#include "MREditWindow.hpp"

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
	const MRFEBlockGeometry &block = mBlockOps.mGeometry;
	bool deleteMarkedBlock = blockContainsPosition(cursor, line, column);
	if (backward && !deleteMarkedBlock) {
		if (block.mode == MRFEBlockMode::Column)
			deleteMarkedBlock = block.line1 <= line && line <= block.line2 && column == block.col2;
		else
			deleteMarkedBlock = cursor == block.rangeEnd && line == editor->lineIndexOfOffset(cursor) &&
			                    column == editor->charColumn(editor->lineStartOffset(cursor), cursor);
	}
	if (deleteMarkedBlock) {
		std::string error;
		if (!deleteBlock(&error) && !error.empty()) mrLogMessage(error.c_str());
		return true;
	}
	if (mBlockOps.isMarking()) endBlock();
	if (editor->hasTextSelection()) editor->setSelectionOffsets(cursor, cursor, False);
	return false;
}
