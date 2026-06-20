#include "MRMacroModelessUi.hpp"

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

	for (char &ch : result)
		ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
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
	const std::string key = upperCaptionToken(name);

	for (const NamedKey &entry : namedKeys)
		if (key == entry.name) return entry.code;
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

static TFrame *initMacroModelessFrame(TRect bounds) {
	return new MRFrame(bounds);
}

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

class MRMacroModelessWindow final : public TWindow {
  public:
	explicit MRMacroModelessWindow(const MRMacroModelessWindowDefinition &windowDefinition) : TWindowInit(initMacroModelessFrame), TWindow(modelessBounds(windowDefinition), windowDefinition.title.empty() ? "MRMac" : windowDefinition.title.c_str(), wnNoNumber), definition(windowDefinition) {
		buildControls();
	}

	TPalette &getPalette() const override {
		static TPalette palette(cpGrayDialog, sizeof(cpGrayDialog) - 1);
		return palette;
	}

	void close() override {
		const auto it = g_windows.find(definition.windowId);
		if (it != g_windows.end() && it->second == this) g_windows.erase(it);
		TWindow::close();
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown) {
			const unsigned char typedChar = static_cast<unsigned char>(event.keyDown.charScan.charCode);
			const ushort hotKey = typedChar >= 32 ? static_cast<ushort>(std::toupper(typedChar)) : event.keyDown.keyCode;

			for (const auto &entry : hotKeys)
				if (entry.first == hotKey) {
					runButton(entry.second);
					clearEvent(event);
					return;
				}
		}

		TWindow::handleEvent(event);
		if (event.what == evCommand) {
			const auto buttonIt = commandToButton.find(event.message.command);
			if (buttonIt != commandToButton.end()) {
				runButton(buttonIt->second);
				clearEvent(event);
				return;
			}
			const auto listIt = commandToList.find(event.message.command);
			if (listIt != commandToList.end()) {
				selection = readSelection(listIt->second);
				clearEvent(event);
				return;
			}
		}
	}

	void refreshLists() {
		for (const auto &entry : listViews) {
			const int oldIndex = entry.second != nullptr ? entry.second->focused + 1 : 1;
			const auto specIt = listSpecs.find(entry.first);
			if (entry.second == nullptr || specIt == listSpecs.end()) continue;
			entry.second->setItems(resolveListItems(specIt->second.itemSpec), std::max(1, oldIndex));
		}
		if (selection.controlId != 0) selection = readSelection(selection.controlId);
	}

  private:
	void buildControls() {
		ushort nextCommand = cmMacroModelessBase;

		options |= ofTileable;
		for (const auto &label : definition.labels)
			insert(new TStaticText(TRect(label.x, label.y, label.x + strwidth(label.text.c_str()), label.y + 1), label.text.c_str()));

		for (const auto &listBox : definition.listBoxes) {
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

		for (const auto &button : definition.buttons) {
			const MRMacroModelessButtonCaption caption = parseButtonCaption(button.text);

			if (nextCommand >= cmMacroModelessMax) break;
			commandToButton[nextCommand] = button.id;
			buttons[button.id] = button;
			for (ushort hotKey : caption.hotKeys)
				hotKeys.emplace_back(hotKey, button.id);
			insert(new TButton(TRect(button.x, button.y, button.x + button.width, button.y + 2), caption.displayLabel.c_str(), nextCommand, bfNormal));
			++nextCommand;
		}
	}

	MRMacroModelessSelection readSelection(int controlId) const {
		MRMacroModelessSelection result;
		const auto it = listViews.find(controlId);

		result.controlId = controlId;
		if (it == listViews.end() || it->second == nullptr) return result;
		result.index = std::max(0, it->second->focused + 1);
		if (result.index > 0 && static_cast<std::size_t>(result.index - 1) < it->second->values().size()) result.text = it->second->values()[static_cast<std::size_t>(result.index - 1)];
		return result;
	}

	void runButton(int buttonId) {
		const auto it = buttons.find(buttonId);
		if (it == buttons.end()) return;
		if (selection.controlId != 0) selection = readSelection(selection.controlId);
		else if (!listViews.empty())
			selection = readSelection(listViews.begin()->first);
		if (g_commandRunner != nullptr) g_commandRunner(definition.windowId, buttonId, selection, it->second.macroSpec);
		refreshLists();
	}

	MRMacroModelessWindowDefinition definition;
	std::map<ushort, int> commandToButton;
	std::map<ushort, int> commandToList;
	std::map<int, MRMacroModelessButtonSpec> buttons;
	std::map<int, MRMacroModelessListBoxSpec> listSpecs;
	std::map<int, MRMacroModelessListView *> listViews;
	std::vector<std::pair<ushort, int>> hotKeys;
	MRMacroModelessSelection selection;
};

} // namespace

void setMacroModelessListResolver(MRMacroModelessListResolver resolver) {
	g_listResolver = resolver;
}

void setMacroModelessCommandRunner(MRMacroModelessCommandRunner runner) {
	g_commandRunner = runner;
}

bool showMacroModelessWindow(const MRMacroModelessWindowDefinition &definition) {
	if (TProgram::deskTop == nullptr || definition.windowId.empty()) return false;
	const auto it = g_windows.find(definition.windowId);
	if (it != g_windows.end() && it->second != nullptr) {
		it->second->refreshLists();
		it->second->select();
		return true;
	}

	MRMacroModelessWindow *window = new MRMacroModelessWindow(definition);
	if (window == nullptr) return false;
	g_windows[definition.windowId] = window;
	TProgram::deskTop->insert(window);
	window->refreshLists();
	return true;
}

bool closeMacroModelessWindow(const std::string &windowId) {
	const auto it = g_windows.find(windowId);
	if (it == g_windows.end() || it->second == nullptr) return false;
	message(it->second, evCommand, cmClose, nullptr);
	return true;
}

void refreshMacroModelessWindows() {
	for (const auto &entry : g_windows)
		if (entry.second != nullptr) entry.second->refreshLists();
}
