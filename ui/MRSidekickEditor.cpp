#define Uses_TDrawBuffer
#define Uses_TDeskTop
#define Uses_TButton
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TScrollBar
#define Uses_TScroller
#define Uses_TView
#define Uses_TWindow
#include <tvision/tv.h>

#include "MRSidekickEditor.hpp"
#include "MRSidekickInternal.hpp"
#include "MRSnippet.hpp"

#include "MREditWindow.hpp"
#include "MRFrame.hpp"
#include "MRFileEditor/MRFileEditor.hpp"
#include "MRWindowSupport.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../app/MREditorApp.hpp"
#include "../app/MRCommands.hpp"
#include "../app/MRHelpTopics.generated.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#include "../keymap/MRKeymapContext.hpp"
#include "../keymap/MRKeymapResolver.hpp"
#include "../keymap/MRKeymapToken.hpp"
#include "../mrmac/vm/MRVMRuntimeState.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <utility>

using mr::sidekick_internal::ReadOnlyMarker;
using mr::sidekick_internal::readOnlySidekickBoundsFor;
using mr::sidekick_internal::readOnlyTextWithMarker;
using mr::sidekick_internal::romBelow;
using mr::sidekick_internal::sidekickColor;
using mr::sidekick_internal::sidekickMaxLineLength;
using mr::sidekick_internal::splitLines;

namespace {

MRSidekickEditor *gActiveSidekick = nullptr;

constexpr TColorAttr kSidekickCursor = 0x70;

class MRSidekickScrollBar final : public TScrollBar {
  public:
	explicit MRSidekickScrollBar(const TRect &bounds) noexcept : TScrollBar(bounds) {
	}

	TColorAttr mapColor(uchar) override {
		return sidekickColor(kMrPaletteSidekickScrollBar, 0x30);
	}
};

} // namespace

MRSidekickEditor::MRSidekickEditor(const TRect &bounds, int parentBufferId, std::string text, std::string title, bool readOnly, bool modalClose, MRSidekickPalette palette)
    : TScroller(bounds, nullptr, nullptr), mParentBufferId(parentBufferId), mTitle(std::move(title)), mLines(), mCursorRow(0), mCursorCol(0), mReadOnly(readOnly), mModalClose(modalClose), mPalette(palette), mOuterBounds(bounds), mHorizontalScrollBar(nullptr), mVerticalScrollBar(nullptr) {
	if (mReadOnly) options &= ~ofSelectable;
	growMode = gfGrowHiX | gfGrowHiY;
	eventMask |= evKeyDown | evMouseDown | evMouseWheel;
	setText(std::move(text));
}

MRSidekickEditor::~MRSidekickEditor() {
	MRSnippet::instance().detach(this);
	if (gActiveSidekick == this) gActiveSidekick = nullptr;
}

void MRSidekickEditor::insertInto(TGroup &group) {
	if (mHorizontalScrollBar == nullptr && mVerticalScrollBar == nullptr) updateScrollBars(mOuterBounds);
	if (mHorizontalScrollBar != nullptr && mHorizontalScrollBar->owner != nullptr) mHorizontalScrollBar->owner->remove(mHorizontalScrollBar);
	if (mVerticalScrollBar != nullptr && mVerticalScrollBar->owner != nullptr) mVerticalScrollBar->owner->remove(mVerticalScrollBar);
	if (owner != nullptr) owner->remove(this);
	group.insert(this);
	if (mHorizontalScrollBar != nullptr) group.insert(mHorizontalScrollBar);
	if (mVerticalScrollBar != nullptr) group.insert(mVerticalScrollBar);
	setLimit(sidekickMaxLineLength(mLines) + (mReadOnly ? 0 : 2), static_cast<int>(mLines.size()));
}

