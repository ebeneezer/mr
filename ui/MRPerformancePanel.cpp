#include "MRPerformancePanel.hpp"

#include "../coprocessor/MRCoprocessor.hpp"
#include "../coprocessor/MRPerformance.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const char *laneName(mr::coprocessor::Lane lane) noexcept {
	switch (lane) {
		case mr::coprocessor::Lane::Io:
			return "IO";
		case mr::coprocessor::Lane::MiniMap:
			return "MiniMap";
		case mr::coprocessor::Lane::Macro:
			return "Macro";
		case mr::coprocessor::Lane::Compute:
		default:
			return "Compute";
	}
}

const char *taskShortName(mr::coprocessor::TaskKind kind) noexcept {
	switch (kind) {
		case mr::coprocessor::TaskKind::LineIndexWarmup:
			return "IDX";
		case mr::coprocessor::TaskKind::SyntaxWarmup:
			return "SYN";
		case mr::coprocessor::TaskKind::FoldWarmup:
			return "FOLD";
		case mr::coprocessor::TaskKind::MiniMapWarmup:
			return "MAP";
		case mr::coprocessor::TaskKind::SaveNormalizationWarmup:
			return "SAVE";
		case mr::coprocessor::TaskKind::IndicatorBlink:
			return "BLINK";
		case mr::coprocessor::TaskKind::ExternalIo:
			return "IO";
		case mr::coprocessor::TaskKind::MacroJob:
			return "MAC";
		case mr::coprocessor::TaskKind::Custom:
		default:
			return "TASK";
	}
}

const char *eventShortName(const mr::performance::Event &event) noexcept {
	if (event.action == "Line index warming") return "IDX";
	if (event.action == "Syntax warming") return "SYN";
	if (event.action == "Fold warming") return "FOLD";
	if (event.action == "Mini map rendering") return "MAP";
	if (event.action == "Save normalization warming") return "SAVE";
	if (event.action == "External command") return "IO";
	if (event.action == "Background macro") return "MAC";
	return "TASK";
}

char eventMarker(const mr::performance::Event &event) noexcept {
	switch (event.outcome) {
		case mr::performance::Outcome::Cancelled:
			return 'x';
		case mr::performance::Outcome::Conflict:
		case mr::performance::Outcome::Failed:
			return '!';
		case mr::performance::Outcome::Completed:
		default:
			return ' ';
	}
}

std::string msText(std::uint64_t micros) {
	char buffer[32];
	const double ms = static_cast<double>(micros) / 1000.0;

	if (ms < 1000.0) {
		std::snprintf(buffer, sizeof(buffer), "%.0fms", ms);
		return buffer;
	}
	std::snprintf(buffer, sizeof(buffer), "%.1fs", ms / 1000.0);
	return buffer;
}

std::string fixedText(std::string text, std::size_t width) {
	for (char &ch : text)
		if (static_cast<unsigned char>(ch) < 32) ch = ' ';
	if (text.size() > width) {
		if (width > 1) text.resize(width - 1);
		text += "~";
		return text;
	}
	while (text.size() < width)
		text.push_back(' ');
	return text;
}

std::string fixedTaskBlock(const char *label, char marker, std::uint64_t micros, bool showDuration) {
	static constexpr std::size_t kBlockWidth = 12;
	std::string text;

	text = "[";
	text += marker;
	text += label;
	if (showDuration) {
		text += " ";
		text += msText(micros);
	}
	text = fixedText(text, kBlockWidth - 1);
	text += "]";
	return text;
}

void appendLimited(std::string &line, const std::string &text, std::size_t maxLen) {
	if (line.size() >= maxLen) return;
	std::size_t remaining = maxLen - line.size();
	if (text.size() <= remaining) {
		line += text;
		return;
	}
	if (remaining > 1) line += text.substr(0, remaining - 1);
	line += "~";
}

std::string queueBlocks(const mr::coprocessor::LaneSnapshot &lane, unsigned frame) {
	static const char *kPhase[] = {"   ", " > ", " >>", ">>>"};
	static constexpr std::size_t kFieldWidth = 70;
	std::string line;
	const std::size_t limit = std::min<std::size_t>(lane.queuedTasks.size(), 5);

	line += kPhase[frame % 4];
	if (lane.queuedTasks.empty()) {
		line += fixedText(" idle", kFieldWidth - line.size());
		return line;
	}
	for (std::size_t i = 0; i < limit; ++i) {
		if (i != 0) line += " ";
		line += fixedTaskBlock(taskShortName(lane.queuedTasks[i].kind), ' ', 0, false);
	}
	if (lane.queuedTasks.size() > limit) {
		line += " ";
		line += fixedText("+" + std::to_string(lane.queuedTasks.size() - limit), 4);
	}
	return fixedText(line, kFieldWidth);
}

std::string workerBlocks(const mr::coprocessor::LaneSnapshot &lane) {
	static constexpr std::size_t kWorkerFieldWidth = 18;
	std::string line;

	for (std::size_t workerSlot = 0; workerSlot < lane.workerCount; ++workerSlot) {
		const mr::coprocessor::ActiveTaskSnapshot *activeTask = nullptr;
		std::string block;

		for (const mr::coprocessor::ActiveTaskSnapshot &candidate : lane.activeTasks) {
			if (candidate.workerSlot != workerSlot) continue;
			activeTask = &candidate;
			break;
		}
		if (!line.empty()) line += " ";
		line += "W";
		line += std::to_string(workerSlot + 1);
		line += " ";
		if (activeTask == nullptr) {
			line += fixedText("idle", kWorkerFieldWidth - 3);
			continue;
		}
		block = fixedTaskBlock(taskShortName(activeTask->task.kind), '*', activeTask->runMicros, true);
		line += fixedText(block, kWorkerFieldWidth - 3);
	}
	return line;
}

