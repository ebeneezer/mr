#define Uses_TButton
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TLabel
#define Uses_TListViewer
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TView
#include <tvision/tv.h>

#include "setup/MRSetupCommon.hpp"
#include "setup/MRSetup.hpp"

#include "../app/MRCommands.hpp"
#include "../app/MRHelpTopics.generated.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/widgets/MRDropList.hpp"
#include "../ui/widgets/MRNumericSlider.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr ushort cmMrColorGroupChoose = 0x7410;
constexpr ushort cmMrColorGroupAccept = 0x7411;
constexpr ushort cmMrColorItemFocused = 0x7412;

constexpr std::array<MRColorSetupGroup, 9> kColorGroups = {MRColorSetupGroup::Window, MRColorSetupGroup::MenuDialog, MRColorSetupGroup::Help, MRColorSetupGroup::Other, MRColorSetupGroup::MiniMap,
                                                         MRColorSetupGroup::FileCompareMiniMap, MRColorSetupGroup::Code, MRColorSetupGroup::FileCompare, MRColorSetupGroup::Debugger};

TFrame *initSetupDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

class TCenteredTextField : public TView {
  public:
	TCenteredTextField(const TRect &bounds, std::string text) : TView(bounds), mText(std::move(text)) {
	}

	void setText(const std::string &text) {
		if (mText == text) return;
		mText = text;
		drawView();
	}

	void draw() override {
		TDrawBuffer buffer;
		const TColorAttr color = TProgram::application != nullptr ? TProgram::application->mapColor(2) : TColorAttr(getColor(1));
		std::string shown = mText;
		int start = 0;

		buffer.moveChar(0, ' ', color, size.x);
		if (shown.size() > static_cast<std::size_t>(size.x)) shown.resize(static_cast<std::size_t>(size.x));
		if (!shown.empty()) {
			start = std::max(0, (size.x - static_cast<int>(shown.size())) / 2);
			buffer.moveStr(static_cast<ushort>(start), shown.c_str(), color, size.x - start);
		}
		writeLine(0, 0, size.x, 1, buffer);
	}

  private:
	std::string mText;
};

class TRgbColorItemList : public TListViewer {
  public:
	TRgbColorItemList(const TRect &bounds, TScrollBar *scrollBar, TView *relay) noexcept : TListViewer(bounds, 1, nullptr, scrollBar), mRelay(relay) {
	}

	void setItems(const MRColorSetupItem *items, MRRgbColorAttribute *colors, std::size_t count, short focusedItem, MRColorOutputMode outputMode) {
		mItems = items;
		mColors = colors;
		mCount = count;
		mOutputMode = outputMode;
		setRange(static_cast<short>(std::min<std::size_t>(count, static_cast<std::size_t>(32767))));
		if (range > 0) focusItemNum(focusedItem);
		else {
			focused = 0;
			topItem = 0;
		}
		drawView();
	}

	void refreshSwatches() {
		drawView();
	}

	void focusItem(short item) override {
		TListViewer::focusItem(item);
		if (mRelay != nullptr) message(mRelay, evBroadcast, cmMrColorItemFocused, reinterpret_cast<void *>(static_cast<std::size_t>(focused)));
	}

	void getText(char *dest, short item, short maxLen) override {
		if (dest == nullptr || maxLen <= 0) return;
		if (item < 0 || static_cast<std::size_t>(item) >= mCount || mItems == nullptr) {
			dest[0] = EOS;
			return;
		}
		std::strncpy(dest, mItems[item].label, static_cast<std::size_t>(maxLen - 1));
		dest[maxLen - 1] = EOS;
	}