void MRSidekickEditor::updateScrollBars(const TRect &bounds) {
	TGroup *group = owner;
	const int contentWidth = sidekickMaxLineLength(mLines) + (mReadOnly ? 0 : 2);
	const int contentHeight = static_cast<int>(mLines.size());
	const int outerWidth = std::max(1, bounds.b.x - bounds.a.x);
	const int outerHeight = std::max(1, bounds.b.y - bounds.a.y);
	bool horizontalVisible = false;
	bool verticalVisible = false;
	bool changed;

	do {
		const int visibleWidth = std::max(1, outerWidth - (verticalVisible ? 1 : 0));
		const int visibleHeight = std::max(1, outerHeight - (horizontalVisible ? 1 : 0));
		const bool nextHorizontalVisible = outerHeight > 1 && contentWidth > visibleWidth;
		const bool nextVerticalVisible = outerWidth > 1 && contentHeight > visibleHeight;

		changed = nextHorizontalVisible != horizontalVisible || nextVerticalVisible != verticalVisible;
		horizontalVisible = nextHorizontalVisible;
		verticalVisible = nextVerticalVisible;
	} while (changed);
	if (bounds == mOuterBounds && horizontalVisible == (mHorizontalScrollBar != nullptr) && verticalVisible == (mVerticalScrollBar != nullptr)) {
		setLimit(contentWidth, contentHeight);
		return;
	}

	if (group != nullptr) group->lock();
	destroyScrollBars();
	delta.x = 0;
	delta.y = 0;
	mOuterBounds = bounds;
	TRect viewBounds = bounds;
	if (verticalVisible) --viewBounds.b.x;
	if (horizontalVisible) --viewBounds.b.y;
	if (viewBounds.b.x <= viewBounds.a.x) viewBounds.b.x = viewBounds.a.x + 1;
	if (viewBounds.b.y <= viewBounds.a.y) viewBounds.b.y = viewBounds.a.y + 1;
	TScroller::changeBounds(viewBounds);

	if (horizontalVisible) {
		mHorizontalScrollBar = new MRSidekickScrollBar(TRect(bounds.a.x, viewBounds.b.y, viewBounds.b.x, bounds.b.y));
		mHorizontalScrollBar->eventMask &= ~evMouseWheel;
		mHorizontalScrollBar->growMode = gfGrowHiX | gfGrowLoY | gfGrowHiY;
		hScrollBar = mHorizontalScrollBar;
	}
	if (verticalVisible) {
		mVerticalScrollBar = new MRSidekickScrollBar(TRect(viewBounds.b.x, bounds.a.y, bounds.b.x, viewBounds.b.y));
		mVerticalScrollBar->eventMask &= ~evMouseWheel;
		mVerticalScrollBar->growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
		vScrollBar = mVerticalScrollBar;
	}
	if (group != nullptr) {
		if (mHorizontalScrollBar != nullptr) group->insert(mHorizontalScrollBar);
		if (mVerticalScrollBar != nullptr) group->insert(mVerticalScrollBar);
	}
	setLimit(contentWidth, contentHeight);
	if (group != nullptr) group->unlock();
}

void MRSidekickEditor::destroyScrollBars() {
	TScrollBar *horizontalScrollBar = mHorizontalScrollBar;
	TScrollBar *verticalScrollBar = mVerticalScrollBar;

	hScrollBar = nullptr;
	vScrollBar = nullptr;
	mHorizontalScrollBar = nullptr;
	mVerticalScrollBar = nullptr;
	if (horizontalScrollBar != nullptr && horizontalScrollBar->owner != nullptr) horizontalScrollBar->owner->remove(horizontalScrollBar);
	if (verticalScrollBar != nullptr && verticalScrollBar->owner != nullptr) verticalScrollBar->owner->remove(verticalScrollBar);
	TObject::destroy(horizontalScrollBar);
	TObject::destroy(verticalScrollBar);
}

void MRSidekickEditor::detachFromOwner() {
	destroyScrollBars();
	if (owner != nullptr) owner->remove(this);
}

void MRSidekickEditor::ensureCursorVisible() {
	const int cursorX = mCursorCol + (mReadOnly ? 0 : 1);
	int x = delta.x;
	int y = delta.y;

	if (cursorX < x) x = cursorX;
	else if (cursorX >= x + size.x) x = cursorX - size.x + 1;
	if (mCursorRow < y) y = mCursorRow;
	else if (mCursorRow >= y + size.y) y = mCursorRow - size.y + 1;
	scrollTo(x, y);
}

int MRSidekickEditor::parentBufferId() const noexcept {
	return mParentBufferId;
}

bool MRSidekickEditor::isReadOnly() const noexcept {
	return mReadOnly;
}

void MRSidekickEditor::setText(std::string textValue) {
	mLines = splitLines(textValue);
	if (mLines.empty()) mLines.push_back(std::string());
	mCursorRow = 0;
	mCursorCol = 0;
	clampCursor();
}

