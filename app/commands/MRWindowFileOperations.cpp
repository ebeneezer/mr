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
#include "MRWindowCommandsInternal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../coprocessor/MRCoprocessor.hpp"
#include "../utils/MRFileIOUtils.hpp"
#include "MRPerformance.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MRBentoHexEditor/MRBentoHexEditor.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRWindowLayout.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "../../ui/MRDesktopWindow.hpp"
#include "../../ui/widgets/MRScopedHistoryUI.hpp"
#include "../../dialogs/setup/MRSetupCommon.hpp"

using mr::window_commands::logWindowTiming;
using mr::window_commands::postWindowCommandError;

// ---- Consolidated from MRFileCommands.cpp ----

namespace {
[[nodiscard]] std::string normalizeTvPath(std::string_view path) {
	std::string result(path);

	for (char &ch : result)
		if (ch == '\\') ch = '/';
#ifdef __unix__
	if (result.size() >= 2 && ((result[0] >= 'A' && result[0] <= 'Z') || (result[0] >= 'a' && result[0] <= 'z')) && result[1] == ':') result.erase(0, 2);
#endif
	return result;
}

[[nodiscard]] std::string trimPathInput(std::string_view path) {
	std::size_t start = 0;
	std::size_t end = path.size();

	while (start < end && std::isspace(static_cast<unsigned char>(path[start])) != 0)
		++start;
	while (end > start && (std::isspace(static_cast<unsigned char>(path[end - 1])) != 0 || static_cast<unsigned char>(path[end - 1]) < 32))
		--end;

	std::string result(path.substr(start, end - start));
	if (result.size() >= 2 && ((result.front() == '"' && result.back() == '"') || (result.front() == '\'' && result.back() == '\''))) result = result.substr(1, result.size() - 2);
	return result;
}

[[nodiscard]] std::string expandUserPath(std::string_view path) {
	std::string result;

	if (path.empty()) return std::string();
	result = normalizeTvPath(trimPathInput(path));
	if (result.size() >= 2 && result[0] == '~' && result[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != nullptr && *home != '\0') return std::string(home) + result.substr(1);
	}
	return result;
}

[[nodiscard]] bool hasWildcardPattern(std::string_view path) {
	return path.find('*') != std::string_view::npos || path.find('?') != std::string_view::npos;
}

[[nodiscard]] std::size_t lastPathSeparator(std::string_view path) {
	const std::size_t slash = path.find_last_of('/');
	const std::size_t backslash = path.find_last_of('\\');

	if (slash == std::string_view::npos) return backslash;
	if (backslash == std::string_view::npos) return slash;
	return std::max(slash, backslash);
}

[[nodiscard]] std::string baseNameForDisplay(const std::string &path) {
	const std::size_t sep = lastPathSeparator(path);

	if (sep == std::string::npos || sep + 1 >= path.size()) return path;
	return path.substr(sep + 1);
}

[[nodiscard]] long long roundedMilliseconds(double valueMs) {
	if (valueMs <= 0.0) return 0;
	return static_cast<long long>(valueMs + 0.5);
}

void postLoadHeroEvents(const std::string &resolvedPath, std::size_t bytes, double loadMs, std::size_t lineCount, bool lineCountExact, double lineCountMs) {
	const std::string fileName = baseNameForDisplay(resolvedPath);
	const std::string loadText = "loaded " + fileName + " in " + (roundedMilliseconds(loadMs) >= 1 ? std::to_string(roundedMilliseconds(loadMs)) : "<1") + " ms";
	std::string lineText;
	const std::chrono::milliseconds loadDuration = mr::messageline::autoDurationForText(loadText);

	if (lineCountExact)
		lineText = "indexed " + std::to_string(bytes) + " bytes, " + std::to_string(lineCount) + " lines, " + std::to_string(roundedMilliseconds(lineCountMs)) + " ms";
	else
		lineText = "mapped " + std::to_string(bytes) + " bytes, est. " + std::to_string(lineCount) + " lines, index warming";
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEvent, loadText, mr::messageline::Kind::Success, mr::messageline::kPriorityHigh);
	mr::messageline::postAutoTimedAfter(mr::messageline::Owner::HeroEventFollowup, lineText, mr::messageline::Kind::Info, loadDuration, mr::messageline::kPriorityLow);
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

