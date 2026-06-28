#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"
#include "../../app/MRCommands.hpp"
#include "../../app/MRCommandRouter.hpp"
#include "../../app/MREditorApp.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"

#include <cstdlib>
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

}

bool MRFileEditor::hasShiftModifier(ushort mods) noexcept {
	return (mods & (kbShift | kbCtrlShift | kbAltShift)) != 0;
}

void captureCurrentBlockStateForUndo(TView *owner, MRTextBufferModel::CustomUndoRecord &record) {
	record.blockMode = 0;
	record.blockAnchor = 0;
	record.blockEnd = 0;
	record.blockAnchorColumn = -1;
	record.blockEndColumn = -1;
	record.blockMarkingOn = false;
	if (MREditWindow *window = dynamic_cast<MREditWindow *>(owner); window != nullptr && window->hasBlock()) {
		record.blockMode = window->blockStatus();
		record.blockAnchor = window->blockAnchorPtr();
		record.blockEnd = window->blockEffectiveEndPtr();
		if (record.blockMode == MREditWindow::bmColumn) {
			record.blockAnchorColumn = std::max(0, window->blockCol1() - 1);
			record.blockEndColumn = std::max(0, window->blockCol2() - 1);
		}
		record.blockMarkingOn = true;
	}
}

void applyRestoredBlockStateToOwner(MRFileEditor &editor, TView *owner, const MRTextBufferModel::CustomUndoRecord &record) {
	MREditWindow *window = dynamic_cast<MREditWindow *>(owner);

	if (record.blockMarkingOn && window != nullptr) {
		window->applyCommittedBlockState(record.blockMode, false, record.blockAnchor, record.blockEnd, record.blockAnchorColumn, record.blockEndColumn);
		return;
	}
	if (record.blockMarkingOn && owner != nullptr) {
		editor.setBlockOverlayState(record.blockMode, record.blockAnchor, record.blockEnd, record.blockMarkingOn, false, record.blockAnchorColumn, record.blockEndColumn);
		return;
	}
	if (owner == nullptr) {
		editor.setSelectionOffsets(editor.bufferModel().cursor(), editor.bufferModel().cursor(), False);
		return;
	}
	if (window != nullptr) window->clearBlock();
	else
		editor.setBlockOverlayState(0, 0, 0, false);
	editor.setSelectionOffsets(editor.bufferModel().cursor(), editor.bufferModel().cursor(), False);
}

void MRFileEditor::pushUndoSnapshot() {
	MRTextBufferModel::CustomUndoRecord record;
	record.preSnapshot = mBufferModel.readSnapshot();
	record.preSnapshot.dropExactLineStartIndex();
	record.cursor = mBufferModel.cursor();
	record.modifiedState = mBufferModel.isModified();
	if (mBufferModel.hasSelection()) {
		record.selAnchor = mBufferModel.selection().range().start;
		record.selCursor = mBufferModel.selection().range().end;
	} else {
		record.selAnchor = 0;
		record.selCursor = 0;
	}
	captureCurrentBlockStateForUndo(owner, record);
	mBufferModel.pushUndoSnapshot(std::move(record));
}

bool MRFileEditor::isTextInputEvent(const TEvent &event) const {
	if (event.what != evKeyDown) return false;
	const ushort mods = event.keyDown.controlKeyState;
	const bool plainTab = event.keyDown.charScan.charCode == 9 && (mods & (kbShift | kbCtrlShift | kbAltShift | kbPaste)) == 0;
	const bool singleByteText = event.keyDown.charScan.charCode >= 32 && event.keyDown.charScan.charCode < 255;
	return (event.keyDown.controlKeyState & kbPaste) != 0 || plainTab || singleByteText;
}

