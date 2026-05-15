#ifndef MRSETUPSECTIONS_HPP
#define MRSETUPSECTIONS_HPP

#include <string>

class TDialog;
class TPalette;

TDialog *createColorSetupDialog();
void runColorSetupDialogFlow();
void runBackupsAutosaveDialogFlow();
void runPathsSetupDialogFlow();
void runUserInterfaceSettingsDialogFlow();

// Regression-only hook used by regression/mr-regression-checks.cpp.
bool mrSaveColorThemeFromWorkingPaletteForTesting(const TPalette &workingPalette, const std::string &themeUri, std::string *errorMessage);

#endif
