#define Uses_TButton
#define Uses_TColorDisplay
#define Uses_TColorGroup
#define Uses_TColorGroupList
#define Uses_TColorItem
#define Uses_TColorItemList
#define Uses_TColorSelector
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TLabel
#define Uses_TMonoSelector
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TView
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"

#include "../app/MRCommands.hpp"
#include "../config/MRDialogPaths.hpp"

#include <string>

namespace {

class TThemeNameField : public TView {
  public:
	TThemeNameField(const TRect &bounds, const std::string &text) : TView(bounds), text_(text) {
	}

	void setText(const std::string &text) {
		text_ = text;
		drawView();
	}

	void draw() override {
		TDrawBuffer buffer;
		TColorAttr color = (TProgram::application != nullptr) ? TProgram::application->mapColor(2)
		                                                      : TColorAttr(getColor(1));
		std::string shown = "active: " + text_;
		int start = 0;

		buffer.moveChar(0, ' ', color, size.x);
		if (size.x > 0) {
			if (shown.size() > static_cast<std::size_t>(size.x))
				shown = shown.substr(0, static_cast<std::size_t>(size.x));
			start = (size.x - static_cast<int>(shown.size())) / 2;
			if (start < 0)
				start = 0;
			buffer.moveStr(static_cast<ushort>(start), shown.c_str(), color, size.x - start);
		}
		writeLine(0, 0, size.x, 1, buffer);
	}

  private:
	std::string text_;
};

class TUnifiedColorSetupDialog : public TDialog {
  public:
	TUnifiedColorSetupDialog(const char *title, TColorGroup *groupsHead) noexcept
	    : TWindowInit(&TDialog::initFrame), TDialog(TRect(0, 0, 76, 21), title) {
		for (TColorGroup *group = groupsHead; group != nullptr; group = group->next)
			group->index = 0;
		options |= ofCentered;

		groupScroll_ = new TScrollBar(TRect(18, 3, 19, 14));
		insert(groupScroll_);
		groups_ = new TColorGroupList(TRect(3, 3, 18, 14), groupScroll_, groupsHead);
		insert(groups_);
		insert(new TLabel(TRect(2, 2, 8, 3), "~G~roup", groups_));

		itemScroll_ = new TScrollBar(TRect(57, 3, 58, 14));
		insert(itemScroll_);
		itemList_ = new TColorItemList(TRect(21, 3, 57, 14), itemScroll_, groupsHead->items);
		insert(itemList_);
		insert(new TLabel(TRect(20, 2, 25, 3), "~I~tem", itemList_));

		forSel_ = new TColorSelector(TRect(60, 3, 72, 7), TColorSelector::csForeground);
		insert(forSel_);
		forLabel_ = new TLabel(TRect(60, 2, 72, 3), "~F~oreground", forSel_);
		insert(forLabel_);

		bakSel_ = new TColorSelector(TRect(60, 9, 72, 11), TColorSelector::csBackground);
		insert(bakSel_);
		bakLabel_ = new TLabel(TRect(60, 8, 72, 9), "~B~ackground", bakSel_);
		insert(bakLabel_);

		display_ = new TColorDisplay(TRect(59, 12, 73, 14), "Text ");
		insert(display_);

		monoSel_ = new TMonoSelector(TRect(59, 3, 74, 7));
		monoSel_->hide();
		insert(monoSel_);
		monoLabel_ = new TLabel(TRect(58, 2, 64, 3), "Color", monoSel_);
		monoLabel_->hide();
		insert(monoLabel_);

		insert(new TButton(TRect(6, 16, 19, 18), "~L~oad Theme", cmMrColorLoadTheme, bfNormal));
		insert(new TButton(TRect(21, 16, 34, 18), "~S~ave Theme", cmMrColorSaveTheme, bfNormal));
		insert(new TButton(TRect(41, 16, 51, 18), "~O~K", cmOK, bfDefault));
		insert(new TButton(TRect(53, 16, 67, 18), "~C~ancel", cmCancel, bfNormal));

		themeField_ = new TThemeNameField(TRect(5, 18, 71, 19), configuredColorThemeDisplayName());
		insert(themeField_);

		selectNext(False);
	}

