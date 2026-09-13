#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"
#include "../MRMessageLineController.hpp"
#include "../../app/MREditorApp.hpp"

#include <algorithm>
#include <cstdlib>

struct MRFileEditor::BlockDragPreview {
	MRTextBufferModel::ReadSnapshot snapshot;
	int mode = 0;
	std::size_t sourceStart = 0;
	std::size_t sourceEnd = 0;
	std::size_t firstLine = 0;
	std::size_t lastLine = 0;
	int firstColumn = 0;
	int lastColumn = 0;
	std::size_t targetLine = 0;
	std::size_t targetOffset = 0;
	int targetColumn = 0;
	bool visible = false;
	bool valid = false;
};

bool MRFileEditor::handleBlockDrag(TEvent &event) {
	MREditWindow *window = dynamic_cast<MREditWindow *>(owner);
	if (window == nullptr || !window->hasBlock() || mReadOnly || !textPointInView(event.mouse.where)) return false;
	if (event.mouse.buttons != mbLeftButton || (event.mouse.eventFlags & meDoubleClick) != 0 ||
	    (event.mouse.controlKeyState & (kbShift | kbCtrlShift | kbAltShift)) != 0) return false;
	const TextViewportGeometry viewport = textViewportGeometry();
	const TPoint press = makeLocal(event.mouse.where);
	int pressColumn = 0;
	const std::size_t pressOffset = mouseOffset(press, &pressColumn);
	const std::size_t pressLine = documentLineForVisibleLine(static_cast<std::size_t>(std::max(0, delta.y + press.y - viewport.topInset)));
	if (!window->blockContainsPosition(pressOffset, pressLine, pressColumn)) return false;

	BlockDragPreview preview;
	preview.snapshot = readSnapshot();
	preview.mode = window->blockStatus();
	preview.firstLine = static_cast<std::size_t>(std::max(1, window->blockLine1()) - 1);
	preview.lastLine = static_cast<std::size_t>(std::max(1, window->blockLine2()) - 1);
	preview.sourceStart = std::min(window->blockAnchorPtr(), window->blockEffectiveEndPtr());
	preview.sourceEnd = std::max(window->blockAnchorPtr(), window->blockEffectiveEndPtr());
	if (preview.mode == MREditWindow::bmLine) {
		preview.sourceStart = preview.snapshot.lineStartByIndex(preview.firstLine);
		preview.sourceEnd = preview.snapshot.nextLine(preview.snapshot.lineStartByIndex(preview.lastLine));
	} else if (preview.mode == MREditWindow::bmColumn) {
		preview.firstColumn = window->blockCol1() - 1;
		preview.lastColumn = window->blockCol2() - 1;
	} else {
		preview.firstColumn = charColumn(lineStartOffset(preview.sourceStart), preview.sourceStart);
		if (preview.sourceEnd > preview.sourceStart) preview.lastLine = preview.snapshot.lineIndex(preview.sourceEnd - 1);
	}
	const TPoint originalScroll = delta;
	const std::size_t originalCursor = cursorOffset();
	const int originalColumn = displayedCursorColumn();
	const std::size_t gripRows = pressLine - preview.firstLine;
	const int gripColumns = pressColumn - preview.firstColumn;
	const std::size_t lastDocumentLine = std::max<std::size_t>(1, preview.snapshot.lineCount()) - 1;
	MREditorApp *app = dynamic_cast<MREditorApp *>(TProgram::application);
	if (app != nullptr) app->beginInteractiveMouseCapture();
	mBlockDragPreview = &preview;
	bool dragged = false;
	bool cancelled = false;
	bool released = false;
	while (!released) {
		TEvent next;
		getEvent(next);
		if (next.what == evNothing || next.what == evKeyState) continue;
		if (next.what == evKeyDown) {
			if (next.keyDown.keyCode == kbEsc) {
				cancelled = true;
				break;
			}
			continue;
		}
		if ((next.what & (evMouseMove | evMouseAuto | evMouseUp | evMouseWheel)) == 0) {
			putEvent(next);
			cancelled = true;
			break;
		}
		const TPoint point = makeLocal(next.mouse.where);
		released = next.what == evMouseUp;
		if (!dragged && (std::abs(point.x - press.x) >= 1 || std::abs(point.y - press.y) >= 1)) dragged = true;
		if (!dragged) continue;
		const TPoint previousScroll = delta;
		int dx = delta.x;
		int dy = delta.y;
		if (!released) {
			if (point.x < viewport.textLeft) --dx;
			else if (point.x >= viewport.textRight) ++dx;
			if (point.y < viewport.topInset) --dy;
			else if (point.y >= viewport.topInset + visibleTextRows()) ++dy;
			if (dx != delta.x || dy != delta.y) scrollTo(std::max(0, dx), std::max(0, dy));
			if (next.what == evMouseWheel) static_cast<void>(scrollWindowByWheel(next.mouse.wheel));
		}
		const int row = std::max(0, std::min(point.y - viewport.topInset, std::max(0, visibleTextRows() - 1)));
		const std::size_t pointerLine = std::min(lastDocumentLine, documentLineForVisibleLine(static_cast<std::size_t>(std::max(0, delta.y + row))));
		int pointerColumn = 0;
		static_cast<void>(mouseOffset(point, &pointerColumn));
		const std::size_t targetLine = pointerLine >= gripRows ? pointerLine - gripRows : 0;
		int targetColumn = preview.mode == MREditWindow::bmLine ? 0 : std::max(0, pointerColumn - gripColumns);
		const std::size_t targetStart = preview.snapshot.lineStartByIndex(targetLine);
		const std::size_t targetOffset = canonicalCursorOffset(charPtrOffset(targetStart, targetColumn));
		if (!freeCursorMovementEnabled()) targetColumn = charColumn(targetStart, targetOffset);
		const bool changed = !preview.visible || preview.targetLine != targetLine || preview.targetColumn != targetColumn || delta.x != previousScroll.x || delta.y != previousScroll.y;
		preview.targetLine = targetLine;
		preview.targetColumn = targetColumn;
		preview.targetOffset = targetOffset;
		preview.visible = true;
		if (preview.mode == MREditWindow::bmColumn) {
			const std::size_t bottom = targetLine + preview.lastLine - preview.firstLine;
			const int right = targetColumn + preview.lastColumn - preview.firstColumn;
			preview.valid = bottom < preview.firstLine || targetLine > preview.lastLine || right <= preview.firstColumn || targetColumn >= preview.lastColumn;
		} else
			preview.valid = targetOffset < preview.sourceStart || targetOffset > preview.sourceEnd;
		if (released && !textPointInView(next.mouse.where)) cancelled = true;
		if (changed) {
			drawView();
			hideCursor();
		}
	}
	mBlockDragPreview = nullptr;
	if (app != nullptr) app->endInteractiveMouseCapture();
	if (!cancelled && released && dragged && preview.valid && documentVersion() == preview.snapshot.version()) {
		if (window->isBlockMarking()) window->endBlock();
		setCursorOffsetAtVisualColumn(preview.targetOffset, preview.targetColumn);
		std::string error;
		if (!window->moveBlock(&error)) {
			setCursorOffsetAtVisualColumn(originalCursor, originalColumn);
			if (!error.empty()) mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, error, mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
		}
	} else if (!cancelled && released && !dragged) {
		if (window->isBlockMarking()) window->endBlock();
		setCursorOffsetAtVisualColumn(pressOffset, pressColumn);
	} else
		scrollTo(originalScroll.x, originalScroll.y);
	mMouseSelectionModifiers = 0;
	mMouseSelectionColumnsValid = false;
	mMouseSelectionLinesValid = false;
	drawView();
	clearEvent(event);
	return true;
}

