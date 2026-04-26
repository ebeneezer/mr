#ifndef MRSETUPDIALOGS_HPP
#define MRSETUPDIALOGS_HPP

#include <string>

class TDialog;
class TPalette;

TDialog *createColorSetupDialog();
void runFileExtensionProfilesDialogFlow();
bool runSetupDialogCommand(unsigned short command);

// Regression-only hook used by regression/mr-regression-checks.cpp.
bool mrSaveColorThemeFromWorkingPaletteForTesting(const TPalette &workingPalette,
                                                  const std::string &themeUri,
                                                  std::string *errorMessage = nullptr);

#endif
