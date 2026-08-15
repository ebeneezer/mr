#define Uses_TCollection
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TListBox
#define Uses_TObject
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TView
#include <tvision/tv.h>

#include "MRColumnListView.hpp"

#include "../../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>
#include <cstring>

namespace {

char *dupCString(const std::string &value) {
	char *copy = new char[value.size() + 1];
	std::memcpy(copy, value.c_str(), value.size() + 1);
	return copy;
}

class TPlainStringCollection : public TCollection {
  public:
	TPlainStringCollection(short aLimit, short aDelta) noexcept : TCollection(aLimit, aDelta) {
	}

  protected:
	void freeItem(void *item) override {
		delete[] static_cast<char *>(item);
	}

  private:
	void *readItem(ipstream &) override {
		return nullptr;
	}

	void writeItem(void *, opstream &) override {
	}
};

} // namespace

MRColumnListView::MRColumnListView(const TRect &bounds, TScrollBar *scrollBar, TView *relay, ushort selectionCommand, ushort activationCommandValue, bool dropListColors) noexcept : TListBox(bounds, 1, scrollBar), relayTarget(relay), relayCommand(selectionCommand), activationCommand(activationCommandValue), useDropListColors(dropListColors) {
}

MRColumnListView::MRColumnListView(const TRect &bounds, TScrollBar *verticalScrollBar, TScrollBar *horizontalScrollBar, TView *relay, ushort selectionCommand, ushort activationCommandValue, bool dropListColors) noexcept : TListBox(bounds, 1, verticalScrollBar), optionalHorizontalScrollBar(horizontalScrollBar), relayTarget(relay), relayCommand(selectionCommand), activationCommand(activationCommandValue), useDropListColors(dropListColors) {
	if (optionalHorizontalScrollBar != nullptr) optionalHorizontalScrollBar->hide();
}

void MRColumnListView::setRows(const std::vector<Row> &rows, short selection) {
	std::vector<std::size_t> widths;
	std::vector<std::string> displayRows;
	TPlainStringCollection *items = new TPlainStringCollection(std::max<short>(1, rows.size()), 8);
	TListBoxRec data;

	rowValues = rows;
	if (items == nullptr) return;
	for (const Row &row : rows) {
		if (widths.size() < row.size()) widths.resize(row.size(), 0);
		for (std::size_t i = 0; i < row.size(); ++i)
			widths[i] = std::max(widths[i], row[i].size());
	}
	displayRows.reserve(rows.size());
	maxDisplayRowWidth = 0;
	for (const Row &row : rows) {
		std::string displayRow = buildDisplayRow(row, widths);
		maxDisplayRowWidth = std::max(maxDisplayRowWidth, displayRow.size());
		displayRows.push_back(std::move(displayRow));
	}
	displayRowValues = displayRows;
	rowStyles.clear();
	for (const std::string &displayRow : displayRows)
		items->insert(dupCString(displayRow));

	if (selection < 0) selection = 0;
	if (!rows.empty() && selection >= static_cast<short>(rows.size())) selection = static_cast<short>(rows.size()) - 1;

	data.items = items;
	data.selection = static_cast<ushort>(selection);
	configureHorizontalScrollBar(maxDisplayRowWidth);
	setData(&data);
}

void MRColumnListView::setRowStyles(const std::vector<RowStyle> &styles) {
	rowStyles = styles;
	if (rowStyles.size() != rowValues.size()) rowStyles.clear();
	drawView();
}

short MRColumnListView::selectedIndex() const {
	TListBoxRec data;

	if (rowValues.empty()) return -1;
	const_cast<MRColumnListView *>(this)->getData(&data);
	if (data.selection >= rowValues.size()) return static_cast<short>(rowValues.size() - 1);
	return static_cast<short>(data.selection);
}

void MRColumnListView::setActivateOnSingleClick(bool enabled) noexcept {
	activateOnSingleClick = enabled;
}

void MRColumnListView::setContextMenuColors(bool enabled) noexcept {
	useContextMenuColors = enabled;
	if (enabled) useDropListColors = false;
}

bool MRColumnListView::handleWheel(TEvent &event) {
	int delta = 0;
	short next = 0;

	if (event.what != evMouseWheel || !containsMouse(event) || range <= 0) return false;
	if (event.mouse.wheel == mwUp || event.mouse.wheel == mwLeft) delta = -1;
	else if (event.mouse.wheel == mwDown || event.mouse.wheel == mwRight)
		delta = 1;
	else
		return false;

	next = static_cast<short>(std::clamp<int>(focused + delta, 0, range - 1));
	focusItemNum(next);
	clearEvent(event);
	return true;
}

