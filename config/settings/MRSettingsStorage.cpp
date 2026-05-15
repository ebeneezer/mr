#include "MRSettingsStorage.hpp"

namespace mr::settings::storage {

bool buildCanonicalSettingsSourceImpl(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource,
                                      std::string *errorMessage);
bool prepareStartupSettingsSourceImpl(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource,
                                      std::string *errorMessage);
std::string describeSettingsLoadReportImpl(const MRSettingsLoadReport &report);
bool diffSettingsSourcesImpl(const std::string &beforeSource, const std::string &afterSource, std::vector<MRSettingsChangeEntry> &changes, std::string *errorMessage);
std::string formatSettingsChangeForLogImpl(const MRSettingsChangeEntry &change);
std::string buildSettingsMacroSourceImpl(const MRSetupPaths &paths);
bool configuredSettingsDirtyImpl();
void clearConfiguredSettingsDirtyImpl();
bool persistConfiguredSettingsSnapshotImpl(std::string *errorMessage, MRSettingsWriteReport *report);
bool writeSettingsMacroFileImpl(const MRSetupPaths &paths, std::string *errorMessage, MRSettingsWriteReport *report);
bool ensureSettingsMacroFileExistsImpl(const std::string &settingsMacroUri, std::string *errorMessage);

} // namespace mr::settings::storage

bool buildCanonicalSettingsSource(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource,
                                  std::string *errorMessage) {
	return mr::settings::storage::buildCanonicalSettingsSourceImpl(settingsPath, source, report, canonicalSource, errorMessage);
}

bool prepareStartupSettingsSource(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource,
                                  std::string *errorMessage) {
	return mr::settings::storage::prepareStartupSettingsSourceImpl(settingsPath, source, report, canonicalSource, errorMessage);
}

std::string describeSettingsLoadReport(const MRSettingsLoadReport &report) {
	return mr::settings::storage::describeSettingsLoadReportImpl(report);
}

bool diffSettingsSources(const std::string &beforeSource, const std::string &afterSource, std::vector<MRSettingsChangeEntry> &changes, std::string *errorMessage) {
	return mr::settings::storage::diffSettingsSourcesImpl(beforeSource, afterSource, changes, errorMessage);
}

std::string formatSettingsChangeForLog(const MRSettingsChangeEntry &change) {
	return mr::settings::storage::formatSettingsChangeForLogImpl(change);
}

std::string buildSettingsMacroSource(const MRSetupPaths &paths) {
	return mr::settings::storage::buildSettingsMacroSourceImpl(paths);
}

bool configuredSettingsDirty() {
	return mr::settings::storage::configuredSettingsDirtyImpl();
}

void clearConfiguredSettingsDirty() {
	mr::settings::storage::clearConfiguredSettingsDirtyImpl();
}

bool persistConfiguredSettingsSnapshot(std::string *errorMessage, MRSettingsWriteReport *report) {
	return mr::settings::storage::persistConfiguredSettingsSnapshotImpl(errorMessage, report);
}

bool writeSettingsMacroFile(const MRSetupPaths &paths, std::string *errorMessage, MRSettingsWriteReport *report) {
	return mr::settings::storage::writeSettingsMacroFileImpl(paths, errorMessage, report);
}

bool ensureSettingsMacroFileExists(const std::string &settingsMacroUri, std::string *errorMessage) {
	return mr::settings::storage::ensureSettingsMacroFileExistsImpl(settingsMacroUri, errorMessage);
}
