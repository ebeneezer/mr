#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_MsgBox
#define Uses_TObject
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"

#include "../app/MRCommands.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {
ushort execDialog(TDialog *dialog) {
	ushort result = cmCancel;
	if (dialog != 0) {
		result = TProgram::deskTop->execView(dialog);
		TObject::destroy(dialog);
		if (result == cmHelp)
			mrShowProjectHelp();
	}
	return result;
}
} // namespace

void runColorSetupDialogFlowLocal() {
	bool running = true;

	while (running) {
		ushort result = execDialog(createColorSetupDialog());
		switch (result) {
			case cmMrColorWindowColors:
				execDialog(createWindowColorsDialog());
				break;

			case cmMrColorMenuDialogColors:
				execDialog(createMenuDialogColorsDialog());
				break;

			case cmMrColorHelpColors:
				execDialog(createHelpColorsDialog());
				break;

			case cmMrColorOtherColors:
				execDialog(createOtherColorsDialog());
				break;

			case cmCancel:
			default:
				running = false;
				break;
		}
	}
}

void runInstallationAndSetupDialogFlow() {
	bool running = true;

	while (running) {
		ushort result = execDialog(createInstallationAndSetupDialog());
		switch (result) {
			case cmMrSetupEditSettings:
				execDialog(createEditSettingsDialog());
				break;

			case cmMrSetupDisplaySetup:
				execDialog(createDisplaySetupDialog());
				break;

			case cmMrSetupColorSetup:
				runColorSetupDialogFlowLocal();
				break;

			case cmMrSetupSearchAndReplaceDefaults:
				messageBox(mfInformation | mfOKButton,
				           "Installation / Search and Replace defaults\n\nDummy implementation for now.");
				break;

			case cmMrSetupSaveConfigurationAndExit:
				messageBox(mfInformation | mfOKButton,
				           "Installation / Save configuration and exit\n\nDummy implementation for now.");
				running = false;
				break;

			case cmCancel:
			default:
				running = false;
				break;
		}
	}
}
