#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"
#include "../../app/MRCommands.hpp"
#include "../../app/MRCommandRouter.hpp"
#include "../../app/MREditorApp.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"

#include <cstdlib>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>

namespace {

bool columnBlockTraceEnabled() noexcept {
	const char *value = std::getenv("MR_COLUMN_BLOCK_TRACE");
	return value != nullptr && value[0] != '\0' && value[0] != '0';
}

void appendColumnBlockTrace(std::string_view message) {
	if (!columnBlockTraceEnabled()) return;
	std::ofstream out(configuredLogFilePath(), std::ios::out | std::ios::app | std::ios::binary);
	if (out) out << "COLBLOCK event " << message << '\n';
}

const char *mouseEventName(ushort what) noexcept {
	switch (what) {
	case evMouseDown:
		return "down";
	case evMouseMove:
		return "move";
	case evMouseAuto:
		return "auto";
	case evMouseWheel:
		return "wheel";
	case evMouseUp:
		return "up";
	default:
		return "other";
	}
}

} // namespace

bool MRFileEditor::handleLineDrawingMouse(TEvent &event, TPoint local) {
	const TextViewportGeometry viewport = textViewportGeometry();
	auto mouseDocumentLine = [this, viewport](TPoint point) {
		const int textRows = std::max(1, visibleTextRows());
		const int clampedY = std::max(0, std::min(point.y - viewport.topInset, textRows - 1));
		const int row = clampedY + delta.y;
		return documentLineForVisibleLine(static_cast<std::size_t>(std::max(row, 0)));
	};
	int previousColumn = 0;
	std::size_t previousOffset = mouseOffset(local, &previousColumn);
	std::size_t previousLine = mouseDocumentLine(local);
	int lastAxis = 0;
	MREditorApp *app = dynamic_cast<MREditorApp *>(TProgram::application);
	const std::chrono::steady_clock::time_point traceStart = std::chrono::steady_clock::now();
	std::size_t traceMouseEvents = 0;
	std::size_t traceSegments = 0;
	std::size_t traceChangedSegments = 0;
	std::size_t traceDrawRequests = 0;
	std::size_t traceCoalescedMoves = 0;
	TEvent bufferedEvent{};
	bool bufferedEventValid = false;
	auto nextDragEvent = [this, &event, &bufferedEvent, &bufferedEventValid]() {
		if (bufferedEventValid) {
			event = bufferedEvent;
			bufferedEventValid = false;
			return event.what != evMouseUp;
		}
		return mouseEvent(event, evMouseMove | evMouseAuto | evMouseWheel);
	};
	auto mousePoint = [this, &mouseDocumentLine](const TEvent &source, int &column, std::size_t &line) {
		const TPoint dragLocal = makeLocal(source.mouse.where);
		static_cast<void>(mouseOffset(dragLocal, &column));
		line = mouseDocumentLine(dragLocal);
	};
	auto movementCollinear = [](std::size_t baseLine, int baseColumn, std::size_t firstLine, int firstColumn, std::size_t secondLine, int secondColumn) {
		return (baseLine == firstLine && firstLine == secondLine) || (baseColumn == firstColumn && firstColumn == secondColumn);
	};
	auto setLineDrawingMouseCursor = [this](std::size_t targetOffset, std::size_t targetLine, int targetColumn) {
		mSelectionAnchor = targetOffset;
		mBufferModel.setCursorAndSelection(targetOffset, targetOffset, targetOffset);
		if (freeCursorMovementEnabled()) {
			mCursorVisualLine = std::max(cachedCursorLineIndex(), targetLine);
			mCursorVisualColumn = std::max(actualCursorVisualColumn(targetOffset), targetColumn);
		} else
			mCursorVisualColumn = actualCursorVisualColumn(targetOffset);
		updateIndicator();
	};

	if (mReadOnly) return false;
	select();
	setLineDrawingMouseCursor(previousOffset, previousLine, previousColumn);
	++traceDrawRequests;
	drawView();
	if (app != nullptr) app->beginInteractiveMouseCapture();
	while (nextDragEvent()) {
		++traceMouseEvents;
		if (event.what == evMouseMove || event.what == evMouseAuto) {
			const TPoint mouse = makeLocal(event.mouse.where);
			int dx = delta.x;
			int dy = delta.y;
			if (mouse.x < viewport.textLeft) --dx;
			else if (mouse.x >= viewport.textRight)
				++dx;
			if (mouse.y < viewport.topInset) --dy;
			else if (mouse.y >= viewport.topInset + std::max(0, visibleTextRows()))
				++dy;
			if (dx != delta.x || dy != delta.y) scrollTo(std::max(dx, 0), std::max(dy, 0));
		} else if (event.what == evMouseWheel) {
			static_cast<void>(scrollWindowByWheel(event.mouse.wheel));
		}
		if ((event.mouse.buttons & mbLeftButton) == 0) break;
		int targetColumn = 0;
		const TPoint dragLocal = makeLocal(event.mouse.where);
		std::size_t targetOffset = mouseOffset(dragLocal, &targetColumn);
		std::size_t targetLine = mouseDocumentLine(dragLocal);
		if (event.what == evMouseMove || event.what == evMouseAuto) {
			while (true) {
				TEvent next{};
				int nextColumn = 0;
				std::size_t nextLine = 0;

				getEvent(next, 0);
				if (next.what == evNothing) break;
				if (next.what == evMouseMove || next.what == evMouseAuto || next.what == evMouseUp) {
					mousePoint(next, nextColumn, nextLine);
					if ((next.mouse.buttons & mbLeftButton) != 0 && movementCollinear(previousLine, previousColumn, targetLine, targetColumn, nextLine, nextColumn)) {
						event = next;
						targetColumn = nextColumn;
						targetLine = nextLine;
						targetOffset = mouseOffset(makeLocal(event.mouse.where), &targetColumn);
						++traceMouseEvents;
						++traceCoalescedMoves;
						if (next.what == evMouseUp) break;
						continue;
					}
					bufferedEvent = next;
					bufferedEventValid = true;
					break;
				}
				if (next.what == evMouseWheel) {
					bufferedEvent = next;
					bufferedEventValid = true;
					break;
				}
				putEvent(next);
				break;
			}
		}
		if (targetLine != previousLine || targetColumn != previousColumn) {
			++traceSegments;
			if (drawLineDrawingMouseSegment(previousLine, previousColumn, targetLine, targetColumn, lastAxis)) ++traceChangedSegments;
			targetOffset = charPtrOffset(mBufferModel.lineStartByIndex(targetLine), targetColumn);
			previousLine = targetLine;
			previousColumn = targetColumn;
			previousOffset = targetOffset;
			setLineDrawingMouseCursor(targetOffset, targetLine, targetColumn);
			++traceDrawRequests;
			drawView();
		}
		if (event.what == evMouseUp) break;
	}
	if (event.what == evMouseUp) {
		++traceMouseEvents;
		int targetColumn = 0;
		const TPoint dragLocal = makeLocal(event.mouse.where);
		std::size_t targetOffset = mouseOffset(dragLocal, &targetColumn);
		std::size_t targetLine = mouseDocumentLine(dragLocal);
		if (targetLine != previousLine || targetColumn != previousColumn) {
			++traceSegments;
			if (drawLineDrawingMouseSegment(previousLine, previousColumn, targetLine, targetColumn, lastAxis)) ++traceChangedSegments;
			targetOffset = charPtrOffset(mBufferModel.lineStartByIndex(targetLine), targetColumn);
			setLineDrawingMouseCursor(targetOffset, targetLine, targetColumn);
			++traceDrawRequests;
			drawView();
		}
	}
	if (app != nullptr) app->endInteractiveMouseCapture();
	mMouseSelectionModifiers = 0;
	mMouseSelectionColumnsValid = false;
	mMouseSelectionLinesValid = false;
	if (columnBlockTraceEnabled()) {
		const std::chrono::steady_clock::time_point traceEnd = std::chrono::steady_clock::now();
		const long long elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(traceEnd - traceStart).count();
		std::ostringstream trace;
		trace << "ld-mouse summary events=" << traceMouseEvents << " segments=" << traceSegments << " changedSegments=" << traceChangedSegments << " drawRequests=" << traceDrawRequests
		      << " coalescedMoves=" << traceCoalescedMoves << " elapsedUs=" << elapsedMicroseconds << " finalLine=" << displayedCursorLineIndex() << " finalColumn=" << displayedCursorColumn();
		appendColumnBlockTrace(trace.str());
	}
	clearEvent(event);
	return true;
}