void MRSidekickEditor::updateReadOnlyText(std::string textValue, std::string title, const TRect &bounds) {
	if (!mReadOnly) return;
	const TPoint previousScroll = delta;
	mTitle = std::move(title);
	setText(std::move(textValue));
	updateScrollBars(bounds);
	scrollTo(previousScroll.x, previousScroll.y);
	drawView();
}

std::string MRSidekickEditor::text() const {
	std::ostringstream out;

	for (std::size_t i = 0; i < mLines.size(); ++i) {
		if (i != 0) out << '\n';
		out << mLines[i];
	}
	return out.str();
}

void MRSidekickEditor::draw() {
	MRSnippet &snippet = MRSnippet::instance();
	const bool snippetEditor = snippet.isEditor(this);
	const MRSnippet::DrawColors colors = snippet.drawColors(*this, getColor(1));
	const TColorAttr textColor = colors.text;
	std::size_t lineStartOffset = 0;
	for (int y = 0; y < delta.y && y < static_cast<int>(mLines.size()); ++y)
		lineStartOffset += mLines[static_cast<std::size_t>(y)].size() + 1;

	for (int y = 0; y < size.y; ++y) {
		TDrawBuffer buffer;
		const int lineIndex = delta.y + y;
		buffer.moveChar(0, ' ', textColor, size.x);
		if (lineIndex < static_cast<int>(mLines.size())) {
			const std::string &line = mLines[static_cast<std::size_t>(lineIndex)];
			const int textX = mReadOnly ? 0 : 1;
			if (mReadOnly) {
				buffer.moveStr(0, line.c_str(), textColor, static_cast<ushort>(size.x), static_cast<ushort>(delta.x));
			} else {
				for (int screenX = 0; screenX < size.x; ++screenX) {
					const int textColumn = delta.x + screenX - textX;
					if (textColumn < 0 || textColumn >= static_cast<int>(line.size())) continue;
					const std::size_t offset = lineStartOffset + static_cast<std::size_t>(textColumn);
					TColorAttr charColor = textColor;
					if (snippetEditor) charColor = snippet.placeholderColor(offset, colors);
					const unsigned char raw = static_cast<unsigned char>(line[static_cast<std::size_t>(textColumn)]);
					buffer.moveChar(static_cast<ushort>(screenX), raw < 32 ? ' ' : line[static_cast<std::size_t>(textColumn)], charColor, 1);
				}
			}
		}
		if (!mReadOnly && lineIndex == mCursorRow) {
			const int cursorX = mCursorCol + 1 - delta.x;
			char cursorChar = (mCursorCol >= 0 && mCursorCol < static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow)].size())) ? mLines[static_cast<std::size_t>(mCursorRow)][static_cast<std::size_t>(mCursorCol)] : ' ';
			if (static_cast<unsigned char>(cursorChar) < 32) cursorChar = ' ';
			if (cursorX >= 0 && cursorX < size.x) buffer.moveChar(static_cast<ushort>(cursorX), cursorChar, kSidekickCursor, 1);
		}
		writeLine(0, static_cast<short>(y), size.x, 1, buffer);
		if (lineIndex < static_cast<int>(mLines.size())) lineStartOffset += mLines[static_cast<std::size_t>(lineIndex)].size() + 1;
	}
}

