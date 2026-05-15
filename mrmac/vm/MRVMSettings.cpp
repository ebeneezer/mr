#include "MRVMSettings.hpp"

#include "../MRVM.hpp"

#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../../keymap/MRKeymapProfile.hpp"

#include <string>
#include <utility>
#include <vector>

namespace {
thread_local bool g_startupSettingsMode = false;

struct StartupKeymapBatchState {
	bool initialized = false;
	bool profilesDirty = false;
	bool activeDirty = false;
	std::vector<MRKeymapProfile> profiles;
	std::string activeProfile;
};

StartupKeymapBatchState &startupKeymapBatchState() {
	static StartupKeymapBatchState state;
	return state;
}

void clearStartupKeymapBatchState() noexcept {
	StartupKeymapBatchState &state = startupKeymapBatchState();

	state.initialized = false;
	state.profilesDirty = false;
	state.activeDirty = false;
	state.profiles.clear();
	state.activeProfile.clear();
}

void ensureStartupKeymapBatchInitialized() {
	StartupKeymapBatchState &state = startupKeymapBatchState();

	if (state.initialized) return;
	state.profiles = configuredKeymapProfiles();
	state.activeProfile = configuredActiveKeymapProfile();
	state.initialized = true;
}

bool hasPendingStartupKeymapBatch() noexcept {
	const StartupKeymapBatchState &state = startupKeymapBatchState();
	return state.initialized && (state.profilesDirty || state.activeDirty);
}

bool flushStartupKeymapBatch(std::string *errorMessage) {
	StartupKeymapBatchState &state = startupKeymapBatchState();

	if (!state.initialized) {
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (state.profilesDirty)
		if (!setConfiguredKeymapProfiles(state.profiles, errorMessage)) return false;
	if (state.activeDirty)
		if (!setConfiguredActiveKeymapProfile(state.activeProfile, errorMessage)) return false;
	clearStartupKeymapBatchState();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool keymapDiagnosticsContainErrors(const std::vector<MRKeymapDiagnostic> &diagnostics) {
	for (const MRKeymapDiagnostic &diagnostic : diagnostics)
		if (diagnostic.severity == MRKeymapDiagnosticSeverity::Error) return true;
	return false;
}

std::string firstKeymapDiagnosticMessage(const std::vector<MRKeymapDiagnostic> &diagnostics) {
	for (const MRKeymapDiagnostic &diagnostic : diagnostics)
		if (diagnostic.severity == MRKeymapDiagnosticSeverity::Error) return diagnostic.message;
	if (!diagnostics.empty()) return diagnostics.front().message;
	return "invalid keymap payload.";
}

bool assignKeymapPayloadError(std::string *errorMessage, std::string message) {
	if (errorMessage != nullptr) *errorMessage = std::move(message);
	return false;
}
} // namespace

void mrvmSetStartupSettingsMode(bool enabled) noexcept {
	g_startupSettingsMode = enabled;
	if (enabled) clearStartupKeymapBatchState();
	else if (hasPendingStartupKeymapBatch())
		clearStartupKeymapBatchState();
}

bool mrvmIsStartupSettingsMode() noexcept {
	return g_startupSettingsMode;
}

bool mrvmFlushPendingStartupKeymapBatch(std::string *errorMessage) {
	return flushStartupKeymapBatch(errorMessage);
}

bool mrvmApplyConfiguredActiveKeymapProfilePayload(const std::string &payload, std::string *errorMessage) {
	MRKeymapProfile activeProfileRecord;
	const auto diagnostics = parseKeymapProfilePayload(payload, activeProfileRecord);

	if (keymapDiagnosticsContainErrors(diagnostics)) return assignKeymapPayloadError(errorMessage, firstKeymapDiagnosticMessage(diagnostics));
	bool ok = false;
	if (mrvmIsStartupSettingsMode()) {
		ensureStartupKeymapBatchInitialized();
		StartupKeymapBatchState &state = startupKeymapBatchState();
		state.activeProfile = activeProfileRecord.name;
		state.activeDirty = true;
		ok = true;
		if (errorMessage != nullptr) errorMessage->clear();
	} else
		ok = setConfiguredActiveKeymapProfile(activeProfileRecord.name, errorMessage);
	return ok;
}

bool mrvmApplyConfiguredKeymapProfilePayload(const std::string &payload, std::string *errorMessage) {
	MRKeymapProfile profile;
	const auto diagnostics = parseKeymapProfilePayload(payload, profile);

	if (keymapDiagnosticsContainErrors(diagnostics)) return assignKeymapPayloadError(errorMessage, firstKeymapDiagnosticMessage(diagnostics));
	std::vector<MRKeymapProfile> profiles = configuredKeymapProfiles();
	if (mrvmIsStartupSettingsMode()) {
		ensureStartupKeymapBatchInitialized();
		profiles = startupKeymapBatchState().profiles;
	}
	for (MRKeymapProfile &existing : profiles)
		if (existing.name == profile.name) {
			existing = profile;
			if (mrvmIsStartupSettingsMode()) {
				StartupKeymapBatchState &state = startupKeymapBatchState();
				state.profiles = std::move(profiles);
				state.profilesDirty = true;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			return setConfiguredKeymapProfiles(profiles, errorMessage);
		}
	profiles.push_back(profile);
	if (mrvmIsStartupSettingsMode()) {
		StartupKeymapBatchState &state = startupKeymapBatchState();
		state.profiles = std::move(profiles);
		state.profilesDirty = true;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setConfiguredKeymapProfiles(profiles, errorMessage);
}

bool mrvmApplyConfiguredKeymapBindingPayload(const std::string &payload, std::string *errorMessage) {
	MRKeymapBindingRecord binding;
	const auto diagnostics = parseKeymapBindingPayload(payload, binding);
	std::vector<MRKeymapProfile> profiles = configuredKeymapProfiles();

	if (keymapDiagnosticsContainErrors(diagnostics)) return assignKeymapPayloadError(errorMessage, firstKeymapDiagnosticMessage(diagnostics));
	if (mrvmIsStartupSettingsMode()) {
		ensureStartupKeymapBatchInitialized();
		profiles = startupKeymapBatchState().profiles;
	}
	for (MRKeymapProfile &profile : profiles)
		if (profile.name == binding.profileName) {
			profile.bindings.push_back(binding);
			if (mrvmIsStartupSettingsMode()) {
				StartupKeymapBatchState &state = startupKeymapBatchState();
				state.profiles = std::move(profiles);
				state.profilesDirty = true;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			return setConfiguredKeymapProfiles(profiles, errorMessage);
		}
	return assignKeymapPayloadError(errorMessage, "Binding references unknown keymap profile: " + binding.profileName);
}

bool mrvmPersistConfiguredSettingsSnapshot(std::string *errorMessage) {
	return persistConfiguredSettingsSnapshot(errorMessage);
}
