#include "MRVMMacroDialogRuntime.hpp"

#include "../modeless/MRMacroModelessControls.hpp"
#include "../modeless/MRVMModelessUiRuntime.hpp"
#include "MRVMScreen.hpp"
#include "../../vm/MRVMValue.hpp"

#include "../../../app/utils/MRStringUtils.hpp"
#include "../../../dialogs/setup/MRSetupCommon.hpp"
#include "../../../ui/MRWindowSupport.hpp"
#include "../../../app/MRHelpTopics.generated.hpp"

#define Uses_TButton
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TLabel
#define Uses_TListViewer
#define Uses_TObject
#define Uses_TProgram
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TView
#include <tvision/tv.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <stdexcept>
#include <utility>

namespace {
using Value = VirtualMachine::Value;

struct MacroMenuRequest {
	int x = 0;
	int y = 0;
	int start = 1;
	std::string title;
	std::string menuSpec;
	bool horizontal = false;
};

struct MacroStringInputRequest {
	int x = 0;
	int y = 0;
	int width = 40;
	std::string title;
	std::string initialValue;
};

struct MacroUiButtonCaption {
	std::string displayLabel;
	std::vector<ushort> hotKeys;
};

ushort macroNamedHotKeyCode(const std::string &name) noexcept {
	struct NamedKey {
		const char *name;
		ushort code;
	};
	static const NamedKey kNamedKeys[] = {
	    {"ENTER", kbEnter}, {"ESC", kbEsc}, {"ESCAPE", kbEsc}, {"TAB", kbTab}, {"F1", kbF1}, {"F2", kbF2}, {"F3", kbF3}, {"F4", kbF4}, {"F5", kbF5}, {"F6", kbF6}, {"F7", kbF7}, {"F8", kbF8}, {"F9", kbF9}, {"F10", kbF10}, {"F11", kbF11}, {"F12", kbF12},
	};

	for (const NamedKey &entry : kNamedKeys)
		if (mrvmUpperKey(name) == entry.name) return entry.code;
	return 0;
}

MacroUiButtonCaption parseMacroUiButtonCaption(const std::string &text) {
	MacroUiButtonCaption entry;
	std::size_t index = 0;

	const std::string label = trimAscii(text);
	while (index < label.size()) {
		if (label[index] == '~') {
			const std::size_t close = label.find('~', index + 1);
			if (close != std::string::npos && close > index + 1) {
				entry.displayLabel += label.substr(index + 1, close - index - 1);
				const char hotChar = static_cast<char>(std::toupper(static_cast<unsigned char>(label[index + 1])));
				entry.hotKeys.push_back(static_cast<ushort>(hotChar));
				index = close + 1;
				continue;
			}
		}
		if (label[index] == '<') {
			const std::size_t close = label.find('>', index + 1);
			if (close != std::string::npos) {
				const ushort keyCode = macroNamedHotKeyCode(trimAscii(label.substr(index + 1, close - index - 1)));
				if (keyCode != 0) entry.hotKeys.push_back(keyCode);
				index = close + 1;
				continue;
			}
		}
		entry.displayLabel.push_back(label[index]);
		++index;
	}
	if (entry.displayLabel.size() == 1) {
		const char hotChar = static_cast<char>(std::toupper(static_cast<unsigned char>(entry.displayLabel.front())));
		entry.hotKeys.push_back(static_cast<ushort>(hotChar));
	}
	return entry;
}

std::vector<std::string> parseMacroMenuItems(const std::string &menuSpec) {
	std::vector<std::string> items;
	std::size_t pos = 0;
	std::size_t last = 0;

	while (pos < menuSpec.size()) {
		std::size_t openPos = menuSpec.find('(', pos);
		if (openPos == std::string::npos) break;
		std::string item = trimAscii(menuSpec.substr(last, openPos - last));
		std::size_t closePos = menuSpec.find(')', openPos + 1);
		if (!item.empty()) items.push_back(item);
		if (closePos == std::string::npos) return items;
		pos = closePos + 1;
		last = pos;
	}
	if (items.empty()) {
		std::size_t start = 0;
		while (start <= menuSpec.size()) {
			std::size_t sep = menuSpec.find('|', start);
			std::string item = trimAscii(menuSpec.substr(start, sep == std::string::npos ? sep : sep - start));
			if (!item.empty()) items.push_back(item);
			if (sep == std::string::npos) break;
			start = sep + 1;
		}
	}
	return items;
}

int macroMenuDialogWidth(const MacroMenuRequest &request) noexcept {
	return std::max(36, std::min(72, static_cast<int>(request.menuSpec.size()) + 8));
}

int macroMenuDialogHeight(const MacroMenuRequest &request) {
	return std::min(22, std::max(10, static_cast<int>(parseMacroMenuItems(request.menuSpec).size()) + 7));
}

TRect macroDialogBounds(int width, int height, int x, int y) {
	TRect desk = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, 80, 25);
	int dialogWidth = std::min(width, desk.b.x - desk.a.x - 2);
	int dialogHeight = std::min(height, desk.b.y - desk.a.y - 2);
	int left = desk.a.x + (desk.b.x - desk.a.x - dialogWidth) / 2;
	int top = desk.a.y + (desk.b.y - desk.a.y - dialogHeight) / 2;

