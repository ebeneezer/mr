#define Uses_TButton
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TListViewer
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TView
#include <tvision/tv.h>

#include "MRAcquireDialog.hpp"

#include "../app/MRCommands.hpp"
#include "../app/MRHelpTopics.generated.hpp"
#include "../app/commands/MRExternalCommand.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/widgets/MRDropList.hpp"
#include "../ui/widgets/MRNumericSlider.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "setup/MRSetupCommon.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

TFrame *initMrDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

enum : ushort {
	cmMrAcquireChooseHistory = 3880,
	cmMrAcquireAcceptHistory,
	cmMrAcquireExec,
	cmMrAcquireLoadAll,
	cmMrAcquireLoad
};

constexpr int kDialogWidth = 78;
constexpr int kMinDialogHeight = 15;
constexpr int kMaxDialogHeight = 21;
constexpr int kExecButtonLeft = 2;
constexpr int kExecButtonRight = 11;
constexpr int kCommandInputLeft = 13;
constexpr int kListTop = 5;

class TAcquireCommandEnterInterceptor final : public TView {
  public:
	explicit TAcquireCommandEnterInterceptor(TInputLine *commandField) noexcept : TView(TRect(0, 0, 0, 0)), commandField(commandField) {
		options |= ofPreProcess;
		eventMask |= evKeyDown;
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown && event.keyDown.keyCode == kbEnter && commandField != nullptr && (commandField->state & sfFocused) != 0) {
			TEvent commandEvent{};
			commandEvent.what = evCommand;
			commandEvent.message.command = cmMrAcquireExec;
			putEvent(commandEvent);
			clearEvent(event);
			return;
		}
		TView::handleEvent(event);
	}

  private:
	TInputLine *commandField = nullptr;
};

class AcquireListView final : public TListViewer {
  public:
	AcquireListView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> &items) noexcept : TListViewer(bounds, 1, nullptr, scrollBar), items(items) {
		setRange(static_cast<short>(items.size()));
	}

	void updateItems() {
		setRange(static_cast<short>(items.size()));
		if (!items.empty() && focused >= range) focusItemNum(static_cast<short>(range - 1));
		drawView();
	}

	[[nodiscard]] std::string selectedValue() const {
		if (focused < 0 || focused >= range) return std::string();
		const std::size_t index = static_cast<std::size_t>(focused);
		return index < items.size() ? items[index] : std::string();
	}

	void getText(char *dest, short item, short maxLen) override {
		if (dest == nullptr || maxLen <= 0) return;
		dest[0] = '\0';
		if (item < 0 || static_cast<std::size_t>(item) >= items.size()) return;
		const std::string ordinal = std::to_string(item + 1);
		const std::size_t ordinalWidth = std::to_string(items.size()).size();
		const std::string text = std::string(ordinalWidth - ordinal.size(), ' ') + ordinal + "  " + items[static_cast<std::size_t>(item)];
		strnzcpy(dest, text.c_str(), static_cast<std::size_t>(maxLen) + 1);
	}

	void handleEvent(TEvent &event) override {
		const bool isDoubleClickActivation = event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0 && (event.mouse.eventFlags & meDoubleClick) != 0;
		TView *target = owner != nullptr && owner->owner != nullptr ? owner->owner : owner;

		TListViewer::handleEvent(event);
		if (isDoubleClickActivation && focused >= 0 && focused < range && target != nullptr) {
			message(target, evCommand, cmMrAcquireLoad, nullptr);
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter && focused >= 0 && focused < range && target != nullptr) {
			message(target, evCommand, cmMrAcquireLoad, nullptr);
			clearEvent(event);
		}
	}

  private:
	std::vector<std::string> &items;
};

class AcquireCountView final : public TView {
  public:
	explicit AcquireCountView(const TRect &bounds) noexcept : TView(bounds) {
	}

	void setCount(std::size_t value) {
		const std::string next = std::to_string(value);

		if (next == text) return;
		text = next;
		drawView();
	}

