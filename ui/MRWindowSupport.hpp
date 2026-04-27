#ifndef MRWINDOWSUPPORT_HPP
#define MRWINDOWSUPPORT_HPP

#define Uses_TEvent
#include <tvision/tv.h>

#include <string>
#include <string_view>

class MREditWindow;
class MRKeymapToken;
struct MRSettingsWriteReport;
enum class MRKeymapContext : unsigned char;

[[nodiscard]] bool mrActivateEditWindow(MREditWindow *win);
void mrScheduleWindowActivation(MREditWindow *win);
[[nodiscard]] bool mrDispatchDeferredWindowActivation();
[[nodiscard]] bool mrShowProjectHelp();
[[nodiscard]] bool mrEnsureLogWindow(bool activate = true);
[[nodiscard]] bool mrEnsureUsableWorkWindow();
[[nodiscard]] bool mrClearLogWindow();
void mrLogMessage(std::string_view message);
bool mrAppendLogBufferToFile(const std::string &path, std::string *errorMessage = nullptr);
void mrLogSettingsWriteReport(std::string_view reason, const MRSettingsWriteReport &report);
bool mrKeyTokenFromEvent(ushort keyCode, ushort controlKeyState, std::string &outToken);
bool mrKeymapTokenFromEvent(ushort keyCode, ushort controlKeyState, MRKeymapToken &outToken);
bool mrHandleRuntimeKeymapEvent(TEvent &event, MRKeymapContext context, MREditWindow *targetWindow = nullptr);
bool mrCaptureBindingKeySpec(const char *title, const char *prompt, std::string &keySpec);
void mrSetKeystrokeRecordingActive(bool active);
[[nodiscard]] bool mrIsKeystrokeRecordingActive();
void mrSetKeystrokeRecordingMarkerVisible(bool visible);
[[nodiscard]] bool mrIsKeystrokeRecordingMarkerVisible();
void mrSetMacroBrainMarkerActive(bool active);
[[nodiscard]] bool mrIsMacroBrainMarkerActive();
void mrSetMacroBrainMarkerVisible(bool visible);
[[nodiscard]] bool mrIsMacroBrainMarkerVisible();

#endif
