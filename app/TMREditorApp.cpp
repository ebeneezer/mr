#define Uses_TKeys
#define Uses_MsgBox
#define Uses_TDialog
#define Uses_TStaticText
#define Uses_TFileDialog
#define Uses_TObject
#define Uses_TApplication
#define Uses_TEvent
#define Uses_TRect
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_TDeskTop
#define Uses_TScreen
#include <tvision/tv.h>

#include "TMREditorApp.hpp"

#include "../coprocessor/MRCoprocessor.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/mrvm.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../coprocessor/MRCoprocessorDispatch.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../dialogs/MRUnsavedChangesDialog.hpp"
#include "../coprocessor/MRPerformance.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../ui/TMRDeskTop.hpp"
#include "../ui/TMREditWindow.hpp"
#include "../ui/TMRMenuBar.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/TMRStatusLine.hpp"
#include "../ui/MRPalette.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "MRAppState.hpp"
#include "MRCommandRouter.hpp"
#include "MRCommands.hpp"
#include "MRMenuFactory.hpp"

#include <ctime>
#include <chrono>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace {
static constexpr std::chrono::milliseconds kRecordingBlinkInterval(450);

class TMacroBindCaptureDialog : public TDialog {
  public:
	TMacroBindCaptureDialog()
	    : TWindowInit(&TDialog::initFrame), TDialog(TRect(0, 0, 52, 8), "Bind Recorded Macro Key"),
	      hasCaptured_(false), keyCode_(kbNoKey), controlState_(0) {
		options |= ofCentered;
		insert(new TStaticText(TRect(2, 2, 50, 6),
		                       "Press key to bind the recorded macro.\nEsc = no binding."));
	}

	virtual void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown) {
			TKey pressed(event.keyDown);
			if (pressed == TKey(kbEsc)) {
				endModal(cmCancel);
				clearEvent(event);
				return;
			}
			if (pressed == TKey(kbAltF10)) {
				clearEvent(event);
				return;
			}
			hasCaptured_ = true;
			keyCode_ = event.keyDown.keyCode;
			controlState_ = event.keyDown.controlKeyState;
			endModal(cmOK);
			clearEvent(event);
			return;
		}
		TDialog::handleEvent(event);
	}

	bool hasCaptured() const noexcept {
		return hasCaptured_;
	}

	ushort keyCode() const noexcept {
		return keyCode_;
	}

	ushort controlState() const noexcept {
		return controlState_;
	}

  private:
	bool hasCaptured_;
	ushort keyCode_;
	ushort controlState_;
};

std::string trimAscii(std::string_view value) {
	std::size_t start = 0;
	std::size_t end = value.size();

	while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0)
		++start;
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
		--end;
	return std::string(value.substr(start, end - start));
}

std::string expandUserPath(std::string_view input) {
	std::string path = trimAscii(input);
	if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != nullptr && *home != '\0')
			return std::string(home) + path.substr(1);
	}
	return path;
}

std::string ensureMrmacExtension(std::string_view pathView) {
	std::string path(pathView);
	std::size_t dotPos = path.rfind('.');
	if (dotPos != std::string::npos) {
		std::string ext = path.substr(dotPos);
		for (char & i : ext)
			i = static_cast<char>(std::tolower(static_cast<unsigned char>(i)));
		if (ext == ".mrmac")
			return path;
	}
	return path + ".mrmac";
}

std::string makeRecordedMacroName(unsigned long counter) {
	std::array<char, 32> timePart{};
	std::time_t now = std::time(nullptr);
	std::tm tmNow;

#if defined(__unix__)
	localtime_r(&now, &tmNow);
#else
	tmNow = *std::localtime(&now);
#endif
	std::strftime(timePart.data(), timePart.size(), "Recorded_%Y%m%d_%H%M%S", &tmNow);
	return std::string(timePart.data()) + "_" + std::to_string(counter);
}

void appendEscapedKeyInChar(std::string &out, unsigned char ch) {
	if (ch == '\'') {
		out += "''";
		return;
	}
	if (ch == '\r' || ch == '\n') {
		out += "<Enter>";
		return;
	}
	if (ch == '\t') {
		out += "<Tab>";
		return;
	}
	if (ch == '\b') {
		out += "<Backspace>";
		return;
	}
	if (ch == 27) {
		out += "<Esc>";
		return;
	}
	if (ch == 127) {
		out += "<Del>";
		return;
	}
	if (std::isprint(ch) == 0)
		return;
	out.push_back(static_cast<char>(ch));
}

