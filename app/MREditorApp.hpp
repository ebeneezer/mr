#define Uses_TApplication
#define Uses_TEvent
#define Uses_TRect
#define Uses_TMenuBar
#define Uses_TStatusLine
#define Uses_TDeskTop
#ifndef MREDITORAPP_HPP
#define MREDITORAPP_HPP

#include <tvision/tv.h>
#include <chrono>
#include <string>
#include <vector>

#include "MRCommandRouter.hpp"
#include "../ui/MRHelpSystem.hpp"

class MRPerformancePanel;
class MREditWindow;

class MREditorApp : public TApplication {
  public:
	static TMenuBar *initMRMenuBar(TRect r);
	static TStatusLine *initMRStatusLine(TRect r);
	static TDeskTop *initMRDeskTop(TRect r);

	MREditorApp();
	~MREditorApp() override;

	void getEvent(TEvent &event) override;
	void handleEvent(TEvent &event) override;
	void idle() override;
	TPalette &getPalette() const override;
	[[nodiscard]] bool showHelpTopic(ushort context);
	[[nodiscard]] bool showPreviousHelpTopic();
	bool quitPrepared() const noexcept;
	void beginInteractiveMouseCapture() noexcept;
	void endInteractiveMouseCapture() noexcept;
	void refreshConfiguredUiSettingsSnapshot();
	void setSnippetSidekickHintsActive(bool active);
	void requestRestartAfterExit() noexcept;
	[[nodiscard]] bool restartAfterExitRequested() const noexcept;

  private:
	static constexpr std::chrono::milliseconds recordingBlinkInterval{450};
	static constexpr std::chrono::microseconds coprocessorPumpBudget{1000};

	void prepareForQuit();
	bool isRecorderToggleKey(const TEvent &event) const;
	bool isRecorderToggleCommand(const TEvent &event) const;
	void startKeystrokeRecording();
	void stopKeystrokeRecording();
	void finalizeKeystrokeRecording();
	void appendRecordedKeyEvent(const TEvent &event);
	bool captureBindingKeySpec(std::string &keySpec);
	void syncRecordingUiState();
	void redrawActiveMarkerFrame();
	void updateRecordingBlink();
	void updateMacroBrainBlink();
	void runConfiguredAutoexecMacros();
	void initializePerformancePanel();
	void initializeFullscreenHint();
	void togglePerformancePanel();
	void updatePerformancePanel();
	void updateFullscreenHint();
	void applyConfiguredDisplayLayout();
	void applyConfiguredWindowFramePolicy();
	void syncFunctionKeyState();
	bool fullscreenTargetStillOpen() const;
	bool enterFullscreenPresentation();
	void leaveFullscreenPresentation();
	void toggleFullscreenPresentation();

	bool exitPrepared;
	bool restartAfterExit;
	bool updateCheckStarted;
	bool keystrokeRecording;
	bool recordingMarkerVisible;
	bool macroBrainMarkerVisible;
	std::string recordedKeySequence;
	unsigned long recordedMacroCounter;
	std::vector<std::string> recordedSessionMacroFiles;
	std::chrono::steady_clock::time_point recordingBlinkToggleAt;
	std::chrono::steady_clock::time_point macroBrainBlinkToggleAt;
	bool performancePanelVisible;
	MRPerformancePanel *performancePanel;
	TView *fullscreenHint;
	std::chrono::steady_clock::time_point performancePanelRefreshAt;
	std::chrono::steady_clock::time_point fullscreenHintVisibleUntil;
	bool startupQuitPending;
	bool fullscreenPresentationActive;
	bool fullscreenMenuBarTransientVisible;
	MREditWindow *fullscreenWindow;
	TRect fullscreenRestoreBounds;
	int interactiveMouseCaptureDepth;
	std::string cursorPositionMarkerFormat;
	bool persistentBlocksMenuEnabled;
	bool snippetSidekickHintsActive;
	ushort functionKeyModifiers;
	int virtualDesktopCount;
	bool cyclicVirtualDesktopsEnabled;
	MRHelpSystem helpSystem;
};

void mrRefreshEditorApplicationUiSettingsSnapshot();
void mrSetSnippetSidekickHintsActive(bool active);
void mrRequestApplicationRestart();

#endif
