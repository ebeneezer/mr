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

struct EventStore {
	std::mutex mutex;
	std::deque<Event> events;
	std::uint64_t nextSequence;

	EventStore() noexcept : nextSequence(1) {
	}
};

static constexpr std::size_t kMaxEvents = 64;

EventStore &eventStore() {
	static EventStore instance;
	return instance;
}

std::string_view leafNameOf(std::string_view path) {
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

void appendEvent(EventStore &store, Event event) {
	event.sequence = store.nextSequence++;
	event.wallClock = std::time(nullptr);
	store.events.push_front(std::move(event));
	while (store.events.size() > kMaxEvents)
		store.events.pop_back();
}

} // namespace

void recordUiEvent(std::string_view action, std::size_t bufferId, std::size_t documentId, std::size_t bytes,
                   double totalMs, std::string_view detail, Outcome outcome) {
	EventStore &store = eventStore();
	std::lock_guard<std::mutex> lock(store.mutex);
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
	appendEvent(store, event);
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
	EventStore &store = eventStore();
	std::lock_guard<std::mutex> lock(store.mutex);
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
	appendEvent(store, event);
}

std::vector<Event> recentForWindow(std::size_t bufferId, std::size_t documentId, std::size_t maxCount) {
	EventStore &store = eventStore();
	std::vector<Event> result;
	std::lock_guard<std::mutex> lock(store.mutex);

	for (const auto & event : store.events) {
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
	EventStore &store = eventStore();
	std::vector<Event> result;
	std::lock_guard<std::mutex> lock(store.mutex);

	for (std::deque<Event>::const_iterator it = store.events.begin();
	     it != store.events.end() && result.size() < maxCount; ++it)
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
		line += leafNameOf(event.detail);
	}
	return line;
}

} // namespace performance
} // namespace mr
