#define Uses_TDrawBuffer
#define Uses_TDeskTop
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TView
#include <tvision/tv.h>

#include "MRSidekickEditor.hpp"

#include "MREditWindow.hpp"
#include "MRFileEditor/MRFileEditor.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../app/commands/MRWindowCommands.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <utility>

namespace {

MRSidekickEditor *gActiveSidekick = nullptr;
int gDismissedReadOnlySidekickParentBufferId = 0;

constexpr TColorAttr kSidekickCursor = 0x70;

enum ReadOnlyMarker {
	romBelow,
	romAbove,
	romLeft
};

TColorAttr sidekickColor(unsigned char paletteSlot, TColorAttr fallback) {
	unsigned char configured = 0;
	if (configuredColorSlotOverride(paletteSlot, configured)) return static_cast<TColorAttr>(configured);
	return fallback;
}

std::vector<std::string> splitLines(const std::string &text) {
	std::vector<std::string> lines;
	std::string current;

	for (char ch : text) {
		if (ch == '\r') continue;
		if (ch == '\n') {
			lines.push_back(current);
			current.clear();
			continue;
		}
		current.push_back(ch);
	}
	lines.push_back(current);
	return lines;
}

int maxLineLength(const std::vector<std::string> &lines) {
	int width = 1;
	for (const std::string &line : lines)
		width = std::max<int>(width, line.size());
	return width;
}

std::string readOnlyTextWithMarker(const std::string &text, ReadOnlyMarker marker) {
	switch (marker) {
		case romAbove: {
			const std::vector<std::string> lines = splitLines(text);
			std::ostringstream marked;
			for (std::size_t i = 0; i < lines.size(); ++i) {
				if (i != 0) marked << '\n';
				marked << (i == lines.size() - 1 ? "v " : "  ");
				marked << lines[i];
			}
			return marked.str();
		}
		case romLeft:
			return std::string("< ") + text;
		case romBelow:
			break;
	}
	const std::vector<std::string> lines = splitLines(text);
	std::ostringstream marked;
	for (std::size_t i = 0; i < lines.size(); ++i) {
		if (i != 0) marked << '\n';
		marked << (i == 0 ? "^ " : "  ");
		marked << lines[i];
	}
	return marked.str();
}

TRect sidekickBoundsFor(MREditWindow *parent, const std::string &text) {
	MRFileEditor *editor = parent != nullptr ? parent->getEditor() : nullptr;
	TRect desktop = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, 80, 25);
	const std::vector<std::string> lines = splitLines(text);
	int wantedWidth = std::clamp(maxLineLength(lines) + 2, 24, std::max(24, desktop.b.x - desktop.a.x - 2));
	const int wantedHeight = std::clamp<int>(static_cast<int>(lines.size()), 3, std::max(3, desktop.b.y - desktop.a.y - 2));
	int x = desktop.a.x + 2;
	int y = desktop.a.y + 2;

	if (editor != nullptr) {
		const TPoint editorGlobal = editor->makeGlobal(TPoint(0, 0));
		x = editorGlobal.x + std::max(0, editor->currentViewColumn() - 1);
		y = editorGlobal.y + std::max(0, editor->currentViewRow() - 1);
		x = std::clamp(x, desktop.a.x, std::max(desktop.a.x, desktop.b.x - 1));
		wantedWidth = std::min(wantedWidth, std::max(1, desktop.b.x - x));
	} else {
		wantedWidth = std::min(wantedWidth, std::max(24, desktop.b.x - desktop.a.x - 2));
		x = std::clamp(x, desktop.a.x, std::max(desktop.a.x, desktop.b.x - wantedWidth));
	}
	y = std::clamp(y, desktop.a.y, std::max(desktop.a.y, desktop.b.y - wantedHeight));
	return TRect(x, y, x + wantedWidth, y + wantedHeight);
}

