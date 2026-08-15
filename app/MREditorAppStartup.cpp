#include "utils/MRFileIOUtils.hpp"
#define Uses_TKeys
#define Uses_MsgBox
#define Uses_TDialog
#define Uses_TStaticText
#define Uses_TFileDialog
#define Uses_TButton
#define Uses_TObject
#define Uses_TApplication
#define Uses_TEvent
#define Uses_TRect
#define Uses_TView
#define Uses_TDrawBuffer
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_TDeskTop
#define Uses_TScreen
#include <tvision/tv.h>

#include "MREditorApp.hpp"

#include "../coprocessor/MRCoprocessor.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/MRVM.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../coprocessor/MRCoprocessorDispatch.hpp"
#include "../config/settings/MRSettingsAssignments.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsRuntimeState.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "../dialogs/MRDirtyGating.hpp"
#include "../dialogs/setup/MRSetupCommon.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../ui/MRDeskTop.hpp"
#include "../ui/MRBentoHexEditor/MRBentoHexEditor.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRFileEditor/MRFileEditor.hpp"
#include "../ui/MRMenuBar.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRSidekickEditor.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/MRWindowLayout.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "MRAppState.hpp"
#include "MRCommandRouter.hpp"
#include "MRFunctionKeyBindings.hpp"
#include "MRMenuFactory.hpp"
#include "MRUpdate.hpp"
#include "MRPrivilegedFileBroker.hpp"
#include "MRRuntimeScheduler.hpp"
#include <ctime>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fnmatch.h>
#include <glob.h>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace {
[[nodiscard]] bool hasVmErrorLineSince(const std::vector<std::string> &lines, std::size_t start, std::string &outError) {
	static constexpr std::string_view prefix = "VM Error:";
	for (std::size_t i = start; i < lines.size(); ++i)
		if (lines[i].compare(0, prefix.size(), prefix.data(), prefix.size()) == 0) {
			outError = lines[i];
			return true;
		}
	return false;
}

class StartupSettingsModeGuard {
  public:
	StartupSettingsModeGuard() noexcept : previous(mrvmIsStartupSettingsMode()) {
		mrvmSetStartupSettingsMode(true);
	}

	~StartupSettingsModeGuard() {
		mrvmSetStartupSettingsMode(previous);
	}

  private:
	bool previous;
};

