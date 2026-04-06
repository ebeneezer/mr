#ifndef MRMESSAGELINECONTROLLER_HPP
#define MRMESSAGELINECONTROLLER_HPP

#include <chrono>
#include <cstdint>
#include <string>

namespace mr {
namespace messageline {

enum class Kind : unsigned char {
    Info,
    Success,
    Warning,
    Error
};

enum class Owner : unsigned char {
    HeroEvent,
    MacroMarquee,
    DialogValidation,
    DialogInteraction
};

struct VisibleMessage {
    bool active;
    Kind kind;
    std::string text;

    VisibleMessage() noexcept : active(false), kind(Kind::Info), text() {
    }
};

using Token = std::uint64_t;

static constexpr int kPriorityLow = 10;
static constexpr int kPriorityMedium = 20;
static constexpr int kPriorityHigh = 30;

Token postTimed(Owner owner, const std::string &text, Kind kind, std::chrono::milliseconds duration,
                int priority);
Token postSticky(Owner owner, const std::string &text, Kind kind, int priority);
void clearOwner(Owner owner);
void clearOwnerToken(Owner owner, Token token);
bool currentVisibleMessage(VisibleMessage &out);
bool currentOwnerMessage(Owner owner, VisibleMessage &out);

} // namespace messageline
} // namespace mr

#endif
