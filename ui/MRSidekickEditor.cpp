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
using mr::sidekick_internal::snippetSidekickBoundsFor;
using mr::sidekick_internal::snippetSidekickDialogColor;
using mr::sidekick_internal::splitLines;
using mr::sidekick_internal::expandSidekickTabs;

namespace {

MRSidekickEditor *gActiveSidekick = nullptr;

constexpr TColorAttr kSidekickCursor = 0x70;

class SnippetSidekickHintGuard {
  public:
	SnippetSidekickHintGuard() {
		mrSetSnippetSidekickHintsActive(true);
	}

	~SnippetSidekickHintGuard() {
		mrSetSnippetSidekickHintsActive(false);
	}
};

class MRSnippetSidekickFrame : public MRFrame {
  public:
	explicit MRSnippetSidekickFrame(const TRect &bounds) noexcept : MRFrame(bounds) {
	}

	void draw() override {
		MRFrame::draw();
		if (size.x < 2 || size.y < 1) return;
		TDrawBuffer buffer;
		const TColorAttr color = sidekickColor(kMrPaletteSnippetSidekickFrame, 0x3F);

		buffer.moveStr(0, "╚═", color, 2);
		writeLine(0, size.y - 1, 2, 1, buffer);
		buffer.moveStr(0, "═╝", color, 2);
		writeLine(size.x - 2, size.y - 1, 2, 1, buffer);
	}
};

TFrame *initSnippetSidekickFrame(TRect bounds) {
	return new MRSnippetSidekickFrame(bounds);
}

class MRSnippetSidekickDialog : public TDialog {
  public:
	MRSnippetSidekickDialog(const TRect &bounds, int parentBufferId, std::size_t replaceStart, std::size_t replaceEnd, const std::string &text, const std::string &title, const std::vector<MRSidekickSpan> &placeholders)
	    : TWindowInit(initSnippetSidekickFrame), TDialog(bounds, title.c_str()), mEditor(nullptr) {
		TButton *helpButton;

		flags |= wfMove | wfGrow | wfClose;
		growMode = gfGrowHiX | gfGrowHiY;
		helpCtx = hcDialogSnippetSidekick;
		mEditor = new MRSidekickEditor(TRect(1, 1, std::max<short>(2, size.x - 1), std::max<short>(2, size.y - 3)), parentBufferId, replaceStart, replaceEnd, text, title, placeholders, false, true, true);
		if (mEditor != nullptr) {
			mEditor->growMode = gfGrowHiX | gfGrowHiY;
			mEditor->insertInto(*this);
			mEditor->select();
		}
		helpButton = new TButton(TRect(std::max<short>(2, size.x - 12), std::max<short>(2, size.y - 3), std::max<short>(3, size.x - 1), std::max<short>(4, size.y - 1)), "~H~elp", cmHelp, bfNormal);
		helpButton->growMode = gfGrowAll;
		insert(helpButton);
	}

	[[nodiscard]] MRSidekickEditor *snippetSidekick() const noexcept {
		return mEditor;
	}

	void sizeLimits(TPoint &min, TPoint &max) override {
		TDialog::sizeLimits(min, max);
		min.x = std::max<short>(min.x, std::min<short>(32, size.x));
		min.y = std::max<short>(min.y, std::min<short>(10, size.y));
	}

	TPalette &getPalette() const override {
		static TColorAttr paletteData[8];
		static TPalette palette(paletteData, 8);

		for (uchar index = 1; index <= 8; ++index)
			paletteData[index - 1] = snippetSidekickDialogColor(index);
		palette = TPalette(paletteData, 8);
		return palette;
	}

	TColorAttr mapColor(uchar index) override {
		if (index >= 1 && index <= 8) return snippetSidekickDialogColor(index);
		return TDialog::mapColor(index);
	}

  private:
	MRSidekickEditor *mEditor;
};


} // namespace

