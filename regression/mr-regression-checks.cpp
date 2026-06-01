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
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits.h>
#include <map>
#include <regex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "../mrmac/mrmac.h"
#include "../mrmac/MRVM.hpp"
#include "../app/MREditorApp.hpp"
#include "../app/MRCommandRouter.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsCompilerProfiles.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "../dialogs/setup/MRSetup.hpp"
#include "../piecetable/MRTextDocument.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRFileEditor/MRFEBlockOps.hpp"

namespace {

struct TestContext {
	int passed;
	int failed;

	TestContext() : passed(0), failed(0) {
	}
};

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
	kMenuDialogIndexHistorySides = 26
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
	if (!setConfiguredEditSetupSettings(snapshot.editSettings, &errorText)) return false;
	if (!setConfiguredEditExtensionProfiles(snapshot.editExtensionProfiles, &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, snapshot.colorSettings.windowColors.data(), snapshot.colorSettings.windowColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, snapshot.colorSettings.menuDialogColors.data(), snapshot.colorSettings.menuDialogColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Help, snapshot.colorSettings.helpColors.data(), snapshot.colorSettings.helpColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Other, snapshot.colorSettings.otherColors.data(), snapshot.colorSettings.otherColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MiniMap, snapshot.colorSettings.miniMapColors.data(), snapshot.colorSettings.miniMapColors.size(), &errorText)) return false;
	if (!setConfiguredColorThemeFilePath(snapshot.colorThemeFilePath, &errorText)) return false;
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
	return true;
}

