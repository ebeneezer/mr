#ifndef MRPERFORMANCE_HPP
#define MRPERFORMANCE_HPP

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include "../coprocessor/MRCoprocessor.hpp"

namespace mr {
namespace performance {

enum class Scope : unsigned char {
	Ui,
	Background
};

enum class Outcome : unsigned char {
	Completed,
	Conflict,
	Cancelled,
	Failed
};

enum class MessageNoticeKind : unsigned char {
	Info,
	Success,
	Warning,
	Error
};

struct Event {
	std::uint64_t sequence;
	std::time_t wallClock;
	Scope scope;
	Outcome outcome;
	mr::coprocessor::Lane lane;
	std::string action;
	std::string detail;
	std::size_t bufferId;
	std::size_t documentId;
	std::size_t bytes;
	double queueMs;
	double runMs;
	double totalMs;

	Event() noexcept : sequence(0), wallClock(0), scope(Scope::Ui), outcome(Outcome::Completed), lane(mr::coprocessor::Lane::Compute), action(), detail(), bufferId(0), documentId(0), bytes(0), queueMs(0.0), runMs(0.0), totalMs(0.0) {
	}
};

struct MessageLineNotice {
	bool active;
	MessageNoticeKind kind;
	std::string text;

	MessageLineNotice() noexcept : active(false), kind(MessageNoticeKind::Info), text() {
	}
};

void recordUiEvent(std::string_view action, std::size_t bufferId, std::size_t documentId, std::size_t bytes, double totalMs, std::string_view detail = {}, Outcome outcome = Outcome::Completed);
void recordBackgroundResult(const mr::coprocessor::Result &result, std::string_view action, std::size_t bufferId, std::size_t documentId, std::size_t bytes, std::string_view detail = {});
void recordBackgroundEvent(mr::coprocessor::Lane lane, Outcome outcome, const mr::coprocessor::TaskTiming &timing, std::string_view action, std::size_t bufferId, std::size_t documentId, std::size_t bytes, std::string_view detail = {});

[[nodiscard]] std::vector<Event> recentForWindow(std::size_t bufferId, std::size_t documentId, std::size_t maxCount = 6);
[[nodiscard]] std::vector<Event> recentGlobal(std::size_t maxCount = 6);
[[nodiscard]] bool currentMessageLineNotice(MessageLineNotice &out);

[[nodiscard]] std::string formatEventLine(const Event &event);
[[nodiscard]] std::string formatDuration(double totalMs);
[[nodiscard]] std::string formatThroughput(std::size_t bytes, double totalMs);

} // namespace performance
} // namespace mr

#endif
