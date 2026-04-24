#ifndef MRWINDOWSUPPORT_HPP
#define MRWINDOWSUPPORT_HPP

#include <string_view>

class MREditWindow;
struct MRSettingsWriteReport;

[[nodiscard]] bool mrActivateEditWindow(MREditWindow *win);
void mrScheduleWindowActivation(MREditWindow *win);
[[nodiscard]] bool mrDispatchDeferredWindowActivation();
[[nodiscard]] bool mrShowProjectHelp();
[[nodiscard]] bool mrEnsureLogWindow(bool activate = true);
[[nodiscard]] bool mrEnsureUsableWorkWindow();
[[nodiscard]] bool mrClearLogWindow();
void mrLogMessage(std::string_view message);
void mrLogSettingsWriteReport(std::string_view reason, const MRSettingsWriteReport &report);
void mrSetKeystrokeRecordingActive(bool active);
[[nodiscard]] bool mrIsKeystrokeRecordingActive();
void mrSetKeystrokeRecordingMarkerVisible(bool visible);
[[nodiscard]] bool mrIsKeystrokeRecordingMarkerVisible();
void mrSetMacroBrainMarkerActive(bool active);
[[nodiscard]] bool mrIsMacroBrainMarkerActive();
void mrSetMacroBrainMarkerVisible(bool visible);
[[nodiscard]] bool mrIsMacroBrainMarkerVisible();

#endif
