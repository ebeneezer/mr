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

#include "MRSetupCommon.hpp"
#include "MRSetup.hpp"

#include "../app/MRCommands.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../ui/MRFrame.hpp"

#include <array>
#include <string>

namespace {

TFrame *initSetupDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

class TRelayColorGroupList : public TColorGroupList {
  public:
	TRelayColorGroupList(const TRect &bounds, TScrollBar *scrollBar, TColorGroup *groups, TView *relay) noexcept : TColorGroupList(bounds, scrollBar, groups), mRelay(relay) {
	}

	void focusItem(short item) override {
		TColorGroupList::focusItem(item);
		if (mRelay == nullptr) return;
		TColorGroup *curGroup = groups;
		short index = item;
		while (curGroup != nullptr && index-- > 0)
			curGroup = curGroup->next;
		if (curGroup != nullptr) message(mRelay, evBroadcast, cmNewColorItem, curGroup);
	}

  private:
	TView *mRelay = nullptr;
};

class TRelayColorItemList : public TColorItemList {
  public:
	TRelayColorItemList(const TRect &bounds, TScrollBar *scrollBar, TColorItem *items, TView *relay) noexcept : TColorItemList(bounds, scrollBar, items), mRelay(relay) {
	}

	void focusItem(short item) override {
		TColorItemList::focusItem(item);
		if (mRelay == nullptr) return;
		message(mRelay, evBroadcast, cmSaveColorIndex, (void *)(size_t)item);
		TColorItem *curItem = items;
		short index = item;
		while (curItem != nullptr && index-- > 0)
			curItem = curItem->next;
		if (curItem != nullptr) message(mRelay, evBroadcast, cmNewColorIndex, (void *)(size_t)(curItem->index));
	}

  private:
	TView *mRelay = nullptr;
};

class TThemeNameField : public TView {
  public:
	TThemeNameField(const TRect &bounds, const std::string &text) : TView(bounds), mText(text) {
	}

	void setText(const std::string &text) {
		mText = text;
		drawView();
	}

	void draw() override {
		TDrawBuffer buffer;
		TColorAttr color = (TProgram::application != nullptr) ? TProgram::application->mapColor(2) : TColorAttr(getColor(1));
		std::string shown = "active: " + mText;
		int start = 0;

		buffer.moveChar(0, ' ', color, size.x);
		if (size.x > 0) {
			if (shown.size() > static_cast<std::size_t>(size.x)) shown = shown.substr(0, static_cast<std::size_t>(size.x));
			start = (size.x - static_cast<int>(shown.size())) / 2;
			if (start < 0) start = 0;
			buffer.moveStr(static_cast<ushort>(start), shown.c_str(), color, size.x - start);
		}
		writeLine(0, 0, size.x, 1, buffer);
	}

  private:
	std::string mText;
};

class TUnifiedColorSetupDialog : public MRScrollableDialog {
  public:
	static const int kDialogWidth = 76;
	static const int kDialogHeight = 21;

	TUnifiedColorSetupDialog(const char *title, TColorGroup *groupsHead) noexcept : TWindowInit(initSetupDialogFrame), MRScrollableDialog(centeredSetupDialogRect(kDialogWidth, kDialogHeight), title, kDialogWidth, kDialogHeight, initSetupDialogFrame) {
		for (TColorGroup *group = groupsHead; group != nullptr; group = group->next)
			group->index = 0;
		buildViews(groupsHead);
		initScrollIfNeeded();
		selectContent();
	}

	~TUnifiedColorSetupDialog() {
		delete mPal;
	}

	ushort dataSize() override {
		return mPal != nullptr ? static_cast<ushort>(*mPal->data + 1) : 0;
	}

	void getData(void *rec) override {
		if (rec != nullptr && mPal != nullptr) *static_cast<TPalette *>(rec) = *mPal;
	}

	void setData(void *rec) override {
		if (rec == nullptr) return;
		if (mPal == nullptr) mPal = new TPalette("", 0);
		*mPal = *static_cast<TPalette *>(rec);
		mDisplay->setColor(&mPal->data[mGroups->getGroupIndex(mGroupIndex)]);
		mGroups->focusItem(mGroupIndex);
		if (showMarkers) {
			mForLabel->hide();
			mForSel->hide();
			mBakLabel->hide();
			mBakSel->hide();
			mMonoLabel->show();
			mMonoSel->show();
		}
		mThemeField->setText(configuredColorThemeDisplayName());
		mGroups->select();
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evBroadcast && event.message.command == cmNewColorItem) mGroupIndex = mGroups->focused;
		MRScrollableDialog::handleEvent(event);
		if (event.what == evCommand && (event.message.command == cmMrColorLoadTheme || event.message.command == cmMrColorSaveTheme)) {
			endModal(event.message.command);
			clearEvent(event);
			return;
		}
		if (event.what == evBroadcast && event.message.command == cmNewColorIndex && mPal != nullptr) mDisplay->setColor(&mPal->data[event.message.infoByte]);
	}

