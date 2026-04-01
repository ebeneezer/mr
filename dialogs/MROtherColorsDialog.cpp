#define Uses_TDialog
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"
#include "MRSetupDialogCommon.hpp"

#include <vector>

TDialog *createOtherColorsDialog() {
	std::vector<std::string> lines;
	lines.push_back("statusline");
	lines.push_back("statusline-Bold");
	lines.push_back("Fkey-Labels");
	lines.push_back("fkey-Numbers");
	lines.push_back("Error");
	lines.push_back("Message");
	lines.push_back("Working");
	lines.push_back("shadow");
	lines.push_back("shadow-Character");
	lines.push_back("Background color");
	lines.push_back("");
	lines.push_back("Message  WORKING  L:333 C:10");
	lines.push_back("ERROR BOX");
	lines.push_back("1Help 2Save 3Load 4Indent");
	return createSetupSimplePreviewDialogForProfile("OTHER COLORS", 34, 18, 42, 22, lines, false);
}
