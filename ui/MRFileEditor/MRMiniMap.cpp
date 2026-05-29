#include "MRMiniMap.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <utility>

namespace {

static constexpr auto kLargeFileViewportWarmupDebounce = std::chrono::milliseconds(180);

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

bool ratioCellInRange(int from, int to, int viewportWidth, int cellIndex, int cellCount) noexcept {
	if (from < 0 || to <= 0 || from >= to || viewportWidth <= 0 || cellCount <= 0) return false;
	long long cellLeft = static_cast<long long>(cellIndex) * viewportWidth;
	long long cellRight = static_cast<long long>(cellIndex + 1) * viewportWidth;
	long long cLeft = static_cast<long long>(from) * cellCount;
	long long cRight = static_cast<long long>(to) * cellCount;
	return cellRight > cLeft && cellLeft < cRight;
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

struct MRMiniMapRenderer::Impl {
	struct RenderCache {
		bool valid = false;
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

	struct SamplingWindow {
		std::size_t startLine = 0;
		std::size_t lineCount = 1;
	};

	std::uint64_t warmupTaskId = 0;
	std::size_t warmupDocumentId = 0;
	std::size_t warmupVersion = 0;
	std::size_t warmupTopLine = 0;
	int warmupRows = 0;
	int warmupBodyWidth = 0;
	int warmupViewportWidth = 0;
	bool warmupBraille = true;
	std::size_t warmupWindowStartLine = 0;
	std::size_t warmupWindowLineCount = 0;
	std::size_t lastWarmupScheduledWindowStartLine = 0;
	std::size_t lastWarmupScheduledWindowLineCount = 0;
	std::chrono::steady_clock::time_point lastWarmupScheduledAt;
	RenderCache cache;

	static bool hasProjectionFor(const RenderCache &cache, int rowCount, int bodyWidth) noexcept {
		return cache.bodyWidth == bodyWidth && cache.rowCount == rowCount && !cache.rowPatterns.empty();
	}

	static void normalizeLineMasks(std::vector<OverlayState::LineMask> &masks) {
		if (masks.empty()) return;
		std::sort(masks.begin(), masks.end(), [](const OverlayState::LineMask &lhs, const OverlayState::LineMask &rhs) { return lhs.lineIndex < rhs.lineIndex; });
		std::size_t writeIndex = 0;
		for (std::size_t readIndex = 1; readIndex < masks.size(); ++readIndex) {
			if (masks[writeIndex].lineIndex == masks[readIndex].lineIndex) masks[writeIndex].dotColumnBits |= masks[readIndex].dotColumnBits;
			else
				masks[++writeIndex] = masks[readIndex];
		}
		masks.resize(writeIndex + 1);
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
			window.startLine = centeredStart + normalizedRowCount >= normalizedTotalLines ? normalizedTotalLines - normalizedRowCount : centeredStart;
		}
		return window;
	}

	static bool pendingWindowStillUseful(std::size_t pendingTopLine, const SamplingWindow &pendingWindow, std::size_t requestedTopLine, int requestedRows) noexcept {
		const std::size_t visibleRows = static_cast<std::size_t>(std::max(requestedRows, 1));
		const std::size_t pendingWindowLineCount = std::max<std::size_t>(1, pendingWindow.lineCount);
		const std::size_t pendingWindowEnd = pendingWindow.startLine + pendingWindowLineCount;
		const std::size_t requestedBottomLine = requestedTopLine + visibleRows;
		if (requestedTopLine < pendingWindow.startLine || requestedBottomLine > pendingWindowEnd) return false;

		const std::size_t focusDelta = pendingTopLine > requestedTopLine ? pendingTopLine - requestedTopLine : requestedTopLine - pendingTopLine;
		if (focusDelta <= visibleRows) return true;

		const std::size_t guardBand = std::max<std::size_t>(visibleRows, pendingWindowLineCount / 5);
		const std::size_t effectiveGuard = std::min(guardBand, pendingWindowLineCount);
		const std::size_t preferredStart = pendingWindow.startLine + effectiveGuard;
		const std::size_t preferredEnd = pendingWindowEnd > effectiveGuard ? pendingWindowEnd - effectiveGuard : pendingWindow.startLine;
		return preferredStart < preferredEnd && requestedTopLine >= preferredStart && requestedBottomLine <= preferredEnd;
	}

	bool cacheReadyForViewport(const Viewport &viewport, int rowCount, bool braille, const SamplingWindow &window, std::size_t documentId, std::size_t version) const noexcept {
		return cache.valid && cache.documentId == documentId && cache.documentVersion == version && cache.rowCount == rowCount && cache.bodyWidth == viewport.bodyWidth && cache.viewportWidth == std::max(1, viewport.width) && cache.braille == braille && cache.windowStartLine == window.startLine && cache.windowLineCount == std::max<std::size_t>(1, window.lineCount);
	}

	Signals clearWarmupTask(std::uint64_t expectedTaskId) noexcept {
		Signals signals;

		if (expectedTaskId != 0 && warmupTaskId != expectedTaskId) return signals;
		if (warmupTaskId == 0) return signals;
		warmupTaskId = 0;
		warmupDocumentId = 0;
		warmupVersion = 0;
		warmupTopLine = 0;
		warmupRows = 0;
		warmupBodyWidth = 0;
		warmupViewportWidth = 0;
		warmupBraille = true;
		warmupWindowStartLine = 0;
		warmupWindowLineCount = 0;
		signals.notifyTaskStateChanged = true;
		return signals;
	}

	Signals invalidate(bool cancelTask, std::size_t documentId) noexcept {
		Signals signals;
		const bool keepStaleCache = !cancelTask && cache.documentId == documentId && cache.bodyWidth > 0 && cache.rowCount > 0;

		cache.valid = false;
		if (!keepStaleCache) {
			cache.rowPatterns.clear();
			cache.rowLineStarts.clear();
			cache.rowLineEnds.clear();
		}
		if (cancelTask && warmupTaskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(warmupTaskId));
			signals.merge(clearWarmupTask(warmupTaskId));
		}
		return signals;
	}
};

MRMiniMapRenderer::MRMiniMapRenderer() noexcept : mImpl(std::make_unique<Impl>()) {
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
	return mImpl != nullptr ? mImpl->warmupTaskId : 0;
}

bool MRMiniMapRenderer::hasProjection(int rowCount, int bodyWidth) const noexcept {
	return mImpl != nullptr && Impl::hasProjectionFor(mImpl->cache, rowCount, bodyWidth);
}

MRMiniMapRenderer::Signals MRMiniMapRenderer::clearWarmupTask(std::uint64_t expectedTaskId) noexcept {
	return mImpl != nullptr ? mImpl->clearWarmupTask(expectedTaskId) : Signals();
}

MRMiniMapRenderer::Signals MRMiniMapRenderer::invalidate(bool cancelTask, std::size_t documentId) noexcept {
	return mImpl != nullptr ? mImpl->invalidate(cancelTask, documentId) : Signals();
}

MRMiniMapRenderer::ApplyWarmupResult MRMiniMapRenderer::applyWarmup(const mr::coprocessor::MiniMapWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId, std::size_t documentId, std::size_t version) noexcept {
	ApplyWarmupResult result;

	if (mImpl == nullptr) return result;
	if (documentId != mImpl->warmupDocumentId || version != expectedVersion) return result;
	const bool matchingPendingTask = expectedTaskId != 0 && mImpl->warmupTaskId == expectedTaskId;
	const bool missingVisibleProjection =
	    !mImpl->cache.valid || mImpl->cache.documentId != documentId || mImpl->cache.documentVersion != version || mImpl->cache.bodyWidth <= 0 || mImpl->cache.rowCount <= 0 || mImpl->cache.rowPatterns.empty();
	if (!matchingPendingTask && !missingVisibleProjection) return result;
	const bool visualChanged = !mImpl->cache.valid || mImpl->cache.braille != payload.braille || mImpl->cache.rowCount != payload.rowCount || mImpl->cache.bodyWidth != payload.bodyWidth ||
	                           mImpl->cache.totalLines != std::max<std::size_t>(1, payload.totalLines) || mImpl->cache.windowStartLine != payload.windowStartLine ||
	                           mImpl->cache.windowLineCount != std::max<std::size_t>(1, payload.windowLineCount) || mImpl->cache.viewportWidth != std::max(1, payload.viewportWidth) ||
	                           mImpl->cache.rowPatterns != payload.rowPatterns || mImpl->cache.rowLineStarts != payload.rowLineStarts || mImpl->cache.rowLineEnds != payload.rowLineEnds;
	mImpl->cache.valid = true;
	mImpl->cache.braille = payload.braille;
	mImpl->cache.rowCount = payload.rowCount;
	mImpl->cache.bodyWidth = payload.bodyWidth;
	mImpl->cache.documentId = documentId;
	mImpl->cache.documentVersion = version;
	mImpl->cache.totalLines = std::max<std::size_t>(1, payload.totalLines);
	mImpl->cache.windowStartLine = payload.windowStartLine;
	mImpl->cache.windowLineCount = std::max<std::size_t>(1, payload.windowLineCount);
	mImpl->cache.viewportWidth = std::max(1, payload.viewportWidth);
	mImpl->cache.rowPatterns = payload.rowPatterns;
	mImpl->cache.rowLineStarts = payload.rowLineStarts;
	mImpl->cache.rowLineEnds = payload.rowLineEnds;
	if (matchingPendingTask) result.signals.merge(mImpl->clearWarmupTask(expectedTaskId));
	result.signals.redraw = visualChanged;
	result.applied = true;
	return result;
}

MRMiniMapRenderer::Signals MRMiniMapRenderer::scheduleWarmupIfNeeded(const Viewport &viewport, int rowCount, bool useBraille, std::size_t totalLinesHint, std::size_t topLine, std::size_t documentId, std::size_t version,
                                                                     const mr::editor::ReadSnapshot &snapshot, const MREditSetupSettings &settings, bool preservePendingTaskForSameDocument) {
	Signals signals;

	if (mImpl == nullptr) return signals;
	if (viewport.bodyWidth <= 0 || rowCount <= 0) return invalidate(true, documentId);
	std::size_t totalLines = std::max<std::size_t>(1, totalLinesHint);
	if (snapshot.exactLineCountKnown()) totalLines = std::max<std::size_t>(1, snapshot.lineCount());
	const Impl::SamplingWindow samplingWindow = Impl::samplingWindowFor(totalLines, topLine, rowCount, useBraille);
	if (mImpl->cacheReadyForViewport(viewport, rowCount, useBraille, samplingWindow, documentId, version)) return signals;
	const int bodyWidth = viewport.bodyWidth;
	const int viewportWidth = std::max(1, viewport.width);
	const bool haveProjection = Impl::hasProjectionFor(mImpl->cache, rowCount, bodyWidth);
	if (preservePendingTaskForSameDocument && mImpl->warmupTaskId == 0 && mImpl->warmupDocumentId == documentId && mImpl->warmupVersion == version && mImpl->warmupRows == rowCount && mImpl->warmupBodyWidth == bodyWidth &&
	    mImpl->warmupViewportWidth == viewportWidth && mImpl->warmupBraille == useBraille && mImpl->lastWarmupScheduledWindowStartLine == samplingWindow.startLine &&
	    mImpl->lastWarmupScheduledWindowLineCount == samplingWindow.lineCount && mImpl->lastWarmupScheduledAt != std::chrono::steady_clock::time_point() &&
	    std::chrono::steady_clock::now() - mImpl->lastWarmupScheduledAt < kLargeFileViewportWarmupDebounce)
		return signals;
	if (mImpl->warmupTaskId != 0) {
		if (mImpl->warmupDocumentId == documentId && mImpl->warmupVersion == version && mImpl->warmupRows == rowCount && mImpl->warmupBodyWidth == bodyWidth && mImpl->warmupViewportWidth == viewportWidth && mImpl->warmupBraille == useBraille && mImpl->warmupWindowStartLine == samplingWindow.startLine && mImpl->warmupWindowLineCount == samplingWindow.lineCount) return signals;
		if (preservePendingTaskForSameDocument && mImpl->warmupDocumentId == documentId && mImpl->warmupVersion == version) {
			const Impl::SamplingWindow pendingWindow = {mImpl->warmupWindowStartLine, std::max<std::size_t>(1, mImpl->warmupWindowLineCount)};
			if (Impl::pendingWindowStillUseful(mImpl->warmupTopLine, pendingWindow, topLine, rowCount) && haveProjection) return signals;
		}
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mImpl->warmupTaskId));
		signals.merge(mImpl->clearWarmupTask(mImpl->warmupTaskId));
	}

	std::uint64_t previousTaskId = mImpl->warmupTaskId;
	mImpl->warmupDocumentId = documentId;
	mImpl->warmupVersion = version;
	mImpl->warmupTopLine = topLine;
	mImpl->warmupRows = rowCount;
	mImpl->warmupBodyWidth = bodyWidth;
	mImpl->warmupViewportWidth = viewportWidth;
	mImpl->warmupBraille = useBraille;
	mImpl->warmupWindowStartLine = samplingWindow.startLine;
	mImpl->warmupWindowLineCount = samplingWindow.lineCount;
	mImpl->lastWarmupScheduledWindowStartLine = samplingWindow.startLine;
	mImpl->lastWarmupScheduledWindowLineCount = samplingWindow.lineCount;
	mImpl->lastWarmupScheduledAt = std::chrono::steady_clock::now();
	std::string miniMapTaskLabel = "rendering mini map lines " + std::to_string(samplingWindow.startLine + 1) + "-" + std::to_string(samplingWindow.startLine + samplingWindow.lineCount);
	mImpl->warmupTaskId = mr::coprocessor::globalCoprocessor().submit(
	    mr::coprocessor::Lane::MiniMap, mr::coprocessor::TaskKind::MiniMapWarmup, documentId, version, miniMapTaskLabel,
	    [snapshot, rowCount, bodyWidth, viewportWidth, useBraille, settings, totalLines, samplingWindow, topLine](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
		mr::coprocessor::Result result;
		struct MiniMapLineSample {
			std::uint64_t dotColumnBits = 0;
		};
		std::vector<unsigned char> rowPatterns;
		std::vector<std::size_t> rowLineStarts;
		std::vector<std::size_t> rowLineEnds;
		const int dotCols = useBraille ? std::max(1, bodyWidth * 2) : std::max(1, bodyWidth);
		const std::size_t windowStartLine = samplingWindow.startLine;
		const std::size_t windowLineCount = std::max<std::size_t>(1, samplingWindow.lineCount);
		std::size_t normalizedTotalLines = std::max<std::size_t>(1, totalLines);
		auto shouldStop = [&]() noexcept { return stopToken.stop_requested() || info.cancelRequested(); };
		result.task = info;

		if (shouldStop()) {
			result.status = mr::coprocessor::TaskStatus::Cancelled;
			return result;
		}
		if (snapshot.exactLineCountKnown()) normalizedTotalLines = std::max<std::size_t>(1, snapshot.lineCount());
		std::vector<std::string> windowLineTexts = buildViewportAnchoredLineTexts(snapshot, windowStartLine, windowLineCount, topLine);
		if (shouldStop()) {
			result.status = mr::coprocessor::TaskStatus::Cancelled;
			return result;
		}
		rowPatterns.assign(static_cast<std::size_t>(std::max(0, rowCount) * std::max(0, bodyWidth)), 0);
		rowLineStarts.assign(static_cast<std::size_t>(std::max(0, rowCount)), 0);
		rowLineEnds.assign(static_cast<std::size_t>(std::max(0, rowCount)), 0);
		auto renderRows = [&](int yStart, int yEnd) -> bool {
			std::map<std::size_t, MiniMapLineSample> sampledLineSamples;
			auto lineSampleAt = [&](std::size_t lineIndex) -> const MiniMapLineSample & {
				auto cached = sampledLineSamples.find(lineIndex);
				if (cached != sampledLineSamples.end()) return cached->second;
				MiniMapLineSample sample;
				if (lineIndex < normalizedTotalLines && lineIndex >= windowStartLine) {
					const std::size_t localLineIndex = lineIndex - windowStartLine;
					if (localLineIndex < windowLineTexts.size()) {
						const std::string &lineText = windowLineTexts[localLineIndex];
						std::size_t index = 0;
						int visualColumn = 0;
						while (index < lineText.size()) {
							std::size_t current = index;
							std::size_t next = index;
							std::size_t width = 0;
							if (!nextDisplayChar(lineText, next, width, visualColumn, settings)) break;
							unsigned char ch = static_cast<unsigned char>(lineText[current]);
							if (std::isspace(ch) == 0) {
								const long long c = static_cast<long long>(visualColumn);
								const long long w = static_cast<long long>(width);
								const long long n = static_cast<long long>(dotCols);
								const long long v = static_cast<long long>(viewportWidth);
								const int dotColStart = static_cast<int>(c * n / v);
								const int dotColEnd = static_cast<int>(((c + w) * n - 1) / v);
								for (int dc = std::max(0, dotColStart); dc <= std::min(63, dotColEnd); ++dc)
									sample.dotColumnBits |= (1ULL << dc);
							}
							visualColumn += static_cast<int>(width);
							index = next;
						}
					}
				}
				auto inserted = sampledLineSamples.insert(std::make_pair(lineIndex, sample));
				return inserted.first->second;
			};

			for (int y = yStart; y < yEnd; ++y) {
				if (shouldStop()) return false;
				const std::size_t rowSampleStart = useBraille ? static_cast<std::size_t>(y) * 4 : static_cast<std::size_t>(y);
				const std::size_t rowSampleCount = useBraille ? 4 : 1;
				if (rowSampleStart < windowLineCount) {
					rowLineStarts[static_cast<std::size_t>(y)] = std::min(normalizedTotalLines, windowStartLine + rowSampleStart);
					rowLineEnds[static_cast<std::size_t>(y)] = std::min(normalizedTotalLines, windowStartLine + std::min(windowLineCount, rowSampleStart + rowSampleCount));
				} else {
					rowLineStarts[static_cast<std::size_t>(y)] = std::min(normalizedTotalLines, windowStartLine + windowLineCount);
					rowLineEnds[static_cast<std::size_t>(y)] = rowLineStarts[static_cast<std::size_t>(y)];
				}
				for (int x = 0; x < bodyWidth; ++x) {
					unsigned char pattern = 0;
					if (useBraille) {
						static const unsigned char dotBits[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};
						for (int py = 0; py < 4; ++py) {
							const std::size_t sampleOffset = static_cast<std::size_t>(y) * 4 + static_cast<std::size_t>(py);
							if (sampleOffset < windowLineCount) {
								const std::size_t lineIndex = windowStartLine + sampleOffset;
								const MiniMapLineSample &sample = lineSampleAt(lineIndex);
								for (int px = 0; px < 2; ++px) {
									const int dotColumn = x * 2 + px;
									if (dotColumn < 64 && (sample.dotColumnBits & (1ULL << dotColumn)) != 0) pattern |= dotBits[py][px];
								}
							}
						}
					} else if (static_cast<std::size_t>(y) < windowLineCount) {
						std::size_t lineIndex = windowStartLine + static_cast<std::size_t>(y);
						const MiniMapLineSample &sample = lineSampleAt(lineIndex);
						if (x < 64 && (sample.dotColumnBits & (1ULL << x)) != 0) pattern = 1;
					}
					rowPatterns[static_cast<std::size_t>(y * bodyWidth + x)] = pattern;
				}
			}
			return true;
		};
		if (!renderRows(0, rowCount)) {
			result.status = mr::coprocessor::TaskStatus::Cancelled;
			return result;
		}
		result.status = mr::coprocessor::TaskStatus::Completed;
		result.payload = std::make_shared<mr::coprocessor::MiniMapWarmupPayload>(useBraille, rowCount, bodyWidth, normalizedTotalLines, windowStartLine, windowLineCount, viewportWidth, std::move(rowPatterns), std::move(rowLineStarts), std::move(rowLineEnds));
		return result;
	});
	if (mImpl->warmupTaskId != previousTaskId) signals.notifyTaskStateChanged = true;
	return signals;
}

