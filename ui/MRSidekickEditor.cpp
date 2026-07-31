#define Uses_TDrawBuffer
#define Uses_TDeskTop
#define Uses_TButton
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TKeys
#define Uses_TProgram
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
using mr::sidekick_internal::readOnlyMarkerGlyph;
using mr::sidekick_internal::readOnlySidekickBoundsFor;
using mr::sidekick_internal::readOnlyTextWithMarker;
using mr::sidekick_internal::romAbove;
using mr::sidekick_internal::romAboveRight;
using mr::sidekick_internal::romBelow;
using mr::sidekick_internal::romBelowRight;
using mr::sidekick_internal::romLeft;
using mr::sidekick_internal::romRight;
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
			insert(mEditor);
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
		min.x = std::max<short>(min.x, 32);
		min.y = std::max<short>(min.y, 10);
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

MRSidekickEditor::MRSidekickEditor(const TRect &bounds, int parentBufferId, std::size_t replaceStart, std::size_t replaceEnd, std::string text, std::string title, std::vector<MRSidekickSpan> placeholders, bool readOnly, bool modalClose, bool snippetSidekick)
    : TView(bounds), mParentBufferId(parentBufferId), mReplaceStart(replaceStart), mReplaceEnd(replaceEnd), mTitle(std::move(title)), mLines(), mPlaceholders(std::move(placeholders)), mPlaceholderTouched(mPlaceholders.size(), 0), mPlaceholderIndex(-1), mPlaceholderEndEdge(false), mCursorRow(0), mCursorCol(0), mReadOnly(readOnly), mModalClose(modalClose), mSnippetSidekick(snippetSidekick) {
	if (!mReadOnly) options |= ofSelectable;
	growMode = gfGrowHiX | gfGrowHiY;
	eventMask |= evKeyDown | evMouseDown;
	setText(std::move(text));
	if (!mPlaceholders.empty()) moveToPlaceholder(1);
}

MRSidekickEditor::~MRSidekickEditor() {
	if (gActiveSidekick == this) gActiveSidekick = nullptr;
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
	TRect target = bounds;
	mTitle = std::move(title);
	setText(std::move(textValue));
	locate(target);
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
	const TColorAttr textColor = sidekickColor(mSnippetSidekick ? kMrPaletteSnippetSidekickText : kMrPaletteSidekickEditorText, 0x30);
	const TColorAttr highlightColor = sidekickColor(mSnippetSidekick ? kMrPaletteSnippetActivePlaceholder : kMrPaletteSidekickEditorHighlight, 0xE0);
	const TColorAttr defaultTextColor = sidekickColor(kMrPaletteSnippetDefaultText, 0x38);
	std::size_t lineStartOffset = 0;

	for (int y = 0; y < size.y; ++y) {
		TDrawBuffer buffer;
		buffer.moveChar(0, ' ', textColor, size.x);
		if (y < static_cast<int>(mLines.size())) {
			const std::string &line = mLines[static_cast<std::size_t>(y)];
			const int textX = mReadOnly ? 0 : 1;
			if (mReadOnly) {
				buffer.moveStr(0, line.c_str(), textColor, static_cast<ushort>(size.x));
			} else {
				const int visible = std::min<int>(line.size(), std::max(0, size.x - textX - 1));
				for (int x = 0; x < visible; ++x) {
					const std::size_t offset = lineStartOffset + static_cast<std::size_t>(x);
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
					const unsigned char raw = static_cast<unsigned char>(line[static_cast<std::size_t>(x)]);
					buffer.moveChar(static_cast<ushort>(x + textX), raw < 32 ? ' ' : line[static_cast<std::size_t>(x)], charColor, 1);
				}
			}
		}
		if (!mReadOnly && y == mCursorRow) {
			const int cursorX = std::clamp(mCursorCol + 1, 0, std::max(0, size.x - 1));
			char cursorChar = (mCursorCol >= 0 && mCursorCol < static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow)].size())) ? mLines[static_cast<std::size_t>(mCursorRow)][static_cast<std::size_t>(mCursorCol)] : ' ';
			if (static_cast<unsigned char>(cursorChar) < 32) cursorChar = ' ';
			buffer.moveChar(static_cast<ushort>(cursorX), cursorChar, kSidekickCursor, 1);
		}
		writeLine(0, static_cast<short>(y), size.x, 1, buffer);
		if (y < static_cast<int>(mLines.size())) lineStartOffset += mLines[static_cast<std::size_t>(y)].size() + 1;
	}
}

