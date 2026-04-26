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
#include "../config/MRDialogPaths.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../mrmac/mrvm.hpp"

void mrvmUiInvalidateScreenBase() noexcept;

namespace {
TMenuItem *findMenuItemByCommand(TMenu *menu, ushort command) {
	for (TMenuItem *item = menu != nullptr ? menu->items : nullptr; item != nullptr; item = item->next) {
		if (item->command == command)
			return item;
		if (item->command == 0) {
			TMenuItem *match = findMenuItemByCommand(item->subMenu, command);
			if (match != nullptr)
				return match;
		}
	}
	return nullptr;
}

std::size_t indexOfMenuItem(const TMenuItem *items, const TMenuItem *target) noexcept {
	std::size_t index = 0;

	for (const TMenuItem *item = items; item != nullptr; item = item->next, ++index)
		if (item == target)
			return index;
	return std::numeric_limits<std::size_t>::max();
}

TMenuItem *menuItemAt(TMenuItem *items, std::size_t index) noexcept {
	std::size_t currentIndex = 0;

	for (TMenuItem *item = items; item != nullptr; item = item->next, ++currentIndex)
		if (currentIndex == index)
			return item;
	return nullptr;
}

TMenu *cloneMenu(const TMenu *source);

TMenuItem *cloneMenuItem(const TMenuItem *source) {
	TMenuItem *cloned = nullptr;

	if (source == nullptr)
		return nullptr;
	if (source->name == nullptr && source->command == 0)
		cloned = &newLine();
	else if (source->command == 0 && source->subMenu != nullptr)
		cloned = new TMenuItem(source->name, source->keyCode, cloneMenu(source->subMenu), source->helpCtx);
	else
		cloned = new TMenuItem(source->name != nullptr ? source->name : "", source->command,
		                       source->keyCode, source->helpCtx,
		                       source->param != nullptr ? source->param : "");

	cloned->disabled = source->disabled;
	return cloned;
}

TMenuItem *cloneMenuItems(const TMenuItem *source) {
	TMenuItem *head = nullptr;
	TMenuItem *tail = nullptr;

	for (const TMenuItem *item = source; item != nullptr; item = item->next) {
		TMenuItem *cloned = cloneMenuItem(item);

		if (head == nullptr)
			head = cloned;
		else
			tail->next = cloned;
		tail = cloned;
	}
	return head;
}

TMenu *cloneMenu(const TMenu *source) {
	TMenu *cloned = nullptr;
	std::size_t defaultIndex = 0;

	if (source == nullptr)
		return nullptr;

	defaultIndex = indexOfMenuItem(source->items, source->deflt);
	cloned = new TMenu();
	cloned->items = cloneMenuItems(source->items);
	cloned->deflt = menuItemAt(cloned->items, defaultIndex);
	if (cloned->deflt == nullptr)
		cloned->deflt = cloned->items;
	return cloned;
}

void appendMenuItem(TMenu *menu, TMenuItem *item) {
	TMenuItem *tail = nullptr;

	if (menu == nullptr || item == nullptr)
		return;
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
	if (ch >= 'A' && ch <= 'Z')
		return ch - 'A';
	if (ch >= '0' && ch <= '9')
		return 26 + (ch - '0');
	return -1;
}

char canonicalHotkeyChar(char ch) noexcept {
	unsigned char uch = static_cast<unsigned char>(ch);
	if (std::isalpha(uch) != 0)
		return static_cast<char>(std::toupper(uch));
	if (std::isdigit(uch) != 0)
		return static_cast<char>(uch);
	return '\0';
}

char markedHotkeyChar(const char *name) noexcept {
	if (name == nullptr)
		return '\0';
	for (const char *pos = name; pos[0] != '\0' && pos[1] != '\0' && pos[2] != '\0'; ++pos)
		if (pos[0] == '~' && pos[2] == '~')
			return canonicalHotkeyChar(pos[1]);
	return '\0';
}

void markUsedHotkey(std::array<bool, 36> &usedHotkeys, char hotkey) noexcept {
	const int index = hotkeyIndex(canonicalHotkeyChar(hotkey));

	if (index >= 0)
		usedHotkeys[static_cast<std::size_t>(index)] = true;
}

char chooseMenuHotkey(const std::string &title, const std::array<bool, 36> &usedHotkeys) {
	for (char ch : title) {
		const char hotkey = canonicalHotkeyChar(ch);
		const int index = hotkeyIndex(hotkey);

		if (hotkey != '\0' && index >= 0 && !usedHotkeys[static_cast<std::size_t>(index)])
			return hotkey;
	}
	return '\0';
}

std::string menuTitleWithHotkeyMarker(const std::string &title, char hotkey) {
	std::string marked = title;
	const char canonical = canonicalHotkeyChar(hotkey);

	if (canonical == '\0')
		return marked;
	for (std::size_t i = 0; i < marked.size(); ++i)
		if (canonicalHotkeyChar(marked[i]) == canonical) {
			marked.insert(i, 1, '~');
			marked.insert(i + 2, 1, '~');
			return marked;
		}
	return marked;
}
} // namespace

