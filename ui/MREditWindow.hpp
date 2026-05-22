#ifndef MREDITWINDOW_HPP
#define MREDITWINDOW_HPP

#define Uses_TWindow
#define Uses_TScrollBar
#define Uses_TIndicator
#define Uses_TFileEditor
#define Uses_TRect
#define Uses_TEvent
#define Uses_TEditor
#define Uses_TObject
#define Uses_TProgram
#include <tvision/tv.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <unistd.h>
#include <vector>

#include "MRFrame.hpp"
#include "MRIndicator.hpp"
#include "MRTextBuffer.hpp"
#include "MRWindowManager.hpp"
#include "MRWindowManager.hpp"
#include "MRWindowSupport.hpp"
#include "../app/MRCommands.hpp"
#include "../keymap/MRKeymapContext.hpp"
#include "../keymap/MRKeymapToken.hpp"
#include "../dialogs/MRWindowList.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../mrmac/MRVM.hpp"

void mrTraceCoprocessorTaskCancel(int bufferId, std::uint64_t taskId);
class MREditWindow;
void setWindowManuallyHidden(MREditWindow *win, bool hidden);
void mrDropSidekickForParent(const MREditWindow *parent);

class MREditWindow : public TWindow {
	friend class MRWindowManager;

  public:
	struct TrackedTask {
		std::uint64_t id;
		mr::coprocessor::TaskKind kind;
		std::string label;
		std::chrono::steady_clock::time_point startedAt;

		TrackedTask() noexcept : id(0), kind(mr::coprocessor::TaskKind::Custom), label(), startedAt(std::chrono::steady_clock::now()) {
		}

		TrackedTask(std::uint64_t aId, mr::coprocessor::TaskKind aKind, std::string aLabel = std::string()) : id(aId), kind(aKind), label(std::move(aLabel)), startedAt(std::chrono::steady_clock::now()) {
		}
	};

	enum WindowRole {
		wrText = 0,
		wrFile,
		wrCommunicationCommand,
		wrCommunicationPipe,
		wrCommunicationDevice,
		wrLog,
		wrHelp
	};

	MREditWindow(const TRect &bounds, const char *title, int aNumber) : TWindowInit(&MREditWindow::initFrame), TWindow(bounds, 0, aNumber), vScrollBar(nullptr), hScrollBar(nullptr), indicator(nullptr), editor(nullptr), mBufferId(allocateBufferId()), mFirstSaveDone(false), mTemporaryFileUsed(false), mTemporaryFileName(), mIndentLevel(1), mBlockMode(bmNone), mBlockMarkingOn(false), mBlockAnchor(0), mBlockEnd(0), mColumnSortAscending(true), mTrackedCoprocessorTasks(), mWindowRole(wrText), mWindowRoleDetail(), mMacroQueuedCount(0), mMacroCompletedCount(0), mMacroConflictCount(0), mMacroCancelledCount(0), mMacroFailedCount(0), mLastMacroSummaryText(), mWindowPaletteData(defaultWindowPaletteData()), mWindowPalette(mWindowPaletteData.data(), static_cast<ushort>(mWindowPaletteData.size())), mCustomEofMarkerColorValid(false), mCustomEofMarkerColor(0), mClosePrepared(false), mMinimized(false), mBufferedBeforeMinimize(false), mRestoreBounds(bounds), mLastMinimizedBounds(0, 0, 0, 0) {
		options |= ofTileable;

		std::strncpy(displayTitle, (title != nullptr && *title != '\0') ? title : "Untitled", sizeof(displayTitle) - 1);
		displayTitle[sizeof(displayTitle) - 1] = '\0';

		hScrollBar = new TScrollBar(TRect(1, size.y - 1, size.x - 1, size.y));
		hScrollBar->hide();
		insert(hScrollBar);

		vScrollBar = new TScrollBar(TRect(size.x - 1, 1, size.x, size.y - 1));
		vScrollBar->hide();
		insert(vScrollBar);

		indicator = new MRIndicator(TRect(2, size.y - 1, 38, size.y));
		indicator->hide();
		insert(indicator);
		if (frame != nullptr) {
			MRFrame *mrFrame = static_cast<MRFrame *>(frame);
			mrFrame->setMarkerStateProvider([this]() {
				const bool hasInsertSlot = indicator != nullptr && indicator->hasInsertMarkerSlot();
				const bool showInsertIcon = indicator != nullptr && indicator->shouldDrawInsertMarker();
				const bool hasWordWrapSlot = indicator != nullptr && indicator->hasWordWrapMarkerSlot();
				const bool showWordWrapIcon = indicator != nullptr && indicator->shouldDrawWordWrapMarker();
				const bool hasTaskSlot = indicator != nullptr && indicator->hasTaskMarkerSlot();
				const bool showTaskIcon = indicator != nullptr && indicator->shouldDrawTaskMarker();
				const bool hasReadOnlySlot = indicator != nullptr && indicator->hasReadOnlyMarkerSlot();
				const bool showReadOnlyIcon = indicator != nullptr && indicator->shouldDrawReadOnlyMarker();
				const bool showLanguageSlot = editor != nullptr && editor->syntaxLanguageAutomatic() && editor->syntaxLanguage() != MRSyntaxLanguage::PlainText;
				const char *languageMarker = showLanguageSlot ? tmrSyntaxLanguageMarker(editor->syntaxLanguage()) : nullptr;
				const std::uint32_t languageMarkerRgb = showLanguageSlot ? tmrSyntaxLanguageMarkerRgb(editor->syntaxLanguage()) : 0;
				const bool isActiveWindow = (this->state & sfActive) != 0;
				const bool showRecordingSlot = isActiveWindow && mrIsKeystrokeRecordingActive();
				const bool showRecordingIcon = showRecordingSlot && mrIsKeystrokeRecordingMarkerVisible();
				const bool showMacroBrainSlot = isActiveWindow && mrIsMacroBrainMarkerActive();
				const bool showMacroBrainIcon = showMacroBrainSlot && mrIsMacroBrainMarkerVisible();
				return MRFrame::MarkerState(isFileChanged(), hasInsertSlot, showInsertIcon, hasWordWrapSlot, showWordWrapIcon, hasTaskSlot, showTaskIcon, hasReadOnlySlot, showReadOnlyIcon, showRecordingSlot, showRecordingIcon, showMacroBrainSlot, showMacroBrainIcon, showLanguageSlot, showLanguageSlot, languageMarker, languageMarkerRgb);
			});
			mrFrame->setTaskOverviewProvider([this]() { return describeRunningTasks(); });
		}

		TRect r(getExtent());
		r.grow(-1, -1);

		editor = createEditor(r, "");
		insert(editor);
		resetWindowColorsToConfiguredDefaults();
		refreshSyntaxContext();
	}

	virtual ~MREditWindow() override {
		mrDropSidekickForParent(this);
		{
			std::ofstream out("misc/mr.log", std::ios::out | std::ios::app | std::ios::binary);
			if (out) {
				std::time_t now = std::time(nullptr);
				std::tm *tmNow = std::localtime(&now);
				char buffer[32];
				if (tmNow != nullptr && std::strftime(buffer, sizeof(buffer), "%H:%M:%S", tmNow) != 0) out << "[" << buffer << "] ";
				else
					out << "[--:--:--] ";
				out << "MREditWindow destructor begin #" << mBufferId << " title='" << (getTitle(0) != nullptr ? getTitle(0) : "?") << "' editor_present=" << (editor != nullptr ? 1 : 0) << ".\n";
				out.flush();
			}
		}
		prepareForClose();
		mrNotifyWindowTopologyChanged();
		{
			std::ofstream out("misc/mr.log", std::ios::out | std::ios::app | std::ios::binary);
			if (out) {
				std::time_t now = std::time(nullptr);
				std::tm *tmNow = std::localtime(&now);
				char buffer[32];
				if (tmNow != nullptr && std::strftime(buffer, sizeof(buffer), "%H:%M:%S", tmNow) != 0) out << "[" << buffer << "] ";
				else
					out << "[--:--:--] ";
				out << "MREditWindow destructor end #" << mBufferId << ".\n";
				out.flush();
			}
		}
	}

	virtual void close() override {
		TWindow::close();
	}

	virtual Boolean valid(ushort command) override {
		if (command != cmClose) return TWindow::valid(command);
		const auto closeValidStartedAt = std::chrono::steady_clock::now();
		{
			std::ofstream out("misc/mr.log", std::ios::out | std::ios::app | std::ios::binary);
			if (out) {
				std::time_t now = std::time(nullptr);
				std::tm *tmNow = std::localtime(&now);
				char buffer[32];
				if (tmNow != nullptr && std::strftime(buffer, sizeof(buffer), "%H:%M:%S", tmNow) != 0) out << "[" << buffer << "] ";
				else
					out << "[--:--:--] ";
				out << "Phase1 discard window valid begin #" << mBufferId << " title='" << (getTitle(0) != nullptr ? getTitle(0) : "?") << "'.\n";
				out.flush();
			}
		}
		if (!TWindow::valid(command)) {
			std::ofstream out("misc/mr.log", std::ios::out | std::ios::app | std::ios::binary);
			if (out) {
				std::time_t now = std::time(nullptr);
				std::tm *tmNow = std::localtime(&now);
				char buffer[32];
				if (tmNow != nullptr && std::strftime(buffer, sizeof(buffer), "%H:%M:%S", tmNow) != 0) out << "[" << buffer << "] ";
				else
					out << "[--:--:--] ";
				out << "Phase1 discard window valid editor rejected #" << mBufferId << " total_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - closeValidStartedAt).count() << ".\n";
				out.flush();
			}
			return False;
		}
		{
			std::ofstream out("misc/mr.log", std::ios::out | std::ios::app | std::ios::binary);
			if (out) {
				std::time_t now = std::time(nullptr);
				std::tm *tmNow = std::localtime(&now);
				char buffer[32];
				if (tmNow != nullptr && std::strftime(buffer, sizeof(buffer), "%H:%M:%S", tmNow) != 0) out << "[" << buffer << "] ";
				else
					out << "[--:--:--] ";
				out << "Phase1 discard window valid editor accepted #" << mBufferId << " total_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - closeValidStartedAt).count() << ".\n";
				out.flush();
			}
		}
		scheduleEnsureUsableWorkWindow();
		prepareForClose();
		{
			std::ofstream out("misc/mr.log", std::ios::out | std::ios::app | std::ios::binary);
			if (out) {
				std::time_t now = std::time(nullptr);
				std::tm *tmNow = std::localtime(&now);
				char buffer[32];
				if (tmNow != nullptr && std::strftime(buffer, sizeof(buffer), "%H:%M:%S", tmNow) != 0) out << "[" << buffer << "] ";
				else
					out << "[--:--:--] ";
				out << "Phase1 discard window valid end #" << mBufferId << " total_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - closeValidStartedAt).count() << ".\n";
				out.flush();
			}
		}
		return True;
	}

	virtual TPalette &getPalette() const override {
		return mWindowPalette;
	}

	virtual TColorAttr mapColor(uchar index) override {
		if (index >= 1 && index <= mWindowPaletteData.size()) return mWindowPaletteData[index - 1];
		return TWindow::mapColor(index);
	}

	virtual const char *getTitle(short) override {
		if (editor != nullptr && editor->hasPersistentFileName()) return editor->persistentFileName();
		return displayTitle;
	}