	if (x > 0) left = std::clamp(desk.a.x + x - 1, desk.a.x, desk.b.x - dialogWidth);
	if (y > 0) top = std::clamp(desk.a.y + y - 1, desk.a.y, desk.b.y - dialogHeight);
	return TRect(left, top, left + dialogWidth, top + dialogHeight);
}

class MacroMenuListView final : public TListViewer {
  public:
	MacroMenuListView(const TRect &bounds, TScrollBar *scrollBar, const std::vector<std::string> &menuItems) noexcept : TListViewer(bounds, 1, nullptr, scrollBar), items(menuItems) {
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
			message(owner, evCommand, cmOK, this);
			clearEvent(event);
		}
	}

  private:
	std::vector<std::string> items;
};

int runMacroMenuDialog(const MacroMenuRequest &request) {
	class MacroMenuDialog final : public MRDialogFoundation {
	  public:
		MacroMenuDialog(const MacroMenuRequest &menuRequest) : TWindowInit(&TDialog::initFrame), MRDialogFoundation(macroDialogBounds(macroMenuDialogWidth(menuRequest), macroMenuDialogHeight(menuRequest), menuRequest.x, menuRequest.y), menuRequest.title.empty() ? (menuRequest.horizontal ? "BAR MENU" : "V MENU") : menuRequest.title.c_str(), macroMenuDialogWidth(menuRequest), macroMenuDialogHeight(menuRequest)), menuRequestItems(parseMacroMenuItems(menuRequest.menuSpec)) {
			int width = size.x;
			int height = size.y;
			const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
			const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 1);

			helpCtx = hcDialogMacroMenu;
			scrollBar = new TScrollBar(TRect(width - 3, 2, width - 2, height - 4));
			insert(scrollBar);
			listView = new MacroMenuListView(TRect(2, 2, width - 3, height - 4), scrollBar, menuRequestItems);
			insert(listView);
			mr::dialogs::insertUniformButtonRow(*this, (width - metrics.rowWidth) / 2, height - 3, 1, buttons);
			if (!menuRequestItems.empty()) {
				int index = std::clamp(menuRequest.start, 1, static_cast<int>(menuRequestItems.size())) - 1;
				listView->focusItemNum(static_cast<short>(index));
			}
		}

		int selectedIndex() const noexcept {
			if (listView == nullptr || listView->focused < 0) return 0;
			return listView->focused + 1;
		}

	  private:
		std::vector<std::string> menuRequestItems;
		TScrollBar *scrollBar = nullptr;
		MacroMenuListView *listView = nullptr;
	};

	MacroMenuDialog *dialog = new MacroMenuDialog(request);
	ushort result = cmCancel;
	int selected = 0;

	if (dialog == nullptr) return 0;
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	selected = result == cmOK ? dialog->selectedIndex() : 0;
	TObject::destroy(dialog);
	return selected;
}

std::string runMacroStringInputDialog(const MacroStringInputRequest &request) {
	class MacroStringInputDialog final : public MRDialogFoundation {
	  public:
		MacroStringInputDialog(const MacroStringInputRequest &inputRequest) : TWindowInit(&TDialog::initFrame), MRDialogFoundation(macroDialogBounds(std::max(34, inputRequest.width + 10), 9, inputRequest.x, inputRequest.y), inputRequest.title.empty() ? "STRING INPUT" : inputRequest.title.c_str(), std::max(34, inputRequest.width + 10), 9) {
			int width = size.x;
			char *buffer = newStr(inputRequest.initialValue.c_str());
			const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
			const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 1);

			helpCtx = hcDialogMacroStringInput;
			inputLine = new TInputLine(TRect(13, 2, width - 3, 3), std::max(1, inputRequest.width));
			insert(new TLabel(TRect(2, 2, 12, 3), "Value:", inputLine));
			insert(inputLine);
			inputLine->setData(buffer);
			delete[] buffer;
			mr::dialogs::insertUniformButtonRow(*this, (width - metrics.rowWidth) / 2, 6, 1, buttons);
			selectNext(False);
		}

		std::string value() const {
			char buffer[512] = {0};
			if (inputLine != nullptr) inputLine->getData(buffer);
			return std::string(buffer);
		}

	  private:
		TInputLine *inputLine = nullptr;
	};

	MacroStringInputDialog *dialog = new MacroStringInputDialog(request);
	ushort result = cmCancel;
	std::string value;

	if (dialog == nullptr) return std::string();
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	value = result == cmOK ? dialog->value() : std::string();
	TObject::destroy(dialog);
	return value;
}


