#define Uses_TApplication
#define Uses_TButton
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TEvent
#define Uses_MsgBox
#define Uses_TProgram
#define Uses_TStaticText
#define Uses_TWindowInit
#include <tvision/tv.h>

#include "MRWindowSupport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "../app/MRCommands.hpp"
#include "../app/MRCommandRouter.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../dialogs/MRSetupCommon.hpp"
#include "../keymap/MRKeymapResolver.hpp"
#include "../keymap/MRKeymapToken.hpp"
#include "MRMessageLineController.hpp"
#include "MREditWindow.hpp"

namespace {
constexpr std::string_view kHelpWindowTitle = "MR HELP";
constexpr std::string_view kLogWindowTitle = "MR LOG";

std::string g_logBuffer;
bool g_keystrokeRecordingActive = false;
bool g_keystrokeRecordingMarkerVisible = false;
bool g_macroBrainMarkerActive = false;
bool g_macroBrainMarkerVisible = false;
MREditWindow *g_deferredActivationWindow = nullptr;
[[nodiscard]] std::string baseNameOf(std::string_view path);

bool runtimeKeymapDebugEnabled() noexcept {
	static int cached = -1;

	if (cached < 0) {
		const char *value = std::getenv("MR_KEY_DEBUG");
		cached = (value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0) ? 1 : 0;
	}
	return cached == 1;
}

const char *runtimeKeymapContextName(MRKeymapContext context) noexcept {
	switch (context) {
		case MRKeymapContext::Menu:
			return "menu";
		case MRKeymapContext::Dialog:
			return "dialog";
		case MRKeymapContext::DialogList:
			return "dialog-list";
		case MRKeymapContext::List:
			return "list";
		case MRKeymapContext::ReadOnly:
			return "readonly";
		case MRKeymapContext::Edit:
			return "edit";
		case MRKeymapContext::None:
			break;
	}
	return "none";
}

const char *keymapResultName(MRKeymapResolver::ResultKind kind) noexcept {
	switch (kind) {
		case MRKeymapResolver::ResultKind::NoMatch:
			return "no-match";
		case MRKeymapResolver::ResultKind::Pending:
			return "pending";
		case MRKeymapResolver::ResultKind::Matched:
			return "matched";
		case MRKeymapResolver::ResultKind::Invalid:
			return "invalid";
		case MRKeymapResolver::ResultKind::Aborted:
			return "aborted";
	}
	return "unknown";
}

bool focusDebugEnabled() noexcept {
	return runtimeKeymapDebugEnabled();
}

std::string describeDesktopCurrentView() {
	TView *current = TProgram::deskTop != nullptr ? TProgram::deskTop->current : nullptr;
	MREditWindow *editWindow = dynamic_cast<MREditWindow *>(current);
	TDialog *dialog = dynamic_cast<TDialog *>(current);
	std::string line = "current=";

	if (editWindow != nullptr) {
		line += "edit('";
		line += editWindow->getTitle(0) != nullptr ? editWindow->getTitle(0) : "?";
		line += "')";
	} else if (dialog != nullptr) {
		line += "dialog('";
		line += dialog->getTitle(0) != nullptr ? dialog->getTitle(0) : "?";
		line += "')";
	} else if (current != nullptr)
		line += "other";
	else
		line += "null";
	if (current != nullptr) {
		line += " visible=";
		line += (current->state & sfVisible) != 0 ? "1" : "0";
		line += " selected=";
		line += (current->state & sfSelected) != 0 ? "1" : "0";
	}
	return line;
}

class TBindingKeyCaptureDialog : public MRDialogFoundation {
  public:
	TBindingKeyCaptureDialog(const char *title, const char *prompt) : TWindowInit(&TDialog::initFrame), MRDialogFoundation(centeredSetupDialogRect(52, 8), title != nullptr ? title : "Bind Key", 52, 8), captureAccepted(false), capturedKeyCode(kbNoKey), capturedControlState(0) {
		insert(new TStaticText(TRect(2, 2, 50, 6), prompt != nullptr ? prompt : "Press key to bind.\nEsc = cancel."));
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown) {
			const TKey pressed(event.keyDown);

			if (pressed == TKey(kbEsc)) {
				endModal(cmCancel);
				clearEvent(event);
				return;
			}
			captureAccepted = true;
			capturedKeyCode = event.keyDown.keyCode;
			capturedControlState = event.keyDown.controlKeyState;
			endModal(cmOK);
			clearEvent(event);
			return;
		}
		MRDialogFoundation::handleEvent(event);
	}

