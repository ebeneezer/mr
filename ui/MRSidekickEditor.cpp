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
	romLeft,
	romRight,
	romBelowRight,
	romAboveRight
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
		width = std::max<int>(width, strwidth(line.c_str()));
	return width;
}

bool visibleEditorRowsHaveText(MRFileEditor &editor, int firstViewRow, int rowCount) {
	const MRTextBufferModel &model = editor.bufferModel();
	const int lastViewRow = firstViewRow + rowCount;

	for (int viewRow = firstViewRow; viewRow < lastViewRow; ++viewRow) {
		const int visibleRow = editor.delta.y + viewRow - 1;
		if (visibleRow < 0) continue;
		const std::size_t documentLine = editor.documentLineForVisibleLine(static_cast<std::size_t>(visibleRow));
		if (documentLine >= model.lineCount()) continue;
		const std::size_t lineStart = model.lineStartByIndex(documentLine);
		if (!trimAscii(model.lineText(lineStart)).empty()) return true;
	}
	return false;
}

std::vector<std::string> wrapReadOnlySidekickLines(const std::string &text, int contentWidth) {
	std::vector<std::string> sourceLines = splitLines(text);
	std::vector<std::string> wrapped;
	const int width = std::max(1, contentWidth);

	for (const std::string &source : sourceLines) {
		std::string line = source;
		while (strwidth(line.c_str()) > width) {
			int cut = 0;
			int lastSpace = -1;
			int column = 0;

			for (std::size_t index = 0; index < line.size(); ++index) {
				const unsigned char ch = static_cast<unsigned char>(line[index]);
				if ((ch & 0xC0) == 0x80) continue;
				std::size_t charEnd = index + 1;
				while (charEnd < line.size() && (static_cast<unsigned char>(line[charEnd]) & 0xC0) == 0x80)
					++charEnd;
				if (ch == ' ') lastSpace = static_cast<int>(index);
				++column;
				if (column > width) break;
				cut = static_cast<int>(charEnd);
			}
			if (lastSpace > 0 && lastSpace < cut) cut = lastSpace;
			if (cut <= 0) cut = 1;
			wrapped.push_back(trimAscii(line.substr(0, static_cast<std::size_t>(cut))));
			line.erase(0, static_cast<std::size_t>(cut));
			line = trimAscii(line);
		}
		wrapped.push_back(line);
	}
	if (wrapped.empty()) wrapped.push_back(std::string());
	return wrapped;
}

const char *readOnlyMarkerGlyph(ReadOnlyMarker marker) noexcept {
	switch (marker) {
		case romAbove:
		case romAboveRight:
			return "▼";
		case romLeft:
			return "◀";
		case romRight:
			return "▶";
		case romBelow:
		case romBelowRight:
		default:
			return "▲";
	}
}

