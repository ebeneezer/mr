#define Uses_TKeys
#define Uses_MsgBox
#define Uses_TDialog
#define Uses_TStaticText
#define Uses_TFileDialog
#define Uses_TButton
#define Uses_TObject
#define Uses_TApplication
#define Uses_TEvent
#define Uses_TRect
#define Uses_TView
#define Uses_TDrawBuffer
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_TDeskTop
#define Uses_TScreen
#include <tvision/tv.h>

#include "MREditorApp.hpp"

#include "../coprocessor/MRCoprocessor.hpp"
#include "../mrmac/MRVM.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../coprocessor/MRCoprocessorDispatch.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
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
#include "MRAppState.hpp"
#include "MRCommandRouter.hpp"
#include "MRFunctionKeyBindings.hpp"
#include "MRMenuFactory.hpp"
#include "MRMacroDebuggerCommandRoute.hpp"
#include "MRRuntimeTimerSource.hpp"
#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace {
static constexpr std::chrono::seconds kFullscreenHintDuration(3);
static constexpr const char *kFullscreenHintText = "F11/ESC exit Fullscreen   F10 Menu";
int fullscreenHintTextWidth() noexcept {
	return strwidth(kFullscreenHintText);
}

class MRFullscreenHintView final : public TView {
  public:
	explicit MRFullscreenHintView(const TRect &bounds) noexcept : TView(bounds) {
	}

	void draw() override {
		TDrawBuffer buffer;
		const TColorAttr color = TColorAttr(0x1F);

		buffer.moveStr(0, kFullscreenHintText, color, static_cast<ushort>(size.x));
		writeLine(0, 0, size.x, 1, buffer);
	}
};
std::string buildTopRightCursorStatus(const std::string &markerFormat) {
	MREditWindow *win = currentEditWindow();
	if (win == nullptr || win->getEditor() == nullptr) return std::string();
	if (isEmptyUntitledEditableWindow(win)) return std::string();

	std::string format = markerFormat;
	std::string out;
	const std::string rowText = std::to_string(win->cursorLineNumber());
	const std::string colText = std::to_string(win->cursorColumnNumber());

	if (format.empty()) format = "R:C";
	out.reserve(format.size() + rowText.size() + colText.size());
	for (char ch : format) {
		if (ch == 'R') out += rowText;
		else if (ch == 'C')
			out += colText;
		else
			out.push_back(ch);
	}
	return out;
}

MRMenuBar::MarqueeKind mapMessageNoticeKind(mr::messageline::Kind kind) {
	switch (kind) {
		case mr::messageline::Kind::Success:
			return MRMenuBar::MarqueeKind::Success;
		case mr::messageline::Kind::Warning:
			return MRMenuBar::MarqueeKind::Warning;
		case mr::messageline::Kind::Error:
			return MRMenuBar::MarqueeKind::Error;
		case mr::messageline::Kind::Info:
		default:
			return MRMenuBar::MarqueeKind::Info;
	}
}

std::vector<MRMenuBar::MarqueeSegment> mapMessageNoticeSegments(const std::vector<mr::messageline::VisibleMessage::Segment> &segments) {
	std::vector<MRMenuBar::MarqueeSegment> mapped;

	mapped.reserve(segments.size());
	for (const mr::messageline::VisibleMessage::Segment &segment : segments)
		mapped.push_back(MRMenuBar::MarqueeSegment{segment.text, mapMessageNoticeKind(segment.kind)});
	return mapped;
}

bool isHeroVisibleMessage(const mr::messageline::VisibleMessage &visible) {
	mr::messageline::VisibleMessage ownerMessage;
	if (mr::messageline::currentOwnerMessage(mr::messageline::Owner::HeroEvent, ownerMessage) && ownerMessage.kind == visible.kind && ownerMessage.text == visible.text) return true;
	if (mr::messageline::currentOwnerMessage(mr::messageline::Owner::HeroEventFollowup, ownerMessage) && ownerMessage.kind == visible.kind && ownerMessage.text == visible.text) return true;
	if (mr::messageline::currentOwnerMessage(mr::messageline::Owner::MacroBrain, ownerMessage) && ownerMessage.kind == visible.kind && ownerMessage.text == visible.text) return true;
	return false;
}