	[[nodiscard]] bool hasCaptured() const noexcept {
		return captureAccepted;
	}

	[[nodiscard]] ushort keyCode() const noexcept {
		return capturedKeyCode;
	}

	[[nodiscard]] ushort controlState() const noexcept {
		return capturedControlState;
	}

  private:
	bool captureAccepted;
	ushort capturedKeyCode;
	ushort capturedControlState;
};

void postWindowSupportError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, text, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

void postWindowSupportWarning(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, text, mr::messageline::Kind::Warning, mr::messageline::kPriorityHigh);
}

[[nodiscard]] std::string currentWorkingDirectory() {
	std::array<char, 1024> cwd{};
	if (::getcwd(cwd.data(), cwd.size()) == nullptr) return std::string();
	return std::string(cwd.data());
}

[[nodiscard]] std::string executableDirectory() {
	std::array<char, 4096> path{};
	const ssize_t len = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
	std::size_t pos;

	if (len <= 0) return std::string();
	path[static_cast<std::size_t>(len)] = '\0';
	pos = std::string_view(path.data()).find_last_of('/');
	if (pos == std::string_view::npos) return std::string();
	return std::string(path.data(), pos);
}

[[nodiscard]] std::string resolveHelpFilePath() {
	const std::string configured = configuredHelpFilePath();
	const std::string fromCwd = currentWorkingDirectory();
	const std::string fromExe = executableDirectory();
	std::string candidate;
	const std::string configuredName = baseNameOf(configured);

	if (!configured.empty() && ::access(configured.c_str(), R_OK) == 0) return configured;
	if (!fromCwd.empty()) {
		candidate = fromCwd + "/" + configuredName;
		if (::access(candidate.c_str(), R_OK) == 0) return candidate;
	}
	if (!fromExe.empty()) {
		candidate = fromExe + "/" + configuredName;
		if (::access(candidate.c_str(), R_OK) == 0) return candidate;
	}
	return configured;
}

[[nodiscard]] std::string currentTimestamp() {
	std::array<char, 32> buffer{};
	const std::time_t now = std::time(nullptr);
	const std::tm *tmNow = std::localtime(&now);

	if (tmNow == nullptr) return std::string("--:--:--");
	if (std::strftime(buffer.data(), buffer.size(), "%H:%M:%S", tmNow) == 0) return std::string("--:--:--");
	return std::string(buffer.data());
}

[[nodiscard]] std::string normalizeLogLine(std::string_view message) {
	std::string line(message);
	while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
		line.pop_back();
	return line;
}

[[nodiscard]] std::string baseNameOf(std::string_view path) {
	const std::size_t pos = path.find_last_of("\\/");
	if (pos == std::string_view::npos) return std::string(path);
	return std::string(path.substr(pos + 1));
}

[[nodiscard]] MREditWindow *findWindowByTitle(std::string_view title) {
	const std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	for (MREditWindow *window : windows) {
		const char *windowTitle = window != nullptr ? window->getTitle(0) : nullptr;
		if (windowTitle != nullptr && title == windowTitle) return window;
	}
	return nullptr;
}

[[nodiscard]] bool isReservedUtilityWindow(MREditWindow *win) {
	const char *title = win != nullptr ? win->getTitle(0) : nullptr;
	return title != nullptr && (kHelpWindowTitle == title || kLogWindowTitle == title);
}

[[nodiscard]] MREditWindow *chooseFallbackWorkWindow() {
	const std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	MREditWindow *hiddenEditable = nullptr;
	MREditWindow *hiddenNonUtility = nullptr;

	for (MREditWindow *window : windows) {
		if ((window->state & sfVisible) != 0) return window;
		if (hiddenEditable == nullptr && !window->isReadOnly()) hiddenEditable = window;
		if (hiddenNonUtility == nullptr && !isReservedUtilityWindow(window)) hiddenNonUtility = window;
	}
	if (hiddenEditable != nullptr) return hiddenEditable;
	if (hiddenNonUtility != nullptr) return hiddenNonUtility;
	return nullptr;
}

