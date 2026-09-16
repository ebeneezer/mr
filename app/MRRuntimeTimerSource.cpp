#define Uses_TProgram
#include <tvision/tv.h>

#include "MRRuntimeTimerSource.hpp"
#include "MRCommands.hpp"
#include "../mrmac/MRVM.hpp"

#include "MRRuntimeScheduler.hpp"

#include <chrono>

void scheduleRuntimeTimerSource() {
	// Worker-side registrations reach the UI timer source through result delivery.
	if (mrvmIsBackgroundExecution() || TProgram::application == nullptr) return;
	message(TProgram::application, evBroadcast, cmMrRuntimeScheduleChanged, nullptr);
}

std::uint64_t runtimeTimerSourceNowMs() {
	const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now().time_since_epoch();
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

std::size_t pumpRuntimeTimerSource(std::uint64_t *nextWakeupMs) {
	return pumpRuntimeScheduler(runtimeTimerSourceNowMs(), nextWakeupMs);
}