	void draw() override {
		TDrawBuffer buffer;
		const TColorAttr color = getColor(1);
		const int start = std::max(0, size.x - static_cast<int>(text.size()));

		buffer.moveChar(0, ' ', color, size.x);
		if (!text.empty()) buffer.moveStr(static_cast<ushort>(start), text.c_str(), color, size.x - start);
		writeLine(0, 0, size.x, 1, buffer);
	}

  private:
	std::string text;
};

[[nodiscard]] int computeDialogHeight() {
	if (TProgram::deskTop == nullptr) return kMaxDialogHeight;
	return std::max(kMinDialogHeight, std::min(kMaxDialogHeight, static_cast<int>(TProgram::deskTop->size.y / 2) + 1));
}

[[nodiscard]] MREditWindow *chooseAcquireBackgroundRestoreWindow() {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();

	for (MREditWindow *window : windows)
		if (window != nullptr && (window->state & sfVisible) != 0) return window;
	return currentEditWindow();
}

[[nodiscard]] MREditWindow *chooseAcquireTopWindowAfterDismiss() {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();

	for (MREditWindow *window : windows)
		if (window != nullptr && (window->state & sfVisible) != 0) return window;
	return nullptr;
}

[[nodiscard]] bool canRestoreAcquireBackgroundWindow(MREditWindow *window) {
	if (window == nullptr || (window->state & sfVisible) == 0) return false;
	const std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	return std::find(windows.begin(), windows.end(), window) != windows.end();
}

[[nodiscard]] std::string decodeAcquirePathCandidate(std::string_view value) {
	std::string trimmed = trimAscii(value);

	if (trimmed.size() >= 2 && ((trimmed.front() == '"' && trimmed.back() == '"') || (trimmed.front() == '\'' && trimmed.back() == '\''))) trimmed = trimmed.substr(1, trimmed.size() - 2);
	if (trimmed.size() >= 16 && trimmed.compare(0, 16, "file://localhost") == 0) return trimmed.substr(16);
	if (trimmed.size() >= 7 && trimmed.compare(0, 7, "file://") == 0) return trimmed.substr(7);
	return trimmed;
}