void MRFileEditor::handleMouse(TEvent &event) {
	const TextViewportGeometry viewport = textViewportGeometry();
	const TPoint local = makeLocal(event.mouse.where);
	std::size_t foldLineIndex = 0;
	mMouseSelectionColumnsValid = false;
	mMouseSelectionLinesValid = false;
	mMouseSelectionModifiers = event.mouse.controlKeyState;
	auto gutterSpanAtPoint = [this, &local, &viewport](std::size_t lineIndex) noexcept -> const MRFoldSpan * {
		const std::vector<unsigned short> &displayLevels = mFoldState.visibleState().displayLevels;
		const std::vector<MRFoldSpan> &visibleSpans = mFoldState.visibleState().spans;
		const int displayColumn = local.x - viewport.codeFoldingX;
		if (displayColumn < 0 || static_cast<std::size_t>(displayColumn) >= displayLevels.size()) return nullptr;
		const unsigned short level = displayLevels[static_cast<std::size_t>(displayColumn)];
		for (const MRFoldSpan &span : visibleSpans) {
			if (span.level != level) continue;
			if (!span.open) {
				if (span.startLine == lineIndex) return &span;
				continue;
			}
			if (lineIndex >= span.startLine && lineIndex <= span.endLine) return &span;
		}
		return nullptr;
	};
	auto toggleFoldColumnsFromPoint = [this, &local, &viewport]() -> bool {
		const std::vector<unsigned short> &displayLevels = mFoldState.visibleState().displayLevels;
		const int displayColumn = local.x - viewport.codeFoldingX;
		if (displayColumn < 0 || static_cast<std::size_t>(displayColumn) >= displayLevels.size()) return false;
		return toggleDocumentFoldLevel(displayLevels[static_cast<std::size_t>(displayColumn)]);
	};

	if ((event.mouse.buttons & (mbLeftButton | mbRightButton)) == 0) return;
	if (dragFormatRulerAtLocalPoint(event, local)) {
		clearEvent(event);
		return;
	}
	if (foldingGutterHit(local, &foldLineIndex)) {
		if ((event.mouse.buttons & mbRightButton) != 0 && toggleFoldColumnsFromPoint()) {
			clearEvent(event);
			return;
		}
		if ((event.mouse.buttons & mbLeftButton) != 0) {
			const MRFoldSpan *clickedSpan = gutterSpanAtPoint(foldLineIndex);
			if (clickedSpan != nullptr && toggleFoldAtLine(clickedSpan->startLine)) {
				updateMetrics();
				drawView();
				updateIndicator();
				clearEvent(event);
				return;
			}
		}
	}
	const bool leftButton = (event.mouse.buttons & mbLeftButton) != 0;
	const bool rightButton = (event.mouse.buttons & mbRightButton) != 0;
	const bool mouseCtrl = (event.mouse.controlKeyState & kbCtrlShift) != 0;
	const bool mouseAlt = (event.mouse.controlKeyState & kbAltShift) != 0;
	const bool mouseShift = (event.mouse.controlKeyState & kbShift) != 0;
	if (mLineDrawingEnabled && leftButton && !rightButton && !mouseShift && !mouseCtrl && !mouseAlt && handleLineDrawingMouse(event, local)) return;

	if (handleBlockDrag(event)) return;
	select();
	auto mouseDocumentLine = [this, viewport](TPoint point) {
		const int textRows = std::max(1, visibleTextRows());
		const int clampedY = std::max(0, std::min(point.y - viewport.topInset, textRows - 1));
		const int row = clampedY + delta.y;
		return documentLineForVisibleLine(static_cast<std::size_t>(std::max(row, 0)));
	};
	int targetColumn = 0;
	std::size_t anchor = mouseOffset(local, &targetColumn);
	std::size_t anchorLine = mouseDocumentLine(local);
	const bool liveLineBlock = (mouseCtrl && mouseAlt) || (leftButton && rightButton);
	const bool liveColumnBlock = !liveLineBlock && (mouseAlt || (rightButton && !leftButton));
	const bool liveStreamBlock = !liveLineBlock && !liveColumnBlock && (leftButton || rightButton || mouseCtrl);
	const int liveBlockMode = liveColumnBlock ? 2 : liveLineBlock ? 1 : liveStreamBlock ? 3 : 0;
	const unsigned short liveSelectionModifiers = liveLineBlock ? static_cast<unsigned short>(kbCtrlShift | kbAltShift) : liveColumnBlock ? kbAltShift : liveStreamBlock ? kbCtrlShift : 0;
	const bool explicitBlockMouseGesture = liveBlockMode != 0 && (mouseCtrl || mouseAlt || (leftButton && rightButton) || (rightButton && !leftButton));
	const std::chrono::steady_clock::time_point traceStart = std::chrono::steady_clock::now();
	std::size_t traceMouseEvents = 0;
	std::size_t traceGeometryUpdates = 0;
	std::size_t traceOverlayUpdates = 0;
	std::size_t traceDrawRequests = 0;
	std::size_t traceSkippedUnchanged = 0;
	if (columnBlockTraceEnabled() && liveColumnBlock) {
		std::ostringstream trace;
		trace << "start local=(" << local.x << ',' << local.y << ") anchor=" << anchor << " anchorLine=" << mBufferModel.lineIndex(anchor) << " targetColumn=" << targetColumn
		      << " buttons=" << event.mouse.buttons << " mods=" << event.mouse.controlKeyState << " delta=(" << delta.x << ',' << delta.y << ") rows=" << visibleTextRows()
		      << " lineCount=" << mBufferModel.lineCount() << " length=" << mBufferModel.length();
		appendColumnBlockTrace(trace.str());
	}
	mMouseSelectionModifiers = 0;
	auto updateLiveMouseBlockOverlay = [this, liveBlockMode](std::size_t current, bool redraw) {
		if (liveBlockMode == 0) return false;
		std::size_t visualAnchor = mSelectionAnchor;
		std::size_t visualEnd = current;
		if (liveBlockMode == 1) {
			const std::size_t length = mBufferModel.length();

			if (visualAnchor == length && length > 0) --visualAnchor;
			if ((visualEnd == length && length > 0) || (visualEnd > 0 && lineStartOffset(visualEnd) == visualEnd && lineEndOffset(visualEnd) == visualEnd)) --visualEnd;
		}
		const bool lineRangeValid = liveBlockMode == 2 && mMouseSelectionLinesValid;
		setBlockOverlayState(liveBlockMode, visualAnchor, visualEnd, true, false, mMouseSelectionAnchorColumn, mMouseSelectionCursorColumn, lineRangeValid,
		                     lineRangeValid ? mMouseSelectionAnchorLine : 0, lineRangeValid ? mMouseSelectionCursorLine : 0, redraw);
		if (columnBlockTraceEnabled() && liveBlockMode == 2) {
			std::ostringstream trace;
			trace << "overlay anchor=" << mSelectionAnchor << " current=" << current << " visualAnchor=" << visualAnchor << " visualEnd=" << visualEnd
			      << " anchorColumn=" << mMouseSelectionAnchorColumn << " cursorColumn=" << mMouseSelectionCursorColumn;
			appendColumnBlockTrace(trace.str());
		}
		return true;
	};
	mSelectionAnchor = anchor;
	mMouseSelectionColumnsValid = true;
	mMouseSelectionLinesValid = true;
	mMouseSelectionAnchorColumn = targetColumn;
	mMouseSelectionCursorColumn = targetColumn;
	mMouseSelectionAnchorLine = anchorLine;
	mMouseSelectionCursorLine = anchorLine;
	auto setMouseSelectionColumns = [this, liveColumnBlock, targetColumn](int cursorColumn, bool activeSelection) {
		mMouseSelectionAnchorColumn = targetColumn;
		mMouseSelectionCursorColumn = cursorColumn;
		if (!activeSelection || !liveColumnBlock) return;
		if (cursorColumn < targetColumn) mMouseSelectionAnchorColumn = targetColumn + 1;
		else
			mMouseSelectionCursorColumn = cursorColumn + 1;
	};
	mBufferModel.setCursorAndSelection(anchor, anchor, anchor);
	if (freeCursorMovementEnabled()) {
		mCursorVisualLine = std::max(cachedCursorLineIndex(), anchorLine);
		mCursorVisualColumn = std::max(actualCursorVisualColumn(anchor), targetColumn);
	} else
		mCursorVisualColumn = actualCursorVisualColumn(anchor);
	updateIndicator();
	if (!explicitBlockMouseGesture) {
		++traceDrawRequests;
		drawView();
	}

	MREditorApp *app = dynamic_cast<MREditorApp *>(TProgram::application);
	if (app != nullptr) app->beginInteractiveMouseCapture();
	bool dragged = false;
	while (mouseEvent(event, evMouseMove | evMouseAuto | evMouseWheel)) {
		++traceMouseEvents;
		bool scrolled = false;
		if (event.what == evMouseMove || event.what == evMouseAuto) {
			const TPoint mouse = makeLocal(event.mouse.where);
			int dx = delta.x;
			int dy = delta.y;
			if (mouse.x < viewport.textLeft) --dx;
			else if (mouse.x >= viewport.textRight)
				++dx;
			if (mouse.y < viewport.topInset) --dy;
			else if (mouse.y >= viewport.topInset + std::max(0, visibleTextRows()))
				++dy;
			if (dx != delta.x || dy != delta.y) {
				scrollTo(std::max(dx, 0), std::max(dy, 0));
				scrolled = true;
			}
		} else if (event.what == evMouseWheel) {
			static_cast<void>(scrollWindowByWheel(event.mouse.wheel));
			scrolled = true;
		}
		int dragColumn = 0;
		const TPoint dragLocal = makeLocal(event.mouse.where);
		std::size_t target = mouseOffset(dragLocal, &dragColumn);
		std::size_t targetLine = mouseDocumentLine(dragLocal);
		int previousDragColumn = mMouseSelectionCursorColumn;
		if (liveColumnBlock && mMouseSelectionCursorColumn > mMouseSelectionAnchorColumn) previousDragColumn = mMouseSelectionCursorColumn - 1;
		const bool targetChanged = target != mBufferModel.cursor() || dragColumn != previousDragColumn || targetLine != mMouseSelectionCursorLine;
		if (target != anchor || dragColumn != targetColumn || targetLine != anchorLine) {
			dragged = true;
			mMouseSelectionModifiers = liveSelectionModifiers;
		}
		if (!targetChanged && !scrolled) {
			++traceSkippedUnchanged;
			continue;
		}
		++traceGeometryUpdates;
		mMouseSelectionCursorLine = targetLine;
		setMouseSelectionColumns(dragColumn, dragged);
		mBufferModel.setCursorAndSelection(target, mSelectionAnchor, target);
		if (freeCursorMovementEnabled()) {
			mCursorVisualLine = std::max(cachedCursorLineIndex(), targetLine);
			mCursorVisualColumn = std::max(actualCursorVisualColumn(target), dragColumn);
		} else
			mCursorVisualColumn = actualCursorVisualColumn(target);
		if (dragged && updateLiveMouseBlockOverlay(target, false)) ++traceOverlayUpdates;
		if (columnBlockTraceEnabled() && liveColumnBlock) {
			std::ostringstream trace;
			trace << mouseEventName(event.what) << " local=(" << dragLocal.x << ',' << dragLocal.y << ") target=" << target << " targetLine=" << mBufferModel.lineIndex(target)
			      << " dragColumn=" << dragColumn << " dragged=" << dragged << " anchorColumn=" << mMouseSelectionAnchorColumn
			      << " cursorColumn=" << mMouseSelectionCursorColumn << " selectionAnchor=" << mSelectionAnchor << " cursor=" << mBufferModel.cursor()
			      << " delta=(" << delta.x << ',' << delta.y << ')';
			appendColumnBlockTrace(trace.str());
		}
		updateIndicator();
		++traceDrawRequests;
		drawView();
	}
	if (event.what == evMouseUp) {
		++traceMouseEvents;
		int dragColumn = 0;
		const TPoint dragLocal = makeLocal(event.mouse.where);
		std::size_t target = mouseOffset(dragLocal, &dragColumn);
		std::size_t targetLine = mouseDocumentLine(dragLocal);
		int previousDragColumn = mMouseSelectionCursorColumn;
		if (liveColumnBlock && mMouseSelectionCursorColumn > mMouseSelectionAnchorColumn) previousDragColumn = mMouseSelectionCursorColumn - 1;
		const bool targetChanged = target != mBufferModel.cursor() || dragColumn != previousDragColumn || targetLine != mMouseSelectionCursorLine;
		if (target != anchor || dragColumn != targetColumn || targetLine != anchorLine) {
			dragged = true;
			mMouseSelectionModifiers = liveSelectionModifiers;
		}
		if (targetChanged) ++traceGeometryUpdates;
		mMouseSelectionCursorLine = targetLine;
		setMouseSelectionColumns(dragColumn, dragged);
		mBufferModel.setCursorAndSelection(target, mSelectionAnchor, target);
		if (freeCursorMovementEnabled()) {
			mCursorVisualLine = std::max(cachedCursorLineIndex(), targetLine);
			mCursorVisualColumn = std::max(actualCursorVisualColumn(target), dragColumn);
		} else
			mCursorVisualColumn = actualCursorVisualColumn(target);
		if (dragged && updateLiveMouseBlockOverlay(target, false)) ++traceOverlayUpdates;
		if (columnBlockTraceEnabled() && liveColumnBlock) {
			std::ostringstream trace;
			trace << mouseEventName(event.what) << " local=(" << dragLocal.x << ',' << dragLocal.y << ") target=" << target << " targetLine=" << mBufferModel.lineIndex(target)
			      << " dragColumn=" << dragColumn << " dragged=" << dragged << " anchorColumn=" << mMouseSelectionAnchorColumn
			      << " cursorColumn=" << mMouseSelectionCursorColumn << " selectionAnchor=" << mSelectionAnchor << " cursor=" << mBufferModel.cursor()
			      << " mods=" << mMouseSelectionModifiers << " delta=(" << delta.x << ',' << delta.y << ')';
			appendColumnBlockTrace(trace.str());
		}
		updateIndicator();
		++traceDrawRequests;
		drawView();
	}
	if (app != nullptr) app->endInteractiveMouseCapture();
	if (!dragged) {
		mMouseSelectionModifiers = 0;
		mMouseSelectionColumnsValid = false;
		mMouseSelectionLinesValid = false;
	}
	if (columnBlockTraceEnabled() && liveColumnBlock) {
		const std::chrono::steady_clock::time_point traceEnd = std::chrono::steady_clock::now();
		const long long elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(traceEnd - traceStart).count();
		std::ostringstream trace;
		trace << "summary events=" << traceMouseEvents << " geometryUpdates=" << traceGeometryUpdates << " overlayUpdates=" << traceOverlayUpdates << " drawRequests=" << traceDrawRequests
		      << " skippedUnchanged=" << traceSkippedUnchanged << " elapsedUs=" << elapsedMicroseconds << " dragged=" << dragged << " finalLine=" << displayedCursorLineIndex()
		      << " finalColumn=" << displayedCursorColumn();
		appendColumnBlockTrace(trace.str());
	}
	clearEvent(event);
}