TRect readOnlySidekickBoundsFor(MREditWindow *parent, const std::string &text, ReadOnlyMarker &marker, int anchorViewColumn, int anchorViewRow, int preferredViewColumn, MRReadOnlySidekickPlacement placement) {
	MRFileEditor *editor = parent != nullptr ? parent->getEditor() : nullptr;
	TRect desktop = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, 80, 25);
	if (editor == nullptr) {
		marker = romBelow;
		return sidekickBoundsFor(parent, readOnlyTextWithMarker(text, marker));
	}

	const TPoint editorGlobal = editor->makeGlobal(TPoint(0, 0));
	const TRect textViewport = editor->visibleTextViewportBounds();
	TRect viewport(editorGlobal.x + textViewport.a.x, editorGlobal.y + textViewport.a.y, editorGlobal.x + textViewport.b.x, editorGlobal.y + textViewport.b.y);
	viewport.a.x = std::max(viewport.a.x, desktop.a.x);
	viewport.a.y = std::max(viewport.a.y, desktop.a.y);
	viewport.b.x = std::min(viewport.b.x, desktop.b.x);
	viewport.b.y = std::min(viewport.b.y, desktop.b.y);
	if (viewport.b.x <= viewport.a.x || viewport.b.y <= viewport.a.y) {
		marker = romBelow;
		return sidekickBoundsFor(parent, readOnlyTextWithMarker(text, marker));
	}

	const int cursorX = std::clamp(editorGlobal.x + textViewport.a.x + std::max(0, anchorViewColumn - 1), viewport.a.x, std::max(viewport.a.x, viewport.b.x - 1));
	const int cursorY = std::clamp(editorGlobal.y + textViewport.a.y + std::max(0, anchorViewRow - 1), viewport.a.y, std::max(viewport.a.y, viewport.b.y - 1));
	const int belowCodeSpace = std::max(0, viewport.b.y - cursorY);
	const int aboveSpace = std::max(0, cursorY - viewport.a.y);
	const int targetX = preferredViewColumn > 0 ? std::clamp(editorGlobal.x + textViewport.a.x + preferredViewColumn - 1, viewport.a.x, std::max(viewport.a.x, viewport.b.x - 1)) : cursorX;
	const int preferredX = preferredViewColumn > 0 ? std::clamp(editorGlobal.x + textViewport.a.x + preferredViewColumn - 1, targetX, std::max(targetX, viewport.b.x - 1)) : targetX;

	if (placement == MRReadOnlySidekickPlacement::UnderCode) {
		bool above = belowCodeSpace <= 0 && aboveSpace > 0;
		marker = above ? romAbove : romBelow;
		std::vector<std::string> lines = splitLines(readOnlyTextWithMarker(text, marker));
		const int lineCount = static_cast<int>(lines.size());
		if (belowCodeSpace < lineCount && aboveSpace >= lineCount) above = true;
		marker = above ? romAbove : romBelow;
		if (above) lines = splitLines(readOnlyTextWithMarker(text, marker));

		const int x = targetX;
		const int wantedWidth = std::clamp(maxLineLength(lines), 1, std::max(1, viewport.b.x - x));
		const int verticalSpace = std::max(1, above ? aboveSpace : belowCodeSpace);
		const int wantedHeight = std::clamp<int>(static_cast<int>(lines.size()), 1, verticalSpace);
		int y = above ? cursorY - wantedHeight : cursorY;
		y = std::clamp(y, viewport.a.y, std::max(viewport.a.y, viewport.b.y - wantedHeight));
		return TRect(x, y, x + wantedWidth, y + wantedHeight);
	}

	const int belowSpace = std::max(0, viewport.b.y - cursorY);
	bool above = belowSpace < 3 && aboveSpace > belowSpace;
	marker = above ? romAbove : romBelow;
	std::vector<std::string> lines = splitLines(readOnlyTextWithMarker(text, marker));
	const int lineCount = static_cast<int>(lines.size());
	if (belowSpace < lineCount && aboveSpace >= lineCount) above = true;
	else if (belowSpace <= 0 && aboveSpace > 0)
		above = true;
	marker = above ? romAbove : romBelow;
	if (above) lines = splitLines(readOnlyTextWithMarker(text, marker));
	int wantedWidth = std::clamp(maxLineLength(lines), 1, std::max(1, viewport.b.x - viewport.a.x));
	const int rightAlignedX = std::max(targetX, viewport.b.x - wantedWidth);
	int x = std::max(preferredX, rightAlignedX);
	if (x + wantedWidth > viewport.b.x) x = rightAlignedX;
	x = std::clamp(x, targetX, std::max(targetX, viewport.b.x - 1));
	if (!above && x > targetX) {
		marker = romLeft;
		lines = splitLines(readOnlyTextWithMarker(text, marker));
		wantedWidth = std::clamp(maxLineLength(lines), 1, std::max(1, viewport.b.x - viewport.a.x));
	}
	wantedWidth = std::min(wantedWidth, std::max(1, viewport.b.x - x));

	const int verticalSpace = std::max(1, above ? aboveSpace : belowSpace);
	const int wantedHeight = std::clamp<int>(static_cast<int>(lines.size()), 1, verticalSpace);
	const int y = (belowSpace == 0 && aboveSpace == 0) ? cursorY : (above ? cursorY - wantedHeight : cursorY);
	return TRect(x, y, x + wantedWidth, y + wantedHeight);
}

} // namespace