void MRSidekickEditor::handleEvent(TEvent &event) {
	if (event.what == evMouseWheel && containsMouse(event)) {
		switch (event.mouse.wheel) {
			case mwUp: scrollTo(delta.x, delta.y - 3); break;
			case mwDown: scrollTo(delta.x, delta.y + 3); break;
			case mwLeft: scrollTo(delta.x - 3, delta.y); break;
			case mwRight: scrollTo(delta.x + 3, delta.y); break;
			default: return;
		}
		clearEvent(event);
		return;
	}
	if (event.what == evMouseDown) {
		if (containsMouse(event)) {
			TPoint local = makeLocal(event.mouse.where);
			mCursorRow = std::clamp<int>(delta.y + local.y, 0, static_cast<int>(mLines.size()) - 1);
			mCursorCol = std::max(0, delta.x + local.x - 1);
			clampCursor();
			drawView();
			clearEvent(event);
		}
		return;
	}
	if (event.what != evKeyDown) {
		TScroller::handleEvent(event);
		return;
	}

	const TKey key(event.keyDown.keyCode, event.keyDown.controlKeyState);
	const ushort arrowKey = ctrlToArrow(event.keyDown.keyCode);
	const ushort mods = event.keyDown.controlKeyState;
	const unsigned char charCode = static_cast<unsigned char>(event.keyDown.charScan.charCode);
	const bool altPressed = (mods & kbAltShift) != 0;
	const bool shiftPressed = (mods & kbShift) != 0;
	const bool ctrlEnterPressed = event.keyDown.keyCode == kbCtrlEnter || key == TKey(kbEnter, kbCtrlShift);
	const bool altEnterPressed = event.keyDown.keyCode == kbAltEnter || key == TKey(kbEnter, kbAltShift) || (altPressed && (event.keyDown.keyCode == kbEnter || arrowKey == kbEnter));
	const bool shiftTabPressed = event.keyDown.keyCode == kbShiftTab || ((event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI || event.keyDown.charScan.charCode == '\t') && shiftPressed);
	const bool tabPressed = !shiftTabPressed && (event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI || event.keyDown.charScan.charCode == '\t');
	const bool backspacePressed = arrowKey == kbBack || event.keyDown.keyCode == kbCtrlH || event.keyDown.keyCode == kbCtrlBack || charCode == '\b' || charCode == 0x7F;

	if (ctrlEnterPressed || altEnterPressed) {
		clearEvent(event);
		if (!mReadOnly) MRSnippet::instance().commit(*this);
		return;
	}
	if (shiftTabPressed || tabPressed) {
		if (mReadOnly) {
			clearEvent(event);
			return;
		}
		if (!MRSnippet::instance().movePlaceholder(*this, shiftTabPressed ? -1 : 1) && tabPressed)
			insertChar('\t');
		drawView();
		clearEvent(event);
		return;
	}
	if (backspacePressed) {
		if (!mReadOnly) eraseBackward();
		drawView();
		clearEvent(event);
		return;
	}
	if (MRSnippet::instance().handleRuntimeKeymap(*this, event)) {
		drawView();
		return;
	}
	switch (arrowKey) {
		case kbEsc:
			clearEvent(event);
			closeSidekick();
			return;
		case kbEnter:
			if (mReadOnly) {
				clearEvent(event);
				return;
			}
			insertNewLine();
			break;
		case kbDel:
			if (mReadOnly) {
				clearEvent(event);
				return;
			}
			eraseForward();
			break;
		case kbLeft:
			moveLeft();
			break;
		case kbRight:
			moveRight();
			break;
		case kbUp:
			moveUp();
			break;
		case kbDown:
			moveDown();
			break;
		default: {
			if (charCode == '\t') {
				if (mReadOnly) {
					clearEvent(event);
					return;
				}
				insertChar('\t');
				break;
			}
			if (charCode >= 32 && charCode < 127) {
				if (mReadOnly) {
					clearEvent(event);
					return;
				}
				insertChar(static_cast<char>(charCode));
				break;
			}
			TScroller::handleEvent(event);
			return;
		}
	}
	ensureCursorVisible();
	drawView();
	clearEvent(event);
}

void MRSidekickEditor::closeSidekick(ushort command) {
	if (mModalClose) {
		endModal(command);
		return;
	}
	if (mReadOnly) mrvmStoreRuntimeStateInt("sidekick", "dismissedReadOnlyParentBufferId", mParentBufferId);
	detachFromOwner();
	TObject::destroy(this);
}

bool mrOpenReadOnlySidekickAt(MREditWindow *parent, const std::string &text, const std::string &title, int anchorViewColumn, int anchorViewRow, int preferredViewColumn, MRReadOnlySidekickPlacement placement, std::size_t apiReferenceAnchorOffset) {
	if (parent == nullptr || parent->getEditor() == nullptr || TProgram::deskTop == nullptr) return false;
	ReadOnlyMarker marker = romBelow;
	int markerColumn = -1;
	const TRect bounds = readOnlySidekickBoundsFor(parent, text, marker, anchorViewColumn, anchorViewRow, preferredViewColumn, placement, markerColumn);
	if (bounds.b.x <= bounds.a.x || bounds.b.y <= bounds.a.y) {
		mrDropSidekickForParent(parent);
		return false;
	}
	const int contentWidth = std::max(1, bounds.b.x - bounds.a.x - 2);
	const int visibleLineCount = std::max(1, bounds.b.y - bounds.a.y);
	const std::string markedText = readOnlyTextWithMarker(text, marker, contentWidth, visibleLineCount, markerColumn);
	MRSidekickEditor *sidekick = gActiveSidekick;
	if (sidekick != nullptr && sidekick->parentBufferId() == parent->bufferId() && sidekick->isReadOnly())
		sidekick->updateReadOnlyText(markedText, title, bounds);
	else {
		mrDropActiveSidekick();
		sidekick = new MRSidekickEditor(bounds, parent->bufferId(), markedText, title, true);
		if (sidekick == nullptr) return false;
		gActiveSidekick = sidekick;
	}
	sidekick->mApiReferenceActive = apiReferenceAnchorOffset != std::string::npos;
	if (sidekick->mApiReferenceActive) {
		sidekick->mApiReferenceAnchorOffset = apiReferenceAnchorOffset;
		sidekick->mApiReferenceCursorOffset = parent->getEditor()->cursorOffset();
		sidekick->mApiReferenceDeltaX = parent->getEditor()->delta.x;
		sidekick->mApiReferenceDeltaY = parent->getEditor()->delta.y;
		sidekick->mApiReferencePlacement = placement;
		sidekick->mApiReferenceText = text;
	} else
		sidekick->mApiReferenceText.clear();
	sidekick->insertInto(*TProgram::deskTop);
	sidekick->drawView();
	return true;
}