		if (ext.empty()) continue;
		for (std::size_t p = 0; p < ext.size(); ++p) {
			candidates[1][p] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[p])));
			candidates[2][p] = static_cast<char>(std::toupper(static_cast<unsigned char>(ext[p])));
		}

		for (const std::string &candidateExt : candidates) {
			std::string candidate = basePath + "." + candidateExt;
			if (!tried.insert(candidate).second) continue;
			if (::access(candidate.c_str(), F_OK) == 0 && ::access(candidate.c_str(), R_OK) == 0) {
				resolvedPath = candidate;
				return true;
			}
		}
	}
	return false;
}

[[nodiscard]] bool shouldAutoOpenHexEditor(const std::string &resolvedPath) {
	return configuredAutoDetectBinaryFiles() && fileContainsNulInBoundarySamples(resolvedPath);
}

MREditWindow *createFileLoadWindow(MRWindowOpenBatch &openBatch, bool useBatch, bool useHexEditor) {
	if (useBatch) return useHexEditor ? static_cast<MREditWindow *>(openBatch.createHexEditorWindow("?No-File?")) : openBatch.createEditorWindow("?No-File?");
	return useHexEditor ? static_cast<MREditWindow *>(createHexEditorWindow("?No-File?")) : createEditorWindow("?No-File?");
}

MREditWindow *applyAutoDetectedHexEditor(MREditWindow *window, bool useHexEditor) {
	MRBentoHexEditor *hexEditor = nullptr;

	if (!useHexEditor || window == nullptr) return window;
	hexEditor = dynamic_cast<MRBentoHexEditor *>(window);
	if (hexEditor != nullptr) return hexEditor;
	hexEditor = convertEditWindowToHexEditor(window);
	if (hexEditor != nullptr) return hexEditor;
	postWindowCommandError("Could not activate Hex editor for detected binary file.");
	return window;
}
} // namespace

bool promptForPath(MRDialogHistoryScope scope, const char *title, char *fileName, std::size_t fileNameSize) {
	ushort result = cmCancel;

	if (fileName == nullptr || fileNameSize == 0) return false;
	fileName[0] = '\0';
	mr::dialogs::seedFileDialogPath(scope, fileName, fileNameSize, "*.*");
	result = mr::dialogs::execRememberingFileDialogWithData(scope, "*.*", title, "~N~ame", fdOpenButton, fileName);
	if (result == cmCancel) return false;
	return true;
}

bool promptForPath(const char *title, char *fileName, std::size_t fileNameSize) {
	return promptForPath(MRDialogHistoryScope::LoadFile, title, fileName, fileNameSize);
}

bool promptForSaveAsPath(const char *title, const char *initialPath, std::string &outResolvedPath) {
	char fileName[MAXPATH] = {0};
	ushort result = cmCancel;
	MRDialogHistoryScope scope = MRDialogHistoryScope::EditorSaveAs;

	outResolvedPath.clear();
	if (std::string_view(title != nullptr ? title : "") == "SAVE LOG AS") scope = MRDialogHistoryScope::SaveLogAs;
	mr::dialogs::seedFileDialogPath(scope, fileName, sizeof(fileName), "*.*");
	mr::dialogs::suggestFileDialogName(fileName, sizeof(fileName), initialPath != nullptr ? std::string_view(initialPath) : std::string_view());
	result = mr::dialogs::execRememberingFileDialogWithData(scope, "*.*", title, "~N~ame", fdOKButton, fileName);
	if (result == cmCancel) return false;
	outResolvedPath = expandUserPath(fileName);
	if (outResolvedPath.empty()) {
		postWindowCommandError("No file name specified.");
		return false;
	}
	if (hasWildcardPattern(outResolvedPath)) {
		postWindowCommandError("Wildcards are not allowed in save file names.");
		return false;
	}
	rememberLoadDialogPath(scope, outResolvedPath.c_str());
	return true;
}

