#include "MRMacroModelessControls.hpp"

#include "MRVMModelessUiRuntime.hpp"
#include "../../../ui/widgets/MRNumericSlider.hpp"

#define Uses_TDrawBuffer
#define Uses_TCheckBoxes
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TListViewer
#define Uses_TPoint
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TSItem
#define Uses_TView
#define Uses_TWindow
#include <tvision/tv.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace {

class MRMacroUiListView final : public TListViewer {
  public:
	MRMacroUiListView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> values, ushort command, std::string ownerWindowId, std::string ownerFieldId) noexcept : TListViewer(bounds, 1, nullptr, scrollBar), items(std::move(values)), activateCommand(command), windowId(std::move(ownerWindowId)), fieldId(std::move(ownerFieldId)) {
		setRange(static_cast<short>(items.size()));
	}

	void getText(char *dest, short item, short maxLen) override {
		if (dest == nullptr || maxLen <= 0) return;
		if (item < 0 || static_cast<std::size_t>(item) >= items.size()) {
			dest[0] = EOS;
			return;
		}
		std::strncpy(dest, items[static_cast<std::size_t>(item)].c_str(), static_cast<std::size_t>(maxLen - 1));
		dest[maxLen - 1] = EOS;
	}

	void handleEvent(TEvent &event) override {
		TListViewer::handleEvent(event);
		storeValue();
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter) {
			sendMacroUiActivationCommand(this, activateCommand);
			clearEvent(event);
			return;
		}
		if (event.what == evMouseDown && (event.mouse.eventFlags & meDoubleClick) != 0) {
			sendMacroUiActivationCommand(this, activateCommand);
			clearEvent(event);
		}
	}

	void setItems(std::vector<std::string> values, int start) {
		items = std::move(values);
		setRange(static_cast<short>(items.size()));
		if (!items.empty()) focusItemNum(static_cast<short>(std::clamp(start, 1, static_cast<int>(items.size())) - 1));
		storeValue();
		drawView();
	}

	bool setSelectedValue(const std::string &value) {
		std::vector<std::string>::const_iterator option = items.begin();

		if (items.empty()) return value.empty();
		if (!value.empty()) {
			option = std::find(items.begin(), items.end(), value);
			if (option == items.end()) return false;
		}
		focusItemNum(static_cast<short>(option - items.begin()));
		storeValue();
		drawView();
		return true;
	}

	int selectedIndex() const noexcept {
		return std::max(0, focused + 1);
	}

	std::string selectedText() const {
		const int index = selectedIndex();

		return index > 0 && static_cast<std::size_t>(index - 1) < items.size() ? items[static_cast<std::size_t>(index - 1)] : std::string();
	}

  private:
	void storeValue() {
		if (!windowId.empty() && !fieldId.empty()) mrvmStoreModelessWindowSelectFieldValue(windowId, fieldId, selectedText());
	}

	std::vector<std::string> items;
	ushort activateCommand = 0;
	std::string windowId;
	std::string fieldId;
};

struct MRMacroGridItem {
	std::string label;
	std::string text;
	std::string detail;
};

MRMacroGridItem parseGridItem(const std::string &source) {
	MRMacroGridItem item;
	const std::size_t firstTab = source.find('\t');
	const std::size_t secondTab = firstTab != std::string::npos ? source.find('\t', firstTab + 1) : std::string::npos;

	if (firstTab == std::string::npos) {
		item.label = source;
		item.text = source;
		return item;
	}
	item.label = source.substr(0, firstTab);
	if (secondTab == std::string::npos) {
		item.text = source.substr(firstTab + 1);
		return item;
	}
	item.text = source.substr(firstTab + 1, secondTab - firstTab - 1);
	item.detail = source.substr(secondTab + 1);
	return item;
}

std::vector<MRMacroGridItem> parseGridItems(const std::vector<std::string> &values) {
	std::vector<MRMacroGridItem> items;

	items.reserve(values.size());
	for (std::size_t index = 0; index < values.size(); ++index)
		items.push_back(parseGridItem(values[index]));
	return items;
}

