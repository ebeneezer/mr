#include "MRMacroModelessUi.hpp"
#include "MRVM.hpp"

#define Uses_TButton
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TListViewer
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TWindow
#include <tvision/tv.h>

#include "../ui/MRFrame.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <utility>

namespace {

constexpr ushort cmMacroModelessBase = 0x6F20;
constexpr ushort cmMacroModelessMax = 0x6FFF;

struct MRMacroModelessButtonCaption {
	std::string displayLabel;
	std::vector<ushort> hotKeys;
};

MRMacroModelessListResolver g_listResolver = nullptr;
MRMacroModelessCommandRunner g_commandRunner = nullptr;
std::map<std::string, class MRMacroModelessWindow *> g_windows;

static std::string trimCaption(const std::string &value) {
	std::size_t first = 0;
	std::size_t last = value.size();

	while (first < last && std::isspace(static_cast<unsigned char>(value[first])))
		++first;
	while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
		--last;
	return value.substr(first, last - first);
}

static std::string upperCaptionToken(const std::string &value) {
	std::string result(value);

	for (std::size_t index = 0; index < result.size(); ++index)
		result[index] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[index])));
	return result;
}

static ushort namedHotKeyCode(const std::string &name) noexcept {
	struct NamedKey {
		const char *name;
		ushort code;
	};
	static const NamedKey namedKeys[] = {
	    {"ENTER", kbEnter}, {"ESC", kbEsc}, {"ESCAPE", kbEsc}, {"TAB", kbTab}, {"F1", kbF1}, {"F2", kbF2}, {"F3", kbF3}, {"F4", kbF4}, {"F5", kbF5}, {"F6", kbF6}, {"F7", kbF7}, {"F8", kbF8}, {"F9", kbF9}, {"F10", kbF10}, {"F11", kbF11}, {"F12", kbF12},
	};
	const std::size_t namedKeyCount = sizeof(namedKeys) / sizeof(namedKeys[0]);
	const std::string key = upperCaptionToken(name);

	for (std::size_t index = 0; index < namedKeyCount; ++index)
		if (key == namedKeys[index].name) return namedKeys[index].code;
	return 0;
}

static MRMacroModelessButtonCaption parseButtonCaption(const std::string &text) {
	MRMacroModelessButtonCaption entry;
	std::size_t index = 0;
	const std::string label = trimCaption(text);

	while (index < label.size()) {
		if (label[index] == '~') {
			const std::size_t close = label.find('~', index + 1);
			if (close != std::string::npos && close > index + 1) {
				entry.displayLabel += label.substr(index + 1, close - index - 1);
				entry.hotKeys.push_back(static_cast<ushort>(std::toupper(static_cast<unsigned char>(label[index + 1]))));
				index = close + 1;
				continue;
			}
		}
		if (label[index] == '<') {
			const std::size_t close = label.find('>', index + 1);
			if (close != std::string::npos) {
				const ushort keyCode = namedHotKeyCode(trimCaption(label.substr(index + 1, close - index - 1)));
				if (keyCode != 0) entry.hotKeys.push_back(keyCode);
				index = close + 1;
				continue;
			}
		}
		entry.displayLabel.push_back(label[index]);
		++index;
	}
	if (entry.displayLabel.size() == 1) entry.hotKeys.push_back(static_cast<ushort>(std::toupper(static_cast<unsigned char>(entry.displayLabel.front()))));
	if (entry.displayLabel.empty()) entry.displayLabel = " ";
	return entry;
}

static std::vector<std::string> resolveListItems(const std::string &itemSpec) {
	if (g_listResolver == nullptr) return std::vector<std::string>();
	return g_listResolver(itemSpec);
}

class MRMacroModelessDisplayLine final : public TView {
  public:
	MRMacroModelessDisplayLine(const TRect &bounds, std::string text) noexcept : TView(bounds), text(std::move(text)) {
		options &= ~(ofSelectable | ofFirstClick);
		eventMask &= static_cast<ushort>(~(evMouseDown | evMouseUp | evMouseMove | evKeyDown));
	}

	void draw() override {
		TDrawBuffer buffer;
		const TColorAttr color = getColor(1);
		const int width = size.x;
		const std::string value = text.empty() ? std::string(" ") : text;

		buffer.moveChar(0, ' ', color, static_cast<ushort>(width));
		if (width > 0) buffer.moveStr(0, value.c_str(), color, width);
		writeLine(0, 0, width, 1, buffer);
	}