bool saveWindowSnapshotToPath(MREditWindow *win, const std::string &resolvedPath) {
	std::ofstream outFile;
	std::string text;
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

	if (win == nullptr || editor == nullptr || resolvedPath.empty()) return false;
	text = editor->snapshotText();
	outFile.open(resolvedPath.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!outFile.is_open()) return false;
	outFile.write(text.data(), static_cast<std::streamsize>(text.size()));
	outFile.close();
	return outFile.good();
}

bool resolveReadableExistingPath(MRDialogHistoryScope scope, const char *path, std::string &resolvedPath, bool reportErrors) {
	bool disableExtensionSearch = false;
	std::string rawInput = expandUserPath(path != nullptr ? std::string_view(path) : std::string_view());

	static_cast<void>(scope);
	resolvedPath = rawInput;
	if (!resolvedPath.empty() && resolvedPath.back() == '.' && !hasWildcardPattern(resolvedPath)) {
		disableExtensionSearch = true;
		resolvedPath.pop_back();
	}
	if (resolvedPath.empty()) {
		if (reportErrors) postWindowCommandError("No file name specified.");
		return false;
	}
	if (::access(resolvedPath.c_str(), F_OK) != 0 && !disableExtensionSearch && !hasWildcardPattern(resolvedPath) && !hasExtensionInBaseName(resolvedPath)) static_cast<void>(resolveWithConfiguredExtensions(resolvedPath, resolvedPath));
	if (access(resolvedPath.c_str(), F_OK) != 0) {
		if (reportErrors) postWindowCommandError("File does not exist: " + resolvedPath);
		return false;
	}
	if (access(resolvedPath.c_str(), R_OK) != 0) {
		if (reportErrors) postWindowCommandError("File is not readable: " + resolvedPath);
		return false;
	}
	return true;
}

bool loadResolvedFileIntoWindow(MREditWindow *win, const std::string &resolvedPath, const char *operationLabel) {
	return loadResolvedFileIntoWindow(win, resolvedPath, operationLabel, MRFileLoadMessages::PerFile);
}

bool loadResolvedFileIntoWindow(MREditWindow *win, const std::string &resolvedPath, const char *operationLabel, MRFileLoadMessages messages) {
	const auto fallbackLoadStartedAt = std::chrono::steady_clock::now();
	if (win == nullptr) return false;
	if (!win->loadFromFile(resolvedPath.c_str())) {
		postWindowCommandError("Unable to load file: " + resolvedPath);
		return false;
	}
	if (MRBentoHexEditor *hexEditor = dynamic_cast<MRBentoHexEditor *>(win); hexEditor != nullptr) {
		hexEditor->synchronizePaneDocumentState();
	}
	const MRFileEditor::LoadTiming timing = win->lastLoadTiming();
	std::size_t bytes = win->bufferLength();
	std::size_t lines = 0;
	bool linesExact = false;
	double loadMs = 0.0;
	double lineCountMs = 0.0;

	if (timing.valid) {
		bytes = timing.bytes;
		lines = timing.lines;
		linesExact = timing.linesExact;
		loadMs = timing.mappedLoadMs;
		lineCountMs = timing.lineCountMs;
	} else {
		loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - fallbackLoadStartedAt).count();
		if (win->exactLineCountKnown()) {
			const auto lineCountStartedAt = std::chrono::steady_clock::now();
			lines = win->bufferLineCount();
			lineCountMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lineCountStartedAt).count();
			linesExact = true;
		} else
			lines = win->estimatedLineCount();
	}

	mr::performance::recordUiEvent(operationLabel != nullptr ? operationLabel : "Load file", static_cast<std::size_t>(win->bufferId()), win->documentId(), bytes, loadMs, resolvedPath);
	mr::performance::recordUiEvent("Line count", static_cast<std::size_t>(win->bufferId()), win->documentId(), bytes, lineCountMs, resolvedPath);
	if (messages == MRFileLoadMessages::PerFile)
		postLoadHeroEvents(resolvedPath, bytes, loadMs, lines, linesExact, lineCountMs);
	else if (win->getEditor() != nullptr)
		win->getEditor()->markMiniMapInitialRenderReported();
	return true;
}