class MRMacroUiGridView final : public TView {
  public:
	MRMacroUiGridView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> values, ushort command) : TView(bounds), items(parseGridItems(values)), scrollBar(scrollBar), activateCommand(command) {
		options |= ofSelectable;
		eventMask |= evMouseDown | evMouseWheel | evKeyDown | evBroadcast;
		updateCellWidth();
		updateScrollBar();
	}

	void draw() override {
		TDrawBuffer row;
		const TColorAttr normal = getColor(1);
		const TColorAttr selected = getColor(3);
		const short blankRow = size.y > 1 ? size.y - 2 : 0;
		const short detailRow = size.y > 0 ? size.y - 1 : 0;

		updateScrollBar();
		for (short y = 0; y < size.y; ++y) {
			row.moveChar(0, ' ', normal, size.x);
			writeLine(0, y, size.x, 1, row);
		}
		for (std::size_t index = 0; index < items.size(); ++index) {
			const int rowIndex = static_cast<int>(index) / columns() - scrollOffset;
			const int colIndex = static_cast<int>(index) % columns();
			const short x = static_cast<short>(gridLeftOffset() + colIndex * cellWidth);
			const short y = static_cast<short>(rowIndex);
			if (y < 0 || y >= blankRow || x >= size.x) continue;
			drawCell(index, x, y, index == selectedIndex ? selected : normal);
		}
		drawDetail(detailRow, normal);
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evMouseDown && containsMouse(event)) {
			TPoint local = makeLocal(event.mouse.where);
			const int gridLeft = gridLeftOffset();
			if (local.x >= gridLeft && local.x < gridLeft + gridWidth()) {
				const int col = cellWidth > 0 ? (local.x - gridLeft) / cellWidth : 0;
				const int row = local.y + scrollOffset;
				const std::size_t index = static_cast<std::size_t>(row * columns() + col);
				if (index < items.size()) {
					selectedIndex = index;
					ensureSelectedVisible();
					drawView();
					if ((event.mouse.eventFlags & meDoubleClick) != 0) sendMacroUiActivationCommand(this, activateCommand);
				}
			}
			clearEvent(event);
			return;
		}
		if (event.what == evMouseWheel && containsMouse(event)) {
			const int step = event.mouse.wheel == mwUp || event.mouse.wheel == mwLeft ? -1 : 1;
			setScrollOffset(scrollOffset + step);
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown) {
			const ushort arrow = ctrlToArrow(event.keyDown.keyCode);
			if (moveSelection(arrow)) {
				clearEvent(event);
				return;
			}
			if (arrow == kbEnter) {
				sendMacroUiActivationCommand(this, activateCommand);
				clearEvent(event);
				return;
			}
		}
		if (event.what == evBroadcast && event.message.command == cmScrollBarChanged && event.message.infoPtr == scrollBar) {
			setScrollOffset(scrollBar != nullptr ? scrollBar->value : 0);
			clearEvent(event);
			return;
		}
		TView::handleEvent(event);
	}

	void setItems(std::vector<std::string> values, int start) {
		items = parseGridItems(values);
		updateCellWidth();
		selectedIndex = items.empty() ? 0 : static_cast<std::size_t>(std::clamp(start, 1, static_cast<int>(items.size())) - 1);
		ensureSelectedVisible();
		drawView();
	}

	void refreshItems(std::vector<std::string> values) {
		const int savedScrollOffset = scrollOffset;
		const std::size_t savedSelectedIndex = selectedIndex;

		items = parseGridItems(values);
		updateCellWidth();
		if (items.empty()) {
			selectedIndex = 0;
			scrollOffset = 0;
		} else {
			selectedIndex = std::min(savedSelectedIndex, items.size() - 1);
			scrollOffset = std::max(0, std::min(savedScrollOffset, maxScrollOffset()));
		}
		updateScrollBar();
		drawView();
	}

	int selectedIndexValue() const noexcept {
		return items.empty() ? 0 : static_cast<int>(selectedIndex + 1);
	}

	std::string selectedText() const {
		return selectedIndex < items.size() ? items[selectedIndex].text : std::string();
	}

  private:
	void updateCellWidth() {
		const int availableWidth = std::max(1, static_cast<int>(size.x) - 1);
		int widestLabel = 2;

		for (std::size_t index = 0; index < items.size(); ++index)
			widestLabel = std::max(widestLabel, strwidth(items[index].label.c_str()) + 2);
		cellWidth = std::min(availableWidth, widestLabel);
	}

	int columns() const noexcept {
		return std::max(1, (static_cast<int>(size.x) - 1) / cellWidth);
	}

	int gridWidth() const noexcept {
		return std::max(0, columns() * cellWidth);
	}

	int gridLeftOffset() const noexcept {
		return std::max(0, (static_cast<int>(size.x) - gridWidth()) / 2);
	}

	void drawCell(std::size_t index, short x, short y, TColorAttr attr) {
		TDrawBuffer cell;
		std::string text = items[index].label;
		if (static_cast<int>(text.size()) + 1 < cellWidth) text = " " + text;
		cell.moveChar(0, ' ', attr, static_cast<ushort>(cellWidth));
		cell.moveStr(0, text.c_str(), attr, static_cast<ushort>(cellWidth));
		writeLine(x, y, static_cast<short>(std::min(cellWidth, static_cast<int>(size.x - x))), 1, cell);
	}

	void drawDetail(short y, TColorAttr attr) {
		if (items.empty() || y < 0 || y >= size.y) return;
		TDrawBuffer row;
		std::string text = items[selectedIndex].label;
		if (!items[selectedIndex].detail.empty()) {
			if (!text.empty()) text += " ";
			text += items[selectedIndex].detail;
		}
		const int detailWidth = strwidth(text.c_str());
		const int start = std::max(0, (static_cast<int>(size.x) - detailWidth) / 2);
		row.moveChar(0, ' ', attr, size.x);
		row.moveStr(static_cast<ushort>(start), text.c_str(), attr, static_cast<ushort>(std::max(0, size.x - start)));
		writeLine(0, y, size.x, 1, row);
	}

	bool moveSelection(ushort keyCode) {
		if (items.empty()) return false;
		std::size_t next = selectedIndex;
		const int cols = columns();
		const int pageStep = std::max(cols, cols * 4);

		switch (keyCode) {
			case kbLeft:
				next = selectedIndex == 0 ? items.size() - 1 : selectedIndex - 1;
				break;
			case kbRight:
				next = (selectedIndex + 1) % items.size();
				break;
			case kbUp:
				next = selectedIndex < static_cast<std::size_t>(cols) ? selectedIndex : selectedIndex - static_cast<std::size_t>(cols);
				break;
			case kbDown:
				next = std::min(items.size() - 1, selectedIndex + static_cast<std::size_t>(cols));
				break;
			case kbHome:
				next = 0;
				break;
			case kbEnd:
				next = items.size() - 1;
				break;
			case kbPgUp:
				next = selectedIndex < static_cast<std::size_t>(pageStep) ? 0 : selectedIndex - static_cast<std::size_t>(pageStep);
				break;
			case kbPgDn:
				next = std::min(items.size() - 1, selectedIndex + static_cast<std::size_t>(pageStep));
				break;
			default:
				return false;
		}
		if (next != selectedIndex) {
			selectedIndex = next;
			ensureSelectedVisible();
			drawView();
		}
		return true;
	}

	int totalRows() const noexcept {
		const int cols = columns();
		return cols <= 0 ? 0 : static_cast<int>((items.size() + static_cast<std::size_t>(cols - 1)) / static_cast<std::size_t>(cols));
	}

	int visibleRows() const noexcept {
		return std::max(1, static_cast<int>(size.y) - 2);
	}

	int maxScrollOffset() const noexcept {
		return std::max(0, totalRows() - visibleRows());
	}

	void setScrollOffset(int offset) {
		const int clamped = std::max(0, std::min(offset, maxScrollOffset()));
		if (clamped == scrollOffset) return;
		scrollOffset = clamped;
		updateScrollBar();
		drawView();
	}

	void updateScrollBar() {
		if (scrollBar == nullptr) return;
		scrollBar->setParams(scrollOffset, 0, maxScrollOffset(), visibleRows(), 1);
	}

	void ensureSelectedVisible() {
		if (items.empty()) {
			scrollOffset = 0;
			updateScrollBar();
			return;
		}
		const int row = static_cast<int>(selectedIndex) / columns();
		if (row < scrollOffset) scrollOffset = row;
		else if (row >= scrollOffset + visibleRows())
			scrollOffset = row - visibleRows() + 1;
		scrollOffset = std::max(0, std::min(scrollOffset, maxScrollOffset()));
		updateScrollBar();
	}

	std::vector<MRMacroGridItem> items;
	TScrollBar *scrollBar = nullptr;
	ushort activateCommand = 0;
	std::size_t selectedIndex = 0;
	int scrollOffset = 0;
	int cellWidth = 4;
};

