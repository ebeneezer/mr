#include "../app/MRVersion.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#define Uses_TKeys
#include <tvision/tv.h>

#include <algorithm>
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
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "../mrmac/mrmac.h"
#include "../mrmac/MRMacroExecutionSession.hpp"
#include "../mrmac/MRVM.hpp"
#include "../app/MRExecSessionStatus.hpp"
#include "../app/MREditorApp.hpp"
#include "../app/MRCommandRouter.hpp"
#include "../app/MRRuntimeScheduler.hpp"
#include "../app/MRRuntimeTimerSource.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../app/services/MRLspEditorSource.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsEditSetup.hpp"
#include "../config/settings/MRSettingsCompilerProfiles.hpp"
#include "../config/settings/MRSettingsSnapshotIO.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../dialogs/MRAbout.hpp"
#include "../dialogs/setup/MRSetup.hpp"
#include "../diff/MRDiff.hpp"
#include "../piecetable/MRTextDocument.hpp"
#include "../ui/MRBentoBox.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRFileEditor/MRFEBlockOps.hpp"
#include "../ui/MRFileEditor/MRFEBlockOpsTestHarness.hpp"
#include "../ui/MRSidekickEditor.hpp"
#include "../ui/MRWindowSupport.hpp"

class MRBentoBoxFileCompareRegressionHarness {
  public:
	static bool seedDiffReadyState(MRBentoBox &bento, const std::vector<mr::diff::MRDiffHunk> &hunks) {
		bento.fileCompareHunks = hunks;
		bento.rebuildFileCompareChangeGroups();
		bento.fileCompareDiffReady = true;
		bento.fileCompareStale = false;
		bento.refreshFileComparePanes();
		return !bento.fileCompareChangeGroups.empty();
	}

	static bool activateComparePane(MRBentoBox &bento) {
		const int compareLeaf = bento.leafIdForRole(bprDiffCompare);
		if (compareLeaf < 0) return false;
		bento.setActivePane(compareLeaf);
		return true;
	}

	static bool attachSourceBuffer(MRBentoBox &bento, MRBentoPaneRole role, MREditWindow &sourceWindow) {
		const int leafId = bento.leafIdForRole(role);
		MREditWindow *targetWindow = leafId == 0 ? static_cast<MREditWindow *>(&bento) : static_cast<MREditWindow *>(bento.paneWindowForLeaf(leafId));
		MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
		MRFileEditor *sourceEditor = sourceWindow.getEditor();
		if (targetEditor == nullptr || sourceEditor == nullptr) return false;
		targetEditor->shareContentStateFrom(*sourceEditor);
		return true;
	}

	static MRFileEditor *activeEditor(MRBentoBox &bento) {
		MREditWindow *window = bento.activeLeafId == 0 ? static_cast<MREditWindow *>(&bento) : static_cast<MREditWindow *>(bento.paneWindowForLeaf(bento.activeLeafId));
		return window != nullptr ? window->getEditor() : nullptr;
	}

	static MRFileEditor *editorForRole(MRBentoBox &bento, MRBentoPaneRole role) {
		const int leafId = bento.leafIdForRole(role);
		MREditWindow *window = leafId == 0 ? static_cast<MREditWindow *>(&bento) : static_cast<MREditWindow *>(bento.paneWindowForLeaf(leafId));
		return window != nullptr ? window->getEditor() : nullptr;
	}

	static unsigned char lineKindAt(MRBentoBox &bento, MRBentoPaneRole role, std::size_t lineIndex) {
		std::vector<unsigned char> lineKinds;
		bento.fileCompareEditableLineKindsForRole(role, lineKinds, nullptr);
		return lineIndex < lineKinds.size() ? lineKinds[lineIndex] : mrfclkNone;
	}

	static bool markedDiffLineAt(MRBentoBox &bento, MRBentoPaneRole role, std::size_t lineIndex) {
		const unsigned char lineKind = lineKindAt(bento, role, lineIndex);
		return lineKind != mrfclkEqual && lineKind != mrfclkNone;
	}

	static int showContextAtDocumentLine(MRBentoBox &bento, MRBentoPaneRole role, std::size_t lineIndex) {
		const int leafId = bento.leafIdForRole(role);
		MREditWindow *targetWindow = leafId == 0 ? static_cast<MREditWindow *>(&bento) : static_cast<MREditWindow *>(bento.paneWindowForLeaf(leafId));
		MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
		if (leafId < 0 || targetEditor == nullptr) return -2;

		const TRect content = bento.contentBounds(bento.paneBoundsForLeaf(leafId));
		const TRect viewport = targetEditor->visibleTextViewportBounds();
		const int localY = viewport.a.y + static_cast<int>(lineIndex) - std::max(0, targetEditor->delta.y);
		const int localX = viewport.a.x;
		bento.showFileCompareActionList(TPoint(content.a.x + localX, content.a.y + localY), leafId);
		return bento.pendingFileCompareActionGroupIndex;
	}
};

namespace {

struct TestContext {
	int passed;
	int failed;

	TestContext() : passed(0), failed(0) {
	}
};

bool keymapMacroBindingDispatchProbeImpl(std::string &failureReason);
bool keymapAutoexecPersistenceAndBootstrapProbeImpl(std::string &failureReason);

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
	if (!setConfiguredAutoexecMacroEntries(snapshot.autoexecMacros, &errorText)) return false;
	if (!setConfiguredEditSetupSettings(snapshot.editSettings, &errorText)) return false;
	if (!setConfiguredEditExtensionProfiles(snapshot.editExtensionProfiles, &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, snapshot.colorSettings.windowColors.data(), snapshot.colorSettings.windowColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, snapshot.colorSettings.menuDialogColors.data(), snapshot.colorSettings.menuDialogColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Help, snapshot.colorSettings.helpColors.data(), snapshot.colorSettings.helpColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Other, snapshot.colorSettings.otherColors.data(), snapshot.colorSettings.otherColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MiniMap, snapshot.colorSettings.miniMapColors.data(), snapshot.colorSettings.miniMapColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::FileCompareMiniMap, snapshot.colorSettings.fileCompareMiniMapColors.data(), snapshot.colorSettings.fileCompareMiniMapColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::FileCompare, snapshot.colorSettings.fileCompareColors.data(), snapshot.colorSettings.fileCompareColors.size(), &errorText)) return false;
	if (!setConfiguredColorThemeFilePath(snapshot.colorThemeFilePath, &errorText)) return false;
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
	bool ownedTaskStillActive = false;

	owner.hasBuffer = true;
	owner.bufferId = 21;
	otherOwner.hasBuffer = true;
	otherOwner.bufferId = 22;

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
	failureReason.clear();
	return true;
}

bool testExecSessionStatusConsumerGuard(std::string &failureReason) {
	static constexpr std::uint64_t kTaskId = 700201;
	MRMacroExecutionOwner owner;
	MRMacroExecutionSession session;
	MRExecSessionStatusSnapshot snapshot;
	std::vector<std::string> lines;
	std::uint64_t generation = 0;
	bool sawHeader = false;
	bool sawSession = false;

	const MRMacroExecutionSessionListenerId listenerId = installExecSessionStatusConsumer();
	if (listenerId == 0 || installExecSessionStatusConsumer() != listenerId) {
		failureReason = "Exec-session status consumer must install idempotently.";
		return false;
	}

	owner.hasBuffer = true;
	owner.bufferId = 31;
	session = createMacroExecutionSession("exec-session-status-consumer", MRMacroExecutionRoute::Background, owner);
	session.taskId = kTaskId;
	trackMacroExecutionSession(session);
	generation = execSessionStatusConsumerGeneration();
	notifyMacroExecutionSessionChanged();
	if (execSessionStatusConsumerGeneration() != generation + 1) {
		publishMacroExecutionResultForTask(kTaskId, MRMacroExecutionState::Cancelled, "cleanup");
		failureReason = "Exec-session status consumer must observe session change notifications.";
		return false;
	}

	snapshot = execSessionStatusSnapshot();
	if (snapshot.activeCount == 0 || snapshot.generation != execSessionStatusConsumerGeneration()) {
		publishMacroExecutionResultForTask(kTaskId, MRMacroExecutionState::Cancelled, "cleanup");
		failureReason = "Exec-session status snapshot must report active count and consumer generation.";
		return false;
	}

	lines = execSessionStatusLines(0);
	for (const std::string &line : lines) {
		if (line.find("MRMac exec sessions: active=") != std::string::npos) sawHeader = true;
		if (line.find("exec-session-status-consumer") != std::string::npos && line.find("route=background") != std::string::npos) sawSession = true;
		if (line.find("breakpoint") != std::string::npos || line.find("debug") != std::string::npos) {
			publishMacroExecutionResultForTask(kTaskId, MRMacroExecutionState::Cancelled, "cleanup");
			failureReason = "Exec-session status consumer must not emit debugger vocabulary.";
			return false;
		}
	}
	if (!sawHeader || !sawSession) {
		publishMacroExecutionResultForTask(kTaskId, MRMacroExecutionState::Cancelled, "cleanup");
		failureReason = "Exec-session status consumer must format header and active sessions.";
		return false;
	}

	publishMacroExecutionResultForTask(kTaskId, MRMacroExecutionState::Cancelled, "cleanup");
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

	failureReason.clear();
	return true;
}

bool testExecSessionKvAccessBoundaryGuard(std::string &failureReason) {
	const std::filesystem::path rootPath = std::filesystem::current_path();
	static const char *kSourceRoots[] = {"app", "config", "coprocessor", "dialogs", "diff", "keymap", "mrmac", "piecetable", "ui"};
	static const char *kAllowedDirectKvFiles[] = {"mrmac/MRVM.cpp", "mrmac/macros/utils/ExecSessionConsole.mrmac"};
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
	    {"app/MRExecSessionStatus.cpp", nullptr, 0},
	    {"mrmac/MRMacroModelessUi.cpp", kModelessUiAllowed, sizeof(kModelessUiAllowed) / sizeof(kModelessUiAllowed[0])},
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
		std::string ioError;
		if (!readTextFile((rootPath / "mrmac/MRVM.cpp").string(), vmSource, ioError)) {
			failureReason = "Unable to read MRVM.cpp for modeless UI staging store guard: " + ioError;
			return false;
		}
		if (vmSource.find("g_macroUiDialog") != std::string::npos || vmSource.find("g_macroUiItemLists") != std::string::npos) {
			failureReason = "Modeless UI staging data must live under MODELESSUI/staging K/V, not in MRVM.cpp store globals.";
			return false;
		}
		if (vmSource.find("readGlobalValue(\"EXECSESSIONS\"") != std::string::npos || vmSource.find("readGlobalValue(\"MODELESSUI\"") != std::string::npos) {
			failureReason = "EXECSESSIONS and MODELESSUI root reads must go through MRVM.cpp K/V root accessors.";
			return false;
		}
		if (vmSource.find("ensureMacroUiStagingHash") == std::string::npos || vmSource.find("\"MODELESSUI\"") == std::string::npos) {
			failureReason = "Modeless UI staging guard expects MODELESSUI/staging K/V accessors in MRVM.cpp.";
			return false;
		}
		if (vmSource.find("findGlobalHashRoot") == std::string::npos || vmSource.find("findExecSessionsChild") == std::string::npos || vmSource.find("findModelessUiChild") == std::string::npos) {
			failureReason = "Exec-session/modeless K/V root accessors must remain centralized in MRVM.cpp.";
			return false;
		}
	}

	failureReason.clear();
	return true;
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

	if (keymapMacroBindingDispatchProbeImpl(failure)) return 0;
	if (!failure.empty()) std::cerr << failure << "\n";
	return 1;
}