MRSidekickEditor::MRSidekickEditor(const TRect &bounds, int parentBufferId, std::size_t replaceStart, std::size_t replaceEnd, std::string text, std::string title, std::vector<MRSidekickSpan> placeholders, bool readOnly)
    : TView(bounds), mParentBufferId(parentBufferId), mReplaceStart(replaceStart), mReplaceEnd(replaceEnd), mTitle(std::move(title)), mLines(), mPlaceholders(std::move(placeholders)), mPlaceholderIndex(-1), mCursorRow(0), mCursorCol(0), mReadOnly(readOnly) {
	if (!mReadOnly) options |= ofSelectable;
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
	const TColorAttr textColor = sidekickColor(kMrPaletteSidekickEditorText, 0x30);
	const TColorAttr highlightColor = sidekickColor(kMrPaletteSidekickEditorHighlight, 0xE0);
	std::size_t lineStartOffset = 0;

	for (int y = 0; y < size.y; ++y) {
		TDrawBuffer buffer;
		buffer.moveChar(0, ' ', textColor, size.x);
		if (y < static_cast<int>(mLines.size())) {
			const std::string &line = mLines[static_cast<std::size_t>(y)];
			const int textX = mReadOnly ? 0 : 1;
			const int visible = std::min<int>(line.size(), std::max(0, size.x - textX - (mReadOnly ? 0 : 1)));
			for (int x = 0; x < visible; ++x) {
				const std::size_t offset = lineStartOffset + static_cast<std::size_t>(x);
				buffer.moveChar(static_cast<ushort>(x + textX), line[static_cast<std::size_t>(x)], offsetInPlaceholder(offset) ? highlightColor : textColor, 1);
			}
		}
		if (!mReadOnly && y == mCursorRow) {
			const int cursorX = std::clamp(mCursorCol + 1, 0, std::max(0, size.x - 1));
			const char cursorChar = (mCursorCol >= 0 && mCursorCol < static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow)].size())) ? mLines[static_cast<std::size_t>(mCursorRow)][static_cast<std::size_t>(mCursorCol)] : ' ';
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
	const bool altPressed = (mods & kbAltShift) != 0;
	const bool shiftPressed = (mods & kbShift) != 0;
	const bool ctrlEnterPressed = event.keyDown.keyCode == kbCtrlEnter || key == TKey(kbEnter, kbCtrlShift);
	const bool altEnterPressed = event.keyDown.keyCode == kbAltEnter || key == TKey(kbEnter, kbAltShift) || (altPressed && (event.keyDown.keyCode == kbEnter || arrowKey == kbEnter));
	const bool shiftTabPressed = event.keyDown.keyCode == kbShiftTab || ((event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI || event.keyDown.charScan.charCode == '\t') && shiftPressed);
	const bool tabPressed = !shiftTabPressed && (event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI || event.keyDown.charScan.charCode == '\t');

	if (ctrlEnterPressed || altEnterPressed) {
		if (!mReadOnly) commitAndClose();
		clearEvent(event);
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
	switch (arrowKey) {
		case kbEsc:
			closeSidekick();
			clearEvent(event);
			return;
		case kbEnter:
			if (mReadOnly) {
				clearEvent(event);
				return;
			}
			insertNewLine();
			break;
		case kbBack:
			if (mReadOnly) {
				clearEvent(event);
				return;
			}
			eraseBackward();
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
			const unsigned char ch = static_cast<unsigned char>(event.keyDown.charScan.charCode);
			if (ch == '\t') {
				if (mReadOnly) {
					clearEvent(event);
					return;
				}
				insertChar('\t');
				break;
			}
			if (ch >= 32 && ch < 127) {
				if (mReadOnly) {
					clearEvent(event);
					return;
				}
				insertChar(static_cast<char>(ch));
				break;
			}
			TView::handleEvent(event);
			return;
		}
	}
	drawView();
	clearEvent(event);
}

void MRSidekickEditor::closeSidekick() {
	TGroup *group = owner;
	if (mReadOnly) gDismissedReadOnlySidekickParentBufferId = mParentBufferId;
	if (group != nullptr) group->remove(this);
	TObject::destroy(this);
}

