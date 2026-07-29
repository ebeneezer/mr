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
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TMonoSelector
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TView
#include <tvision/tv.h>

#include "setup/MRSetupCommon.hpp"
#include "setup/MRSetup.hpp"

#include "../app/MRCommands.hpp"
#include "../app/MRHelpTopics.generated.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/widgets/MRDropList.hpp"

#include <array>
#include <string>

namespace {

constexpr ushort cmMrColorGroupChoose = 0x7410;
constexpr ushort cmMrColorGroupAccept = 0x7411;

TFrame *initSetupDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

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

class TColorGroupCaption : public TView {
  public:
	TColorGroupCaption(const TRect &bounds, const std::string &text) : TView(bounds), mText(text) {
	}

	void setText(const std::string &text) {
		if (mText == text) return;
		mText = text;
		drawView();
	}

	void draw() override {
		TDrawBuffer buffer;
		TColorAttr color = (TProgram::application != nullptr) ? TProgram::application->mapColor(2) : TColorAttr(getColor(1));
		std::string shown = mText;

		buffer.moveChar(0, ' ', color, size.x);
		if (shown.size() > static_cast<std::size_t>(size.x)) shown = shown.substr(0, static_cast<std::size_t>(size.x));
		if (!shown.empty()) buffer.moveStr(0, shown.c_str(), color, std::min<int>(size.x, static_cast<int>(shown.size())));
		writeLine(0, 0, size.x, 1, buffer);
	}

  private:
	std::string mText;
};

class TAnsi16BackgroundSelector : public TColorSelector {
  public:
	TAnsi16BackgroundSelector(const TRect &bounds) noexcept : TColorSelector(bounds, TColorSelector::csBackground) {
	}

	void draw() override {
		TDrawBuffer buffer;

		for (int row = 0; row < size.y; ++row) {
			buffer.moveChar(0, ' ', 0x70, size.x);
			for (int column = 0; column < kColumns; ++column) {
				const int swatch = row * kColumns + column;

				buffer.moveChar(column * kSwatchWidth, '\xDB', static_cast<uchar>(swatch), kSwatchWidth);
				if (swatch == color) {
					buffer.putChar(column * kSwatchWidth + 1, 8);
					if (swatch == 0) buffer.putAttribute(column * kSwatchWidth + 1, 0x70);
				}
			}
			writeLine(0, row, size.x, 1, buffer);
		}
	}

	void handleEvent(TEvent &event) override {
		const uchar oldColor = color;

		TView::handleEvent(event);
		switch (event.what) {
			case evMouseDown:
				do {
					if (mouseInView(event.mouse.where)) {
						const TPoint mouse = makeLocal(event.mouse.where);

						color = static_cast<uchar>(mouse.y * kColumns + mouse.x / kSwatchWidth);
					} else {
						color = oldColor;
					}
					notifyColorChanged();
					drawView();
				} while (mouseEvent(event, evMouseMove));
				clearEvent(event);
				return;

			case evKeyDown:
				switch (ctrlToArrow(event.keyDown.keyCode)) {
					case kbLeft:
						color = color > 0 ? static_cast<uchar>(color - 1) : static_cast<uchar>(kMaxColor);
						break;

					case kbRight:
						color = color < kMaxColor ? static_cast<uchar>(color + 1) : static_cast<uchar>(0);
						break;

					case kbUp:
						color = color >= kColumns ? static_cast<uchar>(color - kColumns) : static_cast<uchar>(color + kColumns);
						break;

					case kbDown:
						color = color < kColumns ? static_cast<uchar>(color + kColumns) : static_cast<uchar>(color - kColumns);
						break;

					default:
						return;
				}
				break;

			case evBroadcast:
				if (event.message.command != cmColorSet) return;
				color = static_cast<uchar>(event.message.infoByte >> 4);
				drawView();
				return;

			default:
				return;
		}
		drawView();
		notifyColorChanged();
		clearEvent(event);
	}

  private:
	static constexpr int kColumns = 4;
	static constexpr int kRows = 4;
	static constexpr int kSwatchWidth = 3;
	static constexpr int kMaxColor = kColumns * kRows - 1;

	void notifyColorChanged() {
		message(owner, evBroadcast, cmColorBackgroundChanged, (void *)(size_t)color);
	}
};

class TRelayColorItemList : public TColorItemList {
  public:
	TRelayColorItemList(const TRect &bounds, TScrollBar *scrollBar, TColorItem *colorItems, TView *relay) noexcept : TColorItemList(bounds, scrollBar, colorItems), mRelay(relay) {
	}