void MRSidekickEditor::handleEvent(TEvent &event) {
	if (event.what == evMouseDown) {
		if (containsMouse(event)) {
			TPoint local = makeLocal(event.mouse.where);
			mCursorRow = std::clamp<int>(local.y, 0, static_cast<int>(mLines.size()) - 1);
			mCursorCol = std::max(0, local.x - 1);
			clampCursor();
			drawView();
			clearEvent(event);
		}
		return;
	}
	if (event.what != evKeyDown) {
		TView::handleEvent(event);
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
			TView::handleEvent(event);
			return;
		}
	}
	drawView();
	clearEvent(event);
}

void MRSidekickEditor::closeSidekick(ushort command) {
	TGroup *group = owner;

	if (mModalClose) {
		endModal(command);
		return;
	}
	if (mReadOnly) mrvmStoreRuntimeStateInt("sidekick", "dismissedReadOnlyParentBufferId", mParentBufferId);
	if (group != nullptr) group->remove(this);
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

bool mrOpenReadOnlySidekick(MREditWindow *parent, const std::string &text, const std::string &title, int preferredViewColumn, MRReadOnlySidekickPlacement placement) {
	MRFileEditor *editor = parent != nullptr ? parent->getEditor() : nullptr;
	const int anchorViewColumn = editor != nullptr ? editor->currentViewColumn() : 1;
	const int anchorViewRow = editor != nullptr ? editor->currentViewRow() : 1;
	return mrOpenReadOnlySidekickAt(parent, text, title, anchorViewColumn, anchorViewRow, preferredViewColumn, placement);
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
		if (gActiveSidekick->owner != TProgram::deskTop) {
			if (gActiveSidekick->owner != nullptr) gActiveSidekick->owner->remove(gActiveSidekick);
			TProgram::deskTop->insert(gActiveSidekick);
		} else {
			TProgram::deskTop->remove(gActiveSidekick);
			TProgram::deskTop->insert(gActiveSidekick);
		}
		gActiveSidekick->drawView();
		return true;
	}
	mrDropActiveSidekick();
	MRSidekickEditor *sidekick = new MRSidekickEditor(bounds, parent->bufferId(), 0, 0, markedText, title, std::vector<MRSidekickSpan>(), true);
	if (sidekick == nullptr) return false;
	gActiveSidekick = sidekick;
	TProgram::deskTop->insert(sidekick);
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
	TGroup *group = sidekick->owner;
	gActiveSidekick = nullptr;
	if (group != nullptr) group->remove(sidekick);
	TObject::destroy(sidekick);
}

