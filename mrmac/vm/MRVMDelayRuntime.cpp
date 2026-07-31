#include "../MRVM.hpp"
#include "MRVMDelayRuntime.hpp"
#include "MRVMRuntimeInternal.hpp"
#include "MRVMRuntimeState.hpp"

#include <thread>

namespace mrvm_execution {

bool sleepDelayBlocking(int millis) {
	if (millis <= 0) return true;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(millis);
	while (std::chrono::steady_clock::now() < deadline) {
		if (mrvm_runtime::backgroundMacroCancelRequested()) return false;
		auto remaining = deadline - std::chrono::steady_clock::now();
		auto slice = std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
		if (slice > std::chrono::milliseconds(10)) slice = std::chrono::milliseconds(10);
		if (slice.count() <= 0) break;
		std::this_thread::sleep_for(slice);
	}
	return true;
}

} // namespace mrvm_execution

VirtualMachine::DelayState::DelayState() noexcept : pending(false), ready(false), enabled(true), bytecode(), length(0), ip(0), callStack(), returnInt(0), returnStr(), errorLevel(0), savedParameterString(), macroFramePushed(false), deadline(), generation(0), millis(0) {
}

void VirtualMachine::DelayState::clear() noexcept {
	pending = false;
	ready = false;
	bytecode.clear();
	callStack.clear();
	length = 0;
	ip = 0;
	returnInt = 0;
	returnStr.clear();
	errorLevel = 0;
	savedParameterString.clear();
	macroFramePushed = false;
	deadline = std::chrono::steady_clock::time_point();
	millis = 0;
}

int VirtualMachine::normalizeDelayMillis(int millis) noexcept {
	static const int kMaxDelayMillis = 60 * 60 * 1000;
	if (millis <= 0) return 0;
	if (millis > kMaxDelayMillis) return kMaxDelayMillis;
	return millis;
}

void VirtualMachine::clearAsyncDelayState() noexcept {
	delayState.clear();
}

void VirtualMachine::execute(const unsigned char *bytecode, size_t length) {
	cancelPendingDelay();
	clearAsyncDelayState();
	executeAt(bytecode, length, 0, std::string(), std::string(), true, false);
}

bool VirtualMachine::resumePendingDelay() {
	if (!delayState.pending) return false;
	if (!delayState.ready || std::chrono::steady_clock::now() < delayState.deadline) return true;
	executeAt(nullptr, 0, 0, std::string(), std::string(), false, false);
	return delayState.pending;
}

bool VirtualMachine::cancelPendingDelay() {
	if (!delayState.pending) return false;
	if (delayState.macroFramePushed) mrvmPopRuntimeMacroFrame();
	cancelledExecution = true;
	mrvm_runtime::setRuntimeErrorLevel(5007);
	appendLogLine("VM Notice: pending DELAY cancelled.", true);
	clearAsyncDelayState();
	return true;
}
