#define Uses_TKeys
#define Uses_TApplication
#define Uses_TEvent
#define Uses_TRect
#define Uses_TView
#define Uses_TMenuBar
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_TDeskTop
#include <tvision/tv.h>

#include "MREditorApp.hpp"

#include "../coprocessor/MRCoprocessor.hpp"
#include "../mrmac/MRVM.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "../dialogs/setup/MRSetupCommon.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../ui/MRDeskTop.hpp"
#include "../ui/MRBentoBox/MRBentoBox.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRMenuBar.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRStatusLine.hpp"
#include "../ui/MRPerformancePanel.hpp"
#include "../ui/MRSidekickEditor.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/MRWindowLayout.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "MRCommandRouter.hpp"
#include "MRCommands.hpp"
#include "MRFunctionKeyBindings.hpp"
#include "MRMenuFactory.hpp"
#include "MRMacroDebuggerCommandRoute.hpp"
#include "MRHelpTopics.generated.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
bool shouldInvalidateScreenBaseForEvent(ushort eventWhat) noexcept {
	switch (eventWhat) {
		case evKeyDown:
		case evMouseDown:
		case evMouseWheel:
		case evCommand:
			return true;
		default:
			return false;
	}
}

bool isCalculatorHotkeyEvent(const TEvent &event) noexcept {
	if (event.what != evKeyDown) return false;
	const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);
	return (normalized.mods & kbAltShift) != 0 && normalized.code == 'C';
}

bool isBuildCurrentFileDefaultKey(const TEvent &event) noexcept {
	if (event.what != evKeyDown) return false;
	const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);

	return normalized == TKey(kbF9);
}

bool topNonEditorDesktopViewContains(TPoint where) {
	TView *desktopTop = TProgram::deskTop != nullptr ? TProgram::deskTop->TopView() : nullptr;

	return desktopTop != nullptr && dynamic_cast<MREditWindow *>(desktopTop) == nullptr && (desktopTop->state & sfVisible) != 0 && desktopTop->mouseInView(where);
}

void traceCalculatorHotkeyEvent(const char *stage, const TEvent &event) {
	if (!isCalculatorHotkeyEvent(event)) return;
	const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);
	char line[288];
	std::snprintf(line, sizeof(line), "KEYDBG calc stage=%s rawCode=0x%04X rawMods=0x%04X normCode=0x%04X normMods=0x%04X textLen=%u char=0x%02X", stage, static_cast<unsigned>(event.keyDown.keyCode), static_cast<unsigned>(event.keyDown.controlKeyState), static_cast<unsigned>(normalized.code), static_cast<unsigned>(normalized.mods), static_cast<unsigned>(event.keyDown.textLength), static_cast<unsigned>(static_cast<unsigned char>(event.keyDown.charScan.charCode)));
	mrLogMessage(line);
}

bool keyDebugEnabled() noexcept {
	static int cached = -1;

	if (cached < 0) {
		const char *value = std::getenv("MR_KEY_DEBUG");
		cached = (value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0) ? 1 : 0;
	}
	return cached == 1;
}

void traceKeyDebugEvent(const char *stage, const TEvent &event) {
	if (!keyDebugEnabled() || event.what != evKeyDown) return;
	const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);
	char line[320];

	std::snprintf(line, sizeof(line), "KEYDBG event stage=%s rawCode=0x%04X rawMods=0x%04X normCode=0x%04X normMods=0x%04X textLen=%u char=0x%02X scan=0x%02X", stage, static_cast<unsigned>(event.keyDown.keyCode), static_cast<unsigned>(event.keyDown.controlKeyState), static_cast<unsigned>(normalized.code), static_cast<unsigned>(normalized.mods), static_cast<unsigned>(event.keyDown.textLength), static_cast<unsigned>(static_cast<unsigned char>(event.keyDown.charScan.charCode)), static_cast<unsigned>(static_cast<unsigned char>(event.keyDown.charScan.scanCode)));
	mrLogMessage(line);
}
} // namespace
TMenuBar *MREditorApp::initMRMenuBar(TRect r) {
	return createMRMenuBar(r);
}

