#include "MRTextViewport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace {

struct GutterLayoutLane {
	int left = 0;
	int right = 0;

	explicit GutterLayoutLane(int width) noexcept : left(0), right(std::max(0, width)) {
	}

	bool placeLeading(int width, int &outX) noexcept {
		if (width <= 0 || width >= right - left) return false;
		outX = left;
		left += width;
		return true;
	}

	bool placeTrailing(int width, int &outX) noexcept {
		if (width <= 0 || width >= right - left) return false;
		right -= width;
		outX = right;
		return true;
	}
};

bool isGutterPositionLeading(const std::string &position) noexcept {
	return position == "LEADING";
}

bool isGutterPositionTrailing(const std::string &position) noexcept {
	return position == "TRAILING";
}

int normalizedMiniMapWidth(const MREditSetupSettings &settings) noexcept {
	if (!isGutterPositionLeading(settings.miniMapPosition) && !isGutterPositionTrailing(settings.miniMapPosition)) return 0;
	return std::max(2, std::min(settings.miniMapWidth, 20));
}

std::string normalizedGuttersOrder(const std::string &configured) {
	std::string normalized;
	std::array<bool, 3> seen = {false, false, false};
	for (char ch : configured) {
		switch (static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(ch)))) {
			case 'L':
				if (!seen[0]) {
					normalized.push_back('L');
					seen[0] = true;
				}
				break;
			case 'C':
				if (!seen[1]) {
					normalized.push_back('C');
					seen[1] = true;
				}
				break;
			case 'M':
				if (!seen[2]) {
					normalized.push_back('M');
					seen[2] = true;
				}
				break;
			default:
				break;
		}
	}
	if (normalized.empty()) normalized = "LCM";
	return normalized;
}

int decimalDigits(std::size_t value) noexcept {
	int digits = 1;
	while (value >= 10) {
		value /= 10;
		++digits;
	}
	return digits;
}

int lineNumberWidthFor(const MRTextViewportLayout::Inputs &inputs, bool showLineNumbers) noexcept {
	if (!showLineNumbers) return 0;
	const int textRows = std::max(1, inputs.visibleRows);
	const std::size_t visibleEnd = static_cast<std::size_t>(std::max(inputs.deltaY, 0) + textRows);
	const std::size_t lines = inputs.exactLineCountKnown ? std::max<std::size_t>(inputs.exactLineCount, visibleEnd) : std::max<std::size_t>(inputs.estimatedLineCount, visibleEnd);
	return std::min(decimalDigits(std::max<std::size_t>(lines, 1)), std::max(0, inputs.viewWidth - 1));
}

} // namespace

bool MRTextViewportLayout::Geometry::containsTextX(long long x) const noexcept {
	return x >= textLeft && x < textRight;
}

bool MRTextViewportLayout::Geometry::containsTextPoint(long long x, long long y, int viewHeight) const noexcept {
	return containsTextX(x) && y >= topInset && y < topInset + viewHeight;
}

int MRTextViewportLayout::Geometry::textColumnFromLocalX(int localX) const noexcept {
	const int clampedRight = std::max(textLeft, textRight - 1);
	const int clampedX = std::max(textLeft, std::min(localX, clampedRight));
	return std::max(0, clampedX - textLeft) + deltaX;
}

long long MRTextViewportLayout::Geometry::localXFromVisualColumn(long long visualColumn) const noexcept {
	return visualColumn - deltaX + textLeft;
}

std::string MRTextViewportLayout::normalizedLineNumbersPosition(const MREditSetupSettings &settings) {
	if (isGutterPositionLeading(settings.lineNumbersPosition) || isGutterPositionTrailing(settings.lineNumbersPosition)) return settings.lineNumbersPosition;
	return settings.showLineNumbers ? std::string("LEADING") : std::string("OFF");
}

std::string MRTextViewportLayout::normalizedCodeFoldingPosition(const MREditSetupSettings &settings) {
	if (isGutterPositionLeading(settings.codeFoldingPosition) || isGutterPositionTrailing(settings.codeFoldingPosition)) return settings.codeFoldingPosition;
	return settings.codeFolding ? std::string("LEADING") : std::string("OFF");
}