	~TUnifiedColorSetupDialog() {
		delete pal_;
	}

	ushort dataSize() override {
		return pal_ != nullptr ? static_cast<ushort>(*pal_->data + 1) : 0;
	}

	void getData(void *rec) override {
		if (rec != nullptr && pal_ != nullptr)
			*static_cast<TPalette *>(rec) = *pal_;
	}

	void setData(void *rec) override {
		if (rec == nullptr)
			return;
		if (pal_ == nullptr)
			pal_ = new TPalette("", 0);
		*pal_ = *static_cast<TPalette *>(rec);
		display_->setColor(&pal_->data[groups_->getGroupIndex(groupIndex_)]);
		groups_->focusItem(groupIndex_);
		if (showMarkers) {
			forLabel_->hide();
			forSel_->hide();
			bakLabel_->hide();
			bakSel_->hide();
			monoLabel_->show();
			monoSel_->show();
		}
		themeField_->setText(configuredColorThemeDisplayName());
		groups_->select();
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown) {
			ushort keyCode = event.keyDown.keyCode;
			if (keyCode == kbTab || keyCode == kbCtrlI) {
				selectNext(False);
				clearEvent(event);
				return;
			}
			if (keyCode == kbShiftTab) {
				selectNext(True);
				clearEvent(event);
				return;
			}
		}
		if (event.what == evBroadcast && event.message.command == cmNewColorItem)
			groupIndex_ = groups_->focused;
		TDialog::handleEvent(event);
		if (event.what == evCommand &&
		    (event.message.command == cmMrColorLoadTheme || event.message.command == cmMrColorSaveTheme)) {
			endModal(event.message.command);
			clearEvent(event);
			return;
		}
		if (event.what == evBroadcast && event.message.command == cmNewColorIndex && pal_ != nullptr)
			display_->setColor(&pal_->data[event.message.infoByte]);
	}

  private:
	TPalette *pal_ = nullptr;
	TColorDisplay *display_ = nullptr;
	TColorGroupList *groups_ = nullptr;
	TColorItemList *itemList_ = nullptr;
	TScrollBar *groupScroll_ = nullptr;
	TScrollBar *itemScroll_ = nullptr;
	TLabel *forLabel_ = nullptr;
	TColorSelector *forSel_ = nullptr;
	TLabel *bakLabel_ = nullptr;
	TColorSelector *bakSel_ = nullptr;
	TLabel *monoLabel_ = nullptr;
	TMonoSelector *monoSel_ = nullptr;
	TThemeNameField *themeField_ = nullptr;
	uchar groupIndex_ = 0;
};

TColorGroup *buildAllColorGroups() {
	static const MRColorSetupGroup groups[] = {MRColorSetupGroup::Window, MRColorSetupGroup::MenuDialog,
	                                           MRColorSetupGroup::Help, MRColorSetupGroup::Other};
	TColorGroup *head = nullptr;

	for (std::size_t g = sizeof(groups) / sizeof(groups[0]); g-- > 0;) {
		std::size_t count = 0;
		const MRColorSetupItem *items = colorSetupGroupItems(groups[g], count);
		TColorItem *itemHead = nullptr;

		if (items == nullptr || count == 0)
			continue;
		for (std::size_t i = count; i-- > 0;)
			itemHead = new TColorItem(items[i].label, items[i].paletteIndex, itemHead);
		head = new TColorGroup(colorSetupGroupTitle(groups[g]), itemHead, head);
	}
	return head;
}

} // namespace

TDialog *createColorSetupDialog() {
	TColorGroup *groupsHead = buildAllColorGroups();

	if (groupsHead == nullptr)
		return nullptr;
	return new TUnifiedColorSetupDialog("Colors", groupsHead);
}