int runKeymapAutoexecBootstrapProbeMode() {
	std::string failure;

	if (keymapAutoexecPersistenceAndBootstrapProbeImpl(failure)) return 0;
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
	if (configuredCursorBehaviour() != MRCursorBehaviour::FreeMovement) {
		failureReason = "Startup context should apply CURSOR_BEHAVIOUR='FREE_MOVEMENT'.";
		return false;
	}
	if (configuredScrollbarVisibility() != MRScrollbarVisibility::Always) {
		failureReason = "Startup context should apply SCROLLBAR_VISIBILITY='ALWAYS'.";
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

	if (colors.windowColors[0] != 0x10 || colors.windowColors[1] != 0x11 || colors.windowColors[2] != 0x12 || colors.windowColors[3] != 0x13 || colors.windowColors[4] != 0x14 || colors.windowColors[5] != 0x15 || colors.windowColors[6] != 0x16 || colors.windowColors[7] != 0x17 || colors.windowColors[8] != 0x1F || colors.windowColors[9] != 0x1F) {
		std::ostringstream out;

		out << "Startup context should apply WINDOWCOLORS list (including legacy migration):";
		for (std::size_t i = 0; i < colors.windowColors.size(); ++i)
			out << " " << i << "=0x" << std::hex << std::uppercase << static_cast<int>(colors.windowColors[i]);
		failureReason = out.str();
		return false;
	}
	if (colors.menuDialogColors[0] != 0x20 || colors.menuDialogColors[10] != 0x2A) {
		failureReason = "Startup context should apply MENUDIALOGCOLORS list.";
		return false;
	}
	if (colors.helpColors[0] != 0x30 || colors.helpColors[8] != 0x38) {
		failureReason = "Startup context should apply HELPCOLORS list.";
		return false;
	}
	if (colors.otherColors[0] != 0x40 || colors.otherColors[6] != 0x46) {
		failureReason = "Startup context should apply OTHERCOLORS list.";
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
	                           "MRSETUP('FILE_COMPARE_ORIGINAL_LEADING_GUTTERS', 'L');\n"
	                           "MRSETUP('FILE_COMPARE_ORIGINAL_TRAILING_GUTTERS', 'M');\n"
	                           "MRSETUP('FILE_COMPARE_COMPARE_LEADING_GUTTERS', 'LD');\n"
	                           "MRSETUP('FILE_COMPARE_COMPARE_TRAILING_GUTTERS', '');\n"
	                           "MRSETUP('FILE_COMPARE_START_CONFIGURATION', 'COMPARE_ORIGINAL');\n"
	                           "MRSETUP('FILE_COMPARE_COMPARE_PANEL_READ_ONLY', 'true');\n"
	                           "MRSETUP('BLOCK_MOVE', 'LEAVE_SPACE');\n"
	                           "MRSETUP('DEFAULT_MODE', 'OVERWRITE');\n"
	                           "WINDOWCOLORS('v1:10,11,12,13,14,15,16,17');\n"
	                           "MENUDIALOGCOLORS('v1:20,21,22,23,24,25,26,27,28,29,2A');\n"
	                           "HELPCOLORS('v1:30,31,32,33,34,35,36,37,38');\n"
	                           "OTHERCOLORS('v1:40,41,42,43,44,45,46');\n"
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

bool testMrsetupWindowColorThemeUriStartupLoad(std::string &failureReason) {
	const std::string themePath = "/tmp/mr-startup-window-colortheme-uri.mrmac";
	const std::string source = std::string("$MACRO Setup;\n") +
	                           "MRSETUP('SETTINGSPATH', '/tmp/mr_settings_theme_uri_probe.mrmac');\n" +
	                           "MRSETUP('WINDOW_COLORTHEME_URI', '" + themePath + "');\n" +
	                           "END_MACRO;\n";
	const std::string themeSource = "$MACRO MR_COLOR_THEME FROM EDIT;\n"
	                                "THEME_RESET();\n"
	                                "WINDOWCOLORS('v6:21,22,23,24,25,26,27,28,29,2A,2B,2C,2D');\n"
	                                "END_MACRO;\n";
	MRColorSetupSettings previousColors = configuredColorSetupSettings();
	std::string previousThemePath = configuredColorThemeFilePath();
	std::vector<unsigned char> bytecode;
	std::string macroName;
	std::string compileError;
	std::string restoreError;
	int entryOffset = -1;
	bool restored = true;

	auto restore = [&]() {
		if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, previousColors.windowColors.data(), previousColors.windowColors.size(), &restoreError)) restored = false;
		if (!setConfiguredColorThemeFilePath(previousThemePath, &restoreError)) restored = false;
		mrvmSetStartupSettingsMode(false);
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
		if (colors.windowColors[0] != 0x21 || colors.windowColors[8] != 0x29 || colors.windowColors[12] != 0x2D) {
			failureReason = "Startup WINDOW_COLORTHEME_URI should apply external theme colors.";
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

bool testSettingsDiscrepancyMigrationGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	const std::string root = "/tmp/mr_regression_settings_migration_" + std::to_string(static_cast<long>(::getpid()));
	const std::string settingsPath = root + "/cfg/settings.mrmac";
	const std::string legacyThemePath = root + "/cfg/legacy-theme.mrmac";
	const std::string legacySource = "$MACRO LegacySettings FROM EDIT;\n"
	                                 "MRSETUP('SETTINGSPATH', '/tmp/ignored-by-migration.mrmac');\n"
	                                 "MRSETUP('MACROPATH', '/tmp');\n"
	                                 "MRSETUP('HELPPATH', 'mr.hlp');\n"
	                                 "MRSETUP('TEMPDIR', '/tmp');\n"
	                                 "MRSETUP('SHELLPATH', '/bin/sh');\n"
	                                 "MRSETUP('TRUNCATE_SPACES', 'false');\n"
	                                 "MRSETUP('TAB_SIZE', '4');\n"
	                                 "MRSETUP('BACKUP_FILES', 'false');\n"
	                                 "MRSETUP('LINE_NUMBERS_POSITION', 'LEADING');\n"
	                                 "MRSETUP('LINE_NUM_ZERO_FILL', 'true');\n"
	                                 "MRSETUP('COLORTHEMEURI', '" +
	                                 legacyThemePath +
	                                 "');\n"
	                                 "MRSETUP('WINDOWCOLORS', 'v1:31,32,33,34,35,36,37,38');\n"
	                                 "MRSETUP('UNKNOWNKEY', 'ignored');\n"
	                                 "END_MACRO;\n";
	std::string content;
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	(void)::remove(settingsPath.c_str());
	(void)::remove(legacyThemePath.c_str());

	if (!mrMigrateSettingsMacroToCurrentVersionForTesting(settingsPath, legacySource, "regression-probe", &errorText)) {
		restore();
		failureReason = "Settings migration probe failed: " + errorText;
		return false;
	}
	if (!readTextFile(settingsPath, content, errorText)) {
		restore();
		failureReason = "Unable to read migrated settings.mrmac: " + errorText;
		return false;
	}
	if (content.find("MRSETUP('SETTINGSPATH', '" + settingsPath + "');") == std::string::npos) {
		restore();
		failureReason = "Migrated settings.mrmac must anchor SETTINGSPATH to the active file.";
		return false;
	}
	if (content.find("MRSETUP('LINE_NUMBERS_POSITION', 'LEADING');") == std::string::npos || content.find("MRSETUP('LINE_NUM_ZERO_FILL', 'true');") == std::string::npos || content.find("MRSETUP('TRUNCATE_SPACES', 'false');") == std::string::npos || content.find("MRSETUP('TAB_SIZE', '4');") == std::string::npos || content.find("MRSETUP('BACKUP_FILES', 'false');") == std::string::npos) {
		restore();
		failureReason = "Migrated settings.mrmac did not carry over recognized edit settings.";
		return false;
	}
	if (content.find("UNKNOWNKEY") != std::string::npos) {
		restore();
		failureReason = "Migrated settings.mrmac must not keep unknown legacy keys.";
		return false;
	}
	if (content.find("MRSETUP('PERSISTENT_BLOCKS', '") == std::string::npos || content.find("MRSETUP('DEFAULT_MODE', '") == std::string::npos) {
		restore();
		failureReason = "Migrated settings.mrmac must include normalized defaults for required keys.";
		return false;
	}
	if (!mrApplySettingsSourceForTesting(content, &errorText)) {
		restore();
		failureReason = "Migrated settings.mrmac should be loadable: " + errorText;
		return false;
	}
	{
		MREditSetupSettings edit = configuredEditSetupSettings();
		if (!edit.showLineNumbers || !edit.lineNumZeroFill || edit.truncateSpaces || edit.tabSize != 4 || edit.backupFiles) {
			restore();
			failureReason = "Applying migrated settings should restore carried edit-setting values.";
			return false;
		}
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after migration probe: " + restoreError;
		return false;
	}
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

bool testWindowColorGroupTargetsBlueWindowPalette(std::string &failureReason) {
	static const unsigned char probeValues[] = {0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E};
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::size_t itemCount = 0;
	const MRColorSetupItem *items = colorSetupGroupItems(MRColorSetupGroup::Window, itemCount);
	std::string errorText;
	unsigned char value = 0;
	bool restoreOk = true;

	auto restore = [&]() {
		if (!restoreOk) return;
		restoreOk = setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, previous.windowColors.data(), previous.windowColors.size(), &errorText);
	};

	if (items == nullptr || itemCount != sizeof(probeValues) / sizeof(probeValues[0])) {
		failureReason = "Unexpected WINDOWCOLORS item mapping.";
		return false;
	}

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, probeValues, sizeof(probeValues) / sizeof(probeValues[0]), &errorText)) {
		failureReason = "Unable to set WINDOWCOLORS probe values: " + errorText;
		return false;
	}

	for (std::size_t i = 0; i < itemCount; ++i) {
		unsigned char slot = items[i].paletteIndex;
		bool isExpectedSlot = (slot == 8 || slot == 9 || slot == 13 || slot == 14 || slot == kMrPaletteCurrentLine || slot == kMrPaletteCurrentLineInBlock || slot == kMrPaletteChangedText || slot == kMrPaletteLineNumbers || slot == kMrPaletteEofMarker || slot == kMrPaletteCodeFolding || slot == kMrPaletteCodeFoldingMarker || slot == kMrPaletteFormatRuler || slot == kMrPaletteFocusedPaneBorder || slot == kMrPaletteDiagnosticInformation);
		if (!configuredColorSlotOverride(items[i].paletteIndex, value)) {
			restore();
			failureReason = "WINDOWCOLORS item must override its mapped palette slot.";
			return false;
		}
		if (value != probeValues[i] || !isExpectedSlot) {
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
	static const unsigned char probeValues[] = {0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C};
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::size_t itemCount = 0;
	const MRColorSetupItem *items = colorSetupGroupItems(MRColorSetupGroup::MenuDialog, itemCount);
	std::string errorText;
	unsigned char value = 0;
	bool restoreOk = true;

	auto restore = [&]() {
		if (!restoreOk) return;
		restoreOk = setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, previous.menuDialogColors.data(), previous.menuDialogColors.size(), &errorText);
	};

	if (items == nullptr || itemCount != sizeof(probeValues) / sizeof(probeValues[0])) {
		failureReason = "Unexpected MENUDIALOGCOLORS item mapping.";
		return false;
	}
	if (std::string(items[kMenuDialogIndexMenuBarHotkey].label) != "Hotkeys on menu bar" || items[kMenuDialogIndexMenuBarHotkey].paletteIndex != kMrPaletteMenuBarHotkey) {
		failureReason = "MENUDIALOGCOLORS must expose a dedicated Hotkeys on menu bar item.";
		return false;
	}

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, probeValues, sizeof(probeValues) / sizeof(probeValues[0]), &errorText)) {
		failureReason = "Unable to set MENUDIALOGCOLORS probe values: " + errorText;
		return false;
	}

	for (std::size_t i = 0; i < itemCount; ++i) {
		unsigned char slot = items[i].paletteIndex;
		bool isMenuSlot = slot >= 2 && slot <= 6;
		bool isGrayDialogSlot = slot >= 32 && slot <= 63;
		bool isExtendedDialogSlot = slot == kMrPaletteDialogInactiveElements || slot == kMrPaletteDropListDescription || slot == kMrPaletteDropListSelectedInactive || slot == kMrPaletteMenuBarHotkey;
		if (!configuredColorSlotOverride(slot, value)) {
			restore();
			failureReason = "MENUDIALOGCOLORS item must override its mapped palette slot.";
			return false;
		}
		if (value != probeValues[i] || (!isMenuSlot && !isGrayDialogSlot && !isExtendedDialogSlot)) {
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
	MRColorSetupSettings defaults = resolveColorSetupDefaults();
	MRColorSetupSettings configured;
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	if (!applyConfiguredColorSetupValue("MENUDIALOGCOLORS", "v1:10,11,12,13,14,15,16,17,18,19,1A,1B,1C,1D", &errorText)) {
		restore();
		failureReason = "Unable to apply 14-entry legacy MENUDIALOGCOLORS list: " + errorText;
		return false;
	}
	configured = configuredColorSetupSettings();
	if (configured.menuDialogColors[kMenuDialogIndexInactiveControls] != defaults.menuDialogColors[kMenuDialogIndexInactiveControls] || configured.menuDialogColors[kMenuDialogIndexInactiveElements] != defaults.menuDialogColors[kMenuDialogIndexInactiveElements] || configured.menuDialogColors[kMenuDialogIndexDialogFrame] != 0x1C || configured.menuDialogColors[kMenuDialogIndexDialogText] != 0x1D || configured.menuDialogColors[kMenuDialogIndexDialogBackground] != 0x1C || configured.menuDialogColors[kMenuDialogIndexMenuBarHotkey] != defaults.menuDialogColors[kMenuDialogIndexMenuBarHotkey]) {
		restore();
		failureReason = "14-entry MENUDIALOGCOLORS upgrade must inject inactive-controls and menu-bar-hotkey defaults and map dialog background to legacy frame color.";
		return false;
	}

	if (!applyConfiguredColorSetupValue("MENUDIALOGCOLORS", "v1:20,21,22,23,24,25,26,27,28,29,2A", &errorText)) {
		restore();
		failureReason = "Unable to apply 11-entry legacy MENUDIALOGCOLORS list: " + errorText;
		return false;
	}
	configured = configuredColorSetupSettings();
	if (configured.menuDialogColors[kMenuDialogIndexListboxSelector] != defaults.menuDialogColors[kMenuDialogIndexListboxSelector] || configured.menuDialogColors[kMenuDialogIndexInactiveControls] != defaults.menuDialogColors[kMenuDialogIndexInactiveControls] || configured.menuDialogColors[kMenuDialogIndexInactiveElements] != defaults.menuDialogColors[kMenuDialogIndexInactiveElements] || configured.menuDialogColors[kMenuDialogIndexDialogFrame] != defaults.menuDialogColors[kMenuDialogIndexDialogFrame] || configured.menuDialogColors[kMenuDialogIndexDialogText] != defaults.menuDialogColors[kMenuDialogIndexDialogText] || configured.menuDialogColors[kMenuDialogIndexDialogBackground] != defaults.menuDialogColors[kMenuDialogIndexDialogBackground] || configured.menuDialogColors[kMenuDialogIndexMenuBarHotkey] != defaults.menuDialogColors[kMenuDialogIndexMenuBarHotkey]) {
		restore();
		failureReason = "11-entry MENUDIALOGCOLORS upgrade must fill missing selector/inactive/frame/text/background/menu-bar-hotkey defaults.";
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
	static const unsigned char probeValues[] = {0x71, 0x72, 0x7B, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7C, 0x7D, 0x7E, 0x7F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x2E};
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::string menuBarContent;
	std::string errorText;
	std::string ioError;
	unsigned char normalHotkey = 0;
	unsigned char selectedHotkey = 0;
	unsigned char menuBarHotkey = 0;
	bool restoreOk = true;

	auto restore = [&]() {
		if (!restoreOk) return;
		restoreOk = setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, previous.menuDialogColors.data(), previous.menuDialogColors.size(), &errorText);
	};

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, probeValues, sizeof(probeValues) / sizeof(probeValues[0]), &errorText)) {
		failureReason = "Unable to set MENUDIALOGCOLORS probe values: " + errorText;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRMenuBar.cpp"), menuBarContent, ioError)) {
		restore();
		failureReason = "Unable to read MRMenuBar.cpp for menu-bar hotkey guard: " + ioError;
		return false;
	}
	if (!configuredColorSlotOverride(4, normalHotkey)) {
		restore();
		failureReason = "Palette slot 4 (entry-hotkey) must be overrideable.";
		return false;
	}
	if (!configuredColorSlotOverride(7, selectedHotkey)) {
		restore();
		failureReason = "Palette slot 7 (selected entry-hotkey) must mirror entry-hotkey.";
		return false;
	}
	if (!configuredColorSlotOverride(kMrPaletteMenuBarHotkey, menuBarHotkey)) {
		restore();
		failureReason = "Menu bar hotkey slot must be overrideable.";
		return false;
	}
	if (normalHotkey != probeValues[2] || selectedHotkey != probeValues[2]) {
		restore();
		failureReason = "Entry-hotkey and selected entry-hotkey must resolve to the same configured color.";
		return false;
	}
	if (menuBarHotkey != probeValues[kMenuDialogIndexMenuBarHotkey] || menuBarHotkey == normalHotkey) {
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
	unsigned char value = 0;
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
	probe[kMenuDialogIndexInactiveControls] = 0x5B;
	probe[kMenuDialogIndexDialogFrame] = 0x4A;
	probe[kMenuDialogIndexDialogText] = 0x3C;
	probe[kMenuDialogIndexDialogBackground] = 0x2D;

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, probe.data(), probe.size(), &errorText)) {
		failureReason = "Unable to set MENUDIALOGCOLORS frame/background probe values: " + errorText;
		return false;
	}

	static const unsigned char frameSlots[] = {33, 34, 65, 66, 97, 98};
	for (unsigned char slot : frameSlots) {
		if (!configuredColorSlotOverride(slot, value)) {
			restore();
			failureReason = "Dialog frame slot override missing.";
			return false;
		}
		if (value != 0x4A) {
			restore();
			failureReason = "Dialog frame propagation mismatch.";
			return false;
		}
	}

	static const unsigned char textSlots[] = {37, 69, 101};
	for (unsigned char slot : textSlots) {
		if (!configuredColorSlotOverride(slot, value)) {
			restore();
			failureReason = "Dialog text slot override missing.";
			return false;
		}
		if (value != 0x3C) {
			restore();
			failureReason = "Dialog text propagation mismatch.";
			return false;
		}
	}

	static const unsigned char backgroundSlots[] = {32, 64, 96};
	for (unsigned char slot : backgroundSlots) {
		if (!configuredColorSlotOverride(slot, value)) {
			restore();
			failureReason = "Dialog background slot override missing.";
			return false;
		}
		if (value != 0x2D) {
			restore();
			failureReason = "Dialog background propagation mismatch.";
			return false;
		}
	}

	static const unsigned char inactiveControlSlots[] = {kPaletteDialogInactiveControlsGray, kPaletteDialogInactiveControlsBlue, kPaletteDialogInactiveControlsCyan};
	for (unsigned char slot : inactiveControlSlots) {
		if (!configuredColorSlotOverride(slot, value)) {
			restore();
			failureReason = "Dialog inactive-control slot override missing.";
			return false;
		}
		if (value != 0x5B) {
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
		failureReason = "WordStar keymap must load without error diagnostics.";
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

bool testWordStarBasicNavigationKeybindingsHarness(const std::string &wordstarKeymapContent, std::string &failureReason) {
	ScopedRegressionKeymap restoreKeymap;
	ScopedRegressionMacroDirectory macroDirectory(absolutePathFromCwd("mrmac/macros"));
	ScopedRegressionCursorBehaviour cursorBehaviour(MRCursorBehaviour::BoundToText);
	MREditWindow window(TRect(0, 0, 80, 16), "wordstar-basic-nav", 1032);
	MRFileEditor *editor = nullptr;

	if (!installRegressionKeymap(wordstarKeymapContent, failureReason)) return false;
	if (!window.replaceTextBuffer("abc\n", "wordstar-basic-nav")) {
		failureReason = "Unable to seed window editor for WordStar basic navigation path.";
		return false;
	}
	editor = window.getEditor();
	if (editor == nullptr) {
		failureReason = "WordStar basic navigation path must have an editor.";
		return false;
	}
	editor->setCursorOffset(0);
	if (!sendWindowRawCtrl(window, 'D')) return false;
	if (editor->cursorOffset() != 1) {
		failureReason = "WordStar Ctrl-D from wordstar.mrmac must move the cursor right through the window key path.";
		return false;
	}
	if (!sendWindowRawCtrl(window, 'S')) return false;
	if (editor->cursorOffset() != 0) {
		failureReason = "WordStar Ctrl-S from wordstar.mrmac must move the cursor left through the window key path.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testWordStarBlockKeybindingsHarness(const std::string &defaultKeymapContent, std::string &failureReason) {
	ScopedRegressionKeymap restoreKeymap;
	ScopedRegressionMacroDirectory macroDirectory(absolutePathFromCwd("mrmac/macros"));
	ScopedRegressionCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	ScopedRegressionPersistentBlocks persistentBlocks(true);
	MREditSetupSettings editSettings = configuredEditSetupSettings();

	editSettings.columnBlockMove = "DELETE_SPACE";
	ScopedRegressionEditSetupSettings editSetup(editSettings);

	if (!installRegressionKeymap(defaultKeymapContent, failureReason)) return false;
	{
		MREditWindow window(TRect(0, 0, 80, 16), "wordstar-block", 1010);
		if (!window.replaceTextBuffer("alpha\nbeta\ngamma\n", "wordstar-block")) {
			failureReason = "Unable to seed window editor for Ctrl-Y key path.";
			return false;
		}
		if (!sendWindowRawCtrl(window, 'Y')) return false;
		if (window.getEditor() == nullptr || window.getEditor()->snapshotText() != "beta\ngamma\n") {
			failureReason = "Ctrl-Y must delete the current line through the WordStar keymap.";
			return false;
		}
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "wordstar-stream-block", 1011);
		if (!window.replaceTextBuffer("alpha\nbeta\ngamma\n", "wordstar-block")) {
			failureReason = "Unable to seed window editor for Ctrl-K stream block path.";
			return false;
		}
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowRawCtrl(window, 'B')) return false;
		if (!sendWindowRawCtrl(window, 'D')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, true, 1, 1, 1, 1, "WordStar Ctrl-K Ctrl-B must not live-grow stream", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, false, 1, 1, 1, 2, "WordStar Ctrl-K Ctrl-B/Ctrl-K Ctrl-K stream", failureReason)) return false;
		if (!expectWindowBlockOverlay(window, MREditWindow::bmStream, "WordStar committed stream overlay", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'D')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, false, 1, 1, 1, 2, "WordStar persistent stream after cursor move", failureReason)) return false;
		if (!expectWindowBlockOverlay(window, MREditWindow::bmStream, "WordStar persistent stream overlay after cursor move", failureReason)) return false;
		if (!expectWindowCommittedBlockRefreshesOverlay(window, rawCtrlKey('D'), 0, MREditWindow::bmStream, "WordStar committed stream overlay refresh after keybinding cursor move", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowKey(window, static_cast<ushort>('H'))) return false;
		if (window.blockStatus() != MREditWindow::bmNone || window.hasBlock()) {
			failureReason = "WordStar Ctrl-K H must hide the visible block.";
			return false;
		}
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowRawCtrl(window, 'H')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, false, 1, 1, 1, 2, "WordStar Ctrl-K Ctrl-H show", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "wordstar-stream-block-plain", 1012);
		if (!window.replaceTextBuffer("alpha\nbeta\ngamma\n", "wordstar-block-plain")) {
			failureReason = "Unable to seed window editor for Ctrl-K B stream block path.";
			return false;
		}
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowKey(window, static_cast<ushort>('b'))) return false;
		if (!sendWindowRawCtrl(window, 'D')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, true, 1, 1, 1, 1, "WordStar Ctrl-K B must not live-grow stream", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowKey(window, static_cast<ushort>('k'))) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, false, 1, 1, 1, 2, "WordStar Ctrl-K B/Ctrl-K K stream", failureReason)) return false;
		if (!expectWindowBlockOverlay(window, MREditWindow::bmStream, "WordStar committed plain stream overlay", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "wordstar-stream-block-arrow", 1012);
		if (!window.replaceTextBuffer("alpha\nbeta\ngamma\n", "wordstar-block-arrow")) {
			failureReason = "Unable to seed window editor for Ctrl-K B arrow stream block path.";
			return false;
		}
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowKey(window, static_cast<ushort>('b'))) return false;
		if (!sendWindowKey(window, kbRight)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, true, 1, 1, 1, 2, "WordStar Ctrl-K B must remain marking after plain Right", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowKey(window, static_cast<ushort>('k'))) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, false, 1, 1, 1, 2, "WordStar Ctrl-K B/plain Right/Ctrl-K K stream", failureReason)) return false;
		if (!expectWindowBlockOverlay(window, MREditWindow::bmStream, "WordStar committed arrow stream overlay", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "wordstar-column-block", 1013);
		if (!window.replaceTextBuffer("alpha\n\nbeta\ngamma\n", "wordstar-column-block")) {
			failureReason = "Unable to seed window editor for Ctrl-K N column block path.";
			return false;
		}
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowRawCtrl(window, 'N')) return false;
		if (!sendWindowRawCtrl(window, 'D')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmColumn, true, 1, 1, 1, 1, "WordStar Ctrl-K Ctrl-N must not live-grow column right", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'X')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmColumn, true, 1, 1, 1, 1, "WordStar Ctrl-K Ctrl-N must not live-grow column down over empty line", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!sendWindowRawCtrl(window, 'K')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmColumn, false, 1, 2, 1, 2, "WordStar Ctrl-K Ctrl-N/Ctrl-K Ctrl-K column", failureReason)) return false;
		if (!expectWindowBlockOverlay(window, MREditWindow::bmColumn, "WordStar committed column overlay", failureReason)) return false;
		if (!sendWindowRawCtrl(window, 'D')) return false;
		if (!expectWindowBlock(window, MREditWindow::bmColumn, false, 1, 2, 1, 2, "WordStar persistent column after cursor move", failureReason)) return false;
		if (!expectWindowBlockOverlay(window, MREditWindow::bmColumn, "WordStar persistent column overlay after cursor move", failureReason)) return false;
		if (!expectWindowCommittedBlockRefreshesOverlay(window, rawCtrlKey('D'), 0, MREditWindow::bmColumn, "WordStar committed column overlay refresh after keybinding cursor move", failureReason)) return false;
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
		MREditWindow window(TRect(0, 0, 80, 16), "wordstar-free-cursor", 1012);
		if (!window.replaceTextBuffer("abc", "wordstar-free-cursor")) {
			failureReason = "Unable to seed window editor for WordStar free-cursor path.";
			return false;
		}
		MRFileEditor *editor = window.getEditor();
		if (editor == nullptr) {
			failureReason = "WordStar free-cursor path must have an editor.";
			return false;
		}
		const std::size_t lineEnd = editor->lineEndOffset(0);
		editor->setCursorOffsetAtVisualColumn(lineEnd, static_cast<int>(editor->columnOfOffset(lineEnd)));
		const int before = editor->displayedCursorColumn();
		const int cursorXBefore = editor->cursor.x;
		if (!sendWindowRawCtrl(window, 'D')) return false;
		if (editor->cursorOffset() != lineEnd || editor->displayedCursorColumn() != before + 1) {
			failureReason = "WordStar Ctrl-D must honor free cursor movement beyond EOL.";
			return false;
		}
		if (editor->cursor.x != cursorXBefore + 1) {
			failureReason = "WordStar Ctrl-D must advance the visible editor caret beyond EOL.";
			return false;
		}
		if (window.cursorColumnNumber() != static_cast<unsigned long>(before + 2)) {
			failureReason = "Window cursor column must report the free cursor column beyond EOL.";
			return false;
		}
		if (!sendWindowRawCtrl(window, 'S')) return false;
		if (editor->cursorOffset() != lineEnd || editor->displayedCursorColumn() != before) {
			failureReason = "WordStar Ctrl-S must step back through free cursor columns.";
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
	static const char text[] = "alpha\n\nbeta\nomega";

	{
		MREditWindow window(TRect(0, 0, 80, 16), "block-input", 1001);
		if (!window.replaceTextBuffer(text, "block-input")) {
			failureReason = "Unable to seed window editor for stream cursor path.";
			return false;
		}
		if (!sendWindowKey(window, kbRight, kbCtrlShift)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, true, 1, 1, 1, 2, "cursor Ctrl+Right stream", failureReason)) return false;
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
		if (!sendWindowKey(window, kbRight, kbAltShift)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmColumn, true, 1, 1, 1, 2, "cursor Alt+Right column", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "block-input", 1003);
		if (!window.replaceTextBuffer(text, "block-input")) {
			failureReason = "Unable to seed window editor for line cursor path.";
			return false;
		}
		if (!sendWindowKey(window, kbDown, kbCtrlShift | kbAltShift)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmLine, true, 1, 2, 1, 1, "cursor Ctrl+Alt+Down line", failureReason)) return false;
	}
	{
		MREditWindow window(TRect(0, 0, 80, 16), "block-input", 1007);
		if (!window.replaceTextBuffer(text, "block-input")) {
			failureReason = "Unable to seed window editor for terminal scan-code cursor path.";
			return false;
		}
		if (!sendWindowKey(window, kbCtrlRight, kbCtrlShift)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmStream, true, 1, 1, 1, 2, "terminal CtrlRight scan stream", failureReason)) return false;
		window.clearBlock();
		if (window.getEditor() == nullptr) {
			failureReason = "Terminal scan-code cursor path must have an editor.";
			return false;
		}
		window.getEditor()->setCursorOffset(0);
		if (!sendWindowKey(window, kbAltRight, kbAltShift)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmColumn, true, 1, 1, 1, 2, "terminal AltRight scan column", failureReason)) return false;
		window.clearBlock();
		window.getEditor()->setCursorOffset(0);
		if (!sendWindowKey(window, kbAltDown, kbCtrlShift | kbAltShift)) return false;
		if (!expectWindowBlock(window, MREditWindow::bmLine, true, 1, 2, 1, 1, "terminal CtrlAltDown scan line", failureReason)) return false;
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
		snapshot.dropExactLineStartIndex();
		restored.setText("temporary\n");
		restored.restoreFromSnapshot(snapshot);
		if (!pieceTableHarnessCheckDocument(restored, expected, "snapshot restore after dropped exact line index", failureReason)) return false;
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

