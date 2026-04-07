#define Uses_TKeys
#include <tvision/tv.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits.h>
#include <map>
#include <regex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "../mrmac/mrmac.h"
#include "../mrmac/mrvm.hpp"
#include "../app/TMREditorApp.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../dialogs/MRSetupDialogs.hpp"
#include "../piecetable/MRTextDocument.hpp"

namespace {

struct TestContext {
	int passed;
	int failed;

	TestContext() : passed(0), failed(0) {
	}
};

bool compileSource(const std::string &source, std::vector<unsigned char> &bytecode, int &entryOffset,
                   std::string &entryName, std::string &errorText) {
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

bool writeTextFile(const std::string &path, const std::string &content) {
	std::ofstream out(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out << content;
	return out.good();
}

bool containsText(const std::vector<std::string> &values, const char *needle) {
	return std::find(values.begin(), values.end(), std::string(needle)) != values.end();
}

bool checkGlobalInt(const std::map<std::string, int> &ints, const char *name, int expected,
                    std::string &failureReason) {
	std::map<std::string, int>::const_iterator it = ints.find(name);
	if (it == ints.end()) {
		failureReason = std::string("Missing global ") + name + ".";
		return false;
	}
	if (it->second != expected) {
		failureReason = std::string("Global ") + name + " mismatch: expected " +
		                std::to_string(expected) + ", got " + std::to_string(it->second) + ".";
		return false;
	}
	return true;
}

bool readTextFile(const std::string &path, std::string &content, std::string &errorReason) {
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	if (!in) {
		errorReason = "Unable to open file.";
		return false;
	}
	content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	if (!in.good() && !in.eof()) {
		errorReason = "Unable to read file.";
		return false;
	}
	errorReason.clear();
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

	if (relativePath == NULL || *relativePath == '\0')
		return std::string();
	if (getcwd(cwd, sizeof(cwd)) == NULL)
		return std::string(relativePath);
	out = cwd;
	if (!out.empty() && out.back() != '/')
		out.push_back('/');
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
	kMenuDialogIndexDialogFrame = 13,
	kMenuDialogIndexDialogText = 14,
	kMenuDialogIndexDialogBackground = 15
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
	if (!setConfiguredSettingsMacroFilePath(snapshot.settingsMacroFilePath, &errorText))
		return false;
	if (!setConfiguredMacroDirectoryPath(snapshot.macroDirectoryPath, &errorText))
		return false;
	if (!setConfiguredHelpFilePath(snapshot.helpFilePath, &errorText))
		return false;
	if (!setConfiguredTempDirectoryPath(snapshot.tempDirectoryPath, &errorText))
		return false;
	if (!setConfiguredShellExecutablePath(snapshot.shellExecutablePath, &errorText))
		return false;
	if (!setConfiguredEditSetupSettings(snapshot.editSettings, &errorText))
		return false;
	if (!setConfiguredEditExtensionProfiles(snapshot.editExtensionProfiles, &errorText))
		return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, snapshot.colorSettings.windowColors.data(),
	                                        snapshot.colorSettings.windowColors.size(), &errorText))
		return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog,
	                                        snapshot.colorSettings.menuDialogColors.data(),
	                                        snapshot.colorSettings.menuDialogColors.size(), &errorText))
		return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Help, snapshot.colorSettings.helpColors.data(),
	                                        snapshot.colorSettings.helpColors.size(), &errorText))
		return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Other, snapshot.colorSettings.otherColors.data(),
	                                        snapshot.colorSettings.otherColors.size(), &errorText))
		return false;
	if (!setConfiguredColorThemeFilePath(snapshot.colorThemeFilePath, &errorText))
		return false;
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
	std::cout << path << " profile=" << mrvmDescribeExecutionProfile(profile)
	          << " canStage=" << (canStage ? 1 : 0) << "\n";
	if (requireStageable && !canStage) {
		failureReason = path + ": staged background expected but rejected.";
		return false;
	}
	return true;
}

