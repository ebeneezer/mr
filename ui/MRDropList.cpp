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
#include "../dialogs/setup/MRSetupCommon.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

char foldAscii(char ch) {
	if (ch >= 'A' && ch <= 'Z') return static_cast<char>(ch - 'A' + 'a');
	return ch;
}

bool containsFoldedAscii(const std::string &value, const std::string &fragment) {
	if (fragment.empty() || value.size() < fragment.size()) return false;
	for (std::size_t offset = 0; offset + fragment.size() <= value.size(); ++offset) {
		bool matches = true;

		for (std::size_t i = 0; i < fragment.size(); ++i)
			if (foldAscii(value[offset + i]) != foldAscii(fragment[i])) {
				matches = false;
				break;
			}
		if (matches) return true;
	}
	return false;
}

short firstMatchingValueIndex(const std::vector<std::string> &values, const std::string &fragment) {
	for (std::size_t i = 0; i < values.size(); ++i)
		if (containsFoldedAscii(values[i], fragment)) return static_cast<short>(i);
	return -1;
}

std::vector<std::string> matchingValues(const std::vector<std::string> &values, const std::string &fragment) {
	std::vector<std::string> matches;

	if (fragment.empty()) return values;
	for (const std::string &value : values)
		if (containsFoldedAscii(value, fragment)) matches.push_back(value);
	return matches;
}

std::string printableKeyText(const TEvent &event) {
	TStringView text;
	std::string result;

	if (event.what != evKeyDown) return result;
	text = event.keyDown.getText();
	if (text.empty()) return result;
	result.reserve(text.size());
	for (char ch : text) {
		const unsigned char byte = static_cast<unsigned char>(ch);

		if (byte < 32 || byte == 127) return std::string();
		result.push_back(ch);
	}
	return result;
}

int inputLineEndFirstPos(const TInputLine &inputLine) {
	const int rightEdge = inputLine.curPos - inputLine.size.x + 2;

	return rightEdge > 0 ? rightEdge : 0;
}

void restoreInputLineCursor(TInputLine &inputLine, int cursor) {
	const int textLength = inputLine.data != nullptr ? static_cast<int>(std::strlen(inputLine.data)) : 0;

	inputLine.curPos = std::clamp(cursor, 0, textLength);
	inputLine.selStart = inputLine.curPos;
	inputLine.selEnd = inputLine.curPos;
	inputLine.firstPos = inputLineEndFirstPos(inputLine);
	inputLine.drawView();
	inputLine.setCursor(inputLine.curPos - inputLine.firstPos + 1, 0);
	inputLine.showCursor();
}

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
	linkedInput = link;
	buttonView = new TDropListButton(bounds, link, relay, command, triggerDownKey);
	if (MRScrollableDialog *scrollable = dynamic_cast<MRScrollableDialog *>(&owner))
		scrollable->addManaged(buttonView, bounds);
	else
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
	linkedInputCursor = -1;
	showValues(owner, anchor, values, values, currentValue, relay, acceptCommand, maxVisibleRows);
}

void MRDropList::showValues(TGroup &owner, const TRect &anchor, const std::vector<std::string> &sourceValueList, const std::vector<std::string> &visibleValues, const std::string &currentValue, TView *relay, ushort acceptCommand, short maxVisibleRows) {
	std::vector<MRColumnListView::Row> rows;
	short selection = 0;
	short visibleRows = static_cast<short>(visibleValues.size());
	TRect bounds;

	if (visibleRows <= 0) return;
	if (maxVisibleRows > 0 && visibleRows > maxVisibleRows) visibleRows = maxVisibleRows;
	bounds = TRect(anchor.a.x, anchor.a.y, anchor.b.x, anchor.a.y + static_cast<int>(visibleRows));
	hide();
	rows.reserve(visibleValues.size());
	for (const std::string &value : visibleValues)
		rows.push_back(MRColumnListView::Row{value});
	for (std::size_t i = 0; i < visibleValues.size(); ++i)
		if (!currentValue.empty() && visibleValues[i] == currentValue) {
			selection = static_cast<short>(i);
			break;
		}
	if (!currentValue.empty() && (visibleValues.empty() || visibleValues[static_cast<std::size_t>(selection)] != currentValue)) {
		const short prefixSelection = firstMatchingValueIndex(visibleValues, currentValue);

		if (prefixSelection >= 0) selection = prefixSelection;
	}

	listView = new MRColumnListView(bounds, nullptr, relay, 0, acceptCommand, true);
	sourceValues = sourceValueList;
	itemValues = visibleValues;
	speedSearchPrefix.clear();
	activeOwner = &owner;
	activeRelay = relay;
	activeAnchor = anchor;
	activeAcceptCommand = acceptCommand;
	activeMaxVisibleRows = maxVisibleRows;
	if (MRScrollableDialog *scrollable = dynamic_cast<MRScrollableDialog *>(&owner)) {
		scrollOwner = scrollable;
		scrollOwner->addManaged(listView, bounds);
	} else {
		listOwner = &owner;
		listOwner->insert(listView);
	}
	listView->setRows(rows, selection);
	listView->select();
}