void MRFileEditor::handleTextInput(TEvent &event) {
	if (mReadOnly) {
		clearEvent(event);
		return;
	}
	if ((event.keyDown.controlKeyState & kbPaste) != 0) {
		char buf[512];
		size_t length = 0;
		while (textEvent(event, TSpan<char>(buf, sizeof(buf)), length)) {
			const std::string insertedText(buf, length);
			if (insertBufferText(insertedText)) applyLiveSmartDedentAfterTextInput(insertedText);
		}
		applyLiveWordWrapAfterTextInput();
		clearEvent(event);
		return;
	}

	const ushort mods = event.keyDown.controlKeyState;
	const bool plainTab = event.keyDown.charScan.charCode == 9 && (mods & (kbShift | kbCtrlShift | kbAltShift | kbPaste)) == 0;
	std::string insertedText;

	if (plainTab)
		insertedText = tabKeyText();
	else if (event.keyDown.charScan.charCode >= 32 && event.keyDown.charScan.charCode < 255)
		insertedText.assign(1, static_cast<char>(event.keyDown.charScan.charCode));
	else
		insertedText.clear();
	if (insertedText.empty()) {
		clearEvent(event);
		return;
	}
	if (insertBufferText(insertedText)) applyLiveSmartDedentAfterTextInput(insertedText);
	applyLiveWordWrapAfterTextInput();
	clearEvent(event);
}

std::string MRFileEditor::tabKeyText() const {
	const MREditSetupSettings settings = effectiveEditSetupSettings();

	if (settings.tabExpand) return "\t";
	std::size_t insertPos = mBufferModel.cursor();
	if (mBufferModel.hasSelection()) insertPos = mBufferModel.selection().range().start;
	int visualColumn = freeCursorMovementEnabled() && insertPos == mBufferModel.cursor() && !mBufferModel.hasSelection() ? displayedCursorColumn() : charColumn(mBufferModel.lineStart(insertPos), insertPos);
	return std::string(static_cast<std::size_t>(tabDisplayWidth(settings, visualColumn)), ' ');
}

void MRFileEditor::handleEvent(TEvent &event) {
	if (event.what == evKeyDown) {
		const ushort mods = event.keyDown.controlKeyState;
		const bool shiftTabPressed = event.keyDown.keyCode == kbShiftTab || ((event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) && hasShiftModifier(mods));
		if (shiftTabPressed) {
			handleKeyDown(event);
			return;
		}
	}

	TScroller::handleEvent(event);

	if (event.what == evBroadcast) {
		if (event.message.command == cmScrollBarClicked && (event.message.infoPtr == hScrollBar || event.message.infoPtr == vScrollBar)) {
			select();
			clearEvent(event);
			return;
		}
		if (event.message.command == cmScrollBarChanged && (event.message.infoPtr == hScrollBar || event.message.infoPtr == vScrollBar)) {
			clearEvent(event);
			return;
		}
	}

	switch (event.what) {
		case evMouseDown:
			handleMouse(event);
			break;
		case evMouseWheel:
			static_cast<void>(scrollWindowByWheel(event.mouse.wheel));
			clearEvent(event);
			return;
			break;
		case evKeyDown:
			handleKeyDown(event);
			break;
		case evCommand:
			handleCommand(event);
			break;
		default:
			break;
	}
}

void MRFileEditor::scrollDraw() {
	int newDeltaX = hScrollBar != nullptr ? hScrollBar->value : 0;
	int newDeltaY = vScrollBar != nullptr ? vScrollBar->value : 0;

	if (newDeltaX != delta.x || newDeltaY != delta.y) {
		const int oldDeltaY = delta.y;
		const bool verticalMoved = newDeltaY != oldDeltaY;
		const int cursorRow = std::max(0, std::min(static_cast<int>(visibleLineForDocumentLine(cachedCursorLineIndex())) - oldDeltaY, std::max(1, visibleTextRows()) - 1));
		const int cursorColumn = displayedCursorColumn();

		delta.x = newDeltaX;
		delta.y = newDeltaY;
		if (verticalMoved) {
			const std::size_t targetVisibleLine = static_cast<std::size_t>(std::max(0, newDeltaY + cursorRow));
			const std::size_t targetDocumentLine = documentLineForVisibleLine(targetVisibleLine);
			const std::size_t targetOffset = charPtrOffset(mBufferModel.lineStartByIndex(targetDocumentLine), cursorColumn);
			moveCursor(targetOffset, false, false, cursorColumn);
			return;
		}
		if (useApproximateLargeFileMetrics()) updateMetrics();
		scheduleSyntaxWarmupIfNeeded();
		drawView();
	} else {
		if (useApproximateLargeFileMetrics()) updateMetrics();
		updateIndicator();
	}
}

