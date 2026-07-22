#include "../MRVM.hpp"
#include "MRVMRuntimeState.hpp"

int VirtualMachine::normalizeDelayMillis(int millis) noexcept {
	static const int kMaxDelayMillis = 60 * 60 * 1000;
	if (millis <= 0) return 0;
	if (millis > kMaxDelayMillis) return kMaxDelayMillis;
	return millis;
}

void VirtualMachine::clearAsyncDelayState() noexcept {
	mAsyncDelayPending = false;
	mAsyncDelayReady = false;
	mAsyncBytecode.clear();
	mAsyncCallStack.clear();
	mAsyncLength = 0;
	mAsyncIp = 0;
	mAsyncReturnInt = 0;
	mAsyncReturnStr.clear();
	mAsyncErrorLevel = 0;
	mAsyncSavedParameterString.clear();
	mAsyncMacroFramePushed = false;
	mAsyncDelayDeadline = std::chrono::steady_clock::time_point();
	mAsyncDelayMillis = 0;
}

void VirtualMachine::execute(const unsigned char *bytecode, size_t length) {
	cancelPendingDelay();
	clearAsyncDelayState();
	executeAt(bytecode, length, 0, std::string(), std::string(), true, false);
}

bool VirtualMachine::resumePendingDelay() {
	if (!mAsyncDelayPending) return false;
	if (!mAsyncDelayReady || std::chrono::steady_clock::now() < mAsyncDelayDeadline) return true;
	executeAt(nullptr, 0, 0, std::string(), std::string(), false, false);
	return mAsyncDelayPending;
}

bool VirtualMachine::cancelPendingDelay() {
	if (!mAsyncDelayPending) return false;
	if (mAsyncMacroFramePushed && !g_runtimeEnv.macroStack.empty()) g_runtimeEnv.macroStack.pop_back();
	cancelledExecution = true;
	g_runtimeEnv.errorLevel = 5007;
	appendLogLine("VM Notice: pending DELAY cancelled.", true);
	clearAsyncDelayState();
	return true;
}
