#ifndef MRMACRO_MODELESS_UI_HPP
#define MRMACRO_MODELESS_UI_HPP

#include <string>
#include <vector>

struct MRMacroModelessLabelSpec {
	int x = 0;
	int y = 0;
	std::string text;
};

struct MRMacroModelessButtonSpec {
	int x = 0;
	int y = 0;
	int width = 8;
	int id = 0;
	std::string text;
	std::string macroSpec;
};

struct MRMacroModelessDisplaySpec {
	int x = 0;
	int y = 0;
	int width = 20;
	std::string text;
};

struct MRMacroModelessListBoxSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	int height = 4;
	int id = 0;
	std::string label;
	std::string itemSpec;
	int start = 1;
};

struct MRMacroModelessGridSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	int height = 4;
	int id = 0;
	std::string label;
	std::string itemSpec;
	std::string macroSpec;
	int start = 1;
};

struct MRMacroModelessWindowDefinition {
	int x = 0;
	int y = 0;
	int width = 40;
	int height = 12;
	std::string windowId;
	std::string title;
	std::vector<MRMacroModelessLabelSpec> labels;
	std::vector<MRMacroModelessDisplaySpec> displays;
	std::vector<MRMacroModelessButtonSpec> buttons;
	std::vector<MRMacroModelessListBoxSpec> listBoxes;
	std::vector<MRMacroModelessGridSpec> grids;
};

struct MRMacroModelessSelection {
	int controlId = 0;
	int index = 0;
	std::string text;
};

using MRMacroModelessListResolver = std::vector<std::string> (*)(const std::string &itemSpec);
using MRMacroModelessCommandRunner = void (*)(const std::string &windowId, int buttonId, const MRMacroModelessSelection &selection, const std::string &macroSpec);

void setMacroModelessListResolver(MRMacroModelessListResolver resolver);
void setMacroModelessCommandRunner(MRMacroModelessCommandRunner runner);
bool showMacroModelessWindow(const MRMacroModelessWindowDefinition &definition);
bool updateMacroModelessWindow(const MRMacroModelessWindowDefinition &definition);
bool updateMacroModelessDisplay(const std::string &windowId, int displayIndex, const std::string &text);
bool closeMacroModelessWindow(const std::string &windowId);
void refreshMacroModelessWindows();

#endif