[[nodiscard]] MREditWindow *createReadOnlyTextWindow(const char *title, const char *text, bool hidden) {
	MREditWindow *previous;
	MREditWindow *win;

	if (TProgram::deskTop == nullptr) return nullptr;

	previous = dynamic_cast<MREditWindow *>(TProgram::deskTop->current);
	win = createEditorWindow(title);
	if (win == nullptr) return nullptr;
	if (!win->loadTextBuffer(text, title)) {
		message(win, evCommand, cmClose, nullptr);
		return nullptr;
	}
	win->setReadOnly(true);
	win->setFileChanged(false);
	setWindowManuallyHidden(win, hidden);
	if (hidden) win->hide();
	if (hidden && previous != nullptr && previous != win) static_cast<void>(mrActivateEditWindow(previous));
	return win;
}

[[nodiscard]] MREditWindow *ensureLogWindowInternal(bool activate) {
	MREditWindow *win = findWindowByTitle(kLogWindowTitle);

	if (g_logBuffer.empty()) g_logBuffer = "MR/MEMAC log initialized.\n";
	if (win == nullptr) win = createReadOnlyTextWindow(kLogWindowTitle.data(), g_logBuffer.c_str(), !activate);
	if (win == nullptr) return nullptr;
	win->replaceTextBuffer(g_logBuffer.c_str(), kLogWindowTitle.data());
	win->setWindowRole(MREditWindow::wrLog);
	win->setReadOnly(true);
	win->setFileChanged(false);
	if (activate) static_cast<void>(mrActivateEditWindow(win));
	return win;
}
} // namespace

bool mrActivateEditWindow(MREditWindow *win) {
	std::string line;

	if (win == nullptr) return false;
	if (focusDebugEnabled()) {
		line = "mrActivateEditWindow before target='";
		line += win->getTitle(0) != nullptr ? win->getTitle(0) : "?";
		line += "' visible=";
		line += (win->state & sfVisible) != 0 ? "1" : "0";
		line += " selected=";
		line += (win->state & sfSelected) != 0 ? "1" : "0";
		line += " ";
		line += describeDesktopCurrentView();
		mrLogMessage(line);
	}
	setWindowManuallyHidden(win, false);
	setCurrentVirtualDesktop(win->mVirtualDesktop);
	if ((win->state & sfVisible) == 0) win->show();
	win->select();
	if (focusDebugEnabled()) {
		line = "mrActivateEditWindow after target='";
		line += win->getTitle(0) != nullptr ? win->getTitle(0) : "?";
		line += "' visible=";
		line += (win->state & sfVisible) != 0 ? "1" : "0";
		line += " selected=";
		line += (win->state & sfSelected) != 0 ? "1" : "0";
		line += " ";
		line += describeDesktopCurrentView();
		mrLogMessage(line);
	}
	return true;
}

void mrScheduleWindowActivation(MREditWindow *win) {
	std::string line;
	if (win == nullptr) return;
	line = "mrScheduleWindowActivation target='";
	line += win->getTitle(0) != nullptr ? win->getTitle(0) : "?";
	line += "' visible=";
	line += (win->state & sfVisible) != 0 ? "1" : "0";
	line += " selected=";
	line += (win->state & sfSelected) != 0 ? "1" : "0";
	line += " hidden=";
	line += isWindowManuallyHidden(win) ? "1" : "0";
	mrLogMessage(line);
	g_deferredActivationWindow = win;
	if (TProgram::application == nullptr) {
		static_cast<void>(mrActivateEditWindow(win));
		g_deferredActivationWindow = nullptr;
		return;
	}
	message(TProgram::application, evCommand, cmMrDeferredActivateWindow, nullptr);
}

