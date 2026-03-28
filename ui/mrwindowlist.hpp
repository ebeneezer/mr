#ifndef MRWINDOWLIST_HPP
#define MRWINDOWLIST_HPP

class TMREditWindow;

enum MRWindowListMode {
	mrwlActivateWindow = 0,
	mrwlSelectLinkTarget = 1
};

TMREditWindow *mrShowWindowListDialog(MRWindowListMode mode, TMREditWindow *current);
bool mrActivateEditWindow(TMREditWindow *win);
bool mrShowProjectHelp();
bool mrEnsureLogWindow(bool activate = true);
bool mrEnsureUsableWorkWindow();
void mrLogMessage(const char *message);

#endif
