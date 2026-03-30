#ifndef MRWINDOWLISTDIALOG_HPP
#define MRWINDOWLISTDIALOG_HPP

class TMREditWindow;

enum MRWindowListMode {
	mrwlActivateWindow = 0,
	mrwlSelectLinkTarget = 1
};

TMREditWindow *mrShowWindowListDialog(MRWindowListMode mode, TMREditWindow *current);

#endif