// This is the final startup apply path. The verified, canonicalized and compiled
// settings.mrmac source is executed by the VM in startup mode, and the in-memory
// settings state becomes authoritative only after this step succeeds.
bool applySettingsSourceViaVm(const std::string &settingsPath, const std::string &source, std::string *errorMessage) {
	size_t bytecodeSize = 0;
	unsigned char *bytecode = nullptr;
	int macroCount = 0;
	VirtualMachine vm;
	MRSetupPaths resetPaths;
	std::string normalizedSettingsPath = normalizeConfiguredPathInput(settingsPath);
	std::string compileError;
	std::string vmError;

	if (!resetConfiguredSettingsModel(normalizedSettingsPath, resetPaths, &vmError)) {
		if (errorMessage != nullptr) *errorMessage = "Settings VM preload reset failed: " + vmError;
		return false;
	}
	bytecode = compile_macro_code(source.c_str(), &bytecodeSize);
	if (bytecode == nullptr) {
		const char *err = get_last_compile_error();
		compileError = (err != nullptr && *err != '\0') ? err : "Compilation failed.";
		if (errorMessage != nullptr) *errorMessage = "Settings load failed (compile): " + compileError;
		return false;
	}
	macroCount = get_compiled_macro_count();
	if (macroCount <= 0) {
		std::free(bytecode);
		if (errorMessage != nullptr) *errorMessage = "Settings load failed: no macros found.";
		return false;
	}
	{
		StartupSettingsModeGuard startupSettingsMode;
		for (int i = 0; i < macroCount; ++i) {
			int entry = get_compiled_macro_entry(i);
			const char *macroName = get_compiled_macro_name(i);
			std::size_t logStart = vm.log.size();

			if (entry < 0 || static_cast<size_t>(entry) >= bytecodeSize) {
				std::free(bytecode);
				if (errorMessage != nullptr) *errorMessage = "Settings load failed: invalid macro entry.";
				return false;
			}
			vm.executeAt(bytecode, bytecodeSize, static_cast<size_t>(entry), std::string(), macroName != nullptr ? macroName : std::string(), i == 0, true);
			if (hasVmErrorLineSince(vm.log, logStart, vmError)) {
				std::free(bytecode);
				if (errorMessage != nullptr) *errorMessage = "Settings load failed (runtime): " + vmError;
				return false;
			}
			if (!mrvmFlushPendingStartupKeymapBatch(&vmError)) {
				std::free(bytecode);
				if (errorMessage != nullptr) *errorMessage = "Settings load failed (keymap batch): " + (vmError.empty() ? std::string("invalid keymap batch.") : vmError);
				return false;
			}
		}
	}
	std::free(bytecode);
	clearConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

ushort mrEditorDialog(int dialog, ...) {
	va_list arg;
	ushort result = cmCancel;

	switch (dialog) {
		case edOutOfMemory:
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Out of memory.", mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			return cmOK;
		case edReadError: {
			const char *path = nullptr;
			va_start(arg, dialog);
			path = va_arg(arg, const char *);
			va_end(arg);
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string("Error reading file: ") + ((path != nullptr && *path != '\0') ? path : "<unknown>"), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			return cmOK;
		}
		case edWriteError: {
			const char *path = nullptr;
			va_start(arg, dialog);
			path = va_arg(arg, const char *);
			va_end(arg);
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string("Error writing file: ") + ((path != nullptr && *path != '\0') ? path : "<unknown>"), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			return cmOK;
		}
		case edCreateError: {
			const char *path = nullptr;
			va_start(arg, dialog);
			path = va_arg(arg, const char *);
			va_end(arg);
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string("Error creating file: ") + ((path != nullptr && *path != '\0') ? path : "<unknown>"), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			return cmOK;
		}
		case edSaveModify: {
			const char *path = nullptr;
			va_start(arg, dialog);
			path = va_arg(arg, const char *);
			va_end(arg);
			return messageBox(mfInformation | mfYesNoCancel, "File modified. Save changes to:\n%s", (path != nullptr && *path != '\0') ? path : "<unnamed>");
		}
		case edSaveUntitled:
			return messageBox(mfInformation | mfYesNoCancel, "Save untitled file?");
		case edSaveAs: {
			char *target = nullptr;
			va_start(arg, dialog);
			target = va_arg(arg, char *);
			va_end(arg);
			if (target == nullptr) return cmCancel;
			std::string suggestedTarget(target);
			mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::EditorSaveAs, target, MAXPATH, "*.*");
			mr::dialogs::suggestFileDialogName(target, MAXPATH, suggestedTarget);
			result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::EditorSaveAs, "*.*", "SAVE FILE AS", "~N~ame", fdOKButton, target);
			return result;
		}
		default:
			return cmCancel;
	}
}

