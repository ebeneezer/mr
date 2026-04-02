#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TGroup
#define Uses_MsgBox
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TSItem
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"
#include "MRSetupDialogCommon.hpp"

#include "../app/TMREditorApp.hpp"
#include "../services/MRDialogPaths.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {
enum : ushort {
	cmMrSetupDisplayHelp = 3820
};

enum : ushort {
	kLayoutStatusLine = 0x0001,
	kLayoutMenuBar = 0x0002,
	kLayoutFunctionKeyLabels = 0x0004,
	kLayoutLeftBorder = 0x0008,
	kLayoutRightBorder = 0x0010,
	kLayoutBottomBorder = 0x0020
};

struct DisplaySetupDialogRecord {
	ushort layoutMask;

	DisplaySetupDialogRecord() noexcept : layoutMask(0) {
	}
};

class TDialogPaletteGroup : public TGroup {
  public:
	explicit TDialogPaletteGroup(const TRect &bounds) : TGroup(bounds) {
	}

	TPalette &getPalette() const override {
		if (owner != nullptr)
			return owner->getPalette();
		return TGroup::getPalette();
	}
};

void initDisplaySetupDialogRecord(DisplaySetupDialogRecord &record) {
	MRDisplaySetupSettings settings = configuredDisplaySetupSettings();
	record.layoutMask = 0;
	if (settings.showStatusLine)
		record.layoutMask |= kLayoutStatusLine;
	if (settings.showMenuBar)
		record.layoutMask |= kLayoutMenuBar;
	if (settings.showFunctionKeyLabels)
		record.layoutMask |= kLayoutFunctionKeyLabels;
	if (settings.showLeftBorder)
		record.layoutMask |= kLayoutLeftBorder;
	if (settings.showRightBorder)
		record.layoutMask |= kLayoutRightBorder;
	if (settings.showBottomBorder)
		record.layoutMask |= kLayoutBottomBorder;
}

bool recordsEqual(const DisplaySetupDialogRecord &lhs, const DisplaySetupDialogRecord &rhs) {
	return lhs.layoutMask == rhs.layoutMask;
}

MRDisplaySetupSettings displaySettingsFromRecord(const DisplaySetupDialogRecord &record) {
	MRDisplaySetupSettings settings = configuredDisplaySetupSettings();
	settings.showStatusLine = (record.layoutMask & kLayoutStatusLine) != 0;
	settings.showMenuBar = (record.layoutMask & kLayoutMenuBar) != 0;
	settings.showFunctionKeyLabels = (record.layoutMask & kLayoutFunctionKeyLabels) != 0;
	settings.showLeftBorder = (record.layoutMask & kLayoutLeftBorder) != 0;
	settings.showRightBorder = (record.layoutMask & kLayoutRightBorder) != 0;
	settings.showBottomBorder = (record.layoutMask & kLayoutBottomBorder) != 0;
	return settings;
}

bool saveAndReloadDisplaySettings(const DisplaySetupDialogRecord &record, std::string &errorText) {
	MRDisplaySetupSettings displaySettings = displaySettingsFromRecord(record);
	MRSetupPaths paths;
	TMREditorApp *app = dynamic_cast<TMREditorApp *>(TProgram::application);

	if (!setConfiguredDisplaySetupSettings(displaySettings, &errorText))
		return false;

	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	if (!writeSettingsMacroFile(paths, &errorText))
		return false;
	if (app == nullptr) {
		errorText = "Application error: TMREditorApp is unavailable.";
		return false;
	}
	if (!app->reloadSettingsMacroFromPath(paths.settingsMacroUri, &errorText))
		return false;

	errorText.clear();
	return true;
}

class TDisplaySetupDialog : public TDialog {
  public:
	struct ManagedItem {
		TView *view;
		TRect base;
	};

