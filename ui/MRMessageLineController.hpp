#ifndef MRMESSAGELINECONTROLLER_HPP
#define MRMESSAGELINECONTROLLER_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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
	DialogInteraction,
	HexEditor,
	WorkspaceRestore,
	ApplicationUpdate,
	Count
};

struct VisibleMessage {
	bool active;
	Kind kind;
	std::string text;
	struct Segment {
		Kind kind;
		std::string text;
	};
	std::vector<Segment> segments;

	VisibleMessage() noexcept : active(false), kind(Kind::Info), text() {
	}
};

using Token = std::uint64_t;

static constexpr int kPriorityLow = 10;
static constexpr int kPriorityMedium = 20;
static constexpr int kPriorityHigh = 30;

Token postTimed(Owner owner, std::string_view text, Kind kind, std::chrono::milliseconds duration, int priority);
Token postTimedSegments(Owner owner, const std::vector<VisibleMessage::Segment> &segments, Kind kind, std::chrono::milliseconds duration, int priority);
Token postSticky(Owner owner, std::string_view text, Kind kind, int priority);
[[nodiscard]] std::chrono::milliseconds autoDurationForText(std::string_view text, std::chrono::milliseconds perCharacter = std::chrono::milliseconds(100));
Token postAutoTimed(Owner owner, std::string_view text, Kind kind, int priority, std::chrono::milliseconds perCharacter = std::chrono::milliseconds(100));
Token postAutoTimedAfter(Owner owner, std::string_view text, Kind kind, std::chrono::milliseconds delay, int priority, std::chrono::milliseconds perCharacter = std::chrono::milliseconds(100));
Token postFileAutoTimed(Owner owner, std::string_view text, Kind kind, std::size_t fileBytes, int priority, std::chrono::milliseconds perCharacter = std::chrono::milliseconds(100));
Token postFileAutoTimedAfter(Owner owner, std::string_view text, Kind kind, std::size_t fileBytes, std::chrono::milliseconds delay, int priority, std::chrono::milliseconds perCharacter = std::chrono::milliseconds(100));
void clearOwner(Owner owner);
void setStaticMode(bool active);
[[nodiscard]] bool staticModeActive();
void setStaticProgress(std::size_t completed, std::size_t total);
[[nodiscard]] bool currentStaticProgress(std::size_t &completed, std::size_t &total);
[[nodiscard]] bool currentVisibleMessage(VisibleMessage &out);
[[nodiscard]] bool currentOwnerMessage(Owner owner, VisibleMessage &out);

} // namespace messageline
} // namespace mr

#endif