class MRMacroModelessTextInput final : public TInputLine {
  public:
	MRMacroModelessTextInput(const TRect &bounds, int width, std::string ownerWindowId, std::string ownerFieldId, const std::string &text) : TInputLine(bounds, std::max(4, width)), windowId(std::move(ownerWindowId)), fieldId(std::move(ownerFieldId)) {
		setValue(text);
	}

	void handleEvent(TEvent &event) override {
		TInputLine::handleEvent(event);
		storeValue();
	}

	void setValue(const std::string &value) {
		std::string buffer(value);

		TInputLine::setData(buffer.data());
		storeValue();
	}

  private:
	void storeValue() {
		mrvmStoreModelessWindowTextFieldValue(windowId, fieldId, data != nullptr ? data : "");
	}

	std::string windowId;
	std::string fieldId;
};

class MRMacroModelessIntInput final : public TInputLine {
  public:
	MRMacroModelessIntInput(const TRect &bounds, int width, std::string ownerWindowId, std::string ownerFieldId, int initialValue) : TInputLine(bounds, std::max(4, width)), windowId(std::move(ownerWindowId)), fieldId(std::move(ownerFieldId)) {
		eventMask |= evBroadcast;
		setValue(initialValue);
	}

	void handleEvent(TEvent &event) override {
		const bool releasedFocus = event.what == evBroadcast && event.message.command == cmReleasedFocus;

		TInputLine::handleEvent(event);
		if (storeValue() || !releasedFocus) return;
		int retainedValue = 0;

		if (mrvmReadModelessWindowIntFieldValue(windowId, fieldId, retainedValue)) setValue(retainedValue);
	}