	explicit TDisplaySetupDialog(const DisplaySetupDialogRecord &initialRecord)
	    : TWindowInit(&TDialog::initFrame),
	      TDialog(centeredSetupDialogRect(kVirtualDialogWidth, kVirtualDialogHeight), "DISPLAY SETUP"),
	      initialRecord_(initialRecord), currentRecord_(initialRecord) {
		contentRect_ = TRect(1, 1, size.x - 1, size.y - 1);
		content_ = new TDialogPaletteGroup(contentRect_);
		if (content_ != nullptr)
			insert(content_);

		int doneLeft = kVirtualDialogWidth / 2 - 17;
		int cancelLeft = doneLeft + 12;
		int helpLeft = cancelLeft + 14;
		int buttonTop = kVirtualDialogHeight - 3;

		addManaged(new TStaticText(TRect(2, 2, kVirtualDialogWidth - 2, 3), "Screen layout:"),
		           TRect(2, 2, kVirtualDialogWidth - 2, 3));
		layoutField_ = new TCheckBoxes(
		    TRect(2, 3, kVirtualDialogWidth - 2, 10),
		    new TSItem("~S~tatus/message line",
		               new TSItem("~M~enu bar",
		                          new TSItem("~F~unction key labels",
		                                     new TSItem("~L~eft-hand border",
		                                                new TSItem("~R~ight-hand border",
		                                                           new TSItem("~B~ottom border",
		                                                                      nullptr)))))));
		addManaged(layoutField_, TRect(2, 3, kVirtualDialogWidth - 2, 10));

		addManaged(new TStaticText(TRect(2, 11, kVirtualDialogWidth - 2, 12),
		                           "Video mode is not used in mr."),
		           TRect(2, 11, kVirtualDialogWidth - 2, 12));
		addManaged(new TStaticText(TRect(2, 12, kVirtualDialogWidth - 2, 13),
		                           "If borders are disabled, windows are fixed and maximized."),
		           TRect(2, 12, kVirtualDialogWidth - 2, 13));

		addManaged(new TButton(TRect(doneLeft, buttonTop, doneLeft + 10, buttonTop + 2), "~D~one", cmOK,
		                       bfDefault),
		           TRect(doneLeft, buttonTop, doneLeft + 10, buttonTop + 2));
		addManaged(new TButton(TRect(cancelLeft, buttonTop, cancelLeft + 12, buttonTop + 2), "~C~ancel",
		                       cmCancel, bfNormal),
		           TRect(cancelLeft, buttonTop, cancelLeft + 12, buttonTop + 2));
		addManaged(new TButton(TRect(helpLeft, buttonTop, helpLeft + 8, buttonTop + 2), "~H~elp",
		                       cmMrSetupDisplayHelp, bfNormal),
		           TRect(helpLeft, buttonTop, helpLeft + 8, buttonTop + 2));

		loadFieldsFromRecord(currentRecord_);
		initScrollIfNeeded();
	}

	ushort run(DisplaySetupDialogRecord &outRecord, bool &changed) {
		ushort result = TProgram::deskTop->execView(this);
		saveFieldsToRecord(currentRecord_);
		outRecord = currentRecord_;
		changed = !recordsEqual(initialRecord_, currentRecord_);
		return result;
	}

	void handleEvent(TEvent &event) override {
		TDialog::handleEvent(event);
		if (event.what == evCommand && event.message.command == cmMrSetupDisplayHelp) {
			endModal(event.message.command);
			clearEvent(event);
			return;
		}
		if (event.what == evBroadcast && event.message.command == cmScrollBarChanged &&
		    (event.message.infoPtr == hScrollBar_ || event.message.infoPtr == vScrollBar_)) {
			applyScroll();
			clearEvent(event);
		}
	}

  private:
	static const int kVirtualDialogWidth = 68;
	static const int kVirtualDialogHeight = 17;