	void focusItem(short item) override {
		TListViewer::focusItem(item);
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

class TUnifiedColorSetupDialog : public MRScrollableDialog {
  public:
	static const int kDialogWidth = 76;
	static const int kDialogHeight = 21;

	TUnifiedColorSetupDialog(const char *title, TColorGroup *groupsHead) noexcept : TWindowInit(initSetupDialogFrame), MRScrollableDialog(centeredSetupDialogRect(kDialogWidth, kDialogHeight), title, kDialogWidth, kDialogHeight, initSetupDialogFrame), mGroupsHead(groupsHead) {
		helpCtx = hcDialogColorSetup;
		for (TColorGroup *group = groupsHead; group != nullptr; group = group->next)
			group->index = 0;
		buildViews(groupsHead);
		initScrollIfNeeded();
		selectContent();
	}

	~TUnifiedColorSetupDialog() {
		delete mPal;
		freeGroups(mGroupsHead);
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
		refreshGroupField();
		showCurrentGroupItems();
		mDisplay->setColor(&mPal->data[currentPaletteIndex()]);
		if (showMarkers) {
			mForLabel->hide();
			mForSel->hide();
			mBakLabel->hide();
			mBakSel->hide();
			mMonoLabel->show();
			mMonoSel->show();
		}
		mThemeField->setText(configuredColorThemeDisplayName());
		mItemList->select();
	}

	void handleEvent(TEvent &event) override {
		if (mGroupField != nullptr && mGroupField->handleDropListEvent(event)) return;
		if (event.what == evMouseDown && mGroupField != nullptr && mGroupField->mouseInView(event.mouse.where)) {
			mGroupField->select();
			toggleGroupList();
			clearEvent(event);
			return;
		}
		if (event.what == evBroadcast && event.message.command == cmSaveColorIndex) rememberCurrentItemIndex(event.message.infoByte);
		if (event.what == evBroadcast && event.message.command == cmNewColorIndex) syncDisplayToPaletteIndex(event.message.infoByte);
		MRScrollableDialog::handleEvent(event);
		if (event.what == evCommand && event.message.command == cmMrColorGroupChoose) {
			toggleGroupList();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrColorGroupAccept) {
			acceptGroupSelection();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && (event.message.command == cmMrColorLoadTheme || event.message.command == cmMrColorSaveTheme)) {
			endModal(event.message.command);
			clearEvent(event);
			return;
		}
	}

  private:
	static void freeItems(TColorItem *item) {
		while (item != nullptr) {
			TColorItem *next = item->next;
			delete item;
			item = next;
		}
	}

	static void freeGroups(TColorGroup *group) {
		while (group != nullptr) {
			TColorGroup *next = group->next;
			freeItems(group->items);
			delete group;
			group = next;
		}
	}

	TColorGroupCaption *addCaption(const TRect &rect, const char *title) {
		TColorGroupCaption *view = new TColorGroupCaption(rect, title);
		addManaged(view, rect);
		return view;
	}

	std::vector<std::string> groupNames() const {
		std::vector<std::string> names;

		for (TColorGroup *group = mGroupsHead; group != nullptr; group = group->next)
			names.push_back(group->name != nullptr ? group->name : "");
		return names;
	}

	TColorGroup *groupAt(uchar index) const {
		TColorGroup *group = mGroupsHead;

		while (group != nullptr && index-- > 0)
			group = group->next;
		return group;
	}

	TColorGroup *currentGroup() const {
		return groupAt(mGroupIndex);
	}

	std::string currentGroupName() const {
		TColorGroup *group = currentGroup();

		return group != nullptr && group->name != nullptr ? group->name : "";
	}

	uchar currentPaletteIndex() const {
		TColorGroup *group = currentGroup();
		TColorItem *item = group != nullptr ? group->items : nullptr;
		uchar index = group != nullptr ? group->index : 0;

		while (item != nullptr && index-- > 0)
			item = item->next;
		if (item == nullptr && group != nullptr) item = group->items;
		return item != nullptr ? item->index : 0;
	}

	void syncDisplayToPaletteIndex(uchar index) {
		if (mPal == nullptr || mDisplay == nullptr) return;
		if (index == 0 || index > static_cast<uchar>(*mPal->data)) return;
		mDisplay->setColor(&mPal->data[index]);
	}

	void syncDisplayToCurrentItem() {
		syncDisplayToPaletteIndex(currentPaletteIndex());
	}

	void refreshGroupField() {
		if (mGroupField != nullptr) mGroupField->setValue(currentGroupName());
	}

	void showCurrentGroupItems() {
		TColorGroup *group = currentGroup();

		if (group != nullptr) message(this, evBroadcast, cmNewColorItem, group);
	}

	void rememberCurrentItemIndex(uchar index) {
		TColorGroup *group = currentGroup();
		uchar itemCount = 0;

		if (group == nullptr) return;
		for (TColorItem *item = group->items; item != nullptr; item = item->next)
			++itemCount;
		if (itemCount == 0) {
			group->index = 0;
			return;
		}
		group->index = index < itemCount ? index : static_cast<uchar>(itemCount - 1);
	}

	void toggleGroupList() {
		if (mGroupField == nullptr) return;
		if (mGroupField->dropListVisible()) {
			mGroupField->hideDropList();
			mGroupField->select();
			return;
		}
		mGroupField->toggleDropList(*this, mGroupListAnchor, this, cmMrColorGroupAccept, 7);
	}

	void acceptGroupSelection() {
		std::string selectedGroup;
		std::vector<std::string> names = groupNames();

		if (mGroupField == nullptr || !mGroupField->acceptDropListSelection()) return;
		selectedGroup = mGroupField->value();
		for (std::size_t i = 0; i < names.size(); ++i) {
			if (names[i] != selectedGroup) continue;
			mGroupIndex = static_cast<uchar>(i);
			refreshGroupField();
			showCurrentGroupItems();
			syncDisplayToCurrentItem();
			mItemList->select();
			return;
		}
		refreshGroupField();
	}

	void buildViews(TColorGroup *groupsHead) {
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~L~oad Theme", cmMrColorLoadTheme, bfNormal}, mr::dialogs::DialogButtonSpec{"~S~ave Theme", cmMrColorSaveTheme, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 2);
		const int buttonLeft = (kDialogWidth - metrics.rowWidth) / 2;

		mGroupField = new MRStringChoiceField(TRect(3, 2, 57, 3), 64);
		mGroupField->setChoices(groupNames());
		mGroupField->setValue(currentGroupName());
		addManaged(mGroupField, TRect(3, 2, 57, 3));
		mGroupField->createDropListButton(*this, TRect(57, 2, 58, 3), this, cmMrColorGroupChoose, false);
		mGroupListAnchor = TRect(3, 3, 58, 4);

		mItemScroll = new TScrollBar(TRect(57, 3, 58, 16));
		addManaged(mItemScroll, TRect(57, 3, 58, 16));

		mItemList = new TRelayColorItemList(TRect(3, 3, 57, 16), mItemScroll, groupsHead->items, this);
		addManaged(mItemList, TRect(3, 3, 57, 16));

		mForSel = new TColorSelector(TRect(60, 3, 72, 7), TColorSelector::csForeground);
		addManaged(mForSel, TRect(60, 3, 72, 7));
		mForLabel = addCaption(TRect(60, 2, 72, 3), "Foreground");

		mBakSel = new TAnsi16BackgroundSelector(TRect(60, 9, 72, 13));
		addManaged(mBakSel, TRect(60, 9, 72, 13));
		mBakLabel = addCaption(TRect(60, 8, 72, 9), "Background");

		mDisplay = new TColorDisplay(TRect(59, 14, 73, 16), "Text ");
		addManaged(mDisplay, TRect(59, 14, 73, 16));

		mMonoSel = new TMonoSelector(TRect(59, 3, 74, 7));
		mMonoSel->hide();
		addManaged(mMonoSel, TRect(59, 3, 74, 7));

		mMonoLabel = addCaption(TRect(58, 2, 64, 3), "Color");
		mMonoLabel->hide();

		mr::dialogs::addManagedUniformButtonRow(*this, buttonLeft, 17, 2, buttons);

		mThemeField = new TThemeNameField(TRect(5, 19, 71, 20), configuredColorThemeDisplayName());
		addManaged(mThemeField, TRect(5, 19, 71, 20));
	}

	TPalette *mPal = nullptr;
	TColorDisplay *mDisplay = nullptr;
	TColorItemList *mItemList = nullptr;
	MRStringChoiceField *mGroupField = nullptr;
	TScrollBar *mItemScroll = nullptr;
	TColorGroupCaption *mForLabel = nullptr;
	TColorSelector *mForSel = nullptr;
	TColorGroupCaption *mBakLabel = nullptr;
	TColorSelector *mBakSel = nullptr;
	TColorGroupCaption *mMonoLabel = nullptr;
	TMonoSelector *mMonoSel = nullptr;
	TThemeNameField *mThemeField = nullptr;
	TColorGroup *mGroupsHead = nullptr;
	TRect mGroupListAnchor;
	uchar mGroupIndex = 0;
};

TColorGroup *buildAllColorGroups() {
	static const MRColorSetupGroup groups[] = {MRColorSetupGroup::Window, MRColorSetupGroup::MenuDialog, MRColorSetupGroup::Help, MRColorSetupGroup::Other, MRColorSetupGroup::MiniMap, MRColorSetupGroup::FileCompareMiniMap, MRColorSetupGroup::Code, MRColorSetupGroup::FileCompare, MRColorSetupGroup::Debugger};
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
	return new TUnifiedColorSetupDialog("COLORS", groupsHead);
}
