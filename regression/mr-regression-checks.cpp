#define Uses_TKeys
#include <tvision/tv.h>

#include <algorithm>
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
#include <unistd.h>
#include <vector>

#include "../mrmac/mrmac.h"
#include "../mrmac/mrvm.hpp"
#include "../services/MRDialogPaths.hpp"

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
	                           "MRSETUP('PAGEBREAK', '\\\\f');\n"
	                           "MRSETUP('WORDDELIMS', '._-');\n"
	                           "MRSETUP('DEFAULTEXTS', 'txt;md');\n"
	                           "MRSETUP('TRUNCSPACES', 'true');\n"
	                           "MRSETUP('EOFCTRLZ', 'false');\n"
	                           "MRSETUP('EOFCRLF', 'true');\n"
	                           "MRSETUP('TABEXPAND', 'false');\n"
	                           "MRSETUP('COLBLOCKMOVE', 'LEAVE_SPACE');\n"
	                           "MRSETUP('DEFAULTMODE', 'OVERWRITE');\n"
	                           "MRSETUP('WINDOWCOLORS', 'v1:10,11,12,13,14,15,16,17');\n"
	                           "MRSETUP('MENUDIALOGCOLORS', 'v1:20,21,22,23,24,25,26,27,28,29,2A');\n"
	                           "MRSETUP('HELPCOLORS', 'v1:30,31,32,33,34,35,36,37,38');\n"
	                           "MRSETUP('OTHERCOLORS', 'v1:40,41,42,43,44,45,46,47,48,49');\n"
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
			failureReason = "Startup context should apply PAGEBREAK='\\\\f'.";
			return false;
		}
		if (configuredTabExpandSetting()) {
			failureReason = "Startup context should apply TABEXPAND='false'.";
			return false;
		}
		if (configuredDefaultInsertMode()) {
			failureReason = "Startup context should apply DEFAULTMODE='OVERWRITE'.";
			return false;
		}
		{
			MRColorSetupSettings colors = configuredColorSetupSettings();

			if (colors.windowColors[0] != 0x10 || colors.windowColors[1] != 0x11 ||
			    colors.windowColors[2] != 0x12 || colors.windowColors[3] != 0x14 ||
			    colors.windowColors[4] != 0x15 || colors.windowColors[5] != 0x16 ||
			    colors.windowColors[6] != 0x17) {
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
			if (colors.otherColors[0] != 0x40 || colors.otherColors[9] != 0x49) {
				failureReason = "Startup context should apply OTHERCOLORS list.";
				return false;
			}
		}
			{
				std::vector<std::string> exts = configuredDefaultExtensionList();
				if (exts.size() < 2 || exts[0] != "TXT" || exts[1] != "MD") {
				failureReason = "Startup context should apply DEFAULTEXTS='txt;md'.";
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
	if (content.find("MRSETUP('PAGEBREAK', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing PAGEBREAK.";
		return false;
	}
	if (content.find("MRSETUP('WORDDELIMS', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing WORDDELIMS.";
		return false;
	}
	if (content.find("MRSETUP('DEFAULTEXTS', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing DEFAULTEXTS.";
		return false;
	}
	if (content.find("MRSETUP('TRUNCSPACES', 'true');") == std::string::npos &&
	    content.find("MRSETUP('TRUNCSPACES', 'false');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist TRUNCSPACES as true/false.";
		return false;
	}
	if (content.find("MRSETUP('TABEXPAND', 'true');") == std::string::npos &&
	    content.find("MRSETUP('TABEXPAND', 'false');") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac should persist TABEXPAND as true/false.";
		return false;
	}
	if (content.find("MRSETUP('COLBLOCKMOVE', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing COLBLOCKMOVE.";
		return false;
	}
	if (content.find("MRSETUP('DEFAULTMODE', '") == std::string::npos) {
		failureReason = "Auto-created settings.mrmac is missing DEFAULTMODE.";
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
	    content.find("slot <= kMrPaletteChangedText") == std::string::npos) {
		failureReason =
		    "TMREditorApp::getPalette must rebuild each call and include extension slots up to kMrPaletteChangedText.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testWindowColorGroupTargetsBlueWindowPalette(std::string &failureReason) {
	static const unsigned char probeValues[] = {0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57};
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
		                       slot == kMrPaletteChangedText);
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
	                                            0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C};
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
	static const char *const expectedLabels[] = {"activeentry-text", "inactiveentry-text", "entry-hotkey",
	                                             "marker-active", "marker-inactive"};
	static const unsigned char expectedSlots[] = {2, 3, 4, 5, 6};
	std::size_t itemCount = 0;
	const MRColorSetupItem *items = colorSetupGroupItems(MRColorSetupGroup::MenuDialog, itemCount);

	if (items == nullptr || itemCount < 5) {
		failureReason = "MENUDIALOGCOLORS must expose semantic menu entries (active/inactive marker+text, hotkey).";
		return false;
	}
	for (std::size_t i = 0; i < 5; ++i) {
		if (items[i].label == nullptr || std::string(items[i].label) != expectedLabels[i]) {
			failureReason = "MENUDIALOGCOLORS semantic label mismatch at index " + std::to_string(i) + ".";
			return false;
		}
		if (items[i].paletteIndex != expectedSlots[i]) {
			failureReason = "MENUDIALOGCOLORS semantic slot mismatch at index " + std::to_string(i) + ".";
			return false;
		}
	}
	failureReason.clear();
	return true;
}

bool testMenuEntryHotkeySelectionAliasGuard(std::string &failureReason) {
	static const unsigned char probeValues[] = {0x71, 0x72, 0x7B, 0x74, 0x75, 0x76,
	                                            0x77, 0x78, 0x79, 0x7A, 0x7C, 0x7D};
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

bool testSetupScrollRefreshGuard(std::string &failureReason) {
	static const char *const files[] = {"dialogs/MRSetupDialogCommon.cpp", "dialogs/MRInstallationAndSetupDialog.cpp",
	                                    "dialogs/MRColorSetupDialog.cpp", "dialogs/MREditSettingsDialog.cpp",
	                                    "dialogs/MRSetupDialogs.cpp"};
	static const char *const requiredMarker = "content_->drawView();";

	for (const char *relPath : files) {
		const std::string sourcePath = absolutePathFromCwd(relPath);
		std::string content;
		std::string ioError;

		if (!readTextFile(sourcePath, content, ioError)) {
			failureReason = std::string("Unable to read ") + relPath + " for scroll refresh guard: " + ioError;
			return false;
		}
		if (content.find("void applyScroll()") == std::string::npos ||
		    content.find(requiredMarker) == std::string::npos) {
			failureReason =
			    std::string(relPath) + " must redraw content after scroll to avoid horizontal refresh artifacts.";
			return false;
		}
	}

	failureReason.clear();
	return true;
}

bool testPathsBrowseEventGuard(std::string &failureReason) {
	const std::string sourcePath = absolutePathFromCwd("dialogs/MRSetupDialogs.cpp");
	std::string content;
	std::string ioError;
	std::size_t classPos = std::string::npos;
	std::size_t handlerPos = std::string::npos;
	std::size_t browseCasePos = std::string::npos;
	std::size_t baseCallPos = std::string::npos;

	if (!readTextFile(sourcePath, content, ioError)) {
		failureReason = "Unable to read MRSetupDialogs.cpp for path-browse guard: " + ioError;
		return false;
	}

	classPos = content.find("class TPathsSetupDialog");
	handlerPos = content.find("void handleEvent(TEvent &event) override", classPos);
	browseCasePos = content.find("case cmMrSetupPathsBrowseSettingsUri:", handlerPos);
	baseCallPos = content.find("TDialog::handleEvent(event);", handlerPos);
	if (classPos == std::string::npos || handlerPos == std::string::npos || browseCasePos == std::string::npos ||
	    baseCallPos == std::string::npos) {
		failureReason = "Unable to validate path-browse event flow markers in TPathsSetupDialog::handleEvent.";
		return false;
	}
	if (browseCasePos > baseCallPos) {
		failureReason =
		    "Path browse commands must be handled before TDialog::handleEvent to avoid losing inline-glyph clicks.";
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
	if (content.find("TPalette palette(\"\\x06\\x07\\x09\\x0A\\x0B\", 5);") == std::string::npos) {
		failureReason = "TMRFileEditor palette must expose current-line and changed-text slots.";
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
	if (content.find("else if (changedLine)") == std::string::npos ||
	    content.find("basePair = getColor(0x0505);") == std::string::npos) {
		failureReason = "Changed-text must drive background pair using dedicated editor palette slot.";
		return false;
	}
	if (content.find("dirtyRanges_") == std::string::npos ||
	    content.find("addDirtyRange(") == std::string::npos ||
	    content.find("intersectsDirtyRanges(") == std::string::npos) {
		failureReason = "Changed-text wiring requires dedicated dirty-range tracking in TMRFileEditor.";
		return false;
	}
	failureReason.clear();
	return true;
}

bool testPersistentBlocksWiringGuard(std::string &failureReason) {
	const std::string settingsPath = absolutePathFromCwd("services/MRDialogPaths.cpp");
	const std::string vmPath = absolutePathFromCwd("mrmac/mrvm.cpp");
	const std::string dialogPath = absolutePathFromCwd("dialogs/MREditSettingsDialog.cpp");
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
		failureReason = "Unable to read MREditSettingsDialog.cpp for persistent-blocks guard: " + ioError;
		return false;
	}
	if (settingsContent.find("upperKeyName == \"PERSISTBLOCKS\" || upperKeyName == \"PERSISTENTBLOCKS\"") ==
	        std::string::npos ||
	    settingsContent.find("MRSETUP('PERSISTENTBLOCKS'") == std::string::npos) {
		failureReason = "Persistent blocks must be parsed and serialized via MRSETUP in MRDialogPaths.";
		return false;
	}
	if (vmContent.find("setupKey == \"PERSISTBLOCKS\"") == std::string::npos) {
		failureReason = "MRVM startup whitelist must accept PERSISTBLOCKS.";
		return false;
	}
	if (dialogContent.find("ersistent blocks") == std::string::npos) {
		failureReason = "Edit settings dialog must expose a Persistent blocks option.";
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

} // namespace

int main(int argc, char **argv) {
	if (argc >= 2) {
		if (argc == 3 && std::strcmp(argv[1], "--probe") == 0) {
			if (std::strcmp(argv[2], "staged-nav") == 0)
				return runStagedNavProbeMode();
			if (std::strcmp(argv[2], "staged-mark-page") == 0)
				return runStagedMarkPageProbeMode();
		}
		std::cerr << "usage: regression/mr-regression-checks [--probe staged-nav|staged-mark-page]\n";
		return 2;
	}

	TestContext ctx;

	runTest(ctx, "Path defaults from environment/OS", testPathDefaultsFromEnvironment);
	runTest(ctx, "MRSETUP startup-only semantics", testMrsetupStartupOnly);
	runTest(ctx, "settings.mrmac auto-create on missing file", testSettingsMacroAutoCreate);
	runTest(ctx, "Dialog palette guard (no 32..63 overrides)", testDialogPaletteOverridesAbsent);
	runTest(ctx, "WINDOWCOLORS targets blue window palette", testWindowColorGroupTargetsBlueWindowPalette);
	runTest(ctx, "MENUDIALOGCOLORS targets menu + gray dialog palette", testMenuDialogColorGroupTargetsExpectedSlots);
	runTest(ctx, "MENUDIALOGCOLORS semantic labels guard", testMenuDialogSemanticLabelsGuard);
	runTest(ctx, "MENUDIALOGCOLORS hotkey selection alias guard", testMenuEntryHotkeySelectionAliasGuard);
	runTest(ctx, "Setup scroll refresh guard", testSetupScrollRefreshGuard);
	runTest(ctx, "Paths browse event guard", testPathsBrowseEventGuard);
	runTest(ctx, "Current-line color wiring guard", testCurrentLineColorWiringGuard);
	runTest(ctx, "Changed-text color wiring guard", testChangedTextColorWiringGuard);
	runTest(ctx, "Persistent blocks wiring guard", testPersistentBlocksWiringGuard);
	runTest(ctx, "Edit clipboard routing guard", testEditClipboardCommandRoutingGuard);
	runTest(ctx, "TO/FROM header parsing + compile guards", testToFromHeaders);
	runTest(ctx, "TO/FROM runtime dispatch", testToFromDispatch);
	runTest(ctx, "KEY_IN behavior + staging guards", testKeyIn);

	std::cout << "\nRegression summary: " << ctx.passed << " passed, " << ctx.failed << " failed.\n";
	return ctx.failed == 0 ? 0 : 1;
}
