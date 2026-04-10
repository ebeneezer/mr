#include "MRMessageLineController.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <string_view>

namespace mr {
namespace messageline {
namespace {

constexpr std::size_t kOwnerCount = static_cast<std::size_t>(Owner::DialogInteraction) + 1;

struct Slot {
    bool active = false;
    Kind kind = Kind::Info;
    std::string text;
    std::chrono::steady_clock::time_point expiresAt = std::chrono::steady_clock::time_point::max();
    bool timed = false;
    int priority = 0;
    Token token = 0;
    std::uint64_t sequence = 0;
};

struct State {
    std::mutex mutex;
    std::array<Slot, kOwnerCount> slots;
    Token nextToken = 1;
    std::uint64_t nextSequence = 1;
};

State &state() {
    static State shared;
    return shared;
}

std::size_t ownerIndex(Owner owner) {
    return static_cast<std::size_t>(owner);
}

void expireLocked(State &shared, std::chrono::steady_clock::time_point now) {
    for (Slot &slot : shared.slots)
        if (slot.active && slot.timed && now >= slot.expiresAt) {
            slot.active = false;
            slot.text.clear();
            slot.timed = false;
            slot.expiresAt = std::chrono::steady_clock::time_point::max();
            slot.priority = 0;
        }
}

bool exportSlot(const Slot &slot, VisibleMessage &out) {
    if (!slot.active || slot.text.empty())
        return false;
    out.active = true;
    out.kind = slot.kind;
    out.text = slot.text;
    return true;
}

} // namespace

Token postTimed(Owner owner, std::string_view text, Kind kind, std::chrono::milliseconds duration,
                int priority) {
    State &shared = state();
    std::lock_guard<std::mutex> lock(shared.mutex);
    const auto now = std::chrono::steady_clock::now();
    Slot &slot = shared.slots[ownerIndex(owner)];

    expireLocked(shared, now);
    slot.active = !text.empty();
    slot.kind = kind;
    slot.text = text;
    slot.timed = !text.empty();
    slot.expiresAt = text.empty() ? std::chrono::steady_clock::time_point::max() : now + duration;
    slot.priority = text.empty() ? 0 : priority;
    slot.token = shared.nextToken++;
    slot.sequence = shared.nextSequence++;
    return slot.token;
}

Token postSticky(Owner owner, std::string_view text, Kind kind, int priority) {
    State &shared = state();
    std::lock_guard<std::mutex> lock(shared.mutex);
    Slot &slot = shared.slots[ownerIndex(owner)];

    slot.active = !text.empty();
    slot.kind = kind;
    slot.text = text;
    slot.timed = false;
    slot.expiresAt = std::chrono::steady_clock::time_point::max();
    slot.priority = text.empty() ? 0 : priority;
    slot.token = shared.nextToken++;
    slot.sequence = shared.nextSequence++;
    return slot.token;
}

std::chrono::milliseconds autoDurationForText(std::string_view text, std::chrono::milliseconds perCharacter) {
    const long long perCharMs = std::max<long long>(1, perCharacter.count());
    const long long textLen = static_cast<long long>(text.size());
    return std::chrono::milliseconds(std::max<long long>(perCharMs, textLen * perCharMs));
}

Token postAutoTimed(Owner owner, std::string_view text, Kind kind, int priority,
                    std::chrono::milliseconds perCharacter) {
    return postTimed(owner, text, kind, autoDurationForText(text, perCharacter), priority);
}

Token postAutoTimedAfter(Owner owner, std::string_view text, Kind kind, std::chrono::milliseconds delay,
                         int priority, std::chrono::milliseconds perCharacter) {
    return postTimed(owner, text, kind, delay + autoDurationForText(text, perCharacter), priority);
}

void clearOwner(Owner owner) {
    State &shared = state();
    std::lock_guard<std::mutex> lock(shared.mutex);
    Slot &slot = shared.slots[ownerIndex(owner)];

    slot.active = false;
    slot.text.clear();
    slot.timed = false;
    slot.expiresAt = std::chrono::steady_clock::time_point::max();
    slot.priority = 0;
    slot.token = shared.nextToken++;
    slot.sequence = shared.nextSequence++;
}

void clearOwnerToken(Owner owner, Token token) {
    State &shared = state();
    std::lock_guard<std::mutex> lock(shared.mutex);
    Slot &slot = shared.slots[ownerIndex(owner)];

    if (slot.token != token)
        return;
    slot.active = false;
    slot.text.clear();
    slot.timed = false;
    slot.expiresAt = std::chrono::steady_clock::time_point::max();
    slot.priority = 0;
    slot.token = shared.nextToken++;
    slot.sequence = shared.nextSequence++;
}

bool currentVisibleMessage(VisibleMessage &out) {
    State &shared = state();
    std::lock_guard<std::mutex> lock(shared.mutex);
    const auto now = std::chrono::steady_clock::now();
    const Slot *best = nullptr;

    expireLocked(shared, now);
    out = VisibleMessage();
    for (const Slot &slot : shared.slots) {
        if (!slot.active || slot.text.empty())
            continue;
        if (best == nullptr || slot.priority > best->priority ||
            (slot.priority == best->priority && slot.sequence > best->sequence))
            best = &slot;
    }
    return best != nullptr ? exportSlot(*best, out) : false;
}

bool currentOwnerMessage(Owner owner, VisibleMessage &out) {
    State &shared = state();
    std::lock_guard<std::mutex> lock(shared.mutex);
    const auto now = std::chrono::steady_clock::now();

    expireLocked(shared, now);
    out = VisibleMessage();
    return exportSlot(shared.slots[ownerIndex(owner)], out);
}

} // namespace messageline
} // namespace mr