	void setState(ushort aState, Boolean enable) override {
		TWindow::setState(aState, enable);
		if (MRWindowManager::isWindowMinimized(this)) {
			layoutEditorChrome();
			if ((aState & (sfFocused | sfSelected | sfActive)) != 0 && frame != nullptr) frame->drawView();
			return;
		}
		if ((aState & (sfFocused | sfSelected | sfActive)) != 0 && frame != nullptr) {
			frame->drawView();
			if (hScrollBar != nullptr) hScrollBar->drawView();
			if (vScrollBar != nullptr) vScrollBar->drawView();
			if (indicator != nullptr) indicator->drawView();
		}
	}

	void dragView(TEvent &event, uchar mode, TRect &limits, TPoint minSize, TPoint maxSize) override {
		MRWindowManager::handleDragView(this, event, mode, limits, minSize, maxSize);
	}

	void sizeLimits(TPoint &minSize, TPoint &maxSize) override {
		TWindow::sizeLimits(minSize, maxSize);
		if (MRWindowManager::isWindowMinimized(this)) {
			minSize.x = MRWindowManager::minimizedWindowWidth(this);
			minSize.y = 1;
			maxSize = minSize;
			return;
		}
		const TRect usableBounds = MRWindowManager::usableDesktopBounds();
		const int usableWidth = std::max(1, usableBounds.b.x - usableBounds.a.x);
		const int usableHeight = std::max(1, usableBounds.b.y - usableBounds.a.y);
		maxSize.x = std::min<int>(maxSize.x, usableWidth);
		maxSize.y = std::min<int>(maxSize.y, usableHeight);
		if (minSize.x > maxSize.x) minSize.x = maxSize.x;
		if (minSize.y > maxSize.y) minSize.y = maxSize.y;
	}

	void changeBounds(const TRect &bounds) override {
		mrDropSidekickForParent(this);
		TWindow::changeBounds(bounds);
		layoutEditorChrome();
		if (MRWindowManager::isWindowMinimized(this)) {
			if (frame != nullptr) frame->drawView();
			return;
		}
		if (hScrollBar != nullptr) hScrollBar->drawView();
		if (vScrollBar != nullptr) vScrollBar->drawView();
		if (indicator != nullptr) indicator->drawView();
	}

	bool editorScrollBarArrowPart(TScrollBar *scrollBar, TPoint global, int &part) const {
		if (scrollBar == nullptr || (scrollBar->state & sfVisible) == 0) return false;

		TPoint local = scrollBar->makeLocal(global);
		if (local.x < 0 || local.y < 0 || local.x >= scrollBar->size.x || local.y >= scrollBar->size.y) return false;

		if (scrollBar->size.x == 1) {
			if (local.y == 0) {
				part = sbUpArrow;
				return true;
			}
			if (local.y == scrollBar->size.y - 1) {
				part = sbDownArrow;
				return true;
			}
			return false;
		}

		if (local.x == 0) {
			part = sbLeftArrow;
			return true;
		}
		if (local.x == scrollBar->size.x - 1) {
			part = sbRightArrow;
			return true;
		}
		return false;
	}

	bool handleEditorScrollBarArrowHold(TEvent &event) {
		if (event.what != evMouseDown || (event.mouse.buttons & mbLeftButton) == 0) return false;

		TScrollBar *scrollBar = nullptr;
		int part = 0;
		if (editorScrollBarArrowPart(vScrollBar, event.mouse.where, part)) scrollBar = vScrollBar;
		else if (editorScrollBarArrowPart(hScrollBar, event.mouse.where, part))
			scrollBar = hScrollBar;
		if (scrollBar == nullptr) return false;

		select();
		scrollBar->setValue(scrollBar->value + scrollBar->scrollStep(part));
		clearEvent(event);

		bool repeat = true;
		int waitMs = 220;
		for (;;) {
			TEvent next{};
			getEvent(next, waitMs);
			if (next.what == evMouseUp) break;
			if ((next.what & (evMouseDown | evMouseMove | evMouseAuto)) != 0) {
				int currentPart = 0;
				repeat = editorScrollBarArrowPart(scrollBar, next.mouse.where, currentPart) && currentPart == part;
			}
			if (repeat) scrollBar->setValue(scrollBar->value + scrollBar->scrollStep(part));
			waitMs = 35;
		}
		return true;
	}

	virtual void handleEvent(TEvent &event) override {
		if (MRWindowManager::isWindowMinimized(this)) {
			if (event.what == evCommand && (event.message.command == cmMrWindowMinimize || event.message.command == cmZoom)) {
				MRWindowManager::restoreWindow(this);
				clearEvent(event);
				return;
			}
			if (event.what == evCommand && event.message.command == cmResize) {
				MRWindowManager::reinsertMinimizedWindow(this);
				clearEvent(event);
				return;
			}
			TWindow::handleEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmClose && mWindowRole == wrLog) {
			setWindowManuallyHidden(this, true);
			hide();
			static_cast<void>(mrEnsureUsableWorkWindow());
			clearEvent(event);
			return;
		}
		if (handleEditorScrollBarArrowHold(event)) return;
		const ushort originalEvent = event.what;
		const ushort keyCodeBefore = event.what == evKeyDown ? ctrlToArrow(event.keyDown.keyCode) : static_cast<ushort>(0);
		const ushort keyModifiersBefore = event.what == evKeyDown ? event.keyDown.controlKeyState : static_cast<ushort>(0);
		const bool markingBefore = mBlockMarkingOn;
		const std::size_t bufferLengthBefore = editor != nullptr ? editor->bufferLength() : 0;
		const std::size_t cursorBefore = editor != nullptr ? editor->cursorOffset() : 0;
		const std::size_t selectionStartBefore = editor != nullptr ? editor->selectionStartOffset() : 0;
		const std::size_t selectionEndBefore = editor != nullptr ? editor->selectionEndOffset() : 0;
		maybeTraceTtyCollisionKeyEvent("window-pre", event);
		traceCalculatorHotkeyEvent("window-pre", event);

		if (event.what == evMouseDown && editor != nullptr && (event.mouse.buttons & mbLeftButton) != 0 && mBlockMode != bmNone && !mBlockMarkingOn) {
			// Keep state, but hide committed block overlay while the mouse selection loop runs,
			// so the live growing selection is visible.
			editor->setBlockOverlayState(0, 0, 0, false, false);
		}
		if (event.what == evKeyDown && TKey(event.keyDown.keyCode, event.keyDown.controlKeyState) == TKey(kbShiftTab)) {
			event.keyDown.keyCode = kbShiftTab;
			event.keyDown.controlKeyState |= kbShift;
		}
		if (event.what == evKeyDown) {
			if (keyDebugEnabled() && TKey(event.keyDown.keyCode, event.keyDown.controlKeyState) == TKey(kbShiftTab)) {
				char line[192];
				std::snprintf(line, sizeof(line), "KEYDBG shifttab stage=window-pre keyCode=0x%04X mods=0x%04X cursor=%zu", static_cast<unsigned>(event.keyDown.keyCode), static_cast<unsigned>(event.keyDown.controlKeyState), editor != nullptr ? editor->cursorOffset() : 0);
				mrLogMessage(line);
			}
			if (mrHandleRuntimeKeymapEvent(event, isReadOnly() ? MRKeymapContext::ReadOnly : MRKeymapContext::Edit, this)) return;
			if (handleBuiltInBlockHotkeys(event)) return;
			std::string executedMacroName;
			if (mrvmRunAssignedMacroForKey(event.keyDown.keyCode, event.keyDown.controlKeyState, executedMacroName, nullptr)) {
				if (isCalculatorHotkeyEvent(event)) {
					std::string detail = "macro=" + executedMacroName;
					traceCalculatorHotkeyEvent("window-macro-consumed", event, detail.c_str());
				}
				if (keyDebugEnabled()) {
					char line[224];
					std::snprintf(line, sizeof(line), "KEYDBG shifttab stage=macro-consumed macro='%s'", executedMacroName.c_str());
					mrLogMessage(line);
				}
				clearEvent(event);
				return;
			}
			if (event.keyDown.keyCode == kbShiftTab && editor != nullptr) {
				const std::size_t cursorStart = editor->cursorOffset();
				const ushort eventTypeBeforeEditor = event.what;
				editor->handleEvent(event);
				if (keyDebugEnabled()) {
					char line[224];
					std::snprintf(line, sizeof(line), "KEYDBG shifttab stage=editor-dispatch eventBefore=0x%04X eventAfter=0x%04X cursorBefore=%zu cursorAfter=%zu", static_cast<unsigned>(eventTypeBeforeEditor), static_cast<unsigned>(event.what), cursorStart, editor->cursorOffset());
					mrLogMessage(line);
				}
			}
		}
		if (shouldCollapseSelectionBeforeEditorInput(event)) {
			const std::size_t cursor = editor->cursorOffset();
			editor->setSelectionOffsets(cursor, cursor, False);
		}
		if (frame != nullptr) {
			MRFrame *mrFrame = static_cast<MRFrame *>(frame);
			if ((event.what & (evMouseDown | evMouseMove | evMouseUp)) != 0) mrFrame->updateTaskHover(event.mouse.where, false);
			else if ((event.what & (evKeyDown | evCommand)) != 0)
				mrFrame->updateTaskHover(TPoint(), true);
		}

		TWindow::handleEvent(event);
		traceCalculatorHotkeyEvent("window-post", event);
		if (keyDebugEnabled() && originalEvent == evKeyDown && TKey(keyCodeBefore, keyModifiersBefore) == TKey(kbShiftTab)) {
			char line[192];
			std::snprintf(line, sizeof(line), "KEYDBG shifttab stage=window-post event=0x%04X cursor=%zu", static_cast<unsigned>(event.what), editor != nullptr ? editor->cursorOffset() : 0);
			mrLogMessage(line);
		}
		if ((originalEvent & (evKeyDown | evMouseDown | evMouseMove | evMouseUp)) != 0) applyPostInputBlockPolicy(markingBefore, originalEvent, selectionStartBefore, selectionEndBefore, bufferLengthBefore, cursorBefore, keyCodeBefore, keyModifiersBefore);
		if (event.what == evBroadcast && event.message.command == cmUpdateTitle) {
			updateTaskMarkers();
			if (frame != nullptr) frame->drawView();
			clearEvent(event);
		}
	}

	bool loadFromFile(const char *fileName) {
		std::string expandedName;
		std::string loadError;

		if (editor == nullptr || fileName == nullptr || *fileName == '\0') return false;

		expandedName = fileName;
		if (expandedName.size() >= editor->persistentFileNameCapacity()) return false;
		char expandedPath[MAXPATH];
		strnzcpy(expandedPath, expandedName.c_str(), sizeof(expandedPath));
		fexpand(expandedPath);
		expandedName = expandedPath;

		if (!editor->loadMappedFile(expandedName.c_str(), loadError)) return false;

		applyWindowColorThemeForPath(expandedName);
		resetTransientEditorState();
		setReadOnly(isExistingPathReadOnly(editor->persistentFileName()));
		mTemporaryFileUsed = false;
		mTemporaryFileName.clear();
		setWindowRole(wrFile);
		updateTitleFromEditor();
		return true;
	}