TStatusLine *MREditorApp::initMRStatusLine(TRect r) {
	r.a.y = r.b.y - 1;
	return new MRStatusLine(r, *new TStatusDef(0, 0xFFFF) + *new TStatusItem("~F1~ Help", kbF1, cmHelp) + *new TStatusItem("~F10~ Menu", kbF10, cmMenu) + *new TStatusItem("~Alt-F10~ Rec", kbAltF10, cmMrMacroToggleRecording) + *new TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit));
}

TDeskTop *MREditorApp::initMRDeskTop(TRect r) {
	r.a.y++;
	r.b.y--;
	return new MRDeskTop(r);
}
bool MREditorApp::quitPrepared() const noexcept {
	return exitPrepared;
}

void MREditorApp::refreshConfiguredUiSettingsSnapshot() {
	cursorPositionMarkerFormat = configuredCursorPositionMarker();
	persistentBlocksMenuEnabled = configuredPersistentBlocksSetting();
	menulineMessagesEnabled = configuredMenulineMessages();
	virtualDesktopCount = configuredVirtualDesktops();
	cyclicVirtualDesktopsEnabled = configuredCyclicVirtualDesktops();
	mr::messageline::setRuntimeMessageLineEnabled(menulineMessagesEnabled);
	mrRefreshVirtualDesktopSettingsSnapshot(virtualDesktopCount, cyclicVirtualDesktopsEnabled);
	if (auto *mrMenuBar = dynamic_cast<MRMenuBar *>(menuBar)) mrMenuBar->setPersistentBlocksMenuState(persistentBlocksMenuEnabled);
}

void MREditorApp::setSnippetSidekickHintsActive(bool active) {
	if (snippetSidekickHintsActive == active) return;
	snippetSidekickHintsActive = active;
	syncFunctionKeyState();
}

void mrRefreshEditorApplicationUiSettingsSnapshot() {
	if (auto *app = dynamic_cast<MREditorApp *>(TProgram::application)) app->refreshConfiguredUiSettingsSnapshot();
}

void mrSetSnippetSidekickHintsActive(bool active) {
	if (auto *app = dynamic_cast<MREditorApp *>(TProgram::application)) app->setSnippetSidekickHintsActive(active);
}

void MREditorApp::beginInteractiveMouseCapture() noexcept {
	++interactiveMouseCaptureDepth;
}

void MREditorApp::endInteractiveMouseCapture() noexcept {
	if (interactiveMouseCaptureDepth > 0) --interactiveMouseCaptureDepth;
}

MREditorApp::~MREditorApp() {
	const auto prepareStartedAt = std::chrono::steady_clock::now();
	prepareForQuit();
	{
		std::ostringstream line;
		line << "App destructor phase prepare_for_quit took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - prepareStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
	}
	const auto shutdownStartedAt = std::chrono::steady_clock::now();
	mr::coprocessor::globalCoprocessor().shutdown(true);
	{
		std::ostringstream line;
		line << "App destructor phase coprocessor_shutdown took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - shutdownStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
	}
}