MRMenuBar::MRMenuBar(const TRect &r, TSubMenu &aMenu)
    : TMenuBar(r, aMenu), mBaseMenu(nullptr), mRuntimeNodes(), mRightStatus(),
      mAutoMarqueeStatus(), mManualMarqueeStatus(), mAutoMarqueeKind(MarqueeKind::Info) {
	mBaseMenu = cloneMenu(menu);
}

MRMenuBar::~MRMenuBar() {
	delete mBaseMenu;
	mBaseMenu = nullptr;
}

std::string MRMenuBar::trimAscii(std::string value) {
	auto isTrimChar = [](unsigned char ch) noexcept {
		return std::isspace(ch) != 0 || ch < 32;
	};

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

	if (canonicalOwner.empty() || canonicalFile.empty() || caretPos == std::string::npos)
		return false;
	{
		const std::size_t searchEnd = caretPos == 0 ? 0 : caretPos - 1;
		const std::size_t slashPos = canonicalOwner.find_last_of("/\\", searchEnd);
		const std::size_t nameStart = slashPos == std::string::npos ? 0 : slashPos + 1;

		ownerFile = canonicalOwner.substr(nameStart, caretPos - nameStart);
	}
	if (canonicalOwner.substr(0, caretPos) == canonicalFile)
		return true;
	return ownerFile == canonicalFile;
}

bool MRMenuBar::allocateRuntimeCommand(ushort &command, std::string *errorMessage) {
	if (mNextRuntimeCommand == std::numeric_limits<ushort>::max()) {
		if (errorMessage != nullptr)
			*errorMessage = "No runtime menu command ids left.";
		return false;
	}
	command = mNextRuntimeCommand++;
	return true;
}

int MRMenuBar::findRuntimeNodeIndex(const std::string &menuKey, const std::string &itemKey,
                                    const std::string &ownerSpec) const noexcept {
	const auto it = std::find_if(mRuntimeNodes.begin(), mRuntimeNodes.end(),
	                             [&](const RuntimeMenuNode &node) {
		                             return node.menuKey == menuKey && node.itemKey == itemKey &&
		                                    node.ownerSpec == ownerSpec;
	                             });

	if (it == mRuntimeNodes.end())
		return -1;
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

	if (rebuilt == nullptr)
		return false;
	for (const TMenuItem *item = rebuilt->items; item != nullptr; item = item->next)
		markUsedHotkey(usedHotkeys, markedHotkeyChar(item->name));
	for (const RuntimeMenuNode &node : mRuntimeNodes) {
		auto it = std::find_if(groups.begin(), groups.end(),
		                       [&](const RuntimeMenuGroup &group) { return group.menuKey == node.menuKey; });
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
	std::sort(groups.begin(), groups.end(), [](const RuntimeMenuGroup &left, const RuntimeMenuGroup &right) {
		return left.order < right.order;
	});
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
			if (keyLabel.empty())
				keyLabel = mrvmUiMenuKeyLabelForMacroSpec(node->ownerSpec);
			appendMenuItem(submenu, new TMenuItem(node->itemTitle.c_str(), node->command, kbNoKey,
			                                     hcNoContext,
			                                     keyLabel.empty() ? nullptr : keyLabel.c_str()));
		}
		markUsedHotkey(usedHotkeys, groupHotkey);
		groupTitle = menuTitleWithHotkeyMarker(groupTitle, groupHotkey);
		appendMenuItem(rebuilt, new TMenuItem(groupTitle.c_str(),
		                                      groupHotkey == '\0'
		                                          ? TKey(kbNoKey)
		                                          : TKey(static_cast<ushort>(groupHotkey), kbAltShift),
		                                      submenu, hcNoContext));
	}

	current = nullptr;
	delete menu;
	menu = rebuilt;
	return true;
}

