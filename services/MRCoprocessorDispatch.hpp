#ifndef MRCOPROCESSORDISPATCH_HPP
#define MRCOPROCESSORDISPATCH_HPP

#include <cstdint>

#include "../coprocessor/MRCoprocessor.hpp"

void handleCoprocessorResult(const mr::coprocessor::Result &result);
void mrTraceCoprocessorTaskCancel(int bufferId, std::uint64_t taskId);
void mrTraceCoprocessorTaskRelease(int bufferId, std::uint64_t taskId, const char *state);

#endif
