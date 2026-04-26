#ifndef MRMESSAGELINECONTROLLER_HPP
#define MRMESSAGELINECONTROLLER_HPP

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

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
    HeroEventFollowup,
    MacroMessage,
    MacroMarquee,
    MacroBrain,
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

Token postTimed(Owner owner, std::string_view text, Kind kind, std::chrono::milliseconds duration,
                int priority);
Token postSticky(Owner owner, std::string_view text, Kind kind, int priority);
[[nodiscard]] std::chrono::milliseconds autoDurationForText(
    std::string_view text, std::chrono::milliseconds perCharacter = std::chrono::milliseconds(100));
Token postAutoTimed(Owner owner, std::string_view text, Kind kind, int priority,
                    std::chrono::milliseconds perCharacter = std::chrono::milliseconds(100));
Token postAutoTimedAfter(Owner owner, std::string_view text, Kind kind, std::chrono::milliseconds delay,
                         int priority,
                         std::chrono::milliseconds perCharacter = std::chrono::milliseconds(100));
void clearOwner(Owner owner);
void clearOwnerToken(Owner owner, Token token);
[[nodiscard]] bool currentVisibleMessage(VisibleMessage &out);
[[nodiscard]] bool currentOwnerMessage(Owner owner, VisibleMessage &out);

} // namespace messageline
} // namespace mr

#endif