bool keyInTokenFromEvent(ushort keyCode, ushort controlKeyState, std::string &outToken) {
	struct ComboSpec {
		const char *prefix;
		ushort mods;
	};
	struct NamedKeySpec {
		const char *token;
		ushort code;
	};
	static const ComboSpec combos[] = {{"", 0},
	                                   {"Shft", kbShift},
	                                   {"Ctrl", kbCtrlShift},
	                                   {"Alt", kbAltShift},
	                                   {"CtrlShft", static_cast<ushort>(kbCtrlShift | kbShift)},
	                                   {"AltShft", static_cast<ushort>(kbAltShift | kbShift)},
	                                   {"CtrlAlt", static_cast<ushort>(kbCtrlShift | kbAltShift)},
	                                   {"CtrlAltShft",
	                                    static_cast<ushort>(kbCtrlShift | kbAltShift | kbShift)}};
	static const NamedKeySpec named[] = {{"Enter", kbEnter},
	                                     {"Tab", kbTab},
	                                     {"Esc", kbEsc},
	                                     {"Backspace", kbBack},
	                                     {"Up", kbUp},
	                                     {"Down", kbDown},
	                                     {"Left", kbLeft},
	                                     {"Right", kbRight},
	                                     {"PgUp", kbPgUp},
	                                     {"PgDn", kbPgDn},
	                                     {"Home", kbHome},
	                                     {"End", kbEnd},
	                                     {"Ins", kbIns},
	                                     {"Del", kbDel},
	                                     {"Grey-", kbGrayMinus},
	                                     {"Grey+", kbGrayPlus},
	                                     {"Grey*", static_cast<ushort>('*')},
	                                     {"Space", static_cast<ushort>(' ')},
	                                     {"Minus", static_cast<ushort>('-')},
	                                     {"Equal", static_cast<ushort>('=')},
	                                     {"F1", kbF1},
	                                     {"F2", kbF2},
	                                     {"F3", kbF3},
	                                     {"F4", kbF4},
	                                     {"F5", kbF5},
	                                     {"F6", kbF6},
	                                     {"F7", kbF7},
	                                     {"F8", kbF8},
	                                     {"F9", kbF9},
	                                     {"F10", kbF10},
	                                     {"F11", kbF11},
	                                     {"F12", kbF12}};
	TKey pressed(keyCode, controlKeyState);

	for (const ComboSpec &combo : combos)
		for (const NamedKeySpec &entry : named)
			if (pressed == TKey(entry.code, combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken += entry.token;
				outToken += ">";
				return true;
			}

	for (const ComboSpec &combo : combos) {
		for (char c = 'A'; c <= 'Z'; ++c)
			if (pressed == TKey(static_cast<ushort>(c), combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken.push_back(c);
				outToken += ">";
				return true;
			}
		for (char c = '0'; c <= '9'; ++c)
			if (pressed == TKey(static_cast<ushort>(c), combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken.push_back(c);
				outToken += ">";
				return true;
			}
	}

	if (keyCode != kbNoKey && keyCode < 256 && std::isprint(static_cast<unsigned char>(keyCode)) != 0) {
		outToken = "<";
		outToken.push_back(static_cast<char>(keyCode));
		outToken += ">";
		return true;
	}
	return false;
}

bool writeTextFile(std::string_view path, std::string_view content) {
	std::ofstream out(std::string(path).c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out << content;
	return out.good();
}

bool pathIsRegularFile(std::string_view path) {
	struct stat st;

	if (path.empty())
		return false;
	return ::stat(std::string(path).c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool confirmOverwriteForPath(const char *primaryLabel, const char *headline, const std::string &targetPath) {
	if (!pathIsRegularFile(targetPath))
		return true;
	return mr::dialogs::showUnsavedChangesDialog(primaryLabel, headline, targetPath.c_str()) ==
	       mr::dialogs::UnsavedChangesChoice::Save;
}

bool readTextFile(std::string_view path, std::string &out) {
	std::ifstream in(std::string(path).c_str(), std::ios::in | std::ios::binary);
	if (!in)
		return false;
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return in.good() || in.eof();
}

bool validateMacroSource(std::string_view source, std::string &errorText) {
	size_t bytecodeSize = 0;
	std::string sourceText(source);
	unsigned char *bytecode = compile_macro_code(sourceText.c_str(), &bytecodeSize);

	if (bytecode == nullptr) {
		const char *err = get_last_compile_error();
		errorText = (err != nullptr && *err != '\0') ? err : "Compilation failed.";
		return false;
	}
	std::free(bytecode);
	errorText.clear();
	return true;
}

[[nodiscard]] bool hasVmErrorLineSince(const std::vector<std::string> &lines, std::size_t start, std::string &outError) {
	static constexpr std::string_view prefix = "VM Error:";
	for (std::size_t i = start; i < lines.size(); ++i)
		if (lines[i].compare(0, prefix.size(), prefix.data(), prefix.size()) == 0) {
			outError = lines[i];
			return true;
		}
	return false;
}

std::string upperAscii(std::string value) {
	for (char & i : value)
		i = static_cast<char>(std::toupper(static_cast<unsigned char>(i)));
	return value;
}

std::string unescapeMrmacSingleQuotedLiteral(const std::string &value) {
	std::string out;
	out.reserve(value.size());
	for (std::size_t i = 0; i < value.size(); ++i) {
		char ch = value[i];
		if (ch == '\'' && i + 1 < value.size() && value[i + 1] == '\'') {
			out.push_back('\'');
			++i;
		} else
			out.push_back(ch);
	}
	return out;
}

[[nodiscard]] bool isCurrentSettingsKey(std::string_view key);

[[nodiscard]] bool isMigratableMrsetupKey(std::string_view key) {
	return isCurrentSettingsKey(key);
}

bool isCurrentSettingsKey(std::string_view key) {
	static constexpr std::array<std::string_view, 27> keys = {"SETTINGS_VERSION",
	                                           "MACROPATH",
	                                           "SETTINGSPATH",
	                                           "HELPPATH",
	                                           "TEMPDIR",
	                                           "SHELLPATH",
	                                           "LASTFILEDIALOGPATH",
	                                           "DEFAULT_PROFILE_DESCRIPTION",
	                                           "COLORTHEMEURI",
	                                           "PAGE_BREAK",
	                                           "WORD_DELIMITERS",
	                                           "DEFAULT_EXTENSIONS",
	                                           "TRUNCATE_SPACES",
		                                           "EOF_CTRL_Z",
		                                           "EOF_CR_LF",
		                                           "TAB_EXPAND",
		                                           "TAB_SIZE",
		                                           "BACKUP_FILES",
	                                           "SHOW_EOF_MARKER",
	                                           "SHOW_EOF_MARKER_EMOJI",
	                                           "SHOW_LINE_NUMBERS",
	                                           "LINE_NUM_ZERO_FILL",
	                                           "PERSISTENT_BLOCKS",
	                                           "CODE_FOLDING",
	                                           "COLUMN_BLOCK_MOVE",
	                                           "DEFAULT_MODE",
	                                           "CURSOR_STATUS_COLOR"};
	return std::find(keys.begin(), keys.end(), key) != keys.end();
}

struct ParsedEditProfileDirective {
	std::string operation;
	std::string profileId;
	std::string arg3;
	std::string arg4;
};

std::vector<std::pair<std::string, std::string>> parseMrsetupAssignments(std::string_view source) {
	static const std::regex assignmentPattern(
	    "MRSETUP\\s*\\(\\s*'([^']+)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)", std::regex::icase);
	std::vector<std::pair<std::string, std::string>> assignments;
	std::smatch match;
	std::string remaining(source);

	while (std::regex_search(remaining, match, assignmentPattern)) {
		if (match.size() >= 3) {
			std::string key = upperAscii(trimAscii(match[1].str()));
			std::string value = unescapeMrmacSingleQuotedLiteral(match[2].str());
			assignments.push_back(std::make_pair(key, value));
		}
		remaining = match.suffix().str();
	}
	return assignments;
}

std::vector<ParsedEditProfileDirective> parseMreditProfileDirectives(std::string_view source) {
	static const std::regex directivePattern(
	    "MREDITPROFILE\\s*\\(\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)",
	    std::regex::icase);
	std::vector<ParsedEditProfileDirective> directives;
	std::smatch match;
	std::string remaining(source);

	while (std::regex_search(remaining, match, directivePattern)) {
		if (match.size() >= 5) {
			ParsedEditProfileDirective directive;
			directive.operation = unescapeMrmacSingleQuotedLiteral(match[1].str());
			directive.profileId = unescapeMrmacSingleQuotedLiteral(match[2].str());
			directive.arg3 = unescapeMrmacSingleQuotedLiteral(match[3].str());
			directive.arg4 = unescapeMrmacSingleQuotedLiteral(match[4].str());
			directives.push_back(directive);
		}
		remaining = match.suffix().str();
	}
	return directives;
}

class StartupSettingsModeGuard {
  public:
	StartupSettingsModeGuard() noexcept : previous_(mrvmIsStartupSettingsMode()) {
		mrvmSetStartupSettingsMode(true);
	}

	~StartupSettingsModeGuard() {
		mrvmSetStartupSettingsMode(previous_);
	}

  private:
	bool previous_;
};

bool validateCurrentSettingsSchema(const std::string &source, std::string &errorText) {
	std::vector<std::pair<std::string, std::string>> assignments = parseMrsetupAssignments(source);
	std::map<std::string, int> counts;
	static constexpr std::array<std::string_view, 26> requiredKeys = {"SETTINGS_VERSION",
	                                           "SETTINGSPATH",
	                                           "MACROPATH",
	                                           "HELPPATH",
	                                           "TEMPDIR",
	                                           "SHELLPATH",
	                                           "LASTFILEDIALOGPATH",
	                                           "DEFAULT_PROFILE_DESCRIPTION",
	                                           "PAGE_BREAK",
	                                           "WORD_DELIMITERS",
	                                           "DEFAULT_EXTENSIONS",
	                                           "TRUNCATE_SPACES",
		                                           "EOF_CTRL_Z",
		                                           "EOF_CR_LF",
		                                           "TAB_EXPAND",
		                                           "TAB_SIZE",
		                                           "BACKUP_FILES",
	                                           "SHOW_EOF_MARKER",
	                                           "SHOW_EOF_MARKER_EMOJI",
	                                           "SHOW_LINE_NUMBERS",
	                                           "LINE_NUM_ZERO_FILL",
	                                           "PERSISTENT_BLOCKS",
	                                           "CODE_FOLDING",
	                                           "COLUMN_BLOCK_MOVE",
	                                           "DEFAULT_MODE",
	                                           "COLORTHEMEURI"};

	if (assignments.empty()) {
		errorText = "No MRSETUP assignments found.";
		return false;
	}
	for (const std::pair<std::string, std::string> &assignment : assignments) {
		if (!isCurrentSettingsKey(assignment.first)) {
			errorText = "Unsupported MRSETUP key in settings.mrmac: " + assignment.first;
			return false;
		}
		++counts[assignment.first];
		if (counts[assignment.first] > 1) {
			errorText = "Duplicate MRSETUP key in settings.mrmac: " + assignment.first;
			return false;
		}
	}
	for (std::string_view required : requiredKeys)
		if (counts[std::string(required)] != 1) {
			errorText = "Missing required MRSETUP key in settings.mrmac: " + std::string(required);
			return false;
		}
	for (const std::pair<std::string, std::string> &assignment : assignments)
		if (assignment.first == "SETTINGS_VERSION" && assignment.second != "2") {
			errorText = "Unsupported settings.mrmac version: " + assignment.second;
			return false;
		}
	errorText.clear();
	return true;
}

bool setConfiguredColorSetupDefaults(std::string &errorText) {
	MRColorSetupSettings defaults = resolveColorSetupDefaults();

	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, defaults.windowColors.data(),
	                                        defaults.windowColors.size(), &errorText))
		return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, defaults.menuDialogColors.data(),
	                                        defaults.menuDialogColors.size(), &errorText))
		return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Help, defaults.helpColors.data(),
	                                        defaults.helpColors.size(), &errorText))
		return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Other, defaults.otherColors.data(),
	                                        defaults.otherColors.size(), &errorText))
		return false;
	errorText.clear();
	return true;
}