	void setValue(int value) {
		std::string buffer = std::to_string(value);

		TInputLine::setData(buffer.data());
		static_cast<void>(storeValue());
	}

  private:
	bool storeValue() {
		char *end = nullptr;
		long parsed = 0;

		if (data == nullptr || data[0] == EOS) return false;
		errno = 0;
		parsed = std::strtol(data, &end, 10);
		if (errno == ERANGE || end == data || end == nullptr || *end != EOS || parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) return false;
		return mrvmStoreModelessWindowIntFieldValue(windowId, fieldId, static_cast<int>(parsed));
	}

	std::string windowId;
	std::string fieldId;
};

class MRMacroModelessProgressView final : public TView {
  public:
	MRMacroModelessProgressView(const TRect &bounds, std::string ownerWindowId, std::string ownerFieldId) : TView(bounds), windowId(std::move(ownerWindowId)), fieldId(std::move(ownerFieldId)) {
		options &= ~(ofSelectable | ofFirstClick);
		eventMask &= static_cast<ushort>(~(evMouseDown | evMouseUp | evMouseMove | evKeyDown));
	}

	void draw() override {
		TDrawBuffer buffer;
		int total = 1;
		int value = 0;
		const int width = size.x;
		const int interiorWidth = std::max(0, width - 2);

		static_cast<void>(mrvmReadModelessWindowProgressFieldValue(windowId, fieldId, total, value));
		buffer.moveChar(0, ' ', getColor(1), static_cast<ushort>(width));
		if (width > 0) buffer.moveChar(0, '[', getColor(1), 1);
		MRProgressSlider::drawProgress(buffer, 1, interiorWidth, static_cast<std::size_t>(std::max(0, value)), static_cast<std::size_t>(std::max(0, total)), std::string(), getColor(1), getColor(2),
		                               MRProgressSlider::Direction::LeftToRight, '-', '#');
		if (width > 1) buffer.moveChar(static_cast<ushort>(width - 1), ']', getColor(1), 1);
		writeLine(0, 0, width, 1, buffer);
	}

