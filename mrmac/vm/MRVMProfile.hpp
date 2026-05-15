#ifndef MRVM_PROFILE_HPP
#define MRVM_PROFILE_HPP

#include <cstddef>
#include <string>
#include <vector>

enum MRMacroExecutionFlags {
	mrefBackgroundSafe = 1u << 0,
	mrefStagedWrite = 1u << 1,
	mrefUiAffinity = 1u << 2,
	mrefExternalIo = 1u << 3
};

struct MRMacroExecutionProfile {
	unsigned flags;
	std::size_t opcodeCount;
	std::size_t intrinsicCount;
	std::size_t procCount;
	std::size_t procVarCount;
	std::size_t tvCallCount;
	std::vector<std::string> stagedWriteSymbols;
	std::vector<std::string> uiAffinitySymbols;
	std::vector<std::string> externalIoSymbols;

	MRMacroExecutionProfile() noexcept : flags(0), opcodeCount(0), intrinsicCount(0), procCount(0), procVarCount(0), tvCallCount(0), stagedWriteSymbols(), uiAffinitySymbols(), externalIoSymbols() {
	}

	bool has(unsigned mask) const noexcept {
		return (flags & mask) != 0;
	}
};

MRMacroExecutionProfile mrvmAnalyzeBytecode(const unsigned char *bytecode, std::size_t length);
std::string mrvmDescribeExecutionProfile(const MRMacroExecutionProfile &profile);
bool mrvmCanRunInBackground(const MRMacroExecutionProfile &profile) noexcept;
bool mrvmCanRunStagedInBackground(const MRMacroExecutionProfile &profile) noexcept;
std::vector<std::string> mrvmUnsupportedStagedSymbols(const MRMacroExecutionProfile &profile);

#endif