namespace {
bool openResolvedFilesIntoWindowsWithBatch(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation, MREditWindow *restoreWindow, MRFileLoadMessages messages,
                                           MRWindowOpenBatch *sharedBatch) {
	MREditWindow *current = currentEditWindow();
	MREditWindow *previousActive = restoreWindow != nullptr ? restoreWindow : current;
	MREditWindow *lastLoadedWindow = nullptr;
	MRWindowOpenBatch localBatch;
	MRWindowOpenBatch &openBatch = sharedBatch != nullptr ? *sharedBatch : localBatch;
	const bool useBatch = sharedBatch != nullptr || resolvedPaths.size() > 1;

	for (const std::string &resolvedPath : resolvedPaths) {
		const bool useHexEditor = shouldAutoOpenHexEditor(resolvedPath);
		MREditWindow *target = findReusableEmptyWindow(current);
		bool createdTarget = false;

		if (target == nullptr) {
			target = createFileLoadWindow(openBatch, useBatch, useHexEditor);
			createdTarget = true;
		}
		if (target == nullptr) continue;
		if (!loadResolvedFileIntoWindow(target, resolvedPath, "Open file", messages)) {
			forgetLoadDialogPath(MRDialogHistoryScope::LoadFile, resolvedPath.c_str());
			if (createdTarget) message(target, evCommand, cmClose, nullptr);
			if (target != nullptr && isEmptyUntitledEditableWindow(target) && current != target && current != nullptr) static_cast<void>(mrActivateEditWindow(current));
			continue;
		}
		MREditWindow *loadedTarget = applyAutoDetectedHexEditor(target, useHexEditor);

		if (previousActive == target) previousActive = loadedTarget;
		if (current == target) current = loadedTarget;
		target = loadedTarget;
		rememberLoadDialogPath(MRDialogHistoryScope::LoadFile, resolvedPath.c_str());
		lastLoadedWindow = target;
		current = target;
	}
	if (sharedBatch == nullptr && openBatch.active()) openBatch.finish(true, lastLoadedWindow != nullptr);
	if (sharedBatch == nullptr && lastLoadedWindow != nullptr) {
		if (activation == MRLoadedWindowActivation::ActivateLast) static_cast<void>(mrActivateEditWindow(lastLoadedWindow));
		else if (previousActive != nullptr && previousActive != lastLoadedWindow)
			static_cast<void>(mrActivateEditWindow(previousActive));
	}
	return lastLoadedWindow != nullptr;
}

bool loadResolvedFilesIntoWindowsWithBatch(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation, MREditWindow *restoreWindow, MRFileLoadMessages messages,
                                           MRWindowOpenBatch *sharedBatch) {
	MREditWindow *target = currentEditWindow();
	MREditWindow *previousActive = restoreWindow != nullptr ? restoreWindow : target;
	MREditWindow *lastLoadedWindow = nullptr;
	bool createdTarget = false;
	bool first = true;
	MRWindowOpenBatch localBatch;
	MRWindowOpenBatch &openBatch = sharedBatch != nullptr ? *sharedBatch : localBatch;
	const bool useBatch = sharedBatch != nullptr || resolvedPaths.size() > 1;

	if (resolvedPaths.empty()) return false;
	if (target != nullptr && !target->confirmAbandonForReload())
		return false;
	for (const std::string &resolvedPath : resolvedPaths) {
		const bool useHexEditor = shouldAutoOpenHexEditor(resolvedPath);
		MREditWindow *loadTarget = first ? target : nullptr;
		bool createdLoadTarget = false;

		if (first && loadTarget == nullptr) {
			loadTarget = createFileLoadWindow(openBatch, useBatch, useHexEditor);
			createdTarget = true;
			createdLoadTarget = true;
			target = loadTarget;
		} else if (!first) {
			loadTarget = findReusableEmptyWindow(nullptr);
			if (loadTarget == nullptr) {
				loadTarget = createFileLoadWindow(openBatch, useBatch, useHexEditor);
				createdLoadTarget = true;
			}
		}
		if (loadTarget == nullptr) {
			first = false;
			continue;
		}
		if (!loadResolvedFileIntoWindow(loadTarget, resolvedPath, "Load file", messages)) {
			forgetLoadDialogPath(MRDialogHistoryScope::LoadFile, resolvedPath.c_str());
			if (createdLoadTarget || (first && createdTarget)) message(loadTarget, evCommand, cmClose, nullptr);
			if (first && createdTarget) target = nullptr;
			first = false;
			continue;
		}
		MREditWindow *loadedTarget = applyAutoDetectedHexEditor(loadTarget, useHexEditor);

		if (previousActive == loadTarget) previousActive = loadedTarget;
		if (first) target = loadedTarget;
		loadTarget = loadedTarget;
		rememberLoadDialogPath(MRDialogHistoryScope::LoadFile, resolvedPath.c_str());
		lastLoadedWindow = loadTarget;
		first = false;
	}
	if (sharedBatch == nullptr && openBatch.active()) openBatch.finish(true, lastLoadedWindow != nullptr);
	if (sharedBatch == nullptr && lastLoadedWindow != nullptr) {
		if (activation == MRLoadedWindowActivation::ActivateLast) static_cast<void>(mrActivateEditWindow(lastLoadedWindow));
		else if (previousActive != nullptr && previousActive != lastLoadedWindow)
			static_cast<void>(mrActivateEditWindow(previousActive));
	}
	return lastLoadedWindow != nullptr;
}
} // namespace