  private:
	std::string windowId;
	std::string fieldId;
};

class MRMacroModelessLogView final : public TView {
  public:
	MRMacroModelessLogView(const TRect &bounds, std::string ownerWindowId, std::string ownerLogId) : TView(bounds), windowId(std::move(ownerWindowId)), logId(std::move(ownerLogId)) {
		options &= ~(ofSelectable | ofFirstClick);
		eventMask &= static_cast<ushort>(~(evMouseDown | evMouseUp | evMouseMove | evKeyDown));
	}

	void draw() override {
		std::vector<std::string> lines;
		const TColorAttr color = getColor(1);
		const int width = size.x;
		const int height = size.y;
		int firstLine = 0;

		static_cast<void>(mrvmReadModelessWindowLogFieldLines(windowId, logId, lines));
		if (static_cast<int>(lines.size()) > height) firstLine = static_cast<int>(lines.size()) - height;
		for (int row = 0; row < height; ++row) {
			TDrawBuffer buffer;
			const int lineIndex = firstLine + row;

			buffer.moveChar(0, ' ', color, static_cast<ushort>(width));
			if (width > 0 && lineIndex >= 0 && static_cast<std::size_t>(lineIndex) < lines.size()) buffer.moveStr(0, lines[static_cast<std::size_t>(lineIndex)].c_str(), color, width);
			writeLine(0, row, width, 1, buffer);
		}
	}

  private:
	std::string windowId;
	std::string logId;
};

class MRMacroModelessBoolInput final : public TCheckBoxes {
  public:
	MRMacroModelessBoolInput(const TRect &bounds, std::string ownerWindowId, std::string ownerFieldId, const std::string &caption, bool initialValue) : TCheckBoxes(bounds, new TSItem(caption.c_str(), nullptr)), windowId(std::move(ownerWindowId)), fieldId(std::move(ownerFieldId)) {
		setValue(initialValue);
	}

	void handleEvent(TEvent &event) override {
		TCheckBoxes::handleEvent(event);
		storeValue();
	}

	void setValue(bool nextValue) {
		ushort checked = nextValue ? 1 : 0;

		TCheckBoxes::setData(&checked);
		storeValue();
	}

  private:
	void storeValue() {
		ushort checked = 0;

		TCheckBoxes::getData(&checked);
		mrvmStoreModelessWindowBoolFieldValue(windowId, fieldId, checked != 0);
	}

	std::string windowId;
	std::string fieldId;
};

} // namespace

void sendMacroUiActivationCommand(TView *source, unsigned short command) {
	TView *target = source != nullptr ? source->owner : nullptr;

	if (command == 0) return;
	while (target != nullptr && dynamic_cast<TWindow *>(target) == nullptr)
		target = target->owner;
	if (target != nullptr) message(target, evCommand, static_cast<ushort>(command), source);
}

TView *createMacroUiListView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> values, unsigned short command) {
	return new MRMacroUiListView(bounds, scrollBar, std::move(values), static_cast<ushort>(command), std::string(), std::string());
}

void setMacroUiListItems(TView *view, std::vector<std::string> values, int start) {
	MRMacroUiListView *listView = dynamic_cast<MRMacroUiListView *>(view);

	if (listView != nullptr) listView->setItems(std::move(values), start);
}

int macroUiListSelectedIndex(const TView *view) {
	const MRMacroUiListView *listView = dynamic_cast<const MRMacroUiListView *>(view);

	return listView != nullptr ? listView->selectedIndex() : 0;
}

std::string macroUiListSelectedText(const TView *view) {
	const MRMacroUiListView *listView = dynamic_cast<const MRMacroUiListView *>(view);

	return listView != nullptr ? listView->selectedText() : std::string();
}

TView *createMacroUiGridView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> values, unsigned short command) {
	return new MRMacroUiGridView(bounds, scrollBar, std::move(values), static_cast<ushort>(command));
}

