#define Uses_TDialog
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"
#include "MRSetupDialogCommon.hpp"

#include <vector>

TDialog *createHelpColorsDialog() {
	std::vector<std::string> lines;
	lines.push_back("Help-Text");
	lines.push_back("help-Highlight");
	lines.push_back("help-Chapter");
	lines.push_back("help-Border");
	lines.push_back("help-Link");
	lines.push_back("help-F-keys");
	lines.push_back("help-attr-1");
	lines.push_back("help-attr-2");
	lines.push_back("help-attr-3");
	lines.push_back("");
	lines.push_back("SAMPLE HELP");
	lines.push_back("CHAPTER HEADING");
	lines.push_back("This is help text.");
	lines.push_back("This is a LINK");
	lines.push_back("Attr1, Attr2, Attr3");
	return createSetupSimplePreviewDialogForProfile("HELP COLORS", 34, 18, 42, 22, lines, false);
}
