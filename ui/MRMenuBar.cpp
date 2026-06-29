#define Uses_TMenuBar
#define Uses_TDrawBuffer
#define Uses_TKeys
#define Uses_TMenu
#define Uses_TMenuItem
#define Uses_TRect
#define Uses_TSubMenu
#include <tvision/tv.h>

#include "MRMenuBar.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <string>
#include <utility>

#include "../app/MRCommands.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../keymap/MRKeymapContext.hpp"
#include "../keymap/MRKeymapResolver.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../mrmac/MRVM.hpp"
#include "MRBentoBox.hpp"
#include "MRWindowSupport.hpp"

void mrvmUiInvalidateScreenBase() noexcept;

namespace {
TMenuItem *findMenuItemByCommand(TMenu *menu, ushort command) {
	for (TMenuItem *item = menu != nullptr ? menu->items : nullptr; item != nullptr; item = item->next) {
		if (item->command == command) return item;
		if (item->command == 0) {
			TMenuItem *match = findMenuItemByCommand(item->subMenu, command);
			if (match != nullptr) return match;
		}
	}
	return nullptr;
}

bool removeMenuItemByCommand(TMenu *menu, ushort command) {
	TMenuItem **link = nullptr;

	if (menu == nullptr) return false;
	link = &menu->items;
	while (*link != nullptr) {
		TMenuItem *item = *link;
		if (item->command == command) {
			*link = item->next;
			item->next = nullptr;
			if (menu->deflt == item) menu->deflt = menu->items;
			delete item;
			return true;
		}
		if (item->command == 0 && removeMenuItemByCommand(item->subMenu, command)) return true;
		link = &item->next;
	}
	return false;
}

struct MenuShortcutSpec {
	ushort command;
	TKey startupKey;
	const char *startupLabel;
	TKey editorKey;
	const char *editorLabel;
	TKey normalKey;
	const char *normalLabel;
};

void setMenuItemShortcut(TMenuItem *item, TKey keyCode, const char *label) {
	if (item == nullptr || item->command == 0) return;
	item->keyCode = keyCode;
	delete[] const_cast<char *>(item->param);
	if (label == nullptr || label[0] == '\0')
		item->param = nullptr;
	else
		item->param = newStr(label);
}

void applyMenuShortcutSpec(TMenu *menu, const MenuShortcutSpec &spec, bool startupActive, bool editorActive) {
	TKey key = spec.normalKey;
	const char *label = spec.normalLabel;

	if (startupActive) {
		key = spec.startupKey;
		label = spec.startupLabel;
	} else if (editorActive) {
		key = spec.editorKey;
		label = spec.editorLabel;
	}
	for (TMenuItem *item = menu != nullptr ? menu->items : nullptr; item != nullptr; item = item->next) {
		if (item->command == spec.command) setMenuItemShortcut(item, key, label);
		if (item->command == 0) applyMenuShortcutSpec(item->subMenu, spec, startupActive, editorActive);
	}
}

std::size_t indexOfMenuItem(const TMenuItem *items, const TMenuItem *target) noexcept {
	std::size_t index = 0;

	for (const TMenuItem *item = items; item != nullptr; item = item->next, ++index)
		if (item == target) return index;
	return std::numeric_limits<std::size_t>::max();
}

TMenuItem *menuItemAt(TMenuItem *items, std::size_t index) noexcept {
	std::size_t currentIndex = 0;

	for (TMenuItem *item = items; item != nullptr; item = item->next, ++currentIndex)
		if (currentIndex == index) return item;
	return nullptr;
}

TMenu *cloneMenu(const TMenu *source);

TMenuItem *cloneMenuItem(const TMenuItem *source) {
	TMenuItem *cloned = nullptr;

	if (source == nullptr) return nullptr;
	if (source->name == nullptr && source->command == 0) cloned = &newLine();
	else if (source->command == 0 && source->subMenu != nullptr)
		cloned = new TMenuItem(source->name, source->keyCode, cloneMenu(source->subMenu), source->helpCtx);
	else
		cloned = new TMenuItem(source->name != nullptr ? source->name : "", source->command, source->keyCode, source->helpCtx, source->param != nullptr ? source->param : "");

	cloned->disabled = source->disabled;
	return cloned;
}

TMenuItem *cloneMenuItems(const TMenuItem *source) {
	TMenuItem *head = nullptr;
	TMenuItem *tail = nullptr;

	for (const TMenuItem *item = source; item != nullptr; item = item->next) {
		TMenuItem *cloned = cloneMenuItem(item);

		if (head == nullptr) head = cloned;
		else
			tail->next = cloned;
		tail = cloned;
	}
	return head;
}

TMenu *cloneMenu(const TMenu *source) {
	TMenu *cloned = nullptr;
	std::size_t defaultIndex = 0;

	if (source == nullptr) return nullptr;

	defaultIndex = indexOfMenuItem(source->items, source->deflt);
	cloned = new TMenu();
	cloned->items = cloneMenuItems(source->items);
	cloned->deflt = menuItemAt(cloned->items, defaultIndex);
	if (cloned->deflt == nullptr) cloned->deflt = cloned->items;
	return cloned;
}

void appendMenuItem(TMenu *menu, TMenuItem *item) {
	TMenuItem *tail = nullptr;

	if (menu == nullptr || item == nullptr) return;
	item->next = nullptr;
	if (menu->items == nullptr) {
		menu->items = item;
		menu->deflt = item;
		return;
	}
	for (tail = menu->items; tail->next != nullptr; tail = tail->next)
		;
	tail->next = item;
}

int hotkeyIndex(char ch) noexcept {
	if (ch >= 'A' && ch <= 'Z') return ch - 'A';
	if (ch >= '0' && ch <= '9') return 26 + (ch - '0');
	return -1;
}

char canonicalHotkeyChar(char ch) noexcept {
	unsigned char uch = static_cast<unsigned char>(ch);
	if (std::isalpha(uch) != 0) return static_cast<char>(std::toupper(uch));
	if (std::isdigit(uch) != 0) return static_cast<char>(uch);
	return '\0';
}

char markedHotkeyChar(const char *name) noexcept {
	if (name == nullptr) return '\0';
	for (const char *pos = name; pos[0] != '\0' && pos[1] != '\0' && pos[2] != '\0'; ++pos)
		if (pos[0] == '~' && pos[2] == '~') return canonicalHotkeyChar(pos[1]);
	return '\0';
}

int markedHotkeyColumn(const char *name) noexcept {
	int column = 0;

	if (name == nullptr) return -1;
	for (const char *pos = name; *pos != '\0'; ++pos) {
		if (pos[0] == '~' && pos[1] != '\0' && pos[2] == '~') return column;
		if (*pos != '~') ++column;
	}
	return -1;
}

void markUsedHotkey(std::array<bool, 36> &usedHotkeys, char hotkey) noexcept {
	const int index = hotkeyIndex(canonicalHotkeyChar(hotkey));

	if (index >= 0) usedHotkeys[static_cast<std::size_t>(index)] = true;
}

bool compilerDiagnosticsFunctionKeysActive() {
	MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(currentEditWindow());

	return bentoBox != nullptr && bentoBox->problemsPane() != nullptr && bentoBox->hasCompilerProblems();
}

bool fileCompareFunctionKeysActive() {
	MREditWindow *window = currentEditWindow();
	MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);

