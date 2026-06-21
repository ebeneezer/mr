#ifndef MRSETTINGSSNAPSHOTIO_HPP
#define MRSETTINGSSNAPSHOTIO_HPP

#include "MRSettingsRuntime.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct MRSettingsWriteReport;

struct MRSettingsSnapshot {
	struct DialogHistoryState {
		std::string lastPath;
		std::vector<std::string> pathHistory;
		std::vector<std::string> fileHistory;

		auto operator==(const DialogHistoryState &) const noexcept -> bool = default;
	};

	MRSetupPaths paths;
	bool windowManagerEnabled{true};
	bool menulineMessagesEnabled{true};
	MRSearchDialogOptions searchDialogOptions;
	MRSarDialogOptions sarDialogOptions;
	MRMultiSearchDialogOptions multiSearchDialogOptions;
	MRMultiSarDialogOptions multiSarDialogOptions;
	MRPdfExportSettings pdfExportSettings;
	MRAcquireSettings acquireSettings;
	MRLiveLogSettings liveLogSettings;
	int virtualDesktops{1};
	bool cyclicVirtualDesktops{false};
	MRCursorBehaviour cursorBehaviour{MRCursorBehaviour::BoundToText};
	MRCompilerErrorMessagePlacement compilerErrorMessagePlacement{MRCompilerErrorMessagePlacement::RightMargin};
	bool languageServerSpawnDaemon{true};
	MRLanguageServerSidekickPlacement languageServerSidekickPlacement{MRLanguageServerSidekickPlacement::RightMargin};
	int languageServerHoverDwellMs{kLanguageServerHoverDwellMsDefault};
	int languageServerDocumentSyncDelayMs{kLanguageServerDocumentSyncDelayMsDefault};
	int languageServerSignatureQuietMs{kLanguageServerSignatureQuietMsDefault};
	MRLanguageServerChannelSettings languageServerChannels;
	MRScrollbarVisibility scrollbarVisibility{MRScrollbarVisibility::Smart};
	bool trackCompilerWarnings{false};
	bool trackCompilerNotes{false};
	MRUiIndentStyle uiIndentStyle{MRUiIndentStyle::KandR};
	std::string cursorPositionMarker{"R:C"};
	std::string fileCompareOriginalLeadingGutters{"L"};
	std::string fileCompareOriginalTrailingGutters{"M"};
	std::string fileCompareCompareLeadingGutters{"LD"};
	std::string fileCompareCompareTrailingGutters;
	MRFileCompareStartConfiguration fileCompareStartConfiguration{MRFileCompareStartConfiguration::OriginalCompare};
	bool fileCompareComparePanelReadOnly{true};
	bool autosaveWorkspace{false};
	bool autoloadWorkspace{false};
	MRLogHandling logHandling{MRLogHandling::Volatile};
	std::string logFilePath;
	std::vector<std::string> autoexecMacros;
	int maxPathHistory{15};
	int maxFileHistory{15};
	int maxWorkspaceHistory{15};
	std::array<DialogHistoryState, static_cast<std::size_t>(MRDialogHistoryScope::Count)> dialogHistory;
	std::vector<std::string> multiFilespecHistory;
	std::vector<std::string> multiPathHistory;
	std::string defaultProfileDescription{"Global defaults"};
	MREditSetupSettings editSettings;
	MRColorSetupSettings colorSettings;
	std::string colorThemeFilePath;
	std::vector<MRCompilerProfile> compilerProfiles;
	std::vector<MREditExtensionProfile> editProfiles;
	std::string keymapFilePath;
	std::vector<MRKeymapProfile> keymapProfiles;
	std::string activeKeymapProfile;

	auto operator==(const MRSettingsSnapshot &) const noexcept -> bool = default;
};

[[nodiscard]] std::string defaultLogFilePathForSettings(std::string_view settingsPath);
bool ensureDirectoryTree(const std::string &directoryPath, std::string *errorMessage);
bool writeNormalizedBootstrapFiles(const MRSettingsSnapshot &snapshot, std::string_view previousSource, const std::string &canonicalSource, std::string *errorMessage);
bool setSnapshotScopedDialogLastPath(MRSettingsSnapshot &snapshot, MRDialogHistoryScope scope, const std::string &path, std::string *errorMessage);
bool setSnapshotPathHistoryLimit(MRSettingsSnapshot &snapshot, int value, std::string *errorMessage);
bool setSnapshotFileHistoryLimit(MRSettingsSnapshot &snapshot, int value, std::string *errorMessage);
bool setSnapshotWorkspaceHistoryLimit(MRSettingsSnapshot &snapshot, int value, std::string *errorMessage);
bool setSnapshotEditProfiles(MRSettingsSnapshot &snapshot, const std::vector<MREditExtensionProfile> &profiles, std::string *errorMessage);
bool setSnapshotCompilerProfiles(MRSettingsSnapshot &snapshot, const std::vector<MRCompilerProfile> &profiles, std::string *errorMessage);
[[nodiscard]] MRSettingsSnapshot captureConfiguredSettingsSnapshot(const MRSetupPaths &paths);
void populateSettingsWriteReport(const std::string &settingsPath, const std::string &beforeSource, const std::string &afterSource, MRSettingsWriteReport *report);
bool resetSettingsSnapshot(const std::string &settingsPath, MRSettingsSnapshot &snapshot, std::string *errorMessage);
[[nodiscard]] std::string buildSettingsMacroSource(const MRSettingsSnapshot &snapshot);

#endif