class MacroUiDisplayLine final : public TView {
  public:
	MacroUiDisplayLine(const TRect &bounds, std::string text) noexcept : TView(bounds), text(std::move(text)) {
		options &= ~(ofSelectable | ofFirstClick);
		eventMask &= static_cast<ushort>(~(evMouseDown | evMouseUp | evMouseMove | evKeyDown));
	}

	void draw() override {
		TDrawBuffer buffer;
		TColorAttr configuredAttr;
		TColorAttr color = getColor(1);
		const int width = size.x;
		const std::string value = text.empty() ? std::string("0") : text;
		const int start = std::max(0, width - static_cast<int>(value.size()));

		if (configuredColorSlotOverride(9, configuredAttr)) color = configuredAttr;
		buffer.moveChar(0, ' ', color, static_cast<ushort>(width));
		if (start < width) buffer.moveStr(static_cast<ushort>(start), value.c_str(), color, width - start);
		writeLine(0, 0, width, 1, buffer);
	}

  private:
	std::string text;
};

class MacroUiDialog final : public MRDialogFoundation {
  public:
	MacroUiDialog(const MacroUiDialogDefinition &definition, MRVMRuntimeKv &aRuntimeKv) : TWindowInit(&TDialog::initFrame), MRDialogFoundation(macroDialogBounds(definition.width, definition.height, definition.x, definition.y), definition.title.empty() ? "DIALOG" : definition.title.c_str(), definition.width, definition.height), runtimeKv(aRuntimeKv) {
		ushort nextCommand = 41000;

		helpCtx = hcDialogMacroUi;
		for (std::size_t index = 0; index < definition.labels.size(); ++index) {
			const MacroUiLabelSpec &label = definition.labels[index];
			insert(new TStaticText(TRect(label.x, label.y, label.x + strwidth(label.text.c_str()), label.y + 1), label.text.c_str()));
		}

		for (std::size_t index = 0; index < definition.displays.size(); ++index) {
			const MacroUiDisplaySpec &display = definition.displays[index];
			insert(new MacroUiDisplayLine(TRect(display.x, display.y, display.x + display.width, display.y + 1), display.text));
		}

		for (std::size_t index = 0; index < definition.inputs.size(); ++index) {
			const MacroUiInputSpec &input = definition.inputs[index];
			const std::string labelText = input.label + ":";
			char *buffer = newStr(input.text.c_str());
			TInputLine *inputLine = new TInputLine(TRect(input.x + strwidth(labelText.c_str()) + 1, input.y, input.x + strwidth(labelText.c_str()) + 1 + input.width, input.y + 1), input.width);
			insert(new TLabel(TRect(input.x, input.y, input.x + strwidth(labelText.c_str()), input.y + 1), labelText.c_str(), inputLine));
			insert(inputLine);
			inputLine->setData(buffer);
			delete[] buffer;
			inputLines.emplace_back(input.id, inputLine);
		}

		for (std::size_t index = 0; index < definition.listBoxes.size(); ++index) {
			const MacroUiListBoxSpec &listBox = definition.listBoxes[index];
			const std::vector<std::string> items = mrvmResolveMacroUiListItems(runtimeKv, listBox.itemSpec);
			const int listTop = listBox.label.empty() ? listBox.y : listBox.y + 1;
			TScrollBar *scrollBar = nullptr;
			TView *listView = nullptr;

			if (!listBox.label.empty()) insert(new TStaticText(TRect(listBox.x, listBox.y, listBox.x + strwidth(listBox.label.c_str()), listBox.y + 1), listBox.label.c_str()));
			scrollBar = new TScrollBar(TRect(listBox.x + listBox.width - 1, listTop, listBox.x + listBox.width, listTop + listBox.height));
			insert(scrollBar);
			listView = createMacroUiListView(TRect(listBox.x, listTop, listBox.x + listBox.width - 1, listTop + listBox.height), scrollBar, items, nextCommand);
			insert(listView);
			setMacroUiListItems(listView, items, listBox.start);
			commandToId[nextCommand] = listBox.id;
			listViews.emplace_back(listBox.id, listView);
			++nextCommand;
		}

		for (std::size_t index = 0; index < definition.grids.size(); ++index) {
			const MacroUiGridSpec &grid = definition.grids[index];
			const std::vector<std::string> items = mrvmResolveMacroUiListItems(runtimeKv, grid.itemSpec);
			const int gridTop = grid.label.empty() ? grid.y : grid.y + 1;
			TScrollBar *scrollBar = nullptr;
			TView *gridView = nullptr;

			if (!grid.label.empty()) insert(new TStaticText(TRect(grid.x, grid.y, grid.x + strwidth(grid.label.c_str()), grid.y + 1), grid.label.c_str()));
			scrollBar = new TScrollBar(TRect(grid.x + grid.width - 1, gridTop, grid.x + grid.width, gridTop + grid.height));
			insert(scrollBar);
			gridView = createMacroUiGridView(TRect(grid.x, gridTop, grid.x + grid.width - 1, gridTop + grid.height), scrollBar, items, nextCommand);
			insert(gridView);
			setMacroUiGridItems(gridView, items, grid.start);
			commandToId[nextCommand] = grid.id;
			gridViews.emplace_back(grid.id, gridView);
			++nextCommand;
		}

		for (std::size_t index = 0; index < definition.trees.size(); ++index) {
			const MacroUiTreeSpec &tree = definition.trees[index];
			const std::vector<std::string> items = mrvmResolveMacroUiListItems(runtimeKv, tree.itemSpec);
			const int treeTop = tree.label.empty() ? tree.y : tree.y + 1;
			TScrollBar *scrollBar = new TScrollBar(TRect(tree.x + tree.width - 1, treeTop, tree.x + tree.width, treeTop + tree.height));
			TView *treeView = nullptr;

			if (!tree.label.empty()) insert(new TStaticText(TRect(tree.x, tree.y, tree.x + strwidth(tree.label.c_str()), tree.y + 1), tree.label.c_str()));
			insert(scrollBar);
			treeView = createMacroUiTreeView(TRect(tree.x, treeTop, tree.x + tree.width - 1, treeTop + tree.height), scrollBar, items, nextCommand);
			insert(treeView);
			setMacroUiTreeItems(treeView, items, tree.start);
			commandToId[nextCommand] = tree.id;
			treeViews.emplace_back(tree.id, treeView);
			++nextCommand;
		}

		for (std::size_t index = 0; index < definition.tables.size(); ++index) {
			const MacroUiTableSpec &table = definition.tables[index];
			const std::vector<std::string> items = mrvmResolveMacroUiListItems(runtimeKv, table.itemSpec);
			const int tableTop = table.label.empty() ? table.y : table.y + 1;
			TScrollBar *scrollBar = new TScrollBar(TRect(table.x + table.width - 1, tableTop, table.x + table.width, tableTop + table.height));
			TView *tableView = nullptr;

			if (!table.label.empty()) insert(new TStaticText(TRect(table.x, table.y, table.x + strwidth(table.label.c_str()), table.y + 1), table.label.c_str()));
			insert(scrollBar);
			tableView = createMacroUiTableView(TRect(table.x, tableTop, table.x + table.width - 1, tableTop + table.height), scrollBar, items, nextCommand);
			insert(tableView);
			setMacroUiTableItems(tableView, items, table.start);
			commandToId[nextCommand] = table.id;
			tableViews.emplace_back(table.id, tableView);
			++nextCommand;
		}

		for (std::size_t index = 0; index < definition.buttons.size(); ++index) {
			const MacroUiButtonSpec &button = definition.buttons[index];
			const MacroUiButtonCaption caption = parseMacroUiButtonCaption(button.text);
			commandToId[nextCommand] = button.id;
			for (std::size_t hotKeyIndex = 0; hotKeyIndex < caption.hotKeys.size(); ++hotKeyIndex)
				buttonHotKeys.emplace_back(caption.hotKeys[hotKeyIndex], nextCommand);
			insert(new TButton(TRect(button.x, button.y, button.x + button.width, button.y + 2), caption.displayLabel.c_str(), nextCommand, bfNormal));
			++nextCommand;
		}
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown) {
			const unsigned char typedChar = static_cast<unsigned char>(event.keyDown.charScan.charCode);
			const ushort hotKey = typedChar >= 32 ? static_cast<ushort>(std::toupper(typedChar)) : event.keyDown.keyCode;
			for (std::size_t index = 0; index < buttonHotKeys.size(); ++index) {
				const ushort registeredKey = buttonHotKeys[index].first;
				const ushort command = buttonHotKeys[index].second;
				if (registeredKey == hotKey) {
					endModal(command);
					clearEvent(event);
					return;
				}
			}
			if (ctrlToArrow(event.keyDown.keyCode) == kbEsc) {
				endModal(cmCancel);
				clearEvent(event);
				return;
			}
		}