bool MRMenuBar::registerRuntimeMenuItem(const std::string &menuTitle, const std::string &itemTitle,
                                        const std::string &macroSpec, const std::string &ownerSpec,
                                        std::string *errorMessage) {
	RuntimeMenuNode node;

	node.menuTitle = trimAscii(menuTitle);
	node.menuKey = canonicalMenuToken(menuTitle);
	node.itemTitle = trimAscii(itemTitle);
	node.itemKey = canonicalMenuToken(itemTitle);
	node.ownerSpec = trimAscii(ownerSpec);
	node.macroSpec = trimAscii(macroSpec);
	if (node.menuKey.empty()) {
		if (errorMessage != nullptr)
			*errorMessage = "REGISTER_MENU_ITEM requires a non-empty menu title.";
		return false;
	}
	if (node.macroSpec.empty() || node.ownerSpec.empty()) {
		if (errorMessage != nullptr)
			*errorMessage = "REGISTER_MENU_ITEM requires macro and owner context.";
		return false;
	}
	if (node.itemKey.empty())
		node.itemKey = "SEP_" + std::to_string(mNextRuntimeOrder + 1);
	if (findRuntimeNodeIndex(node.menuKey, node.itemKey, node.ownerSpec) >= 0) {
		if (errorMessage != nullptr)
			*errorMessage = "REGISTER_MENU_ITEM is already registered by this macro.";
		return false;
	}
	node.kind = node.itemTitle.empty() ? RuntimeMenuNodeKind::Separator : RuntimeMenuNodeKind::Item;
	if (node.kind == RuntimeMenuNodeKind::Item && !allocateRuntimeCommand(node.command, errorMessage))
		return false;
	node.order = ++mNextRuntimeOrder;
	mRuntimeNodes.push_back(std::move(node));
	if (!rebuildRuntimeMenu()) {
		mRuntimeNodes.pop_back();
		if (errorMessage != nullptr)
			*errorMessage = "REGISTER_MENU_ITEM could not rebuild the menu bar.";
		return false;
	}
	drawView();
	return true;
}

bool MRMenuBar::refreshRuntimeMenus(std::string *errorMessage) {
	if (!rebuildRuntimeMenu()) {
		if (errorMessage != nullptr)
			*errorMessage = "Could not rebuild runtime menus.";
		return false;
	}
	drawView();
	return true;
}

bool MRMenuBar::removeRuntimeMenuItem(const std::string &menuTitle, const std::string &itemTitle,
                                      const std::string &ownerSpec,
                                      std::string *errorMessage) {
	const std::string menuKey = canonicalMenuToken(menuTitle);
	const std::string itemKey = canonicalMenuToken(itemTitle);
	const std::string owner = trimAscii(ownerSpec);
	int index = -1;
	std::vector<RuntimeMenuNode> previousNodes = mRuntimeNodes;

	if (menuKey.empty()) {
		if (errorMessage != nullptr)
			*errorMessage = "REMOVE_MENU_ITEM requires a non-empty menu title.";
		return false;
	}
	for (std::size_t i = 0; i < mRuntimeNodes.size(); ++i) {
		const RuntimeMenuNode &node = mRuntimeNodes[i];

		if (node.ownerSpec != owner || node.menuKey != menuKey)
			continue;
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
		if (errorMessage != nullptr)
			*errorMessage = "REMOVE_MENU_ITEM references no item owned by the current macro.";
		return false;
	}
	mRuntimeNodes.erase(mRuntimeNodes.begin() + index);
	if (!rebuildRuntimeMenu()) {
		mRuntimeNodes = std::move(previousNodes);
		static_cast<void>(rebuildRuntimeMenu());
		if (errorMessage != nullptr)
			*errorMessage = "REMOVE_MENU_ITEM could not rebuild the menu bar.";
		return false;
	}
	drawView();
	return true;
}