bool applyRecognizedSettingsAssignment(const std::string &key, const std::string &value, MRSetupPaths &paths,
                                       std::string &errorText) {
	auto applyValidatedNormalizedPath = [&](auto validator, std::string &target) {
		if (!validator(value, &errorText))
			return false;
		target = normalizeConfiguredPathInput(value);
		return true;
	};

	if (key == "SETTINGS_VERSION") {
		if (value != "2") {
			errorText = "Unsupported settings version.";
			return false;
		}
		errorText.clear();
		return true;
	}
	if (key == "SETTINGSPATH") {
		// Keep the active startup file as source of truth; ignore redirected settings URI.
		(void)value;
		errorText.clear();
		return true;
	}
	if (key == "MACROPATH")
		return applyValidatedNormalizedPath(validateMacroDirectoryPath, paths.macroPath);
	if (key == "HELPPATH")
		return applyValidatedNormalizedPath(validateHelpFilePath, paths.helpUri);
	if (key == "TEMPDIR")
		return applyValidatedNormalizedPath(validateTempDirectoryPath, paths.tempPath);
	if (key == "SHELLPATH")
		return applyValidatedNormalizedPath(validateShellExecutablePath, paths.shellUri);
	if (key == "LASTFILEDIALOGPATH")
		return setConfiguredLastFileDialogPath(value, &errorText);
	if (key == "DEFAULT_PROFILE_DESCRIPTION")
		return setConfiguredDefaultProfileDescription(value, &errorText);
	if (key == "COLORTHEMEURI")
		return setConfiguredColorThemeFilePath(value, &errorText);
	if (key == "PAGE_BREAK" || key == "WORD_DELIMITERS" || key == "DEFAULT_EXTENSIONS" ||
	    key == "TRUNCATE_SPACES" || key == "EOF_CTRL_Z" || key == "EOF_CR_LF" ||
	    key == "TAB_EXPAND" || key == "TAB_SIZE" || key == "BACKUP_FILES" ||
	    key == "SHOW_EOF_MARKER" || key == "SHOW_EOF_MARKER_EMOJI" ||
	    key == "SHOW_LINE_NUMBERS" || key == "LINE_NUM_ZERO_FILL" ||
	    key == "PERSISTENT_BLOCKS" || key == "CODE_FOLDING" || key == "COLUMN_BLOCK_MOVE" ||
	    key == "DEFAULT_MODE" || key == "CURSOR_STATUS_COLOR")
		return applyConfiguredEditSetupValue(key, value, &errorText);
	if (key == "WINDOWCOLORS" || key == "MENUDIALOGCOLORS" || key == "HELPCOLORS" || key == "OTHERCOLORS")
		return applyConfiguredColorSetupValue(key, value, &errorText);

	errorText = "Unsupported key.";
	return false;
}