std::string readOnlyTextWithMarker(const std::string &text, ReadOnlyMarker marker, int contentWidth, int visibleLineCount = 0) {
	const std::vector<std::string> lines = wrapReadOnlySidekickLines(text, contentWidth);
	std::string out;
	std::size_t markerLine = 0;
	const bool markerAtRight = marker == romRight || marker == romBelowRight || marker == romAboveRight;

	if ((marker == romAbove || marker == romAboveRight) && !lines.empty()) {
		const int visibleLines = visibleLineCount > 0 ? std::min<int>(visibleLineCount, static_cast<int>(lines.size())) : static_cast<int>(lines.size());
		markerLine = static_cast<std::size_t>(std::max(0, visibleLines - 1));
	}

	for (std::size_t index = 0; index < lines.size(); ++index) {
		if (index != 0) out.push_back('\n');
		if (markerAtRight) {
			out += lines[index];
			if (index == markerLine) {
				const int pad = std::max(0, contentWidth - strwidth(lines[index].c_str()));
				out.append(static_cast<std::size_t>(pad), ' ');
				out.push_back(' ');
				out += readOnlyMarkerGlyph(marker);
			}
		} else {
			if (index == markerLine) {
				out += readOnlyMarkerGlyph(marker);
				out.push_back(' ');
			} else
				out += "  ";
			out += lines[index];
		}
	}
	return out;
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
		return sidekickBoundsFor(parent, readOnlyTextWithMarker(text, marker, 40));
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
		return sidekickBoundsFor(parent, readOnlyTextWithMarker(text, marker, 40));
	}

	const int maxSidekickWidth = std::max(12, (viewport.b.x - viewport.a.x) / 2);
	const int maxContentWidth = std::max(8, maxSidekickWidth - 2);
	const int cursorX = std::clamp(editorGlobal.x + textViewport.a.x + std::max(0, anchorViewColumn - 1), viewport.a.x, std::max(viewport.a.x, viewport.b.x - 1));
	const int cursorY = std::clamp(editorGlobal.y + textViewport.a.y + std::max(0, anchorViewRow - 1), viewport.a.y, std::max(viewport.a.y, viewport.b.y - 1));
	const int belowCodeSpace = std::max(0, viewport.b.y - (cursorY + 1));
	const int aboveCodeSpace = std::max(0, cursorY - viewport.a.y);
	const int targetX = preferredViewColumn > 0 ? std::clamp(editorGlobal.x + textViewport.a.x + preferredViewColumn - 1, viewport.a.x, std::max(viewport.a.x, viewport.b.x - 1)) : cursorX;

	if (placement == MRReadOnlySidekickPlacement::UnderCode) {
		const int viewportWidth = std::max(1, viewport.b.x - viewport.a.x);
		const int readableWidth = std::min(maxSidekickWidth, viewportWidth);
		const int minimumWidth = std::min(readableWidth, 12);
		const int rightAvailable = std::max(0, viewport.b.x - targetX);
		const int leftAvailable = std::max(0, targetX - viewport.a.x + 1);
		bool markerAtRight = false;
		marker = romBelow;
		std::vector<std::string> lines = splitLines(readOnlyTextWithMarker(text, marker, maxContentWidth));
		int wantedWidth = std::clamp(maxLineLength(lines), 1, readableWidth);
		int x = targetX;

		if (wantedWidth > rightAvailable) {
			if (leftAvailable >= minimumWidth) {
				wantedWidth = std::min(wantedWidth, leftAvailable);
				x = targetX - wantedWidth + 1;
				markerAtRight = true;
				marker = romBelowRight;
				lines = splitLines(readOnlyTextWithMarker(text, marker, std::max(1, wantedWidth - 2)));
			} else {
				wantedWidth = std::min(wantedWidth, std::max(1, viewport.b.x - viewport.a.x));
				x = viewport.b.x - wantedWidth;
				lines = splitLines(readOnlyTextWithMarker(text, marker, std::max(1, wantedWidth - 2)));
			}
		}
		const int lineCount = static_cast<int>(lines.size());
		const bool belowFits = belowCodeSpace >= lineCount;
		const bool aboveFits = aboveCodeSpace >= lineCount;
		const bool belowUsesText = belowFits && visibleEditorRowsHaveText(*editor, anchorViewRow + 1, lineCount);
		const bool aboveUsesText = aboveFits && visibleEditorRowsHaveText(*editor, anchorViewRow - lineCount, lineCount);
		bool above = false;

		if (belowFits && !belowUsesText)
			above = false;
		else if (aboveFits && !aboveUsesText)
			above = true;
		else if (!belowFits && aboveCodeSpace > belowCodeSpace)
			above = true;
		else if (aboveFits && aboveCodeSpace > belowCodeSpace)
			above = true;
		marker = above ? (markerAtRight ? romAboveRight : romAbove) : (markerAtRight ? romBelowRight : romBelow);
		lines = splitLines(readOnlyTextWithMarker(text, marker, std::max(1, wantedWidth - 2)));
		const int verticalSpace = above ? aboveCodeSpace : belowCodeSpace;
		const int wantedHeight = std::max(1, std::min<int>(static_cast<int>(lines.size()), std::max(1, verticalSpace)));
		const int y = above ? cursorY - wantedHeight - 1 : cursorY;
		return TRect(x, y, x + wantedWidth, y + wantedHeight);
	}

	const int belowSpace = std::max(0, viewport.b.y - cursorY);
	bool above = belowSpace < 3 && aboveCodeSpace > belowSpace;
	marker = above ? romAbove : romLeft;
	std::vector<std::string> lines = splitLines(readOnlyTextWithMarker(text, marker, maxContentWidth));
	const int lineCount = static_cast<int>(lines.size());
	if (belowSpace < lineCount && aboveCodeSpace >= lineCount) above = true;
	else if (belowSpace <= 0 && aboveCodeSpace > 0)
		above = true;
	marker = above ? romAbove : romLeft;
	if (above) lines = splitLines(readOnlyTextWithMarker(text, marker, maxContentWidth));
	const int viewportWidth = std::max(1, viewport.b.x - viewport.a.x);
	const int readableWidth = std::min(maxSidekickWidth, viewportWidth);
	const int minimumWidth = std::min(readableWidth, 12);
	int wantedWidth = std::clamp(maxLineLength(lines), 1, readableWidth);
	int x = viewport.b.x - wantedWidth;
	const int rightAvailable = std::max(0, viewport.b.x - (targetX + 1));
	const int leftAvailable = std::max(0, targetX - viewport.a.x);

	if (x <= targetX && targetX < x + wantedWidth) {
		if (rightAvailable >= minimumWidth) {
			wantedWidth = std::min(wantedWidth, rightAvailable);
			x = targetX + 1;
			marker = above ? romAbove : romLeft;
		} else if (leftAvailable >= minimumWidth) {
			wantedWidth = std::min(wantedWidth, leftAvailable);
			x = targetX - wantedWidth;
			marker = above ? romAbove : romRight;
		} else {
			wantedWidth = readableWidth;
			x = viewport.b.x - wantedWidth;
			marker = above ? romAbove : romLeft;
		}
	}
	lines = splitLines(readOnlyTextWithMarker(text, marker, std::max(1, wantedWidth - 2)));

	const int verticalSpace = std::max(1, above ? aboveCodeSpace : belowSpace);
	const int wantedHeight = std::clamp<int>(static_cast<int>(lines.size()), 1, verticalSpace);
	const int y = (belowSpace == 0 && aboveCodeSpace == 0) ? cursorY : (above ? cursorY - wantedHeight : cursorY);
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
			if (mReadOnly) {
				buffer.moveStr(0, line.c_str(), textColor, static_cast<ushort>(size.x));
			} else {
				const int visible = std::min<int>(line.size(), std::max(0, size.x - textX - 1));
				for (int x = 0; x < visible; ++x) {
					const std::size_t offset = lineStartOffset + static_cast<std::size_t>(x);
					buffer.moveChar(static_cast<ushort>(x + textX), line[static_cast<std::size_t>(x)], offsetInPlaceholder(offset) ? highlightColor : textColor, 1);
				}
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
	    {"EOF viewport space below is usable", "one\ntwo\nthree\nlast", "error 4:3 - tail", 3, 4, 3, romBelow, false},
	    {"blank document line below is usable", "one\n\nthree", "error 1:3 - blank target", 3, 1, 3, romBelow, false},
	    {"text below pushes sidekick above", "\n\nerr\ntext", "error 3:3 - text below", 3, 3, 3, romAbove, true},
	    {"diagnostic above following code line", "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\ni=\"dumm\";\nreturn(0);", "error 17:3 - Use of undeclared identifier 'i'", 3, 17, 3, romAbove, true},
	    {"right edge above keeps down glyph at right", "\n\nerr\ntext", "error 3:70 - edge", 76, 3, 76, romAboveRight, true},
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
		const int anchorY = editorGlobal.y + textViewport.a.y + testCase.anchorRow - 1;
		const TRect bounds = readOnlySidekickBoundsFor(&window, testCase.hintText, marker, testCase.anchorColumn, testCase.anchorRow, testCase.preferredColumn, MRReadOnlySidekickPlacement::UnderCode);

		if (marker != testCase.expectedMarker) {
			failureReason = std::string(testCase.name) + ": marker mismatch.";
			return false;
		}
		if (testCase.expectAbove) {
			if (bounds.b.y != anchorY - 1) {
				failureReason = std::string(testCase.name) + ": above sidekick must render above the anchor row; anchorY=" + std::to_string(anchorY) + " bounds=" + std::to_string(bounds.a.y) + ".." + std::to_string(bounds.b.y) + ".";
				return false;
			}
		} else if (bounds.a.y != anchorY) {
			failureReason = std::string(testCase.name) + ": below sidekick must render directly below the anchor row; anchorY=" + std::to_string(anchorY) + " bounds=" + std::to_string(bounds.a.y) + ".." + std::to_string(bounds.b.y) + ".";
			return false;
		}
		if (testCase.expectAbove && bounds.a.y <= anchorY && anchorY < bounds.b.y) {
			failureReason = std::string(testCase.name) + ": above sidekick covers anchor row.";
			return false;
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
		const int anchorRow = editor->visibleViewportRows();
		const TPoint editorGlobal = editor->makeGlobal(TPoint(0, 0));
		const TRect textViewport = editor->visibleTextViewportBounds();
		const int anchorY = editorGlobal.y + textViewport.a.y + anchorRow - 1;
		ReadOnlyMarker marker = romBelow;
		const TRect bounds = readOnlySidekickBoundsFor(&window, "error bottom - no below space", marker, 3, anchorRow, 3, MRReadOnlySidekickPlacement::UnderCode);

		if (marker != romAbove) {
			failureReason = "bottom viewport row: marker mismatch.";
			return false;
		}
		if (bounds.b.y != anchorY - 1) {
			failureReason = "bottom viewport row: above sidekick must end directly above the anchor row.";
			return false;
		}
		if (bounds.a.y <= anchorY && anchorY < bounds.b.y) {
			failureReason = "bottom viewport row: sidekick covers anchor row.";
			return false;
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
		const int anchorY = editorGlobal.y + textViewport.a.y + 16;

		if (!mrOpenReadOnlySidekickAt(window, "tail", "LSP hover", 3, 4, 3, MRReadOnlySidekickPlacement::UnderCode)) {
			failureReason = "live sidekick geometry: initial open failed.";
			TProgram::deskTop->remove(window);
			TObject::destroy(window);
			return false;
		}
		if (!mrOpenReadOnlySidekickAt(window, "error 17:3 - Use of undeclared identifier 'i'", "LSP hover", 3, 17, 3, MRReadOnlySidekickPlacement::UnderCode)) {
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
		const TRect bounds = gActiveSidekick->getBounds();
		if (bounds.b.y != anchorY - 1) {
			failureReason = "live sidekick geometry: updated above sidekick must render above the anchor row; anchorY=" + std::to_string(anchorY) + " bounds=" + std::to_string(bounds.a.y) + ".." + std::to_string(bounds.b.y) + ".";
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
		mrDropActiveSidekick();
		TProgram::deskTop->remove(window);
		TObject::destroy(window);
	}
	failureReason.clear();
	return true;
}
