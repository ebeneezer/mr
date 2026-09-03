#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"

#include <algorithm>

namespace {
std::size_t renderedBlockOverlayEndForViewport(const MRTextBufferModel &model, std::size_t overlayStart, std::size_t overlayEnd, int overlayMode) noexcept {
	if (overlayStart > overlayEnd) std::swap(overlayStart, overlayEnd);
	if (overlayMode == 1 && overlayEnd > overlayStart && model.lineStart(overlayEnd) == overlayEnd && model.lineEnd(overlayEnd) == overlayEnd) --overlayEnd;
	return overlayEnd;
}

unsigned char fileCompareTextPaletteSlot(unsigned char lineKind) noexcept {
	switch (lineKind) {
		case mrfclkEqual:
			return kMrPaletteFileCompareTextEqual;
		case mrfclkMissing:
			return kMrPaletteFileCompareTextMissing;
		case mrfclkInsert:
			return kMrPaletteFileCompareTextInsert;
		case mrfclkOffset:
			return kMrPaletteFileCompareTextOffset;
		default:
			return 0;
	}
}
} // namespace

TColorAttr MRFileEditor::tokenColor(MRSyntaxToken token, bool selected, TAttrPair pair) noexcept {
	TColorAttr normal = static_cast<TColorAttr>(pair);
	TColorAttr selectedAttr = static_cast<TColorAttr>(pair >> 8);
	const TColorDesired background = getBack(selected ? selectedAttr : normal);
	auto configuredCodeColor = [background](unsigned char paletteSlot, unsigned char fallbackForeground) noexcept -> TColorAttr {
		TColorAttr configured;

		if (configuredColorSlotOverride(paletteSlot, configured)) return configured;
		return TColorAttr(TColorDesired(fallbackForeground), background);
	};

	if (selected) return selectedAttr;
	switch (token) {
		case MRSyntaxToken::Keyword:
			return configuredCodeColor(kMrPaletteCodeKeywords, 0x0E);
		case MRSyntaxToken::Directive:
			return configuredCodeColor(kMrPaletteCodeDirectives, 0x0E);
		case MRSyntaxToken::Section:
		case MRSyntaxToken::Heading:
			return configuredCodeColor(kMrPaletteCodeKeywords, 0x0E);
		case MRSyntaxToken::Type:
			return configuredCodeColor(kMrPaletteCodeTypes, 0x0B);
		case MRSyntaxToken::Key:
			return configuredCodeColor(kMrPaletteCodeConstants, 0x0B);
		case MRSyntaxToken::Delimiter:
			return configuredCodeColor(kMrPaletteCodeDelimiters, 0x09);
		case MRSyntaxToken::Number:
			return configuredCodeColor(kMrPaletteCodeNumbers, 0x0A);
		case MRSyntaxToken::String:
			return configuredCodeColor(kMrPaletteCodeStrings, 0x0D);
		case MRSyntaxToken::Comment:
			return configuredCodeColor(kMrPaletteCodeComments, 0x03);
		default:
			return normal;
	}
}