	if (bentoBox != nullptr && bentoBox->isFileCompareBox()) return true;
	if (window != nullptr)
		for (MREditWindow *candidate : allEditWindowsInZOrder()) {
			bentoBox = dynamic_cast<MRBentoBox *>(candidate);
			if (bentoBox != nullptr && bentoBox->isFileCompareBox() && bentoBox->containsFileCompareSourceWindow(window)) return true;
		}
	return false;
}

bool bentoToolPaneFunctionKeysActive() {
	MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(currentEditWindow());

	return bentoBox != nullptr && bentoBox->secondaryEditWindow() != nullptr && !compilerDiagnosticsFunctionKeysActive() && !fileCompareFunctionKeysActive();
}

bool readOnlyFunctionKeysActive() {
	MREditWindow *window = currentEditWindow();
	MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);
	MREditWindow *target = bentoBox != nullptr ? bentoBox->editorCommandTarget() : window;

	return target != nullptr && target->isReadOnly();
}

char chooseMenuHotkey(const std::string &title, const std::array<bool, 36> &usedHotkeys) {
	for (char ch : title) {
		const char hotkey = canonicalHotkeyChar(ch);
		const int index = hotkeyIndex(hotkey);

		if (hotkey != '\0' && index >= 0 && !usedHotkeys[static_cast<std::size_t>(index)]) return hotkey;
	}
	return '\0';
}

std::string menuTitleWithHotkeyMarker(const std::string &title, char hotkey) {
	std::string marked = title;
	const char canonical = canonicalHotkeyChar(hotkey);

	if (canonical == '\0') return marked;
	for (std::size_t i = 0; i < marked.size(); ++i)
		if (canonicalHotkeyChar(marked[i]) == canonical) {
			marked.insert(i, 1, '~');
			marked.insert(i + 2, 1, '~');
			return marked;
		}
	return marked;
}
} // namespace

MRMenuBar::MRMenuBar(const TRect &r, TSubMenu &aMenu) : TMenuBar(r, aMenu), mBaseMenu(nullptr), mRuntimeNodes(), mStartupFunctionKeysActive(false), mEditorFunctionKeysActive(false), mRightStatus(), mAutoMarqueeStatus(), mManualMarqueeStatus(), mAutoMarqueeKind(MarqueeKind::Info) {
	mBaseMenu = cloneMenu(menu);
}

MRMenuBar::~MRMenuBar() {
	delete mBaseMenu;
	mBaseMenu = nullptr;
}

void MRMenuBar::setAutoMarqueeStatusSegments(const std::vector<MarqueeSegment> &segments, MarqueeKind kind) {
	std::string status;

	for (const MarqueeSegment &segment : segments)
		status += segment.text;
	if (mAutoMarqueeStatus != status || mAutoMarqueeKind != kind || mAutoMarqueeSegments != segments) {
		mAutoMarqueeStatus = status;
		mAutoMarqueeKind = kind;
		mAutoMarqueeSegments = segments;
		drawView();
	}
}

void MRMenuBar::handleEvent(TEvent &event) {
	if (mrHandleRuntimeKeymapEvent(event, MRKeymapContext::Menu, nullptr)) return;
	if (event.what == evKeyDown && currentEditWindow() != nullptr && runtimeKeymapResolver().hasPending(MRKeymapContext::Edit)) return;
	TMenuBar::handleEvent(event);
}

std::string MRMenuBar::trimAscii(std::string value) {
	auto isTrimChar = [](unsigned char ch) noexcept { return std::isspace(ch) != 0 || ch < 32; };

	while (!value.empty() && isTrimChar(static_cast<unsigned char>(value.front())))
		value.erase(value.begin());
	while (!value.empty() && isTrimChar(static_cast<unsigned char>(value.back())))
		value.pop_back();
	return value;
}

std::string MRMenuBar::canonicalMenuToken(const std::string &value) {
	std::string canonical = trimAscii(value);

	for (char &ch : canonical)
		ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
	return canonical;
}

bool MRMenuBar::ownerSpecMatchesFile(const std::string &ownerSpec, const std::string &fileSpec) noexcept {
	const std::string canonicalOwner = canonicalMenuToken(ownerSpec);
	const std::string canonicalFile = canonicalMenuToken(fileSpec);
	const std::size_t caretPos = canonicalOwner.find('^');
	std::string ownerFile;

	if (canonicalOwner.empty() || canonicalFile.empty() || caretPos == std::string::npos) return false;
	{
		const std::size_t searchEnd = caretPos == 0 ? 0 : caretPos - 1;
		const std::size_t slashPos = canonicalOwner.find_last_of("/\\", searchEnd);
		const std::size_t nameStart = slashPos == std::string::npos ? 0 : slashPos + 1;

		ownerFile = canonicalOwner.substr(nameStart, caretPos - nameStart);
	}
	if (canonicalOwner.substr(0, caretPos) == canonicalFile) return true;
	return ownerFile == canonicalFile;
}

bool MRMenuBar::allocateRuntimeCommand(ushort &command, std::string *errorMessage) {
	if (mNextRuntimeCommand == std::numeric_limits<ushort>::max()) {
		if (errorMessage != nullptr) *errorMessage = "No runtime menu command ids left.";
		return false;
	}
	command = mNextRuntimeCommand++;
	return true;
}