	void setText(std::string value) {
		if (text == value) return;
		text = std::move(value);
		drawView();
	}

  private:
	std::string text;
};

static TRect modelessBounds(const MRMacroModelessWindowDefinition &definition) {
	TRect desk = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, 80, 25);
	int windowWidth = std::min(std::max(24, definition.width), desk.b.x - desk.a.x);
	int windowHeight = std::min(std::max(8, definition.height), desk.b.y - desk.a.y);
	int left = desk.a.x + (desk.b.x - desk.a.x - windowWidth) / 2;
	int top = desk.a.y + (desk.b.y - desk.a.y - windowHeight) / 2;

	if (definition.x > 0) left = std::clamp(desk.a.x + definition.x - 1, desk.a.x, desk.b.x - windowWidth);
	if (definition.y > 0) top = std::clamp(desk.a.y + definition.y - 1, desk.a.y, desk.b.y - windowHeight);
	return TRect(left, top, left + windowWidth, top + windowHeight);
}

static TFrame *initMacroModelessFrame(TRect bounds);

class MRMacroModelessListView final : public TListViewer {
  public:
	MRMacroModelessListView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> values, ushort command) noexcept : TListViewer(bounds, 1, nullptr, scrollBar), items(std::move(values)), activateCommand(command) {
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
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter && owner != nullptr) {
			message(owner, evCommand, activateCommand, this);
			clearEvent(event);
			return;
		}
		if (event.what == evMouseDown && (event.mouse.eventFlags & meDoubleClick) != 0 && owner != nullptr) {
			message(owner, evCommand, activateCommand, this);
			clearEvent(event);
		}
	}

	void setItems(std::vector<std::string> values, int start) {
		items = std::move(values);
		setRange(static_cast<short>(items.size()));
		if (!items.empty()) focusItemNum(static_cast<short>(std::clamp(start, 1, static_cast<int>(items.size())) - 1));
		drawView();
	}

	const std::vector<std::string> &values() const noexcept {
		return items;
	}

  private:
	std::vector<std::string> items;
	ushort activateCommand = 0;
};

struct MRMacroGridItem {
	std::string label;
	std::string text;
	std::string detail;
};