void MRFileEditor::drawBlockDragPreview(TDrawBuffer &buffer, std::size_t line, int textLeft, int width) {
	const BlockDragPreview &preview = *mBlockDragPreview;
	if (!preview.visible || line < preview.targetLine || line - preview.targetLine > preview.lastLine - preview.firstLine) return;
	const std::size_t sourceLine = preview.firstLine + line - preview.targetLine;
	const std::size_t sourceStart = preview.snapshot.lineStartByIndex(sourceLine);
	const std::string sourceText = preview.snapshot.lineText(sourceStart);
	const TStringView text(sourceText.data(), sourceText.size());
	const MREditSetupSettings &settings = effectiveEditSetupSettings();
	TColorAttr color = static_cast<TColorAttr>(getColor(0x0201) >> 8);
	setStyle(color, getStyle(color) | slBold | (preview.valid ? 0 : slUnderline));
	int sourceColumn = 0;
	int destinationColumn = preview.mode == MREditWindow::bmStream && sourceLine != preview.firstLine ? 0 : preview.targetColumn;
	std::size_t byte = 0;
	int previewWidth = 0;
	while (byte < text.size()) {
		std::size_t next = byte;
		std::size_t charWidth = 0;
		if (!nextDisplayChar(text, next, charWidth, sourceColumn, settings)) break;
		const int nextColumn = sourceColumn + static_cast<int>(charWidth);
		bool included = preview.mode == MREditWindow::bmLine;
		int first = sourceColumn;
		int last = nextColumn;
		if (preview.mode == MREditWindow::bmColumn) {
			first = std::max(first, preview.firstColumn);
			last = std::min(last, preview.lastColumn);
			included = first < last;
		} else if (preview.mode == MREditWindow::bmStream)
			included = preview.sourceStart <= sourceStart + byte && sourceStart + byte < preview.sourceEnd;
		if (included) {
			const int cellWidth = text[byte] == '\t' && preview.mode != MREditWindow::bmColumn ? tabDisplayWidth(settings, destinationColumn) : last - first;
			const int x = destinationColumn - delta.x;
			const int left = std::max(0, x);
			const int right = std::min(width, x + cellWidth);
			if (left < right) {
				if (text[byte] == '\t' || x < 0 || first != sourceColumn)
					buffer.moveChar(static_cast<ushort>(textLeft + left), ' ', color, static_cast<ushort>(right - left));
				else
					buffer.moveStr(static_cast<ushort>(textLeft + left), text.substr(byte, next - byte), color, static_cast<ushort>(right - left));
			}
			destinationColumn += cellWidth;
			previewWidth += cellWidth;
		}
		sourceColumn = nextColumn;
		byte = next;
	}
	const int fillEnd = preview.mode == MREditWindow::bmColumn ? preview.targetColumn + preview.lastColumn - preview.firstColumn :
	                    preview.mode == MREditWindow::bmLine ? delta.x + width : destinationColumn + (previewWidth == 0 ? 1 : 0);
	const int left = std::max(0, destinationColumn - delta.x);
	const int right = std::min(width, fillEnd - delta.x);
	if (left < right) buffer.moveChar(static_cast<ushort>(textLeft + left), ' ', color, static_cast<ushort>(right - left));
}
