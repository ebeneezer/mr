#ifndef MRSETUPSECTIONS_HPP
#define MRSETUPSECTIONS_HPP

class TDialog;
class TPalette;

TDialog *createColorSetupDialog();
void runColorSetupDialogFlow();
void runBackupsAutosaveDialogFlow();
void runPathsSetupDialogFlow();
void runUserInterfaceSettingsDialogFlow();
void runLiveLogsSetupDialogFlow();

#endif
