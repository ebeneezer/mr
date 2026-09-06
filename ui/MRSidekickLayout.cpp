#define Uses_TDeskTop
#define Uses_TProgram
#include <tvision/tv.h>

#include "MRSidekickInternal.hpp"
#include "MREditWindow.hpp"
#include "MRFileEditor/MRFileEditor.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>
#include <cctype>

namespace mr::sidekick_internal {
namespace {


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



} // namespace

TColorAttr sidekickColor(unsigned char paletteSlot, TColorAttr fallback) {
	TColorAttr configured;
	if (configuredColorSlotOverride(paletteSlot, configured)) return configured;
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

std::string readOnlyTextWithMarker(const std::string &text, ReadOnlyMarker marker, int contentWidth, int visibleLineCount) {
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
		const TPoint editorDesktop = TProgram::deskTop != nullptr ? TProgram::deskTop->makeLocal(editorGlobal) : editorGlobal;
		x = editorDesktop.x + std::max(0, editor->currentViewColumn() - 1);
		y = editorDesktop.y + std::max(0, editor->currentViewRow() - 1);
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
	const int maxWidth = std::max(1, desktopWidth - 2);
	const int maxHeight = std::max(1, desktopHeight - 2);
	const int minWidth = std::min(48, maxWidth);
	const int minHeight = std::min(10, maxHeight);
	int wantedWidth = std::clamp(sidekickMaxLineLength(lines) + 8, minWidth, maxWidth);
	int wantedHeight = std::clamp<int>(static_cast<int>(lines.size()) + 6, minHeight, maxHeight);
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

TRect readOnlySidekickBoundsFor(MREditWindow *parent, const std::string &text, ReadOnlyMarker &marker, int anchorViewColumn, int anchorViewRow, int preferredViewColumn, MRReadOnlySidekickPlacement placement) {
	MRFileEditor *editor = parent != nullptr ? parent->getEditor() : nullptr;
	TRect desktop = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, 80, 25);
	if (editor == nullptr) {
		marker = romBelow;
		return sidekickBoundsFor(parent, readOnlyTextWithMarker(text, marker, 40));
	}

	const TPoint editorGlobal = editor->makeGlobal(TPoint(0, 0));
	const TPoint editorDesktop = TProgram::deskTop != nullptr ? TProgram::deskTop->makeLocal(editorGlobal) : editorGlobal;
	const TRect textViewport = editor->visibleTextViewportBounds();
	TRect viewport(editorDesktop.x + textViewport.a.x, editorDesktop.y + textViewport.a.y,
	               editorDesktop.x + textViewport.b.x, editorDesktop.y + textViewport.b.y);
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
	const int cursorX = std::clamp(editorDesktop.x + textViewport.a.x + std::max(0, anchorViewColumn - 1), viewport.a.x, std::max(viewport.a.x, viewport.b.x - 1));
	const int anchorRowOffset = placement == MRReadOnlySidekickPlacement::UnderCode ? anchorViewRow - 2 : std::max(0, anchorViewRow - 1);
	const int anchorMinY = placement == MRReadOnlySidekickPlacement::UnderCode ? viewport.a.y - 1 : viewport.a.y;
	const int cursorY = std::clamp(editorDesktop.y + textViewport.a.y + anchorRowOffset, anchorMinY, std::max(anchorMinY, viewport.b.y - 1));
	const int aboveCodeSpace = std::max(0, cursorY - viewport.a.y);
	const int targetX = preferredViewColumn > 0 ? std::clamp(editorDesktop.x + textViewport.a.x + preferredViewColumn - 1, viewport.a.x, std::max(viewport.a.x, viewport.b.x - 1)) : cursorX;

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
	if ((belowSpace < lineCount && aboveCodeSpace >= lineCount) || (belowSpace <= 0 && aboveCodeSpace > 0)) above = true;
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

} // namespace mr::sidekick_internal