bool MRMenuBar::removeRuntimeNodesOwnedByMacroSpec(const std::string &ownerSpec,
                                                   std::string *errorMessage) {
	const std::string owner = trimAscii(ownerSpec);
	std::vector<RuntimeMenuNode> previousNodes = mRuntimeNodes;

	if (owner.empty())
		return true;
	mRuntimeNodes.erase(std::remove_if(mRuntimeNodes.begin(), mRuntimeNodes.end(),
	                                   [&](const RuntimeMenuNode &node) {
		                                   return node.ownerSpec == owner;
	                                   }),
	                    mRuntimeNodes.end());
	if (!rebuildRuntimeMenu()) {
		mRuntimeNodes = std::move(previousNodes);
		static_cast<void>(rebuildRuntimeMenu());
		if (errorMessage != nullptr)
			*errorMessage = "Unable to rebuild the menu bar after macro cleanup.";
		return false;
	}
	drawView();
	return true;
}

bool MRMenuBar::removeRuntimeNodesOwnedByFile(const std::string &fileSpec, std::string *errorMessage) {
	const std::string fileId = trimAscii(fileSpec);
	std::vector<RuntimeMenuNode> previousNodes = mRuntimeNodes;

	if (fileId.empty())
		return true;
	mRuntimeNodes.erase(std::remove_if(mRuntimeNodes.begin(), mRuntimeNodes.end(),
	                                   [&](const RuntimeMenuNode &node) {
		                                   return ownerSpecMatchesFile(node.ownerSpec, fileId);
	                                   }),
	                    mRuntimeNodes.end());
	if (!rebuildRuntimeMenu()) {
		mRuntimeNodes = std::move(previousNodes);
		static_cast<void>(rebuildRuntimeMenu());
		if (errorMessage != nullptr)
			*errorMessage = "Unable to rebuild the menu bar after file cleanup.";
		return false;
	}
	drawView();
	return true;
}

bool MRMenuBar::handleRuntimeCommand(ushort command) {
	const auto it = std::find_if(mRuntimeNodes.begin(), mRuntimeNodes.end(),
	                             [&](const RuntimeMenuNode &node) {
		                             return node.kind == RuntimeMenuNodeKind::Item &&
		                                    node.command == command;
	                             });

	if (it == mRuntimeNodes.end())
		return false;
	static_cast<void>(runMacroSpecByName(it->macroSpec.c_str(), nullptr, true));
	mrvmUiInvalidateScreenBase();
	return true;
}

void MRMenuBar::setPersistentBlocksMenuState(bool enabled) {
	const std::string wantedLabel = enabled ? "~P~ersistent blocks [ON]" : "~P~ersistent blocks [OFF]";
	TMenuItem *item = findMenuItemByCommand(menu, cmMrBlockPersistent);

	if (item == nullptr || item->command != cmMrBlockPersistent)
		return;
	if (item->name != nullptr && wantedLabel == item->name)
		return;
	delete[] const_cast<char *>(item->name);
	item->name = newStr(wantedLabel.c_str());
	drawView();
}

