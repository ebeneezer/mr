#define Uses_TGroup
#define Uses_TDialog
#define Uses_TEvent
#define Uses_THistory
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TObject
#define Uses_TRect
#define Uses_TView
#include <tvision/tv.h>

#include "MRDropList.hpp"

#include "MRColumnListView.hpp"

#include <algorithm>
#include <vector>

namespace {

class TDropListButton final : public THistory {
  public:
	TDropListButton(const TRect &bounds, TInputLine *link, TView *relay, ushort command, bool triggerDownKey) noexcept
	    : THistory(bounds, link, 0), relay(relay), command(command), triggerDownKey(triggerDownKey) {
		options |= ofSelectable | ofFirstClick;
		eventMask |= evMouseDown | evKeyDown;
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evMouseDown || (triggerDownKey && event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbDown && (link->state & sfFocused) != 0)) {
			if (!link->focus()) {
				clearEvent(event);
				return;
			}
			dispatchCommand();
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown) {
			TKey key(event.keyDown);

			if (key == TKey(kbEnter) || key == TKey(' ')) {
				dispatchCommand();
				clearEvent(event);
				return;
			}
		}
		TView::handleEvent(event);
	}

  private:
	void dispatchCommand() {
		TView *target = relay != nullptr ? relay : owner;

		while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
			target = target->owner;
		message(target != nullptr ? target : relay, evCommand, command, this);
	}

	TView *relay = nullptr;
	ushort command = 0;
	bool triggerDownKey = false;
};

} // namespace

MRDropList::~MRDropList() {
	hide();
}

TView *MRDropList::createButton(TGroup &owner, const TRect &bounds, TInputLine *link, TView *relay, ushort command, bool triggerDownKey) {
	if (buttonView != nullptr) return buttonView;
	buttonView = new TDropListButton(bounds, link, relay, command, triggerDownKey);
	owner.insert(buttonView);
	return buttonView;
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

bool MRDropList::buttonContainsPoint(TPoint where) const noexcept {
	return buttonView != nullptr && buttonView->mouseInView(where);
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
