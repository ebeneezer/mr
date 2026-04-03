#ifndef MRSETUPDIALOGS_HPP
#define MRSETUPDIALOGS_HPP

class TDialog;

TDialog *createInstallationAndSetupDialog();
TDialog *createColorSetupDialog();
void runEditSettingsDialogFlow();
void runInstallationAndSetupDialogFlow();

#endif
