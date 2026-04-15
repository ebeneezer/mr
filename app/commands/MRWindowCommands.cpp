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
#include <chrono>
#include <thread>
#include <cmath>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
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

MRWindowManager& MRWindowManager::instance() {
    static MRWindowManager inst;
    return inst;
}

void MRWindowManager::snapToEdges(TMREditWindow* window, TRect limits, const TPoint& mousePos, TPoint minSize, TPoint maxSize, TRect& outBounds, bool& isSnapped) {
    if (window == nullptr) return;

    int halfWidth = (limits.b.x - limits.a.x) / 2;
    int halfHeight = (limits.b.y - limits.a.y) / 2;

    isSnapped = true;

    if (mousePos.x <= limits.a.x) {
        // Snap left
        outBounds = TRect(limits.a.x, limits.a.y, limits.a.x + halfWidth, limits.b.y);
    } else if (mousePos.x >= limits.b.x - 1) {
        // Snap right
        outBounds = TRect(limits.a.x + halfWidth, limits.a.y, limits.b.x, limits.b.y);
    } else if (mousePos.y <= limits.a.y) {
        // Snap top
        outBounds = TRect(limits.a.x, limits.a.y, limits.b.x, limits.a.y + halfHeight);
    } else if (mousePos.y >= limits.b.y - 1) {
        // Snap bottom
        outBounds = TRect(limits.a.x, limits.a.y + halfHeight, limits.b.x, limits.b.y);
    } else {
        isSnapped = false;
    }
}

void MRWindowManager::animateBoundsChange(TMREditWindow* window, const TRect& targetBounds) {
    TRect startBounds = window->getBounds();
    if (startBounds == targetBounds) return;

    int steps = 5;
    for (int i = 1; i <= steps; ++i) {
        TRect currentBounds;
        currentBounds.a.x = startBounds.a.x + (targetBounds.a.x - startBounds.a.x) * i / steps;
        currentBounds.a.y = startBounds.a.y + (targetBounds.a.y - startBounds.a.y) * i / steps;
        currentBounds.b.x = startBounds.b.x + (targetBounds.b.x - startBounds.b.x) * i / steps;
        currentBounds.b.y = startBounds.b.y + (targetBounds.b.y - startBounds.b.y) * i / steps;

        window->locate(currentBounds);
        if (TProgram::deskTop != nullptr) {
            TProgram::deskTop->drawView(); // Ensure the desktop is updated
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
}

void MRWindowManager::dragWindow(TMREditWindow* window, TEvent& event, uchar mode) {
    if (window == nullptr || TProgram::deskTop == nullptr) return;

    bool windowManagerEnabled = configuredEditSetupSettings().windowManager;

    TRect limits = window->owner->getExtent();
    TPoint minSize, maxSize;
    window->sizeLimits(minSize, maxSize);

    if (!windowManagerEnabled) {
        window->dragView(event, window->dragMode | mode, limits, minSize, maxSize);
        return;
    }

    TRect originalBounds = window->getBounds();
    TRect currentBounds = originalBounds;
    bool currentlySnapped = false;
    TRect snappedBounds;

    window->setState(sfDragging, True);

    if (event.what == evMouseDown) {
        if ((mode & dmDragMove) != 0) {
            TPoint p = window->origin - event.mouse.where;
            do {
                event.mouse.where += p;

                bool shouldSnap = false;
                TPoint globalMouse = window->owner->makeGlobal(event.mouse.where);
                TRect globalLimits = window->owner->getExtent(); // Assuming owner is desktop

                snapToEdges(window, globalLimits, globalMouse, minSize, maxSize, snappedBounds, shouldSnap);

                if (shouldSnap) {
                    if (!currentlySnapped) {
                        animateBoundsChange(window, snappedBounds);
                        currentlySnapped = true;
                        currentBounds = snappedBounds;
                    }
                } else {
                    if (currentlySnapped) {
                        animateBoundsChange(window, originalBounds);
                        currentlySnapped = false;
                        currentBounds = originalBounds;
                    }

                    // We can't call moveGrow directly since it is private.
                    // However, we only need to drag the window natively. If we are not snapped,
                    // we'll update bounds manually mimicking dmDragMove behavior
                    TPoint newOrigin = event.mouse.where + p;
                    TRect r(newOrigin.x, newOrigin.y, newOrigin.x + window->size.x, newOrigin.y + window->size.y);

                    // Apply limits
                    r.a.x = std::max(limits.a.x, std::min(r.a.x, limits.b.x - window->size.x + 1));
                    r.a.y = std::max(limits.a.y, std::min(r.a.y, limits.b.y - window->size.y + 1));
                    r.b.x = r.a.x + window->size.x;
                    r.b.y = r.a.y + window->size.y;

                    window->locate(r);
                    originalBounds = window->getBounds();
                }
            } while (window->mouseEvent(event, evMouseMove));
        } else {
            window->dragView(event, window->dragMode | mode, limits, minSize, maxSize);
        }
    }

    window->setState(sfDragging, False);
}

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

	if (win == nullptr)
		return false;
	if (win->isReadOnly()) {
		messageBox(mfInformation | mfOKButton, "Window is read-only.");
		mrLogMessage("Save As rejected for read-only window.");
		return false;
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