void MRFileEditor::setState(ushort aState, Boolean enable) {
	TScroller::setState(aState, enable);
	if ((aState & (sfActive | sfSelected)) != 0) syncScrollBarsToState();
	MREditWindow *window = dynamic_cast<MREditWindow *>(owner);
	if (window != nullptr && window->isMinimized()) return;
	if (aState == sfCursorVis || mIndicatorUpdateInProgress) return;
	updateIndicator();
}

void MRFileEditor::handleKeyDown(TEvent &event) {
	ushort key = ctrlToArrow(event.keyDown.keyCode);
	const ushort mods = event.keyDown.controlKeyState;
	bool extend = hasShiftModifier(mods);
	int coalescedPageCount = 1;
	const bool shiftTabPressed = event.keyDown.keyCode == kbShiftTab || ((event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) && hasShiftModifier(mods));

	if (shiftTabPressed) {
		const std::size_t target = tabStopMoveOffset(cursorOffset(), false);
		if (target != cursorOffset()) setPreferredIndentColumn(charColumn(lineStartOffset(target), target) + 1);
		moveCursor(target, false, false);
		clearEvent(event);
		return;
	}

	if (isTextInputEvent(event)) {
		handleTextInput(event);
		return;
	}

	switch (key) {
		case kbLeft:
			if (freeCursorMovementEnabled() && !extend && !mBufferModel.hasSelection() && displayedCursorColumn() > actualCursorVisualColumn(cursorOffset()))
				moveCursor(cursorOffset(), false, false, displayedCursorColumn() - 1);
			else
				moveCursor(prevCharOffset(cursorOffset()), extend, false);
			break;
		case kbRight:
			if (freeCursorMovementEnabled() && !extend && !mBufferModel.hasSelection() && cursorOffset() == lineEndOffset(cursorOffset()))
				moveCursor(cursorOffset(), false, false, displayedCursorColumn() + 1);
			else
				moveCursor(nextCharOffset(cursorOffset()), extend, false);
			break;
		case kbUp:
			moveCursor(lineMoveOffset(cursorOffset(), -1, displayedCursorColumn()), extend, false, displayedCursorColumn());
			break;
		case kbDown:
			moveCursor(lineMoveOffset(cursorOffset(), 1, displayedCursorColumn()), extend, false, displayedCursorColumn());
			break;
		case kbHome:
			moveCursor(mAutoIndent ? charPtrOffset(lineStartOffset(cursorOffset()), 0) : lineStartOffset(cursorOffset()), extend, false);
			break;
		case kbEnd:
			moveCursor(lineEndOffset(cursorOffset()), extend, false);
			break;
		case kbPgUp:
		{
			static constexpr int maxCoalescedPages = 8;

			if (TApplication *app = dynamic_cast<TApplication *>(TProgram::application); app != nullptr) {
				while (coalescedPageCount < maxCoalescedPages) {
					TEvent queuedEvent;
					std::memset(&queuedEvent, 0, sizeof(queuedEvent));
					static_cast<TView *>(app)->getEvent(queuedEvent, 0);
					if (queuedEvent.what == evNothing) break;
					if (queuedEvent.what == evKeyDown && ctrlToArrow(queuedEvent.keyDown.keyCode) == kbPgUp && queuedEvent.keyDown.controlKeyState == mods) {
						++coalescedPageCount;
						continue;
					}
					app->putEvent(queuedEvent);
					break;
				}
			}
			moveCursor(lineMoveOffset(cursorOffset(), -(std::max(2, visibleTextRows()) - 1) * coalescedPageCount, displayedCursorColumn()), extend, true, displayedCursorColumn());
			break;
		}
		case kbPgDn:
		{
			static constexpr int maxCoalescedPages = 8;

			if (TApplication *app = dynamic_cast<TApplication *>(TProgram::application); app != nullptr) {
				while (coalescedPageCount < maxCoalescedPages) {
					TEvent queuedEvent;
					std::memset(&queuedEvent, 0, sizeof(queuedEvent));
					static_cast<TView *>(app)->getEvent(queuedEvent, 0);
					if (queuedEvent.what == evNothing) break;
					if (queuedEvent.what == evKeyDown && ctrlToArrow(queuedEvent.keyDown.keyCode) == kbPgDn && queuedEvent.keyDown.controlKeyState == mods) {
						++coalescedPageCount;
						continue;
					}
					app->putEvent(queuedEvent);
					break;
				}
			}
			moveCursor(lineMoveOffset(cursorOffset(), (std::max(2, visibleTextRows()) - 1) * coalescedPageCount, displayedCursorColumn()), extend, true, displayedCursorColumn());
			break;
		}
		case kbCtrlHome:
			moveCursor(0, false, false);
			break;
		case kbCtrlEnd:
			moveCursor(bufferLength(), false, false);
			break;
		case kbCtrlLeft:
			moveCursor(prevWordOffset(cursorOffset()), extend, false);
			break;
		case kbCtrlRight:
			moveCursor(nextWordOffset(cursorOffset()), extend, false);
			break;
		case kbEnter:
			if (!mReadOnly) newLineWithPreferredIndent();
			clearEvent(event);
			return;
		case kbBack:
			if (!mReadOnly) {
				if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
				else if (cursorOffset() > 0) replaceRangeAndSelect(static_cast<uint>(prevCharOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
			}
			clearEvent(event);
			return;
		case kbDel:
			if (!mReadOnly) {
				if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
				else
					deleteCharsAtCursor(1);
			}
			clearEvent(event);
			return;
		case kbIns:
			setInsertModeEnabled(!insertModeEnabled());
			clearEvent(event);
			return;
		case kbShiftIns:
			requestSystemClipboardPaste();
			clearEvent(event);
			return;
		case kbCtrlIns:
			if (mBufferModel.hasSelection()) copySelection();
			else
				static_cast<void>(handleMRCommand(cmMrEditCopyToBuffer));
			clearEvent(event);
			return;
		case kbShiftDel:
			if (mBufferModel.hasSelection()) cutSelection();
			else
				static_cast<void>(handleMRCommand(cmMrEditCutToBuffer));
			clearEvent(event);
			return;
		default:
			return;
	}
	clearEvent(event);
}

void MRFileEditor::handleCommand(TEvent &event) {
	switch (event.message.command) {
		case cmSave:
			saveInPlace();
			break;
		case cmSaveAs:
			saveAsWithPrompt();
			break;
		case cmCut:
			cutSelection();
			break;
		case cmCopy:
			copySelection();
			break;
		case cmPaste:
			requestSystemClipboardPaste();
			break;
		case cmUndo:
		case cmMrEditUndo: {
			MRTextBufferModel::CustomUndoRecord record;
			MRTextBufferModel::CustomUndoRecord redoBlockState;
			const MRTextBufferModel::ReadSnapshot oldSnapshot = mBufferModel.readSnapshot();
			const std::size_t oldLength = mBufferModel.length();
			const std::size_t oldVersion = mBufferModel.version();
			captureCurrentBlockStateForUndo(owner, redoBlockState);
			if (mBufferModel.undo(&record)) {
				mBufferModel.updateRedoTopBlockState(redoBlockState);
				const bool modifiedState = mBufferModel.isModified();
				const std::size_t newLength = mBufferModel.length();
				std::size_t prefix = 0;
				while (prefix < oldLength && prefix < newLength && oldSnapshot.charAt(prefix) == mBufferModel.charAt(prefix))
					++prefix;
				std::size_t oldSuffix = oldLength;
				std::size_t newSuffix = newLength;
				while (oldSuffix > prefix && newSuffix > prefix && oldSnapshot.charAt(oldSuffix - 1) == mBufferModel.charAt(newSuffix - 1)) {
					--oldSuffix;
					--newSuffix;
				}
				const std::size_t touchedLength = std::max(oldSuffix - prefix, newSuffix - prefix);
				MRTextBufferModel::DocumentChangeSet changeSet;
				changeSet.changed = true;
				changeSet.oldLength = oldLength;
				changeSet.newLength = newLength;
				changeSet.oldVersion = oldVersion;
				changeSet.newVersion = mBufferModel.version();
				changeSet.touchedRange = MRTextBufferModel::Range(prefix, prefix + touchedLength);
				adoptCommittedDocument(mBufferModel.document(), mBufferModel.cursor(), mBufferModel.selectionStart(), mBufferModel.selectionEnd(), modifiedState, &changeSet);
				applyRestoredBlockStateToOwner(*this, owner, record);
			}
			break;
		}
		case cmMrEditRedo: {
			MRTextBufferModel::CustomUndoRecord record;
			MRTextBufferModel::CustomUndoRecord undoBlockState;
			const MRTextBufferModel::ReadSnapshot oldSnapshot = mBufferModel.readSnapshot();
			const std::size_t oldLength = mBufferModel.length();
			const std::size_t oldVersion = mBufferModel.version();
			captureCurrentBlockStateForUndo(owner, undoBlockState);
			if (mBufferModel.redo(&record)) {
				mBufferModel.updateUndoTopBlockState(undoBlockState);
				const bool modifiedState = mBufferModel.isModified();
				const std::size_t newLength = mBufferModel.length();
				std::size_t prefix = 0;
				while (prefix < oldLength && prefix < newLength && oldSnapshot.charAt(prefix) == mBufferModel.charAt(prefix))
					++prefix;
				std::size_t oldSuffix = oldLength;
				std::size_t newSuffix = newLength;
				while (oldSuffix > prefix && newSuffix > prefix && oldSnapshot.charAt(oldSuffix - 1) == mBufferModel.charAt(newSuffix - 1)) {
					--oldSuffix;
					--newSuffix;
				}
				const std::size_t touchedLength = std::max(oldSuffix - prefix, newSuffix - prefix);
				MRTextBufferModel::DocumentChangeSet changeSet;
				changeSet.changed = true;
				changeSet.oldLength = oldLength;
				changeSet.newLength = newLength;
				changeSet.oldVersion = oldVersion;
				changeSet.newVersion = mBufferModel.version();
				changeSet.touchedRange = MRTextBufferModel::Range(prefix, prefix + touchedLength);
				adoptCommittedDocument(mBufferModel.document(), mBufferModel.cursor(), mBufferModel.selectionStart(), mBufferModel.selectionEnd(), modifiedState, &changeSet);
				applyRestoredBlockStateToOwner(*this, owner, record);
			}
			break;
		}
		case cmMrTextUpperCaseMenu:
			convertSelectionToUpperCase();
			break;
		case cmMrTextLowerCaseMenu:
			convertSelectionToLowerCase();
			break;
		case cmMrTextCenterLine:
			if (!mReadOnly) {
				MREditSetupSettings settings = effectiveEditSetupSettings();
				centerCurrentLine(settings.leftMargin, settings.rightMargin > 0 ? settings.rightMargin : 78);
			}
			break;
		case cmMrTextReformatParagraph:
			if (!mReadOnly) {
				MREditSetupSettings settings = effectiveEditSetupSettings();
				formatParagraph(settings.leftMargin, settings.rightMargin > 0 ? settings.rightMargin : 78);
			}
			break;
		case cmClear:
			if (!mReadOnly) replaceSelectionText(std::string());
			break;
		case cmCharLeft:
			if (freeCursorMovementEnabled() && !mBufferModel.hasSelection() && displayedCursorColumn() > actualCursorVisualColumn(cursorOffset()))
				moveCursor(cursorOffset(), false, false, displayedCursorColumn() - 1);
			else
				moveCursor(prevCharOffset(cursorOffset()), false, false);
			break;
		case cmCharRight:
			if (freeCursorMovementEnabled() && !mBufferModel.hasSelection() && cursorOffset() == lineEndOffset(cursorOffset()))
				moveCursor(cursorOffset(), false, false, displayedCursorColumn() + 1);
			else
				moveCursor(nextCharOffset(cursorOffset()), false, false);
			break;
		case cmWordLeft:
			moveCursor(prevWordOffset(cursorOffset()), false, false);
			break;
		case cmWordRight:
			moveCursor(nextWordOffset(cursorOffset()), false, false);
			break;
		case cmLineStart:
			moveCursor(lineStartOffset(cursorOffset()), false, false);
			break;
		case cmLineEnd:
			moveCursor(lineEndOffset(cursorOffset()), false, false);
			break;
		case cmLineUp:
			moveCursor(lineMoveOffset(cursorOffset(), -1, displayedCursorColumn()), false, false, displayedCursorColumn());
			break;
		case cmLineDown:
			moveCursor(lineMoveOffset(cursorOffset(), 1, displayedCursorColumn()), false, false, displayedCursorColumn());
			break;
		case cmPageUp:
			moveCursor(lineMoveOffset(cursorOffset(), -(std::max(2, visibleTextRows()) - 1), displayedCursorColumn()), false, true, displayedCursorColumn());
			break;
		case cmPageDown:
			moveCursor(lineMoveOffset(cursorOffset(), std::max(2, visibleTextRows()) - 1, displayedCursorColumn()), false, true, displayedCursorColumn());
			break;
		case cmTextStart:
			moveCursor(0, false, false);
			break;
		case cmTextEnd:
			moveCursor(bufferLength(), false, false);
			break;
		case cmNewLine:
			if (!mReadOnly) newLineWithPreferredIndent();
			break;
		case cmBackSpace:
			if (!mReadOnly) {
				if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
				else if (cursorOffset() > 0) replaceRangeAndSelect(static_cast<uint>(prevCharOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
			}
			break;
		case cmDelChar:
			if (!mReadOnly) {
				if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
				else
					deleteCharsAtCursor(1);
			}
			break;
		case cmDelWord:
			if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(cursorOffset()), static_cast<uint>(nextWordOffset(cursorOffset())), "", 0);
			break;
		case cmDelWordLeft:
			if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(prevWordOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
			break;
		case cmDelStart:
			if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(lineStartOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
			break;
		case cmDelEnd:
			if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(cursorOffset()), static_cast<uint>(lineEndOffset(cursorOffset())), "", 0);
			break;
		case cmDelLine:
			if (!mReadOnly) deleteCurrentLineText();
			break;
		case cmInsMode:
			setInsertModeEnabled(!insertModeEnabled());
			break;
		case cmSelectAll:
			mSelectionAnchor = 0;
			mBufferModel.setCursorAndSelection(mBufferModel.length(), 0, mBufferModel.length());
			revealCursor(True);
			break;
		default:
			return;
	}
	clearEvent(event);
}

void MRFileEditor::handleMouse(TEvent &event) {
	const TextViewportGeometry viewport = textViewportGeometry();
	const TPoint local = makeLocal(event.mouse.where);
	std::size_t foldLineIndex = 0;
	mMouseSelectionColumnsValid = false;
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
		const std::vector<MRFoldSpan> &visibleSpans = mFoldState.visibleState().spans;
		std::map<std::size_t, MRFoldSpan> &closedFoldSpans = mFoldState.closedFoldSpans();
		const int displayColumn = local.x - viewport.codeFoldingX;
		if (displayColumn < 0 || static_cast<std::size_t>(displayColumn) >= displayLevels.size()) return false;
		const unsigned short level = displayLevels[static_cast<std::size_t>(displayColumn)];
		bool anyOpen = false;
		for (const MRFoldSpan &span : visibleSpans)
			if (span.level >= level && span.open) {
				anyOpen = true;
				break;
			}
		bool changed = false;
		std::size_t cursorLine = mBufferModel.lineIndex(mBufferModel.cursor());
		std::size_t foldCursorTarget = cursorLine;
		bool foldCursorTargetValid = false;

		for (const MRFoldSpan &span : visibleSpans) {
			if (anyOpen) {
				if (span.level < level) continue;
				if (!span.open) continue;
				closedFoldSpans[span.startLine] = MRFoldSpan(span.startLine, span.endLine, span.level, span.sourceKind, false, span.siblingContinuation);
				if (cursorLine > span.startLine && cursorLine <= span.endLine && (!foldCursorTargetValid || span.startLine < foldCursorTarget)) {
					foldCursorTarget = span.startLine;
					foldCursorTargetValid = true;
				}
			} else {
				if (span.level != level) continue;
				if (span.open) continue;
				closedFoldSpans.erase(span.startLine);
			}
			changed = true;
		}
		if (!changed) return false;
		mFoldState.rebuildEffectiveClosedFolds();
		if (foldCursorTargetValid) moveCursor(mBufferModel.lineStartByIndex(foldCursorTarget), false, false);
		if (mFoldState.warmupState().taskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mFoldState.warmupState().taskId));
			clearFoldWarmupTask(mFoldState.warmupState().taskId);
		}
		invalidateFoldCache(true);
		updateMetrics();
		drawView();
		updateIndicator();
		return true;
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

	select();
	int targetColumn = 0;
	std::size_t anchor = mouseOffset(local, &targetColumn);
	const bool leftButton = (event.mouse.buttons & mbLeftButton) != 0;
	const bool rightButton = (event.mouse.buttons & mbRightButton) != 0;
	const bool mouseCtrl = (event.mouse.controlKeyState & kbCtrlShift) != 0;
	const bool mouseAlt = (event.mouse.controlKeyState & kbAltShift) != 0;
	const bool liveLineBlock = (mouseCtrl && mouseAlt) || (leftButton && rightButton);
	const bool liveColumnBlock = !liveLineBlock && (mouseAlt || (rightButton && !leftButton));
	const bool liveStreamBlock = !liveLineBlock && !liveColumnBlock && (leftButton || rightButton || mouseCtrl);
	const int liveBlockMode = liveColumnBlock ? 2 : liveLineBlock ? 1 : liveStreamBlock ? 3 : 0;
	const unsigned short liveSelectionModifiers = liveLineBlock ? static_cast<unsigned short>(kbCtrlShift | kbAltShift) : liveColumnBlock ? kbAltShift : liveStreamBlock ? kbCtrlShift : 0;
	const bool explicitBlockMouseGesture = liveBlockMode != 0 && (mouseCtrl || mouseAlt || (leftButton && rightButton) || (rightButton && !leftButton));
	if (columnBlockTraceEnabled() && liveColumnBlock) {
		std::ostringstream trace;
		trace << "start local=(" << local.x << ',' << local.y << ") anchor=" << anchor << " anchorLine=" << mBufferModel.lineIndex(anchor) << " targetColumn=" << targetColumn
		      << " buttons=" << event.mouse.buttons << " mods=" << event.mouse.controlKeyState << " delta=(" << delta.x << ',' << delta.y << ") rows=" << visibleTextRows()
		      << " lineCount=" << mBufferModel.lineCount() << " length=" << mBufferModel.length();
		appendColumnBlockTrace(trace.str());
	}
	mMouseSelectionModifiers = 0;
	auto updateLiveMouseBlockOverlay = [this, liveBlockMode](std::size_t current) {
		if (liveBlockMode == 0) return;
		std::size_t visualAnchor = mSelectionAnchor;
		std::size_t visualEnd = current;
		if (liveBlockMode == 1) {
			const std::size_t length = mBufferModel.length();

			if (visualAnchor == length && length > 0) --visualAnchor;
			if (visualEnd == length && length > 0) --visualEnd;
			else if (visualEnd > 0 && lineStartOffset(visualEnd) == visualEnd && lineEndOffset(visualEnd) == visualEnd)
				--visualEnd;
		}
		setBlockOverlayState(liveBlockMode, visualAnchor, visualEnd, true, false, mMouseSelectionAnchorColumn, mMouseSelectionCursorColumn);
		if (columnBlockTraceEnabled() && liveBlockMode == 2) {
			std::ostringstream trace;
			trace << "overlay anchor=" << mSelectionAnchor << " current=" << current << " visualAnchor=" << visualAnchor << " visualEnd=" << visualEnd
			      << " anchorColumn=" << mMouseSelectionAnchorColumn << " cursorColumn=" << mMouseSelectionCursorColumn;
			appendColumnBlockTrace(trace.str());
		}
	};
	mSelectionAnchor = anchor;
	mMouseSelectionColumnsValid = true;
	mMouseSelectionAnchorColumn = targetColumn;
	mMouseSelectionCursorColumn = targetColumn;
	auto setMouseSelectionColumns = [this, liveColumnBlock, targetColumn](int cursorColumn, bool activeSelection) {
		mMouseSelectionAnchorColumn = targetColumn;
		mMouseSelectionCursorColumn = cursorColumn;
		if (!activeSelection || !liveColumnBlock) return;
		if (cursorColumn < targetColumn) mMouseSelectionAnchorColumn = targetColumn + 1;
		else
			mMouseSelectionCursorColumn = cursorColumn + 1;
	};
	mBufferModel.setCursorAndSelection(anchor, anchor, anchor);
	if (freeCursorMovementEnabled()) mCursorVisualColumn = std::max(actualCursorVisualColumn(anchor), targetColumn);
	else
		mCursorVisualColumn = actualCursorVisualColumn(anchor);
	updateIndicator();
	if (!explicitBlockMouseGesture) drawView();

	MREditorApp *app = dynamic_cast<MREditorApp *>(TProgram::application);
	if (app != nullptr) app->beginInteractiveMouseCapture();
	bool dragged = false;
	while (mouseEvent(event, evMouseMove | evMouseAuto | evMouseWheel)) {
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
		int dragColumn = 0;
		const TPoint dragLocal = makeLocal(event.mouse.where);
		std::size_t target = mouseOffset(dragLocal, &dragColumn);
		if (target != anchor || dragColumn != targetColumn) {
			dragged = true;
			mMouseSelectionModifiers = liveSelectionModifiers;
		}
		setMouseSelectionColumns(dragColumn, dragged);
		mBufferModel.setCursorAndSelection(target, mSelectionAnchor, target);
		if (freeCursorMovementEnabled()) mCursorVisualColumn = std::max(actualCursorVisualColumn(target), dragColumn);
		else
			mCursorVisualColumn = actualCursorVisualColumn(target);
		if (dragged) updateLiveMouseBlockOverlay(target);
		if (columnBlockTraceEnabled() && liveColumnBlock) {
			std::ostringstream trace;
			trace << mouseEventName(event.what) << " local=(" << dragLocal.x << ',' << dragLocal.y << ") target=" << target << " targetLine=" << mBufferModel.lineIndex(target)
			      << " dragColumn=" << dragColumn << " dragged=" << dragged << " anchorColumn=" << mMouseSelectionAnchorColumn
			      << " cursorColumn=" << mMouseSelectionCursorColumn << " selectionAnchor=" << mSelectionAnchor << " cursor=" << mBufferModel.cursor()
			      << " delta=(" << delta.x << ',' << delta.y << ')';
			appendColumnBlockTrace(trace.str());
		}
		updateIndicator();
		drawView();
	}
	if (event.what == evMouseUp) {
		int dragColumn = 0;
		const TPoint dragLocal = makeLocal(event.mouse.where);
		std::size_t target = mouseOffset(dragLocal, &dragColumn);
		if (target != anchor || dragColumn != targetColumn) {
			dragged = true;
			mMouseSelectionModifiers = liveSelectionModifiers;
		}
		setMouseSelectionColumns(dragColumn, dragged);
		mBufferModel.setCursorAndSelection(target, mSelectionAnchor, target);
		if (freeCursorMovementEnabled()) mCursorVisualColumn = std::max(actualCursorVisualColumn(target), dragColumn);
		else
			mCursorVisualColumn = actualCursorVisualColumn(target);
		if (dragged) updateLiveMouseBlockOverlay(target);
		if (columnBlockTraceEnabled() && liveColumnBlock) {
			std::ostringstream trace;
			trace << mouseEventName(event.what) << " local=(" << dragLocal.x << ',' << dragLocal.y << ") target=" << target << " targetLine=" << mBufferModel.lineIndex(target)
			      << " dragColumn=" << dragColumn << " dragged=" << dragged << " anchorColumn=" << mMouseSelectionAnchorColumn
			      << " cursorColumn=" << mMouseSelectionCursorColumn << " selectionAnchor=" << mSelectionAnchor << " cursor=" << mBufferModel.cursor()
			      << " mods=" << mMouseSelectionModifiers << " delta=(" << delta.x << ',' << delta.y << ')';
			appendColumnBlockTrace(trace.str());
		}
		updateIndicator();
		drawView();
	}
	if (app != nullptr) app->endInteractiveMouseCapture();
	if (!dragged) {
		mMouseSelectionModifiers = 0;
		mMouseSelectionColumnsValid = false;
	}
	clearEvent(event);
}
