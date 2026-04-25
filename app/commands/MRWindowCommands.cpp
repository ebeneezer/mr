#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TFileDialog
#define Uses_MsgBox
#define Uses_TObject
#include <tvision/tv.h>

#include "MRFileCommands.hpp"
#include "MRWindowCommands.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "MRDialogPaths.hpp"
#include "MRPerformance.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "../../dialogs/MRWindowListDialog.hpp"
#include "../../dialogs/MRSetupDialogCommon.hpp"

namespace {
void postWindowCommandError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, text,
	                               mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

void collectEditWindowsInZOrder(TView *view, void *arg) {
	std::vector<MREditWindow *> *windows = static_cast<std::vector<MREditWindow *> *>(arg);
	MREditWindow *win = dynamic_cast<MREditWindow *>(view);

	if (windows != nullptr && win != nullptr)
		windows->push_back(win);
}
} // namespace

std::vector<MREditWindow *> allEditWindowsInZOrder() {
	std::vector<MREditWindow *> windows;

	if (TProgram::deskTop == nullptr)
		return windows;

	TProgram::deskTop->forEach(collectEditWindowsInZOrder, &windows);
	return windows;
}

namespace {
short nextEditorWindowNumber() {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	std::set<short> used;
	short candidate = 1;

	for (auto & window : windows) {
		if (window != nullptr && window->number > 0)
			used.insert(window->number);
	}

	while (used.find(candidate) != used.end()) {
		if (candidate == std::numeric_limits<short>::max())
			return candidate;
		++candidate;
	}
	return candidate;
}
} // namespace

#include "../utils/MRFileIOUtils.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../config/MRSettingsLoader.hpp"
#include "../ui/MRMessageLineController.hpp"

static int g_currentVirtualDesktop = 1;
static std::set<const MREditWindow *> g_manuallyHiddenWindows;

namespace {
struct WorkspaceEntry {
	std::string url;
	int width = -1;
	int height = -1;
	int x = -1;
	int y = -1;
	int column = 1;
	int line = 1;
	int vd = 1;
};

std::string escapeMrmacSingleQuotedLiteral(std::string_view value) {
	std::string escaped;

	escaped.reserve(value.size());
	for (char ch : value) {
		if (ch == '\'')
			escaped += "''";
		else
			escaped.push_back(ch);
	}
	return escaped;
}

std::string unescapeMrmacSingleQuotedLiteral(std::string_view value) {
	std::string unescaped;

	unescaped.reserve(value.size());
	for (std::size_t i = 0; i < value.size(); ++i) {
		char ch = value[i];
		if (ch == '\'' && i + 1 < value.size() && value[i + 1] == '\'') {
			unescaped.push_back('\'');
			++i;
		}
		else
			unescaped.push_back(ch);
	}
	return unescaped;
}

std::string workspaceDisplayName(const std::string &path) {
	std::size_t pos = path.find_last_of("\\/");
	return pos == std::string::npos ? path : path.substr(pos + 1);
}

void pruneManuallyHiddenWindows(const std::vector<MREditWindow *> &windows) {
	std::set<const MREditWindow *> active;

	for (MREditWindow *win : windows)
		if (win != nullptr)
			active.insert(win);

	for (auto it = g_manuallyHiddenWindows.begin(); it != g_manuallyHiddenWindows.end();) {
		if (active.find(*it) == active.end())
			it = g_manuallyHiddenWindows.erase(it);
		else
			++it;
	}
}

bool parseWorkspaceEntry(const std::string &line, WorkspaceEntry &entry) {
	static const std::regex linePattern(
	    R"(MRSETUP\s*\(\s*'WORKSPACE'\s*,\s*'((?:''|[^'])*)'\s*\)\s*;?)",
	    std::regex_constants::ECMAScript | std::regex_constants::icase);
	static const std::regex payloadPattern(
	    R"(^URL=(.*) size=(-?\d+),(-?\d+) pos=(-?\d+),(-?\d+) cursor=(-?\d+),(-?\d+) vd=(-?\d+)$)",
	    std::regex_constants::ECMAScript);
	std::smatch match;
	std::smatch payloadMatch;
	std::string payload;

	if (!std::regex_search(line, match, linePattern))
		return false;
	payload = unescapeMrmacSingleQuotedLiteral(match[1].str());
	if (!std::regex_match(payload, payloadMatch, payloadPattern))
		return false;

	entry.url = payloadMatch[1].str();
	entry.width = std::stoi(payloadMatch[2].str());
	entry.height = std::stoi(payloadMatch[3].str());
	entry.x = std::stoi(payloadMatch[4].str());
	entry.y = std::stoi(payloadMatch[5].str());
	entry.column = std::stoi(payloadMatch[6].str());
	entry.line = std::stoi(payloadMatch[7].str());
	entry.vd = std::stoi(payloadMatch[8].str());
	return !entry.url.empty();
}

void restoreEditorCursor(MRFileEditor *editor, int line, int column) {
	std::size_t target = 0;

	if (editor == nullptr)
		return;
	line = std::max(1, line);
	column = std::max(1, column);

	for (int currentLine = 1; currentLine < line && target < editor->bufferLength(); ++currentLine) {
		std::size_t next = editor->nextLineOffset(target);
		if (next <= target)
			break;
		target = next;
	}
	target = editor->charPtrOffset(target, column - 1);
	editor->setCursorOffset(target);
	editor->revealCursor(True);
}

} // namespace