int MRMenuBar::findRuntimeNodeIndex(const std::string &menuKey, const std::string &itemKey, const std::string &ownerSpec) const noexcept {
	const auto it = std::find_if(mRuntimeNodes.begin(), mRuntimeNodes.end(), [&](const RuntimeMenuNode &node) { return node.menuKey == menuKey && node.itemKey == itemKey && node.ownerSpec == ownerSpec; });

	if (it == mRuntimeNodes.end()) return -1;
	return static_cast<int>(std::distance(mRuntimeNodes.begin(), it));
}

bool MRMenuBar::rebuildRuntimeMenu() {
	struct RuntimeMenuGroup {
		std::string menuKey;
		std::string menuTitle;
		std::uint32_t order = 0;
		std::vector<const RuntimeMenuNode *> items;
	};

	TMenu *rebuilt = cloneMenu(mBaseMenu);
	std::array<bool, 36> usedHotkeys{};
	std::vector<RuntimeMenuGroup> groups;

	if (rebuilt == nullptr) return false;
	for (const TMenuItem *item = rebuilt->items; item != nullptr; item = item->next)
		markUsedHotkey(usedHotkeys, markedHotkeyChar(item->name));
	for (const RuntimeMenuNode &node : mRuntimeNodes) {
		auto it = std::find_if(groups.begin(), groups.end(), [&](const RuntimeMenuGroup &group) { return group.menuKey == node.menuKey; });
		if (it == groups.end()) {
			RuntimeMenuGroup group;

			group.menuKey = node.menuKey;
			group.menuTitle = node.menuTitle;
			group.order = node.order;
			group.items.push_back(&node);
			groups.push_back(std::move(group));
		} else {
			it->items.push_back(&node);
			if (node.order < it->order) {
				it->order = node.order;
				it->menuTitle = node.menuTitle;
			}
		}
	}
	std::sort(groups.begin(), groups.end(), [](const RuntimeMenuGroup &left, const RuntimeMenuGroup &right) { return left.order < right.order; });
	for (const RuntimeMenuGroup &group : groups) {
		auto *submenu = new TMenu();
		std::string groupTitle = group.menuTitle;
		const char groupHotkey = chooseMenuHotkey(groupTitle, usedHotkeys);

		for (const RuntimeMenuNode *node : group.items) {
			if (node->kind == RuntimeMenuNodeKind::Separator) {
				appendMenuItem(submenu, &newLine());
				continue;
			}
			std::string keyLabel = mrvmUiMenuKeyLabelForMacroSpec(node->macroSpec);
			if (keyLabel.empty()) keyLabel = mrvmUiMenuKeyLabelForMacroSpec(node->ownerSpec);
			appendMenuItem(submenu, new TMenuItem(node->itemTitle.c_str(), node->command, kbNoKey, hcNoContext, keyLabel.empty() ? nullptr : keyLabel.c_str()));
		}
		markUsedHotkey(usedHotkeys, groupHotkey);
		groupTitle = menuTitleWithHotkeyMarker(groupTitle, groupHotkey);
		appendMenuItem(rebuilt, new TMenuItem(groupTitle.c_str(), groupHotkey == '\0' ? TKey(kbNoKey) : TKey(static_cast<ushort>(groupHotkey), kbAltShift), submenu, hcNoContext));
	}

	current = nullptr;
	delete menu;
	menu = rebuilt;
	applyFunctionKeyMenuShortcuts(menu);
	return true;
}

bool MRMenuBar::registerRuntimeMenuItem(const std::string &menuTitle, const std::string &itemTitle, const std::string &macroSpec, const std::string &ownerSpec, std::string *errorMessage) {
	RuntimeMenuNode node;

	node.menuTitle = trimAscii(menuTitle);
	node.menuKey = canonicalMenuToken(menuTitle);
	node.itemTitle = trimAscii(itemTitle);
	node.itemKey = canonicalMenuToken(itemTitle);
	node.ownerSpec = trimAscii(ownerSpec);
	node.macroSpec = trimAscii(macroSpec);
	if (node.menuKey.empty()) {
		if (errorMessage != nullptr) *errorMessage = "REGISTER_MENU_ITEM requires a non-empty menu title.";
		return false;
	}
	if (node.macroSpec.empty() || node.ownerSpec.empty()) {
		if (errorMessage != nullptr) *errorMessage = "REGISTER_MENU_ITEM requires macro and owner context.";
		return false;
	}
	if (node.itemKey.empty()) node.itemKey = "SEP_" + std::to_string(mNextRuntimeOrder + 1);
	const int existingIndex = findRuntimeNodeIndex(node.menuKey, node.itemKey, node.ownerSpec);
	if (existingIndex >= 0 && mRuntimeNodes[static_cast<std::size_t>(existingIndex)].macroSpec == node.macroSpec) return true;
	if (existingIndex >= 0) {
		if (errorMessage != nullptr) *errorMessage = "REGISTER_MENU_ITEM is already registered by this macro.";
		return false;
	}
	node.kind = node.itemTitle.empty() ? RuntimeMenuNodeKind::Separator : RuntimeMenuNodeKind::Item;
	if (node.kind == RuntimeMenuNodeKind::Item && !allocateRuntimeCommand(node.command, errorMessage)) return false;
	node.order = ++mNextRuntimeOrder;
	mRuntimeNodes.push_back(std::move(node));
	if (!rebuildRuntimeMenu()) {
		mRuntimeNodes.pop_back();
		if (errorMessage != nullptr) *errorMessage = "REGISTER_MENU_ITEM could not rebuild the menu bar.";
		return false;
	}
	drawView();
	return true;
}

bool MRMenuBar::refreshRuntimeMenus(std::string *errorMessage) {
	if (!rebuildRuntimeMenu()) {
		if (errorMessage != nullptr) *errorMessage = "Could not rebuild runtime menus.";
		return false;
	}
	drawView();
	return true;
}

