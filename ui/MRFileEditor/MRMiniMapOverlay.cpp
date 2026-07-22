#include "MRMiniMap.hpp"

#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

namespace {

int overlayTabDisplayWidth(const MREditSetupSettings &settings, int visualColumn) noexcept {
	const int currentColumn = std::max(1, visualColumn + 1);
	const int targetColumn = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);
	return std::max(1, targetColumn - currentColumn);
}

bool nextOverlayDisplayChar(TStringView text, std::size_t &index, std::size_t &width, int visualColumn, const MREditSetupSettings &settings) noexcept {
	if (index >= text.size()) return false;
	if (text[index] == '\t') {
		++index;
		width = static_cast<std::size_t>(overlayTabDisplayWidth(settings, visualColumn));
		return true;
	}
	return TText::next(text, index, width);
}

bool overlayRatioCellInRange(int from, int to, int viewportWidth, int cellIndex, int cellCount) noexcept {
	if (from < 0 || to <= 0 || from >= to || viewportWidth <= 0 || cellCount <= 0) return false;
	const long long cellLeft = static_cast<long long>(cellIndex) * viewportWidth;
	const long long cellRight = static_cast<long long>(cellIndex + 1) * viewportWidth;
	const long long contentLeft = static_cast<long long>(from) * cellCount;
	const long long contentRight = static_cast<long long>(to) * cellCount;
	return cellRight > contentLeft && cellLeft < contentRight;
}

void normalizeOverlayLineMasks(MRMiniMapRenderer::OverlayState::LineMasks &masks) {
	std::sort(masks.begin(), masks.end(), [](const MRMiniMapRenderer::OverlayState::LineMask &left, const MRMiniMapRenderer::OverlayState::LineMask &right) {
		return left.lineIndex < right.lineIndex;
	});
	std::size_t write = 0;
	for (const MRMiniMapRenderer::OverlayState::LineMask &mask : masks) {
		if (write != 0 && masks[write - 1].lineIndex == mask.lineIndex)
			masks[write - 1].dotColumnBits |= mask.dotColumnBits;
		else
			masks[write++] = mask;
	}
	masks.resize(write);
}

} // namespace

