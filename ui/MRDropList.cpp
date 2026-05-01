#define Uses_TGroup
#define Uses_TObject
#include <tvision/tv.h>

#include "MRDropList.hpp"

#include "MRColumnListView.hpp"

#include <vector>

MRDropList::~MRDropList() {
	hide();
}

void MRDropList::toggle(TGroup &owner, const TRect &anchor, const std::vector<std::string> &values, const std::string &currentValue, TView *relay, ushort acceptCommand, short maxVisibleRows) {
	if (visible()) {
		hide();
		return;
	}
	show(owner, anchor, values, currentValue, relay, acceptCommand, maxVisibleRows);
}

void MRDropList::show(TGroup &owner, const TRect &anchor, const std::vector<std::string> &values, const std::string &currentValue, TView *relay, ushort acceptCommand, short maxVisibleRows) {
	std::vector<MRColumnListView::Row> rows;
	short selection = 0;
	short visibleRows = static_cast<short>(values.size());
	TRect bounds;

	if (visibleRows <= 0) return;
	if (maxVisibleRows > 0 && visibleRows > maxVisibleRows) visibleRows = maxVisibleRows;
	bounds = TRect(anchor.a.x, anchor.a.y, anchor.b.x, anchor.a.y + static_cast<int>(visibleRows));
	hide();
	rows.reserve(values.size());
	for (const std::string &value : values)
		rows.push_back(MRColumnListView::Row{value});
	for (std::size_t i = 0; i < values.size(); ++i)
		if (!currentValue.empty() && values[i] == currentValue) {
			selection = static_cast<short>(i);
			break;
		}

	listView = new MRColumnListView(bounds, nullptr, relay, 0, acceptCommand, true);
	listOwner = &owner;
	itemValues = values;
	listOwner->insert(listView);
	listView->setRows(rows, selection);
	listView->select();
}

void MRDropList::hide() {
	if (listView == nullptr) return;
	if (listOwner != nullptr) listOwner->remove(listView);
	TObject::destroy(listView);
	listView = nullptr;
	listOwner = nullptr;
	itemValues.clear();
}

bool MRDropList::visible() const noexcept {
	return listView != nullptr;
}

bool MRDropList::containsPoint(TPoint where) const noexcept {
	return listView != nullptr && listView->mouseInView(where);
}

bool MRDropList::acceptSelection(std::string &selectedValue) {
	short selection = -1;

	if (listView == nullptr) return false;
	selection = listView->selectedIndex();
	if (selection < 0 || static_cast<std::size_t>(selection) >= itemValues.size()) {
		hide();
		return false;
	}
	selectedValue = itemValues[static_cast<std::size_t>(selection)];
	hide();
	return true;
}
