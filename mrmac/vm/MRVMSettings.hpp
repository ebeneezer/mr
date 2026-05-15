#ifndef MRVM_SETTINGS_HPP
#define MRVM_SETTINGS_HPP

#include <string>

bool mrvmApplyConfiguredActiveKeymapProfilePayload(const std::string &payload, std::string *errorMessage);
bool mrvmApplyConfiguredKeymapProfilePayload(const std::string &payload, std::string *errorMessage);
bool mrvmApplyConfiguredKeymapBindingPayload(const std::string &payload, std::string *errorMessage);
bool mrvmPersistConfiguredSettingsSnapshot(std::string *errorMessage);

#endif
