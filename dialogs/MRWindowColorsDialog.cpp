#define Uses_TDialog
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"
#include "MRSetupDialogCommon.hpp"

#include <vector>

TDialog *createWindowColorsDialog() {
	std::vector<std::string> lines;
	lines.push_back("Text");
	lines.push_back("Changed-Text");
	lines.push_back("Highlighted-Text");
	lines.push_back("End-Of-File");
	lines.push_back("Window-border");
	lines.push_back("window-Bold");
	lines.push_back("cUrrent line");
	lines.push_back("cuRrent line in block");
	lines.push_back("");
	lines.push_back("Preview:");
	lines.push_back("Normal text");
	lines.push_back("Changed text");
	lines.push_back("Highlighted text");
	lines.push_back("Current line");
	lines.push_back("Current line in block");
	return createSetupSimplePreviewDialog("WINDOW COLORS", 34, 20, lines, false);
}
