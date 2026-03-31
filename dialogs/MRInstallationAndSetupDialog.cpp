#define Uses_TDialog
#define Uses_TButton
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"
#include "MRSetupDialogCommon.hpp"

#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <sys/utsname.h>
#include <unistd.h>

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

	TDialog *dialog =
	    new TDialog(centeredSetupDialogRect(kDialogWidth, kDialogHeight), "INSTALLATION AND SETUP");

	insertSetupStaticLine(dialog, 2, 2, buildSetupInfoLine("OS", currentOsDescription(), kInfoWidth).c_str());
	insertSetupStaticLine(
	    dialog, 2, 3, buildSetupInfoLine("Shell", trimmedValue(std::getenv("SHELL")), kColumnWidth).c_str());
	insertSetupStaticLine(
	    dialog, 37, 3, buildSetupInfoLine("TERM", trimmedValue(std::getenv("TERM")), kColumnWidth).c_str());
	insertSetupStaticLine(dialog, 2, 4,
	                     buildSetupInfoLine("MR Path", currentWorkingDirectory(), kColumnWidth).c_str());
	insertSetupStaticLine(dialog, 37, 4, buildSetupInfoLine("CPU", currentCpuDescription(), kColumnWidth).c_str());
	insertSetupStaticLine(dialog, 2, 5, buildSetupInfoLine("RAM", currentRamDescription(), kColumnWidth).c_str());

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