std::shared_ptr<const MRMiniMapRenderer::OverlayState> MRMiniMapRenderer::computeOverlayState(
    const mr::editor::ReadSnapshot &snapshot, const mr::editor::Range &selection, const OverlaySources &sources, std::size_t totalLines,
    int viewportWidth, int miniMapBodyWidth, bool useBraille, const MREditSetupSettings &settings, unsigned int componentMask) {
	std::shared_ptr<OverlayState> overlay = std::make_shared<OverlayState>();
	const int dotColumns = useBraille ? std::max(1, miniMapBodyWidth * 2) : std::max(1, miniMapBodyWidth);
	const int normalizedViewportWidth = std::max(1, viewportWidth);
	const std::size_t length = snapshot.length();
	if (length == 0 || totalLines == 0 || miniMapBodyWidth <= 0 || (componentMask & ~overlayAll) != 0) return overlay;

	auto rangeMaskForLineSlice = [&](std::string_view lineText, std::size_t sliceStart, std::size_t sliceEnd) {
		if (sliceEnd <= sliceStart || sliceStart >= lineText.size() || dotColumns <= 0) return std::uint64_t(0);
		sliceEnd = std::min(sliceEnd, lineText.size());
		std::size_t index = 0;
		int visualColumn = 0;
		std::uint64_t mask = 0;
		while (index < lineText.size()) {
			const std::size_t current = index;
			std::size_t next = index;
			std::size_t width = 0;
			if (!nextOverlayDisplayChar(TStringView(lineText.data(), lineText.size()), next, width, visualColumn, settings)) break;
			if (next > sliceStart && current < sliceEnd) {
				const int from = visualColumn;
				const int to = visualColumn + static_cast<int>(width);
				for (int dotColumn = 0; dotColumn < std::min(dotColumns, 64); ++dotColumn)
					if (overlayRatioCellInRange(from, to, normalizedViewportWidth, dotColumn, dotColumns)) mask |= 1ULL << dotColumn;
			}
			visualColumn += static_cast<int>(width);
			index = next;
		}
		return mask;
	};

	auto appendRangeMasks = [&](OverlayState::LineMasks &target, mr::editor::Range range, bool markTouchedBlankLines) {
		range = range.clamped(length);
		if (range.end <= range.start) return;
		for (std::size_t lineStart = snapshot.lineStartByIndex(snapshot.lineIndex(range.start)); lineStart < range.end;) {
			const std::size_t lineIndex = snapshot.lineIndex(lineStart);
			if (lineIndex >= totalLines) break;
			const std::size_t nextLineStart = snapshot.nextLine(lineStart);
			const std::size_t lineEnd = std::min(nextLineStart, length);
			const std::size_t sliceStart = range.start > lineStart ? range.start - lineStart : 0;
			const std::size_t sliceEnd = range.end < lineEnd ? range.end - lineStart : lineEnd - lineStart;
			std::uint64_t mask = rangeMaskForLineSlice(snapshot.lineText(lineStart), sliceStart, sliceEnd);
			if (mask == 0 && markTouchedBlankLines) mask = 1ULL;
			if (mask != 0) target.push_back({lineIndex, mask});
			if (nextLineStart <= lineStart) break;
			lineStart = nextLineStart;
		}
	};

	if ((componentMask & overlayFind) != 0) {
		std::shared_ptr<OverlayState::LineMasks> masks = std::make_shared<OverlayState::LineMasks>();
		if (selection.end > selection.start) appendRangeMasks(*masks, selection, false);
		for (const mr::editor::Range &range : *sources.findRanges)
			appendRangeMasks(*masks, range, false);
		normalizeOverlayLineMasks(*masks);
		overlay->findLineMasks = std::move(masks);
	}
	if ((componentMask & overlayDirty) != 0) {
		std::shared_ptr<OverlayState::LineMasks> masks = std::make_shared<OverlayState::LineMasks>();
		for (const mr::editor::Range &range : *sources.dirtyRanges)
			appendRangeMasks(*masks, range, true);
		normalizeOverlayLineMasks(*masks);
		overlay->dirtyLineMasks = std::move(masks);
	}
	if ((componentMask & overlayError) != 0) {
		std::shared_ptr<OverlayState::LineMasks> masks = std::make_shared<OverlayState::LineMasks>();
		for (const mr::editor::Range &range : *sources.errorRanges)
			appendRangeMasks(*masks, range, true);
		normalizeOverlayLineMasks(*masks);
		overlay->errorLineMasks = std::move(masks);
	}
	if ((componentMask & overlayWarning) != 0) {
		std::shared_ptr<OverlayState::LineMasks> masks = std::make_shared<OverlayState::LineMasks>();
		for (const mr::editor::Range &range : *sources.warningRanges)
			appendRangeMasks(*masks, range, true);
		normalizeOverlayLineMasks(*masks);
		overlay->warningLineMasks = std::move(masks);
	}
	if ((componentMask & overlayDiff) != 0) {
		std::shared_ptr<OverlayState::LineMasks> equalMasks = std::make_shared<OverlayState::LineMasks>();
		std::shared_ptr<OverlayState::LineMasks> missingMasks = std::make_shared<OverlayState::LineMasks>();
		std::shared_ptr<OverlayState::LineMasks> insertMasks = std::make_shared<OverlayState::LineMasks>();
		std::shared_ptr<OverlayState::LineMasks> offsetMasks = std::make_shared<OverlayState::LineMasks>();
		auto appendDiffMask = [&](unsigned char lineKind, std::size_t lineIndex, std::uint64_t mask) {
			if (mask == 0) return;
			switch (lineKind) {
				case mrfclkEqual:
					equalMasks->push_back({lineIndex, mask});
					break;
				case mrfclkMissing:
					missingMasks->push_back({lineIndex, mask});
					break;
				case mrfclkInsert:
					insertMasks->push_back({lineIndex, mask});
					break;
				case mrfclkOffset:
					offsetMasks->push_back({lineIndex, mask});
					break;
				default:
					break;
			}
		};
		const std::vector<unsigned char> &lineKinds = *sources.fileCompareLineKinds;
		const std::vector<MRFileCompareMiniMapSlice> &slices = *sources.fileCompareMiniMapSlices;
		if (!lineKinds.empty()) {
			const std::uint64_t fullLineMask = dotColumns >= 64 ? ~0ULL : ((1ULL << static_cast<unsigned int>(dotColumns)) - 1ULL);
			const std::size_t count = std::min(totalLines, lineKinds.size());
			std::vector<bool> slicedChangedLines(count, false);
			for (const MRFileCompareMiniMapSlice &slice : slices)
				if (slice.lineIndex < count && slice.lineKind != mrfclkEqual) slicedChangedLines[slice.lineIndex] = true;
			for (std::size_t lineIndex = 0; lineIndex < count; ++lineIndex) {
				if (lineKinds[lineIndex] != mrfclkEqual && slicedChangedLines[lineIndex]) continue;
				appendDiffMask(lineKinds[lineIndex], lineIndex, fullLineMask);
			}
			for (const MRFileCompareMiniMapSlice &slice : slices) {
				if (slice.lineIndex >= totalLines) continue;
				std::uint64_t mask = fullLineMask;
				if (!slice.fullLine) {
					const std::size_t lineStart = snapshot.lineStartByIndex(slice.lineIndex);
					mask = lineStart < length ? rangeMaskForLineSlice(snapshot.lineText(lineStart), slice.sliceStart, slice.sliceEnd) : 0;
					if (mask == 0) mask = 1ULL;
				}
				appendDiffMask(slice.lineKind, slice.lineIndex, mask);
			}
		}
		normalizeOverlayLineMasks(*equalMasks);
		normalizeOverlayLineMasks(*missingMasks);
		normalizeOverlayLineMasks(*insertMasks);
		normalizeOverlayLineMasks(*offsetMasks);
		overlay->diffEqualLineMasks = std::move(equalMasks);
		overlay->diffMissingLineMasks = std::move(missingMasks);
		overlay->diffInsertLineMasks = std::move(insertMasks);
		overlay->diffOffsetLineMasks = std::move(offsetMasks);
	}
	return overlay;
}