bool openResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation, MREditWindow *restoreWindow) {
	return openResolvedFilesIntoWindows(resolvedPaths, activation, restoreWindow, MRFileLoadMessages::PerFile);
}

bool openResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation, MREditWindow *restoreWindow, MRFileLoadMessages messages) {
	return openResolvedFilesIntoWindowsWithBatch(resolvedPaths, activation, restoreWindow, messages, nullptr);
}

bool openResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation, MREditWindow *restoreWindow, MRFileLoadMessages messages,
                                  MRWindowOpenBatch &openBatch) {
	return openResolvedFilesIntoWindowsWithBatch(resolvedPaths, activation, restoreWindow, messages, &openBatch);
}

bool loadResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation, MREditWindow *restoreWindow) {
	return loadResolvedFilesIntoWindows(resolvedPaths, activation, restoreWindow, MRFileLoadMessages::PerFile);
}

bool loadResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation, MREditWindow *restoreWindow, MRFileLoadMessages messages) {
	return loadResolvedFilesIntoWindowsWithBatch(resolvedPaths, activation, restoreWindow, messages, nullptr);
}

bool loadResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation, MREditWindow *restoreWindow, MRFileLoadMessages messages,
                                  MRWindowOpenBatch &openBatch) {
	return loadResolvedFilesIntoWindowsWithBatch(resolvedPaths, activation, restoreWindow, messages, &openBatch);
}

bool saveEditWindowAs(MREditWindow *win) {
	std::string resolvedPath;
	bool isLogWindow = false;
	const char *initialPath = nullptr;

	if (win == nullptr) return false;
	if (win->isReadOnly()) {
		isLogWindow = win->windowRole() == MREditWindow::wrLog;
		if (!isLogWindow) {
			messageBox(mfInformation | mfOKButton, "Window is read-only.");
			mrLogMessage("Save As rejected for read-only window.");
			return false;
		}
		initialPath = nullptr;
		if (!win->windowRoleDetail().empty()) initialPath = win->windowRoleDetail().c_str();
		if (!promptForSaveAsPath("SAVE LOG AS", initialPath, resolvedPath)) return false;
		auto startedAt = std::chrono::steady_clock::now();
		if (!saveWindowSnapshotToPath(win, resolvedPath)) {
			postWindowCommandError("Unable to save log file: " + resolvedPath);
			mrLogMessage("Save As failed.");
			return false;
		}
		mr::performance::recordUiEvent("Save log as", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(), resolvedPath);
		win->setWindowRole(MREditWindow::wrLog, resolvedPath);
		mrLogMessage("Log window saved as a new file.");
		return true;
	}
	auto startedAt = std::chrono::steady_clock::now();
	MREditWindow *previousActive = currentEditWindow();
	if (previousActive != win) static_cast<void>(mrActivateEditWindow(win));
	if (!win->saveCurrentFileAs()) {
		if (previousActive != nullptr && previousActive != win) static_cast<void>(mrActivateEditWindow(previousActive));
		mrLogMessage("Save As failed.");
		return false;
	}
	if (previousActive != nullptr && previousActive != win) static_cast<void>(mrActivateEditWindow(previousActive));
	mr::performance::recordUiEvent("Save file as", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(), win->currentFileName());
	mrLogMessage("Window saved as a new file.");
	return true;
}

