#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TRect
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#define Uses_MsgBox
#define Uses_TObject
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>

#include "../app/MRCommands.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {
std::string trimmedValue(const char *value, const char *fallback = "<unknown>") {
	if (value == nullptr || *value == '\0')
		return std::string(fallback);
	return std::string(value);
}

std::string currentWorkingDirectory() {
	char cwd[1024];
	if (::getcwd(cwd, sizeof(cwd)) == nullptr)
		return std::string("<unknown>");
	return std::string(cwd);
}

std::string currentOsDescription() {
	struct utsname info;
	if (::uname(&info) != 0)
		return std::string("<unknown>");
	return std::string(info.sysname) + " " + info.release + " (" + info.machine + ")";
}

struct CpuTopologyInfo {
	long logicalProcessors;
	long physicalPackages;
	long physicalCores;

	CpuTopologyInfo() noexcept : logicalProcessors(0), physicalPackages(0), physicalCores(0) {
	}
};

CpuTopologyInfo currentCpuTopology() {
	CpuTopologyInfo info;
	std::ifstream in("/proc/cpuinfo");
	std::string line;
	std::string physicalId;
	std::string coreId;
	bool sawProcessor = false;
	std::set<std::string> packages;
	std::set<std::string> cores;

	if (!in)
		return info;

	auto commitProcessor = [&]() {
		if (!sawProcessor)
			return;
		++info.logicalProcessors;
		if (!physicalId.empty())
			packages.insert(physicalId);
		if (!physicalId.empty() && !coreId.empty())
			cores.insert(physicalId + ":" + coreId);
		physicalId.clear();
		coreId.clear();
		sawProcessor = false;
	};

	while (std::getline(in, line)) {
		if (line.empty()) {
			commitProcessor();
			continue;
		}
		if (line.rfind("processor", 0) == 0) {
			sawProcessor = true;
			continue;
		}
		if (line.rfind("physical id", 0) == 0) {
			std::size_t pos = line.find(':');
			if (pos != std::string::npos)
				physicalId = trimmedValue(line.c_str() + pos + 1, "");
			if (!physicalId.empty() && physicalId[0] == ' ')
				physicalId.erase(0, physicalId.find_first_not_of(' '));
			continue;
		}
		if (line.rfind("core id", 0) == 0) {
			std::size_t pos = line.find(':');
			if (pos != std::string::npos)
				coreId = trimmedValue(line.c_str() + pos + 1, "");
			if (!coreId.empty() && coreId[0] == ' ')
				coreId.erase(0, coreId.find_first_not_of(' '));
			continue;
		}
	}
	commitProcessor();

	info.physicalPackages = static_cast<long>(packages.size());
	info.physicalCores = static_cast<long>(cores.size());
	if (info.logicalProcessors <= 0)
		info.logicalProcessors = ::sysconf(_SC_NPROCESSORS_ONLN);
	if (info.physicalCores <= 0)
		info.physicalCores = info.logicalProcessors;
	if (info.physicalPackages <= 0)
		info.physicalPackages = 1;
	return info;
}

std::string currentCpuDescription() {
	CpuTopologyInfo cpu = currentCpuTopology();
	char buffer[128];
	std::snprintf(buffer, sizeof(buffer), "%ld logical / %ld cores / %ld package%s", cpu.logicalProcessors,
	              cpu.physicalCores, cpu.physicalPackages, cpu.physicalPackages == 1 ? "" : "s");
	return std::string(buffer);
}

std::string currentRamDescription() {
	long pages = ::sysconf(_SC_PHYS_PAGES);
	long pageSize = ::sysconf(_SC_PAGE_SIZE);
	if (pages <= 0 || pageSize <= 0)
		return std::string("<unknown>");

	long double bytes = static_cast<long double>(pages) * static_cast<long double>(pageSize);
	long double gib = bytes / (1024.0L * 1024.0L * 1024.0L);
	char buffer[64];
	std::snprintf(buffer, sizeof(buffer), "%.1Lf GiB", gib);
	return std::string(buffer);
}

std::string fitSetupLine(const std::string &text, std::size_t maxChars) {
	if (text.size() <= maxChars)
		return text;
	if (maxChars <= 3)
		return text.substr(0, maxChars);
	return text.substr(0, maxChars - 3) + "...";
}

std::string buildSetupInfoLine(const char *label, const std::string &value, std::size_t maxChars) {
	return fitSetupLine(std::string(label) + " = " + value, maxChars);
}

