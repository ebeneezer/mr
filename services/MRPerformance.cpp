#include "MRPerformance.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <mutex>

namespace mr {
namespace performance {
namespace {

struct HeroState {
	bool active;
	HeroKind kind;
	std::string text;
	std::chrono::steady_clock::time_point expiresAt;

	HeroState() noexcept
	    : active(false), kind(HeroKind::Info), text(), expiresAt(std::chrono::steady_clock::time_point::min()) {
	}
};

struct State {
	std::mutex mutex;
	std::deque<Event> events;
	std::uint64_t nextSequence;
	HeroState hero;

	State() noexcept : mutex(), events(), nextSequence(1), hero() {
	}
};

static constexpr std::size_t kMaxEvents = 64;
static constexpr std::size_t kUiHeroBytesThreshold = 8u * 1024u * 1024u;
static constexpr std::size_t kWarmupHeroBytesThreshold = 64u * 1024u * 1024u;

State &state() {
	static State instance;
	return instance;
}

const char *baseNameOf(const std::string &path) {
	std::size_t pos = path.find_last_of("\\/");
	if (pos == std::string::npos)
		return path.c_str();
	return path.c_str() + pos + 1;
}

const char *outcomeLabel(Outcome outcome) {
	switch (outcome) {
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
	char buffer[32];
	std::tm *tmNow = std::localtime(&when);

	if (tmNow == nullptr)
		return "--:--:--";
	if (std::strftime(buffer, sizeof(buffer), "%H:%M:%S", tmNow) == 0)
		return "--:--:--";
	return buffer;
}

bool qualifiesForHero(const Event &event) {
	if (event.outcome != Outcome::Completed)
		return false;
	if (event.scope == Scope::Ui)
		return event.bytes >= kUiHeroBytesThreshold &&
		       (event.action == "Open file" || event.action == "Load file" || event.action == "Save file" ||
		        event.action == "Save file as");
	if (event.action == "Line index warmup")
		return event.bytes >= kWarmupHeroBytesThreshold;
	return false;
}

HeroKind heroKindFor(const Event &event) {
	if (event.outcome == Outcome::Failed)
		return HeroKind::Error;
	if (event.outcome == Outcome::Cancelled)
		return HeroKind::Warning;
	if (event.scope == Scope::Ui)
		return HeroKind::Success;
	return HeroKind::Info;
}

std::string buildHeroText(const Event &event) {
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

void recordUiEvent(const std::string &action, std::size_t bufferId, std::size_t documentId, std::size_t bytes,
                   double totalMs, const std::string &detail, Outcome outcome) {
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
	if (qualifiesForHero(shared.events.front())) {
		shared.hero.active = true;
		shared.hero.kind = heroKindFor(shared.events.front());
		shared.hero.text = buildHeroText(shared.events.front());
		shared.hero.expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(4);
	}
}

void recordBackgroundResult(const mr::coprocessor::Result &result, const std::string &action,
                            std::size_t bufferId, std::size_t documentId, std::size_t bytes,
                            const std::string &detail) {
	State &shared = state();
	std::lock_guard<std::mutex> lock(shared.mutex);
	Event event;

	event.scope = Scope::Background;
	event.outcome = result.failed() ? Outcome::Failed : (result.cancelled() ? Outcome::Cancelled : Outcome::Completed);
	event.lane = result.task.lane;
	event.action = action;
	event.detail = detail;
	event.bufferId = bufferId;
	event.documentId = documentId;
	event.bytes = bytes;
	event.queueMs = result.timing.queueMs();
	event.runMs = result.timing.runMs();
	event.totalMs = result.timing.totalMs();
	pushEvent(shared, event);
	if (qualifiesForHero(shared.events.front())) {
		shared.hero.active = true;
		shared.hero.kind = heroKindFor(shared.events.front());
		shared.hero.text = buildHeroText(shared.events.front());
		shared.hero.expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(4);
	}
}

std::vector<Event> recentForWindow(std::size_t bufferId, std::size_t documentId, std::size_t maxCount) {
	State &shared = state();
	std::vector<Event> result;
	std::lock_guard<std::mutex> lock(shared.mutex);

	for (std::deque<Event>::const_iterator it = shared.events.begin(); it != shared.events.end(); ++it) {
		bool matchesBuffer = bufferId != 0 && it->bufferId != 0 && it->bufferId == bufferId;
		bool matchesDocument = documentId != 0 && it->documentId != 0 && it->documentId == documentId;

		if (!matchesBuffer && !matchesDocument)
			continue;
		result.push_back(*it);
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

bool currentHeroNotice(HeroNotice &out) {
	State &shared = state();
	std::lock_guard<std::mutex> lock(shared.mutex);

	if (!shared.hero.active)
		return false;
	if (std::chrono::steady_clock::now() >= shared.hero.expiresAt) {
		shared.hero.active = false;
		shared.hero.text.clear();
		return false;
	}
	out.active = true;
	out.kind = shared.hero.kind;
	out.text = shared.hero.text;
	return true;
}

std::string formatDuration(double totalMs) {
	char buffer[64];

	if (totalMs < 1000.0) {
		std::snprintf(buffer, sizeof(buffer), "%.0f ms", totalMs);
		return buffer;
	}
	if (totalMs < 10000.0) {
		std::snprintf(buffer, sizeof(buffer), "%.2f s", totalMs / 1000.0);
		return buffer;
	}
	std::snprintf(buffer, sizeof(buffer), "%.1f s", totalMs / 1000.0);
	return buffer;
}

std::string formatThroughput(std::size_t bytes, double totalMs) {
	char buffer[64];
	double seconds = totalMs / 1000.0;
	double mibPerSecond;

	if (bytes == 0 || totalMs <= 0.0)
		return std::string();
	mibPerSecond = (static_cast<double>(bytes) / (1024.0 * 1024.0)) / seconds;
	if (!std::isfinite(mibPerSecond) || mibPerSecond <= 0.0)
		return std::string();
	std::snprintf(buffer, sizeof(buffer), "%.1f MiB/s", mibPerSecond);
	return buffer;
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
