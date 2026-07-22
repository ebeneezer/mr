#include "MRMiniMap.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <limits>
#include <memory>
#include <utility>

namespace {

int tabDisplayWidth(const MREditSetupSettings &settings, int visualColumn) noexcept {
	const int currentColumn = std::max(1, visualColumn + 1);
	const int targetColumn = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);
	return std::max(1, targetColumn - currentColumn);
}

bool nextDisplayChar(TStringView text, std::size_t &index, std::size_t &width, int visualColumn, const MREditSetupSettings &settings) noexcept {
	if (index >= text.size()) return false;
	if (text[index] == '\t') {
		++index;
		width = static_cast<std::size_t>(tabDisplayWidth(settings, visualColumn));
		return true;
	}
	return TText::next(text, index, width);
}

std::string utf8FromCodepoint(std::uint32_t codepoint) {
	char bytes[5] = {0, 0, 0, 0, 0};
	if (codepoint <= 0x7F) {
		bytes[0] = static_cast<char>(codepoint);
		return std::string(bytes, 1);
	}
	if (codepoint <= 0x7FF) {
		bytes[0] = static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
		bytes[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
		return std::string(bytes, 2);
	}
	if (codepoint <= 0xFFFF) {
		bytes[0] = static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
		bytes[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
		bytes[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
		return std::string(bytes, 3);
	}
	bytes[0] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
	bytes[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
	bytes[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
	bytes[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
	return std::string(bytes, 4);
}

const std::array<std::string, 256> &brailleGlyphTable() {
	static const std::array<std::string, 256> table = []() {
		std::array<std::string, 256> generated;
		generated[0] = " ";
		for (std::size_t i = 1; i < generated.size(); ++i)
			generated[i] = utf8FromCodepoint(static_cast<std::uint32_t>(0x2800 + i));
		return generated;
	}();
	return table;
}

bool miniMapCellHasOverlayBits(std::uint64_t lineBits, int x, bool useBraille) noexcept {
	if (lineBits == 0 || x < 0) return false;
	if (useBraille) {
		const int leftDotColumn = x * 2;
		const int rightDotColumn = leftDotColumn + 1;
		if (leftDotColumn >= 64) return false;
		const std::uint64_t leftBit = (lineBits >> leftDotColumn) & 1ULL;
		const std::uint64_t rightBit = rightDotColumn < 64 ? ((lineBits >> rightDotColumn) & 1ULL) : 0ULL;
		return leftBit != 0 || rightBit != 0;
	}
	return x < 64 && ((lineBits >> x) & 1ULL) != 0;
}

std::vector<std::string> buildViewportAnchoredLineTexts(const mr::editor::ReadSnapshot &snapshot, std::size_t windowStartLine, std::size_t windowLineCount, std::size_t focusTopLine) {
	std::vector<std::string> lineTexts;

	if (windowLineCount == 0) return lineTexts;
	lineTexts.resize(windowLineCount);
	const std::size_t windowEndLine = windowStartLine + windowLineCount;
	const std::size_t focusLine = std::clamp(focusTopLine, windowStartLine, windowEndLine - 1);
	const std::size_t focusIndex = focusLine - windowStartLine;
	std::size_t lineStart = snapshot.lineStartByIndex(focusLine);

	lineTexts[focusIndex] = snapshot.lineText(lineStart);

	std::size_t forwardStart = lineStart;
	for (std::size_t lineIndex = focusLine + 1; lineIndex < windowEndLine; ++lineIndex) {
		if (forwardStart >= snapshot.length()) break;
		const std::size_t nextLineStart = snapshot.nextLine(forwardStart);
		if (nextLineStart <= forwardStart) break;
		forwardStart = nextLineStart;
		lineTexts[lineIndex - windowStartLine] = snapshot.lineText(forwardStart);
	}

	std::size_t backwardStart = lineStart;
	for (std::size_t lineIndex = focusLine; lineIndex > windowStartLine; --lineIndex) {
		if (backwardStart == 0) break;
		const std::size_t previousLineStart = snapshot.prevLine(backwardStart);
		if (previousLineStart >= backwardStart) break;
		backwardStart = previousLineStart;
		lineTexts[lineIndex - windowStartLine - 1] = snapshot.lineText(backwardStart);
	}

	return lineTexts;
}

} // namespace

MRMiniMapRenderer::OverlayState::OverlayState()
    : errorLineMasks(std::make_shared<const LineMasks>()), warningLineMasks(std::make_shared<const LineMasks>()), findLineMasks(std::make_shared<const LineMasks>()),
      dirtyLineMasks(std::make_shared<const LineMasks>()), diffEqualLineMasks(std::make_shared<const LineMasks>()), diffMissingLineMasks(std::make_shared<const LineMasks>()),
      diffInsertLineMasks(std::make_shared<const LineMasks>()), diffOffsetLineMasks(std::make_shared<const LineMasks>()) {
}

MRMiniMapRenderer::OverlaySources::OverlaySources()
    : revision(1), findRanges(std::make_shared<const std::vector<mr::editor::Range>>()), dirtyRanges(std::make_shared<const std::vector<mr::editor::Range>>()),
      errorRanges(std::make_shared<const std::vector<mr::editor::Range>>()), warningRanges(std::make_shared<const std::vector<mr::editor::Range>>()),
      fileCompareLineKinds(std::make_shared<const std::vector<unsigned char>>()),
      fileCompareMiniMapSlices(std::make_shared<const std::vector<MRFileCompareMiniMapSlice>>()) {
}

struct MRMiniMapRenderer::RendererState {
	struct RenderCache {
		bool braille = true;
		int rowCount = 0;
		int bodyWidth = 0;
		std::size_t documentId = 0;
		std::size_t documentVersion = 0;
		std::size_t totalLines = 1;
		std::size_t windowStartLine = 0;
		std::size_t windowLineCount = 1;
		int viewportWidth = 1;
		std::vector<unsigned char> rowPatterns;
		std::vector<std::size_t> rowLineStarts;
		std::vector<std::size_t> rowLineEnds;
	};

	struct PatternPacketState {
		std::uint64_t taskId = 0;
		int rowStart = 0;
		int rowEnd = 0;
		std::size_t lineStart = 0;
		std::size_t lineEnd = 0;
	};

	struct PatternGenerationState {
		std::uint64_t generation = 0;
		std::size_t documentId = 0;
		std::size_t version = 0;
		std::size_t totalLines = 1;
		std::size_t windowStartLine = 0;
		std::size_t windowLineCount = 1;
		int rowCount = 0;
		int bodyWidth = 0;
		int viewportWidth = 1;
		bool braille = true;
		std::vector<unsigned char> rowPatterns;
		std::vector<std::size_t> rowLineStarts;
		std::vector<std::size_t> rowLineEnds;
		std::vector<bool> rowReady;
		std::vector<PatternPacketState> packets;
	};

	struct OverlayPacketState {
		std::uint64_t taskId = 0;
		unsigned int componentMask = 0;
	};

	struct OverlayGenerationState {
		std::uint64_t generation = 0;
		std::size_t documentId = 0;
		std::size_t version = 0;
		std::size_t totalLines = 1;
		std::uint64_t sourceRevision = 0;
		std::size_t selectionStart = 0;
		std::size_t selectionEnd = 0;
		int viewportWidth = 1;
		int bodyWidth = 0;
		bool braille = true;
		OverlayState projection;
		std::vector<OverlayPacketState> packets;
	};

	struct SamplingWindow {
		std::size_t startLine = 0;
		std::size_t lineCount = 1;
	};

	std::uint64_t nextGeneration = 1;
	std::vector<PatternGenerationState> patternGenerations;
	std::vector<OverlayGenerationState> overlayGenerations;
	std::vector<std::shared_ptr<const RenderCache>> warmWindowLedger;
	std::shared_ptr<const RenderCache> activeCache;
	int activeCacheRowOffset = 0;
	std::shared_ptr<const OverlayState> activeOverlay;
	std::size_t activeOverlayDocumentId = 0;
	std::size_t activeOverlayVersion = 0;
	std::size_t activeOverlayTotalLines = 1;
	std::uint64_t activeOverlayRevision = 0;
	std::size_t activeOverlaySelectionStart = 0;
	std::size_t activeOverlaySelectionEnd = 0;
	int activeOverlayViewportWidth = 1;
	int activeOverlayBodyWidth = 0;
	bool activeOverlayBraille = true;
	std::size_t requestedDocumentId = 0;
	std::size_t requestedVersion = 0;
	std::size_t requestedTotalLines = 1;
	std::size_t requestedWindowStartLine = 0;
	std::size_t requestedWindowLineCount = 1;
	int requestedRows = 0;
	int requestedBodyWidth = 0;
	int requestedViewportWidth = 1;
	bool requestedBraille = true;
	std::size_t requestedTopLine = 0;
	bool requestedPatternValid = false;
	std::uint64_t requestedOverlayRevision = 0;
	std::size_t requestedSelectionStart = 0;
	std::size_t requestedSelectionEnd = 0;

	RendererState() {
		activeOverlay = std::make_shared<const OverlayState>();
	}

	~RendererState() {
		for (const PatternGenerationState &generation : patternGenerations)
			for (const PatternPacketState &packet : generation.packets)
				if (packet.taskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(packet.taskId));
		for (const OverlayGenerationState &generation : overlayGenerations)
			for (const OverlayPacketState &packet : generation.packets)
				if (packet.taskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(packet.taskId));
	}

	static bool hasProjectionFor(const std::shared_ptr<const RenderCache> &cache, int, int bodyWidth) noexcept {
		return cache != nullptr && cache->bodyWidth == bodyWidth && !cache->rowPatterns.empty();
	}

	static std::uint64_t lineMaskBits(const std::vector<OverlayState::LineMask> &masks, std::size_t lineIndex) noexcept {
		auto it = std::lower_bound(masks.begin(), masks.end(), lineIndex, [](const OverlayState::LineMask &mask, std::size_t value) { return mask.lineIndex < value; });
		return (it != masks.end() && it->lineIndex == lineIndex) ? it->dotColumnBits : 0;
	}

	static SamplingWindow samplingWindowFor(std::size_t totalLines, std::size_t topLine, int rowCount, bool useBraille) noexcept {
		const std::size_t normalizedTotalLines = std::max<std::size_t>(1, totalLines);
		const std::size_t lineSamplesPerRow = useBraille ? 4 : 1;
		const std::size_t normalizedRowCount = std::max<std::size_t>(1, static_cast<std::size_t>(std::max(rowCount, 1)) * lineSamplesPerRow);
		SamplingWindow window;
		if (normalizedTotalLines <= normalizedRowCount) {
			window.startLine = 0;
			window.lineCount = normalizedTotalLines;
			return window;
		}
		window.lineCount = normalizedRowCount;
		const std::size_t clampedTop = std::min(topLine, normalizedTotalLines - 1);
		const std::size_t halfWindow = normalizedRowCount / 2;
		if (clampedTop <= halfWindow) window.startLine = 0;
		else {
			const std::size_t centeredStart = clampedTop - halfWindow;
			if (centeredStart + normalizedRowCount >= normalizedTotalLines)
				window.startLine = normalizedTotalLines - normalizedRowCount;
			else
				window.startLine = centeredStart - centeredStart % lineSamplesPerRow;
		}
		return window;
	}

	static SamplingWindow acquisitionWindowFor(std::size_t totalLines, const SamplingWindow &visibleWindow, std::size_t previousTopLine, std::size_t topLine,
	                                           bool previousValid, bool useBraille) noexcept {
		const std::size_t samplesPerRow = useBraille ? 4 : 1;
		const std::size_t coreCount = std::max<std::size_t>(1, mr::coprocessor::globalCoprocessor().allowedCoreCount());
		const std::size_t screensPerWorker = 4;
		std::size_t acquisitionScreens = coreCount;
		if (acquisitionScreens <= std::numeric_limits<std::size_t>::max() / screensPerWorker) acquisitionScreens *= screensPerWorker;
		else
			acquisitionScreens = std::numeric_limits<std::size_t>::max();
		acquisitionScreens = std::max<std::size_t>(8, acquisitionScreens);
		std::size_t desiredLines = visibleWindow.lineCount;
		if (desiredLines <= std::numeric_limits<std::size_t>::max() / acquisitionScreens) desiredLines *= acquisitionScreens;
		else
			desiredLines = std::numeric_limits<std::size_t>::max();
		desiredLines = std::min(std::max<std::size_t>(visibleWindow.lineCount, desiredLines), std::max<std::size_t>(1, totalLines));
		const std::size_t desiredRows = (desiredLines + samplesPerRow - 1) / samplesPerRow;
		const std::size_t visibleRows = (visibleWindow.lineCount + samplesPerRow - 1) / samplesPerRow;
		const std::size_t spareRows = desiredRows > visibleRows ? desiredRows - visibleRows : 0;
		std::size_t rowsBefore = spareRows / 2;
		const std::size_t directionalCushion = std::min<std::size_t>(spareRows, std::max<std::size_t>(visibleRows, visibleRows * screensPerWorker));
		if (previousValid && topLine > previousTopLine) rowsBefore = directionalCushion;
		else if (previousValid && topLine < previousTopLine)
			rowsBefore = spareRows > directionalCushion ? spareRows - directionalCushion : 0;
		rowsBefore = std::min(rowsBefore, visibleWindow.startLine / samplesPerRow);
		SamplingWindow window;
		window.startLine = visibleWindow.startLine - rowsBefore * samplesPerRow;
		window.lineCount = std::min(desiredLines, totalLines - window.startLine);
		const std::size_t visibleEnd = visibleWindow.startLine + visibleWindow.lineCount;
		if (window.startLine + window.lineCount < visibleEnd) window.lineCount = visibleEnd - window.startLine;
		return window;
	}

	static std::size_t warmWindowLimit() noexcept {
		const std::size_t coreCount = std::max<std::size_t>(1, mr::coprocessor::globalCoprocessor().allowedCoreCount());
		if (coreCount > std::numeric_limits<std::size_t>::max() / 2) return std::numeric_limits<std::size_t>::max();
		return std::max<std::size_t>(4, coreCount * 2);
	}

	static int rowOffsetForWindow(const std::vector<std::size_t> &rowLineStarts, const std::vector<std::size_t> &rowLineEnds, const SamplingWindow &window,
	                              bool braille) noexcept {
		if (rowLineStarts.empty() || rowLineStarts.size() != rowLineEnds.size()) return -1;
		const std::vector<std::size_t>::const_iterator first = std::lower_bound(rowLineStarts.begin(), rowLineStarts.end(), window.startLine);
		if (first == rowLineStarts.end() || *first != window.startLine) return -1;
		const std::size_t samplesPerRow = braille ? 4 : 1;
		const std::size_t requiredRows = (window.lineCount + samplesPerRow - 1) / samplesPerRow;
		const std::size_t offset = static_cast<std::size_t>(first - rowLineStarts.begin());
		if (requiredRows == 0 || offset + requiredRows > rowLineEnds.size()) return -1;
		const std::size_t windowEnd = window.startLine + window.lineCount;
		if (rowLineEnds[offset + requiredRows - 1] < windowEnd) return -1;
		if (offset > static_cast<std::size_t>(INT_MAX)) return -1;
		return static_cast<int>(offset);
	}

	static int cacheRowOffset(const RenderCache &cache, const Viewport &viewport, bool braille, const SamplingWindow &window, std::size_t documentId,
	                         std::size_t version) noexcept {
		if (cache.documentId != documentId || cache.documentVersion != version || cache.bodyWidth != viewport.bodyWidth ||
		    cache.viewportWidth != std::max(1, viewport.width) || cache.braille != braille)
			return -1;
		return rowOffsetForWindow(cache.rowLineStarts, cache.rowLineEnds, window, braille);
	}

	bool cacheReadyForViewport(const Viewport &viewport, int rowCount, bool braille, const SamplingWindow &window, std::size_t documentId, std::size_t version) const noexcept {
		static_cast<void>(rowCount);
		return activeCache != nullptr && cacheRowOffset(*activeCache, viewport, braille, window, documentId, version) >= 0;
	}

	bool generationCovers(const PatternGenerationState &generation, const Viewport &viewport, bool braille, const SamplingWindow &window,
	                     std::size_t documentId, std::size_t version) const noexcept {
		if (generation.documentId != documentId || generation.version != version || generation.bodyWidth != viewport.bodyWidth ||
		    generation.viewportWidth != std::max(1, viewport.width) || generation.braille != braille)
			return false;
		return rowOffsetForWindow(generation.rowLineStarts, generation.rowLineEnds, window, braille) >= 0;
	}

	bool selectWarmWindow(const Viewport &viewport, int rowCount, bool braille, const SamplingWindow &window, std::size_t documentId, std::size_t version) {
		static_cast<void>(rowCount);
		for (std::size_t index = warmWindowLedger.size(); index-- > 0;) {
			const int rowOffset = cacheRowOffset(*warmWindowLedger[index], viewport, braille, window, documentId, version);
			if (rowOffset < 0) continue;
			std::shared_ptr<const RenderCache> selected = warmWindowLedger[index];
			warmWindowLedger.erase(warmWindowLedger.begin() + static_cast<std::ptrdiff_t>(index));
			warmWindowLedger.push_back(selected);
			activeCache = std::move(selected);
			activeCacheRowOffset = rowOffset;
			return true;
		}
		return false;
	}

	bool activateCurrentCache(const Viewport &viewport, bool braille, const SamplingWindow &window, std::size_t documentId, std::size_t version) noexcept {
		if (activeCache == nullptr) return false;
		const int rowOffset = cacheRowOffset(*activeCache, viewport, braille, window, documentId, version);
		if (rowOffset < 0) return false;
		activeCacheRowOffset = rowOffset;
		return true;
	}

	bool activateRequestedCache(const std::shared_ptr<const RenderCache> &cache) noexcept {
		if (cache == nullptr || cache->documentId != requestedDocumentId || cache->documentVersion != requestedVersion || cache->bodyWidth != requestedBodyWidth ||
		    cache->viewportWidth != requestedViewportWidth || cache->braille != requestedBraille)
			return false;
		SamplingWindow requestedWindow;
		requestedWindow.startLine = requestedWindowStartLine;
		requestedWindow.lineCount = requestedWindowLineCount;
		Viewport requestedViewport;
		requestedViewport.width = requestedViewportWidth;
		requestedViewport.bodyWidth = requestedBodyWidth;
		const int rowOffset = cacheRowOffset(*cache, requestedViewport, requestedBraille, requestedWindow, requestedDocumentId, requestedVersion);
		if (rowOffset < 0) return false;
		activeCache = cache;
		activeCacheRowOffset = rowOffset;
		return true;
	}

	bool activateRequestedRows(const PatternGenerationState &generation) {
		if (generation.documentId != requestedDocumentId || generation.version != requestedVersion || generation.bodyWidth != requestedBodyWidth ||
		    generation.viewportWidth != requestedViewportWidth || generation.braille != requestedBraille)
			return false;
		SamplingWindow requestedWindow;
		requestedWindow.startLine = requestedWindowStartLine;
		requestedWindow.lineCount = requestedWindowLineCount;
		const int rowOffset = rowOffsetForWindow(generation.rowLineStarts, generation.rowLineEnds, requestedWindow, requestedBraille);
		if (rowOffset < 0) return false;
		const std::size_t samplesPerRow = requestedBraille ? 4 : 1;
		const std::size_t requestedRowCount = (requestedWindow.lineCount + samplesPerRow - 1) / samplesPerRow;
		const std::size_t firstRow = static_cast<std::size_t>(rowOffset);
		if (firstRow + requestedRowCount > generation.rowReady.size()) return false;
		for (std::size_t row = firstRow; row < firstRow + requestedRowCount; ++row)
			if (!generation.rowReady[row]) return false;
		std::shared_ptr<RenderCache> cache = std::make_shared<RenderCache>();
		cache->braille = generation.braille;
		cache->rowCount = static_cast<int>(requestedRowCount);
		cache->bodyWidth = generation.bodyWidth;
		cache->documentId = generation.documentId;
		cache->documentVersion = generation.version;
		cache->totalLines = generation.totalLines;
		cache->windowStartLine = requestedWindow.startLine;
		cache->windowLineCount = requestedWindow.lineCount;
		cache->viewportWidth = generation.viewportWidth;
		const std::size_t patternStart = firstRow * static_cast<std::size_t>(generation.bodyWidth);
		const std::size_t patternEnd = (firstRow + requestedRowCount) * static_cast<std::size_t>(generation.bodyWidth);
		if (patternEnd > generation.rowPatterns.size()) return false;
		cache->rowPatterns.assign(generation.rowPatterns.begin() + static_cast<std::ptrdiff_t>(patternStart),
		                          generation.rowPatterns.begin() + static_cast<std::ptrdiff_t>(patternEnd));
		cache->rowLineStarts.assign(generation.rowLineStarts.begin() + static_cast<std::ptrdiff_t>(firstRow),
		                            generation.rowLineStarts.begin() + static_cast<std::ptrdiff_t>(firstRow + requestedRowCount));
		cache->rowLineEnds.assign(generation.rowLineEnds.begin() + static_cast<std::ptrdiff_t>(firstRow),
		                          generation.rowLineEnds.begin() + static_cast<std::ptrdiff_t>(firstRow + requestedRowCount));
		rememberCache(cache);
		return activateRequestedCache(cache);
	}

	static bool allRowsReady(const PatternGenerationState &generation) noexcept {
		for (bool ready : generation.rowReady)
			if (!ready) return false;
		return true;
	}

	void rememberCache(const std::shared_ptr<const RenderCache> &cache) {
		warmWindowLedger.push_back(cache);
		while (warmWindowLedger.size() > warmWindowLimit()) warmWindowLedger.erase(warmWindowLedger.begin());
	}

	Signals clearWarmupTask(std::uint64_t expectedTaskId) noexcept {
		Signals signals;

		if (expectedTaskId == 0) return signals;
		for (std::size_t generationIndex = 0; generationIndex < patternGenerations.size(); ++generationIndex)
			for (const PatternPacketState &packet : patternGenerations[generationIndex].packets) {
				if (packet.taskId != expectedTaskId) continue;
				for (const PatternGenerationState &generation : patternGenerations)
					for (const PatternPacketState &remaining : generation.packets)
						if (remaining.taskId != 0 && remaining.taskId != expectedTaskId)
							static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(remaining.taskId));
				patternGenerations.clear();
				signals.notifyTaskStateChanged = true;
				return signals;
			}
		for (std::size_t generationIndex = 0; generationIndex < overlayGenerations.size(); ++generationIndex)
			for (const OverlayPacketState &packet : overlayGenerations[generationIndex].packets) {
				if (packet.taskId != expectedTaskId) continue;
				for (const OverlayPacketState &remaining : overlayGenerations[generationIndex].packets)
					if (remaining.taskId != 0 && remaining.taskId != expectedTaskId)
						static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(remaining.taskId));
				overlayGenerations.erase(overlayGenerations.begin() + static_cast<std::ptrdiff_t>(generationIndex));
				signals.notifyTaskStateChanged = true;
				return signals;
			}
		return signals;
	}

	Signals invalidate(bool cancelTask, std::size_t documentId) noexcept {
		Signals signals;
		if (cancelTask) {
			for (const PatternGenerationState &generation : patternGenerations)
				for (const PatternPacketState &packet : generation.packets)
					if (packet.taskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(packet.taskId));
			for (const OverlayGenerationState &generation : overlayGenerations)
				for (const OverlayPacketState &packet : generation.packets)
					if (packet.taskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(packet.taskId));
			signals.notifyTaskStateChanged = !patternGenerations.empty() || !overlayGenerations.empty();
			patternGenerations.clear();
			overlayGenerations.clear();
		}
		if (cancelTask || activeCache == nullptr || activeCache->documentId != documentId) {
			activeCache.reset();
			activeCacheRowOffset = 0;
			activeOverlay = std::make_shared<const OverlayState>();
			warmWindowLedger.clear();
		}
		if (cancelTask) requestedPatternValid = false;
		return signals;
	}
};

MRMiniMapRenderer::MRMiniMapRenderer() noexcept : mState(std::make_unique<RendererState>()) {
}

MRMiniMapRenderer::~MRMiniMapRenderer() noexcept = default;

bool MRMiniMapRenderer::useBrailleRenderer() noexcept {
	static const int kBrailleWidth = strwidth("\xE2\xA3\xBF");
	return kBrailleWidth == 1;
}

std::string MRMiniMapRenderer::normalizedViewportMarkerGlyph(const std::string &configuredGlyph) {
	if (configuredGlyph.empty() || strwidth(configuredGlyph.c_str()) != 1) return "│";
	return configuredGlyph;
}

std::uint64_t MRMiniMapRenderer::pendingWarmupTaskId() const noexcept {
	if (mState == nullptr) return 0;
	for (const RendererState::PatternGenerationState &generation : mState->patternGenerations)
		for (const RendererState::PatternPacketState &packet : generation.packets)
			if (packet.taskId != 0) return packet.taskId;
	for (const RendererState::OverlayGenerationState &generation : mState->overlayGenerations)
		for (const RendererState::OverlayPacketState &packet : generation.packets)
			if (packet.taskId != 0) return packet.taskId;
	return 0;
}

std::size_t MRMiniMapRenderer::pendingWarmupTaskCount() const noexcept {
	if (mState == nullptr) return 0;
	std::size_t count = 0;
	for (const RendererState::PatternGenerationState &generation : mState->patternGenerations)
		count += generation.packets.size();
	for (const RendererState::OverlayGenerationState &generation : mState->overlayGenerations)
		count += generation.packets.size();
	return count;
}

bool MRMiniMapRenderer::ownsWarmupTask(std::uint64_t taskId) const noexcept {
	if (mState == nullptr || taskId == 0) return false;
	for (const RendererState::PatternGenerationState &generation : mState->patternGenerations)
		for (const RendererState::PatternPacketState &packet : generation.packets)
			if (packet.taskId == taskId) return true;
	for (const RendererState::OverlayGenerationState &generation : mState->overlayGenerations)
		for (const RendererState::OverlayPacketState &packet : generation.packets)
			if (packet.taskId == taskId) return true;
	return false;
}

bool MRMiniMapRenderer::hasProjection(int rowCount, int bodyWidth) const noexcept {
	return mState != nullptr && RendererState::hasProjectionFor(mState->activeCache, rowCount, bodyWidth);
}

bool MRMiniMapRenderer::hasAnyProjection() const noexcept {
	return mState != nullptr && mState->activeCache != nullptr && !mState->activeCache->rowPatterns.empty();
}

MRMiniMapRenderer::Signals MRMiniMapRenderer::clearWarmupTask(std::uint64_t expectedTaskId) noexcept {
	return mState != nullptr ? mState->clearWarmupTask(expectedTaskId) : Signals();
}

MRMiniMapRenderer::Signals MRMiniMapRenderer::invalidate(bool cancelTask, std::size_t documentId) noexcept {
	return mState != nullptr ? mState->invalidate(cancelTask, documentId) : Signals();
}

MRMiniMapRenderer::ApplyWarmupResult MRMiniMapRenderer::applyWarmup(const mr::coprocessor::Payload &payload, const mr::coprocessor::Result &taskResult,
                                                                   std::size_t documentId, std::size_t version) noexcept {
	ApplyWarmupResult result;

	if (mState == nullptr || taskResult.task.id == 0 || taskResult.task.documentId != documentId || taskResult.task.baseVersion != version) return result;
	const mr::coprocessor::MiniMapWarmupPayload *pattern = dynamic_cast<const mr::coprocessor::MiniMapWarmupPayload *>(&payload);
	if (pattern != nullptr) {
		std::size_t sourceGenerationIndex = mState->patternGenerations.size();
		std::size_t sourcePacketIndex = 0;
		for (std::size_t generationIndex = 0; generationIndex < mState->patternGenerations.size(); ++generationIndex) {
			RendererState::PatternGenerationState &generation = mState->patternGenerations[generationIndex];
			if (generation.generation != pattern->generation || generation.generation != taskResult.task.generation || generation.documentId != documentId ||
			    generation.version != version)
				continue;
			for (std::size_t packetIndex = 0; packetIndex < generation.packets.size(); ++packetIndex) {
				const RendererState::PatternPacketState packet = generation.packets[packetIndex];
				if (packet.taskId != taskResult.task.id) continue;
				const int packetRows = packet.rowEnd - packet.rowStart;
				const std::size_t expectedPatternCount = static_cast<std::size_t>(std::max(0, packetRows)) * static_cast<std::size_t>(std::max(0, generation.bodyWidth));
				if (pattern->packetRowStart != packet.rowStart || pattern->packetRowEnd != packet.rowEnd || pattern->rowCount != generation.rowCount ||
				    pattern->bodyWidth != generation.bodyWidth || pattern->viewportWidth != generation.viewportWidth || pattern->braille != generation.braille ||
				    pattern->totalLines != generation.totalLines || pattern->windowStartLine != generation.windowStartLine ||
				    pattern->windowLineCount != generation.windowLineCount || pattern->rowPatterns.size() != expectedPatternCount ||
				    pattern->rowLineStarts.size() != static_cast<std::size_t>(std::max(0, packetRows)) ||
				    pattern->rowLineEnds.size() != static_cast<std::size_t>(std::max(0, packetRows)))
					return result;
				sourceGenerationIndex = generationIndex;
				sourcePacketIndex = packetIndex;
				break;
			}
			if (sourceGenerationIndex != mState->patternGenerations.size()) break;
		}
		if (sourceGenerationIndex == mState->patternGenerations.size()) return result;
		mState->patternGenerations[sourceGenerationIndex].packets.erase(
		    mState->patternGenerations[sourceGenerationIndex].packets.begin() + static_cast<std::ptrdiff_t>(sourcePacketIndex));
		result.signals.notifyTaskStateChanged = true;
		for (RendererState::PatternGenerationState &generation : mState->patternGenerations) {
			if (generation.documentId != documentId || generation.version != version || generation.totalLines != pattern->totalLines ||
			    generation.bodyWidth != pattern->bodyWidth || generation.viewportWidth != pattern->viewportWidth || generation.braille != pattern->braille)
				continue;
			for (std::size_t sourceRow = 0; sourceRow < pattern->rowLineStarts.size(); ++sourceRow) {
				const std::vector<std::size_t>::iterator target =
				    std::lower_bound(generation.rowLineStarts.begin(), generation.rowLineStarts.end(), pattern->rowLineStarts[sourceRow]);
				if (target == generation.rowLineStarts.end() || *target != pattern->rowLineStarts[sourceRow]) continue;
				const std::size_t targetRow = static_cast<std::size_t>(target - generation.rowLineStarts.begin());
				if (targetRow >= generation.rowLineEnds.size() || generation.rowLineEnds[targetRow] != pattern->rowLineEnds[sourceRow] ||
				    targetRow >= generation.rowReady.size())
					continue;
				const std::size_t sourceOffset = sourceRow * static_cast<std::size_t>(pattern->bodyWidth);
				const std::size_t targetOffset = targetRow * static_cast<std::size_t>(generation.bodyWidth);
				if (sourceOffset + static_cast<std::size_t>(pattern->bodyWidth) > pattern->rowPatterns.size() ||
				    targetOffset + static_cast<std::size_t>(generation.bodyWidth) > generation.rowPatterns.size())
					continue;
				std::copy(pattern->rowPatterns.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
				          pattern->rowPatterns.begin() + static_cast<std::ptrdiff_t>(sourceOffset + static_cast<std::size_t>(pattern->bodyWidth)),
				          generation.rowPatterns.begin() + static_cast<std::ptrdiff_t>(targetOffset));
				generation.rowReady[targetRow] = true;
			}
		}
		for (const RendererState::PatternGenerationState &generation : mState->patternGenerations)
			if (mState->activateRequestedRows(generation)) {
				result.signals.redraw = true;
				break;
			}
		for (std::size_t generationIndex = 0; generationIndex < mState->patternGenerations.size();) {
			RendererState::PatternGenerationState &generation = mState->patternGenerations[generationIndex];
			if (!generation.packets.empty() || !RendererState::allRowsReady(generation)) {
				++generationIndex;
				continue;
			}
			std::shared_ptr<RendererState::RenderCache> cache = std::make_shared<RendererState::RenderCache>();
			cache->braille = generation.braille;
			cache->rowCount = generation.rowCount;
			cache->bodyWidth = generation.bodyWidth;
			cache->documentId = generation.documentId;
			cache->documentVersion = generation.version;
			cache->totalLines = generation.totalLines;
			cache->windowStartLine = generation.windowStartLine;
			cache->windowLineCount = generation.windowLineCount;
			cache->viewportWidth = generation.viewportWidth;
			cache->rowPatterns = std::move(generation.rowPatterns);
			cache->rowLineStarts = std::move(generation.rowLineStarts);
			cache->rowLineEnds = std::move(generation.rowLineEnds);
			mState->rememberCache(cache);
			static_cast<void>(mState->activateRequestedCache(cache));
			result.signals.redraw = true;
			mState->patternGenerations.erase(mState->patternGenerations.begin() + static_cast<std::ptrdiff_t>(generationIndex));
		}
		result.applied = true;
		return result;
	}

	const MRMiniMapOverlayPacketPayload *overlay = dynamic_cast<const MRMiniMapOverlayPacketPayload *>(&payload);
	if (overlay == nullptr || overlay->projection == nullptr) return result;
	for (std::size_t generationIndex = 0; generationIndex < mState->overlayGenerations.size(); ++generationIndex) {
		RendererState::OverlayGenerationState &generation = mState->overlayGenerations[generationIndex];
		if (generation.generation != overlay->generation || generation.generation != taskResult.task.generation || generation.documentId != documentId ||
		    generation.version != version)
			continue;
		for (std::size_t packetIndex = 0; packetIndex < generation.packets.size(); ++packetIndex) {
			const RendererState::OverlayPacketState packet = generation.packets[packetIndex];
			if (packet.taskId != taskResult.task.id) continue;
			if (packet.componentMask != overlay->componentMask || (packet.componentMask & ~overlayAll) != 0) return result;
			if ((packet.componentMask & overlayFind) != 0) generation.projection.findLineMasks = overlay->projection->findLineMasks;
			if ((packet.componentMask & overlayDirty) != 0) generation.projection.dirtyLineMasks = overlay->projection->dirtyLineMasks;
			if ((packet.componentMask & overlayError) != 0) generation.projection.errorLineMasks = overlay->projection->errorLineMasks;
			if ((packet.componentMask & overlayWarning) != 0) generation.projection.warningLineMasks = overlay->projection->warningLineMasks;
			if ((packet.componentMask & overlayDiff) != 0) {
				generation.projection.diffEqualLineMasks = overlay->projection->diffEqualLineMasks;
				generation.projection.diffMissingLineMasks = overlay->projection->diffMissingLineMasks;
				generation.projection.diffInsertLineMasks = overlay->projection->diffInsertLineMasks;
				generation.projection.diffOffsetLineMasks = overlay->projection->diffOffsetLineMasks;
			}
			generation.packets.erase(generation.packets.begin() + static_cast<std::ptrdiff_t>(packetIndex));
			result.signals.notifyTaskStateChanged = true;
			if (generation.packets.empty()) {
				std::shared_ptr<const OverlayState> projection = std::make_shared<const OverlayState>(generation.projection);
				const bool requested = generation.documentId == mState->requestedDocumentId && generation.version == mState->requestedVersion &&
				                       generation.totalLines == mState->requestedTotalLines && generation.sourceRevision == mState->requestedOverlayRevision &&
				                       generation.selectionStart == mState->requestedSelectionStart && generation.selectionEnd == mState->requestedSelectionEnd &&
				                       generation.viewportWidth == mState->requestedViewportWidth && generation.bodyWidth == mState->requestedBodyWidth &&
				                       generation.braille == mState->requestedBraille;
				if (requested) {
					mState->activeOverlay = std::move(projection);
					mState->activeOverlayDocumentId = generation.documentId;
					mState->activeOverlayVersion = generation.version;
					mState->activeOverlayTotalLines = generation.totalLines;
					mState->activeOverlayRevision = generation.sourceRevision;
					mState->activeOverlaySelectionStart = generation.selectionStart;
					mState->activeOverlaySelectionEnd = generation.selectionEnd;
					mState->activeOverlayViewportWidth = generation.viewportWidth;
					mState->activeOverlayBodyWidth = generation.bodyWidth;
					mState->activeOverlayBraille = generation.braille;
					result.signals.redraw = true;
				}
				mState->overlayGenerations.erase(mState->overlayGenerations.begin() + static_cast<std::ptrdiff_t>(generationIndex));
			}
			result.applied = true;
			return result;
		}
	}
	return result;
}

MRMiniMapRenderer::Signals MRMiniMapRenderer::scheduleWarmupIfNeeded(const Viewport &viewport, int rowCount, bool useBraille, std::size_t totalLinesHint,
                                                                     std::size_t topLine, std::size_t documentId, std::size_t version,
                                                                     mr::coprocessor::ExecutionOwnerKind executionOwnerKind, std::size_t executionOwnerLocalId,
                                                                     const mr::editor::ReadSnapshot &snapshot,
                                                                     const MREditSetupSettings &settings, const OverlaySources &overlaySources,
                                                                     const mr::editor::Range &selection) {
	Signals signals;

	if (mState == nullptr) return signals;
	if (viewport.bodyWidth <= 0 || rowCount <= 0) return invalidate(true, documentId);
	std::size_t totalLines = std::max<std::size_t>(1, totalLinesHint);
	if (snapshot.exactLineCountKnown()) totalLines = std::max<std::size_t>(1, snapshot.lineCount());
	const RendererState::SamplingWindow samplingWindow = RendererState::samplingWindowFor(totalLines, topLine, rowCount, useBraille);
	const int bodyWidth = viewport.bodyWidth;
	const int viewportWidth = std::max(1, viewport.width);
	const bool previousPatternValid = mState->requestedPatternValid && mState->requestedDocumentId == documentId && mState->requestedVersion == version &&
	                                  mState->requestedBodyWidth == bodyWidth && mState->requestedViewportWidth == viewportWidth &&
	                                  mState->requestedBraille == useBraille;
	const std::size_t previousTopLine = mState->requestedTopLine;
	mr::editor::Range normalizedSelection = selection.normalized();
	if (normalizedSelection.empty()) normalizedSelection = mr::editor::Range(0, 0);
	mState->requestedDocumentId = documentId;
	mState->requestedVersion = version;
	mState->requestedTotalLines = totalLines;
	mState->requestedWindowStartLine = samplingWindow.startLine;
	mState->requestedWindowLineCount = samplingWindow.lineCount;
	mState->requestedRows = rowCount;
	mState->requestedBodyWidth = bodyWidth;
	mState->requestedViewportWidth = viewportWidth;
	mState->requestedBraille = useBraille;
	mState->requestedTopLine = topLine;
	mState->requestedPatternValid = true;
	mState->requestedOverlayRevision = overlaySources.revision;
	mState->requestedSelectionStart = normalizedSelection.start;
	mState->requestedSelectionEnd = normalizedSelection.end;

	bool patternReady = mState->activateCurrentCache(viewport, useBraille, samplingWindow, documentId, version);
	if (!patternReady) patternReady = mState->selectWarmWindow(viewport, rowCount, useBraille, samplingWindow, documentId, version);
	if (!patternReady)
		for (const RendererState::PatternGenerationState &generation : mState->patternGenerations)
			if (mState->activateRequestedRows(generation)) {
				patternReady = true;
				break;
			}
	bool patternPending = false;
	for (const RendererState::PatternGenerationState &generation : mState->patternGenerations)
		if (mState->generationCovers(generation, viewport, useBraille, samplingWindow, documentId, version)) {
			patternPending = true;
			break;
		}
	if (!patternReady && !patternPending) {
		const RendererState::SamplingWindow acquisitionWindow =
		    RendererState::acquisitionWindowFor(totalLines, samplingWindow, previousTopLine, topLine, previousPatternValid, useBraille);
		RendererState::PatternGenerationState generation;
		generation.generation = mState->nextGeneration++;
		if (mState->nextGeneration == 0) ++mState->nextGeneration;
		generation.documentId = documentId;
		generation.version = version;
		generation.totalLines = totalLines;
		generation.windowStartLine = acquisitionWindow.startLine;
		generation.windowLineCount = std::max<std::size_t>(1, acquisitionWindow.lineCount);
		const std::size_t samplesPerRow = useBraille ? 4 : 1;
		const std::size_t acquisitionRows = (generation.windowLineCount + samplesPerRow - 1) / samplesPerRow;
		generation.rowCount = static_cast<int>(std::min<std::size_t>(acquisitionRows, static_cast<std::size_t>(INT_MAX)));
		generation.bodyWidth = bodyWidth;
		generation.viewportWidth = viewportWidth;
		generation.braille = useBraille;
		generation.rowPatterns.assign(static_cast<std::size_t>(generation.rowCount) * static_cast<std::size_t>(bodyWidth), 0);
		generation.rowLineStarts.assign(static_cast<std::size_t>(generation.rowCount), generation.windowStartLine + generation.windowLineCount);
		generation.rowLineEnds.assign(static_cast<std::size_t>(generation.rowCount), generation.windowStartLine + generation.windowLineCount);
		generation.rowReady.assign(static_cast<std::size_t>(generation.rowCount), false);
		const int effectiveRows = generation.rowCount;
		std::vector<bool> readyRows(static_cast<std::size_t>(effectiveRows), false);
		for (int row = 0; row < effectiveRows; ++row) {
			const std::size_t rowSampleStart = static_cast<std::size_t>(row) * samplesPerRow;
			generation.rowLineStarts[static_cast<std::size_t>(row)] = generation.windowStartLine + rowSampleStart;
			generation.rowLineEnds[static_cast<std::size_t>(row)] =
			    generation.windowStartLine + std::min(generation.windowLineCount, rowSampleStart + samplesPerRow);
		}
		for (std::size_t cacheIndex = mState->warmWindowLedger.size(); cacheIndex-- > 0;) {
			const std::shared_ptr<const RendererState::RenderCache> &cache = mState->warmWindowLedger[cacheIndex];
			if (cache == nullptr || cache->documentId != documentId || cache->documentVersion != version || cache->bodyWidth != bodyWidth ||
			    cache->viewportWidth != viewportWidth || cache->braille != useBraille)
				continue;
			for (int row = 0; row < effectiveRows; ++row) {
				if (readyRows[static_cast<std::size_t>(row)]) continue;
				const std::size_t targetStart = generation.rowLineStarts[static_cast<std::size_t>(row)];
				if (targetStart < cache->windowStartLine) continue;
				const std::size_t delta = targetStart - cache->windowStartLine;
				if (delta % samplesPerRow != 0) continue;
				const std::size_t sourceRow = delta / samplesPerRow;
				if (sourceRow >= cache->rowLineStarts.size() || sourceRow >= cache->rowLineEnds.size() ||
				    cache->rowLineStarts[sourceRow] != targetStart || cache->rowLineEnds[sourceRow] != generation.rowLineEnds[static_cast<std::size_t>(row)])
					continue;
				const std::size_t sourceOffset = sourceRow * static_cast<std::size_t>(bodyWidth);
				const std::size_t targetOffset = static_cast<std::size_t>(row) * static_cast<std::size_t>(bodyWidth);
				if (sourceOffset + static_cast<std::size_t>(bodyWidth) > cache->rowPatterns.size()) continue;
				std::copy(cache->rowPatterns.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
				          cache->rowPatterns.begin() + static_cast<std::ptrdiff_t>(sourceOffset + static_cast<std::size_t>(bodyWidth)),
				          generation.rowPatterns.begin() + static_cast<std::ptrdiff_t>(targetOffset));
				readyRows[static_cast<std::size_t>(row)] = true;
			}
		}
		for (const RendererState::PatternGenerationState &pending : mState->patternGenerations) {
			if (pending.documentId != documentId || pending.version != version || pending.totalLines != totalLines || pending.bodyWidth != bodyWidth ||
			    pending.viewportWidth != viewportWidth || pending.braille != useBraille)
				continue;
			for (int row = 0; row < effectiveRows; ++row) {
				if (readyRows[static_cast<std::size_t>(row)]) continue;
				const std::vector<std::size_t>::const_iterator source =
				    std::lower_bound(pending.rowLineStarts.begin(), pending.rowLineStarts.end(), generation.rowLineStarts[static_cast<std::size_t>(row)]);
				if (source == pending.rowLineStarts.end() || *source != generation.rowLineStarts[static_cast<std::size_t>(row)]) continue;
				const std::size_t sourceRow = static_cast<std::size_t>(source - pending.rowLineStarts.begin());
				if (sourceRow >= pending.rowLineEnds.size() || sourceRow >= pending.rowReady.size() || !pending.rowReady[sourceRow] ||
				    pending.rowLineEnds[sourceRow] != generation.rowLineEnds[static_cast<std::size_t>(row)])
					continue;
				const std::size_t sourceOffset = sourceRow * static_cast<std::size_t>(bodyWidth);
				const std::size_t targetOffset = static_cast<std::size_t>(row) * static_cast<std::size_t>(bodyWidth);
				if (sourceOffset + static_cast<std::size_t>(bodyWidth) > pending.rowPatterns.size()) continue;
				std::copy(pending.rowPatterns.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
				          pending.rowPatterns.begin() + static_cast<std::ptrdiff_t>(sourceOffset + static_cast<std::size_t>(bodyWidth)),
				          generation.rowPatterns.begin() + static_cast<std::ptrdiff_t>(targetOffset));
				readyRows[static_cast<std::size_t>(row)] = true;
			}
		}
		std::vector<bool> reservedRows(static_cast<std::size_t>(effectiveRows), false);
		for (const RendererState::PatternGenerationState &pending : mState->patternGenerations) {
			if (pending.documentId != documentId || pending.version != version || pending.totalLines != totalLines || pending.bodyWidth != bodyWidth ||
			    pending.viewportWidth != viewportWidth || pending.braille != useBraille)
				continue;
			for (const RendererState::PatternPacketState &packet : pending.packets) {
				for (int row = 0; row < effectiveRows; ++row) {
					if (readyRows[static_cast<std::size_t>(row)] || reservedRows[static_cast<std::size_t>(row)]) continue;
					const std::vector<std::size_t>::const_iterator source =
					    std::lower_bound(pending.rowLineStarts.begin(), pending.rowLineStarts.end(), generation.rowLineStarts[static_cast<std::size_t>(row)]);
					if (source == pending.rowLineStarts.end() || *source != generation.rowLineStarts[static_cast<std::size_t>(row)]) continue;
					const std::size_t sourceRow = static_cast<std::size_t>(source - pending.rowLineStarts.begin());
					if (sourceRow >= pending.rowLineEnds.size() || pending.rowLineEnds[sourceRow] != generation.rowLineEnds[static_cast<std::size_t>(row)] ||
					    sourceRow < static_cast<std::size_t>(std::max(0, packet.rowStart)) || sourceRow >= static_cast<std::size_t>(std::max(0, packet.rowEnd)))
						continue;
					reservedRows[static_cast<std::size_t>(row)] = true;
				}
			}
		}
		generation.rowReady = readyRows;
		std::vector<int> missingRows;
		for (int row = 0; row < effectiveRows; ++row)
			if (!readyRows[static_cast<std::size_t>(row)] && !reservedRows[static_cast<std::size_t>(row)]) missingRows.push_back(row);
		if (missingRows.empty() && RendererState::allRowsReady(generation)) {
			std::shared_ptr<RendererState::RenderCache> cache = std::make_shared<RendererState::RenderCache>();
			cache->braille = generation.braille;
			cache->rowCount = generation.rowCount;
			cache->bodyWidth = generation.bodyWidth;
			cache->documentId = generation.documentId;
			cache->documentVersion = generation.version;
			cache->totalLines = generation.totalLines;
			cache->windowStartLine = generation.windowStartLine;
			cache->windowLineCount = generation.windowLineCount;
			cache->viewportWidth = generation.viewportWidth;
			cache->rowPatterns = std::move(generation.rowPatterns);
			cache->rowLineStarts = std::move(generation.rowLineStarts);
			cache->rowLineEnds = std::move(generation.rowLineEnds);
			mState->rememberCache(cache);
			if (mState->activateRequestedCache(cache)) signals.redraw = true;
		}
		const std::size_t allowedCores = std::max<std::size_t>(1, mr::coprocessor::globalCoprocessor().allowedCoreCount());
		const int packetCount = std::min(static_cast<int>(missingRows.size()), static_cast<int>(std::min<std::size_t>(allowedCores, static_cast<std::size_t>(INT_MAX))));
		const int baseRows = packetCount == 0 ? 0 : static_cast<int>(missingRows.size()) / packetCount;
		const int extraRows = packetCount == 0 ? 0 : static_cast<int>(missingRows.size()) % packetCount;
		int nextMissingRow = 0;
		bool submissionFailed = false;
		for (int packetIndex = 0; packetIndex < packetCount; ++packetIndex) {
			RendererState::PatternPacketState packet;
			const int missingRowCount = baseRows + (packetIndex < extraRows ? 1 : 0);
			packet.rowStart = missingRows[static_cast<std::size_t>(nextMissingRow)];
			packet.rowEnd = missingRows[static_cast<std::size_t>(nextMissingRow + missingRowCount - 1)] + 1;
			packet.lineStart = generation.windowStartLine + static_cast<std::size_t>(packet.rowStart) * samplesPerRow;
			packet.lineEnd = std::min(generation.windowStartLine + generation.windowLineCount,
			                          generation.windowStartLine + static_cast<std::size_t>(packet.rowEnd) * samplesPerRow);
			nextMissingRow += missingRowCount;
			const mr::coprocessor::WorkDirection direction = packet.lineEnd <= topLine ? mr::coprocessor::WorkDirection::Bof : mr::coprocessor::WorkDirection::Eof;
			const std::string label = "mini map pattern lines " + std::to_string(packet.lineStart + 1) + "-" + std::to_string(packet.lineEnd);
			const std::uint64_t generationId = generation.generation;
			const int packetRowStart = packet.rowStart;
			const int packetRowEnd = packet.rowEnd;
			const std::size_t packetLineStart = packet.lineStart;
			const std::size_t packetLineEnd = packet.lineEnd;
			const std::size_t windowStartLine = generation.windowStartLine;
			const std::size_t windowLineCount = generation.windowLineCount;
			const int generationRowCount = generation.rowCount;
			packet.taskId = mr::coprocessor::globalCoprocessor().submitPacket(
			    mr::coprocessor::Lane::MiniMap, mr::coprocessor::TaskKind::MiniMapWarmup, documentId, version,
			    executionOwnerKind, executionOwnerLocalId, generationId, direction, packetLineStart, packetLineEnd, label,
			    [snapshot, settings, generationId, generationRowCount, bodyWidth, viewportWidth, useBraille, totalLines, topLine, packetRowStart, packetRowEnd,
			     packetLineStart, packetLineEnd, windowStartLine, windowLineCount](const mr::coprocessor::TaskInfo &info) {
				    mr::coprocessor::Result packetResult;
				    struct MiniMapLineSample {
					    std::uint64_t dotColumnBits = 0;
				    };
				    packetResult.task = info;
				    if (info.cancelRequested()) {
					    packetResult.status = mr::coprocessor::TaskStatus::Cancelled;
					    return packetResult;
				    }
				    const int packetRows = packetRowEnd - packetRowStart;
				    const int dotCols = useBraille ? std::max(1, bodyWidth * 2) : std::max(1, bodyWidth);
				    const std::size_t packetLineCount = packetLineEnd - packetLineStart;
				    std::vector<std::string> lineTexts = buildViewportAnchoredLineTexts(snapshot, packetLineStart, packetLineCount, topLine);
				    std::vector<unsigned char> rowPatterns(static_cast<std::size_t>(packetRows) * static_cast<std::size_t>(bodyWidth), 0);
				    std::vector<std::size_t> rowLineStarts(static_cast<std::size_t>(packetRows), packetLineEnd);
				    std::vector<std::size_t> rowLineEnds(static_cast<std::size_t>(packetRows), packetLineEnd);
				    for (int localRow = 0; localRow < packetRows; ++localRow) {
					    if (info.cancelRequested()) {
						    packetResult.status = mr::coprocessor::TaskStatus::Cancelled;
						    return packetResult;
					    }
					    const int documentRow = packetRowStart + localRow;
					    const std::size_t rowSampleStart = (useBraille ? 4 : 1) * static_cast<std::size_t>(documentRow);
					    const std::size_t rowSampleCount = useBraille ? 4 : 1;
					    rowLineStarts[static_cast<std::size_t>(localRow)] = std::min(totalLines, windowStartLine + rowSampleStart);
					    rowLineEnds[static_cast<std::size_t>(localRow)] =
					        std::min(totalLines, windowStartLine + std::min(windowLineCount, rowSampleStart + rowSampleCount));
					    MiniMapLineSample samples[4];
					    for (std::size_t py = 0; py < rowSampleCount; ++py) {
						    const std::size_t lineIndex = windowStartLine + rowSampleStart + py;
						    if (lineIndex < packetLineStart || lineIndex >= packetLineEnd) continue;
						    const std::string &lineText = lineTexts[lineIndex - packetLineStart];
						    std::size_t textIndex = 0;
						    int visualColumn = 0;
						    while (textIndex < lineText.size()) {
							    const std::size_t current = textIndex;
							    std::size_t next = textIndex;
							    std::size_t width = 0;
							    if (!nextDisplayChar(TStringView(lineText.data(), lineText.size()), next, width, visualColumn, settings)) break;
							    if (std::isspace(static_cast<unsigned char>(lineText[current])) == 0) {
								    const int dotStart = static_cast<int>(static_cast<long long>(visualColumn) * dotCols / viewportWidth);
								    const int dotEnd = static_cast<int>((static_cast<long long>(visualColumn + static_cast<int>(width)) * dotCols - 1) / viewportWidth);
								    for (int dot = std::max(0, dotStart); dot <= std::min(63, dotEnd); ++dot)
									    samples[py].dotColumnBits |= 1ULL << dot;
							    }
							    visualColumn += static_cast<int>(width);
							    textIndex = next;
						    }
					    }
					    for (int x = 0; x < bodyWidth; ++x) {
						    unsigned char pattern = 0;
						    if (useBraille) {
							    static const unsigned char dotBits[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};
							    for (int py = 0; py < 4; ++py)
								    for (int px = 0; px < 2; ++px) {
									    const int dotColumn = x * 2 + px;
									    if (dotColumn < 64 && (samples[py].dotColumnBits & (1ULL << dotColumn)) != 0) pattern |= dotBits[py][px];
								    }
						    } else if (x < 64 && (samples[0].dotColumnBits & (1ULL << x)) != 0)
							    pattern = 1;
						    rowPatterns[static_cast<std::size_t>(localRow) * static_cast<std::size_t>(bodyWidth) + static_cast<std::size_t>(x)] = pattern;
					    }
				    }
				    packetResult.payload = std::make_shared<mr::coprocessor::MiniMapWarmupPayload>(
					        generationId, useBraille, generationRowCount, bodyWidth, packetRowStart, packetRowEnd, totalLines, windowStartLine, windowLineCount,
				        viewportWidth, std::move(rowPatterns), std::move(rowLineStarts), std::move(rowLineEnds));
				    packetResult.status = mr::coprocessor::TaskStatus::Completed;
				    return packetResult;
			    });
			if (packet.taskId == 0) {
				submissionFailed = true;
				break;
			}
			generation.packets.push_back(packet);
		}
		if (submissionFailed) {
			for (const RendererState::PatternPacketState &packet : generation.packets)
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(packet.taskId));
			if (!generation.packets.empty()) signals.notifyTaskStateChanged = true;
		} else if (!generation.packets.empty()) {
			mState->patternGenerations.push_back(std::move(generation));
			signals.notifyTaskStateChanged = true;
		}
	}

	const bool overlayReady = mState->activeOverlayDocumentId == documentId && mState->activeOverlayVersion == version &&
	                          mState->activeOverlayTotalLines == totalLines && mState->activeOverlayRevision == overlaySources.revision &&
	                          mState->activeOverlaySelectionStart == normalizedSelection.start && mState->activeOverlaySelectionEnd == normalizedSelection.end &&
	                          mState->activeOverlayViewportWidth == viewportWidth && mState->activeOverlayBodyWidth == bodyWidth &&
	                          mState->activeOverlayBraille == useBraille;
	bool overlayPending = false;
	for (const RendererState::OverlayGenerationState &generation : mState->overlayGenerations)
		if (generation.documentId == documentId && generation.version == version && generation.totalLines == totalLines &&
		    generation.sourceRevision == overlaySources.revision && generation.selectionStart == normalizedSelection.start && generation.selectionEnd == normalizedSelection.end &&
		    generation.viewportWidth == viewportWidth && generation.bodyWidth == bodyWidth && generation.braille == useBraille) {
			overlayPending = true;
			break;
		}
	if (!overlayReady && !overlayPending) {
		RendererState::OverlayGenerationState generation;
		generation.generation = mState->nextGeneration++;
		if (mState->nextGeneration == 0) ++mState->nextGeneration;
		generation.documentId = documentId;
		generation.version = version;
		generation.totalLines = totalLines;
		generation.sourceRevision = overlaySources.revision;
		generation.selectionStart = normalizedSelection.start;
		generation.selectionEnd = normalizedSelection.end;
		generation.viewportWidth = viewportWidth;
		generation.bodyWidth = bodyWidth;
		generation.braille = useBraille;
		const bool selectionOnlyChange = mState->activeOverlay != nullptr && mState->activeOverlayDocumentId == documentId &&
		                                 mState->activeOverlayVersion == version && mState->activeOverlayTotalLines == totalLines &&
		                                 mState->activeOverlayRevision == overlaySources.revision && mState->activeOverlayViewportWidth == viewportWidth &&
		                                 mState->activeOverlayBodyWidth == bodyWidth && mState->activeOverlayBraille == useBraille;
		if (selectionOnlyChange) generation.projection = *mState->activeOverlay;
		std::vector<unsigned int> components;
		if (selectionOnlyChange || !normalizedSelection.empty() || !overlaySources.findRanges->empty()) components.push_back(overlayFind);
		if (!selectionOnlyChange) {
			if (!overlaySources.dirtyRanges->empty()) components.push_back(overlayDirty);
			if (!overlaySources.errorRanges->empty()) components.push_back(overlayError);
			if (!overlaySources.warningRanges->empty()) components.push_back(overlayWarning);
			if (!overlaySources.fileCompareLineKinds->empty() || !overlaySources.fileCompareMiniMapSlices->empty()) components.push_back(overlayDiff);
		}
		if (components.empty()) {
			mState->activeOverlay = std::make_shared<const OverlayState>(generation.projection);
			mState->activeOverlayDocumentId = generation.documentId;
			mState->activeOverlayVersion = generation.version;
			mState->activeOverlayTotalLines = generation.totalLines;
			mState->activeOverlayRevision = generation.sourceRevision;
			mState->activeOverlaySelectionStart = generation.selectionStart;
			mState->activeOverlaySelectionEnd = generation.selectionEnd;
			mState->activeOverlayViewportWidth = generation.viewportWidth;
			mState->activeOverlayBodyWidth = generation.bodyWidth;
			mState->activeOverlayBraille = generation.braille;
			signals.redraw = true;
			return signals;
		}
		const std::size_t allowedCores = std::max<std::size_t>(1, mr::coprocessor::globalCoprocessor().allowedCoreCount());
		const std::size_t packetCount = std::min<std::size_t>(allowedCores, components.size());
		for (std::size_t packetIndex = 0; packetIndex < packetCount; ++packetIndex) {
			unsigned int componentMask = 0;
			for (std::size_t componentIndex = packetIndex; componentIndex < components.size(); componentIndex += packetCount)
				componentMask |= components[componentIndex];
			RendererState::OverlayPacketState packet;
			packet.componentMask = componentMask;
			const std::uint64_t generationId = generation.generation;
			const std::string label = "mini map overlay components " + std::to_string(componentMask);
			packet.taskId = mr::coprocessor::globalCoprocessor().submitPacket(
			    mr::coprocessor::Lane::MiniMap, mr::coprocessor::TaskKind::MiniMapWarmup, documentId, version,
			    executionOwnerKind, executionOwnerLocalId, generationId, mr::coprocessor::WorkDirection::Eof, 0, totalLines,
			    label, [snapshot, normalizedSelection, overlaySources, totalLines, viewportWidth, bodyWidth, useBraille, settings, generationId,
			            componentMask](const mr::coprocessor::TaskInfo &info) {
				    mr::coprocessor::Result overlayResult;
				    overlayResult.task = info;
				    if (info.cancelRequested()) {
					    overlayResult.status = mr::coprocessor::TaskStatus::Cancelled;
					    return overlayResult;
				    }
				    std::shared_ptr<const OverlayState> projection = computeOverlayState(snapshot, normalizedSelection, overlaySources, totalLines, viewportWidth,
				                                                                        bodyWidth, useBraille, settings, componentMask);
				    if (info.cancelRequested() || projection == nullptr) {
					    overlayResult.status = mr::coprocessor::TaskStatus::Cancelled;
					    return overlayResult;
				    }
				    overlayResult.payload = std::make_shared<MRMiniMapOverlayPacketPayload>(generationId, componentMask, std::move(projection));
				    overlayResult.status = mr::coprocessor::TaskStatus::Completed;
				    return overlayResult;
			    });
			if (packet.taskId != 0) generation.packets.push_back(packet);
		}
		if (!generation.packets.empty()) {
			mState->overlayGenerations.push_back(std::move(generation));
			signals.notifyTaskStateChanged = true;
		}
	}
	return signals;
}

const MRMiniMapRenderer::OverlayState &MRMiniMapRenderer::overlayProjection() const noexcept {
	static const OverlayState empty;
	return mState != nullptr && mState->activeOverlay != nullptr ? *mState->activeOverlay : empty;
}

void MRMiniMapRenderer::drawGutter(TDrawBuffer &buffer, int y, int miniMapRows, int viewWidth, const Viewport &viewport, std::size_t totalLines, std::size_t topLine, bool useBraille, const std::string &viewportMarkerGlyph, const Palette &palette, const OverlayState &overlay) const {
	if (mState == nullptr) return;
	if (viewport.bodyWidth <= 0 || viewport.bodyX < 0 || viewport.infoX < 0 || totalLines == 0 || miniMapRows <= 0) return;

	const std::array<std::string, 256> &glyphTable = brailleGlyphTable();
	const int bodyX = viewport.bodyX;
	const int bodyWidth = viewport.bodyWidth;
	const RendererState::SamplingWindow samplingWindow = RendererState::samplingWindowFor(totalLines, topLine, miniMapRows, useBraille);
	const bool cacheReady = mState->cacheReadyForViewport(viewport, miniMapRows, useBraille, samplingWindow, mState->requestedDocumentId, mState->requestedVersion);
	const std::shared_ptr<const RendererState::RenderCache> renderCache = mState->activeCache;
	const int renderRowOffset = cacheReady && renderCache != nullptr
	                                ? RendererState::cacheRowOffset(*renderCache, viewport, useBraille, samplingWindow, mState->requestedDocumentId, mState->requestedVersion)
	                                : mState->activeCacheRowOffset;
	const bool stalePatternCacheUsable =
	    !cacheReady && renderCache != nullptr && renderCache->documentId == mState->requestedDocumentId && renderCache->bodyWidth == bodyWidth &&
	    renderCache->viewportWidth == std::max(1, viewport.width) && renderCache->braille == useBraille && renderRowOffset >= 0 &&
	    !renderCache->rowPatterns.empty();

	if (y >= miniMapRows) {
		buffer.moveChar(static_cast<ushort>(bodyX), ' ', palette.normal, static_cast<ushort>(bodyWidth));
		if (viewport.separatorX >= 0 && viewport.separatorX < viewWidth) buffer.moveChar(static_cast<ushort>(viewport.separatorX), ' ', palette.normal, 1);
		buffer.moveChar(static_cast<ushort>(viewport.infoX), ' ', palette.normal, 1);
		return;
	}

	for (int x = 0; x < bodyWidth; ++x) {
		unsigned char pattern = 0;
		if (cacheReady || stalePatternCacheUsable) {
			const std::size_t cacheRow = static_cast<std::size_t>(renderRowOffset) + static_cast<std::size_t>(y);
			std::size_t index = cacheRow * static_cast<std::size_t>(bodyWidth) + static_cast<std::size_t>(x);
			if (renderCache != nullptr && index < renderCache->rowPatterns.size()) pattern = renderCache->rowPatterns[index];
		}
		bool cellFind = false;
		bool cellChanged = false;
		bool cellError = false;
		bool cellWarning = false;
		bool cellDiffEqual = false;
		bool cellDiffMissing = false;
		bool cellDiffInsert = false;
		bool cellDiffOffset = false;
		if (useBraille) {
			for (int py = 0; py < 4; ++py) {
				const std::size_t sampleOffset = static_cast<std::size_t>(y) * 4 + static_cast<std::size_t>(py);
				if (sampleOffset >= samplingWindow.lineCount) break;
				const std::size_t lineIndex = samplingWindow.startLine + sampleOffset;
				const std::uint64_t errorBits = RendererState::lineMaskBits(*overlay.errorLineMasks, lineIndex);
				const std::uint64_t warningBits = RendererState::lineMaskBits(*overlay.warningLineMasks, lineIndex);
				const std::uint64_t findBits = RendererState::lineMaskBits(*overlay.findLineMasks, lineIndex);
				const std::uint64_t dirtyBits = RendererState::lineMaskBits(*overlay.dirtyLineMasks, lineIndex);
				const std::uint64_t diffEqualBits = RendererState::lineMaskBits(*overlay.diffEqualLineMasks, lineIndex);
				const std::uint64_t diffMissingBits = RendererState::lineMaskBits(*overlay.diffMissingLineMasks, lineIndex);
				const std::uint64_t diffInsertBits = RendererState::lineMaskBits(*overlay.diffInsertLineMasks, lineIndex);
				const std::uint64_t diffOffsetBits = RendererState::lineMaskBits(*overlay.diffOffsetLineMasks, lineIndex);
				if (!cellError && miniMapCellHasOverlayBits(errorBits, x, true)) cellError = true;
				if (!cellWarning && miniMapCellHasOverlayBits(warningBits, x, true)) cellWarning = true;
				if (!cellFind && miniMapCellHasOverlayBits(findBits, x, true)) cellFind = true;
				if (!cellChanged && miniMapCellHasOverlayBits(dirtyBits, x, true)) cellChanged = true;
				if (!cellDiffEqual && miniMapCellHasOverlayBits(diffEqualBits, x, true)) cellDiffEqual = true;
				if (!cellDiffMissing && miniMapCellHasOverlayBits(diffMissingBits, x, true)) cellDiffMissing = true;
				if (!cellDiffInsert && miniMapCellHasOverlayBits(diffInsertBits, x, true)) cellDiffInsert = true;
				if (!cellDiffOffset && miniMapCellHasOverlayBits(diffOffsetBits, x, true)) cellDiffOffset = true;
			}
		} else {
			std::size_t lineIndex = samplingWindow.startLine + static_cast<std::size_t>(y);
			const std::uint64_t errorBits = RendererState::lineMaskBits(*overlay.errorLineMasks, lineIndex);
			const std::uint64_t warningBits = RendererState::lineMaskBits(*overlay.warningLineMasks, lineIndex);
			const std::uint64_t findBits = RendererState::lineMaskBits(*overlay.findLineMasks, lineIndex);
			const std::uint64_t dirtyBits = RendererState::lineMaskBits(*overlay.dirtyLineMasks, lineIndex);
			const std::uint64_t diffEqualBits = RendererState::lineMaskBits(*overlay.diffEqualLineMasks, lineIndex);
			const std::uint64_t diffMissingBits = RendererState::lineMaskBits(*overlay.diffMissingLineMasks, lineIndex);
			const std::uint64_t diffInsertBits = RendererState::lineMaskBits(*overlay.diffInsertLineMasks, lineIndex);
			const std::uint64_t diffOffsetBits = RendererState::lineMaskBits(*overlay.diffOffsetLineMasks, lineIndex);
			cellError = miniMapCellHasOverlayBits(errorBits, x, false);
			cellWarning = miniMapCellHasOverlayBits(warningBits, x, false);
			cellFind = miniMapCellHasOverlayBits(findBits, x, false);
			cellChanged = miniMapCellHasOverlayBits(dirtyBits, x, false);
			cellDiffEqual = miniMapCellHasOverlayBits(diffEqualBits, x, false);
			cellDiffMissing = miniMapCellHasOverlayBits(diffMissingBits, x, false);
			cellDiffInsert = miniMapCellHasOverlayBits(diffInsertBits, x, false);
			cellDiffOffset = miniMapCellHasOverlayBits(diffOffsetBits, x, false);
		}
		TColorAttr rowPriorityColor = palette.normal;
		if (cellError) rowPriorityColor = palette.errorMarker;
		else if (cellWarning)
			rowPriorityColor = palette.warningMarker;
		else if (cellDiffMissing)
			rowPriorityColor = palette.diffMissing;
		else if (cellDiffInsert)
			rowPriorityColor = palette.diffInsert;
		else if (cellDiffOffset)
			rowPriorityColor = palette.diffOffset;
		else if (cellDiffEqual)
			rowPriorityColor = palette.diffEqual;
		else if (cellFind)
			rowPriorityColor = palette.findMarker;
		else if (cellChanged)
			rowPriorityColor = palette.changed;
		const bool cellOverlayActive = cellError || cellWarning || cellDiffMissing || cellDiffInsert || cellDiffOffset || cellDiffEqual || cellFind || cellChanged;
		TColorAttr cellColor = (pattern != 0 || cellOverlayActive) ? rowPriorityColor : palette.normal;
		if (useBraille) buffer.moveStr(static_cast<ushort>(bodyX + x), glyphTable[pattern], cellColor, 1);
		else if (pattern != 0)
			buffer.moveStr(static_cast<ushort>(bodyX + x), "\xE2\x96\x88", cellColor, 1);
		else
			buffer.moveChar(static_cast<ushort>(bodyX + x), ' ', cellColor, 1);
	}

	if (viewport.separatorX >= 0 && viewport.separatorX < viewWidth) buffer.moveChar(static_cast<ushort>(viewport.separatorX), ' ', palette.normal, 1);

	const std::size_t clampedTopLine = std::min(topLine, totalLines - 1);
	const std::size_t visibleLines = static_cast<std::size_t>(std::max(miniMapRows, 1));
	const std::size_t viewportLineEnd = std::min(totalLines, clampedTopLine + visibleLines);
	const std::size_t markerRowCount = static_cast<std::size_t>(std::max(miniMapRows, 1));
	const std::size_t markerWindowStart = samplingWindow.startLine;
	const std::size_t markerWindowLineCount = std::max<std::size_t>(1, samplingWindow.lineCount);
	const std::size_t markerWindowEnd = std::min(totalLines, markerWindowStart + markerWindowLineCount);
	const std::size_t markerViewportStart = std::min(std::max(clampedTopLine, markerWindowStart), markerWindowEnd);
	const std::size_t markerViewportEnd = std::min(std::max(viewportLineEnd, markerViewportStart), markerWindowEnd);
	const std::size_t markerSamplesPerRow = useBraille ? 4 : 1;
	std::size_t markerStart = (markerViewportStart - markerWindowStart) / markerSamplesPerRow;
	std::size_t markerEnd = ((markerViewportEnd - markerWindowStart) + markerSamplesPerRow - 1) / markerSamplesPerRow;
	const std::size_t minMarkerRows = std::min<std::size_t>(3, markerRowCount);

	if (markerStart >= markerRowCount) markerStart = markerRowCount - 1;
	if (markerEnd <= markerStart) markerEnd = markerStart + 1;
	if (markerEnd - markerStart < minMarkerRows) {
		const std::size_t grow = minMarkerRows - (markerEnd - markerStart);
		const std::size_t growDown = std::min(grow, markerRowCount - markerEnd);
		markerEnd += growDown;
		markerStart -= grow - growDown > markerStart ? markerStart : grow - growDown;
	}
	if (markerEnd > markerRowCount) markerEnd = markerRowCount;
	const bool markerVisible = static_cast<std::size_t>(y) >= markerStart && static_cast<std::size_t>(y) < markerEnd;
	if (markerVisible) buffer.moveStr(static_cast<ushort>(viewport.infoX), viewportMarkerGlyph, palette.viewport, 1);
	else
		buffer.moveChar(static_cast<ushort>(viewport.infoX), ' ', palette.normal, 1);
}
