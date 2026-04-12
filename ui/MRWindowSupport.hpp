#ifndef MRWINDOWSUPPORT_HPP
#define MRWINDOWSUPPORT_HPP

#include <string_view>

class TMREditWindow;
struct MRSettingsWriteReport;

[[nodiscard]] bool mrActivateEditWindow(TMREditWindow *win);
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

#endif