int currentVirtualDesktop() {
	return g_currentVirtualDesktop;
}

void setWindowManuallyHidden(MREditWindow *win, bool hidden) {
	if (win == nullptr)
		return;
	if (hidden == isWindowManuallyHidden(win))
		return;
	if (hidden)
		g_manuallyHiddenWindows.insert(win);
	else
		g_manuallyHiddenWindows.erase(win);
	mrNotifyWindowTopologyChanged();
}

bool isWindowManuallyHidden(const MREditWindow *win) {
	if (win == nullptr)
		return false;
	return g_manuallyHiddenWindows.find(win) != g_manuallyHiddenWindows.end();
}

namespace {
void postDesktopChangedMessage(int desktop) {
	mr::messageline::postAutoTimed(
	    mr::messageline::Owner::DialogInteraction, "Desktop #" + std::to_string(desktop),
	    mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
}
} // namespace

void syncVirtualDesktopVisibility() {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	MREditWindow *candidate = nullptr;
	MREditWindow *current =
	    TProgram::deskTop != nullptr ? dynamic_cast<MREditWindow *>(TProgram::deskTop->current) : nullptr;

	pruneManuallyHiddenWindows(windows);

	for (MREditWindow *win : windows) {
		const bool manuallyHidden = isWindowManuallyHidden(win);

		if (win == nullptr)
			continue;
		if (win->mVirtualDesktop == g_currentVirtualDesktop) {
			if (candidate == nullptr && !manuallyHidden)
				candidate = win;
			if (!manuallyHidden && (win->state & sfVisible) == 0)
				win->show();
			else if (manuallyHidden && (win->state & sfVisible) != 0)
				win->hide();
		}
		else if ((win->state & sfVisible) != 0)
				win->hide();
	}

	if (candidate != nullptr &&
	    (current == nullptr || current->mVirtualDesktop != g_currentVirtualDesktop ||
	     (current->state & sfVisible) == 0))
		candidate->select();

	if (TProgram::deskTop != nullptr) {
		TProgram::deskTop->redraw();
		TProgram::deskTop->drawView();
	}
	if (TProgram::application != nullptr)
		TProgram::application->redraw();
}

void setCurrentVirtualDesktop(int vd) {
	const int oldDesktop = g_currentVirtualDesktop;

	if (vd < 1) vd = 1;
	int maxVd = configuredVirtualDesktops();
	if (maxVd < 1) maxVd = 1;
	if (vd > maxVd) vd = maxVd;
	g_currentVirtualDesktop = vd;
	syncVirtualDesktopVisibility();
	if (g_currentVirtualDesktop != oldDesktop)
		postDesktopChangedMessage(vd);
}

void applyVirtualDesktopConfigurationChange(int count) {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	std::string ignoredError;

	if (count < 1)
		count = 1;
	if (count > 9)
		count = 9;
	for (MREditWindow *win : windows)
		if (win != nullptr && win->mVirtualDesktop > count)
			win->mVirtualDesktop = count;

	setConfiguredVirtualDesktops(count, &ignoredError);
	setCurrentVirtualDesktop(std::min(currentVirtualDesktop(), count));
}

bool moveToNextVirtualDesktop() {
	MREditWindow *win = currentEditWindow();
	if (win == nullptr) return false;
	int maxVd = configuredVirtualDesktops();
	if (win->mVirtualDesktop >= maxVd) return false;
	win->mVirtualDesktop++;
	syncVirtualDesktopVisibility();
	return true;
}

bool moveToPrevVirtualDesktop() {
	MREditWindow *win = currentEditWindow();
	if (win == nullptr) return false;
	if (win->mVirtualDesktop <= 1) return false;
	win->mVirtualDesktop--;
	syncVirtualDesktopVisibility();
	return true;
}

bool viewportRight() {
	int maxVd = configuredVirtualDesktops();
	if (g_currentVirtualDesktop >= maxVd) {
		if (configuredCyclicVirtualDesktops() && maxVd > 1) {
			setCurrentVirtualDesktop(1);
			return true;
		}
		return false;
	}
	setCurrentVirtualDesktop(g_currentVirtualDesktop + 1);
	return true;
}

bool viewportLeft() {
	int maxVd = configuredVirtualDesktops();
	if (g_currentVirtualDesktop <= 1) {
		if (configuredCyclicVirtualDesktops() && maxVd > 1) {
			setCurrentVirtualDesktop(maxVd);
			return true;
		}
		return false;
	}
	setCurrentVirtualDesktop(g_currentVirtualDesktop - 1);
	return true;
}

void mrSaveWorkspace(const std::string &filename) {
	std::string settingsPath = filename;
	if (settingsPath.empty()) {
		settingsPath = configuredSettingsMacroFilePath();
	}
	std::string dest = settingsPath;
	if (dest.find(".mrmac") == std::string::npos) {
		dest += ".mrmac";
	}

	std::string currentContent;
	std::string errorText;
	readTextFile(dest, currentContent, errorText);

	std::vector<std::string> lines;
	std::istringstream iss(currentContent);
	std::string line;
	while (std::getline(iss, line)) {
		if (line.find("MRSETUP('WORKSPACE'") == std::string::npos) {
			lines.push_back(line);
		}
	}

	for (MREditWindow *win : allEditWindowsInZOrder()) {
		MRFileEditor *editor = win->getEditor();
		if (editor == nullptr || win == nullptr) continue;
		std::string url = editor->persistentFileName();
		if (url.empty()) continue;
		TRect r = win->getBounds();
		int cursorColumn = static_cast<int>(win->cursorColumnNumber());
		int cursorLine = static_cast<int>(win->cursorLineNumber());
		int vd = win->mVirtualDesktop;

		std::string wsLine = "MRSETUP('WORKSPACE', 'URL=" + escapeMrmacSingleQuotedLiteral(url) +
		                     " size=" + std::to_string(r.b.x - r.a.x) + "," +
		                     std::to_string(r.b.y - r.a.y) + " pos=" + std::to_string(r.a.x) + "," +
		                     std::to_string(r.a.y) + " cursor=" + std::to_string(cursorColumn) + "," +
		                     std::to_string(cursorLine) + " vd=" + std::to_string(vd) + "');";
		lines.push_back(wsLine);
	}

	std::string newContent;
	for (const std::string &l : lines) {
		newContent += l + "\n";
	}
	writeTextFile(dest, newContent);
}

void mrLoadWorkspace(const std::string &filename) {
	std::string settingsPath = filename;
	if (settingsPath.empty()) {
		settingsPath = configuredSettingsMacroFilePath();
	}
	std::string dest = settingsPath;
	if (dest.find(".mrmac") == std::string::npos) {
		dest += ".mrmac";
	}

	std::string currentContent;
	std::string errorText;
	if (!readTextFile(dest, currentContent, errorText)) {
		mr::messageline::postAutoTimed(
		    mr::messageline::Owner::HeroEvent,
		    "Unable to read workspace: " + workspaceDisplayName(dest), mr::messageline::Kind::Error,
		    mr::messageline::kPriorityHigh);
		return;
	}

	std::istringstream iss(currentContent);
	std::string line;
	std::vector<std::string> failedFiles;
	while (std::getline(iss, line)) {
		WorkspaceEntry entry;
		MREditWindow *win = nullptr;
		MRFileEditor *editor = nullptr;
		std::string err;

		if (!parseWorkspaceEntry(line, entry))
			continue;

		win = createEditorWindow(entry.url.c_str());
		editor = win != nullptr ? win->getEditor() : nullptr;
		if (win == nullptr || editor == nullptr || !editor->loadMappedFile(entry.url.c_str(), err)) {
			if (win != nullptr)
				message(win, evCommand, cmClose, nullptr);
			failedFiles.push_back(workspaceDisplayName(entry.url));
			continue;
		}

		if (entry.width > 0 && entry.height > 0 && entry.x >= 0 && entry.y >= 0)
			win->changeBounds(TRect(entry.x, entry.y, entry.x + entry.width, entry.y + entry.height));
		win->mVirtualDesktop = std::min(std::max(entry.vd, 1), configuredVirtualDesktops());
		restoreEditorCursor(editor, entry.line, entry.column);
	}
	if (!failedFiles.empty()) {
		std::string failedText;

		for (std::size_t i = 0; i < failedFiles.size(); ++i) {
			if (i != 0)
				failedText += ", ";
			failedText += failedFiles[i];
		}
		mr::messageline::postAutoTimed(
		    mr::messageline::Owner::HeroEvent,
		    "Failed to load workspace files; discarded: " + failedText, mr::messageline::Kind::Error,
		    mr::messageline::kPriorityHigh);
	}
	syncVirtualDesktopVisibility();
}

MREditWindow *createEditorWindow(const char *title) {
	TRect bounds;
	MREditWindow *win;

	if (TProgram::deskTop == nullptr)
		return nullptr;
	bounds = TProgram::deskTop->getExtent();
	bounds.grow(-2, -1);
	win = new MREditWindow(bounds, title, nextEditorWindowNumber());
	TProgram::deskTop->insert(win);
	if (win != nullptr) {
		win->mVirtualDesktop = currentVirtualDesktop();
		win->flags |= (wfMove | wfGrow | wfZoom | wfClose);
	}
	if (win != nullptr && win->getEditor() != nullptr)
		win->getEditor()->setInsertModeEnabled(configuredDefaultInsertMode());
	mrNotifyWindowTopologyChanged();
	return win;
}

MREditWindow *currentEditWindow() {
	if (TProgram::deskTop == nullptr || TProgram::deskTop->current == nullptr)
		return nullptr;
	return dynamic_cast<MREditWindow *>(TProgram::deskTop->current);
}

MREditWindow *findEditWindowByBufferId(int bufferId) {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	for (auto & window : windows)
		if (window != nullptr && window->bufferId() == bufferId)
			return window;
	return nullptr;
}

bool isEmptyUntitledEditableWindow(MREditWindow *win) {
	if (win == nullptr || win->isReadOnly() || win->currentFileName()[0] != '\0' || win->isFileChanged())
		return false;
	return win->isBufferEmpty();
}

MREditWindow *findReusableEmptyWindow(MREditWindow *preferred) {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	if (preferred != nullptr && isEmptyUntitledEditableWindow(preferred))
		return preferred;
	for (auto & window : windows)
		if (isEmptyUntitledEditableWindow(window))
			return window;
	return nullptr;
}

bool closeCurrentEditWindow() {
	MREditWindow *win = currentEditWindow();
	if (win == nullptr)
		return false;
	setWindowManuallyHidden(win, false);
	message(win, evCommand, cmClose, nullptr);
	return mrEnsureUsableWorkWindow();
}

bool activateRelativeEditWindow(int delta) {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	MREditWindow *current = currentEditWindow();
	std::size_t index;

	if (windows.empty())
		return false;
	if (current == nullptr)
		return mrActivateEditWindow(windows.front());

	for (index = 0; index < windows.size(); ++index) {
		if (windows[index] == current) {
			int nextIndex = static_cast<int>(index) + delta;
			int count = static_cast<int>(windows.size());

			while (nextIndex < 0)
				nextIndex += count;
			nextIndex %= count;
			return mrActivateEditWindow(windows[static_cast<std::size_t>(nextIndex)]);
		}
	}
	return mrActivateEditWindow(windows.front());
}

bool hideCurrentEditWindow() {
	MREditWindow *win = currentEditWindow();
	if (win == nullptr)
		return false;
	setWindowManuallyHidden(win, true);
	win->hide();
	return mrEnsureUsableWorkWindow();
}

void mrUpdateAllWindowsColorTheme() {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	for (auto & window : windows) {
		if (window != nullptr) {
			window->applyWindowColorThemeForPath(window->currentFileName());
		}
	}
}


// ---- Consolidated from MRFileCommands.cpp ----

namespace {
ushort execDialogWithPayload(TDialog *dialog, void *data) {
	ushort result = mr::dialogs::execDialogWithData(dialog, data);
	if (result == cmHelp)
		static_cast<void>(mrShowProjectHelp());
	return result;
}

[[nodiscard]] std::string normalizeTvPath(std::string_view path) {
	std::string result(path);

	for (char &ch : result)
		if (ch == '\\')
			ch = '/';
#ifdef __unix__
	if (result.size() >= 2 && ((result[0] >= 'A' && result[0] <= 'Z') ||
	                           (result[0] >= 'a' && result[0] <= 'z')) &&
	    result[1] == ':')
		result.erase(0, 2);
#endif
	return result;
}

[[nodiscard]] std::string trimPathInput(std::string_view path) {
	std::size_t start = 0;
	std::size_t end = path.size();

	while (start < end && std::isspace(static_cast<unsigned char>(path[start])) != 0)
		++start;
	while (end > start &&
	       (std::isspace(static_cast<unsigned char>(path[end - 1])) != 0 ||
	        static_cast<unsigned char>(path[end - 1]) < 32))
		--end;

	std::string result(path.substr(start, end - start));
	if (result.size() >= 2 &&
	    ((result.front() == '"' && result.back() == '"') || (result.front() == '\'' && result.back() == '\'')))
		result = result.substr(1, result.size() - 2);
	return result;
}

[[nodiscard]] std::string expandUserPath(std::string_view path) {
	std::string result;

	if (path.empty())
		return std::string();
	result = normalizeTvPath(trimPathInput(path));
	if (result.size() >= 2 && result[0] == '~' && result[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != nullptr && *home != '\0')
			return std::string(home) + result.substr(1);
	}
	return result;
}

[[nodiscard]] bool hasWildcardPattern(std::string_view path) {
	return path.find('*') != std::string_view::npos || path.find('?') != std::string_view::npos;
}

[[nodiscard]] std::size_t lastPathSeparator(std::string_view path) {
	const std::size_t slash = path.find_last_of('/');
	const std::size_t backslash = path.find_last_of('\\');

	if (slash == std::string_view::npos)
		return backslash;
	if (backslash == std::string_view::npos)
		return slash;
	return std::max(slash, backslash);
}

[[nodiscard]] std::string baseNameForDisplay(const std::string &path) {
	const std::size_t sep = lastPathSeparator(path);

	if (sep == std::string::npos || sep + 1 >= path.size())
		return path;
	return path.substr(sep + 1);
}

[[nodiscard]] long long roundedMilliseconds(double valueMs) {
	if (valueMs <= 0.0)
		return 0;
	return static_cast<long long>(valueMs + 0.5);
}

void postLoadHeroEvents(const std::string &resolvedPath, std::size_t bytes, double loadMs, std::size_t lineCount,
                        double lineCountMs) {
	const std::string fileName = baseNameForDisplay(resolvedPath);
	const std::string loadText =
	    "loaded " + fileName + " in " + (roundedMilliseconds(loadMs) >= 1 ? std::to_string(roundedMilliseconds(loadMs)) : "<1") + " ms";
	const std::string lineText = "indexed " + std::to_string(bytes) + " bytes, " +
	                             std::to_string(lineCount) + " lines, " +
	                             std::to_string(roundedMilliseconds(lineCountMs)) + " ms";
	const std::chrono::milliseconds loadDuration = mr::messageline::autoDurationForText(loadText);

	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEvent, loadText, mr::messageline::Kind::Success,
	                              mr::messageline::kPriorityHigh);
	mr::messageline::postAutoTimedAfter(mr::messageline::Owner::HeroEventFollowup, lineText,
	                                   mr::messageline::Kind::Info, loadDuration,
	                                   mr::messageline::kPriorityLow);
}

[[nodiscard]] bool hasExtensionInBaseName(std::string_view path) {
	const std::size_t sep = lastPathSeparator(path);
	const std::size_t dot = path.find_last_of('.');

	return dot != std::string_view::npos && (sep == std::string_view::npos || dot > sep);
}

[[nodiscard]] bool resolveWithConfiguredExtensions(const std::string &basePath, std::string &resolvedPath) {
	const std::vector<std::string> extensions = configuredDefaultExtensionList();
	std::set<std::string> tried;

	for (const std::string &ext : extensions) {
		std::array<std::string, 3> candidates = {ext, ext, ext};

		if (ext.empty())
			continue;
		for (std::size_t p = 0; p < ext.size(); ++p) {
			candidates[1][p] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[p])));
			candidates[2][p] = static_cast<char>(std::toupper(static_cast<unsigned char>(ext[p])));
		}

		for (const std::string &candidateExt : candidates) {
			std::string candidate = basePath + "." + candidateExt;
			if (!tried.insert(candidate).second)
				continue;
			if (::access(candidate.c_str(), F_OK) == 0 && ::access(candidate.c_str(), R_OK) == 0) {
				resolvedPath = candidate;
				return true;
			}
		}
	}
	return false;
}
} // namespace

