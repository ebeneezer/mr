#define Uses_TRect
#include <tvision/tv.h>

#include "MRWindowCommands.hpp"
#include "MRWindowCommandsInternal.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "../../config/settings/MRSettingsHistory.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../../mrmac/mrmac.h"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRFrame.hpp"
#include "../../ui/MRWindowSupport.hpp"

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;

using mr::window_commands::applicationUiInt;
using mr::window_commands::applicationUiString;
using mr::window_commands::applicationUiUnsigned;
using mr::window_commands::kWorkspaceBranch;
using mr::window_commands::logWindowTiming;
using mr::window_commands::postWindowCommandError;
using mr::window_commands::steadyClockMilliseconds;
using mr::window_commands::storeApplicationUiInt;
using mr::window_commands::storeApplicationUiString;
using mr::window_commands::storeApplicationUiUnsigned;

namespace {

static constexpr std::chrono::milliseconds kWorkspaceAutosaveDelay(1000);

bool autosaveWorkspacePath(std::time_t serializedAt, std::string &path) {
	std::tm localTime{};
	char dateTime[32]{};

	if (::localtime_r(&serializedAt, &localTime) == nullptr) return false;
	if (std::strftime(dateTime, sizeof(dateTime), "%Y-%m-%d %H:%M:%S", &localTime) == 0) return false;
	const std::string directory = workspaceAutosaveDirectoryPath();
	if (directory.empty()) return false;
	std::error_code directoryError;
	std::filesystem::create_directories(directory, directoryError);
	if (directoryError) {
		mrLogMessage("Workspace autosave directory could not be created: " + directory + ": " + directoryError.message());
		return false;
	}
	path = (std::filesystem::path(directory) / (std::string(".MR autosaved Workspace ") + dateTime + ".mrmac")).string();
	return true;
}

std::string normalizedWorkspacePathForWindow(const MREditWindow *win) {
	const MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	const char *path = editor != nullptr ? editor->persistentFileName() : nullptr;
	std::string normalizedPath;

	if (path == nullptr || *path == '\0') return std::string();
	normalizedPath = normalizeConfiguredPathInput(path);
	return normalizedPath.empty() ? std::string(path) : normalizedPath;
}


} // namespace

bool mrSetWorkspaceMainFile(MREditWindow *win) {
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	const std::string oldPath = applicationUiString(runtimeKv, kWorkspaceBranch, "mainFilePath");
	const std::string newPath = normalizedWorkspacePathForWindow(win);

	if (newPath.empty()) {
		postWindowCommandError("Workspace main file requires a saved file.");
		return false;
	}
	storeApplicationUiString(runtimeKv, kWorkspaceBranch, "mainFilePath", newPath);
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		const std::string windowPath = normalizedWorkspacePathForWindow(window);
		if (window != nullptr && window->frame != nullptr && (windowPath == oldPath || windowPath == newPath)) window->frame->drawView();
	}
	mrNotifyWindowTopologyChanged();
	return true;
}

void mrClearWorkspaceMainFile() {
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	const std::string oldPath = applicationUiString(runtimeKv, kWorkspaceBranch, "mainFilePath");

	if (oldPath.empty()) return;
	storeApplicationUiString(runtimeKv, kWorkspaceBranch, "mainFilePath", std::string());
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		const std::string windowPath = normalizedWorkspacePathForWindow(window);
		if (window != nullptr && window->frame != nullptr && windowPath == oldPath) window->frame->drawView();
	}
	mrNotifyWindowTopologyChanged();
}

bool mrIsWorkspaceMainFile(const MREditWindow *win) {
	const std::string path = normalizedWorkspacePathForWindow(win);
	const std::string mainPath = mrWorkspaceMainFilePath();

	return !path.empty() && !mainPath.empty() && path == mainPath;
}

std::string mrWorkspaceMainFilePath() {
	return applicationUiString(mrvmRuntimeKv(), kWorkspaceBranch, "mainFilePath");
}

