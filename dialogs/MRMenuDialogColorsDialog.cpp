#define Uses_TDialog
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"
#include "MRSetupDialogCommon.hpp"

#include <vector>

TDialog *createMenuDialogColorsDialog() {
	std::vector<std::string> lines;
	lines.push_back("activeentry-text");
	lines.push_back("inactiveentry-text");
	lines.push_back("entry-hotkey");
	lines.push_back("marker-active");
	lines.push_back("marker-inactive");
	lines.push_back("bUtton");
	lines.push_back("button-Key");
	lines.push_back("button-shAdow");
	lines.push_back("Select");
	lines.push_back("Not-select");
	lines.push_back("Checkbox bold");
	lines.push_back("List-cuRsor");
	lines.push_back("");
	lines.push_back("Preview:");
	lines.push_back("Window  Menu  File  Block");
	lines.push_back("(*) Select   ( ) Not-select");
	lines.push_back("List cursor");
	lines.push_back("Button<KEY>");
	return createSetupSimplePreviewDialogForProfile("MENU / DIALOG COLORS", 36, 19, 44, 23, lines, false);
}