bool saveAllDirtyEditWindows() {
	std::vector<MREditWindow *> dirtyWindows;
	std::size_t savedCount = 0;

	for (MREditWindow *win : allEditWindowsInZOrder()) {
		if (win == nullptr || !win->isFileChanged() || win->isReadOnly()) continue;
		dirtyWindows.push_back(win);
	}
	if (dirtyWindows.empty()) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "No dirty windows to save.", mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
		return true;
	}
	for (MREditWindow *win : dirtyWindows) {
		if (win == nullptr || !win->isFileChanged() || win->isReadOnly()) continue;
		if (win->canSaveInPlace()) {
			auto startedAt = std::chrono::steady_clock::now();
			if (!win->saveCurrentFile()) {
				postWindowCommandError("Save all stopped: save failed.");
				mrLogMessage("Save all failed.");
				return false;
			}
			mr::performance::recordUiEvent("Save file", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(), win->currentFileName());
			++savedCount;
			continue;
		}
		if (!saveEditWindowAs(win)) {
			postWindowCommandError("Save all cancelled.");
			mrLogMessage("Save all cancelled.");
			return false;
		}
		++savedCount;
	}
	{
		std::ostringstream line;
		line << "Saved " << savedCount << " dirty window";
		if (savedCount != 1) line << "s";
		line << ".";
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, line.str(), mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
		mrLogMessage(line.str());
	}
	return true;
}

bool revertEditWindow(MREditWindow *win) {
	std::string path;

	if (win == nullptr) return false;
	path = win->currentFileName();
	if (path.empty()) {
		postWindowCommandError("No saved file to revert to.");
		return false;
	}
	if (win->isFileChanged() && messageBox(mfConfirmation | mfYesButton | mfNoButton, "Revert window and discard changes?\n%s", path.c_str()) != cmYes) return false;
	auto startedAt = std::chrono::steady_clock::now();
	if (!win->loadFromFile(path.c_str())) {
		postWindowCommandError("Unable to revert file: " + path);
		mrLogMessage("Revert failed.");
		return false;
	}
	mr::performance::recordUiEvent("Revert file", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(), path);
	mrLogMessage("Window reverted.");
	return true;
}

bool saveCurrentEditWindow() {
	MREditWindow *win = currentEditWindow();

	if (win == nullptr) return false;
	if (win->isReadOnly()) {
		messageBox(mfInformation | mfOKButton, "Window is read-only.");
		mrLogMessage("Save rejected for read-only window.");
		return false;
	}
	if (!win->isFileChanged()) return true;
	if (win->canSaveInPlace()) {
		auto startedAt = std::chrono::steady_clock::now();
		if (!win->saveCurrentFile()) {
			mrLogMessage("Save failed.");
			return false;
		}
		mr::performance::recordUiEvent("Save file", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(), win->currentFileName());
		mrLogMessage("Window saved.");
		return true;
	}
	return saveEditWindowAs(win);
}

bool saveCurrentEditWindowAs() {
	MREditWindow *win = currentEditWindow();
	return saveEditWindowAs(win);
}

