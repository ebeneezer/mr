#include "MRMiniMap.hpp"

#include "../MRWindowSupport.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <future>
#include <map>
#include <memory>
#include <thread>
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

std::size_t scaledMidpoint(std::size_t sampleIndex, std::size_t sampleCount, std::size_t targetCount) noexcept {
	if (sampleCount == 0 || targetCount == 0) return 0;
	unsigned long long numerator = static_cast<unsigned long long>(sampleIndex) * 2ull + 1ull;
	unsigned long long scaled = numerator * static_cast<unsigned long long>(targetCount);
	unsigned long long denominator = static_cast<unsigned long long>(sampleCount) * 2ull;
	std::size_t mapped = static_cast<std::size_t>(scaled / denominator);
	return std::min(mapped, targetCount - 1);
}

std::pair<std::size_t, std::size_t> scaledInterval(std::size_t index, std::size_t count, std::size_t targetCount) noexcept {
	if (count == 0 || targetCount == 0) return std::make_pair(0u, 0u);
	std::size_t start = (index * targetCount) / count;
	std::size_t end = ((index + 1) * targetCount + count - 1) / count;
	if (end <= start) end = std::min(targetCount, start + 1);
	return std::make_pair(std::min(start, targetCount), std::min(end, targetCount));
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

bool parallelMiniMapValidationEnabled() noexcept {
	const char *value = std::getenv("MR_VALIDATE_PARALLEL_MINIMAP");
	return value != nullptr && value[0] == '1' && value[1] == '\0';
}

unsigned miniMapWorkerCountFor(std::size_t totalMiniMapCells, int rowCount) noexcept {
	const unsigned hardwareWorkers = std::max(1u, std::thread::hardware_concurrency());
	return totalMiniMapCells >= 512 ? std::min<unsigned>(hardwareWorkers, static_cast<unsigned>(std::max(1, rowCount))) : 1u;
}

struct MiniMapLineSample {
	std::uint64_t dotColumnBits = 0;
};

struct MiniMapWarmupBuildResult {
	mr::coprocessor::TaskStatus status = mr::coprocessor::TaskStatus::Cancelled;
	mr::coprocessor::MiniMapWarmupPayload payload;
};

MiniMapWarmupBuildResult buildMiniMapWarmupPayload(const mr::editor::ReadSnapshot &snapshot, int rowCount, int bodyWidth, int viewportWidth, bool useBraille, const MREditSetupSettings &settings,
                                                   std::size_t totalLines, std::size_t windowStartLine, std::size_t windowLineCount, const mr::coprocessor::TaskInfo &info, std::stop_token stopToken,
                                                   bool allowParallel) {
	MiniMapWarmupBuildResult build;
	std::vector<unsigned char> rowPatterns;
	std::vector<std::size_t> rowLineStarts;
	std::vector<std::size_t> rowLineEnds;
	const int dotRows = useBraille ? std::max(1, rowCount * 4) : std::max(1, rowCount);
	const int dotCols = useBraille ? std::max(1, bodyWidth * 2) : std::max(1, bodyWidth);
	const std::size_t normalizedWindowLineCount = std::max<std::size_t>(1, windowLineCount);
	const std::size_t totalMiniMapCells = static_cast<std::size_t>(std::max(1, rowCount)) * static_cast<std::size_t>(std::max(1, bodyWidth));
	std::size_t normalizedTotalLines = std::max<std::size_t>(1, totalLines);
	const auto shouldStop = [&]() noexcept { return stopToken.stop_requested() || info.cancelRequested(); };

	if (shouldStop()) return build;
	if (snapshot.exactLineCountKnown()) normalizedTotalLines = std::max<std::size_t>(1, snapshot.lineCount());
	rowPatterns.assign(static_cast<std::size_t>(std::max(0, rowCount) * std::max(0, bodyWidth)), 0);
	rowLineStarts.assign(static_cast<std::size_t>(std::max(0, rowCount)), 0);
	rowLineEnds.assign(static_cast<std::size_t>(std::max(0, rowCount)), 0);
	auto renderRows = [&](int yStart, int yEnd) -> bool {
		mr::editor::ReadSnapshot workerSnapshot = snapshot;
		std::map<std::size_t, MiniMapLineSample> sampledLineSamples;
		auto lineSampleAt = [&](std::size_t lineIndex) -> const MiniMapLineSample & {
			auto cached = sampledLineSamples.find(lineIndex);
			if (cached != sampledLineSamples.end()) return cached->second;
			MiniMapLineSample sample;
			if (lineIndex < normalizedTotalLines) {
				std::size_t lineStart = workerSnapshot.lineStartByIndex(lineIndex);
				std::string lineText = workerSnapshot.lineText(lineStart);
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
			auto inserted = sampledLineSamples.insert(std::make_pair(lineIndex, sample));
			return inserted.first->second;
		};

		for (int y = yStart; y < yEnd; ++y) {
			if (shouldStop()) return false;
			std::pair<std::size_t, std::size_t> lineSpan = scaledInterval(static_cast<std::size_t>(y), static_cast<std::size_t>(rowCount), normalizedWindowLineCount);
			rowLineStarts[static_cast<std::size_t>(y)] = std::min(normalizedTotalLines, windowStartLine + lineSpan.first);
			rowLineEnds[static_cast<std::size_t>(y)] = std::min(normalizedTotalLines, windowStartLine + lineSpan.second);
			for (int x = 0; x < bodyWidth; ++x) {
				unsigned char pattern = 0;
				if (useBraille) {
					static const unsigned char dotBits[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};
					for (int py = 0; py < 4; ++py) {
						std::size_t sampleRow = static_cast<std::size_t>(y * 4 + py);
						if (sampleRow >= normalizedWindowLineCount) continue;
						std::size_t lineIndex = windowStartLine + scaledMidpoint(sampleRow, static_cast<std::size_t>(dotRows), normalizedWindowLineCount);
						const MiniMapLineSample &sample = lineSampleAt(lineIndex);
						for (int px = 0; px < 2; ++px) {
							const int dotColumn = x * 2 + px;
							if (dotColumn < 64 && (sample.dotColumnBits & (1ULL << dotColumn)) != 0) pattern |= dotBits[py][px];
						}
					}
				} else if (static_cast<std::size_t>(y) < normalizedWindowLineCount) {
					std::size_t lineIndex = windowStartLine + scaledMidpoint(static_cast<std::size_t>(y), static_cast<std::size_t>(rowCount), normalizedWindowLineCount);
					const MiniMapLineSample &sample = lineSampleAt(lineIndex);
					if (x < 64 && (sample.dotColumnBits & (1ULL << x)) != 0) pattern = 1;
				}
				rowPatterns[static_cast<std::size_t>(y * bodyWidth + x)] = pattern;
			}
		}
		return true;
	};

	const unsigned workerCount = allowParallel ? miniMapWorkerCountFor(totalMiniMapCells, rowCount) : 1u;
	if (workerCount == 1) {
		if (!renderRows(0, rowCount)) return build;
	} else {
		std::vector<std::future<bool>> workers;

		workers.reserve(workerCount);
		for (unsigned workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
			const int yStart = static_cast<int>((static_cast<std::size_t>(workerIndex) * static_cast<std::size_t>(rowCount)) / static_cast<std::size_t>(workerCount));
			const int yEnd = static_cast<int>((static_cast<std::size_t>(workerIndex + 1) * static_cast<std::size_t>(rowCount)) / static_cast<std::size_t>(workerCount));
			if (yStart >= yEnd) continue;
			workers.push_back(std::async(std::launch::async, [&, yStart, yEnd]() { return renderRows(yStart, yEnd); }));
		}
		for (std::future<bool> &worker : workers)
			if (!worker.get()) return build;
	}

	build.status = mr::coprocessor::TaskStatus::Completed;
	build.payload = mr::coprocessor::MiniMapWarmupPayload(useBraille, rowCount, bodyWidth, normalizedTotalLines, windowStartLine, normalizedWindowLineCount, viewportWidth, std::move(rowPatterns),
	                                                      std::move(rowLineStarts), std::move(rowLineEnds));
	return build;
}

bool miniMapWarmupPayloadsMatch(const mr::coprocessor::MiniMapWarmupPayload &lhs, const mr::coprocessor::MiniMapWarmupPayload &rhs) noexcept {
	return lhs.braille == rhs.braille && lhs.rowCount == rhs.rowCount && lhs.bodyWidth == rhs.bodyWidth && lhs.totalLines == rhs.totalLines && lhs.windowStartLine == rhs.windowStartLine &&
	       lhs.windowLineCount == rhs.windowLineCount && lhs.viewportWidth == rhs.viewportWidth && lhs.rowPatterns == rhs.rowPatterns && lhs.rowLineStarts == rhs.rowLineStarts &&
	       lhs.rowLineEnds == rhs.rowLineEnds;
}

const char *miniMapWarmupMismatchField(const mr::coprocessor::MiniMapWarmupPayload &lhs, const mr::coprocessor::MiniMapWarmupPayload &rhs) noexcept {
	if (lhs.braille != rhs.braille) return "braille";
	if (lhs.rowCount != rhs.rowCount) return "rowCount";
	if (lhs.bodyWidth != rhs.bodyWidth) return "bodyWidth";
	if (lhs.totalLines != rhs.totalLines) return "totalLines";
	if (lhs.windowStartLine != rhs.windowStartLine) return "windowStartLine";
	if (lhs.windowLineCount != rhs.windowLineCount) return "windowLineCount";
	if (lhs.viewportWidth != rhs.viewportWidth) return "viewportWidth";
	if (lhs.rowPatterns != rhs.rowPatterns) return "rowPatterns";
	if (lhs.rowLineStarts != rhs.rowLineStarts) return "rowLineStarts";
	if (lhs.rowLineEnds != rhs.rowLineEnds) return "rowLineEnds";
	return "";
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

	struct WarmupRenderKey {
		std::size_t documentId = 0;
		std::size_t version = 0;
		int rowCount = 0;
		int bodyWidth = 0;
		int viewportWidth = 1;
		bool braille = true;
		std::size_t windowStartLine = 0;
		std::size_t windowLineCount = 1;
		std::size_t totalLines = 1;
		std::string formatLine;
		int tabSize = 8;
		int leftMargin = 1;
		int rightMargin = 78;
	};

	std::uint64_t warmupTaskId = 0;
	WarmupRenderKey warmupKey;
	RenderCache cache;

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
		const std::size_t normalizedRowCount = std::max<std::size_t>(1, static_cast<std::size_t>(std::max(rowCount, 1)));
		const std::size_t samplingRowCount = useBraille ? normalizedRowCount * 4u : normalizedRowCount;
		const std::size_t maxWindowLineCount = normalizedRowCount * 9u;
		SamplingWindow window;
		if (normalizedTotalLines <= maxWindowLineCount) {
			window.startLine = 0;
			window.lineCount = std::max(normalizedTotalLines, samplingRowCount);
			return window;
		}
		window.lineCount = std::max<std::size_t>(1, maxWindowLineCount);
		const std::size_t clampedTop = std::min(topLine, normalizedTotalLines - 1);
		std::size_t preferredStart = 0;
		if (clampedTop > window.lineCount / 2) preferredStart = clampedTop - window.lineCount / 2;
		const std::size_t maxStart = normalizedTotalLines - window.lineCount;
		window.startLine = std::min(preferredStart, maxStart);
		return window;
	}

	bool cacheReadyForViewport(const Viewport &viewport, int rowCount, bool braille, const SamplingWindow &window, std::size_t documentId, std::size_t version) const noexcept {
		return cache.valid && cache.documentId == documentId && cache.documentVersion == version && cache.rowCount == rowCount && cache.bodyWidth == viewport.bodyWidth && cache.viewportWidth == std::max(1, viewport.width) && cache.braille == braille && cache.windowStartLine == window.startLine && cache.windowLineCount == std::max<std::size_t>(1, window.lineCount);
	}

	Signals clearWarmupTask(std::uint64_t expectedTaskId) noexcept {
		Signals signals;

		if (expectedTaskId != 0 && warmupTaskId != expectedTaskId) return signals;
		if (warmupTaskId == 0) return signals;
		warmupTaskId = 0;
		warmupKey = WarmupRenderKey();
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

MRMiniMapRenderer::Signals MRMiniMapRenderer::clearWarmupTask(std::uint64_t expectedTaskId) noexcept {
	return mImpl != nullptr ? mImpl->clearWarmupTask(expectedTaskId) : Signals();
}

MRMiniMapRenderer::Signals MRMiniMapRenderer::invalidate(bool cancelTask, std::size_t documentId) noexcept {
	return mImpl != nullptr ? mImpl->invalidate(cancelTask, documentId) : Signals();
}

MRMiniMapRenderer::ApplyWarmupResult MRMiniMapRenderer::applyWarmup(const mr::coprocessor::MiniMapWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId, std::size_t documentId, std::size_t version) noexcept {
	ApplyWarmupResult result;

	if (mImpl == nullptr) return result;
	if (expectedTaskId == 0 || mImpl->warmupTaskId != expectedTaskId) return result;
	if (documentId != mImpl->warmupKey.documentId || version != expectedVersion) return result;
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
	result.signals.merge(mImpl->clearWarmupTask(expectedTaskId));
	result.signals.redraw = true;
	result.applied = true;
	return result;
}

MRMiniMapRenderer::Signals MRMiniMapRenderer::scheduleWarmupIfNeeded(const Viewport &viewport, int rowCount, bool useBraille, std::size_t totalLinesHint, std::size_t topLine, std::size_t documentId, std::size_t version, const mr::editor::ReadSnapshot &snapshot, const MREditSetupSettings &settings) {
	Signals signals;

	if (mImpl == nullptr) return signals;
	if (viewport.bodyWidth <= 0 || rowCount <= 0) return invalidate(true, documentId);
	std::size_t totalLines = std::max<std::size_t>(1, totalLinesHint);
	if (snapshot.exactLineCountKnown()) totalLines = std::max<std::size_t>(1, snapshot.lineCount());
	const Impl::SamplingWindow samplingWindow = Impl::samplingWindowFor(totalLines, topLine, rowCount, useBraille);
	if (mImpl->cacheReadyForViewport(viewport, rowCount, useBraille, samplingWindow, documentId, version)) return signals;
	const int bodyWidth = viewport.bodyWidth;
	const int viewportWidth = std::max(1, viewport.width);
	Impl::WarmupRenderKey requestedWarmupKey;
	requestedWarmupKey.documentId = documentId;
	requestedWarmupKey.version = version;
	requestedWarmupKey.rowCount = rowCount;
	requestedWarmupKey.bodyWidth = bodyWidth;
	requestedWarmupKey.viewportWidth = viewportWidth;
	requestedWarmupKey.braille = useBraille;
	requestedWarmupKey.windowStartLine = samplingWindow.startLine;
	requestedWarmupKey.windowLineCount = samplingWindow.lineCount;
	requestedWarmupKey.totalLines = totalLines;
	requestedWarmupKey.formatLine = settings.formatLine;
	requestedWarmupKey.tabSize = settings.tabSize;
	requestedWarmupKey.leftMargin = settings.leftMargin;
	requestedWarmupKey.rightMargin = settings.rightMargin;
	if (mImpl->warmupTaskId != 0) {
		if (mImpl->warmupKey.documentId == requestedWarmupKey.documentId && mImpl->warmupKey.version == requestedWarmupKey.version && mImpl->warmupKey.rowCount == requestedWarmupKey.rowCount && mImpl->warmupKey.bodyWidth == requestedWarmupKey.bodyWidth &&
		    mImpl->warmupKey.viewportWidth == requestedWarmupKey.viewportWidth && mImpl->warmupKey.braille == requestedWarmupKey.braille && mImpl->warmupKey.windowStartLine == requestedWarmupKey.windowStartLine &&
		    mImpl->warmupKey.windowLineCount == requestedWarmupKey.windowLineCount && mImpl->warmupKey.totalLines == requestedWarmupKey.totalLines && mImpl->warmupKey.formatLine == requestedWarmupKey.formatLine &&
		    mImpl->warmupKey.tabSize == requestedWarmupKey.tabSize && mImpl->warmupKey.leftMargin == requestedWarmupKey.leftMargin && mImpl->warmupKey.rightMargin == requestedWarmupKey.rightMargin)
			return signals;
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mImpl->warmupTaskId));
		signals.merge(mImpl->clearWarmupTask(mImpl->warmupTaskId));
	}

	std::uint64_t previousTaskId = mImpl->warmupTaskId;
	const std::string coalescingKey = "minimap:" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this))) + ":" + std::to_string(requestedWarmupKey.documentId) + ":" +
	                                 std::to_string(requestedWarmupKey.version) + ":" + std::to_string(requestedWarmupKey.rowCount) + ":" + std::to_string(requestedWarmupKey.bodyWidth) + ":" +
	                                 std::to_string(requestedWarmupKey.viewportWidth) + ":" + std::to_string(requestedWarmupKey.braille ? 1 : 0) + ":" + std::to_string(requestedWarmupKey.windowStartLine) + ":" +
	                                 std::to_string(requestedWarmupKey.windowLineCount) + ":" + std::to_string(requestedWarmupKey.totalLines) + ":" + std::to_string(requestedWarmupKey.tabSize) + ":" +
	                                 std::to_string(requestedWarmupKey.leftMargin) + ":" + std::to_string(requestedWarmupKey.rightMargin) + ":" + requestedWarmupKey.formatLine;
	mImpl->warmupKey = std::move(requestedWarmupKey);
	mImpl->warmupTaskId = mr::coprocessor::globalCoprocessor().submitCoalesced(mr::coprocessor::Lane::MiniMap, mr::coprocessor::TaskKind::MiniMapWarmup, documentId, version, coalescingKey, "rendering mini map",
	                                                                          [snapshot, rowCount, bodyWidth, viewportWidth, useBraille, settings, totalLines, samplingWindow](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
		mr::coprocessor::Result result;
		result.task = info;
		const bool validateParallel = parallelMiniMapValidationEnabled();
		const bool canRunParallel = miniMapWorkerCountFor(static_cast<std::size_t>(std::max(1, rowCount)) * static_cast<std::size_t>(std::max(1, bodyWidth)), rowCount) > 1;

		if (validateParallel && canRunParallel) {
			MiniMapWarmupBuildResult serialBuild =
			    buildMiniMapWarmupPayload(snapshot, rowCount, bodyWidth, viewportWidth, useBraille, settings, totalLines, samplingWindow.startLine, samplingWindow.lineCount, info, stopToken, false);
			if (serialBuild.status != mr::coprocessor::TaskStatus::Completed) {
				result.status = serialBuild.status;
				return result;
			}
			MiniMapWarmupBuildResult parallelBuild =
			    buildMiniMapWarmupPayload(snapshot, rowCount, bodyWidth, viewportWidth, useBraille, settings, totalLines, samplingWindow.startLine, samplingWindow.lineCount, info, stopToken, true);
			if (parallelBuild.status != mr::coprocessor::TaskStatus::Completed) {
				result.status = parallelBuild.status;
				return result;
			}
			if (!miniMapWarmupPayloadsMatch(serialBuild.payload, parallelBuild.payload)) {
				const char *field = miniMapWarmupMismatchField(serialBuild.payload, parallelBuild.payload);
				mrLogMessage((std::string("MiniMap parallel validation mismatch on ") + field + "; using serial result.").c_str());
				result.status = mr::coprocessor::TaskStatus::Completed;
				result.payload = std::make_shared<mr::coprocessor::MiniMapWarmupPayload>(std::move(serialBuild.payload));
				return result;
			}
			result.status = mr::coprocessor::TaskStatus::Completed;
			result.payload = std::make_shared<mr::coprocessor::MiniMapWarmupPayload>(std::move(parallelBuild.payload));
			return result;
		}

		MiniMapWarmupBuildResult build =
		    buildMiniMapWarmupPayload(snapshot, rowCount, bodyWidth, viewportWidth, useBraille, settings, totalLines, samplingWindow.startLine, samplingWindow.lineCount, info, stopToken, true);
		result.status = build.status;
		if (build.status == mr::coprocessor::TaskStatus::Completed) result.payload = std::make_shared<mr::coprocessor::MiniMapWarmupPayload>(std::move(build.payload));
		return result;
	});
	if (mImpl->warmupTaskId != previousTaskId) signals.notifyTaskStateChanged = true;
	return signals;
}