std::string completedBlocksForLane(mr::coprocessor::Lane lane, const std::vector<mr::performance::Event> &events) {
	static constexpr std::size_t kMaxBlocks = 3;
	std::string line;
	std::size_t count = 0;

	for (const mr::performance::Event &event : events) {
		if (event.scope != mr::performance::Scope::Background || event.lane != lane) continue;
		if (!line.empty()) line += " ";
		line += fixedTaskBlock(eventShortName(event), eventMarker(event), static_cast<std::uint64_t>(event.totalMs * 1000.0), true);
		++count;
		if (count >= kMaxBlocks) break;
	}
	return line;
}

std::string compactRecent(const std::vector<mr::performance::Event> &events, std::size_t maxLen) {
	std::string line;
	const std::size_t limit = std::min<std::size_t>(events.size(), 4);

	if (events.empty()) return "none";
	for (std::size_t i = 0; i < limit; ++i) {
		std::string part;
		if (i != 0) part += " | ";
		part += events[i].action;
		part += " ";
		part += mr::performance::formatDuration(events[i].totalMs);
		appendLimited(line, part, maxLen);
		if (line.size() >= maxLen) return line;
	}
	return line;
}

} // namespace

MRPerformancePanel::MRPerformancePanel(const TRect &bounds) noexcept : TView(bounds), mAnimationFrame(0) {
	options |= ofBuffered;
	options &= ~ofSelectable;
	eventMask = 0;
}

void MRPerformancePanel::setAnimationFrame(unsigned frame) noexcept {
	if (mAnimationFrame == frame) return;
	mAnimationFrame = frame;
	drawView();
}

void MRPerformancePanel::draw() {
	const mr::coprocessor::Snapshot snapshot = mr::coprocessor::globalCoprocessor().snapshot();
	const std::vector<mr::performance::Event> recent = mr::performance::recentGlobal(12);
	const TColorAttr text = 0x70;
	const TColorAttr header = text;
	const TColorAttr laneColor = text;
	const TColorAttr recentColor = text;
	const TColorAttr queueColor = 0x30;
	const TColorAttr workerColor = 0x20;
	const TColorAttr completedColor = 0x60;
	std::string line;
	int y = 0;

	line = " PERF ";
	line += " results:";
	line += std::to_string(snapshot.pendingResults);
	line += "  recent: ";
	line += compactRecent(recent, static_cast<std::size_t>(std::max(0, size.x - 26)));
	writePanelLine(y++, line, header);

	for (const mr::coprocessor::LaneSnapshot &lane : snapshot.lanes) {
		std::string queue = queueBlocks(lane, mAnimationFrame);
		std::string workers = workerBlocks(lane);
		std::string completed = completedBlocksForLane(lane.lane, recent);
		std::vector<PanelSegment> segments;

		line = laneName(lane.lane);
		while (line.size() < 8)
			line.push_back(' ');
		line += " q:";
		line += std::to_string(lane.queuedTasks.size());
		line += " workers:";
		line += std::to_string(lane.activeTasks.size());
		line += "/";
		line += std::to_string(lane.workerCount);
		line += " ";
		segments.push_back(PanelSegment{line, laneColor});
		segments.push_back(PanelSegment{queue, queueColor});
		segments.push_back(PanelSegment{"  ", laneColor});
		segments.push_back(PanelSegment{workers, workerColor});
		if (!completed.empty()) {
			segments.push_back(PanelSegment{"  ", laneColor});
			segments.push_back(PanelSegment{completed, completedColor});
		}
		writePanelSegments(y++, segments, laneColor);
		if (y >= size.y) break;
	}

	for (std::size_t i = 0; y < size.y && i < recent.size(); ++i) {
		writePanelLine(y++, mr::performance::formatEventLine(recent[i]), recentColor);
	}
	while (y < size.y)
		writePanelLine(y++, std::string(), text);
}

TPalette &MRPerformancePanel::getPalette() const {
	static const TColorAttr paletteData[] = {0x70, 0x70, 0x70, 0x70};
	static TPalette palette(paletteData, 4);
	return palette;
}

void MRPerformancePanel::writePanelLine(int y, const std::string &line, TColorAttr color) {
	TDrawBuffer buffer;
	std::string text = line;

	if (y < 0 || y >= size.y) return;
	for (char &ch : text)
		if (static_cast<unsigned char>(ch) < 32) ch = ' ';
	if (text.size() > static_cast<std::size_t>(std::max(0, size.x))) text.resize(static_cast<std::size_t>(std::max(0, size.x)));
	buffer.moveChar(0, ' ', color, size.x);
	if (!text.empty()) buffer.moveStr(0, text.c_str(), color);
	writeBuf(0, y, size.x, 1, buffer);
}

void MRPerformancePanel::writePanelSegments(int y, const std::vector<PanelSegment> &segments, TColorAttr fillColor) {
	TDrawBuffer buffer;
	short x = 0;

	if (y < 0 || y >= size.y) return;
	buffer.moveChar(0, ' ', fillColor, size.x);
	for (const PanelSegment &segment : segments) {
		std::string text = segment.text;
		if (x >= size.x) break;
		for (char &ch : text)
			if (static_cast<unsigned char>(ch) < 32) ch = ' ';
		if (text.size() > static_cast<std::size_t>(size.x - x)) text.resize(static_cast<std::size_t>(size.x - x));
		if (!text.empty()) {
			buffer.moveStr(x, text.c_str(), segment.color);
			x = static_cast<short>(x + text.size());
		}
	}
	writeBuf(0, y, size.x, 1, buffer);
}
