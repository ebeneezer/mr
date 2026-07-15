#include "MRMacroModelessControls.hpp"

#include "MRVMModelessUiRuntime.hpp"

#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TKeys
#define Uses_TPoint
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TView
#include <tvision/tv.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <utility>

namespace {

constexpr int kMaximumTreeNodes = 256;
constexpr int kMaximumTableColumns = 16;
constexpr int kMaximumTableRows = 512;

struct MacroUiTreeNode {
	std::string id;
	std::string parentId;
	std::string text;
	bool expanded = false;
};

struct MacroUiTableColumn {
	std::string title;
	int width = 8;
};

struct MacroUiTableRow {
	std::string id;
	std::vector<std::string> cells;
};

std::vector<std::string> splitTabs(const std::string &value) {
	std::vector<std::string> fields;
	std::size_t begin = 0;

	for (;;) {
		const std::size_t tab = value.find('\t', begin);

		fields.push_back(value.substr(begin, tab == std::string::npos ? tab : tab - begin));
		if (tab == std::string::npos) return fields;
		begin = tab + 1;
	}
}

bool parsePositiveInt(const std::string &value, int &result) {
	char *end = nullptr;
	long parsed = 0;

	result = 0;
	if (value.empty()) return false;
	parsed = std::strtol(value.c_str(), &end, 10);
	if (end == nullptr || *end != EOS || parsed < 1 || parsed > 80) return false;
	result = static_cast<int>(parsed);
	return true;
}

bool parseTreeItems(const std::vector<std::string> &values, std::vector<MacroUiTreeNode> &nodes) {
	std::map<std::string, int> knownIds;

	nodes.clear();
	for (std::size_t index = 0; index < values.size(); ++index) {
		const std::vector<std::string> fields = splitTabs(values[index]);
		MacroUiTreeNode node;

		if (fields.size() != 5 || fields[0] != "TREE" || fields[1].empty() || fields[3].empty() || (fields[4] != "0" && fields[4] != "1")) return false;
		if (knownIds.find(fields[1]) != knownIds.end()) return false;
		if (!fields[2].empty() && knownIds.find(fields[2]) == knownIds.end()) return false;
		if (nodes.size() >= kMaximumTreeNodes) return false;
		node.id = fields[1];
		node.parentId = fields[2];
		node.text = fields[3];
		node.expanded = fields[4] == "1";
		knownIds[node.id] = static_cast<int>(nodes.size());
		nodes.push_back(node);
	}
	return true;
}

bool parseTableItems(const std::vector<std::string> &values, std::vector<MacroUiTableColumn> &columns, std::vector<MacroUiTableRow> &rows) {
	std::map<std::string, int> rowIds;
	bool rowsStarted = false;

	columns.clear();
	rows.clear();
	for (std::size_t index = 0; index < values.size(); ++index) {
		const std::vector<std::string> fields = splitTabs(values[index]);
		const std::size_t firstTab = values[index].find('\t');
		const std::size_t secondTab = firstTab == std::string::npos ? std::string::npos : values[index].find('\t', firstTab + 1);

		if (fields[0] == "TABLE_COLUMN") {
			MacroUiTableColumn column;

			if (rowsStarted || fields.size() != 3 || fields[1].empty() || !parsePositiveInt(fields[2], column.width) || columns.size() >= kMaximumTableColumns) return false;
			column.title = fields[1];
			columns.push_back(column);
			continue;
		}
		if (fields[0] != "TABLE_ROW" || firstTab == std::string::npos || secondTab == std::string::npos || columns.empty()) return false;
		MacroUiTableRow row;

		row.id = values[index].substr(firstTab + 1, secondTab - firstTab - 1);
		if (row.id.empty() || rowIds.find(row.id) != rowIds.end() || rows.size() >= kMaximumTableRows) return false;
		row.cells = splitTabs(values[index].substr(secondTab + 1));
		if (row.cells.size() != columns.size()) return false;
		rowIds[row.id] = static_cast<int>(rows.size());
		rows.push_back(row);
		rowsStarted = true;
	}
	return !columns.empty();
}

class MRMacroUiTreeView final : public TView {
  public:
	MRMacroUiTreeView(const TRect &bounds, TScrollBar *ownerScrollBar, std::vector<std::string> values, ushort command, std::string ownerWindowId, int ownerControlId) : TView(bounds), scrollBar(ownerScrollBar), activateCommand(command), windowId(std::move(ownerWindowId)), controlId(ownerControlId) {
		options |= ofSelectable;
		eventMask |= evMouseDown | evMouseWheel | evKeyDown | evBroadcast;
		setItems(std::move(values), 1);
	}