		MRDialogFoundation::handleEvent(event);
		if (event.what == evCommand) {
			if (commandToId.find(event.message.command) != commandToId.end()) {
				endModal(event.message.command);
				clearEvent(event);
				return;
			}
		}
	}

	int selectedControlId(ushort result) const noexcept {
		std::map<ushort, int>::const_iterator it = commandToId.find(result);
		return it != commandToId.end() ? it->second : 0;
	}

	void collectValues(std::map<int, std::string> &textValues, std::map<int, int> &indexValues) const {
		char buffer[512] = {0};

		for (std::size_t rowIndex = 0; rowIndex < inputLines.size(); ++rowIndex) {
			const int id = inputLines[rowIndex].first;
			TInputLine *inputLine = inputLines[rowIndex].second;
			std::memset(buffer, 0, sizeof(buffer));
			if (inputLine != nullptr) inputLine->getData(buffer);
			textValues[id] = buffer;
		}
		for (std::size_t rowIndex = 0; rowIndex < listViews.size(); ++rowIndex) {
			const int id = listViews[rowIndex].first;
			TView *listView = listViews[rowIndex].second;
			const int selectedIndex = macroUiListSelectedIndex(listView);
			indexValues[id] = std::max(0, selectedIndex);
			textValues[id] = macroUiListSelectedText(listView);
		}
		for (std::size_t rowIndex = 0; rowIndex < gridViews.size(); ++rowIndex) {
			const int id = gridViews[rowIndex].first;
			TView *gridView = gridViews[rowIndex].second;
			const int selectedIndex = macroUiGridSelectedIndex(gridView);
			indexValues[id] = std::max(0, selectedIndex);
			textValues[id] = macroUiGridSelectedText(gridView);
		}
		for (std::size_t rowIndex = 0; rowIndex < treeViews.size(); ++rowIndex) {
			const int id = treeViews[rowIndex].first;
			TView *treeView = treeViews[rowIndex].second;

			indexValues[id] = macroUiTreeSelectedIndex(treeView);
			textValues[id] = macroUiTreeSelectedText(treeView);
		}
		for (std::size_t rowIndex = 0; rowIndex < tableViews.size(); ++rowIndex) {
			const int id = tableViews[rowIndex].first;
			TView *tableView = tableViews[rowIndex].second;

			indexValues[id] = macroUiTableSelectedIndex(tableView);
			textValues[id] = macroUiTableSelectedText(tableView);
		}
	}

  private:
	MRVMRuntimeKv &runtimeKv;
	std::map<ushort, int> commandToId;
	std::vector<std::pair<ushort, ushort>> buttonHotKeys;
	std::vector<std::pair<int, TInputLine *>> inputLines;
	std::vector<std::pair<int, TView *>> listViews;
	std::vector<std::pair<int, TView *>> gridViews;
	std::vector<std::pair<int, TView *>> treeViews;
	std::vector<std::pair<int, TView *>> tableViews;
};

}

