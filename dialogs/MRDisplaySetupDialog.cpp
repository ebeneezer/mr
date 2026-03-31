#define Uses_TDialog
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"
#include "MRSetupDialogCommon.hpp"

#include <vector>

TDialog *createDisplaySetupDialog() {
	std::vector<std::string> lines;
	lines.push_back("Video mode");
	lines.push_back("  (*) 25 lines");
	lines.push_back("  ( ) 30/33 lines");
	lines.push_back("  ( ) 43/50 lines");
	lines.push_back("  ( ) UltraVision");
	lines.push_back("F-key labels delay (1/10 secs): 3");
	lines.push_back("UltraVision mode (hex): 00");
	lines.push_back("");
	lines.push_back("Screen layout");
	lines.push_back("  [X] Status/message line");
	lines.push_back("  [X] Menu bar");
	lines.push_back("  [X] Function key labels");
	lines.push_back("  [X] Left-hand border");
	lines.push_back("  [X] Right-hand border");
	lines.push_back("  [X] Bottom border");
	return createSetupSimplePreviewDialog("DISPLAY SETUP", 60, 20, lines, true);
}