bool handleWindowCascade() {
	const auto startedAt = std::chrono::steady_clock::now();
	const auto enumerateStartedAt = startedAt;
	std::vector<MRDesktopWindow *> allWindows = allDesktopWindowsInZOrder();
	std::vector<MRDesktopWindow *> visibleWindows;
	TRect desktopBounds;
	long long enumerateUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - enumerateStartedAt).count();
	long long filterUs = 0;
	long long locateUs = 0;

	if (TProgram::deskTop == nullptr) return false;

	desktopBounds = MRWindowLayout::usableDesktopBounds();

	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		for (auto it = allWindows.rbegin(); it != allWindows.rend(); ++it) {
			MRDesktopWindow *window = *it;
			TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;
			if (nativeWindow != nullptr && (nativeWindow->options & ofTileable) != 0 && (nativeWindow->state & sfVisible) != 0 && !window->desktopMinimized()) {
				visibleWindows.push_back(window);
			}
		}
		filterUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}

	if (visibleWindows.empty()) return true;

	int cascadeIndex = 0;
	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		TProgram::deskTop->lock();
		for (MRDesktopWindow *window : visibleWindows) {
			const auto windowStartedAt = std::chrono::steady_clock::now();
			TRect bounds;
			bounds.a.x = desktopBounds.a.x + cascadeIndex;
			bounds.a.y = desktopBounds.a.y + cascadeIndex;
			bounds.b.x = desktopBounds.b.x;
			bounds.b.y = desktopBounds.b.y;
			MRWindowLayout::applyBatchWindowBounds(window, bounds);
			{
				const long long windowUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - windowStartedAt).count();
				if (windowUs >= 10000) logWindowTiming("Window cascade bounds slow", windowUs, "index=" + std::to_string(cascadeIndex));
			}
			cascadeIndex++;
		}
		TProgram::deskTop->unlock();
		MRWindowLayout::refreshDesktopProjection();
		mrNotifyWindowTopologyChanged();
		locateUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	{
		std::ostringstream detail;
		const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();

		detail << "all=" << allWindows.size() << " visible=" << visibleWindows.size() << " enumerate_us=" << enumerateUs << " filter_us=" << filterUs << " locate_us=" << locateUs;
		logWindowTiming("Window cascade timing", tookUs, detail.str());
	}
	return true;
}

bool handleWindowTile() {
	const auto startedAt = std::chrono::steady_clock::now();
	const auto enumerateStartedAt = startedAt;
	std::vector<MRDesktopWindow *> allWindows = allDesktopWindowsInZOrder();
	std::vector<MRDesktopWindow *> visibleWindows;
	TRect desktopBounds;
	long long enumerateUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - enumerateStartedAt).count();
	long long filterUs = 0;
	long long locateUs = 0;

	if (TProgram::deskTop == nullptr) return false;

	desktopBounds = MRWindowLayout::usableDesktopBounds();

	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		for (auto it = allWindows.rbegin(); it != allWindows.rend(); ++it) {
			MRDesktopWindow *window = *it;
			TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;
			if (nativeWindow != nullptr && (nativeWindow->options & ofTileable) != 0 && (nativeWindow->state & sfVisible) != 0 && !window->desktopMinimized()) {
				visibleWindows.push_back(window);
			}
		}
		filterUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}

	int count = visibleWindows.size();

	if (count > 9) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEvent, "max 9 windows can be tiled", mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return true;
	}

	if (count == 0) return true;

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

	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		TProgram::deskTop->lock();
		for (int i = 0; i < count; i++) {
			const auto windowStartedAt = std::chrono::steady_clock::now();
			MRWindowLayout::applyBatchWindowBounds(visibleWindows[i], rects[i]);
			{
				const long long windowUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - windowStartedAt).count();
				if (windowUs >= 10000) logWindowTiming("Window tile bounds slow", windowUs, "index=" + std::to_string(i));
			}
		}
		TProgram::deskTop->unlock();
		MRWindowLayout::refreshDesktopProjection();
		mrNotifyWindowTopologyChanged();
		locateUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	{
		std::ostringstream detail;
		const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();

		detail << "all=" << allWindows.size() << " visible=" << visibleWindows.size() << " enumerate_us=" << enumerateUs << " filter_us=" << filterUs << " locate_us=" << locateUs;
		logWindowTiming("Window tile timing", tookUs, detail.str());
	}
	return true;
}