void mrMarkWorkspaceAutosaveDirty(const char *source, const MREditWindow *window) {
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	const bool wasDirty = applicationUiInt(runtimeKv, kWorkspaceBranch, "autosaveDirty", 0) != 0;
	const bool preserveBefore = runtimePreserveAutosavedWorkspace();
	const char *reason = source != nullptr && *source != '\0' ? source : "unspecified";

	storeApplicationUiInt(runtimeKv, kWorkspaceBranch, "autosaveDirty", 1);
	storeApplicationUiUnsigned(runtimeKv, kWorkspaceBranch, "autosaveDueMs", steadyClockMilliseconds(std::chrono::steady_clock::now() + kWorkspaceAutosaveDelay));
	if (configuredAutosaveWorkspace()) setRuntimePreserveAutosavedWorkspace(false);
	if (!wasDirty) {
		std::ostringstream detail;

		detail << "Workspace autosave dirty false->true source=" << reason << " autosave=" << (configuredAutosaveWorkspace() ? 1 : 0) << " preserve=" << (preserveBefore ? 1 : 0) << "->" << (runtimePreserveAutosavedWorkspace() ? 1 : 0);
		if (window != nullptr) {
			const TRect bounds = window->getBounds();

			detail << " window=" << window->number << " bounds=" << bounds.a.x << "," << bounds.a.y << "," << bounds.b.x << "," << bounds.b.y;
		}
		mrLogMessage(detail.str());
	}
}