bool migrateSettingsMacroToCurrentVersion(const std::string &settingsPath, const std::string &source,
                                          const std::string &reason, std::string *errorMessage) {
	std::string normalizedSettingsPath = normalizeConfiguredPathInput(settingsPath);
	MRSetupPaths migratedPaths = resolveSetupPathDefaults();
	std::vector<std::pair<std::string, std::string>> assignments = parseMrsetupAssignments(source);
	std::vector<ParsedEditProfileDirective> profileDirectives = parseMreditProfileDirectives(source);
	std::string applyError;
	std::string themeError;
	std::string finalThemePath;
	std::size_t migratedAssignments = 0;

	migratedPaths.settingsMacroUri = normalizedSettingsPath;

	if (!setConfiguredSettingsMacroFilePath(normalizedSettingsPath, &applyError))
		goto fail;
	if (!setConfiguredEditSetupSettings(resolveEditSetupDefaults(), &applyError))
		goto fail;
	if (!setConfiguredColorSetupDefaults(applyError))
		goto fail;
	if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(), &applyError))
		goto fail;
	if (!setConfiguredColorThemeFilePath(defaultColorThemeFilePath(), &applyError))
		goto fail;

	for (const std::pair<std::string, std::string> &assignment : assignments) {
		if (!isMigratableMrsetupKey(assignment.first))
			continue;
		if (!applyRecognizedSettingsAssignment(assignment.first, assignment.second, migratedPaths, applyError)) {
			mrLogMessage(("Settings migration ignored invalid value for key " + assignment.first + ": " +
			              applyError)
			                 .c_str());
			continue;
		}
		++migratedAssignments;
	}
	for (const ParsedEditProfileDirective &directive : profileDirectives)
		if (!applyConfiguredEditExtensionProfileDirective(directive.operation, directive.profileId, directive.arg3,
		                                                 directive.arg4, &applyError))
			mrLogMessage(("Settings migration ignored invalid MREDITPROFILE directive for profile id " +
			              directive.profileId + ": " + applyError)
			                 .c_str());

	finalThemePath = configuredColorThemeFilePath();
	if (!loadColorThemeFile(finalThemePath, &themeError)) {
		mrLogMessage(("Settings migration theme fallback: " + themeError).c_str());
		if (!setConfiguredColorSetupDefaults(applyError))
			goto fail;
		if (!setConfiguredColorThemeFilePath(defaultColorThemeFilePath(), &applyError))
			goto fail;
	}

	if (!writeSettingsMacroFile(migratedPaths, &applyError))
		goto fail;

	mrLogMessage(("Settings discrepancy detected, migrated to current version: " + reason).c_str());
	mrLogMessage(("Settings migration carried over " + std::to_string(migratedAssignments) +
	              " recognized assignment(s).")
	                 .c_str());
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;

fail:
	if (errorMessage != nullptr)
		*errorMessage = "Settings migration failed: " + applyError;
	return false;
}

bool applySettingsSource(const std::string &source, std::string *errorMessage) {
	std::string schemaError;
	std::string compileError;
	std::string vmError;
	std::string themeError;
	size_t bytecodeSize = 0;
	unsigned char *bytecode = nullptr;
	int macroCount = 0;
	VirtualMachine vm;

	if (!validateCurrentSettingsSchema(source, schemaError)) {
		if (errorMessage != nullptr)
			*errorMessage = "Settings schema mismatch: " + schemaError;
		return false;
	}

	bytecode = compile_macro_code(source.c_str(), &bytecodeSize);
	if (bytecode == nullptr) {
		const char *err = get_last_compile_error();
		compileError = (err != nullptr && *err != '\0') ? err : "Compilation failed.";
		if (errorMessage != nullptr)
			*errorMessage = "Settings load failed (compile): " + compileError;
		return false;
	}

	macroCount = get_compiled_macro_count();
	if (macroCount <= 0) {
		std::free(bytecode);
		if (errorMessage != nullptr)
			*errorMessage = "Settings load failed: no macros found.";
		return false;
	}

	if (!setConfiguredEditSetupSettings(resolveEditSetupDefaults(), &vmError) ||
	    !setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(), &vmError) ||
	    !setConfiguredDefaultProfileDescription("Global defaults", &vmError) ||
	    !setConfiguredColorSetupDefaults(vmError) ||
	    !setConfiguredColorThemeFilePath(defaultColorThemeFilePath(), &vmError)) {
		std::free(bytecode);
		if (errorMessage != nullptr)
			*errorMessage = "Settings load failed (reset): " + vmError;
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
				if (errorMessage != nullptr)
					*errorMessage = "Settings load failed: invalid macro entry.";
				return false;
			}
			vm.executeAt(bytecode, bytecodeSize, static_cast<size_t>(entry), std::string(),
			             macroName != nullptr ? macroName : std::string(), i == 0, true);
			if (hasVmErrorLineSince(vm.log, logStart, vmError)) {
				std::free(bytecode);
				if (errorMessage != nullptr)
					*errorMessage = "Settings load failed (runtime): " + vmError;
				return false;
			}
		}
	}

	std::free(bytecode);
	if (!loadColorThemeFile(configuredColorThemeFilePath(), &themeError)) {
		if (errorMessage != nullptr)
			*errorMessage = "Color theme load failed: " + themeError;
		return false;
	}

	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

ushort execDialogWithData(TDialog *dialog, void *data) {
	ushort result = cmCancel;

	if (dialog == nullptr)
		return cmCancel;
	if (data != nullptr)
		dialog->setData(data);
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel && data != nullptr)
		dialog->getData(data);
	TObject::destroy(dialog);
	return result;
}

