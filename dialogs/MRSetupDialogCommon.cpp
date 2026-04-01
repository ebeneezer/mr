#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TRect
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#include <tvision/tv.h>

#include "MRSetupDialogCommon.hpp"

#include <algorithm>
#include <cstring>

MRSetupLayoutProfile currentSetupLayoutProfile() {
	static const int kCompactMaxWidth = 80;
	static const int kCompactMaxHeight = 25;
	static const int kRelaxedMinWidth = 84;
	static const int kRelaxedMinHeight = 24;
	TRect r;
	int width;
	int height;

	if (TProgram::deskTop == nullptr)
		return mrSetupLayoutCompact;
	r = TProgram::deskTop->getExtent();
	width = r.b.x - r.a.x;
	height = r.b.y - r.a.y;
	if (width <= kCompactMaxWidth && height <= kCompactMaxHeight)
		return mrSetupLayoutCompact;
	if (width < kRelaxedMinWidth || height < kRelaxedMinHeight)
		return mrSetupLayoutCompact;
	return mrSetupLayoutRelaxed;
}

bool isSetupLayoutCompact() {
	return currentSetupLayoutProfile() == mrSetupLayoutCompact;
}

TRect centeredSetupDialogRect(int width, int height) {
	TRect r = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, 80, 25);
	int availableWidth = std::max(1, r.b.x - r.a.x);
	int availableHeight = std::max(1, r.b.y - r.a.y);
	int safeWidth = std::max(10, std::min(width, availableWidth));
	int safeHeight = std::max(6, std::min(height, availableHeight));
	int left = r.a.x + std::max(0, (availableWidth - safeWidth) / 2);
	int top = r.a.y + std::max(0, (availableHeight - safeHeight) / 2);

	return TRect(left, top, left + safeWidth, top + safeHeight);
}

TRect centeredSetupDialogRectForProfile(int compactWidth, int compactHeight, int relaxedWidth,
                                        int relaxedHeight) {
	if (isSetupLayoutCompact())
		return centeredSetupDialogRect(compactWidth, compactHeight);
	return centeredSetupDialogRect(relaxedWidth, relaxedHeight);
}

void insertSetupStaticLine(TDialog *dialog, int x, int y, const char *text) {
	dialog->insert(new TStaticText(TRect(x, y, x + std::strlen(text) + 1, y + 1), text));
}

TDialog *createSetupSimplePreviewDialog(const char *title, int width, int height,
                                        const std::vector<std::string> &lines,
                                        bool showOkCancelHelp) {
	TDialog *dialog = new TDialog(centeredSetupDialogRect(width, height), title);
	int dialogWidth = dialog != nullptr ? dialog->size.x : width;
	int dialogHeight = dialog != nullptr ? dialog->size.y : height;
	int y = 2;

	for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it, ++y)
		insertSetupStaticLine(dialog, 2, y, it->c_str());

	if (showOkCancelHelp) {
		dialog->insert(new TButton(TRect(dialogWidth - 34, dialogHeight - 3, dialogWidth - 24, dialogHeight - 1), "OK",
		                           cmOK, bfDefault));
		dialog->insert(new TButton(TRect(dialogWidth - 23, dialogHeight - 3, dialogWidth - 10, dialogHeight - 1),
		                           "Cancel", cmCancel, bfNormal));
		dialog->insert(new TButton(TRect(dialogWidth - 9, dialogHeight - 3, dialogWidth - 2, dialogHeight - 1), "Help",
		                           cmHelp, bfNormal));
	} else {
		dialog->insert(new TButton(TRect(dialogWidth / 2 - 4, dialogHeight - 3, dialogWidth / 2 + 4, dialogHeight - 1),
		                           "Done", cmOK, bfDefault));
	}

	return dialog;
}

TDialog *createSetupSimplePreviewDialogForProfile(const char *title, int compactWidth, int compactHeight,
                                                  int relaxedWidth, int relaxedHeight,
                                                  const std::vector<std::string> &lines,
                                                  bool showOkCancelHelp) {
	if (isSetupLayoutCompact())
		return createSetupSimplePreviewDialog(title, compactWidth, compactHeight, lines, showOkCancelHelp);
	return createSetupSimplePreviewDialog(title, relaxedWidth, relaxedHeight, lines, showOkCancelHelp);
}
