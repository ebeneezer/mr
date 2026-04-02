#ifndef MRSETUPDIALOGS_HPP
#define MRSETUPDIALOGS_HPP

class TDialog;

TDialog *createInstallationAndSetupDialog();
TDialog *createColorSetupDialog();
TDialog *createEditSettingsDialog();
TDialog *createWindowColorsDialog();
TDialog *createMenuDialogColorsDialog();
TDialog *createHelpColorsDialog();
TDialog *createOtherColorsDialog();
void runEditSettingsDialogFlow();
void runInstallationAndSetupDialogFlow();

#endif