std::vector<std::string> mrvmResolveMacroUiListItems(MRVMRuntimeKv &runtimeKv, const std::string &itemSpec) {
	const std::string key = mrvmModelessUiListKey(itemSpec);
	std::vector<std::string> values;

	if (mrvmModelessUiReadItemList(runtimeKv, key, values)) return values;
	return parseMacroMenuItems(itemSpec);
}

std::string mrvmMacroUiGridItemText(const std::string &source) {
	return macroUiGridItemText(source);
}

void mrvmBeginMacroUiDialog(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	mrvmModelessUiBeginDialog(runtimeKv, mrvmValueAsInt(args[0]), mrvmValueAsInt(args[1]), std::max(24, mrvmValueAsInt(args[2])), std::max(8, mrvmValueAsInt(args[3])), mrvmValueAsString(args[4]));
}

void mrvmAddMacroUiLabel(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	MacroUiLabelSpec spec;

	spec.x = mrvmValueAsInt(args[0]);
	spec.y = mrvmValueAsInt(args[1]);
	spec.text = mrvmValueAsString(args[2]);
	mrvmModelessUiAppendLabel(runtimeKv, spec);
}

void mrvmAddMacroUiButton(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	MacroUiButtonSpec spec;

	spec.x = mrvmValueAsInt(args[0]);
	spec.y = mrvmValueAsInt(args[1]);
	spec.width = std::max(6, mrvmValueAsInt(args[2]));
	spec.id = mrvmValueAsInt(args[3]);
	spec.text = mrvmValueAsString(args[4]);
	mrvmModelessUiAppendButton(runtimeKv, spec);
}