MRSidekickEditor::MRSidekickEditor(const TRect &bounds, int parentBufferId, std::size_t replaceStart, std::size_t replaceEnd, std::string text, std::string title, std::vector<MRSidekickSpan> placeholders, bool readOnly, bool modalClose, bool snippetSidekick, MRSidekickPalette palette)
    : TScroller(bounds, nullptr, nullptr), mParentBufferId(parentBufferId), mReplaceStart(replaceStart), mReplaceEnd(replaceEnd), mTitle(std::move(title)), mLines(), mPlaceholders(std::move(placeholders)), mPlaceholderTouched(mPlaceholders.size(), 0), mPlaceholderIndex(-1), mPlaceholderEndEdge(false), mCursorRow(0), mCursorCol(0), mReadOnly(readOnly), mModalClose(modalClose), mSnippetSidekick(snippetSidekick), mPalette(palette), mOuterBounds(bounds), mHorizontalScrollBar(nullptr), mVerticalScrollBar(nullptr) {
	if (mReadOnly) options &= ~ofSelectable;
	growMode = gfGrowHiX | gfGrowHiY;
	eventMask |= evKeyDown | evMouseDown;
	setText(std::move(text));
	if (!mPlaceholders.empty()) moveToPlaceholder(1);
}

MRSidekickEditor::~MRSidekickEditor() {
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
		mHorizontalScrollBar = new TScrollBar(TRect(bounds.a.x, viewBounds.b.y, viewBounds.b.x, bounds.b.y));
		mHorizontalScrollBar->growMode = gfGrowHiX | gfGrowLoY | gfGrowHiY;
		hScrollBar = mHorizontalScrollBar;
	}
	if (verticalVisible) {
		mVerticalScrollBar = new TScrollBar(TRect(viewBounds.b.x, bounds.a.y, bounds.b.x, viewBounds.b.y));
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

bool MRSidekickEditor::isSnippetSidekick() const noexcept {
	return mSnippetSidekick;
}

bool MRSidekickEditor::moveSnippetPlaceholder(int direction) {
	if (mReadOnly || !mSnippetSidekick || mPlaceholders.empty()) return false;
	moveToPlaceholder(direction);
	drawView();
	return true;
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
	mTitle = std::move(title);
	setText(std::move(textValue));
	updateScrollBars(bounds);
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
	TColorAttr textColor = getColor(1);
	if (mPalette == MRSidekickPalette::Sidekick) textColor = sidekickColor(mSnippetSidekick ? kMrPaletteSnippetSidekickText : kMrPaletteSidekickEditorText, 0x30);
	const TColorAttr highlightColor = sidekickColor(mSnippetSidekick ? kMrPaletteSnippetActivePlaceholder : kMrPaletteSidekickEditorHighlight, 0xE0);
	const TColorAttr defaultTextColor = sidekickColor(kMrPaletteSnippetDefaultText, 0x38);
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
					if (mSnippetSidekick) {
						for (std::size_t index = 0; index < mPlaceholders.size(); ++index) {
							const MRSidekickSpan &span = mPlaceholders[index];
							if (offset < span.start || offset >= span.end) continue;
							const bool touched = index < mPlaceholderTouched.size() && mPlaceholderTouched[index] != 0;
							if (static_cast<int>(index) == mPlaceholderIndex && !touched) charColor = highlightColor;
							else if (!touched) charColor = defaultTextColor;
							else charColor = textColor;
							break;
						}
					}
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
		if (!mReadOnly) commitAndClose();
		return;
	}
	if (shiftTabPressed || tabPressed) {
		if (mReadOnly) {
			clearEvent(event);
			return;
		}
		if (!mPlaceholders.empty())
			moveToPlaceholder(shiftTabPressed ? -1 : 1);
		else if (tabPressed)
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
	if (handleRuntimeKeymap(event)) {
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

void MRSidekickEditor::commitAndClose() {
	MREditWindow *parent = findEditWindowByBufferId(mParentBufferId);
	MRFileEditor *editor = parent != nullptr ? parent->getEditor() : nullptr;
	const std::string replacement = text();

	if (editor != nullptr && !editor->isReadOnly() && editor->replaceRangeAndSelect(static_cast<uint>(mReplaceStart), static_cast<uint>(mReplaceEnd), replacement.c_str(), static_cast<uint>(replacement.size()))) {
		const std::size_t cursor = std::min<std::size_t>(mReplaceStart + replacement.size(), editor->bufferLength());
		editor->setSelectionOffsets(cursor, cursor, False);
	}
	closeSidekick(cmOK);
}

bool mrOpenReadOnlySidekickAt(MREditWindow *parent, const std::string &text, const std::string &title, int anchorViewColumn, int anchorViewRow, int preferredViewColumn, MRReadOnlySidekickPlacement placement) {
	if (parent == nullptr || parent->getEditor() == nullptr || TProgram::deskTop == nullptr) return false;
	ReadOnlyMarker marker = romBelow;
	const TRect bounds = readOnlySidekickBoundsFor(parent, text, marker, anchorViewColumn, anchorViewRow, preferredViewColumn, placement);
	const int contentWidth = std::max(1, bounds.b.x - bounds.a.x - 2);
	const int visibleLineCount = std::max(1, bounds.b.y - bounds.a.y);
	const std::string markedText = readOnlyTextWithMarker(text, marker, contentWidth, visibleLineCount);
	if (gActiveSidekick != nullptr && gActiveSidekick->parentBufferId() == parent->bufferId() && gActiveSidekick->isReadOnly()) {
		gActiveSidekick->updateReadOnlyText(markedText, title, bounds);
		gActiveSidekick->insertInto(*TProgram::deskTop);
		gActiveSidekick->drawView();
		return true;
	}
	mrDropActiveSidekick();
	MRSidekickEditor *sidekick = new MRSidekickEditor(bounds, parent->bufferId(), 0, 0, markedText, title, std::vector<MRSidekickSpan>(), true);
	if (sidekick == nullptr) return false;
	gActiveSidekick = sidekick;
	sidekick->insertInto(*TProgram::deskTop);
	sidekick->drawView();
	return true;
}

bool mrOpenSnippetSidekickAt(MREditWindow *parent, const std::string &text, const std::string &title, std::size_t replaceStart, std::size_t replaceEnd, const std::vector<MRSidekickSpan> &placeholders, int anchorViewColumn, int anchorViewRow, bool &committed) {
	if (parent == nullptr || parent->getEditor() == nullptr || TProgram::deskTop == nullptr) return false;
	const TRect bounds = snippetSidekickBoundsFor(parent, text, replaceStart, anchorViewColumn, anchorViewRow);

	committed = false;
	mrDropActiveSidekick();
	MRSnippetSidekickDialog *dialog = new MRSnippetSidekickDialog(bounds, parent->bufferId(), replaceStart, replaceEnd, text, title, placeholders);
	if (dialog == nullptr) return false;
	if (dialog->snippetSidekick() == nullptr) {
		TObject::destroy(dialog);
		return false;
	}
	ushort result = cmCancel;
	{
		SnippetSidekickHintGuard hintGuard;
		result = TProgram::deskTop->execView(dialog);
	}
	committed = result == cmOK;
	TObject::destroy(dialog);
	static_cast<void>(mrActivateEditWindow(parent));
	if (parent->getEditor() != nullptr) parent->getEditor()->select();
	return true;
}

bool mrHasReadOnlySidekickForParent(const MREditWindow *parent) {
	return parent != nullptr && gActiveSidekick != nullptr && gActiveSidekick->parentBufferId() == parent->bufferId() && gActiveSidekick->isReadOnly();
}

bool mrConsumeReadOnlySidekickDismissedForParent(const MREditWindow *parent) {
	if (parent == nullptr || mrvmRuntimeStateInt("sidekick", "dismissedReadOnlyParentBufferId") != parent->bufferId()) return false;
	mrvmStoreRuntimeStateInt("sidekick", "dismissedReadOnlyParentBufferId", 0);
	return true;
}

bool mrMoveSnippetPlaceholderForParent(const MREditWindow *parent, int direction) {
	if (parent == nullptr || gActiveSidekick == nullptr) return false;
	if (gActiveSidekick->parentBufferId() != parent->bufferId()) return false;
	if (!gActiveSidekick->isSnippetSidekick()) return false;
	return gActiveSidekick->moveSnippetPlaceholder(direction);
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