bool testBlockMarkingHarness(std::string &failureReason) {
	std::string routerContent;
	std::string menuContent;
	std::string keymapContent;
	std::string defaultKeymapContent;
	std::string wordstarKeymapContent;
	std::string vmContent;
	std::string compilerContent;
	std::string windowContent;
	std::string editorContent;
	std::string blockOpsContent;
	std::string blockOpsSourceContent;
	std::string setupCommonContent;
	std::string appContent;
	std::string ioError;

	if (!readTextFile(absolutePathFromCwd("app/MRCommandRouter.cpp"), routerContent, ioError)) {
		failureReason = "Unable to read MRCommandRouter.cpp for block marking harness: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("app/MREditorApp.cpp"), appContent, ioError)) {
		failureReason = "Unable to read MREditorApp.cpp for block marking harness: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("app/MRMenuFactory.cpp"), menuContent, ioError)) {
		failureReason = "Unable to read MRMenuFactory.cpp for block marking harness: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("keymap/MRKeymapActionCatalog.cpp"), keymapContent, ioError)) {
		failureReason = "Unable to read MRKeymapActionCatalog.cpp for block marking harness: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("mrmac/macros/keymaps/MRDefaultKeymaps.mrmac"), defaultKeymapContent, ioError)) {
		failureReason = "Unable to read MRDefaultKeymaps.mrmac for block marking harness: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("mrmac/macros/keymaps/wordstar.mrmac"), wordstarKeymapContent, ioError)) {
		failureReason = "Unable to read wordstar.mrmac for block marking harness: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("mrmac/MRVM.cpp"), vmContent, ioError)) {
		failureReason = "Unable to read MRVM.cpp for block marking harness: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("mrmac/mrmac.c"), compilerContent, ioError)) {
		failureReason = "Unable to read mrmac.c for block marking harness: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MREditWindow.hpp"), windowContent, ioError)) {
		failureReason = "Unable to read MREditWindow.hpp for block marking harness: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRFileEditor/MRFileEditorEvents.cpp"), editorContent, ioError)) {
		failureReason = "Unable to read MRFileEditorEvents.cpp for block marking harness: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRFileEditor/MRFEBlockOps.hpp"), blockOpsContent, ioError)) {
		failureReason = "Unable to read MRFEBlockOps.hpp for block marking harness: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRFileEditor/MRFEBlockOps.cpp"), blockOpsSourceContent, ioError)) {
		failureReason = "Unable to read MRFEBlockOps.cpp for block marking harness: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("dialogs/setup/MRSetupCommon.cpp"), setupCommonContent, ioError)) {
		failureReason = "Unable to read MRSetupCommon.cpp for block marking harness: " + ioError;
		return false;
	}
	if (!mrfeBlockOpsRegressionHarness(failureReason)) return false;
	if (!testBlockMarkingWindowInputHarness(failureReason)) return false;
	if (!testWordStarBasicNavigationKeybindingsHarness(wordstarKeymapContent, failureReason)) return false;
	if (!testWordStarBlockKeybindingsHarness(defaultKeymapContent, failureReason)) return false;
	if (routerContent.find("win->beginLineBlock();") == std::string::npos || routerContent.find("win->beginColumnBlock();") == std::string::npos || routerContent.find("win->beginStreamBlock();") == std::string::npos || routerContent.find("cmMrBlockToggleVisibility") == std::string::npos) {
		failureReason = "Block marking commands must route to marking methods and visibility toggle.";
		return false;
	}
	if (menuContent.find("cmMrBlockMarkLines, kbF7") == std::string::npos || menuContent.find("cmMrBlockMarkColumns, kbShiftF7") == std::string::npos || menuContent.find("cmMrBlockMarkStream, kbCtrlF7") == std::string::npos || menuContent.find("~H~ide/show block mark") == std::string::npos || menuContent.find("TKey(kbF9, kbShift)") == std::string::npos) {
		failureReason = "Line, column, stream and visibility marking must be present in the Block menu with default hotkeys.";
		return false;
	}
	if (keymapContent.find("MRMAC_BLOCK_SET_BEGIN") == std::string::npos || keymapContent.find("MRMAC_BLOCK_SET_COLUMN_BEGIN") == std::string::npos || keymapContent.find("MRMAC_BLOCK_MARK_STREAM") == std::string::npos || keymapContent.find("MRMAC_BLOCK_SET_END") == std::string::npos || keymapContent.find("MRMAC_BLOCK_CLEAR") == std::string::npos || keymapContent.find("MRMAC_BLOCK_TOGGLE_VISIBILITY") == std::string::npos || keymapContent.find("MR_BLOCK_TOGGLE_VISIBILITY") == std::string::npos) {
		failureReason = "Line, column, stream, end, clear and visibility targets must be present in the keymap action catalog.";
		return false;
	}
	if (defaultKeymapContent.find("MRMAC_BLOCK_SET_BEGIN") == std::string::npos || defaultKeymapContent.find("MRMAC_BLOCK_SET_COLUMN_BEGIN") == std::string::npos || defaultKeymapContent.find("MRMAC_BLOCK_TOGGLE_VISIBILITY") == std::string::npos) {
		failureReason = "Default keymaps must expose line, column and visibility block marking targets.";
		return false;
	}
	if (wordstarKeymapContent.find("MRMAC_BLOCK_SET_COLUMN_BEGIN\" sequence=\"<Ctrl+K> <Ctrl+N>") == std::string::npos || wordstarKeymapContent.find("MRMAC_BLOCK_SET_COLUMN_BEGIN\" sequence=\"<Ctrl+K> <N>") == std::string::npos) {
		failureReason = "WordStar keymap must expose Ctrl-K Ctrl-N and Ctrl-K N column block begins.";
		return false;
	}
	if (vmContent.find("mrvmUiBlockBeginLine()") == std::string::npos || vmContent.find("mrvmUiBlockBeginColumn()") == std::string::npos || vmContent.find("mrvmUiBlockBeginStream()") == std::string::npos || vmContent.find("mrvmUiBlockEndMarking()") == std::string::npos || vmContent.find("mrvmUiBlockTurnMarkingOff()") == std::string::npos || vmContent.find("mrvmUiBlockToggleVisibility()") == std::string::npos || compilerContent.find("BLOCK_BEGIN") == std::string::npos || compilerContent.find("COL_BLOCK_BEGIN") == std::string::npos || compilerContent.find("STR_BLOCK_BEGIN") == std::string::npos || compilerContent.find("BLOCK_END") == std::string::npos || compilerContent.find("BLOCK_OFF") == std::string::npos || compilerContent.find("BLOCK_TOGGLE_VISIBILITY") == std::string::npos) {
		failureReason = "Line, column, stream, end, clear and visibility marking must be wired through MRMAC compiler/runtime surfaces.";
		return false;
	}
	if (routerContent.find("handleLoadBlockFromFile") == std::string::npos || routerContent.find("saveBlockToFile") == std::string::npos || blockOpsContent.find("loadBlockFromFile") == std::string::npos || blockOpsContent.find("saveBlockToFile") == std::string::npos || blockOpsContent.find("captureCurrentBlockPayload") == std::string::npos || blockOpsContent.find("insertPayloadAsStreamBlock") == std::string::npos || blockOpsContent.find("std::vector<char> release() noexcept") == std::string::npos || blockOpsContent.find("prepareTransferMessage") == std::string::npos || vmContent.find("name == \"LOAD_BLOCK\"") == std::string::npos || vmContent.find("mrvmEditorLoadBlockFromFile") == std::string::npos || vmContent.find("mrvmEditorSaveCurrentBlockToFile") == std::string::npos || compilerContent.find("PROC_SIG1(\"LOAD_BLOCK\"") == std::string::npos || compilerContent.find("PROC_SIG1(\"SAVE_BLOCK\"") == std::string::npos) {
		failureReason = "Stream-only load/save block must be wired through menu, keymap and MRMAC surfaces.";
		return false;
	}
	if (routerContent.find("case cmMrBlockCopy:") == std::string::npos || routerContent.find("return handleCopyBlock(currentEditorCommandWindow());") == std::string::npos || routerContent.find("case cmMrBlockMove:") == std::string::npos || routerContent.find("return handleMoveBlock(currentEditorCommandWindow());") == std::string::npos || routerContent.find("case cmMrBlockDelete:") == std::string::npos || routerContent.find("return handleDeleteBlock(currentEditorCommandWindow());") == std::string::npos || routerContent.find("dispatchTargetedKeymapAppCommand") == std::string::npos || blockOpsContent.find("runBlockOperation") == std::string::npos || blockOpsContent.find("runWindowBlockOperation") == std::string::npos || blockOpsSourceContent.find("\"delete-block\"") == std::string::npos) {
		failureReason = "Block copy/move/delete must route to MRFEBlockOps through the active editor command target.";
		return false;
	}
	if (appContent.find("event.message.command == cmMrEditUndo || event.message.command == cmMrEditRedo") == std::string::npos || appContent.find("handleMRCommand(event.message.command, event.message.infoPtr)") == std::string::npos || appContent.find("TApplication::handleEvent(event);") == std::string::npos || appContent.find("event.message.command == cmMrEditUndo || event.message.command == cmMrEditRedo") > appContent.find("TApplication::handleEvent(event);")) {
		failureReason = "Menu Undo/Redo must route through MRCommandRouter before generic TVision command dispatch.";
		return false;
	}
	if (routerContent.find("message(editor, evCommand, editorCommand, nullptr);") == std::string::npos || routerContent.find("if (win->hasBlock() && !win->isBlockMarking()) win->refreshBlockVisual();") == std::string::npos) {
		failureReason = "Menu/App editor commands must refresh committed block overlays after dispatch.";
		return false;
	}
	if (setupCommonContent.find("case MRDialogHistoryScope::BlockSave:") == std::string::npos || routerContent.find("rememberLoadDialogPath(MRDialogHistoryScope::BlockSave, savePath.c_str());") == std::string::npos) {
		failureReason = "Block save history must be deferred by the file dialog and remembered only after a successful save.";
		return false;
	}
		if (windowContent.find("mBlockOps.adoptMouseSelection(*editor, editor->lastMouseSelectionModifiers())") == std::string::npos || windowContent.find("mBlockOps.updateFromEditor(*editor)") == std::string::npos) {
			failureReason = "Mouse button selection and active marking updates must be wired in MREditWindow.";
			return false;
		}
		if (routerContent.find("window->updateBlockFromEditor()") != std::string::npos || windowContent.find("if (editor != nullptr && mBlockOps.isMarking()) static_cast<void>(mBlockOps.updateFromEditor(*editor));") != std::string::npos) {
			failureReason = "MRMAC keymap editor commands must not live-update active block marking.";
			return false;
		}
		if (windowContent.find("handleShiftCursorBlockMarking(event)") == std::string::npos || windowContent.find("normalizedBlockCursorNavigationKey") == std::string::npos || windowContent.find("isBlockCursorMarkingModifier") == std::string::npos || windowContent.find("MRFEBlockMode::Column") == std::string::npos || windowContent.find("MRFEBlockMode::Line") == std::string::npos || windowContent.find("MRFEBlockMode::Stream") == std::string::npos) {
		failureReason = "Cursor block marking must route Ctrl, Alt and Ctrl-Alt navigation through stream, column and line modes and normalize terminal scan codes.";
		return false;
	}
	if (editorContent.find("updateLiveMouseBlockOverlay") == std::string::npos || editorContent.find("setBlockOverlayState(liveBlockMode") == std::string::npos) {
		failureReason = "Mouse block marking must update the overlay while dragging, before mouse release.";
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

bool testSetupScrollRefreshGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	const std::string root = "/tmp/mr_regression_edit_roundtrip_" + std::to_string(static_cast<long>(::getpid()));
	const std::string settingsPath = root + "/cfg/settings.mrmac";
	MREditSetupSettings probe = resolveEditSetupDefaults();
	MREditSetupSettings loaded;
	MRSetupPaths paths = resolveSetupPathDefaults();
	std::string source;
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	probe.pageBreak = "\\f";
	probe.wordDelimiters = "._:-";
	probe.defaultExtensions = "txt;md";
	probe.truncateSpaces = false;
	probe.eofCtrlZ = true;
	probe.eofCrLf = true;
	probe.tabExpand = false;
	probe.displayTabs = true;
	probe.tabSize = 3;
	probe.backupFiles = false;
	probe.backupMethod = "OFF";
	probe.showLineNumbers = true;
	probe.lineNumbersPosition = "LEADING";
	probe.lineNumZeroFill = true;
	probe.persistentBlocks = false;
	probe.columnBlockMove = "LEAVE_SPACE";
	probe.defaultMode = "OVERWRITE";

	if (!setConfiguredEditSetupSettings(probe, &errorText)) {
		restore();
		failureReason = "Unable to seed edit-settings roundtrip probe: " + errorText;
		return false;
	}

	paths.settingsMacroUri = settingsPath;
	paths.macroPath = "/tmp";
	paths.helpUri = "mr.hlp";
	paths.tempPath = "/tmp";
	paths.shellUri = "/bin/sh";
	source = buildSettingsMacroSource(paths);
	if (source.find("MRSETUP('DISPLAY_TABS', 'true');") == std::string::npos || source.find("MRSETUP('TAB_SIZE', '") == std::string::npos || source.find("MRSETUP('LINE_NUMBERS_POSITION', '") == std::string::npos) {
		restore();
		failureReason = "Edit-settings roundtrip source did not use canonical edit-setting keys.";
		return false;
	}
	if (source.find("MRSETUP('TABSIZE', '") != std::string::npos || source.find("MRSETUP('SHOW_LINE_NUMBERS', '") != std::string::npos || source.find("MRSETUP('SHOWLINENUMBERS', '") != std::string::npos || source.find("MRFEPROFILE('SET', 'perl_profile', 'TABSIZE', '3');") != std::string::npos) {
		restore();
		failureReason = "Profile roundtrip source still emitted deprecated edit-setting keys.";
		return false;
	}

	if (!setConfiguredEditSetupSettings(resolveEditSetupDefaults(), &errorText)) {
		restore();
		failureReason = "Unable to reset edit settings before roundtrip apply: " + errorText;
		return false;
	}
	if (!mrApplySettingsSourceForTesting(source, &errorText)) {
		restore();
		failureReason = "Unable to apply settings macro source in edit roundtrip probe: " + errorText;
		return false;
	}

	loaded = configuredEditSetupSettings();
	if (loaded.wordDelimiters != probe.wordDelimiters) {
		restore();
		failureReason = "Word delimiters mismatch after roundtrip.";
		return false;
	}
	if (loaded.defaultExtensions != "txt;md") {
		restore();
		failureReason = "Default extensions mismatch after roundtrip.";
		return false;
	}
	if (loaded.truncateSpaces != probe.truncateSpaces) {
		restore();
		failureReason = "Truncate-spaces mismatch after roundtrip.";
		return false;
	}
	if (loaded.eofCtrlZ != probe.eofCtrlZ) {
		restore();
		failureReason = "EOF_CTRL_Z mismatch after roundtrip.";
		return false;
	}
	if (loaded.eofCrLf != probe.eofCrLf) {
		restore();
		failureReason = "EOF_CR_LF mismatch after roundtrip.";
		return false;
	}
	if (loaded.tabExpand != probe.tabExpand) {
		restore();
		failureReason = "TAB_EXPAND mismatch after roundtrip.";
		return false;
	}
	if (loaded.tabSize != probe.tabSize) {
		restore();
		failureReason = "TAB_SIZE mismatch after roundtrip.";
		return false;
	}
	if (loaded.displayTabs != probe.displayTabs) {
		restore();
		failureReason = "DISPLAY_TABS mismatch after roundtrip.";
		return false;
	}
	if (loaded.backupFiles != probe.backupFiles) {
		restore();
		failureReason = "BACKUP_FILES mismatch after roundtrip.";
		return false;
	}
	if (loaded.lineNumZeroFill != probe.lineNumZeroFill) {
		restore();
		failureReason = "LINE_NUM_ZERO_FILL mismatch after roundtrip.";
		return false;
	}
	if (loaded.persistentBlocks != probe.persistentBlocks) {
		restore();
		failureReason = "PERSISTENT_BLOCKS mismatch after roundtrip.";
		return false;
	}
	if (loaded.columnBlockMove != probe.columnBlockMove) {
		restore();
		failureReason = "BLOCK_MOVE mismatch after roundtrip.";
		return false;
	}
	if (loaded.defaultMode != probe.defaultMode) {
		restore();
		failureReason = "DEFAULT_MODE mismatch after roundtrip.";
		return false;
	}
	if (loaded.lineNumbersPosition != "LEADING" || !loaded.showLineNumbers) {
		restore();
		failureReason = "Line-number position/show flag mismatch after roundtrip.";
		return false;
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after edit roundtrip probe: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testExtendedSettingsRoundtripGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	const std::string root = "/tmp/mr_regression_extended_settings_" + std::to_string(static_cast<long>(::getpid()));
	const std::string settingsPath = root + "/cfg/settings.mrmac";
	MREditSetupSettings probe = resolveEditSetupDefaults();
	MRSetupPaths paths = resolveSetupPathDefaults();
	std::string source;
	std::string errorText;
	std::string restoreError;
	bool restored = false;
	MREditSetupSettings loaded;
	MREditSetupSettings normalized;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	probe.rightMargin = 91;
	probe.leftMargin = 3;
	probe.formatRuler = true;
	probe.wordWrap = false;
	probe.indentStyle = "smart";
	probe.fileType = "binary";
	probe.binaryRecordLength = 123;
	probe.postLoadMacro = root + "/hooks/post-load.mrmac";
	probe.preSaveMacro = root + "/hooks/pre-save.mrmac";
	probe.defaultPath = root + "/workspace";
	probe.formatLine = std::string(90, '.') + "R";
	probe.cursorStatusColor = "7f";

	if (!setConfiguredEditSetupSettings(probe, &errorText)) {
		restore();
		failureReason = "Unable to seed extended settings probe: " + errorText;
		return false;
	}
	normalized = configuredEditSetupSettings();

	paths.settingsMacroUri = settingsPath;
	paths.macroPath = "/tmp";
	paths.helpUri = "mr.hlp";
	paths.tempPath = "/tmp";
	paths.shellUri = "/bin/sh";
	source = buildSettingsMacroSource(paths);
	const std::string expectedFormatLineSetting = "MRSETUP('FORMAT_LINE', '" + normalized.formatLine + "');";
	if (source.find("MRSETUP('LEFT_MARGIN', '3');") == std::string::npos || source.find("MRSETUP('RIGHT_MARGIN', '91');") == std::string::npos || source.find("MRSETUP('FORMAT_RULER', 'true');") == std::string::npos || source.find("MRSETUP('WORD_WRAP', 'false');") == std::string::npos || source.find("MRSETUP('INDENT_STYLE', 'SMART');") == std::string::npos || source.find("MRSETUP('FILE_TYPE', 'BINARY');") == std::string::npos || source.find("MRSETUP('BINARY_RECORD_LENGTH', '123');") == std::string::npos || source.find("MRSETUP('POST_LOAD_MACRO', '") == std::string::npos || source.find("MRSETUP('PRE_SAVE_MACRO', '") == std::string::npos || source.find("MRSETUP('DEFAULT_PATH', '") == std::string::npos || source.find(expectedFormatLineSetting) == std::string::npos || source.find("MRSETUP('CURSOR_STATUS_COLOR', '7F');") == std::string::npos) {
		restore();
		failureReason = "Extended settings serializer did not emit the expected canonical keys.";
		return false;
	}

	if (!setConfiguredEditSetupSettings(resolveEditSetupDefaults(), &errorText)) {
		restore();
		failureReason = "Unable to reset extended settings probe before reload: " + errorText;
		return false;
	}
	if (!mrApplySettingsSourceForTesting(source, &errorText)) {
		restore();
		failureReason = "Unable to reload extended settings probe source: " + errorText;
		return false;
	}

	loaded = configuredEditSetupSettings();
	if (loaded.leftMargin != 3 || loaded.rightMargin != 91 || !loaded.formatRuler || loaded.wordWrap || loaded.indentStyle != "SMART" || loaded.fileType != "BINARY" || loaded.binaryRecordLength != 123 || loaded.postLoadMacro != normalizeConfiguredPathInput(probe.postLoadMacro) || loaded.preSaveMacro != normalizeConfiguredPathInput(probe.preSaveMacro) || loaded.defaultPath != normalizeConfiguredPathInput(probe.defaultPath) || loaded.formatLine != normalized.formatLine || loaded.cursorStatusColor != "7F") {
		restore();
		failureReason = "Extended settings roundtrip lost one or more serialized edit settings.";
		return false;
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after extended settings probe: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}

bool keymapMacroBindingDispatchProbeImpl(std::string &failureReason) {
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

bool keymapAutoexecPersistenceAndBootstrapProbeImpl(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	ScopedRegressionKeymap restoreKeymap;
	const std::string root = "/tmp/mr_regression_keymap_autoexec_bootstrap_" + std::to_string(static_cast<long>(::getpid()));
	const std::string settingsPath = root + "/cfg/settings.mrmac";
	const std::string macroPath = root + "/macros";
	const std::string tempPath = root + "/tmp";
	const std::string actionMacroFilePath = macroPath + "/actions/bootstrap-marker.mrmac";
	const std::string keymapMacroFilePath = macroPath + "/keymaps/bootstrap-keymap.mrmac";
	const std::string retainedAutoexecEntry = "keymaps/bootstrap-keymap.mrmac";
	const std::string missingAutoexecEntry = "keymaps/missing-entry.mrmac";
	const std::string macroTarget = "actions/bootstrap-marker.mrmac^insert_bootstrap_marker";
	MRSettingsSnapshot cleanSettings;
	MRKeymapProfile profile;
	MRKeymapBindingRecord binding;
	std::vector<std::string> configuredEntries;
	std::string persistedSource;
	std::string cleanSource;
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	if (!resetSettingsSnapshot(settingsPath, cleanSettings, &errorText)) {
		failureReason = "Unable to reset clean settings snapshot for AUTOEXEC bootstrap harness: " + errorText;
		return false;
	}
	cleanSettings.paths.settingsMacroUri = settingsPath;
	cleanSettings.paths.macroPath = macroPath;
	cleanSettings.paths.helpUri = absolutePathFromCwd("mr.hlp");
	cleanSettings.paths.tempPath = tempPath;
	cleanSettings.paths.shellUri = "/bin/sh";
	cleanSource = buildSettingsMacroSource(cleanSettings);

	if (!ensureDirectoryTree(root + "/cfg", &errorText) || !ensureDirectoryTree(macroPath + "/actions", &errorText) || !ensureDirectoryTree(macroPath + "/keymaps", &errorText) || !ensureDirectoryTree(tempPath, &errorText)) {
		failureReason = "Unable to create AUTOEXEC bootstrap harness directories: " + errorText;
		return false;
	}
	if (!writeTextFile(actionMacroFilePath, "$MACRO insert_bootstrap_marker;\nTEXT('#');\nEND_MACRO;\n")) {
		restore();
		failureReason = "Unable to write action macro for AUTOEXEC bootstrap harness.";
		return false;
	}

	profile.name = "AUTOEXEC_BOOTSTRAP";
	profile.description = "Regression autoexec bootstrap";
	binding.profileName = profile.name;
	binding.context = MRKeymapContext::Edit;
	binding.target.type = MRKeymapBindingType::Macro;
	binding.target.target = macroTarget;
	binding.sequence = *MRKeymapSequence::parse("<F11>");
	binding.description = "Bootstrap marker";
	profile.bindings.push_back(binding);
	if (!writeTextFile(keymapMacroFilePath, buildExecutableKeymapMacroSource(std::vector<MRKeymapProfile>{profile}, profile.name))) {
		restore();
		failureReason = "Unable to write keymap AUTOEXEC macro file.";
		return false;
	}
	if (!mrApplySettingsSourceForTesting(cleanSource, &errorText)) {
		restore();
		failureReason = "Unable to apply clean settings before AUTOEXEC persistence probe: " + errorText;
		return false;
	}
	if (!setConfiguredSettingsMacroFilePath(settingsPath, &errorText)) {
		restore();
		failureReason = "Unable to configure settings path before AUTOEXEC persistence probe: " + errorText;
		return false;
	}
	if (!setConfiguredAutoexecMacroEntries(std::vector<std::string>{retainedAutoexecEntry, missingAutoexecEntry}, &errorText)) {
		restore();
		failureReason = "Unable to configure AUTOEXEC entries for persistence probe: " + errorText;
		return false;
	}
	if (!persistConfiguredSettingsSnapshot(&errorText)) {
		restore();
		failureReason = "Unable to persist settings with AUTOEXEC entries: " + errorText;
		return false;
	}
	if (!mrApplySettingsSourceForTesting(cleanSource, &errorText)) {
		restore();
		failureReason = "Unable to reset runtime state before AUTOEXEC bootstrap probe: " + errorText;
		return false;
	}
	if (!setConfiguredSettingsMacroFilePath(settingsPath, &errorText)) {
		restore();
		failureReason = "Unable to restore settings path before AUTOEXEC bootstrap probe: " + errorText;
		return false;
	}
	if (!setConfiguredKeymapProfiles({}, &errorText) || !setConfiguredActiveKeymapProfile(std::string(), &errorText)) {
		restore();
		failureReason = "Unable to clear runtime keymap before AUTOEXEC bootstrap probe: " + errorText;
		return false;
	}

	if (ensureRegressionEditorApp(failureReason) == nullptr) {
		restore();
		return false;
	}
	{
		MREditWindow *window = nullptr;
		MRFileEditor *editor = nullptr;

		configuredAutoexecMacroEntries(configuredEntries);
		if (configuredEntries.size() != 1 || configuredEntries.front() != retainedAutoexecEntry) {
			restore();
			failureReason = "AUTOEXEC bootstrap must retain the existing keymap macro and drop missing entries.";
			return false;
		}
		if (!readTextFile(settingsPath, persistedSource, errorText)) {
			restore();
			failureReason = "Unable to read persisted settings after AUTOEXEC bootstrap: " + errorText;
			return false;
		}
		if (persistedSource.find("MRSETUP('AUTOEXEC_MACRO', '" + missingAutoexecEntry + "');") != std::string::npos) {
			restore();
			failureReason = "AUTOEXEC bootstrap must persist the filtered settings.mrmac without stale missing entries.";
			return false;
		}
		if (persistedSource.find("MRSETUP('AUTOEXEC_MACRO', '" + retainedAutoexecEntry + "');") == std::string::npos) {
			restore();
			failureReason = "AUTOEXEC bootstrap must preserve the surviving keymap macro entry in settings.mrmac.";
			return false;
		}
		window = createEditorWindow("keymap-autoexec-bootstrap");
		if (window == nullptr) {
			restore();
			failureReason = "AUTOEXEC bootstrap harness could not create an editor window.";
			return false;
		}
		if (!mrActivateEditWindow(window)) {
			destroyRegressionWindow(window);
			restore();
			failureReason = "AUTOEXEC bootstrap harness could not activate the editor window.";
			return false;
		}
		if (!window->replaceTextBuffer("abc\n", "keymap-autoexec-bootstrap")) {
			destroyRegressionWindow(window);
			restore();
			failureReason = "AUTOEXEC bootstrap harness could not seed editor text.";
			return false;
		}
		editor = window->getEditor();
		if (editor == nullptr) {
			destroyRegressionWindow(window);
			restore();
			failureReason = "AUTOEXEC bootstrap harness window has no editor.";
			return false;
		}
		editor->setCursorOffset(0);
		if (!sendWindowKey(*window, kbF11)) {
			destroyRegressionWindow(window);
			restore();
			failureReason = "AUTOEXEC bootstrap harness could not send the bound key.";
			return false;
		}
		if (editor->snapshotText() != "#abc\n") {
			destroyRegressionWindow(window);
			restore();
			failureReason = "AUTOEXEC bootstrap must restore macro key bindings after restart.";
			return false;
		}
		destroyRegressionWindow(window);
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after AUTOEXEC bootstrap harness: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testKeymapMacroBindingDispatchHarness(std::string &failureReason) {
	return runRegressionProbeProcess("keymap-macro-dispatch", failureReason);
}

bool testKeymapAutoexecPersistenceAndBootstrapHarness(std::string &failureReason) {
	return runRegressionProbeProcess("keymap-autoexec-bootstrap", failureReason);
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

bool testEditProfileRoundtripGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	MREditSetupSettings globalSettings = resolveEditSetupDefaults();
	MREditExtensionProfile profile;
	MRSetupPaths paths = resolveSetupPathDefaults();
	std::string source;
	std::string errorText;
	std::string restoreError;
	bool restored = false;
	std::string matchedProfile;
	MREditSetupSettings effective;
	MREditSetupSettings fallback;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	globalSettings.tabSize = 8;
	globalSettings.showLineNumbers = false;
	globalSettings.lineNumbersPosition = "OFF";
	globalSettings.defaultMode = "INSERT";
	if (!setConfiguredEditSetupSettings(globalSettings, &errorText)) {
		restore();
		failureReason = "Unable to seed global edit settings for profile roundtrip probe: " + errorText;
		return false;
	}

	profile.id = "perl_profile";
	profile.name = "Perl";
	profile.extensions.push_back("pl");
	profile.extensions.push_back("pm");
	profile.overrides.values = resolveEditSetupDefaults();
	profile.overrides.values.tabSize = 3;
	profile.overrides.values.lineNumbersPosition = "LEADING";
	profile.overrides.values.showLineNumbers = true;
	profile.overrides.values.defaultMode = "overwrite";
	profile.overrides.values.backupFiles = false;
	profile.overrides.values.codeLanguage = "PERL";
	profile.overrides.values.codeColoring = true;
	profile.overrides.values.codeFoldingFeature = true;
	profile.overrides.mask = kOvTabSize | kOvLineNumbersPosition | kOvDefaultMode | kOvBackupFiles | kOvCodeLanguage | kOvCodeColoring | kOvCodeFoldingFeature;
	if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(1, profile), &errorText)) {
		restore();
		failureReason = "Unable to seed extension profile roundtrip probe: " + errorText;
		return false;
	}

	paths.settingsMacroUri = snapshot.settingsMacroFilePath;
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	source = buildSettingsMacroSource(paths);
	if (source.find("MRFEPROFILE('SET', 'perl_profile', 'BACKUP_FILES', 'false');") == std::string::npos || source.find("MRFEPROFILE('SET', 'perl_profile', 'CODE_LANGUAGE', 'PERL');") == std::string::npos ||
	    source.find("MRFEPROFILE('SET', 'perl_profile', 'CODE_COLORING', 'true');") == std::string::npos || source.find("MRFEPROFILE('SET', 'perl_profile', 'CODE_FOLDING', 'true');") == std::string::npos) {
		restore();
		failureReason = "Profile roundtrip source did not serialize profile override literals.";
		return false;
	}

	if (!setConfiguredEditSetupSettings(resolveEditSetupDefaults(), &errorText)) {
		restore();
		failureReason = "Unable to reset global edit settings before profile roundtrip apply: " + errorText;
		return false;
	}
	if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(), &errorText)) {
		restore();
		failureReason = "Unable to clear extension profiles before profile roundtrip apply: " + errorText;
		return false;
	}
	if (!mrApplySettingsSourceForTesting(source, &errorText)) {
		restore();
		failureReason = "Unable to apply settings macro source in profile roundtrip probe: " + errorText;
		return false;
	}

	if (configuredEditExtensionProfiles().size() != 1) {
		restore();
		failureReason = "Profile roundtrip did not restore exactly one extension profile.";
		return false;
	}
	if (configuredEditExtensionProfiles()[0].id != "perl_profile") {
		restore();
		failureReason = "Profile roundtrip did not preserve the profile id.";
		return false;
	}
	if (configuredEditExtensionProfiles()[0].name != "Perl") {
		restore();
		failureReason = "Profile roundtrip did not preserve the profile name.";
		return false;
	}
	if (configuredEditExtensionProfiles()[0].extensions.size() != 2 || configuredEditExtensionProfiles()[0].extensions[0] != "pl" || configuredEditExtensionProfiles()[0].extensions[1] != "pm") {
		restore();
		failureReason = "Profile roundtrip did not preserve the extension selector list.";
		return false;
	}

	if (!effectiveEditSetupSettingsForPath("/tmp/example.pl", effective, &matchedProfile)) {
		restore();
		failureReason = "Effective profile lookup failed for matching file.";
		return false;
	}
	if (matchedProfile != "Perl") {
		restore();
		failureReason = "Effective profile lookup did not report the matching profile name.";
		return false;
	}
	if (effective.tabSize != 3 || !effective.showLineNumbers || effective.defaultMode != "OVERWRITE" || effective.backupFiles || effective.codeLanguage != "PERL" || !effective.codeColoring || !effective.codeFoldingFeature) {
		restore();
		failureReason = "Effective edit settings did not merge profile overrides onto globals.";
		return false;
	}

	if (!effectiveEditSetupSettingsForPath("/tmp/example.txt", fallback, &matchedProfile)) {
		restore();
		failureReason = "Effective profile lookup failed for non-matching file.";
		return false;
	}
	if (!matchedProfile.empty()) {
		restore();
		failureReason = "Non-matching file unexpectedly reported an edit profile match.";
		return false;
	}
	if (fallback.tabSize != globalSettings.tabSize || fallback.showLineNumbers != globalSettings.showLineNumbers || fallback.defaultMode != globalSettings.defaultMode) {
		restore();
		failureReason = "Non-matching file did not fall back to the global edit settings.";
		return false;
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after profile roundtrip probe: " + restoreError;
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

bool testLegacyEditProfileMacroDropToDefaultsGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	const std::string currentVersion = mrCurrentPersistenceVersionString();
	std::string source = "$MACRO MR_SETTINGS FROM EDIT;\n"
	                     "MRSETUP('SETTINGS_VERSION', '" + currentVersion + "');\n"
	                     "MRSETUP('TAB_SIZE', '8');\n"
	                     "MREDITPROFILE('DEFINE', 'legacy_cpp', 'Legacy C++', '');\n"
	                     "MREDITPROFILE('EXT', 'legacy_cpp', 'cpp', '');\n"
	                     "MREDITPROFILE('SET', 'legacy_cpp', 'TAB_SIZE', '5');\n"
	                     "END_MACRO;\n";
	std::string errorText;
	std::string restoreError;
	bool restored = false;
	MREditSetupSettings effective;
	std::string matchedProfile;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	if (!mrApplySettingsSourceForTesting(source, &errorText)) {
		restore();
		failureReason = "Legacy MREDITPROFILE source should be dropped to defaults, but apply failed: " + errorText;
		return false;
	}
	if (!effectiveEditSetupSettingsForPath("/tmp/example.cpp", effective, &matchedProfile)) {
		restore();
		failureReason = "Effective settings lookup failed after dropping legacy MREDITPROFILE directives.";
		return false;
	}
	if (!matchedProfile.empty()) {
		restore();
		failureReason = "Legacy MREDITPROFILE directives should not survive as FE profiles.";
		return false;
	}
	if (effective.tabSize != 8) {
		restore();
		failureReason = "Legacy MREDITPROFILE directives should fall back to global defaults/settings.";
		return false;
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after legacy token drop probe: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testEditProfileCaseSensitiveMacroRoundtripGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	MRSetupPaths paths = resolveSetupPathDefaults();
	const std::string currentVersion = mrCurrentPersistenceVersionString();
	std::string source = "$MACRO MR_SETTINGS FROM EDIT;\n"
	                     "MRSETUP('SETTINGS_VERSION', '" + currentVersion + "');\n"
	                     "MRSETUP('TAB_SIZE', '8');\n"
	                     "MRFEPROFILE('DEFINE', 'c_lower', 'Lower C', '');\n"
	                     "MRFEPROFILE('EXT', 'c_lower', 'c', '');\n"
	                     "MRFEPROFILE('SET', 'c_lower', 'TAB_SIZE', '2');\n"
	                     "MRFEPROFILE('DEFINE', 'c_upper', 'Upper C', '');\n"
	                     "MRFEPROFILE('EXT', 'c_upper', 'C', '');\n"
	                     "MRFEPROFILE('SET', 'c_upper', 'TAB_SIZE', '6');\n"
	                     "END_MACRO;\n";
	std::string errorText;
	std::string restoreError;
	bool restored = false;
	MREditSetupSettings effective;
	std::string matchedProfile;
	std::string rewritten;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	if (!setConfiguredEditSetupSettings(resolveEditSetupDefaults(), &errorText)) {
		restore();
		failureReason = "Unable to reset global edit settings before case-sensitive macro probe: " + errorText;
		return false;
	}
	if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(), &errorText)) {
		restore();
		failureReason = "Unable to clear extension profiles before case-sensitive macro probe: " + errorText;
		return false;
	}
	if (!mrApplySettingsSourceForTesting(source, &errorText)) {
		restore();
		failureReason = "Unable to apply case-sensitive profile macro source: " + errorText;
		return false;
	}

	if (configuredEditExtensionProfiles().size() != 2) {
		restore();
		failureReason = "Case-sensitive macro source did not restore exactly two extension profiles.";
		return false;
	}
	if (configuredEditExtensionProfiles()[0].id != "c_lower" || configuredEditExtensionProfiles()[1].id != "c_upper") {
		restore();
		failureReason = "Case-sensitive macro source did not preserve profile ids.";
		return false;
	}
	if (configuredEditExtensionProfiles()[0].extensions.size() != 1 || configuredEditExtensionProfiles()[0].extensions[0] != "c" || configuredEditExtensionProfiles()[1].extensions.size() != 1 || configuredEditExtensionProfiles()[1].extensions[0] != "C") {
		restore();
		failureReason = "Case-sensitive macro source did not preserve exact extension selectors.";
		return false;
	}

	if (!effectiveEditSetupSettingsForPath("/tmp/example.c", effective, &matchedProfile)) {
		restore();
		failureReason = "Effective profile lookup failed for macro-defined .c profile.";
		return false;
	}
	if (matchedProfile != "Lower C" || effective.tabSize != 2) {
		restore();
		failureReason = "Macro-defined lower-case profile did not resolve exactly.";
		return false;
	}

	if (!effectiveEditSetupSettingsForPath("/tmp/example.C", effective, &matchedProfile)) {
		restore();
		failureReason = "Effective profile lookup failed for macro-defined .C profile.";
		return false;
	}
	if (matchedProfile != "Upper C" || effective.tabSize != 6) {
		restore();
		failureReason = "Macro-defined upper-case profile did not resolve exactly.";
		return false;
	}

	paths.settingsMacroUri = snapshot.settingsMacroFilePath;
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	rewritten = buildSettingsMacroSource(paths);
	if (rewritten.find("MRFEPROFILE('EXT', 'c_lower', 'c', '');") == std::string::npos || rewritten.find("MRFEPROFILE('EXT', 'c_upper', 'C', '');") == std::string::npos) {
		restore();
		failureReason = "Case-sensitive macro rewrite did not preserve exact extension selectors.";
		return false;
	}
	if (rewritten.find("MRFEPROFILE('SET', 'c_lower', 'TAB_SIZE', '2');") == std::string::npos || rewritten.find("MRFEPROFILE('SET', 'c_upper', 'TAB_SIZE', '6');") == std::string::npos) {
		restore();
		failureReason = "Case-sensitive macro rewrite did not preserve profile override values.";
		return false;
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after case-sensitive macro probe: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testEditProfileDuplicateExactExtensionMacroGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	const std::string currentVersion = mrCurrentPersistenceVersionString();
	std::string source = "$MACRO MR_SETTINGS FROM EDIT;\n"
	                     "MRSETUP('SETTINGS_VERSION', '" + currentVersion + "');\n"
	                     "MRFEPROFILE('DEFINE', 'c_one', 'One', '');\n"
	                     "MRFEPROFILE('EXT', 'c_one', 'c', '');\n"
	                     "MRFEPROFILE('DEFINE', 'c_two', 'Two', '');\n"
	                     "MRFEPROFILE('EXT', 'c_two', 'c', '');\n"
	                     "END_MACRO;\n";
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	if (mrApplySettingsSourceForTesting(source, &errorText)) {
		restore();
		failureReason = "Duplicate exact extension assignment was accepted from macro source.";
		return false;
	}
	if (errorText.find("Duplicate profile extension 'c'") == std::string::npos) {
		restore();
		failureReason = "Duplicate exact extension assignment should report the conflicting selector.";
		return false;
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after duplicate extension macro probe: " + restoreError;
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
    {"CODE_FOLDING", "true"},
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

bool testEditProfileDescriptorConformanceGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	const std::string currentVersion = mrCurrentPersistenceVersionString();
	const std::string themePath = "/tmp/mr_regression_profile_conformance_theme_" + std::to_string(static_cast<long>(::getpid())) + ".mrmac";
	const std::string profileId = "profile_conformance";
	MREditSetupSettings globalSettings = resolveEditSetupDefaults();
	MRSetupPaths paths = resolveSetupPathDefaults();
	std::size_t descriptorCount = 0;
	const MREditSettingDescriptor *descriptors = editSettingDescriptors(descriptorCount);
	std::string source;
	std::string rewritten;
	std::string errorText;
	std::string restoreError;
	bool restored = false;
	const std::vector<MREditExtensionProfile> *profiles = nullptr;
	MREditSetupSettings effective;
	MREditSetupSettings fallback;
	std::string matchedProfile;
	std::string effectiveThemePath;
	std::vector<MREditExtensionProfile> originalProfiles;

	auto restore = [&]() {
		::unlink(themePath.c_str());
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	globalSettings.tabSize = 11;
	globalSettings.formatRuler = true;
	globalSettings.lineNumbersPosition = "OFF";
	globalSettings.showLineNumbers = false;
	globalSettings.codeLanguage = "NONE";
	if (!setConfiguredEditSetupSettings(globalSettings, &errorText)) {
		restore();
		failureReason = "Unable to seed globals for profile descriptor conformance: " + errorText;
		return false;
	}
	if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(), &errorText)) {
		restore();
		failureReason = "Unable to clear profiles before descriptor conformance: " + errorText;
		return false;
	}

	source = "$MACRO MR_SETTINGS FROM EDIT;\n";
	source += "MRSETUP('SETTINGS_VERSION', '" + currentVersion + "');\n";
	source += "MRFEPROFILE('DEFINE', '" + profileId + "', 'Profile Conformance', '');\n";
	source += "MRFEPROFILE('EXT', '" + profileId + "', 'qsprof', '');\n";
	source += "MRFEPROFILE('SET', '" + profileId + "', 'WINDOW_COLORTHEME_URI', '" + themePath + "');\n";
	source += "MRFEPROFILE('SET', '" + profileId + "', 'COMPILER_PROFILE', 'MR_PROFILE_QS_COMPILER');\n";
	for (std::size_t i = 0; i < descriptorCount; ++i) {
		const MREditSettingDescriptor &descriptor = descriptors[i];
		const char *value = profileConformanceProbeValueForKey(descriptor.key);

		if (!descriptor.profileSupported) continue;
		if (value == nullptr) {
			restore();
			failureReason = std::string("Profile-supported descriptor has no conformance probe value: ") + descriptor.key;
			return false;
		}
		source += "MRFEPROFILE('SET', '" + profileId + "', '" + descriptor.key + "', '" + value + "');\n";
	}
	source += "END_MACRO;\n";

	if (!mrApplySettingsSourceForTesting(source, &errorText)) {
		restore();
		failureReason = "Unable to apply descriptor-driven MRFEPROFILE source: " + errorText;
		return false;
	}
	profiles = &configuredEditExtensionProfiles();
	if (profiles->size() != 1 || (*profiles)[0].id != profileId) {
		restore();
		failureReason = "Descriptor-driven MRFEPROFILE source did not create exactly one expected profile.";
		return false;
	}
	if ((*profiles)[0].windowColorThemeUri != themePath || (*profiles)[0].compilerProfileId != "MR_PROFILE_QS_COMPILER") {
		restore();
		failureReason = "Special MRFEPROFILE SET tokens were not stored on the profile.";
		return false;
	}

	if (!effectiveEditSetupSettingsForPath("/tmp/example.qsprof", effective, &matchedProfile)) {
		restore();
		failureReason = "Effective settings lookup failed for descriptor conformance profile.";
		return false;
	}
	if (matchedProfile != "Profile Conformance") {
		restore();
		failureReason = "Effective settings lookup did not report the descriptor conformance profile.";
		return false;
	}
	if (!effectiveEditSetupSettingsForPath("/tmp/example.none", fallback, &matchedProfile)) {
		restore();
		failureReason = "Fallback settings lookup failed during descriptor conformance.";
		return false;
	}
	if (!matchedProfile.empty() || fallback != configuredEditSetupSettings()) {
		restore();
		failureReason = "Non-matching extension did not preserve global edit settings.";
		return false;
	}
	if (!effectiveEditWindowColorThemePathForPath("/tmp/example.qsprof", effectiveThemePath, &matchedProfile)) {
		restore();
		failureReason = "Effective theme lookup failed for descriptor conformance profile.";
		return false;
	}
	if (effectiveThemePath != themePath || matchedProfile != "Profile Conformance") {
		restore();
		failureReason = "WINDOW_COLORTHEME_URI did not participate in profile-specific lookup.";
		return false;
	}

	for (std::size_t i = 0; i < descriptorCount; ++i) {
		const MREditSettingDescriptor &descriptor = descriptors[i];
		std::string expected;
		std::string actual;

		if (!descriptor.profileSupported) continue;
		if (((*profiles)[0].overrides.mask & descriptor.overrideBit) == 0) {
			restore();
			failureReason = std::string("MRFEPROFILE SET did not set override bit for ") + descriptor.key;
			return false;
		}
		expected = editSetupValueLiteral((*profiles)[0].overrides.values, descriptor.key);
		actual = editSetupValueLiteral(effective, descriptor.key);
		if (expected != actual) {
			restore();
			failureReason = std::string("Effective profile merge mismatch for ") + descriptor.key;
			return false;
		}
	}

	paths.settingsMacroUri = snapshot.settingsMacroFilePath;
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	rewritten = buildSettingsMacroSource(paths);
	if (rewritten.find("MRFEPROFILE('SET', '" + profileId + "', 'WINDOW_COLORTHEME_URI', '" + themePath + "');") == std::string::npos ||
	    rewritten.find("MRFEPROFILE('SET', '" + profileId + "', 'COMPILER_PROFILE', 'MR_PROFILE_QS_COMPILER');") == std::string::npos) {
		restore();
		failureReason = "Profile serializer did not emit special profile SET tokens.";
		return false;
	}
	for (std::size_t i = 0; i < descriptorCount; ++i) {
		const MREditSettingDescriptor &descriptor = descriptors[i];
		const std::string needle = "MRFEPROFILE('SET', '" + profileId + "', '" + descriptor.key + "', '";

		if (!descriptor.profileSupported) continue;
		if (rewritten.find(needle) == std::string::npos) {
			restore();
			failureReason = std::string("Profile serializer did not emit descriptor token ") + descriptor.key;
			return false;
		}
	}

	originalProfiles = *profiles;
	if (!setConfiguredEditSetupSettings(resolveEditSetupDefaults(), &errorText)) {
		restore();
		failureReason = "Unable to reset edit settings before descriptor profile re-apply: " + errorText;
		return false;
	}
	if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(), &errorText)) {
		restore();
		failureReason = "Unable to clear profiles before descriptor profile re-apply: " + errorText;
		return false;
	}
	if (!mrApplySettingsSourceForTesting(rewritten, &errorText)) {
		restore();
		failureReason = "Unable to re-apply descriptor profile serializer output: " + errorText;
		return false;
	}
	if (configuredEditExtensionProfiles() != originalProfiles) {
		restore();
		failureReason = "Descriptor profile serializer output did not re-create the same profile model.";
		return false;
	}
	if (!effectiveEditSetupSettingsForPath("/tmp/example.qsprof", effective, &matchedProfile)) {
		restore();
		failureReason = "Effective settings lookup failed after descriptor profile re-apply.";
		return false;
	}
	for (std::size_t i = 0; i < descriptorCount; ++i) {
		const MREditSettingDescriptor &descriptor = descriptors[i];
		std::string expected;
		std::string actual;

		if (!descriptor.profileSupported) continue;
		expected = editSetupValueLiteral(originalProfiles[0].overrides.values, descriptor.key);
		actual = editSetupValueLiteral(effective, descriptor.key);
		if (expected != actual) {
			restore();
			failureReason = std::string("Descriptor profile re-apply lost effective value for ") + descriptor.key;
			return false;
		}
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after descriptor profile conformance: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testEditProfileInvalidMacroDoesNotLeaveProfileGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	const std::string currentVersion = mrCurrentPersistenceVersionString();
	std::string source = "$MACRO MR_SETTINGS FROM EDIT;\n"
	                     "MRSETUP('SETTINGS_VERSION', '" + currentVersion + "');\n"
	                     "MRFEPROFILE('DEFINE', 'invalid_profile', 'Invalid', '');\n"
	                     "MRFEPROFILE('EXT', 'invalid_profile', 'badprof', '');\n"
	                     "MRFEPROFILE('SET', 'invalid_profile', 'TAB_SIZE', '0');\n"
	                     "END_MACRO;\n";
	std::string errorText;
	std::string restoreError;
	bool restored = false;
	MREditSetupSettings effective;
	std::string matchedProfile;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(), &errorText)) {
		restore();
		failureReason = "Unable to clear profiles before invalid profile macro probe: " + errorText;
		return false;
	}
	if (mrApplySettingsSourceForTesting(source, &errorText)) {
		restore();
		failureReason = "Invalid MRFEPROFILE TAB_SIZE was accepted.";
		return false;
	}
	if (errorText.find("TAB_SIZE") == std::string::npos) {
		restore();
		failureReason = "Invalid MRFEPROFILE value should report TAB_SIZE.";
		return false;
	}
	if (!configuredEditExtensionProfiles().empty()) {
		restore();
		failureReason = "Invalid MRFEPROFILE source left a partial profile in runtime state.";
		return false;
	}
	if (!effectiveEditSetupSettingsForPath("/tmp/example.badprof", effective, &matchedProfile)) {
		restore();
		failureReason = "Effective lookup failed after invalid profile macro probe.";
		return false;
	}
	if (!matchedProfile.empty()) {
		restore();
		failureReason = "Invalid MRFEPROFILE source left a selectable effective profile.";
		return false;
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after invalid profile macro probe: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}

