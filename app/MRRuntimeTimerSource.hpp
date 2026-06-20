#ifndef MRRUNTIMETIMERSOURCE_HPP
#define MRRUNTIMETIMERSOURCE_HPP

#include <cstddef>
#include <cstdint>

std::uint64_t runtimeTimerSourceNowMs();
std::size_t pumpRuntimeTimerSource();

#endif