bool promptForPath(const char *title, char *fileName, std::size_t fileNameSize) {
	if (fileName == nullptr || fileNameSize == 0)
		return false;
	initRememberedLoadDialogPath(fileName, fileNameSize, "*.*");
	return execDialogWithPayload(new TFileDialog("*.*", title, "~N~ame", fdOpenButton, kFileDialogHistoryId), fileName) !=
	       cmCancel;
}

bool promptForSaveAsPath(const char *title, const char *initialPath, std::string &outResolvedPath) {
	char fileName[MAXPATH] = {0};
	ushort result = cmCancel;

	outResolvedPath.clear();
	if (initialPath != nullptr && *initialPath != '\0')
		strnzcpy(fileName, initialPath, sizeof(fileName));
	else
		initRememberedLoadDialogPath(fileName, sizeof(fileName), "*.*");
	result = execDialogWithPayload(new TFileDialog("*.*", title, "~N~ame", fdOKButton, kFileDialogHistoryId),
	                               fileName);
	if (result == cmCancel)
		return false;
	outResolvedPath = expandUserPath(fileName);
	if (outResolvedPath.empty()) {
		postWindowCommandError("No file name specified.");
		return false;
	}
	if (hasWildcardPattern(outResolvedPath)) {
		postWindowCommandError("Wildcards are not allowed in save file names.");
		return false;
	}
	rememberLoadDialogPath(outResolvedPath.c_str());
	return true;
}

