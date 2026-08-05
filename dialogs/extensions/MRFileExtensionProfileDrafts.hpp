#ifndef MRFILEEXTENSIONPROFILEDRAFTS_HPP
#define MRFILEEXTENSIONPROFILEDRAFTS_HPP

#include "MRFileExtensionEditorSettingsInternal.hpp"
#include "../setup/MRSetupCommon.hpp"

#include "../../config/settings/MRSettingsRuntime.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace MRFileExtensionProfilesInternal {

using mr::dialogs::readRecordField;
using mr::dialogs::writeRecordField;

struct EditProfileDraft {
	bool isDefault = false;
	std::string id;
	std::string name;
	std::string extensionsLiteral;
	std::string colorThemeUri;
	std::string compilerProfileId;
	FileExtensionEditorSettingsDialogRecord settingsRecord;
};

[[nodiscard]] bool fileExtensionEditorSettingsDialogRecordsEqual(const FileExtensionEditorSettingsDialogRecord &lhs, const FileExtensionEditorSettingsDialogRecord &rhs);
[[nodiscard]] bool fileExtensionEditorSettingsDialogRecordToSettings(const FileExtensionEditorSettingsDialogRecord &record, MREditSetupSettings &settings, std::string &errorText);
[[nodiscard]] std::vector<std::string> splitExtensionLiteral(const std::string &literal);
void settingsToDialogRecord(const MREditSetupSettings &settings, FileExtensionEditorSettingsDialogRecord &record);
[[nodiscard]] bool normalizeDraftListSyntax(std::vector<EditProfileDraft> &drafts, std::string &errorText);
[[nodiscard]] bool draftsEqual(const EditProfileDraft &lhs, const EditProfileDraft &rhs);
[[nodiscard]] bool draftListsEqual(const std::vector<EditProfileDraft> &lhs, const std::vector<EditProfileDraft> &rhs);
[[nodiscard]] EditProfileDraft draftFromProfile(const MREditExtensionProfile &profile);
[[nodiscard]] EditProfileDraft makeDefaultDraft();
[[nodiscard]] std::string buildProfileListLabel(const EditProfileDraft &draft, std::size_t idWidth);
[[nodiscard]] EditProfileDraft makeNewDraft(const std::vector<EditProfileDraft> &existingDrafts);
[[nodiscard]] EditProfileDraft makeCopiedDraft(const EditProfileDraft &source, const std::vector<EditProfileDraft> &existingDrafts);
[[nodiscard]] bool validateDraftsForUi(const std::vector<EditProfileDraft> &drafts, int currentIndex, const EditProfileDraft *currentDraftOverride, std::string &errorText);
[[nodiscard]] inline bool validateDraftsForUi(const std::vector<EditProfileDraft> &drafts, int currentIndex, std::string &errorText) {
	return validateDraftsForUi(drafts, currentIndex, nullptr, errorText);
}
[[nodiscard]] bool saveAndReloadEditProfiles(const std::vector<EditProfileDraft> &drafts, std::string &errorText);
[[nodiscard]] std::vector<std::string> dirtyDraftIds(const std::vector<EditProfileDraft> &initialDrafts, const std::vector<EditProfileDraft> &drafts);
[[nodiscard]] int focusedEditorProfileIndex(const std::vector<EditProfileDraft> &drafts);

} // namespace MRFileExtensionProfilesInternal

#endif
