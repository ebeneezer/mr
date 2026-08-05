#define Uses_TMenuBar
#define Uses_TProgram
#define Uses_TStatusLine
#include <tvision/tv.h>

#include "../config/settings/MRSettingsRuntime.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../mrmac/vm/MRVMValue.hpp"
#include "MRMessageLineController.hpp"
#include "MRMenuBar.hpp"
#include "MRStatusLine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <string_view>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace mr {
namespace messageline {
namespace {

constexpr std::size_t kOwnerCount = static_cast<std::size_t>(Owner::WorkspaceRestore) + 1;
constexpr const char *kApplicationUiRoot = "APPLICATIONUI";
constexpr const char *kMessageLineBranch = "messageLine";
constexpr const char *kStaticModeKey = "staticMode";
constexpr const char *kSlotsBranch = "slots";

struct Slot {
	bool active = false;
	Kind kind = Kind::Info;
	std::string text;
	std::vector<VisibleMessage::Segment> segments;
	std::chrono::steady_clock::time_point expiresAt = std::chrono::steady_clock::time_point::max();
	bool timed = false;
	int priority = 0;
	Token token = 0;
	std::uint64_t sequence = 0;
};

std::mutex &stateMutex() {
	static std::mutex mutex;
	return mutex;
}

std::size_t ownerIndex(Owner owner) {
	return static_cast<std::size_t>(owner);
}

bool validOwner(Owner owner) {
	const std::size_t index = ownerIndex(owner);
	return index < kOwnerCount;
}

VirtualMachine::Value messageLineRoot(MRVMRuntimeKv &runtimeKv) {
	VirtualMachine::Value applicationUi = runtimeKv.ensureRoot(kApplicationUiRoot);
	return runtimeKv.ensureChild(applicationUi, kMessageLineBranch);
}

bool readValue(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, VirtualMachine::Value &stored) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, parent, key)) return false;
	stored = mrvmHashReadValue(store, store, parent, key);
	return true;
}

int readInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, int fallback) {
	VirtualMachine::Value stored;

	if (!readValue(runtimeKv, parent, key, stored) || stored.type != TYPE_INT) return fallback;
	return stored.i;
}

std::uint64_t readUnsigned(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, std::uint64_t fallback) {
	VirtualMachine::Value stored;

	if (!readValue(runtimeKv, parent, key, stored) || stored.type != TYPE_STR) return fallback;
	try {
		std::size_t consumed = 0;
		const unsigned long long parsed = std::stoull(stored.s, &consumed, 10);
		if (consumed != stored.s.size()) return fallback;
		return static_cast<std::uint64_t>(parsed);
	} catch (...) {
		return fallback;
	}
}

std::string readString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key) {
	VirtualMachine::Value stored;

	if (!readValue(runtimeKv, parent, key, stored) || stored.type != TYPE_STR) return std::string();
	return stored.s;
}

void writeInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeInt(value));
}

void writeUnsigned(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, std::uint64_t value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(std::to_string(value)));
}

void writeString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(value));
}

VirtualMachine::Value slotRoot(MRVMRuntimeKv &runtimeKv, Owner owner) {
	VirtualMachine::Value root = messageLineRoot(runtimeKv);
	VirtualMachine::Value slots = runtimeKv.ensureChild(root, kSlotsBranch);
	return runtimeKv.ensureChild(slots, std::to_string(ownerIndex(owner)));
}

bool findSlotRoot(MRVMRuntimeKv &runtimeKv, Owner owner, VirtualMachine::Value &slot) {
	VirtualMachine::Value root;
	VirtualMachine::Value slots;

	if (!runtimeKv.findRoot(kApplicationUiRoot, root) || !runtimeKv.findChild(root, kMessageLineBranch, root) || !runtimeKv.findChild(root, kSlotsBranch, slots)) return false;
	return runtimeKv.findChild(slots, std::to_string(ownerIndex(owner)), slot);
}

void readSlot(MRVMRuntimeKv &runtimeKv, Owner owner, Slot &slot) {
	VirtualMachine::Value stored;

	slot = Slot();
	if (!findSlotRoot(runtimeKv, owner, stored)) return;
	slot.active = readInt(runtimeKv, stored, "active", 0) != 0;
	slot.kind = static_cast<Kind>(readInt(runtimeKv, stored, "kind", static_cast<int>(Kind::Info)));
	slot.text = readString(runtimeKv, stored, "text");
	slot.timed = readInt(runtimeKv, stored, "timed", 0) != 0;
	const std::uint64_t expiresAtMs = readUnsigned(runtimeKv, stored, "expiresAtMs", 0);
	slot.expiresAt = slot.timed ? std::chrono::steady_clock::time_point(std::chrono::milliseconds(expiresAtMs)) : std::chrono::steady_clock::time_point::max();
	slot.priority = readInt(runtimeKv, stored, "priority", 0);
	slot.token = readUnsigned(runtimeKv, stored, "token", 0);
	slot.sequence = readUnsigned(runtimeKv, stored, "sequence", 0);

	VirtualMachine::Value segments;
	if (!runtimeKv.findChild(stored, "segments", segments)) return;
	const int segmentCount = readInt(runtimeKv, segments, "count", 0);
	for (int index = 0; index < segmentCount; ++index) {
		VirtualMachine::Value segment;
		if (!runtimeKv.findChild(segments, std::to_string(index), segment)) continue;
		VisibleMessage::Segment value;
		value.kind = static_cast<Kind>(readInt(runtimeKv, segment, "kind", static_cast<int>(slot.kind)));
		value.text = readString(runtimeKv, segment, "text");
		slot.segments.push_back(value);
	}
}