bool mrReadOnlySidekickGeometrySelfTestForRegression(std::string &failureReason) {
	struct Case {
		const char *name;
		const char *documentText;
		const char *hintText;
		int anchorColumn;
		int anchorRow;
		int preferredColumn;
		ReadOnlyMarker expectedMarker;
		bool expectAbove;
		};
		const Case cases[] = {
		    {"top left keeps up glyph below anchor", "err\none\ntwo\nthree", "error 1:1 - top left", 1, 1, 1, romBelow, false},
		    {"top right keeps up glyph below anchor", "err\none\ntwo\nthree", "error 1:76 - top right", 76, 1, 76, romBelowRight, false},
		    {"EOF viewport space below is usable", "one\ntwo\nthree\nlast", "error 4:3 - tail", 3, 4, 3, romBelow, false},
		    {"blank document line below is usable", "one\n\nthree", "error 1:3 - blank target", 3, 1, 3, romBelow, false},
		    {"text below still keeps under-code placement", "\n\nerr\ntext", "error 3:3 - text below", 3, 3, 3, romBelow, false},
		    {"diagnostic with following code stays below", "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\ni=\"dumm\";\nreturn(0);", "error 17:3 - Use of undeclared identifier 'i'", 3, 17, 3, romBelow, false},
		    {"right edge with following code keeps up glyph at right", "\n\nerr\ntext", "error 3:70 - edge", 76, 3, 76, romBelowRight, false},
		    {"right edge below keeps up glyph at right", "i=\"dumm\";\n\n\n", "error 1:74 - Use of undeclared identifier 'i'", 76, 1, 76, romBelowRight, false},
		};

	for (const Case &testCase : cases) {
		MREditWindow window(TRect(0, 0, 80, 24), "sidekick-geometry", 4101);
		MRFileEditor *editor = window.getEditor();
		ReadOnlyMarker marker = romBelow;

		if (editor == nullptr) {
			failureReason = std::string(testCase.name) + ": editor missing.";
			return false;
		}
		if (!editor->replaceBufferText(testCase.documentText)) {
			failureReason = std::string(testCase.name) + ": unable to seed editor.";
			return false;
		}
		editor->scrollTo(0, 0);
		const TPoint editorGlobal = editor->makeGlobal(TPoint(0, 0));
		const TRect textViewport = editor->visibleTextViewportBounds();
		const int anchorX = editorGlobal.x + textViewport.a.x + testCase.anchorColumn - 1;
		const int viewportTop = editorGlobal.y + textViewport.a.y;
		const int viewportBottom = editorGlobal.y + textViewport.b.y;
		const int anchorY = std::clamp(viewportTop + testCase.anchorRow - 2, viewportTop - 1, std::max(viewportTop - 1, viewportBottom - 1));
		const TRect bounds = readOnlySidekickBoundsFor(&window, testCase.hintText, marker, testCase.anchorColumn, testCase.anchorRow, testCase.preferredColumn, MRReadOnlySidekickPlacement::UnderCode);
		const int markerX = (marker == romBelowRight || marker == romAboveRight || marker == romRight) ? bounds.b.x - 1 : bounds.a.x;
		const int markerY = testCase.expectAbove ? bounds.b.y - 1 : bounds.a.y;
		const int contentWidth = std::max(1, bounds.b.x - bounds.a.x - 2);
		const int visibleLineCount = std::max(1, bounds.b.y - bounds.a.y);
		const std::vector<std::string> markedLines = splitLines(readOnlyTextWithMarker(testCase.hintText, marker, contentWidth, visibleLineCount));
		const std::size_t markerLine = testCase.expectAbove ? static_cast<std::size_t>(std::min<int>(visibleLineCount, static_cast<int>(markedLines.size())) - 1) : 0;
		const std::string markerGlyph = readOnlyMarkerGlyph(marker);
		const bool markerAtRight = marker == romBelowRight || marker == romAboveRight || marker == romRight;

		if (marker != testCase.expectedMarker) {
			failureReason = std::string(testCase.name) + ": marker mismatch.";
			return false;
		}
		if (bounds.a.x < editorGlobal.x + textViewport.a.x || bounds.b.x > editorGlobal.x + textViewport.b.x || bounds.a.y < editorGlobal.y + textViewport.a.y || bounds.b.y > editorGlobal.y + textViewport.b.y) {
			failureReason = std::string(testCase.name) + ": sidekick escapes text viewport.";
			return false;
		}
		if (testCase.expectAbove) {
			if (bounds.b.y != anchorY) {
				failureReason = std::string(testCase.name) + ": above sidekick must render above the anchor row; anchorY=" + std::to_string(anchorY) + " bounds=" + std::to_string(bounds.a.y) + ".." + std::to_string(bounds.b.y) + ".";
				return false;
			}
		} else if (bounds.a.y != anchorY + 1) {
			failureReason = std::string(testCase.name) + ": below sidekick must render directly below the anchor row; anchorY=" + std::to_string(anchorY) + " bounds=" + std::to_string(bounds.a.y) + ".." + std::to_string(bounds.b.y) + ".";
			return false;
		}
		if (bounds.a.y <= anchorY && anchorY < bounds.b.y) {
			failureReason = std::string(testCase.name) + ": sidekick covers anchor row.";
			return false;
		}
		if (markerX != anchorX) {
			failureReason = std::string(testCase.name) + ": marker column does not point to anchor column.";
			return false;
		}
		if (markerY != (testCase.expectAbove ? anchorY - 1 : anchorY + 1)) {
			failureReason = std::string(testCase.name) + ": marker row does not point to anchor row.";
			return false;
		}
		if (markerLine >= markedLines.size()) {
			failureReason = std::string(testCase.name) + ": marker line missing.";
			return false;
		}
		if (markerAtRight) {
			const std::string &line = markedLines[markerLine];
			if (line.size() < markerGlyph.size() || line.compare(line.size() - markerGlyph.size(), markerGlyph.size(), markerGlyph) != 0) {
				failureReason = std::string(testCase.name) + ": right-edge marker glyph mismatch.";
				return false;
			}
		} else {
			if (markedLines[markerLine].compare(0, markerGlyph.size(), markerGlyph) != 0) {
				failureReason = std::string(testCase.name) + ": left-edge marker glyph mismatch.";
				return false;
			}
		}
	}
	{
		MREditWindow window(TRect(0, 0, 80, 12), "sidekick-bottom-geometry", 4102);
		MRFileEditor *editor = window.getEditor();
		std::string documentText;

		if (editor == nullptr) {
			failureReason = "bottom viewport row: editor missing.";
			return false;
		}
		for (int row = 1; row <= editor->visibleViewportRows(); ++row) {
			if (row != 1) documentText.push_back('\n');
			documentText += row == editor->visibleViewportRows() ? "tail" : "";
		}
		if (!editor->replaceBufferText(documentText.c_str())) {
			failureReason = "bottom viewport row: unable to seed editor.";
			return false;
		}
		editor->scrollTo(0, 0);
		const TPoint editorGlobal = editor->makeGlobal(TPoint(0, 0));
		const TRect textViewport = editor->visibleTextViewportBounds();
		const int anchorRow = editor->visibleViewportRows();
		struct BottomCase {
			const char *name;
			int anchorColumn;
			ReadOnlyMarker expectedMarker;
		};
		const BottomCase bottomCases[] = {
		    {"bottom viewport left", 3, romAbove},
		    {"bottom viewport right", 76, romAboveRight},
		};

		for (const BottomCase &bottomCase : bottomCases) {
			ReadOnlyMarker marker = romBelow;
			const int anchorX = editorGlobal.x + textViewport.a.x + bottomCase.anchorColumn - 1;
			const int viewportTop = editorGlobal.y + textViewport.a.y;
			const int viewportBottom = editorGlobal.y + textViewport.b.y;
			const int anchorY = std::clamp(viewportTop + anchorRow - 2, viewportTop - 1, std::max(viewportTop - 1, viewportBottom - 1));
			const TRect bounds = readOnlySidekickBoundsFor(&window, "error bottom - no below space", marker, bottomCase.anchorColumn, anchorRow, bottomCase.anchorColumn, MRReadOnlySidekickPlacement::UnderCode);
			const int markerX = (marker == romAboveRight) ? bounds.b.x - 1 : bounds.a.x;
			const int markerY = bounds.b.y - 1;
			const int contentWidth = std::max(1, bounds.b.x - bounds.a.x - 2);
			const int visibleLineCount = std::max(1, bounds.b.y - bounds.a.y);
			const std::vector<std::string> markedLines = splitLines(readOnlyTextWithMarker("error bottom - no below space", marker, contentWidth, visibleLineCount));
			const std::size_t markerLine = static_cast<std::size_t>(std::min<int>(visibleLineCount, static_cast<int>(markedLines.size())) - 1);
			const std::string markerGlyph = readOnlyMarkerGlyph(marker);

			if (marker != bottomCase.expectedMarker) {
				failureReason = std::string(bottomCase.name) + ": marker mismatch.";
				return false;
			}
			if (bounds.a.x < editorGlobal.x + textViewport.a.x || bounds.b.x > editorGlobal.x + textViewport.b.x || bounds.a.y < editorGlobal.y + textViewport.a.y || bounds.b.y > editorGlobal.y + textViewport.b.y) {
				failureReason = std::string(bottomCase.name) + ": sidekick escapes text viewport.";
				return false;
			}
			if (bounds.b.y != anchorY) {
				failureReason = std::string(bottomCase.name) + ": above sidekick must end directly above the anchor row.";
				return false;
			}
			if (bounds.a.y <= anchorY && anchorY < bounds.b.y) {
				failureReason = std::string(bottomCase.name) + ": sidekick covers anchor row.";
				return false;
			}
			if (markerX != anchorX || markerY != anchorY - 1) {
				failureReason = std::string(bottomCase.name) + ": marker does not point to anchor cell.";
				return false;
			}
			if (markerLine >= markedLines.size()) {
				failureReason = std::string(bottomCase.name) + ": marker line missing.";
				return false;
			}
			if (marker == romAboveRight) {
				const std::string &line = markedLines[markerLine];
				if (line.size() < markerGlyph.size() || line.compare(line.size() - markerGlyph.size(), markerGlyph.size(), markerGlyph) != 0) {
					failureReason = std::string(bottomCase.name) + ": right-edge marker glyph mismatch.";
					return false;
				}
			} else if (markedLines[markerLine].compare(0, markerGlyph.size(), markerGlyph) != 0) {
				failureReason = std::string(bottomCase.name) + ": left-edge marker glyph mismatch.";
				return false;
			}
		}
	}
	if (TProgram::deskTop != nullptr) {
		MREditWindow *window = new MREditWindow(TRect(0, 0, 80, 24), "sidekick-live-geometry", 4103);
		MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
		const char *documentText = "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\ni=\"dumm\";\nreturn(0);";

		if (window == nullptr || editor == nullptr) {
			failureReason = "live sidekick geometry: editor missing.";
			if (window != nullptr) TObject::destroy(window);
			return false;
		}
		TProgram::deskTop->insert(window);
		if (!editor->replaceBufferText(documentText)) {
			failureReason = "live sidekick geometry: unable to seed editor.";
			TProgram::deskTop->remove(window);
			TObject::destroy(window);
			return false;
		}
		editor->scrollTo(0, 0);
		const TPoint editorGlobal = editor->makeGlobal(TPoint(0, 0));
		const TRect textViewport = editor->visibleTextViewportBounds();
		const int viewportTop = editorGlobal.y + textViewport.a.y;
		const int viewportBottom = editorGlobal.y + textViewport.b.y;
		const int anchorY = std::clamp(viewportTop + 15, viewportTop - 1, std::max(viewportTop - 1, viewportBottom - 1));

		if (!mrOpenReadOnlySidekickAt(window, "tail", "Read-only sidekick", 3, 4, 3, MRReadOnlySidekickPlacement::UnderCode)) {
			failureReason = "live sidekick geometry: initial open failed.";
			TProgram::deskTop->remove(window);
			TObject::destroy(window);
			return false;
		}
		if (gActiveSidekick == nullptr || gActiveSidekick->owner != TProgram::deskTop || TProgram::deskTop->first() != gActiveSidekick) {
			failureReason = "live sidekick geometry: initial sidekick must remain the front desktop overlay.";
			mrDropActiveSidekick();
			TProgram::deskTop->remove(window);
			TObject::destroy(window);
			return false;
		}
		if (!mrOpenReadOnlySidekickAt(window, "error 17:3 - Use of undeclared identifier 'i'", "Read-only sidekick", 3, 17, 3, MRReadOnlySidekickPlacement::UnderCode)) {
			failureReason = "live sidekick geometry: update open failed.";
			mrDropActiveSidekick();
			TProgram::deskTop->remove(window);
			TObject::destroy(window);
			return false;
		}
		if (gActiveSidekick == nullptr) {
			failureReason = "live sidekick geometry: active sidekick missing.";
			TProgram::deskTop->remove(window);
			TObject::destroy(window);
			return false;
		}
		if (gActiveSidekick->owner != TProgram::deskTop || TProgram::deskTop->first() != gActiveSidekick) {
			failureReason = "live sidekick geometry: updated sidekick must remain the front desktop overlay.";
			mrDropActiveSidekick();
			TProgram::deskTop->remove(window);
			TObject::destroy(window);
			return false;
		}
		const TRect bounds = gActiveSidekick->getBounds();
		if (bounds.a.y != anchorY + 1) {
			failureReason = "live sidekick geometry: updated under-code sidekick must render below the anchor row; anchorY=" + std::to_string(anchorY) + " bounds=" + std::to_string(bounds.a.y) + ".." + std::to_string(bounds.b.y) + ".";
			mrDropActiveSidekick();
			TProgram::deskTop->remove(window);
			TObject::destroy(window);
			return false;
		}
		if (bounds.a.y <= anchorY && anchorY < bounds.b.y) {
			failureReason = "live sidekick geometry: updated sidekick covers anchor row.";
			mrDropActiveSidekick();
			TProgram::deskTop->remove(window);
			TObject::destroy(window);
			return false;
		}
		const char *snippetDocumentText = "int main() {\n  printf\n}\n";
		const std::string snippetText = "printf(\"bleppo\");";
		const std::size_t replaceStart = std::string(snippetDocumentText).find("printf");
		if (replaceStart == std::string::npos || !editor->replaceBufferText(snippetDocumentText)) {
			failureReason = "live snippet sidekick: unable to seed literal-anchor case.";
			mrDropActiveSidekick();
			TProgram::deskTop->remove(window);
			TObject::destroy(window);
			return false;
		}
		editor->scrollTo(0, 0);
		const std::size_t literalLine = editor->lineIndexOfOffset(replaceStart);
		const TPoint snippetEditorGlobal = editor->makeGlobal(TPoint(0, 0));
		const TRect snippetViewport = editor->visibleTextViewportBounds();
		const int literalGlobalX = snippetEditorGlobal.x + snippetViewport.a.x + editor->charColumn(editor->lineStartOffset(replaceStart), replaceStart);
		const int literalGlobalY = snippetEditorGlobal.y + snippetViewport.a.y + static_cast<int>(editor->visibleLineForDocumentLine(literalLine)) - editor->delta.y;
		const int expectedSnippetX = literalGlobalX - 1;
			const int expectedSnippetY = literalGlobalY - 2;
		std::vector<MRSidekickSpan> placeholders;
		placeholders.push_back(MRSidekickSpan{8, 14});
		placeholders.push_back(MRSidekickSpan{16, 16});
		const TRect snippetBounds = snippetSidekickBoundsFor(window, snippetText, replaceStart, 40, 20);
		if (snippetBounds.a.x != expectedSnippetX || snippetBounds.a.y != expectedSnippetY) {
			failureReason = "live snippet sidekick: body must align with literal start.";
			mrDropActiveSidekick();
			TProgram::deskTop->remove(window);
			TObject::destroy(window);
			return false;
			}
			MRSnippetSidekickDialog *dialog = new MRSnippetSidekickDialog(snippetBounds, window->bufferId(), replaceStart, replaceStart + 6, snippetText, "Snippet SideKick", placeholders);
		MRSidekickEditor *snippetSidekick = dialog != nullptr ? dialog->snippetSidekick() : nullptr;
			if (dialog == nullptr || snippetSidekick == nullptr) {
				failureReason = "live snippet sidekick: construction failed.";
				if (dialog != nullptr) TObject::destroy(dialog);
				mrDropActiveSidekick();
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			const TPoint snippetTextGlobal = snippetSidekick->makeGlobal(TPoint(1, 0));
			if (snippetTextGlobal.y != literalGlobalY) {
				failureReason = "live snippet sidekick: first editable row must align with literal row.";
				TObject::destroy(dialog);
				mrDropActiveSidekick();
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			TProgram::deskTop->insert(dialog);
			if (gActiveSidekick != nullptr) {
				failureReason = "live snippet sidekick: editable snippet sidekick must not reuse read-only sidekick state.";
				TProgram::deskTop->remove(dialog);
				TObject::destroy(dialog);
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			if ((snippetSidekick->options & ofSelectable) == 0) {
				failureReason = "live snippet sidekick: body is not selectable after insert.";
				TProgram::deskTop->remove(dialog);
				TObject::destroy(dialog);
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			if (!snippetSidekick->moveSnippetPlaceholder(1) || !snippetSidekick->moveSnippetPlaceholder(-1)) {
				failureReason = "live snippet sidekick: placeholder traversal failed.";
				TProgram::deskTop->remove(dialog);
				TObject::destroy(dialog);
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			const ushort backspaceCodes[] = {kbBack, kbCtrlH, kbCtrlBack};
			for (ushort keyCode : backspaceCodes) {
				TEvent event{};
				event.what = evKeyDown;
				event.keyDown.keyCode = keyCode;
				event.keyDown.charScan.charCode = keyCode == kbCtrlBack ? static_cast<char>(0x7F) : static_cast<char>(keyCode & 0xFF);
				snippetSidekick->handleEvent(event);
				if (event.what != evNothing) {
					failureReason = "live snippet sidekick: backspace variant was not consumed.";
					TProgram::deskTop->remove(dialog);
					TObject::destroy(dialog);
					TProgram::deskTop->remove(window);
					TObject::destroy(window);
					return false;
				}
			}
			TProgram::deskTop->remove(dialog);
			TObject::destroy(dialog);
			if (!editor->replaceBufferText(snippetDocumentText)) {
				failureReason = "live snippet sidekick: unable to seed traversal edge case.";
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			MRSnippetSidekickDialog *traversalDialog = new MRSnippetSidekickDialog(snippetSidekickBoundsFor(window, snippetText, replaceStart, 40, 20), window->bufferId(), replaceStart, replaceStart + 6, snippetText, "Snippet SideKick", placeholders);
			MRSidekickEditor *traversalSidekick = traversalDialog != nullptr ? traversalDialog->snippetSidekick() : nullptr;
			if (traversalDialog == nullptr || traversalSidekick == nullptr) {
				failureReason = "live snippet sidekick: traversal dialog construction failed.";
				if (traversalDialog != nullptr) TObject::destroy(traversalDialog);
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			TProgram::deskTop->insert(traversalDialog);
			if (!traversalSidekick->moveSnippetPlaceholder(1)) {
				failureReason = "live snippet sidekick: traversal did not move to second placeholder.";
				TProgram::deskTop->remove(traversalDialog);
				TObject::destroy(traversalDialog);
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			TEvent secondEvent{};
			secondEvent.what = evKeyDown;
			secondEvent.keyDown.keyCode = 'S';
			secondEvent.keyDown.charScan.charCode = 'S';
			traversalSidekick->handleEvent(secondEvent);
			if (secondEvent.what != evNothing || !traversalSidekick->moveSnippetPlaceholder(-1)) {
				failureReason = "live snippet sidekick: reverse traversal did not move to first placeholder.";
				TProgram::deskTop->remove(traversalDialog);
				TObject::destroy(traversalDialog);
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			TEvent firstEvent{};
			firstEvent.what = evKeyDown;
			firstEvent.keyDown.keyCode = 'F';
			firstEvent.keyDown.charScan.charCode = 'F';
			traversalSidekick->handleEvent(firstEvent);
			if (firstEvent.what != evNothing || !traversalSidekick->moveSnippetPlaceholder(1)) {
				failureReason = "live snippet sidekick: forward traversal did not revisit second placeholder start.";
				TProgram::deskTop->remove(traversalDialog);
				TObject::destroy(traversalDialog);
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			TEvent secondAgainEvent{};
			secondAgainEvent.what = evKeyDown;
			secondAgainEvent.keyDown.keyCode = 'T';
			secondAgainEvent.keyDown.charScan.charCode = 'T';
			traversalSidekick->handleEvent(secondAgainEvent);
			if (secondAgainEvent.what != evNothing) {
				failureReason = "live snippet sidekick: second placeholder start insert failed.";
				TProgram::deskTop->remove(traversalDialog);
				TObject::destroy(traversalDialog);
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			TEvent traversalCommitEvent{};
			traversalCommitEvent.what = evKeyDown;
			traversalCommitEvent.keyDown.keyCode = kbAltEnter;
			traversalCommitEvent.keyDown.controlKeyState = kbAltShift;
			traversalSidekick->handleEvent(traversalCommitEvent);
			if (editor->snapshotText() != "int main() {\n  printf(\"F\");TS\n}\n") {
				failureReason = "live snippet sidekick: placeholder traversal visited an edge instead of the next placeholder.";
				TProgram::deskTop->remove(traversalDialog);
				TObject::destroy(traversalDialog);
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			TProgram::deskTop->remove(traversalDialog);
			TObject::destroy(traversalDialog);
			if (!editor->replaceBufferText("abc old xyz")) {
				failureReason = "live snippet sidekick: unable to seed commit selection case.";
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			MRSnippetSidekickDialog *commitDialog = new MRSnippetSidekickDialog(snippetSidekickBoundsFor(window, "new", 4, 5, 1), window->bufferId(), 4, 7, "new", "Snippet SideKick", std::vector<MRSidekickSpan>());
			MRSidekickEditor *commitSidekick = commitDialog != nullptr ? commitDialog->snippetSidekick() : nullptr;
			if (commitDialog == nullptr || commitSidekick == nullptr) {
				failureReason = "live snippet sidekick: commit dialog construction failed.";
				if (commitDialog != nullptr) TObject::destroy(commitDialog);
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			TProgram::deskTop->insert(commitDialog);
			TEvent commitEvent{};
			commitEvent.what = evKeyDown;
			commitEvent.keyDown.keyCode = kbAltEnter;
			commitSidekick->handleEvent(commitEvent);
			if (editor->snapshotText() != "abc new xyz") {
				failureReason = "live snippet sidekick: commit did not replace the parent range.";
				TProgram::deskTop->remove(commitDialog);
				TObject::destroy(commitDialog);
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			if (editor->selectionStartOffset() != 7 || editor->selectionEndOffset() != 7) {
				failureReason = "live snippet sidekick: commit must leave parent text selection collapsed.";
				TProgram::deskTop->remove(commitDialog);
				TObject::destroy(commitDialog);
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			TProgram::deskTop->remove(commitDialog);
			TObject::destroy(commitDialog);
			const std::string scrolledLatexText = std::string(96, 'x') + "\\hline\n";
			const std::size_t scrolledReplaceStart = scrolledLatexText.find("\\hline");
			if (scrolledReplaceStart == std::string::npos || !editor->replaceBufferText(scrolledLatexText.c_str())) {
				failureReason = "live snippet sidekick: unable to seed horizontally scrolled LaTeX anchor case.";
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			editor->scrollTo(92, 0);
			const TPoint scrolledEditorGlobal = editor->makeGlobal(TPoint(0, 0));
			const TRect scrolledViewport = editor->visibleTextViewportBounds();
			const int scrolledVisualColumn = editor->charColumn(editor->lineStartOffset(scrolledReplaceStart), scrolledReplaceStart);
			const int expectedScrolledX = scrolledEditorGlobal.x + scrolledViewport.a.x + scrolledVisualColumn - editor->delta.x - 1;
			const int expectedScrolledY = scrolledEditorGlobal.y + scrolledViewport.a.y - 2;
			const TRect scrolledBounds = snippetSidekickBoundsFor(window, "\\hline", scrolledReplaceStart, 40, 20);
			if (scrolledBounds.a.x != expectedScrolledX || scrolledBounds.a.y != expectedScrolledY) {
				failureReason = "live snippet sidekick: horizontally scrolled LaTeX anchor must use visible view coordinates.";
				TProgram::deskTop->remove(window);
				TObject::destroy(window);
				return false;
			}
			TProgram::deskTop->remove(window);
			TObject::destroy(window);
		}
	failureReason.clear();
	return true;
}