void MRDropList::hide() {
	if (listView == nullptr) return;
	if (scrollOwner != nullptr) {
		if (scrollOwner->current == listView) scrollOwner->setCurrent(nullptr, TView::leaveSelect);
		scrollOwner->removeManaged(listView);
		scrollOwner = nullptr;
	} else if (listOwner != nullptr) {
		if (listOwner->current == listView) listOwner->setCurrent(nullptr, TView::leaveSelect);
		listOwner->remove(listView);
	}
	TObject::destroy(listView);
	listView = nullptr;
	listOwner = nullptr;
	itemValues.clear();
	speedSearchPrefix.clear();
	activeOwner = nullptr;
	activeRelay = nullptr;
	activeAcceptCommand = 0;
	activeMaxVisibleRows = 0;
	if (linkedInput == nullptr) linkedInputCursor = -1;
}

bool MRDropList::handleLinkedInputEvent(TEvent &event, TGroup &owner, const TRect &anchor, const std::vector<std::string> &values, TInputLine *link, TView *relay, ushort acceptCommand, short maxVisibleRows) {
	std::string inputValue;

	if (handleOpenListEvent(event)) return true;
	if (!visible() || link == nullptr || (link->state & sfFocused) == 0 || event.what != evKeyDown) return false;
	linkedInput = link;
	activeOwner = &owner;
	activeRelay = relay;
	activeAnchor = anchor;
	activeAcceptCommand = acceptCommand;
	activeMaxVisibleRows = maxVisibleRows;
	sourceValues = values;
	if (!updateLinkedInputFromKey(event, link, inputValue)) return false;
	static_cast<void>(showFilteredValues(inputValue, true));
	restoreLinkedInputFocus();
	event.what = evNothing;
	return true;
}

bool MRDropList::handleOpenListEvent(TEvent &event, bool hideOnOutsideMouseDown) {
	std::string keyText;

	if (!visible()) return false;
	if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEsc) {
		hide();
		event.what = evNothing;
		return true;
	}
	if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter) {
		dispatchAcceptCommand();
		event.what = evNothing;
		return true;
	}
	if (linkedInput != nullptr && event.what == evKeyDown) {
		std::string inputValue;

		if (updateLinkedInputFromKey(event, linkedInput, inputValue)) {
			static_cast<void>(showFilteredValues(inputValue, true));
			restoreLinkedInputFocus();
			event.what = evNothing;
			return true;
		}
	}
	if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbBack && !speedSearchPrefix.empty()) {
		speedSearchPrefix.resize(speedSearchPrefix.size() - 1);
		if (!speedSearchPrefix.empty()) {
			const short selection = firstMatchingIndex(speedSearchPrefix);

			if (selection >= 0 && listView != nullptr) listView->focusItemNum(selection);
		}
		event.what = evNothing;
		return true;
	}
	keyText = printableKeyText(event);
	if (!keyText.empty() && handleSpeedSearchText(keyText)) {
		event.what = evNothing;
		return true;
	}
	if (event.what == evMouseWheel && listView != nullptr && listView->handleWheel(event)) return true;
	if (hideOnOutsideMouseDown && event.what == evMouseDown && !containsPoint(event.mouse.where) && !buttonContainsPoint(event.mouse.where)) {
		hide();
		event.what = evNothing;
		return true;
	}
	return false;
}

