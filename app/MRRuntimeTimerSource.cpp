#include "MRRuntimeTimerSource.hpp"

#include "MRRuntimeScheduler.hpp"

#include <chrono>

std::uint64_t runtimeTimerSourceNowMs() {
	const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now().time_since_epoch();
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

std::size_t pumpRuntimeTimerSource() {
	return pumpRuntimeScheduler(runtimeTimerSourceNowMs());
}
