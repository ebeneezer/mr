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
#include <set>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "MRDialogPaths.hpp"
#include "MRPerformance.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/TMREditWindow.hpp"
#include "../../ui/MRWindowSupport.hpp"

namespace {
void collectEditWindowsInZOrder(TView *view, void *arg) {
	std::vector<TMREditWindow *> *windows = static_cast<std::vector<TMREditWindow *> *>(arg);
	TMREditWindow *win = dynamic_cast<TMREditWindow *>(view);

	if (windows != nullptr && win != nullptr)
		windows->push_back(win);
}
} // namespace

std::vector<TMREditWindow *> allEditWindowsInZOrder() {
	std::vector<TMREditWindow *> windows;

	if (TProgram::deskTop == nullptr)
		return windows;

	TProgram::deskTop->forEach(collectEditWindowsInZOrder, &windows);
	return windows;
}

namespace {
short nextEditorWindowNumber() {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
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
#include <sstream>

static int g_currentVirtualDesktop = 1;

int currentVirtualDesktop() {
	return g_currentVirtualDesktop;
}

void syncVirtualDesktopVisibility() {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	for (TMREditWindow *win : windows) {
		if (win->virtualDesktop_ == g_currentVirtualDesktop) {
			if ((win->state & sfVisible) == 0) {
				win->show();
			}
		} else {
			if ((win->state & sfVisible) != 0) {
				win->hide();
			}
		}
	}
	if (TProgram::deskTop != nullptr) {
		TProgram::deskTop->drawView();
	}
}

void setCurrentVirtualDesktop(int vd) {
	if (vd < 1) vd = 1;
	int maxVd = configuredVirtualDesktops();
	if (maxVd < 1) maxVd = 1;
	if (vd > maxVd) vd = maxVd;
	g_currentVirtualDesktop = vd;
	syncVirtualDesktopVisibility();
}

bool moveToNextVirtualDesktop() {
	TMREditWindow *win = currentEditWindow();
	if (win == nullptr) return false;
	int maxVd = configuredVirtualDesktops();
	if (win->virtualDesktop_ >= maxVd) return false;
	win->virtualDesktop_++;
	syncVirtualDesktopVisibility();
	return true;
}

bool moveToPrevVirtualDesktop() {
	TMREditWindow *win = currentEditWindow();
	if (win == nullptr) return false;
	if (win->virtualDesktop_ <= 1) return false;
	win->virtualDesktop_--;
	syncVirtualDesktopVisibility();
	return true;
}

bool viewportRight() {
	int maxVd = configuredVirtualDesktops();
	if (g_currentVirtualDesktop >= maxVd) return false;
	g_currentVirtualDesktop++;
	syncVirtualDesktopVisibility();
	return true;
}

bool viewportLeft() {
	if (g_currentVirtualDesktop <= 1) return false;
	g_currentVirtualDesktop--;
	syncVirtualDesktopVisibility();
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

	for (TMREditWindow *win : allEditWindowsInZOrder()) {
		TMRFileEditor *editor = win->getEditor();
		if (editor == nullptr) continue;
		std::string url = editor->persistentFileName();
        if (url.empty()) continue;
		TRect r = win->getBounds();
		int cx = editor->cursor.x;
		int cy = editor->cursor.y;
		int vd = win->virtualDesktop_;

        std::string e_url;
        for (char c : url) {
            if (c == '\'') e_url += "''";
            else e_url += c;
        }

		std::string wsLine = "MRSETUP('WORKSPACE', 'URL=" + e_url +
		" size=" + std::to_string(r.b.x - r.a.x) + "," + std::to_string(r.b.y - r.a.y) +
		" pos=" + std::to_string(r.a.x) + "," + std::to_string(r.a.y) +
		" cursor=" + std::to_string(cx) + "," + std::to_string(cy) +
		" vd=" + std::to_string(vd) + "');";
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
		return;
	}

	std::istringstream iss(currentContent);
	std::string line;
	std::string failedFiles = "";
	while (std::getline(iss, line)) {
		if (line.find("MRSETUP('WORKSPACE'") != std::string::npos) {
			size_t start = line.find("URL=");
			if (start != std::string::npos) {
				size_t space = line.find(" ", start);
				std::string url = line.substr(start + 4, space - (start + 4));

                std::string u_url;
                for (size_t i = 0; i < url.length(); ++i) {
                    if (url[i] == '\'' && i + 1 < url.length() && url[i+1] == '\'') {
                        u_url += '\'';
                        ++i;
                    } else {
                        u_url += url[i];
                    }
                }
                url = u_url;

				TMREditWindow *win = createEditorWindow(url.c_str());
				if (win != nullptr) {
					std::string err;
					TMRFileEditor *editor = win->getEditor();
					if (editor != nullptr) {
                        editor->loadMappedFile(url, err);
                    }
                    if (editor == nullptr || editor->persistentFileName()[0] == '\0') {
						if (!failedFiles.empty()) failedFiles += ", ";
						size_t pos = url.find_last_of("\\/");
						failedFiles += (pos == std::string::npos ? url : url.substr(pos + 1));
					}

					int width = -1, height = -1;
					size_t sizeStart = line.find("size=");
					if (sizeStart != std::string::npos) {
						sscanf(line.c_str() + sizeStart, "size=%d,%d", &width, &height);
					}
					int x = -1, y = -1;
					size_t posStart = line.find("pos=");
					if (posStart != std::string::npos) {
						sscanf(line.c_str() + posStart, "pos=%d,%d", &x, &y);
					}
					int cx = -1, cy = -1;
					size_t curStart = line.find("cursor=");
					if (curStart != std::string::npos) {
						sscanf(line.c_str() + curStart, "cursor=%d,%d", &cx, &cy);
					}
					int vd = 1;
					size_t vdStart = line.find("vd=");
					if (vdStart != std::string::npos) {
						sscanf(line.c_str() + vdStart, "vd=%d", &vd);
					}

					if (width != -1 && height != -1 && x != -1 && y != -1) {
						win->changeBounds(TRect(x, y, x + width, y + height));
					}
					if (cx != -1 && cy != -1 && win->getEditor() != nullptr) {
						win->getEditor()->cursor.x = cx;
						win->getEditor()->cursor.y = cy;
					}
					win->virtualDesktop_ = vd;
				}
			}
		}
	}
	if (!failedFiles.empty()) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEvent, "Failed to load workspace files: " + failedFiles,
		    mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
	}
	syncVirtualDesktopVisibility();
}

TMREditWindow *createEditorWindow(const char *title) {
	TRect bounds;
	TMREditWindow *win;

	if (TProgram::deskTop == nullptr)
		return nullptr;
	bounds = TProgram::deskTop->getExtent();
	bounds.grow(-2, -1);
	win = new TMREditWindow(bounds, title, nextEditorWindowNumber());
	TProgram::deskTop->insert(win);
	if (win != nullptr)
		win->flags |= (wfMove | wfGrow | wfZoom | wfClose);
	if (win != nullptr && win->getEditor() != nullptr)
		win->getEditor()->setInsertModeEnabled(configuredDefaultInsertMode());
	return win;
}

TMREditWindow *currentEditWindow() {
	if (TProgram::deskTop == nullptr || TProgram::deskTop->current == nullptr)
		return nullptr;
	return dynamic_cast<TMREditWindow *>(TProgram::deskTop->current);
}

TMREditWindow *findEditWindowByBufferId(int bufferId) {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	for (auto & window : windows)
		if (window != nullptr && window->bufferId() == bufferId)
			return window;
	return nullptr;
}

bool isEmptyUntitledEditableWindow(TMREditWindow *win) {
	if (win == nullptr || win->isReadOnly() || win->currentFileName()[0] != '\0' || win->isFileChanged())
		return false;
	return win->isBufferEmpty();
}

TMREditWindow *findReusableEmptyWindow(TMREditWindow *preferred) {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	if (preferred != nullptr && isEmptyUntitledEditableWindow(preferred))
		return preferred;
	for (auto & window : windows)
		if (isEmptyUntitledEditableWindow(window))
			return window;
	return nullptr;
}

bool closeCurrentEditWindow() {
	TMREditWindow *win = currentEditWindow();
	if (win == nullptr)
		return false;
	message(win, evCommand, cmClose, nullptr);
	return mrEnsureUsableWorkWindow();
}

bool activateRelativeEditWindow(int delta) {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	TMREditWindow *current = currentEditWindow();
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
	TMREditWindow *win = currentEditWindow();
	if (win == nullptr)
		return false;
	win->hide();
	return mrEnsureUsableWorkWindow();
}

void mrUpdateAllWindowsColorTheme() {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	for (auto & window : windows) {
		if (window != nullptr) {
			window->applyWindowColorThemeForPath(window->currentFileName());
		}
	}
}


// ---- Consolidated from MRFileCommands.cpp ----

namespace {
ushort execDialogWithPayload(TDialog *dialog, void *data) {
	ushort result = cmCancel;
	if (dialog == nullptr)
		return cmCancel;
	if (data != nullptr)
		dialog->setData(data);
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel && data != nullptr)
		dialog->getData(data);
	TObject::destroy(dialog);
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
		messageBox(mfError | mfOKButton, "No file name specified.");
		return false;
	}
	if (hasWildcardPattern(outResolvedPath)) {
		messageBox(mfError | mfOKButton, "Wildcards are not allowed in save file names.");
		return false;
	}
	rememberLoadDialogPath(outResolvedPath.c_str());
	return true;
}