	void draw() override {
		const TColorAttr normal = getColor(1);
		const TColorAttr selected = getColor(3);

		updateScrollBar();
		for (short row = 0; row < size.y; ++row) drawBlankRow(row, normal);
		for (int row = 0; row < visibleRows(); ++row) {
			const int visibleIndex = scrollOffset + row;

			if (visibleIndex < 0 || static_cast<std::size_t>(visibleIndex) >= visibleNodeIndices.size()) break;
			drawNode(static_cast<short>(row), visibleNodeIndices[static_cast<std::size_t>(visibleIndex)], visibleIndex == selectedIndex ? selected : normal);
		}
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evMouseDown && containsMouse(event)) {
			const TPoint point = makeLocal(event.mouse.where);
			const int visibleIndex = scrollOffset + point.y;

			if (visibleIndex >= 0 && static_cast<std::size_t>(visibleIndex) < visibleNodeIndices.size()) {
				selectedIndex = visibleIndex;
				if (point.x <= nodeToggleColumn(visibleNodeIndices[static_cast<std::size_t>(visibleIndex)])) toggleSelectedNode();
				else if ((event.mouse.eventFlags & meDoubleClick) != 0 && owner != nullptr) message(owner, evCommand, activateCommand, this);
				storeSelection();
				drawView();
			}
			clearEvent(event);
			return;
		}
		if (event.what == evMouseWheel && containsMouse(event)) {
			setScrollOffset(scrollOffset + (event.mouse.wheel == mwUp || event.mouse.wheel == mwLeft ? -1 : 1));
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown) {
			const ushort key = ctrlToArrow(event.keyDown.keyCode);

			if (moveSelection(key)) {
				clearEvent(event);
				return;
			}
			if (key == kbEnter && owner != nullptr) {
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
		if (!parseTreeItems(values, nodes)) nodes.clear();
		restoreExpansionState();
		rebuildVisibleNodes();
		selectedIndex = selectedIndexFromState();
		if (selectedIndex < 0) selectedIndex = visibleNodeIndices.empty() ? 0 : std::clamp(start, 1, static_cast<int>(visibleNodeIndices.size())) - 1;
		ensureSelectedVisible();
		storeSelection();
		drawView();
	}

	void refreshItems(std::vector<std::string> values) {
		const std::string savedNodeId = selectedText();
		const int savedScrollOffset = scrollOffset;

		if (!parseTreeItems(values, nodes)) nodes.clear();
		restoreExpansionState();
		rebuildVisibleNodes();
		selectedIndex = selectedIndexFromState();
		if (selectedIndex < 0) selectedIndex = visibleIndexForNode(savedNodeId);
		if (selectedIndex < 0) selectedIndex = visibleNodeIndices.empty() ? 0 : std::min(savedScrollOffset, static_cast<int>(visibleNodeIndices.size()) - 1);
		scrollOffset = std::max(0, std::min(savedScrollOffset, maxScrollOffset()));
		ensureSelectedVisible();
		storeSelection();
		drawView();
	}

	int selectedIndexValue() const noexcept {
		return visibleNodeIndices.empty() ? 0 : selectedIndex + 1;
	}

	std::string selectedText() const {
		if (selectedIndex < 0 || static_cast<std::size_t>(selectedIndex) >= visibleNodeIndices.size()) return std::string();
		return nodes[static_cast<std::size_t>(visibleNodeIndices[static_cast<std::size_t>(selectedIndex)])].id;
	}

  private:
	void drawBlankRow(short row, TColorAttr attr) {
		TDrawBuffer buffer;

		buffer.moveChar(0, ' ', attr, size.x);
		writeLine(0, row, size.x, 1, buffer);
	}

	void drawNode(short row, int nodeIndex, TColorAttr attr) {
		TDrawBuffer buffer;
		const MacroUiTreeNode &node = nodes[static_cast<std::size_t>(nodeIndex)];
		const int depth = nodeDepth(nodeIndex);
		const bool hasChildren = nodeHasChildren(nodeIndex);
		const int textColumn = std::min(static_cast<int>(size.x), depth * 2 + 2);

		buffer.moveChar(0, ' ', attr, size.x);
		if (textColumn > 0 && hasChildren) buffer.moveChar(static_cast<ushort>(textColumn - 2), node.expanded ? '-' : '+', attr, 1);
		if (textColumn < size.x) buffer.moveStr(static_cast<ushort>(textColumn), node.text.c_str(), attr, static_cast<ushort>(size.x - textColumn));
		writeLine(0, row, size.x, 1, buffer);
	}

	bool moveSelection(ushort key) {
		if (visibleNodeIndices.empty()) return false;
		const int oldIndex = selectedIndex;

		switch (key) {
			case kbUp:
				selectedIndex = std::max(0, selectedIndex - 1);
				break;
			case kbDown:
				selectedIndex = std::min(static_cast<int>(visibleNodeIndices.size()) - 1, selectedIndex + 1);
				break;
			case kbHome:
				selectedIndex = 0;
				break;
			case kbEnd:
				selectedIndex = static_cast<int>(visibleNodeIndices.size()) - 1;
				break;
			case kbPgUp:
				selectedIndex = std::max(0, selectedIndex - visibleRows());
				break;
			case kbPgDn:
				selectedIndex = std::min(static_cast<int>(visibleNodeIndices.size()) - 1, selectedIndex + visibleRows());
				break;
			case kbLeft:
				collapseOrSelectParent();
				break;
			case kbRight:
				expandOrSelectChild();
				break;
			default:
				return false;
		}
		ensureSelectedVisible();
		storeSelection();
		if (selectedIndex != oldIndex || key == kbLeft || key == kbRight) drawView();
		return true;
	}

	void collapseOrSelectParent() {
		const int nodeIndex = visibleNodeIndices[static_cast<std::size_t>(selectedIndex)];
		MacroUiTreeNode &node = nodes[static_cast<std::size_t>(nodeIndex)];

		if (node.expanded && nodeHasChildren(nodeIndex)) {
			node.expanded = false;
			storeExpansion(nodeIndex);
			rebuildVisibleNodes();
			return;
		}
		const int parentIndex = nodeIndexForId(node.parentId);
		const int parentVisibleIndex = visibleIndexForNode(parentIndex >= 0 ? nodes[static_cast<std::size_t>(parentIndex)].id : std::string());

		if (parentVisibleIndex >= 0) selectedIndex = parentVisibleIndex;
	}

	void expandOrSelectChild() {
		const int nodeIndex = visibleNodeIndices[static_cast<std::size_t>(selectedIndex)];
		MacroUiTreeNode &node = nodes[static_cast<std::size_t>(nodeIndex)];

		if (!nodeHasChildren(nodeIndex)) return;
		if (!node.expanded) {
			node.expanded = true;
			storeExpansion(nodeIndex);
			rebuildVisibleNodes();
			return;
		}
		for (std::size_t index = 0; index < nodes.size(); ++index)
			if (nodes[index].parentId == node.id) {
				const int childVisibleIndex = visibleIndexForNode(nodes[index].id);

				if (childVisibleIndex >= 0) selectedIndex = childVisibleIndex;
				return;
			}
	}

	void toggleSelectedNode() {
		if (visibleNodeIndices.empty()) return;
		const int nodeIndex = visibleNodeIndices[static_cast<std::size_t>(selectedIndex)];

		if (!nodeHasChildren(nodeIndex)) return;
		nodes[static_cast<std::size_t>(nodeIndex)].expanded = !nodes[static_cast<std::size_t>(nodeIndex)].expanded;
		storeExpansion(nodeIndex);
		rebuildVisibleNodes();
		selectedIndex = std::max(0, visibleIndexForNode(nodes[static_cast<std::size_t>(nodeIndex)].id));
		ensureSelectedVisible();
	}

	void rebuildVisibleNodes() {
		visibleNodeIndices.clear();
		for (std::size_t index = 0; index < nodes.size(); ++index)
			if (nodeAncestorsExpanded(static_cast<int>(index))) visibleNodeIndices.push_back(static_cast<int>(index));
	}

	bool nodeAncestorsExpanded(int nodeIndex) const {
		int parentIndex = nodeIndexForId(nodes[static_cast<std::size_t>(nodeIndex)].parentId);

		while (parentIndex >= 0) {
			if (!nodes[static_cast<std::size_t>(parentIndex)].expanded) return false;
			parentIndex = nodeIndexForId(nodes[static_cast<std::size_t>(parentIndex)].parentId);
		}
		return true;
	}

	int nodeIndexForId(const std::string &id) const {
		for (std::size_t index = 0; index < nodes.size(); ++index)
			if (nodes[index].id == id) return static_cast<int>(index);
		return -1;
	}

	int visibleIndexForNode(const std::string &id) const {
		for (std::size_t index = 0; index < visibleNodeIndices.size(); ++index)
			if (nodes[static_cast<std::size_t>(visibleNodeIndices[index])].id == id) return static_cast<int>(index);
		return -1;
	}

	int selectedIndexFromState() const {
		std::string nodeId;

		if (windowId.empty() || !mrvmReadModelessWindowTreeSelection(windowId, controlId, nodeId)) return -1;
		return visibleIndexForNode(nodeId);
	}

	void restoreExpansionState() {
		if (windowId.empty()) return;
		for (std::size_t index = 0; index < nodes.size(); ++index) {
			bool expanded = false;

			if (mrvmReadModelessWindowTreeExpansion(windowId, controlId, nodes[index].id, expanded)) nodes[index].expanded = expanded;
		}
	}

	void storeSelection() const {
		if (!windowId.empty() && !selectedText().empty()) mrvmStoreModelessWindowTreeSelection(windowId, controlId, selectedText());
	}

	void storeExpansion(int nodeIndex) const {
		const MacroUiTreeNode &node = nodes[static_cast<std::size_t>(nodeIndex)];

		if (!windowId.empty()) mrvmStoreModelessWindowTreeExpansion(windowId, controlId, node.id, node.expanded);
	}

	int nodeDepth(int nodeIndex) const {
		int depth = 0;
		int parentIndex = nodeIndexForId(nodes[static_cast<std::size_t>(nodeIndex)].parentId);

		while (parentIndex >= 0) {
			++depth;
			parentIndex = nodeIndexForId(nodes[static_cast<std::size_t>(parentIndex)].parentId);
		}
		return depth;
	}

	int nodeToggleColumn(int nodeIndex) const {
		return std::min(static_cast<int>(size.x) - 1, nodeDepth(nodeIndex) * 2);
	}

	bool nodeHasChildren(int nodeIndex) const {
		const std::string &id = nodes[static_cast<std::size_t>(nodeIndex)].id;

		for (std::size_t index = 0; index < nodes.size(); ++index)
			if (nodes[index].parentId == id) return true;
		return false;
	}

	int visibleRows() const noexcept {
		return std::max(1, static_cast<int>(size.y));
	}

	int maxScrollOffset() const noexcept {
		return std::max(0, static_cast<int>(visibleNodeIndices.size()) - visibleRows());
	}

	void setScrollOffset(int value) {
		const int clamped = std::max(0, std::min(value, maxScrollOffset()));

		if (clamped == scrollOffset) return;
		scrollOffset = clamped;
		updateScrollBar();
		drawView();
	}

	void updateScrollBar() {
		if (scrollBar == nullptr) return;
		if (maxScrollOffset() == 0) {
			scrollBar->hide();
			return;
		}
		scrollBar->show();
		scrollBar->setParams(scrollOffset, 0, maxScrollOffset(), visibleRows(), 1);
	}

	void ensureSelectedVisible() {
		if (visibleNodeIndices.empty()) {
			scrollOffset = 0;
			return;
		}
		if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
		else if (selectedIndex >= scrollOffset + visibleRows())
			scrollOffset = selectedIndex - visibleRows() + 1;
		scrollOffset = std::max(0, std::min(scrollOffset, maxScrollOffset()));
		updateScrollBar();
	}

	std::vector<MacroUiTreeNode> nodes;
	std::vector<int> visibleNodeIndices;
	TScrollBar *scrollBar = nullptr;
	ushort activateCommand = 0;
	std::string windowId;
	int controlId = 0;
	int selectedIndex = 0;
	int scrollOffset = 0;
};

class MRMacroUiTableView final : public TView {
  public:
	MRMacroUiTableView(const TRect &bounds, TScrollBar *ownerScrollBar, std::vector<std::string> values, ushort command, std::string ownerWindowId, int ownerControlId) : TView(bounds), scrollBar(ownerScrollBar), activateCommand(command), windowId(std::move(ownerWindowId)), controlId(ownerControlId) {
		options |= ofSelectable;
		eventMask |= evMouseDown | evMouseWheel | evKeyDown | evBroadcast;
		setItems(std::move(values), 1);
	}