bool MRMenuBar::removeRuntimeMenuItem(const std::string &menuTitle, const std::string &itemTitle, const std::string &ownerSpec, std::string *errorMessage) {
	const std::string menuKey = canonicalMenuToken(menuTitle);
	const std::string itemKey = canonicalMenuToken(itemTitle);
	const std::string owner = trimAscii(ownerSpec);
	int index = -1;
	std::vector<RuntimeMenuNode> previousNodes = mRuntimeNodes;

	if (menuKey.empty()) {
		if (errorMessage != nullptr) *errorMessage = "REMOVE_MENU_ITEM requires a non-empty menu title.";
		return false;
	}
	for (std::size_t i = 0; i < mRuntimeNodes.size(); ++i) {
		const RuntimeMenuNode &node = mRuntimeNodes[i];

		if (node.ownerSpec != owner || node.menuKey != menuKey) continue;
		if (itemKey.empty()) {
			if (node.kind == RuntimeMenuNodeKind::Separator) {
				index = static_cast<int>(i);
				break;
			}
			continue;
		}
		if (node.itemKey == itemKey) {
			index = static_cast<int>(i);
			break;
		}
	}
	if (index < 0) {
		if (errorMessage != nullptr) *errorMessage = "REMOVE_MENU_ITEM references no item owned by the current macro.";
		return false;
	}
	mRuntimeNodes.erase(mRuntimeNodes.begin() + index);
	if (!rebuildRuntimeMenu()) {
		mRuntimeNodes = std::move(previousNodes);
		static_cast<void>(rebuildRuntimeMenu());
		if (errorMessage != nullptr) *errorMessage = "REMOVE_MENU_ITEM could not rebuild the menu bar.";
		return false;
	}
	drawView();
	return true;
}

bool MRMenuBar::removeRuntimeNodesOwnedByMacroSpec(const std::string &ownerSpec, std::string *errorMessage) {
	const std::string owner = trimAscii(ownerSpec);
	std::vector<RuntimeMenuNode> previousNodes = mRuntimeNodes;

	if (owner.empty()) return true;
	mRuntimeNodes.erase(std::remove_if(mRuntimeNodes.begin(), mRuntimeNodes.end(), [&](const RuntimeMenuNode &node) { return node.ownerSpec == owner; }), mRuntimeNodes.end());
	if (!rebuildRuntimeMenu()) {
		mRuntimeNodes = std::move(previousNodes);
		static_cast<void>(rebuildRuntimeMenu());
		if (errorMessage != nullptr) *errorMessage = "Unable to rebuild the menu bar after macro cleanup.";
		return false;
	}
	drawView();
	return true;
}

bool MRMenuBar::removeRuntimeNodesOwnedByFile(const std::string &fileSpec, std::string *errorMessage) {
	const std::string fileId = trimAscii(fileSpec);
	std::vector<RuntimeMenuNode> previousNodes = mRuntimeNodes;

	if (fileId.empty()) return true;
	mRuntimeNodes.erase(std::remove_if(mRuntimeNodes.begin(), mRuntimeNodes.end(), [&](const RuntimeMenuNode &node) { return ownerSpecMatchesFile(node.ownerSpec, fileId); }), mRuntimeNodes.end());
	if (!rebuildRuntimeMenu()) {
		mRuntimeNodes = std::move(previousNodes);
		static_cast<void>(rebuildRuntimeMenu());
		if (errorMessage != nullptr) *errorMessage = "Unable to rebuild the menu bar after file cleanup.";
		return false;
	}
	drawView();
	return true;
}

bool MRMenuBar::handleRuntimeCommand(ushort command) {
	const auto it = std::find_if(mRuntimeNodes.begin(), mRuntimeNodes.end(), [&](const RuntimeMenuNode &node) { return node.kind == RuntimeMenuNodeKind::Item && node.command == command; });

	if (it == mRuntimeNodes.end()) return false;
	static_cast<void>(runMacroSpecByName(it->macroSpec.c_str(), nullptr, true));
	mrvmUiInvalidateScreenBase();
	return true;
}

