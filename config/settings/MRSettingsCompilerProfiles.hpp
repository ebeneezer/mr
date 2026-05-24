#ifndef MRSETTINGSCOMPILERPROFILES_HPP
#define MRSETTINGSCOMPILERPROFILES_HPP

#include "MRSettingsRuntime.hpp"

#include <string>
#include <vector>

[[nodiscard]] std::string normalizeCompilerProfilePathList(const std::vector<std::string> &paths);
[[nodiscard]] std::vector<std::string> splitCompilerProfilePathList(const std::string &value);
bool normalizeCompilerProfileInPlace(MRCompilerProfile &profile, std::string *errorMessage);
bool validateCompilerProfiles(const std::vector<MRCompilerProfile> &profiles, std::string *errorMessage);
[[nodiscard]] std::vector<std::string> defaultCompilerExecutablePaths();
bool autoConfigureCompilerProfileFromExecutable(MRCompilerProfile &profile, std::string *errorMessage);

#endif