bool runCustomStagedProbe(const std::string &source, const std::string &documentText,
                          const std::string &fileName, std::size_t startCursorOffset,
                          std::size_t expectedCursorOffset, int expectedLine, int expectedColumn,
                          bool printText, std::string &failureReason) {
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
	std::cout << "custom profile=" << mrvmDescribeExecutionProfile(profile)
	          << " canStage=" << (mrvmCanRunStagedInBackground(profile) ? 1 : 0) << "\n";
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

	std::cout << "custom ops=" << result.transaction.operations().size()
	          << " hadError=" << (result.hadError ? 1 : 0)
	          << " applied=" << (commit.applied() ? 1 : 0)
	          << " conflicted=" << (commit.conflicted() ? 1 : 0)
	          << " cursor=" << cursorOffset
	          << " line=" << lineNumber
	          << " col=" << columnNumber
	          << " modified=" << (result.fileChanged ? 1 : 0);
	if (printText)
		std::cout << " text='" << input.document.text() << "'";
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
	static const char *const kMacros[] = {"mrmac/macros/test_cursor_ops.mrmac", "mrmac/macros/test_nav_ops.mrmac",
	                                      "mrmac/macros/test_tab_indent_ops.mrmac"};
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
	                           "MRSETUP('SHOW_EOF_MARKEREMOJI', 'false');\n"
	                           "MRSETUP('SHOW_LINE_NUMBERS', 'true');\n"
	                           "MRSETUP('LINE_NUM_ZERO_FILL', 'true');\n"
	                           "MRSETUP('COLUMN_BLOCK_MOVE', 'LEAVE_SPACE');\n"
	                           "MRSETUP('DEFAULT_MODE', 'OVERWRITE');\n"
	                           "MRSETUP('WINDOWCOLORS', 'v1:10,11,12,13,14,15,16,17');\n"
	                           "MRSETUP('MENUDIALOGCOLORS', 'v1:20,21,22,23,24,25,26,27,28,29,2A');\n"
	                           "MRSETUP('HELPCOLORS', 'v1:30,31,32,33,34,35,36,37,38');\n"
	                           "MRSETUP('OTHERCOLORS', 'v1:40,41,42,43,44,45,46');\n"
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
		vm.executeAt(bytecode.data(), bytecode.size(), static_cast<size_t>(entryOffset), std::string(), macroName,
		             true, true);
		mrvmSetStartupSettingsMode(false);

		if (firstVmError(vm.log, vmError)) {
			failureReason = "Startup context should allow MRSETUP, got: " + vmError;
			return false;
		}
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
					failureReason = "Startup context should apply SHOW_EOF_MARKEREMOJI='false'.";
					return false;
				}
				if (!settings.showLineNumbers) {
					failureReason = "Startup context should apply SHOW_LINE_NUMBERS='true'.";
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
			MRColorSetupSettings colors = configuredColorSetupSettings();

				if (colors.windowColors[0] != 0x10 || colors.windowColors[1] != 0x11 ||
				    colors.windowColors[2] != 0x12 || colors.windowColors[3] != 0x13 ||
				    colors.windowColors[4] != 0x14 || colors.windowColors[5] != 0x15 ||
				    colors.windowColors[6] != 0x16 || colors.windowColors[7] != 0x17 ||
				    colors.windowColors[8] != 0x1F) {
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
		}
			{
				std::vector<std::string> exts = configuredDefaultExtensionList();
				if (exts.size() < 2 || exts[0] != "TXT" || exts[1] != "MD") {
				failureReason = "Startup context should apply DEFAULT_EXTENSIONS='txt;md'.";
				return false;
			}
		}
	}

	{
		VirtualMachine vm;
		std::string vmError;

		mrvmSetStartupSettingsMode(false);
		vm.executeAt(bytecode.data(), bytecode.size(), static_cast<size_t>(entryOffset), std::string(), macroName,
		             true, true);
		if (!firstVmError(vm.log, vmError)) {
			failureReason = "Runtime context should reject MRSETUP, but no VM Error occurred.";
			return false;
		}
		if (vmError.find("MRSETUP is only allowed in settings.mrmac during startup.") == std::string::npos) {
			failureReason = "Runtime context produced unexpected error: " + vmError;
			return false;
		}
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
		if (hadTmpdir)
			setenv("TMPDIR", oldTmpdirValue.c_str(), 1);
		else
			unsetenv("TMPDIR");
		if (hadTemp)
			setenv("TEMP", oldTempValue.c_str(), 1);
		else
			unsetenv("TEMP");
		if (hadTmp)
			setenv("TMP", oldTmpValue.c_str(), 1);
		else
			unsetenv("TMP");
		if (hadShell)
			setenv("SHELL", oldShellValue.c_str(), 1);
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
	const std::string root = "/tmp/mr_regression_settings_bootstrap_" +
	                         std::to_string(static_cast<long>(::getpid()));
	const std::string settingsPath = root + "/cfg/mr/settings.mrmac";
	const std::string expectedSettingLine =
	    "MRSETUP('SETTINGSPATH', '" + settingsPath + "');";
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
	if (!ensureSettingsMacroFileExists(settingsPath, &failureReason))
		return false;
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
	if (content.find("MRSETUP('TRUNCATE_SPACES', 'true');") == std::string::npos &&
	    content.find("MRSETUP('TRUNCATE_SPACES', 'false');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist TRUNCATE_SPACES as true/false.";
		return false;
	}
	if (content.find("MRSETUP('TAB_EXPAND', 'true');") == std::string::npos &&
	    content.find("MRSETUP('TAB_EXPAND', 'false');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist TAB_EXPAND as true/false.";
		return false;
	}
	if (content.find("MRSETUP('TAB_SIZE', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing TAB_SIZE.";
		return false;
	}
	if (content.find("MRSETUP('BACKUP_FILES', 'true');") == std::string::npos &&
	    content.find("MRSETUP('BACKUP_FILES', 'false');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist BACKUP_FILES as true/false.";
		return false;
	}
	if (content.find("MRSETUP('SHOW_EOF_MARKER', 'true');") == std::string::npos &&
	    content.find("MRSETUP('SHOW_EOF_MARKER', 'false');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist SHOW_EOF_MARKER as true/false.";
		return false;
	}
	if (content.find("MRSETUP('SHOW_EOF_MARKEREMOJI', 'true');") == std::string::npos &&
	    content.find("MRSETUP('SHOW_EOF_MARKEREMOJI', 'false');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist SHOW_EOF_MARKEREMOJI as true/false.";
		return false;
	}
	if (content.find("MRSETUP('SHOW_LINE_NUMBERS', 'true');") == std::string::npos &&
	    content.find("MRSETUP('SHOW_LINE_NUMBERS', 'false');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist SHOW_LINE_NUMBERS as true/false.";
		return false;
	}
	if (content.find("MRSETUP('LINE_NUM_ZERO_FILL', 'true');") == std::string::npos &&
	    content.find("MRSETUP('LINE_NUM_ZERO_FILL', 'false');") == std::string::npos) {
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
	if (content.find("MRSETUP('COLORTHEMEURI', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing COLORTHEMEURI.";
		return false;
	}
	if (content.find("MRSETUP('WINDOWCOLORS', '") != std::string::npos ||
	    content.find("MRSETUP('MENUDIALOGCOLORS', '") != std::string::npos ||
	    content.find("MRSETUP('HELPCOLORS', '") != std::string::npos ||
	    content.find("MRSETUP('OTHERCOLORS', '") != std::string::npos) {
		failureReason = "settings.mrmac must not contain direct color lists after theme migration.";
		return false;
	}
	{
		static const std::regex themePattern(
		    "MRSETUP\\('COLORTHEMEURI',\\s*'((?:''|[^'])*)'\\);",
		    std::regex_constants::ECMAScript | std::regex_constants::icase);
		std::smatch match;
		std::string themePath;
		struct stat themeStat;
		if (!std::regex_search(content, match, themePattern) || match.size() < 2) {
			failureReason = "Unable to parse COLORTHEMEURI from auto-created settings.mrmac.";
			return false;
		}
		themePath = match[1].str();
		if (::stat(themePath.c_str(), &themeStat) != 0 || !S_ISREG(themeStat.st_mode)) {
			failureReason = "Auto-created COLORTHEMEURI target is missing.";
			return false;
		}
	}
	if (content.find("MRSETUP('CURSORVISIBILITY', '") != std::string::npos) {
		failureReason = "Auto-created settings.mrmac must not include deprecated CURSORVISIBILITY.";
		return false;
	}
	if (content.find("MRSETUP('PAGEBREAK', '") != std::string::npos ||
	    content.find("MRSETUP('WORDDELIMS', '") != std::string::npos ||
	    content.find("MRSETUP('DEFAULTEXTS', '") != std::string::npos ||
	    content.find("MRSETUP('TRUNCSPACES', '") != std::string::npos ||
	    content.find("MRSETUP('EOFCTRLZ', '") != std::string::npos ||
	    content.find("MRSETUP('EOFCRLF', '") != std::string::npos ||
	    content.find("MRSETUP('TABEXPAND', '") != std::string::npos ||
	    content.find("MRSETUP('TABSIZE', '") != std::string::npos ||
	    content.find("MRSETUP('BACKUPFILES', '") != std::string::npos ||
	    content.find("MRSETUP('SHOWEOFMARKER', '") != std::string::npos ||
	    content.find("MRSETUP('SHOWEOFMARKEREMOJI', '") != std::string::npos ||
	    content.find("MRSETUP('SHOWLINENUMBERS', '") != std::string::npos ||
	    content.find("MRSETUP('LINENUMZEROFILL', '") != std::string::npos ||
	    content.find("MRSETUP('PERSISTENTBLOCKS', '") != std::string::npos ||
	    content.find("MRSETUP('COLBLOCKMOVE', '") != std::string::npos ||
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
	static const ExpectedMacro expected[] = {{"Alpha", "<AltB>", MACRO_MODE_EDIT, MACRO_ATTR_TRANS},
	                                         {"Beta", "<CtrlF7>", MACRO_MODE_DOS_SHELL, MACRO_ATTR_DUMP},
	                                         {"Gamma", "<F5>", MACRO_MODE_ALL, MACRO_ATTR_PERM},
	                                         {"ShiftTab", "<ShftTAB>", MACRO_MODE_EDIT, 0},
	                                         {"Delta", "", MACRO_MODE_EDIT, 0}};

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

	if (!expectCompileError("$MACRO Bad TO <NoSuchKey>;\nEND_MACRO;\n", "Keycode not supported.", failureReason))
		return false;
	if (!expectCompileError("$MACRO Bad TO <F1> TO <F2>;\nEND_MACRO;\n", "Duplicate TO clause.", failureReason))
		return false;
	if (!expectCompileError("$MACRO Bad FROM EDIT FROM ALL;\nEND_MACRO;\n", "Duplicate FROM clause.",
	                        failureReason))
		return false;
	if (!expectCompileError("$MACRO Bad FROM INVALID;\nEND_MACRO;\n", "Mode expected.", failureReason))
		return false;

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

	if (!writeTextFile(macroPath, macroSource)) {
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

	ok = mrvmRunAssignedMacroForKey(kbAltB, 0, executedMacroName, nullptr) &&
	     executedMacroName == "HitEditOverride";
	if (!ok) {
		failureReason = "Edit-mode key dispatch failed.";
		std::remove(macroPath);
		return false;
	}
	ok = mrvmRunAssignedMacroForKey(kbShiftTab, 0, executedMacroName, nullptr) &&
	     executedMacroName == "HitShiftTab";
	if (!ok) {
		failureReason = "Shift+Tab dispatch failed.";
		std::remove(macroPath);
		return false;
	}
	ok = mrvmRunAssignedMacroForKey(kbCtrlA, 0, executedMacroName, nullptr) &&
	     executedMacroName == "HitCtrlA";
	if (!ok) {
		failureReason = "Ctrl+A dispatch failed.";
		std::remove(macroPath);
		return false;
	}
	ok = mrvmRunAssignedMacroForKey(kbAlt1, 0, executedMacroName, nullptr) &&
	     executedMacroName == "HitAlt1";
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
	const std::string root =
	    "/tmp/mr_regression_settings_migration_" + std::to_string(static_cast<long>(::getpid()));
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
	                                 "MRSETUP('SHOW_LINE_NUMBERS', 'true');\n"
	                                 "MRSETUP('LINE_NUM_ZERO_FILL', 'true');\n"
	                                 "MRSETUP('COLORTHEMEURI', '" +
	                                 legacyThemePath + "');\n"
	                                 "MRSETUP('WINDOWCOLORS', 'v1:31,32,33,34,35,36,37,38');\n"
	                                 "MRSETUP('UNKNOWNKEY', 'ignored');\n"
	                                 "END_MACRO;\n";
	std::string content;
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored)
			restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	(void)::remove(settingsPath.c_str());
	(void)::remove(legacyThemePath.c_str());

	if (!mrMigrateSettingsMacroToCurrentVersionForTesting(settingsPath, legacySource, "regression-probe",
	                                                      &errorText)) {
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
	if (content.find("MRSETUP('SHOW_LINE_NUMBERS', 'true');") == std::string::npos ||
	    content.find("MRSETUP('LINE_NUM_ZERO_FILL', 'true');") == std::string::npos ||
	    content.find("MRSETUP('TRUNCATE_SPACES', 'false');") == std::string::npos ||
	    content.find("MRSETUP('TAB_SIZE', '4');") == std::string::npos ||
	    content.find("MRSETUP('BACKUP_FILES', 'false');") == std::string::npos) {
		restore();
		failureReason = "Migrated settings.mrmac did not carry over recognized edit settings.";
		return false;
	}
	if (content.find("UNKNOWNKEY") != std::string::npos) {
		restore();
		failureReason = "Migrated settings.mrmac must not keep unknown legacy keys.";
		return false;
	}
	if (content.find("MRSETUP('PERSISTENT_BLOCKS', '") == std::string::npos ||
	    content.find("MRSETUP('DEFAULT_MODE', '") == std::string::npos) {
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
		if (!edit.showLineNumbers || !edit.lineNumZeroFill || edit.truncateSpaces ||
		    edit.tabSize != 4 || edit.backupFiles) {
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
	const std::string sourcePath = absolutePathFromCwd("app/TMREditorApp.cpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read TMREditorApp.cpp for palette guard: " + ioError;
		return false;
	}
	if (content.find("palette[32] =") != std::string::npos ||
	    content.find("palette[33] =") != std::string::npos ||
	    content.find("palette[34] =") != std::string::npos ||
	    content.find("palette[37] =") != std::string::npos ||
	    content.find("palette[38] =") != std::string::npos ||
	    content.find("palette[39] =") != std::string::npos ||
	    content.find("palette[40] =") != std::string::npos ||
	    content.find("palette[41] =") != std::string::npos ||
	    content.find("palette[42] =") != std::string::npos ||
	    content.find("palette[43] =") != std::string::npos ||
	    content.find("palette[44] =") != std::string::npos ||
	    content.find("palette[45] =") != std::string::npos ||
	    content.find("palette[46] =") != std::string::npos ||
	    content.find("palette[47] =") != std::string::npos ||
	    content.find("palette[48] =") != std::string::npos ||
	    content.find("palette[49] =") != std::string::npos ||
	    content.find("palette[50] =") != std::string::npos ||
	    content.find("palette[51] =") != std::string::npos ||
	    content.find("palette[52] =") != std::string::npos ||
	    content.find("palette[53] =") != std::string::npos ||
	    content.find("palette[54] =") != std::string::npos ||
	    content.find("palette[57] =") != std::string::npos ||
	    content.find("palette[58] =") != std::string::npos ||
	    content.find("palette[59] =") != std::string::npos ||
	    content.find("palette[60] =") != std::string::npos ||
	    content.find("palette[61] =") != std::string::npos ||
	    content.find("palette[62] =") != std::string::npos ||
	    content.find("palette[63] =") != std::string::npos) {
		failureReason =
		    "TMREditorApp.cpp must not hardcode dialog colors outside global scrollbar synchronization.";
		return false;
	}
	if (content.find("static const TPalette basePalette(cpAppColor, sizeof(cpAppColor) - 1);") ==
	        std::string::npos &&
	    content.find("static const TPalette &basePalette = extendedAppBasePalette();") == std::string::npos) {
		failureReason =
		    "TMREditorApp::getPalette must use a stable base palette source before applying overrides.";
		return false;
	}
	if (content.find("palette = basePalette;") == std::string::npos ||
	    content.find("slot <= kMrPaletteMax") == std::string::npos) {
		failureReason =
		    "TMREditorApp::getPalette must rebuild each call and include extension slots up to kMrPaletteMax.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testWindowColorGroupTargetsBlueWindowPalette(std::string &failureReason) {
	static const unsigned char probeValues[] = {0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59};
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::size_t itemCount = 0;
	const MRColorSetupItem *items = colorSetupGroupItems(MRColorSetupGroup::Window, itemCount);
	std::string errorText;
	unsigned char value = 0;
	bool restoreOk = true;

	auto restore = [&]() {
		if (!restoreOk)
			return;
		restoreOk = setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, previous.windowColors.data(),
		                                               previous.windowColors.size(), &errorText);
	};

	if (items == nullptr || itemCount != sizeof(probeValues) / sizeof(probeValues[0])) {
		failureReason = "Unexpected WINDOWCOLORS item mapping.";
		return false;
	}

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, probeValues,
	                                        sizeof(probeValues) / sizeof(probeValues[0]), &errorText)) {
		failureReason = "Unable to set WINDOWCOLORS probe values: " + errorText;
		return false;
	}

	for (std::size_t i = 0; i < itemCount; ++i) {
		unsigned char slot = items[i].paletteIndex;
			bool isExpectedSlot = (slot == 8 || slot == 9 || slot == 13 || slot == 14 ||
			                       slot == kMrPaletteCurrentLine || slot == kMrPaletteCurrentLineInBlock ||
			                       slot == kMrPaletteChangedText || slot == kMrPaletteLineNumbers ||
			                       slot == kMrPaletteEofMarker);
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
	static const unsigned char probeValues[] = {0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
	                                            0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C,
	                                            0x6D, 0x6E, 0x6F, 0x70};
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::size_t itemCount = 0;
	const MRColorSetupItem *items = colorSetupGroupItems(MRColorSetupGroup::MenuDialog, itemCount);
	std::string errorText;
	unsigned char value = 0;
	bool restoreOk = true;

	auto restore = [&]() {
		if (!restoreOk)
			return;
		restoreOk = setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog,
		                                               previous.menuDialogColors.data(),
		                                               previous.menuDialogColors.size(), &errorText);
	};

	if (items == nullptr || itemCount != sizeof(probeValues) / sizeof(probeValues[0])) {
		failureReason = "Unexpected MENUDIALOGCOLORS item mapping.";
		return false;
	}

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, probeValues,
	                                        sizeof(probeValues) / sizeof(probeValues[0]), &errorText)) {
		failureReason = "Unable to set MENUDIALOGCOLORS probe values: " + errorText;
		return false;
	}

	for (std::size_t i = 0; i < itemCount; ++i) {
		unsigned char slot = items[i].paletteIndex;
		bool isMenuSlot = slot >= 2 && slot <= 6;
		bool isGrayDialogSlot = slot >= 32 && slot <= 63;
		if (!configuredColorSlotOverride(slot, value)) {
			restore();
			failureReason = "MENUDIALOGCOLORS item must override its mapped palette slot.";
			return false;
		}
		if (value != probeValues[i] || (!isMenuSlot && !isGrayDialogSlot)) {
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
		if (!restored)
			restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	if (!applyConfiguredColorSetupValue("MENUDIALOGCOLORS",
	                                    "v1:10,11,12,13,14,15,16,17,18,19,1A,1B,1C,1D",
	                                    &errorText)) {
		restore();
		failureReason = "Unable to apply 14-entry legacy MENUDIALOGCOLORS list: " + errorText;
		return false;
	}
	configured = configuredColorSetupSettings();
	if (configured.menuDialogColors[kMenuDialogIndexInactiveControls] !=
	        defaults.menuDialogColors[kMenuDialogIndexInactiveControls] ||
	    configured.menuDialogColors[kMenuDialogIndexDialogFrame] != 0x1C ||
	    configured.menuDialogColors[kMenuDialogIndexDialogText] != 0x1D ||
	    configured.menuDialogColors[kMenuDialogIndexDialogBackground] != 0x1C) {
		restore();
		failureReason =
		    "14-entry MENUDIALOGCOLORS upgrade must inject inactive-controls default and map dialog background to legacy frame color.";
		return false;
	}

	if (!applyConfiguredColorSetupValue("MENUDIALOGCOLORS",
	                                    "v1:20,21,22,23,24,25,26,27,28,29,2A",
	                                    &errorText)) {
		restore();
		failureReason = "Unable to apply 11-entry legacy MENUDIALOGCOLORS list: " + errorText;
		return false;
	}
	configured = configuredColorSetupSettings();
	if (configured.menuDialogColors[kMenuDialogIndexListboxSelector] !=
	        defaults.menuDialogColors[kMenuDialogIndexListboxSelector] ||
	    configured.menuDialogColors[kMenuDialogIndexInactiveControls] !=
	        defaults.menuDialogColors[kMenuDialogIndexInactiveControls] ||
	    configured.menuDialogColors[kMenuDialogIndexDialogFrame] !=
	        defaults.menuDialogColors[kMenuDialogIndexDialogFrame] ||
	    configured.menuDialogColors[kMenuDialogIndexDialogText] != defaults.menuDialogColors[kMenuDialogIndexDialogText] ||
	    configured.menuDialogColors[kMenuDialogIndexDialogBackground] !=
	        defaults.menuDialogColors[kMenuDialogIndexDialogBackground]) {
		restore();
		failureReason =
		    "11-entry MENUDIALOGCOLORS upgrade must fill missing selector/inactive/frame/text/background defaults.";
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
	static const unsigned char probeValues[] = {0x71, 0x72, 0x7B, 0x74, 0x75, 0x76,
	                                            0x77, 0x78, 0x79, 0x7A, 0x7C, 0x7D,
	                                            0x7E, 0x7F, 0x70, 0x71};
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::string errorText;
	unsigned char normalHotkey = 0;
	unsigned char selectedHotkey = 0;
	bool restoreOk = true;

	auto restore = [&]() {
		if (!restoreOk)
			return;
		restoreOk = setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog,
		                                               previous.menuDialogColors.data(),
		                                               previous.menuDialogColors.size(), &errorText);
	};

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, probeValues,
	                                        sizeof(probeValues) / sizeof(probeValues[0]), &errorText)) {
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
		if (!restoreOk)
			return;
		restoreOk = setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog,
		                                               previous.menuDialogColors.data(),
		                                               previous.menuDialogColors.size(), &errorText);
	};

	// Set explicit probe colors for:
	// - inactive radio/checkbox controls (slot 62)
	// - dialog frame (slot 33)
	// - dialog text (slot 37)
	// - dialog background (slot 32)
	auto probe = previous.menuDialogColors;
	if (probe.size() < 16) {
		failureReason = "MENUDIALOGCOLORS must expose frame/text/background entries.";
		return false;
	}
	probe[kMenuDialogIndexInactiveControls] = 0x5B;
	probe[kMenuDialogIndexDialogFrame] = 0x4A;
	probe[kMenuDialogIndexDialogText] = 0x3C;
	probe[kMenuDialogIndexDialogBackground] = 0x2D;

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, probe.data(), probe.size(),
	                                        &errorText)) {
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

	static const unsigned char inactiveControlSlots[] = {kPaletteDialogInactiveControlsGray,
	                                                      kPaletteDialogInactiveControlsBlue,
	                                                      kPaletteDialogInactiveControlsCyan};
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

bool testSetupScrollRefreshGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	const std::string root =
	    "/tmp/mr_regression_edit_roundtrip_" + std::to_string(static_cast<long>(::getpid()));
	const std::string settingsPath = root + "/cfg/settings.mrmac";
	MREditSetupSettings probe = resolveEditSetupDefaults();
	MREditSetupSettings loaded;
	MRSetupPaths paths = resolveSetupPathDefaults();
	std::string source;
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored)
			restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	probe.pageBreak = "\\f";
	probe.wordDelimiters = "._:-";
	probe.defaultExtensions = "txt;md";
	probe.truncateSpaces = false;
	probe.eofCtrlZ = true;
	probe.eofCrLf = true;
	probe.tabExpand = false;
	probe.tabSize = 3;
	probe.backupFiles = false;
	probe.showLineNumbers = true;
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
	if (source.find("MRSETUP('TAB_SIZE', '") == std::string::npos ||
	    source.find("MRSETUP('SHOW_LINE_NUMBERS', '") == std::string::npos ||
	    source.find("MREDITPROFILE('SET', 'perl_profile', 'TAB_SIZE', '3');") == std::string::npos ||
	    source.find("MREDITPROFILE('SET', 'perl_profile', 'SHOW_LINE_NUMBERS', 'true');") == std::string::npos) {
		restore();
		failureReason = "Profile roundtrip source did not use canonical edit-setting keys.";
		return false;
	}
	if (source.find("MRSETUP('TABSIZE', '") != std::string::npos ||
	    source.find("MRSETUP('SHOWLINENUMBERS', '") != std::string::npos ||
	    source.find("MREDITPROFILE('SET', 'perl_profile', 'TABSIZE', '3');") != std::string::npos) {
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
	if (loaded.wordDelimiters != probe.wordDelimiters || loaded.defaultExtensions != "TXT;MD" ||
	    loaded.truncateSpaces != probe.truncateSpaces || loaded.eofCtrlZ != probe.eofCtrlZ ||
	    loaded.eofCrLf != probe.eofCrLf || loaded.tabExpand != probe.tabExpand ||
	    loaded.tabSize != probe.tabSize ||
	    loaded.backupFiles != probe.backupFiles ||
	    loaded.showLineNumbers != probe.showLineNumbers ||
	    loaded.lineNumZeroFill != probe.lineNumZeroFill ||
	    loaded.persistentBlocks != probe.persistentBlocks || loaded.columnBlockMove != probe.columnBlockMove ||
	    loaded.defaultMode != probe.defaultMode) {
		restore();
		failureReason = "Edit-settings roundtrip via settings.mrmac did not preserve configured values.";
		return false;
	}

	if (!restore()) {
		failureReason = "Unable to restore runtime settings after edit roundtrip probe: " + restoreError;
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
		if (!restored)
			restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
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
		if (!restored)
			restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	globalSettings.tabSize = 8;
	globalSettings.showLineNumbers = false;
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
	profile.overrides.values.showLineNumbers = true;
	profile.overrides.values.defaultMode = "overwrite";
	profile.overrides.mask = kOvTabSize | kOvShowLineNumbers | kOvDefaultMode;
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
	if (configuredEditExtensionProfiles()[0].extensions.size() != 2 ||
		configuredEditExtensionProfiles()[0].extensions[0] != "pl" ||
		configuredEditExtensionProfiles()[0].extensions[1] != "pm") {
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
	if (effective.tabSize != 3 || !effective.showLineNumbers || effective.defaultMode != "OVERWRITE") {
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
	if (fallback.tabSize != globalSettings.tabSize || fallback.showLineNumbers != globalSettings.showLineNumbers ||
		fallback.defaultMode != globalSettings.defaultMode) {
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
		if (!restored)
			restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
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

	if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>{lowerProfile, upperProfile},
	                                       &errorText)) {
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


bool testEditProfileCaseSensitiveMacroRoundtripGuard(std::string &failureReason) {
	RuntimeSettingsSnapshot snapshot = captureRuntimeSettingsSnapshot();
	MRSetupPaths paths = resolveSetupPathDefaults();
	std::string source = "$MACRO MR_SETTINGS FROM EDIT;\n"
	                     "MRSETUP('SETTINGS_VERSION', '2');\n"
	                     "MRSETUP('TAB_SIZE', '8');\n"
	                     "MREDITPROFILE('DEFINE', 'c_lower', 'Lower C', '');\n"
	                     "MREDITPROFILE('EXT', 'c_lower', 'c', '');\n"
	                     "MREDITPROFILE('SET', 'c_lower', 'TAB_SIZE', '2');\n"
	                     "MREDITPROFILE('DEFINE', 'c_upper', 'Upper C', '');\n"
	                     "MREDITPROFILE('EXT', 'c_upper', 'C', '');\n"
	                     "MREDITPROFILE('SET', 'c_upper', 'TAB_SIZE', '6');\n"
	                     "END_MACRO;\n";
	std::string errorText;
	std::string restoreError;
	bool restored = false;
	MREditSetupSettings effective;
	std::string matchedProfile;
	std::string rewritten;

	auto restore = [&]() {
		if (!restored)
			restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
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
	if (configuredEditExtensionProfiles()[0].extensions.size() != 1 ||
		configuredEditExtensionProfiles()[0].extensions[0] != "c" ||
		configuredEditExtensionProfiles()[1].extensions.size() != 1 ||
		configuredEditExtensionProfiles()[1].extensions[0] != "C") {
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
	if (rewritten.find("MREDITPROFILE('EXT', 'c_lower', 'c', '');") == std::string::npos ||
	    rewritten.find("MREDITPROFILE('EXT', 'c_upper', 'C', '');") == std::string::npos) {
		restore();
		failureReason = "Case-sensitive macro rewrite did not preserve exact extension selectors.";
		return false;
	}
	if (rewritten.find("MREDITPROFILE('SET', 'c_lower', 'TAB_SIZE', '2');") == std::string::npos ||
	    rewritten.find("MREDITPROFILE('SET', 'c_upper', 'TAB_SIZE', '6');") == std::string::npos) {
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
	std::string source = "$MACRO MR_SETTINGS FROM EDIT;\n"
	                     "MRSETUP('SETTINGS_VERSION', '2');\n"
	                     "MREDITPROFILE('DEFINE', 'c_one', 'One', '');\n"
	                     "MREDITPROFILE('EXT', 'c_one', 'c', '');\n"
	                     "MREDITPROFILE('DEFINE', 'c_two', 'Two', '');\n"
	                     "MREDITPROFILE('EXT', 'c_two', 'c', '');\n"
	                     "END_MACRO;\n";
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored)
			restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	if (mrApplySettingsSourceForTesting(source, &errorText)) {
		restore();
		failureReason = "Duplicate exact extension assignment was accepted from macro source.";
		return false;
	}
	if (errorText.find("assigned more than once: c") == std::string::npos) {
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
	const std::string root =
	    "/tmp/mr_regression_paths_roundtrip_" + std::to_string(static_cast<long>(::getpid()));
	const std::string settingsPath = root + "/cfg/settings.mrmac";
	const std::string macroPath = root + "/macros";
	const std::string tempPath = root + "/tmp";
	MRSetupPaths paths = resolveSetupPathDefaults();
	std::string content;
	std::string errorText;
	std::string restoreError;
	bool restored = false;

	auto restore = [&]() {
		if (!restored)
			restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
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
	const std::string root =
	    "/tmp/mr_regression_color_save_theme_" + std::to_string(static_cast<long>(::getpid()));
	const std::string settingsPath = root + "/cfg/settings.mrmac";
	const std::string themePath = root + "/cfg/probe-theme.mrmac";
	static const MRColorSetupGroup groups[] = {MRColorSetupGroup::Window, MRColorSetupGroup::MenuDialog,
	                                           MRColorSetupGroup::Help, MRColorSetupGroup::Other};
	TColorAttr paletteData[kMrPaletteMax];
	TPalette workingPalette(paletteData, static_cast<ushort>(kMrPaletteMax));
	std::map<unsigned char, unsigned char> expectedBySlot;
	MRSetupPaths paths = resolveSetupPathDefaults();
	std::string content;
	std::string errorText;
	std::string restoreError;
	bool restored = false;
	unsigned char slotValue = 0;
	unsigned char nextColor = 0x21;

	auto restore = [&]() {
		if (!restored)
			restored = restoreRuntimeSettingsSnapshot(snapshot, restoreError);
		return restored;
	};

	for (int i = 0; i < kMrPaletteMax; ++i)
		paletteData[i] = 0x70;

	for (MRColorSetupGroup group : groups) {
		std::size_t count = 0;
		const MRColorSetupItem *items = colorSetupGroupItems(group, count);
		if (items == nullptr || count == 0)
			continue;
		for (std::size_t i = 0; i < count; ++i) {
			workingPalette[items[i].paletteIndex] = nextColor;
			expectedBySlot[items[i].paletteIndex] = nextColor;
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
	if (content.find("MRSETUP('WINDOWCOLORS', '") == std::string::npos ||
	    content.find("MRSETUP('MENUDIALOGCOLORS', '") == std::string::npos ||
	    content.find("MRSETUP('HELPCOLORS', '") == std::string::npos ||
	    content.find("MRSETUP('OTHERCOLORS', '") == std::string::npos) {
		restore();
		failureReason = "Saved color theme must contain all color group assignments.";
		return false;
	}

	for (const auto &entry : expectedBySlot) {
		if (!configuredColorSlotOverride(entry.first, slotValue) || slotValue != entry.second) {
			restore();
			failureReason = "Color Setup save-theme did not apply the working palette before persisting.";
			return false;
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
	const std::string windowColorsPrefix = "MRSETUP('WINDOWCOLORS', 'v3:";
	MRColorSetupSettings previous = configuredColorSetupSettings();
	std::string previousThemePath = configuredColorThemeFilePath();
	const std::array<unsigned char, MRColorSetupSettings::kWindowCount> probeValues = {
	    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29};
	std::string errorText;
	std::string content;
	unsigned char slotValue = 0;
	bool restored = true;

	auto restore = [&]() {
		std::string restoreError;
		if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, previous.windowColors.data(),
		                                        previous.windowColors.size(), &restoreError))
			restored = false;
		if (!setConfiguredColorThemeFilePath(previousThemePath, &restoreError))
			restored = false;
	};

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, probeValues.data(), probeValues.size(),
	                                        &errorText)) {
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
		failureReason = "Saved theme must serialize WINDOWCOLORS using v3 list format.";
		restore();
		return false;
	}

	{
		MRColorSetupSettings defaults = resolveColorSetupDefaults();
		if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, defaults.windowColors.data(),
		                                        defaults.windowColors.size(), &errorText)) {
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
					failureReason = "WINDOWCOLORS v3 roundtrip mismatch after theme reload.";
					restore();
					return false;
				}
		}
	if (!configuredColorSlotOverride(kMrPaletteLineNumbers, slotValue) || slotValue != probeValues.back()) {
		failureReason = "Line-number palette slot must be restored from WINDOWCOLORS theme value.";
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
	const std::string indicatorPath = absolutePathFromCwd("ui/TMRIndicator.hpp");
	const std::string windowPath = absolutePathFromCwd("ui/TMREditWindow.hpp");
	std::string indicatorContent;
	std::string windowContent;
	std::string ioError;

	if (!readTextFile(indicatorPath, indicatorContent, ioError)) {
		failureReason = "Unable to read TMRIndicator.hpp for line-number wiring guard: " + ioError;
		return false;
	}
	if (!readTextFile(windowPath, windowContent, ioError)) {
		failureReason = "Unable to read TMREditWindow.hpp for line-number wiring guard: " + ioError;
		return false;
	}
	if (indicatorContent.find("cursorColor = getColor(3);") == std::string::npos ||
	    indicatorContent.find("b.moveStr(cursorX, cursorText, cursorColor);") == std::string::npos) {
		failureReason = "TMRIndicator must draw line/column text from the dedicated line-number color slot.";
		return false;
	}
	if (indicatorContent.find("TPalette palette(\"\\x02\\x03\\x0C\", 3);") == std::string::npos) {
		failureReason = "TMRIndicator palette must expose line-number color as local slot 12.";
		return false;
	}
	if (windowContent.find("kMrPaletteLineNumbers") == std::string::npos) {
		failureReason = "TMREditWindow palette must include the line-number extension slot.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testCurrentLineColorWiringGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("ui/TMRFileEditor.hpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read TMRFileEditor.hpp for current-line color wiring guard: " + ioError;
		return false;
	}
	if (content.find("TPalette palette(\"\\x06\\x07\\x09\\x0A\\x0B\\x0C\", 6);") == std::string::npos) {
		failureReason = "TMRFileEditor palette must expose current-line, changed-text and line-number slots.";
		return false;
	}
	if (content.find("basePair = getColor(0x0303);") == std::string::npos ||
	    content.find("basePair = getColor(0x0204);") == std::string::npos) {
		failureReason = "Current-line and current-line-in-block must be wired to dedicated palette pairs.";
		return false;
	}
	if (content.find("lineStart <= cursorPos && cursorPos < lineEnd") == std::string::npos) {
		failureReason = "Current-line detection must be range-based in the active render line.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testChangedTextColorWiringGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("ui/TMRFileEditor.hpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read TMRFileEditor.hpp for changed-text color wiring guard: " + ioError;
		return false;
	}
	if (content.find("TAttrPair changedPair = getColor(0x0505);") == std::string::npos ||
	    content.find("bool changedChar = !currentLine && !currentLineInBlock && isDirtyOffset(documentPos);") ==
	        std::string::npos ||
	    content.find("TAttrPair effectivePair = changedChar ? changedPair : basePair;") ==
	        std::string::npos) {
		failureReason = "Changed-text must be applied per character via dedicated dirty-range lookup.";
		return false;
	}
	if (content.find("dirtyRanges_") == std::string::npos ||
	    content.find("addDirtyRange(") == std::string::npos ||
	    content.find("isDirtyOffset(") == std::string::npos) {
		failureReason = "Changed-text wiring requires dedicated dirty-range tracking in TMRFileEditor.";
		return false;
	}
	if (content.find("remapDirtyRangesForAppliedChange(*changeSet);") == std::string::npos ||
	    content.find("void remapDirtyRangesForAppliedChange(") == std::string::npos) {
		failureReason = "Changed-text ranges must be remapped across edits to stay position-correct.";
		return false;
	}
	if (content.find("if (pos >= bufferModel_.length())") == std::string::npos) {
		failureReason = "Changed-text lookup must not clamp offsets beyond EOF into the last dirty character.";
		return false;
	}
	if (content.find("else if (changedLine)") != std::string::npos) {
		failureReason = "Changed-text must not color whole lines anymore.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testEditorCursorViewportGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("ui/TMRFileEditor.hpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read TMRFileEditor.hpp for cursor viewport guard: " + ioError;
		return false;
	}
	if (content.find("struct TextViewportGeometry") == std::string::npos ||
	    content.find("TextViewportGeometry textViewportGeometry() const noexcept") == std::string::npos ||
	    content.find("bool shouldShowEditorCursor(long long x, long long y, const TextViewportGeometry &viewport) const noexcept") ==
	        std::string::npos ||
	    content.find("const bool viewActive = (state & sfActive) != 0;") == std::string::npos ||
	    content.find("const bool viewSelected = (state & sfSelected) != 0;") == std::string::npos ||
	    content.find("if (shouldShowEditorCursor(localX, localY, viewport))") == std::string::npos) {
		failureReason = "Editor cursor visibility must be gated by active/selected state and text viewport bounds.";
		return false;
	}
	if (content.find("int column = viewport.textColumnFromLocalX(local.x);") == std::string::npos ||
	    content.find("TextViewportGeometry viewport = textViewportGeometry();") == std::string::npos) {
		failureReason = "Mouse-to-text mapping must be routed through text viewport conversion.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testEofVirtualLineColorGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("ui/TMRFileEditor.hpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read TMRFileEditor.hpp for EOF virtual-line color guard: " + ioError;
		return false;
	}
		if (content.find("bool isDocumentLine = lineIndex < totalLines;") ==
		        std::string::npos ||
		    content.find(
		        "formatSyntaxLine(buffer, linePtr, delta.x, textWidth, gutterWidth, isDocumentLine, drawEofMarker,") ==
		        std::string::npos) {
		failureReason = "Draw path must pass document-line state into syntax line formatter.";
		return false;
	}
	if (content.find("if (!isDocumentLine)") == std::string::npos) {
		failureReason = "Virtual lines behind EOF must bypass current/changed-line color logic.";
		return false;
	}
	if (content.find("cursorPos == documentLength && lineStart == cursorPos && lineEnd == cursorPos") ==
	    std::string::npos) {
		failureReason = "EOF current-line condition must be constrained to the actual EOF line.";
		return false;
	}
	if (content.find("bool drawEofMarkerAsEmoji = drawEofMarker && editSettings.showEofMarkerEmoji;") ==
	        std::string::npos ||
	    content.find("if (!drawEmoji && configuredColorSlotOverride(kMrPaletteEofMarker, configuredMarkerColor))") ==
	        std::string::npos) {
		failureReason = "EOF marker must support emoji toggle with text-mode color override wiring.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testSaveAsOverwriteAndBackupWiringGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("ui/TMRFileEditor.hpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read TMRFileEditor.hpp for Save As overwrite/backup guard: " + ioError;
		return false;
	}
	if (content.find("showUnsavedChangesDialog(\"Overwrite\", \"Target file exists. Overwrite?\",") ==
	    std::string::npos) {
		failureReason = "Save As must ask for overwrite confirmation via centralized UnsavedChanges dialog.";
		return false;
	}
	if (content.find("if (!samePath(saveName, fileName) && !confirmOverwriteForSaveAs(saveName))") ==
	    std::string::npos) {
		failureReason = "Save As must guard existing target overwrite before writing.";
		return false;
	}
	if (content.find("if (configuredBackupFilesSetting())") == std::string::npos ||
	    content.find("fnmerge(backupName, drive, dir, file, \".bak\");") == std::string::npos) {
		failureReason = "Backup file creation must be gated by configurable BACKUP_FILES setting.";
		return false;
	}

	failureReason.clear();
	return true;
}

bool testThemeAndMacroSaveOverwriteWiringGuard(std::string &failureReason) {
	const std::string setupDialogsPath = absolutePathFromCwd("dialogs/MRSetupDialogs.cpp");
	const std::string appPath = absolutePathFromCwd("app/TMREditorApp.cpp");
	std::string setupContent;
	std::string appContent;
	std::string ioError;

	if (!readTextFile(setupDialogsPath, setupContent, ioError)) {
		failureReason = "Unable to read MRSetupDialogs.cpp for theme overwrite guard: " + ioError;
		return false;
	}
	if (!readTextFile(appPath, appContent, ioError)) {
		failureReason = "Unable to read TMREditorApp.cpp for macro overwrite guard: " + ioError;
		return false;
	}
	if (setupContent.find("confirmOverwriteForPath(\"Overwrite\", \"Theme file exists. Overwrite?\", themeUri)") ==
	    std::string::npos) {
		failureReason = "Color Setup / Save Theme must ask for overwrite confirmation before writing.";
		return false;
	}
	if (setupContent.find("showUnsavedChangesDialog(primaryLabel, headline, targetPath.c_str())") ==
	    std::string::npos) {
		failureReason = "Theme overwrite confirmation must use centralized UnsavedChanges dialog.";
		return false;
	}
	if (appContent.find("confirmOverwriteForPath(\"Overwrite\", \"Macro file exists. Overwrite?\", savePath)") ==
	    std::string::npos) {
		failureReason = "Recorded macro save must ask for overwrite confirmation before writing.";
		return false;
	}
	if (appContent.find("showUnsavedChangesDialog(primaryLabel, headline, targetPath.c_str())") ==
	    std::string::npos) {
		failureReason = "Macro overwrite confirmation must use centralized UnsavedChanges dialog.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testPersistentBlocksWiringGuard(std::string &failureReason) {
	const std::string settingsPath = absolutePathFromCwd("config/MRDialogPaths.cpp");
	const std::string vmPath = absolutePathFromCwd("mrmac/mrvm.cpp");
	const std::string dialogPath = absolutePathFromCwd("dialogs/MREditProfilesDialog.cpp");
	std::string settingsContent;
	std::string vmContent;
	std::string dialogContent;
	std::string ioError;

	if (!readTextFile(settingsPath, settingsContent, ioError)) {
		failureReason = "Unable to read MRDialogPaths.cpp for persistent-blocks guard: " + ioError;
		return false;
	}
	if (!readTextFile(vmPath, vmContent, ioError)) {
		failureReason = "Unable to read mrvm.cpp for persistent-blocks guard: " + ioError;
		return false;
	}
	if (!readTextFile(dialogPath, dialogContent, ioError)) {
		failureReason = "Unable to read MREditProfilesDialog.cpp for persistent-blocks guard: " + ioError;
		return false;
	}
	if (settingsContent.find("upperKeyName == \"PERSISTENT_BLOCKS\"") ==
	        std::string::npos ||
	    settingsContent.find("MRSETUP('PERSISTENT_BLOCKS'") == std::string::npos) {
		failureReason = "Persistent blocks must be parsed and serialized via MRSETUP in MRDialogPaths.";
		return false;
	}
	if (vmContent.find("setupKey == \"PERSISTENT_BLOCKS\"") == std::string::npos) {
		failureReason = "MRVM startup whitelist must accept PERSISTENT_BLOCKS.";
		return false;
	}
	if (dialogContent.find("ersistent blocks") == std::string::npos) {
		failureReason = "Edit profiles dialog must expose a Persistent blocks option.";
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
	if (content.find("case cmMrEditCutToBuffer:") == std::string::npos ||
	    content.find("dispatchEditorClipboardCommand(cmCut, true)") == std::string::npos ||
	    content.find("case cmMrEditCopyToBuffer:") == std::string::npos ||
	    content.find("dispatchEditorClipboardCommand(cmCopy, false)") == std::string::npos ||
	    content.find("case cmMrEditPasteFromBuffer:") == std::string::npos ||
	    content.find("dispatchEditorClipboardCommand(cmPaste, true)") == std::string::npos) {
		failureReason = "Edit Cut/Copy/Paste commands must route to editor clipboard commands.";
		return false;
	}
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

	if (!writeTextFile(macroPath, source)) {
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

	ok = mrvmRunAssignedMacroForKey(kbCtrlP, 0, executedMacroName, nullptr) &&
	     executedMacroName == "KeyOnCtrlP";
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

	if (!expectCompileError("$MACRO Bad;\nKEY_IN(1);\nEND_MACRO;\n", "Type mismatch or syntax error.",
	                        failureReason)) {
		std::remove(macroPath);
		return false;
	}

	std::remove(macroPath);
	failureReason.clear();
	return true;
}

bool testMarqueeProcWiringGuard(std::string &failureReason) {
	const std::string vmPath = absolutePathFromCwd("mrmac/mrvm.cpp");
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
		failureReason = "Unable to read mrvm.cpp for MARQUEE proc guard: " + ioError;
		return false;
	}
	if (content.find("name == \"MARQUEE\" || name == \"MARQUEE_WARNING\" || name == \"MARQUEE_ERROR\"") ==
	    std::string::npos) {
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
	if (mrvmCanRunStagedInBackground(profile)) {
		failureReason = "MARQUEE proc probe must remain UI-affine (not staged-background eligible).";
		return false;
	}
	unsupported = mrvmUnsupportedStagedSymbols(profile);
	if (!containsText(unsupported, "MARQUEE") || !containsText(unsupported, "MARQUEE_WARNING") ||
	    !containsText(unsupported, "MARQUEE_ERROR")) {
		failureReason = "MARQUEE proc names must be reported as unsupported staged symbols.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testTvCallSurfaceGuard(std::string &failureReason) {
	const std::string vmPath = absolutePathFromCwd("mrmac/mrvm.cpp");
	std::string content;
	std::string ioError;
	std::size_t dispatchStart = std::string::npos;
	std::size_t dispatchEnd = std::string::npos;
	std::string dispatchBlock;
	std::regex tvcallNamePattern("funcNameUpper\\s*==\\s*\"([A-Z0-9_]+)\"");
	std::sregex_iterator it;
	std::sregex_iterator end;
	std::vector<std::string> names;

	if (!readTextFile(vmPath, content, ioError)) {
		failureReason = "Unable to read mrvm.cpp for TVCALL surface guard: " + ioError;
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
	for (it = std::sregex_iterator(dispatchBlock.begin(), dispatchBlock.end(), tvcallNamePattern); it != end; ++it)
		names.push_back((*it)[1].str());
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());

	if (names.size() != 1 || names[0] != "MESSAGEBOX") {
		std::string found;
		for (std::size_t i = 0; i < names.size(); ++i) {
			if (i != 0)
				found += ", ";
			found += names[i];
		}
		failureReason = "TVCALL runtime surface must remain MESSAGEBOX-only; found: " + found;
		return false;
	}
	if (dispatchBlock.find("MARQUEE") != std::string::npos) {
		failureReason = "TVCALL runtime dispatch must not route MARQUEE* commands.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testMarqueeColorSourceGuard(std::string &failureReason) {
	const std::string menuBarPath = absolutePathFromCwd("ui/TMRMenuBar.cpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(menuBarPath, content, ioError)) {
		failureReason = "Unable to read TMRMenuBar.cpp for marquee color source guard: " + ioError;
		return false;
	}
	if (content.find("configuredColorSlotOverride(slot, biosAttr)") == std::string::npos) {
		failureReason = "Marquee colors must be sourced from configuredColorSlotOverride(...).";
		return false;
	}
	if (content.find("getColor(0x2B2B)") != std::string::npos || content.find("getColor(0x2C2C)") != std::string::npos ||
	    content.find("getColor(0x2A2A)") != std::string::npos) {
		failureReason =
		    "Marquee colors must not use raw getColor(0x2A2A/0x2B2B/0x2C2C) view-local lookup.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testOtherColorsDedicatedMessageSlotsGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("config/MRDialogPaths.cpp");
	std::string content;
	std::string ioError;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read MRDialogPaths.cpp for OTHERCOLORS slot guard: " + ioError;
		return false;
	}
	if (content.find("{\"error message\", kMrPaletteMessageError}") == std::string::npos ||
	    content.find("{\"message\", kMrPaletteMessage}") == std::string::npos ||
	    content.find("{\"warning message\", kMrPaletteMessageWarning}") == std::string::npos) {
		failureReason = "OTHERCOLORS message entries must target dedicated extension palette slots.";
		return false;
	}
	if (content.find("{\"error message\", 42}") != std::string::npos ||
	    content.find("{\"message\", 43}") != std::string::npos ||
	    content.find("{\"warning message\", 44}") != std::string::npos) {
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
	vm.executeAt(bytecode.data(), bytecode.size(), static_cast<size_t>(entryOffset), std::string(), macroName,
	             true, true);
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

void runTest(TestContext &ctx, const char *name, bool (*fn)(std::string &)) {
	std::string failure;

	if (fn(failure)) {
		++ctx.passed;
		std::cout << "[PASS] " << name << "\n";
		return;
	}
	++ctx.failed;
	std::cout << "[FAIL] " << name << "\n";
	if (!failure.empty())
		std::cout << "       " << failure << "\n";
}

void runCoreSuite(TestContext &ctx) {
	runTest(ctx, "MRSETUP startup-only semantics", testMrsetupStartupOnly);
	runTest(ctx, "settings.mrmac auto-create on missing file", testSettingsMacroAutoCreate);
	runTest(ctx, "settings discrepancy migration behavior", testSettingsDiscrepancyMigrationGuard);
	runTest(ctx, "Edit settings roundtrip behavior", testSetupScrollRefreshGuard);
	runTest(ctx, "Edit profile direct API validation", testEditProfileDirectApiValidationGuard);
	runTest(ctx, "Edit profile roundtrip behavior", testEditProfileRoundtripGuard);
	runTest(ctx, "Edit profile case-sensitive extension matching", testEditProfileCaseSensitiveExtensionMatchGuard);
	runTest(ctx, "Edit profile case-sensitive macro roundtrip", testEditProfileCaseSensitiveMacroRoundtripGuard);
	runTest(ctx, "Edit profile duplicate exact extension rejection", testEditProfileDuplicateExactExtensionMacroGuard);
	runTest(ctx, "Paths settings roundtrip behavior", testPathsBrowseEventGuard);
	runTest(ctx, "Color setup save-theme behavior", testColorSetupSaveThemeUsesWorkingPaletteGuard);
	runTest(ctx, "WINDOWCOLORS v3 + line numbers theme roundtrip", testWindowColorsThemeVersionAndLineNumbersRoundtrip);
	runTest(ctx, "Touched-range mid-insert guard", testTouchedRangeMidInsertGuard);
	runTest(ctx, "Editor cursor viewport guard", testEditorCursorViewportGuard);
	runTest(ctx, "EOF virtual-line color guard", testEofVirtualLineColorGuard);
	runTest(ctx, "Save As overwrite/backup wiring guard", testSaveAsOverwriteAndBackupWiringGuard);
	runTest(ctx, "Theme + macro save overwrite wiring guard", testThemeAndMacroSaveOverwriteWiringGuard);
	runTest(ctx, "TO/FROM header parsing + compile guards", testToFromHeaders);
	runTest(ctx, "TO/FROM runtime dispatch", testToFromDispatch);
	runTest(ctx, "KEY_IN behavior + staging guards", testKeyIn);
	runTest(ctx, "DELAY proc wiring guard", testDelayProcWiringGuard);
	runTest(ctx, "TVCALL surface guard (MESSAGEBOX only)", testTvCallSurfaceGuard);
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
	runTest(ctx, "MENUDIALOGCOLORS dialog frame/background propagation guard",
	        testDialogFrameAndBackgroundPropagationGuard);
	runTest(ctx, "Touched-range mid-insert guard", testTouchedRangeMidInsertGuard);
	runTest(ctx, "Edit settings roundtrip behavior", testSetupScrollRefreshGuard);
	runTest(ctx, "Edit profile direct API validation", testEditProfileDirectApiValidationGuard);
	runTest(ctx, "Edit profile roundtrip behavior", testEditProfileRoundtripGuard);
	runTest(ctx, "Edit profile case-sensitive extension matching", testEditProfileCaseSensitiveExtensionMatchGuard);
	runTest(ctx, "Edit profile case-sensitive macro roundtrip", testEditProfileCaseSensitiveMacroRoundtripGuard);
	runTest(ctx, "Edit profile duplicate exact extension rejection", testEditProfileDuplicateExactExtensionMacroGuard);
	runTest(ctx, "Paths settings roundtrip behavior", testPathsBrowseEventGuard);
	runTest(ctx, "Color setup save-theme behavior", testColorSetupSaveThemeUsesWorkingPaletteGuard);
	runTest(ctx, "WINDOWCOLORS v3 + line numbers theme roundtrip", testWindowColorsThemeVersionAndLineNumbersRoundtrip);
	runTest(ctx, "Indicator line-number color wiring guard", testIndicatorLineNumberColorWiringGuard);
	runTest(ctx, "Current-line color wiring guard", testCurrentLineColorWiringGuard);
	runTest(ctx, "Changed-text color wiring guard", testChangedTextColorWiringGuard);
	runTest(ctx, "Editor cursor viewport guard", testEditorCursorViewportGuard);
	runTest(ctx, "EOF virtual-line color guard", testEofVirtualLineColorGuard);
	runTest(ctx, "Save As overwrite/backup wiring guard", testSaveAsOverwriteAndBackupWiringGuard);
	runTest(ctx, "Theme + macro save overwrite wiring guard", testThemeAndMacroSaveOverwriteWiringGuard);
	runTest(ctx, "Persistent blocks wiring guard", testPersistentBlocksWiringGuard);
	runTest(ctx, "Edit clipboard routing guard", testEditClipboardCommandRoutingGuard);
	runTest(ctx, "TO/FROM header parsing + compile guards", testToFromHeaders);
	runTest(ctx, "TO/FROM runtime dispatch", testToFromDispatch);
	runTest(ctx, "KEY_IN behavior + staging guards", testKeyIn);
	runTest(ctx, "MARQUEE proc wiring guard", testMarqueeProcWiringGuard);
	runTest(ctx, "DELAY proc wiring guard", testDelayProcWiringGuard);
	runTest(ctx, "TVCALL surface guard (MESSAGEBOX only)", testTvCallSurfaceGuard);
	runTest(ctx, "Marquee color source guard", testMarqueeColorSourceGuard);
	runTest(ctx, "OTHERCOLORS dedicated message slots guard", testOtherColorsDedicatedMessageSlotsGuard);
}

} // namespace

int main(int argc, char **argv) {
	bool runFull = false;

	if (argc >= 2) {
		if (argc == 3 && std::strcmp(argv[1], "--probe") == 0) {
			if (std::strcmp(argv[2], "staged-nav") == 0)
				return runStagedNavProbeMode();
			if (std::strcmp(argv[2], "staged-mark-page") == 0)
				return runStagedMarkPageProbeMode();
		} else if (argc == 2 && std::strcmp(argv[1], "--full") == 0) {
			runFull = true;
		} else if (argc == 2 && std::strcmp(argv[1], "--core") == 0) {
			runFull = false;
		} else {
			std::cerr << "usage: regression/mr-regression-checks "
			             "[--core|--full|--probe staged-nav|staged-mark-page]\n";
			return 2;
		}
	}

	TestContext ctx;

	if (runFull)
		runFullSuite(ctx);
	else
		runCoreSuite(ctx);

	std::cout << "\nRegression summary: " << ctx.passed << " passed, " << ctx.failed << " failed.\n";
	return ctx.failed == 0 ? 0 : 1;
}