bool saveWindowSnapshotToPath(MREditWindow *win, const std::string &resolvedPath) {
	std::ofstream outFile;
	std::string text;
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

	if (win == nullptr || editor == nullptr || resolvedPath.empty())
		return false;
	text = editor->snapshotText();
	outFile.open(resolvedPath.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!outFile.is_open())
		return false;
	outFile.write(text.data(), static_cast<std::streamsize>(text.size()));
	outFile.close();
	return outFile.good();
}

bool resolveReadableExistingPath(const char *path, std::string &resolvedPath) {
	bool disableExtensionSearch = false;
	std::string rawInput = expandUserPath(path != nullptr ? std::string_view(path) : std::string_view());

	resolvedPath = rawInput;
	if (!resolvedPath.empty() && resolvedPath.back() == '.' && !hasWildcardPattern(resolvedPath)) {
		disableExtensionSearch = true;
		resolvedPath.pop_back();
	}
	if (resolvedPath.empty()) {
		postWindowCommandError("No file name specified.");
		return false;
	}
	if (::access(resolvedPath.c_str(), F_OK) != 0 && !disableExtensionSearch &&
	    !hasWildcardPattern(resolvedPath) && !hasExtensionInBaseName(resolvedPath))
		static_cast<void>(resolveWithConfiguredExtensions(resolvedPath, resolvedPath));
	if (access(resolvedPath.c_str(), F_OK) != 0) {
		postWindowCommandError("File does not exist: " + resolvedPath);
		return false;
	}
	if (access(resolvedPath.c_str(), R_OK) != 0) {
		postWindowCommandError("File is not readable: " + resolvedPath);
		return false;
	}
	rememberLoadDialogPath(resolvedPath.c_str());
	return true;
}