struct CodeLanguageConformanceEntry {
	const char *settingValue;
	const char *extension;
	const char *text;
	MRSyntaxLanguage language;
	const char *marker;
	const char *lspLanguageId;
	bool automatic;
};

static const CodeLanguageConformanceEntry kCodeLanguageConformanceEntries[] = {
	{"NONE", "txtnone", "plain text\n", MRSyntaxLanguage::PlainText, "", "plaintext", false},
	{"AUTO", "c", "#include <stdio.h>\nint main(void) { return 0; }\n", MRSyntaxLanguage::C, "C", "c", true},
	{"C", "langc", "int main(void) { return 0; }\n", MRSyntaxLanguage::C, "C", "c", false},
	{"CPP", "langcpp", "class Probe { public: int value; };\n", MRSyntaxLanguage::Cpp, "C++", "cpp", false},
	{"PYTHON", "langpython", "def probe():\n    return 1\n", MRSyntaxLanguage::Python, "Py", "python", false},
	{"JAVASCRIPT", "langjavascript", "function probe() { return 1; }\n", MRSyntaxLanguage::JavaScript, "JS", "javascript", false},
	{"TYPESCRIPT", "langtypescript", "function probe(value: number): number { return value; }\n", MRSyntaxLanguage::JavaScript, "JS", "javascript", false},
	{"TSX", "langtsx", "const probe = <div />;\n", MRSyntaxLanguage::JavaScript, "JS", "javascript", false},
	{"BASH", "langbash", "if true; then echo ok; fi\n", MRSyntaxLanguage::Bash, "Ba", "shellscript", false},
	{"ZSH", "langzsh", "if true; then echo ok; fi\n", MRSyntaxLanguage::Zsh, "Zh", "shellscript", false},
	{"FISH", "langfish", "if true\n    echo ok\nend\n", MRSyntaxLanguage::Fish, "Fi", "shellscript", false},
	{"JSON", "langjson", "{\"probe\": true}\n", MRSyntaxLanguage::Json, "Jn", "json", false},
	{"YAML", "langyaml", "probe: true\n", MRSyntaxLanguage::Yaml, "Ya", "yaml", false},
	{"XML", "langxml", "<probe />\n", MRSyntaxLanguage::Xml, "Xm", "xml", false},
	{"PERL", "langperl", "sub probe { return 1; }\n", MRSyntaxLanguage::Perl, "Pl", "perl", false},
	{"SWIFT", "langswift", "func probe() -> Int { return 1 }\n", MRSyntaxLanguage::Swift, "Sw", "swift", false},
	{"RUST", "langrust", "fn probe() -> i32 { 1 }\n", MRSyntaxLanguage::Rust, "Rs", "rust", false},
	{"GO", "langgo", "func probe() int { return 1 }\n", MRSyntaxLanguage::Go, "Go", "go", false},
	{"PASCAL", "langpascal", "begin\nend.\n", MRSyntaxLanguage::Pascal, "Pa", "pascal", false},
	{"SYSTEMD", "langsystemd", "[Unit]\nDescription=Probe\n", MRSyntaxLanguage::Systemd, "Sd", "systemd", false},
	{"MAKE", "langmake", "all:\n\t@echo ok\n", MRSyntaxLanguage::Make, "MK", "makefile", false},
	{"MRMAC", "langmrmac", "$MACRO PROBE;\nEND_MACRO;\n", MRSyntaxLanguage::MRMAC, "MR", "mrmac", false},
	{"MARKDOWN", "langmarkdown", "# Probe\n", MRSyntaxLanguage::Markdown, "MD", "markdown", false},
	{"KOTLIN", "langkotlin", "fun probe(): Int = 1\n", MRSyntaxLanguage::Kotlin, "Kt", "kotlin", false},
	{"CSHARP", "langcsharp", "class Probe { int Value() { return 1; } }\n", MRSyntaxLanguage::CSharp, "C#", "csharp", false},
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
		if (std::strcmp(mr::services::lspLanguageIdForSyntaxLanguage(editor.syntaxLanguage()), entry.lspLanguageId) != 0) {
			restore();
			failureReason = std::string("CODE_LANGUAGE ") + entry.settingValue + " exposed unexpected LSP languageId.";
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

bool testPathsBrowseEventGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	const std::string root = "/tmp/mr_regression_paths_roundtrip_" + std::to_string(static_cast<long>(::getpid()));
	const std::string settingsPath = root + "/cfg/settings.mrmac";
	const std::string macroPath = root + "/macros";
	const std::string tempPath = root + "/tmp";
	MRSetupPaths paths = resolveSetupPathDefaults();
	std::string content;
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	paths.settingsMacroUri = settingsPath;
	paths.macroPath = macroPath;
	paths.helpUri = "mr.hlp";
	paths.tempPath = tempPath;
	paths.shellUri = "/bin/sh";
	(void)::mkdir(root.c_str(), 0700);
	(void)::mkdir(macroPath.c_str(), 0700);
	(void)::mkdir(tempPath.c_str(), 0700);

	if (!writeSettingsMacroFile(paths, &errorText)) {
		restore();
		failureReason = "Unable to write paths roundtrip settings.mrmac: " + errorText;
		return false;
	}
	if (!readTextFile(settingsPath, content, errorText)) {
		restore();
		failureReason = "Unable to read paths roundtrip settings.mrmac: " + errorText;
		return false;
	}
	if (!mrApplySettingsSourceForTesting(content, &errorText)) {
		restore();
		failureReason = "Unable to apply paths roundtrip settings.mrmac: " + errorText;
		return false;
	}
	if (defaultMacroDirectoryPath() != macroPath) {
		restore();
		failureReason = "Paths roundtrip did not apply MACROPATH.";
		return false;
	}
	if (configuredTempDirectoryPath() != tempPath) {
		restore();
		failureReason = "Paths roundtrip did not apply TEMPDIR.";
		return false;
	}
	if (configuredShellExecutablePath() != "/bin/sh") {
		restore();
		failureReason = "Paths roundtrip did not apply SHELLPATH.";
		return false;
	}
	if (configuredHelpFilePath() != absolutePathFromCwd("mr.hlp")) {
		restore();
		failureReason = "Paths roundtrip did not normalize and apply HELPPATH.";
		return false;
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after paths roundtrip probe: " + restoreError;
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

bool colorGroupValueAt(const MRColorSetupSettings &settings, MRColorSetupGroup group, std::size_t index, unsigned char &outValue) {
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
	}
	errorText = "Unknown color group.";
	return false;
}

bool testColorThemeInventoryConformanceGuard(std::string &failureReason) {
	MRColorSetupSettings previous = configuredColorSetupSettings();
	MRColorSetupSettings defaults = resolveColorSetupDefaults();
	std::string source = buildColorThemeMacroSource(defaults);
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
		std::vector<unsigned char> probeValues;
		unsigned char overrideValue = 0;

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
			if (!duplicateSlot) probeValues[i] = static_cast<unsigned char>(0x20 + ((groupIndex * 19 + i) % 0x5F));
		}

		if (!setConfiguredColorSetupGroupValues(entry.group, probeValues.data(), probeValues.size(), &errorText)) {
			restore();
			failureReason = std::string("Unable to set probe values for ") + entry.key + ": " + errorText;
			return false;
		}
		for (std::size_t i = 0; i < count; ++i) {
			unsigned char runtimeValue = 0;

			if (!colorGroupValueAt(configuredColorSetupSettings(), entry.group, i, runtimeValue) || runtimeValue != probeValues[i]) {
				restore();
				failureReason = std::string("Runtime color setup value mismatch for ") + entry.key;
				return false;
			}
			if (!configuredColorSlotOverride(items[i].paletteIndex, overrideValue)) {
				restore();
				failureReason = std::string("configuredColorSlotOverride does not expose ") + entry.key + " slot " + items[i].label;
				return false;
			}
			if (overrideValue != probeValues[i]) {
				restore();
				failureReason = std::string("Color slot override mismatch for ") + entry.key + " slot " + items[i].label;
				return false;
			}
		}
	}

	restore();
	if (!restored) {
		failureReason = "Unable to restore color setup after inventory conformance: " + restoreError;
		return false;
	}
	failureReason.clear();
	return true;
}