ushort mrEditorDialog(int dialog, ...) {
	va_list arg;
	ushort result = cmCancel;

	switch (dialog) {
		case edOutOfMemory:
			return messageBox(mfError | mfOKButton, "Out of memory.");
		case edReadError: {
			const char *path = nullptr;
			va_start(arg, dialog);
			path = va_arg(arg, const char *);
			va_end(arg);
			return messageBox(mfError | mfOKButton, "Error reading file:\n%s",
			                  (path != nullptr && *path != '\0') ? path : "<unknown>");
		}
		case edWriteError: {
			const char *path = nullptr;
			va_start(arg, dialog);
			path = va_arg(arg, const char *);
			va_end(arg);
			return messageBox(mfError | mfOKButton, "Error writing file:\n%s",
			                  (path != nullptr && *path != '\0') ? path : "<unknown>");
		}
		case edCreateError: {
			const char *path = nullptr;
			va_start(arg, dialog);
			path = va_arg(arg, const char *);
			va_end(arg);
			return messageBox(mfError | mfOKButton, "Error creating file:\n%s",
			                  (path != nullptr && *path != '\0') ? path : "<unknown>");
		}
		case edSaveModify: {
			const char *path = nullptr;
			va_start(arg, dialog);
			path = va_arg(arg, const char *);
			va_end(arg);
			return messageBox(mfInformation | mfYesNoCancel, "File modified. Save changes to:\n%s",
			                  (path != nullptr && *path != '\0') ? path : "<unnamed>");
		}
		case edSaveUntitled:
			return messageBox(mfInformation | mfYesNoCancel, "Save untitled file?");
		case edSaveAs: {
			char *target = nullptr;
			va_start(arg, dialog);
			target = va_arg(arg, char *);
			va_end(arg);
			if (target == nullptr)
				return cmCancel;
			result = execDialogWithData(
			    new TFileDialog("*.*", "Save file as", "~N~ame", fdOKButton, 101), target);
			return result;
		}
		default:
			return cmCancel;
	}
}

bool loadStartupSettingsMacro(const std::string &overridePath, std::string *errorMessage) {
	std::string settingsPath = overridePath.empty() ? defaultSettingsMacroFilePath() : overridePath;
	std::string source;
	std::string loadError;
	std::string migrateError;

	if (settingsPath.empty()) {
		if (errorMessage != nullptr)
			*errorMessage = "Settings path is empty.";
		return false;
	}
	if (!ensureSettingsMacroFileExists(settingsPath, errorMessage)) {
		mrLogMessage(errorMessage != nullptr ? errorMessage->c_str()
		                                   : "Settings bootstrap failed (create defaults).");
		return false;
	}
	if (!readTextFile(settingsPath, source)) {
		source.clear();
		loadError = "Settings load failed (read): " + settingsPath;
	} else if (applySettingsSource(source, &loadError)) {
		mrLogMessage(("Settings loaded: " + settingsPath).c_str());
		mrLogMessage(("Color theme loaded: " + configuredColorThemeFilePath()).c_str());
		mrLogMessage(("Settings MACROPATH: " + defaultMacroDirectoryPath()).c_str());
		if (errorMessage != nullptr)
			errorMessage->clear();
		return true;
	}

	if (!migrateSettingsMacroToCurrentVersion(settingsPath, source, loadError, &migrateError)) {
		if (errorMessage != nullptr)
			*errorMessage = migrateError;
		mrLogMessage(errorMessage != nullptr ? errorMessage->c_str() : "Settings migration failed.");
		return false;
	}

	if (!readTextFile(settingsPath, source)) {
		if (errorMessage != nullptr)
			*errorMessage = "Settings load failed after migration (read): " + settingsPath;
		mrLogMessage(errorMessage != nullptr ? errorMessage->c_str()
		                                   : "Settings load failed after migration (read).");
		return false;
	}
	if (!applySettingsSource(source, &loadError)) {
		if (errorMessage != nullptr)
			*errorMessage = loadError;
		mrLogMessage(errorMessage != nullptr ? errorMessage->c_str()
		                                   : "Settings load failed after migration.");
		return false;
	}

	mrLogMessage(("Settings loaded: " + settingsPath).c_str());
	mrLogMessage(("Color theme loaded: " + configuredColorThemeFilePath()).c_str());
	mrLogMessage(("Settings MACROPATH: " + defaultMacroDirectoryPath()).c_str());
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string buildTopRightCursorStatus() {
	TMREditWindow *win = currentEditWindow();
	if (win == nullptr || win->getEditor() == nullptr)
		return std::string();
	if (isEmptyUntitledEditableWindow(win))
		return std::string();

	char buf[64];
	std::snprintf(buf, sizeof(buf), "%lu|%lu", win->cursorLineNumber(), win->cursorColumnNumber());
	return std::string(buf);
}

bool parseCursorStatusColorOverride(std::string_view value, unsigned char &out) {
	std::string text = trimAscii(value);
	unsigned int parsed = 0;

	if (text.empty() || text.size() > 2)
		return false;
	for (char ch : text)
		if (std::isxdigit(static_cast<unsigned char>(ch)) == 0)
			return false;
	parsed = static_cast<unsigned int>(std::strtoul(text.c_str(), nullptr, 16));
	if (parsed > 0xFF)
		return false;
	out = static_cast<unsigned char>(parsed);
	return true;
}

bool currentCursorStatusColorOverride(unsigned char &out) {
	TMREditWindow *win = currentEditWindow();
	MREditSetupSettings effective;
	std::string path;

	if (win == nullptr || win->getEditor() == nullptr || !win->hasPersistentFileName())
		return false;
	path = win->currentFileName();
	if (!effectiveEditSetupSettingsForPath(path, effective, nullptr))
		return false;
	return parseCursorStatusColorOverride(effective.cursorStatusColor, out);
}

TMRMenuBar::MarqueeKind mapMessageNoticeKind(mr::messageline::Kind kind) {
	switch (kind) {
		case mr::messageline::Kind::Success:
			return TMRMenuBar::MarqueeKind::Success;
		case mr::messageline::Kind::Warning:
			return TMRMenuBar::MarqueeKind::Warning;
		case mr::messageline::Kind::Error:
			return TMRMenuBar::MarqueeKind::Error;
		case mr::messageline::Kind::Info:
		default:
			return TMRMenuBar::MarqueeKind::Info;
	}
}

bool isHeroVisibleMessage(const mr::messageline::VisibleMessage &visible) {
	mr::messageline::VisibleMessage ownerMessage;
	if (mr::messageline::currentOwnerMessage(mr::messageline::Owner::HeroEvent, ownerMessage) &&
	    ownerMessage.kind == visible.kind && ownerMessage.text == visible.text)
		return true;
	if (mr::messageline::currentOwnerMessage(mr::messageline::Owner::HeroEventFollowup, ownerMessage) &&
	    ownerMessage.kind == visible.kind && ownerMessage.text == visible.text)
		return true;
	return false;
}

const TPalette &extendedAppBasePalette() {
	static const TPalette palette = []() -> TPalette {
		static const int kBaseSlots = 135;
		static const int kTotalSlots = kMrPaletteMax;
		static const char cp[] = cpAppColor;
		TColorAttr data[kTotalSlots];
		int i = 0;

		for (i = 0; i < kBaseSlots; ++i)
			data[i] = static_cast<unsigned char>(cp[i]);
		// Dedicated editor-only accent slots (avoid window frame/scrollbar side effects).
		data[kMrPaletteCurrentLine - 1] = data[10 - 1];
		data[kMrPaletteCurrentLineInBlock - 1] = data[12 - 1];
		data[kMrPaletteChangedText - 1] = data[14 - 1];
		data[kMrPaletteMessageError - 1] = data[42 - 1];
		data[kMrPaletteMessage - 1] = data[43 - 1];
		data[kMrPaletteMessageWarning - 1] = data[44 - 1];
		data[kMrPaletteMessageHero - 1] = data[43 - 1];
		data[kMrPaletteLineNumbers - 1] = data[9 - 1];
		data[kMrPaletteEofMarker - 1] = data[14 - 1];
		return TPalette(data, static_cast<ushort>(kTotalSlots));
	}();
	return palette;
}
} // namespace

