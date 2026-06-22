#include "../../app/utils/MRFileIOUtils.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "../../app/commands/MRWindowCommands.hpp"
#include "MRSettingsHistory.hpp"
#include "MRSettingsEditSetup.hpp"
#include "MRSettingsNormalize.hpp"
#include "MRSettingsRuntime.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "MRSettingsSnapshotIO.hpp"
#include "MRSettingsSourceModel.hpp"
#include "MRSettingsStorage.hpp"
#include "MRSettingsThemesProfiles.hpp"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <pwd.h>
#include <regex>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace mr::settings::storage {

bool buildCanonicalSettingsSourceFromStorage(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource,
                                      std::string *errorMessage);
bool prepareStartupSettingsSourceFromStorage(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource,
                                      std::string *errorMessage);
std::string describeStorageSettingsLoadReport(const MRSettingsLoadReport &report);
bool diffStorageSettingsSources(const std::string &beforeSource, const std::string &afterSource, std::vector<MRSettingsChangeEntry> &changes, std::string *errorMessage);
std::string formatStorageSettingsChangeForLog(const MRSettingsChangeEntry &change);
std::string buildSettingsMacroSourceFromRuntimeModel(const MRSetupPaths &paths);
bool configuredSettingsDirtyInStorage();
void clearConfiguredSettingsDirtyInStorage();
bool persistConfiguredSettingsSnapshotFromRuntimeModel(std::string *errorMessage, MRSettingsWriteReport *report);
bool persistConfiguredSettingsSnapshotWithWorkspaceFromRuntimeModel(std::string *errorMessage, MRSettingsWriteReport *report);
bool writeSettingsMacroFileFromRuntimeModel(const MRSetupPaths &paths, std::string *errorMessage, MRSettingsWriteReport *report);
bool ensureSettingsMacroFileExistsInStorage(const std::string &settingsMacroUri, std::string *errorMessage);

} // namespace mr::settings::storage

namespace {

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

} // namespace

std::string mr::settings::storage::describeStorageSettingsLoadReport(const MRSettingsLoadReport &report) {
	std::vector<std::string> parts;
	std::string text;

	if (hasFlag(report, MRSettingsLoadReport::UnknownKeyDropped)) parts.emplace_back("unknown keys dropped");
	if (hasFlag(report, MRSettingsLoadReport::DuplicateKeySeen)) parts.emplace_back("duplicates resolved");
	if (hasFlag(report, MRSettingsLoadReport::InvalidValueReset)) parts.emplace_back("invalid values reset to defaults");
	if (hasFlag(report, MRSettingsLoadReport::MissingCanonicalKeyDefaulted)) parts.emplace_back("missing canonical keys defaulted");
	if (hasFlag(report, MRSettingsLoadReport::LegacyInlineColorsSeen)) parts.emplace_back("legacy inline colors normalized");
	if (hasFlag(report, MRSettingsLoadReport::ThemeFallbackUsed)) parts.emplace_back("theme fallback applied");
	if (hasFlag(report, MRSettingsLoadReport::AnchoredSettingsPath)) parts.emplace_back("settings path anchored to active file");
	if (hasFlag(report, MRSettingsLoadReport::ObsoleteFeProfileDropped)) parts.emplace_back("obsolete MREDITPROFILE directives dropped; FE profile defaults restored");
	if (hasFlag(report, MRSettingsLoadReport::VersionUpgradeRequired)) parts.emplace_back("persisted version upgraded");
	if (parts.empty()) return std::string();
	for (std::size_t i = 0; i < parts.size(); ++i) {
		if (i != 0) text += "; ";
		text += parts[i];
	}
	return text;
}

