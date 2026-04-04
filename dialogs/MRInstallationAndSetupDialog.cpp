#define Uses_TDialog
#define Uses_TButton
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TScrollBar
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

bool isInstallationSetupModalCommand(ushort command) {
	switch (command) {
		case cmMrSetupEditSettings:
		case cmMrSetupColorSetup:
		case cmMrSetupKeyMapping:
		case cmMrSetupMouseKeyRepeat:
		case cmMrSetupFilenameExtensions:
		case cmMrSetupSwappingEmsXms:
		case cmMrSetupBackupsTempAutosave:
		case cmMrSetupSearchAndReplaceDefaults:
		case cmMrSetupUserInterfaceSettings:
		case cmMrSetupSaveConfigurationAndExit:
			return true;
		default:
			return false;
	}
}

class TInstallationAndSetupDialog : public TDialog {
  public:
	struct ManagedItem {
		TView *view;
		TRect base;
	};

	TInstallationAndSetupDialog(const TRect &bounds, const char *title, int virtualWidth,
	                            int virtualHeight)
	    : TWindowInit(&TDialog::initFrame), TDialog(bounds, title), virtualWidth_(virtualWidth),
	      virtualHeight_(virtualHeight) {
		contentRect_ = TRect(1, 1, size.x - 1, size.y - 1);
		content_ = createSetupDialogContentGroup(contentRect_);
		if (content_ != nullptr)
			insert(content_);
	}

	void insertManaged(TView *view, const TRect &base) {
		ManagedItem item;
		item.view = view;
		item.base = base;
		managedViews_.push_back(item);
		if (content_ != nullptr) {
			TRect local = base;
			local.move(-contentRect_.a.x, -contentRect_.a.y);
			view->locate(local);
			content_->insert(view);
		} else
			insert(view);
	}

	void initScrollIfNeeded() {
		int virtualContentWidth = std::max(1, virtualWidth_ - 2);
		int virtualContentHeight = std::max(1, virtualHeight_ - 2);
		bool needH = false;
		bool needV = false;

		for (;;) {
			bool prevH = needH;
			bool prevV = needV;
			int viewportWidth = std::max(1, size.x - 2);
			int viewportHeight = std::max(1, size.y - 2);
			needH = virtualContentWidth > viewportWidth;
			needV = virtualContentHeight > viewportHeight;
			if (needH == prevH && needV == prevV)
				break;
		}

		contentRect_ = TRect(1, 1, size.x - 1, size.y - 1);
		if (contentRect_.b.x <= contentRect_.a.x)
			contentRect_.b.x = contentRect_.a.x + 1;
		if (contentRect_.b.y <= contentRect_.a.y)
			contentRect_.b.y = contentRect_.a.y + 1;
		if (content_ != nullptr)
			content_->locate(contentRect_);

		if (needH) {
			TRect hRect(1, size.y - 1, size.x - 1, size.y);
			if (hScrollBar_ == nullptr) {
				hScrollBar_ = new TScrollBar(hRect);
				insert(hScrollBar_);
			} else
				hScrollBar_->locate(hRect);
		}
		if (needV) {
			TRect vRect(size.x - 1, 1, size.x, size.y - 1);
			if (vScrollBar_ == nullptr) {
				vScrollBar_ = new TScrollBar(vRect);
				insert(vScrollBar_);
			} else
				vScrollBar_->locate(vRect);
		}

		if (hScrollBar_ != nullptr) {
			int maxDx = std::max(0, virtualContentWidth - std::max(1, contentRect_.b.x - contentRect_.a.x));
			hScrollBar_->setParams(0, 0, maxDx, std::max(1, (contentRect_.b.x - contentRect_.a.x) / 2), 1);
		}
		if (vScrollBar_ != nullptr) {
			int maxDy = std::max(0, virtualContentHeight - std::max(1, contentRect_.b.y - contentRect_.a.y));
			vScrollBar_->setParams(0, 0, maxDy, std::max(1, (contentRect_.b.y - contentRect_.a.y) / 2), 1);
		}
		applyScroll();
	}

	void applyScroll() {
		int dx = hScrollBar_ != nullptr ? hScrollBar_->value : 0;
		int dy = vScrollBar_ != nullptr ? vScrollBar_->value : 0;

		for (auto & managedView : managedViews_) {
			TRect moved = managedView.base;
			moved.move(-dx, -dy);
			moved.move(-contentRect_.a.x, -contentRect_.a.y);
			managedView.view->locate(moved);
		}
		if (content_ != nullptr)
			content_->drawView();
	}

	void handleEvent(TEvent &event) override {
		TDialog::handleEvent(event);
		if (event.what == evCommand && isInstallationSetupModalCommand(event.message.command)) {
			endModal(event.message.command);
			clearEvent(event);
		}
		if (event.what == evBroadcast && event.message.command == cmScrollBarChanged &&
		    (event.message.infoPtr == hScrollBar_ || event.message.infoPtr == vScrollBar_)) {
			applyScroll();
			clearEvent(event);
		}
	}

  private:
	int virtualWidth_ = 0;
	int virtualHeight_ = 0;
	TRect contentRect_;
	TGroup *content_ = nullptr;
	std::vector<ManagedItem> managedViews_;
	TScrollBar *hScrollBar_ = nullptr;
	TScrollBar *vScrollBar_ = nullptr;
};
} // namespace