ushort execDialog(TDialog *dialog) {
	ushort result = cmCancel;
	if (dialog != 0) {
		result = TProgram::deskTop->execView(dialog);
		TObject::destroy(dialog);
		if (result == cmHelp)
			mrShowProjectHelp();
	}
	return result;
}

TRect centeredRect(int width, int height) {
	TRect r = TProgram::deskTop->getExtent();
	int left = r.a.x + (r.b.x - r.a.x - width) / 2;
	int top = r.a.y + (r.b.y - r.a.y - height) / 2;
	return TRect(left, top, left + width, top + height);
}

void insertStaticLine(TDialog *dialog, int x, int y, const char *text) {
	dialog->insert(new TStaticText(TRect(x, y, x + std::strlen(text) + 1, y + 1), text));
}

TDialog *createSimplePreviewDialog(const char *title, int width, int height,
                                   const std::vector<std::string> &lines,
                                   bool showOkCancelHelp) {
	TDialog *dialog = new TDialog(centeredRect(width, height), title);
	int y = 2;

	for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it, ++y)
		insertStaticLine(dialog, 2, y, it->c_str());

	if (showOkCancelHelp) {
		dialog->insert(new TButton(TRect(width - 34, height - 3, width - 24, height - 1), "OK",
		                           cmOK, bfDefault));
		dialog->insert(new TButton(TRect(width - 23, height - 3, width - 10, height - 1), "Cancel",
		                           cmCancel, bfNormal));
		dialog->insert(new TButton(TRect(width - 9, height - 3, width - 2, height - 1), "Help",
		                           cmHelp, bfNormal));
	} else
		dialog->insert(new TButton(TRect(width / 2 - 4, height - 3, width / 2 + 4, height - 1),
		                           "Done", cmOK, bfDefault));

	return dialog;
}
} // namespace

TDialog *createInstallationAndSetupDialog() {
	static constexpr int kDialogWidth = 72;
	static constexpr int kDialogHeight = 23;
	static constexpr int kLeftX1 = 2;
	static constexpr int kLeftX2 = 35;
	static constexpr int kRightX1 = 37;
	static constexpr int kRightX2 = 70;
	static constexpr std::size_t kInfoWidth = 67;
	static constexpr std::size_t kColumnWidth = 30;

	TDialog *dialog = new TDialog(centeredRect(kDialogWidth, kDialogHeight), "INSTALLATION AND SETUP");

	insertStaticLine(dialog, 2, 2, buildSetupInfoLine("OS", currentOsDescription(), kInfoWidth).c_str());
	insertStaticLine(
	    dialog, 2, 3, buildSetupInfoLine("Shell", trimmedValue(std::getenv("SHELL")), kColumnWidth).c_str());
	insertStaticLine(
	    dialog, 37, 3, buildSetupInfoLine("TERM", trimmedValue(std::getenv("TERM")), kColumnWidth).c_str());
	insertStaticLine(dialog, 2, 4, buildSetupInfoLine("MR Path", currentWorkingDirectory(), kColumnWidth).c_str());
	insertStaticLine(dialog, 37, 4, buildSetupInfoLine("CPU", currentCpuDescription(), kColumnWidth).c_str());
	insertStaticLine(dialog, 2, 5, buildSetupInfoLine("RAM", currentRamDescription(), kColumnWidth).c_str());

	dialog->insert(
	    new TButton(TRect(kLeftX1, 9, kLeftX2, 11), "Edit settings...", cmMrSetupEditSettings, bfNormal));
	dialog->insert(
	    new TButton(TRect(kLeftX1, 11, kLeftX2, 13), "Display setup...", cmMrSetupDisplaySetup, bfNormal));
	dialog->insert(
	    new TButton(TRect(kLeftX1, 13, kLeftX2, 15), "Color setup...", cmMrSetupColorSetup, bfNormal));
	dialog->insert(
	    new TButton(TRect(kLeftX1, 15, kLeftX2, 17), "Key mapping...", cmMrSetupKeyMapping, bfNormal));
	dialog->insert(new TButton(TRect(kRightX1, 9, kRightX2, 11), "Mouse / Key repeat...",
	                           cmMrSetupMouseKeyRepeat, bfNormal));
	dialog->insert(new TButton(TRect(kRightX1, 11, kRightX2, 13), "Filename extensions...",
	                           cmMrSetupFilenameExtensions, bfNormal));
	dialog->insert(new TButton(TRect(kRightX1, 13, kRightX2, 15), "Swapping / EMS / XMS...",
	                           cmMrSetupSwappingEmsXms, bfNormal));
	dialog->insert(new TButton(TRect(kRightX1, 15, kRightX2, 17), "Backups / Temp / Autosave...",
	                           cmMrSetupBackupsTempAutosave, bfNormal));
	dialog->insert(new TButton(TRect(kLeftX1, 17, kLeftX2, 19), "Search and Replace...",
	                           cmMrSetupSearchAndReplaceDefaults, bfNormal));
	dialog->insert(new TButton(TRect(kRightX1, 17, kRightX2, 19), "User interface settings...",
	                           cmMrSetupUserInterfaceSettings, bfNormal));
	dialog->insert(new TButton(TRect(kLeftX1, 20, kLeftX2, 22), "Exit Setup", cmCancel, bfNormal));
	dialog->insert(new TButton(TRect(kRightX1, 20, kRightX2, 22), "Save configuration",
	                           cmMrSetupSaveConfigurationAndExit, bfDefault));

	return dialog;
}

