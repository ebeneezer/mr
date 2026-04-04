#define Uses_TApplication
#define Uses_TEvent
#define Uses_TRect
#define Uses_TMenuBar
#define Uses_TStatusLine
#define Uses_TDeskTop
#ifndef TMREDITORAPP_HPP
#define TMREDITORAPP_HPP

#include <tvision/tv.h>
#include <chrono>
#include <string>
#include <vector>

class TMREditorApp : public TApplication {
 public:
	static TMenuBar *initMRMenuBar(TRect r);
	static TStatusLine *initMRStatusLine(TRect r);
	static TDeskTop *initMRDeskTop(TRect r);

	TMREditorApp();
	~TMREditorApp() override;

	void handleEvent(TEvent &event) override;
	void idle() override;
	TPalette &getPalette() const override;
	bool reloadSettingsMacroFromPath(const std::string &path, std::string *errorMessage = nullptr);

 private:
	void prepareForQuit();
	bool isRecorderToggleKey(const TEvent &event) const;
	bool isRecorderToggleCommand(const TEvent &event) const;
	void startKeystrokeRecording();
	void stopKeystrokeRecording();
	void finalizeKeystrokeRecording();
	void appendRecordedKeyEvent(const TEvent &event);
	bool captureBindingKeySpec(std::string &keySpec);
	void syncRecordingUiState();
	void redrawRecordingMarkerFrames();
	void updateRecordingBlink();
	void bootstrapIndexedMacroBindings();
	void warmIndexedMacroBindings();
	void applyConfiguredDisplayLayout();
	void applyConfiguredWindowFramePolicy();

	bool exitPrepared_;
	bool keystrokeRecording_;
	bool recordingMarkerVisible_;
	std::string recordedKeySequence_;
	unsigned long recordedMacroCounter_;
	std::vector<std::string> recordedSessionMacroFiles_;
	std::chrono::steady_clock::time_point recordingBlinkToggleAt_;
	bool indexedMacroWarmupActive_;
	std::size_t indexedMacroWarmupLoadedFiles_;
};

// Regression-only hooks used by regression/mr-regression-checks.cpp.
bool mrApplySettingsSourceForTesting(const std::string &source, std::string *errorMessage = nullptr);
bool mrMigrateSettingsMacroToCurrentVersionForTesting(const std::string &settingsPath,
                                                      const std::string &source,
                                                      const std::string &reason,
                                                      std::string *errorMessage = nullptr);

#endif