namespace {
void flushWorkspaceAutosave(bool force, std::uint64_t *nextWakeupMs = nullptr) {
	const auto startedAt = std::chrono::steady_clock::now();
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	std::string errorText;
	std::string autosavePath;
	MRSettingsWriteReport report;
	long long persistUs = 0;
	if (nextWakeupMs != nullptr) *nextWakeupMs = 0;

	if (applicationUiInt(runtimeKv, kWorkspaceBranch, "autosaveDirty", 0) == 0) return;
	if (!configuredAutosaveWorkspace()) return;
	if (runtimePreserveAutosavedWorkspace()) return;
	const std::uint64_t dueMs = applicationUiUnsigned(runtimeKv, kWorkspaceBranch, "autosaveDueMs", 0);
	if (!force && steadyClockMilliseconds(std::chrono::steady_clock::now()) < dueMs) {
		if (nextWakeupMs != nullptr) *nextWakeupMs = dueMs;
		return;
	}
	mrLogMessage(std::string("Workspace autosave flush begin force=") + (force ? "1" : "0") + ".");
	mrLogMessage(std::string("Workspace autosave dirty true->false source=flush force=") + (force ? "1" : "0") + ".");
	storeApplicationUiInt(runtimeKv, kWorkspaceBranch, "autosaveDirty", 0);
	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		if (!autosaveWorkspacePath(std::time(nullptr), autosavePath) || !mrSaveWorkspace(autosavePath)) {
			const std::uint64_t retryMs = steadyClockMilliseconds(std::chrono::steady_clock::now() + kWorkspaceAutosaveDelay);
			storeApplicationUiInt(runtimeKv, kWorkspaceBranch, "autosaveDirty", 1);
			storeApplicationUiUnsigned(runtimeKv, kWorkspaceBranch, "autosaveDueMs", retryMs);
			if (nextWakeupMs != nullptr) *nextWakeupMs = retryMs;
			mrLogMessage("Workspace autosave named serialization failed.");
			return;
		}
		rememberLoadDialogPath(MRDialogHistoryScope::WorkspaceSave, autosavePath.c_str());
		rememberLoadDialogPath(MRDialogHistoryScope::WorkspaceLoad, autosavePath.c_str());
		if (!persistConfiguredSettingsSnapshotWithWorkspace(&errorText, &report)) {
			const std::uint64_t retryMs = steadyClockMilliseconds(std::chrono::steady_clock::now() + kWorkspaceAutosaveDelay);
			storeApplicationUiInt(runtimeKv, kWorkspaceBranch, "autosaveDirty", 1);
			storeApplicationUiUnsigned(runtimeKv, kWorkspaceBranch, "autosaveDueMs", retryMs);
			if (nextWakeupMs != nullptr) *nextWakeupMs = retryMs;
			mrLogMessage("Workspace autosave dirty false->true source=flush-failed.");
			if (!errorText.empty()) mrLogMessage("Workspace autosave failed: " + errorText);
			return;
		}
		persistUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	// Reserve one slot for the successful save, even if the clock moved backwards.
	const std::filesystem::path savedPath(autosavePath);
	const std::filesystem::path directory = savedPath.parent_path();
	const std::string savedName = savedPath.filename().string();
	const std::size_t limit = static_cast<std::size_t>(configuredWorkspaceAutosaveLimit());
	std::vector<std::string> retained;
	std::error_code scanError;
	retained.reserve(limit);
	for (std::filesystem::directory_iterator entry(directory, scanError), end; !scanError && entry != end; entry.increment(scanError)) {
		const std::string name = entry->path().filename().string();
		std::tm timestamp{};
		char canonicalName[50]{};
		std::error_code fileError;

		if (name == savedName || name.size() != 49) continue;
		const char *parsedEnd = ::strptime(name.c_str(), ".MR autosaved Workspace %Y-%m-%d %H:%M:%S.mrmac", &timestamp);
		if (parsedEnd == nullptr || *parsedEnd != '\0') continue;
		if (std::strftime(canonicalName, sizeof(canonicalName), ".MR autosaved Workspace %Y-%m-%d %H:%M:%S.mrmac", &timestamp) == 0 || name != canonicalName) continue;
		const std::filesystem::file_status status = entry->symlink_status(fileError);
		if (fileError) {
			mrLogMessage("Workspace autosave retention could not inspect " + entry->path().string() + ": " + fileError.message());
			continue;
		}
		if (!std::filesystem::is_regular_file(status)) continue;

		// A bounded min-heap keeps only the newest candidates in memory.
		retained.push_back(name);
		std::push_heap(retained.begin(), retained.end(), std::greater<std::string>());
		if (retained.size() < limit) continue;
		std::pop_heap(retained.begin(), retained.end(), std::greater<std::string>());
		const std::filesystem::path oldest = directory / retained.back();
		retained.pop_back();
		if (std::filesystem::remove(oldest, fileError)) {
			forgetLoadDialogPath(MRDialogHistoryScope::WorkspaceSave, oldest.c_str());
			forgetLoadDialogPath(MRDialogHistoryScope::WorkspaceLoad, oldest.c_str());
		} else if (fileError) {
			mrLogMessage("Workspace autosave retention could not remove " + oldest.string() + ": " + fileError.message());
		}
	}
	if (scanError) mrLogMessage("Workspace autosave retention could not scan " + directory.string() + ": " + scanError.message());
	if (configuredSettingsDirty() && !persistConfiguredSettingsSnapshot(&errorText)) mrLogMessage("Workspace autosave history cleanup could not be saved: " + errorText);
	mrLogSettingsWriteReport("workspace autosave", report);
	logWindowTiming("Workspace autosave flush timing", std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count(), "persist_us=" + std::to_string(persistUs));
	mrLogMessage("Workspace autosave flush end.");
}
} // namespace

void mrFlushWorkspaceAutosaveIfDue(std::uint64_t *nextWakeupMs) {
	flushWorkspaceAutosave(false, nextWakeupMs);
}

void mrFlushWorkspaceAutosaveNow() {
	flushWorkspaceAutosave(true);
}

bool mrWorkspaceRestoreInProgress() {
	return applicationUiInt(mrvmRuntimeKv(), kWorkspaceBranch, "restoreInProgress", 0) != 0;
}