MRMiniMapRenderer::OverlayState MRMiniMapRenderer::computeOverlayState(const mr::editor::ReadSnapshot &snapshot, const mr::editor::Range &selection, const std::vector<mr::editor::Range> &findRanges, const std::vector<mr::editor::Range> &dirtyRanges, const std::vector<mr::editor::Range> &errorRanges, const std::vector<mr::editor::Range> &warningRanges, std::size_t totalLines, int viewportWidth, int miniMapBodyWidth, bool useBraille, const MREditSetupSettings &settings) const {
	OverlayState overlay;
	const int dotColumns = useBraille ? std::max(1, miniMapBodyWidth * 2) : std::max(1, miniMapBodyWidth);
	const int normalizedViewportWidth = std::max(1, viewportWidth);
	const std::size_t length = snapshot.length();

	if (length == 0 || totalLines == 0 || miniMapBodyWidth <= 0) return overlay;

	auto rangeMaskForLineSlice = [&](std::string_view lineText, std::size_t sliceStart, std::size_t sliceEnd) {
		if (sliceEnd <= sliceStart || sliceStart >= lineText.size() || dotColumns <= 0) return std::uint64_t(0);
		sliceEnd = std::min(sliceEnd, lineText.size());
		std::size_t index = 0;
		int visualColumn = 0;
		std::uint64_t mask = 0;

		while (index < lineText.size()) {
			std::size_t current = index;
			std::size_t next = index;
			std::size_t width = 0;
			if (!nextDisplayChar(TStringView(lineText.data(), lineText.size()), next, width, visualColumn, settings)) break;
			if (next > sliceStart && current < sliceEnd) {
				const int from = visualColumn;
				const int to = visualColumn + static_cast<int>(width);
				for (int dotColumn = 0; dotColumn < std::min(dotColumns, 64); ++dotColumn)
					if (ratioCellInRange(from, to, normalizedViewportWidth, dotColumn, dotColumns)) mask |= (1ULL << dotColumn);
			}
			visualColumn += static_cast<int>(width);
			index = next;
		}
		return mask;
	};

	auto appendRangeMasks = [&](std::vector<OverlayState::LineMask> &target, mr::editor::Range range, bool markTouchedBlankLines) {
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

	if (selection.end > selection.start) appendRangeMasks(overlay.findLineMasks, selection.normalized(), false);
	for (const mr::editor::Range &range : findRanges)
		appendRangeMasks(overlay.findLineMasks, range, false);
	for (const mr::editor::Range &range : dirtyRanges)
		appendRangeMasks(overlay.dirtyLineMasks, range, true);
	for (const mr::editor::Range &range : errorRanges)
		appendRangeMasks(overlay.errorLineMasks, range, true);
	for (const mr::editor::Range &range : warningRanges)
		appendRangeMasks(overlay.warningLineMasks, range, true);
	Impl::normalizeLineMasks(overlay.errorLineMasks);
	Impl::normalizeLineMasks(overlay.warningLineMasks);
	Impl::normalizeLineMasks(overlay.findLineMasks);
	Impl::normalizeLineMasks(overlay.dirtyLineMasks);
	return overlay;
}

void MRMiniMapRenderer::drawGutter(TDrawBuffer &buffer, int y, int miniMapRows, int viewWidth, const Viewport &viewport, std::size_t totalLines, std::size_t topLine, bool useBraille, const std::string &viewportMarkerGlyph, const Palette &palette, const OverlayState &overlay) const {
	if (mImpl == nullptr) return;
	if (viewport.bodyWidth <= 0 || viewport.bodyX < 0 || viewport.infoX < 0 || totalLines == 0 || miniMapRows <= 0) return;

	const std::array<std::string, 256> &glyphTable = brailleGlyphTable();
	const int bodyX = viewport.bodyX;
	const int bodyWidth = viewport.bodyWidth;
	const Impl::SamplingWindow samplingWindow = Impl::samplingWindowFor(totalLines, topLine, miniMapRows, useBraille);
	const bool cacheReady = mImpl->cacheReadyForViewport(viewport, miniMapRows, useBraille, samplingWindow, mImpl->cache.documentId, mImpl->cache.documentVersion);
	const bool stalePatternCacheUsable =
	    !cacheReady && mImpl->cache.documentId != 0 && mImpl->cache.bodyWidth == bodyWidth && mImpl->cache.rowCount == miniMapRows && mImpl->cache.viewportWidth == std::max(1, viewport.width) &&
	    mImpl->cache.braille == useBraille && mImpl->cache.windowStartLine == samplingWindow.startLine && mImpl->cache.windowLineCount == std::max<std::size_t>(1, samplingWindow.lineCount) &&
	    !mImpl->cache.rowPatterns.empty();

	if (y >= miniMapRows) {
		buffer.moveChar(static_cast<ushort>(bodyX), ' ', palette.normal, static_cast<ushort>(bodyWidth));
		if (viewport.separatorX >= 0 && viewport.separatorX < viewWidth) buffer.moveChar(static_cast<ushort>(viewport.separatorX), ' ', palette.normal, 1);
		buffer.moveChar(static_cast<ushort>(viewport.infoX), ' ', palette.normal, 1);
		return;
	}

	for (int x = 0; x < bodyWidth; ++x) {
		unsigned char pattern = 0;
		if (cacheReady || stalePatternCacheUsable) {
			std::size_t index = static_cast<std::size_t>(y * bodyWidth + x);
			if (index < mImpl->cache.rowPatterns.size()) pattern = mImpl->cache.rowPatterns[index];
		}
		bool cellFind = false;
		bool cellChanged = false;
		bool cellError = false;
		bool cellWarning = false;
		if (useBraille) {
			for (int py = 0; py < 4; ++py) {
				const std::size_t sampleOffset = static_cast<std::size_t>(y) * 4 + static_cast<std::size_t>(py);
				if (sampleOffset >= samplingWindow.lineCount) break;
				const std::size_t lineIndex = samplingWindow.startLine + sampleOffset;
				const std::uint64_t errorBits = Impl::lineMaskBits(overlay.errorLineMasks, lineIndex);
				const std::uint64_t warningBits = Impl::lineMaskBits(overlay.warningLineMasks, lineIndex);
				const std::uint64_t findBits = Impl::lineMaskBits(overlay.findLineMasks, lineIndex);
				const std::uint64_t dirtyBits = Impl::lineMaskBits(overlay.dirtyLineMasks, lineIndex);
				if (!cellError && miniMapCellHasOverlayBits(errorBits, x, true)) cellError = true;
				if (!cellWarning && miniMapCellHasOverlayBits(warningBits, x, true)) cellWarning = true;
				if (!cellFind && miniMapCellHasOverlayBits(findBits, x, true)) cellFind = true;
				if (!cellChanged && miniMapCellHasOverlayBits(dirtyBits, x, true)) cellChanged = true;
			}
		} else {
			std::size_t lineIndex = samplingWindow.startLine + static_cast<std::size_t>(y);
			const std::uint64_t errorBits = Impl::lineMaskBits(overlay.errorLineMasks, lineIndex);
			const std::uint64_t warningBits = Impl::lineMaskBits(overlay.warningLineMasks, lineIndex);
			const std::uint64_t findBits = Impl::lineMaskBits(overlay.findLineMasks, lineIndex);
			const std::uint64_t dirtyBits = Impl::lineMaskBits(overlay.dirtyLineMasks, lineIndex);
			cellError = miniMapCellHasOverlayBits(errorBits, x, false);
			cellWarning = miniMapCellHasOverlayBits(warningBits, x, false);
			cellFind = miniMapCellHasOverlayBits(findBits, x, false);
			cellChanged = miniMapCellHasOverlayBits(dirtyBits, x, false);
		}
		TColorAttr rowPriorityColor = palette.normal;
		if (cellError) rowPriorityColor = palette.errorMarker;
		else if (cellWarning)
			rowPriorityColor = palette.warningMarker;
		else if (cellFind)
			rowPriorityColor = palette.findMarker;
		else if (cellChanged)
			rowPriorityColor = palette.changed;
		const bool cellOverlayActive = cellError || cellWarning || cellFind || cellChanged;
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
