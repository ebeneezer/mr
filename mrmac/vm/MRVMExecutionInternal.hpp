#ifndef MRVM_EXECUTION_INTERNAL_HPP
#define MRVM_EXECUTION_INTERNAL_HPP

#include "../MRVM.hpp"
#include "MRVMRuntimeState.hpp"

#include <cstddef>
#include <string>
#include <vector>

struct VirtualMachine::MRMacroDebugChildFrame {
	std::unique_ptr<VirtualMachine> vm;
	MRMacroDebugRunResult result;
	std::string macroKey;
	std::string fileKey;
	std::size_t parentInstructionOffset;
	bool unloadAfterCompletion;
	bool evictTransientAfterCompletion;

	MRMacroDebugChildFrame() : vm(), result(), macroKey(), fileKey(), parentInstructionOffset(0), unloadAfterCompletion(false), evictTransientAfterCompletion(false) {
	}
};

struct VirtualMachine::ExecutionFrame {
	const unsigned char *bytecode;
	std::size_t length;
	std::size_t &ip;
	std::vector<std::size_t> &callStack;
	ExecutionState &state;
	const std::string &savedParameterString;
	const std::string &activeMacroName;
	bool activeFirstRun;
	bool allowAsyncDelay;
};

namespace mrvm_execution {

class DelayYield final {
 public:
	explicit DelayYield(int delayMillis) noexcept : millis(delayMillis) {
	}

	int millis;
};

bool sleepDelayBlocking(int millis);

} // namespace mrvm_execution

#endif
