#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TEvent
#define Uses_MsgBox
#define Uses_TProgram
#include <tvision/tv.h>

#include "MRWindowSupport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "../config/MRDialogPaths.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "TMREditWindow.hpp"

namespace {
constexpr std::string_view kHelpWindowTitle = "MR HELP";
constexpr std::string_view kLogWindowTitle = "MR LOG";

std::string g_logBuffer;
bool g_keystrokeRecordingActive = false;
bool g_keystrokeRecordingMarkerVisible = false;
[[nodiscard]] std::string baseNameOf(std::string_view path);

[[nodiscard]] std::string currentWorkingDirectory() {
	std::array<char, 1024> cwd{};
	if (::getcwd(cwd.data(), cwd.size()) == nullptr)
		return std::string();
	return std::string(cwd.data());
}

[[nodiscard]] std::string executableDirectory() {
	std::array<char, 4096> path{};
	const ssize_t len = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
	std::size_t pos;

	if (len <= 0)
		return std::string();
	path[static_cast<std::size_t>(len)] = '\0';
	pos = std::string_view(path.data()).find_last_of('/');
	if (pos == std::string_view::npos)
		return std::string();
	return std::string(path.data(), pos);
}

[[nodiscard]] std::string resolveHelpFilePath() {
	const std::string configured = configuredHelpFilePath();
	const std::string fromCwd = currentWorkingDirectory();
	const std::string fromExe = executableDirectory();
	std::string candidate;
	const std::string configuredName = baseNameOf(configured);

	if (!configured.empty() && ::access(configured.c_str(), R_OK) == 0)
		return configured;
	if (!fromCwd.empty()) {
		candidate = fromCwd + "/" + configuredName;
		if (::access(candidate.c_str(), R_OK) == 0)
			return candidate;
	}
	if (!fromExe.empty()) {
		candidate = fromExe + "/" + configuredName;
		if (::access(candidate.c_str(), R_OK) == 0)
			return candidate;
	}
	return configured;
}

[[nodiscard]] std::string currentTimestamp() {
	std::array<char, 32> buffer{};
	const std::time_t now = std::time(nullptr);
	const std::tm *tmNow = std::localtime(&now);

	if (tmNow == nullptr)
		return std::string("--:--:--");
	if (std::strftime(buffer.data(), buffer.size(), "%H:%M:%S", tmNow) == 0)
		return std::string("--:--:--");
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
	if (pos == std::string_view::npos)
		return std::string(path);
	return std::string(path.substr(pos + 1));
}

[[nodiscard]] TMREditWindow *findWindowByTitle(std::string_view title) {
	const std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	for (TMREditWindow *window : windows) {
		const char *windowTitle = window != nullptr ? window->getTitle(0) : nullptr;
		if (windowTitle != nullptr && title == windowTitle)
			return window;
	}
	return nullptr;
}

[[nodiscard]] bool isReservedUtilityWindow(TMREditWindow *win) {
	const char *title = win != nullptr ? win->getTitle(0) : nullptr;
	return title != nullptr && (kHelpWindowTitle == title || kLogWindowTitle == title);
}

[[nodiscard]] TMREditWindow *chooseFallbackWorkWindow() {
	const std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	TMREditWindow *hiddenEditable = nullptr;
	TMREditWindow *hiddenNonUtility = nullptr;

	for (TMREditWindow *window : windows) {
		if ((window->state & sfVisible) != 0)
			return window;
		if (hiddenEditable == nullptr && !window->isReadOnly())
			hiddenEditable = window;
		if (hiddenNonUtility == nullptr && !isReservedUtilityWindow(window))
			hiddenNonUtility = window;
	}
	if (hiddenEditable != nullptr)
		return hiddenEditable;
	if (hiddenNonUtility != nullptr)
		return hiddenNonUtility;
	return nullptr;
}

[[nodiscard]] TMREditWindow *createReadOnlyTextWindow(const char *title, const char *text, bool hidden) {
	TMREditWindow *previous;
	TMREditWindow *win;

	if (TProgram::deskTop == nullptr)
		return nullptr;

	previous = dynamic_cast<TMREditWindow *>(TProgram::deskTop->current);
	win = createEditorWindow(title);
	if (win == nullptr)
		return nullptr;
	if (!win->loadTextBuffer(text, title)) {
		message(win, evCommand, cmClose, nullptr);
		return nullptr;
	}
	win->setReadOnly(true);
	win->setFileChanged(false);
	if (hidden)
		win->hide();
	if (hidden && previous != nullptr && previous != win)
		static_cast<void>(mrActivateEditWindow(previous));
	return win;
}

[[nodiscard]] TMREditWindow *ensureLogWindowInternal(bool activate) {
	TMREditWindow *win = findWindowByTitle(kLogWindowTitle);

	if (g_logBuffer.empty())
		g_logBuffer = "MR/MEMAC log initialized.\n";
	if (win == nullptr)
		win = createReadOnlyTextWindow(kLogWindowTitle.data(), g_logBuffer.c_str(), !activate);
	if (win == nullptr)
		return nullptr;
	win->replaceTextBuffer(g_logBuffer.c_str(), kLogWindowTitle.data());
	win->setWindowRole(TMREditWindow::wrLog);
	win->setReadOnly(true);
	win->setFileChanged(false);
	if (activate)
		static_cast<void>(mrActivateEditWindow(win));
	return win;
}
} // namespace