const TPalette &extendedAppBasePalette() {
	static const TPalette palette = []() -> TPalette {
		static const int kBaseSlots = 135;
		static const int kTotalSlots = kMrPaletteMax;
		static const char cp[] = cpAppColor;
		TColorAttr data[kTotalSlots];
		int i = 0;

		for (i = 0; i < kBaseSlots; ++i)
			data[i] = static_cast<unsigned char>(cp[i]);
		for (; i < kTotalSlots; ++i)
			data[i] = data[1 - 1];
		// Dedicated editor-only accent slots (avoid window frame/scrollbar side effects).
		data[kMrPaletteCurrentLine - 1] = data[10 - 1];
		data[kMrPaletteCurrentLineInBlock - 1] = data[12 - 1];
		data[kMrPaletteChangedText - 1] = data[14 - 1];
		data[kMrPaletteMessageError - 1] = data[42 - 1];
		data[kMrPaletteMessage - 1] = data[43 - 1];
		data[kMrPaletteMessageWarning - 1] = data[44 - 1];
		data[kMrPaletteMessageHero - 1] = data[43 - 1];
		data[kMrPaletteCursorPositionMarker - 1] = data[3 - 1];
		data[kMrPaletteLineNumbers - 1] = data[9 - 1];
		data[kMrPaletteEofMarker - 1] = data[14 - 1];
		data[kMrPaletteDialogInactiveElements - 1] = data[62 - 1];
		data[kMrPaletteMiniMapNormal - 1] = data[13 - 1];
		data[kMrPaletteMiniMapViewport - 1] = data[11 - 1];
		data[kMrPaletteMiniMapChanged - 1] = data[14 - 1];
		data[kMrPaletteMiniMapFindMarker - 1] = data[5 - 1];
		data[kMrPaletteMiniMapErrorMarker - 1] = data[42 - 1];
		data[kMrPaletteCodeFolding - 1] = data[9 - 1];
		data[kMrPaletteStatusLine - 1] = data[2 - 1];
		data[kMrPaletteStatusLineBold - 1] = data[3 - 1];
		data[kMrPaletteStatusLineFunctionDescription - 1] = data[4 - 1];
		data[kMrPaletteStatusLineFunctionKey - 1] = data[5 - 1];
		data[kMrPaletteDesktop - 1] = 0x90;
		data[kMrPaletteVirtualDesktopMarker - 1] = 0x9F;
		data[kMrPaletteDiagnosticInformation - 1] = 0x4E;
		data[kMrPaletteDebuggerBreakpointActive - 1] = 0x4E;
		data[kMrPaletteDebuggerBreakpointInactive - 1] = 0x18;
		data[kMrPaletteDebuggerBreakpointUnbound - 1] = 0x4C;
		data[kMrPaletteDebuggerWatchpointActive - 1] = 0x3E;
		data[kMrPaletteDebuggerWatchpointInactive - 1] = 0x38;
		data[kMrPaletteDebuggerWatchpointError - 1] = 0x4F;
		data[kMrPaletteDebuggerInstructionPointer - 1] = 0xE0;
		data[kMrPaletteDebuggerExecutionLine - 1] = 0x1E;
		data[kMrPaletteDebuggerStackFrame - 1] = 0x3F;
		data[kMrPaletteDebuggerValueChanged - 1] = 0x2E;
		data[kMrPaletteDebuggerInputActive - 1] = 0x1B;
		data[kMrPaletteDebuggerInputError - 1] = 0x4F;
		return TPalette(data, static_cast<ushort>(kTotalSlots));
	}();
	return palette;
}
} // namespace
void MREditorApp::applyConfiguredWindowFramePolicy() {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();

	for (auto win : windows) {
		if (win == nullptr) continue;
		win->flags |= (wfMove | wfGrow | wfZoom | wfClose);
		if (win->fullscreenPresentation()) continue;
		if (win->frame != nullptr) win->frame->drawView();
	}
}

void MREditorApp::initializePerformancePanel() {
	if (performancePanel != nullptr) return;

	TRect appRect = getExtent();
	TRect panelRect(0, 1, appRect.b.x - appRect.a.x, 1 + MRPerformancePanel::kPreferredHeight);

	performancePanel = new MRPerformancePanel(panelRect);
	insert(performancePanel);
	performancePanel->hide();
}

void MREditorApp::initializeFullscreenHint() {
	if (fullscreenHint != nullptr) return;

	TRect appRect = getExtent();
	const int appWidth = std::max(1, static_cast<int>(appRect.b.x - appRect.a.x));
	const int appHeight = std::max(1, static_cast<int>(appRect.b.y - appRect.a.y));
	const int hintWidth = std::max(1, std::min(fullscreenHintTextWidth(), appWidth));
	const int hintLeft = std::max(0, (appWidth - hintWidth) / 2);
	const int hintTop = appHeight - 1;
	TRect hintRect(hintLeft, hintTop, hintLeft + hintWidth, hintTop + 1);

	fullscreenHint = new MRFullscreenHintView(hintRect);
	insert(fullscreenHint);
	fullscreenHint->hide();
}

