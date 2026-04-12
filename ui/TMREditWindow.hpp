#ifndef TMREDITWINDOW_HPP
#define TMREDITWINDOW_HPP

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
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <unistd.h>
#include <vector>

#include "TMRFrame.hpp"
#include "TMRIndicator.hpp"
#include "TMRTextBuffer.hpp"
#include "MRWindowSupport.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../mrmac/mrvm.hpp"

void mrTraceCoprocessorTaskCancel(int bufferId, std::uint64_t taskId);

class TMREditWindow : public TWindow {
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

	TMREditWindow(const TRect &bounds, const char *title, int aNumber)
	    : TWindowInit(&TMREditWindow::initFrame), TWindow(bounds, 0, aNumber), vScrollBar(nullptr),
	      hScrollBar(nullptr), indicator(nullptr), editor(nullptr), bufferId_(allocateBufferId()),
	      firstSaveDone_(false), temporaryFileUsed_(false), temporaryFileName_(), indentLevel_(1),
	      blockMode_(bmNone), blockMarkingOn_(false), blockAnchor_(0), blockEnd_(0),
	      trackedCoprocessorTasks_(), windowRole_(wrText), windowRoleDetail_(), macroQueuedCount_(0),
	      macroCompletedCount_(0), macroConflictCount_(0), macroCancelledCount_(0), macroFailedCount_(0),
	      lastMacroSummaryText_(), windowPaletteData_(defaultWindowPaletteData()),
	      windowPalette_(windowPaletteData_.data(), static_cast<ushort>(windowPaletteData_.size())),
	      customEofMarkerColorValid_(false), customEofMarkerColor_(0) {
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

		indicator = new TMRIndicator(TRect(2, size.y - 1, 38, size.y));
		indicator->hide();
		insert(indicator);
		if (frame != nullptr) {
			TMRFrame *mrFrame = static_cast<TMRFrame *>(frame);
			mrFrame->setMarkerStateProvider([this]() {
				const bool hasTaskSlot = indicator != nullptr && indicator->hasTaskMarkerSlot();
				const bool showTaskIcon = indicator != nullptr && indicator->shouldDrawTaskMarker();
				const bool hasReadOnlySlot = indicator != nullptr && indicator->hasReadOnlyMarkerSlot();
				const bool showReadOnlyIcon = indicator != nullptr && indicator->shouldDrawReadOnlyMarker();
				const bool isActiveWindow = (this->state & sfActive) != 0;
				const bool showRecordingSlot = isActiveWindow && mrIsKeystrokeRecordingActive();
				const bool showRecordingIcon = showRecordingSlot && mrIsKeystrokeRecordingMarkerVisible();
				return TMRFrame::MarkerState(isFileChanged(), insertModeEnabled(), hasTaskSlot, showTaskIcon,
				                             hasReadOnlySlot, showReadOnlyIcon, showRecordingSlot,
				                             showRecordingIcon);
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

	virtual ~TMREditWindow() override {
		cancelTrackedCoprocessorTasks();
	}

	virtual TPalette &getPalette() const override {
		return windowPalette_;
	}

	virtual TColorAttr mapColor(uchar index) override {
		if (index >= 1 && index <= windowPaletteData_.size())
			return windowPaletteData_[index - 1];
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
		if (event.what == evKeyDown) {
			std::string executedMacroName;
			if (mrvmRunAssignedMacroForKey(event.keyDown.keyCode, event.keyDown.controlKeyState,
			                               executedMacroName, nullptr)) {
				clearEvent(event);
				return;
			}
		}
		if (frame != nullptr) {
			TMRFrame *mrFrame = static_cast<TMRFrame *>(frame);
			if ((event.what & (evMouseDown | evMouseMove | evMouseUp)) != 0)
				mrFrame->updateTaskHover(event.mouse.where, false);
			else if ((event.what & (evKeyDown | evCommand)) != 0)
				mrFrame->updateTaskHover(TPoint(), true);
		}
		TWindow::handleEvent(event);
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
		temporaryFileUsed_ = false;
		temporaryFileName_.clear();
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
		temporaryFileUsed_ = false;
		temporaryFileName_.clear();
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
			firstSaveDone_ = true;
			temporaryFileUsed_ = false;
			temporaryFileName_.clear();
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
			firstSaveDone_ = true;
			temporaryFileUsed_ = false;
			temporaryFileName_.clear();
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

	TMRFileEditor *getEditor() const {
		return editor;
	}

	TMRTextBuffer buffer() const {
		return TMRTextBuffer(editor);
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

	TMRFileEditor::LoadTiming lastLoadTiming() const noexcept {
		return editor != nullptr ? editor->lastLoadTiming() : TMRFileEditor::LoadTiming();
	}

	bool hasSelection() const {
		return buffer().hasSelection();
	}

	bool hasUndoHistory() const {
		return buffer().hasUndoHistory();
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

	TMRSyntaxLanguage syntaxLanguage() const {
		return editor != nullptr ? editor->syntaxLanguage() : TMRSyntaxLanguage::PlainText;
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
		return firstSaveDone_;
	}

	bool eofInMemory() const {
		return editor != nullptr;
	}

	int bufferId() const {
		return bufferId_;
	}

	std::size_t documentId() const noexcept {
		return editor != nullptr ? editor->documentId() : 0;
	}

	WindowRole windowRole() const noexcept {
		return windowRole_;
	}

	const char *windowRoleName() const noexcept {
		switch (windowRole_) {
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
		windowRole_ = role;
		windowRoleDetail_ = detail;
	}

	const std::string &windowRoleDetail() const noexcept {
		return windowRoleDetail_;
	}

	bool isCommunicationWindow() const noexcept {
		return windowRole_ == wrCommunicationCommand || windowRole_ == wrCommunicationPipe ||
		       windowRole_ == wrCommunicationDevice;
	}

	bool isTemporaryFile() const {
		return temporaryFileUsed_;
	}

	const char *temporaryFileName() const {
		return temporaryFileName_.c_str();
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
		if ((fileName == nullptr || *fileName == '\0') && windowRole_ == wrFile)
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
		for (std::size_t i = 0; i < trackedCoprocessorTasks_.size(); ++i)
			if (trackedCoprocessorTasks_[i].id == taskId)
				return;
		trackedCoprocessorTasks_.push_back(TrackedTask(taskId, kind, label));
		updateTaskMarkers();
	}

	void noteQueuedBackgroundMacro(const std::string &name, bool staged) {
		++macroQueuedCount_;
		lastMacroSummaryText_ = staged ? "Queued staged macro '" : "Queued background macro '";
		lastMacroSummaryText_ += name;
		lastMacroSummaryText_ += "'.";
	}

	void noteBackgroundMacroCompleted(const std::string &summary) {
		++macroCompletedCount_;
		lastMacroSummaryText_ = summary;
	}

	void noteBackgroundMacroConflict(const std::string &summary) {
		++macroConflictCount_;
		lastMacroSummaryText_ = summary;
	}

	void noteBackgroundMacroCancelled(const std::string &summary) {
		++macroCancelledCount_;
		lastMacroSummaryText_ = summary;
	}

	void noteBackgroundMacroFailed(const std::string &summary) {
		++macroFailedCount_;
		lastMacroSummaryText_ = summary;
	}

	void noteBackgroundMacroCancelRequested(std::size_t count) {
		lastMacroSummaryText_ = "Cancel requested for ";
		lastMacroSummaryText_ += std::to_string(count);
		lastMacroSummaryText_ += " background macro task";
		if (count != 1)
			lastMacroSummaryText_ += "s";
		lastMacroSummaryText_ += ".";
	}

	std::size_t trackedCoprocessorTaskCount() const noexcept {
		return trackedCoprocessorTasks_.size();
	}

	std::uint64_t trackedCoprocessorTaskId(std::size_t index) const noexcept {
		return index < trackedCoprocessorTasks_.size() ? trackedCoprocessorTasks_[index].id : 0;
	}

	std::size_t trackedMacroTaskCount() const noexcept {
		std::size_t count = 0;
		for (std::size_t i = 0; i < trackedCoprocessorTasks_.size(); ++i)
			if (trackedCoprocessorTasks_[i].kind == mr::coprocessor::TaskKind::MacroJob)
				++count;
		return count;
	}

	std::size_t trackedTaskCount(mr::coprocessor::TaskKind kind) const noexcept {
		std::size_t count = 0;
		for (std::size_t i = 0; i < trackedCoprocessorTasks_.size(); ++i)
			if (trackedCoprocessorTasks_[i].kind == kind)
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
		text += std::to_string(macroQueuedCount_);
		text += ", ok ";
		text += std::to_string(macroCompletedCount_);
		text += ", conflict ";
		text += std::to_string(macroConflictCount_);
		text += ", cancel ";
		text += std::to_string(macroCancelledCount_);
		text += ", fail ";
		text += std::to_string(macroFailedCount_);
		return text;
	}

	std::string lastMacroSummary() const {
		return lastMacroSummaryText_.empty() ? std::string("<none>") : lastMacroSummaryText_;
	}

	std::vector<std::string> describeRunningTasks() const {
		std::vector<std::string> lines;
		std::size_t i;

		for (i = 0; i < trackedCoprocessorTasks_.size(); ++i) {
			const TrackedTask &task = trackedCoprocessorTasks_[i];
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
			line += "  ";
			line += formatTaskElapsed(task);
			lines.push_back(line);
		}
		if (editor != nullptr) {
			if (editor->pendingLineIndexWarmupTaskId() != 0)
				lines.push_back("Line indexing  running");
			if (editor->pendingSyntaxWarmupTaskId() != 0)
				lines.push_back("Syntax warmup  running");
			if (editor->pendingMiniMapWarmupTaskId() != 0)
				lines.push_back("rendering mini map");
		}
		return lines;
	}

	bool cancelTrackedMacroTasks() {
		bool cancelledAny = false;
		std::size_t macroCount = trackedMacroTaskCount();

		for (std::size_t i = 0; i < trackedCoprocessorTasks_.size(); ++i) {
			if (trackedCoprocessorTasks_[i].kind != mr::coprocessor::TaskKind::MacroJob)
				continue;
			mrTraceCoprocessorTaskCancel(bufferId_, trackedCoprocessorTasks_[i].id);
			if (mr::coprocessor::globalCoprocessor().cancelTask(trackedCoprocessorTasks_[i].id))
				cancelledAny = true;
		}
		if (cancelledAny)
			noteBackgroundMacroCancelRequested(macroCount);
		return cancelledAny;
	}

	bool cancelTrackedExternalIoTasks() {
		bool cancelledAny = false;

		for (std::size_t i = 0; i < trackedCoprocessorTasks_.size(); ++i) {
			if (trackedCoprocessorTasks_[i].kind != mr::coprocessor::TaskKind::ExternalIo)
				continue;
			mrTraceCoprocessorTaskCancel(bufferId_, trackedCoprocessorTasks_[i].id);
			if (mr::coprocessor::globalCoprocessor().cancelTask(trackedCoprocessorTasks_[i].id))
				cancelledAny = true;
		}
		return cancelledAny;
	}

	std::size_t prepareCoprocessorTasksForShutdown() {
		std::size_t clearedCount = trackedCoprocessorTasks_.size();

		for (std::size_t i = 0; i < trackedCoprocessorTasks_.size(); ++i) {
			mrTraceCoprocessorTaskCancel(bufferId_, trackedCoprocessorTasks_[i].id);
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(trackedCoprocessorTasks_[i].id));
		}
		trackedCoprocessorTasks_.clear();
		if (editor != nullptr) {
			std::uint64_t lineIndexTaskId = editor->pendingLineIndexWarmupTaskId();
			if (lineIndexTaskId != 0) {
				mrTraceCoprocessorTaskCancel(bufferId_, lineIndexTaskId);
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(lineIndexTaskId));
				editor->clearLineIndexWarmupTask(lineIndexTaskId);
				++clearedCount;
			}
			std::uint64_t syntaxTaskId = editor->pendingSyntaxWarmupTaskId();
			if (syntaxTaskId != 0) {
				mrTraceCoprocessorTaskCancel(bufferId_, syntaxTaskId);
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(syntaxTaskId));
				editor->clearSyntaxWarmupTask(syntaxTaskId);
				++clearedCount;
			}
			std::uint64_t miniMapTaskId = editor->pendingMiniMapWarmupTaskId();
			if (miniMapTaskId != 0) {
				mrTraceCoprocessorTaskCancel(bufferId_, miniMapTaskId);
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(miniMapTaskId));
				editor->clearMiniMapWarmupTask(miniMapTaskId);
				++clearedCount;
			}
		}
		updateTaskMarkers();
		return clearedCount;
	}

	void showMacroNotice(const std::string &text, TMRIndicator::NoticeKind kind) {
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

	bool usesApproximateMetrics() const noexcept {
		return editor != nullptr && editor->usesApproximateMetrics();
	}

	void releaseCoprocessorTask(std::uint64_t taskId) {
		for (std::vector<TrackedTask>::iterator it = trackedCoprocessorTasks_.begin();
		     it != trackedCoprocessorTasks_.end(); ++it)
			if (it->id == taskId) {
				trackedCoprocessorTasks_.erase(it);
				updateTaskMarkers();
				return;
			}
	}

	int indentLevel() const {
		return indentLevel_;
	}

	void setIndentLevel(int level) {
		if (level < 1)
			level = 1;
		if (level > 254)
			level = 254;
		indentLevel_ = level;
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
		if (editor == nullptr || blockMode_ == bmNone)
			return;
		blockEnd_ = static_cast<uint>(editor->cursorOffset());
		blockMarkingOn_ = false;
		syncBlockVisual();
	}

	void clearBlock() {
		blockMode_ = bmNone;
		blockMarkingOn_ = false;
		blockAnchor_ = 0;
		blockEnd_ = 0;
		if (editor != nullptr) {
			editor->setSelectionOffsets(editor->cursorOffset(), editor->cursorOffset(), False);
			editor->revealCursor(True);
			editor->update(ufView);
		}
	}

	bool hasBlock() const {
		return blockMode_ != bmNone;
	}

	bool isBlockMarking() const {
		return blockMode_ != bmNone && blockMarkingOn_;
	}

	int blockStatus() const {
		return static_cast<int>(blockMode_);
	}

	uint blockAnchorPtr() const {
		return blockAnchor_;
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
		blockMode_ = static_cast<BlockMode>(mode);
		blockMarkingOn_ = blockMode_ != bmNone && markingOn;
		blockAnchor_ = std::min<uint>(anchor, static_cast<uint>(editor->bufferLength()));
		blockEnd_ = std::min<uint>(end, static_cast<uint>(editor->bufferLength()));
		if (blockMode_ == bmNone) {
			blockMarkingOn_ = false;
			blockAnchor_ = 0;
			blockEnd_ = 0;
			editor->update(ufView);
			return;
		}
		syncBlockVisual();
	}


	void resetWindowColorsToConfiguredDefaults() {
		windowPaletteData_ = defaultWindowPaletteData();
		rebuildWindowPalette();
		customEofMarkerColorValid_ = false;
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

		windowPaletteData_[0] = framePassive;
		windowPaletteData_[1] = frameActive;
		windowPaletteData_[2] = frameActive;
		windowPaletteData_[3] = framePassive;
		windowPaletteData_[4] = frameActive;
		windowPaletteData_[5] = textNormal;
		windowPaletteData_[6] = textSelected;
		windowPaletteData_[7] = textSelected;
		windowPaletteData_[8] = static_cast<TColorAttr>(colors[6]);
		windowPaletteData_[9] = static_cast<TColorAttr>(colors[7]);
		windowPaletteData_[10] = static_cast<TColorAttr>(colors[1]);
		windowPaletteData_[11] = static_cast<TColorAttr>(colors[8]);
		customEofMarkerColorValid_ = true;
		customEofMarkerColor_ = static_cast<TColorAttr>(colors[3]);
		rebuildWindowPalette();
		if (editor != nullptr)
			editor->setWindowEofMarkerColorOverride(true, customEofMarkerColor_);
		refreshWindowPaletteViews();
	}

  private:
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
		windowPalette_ = TPalette(windowPaletteData_.data(), static_cast<ushort>(windowPaletteData_.size()));
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

	TMRFileEditor *createEditor(const TRect &bounds, const char *fileName) {
		return new TMRFileEditor(bounds, hScrollBar, vScrollBar, indicator, fileName != nullptr ? fileName : "");
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
		return new TMRFrame(r);
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
		editor->resetUndoState();
		clearBlock();
	}

	void updateTitleFromEditor() {
		if (editor != nullptr && editor->hasPersistentFileName()) {
			std::strncpy(displayTitle, editor->persistentFileName(), sizeof(displayTitle) - 1);
			displayTitle[sizeof(displayTitle) - 1] = '\0';
		}
		refreshSyntaxContext();
		message(owner, evBroadcast, cmUpdateTitle, 0);
	}

	void refreshSyntaxContext() {
		if (editor != nullptr)
			editor->setSyntaxTitleHint(displayTitle);
	}

	void updateTaskMarkers() {
		std::size_t taskCount = trackedCoprocessorTasks_.size();
		if (editor != nullptr) {
			if (editor->pendingLineIndexWarmupTaskId() != 0)
				++taskCount;
			if (editor->pendingSyntaxWarmupTaskId() != 0)
				++taskCount;
			if (editor->pendingMiniMapWarmupTaskId() != 0)
				++taskCount;
		}
		if (indicator != nullptr)
			indicator->setTaskCount(taskCount);
	}

	void cancelTrackedCoprocessorTasks() {
		for (std::size_t i = 0; i < trackedCoprocessorTasks_.size(); ++i) {
			mrTraceCoprocessorTaskCancel(bufferId_, trackedCoprocessorTasks_[i].id);
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(trackedCoprocessorTasks_[i].id));
		}
		trackedCoprocessorTasks_.clear();
		updateTaskMarkers();
	}

	void beginBlock(BlockMode mode) {
		if (editor == nullptr)
			return;
		blockMode_ = mode;
		blockMarkingOn_ = true;
		blockAnchor_ = static_cast<uint>(editor->cursorOffset());
		blockEnd_ = static_cast<uint>(editor->cursorOffset());
		syncBlockVisual();
	}

	uint effectiveBlockEnd() const {
		if (editor != nullptr && blockMarkingOn_)
			return static_cast<uint>(editor->cursorOffset());
		return blockEnd_;
	}

	void blockPtrRange(uint &a, uint &b) const {
		a = blockAnchor_;
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

	int normalizedBlockLine1() const {
		uint a, b;
		if (blockMode_ == bmNone)
			return 0;
		blockPtrRange(a, b);
		return lineNumberForPtr(a);
	}

	int normalizedBlockLine2() const {
		uint a, b;
		if (blockMode_ == bmNone)
			return 0;
		blockPtrRange(a, b);
		return lineNumberForPtr(b);
	}

	int normalizedBlockCol1() const {
		int aCol;
		int bCol;
		if (blockMode_ == bmNone)
			return 0;
		if (blockMode_ == bmLine)
			return 1;
		aCol = columnForPtr(blockAnchor_);
		bCol = columnForPtr(effectiveBlockEnd());
		return std::min(aCol, bCol);
	}

	int normalizedBlockCol2() const {
		int aCol;
		int bCol;
		if (blockMode_ == bmNone)
			return 0;
		if (blockMode_ != bmColumn)
			return 256;
		aCol = columnForPtr(blockAnchor_);
		bCol = columnForPtr(effectiveBlockEnd());
		return std::max(aCol, bCol);
	}

	void syncBlockVisual() {
		uint a;
		uint b;
		if (editor == nullptr)
			return;
		if (blockMode_ == bmStream) {
			blockPtrRange(a, b);
			editor->setSelectionOffsets(a, b, False);
		} else if (blockMode_ == bmLine) {
			blockPtrRange(a, b);
			a = static_cast<uint>(editor->lineStartOffset(a));
			b = static_cast<uint>(editor->nextLineOffset(b));
			if (b > editor->bufferLength())
				b = static_cast<uint>(editor->bufferLength());
			editor->setSelectionOffsets(a, b, False);
		} else
			editor->setSelectionOffsets(editor->cursorOffset(), editor->cursorOffset(), False);
		editor->revealCursor(True);
		editor->update(ufView);
	}

	TScrollBar *vScrollBar;
	TScrollBar *hScrollBar;
	TMRIndicator *indicator;
	TMRFileEditor *editor;
	int bufferId_;
	bool firstSaveDone_;
	bool temporaryFileUsed_;
	std::string temporaryFileName_;
	int indentLevel_;
	BlockMode blockMode_;
	bool blockMarkingOn_;
	uint blockAnchor_;
	uint blockEnd_;
	std::vector<TrackedTask> trackedCoprocessorTasks_;
	WindowRole windowRole_;
	std::string windowRoleDetail_;
	std::size_t macroQueuedCount_;
	std::size_t macroCompletedCount_;
	std::size_t macroConflictCount_;
	std::size_t macroCancelledCount_;
	std::size_t macroFailedCount_;
	std::string lastMacroSummaryText_;
	mutable std::array<TColorAttr, 12> windowPaletteData_;
	mutable TPalette windowPalette_;
	bool customEofMarkerColorValid_;
	TColorAttr customEofMarkerColor_;
	char displayTitle[MAXPATH];
};

#endif