class TAcquireDialog final : public MRDialogFoundation {
 public:
	explicit TAcquireDialog(MRAcquireMode mode, MREditWindow *backgroundRestoreWindow)
	    : TWindowInit(initMrDialogFrame),
	      MRDialogFoundation(mr::dialogs::centeredDialogRect(kDialogWidth, computeDialogHeight()), "ACQUIRE", kDialogWidth, computeDialogHeight(), initMrDialogFrame),
	      mode(mode),
	      backgroundRestoreWindow(backgroundRestoreWindow) {
		static constexpr std::array buttons{
		    mr::dialogs::DialogButtonSpec{"Load ~a~ll", cmMrAcquireLoadAll, bfNormal},
		    mr::dialogs::DialogButtonSpec{"~L~oad", cmMrAcquireLoad, bfDefault},
		    mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal},
		};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 1);
		const int dialogHeight = size.y;
		const int commandButtonRight = kDialogWidth - 2;
		const int historyButtonLeft = commandButtonRight - 3;
		const int buttonTop = dialogHeight - 3;
		const int listBottom = buttonTop - 1;
		const int listRight = kDialogWidth - 3;
		const int listWidth = listRight - 2;
		const int scrollBarLeft = listRight;
		const int buttonLeft = (kDialogWidth - metrics.rowWidth) / 2;
		const MRAcquireSettings settings = configuredAcquireSettings();

		helpCtx = hcDialogAcquire;
		eventMask |= evBroadcast;
		mCommandField = new TInputLine(TRect(kCommandInputLeft, 2, historyButtonLeft, 3), 255);
		insert(mCommandField);
		insert(new TAcquireCommandEnterInterceptor(mCommandField));
		mExecButton = new TButton(TRect(kExecButtonLeft, 2, kExecButtonRight, 4), "~E~xec:", cmMrAcquireExec, bfNormal);
		insert(mExecButton);
		commandHistoryAnchor = mCommandField->getBounds();
		commandHistoryAnchor.move(1, 1);
		commandHistoryDropList.createButton(*this, TRect(historyButtonLeft, 2, commandButtonRight, 3), mCommandField, this, cmMrAcquireChooseHistory, false);

		mStatusView = new MRProgressSlider(TRect(2, 4, listRight, 5));
		insert(mStatusView);
		mScrollBar = new TScrollBar(TRect(scrollBarLeft, kListTop, scrollBarLeft + 1, listBottom));
		insert(mScrollBar);
		mListView = new AcquireListView(TRect(2, kListTop, 2 + listWidth, listBottom), mScrollBar, resolvedPaths);
		insert(mListView);
		mCountView = new AcquireCountView(TRect(2, listBottom, listRight, listBottom + 1));
		insert(mCountView);

		mr::dialogs::insertUniformButtonRow(*this, buttonLeft, buttonTop, 1, buttons, 0, &buttonsRow);
		finalizeLayout();

		commandHistory = settings.commandHistory;
		mr::dialogs::writeRecordField(commandBuffer, sizeof(commandBuffer), settings.commandLine);
		mCommandField->setData(commandBuffer);
		updateStatus();
		updateButtons();
		mCommandField->select();
	}

	Boolean valid(ushort command) override {
		if (command == cmCancel || command == cmClose) {
			persistSettings();
			if (loadAllPending) {
				cancelPendingLoad();
				return False;
			}
			cancelPendingLoad();
			if (taskId != 0) {
				closePending = true;
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(taskId));
				if (mStatusView != nullptr) mStatusView->setText("Closing: stopping subshell...");
				updateButtons();
				return False;
			}
		}
		return TDialog::valid(command);
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evBroadcast && event.message.command == cmMrCoprocessorDialogResult && event.message.infoPtr != nullptr) {
			const mr::coprocessor::Result &result = *static_cast<const mr::coprocessor::Result *>(event.message.infoPtr);
			if (result.task.id == taskId && taskId != 0) {
				handleAcquireResult(result);
				clearEvent(event);
				return;
			}
		}
		if (commandHistoryDropList.handleLinkedInputEvent(event, *this, commandHistoryAnchor, commandHistory, mCommandField, this, cmMrAcquireAcceptHistory, commandHistoryVisibleRows())) return;
		if (event.what == evNothing && loadPending) {
			performPendingLoad();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrAcquireChooseHistory) {
			toggleCommandHistoryList();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrAcquireAcceptHistory) {
			acceptCommandHistorySelection();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrAcquireExec) {
			executeAcquireCommand();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrAcquireLoad) {
			loadSelectedPaths(false);
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrAcquireLoadAll) {
			loadSelectedPaths(true);
			clearEvent(event);
			return;
		}
		MRDialogFoundation::handleEvent(event);
	}

 private:
	void updateButtons() {
		const bool hasSelection = mListView != nullptr && !mListView->selectedValue().empty();
		const bool hasEntries = !resolvedPaths.empty();
		const bool enabled = !loadPending && taskId == 0 && !closePending;

		if (buttonsRow.size() >= 2) {
			buttonsRow[0]->setState(sfDisabled, !enabled || !hasEntries);
			buttonsRow[1]->setState(sfDisabled, !enabled || !hasSelection);
		}
		if (buttonsRow.size() >= 3) buttonsRow[2]->setState(sfDisabled, !enabled);
		if (mExecButton != nullptr) mExecButton->setState(sfDisabled, !enabled);
		if (mCommandField != nullptr) mCommandField->setState(sfDisabled, !enabled);
	}

	void persistSettings() {
		MRAcquireSettings settings = configuredAcquireSettings();
		char buffer[sizeof(commandBuffer)] = {0};
		std::string errorText;

		if (mCommandField != nullptr) mCommandField->getData(buffer);
		settings.commandLine = trimAscii(buffer);
		settings.commandHistory = commandHistory;
		static_cast<void>(setConfiguredAcquireSettings(settings, &errorText));
	}

	void rememberCommand(std::string commandLine) {
		commandLine = trimAscii(commandLine);
		if (commandLine.empty()) return;
		commandHistory.erase(std::remove(commandHistory.begin(), commandHistory.end(), commandLine), commandHistory.end());
		commandHistory.insert(commandHistory.begin(), commandLine);
		if (commandHistory.size() > 15) commandHistory.resize(15);
	}

	void addResolvedPath(std::string_view line) {
		const MRDialogHistoryScope scope = mode == MRAcquireMode::LoadFile ? MRDialogHistoryScope::LoadFile : MRDialogHistoryScope::OpenFile;
		const std::string candidate = decodeAcquirePathCandidate(line);
		std::string resolvedPath;

		if (!candidate.empty() && resolveReadableExistingPath(scope, candidate.c_str(), resolvedPath, false) && seenPaths.insert(resolvedPath).second) resolvedPaths.push_back(std::move(resolvedPath));
	}

	void acceptAcquireOutput(std::string_view output, bool flush) {
		stdoutTail.append(output.data(), output.size());
		std::size_t consumed = 0;
		for (;;) {
			const std::size_t end = stdoutTail.find('\n', consumed);
			if (end == std::string::npos) break;
			addResolvedPath(std::string_view(stdoutTail).substr(consumed, end - consumed));
			consumed = end + 1;
		}
		if (flush && consumed < stdoutTail.size()) addResolvedPath(std::string_view(stdoutTail).substr(consumed));
		if (consumed != 0) stdoutTail.erase(0, consumed);
		if (flush) stdoutTail.clear();
		if (mListView != nullptr) mListView->updateItems();
		updateStatus();
		updateButtons();
	}

	void executeAcquireCommand() {
		char buffer[sizeof(commandBuffer)] = {0};
		std::string commandLine;

		if (taskId != 0 || loadPending || closePending) return;
		if (mCommandField != nullptr) mCommandField->getData(buffer);
		commandLine = trimAscii(buffer);
		rememberCommand(commandLine);
		persistSettings();
		resolvedPaths.clear();
		seenPaths.clear();
		stdoutTail.clear();
		if (mListView != nullptr) mListView->updateItems();
		if (commandLine.empty()) {
			updateStatus();
			updateButtons();
			return;
		}
		const std::size_t ownerId = reinterpret_cast<std::size_t>(this);
		taskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Io, mr::coprocessor::TaskKind::ExternalIo, 0, 0, mr::coprocessor::ExecutionOwnerKind::Dialog, ownerId, "acquire", [commandLine, ownerId](const mr::coprocessor::TaskInfo &info) { return runExternalCommandTask(info, ownerId, commandLine, MRBuildHookContext(), std::string(), std::string(), true, false); });
		if (taskId == 0) {
			if (mStatusView != nullptr) mStatusView->setText("Unable to start subshell.");
		} else if (mStatusView != nullptr) {
			mStatusView->setText("Running");
		}
		updateButtons();
	}

	void handleAcquireResult(const mr::coprocessor::Result &result) {
		const mr::coprocessor::ExternalIoChunkPayload *chunk = dynamic_cast<const mr::coprocessor::ExternalIoChunkPayload *>(result.payload.get());
		if (chunk != nullptr) {
			if (!closePending) acceptAcquireOutput(chunk->text, false);
			return;
		}

		const mr::coprocessor::ExternalIoFinishedPayload *finished = dynamic_cast<const mr::coprocessor::ExternalIoFinishedPayload *>(result.payload.get());
		const bool success = result.completed() && finished != nullptr && finished->exitCode == 0;
		if (!closePending) acceptAcquireOutput(std::string_view(), true);
		taskId = 0;
		if (closePending) {
			closePending = false;
			endModal(cmCancel);
			return;
		}
		if (success) updateStatus();
		else if (result.cancelled())
			mStatusView->setText("Cancelled");
		else
			mStatusView->setText("Command failed");
		updateButtons();
	}

	void loadSelectedPaths(bool loadAll) {
		std::vector<std::string> paths;

		if (loadAll) {
			if (resolvedPaths.empty()) return;
			persistSettings();
			pendingPaths = resolvedPaths;
			pendingPathIndex = 0;
			pendingLoadedCount = 0;
			pendingLoadStartedAt = std::chrono::steady_clock::now();
			loadPending = true;
			loadAllPending = true;
			loadBatch.beginInteractive();
			mr::messageline::setStaticMode(true);
			mr::messageline::setStaticProgress(0, pendingPaths.size());
			updateStatus();
			updateButtons();
			return;
		}
		if (mListView != nullptr) {
			const std::string selected = mListView->selectedValue();
			if (!selected.empty()) paths.push_back(selected);
		}
		if (paths.empty()) return;
		persistSettings();
		pendingPaths = std::move(paths);
		pendingPathIndex = 0;
		pendingLoadedCount = 0;
		loadPending = true;
		loadAllPending = false;
		updateStatus();
		updateButtons();
	}

	void performPendingLoad() {
		if (!loadPending || pendingPathIndex >= pendingPaths.size()) {
			finishPendingLoad(false);
			return;
		}
		const std::vector<std::string> path{pendingPaths[pendingPathIndex]};
		bool loaded = false;

		if (loadAllPending) {
			loaded = mode == MRAcquireMode::LoadFile ? loadResolvedFilesIntoWindows(path, MRLoadedWindowActivation::KeepBackground, backgroundRestoreWindow, MRFileLoadMessages::Suppressed, loadBatch) : openResolvedFilesIntoWindows(path, MRLoadedWindowActivation::KeepBackground, backgroundRestoreWindow, MRFileLoadMessages::Suppressed, loadBatch);
		} else if (mode == MRAcquireMode::LoadFile) {
			loaded = loadResolvedFilesIntoWindows(path, MRLoadedWindowActivation::KeepBackground, backgroundRestoreWindow);
		} else {
			loaded = openResolvedFilesIntoWindows(path, MRLoadedWindowActivation::KeepBackground, backgroundRestoreWindow);
		}
		if (loaded) ++pendingLoadedCount;
		++pendingPathIndex;
		if (loadAllPending) mr::messageline::setStaticProgress(pendingPathIndex, pendingPaths.size());
		updateStatus();
		if (pendingPathIndex >= pendingPaths.size()) {
			finishPendingLoad(false);
			return;
		}
		reactivateAfterBackgroundLoad();
	}

	void cancelPendingLoad() {
		if (loadAllPending) {
			finishPendingLoad(true);
			return;
		}
		loadPending = false;
		pendingPaths.clear();
		pendingPathIndex = 0;
		pendingLoadedCount = 0;
		updateStatus();
		updateButtons();
	}

	void finishPendingLoad(bool cancelled) {
		const bool reportBatch = loadAllPending;
		const std::size_t total = pendingPaths.size();
		const std::size_t completed = pendingPathIndex;
		const std::size_t loaded = pendingLoadedCount;
		const long long elapsedMs = reportBatch ? std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - pendingLoadStartedAt).count() : 0;

		if (loadBatch.active()) loadBatch.finish(true, true);
		loadPending = false;
		loadAllPending = false;
		pendingPaths.clear();
		pendingPathIndex = 0;
		pendingLoadedCount = 0;
		reactivateAfterBackgroundLoad();
		updateStatus();
		updateButtons();
		if (!reportBatch) return;

		mr::messageline::setStaticMode(false);
		if (cancelled) {
			const std::string message = "Load all cancelled: " + std::to_string(loaded) + " files loaded, " + std::to_string(completed) + "/" + std::to_string(total) + " processed in " + std::to_string(elapsedMs) + " ms.";

			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, message, mr::messageline::Kind::Warning, mr::messageline::kPriorityHigh);
		} else {
			const std::string message = std::to_string(loaded) + " files loaded in " + std::to_string(elapsedMs) + " ms";

			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, message, mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
		}
	}

	void updateStatus() {
		if (mCountView != nullptr) mCountView->setCount(resolvedPaths.size());
		if (mStatusView == nullptr) return;
		if (loadPending && !loadAllPending) {
			mStatusView->setText("Loading " + std::to_string(std::min(pendingPathIndex + 1, pendingPaths.size())) + "/" + std::to_string(pendingPaths.size()));
			return;
		}
		if (taskId != 0) {
			mStatusView->setText("Running");
			return;
		}
		mStatusView->setText(std::string());
	}

	void reactivateAfterBackgroundLoad() {
		if (owner != nullptr) makeFirst();
		if (TProgram::deskTop != nullptr) TProgram::deskTop->setCurrent(this, TView::normalSelect);
		else
			select();
		if (mListView != nullptr) mListView->select();
	}

	void toggleCommandHistoryList() {
		const std::string currentValue = mCommandField != nullptr ? std::string(mCommandField->data) : std::string();

		if (commandHistory.empty()) return;
		commandHistoryDropList.toggle(*this, commandHistoryAnchor, commandHistory, currentValue, this, cmMrAcquireAcceptHistory, commandHistoryVisibleRows());
	}

	short commandHistoryVisibleRows() const {
		short visibleRows = 7;

		if (visibleRows > size.y - commandHistoryAnchor.a.y - 1) visibleRows = static_cast<short>(size.y - commandHistoryAnchor.a.y - 1);
		if (visibleRows < 1) visibleRows = 1;
		return visibleRows;
	}

	void acceptCommandHistorySelection() {
		std::string value;

		if (!commandHistoryDropList.acceptSelection(value) || mCommandField == nullptr) return;
		strnzcpy(mCommandField->data, value.c_str(), mCommandField->maxLen + 1);
		mCommandField->selectAll(True);
		mCommandField->drawView();
	}

	MRAcquireMode mode;
	char commandBuffer[256]{};
	TInputLine *mCommandField = nullptr;
	TButton *mExecButton = nullptr;
	MRProgressSlider *mStatusView = nullptr;
	TScrollBar *mScrollBar = nullptr;
	AcquireListView *mListView = nullptr;
	AcquireCountView *mCountView = nullptr;
	std::vector<TButton *> buttonsRow;
	std::vector<std::string> commandHistory;
	std::vector<std::string> resolvedPaths;
	std::set<std::string> seenPaths;
	std::string stdoutTail;
	std::vector<std::string> pendingPaths;
	std::size_t pendingPathIndex = 0;
	std::size_t pendingLoadedCount = 0;
	std::chrono::steady_clock::time_point pendingLoadStartedAt = std::chrono::steady_clock::time_point::min();
	MRWindowOpenBatch loadBatch;
	TRect commandHistoryAnchor;
	MRDropList commandHistoryDropList;
	MREditWindow *backgroundRestoreWindow = nullptr;
	std::uint64_t taskId = 0;
	bool loadPending = false;
	bool loadAllPending = false;
	bool closePending = false;
};

} // namespace

ushort runAcquireDialog(MRAcquireMode mode) {
	TAcquireDialog *dialog = nullptr;
	MREditWindow *backgroundRestoreWindow = chooseAcquireBackgroundRestoreWindow();
	MREditWindow *topWindowAfterDismiss = nullptr;
	ushort result = cmCancel;

	if (TProgram::deskTop == nullptr) return cmCancel;
	dialog = new TAcquireDialog(mode, backgroundRestoreWindow);
	if (dialog == nullptr) return cmCancel;
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);

	topWindowAfterDismiss = chooseAcquireTopWindowAfterDismiss();
	if (canRestoreAcquireBackgroundWindow(topWindowAfterDismiss)) mrScheduleWindowActivation(topWindowAfterDismiss);
	else if (canRestoreAcquireBackgroundWindow(backgroundRestoreWindow))
		mrScheduleWindowActivation(backgroundRestoreWindow);
	else if (TProgram::application != nullptr)
		message(TProgram::application, evCommand, cmMrEnsureUsableWorkWindow, nullptr);
	else
		static_cast<void>(mrEnsureUsableWorkWindow(false));
	return result;
}
