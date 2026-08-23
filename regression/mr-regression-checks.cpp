#include "../app/MRVersion.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#define Uses_TCheckBoxes
#define Uses_TGroup
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TListViewer
#define Uses_TWindow
#include <tvision/tv.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits.h>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <sys/stat.h>
#include <sched.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "../mrmac/mrmac.h"
#include "../mrmac/MRMacroExecutionSession.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../mrmac/ui/modeless/MRMacroModelessControls.hpp"
#include "../mrmac/ui/modeless/MRMacroModelessUi.hpp"
#include "../mrmac/MRVM.hpp"
#include "../mrmac/ui/modeless/MRVMMacroModelessProcedures.hpp"
#include "../mrmac/ui/modeless/MRVMModelessUiRuntime.hpp"
#include "../mrmac/vm/MRVMExecSessions.hpp"
#include "../mrmac/vm/MRVMRuntimeCatalog.hpp"
#include "../mrmac/vm/MRVMRuntimeDebugger.hpp"
#include "../mrmac/vm/MRVMRuntimeState.hpp"
#include "../mrmac/vm/MRVMValue.hpp"
#include "../app/MREditorApp.hpp"
#include "../app/MRCommandRouter.hpp"
#include "../app/MRRuntimeScheduler.hpp"
#include "../app/MRRuntimeTimerSource.hpp"
#include "../app/MRMenuFactory.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../app/commands/MRExternalCommand.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsRuntimeState.hpp"
#include "../config/settings/MRSettingsEditSetup.hpp"
#include "../config/settings/MRSettingsCompilerProfiles.hpp"
#include "../config/settings/MRSettingsAssignments.hpp"
#include "../config/settings/MRSettingsSnapshotIO.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../dialogs/MRAbout.hpp"
#include "../dialogs/extensions/MRFileExtensionProfileDrafts.hpp"
#include "../dialogs/setup/MRSetup.hpp"
#include "../diff/MRDiff.hpp"
#include "../piecetable/MRTextDocument.hpp"
#include "../ui/MRBentoBox/MRBentoBox.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRFileEditor/MRFEBlockOps.hpp"
#include "../ui/MRDeskTop.hpp"
#include "../ui/MRIndicator.hpp"
#include "../ui/MRMenuBar.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRStatusLine.hpp"
#include "../ui/MRSidekickEditor.hpp"
#include "../ui/MRDesktopWindow.hpp"
#include "../ui/MRWindowSupport.hpp"

int runMacroDebuggerCrossSectionProbeMode();

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

namespace {

struct TestContext {
	int passed;
	int failed;

	TestContext() : passed(0), failed(0) {
	}
};

class ScopedRegressionConfigHome {
  public:
	ScopedRegressionConfigHome() : mPreviousValue(), mPath("/tmp/mr_regression_xdg_config_" + std::to_string(static_cast<long>(::getpid()))), mHadPrevious(false), mReady(false) {
		const char *previous = std::getenv("XDG_CONFIG_HOME");
		std::error_code error;

		if (previous != nullptr) {
			mPreviousValue = previous;
			mHadPrevious = true;
		}
		std::filesystem::remove_all(mPath, error);
		error.clear();
		std::filesystem::create_directories(mPath + "/mr/macros", error);
		if (error || ::setenv("XDG_CONFIG_HOME", mPath.c_str(), 1) != 0) return;
		mReady = true;
	}

	~ScopedRegressionConfigHome() {
		std::error_code error;

		if (mHadPrevious)
			::setenv("XDG_CONFIG_HOME", mPreviousValue.c_str(), 1);
		else
			::unsetenv("XDG_CONFIG_HOME");
		if (mReady) std::filesystem::remove_all(mPath, error);
	}

	bool ready() const noexcept {
		return mReady;
	}

  private:
	std::string mPreviousValue;
	std::string mPath;
	bool mHadPrevious;
	bool mReady;
};

bool runKeymapMacroBindingDispatchProbe(std::string &failureReason);

void collectRegressionSourceMapEntry(void *context, const MRMacSourceMapEntry *entry) {
	std::vector<MRMacroSourceMapEntry> *entries = static_cast<std::vector<MRMacroSourceMapEntry> *>(context);
	MRMacroSourceMapEntry mapped;

	if (entries == nullptr || entry == nullptr) return;
	mapped.bytecodeOffset = entry->bytecodeOffset;
	mapped.sourceStartOffset = entry->sourceStartOffset;
	mapped.sourceEndOffset = entry->sourceEndOffset;
	mapped.line = entry->line;
	mapped.column = entry->column;
	mapped.macroName = entry->macroName != nullptr ? entry->macroName : std::string();
	mapped.debuggableKind = entry->debuggableKind;
	entries->push_back(mapped);
}

bool compileSource(const std::string &source, std::vector<unsigned char> &bytecode, int &entryOffset, std::string &entryName, std::string &errorText) {
	unsigned char *compiled = nullptr;
	size_t bytecodeSize = 0;
	int macroCount = 0;
	const char *name = nullptr;

	compiled = compile_macro_code(source.c_str(), &bytecodeSize);
	if (compiled == nullptr) {
		const char *err = get_last_compile_error();
		errorText = (err != nullptr && *err != '\0') ? err : "Compilation failed.";
		return false;
	}

	bytecode.assign(compiled, compiled + bytecodeSize);
	std::free(compiled);

	macroCount = get_compiled_macro_count();
	if (macroCount <= 0) {
		errorText = "No macro entries were compiled.";
		return false;
	}

	entryOffset = get_compiled_macro_entry(0);
	if (entryOffset < 0 || static_cast<size_t>(entryOffset) >= bytecode.size()) {
		errorText = "Invalid macro entry offset.";
		return false;
	}

	name = get_compiled_macro_name(0);
	entryName = name != nullptr ? name : std::string();
	errorText.clear();
	return true;
}

bool firstVmError(const std::vector<std::string> &logLines, std::string &outErrorLine) {
	for (std::size_t i = 0; i < logLines.size(); ++i)
		if (logLines[i].rfind("VM Error:", 0) == 0) {
			outErrorLine = logLines[i];
			return true;
		}
	outErrorLine.clear();
	return false;
}

bool expectCompileError(const std::string &source, const std::string &expectedPart, std::string &failureReason) {
	size_t bytecodeSize = 0;
	unsigned char *bytecode = compile_macro_code(source.c_str(), &bytecodeSize);
	const char *errorText = get_last_compile_error();

	if (bytecode != NULL) {
		std::free(bytecode);
		failureReason = "Expected compile error but compilation succeeded.";
		return false;
	}
	if (errorText == NULL || *errorText == '\0') {
		failureReason = "Expected compile error text, but it is empty.";
		return false;
	}
	if (!expectedPart.empty() && std::strstr(errorText, expectedPart.c_str()) == NULL) {
		failureReason = std::string("Compile error text mismatch: ") + errorText;
		return false;
	}
	failureReason.clear();
	return true;
}

bool containsText(const std::vector<std::string> &values, const char *needle) {
	return std::find(values.begin(), values.end(), std::string(needle)) != values.end();
}

std::size_t countSubstring(const std::string &text, const std::string &needle) {
	std::size_t count = 0;
	std::size_t pos = 0;
	if (needle.empty()) return 0;
	while ((pos = text.find(needle, pos)) != std::string::npos) {
		++count;
		pos += needle.size();
	}
	return count;
}

bool containsAllSubstrings(const std::string &text, std::initializer_list<const char *> needles, std::string &missingNeedle) {
	for (const char *needle : needles)
		if (text.find(needle) == std::string::npos) {
			missingNeedle = needle;
			return false;
		}
	missingNeedle.clear();
	return true;
}

bool validateDiffReconstructsRight(const std::vector<std::string> &leftLines, const std::vector<std::string> &rightLines, const std::vector<mr::diff::MRDiffHunk> &hunks, std::size_t *deleteCount, std::size_t *insertCount, std::size_t *equalCount, std::string &failureReason) {
	std::vector<std::string> reconstructed;
	std::size_t leftPos = 0;
	std::size_t rightPos = 0;
	std::size_t localDeleteCount = 0;
	std::size_t localInsertCount = 0;
	std::size_t localEqualCount = 0;

	for (std::size_t hunkIndex = 0; hunkIndex < hunks.size(); ++hunkIndex) {
		const mr::diff::MRDiffHunk &hunk = hunks[hunkIndex];
		if (hunk.leftStart != leftPos || hunk.rightStart != rightPos) {
			failureReason = "Diff hunk " + std::to_string(hunkIndex) + " is not contiguous.";
			return false;
		}

		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
				if (leftPos + hunk.count > leftLines.size() || rightPos + hunk.count > rightLines.size()) {
					failureReason = "Equal diff hunk exceeds input bounds.";
					return false;
				}
				for (std::size_t i = 0; i < hunk.count; ++i) {
					if (leftLines[leftPos + i] != rightLines[rightPos + i]) {
						failureReason = "Equal diff hunk contains different lines.";
						return false;
					}
					reconstructed.push_back(leftLines[leftPos + i]);
				}
				leftPos += hunk.count;
				rightPos += hunk.count;
				localEqualCount += hunk.count;
				break;
			case mr::diff::MRDiffOp::Delete:
				if (leftPos + hunk.count > leftLines.size()) {
					failureReason = "Delete diff hunk exceeds left input bounds.";
					return false;
				}
				leftPos += hunk.count;
				localDeleteCount += hunk.count;
				break;
			case mr::diff::MRDiffOp::Insert:
				if (rightPos + hunk.count > rightLines.size()) {
					failureReason = "Insert diff hunk exceeds right input bounds.";
					return false;
				}
				for (std::size_t i = 0; i < hunk.count; ++i)
					reconstructed.push_back(rightLines[rightPos + i]);
				rightPos += hunk.count;
				localInsertCount += hunk.count;
				break;
			default:
				failureReason = "Unknown diff hunk operation.";
				return false;
		}
	}

	if (leftPos != leftLines.size() || rightPos != rightLines.size()) {
		failureReason = "Diff hunks did not consume both inputs.";
		return false;
	}
	if (reconstructed != rightLines) {
		failureReason = "Diff hunks do not reconstruct the right input.";
		return false;
	}

	if (deleteCount != nullptr) *deleteCount = localDeleteCount;
	if (insertCount != nullptr) *insertCount = localInsertCount;
	if (equalCount != nullptr) *equalCount = localEqualCount;
	failureReason.clear();
	return true;
}

bool checkGlobalInt(const std::map<std::string, int> &ints, const char *name, int expected, std::string &failureReason) {
	std::map<std::string, int>::const_iterator it = ints.find(name);
	if (it == ints.end()) {
		failureReason = std::string("Missing global ") + name + ".";
		return false;
	}
	if (it->second != expected) {
		failureReason = std::string("Global ") + name + " mismatch: expected " + std::to_string(expected) + ", got " + std::to_string(it->second) + ".";
		return false;
	}
	return true;
}

bool compileBytecode(const std::string &source, std::vector<unsigned char> &bytecode, std::string &errorReason) {
	size_t bytecodeSize = 0;
	unsigned char *compiled = compile_macro_code(source.c_str(), &bytecodeSize);

	if (compiled == NULL) {
		const char *errorText = get_last_compile_error();
		errorReason = (errorText != NULL && *errorText != '\0') ? errorText : "unknown";
		return false;
	}
	bytecode.assign(compiled, compiled + bytecodeSize);
	std::free(compiled);
	errorReason.clear();
	return true;
}

std::string absolutePathFromCwd(const char *relativePath) {
	char cwd[PATH_MAX];
	std::string out;

	if (relativePath == NULL || *relativePath == '\0') return std::string();
	if (getcwd(cwd, sizeof(cwd)) == NULL) return std::string(relativePath);
	out = cwd;
	if (!out.empty() && out.back() != '/') out.push_back('/');
	out += relativePath;
	return out;
}

struct RuntimeSettingsSnapshot {
	std::string settingsMacroFilePath;
	std::string macroDirectoryPath;
	std::string helpFilePath;
	std::string tempDirectoryPath;
	std::string shellExecutablePath;
	std::string colorThemeFilePath;
	MRColorOutputMode colorOutputMode = MRColorOutputMode::RgbAutomatic;
	bool autoDetectBinaryFiles = true;
	std::vector<std::string> autoexecMacros;
	MREditSetupSettings editSettings;
	std::vector<MREditExtensionProfile> editExtensionProfiles;
	MRColorSetupSettings colorSettings;
};

enum : std::size_t {
	kMenuDialogIndexListboxSelector = 11,
	kMenuDialogIndexInactiveControls = 12,
	kMenuDialogIndexInactiveElements = 13,
	kMenuDialogIndexDialogFrame = 14,
	kMenuDialogIndexDialogText = 15,
	kMenuDialogIndexDialogBackground = 16,
	kMenuDialogIndexButtonDefault = 19,
	kMenuDialogIndexButtonSelected = 20,
	kMenuDialogIndexButtonDisabled = 21,
	kMenuDialogIndexInputLineNormal = 22,
	kMenuDialogIndexInputLineSelected = 23,
	kMenuDialogIndexInputLineArrows = 24,
	kMenuDialogIndexHistoryArrow = 25,
	kMenuDialogIndexHistorySides = 26,
	kMenuDialogIndexMenuBarHotkey = 27
};

enum : unsigned char {
	kPaletteDialogInactiveControlsGray = 62,
	kPaletteDialogInactiveControlsBlue = 94,
	kPaletteDialogInactiveControlsCyan = 126
};

RuntimeSettingsSnapshot captureRuntimeSettingsSnapshot() {
	RuntimeSettingsSnapshot snapshot;

	snapshot.settingsMacroFilePath = configuredSettingsMacroFilePath();
	snapshot.macroDirectoryPath = defaultMacroDirectoryPath();
	snapshot.helpFilePath = configuredHelpFilePath();
	snapshot.tempDirectoryPath = configuredTempDirectoryPath();
	snapshot.shellExecutablePath = configuredShellExecutablePath();
	snapshot.colorThemeFilePath = configuredColorThemeFilePath();
	snapshot.colorOutputMode = configuredColorOutputMode();
	snapshot.autoDetectBinaryFiles = configuredAutoDetectBinaryFiles();
	configuredAutoexecMacroEntries(snapshot.autoexecMacros);
	snapshot.editSettings = configuredEditSetupSettings();
	snapshot.editExtensionProfiles = configuredEditExtensionProfiles();
	snapshot.colorSettings = configuredColorSetupSettings();
	return snapshot;
}

bool restoreRuntimeSettingsSnapshot(const RuntimeSettingsSnapshot &snapshot, std::string &errorText) {
	if (!setConfiguredSettingsMacroFilePath(snapshot.settingsMacroFilePath, &errorText)) return false;
	if (!setConfiguredMacroDirectoryPath(snapshot.macroDirectoryPath, &errorText)) return false;
	if (!setConfiguredHelpFilePath(snapshot.helpFilePath, &errorText)) return false;
	if (!setConfiguredTempDirectoryPath(snapshot.tempDirectoryPath, &errorText)) return false;
	if (!setConfiguredShellExecutablePath(snapshot.shellExecutablePath, &errorText)) return false;
	if (!setConfiguredAutoDetectBinaryFiles(snapshot.autoDetectBinaryFiles, &errorText)) return false;
	if (!setConfiguredAutoexecMacroEntries(snapshot.autoexecMacros, &errorText)) return false;
	if (!setConfiguredEditSetupSettings(snapshot.editSettings, &errorText)) return false;
	if (!setConfiguredEditExtensionProfiles(snapshot.editExtensionProfiles, &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, snapshot.colorSettings.windowColors.data(), snapshot.colorSettings.windowColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, snapshot.colorSettings.menuDialogColors.data(), snapshot.colorSettings.menuDialogColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Help, snapshot.colorSettings.helpColors.data(), snapshot.colorSettings.helpColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Other, snapshot.colorSettings.otherColors.data(), snapshot.colorSettings.otherColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MiniMap, snapshot.colorSettings.miniMapColors.data(), snapshot.colorSettings.miniMapColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::FileCompareMiniMap, snapshot.colorSettings.fileCompareMiniMapColors.data(), snapshot.colorSettings.fileCompareMiniMapColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Code, snapshot.colorSettings.codeColors.data(), snapshot.colorSettings.codeColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::FileCompare, snapshot.colorSettings.fileCompareColors.data(), snapshot.colorSettings.fileCompareColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Debugger, snapshot.colorSettings.debuggerColors.data(), snapshot.colorSettings.debuggerColors.size(), &errorText)) return false;
	if (!setConfiguredColorThemeFilePath(snapshot.colorThemeFilePath, &errorText)) return false;
	if (!setConfiguredColorOutputMode(snapshot.colorOutputMode, &errorText)) return false;
	clearConfiguredAutoexecMacroDiagnostics();
	errorText.clear();
	return true;
}

bool printProfileLineForMacro(const std::string &path, bool requireStageable, std::string &failureReason) {
	std::string source;
	std::string ioError;
	std::vector<unsigned char> bytecode;
	std::string compileError;
	MRMacroExecutionProfile profile;
	bool canStage = false;

	if (!readTextFile(path, source, ioError)) {
		failureReason = path + ": " + ioError;
		return false;
	}
	if (!compileBytecode(source, bytecode, compileError)) {
		std::cout << path << ": compile failed: " << compileError << "\n";
		failureReason = path + ": compile failed: " + compileError;
		return false;
	}

	profile = mrvmAnalyzeBytecode(bytecode.data(), bytecode.size());
	canStage = mrvmCanRunStagedInBackground(profile);
	std::cout << path << " profile=" << mrvmDescribeExecutionProfile(profile) << " canStage=" << (canStage ? 1 : 0) << "\n";
	if (requireStageable && !canStage) {
		failureReason = path + ": staged background expected but rejected.";
		return false;
	}
	return true;
}

bool runCustomStagedProbe(const std::string &source, const std::string &documentText, const std::string &fileName, std::size_t startCursorOffset, std::size_t expectedCursorOffset, int expectedLine, int expectedColumn, bool printText, std::string &failureReason) {
	std::vector<unsigned char> bytecode;
	std::string compileError;
	MRMacroExecutionProfile profile;
	MRMacroStagedExecutionInput input;
	MRMacroStagedJobResult result;
	mr::editor::CommitResult commit;
	std::size_t cursorOffset = 0;
	int lineNumber = 1;
	int columnNumber = 1;

	if (!compileBytecode(source, bytecode, compileError)) {
		std::cout << "custom compile failed: " << compileError << "\n";
		failureReason = "custom compile failed: " + compileError;
		return false;
	}

	profile = mrvmAnalyzeBytecode(bytecode.data(), bytecode.size());
	std::cout << "custom profile=" << mrvmDescribeExecutionProfile(profile) << " canStage=" << (mrvmCanRunStagedInBackground(profile) ? 1 : 0) << "\n";
	if (!mrvmCanRunStagedInBackground(profile)) {
		failureReason = "custom macro is not staged-background eligible.";
		return false;
	}

	input.document = mr::editor::TextDocument(documentText);
	input.baseVersion = input.document.version();
	input.cursorOffset = std::min(startCursorOffset, input.document.length());
	input.selectionStart = input.cursorOffset;
	input.selectionEnd = input.cursorOffset;
	input.pageLines = 20;
	input.fileName = fileName;

	result = mrvmRunBytecodeStagedBackground(bytecode.data(), bytecode.size(), input);
	commit = input.document.tryApply(result.transaction);
	cursorOffset = std::min(result.cursorOffset, input.document.length());
	lineNumber = static_cast<int>(input.document.lineIndex(cursorOffset) + 1);
	columnNumber = static_cast<int>(input.document.column(cursorOffset) + 1);

	std::cout << "custom ops=" << result.transaction.operations().size() << " hadError=" << (result.hadError ? 1 : 0) << " applied=" << (commit.applied() ? 1 : 0) << " conflicted=" << (commit.conflicted() ? 1 : 0) << " cursor=" << cursorOffset << " line=" << lineNumber << " col=" << columnNumber << " modified=" << (result.fileChanged ? 1 : 0);
	if (printText) std::cout << " text='" << input.document.text() << "'";
	std::cout << "\n";

	if (result.hadError) {
		failureReason = "custom staged run reported VM error.";
		return false;
	}
	if (result.transaction.operations().size() != 0) {
		failureReason = "custom staged run expected zero staged operations.";
		return false;
	}
	if (commit.applied()) {
		failureReason = "custom staged run expected no document commit.";
		return false;
	}
	if (commit.conflicted()) {
		failureReason = "custom staged run unexpectedly conflicted.";
		return false;
	}
	if (result.fileChanged) {
		failureReason = "custom staged run unexpectedly marked file as modified.";
		return false;
	}
	if (cursorOffset != expectedCursorOffset) {
		failureReason = "custom cursor offset mismatch.";
		return false;
	}
	if (lineNumber != expectedLine) {
		failureReason = "custom cursor line mismatch.";
		return false;
	}
	if (columnNumber != expectedColumn) {
		failureReason = "custom cursor column mismatch.";
		return false;
	}
	if (printText && input.document.text() != documentText) {
		failureReason = "custom probe text mismatch.";
		return false;
	}

	return true;
}

bool testExecSessionStagedConflictRejectionGuard(std::string &failureReason) {
	static constexpr std::uint64_t kTaskId = 700001;
	mr::editor::TextDocument document("alpha\nbeta\n");
	mr::editor::StagedEditTransaction stale(document.readSnapshot(), "exec-session-staged-conflict");
	MRMacroExecutionOwner owner;
	MRMacroExecutionSession session;
	mr::editor::CommitResult conflict;
	std::vector<MRMacroExecutionResult> results;
	std::vector<MRMacroExecutionSession> activeSessions;

	stale.insert(1, "!");
	document.insert(0, "v");
	conflict = document.tryApply(stale);
	if (!conflict.conflicted()) {
		failureReason = "Stale staged transaction must conflict before publishing a rejected exec-session result.";
		return false;
	}

	owner.hasBuffer = true;
	owner.bufferId = 17;
	session = createMacroExecutionSession("exec-session-staged-conflict", MRMacroExecutionRoute::StagedBackground, owner);
	session.taskId = kTaskId;
	trackMacroExecutionSession(session);
	if (!publishMacroExecutionResultForTask(kTaskId, MRMacroExecutionState::Rejected, "staged commit rejected by conflict")) {
		failureReason = "Rejected exec-session result must report that a tracked task was completed.";
		return false;
	}

	results = recentMacroExecutionResults();
	if (results.empty()) {
		failureReason = "Rejected exec-session result was not published.";
		return false;
	}
	const MRMacroExecutionResult &result = results.back();
	if (result.state != MRMacroExecutionState::Rejected || result.session.state != MRMacroExecutionState::Rejected) {
		failureReason = "Staged conflict must publish MRMacroExecutionState::Rejected.";
		return false;
	}
	if (result.session.route != MRMacroExecutionRoute::StagedBackground || result.session.taskId != kTaskId || result.session.owner.bufferId != owner.bufferId) {
		failureReason = "Rejected exec-session result lost route, task or owner metadata.";
		return false;
	}
	activeSessions = activeMacroExecutionSessions();
	for (const MRMacroExecutionSession &activeSession : activeSessions)
		if (activeSession.taskId == kTaskId) {
			failureReason = "Rejected exec-session result must remove the active task session.";
			return false;
		}

	failureReason.clear();
	return true;
}

bool testExecSessionListenerFanoutGuard(std::string &failureReason) {
	MRMacroExecutionSessionListenerId firstListener = installMacroExecutionSessionStatusHook();
	MRMacroExecutionSessionListenerId secondListener = installMacroExecutionSessionStatusHook();
	std::uint64_t generation = macroExecutionSessionStatusGeneration();

	if (firstListener == 0 || secondListener == 0 || firstListener == secondListener) {
		removeMacroExecutionSessionListener(firstListener);
		removeMacroExecutionSessionListener(secondListener);
		failureReason = "Exec-session listeners must receive distinct non-zero ids.";
		return false;
	}

	notifyMacroExecutionSessionChanged();
	if (macroExecutionSessionStatusGeneration() != generation + 2) {
		removeMacroExecutionSessionListener(firstListener);
		removeMacroExecutionSessionListener(secondListener);
		failureReason = "Exec-session notify must fan out to all registered listeners.";
		return false;
	}
	generation += 2;

	if (!removeMacroExecutionSessionListener(firstListener)) {
		removeMacroExecutionSessionListener(secondListener);
		failureReason = "Exec-session listener removal must acknowledge an active listener.";
		return false;
	}
	notifyMacroExecutionSessionChanged();
	if (macroExecutionSessionStatusGeneration() != generation + 1) {
		removeMacroExecutionSessionListener(secondListener);
		failureReason = "Exec-session notify must stop calling a removed listener.";
		return false;
	}
	generation += 1;

	if (!removeMacroExecutionSessionListener(secondListener)) {
		failureReason = "Exec-session listener removal must remove the remaining listener.";
		return false;
	}
	notifyMacroExecutionSessionChanged();
	if (macroExecutionSessionStatusGeneration() != generation) {
		failureReason = "Exec-session notify must not call removed listeners.";
		return false;
	}
	if (removeMacroExecutionSessionListener(secondListener)) {
		failureReason = "Exec-session listener removal must reject an already removed listener.";
		return false;
	}
	if (addMacroExecutionSessionListener(nullptr) != 0) {
		failureReason = "Exec-session listener registration must reject null hooks.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testExecSessionOwnerCancellationGuard(std::string &failureReason) {
	static constexpr std::uint64_t kOwnedTaskId = 700101;
	static constexpr std::uint64_t kOtherTaskId = 700102;
	MRMacroExecutionOwner owner;
	MRMacroExecutionOwner otherOwner;
	MRMacroExecutionSession ownedSession;
	MRMacroExecutionSession otherSession;
	std::vector<MRMacroExecutionSession> ownedSessions;
	std::vector<MRMacroExecutionSession> activeSessions;
	MRMacroModelessWindowDefinition canvasWindow;
	MRMacroModelessWindowDefinition selectDefinition;
	MRMacroModelessCanvasSpec canvas;
	MRMacroModelessTextFieldSpec textField;
	MRMacroModelessBoolFieldSpec boolField;
	MRMacroModelessIntFieldSpec intField;
	MRMacroModelessProgressFieldSpec progressField;
	MRMacroModelessLogFieldSpec logField;
	MRMacroModelessSelectFieldSpec selectField;
	MRMacroModelessCanvasCommand canvasCommand;
	MRMacroModelessCanvasScene canvasScene;
	MacroUiCanvasSpec stagedCanvas;
	MacroUiCanvasHotspotSpec stagedHotspot;
	MacroUiDialogDefinition stagedDefinition;
	std::vector<VirtualMachine::Value> actionButtonArgs;
	std::vector<VirtualMachine::Value> menuClearArgs;
	std::vector<VirtualMachine::Value> menuItemArgs;
	std::vector<VirtualMachine::Value> actionMenuArgs;
	std::vector<VirtualMachine::Value> textFieldArgs;
	std::vector<VirtualMachine::Value> boolFieldArgs;
	std::vector<VirtualMachine::Value> intFieldArgs;
	std::vector<VirtualMachine::Value> progressFieldArgs;
	std::vector<VirtualMachine::Value> logFieldArgs;
	std::vector<VirtualMachine::Value> logCountArgs;
	std::vector<VirtualMachine::Value> selectFieldArgs;
	std::vector<VirtualMachine::Value> selectOptionArgs;
	std::vector<VirtualMachine::Value> statusFieldArgs;
	std::vector<VirtualMachine::Value> unknownTextSetArgs;
	std::vector<VirtualMachine::Value> unknownBoolSetArgs;
	std::vector<VirtualMachine::Value> unknownIntSetArgs;
	std::vector<VirtualMachine::Value> unknownProgressSetArgs;
	std::vector<VirtualMachine::Value> unknownLogAppendArgs;
	std::vector<VirtualMachine::Value> unknownSelectSetArgs;
	std::vector<VirtualMachine::Value> invalidIntSetArgs;
	std::vector<VirtualMachine::Value> intValueArgs;
	std::vector<VirtualMachine::Value> invalidProgressSetArgs;
	std::vector<VirtualMachine::Value> progressValueArgs;
	std::vector<VirtualMachine::Value> invalidSelectSetArgs;
	std::vector<VirtualMachine::Value> selectValueArgs;
	std::vector<VirtualMachine::Value> unknownStatusSetArgs;
	MRMacroModelessWindowGeometry geometry;
	MRMacroModelessWindowDesktopState desktopState;
	MRMacroModelessWindowDesktopState storedDesktopState;
	MRVMRuntimeKv canvasRuntimeKv;
	std::vector<std::string> menuItems;
	std::vector<unsigned char> mmpBytecode;
	std::string compileError;
	std::string sourceText;
	std::string ioError;
	MRMacroExecutionProfile mmpProfile;
	int actionButtonReturnValue = 0;
	std::string actionButtonError;
	int menuClearReturnValue = 0;
	std::string menuClearError;
	int menuItemReturnValue = 0;
	std::string menuItemError;
	int actionMenuReturnValue = 0;
	std::string actionMenuError;
	int textFieldReturnValue = 0;
	std::string textFieldError;
	int boolFieldReturnValue = 0;
	std::string boolFieldError;
	int selectFieldReturnValue = 0;
	std::string selectFieldError;
	int selectOptionReturnValue = 0;
	std::string selectOptionError;
	int statusFieldReturnValue = 0;
	std::string statusFieldError;
	int unknownTextSetReturnValue = 0;
	std::string unknownTextSetError;
	int unknownBoolSetReturnValue = 0;
	std::string unknownBoolSetError;
	int intFieldReturnValue = 0;
	std::string intFieldError;
	int progressFieldReturnValue = 0;
	std::string progressFieldError;
	int logFieldReturnValue = 0;
	std::string logFieldError;
	int unknownIntSetReturnValue = 0;
	std::string unknownIntSetError;
	int unknownProgressSetReturnValue = 0;
	std::string unknownProgressSetError;
	int unknownLogAppendReturnValue = 0;
	std::string unknownLogAppendError;
	int invalidIntSetReturnValue = 0;
	std::string invalidIntSetError;
	int invalidProgressSetReturnValue = 0;
	std::string invalidProgressSetError;
	int unknownSelectSetReturnValue = 0;
	std::string unknownSelectSetError;
	int invalidSelectSetReturnValue = 0;
	std::string invalidSelectSetError;
	int unknownStatusSetReturnValue = 0;
	std::string unknownStatusSetError;
	int statusDisplayIndex = 0;
	std::string textFieldValue;
	bool boolFieldValue = false;
	int intFieldValue = 0;
	int progressTotal = 0;
	int progressValue = 0;
	int logFieldCount = 0;
	std::vector<std::string> logLines;
	std::string selectFieldValue;
	VirtualMachine::Value intValueResult;
	VirtualMachine::Value progressValueResult;
	VirtualMachine::Value logCountResult;
	VirtualMachine::Value selectValueResult;
	bool ownedTaskStillActive = false;
	static const char kMmpSource[] = "$MACRO MmpProbe;\n"
	                                 "DEF_STR(WindowId);\n"
	                                 "WindowId := MMP_WINDOW_INSTANCE('Probe');\n"
	                                 "MMP_CANVAS(1, 1, 20, 4, 'Main');\n"
	                                 "MMP_CANVAS_CLEAR('Window', 'Main', MMP_STYLE_SURFACE());\n"
	                                 "MMP_CANVAS_FILL('Window', 'Main', 1, 1, 18, 2, MMP_STYLE_MUTED());\n"
	                                 "MMP_CANVAS_BOX('Window', 'Main', 0, 0, 20, 4, MMP_STYLE_ACCENT());\n"
	                                 "MMP_CANVAS_TEXT('Window', 'Main', 1, 1, MMP_STYLE_TEXT(), 'text');\n"
	                                 "MMP_CANVAS_GLYPH('Window', 'Main', 1, 2, MMP_STYLE_TEXT(), '*');\n"
	                                 "MMP_CANVAS_LINE('Window', 'Main', 1, 2, 18, 2, MMP_STYLE_ACCENT(), '-');\n"
	                                 "MMP_CANVAS_HOTSPOT('Main', 1, 1, 18, 1, 71, 'MmpProbe^Activate');\n"
	                                 "MMP_ACTION_BUTTON(1, 6, 10, 72, '~A~ctivate', 'MmpProbe^Activate');\n"
	                                 "MMP_MENU_CLEAR('Actions');\n"
	                                 "MMP_MENU_ITEM('Actions', 'Activate', 'ACTIVATE', 'Run the action');\n"
	                                 "MMP_ACTION_MENU(1, 8, 18, 4, 73, 'Actions', 'Actions', 'MmpProbe^Activate');\n"
	                                 "MMP_TEXT_FIELD(1, 12, 18, 'Name', 'Name', 'Ready');\n"
	                                 "MMP_TEXT_SET('Window', 'Name', 'Updated');\n"
	                                 "MMP_BOOL_FIELD(1, 14, 'Enabled', '~E~nabled', 1);\n"
	                                 "MMP_BOOL_SET('Window', 'Enabled', 0);\n"
	                                 "MMP_INT_FIELD(1, 16, 8, 'Retries', 'Retries', 3, 0, 9);\n"
	                                 "MMP_INT_SET('Window', 'Retries', 5);\n"
	                                 "MMP_PROGRESS_FIELD(1, 18, 16, 'Scan', 'Scan', 100, 25);\n"
	                                 "MMP_PROGRESS_SET('Window', 'Scan', 50);\n"
	                                 "MMP_LOG_FIELD(1, 20, 18, 3, 'Events', 'Events', 4);\n"
	                                 "MMP_LOG_APPEND('Window', 'Events', 'Started');\n"
	                                 "MMP_LOG_CLEAR('Window', 'Events');\n"
	                                 "MMP_SELECT_FIELD(1, 20, 18, 3, 'Mode', 'Mode', 'Normal');\n"
	                                 "MMP_SELECT_OPTION('Mode', 'Normal');\n"
	                                 "MMP_SELECT_OPTION('Mode', 'Safe');\n"
	                                 "MMP_SELECT_SET('Window', 'Mode', 'Safe');\n"
	                                 "MMP_STATUS_FIELD(1, 7, 18, 'Activity', 'Ready');\n"
	                                 "MMP_STATUS_SET('Window', 'Activity', 'Updated');\n"
	                                 "DEF_STR(Name);\n"
	                                 "Name := MMP_TEXT_VALUE('Window', 'Name');\n"
	                                 "DEF_INT(Enabled);\n"
	                                 "Enabled := MMP_BOOL_VALUE('Window', 'Enabled');\n"
	                                 "DEF_INT(Retries);\n"
	                                 "Retries := MMP_INT_VALUE('Window', 'Retries');\n"
	                                 "DEF_INT(Scan);\n"
	                                 "Scan := MMP_PROGRESS_VALUE('Window', 'Scan');\n"
	                                 "DEF_INT(EventCount);\n"
	                                 "EventCount := MMP_LOG_COUNT('Window', 'Events');\n"
	                                 "DEF_STR(Mode);\n"
	                                 "Mode := MMP_SELECT_VALUE('Window', 'Mode');\n"
	                                 "MMP_CANVAS_COMMIT('Window', 'Main');\n"
	                                 "MMP_TIMER_START('Window', 'Refresh', 100, 'MmpProbe^Refresh');\n"
	                                 "MMP_TIMER_STOP('Window', 'Refresh');\n"
	                                 "DEF_INT(Width);\n"
	                                 "Width := MMP_WINDOW_WIDTH('Window');\n"
	                                 "END_MACRO;\n";

	owner.hasBuffer = true;
	owner.bufferId = 21;
	otherOwner.hasBuffer = true;
	otherOwner.bufferId = 22;
	MRMacroExecutionOwner modelessOwner;
	MRMacroExecutionOwner otherModelessOwner;

	modelessOwner.modelessWindowId = "MMP-OWNER";
	otherModelessOwner.modelessWindowId = "MMP-OTHER";
	if (!macroExecutionOwnerMatches(modelessOwner, modelessOwner) || macroExecutionOwnerMatches(otherModelessOwner, modelessOwner) || macroExecutionOwnerMatches(modelessOwner, MRMacroExecutionOwner())) {
		failureReason = "Exec-session owner matching must be exact for modeless window owners.";
		return false;
	}

	ownedSession = createMacroExecutionSession("exec-session-owned-cancel", MRMacroExecutionRoute::Background, owner);
	ownedSession.taskId = kOwnedTaskId;
	trackMacroExecutionSession(ownedSession);
	otherSession = createMacroExecutionSession("exec-session-other-cancel", MRMacroExecutionRoute::Background, otherOwner);
	otherSession.taskId = kOtherTaskId;
	trackMacroExecutionSession(otherSession);

	if (!macroExecutionOwnerMatches(owner, owner) || macroExecutionOwnerMatches(otherOwner, owner)) {
		publishMacroExecutionResultForTask(kOwnedTaskId, MRMacroExecutionState::Cancelled, "cleanup");
		publishMacroExecutionResultForTask(kOtherTaskId, MRMacroExecutionState::Cancelled, "cleanup");
		failureReason = "Exec-session owner matching must be exact for buffer owners.";
		return false;
	}

	ownedSessions = activeMacroExecutionSessionsForOwner(owner);
	if (ownedSessions.size() != 1 || ownedSessions.front().taskId != kOwnedTaskId) {
		publishMacroExecutionResultForTask(kOwnedTaskId, MRMacroExecutionState::Cancelled, "cleanup");
		publishMacroExecutionResultForTask(kOtherTaskId, MRMacroExecutionState::Cancelled, "cleanup");
		failureReason = "Exec-session owner filter must return only matching active sessions.";
		return false;
	}

	if (!markMacroExecutionSessionCancellationRequestedForTask(kOwnedTaskId)) {
		publishMacroExecutionResultForTask(kOwnedTaskId, MRMacroExecutionState::Cancelled, "cleanup");
		publishMacroExecutionResultForTask(kOtherTaskId, MRMacroExecutionState::Cancelled, "cleanup");
		failureReason = "Exec-session cancellation request must mark a tracked task.";
		return false;
	}
	activeSessions = activeMacroExecutionSessions();
	for (const MRMacroExecutionSession &activeSession : activeSessions)
		if (activeSession.taskId == kOwnedTaskId) {
			ownedTaskStillActive = true;
			if (activeSession.state != MRMacroExecutionState::CancellationRequested) {
				publishMacroExecutionResultForTask(kOwnedTaskId, MRMacroExecutionState::Cancelled, "cleanup");
				publishMacroExecutionResultForTask(kOtherTaskId, MRMacroExecutionState::Cancelled, "cleanup");
				failureReason = "Exec-session cancellation request must update active state.";
				return false;
			}
		}
	if (!ownedTaskStillActive) {
		publishMacroExecutionResultForTask(kOtherTaskId, MRMacroExecutionState::Cancelled, "cleanup");
		failureReason = "Exec-session cancellation request must not publish a terminal result.";
		return false;
	}
	if (markMacroExecutionSessionCancellationRequestedForTask(0) || markMacroExecutionSessionCancellationRequestedForTask(99999991)) {
		publishMacroExecutionResultForTask(kOwnedTaskId, MRMacroExecutionState::Cancelled, "cleanup");
		publishMacroExecutionResultForTask(kOtherTaskId, MRMacroExecutionState::Cancelled, "cleanup");
		failureReason = "Exec-session cancellation request must reject invalid task ids.";
		return false;
	}

	publishMacroExecutionResultForTask(kOwnedTaskId, MRMacroExecutionState::Cancelled, "cleanup");
	publishMacroExecutionResultForTask(kOtherTaskId, MRMacroExecutionState::Cancelled, "cleanup");

	canvasWindow.windowId = "MMP-CANVAS-PROBE";
	canvasWindow.width = 30;
	canvasWindow.height = 10;
	canvas.canvasId = "MAIN";
	canvas.x = 2;
	canvas.y = 2;
	canvas.width = 20;
	canvas.height = 4;
	canvasWindow.canvases.push_back(canvas);
	stagedCanvas.x = 2;
	stagedCanvas.y = 2;
	stagedCanvas.width = 20;
	stagedCanvas.height = 4;
	stagedCanvas.canvasId = "MAIN";
	stagedHotspot.canvasId = "MAIN";
	stagedHotspot.x = 1;
	stagedHotspot.y = 1;
	stagedHotspot.width = 18;
	stagedHotspot.height = 1;
	stagedHotspot.id = 71;
	stagedHotspot.macroSpec = "MmpProbe^Activate";
	mrvmModelessUiBeginDialog(canvasRuntimeKv, 0, 0, 30, 10, "MMP CANVAS PROBE");
	mrvmModelessUiAppendCanvas(canvasRuntimeKv, stagedCanvas);
	stagedHotspot.x = stagedCanvas.width;
	if (mrvmModelessUiAppendCanvasHotspot(canvasRuntimeKv, stagedHotspot)) {
		failureReason = "MMP canvas hotspot must not extend beyond its retained canvas.";
		return false;
	}
	stagedHotspot.x = 1;
	if (!mrvmModelessUiAppendCanvasHotspot(canvasRuntimeKv, stagedHotspot)) {
		failureReason = "MMP canvas hotspot must attach to a declared retained canvas.";
		return false;
	}
	actionButtonArgs.push_back(mrvmMakeInt(2));
	actionButtonArgs.push_back(mrvmMakeInt(7));
	actionButtonArgs.push_back(mrvmMakeInt(12));
	actionButtonArgs.push_back(mrvmMakeInt(72));
	actionButtonArgs.push_back(mrvmMakeString("~A~ctivate"));
	actionButtonArgs.push_back(mrvmMakeString("MmpProbe^Activate"));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_ACTION_BUTTON", actionButtonArgs, actionButtonReturnValue, actionButtonError) || actionButtonReturnValue != 1 || !actionButtonError.empty()) {
		failureReason = "MMP action buttons must compose the staged button and callback binding.";
		return false;
	}
	menuClearArgs.push_back(mrvmMakeString("Actions"));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_MENU_CLEAR", menuClearArgs, menuClearReturnValue, menuClearError) || menuClearReturnValue != 1 || !menuClearError.empty()) {
		failureReason = "MMP action menus must clear their named item list through the existing runtime path.";
		return false;
	}
	menuItemArgs.push_back(mrvmMakeString("Actions"));
	menuItemArgs.push_back(mrvmMakeString("Activate"));
	menuItemArgs.push_back(mrvmMakeString("ACTIVATE"));
	menuItemArgs.push_back(mrvmMakeString("Run the action"));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_MENU_ITEM", menuItemArgs, menuItemReturnValue, menuItemError) || menuItemReturnValue != 1 || !menuItemError.empty()) {
		failureReason = "MMP action menus must append typed menu items through the existing runtime path.";
		return false;
	}
	if (!mrvmModelessUiReadItemList(canvasRuntimeKv, "ACTIONS", menuItems) || menuItems.size() != 1 || menuItems[0] != "Activate\tACTIVATE\tRun the action") {
		failureReason = "MMP action menu items must use the existing named item-list representation.";
		return false;
	}
	actionMenuArgs.push_back(mrvmMakeInt(2));
	actionMenuArgs.push_back(mrvmMakeInt(8));
	actionMenuArgs.push_back(mrvmMakeInt(18));
	actionMenuArgs.push_back(mrvmMakeInt(4));
	actionMenuArgs.push_back(mrvmMakeInt(73));
	actionMenuArgs.push_back(mrvmMakeString("Actions"));
	actionMenuArgs.push_back(mrvmMakeString("Actions"));
	actionMenuArgs.push_back(mrvmMakeString("MmpProbe^Activate"));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_ACTION_MENU", actionMenuArgs, actionMenuReturnValue, actionMenuError) || actionMenuReturnValue != 1 || !actionMenuError.empty()) {
		failureReason = "MMP action menus must compose the staged grid and callback binding.";
		return false;
	}
	textFieldArgs.push_back(mrvmMakeInt(2));
	textFieldArgs.push_back(mrvmMakeInt(12));
	textFieldArgs.push_back(mrvmMakeInt(18));
	textFieldArgs.push_back(mrvmMakeString("Name"));
	textFieldArgs.push_back(mrvmMakeString("Name"));
	textFieldArgs.push_back(mrvmMakeString("Ready"));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_TEXT_FIELD", textFieldArgs, textFieldReturnValue, textFieldError) || textFieldReturnValue != 1 || !textFieldError.empty()) {
		failureReason = "MMP text fields must compose a named native input definition.";
		return false;
	}
	boolFieldArgs.push_back(mrvmMakeInt(2));
	boolFieldArgs.push_back(mrvmMakeInt(14));
	boolFieldArgs.push_back(mrvmMakeString("Enabled"));
	boolFieldArgs.push_back(mrvmMakeString("~E~nabled"));
	boolFieldArgs.push_back(mrvmMakeInt(1));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_BOOL_FIELD", boolFieldArgs, boolFieldReturnValue, boolFieldError) || boolFieldReturnValue != 1 || !boolFieldError.empty()) {
		failureReason = "MMP boolean fields must compose a named native checkbox definition.";
		return false;
	}
	intFieldArgs.push_back(mrvmMakeInt(2));
	intFieldArgs.push_back(mrvmMakeInt(16));
	intFieldArgs.push_back(mrvmMakeInt(8));
	intFieldArgs.push_back(mrvmMakeString("Retries"));
	intFieldArgs.push_back(mrvmMakeString("Retries"));
	intFieldArgs.push_back(mrvmMakeInt(3));
	intFieldArgs.push_back(mrvmMakeInt(0));
	intFieldArgs.push_back(mrvmMakeInt(9));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_INT_FIELD", intFieldArgs, intFieldReturnValue, intFieldError) || intFieldReturnValue != 1 || !intFieldError.empty()) {
		failureReason = "MMP integer fields must compose a named bounded input definition.";
		return false;
	}
	progressFieldArgs.push_back(mrvmMakeInt(2));
	progressFieldArgs.push_back(mrvmMakeInt(18));
	progressFieldArgs.push_back(mrvmMakeInt(16));
	progressFieldArgs.push_back(mrvmMakeString("Scan"));
	progressFieldArgs.push_back(mrvmMakeString("Scan"));
	progressFieldArgs.push_back(mrvmMakeInt(100));
	progressFieldArgs.push_back(mrvmMakeInt(25));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_PROGRESS_FIELD", progressFieldArgs, progressFieldReturnValue, progressFieldError) || progressFieldReturnValue != 1 || !progressFieldError.empty()) {
		failureReason = "MMP progress fields must compose a named retained progress definition.";
		return false;
	}
	logFieldArgs.push_back(mrvmMakeInt(2));
	logFieldArgs.push_back(mrvmMakeInt(20));
	logFieldArgs.push_back(mrvmMakeInt(18));
	logFieldArgs.push_back(mrvmMakeInt(3));
	logFieldArgs.push_back(mrvmMakeString("Events"));
	logFieldArgs.push_back(mrvmMakeString("Events"));
	logFieldArgs.push_back(mrvmMakeInt(4));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_LOG_FIELD", logFieldArgs, logFieldReturnValue, logFieldError) || logFieldReturnValue != 1 || !logFieldError.empty()) {
		failureReason = "MMP log fields must compose a named bounded event-log definition.";
		return false;
	}
	selectFieldArgs.push_back(mrvmMakeInt(2));
	selectFieldArgs.push_back(mrvmMakeInt(16));
	selectFieldArgs.push_back(mrvmMakeInt(18));
	selectFieldArgs.push_back(mrvmMakeInt(3));
	selectFieldArgs.push_back(mrvmMakeString("Mode"));
	selectFieldArgs.push_back(mrvmMakeString("Mode"));
	selectFieldArgs.push_back(mrvmMakeString("Unknown"));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_SELECT_FIELD", selectFieldArgs, selectFieldReturnValue, selectFieldError) || selectFieldReturnValue != 1 || !selectFieldError.empty()) {
		failureReason = "MMP selection fields must compose a named native selection definition.";
		return false;
	}
	selectOptionArgs.push_back(mrvmMakeString("Mode"));
	selectOptionArgs.push_back(mrvmMakeString("Normal"));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_SELECT_OPTION", selectOptionArgs, selectOptionReturnValue, selectOptionError) || selectOptionReturnValue != 1 || !selectOptionError.empty()) {
		failureReason = "MMP selection fields must retain their first declared option.";
		return false;
	}
	selectOptionArgs[1] = mrvmMakeString("Safe");
	selectOptionReturnValue = 0;
	selectOptionError.clear();
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_SELECT_OPTION", selectOptionArgs, selectOptionReturnValue, selectOptionError) || selectOptionReturnValue != 1 || !selectOptionError.empty()) {
		failureReason = "MMP selection fields must retain further declared options.";
		return false;
	}
	statusFieldArgs.push_back(mrvmMakeInt(2));
	statusFieldArgs.push_back(mrvmMakeInt(8));
	statusFieldArgs.push_back(mrvmMakeInt(18));
	statusFieldArgs.push_back(mrvmMakeString("Activity"));
	statusFieldArgs.push_back(mrvmMakeString("Ready"));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_STATUS_FIELD", statusFieldArgs, statusFieldReturnValue, statusFieldError) || statusFieldReturnValue != 1 || !statusFieldError.empty()) {
		failureReason = "MMP status fields must compose a display with its logical id.";
		return false;
	}
	stagedDefinition = mrvmModelessUiReadDialogDefinition(canvasRuntimeKv);
	if (stagedDefinition.canvasHotspots.size() != 1 || stagedDefinition.canvasHotspots[0].canvasId != "MAIN" || stagedDefinition.canvasHotspots[0].id != 71 || stagedDefinition.canvasHotspots[0].macroSpec != "MmpProbe^Activate") {
		failureReason = "MMP canvas hotspot must remain typed MODELESSUI definition state.";
		return false;
	}
	if (stagedDefinition.buttons.size() != 1 || stagedDefinition.buttons[0].id != 72 || stagedDefinition.buttons[0].text != "~A~ctivate" || stagedDefinition.modelessButtonMacros[72] != "MmpProbe^Activate") {
		failureReason = "MMP action buttons must retain the native button and its callback binding together.";
		return false;
	}
	if (stagedDefinition.grids.size() != 1 || stagedDefinition.grids[0].id != 73 || stagedDefinition.grids[0].label != "Actions" || stagedDefinition.grids[0].itemSpec != "Actions" || stagedDefinition.grids[0].start != 1 || stagedDefinition.modelessButtonMacros[73] != "MmpProbe^Activate") {
		failureReason = "MMP action menus must retain the native grid and its callback binding together.";
		return false;
	}
	if (stagedDefinition.textFields.size() != 1 || stagedDefinition.textFields[0].fieldId != "NAME" || stagedDefinition.textFields[0].input.label != "Name" || stagedDefinition.textFields[0].input.text != "Ready") {
		failureReason = "MMP text fields must retain their logical id and native input definition together.";
		return false;
	}
	if (stagedDefinition.boolFields.size() != 1 || stagedDefinition.boolFields[0].fieldId != "ENABLED" || stagedDefinition.boolFields[0].caption != "~E~nabled" || !stagedDefinition.boolFields[0].value) {
		failureReason = "MMP boolean fields must retain their logical id and native checkbox definition together.";
		return false;
	}
	if (stagedDefinition.intFields.size() != 1 || stagedDefinition.intFields[0].fieldId != "RETRIES" || stagedDefinition.intFields[0].label != "Retries" || stagedDefinition.intFields[0].value != 3 || stagedDefinition.intFields[0].minimum != 0 || stagedDefinition.intFields[0].maximum != 9) {
		failureReason = "MMP integer fields must retain their logical id, value and inclusive range together.";
		return false;
	}
	if (stagedDefinition.progressFields.size() != 1 || stagedDefinition.progressFields[0].fieldId != "SCAN" || stagedDefinition.progressFields[0].label != "Scan" || stagedDefinition.progressFields[0].total != 100 || stagedDefinition.progressFields[0].value != 25) {
		failureReason = "MMP progress fields must retain their logical id, total and current value together.";
		return false;
	}
	if (stagedDefinition.logFields.size() != 1 || stagedDefinition.logFields[0].logId != "EVENTS" || stagedDefinition.logFields[0].label != "Events" || stagedDefinition.logFields[0].height != 3 || stagedDefinition.logFields[0].capacity != 4) {
		failureReason = "MMP log fields must retain their logical id, visible height and bounded capacity together.";
		return false;
	}
	if (stagedDefinition.selectFields.size() != 1 || stagedDefinition.selectFields[0].fieldId != "MODE" || stagedDefinition.selectFields[0].label != "Mode" || stagedDefinition.selectFields[0].value != "Unknown" || stagedDefinition.selectFields[0].options.size() != 2 || stagedDefinition.selectFields[0].options[1] != "Safe") {
		failureReason = "MMP selection fields must retain their logical id, options and initial value together.";
		return false;
	}
	selectDefinition = mrvmBuildMacroModelessDefinition(canvasRuntimeKv, "MMP-SELECT-PROBE");
	if (selectDefinition.selectFields.size() != 1 || selectDefinition.selectFields[0].value != "Normal") {
		failureReason = "MMP selection fields must fall back to their first declared option.";
		return false;
	}
	if (stagedDefinition.displays.size() != 1 || stagedDefinition.displays[0].text != "Ready" || stagedDefinition.statusDisplayIndices["ACTIVITY"] != 1) {
		failureReason = "MMP status fields must retain a logical id mapped to their display.";
		return false;
	}
	canvasWindow.displays.push_back(MRMacroModelessDisplaySpec());
	canvasWindow.displays[0].x = 2;
	canvasWindow.displays[0].y = 8;
	canvasWindow.displays[0].width = 18;
	canvasWindow.displays[0].text = "Ready";
	canvasWindow.statusDisplayIndices["ACTIVITY"] = 1;
	textField.x = 2;
	textField.y = 12;
	textField.width = 18;
	textField.fieldId = "NAME";
	textField.label = "Name";
	textField.text = "Ready";
	canvasWindow.textFields.push_back(textField);
	boolField.x = 2;
	boolField.y = 14;
	boolField.fieldId = "ENABLED";
	boolField.caption = "~E~nabled";
	boolField.value = true;
	canvasWindow.boolFields.push_back(boolField);
	intField.x = 2;
	intField.y = 16;
	intField.width = 8;
	intField.fieldId = "RETRIES";
	intField.label = "Retries";
	intField.minimum = 0;
	intField.maximum = 9;
	intField.value = 3;
	canvasWindow.intFields.push_back(intField);
	progressField.x = 2;
	progressField.y = 18;
	progressField.width = 16;
	progressField.fieldId = "SCAN";
	progressField.label = "Scan";
	progressField.total = 100;
	progressField.value = 25;
	canvasWindow.progressFields.push_back(progressField);
	logField.x = 2;
	logField.y = 20;
	logField.width = 18;
	logField.height = 3;
	logField.logId = "EVENTS";
	logField.label = "Events";
	logField.capacity = 4;
	canvasWindow.logFields.push_back(logField);
	selectField.x = 2;
	selectField.y = 16;
	selectField.width = 18;
	selectField.height = 3;
	selectField.fieldId = "MODE";
	selectField.label = "Mode";
	selectField.value = "Normal";
	selectField.options.push_back("Normal");
	selectField.options.push_back("Safe");
	canvasWindow.selectFields.push_back(selectField);
	mrvmModelessUiStoreWindowDefinition(canvasRuntimeKv, canvasWindow);
	if (!mrvmModelessUiWindowExists(canvasRuntimeKv, canvasWindow.windowId) || !mrvmModelessUiReadWindowGeometry(canvasRuntimeKv, canvasWindow.windowId, geometry) || geometry.width != canvasWindow.width || geometry.height != canvasWindow.height) {
		failureReason = "MMP canvas window definition must remain in MODELESSUI with typed geometry.";
		return false;
	}
	if (!mrvmModelessUiReadWindowStatusDisplayIndex(canvasRuntimeKv, canvasWindow.windowId, "ACTIVITY", statusDisplayIndex) || statusDisplayIndex != 1 || mrvmModelessUiReadWindowStatusDisplayIndex(canvasRuntimeKv, canvasWindow.windowId, "UNKNOWN", statusDisplayIndex)) {
		failureReason = "MMP status ids must resolve only through the retained window model.";
		return false;
	}
	if (!mrvmModelessUiReadWindowTextFieldValue(canvasRuntimeKv, canvasWindow.windowId, "NAME", textFieldValue) || textFieldValue != "Ready" || !mrvmModelessUiStoreWindowTextFieldValue(canvasRuntimeKv, canvasWindow.windowId, "NAME", "Updated") || !mrvmModelessUiReadWindowTextFieldValue(canvasRuntimeKv, canvasWindow.windowId, "NAME", textFieldValue) || textFieldValue != "Updated") {
		failureReason = "MMP text fields must retain and update their values through the window model.";
		return false;
	}
	if (!mrvmModelessUiReadWindowBoolFieldValue(canvasRuntimeKv, canvasWindow.windowId, "ENABLED", boolFieldValue) || !boolFieldValue || !mrvmModelessUiStoreWindowBoolFieldValue(canvasRuntimeKv, canvasWindow.windowId, "ENABLED", false) || !mrvmModelessUiReadWindowBoolFieldValue(canvasRuntimeKv, canvasWindow.windowId, "ENABLED", boolFieldValue) || boolFieldValue) {
		failureReason = "MMP boolean fields must retain and update their values through the window model.";
		return false;
	}
	if (!mrvmModelessUiReadWindowIntFieldValue(canvasRuntimeKv, canvasWindow.windowId, "RETRIES", intFieldValue) || intFieldValue != 3 || !mrvmModelessUiStoreWindowIntFieldValue(canvasRuntimeKv, canvasWindow.windowId, "RETRIES", 5) || !mrvmModelessUiReadWindowIntFieldValue(canvasRuntimeKv, canvasWindow.windowId, "RETRIES", intFieldValue) || intFieldValue != 5 || mrvmModelessUiStoreWindowIntFieldValue(canvasRuntimeKv, canvasWindow.windowId, "RETRIES", 10)) {
		failureReason = "MMP integer fields must retain only values inside their declared range through the window model.";
		return false;
	}
	intValueArgs.push_back(mrvmMakeString(canvasWindow.windowId));
	intValueArgs.push_back(mrvmMakeString("RETRIES"));
	if (!mrvmDispatchMacroModelessIntrinsic(canvasRuntimeKv, "MMP_INT_VALUE", intValueArgs, intValueResult) || intValueResult.type != TYPE_INT || intValueResult.i != 5) {
		failureReason = "MMP integer fields must expose their retained value as an integer intrinsic.";
		return false;
	}
	invalidIntSetArgs.push_back(mrvmMakeString(canvasWindow.windowId));
	invalidIntSetArgs.push_back(mrvmMakeString("RETRIES"));
	invalidIntSetArgs.push_back(mrvmMakeInt(10));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_INT_SET", invalidIntSetArgs, invalidIntSetReturnValue, invalidIntSetError) || invalidIntSetReturnValue != 0 || invalidIntSetError != "MMP_INT_SET value is outside the declared range.") {
		failureReason = "MMP integer updates must reject values outside their declared range.";
		return false;
	}
	if (!mrvmModelessUiReadWindowProgressFieldValue(canvasRuntimeKv, canvasWindow.windowId, "SCAN", progressTotal, progressValue) || progressTotal != 100 || progressValue != 25 || !mrvmModelessUiStoreWindowProgressFieldValue(canvasRuntimeKv, canvasWindow.windowId, "SCAN", 50) || !mrvmModelessUiReadWindowProgressFieldValue(canvasRuntimeKv, canvasWindow.windowId, "SCAN", progressTotal, progressValue) || progressTotal != 100 || progressValue != 50 || mrvmModelessUiStoreWindowProgressFieldValue(canvasRuntimeKv, canvasWindow.windowId, "SCAN", 101)) {
		failureReason = "MMP progress fields must retain only values inside their declared range through the window model.";
		return false;
	}
	progressValueArgs.push_back(mrvmMakeString(canvasWindow.windowId));
	progressValueArgs.push_back(mrvmMakeString("SCAN"));
	if (!mrvmDispatchMacroModelessIntrinsic(canvasRuntimeKv, "MMP_PROGRESS_VALUE", progressValueArgs, progressValueResult) || progressValueResult.type != TYPE_INT || progressValueResult.i != 50) {
		failureReason = "MMP progress fields must expose their retained value as an integer intrinsic.";
		return false;
	}
	invalidProgressSetArgs.push_back(mrvmMakeString(canvasWindow.windowId));
	invalidProgressSetArgs.push_back(mrvmMakeString("SCAN"));
	invalidProgressSetArgs.push_back(mrvmMakeInt(101));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_PROGRESS_SET", invalidProgressSetArgs, invalidProgressSetReturnValue, invalidProgressSetError) || invalidProgressSetReturnValue != 0 || invalidProgressSetError != "MMP_PROGRESS_SET value is outside the declared range.") {
		failureReason = "MMP progress updates must reject values outside their declared range.";
		return false;
	}
	if (!mrvmModelessUiAppendWindowLogFieldLine(canvasRuntimeKv, canvasWindow.windowId, "EVENTS", "One") || !mrvmModelessUiAppendWindowLogFieldLine(canvasRuntimeKv, canvasWindow.windowId, "EVENTS", "Two") || !mrvmModelessUiAppendWindowLogFieldLine(canvasRuntimeKv, canvasWindow.windowId, "EVENTS", "Three") || !mrvmModelessUiAppendWindowLogFieldLine(canvasRuntimeKv, canvasWindow.windowId, "EVENTS", "Four") || !mrvmModelessUiAppendWindowLogFieldLine(canvasRuntimeKv, canvasWindow.windowId, "EVENTS", "Five") || !mrvmModelessUiReadWindowLogFieldLines(canvasRuntimeKv, canvasWindow.windowId, "EVENTS", logLines) || logLines.size() != 4 || logLines[0] != "Two" || logLines[3] != "Five" || !mrvmModelessUiReadWindowLogFieldCount(canvasRuntimeKv, canvasWindow.windowId, "EVENTS", logFieldCount) || logFieldCount != 4) {
		failureReason = "MMP log fields must retain a bounded chronological ring through the window model.";
		return false;
	}
	mrvmModelessUiStoreWindowDefinition(canvasRuntimeKv, canvasWindow);
	if (!mrvmModelessUiReadWindowLogFieldLines(canvasRuntimeKv, canvasWindow.windowId, "EVENTS", logLines) || logLines.size() != 4 || logLines[0] != "Two" || logLines[3] != "Five") {
		failureReason = "MMP log field updates must preserve retained lines when their capacity is unchanged.";
		return false;
	}
	logCountArgs.push_back(mrvmMakeString(canvasWindow.windowId));
	logCountArgs.push_back(mrvmMakeString("EVENTS"));
	if (!mrvmDispatchMacroModelessIntrinsic(canvasRuntimeKv, "MMP_LOG_COUNT", logCountArgs, logCountResult) || logCountResult.type != TYPE_INT || logCountResult.i != 4 || !mrvmModelessUiClearWindowLogField(canvasRuntimeKv, canvasWindow.windowId, "EVENTS") || !mrvmModelessUiReadWindowLogFieldCount(canvasRuntimeKv, canvasWindow.windowId, "EVENTS", logFieldCount) || logFieldCount != 0) {
		failureReason = "MMP log fields must expose their retained count and clear only their addressed ring.";
		return false;
	}
	if (!mrvmModelessUiReadWindowSelectFieldValue(canvasRuntimeKv, canvasWindow.windowId, "MODE", selectFieldValue) || selectFieldValue != "Normal" || !mrvmModelessUiStoreWindowSelectFieldValue(canvasRuntimeKv, canvasWindow.windowId, "MODE", "Safe") || !mrvmModelessUiReadWindowSelectFieldValue(canvasRuntimeKv, canvasWindow.windowId, "MODE", selectFieldValue) || selectFieldValue != "Safe" || mrvmModelessUiStoreWindowSelectFieldValue(canvasRuntimeKv, canvasWindow.windowId, "MODE", "Unknown")) {
		failureReason = "MMP selection fields must retain only their declared option values through the window model.";
		return false;
	}
	selectValueArgs.push_back(mrvmMakeString(canvasWindow.windowId));
	selectValueArgs.push_back(mrvmMakeString("MODE"));
	if (!mrvmDispatchMacroModelessIntrinsic(canvasRuntimeKv, "MMP_SELECT_VALUE", selectValueArgs, selectValueResult) || selectValueResult.type != TYPE_STR || selectValueResult.s != "Safe") {
		failureReason = "MMP selection fields must expose their retained value as a string intrinsic.";
		return false;
	}
	invalidSelectSetArgs.push_back(mrvmMakeString(canvasWindow.windowId));
	invalidSelectSetArgs.push_back(mrvmMakeString("MODE"));
	invalidSelectSetArgs.push_back(mrvmMakeString("Unknown"));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_SELECT_SET", invalidSelectSetArgs, invalidSelectSetReturnValue, invalidSelectSetError) || invalidSelectSetReturnValue != 0 || invalidSelectSetError != "MMP_SELECT_SET value is not a declared option.") {
		failureReason = "MMP selection updates must reject an undeclared option value.";
		return false;
	}
	unknownTextSetArgs.push_back(mrvmMakeString(canvasWindow.windowId));
	unknownTextSetArgs.push_back(mrvmMakeString("Unknown"));
	unknownTextSetArgs.push_back(mrvmMakeString("Updated"));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_TEXT_SET", unknownTextSetArgs, unknownTextSetReturnValue, unknownTextSetError) || unknownTextSetReturnValue != 0 || unknownTextSetError != "MMP_TEXT_SET target does not exist.") {
		failureReason = "MMP text updates must reject an unknown logical field id.";
		return false;
	}
	unknownBoolSetArgs.push_back(mrvmMakeString(canvasWindow.windowId));
	unknownBoolSetArgs.push_back(mrvmMakeString("Unknown"));
	unknownBoolSetArgs.push_back(mrvmMakeInt(1));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_BOOL_SET", unknownBoolSetArgs, unknownBoolSetReturnValue, unknownBoolSetError) || unknownBoolSetReturnValue != 0 || unknownBoolSetError != "MMP_BOOL_SET target does not exist.") {
		failureReason = "MMP boolean updates must reject an unknown logical field id.";
		return false;
	}
	unknownIntSetArgs.push_back(mrvmMakeString(canvasWindow.windowId));
	unknownIntSetArgs.push_back(mrvmMakeString("Unknown"));
	unknownIntSetArgs.push_back(mrvmMakeInt(5));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_INT_SET", unknownIntSetArgs, unknownIntSetReturnValue, unknownIntSetError) || unknownIntSetReturnValue != 0 || unknownIntSetError != "MMP_INT_SET target does not exist.") {
		failureReason = "MMP integer updates must reject an unknown logical field id.";
		return false;
	}
	unknownProgressSetArgs.push_back(mrvmMakeString(canvasWindow.windowId));
	unknownProgressSetArgs.push_back(mrvmMakeString("Unknown"));
	unknownProgressSetArgs.push_back(mrvmMakeInt(50));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_PROGRESS_SET", unknownProgressSetArgs, unknownProgressSetReturnValue, unknownProgressSetError) || unknownProgressSetReturnValue != 0 || unknownProgressSetError != "MMP_PROGRESS_SET target does not exist.") {
		failureReason = "MMP progress updates must reject an unknown logical field id.";
		return false;
	}
	unknownLogAppendArgs.push_back(mrvmMakeString(canvasWindow.windowId));
	unknownLogAppendArgs.push_back(mrvmMakeString("Unknown"));
	unknownLogAppendArgs.push_back(mrvmMakeString("Updated"));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_LOG_APPEND", unknownLogAppendArgs, unknownLogAppendReturnValue, unknownLogAppendError) || unknownLogAppendReturnValue != 0 || unknownLogAppendError != "MMP_LOG_APPEND target does not exist.") {
		failureReason = "MMP log updates must reject an unknown logical log id.";
		return false;
	}
	unknownSelectSetArgs.push_back(mrvmMakeString(canvasWindow.windowId));
	unknownSelectSetArgs.push_back(mrvmMakeString("Unknown"));
	unknownSelectSetArgs.push_back(mrvmMakeString("Normal"));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_SELECT_SET", unknownSelectSetArgs, unknownSelectSetReturnValue, unknownSelectSetError) || unknownSelectSetReturnValue != 0 || unknownSelectSetError != "MMP_SELECT_SET target does not exist.") {
		failureReason = "MMP selection updates must reject an unknown logical field id.";
		return false;
	}
	unknownStatusSetArgs.push_back(mrvmMakeString(canvasWindow.windowId));
	unknownStatusSetArgs.push_back(mrvmMakeString("Unknown"));
	unknownStatusSetArgs.push_back(mrvmMakeString("Updated"));
	if (!mrvmDispatchMacroModelessProcedure(canvasRuntimeKv, "MMP_STATUS_SET", unknownStatusSetArgs, unknownStatusSetReturnValue, unknownStatusSetError) || unknownStatusSetReturnValue != 0 || unknownStatusSetError != "MMP_STATUS_SET target does not exist.") {
		failureReason = "MMP status updates must reject an unknown logical status id.";
		return false;
	}
	canvasCommand.type = MRMacroModelessCanvasCommandType::Text;
	canvasCommand.x = 1;
	canvasCommand.y = 1;
	canvasCommand.style = 1;
	canvasCommand.text = "retained";
	if (!mrvmModelessUiCanvasClear(canvasRuntimeKv, canvasWindow.windowId, canvas.canvasId, 0) || !mrvmModelessUiCanvasAppendCommand(canvasRuntimeKv, canvasWindow.windowId, canvas.canvasId, canvasCommand) || !mrvmModelessUiCommitCanvas(canvasRuntimeKv, canvasWindow.windowId, canvas.canvasId) || !mrvmModelessUiReadCanvasScene(canvasRuntimeKv, canvasWindow.windowId, canvas.canvasId, canvasScene)) {
		failureReason = "MMP canvas scene operations must use the retained MODELESSUI canvas.";
		return false;
	}
	if (canvasScene.generation != 1 || canvasScene.commands.size() != 2 || canvasScene.commands[1].type != MRMacroModelessCanvasCommandType::Text || canvasScene.commands[1].style != 1 || canvasScene.commands[1].text != "retained") {
		failureReason = "MMP canvas commit must preserve a typed retained scene.";
		return false;
	}
	desktopState.virtualDesktop = 3;
	desktopState.manuallyHidden = true;
	desktopState.minimized = true;
	desktopState.bufferedBeforeMinimize = true;
	desktopState.restoreX = 2;
	desktopState.restoreY = 3;
	desktopState.restoreWidth = 40;
	desktopState.restoreHeight = 12;
	desktopState.lastMinimizedX = 4;
	desktopState.lastMinimizedY = 20;
	desktopState.lastMinimizedWidth = 18;
	desktopState.lastMinimizedHeight = 1;
	desktopState.assigned = true;
	mrvmModelessUiStoreWindowDesktopState(canvasRuntimeKv, canvasWindow.windowId, desktopState);
	if (!mrvmModelessUiReadWindowDesktopState(canvasRuntimeKv, canvasWindow.windowId, storedDesktopState) || storedDesktopState.virtualDesktop != 3 || !storedDesktopState.manuallyHidden || !storedDesktopState.minimized || !storedDesktopState.bufferedBeforeMinimize || storedDesktopState.restoreX != 2 || storedDesktopState.restoreY != 3 || storedDesktopState.restoreWidth != 40 || storedDesktopState.restoreHeight != 12 || storedDesktopState.lastMinimizedX != 4 || storedDesktopState.lastMinimizedY != 20 || storedDesktopState.lastMinimizedWidth != 18 || storedDesktopState.lastMinimizedHeight != 1 || !storedDesktopState.assigned) {
		failureReason = "MMP desktop, minimize and restore state must remain in the MODELESSUI window model.";
		return false;
	}
	if (mrvmModelessUiCreateWindowInstanceId(canvasRuntimeKv, "MMP") == mrvmModelessUiCreateWindowInstanceId(canvasRuntimeKv, "MMP")) {
		failureReason = "MMP window instance ids must be unique within the runtime.";
		return false;
	}
	if (!compileBytecode(kMmpSource, mmpBytecode, compileError)) {
		failureReason = "MMP canvas procedures must compile: " + compileError;
		return false;
	}
	mmpProfile = mrvmAnalyzeBytecode(mmpBytecode.data(), mmpBytecode.size());
	if (std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_CANVAS") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_ACTION_BUTTON") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_MENU_CLEAR") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_MENU_ITEM") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_ACTION_MENU") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_TEXT_FIELD") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_TEXT_SET") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_TEXT_VALUE") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_BOOL_FIELD") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_BOOL_SET") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_BOOL_VALUE") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_INT_FIELD") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_INT_SET") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_INT_VALUE") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_PROGRESS_FIELD") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_PROGRESS_SET") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_PROGRESS_VALUE") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_LOG_FIELD") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_LOG_APPEND") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_LOG_CLEAR") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_LOG_COUNT") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_SELECT_FIELD") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_SELECT_OPTION") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_SELECT_SET") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_SELECT_VALUE") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_STATUS_FIELD") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_STATUS_SET") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_TIMER_START") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_TIMER_STOP") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_WINDOW_WIDTH") == mmpProfile.uiAffinitySymbols.end() || std::find(mmpProfile.uiAffinitySymbols.begin(), mmpProfile.uiAffinitySymbols.end(), "MMP_WINDOW_INSTANCE") == mmpProfile.uiAffinitySymbols.end()) {
		failureReason = "MMP canvas, action-button, action-menu, form-field, status and timer primitives must remain UI-affine.";
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("mrmac/macros/utils/MmpCanvasDemo.mrmac"), sourceText, ioError) || !compileBytecode(sourceText, mmpBytecode, compileError)) {
		failureReason = "MMP canvas demonstration macro must compile";
		if (!ioError.empty()) failureReason += ": " + ioError;
		else if (!compileError.empty()) failureReason += ": " + compileError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("mrmac/vm/MRVMMacroRuntime.cpp"), sourceText, ioError)) {
		failureReason = "Unable to read MRVMMacroRuntime.cpp for modeless callback session guard: " + ioError;
		return false;
	}
	const std::size_t callbackStart = sourceText.find("void runMacroModelessCommand");
	const std::size_t callbackEnd = callbackStart == std::string::npos ? std::string::npos : sourceText.find("void showMacroModelessDialog", callbackStart);
	const std::string callbackSource = callbackStart == std::string::npos ? std::string() : sourceText.substr(callbackStart, callbackEnd == std::string::npos ? std::string::npos : callbackEnd - callbackStart);
	if (callbackSource.find("runMacroSpecByNameAsExecutionSessionForOwner") == std::string::npos || callbackSource.find("owner.modelessWindowId = windowId") == std::string::npos || callbackSource.find("mrvmRunMacroSpec(macroSpec") != std::string::npos) {
		failureReason = "Modeless callbacks must enter through a window-owned execution session.";
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("app/commands/MRWindowCommands.cpp"), sourceText, ioError) || sourceText.find("allDesktopWindowsInZOrder") == std::string::npos || sourceText.find("MRDesktopWindow") == std::string::npos || sourceText.find("MRMacroModeless") != std::string::npos) {
		failureReason = "Desktop window commands must use the shared desktop-window role without MMP special cases.";
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRFrame.cpp"), sourceText, ioError) || sourceText.find("desktopShowsFrameGrowHandle") == std::string::npos || sourceText.find("minimizedLayout(desktopWindow") == std::string::npos) {
		failureReason = "MRFrame must use the shared desktop-window role for MMP minimize and grow-glyph behavior.";
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("mrmac/ui/modeless/MRMacroModelessUi.cpp"), sourceText, ioError) || sourceText.find("if (event.what == evMouseDown)") == std::string::npos || sourceText.find("TProgram::deskTop->setCurrent(this, TView::normalSelect)") == std::string::npos || sourceText.find("frame->drawView()") == std::string::npos || sourceText.find("runCanvasHotspot(makeLocal(event.mouse.where))") == std::string::npos || sourceText.find("runModelessMacro(definition.windowId, hotspot.macroSpec)") == std::string::npos || sourceText.find("removeRuntimeScheduledConsumersForOwner(executionOwner)") == std::string::npos || sourceText.find("requestMacroExecutionCancellationForOwner(executionOwner)") == std::string::npos) {
		failureReason = "MMP clicks, hotspots and close cleanup must remain window-owned.";
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("mrmac/ui/modeless/MRVMMacroModelessProcedures.cpp"), sourceText, ioError) || sourceText.find("mrvmAddMacroUiGrid(runtimeKv, gridArgs)") == std::string::npos || sourceText.find("mrvmBindMacroModelessButton(runtimeKv, bindingArgs)") == std::string::npos || sourceText.find("mrvmModelessUiReadWindowTextFieldValue") == std::string::npos || sourceText.find("updateMacroModelessTextField(windowId, fieldId") == std::string::npos || sourceText.find("mrvmModelessUiReadWindowBoolFieldValue") == std::string::npos || sourceText.find("updateMacroModelessBoolField(windowId, fieldId") == std::string::npos || sourceText.find("mrvmModelessUiReadWindowIntFieldValue") == std::string::npos || sourceText.find("updateMacroModelessIntField(windowId, fieldId") == std::string::npos || sourceText.find("mrvmModelessUiReadWindowProgressFieldValue") == std::string::npos || sourceText.find("updateMacroModelessProgressField(windowId, fieldId") == std::string::npos || sourceText.find("mrvmModelessUiReadWindowSelectFieldValue") == std::string::npos || sourceText.find("updateMacroModelessSelectField(windowId, fieldId") == std::string::npos || sourceText.find("mrvmModelessUiReadWindowStatusDisplayIndex") == std::string::npos || sourceText.find("updateMacroModelessDisplay(windowId, displayIndex") == std::string::npos) {
		failureReason = "MMP actions, form fields and status updates must compose their existing modeless routes.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testRuntimeSchedulerSkipEventGuard(std::string &failureReason) {
	static constexpr MRMacroExecutionSessionId kSessionId = 700301;
	MRRuntimeScheduledConsumerConfig invalidConfig;
	MRRuntimeScheduledConsumerConfig config;
	MRRuntimeScheduledConsumerId consumerId = 0;
	MRMacroExecutionSessionId blockingSessionId = 0;
	std::vector<MRRuntimeSchedulerEvent> events;
	std::vector<std::string> lines;
	bool sawStarted = false;
	bool sawSkipped = false;
	bool sawFinished = false;
	bool sawDue = false;
	bool sawDispatchResult = false;
	bool sawSkippedLine = false;
	bool sawSourcePackageLine = false;
	bool sawConsumerKeyLine = false;

	invalidConfig.intervalMs = 0;
	invalidConfig.macroSpec = "runtime-scheduler-invalid";
	if (registerRuntimeScheduledConsumer(invalidConfig) != 0) {
		failureReason = "Runtime scheduler must reject zero interval consumers.";
		return false;
	}

	config.owner.hasBuffer = true;
	config.owner.bufferId = 41;
	config.intervalMs = 1000;
	config.macroSpec = "runtime-scheduler-overrun-skip";
	config.macroSource = "$MACRO RuntimeSchedulerRegression;\nDEF_INT(ProbeValue);\nProbeValue := GLOBAL_INT('RUNTIME_SCHEDULER_REGRESSION');\nEND_MACRO;\n";
	config.consumerKey = "SCHEDULER-KEY";
	config.overrunPolicy = MRRuntimeScheduleOverrunPolicy::Skip;
	consumerId = registerRuntimeScheduledConsumer(config);
	if (consumerId == 0) {
		failureReason = "Runtime scheduler must register a valid scheduled consumer.";
		return false;
	}
	if (!noteRuntimeScheduledConsumerStarted(consumerId, kSessionId)) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler must mark a consumer session as started.";
		return false;
	}
	if (runtimeScheduledConsumerTickMayStart(consumerId, &blockingSessionId)) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler skip policy must reject overrun ticks.";
		return false;
	}
	if (blockingSessionId != kSessionId) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler skip policy must report the blocking session id.";
		return false;
	}

	events = recentRuntimeSchedulerEvents();
	for (const MRRuntimeSchedulerEvent &event : events)
		if (event.consumerId == consumerId) {
			if (event.kind == MRRuntimeSchedulerEventKind::TickStarted && event.sessionId == kSessionId) sawStarted = true;
			if (event.kind == MRRuntimeSchedulerEventKind::TickSkipped && event.blockingSessionId == kSessionId && event.skipReason == MRRuntimeSchedulerSkipReason::PreviousSessionStillActive) sawSkipped = true;
		}
	if (!sawStarted || !sawSkipped) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler must record started and skipped events for debugger/status consumers.";
		return false;
	}
	{
		MRMacroExecutionSession session;

		session.sessionId = kSessionId;
		session.taskId = kSessionId;
		session.owner = config.owner;
		session.route = MRMacroExecutionRoute::Background;
		session.state = MRMacroExecutionState::Running;
		trackMacroExecutionSession(session);
		if (!publishMacroExecutionResultForTask(kSessionId, MRMacroExecutionState::Completed, "scheduler regression completed")) {
			removeRuntimeScheduledConsumer(consumerId);
			failureReason = "Runtime scheduler regression must publish the active execution-session result.";
			return false;
		}
	}
	events = recentRuntimeSchedulerEvents();
	for (const MRRuntimeSchedulerEvent &event : events)
		if (event.consumerId == consumerId && event.kind == MRRuntimeSchedulerEventKind::TickFinished && event.sessionId == kSessionId) sawFinished = true;
	if (!sawFinished) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler must record finished events from execution-session terminal results.";
		return false;
	}
	if (!runtimeScheduledConsumerTickMayStart(consumerId, &blockingSessionId)) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler must release active consumers after execution-session terminal results.";
		return false;
	}

	lines = runtimeSchedulerStatusLines(8);
	for (const std::string &line : lines) {
		if (line.find("tick-skipped") != std::string::npos && line.find("blocking-session #700301") != std::string::npos) sawSkippedLine = true;
		if (line.find("source-package") != std::string::npos) sawSourcePackageLine = true;
		if (line.find("key='SCHEDULER-KEY'") != std::string::npos) sawConsumerKeyLine = true;
	}
	if (!sawSkippedLine) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler status lines must expose skipped ticks with blocking session id.";
		return false;
	}
	if (!sawSourcePackageLine) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler status lines must expose source-package scheduled consumers.";
		return false;
	}
	if (!sawConsumerKeyLine) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler status lines must expose logical consumer keys.";
		return false;
	}
	if (!noteRuntimeScheduledConsumerStarted(consumerId, kSessionId)) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler must mark a consumer session as started after automatic finish.";
		return false;
	}
	if (noteRuntimeScheduledConsumerFinished(consumerId, kSessionId + 1)) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler must reject finishing a non-active session.";
		return false;
	}
	if (!noteRuntimeScheduledConsumerFinished(consumerId, kSessionId)) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler must clear the active session on matching finish.";
		return false;
	}
	if (!runtimeScheduledConsumerTickMayStart(consumerId, &blockingSessionId)) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler must allow a tick after the previous session finished.";
		return false;
	}
	if (pumpRuntimeScheduler(1000000) == 0) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler pump must emit a due event for an idle due consumer.";
		return false;
	}
	if (pumpRuntimeScheduler(1000500) != 0) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler pump must not emit a due event before interval expiry.";
		return false;
	}
	events = recentRuntimeSchedulerEvents();
	for (const MRRuntimeSchedulerEvent &event : events)
		if (event.consumerId == consumerId) {
			if (event.kind == MRRuntimeSchedulerEventKind::TickDue && event.dueAtMs == 1000000 && event.observedAtMs == 1000000) sawDue = true;
			if (event.kind == MRRuntimeSchedulerEventKind::TickStarted && event.sessionId != 0) sawDispatchResult = true;
		}
	if (!sawDue) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler pump must record due and observed timestamps.";
		return false;
	}
	if (!sawDispatchResult) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime scheduler pump must record accepted source-package dispatch results.";
		return false;
	}
	if (runtimeTimerSourceNowMs() == 0) {
		removeRuntimeScheduledConsumer(consumerId);
		failureReason = "Runtime timer source must provide monotonic millisecond time.";
		return false;
	}
	if (!removeRuntimeScheduledConsumer(consumerId) || removeRuntimeScheduledConsumer(consumerId)) {
		failureReason = "Runtime scheduler consumer removal must be acknowledged once.";
		return false;
	}
	{
		MRMacroExecutionOwner modelessOwner;
		MRMacroExecutionOwner otherModelessOwner;
		MRRuntimeScheduledConsumerConfig modelessConfig;
		MRRuntimeScheduledConsumerId refreshConsumerId = 0;
		MRRuntimeScheduledConsumerId keepConsumerId = 0;
		MRRuntimeScheduledConsumerId otherConsumerId = 0;

		modelessOwner.modelessWindowId = "REGRESSION-MMP-TIMER";
		otherModelessOwner.modelessWindowId = "REGRESSION-MMP-OTHER";
		modelessConfig.owner = modelessOwner;
		modelessConfig.intervalMs = 100;
		modelessConfig.macroSpec = "RegressionMmp^Refresh";
		modelessConfig.consumerKey = "Refresh";
		refreshConsumerId = registerRuntimeScheduledConsumer(modelessConfig);
		modelessConfig.consumerKey = "Keep";
		keepConsumerId = registerRuntimeScheduledConsumer(modelessConfig);
		modelessConfig.owner = otherModelessOwner;
		modelessConfig.consumerKey = "Refresh";
		otherConsumerId = registerRuntimeScheduledConsumer(modelessConfig);
		if (refreshConsumerId == 0 || keepConsumerId == 0 || otherConsumerId == 0) {
			removeRuntimeScheduledConsumer(refreshConsumerId);
			removeRuntimeScheduledConsumer(keepConsumerId);
			removeRuntimeScheduledConsumer(otherConsumerId);
			failureReason = "Runtime scheduler must register logical MMP timer consumers.";
			return false;
		}
		if (removeRuntimeScheduledConsumersForOwnerAndKey(modelessOwner, "Refresh") != 1) {
			removeRuntimeScheduledConsumer(keepConsumerId);
			removeRuntimeScheduledConsumer(otherConsumerId);
			failureReason = "MMP timer stop must remove only its owner's named consumer.";
			return false;
		}
		if (removeRuntimeScheduledConsumersForOwner(modelessOwner) != 1) {
			removeRuntimeScheduledConsumer(otherConsumerId);
			failureReason = "MMP window close must remove all remaining owner consumers.";
			return false;
		}
		if (removeRuntimeScheduledConsumersForOwner(otherModelessOwner) != 1) {
			failureReason = "MMP timer ownership must not remove a different modeless window's consumers.";
			return false;
		}
	}

	failureReason.clear();
	return true;
}

bool testExecSessionKvAccessBoundaryGuard(std::string &failureReason) {
	const std::filesystem::path rootPath = std::filesystem::current_path();
	static const char *kSourceRoots[] = {"app", "config", "coprocessor", "dialogs", "diff", "keymap", "mrmac", "piecetable", "ui"};
	static const char *kAllowedDirectKvFiles[] = {"mrmac/MRVM.cpp", "mrmac/vm/MRVMExecSessions.cpp", "mrmac/macros/utils/ExecSessionConsole.mrmac"};
	static const char *kDirectKvNeedles[] = {"\"EXECSESSIONS\"", "'EXECSESSIONS'"};

	for (const char *sourceRoot : kSourceRoots) {
		const std::filesystem::path scanRoot = rootPath / sourceRoot;
		std::error_code errorCode;
		std::filesystem::recursive_directory_iterator it(scanRoot, std::filesystem::directory_options::skip_permission_denied, errorCode);
		std::filesystem::recursive_directory_iterator end;
		if (errorCode) {
			failureReason = "Unable to scan source root for exec-session K/V access guard: " + scanRoot.string();
			return false;
		}
		for (; it != end; it.increment(errorCode)) {
			if (errorCode) {
				failureReason = "Unable to advance source scan for exec-session K/V access guard: " + errorCode.message();
				return false;
			}
			if (!it->is_regular_file(errorCode)) continue;
			if (errorCode) {
				failureReason = "Unable to inspect source entry for exec-session K/V access guard: " + errorCode.message();
				return false;
			}
			const std::filesystem::path path = it->path();
			const std::string extension = path.extension().string();
			if (extension != ".cpp" && extension != ".hpp" && extension != ".c" && extension != ".h" && extension != ".mrmac") continue;

			const std::string relativePath = std::filesystem::relative(path, rootPath, errorCode).generic_string();
			if (errorCode) {
				failureReason = "Unable to compute relative path for exec-session K/V access guard: " + path.string();
				return false;
			}
			bool allowed = false;
			for (const char *allowedPath : kAllowedDirectKvFiles)
				if (relativePath == allowedPath) {
					allowed = true;
					break;
				}
			if (allowed) continue;

			std::string content;
			std::string ioError;
			if (!readTextFile(path.string(), content, ioError)) {
				failureReason = "Unable to read source for exec-session K/V access guard: " + ioError;
				return false;
			}
			for (const char *needle : kDirectKvNeedles)
				if (content.find(needle) != std::string::npos) {
					failureReason = "Non exec-session code must not access EXECSESSIONS K/V contents directly: " + relativePath + " contains " + needle + ".";
					return false;
				}
		}
	}

	failureReason.clear();
	return true;
}

bool testCentralRuntimeKvAuthorityGuard(std::string &failureReason) {
	struct RequiredSource {
		const char *path;
		const char *const *needles;
		std::size_t needleCount;
	};
	static const char *kSettingsRuntimeNeedles[] = {"ensureRoot(\"SETTINGS\")", "ensureChild(settings, \"runtime\")"};
	static const char *kSettingsHistoryNeedles[] = {"ensureRoot(\"SETTINGS\")", "ensureChild(settings, \"history\")"};
	static const char *kSettingsStagingNeedles[] = {"ensureRoot(\"SETTINGS\")", "ensureChild(runtimeKv.ensureRoot(\"SETTINGS\"), \"staging\")"};
	static const char *kApplicationUiNeedles[] = {"ensureRoot(kApplicationUiRoot)", "ensureChild(applicationUi, branch)"};
	static const char *kWorkspaceNeedles[] = {"kWorkspaceBranch, \"autosaveDirty\"", "kWorkspaceBranch, \"autosaveDueMs\""};
	static const char *kSearchNeedles[] = {"kApplicationUiRoot = \"APPLICATIONUI\"", "kSearchBranch = \"search\""};
	static const char *kMessageLineNeedles[] = {
	    "kApplicationUiRoot = \"APPLICATIONUI\"",
	    "kMessageLineBranch = \"messageLine\"",
	    "\"staticMode\"",
	    "\"staticProgressCompleted\"",
	    "\"staticProgressTotal\"",
	};
	static const char *kKeymapNeedles[] = {"ensureRoot(\"KEYMAP\")", "ensureChild(runtime, \"pending\")"};
	static const char *kDeferredUiNeedles[] = {"ensureRoot(\"DEFERREDUI\")", "ensureChild(deferredPlaybackRoot(runtimeKv), \"playbackQueue\")"};
	static const char *kMrmacRuntimeNeedles[] = {"ensureRoot(\"MRMACRUNTIME\")"};
	static const char *kMacroScreenNeedles[] = {"mrvmRuntimeStateSize(\"macroScreen\", \"mutationEpoch\", 1)", "mrvmStoreRuntimeStateString(\"macroScreen\", \"cells\""};
	static const RequiredSource kRequiredSources[] = {
	    {"config/settings/MRSettingsRuntimeState.cpp", kSettingsRuntimeNeedles, sizeof(kSettingsRuntimeNeedles) / sizeof(kSettingsRuntimeNeedles[0])},
	    {"config/settings/MRSettingsHistory.cpp", kSettingsHistoryNeedles, sizeof(kSettingsHistoryNeedles) / sizeof(kSettingsHistoryNeedles[0])},
	    {"config/settings/MRSettingsStructuredStorage.cpp", kSettingsStagingNeedles, sizeof(kSettingsStagingNeedles) / sizeof(kSettingsStagingNeedles[0])},
	    {"app/commands/MRWindowRuntimeState.cpp", kApplicationUiNeedles, sizeof(kApplicationUiNeedles) / sizeof(kApplicationUiNeedles[0])},
	    {"app/commands/MRWorkspaceRuntime.cpp", kWorkspaceNeedles, sizeof(kWorkspaceNeedles) / sizeof(kWorkspaceNeedles[0])},
	    {"app/router/MRCommandRouterSearchState.cpp", kSearchNeedles, sizeof(kSearchNeedles) / sizeof(kSearchNeedles[0])},
	    {"ui/MRMessageLineController.cpp", kMessageLineNeedles, sizeof(kMessageLineNeedles) / sizeof(kMessageLineNeedles[0])},
	    {"keymap/MRKeymapResolver.cpp", kKeymapNeedles, sizeof(kKeymapNeedles) / sizeof(kKeymapNeedles[0])},
	    {"coprocessor/MRCoprocessorDeferredPlayback.cpp", kDeferredUiNeedles, sizeof(kDeferredUiNeedles) / sizeof(kDeferredUiNeedles[0])},
	    {"mrmac/vm/MRVMRuntimeState.cpp", kMrmacRuntimeNeedles, sizeof(kMrmacRuntimeNeedles) / sizeof(kMrmacRuntimeNeedles[0])},
	    {"mrmac/ui/conventional/MRVMScreenState.cpp", kMacroScreenNeedles, sizeof(kMacroScreenNeedles) / sizeof(kMacroScreenNeedles[0])},
	};
	static const char *kSourceRoots[] = {"app", "config", "coprocessor", "dialogs", "keymap", "mrmac", "ui"};
	static const char *kForbiddenSemanticStores[] = {
	    "RuntimeEnvironment",
	    "g_runtimeEnv",
	    "g_workspaceAutosaveDirty",
	    "g_workspaceAutosaveDue",
	    "g_deferredMacroUiPlaybackQueue",
	    "g_macroCellGrid",
	    "g_macroScreenLineColOverlay",
	    "g_screenStateCoordinator",
	    "execSessionStatusConsumerGenerationValue",
	};
	const std::filesystem::path rootPath = std::filesystem::current_path();

	for (const RequiredSource &required : kRequiredSources) {
		std::string content;
		std::string ioError;
		if (!readTextFile((rootPath / required.path).string(), content, ioError)) {
			failureReason = "Unable to read central runtime K/V authority source " + std::string(required.path) + ": " + ioError;
			return false;
		}
		for (std::size_t index = 0; index < required.needleCount; ++index)
			if (content.find(required.needles[index]) == std::string::npos) {
				failureReason = "Central runtime K/V authority guard rejected " + std::string(required.path) + ": missing " + required.needles[index] + ".";
				return false;
			}
	}

	for (const char *sourceRoot : kSourceRoots) {
		std::error_code errorCode;
		std::filesystem::recursive_directory_iterator it(rootPath / sourceRoot, std::filesystem::directory_options::skip_permission_denied, errorCode);
		const std::filesystem::recursive_directory_iterator end;

		if (errorCode) {
			failureReason = "Unable to scan central runtime K/V source root " + std::string(sourceRoot) + ".";
			return false;
		}
		for (; it != end; it.increment(errorCode)) {
			if (errorCode) {
				failureReason = "Unable to advance central runtime K/V source scan: " + errorCode.message();
				return false;
			}
			if (!it->is_regular_file(errorCode)) continue;
			const std::string extension = it->path().extension().string();
			if (extension != ".cpp" && extension != ".hpp" && extension != ".c" && extension != ".h") continue;
			std::string content;
			std::string ioError;
			if (!readTextFile(it->path().string(), content, ioError)) {
				failureReason = "Unable to read central runtime K/V source: " + ioError;
				return false;
			}
			for (const char *forbidden : kForbiddenSemanticStores)
				if (content.find(forbidden) != std::string::npos) {
					failureReason = "Parallel semantic runtime store remains in " + std::filesystem::relative(it->path(), rootPath).generic_string() + ": " + forbidden + ".";
					return false;
				}
		}
	}
	{
		using namespace mr::messageline;
		VisibleMessage visible;

		setRuntimeMessageLineEnabled(true);
		setStaticMode(false);
		for (std::size_t index = 0; index < static_cast<std::size_t>(Owner::Count); ++index)
			clearOwner(static_cast<Owner>(index));
		for (std::size_t index = 0; index < static_cast<std::size_t>(Owner::Count); ++index) {
			const Owner owner = static_cast<Owner>(index);
			const std::string ownerMessage = "owner " + std::to_string(index);
			const Token token = postSticky(owner, ownerMessage, Kind::Info, kPriorityLow);
			const bool visibleForOwner = currentOwnerMessage(owner, visible) && visible.text == ownerMessage;

			clearOwner(owner);
			if (token == 0 || !visibleForOwner) {
				failureReason = "Message-line runtime K/V rejected or failed to expose a declared owner.";
				return false;
			}
		}
		if (postSticky(Owner::Count, "invalid owner", Kind::Info, kPriorityLow) != 0) {
			failureReason = "Message-line runtime K/V accepted the owner-count sentinel as an owner.";
			return false;
		}
	}

	failureReason.clear();
	return true;
}

bool testExecSessionRuntimeStoreBoundaryGuard(std::string &failureReason) {
	const std::filesystem::path rootPath = std::filesystem::current_path();
	struct ScannedFile {
		const char *path;
		const char *const *allowedStores;
		std::size_t allowedStoreCount;
	};
	static const char *kSessionSourceAllowed[] = {"std::vector<MacroExecutionSessionListener> macroExecutionSessionListeners;"};
	static const char *kMacroRunnerAllowed[] = {"std::vector<PendingForegroundMacro> pendingForegroundMacros;"};
	static const char *kModelessUiAllowed[] = {"std::map<std::string, class MRMacroModelessWindow *> g_windows;"};
	static const ScannedFile kFiles[] = {
	    {"mrmac/MRMacroExecutionSession.cpp", kSessionSourceAllowed, sizeof(kSessionSourceAllowed) / sizeof(kSessionSourceAllowed[0])},
	    {"mrmac/MRMacroRunner.cpp", kMacroRunnerAllowed, sizeof(kMacroRunnerAllowed) / sizeof(kMacroRunnerAllowed[0])},
	    {"app/MRRuntimeScheduler.cpp", nullptr, 0},
	    {"mrmac/ui/modeless/MRMacroModelessUi.cpp", kModelessUiAllowed, sizeof(kModelessUiAllowed) / sizeof(kModelessUiAllowed[0])},
	};
	static const char *kForbiddenStoreNeedles[] = {"std::map<", "std::unordered_map<", "std::vector<", "std::deque<", "std::list<", "std::set<", "std::unordered_set<"};
	const std::size_t fileCount = sizeof(kFiles) / sizeof(kFiles[0]);
	const std::size_t forbiddenStoreNeedleCount = sizeof(kForbiddenStoreNeedles) / sizeof(kForbiddenStoreNeedles[0]);

	for (std::size_t fileIndex = 0; fileIndex < fileCount; ++fileIndex) {
		const ScannedFile &file = kFiles[fileIndex];
		std::string content;
		std::string ioError;
		if (!readTextFile((rootPath / file.path).string(), content, ioError)) {
			failureReason = "Unable to read source for exec-session runtime store guard: " + ioError;
			return false;
		}
		std::istringstream lines(content);
		std::string line;
		int lineNumber = 0;
		while (std::getline(lines, line)) {
			++lineNumber;
			if (line.empty() || std::isspace(static_cast<unsigned char>(line[0]))) continue;
			if (line.find('(') != std::string::npos || line.find(';') == std::string::npos) continue;
			bool hasForbiddenStore = false;
			for (std::size_t needleIndex = 0; needleIndex < forbiddenStoreNeedleCount; ++needleIndex) {
				const char *needle = kForbiddenStoreNeedles[needleIndex];
				if (line.find(needle) != std::string::npos) {
					hasForbiddenStore = true;
					break;
				}
			}
			if (!hasForbiddenStore) continue;
			bool allowed = false;
			for (std::size_t index = 0; index < file.allowedStoreCount; ++index) {
				if (line.find(file.allowedStores[index]) != std::string::npos) {
					allowed = true;
					break;
				}
			}
			if (allowed) continue;
			failureReason = "Exec-session/modeless runtime store guard rejected top-level store in " + std::string(file.path) + ":" + std::to_string(lineNumber) + ": " + line;
			return false;
		}
	}
	{
		std::string vmSource;
		std::string modelessUiRuntimeSource;
		std::string ioError;
		if (!readTextFile((rootPath / "mrmac/MRVM.cpp").string(), vmSource, ioError)) {
			failureReason = "Unable to read MRVM.cpp for modeless UI staging store guard: " + ioError;
			return false;
		}
		if (!readTextFile((rootPath / "mrmac/ui/modeless/MRVMModelessUiRuntime.cpp").string(), modelessUiRuntimeSource, ioError)) {
			failureReason = "Unable to read MRVMModelessUiRuntime.cpp for modeless UI staging store guard: " + ioError;
			return false;
		}
		if (vmSource.find("g_macroUiDialog") != std::string::npos || vmSource.find("g_macroUiItemLists") != std::string::npos || modelessUiRuntimeSource.find("g_macroUiDialog") != std::string::npos || modelessUiRuntimeSource.find("g_macroUiItemLists") != std::string::npos) {
			failureReason = "Modeless UI staging data must live under MODELESSUI/staging K/V, not in MRVM.cpp store globals.";
			return false;
		}
		if (vmSource.find("readGlobalValue(\"EXECSESSIONS\"") != std::string::npos || vmSource.find("readGlobalValue(\"MODELESSUI\"") != std::string::npos) {
			failureReason = "EXECSESSIONS and MODELESSUI root reads must go through MRVM.cpp K/V root accessors.";
			return false;
		}
		if (modelessUiRuntimeSource.find("ensureModelessUiChildPath") == std::string::npos || modelessUiRuntimeSource.find("\"MODELESSUI\"") == std::string::npos || modelessUiRuntimeSource.find("\"staging\"") == std::string::npos) {
			failureReason = "Modeless UI staging guard expects MODELESSUI/staging K/V accessors in MRVMModelessUiRuntime.cpp.";
			return false;
		}
		if (vmSource.find("findExecSessionsChild") == std::string::npos || vmSource.find("mrvmModelessUiReadItemList") == std::string::npos) {
			failureReason = "MRVM.cpp must access EXECSESSIONS and MODELESSUI through runtime K/V module functions.";
			return false;
		}
	}

	failureReason.clear();
	return true;
}

bool runMacroDebuggerBreakpointKvProbe(std::string &failureReason) {
	static const char kSource[] = "$MACRO BreakpointProbe;\n"
	                              "DEF_INT(X);\n"
	                              "X := 1;\n"
	                              "X := X + 1;\n"
	                              "END_MACRO;\n";
	static const int kBreakpointLine = 3;
	static const int kDisabledBreakpointLine = 4;
	static const char kMacroKey[] = "BREAKPOINTPROBE";
	MRVMRuntimeKv runtimeKv;
	LoadedMacroFile file;
	MacroRef macroRef;
	MRMacroSourceMapEntry expectedSpan;
	MRMacroSourceMapEntry disabledSpan;
	MRMacroSourceMapEntry interiorSpan;
	MRMacroDebuggerBreakpoint breakpoint;
	std::vector<MRMacroDebuggerBreakpoint> breakpoints;
	std::vector<std::size_t> bytecodeOffsets;
	MRMacroDebugRunResult debugResult;
	MRMacroDebugRunResult steppedDebugResult;
	MRMacroDebugRunResult resumedDebugResult;
	MRMacroDebugRunResult finalDebugResult;
	MRMacroDebugRunResult completedDebugResult;
	MRMacroDebugRunResult sessionDebugResult;
	MRMacroDebugRunResult sessionResumeResult;
	MRMacroDebugRunResult sessionFinalResult;
	MRMacroDebugRunResult scheduledPauseResult;
	MRMacroDebugRunResult registryDebugResult;
	MRMacroDebugRunResult registryStatementStepResult;
	MRMacroDebugRunResult registryEntryStopResult;
	MRMacroExecutionSession debugSession;
	MRMacroExecutionSession scheduledPauseSession;
	MRMacroExecutionSession registryDebugSession;
	MRMacroExecutionSession registryStatementStepSession;
	MRMacroExecutionSession registryEntryStopSession;
	VirtualMachine debugVm;
	std::vector<unsigned char> bytecode;
	std::vector<MRMacroSourceMapEntry> sourceMap;
	size_t bytecodeSize = 0;
	unsigned char *compiled = compile_macro_code_with_source_map(kSource, &bytecodeSize, collectRegressionSourceMapEntry, &sourceMap);

	if (compiled == nullptr) {
		const char *errorText = get_last_compile_error();
		failureReason = std::string("Macro debugger breakpoint probe failed to compile fixture: ") + (errorText != nullptr ? errorText : "");
		return false;
	}
	bytecode.assign(compiled, compiled + bytecodeSize);
	std::free(compiled);

	if (bytecode.empty() || sourceMap.empty()) {
		failureReason = "Macro debugger breakpoint probe needs bytecode and source-map entries.";
		return false;
	}
	file.fileKey = "BREAKPOINT_PROBE_FILE";
	file.displayName = "breakpoint_probe.mrmac";
	file.resolvedPath = "/tmp/breakpoint_probe.mrmac";
	file.bytecode = bytecode;
	file.macroNames.push_back(kMacroKey);
	file.sourceMap = sourceMap;
	file.profile = mrvmAnalyzeBytecode(file.bytecode.data(), file.bytecode.size());

	macroRef.fileKey = file.fileKey;
	macroRef.displayName = "BreakpointProbe";
	macroRef.entryOffset = static_cast<std::size_t>(get_compiled_macro_entry(0));
	mrvmRuntimeCatalogWriteLoadedFile(runtimeKv, file);
	mrvmRuntimeCatalogWriteLoadedMacro(runtimeKv, kMacroKey, macroRef);

	if (!mrvmRuntimeCatalogFirstSourceMapSpanForLine(runtimeKv, kMacroKey, kBreakpointLine, expectedSpan)) {
		failureReason = "Macro debugger breakpoint probe did not find the expected statement source span.";
		return false;
	}
	if (!mrvmRuntimeCatalogFirstSourceMapSpanForLine(runtimeKv, kMacroKey, kDisabledBreakpointLine, disabledSpan)) {
		failureReason = "Macro debugger breakpoint probe did not find the disabled statement source span.";
		return false;
	}
		if (!mrvmRuntimeCatalogSourceMapSpanForBytecodeOffset(runtimeKv, kMacroKey, expectedSpan.bytecodeOffset, interiorSpan) || interiorSpan.bytecodeOffset != expectedSpan.bytecodeOffset ||
		    interiorSpan.sourceStartOffset != expectedSpan.sourceStartOffset || interiorSpan.sourceEndOffset != expectedSpan.sourceEndOffset || interiorSpan.line != expectedSpan.line) {
			failureReason = "Macro debugger breakpoint probe did not resolve the source line index bytecode key.";
			return false;
		}
	if (!mrvmRuntimeDebuggerWriteLineBreakpoint(runtimeKv, "BreakpointProbe", kBreakpointLine, true, "X > 0")) {
		failureReason = "Macro debugger breakpoint probe could not write a source-bound line breakpoint.";
		return false;
	}
	if (!mrvmRuntimeDebuggerWriteLineBreakpoint(runtimeKv, kMacroKey, kDisabledBreakpointLine, false, "")) {
		failureReason = "Macro debugger breakpoint probe could not write a disabled line breakpoint.";
		return false;
	}
	if (!mrvmRuntimeDebuggerReadLineBreakpoint(runtimeKv, kMacroKey, kBreakpointLine, breakpoint)) {
		failureReason = "Macro debugger breakpoint probe could not read the written line breakpoint.";
		return false;
	}
	if (breakpoint.macroKey != kMacroKey || !breakpoint.enabled || breakpoint.line != kBreakpointLine || breakpoint.conditionText != "X > 0") {
		failureReason = "Macro debugger breakpoint probe read mismatching breakpoint identity or condition.";
		return false;
	}
	if (breakpoint.bytecodeOffset != expectedSpan.bytecodeOffset || breakpoint.sourceStartOffset != expectedSpan.sourceStartOffset || breakpoint.sourceEndOffset != expectedSpan.sourceEndOffset || breakpoint.debuggableKind != expectedSpan.debuggableKind) {
		failureReason = "Macro debugger breakpoint probe did not persist the bound source-map span.";
		return false;
	}
	if (!mrvmRuntimeDebuggerLineBreakpointsForMacro(runtimeKv, kMacroKey, breakpoints) || breakpoints.size() != 2) {
		failureReason = "Macro debugger breakpoint probe could not list both line breakpoints.";
		return false;
	}
	if (breakpoints[0].line != kBreakpointLine || breakpoints[1].line != kDisabledBreakpointLine || breakpoints[1].enabled) {
		failureReason = "Macro debugger breakpoint probe listed line breakpoints with wrong order or enabled state.";
		return false;
	}
	if (!mrvmRuntimeDebuggerEnabledBreakpointOffsetsForMacro(runtimeKv, kMacroKey, bytecodeOffsets) || bytecodeOffsets.size() != 1 || bytecodeOffsets[0] != expectedSpan.bytecodeOffset) {
		failureReason = "Macro debugger breakpoint probe did not return the active bytecode offset set.";
		return false;
	}
	if (!mrvmRuntimeDebuggerWriteLineBreakpoint(runtimeKv, kMacroKey, kDisabledBreakpointLine, true, "")) {
		failureReason = "Macro debugger breakpoint probe could not enable the second line breakpoint.";
		return false;
	}
	if (!mrvmRuntimeDebuggerEnabledBreakpointOffsetsForMacro(runtimeKv, kMacroKey, bytecodeOffsets) || bytecodeOffsets.size() != 2 || bytecodeOffsets[0] != expectedSpan.bytecodeOffset || bytecodeOffsets[1] != disabledSpan.bytecodeOffset) {
		failureReason = "Macro debugger breakpoint probe did not return sorted active bytecode offsets.";
		return false;
	}
	debugResult = debugVm.executeDebugAt(file.bytecode.data(), file.bytecode.size(), macroRef.entryOffset, std::string(), macroRef.displayName, bytecodeOffsets);
	if (debugResult.stopReason != mrdStopBreakpoint || debugResult.instructionOffset != expectedSpan.bytecodeOffset || debugResult.hadError || debugResult.cancelled || !debugResult.paused || !debugVm.hasPausedDebug()) {
		failureReason = "Macro debugger breakpoint probe did not stop before the first active bytecode offset.";
		return false;
	}
	{
		bool sawX = false;
		for (const MRMacroDebugVariableSnapshot &variable : debugResult.variables)
			if (variable.name == "X") {
				sawX = true;
				if (variable.valueText != "0") {
					failureReason = "Macro debugger breakpoint probe must snapshot variables before executing the stopped statement.";
					return false;
				}
			}
		if (!sawX) {
			failureReason = "Macro debugger breakpoint probe did not snapshot declared variables.";
			return false;
		}
	}
	steppedDebugResult = debugVm.stepDebug(bytecodeOffsets);
	if (steppedDebugResult.stopReason != mrdStopStep || steppedDebugResult.hadError || steppedDebugResult.cancelled || !steppedDebugResult.paused || !debugVm.hasPausedDebug()) {
		failureReason = "Macro debugger breakpoint probe did not pause after one opcode step.";
		return false;
	}
	if (steppedDebugResult.instructionOffset == debugResult.instructionOffset) {
		failureReason = "Macro debugger breakpoint probe opcode step did not advance the instruction pointer.";
		return false;
	}
	resumedDebugResult = debugVm.continueDebug(bytecodeOffsets);
	if (resumedDebugResult.stopReason != mrdStopBreakpoint || resumedDebugResult.instructionOffset != disabledSpan.bytecodeOffset || resumedDebugResult.hadError || resumedDebugResult.cancelled || !resumedDebugResult.paused || !debugVm.hasPausedDebug()) {
		failureReason = "Macro debugger breakpoint probe did not continue from a step stop to the second active bytecode offset.";
		return false;
	}
	{
		bool sawX = false;
		for (const MRMacroDebugVariableSnapshot &variable : resumedDebugResult.variables)
			if (variable.name == "X") {
				sawX = true;
				if (variable.valueText != "1") {
					failureReason = "Macro debugger breakpoint probe did not execute the first stopped statement before continuing.";
					return false;
				}
			}
		if (!sawX) {
			failureReason = "Macro debugger breakpoint probe did not snapshot resumed variables.";
			return false;
		}
	}
	finalDebugResult = debugVm.continueDebug(bytecodeOffsets);
	if (finalDebugResult.stopReason != mrdStopCompleted || finalDebugResult.hadError || finalDebugResult.cancelled || finalDebugResult.paused || debugVm.hasPausedDebug()) {
		failureReason = "Macro debugger breakpoint probe did not complete after continuing from the second breakpoint.";
		return false;
	}
	{
		bool sawX = false;
		for (const MRMacroDebugVariableSnapshot &variable : finalDebugResult.variables)
			if (variable.name == "X") {
				sawX = true;
				if (variable.valueText != "2") {
					failureReason = "Macro debugger breakpoint probe did not snapshot final continued variable state.";
					return false;
				}
			}
		if (!sawX) {
			failureReason = "Macro debugger breakpoint probe did not snapshot final continued variables.";
			return false;
		}
	}
	sessionDebugResult = mrvmStartDebugSessionAt(file.bytecode.data(), file.bytecode.size(), macroRef.entryOffset, macroRef.displayName, MRMacroExecutionOwner(), bytecodeOffsets, &debugSession);
	if (debugSession.sessionId == 0 || debugSession.route != MRMacroExecutionRoute::Debug || debugSession.state != MRMacroExecutionState::Yielded || sessionDebugResult.stopReason != mrdStopBreakpoint || !sessionDebugResult.paused) {
		failureReason = "Macro debugger breakpoint probe did not bind the first debug stop to an execution session.";
		return false;
	}
	{
		MRMacroDebugWatchSnapshot evaluation;
		std::string evaluationError;

		if (!mrvmEvaluateDebugExpression(debugSession.sessionId, "X + 1", evaluation, &evaluationError) || !evaluation.errorText.empty() || evaluation.type != TYPE_INT || evaluation.valueText != "1") {
			failureReason = "Macro debugger breakpoint probe could not evaluate a paused pure expression: " + evaluationError + " / " + evaluation.errorText;
			return false;
		}
		if (!mrvmEvaluateDebugExpression(debugSession.sessionId, "X := 9", evaluation, &evaluationError) || evaluation.errorText.empty()) {
			failureReason = "Macro debugger breakpoint probe accepted a mutating evaluate expression.";
			return false;
		}
	}
	{
		MRMacroDebugVariableSnapshot variable;
		std::vector<MRMacroDebugVariableSnapshot> updatedVariables;
		std::string mutationError;
		bool found = false;

		for (const MRMacroDebugVariableSnapshot &candidate : sessionDebugResult.variables)
			if (candidate.name == "X" && candidate.scope == mrdVariableSession) {
				variable = candidate;
				found = true;
				break;
			}
		if (!found || !mrvmWriteDebugScalarVariable(debugSession.sessionId, variable, "41", updatedVariables, &mutationError)) {
			failureReason = "Macro debugger breakpoint probe could not write a paused scalar local: " + mutationError;
			return false;
		}
		found = false;
		for (const MRMacroDebugVariableSnapshot &candidate : updatedVariables)
			if (candidate.name == "X" && candidate.scope == mrdVariableSession) {
				found = candidate.valueText == "41";
				break;
			}
		if (!found) {
			failureReason = "Macro debugger breakpoint probe did not return the written scalar local value.";
			return false;
		}
		if (mrvmWriteDebugScalarVariable(debugSession.sessionId, variable, "not-an-int", updatedVariables, &mutationError) || mutationError.empty()) {
			failureReason = "Macro debugger breakpoint probe accepted an invalid scalar local value.";
			return false;
		}
	}
	sessionResumeResult = mrvmContinueDebugSession(debugSession.sessionId, bytecodeOffsets);
	if (sessionResumeResult.stopReason != mrdStopBreakpoint || sessionResumeResult.instructionOffset != disabledSpan.bytecodeOffset || !sessionResumeResult.paused) {
		failureReason = "Macro debugger breakpoint probe did not continue the session-bound debug VM.";
		return false;
	}
	sessionFinalResult = mrvmContinueDebugSession(debugSession.sessionId, bytecodeOffsets);
	if (sessionFinalResult.stopReason != mrdStopCompleted || sessionFinalResult.paused || sessionFinalResult.hadError || sessionFinalResult.cancelled) {
		failureReason = "Macro debugger breakpoint probe did not complete the session-bound debug VM.";
		return false;
	}
	{
		bool sawResult = false;
		const std::vector<MRMacroExecutionResult> results = recentMacroExecutionResults();
		for (const MRMacroExecutionResult &result : results)
			if (result.session.sessionId == debugSession.sessionId && result.state == MRMacroExecutionState::Completed) sawResult = true;
		if (!sawResult) {
			failureReason = "Macro debugger breakpoint probe did not publish the completed debug execution-session result.";
			return false;
		}
	}
	scheduledPauseResult = mrvmStartDebugSessionAt(file.bytecode.data(), file.bytecode.size(), macroRef.entryOffset, macroRef.displayName, MRMacroExecutionOwner(), bytecodeOffsets, &scheduledPauseSession);
	if (scheduledPauseSession.sessionId == 0 || !scheduledPauseResult.paused || !mrvmScheduleDebugMacroContinue(scheduledPauseSession.sessionId, kMacroKey) || !mrvmRequestDebugPause(scheduledPauseSession.sessionId) ||
	    !mrvmPumpDebugSession(scheduledPauseSession.sessionId, kMacroKey, scheduledPauseResult) || scheduledPauseResult.stopReason != mrdStopPaused || !scheduledPauseResult.paused) {
		failureReason = "Macro debugger breakpoint probe did not pause a scheduled continue at an instruction boundary.";
		return false;
	}
	scheduledPauseResult = mrvmContinueDebugSession(scheduledPauseSession.sessionId, bytecodeOffsets);
	if (scheduledPauseResult.stopReason != mrdStopBreakpoint || !scheduledPauseResult.paused) {
		failureReason = "Macro debugger breakpoint probe did not continue after a scheduled pause.";
		return false;
	}
	if (!mrvmCloseDebugSession(scheduledPauseSession.sessionId)) {
		failureReason = "Macro debugger breakpoint probe could not stop a scheduled debug session.";
		return false;
	}
	completedDebugResult = mrvmRunBytecodeDebugAt(file.bytecode.data(), file.bytecode.size(), macroRef.entryOffset, macroRef.displayName, std::vector<std::size_t>());
	if (completedDebugResult.stopReason != mrdStopCompleted || completedDebugResult.hadError || completedDebugResult.cancelled) {
		failureReason = "Macro debugger breakpoint probe did not complete without active bytecode offsets.";
		return false;
	}
	{
		bool sawX = false;
		for (const MRMacroDebugVariableSnapshot &variable : completedDebugResult.variables)
			if (variable.name == "X") {
				sawX = true;
				if (variable.valueText != "2") {
					failureReason = "Macro debugger breakpoint probe did not snapshot completed variable state.";
					return false;
				}
			}
		if (!sawX) {
			failureReason = "Macro debugger breakpoint probe did not snapshot completed variables.";
			return false;
		}
	}
	{
		const std::string registryMacroName = "RegistryDebugProbe" + std::to_string(static_cast<long>(::getpid()));
		const std::string registryMacroPath = "/tmp/mr_registry_debug_probe_" + std::to_string(static_cast<long>(::getpid())) + ".mrmac";
		const std::string registrySource = "$MACRO " + registryMacroName + ";\n"
		                                   "DEF_INT(X);\n"
		                                   "X := 1;\n"
		                                   "X := X + 1;\n"
		                                   "END_MACRO;\n";
		std::string registryError;

		{
			std::ofstream out(registryMacroPath.c_str(), std::ios::out | std::ios::trunc);
			if (!out) {
				failureReason = "Macro debugger breakpoint probe could not write registry macro fixture.";
				return false;
			}
			out << registrySource;
		}
		if (!mrvmLoadMacroFile(registryMacroPath, &registryError)) {
			(void)::remove(registryMacroPath.c_str());
			failureReason = "Macro debugger breakpoint probe could not load registry macro fixture: " + registryError;
			return false;
			}
			registryDebugResult = mrvmStartDebugMacroByName(registryMacroName, MRMacroExecutionOwner(), &registryDebugSession, &registryError);
			if (registryDebugSession.sessionId == 0 || registryDebugSession.route != MRMacroExecutionRoute::Background || registryDebugSession.state != MRMacroExecutionState::Running ||
			    registryDebugResult.stopReason != mrdStopBudget || !registryDebugResult.paused || registryDebugResult.hadError || registryDebugResult.cancelled) {
				(void)::remove(registryMacroPath.c_str());
				failureReason = "Macro debugger breakpoint probe did not schedule a registry macro on its natural worker route: " + registryError;
				return false;
			}
			{
				const MRMacroDebugWorkerResult workerResult =
				    mrvmRunDebugSessionWorkerAction(registryDebugSession.sessionId, registryMacroName, mrdWorkerContinue, 256, std::shared_ptr<std::atomic_bool>());

				registryDebugResult = workerResult.debugResult;
				if (!workerResult.accepted || registryDebugResult.stopReason != mrdStopCompleted || registryDebugResult.paused || registryDebugResult.hadError || registryDebugResult.cancelled) {
					(void)::remove(registryMacroPath.c_str());
					failureReason = "Macro debugger breakpoint probe did not complete the scheduled registry worker session: " + workerResult.errorMessage;
					return false;
				}
			}
			{
				bool sawCompleted = false;

				for (const MRMacroExecutionResult &executionResult : recentMacroExecutionResults())
					if (executionResult.session.sessionId == registryDebugSession.sessionId && executionResult.state == MRMacroExecutionState::Completed) sawCompleted = true;
				if (!sawCompleted) {
					(void)::remove(registryMacroPath.c_str());
					failureReason = "Macro debugger breakpoint probe did not publish the completed registry worker session.";
					return false;
				}
			}
			bool sawRegistryX = false;
		for (const MRMacroDebugVariableSnapshot &variable : registryDebugResult.variables)
			if (variable.name == "X") {
				sawRegistryX = true;
				if (variable.valueText != "2") {
					(void)::remove(registryMacroPath.c_str());
					failureReason = "Macro debugger breakpoint probe did not snapshot registry macro debug variable state.";
					return false;
				}
			}
		if (!sawRegistryX) {
			(void)::remove(registryMacroPath.c_str());
			failureReason = "Macro debugger breakpoint probe did not snapshot registry macro debug variables.";
			return false;
		}
		{
			int beforeStepLine = 0;
			int afterStepLine = 0;
			std::size_t beforeStepStart = 0;
			std::size_t beforeStepEnd = 0;
			std::size_t afterStepStart = 0;
			std::size_t afterStepEnd = 0;
			bool breakpointEnabled = false;

			if (!mrvmToggleDebugLineBreakpoint(registryMacroName, 3, &breakpointEnabled, &registryError) || !breakpointEnabled) {
				(void)::remove(registryMacroPath.c_str());
				failureReason = "Macro debugger breakpoint probe could not set registry statement-step breakpoint: " + registryError;
				return false;
			}
			registryStatementStepResult = mrvmStartDebugMacroByName(registryMacroName, MRMacroExecutionOwner(), &registryStatementStepSession, &registryError);
			if (registryStatementStepSession.sessionId == 0 || registryStatementStepSession.route != MRMacroExecutionRoute::Background || registryStatementStepSession.state != MRMacroExecutionState::Running ||
			    registryStatementStepResult.stopReason != mrdStopBudget || !registryStatementStepResult.paused || registryStatementStepResult.hadError || registryStatementStepResult.cancelled) {
				(void)::remove(registryMacroPath.c_str());
				failureReason = "Macro debugger breakpoint probe did not schedule the registry breakpoint run: " + registryError;
				return false;
			}
			{
				const MRMacroDebugWorkerResult workerResult =
				    mrvmRunDebugSessionWorkerAction(registryStatementStepSession.sessionId, registryMacroName, mrdWorkerContinue, 256, std::shared_ptr<std::atomic_bool>());

				registryStatementStepResult = workerResult.debugResult;
				if (!workerResult.accepted || registryStatementStepResult.stopReason != mrdStopBreakpoint || !registryStatementStepResult.paused ||
				    registryStatementStepResult.hadError || registryStatementStepResult.cancelled) {
					(void)::remove(registryMacroPath.c_str());
					failureReason = "Macro debugger breakpoint probe did not pause the worker at registry line 3: " + workerResult.errorMessage;
					return false;
				}
			}
			if (!mrvmDebugSourceLineForInstruction(registryMacroName, registryStatementStepResult.instructionOffset, &beforeStepLine, &beforeStepStart, &beforeStepEnd) || beforeStepLine != 3) {
				(void)::remove(registryMacroPath.c_str());
				failureReason = "Macro debugger breakpoint probe could not resolve registry statement-step starting span.";
				return false;
			}
			{
				const MRMacroDebugWorkerResult workerResult =
				    mrvmRunDebugSessionWorkerAction(registryStatementStepSession.sessionId, registryMacroName, mrdWorkerStepInto, 256, std::shared_ptr<std::atomic_bool>());

				registryStatementStepResult = workerResult.debugResult;
				if (!workerResult.accepted || registryStatementStepResult.stopReason != mrdStopStep || !registryStatementStepResult.paused ||
				    registryStatementStepResult.hadError || registryStatementStepResult.cancelled) {
					(void)::remove(registryMacroPath.c_str());
					failureReason = "Macro debugger breakpoint probe did not perform a registry worker statement step: " + workerResult.errorMessage;
					return false;
				}
			}
			if (!mrvmDebugSourceLineForInstruction(registryMacroName, registryStatementStepResult.instructionOffset, &afterStepLine, &afterStepStart, &afterStepEnd) || afterStepLine != 4 ||
			    (afterStepStart == beforeStepStart && afterStepEnd == beforeStepEnd)) {
				(void)::remove(registryMacroPath.c_str());
				failureReason = "Macro debugger breakpoint probe statement step did not leave the original source span (before=" + std::to_string(beforeStepLine) + ", after=" + std::to_string(afterStepLine) + ", offset=" + std::to_string(registryStatementStepResult.instructionOffset) + ").";
				return false;
			}
			if (!mrvmCloseDebugSession(registryStatementStepSession.sessionId)) {
				(void)::remove(registryMacroPath.c_str());
				failureReason = "Macro debugger breakpoint probe could not close the registry statement-step session.";
				return false;
			}
			if (!mrvmToggleDebugLineBreakpoint(registryMacroName, 3, &breakpointEnabled, &registryError)) {
				(void)::remove(registryMacroPath.c_str());
				failureReason = "Macro debugger breakpoint probe could not clear registry statement-step breakpoint: " + registryError;
				return false;
			}
		}
		if (!mrvmLoadMacroFile(registryMacroPath, &registryError)) {
			(void)::remove(registryMacroPath.c_str());
			failureReason = "Macro debugger breakpoint probe could not reload registry macro fixture for entry stop: " + registryError;
			return false;
			}
			registryEntryStopResult = mrvmStartDebugMacroByName(registryMacroName, MRMacroExecutionOwner(), &registryEntryStopSession, &registryError, true);
			(void)::remove(registryMacroPath.c_str());
			if (registryEntryStopSession.sessionId == 0 || registryEntryStopSession.route != MRMacroExecutionRoute::Background || registryEntryStopSession.state != MRMacroExecutionState::Yielded || registryEntryStopResult.stopReason != mrdStopBreakpoint || !registryEntryStopResult.paused || registryEntryStopResult.hadError || registryEntryStopResult.cancelled) {
			failureReason = "Macro debugger breakpoint probe did not pause a registry macro at entry: " + registryError;
			return false;
		}
		if (mrvmCloseDebugSession(registryEntryStopSession.sessionId)) {
			const std::vector<MRMacroExecutionResult> results = recentMacroExecutionResults();
			bool sawClosed = false;

			for (const MRMacroExecutionResult &result : results)
				if (result.session.sessionId == registryEntryStopSession.sessionId && result.state == MRMacroExecutionState::Cancelled) sawClosed = true;
			if (!sawClosed) {
				failureReason = "Macro debugger breakpoint probe did not publish the closed entry-stop debug session.";
				return false;
			}
		} else {
			failureReason = "Macro debugger breakpoint probe could not close the entry-stop debug session.";
			return false;
		}
		bool fileBreakpointsEnabled = false;
		if (!mrvmWriteDebugLineBreakpoint(registryMacroName, 3, true, &registryError) || !mrvmWriteDebugLineBreakpoint(registryMacroName, 4, true, &registryError)) {
			failureReason = "Macro debugger breakpoint probe could not create file breakpoints for the enabled-state audit: " + registryError;
			return false;
		}
		if (!mrvmToggleDebugLineBreakpointsEnabledForMacroFile(registryMacroName, &fileBreakpointsEnabled, &registryError) || fileBreakpointsEnabled) {
			failureReason = "Macro debugger breakpoint probe did not disable all file breakpoints: " + registryError;
			return false;
		}
		{
			std::vector<MRMacroDebuggerBreakpoint> fileBreakpoints;

			if (!mrvmDebugLineBreakpointsForMacro(registryMacroName, fileBreakpoints) || fileBreakpoints.size() != 2 || fileBreakpoints[0].enabled || fileBreakpoints[1].enabled) {
				failureReason = "Macro debugger breakpoint probe did not retain disabled file breakpoints.";
				return false;
			}
		}
		if (!mrvmToggleDebugLineBreakpointsEnabledForMacroFile(registryMacroName, &fileBreakpointsEnabled, &registryError) || !fileBreakpointsEnabled) {
			failureReason = "Macro debugger breakpoint probe did not enable all file breakpoints: " + registryError;
			return false;
		}
		if (!mrvmEraseDebugLineBreakpointsForMacroFile(registryMacroName, &registryError)) {
			failureReason = "Macro debugger breakpoint probe could not clear all file breakpoints: " + registryError;
			return false;
		}
		{
			std::vector<MRMacroDebuggerBreakpoint> fileBreakpoints;

			if (mrvmDebugLineBreakpointsForMacro(registryMacroName, fileBreakpoints)) {
				failureReason = "Macro debugger breakpoint probe retained a cleared file breakpoint.";
				return false;
			}
		}
	}
	{
		const std::string nestedMacroName = "NESTEDDEBUGPARENT" + std::to_string(static_cast<long>(::getpid()));
		const std::string nestedChildName = "NESTEDDEBUGCHILD" + std::to_string(static_cast<long>(::getpid()));
		const std::string nestedMacroPath = "/tmp/mr_nested_debug_probe_" + std::to_string(static_cast<long>(::getpid())) + ".mrmac";
		const std::string nestedSource = "$MACRO " + nestedMacroName + ";\n"
		                                       "RUN_MACRO('" + nestedChildName + "');\n"
		                                       "END_MACRO;\n"
		                                       "$MACRO " + nestedChildName + ";\n"
		                                       "DEF_INT(X);\n"
		                                       "X := 1;\n"
		                                       "END_MACRO;\n";
		MRMacroExecutionSession nestedSession;
		MRMacroDebugRunResult nestedResult;
		std::string nestedError;

		{
			std::ofstream out(nestedMacroPath.c_str(), std::ios::out | std::ios::trunc);
			if (!out) {
				failureReason = "Macro debugger breakpoint probe could not write nested debug fixture.";
				return false;
			}
			out << nestedSource;
		}
		if (!mrvmLoadMacroFile(nestedMacroPath, &nestedError)) {
			(void)::remove(nestedMacroPath.c_str());
			failureReason = "Macro debugger breakpoint probe could not load nested debug fixture: " + nestedError;
			return false;
		}
		nestedResult = mrvmStartDebugMacroByName(nestedMacroName, MRMacroExecutionOwner(), &nestedSession, &nestedError, true);
		if (nestedSession.sessionId == 0 || !nestedResult.paused || nestedResult.macroKey != nestedMacroName) {
			(void)::remove(nestedMacroPath.c_str());
			failureReason = "Macro debugger breakpoint probe could not stop nested parent at entry: " + nestedError;
			return false;
		}
		nestedResult = mrvmStepDebugMacroByName(nestedSession.sessionId, nestedMacroName, &nestedError);
		if (!nestedResult.paused || nestedResult.macroKey != nestedChildName) {
			(void)::remove(nestedMacroPath.c_str());
			failureReason = "Macro debugger breakpoint probe did not enter the RUN_MACRO child frame (macro=" + nestedResult.macroKey + ", offset=" + std::to_string(nestedResult.instructionOffset) + ", stop=" + std::to_string(static_cast<int>(nestedResult.stopReason)) + "): " + nestedError;
			return false;
		}
		if (nestedResult.callStack.size() < 2 || nestedResult.callStack[0].macroKey != nestedChildName || nestedResult.callStack[0].kind != mrdStackFrameCurrent || nestedResult.callStack[1].macroKey != nestedMacroName ||
		    nestedResult.callStack[1].kind != mrdStackFrameRunMacro) {
			(void)::remove(nestedMacroPath.c_str());
			failureReason = "Macro debugger breakpoint probe did not snapshot the current child and parent RUN_MACRO frames.";
			return false;
		}
		nestedResult = mrvmStepOutDebugMacroByName(nestedSession.sessionId, nestedMacroName, &nestedError);
		if (!nestedResult.paused || nestedResult.macroKey != nestedMacroName) {
			(void)::remove(nestedMacroPath.c_str());
			failureReason = "Macro debugger breakpoint probe did not return from the child frame: " + nestedError;
			return false;
		}
		if (!mrvmCloseDebugSession(nestedSession.sessionId)) {
			(void)::remove(nestedMacroPath.c_str());
			failureReason = "Macro debugger breakpoint probe could not close nested debug session.";
			return false;
		}
		nestedSession = MRMacroExecutionSession();
		nestedResult = mrvmStartDebugMacroByName(nestedMacroName, MRMacroExecutionOwner(), &nestedSession, &nestedError, true);
		if (nestedSession.sessionId == 0 || !nestedResult.paused) {
			(void)::remove(nestedMacroPath.c_str());
			failureReason = "Macro debugger breakpoint probe could not restart nested debug fixture: " + nestedError;
			return false;
		}
		nestedResult = mrvmStepOverDebugMacroByName(nestedSession.sessionId, nestedMacroName, &nestedError);
		(void)::remove(nestedMacroPath.c_str());
		if (nestedResult.paused || nestedResult.macroKey != nestedMacroName || nestedResult.stopReason != mrdStopCompleted) {
			failureReason = "Macro debugger breakpoint probe did not complete after stepping over the terminal RUN_MACRO child frame (macro=" + nestedResult.macroKey + ", stop=" + std::to_string(static_cast<int>(nestedResult.stopReason)) + "): " + nestedError;
			return false;
		}
	}
	if (mrvmRuntimeDebuggerWriteLineBreakpoint(runtimeKv, kMacroKey, 99, true, "")) {
		failureReason = "Macro debugger breakpoint probe must reject unbound breakpoint lines.";
		return false;
	}
	if (!mrvmRuntimeDebuggerEraseLineBreakpoint(runtimeKv, kMacroKey, kBreakpointLine)) {
		failureReason = "Macro debugger breakpoint probe could not erase the written breakpoint.";
		return false;
	}
	if (mrvmRuntimeDebuggerReadLineBreakpoint(runtimeKv, kMacroKey, kBreakpointLine, breakpoint)) {
		failureReason = "Macro debugger breakpoint probe still sees breakpoint after erase.";
		return false;
	}

	failureReason.clear();
	return true;
}

int runMacroDebuggerBreakpointKvProbeMode() {
	std::string failure;

	if (runMacroDebuggerBreakpointKvProbe(failure)) return 0;
	if (!failure.empty()) std::cerr << failure << "\n";
	return 1;
}

int runStagedNavProbeMode() {
	static const char *const kMacros[] = {"mrmac/macros/test_cursor_ops.mrmac", "mrmac/macros/test_nav_ops.mrmac", "mrmac/macros/test_tab_indent_ops.mrmac"};
	static const char kCustomSource[] = "$MACRO NavStage;\n"
	                                    "GOTO_LINE(2);\n"
	                                    "GOTO_COL(3);\n"
	                                    "LEFT;\n"
	                                    "RIGHT;\n"
	                                    "HOME;\n"
	                                    "EOL;\n"
	                                    "END_MACRO;\n";
	static const char kDocText[] = "alpha beta\nsecond line\n";
	std::string failure;

	for (const char *relativeMacro : kMacros)
		if (!printProfileLineForMacro(absolutePathFromCwd(relativeMacro), true, failure)) {
			std::cerr << failure << "\n";
			return 1;
		}

	if (!runCustomStagedProbe(kCustomSource, kDocText, "/tmp/navstage.txt", 0, 22, 2, 12, true, failure)) {
		std::cerr << failure << "\n";
		return 1;
	}

	return 0;
}

int runStagedMarkPageProbeMode() {
	static const char *const kMacro = "mrmac/macros/test_mark_page_ops.mrmac";
	static const char kCustomSource[] = "$MACRO MarkPageStage;\n"
	                                    "MARK_POS;\n"
	                                    "RIGHT;\n"
	                                    "GOTO_MARK;\n"
	                                    "POP_MARK;\n"
	                                    "PAGE_DOWN;\n"
	                                    "PAGE_UP;\n"
	                                    "NEXT_PAGE_BREAK;\n"
	                                    "LAST_PAGE_BREAK;\n"
	                                    "END_MACRO;\n";
	static const char kDocText[] = "line1\nline2\f\nline3\nline4\f\nline5\n";
	std::string failure;

	if (!printProfileLineForMacro(absolutePathFromCwd(kMacro), true, failure)) {
		std::cerr << failure << "\n";
		return 1;
	}

	if (!runCustomStagedProbe(kCustomSource, kDocText, "/tmp/markpage.txt", 0, 13, 3, 1, false, failure)) {
		std::cerr << failure << "\n";
		return 1;
	}

	return 0;
}

int runClosureHashDefaultProbeMode() {
	static const char kSource[] = "$CLOSURE ClosureHashDefault;\n"
	                              "DEF_TICK(1000);\n"
	                              "DEF_HASH(State);\n"
	                              "END_CLOSURE;\n";
	const std::string closureId = "regression-closure-hash-default-" + std::to_string(static_cast<long>(::getpid()));
	std::vector<unsigned char> bytecode;
	int entryOffset = -1;
	std::string macroName;
	std::string compileError;
	std::string vmError;
	VirtualMachine::Value state;
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();

	if (!compileSource(kSource, bytecode, entryOffset, macroName, compileError)) {
		std::cerr << "Closure hash default probe compile failed: " << compileError << "\n";
		return 1;
	}

	static_cast<void>(mrvmExecSessionsEraseClosureState(runtimeKv, closureId));
	mrvmExecSessionsEnsureClosureState(runtimeKv, closureId, 1000);
	VirtualMachine vm;
	vm.setClosureContext(closureId);
	vm.executeAt(bytecode.data(), bytecode.size(), static_cast<std::size_t>(entryOffset), std::string(), macroName, true, true);

	const bool vmFailed = firstVmError(vm.log, vmError);
	const bool stateRead = mrvmExecSessionsReadClosureVariable(runtimeKv, closureId, "State", state);
	const bool validHash = stateRead && state.type == TYPE_HASH && state.globalStorage && state.hashHandle > 0;
	const std::size_t keyCount = validHash ? runtimeKv.globalStore().keys(state.hashHandle).size() : 0;
	const bool erased = mrvmExecSessionsEraseClosureState(runtimeKv, closureId);

	if (vmFailed) {
		std::cerr << "Closure hash default probe produced VM error: " << vmError << "\n";
		return 1;
	}
	if (!stateRead) {
		std::cerr << "Closure hash default probe did not persist the missing DEF_HASH variable.\n";
		return 1;
	}
	if (!validHash) {
		std::cerr << "Closure hash default probe persisted a non-hash fallback value.\n";
		return 1;
	}
	if (keyCount != 0) {
		std::cerr << "Closure hash default probe did not create an empty hash.\n";
		return 1;
	}
	if (!erased) {
		std::cerr << "Closure hash default probe could not remove its runtime state.\n";
		return 1;
	}
	std::cout << "Closure hash default probe passed.\n";
	return 0;
}

int runMacroScreenFlushProbeMode() {
	static constexpr int kWriteCount = 8;
	static constexpr int kStartX = 3;
	static constexpr int kStartY = 3;
	static constexpr int kBgColor = 1;
	static constexpr int kFgColor = 14;
	MREditorApp app;
	std::uint64_t unbatchedFlushes = 0;
	std::uint64_t batchedFlushes = 0;

	mrvmUiClearScreen();
	mrvmUiResetMacroScreenFlushCount();
	for (int i = 0; i < kWriteCount; ++i)
		mrvmUiWrite("flush-probe", kStartX, kStartY + i, kBgColor, kFgColor);
	unbatchedFlushes = mrvmUiMacroScreenFlushCount();

	mrvmUiClearScreen();
	mrvmUiResetMacroScreenFlushCount();
	mrvmUiBeginMacroScreenBatch();
	for (int i = 0; i < kWriteCount; ++i)
		mrvmUiWrite("flush-probe", kStartX, kStartY + i, kBgColor, kFgColor);
	mrvmUiEndMacroScreenBatch();
	batchedFlushes = mrvmUiMacroScreenFlushCount();

	std::cout << "macro-screen-flush-probe unbatched=" << unbatchedFlushes << " batched=" << batchedFlushes << " reduction=" << (unbatchedFlushes > batchedFlushes ? (unbatchedFlushes - batchedFlushes) : 0) << "\n";
	return batchedFlushes < unbatchedFlushes ? 0 : 1;
}

int runKeymapMacroDispatchProbeMode() {
	std::string failure;

	if (runKeymapMacroBindingDispatchProbe(failure)) return 0;
	if (!failure.empty()) std::cerr << failure << "\n";
	return 1;
}


bool validateMrsetupCorePaths(std::string &failureReason) {
	if (defaultMacroDirectoryPath() != "/tmp") {
		failureReason = "Startup context should apply MACROPATH='/tmp', got: " + defaultMacroDirectoryPath();
		return false;
	}
	if (configuredSettingsMacroFilePath() != "/tmp/mr_settings_probe.mrmac") {
		failureReason = "Startup context should apply SETTINGSPATH='/tmp/mr_settings_probe.mrmac'.";
		return false;
	}
	if (configuredHelpFilePath() != absolutePathFromCwd("mr.hlp")) {
		failureReason = "Startup context should apply HELPPATH as absolute URI from current path.";
		return false;
	}
	if (configuredTempDirectoryPath() != "/tmp") {
		failureReason = "Startup context should apply TEMPDIR='/tmp'.";
		return false;
	}
	if (configuredShellExecutablePath() != "/bin/sh") {
		failureReason = "Startup context should apply SHELLPATH='/bin/sh'.";
		return false;
	}
	if (configuredPageBreakCharacter() != '\f') {
		failureReason = "Startup context should apply PAGE_BREAK='\\\\f'.";
		return false;
	}
	return true;
}

bool validateMrsetupEditorSettings(std::string &failureReason) {
	if (configuredTabExpandSetting()) {
		failureReason = "Startup context should apply TAB_EXPAND='false'.";
		return false;
	}
	if (configuredTabSizeSetting() != 6) {
		failureReason = "Startup context should apply TAB_SIZE='6'.";
		return false;
	}
	{
		MREditSetupSettings settings = configuredEditSetupSettings();
		if (settings.backupFiles) {
			failureReason = "Startup context should apply BACKUP_FILES='false'.";
			return false;
		}
		if (!settings.showEofMarker) {
			failureReason = "Startup context should apply SHOW_EOF_MARKER='true'.";
			return false;
		}
		if (settings.showEofMarkerEmoji) {
			failureReason = "Startup context should apply SHOW_EOF_MARKER_EMOJI='false'.";
			return false;
		}
		if (!settings.showLineNumbers || settings.lineNumbersPosition != "LEADING") {
			failureReason = "Startup context should apply LINE_NUMBERS_POSITION='LEADING'.";
			return false;
		}
		if (!settings.lineNumZeroFill) {
			failureReason = "Startup context should apply LINE_NUM_ZERO_FILL='true'.";
			return false;
		}
	}
	if (configuredDefaultInsertMode()) {
		failureReason = "Startup context should apply DEFAULT_MODE='OVERWRITE'.";
		return false;
	}
	{
		std::vector<std::string> exts = configuredDefaultExtensionList();
		if (exts.size() < 2 || exts[0] != "txt" || exts[1] != "md") {
			failureReason = "Startup context should apply DEFAULT_EXTENSIONS='txt;md'.";
			return false;
		}
	}
	return true;
}

bool validateMrsetupGlobalSettings(std::string &failureReason) {
	if (configuredAutoDetectBinaryFiles()) {
		failureReason = "Startup context should apply AUTODETECT_BINARY_FILES='false'.";
		return false;
	}
	if (configuredCursorBehaviour() != MRCursorBehaviour::FreeMovement) {
		failureReason = "Startup context should apply CURSOR_BEHAVIOUR='FREE_MOVEMENT'.";
		return false;
	}
	if (configuredScrollbarVisibility() != MRScrollbarVisibility::Always) {
		failureReason = "Startup context should apply SCROLLBAR_VISIBILITY='ALWAYS'.";
		return false;
	}
	if (configuredColorOutputMode() != MRColorOutputMode::TerminalPalette) {
		failureReason = "Startup context should apply COLOR_OUTPUT_MODE='TERMINAL_PALETTE'.";
		return false;
	}
	if (configuredFileCompareOriginalLeadingGutters() != "L" || configuredFileCompareOriginalTrailingGutters() != "M" || configuredFileCompareCompareLeadingGutters() != "LD" || configuredFileCompareCompareTrailingGutters() != "") {
		failureReason = "Startup context should apply file compare gutter settings.";
		return false;
	}
	if (configuredFileCompareStartConfiguration() != MRFileCompareStartConfiguration::CompareOriginal) {
		failureReason = "Startup context should apply FILE_COMPARE_START_CONFIGURATION='COMPARE_ORIGINAL'.";
		return false;
	}
	if (!configuredFileCompareComparePanelReadOnly()) {
		failureReason = "Startup context should apply FILE_COMPARE_COMPARE_PANEL_READ_ONLY='true'.";
		return false;
	}
	return true;
}

bool validateMrsetupColorSettings(std::string &failureReason) {
	MRColorSetupSettings colors = configuredColorSetupSettings();

	if (colors.windowColors[0] != MRRgbColorAttribute{0x101010u, 0x202020u} || colors.windowColors[8] != MRRgbColorAttribute{0x181818u, 0x282828u} ||
	    colors.windowColors[13] != MRRgbColorAttribute{0x1D1D1Du, 0x2D2D2Du}) {
		failureReason = "Startup context should apply the exact RGB24 WINDOWCOLORS list.";
		return false;
	}
	if (colors.menuDialogColors[0] != MRRgbColorAttribute{0x303030u, 0x404040u} || colors.menuDialogColors[10] != MRRgbColorAttribute{0x3A3A3Au, 0x4A4A4Au} ||
	    colors.menuDialogColors[31] != MRRgbColorAttribute{0x4F4F4Fu, 0x5F5F5Fu}) {
		failureReason = "Startup context should apply the exact RGB24 MENUDIALOGCOLORS list.";
		return false;
	}
	if (colors.helpColors[0] != MRRgbColorAttribute{0x505050u, 0x606060u} || colors.helpColors[9] != MRRgbColorAttribute{0x595959u, 0x696969u}) {
		failureReason = "Startup context should apply the exact RGB24 HELPCOLORS list.";
		return false;
	}
	if (colors.otherColors[0] != MRRgbColorAttribute{0x707070u, 0x808080u} || colors.otherColors[10] != MRRgbColorAttribute{0x7A7A7Au, 0x8A8A8Au}) {
		failureReason = "Startup context should apply the exact RGB24 OTHERCOLORS list.";
		return false;
	}
	return true;
}

bool validateMrsetupRuntimeRejection(const std::vector<unsigned char> &bytecode, int entryOffset, const std::string &macroName, std::string &failureReason) {
	VirtualMachine vm;
	std::string vmError;

	mrvmSetStartupSettingsMode(false);
	vm.executeAt(bytecode.data(), bytecode.size(), static_cast<size_t>(entryOffset), std::string(), macroName, true, true);
	if (!firstVmError(vm.log, vmError)) {
		failureReason = "Runtime context should reject MRSETUP, but no VM Error occurred.";
		return false;
	}
	if (vmError.find("MRSETUP is only allowed in settings.mrmac during startup.") == std::string::npos) {
		failureReason = "Runtime context produced unexpected error: " + vmError;
		return false;
	}
	return true;
}

bool testMrsetupStartupOnly(std::string &failureReason) {
	const std::string source = "$MACRO Setup;\n"
	                           "MRSETUP('SETTINGSPATH', '/tmp/mr_settings_probe.mrmac');\n"
	                           "MRSETUP('MACROPATH', '/tmp');\n"
	                           "MRSETUP('HELPPATH', 'mr.hlp');\n"
	                           "MRSETUP('TEMPDIR', '/tmp');\n"
	                           "MRSETUP('SHELLPATH', '/bin/sh');\n"
	                           "MRSETUP('PAGE_BREAK', '\\\\f');\n"
	                           "MRSETUP('WORD_DELIMITERS', '._-');\n"
	                           "MRSETUP('DEFAULT_EXTENSIONS', 'txt;md');\n"
	                           "MRSETUP('TRUNCATE_SPACES', 'true');\n"
	                           "MRSETUP('EOF_CTRL_Z', 'false');\n"
	                           "MRSETUP('EOF_CR_LF', 'true');\n"
	                           "MRSETUP('TAB_EXPAND', 'false');\n"
	                           "MRSETUP('TAB_SIZE', '6');\n"
	                           "MRSETUP('BACKUP_FILES', 'false');\n"
	                           "MRSETUP('SHOW_EOF_MARKER', 'true');\n"
	                           "MRSETUP('SHOW_EOF_MARKER_EMOJI', 'false');\n"
	                           "MRSETUP('LINE_NUMBERS_POSITION', 'LEADING');\n"
	                           "MRSETUP('LINE_NUM_ZERO_FILL', 'true');\n"
		                           "MRSETUP('CURSOR_BEHAVIOUR', 'FREE_MOVEMENT');\n"
		                           "MRSETUP('SCROLLBAR_VISIBILITY', 'ALWAYS');\n"
		                           "MRSETUP('COLOR_OUTPUT_MODE', 'TERMINAL_PALETTE');\n"
	                           "MRSETUP('AUTODETECT_BINARY_FILES', 'false');\n"
	                           "MRSETUP('FILE_COMPARE_ORIGINAL_LEADING_GUTTERS', 'L');\n"
	                           "MRSETUP('FILE_COMPARE_ORIGINAL_TRAILING_GUTTERS', 'M');\n"
	                           "MRSETUP('FILE_COMPARE_COMPARE_LEADING_GUTTERS', 'LD');\n"
	                           "MRSETUP('FILE_COMPARE_COMPARE_TRAILING_GUTTERS', '');\n"
	                           "MRSETUP('FILE_COMPARE_START_CONFIGURATION', 'COMPARE_ORIGINAL');\n"
	                           "MRSETUP('FILE_COMPARE_COMPARE_PANEL_READ_ONLY', 'true');\n"
	                           "MRSETUP('BLOCK_MOVE', 'LEAVE_SPACE');\n"
	                           "MRSETUP('DEFAULT_MODE', 'OVERWRITE');\n"
		                           "WINDOWCOLORS('rgb24:101010/202020,111111/212121,121212/222222,131313/232323,141414/242424,151515/252525,161616/262626,171717/272727,181818/282828,191919/292929,1A1A1A/2A2A2A,1B1B1B/2B2B2B,1C1C1C/2C2C2C,1D1D1D/2D2D2D');\n"
		                           "MENUDIALOGCOLORS('rgb24:303030/404040,313131/414141,323232/424242,333333/434343,343434/444444,353535/454545,363636/464646,373737/474747,383838/484848,393939/494949,3A3A3A/4A4A4A,3B3B3B/4B4B4B,3C3C3C/4C4C4C,3D3D3D/4D4D4D,3E3E3E/4E4E4E,3F3F3F/4F4F4F,', '404040/505050,414141/515151,424242/525252,434343/535353,444444/545454,454545/555555,464646/565656,474747/575757,484848/585858,494949/595959,4A4A4A/5A5A5A,4B4B4B/5B5B5B,4C4C4C/5C5C5C,4D4D4D/5D5D5D,4E4E4E/5E5E5E,4F4F4F/5F5F5F');\n"
		                           "HELPCOLORS('rgb24:505050/606060,515151/616161,525252/626262,535353/636363,545454/646464,555555/656565,565656/666666,575757/676767,585858/686868,595959/696969');\n"
		                           "OTHERCOLORS('rgb24:707070/808080,717171/818181,727272/828282,737373/838383,747474/848484,757575/858585,767676/868686,777777/878787,787878/888888,797979/898989,7A7A7A/8A8A8A');\n"
	                           "END_MACRO;\n";
	std::vector<unsigned char> bytecode;
	std::string macroName;
	std::string compileError;
	int entryOffset = -1;

	if (!compileSource(source, bytecode, entryOffset, macroName, compileError)) {
		failureReason = "Compile failed: " + compileError;
		return false;
	}

	{
		VirtualMachine vm;
		std::string vmError;

		mrvmSetStartupSettingsMode(true);
		vm.executeAt(bytecode.data(), bytecode.size(), static_cast<size_t>(entryOffset), std::string(), macroName, true, true);
		mrvmSetStartupSettingsMode(false);

		if (firstVmError(vm.log, vmError)) {
			failureReason = "Startup context should allow MRSETUP, got: " + vmError;
			return false;
		}

		if (!validateMrsetupCorePaths(failureReason)) return false;
		if (!validateMrsetupGlobalSettings(failureReason)) return false;
		if (!validateMrsetupEditorSettings(failureReason)) return false;
		if (!validateMrsetupColorSettings(failureReason)) return false;
	}

	if (!validateMrsetupRuntimeRejection(bytecode, entryOffset, macroName, failureReason)) return false;

	failureReason.clear();
	return true;
}

bool testBinaryBoundaryProbe(std::string &failureReason) {
	const std::string path = "/tmp/mr_binary_boundary_probe_" + std::to_string(static_cast<long>(::getpid()));
	std::string content(24576, 'x');
	auto cleanup = [&]() { static_cast<void>(::remove(path.c_str())); };

	if (!writeTextFile(path, content) || fileContainsNulInBoundarySamples(path)) {
		cleanup();
		failureReason = "Binary boundary probe misclassified plain text.";
		return false;
	}
	content[0] = '\0';
	if (!writeTextFile(path, content) || !fileContainsNulInBoundarySamples(path)) {
		cleanup();
		failureReason = "Binary boundary probe missed a leading NUL.";
		return false;
	}
	content[0] = 'x';
	content.back() = '\0';
	if (!writeTextFile(path, content) || !fileContainsNulInBoundarySamples(path)) {
		cleanup();
		failureReason = "Binary boundary probe missed a trailing NUL.";
		return false;
	}
	content.back() = 'x';
	content[12288] = '\0';
	if (!writeTextFile(path, content) || fileContainsNulInBoundarySamples(path)) {
		cleanup();
		failureReason = "Binary boundary probe read outside its boundary samples.";
		return false;
	}
	cleanup();
	failureReason.clear();
	return true;
}

bool testMrsetupWindowColorThemeUriStartupLoad(std::string &failureReason) {
	const std::string themePath = "/tmp/mr-startup-window-colortheme-uri.mrmac";
	const std::string source = std::string("$MACRO Setup;\n") +
	                           "MRSETUP('SETTINGSPATH', '/tmp/mr_settings_theme_uri_probe.mrmac');\n" +
	                           "MRSETUP('WINDOW_COLORTHEME_URI', '" + themePath + "');\n" +
	                           "END_MACRO;\n";
	const std::string themeSource = "$MACRO MR_COLOR_THEME FROM EDIT;\n"
	                                "THEME_RESET();\n"
	                                "WINDOWCOLORS('rgb24:102030/203040,112131/213141,122232/223242,132333/233343,142434/243444,152535/253545,162636/263646,172737/273747,182838/283848,192939/293949,1A2A3A/2A3A4A,1B2B3B/2B3B4B,1C2C3C/2C3C4C,1D2D3D/2D3D4D');\n"
	                                "END_MACRO;\n";
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	std::vector<unsigned char> bytecode;
	std::string macroName;
	std::string compileError;
	std::string restoreError;
	int entryOffset = -1;
	bool restored = false;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		mrvmSetStartupSettingsMode(false);
		std::remove(themePath.c_str());
	};

	if (!writeTextFile(themePath, themeSource)) {
		failureReason = "Unable to write temporary startup color theme.";
		return false;
	}
	if (!compileSource(source, bytecode, entryOffset, macroName, compileError)) {
		failureReason = "Compile failed: " + compileError;
		restore();
		return false;
	}

	{
		VirtualMachine vm;
		std::string vmError;

		mrvmSetStartupSettingsMode(true);
		vm.executeAt(bytecode.data(), bytecode.size(), static_cast<size_t>(entryOffset), std::string(), macroName, true, true);
		mrvmSetStartupSettingsMode(false);
		if (firstVmError(vm.log, vmError)) {
			failureReason = "Startup WINDOW_COLORTHEME_URI should load theme, got: " + vmError;
			restore();
			return false;
		}
	}

	{
		MRColorSetupSettings colors = configuredColorSetupSettings();
		if (configuredColorThemeFilePath() != themePath) {
			failureReason = "Startup WINDOW_COLORTHEME_URI should persist active theme path.";
			restore();
			return false;
		}
		if (colors.windowColors[0] != MRRgbColorAttribute{0x102030u, 0x203040u} || colors.windowColors[8] != MRRgbColorAttribute{0x182838u, 0x283848u} ||
		    colors.windowColors[12] != MRRgbColorAttribute{0x1C2C3Cu, 0x2C3C4Cu}) {
			failureReason = "Startup WINDOW_COLORTHEME_URI should apply external RGB24 theme colors.";
			restore();
			return false;
		}
	}

	restore();
	if (!restored) {
		failureReason = "Unable to restore color settings after WINDOW_COLORTHEME_URI startup probe.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testPathDefaultsFromEnvironment(std::string &failureReason) {
	const std::string tmpdirProbe = "/tmp/mr_regression_env_tmpdir";
	const char *oldTmpdir = std::getenv("TMPDIR");
	const char *oldTemp = std::getenv("TEMP");
	const char *oldTmp = std::getenv("TMP");
	const char *oldShell = std::getenv("SHELL");
	std::string oldTmpdirValue = oldTmpdir != nullptr ? oldTmpdir : "";
	std::string oldTempValue = oldTemp != nullptr ? oldTemp : "";
	std::string oldTmpValue = oldTmp != nullptr ? oldTmp : "";
	std::string oldShellValue = oldShell != nullptr ? oldShell : "";
	bool hadTmpdir = oldTmpdir != nullptr;
	bool hadTemp = oldTemp != nullptr;
	bool hadTmp = oldTmp != nullptr;
	bool hadShell = oldShell != nullptr;

	auto restoreEnvironment = [&]() {
		if (hadTmpdir) setenv("TMPDIR", oldTmpdirValue.c_str(), 1);
		else
			unsetenv("TMPDIR");
		if (hadTemp) setenv("TEMP", oldTempValue.c_str(), 1);
		else
			unsetenv("TEMP");
		if (hadTmp) setenv("TMP", oldTmpValue.c_str(), 1);
		else
			unsetenv("TMP");
		if (hadShell) setenv("SHELL", oldShellValue.c_str(), 1);
		else
			unsetenv("SHELL");
	};

	if (::mkdir(tmpdirProbe.c_str(), 0700) != 0 && errno != EEXIST) {
		failureReason = "Unable to create TMPDIR probe directory.";
		return false;
	}

	setenv("TMPDIR", tmpdirProbe.c_str(), 1);
	unsetenv("TEMP");
	unsetenv("TMP");
	setenv("SHELL", "/bin/sh", 1);

	if (configuredTempDirectoryPath() != tmpdirProbe) {
		restoreEnvironment();
		failureReason = "configuredTempDirectoryPath() did not use TMPDIR fallback.";
		return false;
	}
	if (configuredShellExecutablePath() != "/bin/sh") {
		restoreEnvironment();
		failureReason = "configuredShellExecutablePath() did not use SHELL/OS fallback.";
		return false;
	}
	if (configuredHelpFilePath().empty()) {
		restoreEnvironment();
		failureReason = "configuredHelpFilePath() returned an empty fallback.";
		return false;
	}
	if (defaultMacroDirectoryPath().empty()) {
		restoreEnvironment();
		failureReason = "defaultMacroDirectoryPath() returned an empty fallback.";
		return false;
	}

	restoreEnvironment();
	failureReason.clear();
	return true;
}

bool testSettingsMacroAutoCreate(std::string &failureReason) {
	const std::string root = "/tmp/mr_regression_settings_bootstrap_" + std::to_string(static_cast<long>(::getpid()));
	const std::string settingsPath = root + "/cfg/mr/settings.mrmac";
	const std::string expectedSettingLine = "MRSETUP('SETTINGSPATH', '" + settingsPath + "');";
	std::string content;
	std::string ioError;
	struct stat st;

	(void)::remove(settingsPath.c_str());
	(void)::rmdir((root + "/cfg/mr").c_str());
	(void)::rmdir((root + "/cfg").c_str());
	(void)::rmdir(root.c_str());

	if (::mkdir(root.c_str(), 0700) != 0 && errno != EEXIST) {
		failureReason = "Unable to create bootstrap probe root directory.";
		return false;
	}
	if (!ensureSettingsMacroFileExists(settingsPath, &failureReason)) return false;
	if (::stat(settingsPath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		failureReason = "Auto-created settings.mrmac is missing.";
		return false;
	}
	if (!readTextFile(settingsPath, content, ioError)) {
		failureReason = "Unable to read auto-created settings.mrmac: " + ioError;
		return false;
	}
	if (content.find("$MACRO MR_SETTINGS FROM EDIT;") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac has no MR_SETTINGS macro header.";
		return false;
	}
	if (content.find(expectedSettingLine) == std::string::npos) {
		failureReason = "Auto-created settings.mrmac did not persist the selected settings URI.";
		return false;
	}
	if (content.find("MRSETUP('MACROPATH', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing MACROPATH.";
		return false;
	}
	if (content.find("MRSETUP('HELPPATH', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing HELPPATH.";
		return false;
	}
	if (content.find("MRSETUP('TEMPDIR', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing TEMPDIR.";
		return false;
	}
	if (content.find("MRSETUP('SHELLPATH', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing SHELLPATH.";
		return false;
	}
	if (content.find("MRSETUP('WINDOW_MANAGER', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing WINDOW_MANAGER.";
		return false;
	}
	if (content.find("MRSETUP('MESSAGES', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing MESSAGES.";
		return false;
	}
	if (content.find("MRSETUP('VIRTUAL_DESKTOPS', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing VIRTUAL_DESKTOPS.";
		return false;
	}
	if (content.find("MRSETUP('CURSOR_BEHAVIOUR', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing CURSOR_BEHAVIOUR.";
		return false;
	}
	if (content.find("MRSETUP('SCROLLBAR_VISIBILITY', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing SCROLLBAR_VISIBILITY.";
		return false;
	}
	if (content.find("MRSETUP('COLOR_OUTPUT_MODE', 'TERMINAL_PALETTE');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac does not preserve the built-in BIOS theme through terminal palette indices.";
		return false;
	}
	if (content.find("MRSETUP('FILE_COMPARE_ORIGINAL_LEADING_GUTTERS', '") == std::string::npos || content.find("MRSETUP('FILE_COMPARE_ORIGINAL_TRAILING_GUTTERS', '") == std::string::npos ||
	    content.find("MRSETUP('FILE_COMPARE_COMPARE_LEADING_GUTTERS', '") == std::string::npos || content.find("MRSETUP('FILE_COMPARE_COMPARE_TRAILING_GUTTERS', '") == std::string::npos ||
	    content.find("MRSETUP('FILE_COMPARE_START_CONFIGURATION', '") == std::string::npos || content.find("MRSETUP('FILE_COMPARE_COMPARE_PANEL_READ_ONLY', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing file compare UI settings.";
		return false;
	}
	if (content.find("MRSETUP('AUTOLOAD_WORKSPACE', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing AUTOLOAD_WORKSPACE.";
		return false;
	}
	if (content.find("MRSETUP('PAGE_BREAK', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing PAGE_BREAK.";
		return false;
	}
	if (content.find("MRSETUP('WORD_DELIMITERS', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing WORD_DELIMITERS.";
		return false;
	}
	if (content.find("MRSETUP('DEFAULT_EXTENSIONS', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing DEFAULT_EXTENSIONS.";
		return false;
	}
	if (content.find("MRSETUP('TRUNCATE_SPACES', 'true');") == std::string::npos && content.find("MRSETUP('TRUNCATE_SPACES', 'false');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist TRUNCATE_SPACES as true/false.";
		return false;
	}
	if (content.find("MRSETUP('TAB_EXPAND', 'true');") == std::string::npos && content.find("MRSETUP('TAB_EXPAND', 'false');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist TAB_EXPAND as true/false.";
		return false;
	}
	if (content.find("MRSETUP('TAB_SIZE', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing TAB_SIZE.";
		return false;
	}
	if (content.find("MRSETUP('BACKUP_FILES', 'true');") == std::string::npos && content.find("MRSETUP('BACKUP_FILES', 'false');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist BACKUP_FILES as true/false.";
		return false;
	}
	if (content.find("MRSETUP('SHOW_EOF_MARKER', 'true');") == std::string::npos && content.find("MRSETUP('SHOW_EOF_MARKER', 'false');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist SHOW_EOF_MARKER as true/false.";
		return false;
	}
	if (content.find("MRSETUP('SHOW_EOF_MARKER_EMOJI', 'true');") == std::string::npos && content.find("MRSETUP('SHOW_EOF_MARKER_EMOJI', 'false');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist SHOW_EOF_MARKER_EMOJI as true/false.";
		return false;
	}
	if (content.find("MRSETUP('LINE_NUMBERS_POSITION', 'OFF');") == std::string::npos && content.find("MRSETUP('LINE_NUMBERS_POSITION', 'LEADING');") == std::string::npos && content.find("MRSETUP('LINE_NUMBERS_POSITION', 'TRAILING');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist LINE_NUMBERS_POSITION as OFF/LEADING/TRAILING.";
		return false;
	}
	if (content.find("MRSETUP('LINE_NUM_ZERO_FILL', 'true');") == std::string::npos && content.find("MRSETUP('LINE_NUM_ZERO_FILL', 'false');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist LINE_NUM_ZERO_FILL as true/false.";
		return false;
	}
	if (content.find("MRSETUP('BLOCK_MOVE', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing BLOCK_MOVE.";
		return false;
	}
	if (content.find("MRSETUP('DEFAULT_MODE', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing DEFAULT_MODE.";
		return false;
	}
	if (content.find("MRSETUP('WINDOW_COLORTHEME_URI', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing WINDOW_COLORTHEME_URI.";
		return false;
	}
	if (content.find("MRSETUP('WINDOWCOLORS', '") != std::string::npos || content.find("MRSETUP('MENUDIALOGCOLORS', '") != std::string::npos || content.find("MRSETUP('HELPCOLORS', '") != std::string::npos || content.find("MRSETUP('OTHERCOLORS', '") != std::string::npos) {
		failureReason = "settings.mrmac must not contain direct color lists after theme migration.";
		return false;
	}
	if (content.find("MRSETUP('COLORTHEMEURI', '") != std::string::npos) {
		failureReason = "Auto-created settings.mrmac must not persist obsolete COLORTHEMEURI.";
		return false;
	}
	if (content.find("MRSETUP('CURSORVISIBILITY', '") != std::string::npos) {
		failureReason = "Auto-created settings.mrmac must not include deprecated CURSORVISIBILITY.";
		return false;
	}
	if (content.find("MRSETUP('PAGEBREAK', '") != std::string::npos || content.find("MRSETUP('WORDDELIMS', '") != std::string::npos || content.find("MRSETUP('DEFAULTEXTS', '") != std::string::npos || content.find("MRSETUP('TRUNCSPACES', '") != std::string::npos || content.find("MRSETUP('EOFCTRLZ', '") != std::string::npos || content.find("MRSETUP('EOFCRLF', '") != std::string::npos || content.find("MRSETUP('TABEXPAND', '") != std::string::npos || content.find("MRSETUP('TABSIZE', '") != std::string::npos || content.find("MRSETUP('BACKUPFILES', '") != std::string::npos || content.find("MRSETUP('SHOWEOFMARKER', '") != std::string::npos || content.find("MRSETUP('SHOWEOFMARKEREMOJI', '") != std::string::npos || content.find("MRSETUP('SHOWLINENUMBERS', '") != std::string::npos || content.find("MRSETUP('LINENUMZEROFILL', '") != std::string::npos || content.find("MRSETUP('PERSISTENTBLOCKS', '") != std::string::npos || content.find("MRSETUP('COLBLOCKMOVE', '") != std::string::npos ||
	    content.find("MRSETUP('DEFAULTMODE', '") != std::string::npos) {
		failureReason = "Auto-created settings.mrmac must not rewrite deprecated edit-setting keys.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testToFromHeaders(std::string &failureReason) {
	static const char source[] = "$MACRO Alpha TO <AltB> FROM EDIT TRANS;\n"
	                             "END_MACRO;\n"
	                             "$MACRO Beta TO <CtrlF7> FROM DOS_SHELL DUMP;\n"
	                             "END_MACRO;\n"
	                             "$MACRO Gamma TO <F5> FROM ALL PERM;\n"
	                             "END_MACRO;\n"
	                             "$MACRO ShiftTab TO <ShftTAB> FROM EDIT;\n"
	                             "END_MACRO;\n"
	                             "$MACRO Delta;\n"
	                             "END_MACRO;\n";
	size_t bytecodeSize = 0;
	unsigned char *bytecode = compile_macro_code(source, &bytecodeSize);

	struct ExpectedMacro {
		const char *name;
		const char *keyspec;
		int mode;
		int flags;
	};
	static const ExpectedMacro expected[] = {{"Alpha", "<AltB>", MACRO_MODE_EDIT, MACRO_ATTR_TRANS}, {"Beta", "<CtrlF7>", MACRO_MODE_DOS_SHELL, MACRO_ATTR_DUMP}, {"Gamma", "<F5>", MACRO_MODE_ALL, MACRO_ATTR_PERM}, {"ShiftTab", "<ShftTAB>", MACRO_MODE_EDIT, 0}, {"Delta", "", MACRO_MODE_EDIT, 0}};

	if (bytecode == NULL) {
		failureReason = std::string("Compilation failed: ") + get_last_compile_error();
		return false;
	}
	std::free(bytecode);

	if (get_compiled_macro_count() != static_cast<int>(sizeof(expected) / sizeof(expected[0]))) {
		failureReason = "Unexpected compiled macro count.";
		return false;
	}

	for (int i = 0; i < get_compiled_macro_count(); ++i) {
		const char *actualName = get_compiled_macro_name(i);
		const char *actualKeyspec = get_compiled_macro_keyspec(i);
		int actualMode = get_compiled_macro_mode(i);
		int actualFlags = get_compiled_macro_flags(i);

		if (actualName == NULL || std::strcmp(actualName, expected[i].name) != 0) {
			failureReason = std::string("Name mismatch at index ") + std::to_string(i) + ".";
			return false;
		}
		if (actualKeyspec == NULL || std::strcmp(actualKeyspec, expected[i].keyspec) != 0) {
			failureReason = std::string("TO mismatch for macro ") + expected[i].name + ".";
			return false;
		}
		if (actualMode != expected[i].mode) {
			failureReason = std::string("FROM mismatch for macro ") + expected[i].name + ".";
			return false;
		}
		if (actualFlags != expected[i].flags) {
			failureReason = std::string("Attribute mismatch for macro ") + expected[i].name + ".";
			return false;
		}
	}

	if (!expectCompileError("$MACRO Bad TO <NoSuchKey>;\nEND_MACRO;\n", "Keycode not supported.", failureReason)) return false;
	if (!expectCompileError("$MACRO Bad TO <F1> TO <F2>;\nEND_MACRO;\n", "Duplicate TO clause.", failureReason)) return false;
	if (!expectCompileError("$MACRO Bad FROM EDIT FROM ALL;\nEND_MACRO;\n", "Duplicate FROM clause.", failureReason)) return false;
	if (!expectCompileError("$MACRO Bad FROM INVALID;\nEND_MACRO;\n", "Mode expected.", failureReason)) return false;

	failureReason.clear();
	return true;
}

bool testToFromDispatch(std::string &failureReason) {
	static const char macroPath[] = "/tmp/mr_tofrom_dispatch.mrmac";
	const std::string macroSource = "$MACRO HitEdit TO <AltB> FROM EDIT;\n"
	                                "SET_GLOBAL_INT('HIT_EDIT', 1);\n"
	                                "END_MACRO;\n"
	                                "$MACRO HitEditOverride TO <AltB> FROM EDIT;\n"
	                                "SET_GLOBAL_INT('HIT_EDIT', 2);\n"
	                                "END_MACRO;\n"
	                                "$MACRO HitShiftTab TO <ShftTAB> FROM EDIT;\n"
	                                "SET_GLOBAL_INT('HIT_SHIFT_TAB', 1);\n"
	                                "END_MACRO;\n"
	                                "$MACRO HitCtrlA TO <CtrlA> FROM EDIT;\n"
	                                "SET_GLOBAL_INT('HIT_CTRL_A', 1);\n"
	                                "END_MACRO;\n"
	                                "$MACRO HitAlt1 TO <Alt1> FROM EDIT;\n"
	                                "SET_GLOBAL_INT('HIT_ALT_1', 1);\n"
	                                "END_MACRO;\n"
	                                "$MACRO HitShell TO <AltS> FROM DOS_SHELL;\n"
	                                "SET_GLOBAL_INT('HIT_SHELL', 1);\n"
	                                "END_MACRO;\n";
	std::string loaderSource;
	size_t bytecodeSize = 0;
	unsigned char *bytecode = NULL;
	VirtualMachine vm;
	std::string executedMacroName;
	bool ok = false;

	if (!writeTextFile(std::string(macroPath), std::string(macroSource))) {
		failureReason = "Unable to create TO/FROM dispatch probe macro file.";
		return false;
	}

	loaderSource = "$MACRO Main;\nLOAD_MACRO_FILE('";
	loaderSource += macroPath;
	loaderSource += "');\nEND_MACRO;\n";

	bytecode = compile_macro_code(loaderSource.c_str(), &bytecodeSize);
	if (bytecode == NULL) {
		failureReason = std::string("Compilation failed: ") + get_last_compile_error();
		std::remove(macroPath);
		return false;
	}
	vm.execute(bytecode, bytecodeSize);
	std::free(bytecode);

	ok = mrvmRunAssignedMacroForKey(kbAltB, 0, executedMacroName, nullptr) && executedMacroName == "HitEditOverride";
	if (!ok) {
		failureReason = "Edit-mode key dispatch failed.";
		std::remove(macroPath);
		return false;
	}
	ok = mrvmRunAssignedMacroForKey(kbShiftTab, 0, executedMacroName, nullptr) && executedMacroName == "HitShiftTab";
	if (!ok) {
		failureReason = "Shift+Tab dispatch failed.";
		std::remove(macroPath);
		return false;
	}
	ok = mrvmRunAssignedMacroForKey(kbCtrlA, 0, executedMacroName, nullptr) && executedMacroName == "HitCtrlA";
	if (!ok) {
		failureReason = "Ctrl+A dispatch failed.";
		std::remove(macroPath);
		return false;
	}
	ok = mrvmRunAssignedMacroForKey(kbAlt1, 0, executedMacroName, nullptr) && executedMacroName == "HitAlt1";
	if (!ok) {
		failureReason = "Alt+1 dispatch failed.";
		std::remove(macroPath);
		return false;
	}
	if (mrvmRunAssignedMacroForKey(kbAltS, 0, executedMacroName, nullptr)) {
		failureReason = "DOS_SHELL macro should not execute in EDIT mode.";
		std::remove(macroPath);
		return false;
	}
	if (mrvmRunAssignedMacroForKey(kbF12, 0, executedMacroName, nullptr)) {
		failureReason = "Unexpected macro dispatch for unbound key.";
		std::remove(macroPath);
		return false;
	}

	std::remove(macroPath);
	failureReason.clear();
	return true;
}


bool testDialogPaletteOverridesAbsent(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("app/MREditorApp.cpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read MREditorApp.cpp for palette guard: " + ioError;
		return false;
	}
	if (content.find("palette[32] =") != std::string::npos || content.find("palette[33] =") != std::string::npos || content.find("palette[34] =") != std::string::npos || content.find("palette[37] =") != std::string::npos || content.find("palette[38] =") != std::string::npos || content.find("palette[39] =") != std::string::npos || content.find("palette[40] =") != std::string::npos || content.find("palette[41] =") != std::string::npos || content.find("palette[42] =") != std::string::npos || content.find("palette[43] =") != std::string::npos || content.find("palette[44] =") != std::string::npos || content.find("palette[45] =") != std::string::npos || content.find("palette[46] =") != std::string::npos || content.find("palette[47] =") != std::string::npos || content.find("palette[48] =") != std::string::npos || content.find("palette[49] =") != std::string::npos || content.find("palette[50] =") != std::string::npos || content.find("palette[51] =") != std::string::npos ||
	    content.find("palette[52] =") != std::string::npos || content.find("palette[53] =") != std::string::npos || content.find("palette[54] =") != std::string::npos || content.find("palette[57] =") != std::string::npos || content.find("palette[58] =") != std::string::npos || content.find("palette[59] =") != std::string::npos || content.find("palette[60] =") != std::string::npos || content.find("palette[61] =") != std::string::npos || content.find("palette[62] =") != std::string::npos || content.find("palette[63] =") != std::string::npos) {
		failureReason = "MREditorApp.cpp must not hardcode dialog colors outside global scrollbar synchronization.";
		return false;
	}
	if (content.find("static const TPalette basePalette(cpAppColor, sizeof(cpAppColor) - 1);") == std::string::npos && content.find("static const TPalette &basePalette = extendedAppBasePalette();") == std::string::npos) {
		failureReason = "MREditorApp::getPalette must use a stable base palette source before applying overrides.";
		return false;
	}
	if (content.find("palette = basePalette;") == std::string::npos || content.find("slot <= kMrPaletteMax") == std::string::npos) {
		failureReason = "MREditorApp::getPalette must rebuild each call and include extension slots up to kMrPaletteMax.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testExtendedBasePaletteInitializationGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("app/MREditorApp.cpp");
	std::string content;
	std::string functionBody;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read MREditorApp.cpp for base palette initialization guard: " + ioError;
		return false;
	}
	{
		const std::size_t start = content.find("const TPalette &extendedAppBasePalette()");
		const std::size_t end = content.find("\n} // namespace", start);

		if (start == std::string::npos || end == std::string::npos) {
			failureReason = "Unable to isolate extendedAppBasePalette().";
			return false;
		}
		functionBody = content.substr(start, end - start);
	}
	if (functionBody.find("static const int kTotalSlots = kMrPaletteMax;") == std::string::npos ||
	    functionBody.find("TColorAttr data[kTotalSlots];") == std::string::npos ||
	    functionBody.find("for (; i < kTotalSlots; ++i)") == std::string::npos ||
	    functionBody.find("data[i] = data[1 - 1];") == std::string::npos) {
		failureReason = "extendedAppBasePalette must initialize every slot up to kMrPaletteMax before applying dedicated defaults.";
		return false;
	}
	if (functionBody.find("return TPalette(data, static_cast<ushort>(kTotalSlots));") == std::string::npos) {
		failureReason = "extendedAppBasePalette must expose all slots up to kMrPaletteMax.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testTerminalPalettePreservesBiosDefaultsAndUsesXTerm256(std::string &failureReason) {
	static constexpr std::array<std::uint32_t, 16> vgaRgb = {
		0x000000u, 0x0000AAu, 0x00AA00u, 0x00AAAAu, 0xAA0000u, 0xAA00AAu, 0xAA5500u, 0xAAAAAAu,
		0x555555u, 0x5555FFu, 0x55FF55u, 0x55FFFFu, 0xFF5555u, 0xFF55FFu, 0xFFFF55u, 0xFFFFFFu
	};

	if (!testSettingsMacroAutoCreate(failureReason)) return false;

	for (std::size_t biosIndex = 0; biosIndex < vgaRgb.size(); ++biosIndex) {
		const TColorAttr projected = projectColorAttribute({vgaRgb[biosIndex], vgaRgb[biosIndex]}, MRColorOutputMode::TerminalPalette);
		const unsigned char expected = BIOStoXTerm16(TColorBIOS(static_cast<unsigned char>(biosIndex)));
		const TColorDesired foreground = getFore(projected);
		const TColorDesired background = getBack(projected);

		if (!foreground.isXTerm() || !background.isXTerm() || static_cast<unsigned char>(foreground.asXTerm()) != expected || static_cast<unsigned char>(background.asXTerm()) != expected) {
			failureReason = "Terminal palette projection does not preserve an exact VGA default through its ANSI palette index.";
			return false;
		}
	}

	const MRRgbColorAttribute source{0xAA5500u, 0x55AAFFu};
	const TColorAttr projected = projectColorAttribute(source, MRColorOutputMode::TerminalPalette);
	const TColorDesired foreground = getFore(projected);
	const TColorDesired background = getBack(projected);
	const unsigned char expectedBackground = RGBtoXTerm256(TColorRGB(source.backgroundRgb));

	if (!foreground.isXTerm() || !background.isXTerm()) {
		failureReason = "Terminal palette projection must retain xterm palette indices instead of BIOS attributes.";
		return false;
	}
	if (static_cast<unsigned char>(foreground.asXTerm()) != BIOStoXTerm16(TColorBIOS(6)) || static_cast<unsigned char>(background.asXTerm()) != expectedBackground) {
		failureReason = "Terminal palette projection does not combine exact ANSI defaults with xterm-256 quantization.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testWindowColorGroupTargetsBlueWindowPalette(std::string &failureReason) {
	std::array<MRRgbColorAttribute, MRColorSetupSettings::kWindowCount> probeValues{};
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::size_t itemCount = 0;
	const MRColorSetupItem *items = colorSetupGroupItems(MRColorSetupGroup::Window, itemCount);
	std::string errorText;
	TColorAttr value;
	bool restoreOk = true;
	for (std::size_t i = 0; i < probeValues.size(); ++i) probeValues[i] = MRRgbColorAttribute{static_cast<std::uint32_t>(0x102030u + i), static_cast<std::uint32_t>(0x405060u + i)};

	auto restore = [&]() {
		if (!restoreOk) return;
		restoreOk = setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, previous.windowColors.data(), previous.windowColors.size(), &errorText);
	};

	if (items == nullptr || itemCount != probeValues.size()) {
		failureReason = "Unexpected WINDOWCOLORS item mapping.";
		return false;
	}

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, probeValues.data(), probeValues.size(), &errorText)) {
		failureReason = "Unable to set WINDOWCOLORS probe values: " + errorText;
		return false;
	}

	for (std::size_t i = 0; i < itemCount; ++i) {
		unsigned char slot = items[i].paletteIndex;
		bool isExpectedSlot = (slot == 8 || slot == 9 || slot == 13 || slot == 14 || slot == kMrPaletteCurrentLine || slot == kMrPaletteCurrentLineInBlock || slot == kMrPaletteChangedText || slot == kMrPaletteLineNumbers || slot == kMrPaletteEofMarker || slot == kMrPaletteCodeFolding || slot == kMrPaletteCodeFoldingMarker || slot == kMrPaletteFormatRuler || slot == kMrPaletteFocusedPaneBorder || slot == kMrPaletteDiagnosticInformation);
		if (!colorSlotOverride(configuredColorSetupSettings(), items[i].paletteIndex, MRColorOutputMode::RgbAutomatic, value)) {
			restore();
			failureReason = "WINDOWCOLORS item must override its mapped palette slot.";
			return false;
		}
		if (value != projectColorAttribute(probeValues[i], MRColorOutputMode::RgbAutomatic) || !isExpectedSlot) {
			restore();
			failureReason = "WINDOWCOLORS slot mapping mismatch.";
			return false;
		}
	}

	restore();
	if (!restoreOk) {
		failureReason = "Unable to restore WINDOWCOLORS after probe: " + errorText;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testMenuDialogColorGroupTargetsExpectedSlots(std::string &failureReason) {
	std::array<MRRgbColorAttribute, MRColorSetupSettings::kMenuDialogCount> probeValues{};
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::size_t itemCount = 0;
	const MRColorSetupItem *items = colorSetupGroupItems(MRColorSetupGroup::MenuDialog, itemCount);
	std::string errorText;
	TColorAttr value;
	bool restoreOk = true;
	for (std::size_t i = 0; i < probeValues.size(); ++i) probeValues[i] = MRRgbColorAttribute{static_cast<std::uint32_t>(0x203040u + i), static_cast<std::uint32_t>(0x506070u + i)};

	auto restore = [&]() {
		if (!restoreOk) return;
		restoreOk = setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, previous.menuDialogColors.data(), previous.menuDialogColors.size(), &errorText);
	};

	if (items == nullptr || itemCount != probeValues.size()) {
		failureReason = "Unexpected MENUDIALOGCOLORS item mapping.";
		return false;
	}
	if (std::string(items[kMenuDialogIndexMenuBarHotkey].label) != "Hotkeys on menu bar" || items[kMenuDialogIndexMenuBarHotkey].paletteIndex != kMrPaletteMenuBarHotkey) {
		failureReason = "MENUDIALOGCOLORS must expose a dedicated Hotkeys on menu bar item.";
		return false;
	}

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, probeValues.data(), probeValues.size(), &errorText)) {
		failureReason = "Unable to set MENUDIALOGCOLORS probe values: " + errorText;
		return false;
	}

	for (std::size_t i = 0; i < itemCount; ++i) {
		unsigned char slot = items[i].paletteIndex;
		bool isMenuSlot = slot >= 2 && slot <= 6;
		bool isGrayDialogSlot = slot >= 32 && slot <= 63;
		bool isExtendedDialogSlot = slot == kMrPaletteDialogInactiveElements || slot == kMrPaletteDropListDescription || slot == kMrPaletteDropListSelectedInactive || slot == kMrPaletteMenuBarHotkey;
		if (!colorSlotOverride(configuredColorSetupSettings(), slot, MRColorOutputMode::RgbAutomatic, value)) {
			restore();
			failureReason = "MENUDIALOGCOLORS item must override its mapped palette slot.";
			return false;
		}
		if (value != projectColorAttribute(probeValues[i], MRColorOutputMode::RgbAutomatic) || (!isMenuSlot && !isGrayDialogSlot && !isExtendedDialogSlot)) {
			restore();
			failureReason = "MENUDIALOGCOLORS slot mapping mismatch.";
			return false;
		}
	}

	restore();
	if (!restoreOk) {
		failureReason = "Unable to restore MENUDIALOGCOLORS after probe: " + errorText;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testMenuDialogSemanticLabelsGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	MRColorSetupSettings before = configuredColorSetupSettings();
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	if (applyConfiguredColorSetupValue("MENUDIALOGCOLORS", "v1:10,11,12,13,14,15,16,17,18,19,1A,1B,1C,1D", &errorText)) {
		restore();
		failureReason = "Legacy MENUDIALOGCOLORS syntax must not be accepted.";
		return false;
	}
	if (configuredColorSetupSettings() != before) {
		restore();
		failureReason = "Rejected legacy MENUDIALOGCOLORS syntax must not mutate runtime colors.";
		return false;
	}

	if (applyConfiguredColorSetupValue("MENUDIALOGCOLORS", "rgb24:102030/405060", &errorText)) {
		restore();
		failureReason = "Wrong-sized RGB24 MENUDIALOGCOLORS syntax must not be accepted.";
		return false;
	}
	if (configuredColorSetupSettings() != before) {
		restore();
		failureReason = "Rejected wrong-sized RGB24 MENUDIALOGCOLORS syntax must not mutate runtime colors.";
		return false;
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after MENUDIALOG legacy probe: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testMenuEntryHotkeySelectionAliasGuard(std::string &failureReason) {
	std::array<MRRgbColorAttribute, MRColorSetupSettings::kMenuDialogCount> probeValues{};
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::string menuBarContent;
	std::string errorText;
	std::string ioError;
	TColorAttr normalHotkey;
	TColorAttr selectedHotkey;
	TColorAttr menuBarHotkey;
	bool restoreOk = true;
	for (std::size_t i = 0; i < probeValues.size(); ++i) probeValues[i] = MRRgbColorAttribute{static_cast<std::uint32_t>(0x304050u + i), static_cast<std::uint32_t>(0x607080u + i)};

	auto restore = [&]() {
		if (!restoreOk) return;
		restoreOk = setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, previous.menuDialogColors.data(), previous.menuDialogColors.size(), &errorText);
	};

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, probeValues.data(), probeValues.size(), &errorText)) {
		failureReason = "Unable to set MENUDIALOGCOLORS probe values: " + errorText;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRMenuBar.cpp"), menuBarContent, ioError)) {
		restore();
		failureReason = "Unable to read MRMenuBar.cpp for menu-bar hotkey guard: " + ioError;
		return false;
	}
	MRColorSetupSettings configured = configuredColorSetupSettings();
	if (!colorSlotOverride(configured, 4, MRColorOutputMode::RgbAutomatic, normalHotkey)) {
		restore();
		failureReason = "Palette slot 4 (entry-hotkey) must be overrideable.";
		return false;
	}
	if (!colorSlotOverride(configured, 7, MRColorOutputMode::RgbAutomatic, selectedHotkey)) {
		restore();
		failureReason = "Palette slot 7 (selected entry-hotkey) must mirror entry-hotkey.";
		return false;
	}
	if (!colorSlotOverride(configured, kMrPaletteMenuBarHotkey, MRColorOutputMode::RgbAutomatic, menuBarHotkey)) {
		restore();
		failureReason = "Menu bar hotkey slot must be overrideable.";
		return false;
	}
	if (normalHotkey != projectColorAttribute(probeValues[2], MRColorOutputMode::RgbAutomatic) || selectedHotkey != projectColorAttribute(probeValues[2], MRColorOutputMode::RgbAutomatic)) {
		restore();
		failureReason = "Entry-hotkey and selected entry-hotkey must resolve to the same configured color.";
		return false;
	}
	if (menuBarHotkey != projectColorAttribute(probeValues[kMenuDialogIndexMenuBarHotkey], MRColorOutputMode::RgbAutomatic) || menuBarHotkey == normalHotkey) {
		restore();
		failureReason = "Menu bar hotkeys must use their own configured color, independent from menu element hotkeys.";
		return false;
	}
	if (menuBarContent.find("configuredColorSlotOverride(kMrPaletteMenuBarHotkey") == std::string::npos || menuBarContent.find("markedHotkeyColumn(p->name)") == std::string::npos || menuBarContent.find("b.putAttribute(static_cast<ushort>(x + 1 + hotkeyColumn), cMenuBarHotkey)") == std::string::npos) {
		restore();
		failureReason = "MRMenuBar must recolor top-level menu hotkeys with the dedicated menu-bar hotkey slot.";
		return false;
	}

	restore();
	if (!restoreOk) {
		failureReason = "Unable to restore MENUDIALOGCOLORS after hotkey alias probe: " + errorText;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testDialogFrameAndBackgroundPropagationGuard(std::string &failureReason) {
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::string errorText;
	TColorAttr value;
	bool restoreOk = true;

	auto restore = [&]() {
		if (!restoreOk) return;
		restoreOk = setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, previous.menuDialogColors.data(), previous.menuDialogColors.size(), &errorText);
	};

	// Set explicit probe colors for:
	// - inactive radio/checkbox controls (slot 62)
	// - dialog frame (slot 33)
	// - dialog text (slot 37)
	// - dialog background (slot 32)
	auto probe = previous.menuDialogColors;
	if (probe.size() < 17) {
		failureReason = "MENUDIALOGCOLORS must expose frame/text/background entries.";
		return false;
	}
	probe[kMenuDialogIndexInactiveControls] = MRRgbColorAttribute{0x102030u, 0x405060u};
	probe[kMenuDialogIndexDialogFrame] = MRRgbColorAttribute{0x203040u, 0x506070u};
	probe[kMenuDialogIndexDialogText] = MRRgbColorAttribute{0x304050u, 0x607080u};
	probe[kMenuDialogIndexDialogBackground] = MRRgbColorAttribute{0x405060u, 0x708090u};

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, probe.data(), probe.size(), &errorText)) {
		failureReason = "Unable to set MENUDIALOGCOLORS frame/background probe values: " + errorText;
		return false;
	}

	static const unsigned char frameSlots[] = {33, 34, 65, 66, 97, 98};
	for (unsigned char slot : frameSlots) {
		if (!colorSlotOverride(configuredColorSetupSettings(), slot, MRColorOutputMode::RgbAutomatic, value)) {
			restore();
			failureReason = "Dialog frame slot override missing.";
			return false;
		}
		if (value != projectColorAttribute(probe[kMenuDialogIndexDialogFrame], MRColorOutputMode::RgbAutomatic)) {
			restore();
			failureReason = "Dialog frame propagation mismatch.";
			return false;
		}
	}

	static const unsigned char textSlots[] = {37, 69, 101};
	for (unsigned char slot : textSlots) {
		if (!colorSlotOverride(configuredColorSetupSettings(), slot, MRColorOutputMode::RgbAutomatic, value)) {
			restore();
			failureReason = "Dialog text slot override missing.";
			return false;
		}
		if (value != projectColorAttribute(probe[kMenuDialogIndexDialogText], MRColorOutputMode::RgbAutomatic)) {
			restore();
			failureReason = "Dialog text propagation mismatch.";
			return false;
		}
	}

	static const unsigned char backgroundSlots[] = {32, 64, 96};
	for (unsigned char slot : backgroundSlots) {
		if (!colorSlotOverride(configuredColorSetupSettings(), slot, MRColorOutputMode::RgbAutomatic, value)) {
			restore();
			failureReason = "Dialog background slot override missing.";
			return false;
		}
		if (value != projectColorAttribute(probe[kMenuDialogIndexDialogBackground], MRColorOutputMode::RgbAutomatic)) {
			restore();
			failureReason = "Dialog background propagation mismatch.";
			return false;
		}
	}

	static const unsigned char inactiveControlSlots[] = {kPaletteDialogInactiveControlsGray, kPaletteDialogInactiveControlsBlue, kPaletteDialogInactiveControlsCyan};
	for (unsigned char slot : inactiveControlSlots) {
		if (!colorSlotOverride(configuredColorSetupSettings(), slot, MRColorOutputMode::RgbAutomatic, value)) {
			restore();
			failureReason = "Dialog inactive-control slot override missing.";
			return false;
		}
		if (value != projectColorAttribute(probe[kMenuDialogIndexInactiveControls], MRColorOutputMode::RgbAutomatic)) {
			restore();
			failureReason = "Dialog inactive-control propagation mismatch.";
			return false;
		}
	}

	restore();
	if (!restoreOk) {
		failureReason = "Unable to restore MENUDIALOGCOLORS after frame/background probe: " + errorText;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testTouchedRangeMidInsertGuard(std::string &failureReason) {
	mr::editor::TextDocument document;
	mr::editor::EditTransaction tx("mid-insert");
	std::string initial = "alpha\nbeta\ngamma\n";
	mr::editor::CommitResult result;

	document.setText(initial);
	tx.insert(6, "X");
	result = document.tryApply(tx, document.version());
	if (!result.applied() || !result.changed()) {
		failureReason = "Mid-insert touched-range guard expected an applied change.";
		return false;
	}
	if (result.change.touchedRange.start != 6 || result.change.touchedRange.end != 7) {
		failureReason = "Touched range for mid-insert must stay local and must not extend to EOF.";
		return false;
	}
	failureReason.clear();
	return true;
}

enum class PieceTableHarnessOperationKind : unsigned char {
	SetText,
	Insert,
	Erase,
	Replace
};

struct PieceTableHarnessOperation {
	PieceTableHarnessOperationKind kind;
	mr::editor::Range range;
	std::string text;

	PieceTableHarnessOperation() : kind(PieceTableHarnessOperationKind::Insert), range(), text() {
	}
};

struct PieceTableHarnessRng {
	unsigned int state;

	explicit PieceTableHarnessRng(unsigned int seed) : state(seed) {
	}

	unsigned int next() {
		state = state * 1664525u + 1013904223u;
		return state;
	}

	std::size_t nextBound(std::size_t limit) {
		if (limit == 0) return 0;
		return static_cast<std::size_t>(next() % static_cast<unsigned int>(limit));
	}
};

std::vector<mr::editor::Offset> pieceTableHarnessLineStarts(const std::string &text) {
	std::vector<mr::editor::Offset> starts;
	starts.push_back(0);
	for (std::size_t i = 0; i < text.size(); ++i)
		if (text[i] == '\n') starts.push_back(i + 1);
	return starts;
}

std::size_t pieceTableHarnessLineIndex(const std::vector<mr::editor::Offset> &starts, mr::editor::Offset pos) {
	std::size_t line = 0;
	for (std::size_t i = 0; i < starts.size(); ++i) {
		if (starts[i] <= pos) line = i;
		else
			break;
	}
	return line;
}

std::string pieceTableHarnessFragment(PieceTableHarnessRng &rng, std::size_t maxLength) {
	static const char alphabet[] = "abc XYZ\t012345";
	const std::size_t length = rng.nextBound(maxLength + 1);
	std::string text;
	text.reserve(length);
	for (std::size_t i = 0; i < length; ++i) {
		const unsigned int choice = rng.next() % 12u;
		if (choice == 0u) text.push_back('\n');
		else
			text.push_back(alphabet[rng.nextBound(sizeof(alphabet) - 1)]);
	}
	return text;
}

PieceTableHarnessOperation pieceTableHarnessRandomOperation(PieceTableHarnessRng &rng, const std::string &model, bool allowSetText) {
	PieceTableHarnessOperation op;
	const std::size_t length = model.size();
	unsigned int selector = rng.next() % (allowSetText ? 4u : 3u);
	if (!allowSetText) ++selector;

	if (selector == 0u) {
		op.kind = PieceTableHarnessOperationKind::SetText;
		op.range = mr::editor::Range(0, length);
		op.text = pieceTableHarnessFragment(rng, 48);
		return op;
	}

	std::size_t start = rng.nextBound(length + 1);
	std::size_t end = rng.nextBound(length + 1);
	if (start > end) std::swap(start, end);

	if (selector == 1u) {
		op.kind = PieceTableHarnessOperationKind::Insert;
		op.range = mr::editor::Range(start, start);
		op.text = pieceTableHarnessFragment(rng, 16);
	} else if (selector == 2u) {
		op.kind = PieceTableHarnessOperationKind::Erase;
		op.range = mr::editor::Range(start, end);
	} else {
		op.kind = PieceTableHarnessOperationKind::Replace;
		op.range = mr::editor::Range(start, end);
		op.text = pieceTableHarnessFragment(rng, 18);
	}
	return op;
}

bool pieceTableHarnessApplyModel(std::string &model, const PieceTableHarnessOperation &op) {
	std::size_t start = std::min<std::size_t>(op.range.start, model.size());
	std::size_t end = std::min<std::size_t>(op.range.end, model.size());
	if (start > end) std::swap(start, end);

	if (op.kind == PieceTableHarnessOperationKind::SetText) {
		if (model == op.text) return false;
		model = op.text;
		return true;
	}
	if (op.kind == PieceTableHarnessOperationKind::Insert) {
		if (op.text.empty()) return false;
		model.insert(start, op.text);
		return true;
	}
	if (op.kind == PieceTableHarnessOperationKind::Erase) {
		if (start == end) return false;
		model.erase(start, end - start);
		return true;
	}
	if (start == end && op.text.empty()) return false;
	model.replace(start, end - start, op.text);
	return true;
}

void pieceTableHarnessAppendOperation(mr::editor::EditTransaction &tx, const PieceTableHarnessOperation &op) {
	if (op.kind == PieceTableHarnessOperationKind::SetText) tx.setText(op.text);
	else if (op.kind == PieceTableHarnessOperationKind::Insert)
		tx.insert(op.range.start, op.text);
	else if (op.kind == PieceTableHarnessOperationKind::Erase)
		tx.erase(op.range);
	else
		tx.replace(op.range, op.text);
}

void pieceTableHarnessAppendOperation(mr::editor::StagedEditTransaction &tx, const PieceTableHarnessOperation &op) {
	if (op.kind == PieceTableHarnessOperationKind::SetText) tx.setText(op.text);
	else if (op.kind == PieceTableHarnessOperationKind::Insert)
		tx.insert(op.range.start, op.text);
	else if (op.kind == PieceTableHarnessOperationKind::Erase)
		tx.erase(op.range);
	else
		tx.replace(op.range, op.text);
}

bool pieceTableHarnessCheckDocument(const mr::editor::TextDocument &document, const std::string &model, const char *phase, std::string &failureReason) {
	if (document.length() != model.size()) {
		failureReason = std::string(phase) + ": document length mismatch.";
		return false;
	}
	if (document.text() != model) {
		failureReason = std::string(phase) + ": materialized text mismatch.";
		return false;
	}

	std::string pieceText;
	for (std::size_t i = 0; i < document.pieceCount(); ++i) {
		mr::editor::PieceChunkView chunk = document.pieceChunk(i);
		if (chunk.length != 0) pieceText.append(chunk.data, chunk.length);
	}
	if (pieceText != model) {
		failureReason = std::string(phase) + ": piece chunk concatenation mismatch.";
		return false;
	}

	const std::vector<mr::editor::Offset> starts = pieceTableHarnessLineStarts(model);
	if (document.lineCount() != starts.size()) {
		failureReason = std::string(phase) + ": line count mismatch.";
		return false;
	}
	for (std::size_t i = 0; i < starts.size(); ++i) {
		if (document.lineStartByIndex(i) != starts[i]) {
			failureReason = std::string(phase) + ": lineStartByIndex mismatch.";
			return false;
		}
	}
	for (std::size_t pos = 0; pos <= model.size(); ++pos) {
		const std::size_t expectedLine = pieceTableHarnessLineIndex(starts, pos);
		const mr::editor::Offset expectedStart = starts[expectedLine];
		if (document.lineIndex(pos) != expectedLine) {
			failureReason = std::string(phase) + ": lineIndex mismatch at offset " + std::to_string(pos) + ".";
			return false;
		}
		if (document.lineStart(pos) != expectedStart) {
			failureReason = std::string(phase) + ": lineStart mismatch at offset " + std::to_string(pos) + ".";
			return false;
		}
		if (document.column(pos) != pos - expectedStart) {
			failureReason = std::string(phase) + ": column mismatch at offset " + std::to_string(pos) + ".";
			return false;
		}
	}

	failureReason.clear();
	return true;
}

bool pieceTableHarnessApplySingle(mr::editor::TextDocument &document, std::string &model, const PieceTableHarnessOperation &op, bool staged, const char *phase, std::string &failureReason) {
	std::string expected = model;
	const bool expectedChanged = pieceTableHarnessApplyModel(expected, op);
	const mr::editor::Offset oldLength = document.length();
	const std::size_t oldVersion = document.version();
	mr::editor::CommitResult result;

	if (staged) {
		mr::editor::StagedEditTransaction tx(document.readSnapshot(), phase);
		pieceTableHarnessAppendOperation(tx, op);
		result = document.tryApply(tx);
	} else {
		mr::editor::EditTransaction tx(phase);
		pieceTableHarnessAppendOperation(tx, op);
		result = document.tryApply(tx, document.version());
	}

	if (expectedChanged) {
		if (!result.applied() || !result.changed()) {
			failureReason = std::string(phase) + ": expected applied change.";
			return false;
		}
		if (result.change.oldLength != oldLength || result.change.newLength != expected.size()) {
			failureReason = std::string(phase) + ": change length metadata mismatch.";
			return false;
		}
		if (result.change.oldVersion != oldVersion || result.change.newVersion != document.version()) {
			failureReason = std::string(phase) + ": change version metadata mismatch.";
			return false;
		}
	} else {
		if (result.status != mr::editor::CommitStatus::NoOp || result.changed()) {
			failureReason = std::string(phase) + ": expected no-op result.";
			return false;
		}
	}

	model = expected;
	return pieceTableHarnessCheckDocument(document, model, phase, failureReason);
}

bool pieceTableHarnessApplyBatch(mr::editor::TextDocument &document, std::string &model, PieceTableHarnessRng &rng, bool staged, const char *phase, std::string &failureReason) {
	std::string expected = model;
	bool expectedChanged = false;
	const std::size_t operationCount = 2 + rng.nextBound(4);
	mr::editor::CommitResult result;

	if (staged) {
		mr::editor::StagedEditTransaction tx(document.readSnapshot(), phase);
		for (std::size_t i = 0; i < operationCount; ++i) {
			PieceTableHarnessOperation op = pieceTableHarnessRandomOperation(rng, expected, false);
			pieceTableHarnessAppendOperation(tx, op);
			expectedChanged = pieceTableHarnessApplyModel(expected, op) || expectedChanged;
		}
		result = document.tryApply(tx);
	} else {
		mr::editor::EditTransaction tx(phase);
		for (std::size_t i = 0; i < operationCount; ++i) {
			PieceTableHarnessOperation op = pieceTableHarnessRandomOperation(rng, expected, false);
			pieceTableHarnessAppendOperation(tx, op);
			expectedChanged = pieceTableHarnessApplyModel(expected, op) || expectedChanged;
		}
		result = document.tryApply(tx, document.version());
	}

	if (expectedChanged) {
		if (!result.applied() || !result.changed()) {
			failureReason = std::string(phase) + ": expected applied batch change.";
			return false;
		}
	} else if (result.status != mr::editor::CommitStatus::NoOp) {
		failureReason = std::string(phase) + ": expected no-op batch result.";
		return false;
	}

	model = expected;
	return pieceTableHarnessCheckDocument(document, model, phase, failureReason);
}

bool sendWindowKey(MREditWindow &window, ushort keyCode, ushort modifiers = 0) {
	TEvent event{};
	event.what = evKeyDown;
	event.keyDown.keyCode = keyCode;
	event.keyDown.controlKeyState = modifiers;
	window.handleEvent(event);
	return true;
}

bool sendWindowCommand(MREditWindow &window, ushort command) {
	TEvent event{};
	event.what = evCommand;
	event.message.command = command;
	window.handleEvent(event);
	return true;
}

bool expectWindowBlock(const MREditWindow &window, int status, bool marking, int line1, int line2, int col1, int col2, const char *phase, std::string &failureReason) {
	if (window.blockStatus() != status || window.isBlockMarking() != marking || window.blockLine1() != line1 || window.blockLine2() != line2 || window.blockCol1() != col1 || window.blockCol2() != col2) {
		failureReason = std::string("Window block path mismatch in ") + phase + ": got status=" + std::to_string(window.blockStatus()) + " marking=" + std::to_string(window.isBlockMarking() ? 1 : 0) +
		                " line1=" + std::to_string(window.blockLine1()) + " line2=" + std::to_string(window.blockLine2()) + " col1=" + std::to_string(window.blockCol1()) + " col2=" +
		                std::to_string(window.blockCol2()) + ".";
		return false;
	}
	return true;
}

bool expectWindowBlockOverlay(const MREditWindow &window, int status, const char *phase, std::string &failureReason) {
	const MRFileEditor *editor = window.getEditor();
	if (editor == nullptr) {
		failureReason = std::string("Window block overlay check has no editor in ") + phase + ".";
		return false;
	}
	const MRFileEditor::BlockOverlayState overlay = editor->blockOverlayState();
	if (!overlay.active || overlay.mode != status) {
		failureReason = std::string("Window block overlay mismatch in ") + phase + ": active=" + std::to_string(overlay.active ? 1 : 0) + " mode=" + std::to_string(overlay.mode) + ".";
		return false;
	}
	return true;
}

bool expectWindowCommittedBlockRefreshesOverlay(MREditWindow &window, ushort keyCode, ushort modifiers, int status, const char *phase, std::string &failureReason) {
	MRFileEditor *editor = window.getEditor();
	if (editor == nullptr) {
		failureReason = std::string("Window committed block refresh check has no editor in ") + phase + ".";
		return false;
	}
	editor->setBlockOverlayState(0, 0, 0, false);
	if (!sendWindowKey(window, keyCode, modifiers)) return false;
	return expectWindowBlockOverlay(window, status, phase, failureReason);
}

ushort rawCtrlKey(char upperLetter) {
	return static_cast<ushort>(upperLetter - 'A' + 1);
}

bool sendWindowRawCtrl(MREditWindow &window, char upperLetter) {
	return sendWindowKey(window, rawCtrlKey(upperLetter));
}

bool diagnosticsContainError(const std::vector<MRKeymapDiagnostic> &diagnostics) {
	for (const MRKeymapDiagnostic &diagnostic : diagnostics)
		if (diagnostic.severity == MRKeymapDiagnosticSeverity::Error) return true;
	return false;
}

class ScopedRegressionKeymap {
  public:
	ScopedRegressionKeymap() : mProfiles(configuredKeymapProfiles()), mActive(configuredActiveKeymapProfile()) {
	}

	~ScopedRegressionKeymap() {
		static_cast<void>(setConfiguredKeymapProfiles(mProfiles, nullptr));
		static_cast<void>(setConfiguredActiveKeymapProfile(mActive, nullptr));
	}

  private:
	std::vector<MRKeymapProfile> mProfiles;
	std::string mActive;
};

class ScopedRegressionMacroDirectory {
  public:
	explicit ScopedRegressionMacroDirectory(const std::string &path) : mPrevious(defaultMacroDirectoryPath()) {
		static_cast<void>(setConfiguredMacroDirectoryPath(path, nullptr));
	}

	~ScopedRegressionMacroDirectory() {
		static_cast<void>(setConfiguredMacroDirectoryPath(mPrevious, nullptr));
	}

  private:
	std::string mPrevious;
};

class ScopedRegressionCursorBehaviour {
  public:
	explicit ScopedRegressionCursorBehaviour(MRCursorBehaviour behaviour) : mPrevious(configuredCursorBehaviour()) {
		static_cast<void>(setConfiguredCursorBehaviour(behaviour));
	}

	~ScopedRegressionCursorBehaviour() {
		static_cast<void>(setConfiguredCursorBehaviour(mPrevious));
	}

  private:
	MRCursorBehaviour mPrevious;
};

class ScopedRegressionPersistentBlocks {
  public:
	explicit ScopedRegressionPersistentBlocks(bool persistentBlocks) : mPrevious(configuredEditSetupSettings()) {
		MREditSetupSettings settings = mPrevious;
		settings.persistentBlocks = persistentBlocks;
		static_cast<void>(setConfiguredEditSetupSettings(settings, nullptr));
	}

	~ScopedRegressionPersistentBlocks() {
		static_cast<void>(setConfiguredEditSetupSettings(mPrevious, nullptr));
	}

  private:
	MREditSetupSettings mPrevious;
};

class ScopedRegressionEditSetupSettings {
  public:
	explicit ScopedRegressionEditSetupSettings(const MREditSetupSettings &settings) : mPrevious(configuredEditSetupSettings()) {
		static_cast<void>(setConfiguredEditSetupSettings(settings, nullptr));
	}

	~ScopedRegressionEditSetupSettings() {
		static_cast<void>(setConfiguredEditSetupSettings(mPrevious, nullptr));
	}

  private:
	MREditSetupSettings mPrevious;
};

bool installRegressionKeymap(std::string_view source, std::string &failureReason) {
	MRKeymapLoadResult loaded = loadKeymapProfilesFromSettingsSource(source);
	std::string errorMessage;

	if (diagnosticsContainError(loaded.diagnostics)) {
		failureReason = "Regression keymap source must load without error diagnostics.";
		for (const MRKeymapDiagnostic &diagnostic : loaded.diagnostics)
			if (diagnostic.severity == MRKeymapDiagnosticSeverity::Error) {
				failureReason += " ";
				failureReason += diagnostic.message;
				break;
			}
		return false;
	}
	if (!setConfiguredKeymapProfiles(loaded.profiles, &errorMessage)) {
		failureReason = "Unable to install regression keymap profiles: " + errorMessage;
		return false;
	}
	if (!setConfiguredActiveKeymapProfile(loaded.activeProfileName, &errorMessage)) {
		failureReason = "Unable to activate regression keymap profile: " + errorMessage;
		return false;
	}
	return true;
}

MREditorApp *ensureRegressionEditorApp(std::string &failureReason) {
	static MREditorApp *app = nullptr;

	if (app == nullptr) app = new MREditorApp();
	if (app == nullptr) {
		failureReason = "Regression app allocation failed.";
		return nullptr;
	}
	return app;
}

void destroyRegressionWindow(MREditWindow *window) {
	if (window == nullptr) return;
	if (TProgram::deskTop != nullptr) TProgram::deskTop->setCurrent(nullptr, TView::leaveSelect);
	TObject::destroy(window);
	if (TProgram::deskTop != nullptr) TProgram::deskTop->setCurrent(nullptr, TView::leaveSelect);
}

bool testMmpClientFocusDispatchHarness(std::string &failureReason) {
	const std::string suffix = std::to_string(static_cast<long>(::getpid()));
	const std::string firstId = "REGRESSIONMMPCANVASFOCUSA" + suffix;
	const std::string secondId = "REGRESSIONMMPCANVASFOCUSB" + suffix;
	MRMacroModelessWindowDefinition firstDefinition;
	MRMacroModelessWindowDefinition secondDefinition;
	MRMacroModelessCanvasSpec canvas;
	MRMacroModelessCanvasHotspotSpec hotspot;
	MRMacroModelessDisplaySpec statusDisplay;
	MRMacroModelessTextFieldSpec textField;
	MRMacroModelessBoolFieldSpec boolField;
	MRMacroModelessIntFieldSpec intField;
	MRMacroModelessProgressFieldSpec progressField;
	MRMacroModelessProgressFieldSpec tickProgressField;
	MRMacroModelessLogFieldSpec logField;
	MRMacroModelessSelectFieldSpec selectField;
	MRRuntimeScheduledConsumerConfig timerConfig;
	MRRuntimeScheduledConsumerConfig focusTimerConfig;
	MRRuntimeScheduledConsumerConfig debugTimerConfig;
	MREditWindow *timerEditor = nullptr;
	TWindow *firstWindow = nullptr;
	TWindow *secondWindow = nullptr;
	TView *canvasView = nullptr;
	TView *textInput = nullptr;
	TView *boolInput = nullptr;
	TView *intInput = nullptr;
	TView *progressView = nullptr;
	TView *logView = nullptr;
	TView *selectInput = nullptr;
	MRRuntimeScheduledConsumerId timerConsumerId = 0;
	MRRuntimeScheduledConsumerId focusTimerConsumerId = 0;
	MRRuntimeScheduledConsumerId debugTimerConsumerId = 0;

	if (ensureRegressionEditorApp(failureReason) == nullptr || TProgram::deskTop == nullptr) return false;
	firstDefinition.x = 2;
	firstDefinition.y = 2;
	firstDefinition.width = 30;
	firstDefinition.height = 24;
	firstDefinition.windowId = firstId;
	firstDefinition.title = "MMP CANVAS FOCUS A";
	canvas.x = 2;
	canvas.y = 2;
	canvas.width = 20;
	canvas.height = 4;
	canvas.canvasId = "MAIN";
	firstDefinition.canvases.push_back(canvas);
	hotspot.canvasId = "MAIN";
	hotspot.x = 10;
	hotspot.y = 1;
	hotspot.width = 8;
	hotspot.height = 1;
	hotspot.id = 7901;
	hotspot.macroSpec = "RegressionMmp^Hotspot";
	firstDefinition.canvasHotspots.push_back(hotspot);
	statusDisplay.x = 2;
	statusDisplay.y = 6;
	statusDisplay.width = 24;
	statusDisplay.text = "Ready";
	firstDefinition.displays.push_back(statusDisplay);
	firstDefinition.statusDisplayIndices["ACTIVITY"] = 1;
	textField.x = 2;
	textField.y = 7;
	textField.width = 12;
	textField.fieldId = "NAME";
	textField.label = "Name";
	textField.text = "Initial";
	firstDefinition.textFields.push_back(textField);
	boolField.x = 2;
	boolField.y = 9;
	boolField.fieldId = "ENABLED";
	boolField.caption = "Enabled";
	boolField.value = true;
	firstDefinition.boolFields.push_back(boolField);
	intField.x = 2;
	intField.y = 11;
	intField.width = 8;
	intField.fieldId = "RETRIES";
	intField.label = "Retries";
	intField.minimum = 0;
	intField.maximum = 9;
	intField.value = 3;
	firstDefinition.intFields.push_back(intField);
	progressField.x = 2;
	progressField.y = 17;
	progressField.width = 10;
	progressField.fieldId = "SCAN";
	progressField.label = "Scan";
	progressField.total = 100;
	progressField.value = 25;
	firstDefinition.progressFields.push_back(progressField);
	tickProgressField.x = 15;
	tickProgressField.y = 17;
	tickProgressField.width = 8;
	tickProgressField.fieldId = "TICKS";
	tickProgressField.label = "Ticks";
	tickProgressField.total = 10;
	tickProgressField.value = 0;
	firstDefinition.progressFields.push_back(tickProgressField);
	logField.x = 2;
	logField.y = 19;
	logField.width = 18;
	logField.height = 3;
	logField.logId = "EVENTS";
	logField.label = "Events";
	logField.capacity = 4;
	firstDefinition.logFields.push_back(logField);
	selectField.x = 2;
	selectField.y = 13;
	selectField.width = 18;
	selectField.height = 3;
	selectField.fieldId = "MODE";
	selectField.label = "Mode";
	selectField.value = "Normal";
	selectField.options.push_back("Normal");
	selectField.options.push_back("Safe");
	selectField.options.push_back("Fast");
	firstDefinition.selectFields.push_back(selectField);
	secondDefinition = firstDefinition;
	secondDefinition.x = 40;
	secondDefinition.windowId = secondId;
	secondDefinition.title = "MMP CANVAS FOCUS B";
	if (!showMacroModelessWindow(firstDefinition) || !showMacroModelessWindow(secondDefinition)) {
		failureReason = "MMP canvas focus harness could not create both modeless windows.";
		return false;
	}
	mrvmStoreModelessWindowDefinition(firstDefinition);
	mrvmStoreModelessWindowDefinition(secondDefinition);
	for (MRDesktopWindow *desktopWindow : allDesktopWindowsInZOrder()) {
		TWindow *nativeWindow = desktopWindow != nullptr ? desktopWindow->desktopNativeWindow() : nullptr;
		const char *title = nativeWindow != nullptr ? nativeWindow->getTitle(0) : nullptr;

		if (title == nullptr) continue;
		if (firstDefinition.title == title) firstWindow = nativeWindow;
		if (secondDefinition.title == title) secondWindow = nativeWindow;
	}
	if (firstWindow != nullptr) {
		TGroup *group = static_cast<TGroup *>(firstWindow);
		TView *firstChild = group->first();

		for (TView *view = firstChild; view != nullptr; view = view->next) {
			if (view->getBounds() == TRect(canvas.x, canvas.y, canvas.x + canvas.width, canvas.y + canvas.height)) canvasView = view;
			if (view->getBounds() == TRect(textField.x + 6, textField.y, textField.x + 6 + textField.width, textField.y + 1)) textInput = view;
			if (view->getBounds() == TRect(boolField.x, boolField.y, boolField.x + strwidth(boolField.caption.c_str()) + 5, boolField.y + 1)) boolInput = view;
			if (view->getBounds() == TRect(intField.x + 9, intField.y, intField.x + 9 + intField.width, intField.y + 1)) intInput = view;
			if (view->getBounds() == TRect(progressField.x + 6, progressField.y, progressField.x + 6 + progressField.width, progressField.y + 1)) progressView = view;
			if (view->getBounds() == TRect(logField.x, logField.y + 1, logField.x + logField.width, logField.y + 1 + logField.height)) logView = view;
			if (view->getBounds() == TRect(selectField.x, selectField.y + 1, selectField.x + selectField.width - 1, selectField.y + 1 + selectField.height)) selectInput = view;
			if (view->next == firstChild) break;
		}
	}
	if (firstWindow == nullptr || secondWindow == nullptr || canvasView == nullptr || textInput == nullptr || boolInput == nullptr || intInput == nullptr || progressView == nullptr || logView == nullptr || selectInput == nullptr) {
		if (firstWindow != nullptr) message(firstWindow, evCommand, cmClose, nullptr);
		if (secondWindow != nullptr) message(secondWindow, evCommand, cmClose, nullptr);
		failureReason = "MMP canvas focus harness could not locate the modeless canvas view.";
		return false;
	}
	std::string textValue;
	bool boolFieldValue = false;
	std::string selectFieldValue;
	TInputLine *inputLine = dynamic_cast<TInputLine *>(textInput);
	const bool textFieldUpdated = inputLine != nullptr && updateMacroModelessTextField(firstId, "NAME", "Updated") && mrvmReadModelessWindowTextFieldValue(firstId, "NAME", textValue) && textValue == "Updated" && std::strcmp(inputLine->data, "Updated") == 0;
	TEvent textInputEvent{};
	bool typedTextStored = false;

	if (textFieldUpdated) {
		static_cast<TGroup *>(firstWindow)->setCurrent(inputLine, TView::normalSelect);
		inputLine->selectAll(True);
		textInputEvent.what = evKeyDown;
		textInputEvent.keyDown.keyCode = static_cast<ushort>('X');
		textInputEvent.keyDown.text[0] = 'X';
		textInputEvent.keyDown.textLength = 1;
		inputLine->handleEvent(textInputEvent);
		typedTextStored = mrvmReadModelessWindowTextFieldValue(firstId, "NAME", textValue) && textValue == "X";
	}
	if (!textFieldUpdated || !typedTextStored) {
		message(firstWindow, evCommand, cmClose, nullptr);
		message(secondWindow, evCommand, cmClose, nullptr);
		failureReason = "MMP text fields must synchronize native edits and retained values together.";
		return false;
	}
	TCheckBoxes *checkBoxes = dynamic_cast<TCheckBoxes *>(boolInput);
	ushort checked = 0;
	const bool boolFieldUpdated = checkBoxes != nullptr && updateMacroModelessBoolField(firstId, "ENABLED", false) && mrvmReadModelessWindowBoolFieldValue(firstId, "ENABLED", boolFieldValue) && !boolFieldValue;
	if (checkBoxes != nullptr) checkBoxes->getData(&checked);
	const bool nativeBoolFieldUpdated = checked == 0;
	TEvent boolInputEvent{};
	bool toggledBoolStored = false;

	if (boolFieldUpdated && nativeBoolFieldUpdated) {
		firstWindow->makeFirst();
		TProgram::deskTop->setCurrent(firstWindow, TView::normalSelect);
		static_cast<TGroup *>(firstWindow)->setCurrent(checkBoxes, TView::normalSelect);
		boolInputEvent.what = evKeyDown;
		boolInputEvent.keyDown.keyCode = static_cast<ushort>(' ');
		boolInputEvent.keyDown.text[0] = ' ';
		boolInputEvent.keyDown.textLength = 1;
		checkBoxes->handleEvent(boolInputEvent);
		checkBoxes->getData(&checked);
		toggledBoolStored = checked == 1 && mrvmReadModelessWindowBoolFieldValue(firstId, "ENABLED", boolFieldValue) && boolFieldValue;
	}
	if (!boolFieldUpdated || !nativeBoolFieldUpdated || !toggledBoolStored) {
		message(firstWindow, evCommand, cmClose, nullptr);
		message(secondWindow, evCommand, cmClose, nullptr);
		failureReason = "MMP boolean fields must synchronize native checkbox changes and retained values together.";
		return false;
	}
	TInputLine *intInputLine = dynamic_cast<TInputLine *>(intInput);
	int intFieldValue = 0;
	const bool intFieldUpdated = intInputLine != nullptr && mrvmStoreModelessWindowIntFieldValue(firstId, "RETRIES", 5) && updateMacroModelessIntField(firstId, "RETRIES", 5) && mrvmReadModelessWindowIntFieldValue(firstId, "RETRIES", intFieldValue) && intFieldValue == 5 && std::strcmp(intInputLine->data, "5") == 0;
	TEvent intInputEvent{};
	bool typedIntStored = false;
	bool invalidIntRestored = false;

	if (intFieldUpdated) {
		firstWindow->makeFirst();
		TProgram::deskTop->setCurrent(firstWindow, TView::normalSelect);
		static_cast<TGroup *>(firstWindow)->setCurrent(intInputLine, TView::normalSelect);
		intInputLine->selectAll(True);
		intInputEvent.what = evKeyDown;
		intInputEvent.keyDown.keyCode = static_cast<ushort>('7');
		intInputEvent.keyDown.text[0] = '7';
		intInputEvent.keyDown.textLength = 1;
		intInputLine->handleEvent(intInputEvent);
		typedIntStored = mrvmReadModelessWindowIntFieldValue(firstId, "RETRIES", intFieldValue) && intFieldValue == 7;
		intInputLine->selectAll(True);
		intInputEvent = TEvent{};
		intInputEvent.what = evKeyDown;
		intInputEvent.keyDown.keyCode = static_cast<ushort>('X');
		intInputEvent.keyDown.text[0] = 'X';
		intInputEvent.keyDown.textLength = 1;
		intInputLine->handleEvent(intInputEvent);
		intInputEvent = TEvent{};
		intInputEvent.what = evBroadcast;
		intInputEvent.message.command = cmReleasedFocus;
		intInputLine->handleEvent(intInputEvent);
		invalidIntRestored = mrvmReadModelessWindowIntFieldValue(firstId, "RETRIES", intFieldValue) && intFieldValue == 7 && std::strcmp(intInputLine->data, "7") == 0;
	}
	if (!intFieldUpdated || !typedIntStored || !invalidIntRestored) {
		message(firstWindow, evCommand, cmClose, nullptr);
		message(secondWindow, evCommand, cmClose, nullptr);
		failureReason = "MMP integer fields must retain valid native edits and restore an invalid focus-leaving value.";
		return false;
	}
	int progressTotal = 0;
	int progressValue = 0;
	const bool progressFieldUpdated = (progressView->options & ofSelectable) == 0 && mrvmStoreModelessWindowProgressFieldValue(firstId, "SCAN", 50) && updateMacroModelessProgressField(firstId, "SCAN") && mrvmReadModelessWindowProgressFieldValue(firstId, "SCAN", progressTotal, progressValue) && progressTotal == 100 && progressValue == 50;
	if (!progressFieldUpdated) {
		message(firstWindow, evCommand, cmClose, nullptr);
		message(secondWindow, evCommand, cmClose, nullptr);
		failureReason = "MMP progress fields must redraw only their non-selectable retained projection.";
		return false;
	}
	std::vector<std::string> logLines;
	const bool logFieldUpdated = (logView->options & ofSelectable) == 0 && mrvmAppendModelessWindowLogFieldLine(firstId, "EVENTS", "One") && mrvmAppendModelessWindowLogFieldLine(firstId, "EVENTS", "Two") && updateMacroModelessLogField(firstId, "EVENTS") && mrvmReadModelessWindowLogFieldLines(firstId, "EVENTS", logLines) && logLines.size() == 2 && logLines[0] == "One" && logLines[1] == "Two";
	if (!logFieldUpdated) {
		message(firstWindow, evCommand, cmClose, nullptr);
		message(secondWindow, evCommand, cmClose, nullptr);
		failureReason = "MMP log fields must redraw only their non-selectable retained projection.";
		return false;
	}
	TListViewer *selectList = dynamic_cast<TListViewer *>(selectInput);
	char firstSelectText[16] = {};
	char lastSelectText[16] = {};
	const bool selectItemsPresent = selectList != nullptr && (selectList->getText(firstSelectText, 0, static_cast<short>(sizeof(firstSelectText))), std::strcmp(firstSelectText, "Normal") == 0) && (selectList->getText(lastSelectText, 2, static_cast<short>(sizeof(lastSelectText))), std::strcmp(lastSelectText, "Fast") == 0);
	const bool selectFieldUpdated = selectItemsPresent && updateMacroModelessSelectField(firstId, "MODE", "Safe") && !updateMacroModelessSelectField(firstId, "MODE", "Unknown") && mrvmReadModelessWindowSelectFieldValue(firstId, "MODE", selectFieldValue) && selectFieldValue == "Safe";
	TEvent selectInputEvent{};
	bool selectedOptionStored = false;

	if (selectFieldUpdated) {
		firstWindow->makeFirst();
		TProgram::deskTop->setCurrent(firstWindow, TView::normalSelect);
		static_cast<TGroup *>(firstWindow)->setCurrent(selectList, TView::normalSelect);
		selectInputEvent.what = evKeyDown;
		selectInputEvent.keyDown.keyCode = kbDown;
		selectList->handleEvent(selectInputEvent);
		selectedOptionStored = mrvmReadModelessWindowSelectFieldValue(firstId, "MODE", selectFieldValue) && selectFieldValue == "Fast";
	}
	if (!selectItemsPresent || !selectFieldUpdated || !selectedOptionStored) {
		message(firstWindow, evCommand, cmClose, nullptr);
		message(secondWindow, evCommand, cmClose, nullptr);
		failureReason = "MMP selection fields must project their declared items and synchronize native selection changes with retained values.";
		return false;
	}
	std::string timerError;
	const std::string timerSetupSource = "$MACRO MmpTimerFocusSetup;\nSET_GLOBAL_STR('MMP_CANVAS_DEMO_WINDOW', '" + firstId + "');\nSET_GLOBAL_INT('MMP_CANVAS_DEMO_TICKS', 0);\nEND_MACRO;\n";
	const bool demoMacroReady = mrvmLoadMacroFile(absolutePathFromCwd("mrmac/macros/utils/MmpCanvasDemo.mrmac"), &timerError) && runMacroSourceText("MmpTimerFocusSetup", timerSetupSource.c_str(), &timerError, false);

	if (!demoMacroReady) {
		message(firstWindow, evCommand, cmClose, nullptr);
		message(secondWindow, evCommand, cmClose, nullptr);
		failureReason = "MMP timer focus harness could not prepare the demo callback: " + timerError;
		return false;
	}
	std::vector<std::string> directTimerLog;
	const bool directTimerCallbackCompleted = mrvmRunMacroSpec("MmpCanvasDemo^MmpCanvasDemoTick", &timerError, &directTimerLog);
	bool directTimerCallbackErrored = false;

	for (const std::string &line : directTimerLog)
		if (line.rfind("VM Error: ", 0) == 0) {
			timerError = line;
			directTimerCallbackErrored = true;
			break;
		}
	if (!directTimerCallbackCompleted || directTimerCallbackErrored) {
		message(firstWindow, evCommand, cmClose, nullptr);
		message(secondWindow, evCommand, cmClose, nullptr);
		failureReason = "MMP timer focus harness could not resolve the demo callback: " + timerError;
		return false;
	}
	timerEditor = createEditorWindow("mmp-timer-focus");
	if (timerEditor == nullptr || !mrActivateEditWindow(timerEditor)) {
		if (timerEditor != nullptr) destroyRegressionWindow(timerEditor);
		message(firstWindow, evCommand, cmClose, nullptr);
		message(secondWindow, evCommand, cmClose, nullptr);
		failureReason = "MMP timer focus harness could not activate an editor window.";
		return false;
	}
	focusTimerConfig.owner.modelessWindowId = firstId;
	focusTimerConfig.consumerKey = "EditorFocus";
	focusTimerConfig.intervalMs = 100;
	focusTimerConfig.macroSpec = "MmpCanvasDemo^MmpCanvasDemoTick";
	focusTimerConsumerId = registerRuntimeScheduledConsumer(focusTimerConfig);
	const bool timerDue = focusTimerConsumerId != 0 && pumpRuntimeScheduler(runtimeTimerSourceNowMs()) != 0;
	int tickTotal = 0;
	int tickValue = 0;
	std::vector<std::string> timerLogLines;
	const bool timerUpdatedWhileEditorFocused = timerDue && mrvmReadModelessWindowProgressFieldValue(firstId, "TICKS", tickTotal, tickValue) && tickTotal == 10 && tickValue == 2 && mrvmReadModelessWindowLogFieldLines(firstId, "EVENTS", timerLogLines) && !timerLogLines.empty() && timerLogLines.back() == "Timer tick 2";

	if (focusTimerConsumerId != 0) removeRuntimeScheduledConsumer(focusTimerConsumerId);
	destroyRegressionWindow(timerEditor);
	if (!timerUpdatedWhileEditorFocused) {
		std::string timerDiagnostic;

		for (const MRRuntimeSchedulerEvent &event : recentRuntimeSchedulerEvents()) {
			if (event.consumerId != focusTimerConsumerId) continue;
			timerDiagnostic += event.message;
			timerDiagnostic += " ";
		}
		message(firstWindow, evCommand, cmClose, nullptr);
		message(secondWindow, evCommand, cmClose, nullptr);
		failureReason = "An MMP timer must update its retained model while an editor window has focus: due=" + std::to_string(timerDue ? 1 : 0) + " ticks=" + std::to_string(tickValue) + " log-count=" + std::to_string(timerLogLines.size()) + " scheduler=" + timerDiagnostic;
		return false;
	}
	{
		const std::string demoMacroPath = absolutePathFromCwd("mrmac/macros/utils/MmpCanvasDemo.mrmac");
		const std::string debugMacroName = "MmpCanvasDemoTick";
		MRBentoBox *debuggerBento = nullptr;
		MRMacroExecutionSession seedSession;
		MRMacroDebugRunResult seedResult;
		MRMacroExecutionSessionId debugSessionId = 0;
		MREditWindow *debuggerOutput = nullptr;
		MREditWindow *debuggerVariables = nullptr;
		MREditWindow *debuggerWatches = nullptr;
		std::string debugError;
		bool debugTimerDue = false;
		bool debuggerAttached = false;
		bool schedulerSkippedPausedCallback = false;
		bool callbackContinued = false;
		bool callbackCompleted = false;
		int debugTickTotal = 0;
		int debugTickValue = 0;

		debuggerBento = new MRBentoBox(TRect(0, 0, 100, 30), "mmp-timer-debugger", 1703, bbmToolWorkspace);
		if (debuggerBento != nullptr) {
			TProgram::deskTop->insert(debuggerBento);
			TProgram::deskTop->setCurrent(debuggerBento, TView::enterSelect);
		}
		if (debuggerBento != nullptr && debuggerBento->getEditor() != nullptr && debuggerBento->getEditor()->loadMappedFile(demoMacroPath.c_str(), debugError)) {
			debuggerBento->setMacroDebuggerTarget(debugMacroName, debugMacroName);
			seedResult = mrvmStartDebugMacroByName(debugMacroName, MRMacroExecutionOwner(), &seedSession, &debugError, true);
			if (seedSession.sessionId != 0 && seedResult.paused && mrvmCloseDebugSession(seedSession.sessionId) && mrvmWriteDebugLineBreakpoint(debugMacroName, 71, true, &debugError) && debuggerBento->ensureMacroDebuggerPanes(debuggerOutput, debuggerVariables, debuggerWatches)) {
				debugTimerConfig.owner.modelessWindowId = firstId;
				debugTimerConfig.consumerKey = "DebuggerBreakpoint";
				debugTimerConfig.intervalMs = 100;
				debugTimerConfig.macroSpec = "MmpCanvasDemo^MmpCanvasDemoTick";
				debugTimerConsumerId = registerRuntimeScheduledConsumer(debugTimerConfig);
				debugTimerDue = debugTimerConsumerId != 0 && pumpRuntimeScheduler(runtimeTimerSourceNowMs()) != 0;
				{
					const std::chrono::steady_clock::time_point workerDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

					while (debugTimerDue && debuggerBento->macroDebuggerSessionRunning() && std::chrono::steady_clock::now() < workerDeadline) {
						mr::coprocessor::globalCoprocessor().pumpFor(std::chrono::milliseconds(1));
						debuggerBento->pumpMacroDebuggerSession();
						std::this_thread::sleep_for(std::chrono::milliseconds(2));
					}
				}
				for (const MRRuntimeScheduledConsumer &consumer : runtimeScheduledConsumers())
					if (consumer.consumerId == debugTimerConsumerId) debugSessionId = consumer.activeSessionId;
				debuggerAttached = debugTimerDue && debugSessionId != 0 && debuggerBento->macroDebuggerHasLiveSession() && !debuggerBento->macroDebuggerSessionRunning() && mrvmReadModelessWindowProgressFieldValue(firstId, "TICKS", debugTickTotal, debugTickValue) && debugTickTotal == 10 && debugTickValue == 2;
				schedulerSkippedPausedCallback = debuggerAttached && pumpRuntimeScheduler(runtimeTimerSourceNowMs() + debugTimerConfig.intervalMs) != 0 && mrvmReadModelessWindowProgressFieldValue(firstId, "TICKS", debugTickTotal, debugTickValue) && debugTickValue == 2;
				if (schedulerSkippedPausedCallback) {
					TEvent continueEvent{};

					continueEvent.what = evKeyDown;
					continueEvent.keyDown.keyCode = kbF5;
					callbackContinued = debuggerBento->handleMacroDebuggerFunctionKey(continueEvent) && continueEvent.what == evNothing;
					const std::chrono::steady_clock::time_point workerDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
					while (callbackContinued && debuggerBento->macroDebuggerHasLiveSession() && std::chrono::steady_clock::now() < workerDeadline) {
						mr::coprocessor::globalCoprocessor().pumpFor(std::chrono::milliseconds(1));
						debuggerBento->pumpMacroDebuggerSession();
						std::this_thread::sleep_for(std::chrono::milliseconds(2));
					}
					callbackCompleted = callbackContinued && !debuggerBento->macroDebuggerHasLiveSession() && mrvmReadModelessWindowProgressFieldValue(firstId, "TICKS", debugTickTotal, debugTickValue) && debugTickTotal == 10 && debugTickValue == 3;
				}
			}
		}
		if (debugSessionId != 0) static_cast<void>(mrvmCloseDebugSession(debugSessionId));
		if (debugTimerConsumerId != 0) removeRuntimeScheduledConsumer(debugTimerConsumerId);
		static_cast<void>(mrvmEraseDebugLineBreakpointsForMacroFile(debugMacroName, nullptr));
		if (debuggerBento != nullptr) destroyRegressionWindow(debuggerBento);
		debuggerBento = nullptr;
		if (!debuggerAttached || !schedulerSkippedPausedCallback || !callbackCompleted) {
			message(firstWindow, evCommand, cmClose, nullptr);
			message(secondWindow, evCommand, cmClose, nullptr);
			failureReason = "An MMP timer breakpoint must pause in its observing debugger and resume through F5: due=" + std::to_string(debugTimerDue ? 1 : 0) + " attached=" + std::to_string(debuggerAttached ? 1 : 0) + " skipped=" + std::to_string(schedulerSkippedPausedCallback ? 1 : 0) + " continued=" + std::to_string(callbackContinued ? 1 : 0) + " completed=" + std::to_string(callbackCompleted ? 1 : 0) + " error=" + debugError;
			return false;
		}
	}
	timerConfig.owner.modelessWindowId = firstId;
	timerConfig.consumerKey = "CloseCleanup";
	timerConfig.intervalMs = 100;
	timerConfig.macroSpec = "RegressionMmp^CloseCleanup";
	timerConsumerId = registerRuntimeScheduledConsumer(timerConfig);
	if (timerConsumerId == 0) {
		message(firstWindow, evCommand, cmClose, nullptr);
		message(secondWindow, evCommand, cmClose, nullptr);
		failureReason = "MMP close harness could not register a window-owned timer consumer.";
		return false;
	}
	secondWindow->makeFirst();
	TProgram::deskTop->setCurrent(secondWindow, TView::normalSelect);
	TEvent event{};

	event.what = evMouseDown;
	event.mouse.where = firstWindow->makeGlobal(TPoint(firstDefinition.width - 3, firstDefinition.height - 3));
	event.mouse.buttons = mbLeftButton;
	firstWindow->handleEvent(event);
	const bool focused = TProgram::deskTop->current == firstWindow && (firstWindow->state & sfSelected) != 0 && (firstWindow->state & sfFocused) != 0 && firstWindow->frame != nullptr;
	TEvent hotspotEvent{};

	setMacroModelessCommandRunner(nullptr);
	secondWindow->makeFirst();
	TProgram::deskTop->setCurrent(secondWindow, TView::normalSelect);
	hotspotEvent.what = evMouseDown;
	hotspotEvent.mouse.where = firstWindow->makeGlobal(TPoint(canvas.x + hotspot.x + 1, canvas.y + hotspot.y));
	hotspotEvent.mouse.buttons = mbLeftButton;
	firstWindow->handleEvent(hotspotEvent);
	const bool hotspotDispatched = TProgram::deskTop->current == firstWindow && hotspotEvent.what == evNothing;
	message(firstWindow, evCommand, cmClose, nullptr);
	message(secondWindow, evCommand, cmClose, nullptr);
	bool timerRemoved = true;
	for (const MRRuntimeScheduledConsumer &consumer : runtimeScheduledConsumers())
		if (consumer.consumerId == timerConsumerId) timerRemoved = false;
	if (!focused) {
		failureReason = "A client-area click outside the canvas must make its MMP window the focused desktop window immediately.";
		return false;
	}
	if (!hotspotDispatched) {
		failureReason = "A canvas hotspot must activate its MMP window and consume the click before TVision frame routing.";
		return false;
	}
	if (!timerRemoved) {
		removeRuntimeScheduledConsumer(timerConsumerId);
		failureReason = "Closing an MMP window must remove its scheduler consumers.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testMmpCollectionControlHarness(std::string &failureReason) {
	TScrollBar *scrollBar = new TScrollBar(TRect(23, 0, 24, 4));
	TScrollBar *treeScrollBar = new TScrollBar(TRect(23, 0, 24, 4));
	TScrollBar *tableScrollBar = new TScrollBar(TRect(23, 0, 24, 4));
	TView *gridView = createMacroUiGridView(TRect(0, 0, 23, 4), scrollBar, {"Refresh\tREFRESH\tUpdate the activity field", "Close\tCLOSE\tClose the panel"}, 0);
	TView *treeView = createMacroUiTreeView(TRect(0, 0, 23, 4), treeScrollBar, {"TREE\troot\t\tRoot\t1", "TREE\tapp\troot\tApplication\t0", "TREE\tcommands\tapp\tCommands\t0"}, 0);
	TView *tableView = createMacroUiTableView(TRect(0, 0, 23, 4), tableScrollBar, {"TABLE_COLUMN\tName\t12", "TABLE_COLUMN\tState\t8", "TABLE_ROW\tone\tOne\tready", "TABLE_ROW\ttwo\tTwo\tready", "TABLE_ROW\tthree\tThree\tqueued"}, 0);
	TEvent event{};
	std::string sourceText;
	std::string conventionalSource;
	std::string modelessSource;
	std::string ioError;
	std::vector<unsigned char> bytecode;
	std::string compileError;
	MRVMRuntimeKv collectionRuntimeKv;
	MRMacroModelessWindowDefinition collectionDefinition;
	std::string selectedNodeId;
	std::string selectedRowId;
	bool nodeExpanded = false;
	std::vector<std::string> globalOrder;
	std::map<std::string, int> globalInts;
	std::map<std::string, std::string> globalStrings;
	std::vector<std::string> macroLog;
	std::string demoWindowId;
	std::string demoError;
	TWindow *demoWindow = nullptr;
	TView *demoTreeView = nullptr;
	TView *demoTableView = nullptr;
	const bool initialSelection = gridView != nullptr && macroUiGridSelectedIndex(gridView) == 1 && macroUiGridSelectedText(gridView) == "REFRESH";

	if (initialSelection) {
		event.what = evKeyDown;
		event.keyDown.keyCode = kbRight;
		gridView->handleEvent(event);
	}
	const bool nextSelection = initialSelection && event.what == evNothing && macroUiGridSelectedIndex(gridView) == 2 && macroUiGridSelectedText(gridView) == "CLOSE";
	const bool initialTreeSelection = treeView != nullptr && macroUiTreeSelectedIndex(treeView) == 1 && macroUiTreeSelectedText(treeView) == "root";

	if (initialTreeSelection) {
		event = TEvent{};
		event.what = evKeyDown;
		event.keyDown.keyCode = kbDown;
		treeView->handleEvent(event);
	}
	const bool treeChildSelected = initialTreeSelection && event.what == evNothing && macroUiTreeSelectedIndex(treeView) == 2 && macroUiTreeSelectedText(treeView) == "app";
	if (treeChildSelected) {
		event = TEvent{};
		event.what = evKeyDown;
		event.keyDown.keyCode = kbRight;
		treeView->handleEvent(event);
		event = TEvent{};
		event.what = evKeyDown;
		event.keyDown.keyCode = kbDown;
		treeView->handleEvent(event);
	}
	const bool treeExpandedAndSelected = treeChildSelected && event.what == evNothing && macroUiTreeSelectedIndex(treeView) == 3 && macroUiTreeSelectedText(treeView) == "commands";
	const bool initialTableSelection = tableView != nullptr && macroUiTableSelectedIndex(tableView) == 1 && macroUiTableSelectedText(tableView) == "one";

	if (initialTableSelection) {
		event = TEvent{};
		event.what = evKeyDown;
		event.keyDown.keyCode = kbDown;
		tableView->handleEvent(event);
	}
	const bool nextTableSelection = initialTableSelection && event.what == evNothing && macroUiTableSelectedIndex(tableView) == 2 && macroUiTableSelectedText(tableView) == "two";
	collectionDefinition.windowId = "MMP-COLLECTION-STATE";
	collectionDefinition.width = 30;
	collectionDefinition.height = 10;
	mrvmModelessUiStoreWindowDefinition(collectionRuntimeKv, collectionDefinition);
	const bool collectionStateRetained = mrvmModelessUiStoreWindowTreeSelection(collectionRuntimeKv, collectionDefinition.windowId, 71, "commands") && mrvmModelessUiStoreWindowTreeExpansion(collectionRuntimeKv, collectionDefinition.windowId, 71, "app", true) && mrvmModelessUiStoreWindowTableSelection(collectionRuntimeKv, collectionDefinition.windowId, 72, "two");
	mrvmModelessUiStoreWindowDefinition(collectionRuntimeKv, collectionDefinition);
	const bool collectionStateReadable = collectionStateRetained && mrvmModelessUiReadWindowTreeSelection(collectionRuntimeKv, collectionDefinition.windowId, 71, selectedNodeId) && selectedNodeId == "commands" && mrvmModelessUiReadWindowTreeExpansion(collectionRuntimeKv, collectionDefinition.windowId, 71, "app", nodeExpanded) && nodeExpanded && mrvmModelessUiReadWindowTableSelection(collectionRuntimeKv, collectionDefinition.windowId, 72, selectedRowId) && selectedRowId == "two";
	const bool invalidCollectionsRejected = !macroUiTreeItemsValid({"TREE\tchild\tmissing\tChild\t0"}) && !macroUiTableItemsValid({"TABLE_COLUMN\tName\t8", "TABLE_ROW\tone\tOne\tready"});
	const bool demoStarted = ensureRegressionEditorApp(failureReason) != nullptr && mrvmLoadMacroFile(absolutePathFromCwd("mrmac/macros/utils/MmpTreeTableDemo.mrmac"), &demoError) && mrvmRunMacroSpec("MmpTreeTableDemo^MmpTreeTableDemo", &demoError, &macroLog);

	if (demoStarted) {
		mrvmUiCopyGlobals(globalOrder, globalInts, globalStrings);
		std::map<std::string, std::string>::const_iterator windowIdIt = globalStrings.find("MMP_TREE_TABLE_WINDOW");

		if (windowIdIt != globalStrings.end()) demoWindowId = windowIdIt->second;
		for (MRDesktopWindow *desktopWindow : allDesktopWindowsInZOrder()) {
			TWindow *nativeWindow = desktopWindow != nullptr ? desktopWindow->desktopNativeWindow() : nullptr;
			const char *title = nativeWindow != nullptr ? nativeWindow->getTitle(0) : nullptr;

			if (title != nullptr && std::string(title) == "MMP TREE AND TABLE") demoWindow = nativeWindow;
		}
		if (demoWindow != nullptr) {
			TGroup *group = static_cast<TGroup *>(demoWindow);
			TView *firstChild = group->first();

			for (TView *view = firstChild; view != nullptr; view = view->next) {
				if (view->getBounds() == TRect(2, 4, 31, 12)) demoTreeView = view;
				if (view->getBounds() == TRect(36, 4, 69, 12)) demoTableView = view;
				if (view->next == firstChild) break;
			}
		}
	}
	bool liveModelessState = demoStarted && !demoWindowId.empty() && demoWindow != nullptr && demoTreeView != nullptr && demoTableView != nullptr;

	if (liveModelessState) {
		event = TEvent{};
		event.what = evKeyDown;
		event.keyDown.keyCode = kbDown;
		demoTreeView->handleEvent(event);
		event = TEvent{};
		event.what = evKeyDown;
		event.keyDown.keyCode = kbLeft;
		demoTreeView->handleEvent(event);
		event = TEvent{};
		event.what = evKeyDown;
		event.keyDown.keyCode = kbRight;
		demoTreeView->handleEvent(event);
		event = TEvent{};
		event.what = evKeyDown;
		event.keyDown.keyCode = kbRight;
		demoTreeView->handleEvent(event);
		liveModelessState = event.what == evNothing && mrvmReadModelessWindowTreeSelection(demoWindowId, 71, selectedNodeId) && selectedNodeId == "commands" && mrvmReadModelessWindowTreeExpansion(demoWindowId, 71, "app", nodeExpanded) && nodeExpanded;
		event = TEvent{};
		event.what = evKeyDown;
		event.keyDown.keyCode = kbDown;
		demoTableView->handleEvent(event);
		liveModelessState = liveModelessState && event.what == evNothing && mrvmReadModelessWindowTableSelection(demoWindowId, 72, selectedRowId) && selectedRowId == "syntax";
	}
	if (demoWindow != nullptr) message(demoWindow, evCommand, cmClose, nullptr);
	if (gridView != nullptr) TObject::destroy(gridView);
	if (treeView != nullptr) TObject::destroy(treeView);
	if (tableView != nullptr) TObject::destroy(tableView);
	if (scrollBar != nullptr) TObject::destroy(scrollBar);
	if (treeScrollBar != nullptr) TObject::destroy(treeScrollBar);
	if (tableScrollBar != nullptr) TObject::destroy(tableScrollBar);
	if (!initialSelection || !nextSelection) {
		failureReason = "MMP action grids must retain full action values and keyboard selection.";
		return false;
	}
	if (!initialTreeSelection || !treeChildSelected || !treeExpandedAndSelected || !initialTableSelection || !nextTableSelection) {
		failureReason = "Shared tree and table controls must navigate stable node and row ids through their native key paths.";
		return false;
	}
	if (!invalidCollectionsRejected) {
		failureReason = "Tree and table item encodings must reject invalid parent and cell structures.";
		return false;
	}
	if (!collectionStateReadable) {
		failureReason = "Modeless tree and table selection state must remain in the window K/V model across a definition refresh.";
		return false;
	}
	if (!liveModelessState) {
		failureReason = "The tree/table demo must create shared controls whose native navigation synchronizes the live MMP K/V model: " + demoError + " started=" + std::to_string(demoStarted ? 1 : 0) + " id=" + demoWindowId + " window=" + std::to_string(demoWindow != nullptr ? 1 : 0) + " tree=" + std::to_string(demoTreeView != nullptr ? 1 : 0) + " table=" + std::to_string(demoTableView != nullptr ? 1 : 0);
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("mrmac/ui/modeless/MRMacroModelessControls.cpp"), sourceText, ioError) || sourceText.find("updateCellWidth()") == std::string::npos || sourceText.find("static constexpr int cellWidth = 4") != std::string::npos) {
		failureReason = "MMP action grids must derive their cell width from their labels.";
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("mrmac/ui/conventional/MRVMMacroDialogRuntime.cpp"), conventionalSource, ioError) || !readTextFile(absolutePathFromCwd("mrmac/ui/modeless/MRMacroModelessUi.cpp"), modelessSource, ioError) || conventionalSource.find("createMacroUiListView") == std::string::npos || conventionalSource.find("createMacroUiGridView") == std::string::npos || conventionalSource.find("createMacroUiTreeView") == std::string::npos || conventionalSource.find("createMacroUiTableView") == std::string::npos || modelessSource.find("createMacroUiListView") == std::string::npos || modelessSource.find("createMacroUiGridView") == std::string::npos || modelessSource.find("createMacroUiTreeView") == std::string::npos || modelessSource.find("createMacroUiTableView") == std::string::npos) {
		failureReason = "Conventional dialogs and MMPs must project every collection control through the shared UI control factory.";
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("mrmac/macros/utils/MmpTreeTableDemo.mrmac"), sourceText, ioError) || !compileBytecode(sourceText, bytecode, compileError)) {
		failureReason = "The MMP tree/table demo must compile: " + (ioError.empty() ? compileError : ioError);
		return false;
	}
	failureReason.clear();
	return true;
}

int runMacroDebuggerF9RouteProbeMode() {
	MREditorApp app;
	MRBentoBox *bento = nullptr;
	MREditWindow *debuggerOutput = nullptr;
	MREditWindow *variables = nullptr;
	MREditWindow *watches = nullptr;
	MRFileEditor *sourceEditor = nullptr;
	MRFileEditor *outputEditor = nullptr;
	MRFileEditor *variablesEditor = nullptr;
	MRStatusLine *mrStatusLine = nullptr;
	MRMacroExecutionSession debugSession;
	MRMacroDebugRunResult debugStartResult;
	TEvent event;
	TEvent commandEvent;
	TEvent continueEvent;
	TEvent stopEvent;
	std::string outputText;
	std::string sourceText;
	std::string logText;
	std::string logError;
	std::string registryError;
	const long probePid = static_cast<long>(::getpid());
	const std::string macroName = "MacroDebuggerF9RouteProbe" + std::to_string(probePid);
	const std::string macroPath = "/tmp/mr_macro_debugger_f9_route_probe_" + std::to_string(probePid) + ".mrmac";
	const std::string logPath = "/tmp/mr_macro_debugger_f9_route_probe.log";

	::unlink(logPath.c_str());
	sourceText = "$MACRO " + macroName + ";\n"
	             "DEF_INT(X);\n"
	             "X := 1;\n"
	             "X := X + 1;\n"
	             "MAKE_MESSAGE('unreached debugger route probe');\n"
	             "END_MACRO;\n";
	{
		std::ofstream out(macroPath.c_str(), std::ios::out | std::ios::trunc);
		if (!out) {
			std::cerr << "Unable to write macro debugger F9 route fixture.\n";
			return 1;
		}
		out << sourceText;
	}
	if (!mrvmLoadMacroFile(macroPath, &registryError)) {
		(void)::remove(macroPath.c_str());
		std::cerr << "Unable to load macro debugger F9 route fixture: " << registryError << "\n";
		return 1;
	}
	debugStartResult = mrvmStartDebugMacroByName(macroName, MRMacroExecutionOwner(), &debugSession, &registryError, true);
	if (debugSession.sessionId == 0 || debugStartResult.stopReason != mrdStopBreakpoint || !debugStartResult.paused) {
		(void)::remove(macroPath.c_str());
		std::cerr << "Unable to start macro debugger F9 route fixture as debug session: " << registryError << "\n";
		return 1;
	}
	if (TProgram::deskTop == nullptr) {
		(void)::remove(macroPath.c_str());
		std::cerr << "Regression app has no desktop.\n";
		return 1;
	}
	bento = new MRBentoBox(TRect(0, 0, 120, 32), "debug-f9-route", 1701, bbmToolWorkspace);
	if (bento == nullptr) {
		(void)::remove(macroPath.c_str());
		std::cerr << "Unable to allocate debug Bento.\n";
		return 1;
	}
	TProgram::deskTop->insert(bento);
	TProgram::deskTop->setCurrent(bento, TView::enterSelect);
	bento->setMacroDebuggerTarget(macroName, macroName);
	bento->setMacroDebuggerSession(debugSession.sessionId);
	if (!bento->replaceTextBuffer(sourceText.c_str(), "debug-f9-route.mrmac")) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Debug Bento source text could not be installed.\n";
		return 1;
	}
	if (!bento->ensureMacroDebuggerPanes(debuggerOutput, variables, watches) || debuggerOutput == nullptr) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Debug Bento panes were not created.\n";
		return 1;
	}
	bento->activatePrimaryPane();
	sourceEditor = bento->getEditor();
	if (sourceEditor == nullptr) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Debug Bento source editor is missing.\n";
		return 1;
	}
	sourceEditor->setCursorOffset(sourceEditor->nextLineOffset(sourceEditor->nextLineOffset(0)));
	if (sourceEditor->lineIndexOfOffset(sourceEditor->cursorOffset()) != 2) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Debug Bento source cursor did not move to line 3. cursor=" << sourceEditor->cursorOffset() << " line=" << (sourceEditor->lineIndexOfOffset(sourceEditor->cursorOffset()) + 1) << " text=" << sourceEditor->snapshotText() << "\n";
		return 1;
	}
	mrStatusLine = dynamic_cast<MRStatusLine *>(TProgram::statusLine);
	if (mrStatusLine == nullptr) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Regression app has no MRStatusLine.\n";
		return 1;
	}
	mrStatusLine->setContextFunctionKeyLabels({{TKey(kbF9), cmMrOtherBuildCurrentFile, "~F9~ BP"}});
	mrStatusLine->setContextFunctionKeysActive(true);
	TView::enableCommand(cmMrOtherBuildCurrentFile);
	std::memset(&event, 0, sizeof(event));
	event.what = evKeyDown;
	event.keyDown.keyCode = kbF9;
	mrStatusLine->handleEvent(event);
	if (event.what != evCommand || event.message.command != cmMrOtherBuildCurrentFile) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Statusline F9 did not become the build command. what=" << event.what << " command=" << event.message.command << "\n";
		return 1;
	}
	app.handleEvent(event);
	if (event.what != evNothing) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Statusline F9 command was not consumed by the macro debugger route.\n";
		return 1;
	}
	std::memset(&event, 0, sizeof(event));
	event.what = evKeyDown;
	event.keyDown.keyCode = kbF9;
	app.handleEvent(event);
	if (event.what != evNothing) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Raw F9 was not consumed by the macro debugger route.\n";
		return 1;
	}
	if (!mrAppendLogBufferToFile(logPath, &logError)) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Unable to write macro debugger F9 route log: " << logError << "\n";
		return 1;
	}
	outputEditor = debuggerOutput->getEditor();
	outputText = outputEditor != nullptr ? outputEditor->snapshotText() : std::string();
	if (outputText.find("Breakpoint:") == std::string::npos || outputText.find("Breakpoints:") == std::string::npos) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "F9 did not refresh Debugger Output. Output was: " << outputText << "\n";
		return 1;
	}
	std::memset(&commandEvent, 0, sizeof(commandEvent));
	commandEvent.what = evCommand;
	commandEvent.message.command = cmMrOtherBuildCurrentFile;
	app.handleEvent(commandEvent);
	if (commandEvent.what != evNothing) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Build command was not consumed by the macro debugger route.\n";
		return 1;
	}
	outputText = outputEditor != nullptr ? outputEditor->snapshotText() : std::string();
	if (outputText.find("Breakpoints:") == std::string::npos || outputText.find("#3") == std::string::npos) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "F9 route did not leave an active line-3 breakpoint. Output was: " << outputText << "\n";
		return 1;
	}
	std::memset(&event, 0, sizeof(event));
	event.what = evKeyDown;
	event.keyDown.keyCode = kbF9;
	event.keyDown.controlKeyState = kbShift;
	app.handleEvent(event);
	outputText = outputEditor != nullptr ? outputEditor->snapshotText() : std::string();
	if (event.what != evNothing || outputText.find("Breakpoint: disabled") == std::string::npos) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Shift+F9 did not disable the source breakpoint. Output was: " << outputText << "\n";
		return 1;
	}
	std::memset(&event, 0, sizeof(event));
	event.what = evKeyDown;
	event.keyDown.keyCode = kbF9;
	event.keyDown.controlKeyState = kbShift;
	app.handleEvent(event);
	outputText = outputEditor != nullptr ? outputEditor->snapshotText() : std::string();
	if (event.what != evNothing || outputText.find("Breakpoint: enabled") == std::string::npos) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Shift+F9 did not re-enable the source breakpoint. Output was: " << outputText << "\n";
		return 1;
	}
	std::memset(&event, 0, sizeof(event));
	event.what = evKeyDown;
	event.keyDown.keyCode = kbF9;
	event.keyDown.controlKeyState = static_cast<ushort>(kbAltShift | kbShift);
	app.handleEvent(event);
	outputText = outputEditor != nullptr ? outputEditor->snapshotText() : std::string();
	if (event.what != evNothing || outputText.find("Breakpoints: all disabled") == std::string::npos) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Alt+Shift+F9 did not disable all source breakpoints. Output was: " << outputText << "\n";
		return 1;
	}
	std::memset(&event, 0, sizeof(event));
	event.what = evKeyDown;
	event.keyDown.keyCode = kbF9;
	event.keyDown.controlKeyState = static_cast<ushort>(kbAltShift | kbShift);
	app.handleEvent(event);
	outputText = outputEditor != nullptr ? outputEditor->snapshotText() : std::string();
	if (event.what != evNothing || outputText.find("Breakpoints: all enabled") == std::string::npos) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Alt+Shift+F9 did not re-enable all source breakpoints. Output was: " << outputText << "\n";
		return 1;
	}
	std::memset(&event, 0, sizeof(event));
	event.what = evKeyDown;
	event.keyDown.keyCode = kbF9;
	event.keyDown.controlKeyState = static_cast<ushort>(kbCtrlShift | kbShift);
	app.handleEvent(event);
	outputText = outputEditor != nullptr ? outputEditor->snapshotText() : std::string();
	if (event.what != evNothing || outputText.find("Breakpoints: all cleared") == std::string::npos) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Ctrl+Shift+F9 did not clear all source breakpoints. Output was: " << outputText << "\n";
		return 1;
	}
	std::memset(&commandEvent, 0, sizeof(commandEvent));
	commandEvent.what = evCommand;
	commandEvent.message.command = cmMrOtherBuildCurrentFile;
	app.handleEvent(commandEvent);
	if (commandEvent.what != evNothing) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "F9 could not restore the cleared source breakpoint.\n";
		return 1;
	}
	std::memset(&continueEvent, 0, sizeof(continueEvent));
	continueEvent.what = evCommand;
	continueEvent.message.command = cmMrMacroDebuggerContinue;
	app.handleEvent(continueEvent);
	if (continueEvent.what != evNothing) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Continue command was not consumed by the macro debugger route.\n";
		return 1;
	}
	std::memset(&continueEvent, 0, sizeof(continueEvent));
	continueEvent.what = evCommand;
	continueEvent.message.command = cmMrMacroDebuggerContinue;
	app.handleEvent(continueEvent);
	if (continueEvent.what != evNothing) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Pause command was not consumed by the macro debugger route.\n";
		return 1;
	}
	bento->pumpMacroDebuggerSession();
	outputText = outputEditor != nullptr ? outputEditor->snapshotText() : std::string();
	if (outputText.find("State: paused") == std::string::npos || outputText.find("Stop: paused") == std::string::npos) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "F5 pause did not stop the scheduled debug session. Output was: " << outputText << "\n";
		return 1;
	}
	std::memset(&continueEvent, 0, sizeof(continueEvent));
	continueEvent.what = evCommand;
	continueEvent.message.command = cmMrMacroDebuggerContinue;
	app.handleEvent(continueEvent);
	if (continueEvent.what != evNothing) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Continue-after-pause command was not consumed by the macro debugger route.\n";
		return 1;
	}
	bento->pumpMacroDebuggerSession();
	outputText = outputEditor != nullptr ? outputEditor->snapshotText() : std::string();
	if (outputText.find("State: paused") == std::string::npos || outputText.find("Stop: breakpoint") == std::string::npos) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "F5 continue did not stop at the active breakpoint. Output was: " << outputText << "\n";
		return 1;
	}
	variablesEditor = variables != nullptr ? variables->getEditor() : nullptr;
	outputText = variablesEditor != nullptr ? variablesEditor->snapshotText() : std::string();
	if (outputText.find("Variables") == std::string::npos || outputText.find("Session") == std::string::npos || outputText.find("X [int] = 0") == std::string::npos) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "F5 continue did not refresh the Variables pane with a typed local snapshot. Variables were: " << outputText << "\n";
		return 1;
	}
	std::memset(&stopEvent, 0, sizeof(stopEvent));
	stopEvent.what = evCommand;
	stopEvent.message.command = cmMrMacroDebuggerStop;
	app.handleEvent(stopEvent);
	if (stopEvent.what != evNothing) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Stop command was not consumed by the macro debugger route.\n";
		return 1;
	}
	outputText = outputEditor != nullptr ? outputEditor->snapshotText() : std::string();
	if (outputText.find("State: stopped/no live session") == std::string::npos || outputText.find("F5 Pause/Continue unavailable") != std::string::npos || outputText.find("F8 Stop unavailable") != std::string::npos || outputText.find("F10 Step unavailable") != std::string::npos) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "F8 stop did not close the live debug session. Output was: " << outputText << "\n";
		return 1;
	}
	if (!mrAppendLogBufferToFile(logPath, &logError)) {
		destroyRegressionWindow(bento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Unable to append macro debugger F9 command route log: " << logError << "\n";
		return 1;
	}
	destroyRegressionWindow(bento);
	(void)mrvmCloseDebugSession(debugSession.sessionId);
	(void)::remove(macroPath.c_str());
	if (!readTextFile(logPath, logText) || logText.find("MACRODBG key stage=app-macro-debugger-route") == std::string::npos || logText.find("MACRODBG key stage=bento-fkey") == std::string::npos || logText.find("MACRODBG key stage=bento-toggle") == std::string::npos) {
		std::cerr << "Macro debugger F9 route log did not contain expected instrumentation.\n";
		return 1;
	}
	if (logText.find("MACRODBG key stage=app-build-command") == std::string::npos) {
		std::cerr << "Macro debugger F9 command route log did not contain expected instrumentation.\n";
		return 1;
	}
	if (logText.find("MACRODBG key stage=app-continue-command") == std::string::npos || logText.find("MACRODBG key stage=bento-continue") == std::string::npos) {
		std::cerr << "Macro debugger F5 continue route log did not contain expected instrumentation.\n";
		return 1;
	}
	if (logText.find("MACRODBG key stage=app-stop-command") == std::string::npos || logText.find("MACRODBG key stage=bento-stop") == std::string::npos) {
		std::cerr << "Macro debugger F8 stop route log did not contain expected instrumentation.\n";
		return 1;
	}
	std::cout << "macro-debugger-f9-route consumed=1 output=1 log=" << logPath << "\n";
	return 0;
}

int runMacroDebuggerWorkspaceBreakpointRoundtripProbeMode() {
	MREditorApp app;
	MRBentoBox *sourceBento = nullptr;
	MRBentoBox *restoredBento = nullptr;
	MRSetupPaths paths = resolveSetupPathDefaults();
	MRMacroDebuggerWorkspaceConfiguration configuration;
	std::vector<MRMacroDebuggerBreakpoint> breakpoints;
	std::string source;
	std::string workspace;
	std::string error;
	const long probePid = static_cast<long>(::getpid());
	const std::string macroName = "MacroDebuggerWorkspaceProbe" + std::to_string(probePid);
	const std::string macroPath = "/tmp/mr_macro_debugger_workspace_probe_" + std::to_string(probePid) + ".mrmac";
	const std::string workspacePath = "/tmp/mr_macro_debugger_workspace_probe_" + std::to_string(probePid) + ".workspace.mrmac";

	auto hasWorkspaceBreakpoint = [](const MRMacroDebuggerWorkspaceConfiguration &candidate, int line, bool enabled) {
		for (const MRMacroDebuggerWorkspaceBreakpoint &breakpoint : candidate.breakpoints)
			if (breakpoint.line == line && breakpoint.enabled == enabled) return true;
		return false;
	};
	auto hasRuntimeBreakpoint = [](const std::vector<MRMacroDebuggerBreakpoint> &candidate, int line, bool enabled) {
		for (const MRMacroDebuggerBreakpoint &breakpoint : candidate)
			if (breakpoint.line == line && breakpoint.enabled == enabled) return true;
		return false;
	};

	source = "$MACRO " + macroName + ";\n"
	         "DEF_INT(X);\n"
	         "X := 1;\n"
	         "X := X + 1;\n"
	         "END_MACRO;\n";
	{
		std::ofstream out(macroPath.c_str(), std::ios::out | std::ios::trunc);
		if (!out) {
			std::cerr << "Unable to write macro debugger workspace fixture.\n";
			return 1;
		}
		out << source;
	}
	if (TProgram::deskTop == nullptr) {
		(void)::remove(macroPath.c_str());
		std::cerr << "Regression app has no desktop.\n";
		return 1;
	}
	sourceBento = new MRBentoBox(TRect(0, 0, 120, 32), "debug-workspace-roundtrip", 1702, bbmToolWorkspace);
	if (sourceBento == nullptr) {
		(void)::remove(macroPath.c_str());
		std::cerr << "Unable to allocate workspace debugger Bento.\n";
		return 1;
	}
	TProgram::deskTop->insert(sourceBento);
	if (sourceBento->getEditor() == nullptr || !sourceBento->getEditor()->loadMappedFile(macroPath.c_str(), error)) {
		destroyRegressionWindow(sourceBento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Unable to load workspace debugger fixture: " << error << "\n";
		return 1;
	}
	sourceBento->setMacroDebuggerTarget(macroName, macroName);
	MRMacroExecutionSession seedSession;
	MRMacroDebugRunResult seedResult;

	if (!mrvmLoadMacroFile(macroPath, &error)) {
		destroyRegressionWindow(sourceBento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Unable to load workspace debugger macro: " << error << "\n";
		return 1;
	}
	seedResult = mrvmStartDebugMacroByName(macroName, MRMacroExecutionOwner(), &seedSession, &error, true);
	if (seedSession.sessionId == 0 || !seedResult.paused || !mrvmCloseDebugSession(seedSession.sessionId) || !mrvmWriteDebugLineBreakpoint(macroName, 3, true, &error) || !mrvmWriteDebugLineBreakpoint(macroName, 4, false, &error)) {
		destroyRegressionWindow(sourceBento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Unable to seed workspace debugger breakpoints: " << error << "\n";
		return 1;
	}
	if (!sourceBento->macroDebuggerWorkspaceConfiguration(configuration) || !hasWorkspaceBreakpoint(configuration, 3, true) || !hasWorkspaceBreakpoint(configuration, 4, false)) {
		destroyRegressionWindow(sourceBento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Workspace debugger source configuration does not contain enabled and disabled breakpoints.\n";
		return 1;
	}
	paths.settingsMacroUri = workspacePath;
	workspace = buildSettingsMacroSourceWithWorkspace(paths);
	if (workspace.find(" debug=v2") == std::string::npos || workspace.find(":3:1:") == std::string::npos || workspace.find(":4:0:") == std::string::npos) {
		destroyRegressionWindow(sourceBento);
		(void)::remove(macroPath.c_str());
		std::cerr << "Workspace serialization did not encode enabled and disabled breakpoint states.\n";
		return 1;
	}
	{
		std::ofstream out(workspacePath.c_str(), std::ios::out | std::ios::trunc);
		if (!out) {
			destroyRegressionWindow(sourceBento);
			(void)::remove(macroPath.c_str());
			std::cerr << "Unable to write debugger workspace fixture.\n";
			return 1;
		}
		out << workspace;
	}
	if (!mrvmEraseDebugLineBreakpointsForMacroFile(macroName, &error)) {
		destroyRegressionWindow(sourceBento);
		(void)::remove(workspacePath.c_str());
		(void)::remove(macroPath.c_str());
		std::cerr << "Unable to clear runtime breakpoints before workspace load: " << error << "\n";
		return 1;
	}
	destroyRegressionWindow(sourceBento);
	sourceBento = nullptr;
	mrLoadWorkspace(workspacePath);
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		MRBentoBox *candidate = dynamic_cast<MRBentoBox *>(window);
		MRMacroDebuggerWorkspaceConfiguration candidateConfiguration;

		if (candidate != nullptr && candidate->macroDebuggerWorkspaceConfiguration(candidateConfiguration) && candidateConfiguration.macroKey == macroName) {
			restoredBento = candidate;
			configuration = candidateConfiguration;
			break;
		}
	}
	if (restoredBento == nullptr || !hasWorkspaceBreakpoint(configuration, 3, true) || !hasWorkspaceBreakpoint(configuration, 4, false)) {
		(void)::remove(workspacePath.c_str());
		(void)::remove(macroPath.c_str());
		std::cerr << "Workspace load did not restore enabled and disabled breakpoint configuration.\n";
		return 1;
	}
	TProgram::deskTop->setCurrent(restoredBento, TView::enterSelect);
	TEvent resetEvent;

	std::memset(&resetEvent, 0, sizeof(resetEvent));
	resetEvent.what = evCommand;
	resetEvent.message.command = cmMrMacroDebuggerStop;
	app.handleEvent(resetEvent);
	if (resetEvent.what != evNothing) {
		destroyRegressionWindow(restoredBento);
		(void)::remove(workspacePath.c_str());
		(void)::remove(macroPath.c_str());
		std::cerr << "Workspace-loaded debugger F8 reset route was not consumed.\n";
		return 1;
	}
	if (!mrvmDebugLineBreakpointsForMacro(macroName, breakpoints) || !hasRuntimeBreakpoint(breakpoints, 3, true) || !hasRuntimeBreakpoint(breakpoints, 4, false)) {
		destroyRegressionWindow(restoredBento);
		(void)::remove(workspacePath.c_str());
		(void)::remove(macroPath.c_str());
		std::cerr << "Workspace-loaded debugger did not rebind enabled and disabled breakpoint states.\n";
		return 1;
	}
	destroyRegressionWindow(restoredBento);
	(void)mrvmEraseDebugLineBreakpointsForMacroFile(macroName, nullptr);
	(void)::remove(workspacePath.c_str());
	(void)::remove(macroPath.c_str());
	std::cout << "macro-debugger-workspace-breakpoint-roundtrip serialized=1 restored=1 rebound=1\n";
	return 0;
}

bool runRegressionProbeProcess(const char *probeName, std::string &failureReason) {
	const std::string logPath = "/tmp/mr_regression_probe_" + std::string(probeName) + "_" + std::to_string(static_cast<long>(::getpid())) + ".log";
	const std::string command = "./regression/mr-regression-checks --probe " + std::string(probeName) + " >" + logPath + " 2>&1";
	const int status = std::system(command.c_str());
	std::string output;

	if (status == 0) {
		failureReason.clear();
		return true;
	}
	if (readTextFile(logPath, output) && !output.empty()) {
		const std::size_t lineEnd = output.find_first_of("\r\n");
		failureReason = lineEnd == std::string::npos ? output : output.substr(0, lineEnd);
	} else if (WIFEXITED(status))
		failureReason = "Probe exited with code " + std::to_string(WEXITSTATUS(status)) + ".";
	else if (WIFSIGNALED(status))
		failureReason = "Probe terminated by signal " + std::to_string(WTERMSIG(status)) + ".";
	else
		failureReason = "Probe did not complete successfully.";
	return false;
}

bool testConfiguredKeymapBasicNavigationHarness(const std::string &keymapContent, std::string &failureReason) {
	ScopedRegressionKeymap restoreKeymap;
	ScopedRegressionMacroDirectory macroDirectory(absolutePathFromCwd("mrmac/macros"));
	ScopedRegressionCursorBehaviour cursorBehaviour(MRCursorBehaviour::BoundToText);
	MREditWindow window(TRect(0, 0, 80, 16), "keymap-basic-nav", 1032);
	MRFileEditor *editor = nullptr;

	if (!installRegressionKeymap(keymapContent, failureReason)) return false;
	if (!window.replaceTextBuffer("abc\n", "keymap-basic-nav")) {
		failureReason = "Unable to seed window editor for configured keymap basic navigation path.";
		return false;
	}
	editor = window.getEditor();
	if (editor == nullptr) {
		failureReason = "Configured keymap basic navigation path must have an editor.";
		return false;
	}
	editor->setCursorOffset(0);
	if (!sendWindowRawCtrl(window, 'D')) return false;
	if (editor->cursorOffset() != 1) {
		failureReason = "Configured keymap Ctrl-D must move the cursor right through the window key path.";
		return false;
	}
	if (!sendWindowRawCtrl(window, 'S')) return false;
	if (editor->cursorOffset() != 0) {
		failureReason = "Configured keymap Ctrl-S must move the cursor left through the window key path.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testConfiguredKeymapBlockBindingsHarness(const std::string &defaultKeymapContent, std::string &failureReason) {
	ScopedRegressionKeymap restoreKeymap;
	ScopedRegressionMacroDirectory macroDirectory(absolutePathFromCwd("mrmac/macros"));
	ScopedRegressionCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	ScopedRegressionPersistentBlocks persistentBlocks(true);
	MREditSetupSettings editSettings = configuredEditSetupSettings();

	editSettings.columnBlockMove = "DELETE_SPACE";
	ScopedRegressionEditSetupSettings editSetup(editSettings);

	if (!installRegressionKeymap(defaultKeymapContent, failureReason)) return false;
	{
		MREditWindow window(TRect(0, 0, 80, 16), "keymap-block", 1010);
		if (!window.replaceTextBuffer("alpha\nbeta\ngamma\n", "keymap-block")) {
			failureReason = "Unable to seed window editor for Ctrl-Y key path.";
			return false;
		}
		if (!sendWindowRawCtrl(window, 'Y')) return false;
		if (window.getEditor() == nullptr || window.getEditor()->snapshotText() != "beta\ngamma\n") {
			failureReason = "Ctrl-Y must delete the current line through the configured keymap.";
			return false;
		}
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "keymap-stream-block", 1011);
		if (!window.replaceTextBuffer("alpha\nbeta\ngamma\n", "keymap-stream-block")) {
			failureReason = "Unable to seed window editor for Ctrl-K stream block path.";
			return false;
		}
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowRawCtrl(window, 'B')) return false;
		if (!sendWindowRawCtrl(window, 'D')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, true, 1, 1, 1, 1, "Configured keymap Ctrl-K Ctrl-B must not live-grow stream", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, false, 1, 1, 1, 2, "Configured keymap Ctrl-K Ctrl-B/Ctrl-K Ctrl-K stream", failureReason)) return false;
		if (!expectWindowBlockOverlay(window, MREditWindow::bmStream, "Configured keymap committed stream overlay", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'D')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, false, 1, 1, 1, 2, "Configured keymap persistent stream after cursor move", failureReason)) return false;
		if (!expectWindowBlockOverlay(window, MREditWindow::bmStream, "Configured keymap persistent stream overlay after cursor move", failureReason)) return false;
		if (!expectWindowCommittedBlockRefreshesOverlay(window, rawCtrlKey('D'), 0, MREditWindow::bmStream, "Configured keymap committed stream overlay refresh after keybinding cursor move", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowKey(window, static_cast<ushort>('H'))) return false;
		if (window.blockStatus() != MREditWindow::bmNone || window.hasBlock()) {
			failureReason = "Configured keymap Ctrl-K H must hide the visible block.";
			return false;
		}
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowRawCtrl(window, 'H')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, false, 1, 1, 1, 2, "Configured keymap Ctrl-K Ctrl-H show", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "keymap-stream-block-plain", 1012);
		if (!window.replaceTextBuffer("alpha\nbeta\ngamma\n", "keymap-block-plain")) {
			failureReason = "Unable to seed window editor for Ctrl-K B stream block path.";
			return false;
		}
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowKey(window, static_cast<ushort>('b'))) return false;
		if (!sendWindowRawCtrl(window, 'D')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, true, 1, 1, 1, 1, "Configured keymap Ctrl-K B must not live-grow stream", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowKey(window, static_cast<ushort>('k'))) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, false, 1, 1, 1, 2, "Configured keymap Ctrl-K B/Ctrl-K K stream", failureReason)) return false;
		if (!expectWindowBlockOverlay(window, MREditWindow::bmStream, "Configured keymap committed plain stream overlay", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "keymap-stream-block-arrow", 1012);
		if (!window.replaceTextBuffer("alpha\nbeta\ngamma\n", "keymap-block-arrow")) {
			failureReason = "Unable to seed window editor for Ctrl-K B arrow stream block path.";
			return false;
		}
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowKey(window, static_cast<ushort>('b'))) return false;
		if (!sendWindowKey(window, kbRight)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, true, 1, 1, 1, 2, "Configured keymap Ctrl-K B must remain marking after plain Right", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowKey(window, static_cast<ushort>('k'))) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, false, 1, 1, 1, 2, "Configured keymap Ctrl-K B/plain Right/Ctrl-K K stream", failureReason)) return false;
		if (!expectWindowBlockOverlay(window, MREditWindow::bmStream, "Configured keymap committed arrow stream overlay", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "keymap-column-block", 1013);
		if (!window.replaceTextBuffer("alpha\n\nbeta\ngamma\n", "keymap-column-block")) {
			failureReason = "Unable to seed window editor for Ctrl-K N column block path.";
			return false;
		}
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowRawCtrl(window, 'N')) return false;
		if (!sendWindowRawCtrl(window, 'D')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmColumn, true, 1, 1, 1, 1, "Configured keymap Ctrl-K Ctrl-N must not live-grow column right", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'X')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmColumn, true, 1, 1, 1, 1, "Configured keymap Ctrl-K Ctrl-N must not live-grow column down over empty line", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmColumn, false, 1, 2, 1, 2, "Configured keymap Ctrl-K Ctrl-N/Ctrl-K Ctrl-K column", failureReason)) return false;
		if (!expectWindowBlockOverlay(window, MREditWindow::bmColumn, "Configured keymap committed column overlay", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'D')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmColumn, false, 1, 2, 1, 2, "Configured keymap persistent column after cursor move", failureReason)) return false;
		if (!expectWindowBlockOverlay(window, MREditWindow::bmColumn, "Configured keymap persistent column overlay after cursor move", failureReason)) return false;
		if (!expectWindowCommittedBlockRefreshesOverlay(window, rawCtrlKey('D'), 0, MREditWindow::bmColumn, "Configured keymap committed column overlay refresh after keybinding cursor move", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "mrmac-action-nonlive-column", 1014);
		if (!window.replaceTextBuffer("alpha\n\nbeta\ngamma\n", "mrmac-action-live-column")) {
			failureReason = "Unable to seed window editor for direct MRMAC action non-live column path.";
			return false;
		}
		if (!dispatchMRKeymapAction("MRMAC_BLOCK_SET_COLUMN_BEGIN", "", &window)) {
			failureReason = "MRMAC_BLOCK_SET_COLUMN_BEGIN action dispatch failed.";
			return false;
		}
		if (!dispatchMRKeymapAction("MRMAC_CURSOR_RIGHT", "", &window)) {
			failureReason = "MRMAC_CURSOR_RIGHT action dispatch failed.";
			return false;
		}
		if (!expectWindowBlock(window, MREditWindow::bmColumn, true, 1, 1, 1, 1, "direct MRMAC action must not live-grow column right", failureReason)) return false;
		if (!expectWindowBlockOverlay(window, MREditWindow::bmColumn, "direct MRMAC action non-live column overlay", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "mrmac-action-stream-begin-end", 1017);
		if (!window.replaceTextBuffer("alpha\nbeta\ngamma\n", "mrmac-action-stream-begin-end")) {
			failureReason = "Unable to seed window editor for direct MRMAC stream begin/end path.";
			return false;
		}
		if (!dispatchMRKeymapAction("MRMAC_BLOCK_SET_BEGIN", "", &window)) {
			failureReason = "MRMAC_BLOCK_SET_BEGIN action dispatch failed.";
			return false;
		}
		if (!dispatchMRKeymapAction("MRMAC_CURSOR_RIGHT", "", &window)) {
			failureReason = "MRMAC_CURSOR_RIGHT action dispatch failed after stream begin.";
			return false;
		}
		if (!expectWindowBlock(window, MREditWindow::bmStream, true, 1, 1, 1, 1, "direct MRMAC stream begin must not live-grow", failureReason)) return false;
		if (!dispatchMRKeymapAction("MRMAC_BLOCK_SET_END", "", &window)) {
			failureReason = "MRMAC_BLOCK_SET_END action dispatch failed.";
			return false;
		}
		if (!expectWindowBlock(window, MREditWindow::bmStream, false, 1, 1, 1, 2, "direct MRMAC stream begin/end", failureReason)) return false;
		if (!expectWindowBlockOverlay(window, MREditWindow::bmStream, "direct MRMAC committed stream overlay", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "mrmac-action-clear-state", 1018);
		MRFileEditor *editor = window.getEditor();
		if (editor == nullptr || !window.replaceTextBuffer("alpha\nbeta\ngamma\n", "mrmac-action-clear-state")) {
			failureReason = "Unable to seed window editor for direct MRMAC clear-state path.";
			return false;
		}
		if (!dispatchMRKeymapAction("MRMAC_BLOCK_SET_BEGIN", "", &window)) {
			failureReason = "MRMAC_BLOCK_SET_BEGIN action dispatch failed before clear-state path.";
			return false;
		}
		if (!dispatchMRKeymapAction("MRMAC_CURSOR_RIGHT", "", &window)) {
			failureReason = "MRMAC_CURSOR_RIGHT action dispatch failed before clear-state path.";
			return false;
		}
		if (!dispatchMRKeymapAction("MRMAC_BLOCK_CLEAR", "", &window)) {
			failureReason = "MRMAC_BLOCK_CLEAR action dispatch failed.";
			return false;
		}
		if (window.hasBlock() || window.isBlockMarking() || editor->blockOverlayState().active || editor->hasTextSelection()) {
			failureReason = "Direct MRMAC block clear must remove marking state, overlay and editor selection.";
			return false;
		}
		if (!dispatchMRKeymapAction("MRMAC_CURSOR_DOWN", "", &window) || !dispatchMRKeymapAction("MRMAC_CURSOR_RIGHT", "", &window)) {
			failureReason = "Direct MRMAC cursor action dispatch failed after clear-state path.";
			return false;
		}
		if (window.hasBlock() || window.isBlockMarking() || editor->blockOverlayState().active || editor->hasTextSelection() || editor->currentLineNumber() != 2 || editor->currentColumnNumber() != 3) {
			failureReason = "Direct MRMAC cursor movement after block clear must not reactivate marking state: block=" + std::to_string(window.hasBlock() ? 1 : 0) +
			                " marking=" + std::to_string(window.isBlockMarking() ? 1 : 0) + " overlay=" + std::to_string(editor->blockOverlayState().active ? 1 : 0) +
			                " selection=" + std::to_string(editor->hasTextSelection() ? 1 : 0) + " line=" + std::to_string(editor->currentLineNumber()) +
			                " column=" + std::to_string(editor->currentColumnNumber()) + ".";
			return false;
		}
	}
	{
		const std::string text = "aa MOVE zz\nend";
		MREditWindow window(TRect(0, 0, 80, 16), "wordstar-keymap-move-undo", 1015);
		MRFileEditor *editor = window.getEditor();
		if (editor == nullptr) {
			failureReason = "Keymap block move undo path must have an editor.";
			return false;
		}
		if (!window.replaceTextBuffer(text.c_str(), "wordstar-keymap-move-undo")) {
			failureReason = "Unable to seed editor for keymap block move undo path.";
			return false;
		}
		editor->setCursorOffset(3);
		window.beginStreamBlock();
		editor->setCursorOffset(7);
		window.endBlock();
		if (!window.hasBlock()) {
			failureReason = "Stream block must be committed before Ctrl-K V.";
			return false;
		}
		editor->setCursorOffset(text.size());
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowRawCtrl(window, 'V')) return false;
		if (editor->snapshotText() != "aa  zz\nendMOVE") {
			failureReason = "Ctrl-K V window key path after committed block produced wrong text: " + editor->snapshotText();
			return false;
		}
		if (!sendWindowCommand(window, cmMrEditUndo)) return false;
		if (editor->snapshotText() != text) {
			failureReason = "Window undo command after Ctrl-K V block move must restore original text, got: " + editor->snapshotText();
			return false;
		}
		if (editor->snapshotText().empty()) {
			failureReason = "Window undo command after Ctrl-K V block move must not clear the editor.";
			return false;
		}
		if (!window.hasBlock() || window.blockStatus() != MREditWindow::bmStream) {
			failureReason = "Window undo command after Ctrl-K V block move must preserve the visible stream block mark.";
			return false;
		}
		editor->setCursorOffset(3);
		window.beginStreamBlock();
		editor->setCursorOffset(7);
		window.endBlock();
		editor->setCursorOffset(text.size());
		if (!dispatchMRKeymapAction("MRMAC_BLOCK_MOVE", "<Ctrl+K> <Ctrl+V>", &window)) {
			failureReason = "MRMAC_BLOCK_MOVE action dispatch failed in keymap block move undo path.";
			return false;
		}
		if (!dispatchMRKeymapAction("MRMAC_UNDO", "<Ctrl+Z>", &window)) {
			failureReason = "MRMAC_UNDO action dispatch failed after keymap block move.";
			return false;
		}
		if (editor->snapshotText() != text) {
			failureReason = "Ctrl-Z after Ctrl-K V block move must restore original text, got: " + editor->snapshotText();
			return false;
		}
		if (editor->snapshotText().empty()) {
			failureReason = "Ctrl-Z after Ctrl-K V block move must not clear the editor.";
			return false;
		}
		if (!window.hasBlock() || window.blockStatus() != MREditWindow::bmStream) {
			failureReason = "Ctrl-Z after Ctrl-K V block move must preserve the visible stream block mark.";
			return false;
		}
	}
	{
		const std::string text = "aa COPY zz\nend";
		MREditWindow window(TRect(0, 0, 80, 16), "window-copy-undo-block", 1016);
		MRFileEditor *editor = window.getEditor();
		if (editor == nullptr) {
			failureReason = "Window copy undo path must have an editor.";
			return false;
		}
		if (!window.replaceTextBuffer(text.c_str(), "window-copy-undo-block")) {
			failureReason = "Unable to seed editor for window copy undo path.";
			return false;
		}
		editor->setCursorOffset(3);
		window.beginStreamBlock();
		editor->setCursorOffset(7);
		window.endBlock();
		editor->setCursorOffset(text.size());
		if (!window.copyBlock()) {
			failureReason = "Window copy block path failed before undo.";
			return false;
		}
		if (editor->snapshotText() != "aa COPY zz\nendCOPY") {
			failureReason = "Window copy block path produced wrong text before undo: " + editor->snapshotText();
			return false;
		}
		if (!sendWindowCommand(window, cmMrEditUndo)) return false;
		if (editor->snapshotText() != text) {
			failureReason = "Window undo command after block copy must restore original text, got: " + editor->snapshotText();
			return false;
		}
		if (!window.hasBlock() || window.blockStatus() != MREditWindow::bmStream) {
			failureReason = "Window undo command after block copy must preserve the visible stream block mark.";
			return false;
		}
	}
	{
		const std::string text = "one\ntwo\nlast";
		MREditWindow window(TRect(0, 0, 80, 16), "window-line-copy-undo-block", 1017);
		MRFileEditor *editor = window.getEditor();
		if (editor == nullptr) {
			failureReason = "Window line copy undo path must have an editor.";
			return false;
		}
		if (!window.replaceTextBuffer(text.c_str(), "window-line-copy-undo-block")) {
			failureReason = "Unable to seed editor for window line copy undo path.";
			return false;
		}
		editor->setCursorOffset(0);
		window.beginLineBlock();
		window.endBlock();
		editor->setCursorOffset(editor->nextLineOffset(editor->nextLineOffset(0)));
		if (!window.copyBlock()) {
			failureReason = "Window line copy block path failed before undo.";
			return false;
		}
		if (editor->snapshotText() != "one\ntwo\none\nlast") {
			failureReason = "Window line copy block path produced wrong text before undo: " + editor->snapshotText();
			return false;
		}
		if (!sendWindowCommand(window, cmMrEditUndo)) return false;
		if (editor->snapshotText() != text) {
			failureReason = "Window undo command after line block copy must restore original text, got: " + editor->snapshotText();
			return false;
		}
		if (!window.hasBlock() || window.blockStatus() != MREditWindow::bmLine || editor->hasTextSelection()) {
			failureReason = "Window undo command after line block copy must preserve the visible line block mark without text selection.";
			return false;
		}
	}
	{
		const std::string text = "012345\nabcdef\nXYZ";
		MREditWindow window(TRect(0, 0, 80, 16), "window-column-copy-undo-block", 1018);
		MRFileEditor *editor = window.getEditor();
		if (editor == nullptr) {
			failureReason = "Window column copy undo path must have an editor.";
			return false;
		}
		if (!window.replaceTextBuffer(text.c_str(), "window-column-copy-undo-block")) {
			failureReason = "Unable to seed editor for window column copy undo path.";
			return false;
		}
		const std::size_t secondLine = editor->nextLineOffset(0);
		const std::size_t thirdLine = editor->nextLineOffset(secondLine);
		editor->setCursorOffsetAtVisualColumn(1, 1);
		window.beginColumnBlock();
		editor->setCursorOffsetAtVisualColumn(secondLine + 4, 4);
		window.endBlock();
		editor->setCursorOffsetAtVisualColumn(thirdLine + 1, 1);
		if (!window.copyBlock()) {
			failureReason = "Window column copy block path failed before undo.";
			return false;
		}
		if (editor->snapshotText() != "012345\nabcdef\nX123YZ\n bcd") {
			failureReason = "Window column copy block path produced wrong text before undo: " + editor->snapshotText();
			return false;
		}
		if (!sendWindowCommand(window, cmMrEditUndo)) return false;
		if (editor->snapshotText() != text) {
			failureReason = "Window undo command after column block copy must restore original text, got: " + editor->snapshotText();
			return false;
		}
		if (!window.hasBlock() || window.blockStatus() != MREditWindow::bmColumn || editor->hasTextSelection()) {
			failureReason = "Window undo command after column block copy must preserve the visible column block mark without text selection.";
			return false;
		}
	}
	{
		const std::string text = "one\ntwo\nlast";
		MREditWindow window(TRect(0, 0, 80, 16), "window-line-move-undo-block", 1019);
		MRFileEditor *editor = window.getEditor();
		if (editor == nullptr) {
			failureReason = "Window line move undo path must have an editor.";
			return false;
		}
		if (!window.replaceTextBuffer(text.c_str(), "window-line-move-undo-block")) {
			failureReason = "Unable to seed editor for window line move undo path.";
			return false;
		}
		editor->setCursorOffset(0);
		window.beginLineBlock();
		window.endBlock();
		editor->setCursorOffset(editor->nextLineOffset(editor->nextLineOffset(0)));
		if (!window.moveBlock()) {
			failureReason = "Window line move block path failed before undo.";
			return false;
		}
		if (editor->snapshotText() != "two\none\nlast") {
			failureReason = "Window line move block path produced wrong text before undo: " + editor->snapshotText();
			return false;
		}
		if (!sendWindowCommand(window, cmMrEditUndo)) return false;
		if (editor->snapshotText() != text) {
			failureReason = "Window undo command after line block move must restore original text, got: " + editor->snapshotText();
			return false;
		}
		if (!window.hasBlock() || window.blockStatus() != MREditWindow::bmLine || editor->hasTextSelection()) {
			failureReason = "Window undo command after line block move must preserve the visible line block mark without text selection.";
			return false;
		}
	}
	{
		const std::string text = "012345\nabcdef\nXYZ";
		MREditWindow window(TRect(0, 0, 80, 16), "window-column-move-undo-block", 1020);
		MRFileEditor *editor = window.getEditor();
		if (editor == nullptr) {
			failureReason = "Window column move undo path must have an editor.";
			return false;
		}
		if (!window.replaceTextBuffer(text.c_str(), "window-column-move-undo-block")) {
			failureReason = "Unable to seed editor for window column move undo path.";
			return false;
		}
		const std::size_t secondLine = editor->nextLineOffset(0);
		const std::size_t thirdLine = editor->nextLineOffset(secondLine);
		editor->setCursorOffsetAtVisualColumn(1, 1);
		window.beginColumnBlock();
		editor->setCursorOffsetAtVisualColumn(secondLine + 4, 4);
		window.endBlock();
		editor->setCursorOffsetAtVisualColumn(thirdLine + 1, 1);
		if (!window.moveBlock()) {
			failureReason = "Window column move block path failed before undo.";
			return false;
		}
		if (editor->snapshotText() == text) {
			failureReason = "Window column move block path did not change text before undo.";
			return false;
		}
		if (!sendWindowCommand(window, cmMrEditUndo)) return false;
		if (editor->snapshotText() != text) {
			failureReason = "Window undo command after column block move must restore original text, got: " + editor->snapshotText();
			return false;
		}
		if (!window.hasBlock() || window.blockStatus() != MREditWindow::bmColumn || editor->hasTextSelection()) {
			failureReason = "Window undo command after column block move must preserve the visible column block mark without text selection.";
			return false;
		}
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "keymap-free-cursor", 1012);
		if (!window.replaceTextBuffer("abc", "keymap-free-cursor")) {
			failureReason = "Unable to seed window editor for configured keymap free-cursor path.";
			return false;
		}
		MRFileEditor *editor = window.getEditor();
		if (editor == nullptr) {
			failureReason = "Configured keymap free-cursor path must have an editor.";
			return false;
		}
		const std::size_t lineEnd = editor->lineEndOffset(0);
		editor->setCursorOffsetAtVisualColumn(lineEnd, static_cast<int>(editor->columnOfOffset(lineEnd)));
		const int before = editor->displayedCursorColumn();
		const int cursorXBefore = editor->cursor.x;
		if (!sendWindowRawCtrl(window, 'D')) return false;
		if (editor->cursorOffset() != lineEnd || editor->displayedCursorColumn() != before + 1) {
			failureReason = "Configured keymap Ctrl-D must honor free cursor movement beyond EOL.";
			return false;
		}
		if (editor->cursor.x != cursorXBefore + 1) {
			failureReason = "Configured keymap Ctrl-D must advance the visible editor caret beyond EOL.";
			return false;
		}
		if (window.cursorColumnNumber() != static_cast<unsigned long>(before + 2)) {
			failureReason = "Window cursor column must report the free cursor column beyond EOL.";
			return false;
		}
		if (!sendWindowRawCtrl(window, 'S')) return false;
		if (editor->cursorOffset() != lineEnd || editor->displayedCursorColumn() != before) {
			failureReason = "Configured keymap Ctrl-S must step back through free cursor columns.";
			return false;
		}
	}
	{
		MREditSetupSettings editSettings = configuredEditSetupSettings();
		editSettings.indentStyle = "AUTOMATIC";
		ScopedRegressionEditSetupSettings editSetup(editSettings);
		MREditWindow window(TRect(0, 0, 80, 16), "free-cursor-enter", 1021);
		if (!window.replaceTextBuffer("alpha\n\nomega", "free-cursor-enter")) {
			failureReason = "Unable to seed window editor for free-cursor Enter path.";
			return false;
		}
		MRFileEditor *editor = window.getEditor();
		if (editor == nullptr) {
			failureReason = "Free-cursor Enter path must have an editor.";
			return false;
		}
		const std::size_t emptyLine = editor->nextLineOffset(0);
		editor->setCursorOffsetAtVisualColumn(emptyLine, 12);
		if (!sendWindowKey(window, kbEnter)) return false;
		if (editor->snapshotText() != "alpha\n\n\nomega") {
			failureReason = "Free-cursor Enter must not materialize padding spaces before newline, got: " + editor->snapshotText();
			return false;
		}
		if (editor->displayedCursorColumn() != 0) {
			failureReason = "Free-cursor Enter must reset visible cursor column on the inserted blank line.";
			return false;
		}
		if (!window.replaceTextBuffer("alpha\n            \nomega", "free-cursor-enter-spaces")) {
			failureReason = "Unable to seed window editor for whitespace-only Enter path.";
			return false;
		}
		const std::size_t whitespaceLine = editor->nextLineOffset(0);
		editor->setCursorOffsetAtVisualColumn(editor->lineEndOffset(whitespaceLine), 12);
		if (!sendWindowKey(window, kbEnter)) return false;
		if (editor->snapshotText() != "alpha\n\n\nomega") {
			failureReason = "Automatic Enter on whitespace-only line must not propagate indentation, got: " + editor->snapshotText();
			return false;
		}
			if (editor->displayedCursorColumn() != 0) {
				failureReason = "Automatic Enter on whitespace-only line must reset visible cursor column.";
				return false;
			}
			if (!window.replaceTextBuffer("alpha\n        beta\nomega", "automatic-leading-whitespace-enter")) {
				failureReason = "Unable to seed window editor for leading-whitespace Enter path.";
				return false;
			}
			const std::size_t indentedLine = editor->nextLineOffset(0);
			editor->setCursorOffset(indentedLine + 4);
			if (!sendWindowKey(window, kbEnter)) return false;
			if (editor->snapshotText() != "alpha\n    \n    beta\nomega") {
				failureReason = "Automatic Enter inside leading whitespace must not duplicate the full line indent, got: " + editor->snapshotText();
				return false;
			}
			if (editor->displayedCursorColumn() != 0) {
				failureReason = "Automatic Enter inside leading whitespace must leave cursor at start of inserted line.";
				return false;
			}
		}
		return true;
	}

bool expectWindowFreeCursorRightPastEol(MREditWindow &window, const char *phase, std::string &failureReason) {
	MRFileEditor *editor = window.getEditor();
	if (editor == nullptr) {
		failureReason = std::string("Window editor missing in ") + phase + ".";
		return false;
	}
	const std::size_t lineEnd = editor->lineEndOffset(0);
	editor->setCursorOffsetAtVisualColumn(lineEnd, static_cast<int>(editor->columnOfOffset(lineEnd)));
	const int before = editor->displayedCursorColumn();
	const int cursorXBefore = editor->cursor.x;
	if (!sendWindowKey(window, kbRight)) return false;
	if (editor->cursorOffset() != lineEnd || editor->displayedCursorColumn() != before + 1) {
		failureReason = std::string("Window free cursor must advance past EOL in ") + phase + ".";
		return false;
	}
	if (editor->cursor.x != cursorXBefore + 1) {
		failureReason = std::string("Window visible editor caret must advance past EOL in ") + phase + ".";
		return false;
	}
	if (window.cursorColumnNumber() != static_cast<unsigned long>(before + 2)) {
		failureReason = std::string("Window cursor column must report the free cursor column in ") + phase + ".";
		return false;
	}
	return true;
}

bool testBlockMarkingWindowInputHarness(std::string &failureReason) {
	ScopedRegressionCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	ScopedRegressionPersistentBlocks persistentBlocks(true);
	static const char text[] = "alpha beta\n\nbeta\nomega";

	{
		MREditWindow window(TRect(0, 0, 80, 16), "block-input", 1001);
		if (!window.replaceTextBuffer(text, "block-input")) {
			failureReason = "Unable to seed window editor for word navigation and stream cursor path.";
			return false;
		}
		MRFileEditor *editor = window.getEditor();
		if (editor == nullptr) {
			failureReason = "Word navigation and stream cursor path must have an editor.";
			return false;
		}
		editor->setCursorOffset(0);
		if (!sendWindowKey(window, kbCtrlRight, kbCtrlShift)) return false;
		if (editor->cursorOffset() != 6 || editor->hasTextSelection() || window.blockStatus() != MREditWindow::bmNone || window.hasBlock()) {
			failureReason = "Ctrl+Right must move to the next word without selecting text or starting a block.";
			return false;
		}
		if (!sendWindowKey(window, kbCtrlLeft, kbCtrlShift)) return false;
		if (editor->cursorOffset() != 0 || editor->hasTextSelection() || window.blockStatus() != MREditWindow::bmNone || window.hasBlock()) {
			failureReason = "Ctrl+Left must move to the previous word without selecting text or starting a block.";
			return false;
		}
		if (!sendWindowKey(window, kbRight, kbShift)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, true, 1, 1, 1, 2, "cursor Shift+Right stream", failureReason)) return false;
		if (!sendWindowKey(window, kbRight)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, false, 1, 1, 1, 2, "plain cursor commits stream marking", failureReason)) return false;
		if (!expectWindowCommittedBlockRefreshesOverlay(window, kbRight, 0, MREditWindow::bmStream, "plain cursor committed stream overlay refresh", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "block-input", 1002);
		if (!window.replaceTextBuffer(text, "block-input")) {
			failureReason = "Unable to seed window editor for column cursor path.";
			return false;
		}
		if (!sendWindowKey(window, kbRight, kbShift | kbAltShift)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmColumn, true, 1, 1, 1, 2, "cursor Shift+Alt+Right column", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "block-input", 1003);
		if (!window.replaceTextBuffer(text, "block-input")) {
			failureReason = "Unable to seed window editor for line cursor path.";
			return false;
		}
		if (!sendWindowKey(window, kbDown, kbShift | kbCtrlShift | kbAltShift)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmLine, true, 1, 2, 1, 1, "cursor Shift+Ctrl+Alt+Down line", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "block-input", 1007);
		if (!window.replaceTextBuffer(text, "block-input")) {
			failureReason = "Unable to seed window editor for terminal scan-code cursor path.";
			return false;
		}
		if (!sendWindowKey(window, kbCtrlRight, kbShift | kbCtrlShift)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, true, 1, 1, 1, 2, "terminal Shift+CtrlRight scan stream", failureReason)) return false;
		window.clearBlock();
		if (window.getEditor() == nullptr) {
			failureReason = "Terminal scan-code cursor path must have an editor.";
			return false;
		}
		window.getEditor()->setCursorOffset(0);
		if (!sendWindowKey(window, kbAltRight, kbShift | kbAltShift)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmColumn, true, 1, 1, 1, 2, "terminal Shift+AltRight scan column", failureReason)) return false;
		window.clearBlock();
		window.getEditor()->setCursorOffset(0);
		if (!sendWindowKey(window, kbAltDown, kbShift | kbCtrlShift | kbAltShift)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmLine, true, 1, 2, 1, 1, "terminal Shift+CtrlAltDown scan line", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "block-input", 1004);
		if (!window.replaceTextBuffer(text, "block-input")) {
			failureReason = "Unable to seed window editor for menu stream path.";
			return false;
		}
			window.beginStreamBlock();
			window.endBlock();
			if (window.blockStatus() != MREditWindow::bmNone || window.hasBlock() || window.isBlockMarking()) {
				failureReason = "Empty menu/window stream begin-end must turn marking off.";
				return false;
			}
			window.beginStreamBlock();
			if (window.getEditor() == nullptr) {
				failureReason = "Menu/window stream path must have an editor.";
				return false;
			}
			window.getEditor()->setCursorOffset(1);
			window.endBlock();
			if (!expectWindowBlock(window, MREditWindow::bmStream, false, 1, 1, 1, 2, "menu/window stream begin-end", failureReason)) return false;
			if (!expectWindowBlockOverlay(window, MREditWindow::bmStream, "menu/window stream overlay", failureReason)) return false;
			if (!expectWindowFreeCursorRightPastEol(window, "window free cursor after stream block", failureReason)) return false;
			if (!expectWindowBlock(window, MREditWindow::bmStream, false, 1, 1, 1, 2, "menu/window persistent stream after cursor move", failureReason)) return false;
			if (!expectWindowBlockOverlay(window, MREditWindow::bmStream, "menu/window persistent stream overlay", failureReason)) return false;
			if (!expectWindowCommittedBlockRefreshesOverlay(window, kbRight, 0, MREditWindow::bmStream, "menu/window committed stream overlay refresh", failureReason)) return false;
		}
		{
			MREditWindow window(TRect(0, 0, 80, 16), "block-input", 1005);
		if (!window.replaceTextBuffer(text, "block-input")) {
			failureReason = "Unable to seed window editor for menu line path.";
			return false;
		}
		window.beginLineBlock();
			if (!sendWindowKey(window, kbDown)) return false;
			window.endBlock();
			if (!expectWindowBlock(window, MREditWindow::bmLine, false, 1, 2, 1, 1, "menu/window line begin-end", failureReason)) return false;
			if (!expectWindowBlockOverlay(window, MREditWindow::bmLine, "menu/window line overlay", failureReason)) return false;
			if (!expectWindowFreeCursorRightPastEol(window, "window free cursor after line block", failureReason)) return false;
			if (!expectWindowBlock(window, MREditWindow::bmLine, false, 1, 2, 1, 1, "menu/window persistent line after cursor move", failureReason)) return false;
			if (!expectWindowBlockOverlay(window, MREditWindow::bmLine, "menu/window persistent line overlay", failureReason)) return false;
			if (!expectWindowCommittedBlockRefreshesOverlay(window, kbRight, 0, MREditWindow::bmLine, "menu/window committed line overlay refresh", failureReason)) return false;
		}
		{
			MREditWindow window(TRect(0, 0, 80, 16), "block-input", 1006);
		if (!window.replaceTextBuffer(text, "block-input")) {
			failureReason = "Unable to seed window editor for menu column path.";
			return false;
		}
		window.beginColumnBlock();
			window.endBlock();
			if (window.blockStatus() != MREditWindow::bmNone || window.hasBlock() || window.isBlockMarking()) {
				failureReason = "Empty menu/window column begin-end must turn marking off.";
				return false;
			}
			window.beginColumnBlock();
			if (window.getEditor() == nullptr) {
				failureReason = "Menu/window column path must have an editor.";
				return false;
			}
			window.getEditor()->setCursorOffsetAtVisualColumn(1, 1);
			window.endBlock();
			if (!expectWindowBlock(window, MREditWindow::bmColumn, false, 1, 1, 1, 2, "menu/window column begin-end", failureReason)) return false;
			if (!expectWindowBlockOverlay(window, MREditWindow::bmColumn, "menu/window column overlay", failureReason)) return false;
			if (!window.toggleBlockVisibility()) {
				failureReason = "Window block visibility toggle should hide a marked column block.";
				return false;
		}
		if (window.blockStatus() != MREditWindow::bmNone || window.hasBlock()) {
			failureReason = "Hidden window block must not remain visible or operative.";
			return false;
		}
		if (!window.toggleBlockVisibility()) {
			failureReason = "Window block visibility toggle should show a stored column block.";
			return false;
			}
			if (!expectWindowBlock(window, MREditWindow::bmColumn, false, 1, 1, 1, 2, "menu/window column toggle show", failureReason)) return false;
			if (!expectWindowFreeCursorRightPastEol(window, "window free cursor after column block", failureReason)) return false;
			if (!expectWindowBlock(window, MREditWindow::bmColumn, false, 1, 1, 1, 2, "menu/window persistent column after cursor move", failureReason)) return false;
			if (!expectWindowBlockOverlay(window, MREditWindow::bmColumn, "menu/window persistent column overlay", failureReason)) return false;
			if (!expectWindowCommittedBlockRefreshesOverlay(window, kbRight, 0, MREditWindow::bmColumn, "menu/window committed column overlay refresh", failureReason)) return false;
		}
	return true;
}

bool testTextDocumentPieceTableMutationHarness(std::string &failureReason) {
	mr::editor::TextDocument document("alpha\nbeta\n");
	std::string model = "alpha\nbeta\n";

	if (!pieceTableHarnessCheckDocument(document, model, "initial", failureReason)) return false;

	PieceTableHarnessOperation op;
	op.kind = PieceTableHarnessOperationKind::Insert;
	op.range = mr::editor::Range(0, 0);
	op.text = "HEAD\n";
	if (!pieceTableHarnessApplySingle(document, model, op, false, "edge insert front", failureReason)) return false;

	op.kind = PieceTableHarnessOperationKind::Replace;
	op.range = mr::editor::Range(2, 9);
	op.text = "middle\nblock";
	if (!pieceTableHarnessApplySingle(document, model, op, true, "edge staged replace middle", failureReason)) return false;

	op.kind = PieceTableHarnessOperationKind::Erase;
	op.range = mr::editor::Range(3, 3);
	op.text.clear();
	if (!pieceTableHarnessApplySingle(document, model, op, false, "edge empty erase", failureReason)) return false;

	op.kind = PieceTableHarnessOperationKind::SetText;
	op.range = mr::editor::Range(0, model.size());
	op.text = model;
	if (!pieceTableHarnessApplySingle(document, model, op, true, "edge same setText", failureReason)) return false;

	{
		mr::editor::StagedEditTransaction stale(document.readSnapshot(), "stale staged conflict");
		stale.insert(1, "!");
		document.insert(0, "v");
		model.insert(0, "v");
		mr::editor::CommitResult conflict = document.tryApply(stale);
		if (!conflict.conflicted()) {
			failureReason = "stale staged transaction must report version conflict.";
			return false;
		}
		if (!pieceTableHarnessCheckDocument(document, model, "stale conflict", failureReason)) return false;
	}

	PieceTableHarnessRng rng(0x4d524645u);
	for (std::size_t i = 0; i < 240; ++i) {
		if (model.size() > 320) {
			op.kind = PieceTableHarnessOperationKind::Erase;
			op.range = mr::editor::Range(0, model.size() / 2);
			op.text.clear();
		} else
			op = pieceTableHarnessRandomOperation(rng, model, true);
		if (!pieceTableHarnessApplySingle(document, model, op, (i % 2) == 0, "deterministic single-op", failureReason)) return false;
	}

	for (std::size_t i = 0; i < 80; ++i) {
		if (model.size() > 320) {
			op.kind = PieceTableHarnessOperationKind::SetText;
			op.range = mr::editor::Range(0, model.size());
			op.text = "compact\nseed\n";
			if (!pieceTableHarnessApplySingle(document, model, op, (i % 2) == 0, "batch size reset", failureReason)) return false;
		}
		if (!pieceTableHarnessApplyBatch(document, model, rng, (i % 2) != 0, "deterministic multi-op", failureReason)) return false;
	}

	{
		mr::editor::TextDocument restored("line01\nline02\nline03\n");
		mr::editor::StagedEditTransaction tx(restored.readSnapshot(), "snapshot-restore-seed");
		std::string expected;
		tx.insert(6, "X");
		if (!restored.tryApply(tx).applied()) {
			failureReason = "Snapshot restore seed edit must apply.";
			return false;
		}
		expected = restored.text();
		mr::editor::ReadSnapshot snapshot = restored.readSnapshot();
		snapshot.compactLineIndexForUndo(13);
		restored.setText("temporary\n");
		restored.restoreFromSnapshot(snapshot);
		if (!pieceTableHarnessCheckDocument(restored, expected, "snapshot restore after compacted exact line index", failureReason)) return false;
		mr::editor::EditTransaction afterUndo("snapshot-restore-type");
		afterUndo.insert(0, "s");
		if (!restored.tryApply(afterUndo, restored.version()).applied()) {
			failureReason = "Snapshot restore follow-up edit must apply.";
			return false;
		}
		expected.insert(0, "s");
		if (!pieceTableHarnessCheckDocument(restored, expected, "snapshot restore follow-up typing", failureReason)) return false;
	}

	failureReason.clear();
	return true;
}

bool testDeferredLargeLineIndexHarness(std::string &failureReason) {
	static constexpr std::size_t kLargeBytes = static_cast<std::size_t>(8) * 1024 * 1024 + 4096;
	std::string text;
	text.reserve(kLargeBytes + 80);
	while (text.size() < kLargeBytes) {
		text.append(79, 'x');
		text.push_back('\n');
	}
	mr::editor::TextDocument document(text);
	if (document.exactLineCountKnown()) {
		failureReason = "large direct documents must not build the complete line index during construction.";
		return false;
	}
	const std::size_t estimatedLines = document.estimatedLineCount();
	if (estimatedLines < 2) {
		failureReason = "large deferred line index must provide a nontrivial estimate.";
		return false;
	}
	static_cast<void>(document.lineStartByIndex(estimatedLines / 2));
	if (document.exactLineCountKnown()) {
		failureReason = "a large line-number jump must not force complete synchronous indexing.";
		return false;
	}
	mr::editor::ReadSnapshot snapshot = document.readSnapshot();
	mr::editor::LineIndexWarmupData warmup;
	if (!snapshot.warmLineIndexChunk(warmup, 2) || warmup.lazyIndexedLine == 0 || warmup.lazyLineIndexComplete) {
		failureReason = "bounded line-index warmup must advance without completing the large document in one task.";
		return false;
	}
	if (!document.adoptLineIndexWarmup(warmup, document.version())) {
		failureReason = "version-matched deferred line-index progress must be adoptable.";
		return false;
	}
	failureReason.clear();
	return true;
}


bool testTruncateSpacesSaveOnlyGuard(std::string &failureReason) {
	MRTextSaveOptions options;
	const std::string trimmedText = normalizeTextForSave("abc   \n", options);
	const std::string preservedIndent = normalizeTextForSave("\t   \n", options);

	options.truncateTrailingWhitespace = true;
	if (trimmedText != "abc   \n") {
		failureReason = "Save normalization without TRUNCATE_SPACES must preserve text-line trailing spaces.";
		return false;
	}
	if (normalizeTextForSave("abc   \n", options) != "abc\n") {
		failureReason = "TRUNCATE_SPACES must trim trailing spaces on text lines during save normalization.";
		return false;
	}
	if (normalizeTextForSave("\t   \n", options) != preservedIndent) {
		failureReason = "TRUNCATE_SPACES must preserve whitespace-only indentation lines during save normalization.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testEofMarkerDoesNotExtendScrollRange(std::string &failureReason) {
	MREditSetupSettings settings = configuredEditSetupSettings();
	std::string text;
	auto maxScrollDeltaFor = [&](bool showEofMarker, int &outDeltaY) {
		MREditSetupSettings probe = settings;

		probe.showEofMarker = showEofMarker;
		probe.showEofMarkerEmoji = false;
		probe.formatRuler = false;
		probe.showLineNumbers = false;
		probe.lineNumbersPosition = "OFF";
		probe.codeFolding = false;
		probe.codeFoldingPosition = "OFF";
		probe.miniMapPosition = "OFF";
		ScopedRegressionEditSetupSettings scopedSettings(probe);
		MREditWindow window(TRect(0, 0, 80, 10), showEofMarker ? "eof-marker-scroll-on" : "eof-marker-scroll-off", 3050);
		MRFileEditor *editor = nullptr;

		if (!window.replaceTextBuffer(text.c_str(), "eof-marker-scroll")) {
			failureReason = "Unable to seed EOF marker scroll-range probe.";
			return false;
		}
		editor = window.getEditor();
		if (editor == nullptr) {
			failureReason = "EOF marker scroll-range probe did not create an editor.";
			return false;
		}
		editor->updateMetrics();
		editor->scrollTo(0, 100000);
		outDeltaY = editor->delta.y;
		return true;
	};

	for (int i = 1; i <= 24; ++i) {
		if (i > 1) text += '\n';
		text += "line ";
		text += std::to_string(i);
	}
	text += " >";

	int maxWithoutMarker = 0;
	int maxWithMarker = 0;
	if (!maxScrollDeltaFor(false, maxWithoutMarker)) return false;
	if (!maxScrollDeltaFor(true, maxWithMarker)) return false;
	if (maxWithMarker != maxWithoutMarker) {
		failureReason = "SHOW_EOF_MARKER must not extend vertical scroll range past the last document line; off=" + std::to_string(maxWithoutMarker) + " on=" + std::to_string(maxWithMarker) + ".";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testCommunicationViewerDrawDoesNotReadSettings(std::string &failureReason) {
	MREditWindow window(TRect(0, 0, 100, 20), "settings-io-communication-draw", 3051);
	MRFileEditor *editor = window.getEditor();
	TMenuBar *menuBar = createMRMenuBar(TRect(0, 0, 100, 1));
	MRMenuBar *mrMenuBar = dynamic_cast<MRMenuBar *>(menuBar);
	MRIndicator indicator(TRect(0, 0, 40, 1));
	MRDesktopBackground desktopBackground(TRect(0, 0, 100, 20));

	if (editor == nullptr) {
		failureReason = "Communication draw settings-IO guard did not create an editor.";
		delete menuBar;
		return false;
	}
	if (mrMenuBar == nullptr) {
		failureReason = "Communication draw settings-IO guard did not create an MR menu bar.";
		delete menuBar;
		return false;
	}
	editor->setCommunicationViewerMode(true, true, MRLiveLogScrollDirection::Up);
	window.setReadOnly(true);
	indicator.setDisplayValue(25, 7, False);
	mrMenuBar->setRightStatus("8:26");
	mrMenuBar->setAutoMarqueeStatus("settings io guard", MRMenuBar::MarqueeKind::Warning);

	MRSettingsRuntimeIoRateSnapshot before = settingsRuntimeIoRateSnapshot();
	for (int i = 0; i < 8; ++i)
		editor->draw();
	MRSettingsRuntimeIoRateSnapshot after = settingsRuntimeIoRateSnapshot();

	if (after.readsPerMinute != before.readsPerMinute) {
		failureReason = "Communication viewer draw must not read runtime settings; reads before=" + std::to_string(before.readsPerMinute) + " after=" + std::to_string(after.readsPerMinute) + ".";
		delete menuBar;
		return false;
	}
	before = settingsRuntimeIoRateSnapshot();
	for (int i = 0; i < 8; ++i)
		window.draw();
	after = settingsRuntimeIoRateSnapshot();
	if (after.readsPerMinute != before.readsPerMinute) {
		failureReason = "Communication window chrome draw must not read runtime settings; reads before=" + std::to_string(before.readsPerMinute) + " after=" + std::to_string(after.readsPerMinute) + ".";
		delete menuBar;
		return false;
	}
	before = settingsRuntimeIoRateSnapshot();
	for (int i = 0; i < 8; ++i)
		indicator.draw();
	after = settingsRuntimeIoRateSnapshot();
	if (after.readsPerMinute != before.readsPerMinute) {
		failureReason = "Indicator draw must not read runtime settings; reads before=" + std::to_string(before.readsPerMinute) + " after=" + std::to_string(after.readsPerMinute) + ".";
		delete menuBar;
		return false;
	}
	before = settingsRuntimeIoRateSnapshot();
	for (int i = 0; i < 8; ++i)
		menuBar->draw();
	after = settingsRuntimeIoRateSnapshot();
	if (after.readsPerMinute != before.readsPerMinute) {
		failureReason = "Menu bar draw must not read runtime settings; reads before=" + std::to_string(before.readsPerMinute) + " after=" + std::to_string(after.readsPerMinute) + ".";
		delete menuBar;
		return false;
	}
	mrRefreshVirtualDesktopSettingsSnapshot(3, false);
	before = settingsRuntimeIoRateSnapshot();
	for (int i = 0; i < 8; ++i)
		desktopBackground.draw();
	after = settingsRuntimeIoRateSnapshot();
	if (after.readsPerMinute != before.readsPerMinute) {
		failureReason = "Desktop background draw must not read runtime settings; reads before=" + std::to_string(before.readsPerMinute) + " after=" + std::to_string(after.readsPerMinute) + ".";
		delete menuBar;
		return false;
	}
	mr::messageline::setRuntimeMessageLineEnabled(true);
	mr::messageline::VisibleMessage visibleMessage;
	before = settingsRuntimeIoRateSnapshot();
	for (int i = 0; i < 8; ++i) {
		static_cast<void>(mr::messageline::currentVisibleMessage(visibleMessage));
		static_cast<void>(mr::messageline::currentOwnerMessage(mr::messageline::Owner::DialogInteraction, visibleMessage));
	}
	after = settingsRuntimeIoRateSnapshot();
	if (after.readsPerMinute != before.readsPerMinute) {
		failureReason = "Message line polling must not read runtime settings; reads before=" + std::to_string(before.readsPerMinute) + " after=" + std::to_string(after.readsPerMinute) + ".";
		delete menuBar;
		return false;
	}
	delete menuBar;
	failureReason.clear();
	return true;
}

bool testMessageLineStaticModeHarness(std::string &failureReason) {
	using namespace mr::messageline;
	auto fail = [&failureReason](const char *text) {
		setStaticMode(false);
		clearOwner(Owner::DialogInteraction);
		failureReason = text;
		return false;
	};

	setRuntimeMessageLineEnabled(true);
	setStaticMode(false);
	clearOwner(Owner::DialogInteraction);
	if (postSticky(Owner::DialogInteraction, "before static", Kind::Info, kPriorityMedium) == 0) return fail("Message line rejected an ordinary message before Static Mode.");
	VisibleMessage visible;
	if (!currentVisibleMessage(visible) || visible.text != "before static") return fail("Message line did not expose the ordinary message before Static Mode.");
	setStaticMode(true);
	if (!staticModeActive()) return fail("Static Mode semaphore was not published through the runtime K/V store.");
	if (currentVisibleMessage(visible)) return fail("Entering Static Mode did not clear the previous ordinary message.");
	if (postSticky(Owner::DialogInteraction, "discarded during static", Kind::Warning, kPriorityHigh) != 0 || currentVisibleMessage(visible)) return fail("Static Mode retained an ordinary message instead of discarding it.");
	setStaticProgress(3, 7);
	std::size_t completed = 0;
	std::size_t total = 0;
	if (!currentStaticProgress(completed, total) || completed != 3 || total != 7) return fail("Static Mode progress was not published through the runtime K/V store.");
	setStaticMode(false);
	if (staticModeActive() || currentStaticProgress(completed, total) || completed != 0 || total != 0 || currentVisibleMessage(visible)) return fail("Leaving Static Mode did not clear its state and content.");
	if (postSticky(Owner::DialogInteraction, "after static", Kind::Info, kPriorityMedium) == 0 || !currentVisibleMessage(visible) || visible.text != "after static") return fail("Message line did not resume ordinary messages after Static Mode.");
	clearOwner(Owner::DialogInteraction);
	failureReason.clear();
	return true;
}

bool testFullscreenSuspendsStaticModeWiring(std::string &failureReason) {
	const std::string editorAppPath = absolutePathFromCwd("app/MREditorApp.cpp");
	const std::string presentationPath = absolutePathFromCwd("app/MREditorAppPresentation.cpp");
	const std::string menuBarPath = absolutePathFromCwd("ui/MRMenuBar.cpp");
	const std::string menuBarHeaderPath = absolutePathFromCwd("ui/MRMenuBar.hpp");
	std::string editorApp;
	std::string presentation;
	std::string menuBar;
	std::string menuBarHeader;
	std::string ioError;
	std::string missingNeedle;

	if (!readTextFile(editorAppPath, editorApp, ioError) || !readTextFile(presentationPath, presentation, ioError) || !readTextFile(menuBarPath, menuBar, ioError) || !readTextFile(menuBarHeaderPath, menuBarHeader, ioError)) {
		failureReason = "Unable to read Fullscreen/Static Mode wiring source: " + ioError;
		return false;
	}
	const std::size_t f11Route = editorApp.find("if (event.what == evKeyDown && TKey(event.keyDown) == TKey(kbF11))");
	const std::size_t fullscreenEscapeRoute = editorApp.find("if (fullscreenPresentationActive && event.what == evKeyDown && TKey(event.keyDown) == TKey(kbEsc))");
	const std::size_t staticModeGuard = editorApp.find("if (event.what == evKeyDown && !fullscreenPresentationActive && mr::messageline::staticModeActive())");

	if (f11Route == std::string::npos || fullscreenEscapeRoute == std::string::npos || staticModeGuard == std::string::npos || fullscreenEscapeRoute > staticModeGuard) {
		failureReason = "Fullscreen Escape must precede the non-Fullscreen Static Mode function-key guard.";
		return false;
	}
	const std::size_t staticModeGuardEnd = editorApp.find("\n\tif (event.what == evKeyDown) {", staticModeGuard);
	if (staticModeGuardEnd == std::string::npos || editorApp.substr(staticModeGuard, staticModeGuardEnd - staticModeGuard).find("case kbF11:") != std::string::npos) {
		failureReason = "The non-Fullscreen Static Mode function-key guard must leave F11 available.";
		return false;
	}
	if (!containsAllSubstrings(presentation, {"mrMenuBar->setFullscreenPresentation(fullscreenActive)", "if (fullscreenActive && !fullscreenMenuBarVisible) menuBar->hide()"}, missingNeedle)) {
		failureReason = "Fullscreen layout no longer suppresses Static Mode menu projection: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(menuBarHeader, {"void setFullscreenPresentation(bool active);", "bool mFullscreenPresentation = false;"}, missingNeedle) ||
	    !containsAllSubstrings(menuBar, {"const bool staticModePresentationSuppressed = mFullscreenPresentation && mr::messageline::staticModeActive();",
	                                    "const bool staticProgressVisible = !mFullscreenPresentation && mr::messageline::currentStaticProgress", "if (staticModePresentationSuppressed)"},
	                           missingNeedle)) {
		failureReason = "Menu bar Static Mode presentation suspension changed: missing " + missingNeedle + ".";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testApplicationIdleDoesNotReadMenuSettings(std::string &failureReason) {
	const std::string appPath = absolutePathFromCwd("app/MREditorApp.cpp");
	const std::string routerPath = absolutePathFromCwd("app/MRCommandRouter.cpp");
	std::string appContent;
	std::string routerContent;
	std::string ioError;

	if (!readTextFile(appPath, appContent, ioError)) {
		failureReason = "Unable to read MREditorApp.cpp for idle settings-IO guard: " + ioError;
		return false;
	}
	if (!readTextFile(routerPath, routerContent, ioError)) {
		failureReason = "Unable to read MRCommandRouter.cpp for idle settings-IO guard: " + ioError;
		return false;
	}
	const std::string idleNeedle = "void MREditorApp::idle()";
	const std::size_t idleStart = appContent.find(idleNeedle);
	const std::size_t paletteStart = appContent.find("TPalette &MREditorApp::getPalette()", idleStart == std::string::npos ? 0 : idleStart);
	if (idleStart == std::string::npos || paletteStart == std::string::npos || paletteStart <= idleStart) {
		failureReason = "Unable to isolate MREditorApp::idle() for settings-IO guard.";
		return false;
	}
	const std::string idleBody = appContent.substr(idleStart, paletteStart - idleStart);
	if (idleBody.find("configuredCursorPositionMarker(") != std::string::npos || idleBody.find("configuredPersistentBlocksSetting(") != std::string::npos || idleBody.find("configuredMenulineMessages(") != std::string::npos || idleBody.find("configuredVirtualDesktops(") != std::string::npos || idleBody.find("configuredCyclicVirtualDesktops(") != std::string::npos) {
		failureReason = "MREditorApp::idle() must use cached UI settings, not counted settings getters.";
		return false;
	}
	if (idleBody.find("updateAppCommandState(virtualDesktopCount, cyclicVirtualDesktopsEnabled)") == std::string::npos) {
		failureReason = "MREditorApp::idle() must pass cached settings snapshots into tick helpers.";
		return false;
	}
	if (appContent.find("buildTopRightCursorStatus(const std::string &markerFormat)") == std::string::npos || appContent.find("cursorPositionMarkerFormat = configuredCursorPositionMarker();") == std::string::npos || appContent.find("persistentBlocksMenuEnabled = configuredPersistentBlocksSetting();") == std::string::npos || appContent.find("menulineMessagesEnabled = configuredMenulineMessages();") == std::string::npos || appContent.find("virtualDesktopCount = configuredVirtualDesktops();") == std::string::npos || appContent.find("cyclicVirtualDesktopsEnabled = configuredCyclicVirtualDesktops();") == std::string::npos) {
		failureReason = "App UI settings cache must own cursor marker, persistent-blocks, message-line and desktop snapshots.";
		return false;
	}
	if (appContent.find("mr::messageline::setRuntimeMessageLineEnabled(menulineMessagesEnabled);") == std::string::npos) {
		failureReason = "App UI settings cache must publish the message-line runtime snapshot.";
		return false;
	}
	if (routerContent.find("mrRefreshEditorApplicationUiSettingsSnapshot();") == std::string::npos) {
		failureReason = "Setup command handling must refresh the app UI settings cache.";
		return false;
	}
	failureReason.clear();
	return true;
}



bool runKeymapMacroBindingDispatchProbe(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	ScopedRegressionKeymap restoreKeymap;
	const std::string root = "/tmp/mr_regression_keymap_macro_dispatch_" + std::to_string(static_cast<long>(::getpid()));
	const std::string settingsPath = root + "/cfg/settings.mrmac";
	const std::string macroPath = root + "/macros";
	const std::string tempPath = root + "/tmp";
	const std::string macroFilePath = macroPath + "/actions/insert-marker.mrmac";
	const std::string macroTarget = "actions/insert-marker.mrmac^insert_marker";
	MRSettingsSnapshot cleanSettings;
	MRKeymapProfile profile;
	MRKeymapBindingRecord binding;
	std::string keymapSource;
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	if (!resetSettingsSnapshot(settingsPath, cleanSettings, &errorText)) {
		failureReason = "Unable to reset clean settings snapshot for macro-dispatch harness: " + errorText;
		return false;
	}
	cleanSettings.paths.settingsMacroUri = settingsPath;
	cleanSettings.paths.macroPath = macroPath;
	cleanSettings.paths.helpUri = absolutePathFromCwd("mr.hlp");
	cleanSettings.paths.tempPath = tempPath;
	cleanSettings.paths.shellUri = "/bin/sh";

	if (!ensureDirectoryTree(root + "/cfg", &errorText) || !ensureDirectoryTree(macroPath + "/actions", &errorText) || !ensureDirectoryTree(tempPath, &errorText)) {
		failureReason = "Unable to create macro-dispatch harness directories: " + errorText;
		return false;
	}
	if (!writeTextFile(settingsPath, buildSettingsMacroSource(cleanSettings))) {
		failureReason = "Unable to write clean settings.mrmac for macro-dispatch harness.";
		return false;
	}
	if (!setConfiguredSettingsMacroFilePath(settingsPath, &errorText)) {
		failureReason = "Unable to configure settings path for macro-dispatch harness: " + errorText;
		return false;
	}
	if (!setConfiguredMacroDirectoryPath(macroPath, &errorText)) {
		failureReason = "Unable to configure macro directory for macro-dispatch harness: " + errorText;
		return false;
	}
	if (!writeTextFile(macroFilePath, "$MACRO insert_marker;\nTEXT('!');\nEND_MACRO;\n")) {
		restore();
		failureReason = "Unable to write macro target file for macro-dispatch harness.";
		return false;
	}

	profile.name = "MACRO_DISPATCH";
	profile.description = "Regression macro dispatch";
	binding.profileName = profile.name;
	binding.context = MRKeymapContext::Edit;
	binding.target.type = MRKeymapBindingType::Macro;
	binding.target.target = macroTarget;
	binding.sequence = *MRKeymapSequence::parse("<F12>");
	binding.description = "Insert marker";
	profile.bindings.push_back(binding);
	keymapSource = serializeKeymapProfilesToSettingsSource(std::vector<MRKeymapProfile>{profile}, profile.name);

	if (ensureRegressionEditorApp(failureReason) == nullptr) {
		restore();
		return false;
	}
	if (!testBlockMarkingWindowInputHarness(failureReason)) {
		restore();
		return false;
	}
	{
		MREditWindow *window = nullptr;
		MRFileEditor *editor = nullptr;

		if (!installRegressionKeymap(keymapSource, failureReason)) {
			restore();
			return false;
		}
		window = createEditorWindow("keymap-macro-dispatch");
		if (window == nullptr) {
			restore();
			failureReason = "Macro-dispatch harness could not create an editor window.";
			return false;
		}
		if (!mrActivateEditWindow(window)) {
			destroyRegressionWindow(window);
			restore();
			failureReason = "Macro-dispatch harness could not activate the editor window.";
			return false;
		}
		if (!window->replaceTextBuffer("abc\n", "keymap-macro-dispatch")) {
			destroyRegressionWindow(window);
			restore();
			failureReason = "Macro-dispatch harness could not seed editor text.";
			return false;
		}
		editor = window->getEditor();
		if (editor == nullptr) {
			destroyRegressionWindow(window);
			restore();
			failureReason = "Macro-dispatch harness window has no editor.";
			return false;
		}
		editor->setCursorOffset(0);
		if (!sendWindowKey(*window, kbF12)) {
			destroyRegressionWindow(window);
			restore();
			failureReason = "Macro-dispatch harness could not send the bound key.";
			return false;
		}
		if (editor->snapshotText() != "!abc\n") {
			destroyRegressionWindow(window);
			restore();
			failureReason = "Runtime macro binding must dispatch through the editor key path and mutate the active buffer.";
			return false;
		}
		destroyRegressionWindow(window);
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after macro-dispatch harness: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testKeymapMacroBindingDispatchHarness(std::string &failureReason) {
	return runRegressionProbeProcess("keymap-macro-dispatch", failureReason);
}


bool testKeymapMacroBindingNegativeDiagnosticsHarness(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	const std::string root = "/tmp/mr_regression_keymap_macro_diagnostics_" + std::to_string(static_cast<long>(::getpid()));
	const std::string macroPath = root + "/macros";
	const std::string presentMacroFilePath = macroPath + "/present.mrmac";
	MRKeymapProfile profile;
	MRKeymapBindingRecord binding;
	std::string source;
	std::string errorText;
	std::string restoreError;
	bool restored = false;
	bool missingFileError = false;
	bool missingMacroNameError = false;
	bool missingMacroDefinitionError = false;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	if (!ensureDirectoryTree(macroPath, &errorText)) {
		failureReason = "Unable to create macro-diagnostics harness directory: " + errorText;
		return false;
	}
	if (!setConfiguredMacroDirectoryPath(macroPath, &errorText)) {
		failureReason = "Unable to configure macro directory for macro-diagnostics harness: " + errorText;
		return false;
	}
	if (!writeTextFile(presentMacroFilePath, "$MACRO existing_macro;\nTEXT('x');\nEND_MACRO;\n")) {
		restore();
		failureReason = "Unable to write present macro file for diagnostics harness.";
		return false;
	}

	profile.name = "MACRO_DIAGNOSTICS";
	profile.description = "Regression macro diagnostics";
	binding.profileName = profile.name;
	binding.context = MRKeymapContext::Edit;
	binding.target.type = MRKeymapBindingType::Macro;
	binding.description = "Broken macro";

	binding.target.target = "missing-file.mrmac^run_missing";
	binding.sequence = *MRKeymapSequence::parse("<F5>");
	profile.bindings.push_back(binding);
	binding.target.target = "present.mrmac^";
	binding.sequence = *MRKeymapSequence::parse("<F6>");
	profile.bindings.push_back(binding);
	binding.target.target = "present.mrmac^missing_macro";
	binding.sequence = *MRKeymapSequence::parse("<F7>");
	profile.bindings.push_back(binding);

	source = serializeKeymapProfilesToSettingsSource(std::vector<MRKeymapProfile>{profile}, profile.name);

	{
		MRKeymapLoadResult loaded = loadKeymapProfilesFromSettingsSource(source);
		for (const MRKeymapDiagnostic &diagnostic : loaded.diagnostics) {
			if (diagnostic.severity != MRKeymapDiagnosticSeverity::Error) continue;
			if (diagnostic.message.find("Macro file could not be resolved.") != std::string::npos) missingFileError = true;
			if (diagnostic.message.find("Macro target has no macro name.") != std::string::npos) missingMacroNameError = true;
			if (diagnostic.message.find("Macro not found in file.") != std::string::npos) missingMacroDefinitionError = true;
		}
	}

	if (!missingFileError || !missingMacroNameError || !missingMacroDefinitionError) {
		restore();
		failureReason = "Keymap macro validation must report unresolved files, missing macro names and absent macro definitions.";
		return false;
	}
	if (!restore()) {
		failureReason = "Unable to restore runtime settings after macro-diagnostics harness: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testEditProfileDirectApiValidationGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	MREditExtensionProfile profile;
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	profile.id = "broken_profile";
	profile.name = "Broken";
	profile.extensions.push_back("txt");
	profile.overrides.mask = kOvTabSize;
	profile.overrides.values.tabSize = 0;

	if (setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(1, profile), &errorText)) {
		restore();
		failureReason = "Direct profile API accepted invalid TAB_SIZE override.";
		return false;
	}
	if (errorText.find("TAB_SIZE") == std::string::npos) {
		restore();
		failureReason = "Invalid profile override should report the normalized TAB_SIZE validation error.";
		return false;
	}
	if (!restore()) {
		failureReason = "Unable to restore runtime settings after invalid profile API probe: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}


bool testEditProfileCaseSensitiveExtensionMatchGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	MREditSetupSettings globalSettings = resolveEditSetupDefaults();
	MREditExtensionProfile lowerProfile;
	MREditExtensionProfile upperProfile;
	std::string errorText;
	std::string restoreError;
	bool restored = false;
	MREditSetupSettings effective;
	std::string matchedProfile;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	globalSettings.tabSize = 8;
	if (!setConfiguredEditSetupSettings(globalSettings, &errorText)) {
		restore();
		failureReason = "Unable to seed globals for case-sensitive extension probe: " + errorText;
		return false;
	}

	lowerProfile.id = "c_lower";
	lowerProfile.name = "Lower C";
	lowerProfile.extensions.push_back("c");
	lowerProfile.overrides.values = resolveEditSetupDefaults();
	lowerProfile.overrides.values.tabSize = 2;
	lowerProfile.overrides.mask = kOvTabSize;

	upperProfile.id = "c_upper";
	upperProfile.name = "Upper C";
	upperProfile.extensions.push_back("C");
	upperProfile.overrides.values = resolveEditSetupDefaults();
	upperProfile.overrides.values.tabSize = 6;
	upperProfile.overrides.mask = kOvTabSize;

	if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>{lowerProfile, upperProfile}, &errorText)) {
		restore();
		failureReason = "Unable to seed case-sensitive extension profiles: " + errorText;
		return false;
	}

	if (!effectiveEditSetupSettingsForPath("/tmp/example.c", effective, &matchedProfile)) {
		restore();
		failureReason = "Effective profile lookup failed for .c.";
		return false;
	}
	if (matchedProfile != "Lower C" || effective.tabSize != 2) {
		restore();
		failureReason = "Lower-case extension did not resolve to the exact lower-case profile.";
		return false;
	}

	if (!effectiveEditSetupSettingsForPath("/tmp/example.C", effective, &matchedProfile)) {
		restore();
		failureReason = "Effective profile lookup failed for .C.";
		return false;
	}
	if (matchedProfile != "Upper C" || effective.tabSize != 6) {
		restore();
		failureReason = "Upper-case extension did not resolve to the exact upper-case profile.";
		return false;
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after case-sensitive extension probe: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testEffectiveCProfileControlsLoadedEditorGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	MREditSetupSettings globalSettings = resolveEditSetupDefaults();
	MREditExtensionProfile cProfile;
	const std::string path = "/tmp/mr_regression_effective_c_profile_" + std::to_string(static_cast<long>(::getpid())) + ".c";
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		::unlink(path.c_str());
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	globalSettings.formatRuler = true;
	globalSettings.codeLanguage = "AUTO";
	if (!setConfiguredEditSetupSettings(globalSettings, &errorText)) {
		restore();
		failureReason = "Unable to seed globals for loaded editor profile probe: " + errorText;
		return false;
	}

	cProfile.id = "c_runtime_profile";
	cProfile.name = "C Runtime Profile";
	cProfile.extensions.push_back("c");
	cProfile.overrides.values = resolveEditSetupDefaults();
	cProfile.overrides.values.formatRuler = false;
	cProfile.overrides.values.codeLanguage = "C";
	cProfile.overrides.mask = kOvFormatRuler | kOvCodeLanguage;
	if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(1, cProfile), &errorText)) {
		restore();
		failureReason = "Unable to seed C extension profile for loaded editor probe: " + errorText;
		return false;
	}

	{
		std::ofstream file(path.c_str(), std::ios::out | std::ios::trunc);
		if (!file) {
			restore();
			failureReason = "Unable to create temporary C file for loaded editor profile probe.";
			return false;
		}
		file << "#include <stdio.h>\n\nint main(void) {\n\treturn 0;\n}\n";
	}

	{
		MREditWindow window(TRect(0, 0, 80, 16), "effective-c-profile", 1031);
		MRFileEditor *editor = nullptr;

		if (!window.loadFromFile(path.c_str())) {
			restore();
			failureReason = "Unable to load temporary C file for effective profile probe.";
			return false;
		}
		editor = window.getEditor();
		if (editor == nullptr) {
			restore();
			failureReason = "Loaded editor profile probe has no editor.";
			return false;
		}
		editor->updateMetrics();
		if (window.syntaxLanguage() != MRSyntaxLanguage::C || editor->syntaxLanguage() != MRSyntaxLanguage::C) {
			restore();
			failureReason = "Loaded .c file must activate the C syntax profile.";
			return false;
		}
		if (editor->visibleViewportRows() != editor->size.y) {
			restore();
			failureReason = "C profile FORMAT_RULER=false must leave all editor rows available for text.";
			return false;
		}
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after loaded editor profile probe: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}




struct ProfileConformanceProbeValue {
	const char *key;
	const char *value;
};

static const ProfileConformanceProbeValue kProfileConformanceProbeValues[] = {
    {"PAGE_BREAK", "\\n"},
    {"WORD_DELIMITERS", "_:"},
    {"DEFAULT_EXTENSIONS", "qsprof;mrq"},
    {"TRUNCATE_SPACES", "false"},
    {"EOF_CTRL_Z", "true"},
    {"EOF_CR_LF", "true"},
    {"TAB_EXPAND", "false"},
    {"DISPLAY_TABS", "true"},
    {"TAB_SIZE", "4"},
    {"LEFT_MARGIN", "2"},
    {"RIGHT_MARGIN", "96"},
    {"FORMAT_RULER", "true"},
    {"WORD_WRAP", "false"},
    {"INDENT_STYLE", "SMART"},
    {"CODE_LANGUAGE", "C"},
    {"CODE_COLORING", "true"},
    {"FILE_TYPE", "BINARY"},
    {"BINARY_RECORD_LENGTH", "256"},
    {"POST_LOAD_MACRO", "/tmp/mr_regression_post_load.mrmac"},
    {"PRE_SAVE_MACRO", "/tmp/mr_regression_pre_save.mrmac"},
    {"DEFAULT_PATH", "/tmp"},
    {"FORMAT_LINE", "L..|..R"},
    {"BACKUP_FILES", "false"},
    {"SHOW_EOF_MARKER", "true"},
    {"SHOW_EOF_MARKER_EMOJI", "false"},
    {"LINE_NUMBERS_POSITION", "TRAILING"},
    {"LINE_NUM_ZERO_FILL", "true"},
    {"MINIMAP_POSITION", "LEADING"},
    {"MINIMAP_WIDTH", "7"},
    {"MINIMAP_MARKER_GLYPH", "|"},
    {"GUTTERS", "MCL"},
    {"PERSISTENT_BLOCKS", "false"},
    {"CODE_FOLDING_POSITION", "LEADING"},
    {"BLOCK_MOVE", "LEAVE_SPACE"},
    {"DEFAULT_MODE", "OVERWRITE"},
    {"CURSOR_STATUS_COLOR", "5E"},
};

const char *profileConformanceProbeValueForKey(const char *key) {
	for (std::size_t i = 0; i < sizeof(kProfileConformanceProbeValues) / sizeof(kProfileConformanceProbeValues[0]); ++i)
		if (std::strcmp(kProfileConformanceProbeValues[i].key, key) == 0) return kProfileConformanceProbeValues[i].value;
	return nullptr;
}



struct CodeLanguageConformanceEntry {
	const char *settingValue;
	const char *extension;
	const char *text;
	MRSyntaxLanguage language;
	const char *marker;
	bool automatic;
};

static const CodeLanguageConformanceEntry kCodeLanguageConformanceEntries[] = {
	{"NONE", "txtnone", "plain text\n", MRSyntaxLanguage::PlainText, "", false},
	{"AUTO", "c", "#include <stdio.h>\nint main(void) { return 0; }\n", MRSyntaxLanguage::C, "C", true},
	{"C", "langc", "int main(void) { return 0; }\n", MRSyntaxLanguage::C, "C", false},
	{"CPP", "langcpp", "class Probe { public: int value; };\n", MRSyntaxLanguage::Cpp, "C++", false},
	{"PYTHON", "langpython", "def probe():\n    return 1\n", MRSyntaxLanguage::Python, "Py", false},
	{"JAVASCRIPT", "langjavascript", "function probe() { return 1; }\n", MRSyntaxLanguage::JavaScript, "JS", false},
	{"TYPESCRIPT", "langtypescript", "function probe(value: number): number { return value; }\n", MRSyntaxLanguage::JavaScript, "JS", false},
	{"TSX", "langtsx", "const probe = <div />;\n", MRSyntaxLanguage::JavaScript, "JS", false},
	{"BASH", "langbash", "if true; then echo ok; fi\n", MRSyntaxLanguage::Bash, "Ba", false},
	{"ZSH", "langzsh", "if true; then echo ok; fi\n", MRSyntaxLanguage::Zsh, "Zh", false},
	{"FISH", "langfish", "if true\n    echo ok\nend\n", MRSyntaxLanguage::Fish, "Fi", false},
	{"JSON", "langjson", "{\"probe\": true}\n", MRSyntaxLanguage::Json, "Jn", false},
	{"YAML", "langyaml", "probe: true\n", MRSyntaxLanguage::Yaml, "Ya", false},
	{"XML", "langxml", "<probe />\n", MRSyntaxLanguage::Xml, "Xm", false},
	{"PERL", "langperl", "sub probe { return 1; }\n", MRSyntaxLanguage::Perl, "Pl", false},
	{"SWIFT", "langswift", "func probe() -> Int { return 1 }\n", MRSyntaxLanguage::Swift, "Sw", false},
	{"RUST", "langrust", "fn probe() -> i32 { 1 }\n", MRSyntaxLanguage::Rust, "Rs", false},
	{"GO", "langgo", "func probe() int { return 1 }\n", MRSyntaxLanguage::Go, "Go", false},
	{"PASCAL", "langpascal", "begin\nend.\n", MRSyntaxLanguage::Pascal, "Pa", false},
	{"BASIC", "langbasic", "FUNCTION Probe()\n    Probe = 1\nEND FUNCTION\n", MRSyntaxLanguage::Basic, "BAS", false},
	{"SYSTEMD", "langsystemd", "[Unit]\nDescription=Probe\n", MRSyntaxLanguage::Systemd, "Sd", false},
	{"MAKE", "langmake", "all:\n\t@echo ok\n", MRSyntaxLanguage::Make, "MK", false},
	{"MRMAC", "langmrmac", "$MACRO PROBE;\nEND_MACRO;\n", MRSyntaxLanguage::MRMAC, "MR", false},
	{"MARKDOWN", "langmarkdown", "# Probe\n", MRSyntaxLanguage::Markdown, "MD", false},
	{"LATEX", "langlatex", "\\documentclass{article}\n\\begin{document}\nProbe\n\\end{document}\n", MRSyntaxLanguage::Latex, "TeX", false},
	{"KOTLIN", "langkotlin", "fun probe(): Int = 1\n", MRSyntaxLanguage::Kotlin, "Kt", false},
	{"CSHARP", "langcsharp", "class Probe { int Value() { return 1; } }\n", MRSyntaxLanguage::CSharp, "C#", false},
};

bool testEditProfileCodeLanguageRasterGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	MREditSetupSettings globalSettings = resolveEditSetupDefaults();
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	globalSettings.codeLanguage = "NONE";
	if (!setConfiguredEditSetupSettings(globalSettings, &errorText)) {
		restore();
		failureReason = "Unable to seed globals for CODE_LANGUAGE raster: " + errorText;
		return false;
	}
	clearConfiguredSettingsDirty();
	if (!applyConfiguredEditSetupValue("CODE_LANGUAGE", " cpp ", &errorText) || configuredEditSetupSettings().codeLanguage != "CPP") {
		restore();
		failureReason = "Valid CODE_LANGUAGE assignment was not normalized to CPP: " + errorText;
		return false;
	}
	if (!configuredSettingsDirty()) {
		restore();
		failureReason = "Changed CODE_LANGUAGE assignment did not mark settings dirty.";
		return false;
	}
	clearConfiguredSettingsDirty();
	if (!applyConfiguredEditSetupValue("CODE_LANGUAGE", "CPP", &errorText)) {
		restore();
		failureReason = "Repeated CODE_LANGUAGE assignment failed: " + errorText;
		return false;
	}
	if (configuredSettingsDirty()) {
		restore();
		failureReason = "Repeated CODE_LANGUAGE assignment marked unchanged settings dirty.";
		return false;
	}
	if (applyConfiguredEditSetupValue("CODE_LANGUAGE", "NOT_A_LANGUAGE", &errorText)) {
		restore();
		failureReason = "Invalid CODE_LANGUAGE assignment was accepted.";
		return false;
	}
	if (configuredEditSetupSettings().codeLanguage != "CPP" || configuredSettingsDirty()) {
		restore();
		failureReason = "Invalid CODE_LANGUAGE assignment changed runtime state or dirty gating.";
		return false;
	}

	for (std::size_t i = 0; i < sizeof(kCodeLanguageConformanceEntries) / sizeof(kCodeLanguageConformanceEntries[0]); ++i) {
		const CodeLanguageConformanceEntry &entry = kCodeLanguageConformanceEntries[i];
		MREditExtensionProfile profile;
		MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, "");
		const std::string path = std::string("/tmp/mr_regression_code_language_") + entry.settingValue + "." + entry.extension;

		profile.id = std::string("language_") + entry.extension;
		profile.name = std::string("Language ") + entry.settingValue;
		profile.extensions.push_back(entry.extension);
		profile.overrides.values = resolveEditSetupDefaults();
		profile.overrides.values.codeLanguage = entry.settingValue;
		profile.overrides.mask = kOvCodeLanguage;
		if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(1, profile), &errorText)) {
			restore();
			failureReason = std::string("Unable to seed CODE_LANGUAGE profile for ") + entry.settingValue + ": " + errorText;
			return false;
		}
		if (!editor.replaceBufferText(entry.text)) {
			restore();
			failureReason = std::string("Unable to seed editor text for CODE_LANGUAGE ") + entry.settingValue;
			return false;
		}
		editor.setPersistentFileName(path.c_str());
		if (editor.syntaxLanguage() != entry.language) {
			restore();
			failureReason = std::string("CODE_LANGUAGE ") + entry.settingValue + " mapped to unexpected syntax language " + editor.syntaxLanguageName();
			return false;
		}
		if (editor.syntaxLanguageAutomatic() != entry.automatic) {
			restore();
			failureReason = std::string("CODE_LANGUAGE ") + entry.settingValue + " did not preserve automatic-language state.";
			return false;
		}
		if (std::strcmp(tmrSyntaxLanguageMarker(editor.syntaxLanguage()), entry.marker) != 0) {
			restore();
			failureReason = std::string("CODE_LANGUAGE ") + entry.settingValue + " exposed unexpected window marker.";
			return false;
		}
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after CODE_LANGUAGE raster: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}


struct ColorGroupConformanceEntry {
	MRColorSetupGroup group;
	const char *key;
	std::size_t expectedCount;
};

static const ColorGroupConformanceEntry kColorGroupConformanceEntries[] = {
    {MRColorSetupGroup::Window, "WINDOWCOLORS", MRColorSetupSettings::kWindowCount},
    {MRColorSetupGroup::MenuDialog, "MENUDIALOGCOLORS", MRColorSetupSettings::kMenuDialogCount},
    {MRColorSetupGroup::Help, "HELPCOLORS", MRColorSetupSettings::kHelpCount},
    {MRColorSetupGroup::Other, "OTHERCOLORS", MRColorSetupSettings::kOtherCount},
    {MRColorSetupGroup::MiniMap, "MINIMAPCOLORS", MRColorSetupSettings::kMiniMapCount},
    {MRColorSetupGroup::FileCompareMiniMap, "FILECOMPAREMINIMAPCOLORS", MRColorSetupSettings::kFileCompareMiniMapCount},
    {MRColorSetupGroup::Code, "CODECOLORS", MRColorSetupSettings::kCodeCount},
    {MRColorSetupGroup::FileCompare, "FILECOMPARECOLORS", MRColorSetupSettings::kFileCompareCount},
    {MRColorSetupGroup::Debugger, "DEBUGGERCOLORS", MRColorSetupSettings::kDebuggerCount},
};

struct ColorGroupAliasEntry {
	MRColorSetupGroup group;
	const char *firstName;
	const char *secondName;
};

static const ColorGroupAliasEntry kAllowedColorGroupAliases[] = {
    {MRColorSetupGroup::Help, "Help-Text", "help-attr-1"},
    {MRColorSetupGroup::Help, "help-Highlight", "help-Link"},
    {MRColorSetupGroup::Help, "help-Highlight", "help-attr-2"},
    {MRColorSetupGroup::Help, "help-Link", "help-attr-2"},
    {MRColorSetupGroup::Help, "help-Chapter", "help-F-keys"},
    {MRColorSetupGroup::Help, "help-Chapter", "help-attr-3"},
    {MRColorSetupGroup::Help, "help-F-keys", "help-attr-3"},
};

bool colorGroupAliasAllowed(MRColorSetupGroup group, const char *firstName, const char *secondName) {
	for (std::size_t i = 0; i < sizeof(kAllowedColorGroupAliases) / sizeof(kAllowedColorGroupAliases[0]); ++i) {
		const ColorGroupAliasEntry &entry = kAllowedColorGroupAliases[i];

		if (entry.group != group) continue;
		if (std::strcmp(entry.firstName, firstName) == 0 && std::strcmp(entry.secondName, secondName) == 0) return true;
		if (std::strcmp(entry.firstName, secondName) == 0 && std::strcmp(entry.secondName, firstName) == 0) return true;
	}
	return false;
}

bool colorGroupValueAt(const MRColorSetupSettings &settings, MRColorSetupGroup group, std::size_t index, MRRgbColorAttribute &outValue) {
	switch (group) {
		case MRColorSetupGroup::Window:
			if (index >= settings.windowColors.size()) return false;
			outValue = settings.windowColors[index];
			return true;
		case MRColorSetupGroup::MenuDialog:
			if (index >= settings.menuDialogColors.size()) return false;
			outValue = settings.menuDialogColors[index];
			return true;
		case MRColorSetupGroup::Help:
			if (index >= settings.helpColors.size()) return false;
			outValue = settings.helpColors[index];
			return true;
		case MRColorSetupGroup::Other:
			if (index >= settings.otherColors.size()) return false;
			outValue = settings.otherColors[index];
			return true;
		case MRColorSetupGroup::MiniMap:
			if (index >= settings.miniMapColors.size()) return false;
			outValue = settings.miniMapColors[index];
			return true;
		case MRColorSetupGroup::FileCompareMiniMap:
			if (index >= settings.fileCompareMiniMapColors.size()) return false;
			outValue = settings.fileCompareMiniMapColors[index];
			return true;
		case MRColorSetupGroup::Code:
			if (index >= settings.codeColors.size()) return false;
			outValue = settings.codeColors[index];
			return true;
		case MRColorSetupGroup::FileCompare:
			if (index >= settings.fileCompareColors.size()) return false;
			outValue = settings.fileCompareColors[index];
			return true;
		case MRColorSetupGroup::Debugger:
			if (index >= settings.debuggerColors.size()) return false;
			outValue = settings.debuggerColors[index];
			return true;
	}
	return false;
}

bool restoreColorGroupValues(MRColorSetupGroup group, const MRColorSetupSettings &settings, std::string &errorText) {
	switch (group) {
		case MRColorSetupGroup::Window:
			return setConfiguredColorSetupGroupValues(group, settings.windowColors.data(), settings.windowColors.size(), &errorText);
		case MRColorSetupGroup::MenuDialog:
			return setConfiguredColorSetupGroupValues(group, settings.menuDialogColors.data(), settings.menuDialogColors.size(), &errorText);
		case MRColorSetupGroup::Help:
			return setConfiguredColorSetupGroupValues(group, settings.helpColors.data(), settings.helpColors.size(), &errorText);
		case MRColorSetupGroup::Other:
			return setConfiguredColorSetupGroupValues(group, settings.otherColors.data(), settings.otherColors.size(), &errorText);
		case MRColorSetupGroup::MiniMap:
			return setConfiguredColorSetupGroupValues(group, settings.miniMapColors.data(), settings.miniMapColors.size(), &errorText);
		case MRColorSetupGroup::FileCompareMiniMap:
			return setConfiguredColorSetupGroupValues(group, settings.fileCompareMiniMapColors.data(), settings.fileCompareMiniMapColors.size(), &errorText);
		case MRColorSetupGroup::Code:
			return setConfiguredColorSetupGroupValues(group, settings.codeColors.data(), settings.codeColors.size(), &errorText);
		case MRColorSetupGroup::FileCompare:
			return setConfiguredColorSetupGroupValues(group, settings.fileCompareColors.data(), settings.fileCompareColors.size(), &errorText);
		case MRColorSetupGroup::Debugger:
			return setConfiguredColorSetupGroupValues(group, settings.debuggerColors.data(), settings.debuggerColors.size(), &errorText);
	}
	errorText = "Unknown color group.";
	return false;
}

bool testColorThemeInventoryConformanceGuard(std::string &failureReason) {
	MRColorSetupSettings previous = configuredColorSetupSettings();
	MRColorSetupSettings defaults = resolveColorSetupDefaults();
	std::string source = buildColorThemeMacroSource(defaults);
	std::string colorSetupDialogSource;
	std::string errorText;
	std::string restoreError;
	bool restored = true;

	auto restore = [&]() {
		for (std::size_t i = 0; i < sizeof(kColorGroupConformanceEntries) / sizeof(kColorGroupConformanceEntries[0]); ++i)
			if (!restoreColorGroupValues(kColorGroupConformanceEntries[i].group, previous, restoreError)) restored = false;
	};

	for (std::size_t groupIndex = 0; groupIndex < sizeof(kColorGroupConformanceEntries) / sizeof(kColorGroupConformanceEntries[0]); ++groupIndex) {
		const ColorGroupConformanceEntry &entry = kColorGroupConformanceEntries[groupIndex];
		const MRColorSetupItem *items = nullptr;
		std::size_t count = 0;
		std::vector<MRRgbColorAttribute> probeValues;
		TColorAttr overrideValue;

		if (std::strcmp(colorSetupGroupKey(entry.group), entry.key) != 0) {
			failureReason = std::string("Color setup group key mismatch for ") + entry.key;
			return false;
		}
		if (colorSetupGroupTitle(entry.group) == nullptr || colorSetupGroupTitle(entry.group)[0] == '\0') {
			failureReason = std::string("Color setup group has no visible title: ") + entry.key;
			return false;
		}
		if (source.find(std::string(entry.key) + "('") == std::string::npos) {
			failureReason = std::string("Theme serializer did not emit color group ") + entry.key;
			return false;
		}

		items = colorSetupGroupItems(entry.group, count);
		if (items == nullptr || count != entry.expectedCount) {
			failureReason = std::string("Color setup item count mismatch for ") + entry.key;
			return false;
		}
		probeValues.resize(count);
		for (std::size_t i = 0; i < count; ++i) {
			bool duplicateSlot = false;

			if (items[i].label == nullptr || items[i].label[0] == '\0') {
				failureReason = std::string("Color setup item without name in ") + entry.key;
				return false;
			}
			for (std::size_t j = 0; j < i; ++j)
				if (items[j].paletteIndex == items[i].paletteIndex) {
					if (!colorGroupAliasAllowed(entry.group, items[j].label, items[i].label)) {
						failureReason = std::string("Undocumented color setup alias in ") + entry.key + ": " + items[j].label + " / " + items[i].label;
						return false;
					}
					probeValues[i] = probeValues[j];
					duplicateSlot = true;
					break;
				}
			if (!duplicateSlot)
				probeValues[i] = MRRgbColorAttribute{static_cast<std::uint32_t>(0x102030u + groupIndex * 0x100u + i), static_cast<std::uint32_t>(0x405060u + groupIndex * 0x100u + i)};
		}

		if (!setConfiguredColorSetupGroupValues(entry.group, probeValues.data(), probeValues.size(), &errorText)) {
			restore();
			failureReason = std::string("Unable to set probe values for ") + entry.key + ": " + errorText;
			return false;
		}
		for (std::size_t i = 0; i < count; ++i) {
			MRRgbColorAttribute runtimeValue;

			if (!colorGroupValueAt(configuredColorSetupSettings(), entry.group, i, runtimeValue) || runtimeValue != probeValues[i]) {
				restore();
				failureReason = std::string("Runtime color setup value mismatch for ") + entry.key;
				return false;
			}
			if (!colorSlotOverride(configuredColorSetupSettings(), items[i].paletteIndex, MRColorOutputMode::RgbAutomatic, overrideValue)) {
				restore();
				failureReason = std::string("configuredColorSlotOverride does not expose ") + entry.key + " slot " + items[i].label;
				return false;
			}
			if (overrideValue != projectColorAttribute(probeValues[i], MRColorOutputMode::RgbAutomatic)) {
				restore();
				failureReason = std::string("Color slot override mismatch for ") + entry.key + " slot " + items[i].label;
				return false;
			}
		}
	}

	if (!readTextFile(absolutePathFromCwd("dialogs/MRColorSetup.cpp"), colorSetupDialogSource, errorText)) {
		restore();
		failureReason = "Unable to read Color Setup dialog source for inventory guard: " + errorText;
		return false;
	}
	if (colorSetupDialogSource.find("MRColorSetupGroup::Debugger") == std::string::npos) {
		restore();
		failureReason = "Color Setup dialog inventory must expose DEBUGGERCOLORS.";
		return false;
	}

	restore();
	if (!restored) {
		failureReason = "Unable to restore color setup after inventory conformance: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}


struct InvalidCurrentColorListEntry {
	const char *key;
	const char *value;
};

static const InvalidCurrentColorListEntry kInvalidCurrentColorListEntries[] = {
	{"WINDOWCOLORS", "rgb24:102030/405060"},
	{"MENUDIALOGCOLORS", "rgb24:102030/405060"},
	{"HELPCOLORS", "rgb24:102030/405060"},
	{"OTHERCOLORS", "rgb24:102030/405060"},
	{"MINIMAPCOLORS", "rgb24:102030/405060"},
	{"FILECOMPAREMINIMAPCOLORS", "rgb24:102030/405060"},
	{"CODECOLORS", "rgb24:102030/405060"},
	{"FILECOMPARECOLORS", "rgb24:102030/405060"},
	{"DEBUGGERCOLORS", "rgb24:102030/405060"},
};

bool testCurrentColorThemeInvalidListsDoNotMutateGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	for (std::size_t i = 0; i < sizeof(kInvalidCurrentColorListEntries) / sizeof(kInvalidCurrentColorListEntries[0]); ++i) {
		const InvalidCurrentColorListEntry &entry = kInvalidCurrentColorListEntries[i];

		errorText.clear();
		if (applyConfiguredColorSetupValue(entry.key, entry.value, &errorText)) {
			restore();
			failureReason = std::string("Invalid current color list was accepted for ") + entry.key;
			return false;
		}
		if (errorText.empty()) {
			restore();
			failureReason = std::string("Invalid current color list did not report an error for ") + entry.key;
			return false;
			}
			MRColorSetupSettings current = configuredColorSetupSettings();
			if (current != previous) {
				restore();
				failureReason = std::string("Invalid current color list mutated runtime colors for ") + entry.key;
				return false;
			}
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after invalid color list probe: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testWindowColorsThemeVersionAndLineNumbersRoundtrip(std::string &failureReason) {
	const std::string themePath = "/tmp/mr-windowcolors-line-numbers-theme.mrmac";
	const std::string windowColorsPrefix = "WINDOWCOLORS('rgb24:";
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	std::array<MRRgbColorAttribute, MRColorSetupSettings::kWindowCount> probeValues{};
	std::string errorText;
	std::string content;
	std::string contentAfterLoad;
	std::vector<unsigned char> themeBytecode;
	std::string themeMacroName;
	std::string themeCompileError;
	int themeEntryOffset = -1;
	TColorAttr slotValue;
	bool restored = false;
	for (std::size_t i = 0; i < probeValues.size(); ++i) probeValues[i] = MRRgbColorAttribute{static_cast<std::uint32_t>(0x123400u + i), static_cast<std::uint32_t>(0x567800u + i)};

	auto restore = [&]() {
		std::string restoreError;
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		std::remove(themePath.c_str());
	};

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, probeValues.data(), probeValues.size(), &errorText)) {
		failureReason = "Unable to seed WINDOWCOLORS probe values: " + errorText;
		restore();
		return false;
	}
	if (!writeColorThemeFile(themePath, &errorText)) {
		failureReason = "Unable to write color theme for WINDOWCOLORS probe: " + errorText;
		restore();
		return false;
	}
	if (!readTextFile(themePath, content, errorText)) {
		failureReason = "Unable to read color theme file after write: " + errorText;
		restore();
		return false;
	}
	if (content.find(windowColorsPrefix) == std::string::npos) {
		failureReason = "Saved theme must serialize WINDOWCOLORS using canonical RGB24 list format.";
		restore();
		return false;
	}
	if (!compileSource(content, themeBytecode, themeEntryOffset, themeMacroName, themeCompileError)) {
		failureReason = "Saved RGB24 theme must compile as MRMAC: " + themeCompileError;
		restore();
		return false;
	}

	{
		MRColorSetupSettings defaults = resolveColorSetupDefaults();
		if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, defaults.windowColors.data(), defaults.windowColors.size(), &errorText)) {
			failureReason = "Unable to reset WINDOWCOLORS before reload probe: " + errorText;
			restore();
			return false;
		}
	}
	if (!loadColorThemeFile(themePath, &errorText)) {
		failureReason = "Unable to load written color theme for WINDOWCOLORS probe: " + errorText;
		restore();
		return false;
	}
	if (!readTextFile(themePath, contentAfterLoad, errorText)) {
		failureReason = "Unable to read color theme file after reload: " + errorText;
		restore();
		return false;
	}
	if (contentAfterLoad != content) {
		failureReason = "Loading a current complete color theme must not rewrite the theme file.";
		restore();
		return false;
	}

	{
		MRColorSetupSettings loaded = configuredColorSetupSettings();
		for (std::size_t i = 0; i < probeValues.size(); ++i)
			if (loaded.windowColors[i] != probeValues[i]) {
				std::ostringstream out;
					out << "WINDOWCOLORS RGB24 roundtrip mismatch after theme reload at " << i;
				failureReason = out.str();
				restore();
				return false;
			}
	}
	MRColorSetupSettings loadedColors = configuredColorSetupSettings();
	if (!colorSlotOverride(loadedColors, kMrPaletteLineNumbers, MRColorOutputMode::RgbAutomatic, slotValue) || slotValue != projectColorAttribute(probeValues[8], MRColorOutputMode::RgbAutomatic)) {
		failureReason = "Line-number palette slot must be restored from WINDOWCOLORS theme value.";
		restore();
		return false;
	}
	if (!colorSlotOverride(loadedColors, kMrPaletteCodeFolding, MRColorOutputMode::RgbAutomatic, slotValue) || slotValue != projectColorAttribute(probeValues[9], MRColorOutputMode::RgbAutomatic)) {
		failureReason = "Code-folding palette slot must be restored from WINDOWCOLORS theme value.";
		restore();
		return false;
	}
	if (!colorSlotOverride(loadedColors, kMrPaletteCodeFoldingMarker, MRColorOutputMode::RgbAutomatic, slotValue) || slotValue != projectColorAttribute(probeValues[10], MRColorOutputMode::RgbAutomatic)) {
		failureReason = "Code-folding-marker palette slot must be restored from WINDOWCOLORS theme value.";
		restore();
		return false;
	}
	if (!colorSlotOverride(loadedColors, kMrPaletteFormatRuler, MRColorOutputMode::RgbAutomatic, slotValue) || slotValue != projectColorAttribute(probeValues[11], MRColorOutputMode::RgbAutomatic)) {
		failureReason = "Format-ruler palette slot must be restored from WINDOWCOLORS theme value.";
		restore();
		return false;
	}
	if (!colorSlotOverride(loadedColors, kMrPaletteFocusedPaneBorder, MRColorOutputMode::RgbAutomatic, slotValue) || slotValue != projectColorAttribute(probeValues[12], MRColorOutputMode::RgbAutomatic)) {
		failureReason = "Focused-pane-border palette slot must be restored from WINDOWCOLORS theme value.";
		restore();
		return false;
	}
	{
		std::string effectiveThemePath;
		std::string matchedProfileName;

		if (!effectiveEditWindowColorThemePathForPath("", effectiveThemePath, &matchedProfileName)) {
			failureReason = "Global color theme fallback lookup failed.";
			restore();
			return false;
		}
		if (effectiveThemePath != themePath || !matchedProfileName.empty()) {
			failureReason = "Pathless editors must use the global color theme fallback.";
			restore();
			return false;
		}
	}

	restore();
	if (!restored) {
		failureReason = "Unable to restore WINDOWCOLORS/theme path after roundtrip probe.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testFileCompareTextColorPreservesBackgroundGuard(std::string &failureReason) {
	const std::string viewportPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorViewport.cpp");
	const std::string paneWindowPath = absolutePathFromCwd("ui/MRBentoBox/MRBentoBoxPaneWindow.cpp");
	const std::string projectionPath = absolutePathFromCwd("ui/MRBentoBox/MRBentoBoxProjection.cpp");
	std::string viewportContent;
	std::string paneWindowContent;
	std::string projectionContent;
	std::string errorText;

	if (!readTextFile(viewportPath, viewportContent, errorText)) {
		failureReason = "Unable to read MRFileEditorViewport.cpp for FC text color guard: " + errorText;
		return false;
	}
	if (!readTextFile(paneWindowPath, paneWindowContent, errorText)) {
		failureReason = "Unable to read MRBentoBoxPaneWindow.cpp for FC text color guard: " + errorText;
		return false;
	}
	if (!readTextFile(projectionPath, projectionContent, errorText)) {
		failureReason = "Unable to read MRBentoBoxProjection.cpp for FC text color guard: " + errorText;
		return false;
	}
	if (viewportContent.find("diffTextColor = configured;") == std::string::npos) {
		failureReason = "File Compare text color must preserve the configured background.";
		return false;
	}
	if (viewportContent.find("mFileCompareGuttersConfigured && configuredColorSlotOverride(kMrPaletteFileCompareTextEqual, configured)") == std::string::npos ||
	    viewportContent.find("TColorAttr color = editorTextFillColor();") == std::string::npos) {
		failureReason = "File Compare blank and virtual text rows must use the FC text fill color.";
		return false;
	}
	if (viewportContent.find("diffTextColor = static_cast<TColorAttr>((baseTextColor & 0xF0) | (configured & 0x0F));") != std::string::npos) {
		failureReason = "File Compare text color must not mask away the configured background.";
		return false;
	}
	if (paneWindowContent.find("markerAttr = fillAttr;") == std::string::npos || projectionContent.find("sourceMarkerAttr = fillAttr;") == std::string::npos) {
		failureReason = "File Compare scrollbar indicator fallback must not use the normal window frame color.";
		return false;
	}
	const std::size_t paneLayoutStart = paneWindowContent.find("void MRPaneEditWindow::layoutPaneChrome() noexcept");
	const std::size_t paneLayoutEnd = paneWindowContent.find("\nvoid MRPaneEditWindow::configurePaneScrollBarColors", paneLayoutStart);
	const std::size_t sourceLayoutStart = projectionContent.find("void MRBentoBox::layoutSourcePaneChrome(const TRect &content) noexcept");
	const std::size_t sourceLayoutEnd = projectionContent.find("\nvoid MRBentoBox::hideSourcePaneChrome", sourceLayoutStart);
	if (paneLayoutStart == std::string::npos || paneLayoutEnd == std::string::npos || sourceLayoutStart == std::string::npos || sourceLayoutEnd == std::string::npos) {
		failureReason = "Unable to isolate File Compare scrollbar layout functions.";
		return false;
	}
	const std::string paneLayoutFunction = paneWindowContent.substr(paneLayoutStart, paneLayoutEnd - paneLayoutStart);
	const std::string sourceLayoutFunction = projectionContent.substr(sourceLayoutStart, sourceLayoutEnd - sourceLayoutStart);
	if (paneWindowContent.find("void MRPaneEditWindow::configurePaneScrollBarColors() noexcept") == std::string::npos ||
	    projectionContent.find("void MRBentoBox::configureSourcePaneScrollBarColors() noexcept") == std::string::npos ||
	    paneLayoutFunction.find("configurePaneScrollBarColors();") == std::string::npos ||
	    paneLayoutFunction.find("drawPaneScrollBars();") == std::string::npos ||
	    sourceLayoutFunction.find("configureSourcePaneScrollBarColors();") == std::string::npos) {
		failureReason = "File Compare scrollbar overrides must be configured in stable layout/draw paths.";
		return false;
	}
	if (paneWindowContent.find("MREditWindow::setState(aState, enable);") != std::string::npos && paneWindowContent.find("configurePaneScrollBarColors();\n\tMREditWindow::setState(aState, enable);") != std::string::npos) {
		failureReason = "File Compare pane scrollbar overrides must not run before MREditWindow::setState.";
		return false;
	}
	if (projectionContent.find("configureSourcePaneScrollBarColors();\n\tMREditWindow::setState(aState, enable);") != std::string::npos) {
		failureReason = "File Compare source scrollbar overrides must not run before MREditWindow::setState.";
		return false;
	}
	if (projectionContent.find("std::size_t displayLineCount = 0;") == std::string::npos || projectionContent.find("if (displayLineCount > 0) text.push_back('\\n');") == std::string::npos ||
	    projectionContent.find("++displayLineCount;") == std::string::npos) {
		failureReason = "File Compare display text must not append a synthetic terminal newline after the last projected line.";
		return false;
	}
	if (projectionContent.find("text += line;\n\ttext.push_back('\\n');") != std::string::npos) {
		failureReason = "File Compare display text must use separators between projected lines, not terminal newline append.";
		return false;
	}
	const std::size_t paneScrollStart = paneWindowContent.find("void MRPaneEditWindow::drawPaneScrollBars() noexcept");
	const std::size_t paneScrollEnd = paneWindowContent.find("\nTFrame *MRPaneEditWindow::initFrame", paneScrollStart);
	const std::size_t sourceScrollStart = projectionContent.find("void MRBentoBox::drawSourcePaneScrollBars() noexcept");
	const std::size_t sourceScrollEnd = projectionContent.find("\nvoid MRBentoBox::drawSharedEditorPanes", sourceScrollStart);
	if (paneScrollStart == std::string::npos || paneScrollEnd == std::string::npos || sourceScrollStart == std::string::npos || sourceScrollEnd == std::string::npos) {
		failureReason = "Unable to isolate File Compare scrollbar drawing functions.";
		return false;
	}
	const std::string paneScrollFunction = paneWindowContent.substr(paneScrollStart, paneScrollEnd - paneScrollStart);
	const std::string sourceScrollFunction = projectionContent.substr(sourceScrollStart, sourceScrollEnd - sourceScrollStart);
	if (paneScrollFunction.find("configurePaneScrollBarColors();") == std::string::npos || sourceScrollFunction.find("configureSourcePaneScrollBarColors();") == std::string::npos) {
		failureReason = "File Compare scrollbar overrides must be refreshed in draw paths.";
		return false;
	}
	if (paneScrollFunction.find("kMrPaletteFileComparePaneBorder") != std::string::npos || paneScrollFunction.find("kMrPaletteFileCompareFocusedPaneBorder") != std::string::npos ||
	    sourceScrollFunction.find("kMrPaletteFileComparePaneBorder") != std::string::npos || sourceScrollFunction.find("kMrPaletteFileCompareFocusedPaneBorder") != std::string::npos) {
		failureReason = "File Compare scrollbars must not use FC pane-border colors.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testCodeColorUsesConfiguredAttributeGuard(std::string &failureReason) {
	std::array<MRRgbColorAttribute, MRColorSetupSettings::kCodeCount> probeValues{};
	struct CodeColorInventoryEntry {
		const char *name;
		const char *paletteMacro;
		unsigned char paletteIndex;
		bool tokenColorConsumer;
		bool sidekickConsumer;
		bool contextMenuConsumer;
		bool editorViewportConsumer;
		bool explicitReserve;
	};
	static const CodeColorInventoryEntry codeColorInventory[] = {
	    {"comments", "kMrPaletteCodeComments", kMrPaletteCodeComments, true, false, false, false, false},
	    {"strings", "kMrPaletteCodeStrings", kMrPaletteCodeStrings, true, false, false, false, false},
	    {"characters", "kMrPaletteCodeCharacters", kMrPaletteCodeCharacters, false, false, false, false, true},
	    {"numbers", "kMrPaletteCodeNumbers", kMrPaletteCodeNumbers, true, false, false, false, false},
	    {"keywords", "kMrPaletteCodeKeywords", kMrPaletteCodeKeywords, true, false, false, false, false},
	    {"types", "kMrPaletteCodeTypes", kMrPaletteCodeTypes, true, false, false, false, false},
	    {"directives", "kMrPaletteCodeDirectives", kMrPaletteCodeDirectives, true, false, false, false, false},
	    {"functions", "kMrPaletteCodeFunctions", kMrPaletteCodeFunctions, false, false, false, false, true},
	    {"builtins", "kMrPaletteCodeBuiltins", kMrPaletteCodeBuiltins, false, false, false, false, true},
	    {"constants", "kMrPaletteCodeConstants", kMrPaletteCodeConstants, true, false, false, false, false},
	    {"operators", "kMrPaletteCodeOperators", kMrPaletteCodeOperators, false, false, false, false, true},
	    {"brackets", "kMrPaletteCodeBrackets", kMrPaletteCodeBrackets, false, false, false, false, true},
	    {"delimiters", "kMrPaletteCodeDelimiters", kMrPaletteCodeDelimiters, true, false, false, false, false},
	    {"sidekick editor text", "kMrPaletteSidekickEditorText", kMrPaletteSidekickEditorText, false, true, false, false, false},
	    {"sidekick editor highlight", "kMrPaletteSidekickEditorHighlight", kMrPaletteSidekickEditorHighlight, false, true, false, false, false},
	    {"context menu", "kMrPaletteContextMenu", kMrPaletteContextMenu, false, false, true, false, false},
	    {"context menu selector", "kMrPaletteContextMenuSelector", kMrPaletteContextMenuSelector, false, false, true, false, false},
	    {"snippet sidekick frame", "kMrPaletteSnippetSidekickFrame", kMrPaletteSnippetSidekickFrame, false, true, false, false, false},
		    {"snippet sidekick text", "kMrPaletteSnippetSidekickText", kMrPaletteSnippetSidekickText, false, true, false, false, false},
		    {"snippet placeholder", "kMrPaletteSnippetPlaceholder", kMrPaletteSnippetPlaceholder, false, false, false, false, true},
		    {"snippet active placeholder", "kMrPaletteSnippetActivePlaceholder", kMrPaletteSnippetActivePlaceholder, false, true, false, false, false},
		    {"snippet default text", "kMrPaletteSnippetDefaultText", kMrPaletteSnippetDefaultText, false, true, false, false, false},
		    {"outline file header", "kMrPaletteOutlineFileHeader", kMrPaletteOutlineFileHeader, false, false, true, false, false},
		    {"outline level 1", "kMrPaletteOutlineLevel0", kMrPaletteOutlineLevel0, false, false, true, false, false},
		    {"outline level 2", "kMrPaletteOutlineLevel1", kMrPaletteOutlineLevel1, false, false, true, false, false},
		    {"outline level 3", "kMrPaletteOutlineLevel2", kMrPaletteOutlineLevel2, false, false, true, false, false},
		    {"outline level 4", "kMrPaletteOutlineLevel3", kMrPaletteOutlineLevel3, false, false, true, false, false},
		    {"outline level 5", "kMrPaletteOutlineLevel4", kMrPaletteOutlineLevel4, false, false, true, false, false},
		    {"outline level 6", "kMrPaletteOutlineLevel5", kMrPaletteOutlineLevel5, false, false, true, false, false},
		    {"outline level 7", "kMrPaletteOutlineLevel6", kMrPaletteOutlineLevel6, false, false, true, false, false},
		    {"outline level 8", "kMrPaletteOutlineLevel7", kMrPaletteOutlineLevel7, false, false, true, false, false},
		    {"outline level 9", "kMrPaletteOutlineLevel8", kMrPaletteOutlineLevel8, false, false, true, false, false},
		    {"outline level 10", "kMrPaletteOutlineLevel9", kMrPaletteOutlineLevel9, false, false, true, false, false},
		};
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::size_t itemCount = 0;
	const MRColorSetupItem *items = colorSetupGroupItems(MRColorSetupGroup::Code, itemCount);
	const std::string viewportPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorViewport.cpp");
	const std::string warmupPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorWarmup.cpp");
	const std::string sidekickPath = absolutePathFromCwd("ui/MRSidekickEditor.cpp");
	const std::string columnListPath = absolutePathFromCwd("ui/widgets/MRColumnListView.cpp");
	std::string viewportContent;
	std::string warmupContent;
	std::string sidekickContent;
	std::string columnListContent;
	std::string tokenColorFunction;
	std::string errorText;
	TColorAttr value;
	bool restoreOk = true;
	for (std::size_t i = 0; i < probeValues.size(); ++i) probeValues[i] = MRRgbColorAttribute{static_cast<std::uint32_t>(0x234500u + i), static_cast<std::uint32_t>(0x678900u + i)};

	auto restore = [&]() {
		if (!restoreOk) return;
		restoreOk = setConfiguredColorSetupGroupValues(MRColorSetupGroup::Code, previous.codeColors.data(), previous.codeColors.size(), &errorText);
	};

	if (items == nullptr || itemCount != probeValues.size() || itemCount != sizeof(codeColorInventory) / sizeof(codeColorInventory[0])) {
		failureReason = "Unexpected CODECOLORS item mapping.";
		return false;
	}
	for (std::size_t i = 0; i < itemCount; ++i) {
		if (std::strcmp(items[i].label, codeColorInventory[i].name) != 0 || items[i].paletteIndex != codeColorInventory[i].paletteIndex) {
			failureReason = "CODECOLORS inventory order, name or palette slot changed without updating the conformance guard.";
			return false;
		}
	}
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Code, probeValues.data(), probeValues.size(), &errorText)) {
		failureReason = "Unable to set CODECOLORS probe values: " + errorText;
		return false;
	}
	for (std::size_t i = 0; i < itemCount; ++i) {
		if (!colorSlotOverride(configuredColorSetupSettings(), items[i].paletteIndex, MRColorOutputMode::RgbAutomatic, value)) {
			restore();
			failureReason = "CODECOLORS item must override its mapped palette slot.";
			return false;
		}
		if (value != projectColorAttribute(probeValues[i], MRColorOutputMode::RgbAutomatic)) {
			restore();
			failureReason = "CODECOLORS slot mapping must preserve the full configured attribute.";
			return false;
		}
	}

	if (!readTextFile(viewportPath, viewportContent, errorText)) {
		restore();
		failureReason = "Unable to read MRFileEditorViewport.cpp for code color guard: " + errorText;
		return false;
	}
	if (!readTextFile(warmupPath, warmupContent, errorText)) {
		restore();
		failureReason = "Unable to read MRFileEditorWarmup.cpp for code color guard: " + errorText;
		return false;
	}
	if (!readTextFile(sidekickPath, sidekickContent, errorText)) {
		restore();
		failureReason = "Unable to read MRSidekickEditor.cpp for code color guard: " + errorText;
		return false;
	}
	if (!readTextFile(columnListPath, columnListContent, errorText)) {
		restore();
		failureReason = "Unable to read MRColumnListView.cpp for code color guard: " + errorText;
		return false;
	}
	if (sidekickContent.find("mSnippetSidekick ? kMrPaletteSnippetSidekickText : kMrPaletteSidekickEditorText") == std::string::npos ||
	    sidekickContent.find("mSnippetSidekick ? kMrPaletteSnippetActivePlaceholder : kMrPaletteSidekickEditorHighlight") == std::string::npos || sidekickContent.find("if (mSnippetSidekick)") == std::string::npos) {
		restore();
		failureReason = "Snippet SideKick colors must be gated by snippet SideKick state, not by editability.";
		return false;
	}
	const std::size_t tokenColorStart = viewportContent.find("TColorAttr MRFileEditor::tokenColor");
	const std::size_t tokenColorEnd = viewportContent.find("\nvoid MRFileEditor::formatSyntaxLine", tokenColorStart);
	if (tokenColorStart == std::string::npos || tokenColorEnd == std::string::npos) {
		restore();
		failureReason = "Unable to isolate MRFileEditor::tokenColor for code color guard.";
		return false;
	}
	tokenColorFunction = viewportContent.substr(tokenColorStart, tokenColorEnd - tokenColorStart);
	if (tokenColorFunction.find("if (configuredColorSlotOverride(paletteSlot, configured)) return configured;") == std::string::npos) {
		restore();
		failureReason = "Code token colors must preserve the full configured attribute, including background.";
		return false;
	}
	if (tokenColorFunction.find("return TColorAttr(TColorDesired(fallbackForeground), background);") == std::string::npos) {
		restore();
		failureReason = "Code token fallback colors must still combine editor background with fallback foreground.";
		return false;
	}
	if (tokenColorFunction.find("configured & 0x0F") != std::string::npos || tokenColorFunction.find("(configured &") != std::string::npos) {
		restore();
		failureReason = "Code token colors must not mask configured colors down to foreground.";
		return false;
	}
	if (warmupContent.find("bool MRFileEditor::syntaxPipelineEnabled() const") == std::string::npos || warmupContent.find("return languageFeaturesEnabled() && effectiveEditSetupSettings().codeColoring;") == std::string::npos) {
		restore();
		failureReason = "Editor syntax coloring pipeline must be gated by CODE_COLORING, not only by CODE_LANGUAGE.";
		return false;
	}
	for (std::size_t i = 0; i < itemCount; ++i) {
		const CodeColorInventoryEntry &entry = codeColorInventory[i];
		const bool usedByTokenColor = tokenColorFunction.find(entry.paletteMacro) != std::string::npos;
		const bool usedBySidekick = sidekickContent.find(entry.paletteMacro) != std::string::npos;
		const bool usedByContextMenu = columnListContent.find(entry.paletteMacro) != std::string::npos;
		const bool usedByEditorViewport = viewportContent.find(entry.paletteMacro) != std::string::npos;

		if (entry.tokenColorConsumer && !usedByTokenColor) {
			restore();
			failureReason = std::string("CODECOLORS token consumer is missing for ") + entry.name;
			return false;
		}
		if (entry.sidekickConsumer && !usedBySidekick) {
			restore();
			failureReason = std::string("CODECOLORS sidekick consumer is missing for ") + entry.name;
			return false;
		}
		if (entry.contextMenuConsumer && !usedByContextMenu) {
			restore();
			failureReason = std::string("CODECOLORS context menu consumer is missing for ") + entry.name;
			return false;
		}
		if (entry.editorViewportConsumer && !usedByEditorViewport) {
			restore();
			failureReason = std::string("CODECOLORS editor viewport consumer is missing for ") + entry.name;
			return false;
		}
		if (entry.explicitReserve && (usedByTokenColor || usedBySidekick || usedByContextMenu || usedByEditorViewport)) {
			restore();
			failureReason = std::string("CODECOLORS reserve slot gained a consumer without contract update: ") + entry.name;
			return false;
		}
		if (!entry.tokenColorConsumer && !entry.sidekickConsumer && !entry.contextMenuConsumer && !entry.editorViewportConsumer && !entry.explicitReserve) {
			restore();
			failureReason = std::string("CODECOLORS slot is neither consumed nor explicitly reserved: ") + entry.name;
			return false;
		}
	}

	restore();
	if (!restoreOk) {
		failureReason = "Unable to restore CODECOLORS after probe: " + errorText;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testIndicatorLineNumberColorWiringGuard(std::string &failureReason) {
	const std::string indicatorPath = absolutePathFromCwd("ui/MRIndicator.hpp");
	const std::string windowPath = absolutePathFromCwd("ui/MREditWindow.hpp");
	std::string indicatorContent;
	std::string windowContent;
	std::string ioError;

	if (!readTextFile(indicatorPath, indicatorContent, ioError)) {
		failureReason = "Unable to read MRIndicator.hpp for line-number wiring guard: " + ioError;
		return false;
	}
	if (!readTextFile(windowPath, windowContent, ioError)) {
		failureReason = "Unable to read MREditWindow.hpp for line-number wiring guard: " + ioError;
		return false;
	}
	if (indicatorContent.find("cursorColor = getColor(3);") == std::string::npos || indicatorContent.find("b.moveStr(cursorX, cursorText, cursorColor);") == std::string::npos) {
		failureReason = "MRIndicator must draw line/column text from the dedicated line-number color slot.";
		return false;
	}
	if (indicatorContent.find("TPalette palette(\"\\x02\\x03\\x0C\", 3);") == std::string::npos) {
		failureReason = "MRIndicator palette must expose line-number color as local slot 12.";
		return false;
	}
	if (windowContent.find("kMrPaletteLineNumbers") == std::string::npos) {
		failureReason = "MREditWindow palette must include the line-number extension slot.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testExplicitSyntaxLanguageMarkerGuard(std::string &failureReason) {
	const std::string windowPath = absolutePathFromCwd("ui/MREditWindow.hpp");
	std::string windowContent;
	std::string ioError;
	const std::string needle = "const bool showLanguageSlot =";
	std::size_t pos = std::string::npos;
	std::size_t lineEnd = std::string::npos;
	std::string line;

	if (!readTextFile(windowPath, windowContent, ioError)) {
		failureReason = "Unable to read MREditWindow.hpp for syntax-language marker guard: " + ioError;
		return false;
	}
	pos = windowContent.find(needle);
	if (pos == std::string::npos) {
		failureReason = "MREditWindow marker provider must define showLanguageSlot.";
		return false;
	}
	lineEnd = windowContent.find('\n', pos);
	line = windowContent.substr(pos, lineEnd == std::string::npos ? std::string::npos : lineEnd - pos);
	if (line.find("syntaxLanguage() != MRSyntaxLanguage::PlainText") == std::string::npos) {
		failureReason = "Syntax language marker must be shown for every non-plain explicit or automatic language.";
		return false;
	}
	if (line.find("syntaxLanguageAutomatic()") != std::string::npos) {
		failureReason = "Syntax language marker must not be gated on automatic language detection.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testCurrentLineColorWiringGuard(std::string &failureReason) {
	const std::string palettePath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorViewState.cpp");
	const std::string viewportPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorViewport.cpp");
	std::string paletteContent;
	std::string viewportContent;
	std::string ioError;

	if (!readTextFile(palettePath, paletteContent, ioError)) {
		failureReason = "Unable to read MRFileEditorViewState.cpp for current-line color wiring guard: " + ioError;
		return false;
	}
	if (!readTextFile(viewportPath, viewportContent, ioError)) {
		failureReason = "Unable to read MRFileEditorViewport.cpp for current-line color wiring guard: " + ioError;
		return false;
	}
	if (paletteContent.find("TPalette palette(\"\\x06\\x07\\x09\\x0A\\x0B\\x0C\", 6);") == std::string::npos) {
		failureReason = "MRFileEditor palette must expose current-line, changed-text and line-number slots.";
		return false;
	}
	if (viewportContent.find("basePair = getColor(0x0303);") == std::string::npos || viewportContent.find("currentLineInBlock = false;") == std::string::npos || viewportContent.find("overlayActive = mBlockOverlayActive;") == std::string::npos) {
		failureReason = "Current-line rendering must stay wired with block overlay state.";
		return false;
	}
	if (viewportContent.find("lineStart <= cursorPos && cursorPos < lineEnd") == std::string::npos) {
		failureReason = "Current-line detection must be range-based in the active render line.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testChangedTextColorWiringGuard(std::string &failureReason) {
	const std::string headerPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditor.hpp");
	const std::string sourcePath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorViewport.cpp");
	const std::string editorPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorMarkers.cpp");
	const std::string indentPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorIndent.cpp");
	std::string headerContent;
	std::string sourceContent;
	std::string editorContent;
	std::string indentContent;
	std::string ioError;

	if (!readTextFile(headerPath, headerContent, ioError)) {
		failureReason = "Unable to read MRFileEditor.hpp for changed-text color wiring guard: " + ioError;
		return false;
	}
	if (!readTextFile(sourcePath, sourceContent, ioError)) {
		failureReason = "Unable to read MRFileEditorViewport.cpp for changed-text color wiring guard: " + ioError;
		return false;
	}
	if (!readTextFile(editorPath, editorContent, ioError)) {
		failureReason = "Unable to read MRFileEditorMarkers.cpp for changed-text dirty-range guard: " + ioError;
		return false;
	}
	if (!readTextFile(indentPath, indentContent, ioError)) {
		failureReason = "Unable to read MRFileEditorIndent.cpp for changed-text dirty-range guard: " + ioError;
		return false;
	}
	if (sourceContent.find("TAttrPair changedPair = getColor(0x0505);") == std::string::npos || sourceContent.find("bool changedChar = !currentLine && !currentLineInBlock && isDirtyOffset(documentPos);") == std::string::npos || sourceContent.find("TAttrPair effectivePair = changedChar ? changedPair : basePair;") == std::string::npos) {
		failureReason = "Changed-text must be applied per character via dedicated dirty-range lookup.";
		return false;
	}
	if (sourceContent.find("overlayActive = mBlockOverlayActive;") == std::string::npos || sourceContent.find("mBlockOverlayMode") == std::string::npos) {
		failureReason = "Changed-text rendering must account for the restored block overlay state.";
		return false;
	}
	if (headerContent.find("std::vector<MRTextBufferModel::Range> mDirtyRanges;") == std::string::npos || headerContent.find("void addDirtyRange(") == std::string::npos || headerContent.find("bool isDirtyOffset(std::size_t pos) const noexcept;") == std::string::npos || editorContent.find("bool MRFileEditor::isDirtyOffset(") == std::string::npos) {
		failureReason = "Changed-text wiring requires dedicated dirty-range tracking in MRFileEditor.";
		return false;
	}
	if (indentContent.find("remapDirtyRangesForAppliedChange(*changeSet);") == std::string::npos || editorContent.find("void MRFileEditor::remapDirtyRangesForAppliedChange(") == std::string::npos) {
		failureReason = "Changed-text ranges must be remapped across edits to stay position-correct.";
		return false;
	}
	if (editorContent.find("if (pos >= mBufferModel.length())") == std::string::npos) {
		failureReason = "Changed-text lookup must not clamp offsets beyond EOF into the last dirty character.";
		return false;
	}
	if (sourceContent.find("else if (changedLine)") != std::string::npos) {
		failureReason = "Changed-text must not color whole lines anymore.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testEditorCursorViewportGuard(std::string &failureReason) {
	const std::string headerPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditor.hpp");
	const std::string sourcePath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorNavigation.cpp");
	const std::string viewportPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorViewport.cpp");
	std::string headerContent;
	std::string sourceContent;
	std::string viewportContent;
	std::string ioError;

	if (!readTextFile(headerPath, headerContent, ioError)) {
		failureReason = "Unable to read MRFileEditor.hpp for cursor viewport guard: " + ioError;
		return false;
	}
	if (!readTextFile(sourcePath, sourceContent, ioError)) {
		failureReason = "Unable to read MRFileEditorNavigation.cpp for cursor viewport guard: " + ioError;
		return false;
	}
	if (!readTextFile(viewportPath, viewportContent, ioError)) {
		failureReason = "Unable to read MRFileEditorViewport.cpp for cursor viewport guard: " + ioError;
		return false;
	}
	if (headerContent.find("using TextViewportGeometry = MRTextViewportLayout::Geometry;") == std::string::npos || headerContent.find("TextViewportGeometry textViewportGeometry() const noexcept") == std::string::npos ||
	    headerContent.find("bool shouldShowEditorCursor(long long x, long long y, const TextViewportGeometry &viewport) const noexcept") == std::string::npos ||
	    viewportContent.find("return MRTextViewportLayout::shouldShowCursor(viewport, x, y, visibleTextRows(), (state & sfActive) != 0, (state & sfSelected) != 0);") == std::string::npos ||
	    viewportContent.find("if (shouldShowEditorCursor(localX, localY, viewport))") == std::string::npos) {
		failureReason = "Editor cursor visibility must be gated by active/selected state and text viewport bounds.";
		return false;
	}
	if (sourceContent.find("int column = viewport.textColumnFromLocalX(local.x);") == std::string::npos || sourceContent.find("TextViewportGeometry viewport = textViewportGeometry();") == std::string::npos) {
		failureReason = "Mouse-to-text mapping must be routed through text viewport conversion.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testEofVirtualLineColorGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorViewport.cpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read MRFileEditorViewport.cpp for post-EOF clear-area guard: " + ioError;
		return false;
	}
	if (content.find("if (visibleLineIndex >= totalLines) break;") == std::string::npos ||
	    content.find("const bool drawEofMarker = editSettings.showEofMarker && isDocumentLine && currentLinePtr == mBufferModel.length();") == std::string::npos ||
	    content.find("formatSyntaxLine(buffer, currentLinePtr, currentLineIndex, syntaxLine, delta.x, textWidth, viewport.textLeft, isDocumentLine, drawEofMarker, drawEofMarker && editSettings.showEofMarkerEmoji);") == std::string::npos) {
		failureReason = "Draw path must stop semantic document rendering at EOF.";
		return false;
	}
	if (content.find("backgroundBuffer.moveChar(0, ' ', editorTextFill, static_cast<ushort>(size.x));") != std::string::npos ||
	    content.find("for (int y = 0; y < size.y; ++y)") != std::string::npos ||
	    content.find("writeBuf(0, y, size.x, 1, backgroundBuffer);") != std::string::npos) {
		failureReason = "Editor draw path must not clear the full target before document rendering.";
		return false;
	}
	if (content.find("const std::size_t documentRows = topLine < totalLines ? std::min<std::size_t>(static_cast<std::size_t>(textRows), totalLines - topLine) : 0;") == std::string::npos ||
	    content.find("for (int y = static_cast<int>(documentRows); y < textRows; ++y)") == std::string::npos ||
	    content.find("writeBuf(0, y + viewport.topInset, size.x, 1, gutterBackground);") == std::string::npos) {
		failureReason = "Post-EOF editor area must be cleared only after the last rendered document line.";
		return false;
	}
	if (content.find("const bool emptyEofDocumentLine = lineStart == documentLength && lineEnd == documentLength;") == std::string::npos ||
	    content.find("currentLine = !emptyEofDocumentLine && lineStart <= cursorPos && cursorPos < lineEnd;") == std::string::npos) {
		failureReason = "Empty EOF document lines must keep the text color combination instead of current-line color.";
		return false;
	}
	if (content.find("bool eofDocumentLineVisible = false;") == std::string::npos ||
	    content.find("eofDocumentLineVisible = lastVisibleDocumentLine < exactLineCount && lineStartForIndex(lastVisibleDocumentLine) == mBufferModel.length();") == std::string::npos ||
	    content.find("const bool drawEofMarker = editSettings.showEofMarker && !eofDocumentLineVisible && y == static_cast<int>(documentRows);") == std::string::npos ||
	    content.find("formatSyntaxLine(gutterBackground, virtualLineIndex, virtualLineIndex, MRSyntaxLineResult(), delta.x, textWidth, viewport.textLeft, false, true, editSettings.showEofMarkerEmoji);") == std::string::npos ||
	    content.find("if (drawEofMarker) drawEofMarkerGlyph(b, hScroll, width, drawX, basePair, drawEofMarkerAsEmoji);") == std::string::npos) {
		failureReason = "EOF marker must be drawn once on the EOF line or first visible post-EOF line without extending scroll range.";
		return false;
	}
	if (content.find("if (!drawEmoji && configuredColorSlotOverride(kMrPaletteEofMarker, configuredMarkerColor))") == std::string::npos) {
		failureReason = "EOF marker must support emoji toggle with text-mode color override wiring.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testSaveAsOverwriteAndBackupWiringGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorSave.cpp");
	const std::string viewportPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorViewport.cpp");
	std::string content;
	std::string viewportContent;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read MRFileEditorSave.cpp for Save As overwrite/backup guard: " + ioError;
		return false;
	}
	if (!readTextFile(viewportPath, viewportContent, ioError)) {
		failureReason = "Unable to read MRFileEditorViewport.cpp for Save As overwrite/backup guard: " + ioError;
		return false;
	}
	if (viewportContent.find("showUnsavedChangesDialog(\"Overwrite\", \"Target file exists. Overwrite?\",") == std::string::npos) {
		failureReason = "Save As must ask for overwrite confirmation via centralized UnsavedChanges dialog.";
		return false;
	}
	if (content.find("if (!samePath(saveName, fileName) && !confirmOverwriteForSaveAs(saveName))") == std::string::npos) {
		failureReason = "Save As must guard existing target overwrite before writing.";
		return false;
	}
	if (content.find("const bool backupEnabled = configuredBackupFilesSetting();") == std::string::npos || content.find("if (backupEnabled)") == std::string::npos || content.find("fnmerge(backupName, drive, dir, file, \".bak\");") == std::string::npos) {
		failureReason = "Backup file creation must be gated by configurable BACKUP_FILES setting.";
		return false;
	}
	if (content.find("const bool mappedInPlaceSave = mBufferModel.document().hasMappedOriginal() && samePath(mBufferModel.document().mappedPath().c_str(), targetPath);") == std::string::npos || content.find("useTemporaryTarget = mappedInPlaceSave && !backupMovedTarget;") == std::string::npos ||
	    content.find("rename(temporaryTargetPath.c_str(), targetPath)") == std::string::npos) {
		failureReason = "Mapped in-place save must write through a temporary target unless the original file was moved away for backup.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testThemeAndMacroSaveOverwriteWiringGuard(std::string &failureReason) {
	const std::string setupDialogsPath = absolutePathFromCwd("dialogs/setup/MRSetupSections.cpp");
	const std::string appPath = absolutePathFromCwd("app/MREditorApp.cpp");
	std::string setupContent;
	std::string appContent;
	std::string ioError;

	if (!readTextFile(setupDialogsPath, setupContent, ioError)) {
		failureReason = "Unable to read MRSetupSections.cpp for theme overwrite guard: " + ioError;
		return false;
	}
	if (!readTextFile(appPath, appContent, ioError)) {
		failureReason = "Unable to read MREditorApp.cpp for macro overwrite guard: " + ioError;
		return false;
	}
	if (setupContent.find("confirmOverwriteForPath(\"Overwrite\", \"Theme file exists. Overwrite?\", themeUri)") == std::string::npos) {
		failureReason = "Color Setup / Save Theme must ask for overwrite confirmation before writing.";
		return false;
	}
	if (setupContent.find("showUnsavedChangesDialog(primaryLabel, headline, targetPath.c_str())") == std::string::npos) {
		failureReason = "Theme overwrite confirmation must use centralized UnsavedChanges dialog.";
		return false;
	}
	if (appContent.find("confirmOverwriteForPath(\"Overwrite\", \"Macro file exists. Overwrite?\", savePath)") == std::string::npos) {
		failureReason = "Recorded macro save must ask for overwrite confirmation before writing.";
		return false;
	}
	if (appContent.find("showUnsavedChangesDialog(primaryLabel, headline, targetPath.c_str())") == std::string::npos) {
		failureReason = "Macro overwrite confirmation must use centralized UnsavedChanges dialog.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testPersistentBlocksWiringGuard(std::string &failureReason) {
	const std::string editSetupPath = absolutePathFromCwd("config/settings/MRSettingsEditSetup.cpp");
	const std::string snapshotPath = absolutePathFromCwd("config/settings/MRSettingsSnapshotIO.cpp");
	const std::string vmPath = absolutePathFromCwd("mrmac/MRVM.cpp");
	const std::string panelPath = absolutePathFromCwd("dialogs/extensions/MRFileExtensionEditorSettings.cpp");
	std::string editSetupContent;
	std::string snapshotContent;
	std::string vmContent;
	std::string panelContent;
	std::string ioError;

	if (!readTextFile(editSetupPath, editSetupContent, ioError)) {
		failureReason = "Unable to read MRSettingsEditSetup.cpp for persistent-blocks guard: " + ioError;
		return false;
	}
	if (!readTextFile(snapshotPath, snapshotContent, ioError)) {
		failureReason = "Unable to read MRSettingsSnapshotIO.cpp for persistent-blocks guard: " + ioError;
		return false;
	}
	if (!readTextFile(vmPath, vmContent, ioError)) {
		failureReason = "Unable to read MRVM.cpp for persistent-blocks guard: " + ioError;
		return false;
	}
	if (!readTextFile(panelPath, panelContent, ioError)) {
		failureReason = "Unable to read MRFileExtensionEditorSettings.cpp for persistent-blocks guard: " + ioError;
		return false;
	}
	if (editSetupContent.find("{\"PERSISTENT_BLOCKS\"") == std::string::npos || editSetupContent.find("upperKeyName == \"PERSISTENT_BLOCKS\"") == std::string::npos || snapshotContent.find("MRSETUP('PERSISTENT_BLOCKS'") == std::string::npos) {
		failureReason = "Persistent blocks must be parsed through edit setup descriptors and serialized via MRSETUP.";
		return false;
	}
	if (vmContent.find("classifySettingsKey(setupKey)") == std::string::npos || vmContent.find("PERSISTENT_BLOCKS") == std::string::npos) {
		failureReason = "MRVM startup whitelist must accept PERSISTENT_BLOCKS.";
		return false;
	}
	if (panelContent.find("Persistent ~B~locks") == std::string::npos || panelContent.find("kOptionPersistentBlocks") == std::string::npos) {
		failureReason = "File extension editor settings panel must expose and wire a Persistent blocks option.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testFileExtensionRightMarginSyncGuard(std::string &failureReason) {
	const std::string panelPath = absolutePathFromCwd("dialogs/extensions/MRFileExtensionEditorSettings.cpp");
	std::string panelContent;
	std::string ioError;

	if (!readTextFile(panelPath, panelContent, ioError)) {
		failureReason = "Unable to read MRFileExtensionEditorSettings.cpp for right-margin sync guard: " + ioError;
		return false;
	}
	if (panelContent.find("int typedRightMargin = lastKnownRightMarginForFormatLine;") == std::string::npos || panelContent.find("typedRightMargin = parseIntegerTextOrDefault(readInputFieldValue(rightMarginField)") == std::string::npos || panelContent.find("synchronizeEditFormatLineMargins(currentFormatLine, typedLeftMargin, typedRightMargin") == std::string::npos) {
		failureReason = "File extension editor settings panel must preserve the typed right margin while synchronizing FORMAT_LINE.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testFileExtensionCodeLanguageChoicesGuard(std::string &failureReason) {
	struct ChoiceCase {
		const char *label;
		const char *canonicalValue;
	};
	static const ChoiceCase choicesExpected[] = {
	    {"None", "NONE"},
	    {"Automatic", "AUTO"},
	    {"C", "C"},
	    {"C++", "CPP"},
	    {"Python", "PYTHON"},
	    {"JavaScript", "JAVASCRIPT"},
	    {"TypeScript", "TYPESCRIPT"},
	    {"TSX", "TSX"},
	    {"Bash", "BASH"},
	    {"zsh", "ZSH"},
	    {"fish", "FISH"},
	    {"JSON", "JSON"},
	    {"YAML", "YAML"},
	    {"XML", "XML"},
	    {"Perl", "PERL"},
	    {"Swift", "SWIFT"},
	    {"Rust", "RUST"},
	    {"Go", "GO"},
	    {"Pascal", "PASCAL"},
	    {"BASIC", "BASIC"},
	    {"LaTeX", "LATEX"},
	    {"Kotlin", "KOTLIN"},
	    {"C#", "CSHARP"},
	    {"systemd et al.", "SYSTEMD"},
	};
	static const ChoiceCase aliasesExpected[] = {
	    {"", "NONE"},
	    {"AUTOMATIC", "AUTO"},
	    {"C++", "CPP"},
	    {"FREEBASIC", "BASIC"},
	    {"QB64", "BASIC"},
	    {"QB64PE", "BASIC"},
	    {"GAMBAS", "BASIC"},
	    {"TEX", "LATEX"},
	    {"C#", "CSHARP"},
	    {"SYSTEMD ET AL.", "SYSTEMD"},
	};
	MRFileExtensionProfilesInternal::FileExtensionEditorSettingsDialogRecord record;
	MREditSetupSettings settings;
	std::string errorText;
	const std::vector<std::string> choices = MRFileExtensionProfilesInternal::dialogCodeLanguageChoices();

	if (choices.size() != std::size(choicesExpected)) {
		failureReason = "File extension code-language drop list exposes an unexpected number of choices.";
		return false;
	}
	for (std::size_t i = 0; i < choices.size(); ++i) {
		if (choices[i] != choicesExpected[i].label) {
			failureReason = "File extension code-language drop list order differs at " + std::to_string(i) + ".";
			return false;
		}
		settings = resolveEditSetupDefaults();
		settings.codeLanguage = choicesExpected[i].canonicalValue;
		MRFileExtensionProfilesInternal::settingsToDialogRecord(settings, record);
		if (MRFileExtensionProfilesInternal::readRecordField(record.codeLanguage) != choicesExpected[i].label) {
			failureReason = std::string("File extension code-language label mismatch for ") + choicesExpected[i].canonicalValue + ".";
			return false;
		}
		if (!MRFileExtensionProfilesInternal::fileExtensionEditorSettingsDialogRecordToSettings(record, settings, errorText)) {
			failureReason = std::string("File extension code-language parser rejected ") + choicesExpected[i].label + ": " + errorText;
			return false;
		}
		if (settings.codeLanguage != choicesExpected[i].canonicalValue) {
			failureReason = std::string("File extension code-language parser did not canonicalize ") + choicesExpected[i].label + ".";
			return false;
		}
	}

	MRFileExtensionProfilesInternal::settingsToDialogRecord(resolveEditSetupDefaults(), record);
	for (const ChoiceCase &alias : aliasesExpected) {
		MRFileExtensionProfilesInternal::writeRecordField(record.codeLanguage, sizeof(record.codeLanguage), alias.label);
		if (!MRFileExtensionProfilesInternal::fileExtensionEditorSettingsDialogRecordToSettings(record, settings, errorText)) {
			failureReason = std::string("File extension code-language parser rejected alias ") + alias.label + ": " + errorText;
			return false;
		}
		if (settings.codeLanguage != alias.canonicalValue) {
			failureReason = std::string("File extension code-language alias did not canonicalize to ") + alias.canonicalValue + ".";
			return false;
		}
	}

	MRFileExtensionProfilesInternal::writeRecordField(record.codeLanguage, sizeof(record.codeLanguage), "NOT_A_LANGUAGE");
	errorText.clear();
	if (MRFileExtensionProfilesInternal::fileExtensionEditorSettingsDialogRecordToSettings(record, settings, errorText) || errorText.find("CODE_LANGUAGE") == std::string::npos) {
		failureReason = "File extension code-language parser accepted an unknown value or omitted its diagnostic.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testFileExtensionFoldingControlsGuard(std::string &failureReason) {
	const std::string panelPath = absolutePathFromCwd("dialogs/extensions/MRFileExtensionEditorSettings.cpp");
	const std::string internalPath = absolutePathFromCwd("dialogs/extensions/MRFileExtensionEditorSettingsInternal.hpp");
	const std::string editSetupPath = absolutePathFromCwd("config/settings/MRSettingsEditSetup.cpp");
	const std::string snapshotPath = absolutePathFromCwd("config/settings/MRSettingsSnapshotIO.cpp");
	const std::string warmupPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorWarmup.cpp");
	std::string panelContent;
	std::string internalContent;
	std::string editSetupContent;
	std::string snapshotContent;
	std::string warmupContent;
	std::string ioError;

	if (!readTextFile(panelPath, panelContent, ioError) || !readTextFile(internalPath, internalContent, ioError) || !readTextFile(editSetupPath, editSetupContent, ioError) || !readTextFile(snapshotPath, snapshotContent, ioError) ||
	    !readTextFile(warmupPath, warmupContent, ioError)) {
		failureReason = "Unable to read file extension folding-control sources: " + ioError;
		return false;
	}
	if (panelContent.find("Code fo~L~ding") != std::string::npos || internalContent.find("kOptionCodeFoldingFeature") != std::string::npos || internalContent.find("kLeftOptionCodeFoldingFeature") != std::string::npos) {
		failureReason = "File extension editor settings must not expose a separate Code folding checkbox.";
		return false;
	}
	if (panelContent.find("\"Code folding:\"") == std::string::npos || panelContent.find("new TSItem(\"~O~ff\", new TSItem(\"~L~eading\", new TSItem(\"~T~railing\", nullptr)))") == std::string::npos) {
		failureReason = "File extension editor settings must expose Code folding only as Off/Leading/Trailing.";
		return false;
	}
	if (editSetupContent.find("{\"CODE_FOLDING\",") != std::string::npos || snapshotContent.find("MRSETUP('CODE_FOLDING',") != std::string::npos) {
		failureReason = "CODE_FOLDING must not remain a canonical edit setting or serialized MRSETUP token.";
		return false;
	}
	if (editSetupContent.find("{\"CODE_FOLDING_POSITION\"") == std::string::npos || snapshotContent.find("MRSETUP('CODE_FOLDING_POSITION'") == std::string::npos) {
		failureReason = "CODE_FOLDING_POSITION must remain the canonical folding control.";
		return false;
	}
	if (warmupContent.find("bool MRFileEditor::foldingPipelineEnabled() const") == std::string::npos || warmupContent.find("return languageFeaturesEnabled() && effectiveEditSetupSettings().codeFolding;") == std::string::npos) {
		failureReason = "Editor folding pipeline must be gated by effective code folding position.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testFileExtensionCompilerProfileChoicesGuard(std::string &failureReason) {
	const std::string dialogPath = absolutePathFromCwd("dialogs/extensions/MRFileExtensionProfiles.cpp");
	const std::string draftsPath = absolutePathFromCwd("dialogs/extensions/MRFileExtensionProfileDrafts.cpp");
	std::string dialogContent;
	std::string draftsContent;
	std::string ioError;

	if (!readTextFile(dialogPath, dialogContent, ioError)) {
		failureReason = "Unable to read MRFileExtensionProfiles.cpp for compiler-profile choices guard: " + ioError;
		return false;
	}
	if (!readTextFile(draftsPath, draftsContent, ioError)) {
		failureReason = "Unable to read MRFileExtensionProfileDrafts.cpp for compiler-profile choices guard: " + ioError;
		return false;
	}
	if (dialogContent.find("std::vector<MRCompilerProfile> profiles = configuredCompilerProfiles();") == std::string::npos) {
		failureReason = "File extension compiler-profile drop list must read configured profiles.";
		return false;
	}
	if (dialogContent.find("detectedCompilerProfiles()") != std::string::npos || draftsContent.find("detectedCompilerProfiles()") != std::string::npos || dialogContent.find("detectedCompilerProfileIds()") != std::string::npos ||
	    draftsContent.find("detectedCompilerProfileIds()") != std::string::npos) {
		failureReason = "File extension compiler-profile UI must not synthesize compiler profile ids from auto-detection.";
		return false;
	}
	if (draftsContent.find("if (configuredCompilerProfiles().empty())") != std::string::npos) {
		failureReason = "File extension compiler-profile validation must not accept detected profiles only when configured profiles are empty.";
		return false;
	}
	if (draftsContent.find("return compilerProfileIdExists(id);") == std::string::npos) {
		failureReason = "File extension compiler-profile validation must accept configured compiler profile ids only.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testEditClipboardCommandRoutingGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("app/MRCommandRouter.cpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read MRCommandRouter.cpp for clipboard routing guard: " + ioError;
		return false;
	}
	if (content.find("case cmMrEditCutToBuffer:") == std::string::npos || content.find("return handleEditCutToSystemClipboard(window);") == std::string::npos ||
	    content.find("return handleEditCutToSystemClipboard(currentEditorCommandWindow());") == std::string::npos || content.find("case cmMrEditCopyToBuffer:") == std::string::npos ||
	    content.find("return handleEditCopyToSystemClipboard(window);") == std::string::npos || content.find("return handleEditCopyToSystemClipboard(currentEditorCommandWindow());") == std::string::npos ||
	    content.find("case cmMrEditPasteFromBuffer:") == std::string::npos || content.find("return dispatchEditorCommandEvent(window, cmPaste);") == std::string::npos ||
	    content.find("return dispatchEditorCommand(cmPaste, true);") == std::string::npos ||
	    content.find("if (window->hasBlock()) return handleCopyBlock(window);") != std::string::npos ||
	    content.find("currentEditorCommandWindow()->hasBlock()) return handleCopyBlock(currentEditorCommandWindow());") != std::string::npos ||
	    content.find("KeymapActionDispatchEntry{\"MRMAC_BLOCK_COPY_TO_CLIPBOARD\", KeymapDispatchKind::Custom, 0, KeymapWindowMethod::None, KeymapCustomAction::CopyMarkedBlockToSystemClipboard}") == std::string::npos ||
	    content.find("KeymapActionDispatchEntry{\"MRMAC_BLOCK_PASTE_FROM_CLIPBOARD\", KeymapDispatchKind::AppCommand, cmMrEditPasteFromBuffer, KeymapWindowMethod::None, KeymapCustomAction::None}") == std::string::npos ||
	    content.find("copyMarkedBlockToSystemClipboard(") == std::string::npos || content.find("captureBlockPayload(") == std::string::npos || content.find("deleteBlock(&errorText)") == std::string::npos || content.find("TClipboard::setText(") == std::string::npos) {
		failureReason = "Edit clipboard commands must route through the canonical editor/system clipboard surface.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testEditInsertModeCommandRoutingGuard(std::string &failureReason) {
	const std::string routerPath = absolutePathFromCwd("app/MRCommandRouter.cpp");
	const std::string menuPath = absolutePathFromCwd("app/MRMenuFactory.cpp");
	const std::string catalogPath = absolutePathFromCwd("keymap/MRKeymapActionCatalog.cpp");
	const std::string menuBarHeaderPath = absolutePathFromCwd("ui/MRMenuBar.hpp");
	const std::string menuBarSourcePath = absolutePathFromCwd("ui/MRMenuBar.cpp");
	const std::string appStatePath = absolutePathFromCwd("app/MRAppState.cpp");
	const std::string editorAppPath = absolutePathFromCwd("app/MREditorApp.cpp");
	std::string routerContent;
	std::string menuContent;
	std::string catalogContent;
	std::string menuBarHeaderContent;
	std::string menuBarSourceContent;
	std::string appStateContent;
	std::string editorAppContent;
	std::string ioError;

	if (!readTextFile(routerPath, routerContent, ioError)) {
		failureReason = "Unable to read MRCommandRouter.cpp for insert-mode routing guard: " + ioError;
		return false;
	}
	if (!readTextFile(menuPath, menuContent, ioError)) {
		failureReason = "Unable to read MRMenuFactory.cpp for insert-mode routing guard: " + ioError;
		return false;
	}
	if (!readTextFile(catalogPath, catalogContent, ioError)) {
		failureReason = "Unable to read MRKeymapActionCatalog.cpp for insert-mode routing guard: " + ioError;
		return false;
	}
	if (!readTextFile(menuBarHeaderPath, menuBarHeaderContent, ioError)) {
		failureReason = "Unable to read MRMenuBar.hpp for insert-mode routing guard: " + ioError;
		return false;
	}
	if (!readTextFile(menuBarSourcePath, menuBarSourceContent, ioError)) {
		failureReason = "Unable to read MRMenuBar.cpp for insert-mode routing guard: " + ioError;
		return false;
	}
	if (!readTextFile(appStatePath, appStateContent, ioError)) {
		failureReason = "Unable to read MRAppState.cpp for insert-mode routing guard: " + ioError;
		return false;
	}
	if (!readTextFile(editorAppPath, editorAppContent, ioError)) {
		failureReason = "Unable to read MREditorApp.cpp for insert-mode routing guard: " + ioError;
		return false;
	}
	if (catalogContent.find("MR_EDIT_TOGGLE_INSERT_MODE") == std::string::npos || routerContent.find("MR_EDIT_TOGGLE_INSERT_MODE") == std::string::npos || routerContent.find("cmMrEditToggleInsertMode") == std::string::npos) {
		failureReason = "Insert/overwrite must have a keymap action target and router command.";
		return false;
	}
	if (menuContent.find("~I~nsert [OFF]") == std::string::npos || menuContent.find("cmMrEditToggleInsertMode, kbIns") == std::string::npos) {
		failureReason = "Edit menu must expose Insert [ON/OFF] with the Insert key.";
		return false;
	}
	if (menuBarHeaderContent.find("setInsertModeMenuState") == std::string::npos || menuBarSourceContent.find("~I~nsert [ON]") == std::string::npos || menuBarSourceContent.find("~I~nsert [OFF]") == std::string::npos || menuBarSourceContent.find("findMenuItemByCommand(menu, cmMrEditToggleInsertMode)") == std::string::npos) {
		failureReason = "Menu bar must update the Insert [ON/OFF] menu label from editor state.";
		return false;
	}
	if (appStateContent.find("setCommandEnabled(cmMrEditToggleInsertMode, hasEditor)") == std::string::npos || editorAppContent.find("setInsertModeMenuState(win->insertModeEnabled())") == std::string::npos) {
		failureReason = "Insert mode command must be enabled for active editors and synced during idle.";
		return false;
	}

	MREditWindow window(TRect(0, 0, 80, 16), "insert-mode-keymap-toggle", 1016);
	if (window.getEditor() == nullptr) {
		failureReason = "Insert-mode keymap harness must create an editor.";
		return false;
	}
	window.getEditor()->setInsertModeEnabled(false);
	if (!dispatchMRKeymapAction("MR_EDIT_TOGGLE_INSERT_MODE", "<Insert>", &window) || !window.getEditor()->insertModeEnabled()) {
		failureReason = "MR_EDIT_TOGGLE_INSERT_MODE must toggle insert mode on.";
		return false;
	}
	if (!dispatchMRKeymapAction("MR_EDIT_TOGGLE_INSERT_MODE", "<Insert>", &window) || window.getEditor()->insertModeEnabled()) {
		failureReason = "MR_EDIT_TOGGLE_INSERT_MODE must toggle insert mode off.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testSearchMarkerRoutingAndTextMenuGuard(std::string &failureReason) {
	const std::string routerPath = absolutePathFromCwd("app/MRCommandRouter.cpp");
	const std::string menuPath = absolutePathFromCwd("app/MRMenuFactory.cpp");
	std::string routerContent;
	std::string menuContent;
	std::string ioError;

	if (!readTextFile(routerPath, routerContent, ioError)) {
		failureReason = "Unable to read MRCommandRouter.cpp for search-marker routing guard: " + ioError;
		return false;
	}
	if (!readTextFile(menuPath, menuContent, ioError)) {
		failureReason = "Unable to read MRMenuFactory.cpp for text-menu F4/ShiftF4 guard: " + ioError;
		return false;
	}
	if (routerContent.find("case cmMrSearchPushMarker:") == std::string::npos || routerContent.find("handleBlockAction(mrvmUiPushMarker(), \"Unable to push position onto marker stack.\")") == std::string::npos || routerContent.find("case cmMrSearchGetMarker:") == std::string::npos || routerContent.find("handleBlockAction(mrvmUiGetMarker(), \"No marker position on stack.\")") == std::string::npos) {
		failureReason = "Search marker commands must route through MRCommandRouter to mrvmUiPushMarker/mrvmUiGetMarker.";
		return false;
	}
	if (menuContent.find("TSubMenu *createTextMenu()") == std::string::npos || menuContent.find("cmMrSearchPushMarker, kbF4") == std::string::npos || menuContent.find("cmMrSearchGetMarker, kbShiftF4") == std::string::npos) {
		failureReason = "Text menu must expose F4/ShiftF4 marker stack actions.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testBlockHotkeyModifierRoutingGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("ui/MREditWindow.hpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read MREditWindow.hpp for block-hotkey guard: " + ioError;
		return false;
	}
	if (content.find("bool handleBuiltInBlockHotkeys(TEvent &event)") == std::string::npos || content.find("beginLineBlock();") == std::string::npos || content.find("beginColumnBlock();") == std::string::npos || content.find("beginStreamBlock();") == std::string::npos || content.find("toggleBlockVisibility()") == std::string::npos || content.find("clearBlock();") == std::string::npos) {
		failureReason = "Built-in block hotkeys must route to line/column/stream marking, visibility toggle and clear.";
		return false;
	}
	if (content.find("keyCode == kbF7 && !shift && !ctrl") == std::string::npos || content.find("keyCode == kbF7 && shift && !ctrl") == std::string::npos || content.find("keyCode == kbF7 && ctrl && !shift") == std::string::npos || content.find("keyCode == kbF9 && shift && !ctrl") == std::string::npos || content.find("keyCode == kbF9 && ctrl && !shift") == std::string::npos) {
		failureReason = "Block hotkey modifier distinctions must remain explicit.";
		return false;
	}
	if (content.find("void applyPostInputBlockPolicy(") == std::string::npos || content.find("(void)selectionCollapsedBeforeEditorInput;") == std::string::npos) {
		failureReason = "Post-input block policy must remain a dummy surface.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testInterWindowBlockSourceTargetGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("app/MRCommandRouter.cpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read MRCommandRouter.cpp for inter-window block guard: " + ioError;
		return false;
	}
	if (content.find("case cmMrBlockWindowCopy:") == std::string::npos || content.find("return handleWindowCopyBlock(currentEditorCommandWindow());") == std::string::npos || content.find("case cmMrBlockWindowMove:") == std::string::npos || content.find("return handleWindowMoveBlock(currentEditorCommandWindow());") == std::string::npos || content.find("mrShowWindowListDialog(mrwlSelectLinkTarget, window)") == std::string::npos || content.find("mrvmUiCopyBlockFromWindow(") != std::string::npos || content.find("mrvmUiMoveBlockFromWindow(") != std::string::npos) {
		failureReason = "Inter-window copy and move must route through the command router target selection and active editor command target.";
		return false;
	}
	failureReason.clear();
	return true;
}


bool testAboutQuoteReadmeExtractionGuard(std::string &failureReason) {
	const std::string readmePath = absolutePathFromCwd("README.md");
	const std::string generatedPath = absolutePathFromCwd("app/MRAboutQuotes.generated.hpp");
	std::string readmeContent;
	std::string generatedContent;
	std::string ioError;
	std::size_t readmeQuoteCount = 0;
	std::size_t generatedQuoteCount = 0;
	std::size_t lineStart = 0;
	bool inQuoteBlock = false;
	bool quoteBlockDone = false;

	if (!readTextFile(readmePath, readmeContent, ioError)) {
		failureReason = "Unable to read README.md for about quote guard: " + ioError;
		return false;
	}
	if (!readTextFile(generatedPath, generatedContent, ioError)) {
		failureReason = "Unable to read MRAboutQuotes.generated.hpp for about quote guard: " + ioError;
		return false;
	}

	while (lineStart <= readmeContent.size() && !quoteBlockDone) {
		std::size_t lineEnd = readmeContent.find('\n', lineStart);
		std::string line = lineEnd == std::string::npos ? readmeContent.substr(lineStart) : readmeContent.substr(lineStart, lineEnd - lineStart);
		std::size_t pos = 1;

		if (!line.empty() && line[0] == '>') {
			while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
				++pos;
			if (pos < line.size() && line[pos] == '-') {
				++pos;
				while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
					++pos;
				if (pos < line.size()) {
					inQuoteBlock = true;
					++readmeQuoteCount;
				}
			} else if (inQuoteBlock) {
				quoteBlockDone = true;
			}
		} else if (inQuoteBlock) {
			quoteBlockDone = true;
		}

		if (lineEnd == std::string::npos)
			break;
		lineStart = lineEnd + 1;
	}

	lineStart = 0;
	while (lineStart <= generatedContent.size()) {
		std::size_t lineEnd = generatedContent.find('\n', lineStart);
		std::string line = lineEnd == std::string::npos ? generatedContent.substr(lineStart) : generatedContent.substr(lineStart, lineEnd - lineStart);

		if (line.rfind("    \"", 0) == 0)
			++generatedQuoteCount;
		if (lineEnd == std::string::npos)
			break;
		lineStart = lineEnd + 1;
	}

	if (readmeQuoteCount == 0) {
		failureReason = "README.md about quote block was not detected.";
		return false;
	}
	if (generatedQuoteCount != readmeQuoteCount) {
		failureReason = "Generated about quote count mismatch: README has " + std::to_string(readmeQuoteCount) + ", generated header has " + std::to_string(generatedQuoteCount) + ".";
		return false;
	}
	if (generatedContent.find("\342\200\234") != std::string::npos || generatedContent.find("\342\200\235") != std::string::npos || generatedContent.find("\342\200\236") != std::string::npos) {
		failureReason = "Generated about quotes must normalize typographic double quotes.";
		return false;
	}
	if (generatedContent.find("Bjarne Stroustrup") == std::string::npos || generatedContent.find("Linus Torvalds") == std::string::npos || generatedContent.find("Donald Knuth") == std::string::npos) {
		failureReason = "Generated about quotes lost README entries after the first quote.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testBlockPasteFreeCursorTargetGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("mrmac/MRVM.cpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read MRVM.cpp for block-paste free-cursor target guard: " + ioError;
		return false;
	}
	if (content.find("struct BlockPasteTarget") != std::string::npos || content.find("materializeEditorPasteTarget(") != std::string::npos || content.find("MRBlockMutation") != std::string::npos || content.find("MRBlockSnapshot") != std::string::npos || content.find("MRBlockSelection") != std::string::npos) {
		failureReason = "Old block paste and column geometry helpers must remain removed.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testColumnIndentUndentWiringGuard(std::string &failureReason) {
	std::string routerContent;
	std::string blockOpsHeaderContent;
	std::string blockOpsSourceContent;
	std::string vmContent;
	std::string ioError;

	if (!readTextFile(absolutePathFromCwd("app/MRCommandRouter.cpp"), routerContent, ioError)) {
		failureReason = "Unable to read MRCommandRouter.cpp for column indent/undent wiring guard: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRFileEditor/MRFEBlockOps.hpp"), blockOpsHeaderContent, ioError)) {
		failureReason = "Unable to read MRFEBlockOps.hpp for column indent/undent wiring guard: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRFileEditor/MRFEBlockOps.cpp"), blockOpsSourceContent, ioError)) {
		failureReason = "Unable to read MRFEBlockOps.cpp for column indent/undent wiring guard: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("mrmac/MRVM.cpp"), vmContent, ioError)) {
		failureReason = "Unable to read MRVM.cpp for column indent/undent wiring guard: " + ioError;
		return false;
	}
	if (routerContent.find("return handleIndentBlock(currentEditorCommandWindow());") == std::string::npos || routerContent.find("return handleUndentBlock(currentEditorCommandWindow());") == std::string::npos || routerContent.find("case cmMrBlockIndent:") == std::string::npos || routerContent.find("case cmMrBlockUndent:") == std::string::npos) {
		failureReason = "Column block indent/undent commands must route through MRCommandRouter.";
		return false;
	}
	if (blockOpsHeaderContent.find("shiftCurrentBlockToTab") == std::string::npos || blockOpsSourceContent.find("BlockOperation::Indent") == std::string::npos || blockOpsSourceContent.find("BlockOperation::Undent") == std::string::npos || blockOpsSourceContent.find("std::array<ShiftFunction") == std::string::npos || blockOpsSourceContent.find("\"shift-column-block\"") == std::string::npos) {
		failureReason = "Column block indent/undent must be implemented by MRFEBlockOps.";
		return false;
	}
	if (vmContent.find("configuredColumnBlockMoveLeavesSpace(") != std::string::npos || vmContent.find("shiftCurrentBlockIndent(") != std::string::npos || vmContent.find("line.replace(start, static_cast<std::size_t>(removeCount),") != std::string::npos) {
		failureReason = "Old VM column indent/undent helpers must remain removed.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testTabstopIndentingOps(std::string &failureReason) {
	const std::string source = "$MACRO TabstopOpsProbe;\n"
	                           "DEF_STR(S);\n"
	                           "DEF_INT(I);\n"
	                           "TAB_EXPAND := TRUE;\n"
	                           "S := CHAR(9) + 'A';\n"
	                           "I := 2;\n"
	                           "EXPAND_TABS(S, I);\n"
	                           "SET_GLOBAL_INT('TABOPS_EXP_I', I);\n"
	                           "SET_GLOBAL_INT('TABOPS_EXP_LEN', LENGTH(S));\n"
	                           "SET_GLOBAL_INT('TABOPS_EXP_POSA', POS('A', S));\n"
	                           "SET_GLOBAL_INT('TABOPS_EXP_FIRST', ASCII(COPY(S, 1, 1)));\n"
	                           "TABS_TO_SPACES(S);\n"
	                           "SET_GLOBAL_INT('TABOPS_SPC_FIRST', ASCII(COPY(S, 1, 1)));\n"
	                           "SET_GLOBAL_INT('TABOPS_SPC_POSA', POS('A', S));\n"
	                           "SET_GLOBAL_INT('TABOPS_SPC_LEN', LENGTH(S));\n"
	                           "INSERT_MODE := FALSE;\n"
	                           "TAB_RIGHT;\n"
	                           "SET_GLOBAL_INT('TABOPS_COL_AFTER_TAB', C_COL);\n"
	                           "SET_GLOBAL_INT('TABOPS_TABCHAR', ASCII(COPY(GET_LINE, 1, 1)));\n"
	                           "TAB_LEFT;\n"
	                           "INDENT;\n"
	                           "UNDENT;\n"
	                           "SET_INDENT_LEVEL;\n"
	                           "SET_GLOBAL_INT('TABOPS_INDENT', INDENT_LEVEL);\n"
	                           "END_MACRO;\n";
	std::vector<unsigned char> bytecode;
	int entryOffset = -1;
	std::string entryName;
	std::string compileError;
	MRMacroExecutionProfile profile;
	std::vector<std::string> unsupported;
	MRMacroStagedExecutionInput input;
	MRMacroStagedJobResult result;
	mr::editor::CommitResult commit;
	std::string vmError;
	int expectedPosA = 0;
	std::vector<std::string> savedOrder;
	std::map<std::string, int> savedInts;
	std::map<std::string, std::string> savedStrings;

	if (!compileSource(source, bytecode, entryOffset, entryName, compileError)) {
		failureReason = "Compile failed for tabstop/indenting probe: " + compileError;
		return false;
	}

	profile = mrvmAnalyzeBytecode(bytecode.data(), bytecode.size());
	if (!mrvmCanRunStagedInBackground(profile)) {
		unsupported = mrvmUnsupportedStagedSymbols(profile);
		failureReason = "Tabstop/indenting probe should be staged-background eligible.";
		if (!unsupported.empty()) failureReason += " Unsupported symbol example: " + unsupported.front() + ".";
		return false;
	}

	mrvmUiCopyGlobals(savedOrder, savedInts, savedStrings);
	struct GlobalsRestore {
		std::vector<std::string> order;
		std::map<std::string, int> ints;
		std::map<std::string, std::string> strings;

		GlobalsRestore(std::vector<std::string> savedOrderRef, std::map<std::string, int> savedIntsRef, std::map<std::string, std::string> savedStringsRef) : order(std::move(savedOrderRef)), ints(std::move(savedIntsRef)), strings(std::move(savedStringsRef)) {
		}

		~GlobalsRestore() {
			mrvmUiReplaceGlobals(order, ints, strings);
		}
	} restoreGuard(savedOrder, savedInts, savedStrings);

	input.document.setText("abcd\n");
	input.baseVersion = input.document.version();
	input.cursorOffset = 0;
	input.selectionStart = 0;
	input.selectionEnd = 0;
	input.tabExpand = true;
	input.insertMode = false;
	input.indentLevel = 1;
	input.pageLines = 20;
	input.fileName = "/tmp/tabstop_ops_probe.txt";

	result = mrvmRunBytecodeStagedBackground(bytecode.data() + static_cast<std::size_t>(entryOffset), bytecode.size() - static_cast<std::size_t>(entryOffset), input);
	if (result.hadError) {
		if (firstVmError(result.logLines, vmError)) failureReason = "Tabstop/indenting probe produced VM error: " + vmError;
		else
			failureReason = "Tabstop/indenting probe produced VM error.";
		return false;
	}
	commit = input.document.tryApply(result.transaction);
	if (!commit.applied()) {
		failureReason = "Tabstop/indenting probe should modify the staged document.";
		return false;
	}

	{
		const MREditSetupSettings settings = configuredEditSetupSettings();
		expectedPosA = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, 1);
	}

	if (!checkGlobalInt(result.globalInts, "TABOPS_EXP_I", expectedPosA, failureReason)) return false;
	if (!checkGlobalInt(result.globalInts, "TABOPS_EXP_LEN", expectedPosA, failureReason)) return false;
	if (!checkGlobalInt(result.globalInts, "TABOPS_EXP_POSA", expectedPosA, failureReason)) return false;
	if (!checkGlobalInt(result.globalInts, "TABOPS_EXP_FIRST", 9, failureReason)) return false;
	if (!checkGlobalInt(result.globalInts, "TABOPS_SPC_FIRST", 32, failureReason)) return false;
	if (!checkGlobalInt(result.globalInts, "TABOPS_SPC_POSA", expectedPosA, failureReason)) return false;
	if (!checkGlobalInt(result.globalInts, "TABOPS_SPC_LEN", expectedPosA, failureReason)) return false;
	if (!checkGlobalInt(result.globalInts, "TABOPS_COL_AFTER_TAB", 2, failureReason)) return false;
	if (!checkGlobalInt(result.globalInts, "TABOPS_TABCHAR", 9, failureReason)) return false;
	if (!checkGlobalInt(result.globalInts, "TABOPS_INDENT", 1, failureReason)) return false;

	if (!expectCompileError("$MACRO Bad;\nDEF_STR(S);\nEXPAND_TABS(S, S);\nEND_MACRO;\n", "Type mismatch or syntax error.", failureReason)) return false;

	failureReason.clear();
	return true;
}

bool testKeyIn(std::string &failureReason) {
	static const char source[] = "$MACRO KeyOnCtrlP TO <CtrlP> FROM EDIT;\n"
	                             "KEY_IN('X<Enter>Y');\n"
	                             "SET_GLOBAL_INT('KEYIN_HIT', 1);\n"
	                             "SET_GLOBAL_INT('KEYIN_ERR', ERROR_LEVEL);\n"
	                             "END_MACRO;\n";
	static const char macroPath[] = "/tmp/mr_keyin_probe.mrmac";
	size_t bytecodeSize = 0;
	unsigned char *bytecode = compile_macro_code(source, &bytecodeSize);
	std::string loaderSource;
	VirtualMachine vm;
	std::string executedMacroName;
	std::vector<std::string> globalOrder;
	std::map<std::string, int> globalInts;
	std::map<std::string, std::string> globalStrings;
	MRMacroExecutionProfile profile;
	bool ok = false;

	if (bytecode == NULL) {
		failureReason = std::string("Compilation failed: ") + get_last_compile_error();
		return false;
	}
	profile = mrvmAnalyzeBytecode(bytecode, bytecodeSize);
	if (mrvmCanRunStagedInBackground(profile)) {
		failureReason = "KEY_IN macro should not be staged-background eligible.";
		std::free(bytecode);
		return false;
	}
	if (!containsText(mrvmUnsupportedStagedSymbols(profile), "KEY_IN")) {
		failureReason = "KEY_IN should be reported as unsupported staged symbol.";
		std::free(bytecode);
		return false;
	}
	std::free(bytecode);

	if (!writeTextFile(std::string(macroPath), std::string(source))) {
		failureReason = "Unable to create KEY_IN probe macro file.";
		return false;
	}

	loaderSource = "$MACRO Main;\nLOAD_MACRO_FILE('";
	loaderSource += macroPath;
	loaderSource += "');\nEND_MACRO;\n";
	bytecode = compile_macro_code(loaderSource.c_str(), &bytecodeSize);
	if (bytecode == NULL) {
		failureReason = std::string("Compilation failed: ") + get_last_compile_error();
		std::remove(macroPath);
		return false;
	}
	vm.execute(bytecode, bytecodeSize);
	std::free(bytecode);

	ok = mrvmRunAssignedMacroForKey(kbCtrlP, 0, executedMacroName, nullptr) && executedMacroName == "KeyOnCtrlP";
	if (!ok) {
		failureReason = "Ctrl+P macro dispatch failed.";
		std::remove(macroPath);
		return false;
	}

	mrvmUiCopyGlobals(globalOrder, globalInts, globalStrings);
	if (!checkGlobalInt(globalInts, "KEYIN_HIT", 1, failureReason)) {
		std::remove(macroPath);
		return false;
	}
	if (!checkGlobalInt(globalInts, "KEYIN_ERR", 1001, failureReason)) {
		std::remove(macroPath);
		return false;
	}

	if (!expectCompileError("$MACRO Bad;\nKEY_IN(1);\nEND_MACRO;\n", "Type mismatch or syntax error.", failureReason)) {
		std::remove(macroPath);
		return false;
	}

	std::remove(macroPath);
	failureReason.clear();
	return true;
}

bool testCreateGlobalStrOperation(std::string &failureReason) {
	const std::string source = "$MACRO CreateGlobalStrProbe;\n"
	                           "DEF_INT(Kind, Seen);\n"
	                           "DEF_STR(Name);\n"
	                           "CREATE_GLOBAL_STR('CSTR_PROBE', 'Alpha');\n"
	                           "Seen := 0;\n"
	                           "Name := FIRST_GLOBAL(Kind);\n"
	                           "WHILE LENGTH(Name) > 0 DO\n"
	                           "  IF (Name = 'CSTR_PROBE') AND (Kind = 0) THEN\n"
	                           "    Seen := 1;\n"
	                           "    Name := '';\n"
	                           "  ELSE\n"
	                           "    Name := NEXT_GLOBAL(Kind);\n"
	                           "  END;\n"
	                           "END;\n"
	                           "SET_GLOBAL_INT('CSTR_PROBE_SEEN', Seen);\n"
	                           "SET_GLOBAL_INT('CSTR_PROBE_LEN', LENGTH(GLOBAL_STR('CSTR_PROBE')));\n"
	                           "END_MACRO;\n";
	std::vector<unsigned char> bytecode;
	int entryOffset = -1;
	std::string macroName;
	std::string compileError;
	VirtualMachine vm;
	std::string vmError;
	MRMacroExecutionProfile profile;
	std::vector<std::string> unsupported;
	std::vector<std::string> savedOrder;
	std::map<std::string, int> savedInts;
	std::map<std::string, std::string> savedStrings;
	std::vector<std::string> globalOrder;
	std::map<std::string, int> globalInts;
	std::map<std::string, std::string> globalStrings;
	std::map<std::string, std::string>::const_iterator strIt;

	if (!compileSource(source, bytecode, entryOffset, macroName, compileError)) {
		failureReason = "Compile failed for CREATE_GLOBAL_STR probe: " + compileError;
		return false;
	}
	profile = mrvmAnalyzeBytecode(bytecode.data(), bytecode.size());
	if (!mrvmCanRunStagedInBackground(profile)) {
		unsupported = mrvmUnsupportedStagedSymbols(profile);
		failureReason = "CREATE_GLOBAL_STR probe should be staged-background eligible.";
		if (!unsupported.empty()) failureReason += " Unsupported symbol example: " + unsupported.front() + ".";
		return false;
	}
	unsupported = mrvmUnsupportedStagedSymbols(profile);
	if (containsText(unsupported, "CREATE_GLOBAL_STR")) {
		failureReason = "CREATE_GLOBAL_STR must be treated as supported staged symbol.";
		return false;
	}

	mrvmUiCopyGlobals(savedOrder, savedInts, savedStrings);
	struct GlobalsRestore {
		std::vector<std::string> order;
		std::map<std::string, int> ints;
		std::map<std::string, std::string> strings;

		GlobalsRestore(std::vector<std::string> savedOrderRef, std::map<std::string, int> savedIntsRef, std::map<std::string, std::string> savedStringsRef) : order(std::move(savedOrderRef)), ints(std::move(savedIntsRef)), strings(std::move(savedStringsRef)) {
		}

		~GlobalsRestore() {
			mrvmUiReplaceGlobals(order, ints, strings);
		}
	} restoreGuard(savedOrder, savedInts, savedStrings);

	vm.executeAt(bytecode.data(), bytecode.size(), static_cast<size_t>(entryOffset), std::string(), macroName, true, true);
	if (firstVmError(vm.log, vmError)) {
		failureReason = "CREATE_GLOBAL_STR probe produced VM error: " + vmError;
		return false;
	}

	mrvmUiCopyGlobals(globalOrder, globalInts, globalStrings);
	if (!checkGlobalInt(globalInts, "CSTR_PROBE_SEEN", 1, failureReason)) return false;
	if (!checkGlobalInt(globalInts, "CSTR_PROBE_LEN", 5, failureReason)) return false;
	strIt = globalStrings.find("CSTR_PROBE");
	if (strIt == globalStrings.end() || strIt->second != "Alpha") {
		failureReason = "CREATE_GLOBAL_STR did not persist expected string value.";
		return false;
	}

	if (!expectCompileError("$MACRO Bad;\nCREATE_GLOBAL_STR('X', 1);\nEND_MACRO;\n", "Type mismatch or syntax error.", failureReason)) return false;

	failureReason.clear();
	return true;
}

bool testMarqueeProcWiringGuard(std::string &failureReason) {
	const std::string vmPath = absolutePathFromCwd("mrmac/MRVM.cpp");
	std::string content;
	std::string ioError;
	std::vector<unsigned char> bytecode;
	std::string compileError;
	MRMacroExecutionProfile profile;
	std::vector<std::string> unsupported;
	static const char kSource[] = "$MACRO Probe;\n"
	                              "MARQUEE('normal');\n"
	                              "MARQUEE_WARNING('warn');\n"
	                              "MARQUEE_ERROR('err');\n"
	                              "END_MACRO;\n";

	if (!readTextFile(vmPath, content, ioError)) {
		failureReason = "Unable to read MRVM.cpp for MARQUEE proc guard: " + ioError;
		return false;
	}
	if (content.find("name == \"MARQUEE\" || name == \"MARQUEE_WARNING\" || name == \"MARQUEE_ERROR\"") == std::string::npos) {
		failureReason = "MRVM OP_PROC dispatcher must handle MARQUEE, MARQUEE_WARNING and MARQUEE_ERROR.";
		return false;
	}
	if (!compileBytecode(kSource, bytecode, compileError)) {
		failureReason = "Unable to compile MARQUEE proc probe: " + compileError;
		return false;
	}
	profile = mrvmAnalyzeBytecode(bytecode.data(), bytecode.size());
	if (profile.procCount < 3) {
		failureReason = "MARQUEE probe must compile as OP_PROC.";
		return false;
	}
	if (!mrvmCanRunStagedInBackground(profile)) {
		failureReason = "MARQUEE proc probe must be staged-background eligible.";
		return false;
	}
	unsupported = mrvmUnsupportedStagedSymbols(profile);
	if (!unsupported.empty()) {
		failureReason = "MARQUEE proc names must be accepted staged symbols.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testDeferredUiPlaybackMailboxGuard(std::string &failureReason) {
	const std::string dispatchPath = absolutePathFromCwd("coprocessor/MRCoprocessorDispatch.cpp");
	const std::string dispatchHeaderPath = absolutePathFromCwd("coprocessor/MRCoprocessorDispatch.hpp");
	std::string dispatchContent;
	std::string dispatchHeaderContent;
	std::string ioError;

	if (!readTextFile(dispatchPath, dispatchContent, ioError)) {
		failureReason = "Unable to read MRCoprocessorDispatch.cpp for deferred playback guard: " + ioError;
		return false;
	}
	if (!readTextFile(dispatchHeaderPath, dispatchHeaderContent, ioError)) {
		failureReason = "Unable to read MRCoprocessorDispatch.hpp for deferred playback guard: " + ioError;
		return false;
	}
	if (dispatchHeaderContent.find("void pumpDeferredMacroUiPlayback();") == std::string::npos) {
		failureReason = "Deferred UI playback pump must be declared in MRCoprocessorDispatch.hpp.";
		return false;
	}
	if (dispatchContent.find("struct MacroScreenModel") == std::string::npos || dispatchContent.find("struct MacroScreenView") == std::string::npos || dispatchContent.find("struct DeferredUiRenderGateway") == std::string::npos) {
		failureReason = "Deferred playback must define MacroScreenModel, MacroScreenView and DeferredUiRenderGateway.";
		return false;
	}
	if (dispatchContent.find("DeferredUiRenderGateway::renderDeferredCommand(command)") == std::string::npos) {
		failureReason = "MacroScreenView must route rendering through the deferred UI render gateway.";
		return false;
	}
	if (dispatchContent.find("queueDeferredMacroUiPlayback(") == std::string::npos) {
		failureReason = "Staged macro completion must queue deferred UI playback.";
		return false;
	}
	if (dispatchContent.find("Queued deferred UI playback for macro") == std::string::npos) {
		failureReason = "Deferred playback queueing must be logged for staged macros.";
		return false;
	}
	if (dispatchContent.find("if (command.type == mrducDelay)") == std::string::npos) {
		failureReason = "Deferred playback loop must handle DELAY cooperatively.";
		return false;
	}
	if (dispatchContent.find("Applied deferred UI commands for macro") != std::string::npos) {
		failureReason = "Legacy immediate deferred-UI apply log must not remain.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testDeferredUiMutationEpochGuard(std::string &failureReason) {
	const std::string vmPath = absolutePathFromCwd("mrmac/MRVM.cpp");
	const std::string vmHeaderPath = absolutePathFromCwd("mrmac/MRVM.hpp");
	const std::string screenPath = absolutePathFromCwd("mrmac/ui/conventional/MRVMScreen.cpp");
	const std::string screenStatePath = absolutePathFromCwd("mrmac/ui/conventional/MRVMScreenState.cpp");
	const std::string editorBridgePath = absolutePathFromCwd("mrmac/ui/conventional/MRVMEditor.cpp");
	const std::string dispatchPath = absolutePathFromCwd("coprocessor/MRCoprocessorDispatch.cpp");
	const std::string appPath = absolutePathFromCwd("app/MREditorApp.cpp");
	const std::string menuBarPath = absolutePathFromCwd("ui/MRMenuBar.cpp");
	const std::string framePath = absolutePathFromCwd("ui/MRFrame.cpp");
	const std::string indicatorPath = absolutePathFromCwd("ui/MRIndicator.hpp");
	const std::string statusLinePath = absolutePathFromCwd("ui/MRStatusLine.hpp");
	std::string vmContent;
	std::string vmHeaderContent;
	std::string screenContent;
	std::string screenStateContent;
	std::string editorBridgeContent;
	std::string dispatchContent;
	std::string appContent;
	std::string menuBarContent;
	std::string frameContent;
	std::string indicatorContent;
	std::string statusLineContent;
	std::string ioError;

	if (!readTextFile(vmPath, vmContent, ioError)) {
		failureReason = "Unable to read MRVM.cpp for deferred UI mutation-epoch guard: " + ioError;
		return false;
	}
	if (!readTextFile(vmHeaderPath, vmHeaderContent, ioError)) {
		failureReason = "Unable to read MRVM.hpp for deferred UI mutation-epoch guard: " + ioError;
		return false;
	}
	if (!readTextFile(screenPath, screenContent, ioError)) {
		failureReason = "Unable to read MRVMScreen.cpp for deferred UI mutation-epoch guard: " + ioError;
		return false;
	}
	if (!readTextFile(screenStatePath, screenStateContent, ioError)) {
		failureReason = "Unable to read MRVMScreenState.cpp for deferred UI mutation-epoch guard: " + ioError;
		return false;
	}
	if (!readTextFile(editorBridgePath, editorBridgeContent, ioError)) {
		failureReason = "Unable to read MRVMEditor.cpp for deferred UI mutation-epoch guard: " + ioError;
		return false;
	}
	if (!readTextFile(dispatchPath, dispatchContent, ioError)) {
		failureReason = "Unable to read MRCoprocessorDispatch.cpp for mutation-epoch guard: " + ioError;
		return false;
	}
	if (!readTextFile(appPath, appContent, ioError)) {
		failureReason = "Unable to read MREditorApp.cpp for mutation-epoch guard: " + ioError;
		return false;
	}
	if (!readTextFile(menuBarPath, menuBarContent, ioError)) {
		failureReason = "Unable to read MRMenuBar.cpp for mutation-epoch guard: " + ioError;
		return false;
	}
	if (!readTextFile(framePath, frameContent, ioError)) {
		failureReason = "Unable to read MRFrame.cpp for mutation-epoch guard: " + ioError;
		return false;
	}
	if (!readTextFile(indicatorPath, indicatorContent, ioError)) {
		failureReason = "Unable to read MRIndicator.hpp for mutation-epoch guard: " + ioError;
		return false;
	}
	if (!readTextFile(statusLinePath, statusLineContent, ioError)) {
		failureReason = "Unable to read MRStatusLine.hpp for mutation-epoch guard: " + ioError;
		return false;
	}

	if (vmHeaderContent.find("std::uint64_t mrvmUiScreenMutationEpoch() noexcept;") == std::string::npos || vmHeaderContent.find("void mrvmUiInvalidateScreenBase() noexcept;") == std::string::npos || vmHeaderContent.find("void mrvmUiTouchScreenMutationEpoch() noexcept;") == std::string::npos || vmHeaderContent.find("void mrvmUiBeginMacroScreenBatch() noexcept;") == std::string::npos || vmHeaderContent.find("void mrvmUiEndMacroScreenBatch() noexcept;") == std::string::npos || vmHeaderContent.find("bool mrvmUiRenderFacadeRenderDeferredCommand(const MRMacroDeferredUiCommand &command);") == std::string::npos || vmHeaderContent.find("bool mrvmUiEraseCurrentWindow();") == std::string::npos) {
		failureReason = "MRVM.hpp must expose screen mutation epoch and base invalidation APIs.";
		return false;
	}
	if (screenContent.find("returnWithMacroScreenMutation(") == std::string::npos || screenContent.find("returnWithDirectScreenMutation(") == std::string::npos || screenContent.find("std::uint64_t mrvmUiScreenMutationEpoch() noexcept") == std::string::npos || screenContent.find("void mrvmUiInvalidateScreenBase() noexcept") == std::string::npos || screenContent.find("void mrvmUiTouchScreenMutationEpoch() noexcept") == std::string::npos ||
	    screenContent.find("void mrvmUiBeginMacroScreenBatch() noexcept") == std::string::npos || screenContent.find("void mrvmUiEndMacroScreenBatch() noexcept") == std::string::npos || vmContent.find("struct UiRenderFacade") == std::string::npos || vmContent.find("bool mrvmUiRenderFacadeRenderDeferredCommand(const MRMacroDeferredUiCommand &command)") == std::string::npos || editorBridgeContent.find("bool mrvmUiEraseCurrentWindow()") == std::string::npos || editorBridgeContent.find("return returnWithDirectScreenMutation(mrvmEditorEraseCurrentWindow());") == std::string::npos || vmContent.find("ok = mrvmUiEraseCurrentWindow();") == std::string::npos) {
		failureReason = "MRVM screen layer must maintain the mutation facade and route editor bridge mutations through it.";
		return false;
	}
	if (screenStateContent.find("UiScreenStateFacade::nextGeneration() noexcept") == std::string::npos || screenStateContent.find("mrvmRuntimeStateSize(\"macroScreen\", \"mutationEpoch\", 1)") == std::string::npos || screenStateContent.find("mrvmStoreRuntimeStateSize(\"macroScreen\", \"mutationEpoch\", generation)") == std::string::npos || screenStateContent.find("UiScreenStateFacade::noteMacroOverlayMutation() noexcept") == std::string::npos || screenStateContent.find("UiScreenStateFacade::noteBaseMutation() noexcept") == std::string::npos || screenStateContent.find("MacroCellGrid::loadState()") == std::string::npos || screenStateContent.find("MacroCellGrid::storeState()") == std::string::npos) {
		failureReason = "MRVM screen state and its mutation epoch must be owned by the central runtime K/V.";
		return false;
	}
	if (screenContent.find("g_macroScreenMutationEpoch") != std::string::npos || screenStateContent.find("g_macroScreenMutationEpoch") != std::string::npos || screenContent.find("g_screenStateCoordinator") != std::string::npos || screenStateContent.find("g_screenStateCoordinator") != std::string::npos) {
		failureReason = "MRVM screen state must not retain a parallel global coordinator.";
		return false;
	}
	if (dispatchContent.find("observedScreenEpoch") == std::string::npos || dispatchContent.find("liveEpoch != playback.observedScreenEpoch") == std::string::npos || dispatchContent.find("mrvmUiScreenMutationEpoch()") == std::string::npos || dispatchContent.find("mrvmUiRenderFacadeRenderDeferredCommand(command)") == std::string::npos) {
		failureReason = "Deferred UI playback must invalidate projection when global screen mutation epoch changes.";
		return false;
	}
	if (appContent.find("shouldInvalidateScreenBaseForEvent(") == std::string::npos || appContent.find("mrvmUiInvalidateScreenBase();") == std::string::npos || appContent.find("shouldInvalidateScreenBaseForEvent(originalWhat)") == std::string::npos) {
		failureReason = "MREditorApp must invalidate base screen state after UI-driving input handling.";
		return false;
	}
	if (menuBarContent.find("mrvmUiInvalidateScreenBase();") == std::string::npos || frameContent.find("mrvmUiInvalidateScreenBase();") == std::string::npos || indicatorContent.find("mrvmUiInvalidateScreenBase();") == std::string::npos || statusLineContent.find("mrvmUiInvalidateScreenBase();") == std::string::npos) {
		failureReason = "Core UI render sinks must invalidate base screen state on direct drawing.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testUiMessageBoxProcGuard(std::string &failureReason) {
	const std::string headerPath = absolutePathFromCwd("mrmac/mrmac.h");
	const std::string compilerPath = absolutePathFromCwd("mrmac/mrmac.c");
	const std::string vmPath = absolutePathFromCwd("mrmac/MRVM.cpp");
	const std::string deferredHeaderPath = absolutePathFromCwd("mrmac/ui/conventional/MRVMDeferredUi.hpp");
	const std::string deferredPath = absolutePathFromCwd("mrmac/ui/conventional/MRVMDeferredUi.cpp");
	const std::string syntaxPath = absolutePathFromCwd("ui/MRSyntax.cpp");
	std::string headerContent;
	std::string compilerContent;
	std::string vmContent;
	std::string deferredHeaderContent;
	std::string deferredContent;
	std::string syntaxContent;
	std::string ioError;
	std::vector<unsigned char> bytecode;
	std::string compileError;
	const std::string legacyKeyword = std::string("TV") + "CALL";
	MRMacroExecutionProfile profile;
	std::vector<std::string> unsupported;
	static const char kSource[] = "$MACRO Probe;\n"
	                              "UI_MESSAGEBOX('hello');\n"
	                              "END_MACRO;\n";

	if (!readTextFile(headerPath, headerContent, ioError)) {
		failureReason = "Unable to read mrmac.h for UI_MESSAGEBOX guard: " + ioError;
		return false;
	}
	if (!readTextFile(compilerPath, compilerContent, ioError)) {
		failureReason = "Unable to read mrmac.c for UI_MESSAGEBOX guard: " + ioError;
		return false;
	}
	if (!readTextFile(vmPath, vmContent, ioError)) {
		failureReason = "Unable to read MRVM.cpp for UI_MESSAGEBOX guard: " + ioError;
		return false;
	}
	if (!readTextFile(deferredHeaderPath, deferredHeaderContent, ioError)) {
		failureReason = "Unable to read MRVMDeferredUi.hpp for UI_MESSAGEBOX guard: " + ioError;
		return false;
	}
	if (!readTextFile(deferredPath, deferredContent, ioError)) {
		failureReason = "Unable to read MRVMDeferredUi.cpp for UI_MESSAGEBOX guard: " + ioError;
		return false;
	}
	if (!readTextFile(syntaxPath, syntaxContent, ioError)) {
		failureReason = "Unable to read MRSyntax.cpp for UI_MESSAGEBOX guard: " + ioError;
		return false;
	}
	if (headerContent.find("OP_" + legacyKeyword) != std::string::npos || compilerContent.find("TOK_" + legacyKeyword) != std::string::npos || compilerContent.find("OP_" + legacyKeyword) != std::string::npos || vmContent.find("OP_" + legacyKeyword) != std::string::npos || deferredHeaderContent.find("dispatchDeferredUi" + legacyKeyword) != std::string::npos || deferredContent.find("dispatchDeferredUi" + legacyKeyword) != std::string::npos || syntaxContent.find("\"" + legacyKeyword + "\"") != std::string::npos) {
		failureReason = "Legacy UI-call lexer/parser/opcode/runtime/syntax surface must be removed.";
		return false;
	}
	if (compilerContent.find("PROC_SIG1(\"UI_MESSAGEBOX\"") == std::string::npos || vmContent.find("name == \"UI_MESSAGEBOX\"") == std::string::npos || deferredContent.find("DeferredVisualUiProc::MessageBox") == std::string::npos || deferredContent.find("mrducMessageBox") == std::string::npos) {
		failureReason = "UI_MESSAGEBOX must be a typed OP_PROC routed through the deferred UI command path.";
		return false;
	}
	if (!compileBytecode(kSource, bytecode, compileError)) {
		failureReason = "Unable to compile UI_MESSAGEBOX proc probe: " + compileError;
		return false;
	}
	profile = mrvmAnalyzeBytecode(bytecode.data(), bytecode.size());
	if (profile.procCount < 1) {
		failureReason = "UI_MESSAGEBOX probe must compile as OP_PROC.";
		return false;
	}
	if (!mrvmCanRunStagedInBackground(profile)) {
		failureReason = "UI_MESSAGEBOX proc probe must be staged-background eligible.";
		return false;
	}
	unsupported = mrvmUnsupportedStagedSymbols(profile);
	if (!unsupported.empty()) {
		failureReason = "UI_MESSAGEBOX proc name must be an accepted staged symbol.";
		return false;
	}
	if (!expectCompileError("$MACRO Bad;\n" + legacyKeyword + " MESSAGEBOX('hello');\nEND_MACRO;\n", "Syntax Error.", failureReason)) return false;
	failureReason.clear();
	return true;
}

bool testScreenRenderFacadeBoundaryGuard(std::string &failureReason) {
	const std::string vmPath = absolutePathFromCwd("mrmac/MRVM.cpp");
	const std::string editorPath = absolutePathFromCwd("mrmac/ui/conventional/MRVMEditor.cpp");
	const std::string screenPath = absolutePathFromCwd("mrmac/ui/conventional/MRVMScreen.cpp");
	const std::string screenStatePath = absolutePathFromCwd("mrmac/ui/conventional/MRVMScreenState.cpp");
	const std::string dispatchPath = absolutePathFromCwd("coprocessor/MRCoprocessorDispatch.cpp");
	const std::string deferredPlaybackPath = absolutePathFromCwd("coprocessor/MRCoprocessorDeferredPlayback.cpp");
	std::string vmContent;
	std::string editorContent;
	std::string screenContent;
	std::string screenStateContent;
	std::string dispatchContent;
	std::string deferredPlaybackContent;
	std::string ioError;

	if (!readTextFile(vmPath, vmContent, ioError)) {
		failureReason = "Unable to read MRVM.cpp for screen facade guard: " + ioError;
		return false;
	}
	if (!readTextFile(editorPath, editorContent, ioError)) {
		failureReason = "Unable to read MRVMEditor.cpp for screen facade guard: " + ioError;
		return false;
	}
	if (!readTextFile(screenPath, screenContent, ioError)) {
		failureReason = "Unable to read MRVMScreen.cpp for screen facade guard: " + ioError;
		return false;
	}
	if (!readTextFile(screenStatePath, screenStateContent, ioError)) {
		failureReason = "Unable to read MRVMScreenState.cpp for screen facade guard: " + ioError;
		return false;
	}
	if (!readTextFile(dispatchPath, dispatchContent, ioError)) {
		failureReason = "Unable to read MRCoprocessorDispatch.cpp for screen facade guard: " + ioError;
		return false;
	}
	if (!readTextFile(deferredPlaybackPath, deferredPlaybackContent, ioError)) {
		failureReason = "Unable to read MRCoprocessorDeferredPlayback.cpp for screen facade guard: " + ioError;
		return false;
	}
	dispatchContent += "\n" + deferredPlaybackContent;

	if (vmContent.find("TScreen::screenBuffer") != std::string::npos || screenContent.find("TScreen::screenBuffer") != std::string::npos || dispatchContent.find("TScreen::screenBuffer") != std::string::npos) {
		failureReason = "VM and deferred UI playback must not write through TScreen::screenBuffer.";
		return false;
	}
	if (vmContent.find("struct UiRenderFacade") == std::string::npos || vmContent.find("UiRenderFacade::renderDeferredCommand(command)") == std::string::npos || dispatchContent.find("mrvmUiRenderFacadeRenderDeferredCommand(command)") == std::string::npos) {
		failureReason = "Deferred UI commands must cross the central VM UI render facade.";
		return false;
	}
	if (dispatchContent.find("struct UiRenderFacade") != std::string::npos) {
		failureReason = "Coprocessor playback must not define a second UiRenderFacade.";
		return false;
	}
	if (screenStateContent.find("UiScreenStateFacade::noteMacroOverlayMutation") == std::string::npos || screenStateContent.find("UiScreenStateFacade::noteBaseMutation") == std::string::npos || screenStateContent.find("UiScreenStateFacade::renderBaseThenOverlayIfNeeded") == std::string::npos || screenStateContent.find("UiScreenStateFacade::renderOverlay") == std::string::npos || screenStateContent.find("mrvmStoreRuntimeStateSize(\"macroScreen\", \"overlayGeneration\"") == std::string::npos || screenStateContent.find("mrvmStoreRuntimeStateSize(\"macroScreen\", \"baseGeneration\"") == std::string::npos) {
		failureReason = "Screen render facade must keep K/V-backed base/overlay generation coordination.";
		return false;
	}
	if (screenContent.find("g_screenStateCoordinator") != std::string::npos || screenStateContent.find("g_screenStateCoordinator") != std::string::npos || screenStateContent.find("ScreenStateCoordinator") != std::string::npos) {
		failureReason = "Screen render state must not be duplicated in a global coordinator.";
		return false;
	}
	if (editorContent.find("bool mrvmUiSetCurrentWindow(const void *windowKey)") == std::string::npos ||
	    editorContent.find("returnWithDirectScreenMutation(mrvmEditorCreateWindow())") == std::string::npos ||
	    editorContent.find("returnWithDirectScreenMutation(mrvmEditorDeleteCurrentWindow())") == std::string::npos ||
	    editorContent.find("returnWithDirectScreenMutation(mrvmEditorModifyCurrentWindow())") == std::string::npos ||
	    editorContent.find("returnWithDirectScreenMutation(mrvmEditorSwitchWindow(index))") == std::string::npos ||
	    editorContent.find("returnWithDirectScreenMutation(mrvmEditorSizeCurrentWindow(x1, y1, x2, y2))") == std::string::npos ||
	    editorContent.find("returnWithDirectScreenMutation(mrvmEditorZoomCurrentWindow())") == std::string::npos ||
	    editorContent.find("returnWithDirectScreenMutation(mrvmEditorRedrawCurrentWindow())") == std::string::npos ||
	    editorContent.find("returnWithDirectScreenMutation(mrvmEditorRedrawEntireScreen())") == std::string::npos) {
		failureReason = "Window and desktop render operations must invalidate the base screen through the facade.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testRenderSinkClassificationGuard(std::string &failureReason) {
	const std::string vmPath = absolutePathFromCwd("mrmac/MRVM.cpp");
	const std::string editorPath = absolutePathFromCwd("mrmac/ui/conventional/MRVMEditor.cpp");
	const std::string screenPath = absolutePathFromCwd("mrmac/ui/conventional/MRVMScreen.cpp");
	const std::string screenStatePath = absolutePathFromCwd("mrmac/ui/conventional/MRVMScreenState.cpp");
	const std::string dispatchPath = absolutePathFromCwd("coprocessor/MRCoprocessorDispatch.cpp");
	const std::string appPath = absolutePathFromCwd("app/MREditorApp.cpp");
	const std::string windowCommandsPath = absolutePathFromCwd("app/commands/MRWindowCommands.cpp");
	const std::string virtualDesktopCommandsPath = absolutePathFromCwd("app/commands/MRVirtualDesktopCommands.cpp");
	std::string vmContent;
	std::string editorContent;
	std::string screenContent;
	std::string screenStateContent;
	std::string dispatchContent;
	std::string appContent;
	std::string windowCommandsContent;
	std::string ioError;
	std::string missingNeedle;

	if (!readTextFile(vmPath, vmContent, ioError)) {
		failureReason = "Unable to read MRVM.cpp for render sink classification guard: " + ioError;
		return false;
	}
	if (!readTextFile(editorPath, editorContent, ioError)) {
		failureReason = "Unable to read MRVMEditor.cpp for render sink classification guard: " + ioError;
		return false;
	}
	if (!readTextFile(screenPath, screenContent, ioError)) {
		failureReason = "Unable to read MRVMScreen.cpp for render sink classification guard: " + ioError;
		return false;
	}
	if (!readTextFile(screenStatePath, screenStateContent, ioError)) {
		failureReason = "Unable to read MRVMScreenState.cpp for render sink classification guard: " + ioError;
		return false;
	}
	screenContent += "\n" + screenStateContent;
	if (!readTextFile(dispatchPath, dispatchContent, ioError)) {
		failureReason = "Unable to read MRCoprocessorDispatch.cpp for render sink classification guard: " + ioError;
		return false;
	}
	if (!readTextFile(appPath, appContent, ioError)) {
		failureReason = "Unable to read MREditorApp.cpp for render sink classification guard: " + ioError;
		return false;
	}
	if (!readTextFile(windowCommandsPath, windowCommandsContent, ioError)) {
		failureReason = "Unable to read MRWindowCommands.cpp for render sink classification guard: " + ioError;
		return false;
	}
	if (!readTextFile(virtualDesktopCommandsPath, screenStateContent, ioError)) {
		failureReason = "Unable to read MRVirtualDesktopCommands.cpp for render sink classification guard: " + ioError;
		return false;
	}
	windowCommandsContent += "\n" + screenStateContent;
	if (dispatchContent.find("writeLine(") != std::string::npos || dispatchContent.find("writeBuf(") != std::string::npos || dispatchContent.find("TScreen::flushScreen()") != std::string::npos) {
		failureReason = "Deferred playback must not own physical render sinks.";
		return false;
	}
	if (!containsAllSubstrings(screenContent, {"Render sink classification for the Strangler foundation:", "ordinary-view-draw:", "base-redraw-trigger:", "overlay-render:", "unsafe-physical-write:"}, missingNeedle)) {
		failureReason = "MRVMScreen.cpp must document the render sink classification foundation: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(screenContent, {"void MacroCellView::draw()", "grid.drawKnownCells(*this);", "void MacroCellGrid::projectRowSpan", "void MacroCellGrid::projectAll", "void MacroCellGrid::redrawBaseAndOverlay"}, missingNeedle)) {
		failureReason = "VM render sinks must keep MacroCell overlay projection classified explicitly: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(vmContent, {"bool mrvmEditorRedrawCurrentWindow()", "bool mrvmEditorRedrawEntireScreen()"}, missingNeedle)) {
		if (!containsAllSubstrings(screenContent, {"static void forceMacroUiMessageRefresh(TApplication *app)"}, missingNeedle) ||
		    !containsAllSubstrings(vmContent, {"bool mrvmEditorRedrawCurrentWindow()", "bool mrvmEditorRedrawEntireScreen()"}, missingNeedle) ||
		    !containsAllSubstrings(editorContent, {"bool mrvmUiRedrawCurrentWindow()", "bool mrvmUiNewScreen()"}, missingNeedle)) {
			failureReason = "VM render sinks must keep explicit base redraw trigger entry points classified: missing " + missingNeedle + ".";
			return false;
		}
	}
	if (!containsAllSubstrings(appContent, {"void MREditorApp::applyConfiguredDisplayLayout()", "deskTop->drawView();", "menuBar->drawView();", "statusLine->drawView();"}, missingNeedle)) {
		failureReason = "Application render sinks must keep layout redraw triggers classified explicitly: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(windowCommandsContent, {"void syncVirtualDesktopVisibility()", "TProgram::deskTop->drawView();", "TProgram::application->redraw();"}, missingNeedle)) {
		failureReason = "Virtual desktop render sinks must keep redraw triggers classified explicitly: missing " + missingNeedle + ".";
		return false;
	}
	if (screenContent.find("dirtyRows") == std::string::npos || screenContent.find("fullProjectionPending") == std::string::npos || screenContent.find("projectDirtyRows(") == std::string::npos) {
		failureReason = "MacroCellGrid must keep dirty-row coalescing state in the VM render path.";
		return false;
	}
	if (countSubstring(vmContent, "TScreen::flushScreen()") + countSubstring(screenContent, "TScreen::flushScreen()") != 4) {
		failureReason = "Unexpected VM flushScreen sink count; classify new sinks before adding them.";
		return false;
	}
	{
		const std::size_t messageRefreshStart = screenContent.find("static void forceMacroUiMessageRefresh(TApplication *app)");
		const std::size_t messageRefreshEnd = screenContent.find("bool applyMarqueeProc(", messageRefreshStart);
		const std::size_t batchEndStart = screenContent.find("void MacroCellGrid::endProjectionBatch()");
		const std::size_t batchEndEnd = screenContent.find("bool MacroCellGrid::hasDirtyRows() const noexcept", batchEndStart);
		const std::size_t projectAllStart = screenContent.find("void MacroCellGrid::projectAll()");
		const std::size_t projectAllEnd = screenContent.find("void MacroCellGrid::redrawBaseAndOverlay()", projectAllStart);
		const std::size_t redrawStart = screenContent.find("void MacroCellGrid::redrawBaseAndOverlay()");
		const std::size_t redrawEnd = screenContent.find("bool MacroCellGrid::putBox(", redrawStart);
		if (messageRefreshStart == std::string::npos || messageRefreshEnd == std::string::npos || batchEndStart == std::string::npos || batchEndEnd == std::string::npos || projectAllStart == std::string::npos || projectAllEnd == std::string::npos || redrawStart == std::string::npos || redrawEnd == std::string::npos) {
			failureReason = "Unable to locate approved flushScreen sink boundaries in MRVMScreen.cpp.";
			return false;
		}
		const std::string messageRefreshBlock = screenContent.substr(messageRefreshStart, messageRefreshEnd - messageRefreshStart);
		const std::string batchEndBlock = screenContent.substr(batchEndStart, batchEndEnd - batchEndStart);
		const std::string projectAllBlock = screenContent.substr(projectAllStart, projectAllEnd - projectAllStart);
		const std::string redrawBlock = screenContent.substr(redrawStart, redrawEnd - redrawStart);
		if (countSubstring(messageRefreshBlock, "TScreen::flushScreen()") != 1 || countSubstring(batchEndBlock, "TScreen::flushScreen()") != 1 || countSubstring(projectAllBlock, "TScreen::flushScreen()") != 1 || countSubstring(redrawBlock, "TScreen::flushScreen()") != 1) {
			failureReason = "Approved VM flushScreen sinks must remain limited to forceMacroUiMessageRefresh(), endProjectionBatch(), projectAll() and redrawBaseAndOverlay().";
			return false;
		}
		std::string outsideApprovedFlushSinks = screenContent;
		outsideApprovedFlushSinks.erase(redrawStart, redrawEnd - redrawStart);
		outsideApprovedFlushSinks.erase(projectAllStart, projectAllEnd - projectAllStart);
		outsideApprovedFlushSinks.erase(batchEndStart, batchEndEnd - batchEndStart);
		outsideApprovedFlushSinks.erase(messageRefreshStart, messageRefreshEnd - messageRefreshStart);
		if (outsideApprovedFlushSinks.find("TScreen::flushScreen()") != std::string::npos) {
			failureReason = "VM flushScreen() must not appear outside the approved facade/consumer sinks.";
			return false;
		}
	}

	failureReason.clear();
	return true;
}

bool testResizeKillBoxReprojectionGuard(std::string &failureReason) {
	const std::string screenPath = absolutePathFromCwd("mrmac/ui/conventional/MRVMScreenState.cpp");
	std::string screenContent;
	std::string ioError;
	std::string missingNeedle;

	if (!readTextFile(screenPath, screenContent, ioError)) {
		failureReason = "Unable to read MRVMScreenState.cpp for resize/KILL_BOX reprojection guard: " + ioError;
		return false;
	}
	if (!containsAllSubstrings(screenContent, {"bool geometryResetPending = false;", "boxStack.clear();", "geometryResetPending = true;", "UiScreenStateFacade::renderBaseThenOverlayIfNeeded(*this)", "grid.geometryResetPending || UiScreenStateFacade::needsOverlayReprojection()", "geometryResetPending = false;"}, missingNeedle)) {
		failureReason = "MacroCellGrid must keep explicit geometry-reset reprojection state for resize handling: missing " + missingNeedle + ".";
		return false;
	}
	{
		const std::size_t killBoxStart = screenContent.find("bool MacroCellGrid::killBox()");
		const std::size_t killBoxEnd = screenContent.find("\n} // namespace mrvm_screen", killBoxStart);
		if (killBoxStart == std::string::npos || killBoxEnd == std::string::npos || killBoxEnd <= killBoxStart) {
			failureReason = "Unable to locate MacroCellGrid::killBox() for resize/KILL_BOX reprojection guard.";
			return false;
		}
		const std::string killBoxBlock = screenContent.substr(killBoxStart, killBoxEnd - killBoxStart);
		if (!containsAllSubstrings(killBoxBlock, {"if (boxStack.empty()) {", "if (geometryResetPending)", "redrawBaseAndOverlay();", "return true;"}, missingNeedle)) {
			failureReason = "MacroCellGrid::killBox() must redraw base and overlay after resize when snapshots were cleared: missing " + missingNeedle + ".";
			return false;
		}
	}

	failureReason.clear();
	return true;
}

bool testClearScreenSnapshotResetGuard(std::string &failureReason) {
	const std::string screenPath = absolutePathFromCwd("mrmac/ui/conventional/MRVMScreenState.cpp");
	std::string screenContent;
	std::string ioError;

	if (!readTextFile(screenPath, screenContent, ioError)) {
		failureReason = "Unable to read MRVMScreenState.cpp for CLEAR_SCREEN snapshot guard: " + ioError;
		return false;
	}
	{
		const std::size_t clearScreenStart = screenContent.find("bool MacroCellGrid::clearScreen(int attr) {");
		const std::size_t clearScreenEnd = screenContent.find("bool MacroCellGrid::scrollBox(", clearScreenStart);
		if (clearScreenStart == std::string::npos || clearScreenEnd == std::string::npos || clearScreenEnd <= clearScreenStart) {
			failureReason = "Unable to locate MacroCellGrid::clearScreen() for CLEAR_SCREEN snapshot guard.";
			return false;
		}
		const std::string clearScreenBlock = screenContent.substr(clearScreenStart, clearScreenEnd - clearScreenStart);
		if (clearScreenBlock.find("boxStack.clear();") == std::string::npos) {
			failureReason = "CLEAR_SCREEN must clear active PUT_BOX snapshots before reprojection.";
			return false;
		}
	}
	{
		const std::size_t clearLineStart = screenContent.find("bool MacroCellGrid::clearLine(int col, int row, int count) {");
		const std::size_t clearLineEnd = screenContent.find("bool MacroCellGrid::clearScreen(int attr) {", clearLineStart);
		if (clearLineStart == std::string::npos || clearLineEnd == std::string::npos || clearLineEnd <= clearLineStart) {
			failureReason = "Unable to locate MacroCellGrid::clearLine() for CLEAR_SCREEN snapshot guard.";
			return false;
		}
		const std::string clearLineBlock = screenContent.substr(clearLineStart, clearLineEnd - clearLineStart);
		if (clearLineBlock.find("boxStack.clear();") != std::string::npos) {
			failureReason = "CLR_LINE must not discard PUT_BOX snapshots.";
			return false;
		}
	}
	{
		const std::size_t scrollBoxStart = screenContent.find("bool MacroCellGrid::scrollBox(int x1, int y1, int x2, int y2, int attr, bool down) {");
		const std::size_t scrollBoxEnd = screenContent.find("bool MacroCellGrid::putLineColOverlay(", scrollBoxStart);
		if (scrollBoxStart == std::string::npos || scrollBoxEnd == std::string::npos || scrollBoxEnd <= scrollBoxStart) {
			failureReason = "Unable to locate MacroCellGrid::scrollBox() for CLEAR_SCREEN snapshot guard.";
			return false;
		}
		const std::string scrollBoxBlock = screenContent.substr(scrollBoxStart, scrollBoxEnd - scrollBoxStart);
		if (scrollBoxBlock.find("boxStack.clear();") != std::string::npos) {
			failureReason = "SCROLL_BOX_* must not discard PUT_BOX snapshots.";
			return false;
		}
	}

	failureReason.clear();
	return true;
}

bool testLineColOverlayReplayGuard(std::string &failureReason) {
	const std::string screenPath = absolutePathFromCwd("mrmac/ui/conventional/MRVMScreenState.cpp");
	std::string screenContent;
	std::string ioError;
	std::string missingNeedle;

	if (!readTextFile(screenPath, screenContent, ioError)) {
		failureReason = "Unable to read MRVMScreenState.cpp for line/column overlay replay guard: " + ioError;
		return false;
	}
	if (!containsAllSubstrings(screenContent, {"mrvmRuntimeStateInt(\"macroScreen\", \"haveLine\")", "mrvmRuntimeStateInt(\"macroScreen\", \"haveCol\")", "putLineColOverlay(mrvmRuntimeStateInt(\"macroScreen\", \"line\"), mrvmRuntimeStateInt(\"macroScreen\", \"col\"), haveLine, haveCol)"}, missingNeedle)) {
		failureReason = "Macro line/column overlay replay must remain wired through the K/V-backed overlay state: missing " + missingNeedle + ".";
		return false;
	}
	{
		const std::size_t killBoxStart = screenContent.find("bool MacroCellGrid::killBox()");
		const std::size_t killBoxEnd = screenContent.find("\n} // namespace mrvm_screen", killBoxStart);
		if (killBoxStart == std::string::npos || killBoxEnd == std::string::npos || killBoxEnd <= killBoxStart) {
			failureReason = "Unable to locate MacroCellGrid::killBox() for line/column overlay replay guard.";
			return false;
		}
		const std::string killBoxBlock = screenContent.substr(killBoxStart, killBoxEnd - killBoxStart);
		if (countSubstring(killBoxBlock, "putLineColOverlay(mrvmRuntimeStateInt(\"macroScreen\", \"line\"), mrvmRuntimeStateInt(\"macroScreen\", \"col\"), haveLine, haveCol)") < 3) {
			failureReason = "MacroCellGrid::killBox() must reapply the current line/column overlay after every redraw-based restore path.";
			return false;
		}
	}

	failureReason.clear();
	return true;
}

bool testCoprocessorScreenRendererBoundaryGuard(std::string &failureReason) {
	const std::string dispatchPath = absolutePathFromCwd("coprocessor/MRCoprocessorDispatch.cpp");
	std::string dispatchContent;
	std::string ioError;
	std::string forbidden;
	std::vector<std::string> observedCalls;
	std::vector<std::string> unexpectedCalls;

	if (!readTextFile(dispatchPath, dispatchContent, ioError)) {
		failureReason = "Unable to read MRCoprocessorDispatch.cpp for coprocessor screen-renderer guard: " + ioError;
		return false;
	}
	static constexpr const char *kForbiddenDirectScreenRenderers[] = {"mrvmUiCreateWindow(", "mrvmUiDeleteCurrentWindow(", "mrvmUiEraseCurrentWindow(", "mrvmUiModifyCurrentWindow(", "mrvmUiLinkCurrentWindow(", "mrvmUiUnlinkCurrentWindow(", "mrvmUiZoomCurrentWindow(", "mrvmUiRedrawCurrentWindow(", "mrvmUiNewScreen(", "mrvmUiMarquee(", "mrvmUiBrain(", "mrvmUiPutBox(", "mrvmUiWrite(", "mrvmUiClrLine(", "mrvmUiGotoxy(", "mrvmUiPutLineNum(", "mrvmUiPutColNum(", "mrvmUiScrollBoxUp(", "mrvmUiScrollBoxDn(", "mrvmUiClearScreen(", "mrvmUiKillBox(", "mrvmUiMessageBox("};
	static constexpr const char *kAllowedUiBridgeCalls[] = {"mrvmUiBeginMacroScreenBatch(",   "mrvmUiCopyGlobals(",         "mrvmUiCopyRuntimeOptions(",   "mrvmUiCopyWindowLastSearch(",
	                                                        "mrvmUiCopyWindowMarkStack(",   "mrvmUiCursorPosition(",       "mrvmUiEndMacroScreenBatch(", "mrvmUiLinkStatus(",
	                                                        "mrvmUiRenderFacadeRenderDeferredCommand(",                     "mrvmUiReplaceGlobals(",      "mrvmUiReplaceRuntimeOptions(",
	                                                        "mrvmUiReplaceWindowLastSearch(", "mrvmUiReplaceWindowMarkStack(", "mrvmUiScreenHeight(",     "mrvmUiScreenMutationEpoch(",
	                                                        "mrvmUiScreenWidth(",            "mrvmUiSetCurrentWindow(",     "mrvmUiSyncLinkedWindowsFrom(", "mrvmUiWindowCount(",
	                                                        "mrvmUiWindowGeometry("};

	for (const char *needle : kForbiddenDirectScreenRenderers)
		if (dispatchContent.find(needle) != std::string::npos) {
			forbidden = needle;
			failureReason = "Coprocessor playback must not call direct mrvmUi* screen renderers outside the central gateway: " + forbidden;
			return false;
		}
	{
		const std::regex uiBridgeCallPattern("mrvmUi[A-Z][A-Za-z0-9_]*\\(");
		for (std::sregex_iterator it(dispatchContent.begin(), dispatchContent.end(), uiBridgeCallPattern), end; it != end; ++it) {
			const std::string call = it->str();
			if (std::find(observedCalls.begin(), observedCalls.end(), call) == observedCalls.end()) observedCalls.push_back(call);
		}
		for (const std::string &call : observedCalls)
			if (std::find(std::begin(kAllowedUiBridgeCalls), std::end(kAllowedUiBridgeCalls), call) == std::end(kAllowedUiBridgeCalls)) unexpectedCalls.push_back(call);
		if (!unexpectedCalls.empty()) {
			failureReason = "Coprocessor playback must keep mrvmUi* bridge usage on the approved whitelist: " + unexpectedCalls.front();
			return false;
		}
	}
	if (dispatchContent.find("mrvmUiRenderFacadeRenderDeferredCommand(command)") == std::string::npos || dispatchContent.find("DeferredUiRenderGateway::renderDeferredCommand(command)") == std::string::npos || dispatchContent.find("MacroScreenView::render(command)") == std::string::npos || dispatchContent.find("mrvmUiBeginMacroScreenBatch();") == std::string::npos || dispatchContent.find("mrvmUiEndMacroScreenBatch();") == std::string::npos) {
		failureReason = "Coprocessor playback must keep all deferred screen rendering routed through the gateway/view chain.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testMarqueeColorSourceGuard(std::string &failureReason) {
	const std::string menuBarPath = absolutePathFromCwd("ui/MRMenuBar.cpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(menuBarPath, content, ioError)) {
		failureReason = "Unable to read MRMenuBar.cpp for marquee color source guard: " + ioError;
		return false;
	}
	if (content.find("resolvedPaletteAttribute(marqueePaletteSlot(") == std::string::npos) {
		failureReason = "Marquee colors must be sourced from the configured palette slot helper.";
		return false;
	}
	if (content.find("getColor(0x2B2B)") != std::string::npos || content.find("getColor(0x2C2C)") != std::string::npos || content.find("getColor(0x2A2A)") != std::string::npos) {
		failureReason = "Marquee colors must not use raw getColor(0x2A2A/0x2B2B/0x2C2C) view-local lookup.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testOtherColorsDedicatedMessageSlotsGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("config/settings/MRSettingsThemesProfiles.cpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read MRSettingsThemesProfiles.cpp for OTHERCOLORS slot guard: " + ioError;
		return false;
	}
	if (content.find("{\"error message\", kMrPaletteMessageError}") == std::string::npos || content.find("{\"message\", kMrPaletteMessage}") == std::string::npos || content.find("{\"warning message\", kMrPaletteMessageWarning}") == std::string::npos) {
		failureReason = "OTHERCOLORS message entries must target dedicated extension palette slots.";
		return false;
	}
	if (content.find("{\"error message\", 42}") != std::string::npos || content.find("{\"message\", 43}") != std::string::npos || content.find("{\"warning message\", 44}") != std::string::npos) {
		failureReason = "OTHERCOLORS message entries must not map directly to dialog palette slots 42/43/44.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testDelayProcWiringGuard(std::string &failureReason) {
	const std::string source = "$MACRO DelayProbe FROM EDIT;\n"
	                           "DELAY(20);\n"
	                           "SET_GLOBAL_STR('DELAY_PROBE', 'ok');\n"
	                           "END_MACRO;\n";
	std::vector<unsigned char> bytecode;
	std::string macroName;
	std::string compileError;
	int entryOffset = -1;
	VirtualMachine vm;
	std::string vmError;
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

	if (!compileSource(source, bytecode, entryOffset, macroName, compileError)) {
		failureReason = "Compile failed for DELAY probe: " + compileError;
		return false;
	}
	vm.executeAt(bytecode.data(), bytecode.size(), static_cast<size_t>(entryOffset), std::string(), macroName, true, true);
	while (vm.hasPendingDelay() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
		vm.resumePendingDelay();
	}
	if (vm.hasPendingDelay()) {
		failureReason = "DELAY probe remained suspended after timeout.";
		return false;
	}
	if (firstVmError(vm.log, vmError)) {
		failureReason = "DELAY probe produced VM error: " + vmError;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testStartupCliLoadRecursiveGuard(std::string &failureReason) {
	const std::string appSourcePath = absolutePathFromCwd("app/MREditorApp.cpp");
	const std::string mainSourcePath = absolutePathFromCwd("mr.cpp");
	const std::string makefilePath = absolutePathFromCwd("Makefile");
	const std::string vmHeaderPath = absolutePathFromCwd("mrmac/MRVM.hpp");
	const std::string vmSourcePath = absolutePathFromCwd("mrmac/MRVM.cpp");
	std::string appContent;
	std::string mainContent;
	std::string makefileContent;
	std::string vmHeaderContent;
	std::string vmSourceContent;
	std::string ioError;

	if (!readTextFile(appSourcePath, appContent, ioError)) {
		failureReason = "Unable to read MREditorApp.cpp for startup CLI guard: " + ioError;
		return false;
	}
	if (!readTextFile(mainSourcePath, mainContent, ioError)) {
		failureReason = "Unable to read mr.cpp for startup CLI guard: " + ioError;
		return false;
	}
	if (!readTextFile(makefilePath, makefileContent, ioError)) {
		failureReason = "Unable to read Makefile for startup CLI guard: " + ioError;
		return false;
	}
	if (!readTextFile(vmHeaderPath, vmHeaderContent, ioError)) {
		failureReason = "Unable to read MRVM.hpp for startup CLI guard: " + ioError;
		return false;
	}
	if (!readTextFile(vmSourcePath, vmSourceContent, ioError)) {
		failureReason = "Unable to read MRVM.cpp for startup CLI guard: " + ioError;
		return false;
	}
	if (appContent.find("--load-recursive") == std::string::npos || appContent.find("-lr") == std::string::npos || appContent.find("recursive_directory_iterator") == std::string::npos || appContent.find("fnmatch(") == std::string::npos) {
		failureReason = "MREditorApp startup loading must support --load-recursive/-lr with recursive glob matching.";
		return false;
	}
	if (appContent.find("patternSuffix.find('/') == std::string::npos") == std::string::npos || appContent.find("candidatePath.filename().string()") == std::string::npos || appContent.find("std::filesystem::relative(candidatePath, basePath") == std::string::npos) {
		failureReason = "Recursive glob must match by basename without path prefix and by relative path for path patterns.";
		return false;
	}
	if (appContent.find("mrvmProcessArguments()") == std::string::npos) {
		failureReason = "MREditorApp startup loading must consume CLI args via mrvmProcessArguments().";
		return false;
	}
	if (appContent.find("static_cast<void>(loadStartupFilesFromCommandLine());") == std::string::npos) {
		failureReason = "MREditorApp constructor must load startup files from CLI.";
		return false;
	}
	if (appContent.find("createEditorWindow(\"?No-File?\")") != std::string::npos) {
		failureReason = "MREditorApp must not create an empty placeholder editor window on startup.";
		return false;
	}
	if (vmHeaderContent.find("mrvmProcessArguments();") == std::string::npos || vmSourceContent.find("std::vector<std::string> mrvmProcessArguments()") == std::string::npos) {
		failureReason = "mrvm process-argument getter must be declared and implemented.";
		return false;
	}
	if (mainContent.find("--help") == std::string::npos || mainContent.find("-h") == std::string::npos || mainContent.find("kMrEmbeddedHelpMarkdown") == std::string::npos) {
		failureReason = "mr.cpp must provide --help/-h and print embedded markdown help.";
		return false;
	}
	if (makefileContent.find("app/mrhelp.md") == std::string::npos || makefileContent.find("app/MRHelp.generated.hpp") == std::string::npos || makefileContent.find("generate_help_markdown.sh") == std::string::npos) {
		failureReason = "Makefile must regenerate embedded help header from app/mrhelp.md.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testCompilerSupportMacrosCompileGuard(std::string &failureReason) {
	namespace fs = std::filesystem;
	const fs::path macroDirectory = absolutePathFromCwd("mrmac/macros/compilersupport");
	std::vector<fs::path> macroPaths;
	std::error_code errorCode;

	if (!fs::is_directory(macroDirectory, errorCode)) {
		failureReason = "Compiler support macro directory is missing.";
		return false;
	}
	for (const fs::directory_entry &entry : fs::directory_iterator(macroDirectory, errorCode)) {
		if (errorCode) {
			failureReason = "Unable to scan compiler support macros: " + errorCode.message();
			return false;
		}
		if (!entry.is_regular_file(errorCode)) continue;
		if (entry.path().extension() == ".mrmac") macroPaths.push_back(entry.path());
	}
	if (errorCode) {
		failureReason = "Unable to finish compiler support macro scan: " + errorCode.message();
		return false;
	}
	if (macroPaths.empty()) {
		failureReason = "Compiler support macro directory contains no .mrmac files.";
		return false;
	}
	std::sort(macroPaths.begin(), macroPaths.end());
	for (const fs::path &macroPath : macroPaths) {
		std::string source;
		std::string ioError;
		std::vector<unsigned char> bytecode;
		int entryOffset = 0;
		std::string entryName;
		std::string compileError;

		if (!readTextFile(macroPath.string(), source, ioError)) {
			failureReason = "Unable to read compiler support macro " + macroPath.filename().string() + ": " + ioError;
			return false;
		}
		if (!compileSource(source, bytecode, entryOffset, entryName, compileError)) {
			failureReason = "Compiler support macro does not compile: " + macroPath.filename().string() + ": " + compileError;
			return false;
		}
	}

	failureReason.clear();
	return true;
}

bool testBentoBoxFoundationGuard(std::string &failureReason) {
	std::string source;
	std::string diagnosticsSource;
	std::string header;
	std::string paneWindowSource;
	std::string projectionSource;
	std::string frameSource;
	std::string editWindowHeader;
	std::string editorHeader;
	std::string editorSource;
	std::string bufferModelHeader;
	std::string windowCommandsHeader;
	std::string windowCommandsSource;
	std::string commandRouterSource;
	std::string coprocessorDispatchSource;
	std::string ioError;
	std::string missingNeedle;
	std::size_t commandRouterPos = std::string::npos;
	std::size_t cmClosePos = std::string::npos;
	std::size_t commandDefaultPos = std::string::npos;
	std::size_t splitRightPos = std::string::npos;
	std::size_t splitDownPos = std::string::npos;
	std::size_t verticalOrientationPos = std::string::npos;
	std::size_t horizontalOrientationPos = std::string::npos;

	if (!readTextFile(absolutePathFromCwd("ui/MRBentoBox/MRBentoBox.cpp"), source, ioError)) {
		failureReason = "Unable to read MRBentoBox.cpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRBentoBox/MRBentoBoxDiagnostics.cpp"), diagnosticsSource, ioError)) {
		failureReason = "Unable to read MRBentoBoxDiagnostics.cpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRBentoBox/MRBentoBoxPaneWindow.cpp"), paneWindowSource, ioError)) {
		failureReason = "Unable to read MRBentoBoxPaneWindow.cpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRBentoBox/MRBentoBoxProjection.cpp"), projectionSource, ioError)) {
		failureReason = "Unable to read MRBentoBoxProjection.cpp: " + ioError;
		return false;
	}
	source += "\n";
	source += diagnosticsSource;
	source += "\n";
	source += paneWindowSource;
	source += "\n";
	source += projectionSource;
	if (!readTextFile(absolutePathFromCwd("ui/MRBentoBox/MRBentoBox.hpp"), header, ioError)) {
		failureReason = "Unable to read MRBentoBox.hpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRFrame.cpp"), frameSource, ioError)) {
		failureReason = "Unable to read MRFrame.cpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MREditWindow.hpp"), editWindowHeader, ioError)) {
		failureReason = "Unable to read MREditWindow.hpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRFileEditor/MRFileEditor.hpp"), editorHeader, ioError)) {
		failureReason = "Unable to read MRFileEditor.hpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRFileEditor/MRFileEditor.cpp"), editorSource, ioError)) {
		failureReason = "Unable to read MRFileEditor.cpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRTextBufferModel.hpp"), bufferModelHeader, ioError)) {
		failureReason = "Unable to read MRTextBufferModel.hpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("app/commands/MRWindowCommands.hpp"), windowCommandsHeader, ioError)) {
		failureReason = "Unable to read MRWindowCommands.hpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("app/commands/MRWindowCommands.cpp"), windowCommandsSource, ioError)) {
		failureReason = "Unable to read MRWindowCommands.cpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("app/MRCommandRouter.cpp"), commandRouterSource, ioError)) {
		failureReason = "Unable to read MRCommandRouter.cpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("coprocessor/MRCoprocessorDispatch.cpp"), coprocessorDispatchSource, ioError)) {
		failureReason = "Unable to read MRCoprocessorDispatch.cpp: " + ioError;
		return false;
	}
	commandRouterPos = source.find("bool splitCommandTargetsSecondaryPane(ushort command) noexcept");
	cmClosePos = commandRouterPos != std::string::npos ? source.find("case cmClose:", commandRouterPos) : std::string::npos;
	commandDefaultPos = commandRouterPos != std::string::npos ? source.find("default:", commandRouterPos) : std::string::npos;
	if (commandRouterPos == std::string::npos || cmClosePos == std::string::npos || commandDefaultPos == std::string::npos || cmClosePos > commandDefaultPos) {
		failureReason = "Bento command routing must keep window-close commands out of secondary panes.";
		return false;
	}
	if (!containsAllSubstrings(source, {"evMouseDown | evMouseMove | evMouseUp | evMouseAuto | evMouseWheel | evKeyDown", "const bool mouseEvent = (event.what & (evMouseDown | evMouseMove | evMouseUp | evMouseAuto | evMouseWheel)) != 0;", "if (event.what == evMouseDown) setActivePaneForMouse(event.mouse.where);", "const bool mouseTargetsActivePane = !mouseEvent || mouseLeafId == activeLeafId;", "if (activeLeafId != 0 && mouseTargetsActivePane && splitEventTargetsSecondaryPane(event))"}, missingNeedle)) {
		failureReason = "Bento mouse routing must focus panes by click only and route mouse events to the active pane only: missing " + missingNeedle + ".";
		return false;
	}
	if (source.find("setActivePane(wheelLeafId);") != std::string::npos) {
		failureReason = "Bento mouse-wheel routing must not change the active pane.";
		return false;
	}
	if (!containsAllSubstrings(editWindowHeader, {"class MRHelpWindow : public MREditWindow", "class MRLogWindow : public MREditWindow", "class MRCommunicationWindow : public MREditWindow", "virtual bool allowsDocumentViewportSplit() const noexcept", "virtual bool isCommunicationWindow() const noexcept override"}, missingNeedle)) {
		failureReason = "Special editor windows must be modeled as MREditWindow subtypes: missing " + missingNeedle + ".";
		return false;
	}
	splitRightPos = source.find("case bppSplitRight:");
	splitDownPos = source.find("case bppSplitDown:");
	verticalOrientationPos = splitRightPos != std::string::npos ? source.find("splitLeafNode(targetLeafId, bsoVertical, spec)", splitRightPos) : std::string::npos;
	horizontalOrientationPos = splitDownPos != std::string::npos ? source.find("splitLeafNode(targetLeafId, bsoHorizontal, spec)", splitDownPos) : std::string::npos;
	if (splitRightPos == std::string::npos || splitDownPos == std::string::npos || verticalOrientationPos == std::string::npos || horizontalOrientationPos == std::string::npos || verticalOrientationPos > splitDownPos) {
		failureReason = "Bento split placement mapping changed.";
		return false;
	}
	if (!containsAllSubstrings(source, {"constexpr BentoFrameGlyphs kBentoFrameGlyphs;", "static const BentoPaneActionDescriptor kBentoPaneActions[]", "{kBentoPaneActionSplitRight, bppSplitDown}", "{kBentoPaneActionSplitDown, bppSplitRight}", "for (const BentoPaneActionDescriptor &descriptor : kBentoPaneActions)"}, missingNeedle)) {
		failureReason = "Bento frame glyph table and split action mapping changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(source, {"int MRBentoBox::splitLeafNode(int leafId, BentoSplitOrientation orientation, const MRBentoPaneSpec &spec)", "target.kind = blnSplit;", "target.firstChild = existingLeafNode;", "target.secondChild = newLeafNode;", "layoutTree.push_back(node);"}, missingNeedle)) {
		failureReason = "Bento recursive split foundation changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(header, {"std::vector<BentoLayoutNode> layoutTree;", "std::vector<BentoLeaf> leaves;", "std::vector<MRBentoPaneFrameView *> paneFrameViews;", "int activeLeafId;"}, missingNeedle)) {
		failureReason = "Bento dynamic pane members changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(header, {"struct MRBentoPaneTitleMenuSpec", "enum MRBentoBoxMode", "bbmDocumentViewports", "struct MRBentoPaneSpec", "MRBentoPaneBufferPolicy bufferPolicy;", "bool readOnly;", "bool suppressMiniMap;", "bool suppressWordWrap;", "bool scrollBarsAlwaysVisible;", "const MRBentoPaneTitleMenuSpec *titleMenu;"}, missingNeedle)) {
		failureReason = "Bento pane spec contract changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(source, {"MRBentoPaneSpec MRBentoBox::paneSpecForRole(MRBentoPaneRole role) const noexcept", "const MRBentoPaneTitleMenuSpec *titleMenu = bentoMode == bbmDocumentViewports ? nullptr : &kBentoRoleTitleMenu;", "return MRBentoPaneSpec(bprSplitEditor, bpbSharedSourceBuffer, false, false, false, bentoMode == bbmDocumentViewports, titleMenu);", "return MRBentoPaneSpec(role, bpbOwnBuffer, true, true, true, true, titleMenu);"}, missingNeedle)) {
		failureReason = "Bento role-to-spec mapping changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(source, {"source.title = bentoMode == bbmDocumentViewports ? \"\" : bentoPaneRoleTitle(bprSource);", "leaf.title = bentoMode == bbmDocumentViewports ? \"\" : bentoPaneRoleTitle(spec.role);", "int MRBentoBox::viewportNumberForLeaf(int leafId) const noexcept", "stack.push_back(node.secondChild);", "stack.push_back(node.firstChild);", "return \"Viewport #\" + std::to_string(std::max(1, viewportNumber));", "if (!titleMenuEnabledForLeaf(targetLeafId)) return;"}, missingNeedle)) {
		failureReason = "Document viewport mode must use current layout-derived viewport titles and suppress pane title menus: missing " + missingNeedle + ".";
		return false;
	}
	if (header.find("nextViewportNumber") != std::string::npos || source.find("nextViewportTitle") != std::string::npos) {
		failureReason = "Document viewport titles must not use a monotonic historical counter.";
		return false;
	}
	if (!containsAllSubstrings(source, {"paneEditor->setMiniMapSuppressed(mPaneSpec.suppressMiniMap);", "paneEditor->setWordWrapSuppressed(mPaneSpec.suppressWordWrap);", "paneEditor->setScrollBarsAlwaysVisible(mPaneSpec.scrollBarsAlwaysVisible);"}, missingNeedle)) {
		failureReason = "Bento spec-driven editor policy changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(source, {"MRPaneEditWindow::MRPaneEditWindow", "state &= static_cast<ushort>(~sfShadow);", "eventMask = 0;", "const bool withControls = focused;", "leaf.id == 0 && bentoMode != bbmDocumentViewports", "toggleLeafMaximized(int leafId) noexcept", "leafId == 0 && bentoMode != bbmDocumentViewports"}, missingNeedle)) {
		failureReason = "Document viewport pane chrome and dispatcher ownership changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(source, {"class MRBentoPaneFrameView : public TView", "insertBefore(view, nullptr);", "view->hide();", "view->show();", "paneFrameViews[i]->drawView();", "TColorAttr MRBentoBox::paneFrameColor(bool focused)"}, missingNeedle)) {
		failureReason = "Bento pane chrome must remain an attached, buffer-backed view: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(source, {"void MRBentoBox::drawSharedEditorPanes() noexcept", "leaf.spec.bufferPolicy == bpbSharedSourceBuffer", "targetPane->handleEvent(event);", "drawSharedEditorPanes();"}, missingNeedle)) {
		failureReason = "Bento shared editor pane redraw changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(bufferModelHeader, {"std::shared_ptr<SharedState> mShared;", "void shareContentStateFrom(const MRTextBufferModel &source) noexcept", "void detachContentStateCopy()", "Cursor mCursor;", "Selection mSelection;"}, missingNeedle)) {
		failureReason = "Shared content state must keep per-view cursor and selection: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(editorHeader, {"void shareContentStateFrom(const MRFileEditor &source);", "void detachContentStateCopy();"}, missingNeedle)) {
		failureReason = "MRFileEditor shared content API changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(editorSource, {"mBufferModel.shareContentStateFrom(source.bufferModel());", "mBufferModel.detachContentStateCopy();", "clearFindMarkerRanges();", "clearDirtyRanges();"}, missingNeedle)) {
		failureReason = "MRFileEditor shared content behavior changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(windowCommandsHeader, {"MREditWindow *createHelpWindow(const char *title);", "MREditWindow *createLogWindow(const char *title);", "MREditWindow *createCommunicationWindow(const char *title);", "MRBentoBox *convertEditWindowToBentoBox(MREditWindow *source);", "std::vector<MREditWindow *> allEditWindowsAndBentoPanesInZOrder();"}, missingNeedle)) {
		failureReason = "Window split must expose normal-editor to Bento conversion: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(header, {"void collectVisiblePaneWindows(std::vector<MREditWindow *> &windows) const noexcept;"}, missingNeedle)) {
		failureReason = "Bento visible pane collection API changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(windowCommandsSource, {"std::vector<MREditWindow *> allEditWindowsAndBentoPanesInZOrder()", "if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window)) bentoBox->collectVisiblePaneWindows(expanded);"}, missingNeedle)) {
		failureReason = "Bento pane-aware window enumeration changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(windowCommandsSource, {"MREditWindow *createEditorWindow(const char *title)", "win = new MRBentoBox(bounds, title, nextEditorWindowNumber(), bbmDocumentViewports);", "MREditWindow *createHelpWindow(const char *title)", "win = new MRHelpWindow(bounds, title, nextEditorWindowNumber());", "MREditWindow *createLogWindow(const char *title)", "win = new MRLogWindow(bounds, title, nextEditorWindowNumber());", "finishNewEditWindow(win, false);", "MREditWindow *createCommunicationWindow(const char *title)", "win = new MRCommunicationWindow(bounds, title, nextEditorWindowNumber());"}, missingNeedle)) {
		failureReason = "Document windows must default to Bento while special windows use explicit subtypes: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(coprocessorDispatchSource, {"std::vector<MREditWindow *> windows = allEditWindowsAndBentoPanesInZOrder();", "editor->applySyntaxWarmup(*syntax, result.task.baseVersion, result.task.id)", "editor->applyFoldWarmup(*result.payload, result.task.baseVersion, result.task.id)", "editor->applyMiniMapWarmup(*miniMap, result.task.baseVersion, result.task.id)"}, missingNeedle)) {
		failureReason = "Derived-state warmup dispatch must include Bento split panes: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(windowCommandsSource, {"MRBentoBox *convertEditWindowToBentoBox(MREditWindow *source)", "bbmDocumentViewports", "win->getEditor()->shareContentStateFrom(*source->getEditor());", "source->getEditor()->detachContentStateCopy();", "source->setFileChanged(false);", "message(source, evCommand, cmClose, nullptr);"}, missingNeedle)) {
		failureReason = "Window split conversion must preserve shared source content without dirty-gating the old shell: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(commandRouterSource, {"case cmMrWindowSplitHorizontal:", "!window->allowsDocumentViewportSplit()", "bentoBox = convertEditWindowToBentoBox(window);", "return bentoBox->splitActiveEditorPane(bppSplitDown);", "case cmMrWindowSplitVertical:", "return bentoBox->splitActiveEditorPane(bppSplitRight);"}, missingNeedle)) {
		failureReason = "Window split commands must route through Bento conversion and the shared split API: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(source, {"class MRBentoPaneFrameView : public TView", "const TAttrPair frameColor = TAttrPair(owner != nullptr ? owner->mapColor(focused ? 13 : 1) : mapColor(1));"}, missingNeedle)) {
		failureReason = "Bento pane focus must remain color-based: missing " + missingNeedle + ".";
		return false;
	}
	if (source.find("insert(view);") != std::string::npos) {
		failureReason = "Bento pane chrome must not be inserted as a child overlay view.";
		return false;
	}
	if (!containsAllSubstrings(source, {"void MRBentoBox::draw()", "MREditWindow::draw();", "drawSourcePaneScrollBars();", "drawPaneFrames();", "void MRBentoBox::handleEvent(TEvent &event)", "updateTrackedCompilerSidekick();", "drawPaneFrames();"}, missingNeedle)) {
		failureReason = "Bento pane chrome redraw guard changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(source, {"TColorAttr MRPaneEditWindow::mapColor(uchar index)", "if (index == 4 || index == 5) return MREditWindow::mapColor(mPaneFocused ? 13 : 1);", "void MRPaneEditWindow::setPaneFocused(bool focused) noexcept", "void MRPaneEditWindow::drawPaneScrollBars() noexcept"}, missingNeedle)) {
		failureReason = "Bento tool-pane scrollbar focus color guard changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(source, {"TColorAttr MRBentoBox::mapColor(uchar index)", "sourceScrollBarPaletteActive && (index == 4 || index == 5)", "void MRBentoBox::drawSourcePaneScrollBars() noexcept", "sourceScrollBarPaletteActive = true;", "sourceScrollBarPaletteActive = false;", "writeBuf(verticalBounds.a.x, horizontalBounds.a.y, 1, 1, buffer);", "drawSourcePaneScrollBars();"}, missingNeedle)) {
		failureReason = "Bento source-pane scrollbar focus color guard changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(header, {"virtual TColorAttr mapColor(uchar index) override;", "void setPaneFocused(bool focused) noexcept;", "void drawPaneScrollBars() noexcept;", "void drawSourcePaneScrollBars() noexcept;", "void postCloseCommand() noexcept;", "bool sourceScrollBarPaletteActive;"}, missingNeedle)) {
		failureReason = "Bento scrollbar focus API guard changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(frameSource, {"kFocusedDragIcon = \"\\xCD\\xBC\"", "kFocusedDragLeftIcon = \"\\xC8\\xCD\"", "f == 9 ? kFocusedDragLeftIcon : kDragLeftIcon", "f == 9 ? kFocusedDragIcon : kDragIcon"}, missingNeedle)) {
		failureReason = "Focused double-frame resize corner guard changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(source, {"void MRBentoBox::postCloseCommand() noexcept", "event.message.command = cmClose;", "event.message.infoPtr = this;", "putEvent(event);", "bool MRBentoBox::handleOuterFrameCloseMouse(TEvent &event)", "postCloseCommand();"}, missingNeedle)) {
		failureReason = "Bento outer frame close routing changed: missing " + missingNeedle + ".";
		return false;
	}
	if (source.find("message(this, evCommand, cmClose") != std::string::npos) {
		failureReason = "Bento close routing must not synchronously close the BentoBox from a mouse handler.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testMyersDiffCoreHarness(std::string &failureReason) {
	std::vector<mr::diff::MRDiffHunk> hunks;
	std::string errorText;
	std::size_t deleteCount = 0;
	std::size_t insertCount = 0;
	std::size_t equalCount = 0;

	const std::vector<std::string> leftLines{
	    "alpha",
	    "keep",
	    "old",
	    "tail",
	};
	const std::vector<std::string> rightLines{
	    "alpha",
	    "keep",
	    "new",
	    "tail",
	    "extra",
	};
	if (!mr::diff::mrComputeMyersDiff(leftLines, rightLines, hunks, &errorText)) {
		failureReason = "Myers diff failed: " + errorText;
		return false;
	}
	if (!validateDiffReconstructsRight(leftLines, rightLines, hunks, &deleteCount, &insertCount, &equalCount, failureReason)) return false;
	if (deleteCount != 1 || insertCount != 2 || equalCount != 3) {
		failureReason = "Unexpected diff counts: delete=" + std::to_string(deleteCount) + " insert=" + std::to_string(insertCount) + " equal=" + std::to_string(equalCount) + ".";
		return false;
	}
	if (hunks.empty() || hunks.front().op != mr::diff::MRDiffOp::Equal || hunks.front().count != 2) {
		failureReason = "Myers diff did not preserve the leading equal run.";
		return false;
	}

	const std::vector<std::string> emptyLines;
	const std::vector<std::string> singleLine{"solo"};
	if (!mr::diff::mrComputeMyersDiff(emptyLines, singleLine, hunks, &errorText)) {
		failureReason = "Myers diff empty-left failed: " + errorText;
		return false;
	}
	if (!validateDiffReconstructsRight(emptyLines, singleLine, hunks, &deleteCount, &insertCount, &equalCount, failureReason)) return false;
	if (deleteCount != 0 || insertCount != 1 || equalCount != 0) {
		failureReason = "Unexpected empty-left diff counts.";
		return false;
	}

	if (!mr::diff::mrComputeMyersDiff(singleLine, emptyLines, hunks, &errorText)) {
		failureReason = "Myers diff empty-right failed: " + errorText;
		return false;
	}
	if (!validateDiffReconstructsRight(singleLine, emptyLines, hunks, &deleteCount, &insertCount, &equalCount, failureReason)) return false;
	if (deleteCount != 1 || insertCount != 0 || equalCount != 0) {
		failureReason = "Unexpected empty-right diff counts.";
		return false;
	}

	if (!mr::diff::mrComputeMyersDiff(singleLine, singleLine, hunks, &errorText)) {
		failureReason = "Myers diff identical failed: " + errorText;
		return false;
	}
	if (!validateDiffReconstructsRight(singleLine, singleLine, hunks, &deleteCount, &insertCount, &equalCount, failureReason)) return false;
	if (deleteCount != 0 || insertCount != 0 || equalCount != 1) {
		failureReason = "Unexpected identical diff counts.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testFileCompareCoprocessorHarness(std::string &failureReason) {
	const std::vector<std::string> leftLines{
	    "one",
	    "two",
	    "three",
	    "five",
	};
	const std::vector<std::string> rightLines{
	    "one",
	    "two",
	    "four",
	    "five",
	};
	const std::size_t originalDocumentId = 101;
	const std::size_t originalBaseVersion = 7;
	const std::size_t compareDocumentId = 202;
	const std::size_t compareBaseVersion = 9;
	mr::coprocessor::Coprocessor coprocessor;
	mr::coprocessor::Result capturedResult;
	bool captured = false;

	coprocessor.setResultHandler([&capturedResult, &captured](const mr::coprocessor::Result &result) {
		capturedResult = result;
		captured = true;
	});

	const std::uint64_t taskId = coprocessor.submit(mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FileCompare, originalDocumentId, originalBaseVersion, mr::coprocessor::ExecutionOwnerKind::Worker, 9001, "file compare regression", [leftLines, rightLines, originalDocumentId, originalBaseVersion, compareDocumentId, compareBaseVersion](const mr::coprocessor::TaskInfo &task) {
		mr::coprocessor::Result result;
		std::vector<mr::diff::MRDiffHunk> hunks;
		std::string errorText;

		result.task = task;
		if (!mr::diff::mrComputeMyersDiff(leftLines, rightLines, hunks, &errorText, task.cancelFlag.get())) {
			result.status = task.cancelRequested() ? mr::coprocessor::TaskStatus::Cancelled : mr::coprocessor::TaskStatus::Failed;
			result.error = errorText;
			return result;
		}

		result.status = mr::coprocessor::TaskStatus::Completed;
		result.payload = std::make_shared<mr::coprocessor::FileComparePayload>(originalDocumentId, originalBaseVersion, compareDocumentId, compareBaseVersion, leftLines.size(), rightLines.size(), std::move(hunks));
		return result;
	});

	if (taskId == 0) {
		coprocessor.shutdown();
		failureReason = "File compare coprocessor task was not submitted.";
		return false;
	}

	const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!captured && std::chrono::steady_clock::now() < deadline) {
		coprocessor.pumpFor(std::chrono::milliseconds(1));
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	coprocessor.shutdown(true);

	if (!captured) {
		failureReason = "File compare coprocessor task did not produce a result.";
		return false;
	}
	if (!capturedResult.completed()) {
		failureReason = "File compare coprocessor task failed: " + capturedResult.error;
		return false;
	}
	if (capturedResult.task.id != taskId || capturedResult.task.kind != mr::coprocessor::TaskKind::FileCompare || capturedResult.task.documentId != originalDocumentId || capturedResult.task.baseVersion != originalBaseVersion) {
		failureReason = "File compare coprocessor task metadata changed unexpectedly.";
		return false;
	}

	const std::shared_ptr<const mr::coprocessor::FileComparePayload> payload = std::dynamic_pointer_cast<const mr::coprocessor::FileComparePayload>(capturedResult.payload);
	if (payload == nullptr) {
		failureReason = "File compare coprocessor result did not carry a FileComparePayload.";
		return false;
	}
	if (payload->originalDocumentId != originalDocumentId || payload->originalBaseVersion != originalBaseVersion || payload->compareDocumentId != compareDocumentId || payload->compareBaseVersion != compareBaseVersion) {
		failureReason = "File compare payload document/version metadata mismatch.";
		return false;
	}
	if (payload->originalLineCount != leftLines.size() || payload->compareLineCount != rightLines.size()) {
		failureReason = "File compare payload line-count metadata mismatch.";
		return false;
	}

	std::size_t deleteCount = 0;
	std::size_t insertCount = 0;
	std::size_t equalCount = 0;
	if (!validateDiffReconstructsRight(leftLines, rightLines, payload->hunks, &deleteCount, &insertCount, &equalCount, failureReason)) return false;
	if (deleteCount != 1 || insertCount != 1 || equalCount != 3) {
		failureReason = "Unexpected coprocessor diff counts: delete=" + std::to_string(deleteCount) + " insert=" + std::to_string(insertCount) + " equal=" + std::to_string(equalCount) + ".";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testCoprocessorPacketMetadataHarness(std::string &failureReason) {
	static constexpr std::uint64_t kGeneration = 47;
	static constexpr std::uint64_t kPacketStart = 100;
	static constexpr std::uint64_t kPacketEnd = 240;
	cpu_set_t originalCoreSet;
	cpu_set_t probeCoreSet;
	bool coreSetRestricted = false;

	CPU_ZERO(&originalCoreSet);
	CPU_ZERO(&probeCoreSet);
	if (sched_getaffinity(0, sizeof(originalCoreSet), &originalCoreSet) == 0) {
		std::size_t selectedCoreCount = 0;
		for (int core = 0; core < CPU_SETSIZE && selectedCoreCount < 2; ++core) {
			if (!CPU_ISSET(core, &originalCoreSet)) continue;
			CPU_SET(core, &probeCoreSet);
			++selectedCoreCount;
		}
		if (selectedCoreCount != 0 && sched_setaffinity(0, sizeof(probeCoreSet), &probeCoreSet) == 0) coreSetRestricted = true;
	}

	mr::coprocessor::Coprocessor coprocessor;
	if (coreSetRestricted && sched_setaffinity(0, sizeof(originalCoreSet), &originalCoreSet) != 0) {
		coprocessor.shutdown();
		failureReason = "could not restore the process CPU affinity after constructing the modulo probe.";
		return false;
	}
	const mr::coprocessor::WorkerTelemetrySnapshot initialTelemetry = coprocessor.telemetrySnapshot();
	if (initialTelemetry.allowedCoreIds.empty()) {
		coprocessor.shutdown();
		failureReason = "coprocessor reported no allowed CPU cores.";
		return false;
	}
	const std::size_t finiteWorkerCount = initialTelemetry.allowedCoreIds.size() + 3;
	mr::coprocessor::Result capturedResult;
	mr::coprocessor::WorkerTelemetrySnapshot telemetry;
	std::uint64_t metadataTaskId = 0;
	std::uint64_t persistentWorkerOrdinal = mr::coprocessor::kInvalidWorkerOrdinal;
	std::size_t completedResultCount = 0;
	std::atomic<std::size_t> finiteWorkerStartCount(0);
	std::atomic_bool releaseFiniteWorkers(false);
	bool captured = false;
	std::atomic_bool persistentWorkerEntered(false);
	bool persistentResultCancelled = false;

	coprocessor.setResultHandler([&capturedResult, &captured, &metadataTaskId, &persistentWorkerOrdinal, &completedResultCount, &persistentResultCancelled](const mr::coprocessor::Result &result) {
		++completedResultCount;
		if (result.task.id == metadataTaskId) {
			capturedResult = result;
			captured = true;
		}
		if (result.task.workerOrdinal == persistentWorkerOrdinal && result.cancelled()) persistentResultCancelled = true;
	});
	for (std::size_t workerIndex = 0; workerIndex < finiteWorkerCount; ++workerIndex) {
		const bool metadataPacket = workerIndex == 0;
		const std::uint64_t taskId = coprocessor.submitPacket(
		    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::DisplayWidthWarmup, 313, 17, mr::coprocessor::ExecutionOwnerKind::EditorWindow, 313, metadataPacket ? kGeneration : 0, metadataPacket ? mr::coprocessor::WorkDirection::Eof : mr::coprocessor::WorkDirection::None, metadataPacket ? kPacketStart : 0, metadataPacket ? kPacketEnd : 0, "packet modulo regression",
		    [&finiteWorkerStartCount, &releaseFiniteWorkers](const mr::coprocessor::TaskInfo &task) {
			    finiteWorkerStartCount.fetch_add(1, std::memory_order_acq_rel);
			    while (!releaseFiniteWorkers.load(std::memory_order_acquire) && !task.cancelRequested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
			    mr::coprocessor::Result result;
			    result.task = task;
			    result.status = task.cancelRequested() ? mr::coprocessor::TaskStatus::Cancelled : mr::coprocessor::TaskStatus::Completed;
			    return result;
		    });
		if (taskId == 0) {
			releaseFiniteWorkers.store(true, std::memory_order_release);
			coprocessor.shutdown();
			failureReason = "finite packet worker was not submitted.";
			return false;
		}
		if (metadataPacket) metadataTaskId = taskId;
	}

	const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (finiteWorkerStartCount.load(std::memory_order_acquire) < finiteWorkerCount && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
	if (finiteWorkerStartCount.load(std::memory_order_acquire) != finiteWorkerCount) {
		releaseFiniteWorkers.store(true, std::memory_order_release);
		coprocessor.shutdown();
		failureReason = "same-lane finite workers were serialized before the common release barrier.";
		return false;
	}
	releaseFiniteWorkers.store(true, std::memory_order_release);
	while (completedResultCount < finiteWorkerCount && std::chrono::steady_clock::now() < deadline) {
		coprocessor.pumpFor(std::chrono::milliseconds(1));
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	while (completedResultCount == finiteWorkerCount && std::chrono::steady_clock::now() < deadline) {
		telemetry = coprocessor.telemetrySnapshot();
		if (telemetry.finishedCount == finiteWorkerCount) break;
		coprocessor.pumpFor(std::chrono::milliseconds(1));
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	telemetry = coprocessor.telemetrySnapshot();
	if (completedResultCount != finiteWorkerCount || telemetry.finishedCount != finiteWorkerCount) {
		coprocessor.shutdown();
		failureReason = "finite packet workers did not complete and finish before the deadline.";
		return false;
	}

	persistentWorkerOrdinal = coprocessor.registerWorker(mr::coprocessor::Lane::Compute, mr::coprocessor::ExecutionOwnerKind::Worker, 777);
	if (persistentWorkerOrdinal == mr::coprocessor::kInvalidWorkerOrdinal) {
		coprocessor.shutdown();
		failureReason = "persistent worker was not registered.";
		return false;
	}
	const std::uint64_t persistentTaskId = coprocessor.submitWorker(
	    persistentWorkerOrdinal, mr::coprocessor::TaskKind::Custom, 0, 0, "persistent stop and join regression",
	    [&persistentWorkerEntered](const mr::coprocessor::TaskInfo &task) {
		    persistentWorkerEntered.store(true, std::memory_order_release);
		    while (!task.cancelRequested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
		    mr::coprocessor::Result result;
		    result.task = task;
		    result.status = mr::coprocessor::TaskStatus::Cancelled;
		    return result;
	    });
	if (persistentTaskId == 0) {
		coprocessor.unregisterWorker(persistentWorkerOrdinal);
		coprocessor.shutdown();
		failureReason = "persistent worker task was not submitted.";
		return false;
	}
	const std::chrono::steady_clock::time_point persistentDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!persistentWorkerEntered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < persistentDeadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
	if (!persistentWorkerEntered.load(std::memory_order_acquire)) {
		coprocessor.unregisterWorker(persistentWorkerOrdinal);
		coprocessor.shutdown();
		failureReason = "persistent worker did not enter its task before the deadline.";
		return false;
	}
	coprocessor.unregisterWorker(persistentWorkerOrdinal);
	const std::chrono::steady_clock::time_point stopDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while ((!persistentResultCancelled || coprocessor.telemetrySnapshot().finishedCount != finiteWorkerCount + 1) && std::chrono::steady_clock::now() < stopDeadline) {
		coprocessor.pumpFor(std::chrono::milliseconds(1));
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	coprocessor.shutdown(true);
	telemetry = coprocessor.telemetrySnapshot(4096);
	if (!persistentResultCancelled || telemetry.createdCount != finiteWorkerCount + 1 || telemetry.finishedCount != finiteWorkerCount + 1 || !telemetry.workers.empty()) {
		failureReason = "persistent worker stop did not produce a cancelled result and a balanced joined lifecycle.";
		return false;
	}

	std::vector<bool> assignedOrdinals(finiteWorkerCount + 1, false);
	std::vector<bool> finishedOrdinals(finiteWorkerCount + 1, false);
	bool sawPersistentStop = false;
	for (const mr::coprocessor::WorkerLifecycleEvent &event : telemetry.recentEvents) {
		if (event.workerOrdinal >= assignedOrdinals.size()) continue;
		if (event.state == mr::coprocessor::WorkerLifecycleState::Assigned) {
			const int expectedCore = telemetry.allowedCoreIds[static_cast<std::size_t>(event.workerOrdinal % telemetry.allowedCoreIds.size())];
			if (event.assignedCore != expectedCore || event.affinityResult != 0 || event.osThreadId == 0) {
				failureReason = "worker ordinal was not assigned to the exact modulo-selected CPU core.";
				return false;
			}
			assignedOrdinals[static_cast<std::size_t>(event.workerOrdinal)] = true;
		}
		if (event.state != mr::coprocessor::WorkerLifecycleState::Finished) continue;
		finishedOrdinals[static_cast<std::size_t>(event.workerOrdinal)] = true;
		if (event.workerOrdinal == persistentWorkerOrdinal && event.reason == mr::coprocessor::LifecycleReason::StopRequested) sawPersistentStop = true;
	}
	for (std::size_t workerIndex = 0; workerIndex < assignedOrdinals.size(); ++workerIndex) {
		if (assignedOrdinals[workerIndex] && finishedOrdinals[workerIndex]) continue;
		failureReason = "worker lifecycle omitted an assigned or finished event.";
		return false;
	}
	if (!sawPersistentStop) {
		failureReason = "persistent worker lifecycle omitted the explicit stop-requested finish reason.";
		return false;
	}
	if (!captured || !capturedResult.completed()) {
		failureReason = "packet metadata task did not complete.";
		return false;
	}
	const mr::coprocessor::TaskInfo &task = capturedResult.task;
	if (task.id != metadataTaskId || task.kind != mr::coprocessor::TaskKind::DisplayWidthWarmup || task.generation != kGeneration || task.direction != mr::coprocessor::WorkDirection::Eof || !task.hasPacketSpan ||
	    task.packetStart != kPacketStart || task.packetEnd != kPacketEnd) {
		failureReason = "packet task metadata was not preserved through worker execution.";
		return false;
	}
	failureReason.clear();
	return true;
}


bool testFileCompareBentoWiringGuard(std::string &failureReason) {
	const std::string commandsPath = absolutePathFromCwd("app/MRCommands.hpp");
	const std::string menuPath = absolutePathFromCwd("app/MRMenuFactory.cpp");
	const std::string appStatePath = absolutePathFromCwd("app/MRAppState.cpp");
	const std::string editorAppPath = absolutePathFromCwd("app/MREditorApp.cpp");
	const std::string routerPath = absolutePathFromCwd("app/MRCommandRouter.cpp");
	const std::string windowCommandsHeaderPath = absolutePathFromCwd("app/commands/MRWindowCommands.hpp");
	const std::string windowCommandsPath = absolutePathFromCwd("app/commands/MRWindowCommands.cpp");
	const std::string windowListHeaderPath = absolutePathFromCwd("dialogs/MRWindowList.hpp");
	const std::string windowListPath = absolutePathFromCwd("dialogs/MRWindowList.cpp");
	const std::string bentoHeaderPath = absolutePathFromCwd("ui/MRBentoBox/MRBentoBox.hpp");
	const std::string bentoProjectionPath = absolutePathFromCwd("ui/MRBentoBox/MRBentoBoxProjection.cpp");
	const std::string dispatchPath = absolutePathFromCwd("coprocessor/MRCoprocessorDispatch.cpp");
	const std::string catalogPath = absolutePathFromCwd("keymap/MRKeymapActionCatalog.cpp");
	std::string commands;
	std::string menu;
	std::string appState;
	std::string editorApp;
	std::string router;
	std::string windowCommandsHeader;
	std::string windowCommands;
	std::string windowListHeader;
	std::string windowList;
	std::string bentoHeader;
	std::string bentoProjection;
	std::string dispatch;
	std::string catalog;
	std::string ioError;
	std::string missingNeedle;

	if (!readTextFile(commandsPath, commands, ioError) || !readTextFile(menuPath, menu, ioError) || !readTextFile(appStatePath, appState, ioError) || !readTextFile(editorAppPath, editorApp, ioError) || !readTextFile(routerPath, router, ioError) || !readTextFile(windowCommandsHeaderPath, windowCommandsHeader, ioError) || !readTextFile(windowCommandsPath, windowCommands, ioError) || !readTextFile(windowListHeaderPath, windowListHeader, ioError) || !readTextFile(windowListPath, windowList, ioError) || !readTextFile(bentoHeaderPath, bentoHeader, ioError) || !readTextFile(bentoProjectionPath, bentoProjection, ioError) || !readTextFile(dispatchPath, dispatch, ioError) || !readTextFile(catalogPath, catalog, ioError)) {
		failureReason = "Unable to read file-compare wiring source: " + ioError;
		return false;
	}
	if (!containsAllSubstrings(commands, {"cmMrTextFileCompare", "cmMrFileCompareApplyOriginalToCompare", "cmMrFileCompareApplyCompareToOriginal"}, missingNeedle) || !containsAllSubstrings(menu, {"file co~M~pare...", "cmMrTextFileCompare", "apply original -> compare", "apply compare -> original"}, missingNeedle) || !containsAllSubstrings(appState, {"setCommandEnabled(cmMrTextFileCompare, hasEditor && hasMultipleWindows)", "setCommandEnabled(cmMrFileCompareApplyOriginalToCompare, state.hasFileCompareWindow)", "setCommandEnabled(cmMrFileCompareApplyCompareToOriginal, state.hasFileCompareWindow)"}, missingNeedle)) {
		failureReason = "File compare text-menu command wiring changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(editorApp, {"cmMrFileCompareApplyOriginalToCompare", "cmMrFileCompareApplyCompareToOriginal", "bentoBox->applyFileCompareChange(applyOriginalToCompare)"}, missingNeedle)) {
		failureReason = "File compare edit command routing changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(catalog, {"MR_TEXT_FILE_COMPARE"}, missingNeedle) || !containsAllSubstrings(router, {"KeymapActionDispatchEntry{\"MR_TEXT_FILE_COMPARE\", KeymapDispatchKind::AppCommand, cmMrTextFileCompare", "case cmMrTextFileCompare:", "return handleTextFileCompare();"}, missingNeedle)) {
		failureReason = "File compare keymap/router dispatch wiring changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(router, {"mrShowWindowListDialog(mrwlSelectFileCompareTarget, originalWindow)", "createFileCompareBentoBoxWindow(title.c_str())", "compareBento->initializeFileCompare(setup)", "mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FileCompare"}, missingNeedle)) {
		failureReason = "File compare command flow changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(windowCommandsHeader, {"MRBentoBox *createFileCompareBentoBoxWindow(const char *title);"}, missingNeedle) || !containsAllSubstrings(windowCommands, {"MRBentoBox *createFileCompareBentoBoxWindow(const char *title)", "bbmFileCompare"}, missingNeedle)) {
		failureReason = "File compare Bento window factory changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(windowListHeader, {"mrwlSelectFileCompareTarget"}, missingNeedle) || !containsAllSubstrings(windowList, {"isFileCompareWindowListCandidate", "mode == mrwlSelectFileCompareTarget", "!isFileCompareWindowListCandidate(windows[i], current)"}, missingNeedle)) {
		failureReason = "Window list file-compare target filtering changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(bentoHeader, {"bprDiffOriginal", "bprDiffCompare", "bbmFileCompare", "MRBentoCompareSetup", "initializeFileCompare", "applyFileCompareResult", "applyFileCompareChange", "restoreFileCompareSources", "syncFileCompareLinkedPaneFrom", "showFileCompareActionList", "acceptFileCompareActionChoice", "fileCompareActionDropList", "FileCompareChangeGroup", "rebuildFileCompareChangeGroups", "fileCompareStatusForLeaf", "fileCompareChangeGroups", "fileCompareChangeGroupIndexAtCursor", "fileCompareChangeGroupIndexAtLine", "fileCompareGroupNavigationLineForRole", "moveFileCompareEditorToGroup", "applyFileCompareChangeGroup", "pendingFileCompareActionGroupIndex"}, missingNeedle)) {
		failureReason = "Bento file-compare public surface changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(bentoProjection, {"{bprDiffOriginal, \"Diff Original\", true}", "{bprDiffCompare, \"Diff Compare\", true}", "bentoMode == bbmFileCompare && !bentoRoleIsDiff(role)", "if (bentoMode == bbmFileCompare)", "source.role = bprDiffOriginal", "configuredFileCompareStartConfiguration()", "refreshFileCompareCachedSnapshots(bprSource, true)", "mr::diff::mrSplitTextLinesForDiff(source.text, lineCache)", "mr::diff::mrSplitTextLinesForDiff(fileCompareSetup.original.text, fileCompareOriginalLines)", "originalLines = fileCompareOriginalLines, compareLines = fileCompareCompareLines", "payload->originalDocumentId != fileCompareSetup.original.documentId", "fileCompareSourceStillMatches(fileCompareSetup.original)", "appendDiffDisplayLine(text, lineKinds", "mrfclkMissing", "mrfclkInsert", "mrfclkOffset", "configuredFileCompareOriginalLeadingGutters()", "configuredFileCompareOriginalTrailingGutters()", "configuredFileCompareCompareLeadingGutters()", "configuredFileCompareCompareTrailingGutters()", "targetEditor->setFileCompareGutters(leadingGutters, trailingGutters)", "targetEditor->setFileCompareLineKinds(lineKinds)", "targetEditor->setMiniMapSuppressed(!miniMapConfigured)", "targetEditor->setFileCompareGutterVisible(true)", "syncFileCompareLinkedPaneFrom(activeLeafId)", "syncFileCompareLinkedPaneFrom(0)", "displayStartLine", "displayLineCount", "deletedLineCount", "insertedLineCount", "rebuildFileCompareChangeGroups();", "std::string MRBentoBox::fileCompareStatusForLeaf", "firstVisibleChange", "lastVisibleChange", "visibleDeletedLines", "visibleInsertedLines", "totalDeletedLines", "totalInsertedLines", "status += \"/\" + std::to_string(fileCompareChangeGroups.size())", "status += \" -\" + std::to_string(totalDeletedLines)", "bool MRBentoBox::applyFileCompareChange(bool originalToCompare)", "bool MRBentoBox::applyFileCompareChangeGroup(bool originalToCompare, const FileCompareChangeGroup &group)", "normalizeFileCompareHunks(fileCompareOriginalLines, fileCompareCompareLines, fileCompareHunks);", "fileCompareJoinedLineRange", "fileCompareEditorLineRange", "targetEditor->replaceRangeAndSelect", "targetEditor->setSelectionOffsets(selectionEnd, selectionEnd, False)", "refreshFileCompareAfterSourceMutation(targetRole);", "fileCompareHunks.clear();", "fileCompareDiffReady = false;", "kFileCompareActionApply", "apply diff", "cmMrFileComparePaneActionAccepted", "showFileCompareActionList(event.mouse.where, targetLeafId)", "fileCompareChangeGroupIndexAtLine", "fileCompareGroupNavigationLineForRole", "pendingFileCompareActionGroupIndex", "moveFileCompareEditorToGroup", "editor.moveCursorToDocumentLineTop(targetLine, 0)", "cursorGroupIndex", "targetIndex = next ?"}, missingNeedle)) {
		failureReason = "Bento file-compare role/display/version wiring changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(dispatch, {"result.task.kind == mr::coprocessor::TaskKind::FileCompare", "handleFileCompareResult(result)", "bentoBox->applyFileCompareResult(result)", "recordTaskPerformance(result, \"File compare\""}, missingNeedle)) {
		failureReason = "File compare coprocessor dispatch wiring changed: missing " + missingNeedle + ".";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testWorkspaceAutosaveLazyWiringGuard(std::string &failureReason) {
	const std::string windowCommandsHeaderPath = absolutePathFromCwd("app/commands/MRWindowCommands.hpp");
	const std::string workspaceRuntimePath = absolutePathFromCwd("app/commands/MRWorkspaceRuntime.cpp");
	const std::string workspaceCommandsPath = absolutePathFromCwd("app/commands/MRWorkspaceCommands.cpp");
	const std::string windowListPath = absolutePathFromCwd("dialogs/MRWindowList.cpp");
	const std::string editWindowPath = absolutePathFromCwd("ui/MREditWindow.hpp");
	const std::string editorAppPath = absolutePathFromCwd("app/MREditorApp.cpp");
	const std::string bentoProjectionPath = absolutePathFromCwd("ui/MRBentoBox/MRBentoBoxProjection.cpp");
	const std::string settingsStorageHeaderPath = absolutePathFromCwd("config/settings/MRSettingsStorage.hpp");
	const std::string settingsRuntimePath = absolutePathFromCwd("config/settings/MRSettingsRuntime.cpp");
	std::string windowCommandsHeader;
	std::string windowCommands;
	std::string windowList;
	std::string editWindow;
	std::string editorApp;
	std::string bentoProjection;
	std::string settingsStorageHeader;
	std::string settingsRuntime;
	std::string flushBody;
	std::string loadWorkspaceBody;
	std::string ioError;
	std::string missingNeedle;

	if (!readTextFile(windowCommandsHeaderPath, windowCommandsHeader, ioError) || !readTextFile(workspaceRuntimePath, windowCommands, ioError) || !readTextFile(workspaceCommandsPath, loadWorkspaceBody, ioError) || !readTextFile(windowListPath, windowList, ioError) || !readTextFile(editWindowPath, editWindow, ioError) || !readTextFile(editorAppPath, editorApp, ioError) || !readTextFile(bentoProjectionPath, bentoProjection, ioError) || !readTextFile(settingsStorageHeaderPath, settingsStorageHeader, ioError) || !readTextFile(settingsRuntimePath, settingsRuntime, ioError)) {
		failureReason = "Unable to read workspace autosave lazy wiring sources: " + ioError;
		return false;
	}
	if (!containsAllSubstrings(windowCommandsHeader, {"void mrMarkWorkspaceAutosaveDirty(const char *source, const MREditWindow *window = nullptr);", "void mrFlushWorkspaceAutosaveIfDue();", "void mrFlushWorkspaceAutosaveNow();"}, missingNeedle)) {
		failureReason = "Workspace autosave lazy public wiring changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(windowCommands, {"applicationUiInt(runtimeKv, kWorkspaceBranch, \"autosaveDirty\", 0)", "storeApplicationUiUnsigned(runtimeKv, kWorkspaceBranch, \"autosaveDueMs\"", "kWorkspaceAutosaveDelay", "configuredAutosaveWorkspace()", "setRuntimePreserveAutosavedWorkspace(false)", "runtimePreserveAutosavedWorkspace()", "Workspace autosave dirty false->true source=", "Workspace autosave dirty true->false source=flush", "Workspace autosave dirty false->true source=flush-failed.", "persistConfiguredSettingsSnapshotWithWorkspace(&errorText, &report)", "mrLogSettingsWriteReport(\"workspace autosave\", report)"}, missingNeedle)) {
		failureReason = "Workspace autosave lazy persistence path changed: missing " + missingNeedle + ".";
		return false;
	}
	const std::size_t flushStart = windowCommands.find("void flushWorkspaceAutosave(bool force)");
	const std::size_t flushEnd = windowCommands.find("\n} // namespace", flushStart);
	if (flushStart == std::string::npos || flushEnd == std::string::npos) {
		failureReason = "Unable to isolate workspace autosave flush helper.";
		return false;
	}
	flushBody = windowCommands.substr(flushStart, flushEnd - flushStart);
	if (flushBody.find("mrSaveWorkspace(") != std::string::npos || flushBody.find("writeTextFile(") != std::string::npos) {
		failureReason = "Workspace autosave lazy flush must not call mrSaveWorkspace directly.";
		return false;
	}
	if (flushBody.find("setRuntimePreserveAutosavedWorkspace(false)") != std::string::npos || flushBody.find("if (runtimePreserveAutosavedWorkspace()) return;") == std::string::npos) {
		failureReason = "Workspace autosave flush must respect preserved autosaved workspace state.";
		return false;
	}
	if (flushBody.find("!force && steadyClockMilliseconds(std::chrono::steady_clock::now()) < applicationUiUnsigned(runtimeKv, kWorkspaceBranch, \"autosaveDueMs\", 0)") == std::string::npos) {
		failureReason = "Workspace autosave flush must support delayed idle flush and forced quit flush.";
		return false;
	}
	const std::size_t loadStart = loadWorkspaceBody.find("void mrLoadWorkspace(const std::string &filename)");
	if (loadStart == std::string::npos) {
		failureReason = "Unable to isolate mrLoadWorkspace.";
		return false;
	}
	loadWorkspaceBody = loadWorkspaceBody.substr(loadStart);
	if (loadWorkspaceBody.find("mrSaveWorkspace(") != std::string::npos) {
		failureReason = "Workspace load must not rewrite its source when entries cannot be restored.";
		return false;
	}
	if (!containsAllSubstrings(loadWorkspaceBody, {"parsedWorkspaceEntries", "loadedWorkspaceEntries", "setRuntimePreserveAutosavedWorkspace(true)"}, missingNeedle)) {
		failureReason = "Workspace load must preserve autosaved source when no entries restore: missing " + missingNeedle + ".";
		return false;
	}
	const std::size_t geometryApply = loadWorkspaceBody.find("applyWorkspaceEntryGeometry(win, entry)");
	const std::size_t bentoRestore = loadWorkspaceBody.find("bentoBox->restoreWorkspaceSnapshot(entry.bentoSnapshot)");
	if (geometryApply == std::string::npos || bentoRestore == std::string::npos || geometryApply > bentoRestore) {
		failureReason = "Workspace load must apply outer Bento bounds before restoring the Bento snapshot.";
		return false;
	}
	if (settingsStorageHeader.find("persistConfiguredSettingsSnapshotWithWorkspace") == std::string::npos) {
		failureReason = "Settings storage must expose a dedicated workspace snapshot persist path.";
		return false;
	}
	if (!containsAllSubstrings(settingsRuntime, {"persistConfiguredSettingsSnapshotWithMode(false", "persistConfiguredSettingsSnapshotWithMode(true", "includeWorkspace ? buildSettingsMacroSourceWithWorkspace(paths) : buildSettingsMacroSourcePreservingWorkspace(paths, previousSource)", "source = buildSettingsMacroSourcePreservingWorkspace(paths, previousSource);"}, missingNeedle)) {
		failureReason = "Settings persistence must separate normal settings writes from workspace autosave writes: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(windowList, {"void mrNotifyWindowTopologyChanged()", "mrMarkWorkspaceAutosaveDirty(\"window topology\");"}, missingNeedle)) {
		failureReason = "Workspace topology changes must mark lazy autosave dirty: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(editWindow, {"const TRect previousBounds = getBounds();", "if (previousBounds != getBounds()) mrMarkWorkspaceAutosaveDirty(\"window bounds\", this);"}, missingNeedle)) {
		failureReason = "Workspace window geometry changes must mark lazy autosave dirty: missing " + missingNeedle + ".";
		return false;
	}
	if (editorApp.find("mrFlushWorkspaceAutosaveIfDue();") == std::string::npos) {
		failureReason = "Editor idle loop must flush lazy workspace autosave.";
		return false;
	}
	if (editorApp.find("mrFlushWorkspaceAutosaveNow();") == std::string::npos) {
		failureReason = "Quit path must force pending lazy workspace autosave before settings snapshot.";
		return false;
	}
	if (editorApp.find("setRuntimePreserveAutosavedWorkspace(true);\n\t\tconst mr::dialogs::UnsavedChangesChoice choice = mr::dialogs::showWorkspaceLoadDialog") == std::string::npos) {
		failureReason = "Workspace autoload prompt must preserve autosaved workspace before the user chooses.";
		return false;
	}
	if (!containsAllSubstrings(bentoProjection, {"mrMarkWorkspaceAutosaveDirty(\"bento divider\", this);", "mrMarkWorkspaceAutosaveDirty(\"bento maximize\", this);", "setDividerPosition", "toggleLeafMaximized", "closePane"}, missingNeedle)) {
		failureReason = "Bento workspace geometry changes must mark lazy autosave dirty: missing " + missingNeedle + ".";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testWorkspaceCommandLineAutoloadFocusGuard(std::string &failureReason) {
	const std::string startupPath = absolutePathFromCwd("app/MREditorAppStartup.cpp");
	const std::string workspaceCommandsPath = absolutePathFromCwd("app/commands/MRWorkspaceCommands.cpp");
	const std::string dirtyGatingPath = absolutePathFromCwd("dialogs/MRDirtyGating.cpp");
	std::string startup;
	std::string workspaceCommands;
	std::string dirtyGating;
	std::string ioError;
	std::string missingNeedle;
	std::string startupLoadBody;
	std::string constructorBody;
	std::size_t startupLoadStart = std::string::npos;
	std::size_t startupLoadEnd = std::string::npos;
	std::size_t constructorStart = std::string::npos;
	std::size_t constructorEnd = std::string::npos;
	std::size_t autoloadFlag = std::string::npos;
	std::size_t workspaceRestore = std::string::npos;
	std::size_t startupFiles = std::string::npos;

	if (!readTextFile(startupPath, startup, ioError) || !readTextFile(workspaceCommandsPath, workspaceCommands, ioError) || !readTextFile(dirtyGatingPath, dirtyGating, ioError)) {
		failureReason = "Unable to read workspace command-line startup source: " + ioError;
		return false;
	}
	startupLoadStart = startup.find("std::vector<std::string> loadStartupFilesFromCommandLine(const StartupLoadRequest &request, const std::vector<std::string> &files, bool focusRestoredWorkspaceFiles)");
	startupLoadEnd = startup.find("\n} // namespace", startupLoadStart);
	if (startupLoadStart == std::string::npos || startupLoadEnd == std::string::npos) {
		failureReason = "Unable to isolate command-line startup file loading.";
		return false;
	}
	startupLoadBody = startup.substr(startupLoadStart, startupLoadEnd - startupLoadStart);
	if (!containsAllSubstrings(startupLoadBody, {"restoredWindows = allEditWindowsInZOrder()", "candidateEditor->persistentFileName()", "normalizePathForLoad(std::filesystem::path(candidatePath)) != file", "Startup file resolved to restored workspace window:", "const bool newDocument = pathDoesNotExist", "win->setCurrentFileName(file.c_str())", "Startup created empty editor for new file:", "loadedFiles.push_back(file);", "createEditorWindow(file.c_str())", "mrActivateEditWindow(lastStartupWindow)", "return loadedFiles;"}, missingNeedle)) {
		failureReason = "Workspace command-line focus reuse changed: missing " + missingNeedle + ".";
		return false;
	}
	constructorStart = startup.find("MREditorApp::MREditorApp()");
	constructorEnd = startup.size();
	if (constructorStart == std::string::npos || constructorEnd == std::string::npos) {
		failureReason = "Unable to isolate editor startup ordering.";
		return false;
	}
	constructorBody = startup.substr(constructorStart, constructorEnd - constructorStart);
	autoloadFlag = constructorBody.find("const bool autoloadWorkspace = configuredAutoloadWorkspace()");
	workspaceRestore = constructorBody.find("mrLoadWorkspace(\"\")", autoloadFlag);
	startupFiles = constructorBody.find("loadStartupFilesFromCommandLine(startupLoadRequest, requestedStartupFiles, restoreWorkspaceAtStartup)", workspaceRestore);
	if (autoloadFlag == std::string::npos || workspaceRestore == std::string::npos || startupFiles == std::string::npos || !(autoloadFlag < workspaceRestore && workspaceRestore < startupFiles)) {
		failureReason = "Automatic workspace restore must precede command-line file focus and loading.";
		return false;
	}
	if (!containsAllSubstrings(constructorBody, {"const std::vector<std::string> requestedStartupFiles =", "const std::vector<std::string> autosavedWorkspaceFiles = !autoloadWorkspace ? mrSettingsFileAutosavedWorkspaceFiles()",
	                                             "autosavedWorkspaceFiles.size() == 1", "startupFile == workspaceFile", "commandLineForcesWorkspaceRestore = true",
	                                             "const bool restoreWorkspaceAtStartup = autoloadWorkspace || commandLineForcesWorkspaceRestore", "mrLoadWorkspace(\"\")",
	                                             "const std::vector<std::string> startupFiles = loadStartupFilesFromCommandLine(startupLoadRequest, requestedStartupFiles, restoreWorkspaceAtStartup)",
	                                             "if (commandLineForcesWorkspaceRestore)", "singleFileWorkspaceLoadedFromCommandLine = true",
	                                             "if (!autosavedWorkspaceFiles.empty() && !singleFileWorkspaceLoadedFromCommandLine)",
	                                             "showWorkspaceLoadDialog(\"Restore workspace\", autosavedWorkspaceFiles, \"Discard workspace\")"},
	                           missingNeedle)) {
		failureReason = "Manual workspace restore preview or command-line reconciliation changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(workspaceCommands, {"std::vector<std::string> mrSettingsFileAutosavedWorkspaceFiles()", "entry.hasBentoSnapshot && entry.bentoSnapshot.mode == bbmFileCompare && entry.hasFileCompareSources", "files.push_back(entry.fileCompareOriginalUrl);", "files.push_back(entry.fileCompareCompareUrl);", "files.push_back(entry.url);"}, missingNeedle)) {
		failureReason = "Workspace restore preview file enumeration changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(dirtyGating, {"const bool showFileList = fileUrls.size() >= 10;", "const std::size_t numberWidth = std::to_string(fileUrls.size()).size();", "std::string(numberWidth - number.size(), ' ') + number + \" \" + fileUrls[i]", "fileNames.push_back(separator == std::string::npos", "joinCommaSeparatedItems(fileNames) + \" (\" + fileCountText", "const int maximumDialogWidth = std::max(1, desktopWidth - 4);", "const int maximumDialogHeight = std::max(1, desktopHeight - 4);", "\"RESTORE WORKSPACE\"", "hcDialogWorkspaceRestore", "TScrollBar *verticalScrollBar", "TScrollBar *horizontalScrollBar", "MRColumnListView *fileList", "fileCountLine + fileCountText"}, missingNeedle)) {
		failureReason = "Workspace restore preview layout changed: missing " + missingNeedle + ".";
		return false;
	}

	failureReason.clear();
	return true;
}

void runTest(TestContext &ctx, const char *name, bool (*fn)(std::string &)) {
	std::string failure;

	if (fn(failure)) {
		++ctx.passed;
		std::cout << "[PASS] " << name << "\n";
		return;
	}
	++ctx.failed;
	std::cout << "[FAIL] " << name << "\n";
	if (!failure.empty()) std::cout << "       " << failure << "\n";
}

void runCoreSuite(TestContext &ctx) {
	runTest(ctx, "MRSETUP startup-only semantics", testMrsetupStartupOnly);
	runTest(ctx, "Edit clipboard command routing guard", testEditClipboardCommandRoutingGuard);
	runTest(ctx, "Keymap runtime macro dispatch harness", testKeymapMacroBindingDispatchHarness);
	runTest(ctx, "Keymap macro diagnostics harness", testKeymapMacroBindingNegativeDiagnosticsHarness);
	runTest(ctx, "Edit profile case-sensitive extension matching", testEditProfileCaseSensitiveExtensionMatchGuard);
	runTest(ctx, "Edit profile code-language raster", testEditProfileCodeLanguageRasterGuard);
	runTest(ctx, "File extension code-language dialog conformance", testFileExtensionCodeLanguageChoicesGuard);
	runTest(ctx, "DELAY deadline resume harness", testDelayProcWiringGuard);
	runTest(ctx, "Color theme inventory conformance", testColorThemeInventoryConformanceGuard);
	runTest(ctx, "Color theme URI startup load", testMrsetupWindowColorThemeUriStartupLoad);
	runTest(ctx, "Invalid RGB color lists are atomic", testCurrentColorThemeInvalidListsDoNotMutateGuard);
	runTest(ctx, "RGB color theme compile and roundtrip", testWindowColorsThemeVersionAndLineNumbersRoundtrip);
	runTest(ctx, "Fresh color default and terminal palette projection", testTerminalPalettePreservesBiosDefaultsAndUsesXTerm256);
	runTest(ctx, "Code colors preserve configured attributes", testCodeColorUsesConfiguredAttributeGuard);
	runTest(ctx, "TextDocument Piece/AddBuffer mutation harness", testTextDocumentPieceTableMutationHarness);
	runTest(ctx, "Deferred large line-index harness", testDeferredLargeLineIndexHarness);
	runTest(ctx, "EOF marker scroll range guard", testEofMarkerDoesNotExtendScrollRange);
	runTest(ctx, "Post-EOF clear-area guard", testEofVirtualLineColorGuard);
	runTest(ctx, "File extension compiler-profile choices guard", testFileExtensionCompilerProfileChoicesGuard);
	runTest(ctx, "Central runtime K/V authority guard", testCentralRuntimeKvAuthorityGuard);
	runTest(ctx, "Exec session owner and MMP canvas guard", testExecSessionOwnerCancellationGuard);
}

void runFullSuite(TestContext &ctx) {
	runCoreSuite(ctx);
	runTest(ctx, "Runtime scheduler skip event guard", testRuntimeSchedulerSkipEventGuard);
	runTest(ctx, "Screen render facade boundary guard", testScreenRenderFacadeBoundaryGuard);
	runTest(ctx, "MMP client and hotspot dispatch harness", testMmpClientFocusDispatchHarness);
	runTest(ctx, "MMP common collection controls harness", testMmpCollectionControlHarness);
	runTest(ctx, "Message-line Static Mode harness", testMessageLineStaticModeHarness);
	runTest(ctx, "Fullscreen suspends Static Mode wiring", testFullscreenSuspendsStaticModeWiring);
	runTest(ctx, "Workspace command-line autoload focus guard", testWorkspaceCommandLineAutoloadFocusGuard);
}

} // namespace

int main(int argc, char **argv) {
	bool runFull = false;

	if (argc >= 2) {
		if (argc == 3 && std::strcmp(argv[1], "--probe") == 0) {
			ScopedRegressionConfigHome configHome;

			if (!configHome.ready()) {
				std::cerr << "Unable to isolate regression probe configuration.\n";
				return 1;
			}
			if (std::strcmp(argv[2], "staged-nav") == 0) return runStagedNavProbeMode();
			if (std::strcmp(argv[2], "staged-mark-page") == 0) return runStagedMarkPageProbeMode();
			if (std::strcmp(argv[2], "closure-hash-default") == 0) return runClosureHashDefaultProbeMode();
			if (std::strcmp(argv[2], "macro-screen-flush") == 0) return runMacroScreenFlushProbeMode();
			if (std::strcmp(argv[2], "keymap-macro-dispatch") == 0) return runKeymapMacroDispatchProbeMode();
			if (std::strcmp(argv[2], "macro-debugger-breakpoint-kv") == 0) return runMacroDebuggerBreakpointKvProbeMode();
			if (std::strcmp(argv[2], "macro-debugger-cross-section") == 0) return runMacroDebuggerCrossSectionProbeMode();
			if (std::strcmp(argv[2], "macro-debugger-f9-route") == 0) return runMacroDebuggerF9RouteProbeMode();
			if (std::strcmp(argv[2], "macro-debugger-workspace-breakpoint-roundtrip") == 0) return runMacroDebuggerWorkspaceBreakpointRoundtripProbeMode();
		} else if (argc == 2 && std::strcmp(argv[1], "--full") == 0) {
			runFull = true;
		} else if (argc == 2 && std::strcmp(argv[1], "--core") == 0) {
			runFull = false;
		} else {
			std::cerr << "usage: regression/mr-regression-checks "
			             "[--core|--full|--probe staged-nav|staged-mark-page|closure-hash-default|macro-screen-flush|keymap-macro-dispatch|macro-debugger-breakpoint-kv|macro-debugger-cross-section|macro-debugger-f9-route|macro-debugger-workspace-breakpoint-roundtrip]\n";
			return 2;
		}
	}

	TestContext ctx;

	if (runFull) runFullSuite(ctx);
	else
		runCoreSuite(ctx);

	std::cout << "\nRegression summary: " << ctx.passed << " passed, " << ctx.failed << " failed.\n";
	return ctx.failed == 0 ? 0 : 1;
}