void MREditorApp::togglePerformancePanel() {
	performancePanelVisible = !performancePanelVisible;
	applyConfiguredDisplayLayout();
	updatePerformancePanel();
}

void MREditorApp::updatePerformancePanel() {
	static constexpr std::chrono::milliseconds kPanelRefreshInterval(250);
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	if (!performancePanelVisible || performancePanel == nullptr) return;
	if (now < performancePanelRefreshAt) return;
	performancePanelRefreshAt = now + kPanelRefreshInterval;
	performancePanel->refresh();
}

void MREditorApp::updateFullscreenHint() {
	if (fullscreenHint == nullptr) initializeFullscreenHint();
	if (fullscreenHint == nullptr) return;

	const auto now = std::chrono::steady_clock::now();
	if (fullscreenPresentationActive && fullscreenWindow != nullptr && !fullscreenTargetStillOpen()) fullscreenWindow = nullptr;
	const bool fullscreenDesktopEmpty = fullscreenPresentationActive && fullscreenWindow == nullptr && currentEditWindow() == nullptr;
	const bool fullscreenHintTimed = fullscreenPresentationActive && now < fullscreenHintVisibleUntil;
	const bool hintVisible = fullscreenPresentationActive && (fullscreenDesktopEmpty || fullscreenHintTimed);

	if (!hintVisible) {
		fullscreenHint->hide();
		return;
	}

	TRect appRect = getExtent();
	const int appWidth = static_cast<int>(appRect.b.x - appRect.a.x);
	const int appHeight = static_cast<int>(appRect.b.y - appRect.a.y);
	if (appWidth <= 0 || appHeight <= 0) {
		fullscreenHint->hide();
		return;
	}

	const int hintWidth = std::max(1, std::min(fullscreenHintTextWidth(), appWidth));
	const int hintLeft = std::max(0, (appWidth - hintWidth) / 2);
	const int hintTop = appHeight - 1;
	TRect hintRect(hintLeft, hintTop, hintLeft + hintWidth, hintTop + 1);

	fullscreenHint->locate(hintRect);
	fullscreenHint->show();
	fullscreenHint->drawView();
}

bool MREditorApp::fullscreenTargetStillOpen() const {
	if (fullscreenWindow == nullptr) return false;
	for (MREditWindow *window : allEditWindowsInZOrder())
		if (window == fullscreenWindow) return true;
	return false;
}

bool MREditorApp::enterFullscreenPresentation() {
	MREditWindow *window = currentEditWindow();

	if (fullscreenPresentationActive) return true;
	fullscreenWindow = window;
	if (window != nullptr) {
		if (window->isMinimized()) window->restoreWindow();
		fullscreenRestoreBounds = window->getBounds();
	}
	fullscreenPresentationActive = true;
	fullscreenMenuBarTransientVisible = false;
	fullscreenHintVisibleUntil = std::chrono::steady_clock::now() + kFullscreenHintDuration;
	if (window != nullptr) window->setFullscreenPresentation(true);
	applyConfiguredDisplayLayout();
	if (window != nullptr) window->select();
	mrvmUiInvalidateScreenBase();
	return true;
}

void MREditorApp::leaveFullscreenPresentation() {
	MREditWindow *window = fullscreenTargetStillOpen() ? fullscreenWindow : nullptr;
	TRect restoreBounds = fullscreenRestoreBounds;

	if (!fullscreenPresentationActive) return;
	fullscreenPresentationActive = false;
	fullscreenMenuBarTransientVisible = false;
	fullscreenHintVisibleUntil = std::chrono::steady_clock::time_point::min();
	fullscreenWindow = nullptr;
	if (window != nullptr) window->setFullscreenPresentation(false);
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEvent, "Fullscreen ended - welcome back!", mr::messageline::Kind::Info, mr::messageline::kPriorityHigh);
	applyConfiguredDisplayLayout();
	if (window != nullptr) {
		window->locate(restoreBounds);
		window->select();
		window->drawView();
	}
	mrvmUiInvalidateScreenBase();
}

void MREditorApp::toggleFullscreenPresentation() {
	if (fullscreenPresentationActive) leaveFullscreenPresentation();
	else
		static_cast<void>(enterFullscreenPresentation());
}

