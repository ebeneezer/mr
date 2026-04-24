#include "../config/MRDialogPaths.hpp"
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

Slot *slotForOwner(State &shared, Owner owner) {
    const std::size_t index = ownerIndex(owner);
    if (index >= shared.slots.size())
        return nullptr;
    return &shared.slots[index];
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
    if (!configuredMenulineMessages())
        return 0;
    State &shared = state();
    std::lock_guard<std::mutex> lock(shared.mutex);
    const auto now = std::chrono::steady_clock::now();
    Slot *slot = slotForOwner(shared, owner);
    if (slot == nullptr)
        return 0;

    expireLocked(shared, now);
    duration = text.empty() ? std::chrono::milliseconds(0) : clampDurationForKind(kind, duration);
    slot->active = !text.empty();
    slot->kind = kind;
    slot->text = text;
    slot->timed = !text.empty();
    slot->expiresAt = text.empty() ? std::chrono::steady_clock::time_point::max() : now + duration;
    slot->priority = text.empty() ? 0 : priority;
    slot->token = shared.nextToken++;
    slot->sequence = shared.nextSequence++;
    return slot->token;
}

Token postSticky(Owner owner, std::string_view text, Kind kind, int priority) {
    if (!configuredMenulineMessages())
        return 0;
    State &shared = state();
    std::lock_guard<std::mutex> lock(shared.mutex);
    Slot *slot = slotForOwner(shared, owner);
    if (slot == nullptr)
        return 0;

    slot->active = !text.empty();
    slot->kind = kind;
    slot->text = text;
    slot->timed = false;
    slot->expiresAt = std::chrono::steady_clock::time_point::max();
    slot->priority = text.empty() ? 0 : priority;
    slot->token = shared.nextToken++;
    slot->sequence = shared.nextSequence++;
    return slot->token;
}

std::chrono::milliseconds autoDurationForText(std::string_view text, std::chrono::milliseconds perCharacter) {
    constexpr long long kMinimumDisplayMs = 2000;
    const long long perCharMs = std::max<long long>(1, perCharacter.count());
    const long long textLen = static_cast<long long>(text.size());
    const long long dynamicMs = std::max<long long>(perCharMs, textLen * perCharMs);
    return std::chrono::milliseconds(std::max<long long>(kMinimumDisplayMs, dynamicMs));
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
    Slot *slot = slotForOwner(shared, owner);
    if (slot == nullptr)
        return;

    slot->active = false;
    slot->text.clear();
    slot->timed = false;
    slot->expiresAt = std::chrono::steady_clock::time_point::max();
    slot->priority = 0;
    slot->token = shared.nextToken++;
    slot->sequence = shared.nextSequence++;
}

void clearOwnerToken(Owner owner, Token token) {
    State &shared = state();
    std::lock_guard<std::mutex> lock(shared.mutex);
    Slot *slot = slotForOwner(shared, owner);
    if (slot == nullptr)
        return;

    if (slot->token != token)
        return;
    slot->active = false;
    slot->text.clear();
    slot->timed = false;
    slot->expiresAt = std::chrono::steady_clock::time_point::max();
    slot->priority = 0;
    slot->token = shared.nextToken++;
    slot->sequence = shared.nextSequence++;
}

bool currentVisibleMessage(VisibleMessage &out) {
    if (!configuredMenulineMessages()) {
        out = VisibleMessage();
        return false;
    }
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
    if (!configuredMenulineMessages()) {
        out = VisibleMessage();
        return false;
    }
    State &shared = state();
    std::lock_guard<std::mutex> lock(shared.mutex);
    const auto now = std::chrono::steady_clock::now();

    expireLocked(shared, now);
    out = VisibleMessage();
    const Slot *slot = slotForOwner(shared, owner);
    return slot != nullptr ? exportSlot(*slot, out) : false;
}

} // namespace messageline
} // namespace mr