void MREditorApp::prepareForQuit() {
	if (exitPrepared) return;

	const auto quitStartedAt = std::chrono::steady_clock::now();
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	std::size_t pendingTaskCount = 0;
	std::string settingsError;
	MRSettingsWriteReport settingsWriteReport;
	{
		std::ostringstream line;
		line << "Quit prepare begin windows=" << windows.size() << ".";
		mrLogMessage(line.str().c_str());
	}
	for (auto *window : windows) {
		if (window == nullptr) continue;
		std::ostringstream line;
		line << "Quit window state #" << window->number << " modified=" << (window->isFileChanged() ? 1 : 0) << " len=" << window->bufferLength() << " add=" << window->addBufferLength()
		     << " pieces=" << window->pieceCount() << " undo=" << window->undoStackDepth() << " redo=" << window->redoStackDepth() << ".";
		mrLogMessage(line.str().c_str());
	}

	const auto snapshotStartedAt = std::chrono::steady_clock::now();
	mrFlushWorkspaceAutosaveNow();
	if (!persistConfiguredSettingsSnapshot(&settingsError, &settingsWriteReport) && !settingsError.empty()) mrLogMessage(("Settings snapshot on exit failed: " + settingsError).c_str());
	else
		mrLogSettingsWriteReport("exit snapshot", settingsWriteReport);
	{
		std::ostringstream line;
		line << "Quit phase settings_snapshot took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - snapshotStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
	}

	exitPrepared = true;
	const auto cancelStartedAt = std::chrono::steady_clock::now();
	for (auto &window : windows)
		if (window != nullptr) pendingTaskCount += window->prepareCoprocessorTasksForShutdown();
	{
		std::ostringstream line;
		line << "Quit phase cancel_tasks took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cancelStartedAt).count() << " pending=" << pendingTaskCount << ".";
		mrLogMessage(line.str().c_str());
	}

	if (pendingTaskCount != 0) {
		std::string line = "Exit requested; cancelling ";
		line += std::to_string(pendingTaskCount);
		line += " running or pending coprocessor task";
		if (pendingTaskCount != 1) line += "s";
		line += ".";
		mrLogMessage(line.c_str());
		const auto pumpStartedAt = std::chrono::steady_clock::now();
		mr::coprocessor::globalCoprocessor().pump(64);
		{
			std::ostringstream pumpLine;
			pumpLine << "Quit phase pump_after_cancel took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - pumpStartedAt).count() << ".";
			mrLogMessage(pumpLine.str().c_str());
		}
	}
	cancelForegroundMacroDelays();
	if (configuredLogHandling() == MRLogHandling::Persist) {
		const std::string logPath = configuredLogFilePath();
		const auto appendStartedAt = std::chrono::steady_clock::now();
		if (!mrAppendLogBufferToFile(logPath, &settingsError)) mrLogMessage(("MR log append on exit failed: " + settingsError).c_str());
		{
			std::ostringstream line;
			line << "Quit phase append_log took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - appendStartedAt).count() << ".";
			mrLogMessage(line.str().c_str());
		}
	}
	{
		std::ostringstream line;
		line << "Quit prepare end total_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - quitStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
	}
}
void MREditorApp::runConfiguredAutoexecMacros() {
	std::vector<std::string> configuredEntries;
	std::vector<std::string> failedEntries;
	std::vector<std::string> survivingEntries;
	std::string directory = defaultMacroDirectoryPath();
	std::size_t executedCount = 0;

	clearConfiguredAutoexecMacroDiagnostics();
	configuredAutoexecMacroEntries(configuredEntries);
	if (configuredEntries.empty()) {
		mrLogMessage("Autoexec bootstrap: no AUTOEXEC_MACRO entries configured.");
		return;
	}

	for (const std::string &fileName : configuredEntries) {
		const std::filesystem::path resolvedPath = std::filesystem::path(directory) / fileName;
		const std::string resolved = resolvedPath.lexically_normal().generic_string();
		std::string errorText;

		if (!std::filesystem::exists(resolvedPath)) {
			errorText = "Autoexec macro file not found.";
			failedEntries.push_back(fileName);
			rememberConfiguredAutoexecMacroDiagnostic(fileName, errorText);
			mrLogMessage(("Autoexec bootstrap missing macro: " + fileName).c_str());
			continue;
		}
		if (runMacroFileByPath(resolved.c_str(), &errorText, true)) {
			++executedCount;
			survivingEntries.push_back(fileName);
			continue;
		}

		failedEntries.push_back(fileName);
		rememberConfiguredAutoexecMacroDiagnostic(fileName, errorText.empty() ? "Autoexec execution failed." : errorText);
		mrLogMessage(("Autoexec bootstrap failed for " + fileName + ": " + (errorText.empty() ? std::string("unknown error") : errorText)).c_str());
	}

	{
		std::string line = "Autoexec bootstrap executed ";
		line += std::to_string(executedCount);
		line += " macro";
		if (executedCount != 1) line += "s";
		line += " from ";
		line += std::to_string(configuredEntries.size());
		line += " configured entry";
		if (configuredEntries.size() != 1) line += "s";
		line += ".";
		mrLogMessage(line.c_str());
	}

	if (!failedEntries.empty()) {
		std::string errorText;

		if (!setConfiguredAutoexecMacroEntries(survivingEntries, &errorText)) {
			mrLogMessage(("Autoexec bootstrap could not remove failed entries: " + errorText).c_str());
			return;
		}
		mrLogMessage(("Autoexec bootstrap removed " + std::to_string(failedEntries.size()) + " failed AUTOEXEC_MACRO entr" + (failedEntries.size() == 1 ? "y; settings persist is pending." : "ies; settings persist is pending.")).c_str());
	}
}

