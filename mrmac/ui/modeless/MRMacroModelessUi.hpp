#ifndef MRMACRO_MODELESS_UI_HPP
#define MRMACRO_MODELESS_UI_HPP

#include <map>
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

struct MRMacroModelessTextFieldSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	std::string fieldId;
	std::string label;
	std::string text;
};

struct MRMacroModelessBoolFieldSpec {
	int x = 0;
	int y = 0;
	std::string fieldId;
	std::string caption;
	bool value = false;
};

struct MRMacroModelessIntFieldSpec {
	int x = 0;
	int y = 0;
	int width = 8;
	std::string fieldId;
	std::string label;
	int minimum = 0;
	int maximum = 100;
	int value = 0;
};

struct MRMacroModelessProgressFieldSpec {
	int x = 0;
	int y = 0;
	int width = 16;
	std::string fieldId;
	std::string label;
	int total = 100;
	int value = 0;
};

struct MRMacroModelessLogFieldSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	int height = 4;
	std::string logId;
	std::string label;
	int capacity = 16;
};

struct MRMacroModelessSelectFieldSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	int height = 4;
	std::string fieldId;
	std::string label;
	std::string value;
	std::vector<std::string> options;
};

struct MRMacroModelessCanvasSpec {
	int x = 0;
	int y = 0;
	int width = 20;
	int height = 6;
	std::string canvasId;
};

struct MRMacroModelessCanvasHotspotSpec {
	std::string canvasId;
	int x = 0;
	int y = 0;
	int width = 1;
	int height = 1;
	int id = 0;
	std::string macroSpec;
};

enum class MRMacroModelessCanvasCommandType : unsigned char {
	Clear = 0,
	Text,
	Glyph,
	Line,
	Box,
	Fill
};

struct MRMacroModelessCanvasCommand {
	MRMacroModelessCanvasCommandType type = MRMacroModelessCanvasCommandType::Clear;
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	int x2 = 0;
	int y2 = 0;
	int style = 0;
	std::string text;
};

struct MRMacroModelessCanvasScene {
	unsigned long generation = 0;
	std::vector<MRMacroModelessCanvasCommand> commands;
};

struct MRMacroModelessSelectionSpec {
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

using MRMacroModelessListBoxSpec = MRMacroModelessSelectionSpec;
using MRMacroModelessGridSpec = MRMacroModelessSelectionSpec;
using MRMacroModelessTreeSpec = MRMacroModelessSelectionSpec;
using MRMacroModelessTableSpec = MRMacroModelessSelectionSpec;

struct MRMacroModelessWindowDefinition {
	int x = 0;
	int y = 0;
	int width = 40;
	int height = 12;
	std::string windowId;
	std::string title;
	std::vector<MRMacroModelessLabelSpec> labels;
	std::vector<MRMacroModelessDisplaySpec> displays;
	std::map<std::string, int> statusDisplayIndices;
	std::vector<MRMacroModelessTextFieldSpec> textFields;
	std::vector<MRMacroModelessBoolFieldSpec> boolFields;
	std::vector<MRMacroModelessIntFieldSpec> intFields;
	std::vector<MRMacroModelessProgressFieldSpec> progressFields;
	std::vector<MRMacroModelessLogFieldSpec> logFields;
	std::vector<MRMacroModelessSelectFieldSpec> selectFields;
	std::vector<MRMacroModelessCanvasSpec> canvases;
	std::vector<MRMacroModelessCanvasHotspotSpec> canvasHotspots;
	std::vector<MRMacroModelessButtonSpec> buttons;
	std::vector<MRMacroModelessListBoxSpec> listBoxes;
	std::vector<MRMacroModelessGridSpec> grids;
	std::vector<MRMacroModelessTreeSpec> trees;
	std::vector<MRMacroModelessTableSpec> tables;
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
bool updateMacroModelessTextField(const std::string &windowId, const std::string &fieldId, const std::string &text);
bool updateMacroModelessBoolField(const std::string &windowId, const std::string &fieldId, bool value);
bool updateMacroModelessIntField(const std::string &windowId, const std::string &fieldId, int value);
bool updateMacroModelessProgressField(const std::string &windowId, const std::string &fieldId);
bool updateMacroModelessLogField(const std::string &windowId, const std::string &logId);
bool updateMacroModelessSelectField(const std::string &windowId, const std::string &fieldId, const std::string &value);
bool commitMacroModelessCanvas(const std::string &windowId, const std::string &canvasId);
bool closeMacroModelessWindow(const std::string &windowId);
void refreshMacroModelessWindows();

#endif
