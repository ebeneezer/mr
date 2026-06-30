#define Uses_TDrawBuffer
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TView
#define Uses_TWindow
#include <tvision/tv.h>

#include "MRSidekickEditor.hpp"

#include "MREditWindow.hpp"
#include "MRFrame.hpp"
#include "MRFileEditor/MRFileEditor.hpp"
#include "MRWindowSupport.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../app/MRCommands.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#include "../keymap/MRKeymapContext.hpp"
#include "../keymap/MRKeymapResolver.hpp"
#include "../keymap/MRKeymapToken.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <utility>

namespace {

MRSidekickEditor *gActiveSidekick = nullptr;
int gDismissedReadOnlySidekickParentBufferId = 0;

constexpr TColorAttr kSidekickCursor = 0x70;

enum class SnippetSidekickAction : unsigned char {
	CursorLeft,
	CursorRight,
	CursorUp,
	CursorDown,
	CursorHome,
	CursorEnd,
	CursorWordLeft,
	CursorWordRight,
	DeleteBackwardChar,
	DeleteForwardChar,
	DeleteBackwardWord,
	DeleteForwardWord,
	DeleteBackwardToHome,
	DeleteToEndOfLine,
	DeleteLine,
	LoadBlockFromFile,
	PlaceholderNext,
	PlaceholderPrevious
};

struct SnippetSidekickActionEntry {
	const char *actionId;
	SnippetSidekickAction action;
};

constexpr SnippetSidekickActionEntry kSnippetSidekickActions[] = {
    {"MRMAC_CURSOR_LEFT", SnippetSidekickAction::CursorLeft},
    {"MRMAC_CURSOR_RIGHT", SnippetSidekickAction::CursorRight},
    {"MRMAC_CURSOR_UP", SnippetSidekickAction::CursorUp},
    {"MRMAC_CURSOR_DOWN", SnippetSidekickAction::CursorDown},
    {"MRMAC_CURSOR_HOME", SnippetSidekickAction::CursorHome},
    {"MRMAC_CURSOR_END_OF_LINE", SnippetSidekickAction::CursorEnd},
    {"MRMAC_CURSOR_WORD_LEFT", SnippetSidekickAction::CursorWordLeft},
    {"MRMAC_CURSOR_WORD_RIGHT", SnippetSidekickAction::CursorWordRight},
    {"MRMAC_DELETE_BACKWARD_CHAR", SnippetSidekickAction::DeleteBackwardChar},
    {"MRMAC_DELETE_FORWARD_CHAR", SnippetSidekickAction::DeleteForwardChar},
    {"MRMAC_DELETE_FORWARD_CHAR_OR_BLOCK", SnippetSidekickAction::DeleteForwardChar},
    {"MRMAC_DELETE_BACKWARD_WORD", SnippetSidekickAction::DeleteBackwardWord},
    {"MRMAC_DELETE_FORWARD_WORD", SnippetSidekickAction::DeleteForwardWord},
    {"MRMAC_DELETE_BACKWARD_TO_HOME", SnippetSidekickAction::DeleteBackwardToHome},
    {"MRMAC_DELETE_TO_EOL", SnippetSidekickAction::DeleteToEndOfLine},
    {"MRMAC_DELETE_LINE", SnippetSidekickAction::DeleteLine},
    {"MR_LOAD_BLOCK_FROM_FILE", SnippetSidekickAction::LoadBlockFromFile},
    {"MR_SNIPPET_PLACEHOLDER_NEXT", SnippetSidekickAction::PlaceholderNext},
    {"MR_SNIPPET_PLACEHOLDER_PREVIOUS", SnippetSidekickAction::PlaceholderPrevious},
};

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

std::string expandSidekickTabs(const std::string &value) {
	const int tabSize = std::max(1, configuredEditSetupSettings().tabSize);
	std::string out;
	int column = 0;

	for (char raw : value) {
		const unsigned char ch = static_cast<unsigned char>(raw);
		if (raw == '\r') continue;
		if (raw == '\n') {
			out.push_back('\n');
			column = 0;
			continue;
		}
		if (raw == '\t') {
			const int spaces = tabSize - (column % tabSize);
			out.append(static_cast<std::size_t>(spaces), ' ');
			column += spaces;
			continue;
		}
		out.push_back(ch < 32 ? ' ' : raw);
		++column;
	}
	return out;
}

bool snippetSidekickActionFromId(const std::string &actionId, SnippetSidekickAction &action) noexcept {
	for (const SnippetSidekickActionEntry &entry : kSnippetSidekickActions) {
		if (actionId == entry.actionId) {
			action = entry.action;
			return true;
		}
	}
	return false;
}

bool snippetSidekickWordByte(char ch) noexcept {
	const unsigned char value = static_cast<unsigned char>(ch);
	return std::isalnum(value) != 0 || ch == '_';
}

std::size_t snippetSidekickWordLeftOffset(const std::string &value, std::size_t offset) noexcept {
	std::size_t pos = std::min(offset, value.size());

	if (pos == 0) return 0;
	--pos;
	while (pos > 0 && !snippetSidekickWordByte(value[pos]))
		--pos;
	while (pos > 0 && snippetSidekickWordByte(value[pos - 1]))
		--pos;
	return pos;
}

std::size_t snippetSidekickWordRightOffset(const std::string &value, std::size_t offset) noexcept {
	std::size_t pos = std::min(offset, value.size());

	while (pos < value.size() && snippetSidekickWordByte(value[pos]))
		++pos;
	while (pos < value.size() && !snippetSidekickWordByte(value[pos]))
		++pos;
	return pos;
}

TColorAttr snippetSidekickDialogColor(uchar index) noexcept {
	const TColorAttr frame = sidekickColor(kMrPaletteSnippetSidekickFrame, 0x3F);
	const TColorAttr text = sidekickColor(kMrPaletteSnippetSidekickText, 0x30);
	const TColorAttr selected = sidekickColor(kMrPaletteSnippetActivePlaceholder, 0xE0);

	switch (index) {
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
			return frame;
		case 6:
			return text;
		case 7:
			return selected;
		case 8:
			return text;
		default:
			return frame;
	}
}

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

int sidekickMaxLineLength(const std::vector<std::string> &lines) {
	int width = 1;
	for (const std::string &line : lines)
		width = std::max<int>(width, strwidth(line.c_str()));
	return width;
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
	int wantedWidth = std::clamp(sidekickMaxLineLength(lines) + 2, 24, std::max(24, desktop.b.x - desktop.a.x - 2));
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

TRect snippetSidekickBoundsFor(MREditWindow *parent, const std::string &text, std::size_t replaceStart, int anchorViewColumn, int anchorViewRow) {
	MRFileEditor *editor = parent != nullptr ? parent->getEditor() : nullptr;
	TRect desktop = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, 80, 25);
	const std::vector<std::string> lines = splitLines(text);
	const int desktopWidth = std::max(1, desktop.b.x - desktop.a.x);
	const int desktopHeight = std::max(1, desktop.b.y - desktop.a.y);
	const int maxWidth = std::max(32, desktopWidth - 2);
	const int maxHeight = std::max(6, desktopHeight - 2);
	int wantedWidth = std::clamp(sidekickMaxLineLength(lines) + 8, 48, maxWidth);
	int wantedHeight = std::clamp<int>(static_cast<int>(lines.size()) + 4, 8, maxHeight);
	int x = desktop.a.x + 2;
	int y = desktop.a.y + 2;

	if (editor != nullptr) {
		const TPoint editorGlobal = editor->makeGlobal(TPoint(0, 0));
		const TRect textViewport = editor->visibleTextViewportBounds();
		const std::size_t lineIndex = editor->lineIndexOfOffset(replaceStart);
		const std::size_t visibleLine = editor->visibleLineForDocumentLine(lineIndex);
		const std::size_t lineStart = editor->lineStartOffset(replaceStart);
		const int literalViewColumn = editor->charColumn(lineStart, replaceStart) - editor->delta.x + 1;
		const int literalViewRow = static_cast<int>(visibleLine) - editor->delta.y + 1;

		anchorViewColumn = literalViewColumn > 0 ? literalViewColumn : anchorViewColumn;
		anchorViewRow = literalViewRow > 0 ? literalViewRow : anchorViewRow;
		x = editorGlobal.x + textViewport.a.x + std::max(0, anchorViewColumn - 1) - 1;
		y = editorGlobal.y + textViewport.a.y + std::max(0, anchorViewRow - 1) - 2;
	}
	x = std::clamp(x, desktop.a.x, std::max(desktop.a.x, desktop.b.x - wantedWidth));
	if (y + wantedHeight > desktop.b.y) y = y - wantedHeight - 1;
	y = std::clamp(y, desktop.a.y, std::max(desktop.a.y, desktop.b.y - wantedHeight));
	return TRect(x, y, x + wantedWidth, y + wantedHeight);
}

class MRSnippetSidekickDialog : public TDialog {
  public:
	MRSnippetSidekickDialog(const TRect &bounds, int parentBufferId, std::size_t replaceStart, std::size_t replaceEnd, const std::string &text, const std::string &title, const std::vector<MRSidekickSpan> &placeholders)
	    : TWindowInit(initSnippetSidekickFrame), TDialog(bounds, title.c_str()), mEditor(nullptr) {
		flags |= wfMove | wfGrow | wfClose;
		growMode = gfGrowHiX | gfGrowHiY;
		mEditor = new MRSidekickEditor(TRect(1, 1, std::max<short>(2, size.x - 1), std::max<short>(2, size.y - 1)), parentBufferId, replaceStart, replaceEnd, text, title, placeholders, false, true, true);
		if (mEditor != nullptr) {
			mEditor->growMode = gfGrowHiX | gfGrowHiY;
			insert(mEditor);
			mEditor->select();
		}
	}