TColorAttr MRColumnListView::mapColor(uchar index) {
	TColorAttr configured;

	if (useContextMenuColors) {
		if ((index == 1 || index == 2 || index == 5) && configuredColorSlotOverride(kMrPaletteContextMenu, configured)) return configured;
		if ((index == 3 || index == 4) && configuredColorSlotOverride(kMrPaletteContextMenuSelector, configured)) return configured;
		return TListBox::mapColor(index);
	}
	if (!useDropListColors) return TListBox::mapColor(index);
	if ((index == 1 || index == 2) && configuredColorSlotOverride(kMrPaletteDropListDescription, configured)) return configured;
	if (index == 3 && configuredColorSlotOverride(58, configured)) return configured;
	if (index == 4 && configuredColorSlotOverride(kMrPaletteDropListSelectedInactive, configured)) return configured;
	return TListBox::mapColor(index);
}

void MRColumnListView::changeBounds(const TRect &bounds) {
	TListBox::changeBounds(bounds);
	configureHorizontalScrollBar(maxDisplayRowWidth);
}

void MRColumnListView::handleEvent(TEvent &event) {
	bool acceptAfterMouseDown = false;

	if (activateOnSingleClick && event.what == evMouseDown && (event.mouse.eventFlags & meDoubleClick) == 0 && mouseInView(event.mouse.where)) {
		TPoint mouse = makeLocal(event.mouse.where);
		const short clickedItem = static_cast<short>(topItem + mouse.y);

		acceptAfterMouseDown = mouse.y >= 0 && mouse.y < size.y && clickedItem >= 0 && clickedItem < range;
	}
	TListBox::handleEvent(event);
	if (acceptAfterMouseDown) dispatchActivation();
}

TColorAttr MRColumnListView::colorForRow(short row) {
	TColorAttr configured;
	RowStyle style = RowStyle::Normal;

	if (row >= 0 && static_cast<std::size_t>(row) < rowStyles.size()) style = rowStyles[static_cast<std::size_t>(row)];
	switch (style) {
		case RowStyle::OutlineHeader:
			if (configuredColorSlotOverride(kMrPaletteOutlineFileHeader, configured)) return configured;
			return TColorAttr(0x1F);
		case RowStyle::OutlineLevel0:
			if (configuredColorSlotOverride(kMrPaletteOutlineLevel0, configured)) return configured;
			return TColorAttr(0x1F);
		case RowStyle::OutlineLevel1:
			if (configuredColorSlotOverride(kMrPaletteOutlineLevel1, configured)) return configured;
			return TColorAttr(0x1E);
		case RowStyle::OutlineLevel2:
			if (configuredColorSlotOverride(kMrPaletteOutlineLevel2, configured)) return configured;
			return TColorAttr(0x1B);
		case RowStyle::OutlineLevel3:
			if (configuredColorSlotOverride(kMrPaletteOutlineLevel3, configured)) return configured;
			return TColorAttr(0x1A);
		case RowStyle::OutlineLevel4:
			if (configuredColorSlotOverride(kMrPaletteOutlineLevel4, configured)) return configured;
			return TColorAttr(0x1D);
		case RowStyle::OutlineLevel5:
			if (configuredColorSlotOverride(kMrPaletteOutlineLevel5, configured)) return configured;
			return TColorAttr(0x19);
		case RowStyle::OutlineLevel6:
			if (configuredColorSlotOverride(kMrPaletteOutlineLevel6, configured)) return configured;
			return TColorAttr(0x1C);
		case RowStyle::OutlineLevel7:
			if (configuredColorSlotOverride(kMrPaletteOutlineLevel7, configured)) return configured;
			return TColorAttr(0x13);
		case RowStyle::OutlineLevel8:
			if (configuredColorSlotOverride(kMrPaletteOutlineLevel8, configured)) return configured;
			return TColorAttr(0x1F);
		case RowStyle::OutlineLevel9:
			if (configuredColorSlotOverride(kMrPaletteOutlineLevel9, configured)) return configured;
			return TColorAttr(0x1E);
		case RowStyle::Normal:
		default:
			return mapColor(1);
	}
}

