#ifndef MRSIDEKICKINTERNAL_HPP
#define MRSIDEKICKINTERNAL_HPP

#include "MRSidekickEditor.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace mr::sidekick_internal {

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

enum ReadOnlyMarker {
	romBelow,
	romAbove,
	romLeft,
	romRight,
	romBelowRight,
	romAboveRight
};

[[nodiscard]] TColorAttr sidekickColor(unsigned char paletteSlot, TColorAttr fallback);
[[nodiscard]] std::string expandSidekickTabs(const std::string &value);
[[nodiscard]] bool snippetSidekickActionFromId(const std::string &actionId, SnippetSidekickAction &action) noexcept;
[[nodiscard]] std::size_t snippetSidekickWordLeftOffset(const std::string &value, std::size_t offset) noexcept;
[[nodiscard]] std::size_t snippetSidekickWordRightOffset(const std::string &value, std::size_t offset) noexcept;
[[nodiscard]] TColorAttr snippetSidekickDialogColor(uchar index) noexcept;
[[nodiscard]] std::vector<std::string> splitLines(const std::string &text);
[[nodiscard]] int sidekickMaxLineLength(const std::vector<std::string> &lines);
[[nodiscard]] const char *readOnlyMarkerGlyph(ReadOnlyMarker marker) noexcept;
[[nodiscard]] std::string readOnlyTextWithMarker(const std::string &text, ReadOnlyMarker marker, int contentWidth, int visibleLineCount = 0);
[[nodiscard]] TRect sidekickBoundsFor(MREditWindow *parent, const std::string &text);
[[nodiscard]] TRect snippetSidekickBoundsFor(MREditWindow *parent, const std::string &text, std::size_t replaceStart, int anchorViewColumn, int anchorViewRow);
[[nodiscard]] TRect readOnlySidekickBoundsFor(MREditWindow *parent, const std::string &text, ReadOnlyMarker &marker, int anchorViewColumn, int anchorViewRow, int preferredViewColumn, MRReadOnlySidekickPlacement placement);

} // namespace mr::sidekick_internal

#endif