void MREditorApp::syncFunctionKeyState() {
	const bool startupActive = currentEditWindow() == nullptr;
	const bool editorActive = !startupActive && mrEditorFunctionKeyContextActive();

	if (auto *mrStatus = dynamic_cast<MRStatusLine *>(statusLine)) {
		if (snippetSidekickHintsActive) {
			mrStatus->setContextFunctionKeysActive(false);
			mrStatus->setContextHintLabels(mrSnippetSidekickHintLabels());
			mrStatus->setContextHintLabelsActive(true);
		} else if (startupActive) {
			mrStatus->setContextHintLabelsActive(false);
			mrStatus->setContextFunctionKeyLabels(mrStartupFunctionKeyLabels(functionKeyModifiers));
			mrStatus->setContextFunctionKeysActive(true);
		} else if (editorActive) {
			mrStatus->setContextHintLabelsActive(false);
			mrStatus->setContextFunctionKeyLabels(mrEditorFunctionKeyLabels(functionKeyModifiers));
			mrStatus->setContextFunctionKeysActive(true);
		} else {
			mrStatus->setContextHintLabelsActive(false);
			mrStatus->setContextFunctionKeysActive(false);
		}
	}
	if (auto *mrMenuBar = dynamic_cast<MRMenuBar *>(menuBar)) {
		mrMenuBar->setStartupFunctionKeysActive(startupActive);
		mrMenuBar->setEditorFunctionKeysActive(editorActive);
	}
}

void MREditorApp::applyConfiguredDisplayLayout() {
	if (fullscreenPresentationActive && !fullscreenTargetStillOpen()) {
		fullscreenWindow = nullptr;
	}
	if (fullscreenPresentationActive && fullscreenWindow == nullptr) {
		if (MREditWindow *window = currentEditWindow(); window != nullptr) {
			if (window->isMinimized()) window->restoreWindow();
			fullscreenWindow = window;
			fullscreenRestoreBounds = window->getBounds();
			window->setFullscreenPresentation(true);
		}
	}
	const bool fullscreenActive = fullscreenPresentationActive;
	const bool fullscreenMenuBarVisible = fullscreenActive && fullscreenMenuBarTransientVisible;
	bool statusVisible = !fullscreenActive;
	TRect appRect = getExtent();
	TRect desktopRect;
	const int appHeight = appRect.b.y - appRect.a.y;
	const int maxPanelHeight = std::max(0, appHeight - 2);
	const int panelHeight = !fullscreenActive && performancePanelVisible ? std::min(MRPerformancePanel::kPreferredHeight, maxPanelHeight) : 0;

	if (menuBar != nullptr) {
		if (auto *mrMenuBar = dynamic_cast<MRMenuBar *>(menuBar)) mrMenuBar->setFullscreenPresentation(fullscreenActive);
		if (fullscreenActive && !fullscreenMenuBarVisible) menuBar->hide();
		else
			menuBar->show();
	}
	if (auto *mrStatus = dynamic_cast<MRStatusLine *>(statusLine)) {
		mrStatus->setShowFunctionKeyLabels(true);
		if (fullscreenActive) mrStatus->hide();
		else
			mrStatus->show();
	}
	syncFunctionKeyState();
	if (performancePanel != nullptr) {
		if (!fullscreenActive && panelHeight > 0) {
			TRect panelRect(0, 1, appRect.b.x - appRect.a.x, 1 + panelHeight);
			performancePanel->locate(panelRect);
			performancePanel->show();
			performancePanel->drawView();
		} else {
			performancePanel->hide();
		}
	}
	desktopRect.a.x = 0;
	desktopRect.b.x = appRect.b.x - appRect.a.x;
	desktopRect.a.y = fullscreenActive ? (fullscreenMenuBarVisible ? 1 : 0) : 1 + panelHeight;
	desktopRect.b.y = appRect.b.y - appRect.a.y - (statusVisible ? 1 : 0);
	if (desktopRect.b.y <= desktopRect.a.y) desktopRect.b.y = desktopRect.a.y + 1;
	if (deskTop != nullptr) deskTop->locate(desktopRect);
	applyConfiguredWindowFramePolicy();
	MRWindowLayout::handleDesktopLayoutChange();
	if (fullscreenActive && fullscreenWindow != nullptr && deskTop != nullptr) {
		TRect fullscreenBounds = deskTop->getExtent();
		fullscreenWindow->setFullscreenPresentation(true);
		fullscreenWindow->locate(fullscreenBounds);
		fullscreenWindow->select();
	}
	if (deskTop != nullptr) deskTop->drawView();
	if (menuBar != nullptr) menuBar->drawView();
	if (statusLine != nullptr) statusLine->drawView();
	updateFullscreenHint();
}

