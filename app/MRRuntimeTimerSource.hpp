#ifndef MRRUNTIMETIMERSOURCE_HPP
#define MRRUNTIMETIMERSOURCE_HPP

#include <cstddef>
#include <cstdint>

std::uint64_t runtimeTimerSourceNowMs();
void scheduleRuntimeTimerSource();
std::size_t pumpRuntimeTimerSource(std::uint64_t *nextWakeupMs = nullptr);

#endif