void writeSlot(MRVMRuntimeKv &runtimeKv, Owner owner, const Slot &slot) {
	VirtualMachine::Value stored = slotRoot(runtimeKv, owner);
	VirtualMachine::Value segments = runtimeKv.replaceChild(stored, "segments");

	writeInt(runtimeKv, stored, "active", slot.active ? 1 : 0);
	writeInt(runtimeKv, stored, "kind", static_cast<int>(slot.kind));
	writeString(runtimeKv, stored, "text", slot.text);
	writeInt(runtimeKv, stored, "timed", slot.timed ? 1 : 0);
	writeUnsigned(runtimeKv, stored, "expiresAtMs", slot.timed ? static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(slot.expiresAt.time_since_epoch()).count()) : 0);
	writeInt(runtimeKv, stored, "priority", slot.priority);
	writeUnsigned(runtimeKv, stored, "token", slot.token);
	writeUnsigned(runtimeKv, stored, "sequence", slot.sequence);
	writeInt(runtimeKv, segments, "count", static_cast<int>(slot.segments.size()));
	for (std::size_t index = 0; index < slot.segments.size(); ++index) {
		VirtualMachine::Value segment = runtimeKv.ensureChild(segments, std::to_string(index));
		writeInt(runtimeKv, segment, "kind", static_cast<int>(slot.segments[index].kind));
		writeString(runtimeKv, segment, "text", slot.segments[index].text);
	}
}

std::uint64_t takeCounter(MRVMRuntimeKv &runtimeKv, const char *key) {
	VirtualMachine::Value root = messageLineRoot(runtimeKv);
	const std::uint64_t value = readUnsigned(runtimeKv, root, key, 1);
	writeUnsigned(runtimeKv, root, key, value + 1);
	return value;
}

bool messageLineEnabled(MRVMRuntimeKv &runtimeKv) {
	return readInt(runtimeKv, messageLineRoot(runtimeKv), "enabled", 1) != 0;
}

std::chrono::milliseconds minimumDurationForKind(Kind kind) {
	switch (kind) {
		case Kind::Warning:
			return std::chrono::seconds(5);
		case Kind::Error:
			return std::chrono::seconds(7);
		default:
			return std::chrono::milliseconds(0);
	}
}

std::chrono::milliseconds clampDurationForKind(Kind kind, std::chrono::milliseconds duration) {
	return std::max(duration, minimumDurationForKind(kind));
}

void expireLocked(MRVMRuntimeKv &runtimeKv, std::chrono::steady_clock::time_point now) {
	for (std::size_t index = 0; index < kOwnerCount; ++index) {
		const Owner owner = static_cast<Owner>(index);
		Slot slot;
		readSlot(runtimeKv, owner, slot);
		if (slot.active && slot.timed && now >= slot.expiresAt) {
			slot.active = false;
			slot.text.clear();
			slot.segments.clear();
			slot.timed = false;
			slot.expiresAt = std::chrono::steady_clock::time_point::max();
			slot.priority = 0;
			writeSlot(runtimeKv, owner, slot);
		}
	}
}

bool exportSlot(const Slot &slot, VisibleMessage &out) {
	if (!slot.active || slot.text.empty()) return false;
	out.active = true;
	out.kind = slot.kind;
	out.text = slot.text;
	out.segments = slot.segments;
	return true;
}

bool staticModeActiveLocked(MRVMRuntimeKv &runtimeKv) {
	VirtualMachine::Value applicationUi;
	VirtualMachine::Value messageLine;
	VirtualMachine::Value stored;
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!runtimeKv.findRoot(kApplicationUiRoot, applicationUi) || !runtimeKv.findChild(applicationUi, kMessageLineBranch, messageLine)) return false;
	if (!mrvmHashContainsValue(store, store, messageLine, kStaticModeKey)) return false;
	stored = mrvmHashReadValue(store, store, messageLine, kStaticModeKey);
	return stored.type == TYPE_INT && stored.i != 0;
}