	[[nodiscard]] MRSidekickEditor *snippetSidekick() const noexcept {
		return mEditor;
	}

	void sizeLimits(TPoint &min, TPoint &max) override {
		TDialog::sizeLimits(min, max);
		min.x = std::max<short>(min.x, 32);
		min.y = std::max<short>(min.y, 6);
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
	const int anchorRowOffset = placement == MRReadOnlySidekickPlacement::UnderCode ? anchorViewRow - 2 : std::max(0, anchorViewRow - 1);
	const int anchorMinY = placement == MRReadOnlySidekickPlacement::UnderCode ? viewport.a.y - 1 : viewport.a.y;
	const int cursorY = std::clamp(editorGlobal.y + textViewport.a.y + anchorRowOffset, anchorMinY, std::max(anchorMinY, viewport.b.y - 1));
	const int aboveCodeSpace = std::max(0, cursorY - viewport.a.y);
	const int targetX = preferredViewColumn > 0 ? std::clamp(editorGlobal.x + textViewport.a.x + preferredViewColumn - 1, viewport.a.x, std::max(viewport.a.x, viewport.b.x - 1)) : cursorX;

	if (placement == MRReadOnlySidekickPlacement::UnderCode) {
		const int viewportWidth = std::max(1, viewport.b.x - viewport.a.x);
		const int readableWidth = std::min(maxSidekickWidth, viewportWidth);
		const int errorX = cursorX;
		const int errorY = cursorY;
		const int underCodeBottom = std::max(viewport.a.y, viewport.b.y - 1);
		const int belowSpace = std::max(0, underCodeBottom - (errorY + 1));
		const int aboveSpace = std::max(0, errorY - viewport.a.y);
		const int rightAvailable = std::max(1, viewport.b.x - errorX);
		const int leftAvailable = std::max(1, errorX - viewport.a.x + 1);
		marker = romBelow;

		std::vector<std::string> lines = splitLines(readOnlyTextWithMarker(text, marker, maxContentWidth));
		const int naturalWidth = std::clamp(sidekickMaxLineLength(lines), 1, readableWidth);
		const bool rightEdge = naturalWidth > rightAvailable;
		int wantedWidth = rightEdge ? std::min(naturalWidth, leftAvailable) : std::min(naturalWidth, rightAvailable);
		wantedWidth = std::clamp(wantedWidth, 1, readableWidth);
		int x = rightEdge ? errorX - wantedWidth + 1 : errorX;
		x = std::clamp(x, viewport.a.x, std::max(viewport.a.x, viewport.b.x - wantedWidth));
		marker = rightEdge ? romBelowRight : romBelow;
		lines = splitLines(readOnlyTextWithMarker(text, marker, std::max(1, wantedWidth - 2)));

		const int lineCount = static_cast<int>(lines.size());
		const bool lowerEdge = belowSpace < lineCount && aboveSpace > 0;
		const bool above = lowerEdge;

		marker = above ? (rightEdge ? romAboveRight : romAbove) : (rightEdge ? romBelowRight : romBelow);

		lines = splitLines(readOnlyTextWithMarker(text, marker, std::max(1, wantedWidth - 2)));
		const int verticalSpace = above ? aboveSpace : belowSpace;
		const int wantedHeight = std::max(1, std::min<int>(static_cast<int>(lines.size()), std::max(1, verticalSpace)));
		const int y = above ? errorY - wantedHeight : errorY + 1;
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
	int wantedWidth = std::clamp(sidekickMaxLineLength(lines), 1, readableWidth);
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
	if (mReadOnly) gDismissedReadOnlySidekickParentBufferId = mParentBufferId;
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

void MRSidekickEditor::insertChar(char ch) {
	const std::size_t offset = cursorOffset();
	std::string &line = mLines[static_cast<std::size_t>(mCursorRow)];

	if (replaceActivePlaceholder(std::string(1, ch))) return;
	line.insert(static_cast<std::size_t>(mCursorCol), 1, ch);
	++mCursorCol;
	adjustPlaceholdersAfterInsert(offset, 1);
	resizeSnippetSidekickForContent();
}

void MRSidekickEditor::insertTextAtCursor(const std::string &value) {
	const std::size_t offset = cursorOffset();
	std::string current;

	if (value.empty()) return;
	if (replaceActivePlaceholder(value)) return;
	current = text();
	if (offset > current.size()) return;
	current.insert(offset, value);
	adjustPlaceholdersAfterInsert(offset, value.size());
	setText(std::move(current));
	setCursorFromOffset(offset + value.size());
	resizeSnippetSidekickForContent();
}

void MRSidekickEditor::insertNewLine() {
	const std::size_t offset = cursorOffset();
	std::string &line = mLines[static_cast<std::size_t>(mCursorRow)];
	std::string tail = line.substr(static_cast<std::size_t>(mCursorCol));

	if (replaceActivePlaceholder("\n")) return;
	line.erase(static_cast<std::size_t>(mCursorCol));
	mLines.insert(mLines.begin() + mCursorRow + 1, tail);
	++mCursorRow;
	mCursorCol = 0;
	adjustPlaceholdersAfterInsert(offset, 1);
	resizeSnippetSidekickForContent();
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

void MRSidekickEditor::eraseWordBackward() {
	const std::string current = text();
	const std::size_t end = cursorOffset();
	const std::size_t start = snippetSidekickWordLeftOffset(current, end);
	std::string next = current;

	if (start >= end || end > next.size()) return;
	next.erase(start, end - start);
	adjustPlaceholdersAfterErase(start, end - start);
	setText(std::move(next));
	setCursorFromOffset(start);
}

void MRSidekickEditor::eraseWordForward() {
	const std::string current = text();
	const std::size_t start = cursorOffset();
	const std::size_t end = snippetSidekickWordRightOffset(current, start);
	std::string next = current;

	if (start >= end || end > next.size()) return;
	next.erase(start, end - start);
	adjustPlaceholdersAfterErase(start, end - start);
	setText(std::move(next));
	setCursorFromOffset(start);
}

void MRSidekickEditor::eraseToLineStart() {
	const std::size_t end = cursorOffset();
	const std::size_t start = end - static_cast<std::size_t>(std::max(0, mCursorCol));
	std::string current = text();

	if (start >= end || end > current.size()) return;
	current.erase(start, end - start);
	adjustPlaceholdersAfterErase(start, end - start);
	setText(std::move(current));
	setCursorFromOffset(start);
}

void MRSidekickEditor::eraseToLineEnd() {
	const std::size_t start = cursorOffset();
	const std::size_t length = mCursorRow >= 0 && mCursorRow < static_cast<int>(mLines.size()) ? mLines[static_cast<std::size_t>(mCursorRow)].size() - static_cast<std::size_t>(std::max(0, mCursorCol)) : 0;
	std::string current = text();

	if (length == 0 || start + length > current.size()) return;
	current.erase(start, length);
	adjustPlaceholdersAfterErase(start, length);
	setText(std::move(current));
	setCursorFromOffset(start);
}

void MRSidekickEditor::eraseLine() {
	std::size_t start = cursorOffset() - static_cast<std::size_t>(std::max(0, mCursorCol));
	std::size_t length = mCursorRow >= 0 && mCursorRow < static_cast<int>(mLines.size()) ? mLines[static_cast<std::size_t>(mCursorRow)].size() : 0;
	std::string current = text();

	if (mLines.size() > 1 && start + length < current.size()) ++length;
	else if (mLines.size() > 1 && start > 0) {
		--start;
		++length;
	}
	if (length == 0 || start + length > current.size()) return;
	current.erase(start, length);
	adjustPlaceholdersAfterErase(start, length);
	setText(std::move(current));
	setCursorFromOffset(std::min(start, text().size()));
}

bool MRSidekickEditor::replaceActivePlaceholder(const std::string &replacement) {
	std::vector<MRSidekickSpan> placeholders;
	std::string value;
	MRSidekickSpan active{};
	std::size_t offset = 0;
	std::size_t oldLength = 0;
	std::size_t newLength = 0;

	if (mPlaceholderIndex < 0 || static_cast<std::size_t>(mPlaceholderIndex) >= mPlaceholders.size()) return false;
	if (static_cast<std::size_t>(mPlaceholderIndex) < mPlaceholderTouched.size() && mPlaceholderTouched[static_cast<std::size_t>(mPlaceholderIndex)] != 0) return false;
	active = mPlaceholders[static_cast<std::size_t>(mPlaceholderIndex)];
	offset = cursorOffset();
	if (offset < active.start || offset > active.end) return false;
	value = text();
	if (active.start > value.size() || active.end > value.size() || active.end < active.start) return false;
	oldLength = active.end - active.start;
	newLength = replacement.size();
	value.replace(active.start, oldLength, replacement);
	placeholders = mPlaceholders;
	for (std::size_t index = 0; index < placeholders.size(); ++index) {
		MRSidekickSpan &span = placeholders[index];

		if (index == static_cast<std::size_t>(mPlaceholderIndex)) {
			span.end = span.start + newLength;
		} else if (span.start >= active.end) {
			if (newLength >= oldLength) {
				span.start += newLength - oldLength;
				span.end += newLength - oldLength;
			} else {
				span.start -= std::min(span.start, oldLength - newLength);
				span.end -= std::min(span.end, oldLength - newLength);
			}
		}
	}
	mPlaceholders = std::move(placeholders);
	if (static_cast<std::size_t>(mPlaceholderIndex) < mPlaceholderTouched.size()) mPlaceholderTouched[static_cast<std::size_t>(mPlaceholderIndex)] = 1;
	setText(std::move(value));
	mPlaceholderEndEdge = true;
	setCursorFromOffset(active.start + newLength);
	resizeSnippetSidekickForContent();
	return true;
}

bool MRSidekickEditor::handleRuntimeKeymap(TEvent &event) {
	MRKeymapToken token(MRKeymapBaseKey::Esc, 0);

	if (!mSnippetSidekick || mReadOnly || event.what != evKeyDown) return false;
	if (!mrKeymapTokenFromEvent(event.keyDown.keyCode, event.keyDown.controlKeyState, token)) return false;
	const MRKeymapResolver::Result result = runtimeKeymapResolver().resolve(MRKeymapContext::Edit, token);
	switch (result.kind) {
		case MRKeymapResolver::ResultKind::NoMatch:
			return false;
		case MRKeymapResolver::ResultKind::Pending:
		case MRKeymapResolver::ResultKind::Invalid:
		case MRKeymapResolver::ResultKind::Aborted:
			clearEvent(event);
			return true;
		case MRKeymapResolver::ResultKind::Matched:
			clearEvent(event);
			if (result.target.type != MRKeymapBindingType::Action) return true;
			return handleSnippetSidekickAction(result.target.target);
	}
	return false;
}

bool MRSidekickEditor::handleSnippetSidekickAction(const std::string &actionId) {
	SnippetSidekickAction action = SnippetSidekickAction::CursorLeft;

	if (!snippetSidekickActionFromId(actionId, action)) return false;
	switch (action) {
		case SnippetSidekickAction::CursorLeft:
			moveLeft();
			return true;
		case SnippetSidekickAction::CursorRight:
			moveRight();
			return true;
		case SnippetSidekickAction::CursorUp:
			moveUp();
			return true;
		case SnippetSidekickAction::CursorDown:
			moveDown();
			return true;
		case SnippetSidekickAction::CursorHome:
			moveLineStart();
			return true;
		case SnippetSidekickAction::CursorEnd:
			moveLineEnd();
			return true;
		case SnippetSidekickAction::CursorWordLeft:
			moveWordLeft();
			return true;
		case SnippetSidekickAction::CursorWordRight:
			moveWordRight();
			return true;
		case SnippetSidekickAction::DeleteBackwardChar:
			eraseBackward();
			return true;
		case SnippetSidekickAction::DeleteForwardChar:
			eraseForward();
			return true;
		case SnippetSidekickAction::DeleteBackwardWord:
			eraseWordBackward();
			return true;
		case SnippetSidekickAction::DeleteForwardWord:
			eraseWordForward();
			return true;
		case SnippetSidekickAction::DeleteBackwardToHome:
			eraseToLineStart();
			return true;
		case SnippetSidekickAction::DeleteToEndOfLine:
			eraseToLineEnd();
			return true;
		case SnippetSidekickAction::DeleteLine:
			eraseLine();
			return true;
		case SnippetSidekickAction::LoadBlockFromFile:
			return loadBlockFromFileIntoSnippetSidekick();
		case SnippetSidekickAction::PlaceholderNext:
			moveToPlaceholder(1);
			return true;
		case SnippetSidekickAction::PlaceholderPrevious:
			moveToPlaceholder(-1);
			return true;
	}
	return false;
}

bool MRSidekickEditor::loadBlockFromFileIntoSnippetSidekick() {
	char fileName[MAXPATH] = {0};
	std::string resolvedPath;
	std::string content;
	std::string errorText;

	if (!promptForPath(MRDialogHistoryScope::BlockLoad, "LOAD BLOCK", fileName, sizeof(fileName))) return true;
	if (!resolveReadableExistingPath(MRDialogHistoryScope::BlockLoad, fileName, resolvedPath)) return true;
	if (!readTextFile(resolvedPath, content, errorText)) {
		mrLogMessage(errorText.empty() ? "Snippet SideKick block load failed." : errorText);
		return true;
	}
	insertTextAtCursor(expandSidekickTabs(content));
	rememberLoadDialogPath(MRDialogHistoryScope::BlockLoad, resolvedPath.c_str());
	return true;
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

void MRSidekickEditor::moveLineStart() noexcept {
	mCursorCol = 0;
}

void MRSidekickEditor::moveLineEnd() noexcept {
	if (mCursorRow >= 0 && mCursorRow < static_cast<int>(mLines.size())) mCursorCol = static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow)].size());
}

void MRSidekickEditor::moveWordLeft() {
	setCursorFromOffset(snippetSidekickWordLeftOffset(text(), cursorOffset()));
}

void MRSidekickEditor::moveWordRight() {
	setCursorFromOffset(snippetSidekickWordRightOffset(text(), cursorOffset()));
}

void MRSidekickEditor::moveToPlaceholder(int direction) {
	if (mPlaceholders.empty()) return;
	if (mPlaceholderIndex < 0) {
		mPlaceholderIndex = direction >= 0 ? 0 : static_cast<int>(mPlaceholders.size()) - 1;
	} else if (direction >= 0) {
		mPlaceholderIndex = (mPlaceholderIndex + 1) % static_cast<int>(mPlaceholders.size());
	} else {
		mPlaceholderIndex = (mPlaceholderIndex + static_cast<int>(mPlaceholders.size()) - 1) % static_cast<int>(mPlaceholders.size());
	}
	mPlaceholderEndEdge = false;
	setCursorFromActivePlaceholder();
}

void MRSidekickEditor::setCursorFromActivePlaceholder() {
	const MRSidekickSpan &placeholder = mPlaceholders[static_cast<std::size_t>(mPlaceholderIndex)];

	setCursorFromOffset(mPlaceholderEndEdge ? placeholder.end : placeholder.start);
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

void MRSidekickEditor::resizeSnippetSidekickForContent() {
	if (!mSnippetSidekick || mReadOnly || owner == nullptr || TProgram::deskTop == nullptr) return;

	TRect desktop = TProgram::deskTop->getExtent();
	TRect bounds = owner->getBounds();
	const int desktopWidth = std::max(1, desktop.b.x - desktop.a.x);
	const int desktopHeight = std::max(1, desktop.b.y - desktop.a.y);
	const int maxWidth = std::max(32, desktopWidth - 2);
	const int maxHeight = std::max(6, desktopHeight - 2);
	const int wantedWidth = std::clamp(sidekickMaxLineLength(mLines) + 8, 32, maxWidth);
	const int wantedHeight = std::clamp<int>(static_cast<int>(mLines.size()) + 4, 6, maxHeight);
	const int currentWidth = std::max(1, bounds.b.x - bounds.a.x);
	const int currentHeight = std::max(1, bounds.b.y - bounds.a.y);
	const int newWidth = std::max(currentWidth, wantedWidth);
	const int newHeight = std::max(currentHeight, wantedHeight);

	if (newWidth == currentWidth && newHeight == currentHeight) return;
	bounds.b.x = bounds.a.x + newWidth;
	bounds.b.y = bounds.a.y + newHeight;
	if (bounds.b.x > desktop.b.x) bounds.move(desktop.b.x - bounds.b.x, 0);
	if (bounds.a.x < desktop.a.x) bounds.move(desktop.a.x - bounds.a.x, 0);
	if (bounds.b.y > desktop.b.y) bounds.move(0, desktop.b.y - bounds.b.y);
	if (bounds.a.y < desktop.a.y) bounds.move(0, desktop.a.y - bounds.a.y);
	owner->locate(bounds);
	TRect editorBounds(1, 1, std::max<short>(2, owner->size.x - 1), std::max<short>(2, owner->size.y - 1));
	locate(editorBounds);
	owner->drawView();
}

void MRSidekickEditor::clampCursor() noexcept {
	if (mLines.empty()) mLines.push_back(std::string());
	mCursorRow = std::clamp(mCursorRow, 0, static_cast<int>(mLines.size()) - 1);
	mCursorCol = std::clamp(mCursorCol, 0, static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow)].size()));
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
	const ushort result = TProgram::deskTop->execView(dialog);
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
	if (parent == nullptr || gDismissedReadOnlySidekickParentBufferId != parent->bufferId()) return false;
	gDismissedReadOnlySidekickParentBufferId = 0;
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

		if (!mrOpenReadOnlySidekickAt(window, "tail", "LSP hover", 3, 4, 3, MRReadOnlySidekickPlacement::UnderCode)) {
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