void mrvmAddMacroUiDisplay(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	MacroUiDisplaySpec spec;

	spec.x = mrvmValueAsInt(args[0]);
	spec.y = mrvmValueAsInt(args[1]);
	spec.width = std::max(4, mrvmValueAsInt(args[2]));
	spec.text = mrvmValueAsString(args[3]);
	mrvmModelessUiAppendDisplay(runtimeKv, spec);
}

void mrvmAddMacroUiInput(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	MacroUiInputSpec spec;

	spec.x = mrvmValueAsInt(args[0]);
	spec.y = mrvmValueAsInt(args[1]);
	spec.width = std::max(4, mrvmValueAsInt(args[2]));
	spec.id = mrvmValueAsInt(args[3]);
	spec.label = mrvmValueAsString(args[4]);
	spec.text = mrvmValueAsString(args[5]);
	mrvmModelessUiAppendInput(runtimeKv, spec);
}

static MacroUiSelectionSpec macroUiSelectionSpec(const std::vector<Value> &args, int minimumHeight) {
	MacroUiSelectionSpec spec;

	spec.x = mrvmValueAsInt(args[0]);
	spec.y = mrvmValueAsInt(args[1]);
	spec.width = std::max(8, mrvmValueAsInt(args[2]));
	spec.height = std::max(minimumHeight, mrvmValueAsInt(args[3]));
	spec.id = mrvmValueAsInt(args[4]);
	spec.label = mrvmValueAsString(args[5]);
	spec.itemSpec = mrvmValueAsString(args[6]);
	spec.start = std::max(1, mrvmValueAsInt(args[7]));
	return spec;
}

void mrvmAddMacroUiListBox(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	MacroUiListBoxSpec spec;
	const std::vector<std::string> items = mrvmResolveMacroUiListItems(runtimeKv, mrvmValueAsString(args[6]));
	int selectedIndex = 0;

	spec = macroUiSelectionSpec(args, 2);
	if (!items.empty()) selectedIndex = std::min(static_cast<int>(items.size()), spec.start);
	mrvmModelessUiWriteIndexValue(runtimeKv, spec.id, selectedIndex);
	mrvmModelessUiWriteTextValue(runtimeKv, spec.id, selectedIndex > 0 ? items[static_cast<std::size_t>(selectedIndex - 1)] : std::string());
	mrvmModelessUiAppendListBox(runtimeKv, spec);
}

void mrvmAddMacroUiGrid(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	MacroUiGridSpec spec;
	const std::vector<std::string> items = mrvmResolveMacroUiListItems(runtimeKv, mrvmValueAsString(args[6]));
	int selectedIndex = 0;

	spec = macroUiSelectionSpec(args, 3);
	if (!items.empty()) selectedIndex = std::min(static_cast<int>(items.size()), spec.start);
	mrvmModelessUiWriteIndexValue(runtimeKv, spec.id, selectedIndex);
	mrvmModelessUiWriteTextValue(runtimeKv, spec.id, selectedIndex > 0 ? mrvmMacroUiGridItemText(items[static_cast<std::size_t>(selectedIndex - 1)]) : std::string());
	mrvmModelessUiAppendGrid(runtimeKv, spec);
}

void mrvmAddMacroUiTree(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	MacroUiTreeSpec spec = macroUiSelectionSpec(args, 2);
	const std::vector<std::string> items = mrvmResolveMacroUiListItems(runtimeKv, spec.itemSpec);
	const int selectedIndex = items.empty() ? 0 : std::min(static_cast<int>(items.size()), spec.start);

	if (!macroUiTreeItemsValid(items)) throw std::runtime_error("UI_TREE expects a tree created with UI_TREE_NODE.");
	mrvmModelessUiWriteIndexValue(runtimeKv, spec.id, selectedIndex);
	mrvmModelessUiWriteTextValue(runtimeKv, spec.id, std::string());
	mrvmModelessUiAppendTree(runtimeKv, spec);
}

