#include "../../app/utils/MRStringUtils.hpp"
#include "../../app/MRVersion.hpp"
#include "MRSettingsAssignmentInternal.hpp"
#include "MRSettingsAssignments.hpp"
#include "MRSettingsCompilerProfiles.hpp"
#include "MRSettingsEditSetup.hpp"
#include "MRSettingsHistory.hpp"
#include "MRSettingsRuntime.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "MRSettingsSnapshotIO.hpp"
#include "MRSettingsThemesProfiles.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace mr::settings_assignment;

bool applySettingsSnapshotAssignment(MRSettingsSnapshot &snapshot, const std::string &key, const std::string &value, std::string *errorMessage) {
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
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MACROPATH") {
				const std::string normalized = normalizeConfiguredPathInput(value);
				MRSettingsSnapshot::DialogHistoryState &generalHistory = snapshot.dialogHistory[dialogHistoryScopeIndex(MRDialogHistoryScope::General)];

				if (!validateMacroDirectoryPath(value, errorMessage)) return false;
				snapshot.paths.macroPath = makeAbsolutePath(normalized);
				if (generalHistory.pathHistory.empty() && isReadableDirectory(snapshot.paths.macroPath)) addHistoryEntry(generalHistory.pathHistory, snapshot.paths.macroPath, snapshot.maxPathHistory);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "HELPPATH") {
				const std::string normalized = normalizeConfiguredPathInput(value);

				if (!validateHelpFilePath(value, errorMessage)) return false;
				snapshot.paths.helpUri = makeAbsolutePath(normalized);
				return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupHelpFile, snapshot.paths.helpUri, errorMessage);
			}
			if (upper == "TEMPDIR") {
				const std::string normalized = normalizeConfiguredPathInput(value);

				if (!validateTempDirectoryPath(value, errorMessage)) return false;
				snapshot.paths.tempPath = makeAbsolutePath(normalized);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "SHELLPATH") {
				const std::string normalized = normalizeConfiguredPathInput(value);

				if (!validateShellExecutablePath(value, errorMessage)) return false;
				snapshot.paths.shellUri = makeAbsolutePath(normalized);
				return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupShellExecutable, snapshot.paths.shellUri, errorMessage);
			}
			break;
		}
		case MRSettingsKeyClass::Global: {
			std::string upper = upperAscii(trimAscii(key));
			if (upper == "WINDOW_MANAGER") {
				if (!parseBooleanLiteral(value, snapshot.windowManagerEnabled, errorMessage)) return false;
				return true;
			}
			if (upper == "MESSAGES") {
				if (!parseBooleanLiteral(value, snapshot.menulineMessagesEnabled, errorMessage)) return false;
				return true;
			}
			if (upper == "AUTODETECT_BINARY_FILES") {
				if (!parseBooleanLiteral(value, snapshot.autoDetectBinaryFiles, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_TEXT_TYPE") {
				if (!parseSearchTextTypeLiteral(value, snapshot.searchDialogOptions.textType, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_DIRECTION") {
				if (!parseSearchDirectionLiteral(value, snapshot.searchDialogOptions.direction, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_MODE") {
				if (!parseSearchModeLiteral(value, snapshot.searchDialogOptions.mode, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_CASE_SENSITIVE") {
				if (!parseBooleanLiteral(value, snapshot.searchDialogOptions.caseSensitive, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_GLOBAL_SEARCH") {
				if (!parseBooleanLiteral(value, snapshot.searchDialogOptions.globalSearch, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_RESTRICT_MARKED_BLOCK") {
				if (!parseBooleanLiteral(value, snapshot.searchDialogOptions.restrictToMarkedBlock, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_ALL_WINDOWS") {
				if (!parseBooleanLiteral(value, snapshot.searchDialogOptions.searchAllWindows, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_LIST_ALL_OCCURRENCES") {
				bool listAll = false;
				if (!parseBooleanLiteral(value, listAll, errorMessage)) return false;
				snapshot.searchDialogOptions.mode = listAll ? MRSearchMode::ListAll : MRSearchMode::StopFirst;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "SAR_TEXT_TYPE") {
				if (!parseSearchTextTypeLiteral(value, snapshot.sarDialogOptions.textType, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_DIRECTION") {
				if (!parseSearchDirectionLiteral(value, snapshot.sarDialogOptions.direction, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_MODE") {
				if (!parseSarModeLiteral(value, snapshot.sarDialogOptions.mode, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_LEAVE_CURSOR_AT") {
				if (!parseSarLeaveCursorLiteral(value, snapshot.sarDialogOptions.leaveCursorAt, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_CASE_SENSITIVE") {
				if (!parseBooleanLiteral(value, snapshot.sarDialogOptions.caseSensitive, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_GLOBAL_SEARCH") {
				if (!parseBooleanLiteral(value, snapshot.sarDialogOptions.globalSearch, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_RESTRICT_MARKED_BLOCK") {
				if (!parseBooleanLiteral(value, snapshot.sarDialogOptions.restrictToMarkedBlock, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_ALL_WINDOWS") {
				if (!parseBooleanLiteral(value, snapshot.sarDialogOptions.searchAllWindows, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_REPLACE_MODE") {
				MRSarMode mode = MRSarMode::ReplaceFirst;
				if (!parseSarModeLiteral(value, mode, errorMessage)) return false;
				snapshot.sarDialogOptions.mode = mode == MRSarMode::ReplaceAll ? MRSarMode::ReplaceAll : MRSarMode::ReplaceFirst;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "SAR_PROMPT_EACH_REPLACE") {
				bool promptEach = false;
				if (!parseBooleanLiteral(value, promptEach, errorMessage)) return false;
				if (promptEach) snapshot.sarDialogOptions.mode = MRSarMode::PromptEach;
				else if (snapshot.sarDialogOptions.mode == MRSarMode::PromptEach)
					snapshot.sarDialogOptions.mode = MRSarMode::ReplaceFirst;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SEARCH_FILESPEC") {
				snapshot.multiSearchDialogOptions.filespec = trimAscii(value);
				if (snapshot.multiSearchDialogOptions.filespec.empty()) snapshot.multiSearchDialogOptions.filespec = "*.*";
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SEARCH_TEXT") {
				snapshot.multiSearchDialogOptions.searchText = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SEARCH_STARTING_PATH") {
				snapshot.multiSearchDialogOptions.startingPath = normalizeConfiguredPathInput(value);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SEARCH_SUBDIRECTORIES") {
				if (!parseBooleanLiteral(value, snapshot.multiSearchDialogOptions.searchSubdirectories, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SEARCH_CASE_SENSITIVE") {
				if (!parseBooleanLiteral(value, snapshot.multiSearchDialogOptions.caseSensitive, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SEARCH_REGULAR_EXPRESSIONS") {
				if (!parseBooleanLiteral(value, snapshot.multiSearchDialogOptions.regularExpressions, errorMessage)) return false;
				if (snapshot.multiSearchDialogOptions.regularExpressions) snapshot.multiSearchDialogOptions.wholeWords = false;
				return true;
			}
			if (upper == "MULTI_SEARCH_WHOLE_WORDS") {
				if (!parseBooleanLiteral(value, snapshot.multiSearchDialogOptions.wholeWords, errorMessage)) return false;
				if (snapshot.multiSearchDialogOptions.wholeWords) snapshot.multiSearchDialogOptions.regularExpressions = false;
				return true;
			}
			if (upper == "MULTI_SEARCH_FILES_IN_MEMORY") {
				if (!parseBooleanLiteral(value, snapshot.multiSearchDialogOptions.searchFilesInMemory, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SEARCH_RESTRICT_WORKSPACE") {
				if (!parseBooleanLiteral(value, snapshot.multiSearchDialogOptions.restrictToWorkspace, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_FILESPEC") {
				snapshot.multiSarDialogOptions.filespec = trimAscii(value);
				if (snapshot.multiSarDialogOptions.filespec.empty()) snapshot.multiSarDialogOptions.filespec = "*.*";
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SAR_TEXT") {
				snapshot.multiSarDialogOptions.searchText = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SAR_REPLACEMENT") {
				snapshot.multiSarDialogOptions.replacementText = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SAR_STARTING_PATH") {
				snapshot.multiSarDialogOptions.startingPath = normalizeConfiguredPathInput(value);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SAR_SUBDIRECTORIES") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.searchSubdirectories, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_CASE_SENSITIVE") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.caseSensitive, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_REGULAR_EXPRESSIONS") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.regularExpressions, errorMessage)) return false;
				if (snapshot.multiSarDialogOptions.regularExpressions) snapshot.multiSarDialogOptions.wholeWords = false;
				return true;
			}
			if (upper == "MULTI_SAR_WHOLE_WORDS") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.wholeWords, errorMessage)) return false;
				if (snapshot.multiSarDialogOptions.wholeWords) snapshot.multiSarDialogOptions.regularExpressions = false;
				return true;
			}
			if (upper == "MULTI_SAR_FILES_IN_MEMORY") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.searchFilesInMemory, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_KEEP_FILES_OPEN") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.keepFilesOpen, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_RESTRICT_WORKSPACE") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.restrictToWorkspace, errorMessage)) return false;
				return true;
			}
			if (upper == "PDF_EXPORT_PATH") {
				snapshot.pdfExportSettings.outputPath = value;
				if (!trimAscii(value).empty()) return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::PdfExport, value, errorMessage);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_PAGE_SEPARATOR") {
				snapshot.pdfExportSettings.pageSeparatorLiteral = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_FONT_FAMILY") {
				snapshot.pdfExportSettings.fontFamily = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_FONT_SIZE") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 1 || parsed > 40) return setError(errorMessage, "PDF_EXPORT_FONT_SIZE must be within 1..40.");
				snapshot.pdfExportSettings.fontSizePoints = static_cast<int>(parsed);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_HEADER_LINE") {
				snapshot.pdfExportSettings.headerLine = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_FOOTER_LINE") {
				snapshot.pdfExportSettings.footerLine = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_USE_PRINT_MARGIN") {
				bool enabled = true;
				if (!parseBooleanLiteral(value, enabled, errorMessage)) return false;
				if (!enabled) snapshot.pdfExportSettings.textWidth = "0";
				return true;
			}
			if (upper == "PDF_EXPORT_PRINT_MARGIN_COLUMNS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_TEXT_WIDTH must be within 0..9999.");
				if (trimAscii(snapshot.pdfExportSettings.textWidth) != "0") snapshot.pdfExportSettings.textWidth = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_TEXT_WIDTH") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_TEXT_WIDTH must be within 0..9999.");
				snapshot.pdfExportSettings.textWidth = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_LEFT_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_LEFT_MARGIN_POINTS must be within 0..9999.");
				snapshot.pdfExportSettings.leftMarginPoints = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_RIGHT_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_RIGHT_MARGIN_POINTS must be within 0..9999.");
				snapshot.pdfExportSettings.rightMarginPoints = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_TOP_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_TOP_MARGIN_POINTS must be within 0..9999.");
				snapshot.pdfExportSettings.topMarginPoints = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_BOTTOM_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_BOTTOM_MARGIN_POINTS must be within 0..9999.");
				snapshot.pdfExportSettings.bottomMarginPoints = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "ACQUIRE_COMMAND") {
				snapshot.acquireSettings.commandLine = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "ACQUIRE_COMMAND_HISTORY") {
				const std::string trimmed = trimAscii(value);
				if (!trimmed.empty()) snapshot.acquireSettings.commandHistory.push_back(trimmed);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
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
				snapshot.virtualDesktops = parsed;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "CYCLIC_VIRTUAL_DESKTOPS") {
				if (!parseBooleanLiteral(value, snapshot.cyclicVirtualDesktops, errorMessage)) return false;
				return true;
			}
			if (upper == "CURSOR_BEHAVIOUR") {
				if (!parseCursorBehaviourLiteral(value, snapshot.cursorBehaviour, errorMessage)) return false;
				return true;
			}
			if (upper == "COMPILER_ERROR_MESSAGE_PLACEMENT") {
				if (!parseCompilerErrorMessagePlacementLiteral(value, snapshot.compilerErrorMessagePlacement, errorMessage)) return false;
				return true;
			}
			if (upper == "SCROLLBAR_VISIBILITY") {
				if (!parseScrollbarVisibilityLiteral(value, snapshot.scrollbarVisibility, errorMessage)) return false;
				return true;
			}
			if (upper == "COLOR_OUTPUT_MODE") return parseColorOutputModeLiteral(value, snapshot.colorOutputMode, errorMessage);
			if (upper == "TRACK_COMPILER_WARNINGS") {
				if (!parseBooleanLiteral(value, snapshot.trackCompilerWarnings, errorMessage)) return false;
				return true;
			}
			if (upper == "TRACK_COMPILER_NOTES") {
				if (!parseBooleanLiteral(value, snapshot.trackCompilerNotes, errorMessage)) return false;
				return true;
			}
			if (upper == "UI_INDENT_STYLE") {
				if (!parseUiIndentStyleLiteral(value, snapshot.uiIndentStyle, errorMessage)) return false;
				return true;
				}
				if (upper == "CURSOR_POSITION_MARKER") return normalizeCursorPositionMarker(value, snapshot.cursorPositionMarker, errorMessage);
				if (upper == kWindowColorThemeProfileKey) {
					const std::string normalized = normalizeConfiguredPathInput(value);

					if (!validateColorThemeFilePath(normalized, errorMessage)) return false;
					snapshot.colorThemeFilePath = makeAbsolutePath(normalized);
					if (errorMessage != nullptr) errorMessage->clear();
					return true;
				}
				if (upper == "FILE_COMPARE_ORIGINAL_LEADING_GUTTERS") return normalizeFileCompareGutters(value, snapshot.fileCompareOriginalLeadingGutters, errorMessage);
			if (upper == "FILE_COMPARE_ORIGINAL_TRAILING_GUTTERS") return normalizeFileCompareGutters(value, snapshot.fileCompareOriginalTrailingGutters, errorMessage);
			if (upper == "FILE_COMPARE_COMPARE_LEADING_GUTTERS") return normalizeFileCompareGutters(value, snapshot.fileCompareCompareLeadingGutters, errorMessage);
			if (upper == "FILE_COMPARE_COMPARE_TRAILING_GUTTERS") return normalizeFileCompareGutters(value, snapshot.fileCompareCompareTrailingGutters, errorMessage);
			if (upper == "FILE_COMPARE_START_CONFIGURATION") {
				if (!parseFileCompareStartConfigurationLiteral(value, snapshot.fileCompareStartConfiguration, errorMessage)) return false;
				return true;
			}
			if (upper == "FILE_COMPARE_COMPARE_PANEL_READ_ONLY") {
				if (!parseBooleanLiteral(value, snapshot.fileCompareComparePanelReadOnly, errorMessage)) return false;
				return true;
			}
			if (upper == "AUTOSAVE_WORKSPACE") {
				if (!parseBooleanLiteral(value, snapshot.autosaveWorkspace, errorMessage)) return false;
				return true;
			}
			if (upper == "AUTOLOAD_WORKSPACE") {
				if (!parseBooleanLiteral(value, snapshot.autoloadWorkspace, errorMessage)) return false;
				return true;
			}
			if (upper == "LOG_HANDLING") {
				if (!parseLogHandlingLiteral(value, snapshot.logHandling, errorMessage)) return false;
				return true;
			}
			if (upper == "LOGFILE") {
				const std::string normalized = normalizeConfiguredPathInput(value);

				if (!validateLogFilePath(value, errorMessage)) return false;
				snapshot.logFilePath = makeAbsolutePath(normalized);
				return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupLogFile, snapshot.logFilePath, errorMessage);
			}
			if (upper == "LIVE_LOG_REPORT_MESSAGE_LINE") {
				if (!parseBooleanLiteral(value, snapshot.liveLogSettings.reportSearchHitsOnMessageLine, errorMessage)) return false;
				return true;
			}
			if (upper == "LIVE_LOG_REPORT_BEEP") {
				if (!parseBooleanLiteral(value, snapshot.liveLogSettings.reportSearchHitsWithSystemBeep, errorMessage)) return false;
				return true;
			}
			if (upper == "LIVE_LOG_REPORT_AUDIO") {
				if (!parseBooleanLiteral(value, snapshot.liveLogSettings.reportSearchHitsWithAudioSignal, errorMessage)) return false;
				return true;
			}
			if (upper == "LIVE_LOG_SCROLL_DIRECTION") {
				if (!parseLiveLogScrollDirectionLiteral(value, snapshot.liveLogSettings.scrollDirection, errorMessage)) return false;
				return true;
			}
			if (upper == "LIVE_LOG_LINE_NUMBERS") {
				if (!parseBooleanLiteral(value, snapshot.liveLogSettings.showLineNumbers, errorMessage)) return false;
				return true;
			}
			if (upper == "LIVE_LOG_TIMESTAMPS") {
				if (!parseBooleanLiteral(value, snapshot.liveLogSettings.showTimestamps, errorMessage)) return false;
				return true;
			}
			if (upper == "LIVE_LOG_SYNTAX_HIGHLIGHTING") {
				if (!parseBooleanLiteral(value, snapshot.liveLogSettings.syntaxHighlighting, errorMessage)) return false;
				return true;
			}
			if (upper == "LIVE_LOG_AUDIO_URI") {
				snapshot.liveLogSettings.audioSignalUri = normalizeConfiguredPathInput(value);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "LIVE_LOG_JOURNAL_TAG_HISTORY") {
				snapshot.liveLogSettings.journalAppTagHistory.push_back(value);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "AUDIO_PLAYER") {
				snapshot.audioPlayerPath = normalizeConfiguredPathInput(value);
				if (!snapshot.audioPlayerPath.empty()) snapshot.audioPlayerPath = makeAbsolutePath(snapshot.audioPlayerPath);
				if (!snapshot.audioPlayerPath.empty() && !isExecutableFile(snapshot.audioPlayerPath)) snapshot.audioPlayerPath.clear();
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "AUTOEXEC_MACRO") {
				const std::string normalized = normalizeAutoexecMacroEntry(value);
				if (!validateAutoexecMacroEntry(normalized, errorMessage)) return false;
				if (std::find(snapshot.autoexecMacros.begin(), snapshot.autoexecMacros.end(), normalized) == snapshot.autoexecMacros.end()) snapshot.autoexecMacros.push_back(normalized);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "LASTFILEDIALOGPATH") return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::General, value, errorMessage);
			if (upper == "WORKSPACE") {
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MAX_PATH_HISTORY") {
				int parsed = 0;
				if (!parseHistoryLimitLiteral(value, parsed, errorMessage, "MAX_PATH_HISTORY")) return false;
				return setSnapshotPathHistoryLimit(snapshot, parsed, errorMessage);
			}
			if (upper == "MAX_FILE_HISTORY") {
				int parsed = 0;
				if (!parseHistoryLimitLiteral(value, parsed, errorMessage, "MAX_FILE_HISTORY")) return false;
				return setSnapshotFileHistoryLimit(snapshot, parsed, errorMessage);
			}
			if (upper == "MAX_WORKSPACE_HISTORY") {
				int parsed = 0;
				if (!parseHistoryLimitLiteral(value, parsed, errorMessage, "MAX_WORKSPACE_HISTORY")) return false;
				return setSnapshotWorkspaceHistoryLimit(snapshot, parsed, errorMessage);
			}
			if (upper == "PATH_HISTORY") {
				addSerializedHistoryEntry(snapshot.dialogHistory[dialogHistoryScopeIndex(MRDialogHistoryScope::General)].pathHistory, value, snapshot.maxPathHistory, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "FILE_HISTORY") {
				addSerializedHistoryEntry(snapshot.dialogHistory[dialogHistoryScopeIndex(MRDialogHistoryScope::General)].fileHistory, value, snapshot.maxFileHistory, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kDialogLastPathKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedPath;

				if (!parseScopedHistoryPayload(value, "path", scope, parsedPath, errorMessage)) return false;
				return setSnapshotScopedDialogLastPath(snapshot, scope, parsedPath, errorMessage);
			}
			if (upper == kDialogPathHistoryKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedValue;

				if (!parseScopedHistoryPayload(value, "value", scope, parsedValue, errorMessage)) return false;
				addSerializedHistoryEntry(snapshot.dialogHistory[dialogHistoryScopeIndex(scope)].pathHistory, parsedValue, snapshot.maxPathHistory, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kDialogFileHistoryKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedValue;
				int limit = 0;

				if (!parseScopedHistoryPayload(value, "value", scope, parsedValue, errorMessage)) return false;
				limit = scope == MRDialogHistoryScope::WorkspaceLoad || scope == MRDialogHistoryScope::WorkspaceSave ? snapshot.maxWorkspaceHistory : snapshot.maxFileHistory;
				addSerializedHistoryEntry(snapshot.dialogHistory[dialogHistoryScopeIndex(scope)].fileHistory, parsedValue, limit, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_FILESPEC_HISTORY") {
				addSerializedHistoryEntry(snapshot.multiFilespecHistory, value, snapshot.maxFileHistory, false);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_PATH_HISTORY") {
				addSerializedHistoryEntry(snapshot.multiPathHistory, value, snapshot.maxPathHistory, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "DEFAULT_PROFILE_DESCRIPTION") {
				snapshot.defaultProfileDescription = trimAscii(value);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "KEYMAP_PROFILE") return applyKeymapProfileRecord(snapshot.keymapProfiles, value, errorMessage);
			if (upper == "KEYMAP_BIND") return applyKeymapBindingRecord(snapshot.keymapProfiles, value, errorMessage);
			if (upper == "ACTIVE_KEYMAP_PROFILE") return parseActiveKeymapProfileRecord(value, snapshot.activeKeymapProfile, errorMessage);
			break;
		}
		case MRSettingsKeyClass::Edit:
			return applyEditSetupValueInternal(snapshot.editSettings, key, value, errorMessage);
		case MRSettingsKeyClass::ColorInline:
			return setError(errorMessage, "Inline color settings are not supported in settings.mrmac.");
	}
	return setError(errorMessage, "Unsupported MRSETUP key.");
}

bool applySettingsSnapshotEditExtensionProfileDirective(MRSettingsSnapshot &snapshot, const std::string &operation, const std::string &profileId, const std::string &arg3, const std::string &arg4, std::string *errorMessage) {
	std::string op = upperAscii(trimAscii(operation));
	std::string id = canonicalEditProfileId(profileId);
	std::vector<MREditExtensionProfile> profiles = snapshot.editProfiles;
	MREditExtensionProfile *profile = nullptr;

	if (op.empty()) return setError(errorMessage, "MRFEPROFILE operation may not be empty.");
	if (id.empty()) return setError(errorMessage, "MRFEPROFILE profile id may not be empty.");
	for (std::size_t i = 0; i < profiles.size(); ++i)
		if (profileIdLookupKey(profiles[i].id) == profileIdLookupKey(id)) {
			profile = &profiles[i];
			break;
		}
	if (op == "DEFINE") {
		std::string name = canonicalEditProfileName(arg3);

		if (name.empty() && trimAscii(arg4).empty()) name = id;
		if (name.empty()) return setError(errorMessage, "MRFEPROFILE DEFINE requires a non-empty display name.");
		if (profile != nullptr) return setError(errorMessage, "Duplicate extension profile id: " + id);
		MREditExtensionProfile created;
		created.id = id;
		created.name = name;
		created.overrides.values = resolveEditSetupDefaults();
		profiles.push_back(created);
		return setSnapshotEditProfiles(snapshot, profiles, errorMessage);
	}
	if (profile == nullptr) return setError(errorMessage, "Unknown extension profile id: " + id);
	if (op == "EXT") {
		profile->extensions.push_back(arg3);
		return setSnapshotEditProfiles(snapshot, profiles, errorMessage);
	}
	if (op == "SET") {
		if (upperAscii(trimAscii(arg3)) == kWindowColorThemeProfileKey) {
			std::string normalizedTheme = canonicalWindowColorThemeUri(arg4);
			if (!normalizedTheme.empty() && !validateColorThemeFilePath(normalizedTheme, errorMessage)) return false;
			profile->windowColorThemeUri = normalizedTheme;
			return setSnapshotEditProfiles(snapshot, profiles, errorMessage);
		}
		if (upperAscii(trimAscii(arg3)) == "COMPILER_PROFILE") {
			profile->compilerProfileId = canonicalCompilerProfileId(arg4);
			return setSnapshotEditProfiles(snapshot, profiles, errorMessage);
		}
		const MREditSettingDescriptor *descriptor = editSettingDescriptorByKeyInternal(arg3);

		if (descriptor == nullptr) return setError(errorMessage, "Unknown edit setting key for extension profile.");
		if (!descriptor->profileSupported) return setError(errorMessage, std::string("Setting is global-only and cannot be overridden: ") + descriptor->key);
		if (!applyEditSetupValueInternal(profile->overrides.values, descriptor->key, arg4, errorMessage)) return false;
		profile->overrides.mask |= descriptor->overrideBit;
		return setSnapshotEditProfiles(snapshot, profiles, errorMessage);
	}
	return setError(errorMessage, "MRFEPROFILE supports operations DEFINE, EXT and SET.");
}

bool applySettingsSnapshotCompilerProfileDirective(MRSettingsSnapshot &snapshot, const std::string &operation, const std::string &profileId, const std::string &arg3, const std::string &arg4, std::string *errorMessage) {
	return applyCompilerProfileDirectiveToVector(snapshot.compilerProfiles, operation, profileId, arg3, arg4, nullptr, errorMessage);
}