void MRMenuBar::tickMarquee() {
	const int textLen = static_cast<int>(mMarqueeActiveText.size());
	auto now = std::chrono::steady_clock::now();
	const int visibleSpan = marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);

	if (mMarqueeLaneWidth <= 0 || textLen == 0)
		return;
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
					mMarqueeActiveKind = mMarqueePendingKind;
					mMarqueeHasPending = false;
					mMarqueePendingText.clear();
					mMarqueePendingKind = MarqueeKind::Info;
					mMarqueeOffset =
					    std::max(0, static_cast<int>(mMarqueeActiveText.size()) - mMarqueeLaneWidth);
					mMarqueeDirection = -1;
					if (!mMarqueeActiveText.empty()) {
						mMarqueeIntroActive = true;
						mMarqueeIntroStartShift =
						    marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);
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
			const long long elapsedMs =
			    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
			int newShift = mMarqueeOutroStartShift;
			if (durationMs > 0) {
				newShift += static_cast<int>(
				    (static_cast<long long>(visibleSpan - mMarqueeOutroStartShift) * elapsedMs +
				     durationMs - 1) /
				    durationMs);
			}
			if (newShift > visibleSpan)
				newShift = visibleSpan;
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
				mMarqueeScrollNextAt =
				    textLen > mMarqueeLaneWidth ? now + marqueeScrollStartDelay()
				                                : std::chrono::steady_clock::time_point::min();
				if (changed)
					drawView();
				return;
			}
			const long long durationMs = duration.count();
			const long long elapsedMs =
			    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
			const long long remainingMs = durationMs - elapsedMs;
			int newShift = static_cast<int>(
			    (static_cast<long long>(mMarqueeIntroStartShift) * remainingMs + durationMs - 1) /
			    durationMs);
			if (newShift < 0)
				newShift = 0;
			if (newShift != mMarqueeIntroShift) {
				mMarqueeIntroShift = newShift;
				drawView();
			}
			return;
		}
	}
	if (textLen <= mMarqueeLaneWidth)
		return;
	if (mMarqueeScrollNextAt == std::chrono::steady_clock::time_point::min()) {
		mMarqueeScrollNextAt = now + marqueeScrollStartDelay();
		return;
	}
	if (now < mMarqueeScrollNextAt)
		return;

	const int maxOffset = textLen - mMarqueeLaneWidth;
	if (mMarqueeDirection >= 0) {
		if (mMarqueeOffset < maxOffset)
			++mMarqueeOffset;
		else
			mMarqueeDirection = -1;
	} else {
		if (mMarqueeOffset > 0)
			--mMarqueeOffset;
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
	TColorAttr cStatus = TColorAttr(cNormal);
	TColorAttr cMarquee = TColorAttr(cNormal);
	MarqueeKind targetMarqueeKind = mManualMarqueeStatus.empty() ? mAutoMarqueeKind : mManualMarqueeKind;
	int rightLen = mRightStatus.empty() ? 0 : static_cast<int>(mRightStatus.size());
	const std::string &targetText = mManualMarqueeStatus.empty() ? mAutoMarqueeStatus : mManualMarqueeStatus;
	int rightStart = size.x;
	int menuEnd = 0;

	{
		const MRColorSetupSettings colors = configuredColorSetupSettings();
		unsigned char biosAttr = colors.otherColors[8];
		(void)configuredColorSlotOverride(kMrPaletteCursorPositionMarker, biosAttr);
		cStatus = TColorAttr(biosAttr);
	}
	setStyle(cStatus, getStyle(cStatus) | slBold);

	b.moveChar(0, ' ', cNormal, size.x);
	if (rightLen != 0)
		rightStart = size.x - rightLen - 1;

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
						if (p == current)
							color = cSelDisabled;
						else
							color = cNormDisabled;
					else if (p == current)
						color = cSelect;
					else
						color = cNormal;

					b.moveChar(x, ' ', color, 1);
					b.moveCStr(x + 1, p->name, color);
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
		if (targetText == mMarqueeActiveText && targetMarqueeKind == mMarqueeActiveKind) {
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
				mMarqueeScrollNextAt =
				    !mMarqueeActiveText.empty() &&
				            static_cast<int>(mMarqueeActiveText.size()) > mMarqueeLaneWidth
				        ? now + marqueeScrollStartDelay()
				        : std::chrono::steady_clock::time_point::min();
			}
		} else {
			mMarqueeHasPending = true;
			mMarqueePendingText = targetText;
			mMarqueePendingKind = targetMarqueeKind;
			// No outgoing animation when there is no currently visible text.
			// Start the incoming animation immediately.
			if (mMarqueeActiveText.empty()) {
				mMarqueeActiveText = mMarqueePendingText;
				mMarqueeActiveKind = mMarqueePendingKind;
				mMarqueeHasPending = false;
				mMarqueePendingText.clear();
				mMarqueePendingKind = MarqueeKind::Info;
				mMarqueeOffset =
				    std::max(0, static_cast<int>(mMarqueeActiveText.size()) - mMarqueeLaneWidth);
				mMarqueeDirection = -1;
				mMarqueeOutroActive = false;
					mMarqueeOutroShift = 0;
					mMarqueeOutroStartShift = 0;
					mMarqueeOutroStartedAt = std::chrono::steady_clock::time_point::min();
					if (!mMarqueeActiveText.empty()) {
						mMarqueeIntroActive = true;
						mMarqueeIntroStartShift =
						    marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);
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
					const int visibleShiftSpan =
					    marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);
					mMarqueeOutroActive = true;
					mMarqueeOutroStartShift = mMarqueeIntroActive ? mMarqueeIntroShift : 0;
					if (mMarqueeOutroStartShift < 0)
						mMarqueeOutroStartShift = 0;
					if (mMarqueeOutroStartShift > visibleShiftSpan)
						mMarqueeOutroStartShift = visibleShiftSpan;
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
			mMarqueeActiveKind = mMarqueePendingKind;
			mMarqueeHasPending = false;
			mMarqueePendingText.clear();
			mMarqueePendingKind = MarqueeKind::Info;
			mMarqueeOffset =
			    std::max(0, static_cast<int>(mMarqueeActiveText.size()) - mMarqueeLaneWidth);
			mMarqueeDirection = -1;
			if (!mMarqueeActiveText.empty()) {
				mMarqueeIntroActive = true;
				mMarqueeIntroStartShift =
				    marqueeVisibleSpanFor(mMarqueeActiveText, mMarqueeLaneWidth);
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
			if (configuredColorSlotOverride(slot, biosAttr))
				cMarquee = TColorAttr(biosAttr);
			else
				cMarquee = TColorAttr(biosAttr);
		}
		int marqueeTextLen = static_cast<int>(mMarqueeActiveText.size());
		int drawStart = laneStart;
		const char *drawPtr = mMarqueeActiveText.c_str();
		int drawLen = marqueeTextLen;

		if (marqueeTextLen <= 0) {
			// no-op
		} else if (marqueeTextLen <= mMarqueeLaneWidth) {
			drawStart = laneEnd - marqueeTextLen + 1;
		} else {
			const int maxOffset = marqueeTextLen - mMarqueeLaneWidth;
			if (mMarqueeOffset < 0)
				mMarqueeOffset = 0;
			if (mMarqueeOffset > maxOffset)
				mMarqueeOffset = maxOffset;
			drawPtr = mMarqueeActiveText.c_str() + mMarqueeOffset;
			drawLen = mMarqueeLaneWidth;
		}
		if (mMarqueeIntroActive)
			drawStart += mMarqueeIntroShift;
		else if (mMarqueeOutroActive)
			drawStart += mMarqueeOutroShift;
		if (drawStart <= laneEnd) {
			int visibleLen = laneEnd - drawStart + 1;
			if (visibleLen > drawLen)
				visibleLen = drawLen;
			if (visibleLen > 0)
				b.moveStr(static_cast<ushort>(drawStart), drawPtr, cMarquee,
				          static_cast<ushort>(visibleLen));
		}
	} else {
		resetMarqueeState();
	}

	if (rightLen != 0) {
		int start = rightStart;
		if (start < 1)
			start = 1;
		b.moveStr(static_cast<ushort>(start), mRightStatus.c_str(), cStatus,
		          static_cast<ushort>(std::min(rightLen, size.x - start)));
	}

	writeBuf(0, 0, size.x, 1, b);
	mrvmUiInvalidateScreenBase();
}