void MRMenuBar::applyFunctionKeyMenuShortcuts(TMenu *targetMenu) const {
	const bool diagnosticsActive = mEditorFunctionKeysActive && compilerDiagnosticsFunctionKeysActive();
	const bool fileCompareActive = mEditorFunctionKeysActive && fileCompareFunctionKeysActive();
	const bool bentoToolPaneActive = mEditorFunctionKeysActive && bentoToolPaneFunctionKeysActive();
	const bool readOnlyActive = mEditorFunctionKeysActive && readOnlyFunctionKeysActive();
	static const MenuShortcutSpec specs[] = {
	    {cmMrFileOpen, TKey(kbF3), "F3", TKey(kbNoKey), nullptr, TKey(kbF3), "F3"},
	    {cmMrFileLoad, TKey(kbF2), "F2", TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr},
	    {cmMrFileAcquire, TKey(kbF4), "F4", TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr},
	    {cmMrFileInformation, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr},
	    {cmMrFileSaveAs, TKey(kbNoKey), nullptr, TKey(kbCtrlF2), "CtrlF2", TKey(kbCtrlF2), "CtrlF2"},
	    {cmMrSearchMultiFileSearch, TKey(kbF5), "F5", TKey('F', kbAltShift), "AltShiftF", TKey('F', kbAltShift), "AltShiftF"},
	    {cmMrWindowOpen, TKey(kbF6), "F6", TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr},
	    {cmMrSearchMultiFileSearchReplace, TKey(kbF7), "F7", TKey('R', kbAltShift), "AltShiftR", TKey('R', kbAltShift), "AltShiftR"},
	    {cmMrFileOpenLiveLog, TKey(kbF8), "F8", TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr},
	    {cmMrFileOpenJournal, TKey(kbF9), "F9", TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr},
	    {cmMrSetupUserInterfaceSettings, TKey(kbF12), "F12", TKey(kbF12), "F12", TKey(kbNoKey), nullptr},
	    {cmQuit, TKey(kbAltX), "Alt-X", TKey(kbAltX), "Alt-X", TKey(kbAltX), "Alt-X"},
	    {cmMrMacroToggleRecording, TKey(kbNoKey), nullptr, TKey(kbAltF10), "AltF10", TKey(kbAltF10), "AltF10"},
	    {cmMrFileSave, TKey(kbNoKey), nullptr, TKey(kbF2), "F2", TKey(kbF2), "F2"},
	    {cmMrBlockLoadFromDisk, TKey(kbNoKey), nullptr, TKey(kbF3), "F3", TKey(kbNoKey), nullptr},
	    {cmMrBlockSaveToDisk, TKey(kbNoKey), nullptr, TKey(kbF4), "F4", TKey(kbShiftF2), "ShiftF2"},
	    {cmMrWindowCascade, TKey(kbNoKey), nullptr, TKey(kbF5), "F5", TKey(kbNoKey), nullptr},
	    {cmMrWindowTile, TKey(kbNoKey), nullptr, TKey(kbF6), "F6", TKey(kbNoKey), nullptr},
	    {cmMrWindowSplitVertical, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr},
	    {cmMrWindowSplitHorizontal, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr},
	    {cmMrWindowPrevDesktop, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr, TKey(kbF11, kbCtrlShift), "CtrlF11"},
	    {cmMrWindowNextDesktop, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr, TKey(kbF12, kbCtrlShift), "CtrlF12"},
	    {cmMrWindowMoveToPrevDesktop, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr, TKey(kbF11, kbShift), "ShiftF11"},
	    {cmMrWindowMoveToNextDesktop, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr, TKey(kbF12, kbShift), "ShiftF12"},
	    {cmMrSearchFindText, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr, TKey(kbF5), "F5"},
	    {cmMrSearchRepeatPrevious, TKey(kbNoKey), nullptr, TKey(kbCtrlF5), "CtrlF5", TKey(kbCtrlF5), "CtrlF5"},
	    {cmMrSearchGotoLineNumber, TKey(kbNoKey), nullptr, TKey(kbAltF5), "AltF5", TKey(kbAltF5), "AltF5"},
	    {cmMrSearchPushMarker, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr, TKey(kbF4), "F4"},
	    {cmMrWindowNext, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr, TKey(kbF6), "F6"},
	    {cmMrBlockMarkLines, TKey(kbNoKey), nullptr, TKey(kbF7), "F7", TKey(kbF7), "F7"},
	    {cmMrBlockEndMarking, TKey(kbNoKey), nullptr, TKey(kbF7), "F7", TKey(kbF7), "F7"},
	    {cmMrBlockCopy, TKey(kbNoKey), nullptr, TKey(kbF8), "F8", TKey(kbF8), "F8"},
	    {cmMrOtherBuildCurrentFile, TKey(kbNoKey), nullptr, TKey(kbF9), "F9", TKey(kbF9), "F9"},
	    {cmMrOtherClearOutput, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr, TKey(kbNoKey), nullptr},
	};

	for (const MenuShortcutSpec &spec : specs)
		applyMenuShortcutSpec(targetMenu, spec, mStartupFunctionKeysActive, mEditorFunctionKeysActive);
	if (diagnosticsActive) {
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockLoadFromDisk), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockSaveToDisk), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockMarkLines), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockEndMarking), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockCopy), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrWindowSplitHorizontal), TKey(kbF3), "F3");
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrWindowSplitVertical), TKey(kbF4), "F4");
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrWindowCascade), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrOtherClearOutput), TKey(kbF5), "F5");
	}
	if (bentoToolPaneActive) {
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockLoadFromDisk), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockSaveToDisk), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrWindowCascade), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrWindowSplitHorizontal), TKey(kbF3), "F3");
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrWindowSplitVertical), TKey(kbF4), "F4");
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrOtherClearOutput), TKey(kbF5), "F5");
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockMarkLines), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockEndMarking), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockCopy), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrSearchGotoLineNumber), TKey(kbF7), "F7");
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrSearchRepeatPrevious), TKey(kbF8), "F8");
	}
	if (fileCompareActive) {
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockLoadFromDisk), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockSaveToDisk), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockMarkLines), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockEndMarking), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockCopy), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockMove), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrSearchRepeatPrevious), TKey(kbNoKey), nullptr);
	}
	if (readOnlyActive && !fileCompareActive && !bentoToolPaneActive && !diagnosticsActive) {
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockLoadFromDisk), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockSaveToDisk), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockMarkLines), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockEndMarking), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrBlockCopy), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrFileSaveAs), TKey(kbF3), "F3");
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrSearchFindText), TKey(kbF4), "F4");
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrWindowCascade), TKey(kbNoKey), nullptr);
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrSearchMultiFileSearch), TKey(kbF5), "F5");
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrSearchGotoLineNumber), TKey(kbF7), "F7");
		setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrSearchRepeatPrevious), TKey(kbF8), "F8");
	}
	setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrOtherFindPreviousCompilerError), diagnosticsActive ? TKey(kbF7) : TKey(kbNoKey), diagnosticsActive ? "F7" : nullptr);
	setMenuItemShortcut(findMenuItemByCommand(targetMenu, cmMrOtherFindNextCompilerError), diagnosticsActive ? TKey(kbF8) : TKey(kbNoKey), diagnosticsActive ? "F8" : nullptr);
}

void MRMenuBar::setStartupFunctionKeysActive(bool active) {
	const bool editorActive = active ? false : mEditorFunctionKeysActive;
	if (mStartupFunctionKeysActive == active && mEditorFunctionKeysActive == editorActive) return;
	mStartupFunctionKeysActive = active;
	mEditorFunctionKeysActive = editorActive;
	applyFunctionKeyMenuShortcuts(mBaseMenu);
	applyFunctionKeyMenuShortcuts(menu);
	drawView();
}

void MRMenuBar::setEditorFunctionKeysActive(bool active) {
	const bool startupActive = active ? false : mStartupFunctionKeysActive;
	if (mEditorFunctionKeysActive == active && mStartupFunctionKeysActive == startupActive) return;
	mEditorFunctionKeysActive = active;
	mStartupFunctionKeysActive = startupActive;
	applyFunctionKeyMenuShortcuts(mBaseMenu);
	applyFunctionKeyMenuShortcuts(menu);
	drawView();
}

void MRMenuBar::setPersistentBlocksMenuState(bool enabled) {
	const std::string wantedLabel = enabled ? "~P~ersistent blocks [ON]" : "~P~ersistent blocks [OFF]";
	TMenuItem *item = findMenuItemByCommand(menu, cmMrBlockPersistent);

	if (item == nullptr || item->command != cmMrBlockPersistent) return;
	if (item->name != nullptr && wantedLabel == item->name) return;
	delete[] const_cast<char *>(item->name);
	item->name = newStr(wantedLabel.c_str());
	drawView();
}

void MRMenuBar::setInsertModeMenuState(bool enabled) {
	const std::string wantedLabel = enabled ? "~I~nsert [ON]" : "~I~nsert [OFF]";
	TMenuItem *item = findMenuItemByCommand(menu, cmMrEditToggleInsertMode);

	if (item == nullptr || item->command != cmMrEditToggleInsertMode) return;
	if (item->name != nullptr && wantedLabel == item->name) return;
	delete[] const_cast<char *>(item->name);
	item->name = newStr(wantedLabel.c_str());
	drawView();
}

