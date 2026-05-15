#ifndef MRSETTINGSNORMALIZE_HPP
#define MRSETTINGSNORMALIZE_HPP

#include "MRSettingsAssignments.hpp"
#include "MRSettingsSnapshotIO.hpp"
#include "MRSettingsStorage.hpp"

#include <string>
bool loadAndNormalizeSettingsSource(const std::string &settingsPath, const std::string &source, MRSettingsSnapshot &snapshot, MRSettingsLoadReport *report, std::string *errorMessage);

#endif