void MRFileEditor::formatSyntaxLine(TDrawBuffer &b, std::size_t lineStart, std::size_t lineIndex, const MRSyntaxLineResult &syntaxLine, int hScroll, int width, int drawX, bool isDocumentLine, bool drawEofMarker, bool drawEofMarkerAsEmoji) {
	TAttrPair basePair = getColor(0x0201);
	TAttrPair changedPair = getColor(0x0505);
	TAttrPair selectionPair = getColor(0x0201);
	MRTextBufferModel::Range selection;
	std::size_t documentLength = mBufferModel.length();
	std::size_t lineEnd = lineStart;
	std::size_t cursorPos = 0;
	bool currentLine = false;
	bool currentLineInBlock = false;
	bool instructionLine = false;
	bool overlayActive = false;
	int overlayMode = 0;
	std::size_t overlayStart = 0;
	std::size_t overlayEnd = 0;
	std::size_t overlayLine1 = 0;
	std::size_t overlayLine2 = 0;
	int overlayCol1 = 0;
	int overlayCol2Exclusive = 0;
	std::size_t bytePos = 0;
	int visual = 0;
	int x = 0;
	const MREditSetupSettings settings = effectiveEditSetupSettings();
	const bool displayTabs = configuredDisplayTabs();
	unsigned char diffLineKind = mrfclkNone;
	bool diffTextActive = false;
	TColorAttr diffTextColor = 0;

	hScroll = std::max(hScroll, 0);
	width = std::max(width, 0);
	drawX = std::max(drawX, 0);
	if (!isDocumentLine) {
		TColorAttr color = editorTextFillColor();
		const std::size_t virtualLineIndex = lineStart;
		b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
		if (displayedCursorLineIndex() == virtualLineIndex) b.moveChar(static_cast<ushort>(drawX), ' ', static_cast<TColorAttr>(getColor(0x0303)), static_cast<ushort>(width));
		if (mBlockOverlayActive && mBlockOverlayMode == 2 && mBlockOverlayLineRangeValid) {
			const std::size_t overlayLine1 = std::min(mBlockOverlayLine1, mBlockOverlayLine2);
			const std::size_t overlayLine2 = std::max(mBlockOverlayLine1, mBlockOverlayLine2);
			const int overlayCol1 = std::min(mBlockOverlayColumnAnchor, mBlockOverlayColumnEnd);
			const int overlayCol2Exclusive = std::max(mBlockOverlayColumnAnchor, mBlockOverlayColumnEnd);

			if (overlayLine1 <= virtualLineIndex && virtualLineIndex <= overlayLine2 && overlayCol2Exclusive > overlayCol1) {
				const int selectedStartX = std::max(0, overlayCol1 - hScroll);
				const int selectedEndX = std::min(width, overlayCol2Exclusive - hScroll);

				if (selectedEndX > selectedStartX)
					b.moveChar(static_cast<ushort>(drawX + selectedStartX), ' ', static_cast<TColorAttr>(getColor(0x0201) >> 8), static_cast<ushort>(selectedEndX - selectedStartX));
			}
		}
		if (drawEofMarker) drawEofMarkerGlyph(b, hScroll, width, drawX, basePair, drawEofMarkerAsEmoji);
		return;
	}
	std::string lineText = mBufferModel.lineText(lineStart);
	TStringView line(lineText.data(), lineText.size());
	selection = mBufferModel.selection().range();
	lineEnd = mBufferModel.nextLine(lineStart);
	cursorPos = mBufferModel.cursor();
	overlayActive = mBlockOverlayActive;
	if (overlayActive) {
		overlayMode = mBlockOverlayMode;
		overlayStart = mBlockOverlayAnchor;
		overlayEnd = mBlockOverlayTrackCursor ? mBufferModel.cursor() : mBlockOverlayEnd;
		if (overlayStart > overlayEnd) std::swap(overlayStart, overlayEnd);
		overlayEnd = renderedBlockOverlayEndForViewport(mBufferModel, overlayStart, overlayEnd, overlayMode);
		if (overlayMode == 2 && mBlockOverlayLineRangeValid) {
			overlayLine1 = mBlockOverlayLine1;
			overlayLine2 = mBlockOverlayLine2;
		} else {
			overlayLine1 = mBufferModel.lineIndex(overlayStart);
			overlayLine2 = mBufferModel.lineIndex(overlayEnd);
		}
		if (overlayLine1 > overlayLine2) std::swap(overlayLine1, overlayLine2);
	}
	overlayCol1 = std::min(mBlockOverlayColumnAnchor, mBlockOverlayColumnEnd);
	overlayCol2Exclusive = std::max(mBlockOverlayColumnAnchor, mBlockOverlayColumnEnd);
	const bool emptyEofDocumentLine = lineStart == documentLength && lineEnd == documentLength;
	currentLine = !emptyEofDocumentLine && lineStart <= cursorPos && cursorPos < lineEnd;
	if (overlayActive) {
		if (overlayMode == 3) currentLineInBlock = false;
		else
			currentLineInBlock = currentLine && overlayLine1 <= lineIndex && lineIndex <= overlayLine2;
	} else
		currentLineInBlock = false;
	diffLineKind = fileCompareLineKindAt(lineIndex);
	if (currentLineInBlock) basePair = getColor(0x0204);
	else if (currentLine && diffLineKind == mrfclkNone)
		basePair = getColor(0x0303);
	instructionLine = mDebuggerInstructionLineValid && mDebuggerInstructionLine == lineIndex;
	if (instructionLine) {
		TColorAttr executionLineAttr;

		if (configuredColorSlotOverride(kMrPaletteDebuggerExecutionLine, executionLineAttr)) basePair = TAttrPair(executionLineAttr);
		else
			basePair = TAttrPair(0x1E);
	}
	if (diffLineKind != mrfclkNone) {
		const unsigned char slot = fileCompareTextPaletteSlot(diffLineKind);
			TColorAttr configured;

			if (slot != 0 && configuredColorSlotOverride(slot, configured)) {
				diffTextColor = configured;
				diffTextActive = true;
			}
		}

	std::size_t runIndex = 0;
	while (bytePos < line.size() && x < width) {
		std::size_t next = bytePos;
		std::size_t charWidth = 0;
		if (!nextDisplayChar(line, next, charWidth, visual, settings)) break;

		int nextVisual = visual + static_cast<int>(charWidth);
		if (nextVisual > hScroll) {
			std::size_t documentPos = lineStart + bytePos;
			MRSyntaxToken token = MRSyntaxToken::Text;
			bool selected = false;
			TAttrPair tokenPair;
			TColorAttr color;
			int visibleWidth = 0;

			while (runIndex < syntaxLine.tokenRuns.size()) {
				const MRSyntaxTokenRun &run = syntaxLine.tokenRuns[runIndex];
				const std::size_t runStart = static_cast<std::size_t>(run.column);
				const std::size_t runEnd = runStart + static_cast<std::size_t>(run.length);
				if (bytePos < runStart) break;
				if (bytePos < runEnd) {
					token = run.token;
					break;
				}
				++runIndex;
			}

			if (overlayActive) {
				if (overlayMode == 3) selected = overlayStart <= documentPos && documentPos < overlayEnd;
				else if (overlayMode == 1)
					selected = overlayLine1 <= lineIndex && lineIndex <= overlayLine2;
				else if (overlayMode == 2)
					selected = overlayLine1 <= lineIndex && lineIndex <= overlayLine2 && visual < overlayCol2Exclusive && nextVisual > overlayCol1;
			} else {
				selected = selection.start <= documentPos && documentPos < selection.end;
			}
			bool changedChar = !currentLine && !currentLineInBlock && isDirtyOffset(documentPos);
			bool findMarkedChar = !selected && findMarkerContainsOffset(documentPos);
			bool debuggerBreakpointChar = !selected && debuggerBreakpointContainsOffset(documentPos);
			bool debuggerBreakpointInactiveChar = !selected && debuggerBreakpointInactiveContainsOffset(documentPos);
			bool debuggerBreakpointUnboundChar = !selected && debuggerBreakpointUnboundContainsOffset(documentPos);
			bool debuggerWatchpointActiveChar = !selected && debuggerWatchpointActiveContainsOffset(documentPos);
			bool debuggerWatchpointInactiveChar = !selected && debuggerWatchpointInactiveContainsOffset(documentPos);
			bool debuggerWatchpointErrorChar = !selected && debuggerWatchpointErrorContainsOffset(documentPos);
			bool debuggerVariableChangedChar = !selected && debuggerVariableChangedContainsOffset(documentPos);
			TAttrPair effectivePair = instructionLine ? basePair : (changedChar ? changedPair : basePair);
			TColorAttr unselectedColor = instructionLine ? static_cast<TColorAttr>(basePair) : tokenColor(token, false, effectivePair);
			TColorAttr selectedColor = tokenColor(token, true, selectionPair);
			tokenPair = selected ? selectionPair : effectivePair;
			color = selected ? selectedColor : unselectedColor;
			if (findMarkedChar) {
				TColorAttr highlightedTextAttr;
				if (configuredColorSlotOverride(14, highlightedTextAttr)) color = highlightedTextAttr;
				else
					color = static_cast<TColorAttr>(getColor(3));
			}
			if (debuggerBreakpointChar) {
				TColorAttr breakpointAttr;
				if (configuredColorSlotOverride(kMrPaletteDebuggerBreakpointActive, breakpointAttr)) color = breakpointAttr;
				else
					color = static_cast<TColorAttr>(TAttrPair(0x4E));
			}
			if (debuggerBreakpointInactiveChar) {
				TColorAttr breakpointAttr;
				if (configuredColorSlotOverride(kMrPaletteDebuggerBreakpointInactive, breakpointAttr)) color = breakpointAttr;
				else
					color = static_cast<TColorAttr>(TAttrPair(0x18));
			}
			if (debuggerBreakpointUnboundChar) {
				TColorAttr breakpointAttr;
				if (configuredColorSlotOverride(kMrPaletteDebuggerBreakpointUnbound, breakpointAttr)) color = breakpointAttr;
				else
					color = static_cast<TColorAttr>(TAttrPair(0x4C));
			}
			if (debuggerWatchpointActiveChar) {
				TColorAttr watchpointAttr;
				if (configuredColorSlotOverride(kMrPaletteDebuggerWatchpointActive, watchpointAttr)) color = watchpointAttr;
				else
					color = static_cast<TColorAttr>(TAttrPair(0x3E));
			}
			if (debuggerWatchpointInactiveChar) {
				TColorAttr watchpointAttr;
				if (configuredColorSlotOverride(kMrPaletteDebuggerWatchpointInactive, watchpointAttr)) color = watchpointAttr;
				else
					color = static_cast<TColorAttr>(TAttrPair(0x38));
			}
			if (debuggerWatchpointErrorChar) {
				TColorAttr watchpointAttr;
				if (configuredColorSlotOverride(kMrPaletteDebuggerWatchpointError, watchpointAttr)) color = watchpointAttr;
				else
					color = static_cast<TColorAttr>(TAttrPair(0x4F));
			}
			if (debuggerVariableChangedChar) {
				TColorAttr valueChangedAttr;
				if (configuredColorSlotOverride(kMrPaletteDebuggerValueChanged, valueChangedAttr)) color = valueChangedAttr;
				else
					color = static_cast<TColorAttr>(TAttrPair(0x2E));
			}
			if (diffTextActive && !selected && !instructionLine) color = diffTextColor;
			if (!selected) unselectedColor = color;
			visibleWidth = nextVisual - std::max(visual, hScroll);

			if (line[bytePos] == '\t' && overlayActive && overlayMode == 2 && overlayLine1 <= lineIndex && lineIndex <= overlayLine2 && charWidth > 1 && visibleWidth > 0) {
				const int visibleStart = std::max(visual, hScroll);
				const int visibleEnd = nextVisual;
				int drawColumn = drawX + x;
				int cell = visibleStart;
				while (cell < visibleEnd) {
					const bool cellSelected = overlayCol1 <= cell && cell < overlayCol2Exclusive;
					const TColorAttr cellColor = cellSelected ? selectedColor : unselectedColor;
					const int segmentStart = cell;
					int segmentEnd = std::min(visibleEnd, cellSelected ? overlayCol2Exclusive : overlayCol1);
					if (segmentEnd <= segmentStart) segmentEnd = visibleEnd;
					if (displayTabs && segmentStart == visual)
						b.moveStr(static_cast<ushort>(drawColumn), "\xE2\x96\xB6", cellColor, 1);
					else
						b.moveChar(static_cast<ushort>(drawColumn), ' ', cellColor, 1);
					if (segmentEnd - segmentStart > 1) b.moveChar(static_cast<ushort>(drawColumn + 1), ' ', cellColor, static_cast<ushort>(segmentEnd - segmentStart - 1));
					drawColumn += segmentEnd - segmentStart;
					cell = segmentEnd;
				}
			} else if (line[bytePos] == '\t' && displayTabs && visual >= hScroll && visibleWidth > 0) {
				b.moveStr(static_cast<ushort>(drawX + x), "\xE2\x96\xB6", color, 1);
				if (visibleWidth > 1) b.moveChar(static_cast<ushort>(drawX + x + 1), ' ', color, static_cast<ushort>(visibleWidth - 1));
			} else if (line[bytePos] == '\t' || visual < hScroll)
				b.moveChar(static_cast<ushort>(drawX + x), ' ', color, static_cast<ushort>(visibleWidth));
			else
				b.moveStr(static_cast<ushort>(drawX + x), line.substr(bytePos, next - bytePos), color, static_cast<ushort>(visibleWidth));
			x += visibleWidth;
		}
		visual = nextVisual;
		bytePos = next;
	}

	if (x < width) {
		TColorAttr color = instructionLine ? static_cast<TColorAttr>(basePair) : tokenColor(MRSyntaxToken::Text, false, basePair);
		TColorAttr selectedColor = tokenColor(MRSyntaxToken::Text, true, selectionPair);
		int selectedStartX = width;
		int selectedEndX = width;

		if (overlayActive) {
			if (overlayMode == 1 && overlayLine1 <= lineIndex && lineIndex <= overlayLine2) {
				selectedStartX = x;
				selectedEndX = width;
			} else if (overlayMode == 2 && overlayLine1 <= lineIndex && lineIndex <= overlayLine2) {
				selectedStartX = overlayCol1 - hScroll;
				selectedEndX = overlayCol2Exclusive - hScroll;
			} else if (overlayMode == 3) {
				const std::size_t streamLine1 = mBufferModel.lineIndex(overlayStart);
				const std::size_t streamLine2 = mBufferModel.lineIndex(overlayEnd);

				if (streamLine1 <= lineIndex && lineIndex <= streamLine2) {
					int selectedStartVisual = hScroll;
					int selectedEndVisual = hScroll + width;

					if (streamLine1 == streamLine2) {
						selectedStartVisual = charColumn(mBufferModel.lineStart(overlayStart), overlayStart);
						selectedEndVisual = charColumn(mBufferModel.lineStart(overlayEnd), overlayEnd);
					} else if (lineIndex == streamLine1) {
						selectedStartVisual = charColumn(mBufferModel.lineStart(overlayStart), overlayStart);
					} else if (lineIndex == streamLine2) {
						selectedEndVisual = charColumn(mBufferModel.lineStart(overlayEnd), overlayEnd);
					}
					selectedStartX = selectedStartVisual - hScroll;
					selectedEndX = selectedEndVisual - hScroll;
				}
			}
		}
		selectedStartX = std::max(x, std::min(width, selectedStartX));
		selectedEndX = std::max(x, std::min(width, selectedEndX));
		if (diffTextActive && !instructionLine) color = diffTextColor;
		if (selectedStartX < selectedEndX) {
			if (x < selectedStartX) b.moveChar(static_cast<ushort>(drawX + x), ' ', color, static_cast<ushort>(selectedStartX - x));
			b.moveChar(static_cast<ushort>(drawX + selectedStartX), ' ', selectedColor, static_cast<ushort>(selectedEndX - selectedStartX));
			if (selectedEndX < width) b.moveChar(static_cast<ushort>(drawX + selectedEndX), ' ', color, static_cast<ushort>(width - selectedEndX));
		} else
			b.moveChar(static_cast<ushort>(drawX + x), ' ', color, static_cast<ushort>(width - x));
	}
	if (drawEofMarker) drawEofMarkerGlyph(b, hScroll, width, drawX, basePair, drawEofMarkerAsEmoji);
}

void MRFileEditor::drawEofMarkerGlyph(TDrawBuffer &b, int hScroll, int width, int drawX, TAttrPair basePair, bool drawEmoji) {
	static const char *const kEofMarkerText = "EOF";
	static const char *const kEofMarkerEmoji = "\xF0\x9F\x94\x9A";
	const char *marker = drawEmoji ? kEofMarkerEmoji : kEofMarkerText;
	int markerWidth = 0;
	TColorAttr markerColor = tokenColor(MRSyntaxToken::Text, false, basePair);
	TColorAttr configuredMarkerColor;

	if (width <= 0 || hScroll != 0) return;
	if (!drawEmoji && mCustomWindowEofMarkerColorOverrideValid) markerColor = mCustomWindowEofMarkerColorOverride;
	else if (!drawEmoji && configuredColorSlotOverride(kMrPaletteEofMarker, configuredMarkerColor))
		markerColor = configuredMarkerColor;
	markerWidth = std::max(1, strwidth(marker));
	markerWidth = std::min(markerWidth, width);
	b.moveStr(static_cast<ushort>(drawX), marker, markerColor, static_cast<ushort>(markerWidth));
}
