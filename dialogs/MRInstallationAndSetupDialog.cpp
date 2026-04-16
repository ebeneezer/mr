#define Uses_TButton
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TProgram
#define Uses_TStaticText
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"
#include "MRSetupDialogCommon.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>

#include "../app/MRCommands.hpp"

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
			// Optimization: manual loop is faster than find_first_not_of
			std::size_t trimPos = 0;
			while (trimPos < physicalId.size() && physicalId[trimPos] == ' ')
				++trimPos;
			if (trimPos > 0)
				physicalId.erase(0, trimPos);
			continue;
		}
		if (line.rfind("core id", 0) == 0) {
			std::size_t pos = line.find(':');
			if (pos != std::string::npos)
				coreId = trimmedValue(line.c_str() + pos + 1, "");
			// Optimization: manual loop is faster than find_first_not_of
			std::size_t trimPos = 0;
			while (trimPos < coreId.size() && coreId[trimPos] == ' ')
				++trimPos;
			if (trimPos > 0)
				coreId.erase(0, trimPos);
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
	std::snprintf(buffer, sizeof(buffer), "%ld logical / %ld cores / %ld package%s",
	              cpu.logicalProcessors, cpu.physicalCores, cpu.physicalPackages,
	              cpu.physicalPackages == 1 ? "" : "s");
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

bool isInstallationSetupModalCommand(ushort command) {
	switch (command) {
		case cmMrSetupEditSettings:
		case cmMrSetupColorSetup:
		case cmMrSetupKeyMapping:
		case cmMrSetupMouseKeyRepeat:
		case cmMrSetupFilenameExtensions:
		case cmMrSetupPaths:
		case cmMrSetupBackupsAutosave:
		case cmMrSetupSearchAndReplaceDefaults:
		case cmMrSetupUserInterfaceSettings:
			return true;
		default:
			return false;
	}
}

struct InstallationSetupLayout {
	static const int kDialogWidth = 84;
	static const int kDialogHeight = 24;
	static const int kLeftX1 = 2;
	static const int kGap = 2;

	int columnWidth = (kDialogWidth - kLeftX1 * 2 - kGap) / 2;
	int leftX2 = kLeftX1 + columnWidth;
	int rightX1 = leftX2 + kGap;
	int rightX2 = rightX1 + columnWidth;
	int finalRowTop = kDialogHeight - 3;
	std::size_t infoWidth = static_cast<std::size_t>(std::max(10, kDialogWidth - 5));
	std::size_t columnInfoWidth = static_cast<std::size_t>(std::max(10, columnWidth - 3));
};

class TInstallationAndSetupDialog : public MRScrollableDialog {
  public:
	TInstallationAndSetupDialog()
	    : TWindowInit(&TDialog::initFrame),
	      MRScrollableDialog(centeredSetupDialogRect(InstallationSetupLayout::kDialogWidth,
	                                                InstallationSetupLayout::kDialogHeight),
	                         "INSTALLATION AND SETUP", InstallationSetupLayout::kDialogWidth,
	                         InstallationSetupLayout::kDialogHeight) {
		buildViews();
		initScrollIfNeeded();
		selectContent();
	}

	void handleEvent(TEvent &event) override {
		MRScrollableDialog::handleEvent(event);
		if (event.what == evCommand && isInstallationSetupModalCommand(event.message.command)) {
			endModal(event.message.command);
			clearEvent(event);
		}
	}

  private:
	TStaticText *addLine(const TRect &rect, const std::string &text) {
		TStaticText *view = new TStaticText(rect, text.c_str());
		addManaged(view, rect);
		return view;
	}

	TButton *addButton(const TRect &rect, const char *title, ushort command, ushort flags) {
		TButton *view = new TButton(rect, title, command, flags);
		addManaged(view, rect);
		return view;
	}

	void buildViews() {
		const InstallationSetupLayout g;
		std::string osLine = buildSetupInfoLine("OS", currentOsDescription(), g.infoWidth);
		std::string shellLine =
		    buildSetupInfoLine("Shell", trimmedValue(std::getenv("SHELL")), g.columnInfoWidth);
		std::string termLine =
		    buildSetupInfoLine("TERM", trimmedValue(std::getenv("TERM")), g.columnInfoWidth);
		std::string mrPathLine = buildSetupInfoLine("MR Path", currentWorkingDirectory(),
		                                          g.columnInfoWidth);
		std::string cpuLine = buildSetupInfoLine("CPU", currentCpuDescription(), g.columnInfoWidth);
		std::string ramLine = buildSetupInfoLine("RAM", currentRamDescription(), g.columnInfoWidth);

		addLine(TRect(2, 2, 2 + static_cast<short>(osLine.size() + 1), 3), osLine);
		addLine(TRect(2, 3, 2 + static_cast<short>(shellLine.size() + 1), 4), shellLine);
		addLine(TRect(g.rightX1, 3, g.rightX1 + static_cast<short>(termLine.size() + 1), 4), termLine);
		addLine(TRect(2, 4, 2 + static_cast<short>(mrPathLine.size() + 1), 5), mrPathLine);
		addLine(TRect(g.rightX1, 4, g.rightX1 + static_cast<short>(cpuLine.size() + 1), 5), cpuLine);
		addLine(TRect(2, 5, 2 + static_cast<short>(ramLine.size() + 1), 6), ramLine);

		addButton(TRect(g.kLeftX1, 9, g.leftX2, 11), "~C~olor setup", cmMrSetupColorSetup,
		          bfNormal);
		addButton(TRect(g.kLeftX1, 11, g.leftX2, 13), "~K~ey mapping", cmMrSetupKeyMapping,
		          bfNormal);
		addButton(TRect(g.kLeftX1, 13, g.leftX2, 15), "~S~earch and Replace",
		          cmMrSetupSearchAndReplaceDefaults, bfNormal);

		addButton(TRect(g.rightX1, 9, g.rightX2, 11), "~M~ouse / Key repeat",
		          cmMrSetupMouseKeyRepeat, bfNormal);
		addButton(TRect(g.rightX1, 11, g.rightX2, 13), "~F~ilename extensions",
		          cmMrSetupFilenameExtensions, bfNormal);
		addButton(TRect(g.rightX1, 13, g.rightX2, 15), "~P~aths", cmMrSetupPaths,
		          bfNormal);
		addButton(TRect(g.rightX1, 15, g.rightX2, 17), "~B~ackups / Autosave",
		          cmMrSetupBackupsAutosave, bfNormal);
		addButton(TRect(g.rightX1, 17, g.rightX2, 19), "~U~ser interface settings",
		          cmMrSetupUserInterfaceSettings, bfNormal);

	}
};

} // namespace

TDialog *createInstallationAndSetupDialog() {
	return new TInstallationAndSetupDialog();
}