bool loadResolvedFileIntoWindow(MREditWindow *win, const std::string &resolvedPath, const char *operationLabel) {
	const auto fallbackLoadStartedAt = std::chrono::steady_clock::now();
	if (win == nullptr)
		return false;
	if (!win->loadFromFile(resolvedPath.c_str())) {
		postWindowCommandError("Unable to load file: " + resolvedPath);
		return false;
	}
	const MRFileEditor::LoadTiming timing = win->lastLoadTiming();
	std::size_t bytes = win->bufferLength();
	std::size_t lines = 0;
	double loadMs = 0.0;
	double lineCountMs = 0.0;

	if (timing.valid) {
		bytes = timing.bytes;
		lines = timing.lines;
		loadMs = timing.mappedLoadMs;
		lineCountMs = timing.lineCountMs;
	} else {
		loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
		                                                   fallbackLoadStartedAt)
		             .count();
		const auto lineCountStartedAt = std::chrono::steady_clock::now();
		lines = win->bufferLineCount();
		lineCountMs =
		    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lineCountStartedAt).count();
	}

	mr::performance::recordUiEvent(operationLabel != nullptr ? operationLabel : "Load file",
	                               static_cast<std::size_t>(win->bufferId()), win->documentId(), bytes, loadMs,
	                               resolvedPath);
	mr::performance::recordUiEvent("Line count", static_cast<std::size_t>(win->bufferId()), win->documentId(), bytes,
	                               lineCountMs, resolvedPath);
	postLoadHeroEvents(resolvedPath, bytes, loadMs, lines, lineCountMs);
	return true;
}

