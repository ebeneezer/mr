#include "MRPerformance.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string_view>

#include "../ui/MRMessageLineController.hpp"

namespace mr {
namespace performance {
namespace {

struct State {
	std::mutex mutex;
	std::deque<Event> events;
	std::uint64_t nextSequence;

	State() noexcept : nextSequence(1) {
	}
};

static constexpr std::size_t kMaxEvents = 64;
static constexpr std::size_t kUiNoticeBytesThreshold = 8u * 1024u * 1024u;
static constexpr std::size_t kWarmupNoticeBytesThreshold = 64u * 1024u * 1024u;

State &state() {
	static State instance;
	return instance;
}

std::string_view baseNameOf(std::string_view path) {
	std::size_t pos = path.find_last_of("\\/");
	if (pos == std::string_view::npos)
		return path;
	return path.substr(pos + 1);
}

const char *outcomeLabel(Outcome outcome) {
	switch (outcome) {
		case Outcome::Conflict:
			return "conflict";
		case Outcome::Cancelled:
			return "cancel";
		case Outcome::Failed:
			return "failed";
		case Outcome::Completed:
		default:
			return "ok";
	}
}

const char *scopeLabel(Scope scope) {
	return scope == Scope::Ui ? "UI" : "BG";
}

const char *laneLabel(mr::coprocessor::Lane lane) {
	switch (lane) {
		case mr::coprocessor::Lane::Io:
			return "io";
		case mr::coprocessor::Lane::Macro:
			return "macro";
		case mr::coprocessor::Lane::Compute:
		default:
			return "compute";
	}
}

std::string formatWallClock(std::time_t when) {
	std::array<char, 32> buffer{};
	std::tm *tmNow = std::localtime(&when);

	if (tmNow == nullptr)
		return "--:--:--";
	if (std::strftime(buffer.data(), buffer.size(), "%H:%M:%S", tmNow) == 0)
		return "--:--:--";
	return buffer.data();
}

bool qualifiesForNotice(const Event &event) {
	if (event.outcome != Outcome::Completed)
		return false;
	if (event.scope == Scope::Ui)
		return event.bytes >= kUiNoticeBytesThreshold &&
		       (event.action == "Open file" || event.action == "Load file" || event.action == "Save file" ||
		        event.action == "Save file as");
	if (event.action == "Line index warmup")
		return event.bytes >= kWarmupNoticeBytesThreshold;
	return false;
}

MessageNoticeKind messageNoticeKindFor(const Event &event) {
	if (event.outcome == Outcome::Failed)
		return MessageNoticeKind::Error;
	if (event.outcome == Outcome::Cancelled || event.outcome == Outcome::Conflict)
		return MessageNoticeKind::Warning;
	if (event.scope == Scope::Ui)
		return MessageNoticeKind::Success;
	return MessageNoticeKind::Info;
}

std::string buildMessageNoticeText(const Event &event) {
	std::string name = event.detail.empty() ? std::string("<buffer>") : std::string(baseNameOf(event.detail));

	if (event.action == "Open file")
		return "Opened " + name + " in " + formatDuration(event.totalMs) +
		       (event.bytes != 0 ? " (" + formatThroughput(event.bytes, event.totalMs) + ")" : std::string());
	if (event.action == "Load file")
		return "Loaded " + name + " in " + formatDuration(event.totalMs) +
		       (event.bytes != 0 ? " (" + formatThroughput(event.bytes, event.totalMs) + ")" : std::string());
	if (event.action == "Save file" || event.action == "Save file as")
		return "Saved " + name + " in " + formatDuration(event.totalMs) +
		       (event.bytes != 0 ? " (" + formatThroughput(event.bytes, event.totalMs) + ")" : std::string());
	if (event.action == "Line index warmup")
		return "Indexed " + name + " in " + formatDuration(event.totalMs);
	return std::string();
}

void pushEvent(State &shared, Event event) {
	event.sequence = shared.nextSequence++;
	event.wallClock = std::time(nullptr);
	shared.events.push_front(std::move(event));
	while (shared.events.size() > kMaxEvents)
		shared.events.pop_back();
}

} // namespace

void recordUiEvent(std::string_view action, std::size_t bufferId, std::size_t documentId, std::size_t bytes,
                   double totalMs, std::string_view detail, Outcome outcome) {
	State &shared = state();
	std::lock_guard<std::mutex> lock(shared.mutex);
	Event event;

	event.scope = Scope::Ui;
	event.outcome = outcome;
	event.lane = mr::coprocessor::Lane::Compute;
	event.action = action;
	event.detail = detail;
	event.bufferId = bufferId;
	event.documentId = documentId;
	event.bytes = bytes;
	event.runMs = totalMs;
	event.totalMs = totalMs;
	pushEvent(shared, event);
	if (qualifiesForNotice(shared.events.front()))
		mr::messageline::postTimed(mr::messageline::Owner::HeroEvent,
		                          buildMessageNoticeText(shared.events.front()),
		                          static_cast<mr::messageline::Kind>(messageNoticeKindFor(shared.events.front())),
		                          std::chrono::seconds(4), mr::messageline::kPriorityLow);
}

void recordBackgroundResult(const mr::coprocessor::Result &result, std::string_view action,
                            std::size_t bufferId, std::size_t documentId, std::size_t bytes,
                            std::string_view detail) {
	Outcome outcome =
	    result.failed() ? Outcome::Failed : (result.cancelled() ? Outcome::Cancelled : Outcome::Completed);
	recordBackgroundEvent(result.task.lane, outcome, result.timing, action, bufferId, documentId, bytes, detail);
}

void recordBackgroundEvent(mr::coprocessor::Lane lane, Outcome outcome, const mr::coprocessor::TaskTiming &timing,
                           std::string_view action, std::size_t bufferId, std::size_t documentId,
                           std::size_t bytes, std::string_view detail) {
	State &shared = state();
	std::lock_guard<std::mutex> lock(shared.mutex);
	Event event;

	event.scope = Scope::Background;
	event.outcome = outcome;
	event.lane = lane;
	event.action = action;
	event.detail = detail;
	event.bufferId = bufferId;
	event.documentId = documentId;
	event.bytes = bytes;
	event.queueMs = timing.queueMs();
	event.runMs = timing.runMs();
	event.totalMs = timing.totalMs();
	pushEvent(shared, event);
	if (qualifiesForNotice(shared.events.front()))
		mr::messageline::postTimed(mr::messageline::Owner::HeroEvent,
		                          buildMessageNoticeText(shared.events.front()),
		                          static_cast<mr::messageline::Kind>(messageNoticeKindFor(shared.events.front())),
		                          std::chrono::seconds(4), mr::messageline::kPriorityLow);
}

std::vector<Event> recentForWindow(std::size_t bufferId, std::size_t documentId, std::size_t maxCount) {
	State &shared = state();
	std::vector<Event> result;
	std::lock_guard<std::mutex> lock(shared.mutex);

	for (const auto & event : shared.events) {
		bool matchesBuffer = bufferId != 0 && event.bufferId != 0 && event.bufferId == bufferId;
		bool matchesDocument = documentId != 0 && event.documentId != 0 && event.documentId == documentId;

		if (!matchesBuffer && !matchesDocument)
			continue;
		result.push_back(event);
		if (result.size() >= maxCount)
			break;
	}
	return result;
}

std::vector<Event> recentGlobal(std::size_t maxCount) {
	State &shared = state();
	std::vector<Event> result;
	std::lock_guard<std::mutex> lock(shared.mutex);

	for (std::deque<Event>::const_iterator it = shared.events.begin();
	     it != shared.events.end() && result.size() < maxCount; ++it)
		result.push_back(*it);
	return result;
}

bool currentMessageLineNotice(MessageLineNotice &out) {
	mr::messageline::VisibleMessage message;

	if (!mr::messageline::currentOwnerMessage(mr::messageline::Owner::HeroEvent, message))
		return false;
	out.active = true;
	out.kind = static_cast<MessageNoticeKind>(message.kind);
	out.text = message.text;
	return true;
}

std::string formatDuration(double totalMs) {
	std::array<char, 64> buffer{};

	if (totalMs < 1000.0) {
		std::snprintf(buffer.data(), buffer.size(), "%.0f ms", totalMs);
		return buffer.data();
	}
	if (totalMs < 10000.0) {
		std::snprintf(buffer.data(), buffer.size(), "%.2f s", totalMs / 1000.0);
		return buffer.data();
	}
	std::snprintf(buffer.data(), buffer.size(), "%.1f s", totalMs / 1000.0);
	return buffer.data();
}

std::string formatThroughput(std::size_t bytes, double totalMs) {
	std::array<char, 64> buffer{};
	double seconds = totalMs / 1000.0;
	double mibPerSecond;

	if (bytes == 0 || totalMs <= 0.0)
		return std::string();
	mibPerSecond = (static_cast<double>(bytes) / (1024.0 * 1024.0)) / seconds;
	if (!std::isfinite(mibPerSecond) || mibPerSecond <= 0.0)
		return std::string();
	std::snprintf(buffer.data(), buffer.size(), "%.1f MiB/s", mibPerSecond);
	return buffer.data();
}

std::string formatEventLine(const Event &event) {
	std::string line = "[" + formatWallClock(event.wallClock) + "] ";

	line += scopeLabel(event.scope);
	line += " ";
	if (event.scope == Scope::Background) {
		line += laneLabel(event.lane);
		line += " ";
	}
	line += event.action;
	line += " ";
	line += outcomeLabel(event.outcome);
	line += " ";
	if (event.scope == Scope::Background)
		line += "q " + formatDuration(event.queueMs) + ", run " + formatDuration(event.runMs) + ", total " +
		        formatDuration(event.totalMs);
	else
		line += formatDuration(event.totalMs);
	if (event.bytes != 0) {
		std::string throughput = formatThroughput(event.bytes, event.totalMs);
		if (!throughput.empty()) {
			line += " ";
			line += throughput;
		}
	}
	if (!event.detail.empty()) {
		line += " ";
		line += baseNameOf(event.detail);
	}
	return line;
}

} // namespace performance
} // namespace mr
