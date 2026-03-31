#ifndef MRWINDOWSUPPORT_HPP
#define MRWINDOWSUPPORT_HPP

class TMREditWindow;

bool mrActivateEditWindow(TMREditWindow *win);
bool mrShowProjectHelp();
bool mrEnsureLogWindow(bool activate = true);
bool mrEnsureUsableWorkWindow();
bool mrClearLogWindow();
void mrLogMessage(const char *message);

#endif