void MRMenuBar::setLineDrawingMenuState(bool enabled, bool doubleLines) {
	const std::string lineDrawingLabel = enabled ? "~L~ine drawing [ON]" : "~L~ine drawing [OFF]";
	const std::string doubleLinesLabel = doubleLines ? "~D~ouble lines [ON]" : "~D~ouble lines [OFF]";
	TMenuItem *lineDrawingItem = nullptr;
	TMenuItem *doubleLinesItem = findMenuItemByCommand(menu, cmMrTextToggleDoubleLines);
	bool changed = false;

	if (enabled && doubleLinesItem == nullptr) {
		changed = rebuildRuntimeMenu() || changed;
	} else if (!enabled && doubleLinesItem != nullptr) {
		changed = removeMenuItemByCommand(menu, cmMrTextToggleDoubleLines) || changed;
	}
	lineDrawingItem = findMenuItemByCommand(menu, cmMrTextToggleLineDrawing);
	doubleLinesItem = findMenuItemByCommand(menu, cmMrTextToggleDoubleLines);
	if (lineDrawingItem != nullptr && lineDrawingItem->command == cmMrTextToggleLineDrawing) {
		if (lineDrawingItem->name == nullptr || lineDrawingLabel != lineDrawingItem->name) {
			delete[] const_cast<char *>(lineDrawingItem->name);
			lineDrawingItem->name = newStr(lineDrawingLabel.c_str());
			changed = true;
		}
	}
	if (doubleLinesItem != nullptr && doubleLinesItem->command == cmMrTextToggleDoubleLines) {
		if (doubleLinesItem->name == nullptr || doubleLinesLabel != doubleLinesItem->name) {
			delete[] const_cast<char *>(doubleLinesItem->name);
			doubleLinesItem->name = newStr(doubleLinesLabel.c_str());
			changed = true;
		}
		if (doubleLinesItem->disabled) {
			doubleLinesItem->disabled = false;
			changed = true;
		}
	}
	if (changed) drawView();
}

void MRMenuBar::tickMarquee() {
	const int textLen = static_cast<int>(mMarqueeActiveText.size());
	auto now = std::chrono::steady_clock::now();
	const int visibleSpan = marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);

	if (mMarqueeLaneWidth <= 0 || textLen == 0) return;
	if (mMarqueeOutroActive) {
		const auto duration = marqueeIntroDuration();
		if (mMarqueeOutroStartedAt == std::chrono::steady_clock::time_point::min()) {
			mMarqueeOutroActive = false;
			mMarqueeOutroShift = 0;
		} else {
			const auto elapsed = now - mMarqueeOutroStartedAt;
			if (elapsed >= duration) {
				mMarqueeOutroActive = false;
				mMarqueeOutroShift = 0;
				mMarqueeOutroStartShift = 0;
				mMarqueeOutroStartedAt = std::chrono::steady_clock::time_point::min();
				if (mMarqueeHasPending) {
					mMarqueeActiveText = mMarqueePendingText;
					mMarqueeActiveSegments = mMarqueePendingSegments;
					mMarqueeActiveKind = mMarqueePendingKind;
					mMarqueeHasPending = false;
					mMarqueePendingText.clear();
					mMarqueePendingSegments.clear();
					mMarqueePendingKind = MarqueeKind::Info;
					mMarqueeOffset = std::max(0, static_cast<int>(mMarqueeActiveText.size()) - mMarqueeLaneWidth);
					mMarqueeDirection = -1;
					if (!mMarqueeActiveText.empty()) {
						mMarqueeIntroActive = true;
						mMarqueeIntroStartShift = marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);
						mMarqueeIntroShift = mMarqueeIntroStartShift;
						mMarqueeIntroStartedAt = now;
						mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
					} else {
						mMarqueeIntroActive = false;
						mMarqueeIntroShift = 0;
						mMarqueeIntroStartShift = 0;
						mMarqueeIntroStartedAt = std::chrono::steady_clock::time_point::min();
						mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
					}
				}
				drawView();
				return;
			}
			const long long durationMs = duration.count();
			const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
			int newShift = mMarqueeOutroStartShift;
			if (durationMs > 0) {
				newShift += static_cast<int>((static_cast<long long>(visibleSpan - mMarqueeOutroStartShift) * elapsedMs + durationMs - 1) / durationMs);
			}
			if (newShift > visibleSpan) newShift = visibleSpan;
			if (newShift != mMarqueeOutroShift) {
				mMarqueeOutroShift = newShift;
				drawView();
			}
			return;
		}
	}
	if (mMarqueeIntroActive) {
		const auto duration = marqueeIntroDuration();
		if (mMarqueeIntroStartedAt == std::chrono::steady_clock::time_point::min()) {
			mMarqueeIntroActive = false;
			mMarqueeIntroShift = 0;
		} else {
			const auto elapsed = now - mMarqueeIntroStartedAt;
			if (elapsed >= duration) {
				bool changed = mMarqueeIntroShift != 0;
				mMarqueeIntroActive = false;
				mMarqueeIntroShift = 0;
				mMarqueeScrollNextAt = textLen > mMarqueeLaneWidth ? now + marqueeScrollStartDelay() : std::chrono::steady_clock::time_point::min();
				if (changed) drawView();
				return;
			}
			const long long durationMs = duration.count();
			const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
			const long long remainingMs = durationMs - elapsedMs;
			int newShift = static_cast<int>((static_cast<long long>(mMarqueeIntroStartShift) * remainingMs + durationMs - 1) / durationMs);
			if (newShift < 0) newShift = 0;
			if (newShift != mMarqueeIntroShift) {
				mMarqueeIntroShift = newShift;
				drawView();
			}
			return;
		}
	}
	if (textLen <= mMarqueeLaneWidth) return;
	if (mMarqueeScrollNextAt == std::chrono::steady_clock::time_point::min()) {
		mMarqueeScrollNextAt = now + marqueeScrollStartDelay();
		return;
	}
	if (now < mMarqueeScrollNextAt) return;

	const int maxOffset = textLen - mMarqueeLaneWidth;
	if (mMarqueeDirection >= 0) {
		if (mMarqueeOffset < maxOffset) ++mMarqueeOffset;
		else
			mMarqueeDirection = -1;
	} else {
		if (mMarqueeOffset > 0) --mMarqueeOffset;
		else
			mMarqueeDirection = 1;
	}

	mMarqueeScrollNextAt = now + marqueeScrollStepInterval();
	drawView();
}

