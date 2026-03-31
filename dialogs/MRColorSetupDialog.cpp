#define Uses_TDialog
#define Uses_TButton
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"
#include "MRSetupDialogCommon.hpp"

#include "../app/MRCommands.hpp"

TDialog *createColorSetupDialog() {
	TDialog *dialog = new TDialog(centeredSetupDialogRect(32, 11), "COLOR SETUP");
	dialog->insert(new TButton(TRect(2, 2, 29, 4), "Window colors", cmMrColorWindowColors, bfNormal));
	dialog->insert(new TButton(TRect(2, 4, 29, 6), "Menu/Dialog colors", cmMrColorMenuDialogColors, bfNormal));
	dialog->insert(new TButton(TRect(2, 6, 29, 8), "Help colors", cmMrColorHelpColors, bfNormal));
	dialog->insert(new TButton(TRect(2, 8, 29, 10), "Other colors", cmMrColorOtherColors, bfNormal));
	return dialog;
}
