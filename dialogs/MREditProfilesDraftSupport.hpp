#ifndef MREDITPROFILESDRAFTSUPPORT_HPP
#define MREDITPROFILESDRAFTSUPPORT_HPP

#include "MREditProfilesPanelInternal.hpp"

#include <cstddef>
#include <string>
#include <vector>

struct MREditExtensionProfile;
struct MREditSetupSettings;

namespace MREditProfilesDialogInternal {

struct EditProfileDraft {
	bool isDefault = false;
	std::string id;
	std::string name;
	std::string extensionsLiteral;
	std::string colorThemeUri;
	EditSettingsDialogRecord settingsRecord;
};

[[nodiscard]] std::vector<std::string> splitExtensionLiteral(const std::string &literal);
void settingsToRecordLocal(const MREditSetupSettings &settings, EditSettingsDialogRecord &record);
[[nodiscard]] bool draftsEqual(const EditProfileDraft &lhs, const EditProfileDraft &rhs);
[[nodiscard]] bool draftListsEqual(const std::vector<EditProfileDraft> &lhs,
                                   const std::vector<EditProfileDraft> &rhs);
[[nodiscard]] EditProfileDraft draftFromProfile(const MREditExtensionProfile &profile);
[[nodiscard]] EditProfileDraft makeDefaultDraft();
[[nodiscard]] std::string buildProfileListLabel(const EditProfileDraft &draft, std::size_t idWidth);
[[nodiscard]] EditProfileDraft makeNewDraft(const std::vector<EditProfileDraft> &existingDrafts);
[[nodiscard]] EditProfileDraft makeCopiedDraft(const EditProfileDraft &source,
                                               const std::vector<EditProfileDraft> &existingDrafts);
[[nodiscard]] bool validateDraftsForUi(const std::vector<EditProfileDraft> &drafts, int currentIndex,
                                       std::string &errorText);
[[nodiscard]] bool saveAndReloadEditProfiles(const std::vector<EditProfileDraft> &drafts,
                                             std::string &errorText);
[[nodiscard]] std::vector<std::string> dirtyDraftIds(const std::vector<EditProfileDraft> &initialDrafts,
                                                     const std::vector<EditProfileDraft> &drafts);
[[nodiscard]] std::string joinCommaSeparated(const std::vector<std::string> &values);

} // namespace MREditProfilesDialogInternal

#endif
