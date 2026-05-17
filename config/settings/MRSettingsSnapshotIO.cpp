#include "../../app/MRVersion.hpp"
#include "../../app/utils/MRFileIOUtils.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "MRSettingsEditSetup.hpp"
#include "MRSettingsHistory.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "MRSettingsSnapshotIO.hpp"
#include "MRSettingsStorage.hpp"
#include "MRSettingsThemesProfiles.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

namespace {

static const char *const kWindowColorThemeProfileKey = "WINDOW_COLORTHEME_URI";
static const char *const kSearchTextTypeLiteral = "LITERAL";
static const char *const kSearchTextTypePcre = "PCRE";
static const char *const kSearchTextTypeWord = "WORD";
static const char *const kSearchDirectionForward = "FORWARD";
static const char *const kSearchDirectionBackward = "BACKWARD";
static const char *const kSearchModeStopFirst = "STOP_FIRST_OCCURRENCE";
static const char *const kSearchModePromptNext = "PROMPT_FOR_NEXT_MATCH";
static const char *const kSearchModeListAll = "LIST_ALL_OCCURRENCES";
static const char *const kSarModeReplaceFirst = "REPLACE_FIRST_OCCURRENCE";
static const char *const kSarModePromptEach = "PROMPT_FOR_EACH_REPLACE";
static const char *const kSarModeReplaceAll = "REPLACE_ALL_OCCURRENCES";
static const char *const kSarLeaveCursorEnd = "END_OF_REPLACE_STRING";
static const char *const kSarLeaveCursorStart = "START_OF_REPLACE_STRING";
static const char *const kLogHandlingVolatile = "VOLATILE";
static const char *const kLogHandlingPersist = "PERSIST";
static const char *const kLogHandlingJournalctl = "JOURNALCTL";
static const char *const kCursorBehaviourBoundToText = "BOUND_TO_TEXT";
static const char *const kCursorBehaviourFreeMovement = "FREE_MOVEMENT";
static const char *const kDialogLastPathKey = "DIALOG_LAST_PATH";
static const char *const kDialogPathHistoryKey = "DIALOG_PATH_HISTORY";
static const char *const kDialogFileHistoryKey = "DIALOG_FILE_HISTORY";

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

std::string formatLogHandlingLiteral(MRLogHandling handling) {
	switch (handling) {
		case MRLogHandling::Volatile:
			return kLogHandlingVolatile;
		case MRLogHandling::Persist:
			return kLogHandlingPersist;
		case MRLogHandling::Journalctl:
			return kLogHandlingJournalctl;
	}
	return kLogHandlingVolatile;
}

std::string formatCursorBehaviourLiteral(MRCursorBehaviour behaviour) {
	return behaviour == MRCursorBehaviour::FreeMovement ? kCursorBehaviourFreeMovement : kCursorBehaviourBoundToText;
}

std::string formatUiIndentStyleLiteral(MRUiIndentStyle style) {
	switch (style) {
		case MRUiIndentStyle::KandR4:
			return "K_AND_R4";
		case MRUiIndentStyle::Allman:
			return "ALLMAN";
		case MRUiIndentStyle::Gnome:
			return "GNOME";
		case MRUiIndentStyle::Whitesmiths:
			return "WHITESMITHS";
		case MRUiIndentStyle::Horstmann:
			return "HORSTMANN";
		case MRUiIndentStyle::KandR:
		default:
			return "K_AND_R";
	}
}

std::string formatSearchTextType(MRSearchTextType value) {
	if (value == MRSearchTextType::Word) return kSearchTextTypeWord;
	if (value == MRSearchTextType::Pcre) return kSearchTextTypePcre;
	return kSearchTextTypeLiteral;
}

std::string formatSearchDirection(MRSearchDirection value) {
	return value == MRSearchDirection::Backward ? kSearchDirectionBackward : kSearchDirectionForward;
}

std::string formatSearchMode(MRSearchMode value) {
	if (value == MRSearchMode::PromptNext) return kSearchModePromptNext;
	if (value == MRSearchMode::ListAll) return kSearchModeListAll;
	return kSearchModeStopFirst;
}

std::string formatSarMode(MRSarMode value) {
	if (value == MRSarMode::PromptEach) return kSarModePromptEach;
	if (value == MRSarMode::ReplaceAll) return kSarModeReplaceAll;
	return kSarModeReplaceFirst;
}

std::string formatSarLeaveCursor(MRSarLeaveCursor value) {
	return value == MRSarLeaveCursor::StartOfReplaceString ? kSarLeaveCursorStart : kSarLeaveCursorEnd;
}

void trimHistoryToLimit(std::vector<std::string> &entries, int limit) {
	if (limit < 0) limit = 0;
	if (entries.size() > static_cast<std::size_t>(limit)) entries.resize(static_cast<std::size_t>(limit));
}

void addHistoryEntry(std::vector<std::string> &entries, const std::string &value, int limit) {
	if (value.empty()) return;
	entries.erase(std::remove(entries.begin(), entries.end(), value), entries.end());
	entries.insert(entries.begin(), value);
	trimHistoryToLimit(entries, limit);
}

std::string fallbackRememberedLoadDirectory(const MRSettingsSnapshot &snapshot) {
	std::string macroDir = makeAbsolutePath(snapshot.paths.macroPath);
	std::string cwd = currentWorkingDirectory();

	if (isReadableDirectory(macroDir)) return macroDir;
	if (isReadableDirectory(cwd)) return cwd;
	return std::string();
}

std::string appendFileName(std::string_view directory, const char *fileName) {
	std::string base(directory);

	if (fileName == nullptr || *fileName == '\0') return base;
	if (base.empty()) return std::string(fileName);
	if (base.back() != '/') base.push_back('/');
	base += fileName;
	return base;
}

std::string builtInTempDirectoryPath() {
	std::string cwd;

	if (isWritableDirectory("/tmp")) return "/tmp";
	cwd = currentWorkingDirectory();
	if (!cwd.empty() && isWritableDirectory(cwd)) return cwd;
	return "/tmp";
}

void accumulateSettingsChangeCounts(const std::vector<MRSettingsChangeEntry> &changes, std::size_t &addedCount, std::size_t &removedCount, std::size_t &changedCount) {
	addedCount = 0;
	removedCount = 0;
	changedCount = 0;
	for (const MRSettingsChangeEntry &change : changes)
		if (change.kind == MRSettingsChangeEntry::Kind::Added) ++addedCount;
		else if (change.kind == MRSettingsChangeEntry::Kind::Removed)
			++removedCount;
		else
			++changedCount;
}

std::string escapeMrmacSingleQuotedLiteral(const std::string &value) {
	std::string out;
	out.reserve(value.size() + 8);
	for (char ch : value) {
		if (ch == '\'') out += "''";
		else
			out.push_back(ch);
	}
	return out;
}

std::string escapePayloadQuotedString(std::string_view value) {
	std::string escaped;

	escaped.reserve(value.size() + 8);
	for (const char ch : value)
		switch (ch) {
			case '"':
				escaped += "\\\"";
				break;
			case '\\':
				escaped += "\\\\";
				break;
			case '\n':
				escaped += "\\n";
				break;
			case '\r':
				escaped += "\\r";
				break;
			case '\t':
				escaped += "\\t";
				break;
			default:
				escaped.push_back(ch);
				break;
		}
	return escaped;
}

std::string serializeScopedHistoryRecord(std::string_view key, MRDialogHistoryScope scope, std::string_view valueMemberName, std::string_view value) {
	std::string payload = "scope=\"";
	payload += escapePayloadQuotedString(dialogHistoryScopeName(scope));
	payload += "\" ";
	payload += valueMemberName;
	payload += "=\"";
	payload += escapePayloadQuotedString(value);
	payload += '"';
	return "MRSETUP('" + std::string(key) + "', '" + escapeMrmacSingleQuotedLiteral(payload) + "');\n";
}

} // namespace

