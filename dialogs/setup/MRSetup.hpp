#ifndef MRSETUPDIALOGS_HPP
#define MRSETUPDIALOGS_HPP

class TDialog;
class TPalette;

TDialog *createColorSetupDialog();
void runFileExtensionProfilesDialogFlow();
void runCompilerProfilesDialogFlow();
bool runSetupDialogCommand(unsigned short command);

#endif