MRMiniMapRenderer::OverlayState MRMiniMapRenderer::computeOverlayState(const mr::editor::ReadSnapshot &snapshot, const mr::editor::Range &selection, const std::vector<mr::editor::Range> &findRanges, const std::vector<mr::editor::Range> &dirtyRanges, std::size_t totalLines, int viewportWidth, int miniMapBodyWidth, bool useBraille, const MREditSetupSettings &settings) const {
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

	auto appendRangeMasks = [&](std::vector<OverlayState::LineMask> &target, mr::editor::Range range) {
		range = range.clamped(length);
		if (range.end <= range.start) return;

		for (std::size_t lineStart = snapshot.lineStartByIndex(snapshot.lineIndex(range.start)); lineStart < range.end;) {
			const std::size_t lineIndex = snapshot.lineIndex(lineStart);
			if (lineIndex >= totalLines) break;
			const std::size_t nextLineStart = snapshot.nextLine(lineStart);
			const std::size_t lineEnd = std::min(nextLineStart, length);
			const std::size_t sliceStart = range.start > lineStart ? range.start - lineStart : 0;
			const std::size_t sliceEnd = range.end < lineEnd ? range.end - lineStart : lineEnd - lineStart;
			const std::uint64_t mask = rangeMaskForLineSlice(snapshot.lineText(lineStart), sliceStart, sliceEnd);
			if (mask != 0) target.push_back({lineIndex, mask});
			if (nextLineStart <= lineStart) break;
			lineStart = nextLineStart;
		}
	};

	if (selection.end > selection.start) appendRangeMasks(overlay.findLineMasks, selection.normalized());
	for (const mr::editor::Range &range : findRanges)
		appendRangeMasks(overlay.findLineMasks, range);
	for (const mr::editor::Range &range : dirtyRanges)
		appendRangeMasks(overlay.dirtyLineMasks, range);
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
	const bool stalePatternCacheUsable = !cacheReady && mImpl->cache.bodyWidth == bodyWidth && mImpl->cache.rowCount == miniMapRows && !mImpl->cache.rowPatterns.empty();
	std::size_t rowLineStart = 0;
	std::size_t rowLineEnd = 0;

	if (y >= miniMapRows) {
		buffer.moveChar(static_cast<ushort>(bodyX), ' ', palette.normal, static_cast<ushort>(bodyWidth));
		if (viewport.separatorX >= 0 && viewport.separatorX < viewWidth) buffer.moveChar(static_cast<ushort>(viewport.separatorX), ' ', palette.normal, 1);
		buffer.moveChar(static_cast<ushort>(viewport.infoX), ' ', palette.normal, 1);
		return;
	}

	if ((cacheReady || stalePatternCacheUsable) && static_cast<std::size_t>(y) < mImpl->cache.rowLineStarts.size() && static_cast<std::size_t>(y) < mImpl->cache.rowLineEnds.size()) {
		rowLineStart = mImpl->cache.rowLineStarts[static_cast<std::size_t>(y)];
		rowLineEnd = mImpl->cache.rowLineEnds[static_cast<std::size_t>(y)];
	} else {
		std::pair<std::size_t, std::size_t> rowSpan = scaledInterval(static_cast<std::size_t>(y), static_cast<std::size_t>(std::max(miniMapRows, 1)), samplingWindow.lineCount);
		const std::size_t normTotal = std::max<std::size_t>(1, totalLines);
		rowLineStart = std::min(samplingWindow.startLine + rowSpan.first, normTotal);
		rowLineEnd = std::min(samplingWindow.startLine + rowSpan.second, normTotal);
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
		if (useBraille) {
			for (int py = 0; py < 4 && (!cellFind || !cellChanged); ++py) {
				std::size_t sampleRow = static_cast<std::size_t>(y * 4 + py);
				if (sampleRow >= samplingWindow.lineCount) continue;
				std::size_t lineIndex = samplingWindow.startLine + scaledMidpoint(sampleRow, static_cast<std::size_t>(std::max(miniMapRows * 4, 1)), samplingWindow.lineCount);
				const std::uint64_t findBits = Impl::lineMaskBits(overlay.findLineMasks, lineIndex);
				const std::uint64_t dirtyBits = Impl::lineMaskBits(overlay.dirtyLineMasks, lineIndex);
				if (!cellFind && miniMapCellHasOverlayBits(findBits, x, true)) cellFind = true;
				if (!cellChanged && miniMapCellHasOverlayBits(dirtyBits, x, true)) cellChanged = true;
			}
		} else {
			std::size_t lineIndex = samplingWindow.startLine + scaledMidpoint(static_cast<std::size_t>(y), static_cast<std::size_t>(std::max(miniMapRows, 1)), samplingWindow.lineCount);
			const std::uint64_t findBits = Impl::lineMaskBits(overlay.findLineMasks, lineIndex);
			const std::uint64_t dirtyBits = Impl::lineMaskBits(overlay.dirtyLineMasks, lineIndex);
			cellFind = miniMapCellHasOverlayBits(findBits, x, false);
			cellChanged = miniMapCellHasOverlayBits(dirtyBits, x, false);
		}
		TColorAttr rowPriorityColor = palette.normal;
		if (cellFind) rowPriorityColor = palette.findMarker;
		else if (cellError)
			rowPriorityColor = palette.errorMarker;
		else if (cellChanged)
			rowPriorityColor = palette.changed;
		const bool cellOverlayActive = cellFind || cellError || cellChanged;
		TColorAttr cellColor = (pattern != 0 || cellOverlayActive) ? rowPriorityColor : palette.normal;
		if (useBraille) buffer.moveStr(static_cast<ushort>(bodyX + x), glyphTable[pattern], cellColor, 1);
		else if (pattern != 0)
			buffer.moveStr(static_cast<ushort>(bodyX + x), "\xE2\x96\x88", cellColor, 1);
		else
			buffer.moveChar(static_cast<ushort>(bodyX + x), ' ', cellColor, 1);
	}

	if (viewport.separatorX >= 0 && viewport.separatorX < viewWidth) buffer.moveChar(static_cast<ushort>(viewport.separatorX), ' ', palette.normal, 1);

	std::size_t clampedTopLine = std::min(topLine, totalLines - 1);
	std::size_t visibleLines = static_cast<std::size_t>(std::max(miniMapRows, 1));
	std::size_t viewportLineEnd = std::min(totalLines, clampedTopLine + visibleLines);
	bool markerVisible = false;
	if (rowLineEnd > rowLineStart) markerVisible = rowLineStart < viewportLineEnd && rowLineEnd > clampedTopLine;
	if (markerVisible) buffer.moveStr(static_cast<ushort>(viewport.infoX), viewportMarkerGlyph, palette.viewport, 1);
	else
		buffer.moveChar(static_cast<ushort>(viewport.infoX), ' ', palette.normal, 1);
}