	void draw() override {
		TColorAttr normalColor;
		TColorAttr focusedColor;

		if ((state & (sfSelected | sfActive)) == (sfSelected | sfActive)) {
			normalColor = getColor(1);
			focusedColor = getColor(3);
		} else {
			normalColor = getColor(2);
			focusedColor = normalColor;
		}

		bool focusedVisible = false;
		for (short y = 0; y < size.y; ++y) {
			TDrawBuffer buffer;
			const short item = static_cast<short>(topItem + y);
			const bool valid = item >= 0 && static_cast<std::size_t>(item) < mCount && mItems != nullptr && mColors != nullptr;
			const bool isFocused = valid && item == focused && (state & (sfSelected | sfActive)) == (sfSelected | sfActive);
			const TColorAttr descriptionColor = isFocused ? focusedColor : normalColor;

			buffer.moveChar(0, ' ', descriptionColor, size.x);
			if (valid) {
				const TColorAttr sampleColor = projectColorAttribute(mColors[item], mOutputMode);

				buffer.moveStr(1, "Aa ", sampleColor, 3);
				buffer.moveChar(4, ' ', normalColor, 1);
				if (size.x > 5) buffer.moveStr(5, mItems[item].label, descriptionColor, size.x - 5);
				if (isFocused) {
					buffer.putChar(0, '>');
					setCursor(5, y);
					focusedVisible = true;
				}
			}
			writeLine(0, y, size.x, 1, buffer);
		}
		if (!focusedVisible) setCursor(-1, -1);
	}

  private:
	TView *mRelay = nullptr;
	const MRColorSetupItem *mItems = nullptr;
	MRRgbColorAttribute *mColors = nullptr;
	std::size_t mCount = 0;
	MRColorOutputMode mOutputMode = MRColorOutputMode::RgbAutomatic;
};

class TUnifiedColorSetupDialog : public MRScrollableDialog {
  public:
	static const int kDialogWidth = 80;
	static const int kDialogHeight = 22;

	TUnifiedColorSetupDialog() noexcept
	    : TWindowInit(initSetupDialogFrame), MRScrollableDialog(centeredSetupDialogRect(kDialogWidth, kDialogHeight), "COLORS", kDialogWidth, kDialogHeight, initSetupDialogFrame), mDraft(resolveColorSetupDefaults()),
	      mOutputMode(configuredColorOutputMode()) {
		helpCtx = hcDialogColorSetup;
		buildViews();
		initScrollIfNeeded();
		refreshGroupField();
		showCurrentGroupItems();
		refreshSlidersFromCurrentColor();
		selectContent();
	}

	ushort dataSize() override {
		return static_cast<ushort>(sizeof(MRColorSetupSettings));
	}

	void getData(void *rec) override {
		storeSlidersInCurrentColor();
		if (rec != nullptr) *static_cast<MRColorSetupSettings *>(rec) = mDraft;
	}

	void setData(void *rec) override {
		if (rec == nullptr) return;
		mDraft = *static_cast<const MRColorSetupSettings *>(rec);
		mGroupIndex = 0;
		mItemIndices.fill(0);
		refreshGroupField();
		showCurrentGroupItems();
		refreshSlidersFromCurrentColor();
		refreshThemeField();
		if (mItemList != nullptr) mItemList->select();
	}