bool MRDropList::updateLinkedInputFromKey(TEvent &event, TInputLine *link, std::string &inputValue) {
	std::string keyText = printableKeyText(event);
	std::string currentValue;
	std::string nextValue;
	int start = 0;
	int end = 0;

	if (link == nullptr || link->data == nullptr || event.what != evKeyDown) return false;
	currentValue = link->data;
	if (visible() && linkedInputCursor >= 0) {
		start = std::clamp(linkedInputCursor, 0, static_cast<int>(currentValue.size()));
		end = start;
	} else {
		start = std::clamp(std::min(link->selStart, link->selEnd), 0, static_cast<int>(currentValue.size()));
		end = std::clamp(std::max(link->selStart, link->selEnd), 0, static_cast<int>(currentValue.size()));
	}
	if (!keyText.empty()) {
		nextValue = currentValue.substr(0, static_cast<std::size_t>(start)) + keyText + currentValue.substr(static_cast<std::size_t>(end));
		if (nextValue.size() > static_cast<std::size_t>(link->maxLen)) return false;
		link->curPos = start + static_cast<int>(keyText.size());
	} else if (ctrlToArrow(event.keyDown.keyCode) == kbBack) {
		if (start != end) {
			nextValue = currentValue.substr(0, static_cast<std::size_t>(start)) + currentValue.substr(static_cast<std::size_t>(end));
			link->curPos = start;
		} else if (start > 0) {
			nextValue = currentValue.substr(0, static_cast<std::size_t>(start - 1)) + currentValue.substr(static_cast<std::size_t>(start));
			link->curPos = start - 1;
		} else
			return false;
	} else
		return false;

	strnzcpy(link->data, nextValue.c_str(), static_cast<std::size_t>(link->maxLen) + 1);
	linkedInputCursor = link->curPos;
	restoreInputLineCursor(*link, linkedInputCursor);
	inputValue = nextValue;
	return true;
}

bool MRDropList::showFilteredValues(const std::string &inputValue, bool restoreLinkFocusOnNoMatch) {
	const std::vector<std::string> fullValues = sourceValues;
	std::vector<std::string> filteredValues = matchingValues(sourceValues, inputValue);
	TInputLine *restoreLink = linkedInput;

	if (filteredValues.empty()) {
		hide();
		if (restoreLinkFocusOnNoMatch && restoreLink != nullptr) {
			restoreLink->select();
			if (linkedInputCursor >= 0) restoreInputLineCursor(*restoreLink, linkedInputCursor);
		}
		return false;
	}
	if (activeOwner == nullptr) return false;
	showValues(*activeOwner, activeAnchor, fullValues, filteredValues, inputValue, activeRelay, activeAcceptCommand, activeMaxVisibleRows);
	return true;
}

void MRDropList::restoreLinkedInputFocus() {
	if (linkedInput == nullptr) return;
	linkedInput->select();
	if (linkedInputCursor >= 0) restoreInputLineCursor(*linkedInput, linkedInputCursor);
}

bool MRDropList::handleSpeedSearchText(const std::string &text) {
	std::string candidate = speedSearchPrefix + text;
	short selection = firstMatchingIndex(candidate);

	if (text.empty()) return false;
	if (selection < 0) {
		candidate = text;
		selection = firstMatchingIndex(candidate);
	}
	if (selection >= 0 && listView != nullptr) {
		speedSearchPrefix = candidate;
		listView->focusItemNum(selection);
	}
	return true;
}

short MRDropList::firstMatchingIndex(const std::string &prefix) const {
	return firstMatchingValueIndex(itemValues, prefix);
}

