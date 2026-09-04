#define Uses_TRect
#include <tvision/tv.h>

#include "MRWindowCommands.hpp"
#include "MRWindowCommandsInternal.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <string>

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
	const std::string directory = effectiveRememberedLoadDirectory(MRDialogHistoryScope::WorkspaceSave);
	if (directory.empty()) return false;
	path = (std::filesystem::path(directory) / (std::string("Autosave ") + dateTime + ".mrmac")).string();
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
void flushWorkspaceAutosave(bool force) {
	const auto startedAt = std::chrono::steady_clock::now();
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	std::string errorText;
	std::string autosavePath;
	MRSettingsWriteReport report;
	long long persistUs = 0;

	if (applicationUiInt(runtimeKv, kWorkspaceBranch, "autosaveDirty", 0) == 0) return;
	if (!configuredAutosaveWorkspace()) return;
	if (runtimePreserveAutosavedWorkspace()) return;
	if (!force && steadyClockMilliseconds(std::chrono::steady_clock::now()) < applicationUiUnsigned(runtimeKv, kWorkspaceBranch, "autosaveDueMs", 0)) return;
	mrLogMessage(std::string("Workspace autosave flush begin force=") + (force ? "1" : "0") + ".");
	mrLogMessage(std::string("Workspace autosave dirty true->false source=flush force=") + (force ? "1" : "0") + ".");
	storeApplicationUiInt(runtimeKv, kWorkspaceBranch, "autosaveDirty", 0);
	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		if (!autosaveWorkspacePath(std::time(nullptr), autosavePath) || !mrSaveWorkspace(autosavePath)) {
			storeApplicationUiInt(runtimeKv, kWorkspaceBranch, "autosaveDirty", 1);
			storeApplicationUiUnsigned(runtimeKv, kWorkspaceBranch, "autosaveDueMs", steadyClockMilliseconds(std::chrono::steady_clock::now() + kWorkspaceAutosaveDelay));
			mrLogMessage("Workspace autosave named serialization failed.");
			return;
		}
		rememberLoadDialogPath(MRDialogHistoryScope::WorkspaceSave, autosavePath.c_str());
		rememberLoadDialogPath(MRDialogHistoryScope::WorkspaceLoad, autosavePath.c_str());
		if (!persistConfiguredSettingsSnapshotWithWorkspace(&errorText, &report)) {
			storeApplicationUiInt(runtimeKv, kWorkspaceBranch, "autosaveDirty", 1);
			storeApplicationUiUnsigned(runtimeKv, kWorkspaceBranch, "autosaveDueMs", steadyClockMilliseconds(std::chrono::steady_clock::now() + kWorkspaceAutosaveDelay));
			mrLogMessage("Workspace autosave dirty false->true source=flush-failed.");
			if (!errorText.empty()) mrLogMessage("Workspace autosave failed: " + errorText);
			return;
		}
		persistUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	mrLogSettingsWriteReport("workspace autosave", report);
	logWindowTiming("Workspace autosave flush timing", std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count(), "persist_us=" + std::to_string(persistUs));
	mrLogMessage("Workspace autosave flush end.");
}
} // namespace

void mrFlushWorkspaceAutosaveIfDue() {
	flushWorkspaceAutosave(false);
}

void mrFlushWorkspaceAutosaveNow() {
	flushWorkspaceAutosave(true);
}

bool mrWorkspaceRestoreInProgress() {
	return applicationUiInt(mrvmRuntimeKv(), kWorkspaceBranch, "restoreInProgress", 0) != 0;
}
