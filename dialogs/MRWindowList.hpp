#ifndef MRWINDOWLISTDIALOG_HPP
#define MRWINDOWLISTDIALOG_HPP

class MREditWindow;

enum MRWindowListMode {
	mrwlActivateWindow = 0,
	mrwlSelectLinkTarget = 1,
	mrwlManageWindows = 2
};

MREditWindow *mrShowWindowListDialog(MRWindowListMode mode, MREditWindow *current);
void mrRefreshManageWindowListDialog();
void mrNotifyWindowTopologyChanged();

#endif