void storeStaticModeLocked(MRVMRuntimeKv &runtimeKv, bool active) {
	VirtualMachine::Value applicationUi = runtimeKv.ensureRoot(kApplicationUiRoot);
	VirtualMachine::Value messageLine = runtimeKv.ensureChild(applicationUi, kMessageLineBranch);
	VirtualMachine::Value stored;
	MRVMHashStore &store = runtimeKv.globalStore();

	stored.type = TYPE_INT;
	stored.i = active ? 1 : 0;
	mrvmHashWriteValue(store, store, messageLine, kStaticModeKey, stored);
}

void clearSlotsLocked(MRVMRuntimeKv &runtimeKv) {
	for (std::size_t index = 0; index < kOwnerCount; ++index) {
		Slot slot;
		slot.active = false;
		slot.text.clear();
		slot.segments.clear();
		slot.timed = false;
		slot.expiresAt = std::chrono::steady_clock::time_point::max();
		slot.priority = 0;
		slot.token = takeCounter(runtimeKv, "nextToken");
		slot.sequence = takeCounter(runtimeKv, "nextSequence");
		writeSlot(runtimeKv, static_cast<Owner>(index), slot);
	}
}

} // namespace

Token postTimed(Owner owner, std::string_view text, Kind kind, std::chrono::milliseconds duration, int priority) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	std::lock_guard<std::mutex> lock(stateMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	const auto now = std::chrono::steady_clock::now();
	Slot slot;

	if (!validOwner(owner) || !messageLineEnabled(runtimeKv) || staticModeActiveLocked(runtimeKv)) return 0;
	expireLocked(runtimeKv, now);
	readSlot(runtimeKv, owner, slot);
	duration = text.empty() ? std::chrono::milliseconds(0) : clampDurationForKind(kind, duration);
	slot.active = !text.empty();
	slot.kind = kind;
	slot.text = text;
	slot.segments.clear();
	slot.timed = !text.empty();
	slot.expiresAt = text.empty() ? std::chrono::steady_clock::time_point::max() : now + duration;
	slot.priority = text.empty() ? 0 : priority;
	slot.token = takeCounter(runtimeKv, "nextToken");
	slot.sequence = takeCounter(runtimeKv, "nextSequence");
	writeSlot(runtimeKv, owner, slot);
	return slot.token;
}

Token postTimedSegments(Owner owner, const std::vector<VisibleMessage::Segment> &segments, Kind kind, std::chrono::milliseconds duration, int priority) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	std::lock_guard<std::mutex> lock(stateMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	const auto now = std::chrono::steady_clock::now();
	Slot slot;
	std::string text;

	if (!validOwner(owner) || !messageLineEnabled(runtimeKv) || staticModeActiveLocked(runtimeKv)) return 0;
	for (const VisibleMessage::Segment &segment : segments)
		text += segment.text;

	expireLocked(runtimeKv, now);
	duration = text.empty() ? std::chrono::milliseconds(0) : duration;
	slot.active = !text.empty();
	slot.kind = kind;
	slot.text = text;
	slot.segments = segments;
	slot.timed = !text.empty();
	slot.expiresAt = text.empty() ? std::chrono::steady_clock::time_point::max() : now + duration;
	slot.priority = text.empty() ? 0 : priority;
	slot.token = takeCounter(runtimeKv, "nextToken");
	slot.sequence = takeCounter(runtimeKv, "nextSequence");
	writeSlot(runtimeKv, owner, slot);
	return slot.token;
}

Token postSticky(Owner owner, std::string_view text, Kind kind, int priority) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	std::lock_guard<std::mutex> lock(stateMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Slot slot;

	if (!validOwner(owner) || !messageLineEnabled(runtimeKv) || staticModeActiveLocked(runtimeKv)) return 0;
	slot.active = !text.empty();
	slot.kind = kind;
	slot.text = text;
	slot.segments.clear();
	slot.timed = false;
	slot.expiresAt = std::chrono::steady_clock::time_point::max();
	slot.priority = text.empty() ? 0 : priority;
	slot.token = takeCounter(runtimeKv, "nextToken");
	slot.sequence = takeCounter(runtimeKv, "nextSequence");
	writeSlot(runtimeKv, owner, slot);
	return slot.token;
}

std::chrono::milliseconds autoDurationForText(std::string_view text, std::chrono::milliseconds perCharacter) {
	constexpr long long kMinimumDisplayMs = 2000;
	const long long perCharMs = std::max<long long>(1, perCharacter.count());
	const long long textLen = static_cast<long long>(text.size());
	const long long dynamicMs = std::max<long long>(perCharMs, textLen * perCharMs);
	return std::chrono::milliseconds(std::max<long long>(kMinimumDisplayMs, dynamicMs));
}