bool testColorSetupSaveThemeUsesWorkingPaletteGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	const std::string root = "/tmp/mr_regression_color_save_theme_" + std::to_string(static_cast<long>(::getpid()));
	const std::string settingsPath = root + "/cfg/settings.mrmac";
	const std::string themePath = root + "/cfg/probe-theme.mrmac";
	static const MRColorSetupGroup groups[] = {MRColorSetupGroup::Window, MRColorSetupGroup::MenuDialog, MRColorSetupGroup::Help, MRColorSetupGroup::Other, MRColorSetupGroup::MiniMap, MRColorSetupGroup::FileCompareMiniMap, MRColorSetupGroup::FileCompare};
	TColorAttr paletteData[kMrPaletteMax];
	TPalette workingPalette(paletteData, static_cast<ushort>(kMrPaletteMax));
	MRSetupPaths paths = resolveSetupPathDefaults();
	std::string content;
	std::string errorText;
	std::string restoreError;
	bool restored = false;
	unsigned char nextColor = 0x21;

	auto restore = [&]() {
		if (!restored) restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	for (int i = 0; i < kMrPaletteMax; ++i)
		paletteData[i] = 0x70;

	for (MRColorSetupGroup group : groups) {
		std::size_t count = 0;
		const MRColorSetupItem *items = colorSetupGroupItems(group, count);
		if (items == nullptr || count == 0) continue;
		for (std::size_t i = 0; i < count; ++i) {
			workingPalette[items[i].paletteIndex] = nextColor;
			++nextColor;
		}
	}

	paths.settingsMacroUri = settingsPath;
	paths.macroPath = root + "/macros";
	paths.helpUri = "mr.hlp";
	paths.tempPath = root + "/tmp";
	paths.shellUri = "/bin/sh";
	(void)::mkdir(root.c_str(), 0700);
	(void)::mkdir(paths.macroPath.c_str(), 0700);
	(void)::mkdir(paths.tempPath.c_str(), 0700);
	if (!setConfiguredSettingsMacroFilePath(settingsPath, &errorText)) {
		restore();
		failureReason = "Unable to configure settings path for Color Setup save-theme probe: " + errorText;
		return false;
	}
	if (!writeSettingsMacroFile(paths, &errorText)) {
		restore();
		failureReason = "Unable to prime settings file for Color Setup save-theme probe: " + errorText;
		return false;
	}

	if (!mrSaveColorThemeFromWorkingPaletteForTesting(workingPalette, themePath, &errorText)) {
		restore();
		failureReason = "Color Setup save-theme behavior probe failed: " + errorText;
		return false;
	}
	if (!readTextFile(themePath, content, errorText)) {
		restore();
		failureReason = "Unable to read saved theme file after Color Setup save-theme probe: " + errorText;
		return false;
	}
	if (content.find("WINDOWCOLORS('") == std::string::npos || content.find("MENUDIALOGCOLORS('") == std::string::npos || content.find("HELPCOLORS('") == std::string::npos || content.find("OTHERCOLORS('") == std::string::npos || content.find("MINIMAPCOLORS('") == std::string::npos || content.find("FILECOMPAREMINIMAPCOLORS") == std::string::npos || content.find("FILECOMPARECOLORS('") == std::string::npos) {
		restore();
		failureReason = "Saved color theme must contain all color group assignments.";
		return false;
	}

	{
		const MRColorSetupSettings configured = configuredColorSetupSettings();
		for (MRColorSetupGroup group : groups) {
			std::size_t count = 0;
			const MRColorSetupItem *items = colorSetupGroupItems(group, count);
			if (items == nullptr || count == 0) continue;
			for (std::size_t i = 0; i < count; ++i) {
				const unsigned char expected = static_cast<unsigned char>(workingPalette[items[i].paletteIndex]);
				unsigned char actual = 0;
				switch (group) {
					case MRColorSetupGroup::Window:
						actual = configured.windowColors[i];
						break;
					case MRColorSetupGroup::MenuDialog:
						actual = configured.menuDialogColors[i];
						break;
					case MRColorSetupGroup::Help:
						actual = configured.helpColors[i];
						break;
					case MRColorSetupGroup::Other:
						actual = configured.otherColors[i];
						break;
					case MRColorSetupGroup::MiniMap:
						actual = configured.miniMapColors[i];
						break;
					case MRColorSetupGroup::FileCompareMiniMap:
						actual = configured.fileCompareMiniMapColors[i];
						break;
					case MRColorSetupGroup::Code:
						// Code colors are intentionally outside this guard's scope.
						actual = expected;
						break;
					case MRColorSetupGroup::FileCompare:
						actual = configured.fileCompareColors[i];
						break;
				}
				if (actual != expected) {
					restore();
					failureReason = "Color Setup save-theme did not apply the working palette before persisting.";
					return false;
				}
			}
		}
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after Color Setup save-theme probe: " + restoreError;
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
	{"WINDOWCOLORS", "v7:21"},
	{"MENUDIALOGCOLORS", "v1:21"},
	{"HELPCOLORS", "v1:21"},
	{"OTHERCOLORS", "v1:21"},
	{"MINIMAPCOLORS", "v1:21"},
	{"FILECOMPAREMINIMAPCOLORS", "v1:21"},
	{"CODECOLORS", "v1:21"},
	{"FILECOMPARECOLORS", "v2:21"},
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
		if (configuredColorSetupSettings() != previous) {
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
	const std::string windowColorsPrefix = "WINDOWCOLORS('v7:";
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::string previousThemePath = configuredColorThemeFilePath();
	const std::array<unsigned char, MRColorSetupSettings::kWindowCount> probeValues = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E};
	std::string errorText;
	std::string content;
	std::string contentAfterLoad;
	unsigned char slotValue = 0;
	bool restored = true;

	auto restore = [&]() {
		std::string restoreError;
		if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, previous.windowColors.data(), previous.windowColors.size(), &restoreError)) restored = false;
		if (!setConfiguredColorThemeFilePath(previousThemePath, &restoreError)) restored = false;
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
		failureReason = "Saved theme must serialize WINDOWCOLORS using canonical v7 list format.";
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
				failureReason = "WINDOWCOLORS v7 roundtrip mismatch after theme reload.";
				restore();
				return false;
			}
	}
	if (!configuredColorSlotOverride(kMrPaletteLineNumbers, slotValue) || slotValue != probeValues[8]) {
		failureReason = "Line-number palette slot must be restored from WINDOWCOLORS theme value.";
		restore();
		return false;
	}
	if (!configuredColorSlotOverride(kMrPaletteCodeFolding, slotValue) || slotValue != probeValues[9]) {
		failureReason = "Code-folding palette slot must be restored from WINDOWCOLORS theme value.";
		restore();
		return false;
	}
	if (!configuredColorSlotOverride(kMrPaletteCodeFoldingMarker, slotValue) || slotValue != probeValues[10]) {
		failureReason = "Code-folding-marker palette slot must be restored from WINDOWCOLORS theme value.";
		restore();
		return false;
	}
	if (!configuredColorSlotOverride(kMrPaletteFormatRuler, slotValue) || slotValue != probeValues[11]) {
		failureReason = "Format-ruler palette slot must be restored from WINDOWCOLORS theme value.";
		restore();
		return false;
	}
	if (!configuredColorSlotOverride(kMrPaletteFocusedPaneBorder, slotValue) || slotValue != probeValues[12]) {
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
	const std::string paneWindowPath = absolutePathFromCwd("ui/MRBentoBoxPaneWindow.cpp");
	const std::string projectionPath = absolutePathFromCwd("ui/MRBentoBoxProjection.cpp");
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
	if (viewportContent.find("diffTextColor = static_cast<TColorAttr>(configured);") == std::string::npos) {
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
	static const unsigned char probeValues[] = {0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87, 0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0x1E, 0x2F, 0x3A, 0x4B};
	struct CodeColorInventoryEntry {
		const char *name;
		const char *paletteMacro;
		unsigned char paletteIndex;
		bool tokenColorConsumer;
		bool sidekickConsumer;
		bool contextMenuConsumer;
		bool explicitReserve;
	};
	static const CodeColorInventoryEntry codeColorInventory[] = {
	    {"comments", "kMrPaletteCodeComments", kMrPaletteCodeComments, true, false, false, false},
	    {"strings", "kMrPaletteCodeStrings", kMrPaletteCodeStrings, true, false, false, false},
	    {"characters", "kMrPaletteCodeCharacters", kMrPaletteCodeCharacters, false, false, false, true},
	    {"numbers", "kMrPaletteCodeNumbers", kMrPaletteCodeNumbers, true, false, false, false},
	    {"keywords", "kMrPaletteCodeKeywords", kMrPaletteCodeKeywords, true, false, false, false},
	    {"types", "kMrPaletteCodeTypes", kMrPaletteCodeTypes, true, false, false, false},
	    {"directives", "kMrPaletteCodeDirectives", kMrPaletteCodeDirectives, true, false, false, false},
	    {"functions", "kMrPaletteCodeFunctions", kMrPaletteCodeFunctions, false, false, false, true},
	    {"builtins", "kMrPaletteCodeBuiltins", kMrPaletteCodeBuiltins, false, false, false, true},
	    {"constants", "kMrPaletteCodeConstants", kMrPaletteCodeConstants, true, false, false, false},
	    {"operators", "kMrPaletteCodeOperators", kMrPaletteCodeOperators, false, false, false, true},
	    {"brackets", "kMrPaletteCodeBrackets", kMrPaletteCodeBrackets, false, false, false, true},
	    {"delimiters", "kMrPaletteCodeDelimiters", kMrPaletteCodeDelimiters, true, false, false, false},
	    {"sidekick editor text", "kMrPaletteSidekickEditorText", kMrPaletteSidekickEditorText, false, true, false, false},
	    {"sidekick editor highlight", "kMrPaletteSidekickEditorHighlight", kMrPaletteSidekickEditorHighlight, false, true, false, false},
	    {"context menu", "kMrPaletteContextMenu", kMrPaletteContextMenu, false, false, true, false},
	    {"context menu selector", "kMrPaletteContextMenuSelector", kMrPaletteContextMenuSelector, false, false, true, false},
	};
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::size_t itemCount = 0;
	const MRColorSetupItem *items = colorSetupGroupItems(MRColorSetupGroup::Code, itemCount);
	const std::string viewportPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorViewport.cpp");
	const std::string sidekickPath = absolutePathFromCwd("ui/MRSidekickEditor.cpp");
	const std::string columnListPath = absolutePathFromCwd("ui/widgets/MRColumnListView.cpp");
	std::string viewportContent;
	std::string sidekickContent;
	std::string columnListContent;
	std::string tokenColorFunction;
	std::string errorText;
	unsigned char value = 0;
	bool restoreOk = true;

	auto restore = [&]() {
		if (!restoreOk) return;
		restoreOk = setConfiguredColorSetupGroupValues(MRColorSetupGroup::Code, previous.codeColors.data(), previous.codeColors.size(), &errorText);
	};

	if (items == nullptr || itemCount != sizeof(probeValues) / sizeof(probeValues[0]) || itemCount != sizeof(codeColorInventory) / sizeof(codeColorInventory[0])) {
		failureReason = "Unexpected CODECOLORS item mapping.";
		return false;
	}
	for (std::size_t i = 0; i < itemCount; ++i) {
		if (std::strcmp(items[i].label, codeColorInventory[i].name) != 0 || items[i].paletteIndex != codeColorInventory[i].paletteIndex) {
			failureReason = "CODECOLORS inventory order, name or palette slot changed without updating the conformance guard.";
			return false;
		}
	}
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Code, probeValues, sizeof(probeValues) / sizeof(probeValues[0]), &errorText)) {
		failureReason = "Unable to set CODECOLORS probe values: " + errorText;
		return false;
	}
	for (std::size_t i = 0; i < itemCount; ++i) {
		if (!configuredColorSlotOverride(items[i].paletteIndex, value)) {
			restore();
			failureReason = "CODECOLORS item must override its mapped palette slot.";
			return false;
		}
		if (value != probeValues[i]) {
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
	const std::size_t tokenColorStart = viewportContent.find("TColorAttr MRFileEditor::tokenColor");
	const std::size_t tokenColorEnd = viewportContent.find("\nvoid MRFileEditor::formatSyntaxLine", tokenColorStart);
	if (tokenColorStart == std::string::npos || tokenColorEnd == std::string::npos) {
		restore();
		failureReason = "Unable to isolate MRFileEditor::tokenColor for code color guard.";
		return false;
	}
	tokenColorFunction = viewportContent.substr(tokenColorStart, tokenColorEnd - tokenColorStart);
	if (tokenColorFunction.find("if (configuredColorSlotOverride(paletteSlot, configured)) return static_cast<TColorAttr>(configured);") == std::string::npos) {
		restore();
		failureReason = "Code token colors must preserve the full configured attribute, including background.";
		return false;
	}
	if (tokenColorFunction.find("return static_cast<TColorAttr>(background | fallbackForeground);") == std::string::npos) {
		restore();
		failureReason = "Code token fallback colors must still combine editor background with fallback foreground.";
		return false;
	}
	if (tokenColorFunction.find("configured & 0x0F") != std::string::npos || tokenColorFunction.find("(configured &") != std::string::npos) {
		restore();
		failureReason = "Code token colors must not mask configured colors down to foreground.";
		return false;
	}
	for (std::size_t i = 0; i < itemCount; ++i) {
		const CodeColorInventoryEntry &entry = codeColorInventory[i];
		const bool usedByTokenColor = tokenColorFunction.find(entry.paletteMacro) != std::string::npos;
		const bool usedBySidekick = sidekickContent.find(entry.paletteMacro) != std::string::npos;
		const bool usedByContextMenu = columnListContent.find(entry.paletteMacro) != std::string::npos;

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
		if (entry.explicitReserve && (usedByTokenColor || usedBySidekick || usedByContextMenu)) {
			restore();
			failureReason = std::string("CODECOLORS reserve slot gained a consumer without contract update: ") + entry.name;
			return false;
		}
		if (!entry.tokenColorConsumer && !entry.sidekickConsumer && !entry.contextMenuConsumer && !entry.explicitReserve) {
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
	    content.find("formatSyntaxLine(buffer, currentLinePtr, syntaxLine, delta.x, textWidth, viewport.textLeft, isDocumentLine, false, false);") == std::string::npos) {
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
	if (content.find("if (!drawEmoji && configuredColorSlotOverride(kMrPaletteEofMarker, configuredMarkerColor))") == std::string::npos) {
		failureReason = "EOF marker must support emoji toggle with text-mode color override wiring.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testLspCompletionReportingMarksBeforeDialogGuard(std::string &failureReason) {
	const std::string routerPath = absolutePathFromCwd("app/MRCommandRouter.cpp");
	std::string content;
	std::string functionBody;
	std::string helperBody;
	std::string requestBody;
	std::string ioError;

	if (!readTextFile(routerPath, content, ioError)) {
		failureReason = "Unable to read MRCommandRouter.cpp for LSP completion reporting guard: " + ioError;
		return false;
	}

	const std::size_t helperStart = content.find("void reportLspCompletionResult");
	const std::size_t helperEnd = content.find("\nvoid reportNewLspCompletions", helperStart);
	if (helperStart == std::string::npos || helperEnd == std::string::npos) {
		failureReason = "Unable to isolate reportLspCompletionResult for LSP completion reporting guard.";
		return false;
	}
	helperBody = content.substr(helperStart, helperEnd - helperStart);
	if (helperBody.find("showLspCompletionDialog(result)") == std::string::npos) {
		failureReason = "LSP completion result helper must open the completion dialog.";
		return false;
	}

	const std::size_t functionStart = content.find("void reportNewLspCompletions", helperEnd);
	const std::size_t functionEnd = content.find("\nbool lspCompletionRequestIdKnown", functionStart);
	if (functionStart == std::string::npos || functionEnd == std::string::npos) {
		failureReason = "Unable to isolate reportNewLspCompletions for LSP completion reporting guard.";
		return false;
	}
	functionBody = content.substr(functionStart, functionEnd - functionStart);

	const std::size_t resultCopy = functionBody.find("const mr::services::MRServiceCompletionResult result = completions[g_lspReportedCompletionCount];");
	const std::size_t reportedIncrement = functionBody.find("++g_lspReportedCompletionCount;", resultCopy);
	const std::size_t helperCall = functionBody.find("reportLspCompletionResult(result)", resultCopy);
	if (resultCopy == std::string::npos || reportedIncrement == std::string::npos || helperCall == std::string::npos) {
		failureReason = "LSP completion reporting must copy the pending result, advance the reported count, and then report the copied result.";
		return false;
	}
	if (reportedIncrement > helperCall) {
		failureReason = "LSP completion reporting must advance g_lspReportedCompletionCount before opening the modal completion dialog via the helper.";
		return false;
	}

	const std::size_t requestStart = content.find("bool requestLspCompletionCommand");
	const std::size_t requestEnd = content.find("\nvoid reportNewLspResults", requestStart);
	if (requestStart == std::string::npos || requestEnd == std::string::npos) {
		failureReason = "Unable to isolate requestLspCompletionCommand for LSP completion replacement guard.";
		return false;
	}
	requestBody = content.substr(requestStart, requestEnd - requestStart);
	const std::size_t requestIdCheck = requestBody.find("lspCompletionRequestIdKnown(knownRequestIds, completions[index].header.requestId)");
	const std::size_t markReported = requestBody.find("g_lspReportedCompletionCount = completions.size()", requestIdCheck);
	const std::size_t directReport = requestBody.find("reportLspCompletionResult(completions[index])", requestIdCheck);
	const std::size_t pollCall = requestBody.find("g_lspAppService.poll(errorMessage)");
	const std::size_t completionsRead = requestBody.find("completionResults()", pollCall);
	const std::size_t genericReport = requestBody.find("reportNewLspResults");
	const std::size_t retryLoop = requestBody.find("attempt < 2");
	const std::size_t knownRequestPush = requestBody.find("knownRequestIds.push_back(completions[index].header.requestId)", requestIdCheck);
	const std::size_t emptyRetry = requestBody.find("attempt == 0 && completions[index].items.empty()", requestIdCheck);
	const std::size_t delayedRetryWait = requestBody.find("waitIndex < 15", emptyRetry);
	const std::size_t retryFlag = requestBody.find("retryAfterEmptyCompletion = true", emptyRetry);
	if (requestIdCheck == std::string::npos || markReported == std::string::npos || directReport == std::string::npos) {
		failureReason = "LSP completion command must detect replacement results by request id and report the matching result directly.";
		return false;
	}
	if (markReported > directReport) {
		failureReason = "LSP completion command must mark stored completion results reported before opening the modal completion dialog.";
		return false;
	}
	if (pollCall == std::string::npos || completionsRead == std::string::npos || pollCall > completionsRead) {
		failureReason = "LSP completion command must poll and inspect completion results directly.";
		return false;
	}
	if (genericReport != std::string::npos) {
		failureReason = "LSP completion command must not use generic result reporting while waiting for completion.";
		return false;
	}
	if (retryLoop == std::string::npos || knownRequestPush == std::string::npos || emptyRetry == std::string::npos || delayedRetryWait == std::string::npos || retryFlag == std::string::npos) {
		failureReason = "LSP completion command must retry one empty fresh completion response after a short poll delay without reusing the same request id.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testLspCompletionInsertTextGuard(std::string &failureReason) {
	const std::string routerPath = absolutePathFromCwd("app/MRCommandRouter.cpp");
	std::string content;
	std::string insertTextBody;
	std::string dialogBody;
	std::string ioError;

	if (!readTextFile(routerPath, content, ioError)) {
		failureReason = "Unable to read MRCommandRouter.cpp for LSP completion insert-text guard: " + ioError;
		return false;
	}

	const std::size_t insertTextStart = content.find("std::string lspCompletionInsertTextForItem");
	const std::size_t insertTextEnd = content.find("\nbool showLspCompletionDialog", insertTextStart);
	if (insertTextStart == std::string::npos || insertTextEnd == std::string::npos) {
		failureReason = "Unable to isolate lspCompletionInsertTextForItem.";
		return false;
	}
	insertTextBody = content.substr(insertTextStart, insertTextEnd - insertTextStart);
	if (insertTextBody.find("const std::string rawText = !item.insertText.empty() ? item.insertText : item.label;") == std::string::npos) {
		failureReason = "LSP completion insert text must prefer insertText and fall back to label.";
		return false;
	}
	if (insertTextBody.find("item.hasInsertTextFormat && item.insertTextFormat == 2") == std::string::npos ||
	    insertTextBody.find("lspCompletionPlainTextFromSnippet(rawText)") == std::string::npos) {
		failureReason = "LSP snippet completion must be normalized before insertion.";
		return false;
	}

	const std::size_t dialogStart = content.find("bool showLspCompletionDialog", insertTextEnd);
	const std::size_t dialogEnd = content.find("\nMREditWindow *findLspCodeActionTargetWindow", dialogStart);
	if (dialogStart == std::string::npos || dialogEnd == std::string::npos) {
		failureReason = "Unable to isolate showLspCompletionDialog.";
		return false;
	}
	dialogBody = content.substr(dialogStart, dialogEnd - dialogStart);
	const std::size_t itemLookup = dialogBody.find("const mr::services::MRServiceCompletionItem &item = result.items[selectedIndex];");
	const std::size_t normalizedInsert = dialogBody.find("insertText = lspCompletionReplacementTextForItem(item)", itemLookup);
	const std::size_t dialogDestroy = dialogBody.find("TObject::destroy(dialog)", normalizedInsert);
	const std::size_t editorReplace = dialogBody.find("applyLspCompletionItem(*targetEditor, result, selectedItem, insertText, errorMessage)", dialogDestroy);
	if (itemLookup == std::string::npos || normalizedInsert == std::string::npos || dialogDestroy == std::string::npos || editorReplace == std::string::npos) {
		failureReason = "LSP completion dialog must replace the selected completion range with normalized completion text.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testLspBentoPaneTargetRoutingGuard(std::string &failureReason) {
	const std::string routerPath = absolutePathFromCwd("app/MRCommandRouter.cpp");
	const std::string bentoHeaderPath = absolutePathFromCwd("ui/MRBentoBox.hpp");
	const std::string bentoProjectionPath = absolutePathFromCwd("ui/MRBentoBoxProjection.cpp");
	std::string content;
	std::string bentoHeader;
	std::string bentoProjection;
	std::string targetBody;
	std::string activateBody;
	std::string navigationBody;
	std::string miniMenuBoundsBody;
	std::string workspaceSymbolsBody;
	std::string completionDialogBody;
	std::string resultsDialogBody;
	std::string contextMenuItemsBody;
	std::string contextMenuBody;
	std::string bentoActivateBody;
	std::string hoverBody;
	std::string signatureBody;
	std::string ioError;
	std::string missingNeedle;

	if (!readTextFile(routerPath, content, ioError) || !readTextFile(bentoHeaderPath, bentoHeader, ioError) || !readTextFile(bentoProjectionPath, bentoProjection, ioError)) {
		failureReason = "Unable to read MRCommandRouter.cpp for LSP Bento pane routing guard: " + ioError;
		return false;
	}

	const std::size_t targetStart = content.find("MREditWindow *findOpenLspTargetWindow");
	const std::size_t targetEnd = content.find("\nMREditWindow *findLspResultTargetWindow", targetStart);
	if (targetStart == std::string::npos || targetEnd == std::string::npos) {
		failureReason = "Unable to isolate findOpenLspTargetWindow for LSP Bento pane routing guard.";
		return false;
	}
	targetBody = content.substr(targetStart, targetEnd - targetStart);
	if (targetBody.find("allEditWindowsAndBentoPanesInZOrder()") == std::string::npos) {
		failureReason = "LSP open-target lookup must include visible Bento panes.";
		return false;
	}
	if (targetBody.find("allEditWindowsInZOrder()") != std::string::npos) {
		failureReason = "LSP open-target lookup must not regress to top-level editor enumeration only.";
		return false;
	}

	const std::size_t helperStart = content.find("MREditWindow *findLspResultTargetWindow");
	const std::size_t helperEnd = content.find("\nbool lspVisualColumnForTarget", helperStart);
	if (helperStart == std::string::npos || helperEnd == std::string::npos) {
		failureReason = "Unable to isolate findLspResultTargetWindow for LSP Bento pane routing guard.";
		return false;
	}
	const std::string helperBody = content.substr(helperStart, helperEnd - helperStart);
	if (!containsAllSubstrings(helperBody, {"findEditWindowByBufferId(identity.bufferId)", "findOpenLspTargetWindow(identity.path)"}, missingNeedle)) {
		failureReason = "LSP result target lookup must prefer buffer id and fall back to pane-aware path lookup: missing " + missingNeedle + ".";
		return false;
	}

	if (bentoHeader.find("[[nodiscard]] bool activatePaneWindow(MREditWindow *pane) noexcept;") == std::string::npos) {
		failureReason = "MRBentoBox must expose explicit pane activation for LSP result navigation.";
		return false;
	}
	const std::size_t bentoActivateStart = bentoProjection.find("bool MRBentoBox::activatePaneWindow");
	const std::size_t bentoActivateEnd = bentoProjection.find("\nbool MRBentoBox::showsFrameGrowHandle", bentoActivateStart);
	if (bentoActivateStart == std::string::npos || bentoActivateEnd == std::string::npos) {
		failureReason = "Unable to isolate MRBentoBox::activatePaneWindow for LSP Bento pane routing guard.";
		return false;
	}
	bentoActivateBody = bentoProjection.substr(bentoActivateStart, bentoActivateEnd - bentoActivateStart);
	if (!containsAllSubstrings(bentoActivateBody, {"setActivePane(leaf.id)", "mrActivateEditWindow(this)"}, missingNeedle)) {
		failureReason = "Bento pane activation must select the owning leaf and activate the Bento window: missing " + missingNeedle + ".";
		return false;
	}

	const std::size_t activateStart = content.find("bool activateLspTargetWindow");
	const std::size_t activateEnd = content.find("\nbool lspVisualColumnForTarget", activateStart);
	if (activateStart == std::string::npos || activateEnd == std::string::npos) {
		failureReason = "Unable to isolate activateLspTargetWindow for LSP Bento pane routing guard.";
		return false;
	}
	activateBody = content.substr(activateStart, activateEnd - activateStart);
	if (!containsAllSubstrings(activateBody, {"bentoBox->activatePaneWindow(window)", "dynamic_cast<MRBentoBox *>(window->owner)", "mrActivateEditWindow(window)"}, missingNeedle)) {
		failureReason = "LSP target activation must handle Bento panes and plain editor windows: missing " + missingNeedle + ".";
		return false;
	}

	const std::size_t navigationStart = content.find("bool navigateToLspLocation");
	const std::size_t navigationEnd = content.find("\nstd::string lspLocationDisplayText", navigationStart);
	if (navigationStart == std::string::npos || navigationEnd == std::string::npos) {
		failureReason = "Unable to isolate navigateToLspLocation for LSP Bento pane routing guard.";
		return false;
	}
	navigationBody = content.substr(navigationStart, navigationEnd - navigationStart);
	if (navigationBody.find("activateLspTargetWindow(window)") == std::string::npos || navigationBody.find("mrActivateEditWindow(window)") != std::string::npos) {
		failureReason = "LSP navigation must activate through the Bento-aware target activator.";
		return false;
	}

	const std::size_t miniMenuBoundsStart = content.find("bool lspMiniMenuBoundsFor");
	const std::size_t miniMenuBoundsEnd = content.find("\nMRColumnListView *showLspMiniMenuList", miniMenuBoundsStart);
	if (miniMenuBoundsStart == std::string::npos || miniMenuBoundsEnd == std::string::npos) {
		failureReason = "Unable to isolate lspMiniMenuBoundsFor for LSP Bento pane routing guard.";
		return false;
	}
	miniMenuBoundsBody = content.substr(miniMenuBoundsStart, miniMenuBoundsEnd - miniMenuBoundsStart);
	if (miniMenuBoundsBody.find("visibleTextViewportBounds") == std::string::npos || miniMenuBoundsBody.find("safeWidth > constraint.b.x - constraint.a.x") == std::string::npos) {
		failureReason = "LSP mini menu bounds must stay inside the target editor text viewport.";
		return false;
	}

	const std::size_t workspaceSymbolsStart = content.find("bool showLspWorkspaceSymbolsPicker");
	const std::size_t workspaceSymbolsEnd = content.find("\nstd::vector<LspMiniMenuEntry> buildLspEditMiniMenuItems", workspaceSymbolsStart);
	if (workspaceSymbolsStart == std::string::npos || workspaceSymbolsEnd == std::string::npos) {
		failureReason = "Unable to isolate showLspWorkspaceSymbolsPicker for LSP Bento pane routing guard.";
		return false;
	}
	workspaceSymbolsBody = content.substr(workspaceSymbolsStart, workspaceSymbolsEnd - workspaceSymbolsStart);
	if (countSubstring(workspaceSymbolsBody, "displayRows.push_back") != 1) {
		failureReason = "LSP workspace symbols picker must append display rows only after symbol ranking.";
		return false;
	}

	const std::size_t completionDialogStart = content.find("bool showLspCompletionDialog");
	const std::size_t completionDialogEnd = content.find("\nMREditWindow *findLspCodeActionTargetWindow", completionDialogStart);
	if (completionDialogStart == std::string::npos || completionDialogEnd == std::string::npos) {
		failureReason = "Unable to isolate showLspCompletionDialog for LSP Bento pane routing guard.";
		return false;
	}
	completionDialogBody = content.substr(completionDialogStart, completionDialogEnd - completionDialogStart);
	if (completionDialogBody.find("activateLspTargetWindow(targetWindow)") == std::string::npos || completionDialogBody.find("mrActivateEditWindow(targetWindow)") != std::string::npos) {
		failureReason = "LSP completion insertion must activate through the Bento-aware target activator.";
		return false;
	}

	const std::size_t resultsDialogStart = content.find("bool showLspResultsDialog");
	const std::size_t resultsDialogEnd = content.find("\nvoid reportNewLspDiagnostics", resultsDialogStart);
	if (resultsDialogStart == std::string::npos || resultsDialogEnd == std::string::npos) {
		failureReason = "Unable to isolate showLspResultsDialog for LSP Bento pane routing guard.";
		return false;
	}
	resultsDialogBody = content.substr(resultsDialogStart, resultsDialogEnd - resultsDialogStart);
	if (resultsDialogBody.find("activateLspTargetWindow(targetWindow)") == std::string::npos || resultsDialogBody.find("mrActivateEditWindow(targetWindow)") != std::string::npos) {
		failureReason = "LSP result actions must activate through the Bento-aware target activator.";
		return false;
	}

	const std::size_t contextMenuStart = content.find("bool showLspContextMenuForWindow");
	const std::size_t contextMenuEnd = content.find("\nstruct ParenthesisPair", contextMenuStart);
	if (contextMenuStart == std::string::npos || contextMenuEnd == std::string::npos) {
		failureReason = "Unable to isolate showLspContextMenuForWindow for LSP Bento pane routing guard.";
		return false;
	}
	contextMenuBody = content.substr(contextMenuStart, contextMenuEnd - contextMenuStart);
	if (contextMenuBody.find("activateLspTargetWindow(targetWindow)") == std::string::npos || contextMenuBody.find("mrActivateEditWindow(targetWindow)") != std::string::npos) {
		failureReason = "LSP context menu must activate the target through the Bento-aware activator.";
		return false;
	}

	const std::size_t contextMenuItemsStart = content.find("std::vector<LspMiniMenuEntry> buildLspContextMenuItems");
	const std::size_t contextMenuItemsEnd = content.find("\nbool requestLspCodeActionsAtPosition", contextMenuItemsStart);
	if (contextMenuItemsStart == std::string::npos || contextMenuItemsEnd == std::string::npos) {
		failureReason = "Unable to isolate buildLspContextMenuItems for LSP Bento pane routing guard.";
		return false;
	}
	contextMenuItemsBody = content.substr(contextMenuItemsStart, contextMenuItemsEnd - contextMenuItemsStart);
	if (contextMenuItemsBody.find("currentDocumentPositionServiceSnapshot") != std::string::npos || contextMenuItemsBody.find("currentDocumentServiceSnapshot(document)") == std::string::npos) {
		failureReason = "LSP context menu must use a document-wide service snapshot before rendering the menu.";
		return false;
	}

	const std::size_t hoverStart = content.find("bool showLspHoverSidekick");
	const std::size_t hoverEnd = content.find("\nbool showLspSignatureHelpSidekick", hoverStart);
	if (hoverStart == std::string::npos || hoverEnd == std::string::npos) {
		failureReason = "Unable to isolate showLspHoverSidekick for LSP Bento pane routing guard.";
		return false;
	}
	hoverBody = content.substr(hoverStart, hoverEnd - hoverStart);
	if (hoverBody.find("findLspResultTargetWindow(result.header.identity)") == std::string::npos || hoverBody.find("currentEditorCommandWindow()") != std::string::npos) {
		failureReason = "LSP hover sidekick must target the result owner, not the current editor command window.";
		return false;
	}

	const std::size_t signatureStart = content.find("bool showLspSignatureHelpSidekick", hoverEnd);
	const std::size_t signatureEnd = content.find("\nMREditWindow *findLspCompletionTargetWindow", signatureStart);
	if (signatureStart == std::string::npos || signatureEnd == std::string::npos) {
		failureReason = "Unable to isolate showLspSignatureHelpSidekick for LSP Bento pane routing guard.";
		return false;
	}
	signatureBody = content.substr(signatureStart, signatureEnd - signatureStart);
	if (signatureBody.find("findLspResultTargetWindow(result.header.identity)") == std::string::npos || signatureBody.find("currentEditorCommandWindow()") != std::string::npos) {
		failureReason = "LSP signature sidekick must target the result owner, not the current editor command window.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testLspRequestVersionRoutingGuard(std::string &failureReason) {
	const std::string sessionHeaderPath = absolutePathFromCwd("app/services/MRLspServiceSession.hpp");
	const std::string sessionSourcePath = absolutePathFromCwd("app/services/MRLspServiceSession.cpp");
	std::string header;
	std::string source;
	std::string consumeBody;
	std::string clearBody;
	std::string ioError;
	std::string missingNeedle;

	if (!readTextFile(sessionHeaderPath, header, ioError) || !readTextFile(sessionSourcePath, source, ioError)) {
		failureReason = "Unable to read MRLspServiceSession files for LSP request-version routing guard: " + ioError;
		return false;
	}
	if (!containsAllSubstrings(header, {"std::size_t definitionRequestVersion = 0;", "std::size_t referencesRequestVersion = 0;", "std::size_t hoverRequestVersion = 0;", "std::size_t completionRequestVersion = 0;", "std::size_t documentHighlightRequestVersion = 0;", "std::size_t documentSymbolsRequestVersion = 0;", "std::size_t signatureHelpRequestVersion = 0;"}, missingNeedle)) {
		failureReason = "LSP service session must store per-request document versions: missing " + missingNeedle + ".";
		return false;
	}

	const std::size_t consumeStart = source.find("bool MRLspServiceSession::consumeInboundMessage");
	const std::size_t consumeEnd = source.find("\nvoid MRLspServiceSession::clearRequests", consumeStart);
	if (consumeStart == std::string::npos || consumeEnd == std::string::npos) {
		failureReason = "Unable to isolate MRLspServiceSession::consumeInboundMessage.";
		return false;
	}
	consumeBody = source.substr(consumeStart, consumeEnd - consumeStart);
	if (!containsAllSubstrings(consumeBody, {"definitionRequestVersion", "referencesRequestVersion", "hoverRequestVersion", "completionRequestVersion", "documentHighlightRequestVersion", "documentSymbolsRequestVersion", "signatureHelpRequestVersion"}, missingNeedle)) {
		failureReason = "LSP result construction must use per-request document versions: missing " + missingNeedle + ".";
		return false;
	}
	if (consumeBody.find("activeWorkspace.documents.front().documentVersion") != std::string::npos) {
		failureReason = "LSP result construction must not use the first workspace document as version anchor.";
		return false;
	}

	const std::size_t clearStart = source.find("void MRLspServiceSession::clearRequests");
	const std::size_t clearEnd = source.find("\nvoid MRLspServiceSession::clearRuntimeBinding", clearStart);
	if (clearStart == std::string::npos || clearEnd == std::string::npos) {
		failureReason = "Unable to isolate MRLspServiceSession::clearRequests.";
		return false;
	}
	clearBody = source.substr(clearStart, clearEnd - clearStart);
	if (!containsAllSubstrings(clearBody, {"definitionRequestVersion = 0;", "referencesRequestVersion = 0;", "hoverRequestVersion = 0;", "completionRequestVersion = 0;", "documentHighlightRequestVersion = 0;", "documentSymbolsRequestVersion = 0;", "signatureHelpRequestVersion = 0;"}, missingNeedle)) {
		failureReason = "LSP request version fields must be reset with requests: missing " + missingNeedle + ".";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testLspDocumentHighlightChannelGuard(std::string &failureReason) {
	const std::string adapterHeaderPath = absolutePathFromCwd("lsp/MRLspDocumentHighlight.hpp");
	const std::string adapterSourcePath = absolutePathFromCwd("lsp/MRLspDocumentHighlight.cpp");
	const std::string serviceHeaderPath = absolutePathFromCwd("app/services/MRLspServiceSession.hpp");
	const std::string serviceSourcePath = absolutePathFromCwd("app/services/MRLspServiceSession.cpp");
	const std::string resultsHeaderPath = absolutePathFromCwd("app/services/MRServiceResults.hpp");
	const std::string resultsSourcePath = absolutePathFromCwd("app/services/MRServiceResults.cpp");
	const std::string routerPath = absolutePathFromCwd("app/MRCommandRouter.cpp");
	const std::string editorHeaderPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditor.hpp");
	const std::string editorMarkersPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorMarkers.cpp");
	const std::string editorViewportPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorViewport.cpp");
	const std::string makefilePath = absolutePathFromCwd("Makefile");
	std::string adapterHeader;
	std::string adapterSource;
	std::string serviceHeader;
	std::string serviceSource;
	std::string resultsHeader;
	std::string resultsSource;
	std::string router;
	std::string editorHeader;
	std::string editorMarkers;
	std::string editorViewport;
	std::string makefile;
	std::string setterBody;
	std::string ioError;
	std::string missingNeedle;

	if (!readTextFile(adapterHeaderPath, adapterHeader, ioError) || !readTextFile(adapterSourcePath, adapterSource, ioError) || !readTextFile(serviceHeaderPath, serviceHeader, ioError) || !readTextFile(serviceSourcePath, serviceSource, ioError) ||
	    !readTextFile(resultsHeaderPath, resultsHeader, ioError) || !readTextFile(resultsSourcePath, resultsSource, ioError) || !readTextFile(routerPath, router, ioError) || !readTextFile(editorHeaderPath, editorHeader, ioError) ||
	    !readTextFile(editorMarkersPath, editorMarkers, ioError) || !readTextFile(editorViewportPath, editorViewport, ioError) || !readTextFile(makefilePath, makefile, ioError)) {
		failureReason = "Unable to read files for LSP document highlight channel guard: " + ioError;
		return false;
	}

	if (!containsAllSubstrings(adapterHeader, {"LspDocumentHighlightRequest", "LspDocumentHighlightResult", "LspDocumentHighlightAdapter"}, missingNeedle) || adapterSource.find("\"textDocument/documentHighlight\"") == std::string::npos) {
		failureReason = "LSP document highlight adapter must be a dedicated documentHighlight adapter.";
		return false;
	}
	if (!containsAllSubstrings(serviceHeader, {"DocumentHighlight", "requestDocumentHighlight", "LspDocumentHighlightAdapter documentHighlightAdapter", "LspDocumentHighlightRequest documentHighlightRequest", "documentHighlightRequestVersion"}, missingNeedle)) {
		failureReason = "LSP service session must expose a dedicated document highlight request path: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(serviceSource, {"MR_LSP_DOCUMENT_HIGHLIGHT", "requestDocumentHighlight(position, errorMessage)", "documentHighlightAdapter.consume", "putDocumentHighlights(buildServiceDocumentHighlightsFromLsp", "documentHighlightRequestVersion = 0;"}, missingNeedle)) {
		failureReason = "LSP service session must request, consume and reset document highlight results: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(resultsHeader, {"MRServiceDocumentHighlightEntry", "MRServiceDocumentHighlightResult", "putDocumentHighlights", "documentHighlightResults", "buildServiceDocumentHighlightsFromLsp"}, missingNeedle) ||
	    !containsAllSubstrings(resultsSource, {"MRServiceResultKind::DocumentHighlight", "MRServiceResultStore::putDocumentHighlights", "MRServiceResultStore::documentHighlightResults", "buildServiceDocumentHighlightsFromLsp"}, missingNeedle)) {
		failureReason = "Service results must store document highlights separately from locations and diagnostics.";
		return false;
	}
	if (!containsAllSubstrings(router, {"cmMrOtherLspDocumentHighlight", "{\"Highlight\", cmMrOtherLspDocumentHighlight", "MRLspServiceCommandId::DocumentHighlight", "applyLspDocumentHighlightRanges", "reportNewLspDocumentHighlights"}, missingNeedle)) {
		failureReason = "Command router must expose and apply LSP document highlight through the LSP mini menu: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(editorHeader, {"setLspDocumentHighlightRanges", "clearLspDocumentHighlightRanges", "mLspDocumentHighlightRanges", "lspDocumentHighlightContainsOffset"}, missingNeedle) ||
	    !containsAllSubstrings(editorViewport, {"lspDocumentHighlightContainsOffset", "documentHighlightChar", "configuredColorSlotOverride(14, highlightedAttr)"}, missingNeedle)) {
		failureReason = "MRFileEditor must render document highlights through its own transient range state: missing " + missingNeedle + ".";
		return false;
	}

	const std::size_t setterStart = editorMarkers.find("void MRFileEditor::setLspDocumentHighlightRanges");
	const std::size_t setterEnd = editorMarkers.find("\nvoid MRFileEditor::clearLspDocumentHighlightRanges", setterStart);
	if (setterStart == std::string::npos || setterEnd == std::string::npos) {
		failureReason = "Unable to isolate setLspDocumentHighlightRanges.";
		return false;
	}
	setterBody = editorMarkers.substr(setterStart, setterEnd - setterStart);
	if (setterBody.find("mFindMarkerRanges") != std::string::npos || setterBody.find("mLspDiagnosticInformationRanges") != std::string::npos) {
		failureReason = "Document highlight setter must not reuse find marker or diagnostic information storage.";
		return false;
	}

	if (!containsAllSubstrings(makefile, {"LSP_DOCUMENT_HIGHLIGHT_SOURCE", "LSP_DOCUMENT_HIGHLIGHT_OBJECT", "$(LSP_DOCUMENT_HIGHLIGHT_OBJECT)"}, missingNeedle)) {
		failureReason = "Makefile must build and link the LSP document highlight adapter: missing " + missingNeedle + ".";
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
	if (content.find("if (configuredBackupFilesSetting())") == std::string::npos || content.find("fnmerge(backupName, drive, dir, file, \".bak\");") == std::string::npos) {
		failureReason = "Backup file creation must be gated by configurable BACKUP_FILES setting.";
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

bool testAboutAnimationHarness(std::string &failureReason) {
	return mrAboutAnimationRegressionHarness(failureReason);
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
	const std::string screenPath = absolutePathFromCwd("mrmac/vm/MRVMScreen.cpp");
	const std::string editorBridgePath = absolutePathFromCwd("mrmac/vm/MRVMEditor.cpp");
	const std::string dispatchPath = absolutePathFromCwd("coprocessor/MRCoprocessorDispatch.cpp");
	const std::string appPath = absolutePathFromCwd("app/MREditorApp.cpp");
	const std::string menuBarPath = absolutePathFromCwd("ui/MRMenuBar.cpp");
	const std::string framePath = absolutePathFromCwd("ui/MRFrame.cpp");
	const std::string indicatorPath = absolutePathFromCwd("ui/MRIndicator.hpp");
	const std::string statusLinePath = absolutePathFromCwd("ui/MRStatusLine.hpp");
	std::string vmContent;
	std::string vmHeaderContent;
	std::string screenContent;
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
	if (screenContent.find("static std::atomic<std::uint64_t> g_macroScreenMutationEpoch(1);") == std::string::npos || screenContent.find("struct ScreenStateCoordinator") == std::string::npos || screenContent.find("static ScreenStateCoordinator g_screenStateCoordinator;") == std::string::npos || screenContent.find("struct UiScreenStateFacade") == std::string::npos || screenContent.find("class MacroCellGrid") == std::string::npos || screenContent.find("returnWithMacroScreenMutation(") == std::string::npos || screenContent.find("returnWithDirectScreenMutation(") == std::string::npos || screenContent.find("std::uint64_t mrvmUiScreenMutationEpoch() noexcept") == std::string::npos || screenContent.find("void mrvmUiInvalidateScreenBase() noexcept") == std::string::npos || screenContent.find("void mrvmUiTouchScreenMutationEpoch() noexcept") == std::string::npos ||
	    screenContent.find("void mrvmUiBeginMacroScreenBatch() noexcept") == std::string::npos || screenContent.find("void mrvmUiEndMacroScreenBatch() noexcept") == std::string::npos || vmContent.find("struct UiRenderFacade") == std::string::npos || vmContent.find("bool mrvmUiRenderFacadeRenderDeferredCommand(const MRMacroDeferredUiCommand &command)") == std::string::npos || editorBridgeContent.find("bool mrvmUiEraseCurrentWindow()") == std::string::npos || editorBridgeContent.find("return returnWithDirectScreenMutation(mrvmEditorEraseCurrentWindow());") == std::string::npos || vmContent.find("ok = mrvmUiEraseCurrentWindow();") == std::string::npos) {
		failureReason = "MRVM screen layer must maintain a central screen-mutation epoch coordinator and route editor bridge mutations through it.";
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
	const std::string deferredHeaderPath = absolutePathFromCwd("mrmac/vm/MRVMDeferredUi.hpp");
	const std::string deferredPath = absolutePathFromCwd("mrmac/vm/MRVMDeferredUi.cpp");
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
	const std::string editorPath = absolutePathFromCwd("mrmac/vm/MRVMEditor.cpp");
	const std::string screenPath = absolutePathFromCwd("mrmac/vm/MRVMScreen.cpp");
	const std::string dispatchPath = absolutePathFromCwd("coprocessor/MRCoprocessorDispatch.cpp");
	std::string vmContent;
	std::string editorContent;
	std::string screenContent;
	std::string dispatchContent;
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
	if (!readTextFile(dispatchPath, dispatchContent, ioError)) {
		failureReason = "Unable to read MRCoprocessorDispatch.cpp for screen facade guard: " + ioError;
		return false;
	}

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
	if (screenContent.find("struct ScreenStateCoordinator") == std::string::npos || screenContent.find("struct UiScreenStateFacade") == std::string::npos || screenContent.find("UiScreenStateFacade::noteMacroOverlayMutation") == std::string::npos || screenContent.find("UiScreenStateFacade::noteBaseMutation") == std::string::npos || screenContent.find("UiScreenStateFacade::renderBaseThenOverlayIfNeeded") == std::string::npos || screenContent.find("UiScreenStateFacade::renderOverlay") == std::string::npos) {
		failureReason = "Screen render facade must keep base/overlay generation coordination.";
		return false;
	}
	if (countSubstring(screenContent, "g_screenStateCoordinator.") != 4) {
		failureReason = "ScreenStateCoordinator must only be touched through UiScreenStateFacade.";
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
	const std::string editorPath = absolutePathFromCwd("mrmac/vm/MRVMEditor.cpp");
	const std::string screenPath = absolutePathFromCwd("mrmac/vm/MRVMScreen.cpp");
	const std::string dispatchPath = absolutePathFromCwd("coprocessor/MRCoprocessorDispatch.cpp");
	const std::string appPath = absolutePathFromCwd("app/MREditorApp.cpp");
	const std::string windowCommandsPath = absolutePathFromCwd("app/commands/MRWindowCommands.cpp");
	std::string vmContent;
	std::string editorContent;
	std::string screenContent;
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
	const std::string screenPath = absolutePathFromCwd("mrmac/vm/MRVMScreen.cpp");
	std::string screenContent;
	std::string ioError;
	std::string missingNeedle;

	if (!readTextFile(screenPath, screenContent, ioError)) {
		failureReason = "Unable to read MRVMScreen.cpp for resize/KILL_BOX reprojection guard: " + ioError;
		return false;
	}
	if (!containsAllSubstrings(screenContent, {"bool geometryResetPending = false;", "boxStack.clear();", "geometryResetPending = true;", "UiScreenStateFacade::renderBaseThenOverlayIfNeeded(*this)", "grid.geometryResetPending || UiScreenStateFacade::needsOverlayReprojection()", "geometryResetPending = false;"}, missingNeedle)) {
		failureReason = "MacroCellGrid must keep explicit geometry-reset reprojection state for resize handling: missing " + missingNeedle + ".";
		return false;
	}
	{
		const std::size_t killBoxStart = screenContent.find("bool MacroCellGrid::killBox()");
		const std::size_t killBoxEnd = screenContent.find("bool applyPutBoxProc(", killBoxStart);
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
	const std::string screenPath = absolutePathFromCwd("mrmac/vm/MRVMScreen.cpp");
	std::string screenContent;
	std::string ioError;

	if (!readTextFile(screenPath, screenContent, ioError)) {
		failureReason = "Unable to read MRVMScreen.cpp for CLEAR_SCREEN snapshot guard: " + ioError;
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
	const std::string screenPath = absolutePathFromCwd("mrmac/vm/MRVMScreen.cpp");
	std::string screenContent;
	std::string ioError;
	std::string missingNeedle;

	if (!readTextFile(screenPath, screenContent, ioError)) {
		failureReason = "Unable to read MRVMScreen.cpp for line/column overlay replay guard: " + ioError;
		return false;
	}
	if (!containsAllSubstrings(screenContent, {"static bool renderMacroLineColOverlay()", "static bool reapplyMacroLineColOverlayIfActive()", "if (!g_macroScreenLineColOverlay.haveLine && !g_macroScreenLineColOverlay.haveCol)", "return renderMacroLineColOverlay();"}, missingNeedle)) {
		failureReason = "Macro line/column overlay replay helper must remain wired through the persistent overlay state: missing " + missingNeedle + ".";
		return false;
	}
	{
		const std::size_t killBoxStart = screenContent.find("bool MacroCellGrid::killBox()");
		const std::size_t killBoxEnd = screenContent.find("bool applyPutBoxProc(", killBoxStart);
		if (killBoxStart == std::string::npos || killBoxEnd == std::string::npos || killBoxEnd <= killBoxStart) {
			failureReason = "Unable to locate MacroCellGrid::killBox() for line/column overlay replay guard.";
			return false;
		}
		const std::string killBoxBlock = screenContent.substr(killBoxStart, killBoxEnd - killBoxStart);
		if (countSubstring(killBoxBlock, "reapplyMacroLineColOverlayIfActive();") < 3) {
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
	if (content.find("configuredColorSlotOverride(slot, biosAttr)") == std::string::npos) {
		failureReason = "Marquee colors must be sourced from configuredColorSlotOverride(...).";
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

bool testCompilerProfileAutomaticSetupGuard(std::string &failureReason) {
	MRCompilerProfile profile;
	std::string errorText;

	profile.id = "AUTO_SETUP";
	profile.name = "Auto Setup";
	profile.toolchain = "CUSTOM";
	if (autoConfigureCompilerProfileFromExecutable(profile, &errorText)) {
		failureReason = "Automatic compiler setup must reject an empty executable field.";
		return false;
	}
	if (errorText != "need compiler executable for automatic setup") {
		failureReason = "Automatic compiler setup empty-executable error text changed.";
		return false;
	}
	profile.executablePath = "/no/such/compiler";
	if (autoConfigureCompilerProfileFromExecutable(profile, &errorText)) {
		failureReason = "Automatic compiler setup must reject an unprobeable executable path.";
		return false;
	}

	std::vector<std::string> executablePaths = defaultCompilerExecutablePaths();
	if (!executablePaths.empty()) {
		std::string path = executablePaths.front();
		std::size_t slash = path.find_last_of('/');

		profile.executablePath = slash == std::string::npos ? path : path.substr(slash + 1);
		profile.id = "AUTO_SETUP_SPEED";
		profile.name = "Auto Setup Speed";
		if (!autoConfigureCompilerProfileFromExecutable(profile, &errorText)) {
			failureReason = "Automatic compiler setup did not resolve a known compiler binary name: " + errorText;
			return false;
		}
		if (profile.executablePath.empty() || profile.toolchain.empty() || profile.versionText.empty() || profile.buildFlags.empty()) {
			failureReason = "Automatic compiler setup did not populate the expected compiler profile fields.";
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

	if (!readTextFile(absolutePathFromCwd("ui/MRBentoBox.cpp"), source, ioError)) {
		failureReason = "Unable to read MRBentoBox.cpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRBentoBoxDiagnostics.cpp"), diagnosticsSource, ioError)) {
		failureReason = "Unable to read MRBentoBoxDiagnostics.cpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRBentoBoxPaneWindow.cpp"), paneWindowSource, ioError)) {
		failureReason = "Unable to read MRBentoBoxPaneWindow.cpp: " + ioError;
		return false;
	}
	if (!readTextFile(absolutePathFromCwd("ui/MRBentoBoxProjection.cpp"), projectionSource, ioError)) {
		failureReason = "Unable to read MRBentoBoxProjection.cpp: " + ioError;
		return false;
	}
	source += "\n";
	source += diagnosticsSource;
	source += "\n";
	source += paneWindowSource;
	source += "\n";
	source += projectionSource;
	if (!readTextFile(absolutePathFromCwd("ui/MRBentoBox.hpp"), header, ioError)) {
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
	if (!containsAllSubstrings(source, {"evMouseDown | evMouseMove | evMouseUp | evMouseAuto | evMouseWheel | evKeyDown", "const bool mouseEvent = (event.what & (evMouseDown | evMouseMove | evMouseUp | evMouseAuto | evMouseWheel)) != 0;", "if (mouseEvent) setActivePaneForMouse(event.mouse.where);"}, missingNeedle)) {
		failureReason = "Bento mouse-wheel routing must target the pane under the pointer: missing " + missingNeedle + ".";
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
	if (!containsAllSubstrings(windowCommandsSource, {"MREditWindow *createEditorWindow(const char *title)", "win = new MRBentoBox(bounds, title, nextEditorWindowNumber(), bbmDocumentViewports);", "MREditWindow *createHelpWindow(const char *title)", "win = new MRHelpWindow(bounds, title, nextEditorWindowNumber());", "MREditWindow *createLogWindow(const char *title)", "win = new MRLogWindow(bounds, title, nextEditorWindowNumber());", "MREditWindow *createCommunicationWindow(const char *title)", "win = new MRCommunicationWindow(bounds, title, nextEditorWindowNumber());"}, missingNeedle)) {
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

	const std::uint64_t taskId = coprocessor.submit(mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FileCompare, originalDocumentId, originalBaseVersion, "file compare regression", [leftLines, rightLines, originalDocumentId, originalBaseVersion, compareDocumentId, compareBaseVersion](const mr::coprocessor::TaskInfo &task, std::stop_token stopToken) {
		mr::coprocessor::Result result;
		std::vector<mr::diff::MRDiffHunk> hunks;
		std::string errorText;

		result.task = task;
		if (!mr::diff::mrComputeMyersDiff(leftLines, rightLines, hunks, &errorText, stopToken)) {
			result.status = stopToken.stop_requested() ? mr::coprocessor::TaskStatus::Cancelled : mr::coprocessor::TaskStatus::Failed;
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

bool testFileCompareCompareNavigationHarness(std::string &failureReason) {
	const std::string originalText = "same 0\nold one\nsame 2\nsame 3\nold two\nsame 5";
	const std::string compareText = "same 0\nnew one\nsame 2\nsame 3\nnew two\nsame 5";
	const MRFileCompareStartConfiguration oldStartConfiguration = configuredFileCompareStartConfiguration();
	const bool oldCompareReadOnly = configuredFileCompareComparePanelReadOnly();
	MREditSetupSettings editSettings = configuredEditSetupSettings();
	MREditWindow originalWindow(TRect(0, 0, 80, 20), "original", 101);
	MREditWindow compareWindow(TRect(0, 0, 80, 20), "compare", 102);
	MRBentoBox bento(TRect(0, 0, 160, 40), "file compare", 103, bbmFileCompare);
	MRBentoCompareSetup setup;
	std::vector<mr::diff::MRDiffHunk> hunks;
	bool ok = true;

	editSettings.formatRuler = true;
	ScopedRegressionEditSetupSettings scopedEditSettings(editSettings);
	setConfiguredFileCompareStartConfiguration(MRFileCompareStartConfiguration::OriginalCompare, nullptr);
	setConfiguredFileCompareComparePanelReadOnly(false, nullptr);

	if (!originalWindow.replaceTextBuffer(originalText.c_str(), "original") || !compareWindow.replaceTextBuffer(compareText.c_str(), "compare")) {
		failureReason = "File compare navigation harness could not seed source editor buffers.";
		ok = false;
	}

	if (ok) {
		setup.original.window = &originalWindow;
		setup.original.bufferId = originalWindow.bufferId();
		setup.original.documentId = originalWindow.documentId();
		setup.original.version = originalWindow.documentVersion();
		setup.original.text = originalText;
		setup.compare.window = &compareWindow;
		setup.compare.bufferId = compareWindow.bufferId();
		setup.compare.documentId = compareWindow.documentId();
		setup.compare.version = compareWindow.documentVersion();
		setup.compare.text = compareText;

		hunks.push_back(mr::diff::MRDiffHunk(mr::diff::MRDiffOp::Equal, 0, 0, 1));
		hunks.push_back(mr::diff::MRDiffHunk(mr::diff::MRDiffOp::Delete, 1, 1, 1));
		hunks.push_back(mr::diff::MRDiffHunk(mr::diff::MRDiffOp::Insert, 2, 1, 1));
		hunks.push_back(mr::diff::MRDiffHunk(mr::diff::MRDiffOp::Equal, 2, 2, 2));
		hunks.push_back(mr::diff::MRDiffHunk(mr::diff::MRDiffOp::Delete, 4, 4, 1));
		hunks.push_back(mr::diff::MRDiffHunk(mr::diff::MRDiffOp::Insert, 5, 4, 1));
		hunks.push_back(mr::diff::MRDiffHunk(mr::diff::MRDiffOp::Equal, 5, 5, 1));
	}

	if (ok && !bento.initializeFileCompare(setup)) {
		failureReason = "File compare navigation harness could not initialize Bento compare.";
		ok = false;
	}

	if (ok && (!MRBentoBoxFileCompareRegressionHarness::attachSourceBuffer(bento, bprDiffOriginal, originalWindow) || !MRBentoBoxFileCompareRegressionHarness::attachSourceBuffer(bento, bprDiffCompare, compareWindow))) {
		failureReason = "File compare navigation harness could not attach source buffers.";
		ok = false;
	}

	if (ok && !MRBentoBoxFileCompareRegressionHarness::seedDiffReadyState(bento, hunks)) {
		failureReason = "File compare navigation harness did not build change groups.";
		ok = false;
	}

	if (ok) {
		if (!MRBentoBoxFileCompareRegressionHarness::activateComparePane(bento)) {
			failureReason = "File compare navigation harness could not activate the compare pane.";
			ok = false;
		}
		MRFileEditor *compareEditor = ok ? MRBentoBoxFileCompareRegressionHarness::activeEditor(bento) : nullptr;
		MRFileEditor *originalEditor = ok ? MRBentoBoxFileCompareRegressionHarness::editorForRole(bento, bprDiffOriginal) : nullptr;
		if (compareEditor == nullptr) {
			failureReason = "File compare navigation harness did not expose the compare editor.";
			ok = false;
		} else if (originalEditor == nullptr) {
			failureReason = "File compare navigation harness did not expose the original editor.";
			ok = false;
		} else {
			compareEditor->setCursorOffsetAtVisualColumn(compareEditor->bufferModel().lineStartByIndex(0), 0);
			if (!bento.navigateFileCompareChange(true)) {
				failureReason = "File compare next-diff navigation failed.";
				ok = false;
			} else {
				std::size_t cursorLine = compareEditor->lineIndexOfOffset(compareEditor->cursorOffset());
				if (cursorLine != 1) {
					failureReason = "File compare next-diff compare cursor line mismatch after first jump: expected 1, got " + std::to_string(cursorLine) + ".";
					ok = false;
				} else if (!MRBentoBoxFileCompareRegressionHarness::markedDiffLineAt(bento, bprDiffCompare, cursorLine)) {
					failureReason = "File compare next-diff cursor is not on a marked compare diff line after first jump.";
					ok = false;
				}
				const std::size_t originalCursorLine = originalEditor->lineIndexOfOffset(originalEditor->cursorOffset());
				if (ok && originalCursorLine != 1) {
					failureReason = "File compare next-diff synced original cursor line mismatch after first compare jump: expected 1, got " + std::to_string(originalCursorLine) + ".";
					ok = false;
				}
			}

			if (ok && !bento.navigateFileCompareChange(true)) {
				failureReason = "File compare second next-diff navigation failed.";
				ok = false;
			} else if (ok) {
				std::size_t cursorLine = compareEditor->lineIndexOfOffset(compareEditor->cursorOffset());
				if (cursorLine != 4) {
					failureReason = "File compare next-diff compare cursor line mismatch after second jump: expected 4, got " + std::to_string(cursorLine) + ".";
					ok = false;
				} else if (!MRBentoBoxFileCompareRegressionHarness::markedDiffLineAt(bento, bprDiffCompare, cursorLine)) {
					failureReason = "File compare next-diff cursor is not on a marked compare diff line after second jump.";
					ok = false;
				}
				const std::size_t originalCursorLine = originalEditor->lineIndexOfOffset(originalEditor->cursorOffset());
				if (ok && originalCursorLine != 4) {
					failureReason = "File compare next-diff synced original cursor line mismatch after second compare jump: expected 4, got " + std::to_string(originalCursorLine) + ".";
					ok = false;
				}
			}

			if (ok && !bento.navigateFileCompareChange(false)) {
				failureReason = "File compare previous-diff navigation failed.";
				ok = false;
			} else if (ok) {
				std::size_t cursorLine = compareEditor->lineIndexOfOffset(compareEditor->cursorOffset());
				if (cursorLine != 1) {
					failureReason = "File compare previous-diff compare cursor line mismatch: expected 1, got " + std::to_string(cursorLine) + ".";
					ok = false;
				} else if (!MRBentoBoxFileCompareRegressionHarness::markedDiffLineAt(bento, bprDiffCompare, cursorLine)) {
					failureReason = "File compare previous-diff cursor is not on a marked compare diff line.";
					ok = false;
				}
			}

			if (ok) {
				const int contextGroupIndex = MRBentoBoxFileCompareRegressionHarness::showContextAtDocumentLine(bento, bprDiffCompare, 1);
				if (contextGroupIndex != 0) {
					failureReason = "File compare compare-pane context hit-test should select first diff group, got " + std::to_string(contextGroupIndex) + ".";
					ok = false;
				}
			}
		}
	}

	bento.restoreFileCompareSources();
	setConfiguredFileCompareStartConfiguration(oldStartConfiguration, nullptr);
	setConfiguredFileCompareComparePanelReadOnly(oldCompareReadOnly, nullptr);

	if (ok) failureReason.clear();
	return ok;
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
	const std::string bentoHeaderPath = absolutePathFromCwd("ui/MRBentoBox.hpp");
	const std::string bentoProjectionPath = absolutePathFromCwd("ui/MRBentoBoxProjection.cpp");
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
	if (!containsAllSubstrings(bentoProjection, {"{bprDiffOriginal, \"Diff Original\", true}", "{bprDiffCompare, \"Diff Compare\", true}", "bentoMode == bbmFileCompare && !bentoRoleIsDiff(role)", "if (bentoMode == bbmFileCompare)", "source.role = bprDiffOriginal", "configuredFileCompareStartConfiguration()", "mr::diff::mrSplitTextLinesForDiff(fileCompareSetup.original.text, originalLines)", "payload->originalDocumentId != fileCompareSetup.original.documentId", "fileCompareSourceStillMatches(fileCompareSetup.original)", "appendDiffDisplayLine(text, lineKinds", "mrfclkMissing", "mrfclkInsert", "mrfclkOffset", "configuredFileCompareOriginalLeadingGutters()", "configuredFileCompareOriginalTrailingGutters()", "configuredFileCompareCompareLeadingGutters()", "configuredFileCompareCompareTrailingGutters()", "targetEditor->setFileCompareGutters(leadingGutters, trailingGutters)", "targetEditor->setFileCompareLineKinds(lineKinds)", "targetEditor->setMiniMapSuppressed(!miniMapConfigured)", "targetEditor->setFileCompareGutterVisible(true)", "syncFileCompareLinkedPaneFrom(activeLeafId)", "syncFileCompareLinkedPaneFrom(0)", "displayStartLine", "displayLineCount", "deletedLineCount", "insertedLineCount", "rebuildFileCompareChangeGroups();", "std::string MRBentoBox::fileCompareStatusForLeaf", "firstVisibleChange", "lastVisibleChange", "visibleDeletedLines", "visibleInsertedLines", "totalDeletedLines", "totalInsertedLines", "status += \"/\" + std::to_string(fileCompareChangeGroups.size())", "status += \" -\" + std::to_string(totalDeletedLines)", "bool MRBentoBox::applyFileCompareChange(bool originalToCompare)", "bool MRBentoBox::applyFileCompareChangeGroup(bool originalToCompare, const FileCompareChangeGroup &group)", "normalizeFileCompareHunks(originalLines, compareLines, fileCompareHunks);", "fileCompareJoinedLineRange", "fileCompareEditorLineRange", "targetEditor->replaceRangeAndSelect", "targetEditor->setSelectionOffsets(selectionEnd, selectionEnd, False)", "refreshFileCompareAfterSourceMutation();", "fileCompareHunks.clear();", "fileCompareDiffReady = false;", "kFileCompareActionApply", "apply diff", "cmMrFileComparePaneActionAccepted", "showFileCompareActionList(event.mouse.where, targetLeafId)", "fileCompareChangeGroupIndexAtLine", "fileCompareGroupNavigationLineForRole", "pendingFileCompareActionGroupIndex", "moveFileCompareEditorToGroup", "editor.moveCursorToDocumentLineTop(targetLine, 0)", "cursorGroupIndex", "targetIndex = next ?"}, missingNeedle)) {
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
	const std::string windowCommandsPath = absolutePathFromCwd("app/commands/MRWindowCommands.cpp");
	const std::string windowListPath = absolutePathFromCwd("dialogs/MRWindowList.cpp");
	const std::string editWindowPath = absolutePathFromCwd("ui/MREditWindow.hpp");
	const std::string editorAppPath = absolutePathFromCwd("app/MREditorApp.cpp");
	const std::string bentoProjectionPath = absolutePathFromCwd("ui/MRBentoBoxProjection.cpp");
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

	if (!readTextFile(windowCommandsHeaderPath, windowCommandsHeader, ioError) || !readTextFile(windowCommandsPath, windowCommands, ioError) || !readTextFile(windowListPath, windowList, ioError) || !readTextFile(editWindowPath, editWindow, ioError) || !readTextFile(editorAppPath, editorApp, ioError) || !readTextFile(bentoProjectionPath, bentoProjection, ioError) || !readTextFile(settingsStorageHeaderPath, settingsStorageHeader, ioError) || !readTextFile(settingsRuntimePath, settingsRuntime, ioError)) {
		failureReason = "Unable to read workspace autosave lazy wiring sources: " + ioError;
		return false;
	}
	if (!containsAllSubstrings(windowCommandsHeader, {"void mrMarkWorkspaceAutosaveDirty();", "void mrFlushWorkspaceAutosaveIfDue();", "void mrFlushWorkspaceAutosaveNow();"}, missingNeedle)) {
		failureReason = "Workspace autosave lazy public wiring changed: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(windowCommands, {"g_workspaceAutosaveDirty", "kWorkspaceAutosaveDelay", "configuredAutosaveWorkspace()", "setRuntimePreserveAutosavedWorkspace(false)", "runtimePreserveAutosavedWorkspace()", "persistConfiguredSettingsSnapshotWithWorkspace(&errorText, &report)", "mrLogSettingsWriteReport(\"workspace autosave\", report)"}, missingNeedle)) {
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
	if (flushBody.find("!force && std::chrono::steady_clock::now() < g_workspaceAutosaveDue") == std::string::npos) {
		failureReason = "Workspace autosave flush must support delayed idle flush and forced quit flush.";
		return false;
	}
	const std::size_t loadStart = windowCommands.find("void mrLoadWorkspace(const std::string &filename)");
	const std::size_t loadEnd = windowCommands.find("\nMREditWindow *createEditorWindow", loadStart);
	if (loadStart == std::string::npos || loadEnd == std::string::npos) {
		failureReason = "Unable to isolate mrLoadWorkspace.";
		return false;
	}
	loadWorkspaceBody = windowCommands.substr(loadStart, loadEnd - loadStart);
	if (loadWorkspaceBody.find("mrSaveWorkspace(") != std::string::npos) {
		failureReason = "Workspace load must not rewrite its source when entries cannot be restored.";
		return false;
	}
	if (!containsAllSubstrings(loadWorkspaceBody, {"parsedWorkspaceEntries", "loadedWorkspaceEntries", "setRuntimePreserveAutosavedWorkspace(true)"}, missingNeedle)) {
		failureReason = "Workspace load must preserve autosaved source when no entries restore: missing " + missingNeedle + ".";
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
	if (!containsAllSubstrings(windowList, {"void mrNotifyWindowTopologyChanged()", "mrMarkWorkspaceAutosaveDirty();"}, missingNeedle)) {
		failureReason = "Workspace topology changes must mark lazy autosave dirty: missing " + missingNeedle + ".";
		return false;
	}
	if (!containsAllSubstrings(editWindow, {"const TRect previousBounds = getBounds();", "if (previousBounds != getBounds()) mrMarkWorkspaceAutosaveDirty();"}, missingNeedle)) {
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
	if (!containsAllSubstrings(bentoProjection, {"mrMarkWorkspaceAutosaveDirty();", "setDividerPosition", "toggleLeafMaximized", "closePane"}, missingNeedle)) {
		failureReason = "Bento workspace geometry changes must mark lazy autosave dirty: missing " + missingNeedle + ".";
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
	runTest(ctx, "MRSETUP window color theme URI startup load", testMrsetupWindowColorThemeUriStartupLoad);
	runTest(ctx, "settings.mrmac auto-create on missing file", testSettingsMacroAutoCreate);
	runTest(ctx, "settings discrepancy migration behavior", testSettingsDiscrepancyMigrationGuard);
	runTest(ctx, "Edit settings roundtrip behavior", testSetupScrollRefreshGuard);
	runTest(ctx, "Extended settings roundtrip behavior", testExtendedSettingsRoundtripGuard);
	runTest(ctx, "Keymap AUTOEXEC persistence + bootstrap harness", testKeymapAutoexecPersistenceAndBootstrapHarness);
	runTest(ctx, "Keymap runtime macro dispatch harness", testKeymapMacroBindingDispatchHarness);
	runTest(ctx, "Keymap macro diagnostics harness", testKeymapMacroBindingNegativeDiagnosticsHarness);
	runTest(ctx, "Edit profile direct API validation", testEditProfileDirectApiValidationGuard);
	runTest(ctx, "Edit profile roundtrip behavior", testEditProfileRoundtripGuard);
	runTest(ctx, "Edit profile case-sensitive extension matching", testEditProfileCaseSensitiveExtensionMatchGuard);
	runTest(ctx, "Effective C profile controls loaded editor", testEffectiveCProfileControlsLoadedEditorGuard);
	runTest(ctx, "Legacy MREDITPROFILE drop-to-defaults", testLegacyEditProfileMacroDropToDefaultsGuard);
	runTest(ctx, "Edit profile case-sensitive macro roundtrip", testEditProfileCaseSensitiveMacroRoundtripGuard);
	runTest(ctx, "Edit profile duplicate exact extension rejection", testEditProfileDuplicateExactExtensionMacroGuard);
	runTest(ctx, "Edit profile descriptor conformance", testEditProfileDescriptorConformanceGuard);
	runTest(ctx, "Edit profile invalid macro rollback", testEditProfileInvalidMacroDoesNotLeaveProfileGuard);
	runTest(ctx, "Edit profile CODE_LANGUAGE raster", testEditProfileCodeLanguageRasterGuard);
	runTest(ctx, "Compiler profile automatic setup guard", testCompilerProfileAutomaticSetupGuard);
	runTest(ctx, "BentoBox foundation guard", testBentoBoxFoundationGuard);
	runTest(ctx, "Myers diff core harness", testMyersDiffCoreHarness);
	runTest(ctx, "File compare coprocessor harness", testFileCompareCoprocessorHarness);
	runTest(ctx, "File compare compare-pane navigation harness", testFileCompareCompareNavigationHarness);
	runTest(ctx, "File compare Bento wiring guard", testFileCompareBentoWiringGuard);
	runTest(ctx, "Workspace autosave lazy wiring guard", testWorkspaceAutosaveLazyWiringGuard);
	runTest(ctx, "Paths settings roundtrip behavior", testPathsBrowseEventGuard);
	runTest(ctx, "Extended base palette initialization guard", testExtendedBasePaletteInitializationGuard);
	runTest(ctx, "Color theme inventory conformance", testColorThemeInventoryConformanceGuard);
	runTest(ctx, "Color setup save-theme behavior", testColorSetupSaveThemeUsesWorkingPaletteGuard);
	runTest(ctx, "Current color theme invalid list rejection", testCurrentColorThemeInvalidListsDoNotMutateGuard);
	runTest(ctx, "WINDOWCOLORS v6 + focused pane border theme roundtrip", testWindowColorsThemeVersionAndLineNumbersRoundtrip);
	runTest(ctx, "File compare text color preserves background guard", testFileCompareTextColorPreservesBackgroundGuard);
	runTest(ctx, "Code colors preserve configured attributes", testCodeColorUsesConfiguredAttributeGuard);
	runTest(ctx, "Explicit syntax-language marker guard", testExplicitSyntaxLanguageMarkerGuard);
	runTest(ctx, "Touched-range mid-insert guard", testTouchedRangeMidInsertGuard);
	runTest(ctx, "TextDocument Piece/AddBuffer mutation harness", testTextDocumentPieceTableMutationHarness);
	runTest(ctx, "Block marking harness", testBlockMarkingHarness);
	runTest(ctx, "TRUNCATE_SPACES save-only guard", testTruncateSpacesSaveOnlyGuard);
	runTest(ctx, "EOF marker scroll range guard", testEofMarkerDoesNotExtendScrollRange);
	runTest(ctx, "Editor cursor viewport guard", testEditorCursorViewportGuard);
	runTest(ctx, "Post-EOF clear-area guard", testEofVirtualLineColorGuard);
	runTest(ctx, "Save As overwrite/backup wiring guard", testSaveAsOverwriteAndBackupWiringGuard);
	runTest(ctx, "Theme + macro save overwrite wiring guard", testThemeAndMacroSaveOverwriteWiringGuard);
	runTest(ctx, "Edit insert mode routing guard", testEditInsertModeCommandRoutingGuard);
	runTest(ctx, "LSP completion reporting reentrancy guard", testLspCompletionReportingMarksBeforeDialogGuard);
	runTest(ctx, "LSP completion insert-text guard", testLspCompletionInsertTextGuard);
	runTest(ctx, "LSP Bento pane target routing guard", testLspBentoPaneTargetRoutingGuard);
	runTest(ctx, "LSP request version routing guard", testLspRequestVersionRoutingGuard);
	runTest(ctx, "LSP document highlight channel guard", testLspDocumentHighlightChannelGuard);
	runTest(ctx, "Read-only SideKick geometry matrix", mrReadOnlySidekickGeometrySelfTestForRegression);
	runTest(ctx, "File extension right-margin sync guard", testFileExtensionRightMarginSyncGuard);
	runTest(ctx, "Search marker routing + Text menu F4 wiring guard", testSearchMarkerRoutingAndTextMenuGuard);
	runTest(ctx, "Block hotkey modifier routing guard", testBlockHotkeyModifierRoutingGuard);
	runTest(ctx, "Inter-window block source/target guard", testInterWindowBlockSourceTargetGuard);
	runTest(ctx, "About animation harness", testAboutAnimationHarness);
	runTest(ctx, "About quote README extraction guard", testAboutQuoteReadmeExtractionGuard);
	runTest(ctx, "Block paste free-cursor target guard", testBlockPasteFreeCursorTargetGuard);
	runTest(ctx, "Column indent/undent wiring guard", testColumnIndentUndentWiringGuard);
	runTest(ctx, "Tabstop + indenting operations", testTabstopIndentingOps);
	runTest(ctx, "TO/FROM header parsing + compile guards", testToFromHeaders);
	runTest(ctx, "TO/FROM runtime dispatch", testToFromDispatch);
	runTest(ctx, "KEY_IN behavior + staging guards", testKeyIn);
	runTest(ctx, "CREATE_GLOBAL_STR operation + staging guards", testCreateGlobalStrOperation);
	runTest(ctx, "Exec session staged conflict rejection guard", testExecSessionStagedConflictRejectionGuard);
	runTest(ctx, "Exec session listener fanout guard", testExecSessionListenerFanoutGuard);
	runTest(ctx, "Exec session owner cancellation guard", testExecSessionOwnerCancellationGuard);
	runTest(ctx, "Exec session status consumer guard", testExecSessionStatusConsumerGuard);
	runTest(ctx, "Runtime scheduler skip event guard", testRuntimeSchedulerSkipEventGuard);
	runTest(ctx, "Exec session K/V access boundary guard", testExecSessionKvAccessBoundaryGuard);
	runTest(ctx, "Exec session runtime store boundary guard", testExecSessionRuntimeStoreBoundaryGuard);
	runTest(ctx, "Startup CLI + recursive load wiring guard", testStartupCliLoadRecursiveGuard);
	runTest(ctx, "DELAY proc wiring guard", testDelayProcWiringGuard);
	runTest(ctx, "UI_MESSAGEBOX proc guard / legacy UI-call removal guard", testUiMessageBoxProcGuard);
	runTest(ctx, "Screen render facade boundary guard", testScreenRenderFacadeBoundaryGuard);
	runTest(ctx, "Render sink classification guard", testRenderSinkClassificationGuard);
	runTest(ctx, "Resize/KILL_BOX reprojection guard", testResizeKillBoxReprojectionGuard);
	runTest(ctx, "CLEAR_SCREEN snapshot reset guard", testClearScreenSnapshotResetGuard);
	runTest(ctx, "Line/column overlay replay guard", testLineColOverlayReplayGuard);
	runTest(ctx, "Coprocessor screen-renderer boundary guard", testCoprocessorScreenRendererBoundaryGuard);
}

void runFullSuite(TestContext &ctx) {
	runTest(ctx, "Path defaults from environment/OS", testPathDefaultsFromEnvironment);
	runTest(ctx, "MRSETUP startup-only semantics", testMrsetupStartupOnly);
	runTest(ctx, "MRSETUP window color theme URI startup load", testMrsetupWindowColorThemeUriStartupLoad);
	runTest(ctx, "settings.mrmac auto-create on missing file", testSettingsMacroAutoCreate);
	runTest(ctx, "settings discrepancy migration behavior", testSettingsDiscrepancyMigrationGuard);
	runTest(ctx, "Dialog palette guard (no 32..63 overrides)", testDialogPaletteOverridesAbsent);
	runTest(ctx, "WINDOWCOLORS targets blue window palette", testWindowColorGroupTargetsBlueWindowPalette);
	runTest(ctx, "MENUDIALOGCOLORS targets menu + gray dialog palette", testMenuDialogColorGroupTargetsExpectedSlots);
	runTest(ctx, "MENUDIALOGCOLORS legacy list upgrade behavior", testMenuDialogSemanticLabelsGuard);
	runTest(ctx, "MENUDIALOGCOLORS hotkey selection alias guard", testMenuEntryHotkeySelectionAliasGuard);
	runTest(ctx, "MENUDIALOGCOLORS dialog frame/background propagation guard", testDialogFrameAndBackgroundPropagationGuard);
	runTest(ctx, "Touched-range mid-insert guard", testTouchedRangeMidInsertGuard);
	runTest(ctx, "TextDocument Piece/AddBuffer mutation harness", testTextDocumentPieceTableMutationHarness);
	runTest(ctx, "Block marking harness", testBlockMarkingHarness);
	runTest(ctx, "Edit settings roundtrip behavior", testSetupScrollRefreshGuard);
	runTest(ctx, "Extended settings roundtrip behavior", testExtendedSettingsRoundtripGuard);
	runTest(ctx, "Keymap AUTOEXEC persistence + bootstrap harness", testKeymapAutoexecPersistenceAndBootstrapHarness);
	runTest(ctx, "Keymap runtime macro dispatch harness", testKeymapMacroBindingDispatchHarness);
	runTest(ctx, "Keymap macro diagnostics harness", testKeymapMacroBindingNegativeDiagnosticsHarness);
	runTest(ctx, "Edit profile direct API validation", testEditProfileDirectApiValidationGuard);
	runTest(ctx, "Edit profile roundtrip behavior", testEditProfileRoundtripGuard);
	runTest(ctx, "Edit profile case-sensitive extension matching", testEditProfileCaseSensitiveExtensionMatchGuard);
	runTest(ctx, "Effective C profile controls loaded editor", testEffectiveCProfileControlsLoadedEditorGuard);
	runTest(ctx, "Legacy MREDITPROFILE drop-to-defaults", testLegacyEditProfileMacroDropToDefaultsGuard);
	runTest(ctx, "Edit profile case-sensitive macro roundtrip", testEditProfileCaseSensitiveMacroRoundtripGuard);
	runTest(ctx, "Edit profile duplicate exact extension rejection", testEditProfileDuplicateExactExtensionMacroGuard);
	runTest(ctx, "Edit profile descriptor conformance", testEditProfileDescriptorConformanceGuard);
	runTest(ctx, "Edit profile invalid macro rollback", testEditProfileInvalidMacroDoesNotLeaveProfileGuard);
	runTest(ctx, "Edit profile CODE_LANGUAGE raster", testEditProfileCodeLanguageRasterGuard);
	runTest(ctx, "Compiler profile automatic setup guard", testCompilerProfileAutomaticSetupGuard);
	runTest(ctx, "BentoBox foundation guard", testBentoBoxFoundationGuard);
	runTest(ctx, "Myers diff core harness", testMyersDiffCoreHarness);
	runTest(ctx, "File compare coprocessor harness", testFileCompareCoprocessorHarness);
	runTest(ctx, "File compare compare-pane navigation harness", testFileCompareCompareNavigationHarness);
	runTest(ctx, "File compare Bento wiring guard", testFileCompareBentoWiringGuard);
	runTest(ctx, "Workspace autosave lazy wiring guard", testWorkspaceAutosaveLazyWiringGuard);
	runTest(ctx, "Paths settings roundtrip behavior", testPathsBrowseEventGuard);
	runTest(ctx, "Extended base palette initialization guard", testExtendedBasePaletteInitializationGuard);
	runTest(ctx, "Color theme inventory conformance", testColorThemeInventoryConformanceGuard);
	runTest(ctx, "Color setup save-theme behavior", testColorSetupSaveThemeUsesWorkingPaletteGuard);
	runTest(ctx, "Current color theme invalid list rejection", testCurrentColorThemeInvalidListsDoNotMutateGuard);
	runTest(ctx, "WINDOWCOLORS v6 + focused pane border theme roundtrip", testWindowColorsThemeVersionAndLineNumbersRoundtrip);
	runTest(ctx, "File compare text color preserves background guard", testFileCompareTextColorPreservesBackgroundGuard);
	runTest(ctx, "Code colors preserve configured attributes", testCodeColorUsesConfiguredAttributeGuard);
	runTest(ctx, "Explicit syntax-language marker guard", testExplicitSyntaxLanguageMarkerGuard);
	runTest(ctx, "TRUNCATE_SPACES save-only guard", testTruncateSpacesSaveOnlyGuard);
	runTest(ctx, "EOF marker scroll range guard", testEofMarkerDoesNotExtendScrollRange);
	runTest(ctx, "Indicator line-number color wiring guard", testIndicatorLineNumberColorWiringGuard);
	runTest(ctx, "Current-line color wiring guard", testCurrentLineColorWiringGuard);
	runTest(ctx, "Changed-text color wiring guard", testChangedTextColorWiringGuard);
	runTest(ctx, "Editor cursor viewport guard", testEditorCursorViewportGuard);
	runTest(ctx, "Post-EOF clear-area guard", testEofVirtualLineColorGuard);
	runTest(ctx, "Save As overwrite/backup wiring guard", testSaveAsOverwriteAndBackupWiringGuard);
	runTest(ctx, "Theme + macro save overwrite wiring guard", testThemeAndMacroSaveOverwriteWiringGuard);
	runTest(ctx, "Persistent blocks wiring guard", testPersistentBlocksWiringGuard);
	runTest(ctx, "File extension right-margin sync guard", testFileExtensionRightMarginSyncGuard);
	runTest(ctx, "Edit clipboard routing guard", testEditClipboardCommandRoutingGuard);
	runTest(ctx, "Edit insert mode routing guard", testEditInsertModeCommandRoutingGuard);
	runTest(ctx, "LSP completion reporting reentrancy guard", testLspCompletionReportingMarksBeforeDialogGuard);
	runTest(ctx, "LSP completion insert-text guard", testLspCompletionInsertTextGuard);
	runTest(ctx, "LSP Bento pane target routing guard", testLspBentoPaneTargetRoutingGuard);
	runTest(ctx, "LSP request version routing guard", testLspRequestVersionRoutingGuard);
	runTest(ctx, "LSP document highlight channel guard", testLspDocumentHighlightChannelGuard);
	runTest(ctx, "Read-only SideKick geometry matrix", mrReadOnlySidekickGeometrySelfTestForRegression);
	runTest(ctx, "Search marker routing + Text menu F4 wiring guard", testSearchMarkerRoutingAndTextMenuGuard);
	runTest(ctx, "Block hotkey modifier routing guard", testBlockHotkeyModifierRoutingGuard);
	runTest(ctx, "Inter-window block source/target guard", testInterWindowBlockSourceTargetGuard);
	runTest(ctx, "About animation harness", testAboutAnimationHarness);
	runTest(ctx, "About quote README extraction guard", testAboutQuoteReadmeExtractionGuard);
	runTest(ctx, "Block paste free-cursor target guard", testBlockPasteFreeCursorTargetGuard);
	runTest(ctx, "Column indent/undent wiring guard", testColumnIndentUndentWiringGuard);
	runTest(ctx, "Tabstop + indenting operations", testTabstopIndentingOps);
	runTest(ctx, "TO/FROM header parsing + compile guards", testToFromHeaders);
	runTest(ctx, "TO/FROM runtime dispatch", testToFromDispatch);
	runTest(ctx, "KEY_IN behavior + staging guards", testKeyIn);
	runTest(ctx, "CREATE_GLOBAL_STR operation + staging guards", testCreateGlobalStrOperation);
	runTest(ctx, "Exec session staged conflict rejection guard", testExecSessionStagedConflictRejectionGuard);
	runTest(ctx, "Exec session listener fanout guard", testExecSessionListenerFanoutGuard);
	runTest(ctx, "Exec session owner cancellation guard", testExecSessionOwnerCancellationGuard);
	runTest(ctx, "Exec session status consumer guard", testExecSessionStatusConsumerGuard);
	runTest(ctx, "Runtime scheduler skip event guard", testRuntimeSchedulerSkipEventGuard);
	runTest(ctx, "Exec session K/V access boundary guard", testExecSessionKvAccessBoundaryGuard);
	runTest(ctx, "Exec session runtime store boundary guard", testExecSessionRuntimeStoreBoundaryGuard);
	runTest(ctx, "Startup CLI + recursive load wiring guard", testStartupCliLoadRecursiveGuard);
	runTest(ctx, "MARQUEE proc wiring guard", testMarqueeProcWiringGuard);
	runTest(ctx, "Deferred UI mailbox playback guard", testDeferredUiPlaybackMailboxGuard);
	runTest(ctx, "Deferred UI mutation-epoch guard", testDeferredUiMutationEpochGuard);
	runTest(ctx, "DELAY proc wiring guard", testDelayProcWiringGuard);
	runTest(ctx, "UI_MESSAGEBOX proc guard / legacy UI-call removal guard", testUiMessageBoxProcGuard);
	runTest(ctx, "Screen render facade boundary guard", testScreenRenderFacadeBoundaryGuard);
	runTest(ctx, "Render sink classification guard", testRenderSinkClassificationGuard);
	runTest(ctx, "Resize/KILL_BOX reprojection guard", testResizeKillBoxReprojectionGuard);
	runTest(ctx, "CLEAR_SCREEN snapshot reset guard", testClearScreenSnapshotResetGuard);
	runTest(ctx, "Line/column overlay replay guard", testLineColOverlayReplayGuard);
	runTest(ctx, "Coprocessor screen-renderer boundary guard", testCoprocessorScreenRendererBoundaryGuard);
	runTest(ctx, "Marquee color source guard", testMarqueeColorSourceGuard);
	runTest(ctx, "OTHERCOLORS dedicated message slots guard", testOtherColorsDedicatedMessageSlotsGuard);
}

} // namespace

int main(int argc, char **argv) {
	bool runFull = false;

	if (argc >= 2) {
		if (argc == 3 && std::strcmp(argv[1], "--probe") == 0) {
			if (std::strcmp(argv[2], "staged-nav") == 0) return runStagedNavProbeMode();
			if (std::strcmp(argv[2], "staged-mark-page") == 0) return runStagedMarkPageProbeMode();
			if (std::strcmp(argv[2], "macro-screen-flush") == 0) return runMacroScreenFlushProbeMode();
			if (std::strcmp(argv[2], "keymap-macro-dispatch") == 0) return runKeymapMacroDispatchProbeMode();
			if (std::strcmp(argv[2], "keymap-autoexec-bootstrap") == 0) return runKeymapAutoexecBootstrapProbeMode();
		} else if (argc == 2 && std::strcmp(argv[1], "--full") == 0) {
			runFull = true;
		} else if (argc == 2 && std::strcmp(argv[1], "--core") == 0) {
			runFull = false;
		} else {
			std::cerr << "usage: regression/mr-regression-checks "
			             "[--core|--full|--probe staged-nav|staged-mark-page|macro-screen-flush|keymap-macro-dispatch|keymap-autoexec-bootstrap]\n";
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
