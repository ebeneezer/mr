#define Uses_TDialog
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"
#include "MRSetupDialogCommon.hpp"

#include <vector>

TDialog *createEditSettingsDialog() {
	std::vector<std::string> lines;
	lines.push_back("Page break string........ ?");
	lines.push_back("Word delimits........... .()'\\\",#$012%^&*+-/[]?");
	lines.push_back("Max undo count.......... 32000");
	lines.push_back("Default file extension(s) C:PAS;ASM;BAT;TXT;DO");
	lines.push_back("");
	lines.push_back("Cursor: Insert / Overwrite / Underline / 1/2 block");
	lines.push_back("        2/3 block / Full block");
	lines.push_back("Options: [X] Truncate spaces");
	lines.push_back("         [ ] Control-Z at EOF");
	lines.push_back("         [ ] CR/LF at EOF");
	lines.push_back("Tab expand: Spaces");
	lines.push_back("Column block move style: Delete space");
	lines.push_back("Default mode: Insert");
	return createSetupSimplePreviewDialog("EDIT SETTINGS", 76, 20, lines, true);
}