bool mrDispatchDeferredWindowActivation() {
	MREditWindow *win = g_deferredActivationWindow;
	std::string line;

	if (win == nullptr) return false;
	line = "mrDispatchDeferredWindowActivation before target='";
	line += win->getTitle(0) != nullptr ? win->getTitle(0) : "?";
	line += "' visible=";
	line += (win->state & sfVisible) != 0 ? "1" : "0";
	line += " selected=";
	line += (win->state & sfSelected) != 0 ? "1" : "0";
	line += " hidden=";
	line += isWindowManuallyHidden(win) ? "1" : "0";
	mrLogMessage(line);
	g_deferredActivationWindow = nullptr;
	if (!mrActivateEditWindow(win)) return false;
	line = "mrDispatchDeferredWindowActivation after target='";
	line += win->getTitle(0) != nullptr ? win->getTitle(0) : "?";
	line += "' visible=";
	line += (win->state & sfVisible) != 0 ? "1" : "0";
	line += " selected=";
	line += (win->state & sfSelected) != 0 ? "1" : "0";
	line += " hidden=";
	line += isWindowManuallyHidden(win) ? "1" : "0";
	mrLogMessage(line);
	return true;
}

bool mrShowProjectHelp() {
	MREditWindow *win;
	const std::string helpPath = resolveHelpFilePath();

	if (TProgram::deskTop == nullptr) return false;

	win = dynamic_cast<MREditWindow *>(TProgram::deskTop->current);
	if (win != nullptr) {
		const std::string currentFile = win->currentFileName();
		const char *title = win->getTitle(0);
		if ((!currentFile.empty() && baseNameOf(currentFile) == baseNameOf(helpPath)) || (title != nullptr && kHelpWindowTitle == title)) return true;
	}

	win = findWindowByTitle(kHelpWindowTitle);
	if (win == nullptr) {
		win = createEditorWindow(kHelpWindowTitle.data());
		if (win == nullptr) return false;

		if (!win->loadFromFile(helpPath.c_str())) {
			postWindowSupportError("Unable to load help file: " + helpPath);
			message(win, evCommand, cmClose, nullptr);
			return false;
		}

		win->setReadOnly(true);
		win->setFileChanged(false);
	}
	win->setWindowRole(MREditWindow::wrHelp, helpPath);
	static_cast<void>(mrActivateEditWindow(win));
	return true;
}

bool mrEnsureLogWindow(bool activate) {
	return ensureLogWindowInternal(activate) != nullptr;
}

bool mrClearLogWindow() {
	MREditWindow *win;

	g_logBuffer = "MR/MEMAC log initialized.\n";
	win = ensureLogWindowInternal(false);
	if (win == nullptr) return false;
	if (!win->replaceTextBuffer(g_logBuffer.c_str(), kLogWindowTitle.data())) return false;
	win->setWindowRole(MREditWindow::wrLog);
	win->setReadOnly(true);
	win->setFileChanged(false);
	return true;
}

bool mrEnsureUsableWorkWindow() {
	TView *currentView = TProgram::deskTop != nullptr ? TProgram::deskTop->current : nullptr;
	MREditWindow *current = dynamic_cast<MREditWindow *>(currentView);
	MREditWindow *fallback;

	if (TProgram::deskTop == nullptr) return false;
	if (currentView != nullptr && (currentView->state & sfVisible) != 0 && current == nullptr) return true;
	if (current != nullptr && (current->state & sfVisible) != 0) return true;
	fallback = chooseFallbackWorkWindow();
	if (fallback != nullptr) return mrActivateEditWindow(fallback);
	fallback = createEditorWindow("?No-File?");
	if (fallback == nullptr) return false;
	mrLogMessage("Created fallback empty window to keep the editor usable.");
	return mrActivateEditWindow(fallback);
}

void mrLogMessage(std::string_view message) {
	const std::string line = normalizeLogLine(message);
	MREditWindow *win;

	if (line.empty()) return;
	if (!g_logBuffer.empty() && g_logBuffer[g_logBuffer.size() - 1] != '\n') g_logBuffer += '\n';
	g_logBuffer += "[" + currentTimestamp() + "] " + line + "\n";
	win = ensureLogWindowInternal(false);
	if (win != nullptr) {
		win->replaceTextBuffer(g_logBuffer.c_str(), kLogWindowTitle.data());
		win->setReadOnly(true);
		win->setFileChanged(false);
	}
}

