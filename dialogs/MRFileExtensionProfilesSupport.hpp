#include "../app/utils/MRStringUtils.hpp"
#ifndef MRFILEEXTENSIONPROFILESSUPPORT_HPP
#define MRFILEEXTENSIONPROFILESSUPPORT_HPP

#include "MRFileExtensionEditorSettingsPanelInternal.hpp"

#include "../config/MRDialogPaths.hpp"

#include <cstddef>
#include <string>
#include <vector>

using MRFileExtensionProfile = MREditExtensionProfile;
using MRFileExtensionEditorSettings = MREditSetupSettings;

inline MRFileExtensionEditorSettings configuredFileExtensionEditorSettings() {
	return configuredEditSetupSettings();
}

inline MRFileExtensionEditorSettings mergeFileExtensionEditorSettings(const MRFileExtensionEditorSettings &defaults,
	                                                             const MREditSetupOverrides &overrides) {
	return mergeEditSetupSettings(defaults, overrides);
}

inline const std::vector<MRFileExtensionProfile> &configuredFileExtensionProfiles() {
	return configuredEditExtensionProfiles();
}

inline bool setConfiguredFileExtensionEditorSettings(const MRFileExtensionEditorSettings &settings,
	                                             std::string *errorMessage = nullptr) {
	return setConfiguredEditSetupSettings(settings, errorMessage);
}

inline bool setConfiguredFileExtensionProfiles(const std::vector<MRFileExtensionProfile> &profiles,
	                                       std::string *errorMessage = nullptr) {
	return setConfiguredEditExtensionProfiles(profiles, errorMessage);
}

namespace MRFileExtensionProfilesDialogInternal {

struct EditProfileDraft {
	bool isDefault = false;
	std::string id;
	std::string name;
	std::string extensionsLiteral;
	std::string colorThemeUri;
	FileExtensionEditorSettingsDialogRecord settingsRecord;
};

[[nodiscard]] std::string readRecordField(const char *value);
void writeRecordField(char *dest, std::size_t destSize, const std::string &value);
[[nodiscard]] bool fileExtensionEditorSettingsDialogRecordsEqual(const FileExtensionEditorSettingsDialogRecord &lhs, const FileExtensionEditorSettingsDialogRecord &rhs);
void initFileExtensionEditorSettingsDialogRecord(FileExtensionEditorSettingsDialogRecord &record);
[[nodiscard]] bool fileExtensionEditorSettingsDialogRecordToSettings(const FileExtensionEditorSettingsDialogRecord &record, MRFileExtensionEditorSettings &settings,
                                                      std::string &errorText);
[[nodiscard]] std::vector<std::string> splitExtensionLiteral(const std::string &literal);
void settingsToDialogRecord(const MRFileExtensionEditorSettings &settings, FileExtensionEditorSettingsDialogRecord &record);
[[nodiscard]] bool draftsEqual(const EditProfileDraft &lhs, const EditProfileDraft &rhs);
[[nodiscard]] bool draftListsEqual(const std::vector<EditProfileDraft> &lhs,
                                   const std::vector<EditProfileDraft> &rhs);
[[nodiscard]] EditProfileDraft draftFromProfile(const MRFileExtensionProfile &profile);
[[nodiscard]] EditProfileDraft makeDefaultDraft();
[[nodiscard]] std::string buildProfileListLabel(const EditProfileDraft &draft, std::size_t idWidth);
[[nodiscard]] EditProfileDraft makeNewDraft(const std::vector<EditProfileDraft> &existingDrafts);
[[nodiscard]] EditProfileDraft makeCopiedDraft(const EditProfileDraft &source,
                                               const std::vector<EditProfileDraft> &existingDrafts);
[[nodiscard]] bool validateDraftsForUi(const std::vector<EditProfileDraft> &drafts, int currentIndex,
                                       const EditProfileDraft *currentDraftOverride,
                                       std::string &errorText);
[[nodiscard]] inline bool validateDraftsForUi(const std::vector<EditProfileDraft> &drafts, int currentIndex,
                                              std::string &errorText) {
	return validateDraftsForUi(drafts, currentIndex, nullptr, errorText);
}
[[nodiscard]] bool saveAndReloadEditProfiles(const std::vector<EditProfileDraft> &drafts,
                                             std::string &errorText);
[[nodiscard]] std::vector<std::string> dirtyDraftIds(const std::vector<EditProfileDraft> &initialDrafts,
                                                     const std::vector<EditProfileDraft> &drafts);
[[nodiscard]] std::string joinCommaSeparated(const std::vector<std::string> &values);

} // namespace MRFileExtensionProfilesDialogInternal

#endif