void MREditorApp::getEvent(TEvent &event) {
	TApplication::getEvent(event);
	if (event.what != evCommand) return;

	switch (event.message.command) {
		case cmHelp:
			static_cast<void>(helpSystem.showTopic(getHelpCtx()));
			break;
		case cmMrHelpContents:
			static_cast<void>(helpSystem.showTopic(hcContents));
			break;
		case cmMrHelpKeys:
			static_cast<void>(helpSystem.showTopic(hcKeys));
			break;
		case cmMrHelpDetailedIndex:
			static_cast<void>(helpSystem.showTopic(hcDetailedIndex));
			break;
		case cmMrHelpPreviousTopic:
			static_cast<void>(helpSystem.showPreviousTopic());
			break;
		default:
			return;
	}
	clearEvent(event);
}

bool MREditorApp::showHelpTopic(ushort context) {
	return helpSystem.showTopic(context);
}

bool MREditorApp::showPreviousHelpTopic() {
	return helpSystem.showPreviousTopic();
}

void MREditorApp::handleEvent(TEvent &event) {
		mr::coprocessor::globalCoprocessor().pumpFor(coprocessorPumpBudget);
	const ushort originalWhat = event.what;

	if (event.what == evKeyState) {
		const ushort rawModifiers = event.keyState.controlKeyState;
		ushort modifiers = 0;

		if ((rawModifiers & kbShift) != 0) modifiers |= kbShift;
		if ((rawModifiers & kbCtrlShift) != 0) modifiers |= kbCtrlShift;
		if ((rawModifiers & kbAltShift) != 0) modifiers |= kbAltShift;
		if ((rawModifiers & kbSuperShift) != 0) modifiers |= kbSuperShift;

		if (functionKeyModifiers != modifiers) {
			functionKeyModifiers = modifiers;
			syncFunctionKeyState();
		}
		clearEvent(event);
		return;
	}
	traceKeyDebugEvent("app-pre", event);
	traceCalculatorHotkeyEvent("app-pre", event);
	clearTransientSearchSelectionOnUserInput(event);
	if (event.what == evKeyDown && mr::messageline::staticModeActive()) {
		switch (TKey(event.keyDown).code) {
			case kbF1:
			case kbF2:
			case kbF3:
			case kbF4:
			case kbF5:
			case kbF6:
			case kbF7:
			case kbF8:
			case kbF9:
			case kbF10:
			case kbF11:
			case kbF12:
				clearEvent(event);
				return;
			default:
				break;
		}
	}
	if (event.what == evKeyDown) {
		const TKey pressed(event.keyDown);
		bool functionKey = false;

		switch (pressed.code) {
			case kbF1:
			case kbF2:
			case kbF3:
			case kbF4:
			case kbF5:
			case kbF6:
			case kbF7:
			case kbF8:
			case kbF9:
			case kbF10:
			case kbF11:
			case kbF12:
				functionKey = true;
				break;
			default:
				break;
		}
		if (functionKey) {
			MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(currentEditWindow());
			if (bentoBox != nullptr) bentoBox->dismissPaneMenus();
		}
	}
	if (event.what == evMouseWheel) {
		TView *top = TopView();
		const bool modalViewActive = top != nullptr && top != this && (top->state & sfModal) != 0;
		bool nonEditorDesktopTopUnderMouse = !modalViewActive && topNonEditorDesktopViewContains(event.mouse.where);
		if (!modalViewActive && !nonEditorDesktopTopUnderMouse) {
			MREditWindow *wheelWindow = currentEditWindow();
			if (wheelWindow == nullptr || !wheelWindow->containsMouse(event)) {
				wheelWindow = nullptr;
				for (MREditWindow *candidate : allEditWindowsInZOrder()) {
					if (candidate != nullptr && (candidate->state & sfVisible) != 0 && candidate->containsMouse(event)) {
						wheelWindow = candidate;
						break;
					}
				}
			}
			if (wheelWindow != nullptr) {
				wheelWindow->handleEvent(event);
				if (event.what == evNothing) return;
			}
		}
	}
	if (event.what == evKeyDown && TKey(event.keyDown) == TKey(kbF11)) {
		toggleFullscreenPresentation();
		clearEvent(event);
		return;
	}
	if (fullscreenPresentationActive && event.what == evKeyDown && TKey(event.keyDown) == TKey(kbEsc)) {
		leaveFullscreenPresentation();
		clearEvent(event);
		return;
	}
	if (performancePanelVisible && event.what == evKeyDown && TKey(event.keyDown) == TKey(kbEsc)) {
		togglePerformancePanel();
		clearEvent(event);
		return;
	}
	if (event.what == evCommand && event.message.command == cmMrToggleFullscreen) {
		toggleFullscreenPresentation();
		clearEvent(event);
		return;
	}
	if (isRecorderToggleCommand(event)) {
		if (keystrokeRecording) stopKeystrokeRecording();
		else
			startKeystrokeRecording();
		mrvmUiInvalidateScreenBase();
		clearEvent(event);
		return;
	}
	if (isRecorderToggleKey(event)) {
		if (keystrokeRecording) stopKeystrokeRecording();
		else
			startKeystrokeRecording();
		mrvmUiInvalidateScreenBase();
		clearEvent(event);
		return;
	}
	if (keystrokeRecording && event.what == evKeyDown) appendRecordedKeyEvent(event);
	if (event.what == evKeyDown && (event.keyDown.controlKeyState & kbCtrlShift) != 0 && (event.keyDown.controlKeyState & kbAltShift) != 0 && (event.keyDown.controlKeyState & kbShift) == 0) {
		const TKey pressed(event.keyDown);
		if ((pressed == TKey('Z', kbCtrlShift | kbAltShift) || event.keyDown.keyCode == kbCtrlZ || event.keyDown.keyCode == kbAltZ) && TView::commandEnabled(cmMrEditRedo) && handleMRCommand(cmMrEditRedo)) {
			clearEvent(event);
			return;
		}
	}

	if (event.what == evCommand && event.message.command == cmMrDeferredActivateWindow) {
		mrLogMessage("MREditorApp handling cmMrDeferredActivateWindow");
		static_cast<void>(mrDispatchDeferredWindowActivation());
		mrvmUiInvalidateScreenBase();
		clearEvent(event);
		return;
	}
	if (event.what == evCommand && event.message.command == cmMrEnsureUsableWorkWindow) {
		mrLogMessage("MREditorApp handling cmMrEnsureUsableWorkWindow");
		static_cast<void>(mrEnsureUsableWorkWindow(false));
		mrvmUiInvalidateScreenBase();
		clearEvent(event);
		return;
	}
	if (event.what == evCommand && event.message.command == cmMrHelpPerformancePanel) {
		togglePerformancePanel();
		clearEvent(event);
		return;
	}
	if (event.what == evCommand && (event.message.command == cmMrEditUndo || event.message.command == cmMrEditRedo) && handleMRCommand(event.message.command, event.message.infoPtr)) {
		clearEvent(event);
		return;
	}
	if (mrHandleFileCompareCommand(event)) return;
	if (mrHandleMacroDebuggerCommand(mrCurrentMacroDebuggerBentoBox(), event)) return;
	if (event.what == evKeyDown && currentEditWindow() == nullptr) {
		std::string executedMacroName;
		if (mrvmRunAssignedMacroForKey(event.keyDown.keyCode, event.keyDown.controlKeyState, executedMacroName, nullptr)) {
			traceCalculatorHotkeyEvent("app-macro-consumed", event);
			clearEvent(event);
			return;
		}
		if (mrHandleStartupFunctionKey(event)) {
			traceCalculatorHotkeyEvent("app-startup-fkey-consumed", event);
			clearEvent(event);
			return;
		}
	}
	if (event.what == evKeyDown && mrHandleFileCompareFunctionKey(event)) {
		traceCalculatorHotkeyEvent("app-file-compare-fkey-consumed", event);
		return;
	}
	if (event.what == evKeyDown && mrHandleMacroDebuggerFunctionKey(mrCurrentMacroDebuggerBentoBox(), event)) {
		traceCalculatorHotkeyEvent("app-macro-debugger-fkey-consumed", event);
		return;
	}
	if (event.what == evKeyDown && mrEditorFunctionKeyContextActive() && mrHandleEditorFunctionKey(event)) {
		traceCalculatorHotkeyEvent("app-editor-fkey-consumed", event);
		clearEvent(event);
		return;
	}

	if (event.what == evCommand && event.message.command == cmQuit && !exitPrepared) {
		for (MREditWindow *window : allEditWindowsInZOrder())
			if (window != nullptr && window->isFileChanged()) {
				clearEvent(event);
				static_cast<void>(requestMRExitWithDirtyGating());
				return;
			}
		prepareForQuit();
	}
	if (isBuildCurrentFileDefaultKey(event) && TView::commandEnabled(cmMrOtherBuildCurrentFile) && handleMRCommand(cmMrOtherBuildCurrentFile)) {
		clearEvent(event);
		return;
	}
	const bool fullscreenMenuBarActivation = fullscreenPresentationActive && menuBar != nullptr && ((event.what == evKeyDown && TKey(event.keyDown) == TKey(kbF10)) || (event.what == evCommand && event.message.command == cmMenu));
	if (fullscreenMenuBarActivation) {
		fullscreenMenuBarTransientVisible = true;
		applyConfiguredDisplayLayout();
	}
	TApplication::handleEvent(event);
	traceCalculatorHotkeyEvent("app-post", event);
	if (shouldInvalidateScreenBaseForEvent(originalWhat)) mrvmUiInvalidateScreenBase();

	auto restoreFullscreenMenuLayout = [this, fullscreenMenuBarActivation]() {
		if (!fullscreenMenuBarActivation || !fullscreenPresentationActive) return;
		fullscreenMenuBarTransientVisible = false;
		applyConfiguredDisplayLayout();
	};

	if (event.what != evCommand) {
		restoreFullscreenMenuLayout();
		return;
	}
	if (auto *mrMenuBar = dynamic_cast<MRMenuBar *>(menuBar); mrMenuBar != nullptr && mrMenuBar->handleRuntimeCommand(event.message.command)) {
		clearEvent(event);
		restoreFullscreenMenuLayout();
		return;
	}
	if (handleMRCommand(event.message.command, event.message.infoPtr)) clearEvent(event);
	restoreFullscreenMenuLayout();
}