TDialog *createColorSetupDialog() {
	TDialog *dialog = new TDialog(centeredRect(32, 11), "COLOR SETUP");
	dialog->insert(
	    new TButton(TRect(2, 2, 29, 4), "Window colors", cmMrColorWindowColors, bfNormal));
	dialog->insert(
	    new TButton(TRect(2, 4, 29, 6), "Menu/Dialog colors", cmMrColorMenuDialogColors, bfNormal));
	dialog->insert(new TButton(TRect(2, 6, 29, 8), "Help colors", cmMrColorHelpColors, bfNormal));
	dialog->insert(
	    new TButton(TRect(2, 8, 29, 10), "Other colors", cmMrColorOtherColors, bfNormal));
	return dialog;
}

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
	return createSimplePreviewDialog("EDIT SETTINGS", 76, 20, lines, true);
}

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
	return createSimplePreviewDialog("DISPLAY SETUP", 60, 20, lines, true);
}

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
	return createSimplePreviewDialog("WINDOW COLORS", 34, 20, lines, false);
}

TDialog *createMenuDialogColorsDialog() {
	std::vector<std::string> lines;
	lines.push_back("Menu-Text");
	lines.push_back("menu-Highlight");
	lines.push_back("menu-Bold");
	lines.push_back("menu-skip");
	lines.push_back("Menu-border");
	lines.push_back("bUtton");
	lines.push_back("button-Key");
	lines.push_back("button-shAdow");
	lines.push_back("Select");
	lines.push_back("Not-select");
	lines.push_back("Checkbox bold");
	lines.push_back("");
	lines.push_back("Preview:");
	lines.push_back("Window  Menu  File  Block");
	lines.push_back("(*) Select   ( ) Not-select");
	lines.push_back("Button<KEY>");
	return createSimplePreviewDialog("MENU / DIALOG COLORS", 38, 21, lines, false);
}

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
	return createSimplePreviewDialog("HELP COLORS", 36, 20, lines, false);
}

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
	return createSimplePreviewDialog("OTHER COLORS", 36, 19, lines, false);
}

void runColorSetupDialogFlow() {
	bool running = true;

	while (running) {
		ushort result = execDialog(createColorSetupDialog());
		switch (result) {
			case cmMrColorWindowColors:
				execDialog(createWindowColorsDialog());
				break;

			case cmMrColorMenuDialogColors:
				execDialog(createMenuDialogColorsDialog());
				break;

			case cmMrColorHelpColors:
				execDialog(createHelpColorsDialog());
				break;

			case cmMrColorOtherColors:
				execDialog(createOtherColorsDialog());
				break;

			case cmCancel:
			default:
				running = false;
				break;
		}
	}
}

void runInstallationAndSetupDialogFlow() {
	bool running = true;

	while (running) {
		ushort result = execDialog(createInstallationAndSetupDialog());
		switch (result) {
			case cmMrSetupEditSettings:
				execDialog(createEditSettingsDialog());
				break;

			case cmMrSetupDisplaySetup:
				execDialog(createDisplaySetupDialog());
				break;

			case cmMrSetupColorSetup:
				runColorSetupDialogFlow();
				break;

			case cmMrSetupSearchAndReplaceDefaults:
				messageBox(mfInformation | mfOKButton,
				           "Installation / Search and Replace defaults\n\nDummy implementation for now.");
				break;

			case cmMrSetupSaveConfigurationAndExit:
				messageBox(mfInformation | mfOKButton,
				           "Installation / Save configuration and exit\n\nDummy implementation for now.");
				running = false;
				break;

			case cmCancel:
			default:
				running = false;
				break;
		}
	}
}