	void draw() override {
		const TColorAttr normal = getColor(1);
		const TColorAttr heading = getColor(2);
		const TColorAttr selected = getColor(3);

		updateScrollBar();
		for (short row = 0; row < size.y; ++row) drawBlankRow(row, normal);
		if (size.y > 0) drawHeader(heading);
		for (int row = 0; row < visibleRows(); ++row) {
			const int itemIndex = scrollOffset + row;

			if (itemIndex < 0 || static_cast<std::size_t>(itemIndex) >= rows.size()) break;
			drawCells(static_cast<short>(row + 1), rows[static_cast<std::size_t>(itemIndex)].cells, itemIndex == selectedIndex ? selected : normal);
		}
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evMouseDown && containsMouse(event)) {
			const TPoint point = makeLocal(event.mouse.where);
			const int itemIndex = scrollOffset + point.y - 1;

			if (point.y > 0 && itemIndex >= 0 && static_cast<std::size_t>(itemIndex) < rows.size()) {
				selectedIndex = itemIndex;
				storeSelection();
				drawView();
				if ((event.mouse.eventFlags & meDoubleClick) != 0 && owner != nullptr) message(owner, evCommand, activateCommand, this);
			}
			clearEvent(event);
			return;
		}
		if (event.what == evMouseWheel && containsMouse(event)) {
			setScrollOffset(scrollOffset + (event.mouse.wheel == mwUp || event.mouse.wheel == mwLeft ? -1 : 1));
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown) {
			const ushort key = ctrlToArrow(event.keyDown.keyCode);

			if (moveSelection(key)) {
				clearEvent(event);
				return;
			}
			if (key == kbEnter && owner != nullptr) {
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
		if (!parseTableItems(values, columns, rows)) {
			columns.clear();
			rows.clear();
		}
		selectedIndex = selectedIndexFromState();
		if (selectedIndex < 0) selectedIndex = rows.empty() ? 0 : std::clamp(start, 1, static_cast<int>(rows.size())) - 1;
		ensureSelectedVisible();
		storeSelection();
		drawView();
	}

	void refreshItems(std::vector<std::string> values) {
		const std::string savedRowId = selectedText();
		const int savedScrollOffset = scrollOffset;

		if (!parseTableItems(values, columns, rows)) {
			columns.clear();
			rows.clear();
		}
		selectedIndex = selectedIndexFromState();
		if (selectedIndex < 0) selectedIndex = rowIndexForId(savedRowId);
		if (selectedIndex < 0) selectedIndex = rows.empty() ? 0 : std::min(savedScrollOffset, static_cast<int>(rows.size()) - 1);
		scrollOffset = std::max(0, std::min(savedScrollOffset, maxScrollOffset()));
		ensureSelectedVisible();
		storeSelection();
		drawView();
	}

	int selectedIndexValue() const noexcept {
		return rows.empty() ? 0 : selectedIndex + 1;
	}

	std::string selectedText() const {
		return selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < rows.size() ? rows[static_cast<std::size_t>(selectedIndex)].id : std::string();
	}

  private:
	void drawBlankRow(short row, TColorAttr attr) {
		TDrawBuffer buffer;

		buffer.moveChar(0, ' ', attr, size.x);
		writeLine(0, row, size.x, 1, buffer);
	}

	void drawHeader(TColorAttr attr) {
		TDrawBuffer buffer;
		int column = 0;

		buffer.moveChar(0, ' ', attr, size.x);
		for (std::size_t index = 0; index < columns.size() && column < size.x; ++index) {
			const int width = std::min(columns[index].width, static_cast<int>(size.x) - column);

			buffer.moveStr(static_cast<ushort>(column), columns[index].title.c_str(), attr, static_cast<ushort>(width));
			column += columns[index].width;
			if (column < size.x) buffer.moveChar(static_cast<ushort>(column++), '|', attr, 1);
		}
		writeLine(0, 0, size.x, 1, buffer);
	}

	void drawCells(short row, const std::vector<std::string> &cells, TColorAttr attr) {
		TDrawBuffer buffer;
		int column = 0;

		buffer.moveChar(0, ' ', attr, size.x);
		for (std::size_t index = 0; index < columns.size() && index < cells.size() && column < size.x; ++index) {
			const int width = std::min(columns[index].width, static_cast<int>(size.x) - column);

			buffer.moveStr(static_cast<ushort>(column), cells[index].c_str(), attr, static_cast<ushort>(width));
			column += columns[index].width;
			if (column < size.x) buffer.moveChar(static_cast<ushort>(column++), '|', attr, 1);
		}
		writeLine(0, row, size.x, 1, buffer);
	}

	bool moveSelection(ushort key) {
		if (rows.empty()) return false;
		const int oldIndex = selectedIndex;

		switch (key) {
			case kbUp:
				selectedIndex = std::max(0, selectedIndex - 1);
				break;
			case kbDown:
				selectedIndex = std::min(static_cast<int>(rows.size()) - 1, selectedIndex + 1);
				break;
			case kbHome:
				selectedIndex = 0;
				break;
			case kbEnd:
				selectedIndex = static_cast<int>(rows.size()) - 1;
				break;
			case kbPgUp:
				selectedIndex = std::max(0, selectedIndex - visibleRows());
				break;
			case kbPgDn:
				selectedIndex = std::min(static_cast<int>(rows.size()) - 1, selectedIndex + visibleRows());
				break;
			default:
				return false;
		}
		ensureSelectedVisible();
		storeSelection();
		if (selectedIndex != oldIndex) drawView();
		return true;
	}

	int rowIndexForId(const std::string &id) const {
		for (std::size_t index = 0; index < rows.size(); ++index)
			if (rows[index].id == id) return static_cast<int>(index);
		return -1;
	}

	int selectedIndexFromState() const {
		std::string rowId;

		if (windowId.empty() || !mrvmReadModelessWindowTableSelection(windowId, controlId, rowId)) return -1;
		return rowIndexForId(rowId);
	}

	void storeSelection() const {
		if (!windowId.empty() && !selectedText().empty()) mrvmStoreModelessWindowTableSelection(windowId, controlId, selectedText());
	}

	int visibleRows() const noexcept {
		return std::max(1, static_cast<int>(size.y) - 1);
	}

	int maxScrollOffset() const noexcept {
		return std::max(0, static_cast<int>(rows.size()) - visibleRows());
	}

	void setScrollOffset(int value) {
		const int clamped = std::max(0, std::min(value, maxScrollOffset()));

		if (clamped == scrollOffset) return;
		scrollOffset = clamped;
		updateScrollBar();
		drawView();
	}

	void updateScrollBar() {
		if (scrollBar == nullptr) return;
		if (maxScrollOffset() == 0) {
			scrollBar->hide();
			return;
		}
		scrollBar->show();
		scrollBar->setParams(scrollOffset, 0, maxScrollOffset(), visibleRows(), 1);
	}

	void ensureSelectedVisible() {
		if (rows.empty()) {
			scrollOffset = 0;
			return;
		}
		if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
		else if (selectedIndex >= scrollOffset + visibleRows())
			scrollOffset = selectedIndex - visibleRows() + 1;
		scrollOffset = std::max(0, std::min(scrollOffset, maxScrollOffset()));
		updateScrollBar();
	}

	std::vector<MacroUiTableColumn> columns;
	std::vector<MacroUiTableRow> rows;
	TScrollBar *scrollBar = nullptr;
	ushort activateCommand = 0;
	std::string windowId;
	int controlId = 0;
	int selectedIndex = 0;
	int scrollOffset = 0;
};

} // namespace

std::string macroUiTreeNodeItem(const std::string &nodeId, const std::string &parentId, const std::string &text, bool expanded) {
	return std::string("TREE\t") + nodeId + "\t" + parentId + "\t" + text + "\t" + (expanded ? "1" : "0");
}

bool macroUiTreeItemsValid(const std::vector<std::string> &values) {
	std::vector<MacroUiTreeNode> nodes;

	return parseTreeItems(values, nodes);
}

TView *createMacroUiTreeView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> values, unsigned short command, const std::string &windowId, int controlId) {
	return new MRMacroUiTreeView(bounds, scrollBar, std::move(values), static_cast<ushort>(command), windowId, controlId);
}

void setMacroUiTreeItems(TView *view, std::vector<std::string> values, int start) {
	MRMacroUiTreeView *treeView = dynamic_cast<MRMacroUiTreeView *>(view);

	if (treeView != nullptr) treeView->setItems(std::move(values), start);
}

void refreshMacroUiTreeItems(TView *view, std::vector<std::string> values) {
	MRMacroUiTreeView *treeView = dynamic_cast<MRMacroUiTreeView *>(view);

	if (treeView != nullptr) treeView->refreshItems(std::move(values));
}

int macroUiTreeSelectedIndex(const TView *view) {
	const MRMacroUiTreeView *treeView = dynamic_cast<const MRMacroUiTreeView *>(view);

	return treeView != nullptr ? treeView->selectedIndexValue() : 0;
}

std::string macroUiTreeSelectedText(const TView *view) {
	const MRMacroUiTreeView *treeView = dynamic_cast<const MRMacroUiTreeView *>(view);

	return treeView != nullptr ? treeView->selectedText() : std::string();
}

std::string macroUiTableColumnItem(const std::string &title, int width) {
	return std::string("TABLE_COLUMN\t") + title + "\t" + std::to_string(width);
}

std::string macroUiTableRowItem(const std::string &rowId, const std::string &cells) {
	return std::string("TABLE_ROW\t") + rowId + "\t" + cells;
}

bool macroUiTableItemsValid(const std::vector<std::string> &values) {
	std::vector<MacroUiTableColumn> columns;
	std::vector<MacroUiTableRow> rows;

	return parseTableItems(values, columns, rows);
}

TView *createMacroUiTableView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> values, unsigned short command, const std::string &windowId, int controlId) {
	return new MRMacroUiTableView(bounds, scrollBar, std::move(values), static_cast<ushort>(command), windowId, controlId);
}

void setMacroUiTableItems(TView *view, std::vector<std::string> values, int start) {
	MRMacroUiTableView *tableView = dynamic_cast<MRMacroUiTableView *>(view);

	if (tableView != nullptr) tableView->setItems(std::move(values), start);
}

void refreshMacroUiTableItems(TView *view, std::vector<std::string> values) {
	MRMacroUiTableView *tableView = dynamic_cast<MRMacroUiTableView *>(view);

	if (tableView != nullptr) tableView->refreshItems(std::move(values));
}

int macroUiTableSelectedIndex(const TView *view) {
	const MRMacroUiTableView *tableView = dynamic_cast<const MRMacroUiTableView *>(view);

	return tableView != nullptr ? tableView->selectedIndexValue() : 0;
}

std::string macroUiTableSelectedText(const TView *view) {
	const MRMacroUiTableView *tableView = dynamic_cast<const MRMacroUiTableView *>(view);

	return tableView != nullptr ? tableView->selectedText() : std::string();
}