void MRSidekickEditor::commitAndClose() {
	MREditWindow *parent = findEditWindowByBufferId(mParentBufferId);
	MRFileEditor *editor = parent != nullptr ? parent->getEditor() : nullptr;
	const std::string replacement = text();

	if (editor != nullptr && !editor->isReadOnly()) editor->replaceRangeAndSelect(static_cast<uint>(mReplaceStart), static_cast<uint>(mReplaceEnd), replacement.c_str(), static_cast<uint>(replacement.size()));
	closeSidekick();
}

void MRSidekickEditor::insertChar(char ch) {
	const std::size_t offset = cursorOffset();
	std::string &line = mLines[static_cast<std::size_t>(mCursorRow)];
	line.insert(static_cast<std::size_t>(mCursorCol), 1, ch);
	++mCursorCol;
	adjustPlaceholdersAfterInsert(offset, 1);
}

void MRSidekickEditor::insertNewLine() {
	const std::size_t offset = cursorOffset();
	std::string &line = mLines[static_cast<std::size_t>(mCursorRow)];
	std::string tail = line.substr(static_cast<std::size_t>(mCursorCol));
	line.erase(static_cast<std::size_t>(mCursorCol));
	mLines.insert(mLines.begin() + mCursorRow + 1, tail);
	++mCursorRow;
	mCursorCol = 0;
	adjustPlaceholdersAfterInsert(offset, 1);
}

void MRSidekickEditor::eraseBackward() {
	if (mCursorCol > 0) {
		const std::size_t eraseOffset = cursorOffset() - 1;
		std::string &line = mLines[static_cast<std::size_t>(mCursorRow)];
		line.erase(static_cast<std::size_t>(mCursorCol - 1), 1);
		--mCursorCol;
		adjustPlaceholdersAfterErase(eraseOffset, 1);
		return;
	}
	if (mCursorRow <= 0) return;
	const std::size_t eraseOffset = cursorOffset() - 1;
	const int previousLength = static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow - 1)].size());
	mLines[static_cast<std::size_t>(mCursorRow - 1)] += mLines[static_cast<std::size_t>(mCursorRow)];
	mLines.erase(mLines.begin() + mCursorRow);
	--mCursorRow;
	mCursorCol = previousLength;
	adjustPlaceholdersAfterErase(eraseOffset, 1);
}

void MRSidekickEditor::eraseForward() {
	const std::size_t eraseOffset = cursorOffset();
	std::string &line = mLines[static_cast<std::size_t>(mCursorRow)];
	if (mCursorCol < static_cast<int>(line.size())) {
		line.erase(static_cast<std::size_t>(mCursorCol), 1);
		adjustPlaceholdersAfterErase(eraseOffset, 1);
		return;
	}
	if (mCursorRow + 1 >= static_cast<int>(mLines.size())) return;
	line += mLines[static_cast<std::size_t>(mCursorRow + 1)];
	mLines.erase(mLines.begin() + mCursorRow + 1);
	adjustPlaceholdersAfterErase(eraseOffset, 1);
}

void MRSidekickEditor::moveLeft() {
	if (mCursorCol > 0) {
		--mCursorCol;
		return;
	}
	if (mCursorRow > 0) {
		--mCursorRow;
		mCursorCol = static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow)].size());
	}
}

void MRSidekickEditor::moveRight() {
	if (mCursorCol < static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow)].size())) {
		++mCursorCol;
		return;
	}
	if (mCursorRow + 1 < static_cast<int>(mLines.size())) {
		++mCursorRow;
		mCursorCol = 0;
	}
}

void MRSidekickEditor::moveUp() {
	if (mCursorRow > 0) --mCursorRow;
	clampCursor();
}

void MRSidekickEditor::moveDown() {
	if (mCursorRow + 1 < static_cast<int>(mLines.size())) ++mCursorRow;
	clampCursor();
}

void MRSidekickEditor::moveToPlaceholder(int direction) {
	if (mPlaceholders.empty()) return;
	if (mPlaceholderIndex < 0)
		mPlaceholderIndex = direction >= 0 ? 0 : static_cast<int>(mPlaceholders.size()) - 1;
	else
		mPlaceholderIndex = (mPlaceholderIndex + direction + static_cast<int>(mPlaceholders.size())) % static_cast<int>(mPlaceholders.size());
	setCursorFromOffset(mPlaceholders[static_cast<std::size_t>(mPlaceholderIndex)].start);
}

