#include "MRPerformance.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string_view>

#include "../mrmac/mrmac.h"
#include "../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../mrmac/vm/MRVMRuntimeState.hpp"
#include "../mrmac/vm/MRVMValue.hpp"
#include "../ui/MRMessageLineController.hpp"

namespace mr {
namespace performance {
namespace {

using Value = VirtualMachine::Value;

static constexpr std::size_t kMaxEvents = 64;

Value performanceRoot(MRVMRuntimeKv &runtimeKv) {
	Value applicationUi = runtimeKv.ensureRoot("APPLICATIONUI");
	return runtimeKv.ensureChild(applicationUi, "performance");
}

Value performanceEvents(MRVMRuntimeKv &runtimeKv) {
	return runtimeKv.ensureChild(performanceRoot(runtimeKv), "events");
}

bool readValue(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key, Value &value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, parent, key)) return false;
	value = mrvmHashReadValue(store, store, parent, key);
	return true;
}

int readInt(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key, int fallback = 0) {
	Value value;

	if (!readValue(runtimeKv, parent, key, value) || value.type != TYPE_INT) return fallback;
	return value.i;
}

std::uint64_t parseUnsigned(const std::string &text) {
	char *end = nullptr;
	const unsigned long long value = std::strtoull(text.c_str(), &end, 10);

	return end != text.c_str() && *end == '\0' ? static_cast<std::uint64_t>(value) : 0;
}

std::uint64_t readUnsigned(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key) {
	Value value;

	if (!readValue(runtimeKv, parent, key, value)) return 0;
	if (value.type == TYPE_INT) return value.i > 0 ? static_cast<std::uint64_t>(value.i) : 0;
	if (value.type == TYPE_STR) return parseUnsigned(value.s);
	return 0;
}

double readReal(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key) {
	Value value;

	if (!readValue(runtimeKv, parent, key, value)) return 0.0;
	if (value.type == TYPE_REAL) return value.r;
	if (value.type == TYPE_INT) return value.i;
	return 0.0;
}

std::string readString(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key) {
	Value value;

	if (!readValue(runtimeKv, parent, key, value) || value.type != TYPE_STR) return std::string();
	return value.s;
}

void writeInt(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeInt(value));
}

void writeUnsigned(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key, std::uint64_t value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(std::to_string(value)));
}

void writeReal(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key, double value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeReal(value));
}

void writeString(MRVMRuntimeKv &runtimeKv, const Value &parent, const char *key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(value));
}

void writeEvent(MRVMRuntimeKv &runtimeKv, const Value &parent, const Event &event) {
	writeUnsigned(runtimeKv, parent, "sequence", event.sequence);
	writeUnsigned(runtimeKv, parent, "wallClock", static_cast<std::uint64_t>(event.wallClock));
	writeInt(runtimeKv, parent, "scope", static_cast<int>(event.scope));
	writeInt(runtimeKv, parent, "outcome", static_cast<int>(event.outcome));
	writeInt(runtimeKv, parent, "lane", static_cast<int>(event.lane));
	writeString(runtimeKv, parent, "action", event.action);
	writeString(runtimeKv, parent, "detail", event.detail);
	writeUnsigned(runtimeKv, parent, "bufferId", event.bufferId);
	writeUnsigned(runtimeKv, parent, "documentId", event.documentId);
	writeUnsigned(runtimeKv, parent, "bytes", event.bytes);
	writeReal(runtimeKv, parent, "queueMs", event.queueMs);
	writeReal(runtimeKv, parent, "runMs", event.runMs);
	writeReal(runtimeKv, parent, "totalMs", event.totalMs);
	writeInt(runtimeKv, parent, "derivedStateApplied", event.derivedStateApplied ? 1 : 0);
}

Event readEvent(MRVMRuntimeKv &runtimeKv, const Value &parent) {
	Event event;

	event.sequence = readUnsigned(runtimeKv, parent, "sequence");
	event.wallClock = static_cast<std::time_t>(readUnsigned(runtimeKv, parent, "wallClock"));
	event.scope = static_cast<Scope>(readInt(runtimeKv, parent, "scope"));
	event.outcome = static_cast<Outcome>(readInt(runtimeKv, parent, "outcome"));
	event.lane = static_cast<mr::coprocessor::Lane>(readInt(runtimeKv, parent, "lane", static_cast<int>(mr::coprocessor::Lane::Compute)));
	event.action = readString(runtimeKv, parent, "action");
	event.detail = readString(runtimeKv, parent, "detail");
	event.bufferId = static_cast<std::size_t>(readUnsigned(runtimeKv, parent, "bufferId"));
	event.documentId = static_cast<std::size_t>(readUnsigned(runtimeKv, parent, "documentId"));
	event.bytes = static_cast<std::size_t>(readUnsigned(runtimeKv, parent, "bytes"));
	event.queueMs = readReal(runtimeKv, parent, "queueMs");
	event.runMs = readReal(runtimeKv, parent, "runMs");
	event.totalMs = readReal(runtimeKv, parent, "totalMs");
	event.derivedStateApplied = readInt(runtimeKv, parent, "derivedStateApplied") != 0;
	return event;
}

std::vector<std::uint64_t> eventSequences(MRVMRuntimeKv &runtimeKv, const Value &events) {
	std::vector<std::uint64_t> sequences;
	const std::vector<std::string> keys = runtimeKv.globalStore().keys(events.hashHandle);

	sequences.reserve(keys.size());
	for (const std::string &key : keys) {
		const std::uint64_t sequence = parseUnsigned(key);
		if (sequence != 0) sequences.push_back(sequence);
	}
	std::sort(sequences.begin(), sequences.end(), std::greater<std::uint64_t>());
	return sequences;
}

