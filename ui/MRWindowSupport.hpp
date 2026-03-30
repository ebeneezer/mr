#ifndef MRWINDOWSUPPORT_HPP
#define MRWINDOWSUPPORT_HPP

class TMREditWindow;

bool mrActivateEditWindow(TMREditWindow *win);
bool mrShowProjectHelp();
bool mrEnsureLogWindow(bool activate = true);
bool mrEnsureUsableWorkWindow();
void mrLogMessage(const char *message);

#endif