  private:
	TLabel *addLabel(const TRect &rect, const char *title, TView *link) {
		TLabel *view = new TLabel(rect, title, link);
		addManaged(view, rect);
		return view;
	}

	void buildViews(TColorGroup *groupsHead) {
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~L~oad Theme", cmMrColorLoadTheme, bfNormal}, mr::dialogs::DialogButtonSpec{"~S~ave Theme", cmMrColorSaveTheme, bfNormal}, mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 2);
		const int buttonLeft = (kDialogWidth - metrics.rowWidth) / 2;

		mGroupScroll = new TScrollBar(TRect(18, 3, 19, 14));
		addManaged(mGroupScroll, TRect(18, 3, 19, 14));

		mGroups = new TRelayColorGroupList(TRect(3, 3, 18, 14), mGroupScroll, groupsHead, this);
		addManaged(mGroups, TRect(3, 3, 18, 14));
		addLabel(TRect(2, 2, 8, 3), "~G~roup", mGroups);

		mItemScroll = new TScrollBar(TRect(57, 3, 58, 14));
		addManaged(mItemScroll, TRect(57, 3, 58, 14));

		mItemList = new TRelayColorItemList(TRect(21, 3, 57, 14), mItemScroll, groupsHead->items, this);
		addManaged(mItemList, TRect(21, 3, 57, 14));
		addLabel(TRect(20, 2, 25, 3), "~I~tem", mItemList);

		mForSel = new TColorSelector(TRect(60, 3, 72, 7), TColorSelector::csForeground);
		addManaged(mForSel, TRect(60, 3, 72, 7));
		mForLabel = addLabel(TRect(60, 2, 72, 3), "~F~oreground", mForSel);

		mBakSel = new TColorSelector(TRect(60, 9, 72, 11), TColorSelector::csBackground);
		addManaged(mBakSel, TRect(60, 9, 72, 11));
		mBakLabel = addLabel(TRect(60, 8, 72, 9), "~B~ackground", mBakSel);

		mDisplay = new TColorDisplay(TRect(59, 12, 73, 14), "Text ");
		addManaged(mDisplay, TRect(59, 12, 73, 14));

		mMonoSel = new TMonoSelector(TRect(59, 3, 74, 7));
		mMonoSel->hide();
		addManaged(mMonoSel, TRect(59, 3, 74, 7));

		mMonoLabel = addLabel(TRect(58, 2, 64, 3), "Color", mMonoSel);
		mMonoLabel->hide();

		mr::dialogs::addManagedUniformButtonRow(*this, buttonLeft, 16, 2, buttons);

		mThemeField = new TThemeNameField(TRect(5, 18, 71, 19), configuredColorThemeDisplayName());
		addManaged(mThemeField, TRect(5, 18, 71, 19));
	}

	TPalette *mPal = nullptr;
	TColorDisplay *mDisplay = nullptr;
	TColorGroupList *mGroups = nullptr;
	TColorItemList *mItemList = nullptr;
	TScrollBar *mGroupScroll = nullptr;
	TScrollBar *mItemScroll = nullptr;
	TLabel *mForLabel = nullptr;
	TColorSelector *mForSel = nullptr;
	TLabel *mBakLabel = nullptr;
	TColorSelector *mBakSel = nullptr;
	TLabel *mMonoLabel = nullptr;
	TMonoSelector *mMonoSel = nullptr;
	TThemeNameField *mThemeField = nullptr;
	uchar mGroupIndex = 0;
};

TColorGroup *buildAllColorGroups() {
	static const MRColorSetupGroup groups[] = {MRColorSetupGroup::Window, MRColorSetupGroup::MenuDialog, MRColorSetupGroup::Help, MRColorSetupGroup::Other, MRColorSetupGroup::MiniMap};
	TColorGroup *head = nullptr;

	for (std::size_t g = sizeof(groups) / sizeof(groups[0]); g-- > 0;) {
		std::size_t count = 0;
		const MRColorSetupItem *items = colorSetupGroupItems(groups[g], count);
		TColorItem *itemHead = nullptr;

		if (items == nullptr || count == 0) continue;
		for (std::size_t i = count; i-- > 0;) {
			if (groups[g] == MRColorSetupGroup::MenuDialog && items[i].paletteIndex == 62) continue;
			itemHead = new TColorItem(items[i].label, items[i].paletteIndex, itemHead);
		}
		head = new TColorGroup(colorSetupGroupTitle(groups[g]), itemHead, head);
	}
	return head;
}

} // namespace

TDialog *createColorSetupDialog() {
	TColorGroup *groupsHead = buildAllColorGroups();

	if (groupsHead == nullptr) return nullptr;
	return new TUnifiedColorSetupDialog("Colors", groupsHead);
}