std::string defaultLogFilePathForSettings(std::string_view settingsPath) {
	std::string dir = directoryPartOf(makeAbsolutePath(std::string(settingsPath)));
	if (dir.empty() || !isWritableDirectory(dir)) dir = builtInTempDirectoryPath();
	return appendFileName(dir, "mr.log");
}

bool ensureDirectoryTree(const std::string &directoryPath, std::string *errorMessage) {
	struct stat st;
	std::string parentPath;

	if (directoryPath.empty() || directoryPath == "." || directoryPath == "/") {
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (::stat(directoryPath.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		}
		if (errorMessage != nullptr) *errorMessage = "Path exists and is not a path container: " + directoryPath;
		return false;
	}
	parentPath = directoryPartOf(directoryPath);
	if (!parentPath.empty() && parentPath != directoryPath)
		if (!ensureDirectoryTree(parentPath, errorMessage)) return false;
	if (::mkdir(directoryPath.c_str(), 0755) != 0 && errno != EEXIST) {
		if (errorMessage != nullptr) *errorMessage = "Unable to create path container: " + directoryPath;
		return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool writeNormalizedBootstrapFiles(const MRSettingsSnapshot &snapshot, std::string_view previousSource, const std::string &canonicalSource, std::string *errorMessage) {
	const std::string settingsPath = normalizeConfiguredPathInput(snapshot.paths.settingsMacroUri);
	const std::filesystem::path settingsDir = std::filesystem::path(settingsPath).parent_path();
	static const std::regex workspacePattern(R"(MRSETUP\s*\(\s*'WORKSPACE'\s*,\s*'((?:''|[^'])*)'\s*\)\s*;?)", std::regex_constants::ECMAScript | std::regex_constants::icase);
	std::string finalSource = canonicalSource;
	std::string previousText(previousSource);
	std::smatch match;
	std::string workspaceLines;
	std::error_code ec;

	while (std::regex_search(previousText, match, workspacePattern)) {
		workspaceLines += "MRSETUP('WORKSPACE', '";
		workspaceLines += match[1].str();
		workspaceLines += "');\n";
		previousText = match.suffix().str();
	}
	if (!workspaceLines.empty()) {
		const std::size_t endMacro = finalSource.rfind("END_MACRO;");

		if (endMacro != std::string::npos) finalSource.insert(endMacro, workspaceLines);
	}

	if (!validateSettingsMacroFilePath(settingsPath, errorMessage)) return false;
	if (!settingsDir.empty()) {
		std::filesystem::create_directories(settingsDir, ec);
		if (ec) {
			if (errorMessage != nullptr) *errorMessage = "Unable to create settings directory: " + settingsDir.string();
			return false;
		}
	}
	if (!writeTextFile(settingsPath, finalSource)) {
		if (errorMessage != nullptr) *errorMessage = "Unable to write settings macro file: " + settingsPath;
		return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setSnapshotScopedDialogLastPath(MRSettingsSnapshot &snapshot, MRDialogHistoryScope scope, const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	std::string directory;
	MRSettingsSnapshot::DialogHistoryState &state = snapshot.dialogHistory[dialogHistoryScopeIndex(scope)];

	if (!normalized.empty() && isReadableDirectory(normalized)) {
		state.lastPath = normalized;
		addHistoryEntry(state.pathHistory, normalized, snapshot.maxPathHistory);
	} else if (!normalized.empty()) {
		addHistoryEntry(state.fileHistory, normalized, snapshot.maxFileHistory);
		directory = normalizedDialogDirectoryFromPath(normalized);
		if (!directory.empty()) {
			state.lastPath = directory;
			addHistoryEntry(state.pathHistory, directory, snapshot.maxPathHistory);
		}
	} else if (state.lastPath.empty()) {
		directory = fallbackRememberedLoadDirectory(snapshot);
		if (!directory.empty()) {
			state.lastPath = directory;
			addHistoryEntry(state.pathHistory, directory, snapshot.maxPathHistory);
		}
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setSnapshotPathHistoryLimit(MRSettingsSnapshot &snapshot, int value, std::string *errorMessage) {
	if (value < kHistoryLimitMin || value > kHistoryLimitMax) return setError(errorMessage, "MAX_PATH_HISTORY must be within 5..50.");
	snapshot.maxPathHistory = value;
	for (MRSettingsSnapshot::DialogHistoryState &state : snapshot.dialogHistory)
		trimHistoryToLimit(state.pathHistory, value);
	trimHistoryToLimit(snapshot.multiPathHistory, value);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setSnapshotFileHistoryLimit(MRSettingsSnapshot &snapshot, int value, std::string *errorMessage) {
	if (value < kHistoryLimitMin || value > kHistoryLimitMax) return setError(errorMessage, "MAX_FILE_HISTORY must be within 5..50.");
	snapshot.maxFileHistory = value;
	for (MRSettingsSnapshot::DialogHistoryState &state : snapshot.dialogHistory)
		trimHistoryToLimit(state.fileHistory, value);
	trimHistoryToLimit(snapshot.multiFilespecHistory, value);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setSnapshotEditProfiles(MRSettingsSnapshot &snapshot, const std::vector<MREditExtensionProfile> &profiles, std::string *errorMessage) {
	std::vector<MREditExtensionProfile> normalized = profiles;

	for (MREditExtensionProfile &profile : normalized) {
		profile.id = canonicalEditProfileId(profile.id);
		profile.name = canonicalEditProfileName(profile.name);
		profile.windowColorThemeUri = canonicalWindowColorThemeUri(profile.windowColorThemeUri);
		if (!normalizeEditExtensionSelectorsInPlace(profile.extensions, errorMessage)) return false;
		if (!normalizeEditProfileOverridesInPlace(profile, errorMessage)) return false;
	}
	if (!validateNormalizedEditProfiles(normalized, errorMessage)) return false;
	snapshot.editProfiles = std::move(normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRSettingsSnapshot captureConfiguredSettingsSnapshot(const MRSetupPaths &paths) {
	MRSettingsSnapshot snapshot;

	snapshot.paths = paths;
	snapshot.windowManagerEnabled = configuredWindowManager();
	snapshot.menulineMessagesEnabled = configuredMenulineMessages();
	snapshot.searchDialogOptions = configuredSearchDialogOptions();
	snapshot.sarDialogOptions = configuredSarDialogOptions();
	snapshot.multiSearchDialogOptions = configuredMultiSearchDialogOptions();
	snapshot.multiSarDialogOptions = configuredMultiSarDialogOptions();
	snapshot.pdfExportSettings = configuredPdfExportSettings();
	snapshot.acquireSettings = configuredAcquireSettings();
	snapshot.virtualDesktops = configuredVirtualDesktops();
	snapshot.cyclicVirtualDesktops = configuredCyclicVirtualDesktops();
	snapshot.cursorBehaviour = configuredCursorBehaviour();
	snapshot.uiIndentStyle = configuredUiIndentStyle();
	snapshot.cursorPositionMarker = configuredCursorPositionMarker();
	snapshot.autoloadWorkspace = configuredAutoloadWorkspace();
	snapshot.logHandling = configuredLogHandling();
	snapshot.logFilePath = configuredLogFilePath();
	configuredAutoexecMacroEntries(snapshot.autoexecMacros);
	snapshot.maxPathHistory = configuredMaxPathHistory();
	snapshot.maxFileHistory = configuredMaxFileHistory();
	snapshot.defaultProfileDescription = configuredDefaultProfileDescription();
	snapshot.editSettings = configuredEditSetupSettings();
	snapshot.colorSettings = configuredColorSetupSettings();
	snapshot.editProfiles = configuredEditExtensionProfiles();
	snapshot.keymapProfiles = configuredKeymapProfiles();
	snapshot.activeKeymapProfile = configuredActiveKeymapProfile();

	for (std::size_t i = 0; i < static_cast<std::size_t>(MRDialogHistoryScope::Count); ++i) {
		const MRDialogHistoryScope scope = static_cast<MRDialogHistoryScope>(i);
		const MRScopedDialogHistoryState &configuredState = dialogHistoryState(scope);
		MRSettingsSnapshot::DialogHistoryState &state = snapshot.dialogHistory[i];

		state.lastPath = configuredState.lastPath;
		for (const MRDialogHistoryEntry &entry : configuredState.pathHistory)
			state.pathHistory.push_back(entry.value);
		for (const MRDialogHistoryEntry &entry : configuredState.fileHistory)
			state.fileHistory.push_back(entry.value);
	}
	configuredMultiFilespecHistoryEntries(snapshot.multiFilespecHistory);
	configuredMultiPathHistoryEntries(snapshot.multiPathHistory);
	return snapshot;
}

void populateSettingsWriteReport(const std::string &settingsPath, const std::string &beforeSource, const std::string &afterSource, MRSettingsWriteReport *report) {
	std::vector<MRSettingsChangeEntry> changes;

	if (report == nullptr) return;
	*report = MRSettingsWriteReport();
	report->settingsPath = settingsPath;
	report->fileWritten = true;
	report->contentChanged = beforeSource != afterSource;
	if (!diffSettingsSources(beforeSource, afterSource, changes, nullptr)) return;
	accumulateSettingsChangeCounts(changes, report->addedCount, report->removedCount, report->changedCount);
	if (report->contentChanged && !changes.empty()) {
		report->logLines.push_back("settings.mrmac updated: " + std::to_string(report->changedCount) + " changed, " + std::to_string(report->addedCount) + " added, " + std::to_string(report->removedCount) + " removed.");
		for (const MRSettingsChangeEntry &change : changes)
			report->logLines.push_back(formatSettingsChangeForLog(change));
	} else if (report->contentChanged)
		report->logLines.push_back("settings.mrmac rewritten without semantic change.");
}

bool resetSettingsSnapshot(const std::string &settingsPath, MRSettingsSnapshot &snapshot, std::string *errorMessage) {
	MRSetupPaths paths = resolveSetupPathDefaults();
	std::string normalized;

	snapshot = MRSettingsSnapshot();
	snapshot.paths = paths;
	normalized = normalizeConfiguredPathInput(settingsPath);
	if (!validateSettingsMacroFilePath(normalized, errorMessage)) return false;
	snapshot.paths.settingsMacroUri = makeAbsolutePath(normalized);
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupSettingsMacro, snapshot.paths.settingsMacroUri, errorMessage)) return false;
	normalized = normalizeConfiguredPathInput(paths.macroPath);
	if (!validateMacroDirectoryPath(paths.macroPath, errorMessage)) return false;
	snapshot.paths.macroPath = makeAbsolutePath(normalized);
	if (snapshot.dialogHistory[dialogHistoryScopeIndex(MRDialogHistoryScope::General)].pathHistory.empty() && isReadableDirectory(snapshot.paths.macroPath))
		addHistoryEntry(snapshot.dialogHistory[dialogHistoryScopeIndex(MRDialogHistoryScope::General)].pathHistory, snapshot.paths.macroPath, snapshot.maxPathHistory);
	normalized = normalizeConfiguredPathInput(paths.helpUri);
	if (!validateHelpFilePath(paths.helpUri, errorMessage)) return false;
	snapshot.paths.helpUri = makeAbsolutePath(normalized);
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupHelpFile, snapshot.paths.helpUri, errorMessage)) return false;
	normalized = normalizeConfiguredPathInput(paths.tempPath);
	if (!validateTempDirectoryPath(paths.tempPath, errorMessage)) return false;
	snapshot.paths.tempPath = makeAbsolutePath(normalized);
	normalized = normalizeConfiguredPathInput(paths.shellUri);
	if (!validateShellExecutablePath(paths.shellUri, errorMessage)) return false;
	snapshot.paths.shellUri = makeAbsolutePath(normalized);
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupShellExecutable, snapshot.paths.shellUri, errorMessage)) return false;
	normalized = normalizeConfiguredPathInput(defaultLogFilePathForSettings(snapshot.paths.settingsMacroUri));
	if (!validateLogFilePath(normalized, errorMessage)) return false;
	snapshot.logFilePath = makeAbsolutePath(normalized);
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupLogFile, snapshot.logFilePath, errorMessage)) return false;
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::General, snapshot.paths.macroPath, errorMessage)) return false;
	snapshot.defaultProfileDescription = "Global defaults";
	snapshot.editSettings = resolveEditSetupDefaults();
	snapshot.colorSettings = resolveColorSetupDefaults();
	if (!setSnapshotEditProfiles(snapshot, std::vector<MREditExtensionProfile>(), errorMessage)) return false;
	snapshot.keymapFilePath.clear();
	normalized = normalizeConfiguredPathInput(defaultColorThemePathForSettings(snapshot.paths.settingsMacroUri));
	if (!validateColorThemeFilePath(normalized, errorMessage)) return false;
	snapshot.colorThemeFilePath = makeAbsolutePath(normalized);
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupThemeLoad, snapshot.colorThemeFilePath, errorMessage)) return false;
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupThemeSave, snapshot.colorThemeFilePath, errorMessage)) return false;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string buildSettingsMacroSource(const MRSettingsSnapshot &snapshot) {
	std::string settingsPath = normalizeConfiguredPathInput(snapshot.paths.settingsMacroUri);
	std::string macroDir = normalizeConfiguredPathInput(snapshot.paths.macroPath);
	std::string helpPath = normalizeConfiguredPathInput(snapshot.paths.helpUri);
	std::string tempDir = normalizeConfiguredPathInput(snapshot.paths.tempPath);
	std::string shellPath = normalizeConfiguredPathInput(snapshot.paths.shellUri);
	const MREditSetupSettings &edit = snapshot.editSettings;
	std::string source;
	std::size_t descriptorCount = 0;
	const MREditSettingDescriptor *descriptors = editSettingDescriptors(descriptorCount);

	source += "$MACRO MR_SETTINGS FROM EDIT;\n";
	source += "MRSETUP('" + std::string(mrSettingsVersionSetupKey()) + "', '" + escapeMrmacSingleQuotedLiteral(mrCurrentPersistenceVersionString()) + "');\n";
	source += "MRSETUP('SETTINGSPATH', '" + escapeMrmacSingleQuotedLiteral(settingsPath) + "');\n";
	source += "MRSETUP('MACROPATH', '" + escapeMrmacSingleQuotedLiteral(macroDir) + "');\n";
	source += "MRSETUP('HELPPATH', '" + escapeMrmacSingleQuotedLiteral(helpPath) + "');\n";
	source += "MRSETUP('TEMPDIR', '" + escapeMrmacSingleQuotedLiteral(tempDir) + "');\n";
	source += "MRSETUP('SHELLPATH', '" + escapeMrmacSingleQuotedLiteral(shellPath) + "');\n";
	source += "MRSETUP('WINDOW_MANAGER', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.windowManagerEnabled)) + "');\n";
	source += "MRSETUP('MESSAGES', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.menulineMessagesEnabled)) + "');\n";
	source += "MRSETUP('SEARCH_TEXT_TYPE', '" + escapeMrmacSingleQuotedLiteral(formatSearchTextType(snapshot.searchDialogOptions.textType)) + "');\n";
	source += "MRSETUP('SEARCH_DIRECTION', '" + escapeMrmacSingleQuotedLiteral(formatSearchDirection(snapshot.searchDialogOptions.direction)) + "');\n";
	source += "MRSETUP('SEARCH_MODE', '" + escapeMrmacSingleQuotedLiteral(formatSearchMode(snapshot.searchDialogOptions.mode)) + "');\n";
	source += "MRSETUP('SEARCH_CASE_SENSITIVE', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.searchDialogOptions.caseSensitive)) + "');\n";
	source += "MRSETUP('SEARCH_GLOBAL_SEARCH', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.searchDialogOptions.globalSearch)) + "');\n";
	source += "MRSETUP('SEARCH_RESTRICT_MARKED_BLOCK', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.searchDialogOptions.restrictToMarkedBlock)) + "');\n";
	source += "MRSETUP('SEARCH_ALL_WINDOWS', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.searchDialogOptions.searchAllWindows)) + "');\n";
	source += "MRSETUP('SAR_TEXT_TYPE', '" + escapeMrmacSingleQuotedLiteral(formatSearchTextType(snapshot.sarDialogOptions.textType)) + "');\n";
	source += "MRSETUP('SAR_DIRECTION', '" + escapeMrmacSingleQuotedLiteral(formatSearchDirection(snapshot.sarDialogOptions.direction)) + "');\n";
	source += "MRSETUP('SAR_MODE', '" + escapeMrmacSingleQuotedLiteral(formatSarMode(snapshot.sarDialogOptions.mode)) + "');\n";
	source += "MRSETUP('SAR_LEAVE_CURSOR_AT', '" + escapeMrmacSingleQuotedLiteral(formatSarLeaveCursor(snapshot.sarDialogOptions.leaveCursorAt)) + "');\n";
	source += "MRSETUP('SAR_CASE_SENSITIVE', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.sarDialogOptions.caseSensitive)) + "');\n";
	source += "MRSETUP('SAR_GLOBAL_SEARCH', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.sarDialogOptions.globalSearch)) + "');\n";
	source += "MRSETUP('SAR_RESTRICT_MARKED_BLOCK', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.sarDialogOptions.restrictToMarkedBlock)) + "');\n";
	source += "MRSETUP('SAR_ALL_WINDOWS', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.sarDialogOptions.searchAllWindows)) + "');\n";
	source += "MRSETUP('MULTI_SEARCH_FILESPEC', '" + escapeMrmacSingleQuotedLiteral(snapshot.multiSearchDialogOptions.filespec) + "');\n";
	source += "MRSETUP('MULTI_SEARCH_TEXT', '" + escapeMrmacSingleQuotedLiteral(snapshot.multiSearchDialogOptions.searchText) + "');\n";
	source += "MRSETUP('MULTI_SEARCH_STARTING_PATH', '" + escapeMrmacSingleQuotedLiteral(normalizeConfiguredPathInput(snapshot.multiSearchDialogOptions.startingPath)) + "');\n";
	source += "MRSETUP('MULTI_SEARCH_SUBDIRECTORIES', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSearchDialogOptions.searchSubdirectories)) + "');\n";
	source += "MRSETUP('MULTI_SEARCH_CASE_SENSITIVE', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSearchDialogOptions.caseSensitive)) + "');\n";
	source += "MRSETUP('MULTI_SEARCH_REGULAR_EXPRESSIONS', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSearchDialogOptions.regularExpressions)) + "');\n";
	source += "MRSETUP('MULTI_SEARCH_FILES_IN_MEMORY', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSearchDialogOptions.searchFilesInMemory)) + "');\n";
	source += "MRSETUP('MULTI_SAR_FILESPEC', '" + escapeMrmacSingleQuotedLiteral(snapshot.multiSarDialogOptions.filespec) + "');\n";
	source += "MRSETUP('MULTI_SAR_TEXT', '" + escapeMrmacSingleQuotedLiteral(snapshot.multiSarDialogOptions.searchText) + "');\n";
	source += "MRSETUP('MULTI_SAR_REPLACEMENT', '" + escapeMrmacSingleQuotedLiteral(snapshot.multiSarDialogOptions.replacementText) + "');\n";
	source += "MRSETUP('MULTI_SAR_STARTING_PATH', '" + escapeMrmacSingleQuotedLiteral(normalizeConfiguredPathInput(snapshot.multiSarDialogOptions.startingPath)) + "');\n";
	source += "MRSETUP('MULTI_SAR_SUBDIRECTORIES', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSarDialogOptions.searchSubdirectories)) + "');\n";
	source += "MRSETUP('MULTI_SAR_CASE_SENSITIVE', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSarDialogOptions.caseSensitive)) + "');\n";
	source += "MRSETUP('MULTI_SAR_REGULAR_EXPRESSIONS', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSarDialogOptions.regularExpressions)) + "');\n";
	source += "MRSETUP('MULTI_SAR_FILES_IN_MEMORY', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSarDialogOptions.searchFilesInMemory)) + "');\n";
	source += "MRSETUP('MULTI_SAR_KEEP_FILES_OPEN', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.multiSarDialogOptions.keepFilesOpen)) + "');\n";
	source += "MRSETUP('PDF_EXPORT_PATH', '" + escapeMrmacSingleQuotedLiteral(snapshot.pdfExportSettings.outputPath) + "');\n";
	source += "MRSETUP('PDF_EXPORT_PAGE_SEPARATOR', '" + escapeMrmacSingleQuotedLiteral(snapshot.pdfExportSettings.pageSeparatorLiteral) + "');\n";
	source += "MRSETUP('PDF_EXPORT_FONT_FAMILY', '" + escapeMrmacSingleQuotedLiteral(snapshot.pdfExportSettings.fontFamily) + "');\n";
	source += "MRSETUP('PDF_EXPORT_FONT_SIZE', '" + std::to_string(snapshot.pdfExportSettings.fontSizePoints) + "');\n";
	source += "MRSETUP('PDF_EXPORT_HEADER_LINE', '" + escapeMrmacSingleQuotedLiteral(snapshot.pdfExportSettings.headerLine) + "');\n";
	source += "MRSETUP('PDF_EXPORT_FOOTER_LINE', '" + escapeMrmacSingleQuotedLiteral(snapshot.pdfExportSettings.footerLine) + "');\n";
	source += "MRSETUP('PDF_EXPORT_TEXT_WIDTH', '" + escapeMrmacSingleQuotedLiteral(snapshot.pdfExportSettings.textWidth) + "');\n";
	source += "MRSETUP('PDF_EXPORT_LEFT_MARGIN_POINTS', '" + escapeMrmacSingleQuotedLiteral(snapshot.pdfExportSettings.leftMarginPoints) + "');\n";
	source += "MRSETUP('PDF_EXPORT_RIGHT_MARGIN_POINTS', '" + escapeMrmacSingleQuotedLiteral(snapshot.pdfExportSettings.rightMarginPoints) + "');\n";
	source += "MRSETUP('PDF_EXPORT_TOP_MARGIN_POINTS', '" + escapeMrmacSingleQuotedLiteral(snapshot.pdfExportSettings.topMarginPoints) + "');\n";
	source += "MRSETUP('PDF_EXPORT_BOTTOM_MARGIN_POINTS', '" + escapeMrmacSingleQuotedLiteral(snapshot.pdfExportSettings.bottomMarginPoints) + "');\n";
	source += "MRSETUP('ACQUIRE_COMMAND', '" + escapeMrmacSingleQuotedLiteral(snapshot.acquireSettings.commandLine) + "');\n";
	for (const std::string &entry : snapshot.acquireSettings.commandHistory)
		source += "MRSETUP('ACQUIRE_COMMAND_HISTORY', '" + escapeMrmacSingleQuotedLiteral(entry) + "');\n";
	source += "MRSETUP('VIRTUAL_DESKTOPS', '" + std::to_string(snapshot.virtualDesktops) + "');\n";
	source += "MRSETUP('CYCLIC_VIRTUAL_DESKTOPS', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.cyclicVirtualDesktops)) + "');\n";
	source += "MRSETUP('CURSOR_BEHAVIOUR', '" + escapeMrmacSingleQuotedLiteral(formatCursorBehaviourLiteral(snapshot.cursorBehaviour)) + "');\n";
	source += "MRSETUP('UI_INDENT_STYLE', '" + escapeMrmacSingleQuotedLiteral(formatUiIndentStyleLiteral(snapshot.uiIndentStyle)) + "');\n";
	source += "MRSETUP('CURSOR_POSITION_MARKER', '" + escapeMrmacSingleQuotedLiteral(snapshot.cursorPositionMarker) + "');\n";
	source += "MRSETUP('AUTOLOAD_WORKSPACE', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(snapshot.autoloadWorkspace)) + "');\n";
	source += "MRSETUP('LOG_HANDLING', '" + escapeMrmacSingleQuotedLiteral(formatLogHandlingLiteral(snapshot.logHandling)) + "');\n";
	source += "MRSETUP('LOGFILE', '" + escapeMrmacSingleQuotedLiteral(snapshot.logFilePath) + "');\n";
	for (const std::string &autoexecMacro : snapshot.autoexecMacros)
		source += "MRSETUP('AUTOEXEC_MACRO', '" + escapeMrmacSingleQuotedLiteral(autoexecMacro) + "');\n";
	source += "MRSETUP('MAX_PATH_HISTORY', '" + std::to_string(snapshot.maxPathHistory) + "');\n";
	source += "MRSETUP('MAX_FILE_HISTORY', '" + std::to_string(snapshot.maxFileHistory) + "');\n";
	for (const MRDialogHistoryScopeSpec &scopeSpec : kDialogHistoryScopeSpecs) {
		const MRSettingsSnapshot::DialogHistoryState &state = snapshot.dialogHistory[dialogHistoryScopeIndex(scopeSpec.scope)];

		if (!state.lastPath.empty()) source += serializeScopedHistoryRecord(kDialogLastPathKey, scopeSpec.scope, "path", state.lastPath);
		for (const std::string &entry : state.pathHistory)
			source += serializeScopedHistoryRecord(kDialogPathHistoryKey, scopeSpec.scope, "value", entry);
		for (const std::string &entry : state.fileHistory)
			source += serializeScopedHistoryRecord(kDialogFileHistoryKey, scopeSpec.scope, "value", entry);
	}
	for (const std::string &entry : snapshot.multiFilespecHistory)
		source += "MRSETUP('MULTI_FILESPEC_HISTORY', '" + escapeMrmacSingleQuotedLiteral(entry) + "');\n";
	for (const std::string &entry : snapshot.multiPathHistory)
		source += "MRSETUP('MULTI_PATH_HISTORY', '" + escapeMrmacSingleQuotedLiteral(entry) + "');\n";
	source += "MRSETUP('DEFAULT_PROFILE_DESCRIPTION', '" + escapeMrmacSingleQuotedLiteral(snapshot.defaultProfileDescription) + "');\n";
	source += "MRSETUP('PAGE_BREAK', '" + escapeMrmacSingleQuotedLiteral(edit.pageBreak) + "');\n";
	source += "MRSETUP('WORD_DELIMITERS', '" + escapeMrmacSingleQuotedLiteral(edit.wordDelimiters) + "');\n";
	source += "MRSETUP('DEFAULT_EXTENSIONS', '" + escapeMrmacSingleQuotedLiteral(edit.defaultExtensions) + "');\n";
	source += "MRSETUP('TRUNCATE_SPACES', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.truncateSpaces)) + "');\n";
	source += "MRSETUP('EOF_CTRL_Z', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.eofCtrlZ)) + "');\n";
	source += "MRSETUP('EOF_CR_LF', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.eofCrLf)) + "');\n";
	source += "MRSETUP('TAB_EXPAND', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.tabExpand)) + "');\n";
	source += "MRSETUP('DISPLAY_TABS', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.displayTabs)) + "');\n";
	source += "MRSETUP('TAB_SIZE', '" + std::to_string(edit.tabSize) + "');\n";
	source += "MRSETUP('LEFT_MARGIN', '" + std::to_string(edit.leftMargin) + "');\n";
	source += "MRSETUP('RIGHT_MARGIN', '" + std::to_string(edit.rightMargin) + "');\n";
	source += "MRSETUP('FORMAT_RULER', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.formatRuler)) + "');\n";
	source += "MRSETUP('WORD_WRAP', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.wordWrap)) + "');\n";
	source += "MRSETUP('INDENT_STYLE', '" + escapeMrmacSingleQuotedLiteral(edit.indentStyle) + "');\n";
	source += "MRSETUP('CODE_LANGUAGE', '" + escapeMrmacSingleQuotedLiteral(edit.codeLanguage) + "');\n";
	source += "MRSETUP('CODE_COLORING', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.codeColoring)) + "');\n";
	source += "MRSETUP('CODE_FOLDING', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.codeFoldingFeature)) + "');\n";
	source += "MRSETUP('FILE_TYPE', '" + escapeMrmacSingleQuotedLiteral(edit.fileType) + "');\n";
	source += "MRSETUP('BINARY_RECORD_LENGTH', '" + std::to_string(edit.binaryRecordLength) + "');\n";
	source += "MRSETUP('POST_LOAD_MACRO', '" + escapeMrmacSingleQuotedLiteral(edit.postLoadMacro) + "');\n";
	source += "MRSETUP('PRE_SAVE_MACRO', '" + escapeMrmacSingleQuotedLiteral(edit.preSaveMacro) + "');\n";
	source += "MRSETUP('DEFAULT_PATH', '" + escapeMrmacSingleQuotedLiteral(edit.defaultPath) + "');\n";
	source += "MRSETUP('FORMAT_LINE', '" + escapeMrmacSingleQuotedLiteral(edit.formatLine) + "');\n";
	source += "MRSETUP('BACKUP_METHOD', '" + escapeMrmacSingleQuotedLiteral(edit.backupMethod) + "');\n";
	source += "MRSETUP('BACKUP_FREQUENCY', '" + escapeMrmacSingleQuotedLiteral(edit.backupFrequency) + "');\n";
	source += "MRSETUP('BACKUP_EXTENSION', '" + escapeMrmacSingleQuotedLiteral(edit.backupExtension) + "');\n";
	source += "MRSETUP('BACKUP_DIRECTORY', '" + escapeMrmacSingleQuotedLiteral(edit.backupDirectory) + "');\n";
	source += "MRSETUP('AUTOSAVE_INACTIVITY_SECONDS', '" + std::to_string(edit.autosaveInactivitySeconds) + "');\n";
	source += "MRSETUP('AUTOSAVE_INTERVAL_SECONDS', '" + std::to_string(edit.autosaveIntervalSeconds) + "');\n";
	source += "MRSETUP('BACKUP_FILES', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.backupFiles)) + "');\n";
	source += "MRSETUP('SHOW_EOF_MARKER', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.showEofMarker)) + "');\n";
	source += "MRSETUP('SHOW_EOF_MARKER_EMOJI', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.showEofMarkerEmoji)) + "');\n";
	source += "MRSETUP('LINE_NUMBERS_POSITION', '" + escapeMrmacSingleQuotedLiteral(edit.lineNumbersPosition) + "');\n";
	source += "MRSETUP('LINE_NUM_ZERO_FILL', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.lineNumZeroFill)) + "');\n";
	source += "MRSETUP('MINIMAP_POSITION', '" + escapeMrmacSingleQuotedLiteral(edit.miniMapPosition) + "');\n";
	source += "MRSETUP('MINIMAP_WIDTH', '" + std::to_string(edit.miniMapWidth) + "');\n";
	source += "MRSETUP('MINIMAP_MARKER_GLYPH', '" + escapeMrmacSingleQuotedLiteral(edit.miniMapMarkerGlyph) + "');\n";
	source += "MRSETUP('GUTTERS', '" + escapeMrmacSingleQuotedLiteral(edit.gutters) + "');\n";
	source += "MRSETUP('PERSISTENT_BLOCKS', '" + escapeMrmacSingleQuotedLiteral(formatEditSetupBoolean(edit.persistentBlocks)) + "');\n";
	source += "MRSETUP('CODE_FOLDING_POSITION', '" + escapeMrmacSingleQuotedLiteral(edit.codeFoldingPosition) + "');\n";
	source += "MRSETUP('COLUMN_BLOCK_MOVE', '" + escapeMrmacSingleQuotedLiteral(edit.columnBlockMove) + "');\n";
	source += "MRSETUP('DEFAULT_MODE', '" + escapeMrmacSingleQuotedLiteral(edit.defaultMode) + "');\n";
	source += "MRSETUP('CURSOR_STATUS_COLOR', '" + escapeMrmacSingleQuotedLiteral(edit.cursorStatusColor) + "');\n";

	for (const auto &profile : snapshot.editProfiles) {
		source += "MRFEPROFILE('DEFINE', '" + escapeMrmacSingleQuotedLiteral(profile.id) + "', '" + escapeMrmacSingleQuotedLiteral(profile.name) + "', '');\n";
		for (const std::string &ext : profile.extensions)
			source += "MRFEPROFILE('EXT', '" + escapeMrmacSingleQuotedLiteral(profile.id) + "', '" + escapeMrmacSingleQuotedLiteral(ext) + "', '');\n";
		if (!profile.windowColorThemeUri.empty()) source += "MRFEPROFILE('SET', '" + escapeMrmacSingleQuotedLiteral(profile.id) + "', '" + std::string(kWindowColorThemeProfileKey) + "', '" + escapeMrmacSingleQuotedLiteral(profile.windowColorThemeUri) + "');\n";
		for (std::size_t i = 0; i < descriptorCount; ++i)
			if (descriptors[i].profileSupported && (profile.overrides.mask & descriptors[i].overrideBit) != 0) {
				std::string value;

				if (std::string(descriptors[i].key) == "PAGE_BREAK") value = profile.overrides.values.pageBreak;
				else if (std::string(descriptors[i].key) == "WORD_DELIMITERS")
					value = profile.overrides.values.wordDelimiters;
				else if (std::string(descriptors[i].key) == "DEFAULT_EXTENSIONS")
					value = profile.overrides.values.defaultExtensions;
				else if (std::string(descriptors[i].key) == "TRUNCATE_SPACES")
					value = formatEditSetupBoolean(profile.overrides.values.truncateSpaces);
				else if (std::string(descriptors[i].key) == "EOF_CTRL_Z")
					value = formatEditSetupBoolean(profile.overrides.values.eofCtrlZ);
				else if (std::string(descriptors[i].key) == "EOF_CR_LF")
					value = formatEditSetupBoolean(profile.overrides.values.eofCrLf);
				else if (std::string(descriptors[i].key) == "TAB_EXPAND")
					value = formatEditSetupBoolean(profile.overrides.values.tabExpand);
				else if (std::string(descriptors[i].key) == "DISPLAY_TABS")
					value = formatEditSetupBoolean(profile.overrides.values.displayTabs);
				else if (std::string(descriptors[i].key) == "TAB_SIZE")
					value = std::to_string(profile.overrides.values.tabSize);
				else if (std::string(descriptors[i].key) == "LEFT_MARGIN")
					value = std::to_string(profile.overrides.values.leftMargin);
				else if (std::string(descriptors[i].key) == "RIGHT_MARGIN")
					value = std::to_string(profile.overrides.values.rightMargin);
				else if (std::string(descriptors[i].key) == "FORMAT_RULER")
					value = formatEditSetupBoolean(profile.overrides.values.formatRuler);
				else if (std::string(descriptors[i].key) == "WORD_WRAP")
					value = formatEditSetupBoolean(profile.overrides.values.wordWrap);
				else if (std::string(descriptors[i].key) == "INDENT_STYLE")
					value = profile.overrides.values.indentStyle;
				else if (std::string(descriptors[i].key) == "FILE_TYPE")
					value = profile.overrides.values.fileType;
				else if (std::string(descriptors[i].key) == "BINARY_RECORD_LENGTH")
					value = std::to_string(profile.overrides.values.binaryRecordLength);
				else if (std::string(descriptors[i].key) == "POST_LOAD_MACRO")
					value = profile.overrides.values.postLoadMacro;
				else if (std::string(descriptors[i].key) == "PRE_SAVE_MACRO")
					value = profile.overrides.values.preSaveMacro;
				else if (std::string(descriptors[i].key) == "DEFAULT_PATH")
					value = profile.overrides.values.defaultPath;
				else if (std::string(descriptors[i].key) == "FORMAT_LINE")
					value = profile.overrides.values.formatLine;
				else if (std::string(descriptors[i].key) == "BACKUP_FILES")
					value = formatEditSetupBoolean(profile.overrides.values.backupFiles);
				else if (std::string(descriptors[i].key) == "SHOW_EOF_MARKER")
					value = formatEditSetupBoolean(profile.overrides.values.showEofMarker);
				else if (std::string(descriptors[i].key) == "SHOW_EOF_MARKER_EMOJI")
					value = formatEditSetupBoolean(profile.overrides.values.showEofMarkerEmoji);
				else if (std::string(descriptors[i].key) == "LINE_NUMBERS_POSITION")
					value = profile.overrides.values.lineNumbersPosition;
				else if (std::string(descriptors[i].key) == "LINE_NUM_ZERO_FILL")
					value = formatEditSetupBoolean(profile.overrides.values.lineNumZeroFill);
				else if (std::string(descriptors[i].key) == "MINIMAP_POSITION")
					value = profile.overrides.values.miniMapPosition;
				else if (std::string(descriptors[i].key) == "MINIMAP_WIDTH")
					value = std::to_string(profile.overrides.values.miniMapWidth);
				else if (std::string(descriptors[i].key) == "MINIMAP_MARKER_GLYPH")
					value = profile.overrides.values.miniMapMarkerGlyph;
				else if (std::string(descriptors[i].key) == "GUTTERS")
					value = profile.overrides.values.gutters;
				else if (std::string(descriptors[i].key) == "PERSISTENT_BLOCKS")
					value = formatEditSetupBoolean(profile.overrides.values.persistentBlocks);
				else if (std::string(descriptors[i].key) == "CODE_FOLDING_POSITION")
					value = profile.overrides.values.codeFoldingPosition;
				else if (std::string(descriptors[i].key) == "COLUMN_BLOCK_MOVE")
					value = profile.overrides.values.columnBlockMove;
				else if (std::string(descriptors[i].key) == "DEFAULT_MODE")
					value = profile.overrides.values.defaultMode;
				else if (std::string(descriptors[i].key) == "CURSOR_STATUS_COLOR")
					value = profile.overrides.values.cursorStatusColor;

				source += "MRFEPROFILE('SET', '" + escapeMrmacSingleQuotedLiteral(profile.id) + "', '" + descriptors[i].key + "', '" + escapeMrmacSingleQuotedLiteral(value) + "');\n";
			}
	}
	source += "END_MACRO;\n";
	return source;
}