bool saveCurrentEditWindow() {
	MREditWindow *win = currentEditWindow();

	if (win == nullptr)
		return false;
	if (win->isReadOnly()) {
		messageBox(mfInformation | mfOKButton, "Window is read-only.");
		mrLogMessage("Save rejected for read-only window.");
		return false;
	}
	if (!win->isFileChanged())
		return true;
	if (win->canSaveInPlace()) {
		auto startedAt = std::chrono::steady_clock::now();
		if (!win->saveCurrentFile()) {
			mrLogMessage("Save failed.");
			return false;
		}
		mr::performance::recordUiEvent(
		    "Save file", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(),
		    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(),
		    win->currentFileName());
		mrLogMessage("Window saved.");
		return true;
	}
	if (!win->saveCurrentFileAs()) {
		mrLogMessage("Save failed.");
		return false;
	}
	mrLogMessage("Window saved as a new file.");
	return true;
}

bool saveCurrentEditWindowAs() {
	MREditWindow *win = currentEditWindow();
	std::string resolvedPath;
	bool isLogWindow = false;

	if (win == nullptr)
		return false;
	isLogWindow = win->windowRole() == MREditWindow::wrLog;
	if (win->isReadOnly()) {
		if (!isLogWindow) {
			messageBox(mfInformation | mfOKButton, "Window is read-only.");
			mrLogMessage("Save As rejected for read-only window.");
			return false;
		}
		if (!promptForSaveAsPath("Save log as", win->currentFileName(), resolvedPath))
			return false;
		auto startedAt = std::chrono::steady_clock::now();
		if (!saveWindowSnapshotToPath(win, resolvedPath)) {
			postWindowCommandError("Unable to save log file: " + resolvedPath);
			mrLogMessage("Save As failed.");
			return false;
		}
		mr::performance::recordUiEvent(
		    "Save log as", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(),
		    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(),
		    resolvedPath);
		mrLogMessage("Log window saved as a new file.");
		return true;
	}
	auto startedAt = std::chrono::steady_clock::now();
	if (!win->saveCurrentFileAs()) {
		mrLogMessage("Save As failed.");
		return false;
	}
	mr::performance::recordUiEvent(
	    "Save file as", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(),
	    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(),
	    win->currentFileName());
	mrLogMessage("Window saved as a new file.");
	return true;
}