	bool loadTextBuffer(const char *text, const char *title = nullptr) {
		if (editor == nullptr) return false;
		if (!editor->replaceBufferText(text)) return false;

		resetWindowColorsToConfiguredDefaults();
		resetTransientEditorState();
		setReadOnly(false);
		mTemporaryFileUsed = false;
		mTemporaryFileName.clear();
		editor->clearPersistentFileName();
		setWindowRole(wrText);
		if (title != nullptr && *title != '\0') setDisplayTitle(title);
		else
			updateTitleFromEditor();
		refreshSyntaxContext();
		return true;
	}

	bool saveCurrentFile() {
		if (editor == nullptr || isReadOnly() || !editor->canSaveInPlace()) return false;

		bool ok = editor->saveInPlace() == True;
		if (ok) {
			mFirstSaveDone = true;
			mTemporaryFileUsed = false;
			mTemporaryFileName.clear();
			setWindowRole(wrFile);
			updateTitleFromEditor();
		}
		return ok;
	}

	bool saveCurrentFileAs() {
		if (editor == nullptr || isReadOnly() || !editor->canSaveAs()) return false;

		bool ok = editor->saveAsWithPrompt() == True;
		if (ok) {
			mFirstSaveDone = true;
			mTemporaryFileUsed = false;
			mTemporaryFileName.clear();
			setReadOnly(isExistingPathReadOnly(editor->persistentFileName()));
			setWindowRole(wrFile);
			updateTitleFromEditor();
		}
		return ok;
	}

	bool saveCurrentFileWithoutOverwritePrompt() {
		if (editor == nullptr || isReadOnly() || !editor->canSaveAs()) return false;

		bool ok = editor->canSaveInPlace() ? editor->saveInPlace() == True : editor->saveAsWithoutOverwritePrompt() == True;
		if (ok) {
			mFirstSaveDone = true;
			mTemporaryFileUsed = false;
			mTemporaryFileName.clear();
			setReadOnly(isExistingPathReadOnly(editor->persistentFileName()));
			setWindowRole(wrFile);
			updateTitleFromEditor();
		}
		return ok;
	}

	const char *currentFileName() const {
		if (editor != nullptr && editor->hasPersistentFileName()) return editor->persistentFileName();
		return "";
	}

	MRFileEditor *getEditor() const {
		return editor;
	}

	MRTextBuffer buffer() const {
		return MRTextBuffer(editor);
	}

	bool isBufferEmpty() const {
		return buffer().isEmpty();
	}

	std::size_t bufferLength() const {
		return buffer().length();
	}

	std::size_t bufferLineCount() const {
		return buffer().lineCount();
	}

	MRFileEditor::LoadTiming lastLoadTiming() const noexcept {
		return editor != nullptr ? editor->lastLoadTiming() : MRFileEditor::LoadTiming();
	}

	bool hasSelection() const {
		return buffer().hasSelection();
	}

	bool hasUndoHistory() const {
		return buffer().hasUndoHistory();
	}

	bool hasRedoHistory() const {
		return buffer().hasRedoHistory();
	}

	std::size_t undoStackDepth() const {
		return buffer().undoStackDepth();
	}

	std::size_t redoStackDepth() const {
		return buffer().redoStackDepth();
	}

	TPoint cursorPoint() const {
		return buffer().cursorPoint();
	}

	unsigned long cursorLineNumber() const {
		return buffer().cursorLineNumber();
	}

	unsigned long cursorColumnNumber() const {
		return buffer().cursorColumnNumber();
	}

	bool insertModeEnabled() const noexcept {
		return editor != nullptr && editor->insertModeEnabled();
	}

	std::size_t cursorOffset() const noexcept {
		return editor != nullptr ? editor->cursorOffset() : 0;
	}

	std::size_t selectionLength() const noexcept {
		return editor != nullptr ? editor->selectionLength() : 0;
	}

	const char *syntaxLanguageName() const {
		return editor != nullptr ? editor->syntaxLanguageName() : "Plain Text";
	}

	MRSyntaxLanguage syntaxLanguage() const {
		return editor != nullptr ? editor->syntaxLanguage() : MRSyntaxLanguage::PlainText;
	}

	bool hasPersistentFileName() const {
		return editor != nullptr && editor->hasPersistentFileName();
	}

	bool isMinimized() const noexcept {
		return MRWindowManager::isWindowMinimized(this);
	}

	void minimizeWindow() {
		MRWindowManager::minimizeWindow(this);
	}

	void restoreWindow() {
		MRWindowManager::restoreWindow(this);
	}

	TRect minimizedWorkspaceBounds() const noexcept {
		return MRWindowManager::minimizedBoundsForWorkspace(this);
	}

	TRect restoreWorkspaceBounds() const noexcept {
		return MRWindowManager::restoreBoundsForWorkspace(this);
	}

	bool canSaveInPlace() const {
		return editor != nullptr && editor->canSaveInPlace();
	}

	bool canSaveAs() const {
		return editor != nullptr && editor->canSaveAs();
	}

	bool isReadOnly() const {
		return editor != nullptr && editor->isReadOnly();
	}

	void setReadOnly(bool readOnly) {
		if (editor != nullptr) editor->setReadOnly(readOnly);
		if (indicator != nullptr) indicator->setReadOnly(readOnly);
		if (readOnly && editor != nullptr) editor->setDocumentModified(false);
	}

	bool hasBeenSavedInSession() const {
		return mFirstSaveDone;
	}

	bool eofInMemory() const {
		return editor != nullptr;
	}

	int bufferId() const {
		return mBufferId;
	}

	std::size_t documentId() const noexcept {
		return editor != nullptr ? editor->documentId() : 0;
	}

	WindowRole windowRole() const noexcept {
		return mWindowRole;
	}

	const char *windowRoleName() const noexcept {
		switch (mWindowRole) {
			case wrFile:
				return "File text";
			case wrCommunicationCommand:
				return "Communication command";
			case wrCommunicationPipe:
				return "Communication pipe";
			case wrCommunicationDevice:
				return "Communication device";
			case wrLog:
				return "Log window";
			case wrHelp:
				return "Help window";
			case wrText:
			default:
				return "Text window";
		}
	}

	void setWindowRole(WindowRole role, const std::string &detail = std::string()) {
		mWindowRole = role;
		mWindowRoleDetail = detail;
	}

	const std::string &windowRoleDetail() const noexcept {
		return mWindowRoleDetail;
	}

	bool isCommunicationWindow() const noexcept {
		return mWindowRole == wrCommunicationCommand || mWindowRole == wrCommunicationPipe || mWindowRole == wrCommunicationDevice;
	}

	bool isTemporaryFile() const {
		return mTemporaryFileUsed;
	}

	const char *temporaryFileName() const {
		return mTemporaryFileName.c_str();
	}

	bool isFileChanged() const {
		return editor != nullptr && !isReadOnly() && editor->isDocumentModified();
	}

	void setFileChanged(bool changed) {
		if (editor != nullptr) editor->setDocumentModified(changed && !isReadOnly());
	}

	void setCurrentFileName(const char *fileName) {
		if (editor == nullptr) return;

		if (fileName == nullptr || *fileName == '\0') editor->clearPersistentFileName();
		else {
			editor->setPersistentFileName(fileName);
			setWindowRole(wrFile);
		}
		if ((fileName == nullptr || *fileName == '\0') && mWindowRole == wrFile) setWindowRole(wrText);
		updateTitleFromEditor();
		refreshSyntaxContext();
	}

	bool confirmAbandonForReload() {
		if (editor == nullptr) return false;
		return editor->valid(cmClose) == True;
	}

	bool replaceTextBuffer(const char *text, const char *title = nullptr) {
		if (editor == nullptr || !editor->replaceBufferText(text)) return false;
		if (title != nullptr && *title != '\0') setDisplayTitle(title);
		return true;
	}

	bool appendTextBuffer(const char *text) {
		if (editor == nullptr) return false;
		return editor->appendBufferText(text);
	}

	bool appendLogViewerText(const char *text, const std::vector<std::pair<std::size_t, std::size_t>> *chunkFindRanges = nullptr) {
		if (editor == nullptr || text == nullptr) return false;
		return editor->appendLogViewerData(text, static_cast<uint>(std::strlen(text)), chunkFindRanges);
	}

	bool prependLogViewerText(const char *text, const std::vector<std::pair<std::size_t, std::size_t>> *chunkFindRanges = nullptr) {
		if (editor == nullptr || text == nullptr) return false;
		return editor->prependLogViewerData(text, static_cast<uint>(std::strlen(text)), chunkFindRanges);
	}

	void setLogViewerOptions(bool lineNumbers) {
		if (editor == nullptr) return;
		editor->setCommunicationViewerOptions(lineNumbers);
	}

	void setDisplayTitle(const char *title) {
		std::strncpy(displayTitle, (title != nullptr && *title != '\0') ? title : "Untitled", sizeof(displayTitle) - 1);
		displayTitle[sizeof(displayTitle) - 1] = '\0';
		refreshSyntaxContext();
		message(owner, evBroadcast, cmUpdateTitle, 0);
	}

	void trackCoprocessorTask(std::uint64_t taskId, mr::coprocessor::TaskKind kind = mr::coprocessor::TaskKind::Custom, const std::string &label = std::string()) {
		if (taskId == 0) return;
		for (std::size_t i = 0; i < mTrackedCoprocessorTasks.size(); ++i)
			if (mTrackedCoprocessorTasks[i].id == taskId) return;
		mTrackedCoprocessorTasks.push_back(TrackedTask(taskId, kind, label));
		updateTaskMarkers();
	}

	void noteQueuedBackgroundMacro(const std::string &name, bool staged) {
		++mMacroQueuedCount;
		mLastMacroSummaryText = staged ? "Queued staged macro '" : "Queued background macro '";
		mLastMacroSummaryText += name;
		mLastMacroSummaryText += "'.";
	}

	void noteBackgroundMacroCompleted(const std::string &summary) {
		++mMacroCompletedCount;
		mLastMacroSummaryText = summary;
	}

	void noteBackgroundMacroConflict(const std::string &summary) {
		++mMacroConflictCount;
		mLastMacroSummaryText = summary;
	}

	void noteBackgroundMacroCancelled(const std::string &summary) {
		++mMacroCancelledCount;
		mLastMacroSummaryText = summary;
	}

	void noteBackgroundMacroFailed(const std::string &summary) {
		++mMacroFailedCount;
		mLastMacroSummaryText = summary;
	}

	void noteBackgroundMacroCancelRequested(std::size_t count) {
		mLastMacroSummaryText = "Cancel requested for ";
		mLastMacroSummaryText += std::to_string(count);
		mLastMacroSummaryText += " background macro task";
		if (count != 1) mLastMacroSummaryText += "s";
		mLastMacroSummaryText += ".";
	}

	std::size_t trackedCoprocessorTaskCount() const noexcept {
		return mTrackedCoprocessorTasks.size();
	}

	std::uint64_t trackedCoprocessorTaskId(std::size_t index) const noexcept {
		return index < mTrackedCoprocessorTasks.size() ? mTrackedCoprocessorTasks[index].id : 0;
	}

	std::size_t trackedMacroTaskCount() const noexcept {
		std::size_t count = 0;
		for (std::size_t i = 0; i < mTrackedCoprocessorTasks.size(); ++i)
			if (mTrackedCoprocessorTasks[i].kind == mr::coprocessor::TaskKind::MacroJob) ++count;
		return count;
	}

	std::size_t trackedTaskCount(mr::coprocessor::TaskKind kind) const noexcept {
		std::size_t count = 0;
		for (std::size_t i = 0; i < mTrackedCoprocessorTasks.size(); ++i)
			if (mTrackedCoprocessorTasks[i].kind == kind) ++count;
		return count;
	}

