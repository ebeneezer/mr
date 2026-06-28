#ifndef MRSETTINGSASSIGNMENTS_HPP
#define MRSETTINGSASSIGNMENTS_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

enum class MRSettingsKeyClass : unsigned char;
struct MRSetupPaths;
struct MRSettingsSnapshot;

[[nodiscard]] MRSettingsKeyClass classifySettingsKey(std::string_view key);
[[nodiscard]] bool isCanonicalSerializedSettingsKey(std::string_view key);
[[nodiscard]] std::vector<std::string> canonicalSerializedSettingsKeys();
[[nodiscard]] std::size_t canonicalSerializedSettingsKeyCount();
bool resetConfiguredSettingsModel(const std::string &settingsPath, MRSetupPaths &paths, std::string *errorMessage = nullptr);
bool applyConfiguredSettingsAssignment(const std::string &key, const std::string &value, MRSetupPaths &paths, std::string *errorMessage = nullptr);
bool applySettingsSnapshotAssignment(MRSettingsSnapshot &snapshot, const std::string &key, const std::string &value, std::string *errorMessage);
bool applySettingsSnapshotEditExtensionProfileDirective(MRSettingsSnapshot &snapshot, const std::string &operation, const std::string &profileId, const std::string &arg3, const std::string &arg4, std::string *errorMessage);
bool applySettingsSnapshotCompilerProfileDirective(MRSettingsSnapshot &snapshot, const std::string &operation, const std::string &profileId, const std::string &arg3, const std::string &arg4, std::string *errorMessage);

#endif