bool handleWindowCascade() {
	std::vector<MREditWindow *> allWindows = allEditWindowsInZOrder();
	std::vector<MREditWindow *> visibleWindows;
	TRect desktopBounds;

	if (TProgram::deskTop == nullptr)
		return false;

	desktopBounds = TProgram::deskTop->getExtent();

	for (auto it = allWindows.rbegin(); it != allWindows.rend(); ++it) {
		MREditWindow *win = *it;
		if (win != nullptr && (win->state & sfVisible) != 0) {
			visibleWindows.push_back(win);
		}
	}

	if (visibleWindows.empty())
		return true;

	int cascadeIndex = 0;
	TProgram::deskTop->lock();
	for (MREditWindow *win : visibleWindows) {
		TRect bounds;
		bounds.a.x = desktopBounds.a.x + cascadeIndex;
		bounds.a.y = desktopBounds.a.y + cascadeIndex;
		bounds.b.x = desktopBounds.b.x;
		bounds.b.y = desktopBounds.b.y;
		win->locate(bounds);
		cascadeIndex++;
	}
	TProgram::deskTop->unlock();

	return true;
}

bool handleWindowTile() {
	std::vector<MREditWindow *> allWindows = allEditWindowsInZOrder();
	std::vector<MREditWindow *> visibleWindows;
	TRect desktopBounds;

	if (TProgram::deskTop == nullptr)
		return false;

	desktopBounds = TProgram::deskTop->getExtent();

	for (auto it = allWindows.rbegin(); it != allWindows.rend(); ++it) {
		MREditWindow *win = *it;
		if (win != nullptr && (win->state & sfVisible) != 0) {
			visibleWindows.push_back(win);
		}
	}

	int count = visibleWindows.size();

	if (count > 9) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEvent, "max 9 windows can be tiled",
		                               mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return true;
	}

	if (count == 0)
		return true;

	std::vector<TRect> rects(count);
	int width = desktopBounds.b.x - desktopBounds.a.x;
	int height = desktopBounds.b.y - desktopBounds.a.y;
	int halfWidth = width / 2;
	int halfHeight = height / 2;

	switch (count) {
		case 1:
			rects[0] = desktopBounds;
			break;
		case 2:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + halfWidth, desktopBounds.b.y);
			rects[1] = TRect(desktopBounds.a.x + halfWidth, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.b.y);
			break;
		case 3:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + halfWidth, desktopBounds.a.y + halfHeight);
			rects[1] = TRect(desktopBounds.a.x + halfWidth, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.a.y + halfHeight);
			rects[2] = TRect(desktopBounds.a.x, desktopBounds.a.y + halfHeight, desktopBounds.b.x, desktopBounds.b.y);
			break;
		case 4:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + halfWidth, desktopBounds.a.y + halfHeight);
			rects[1] = TRect(desktopBounds.a.x + halfWidth, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.a.y + halfHeight);
			rects[2] = TRect(desktopBounds.a.x, desktopBounds.a.y + halfHeight, desktopBounds.a.x + halfWidth, desktopBounds.b.y);
			rects[3] = TRect(desktopBounds.a.x + halfWidth, desktopBounds.a.y + halfHeight, desktopBounds.b.x, desktopBounds.b.y);
			break;
		case 5:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + width / 3, desktopBounds.a.y + halfHeight);
			rects[1] = TRect(desktopBounds.a.x + width / 3, desktopBounds.a.y, desktopBounds.a.x + 2 * width / 3, desktopBounds.a.y + halfHeight);
			rects[2] = TRect(desktopBounds.a.x + 2 * width / 3, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.a.y + halfHeight);
			rects[3] = TRect(desktopBounds.a.x, desktopBounds.a.y + halfHeight, desktopBounds.a.x + halfWidth, desktopBounds.b.y);
			rects[4] = TRect(desktopBounds.a.x + halfWidth, desktopBounds.a.y + halfHeight, desktopBounds.b.x, desktopBounds.b.y);
			break;
		case 6:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + width / 3, desktopBounds.a.y + halfHeight);
			rects[1] = TRect(desktopBounds.a.x + width / 3, desktopBounds.a.y, desktopBounds.a.x + 2 * width / 3, desktopBounds.a.y + halfHeight);
			rects[2] = TRect(desktopBounds.a.x + 2 * width / 3, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.a.y + halfHeight);
			rects[3] = TRect(desktopBounds.a.x, desktopBounds.a.y + halfHeight, desktopBounds.a.x + width / 3, desktopBounds.b.y);
			rects[4] = TRect(desktopBounds.a.x + width / 3, desktopBounds.a.y + halfHeight, desktopBounds.a.x + 2 * width / 3, desktopBounds.b.y);
			rects[5] = TRect(desktopBounds.a.x + 2 * width / 3, desktopBounds.a.y + halfHeight, desktopBounds.b.x, desktopBounds.b.y);
			break;
		case 7:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + width / 4, desktopBounds.a.y + halfHeight);
			rects[1] = TRect(desktopBounds.a.x + width / 4, desktopBounds.a.y, desktopBounds.a.x + 2 * width / 4, desktopBounds.a.y + halfHeight);
			rects[2] = TRect(desktopBounds.a.x + 2 * width / 4, desktopBounds.a.y, desktopBounds.a.x + 3 * width / 4, desktopBounds.a.y + halfHeight);
			rects[3] = TRect(desktopBounds.a.x + 3 * width / 4, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.a.y + halfHeight);
			rects[4] = TRect(desktopBounds.a.x, desktopBounds.a.y + halfHeight, desktopBounds.a.x + width / 3, desktopBounds.b.y);
			rects[5] = TRect(desktopBounds.a.x + width / 3, desktopBounds.a.y + halfHeight, desktopBounds.a.x + 2 * width / 3, desktopBounds.b.y);
			rects[6] = TRect(desktopBounds.a.x + 2 * width / 3, desktopBounds.a.y + halfHeight, desktopBounds.b.x, desktopBounds.b.y);
			break;
		case 8:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + width / 4, desktopBounds.a.y + halfHeight);
			rects[1] = TRect(desktopBounds.a.x + width / 4, desktopBounds.a.y, desktopBounds.a.x + 2 * width / 4, desktopBounds.a.y + halfHeight);
			rects[2] = TRect(desktopBounds.a.x + 2 * width / 4, desktopBounds.a.y, desktopBounds.a.x + 3 * width / 4, desktopBounds.a.y + halfHeight);
			rects[3] = TRect(desktopBounds.a.x + 3 * width / 4, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.a.y + halfHeight);
			rects[4] = TRect(desktopBounds.a.x, desktopBounds.a.y + halfHeight, desktopBounds.a.x + width / 4, desktopBounds.b.y);
			rects[5] = TRect(desktopBounds.a.x + width / 4, desktopBounds.a.y + halfHeight, desktopBounds.a.x + 2 * width / 4, desktopBounds.b.y);
			rects[6] = TRect(desktopBounds.a.x + 2 * width / 4, desktopBounds.a.y + halfHeight, desktopBounds.a.x + 3 * width / 4, desktopBounds.b.y);
			rects[7] = TRect(desktopBounds.a.x + 3 * width / 4, desktopBounds.a.y + halfHeight, desktopBounds.b.x, desktopBounds.b.y);
			break;
		case 9:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + width / 5, desktopBounds.a.y + halfHeight);
			rects[1] = TRect(desktopBounds.a.x + width / 5, desktopBounds.a.y, desktopBounds.a.x + 2 * width / 5, desktopBounds.a.y + halfHeight);
			rects[2] = TRect(desktopBounds.a.x + 2 * width / 5, desktopBounds.a.y, desktopBounds.a.x + 3 * width / 5, desktopBounds.a.y + halfHeight);
			rects[3] = TRect(desktopBounds.a.x + 3 * width / 5, desktopBounds.a.y, desktopBounds.a.x + 4 * width / 5, desktopBounds.a.y + halfHeight);
			rects[4] = TRect(desktopBounds.a.x + 4 * width / 5, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.a.y + halfHeight);
			rects[5] = TRect(desktopBounds.a.x, desktopBounds.a.y + halfHeight, desktopBounds.a.x + width / 4, desktopBounds.b.y);
			rects[6] = TRect(desktopBounds.a.x + width / 4, desktopBounds.a.y + halfHeight, desktopBounds.a.x + 2 * width / 4, desktopBounds.b.y);
			rects[7] = TRect(desktopBounds.a.x + 2 * width / 4, desktopBounds.a.y + halfHeight, desktopBounds.a.x + 3 * width / 4, desktopBounds.b.y);
			rects[8] = TRect(desktopBounds.a.x + 3 * width / 4, desktopBounds.a.y + halfHeight, desktopBounds.b.x, desktopBounds.b.y);
			break;
	}

	TProgram::deskTop->lock();
	for (int i = 0; i < count; i++) {
		visibleWindows[i]->locate(rects[i]);
	}
	TProgram::deskTop->unlock();

	return true;
}