void MREditorApp::idle() {
	if (startupQuitPending) {
		TEvent quitEvent{};

		startupQuitPending = false;
		quitEvent.what = evCommand;
		quitEvent.message.command = cmQuit;
		putEvent(quitEvent);
	}
	TApplication::idle();
	if (interactiveMouseCaptureDepth > 0) return;
	pumpRuntimeTimerSource();
	pumpForegroundMacroDelays();
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);

		if (bentoBox != nullptr) bentoBox->pumpMacroDebuggerSession();
	}
	updateRecordingBlink();
	updateMacroBrainBlink();
	mr::coprocessor::globalCoprocessor().pumpFor(coprocessorPumpBudget);
	pumpDeferredMacroUiPlayback();
	mrFlushWorkspaceAutosaveIfDue();
	updatePerformancePanel();
	updateFullscreenHint();
	if (auto *mrMenuBar = dynamic_cast<MRMenuBar *>(menuBar)) {
		mr::messageline::VisibleMessage message;
		std::string rightStatus = buildTopRightCursorStatus(cursorPositionMarkerFormat);
		mrMenuBar->setRightStatus(rightStatus);
		mrMenuBar->setPersistentBlocksMenuState(persistentBlocksMenuEnabled);
		if (MREditWindow *win = currentEditWindow(); win != nullptr) {
			mrMenuBar->setInsertModeMenuState(win->insertModeEnabled());
			mrMenuBar->setLineDrawingMenuState(win->lineDrawingEnabled(), win->lineDrawingDoubleLines());
		} else {
			mrMenuBar->setInsertModeMenuState(false);
			mrMenuBar->setLineDrawingMenuState(false, false);
		}
		if (mr::messageline::currentVisibleMessage(message)) {
			MRMenuBar::MarqueeKind marqueeKind = mapMessageNoticeKind(message.kind);
			if (isHeroVisibleMessage(message)) marqueeKind = MRMenuBar::MarqueeKind::Hero;
			if (!message.segments.empty()) mrMenuBar->setAutoMarqueeStatusSegments(mapMessageNoticeSegments(message.segments), marqueeKind);
			else
				mrMenuBar->setAutoMarqueeStatus(message.text, marqueeKind);
		} else
			mrMenuBar->setAutoMarqueeStatus(std::string());
		mrMenuBar->tickMarquee();
	}
	{
		std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
		for (auto *window : windows) {
			if (window == nullptr || window->frame == nullptr) continue;
			if (auto *mrFrame = dynamic_cast<MRFrame *>(window->frame)) mrFrame->tickTaskOverviewAnimation();
		}
	}
	MRWindowLayout::handleDesktopLayoutChange();
	updateAppCommandState(virtualDesktopCount, cyclicVirtualDesktopsEnabled);
	syncFunctionKeyState();
	if (auto *mrStatus = dynamic_cast<MRStatusLine *>(statusLine)) mrStatus->tickFunctionKeyLabelTransitions();
}

TPalette &MREditorApp::getPalette() const {
	static const TPalette &basePalette = extendedAppBasePalette();
	static TPalette palette = basePalette;
	const MRColorSetupSettings configuredColors = configuredColorSetupSettings();
	unsigned char overrideValue = 0;
	int slot = 0;

	// Rebuild from TV default on every call so stale overrides never leak between frames.
	palette = basePalette;

	for (slot = 1; slot <= kMrPaletteMax; ++slot)
		if (colorSlotOverride(configuredColors, static_cast<unsigned char>(slot), overrideValue)) palette[slot] = overrideValue;

	// TVision-wide policy: Dialog scrollbars follow dialog frame color globally.
	// Applies to gray/blue/cyan dialog palette blocks, no per-view exceptions.
	auto syncDialogScrollbarsToFrame = [&](int base) {
		palette[base + 3] = palette[base + 0];  // slot 4: scrollbar page
		palette[base + 4] = palette[base + 0];  // slot 5: scrollbar controls
		palette[base + 23] = palette[base + 0]; // slot 24: history scrollbar page
		palette[base + 24] = palette[base + 0]; // slot 25: history scrollbar controls
	};
	// Blue/cyan/gray window scrollbars should not drift away from the window frame.
	auto syncWindowScrollbarsToFrame = [&](int base) {
		palette[base + 2] = palette[base + 0]; // slot 3: scrollbar page
		palette[base + 3] = palette[base + 0]; // slot 4: scrollbar controls / thumb
	};
	syncDialogScrollbarsToFrame(32);
	syncDialogScrollbarsToFrame(64);
	syncDialogScrollbarsToFrame(96);
	syncWindowScrollbarsToFrame(8);
	syncWindowScrollbarsToFrame(16);
	syncWindowScrollbarsToFrame(24);

	palette[1] = palette[kMrPaletteDesktop];
	return palette;
}