Token postAutoTimed(Owner owner, std::string_view text, Kind kind, int priority, std::chrono::milliseconds perCharacter) {
	return postTimed(owner, text, kind, autoDurationForText(text, perCharacter), priority);
}

Token postAutoTimedAfter(Owner owner, std::string_view text, Kind kind, std::chrono::milliseconds delay, int priority, std::chrono::milliseconds perCharacter) {
	return postTimed(owner, text, kind, delay + autoDurationForText(text, perCharacter), priority);
}

void clearOwner(Owner owner) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	std::lock_guard<std::mutex> lock(stateMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Slot slot;

	if (!validOwner(owner)) return;
	readSlot(runtimeKv, owner, slot);
	slot.active = false;
	slot.text.clear();
	slot.segments.clear();
	slot.timed = false;
	slot.expiresAt = std::chrono::steady_clock::time_point::max();
	slot.priority = 0;
	slot.token = takeCounter(runtimeKv, "nextToken");
	slot.sequence = takeCounter(runtimeKv, "nextSequence");
	writeSlot(runtimeKv, owner, slot);
}

void setRuntimeMessageLineEnabled(bool enabled) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	std::lock_guard<std::mutex> lock(stateMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();

	writeInt(runtimeKv, messageLineRoot(runtimeKv), "enabled", enabled ? 1 : 0);
	if (enabled) return;
	clearSlotsLocked(runtimeKv);
}

void setStaticMode(bool active) {
	{
		std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
		std::lock_guard<std::mutex> lock(stateMutex());
		MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();

		storeStaticModeLocked(runtimeKv, active);
		writeUnsigned(runtimeKv, messageLineRoot(runtimeKv), "staticProgressCompleted", 0);
		writeUnsigned(runtimeKv, messageLineRoot(runtimeKv), "staticProgressTotal", 0);
		clearSlotsLocked(runtimeKv);
	}
	if (auto *menuBar = dynamic_cast<MRMenuBar *>(TProgram::menuBar)) menuBar->setStaticProgressMode(active);
	if (auto *statusLine = dynamic_cast<MRStatusLine *>(TProgram::statusLine)) statusLine->setStaticModePresentation(active);
	else if (TProgram::statusLine != nullptr)
		TProgram::statusLine->drawView();
}

bool staticModeActive() {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());

	return staticModeActiveLocked(mrvmRuntimeKv());
}

void setStaticProgress(std::size_t completed, std::size_t total) {
	{
		std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
		std::lock_guard<std::mutex> lock(stateMutex());
		MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
		VirtualMachine::Value root = messageLineRoot(runtimeKv);

		if (!staticModeActiveLocked(runtimeKv)) return;
		completed = std::min(completed, total);
		writeUnsigned(runtimeKv, root, "staticProgressCompleted", completed);
		writeUnsigned(runtimeKv, root, "staticProgressTotal", total);
	}
	if (auto *menuBar = dynamic_cast<MRMenuBar *>(TProgram::menuBar)) menuBar->drawView();
}

bool currentStaticProgress(std::size_t &completed, std::size_t &total) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	std::lock_guard<std::mutex> lock(stateMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value root = messageLineRoot(runtimeKv);

	completed = 0;
	total = 0;
	if (!staticModeActiveLocked(runtimeKv)) return false;
	completed = static_cast<std::size_t>(readUnsigned(runtimeKv, root, "staticProgressCompleted", 0));
	total = static_cast<std::size_t>(readUnsigned(runtimeKv, root, "staticProgressTotal", 0));
	completed = std::min(completed, total);
	return true;
}

bool currentVisibleMessage(VisibleMessage &out) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	std::lock_guard<std::mutex> lock(stateMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	const auto now = std::chrono::steady_clock::now();
	Slot best;
	bool found = false;

	expireLocked(runtimeKv, now);
	out = VisibleMessage();
	if (!messageLineEnabled(runtimeKv)) return false;
	for (std::size_t index = 0; index < kOwnerCount; ++index) {
		Slot slot;
		readSlot(runtimeKv, static_cast<Owner>(index), slot);
		if (!slot.active || slot.text.empty()) continue;
		if (!found || slot.priority > best.priority || (slot.priority == best.priority && slot.sequence > best.sequence)) {
			best = slot;
			found = true;
		}
	}
	return found ? exportSlot(best, out) : false;
}

bool currentOwnerMessage(Owner owner, VisibleMessage &out) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	std::lock_guard<std::mutex> lock(stateMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	const auto now = std::chrono::steady_clock::now();
	Slot slot;

	expireLocked(runtimeKv, now);
	out = VisibleMessage();
	if (!validOwner(owner) || !messageLineEnabled(runtimeKv)) return false;
	readSlot(runtimeKv, owner, slot);
	return exportSlot(slot, out);
}

} // namespace messageline
} // namespace mr