MRTextViewportLayout::Geometry MRTextViewportLayout::geometryFor(const MREditSetupSettings &settings, const Inputs &inputs) noexcept {
	Geometry viewport;
	GutterLayoutLane lane(std::max(0, inputs.viewWidth));
	viewport.topInset = settings.formatRuler ? 1 : 0;
	const std::string lineNumbersPosition = normalizedLineNumbersPosition(settings);
	const std::string codeFoldingPosition = normalizedCodeFoldingPosition(settings);
	const bool lineNumbersLeading = lineNumbersPosition == "LEADING";
	const bool lineNumbersTrailing = lineNumbersPosition == "TRAILING";
	const bool codeFoldingLeading = codeFoldingPosition == "LEADING";
	const bool codeFoldingTrailing = codeFoldingPosition == "TRAILING";
	const int lineNumberWidth = lineNumberWidthFor(inputs, lineNumbersLeading || lineNumbersTrailing);
	const int codeFoldingWidth = codeFoldingLeading || codeFoldingTrailing ? 1 : 0;
	const int miniMapTotalWidth = normalizedMiniMapWidth(settings);
	const bool leadingMiniMap = miniMapTotalWidth > 0 && isGutterPositionLeading(settings.miniMapPosition);
	const bool trailingMiniMap = miniMapTotalWidth > 0 && isGutterPositionTrailing(settings.miniMapPosition);
	const std::string guttersOrder = normalizedGuttersOrder(settings.gutters);
	const auto gutterWidthFor = [&](char marker) noexcept -> int {
		switch (marker) {
			case 'L':
				return lineNumberWidth;
			case 'C':
				return codeFoldingWidth;
			case 'M':
				return miniMapTotalWidth;
			default:
				return 0;
		}
	};
	const auto enabledOnLeading = [&](char marker) noexcept -> bool {
		switch (marker) {
			case 'L':
				return lineNumbersLeading;
			case 'C':
				return codeFoldingLeading;
			case 'M':
				return leadingMiniMap;
			default:
				return false;
		}
	};
	const auto enabledOnTrailing = [&](char marker) noexcept -> bool {
		switch (marker) {
			case 'L':
				return lineNumbersTrailing;
			case 'C':
				return codeFoldingTrailing;
			case 'M':
				return trailingMiniMap;
			default:
				return false;
		}
	};
	const auto assignGutterPlacement = [&](char marker, int x, bool leadingSide) {
		switch (marker) {
			case 'L':
				viewport.lineNumberX = x;
				viewport.lineNumberWidth = lineNumberWidth;
				break;
			case 'C':
				viewport.codeFoldingX = x;
				viewport.codeFoldingWidth = codeFoldingWidth;
				break;
			case 'M':
				viewport.miniMapTotalWidth = miniMapTotalWidth;
				viewport.miniMapBodyWidth = std::max(1, miniMapTotalWidth - 1);
				if (leadingSide) {
					viewport.miniMapBodyX = x;
					viewport.miniMapInfoX = x + viewport.miniMapBodyWidth;
				} else {
					viewport.miniMapInfoX = x;
					viewport.miniMapBodyX = x + 1;
				}
				break;
			default:
				break;
		}
	};
	std::vector<char> leadingSequence;
	std::vector<char> trailingSequence;
	leadingSequence.reserve(3);
	trailingSequence.reserve(3);
	for (char marker : guttersOrder) {
		if (enabledOnLeading(marker)) leadingSequence.push_back(marker);
		if (enabledOnTrailing(marker)) trailingSequence.push_back(marker);
	}
	for (char marker : leadingSequence) {
		int width = gutterWidthFor(marker);
		int x = -1;
		if (width > 0 && lane.placeLeading(width, x)) assignGutterPlacement(marker, x, true);
	}
	for (auto it = trailingSequence.rbegin(); it != trailingSequence.rend(); ++it) {
		int width = gutterWidthFor(*it);
		int x = -1;
		if (width > 0 && lane.placeTrailing(width, x)) assignGutterPlacement(*it, x, false);
	}
	if (!leadingSequence.empty() && leadingSequence.back() == 'M') {
		int separatorX = -1;
		if (lane.placeLeading(1, separatorX)) viewport.miniMapSeparatorX = separatorX;
	}
	if (!trailingSequence.empty() && trailingSequence.front() == 'M') {
		int separatorX = -1;
		if (lane.placeTrailing(1, separatorX)) viewport.miniMapSeparatorX = separatorX;
	}
	if (lane.right <= lane.left) {
		lane.left = std::max(0, std::min(lane.left, std::max(0, inputs.viewWidth - 1)));
		lane.right = std::max(lane.left + 1, inputs.viewWidth);
	}
	viewport.textLeft = lane.left;
	viewport.textRight = std::max(viewport.textLeft + 1, std::min(lane.right, inputs.viewWidth));
	viewport.width = std::max(1, viewport.textRight - viewport.textLeft);
	viewport.gutterWidth = viewport.textLeft;
	viewport.rightInset = std::max(0, inputs.viewWidth - viewport.textRight);
	viewport.deltaX = inputs.deltaX;
	viewport.deltaY = inputs.deltaY;
	return viewport;
}

bool MRTextViewportLayout::shouldShowCursor(const Geometry &geometry, long long x, long long y, int viewHeight, bool viewActive, bool viewSelected) noexcept {
	return viewActive && viewSelected && geometry.containsTextPoint(x, y, viewHeight);
}