bool mrActivateEditWindow(TMREditWindow *win) {
	if (win == nullptr)
		return false;
	if ((win->state & sfVisible) == 0)
		win->show();
	win->select();
	return true;
}

bool mrShowProjectHelp() {
	TMREditWindow *win;
	const std::string helpPath = resolveHelpFilePath();

	if (TProgram::deskTop == nullptr)
		return false;

	win = dynamic_cast<TMREditWindow *>(TProgram::deskTop->current);
	if (win != nullptr) {
		const std::string currentFile = win->currentFileName();
		const char *title = win->getTitle(0);
		if ((!currentFile.empty() && baseNameOf(currentFile) == baseNameOf(helpPath)) ||
		    (title != nullptr && kHelpWindowTitle == title))
			return true;
	}

	win = findWindowByTitle(kHelpWindowTitle);
	if (win == nullptr) {
		win = createEditorWindow(kHelpWindowTitle.data());
		if (win == nullptr)
			return false;

		if (!win->loadFromFile(helpPath.c_str())) {
			messageBox(mfError | mfOKButton, "Unable to load help file:\n%s", helpPath.c_str());
			message(win, evCommand, cmClose, nullptr);
			return false;
		}

		win->setReadOnly(true);
		win->setFileChanged(false);
	}
	win->setWindowRole(TMREditWindow::wrHelp, helpPath);
	static_cast<void>(mrActivateEditWindow(win));
	return true;
}

bool mrEnsureLogWindow(bool activate) {
	return ensureLogWindowInternal(activate) != nullptr;
}

bool mrClearLogWindow() {
	TMREditWindow *win;

	g_logBuffer = "MR/MEMAC log initialized.\n";
	win = ensureLogWindowInternal(false);
	if (win == nullptr)
		return false;
	if (!win->replaceTextBuffer(g_logBuffer.c_str(), kLogWindowTitle.data()))
		return false;
	win->setWindowRole(TMREditWindow::wrLog);
	win->setReadOnly(true);
	win->setFileChanged(false);
	return true;
}

bool mrEnsureUsableWorkWindow() {
	TMREditWindow *current =
	    dynamic_cast<TMREditWindow *>(TProgram::deskTop != nullptr ? TProgram::deskTop->current : nullptr);
	TMREditWindow *fallback;

	if (TProgram::deskTop == nullptr)
		return false;
	if (current != nullptr && (current->state & sfVisible) != 0)
		return true;
	fallback = chooseFallbackWorkWindow();
	if (fallback != nullptr)
		return mrActivateEditWindow(fallback);
	fallback = createEditorWindow("?No-File?");
	if (fallback == nullptr)
		return false;
	mrLogMessage("Created fallback empty window to keep the editor usable.");
	return mrActivateEditWindow(fallback);
}

void mrLogMessage(std::string_view message) {
	const std::string line = normalizeLogLine(message);
	TMREditWindow *win;

	if (line.empty())
		return;
	if (!g_logBuffer.empty() && g_logBuffer[g_logBuffer.size() - 1] != '\n')
		g_logBuffer += '\n';
	g_logBuffer += "[" + currentTimestamp() + "] " + line + "\n";
	win = ensureLogWindowInternal(false);
	if (win != nullptr) {
		win->replaceTextBuffer(g_logBuffer.c_str(), kLogWindowTitle.data());
		win->setReadOnly(true);
		win->setFileChanged(false);
	}
}

void mrLogSettingsWriteReport(std::string_view reason, const MRSettingsWriteReport &report) {
	if (!report.contentChanged && report.logLines.empty())
		return;
	if (!reason.empty())
		mrLogMessage(std::string("settings.mrmac write (") + std::string(reason) + "): " + report.settingsPath);
	for (const std::string &line : report.logLines)
		mrLogMessage(line);
}

void mrSetKeystrokeRecordingActive(bool active) {
	g_keystrokeRecordingActive = active;
	if (!active)
		g_keystrokeRecordingMarkerVisible = false;
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