void MRColumnListView::draw() {
	if (rowStyles.empty()) {
		TListBox::draw();
		return;
	}

	for (short y = 0; y < size.y; ++y) {
		TDrawBuffer buffer;
		const short row = static_cast<short>(topItem + y);
		const bool selected = row == focused;
		const TColorAttr color = selected ? mapColor(3) : colorForRow(row);
		std::string text;

		buffer.moveChar(0, ' ', color, size.x);
		if (row >= 0 && static_cast<std::size_t>(row) < displayRowValues.size()) text = displayRowValues[static_cast<std::size_t>(row)];
		if (hScrollBar != nullptr && hScrollBar->value > 0) {
			const std::size_t offset = static_cast<std::size_t>(std::min<int>(hScrollBar->value, static_cast<int>(text.size())));
			text.erase(0, offset);
		}
		buffer.moveStr(0, text.c_str(), color, size.x);
		writeLine(0, y, size.x, 1, buffer);
	}
}

void MRColumnListView::focusItemNum(short item) {
	const short oldFocused = focused;
	const short oldTopItem = topItem;

	if (vScrollBar != nullptr) {
		TListBox::focusItemNum(item);
		if (focused != oldFocused) dispatchSelectionChanged();
		return;
	}
	if (item < 0)
		item = 0;
	else if (item >= range && range > 0)
		item = range - 1;
	if (range == 0) return;

	focused = item;
	if (size.y > 0) {
		if (item < topItem) {
			if (numCols == 1)
				topItem = item;
			else
				topItem = item - item % size.y;
		} else if (item >= topItem + size.y * numCols) {
			if (numCols == 1)
				topItem = item - size.y + 1;
			else
				topItem = item - item % size.y - (size.y * (numCols - 1));
		}
	}
	if (focused != oldFocused || topItem != oldTopItem) drawView();
	if (focused != oldFocused) dispatchSelectionChanged();
}

void MRColumnListView::configureHorizontalScrollBar(std::size_t displayWidth) {
	static constexpr int kMaxListViewerIndent = 254;
	const int visibleTextWidth = std::max<int>(1, size.x - 2);
	int maxValue = 0;

	if (optionalHorizontalScrollBar == nullptr) return;
	if (displayWidth > static_cast<std::size_t>(visibleTextWidth)) {
		const std::size_t overflow = displayWidth - static_cast<std::size_t>(visibleTextWidth);
		maxValue = static_cast<int>(std::min<std::size_t>(overflow, kMaxListViewerIndent));
	}
	if (maxValue <= 0) {
		hScrollBar = nullptr;
		optionalHorizontalScrollBar->setParams(0, 0, 0, visibleTextWidth, 1);
		optionalHorizontalScrollBar->hide();
		return;
	}

	hScrollBar = optionalHorizontalScrollBar;
	optionalHorizontalScrollBar->setParams(std::clamp(optionalHorizontalScrollBar->value, 0, maxValue), 0, maxValue, visibleTextWidth, 1);
	if (getState(sfVisible) && getState(sfActive)) optionalHorizontalScrollBar->show();
	else
		optionalHorizontalScrollBar->hide();
}

void MRColumnListView::dispatchSelectionChanged() {
	TView *target = relayTarget != nullptr ? relayTarget : owner;

	if (relayCommand == 0) return;
	while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
		target = target->owner;
	message(target != nullptr ? target : owner, evBroadcast, relayCommand, this);
}

void MRColumnListView::dispatchActivation() {
	TView *target = relayTarget != nullptr ? relayTarget : owner;
	TEvent event{};

	if (activationCommand == 0) return;
	while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
		target = target->owner;
	target = target != nullptr ? target : owner;
	if (target == nullptr) return;

	event.what = evCommand;
	event.message.command = activationCommand;
	target->putEvent(event);
}

void MRColumnListView::selectItem(short item) {
	TListBox::selectItem(item);
	dispatchActivation();
}

std::string MRColumnListView::buildDisplayRow(const Row &row, const std::vector<std::size_t> &widths) const {
	std::string display;

	for (std::size_t i = 0; i < row.size(); ++i) {
		display += row[i];
		if (i + 1 >= row.size()) continue;
		const std::size_t width = i < widths.size() ? widths[i] : row[i].size();
		if (row[i].size() < width) display.append(width - row[i].size(), ' ');
		display.append("  ");
	}
	return display;
}
