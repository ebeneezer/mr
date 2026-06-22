#include "MRSettingsStorage.hpp"

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

bool buildCanonicalSettingsSource(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource,
                                  std::string *errorMessage) {
	return mr::settings::storage::buildCanonicalSettingsSourceFromStorage(settingsPath, source, report, canonicalSource, errorMessage);
}

bool prepareStartupSettingsSource(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource,
                                  std::string *errorMessage) {
	return mr::settings::storage::prepareStartupSettingsSourceFromStorage(settingsPath, source, report, canonicalSource, errorMessage);
}

std::string describeSettingsLoadReport(const MRSettingsLoadReport &report) {
	return mr::settings::storage::describeStorageSettingsLoadReport(report);
}

bool diffSettingsSources(const std::string &beforeSource, const std::string &afterSource, std::vector<MRSettingsChangeEntry> &changes, std::string *errorMessage) {
	return mr::settings::storage::diffStorageSettingsSources(beforeSource, afterSource, changes, errorMessage);
}

std::string formatSettingsChangeForLog(const MRSettingsChangeEntry &change) {
	return mr::settings::storage::formatStorageSettingsChangeForLog(change);
}

std::string buildSettingsMacroSource(const MRSetupPaths &paths) {
	return mr::settings::storage::buildSettingsMacroSourceFromRuntimeModel(paths);
}

bool configuredSettingsDirty() {
	return mr::settings::storage::configuredSettingsDirtyInStorage();
}

void clearConfiguredSettingsDirty() {
	mr::settings::storage::clearConfiguredSettingsDirtyInStorage();
}

bool persistConfiguredSettingsSnapshot(std::string *errorMessage, MRSettingsWriteReport *report) {
	return mr::settings::storage::persistConfiguredSettingsSnapshotFromRuntimeModel(errorMessage, report);
}

bool persistConfiguredSettingsSnapshotWithWorkspace(std::string *errorMessage, MRSettingsWriteReport *report) {
	return mr::settings::storage::persistConfiguredSettingsSnapshotWithWorkspaceFromRuntimeModel(errorMessage, report);
}

bool writeSettingsMacroFile(const MRSetupPaths &paths, std::string *errorMessage, MRSettingsWriteReport *report) {
	return mr::settings::storage::writeSettingsMacroFileFromRuntimeModel(paths, errorMessage, report);
}

bool ensureSettingsMacroFileExists(const std::string &settingsMacroUri, std::string *errorMessage) {
	return mr::settings::storage::ensureSettingsMacroFileExistsInStorage(settingsMacroUri, errorMessage);
}