	bool hasTrackedMacroTasks() const noexcept {
		return trackedMacroTaskCount() != 0;
	}

	bool hasTrackedExternalIoTasks() const noexcept {
		return trackedTaskCount(mr::coprocessor::TaskKind::ExternalIo) != 0;
	}

	std::string macroPolicySummary() const {
		return "Single writer: background macros never write live state directly";
	}

	std::string macroConflictPolicySummary() const {
		return "Staged commit on UI thread; version mismatch aborts, no rebase";
	}

	std::string macroCancelPolicySummary() const {
		return "Cancel is cooperative at VM safe points";
	}

	std::string macroCounterSummary() const {
		std::string text = "queued ";
		text += std::to_string(mMacroQueuedCount);
		text += ", ok ";
		text += std::to_string(mMacroCompletedCount);
		text += ", conflict ";
		text += std::to_string(mMacroConflictCount);
		text += ", cancel ";
		text += std::to_string(mMacroCancelledCount);
		text += ", fail ";
		text += std::to_string(mMacroFailedCount);
		return text;
	}

	std::string lastMacroSummary() const {
		return mLastMacroSummaryText.empty() ? std::string("<none>") : mLastMacroSummaryText;
	}

	std::vector<std::string> describeRunningTasks() const {
		std::vector<std::string> lines;
		std::size_t i;
		const std::string bullet = taskActivityBullet();
		const mr::coprocessor::Snapshot snapshot = mr::coprocessor::globalCoprocessor().snapshot();

		for (i = 0; i < mTrackedCoprocessorTasks.size(); ++i) {
			const TrackedTask &task = mTrackedCoprocessorTasks[i];
			std::string line;

			switch (task.kind) {
				case mr::coprocessor::TaskKind::MacroJob:
					line = "Macro";
					break;
				case mr::coprocessor::TaskKind::ExternalIo:
					line = "Program";
					break;
				case mr::coprocessor::TaskKind::IndicatorBlink:
					line = "Indicator";
					break;
				case mr::coprocessor::TaskKind::SyntaxWarmup:
					line = "Syntax";
					break;
				case mr::coprocessor::TaskKind::FoldWarmup:
					line = "Folding";
					break;
				case mr::coprocessor::TaskKind::MiniMapWarmup:
					line = "Mini map";
					break;
				case mr::coprocessor::TaskKind::SaveNormalizationWarmup:
					line = "Save cache";
					break;
				case mr::coprocessor::TaskKind::LineIndexWarmup:
					line = "Line index";
					break;
				case mr::coprocessor::TaskKind::Custom:
				default:
					line = "Task";
					break;
			}
			if (!task.label.empty()) {
				line += ": ";
				line += compactTaskLabel(task);
			}
			line = bullet + " " + line + "  " + formatTaskElapsed(task);
			lines.push_back(line);
		}
		if (editor != nullptr) {
			const bool lineIndexPending = editor->pendingLineIndexWarmupTaskId() != 0;
			const bool syntaxPending = editor->pendingSyntaxWarmupTaskId() != 0;
			const bool foldPending = editor->pendingFoldWarmupTaskId() != 0;

			if (lineIndexPending && trackedTaskCount(mr::coprocessor::TaskKind::LineIndexWarmup) == 0) lines.push_back(bullet + " " + lineIndexWarmingLabel());
			if (syntaxPending && trackedTaskCount(mr::coprocessor::TaskKind::SyntaxWarmup) == 0) lines.push_back(bullet + " " + syntaxWarmingLabel());
			if (foldPending && trackedTaskCount(mr::coprocessor::TaskKind::FoldWarmup) == 0) lines.push_back(bullet + " Folding");
			if (editor->pendingMiniMapWarmupTaskId() != 0 && trackedTaskCount(mr::coprocessor::TaskKind::MiniMapWarmup) == 0) lines.push_back(bullet + " " + miniMapRenderingLabel());
			if (editor->pendingSaveNormalizationWarmupTaskId() != 0 && trackedTaskCount(mr::coprocessor::TaskKind::SaveNormalizationWarmup) == 0) lines.push_back(bullet + " " + saveNormalizationWarmingLabel());

			{
				std::string line = "Syntax cov: ";
				line += std::to_string(syntaxPrefetchReachedBottomLine());
				line += "/";
				line += std::to_string(syntaxPrefetchTargetBottomLine());
				lines.push_back(line);
			}
			if (pendingSyntaxWarmupTaskId() != 0) {
				std::string line = "Syntax run: ";
				line += std::to_string(syntaxWarmupTopLine());
				line += "..";
				line += std::to_string(syntaxWarmupBottomLine());
				lines.push_back(line);
			}
			{
				std::string line = "Line idx: ";
				line += exactLineCountKnown() ? "exact " : "est ";
				line += std::to_string(estimatedLineCount());
				lines.push_back(line);
			}
			if (!editor->lastUiHotpathTrace().empty()) lines.push_back(std::string("UI: ") + editor->lastUiHotpathTrace());
		}
		lines.push_back(std::string("Results pending: ") + std::to_string(snapshot.pendingResults));
		for (const mr::coprocessor::LaneSnapshot &lane : snapshot.lanes) {
			std::string line = std::string("Lane ") + laneLabel(lane.lane) + ": ";
			const std::size_t activeWorkerCount = lane.activeTasks.size();

			if (lane.activeTasks.empty()) line += "idle";
			else {
				line += "act ";
				line += std::to_string(activeWorkerCount);
				if (lane.workerCount > 1) {
					line += "/";
					line += std::to_string(lane.workerCount);
				}
			}
			line += " q ";
			line += std::to_string(lane.queuedTasks.size());
			lines.push_back(line);

			for (std::size_t workerSlot = 0; workerSlot < lane.workerCount; ++workerSlot) {
				std::string workerLine = "  w";
				const mr::coprocessor::ActiveTaskSnapshot *activeTask = nullptr;

				for (const mr::coprocessor::ActiveTaskSnapshot &candidate : lane.activeTasks) {
					if (candidate.workerSlot != workerSlot) continue;
					activeTask = &candidate;
					break;
				}
				workerLine += std::to_string(workerSlot + 1);
				if (activeTask == nullptr) {
					workerLine += " idle";
					lines.push_back(workerLine);
					continue;
				}
				workerLine += " ";
				workerLine += taskKindLabel(activeTask->task.kind);
				if (!activeTask->task.label.empty()) {
					workerLine += " ";
					workerLine += compactTaskLabelLimited(activeTask->task.kind, activeTask->task.label, 18);
				}
				workerLine += " doc ";
				workerLine += std::to_string(activeTask->task.documentId);
				workerLine += " v";
				workerLine += std::to_string(activeTask->task.baseVersion);
				workerLine += " run ";
				workerLine += formatMicrosElapsed(activeTask->runMicros);
				workerLine += " wait ";
				workerLine += formatMicrosElapsed(activeTask->queueMicros);
				lines.push_back(workerLine);
			}

			if (!lane.queuedTasks.empty()) {
				std::string queuedLine = "  next ";
				const std::size_t limit = std::min<std::size_t>(3, lane.queuedTasks.size());

				for (std::size_t queueIndex = 0; queueIndex < limit; ++queueIndex) {
					if (queueIndex != 0) queuedLine += " | ";
					queuedLine += taskKindLabel(lane.queuedTasks[queueIndex].kind);
					if (!lane.queuedTasks[queueIndex].label.empty()) {
						queuedLine += " ";
						queuedLine += compactTaskLabelLimited(lane.queuedTasks[queueIndex].kind, lane.queuedTasks[queueIndex].label, 14);
					}
				}
				if (lane.queuedTasks.size() > limit) {
					queuedLine += " | +";
					queuedLine += std::to_string(lane.queuedTasks.size() - limit);
				}
				lines.push_back(queuedLine);
			}
		}
		return lines;
	}

	bool cancelTrackedMacroTasks() {
		bool cancelledAny = false;
		std::size_t macroCount = trackedMacroTaskCount();

		for (std::size_t i = 0; i < mTrackedCoprocessorTasks.size(); ++i) {
			if (mTrackedCoprocessorTasks[i].kind != mr::coprocessor::TaskKind::MacroJob) continue;
			mrTraceCoprocessorTaskCancel(mBufferId, mTrackedCoprocessorTasks[i].id);
			if (mr::coprocessor::globalCoprocessor().cancelTask(mTrackedCoprocessorTasks[i].id)) cancelledAny = true;
		}
		if (cancelledAny) noteBackgroundMacroCancelRequested(macroCount);
		return cancelledAny;
	}

	bool cancelTrackedExternalIoTasks() {
		bool cancelledAny = false;

		for (std::size_t i = 0; i < mTrackedCoprocessorTasks.size(); ++i) {
			if (mTrackedCoprocessorTasks[i].kind != mr::coprocessor::TaskKind::ExternalIo) continue;
			mrTraceCoprocessorTaskCancel(mBufferId, mTrackedCoprocessorTasks[i].id);
			if (mr::coprocessor::globalCoprocessor().cancelTask(mTrackedCoprocessorTasks[i].id)) cancelledAny = true;
		}
		return cancelledAny;
	}