void MRDropList::dispatchAcceptCommand() {
	TView *target = activeRelay != nullptr ? activeRelay : listOwner;
	TEvent event{};

	if (activeAcceptCommand == 0) return;
	while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
		target = target->owner;
	target = target != nullptr ? target : activeRelay;
	if (target == nullptr) return;
	event.what = evCommand;
	event.message.command = activeAcceptCommand;
	target->putEvent(event);
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

bool MRDropList::selectedValue(std::string &selectedValue) const {
	const short selection = selectedIndex();

	if (selection < 0 || static_cast<std::size_t>(selection) >= itemValues.size()) return false;
	selectedValue = itemValues[static_cast<std::size_t>(selection)];
	return true;
}

short MRDropList::selectedIndex() const {
	return listView != nullptr ? listView->selectedIndex() : -1;
}

void MRDropList::focusIndex(short index) {
	if (listView != nullptr) listView->focusItemNum(index);
}

void MRDropList::drawOpenList() noexcept {
	if (listView != nullptr) listView->drawView();
}

bool MRDropList::acceptSelection(std::string &selectedValue) {
	if (!this->selectedValue(selectedValue)) {
		hide();
		return false;
	}
	hide();
	return true;
}

MRStringChoiceField::MRStringChoiceField(const TRect &bounds, int maxLen) noexcept : TInputLine(bounds, maxLen) {
}

void MRStringChoiceField::setChoices(const std::vector<std::string> &values) {
	itemValues = values;
	dropList.hide();
}

const std::vector<std::string> &MRStringChoiceField::choices() const noexcept {
	return itemValues;
}

void MRStringChoiceField::setValue(const std::string &text) {
	strnzcpy(data, text.c_str(), static_cast<std::size_t>(maxLen) + 1);
	curPos = static_cast<int>(std::strlen(data));
	selStart = selEnd = 0;
	firstPos = inputLineEndFirstPos(*this);
	drawView();
}

std::string MRStringChoiceField::value() const {
	return data != nullptr ? std::string(data) : std::string();
}

TView *MRStringChoiceField::createDropListButton(TGroup &owner, const TRect &bounds, TView *relay, ushort command, bool triggerDownKey) {
	commandRelay = relay;
	openCommand = command;
	return dropList.createButton(owner, bounds, this, relay, command, triggerDownKey);
}

void MRStringChoiceField::toggleDropList(TGroup &owner, const TRect &anchor, TView *relay, ushort acceptCommand, short maxVisibleRows) {
	dropList.toggle(owner, anchor, itemValues, value(), relay, acceptCommand, maxVisibleRows);
}

void MRStringChoiceField::hideDropList() {
	dropList.hide();
}

bool MRStringChoiceField::handleDropListEvent(TEvent &event, bool hideOnOutsideMouseDown) {
	return dropList.handleOpenListEvent(event, hideOnOutsideMouseDown);
}

bool MRStringChoiceField::dropListVisible() const noexcept {
	return dropList.visible();
}

bool MRStringChoiceField::dropListContainsPoint(TPoint where) const noexcept {
	return dropList.containsPoint(where) || dropList.buttonContainsPoint(where);
}

bool MRStringChoiceField::acceptDropListSelection() {
	std::string selectedValue;

	if (!dropList.acceptSelection(selectedValue)) return false;
	setValue(selectedValue);
	return true;
}

void MRStringChoiceField::handleEvent(TEvent &event) {
	const bool autocompleteAfterInput = shouldAutocomplete(event);

	if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbDown && openCommand != 0) {
		dispatchOpenCommand();
		clearEvent(event);
		return;
	}
	TInputLine::handleEvent(event);
	if (autocompleteAfterInput && event.what == evNothing) autocompleteCurrentPrefix();
}

bool MRStringChoiceField::shouldAutocomplete(const TEvent &event) const {
	return !printableKeyText(event).empty();
}

void MRStringChoiceField::autocompleteCurrentPrefix() {
	std::string prefix;
	short selection = -1;
	const char *selectedValue = nullptr;
	const int valueLength = data != nullptr ? static_cast<int>(std::strlen(data)) : 0;

	if (itemValues.empty() || data == nullptr || curPos <= 0 || curPos > valueLength) return;
	prefix.assign(data, static_cast<std::size_t>(curPos));
	selection = firstMatchingValueIndex(itemValues, prefix);
	if (selection < 0) return;
	selectedValue = itemValues[static_cast<std::size_t>(selection)].c_str();
	if (static_cast<int>(std::strlen(selectedValue)) > maxLen) return;

	strnzcpy(data, selectedValue, static_cast<std::size_t>(maxLen) + 1);
	curPos = static_cast<int>(prefix.size());
	selStart = curPos;
	selEnd = static_cast<int>(std::strlen(data));
	firstPos = inputLineEndFirstPos(*this);
	drawView();
}

void MRStringChoiceField::dispatchOpenCommand() {
	TView *target = commandRelay != nullptr ? commandRelay : owner;

	while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
		target = target->owner;
	message(target != nullptr ? target : commandRelay, evCommand, openCommand, this);
}
