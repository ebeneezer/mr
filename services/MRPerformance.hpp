#ifndef MRPERFORMANCE_HPP
#define MRPERFORMANCE_HPP

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
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
	Cancelled,
	Failed
};

enum class HeroKind : unsigned char {
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

	Event() noexcept
	    : sequence(0), wallClock(0), scope(Scope::Ui), outcome(Outcome::Completed),
	      lane(mr::coprocessor::Lane::Compute), action(), detail(), bufferId(0), documentId(0), bytes(0),
	      queueMs(0.0), runMs(0.0), totalMs(0.0) {
	}
};

struct HeroNotice {
	bool active;
	HeroKind kind;
	std::string text;

	HeroNotice() noexcept : active(false), kind(HeroKind::Info), text() {
	}
};

void recordUiEvent(const std::string &action, std::size_t bufferId, std::size_t documentId, std::size_t bytes,
                   double totalMs, const std::string &detail = std::string(),
                   Outcome outcome = Outcome::Completed);
void recordBackgroundResult(const mr::coprocessor::Result &result, const std::string &action,
                            std::size_t bufferId, std::size_t documentId, std::size_t bytes,
                            const std::string &detail = std::string());

std::vector<Event> recentForWindow(std::size_t bufferId, std::size_t documentId, std::size_t maxCount = 6);
std::vector<Event> recentGlobal(std::size_t maxCount = 6);
bool currentHeroNotice(HeroNotice &out);

std::string formatEventLine(const Event &event);
std::string formatDuration(double totalMs);
std::string formatThroughput(std::size_t bytes, double totalMs);

} // namespace performance
} // namespace mr

#endif