static MRMacroGridItem parseGridItem(const std::string &source) {
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

static std::vector<MRMacroGridItem> parseGridItems(const std::vector<std::string> &values) {
	std::vector<MRMacroGridItem> items;

	items.reserve(values.size());
	for (std::size_t index = 0; index < values.size(); ++index)
		items.push_back(parseGridItem(values[index]));
	return items;
}

class MRMacroModelessGridView final : public TView {
  public:
	MRMacroModelessGridView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> values, ushort command) : TView(bounds), items(parseGridItems(values)), scrollBar(scrollBar), activateCommand(command) {
		options |= ofSelectable;
		eventMask |= evMouseDown | evMouseWheel | evKeyDown | evBroadcast;
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
					if ((event.mouse.eventFlags & meDoubleClick) != 0 && owner != nullptr) message(owner, evCommand, activateCommand, this);
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
			if (arrow == kbEnter && owner != nullptr) {
				message(owner, evCommand, activateCommand, this);
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
		selectedIndex = items.empty() ? 0 : static_cast<std::size_t>(std::clamp(start, 1, static_cast<int>(items.size())) - 1);
		ensureSelectedVisible();
		drawView();
	}

	void refreshItems(std::vector<std::string> values) {
		const int savedScrollOffset = scrollOffset;
		const std::size_t savedSelectedIndex = selectedIndex;

		items = parseGridItems(values);
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

	[[nodiscard]] int selectedIndexValue() const noexcept {
		return items.empty() ? 0 : static_cast<int>(selectedIndex + 1);
	}

	[[nodiscard]] std::string selectedText() const {
		return selectedIndex < items.size() ? items[selectedIndex].text : std::string();
	}

  private:
	[[nodiscard]] int columns() const noexcept {
		return std::max(1, (static_cast<int>(size.x) - 1) / cellWidth);
	}

	[[nodiscard]] int gridWidth() const noexcept {
		return std::max(0, columns() * cellWidth);
	}

	[[nodiscard]] int gridLeftOffset() const noexcept {
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

	[[nodiscard]] int totalRows() const noexcept {
		const int cols = columns();
		return cols <= 0 ? 0 : static_cast<int>((items.size() + static_cast<std::size_t>(cols - 1)) / static_cast<std::size_t>(cols));
	}

	[[nodiscard]] int visibleRows() const noexcept {
		return std::max(1, static_cast<int>(size.y) - 2);
	}

	[[nodiscard]] int maxScrollOffset() const noexcept {
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
	static constexpr int cellWidth = 4;
};

class MRMacroModelessWindow final : public TWindow {
  public:
	explicit MRMacroModelessWindow(const MRMacroModelessWindowDefinition &windowDefinition) : TWindowInit(initMacroModelessFrame), TWindow(modelessBounds(windowDefinition), windowDefinition.title.empty() ? "MRMac" : windowDefinition.title.c_str(), wnNoNumber), definition(windowDefinition) {
		if (!definition.grids.empty()) flags &= static_cast<ushort>(~wfZoom);
		buildControls();
	}

	TPalette &getPalette() const override {
		static TPalette palette(cpGrayDialog, sizeof(cpGrayDialog) - 1);
		return palette;
	}

	void close() override {
		std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(definition.windowId);

		if (it != g_windows.end() && it->second == this) g_windows.erase(it);
		mrvmRemoveModelessWindowDefinition(definition.windowId);
		TWindow::close();
	}

	void changeBounds(const TRect &bounds) override {
		const TRect previousBounds = getBounds();

		TWindow::changeBounds(bounds);
		if (previousBounds != getBounds()) storeLiveGeometry();
	}

	void handleEvent(TEvent &event) override {
		const TRect previousBounds = getBounds();

		if (event.what == evKeyDown) {
			const unsigned char typedChar = static_cast<unsigned char>(event.keyDown.charScan.charCode);
			const ushort hotKey = typedChar >= 32 ? static_cast<ushort>(std::toupper(typedChar)) : event.keyDown.keyCode;

			for (std::size_t index = 0; index < hotKeys.size(); ++index)
				if (hotKeys[index].first == hotKey) {
					runButton(hotKeys[index].second);
					clearEvent(event);
					return;
				}
		}

		TWindow::handleEvent(event);
		if (previousBounds != getBounds()) storeLiveGeometry();
		if (event.what == evCommand) {
			std::map<ushort, int>::const_iterator buttonIt = commandToButton.find(event.message.command);
			if (buttonIt != commandToButton.end()) {
				runButton(buttonIt->second);
				clearEvent(event);
				return;
			}
			std::map<ushort, int>::const_iterator listIt = commandToList.find(event.message.command);
			if (listIt != commandToList.end()) {
				selection = readSelection(listIt->second);
				clearEvent(event);
				return;
			}
			std::map<ushort, int>::const_iterator gridIt = commandToGrid.find(event.message.command);
			if (gridIt != commandToGrid.end()) {
				std::map<int, MRMacroModelessGridSpec>::const_iterator specIt = gridSpecs.find(gridIt->second);

				selection = readSelection(gridIt->second);
				if (specIt != gridSpecs.end()) runModelessMacro(definition.windowId, specIt->second.macroSpec);
				clearEvent(event);
				return;
			}
		}
	}

	void refreshLists() {
		refreshDisplays();
		for (std::map<int, MRMacroModelessListView *>::iterator entry = listViews.begin(); entry != listViews.end(); ++entry) {
			std::map<int, MRMacroModelessListBoxSpec>::const_iterator specIt = listSpecs.find(entry->first);
			MRMacroModelessListView *listView = entry->second;
			const int oldIndex = listView != nullptr ? listView->focused + 1 : 1;

			if (listView == nullptr || specIt == listSpecs.end()) continue;
			listView->setItems(resolveListItems(specIt->second.itemSpec), std::max(1, oldIndex));
		}
		for (std::map<int, MRMacroModelessGridView *>::iterator entry = gridViews.begin(); entry != gridViews.end(); ++entry) {
			std::map<int, MRMacroModelessGridSpec>::const_iterator specIt = gridSpecs.find(entry->first);
			MRMacroModelessGridView *gridView = entry->second;

			if (gridView == nullptr || specIt == gridSpecs.end()) continue;
			gridView->refreshItems(resolveListItems(specIt->second.itemSpec));
		}
		if (selection.controlId != 0) selection = readSelection(selection.controlId);
	}

	void updateDefinition(const MRMacroModelessWindowDefinition &nextDefinition) {
		definition = nextDefinition;
		refreshLists();
		storeLiveGeometry();
	}

	bool updateDisplay(int displayIndex, const std::string &text) {
		const std::size_t index = static_cast<std::size_t>(displayIndex - 1);

		if (displayIndex <= 0 || index >= displayViews.size() || index >= definition.displays.size()) return false;
		definition.displays[index].text = text;
		if (displayViews[index] != nullptr) displayViews[index]->setText(text);
		return true;
	}

	void storeLiveGeometry() {
		const TRect bounds = getBounds();

		mrvmStoreModelessWindowLiveGeometry(definition.windowId, bounds.a.x, bounds.a.y, bounds.b.x - bounds.a.x, bounds.b.y - bounds.a.y);
	}

  private:
	void buildControls() {
		ushort nextCommand = cmMacroModelessBase;

		options |= ofTileable;
		for (std::size_t labelIndex = 0; labelIndex < definition.labels.size(); ++labelIndex) {
			const MRMacroModelessLabelSpec &label = definition.labels[labelIndex];

			insert(new TStaticText(TRect(label.x, label.y, label.x + strwidth(label.text.c_str()), label.y + 1), label.text.c_str()));
		}

		for (std::size_t displayIndex = 0; displayIndex < definition.displays.size(); ++displayIndex) {
			const MRMacroModelessDisplaySpec &display = definition.displays[displayIndex];
			MRMacroModelessDisplayLine *displayLine = new MRMacroModelessDisplayLine(TRect(display.x, display.y, display.x + display.width, display.y + 1), display.text);

			insert(displayLine);
			displayViews.push_back(displayLine);
		}

		for (std::size_t listBoxIndex = 0; listBoxIndex < definition.listBoxes.size(); ++listBoxIndex) {
			const MRMacroModelessListBoxSpec &listBox = definition.listBoxes[listBoxIndex];
			const std::vector<std::string> items = resolveListItems(listBox.itemSpec);
			const int listTop = listBox.label.empty() ? listBox.y : listBox.y + 1;
			TScrollBar *scrollBar = new TScrollBar(TRect(listBox.x + listBox.width - 1, listTop, listBox.x + listBox.width, listTop + listBox.height));
			MRMacroModelessListView *listView = nullptr;

			if (nextCommand >= cmMacroModelessMax) break;
			if (!listBox.label.empty()) insert(new TStaticText(TRect(listBox.x, listBox.y, listBox.x + strwidth(listBox.label.c_str()), listBox.y + 1), listBox.label.c_str()));
			insert(scrollBar);
			listView = new MRMacroModelessListView(TRect(listBox.x, listTop, listBox.x + listBox.width - 1, listTop + listBox.height), scrollBar, items, nextCommand);
			insert(listView);
			if (!items.empty()) listView->focusItemNum(static_cast<short>(std::clamp(listBox.start, 1, static_cast<int>(items.size())) - 1));
			commandToList[nextCommand] = listBox.id;
			listSpecs[listBox.id] = listBox;
			listViews[listBox.id] = listView;
			if (selection.controlId == 0) selection = readSelection(listBox.id);
			++nextCommand;
		}

		for (std::size_t gridIndex = 0; gridIndex < definition.grids.size(); ++gridIndex) {
			const MRMacroModelessGridSpec &grid = definition.grids[gridIndex];
			const std::vector<std::string> items = resolveListItems(grid.itemSpec);
			const int gridTop = grid.label.empty() ? grid.y : grid.y + 1;
			TScrollBar *scrollBar = new TScrollBar(TRect(grid.x + grid.width - 1, gridTop, grid.x + grid.width, gridTop + grid.height));
			MRMacroModelessGridView *gridView = nullptr;

			if (nextCommand >= cmMacroModelessMax) break;
			if (!grid.label.empty()) insert(new TStaticText(TRect(grid.x, grid.y, grid.x + strwidth(grid.label.c_str()), grid.y + 1), grid.label.c_str()));
			insert(scrollBar);
			gridView = new MRMacroModelessGridView(TRect(grid.x, gridTop, grid.x + grid.width - 1, gridTop + grid.height), scrollBar, items, nextCommand);
			insert(gridView);
			gridView->setItems(items, grid.start);
			commandToGrid[nextCommand] = grid.id;
			gridSpecs[grid.id] = grid;
			gridViews[grid.id] = gridView;
			if (selection.controlId == 0) selection = readSelection(grid.id);
			++nextCommand;
		}

		for (std::size_t buttonIndex = 0; buttonIndex < definition.buttons.size(); ++buttonIndex) {
			const MRMacroModelessButtonSpec &button = definition.buttons[buttonIndex];
			const MRMacroModelessButtonCaption caption = parseButtonCaption(button.text);

			if (nextCommand >= cmMacroModelessMax) break;
			commandToButton[nextCommand] = button.id;
			buttons[button.id] = button;
			for (std::size_t hotKeyIndex = 0; hotKeyIndex < caption.hotKeys.size(); ++hotKeyIndex)
				hotKeys.emplace_back(caption.hotKeys[hotKeyIndex], button.id);
			insert(new TButton(TRect(button.x, button.y, button.x + button.width, button.y + 2), caption.displayLabel.c_str(), nextCommand, bfNormal));
			++nextCommand;
		}
	}

	void refreshDisplays() {
		while (displayViews.size() > definition.displays.size()) {
			MRMacroModelessDisplayLine *displayLine = displayViews.back();

			displayViews.pop_back();
			if (displayLine == nullptr) continue;
			remove(displayLine);
			TObject::destroy(displayLine);
		}

		for (std::size_t index = 0; index < definition.displays.size(); ++index) {
			const MRMacroModelessDisplaySpec &display = definition.displays[index];
			TRect bounds(display.x, display.y, display.x + display.width, display.y + 1);

			if (index >= displayViews.size()) {
				MRMacroModelessDisplayLine *displayLine = new MRMacroModelessDisplayLine(bounds, display.text);

				insert(displayLine);
				displayViews.push_back(displayLine);
				continue;
			}
			if (displayViews[index] == nullptr) continue;
			if (displayViews[index]->getBounds() != bounds) displayViews[index]->locate(bounds);
			displayViews[index]->setText(display.text);
		}
	}

	MRMacroModelessSelection readSelection(int controlId) const {
		MRMacroModelessSelection result;
		std::map<int, MRMacroModelessListView *>::const_iterator it = listViews.find(controlId);
		std::map<int, MRMacroModelessGridView *>::const_iterator gridIt = gridViews.find(controlId);

		result.controlId = controlId;
		if (it != listViews.end() && it->second != nullptr) {
			result.index = std::max(0, it->second->focused + 1);
			if (result.index > 0 && static_cast<std::size_t>(result.index - 1) < it->second->values().size()) result.text = it->second->values()[static_cast<std::size_t>(result.index - 1)];
			return result;
		}
		if (gridIt == gridViews.end() || gridIt->second == nullptr) return result;
		result.index = gridIt->second->selectedIndexValue();
		result.text = gridIt->second->selectedText();
		return result;
	}

	void runButton(int buttonId) {
		std::map<int, MRMacroModelessButtonSpec>::const_iterator it = buttons.find(buttonId);
		const std::string windowId = definition.windowId;

		if (it == buttons.end()) return;
		if (selection.controlId != 0) selection = readSelection(selection.controlId);
		else if (!listViews.empty())
			selection = readSelection(listViews.begin()->first);
		else if (!gridViews.empty())
			selection = readSelection(gridViews.begin()->first);
		runModelessMacro(windowId, it->second.macroSpec);
	}

	void runModelessMacro(const std::string &windowId, const std::string &macroSpec) {
		std::map<std::string, MRMacroModelessWindow *>::const_iterator windowIt;

		if (g_commandRunner != nullptr) g_commandRunner(windowId, selection.controlId, selection, macroSpec);
		windowIt = g_windows.find(windowId);
		if (windowIt == g_windows.end() || windowIt->second != this) return;
		refreshLists();
	}

	MRMacroModelessWindowDefinition definition;
	std::map<ushort, int> commandToButton;
	std::map<ushort, int> commandToList;
	std::map<ushort, int> commandToGrid;
	std::map<int, MRMacroModelessButtonSpec> buttons;
	std::map<int, MRMacroModelessListBoxSpec> listSpecs;
	std::map<int, MRMacroModelessGridSpec> gridSpecs;
	std::map<int, MRMacroModelessListView *> listViews;
	std::map<int, MRMacroModelessGridView *> gridViews;
	std::vector<MRMacroModelessDisplayLine *> displayViews;
	std::vector<std::pair<ushort, int>> hotKeys;
	MRMacroModelessSelection selection;
};

class MRMacroModelessFrame final : public MRFrame {
  public:
	explicit MRMacroModelessFrame(const TRect &bounds) noexcept : MRFrame(bounds) {
	}

	void draw() override {
		TWindow *window = static_cast<TWindow *>(owner);
		const ushort savedFlags = window != nullptr ? window->flags : 0;
		const bool suppressGrowHandle = window != nullptr && (window->flags & wfGrow) != 0;

		if (suppressGrowHandle) window->flags &= static_cast<ushort>(~wfGrow);
		MRFrame::draw();
		if (suppressGrowHandle) window->flags = savedFlags;
	}

	void handleEvent(TEvent &event) override {
		MRMacroModelessWindow *window = dynamic_cast<MRMacroModelessWindow *>(owner);
		const TRect previousBounds = window != nullptr ? window->getBounds() : TRect(0, 0, 0, 0);

		MRFrame::handleEvent(event);
		if (window != nullptr && previousBounds != window->getBounds()) window->storeLiveGeometry();
	}
};

static TFrame *initMacroModelessFrame(TRect bounds) {
	return new MRMacroModelessFrame(bounds);
}

} // namespace

void setMacroModelessListResolver(MRMacroModelessListResolver resolver) {
	g_listResolver = resolver;
}

void setMacroModelessCommandRunner(MRMacroModelessCommandRunner runner) {
	g_commandRunner = runner;
}

bool showMacroModelessWindow(const MRMacroModelessWindowDefinition &definition) {
	if (TProgram::deskTop == nullptr || definition.windowId.empty()) return false;
	std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(definition.windowId);

	if (it != g_windows.end() && it->second != nullptr) {
		mrvmStoreModelessWindowDefinition(definition);
		it->second->updateDefinition(definition);
		it->second->select();
		return true;
	}

	MRMacroModelessWindow *window = new MRMacroModelessWindow(definition);
	if (window == nullptr) return false;
	mrvmStoreModelessWindowDefinition(definition);
	g_windows[definition.windowId] = window;
	TProgram::deskTop->insert(window);
	window->refreshLists();
	window->storeLiveGeometry();
	return true;
}

bool updateMacroModelessWindow(const MRMacroModelessWindowDefinition &definition) {
	if (TProgram::deskTop == nullptr || definition.windowId.empty()) return false;
	std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(definition.windowId);

	if (it == g_windows.end() || it->second == nullptr) return false;
	mrvmStoreModelessWindowDefinition(definition);
	it->second->updateDefinition(definition);
	return true;
}

bool updateMacroModelessDisplay(const std::string &windowId, int displayIndex, const std::string &text) {
	if (TProgram::deskTop == nullptr || windowId.empty() || displayIndex <= 0) return false;
	std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(windowId);

	if (it == g_windows.end() || it->second == nullptr) return false;
	if (!mrvmStoreModelessWindowDisplay(windowId, displayIndex, text)) return false;
	return it->second->updateDisplay(displayIndex, text);
}

bool closeMacroModelessWindow(const std::string &windowId) {
	std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(windowId);
	TEvent closeEvent{};

	if (it == g_windows.end() || it->second == nullptr) return false;
	closeEvent.what = evCommand;
	closeEvent.message.command = cmClose;
	closeEvent.message.infoPtr = it->second;
	it->second->putEvent(closeEvent);
	return true;
}

void refreshMacroModelessWindows() {
	for (std::map<std::string, MRMacroModelessWindow *>::iterator entry = g_windows.begin(); entry != g_windows.end(); ++entry)
		if (entry->second != nullptr) {
			entry->second->storeLiveGeometry();
			entry->second->refreshLists();
		}
}