TMenuBar *TMREditorApp::initMRMenuBar(TRect r) {
	return createMRMenuBar(r);
}

TStatusLine *TMREditorApp::initMRStatusLine(TRect r) {
	r.a.y = r.b.y - 1;
	return new TMRStatusLine(r, *new TStatusDef(0, 0xFFFF) +
	                                *new TStatusItem("~F1~ Help", kbF1, cmMrHelpContents) +
	                                *new TStatusItem("~F10~ Menu", kbF10, cmMenu) +
	                                *new TStatusItem("~Alt-F10~ Rec", kbAltF10,
	                                                 cmMrMacroToggleRecording) +
	                                *new TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit));
}

TDeskTop *TMREditorApp::initMRDeskTop(TRect r) {
	r.a.y++;
	r.b.y--;
	return new TMRDeskTop(r);
}

TMREditorApp::TMREditorApp()
    : TProgInit(&TMREditorApp::initMRStatusLine, &TMREditorApp::initMRMenuBar,
                &TMREditorApp::initMRDeskTop),
      exitPrepared_(false), keystrokeRecording_(false), recordingMarkerVisible_(false),
       recordedMacroCounter_(0), 
      recordingBlinkToggleAt_(std::chrono::steady_clock::now() + kRecordingBlinkInterval),
      indexedMacroWarmupActive_(false), indexedMacroWarmupLoadedFiles_(0) {
	TEditor::editorDialog = mrEditorDialog;
	mr::coprocessor::globalCoprocessor().setResultHandler(handleCoprocessorResult);
	loadStartupSettingsMacro(std::string(), nullptr);
	applyConfiguredDisplayLayout();
	bootstrapIndexedMacroBindings();
	static_cast<void>(createEditorWindow("?No-File?"));
	applyConfiguredDisplayLayout();
	static_cast<void>(mrEnsureLogWindow(false));
	syncRecordingUiState();
	mrLogMessage("Editor session started.");
	updateAppCommandState();
}

TMREditorApp::~TMREditorApp() {
	prepareForQuit();
	mr::coprocessor::globalCoprocessor().shutdown(true);
}

bool TMREditorApp::reloadSettingsMacroFromPath(const std::string &path, std::string *errorMessage) {
	std::vector<TMREditWindow *> windows;
	bool defaultInsertMode = true;

	if (!loadStartupSettingsMacro(path, errorMessage))
		return false;
	defaultInsertMode = configuredDefaultInsertMode();
	windows = allEditWindowsInZOrder();
	for (auto & window : windows)
		if (window != nullptr && window->getEditor() != nullptr) {
			if (!window->isReadOnly())
				window->getEditor()->setInsertModeEnabled(defaultInsertMode);
			window->getEditor()->refreshConfiguredVisualSettings();
		}
	bootstrapIndexedMacroBindings();
	applyConfiguredDisplayLayout();
	return true;
}

void TMREditorApp::applyConfiguredWindowFramePolicy() {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();

	for (auto win : windows) {
			if (win == nullptr)
			continue;
		win->flags |= (wfMove | wfGrow | wfZoom | wfClose);
		if (win->frame != nullptr)
			win->frame->drawView();
	}
}

void TMREditorApp::applyConfiguredDisplayLayout() {
	bool statusVisible = true;
	TRect appRect = getExtent();
	TRect desktopRect;

	if (menuBar != nullptr) {
		menuBar->show();
	}
	if (auto *mrStatus = dynamic_cast<TMRStatusLine *>(statusLine)) {
		mrStatus->setShowFunctionKeyLabels(true);
		mrStatus->show();
	}
	desktopRect.a.x = 0;
	desktopRect.b.x = appRect.b.x - appRect.a.x;
	desktopRect.a.y = 1;
	desktopRect.b.y = appRect.b.y - appRect.a.y - (statusVisible ? 1 : 0);
	if (desktopRect.b.y <= desktopRect.a.y)
		desktopRect.b.y = desktopRect.a.y + 1;
	if (deskTop != nullptr)
		deskTop->locate(desktopRect);
	applyConfiguredWindowFramePolicy();
	if (deskTop != nullptr)
		deskTop->drawView();
	if (menuBar != nullptr)
		menuBar->drawView();
	if (statusLine != nullptr)
		statusLine->drawView();
}

void TMREditorApp::prepareForQuit() {
	if (exitPrepared_)
		return;

	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	std::size_t pendingTaskCount = 0;
	std::string settingsError;

	if (!persistConfiguredSettingsSnapshot(&settingsError) && !settingsError.empty())
		mrLogMessage(("Settings snapshot on exit failed: " + settingsError).c_str());

	exitPrepared_ = true;
	for (auto & window : windows)
		if (window != nullptr)
			pendingTaskCount += window->prepareCoprocessorTasksForShutdown();

	if (pendingTaskCount != 0) {
		std::string line = "Exit requested; cancelling ";
		line += std::to_string(pendingTaskCount);
		line += " running or pending coprocessor task";
		if (pendingTaskCount != 1)
			line += "s";
		line += ".";
		mrLogMessage(line.c_str());
		mr::coprocessor::globalCoprocessor().pump(64);
	}
	cancelForegroundMacroDelays();
}

bool TMREditorApp::isRecorderToggleKey(const TEvent &event) const {
	return event.what == evKeyDown && TKey(event.keyDown) == TKey(kbAltF10);
}

bool TMREditorApp::isRecorderToggleCommand(const TEvent &event) const {
	return event.what == evCommand && event.message.command == cmMrMacroToggleRecording;
}

void TMREditorApp::redrawRecordingMarkerFrames() {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	for (auto & window : windows) {
		if (window != nullptr && window->frame != nullptr)
			window->frame->drawView();
	}
}