bool validateMrsetupColorSettings(std::string &failureReason) {
	MRColorSetupSettings colors = configuredColorSetupSettings();

	if (colors.windowColors[0] != 0x10 || colors.windowColors[1] != 0x11 || colors.windowColors[2] != 0x12 || colors.windowColors[3] != 0x13 || colors.windowColors[4] != 0x14 || colors.windowColors[5] != 0x15 || colors.windowColors[6] != 0x16 || colors.windowColors[7] != 0x17 || colors.windowColors[8] != 0x1F || colors.windowColors[9] != 0x1F) {
		failureReason = "Startup context should apply WINDOWCOLORS list (including legacy migration).";
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
	                           "MRSETUP('COLUMN_BLOCK_MOVE', 'LEAVE_SPACE');\n"
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
	if (content.find("MRSETUP('COLUMN_BLOCK_MOVE', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing COLUMN_BLOCK_MOVE.";
		return false;
	}
	if (content.find("MRSETUP('DEFAULT_MODE', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing DEFAULT_MODE.";
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

bool testWindowColorGroupTargetsBlueWindowPalette(std::string &failureReason) {
	static const unsigned char probeValues[] = {0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D};
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
		bool isExpectedSlot = (slot == 8 || slot == 9 || slot == 13 || slot == 14 || slot == kMrPaletteCurrentLine || slot == kMrPaletteCurrentLineInBlock || slot == kMrPaletteChangedText || slot == kMrPaletteLineNumbers || slot == kMrPaletteEofMarker || slot == kMrPaletteCodeFolding || slot == kMrPaletteCodeFoldingMarker || slot == kMrPaletteFormatRuler || slot == kMrPaletteFocusedPaneBorder);
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
	static const unsigned char probeValues[] = {0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B};
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

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, probeValues, sizeof(probeValues) / sizeof(probeValues[0]), &errorText)) {
		failureReason = "Unable to set MENUDIALOGCOLORS probe values: " + errorText;
		return false;
	}

	for (std::size_t i = 0; i < itemCount; ++i) {
		unsigned char slot = items[i].paletteIndex;
		bool isMenuSlot = slot >= 2 && slot <= 6;
		bool isGrayDialogSlot = slot >= 32 && slot <= 63;
		bool isExtendedDialogSlot = slot == kMrPaletteDialogInactiveElements || slot == kMrPaletteDropListDescription || slot == kMrPaletteDropListSelectedInactive;
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
	if (configured.menuDialogColors[kMenuDialogIndexInactiveControls] != defaults.menuDialogColors[kMenuDialogIndexInactiveControls] || configured.menuDialogColors[kMenuDialogIndexInactiveElements] != defaults.menuDialogColors[kMenuDialogIndexInactiveElements] || configured.menuDialogColors[kMenuDialogIndexDialogFrame] != 0x1C || configured.menuDialogColors[kMenuDialogIndexDialogText] != 0x1D || configured.menuDialogColors[kMenuDialogIndexDialogBackground] != 0x1C) {
		restore();
		failureReason = "14-entry MENUDIALOGCOLORS upgrade must inject inactive-controls default and map dialog background to legacy frame color.";
		return false;
	}

	if (!applyConfiguredColorSetupValue("MENUDIALOGCOLORS", "v1:20,21,22,23,24,25,26,27,28,29,2A", &errorText)) {
		restore();
		failureReason = "Unable to apply 11-entry legacy MENUDIALOGCOLORS list: " + errorText;
		return false;
	}
	configured = configuredColorSetupSettings();
	if (configured.menuDialogColors[kMenuDialogIndexListboxSelector] != defaults.menuDialogColors[kMenuDialogIndexListboxSelector] || configured.menuDialogColors[kMenuDialogIndexInactiveControls] != defaults.menuDialogColors[kMenuDialogIndexInactiveControls] || configured.menuDialogColors[kMenuDialogIndexInactiveElements] != defaults.menuDialogColors[kMenuDialogIndexInactiveElements] || configured.menuDialogColors[kMenuDialogIndexDialogFrame] != defaults.menuDialogColors[kMenuDialogIndexDialogFrame] || configured.menuDialogColors[kMenuDialogIndexDialogText] != defaults.menuDialogColors[kMenuDialogIndexDialogText] || configured.menuDialogColors[kMenuDialogIndexDialogBackground] != defaults.menuDialogColors[kMenuDialogIndexDialogBackground]) {
		restore();
		failureReason = "11-entry MENUDIALOGCOLORS upgrade must fill missing selector/inactive/frame/text/background defaults.";
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
	static const unsigned char probeValues[] = {0x71, 0x72, 0x7B, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7C, 0x7D, 0x7E, 0x7F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C};
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::string errorText;
	unsigned char normalHotkey = 0;
	unsigned char selectedHotkey = 0;
	bool restoreOk = true;

	auto restore = [&]() {
		if (!restoreOk) return;
		restoreOk = setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, previous.menuDialogColors.data(), previous.menuDialogColors.size(), &errorText);
	};

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, probeValues, sizeof(probeValues) / sizeof(probeValues[0]), &errorText)) {
		failureReason = "Unable to set MENUDIALOGCOLORS probe values: " + errorText;
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
	if (normalHotkey != probeValues[2] || selectedHotkey != probeValues[2]) {
		restore();
		failureReason = "Entry-hotkey and selected entry-hotkey must resolve to the same configured color.";
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

bool testWordStarBlockKeybindingsHarness(const std::string &defaultKeymapContent, std::string &failureReason) {
	ScopedRegressionKeymap restoreKeymap;
	ScopedRegressionCursorBehaviour cursorBehaviour(MRCursorBehaviour::FreeMovement);
	ScopedRegressionPersistentBlocks persistentBlocks(true);

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
			if (!sendWindowKey(window, kbRight)) return false;
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
			if (!sendWindowKey(window, kbRight)) return false;
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
	std::string ioError;

	if (!readTextFile(absolutePathFromCwd("app/MRCommandRouter.cpp"), routerContent, ioError)) {
		failureReason = "Unable to read MRCommandRouter.cpp for block marking harness: " + ioError;
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
	if (!readTextFile(absolutePathFromCwd("ui/MRFileEditor/MRFileEditor.cpp"), editorContent, ioError)) {
		failureReason = "Unable to read MRFileEditor.cpp for block marking harness: " + ioError;
		return false;
	}
	if (!mrfeBlockOpsRegressionHarness(failureReason)) return false;
	if (!testBlockMarkingWindowInputHarness(failureReason)) return false;
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
		failureReason = "COLUMN_BLOCK_MOVE mismatch after roundtrip.";
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

bool testColorSetupSaveThemeUsesWorkingPaletteGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	const std::string root = "/tmp/mr_regression_color_save_theme_" + std::to_string(static_cast<long>(::getpid()));
	const std::string settingsPath = root + "/cfg/settings.mrmac";
	const std::string themePath = root + "/cfg/probe-theme.mrmac";
	static const MRColorSetupGroup groups[] = {MRColorSetupGroup::Window, MRColorSetupGroup::MenuDialog, MRColorSetupGroup::Help, MRColorSetupGroup::Other, MRColorSetupGroup::MiniMap};
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
	if (content.find("WINDOWCOLORS('") == std::string::npos || content.find("MENUDIALOGCOLORS('") == std::string::npos || content.find("HELPCOLORS('") == std::string::npos || content.find("OTHERCOLORS('") == std::string::npos || content.find("MINIMAPCOLORS('") == std::string::npos) {
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
					case MRColorSetupGroup::Code:
						// Code colors are intentionally outside this guard's scope.
						actual = expected;
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

bool testWindowColorsThemeVersionAndLineNumbersRoundtrip(std::string &failureReason) {
	const std::string themePath = "/tmp/mr-windowcolors-line-numbers-theme.mrmac";
	const std::string windowColorsPrefix = "WINDOWCOLORS('v6:";
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::string previousThemePath = configuredColorThemeFilePath();
	const std::array<unsigned char, MRColorSetupSettings::kWindowCount> probeValues = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D};
	std::string errorText;
	std::string content;
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
		failureReason = "Saved theme must serialize WINDOWCOLORS using canonical v6 list format.";
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

	{
		MRColorSetupSettings loaded = configuredColorSetupSettings();
		for (std::size_t i = 0; i < probeValues.size(); ++i)
			if (loaded.windowColors[i] != probeValues[i]) {
				failureReason = "WINDOWCOLORS v5 roundtrip mismatch after theme reload.";
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

	restore();
	if (!restored) {
		failureReason = "Unable to restore WINDOWCOLORS/theme path after roundtrip probe.";
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
	const std::string palettePath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditor.cpp");
	const std::string viewportPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorViewport.cpp");
	std::string paletteContent;
	std::string viewportContent;
	std::string ioError;

	if (!readTextFile(palettePath, paletteContent, ioError)) {
		failureReason = "Unable to read MRFileEditor.cpp for current-line color wiring guard: " + ioError;
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
	const std::string editorPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditor.cpp");
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
		failureReason = "Unable to read MRFileEditor.cpp for changed-text dirty-range guard: " + ioError;
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
	const std::string sourcePath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditor.cpp");
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
		failureReason = "Unable to read MRFileEditor.cpp for cursor viewport guard: " + ioError;
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
		failureReason = "Unable to read MRFileEditorViewport.cpp for EOF virtual-line color guard: " + ioError;
		return false;
	}
	if (content.find("bool isDocumentLine = visibleLineIndex < totalLines;") == std::string::npos ||
	    content.find("formatSyntaxLine(buffer, currentLinePtr, syntaxLine, delta.x, textWidth, viewport.textLeft, isDocumentLine, drawEofMarker, drawEofMarkerAsEmoji);") ==
	        std::string::npos) {
		failureReason = "Draw path must pass document-line state into syntax line formatter.";
		return false;
	}
	if (content.find("if (!isDocumentLine)") == std::string::npos) {
		failureReason = "Virtual lines behind EOF must bypass current/changed-line color logic.";
		return false;
	}
	if (content.find("cursorPos == documentLength && lineStart == cursorPos && lineEnd == cursorPos") == std::string::npos) {
		failureReason = "EOF current-line condition must be constrained to the actual EOF line.";
		return false;
	}
	if (content.find("bool drawEofMarkerAsEmoji = drawEofMarker && editSettings.showEofMarkerEmoji;") == std::string::npos || content.find("if (!drawEmoji && configuredColorSlotOverride(kMrPaletteEofMarker, configuredMarkerColor))") == std::string::npos) {
		failureReason = "EOF marker must support emoji toggle with text-mode color override wiring.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testSaveAsOverwriteAndBackupWiringGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditor.cpp");
	const std::string viewportPath = absolutePathFromCwd("ui/MRFileEditor/MRFileEditorViewport.cpp");
	std::string content;
	std::string viewportContent;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read MRFileEditor.cpp for Save As overwrite/backup guard: " + ioError;
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

bool testEditClipboardCommandRoutingGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("app/MRCommandRouter.cpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read MRCommandRouter.cpp for clipboard routing guard: " + ioError;
		return false;
	}
	if (content.find("case cmMrEditCutToBuffer:") == std::string::npos || content.find("case cmMrEditCopyToBuffer:") == std::string::npos || content.find("case cmMrEditPasteFromBuffer:") == std::string::npos || content.find("return runDisabledBlockAction();") == std::string::npos || content.find("copyCurrentBlockToSystemClipboard(") != std::string::npos) {
		failureReason = "Block-buffer edit commands must stay on the disabled block-command surface.";
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
	if (content.find("case cmMrBlockWindowCopy:") == std::string::npos || content.find("case cmMrBlockWindowMove:") == std::string::npos || content.find("return runDisabledBlockAction();") == std::string::npos || content.find("chooseInterWindowBlockTarget(") != std::string::npos || content.find("mrvmUiCopyBlockFromWindow(") != std::string::npos || content.find("mrvmUiMoveBlockFromWindow(") != std::string::npos) {
		failureReason = "Inter-window block commands must stay disabled and must not select source/target windows.";
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

bool testColumnUndentPolicyGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("mrmac/MRVM.cpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read MRVM.cpp for column-undent policy guard: " + ioError;
		return false;
	}
	if (content.find("configuredColumnBlockMoveLeavesSpace(") != std::string::npos || content.find("shiftCurrentBlockIndent(") != std::string::npos || content.find("line.replace(start, static_cast<std::size_t>(removeCount),") != std::string::npos) {
		failureReason = "Column block indent/undent implementation must remain removed.";
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
	if (profile.procCount < 3 || profile.tvCallCount != 0) {
		failureReason = "MARQUEE probe must compile as OP_PROC (not TVCALL).";
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

bool testTvCallSurfaceGuard(std::string &failureReason) {
	const std::string vmPath = absolutePathFromCwd("mrmac/MRVM.cpp");
	const std::string deferredPath = absolutePathFromCwd("mrmac/vm/MRVMDeferredUi.cpp");
	std::string content;
	std::string deferredContent;
	std::string ioError;
	std::size_t dispatchStart = std::string::npos;
	std::size_t dispatchEnd = std::string::npos;
	std::string dispatchBlock;

	if (!readTextFile(vmPath, content, ioError)) {
		failureReason = "Unable to read MRVM.cpp for TVCALL surface guard: " + ioError;
		return false;
	}
	if (!readTextFile(deferredPath, deferredContent, ioError)) {
		failureReason = "Unable to read MRVMDeferredUi.cpp for TVCALL surface guard: " + ioError;
		return false;
	}
	dispatchStart = content.find("} else if (opcode == OP_TVCALL) {");
	if (dispatchStart == std::string::npos) {
		failureReason = "Unable to locate OP_TVCALL runtime dispatch block.";
		return false;
	}
	dispatchEnd = content.find("} else if (opcode == OP_HALT) {", dispatchStart);
	if (dispatchEnd == std::string::npos || dispatchEnd <= dispatchStart) {
		failureReason = "Unable to locate OP_TVCALL runtime dispatch block end marker.";
		return false;
	}
	dispatchBlock = content.substr(dispatchStart, dispatchEnd - dispatchStart);
	if (deferredContent.find("bool dispatchDeferredUiTvCall(") == std::string::npos || content.find("if (dispatchDeferredUiTvCall(funcNameUpper, args, deferredError))") == std::string::npos || deferredContent.find("mrvmUiRenderFacadeRenderDeferredCommand(command);") == std::string::npos || content.find("session->deferredUiCommands.push_back(command);") == std::string::npos) {
		failureReason = "TVCALL runtime dispatch must route through dispatchDeferredUiTvCall and the central deferred UI command path.";
		return false;
	}
	if (deferredContent.find("kDeferredTvCallVideoMode = \"VIDEO_MODE\"") == std::string::npos || deferredContent.find("kDeferredTvCallVideoCard = \"VIDEO_CARD\"") == std::string::npos || deferredContent.find("kDeferredTvCallToggle = \"TOGGLE\"") == std::string::npos || deferredContent.find("is not implemented.") == std::string::npos) {
		failureReason = "TVCALL runtime must keep VIDEO_MODE/VIDEO_CARD/TOGGLE explicitly unimplemented.";
		return false;
	}
	if (dispatchBlock.find("MARQUEE") != std::string::npos) {
		failureReason = "TVCALL runtime dispatch must not route MARQUEE* commands.";
		return false;
	}
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
	std::string header;
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
	verticalOrientationPos = splitRightPos != std::string::npos ? source.find("return splitLeafNode(targetLeafId, bsoVertical, spec) >= 0;", splitRightPos) : std::string::npos;
	horizontalOrientationPos = splitDownPos != std::string::npos ? source.find("return splitLeafNode(targetLeafId, bsoHorizontal, spec) >= 0;", splitDownPos) : std::string::npos;
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
	runTest(ctx, "settings.mrmac auto-create on missing file", testSettingsMacroAutoCreate);
	runTest(ctx, "settings discrepancy migration behavior", testSettingsDiscrepancyMigrationGuard);
	runTest(ctx, "Edit settings roundtrip behavior", testSetupScrollRefreshGuard);
	runTest(ctx, "Extended settings roundtrip behavior", testExtendedSettingsRoundtripGuard);
	runTest(ctx, "Edit profile direct API validation", testEditProfileDirectApiValidationGuard);
	runTest(ctx, "Edit profile roundtrip behavior", testEditProfileRoundtripGuard);
	runTest(ctx, "Edit profile case-sensitive extension matching", testEditProfileCaseSensitiveExtensionMatchGuard);
	runTest(ctx, "Legacy MREDITPROFILE drop-to-defaults", testLegacyEditProfileMacroDropToDefaultsGuard);
	runTest(ctx, "Edit profile case-sensitive macro roundtrip", testEditProfileCaseSensitiveMacroRoundtripGuard);
	runTest(ctx, "Edit profile duplicate exact extension rejection", testEditProfileDuplicateExactExtensionMacroGuard);
	runTest(ctx, "Compiler profile automatic setup guard", testCompilerProfileAutomaticSetupGuard);
	runTest(ctx, "BentoBox foundation guard", testBentoBoxFoundationGuard);
	runTest(ctx, "Paths settings roundtrip behavior", testPathsBrowseEventGuard);
	runTest(ctx, "Color setup save-theme behavior", testColorSetupSaveThemeUsesWorkingPaletteGuard);
	runTest(ctx, "WINDOWCOLORS v6 + focused pane border theme roundtrip", testWindowColorsThemeVersionAndLineNumbersRoundtrip);
	runTest(ctx, "Explicit syntax-language marker guard", testExplicitSyntaxLanguageMarkerGuard);
	runTest(ctx, "Touched-range mid-insert guard", testTouchedRangeMidInsertGuard);
	runTest(ctx, "TextDocument Piece/AddBuffer mutation harness", testTextDocumentPieceTableMutationHarness);
	runTest(ctx, "Block marking harness", testBlockMarkingHarness);
	runTest(ctx, "TRUNCATE_SPACES save-only guard", testTruncateSpacesSaveOnlyGuard);
	runTest(ctx, "Editor cursor viewport guard", testEditorCursorViewportGuard);
	runTest(ctx, "EOF virtual-line color guard", testEofVirtualLineColorGuard);
	runTest(ctx, "Save As overwrite/backup wiring guard", testSaveAsOverwriteAndBackupWiringGuard);
	runTest(ctx, "Theme + macro save overwrite wiring guard", testThemeAndMacroSaveOverwriteWiringGuard);
	runTest(ctx, "Search marker routing + Text menu F4 wiring guard", testSearchMarkerRoutingAndTextMenuGuard);
	runTest(ctx, "Block hotkey modifier routing guard", testBlockHotkeyModifierRoutingGuard);
	runTest(ctx, "Inter-window block source/target guard", testInterWindowBlockSourceTargetGuard);
	runTest(ctx, "Block paste free-cursor target guard", testBlockPasteFreeCursorTargetGuard);
	runTest(ctx, "Column UNDENT policy guard", testColumnUndentPolicyGuard);
	runTest(ctx, "Tabstop + indenting operations", testTabstopIndentingOps);
	runTest(ctx, "TO/FROM header parsing + compile guards", testToFromHeaders);
	runTest(ctx, "TO/FROM runtime dispatch", testToFromDispatch);
	runTest(ctx, "KEY_IN behavior + staging guards", testKeyIn);
	runTest(ctx, "CREATE_GLOBAL_STR operation + staging guards", testCreateGlobalStrOperation);
	runTest(ctx, "Startup CLI + recursive load wiring guard", testStartupCliLoadRecursiveGuard);
	runTest(ctx, "DELAY proc wiring guard", testDelayProcWiringGuard);
	runTest(ctx, "TVCALL surface guard (MESSAGEBOX only)", testTvCallSurfaceGuard);
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
	runTest(ctx, "Edit profile direct API validation", testEditProfileDirectApiValidationGuard);
	runTest(ctx, "Edit profile roundtrip behavior", testEditProfileRoundtripGuard);
	runTest(ctx, "Edit profile case-sensitive extension matching", testEditProfileCaseSensitiveExtensionMatchGuard);
	runTest(ctx, "Legacy MREDITPROFILE drop-to-defaults", testLegacyEditProfileMacroDropToDefaultsGuard);
	runTest(ctx, "Edit profile case-sensitive macro roundtrip", testEditProfileCaseSensitiveMacroRoundtripGuard);
	runTest(ctx, "Edit profile duplicate exact extension rejection", testEditProfileDuplicateExactExtensionMacroGuard);
	runTest(ctx, "Compiler profile automatic setup guard", testCompilerProfileAutomaticSetupGuard);
	runTest(ctx, "BentoBox foundation guard", testBentoBoxFoundationGuard);
	runTest(ctx, "Paths settings roundtrip behavior", testPathsBrowseEventGuard);
	runTest(ctx, "Color setup save-theme behavior", testColorSetupSaveThemeUsesWorkingPaletteGuard);
	runTest(ctx, "WINDOWCOLORS v6 + focused pane border theme roundtrip", testWindowColorsThemeVersionAndLineNumbersRoundtrip);
	runTest(ctx, "Explicit syntax-language marker guard", testExplicitSyntaxLanguageMarkerGuard);
	runTest(ctx, "TRUNCATE_SPACES save-only guard", testTruncateSpacesSaveOnlyGuard);
	runTest(ctx, "Indicator line-number color wiring guard", testIndicatorLineNumberColorWiringGuard);
	runTest(ctx, "Current-line color wiring guard", testCurrentLineColorWiringGuard);
	runTest(ctx, "Changed-text color wiring guard", testChangedTextColorWiringGuard);
	runTest(ctx, "Editor cursor viewport guard", testEditorCursorViewportGuard);
	runTest(ctx, "EOF virtual-line color guard", testEofVirtualLineColorGuard);
	runTest(ctx, "Save As overwrite/backup wiring guard", testSaveAsOverwriteAndBackupWiringGuard);
	runTest(ctx, "Theme + macro save overwrite wiring guard", testThemeAndMacroSaveOverwriteWiringGuard);
	runTest(ctx, "Persistent blocks wiring guard", testPersistentBlocksWiringGuard);
	runTest(ctx, "Edit clipboard routing guard", testEditClipboardCommandRoutingGuard);
	runTest(ctx, "Search marker routing + Text menu F4 wiring guard", testSearchMarkerRoutingAndTextMenuGuard);
	runTest(ctx, "Block hotkey modifier routing guard", testBlockHotkeyModifierRoutingGuard);
	runTest(ctx, "Inter-window block source/target guard", testInterWindowBlockSourceTargetGuard);
	runTest(ctx, "Block paste free-cursor target guard", testBlockPasteFreeCursorTargetGuard);
	runTest(ctx, "Column UNDENT policy guard", testColumnUndentPolicyGuard);
	runTest(ctx, "Tabstop + indenting operations", testTabstopIndentingOps);
	runTest(ctx, "TO/FROM header parsing + compile guards", testToFromHeaders);
	runTest(ctx, "TO/FROM runtime dispatch", testToFromDispatch);
	runTest(ctx, "KEY_IN behavior + staging guards", testKeyIn);
	runTest(ctx, "CREATE_GLOBAL_STR operation + staging guards", testCreateGlobalStrOperation);
	runTest(ctx, "Startup CLI + recursive load wiring guard", testStartupCliLoadRecursiveGuard);
	runTest(ctx, "MARQUEE proc wiring guard", testMarqueeProcWiringGuard);
	runTest(ctx, "Deferred UI mailbox playback guard", testDeferredUiPlaybackMailboxGuard);
	runTest(ctx, "Deferred UI mutation-epoch guard", testDeferredUiMutationEpochGuard);
	runTest(ctx, "DELAY proc wiring guard", testDelayProcWiringGuard);
	runTest(ctx, "TVCALL surface guard (MESSAGEBOX only)", testTvCallSurfaceGuard);
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
		} else if (argc == 2 && std::strcmp(argv[1], "--full") == 0) {
			runFull = true;
		} else if (argc == 2 && std::strcmp(argv[1], "--core") == 0) {
			runFull = false;
		} else {
			std::cerr << "usage: regression/mr-regression-checks "
			             "[--core|--full|--probe staged-nav|staged-mark-page|macro-screen-flush]\n";
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
