#ifndef MREDITPROFILESRECORDSUPPORT_HPP
#define MREDITPROFILESRECORDSUPPORT_HPP

#include "MREditProfilesPanelInternal.hpp"

#include <cstddef>
#include <string>

struct MREditSetupSettings;

namespace MREditProfilesDialogInternal {

[[nodiscard]] std::string trimAscii(const std::string &value);
[[nodiscard]] std::string upperAscii(std::string value);
[[nodiscard]] std::string readRecordField(const char *value);
void writeRecordField(char *dest, std::size_t destSize, const std::string &value);
[[nodiscard]] bool recordsEqual(const EditSettingsDialogRecord &lhs, const EditSettingsDialogRecord &rhs);
void initEditSettingsDialogRecord(EditSettingsDialogRecord &record);
[[nodiscard]] bool recordToSettings(const EditSettingsDialogRecord &record, MREditSetupSettings &settings,
                      std::string &errorText);
[[nodiscard]] bool saveAndReloadEditSettings(const EditSettingsDialogRecord &record, std::string &errorText);

} // namespace MREditProfilesDialogInternal

#endif