	void handleEvent(TEvent &event) override {
		const bool sliderChanged = event.what == evBroadcast && event.message.command == cmMRNumericSliderChanged;

		if (mGroupField != nullptr && mGroupField->handleDropListEvent(event)) return;
		if (event.what == evMouseDown && mGroupField != nullptr && mGroupField->mouseInView(event.mouse.where)) {
			mGroupField->select();
			toggleGroupList();
			clearEvent(event);
			return;
		}
		if (event.what == evBroadcast && event.message.command == cmMrColorItemFocused) {
			setCurrentItemIndex(static_cast<short>(event.message.infoLong));
			clearEvent(event);
			return;
		}
		MRScrollableDialog::handleEvent(event);
		if (sliderChanged) {
			storeSlidersInCurrentColor();
			clearEvent(event);
			return;
		}
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
		}
	}

  private:
	std::vector<std::string> groupNames() const {
		std::vector<std::string> names;

		for (MRColorSetupGroup group : kColorGroups)
			names.emplace_back(colorSetupGroupTitle(group));
		return names;
	}

	MRRgbColorAttribute *groupColors(MRColorSetupGroup group, std::size_t &count) {
		switch (group) {
			case MRColorSetupGroup::Window:
				count = mDraft.windowColors.size();
				return mDraft.windowColors.data();
			case MRColorSetupGroup::MenuDialog:
				count = mDraft.menuDialogColors.size();
				return mDraft.menuDialogColors.data();
			case MRColorSetupGroup::Help:
				count = mDraft.helpColors.size();
				return mDraft.helpColors.data();
			case MRColorSetupGroup::Other:
				count = mDraft.otherColors.size();
				return mDraft.otherColors.data();
			case MRColorSetupGroup::MiniMap:
				count = mDraft.miniMapColors.size();
				return mDraft.miniMapColors.data();
			case MRColorSetupGroup::FileCompareMiniMap:
				count = mDraft.fileCompareMiniMapColors.size();
				return mDraft.fileCompareMiniMapColors.data();
			case MRColorSetupGroup::Code:
				count = mDraft.codeColors.size();
				return mDraft.codeColors.data();
			case MRColorSetupGroup::FileCompare:
				count = mDraft.fileCompareColors.size();
				return mDraft.fileCompareColors.data();
			case MRColorSetupGroup::Debugger:
				count = mDraft.debuggerColors.size();
				return mDraft.debuggerColors.data();
		}
		count = 0;
		return nullptr;
	}

	MRColorSetupGroup currentGroup() const {
		return kColorGroups[mGroupIndex];
	}

	MRRgbColorAttribute *currentColor() {
		std::size_t count = 0;
		MRRgbColorAttribute *colors = groupColors(currentGroup(), count);
		const short itemIndex = mItemIndices[mGroupIndex];

		if (colors == nullptr || itemIndex < 0 || static_cast<std::size_t>(itemIndex) >= count) return nullptr;
		return &colors[itemIndex];
	}

	void refreshGroupField() {
		if (mGroupField != nullptr) mGroupField->setValue(colorSetupGroupTitle(currentGroup()));
	}

	void showCurrentGroupItems() {
		std::size_t itemCount = 0;
		std::size_t colorCount = 0;
		const MRColorSetupItem *items = colorSetupGroupItems(currentGroup(), itemCount);
		MRRgbColorAttribute *colors = groupColors(currentGroup(), colorCount);

		if (mItemList == nullptr) return;
		mItemList->setItems(items, colors, std::min(itemCount, colorCount), mItemIndices[mGroupIndex], mOutputMode);
	}

	void setCurrentItemIndex(short index) {
		std::size_t count = 0;
		groupColors(currentGroup(), count);
		if (count == 0) return;
		mItemIndices[mGroupIndex] = static_cast<short>(std::clamp<int>(index, 0, static_cast<int>(count - 1)));
		refreshSlidersFromCurrentColor();
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
		const std::vector<std::string> names = groupNames();

		if (mGroupField == nullptr || !mGroupField->acceptDropListSelection()) return;
		selectedGroup = mGroupField->value();
		for (std::size_t i = 0; i < names.size(); ++i) {
			if (names[i] != selectedGroup) continue;
			mGroupIndex = i;
			refreshGroupField();
			showCurrentGroupItems();
			refreshSlidersFromCurrentColor();
			mItemList->select();
			return;
		}
		refreshGroupField();
	}

	static int colorComponent(std::uint32_t rgb, unsigned shift) {
		return static_cast<int>((rgb >> shift) & 0xFFU);
	}

	static std::uint32_t sliderRgb(MRNumericSlider *red, MRNumericSlider *green, MRNumericSlider *blue) {
		return (static_cast<std::uint32_t>(red->getValue()) << 16U) | (static_cast<std::uint32_t>(green->getValue()) << 8U) | static_cast<std::uint32_t>(blue->getValue());
	}

	void refreshSlidersFromCurrentColor() {
		const MRRgbColorAttribute *color = currentColor();

		if (color == nullptr) return;
		mForegroundRed->setValue(colorComponent(color->foregroundRgb, 16));
		mForegroundGreen->setValue(colorComponent(color->foregroundRgb, 8));
		mForegroundBlue->setValue(colorComponent(color->foregroundRgb, 0));
		mBackgroundRed->setValue(colorComponent(color->backgroundRgb, 16));
		mBackgroundGreen->setValue(colorComponent(color->backgroundRgb, 8));
		mBackgroundBlue->setValue(colorComponent(color->backgroundRgb, 0));
		refreshExactValue();
	}

	void storeSlidersInCurrentColor() {
		MRRgbColorAttribute *color = currentColor();

		if (color == nullptr) return;
		color->foregroundRgb = sliderRgb(mForegroundRed, mForegroundGreen, mForegroundBlue);
		color->backgroundRgb = sliderRgb(mBackgroundRed, mBackgroundGreen, mBackgroundBlue);
		refreshExactValue();
		if (mItemList != nullptr) mItemList->refreshSwatches();
	}

	void refreshExactValue() {
		const MRRgbColorAttribute *color = currentColor();
		std::array<char, 32> text{};

		if (color == nullptr || mExactValueField == nullptr) return;
		std::snprintf(text.data(), text.size(), "%06X/%06X", static_cast<unsigned>(color->foregroundRgb), static_cast<unsigned>(color->backgroundRgb));
		mExactValueField->setText(text.data());
	}

	void refreshThemeField() {
		if (mThemeField != nullptr) mThemeField->setText("active: " + configuredColorThemeDisplayName());
	}

	MRNumericSlider *addSlider(const TRect &rect, const char *label) {
		MRNumericSlider *slider = new MRNumericSlider(rect, 0, 255, 0, 1, 16, MRNumericSlider::fmtRaw, cmMRNumericSliderChanged);
		addManaged(slider, rect);
		const TRect labelRect(rect.a.x - 3, rect.a.y, rect.a.x - 1, rect.b.y);
		addManaged(new TLabel(labelRect, label, slider), labelRect);
		return slider;
	}

	void buildViews() {
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~O~K", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal},
		                         mr::dialogs::DialogButtonSpec{"~L~oad Theme", cmMrColorLoadTheme, bfNormal}, mr::dialogs::DialogButtonSpec{"~S~ave Theme", cmMrColorSaveTheme, bfNormal},
		                         mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 1);
		const int buttonLeft = (kDialogWidth - metrics.rowWidth) / 2;

		mGroupField = new MRStringChoiceField(TRect(3, 1, 78, 2), 72);
		mGroupField->setChoices(groupNames());
		addManaged(mGroupField, TRect(3, 1, 78, 2));
		mGroupField->createDropListButton(*this, TRect(78, 1, 79, 2), this, cmMrColorGroupChoose, false);
		mGroupListAnchor = TRect(3, 2, 79, 3);

		mItemScroll = new TScrollBar(TRect(78, 2, 79, 12));
		addManaged(mItemScroll, TRect(78, 2, 79, 12));
		mItemList = new TRgbColorItemList(TRect(3, 2, 78, 12), mItemScroll, this);
		addManaged(mItemList, TRect(3, 2, 78, 12));

		addManaged(new TStaticText(TRect(3, 12, 38, 13), "Foreground RGB"), TRect(3, 12, 38, 13));
		addManaged(new TStaticText(TRect(42, 12, 77, 13), "Background RGB"), TRect(42, 12, 77, 13));
		mForegroundRed = addSlider(TRect(6, 13, 38, 14), "~R~");
		mForegroundGreen = addSlider(TRect(6, 14, 38, 15), "~G~");
		mForegroundBlue = addSlider(TRect(6, 15, 38, 16), "~B~");
		mBackgroundRed = addSlider(TRect(45, 13, 77, 14), "R");
		mBackgroundGreen = addSlider(TRect(45, 14, 77, 15), "G");
		mBackgroundBlue = addSlider(TRect(45, 15, 77, 16), "B");

		mExactValueField = new TCenteredTextField(TRect(3, 16, 79, 17), "000000/000000");
		addManaged(mExactValueField, TRect(3, 16, 79, 17));
		mr::dialogs::addManagedUniformButtonRow(*this, buttonLeft, 18, 1, buttons);
		mThemeField = new TCenteredTextField(TRect(5, 20, 77, 21), "active: " + configuredColorThemeDisplayName());
		addManaged(mThemeField, TRect(5, 20, 77, 21));
	}

	MRColorSetupSettings mDraft;
	MRColorOutputMode mOutputMode;
	std::array<short, kColorGroups.size()> mItemIndices{};
	std::size_t mGroupIndex = 0;
	MRStringChoiceField *mGroupField = nullptr;
	TScrollBar *mItemScroll = nullptr;
	TRgbColorItemList *mItemList = nullptr;
	MRNumericSlider *mForegroundRed = nullptr;
	MRNumericSlider *mForegroundGreen = nullptr;
	MRNumericSlider *mForegroundBlue = nullptr;
	MRNumericSlider *mBackgroundRed = nullptr;
	MRNumericSlider *mBackgroundGreen = nullptr;
	MRNumericSlider *mBackgroundBlue = nullptr;
	TCenteredTextField *mExactValueField = nullptr;
	TCenteredTextField *mThemeField = nullptr;
	TRect mGroupListAnchor;
};

} // namespace

TDialog *createColorSetupDialog() {
	return new TUnifiedColorSetupDialog();
}
