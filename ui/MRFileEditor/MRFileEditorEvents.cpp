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

bool textCanTriggerSmartDedent(std::string_view text) noexcept {
	for (const char value : text)
		if (value != ' ' && value != '\t') return true;
	return false;
}

}

bool MRFileEditor::hasShiftModifier(ushort mods) noexcept {
	return (mods & kbShift) != 0;
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
	record.preSnapshot.compactLineIndexForUndo(mBufferModel.cursor());
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

bool MRFileEditor::revertUndoSuffix(std::size_t baseDepth) {
	MRTextBufferModel::CustomUndoRecord record;
	const std::size_t oldLength = mBufferModel.length();
	const std::size_t oldVersion = mBufferModel.version();

	if (!mBufferModel.revertUndoSuffix(baseDepth, &record)) return false;

	MRTextBufferModel::DocumentChangeSet changeSet;
	changeSet.changed = true;
	changeSet.oldLength = oldLength;
	changeSet.newLength = mBufferModel.length();
	changeSet.oldVersion = oldVersion;
	changeSet.newVersion = mBufferModel.version();
	changeSet.touchedRange = MRTextBufferModel::Range(0, std::max(oldLength, mBufferModel.length()));
	syncAfterCommittedDocument(mBufferModel.cursor(), mBufferModel.selectionStart(), mBufferModel.selectionEnd(), mBufferModel.isModified(), &changeSet);
	applyRestoredBlockStateToOwner(*this, owner, record);
	return true;
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
			if (insertBufferText(insertedText) && textCanTriggerSmartDedent(insertedText)) applyLiveSmartDedentAfterTextInput(insertedText);
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
	char pairedCloser = '\0';
	const bool autoPairBrackets = effectiveEditSetupSettings().autoPairBrackets && insertModeEnabled() && !mBufferModel.hasSelection() && insertedText.size() == 1;
	if (autoPairBrackets) {
		switch (insertedText[0]) {
			case '(': pairedCloser = ')'; break;
			case '[': pairedCloser = ']'; break;
			case '{': pairedCloser = '}'; break;
			case ')':
			case ']':
			case '}':
				if (cursorOffset() < bufferLength() && charAtOffset(cursorOffset()) == insertedText[0]) {
					moveCursor(nextCharOffset(cursorOffset()), false, false);
					if (textCanTriggerSmartDedent(insertedText)) applyLiveSmartDedentAfterTextInput(insertedText);
					applyLiveWordWrapAfterTextInput();
					clearEvent(event);
					return;
				}
				break;
			case '"':
			case '\'':
				if (cursorOffset() < bufferLength() && charAtOffset(cursorOffset()) == insertedText[0]) {
					moveCursor(nextCharOffset(cursorOffset()), false, false);
					applyLiveWordWrapAfterTextInput();
					clearEvent(event);
					return;
				}
				pairedCloser = insertedText[0];
				break;
			default: break;
		}
		if (pairedCloser != '\0') insertedText.push_back(pairedCloser);
	}
	if (insertBufferText(insertedText)) {
		if (pairedCloser != '\0') moveCursor(prevCharOffset(cursorOffset()), false, false);
		if (textCanTriggerSmartDedent(insertedText)) applyLiveSmartDedentAfterTextInput(insertedText);
	}
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
		if (verticalMoved && drawLock == 0) {
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
	if (columnBlockTraceEnabled() && mLineDrawingEnabled && (key == kbLeft || key == kbRight || key == kbUp || key == kbDown)) {
		std::ostringstream trace;
		trace << "ld-key key=0x" << std::hex << key << std::dec << " raw=0x" << std::hex << event.keyDown.keyCode << std::dec << " mods=" << mods << " extend=" << extend
		      << " cursorLine=" << displayedCursorLineIndex() << " cursorColumn=" << displayedCursorColumn();
		appendColumnBlockTrace(trace.str());
	}
	if (mLineDrawingEnabled && extend && (key == kbLeft || key == kbRight || key == kbUp || key == kbDown)) {
		static_cast<void>(drawLineDrawingCursorMotion(key));
		clearEvent(event);
		return;
	}

	switch (key) {
		case kbLeft:
			if (freeCursorVirtualMovementAllowed() && !extend && displayedCursorColumn() > actualCursorVisualColumn(cursorOffset()))
				moveCursor(cursorOffset(), false, false, displayedCursorColumn() - 1);
			else
				moveCursor(prevCharOffset(cursorOffset()), extend, false);
			break;
		case kbRight:
			if (freeCursorVirtualMovementAllowed() && !extend && cursorOffset() == lineEndOffset(cursorOffset()))
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
		case kbDel: {
			const ushort command = key == kbBack ? cmBackSpace : cmDelChar;
			event.what = evCommand;
			event.message.command = command;
			handleCommand(event);
			return;
		}
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
			const std::size_t oldLength = mBufferModel.length();
			const std::size_t oldVersion = mBufferModel.version();
			captureCurrentBlockStateForUndo(owner, redoBlockState);
			if (mBufferModel.undo(&record)) {
				mBufferModel.updateRedoTopBlockState(redoBlockState);
				const bool modifiedState = mBufferModel.isModified();
				const std::size_t newLength = mBufferModel.length();
				MRTextBufferModel::DocumentChangeSet changeSet = record.changeSet;
				changeSet.changed = true;
				changeSet.oldLength = oldLength;
				changeSet.newLength = newLength;
				changeSet.oldVersion = oldVersion;
				changeSet.newVersion = mBufferModel.version();
				if (!record.changeSet.changed) changeSet.touchedRange = MRTextBufferModel::Range(0, std::max(oldLength, newLength));
				adoptCommittedDocument(mBufferModel.document(), mBufferModel.cursor(), mBufferModel.selectionStart(), mBufferModel.selectionEnd(), modifiedState, &changeSet);
				applyRestoredBlockStateToOwner(*this, owner, record);
			}
			break;
		}
		case cmMrEditRedo: {
			MRTextBufferModel::CustomUndoRecord record;
			MRTextBufferModel::CustomUndoRecord undoBlockState;
			const std::size_t oldLength = mBufferModel.length();
			const std::size_t oldVersion = mBufferModel.version();
			captureCurrentBlockStateForUndo(owner, undoBlockState);
			if (mBufferModel.redo(&record)) {
				mBufferModel.updateUndoTopBlockState(undoBlockState);
				const bool modifiedState = mBufferModel.isModified();
				const std::size_t newLength = mBufferModel.length();
				MRTextBufferModel::DocumentChangeSet changeSet = record.changeSet;
				changeSet.changed = true;
				changeSet.oldLength = oldLength;
				changeSet.newLength = newLength;
				changeSet.oldVersion = oldVersion;
				changeSet.newVersion = mBufferModel.version();
				if (!record.changeSet.changed) changeSet.touchedRange = MRTextBufferModel::Range(0, std::max(oldLength, newLength));
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
		case cmMrTextPrettifyBlockOrFile:
			if (!mReadOnly) prettifyBlockOrFile();
			break;
		case cmClear:
			if (!mReadOnly) replaceSelectionText(std::string());
			break;
		case cmCharLeft:
			if (freeCursorVirtualMovementAllowed() && displayedCursorColumn() > actualCursorVisualColumn(cursorOffset()))
				moveCursor(cursorOffset(), false, false, displayedCursorColumn() - 1);
			else
				moveCursor(prevCharOffset(cursorOffset()), false, false);
			break;
		case cmCharRight:
			if (freeCursorVirtualMovementAllowed() && cursorOffset() == lineEndOffset(cursorOffset()))
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
		case cmDelChar:
			if (!mReadOnly) {
				const bool backward = event.message.command == cmBackSpace;
				if (backward && freeCursorVirtualMovementAllowed() && displayedCursorColumn() > actualCursorVisualColumn(cursorOffset())) {
					moveCursor(cursorOffset(), false, false, displayedCursorColumn() - 1);
					break;
				}
				MREditWindow *window = dynamic_cast<MREditWindow *>(owner);
				if (window != nullptr && window->deleteBlockForEditorInput(backward)) break;
				if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
				else if (backward) {
					if (cursorOffset() > 0) replaceRangeAndSelect(static_cast<uint>(prevCharOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
				} else
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