std::vector<Event> recentEvents(std::size_t maxCount) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value applicationUi;
	Value performance;
	Value events;
	std::vector<Event> result;

	if (!runtimeKv.findRoot("APPLICATIONUI", applicationUi) || !runtimeKv.findChild(applicationUi, "performance", performance) || !runtimeKv.findChild(performance, "events", events)) return result;
	const std::vector<std::uint64_t> sequences = eventSequences(runtimeKv, events);
	result.reserve(std::min(maxCount, sequences.size()));
	for (std::size_t i = 0; i < sequences.size() && result.size() < maxCount; ++i) {
		Value eventNode;
		if (!runtimeKv.findChild(events, std::to_string(sequences[i]), eventNode)) continue;
		result.push_back(readEvent(runtimeKv, eventNode));
	}
	return result;
}

std::string_view leafNameOf(std::string_view path) {
	std::size_t pos = path.find_last_of("\\/");
	if (pos == std::string_view::npos) return path;
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
		case mr::coprocessor::Lane::MiniMap:
			return "minimap";
		case mr::coprocessor::Lane::Macro:
			return "macro";
		case mr::coprocessor::Lane::Extern:
			return "extern";
		case mr::coprocessor::Lane::Compute:
		default:
			return "compute";
	}
}

std::string formatWallClock(std::time_t when) {
	std::array<char, 32> buffer{};
	std::tm *tmNow = std::localtime(&when);

	if (tmNow == nullptr) return "--:--:--";
	if (std::strftime(buffer.data(), buffer.size(), "%H:%M:%S", tmNow) == 0) return "--:--:--";
	return buffer.data();
}

void appendEvent(Event event) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	const Value root = performanceRoot(runtimeKv);
	const Value events = performanceEvents(runtimeKv);
	const std::uint64_t sequence = readUnsigned(runtimeKv, root, "nextSequence") + 1;

	event.sequence = sequence;
	event.wallClock = std::time(nullptr);
	writeUnsigned(runtimeKv, root, "nextSequence", sequence);
	writeEvent(runtimeKv, runtimeKv.replaceChild(events, std::to_string(sequence)), event);
	const std::vector<std::uint64_t> sequences = eventSequences(runtimeKv, events);
	for (std::size_t i = kMaxEvents; i < sequences.size(); ++i)
		static_cast<void>(runtimeKv.eraseChild(events, std::to_string(sequences[i])));
}

} // namespace

void recordUiEvent(std::string_view action, std::size_t bufferId, std::size_t documentId, std::size_t bytes, double totalMs, std::string_view detail, Outcome outcome) {
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
	appendEvent(event);
}

void recordBackgroundResult(const mr::coprocessor::Result &result, std::string_view action, std::size_t bufferId, std::size_t documentId, std::size_t bytes, std::string_view detail) {
	Outcome outcome = result.failed() ? Outcome::Failed : (result.cancelled() ? Outcome::Cancelled : Outcome::Completed);
	recordBackgroundEvent(result.task.lane, outcome, result.timing, action, bufferId, documentId, bytes, detail);
}

void recordBackgroundEvent(mr::coprocessor::Lane lane, Outcome outcome, const mr::coprocessor::TaskTiming &timing, std::string_view action, std::size_t bufferId, std::size_t documentId, std::size_t bytes, std::string_view detail) {
	recordBackgroundEvent(lane, outcome, timing, action, bufferId, documentId, bytes, detail, false);
}

void recordBackgroundEvent(mr::coprocessor::Lane lane, Outcome outcome, const mr::coprocessor::TaskTiming &timing, std::string_view action, std::size_t bufferId, std::size_t documentId, std::size_t bytes,
                           std::string_view detail, bool derivedStateApplied) {
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
	event.derivedStateApplied = derivedStateApplied;
	appendEvent(event);
}

std::vector<Event> recentForWindow(std::size_t bufferId, std::size_t documentId, std::size_t maxCount) {
	const std::vector<Event> events = recentEvents(kMaxEvents);
	std::vector<Event> result;

	for (const Event &event : events) {
		bool matchesBuffer = bufferId != 0 && event.bufferId != 0 && event.bufferId == bufferId;
		bool matchesDocument = documentId != 0 && event.documentId != 0 && event.documentId == documentId;

		if (!matchesBuffer && !matchesDocument) continue;
		result.push_back(event);
		if (result.size() >= maxCount) break;
	}
	return result;
}

std::vector<Event> recentGlobal(std::size_t maxCount) {
	return recentEvents(maxCount);
}

bool currentMessageLineNotice(MessageLineNotice &out) {
	mr::messageline::VisibleMessage message;

	if (!mr::messageline::currentOwnerMessage(mr::messageline::Owner::HeroEvent, message)) return false;
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

	if (bytes == 0 || totalMs <= 0.0) return std::string();
	mibPerSecond = (static_cast<double>(bytes) / (1024.0 * 1024.0)) / seconds;
	if (!std::isfinite(mibPerSecond) || mibPerSecond <= 0.0) return std::string();
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
	if (event.scope == Scope::Background) line += "q " + formatDuration(event.queueMs) + ", run " + formatDuration(event.runMs) + ", total " + formatDuration(event.totalMs);
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