bool mrAppendLogBufferToFile(const std::string &path, std::string *errorMessage) {
	std::ofstream out(path, std::ios::out | std::ios::app | std::ios::binary);

	if (!out) {
		if (errorMessage != nullptr) *errorMessage = "Unable to open log file for append: " + path;
		return false;
	}
	out << g_logBuffer;
	if (!out.good()) {
		if (errorMessage != nullptr) *errorMessage = "Unable to append log file: " + path;
		return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

void mrLogSettingsWriteReport(std::string_view reason, const MRSettingsWriteReport &report) {
	if (!report.contentChanged && report.logLines.empty()) return;
	if (!reason.empty()) mrLogMessage(std::string("settings.mrmac write (") + std::string(reason) + "): " + report.settingsPath);
	for (const std::string &line : report.logLines)
		mrLogMessage(line);
}

bool mrKeyTokenFromEvent(ushort keyCode, ushort controlKeyState, std::string &outToken) {
	struct ComboSpec {
		const char *prefix;
		ushort mods;
	};
	struct NamedKeySpec {
		const char *token;
		ushort code;
	};
	static const ComboSpec combos[] = {{"", 0}, {"Shft", kbShift}, {"Ctrl", kbCtrlShift}, {"Alt", kbAltShift}, {"CtrlShft", static_cast<ushort>(kbCtrlShift | kbShift)}, {"AltShft", static_cast<ushort>(kbAltShift | kbShift)}, {"CtrlAlt", static_cast<ushort>(kbCtrlShift | kbAltShift)}, {"CtrlAltShft", static_cast<ushort>(kbCtrlShift | kbAltShift | kbShift)}};
	static const NamedKeySpec named[] = {{"Enter", kbEnter}, {"Tab", kbTab}, {"Esc", kbEsc}, {"Backspace", kbBack}, {"Up", kbUp}, {"Down", kbDown}, {"Left", kbLeft}, {"Right", kbRight}, {"PgUp", kbPgUp}, {"PgDn", kbPgDn}, {"Home", kbHome}, {"End", kbEnd}, {"Ins", kbIns}, {"Del", kbDel}, {"Grey-", kbGrayMinus}, {"Grey+", kbGrayPlus}, {"Grey*", static_cast<ushort>('*')}, {"Space", static_cast<ushort>(' ')}, {"Minus", static_cast<ushort>('-')}, {"Equal", static_cast<ushort>('=')}, {"F1", kbF1}, {"F2", kbF2}, {"F3", kbF3}, {"F4", kbF4}, {"F5", kbF5}, {"F6", kbF6}, {"F7", kbF7}, {"F8", kbF8}, {"F9", kbF9}, {"F10", kbF10}, {"F11", kbF11}, {"F12", kbF12}};
	const TKey pressed(keyCode, controlKeyState);

	for (const ComboSpec &combo : combos)
		for (const NamedKeySpec &entry : named)
			if (pressed == TKey(entry.code, combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken += entry.token;
				outToken += ">";
				return true;
			}

	for (const ComboSpec &combo : combos) {
		for (char c = 'A'; c <= 'Z'; ++c)
			if (pressed == TKey(static_cast<ushort>(c), combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken.push_back(c);
				outToken += ">";
				return true;
			}
		for (char c = '0'; c <= '9'; ++c)
			if (pressed == TKey(static_cast<ushort>(c), combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken.push_back(c);
				outToken += ">";
				return true;
			}
	}

	if (keyCode >= 1 && keyCode <= 26 && (controlKeyState & (kbAltShift | kbPaste)) == 0) {
		outToken = "<Ctrl";
		outToken.push_back(static_cast<char>('A' + keyCode - 1));
		outToken += ">";
		return true;
	}

	if (keyCode != kbNoKey && keyCode < 256 && std::isprint(static_cast<unsigned char>(keyCode)) != 0) {
		outToken = "<";
		outToken.push_back(static_cast<char>(keyCode));
		outToken += ">";
		return true;
	}
	return false;
}

bool mrKeymapTokenFromEvent(ushort keyCode, ushort controlKeyState, MRKeymapToken &outToken) {
	std::string tokenText;
	const auto parsed = mrKeyTokenFromEvent(keyCode, controlKeyState, tokenText) ? MRKeymapToken::parse(tokenText) : std::nullopt;

	if (!parsed) return false;
	outToken = *parsed;
	return true;
}

bool mrHandleRuntimeKeymapEvent(TEvent &event, MRKeymapContext context, MREditWindow *targetWindow) {
	MRKeymapToken token(MRKeymapBaseKey::Esc, 0);
	std::string tokenText;
	char line[512];

	if (event.what != evKeyDown || !mrKeymapTokenFromEvent(event.keyDown.keyCode, event.keyDown.controlKeyState, token)) return false;
	tokenText = token.toString();

	const MRKeymapResolver::Result result = runtimeKeymapResolver().resolve(context, token);
	if (runtimeKeymapDebugEnabled()) {
		std::snprintf(line, sizeof(line), "KEYDBG keymap context=%s rawCode=0x%04X rawMods=0x%04X token=%s result=%s sequence=%s target=%s", runtimeKeymapContextName(context), static_cast<unsigned>(event.keyDown.keyCode), static_cast<unsigned>(event.keyDown.controlKeyState), tokenText.c_str(), keymapResultName(result.kind), result.sequenceText.c_str(), result.target.target.c_str());
		mrLogMessage(line);
	}
	switch (result.kind) {
		case MRKeymapResolver::ResultKind::NoMatch:
			return false;
		case MRKeymapResolver::ResultKind::Pending:
		case MRKeymapResolver::ResultKind::Invalid:
		case MRKeymapResolver::ResultKind::Aborted:
			event.what = evNothing;
			return true;
		case MRKeymapResolver::ResultKind::Matched:
			event.what = evNothing;
			if (result.target.type == MRKeymapBindingType::Macro) {
				if (!dispatchMRKeymapMacro(result.target.target)) postWindowSupportError("Macro binding failed: " + result.target.target);
				return true;
			}
			if (!dispatchMRKeymapAction(result.target.target, result.sequenceText, targetWindow)) postWindowSupportError("Keymap action is not implemented: " + result.target.target);
			return true;
	}
	return false;
}

bool mrCaptureBindingKeySpec(const char *title, const char *prompt, std::string &keySpec) {
	TBindingKeyCaptureDialog *dialog = new TBindingKeyCaptureDialog(title, prompt);
	const ushort result = dialog != nullptr && TProgram::deskTop != nullptr ? TProgram::deskTop->execView(dialog) : cmCancel;
	const bool captured = dialog != nullptr ? dialog->hasCaptured() : false;
	const ushort keyCode = dialog != nullptr ? dialog->keyCode() : kbNoKey;
	const ushort controlState = dialog != nullptr ? dialog->controlState() : 0;

	keySpec.clear();
	if (dialog != nullptr) TObject::destroy(dialog);
	if (result == cmCancel || !captured) return true;
	if (!mrKeyTokenFromEvent(keyCode, controlState, keySpec)) {
		postWindowSupportWarning("Unsupported binding key. Use a function key or a Ctrl/Alt/Shift combination.");
		return false;
	}
	return true;
}

void mrSetKeystrokeRecordingActive(bool active) {
	g_keystrokeRecordingActive = active;
	if (!active) g_keystrokeRecordingMarkerVisible = false;
}

bool mrIsKeystrokeRecordingActive() {
	return g_keystrokeRecordingActive;
}

void mrSetKeystrokeRecordingMarkerVisible(bool visible) {
	g_keystrokeRecordingMarkerVisible = visible;
}

bool mrIsKeystrokeRecordingMarkerVisible() {
	return g_keystrokeRecordingMarkerVisible;
}

void mrSetMacroBrainMarkerActive(bool active) {
	g_macroBrainMarkerActive = active;
	if (!active) g_macroBrainMarkerVisible = false;
}

bool mrIsMacroBrainMarkerActive() {
	return g_macroBrainMarkerActive;
}

void mrSetMacroBrainMarkerVisible(bool visible) {
	g_macroBrainMarkerVisible = visible;
}

bool mrIsMacroBrainMarkerVisible() {
	return g_macroBrainMarkerVisible;
}