void TMREditorApp::syncRecordingUiState() {
	mrSetKeystrokeRecordingActive(keystrokeRecording_);
	mrSetKeystrokeRecordingMarkerVisible(keystrokeRecording_ && recordingMarkerVisible_);
	if (auto *mrStatusLine = dynamic_cast<TMRStatusLine *>(statusLine))
		mrStatusLine->setRecordingState(keystrokeRecording_, recordingMarkerVisible_);
	redrawRecordingMarkerFrames();
}

void TMREditorApp::updateRecordingBlink() {
	std::chrono::steady_clock::time_point now;
	if (!keystrokeRecording_)
		return;

	now = std::chrono::steady_clock::now();
	if (now < recordingBlinkToggleAt_)
		return;

	recordingMarkerVisible_ = !recordingMarkerVisible_;
	recordingBlinkToggleAt_ = now + kRecordingBlinkInterval;
	mrSetKeystrokeRecordingMarkerVisible(recordingMarkerVisible_);
	if (auto *mrStatusLine = dynamic_cast<TMRStatusLine *>(statusLine))
		mrStatusLine->setRecordingState(keystrokeRecording_, recordingMarkerVisible_);
	redrawRecordingMarkerFrames();
}

void TMREditorApp::startKeystrokeRecording() {
	keystrokeRecording_ = true;
	recordingMarkerVisible_ = true;
	recordingBlinkToggleAt_ = std::chrono::steady_clock::now() + kRecordingBlinkInterval;
	recordedKeySequence_.clear();
	syncRecordingUiState();
	mrLogMessage("Keystroke recording started (Alt-F10 to stop).");
}

void TMREditorApp::appendRecordedKeyEvent(const TEvent &event) {
	std::string keyToken;
	ushort state;

	if (event.what != evKeyDown)
		return;
	state = event.keyDown.controlKeyState;

	if ((state & kbPaste) != 0 && event.keyDown.textLength > 0) {
		for (uchar i = 0; i < event.keyDown.textLength; ++i)
			appendEscapedKeyInChar(recordedKeySequence_, static_cast<unsigned char>(event.keyDown.text[i]));
		return;
	}
	if (event.keyDown.textLength > 0) {
		for (uchar i = 0; i < event.keyDown.textLength; ++i)
			appendEscapedKeyInChar(recordedKeySequence_, static_cast<unsigned char>(event.keyDown.text[i]));
		return;
	}
	if (keyInTokenFromEvent(event.keyDown.keyCode, state, keyToken))
		recordedKeySequence_ += keyToken;
}

bool TMREditorApp::captureBindingKeySpec(std::string &keySpec) {
	TMacroBindCaptureDialog *dialog = nullptr;
	ushort modalResult;
	bool captured = false;
	ushort keyCode = kbNoKey;
	ushort controlState = 0;

	keySpec.clear();
	dialog = new TMacroBindCaptureDialog();
	if (dialog == nullptr)
		return false;
	modalResult = deskTop != nullptr ? deskTop->execView(dialog) : cmCancel;
	captured = dialog->hasCaptured();
	keyCode = dialog->keyCode();
	controlState = dialog->controlState();
	TObject::destroy(dialog);

	if (modalResult == cmCancel || !captured)
		return true;
	if (!keyInTokenFromEvent(keyCode, controlState, keySpec)) {
		messageBox(mfError | mfOKButton,
		           "Unsupported binding key.\nUse a function key or a Ctrl/Alt/Shift combination.");
		return false;
	}
	return true;
}

void TMREditorApp::finalizeKeystrokeRecording() {
	enum { SavePathBufferSize = 512 };
	char savePathBuffer[SavePathBufferSize];
	std::string keySpec;
	std::string savePath;
	std::string macroName;
	std::ostringstream source;
	std::string macroSource;
	std::string validationError;
	std::string sessionPath;
	std::string loadError;
	std::string summary;

	if (recordedKeySequence_.empty()) {
		messageBox(mfInformation | mfOKButton,
		           "Keystroke recording is empty.\n\nNothing to bind or save.");
		return;
	}

	if (!captureBindingKeySpec(keySpec))
		return;

	std::memset(savePathBuffer, 0, sizeof(savePathBuffer));
	if (inputBox("KEYSTROKE RECORDER", "~S~ave .mrmac (leer=nur Session-Bindung)",
	             savePathBuffer, static_cast<uchar>(sizeof(savePathBuffer) - 1)) != cmCancel)
		savePath = trimAscii(savePathBuffer);
	if (!savePath.empty())
		savePath = ensureMrmacExtension(expandUserPath(savePath));

	macroName = makeRecordedMacroName(++recordedMacroCounter_);
	source << "$MACRO " << macroName;
	if (!keySpec.empty())
		source << " TO " << keySpec << " FROM EDIT";
	else
		source << " FROM EDIT";
	source << ";\n";
	source << "KEY_IN('" << recordedKeySequence_ << "');\n";
	source << "END_MACRO;\n";
	macroSource = source.str();

	if (!validateMacroSource(macroSource, validationError)) {
		messageBox(mfError | mfOKButton, "Recorded macro is invalid:\n\n%s", validationError.c_str());
		return;
	}

	if (!savePath.empty()) {
		if (!confirmOverwriteForPath("Overwrite", "Macro file exists. Overwrite?", savePath))
			return;
		if (!writeTextFile(savePath, macroSource)) {
			messageBox(mfError | mfOKButton, "Could not save recorded macro:\n%s", savePath.c_str());
			return;
		}
		std::string line = "Saved recorded macro to ";
		line += savePath;
		mrLogMessage(line.c_str());
	}

	if (!keySpec.empty()) {
		if (!savePath.empty())
			sessionPath = savePath;
		else {
			sessionPath = configuredTempDirectoryPath() + "/mr_recorded_" +
			              std::to_string(static_cast<long>(::getpid())) + "_" +
			              std::to_string(recordedMacroCounter_) + ".mrmac";
			if (!writeTextFile(sessionPath, macroSource)) {
				messageBox(mfError | mfOKButton, "Could not create session macro file.");
				return;
			}
			recordedSessionMacroFiles_.push_back(sessionPath);
		}

		if (!mrvmLoadMacroFile(sessionPath, &loadError)) {
			messageBox(mfError | mfOKButton, "Could not bind recorded macro:\n\n%s", loadError.c_str());
			return;
		}
		{
			std::string line = "Recorded macro bound to ";
			line += keySpec;
			line += " via ";
			line += sessionPath;
			mrLogMessage(line.c_str());
		}
	}

	summary = "Recording finalized.";
	if (!keySpec.empty())
		summary += "\nBound key: " + keySpec;
	if (!savePath.empty())
		summary += "\nSaved: " + savePath;
	messageBox(mfInformation | mfOKButton, "%s", summary.c_str());
}

