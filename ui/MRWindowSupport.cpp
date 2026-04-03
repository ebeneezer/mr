#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TEvent
#define Uses_MsgBox
#define Uses_TProgram
#include <tvision/tv.h>

#include "MRWindowSupport.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>
#include <vector>

#include "../config/MRDialogPaths.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "TMREditWindow.hpp"

namespace {
const char *kHelpWindowTitle = "MR HELP";
const char *kLogWindowTitle = "MR LOG";

std::string g_logBuffer;
bool g_keystrokeRecordingActive = false;
bool g_keystrokeRecordingMarkerVisible = false;
std::string baseNameOf(const std::string &path);

std::string currentWorkingDirectory() {
	char cwd[1024];
	if (::getcwd(cwd, sizeof(cwd)) == nullptr)
		return std::string();
	return std::string(cwd);
}

std::string executableDirectory() {
	char path[4096];
	ssize_t len = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
	std::size_t pos;

	if (len <= 0)
		return std::string();
	path[len] = '\0';
	pos = std::string(path).find_last_of('/');
	if (pos == std::string::npos)
		return std::string();
	return std::string(path, static_cast<std::size_t>(pos));
}

std::string resolveHelpFilePath() {
	std::string configured = configuredHelpFilePath();
	std::string fromCwd = currentWorkingDirectory();
	std::string fromExe = executableDirectory();
	std::string candidate;
	std::string configuredName = baseNameOf(configured);

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

std::string currentTimestamp() {
	char buffer[32];
	std::time_t now = std::time(nullptr);
	std::tm *tmNow = std::localtime(&now);

	if (tmNow == nullptr)
		return std::string("--:--:--");
	if (std::strftime(buffer, sizeof(buffer), "%H:%M:%S", tmNow) == 0)
		return std::string("--:--:--");
	return std::string(buffer);
}

std::string normalizeLogLine(const char *message) {
	std::string line = message != nullptr ? message : "";
	while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
		line.pop_back();
	return line;
}

std::string baseNameOf(const std::string &path) {
	std::size_t pos = path.find_last_of("\\/");
	if (pos == std::string::npos)
		return path;
	return path.substr(pos + 1);
}

TMREditWindow *findWindowByTitle(const char *title) {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	for (std::size_t i = 0; i < windows.size(); ++i) {
		const char *windowTitle = windows[i]->getTitle(0);
		if (title != nullptr && windowTitle != nullptr && std::strcmp(windowTitle, title) == 0)
			return windows[i];
	}
	return nullptr;
}

bool isReservedUtilityWindow(TMREditWindow *win) {
	const char *title = win != nullptr ? win->getTitle(0) : nullptr;
	return title != nullptr &&
	       (std::strcmp(title, kHelpWindowTitle) == 0 || std::strcmp(title, kLogWindowTitle) == 0);
}

TMREditWindow *chooseFallbackWorkWindow() {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	TMREditWindow *hiddenEditable = nullptr;
	TMREditWindow *hiddenNonUtility = nullptr;

	for (std::size_t i = 0; i < windows.size(); ++i) {
		if ((windows[i]->state & sfVisible) != 0)
			return windows[i];
		if (hiddenEditable == nullptr && !windows[i]->isReadOnly())
			hiddenEditable = windows[i];
		if (hiddenNonUtility == nullptr && !isReservedUtilityWindow(windows[i]))
			hiddenNonUtility = windows[i];
	}
	if (hiddenEditable != nullptr)
		return hiddenEditable;
	if (hiddenNonUtility != nullptr)
		return hiddenNonUtility;
	return nullptr;
}

TMREditWindow *createReadOnlyTextWindow(const char *title, const char *text, bool hidden) {
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
		mrActivateEditWindow(previous);
	return win;
}

TMREditWindow *ensureLogWindowInternal(bool activate) {
	TMREditWindow *win = findWindowByTitle(kLogWindowTitle);

	if (g_logBuffer.empty())
		g_logBuffer = "MR/MEMAC log initialized.\n";
	if (win == nullptr)
		win = createReadOnlyTextWindow(kLogWindowTitle, g_logBuffer.c_str(), !activate);
	if (win == nullptr)
		return nullptr;
	win->replaceTextBuffer(g_logBuffer.c_str(), kLogWindowTitle);
	win->setWindowRole(TMREditWindow::wrLog);
	win->setReadOnly(true);
	win->setFileChanged(false);
	if (activate)
		mrActivateEditWindow(win);
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
	std::string helpPath = resolveHelpFilePath();

	if (TProgram::deskTop == nullptr)
		return false;

	win = dynamic_cast<TMREditWindow *>(TProgram::deskTop->current);
	if (win != nullptr) {
		std::string currentFile = win->currentFileName();
		const char *title = win->getTitle(0);
		if ((!currentFile.empty() && baseNameOf(currentFile) == baseNameOf(helpPath)) ||
		    (title != nullptr && std::strcmp(title, kHelpWindowTitle) == 0))
			return true;
	}

	win = findWindowByTitle(kHelpWindowTitle);
	if (win == nullptr) {
		win = createEditorWindow(kHelpWindowTitle);
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
	mrActivateEditWindow(win);
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
	if (!win->replaceTextBuffer(g_logBuffer.c_str(), kLogWindowTitle))
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

void mrLogMessage(const char *message) {
	std::string line = normalizeLogLine(message);
	TMREditWindow *win;

	if (line.empty())
		return;
	if (!g_logBuffer.empty() && g_logBuffer[g_logBuffer.size() - 1] != '\n')
		g_logBuffer += '\n';
	g_logBuffer += "[" + currentTimestamp() + "] " + line + "\n";
	win = ensureLogWindowInternal(false);
	if (win != nullptr) {
		win->replaceTextBuffer(g_logBuffer.c_str(), kLogWindowTitle);
		win->setReadOnly(true);
		win->setFileChanged(false);
	}
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
