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
#include <tvision/tv.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
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
#include "../dialogs/MRWindowListDialog.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../mrmac/mrvm.hpp"

void mrTraceCoprocessorTaskCancel(int bufferId, std::uint64_t taskId);
class MREditWindow;
void setWindowManuallyHidden(MREditWindow *win, bool hidden);

class MREditWindow : public TWindow {
  public:
	struct TrackedTask {
		std::uint64_t id;
		mr::coprocessor::TaskKind kind;
		std::string label;
		std::chrono::steady_clock::time_point startedAt;

		TrackedTask() noexcept
		    : id(0), kind(mr::coprocessor::TaskKind::Custom), label(), startedAt(std::chrono::steady_clock::now()) {
		}

		TrackedTask(std::uint64_t aId, mr::coprocessor::TaskKind aKind, std::string aLabel = std::string())
		    : id(aId), kind(aKind), label(std::move(aLabel)), startedAt(std::chrono::steady_clock::now()) {
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

	MREditWindow(const TRect &bounds, const char *title, int aNumber)
	    : TWindowInit(&MREditWindow::initFrame), TWindow(bounds, 0, aNumber), vScrollBar(nullptr),
	      hScrollBar(nullptr), indicator(nullptr), editor(nullptr), mBufferId(allocateBufferId()),
	      mFirstSaveDone(false), mTemporaryFileUsed(false), mTemporaryFileName(), mIndentLevel(1),
	      mBlockMode(bmNone), mBlockMarkingOn(false), mBlockAnchor(0), mBlockEnd(0),
	      mTrackedCoprocessorTasks(), mWindowRole(wrText), mWindowRoleDetail(), mMacroQueuedCount(0),
	      mMacroCompletedCount(0), mMacroConflictCount(0), mMacroCancelledCount(0), mMacroFailedCount(0),
	      mLastMacroSummaryText(), mWindowPaletteData(defaultWindowPaletteData()),
	      mWindowPalette(mWindowPaletteData.data(), static_cast<ushort>(mWindowPaletteData.size())),
	      mCustomEofMarkerColorValid(false), mCustomEofMarkerColor(0) {
		options |= ofTileable;

		std::strncpy(displayTitle, (title != nullptr && *title != '\0') ? title : "Untitled",
		             sizeof(displayTitle) - 1);
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
				const bool hasTaskSlot = indicator != nullptr && indicator->hasTaskMarkerSlot();
				const bool showTaskIcon = indicator != nullptr && indicator->shouldDrawTaskMarker();
				const bool hasReadOnlySlot = indicator != nullptr && indicator->hasReadOnlyMarkerSlot();
				const bool showReadOnlyIcon = indicator != nullptr && indicator->shouldDrawReadOnlyMarker();
				const bool isActiveWindow = (this->state & sfActive) != 0;
				const bool showRecordingSlot = isActiveWindow && mrIsKeystrokeRecordingActive();
				const bool showRecordingIcon = showRecordingSlot && mrIsKeystrokeRecordingMarkerVisible();
				const bool showMacroBrainSlot = isActiveWindow && mrIsMacroBrainMarkerActive();
				const bool showMacroBrainIcon = showMacroBrainSlot && mrIsMacroBrainMarkerVisible();
				return MRFrame::MarkerState(isFileChanged(), insertModeEnabled(), hasTaskSlot, showTaskIcon,
				                             hasReadOnlySlot, showReadOnlyIcon, showRecordingSlot,
				                             showRecordingIcon, showMacroBrainSlot, showMacroBrainIcon);
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
		cancelTrackedCoprocessorTasks();
		mrNotifyWindowTopologyChanged();
	}

	virtual TPalette &getPalette() const override {
		return mWindowPalette;
	}

	virtual TColorAttr mapColor(uchar index) override {
		if (index >= 1 && index <= mWindowPaletteData.size())
			return mWindowPaletteData[index - 1];
		return TWindow::mapColor(index);
	}

	virtual const char *getTitle(short) override {
		if (editor != nullptr && editor->hasPersistentFileName())
			return editor->persistentFileName();
		return displayTitle;
	}

	void setState(ushort aState, Boolean enable) override {
		TWindow::setState(aState, enable);
		if ((aState & (sfFocused | sfSelected | sfActive)) != 0 && frame != nullptr) {
			frame->drawView();
			if (hScrollBar != nullptr)
				hScrollBar->drawView();
			if (vScrollBar != nullptr)
				vScrollBar->drawView();
			if (indicator != nullptr)
				indicator->drawView();
		}
	}

	void dragView(TEvent &event, uchar mode, TRect &limits, TPoint minSize, TPoint maxSize) override {
		MRWindowManager::handleDragView(this, event, mode, limits, minSize, maxSize);
	}

	void changeBounds(const TRect &bounds) override {
		TWindow::changeBounds(bounds);
		layoutEditorChrome();
		if (hScrollBar != nullptr)
			hScrollBar->drawView();
		if (vScrollBar != nullptr)
			vScrollBar->drawView();
		if (indicator != nullptr)
			indicator->drawView();
	}

	virtual void handleEvent(TEvent &event) override {
			if (event.what == evCommand && event.message.command == cmClose && mWindowRole == wrLog) {
				setWindowManuallyHidden(this, true);
				hide();
				static_cast<void>(mrEnsureUsableWorkWindow());
				clearEvent(event);
				return;
			}
			const ushort originalEvent = event.what;
			const ushort keyCodeBefore =
			    event.what == evKeyDown ? ctrlToArrow(event.keyDown.keyCode) : static_cast<ushort>(0);
			const ushort keyModifiersBefore =
			    event.what == evKeyDown ? event.keyDown.controlKeyState : static_cast<ushort>(0);
			const bool markingBefore = mBlockMarkingOn;
			const std::size_t bufferLengthBefore = editor != nullptr ? editor->bufferLength() : 0;
			const std::size_t cursorBefore = editor != nullptr ? editor->cursorOffset() : 0;
			const std::size_t selectionStartBefore =
			    editor != nullptr ? editor->selectionStartOffset() : 0;
				const std::size_t selectionEndBefore =
				    editor != nullptr ? editor->selectionEndOffset() : 0;

				maybeTraceTabKeyEvent(event);
				traceCalculatorHotkeyEvent("window-pre", event);

				if (event.what == evMouseDown && editor != nullptr &&
				    (event.mouse.buttons & mbLeftButton) != 0 && mBlockMode != bmNone && !mBlockMarkingOn) {
				// Keep state, but hide committed block overlay while the mouse selection loop runs,
				// so the live growing selection is visible.
				editor->setBlockOverlayState(0, 0, 0, false, false);
			}
			if (event.what == evKeyDown &&
			    TKey(event.keyDown.keyCode, event.keyDown.controlKeyState) == TKey(kbShiftTab)) {
				event.keyDown.keyCode = kbShiftTab;
				event.keyDown.controlKeyState |= kbShift;
			}
			if (event.what == evKeyDown) {
				if (keyDebugEnabled() &&
				    TKey(event.keyDown.keyCode, event.keyDown.controlKeyState) == TKey(kbShiftTab)) {
					char line[192];
					std::snprintf(line, sizeof(line),
					              "KEYDBG shifttab stage=window-pre keyCode=0x%04X mods=0x%04X cursor=%zu",
					              static_cast<unsigned>(event.keyDown.keyCode),
					              static_cast<unsigned>(event.keyDown.controlKeyState),
					              editor != nullptr ? editor->cursorOffset() : 0);
					mrLogMessage(line);
				}
					if (handleBuiltInBlockHotkeys(event))
						return;
					std::string executedMacroName;
					if (mrvmRunAssignedMacroForKey(event.keyDown.keyCode, event.keyDown.controlKeyState,
					                               executedMacroName, nullptr)) {
						if (isCalculatorHotkeyEvent(event)) {
							std::string detail = "macro=" + executedMacroName;
							traceCalculatorHotkeyEvent("window-macro-consumed", event, detail.c_str());
						}
						if (keyDebugEnabled()) {
							char line[224];
							std::snprintf(line, sizeof(line),
							              "KEYDBG shifttab stage=macro-consumed macro='%s'",
							              executedMacroName.c_str());
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
							std::snprintf(
							    line, sizeof(line),
							    "KEYDBG shifttab stage=editor-dispatch eventBefore=0x%04X eventAfter=0x%04X cursorBefore=%zu cursorAfter=%zu",
							    static_cast<unsigned>(eventTypeBeforeEditor),
							    static_cast<unsigned>(event.what), cursorStart, editor->cursorOffset());
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
				if ((event.what & (evMouseDown | evMouseMove | evMouseUp)) != 0)
					mrFrame->updateTaskHover(event.mouse.where, false);
				else if ((event.what & (evKeyDown | evCommand)) != 0)
					mrFrame->updateTaskHover(TPoint(), true);
			}

				TWindow::handleEvent(event);
				traceCalculatorHotkeyEvent("window-post", event);
				if (keyDebugEnabled() && originalEvent == evKeyDown &&
				    TKey(keyCodeBefore, keyModifiersBefore) == TKey(kbShiftTab)) {
					char line[192];
				std::snprintf(line, sizeof(line),
				              "KEYDBG shifttab stage=window-post event=0x%04X cursor=%zu",
				              static_cast<unsigned>(event.what),
				              editor != nullptr ? editor->cursorOffset() : 0);
				mrLogMessage(line);
			}
			if ((originalEvent & (evKeyDown | evMouseDown | evMouseMove | evMouseUp)) != 0)
				applyPostInputBlockPolicy(markingBefore, originalEvent, selectionStartBefore,
				                          selectionEndBefore, bufferLengthBefore, cursorBefore,
				                          keyCodeBefore, keyModifiersBefore);
			if (event.what == evBroadcast && event.message.command == cmUpdateTitle) {
				updateTaskMarkers();
				if (frame != nullptr)
					frame->drawView();
				clearEvent(event);
			}
		}

	bool loadFromFile(const char *fileName) {
		std::string expandedName;
		std::string loadError;

		if (editor == nullptr || fileName == nullptr || *fileName == '\0')
			return false;

		expandedName = fileName;
		if (expandedName.size() >= editor->persistentFileNameCapacity())
			return false;
		char expandedPath[MAXPATH];
		strnzcpy(expandedPath, expandedName.c_str(), sizeof(expandedPath));
		fexpand(expandedPath);
		expandedName = expandedPath;

		if (!editor->loadMappedFile(expandedName.c_str(), loadError))
			return false;

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
		if (editor == nullptr)
			return false;
		if (!editor->replaceBufferText(text))
			return false;

		resetWindowColorsToConfiguredDefaults();
		resetTransientEditorState();
		setReadOnly(false);
		mTemporaryFileUsed = false;
		mTemporaryFileName.clear();
		editor->clearPersistentFileName();
		setWindowRole(wrText);
		if (title != nullptr && *title != '\0')
			setDisplayTitle(title);
		else
			updateTitleFromEditor();
		refreshSyntaxContext();
		return true;
	}

	bool saveCurrentFile() {
		if (editor == nullptr || isReadOnly() || !editor->canSaveInPlace())
			return false;

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
		if (editor == nullptr || isReadOnly() || !editor->canSaveAs())
			return false;

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

	const char *currentFileName() const {
		if (editor != nullptr && editor->hasPersistentFileName())
			return editor->persistentFileName();
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
		if (editor != nullptr)
			editor->setReadOnly(readOnly);
		if (indicator != nullptr)
			indicator->setReadOnly(readOnly);
		if (readOnly && editor != nullptr)
			editor->setDocumentModified(false);
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
		return mWindowRole == wrCommunicationCommand || mWindowRole == wrCommunicationPipe ||
		       mWindowRole == wrCommunicationDevice;
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
		if (editor != nullptr)
			editor->setDocumentModified(changed && !isReadOnly());
	}

	void setCurrentFileName(const char *fileName) {
		if (editor == nullptr)
			return;

		if (fileName == nullptr || *fileName == '\0')
			editor->clearPersistentFileName();
		else {
			editor->setPersistentFileName(fileName);
			setWindowRole(wrFile);
		}
		if ((fileName == nullptr || *fileName == '\0') && mWindowRole == wrFile)
			setWindowRole(wrText);
		updateTitleFromEditor();
		refreshSyntaxContext();
	}

	bool confirmAbandonForReload() {
		if (editor == nullptr)
			return false;
		return editor->valid(cmClose) == True;
	}

	bool replaceTextBuffer(const char *text, const char *title = nullptr) {
		if (editor == nullptr || !editor->replaceBufferText(text))
			return false;
		if (title != nullptr && *title != '\0')
			setDisplayTitle(title);
		return true;
	}

	bool appendTextBuffer(const char *text) {
		if (editor == nullptr)
			return false;
		return editor->appendBufferText(text);
	}

	void setDisplayTitle(const char *title) {
		std::strncpy(displayTitle, (title != nullptr && *title != '\0') ? title : "Untitled",
		             sizeof(displayTitle) - 1);
		displayTitle[sizeof(displayTitle) - 1] = '\0';
		refreshSyntaxContext();
		message(owner, evBroadcast, cmUpdateTitle, 0);
	}

	void trackCoprocessorTask(std::uint64_t taskId,
	                          mr::coprocessor::TaskKind kind = mr::coprocessor::TaskKind::Custom,
	                          const std::string &label = std::string()) {
		if (taskId == 0)
			return;
		for (std::size_t i = 0; i < mTrackedCoprocessorTasks.size(); ++i)
			if (mTrackedCoprocessorTasks[i].id == taskId)
				return;
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
		if (count != 1)
			mLastMacroSummaryText += "s";
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
			if (mTrackedCoprocessorTasks[i].kind == mr::coprocessor::TaskKind::MacroJob)
				++count;
		return count;
	}

	std::size_t trackedTaskCount(mr::coprocessor::TaskKind kind) const noexcept {
		std::size_t count = 0;
		for (std::size_t i = 0; i < mTrackedCoprocessorTasks.size(); ++i)
			if (mTrackedCoprocessorTasks[i].kind == kind)
				++count;
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
				if (editor->pendingLineIndexWarmupTaskId() != 0 &&
				    trackedTaskCount(mr::coprocessor::TaskKind::LineIndexWarmup) == 0)
					lines.push_back(bullet + " " + lineIndexWarmingLabel());
				if (editor->pendingSyntaxWarmupTaskId() != 0 &&
				    trackedTaskCount(mr::coprocessor::TaskKind::SyntaxWarmup) == 0)
					lines.push_back(bullet + " " + syntaxWarmingLabel());
				if (editor->pendingMiniMapWarmupTaskId() != 0 &&
				    trackedTaskCount(mr::coprocessor::TaskKind::MiniMapWarmup) == 0)
					lines.push_back(bullet + " " + miniMapRenderingLabel());
				if (editor->pendingSaveNormalizationWarmupTaskId() != 0 &&
				    trackedTaskCount(mr::coprocessor::TaskKind::SaveNormalizationWarmup) == 0)
					lines.push_back(bullet + " " + saveNormalizationWarmingLabel());
			}
		return lines;
	}

	bool cancelTrackedMacroTasks() {
		bool cancelledAny = false;
		std::size_t macroCount = trackedMacroTaskCount();

		for (std::size_t i = 0; i < mTrackedCoprocessorTasks.size(); ++i) {
			if (mTrackedCoprocessorTasks[i].kind != mr::coprocessor::TaskKind::MacroJob)
				continue;
			mrTraceCoprocessorTaskCancel(mBufferId, mTrackedCoprocessorTasks[i].id);
			if (mr::coprocessor::globalCoprocessor().cancelTask(mTrackedCoprocessorTasks[i].id))
				cancelledAny = true;
		}
		if (cancelledAny)
			noteBackgroundMacroCancelRequested(macroCount);
		return cancelledAny;
	}

	bool cancelTrackedExternalIoTasks() {
		bool cancelledAny = false;

		for (std::size_t i = 0; i < mTrackedCoprocessorTasks.size(); ++i) {
			if (mTrackedCoprocessorTasks[i].kind != mr::coprocessor::TaskKind::ExternalIo)
				continue;
			mrTraceCoprocessorTaskCancel(mBufferId, mTrackedCoprocessorTasks[i].id);
			if (mr::coprocessor::globalCoprocessor().cancelTask(mTrackedCoprocessorTasks[i].id))
				cancelledAny = true;
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
		if (indicator != nullptr)
			indicator->showStatusNotice(text, kind);
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
		if (editor != nullptr)
			return editor->mappedOriginalPath();
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

	std::uint64_t pendingMiniMapWarmupTaskId() const noexcept {
		return editor != nullptr ? editor->pendingMiniMapWarmupTaskId() : 0;
	}

	std::uint64_t pendingSaveNormalizationWarmupTaskId() const noexcept {
		return editor != nullptr ? editor->pendingSaveNormalizationWarmupTaskId() : 0;
	}

	bool usesApproximateMetrics() const noexcept {
		return editor != nullptr && editor->usesApproximateMetrics();
	}

	void releaseCoprocessorTask(std::uint64_t taskId) {
		for (std::vector<TrackedTask>::iterator it = mTrackedCoprocessorTasks.begin();
		     it != mTrackedCoprocessorTasks.end(); ++it)
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
		if (level < 1)
			level = 1;
		if (level > 254)
			level = 254;
		mIndentLevel = level;
	}

	enum BlockMode { bmNone = 0, bmLine = 1, bmColumn = 2, bmStream = 3 };

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
		if (editor == nullptr || mBlockMode == bmNone)
			return;
		mBlockEnd = static_cast<uint>(editor->cursorOffset());
		mBlockMarkingOn = false;
		syncBlockVisual();
	}

	void clearBlock() {
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

	void applyCommittedBlockState(int mode, bool markingOn, uint anchor, uint end) {
		if (editor == nullptr)
			return;
		if (mode < bmNone || mode > bmStream)
			mode = bmNone;
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


	void resetWindowColorsToConfiguredDefaults() {
		mWindowPaletteData = defaultWindowPaletteData();
		rebuildWindowPalette();
		mCustomEofMarkerColorValid = false;
		if (editor != nullptr)
			editor->setWindowEofMarkerColorOverride(false);
		refreshWindowPaletteViews();
	}

	void applyWindowColorThemeForPath(const std::string &path) {
		std::array<unsigned char, MRColorSetupSettings::kWindowCount> colors;
		std::string themePath;
		std::string errorText;
		resetWindowColorsToConfiguredDefaults();
		if (!effectiveEditWindowColorThemePathForPath(path, themePath, nullptr) || themePath.empty())
			return;
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
		if (editor != nullptr)
			editor->setWindowEofMarkerColorOverride(true, mCustomEofMarkerColor);
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

	static bool isTabRelatedKeyEvent(const TEvent &event) noexcept {
		if (event.what != evKeyDown)
			return false;
		if (event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbShiftTab ||
		    event.keyDown.keyCode == kbCtrlI)
			return true;
		return TKey(event.keyDown.keyCode, event.keyDown.controlKeyState) == TKey(kbTab) ||
		       TKey(event.keyDown.keyCode, event.keyDown.controlKeyState) == TKey(kbShiftTab);
	}

		static void maybeTraceTabKeyEvent(const TEvent &event) {
			if (!keyDebugEnabled() || !isTabRelatedKeyEvent(event))
				return;
			const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);
		char line[256];
		std::snprintf(
		    line, sizeof(line),
		    "KEYDBG tab rawCode=0x%04X rawMods=0x%04X normCode=0x%04X normMods=0x%04X textLen=%u char=0x%02X",
		    static_cast<unsigned>(event.keyDown.keyCode),
		    static_cast<unsigned>(event.keyDown.controlKeyState),
		    static_cast<unsigned>(normalized.code), static_cast<unsigned>(normalized.mods),
		    static_cast<unsigned>(event.keyDown.textLength),
		    static_cast<unsigned>(static_cast<unsigned char>(event.keyDown.charScan.charCode)));
			mrLogMessage(line);
		}

		static bool isCalculatorHotkeyEvent(const TEvent &event) noexcept {
			if (event.what != evKeyDown)
				return false;
			const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);
			return (normalized.mods & kbAltShift) != 0 && normalized.code == 'C';
		}

		static void traceCalculatorHotkeyEvent(const char *stage, const TEvent &event,
		                                       const char *detail = nullptr) {
			if (!isCalculatorHotkeyEvent(event))
				return;
			const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);
			char line[320];
			std::snprintf(line, sizeof(line),
			              "KEYDBG calc stage=%s rawCode=0x%04X rawMods=0x%04X normCode=0x%04X normMods=0x%04X textLen=%u char=0x%02X%s%s",
			              stage, static_cast<unsigned>(event.keyDown.keyCode),
			              static_cast<unsigned>(event.keyDown.controlKeyState),
			              static_cast<unsigned>(normalized.code), static_cast<unsigned>(normalized.mods),
			              static_cast<unsigned>(event.keyDown.textLength),
			              static_cast<unsigned>(static_cast<unsigned char>(event.keyDown.charScan.charCode)),
			              detail != nullptr ? " " : "", detail != nullptr ? detail : "");
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
		if (configuredColorSlotOverride(slot, overrideValue))
			value = overrideValue;
		return static_cast<TColorAttr>(value);
	}

	static std::array<TColorAttr, 12> defaultWindowPaletteData() noexcept {
		return {configuredWindowPaletteSlot(8), configuredWindowPaletteSlot(9), configuredWindowPaletteSlot(10),
		        configuredWindowPaletteSlot(11), configuredWindowPaletteSlot(12), configuredWindowPaletteSlot(13),
		        configuredWindowPaletteSlot(14), configuredWindowPaletteSlot(15),
		        configuredWindowPaletteSlot(kMrPaletteCurrentLine),
		        configuredWindowPaletteSlot(kMrPaletteCurrentLineInBlock),
		        configuredWindowPaletteSlot(kMrPaletteChangedText),
		        configuredWindowPaletteSlot(kMrPaletteLineNumbers)};
	}

	void rebuildWindowPalette() {
		mWindowPalette = TPalette(mWindowPaletteData.data(), static_cast<ushort>(mWindowPaletteData.size()));
	}

	void refreshWindowPaletteViews() {
		drawView();
		if (frame != nullptr)
			frame->drawView();
		if (hScrollBar != nullptr)
			hScrollBar->drawView();
		if (vScrollBar != nullptr)
			vScrollBar->drawView();
		if (indicator != nullptr)
			indicator->drawView();
		if (editor != nullptr)
			editor->drawView();
	}

	MRFileEditor *createEditor(const TRect &bounds, const char *fileName) {
		return new MRFileEditor(bounds, hScrollBar, vScrollBar, indicator, fileName != nullptr ? fileName : "");
	}

	void layoutEditorChrome() {
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
		if (fileName == nullptr || *fileName == '\0')
			return false;
		if (access(fileName, F_OK) != 0)
			return false;
		return access(fileName, W_OK) != 0;
	}

	void resetTransientEditorState() {
		if (editor == nullptr)
			return;
		clearBlock();
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
		if (editor != nullptr)
			editor->setSyntaxTitleHint(displayTitle);
	}

	bool shouldCollapseSelectionBeforeEditorInput(const TEvent &event) const {
		ushort key = 0;
		unsigned char ch = 0;

		if (editor == nullptr || mBlockMode == bmNone || mBlockMarkingOn || event.what != evKeyDown)
			return false;
		key = ctrlToArrow(event.keyDown.keyCode);
		ch = static_cast<unsigned char>(event.keyDown.charScan.charCode);
		if ((event.keyDown.controlKeyState & kbPaste) != 0 || event.keyDown.textLength > 0)
			return true;
		if (key == kbBack || key == kbDel || key == kbEnter || key == kbTab)
			return true;
		return ch >= 32 && ch < 255;
	}

	bool handleBuiltInBlockHotkeys(TEvent &event) {
		ushort keyCode = event.keyDown.keyCode;
		ushort mods = event.keyDown.controlKeyState;
		bool shift = (mods & kbShift) != 0;
		bool ctrl = (mods & kbCtrlShift) != 0;

		if (editor == nullptr || event.what != evKeyDown)
			return false;
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
			if (mBlockMode != bmNone && mBlockMarkingOn)
				endBlock();
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
			if ((keyModifiers & kbShift) == 0 || (keyModifiers & kbPaste) != 0)
				return false;
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

		void applyPostInputBlockPolicy(bool markingBefore, ushort originalEvent,
		                               std::size_t selectionStartBefore,
		                               std::size_t selectionEndBefore,
		                               std::size_t bufferLengthBefore,
		                               std::size_t cursorBefore, ushort keyCodeBefore,
		                               ushort keyModifiersBefore) {
			if (editor == nullptr)
				return;
			if (originalEvent == evKeyDown &&
			    isBlockShiftNavigationKey(keyCodeBefore, keyModifiersBefore)) {
				const std::size_t currentCursor =
				    std::min(editor->cursorOffset(), editor->bufferLength());
				const std::size_t anchorCursor = std::min(cursorBefore, bufferLengthBefore);

				if (mBlockMode == bmNone)
					mBlockMode = bmStream;
				if (!markingBefore) {
					mBlockMarkingOn = true;
					mBlockAnchor = static_cast<uint>(anchorCursor);
				}
				mBlockEnd = static_cast<uint>(currentCursor);
				syncBlockVisual();
				return;
			}
			if (originalEvent == evMouseDown && mBlockMode == bmNone) {
				const std::size_t selectionStartNow = editor->selectionStartOffset();
				const std::size_t selectionEndNow = editor->selectionEndOffset();

				// Mouse drag selection without an explicit mode defaults to stream block.
				if (selectionStartNow != selectionEndNow &&
				    (selectionStartNow != selectionStartBefore ||
				     selectionEndNow != selectionEndBefore)) {
					mBlockMode = bmStream;
					mBlockMarkingOn = false;
					mBlockAnchor = static_cast<uint>(selectionStartNow);
					mBlockEnd = static_cast<uint>(selectionEndNow);
					syncBlockVisual();
					return;
				}
			}
			if (originalEvent == evKeyDown && mBlockMode != bmNone && !mBlockMarkingOn &&
			    bufferLengthBefore != editor->bufferLength()) {
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
					if (shifted < 0)
						shifted = 0;
					if (shifted > static_cast<long>(currentLength))
						shifted = static_cast<long>(currentLength);
					ptr = static_cast<uint>(shifted);
				};

				if (selectionStartBefore == selectionEndBefore) {
					changePos = normalizedCursorBefore;
					if (keyCodeBefore == kbBack && normalizedCursorBefore > 0)
						changePos = normalizedCursorBefore - 1;
				}

				startPtr = std::min(mBlockAnchor, mBlockEnd);
				endPtr = std::max(mBlockAnchor, mBlockEnd);
				normalizedStart = startPtr;
				normalizedEnd = endPtr;

				if (changePos <= normalizedStart) {
					shiftPtr(mBlockAnchor);
					shiftPtr(mBlockEnd);
				} else if (changePos < normalizedEnd && mBlockMode == bmStream) {
					if (mBlockAnchor <= mBlockEnd)
						shiftPtr(mBlockEnd);
					else
						shiftPtr(mBlockAnchor);
				}
			}
			// Drag-release with the mouse finalizes marking in the active block mode.
			if (markingBefore && originalEvent == evMouseDown && mBlockMarkingOn) {
				endBlock();
				return;
			}
			if (mBlockMode != bmNone)
				syncBlockVisual();
		}

	void updateTaskMarkers() {
		std::size_t taskCount = mTrackedCoprocessorTasks.size();
		if (editor != nullptr) {
			if (editor->pendingLineIndexWarmupTaskId() != 0)
				++taskCount;
			if (editor->pendingSyntaxWarmupTaskId() != 0)
				++taskCount;
			if (editor->pendingMiniMapWarmupTaskId() != 0)
				++taskCount;
			if (editor->pendingSaveNormalizationWarmupTaskId() != 0)
				++taskCount;
		}
		if (indicator != nullptr)
			indicator->setTaskCount(taskCount);
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
		if (editor == nullptr)
			return;
		mBlockMode = mode;
		mBlockMarkingOn = true;
		mBlockAnchor = static_cast<uint>(editor->cursorOffset());
		mBlockEnd = static_cast<uint>(editor->cursorOffset());
		syncBlockVisual();
	}

	uint effectiveBlockEnd() const {
		if (editor != nullptr && mBlockMarkingOn)
			return static_cast<uint>(editor->cursorOffset());
		return mBlockEnd;
	}

	void blockPtrRange(uint &a, uint &b) const {
		a = mBlockAnchor;
		b = effectiveBlockEnd();
		if (a > b)
			std::swap(a, b);
	}

	int lineNumberForPtr(uint ptr) const {
		uint pos = 0;
		int line = 1;
		if (editor == nullptr)
			return 0;
		if (ptr > editor->bufferLength())
			ptr = static_cast<uint>(editor->bufferLength());
		while (pos < ptr && pos < editor->bufferLength()) {
			uint next = static_cast<uint>(editor->nextLineOffset(pos));
			if (next <= pos || next > ptr)
				break;
			pos = next;
			++line;
		}
		return line;
	}

	int columnForPtr(uint ptr) const {
		uint start;
		if (editor == nullptr)
			return 0;
		if (ptr > editor->bufferLength())
			ptr = static_cast<uint>(editor->bufferLength());
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
		if (dot == std::string::npos || dot == 0)
			return name;
		return name.substr(0, dot);
	}

	static std::string trimTaskLabel(const std::string &label) {
		std::size_t start = label.find_first_not_of(' ');
		std::size_t end = label.find_last_not_of(' ');
		if (start == std::string::npos)
			return std::string();
		return label.substr(start, end - start + 1);
	}

	static std::string compactTaskLabel(const TrackedTask &task) {
		std::string label = trimTaskLabel(task.label);

		if (label.empty())
			return label;
		if (task.kind == mr::coprocessor::TaskKind::MacroJob)
			return stripExtension(baseNameOf(label));
		if (task.kind == mr::coprocessor::TaskKind::ExternalIo) {
			std::size_t split = label.find_first_of(" \t");
			std::string head = split == std::string::npos ? label : label.substr(0, split);
			return baseNameOf(head);
		}
		return baseNameOf(label);
	}

	static std::string formatTaskElapsed(const TrackedTask &task) {
		using namespace std::chrono;
		char buffer[32];
		double elapsedMs =
		    duration_cast<milliseconds>(steady_clock::now() - task.startedAt).count();

		if (elapsedMs < 1000.0) {
			std::snprintf(buffer, sizeof(buffer), "%.0f ms", elapsedMs);
			return buffer;
		}
		std::snprintf(buffer, sizeof(buffer), "%.2f s", elapsedMs / 1000.0);
		return buffer;
	}

	static std::string taskActivityBullet() {
		using namespace std::chrono;
		static const std::array<const char *, 12> kClockFrames = {
		    "🕛", "🕐", "🕑", "🕒", "🕓", "🕔", "🕕", "🕖", "🕗", "🕘", "🕙", "🕚"};
		static const std::array<const char *, 4> kAsciiFrames = {"|", "/", "-", "\\"};
		const long long elapsedMs =
		    duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
		const std::size_t frame = static_cast<std::size_t>((elapsedMs / 200) % kClockFrames.size());

		if (strwidth(kClockFrames[0]) >= 1)
			return kClockFrames[frame];
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
		if (mBlockMode == bmNone)
			return 0;
		blockPtrRange(a, b);
		return lineNumberForPtr(a);
	}

	int normalizedBlockLine2() const {
		uint a, b;
		if (mBlockMode == bmNone)
			return 0;
		blockPtrRange(a, b);
		return lineNumberForPtr(b);
	}

	int normalizedBlockCol1() const {
		int aCol;
		int bCol;
		if (mBlockMode == bmNone)
			return 0;
		if (mBlockMode == bmLine)
			return 1;
		aCol = columnForPtr(mBlockAnchor);
		bCol = columnForPtr(effectiveBlockEnd());
		return std::min(aCol, bCol);
	}

	int normalizedBlockCol2() const {
		int aCol;
		int bCol;
		if (mBlockMode == bmNone)
			return 0;
		if (mBlockMode == bmLine)
			return 1000;
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
		if (editor == nullptr)
			return;
		if (mBlockMode == bmStream) {
			blockPtrRange(a, b);
			editor->setBlockOverlayState(static_cast<int>(mBlockMode), a, b, true, mBlockMarkingOn);
			editor->setSelectionOffsets(a, b, False);
		} else if (mBlockMode == bmLine) {
			blockPtrRange(a, b);
			editor->setBlockOverlayState(static_cast<int>(mBlockMode), a, b, true, mBlockMarkingOn);
			a = static_cast<uint>(editor->lineStartOffset(a));
			b = static_cast<uint>(editor->nextLineOffset(b));
			if (b > editor->bufferLength())
				b = static_cast<uint>(editor->bufferLength());
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
	char displayTitle[MAXPATH];
};

#endif
