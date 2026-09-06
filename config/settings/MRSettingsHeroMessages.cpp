#include "MRSettingsRuntime.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "../../mrmac/mrmac.h"
#include "../../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../../mrmac/vm/MRVMValue.hpp"
#include "../../ui/MRMessageLineController.hpp"

#include <mutex>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace {

VirtualMachine::Value heroMessagesRoot(MRVMRuntimeKv &runtimeKv) {
	VirtualMachine::Value settings = runtimeKv.ensureRoot("SETTINGS");
	VirtualMachine::Value runtime = runtimeKv.ensureChild(settings, "runtime");
	return runtimeKv.ensureChild(runtime, "heroMessages");
}

int readInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, int fallback) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, parent, key)) return fallback;
	const VirtualMachine::Value value = mrvmHashReadValue(store, store, parent, key);
	return value.type == TYPE_INT ? value.i : fallback;
}

void writeInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeInt(value));
}

} // namespace

bool setConfiguredHeroMessageSettings(const MRHeroMessageSettings &settings, std::string *errorMessage) {
	if (settings.fileThresholdMb < 0 || settings.fileThresholdMb > 16) {
		if (errorMessage != nullptr) *errorMessage = "Hero message file threshold must be within 0..16 MB.";
		return false;
	}
	const MRHeroMessageSettings previous = configuredHeroMessageSettings();
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
		const VirtualMachine::Value root = heroMessagesRoot(runtimeKv);

		writeInt(runtimeKv, root, "onMessageLine", settings.onMessageLine ? 1 : 0);
		writeInt(runtimeKv, root, "inLogFile", settings.inLogFile ? 1 : 0);
		writeInt(runtimeKv, root, "fileThresholdMb", settings.fileThresholdMb);
	}
	if (previous != settings) markConfiguredSettingsDirty();
	if (previous.onMessageLine && !settings.onMessageLine) {
		mr::messageline::clearOwner(mr::messageline::Owner::HeroEvent);
		mr::messageline::clearOwner(mr::messageline::Owner::HeroEventFollowup);
		mr::messageline::clearOwner(mr::messageline::Owner::MacroBrain);
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MRHeroMessageSettings configuredHeroMessageSettings() {
	MRHeroMessageSettings settings;
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	const VirtualMachine::Value root = heroMessagesRoot(runtimeKv);

	recordSettingsRuntimeRead();
	settings.onMessageLine = readInt(runtimeKv, root, "onMessageLine", settings.onMessageLine ? 1 : 0) != 0;
	settings.inLogFile = readInt(runtimeKv, root, "inLogFile", settings.inLogFile ? 1 : 0) != 0;
	settings.fileThresholdMb = readInt(runtimeKv, root, "fileThresholdMb", settings.fileThresholdMb);
	if (settings.fileThresholdMb < 0 || settings.fileThresholdMb > 16) settings.fileThresholdMb = 8;
	return settings;
}
