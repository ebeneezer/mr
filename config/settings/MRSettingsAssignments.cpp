#include "../../app/commands/MRWindowCommands.hpp"
#include "../../app/MRVersion.hpp"
#include "../../app/utils/MRFileIOUtils.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "MRSettingsAssignments.hpp"
#include "MRSettingsAssignmentInternal.hpp"
#include "MRSettingsCompilerProfiles.hpp"
#include "MRSettingsEditSetup.hpp"
#include "MRSettingsHistory.hpp"
#include "MRSettingsRuntime.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "MRSettingsSnapshotIO.hpp"
#include "MRSettingsStorage.hpp"
#include "MRSettingsThemesProfiles.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <map>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

using namespace mr::settings_assignment;

bool resetConfiguredSettingsModel(const std::string &settingsPath, MRSetupPaths &paths, std::string *errorMessage) {
	std::array<MRScopedDialogHistoryState, static_cast<std::size_t>(MRDialogHistoryScope::Count)> dialogHistories;

	paths = resolveSetupPathDefaults();
	paths.settingsMacroUri = normalizeConfiguredPathInput(settingsPath);
	if (paths.settingsMacroUri.empty()) return setError(errorMessage, "Settings path is empty.");
	if (!setConfiguredSettingsMacroFilePath(paths.settingsMacroUri, errorMessage)) return false;
	if (!setConfiguredMacroDirectoryPath(paths.macroPath, errorMessage)) return false;
	if (!setConfiguredHelpFilePath(paths.helpUri, errorMessage)) return false;
	if (!setConfiguredTempDirectoryPath(paths.tempPath, errorMessage)) return false;
	if (!setConfiguredShellExecutablePath(paths.shellUri, errorMessage)) return false;
	if (!setConfiguredLogFilePath(defaultLogFilePathForSettings(paths.settingsMacroUri), errorMessage)) return false;
	if (!setConfiguredLastFileDialogPath(paths.macroPath, errorMessage)) return false;
	if (!setConfiguredDefaultProfileDescription("Global defaults", errorMessage)) return false;
	if (!setConfiguredSearchDialogOptions(MRSearchDialogOptions(), errorMessage)) return false;
	if (!setConfiguredSarDialogOptions(MRSarDialogOptions(), errorMessage)) return false;
	if (!setConfiguredMultiSearchDialogOptions(MRMultiSearchDialogOptions(), errorMessage)) return false;
	if (!setConfiguredMultiSarDialogOptions(MRMultiSarDialogOptions(), errorMessage)) return false;
	if (!setConfiguredPdfExportSettings(MRPdfExportSettings(), errorMessage)) return false;
	if (!setConfiguredAcquireSettings(MRAcquireSettings(), errorMessage)) return false;
	if (!setConfiguredLiveLogSettings(MRLiveLogSettings(), errorMessage)) return false;
	if (!setConfiguredAudioPlayerPath("", errorMessage)) return false;
	if (!setConfiguredCursorBehaviour(MRCursorBehaviour::BoundToText, errorMessage)) return false;
	if (!setConfiguredCompilerErrorMessagePlacement(MRCompilerErrorMessagePlacement::RightMargin, errorMessage)) return false;
	if (!setConfiguredScrollbarVisibility(MRScrollbarVisibility::Smart, errorMessage)) return false;
	if (!setConfiguredTrackCompilerWarnings(false, errorMessage)) return false;
	if (!setConfiguredTrackCompilerNotes(false, errorMessage)) return false;
	if (!setConfiguredUiIndentStyle(MRUiIndentStyle::KandR, errorMessage)) return false;
	if (!setConfiguredCursorPositionMarker("R:C", errorMessage)) return false;
	if (!setConfiguredFileCompareOriginalLeadingGutters("L", errorMessage)) return false;
	if (!setConfiguredFileCompareOriginalTrailingGutters("M", errorMessage)) return false;
	if (!setConfiguredFileCompareCompareLeadingGutters("LD", errorMessage)) return false;
	if (!setConfiguredFileCompareCompareTrailingGutters("", errorMessage)) return false;
	if (!setConfiguredFileCompareStartConfiguration(MRFileCompareStartConfiguration::OriginalCompare, errorMessage)) return false;
	if (!setConfiguredFileCompareComparePanelReadOnly(true, errorMessage)) return false;
	if (!setConfiguredAutosaveWorkspace(false, errorMessage)) return false;
	if (!setConfiguredAutoloadWorkspace(false, errorMessage)) return false;
	if (!setConfiguredLogHandling(MRLogHandling::Volatile, errorMessage)) return false;
	storeConfiguredAutoexecMacroStorage(std::vector<std::string>());
	if (!setConfiguredEditSetupSettings(resolveEditSetupDefaults(), errorMessage)) return false;
	storeConfiguredColorSettings(resolveColorSetupDefaults());
	storeConfiguredColorSettingsInitialized(true);
	storeConfiguredColorThemeDisplayNameValue(std::string());
	if (!setConfiguredCompilerProfiles(std::vector<MRCompilerProfile>(), errorMessage)) return false;
	if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(), errorMessage)) return false;
	if (!setConfiguredKeymapProfiles(std::vector<MRKeymapProfile>(), errorMessage)) return false;
	if (!setConfiguredKeymapFilePath("", errorMessage)) return false;
	if (!setConfiguredActiveKeymapProfile("", errorMessage)) return false;
	if (!setConfiguredColorThemeFilePath(defaultColorThemeFilePath(), errorMessage)) return false;
	storeConfiguredPathHistoryLimit(kHistoryLimitDefault);
	storeConfiguredFileHistoryLimit(kHistoryLimitDefault);
	storeConfiguredWorkspaceHistoryLimit(kHistoryLimitDefault);
	dialogHistories = configuredDialogHistoryStorage();
	for (MRScopedDialogHistoryState &state : dialogHistories) {
		state.lastPath.clear();
		state.pathHistory.clear();
		state.fileHistory.clear();
	}
	storeConfiguredDialogHistoryStorage(dialogHistories);
	storeConfiguredMultiFilespecHistoryStorage(std::vector<MRDialogHistoryEntry>());
	storeConfiguredMultiPathHistoryStorage(std::vector<MRDialogHistoryEntry>());
	storeConfiguredHistoryEpochCounter(std::max(static_cast<long long>(0), static_cast<long long>(std::time(nullptr))));
	clearConfiguredSettingsDirty();
	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = configuredMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool applyConfiguredSettingsAssignment(const std::string &key, const std::string &value, MRSetupPaths &paths, std::string *errorMessage) {
	switch (classifySettingsKey(key)) {
		case MRSettingsKeyClass::Unknown:
			return setError(errorMessage, "Unsupported MRSETUP key.");
		case MRSettingsKeyClass::Version:
			if (trimAscii(value) != mrCurrentPersistenceVersionString()) return setError(errorMessage, mrUnsupportedCurrentBuildVersionMessage("settings source"));
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		case MRSettingsKeyClass::Path: {
			std::string upper = upperAscii(trimAscii(key));
			if (upper == "SETTINGSPATH") {
				paths.settingsMacroUri = configuredSettingsMacroFilePath();
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MACROPATH") {
				if (!validateMacroDirectoryPath(value, errorMessage)) return false;
				if (!setConfiguredMacroDirectoryPath(value, errorMessage)) return false;
				paths.macroPath = normalizeConfiguredPathInput(value);
				return true;
			}
			if (upper == "HELPPATH") {
				if (!validateHelpFilePath(value, errorMessage)) return false;
				if (!setConfiguredHelpFilePath(value, errorMessage)) return false;
				paths.helpUri = normalizeConfiguredPathInput(value);
				return true;
			}
			if (upper == "TEMPDIR") {
				if (!validateTempDirectoryPath(value, errorMessage)) return false;
				if (!setConfiguredTempDirectoryPath(value, errorMessage)) return false;
				paths.tempPath = normalizeConfiguredPathInput(value);
				return true;
			}
			if (upper == "SHELLPATH") {
				if (!validateShellExecutablePath(value, errorMessage)) return false;
				if (!setConfiguredShellExecutablePath(value, errorMessage)) return false;
				paths.shellUri = normalizeConfiguredPathInput(value);
				return true;
			}
			break;
		}
		case MRSettingsKeyClass::Global: {
			std::string upper = upperAscii(trimAscii(key));
			if (upper == "WINDOW_MANAGER") {
				bool parsed = true;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredWindowManager(parsed, errorMessage);
			}
			if (upper == "MESSAGES") {
				bool parsed = true;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredMenulineMessages(parsed, errorMessage);
			}
			if (upper == "AUTODETECT_BINARY_FILES") {
				bool parsed = true;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredAutoDetectBinaryFiles(parsed, errorMessage);
			}
			if (upper == "SEARCH_TEXT_TYPE") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseSearchTextTypeLiteral(value, options.textType, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_DIRECTION") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseSearchDirectionLiteral(value, options.direction, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_MODE") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseSearchModeLiteral(value, options.mode, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_CASE_SENSITIVE") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.caseSensitive, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_GLOBAL_SEARCH") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.globalSearch, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_RESTRICT_MARKED_BLOCK") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.restrictToMarkedBlock, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_ALL_WINDOWS") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.searchAllWindows, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_LIST_ALL_OCCURRENCES") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				bool listAll = false;
				if (!parseBooleanLiteral(value, listAll, errorMessage)) return false;
				options.mode = listAll ? MRSearchMode::ListAll : MRSearchMode::StopFirst;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_TEXT_TYPE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseSearchTextTypeLiteral(value, options.textType, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_DIRECTION") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseSearchDirectionLiteral(value, options.direction, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_MODE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseSarModeLiteral(value, options.mode, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_LEAVE_CURSOR_AT") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseSarLeaveCursorLiteral(value, options.leaveCursorAt, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_CASE_SENSITIVE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseBooleanLiteral(value, options.caseSensitive, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_GLOBAL_SEARCH") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseBooleanLiteral(value, options.globalSearch, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_RESTRICT_MARKED_BLOCK") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseBooleanLiteral(value, options.restrictToMarkedBlock, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_ALL_WINDOWS") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseBooleanLiteral(value, options.searchAllWindows, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_REPLACE_MODE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				MRSarMode mode = MRSarMode::ReplaceFirst;
				if (!parseSarModeLiteral(value, mode, errorMessage)) return false;
				options.mode = mode == MRSarMode::ReplaceAll ? MRSarMode::ReplaceAll : MRSarMode::ReplaceFirst;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_PROMPT_EACH_REPLACE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				bool promptEach = false;
				if (!parseBooleanLiteral(value, promptEach, errorMessage)) return false;
				if (promptEach) options.mode = MRSarMode::PromptEach;
				else if (options.mode == MRSarMode::PromptEach)
					options.mode = MRSarMode::ReplaceFirst;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_FILESPEC") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				options.filespec = trimAscii(value);
				if (options.filespec.empty()) options.filespec = "*.*";
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_TEXT") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				options.searchText = value;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_STARTING_PATH") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				options.startingPath = normalizeConfiguredPathInput(value);
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_SUBDIRECTORIES") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.searchSubdirectories, errorMessage)) return false;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_CASE_SENSITIVE") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.caseSensitive, errorMessage)) return false;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_REGULAR_EXPRESSIONS") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.regularExpressions, errorMessage)) return false;
				if (options.regularExpressions) options.wholeWords = false;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_WHOLE_WORDS") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.wholeWords, errorMessage)) return false;
				if (options.wholeWords) options.regularExpressions = false;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_FILES_IN_MEMORY") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.searchFilesInMemory, errorMessage)) return false;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_RESTRICT_WORKSPACE") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.restrictToWorkspace, errorMessage)) return false;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_FILESPEC") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				options.filespec = trimAscii(value);
				if (options.filespec.empty()) options.filespec = "*.*";
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_TEXT") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				options.searchText = value;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_REPLACEMENT") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				options.replacementText = value;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_STARTING_PATH") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				options.startingPath = normalizeConfiguredPathInput(value);
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_SUBDIRECTORIES") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.searchSubdirectories, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_CASE_SENSITIVE") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.caseSensitive, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_REGULAR_EXPRESSIONS") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.regularExpressions, errorMessage)) return false;
				if (options.regularExpressions) options.wholeWords = false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_WHOLE_WORDS") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.wholeWords, errorMessage)) return false;
				if (options.wholeWords) options.regularExpressions = false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_FILES_IN_MEMORY") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.searchFilesInMemory, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_KEEP_FILES_OPEN") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.keepFilesOpen, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_RESTRICT_WORKSPACE") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.restrictToWorkspace, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "PDF_EXPORT_PATH") {
				MRPdfExportSettings settings = configuredPdfExportSettings();
				settings.outputPath = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_PAGE_SEPARATOR") {
				MRPdfExportSettings settings = configuredPdfExportSettings();
				settings.pageSeparatorLiteral = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_FONT_FAMILY") {
				MRPdfExportSettings settings = configuredPdfExportSettings();
				settings.fontFamily = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_FONT_SIZE") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 1 || parsed > 40) return setError(errorMessage, "PDF_EXPORT_FONT_SIZE must be within 1..40.");
				settings.fontSizePoints = static_cast<int>(parsed);
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_HEADER_LINE") {
				MRPdfExportSettings settings = configuredPdfExportSettings();
				settings.headerLine = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_FOOTER_LINE") {
				MRPdfExportSettings settings = configuredPdfExportSettings();
				settings.footerLine = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_USE_PRINT_MARGIN") {
				bool enabled = true;
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (!parseBooleanLiteral(value, enabled, errorMessage)) return false;
				if (!enabled) settings.textWidth = "0";
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_PRINT_MARGIN_COLUMNS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_TEXT_WIDTH must be within 0..9999.");
				if (trimAscii(settings.textWidth) == "0") {
					if (errorMessage != nullptr) errorMessage->clear();
					return true;
				}
				settings.textWidth = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_TEXT_WIDTH") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_TEXT_WIDTH must be within 0..9999.");
				settings.textWidth = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_LEFT_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_LEFT_MARGIN_POINTS must be within 0..9999.");
				settings.leftMarginPoints = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_RIGHT_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_RIGHT_MARGIN_POINTS must be within 0..9999.");
				settings.rightMarginPoints = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_TOP_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_TOP_MARGIN_POINTS must be within 0..9999.");
				settings.topMarginPoints = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_BOTTOM_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_BOTTOM_MARGIN_POINTS must be within 0..9999.");
				settings.bottomMarginPoints = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "ACQUIRE_COMMAND") {
				MRAcquireSettings settings = configuredAcquireSettings();
				settings.commandLine = value;
				return setConfiguredAcquireSettings(settings, errorMessage);
			}
			if (upper == "ACQUIRE_COMMAND_HISTORY") {
				MRAcquireSettings settings = configuredAcquireSettings();
				const std::string trimmed = trimAscii(value);

				if (!trimmed.empty()) settings.commandHistory.push_back(trimmed);
				return setConfiguredAcquireSettings(settings, errorMessage);
			}
			if (upper == "VIRTUAL_DESKTOPS") {
				int parsed = 1;
				try {
					parsed = std::stoi(value);
				} catch (...) {
					parsed = 1;
				}
				if (parsed < 1) parsed = 1;
				if (parsed > 9) parsed = 9;
				applyVirtualDesktopConfigurationChange(parsed);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "CYCLIC_VIRTUAL_DESKTOPS") {
				bool parsed = false;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredCyclicVirtualDesktops(parsed, errorMessage);
			}
			if (upper == "CURSOR_BEHAVIOUR") {
				MRCursorBehaviour behaviour = MRCursorBehaviour::BoundToText;
				if (!parseCursorBehaviourLiteral(value, behaviour, errorMessage)) return false;
				return setConfiguredCursorBehaviour(behaviour, errorMessage);
			}
			if (upper == "COMPILER_ERROR_MESSAGE_PLACEMENT") {
				MRCompilerErrorMessagePlacement placement = MRCompilerErrorMessagePlacement::RightMargin;
				if (!parseCompilerErrorMessagePlacementLiteral(value, placement, errorMessage)) return false;
				return setConfiguredCompilerErrorMessagePlacement(placement, errorMessage);
			}
			if (upper == "SCROLLBAR_VISIBILITY") {
				MRScrollbarVisibility visibility = MRScrollbarVisibility::Smart;
				if (!parseScrollbarVisibilityLiteral(value, visibility, errorMessage)) return false;
				return setConfiguredScrollbarVisibility(visibility, errorMessage);
			}
			if (upper == "TRACK_COMPILER_WARNINGS") {
				bool parsed = false;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredTrackCompilerWarnings(parsed, errorMessage);
			}
			if (upper == "TRACK_COMPILER_NOTES") {
				bool parsed = false;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredTrackCompilerNotes(parsed, errorMessage);
			}
				if (upper == "UI_INDENT_STYLE") {
					MRUiIndentStyle style = MRUiIndentStyle::KandR;
					if (!parseUiIndentStyleLiteral(value, style, errorMessage)) return false;
					return setConfiguredUiIndentStyle(style, errorMessage);
				}
				if (upper == "CURSOR_POSITION_MARKER") return setConfiguredCursorPositionMarker(value, errorMessage);
				if (upper == kWindowColorThemeProfileKey) return loadColorThemeFile(value, errorMessage);
				if (upper == "FILE_COMPARE_ORIGINAL_LEADING_GUTTERS") return setConfiguredFileCompareOriginalLeadingGutters(value, errorMessage);
			if (upper == "FILE_COMPARE_ORIGINAL_TRAILING_GUTTERS") return setConfiguredFileCompareOriginalTrailingGutters(value, errorMessage);
			if (upper == "FILE_COMPARE_COMPARE_LEADING_GUTTERS") return setConfiguredFileCompareCompareLeadingGutters(value, errorMessage);
			if (upper == "FILE_COMPARE_COMPARE_TRAILING_GUTTERS") return setConfiguredFileCompareCompareTrailingGutters(value, errorMessage);
			if (upper == "FILE_COMPARE_START_CONFIGURATION") {
				MRFileCompareStartConfiguration configuration = MRFileCompareStartConfiguration::OriginalCompare;
				if (!parseFileCompareStartConfigurationLiteral(value, configuration, errorMessage)) return false;
				return setConfiguredFileCompareStartConfiguration(configuration, errorMessage);
			}
			if (upper == "FILE_COMPARE_COMPARE_PANEL_READ_ONLY") {
				bool parsed = true;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredFileCompareComparePanelReadOnly(parsed, errorMessage);
			}
			if (upper == "AUTOSAVE_WORKSPACE") {
				bool parsed = false;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredAutosaveWorkspace(parsed, errorMessage);
			}
			if (upper == "AUTOLOAD_WORKSPACE") {
				bool parsed = false;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredAutoloadWorkspace(parsed, errorMessage);
			}
			if (upper == "LOG_HANDLING") {
				MRLogHandling handling = MRLogHandling::Volatile;
				if (!parseLogHandlingLiteral(value, handling, errorMessage)) return false;
				return setConfiguredLogHandling(handling, errorMessage);
			}
			if (upper == "LOGFILE") return setConfiguredLogFilePath(value, errorMessage);
			if (upper == "LIVE_LOG_REPORT_MESSAGE_LINE") {
				MRLiveLogSettings settings = configuredLiveLogSettings();
				if (!parseBooleanLiteral(value, settings.reportSearchHitsOnMessageLine, errorMessage)) return false;
				return setConfiguredLiveLogSettings(settings, errorMessage);
			}
			if (upper == "LIVE_LOG_REPORT_BEEP") {
				MRLiveLogSettings settings = configuredLiveLogSettings();
				if (!parseBooleanLiteral(value, settings.reportSearchHitsWithSystemBeep, errorMessage)) return false;
				return setConfiguredLiveLogSettings(settings, errorMessage);
			}
			if (upper == "LIVE_LOG_REPORT_AUDIO") {
				MRLiveLogSettings settings = configuredLiveLogSettings();
				if (!parseBooleanLiteral(value, settings.reportSearchHitsWithAudioSignal, errorMessage)) return false;
				return setConfiguredLiveLogSettings(settings, errorMessage);
			}
			if (upper == "LIVE_LOG_SCROLL_DIRECTION") {
				MRLiveLogSettings settings = configuredLiveLogSettings();
				if (!parseLiveLogScrollDirectionLiteral(value, settings.scrollDirection, errorMessage)) return false;
				return setConfiguredLiveLogSettings(settings, errorMessage);
			}
			if (upper == "LIVE_LOG_LINE_NUMBERS") {
				MRLiveLogSettings settings = configuredLiveLogSettings();
				if (!parseBooleanLiteral(value, settings.showLineNumbers, errorMessage)) return false;
				return setConfiguredLiveLogSettings(settings, errorMessage);
			}
			if (upper == "LIVE_LOG_TIMESTAMPS") {
				MRLiveLogSettings settings = configuredLiveLogSettings();
				if (!parseBooleanLiteral(value, settings.showTimestamps, errorMessage)) return false;
				return setConfiguredLiveLogSettings(settings, errorMessage);
			}
			if (upper == "LIVE_LOG_SYNTAX_HIGHLIGHTING") {
				MRLiveLogSettings settings = configuredLiveLogSettings();
				if (!parseBooleanLiteral(value, settings.syntaxHighlighting, errorMessage)) return false;
				return setConfiguredLiveLogSettings(settings, errorMessage);
			}
			if (upper == "LIVE_LOG_AUDIO_URI") {
				MRLiveLogSettings settings = configuredLiveLogSettings();
				settings.audioSignalUri = normalizeConfiguredPathInput(value);
				return setConfiguredLiveLogSettings(settings, errorMessage);
			}
			if (upper == "LIVE_LOG_JOURNAL_TAG_HISTORY") {
				MRLiveLogSettings settings = configuredLiveLogSettings();
				settings.journalAppTagHistory.push_back(value);
				return setConfiguredLiveLogSettings(settings, errorMessage);
			}
			if (upper == "AUDIO_PLAYER") return setConfiguredAudioPlayerPath(value, errorMessage);
			if (upper == "AUTOEXEC_MACRO") return addConfiguredAutoexecMacroEntry(value, errorMessage);
			if (upper == "LASTFILEDIALOGPATH") return setConfiguredLastFileDialogPath(value, errorMessage);
			if (upper == "WORKSPACE") {
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MAX_PATH_HISTORY") {
				int parsed = 0;
				if (!parseHistoryLimitLiteral(value, parsed, errorMessage, "MAX_PATH_HISTORY")) return false;
				return setConfiguredPathHistoryLimitValue(parsed, errorMessage);
			}
			if (upper == "MAX_FILE_HISTORY") {
				int parsed = 0;
				if (!parseHistoryLimitLiteral(value, parsed, errorMessage, "MAX_FILE_HISTORY")) return false;
				return setConfiguredFileHistoryLimitValue(parsed, errorMessage);
			}
			if (upper == "MAX_WORKSPACE_HISTORY") {
				int parsed = 0;
				if (!parseHistoryLimitLiteral(value, parsed, errorMessage, "MAX_WORKSPACE_HISTORY")) return false;
				return setConfiguredWorkspaceHistoryLimitValue(parsed, errorMessage);
			}
			if (upper == "PATH_HISTORY") {
				MRScopedDialogHistoryState state = dialogHistoryState(MRDialogHistoryScope::General);
				addSerializedHistoryEntry(state.pathHistory, value, configuredPathHistoryLimit(), true);
				storeDialogHistoryState(MRDialogHistoryScope::General, state);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "FILE_HISTORY") {
				MRScopedDialogHistoryState state = dialogHistoryState(MRDialogHistoryScope::General);
				addSerializedHistoryEntry(state.fileHistory, value, configuredFileHistoryLimit(), true);
				storeDialogHistoryState(MRDialogHistoryScope::General, state);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kDialogLastPathKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedPath;

				if (!parseScopedHistoryPayload(value, "path", scope, parsedPath, errorMessage)) return false;
				return setScopedDialogLastPath(scope, parsedPath, errorMessage);
			}
			if (upper == kDialogPathHistoryKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedValue;
				MRScopedDialogHistoryState state;

				if (!parseScopedHistoryPayload(value, "value", scope, parsedValue, errorMessage)) return false;
				state = dialogHistoryState(scope);
				addSerializedHistoryEntry(state.pathHistory, parsedValue, configuredPathHistoryLimit(), true);
				storeDialogHistoryState(scope, state);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kDialogFileHistoryKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedValue;
				MRScopedDialogHistoryState state;

				if (!parseScopedHistoryPayload(value, "value", scope, parsedValue, errorMessage)) return false;
				state = dialogHistoryState(scope);
				addSerializedHistoryEntry(state.fileHistory, parsedValue, configuredFileHistoryLimitForScope(scope), true);
				storeDialogHistoryState(scope, state);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_FILESPEC_HISTORY") {
				std::vector<MRDialogHistoryEntry> entries = configuredMultiFilespecHistoryStorage();
				addSerializedHistoryEntry(entries, value, configuredFileHistoryLimit(), false);
				storeConfiguredMultiFilespecHistoryStorage(entries);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_PATH_HISTORY") {
				std::vector<MRDialogHistoryEntry> entries = configuredMultiPathHistoryStorage();
				addSerializedHistoryEntry(entries, value, configuredPathHistoryLimit(), true);
				storeConfiguredMultiPathHistoryStorage(entries);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "KEYMAP_PROFILE") {
				std::vector<MRKeymapProfile> profiles = configuredKeymapProfiles();

				if (!applyKeymapProfileRecord(profiles, value, errorMessage)) return false;
				return setConfiguredKeymapProfiles(profiles, errorMessage);
			}
			if (upper == "KEYMAP_BIND") {
				std::vector<MRKeymapProfile> profiles = configuredKeymapProfiles();

				if (!applyKeymapBindingRecord(profiles, value, errorMessage)) return false;
				return setConfiguredKeymapProfiles(profiles, errorMessage);
			}
			if (upper == "ACTIVE_KEYMAP_PROFILE") {
				std::string activeProfile;

				if (!parseActiveKeymapProfileRecord(value, activeProfile, errorMessage)) return false;
				return setConfiguredActiveKeymapProfile(activeProfile, errorMessage);
			}
			if (upper == "DEFAULT_PROFILE_DESCRIPTION") return setConfiguredDefaultProfileDescription(value, errorMessage);
			break;
		}
		case MRSettingsKeyClass::Edit:
			return applyConfiguredEditSetupValue(key, value, errorMessage);
		case MRSettingsKeyClass::ColorInline:
			return setError(errorMessage, "Inline color settings are not supported in settings.mrmac.");
	}
	return setError(errorMessage, "Unsupported MRSETUP key.");
}