void mrvmAddMacroUiTable(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	MacroUiTableSpec spec = macroUiSelectionSpec(args, 3);
	const std::vector<std::string> items = mrvmResolveMacroUiListItems(runtimeKv, spec.itemSpec);
	int rowCount = 0;

	for (std::size_t index = 0; index < items.size(); ++index)
		if (items[index].compare(0, 10, "TABLE_ROW\t") == 0) ++rowCount;
	const int selectedIndex = rowCount > 0 ? std::min(rowCount, spec.start) : 0;

	if (!macroUiTableItemsValid(items)) throw std::runtime_error("UI_TABLE expects columns and rows created with UI_TABLE_COLUMN and UI_TABLE_ROW.");
	mrvmModelessUiWriteIndexValue(runtimeKv, spec.id, selectedIndex);
	mrvmModelessUiWriteTextValue(runtimeKv, spec.id, std::string());
	mrvmModelessUiAppendTable(runtimeKv, spec);
}

void mrvmClearMacroUiItemList(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	const std::string key = mrvmModelessUiListKey(mrvmValueAsString(args[0]));

	if (key.empty()) throw std::runtime_error("UI_LIST_CLEAR expects a non-empty list name.");
	mrvmModelessUiClearItemList(runtimeKv, key);
}

void mrvmAddMacroUiItemListValue(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	const std::string key = mrvmModelessUiListKey(mrvmValueAsString(args[0]));

	if (key.empty()) throw std::runtime_error("UI_LIST_ADD expects a non-empty list name.");
	mrvmModelessUiAddItemListValue(runtimeKv, key, mrvmValueAsString(args[1]));
}

void mrvmClearMacroUiTree(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	const std::string key = mrvmModelessUiListKey(mrvmValueAsString(args[0]));

	if (key.empty()) throw std::runtime_error("UI_TREE_CLEAR expects a non-empty tree name.");
	mrvmModelessUiClearItemList(runtimeKv, key);
}

void mrvmAddMacroUiTreeNode(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	const std::string key = mrvmModelessUiListKey(mrvmValueAsString(args[0]));
	const std::string nodeId = mrvmValueAsString(args[1]);
	const std::string parentId = mrvmValueAsString(args[2]);
	const std::string text = mrvmValueAsString(args[3]);
	std::vector<std::string> values;

	if (key.empty() || nodeId.empty() || text.empty() || nodeId.find('\t') != std::string::npos || parentId.find('\t') != std::string::npos || text.find('\t') != std::string::npos) throw std::runtime_error("UI_TREE_NODE expects a tree name, node id and text without tab characters.");
	static_cast<void>(mrvmModelessUiReadItemList(runtimeKv, key, values));
	values.push_back(macroUiTreeNodeItem(nodeId, parentId, text, mrvmValueAsInt(args[4]) != 0));
	if (!macroUiTreeItemsValid(values)) throw std::runtime_error("UI_TREE_NODE expects unique nodes whose parent was declared earlier in the same tree.");
	mrvmModelessUiAddItemListValue(runtimeKv, key, values.back());
}

void mrvmClearMacroUiTable(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	const std::string key = mrvmModelessUiListKey(mrvmValueAsString(args[0]));

	if (key.empty()) throw std::runtime_error("UI_TABLE_CLEAR expects a non-empty table name.");
	mrvmModelessUiClearItemList(runtimeKv, key);
}

void mrvmAddMacroUiTableColumn(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	const std::string key = mrvmModelessUiListKey(mrvmValueAsString(args[0]));
	const std::string title = mrvmValueAsString(args[1]);
	std::vector<std::string> values;

	if (key.empty() || title.empty() || title.find('\t') != std::string::npos || mrvmValueAsInt(args[2]) < 1 || mrvmValueAsInt(args[2]) > 80) throw std::runtime_error("UI_TABLE_COLUMN expects a table name, title without tabs and a width from 1 through 80.");
	static_cast<void>(mrvmModelessUiReadItemList(runtimeKv, key, values));
	values.push_back(macroUiTableColumnItem(title, mrvmValueAsInt(args[2])));
	if (!macroUiTableItemsValid(values)) throw std::runtime_error("UI_TABLE_COLUMN must precede all rows and a table is limited to 16 columns.");
	mrvmModelessUiAddItemListValue(runtimeKv, key, values.back());
}