void TMREditorApp::stopKeystrokeRecording() {
	keystrokeRecording_ = false;
	recordingMarkerVisible_ = false;
	syncRecordingUiState();
	mrLogMessage("Keystroke recording stopped.");
	finalizeKeystrokeRecording();
	recordedKeySequence_.clear();
}

void TMREditorApp::bootstrapIndexedMacroBindings() {
	std::size_t fileCount = 0;
	std::size_t bindingCount = 0;
	std::string directory = defaultMacroDirectoryPath();

	mrvmBootstrapBoundMacroIndex(directory, &fileCount, &bindingCount);
	indexedMacroWarmupLoadedFiles_ = 0;
	indexedMacroWarmupActive_ = (fileCount != 0);

	if (fileCount == 0) {
		mrLogMessage("Macro bootstrap: no bound .mrmac files found.");
		return;
	}

	{
		std::string line = "Macro bootstrap indexed ";
		line += std::to_string(bindingCount);
		line += " binding";
		if (bindingCount != 1)
			line += "s";
		line += " across ";
		line += std::to_string(fileCount);
		line += " file";
		if (fileCount != 1)
			line += "s";
		line += " in ";
		line += directory;
		line += ".";
		mrLogMessage(line.c_str());
	}
}

void TMREditorApp::warmIndexedMacroBindings() {
	std::string loadedPath;
	std::string failedPath;
	std::string errorText;

	if (!indexedMacroWarmupActive_)
		return;

	if (mrvmWarmLoadNextIndexedMacroFile(&loadedPath, &failedPath, &errorText)) {
		++indexedMacroWarmupLoadedFiles_;
		return;
	}

	if (!failedPath.empty()) {
		std::string line = "Macro warmup failed for ";
		line += failedPath;
		line += ": ";
		line += errorText.empty() ? std::string("unknown error") : errorText;
		mrLogMessage(line.c_str());
	}

	if (!mrvmHasPendingIndexedMacroWarmup()) {
		std::string line = "Macro warmup completed: ";
		line += std::to_string(indexedMacroWarmupLoadedFiles_);
		line += " file";
		if (indexedMacroWarmupLoadedFiles_ != 1)
			line += "s";
		line += " loaded.";
		mrLogMessage(line.c_str());
		indexedMacroWarmupActive_ = false;
	}
}

void TMREditorApp::handleEvent(TEvent &event) {
	if (isRecorderToggleCommand(event)) {
		if (keystrokeRecording_)
			stopKeystrokeRecording();
		else
			startKeystrokeRecording();
		clearEvent(event);
		return;
	}
	if (isRecorderToggleKey(event)) {
		if (keystrokeRecording_)
			stopKeystrokeRecording();
		else
			startKeystrokeRecording();
		clearEvent(event);
		return;
	}
	if (keystrokeRecording_ && event.what == evKeyDown)
		appendRecordedKeyEvent(event);

	if (event.what == evCommand && event.message.command == cmQuit)
		prepareForQuit();
	TApplication::handleEvent(event);

	if (event.what != evCommand)
		return;
	if (handleMRCommand(event.message.command))
		clearEvent(event);
}

void TMREditorApp::idle() {
	TApplication::idle();
	pumpForegroundMacroDelays();
	updateRecordingBlink();
	warmIndexedMacroBindings();
	mr::coprocessor::globalCoprocessor().pump(8);
	if (auto *mrMenuBar = dynamic_cast<TMRMenuBar *>(menuBar)) {
		mr::messageline::VisibleMessage message;
		unsigned char cursorStatusColor = 0;
		std::string rightStatus = buildTopRightCursorStatus();
		mrMenuBar->setRightStatus(rightStatus, currentCursorStatusColorOverride(cursorStatusColor),
		                         TColorAttr(cursorStatusColor));
		if (mr::messageline::currentVisibleMessage(message)) {
			TMRMenuBar::MarqueeKind marqueeKind = mapMessageNoticeKind(message.kind);
			if (isHeroVisibleMessage(message))
				marqueeKind = TMRMenuBar::MarqueeKind::Hero;
			mrMenuBar->setAutoMarqueeStatus(message.text, marqueeKind);
		}
		else
			mrMenuBar->setAutoMarqueeStatus(std::string());
		mrMenuBar->tickMarquee();
	}
	updateAppCommandState();
}

TPalette &TMREditorApp::getPalette() const {
	static const TPalette &basePalette = extendedAppBasePalette();
	static TPalette palette = basePalette;
	unsigned char overrideValue = 0;
	int slot = 0;

	// Rebuild from TV default on every call so stale overrides never leak between frames.
	palette = basePalette;

	for (slot = 1; slot <= kMrPaletteMax; ++slot)
		if (configuredColorSlotOverride(static_cast<unsigned char>(slot), overrideValue))
			palette[slot] = overrideValue;

	// TVision-wide policy: Dialog scrollbars follow dialog frame color globally.
	// Applies to gray/blue/cyan dialog palette blocks, no per-view exceptions.
	auto syncDialogScrollbarsToFrame = [&](int base) {
		palette[base + 3] = palette[base + 0];  // slot 4: scrollbar page
		palette[base + 4] = palette[base + 0];  // slot 5: scrollbar controls
		palette[base + 23] = palette[base + 0]; // slot 24: history scrollbar page
		palette[base + 24] = palette[base + 0]; // slot 25: history scrollbar controls
	};
	// Blue/cyan/gray window scrollbars should not drift away from the window frame.
	auto syncWindowScrollbarsToFrame = [&](int base) {
		palette[base + 2] = palette[base + 0]; // slot 3: scrollbar page
		palette[base + 3] = palette[base + 0]; // slot 4: scrollbar controls / thumb
	};
	syncDialogScrollbarsToFrame(32);
	syncDialogScrollbarsToFrame(64);
	syncDialogScrollbarsToFrame(96);
	syncWindowScrollbarsToFrame(8);
	syncWindowScrollbarsToFrame(16);
	syncWindowScrollbarsToFrame(24);

	palette[1] = currentPalette.desktop;
	return palette;
}

bool mrApplySettingsSourceForTesting(const std::string &source, std::string *errorMessage) {
	return applySettingsSource(source, errorMessage);
}

bool mrMigrateSettingsMacroToCurrentVersionForTesting(const std::string &settingsPath,
                                                      const std::string &source,
                                                      const std::string &reason,
                                                      std::string *errorMessage) {
	return migrateSettingsMacroToCurrentVersion(settingsPath, source, reason, errorMessage);
}