TDialog *createInstallationAndSetupDialog() {
	int dialogWidth = 84;
	int dialogHeight = 24;
	int leftX1 = 2;
	int gap = 2;
	int columnWidth = (dialogWidth - leftX1 * 2 - gap) / 2;
	int leftX2 = leftX1 + columnWidth;
	int rightX1 = leftX2 + gap;
	int rightX2 = rightX1 + columnWidth;
	int finalRowTop = dialogHeight - 3;
	std::size_t infoWidth = static_cast<std::size_t>(std::max(10, dialogWidth - 5));
	std::size_t columnInfoWidth = static_cast<std::size_t>(std::max(10, columnWidth - 3));

	TInstallationAndSetupDialog *dialog =
	    new TInstallationAndSetupDialog(centeredSetupDialogRect(dialogWidth, dialogHeight),
	                                    "INSTALLATION AND SETUP", dialogWidth, dialogHeight);
	if (dialog == nullptr)
		return nullptr;

	std::string osLine = buildSetupInfoLine("OS", currentOsDescription(), infoWidth);
	std::string shellLine = buildSetupInfoLine("Shell", trimmedValue(std::getenv("SHELL")), columnInfoWidth);
	std::string termLine = buildSetupInfoLine("TERM", trimmedValue(std::getenv("TERM")), columnInfoWidth);
	std::string mrPathLine = buildSetupInfoLine("MR Path", currentWorkingDirectory(), columnInfoWidth);
	std::string cpuLine = buildSetupInfoLine("CPU", currentCpuDescription(), columnInfoWidth);
	std::string ramLine = buildSetupInfoLine("RAM", currentRamDescription(), columnInfoWidth);

	TRect osRect(2, 2, 2 + static_cast<short>(osLine.size() + 1), 3);
	TRect shellRect(2, 3, 2 + static_cast<short>(shellLine.size() + 1), 4);
	TRect termRect(rightX1, 3, rightX1 + static_cast<short>(termLine.size() + 1), 4);
	TRect mrPathRect(2, 4, 2 + static_cast<short>(mrPathLine.size() + 1), 5);
	TRect cpuRect(rightX1, 4, rightX1 + static_cast<short>(cpuLine.size() + 1), 5);
	TRect ramRect(2, 5, 2 + static_cast<short>(ramLine.size() + 1), 6);

	dialog->insertManaged(new TStaticText(osRect, osLine.c_str()), osRect);
	dialog->insertManaged(new TStaticText(shellRect, shellLine.c_str()), shellRect);
	dialog->insertManaged(new TStaticText(termRect, termLine.c_str()), termRect);
	dialog->insertManaged(new TStaticText(mrPathRect, mrPathLine.c_str()), mrPathRect);
	dialog->insertManaged(new TStaticText(cpuRect, cpuLine.c_str()), cpuRect);
	dialog->insertManaged(new TStaticText(ramRect, ramLine.c_str()), ramRect);

	dialog->insertManaged(
	    new TButton(TRect(leftX1, 9, leftX2, 11), "~E~dit settings...", cmMrSetupEditSettings, bfNormal),
	    TRect(leftX1, 9, leftX2, 11));
	dialog->insertManaged(
	    new TButton(TRect(leftX1, 11, leftX2, 13), "~C~olor setup...", cmMrSetupColorSetup, bfNormal),
	    TRect(leftX1, 11, leftX2, 13));
	dialog->insertManaged(
	    new TButton(TRect(leftX1, 13, leftX2, 15), "~K~ey mapping...", cmMrSetupKeyMapping, bfNormal),
	    TRect(leftX1, 13, leftX2, 15));
	dialog->insertManaged(
	    new TButton(TRect(leftX1, 15, leftX2, 17), "~S~earch and Replace...",
	                cmMrSetupSearchAndReplaceDefaults, bfNormal),
	    TRect(leftX1, 15, leftX2, 17));
	dialog->insertManaged(new TButton(TRect(rightX1, 9, rightX2, 11), "~M~ouse / Key repeat...",
	                                  cmMrSetupMouseKeyRepeat, bfNormal),
	                      TRect(rightX1, 9, rightX2, 11));
	dialog->insertManaged(new TButton(TRect(rightX1, 11, rightX2, 13), "~F~ilename extensions...",
	                                  cmMrSetupFilenameExtensions, bfNormal),
	                      TRect(rightX1, 11, rightX2, 13));
	dialog->insertManaged(new TButton(TRect(rightX1, 13, rightX2, 15), "~P~aths",
	                                  cmMrSetupSwappingEmsXms, bfNormal),
	                      TRect(rightX1, 13, rightX2, 15));
	dialog->insertManaged(new TButton(TRect(rightX1, 15, rightX2, 17), "~B~ackups / Temp / Autosave...",
	                                  cmMrSetupBackupsTempAutosave, bfNormal),
	                      TRect(rightX1, 15, rightX2, 17));
	dialog->insertManaged(new TButton(TRect(rightX1, 17, rightX2, 19), "~U~ser interface settings...",
	                                  cmMrSetupUserInterfaceSettings, bfNormal),
	                      TRect(rightX1, 17, rightX2, 19));
	dialog->insertManaged(
	    new TButton(TRect(leftX1, finalRowTop, leftX2, finalRowTop + 2), "E~x~it Setup", cmCancel, bfNormal),
	    TRect(leftX1, finalRowTop, leftX2, finalRowTop + 2));
	dialog->insertManaged(new TButton(TRect(rightX1, finalRowTop, rightX2, finalRowTop + 2),
	                                  "Sa~V~e configuration", cmMrSetupSaveConfigurationAndExit, bfDefault),
	                      TRect(rightX1, finalRowTop, rightX2, finalRowTop + 2));

	dialog->initScrollIfNeeded();
	return dialog;
}