	void addManaged(TView *view, const TRect &base) {
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

	void applyScroll() {
		int dx = hScrollBar_ != nullptr ? hScrollBar_->value : 0;
		int dy = vScrollBar_ != nullptr ? vScrollBar_->value : 0;

		for (std::size_t i = 0; i < managedViews_.size(); ++i) {
			TRect moved = managedViews_[i].base;
			moved.move(-dx, -dy);
			moved.move(-contentRect_.a.x, -contentRect_.a.y);
			managedViews_[i].view->locate(moved);
		}
	}

	void initScrollIfNeeded() {
		int virtualContentWidth = std::max(1, kVirtualDialogWidth - 2);
		int virtualContentHeight = std::max(1, kVirtualDialogHeight - 2);
		bool needH = false;
		bool needV = false;

		for (;;) {
			bool prevH = needH;
			bool prevV = needV;
			int viewportWidth = std::max(1, size.x - 2 - (needV ? 1 : 0));
			int viewportHeight = std::max(1, size.y - 2 - (needH ? 1 : 0));
			needH = virtualContentWidth > viewportWidth;
			needV = virtualContentHeight > viewportHeight;
			if (needH == prevH && needV == prevV)
				break;
		}

		contentRect_ = TRect(1, 1, size.x - 1 - (needV ? 1 : 0), size.y - 1 - (needH ? 1 : 0));
		if (contentRect_.b.x <= contentRect_.a.x)
			contentRect_.b.x = contentRect_.a.x + 1;
		if (contentRect_.b.y <= contentRect_.a.y)
			contentRect_.b.y = contentRect_.a.y + 1;
		if (content_ != nullptr)
			content_->locate(contentRect_);

		if (needH) {
			TRect hRect(1, size.y - 2, size.x - 1 - (needV ? 1 : 0), size.y - 1);
			if (hScrollBar_ == nullptr) {
				hScrollBar_ = new TScrollBar(hRect);
				insert(hScrollBar_);
			} else
				hScrollBar_->locate(hRect);
		}
		if (needV) {
			TRect vRect(size.x - 2, 1, size.x - 1, size.y - 1 - (needH ? 1 : 0));
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

	void loadFieldsFromRecord(const DisplaySetupDialogRecord &record) {
		layoutField_->setData((void *)&record.layoutMask);
	}

	void saveFieldsToRecord(DisplaySetupDialogRecord &record) {
		layoutField_->getData((void *)&record.layoutMask);
	}

	DisplaySetupDialogRecord initialRecord_;
	DisplaySetupDialogRecord currentRecord_;
	TRect contentRect_;
	TDialogPaletteGroup *content_ = nullptr;
	std::vector<ManagedItem> managedViews_;
	TScrollBar *hScrollBar_ = nullptr;
	TScrollBar *vScrollBar_ = nullptr;
	TCheckBoxes *layoutField_ = nullptr;
};

void showDisplaySetupHelpDummyDialog() {
	std::vector<std::string> lines;
	TDialog *dialog;
	lines.push_back("DISPLAY SETUP HELP");
	lines.push_back("");
	lines.push_back("Screen layout toggles menu/status/fkey labels and window borders.");
	lines.push_back("Left/right/bottom border off locks windows and keeps them maximized.");
	lines.push_back("Done writes settings.mrmac and reloads silently.");
	dialog = createSetupSimplePreviewDialogForProfile("DISPLAY SETUP HELP", 66, 13, 76, 16, lines, false);
	if (dialog != nullptr) {
		TProgram::deskTop->execView(dialog);
		TObject::destroy(dialog);
	}
}
} // namespace

TDialog *createDisplaySetupDialog() {
	DisplaySetupDialogRecord record;
	initDisplaySetupDialogRecord(record);
	return new TDisplaySetupDialog(record);
}

void runDisplaySetupDialogFlow() {
	bool running = true;
	DisplaySetupDialogRecord workingRecord;

	initDisplaySetupDialogRecord(workingRecord);
	while (running) {
		ushort result;
		bool changed = false;
		DisplaySetupDialogRecord editedRecord = workingRecord;
		std::string errorText;
		TDisplaySetupDialog *dialog = new TDisplaySetupDialog(workingRecord);

		if (dialog == nullptr)
			return;
		result = dialog->run(editedRecord, changed);
		TObject::destroy(dialog);
		workingRecord = editedRecord;

		switch (result) {
			case cmMrSetupDisplayHelp:
				showDisplaySetupHelpDummyDialog();
				break;

			case cmOK:
				if (!saveAndReloadDisplaySettings(workingRecord, errorText)) {
					messageBox(mfError | mfOKButton, "Installation / Display setup\n\n%s", errorText.c_str());
					break;
				}
				running = false;
				break;

			case cmCancel:
				if (changed) {
					if (messageBox(mfConfirmation | mfYesButton | mfNoButton,
					               "Discard changed display settings?") != cmYes)
						break;
				}
				running = false;
				break;

			default:
				running = false;
				break;
		}
	}
}