void MRSidekickEditor::setCursorFromOffset(std::size_t offset) {
	std::size_t lineStart = 0;

	for (std::size_t row = 0; row < mLines.size(); ++row) {
		const std::size_t lineLength = mLines[row].size();
		if (offset <= lineStart + lineLength) {
			mCursorRow = static_cast<int>(row);
			mCursorCol = static_cast<int>(offset - lineStart);
			clampCursor();
			return;
		}
		lineStart += lineLength + 1;
	}
	mCursorRow = static_cast<int>(mLines.size()) - 1;
	mCursorCol = static_cast<int>(mLines.back().size());
}

std::size_t MRSidekickEditor::cursorOffset() const noexcept {
	std::size_t offset = 0;

	for (int row = 0; row < mCursorRow && row < static_cast<int>(mLines.size()); ++row)
		offset += mLines[static_cast<std::size_t>(row)].size() + 1;
	return offset + static_cast<std::size_t>(std::max(0, mCursorCol));
}

bool MRSidekickEditor::offsetInPlaceholder(std::size_t offset) const noexcept {
	for (const MRSidekickSpan &span : mPlaceholders)
		if (offset >= span.start && offset < span.end) return true;
	return false;
}

void MRSidekickEditor::adjustPlaceholdersAfterInsert(std::size_t offset, std::size_t length) {
	for (MRSidekickSpan &span : mPlaceholders) {
		if (offset <= span.start) {
			span.start += length;
			span.end += length;
		} else if (offset < span.end)
			span.end += length;
	}
}

void MRSidekickEditor::adjustPlaceholdersAfterErase(std::size_t offset, std::size_t length) {
	const std::size_t eraseEnd = offset + length;

	for (MRSidekickSpan &span : mPlaceholders) {
		if (eraseEnd <= span.start) {
			span.start -= std::min(length, span.start);
			span.end -= std::min(length, span.end);
		} else if (offset < span.end) {
			const std::size_t overlapStart = std::max(offset, span.start);
			const std::size_t overlapEnd = std::min(eraseEnd, span.end);
			span.end -= overlapEnd > overlapStart ? overlapEnd - overlapStart : 0;
			if (offset < span.start) span.start = offset;
			if (span.end < span.start) span.end = span.start;
		}
	}
}

void MRSidekickEditor::clampCursor() noexcept {
	if (mLines.empty()) mLines.push_back(std::string());
	mCursorRow = std::clamp(mCursorRow, 0, static_cast<int>(mLines.size()) - 1);
	mCursorCol = std::clamp(mCursorCol, 0, static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow)].size()));
}

bool mrOpenSnippetSidekick(MREditWindow *parent, std::size_t replaceStart, std::size_t replaceEnd, const std::string &text, const std::string &title, const std::vector<MRSidekickSpan> &placeholders) {
	if (parent == nullptr || parent->getEditor() == nullptr || TProgram::deskTop == nullptr) return false;
	mrDropActiveSidekick();
	MRSidekickEditor *sidekick = new MRSidekickEditor(sidekickBoundsFor(parent, text), parent->bufferId(), replaceStart, replaceEnd, text, title, placeholders);
	if (sidekick == nullptr) return false;
	gActiveSidekick = sidekick;
	TProgram::deskTop->insert(sidekick);
	TProgram::deskTop->setCurrent(sidekick, TView::normalSelect);
	sidekick->drawView();
	return true;
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
	const std::string markedText = readOnlyTextWithMarker(text, marker);
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

bool mrHasReadOnlySidekickForParent(const MREditWindow *parent) {
	return parent != nullptr && gActiveSidekick != nullptr && gActiveSidekick->parentBufferId() == parent->bufferId() && gActiveSidekick->isReadOnly();
}

bool mrConsumeReadOnlySidekickDismissedForParent(const MREditWindow *parent) {
	if (parent == nullptr || gDismissedReadOnlySidekickParentBufferId != parent->bufferId()) return false;
	gDismissedReadOnlySidekickParentBufferId = 0;
	return true;
}

void mrDropSidekickForParent(const MREditWindow *parent) {
	if (parent == nullptr || gActiveSidekick == nullptr) return;
	if (gActiveSidekick->parentBufferId() == parent->bufferId()) mrDropActiveSidekick();
}

void mrDropActiveSidekick() {
	MRSidekickEditor *sidekick = gActiveSidekick;
	if (sidekick == nullptr) return;
	TGroup *group = sidekick->owner;
	gActiveSidekick = nullptr;
	if (group != nullptr) group->remove(sidekick);
	TObject::destroy(sidekick);
}
