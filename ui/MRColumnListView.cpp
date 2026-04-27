#define Uses_TCollection
#define Uses_TDialog
#define Uses_TListBox
#define Uses_TObject
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TView
#include <tvision/tv.h>

#include "MRColumnListView.hpp"

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

MRColumnListView::MRColumnListView(const TRect &bounds, TScrollBar *scrollBar, TView *relay,
                                   ushort selectionCommand, ushort activationCommandValue) noexcept
    : TListBox(bounds, 1, scrollBar), relayTarget(relay), relayCommand(selectionCommand),
      activationCommand(activationCommandValue) {
}

void MRColumnListView::setRows(const std::vector<Row> &rows, short selection) {
	std::vector<std::size_t> widths;
	TPlainStringCollection *items = new TPlainStringCollection(std::max<short>(1, rows.size()), 8);
	TListBoxRec data;

	rowValues = rows;
	if (items == nullptr)
		return;
	for (const Row &row : rows) {
		if (widths.size() < row.size())
			widths.resize(row.size(), 0);
		for (std::size_t i = 0; i < row.size(); ++i)
			widths[i] = std::max(widths[i], row[i].size());
	}
	for (const Row &row : rows)
		items->insert(dupCString(buildDisplayRow(row, widths)));

	if (selection < 0)
		selection = 0;
	if (!rows.empty() && selection >= static_cast<short>(rows.size()))
		selection = static_cast<short>(rows.size()) - 1;

	data.items = items;
	data.selection = static_cast<ushort>(selection);
	setData(&data);
}

short MRColumnListView::selectedIndex() const {
	TListBoxRec data;

	if (rowValues.empty())
		return -1;
	const_cast<MRColumnListView *>(this)->getData(&data);
	if (data.selection >= rowValues.size())
		return static_cast<short>(rowValues.size() - 1);
	return static_cast<short>(data.selection);
}

void MRColumnListView::focusItemNum(short item) {
	const short oldFocused = focused;

	TListBox::focusItemNum(item);
	if (focused != oldFocused)
		dispatchSelectionChanged();
}

void MRColumnListView::dispatchSelectionChanged() {
	TView *target = relayTarget != nullptr ? relayTarget : owner;

	if (relayCommand == 0)
		return;
	while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
		target = target->owner;
	message(target != nullptr ? target : owner, evBroadcast, relayCommand, this);
}

void MRColumnListView::dispatchActivation() {
	TView *target = relayTarget != nullptr ? relayTarget : owner;

	if (activationCommand == 0)
		return;
	while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
		target = target->owner;
	message(target != nullptr ? target : owner, evCommand, activationCommand, this);
}

void MRColumnListView::selectItem(short item) {
	TListBox::selectItem(item);
	dispatchActivation();
}

std::string MRColumnListView::buildDisplayRow(const Row &row,
                                              const std::vector<std::size_t> &widths) const {
	std::string display;

	for (std::size_t i = 0; i < row.size(); ++i) {
		display += row[i];
		if (i + 1 >= row.size())
			continue;
		const std::size_t width = i < widths.size() ? widths[i] : row[i].size();
		if (row[i].size() < width)
			display.append(width - row[i].size(), ' ');
		display.append("  ");
	}
	return display;
}