bool mrDismissApiReferenceSidekickForParent(const MREditWindow *parent) {
	if (parent == nullptr || gActiveSidekick == nullptr || !gActiveSidekick->mApiReferenceActive || gActiveSidekick->parentBufferId() != parent->bufferId()) return false;
	mrDropActiveSidekick();
	return true;
}

void mrSyncApiReferenceSidekickForParent(MREditWindow *parent, bool sourceScroll) {
	if (parent == nullptr || gActiveSidekick == nullptr || !gActiveSidekick->mApiReferenceActive || gActiveSidekick->parentBufferId() != parent->bufferId()) return;
	MRFileEditor *editor = parent->getEditor();
	if (editor == nullptr || (!sourceScroll && editor->cursorOffset() != gActiveSidekick->mApiReferenceCursorOffset)) {
		mrDropActiveSidekick();
		return;
	}
	if (sourceScroll) gActiveSidekick->mApiReferenceCursorOffset = editor->cursorOffset();
	if (editor->delta.x == gActiveSidekick->mApiReferenceDeltaX && editor->delta.y == gActiveSidekick->mApiReferenceDeltaY) return;
	const std::size_t anchorOffset = gActiveSidekick->mApiReferenceAnchorOffset;
	const std::size_t line = editor->lineIndexOfOffset(anchorOffset);
	const int viewRow = static_cast<int>(editor->visibleLineForDocumentLine(line)) - editor->delta.y + 1;
	if (viewRow < 1 || viewRow > editor->visibleViewportRows()) {
		mrDropActiveSidekick();
		return;
	}
	const int viewColumn = std::max(1, editor->charColumn(editor->lineStartOffset(anchorOffset), anchorOffset) - editor->delta.x + 1);
	const std::string text = gActiveSidekick->mApiReferenceText;
	const std::string title = gActiveSidekick->mTitle;
	const MRReadOnlySidekickPlacement placement = gActiveSidekick->mApiReferencePlacement;
	static_cast<void>(mrOpenReadOnlySidekickAt(parent, text, title, viewColumn, viewRow, 0, placement, anchorOffset));
}

bool mrHasReadOnlySidekickForParent(const MREditWindow *parent) {
	return parent != nullptr && gActiveSidekick != nullptr && gActiveSidekick->parentBufferId() == parent->bufferId() && gActiveSidekick->isReadOnly();
}

bool mrConsumeReadOnlySidekickDismissedForParent(const MREditWindow *parent) {
	if (parent == nullptr || mrvmRuntimeStateInt("sidekick", "dismissedReadOnlyParentBufferId") != parent->bufferId()) return false;
	mrvmStoreRuntimeStateInt("sidekick", "dismissedReadOnlyParentBufferId", 0);
	return true;
}

void mrDropSidekickForParent(const MREditWindow *parent) {
	if (parent == nullptr || gActiveSidekick == nullptr) return;
	if (gActiveSidekick->parentBufferId() == parent->bufferId()) {
		mrDropActiveSidekick();
	}
}

void mrDropActiveSidekick() {
	MRSidekickEditor *sidekick = gActiveSidekick;
	if (sidekick == nullptr) return;
	gActiveSidekick = nullptr;
	sidekick->detachFromOwner();
	TObject::destroy(sidekick);
}
