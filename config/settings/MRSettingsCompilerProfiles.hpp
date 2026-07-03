#ifndef MRSETTINGSCOMPILERPROFILES_HPP
#define MRSETTINGSCOMPILERPROFILES_HPP

#include "MRSettingsRuntime.hpp"

#include <string>
#include <vector>

[[nodiscard]] std::string normalizeCompilerProfilePathList(const std::vector<std::string> &paths);
[[nodiscard]] std::vector<std::string> splitCompilerProfilePathList(const std::string &value);
bool normalizeCompilerProfileInPlace(MRCompilerProfile &profile, std::string *errorMessage);
bool validateCompilerProfiles(const std::vector<MRCompilerProfile> &profiles, std::string *errorMessage);
bool applyCompilerProfileDirectiveToVector(std::vector<MRCompilerProfile> &profiles, const std::string &operation, const std::string &profileId, const std::string &arg3, const std::string &arg4, bool *changed, std::string *errorMessage);
[[nodiscard]] std::vector<std::string> detectedCompilerExecutablePaths();
bool autoConfigureCompilerProfileFromExecutable(MRCompilerProfile &profile, std::string *errorMessage);

#endif