bool saveWindowSnapshotToPath(TMREditWindow *win, const std::string &resolvedPath) {
	std::ofstream outFile;
	std::string text;
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

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
		messageBox(mfError | mfOKButton, "No file name specified.");
		return false;
	}
	if (::access(resolvedPath.c_str(), F_OK) != 0 && !disableExtensionSearch &&
	    !hasWildcardPattern(resolvedPath) && !hasExtensionInBaseName(resolvedPath))
		static_cast<void>(resolveWithConfiguredExtensions(resolvedPath, resolvedPath));
	if (access(resolvedPath.c_str(), F_OK) != 0) {
		messageBox(mfError | mfOKButton, "File does not exist:\n%s", resolvedPath.c_str());
		return false;
	}
	if (access(resolvedPath.c_str(), R_OK) != 0) {
		messageBox(mfError | mfOKButton, "File is not readable:\n%s", resolvedPath.c_str());
		return false;
	}
	rememberLoadDialogPath(resolvedPath.c_str());
	return true;
}

bool loadResolvedFileIntoWindow(TMREditWindow *win, const std::string &resolvedPath, const char *operationLabel) {
	const auto fallbackLoadStartedAt = std::chrono::steady_clock::now();
	if (win == nullptr)
		return false;
	if (!win->loadFromFile(resolvedPath.c_str())) {
		messageBox(mfError | mfOKButton, "Unable to load file:\n%s", resolvedPath.c_str());
		return false;
	}
	const TMRFileEditor::LoadTiming timing = win->lastLoadTiming();
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
	TMREditWindow *win = currentEditWindow();

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
	TMREditWindow *win = currentEditWindow();
	std::string resolvedPath;
	bool isLogWindow = false;

	if (win == nullptr)
		return false;
	isLogWindow = win->windowRole() == TMREditWindow::wrLog;
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
			messageBox(mfError | mfOKButton, "Unable to save log file:\n%s", resolvedPath.c_str());
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
	std::vector<TMREditWindow *> allWindows = allEditWindowsInZOrder();
	std::vector<TMREditWindow *> visibleWindows;
	TRect desktopBounds;

	if (TProgram::deskTop == nullptr)
		return false;

	desktopBounds = TProgram::deskTop->getExtent();

	for (auto it = allWindows.rbegin(); it != allWindows.rend(); ++it) {
		TMREditWindow *win = *it;
		if (win != nullptr && (win->state & sfVisible) != 0) {
			visibleWindows.push_back(win);
		}
	}

	if (visibleWindows.empty())
		return true;

	int cascadeIndex = 0;
	TProgram::deskTop->lock();
	for (TMREditWindow *win : visibleWindows) {
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
	std::vector<TMREditWindow *> allWindows = allEditWindowsInZOrder();
	std::vector<TMREditWindow *> visibleWindows;
	TRect desktopBounds;

	if (TProgram::deskTop == nullptr)
		return false;

	desktopBounds = TProgram::deskTop->getExtent();

	for (auto it = allWindows.rbegin(); it != allWindows.rend(); ++it) {
		TMREditWindow *win = *it;
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
