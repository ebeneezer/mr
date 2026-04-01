#define Uses_TDialog
#define Uses_TButton
#define Uses_TEvent
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"
#include "MRSetupDialogCommon.hpp"

#include "../app/MRCommands.hpp"

namespace {
bool isColorSetupModalCommand(ushort command) {
	switch (command) {
		case cmMrColorWindowColors:
		case cmMrColorMenuDialogColors:
		case cmMrColorHelpColors:
		case cmMrColorOtherColors:
			return true;
		default:
			return false;
	}
}

class TColorSetupDialog : public TDialog {
  public:
	TColorSetupDialog(const TRect &bounds, const char *title) : TWindowInit(&TDialog::initFrame), TDialog(bounds, title) {
	}

	void handleEvent(TEvent &event) override {
		TDialog::handleEvent(event);
		if (event.what == evCommand && isColorSetupModalCommand(event.message.command)) {
			endModal(event.message.command);
			clearEvent(event);
		}
	}
};
} // namespace

TDialog *createColorSetupDialog() {
	bool compact = isSetupLayoutCompact();
	int width = compact ? 32 : 40;
	int height = compact ? 11 : 13;
	int left = 2;
	int right = width - 2;
	int row = 2;
	TDialog *dialog = new TColorSetupDialog(centeredSetupDialogRect(width, height), "COLOR SETUP");

	dialog->insert(new TButton(TRect(left, row, right, row + 2), "Window colors", cmMrColorWindowColors, bfNormal));
	row += 2;
	dialog->insert(
	    new TButton(TRect(left, row, right, row + 2), "Menu/Dialog colors", cmMrColorMenuDialogColors, bfNormal));
	row += 2;
	dialog->insert(new TButton(TRect(left, row, right, row + 2), "Help colors", cmMrColorHelpColors, bfNormal));
	row += 2;
	dialog->insert(new TButton(TRect(left, row, right, row + 2), "Other colors", cmMrColorOtherColors, bfNormal));
	return dialog;
}
