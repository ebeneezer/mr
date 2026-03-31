#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TRect
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#include <tvision/tv.h>

#include "MRSetupDialogCommon.hpp"

#include <cstring>

TRect centeredSetupDialogRect(int width, int height) {
	TRect r = TProgram::deskTop->getExtent();
	int left = r.a.x + (r.b.x - r.a.x - width) / 2;
	int top = r.a.y + (r.b.y - r.a.y - height) / 2;
	return TRect(left, top, left + width, top + height);
}

void insertSetupStaticLine(TDialog *dialog, int x, int y, const char *text) {
	dialog->insert(new TStaticText(TRect(x, y, x + std::strlen(text) + 1, y + 1), text));
}

TDialog *createSetupSimplePreviewDialog(const char *title, int width, int height,
                                        const std::vector<std::string> &lines,
                                        bool showOkCancelHelp) {
	TDialog *dialog = new TDialog(centeredSetupDialogRect(width, height), title);
	int y = 2;

	for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it, ++y)
		insertSetupStaticLine(dialog, 2, y, it->c_str());

	if (showOkCancelHelp) {
		dialog->insert(new TButton(TRect(width - 34, height - 3, width - 24, height - 1), "OK",
		                           cmOK, bfDefault));
		dialog->insert(new TButton(TRect(width - 23, height - 3, width - 10, height - 1), "Cancel",
		                           cmCancel, bfNormal));
		dialog->insert(new TButton(TRect(width - 9, height - 3, width - 2, height - 1), "Help",
		                           cmHelp, bfNormal));
	} else {
		dialog->insert(new TButton(TRect(width / 2 - 4, height - 3, width / 2 + 4, height - 1),
		                           "Done", cmOK, bfDefault));
	}

	return dialog;
}