void setMacroUiGridItems(TView *view, std::vector<std::string> values, int start) {
	MRMacroUiGridView *gridView = dynamic_cast<MRMacroUiGridView *>(view);

	if (gridView != nullptr) gridView->setItems(std::move(values), start);
}

void refreshMacroUiGridItems(TView *view, std::vector<std::string> values) {
	MRMacroUiGridView *gridView = dynamic_cast<MRMacroUiGridView *>(view);

	if (gridView != nullptr) gridView->refreshItems(std::move(values));
}

int macroUiGridSelectedIndex(const TView *view) {
	const MRMacroUiGridView *gridView = dynamic_cast<const MRMacroUiGridView *>(view);

	return gridView != nullptr ? gridView->selectedIndexValue() : 0;
}

std::string macroUiGridSelectedText(const TView *view) {
	const MRMacroUiGridView *gridView = dynamic_cast<const MRMacroUiGridView *>(view);

	return gridView != nullptr ? gridView->selectedText() : std::string();
}

std::string macroUiGridItemText(const std::string &value) {
	return parseGridItem(value).text;
}

TView *createMacroModelessTextInput(const TRect &bounds, int width, const std::string &windowId, const std::string &fieldId, const std::string &text) {
	return new MRMacroModelessTextInput(bounds, width, windowId, fieldId, text);
}

bool setMacroModelessTextInputValue(TView *view, const std::string &text) {
	MRMacroModelessTextInput *input = dynamic_cast<MRMacroModelessTextInput *>(view);

	if (input == nullptr) return false;
	input->setValue(text);
	return true;
}

TView *createMacroModelessBoolInput(const TRect &bounds, const std::string &windowId, const std::string &fieldId, const std::string &caption, bool value) {
	return new MRMacroModelessBoolInput(bounds, windowId, fieldId, caption, value);
}

bool setMacroModelessBoolInputValue(TView *view, bool value) {
	MRMacroModelessBoolInput *input = dynamic_cast<MRMacroModelessBoolInput *>(view);

	if (input == nullptr) return false;
	input->setValue(value);
	return true;
}

TView *createMacroModelessIntInput(const TRect &bounds, int width, const std::string &windowId, const std::string &fieldId, int value) {
	return new MRMacroModelessIntInput(bounds, width, windowId, fieldId, value);
}

bool setMacroModelessIntInputValue(TView *view, int value) {
	MRMacroModelessIntInput *input = dynamic_cast<MRMacroModelessIntInput *>(view);

	if (input == nullptr) return false;
	input->setValue(value);
	return true;
}

TView *createMacroModelessProgressView(const TRect &bounds, const std::string &windowId, const std::string &fieldId) {
	return new MRMacroModelessProgressView(bounds, windowId, fieldId);
}

void redrawMacroModelessProgressView(TView *view) {
	if (view != nullptr) view->drawView();
}

TView *createMacroModelessLogView(const TRect &bounds, const std::string &windowId, const std::string &logId) {
	return new MRMacroModelessLogView(bounds, windowId, logId);
}

void redrawMacroModelessLogView(TView *view) {
	if (view != nullptr) view->drawView();
}

TView *createMacroModelessSelectInput(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> options, const std::string &windowId, const std::string &fieldId, const std::string &value) {
	MRMacroUiListView *input = new MRMacroUiListView(bounds, scrollBar, std::move(options), 0, windowId, fieldId);

	if (input != nullptr && !input->setSelectedValue(value)) static_cast<void>(input->setSelectedValue(std::string()));
	return input;
}

bool setMacroModelessSelectInputValue(TView *view, const std::string &value) {
	MRMacroUiListView *input = dynamic_cast<MRMacroUiListView *>(view);

	return input != nullptr && input->setSelectedValue(value);
}

bool setMacroModelessSelectInputOptions(TView *view, std::vector<std::string> options, const std::string &value) {
	MRMacroUiListView *input = dynamic_cast<MRMacroUiListView *>(view);

	if (input == nullptr) return false;
	input->setItems(std::move(options), 1);
	if (!input->setSelectedValue(value)) static_cast<void>(input->setSelectedValue(std::string()));
	return true;
}
