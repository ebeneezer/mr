#ifndef MRSETTINGSEDITSETUP_HPP
#define MRSETTINGSEDITSETUP_HPP

#include "MRSettingsRuntime.hpp"

#include <string>
#include <string_view>
#include <vector>

const MREditSettingDescriptor *editSettingDescriptorByKeyInternal(const std::string &key);
std::string normalizeEditExtensionSelectorValue(const std::string &value);
std::string canonicalEditProfileId(const std::string &value);
std::string canonicalEditProfileName(const std::string &value);
std::string canonicalWindowColorThemeUri(const std::string &value);
std::string profileIdLookupKey(const std::string &value);
bool normalizeEditExtensionSelectorsInPlace(std::vector<std::string> &selectors, std::string *errorMessage);
bool normalizeEditProfileOverridesInPlace(MREditExtensionProfile &profile, std::string *errorMessage);
bool validateNormalizedEditProfiles(const std::vector<MREditExtensionProfile> &profiles, std::string *errorMessage);
bool applyEditSetupValueInternal(MREditSetupSettings &current, const std::string &keyName, const std::string &value, std::string *errorMessage);
std::string editSetupValueLiteral(const MREditSetupSettings &settings, const char *key);

#endif