void mrvmAddMacroUiTableRow(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	const std::string key = mrvmModelessUiListKey(mrvmValueAsString(args[0]));
	const std::string rowId = mrvmValueAsString(args[1]);
	const std::string cells = mrvmValueAsString(args[2]);
	std::vector<std::string> values;

	if (key.empty() || rowId.empty() || rowId.find('\t') != std::string::npos) throw std::runtime_error("UI_TABLE_ROW expects a table name and row id without tabs.");
	static_cast<void>(mrvmModelessUiReadItemList(runtimeKv, key, values));
	values.push_back(macroUiTableRowItem(rowId, cells));
	if (!macroUiTableItemsValid(values)) throw std::runtime_error("UI_TABLE_ROW expects exactly one tab-separated cell per declared column and at most 512 rows.");
	mrvmModelessUiAddItemListValue(runtimeKv, key, values.back());
}

void mrvmBindMacroModelessButton(MRVMRuntimeKv &runtimeKv, const std::vector<Value> &args) {
	const int buttonId = mrvmValueAsInt(args[0]);
	const std::string macroSpec = mrvmValueAsString(args[1]);

	if (buttonId <= 0) throw std::runtime_error("UI_MODELESS_ON expects a positive control id.");
	mrvmModelessUiWriteModelessMacro(runtimeKv, buttonId, macroSpec);
}

int mrvmRunMacroUiDialogDefinition(MRVMRuntimeKv &runtimeKv) {
	MacroUiDialogDefinition definition = mrvmModelessUiReadDialogDefinition(runtimeKv);
	MacroUiDialog *dialog = new MacroUiDialog(definition, runtimeKv);
	std::map<int, std::string> textValues;
	std::map<int, int> indexValues;
	ushort result = cmCancel;
	int lastCommandId = 0;

	if (dialog == nullptr) return 0;
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	dialog->collectValues(textValues, indexValues);
	for (std::map<int, std::string>::const_iterator it = textValues.begin(); it != textValues.end(); ++it)
		mrvmModelessUiWriteTextValue(runtimeKv, it->first, it->second);
	for (std::map<int, int>::const_iterator it = indexValues.begin(); it != indexValues.end(); ++it)
		mrvmModelessUiWriteIndexValue(runtimeKv, it->first, it->second);
	lastCommandId = result == cmCancel ? 0 : dialog->selectedControlId(result);
	TObject::destroy(dialog);
	return lastCommandId;
}

int mrvmRunMacroMenuIntrinsic(const std::string &name, const std::vector<Value> &args) {
	MacroMenuRequest request;

	request.horizontal = name == "BAR_MENU";
	if (args.size() == 1) {
		request.menuSpec = mrvmValueAsString(args[0]);
	} else if (args.size() == 2) {
		request.title = mrvmValueAsString(args[0]);
		request.menuSpec = mrvmValueAsString(args[1]);
	} else if (args.size() == 3) {
		request.start = mrvmValueAsInt(args[0]);
		request.title = mrvmValueAsString(args[1]);
		request.menuSpec = mrvmValueAsString(args[2]);
	} else if (args.size() == 5) {
		request.x = mrvmValueAsInt(args[0]);
		request.y = mrvmValueAsInt(args[1]);
		request.start = mrvmValueAsInt(args[2]);
		request.title = mrvmValueAsString(args[3]);
		request.menuSpec = mrvmValueAsString(args[4]);
	} else {
		throw std::runtime_error(name + " expects 1, 2, 3 or 5 arguments.");
	}
	return runMacroMenuDialog(request);
}

std::string mrvmRunMacroStringInputIntrinsic(const std::vector<Value> &args) {
	MacroStringInputRequest request;

	request.title = "STRING INPUT";
	if (args.size() == 1) {
		request.initialValue = mrvmValueAsString(args[0]);
		request.width = std::max(20, static_cast<int>(request.initialValue.size()) + 2);
	} else if (args.size() == 2) {
		request.title = mrvmValueAsString(args[0]);
		request.initialValue = mrvmValueAsString(args[1]);
		request.width = std::max(20, static_cast<int>(request.initialValue.size()) + 2);
	} else if (args.size() == 3) {
		request.title = mrvmValueAsString(args[0]);
		request.initialValue = mrvmValueAsString(args[1]);
		request.width = std::max(8, mrvmValueAsInt(args[2]));
	} else if (args.size() == 5) {
		request.x = mrvmValueAsInt(args[0]);
		request.y = mrvmValueAsInt(args[1]);
		request.width = std::max(8, mrvmValueAsInt(args[2]));
		request.title = mrvmValueAsString(args[3]);
		request.initialValue = mrvmValueAsString(args[4]);
	} else {
		throw std::runtime_error("STRING_IN expects 1, 2, 3 or 5 arguments.");
	}
	return runMacroStringInputDialog(request);
}
