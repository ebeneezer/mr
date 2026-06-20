#ifndef MREXECSESSIONSTATUS_HPP
#define MREXECSESSIONSTATUS_HPP

#include "../mrmac/MRMacroExecutionSession.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct MRExecSessionStatusSnapshot {
	std::uint64_t generation = 0;
	std::size_t activeCount = 0;
	std::size_t pendingDelayCount = 0;
	std::size_t recentResultCount = 0;
};

MRMacroExecutionSessionListenerId installExecSessionStatusConsumer();
void installExecSessionStatusConsumerIfEnabled();
std::uint64_t execSessionStatusConsumerGeneration();
MRExecSessionStatusSnapshot execSessionStatusSnapshot();
std::vector<std::string> execSessionStatusLines(std::size_t maxRecentResults);
void logExecSessionStatusSnapshotIfEnabled();

#endif