void MRMenuBar::draw() {
	TAttrPair color;
	short x, l;
	TMenuItem *p;
	TDrawBuffer b;

	TAttrPair cNormal = getColor(0x0301);
	TAttrPair cSelect = getColor(0x0604);
	TAttrPair cNormDisabled = getColor(0x0202);
	TAttrPair cSelDisabled = getColor(0x0505);
	TColorAttr cMenuBarHotkey = TColorAttr(cNormal);
	TColorAttr cStatus = TColorAttr(cNormal);
	TColorAttr cMarquee = TColorAttr(cNormal);
	MarqueeKind targetMarqueeKind = mManualMarqueeStatus.empty() ? mAutoMarqueeKind : mManualMarqueeKind;
	int rightLen = mRightStatus.empty() ? 0 : static_cast<int>(mRightStatus.size());
	const std::string &targetText = mManualMarqueeStatus.empty() ? mAutoMarqueeStatus : mManualMarqueeStatus;
	int rightStart = size.x;
	int menuEnd = 0;

	{
		const MRColorSetupSettings colors = configuredColorSetupSettings();
		unsigned char statusAttr = colors.otherColors[8];
		unsigned char menuBarHotkeyAttr = 0;

		(void)configuredColorSlotOverride(kMrPaletteCursorPositionMarker, statusAttr);
		(void)configuredColorSlotOverride(kMrPaletteMenuBarHotkey, menuBarHotkeyAttr);
		cMenuBarHotkey = TColorAttr(menuBarHotkeyAttr);
		cStatus = TColorAttr(statusAttr);
	}
	setStyle(cStatus, getStyle(cStatus) | slBold);

	b.moveChar(0, ' ', cNormal, size.x);
	if (rightLen != 0) rightStart = size.x - rightLen - 1;

	// Keep one blank column between the dynamic message lane and cursor status.
	const int menuLimit = std::max(1, rightStart - 2);

	if (menu != nullptr) {
		x = 1;
		p = menu->items;
		while (p != nullptr) {
			if (p->name != nullptr) {
				l = cstrlen(p->name);
				if (x + l < menuLimit) {
					if (p->disabled)
						if (p == current) color = cSelDisabled;
						else
							color = cNormDisabled;
					else if (p == current)
						color = cSelect;
					else
						color = cNormal;

					b.moveChar(x, ' ', color, 1);
					b.moveCStr(x + 1, p->name, color);
					{
						const int hotkeyColumn = markedHotkeyColumn(p->name);
						if (hotkeyColumn >= 0) b.putAttribute(static_cast<ushort>(x + 1 + hotkeyColumn), cMenuBarHotkey);
					}
					b.moveChar(x + l + 1, ' ', color, 1);
					menuEnd = x + l + 1;
				}
				x += l + 2;
			}
			p = p->next;
		}
	}

	// Dynamic top-line message lane between left menus and right cursor status.
	// We keep one blank before right status and render a marquee when text is wider than lane.
	int laneStart = std::max(1, menuEnd + 1);
	int laneEnd = rightStart - 2;
	mMarqueeLaneWidth = 0;
	if (laneStart <= laneEnd) {
		const int newLaneWidth = laneEnd - laneStart + 1;
		auto now = std::chrono::steady_clock::now();
		mMarqueeLaneWidth = newLaneWidth;
		const std::vector<MarqueeSegment> targetSegments = mManualMarqueeStatus.empty() ? mAutoMarqueeSegments : std::vector<MarqueeSegment>();
		if (targetText == mMarqueeActiveText && targetMarqueeKind == mMarqueeActiveKind && targetSegments == mMarqueeActiveSegments) {
			if (mMarqueeHasPending) {
				mMarqueeHasPending = false;
				mMarqueePendingText.clear();
				mMarqueePendingKind = MarqueeKind::Info;
			}
			if (mMarqueeOutroActive) {
				mMarqueeOutroActive = false;
				mMarqueeOutroShift = 0;
				mMarqueeOutroStartShift = 0;
				mMarqueeOutroStartedAt = std::chrono::steady_clock::time_point::min();
				mMarqueeScrollNextAt = !mMarqueeActiveText.empty() && static_cast<int>(mMarqueeActiveText.size()) > mMarqueeLaneWidth ? now + marqueeScrollStartDelay() : std::chrono::steady_clock::time_point::min();
			}
		} else {
			mMarqueeHasPending = true;
			mMarqueePendingText = targetText;
			mMarqueePendingSegments = targetSegments;
			mMarqueePendingKind = targetMarqueeKind;
			// No outgoing animation when there is no currently visible text.
			// Start the incoming animation immediately.
			if (mMarqueeActiveText.empty()) {
				mMarqueeActiveText = mMarqueePendingText;
				mMarqueeActiveSegments = mMarqueePendingSegments;
				mMarqueeActiveKind = mMarqueePendingKind;
				mMarqueeHasPending = false;
				mMarqueePendingText.clear();
				mMarqueePendingSegments.clear();
				mMarqueePendingKind = MarqueeKind::Info;
				mMarqueeOffset = std::max(0, static_cast<int>(mMarqueeActiveText.size()) - mMarqueeLaneWidth);
				mMarqueeDirection = -1;
				mMarqueeOutroActive = false;
				mMarqueeOutroShift = 0;
				mMarqueeOutroStartShift = 0;
				mMarqueeOutroStartedAt = std::chrono::steady_clock::time_point::min();
				if (!mMarqueeActiveText.empty()) {
					mMarqueeIntroActive = true;
					mMarqueeIntroStartShift = marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);
					mMarqueeIntroShift = mMarqueeIntroStartShift;
					mMarqueeIntroStartedAt = now;
					mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
				} else {
					mMarqueeIntroActive = false;
					mMarqueeIntroShift = 0;
					mMarqueeIntroStartShift = 0;
					mMarqueeIntroStartedAt = std::chrono::steady_clock::time_point::min();
					mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
				}
			} else if (!mMarqueeOutroActive) {
				const int visibleShiftSpan = marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);
				mMarqueeOutroActive = true;
				mMarqueeOutroStartShift = mMarqueeIntroActive ? mMarqueeIntroShift : 0;
				if (mMarqueeOutroStartShift < 0) mMarqueeOutroStartShift = 0;
				if (mMarqueeOutroStartShift > visibleShiftSpan) mMarqueeOutroStartShift = visibleShiftSpan;
				mMarqueeOutroShift = mMarqueeOutroStartShift;
				mMarqueeOutroStartedAt = now;
				mMarqueeIntroActive = false;
				mMarqueeIntroShift = 0;
				mMarqueeIntroStartShift = 0;
				mMarqueeIntroStartedAt = std::chrono::steady_clock::time_point::min();
				mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
			}
		}
		if (mMarqueeActiveText.empty() && mMarqueeHasPending && !mMarqueeOutroActive) {
			mMarqueeActiveText = mMarqueePendingText;
			mMarqueeActiveSegments = mMarqueePendingSegments;
			mMarqueeActiveKind = mMarqueePendingKind;
			mMarqueeHasPending = false;
			mMarqueePendingText.clear();
			mMarqueePendingSegments.clear();
			mMarqueePendingKind = MarqueeKind::Info;
			mMarqueeOffset = std::max(0, static_cast<int>(mMarqueeActiveText.size()) - mMarqueeLaneWidth);
			mMarqueeDirection = -1;
			if (!mMarqueeActiveText.empty()) {
				mMarqueeIntroActive = true;
				mMarqueeIntroStartShift = marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);
				mMarqueeIntroShift = mMarqueeIntroStartShift;
				mMarqueeIntroStartedAt = now;
				mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
			}
		}
		{
			const MRColorSetupSettings colors = configuredColorSetupSettings();
			unsigned char biosAttr = colors.otherColors[5]; // "message"
			unsigned char slot = kMrPaletteMessage;
			switch (mMarqueeActiveKind) {
				case MarqueeKind::Warning:
					slot = kMrPaletteMessageWarning;
					biosAttr = colors.otherColors[6];
					break;
				case MarqueeKind::Error:
					slot = kMrPaletteMessageError;
					biosAttr = colors.otherColors[4];
					break;
				case MarqueeKind::Hero:
					slot = kMrPaletteMessageHero;
					biosAttr = colors.otherColors[7];
					break;
				case MarqueeKind::Success:
				case MarqueeKind::Info:
				default:
					slot = kMrPaletteMessage;
					biosAttr = colors.otherColors[5];
					break;
			}
			// Primary source required by regression guard; array value remains fallback.
			if (configuredColorSlotOverride(slot, biosAttr)) cMarquee = TColorAttr(biosAttr);
			else
				cMarquee = TColorAttr(biosAttr);
		}
		auto colorForMarqueeKind = [](MarqueeKind kind) -> TColorAttr {
			const MRColorSetupSettings colors = configuredColorSetupSettings();
			unsigned char biosAttr = colors.otherColors[5];
			unsigned char slot = kMrPaletteMessage;

			switch (kind) {
				case MarqueeKind::Warning:
					slot = kMrPaletteMessageWarning;
					biosAttr = colors.otherColors[6];
					break;
				case MarqueeKind::Error:
					slot = kMrPaletteMessageError;
					biosAttr = colors.otherColors[4];
					break;
				case MarqueeKind::Hero:
					slot = kMrPaletteMessageHero;
					biosAttr = colors.otherColors[7];
					break;
				case MarqueeKind::Success:
				case MarqueeKind::Info:
				default:
					slot = kMrPaletteMessage;
					biosAttr = colors.otherColors[5];
					break;
			}
			if (configuredColorSlotOverride(slot, biosAttr)) return TColorAttr(biosAttr);
			return TColorAttr(biosAttr);
		};
		int marqueeTextLen = static_cast<int>(mMarqueeActiveText.size());
		int drawStart = laneStart;
		const char *drawPtr = mMarqueeActiveText.c_str();
		int drawLen = marqueeTextLen;
		int drawOffset = 0;

		if (marqueeTextLen <= 0) {
			// no-op
		} else if (marqueeTextLen <= mMarqueeLaneWidth) {
			drawStart = laneEnd - marqueeTextLen + 1;
		} else {
			const int maxOffset = marqueeTextLen - mMarqueeLaneWidth;
			if (mMarqueeOffset < 0) mMarqueeOffset = 0;
			if (mMarqueeOffset > maxOffset) mMarqueeOffset = maxOffset;
			drawPtr = mMarqueeActiveText.c_str() + mMarqueeOffset;
			drawLen = mMarqueeLaneWidth;
			drawOffset = mMarqueeOffset;
		}
		if (mMarqueeIntroActive) drawStart += mMarqueeIntroShift;
		else if (mMarqueeOutroActive)
			drawStart += mMarqueeOutroShift;
		if (drawStart <= laneEnd) {
			int visibleLen = laneEnd - drawStart + 1;
			if (visibleLen > drawLen) visibleLen = drawLen;
			if (visibleLen > 0 && mMarqueeActiveSegments.empty()) b.moveStr(static_cast<ushort>(drawStart), drawPtr, cMarquee, static_cast<ushort>(visibleLen));
			else if (visibleLen > 0) {
				int segmentStart = 0;
				int written = 0;

				for (const MarqueeSegment &segment : mMarqueeActiveSegments) {
					const int segmentLen = static_cast<int>(segment.text.size());
					const int segmentEnd = segmentStart + segmentLen;
					const int visibleStart = std::max(segmentStart, drawOffset);
					const int visibleEnd = std::min(segmentEnd, drawOffset + visibleLen);

					if (visibleEnd > visibleStart) {
						const int sourceOffset = visibleStart - segmentStart;
						const int targetOffset = visibleStart - drawOffset;
						const int count = visibleEnd - visibleStart;
						b.moveStr(static_cast<ushort>(drawStart + targetOffset), segment.text.c_str() + sourceOffset, colorForMarqueeKind(segment.kind), static_cast<ushort>(count));
						written += count;
					}
					segmentStart = segmentEnd;
				}
				if (written < visibleLen) b.moveStr(static_cast<ushort>(drawStart + written), drawPtr + written, cMarquee, static_cast<ushort>(visibleLen - written));
			}
		}
	} else {
		resetMarqueeState();
	}

	if (rightLen != 0) {
		int start = rightStart;
		if (start < 1) start = 1;
		b.moveStr(static_cast<ushort>(start), mRightStatus.c_str(), cStatus, static_cast<ushort>(std::min(rightLen, size.x - start)));
	}

	writeBuf(0, 0, size.x, 1, b);
	mrvmUiInvalidateScreenBase();
}