	std::size_t prepareCoprocessorTasksForShutdown() {
		std::size_t clearedCount = mTrackedCoprocessorTasks.size();

		for (std::size_t i = 0; i < mTrackedCoprocessorTasks.size(); ++i) {
			mrTraceCoprocessorTaskCancel(mBufferId, mTrackedCoprocessorTasks[i].id);
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mTrackedCoprocessorTasks[i].id));
		}
		mTrackedCoprocessorTasks.clear();
		if (editor != nullptr) {
			std::uint64_t lineIndexTaskId = editor->pendingLineIndexWarmupTaskId();
			if (lineIndexTaskId != 0) {
				mrTraceCoprocessorTaskCancel(mBufferId, lineIndexTaskId);
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(lineIndexTaskId));
				editor->clearLineIndexWarmupTask(lineIndexTaskId);
				++clearedCount;
			}
			std::uint64_t syntaxTaskId = editor->pendingSyntaxWarmupTaskId();
			if (syntaxTaskId != 0) {
				mrTraceCoprocessorTaskCancel(mBufferId, syntaxTaskId);
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(syntaxTaskId));
				editor->clearSyntaxWarmupTask(syntaxTaskId);
				++clearedCount;
			}
			std::uint64_t foldTaskId = editor->pendingFoldWarmupTaskId();
			if (foldTaskId != 0) {
				mrTraceCoprocessorTaskCancel(mBufferId, foldTaskId);
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(foldTaskId));
				editor->clearFoldWarmupTask(foldTaskId);
				++clearedCount;
			}
			std::uint64_t miniMapTaskId = editor->pendingMiniMapWarmupTaskId();
			if (miniMapTaskId != 0) {
				mrTraceCoprocessorTaskCancel(mBufferId, miniMapTaskId);
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(miniMapTaskId));
				editor->clearMiniMapWarmupTask(miniMapTaskId);
				++clearedCount;
			}
			std::uint64_t saveNormalizationTaskId = editor->pendingSaveNormalizationWarmupTaskId();
			if (saveNormalizationTaskId != 0) {
				mrTraceCoprocessorTaskCancel(mBufferId, saveNormalizationTaskId);
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(saveNormalizationTaskId));
				editor->clearSaveNormalizationWarmupTask(saveNormalizationTaskId);
				++clearedCount;
			}
		}
		updateTaskMarkers();
		return clearedCount;
	}

	void showMacroNotice(const std::string &text, MRIndicator::NoticeKind kind) {
		if (indicator != nullptr) indicator->showStatusNotice(text, kind);
	}

	std::size_t originalBufferLength() const noexcept {
		return editor != nullptr ? editor->originalBufferLength() : 0;
	}

	std::size_t addBufferLength() const noexcept {
		return editor != nullptr ? editor->addBufferLength() : 0;
	}

	std::size_t pieceCount() const noexcept {
		return editor != nullptr ? editor->pieceCount() : 0;
	}

	std::size_t documentVersion() const noexcept {
		return editor != nullptr ? editor->documentVersion() : 0;
	}

	bool hasMappedOriginalSource() const noexcept {
		return editor != nullptr && editor->hasMappedOriginalSource();
	}

	const std::string &mappedOriginalPath() const noexcept {
		if (editor != nullptr) return editor->mappedOriginalPath();
		return emptyString();
	}

	std::size_t estimatedLineCount() const noexcept {
		return editor != nullptr ? editor->estimatedLineCount() : 1;
	}

	bool exactLineCountKnown() const noexcept {
		return editor != nullptr && editor->exactLineCountKnown();
	}

	std::uint64_t pendingLineIndexWarmupTaskId() const noexcept {
		return editor != nullptr ? editor->pendingLineIndexWarmupTaskId() : 0;
	}

	std::uint64_t pendingSyntaxWarmupTaskId() const noexcept {
		return editor != nullptr ? editor->pendingSyntaxWarmupTaskId() : 0;
	}

	std::uint64_t pendingFoldWarmupTaskId() const noexcept {
		return editor != nullptr ? editor->pendingFoldWarmupTaskId() : 0;
	}

	std::uint64_t pendingMiniMapWarmupTaskId() const noexcept {
		return editor != nullptr ? editor->pendingMiniMapWarmupTaskId() : 0;
	}

	std::uint64_t pendingSaveNormalizationWarmupTaskId() const noexcept {
		return editor != nullptr ? editor->pendingSaveNormalizationWarmupTaskId() : 0;
	}

	std::size_t syntaxWarmupTopLine() const noexcept {
		return editor != nullptr ? editor->syntaxWarmupTopLine() : 0;
	}

	std::size_t syntaxWarmupBottomLine() const noexcept {
		return editor != nullptr ? editor->syntaxWarmupBottomLine() : 0;
	}

	std::size_t syntaxPrefetchTargetBottomLine() const noexcept {
		return editor != nullptr ? editor->syntaxPrefetchTargetBottomLine() : 0;
	}

	std::size_t syntaxPrefetchReachedBottomLine() const noexcept {
		return editor != nullptr ? editor->syntaxPrefetchReachedBottomLine() : 0;
	}

	bool usesApproximateMetrics() const noexcept {
		return editor != nullptr && editor->usesApproximateMetrics();
	}

	void releaseCoprocessorTask(std::uint64_t taskId) {
		for (std::vector<TrackedTask>::iterator it = mTrackedCoprocessorTasks.begin(); it != mTrackedCoprocessorTasks.end(); ++it)
			if (it->id == taskId) {
				mTrackedCoprocessorTasks.erase(it);
				updateTaskMarkers();
				return;
			}
	}

	int indentLevel() const {
		return mIndentLevel;
	}

	void setIndentLevel(int level) {
		if (level < 1) level = 1;
		if (level > 254) level = 254;
		mIndentLevel = level;
		if (editor != nullptr) editor->setPreferredIndentColumn(level);
	}

	enum BlockMode {
		bmNone = 0,
		bmLine = 1,
		bmColumn = 2,
		bmStream = 3
	};

	void beginLineBlock() {
		beginBlock(bmLine);
	}

	void beginColumnBlock() {
		beginBlock(bmColumn);
	}

	void beginStreamBlock() {
		beginBlock(bmStream);
	}

	void endBlock() {
		if (editor == nullptr || mBlockMode == bmNone) return;
		mBlockEnd = static_cast<uint>(editor->cursorOffset());
		mBlockMarkingOn = false;
		syncBlockVisual();
	}

	void clearBlock() {
		clearBlockState(false);
	}

	bool toggleBlockVisibility() {
		if (mBlockMode != bmNone) {
			storeHiddenBlockState();
			clearBlockState(true);
			return true;
		}
		if (!mHiddenBlockValid || editor == nullptr) return false;
		applyCommittedBlockState(static_cast<int>(mHiddenBlockMode), mHiddenBlockMarkingOn, mHiddenBlockAnchor, mHiddenBlockEnd);
		mHiddenBlockValid = false;
		return true;
	}

	bool hasBlock() const {
		return mBlockMode != bmNone;
	}

	bool isBlockMarking() const {
		return mBlockMode != bmNone && mBlockMarkingOn;
	}

	int blockStatus() const {
		return static_cast<int>(mBlockMode);
	}

	uint blockAnchorPtr() const {
		return mBlockAnchor;
	}

	uint blockEffectiveEndPtr() const {
		return effectiveBlockEnd();
	}

	int blockLine1() const {
		return normalizedBlockLine1();
	}

	int blockLine2() const {
		return normalizedBlockLine2();
	}

	int blockCol1() const {
		return normalizedBlockCol1();
	}

	int blockCol2() const {
		return normalizedBlockCol2();
	}

	void refreshBlockVisual() {
		syncBlockVisual();
	}

	bool moveCursorToTopOfView() {
		if (editor == nullptr) return false;
		editor->setCursorOffset(editor->lineMoveOffset(editor->cursorOffset(), -(std::max(0, editor->currentViewRow() - 1))));
		return true;
	}

	bool moveCursorToBottomOfView() {
		if (editor == nullptr) return false;
		editor->setCursorOffset(editor->lineMoveOffset(editor->cursorOffset(), std::max(0, editor->visibleViewportRows() - editor->currentViewRow())));
		return true;
	}

	bool moveCursorToBlockStart() {
		uint a = 0;
		uint b = 0;
		if (editor == nullptr || mBlockMode == bmNone) return false;
		blockPtrRange(a, b);
		editor->setCursorOffset(a);
		return true;
	}

	bool moveCursorToBlockEnd() {
		uint a = 0;
		uint b = 0;
		if (editor == nullptr || mBlockMode == bmNone) return false;
		blockPtrRange(a, b);
		editor->setCursorOffset(b);
		return true;
	}

	bool centerCursorInView() {
		if (editor == nullptr) return false;
		editor->revealCursor(True);
		return true;
	}

	bool shiftCursorBlockMark(const MRKeymapToken &token) {
		std::size_t target = 0;

		if (editor == nullptr) return false;
		target = editor->cursorOffset();
		switch (token.baseKey()) {
			case MRKeymapBaseKey::Left:
				target = token.hasModifier(MRKeymapModifier::Ctrl) ? editor->prevWordOffset(target) : editor->prevCharOffset(target);
				break;
			case MRKeymapBaseKey::Right:
				target = token.hasModifier(MRKeymapModifier::Ctrl) ? editor->nextWordOffset(target) : editor->nextCharOffset(target);
				break;
			case MRKeymapBaseKey::Up:
				target = editor->lineMoveOffset(target, -1);
				break;
			case MRKeymapBaseKey::Down:
				target = editor->lineMoveOffset(target, 1);
				break;
			case MRKeymapBaseKey::Home:
				target = token.hasModifier(MRKeymapModifier::Ctrl) ? 0 : editor->lineStartOffset(target);
				break;
			case MRKeymapBaseKey::End:
				target = token.hasModifier(MRKeymapModifier::Ctrl) ? editor->bufferLength() : editor->lineEndOffset(target);
				break;
			case MRKeymapBaseKey::PageUp:
				target = editor->lineMoveOffset(target, -(std::max(2, editor->visibleViewportRows()) - 1));
				break;
			case MRKeymapBaseKey::PageDown:
				target = editor->lineMoveOffset(target, std::max(2, editor->visibleViewportRows()) - 1);
				break;
			default:
				return false;
		}
		if (mBlockMode == bmNone) mBlockMode = bmStream;
		if (!mBlockMarkingOn) {
			discardHiddenBlockState();
			mBlockMarkingOn = true;
			mBlockAnchor = static_cast<uint>(editor->cursorOffset());
			mBlockEnd = static_cast<uint>(editor->cursorOffset());
		}
		editor->setCursorOffset(target);
		mBlockEnd = static_cast<uint>(editor->cursorOffset());
		syncBlockVisual();
		return true;
	}

	bool markWordRight() {
		auto isWordByte = [](char ch) noexcept {
			const unsigned char uch = static_cast<unsigned char>(ch);
			return std::isalnum(uch) != 0 || ch == '_';
		};
		std::size_t start = 0;
		std::size_t end = 0;

		if (editor == nullptr) return false;
		start = editor->cursorOffset();
		if (start >= editor->bufferLength()) return false;
		if (!isWordByte(editor->charAtOffset(start))) {
			start = editor->nextWordOffset(start);
			if (start >= editor->bufferLength()) return false;
		}
		while (start > 0 && isWordByte(editor->charAtOffset(start - 1)))
			--start;
		end = start;
		while (end < editor->bufferLength() && isWordByte(editor->charAtOffset(end)))
			end = editor->nextCharOffset(end);
		if (end <= start) return false;
		applyCommittedBlockState(static_cast<int>(bmStream), false, static_cast<uint>(start), static_cast<uint>(end));
		return true;
	}

	bool sortColumnBlock(bool ascending) {
		struct ColumnRow {
			std::string text;
			std::string lineEnding;
			std::string key;
		};

		if (editor == nullptr || mBlockMode != bmColumn) return false;

		uint blockStart = 0;
		uint blockEnd = 0;
		const int line1 = normalizedBlockLine1();
		const int line2 = normalizedBlockLine2();
		const int col1 = normalizedBlockCol1();
		const int col2 = normalizedBlockCol2();
		const std::size_t regionStart = editor->lineStartOffset(std::min(mBlockAnchor, effectiveBlockEnd()));
		const std::size_t regionEnd = std::min(editor->bufferLength(), editor->nextLineOffset(std::max(mBlockAnchor, effectiveBlockEnd())));
		const std::string snapshot = editor->snapshotText();
		std::vector<ColumnRow> rows;
		std::string sortedText;
		auto lineStartByNumber = [this](int lineNumber) {
			std::size_t lineStart = 0;
			for (int currentLine = 1; currentLine < lineNumber; ++currentLine) {
				std::size_t next = editor->nextLineOffset(lineStart);
				if (next <= lineStart) break;
				lineStart = next;
			}
			return lineStart;
		};

		blockPtrRange(blockStart, blockEnd);
		for (std::size_t lineStart = regionStart; lineStart < regionEnd; lineStart = editor->nextLineOffset(lineStart)) {
			const std::size_t lineEnd = editor->lineEndOffset(lineStart);
			const std::size_t nextLine = std::min(editor->bufferLength(), editor->nextLineOffset(lineStart));
			const std::size_t keyStart = editor->charPtrOffset(lineStart, std::max(0, col1 - 1));
			const std::size_t keyEnd = std::min(lineEnd, editor->charPtrOffset(lineStart, col2));
			ColumnRow row;

			row.text = snapshot.substr(lineStart, lineEnd - lineStart);
			row.lineEnding = snapshot.substr(lineEnd, nextLine - lineEnd);
			row.key = snapshot.substr(keyStart, keyEnd - keyStart);
			rows.push_back(std::move(row));
			if (nextLine <= lineStart) break;
		}
		if (rows.size() < 2) return true;
		std::stable_sort(rows.begin(), rows.end(), [ascending](const ColumnRow &left, const ColumnRow &right) { return ascending ? left.key < right.key : right.key < left.key; });
		for (const ColumnRow &row : rows)
			sortedText += row.text + row.lineEnding;
		if (!editor->replaceRangeAndSelect(static_cast<uint>(regionStart), static_cast<uint>(regionEnd), sortedText.data(), static_cast<uint>(sortedText.size()))) return false;
		{
			const std::size_t sortedStart = editor->charPtrOffset(lineStartByNumber(line1), std::max(0, col1 - 1));
			const std::size_t sortedEnd = editor->charPtrOffset(lineStartByNumber(line2), std::max(0, col2 - 1));
			applyCommittedBlockState(static_cast<int>(bmColumn), false, static_cast<uint>(sortedStart), static_cast<uint>(sortedEnd));
		}
		mColumnSortAscending = !ascending;
		return true;
	}

	bool sortColumnBlockToggleOrder() {
		return sortColumnBlock(mColumnSortAscending);
	}

	void applyCommittedBlockState(int mode, bool markingOn, uint anchor, uint end) {
		discardHiddenBlockState();
		if (editor == nullptr) return;
		if (mode < bmNone || mode > bmStream) mode = bmNone;
		mBlockMode = static_cast<BlockMode>(mode);
		mBlockMarkingOn = mBlockMode != bmNone && markingOn;
		mBlockAnchor = std::min<uint>(anchor, static_cast<uint>(editor->bufferLength()));
		mBlockEnd = std::min<uint>(end, static_cast<uint>(editor->bufferLength()));
		if (mBlockMode == bmNone) {
			mBlockMarkingOn = false;
			mBlockAnchor = 0;
			mBlockEnd = 0;
			editor->setBlockOverlayState(0, 0, 0, false, false);
			editor->setSelectionOffsets(editor->cursorOffset(), editor->cursorOffset(), False);
			editor->update(ufView);
			return;
		}
		syncBlockVisual();
	}

  private:
	void clearBlockState(bool preserveHiddenState) {
		if (!preserveHiddenState) discardHiddenBlockState();
		mBlockMode = bmNone;
		mBlockMarkingOn = false;
		mBlockAnchor = 0;
		mBlockEnd = 0;
		if (editor != nullptr) {
			editor->setBlockOverlayState(0, 0, 0, false);
			editor->setSelectionOffsets(editor->cursorOffset(), editor->cursorOffset(), False);
			editor->revealCursor(True);
			editor->update(ufView);
		}
	}

	void storeHiddenBlockState() {
		if (mBlockMode == bmNone) return;
		mHiddenBlockMode = mBlockMode;
		mHiddenBlockMarkingOn = mBlockMarkingOn;
		mHiddenBlockAnchor = mBlockAnchor;
		mHiddenBlockEnd = effectiveBlockEnd();
		mHiddenBlockValid = true;
	}

	void discardHiddenBlockState() {
		mHiddenBlockValid = false;
		mHiddenBlockMode = bmNone;
		mHiddenBlockMarkingOn = false;
		mHiddenBlockAnchor = 0;
		mHiddenBlockEnd = 0;
	}
	void resetWindowColorsToConfiguredDefaults() {
		mWindowPaletteData = defaultWindowPaletteData();
		rebuildWindowPalette();
		mCustomEofMarkerColorValid = false;
		if (editor != nullptr) editor->setWindowEofMarkerColorOverride(false);
		refreshWindowPaletteViews();
	}

  public:
	void applyWindowColorThemeForPath(const std::string &path) {
		std::array<unsigned char, MRColorSetupSettings::kWindowCount> colors;
		std::string themePath;
		std::string errorText;
		resetWindowColorsToConfiguredDefaults();
		if (!effectiveEditWindowColorThemePathForPath(path, themePath, nullptr) || themePath.empty()) return;
		if (!loadWindowColorThemeGroupValues(themePath, colors, &errorText)) {
			mrLogMessage(("Window color theme load failed: " + themePath + " (" + errorText + ")").c_str());
			return;
		}

		const TColorAttr framePassive = static_cast<TColorAttr>(colors[4]);
		const TColorAttr frameActive = static_cast<TColorAttr>(colors[5]);
		const TColorAttr textNormal = static_cast<TColorAttr>(colors[0]);
		const TColorAttr textSelected = static_cast<TColorAttr>(colors[2]);

		mWindowPaletteData[0] = framePassive;
		mWindowPaletteData[1] = frameActive;
		mWindowPaletteData[2] = frameActive;
		mWindowPaletteData[3] = framePassive;
		mWindowPaletteData[4] = frameActive;
		mWindowPaletteData[5] = textNormal;
		mWindowPaletteData[6] = textSelected;
		mWindowPaletteData[7] = textSelected;
		mWindowPaletteData[8] = static_cast<TColorAttr>(colors[6]);
		mWindowPaletteData[9] = static_cast<TColorAttr>(colors[7]);
		mWindowPaletteData[10] = static_cast<TColorAttr>(colors[1]);
		mWindowPaletteData[11] = static_cast<TColorAttr>(colors[8]);
		mCustomEofMarkerColorValid = true;
		mCustomEofMarkerColor = static_cast<TColorAttr>(colors[3]);
		rebuildWindowPalette();
		if (editor != nullptr) editor->setWindowEofMarkerColorOverride(true, mCustomEofMarkerColor);
		refreshWindowPaletteViews();
	}

  private:
	static bool keyDebugEnabled() noexcept {
		static int cached = -1;
		if (cached < 0) {
			const char *value = std::getenv("MR_KEY_DEBUG");
			cached = (value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0) ? 1 : 0;
		}
		return cached == 1;
	}

	static bool isTtyCollisionKeyEvent(const TEvent &event) noexcept {
		if (event.what != evKeyDown) return false;
		switch (event.keyDown.keyCode) {
			case kbBack:
			case kbCtrlH:
			case kbTab:
			case kbShiftTab:
			case kbCtrlI:
			case kbEnter:
			case kbCtrlJ:
			case kbCtrlM:
				return true;
			default:
				break;
		}
		const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);
		return normalized == TKey(kbBack) || normalized == TKey(kbTab) || normalized == TKey(kbShiftTab) || normalized == TKey(kbEnter);
	}

	static const char *ttyCollisionClass(const TKey &normalized) noexcept {
		if (normalized == TKey(kbBack)) return "backspace";
		if (normalized == TKey(kbTab)) return "tab";
		if (normalized == TKey(kbShiftTab)) return "shifttab";
		if (normalized == TKey(kbEnter)) return "enter";
		return "other";
	}

	static void maybeTraceTtyCollisionKeyEvent(const char *stage, const TEvent &event) {
		if (!keyDebugEnabled() || !isTtyCollisionKeyEvent(event)) return;
		const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);
		char line[320];
		std::snprintf(line, sizeof(line), "KEYDBG tty stage=%s class=%s rawCode=0x%04X rawMods=0x%04X normCode=0x%04X normMods=0x%04X textLen=%u char=0x%02X", stage, ttyCollisionClass(normalized), static_cast<unsigned>(event.keyDown.keyCode), static_cast<unsigned>(event.keyDown.controlKeyState), static_cast<unsigned>(normalized.code), static_cast<unsigned>(normalized.mods), static_cast<unsigned>(event.keyDown.textLength), static_cast<unsigned>(static_cast<unsigned char>(event.keyDown.charScan.charCode)));
		mrLogMessage(line);
	}

	static bool isCalculatorHotkeyEvent(const TEvent &event) noexcept {
		if (event.what != evKeyDown) return false;
		const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);
		return (normalized.mods & kbAltShift) != 0 && normalized.code == 'C';
	}

	static void traceCalculatorHotkeyEvent(const char *stage, const TEvent &event, const char *detail = nullptr) {
		if (!isCalculatorHotkeyEvent(event)) return;
		const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);
		char line[320];
		std::snprintf(line, sizeof(line), "KEYDBG calc stage=%s rawCode=0x%04X rawMods=0x%04X normCode=0x%04X normMods=0x%04X textLen=%u char=0x%02X%s%s", stage, static_cast<unsigned>(event.keyDown.keyCode), static_cast<unsigned>(event.keyDown.controlKeyState), static_cast<unsigned>(normalized.code), static_cast<unsigned>(normalized.mods), static_cast<unsigned>(event.keyDown.textLength), static_cast<unsigned>(static_cast<unsigned char>(event.keyDown.charScan.charCode)), detail != nullptr ? " " : "", detail != nullptr ? detail : "");
		mrLogMessage(line);
	}

	static TColorAttr configuredWindowPaletteSlot(unsigned char slot) noexcept {
		unsigned char value = 0x07;
		unsigned char overrideValue = 0;

		switch (slot) {
			case 8:
				value = 0x17;
				break;
			case 9:
				value = 0x1F;
				break;
			case 10:
			case kMrPaletteCurrentLine:
				value = 0x1A;
				break;
			case 11:
				value = 0x31;
				break;
			case 12:
			case kMrPaletteCurrentLineInBlock:
				value = 0x31;
				break;
			case 13:
				value = 0x1E;
				break;
			case 14:
			case kMrPaletteChangedText:
			case kMrPaletteEofMarker:
				value = 0x71;
				break;
			case 15:
				value = 0x1F;
				break;
			case kMrPaletteLineNumbers:
				value = 0x1F;
				break;
			default:
				break;
		}
		if (configuredColorSlotOverride(slot, overrideValue)) value = overrideValue;
		return static_cast<TColorAttr>(value);
	}

	static std::array<TColorAttr, 12> defaultWindowPaletteData() noexcept {
		return {configuredWindowPaletteSlot(8), configuredWindowPaletteSlot(9), configuredWindowPaletteSlot(10), configuredWindowPaletteSlot(11), configuredWindowPaletteSlot(12), configuredWindowPaletteSlot(13), configuredWindowPaletteSlot(14), configuredWindowPaletteSlot(15), configuredWindowPaletteSlot(kMrPaletteCurrentLine), configuredWindowPaletteSlot(kMrPaletteCurrentLineInBlock), configuredWindowPaletteSlot(kMrPaletteChangedText), configuredWindowPaletteSlot(kMrPaletteLineNumbers)};
	}

	void rebuildWindowPalette() {
		mWindowPalette = TPalette(mWindowPaletteData.data(), static_cast<ushort>(mWindowPaletteData.size()));
	}

	void refreshWindowPaletteViews() {
		drawView();
		if (frame != nullptr) frame->drawView();
		if (hScrollBar != nullptr) hScrollBar->drawView();
		if (vScrollBar != nullptr) vScrollBar->drawView();
		if (indicator != nullptr) indicator->drawView();
		if (editor != nullptr) editor->drawView();
	}

	MRFileEditor *createEditor(const TRect &bounds, const char *fileName) {
		MRFileEditor *created = new MRFileEditor(bounds, hScrollBar, vScrollBar, indicator, fileName != nullptr ? fileName : "");
		created->setPreferredIndentColumn(mIndentLevel);
		return created;
	}

	void layoutEditorChrome() {
		if (MRWindowManager::isWindowMinimized(this)) {
			if (hScrollBar != nullptr) hScrollBar->hide();
			if (vScrollBar != nullptr) vScrollBar->hide();
			if (indicator != nullptr) indicator->hide();
			if (editor != nullptr) editor->hide();
			return;
		}
		if (editor != nullptr && (editor->state & sfVisible) == 0) editor->show();
		if (hScrollBar != nullptr) {
			TRect hRect(1, size.y - 1, size.x - 1, size.y);
			hScrollBar->locate(hRect);
		}
		if (vScrollBar != nullptr) {
			TRect vRect(size.x - 1, 1, size.x, size.y - 1);
			vScrollBar->locate(vRect);
		}
		if (indicator != nullptr) {
			short right = std::max<short>(3, std::min<short>(38, short(size.x - 1)));
			TRect indicatorRect(2, size.y - 1, right, size.y);
			indicator->locate(indicatorRect);
		}
		if (editor != nullptr) {
			TRect r(getExtent());
			r.grow(-1, -1);
			editor->changeBounds(r);
		}
	}

	static int allocateBufferId() {
		static int nextId = 1;
		return nextId++;
	}

	static const std::string &emptyString() noexcept {
		static const std::string value;
		return value;
	}

	static TFrame *initFrame(TRect r) {
		return new MRFrame(r);
	}

	static bool isExistingPathReadOnly(const char *fileName) {
		if (fileName == nullptr || *fileName == '\0') return false;
		if (access(fileName, F_OK) != 0) return false;
		return access(fileName, W_OK) != 0;
	}

	void resetTransientEditorState() {
		if (editor == nullptr) return;
		clearBlock();
	}

	void prepareForClose() {
		std::ostringstream line;
		const char *title = getTitle(0);
		std::size_t cancelledCount = 0;
		const auto startedAt = std::chrono::steady_clock::now();
		long long clearUndoRedoMs = 0;
		long long destroyEditorMs = 0;
		std::size_t undoBefore = 0;
		std::size_t redoBefore = 0;
		std::size_t undoAfter = 0;
		std::size_t redoAfter = 0;
		const bool modified = isFileChanged();
		const std::size_t lengthBeforeClose = bufferLength();
		const std::size_t addBeforeClose = addBufferLength();
		const std::size_t piecesBeforeClose = pieceCount();

		if (mClosePrepared) return;
		mClosePrepared = true;
		cancelledCount = prepareCoprocessorTasksForShutdown();
		if (editor != nullptr) {
			undoBefore = editor->bufferModel().undoStackDepth();
			redoBefore = editor->bufferModel().redoStackDepth();
			const auto clearStartedAt = std::chrono::steady_clock::now();
			editor->bufferModel().clearUndoRedo();
			clearUndoRedoMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - clearStartedAt).count();
			undoAfter = editor->bufferModel().undoStackDepth();
			redoAfter = editor->bufferModel().redoStackDepth();
			const auto destroyStartedAt = std::chrono::steady_clock::now();
			remove(editor);
			TObject::destroy(editor);
			editor = nullptr;
			destroyEditorMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - destroyStartedAt).count();
		}
		line << "Preparing close for window #" << mBufferId << " title='" << (title != nullptr ? title : "?") << "'"
		     << " cancelled_tasks=" << cancelledCount << " modified=" << (modified ? 1 : 0) << " len=" << lengthBeforeClose << " add=" << addBeforeClose << " pieces=" << piecesBeforeClose
		     << " undo_before=" << undoBefore << " redo_before=" << redoBefore << " undo_after=" << undoAfter << " redo_after=" << redoAfter << " clear_undo_redo_ms=" << clearUndoRedoMs
		     << " destroy_editor_ms=" << destroyEditorMs << " took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt).count() << ".";
		{
			std::ofstream out("misc/mr.log", std::ios::out | std::ios::app | std::ios::binary);
			if (out) {
				std::time_t now = std::time(nullptr);
				std::tm *tmNow = std::localtime(&now);
				char buffer[32];
				if (tmNow != nullptr && std::strftime(buffer, sizeof(buffer), "%H:%M:%S", tmNow) != 0) out << "[" << buffer << "] " << line.str() << '\n';
				else
					out << "[--:--:--] " << line.str() << '\n';
				out.flush();
			}
		}
	}

	static void scheduleEnsureUsableWorkWindow() {
		TEvent event;

		if (TProgram::application == nullptr) return;
		std::memset(&event, 0, sizeof(event));
		event.what = evCommand;
		event.message.command = cmMrEnsureUsableWorkWindow;
		TProgram::application->putEvent(event);
	}

	void updateTitleFromEditor() {
		if (editor != nullptr && editor->hasPersistentFileName()) {
			std::strncpy(displayTitle, editor->persistentFileName(), sizeof(displayTitle) - 1);
			displayTitle[sizeof(displayTitle) - 1] = '\0';
		}
		refreshSyntaxContext();
		applyWindowColorThemeForPath(currentFileName());
		message(owner, evBroadcast, cmUpdateTitle, 0);
	}

	void refreshSyntaxContext() {
		if (editor != nullptr) editor->setSyntaxTitleHint(displayTitle);
	}

	bool shouldCollapseSelectionBeforeEditorInput(const TEvent &event) const {
		ushort key = 0;
		unsigned char ch = 0;

		if (editor == nullptr || mBlockMode == bmNone || mBlockMarkingOn || event.what != evKeyDown) return false;
		key = ctrlToArrow(event.keyDown.keyCode);
		ch = static_cast<unsigned char>(event.keyDown.charScan.charCode);
		if ((event.keyDown.controlKeyState & kbPaste) != 0 || event.keyDown.textLength > 0) return true;
		if (key == kbBack || key == kbDel || key == kbEnter || key == kbTab) return true;
		return ch >= 32 && ch < 255;
	}

	bool handleBuiltInBlockHotkeys(TEvent &event) {
		ushort keyCode = event.keyDown.keyCode;
		ushort mods = event.keyDown.controlKeyState;
		bool shift = (mods & kbShift) != 0;
		bool ctrl = (mods & kbCtrlShift) != 0;

		if (editor == nullptr || event.what != evKeyDown) return false;
		if (keyCode == kbCtrlF7 || (keyCode == kbF7 && ctrl && !shift)) {
			beginStreamBlock();
			clearEvent(event);
			return true;
		}
		if (keyCode == kbShiftF7 || (keyCode == kbF7 && shift && !ctrl)) {
			beginColumnBlock();
			clearEvent(event);
			return true;
		}
		if (keyCode == kbF7 && !shift && !ctrl) {
			if (mBlockMode != bmNone && mBlockMarkingOn) endBlock();
			else
				beginLineBlock();
			clearEvent(event);
			return true;
		}
		if (keyCode == kbCtrlF9 || (keyCode == kbF9 && ctrl && !shift)) {
			clearBlock();
			clearEvent(event);
			return true;
		}
		return false;
	}

	static bool isBlockShiftNavigationKey(ushort keyCode, ushort keyModifiers) {
		if ((keyModifiers & kbShift) == 0 || (keyModifiers & kbPaste) != 0) return false;
		switch (keyCode) {
			case kbLeft:
			case kbRight:
			case kbUp:
			case kbDown:
			case kbHome:
			case kbEnd:
			case kbPgUp:
			case kbPgDn:
			case kbCtrlLeft:
			case kbCtrlRight:
			case kbCtrlHome:
			case kbCtrlEnd:
				return true;
			default:
				return false;
		}
	}

	void applyPostInputBlockPolicy(bool markingBefore, ushort originalEvent, std::size_t selectionStartBefore, std::size_t selectionEndBefore, std::size_t bufferLengthBefore, std::size_t cursorBefore, ushort keyCodeBefore, ushort keyModifiersBefore) {
		if (editor == nullptr) return;
		if (originalEvent == evKeyDown && isBlockShiftNavigationKey(keyCodeBefore, keyModifiersBefore)) {
			const std::size_t currentCursor = std::min(editor->cursorOffset(), editor->bufferLength());
			const std::size_t anchorCursor = std::min(cursorBefore, bufferLengthBefore);

			if (mBlockMode == bmNone) mBlockMode = bmStream;
			if (!markingBefore) {
				mBlockMarkingOn = true;
				mBlockAnchor = static_cast<uint>(anchorCursor);
			}
			mBlockEnd = static_cast<uint>(currentCursor);
			syncBlockVisual();
			return;
		}
		if (originalEvent == evMouseDown && !markingBefore) {
			const std::size_t selectionStartNow = editor->selectionStartOffset();
			const std::size_t selectionEndNow = editor->selectionEndOffset();

			// Mouse drag selection is authoritative for the next committed stream block.
			if (selectionStartNow != selectionEndNow && (selectionStartNow != selectionStartBefore || selectionEndNow != selectionEndBefore)) {
				mBlockMode = bmStream;
				mBlockMarkingOn = false;
				mBlockAnchor = static_cast<uint>(selectionStartNow);
				mBlockEnd = static_cast<uint>(selectionEndNow);
				syncBlockVisual();
				return;
			}
		}
		if (originalEvent == evKeyDown && mBlockMode != bmNone && !mBlockMarkingOn && bufferLengthBefore != editor->bufferLength()) {
			const std::size_t currentLength = editor->bufferLength();
			const std::size_t normalizedCursorBefore = std::min(cursorBefore, bufferLengthBefore);
			std::size_t changePos = selectionStartBefore;
			long delta = static_cast<long>(currentLength) - static_cast<long>(bufferLengthBefore);
			uint startPtr = 0;
			uint endPtr = 0;
			uint normalizedStart = 0;
			uint normalizedEnd = 0;
			auto shiftPtr = [&](uint &ptr) {
				long shifted = static_cast<long>(ptr) + delta;
				if (shifted < 0) shifted = 0;
				if (shifted > static_cast<long>(currentLength)) shifted = static_cast<long>(currentLength);
				ptr = static_cast<uint>(shifted);
			};

			if (selectionStartBefore == selectionEndBefore) {
				changePos = normalizedCursorBefore;
				if (keyCodeBefore == kbBack && normalizedCursorBefore > 0) changePos = normalizedCursorBefore - 1;
			}

			startPtr = std::min(mBlockAnchor, mBlockEnd);
			endPtr = std::max(mBlockAnchor, mBlockEnd);
			normalizedStart = startPtr;
			normalizedEnd = endPtr;

			if (changePos <= normalizedStart) {
				shiftPtr(mBlockAnchor);
				shiftPtr(mBlockEnd);
			} else if (changePos < normalizedEnd && mBlockMode == bmStream) {
				if (mBlockAnchor <= mBlockEnd) shiftPtr(mBlockEnd);
				else
					shiftPtr(mBlockAnchor);
			}
		}
		// Drag-release with the mouse finalizes marking in the active block mode.
		if (markingBefore && originalEvent == evMouseDown && mBlockMarkingOn) {
			endBlock();
			return;
		}
		if (mBlockMode != bmNone) syncBlockVisual();
	}

	void updateTaskMarkers() {
		std::size_t taskCount = mTrackedCoprocessorTasks.size();
		if (editor != nullptr) {
			if (editor->pendingLineIndexWarmupTaskId() != 0) ++taskCount;
			if (editor->pendingSyntaxWarmupTaskId() != 0) ++taskCount;
			if (editor->pendingFoldWarmupTaskId() != 0) ++taskCount;
			if (editor->pendingMiniMapWarmupTaskId() != 0) ++taskCount;
			if (editor->pendingSaveNormalizationWarmupTaskId() != 0) ++taskCount;
		}
		if (indicator != nullptr) indicator->setTaskCount(taskCount);
	}

	void cancelTrackedCoprocessorTasks() {
		for (std::size_t i = 0; i < mTrackedCoprocessorTasks.size(); ++i) {
			mrTraceCoprocessorTaskCancel(mBufferId, mTrackedCoprocessorTasks[i].id);
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mTrackedCoprocessorTasks[i].id));
		}
		mTrackedCoprocessorTasks.clear();
		updateTaskMarkers();
	}

	void beginBlock(BlockMode mode) {
		if (editor == nullptr) return;
		discardHiddenBlockState();
		mBlockMode = mode;
		mBlockMarkingOn = true;
		mBlockAnchor = static_cast<uint>(editor->cursorOffset());
		mBlockEnd = static_cast<uint>(editor->cursorOffset());
		syncBlockVisual();
	}

	uint effectiveBlockEnd() const {
		if (editor != nullptr && mBlockMarkingOn) return static_cast<uint>(editor->cursorOffset());
		return mBlockEnd;
	}

	void blockPtrRange(uint &a, uint &b) const {
		a = mBlockAnchor;
		b = effectiveBlockEnd();
		if (a > b) std::swap(a, b);
	}

	int lineNumberForPtr(uint ptr) const {
		uint pos = 0;
		int line = 1;
		if (editor == nullptr) return 0;
		if (ptr > editor->bufferLength()) ptr = static_cast<uint>(editor->bufferLength());
		while (pos < ptr && pos < editor->bufferLength()) {
			uint next = static_cast<uint>(editor->nextLineOffset(pos));
			if (next <= pos || next > ptr) break;
			pos = next;
			++line;
		}
		return line;
	}

	int columnForPtr(uint ptr) const {
		uint start;
		if (editor == nullptr) return 0;
		if (ptr > editor->bufferLength()) ptr = static_cast<uint>(editor->bufferLength());
		start = static_cast<uint>(editor->lineStartOffset(ptr));
		return editor->charColumn(start, ptr) + 1;
	}

  public:
	static std::string baseNameOf(const std::string &path) {
		std::size_t pos = path.find_last_of("\\/");
		return pos == std::string::npos ? path : path.substr(pos + 1);
	}

	static std::string stripExtension(const std::string &name) {
		std::size_t dot = name.find_last_of('.');
		if (dot == std::string::npos || dot == 0) return name;
		return name.substr(0, dot);
	}

	static std::string trimTaskLabel(const std::string &label) {
		std::size_t start = label.find_first_not_of(' ');
		std::size_t end = label.find_last_not_of(' ');
		if (start == std::string::npos) return std::string();
		return label.substr(start, end - start + 1);
	}

	static std::string compactTaskLabel(const TrackedTask &task) {
		std::string label = trimTaskLabel(task.label);

		if (label.empty()) return label;
		if (task.kind == mr::coprocessor::TaskKind::MacroJob) return stripExtension(baseNameOf(label));
		if (task.kind == mr::coprocessor::TaskKind::ExternalIo) {
			std::size_t split = label.find_first_of(" \t");
			std::string head = split == std::string::npos ? label : label.substr(0, split);
			return baseNameOf(head);
		}
		return baseNameOf(label);
	}

	static std::string compactTaskLabel(mr::coprocessor::TaskKind kind, const std::string &taskLabel) {
		TrackedTask task;
		task.kind = kind;
		task.label = taskLabel;
		return compactTaskLabel(task);
	}

	static std::string compactTaskLabelLimited(mr::coprocessor::TaskKind kind, const std::string &taskLabel, std::size_t maxChars) {
		std::string label = compactTaskLabel(kind, taskLabel);

		if (label.size() <= maxChars) return label;
		if (maxChars <= 3) return label.substr(0, maxChars);
		return label.substr(0, maxChars - 3) + "...";
	}

	static const char *laneLabel(mr::coprocessor::Lane lane) noexcept {
		switch (lane) {
			case mr::coprocessor::Lane::Io:
				return "IO";
			case mr::coprocessor::Lane::Compute:
				return "Compute";
			case mr::coprocessor::Lane::MiniMap:
				return "MiniMap";
			case mr::coprocessor::Lane::Macro:
				return "Macro";
			case mr::coprocessor::Lane::Extern:
				return "Extern";
		}
		return "Lane";
	}

	static const char *taskKindLabel(mr::coprocessor::TaskKind kind) noexcept {
		switch (kind) {
			case mr::coprocessor::TaskKind::LineIndexWarmup:
				return "LineIndex";
			case mr::coprocessor::TaskKind::SyntaxWarmup:
				return "Syntax";
			case mr::coprocessor::TaskKind::FoldWarmup:
				return "Folding";
			case mr::coprocessor::TaskKind::MiniMapWarmup:
				return "MiniMap";
			case mr::coprocessor::TaskKind::SaveNormalizationWarmup:
				return "SaveCache";
			case mr::coprocessor::TaskKind::IndicatorBlink:
				return "Indicator";
			case mr::coprocessor::TaskKind::ExternalIo:
				return "ExternalIO";
			case mr::coprocessor::TaskKind::MacroJob:
				return "Macro";
			case mr::coprocessor::TaskKind::Custom:
			default:
				return "Task";
		}
	}

	static std::string formatMicrosElapsed(std::uint64_t micros) {
		char buffer[32];
		double elapsedMs = static_cast<double>(micros) / 1000.0;

		if (elapsedMs < 1000.0) {
			std::snprintf(buffer, sizeof(buffer), "%.0f ms", elapsedMs);
			return buffer;
		}
		std::snprintf(buffer, sizeof(buffer), "%.2f s", elapsedMs / 1000.0);
		return buffer;
	}

	static std::string formatTaskElapsed(const TrackedTask &task) {
		using namespace std::chrono;
		char buffer[32];
		double elapsedMs = duration_cast<milliseconds>(steady_clock::now() - task.startedAt).count();

		if (elapsedMs < 1000.0) {
			std::snprintf(buffer, sizeof(buffer), "%.0f ms", elapsedMs);
			return buffer;
		}
		std::snprintf(buffer, sizeof(buffer), "%.2f s", elapsedMs / 1000.0);
		return buffer;
	}

	static std::string taskActivityBullet() {
		using namespace std::chrono;
		static const std::array<const char *, 12> kClockFrames = {"🕛", "🕐", "🕑", "🕒", "🕓", "🕔", "🕕", "🕖", "🕗", "🕘", "🕙", "🕚"};
		static const std::array<const char *, 4> kAsciiFrames = {"|", "/", "-", "\\"};
		const long long elapsedMs = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
		const std::size_t frame = static_cast<std::size_t>((elapsedMs / 200) % kClockFrames.size());

		if (strwidth(kClockFrames[0]) >= 1) return kClockFrames[frame];
		return kAsciiFrames[frame % kAsciiFrames.size()];
	}

	static const char *lineIndexWarmingLabel() noexcept {
		return "Line index warming";
	}

	static const char *syntaxWarmingLabel() noexcept {
		return "Syntax warming";
	}

	static const char *miniMapRenderingLabel() noexcept {
		return "Mini map rendering";
	}

	static const char *saveNormalizationWarmingLabel() noexcept {
		return "Save cache warming";
	}

	int normalizedBlockLine1() const {
		uint a, b;
		if (mBlockMode == bmNone) return 0;
		blockPtrRange(a, b);
		return lineNumberForPtr(a);
	}

	int normalizedBlockLine2() const {
		uint a, b;
		if (mBlockMode == bmNone) return 0;
		blockPtrRange(a, b);
		return lineNumberForPtr(b);
	}

	int normalizedBlockCol1() const {
		int aCol;
		int bCol;
		if (mBlockMode == bmNone) return 0;
		if (mBlockMode == bmLine) return 1;
		aCol = columnForPtr(mBlockAnchor);
		bCol = columnForPtr(effectiveBlockEnd());
		return std::min(aCol, bCol);
	}

	int normalizedBlockCol2() const {
		int aCol;
		int bCol;
		if (mBlockMode == bmNone) return 0;
		if (mBlockMode == bmLine) return 1000;
		aCol = columnForPtr(mBlockAnchor);
		bCol = columnForPtr(effectiveBlockEnd());
		return std::max(aCol, bCol);
	}

  public:
	int mVirtualDesktop = 1;

  private:
	void syncBlockVisual() {
		uint a;
		uint b;
		if (editor == nullptr) return;
		if (mBlockMode == bmStream) {
			blockPtrRange(a, b);
			editor->setBlockOverlayState(static_cast<int>(mBlockMode), a, b, true, mBlockMarkingOn);
			editor->setSelectionOffsets(a, b, False);
		} else if (mBlockMode == bmLine) {
			blockPtrRange(a, b);
			editor->setBlockOverlayState(static_cast<int>(mBlockMode), a, b, true, mBlockMarkingOn);
			a = static_cast<uint>(editor->lineStartOffset(a));
			b = static_cast<uint>(editor->nextLineOffset(b));
			if (b > editor->bufferLength()) b = static_cast<uint>(editor->bufferLength());
			editor->setSelectionOffsets(a, b, False);
		} else if (mBlockMode == bmColumn) {
			blockPtrRange(a, b);
			editor->setBlockOverlayState(static_cast<int>(mBlockMode), a, b, true, mBlockMarkingOn);
			editor->setSelectionOffsets(editor->cursorOffset(), editor->cursorOffset(), False);
		} else {
			editor->setBlockOverlayState(0, 0, 0, false, false);
			editor->setSelectionOffsets(editor->cursorOffset(), editor->cursorOffset(), False);
		}
		editor->revealCursor(False);
		editor->update(ufView);
	}

	TScrollBar *vScrollBar;
	TScrollBar *hScrollBar;
	MRIndicator *indicator;
	MRFileEditor *editor;
	int mBufferId;
	bool mFirstSaveDone;
	bool mTemporaryFileUsed;
	std::string mTemporaryFileName;
	int mIndentLevel;
	BlockMode mBlockMode;
	bool mBlockMarkingOn;
	uint mBlockAnchor;
	uint mBlockEnd;
	bool mHiddenBlockValid;
	BlockMode mHiddenBlockMode;
	bool mHiddenBlockMarkingOn;
	uint mHiddenBlockAnchor;
	uint mHiddenBlockEnd;
	bool mColumnSortAscending;
	std::vector<TrackedTask> mTrackedCoprocessorTasks;
	WindowRole mWindowRole;
	std::string mWindowRoleDetail;
	std::size_t mMacroQueuedCount;
	std::size_t mMacroCompletedCount;
	std::size_t mMacroConflictCount;
	std::size_t mMacroCancelledCount;
	std::size_t mMacroFailedCount;
	std::string mLastMacroSummaryText;
	mutable std::array<TColorAttr, 12> mWindowPaletteData;
	mutable TPalette mWindowPalette;
	bool mCustomEofMarkerColorValid;
	TColorAttr mCustomEofMarkerColor;
	bool mClosePrepared;
	bool mMinimized;
	bool mBufferedBeforeMinimize;
	TRect mRestoreBounds;
	TRect mLastMinimizedBounds;
	char displayTitle[MAXPATH];
};

#endif
