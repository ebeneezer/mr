#include "MRVMSettings.hpp"

#include "../MRVM.hpp"

#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../config/settings/MRSettingsRuntimeState.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../../keymap/MRKeymapProfile.hpp"

#include <string>
#include <utility>
#include <vector>

namespace {
void clearKeymapBatchState() noexcept {
	clearConfiguredKeymapBatchStateValue();
}

bool configuredKeymapBatchActive() noexcept {
	return settingsStartupModeValue() || settingsKeymapBatchDepthValue() > 0;
}

MRConfiguredKeymapBatchState initializedKeymapBatchState() {
	MRConfiguredKeymapBatchState state = configuredKeymapBatchStateValue();

	if (!state.initialized) {
		state.profiles = configuredKeymapProfiles();
		state.activeProfile = configuredActiveKeymapProfile();
		state.initialized = true;
		storeConfiguredKeymapBatchStateValue(state);
	}
	return state;
}

bool hasPendingKeymapBatch() noexcept {
	const MRConfiguredKeymapBatchState state = configuredKeymapBatchStateValue();
	return state.initialized && (state.profilesDirty || state.activeDirty);
}

bool flushKeymapBatch(std::string *errorMessage) {
	const MRConfiguredKeymapBatchState state = configuredKeymapBatchStateValue();

	if (!state.initialized) {
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (state.profilesDirty)
		if (!setConfiguredKeymapProfiles(state.profiles, errorMessage)) return false;
	if (state.activeDirty)
		if (!setConfiguredActiveKeymapProfile(state.activeProfile, errorMessage)) return false;
	clearKeymapBatchState();
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
	storeSettingsStartupModeValue(enabled);
	if (enabled) clearKeymapBatchState();
	else if (settingsKeymapBatchDepthValue() == 0 && hasPendingKeymapBatch())
		clearKeymapBatchState();
}

bool mrvmIsStartupSettingsMode() noexcept {
	return settingsStartupModeValue();
}

void mrvmBeginConfiguredKeymapBatch() noexcept {
	storeSettingsKeymapBatchDepthValue(settingsKeymapBatchDepthValue() + 1);
}

bool mrvmEndConfiguredKeymapBatch(std::string *errorMessage) {
	int depth = settingsKeymapBatchDepthValue();

	if (depth <= 0) {
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	--depth;
	storeSettingsKeymapBatchDepthValue(depth);
	if (depth > 0 || settingsStartupModeValue()) {
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return flushKeymapBatch(errorMessage);
}

bool mrvmFlushPendingStartupKeymapBatch(std::string *errorMessage) {
	return flushKeymapBatch(errorMessage);
}

bool mrvmApplyConfiguredActiveKeymapProfilePayload(const std::string &payload, std::string *errorMessage) {
	MRKeymapProfile activeProfileRecord;
	const auto diagnostics = parseKeymapProfilePayload(payload, activeProfileRecord);

	if (keymapDiagnosticsContainErrors(diagnostics)) return assignKeymapPayloadError(errorMessage, firstKeymapDiagnosticMessage(diagnostics));
	bool ok = false;
	if (configuredKeymapBatchActive()) {
		MRConfiguredKeymapBatchState state = initializedKeymapBatchState();
		state.activeProfile = activeProfileRecord.name;
		state.activeDirty = true;
		storeConfiguredKeymapBatchStateValue(state);
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
	if (configuredKeymapBatchActive()) {
		profiles = initializedKeymapBatchState().profiles;
	}
	for (MRKeymapProfile &existing : profiles)
		if (existing.name == profile.name) {
			existing = profile;
			if (configuredKeymapBatchActive()) {
				MRConfiguredKeymapBatchState state = initializedKeymapBatchState();
				state.profiles = std::move(profiles);
				state.profilesDirty = true;
				storeConfiguredKeymapBatchStateValue(state);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			return setConfiguredKeymapProfiles(profiles, errorMessage);
		}
	profiles.push_back(profile);
	if (configuredKeymapBatchActive()) {
		MRConfiguredKeymapBatchState state = initializedKeymapBatchState();
		state.profiles = std::move(profiles);
		state.profilesDirty = true;
		storeConfiguredKeymapBatchStateValue(state);
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
	if (configuredKeymapBatchActive()) {
		profiles = initializedKeymapBatchState().profiles;
	}
	for (MRKeymapProfile &profile : profiles)
		if (profile.name == binding.profileName) {
			profile.bindings.push_back(binding);
			if (configuredKeymapBatchActive()) {
				MRConfiguredKeymapBatchState state = initializedKeymapBatchState();
				state.profiles = std::move(profiles);
				state.profilesDirty = true;
				storeConfiguredKeymapBatchStateValue(state);
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
