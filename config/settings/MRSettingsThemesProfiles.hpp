#ifndef MRSETTINGSTHEMESPROFILES_HPP
#define MRSETTINGSTHEMESPROFILES_HPP

#include <map>
#include <string>
#include <string_view>

std::string defaultColorThemePathForSettings(std::string_view settingsPath);
bool applyColorSetupValueInternal(struct MRColorSetupSettings &configured, const std::string &key, const std::string &value, std::string *errorMessage);
bool parseThemeSetupAssignments(const std::string &source, std::map<std::string, std::string> &assignments, bool *upgradeRequired, std::string *errorMessage);
void ensureConfiguredColorSettingsInitialized();

#endif
