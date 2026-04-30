#include "MRFileEditor.hpp"

bool MRFileEditor::useBrailleMiniMapRenderer() noexcept {
	static const int kBrailleWidth = strwidth("\xE2\xA3\xBF"); // U+28FF
	return kBrailleWidth == 1;
}

std::string MRFileEditor::normalizedMiniMapViewportMarkerGlyph(const std::string &configuredGlyph) {
	if (configuredGlyph.empty() || strwidth(configuredGlyph.c_str()) != 1) return "│";
	return configuredGlyph;
}

const char *MRFileEditor::miniMapWarmupTaskLabel() noexcept {
	return "rendering mini map";
}

std::string MRFileEditor::utf8FromCodepoint(std::uint32_t codepoint) {
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

const std::array<std::string, 256> &MRFileEditor::brailleGlyphTable() {
	static const std::array<std::string, 256> table = []() {
		std::array<std::string, 256> generated;
		generated[0] = " ";
		for (std::size_t i = 1; i < generated.size(); ++i)
			generated[i] = utf8FromCodepoint(static_cast<std::uint32_t>(0x2800 + i));
		return generated;
	}();
	return table;
}

std::size_t MRFileEditor::scaledMidpoint(std::size_t sampleIndex, std::size_t sampleCount, std::size_t targetCount) noexcept {
	if (sampleCount == 0 || targetCount == 0) return 0;
	unsigned long long numerator = static_cast<unsigned long long>(sampleIndex) * 2ull + 1ull;
	unsigned long long scaled = numerator * static_cast<unsigned long long>(targetCount);
	unsigned long long denominator = static_cast<unsigned long long>(sampleCount) * 2ull;
	std::size_t mapped = static_cast<std::size_t>(scaled / denominator);
	return std::min(mapped, targetCount - 1);
}

MRFileEditor::MiniMapPalette MRFileEditor::resolveMiniMapPalette() {
	MiniMapPalette palette;
	unsigned char configured = 0;
	const TColorAttr fallback = static_cast<TColorAttr>(getColor(0x0201));

	palette.normal = configuredColorSlotOverride(kMrPaletteMiniMapNormal, configured) ? static_cast<TColorAttr>(configured) : fallback;
	palette.viewport = configuredColorSlotOverride(kMrPaletteMiniMapViewport, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	palette.changed = configuredColorSlotOverride(kMrPaletteMiniMapChanged, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	palette.findMarker = configuredColorSlotOverride(kMrPaletteMiniMapFindMarker, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	palette.errorMarker = configuredColorSlotOverride(kMrPaletteMiniMapErrorMarker, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	return palette;
}

std::pair<std::size_t, std::size_t> MRFileEditor::scaledInterval(std::size_t index, std::size_t count, std::size_t targetCount) noexcept {
	if (count == 0 || targetCount == 0) return std::make_pair(0u, 0u);
	std::size_t start = (index * targetCount) / count;
	std::size_t end = ((index + 1) * targetCount + count - 1) / count;
	if (end <= start) end = std::min(targetCount, start + 1);
	return std::make_pair(std::min(start, targetCount), std::min(end, targetCount));
}

void MRFileEditor::normalizeMiniMapLineMasks(std::vector<MiniMapOverlayState::LineMask> &masks) {
	if (masks.empty()) return;
	std::sort(masks.begin(), masks.end(), [](const MiniMapOverlayState::LineMask &lhs, const MiniMapOverlayState::LineMask &rhs) { return lhs.lineIndex < rhs.lineIndex; });
	std::size_t writeIndex = 0;
	for (std::size_t readIndex = 1; readIndex < masks.size(); ++readIndex) {
		if (masks[writeIndex].lineIndex == masks[readIndex].lineIndex) masks[writeIndex].dotColumnBits |= masks[readIndex].dotColumnBits;
		else
			masks[++writeIndex] = masks[readIndex];
	}
	masks.resize(writeIndex + 1);
}

std::uint64_t MRFileEditor::miniMapLineMaskBits(const std::vector<MiniMapOverlayState::LineMask> &masks, std::size_t lineIndex) noexcept {
	auto it = std::lower_bound(masks.begin(), masks.end(), lineIndex, [](const MiniMapOverlayState::LineMask &mask, std::size_t value) { return mask.lineIndex < value; });
	return (it != masks.end() && it->lineIndex == lineIndex) ? it->dotColumnBits : 0;
}

bool MRFileEditor::miniMapCellHasOverlayBits(std::uint64_t lineBits, int x, bool useBraille) noexcept {
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

MRFileEditor::MiniMapSamplingWindow MRFileEditor::miniMapSamplingWindowFor(std::size_t totalLines, std::size_t topLine, int rowCount, bool useBraille) noexcept {
	const std::size_t normalizedTotalLines = std::max<std::size_t>(1, totalLines);
	const std::size_t normalizedRowCount = std::max<std::size_t>(1, static_cast<std::size_t>(std::max(rowCount, 1)));
	const std::size_t samplingRowCount = useBraille ? normalizedRowCount * 4u : normalizedRowCount;
	const std::size_t maxWindowLineCount = normalizedRowCount * 9u;
	MiniMapSamplingWindow window;
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

bool MRFileEditor::ratioCellInRange(int from, int to, int viewportWidth, int cellIndex, int cellCount) noexcept {
	if (from < 0 || to <= 0 || from >= to || viewportWidth <= 0 || cellCount <= 0) return false;
	long long cellLeft = static_cast<long long>(cellIndex) * viewportWidth;
	long long cellRight = static_cast<long long>(cellIndex + 1) * viewportWidth;
	long long cLeft = static_cast<long long>(from) * cellCount;
	long long cRight = static_cast<long long>(to) * cellCount;
	return cellRight > cLeft && cellLeft < cRight;
}

bool MRFileEditor::miniMapCacheReadyForViewport(const TextViewportGeometry &viewport, bool braille, const MiniMapSamplingWindow &window) const noexcept {
	const int rowCount = std::max(0, miniMapViewportRows());
	return mMiniMapCache.valid && mMiniMapCache.documentId == mBufferModel.documentId() && mMiniMapCache.documentVersion == mBufferModel.version() && mMiniMapCache.rowCount == rowCount && mMiniMapCache.bodyWidth == viewport.miniMapBodyWidth && mMiniMapCache.viewportWidth == std::max(1, viewport.width) && mMiniMapCache.braille == braille && mMiniMapCache.windowStartLine == window.startLine && mMiniMapCache.windowLineCount == std::max<std::size_t>(1, window.lineCount);
}

int MRFileEditor::miniMapViewportRows() const noexcept {
	return std::max(0, visibleTextRows());
}

void MRFileEditor::clearMiniMapWarmupTaskInternal(std::uint64_t expectedTaskId) noexcept {
	if (expectedTaskId != 0 && mMiniMapWarmupTaskId != expectedTaskId) return;
	if (mMiniMapWarmupTaskId == 0) return;
	const bool shouldReschedule = mMiniMapWarmupReschedulePending;
	mMiniMapWarmupTaskId = 0;
	mMiniMapWarmupDocumentId = 0;
	mMiniMapWarmupVersion = 0;
	mMiniMapWarmupRows = 0;
	mMiniMapWarmupBodyWidth = 0;
	mMiniMapWarmupViewportWidth = 0;
	mMiniMapWarmupBraille = true;
	mMiniMapWarmupWindowStartLine = 0;
	mMiniMapWarmupWindowLineCount = 0;
	mMiniMapWarmupReschedulePending = false;
	notifyWindowTaskStateChanged();
	if (shouldReschedule) drawView();
}

void MRFileEditor::invalidateMiniMapCache(bool cancelTask) {
	const bool keepStaleCache = !cancelTask && mMiniMapCache.documentId == mBufferModel.documentId() && mMiniMapCache.bodyWidth > 0 && mMiniMapCache.rowCount > 0;
	mMiniMapCache.valid = false;
	if (!keepStaleCache) {
		mMiniMapCache.rowPatterns.clear();
		mMiniMapCache.rowLineStarts.clear();
		mMiniMapCache.rowLineEnds.clear();
	}
	mMiniMapWarmupReschedulePending = false;
	if (cancelTask && mMiniMapWarmupTaskId != 0) {
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mMiniMapWarmupTaskId));
		clearMiniMapWarmupTaskInternal(mMiniMapWarmupTaskId);
	}
}

bool MRFileEditor::applyMiniMapWarmupInternal(const mr::coprocessor::MiniMapWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId) {
	if (expectedTaskId == 0 || mMiniMapWarmupTaskId != expectedTaskId) return false;
	if (mBufferModel.documentId() != mMiniMapWarmupDocumentId || mBufferModel.version() != expectedVersion) return false;

	mMiniMapCache.valid = true;
	mMiniMapCache.braille = payload.braille;
	mMiniMapCache.rowCount = payload.rowCount;
	mMiniMapCache.bodyWidth = payload.bodyWidth;
	mMiniMapCache.documentId = mBufferModel.documentId();
	mMiniMapCache.documentVersion = mBufferModel.version();
	mMiniMapCache.totalLines = std::max<std::size_t>(1, payload.totalLines);
	mMiniMapCache.windowStartLine = payload.windowStartLine;
	mMiniMapCache.windowLineCount = std::max<std::size_t>(1, payload.windowLineCount);
	mMiniMapCache.viewportWidth = std::max(1, payload.viewportWidth);
	mMiniMapCache.rowPatterns = payload.rowPatterns;
	mMiniMapCache.rowLineStarts = payload.rowLineStarts;
	mMiniMapCache.rowLineEnds = payload.rowLineEnds;

	clearMiniMapWarmupTaskInternal(expectedTaskId);
	drawView();
	return true;
}

void MRFileEditor::scheduleMiniMapWarmupIfNeeded(const TextViewportGeometry &viewport, bool useBraille, std::size_t totalLinesHint, std::size_t topLine) {
	const int rowCount = miniMapViewportRows();
	if (viewport.miniMapBodyWidth <= 0 || rowCount <= 0) {
		invalidateMiniMapCache(true);
		return;
	}
	const std::size_t docId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	std::size_t totalLines = std::max<std::size_t>(1, totalLinesHint);
	if (mBufferModel.exactLineCountKnown()) totalLines = std::max<std::size_t>(1, mBufferModel.lineCount());
	const MiniMapSamplingWindow samplingWindow = miniMapSamplingWindowFor(totalLines, topLine, rowCount, useBraille);
	if (miniMapCacheReadyForViewport(viewport, useBraille, samplingWindow)) return;

	const int bodyWidth = viewport.miniMapBodyWidth;
	const int viewportWidth = std::max(1, viewport.width);
	const MREditSetupSettings settings = configuredEditSetupSettings();
	if (mMiniMapWarmupTaskId != 0) {
		if (mMiniMapWarmupDocumentId == docId && mMiniMapWarmupVersion == version && mMiniMapWarmupRows == rowCount && mMiniMapWarmupBodyWidth == bodyWidth && mMiniMapWarmupViewportWidth == viewportWidth && mMiniMapWarmupBraille == useBraille && mMiniMapWarmupWindowStartLine == samplingWindow.startLine && mMiniMapWarmupWindowLineCount == samplingWindow.lineCount) return;
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mMiniMapWarmupTaskId));
		clearMiniMapWarmupTaskInternal(mMiniMapWarmupTaskId);
	}

	MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
	std::uint64_t previousTaskId = mMiniMapWarmupTaskId;
	mMiniMapWarmupDocumentId = docId;
	mMiniMapWarmupVersion = version;
	mMiniMapWarmupRows = rowCount;
	mMiniMapWarmupBodyWidth = bodyWidth;
	mMiniMapWarmupViewportWidth = viewportWidth;
	mMiniMapWarmupBraille = useBraille;
	mMiniMapWarmupWindowStartLine = samplingWindow.startLine;
	mMiniMapWarmupWindowLineCount = samplingWindow.lineCount;
	mMiniMapWarmupReschedulePending = false;
	mMiniMapWarmupTaskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::MiniMap, mr::coprocessor::TaskKind::MiniMapWarmup, docId, version, miniMapWarmupTaskLabel(), [snapshot, rowCount, bodyWidth, viewportWidth, useBraille, settings, totalLines, samplingWindow](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
		mr::coprocessor::Result result;
		struct MiniMapLineSample {
			std::uint64_t dotColumnBits = 0;
		};
		std::vector<unsigned char> rowPatterns;
		std::vector<std::size_t> rowLineStarts;
		std::vector<std::size_t> rowLineEnds;
		const int dotRows = useBraille ? std::max(1, rowCount * 4) : std::max(1, rowCount);
		const int dotCols = useBraille ? std::max(1, bodyWidth * 2) : std::max(1, bodyWidth);
		const std::size_t windowStartLine = samplingWindow.startLine;
		const std::size_t windowLineCount = std::max<std::size_t>(1, samplingWindow.lineCount);
		const std::size_t totalMiniMapCells = static_cast<std::size_t>(std::max(1, rowCount)) * static_cast<std::size_t>(std::max(1, bodyWidth));
		std::size_t normalizedTotalLines = std::max<std::size_t>(1, totalLines);
		auto shouldStop = [&]() noexcept { return stopToken.stop_requested() || info.cancelRequested(); };
		result.task = info;

		if (shouldStop()) {
			result.status = mr::coprocessor::TaskStatus::Cancelled;
			return result;
		}
		if (snapshot.exactLineCountKnown()) normalizedTotalLines = std::max<std::size_t>(1, snapshot.lineCount());
		rowPatterns.assign(static_cast<std::size_t>(std::max(0, rowCount) * std::max(0, bodyWidth)), 0);
		rowLineStarts.assign(static_cast<std::size_t>(std::max(0, rowCount)), 0);
		rowLineEnds.assign(static_cast<std::size_t>(std::max(0, rowCount)), 0);
		auto renderRows = [&](int yStart, int yEnd) -> bool {
			MRTextBufferModel::ReadSnapshot workerSnapshot = snapshot;
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
				std::pair<std::size_t, std::size_t> lineSpan = scaledInterval(static_cast<std::size_t>(y), static_cast<std::size_t>(rowCount), windowLineCount);
				rowLineStarts[static_cast<std::size_t>(y)] = std::min(normalizedTotalLines, windowStartLine + lineSpan.first);
				rowLineEnds[static_cast<std::size_t>(y)] = std::min(normalizedTotalLines, windowStartLine + lineSpan.second);
				for (int x = 0; x < bodyWidth; ++x) {
					unsigned char pattern = 0;
					if (useBraille) {
						static const unsigned char dotBits[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};
						for (int py = 0; py < 4; ++py) {
							std::size_t sampleRow = static_cast<std::size_t>(y * 4 + py);
							if (sampleRow >= windowLineCount) continue;
							std::size_t lineIndex = windowStartLine + scaledMidpoint(sampleRow, static_cast<std::size_t>(dotRows), windowLineCount);
							const MiniMapLineSample &sample = lineSampleAt(lineIndex);
							for (int px = 0; px < 2; ++px) {
								const int dotColumn = x * 2 + px;
								if (dotColumn < 64 && (sample.dotColumnBits & (1ULL << dotColumn)) != 0) pattern |= dotBits[py][px];
							}
						}
					} else if (static_cast<std::size_t>(y) < windowLineCount) {
						std::size_t lineIndex = windowStartLine + scaledMidpoint(static_cast<std::size_t>(y), static_cast<std::size_t>(rowCount), windowLineCount);
						const MiniMapLineSample &sample = lineSampleAt(lineIndex);
						if (x < 64 && (sample.dotColumnBits & (1ULL << x)) != 0) pattern = 1;
					}
					rowPatterns[static_cast<std::size_t>(y * bodyWidth + x)] = pattern;
				}
			}
			return true;
		};
		const unsigned hardwareWorkers = std::max(1u, std::thread::hardware_concurrency());
		const unsigned workerCount = totalMiniMapCells >= 512 ? std::min<unsigned>(hardwareWorkers, static_cast<unsigned>(std::max(1, rowCount))) : 1u;
		if (workerCount == 1) {
			if (!renderRows(0, rowCount)) {
				result.status = mr::coprocessor::TaskStatus::Cancelled;
				return result;
			}
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
				if (!worker.get()) {
					result.status = mr::coprocessor::TaskStatus::Cancelled;
					return result;
				}
		}
		result.status = mr::coprocessor::TaskStatus::Completed;
		result.payload = std::make_shared<mr::coprocessor::MiniMapWarmupPayload>(useBraille, rowCount, bodyWidth, normalizedTotalLines, windowStartLine, windowLineCount, viewportWidth, std::move(rowPatterns), std::move(rowLineStarts), std::move(rowLineEnds));
		return result;
	});
	if (mMiniMapWarmupTaskId != previousTaskId) notifyWindowTaskStateChanged();
}

MRFileEditor::MiniMapOverlayState MRFileEditor::computeMiniMapOverlayState(const MRTextBufferModel::Range &selection, std::size_t totalLines, int viewportWidth, int miniMapBodyWidth, bool useBraille, const MREditSetupSettings &settings) const {
	MiniMapOverlayState overlay;
	const int dotColumns = useBraille ? std::max(1, miniMapBodyWidth * 2) : std::max(1, miniMapBodyWidth);
	const int normalizedViewportWidth = std::max(1, viewportWidth);
	const std::size_t length = mBufferModel.length();

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
			if (!nextDisplayChar(lineText, next, width, visualColumn, settings)) break;
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

	auto appendRangeMasks = [&](std::vector<MiniMapOverlayState::LineMask> &target, MRTextBufferModel::Range range) {
		range = range.clamped(length);
		if (range.end <= range.start) return;

		for (std::size_t lineStart = mBufferModel.lineStartByIndex(mBufferModel.lineIndex(range.start)); lineStart < range.end;) {
			const std::size_t lineIndex = mBufferModel.lineIndex(lineStart);
			if (lineIndex >= totalLines) break;
			const std::size_t nextLineStart = mBufferModel.nextLine(lineStart);
			const std::size_t lineEnd = std::min(nextLineStart, length);
			const std::size_t sliceStart = range.start > lineStart ? range.start - lineStart : 0;
			const std::size_t sliceEnd = range.end < lineEnd ? range.end - lineStart : lineEnd - lineStart;
			const std::uint64_t mask = rangeMaskForLineSlice(mBufferModel.lineText(lineStart), sliceStart, sliceEnd);
			if (mask != 0) target.push_back({lineIndex, mask});
			if (nextLineStart <= lineStart) break;
			lineStart = nextLineStart;
		}
	};

	if (selection.end > selection.start) appendRangeMasks(overlay.findLineMasks, selection.normalized());
	for (const MRTextBufferModel::Range &range : mFindMarkerRanges)
		appendRangeMasks(overlay.findLineMasks, range);
	for (const MRTextBufferModel::Range &range : mDirtyRanges)
		appendRangeMasks(overlay.dirtyLineMasks, range);
	normalizeMiniMapLineMasks(overlay.findLineMasks);
	normalizeMiniMapLineMasks(overlay.dirtyLineMasks);
	return overlay;
}

void MRFileEditor::drawMiniMapGutter(TDrawBuffer &b, int y, int miniMapRows, const TextViewportGeometry &viewport, std::size_t totalLines, std::size_t topLine, bool useBraille, const std::string &viewportMarkerGlyph, const MiniMapPalette &palette, const MiniMapOverlayState &overlay) {
	if (viewport.miniMapBodyWidth <= 0 || viewport.miniMapBodyX < 0 || viewport.miniMapInfoX < 0 || totalLines == 0 || miniMapRows <= 0) return;

	const std::array<std::string, 256> &glyphTable = brailleGlyphTable();
	const int bodyX = viewport.miniMapBodyX;
	const int bodyWidth = viewport.miniMapBodyWidth;
	const MiniMapSamplingWindow samplingWindow = miniMapSamplingWindowFor(totalLines, topLine, miniMapRows, useBraille);
	const bool cacheReady = miniMapCacheReadyForViewport(viewport, useBraille, samplingWindow);
	const bool stalePatternCacheUsable = !cacheReady && mMiniMapCache.bodyWidth == bodyWidth && mMiniMapCache.rowCount == miniMapRows && !mMiniMapCache.rowPatterns.empty();
	std::size_t rowLineStart = 0;
	std::size_t rowLineEnd = 0;

	if (y >= miniMapRows) {
		b.moveChar(static_cast<ushort>(bodyX), ' ', palette.normal, static_cast<ushort>(bodyWidth));
		if (viewport.miniMapSeparatorX >= 0 && viewport.miniMapSeparatorX < size.x) b.moveChar(static_cast<ushort>(viewport.miniMapSeparatorX), ' ', palette.normal, 1);
		b.moveChar(static_cast<ushort>(viewport.miniMapInfoX), ' ', palette.normal, 1);
		return;
	}

	if ((cacheReady || stalePatternCacheUsable) && static_cast<std::size_t>(y) < mMiniMapCache.rowLineStarts.size() && static_cast<std::size_t>(y) < mMiniMapCache.rowLineEnds.size()) {
		rowLineStart = mMiniMapCache.rowLineStarts[static_cast<std::size_t>(y)];
		rowLineEnd = mMiniMapCache.rowLineEnds[static_cast<std::size_t>(y)];
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
			if (index < mMiniMapCache.rowPatterns.size()) pattern = mMiniMapCache.rowPatterns[index];
		}
		bool cellFind = false;
		bool cellChanged = false;
		bool cellError = false;
		if (useBraille) {
			for (int py = 0; py < 4 && (!cellFind || !cellChanged); ++py) {
				std::size_t sampleRow = static_cast<std::size_t>(y * 4 + py);
				if (sampleRow >= samplingWindow.lineCount) continue;
				std::size_t lineIndex = samplingWindow.startLine + scaledMidpoint(sampleRow, static_cast<std::size_t>(std::max(miniMapRows * 4, 1)), samplingWindow.lineCount);
				const std::uint64_t findBits = miniMapLineMaskBits(overlay.findLineMasks, lineIndex);
				const std::uint64_t dirtyBits = miniMapLineMaskBits(overlay.dirtyLineMasks, lineIndex);
				if (!cellFind && miniMapCellHasOverlayBits(findBits, x, true)) cellFind = true;
				if (!cellChanged && miniMapCellHasOverlayBits(dirtyBits, x, true)) cellChanged = true;
			}
		} else {
			std::size_t lineIndex = samplingWindow.startLine + scaledMidpoint(static_cast<std::size_t>(y), static_cast<std::size_t>(std::max(miniMapRows, 1)), samplingWindow.lineCount);
			const std::uint64_t findBits = miniMapLineMaskBits(overlay.findLineMasks, lineIndex);
			const std::uint64_t dirtyBits = miniMapLineMaskBits(overlay.dirtyLineMasks, lineIndex);
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
		if (useBraille) b.moveStr(static_cast<ushort>(bodyX + x), glyphTable[pattern], cellColor, 1);
		else if (pattern != 0)
			b.moveStr(static_cast<ushort>(bodyX + x), "\xE2\x96\x88", cellColor, 1); // U+2588
		else
			b.moveChar(static_cast<ushort>(bodyX + x), ' ', cellColor, 1);
	}

	if (viewport.miniMapSeparatorX >= 0 && viewport.miniMapSeparatorX < size.x) b.moveChar(static_cast<ushort>(viewport.miniMapSeparatorX), ' ', palette.normal, 1);

	std::size_t clampedTopLine = std::min(topLine, totalLines - 1);
	std::size_t visibleLines = static_cast<std::size_t>(std::max(miniMapRows, 1));
	std::size_t viewportLineEnd = std::min(totalLines, clampedTopLine + visibleLines);
	bool markerVisible = false;
	if (rowLineEnd > rowLineStart) markerVisible = rowLineStart < viewportLineEnd && rowLineEnd > clampedTopLine;
	if (markerVisible) b.moveStr(static_cast<ushort>(viewport.miniMapInfoX), viewportMarkerGlyph, palette.viewport, 1);
	else
		b.moveChar(static_cast<ushort>(viewport.miniMapInfoX), ' ', palette.normal, 1);
}