// This function orchestrates bootstrap staging, canonicalization and the final
// VM apply. The runtime settings state is authoritative only after the final
// applySettingsSourceViaVm call completes successfully.
bool loadStartupSettingsMacro(const std::string &overridePath, std::string *errorMessage) {
	std::string settingsPath = overridePath.empty() ? defaultSettingsMacroFilePath() : overridePath;
	std::string source;
	MRSettingsLoadReport report;
	std::string canonicalSource;
	const auto settingsStartedAt = std::chrono::steady_clock::now();
	auto phaseStartedAt = settingsStartedAt;
	auto logSettingsBootstrapPhase = [&phaseStartedAt](const char *phase) {
		const auto now = std::chrono::steady_clock::now();
		std::ostringstream line;

		line << "Bootstrap settings phase " << phase << " took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(now - phaseStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
		phaseStartedAt = now;
	};

	if (settingsPath.empty()) {
		if (errorMessage != nullptr) *errorMessage = "Settings path is empty.";
		return false;
	}
	if (!ensureSettingsMacroFileExists(settingsPath, errorMessage)) {
		logSettingsBootstrapPhase("ensure_file");
		mrLogMessage(errorMessage != nullptr ? errorMessage->c_str() : "Settings bootstrap failed (create defaults).");
		return false;
	}
	logSettingsBootstrapPhase("ensure_file");
	if (!readTextFile(settingsPath, source)) {
		source.clear();
	}
	logSettingsBootstrapPhase("read_file");
	if (!prepareStartupSettingsSource(settingsPath, source, &report, canonicalSource, errorMessage)) {
		logSettingsBootstrapPhase("prepare_canonical_source");
		mrLogMessage(errorMessage != nullptr ? errorMessage->c_str() : "Settings canonicalization failed.");
		return false;
	}
	logSettingsBootstrapPhase("prepare_canonical_source");
	if (!applySettingsSourceViaVm(settingsPath, canonicalSource, errorMessage)) {
		logSettingsBootstrapPhase("vm_apply");
		mrLogMessage((errorMessage != nullptr && !errorMessage->empty()) ? errorMessage->c_str() : "Settings VM apply failed.");
		return false;
	}
	logSettingsBootstrapPhase("vm_apply");

	mrLogMessage(("Bootstrap settings loaded path=" + settingsPath + " macropath=" + defaultMacroDirectoryPath()).c_str());
	logSettingsBootstrapPhase("post_apply_log");
	{
		std::ostringstream line;
		line << "Bootstrap settings total took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - settingsStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

struct StartupLoadRequest {
	bool recursive;
	std::vector<std::string> specs;

	StartupLoadRequest() : recursive(false), specs() {
	}
};

struct StartupAutomationRequest {
	std::vector<std::string> macroFiles;
	bool exitAfterRunMacro;

	StartupAutomationRequest() : macroFiles(), exitAfterRunMacro(false) {
	}
};

bool parseRunMacroOptionValue(const std::string &arg, std::string &value) {
	static const std::string prefix = "--run-macro=";

	if (arg.rfind(prefix, 0) != 0) return false;
	value = arg.substr(prefix.size());
	return true;
}

bool hasGlobWildcard(std::string_view pathSpec) {
	return pathSpec.find_first_of("*?[") != std::string::npos;
}

bool isReadableRegularFile(const std::filesystem::path &path) {
	std::error_code ec;
	std::string pathString = path.string();

	if (pathString.empty()) return false;
	if (std::filesystem::is_regular_file(path, ec) && !ec && ::access(pathString.c_str(), R_OK) == 0) return true;
	return mrPrivilegedFileBrokerAllowsPath(pathString);
}

std::string normalizePathForLoad(const std::filesystem::path &path) {
	std::error_code ec;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);

	if (ec || normalized.empty()) normalized = path.lexically_normal();
	return normalized.string();
}

void appendUniqueFilePath(const std::filesystem::path &path, std::vector<std::string> &paths, std::set<std::string> &seen) {
	std::string normalized;

	if (!isReadableRegularFile(path)) return;
	normalized = normalizePathForLoad(path);
	if (normalized.empty()) return;
	if (seen.insert(normalized).second) paths.push_back(normalized);
}

void appendGlobMatchesFlat(const std::string &pattern, std::vector<std::string> &paths, std::set<std::string> &seen) {
	glob_t globResult{};
	int result = ::glob(pattern.c_str(), 0, nullptr, &globResult);

	if (result == 0) {
		for (std::size_t i = 0; i < globResult.gl_pathc; ++i)
			appendUniqueFilePath(std::filesystem::path(globResult.gl_pathv[i]), paths, seen);
	} else if (result != GLOB_NOMATCH) {
		std::string line = "Startup glob failed for pattern: ";
		line += pattern;
		mrLogMessage(line.c_str());
	}
	globfree(&globResult);
}

std::filesystem::path recursiveRootForPattern(const std::string &pattern) {
	std::size_t wildcardPos = pattern.find_first_of("*?[");
	std::size_t slashPos;

	if (wildcardPos == std::string::npos) return std::filesystem::path(pattern);
	slashPos = pattern.rfind('/', wildcardPos);
	if (slashPos == std::string::npos) return std::filesystem::path(".");
	if (slashPos == 0) return std::filesystem::path("/");
	return std::filesystem::path(pattern.substr(0, slashPos));
}

void appendRecursiveGlobMatches(const std::string &pattern, std::vector<std::string> &paths, std::set<std::string> &seen) {
	std::filesystem::path rootPath = recursiveRootForPattern(pattern);
	std::size_t wildcardPos = pattern.find_first_of("*?[");
	std::size_t rootSlashPos = pattern.rfind('/', wildcardPos);
	std::string patternSuffix = rootSlashPos == std::string::npos ? pattern : pattern.substr(rootSlashPos + 1);
	const bool matchBaseName = patternSuffix.find('/') == std::string::npos;
	std::error_code ec;
	auto matchesPattern = [&](const std::filesystem::path &candidatePath, const std::filesystem::path &basePath) -> bool {
		std::string candidate;
		if (matchBaseName) candidate = candidatePath.filename().string();
		else {
			std::error_code relEc;
			std::filesystem::path relativePath = std::filesystem::relative(candidatePath, basePath, relEc);
			candidate = relEc ? candidatePath.lexically_normal().string() : relativePath.lexically_normal().string();
		}
		if (candidate.empty()) return false;
		return fnmatch(patternSuffix.c_str(), candidate.c_str(), 0) == 0;
	};

	if (rootPath.empty()) rootPath = ".";
	if (!std::filesystem::exists(rootPath, ec) || ec) return;
	if (!std::filesystem::is_directory(rootPath, ec) || ec) {
		if (matchesPattern(rootPath, rootPath.parent_path())) appendUniqueFilePath(rootPath, paths, seen);
		return;
	}

	std::filesystem::recursive_directory_iterator it(rootPath, std::filesystem::directory_options::skip_permission_denied, ec);
	std::filesystem::recursive_directory_iterator end;
	for (; !ec && it != end; it.increment(ec)) {
		if (!it->is_regular_file(ec) || ec) {
			ec.clear();
			continue;
		}
		std::filesystem::path candidatePath = it->path().lexically_normal();
		if (matchesPattern(candidatePath, rootPath)) appendUniqueFilePath(candidatePath, paths, seen);
	}
}

void appendRecursivePathFiles(const std::filesystem::path &path, std::vector<std::string> &paths, std::set<std::string> &seen) {
	std::error_code ec;

	if (isReadableRegularFile(path)) {
		appendUniqueFilePath(path, paths, seen);
		return;
	}
	if (!std::filesystem::is_directory(path, ec) || ec) return;
	std::filesystem::recursive_directory_iterator it(path, std::filesystem::directory_options::skip_permission_denied, ec);
	std::filesystem::recursive_directory_iterator end;
	for (; !ec && it != end; it.increment(ec)) {
		if (!it->is_regular_file(ec) || ec) {
			ec.clear();
			continue;
		}
		appendUniqueFilePath(it->path(), paths, seen);
	}
}

StartupLoadRequest parseStartupLoadRequest() {
	StartupLoadRequest request;
	std::vector<std::string> args = mrvmProcessArguments();
	bool skipNext = false;

	for (const std::string &arg : args) {
		std::string ignored;
		if (skipNext) {
			skipNext = false;
			continue;
		}
		if (arg == "--load-recursive" || arg == "-lr") {
			request.recursive = true;
			continue;
		}
		if (arg == "--run-macro" || arg == "-rm") {
			skipNext = true;
			continue;
		}
		if (parseRunMacroOptionValue(arg, ignored)) continue;
		if (arg == "--exit-after-run-macro" || arg == "--internal-reload-workspace-after-update") continue;
		if (!arg.empty()) request.specs.push_back(arg);
	}
	return request;
}

StartupAutomationRequest parseStartupAutomationRequest() {
	StartupAutomationRequest request;
	std::vector<std::string> args = mrvmProcessArguments();
	bool expectRunMacroPath = false;

	for (const std::string &arg : args) {
		std::string value;

		if (expectRunMacroPath) {
			if (!arg.empty()) request.macroFiles.push_back(arg);
			expectRunMacroPath = false;
			continue;
		}
		if (arg == "--run-macro" || arg == "-rm") {
			expectRunMacroPath = true;
			continue;
		}
		if (parseRunMacroOptionValue(arg, value)) {
			if (!value.empty()) request.macroFiles.push_back(value);
			continue;
		}
		if (arg == "--exit-after-run-macro") {
			request.exitAfterRunMacro = true;
			continue;
		}
	}
	if (expectRunMacroPath) mrLogMessage("Startup automation ignored --run-macro/-rm without a macro file.");
	return request;
}

bool runStartupAutomationFromCommandLine() {
	StartupAutomationRequest request = parseStartupAutomationRequest();

	for (const std::string &macroFile : request.macroFiles) {
		std::string errorText;
		if (runMacroFileByPathOnUiThread(macroFile.c_str(), &errorText, false)) {
			mrLogMessage(("Startup automation ran macro: " + macroFile).c_str());
			continue;
		}
		if (errorText.empty()) errorText = "Macro execution failed.";
		mrLogMessage(("Startup automation macro failed: " + macroFile + ": " + errorText).c_str());
		mr::messageline::postAutoTimed(mr::messageline::Owner::MacroMessage, "startup macro failed: " + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
	}
	return request.exitAfterRunMacro && !request.macroFiles.empty();
}

std::vector<std::string> collectStartupFilesFromRequest(const StartupLoadRequest &request) {
	std::vector<std::string> paths;
	std::set<std::string> seen;

	for (const std::string &specRaw : request.specs) {
		std::string spec = expandUserPath(specRaw);
		std::filesystem::path specPath(spec);

		if (spec.empty()) continue;
		if (request.recursive) {
			if (hasGlobWildcard(spec)) appendRecursiveGlobMatches(spec, paths, seen);
			else
				appendRecursivePathFiles(specPath, paths, seen);
			continue;
		}
		if (hasGlobWildcard(spec)) {
			appendGlobMatchesFlat(spec, paths, seen);
			continue;
		}
		appendUniqueFilePath(specPath, paths, seen);
	}
	return paths;
}

std::vector<std::string> loadStartupFilesFromCommandLine(const StartupLoadRequest &request, const std::vector<std::string> &files, bool focusRestoredWorkspaceFiles) {
	std::vector<std::string> loadedFiles;
	std::vector<MREditWindow *> restoredWindows;
	std::size_t loadedCount = 0;
	MREditWindow *lastStartupWindow = nullptr;
	MRWindowOpenBatch openBatch;

	if (request.specs.empty()) return loadedFiles;
	if (files.empty()) {
		mrLogMessage("No readable startup files matched command-line arguments.");
		return loadedFiles;
	}
	if (focusRestoredWorkspaceFiles) restoredWindows = allEditWindowsInZOrder();
	if (files.size() > 1) openBatch.begin();
	for (const std::string &file : files) {
		bool restoredFileFound = false;

		if (focusRestoredWorkspaceFiles) {
			for (MREditWindow *candidate : restoredWindows) {
				MRFileEditor *candidateEditor = candidate != nullptr ? candidate->getEditor() : nullptr;
				const char *candidatePath = candidateEditor != nullptr ? candidateEditor->persistentFileName() : nullptr;

				if (candidatePath == nullptr || *candidatePath == '\0') continue;
				if (normalizePathForLoad(std::filesystem::path(candidatePath)) != file) continue;
				lastStartupWindow = candidate;
				restoredFileFound = true;
				mrLogMessage(("Startup file resolved to restored workspace window: " + file).c_str());
				break;
			}
		}
		if (restoredFileFound) {
			loadedFiles.push_back(file);
			continue;
		}
		const bool useHexEditor = configuredAutoDetectBinaryFiles() && fileContainsNulInBoundarySamples(file);
		MREditWindow *win = nullptr;

		if (useHexEditor) {
			win = openBatch.active() ? static_cast<MREditWindow *>(openBatch.createHexEditorWindow(file.c_str())) : static_cast<MREditWindow *>(createHexEditorWindow(file.c_str()));
		} else {
			win = openBatch.active() ? openBatch.createEditorWindow(file.c_str()) : createEditorWindow(file.c_str());
		}
		if (win == nullptr) {
			mrLogMessage("Startup load aborted: unable to create editor window.");
			break;
		}
		if (!loadResolvedFileIntoWindow(win, file, "Startup load")) {
			message(win, evCommand, cmClose, nullptr);
			continue;
		}
		lastStartupWindow = win;
		loadedFiles.push_back(file);
		++loadedCount;
	}
	if (openBatch.active()) openBatch.finish(true, loadedCount != 0);
	if (lastStartupWindow != nullptr) static_cast<void>(mrActivateEditWindow(lastStartupWindow));
	return loadedFiles;
}

} // namespace
MREditorApp::MREditorApp() : TProgInit(&MREditorApp::initMRStatusLine, &MREditorApp::initMRMenuBar, &MREditorApp::initMRDeskTop), exitPrepared(false), restartAfterExit(false), updateCheckStarted(false), keystrokeRecording(false), recordingMarkerVisible(false), macroBrainMarkerVisible(false), recordedMacroCounter(0), recordingBlinkToggleAt(std::chrono::steady_clock::now() + recordingBlinkInterval), macroBrainBlinkToggleAt(std::chrono::steady_clock::now() + recordingBlinkInterval), performancePanelVisible(false), performancePanel(nullptr), fullscreenHint(nullptr), performancePanelRefreshAt(std::chrono::steady_clock::now()), fullscreenHintVisibleUntil(std::chrono::steady_clock::time_point::min()), startupQuitPending(false), fullscreenPresentationActive(false), fullscreenMenuBarTransientVisible(false), fullscreenWindow(nullptr), fullscreenRestoreBounds(0, 0, 0, 0), interactiveMouseCaptureDepth(0), cursorPositionMarkerFormat("R:C"), persistentBlocksMenuEnabled(false), menulineMessagesEnabled(true), snippetSidekickHintsActive(false), functionKeyModifiers(0), virtualDesktopCount(1), cyclicVirtualDesktopsEnabled(false) {
	const auto startupStartedAt = std::chrono::steady_clock::now();
	auto phaseStartedAt = startupStartedAt;
	auto logStartupPhase = [&phaseStartedAt](const char *phase) {
		const auto now = std::chrono::steady_clock::now();
		std::ostringstream line;

		line << "Bootstrap phase " << phase << " took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(now - phaseStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
		phaseStartedAt = now;
	};
	TEditor::editorDialog = mrEditorDialog;
	mr::coprocessor::globalCoprocessor().setResultHandler(handleCoprocessorResult);
	initializePerformancePanel();
	initializeFullscreenHint();
	loadStartupSettingsMacro(std::string(), nullptr);
	refreshConfiguredUiSettingsSnapshot();
	redraw();
	logStartupPhase("settings_bootstrap");
	logStartupPhase("runtime_scheduler");
	applyConfiguredDisplayLayout();
	logStartupPhase("display_layout_initial");
	runConfiguredAutoexecMacros();
	logStartupPhase("autoexec_macros");
	const bool autoloadWorkspace = configuredAutoloadWorkspace();
	const bool updateForcesWorkspaceRestore = mrUpdateForcesWorkspaceRestore();
	const StartupLoadRequest startupLoadRequest = parseStartupLoadRequest();
	const std::vector<std::string> requestedStartupFiles = startupLoadRequest.specs.empty() ? std::vector<std::string>() : collectStartupFilesFromRequest(startupLoadRequest);
	const std::vector<std::string> autosavedWorkspaceFiles = !autoloadWorkspace ? mrSettingsFileAutosavedWorkspaceFiles() : std::vector<std::string>();
	bool commandLineForcesWorkspaceRestore = false;

	if (autosavedWorkspaceFiles.size() == 1) {
		const std::string workspaceFile = normalizePathForLoad(std::filesystem::path(autosavedWorkspaceFiles.front()));

		for (const std::string &startupFile : requestedStartupFiles)
			if (startupFile == workspaceFile) {
				commandLineForcesWorkspaceRestore = true;
				break;
			}
	}
	const bool restoreWorkspaceAtStartup = autoloadWorkspace || commandLineForcesWorkspaceRestore || updateForcesWorkspaceRestore;
	if (restoreWorkspaceAtStartup) {
		applyConfiguredDisplayLayout();
		logStartupPhase("display_layout_final");
		mrLoadWorkspace("");
		logStartupPhase("workspace_autoload");
	}
	const std::vector<std::string> startupFiles = loadStartupFilesFromCommandLine(startupLoadRequest, requestedStartupFiles, restoreWorkspaceAtStartup);
	logStartupPhase("startup_files");
	startupQuitPending = runStartupAutomationFromCommandLine();
	logStartupPhase("startup_automation");
	if (!restoreWorkspaceAtStartup) {
		applyConfiguredDisplayLayout();
		logStartupPhase("display_layout_final");
	}
	static_cast<void>(mrEnsureLogWindow(false));
	logStartupPhase("log_window");
	syncRecordingUiState();
	logStartupPhase("recording_ui");
	if (auto *mrMenuBar = dynamic_cast<MRMenuBar *>(menuBar)) {
		mrMenuBar->setPersistentBlocksMenuState(persistentBlocksMenuEnabled);
		if (MREditWindow *win = currentEditWindow(); win != nullptr) {
			mrMenuBar->setInsertModeMenuState(win->insertModeEnabled());
			mrMenuBar->setLineDrawingMenuState(win->lineDrawingEnabled(), win->lineDrawingDoubleLines());
		}
	}
	logStartupPhase("menu_state");

	bool singleFileWorkspaceLoadedFromCommandLine = false;

	if (commandLineForcesWorkspaceRestore) {
		const std::string workspaceFile = normalizePathForLoad(std::filesystem::path(autosavedWorkspaceFiles.front()));

		for (const std::string &startupFile : startupFiles)
			if (startupFile == workspaceFile) {
				singleFileWorkspaceLoadedFromCommandLine = true;
				mrLogMessage("Autosaved single-file workspace satisfied by command-line file: " + workspaceFile);
				break;
			}
	}
	if (!updateForcesWorkspaceRestore && !autosavedWorkspaceFiles.empty() && !singleFileWorkspaceLoadedFromCommandLine) {
		setRuntimePreserveAutosavedWorkspace(true);
		const mr::dialogs::UnsavedChangesChoice choice = mr::dialogs::showWorkspaceLoadDialog("Restore workspace", autosavedWorkspaceFiles, "Discard workspace");

		if (choice == mr::dialogs::UnsavedChangesChoice::Save) {
			setRuntimePreserveAutosavedWorkspace(false);
			mrLoadWorkspace("");
		} else if (choice == mr::dialogs::UnsavedChangesChoice::Discard) {
			setRuntimePreserveAutosavedWorkspace(false);
			static_cast<void>(mrClearAutosavedWorkspace());
		}
	}
	if (!autoloadWorkspace) logStartupPhase("workspace_autoload");
	mrLogMessage("Editor session started.");
	updateAppCommandState(virtualDesktopCount, cyclicVirtualDesktopsEnabled);
	syncFunctionKeyState();
	logStartupPhase("command_state");
	{
		std::ostringstream line;

		line << "Bootstrap total took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startupStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
	}
}