bool mr::settings::storage::diffStorageSettingsSources(const std::string &beforeSource, const std::string &afterSource, std::vector<MRSettingsChangeEntry> &changes, std::string *errorMessage) {
	const MRFlattenedSettingsDocument before = flattenSettingsDocument(parseSettingsDocument(beforeSource, true));
	const MRFlattenedSettingsDocument after = flattenSettingsDocument(parseSettingsDocument(afterSource, true));

	changes.clear();
	diffFlattenedDocuments(before, after, changes);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string mr::settings::storage::formatStorageSettingsChangeForLog(const MRSettingsChangeEntry &change) {
	std::string text = change.scope + " ";

	if (change.kind == MRSettingsChangeEntry::Kind::Added) text += "+ " + change.key + " = " + quoteValue(change.newValue);
	else if (change.kind == MRSettingsChangeEntry::Kind::Removed)
		text += "- " + change.key + " (was " + quoteValue(change.oldValue) + ")";
	else
		text += change.key + ": " + quoteValue(change.oldValue) + " -> " + quoteValue(change.newValue);
	return text;
}

std::string mr::settings::storage::buildSettingsMacroSourceFromRuntimeModel(const MRSetupPaths &paths) {
	return buildSettingsMacroSource(captureConfiguredSettingsSnapshot(paths));
}

std::string buildSettingsMacroSourcePreservingWorkspace(const MRSetupPaths &paths, std::string_view previousSource) {
	static const std::regex workspacePattern(R"(MRSETUP\s*\(\s*'WORKSPACE'\s*,\s*'((?:''|[^'])*)'\s*\)\s*;?)", std::regex_constants::ECMAScript | std::regex_constants::icase);
	std::string source = buildSettingsMacroSource(paths);
	std::string previousText(previousSource);
	std::string workspaceLines;
	std::smatch match;

	while (std::regex_search(previousText, match, workspacePattern)) {
		workspaceLines += "MRSETUP('WORKSPACE', '";
		workspaceLines += match[1].str();
		workspaceLines += "');\n";
		previousText = match.suffix().str();
	}
	if (!workspaceLines.empty()) {
		const std::size_t endMacro = source.rfind("END_MACRO;");

		if (endMacro != std::string::npos) source.insert(endMacro, workspaceLines);
	}
	return source;
}

bool mr::settings::storage::configuredSettingsDirtyInStorage() {
	return configuredSettingsDirtyFlag();
}

void mr::settings::storage::clearConfiguredSettingsDirtyInStorage() {
	configuredSettingsDirtyFlag() = false;
}

bool persistConfiguredSettingsSnapshotWithMode(bool includeWorkspace, std::string *errorMessage, MRSettingsWriteReport *report) {
	MRSetupPaths paths;
	std::string settingsPath = configuredSettingsMacroFilePath();
	std::string settingsDir = directoryPartOf(settingsPath);
	std::string source;
	std::string previousSource;

	if (report != nullptr) *report = MRSettingsWriteReport();
	if (report != nullptr) report->settingsPath = settingsPath;
	if (!mr::settings::storage::configuredSettingsDirtyInStorage() && !includeWorkspace) {
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}

	paths.settingsMacroUri = settingsPath;
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();

	if (!validateSettingsMacroFilePath(settingsPath, errorMessage)) return false;
	if (!ensureDirectoryTree(settingsDir, errorMessage)) return false;
	static_cast<void>(readTextFile(settingsPath, previousSource));
	source = includeWorkspace ? buildSettingsMacroSourceWithWorkspace(paths) : buildSettingsMacroSourcePreservingWorkspace(paths, previousSource);
	if (!writeTextFile(settingsPath, source)) return setError(errorMessage, "Unable to write settings macro file: " + settingsPath);
	populateSettingsWriteReport(settingsPath, previousSource, source, report);
	mr::settings::storage::clearConfiguredSettingsDirtyInStorage();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool mr::settings::storage::persistConfiguredSettingsSnapshotFromRuntimeModel(std::string *errorMessage, MRSettingsWriteReport *report) {
	return persistConfiguredSettingsSnapshotWithMode(false, errorMessage, report);
}

bool mr::settings::storage::persistConfiguredSettingsSnapshotWithWorkspaceFromRuntimeModel(std::string *errorMessage, MRSettingsWriteReport *report) {
	return persistConfiguredSettingsSnapshotWithMode(true, errorMessage, report);
}

bool mr::settings::storage::writeSettingsMacroFileFromRuntimeModel(const MRSetupPaths &paths, std::string *errorMessage, MRSettingsWriteReport *report) {
	std::string settingsPath = normalizeConfiguredPathInput(paths.settingsMacroUri);
	std::string settingsDir = directoryPartOf(settingsPath);
	std::string source;
	std::string previousSource;

	if (!validateSettingsMacroFilePath(settingsPath, errorMessage)) return false;
	if (!ensureDirectoryTree(settingsDir, errorMessage)) return false;
	static_cast<void>(readTextFile(settingsPath, previousSource));
	source = buildSettingsMacroSourcePreservingWorkspace(paths, previousSource);
	if (!writeTextFile(settingsPath, source)) return setError(errorMessage, "Unable to write settings macro file: " + settingsPath);
	populateSettingsWriteReport(settingsPath, previousSource, source, report);
	mr::settings::storage::clearConfiguredSettingsDirtyInStorage();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool mr::settings::storage::ensureSettingsMacroFileExistsInStorage(const std::string &settingsMacroUri, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(settingsMacroUri);
	struct stat st;

	if (!validateSettingsMacroFilePath(normalized, errorMessage)) return false;
	if (::stat(normalized.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode)) return setError(errorMessage, "Settings macro URI must include a filename.");
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}

	{
		MRSettingsSnapshot snapshot;
		std::string settingsDir = directoryPartOf(normalized);

		if (!resetSettingsSnapshot(normalized, snapshot, errorMessage)) return false;
		if (!ensureDirectoryTree(settingsDir, errorMessage)) return false;
		if (!writeTextFile(normalized, buildSettingsMacroSource(snapshot))) return setError(errorMessage, "Unable to write settings macro file: " + normalized);
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
}
