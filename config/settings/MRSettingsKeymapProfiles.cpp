#include "MRSettingsRuntime.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "MRSettingsThemesProfiles.hpp"
#include "MRSettingsHistory.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "../../keymap/MRKeymapResolver.hpp"
#include "../../mrmac/mrmac.h"
#include "../../ui/MRWindowSupport.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <sys/stat.h>

namespace {

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

std::string summarizeConfiguredKeymapsForLog(const std::vector<MRKeymapProfile> &profiles, std::string_view activeProfileName) {
	std::string text = "Keymap configured state: active='" + std::string(activeProfileName) + "' profiles=" + std::to_string(profiles.size());

	for (const MRKeymapProfile &profile : profiles)
		text += " [" + profile.name + ":" + std::to_string(profile.bindings.size()) + "]";
	return text;
}

} // namespace

std::string configuredDefaultProfileDescription() {
	recordSettingsRuntimeRead();
	return configuredDefaultProfileDescriptionValue();
}

bool setConfiguredDefaultProfileDescription(const std::string &value, std::string *errorMessage) {
	const std::string normalized = trimAscii(value);

	if (configuredDefaultProfileDescriptionValue() != normalized) {
		storeConfiguredDefaultProfileDescriptionValue(normalized);
		markConfiguredSettingsDirty();
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::vector<MRKeymapProfile> configuredKeymapProfiles() {
	recordSettingsRuntimeRead();
	return configuredKeymapProfilesValue();
}

bool setConfiguredKeymapProfiles(const std::vector<MRKeymapProfile> &profiles, std::string *errorMessage) {
	std::vector<MRKeymapProfile> normalized = profiles;
	std::string normalizedActiveProfile = configuredActiveKeymapProfileValue();
	std::string runtimeError;
	const std::vector<MRKeymapProfile> previousProfiles = configuredKeymapProfilesValue();
	const std::string previousActiveProfile = configuredActiveKeymapProfileValue();

	for (MRKeymapProfile &profile : normalized) {
		profile.name = trimAscii(profile.name);
		profile.description = trimAscii(profile.description);
		for (MRKeymapBindingRecord &binding : profile.bindings) {
			binding.profileName = trimAscii(binding.profileName);
			binding.target.target = trimAscii(binding.target.target);
			binding.description = trimAscii(binding.description);
		}
	}
	bool activeProfileExists = false;
	for (const MRKeymapProfile &profile : normalized) {
		if (profile.name == normalizedActiveProfile) {
			activeProfileExists = true;
			break;
		}
	}
	if (normalizedActiveProfile.empty() || !activeProfileExists) normalizedActiveProfile.clear();

	const auto diagnostics = validateKeymapProfiles(normalized);
	for (const MRKeymapDiagnostic &diagnostic : diagnostics)
		if (diagnostic.severity == MRKeymapDiagnosticSeverity::Error) return setError(errorMessage, diagnostic.message);
	if (!runtimeKeymapResolver().rebuild(normalized, normalizedActiveProfile, &runtimeError)) return setError(errorMessage, runtimeError);

	storeConfiguredKeymapProfilesValue(normalized);
	storeConfiguredActiveKeymapProfileValue(normalizedActiveProfile);
	if (previousProfiles != normalized || previousActiveProfile != normalizedActiveProfile) markConfiguredSettingsDirty();
	mrLogMessage(summarizeConfiguredKeymapsForLog(normalized, normalizedActiveProfile));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredKeymapFilePath() {
	const std::string configured = configuredKeymapFileValue();

	recordSettingsRuntimeRead();
	return configured.empty() ? std::string() : makeAbsolutePath(configured);
}

bool setConfiguredKeymapFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	struct stat st;

	if (normalized.empty()) {
		if (!configuredKeymapFileValue().empty()) storeConfiguredKeymapFileValue(std::string());
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (::stat(normalized.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return setError(errorMessage, "Keymap URI must include a filename.");
	normalized = makeAbsolutePath(normalized);
	storeConfiguredKeymapFileValue(normalized);
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::KeymapProfileLoad, normalized, nullptr));
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::KeymapProfileSave, normalized, nullptr));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredActiveKeymapProfile() {
	recordSettingsRuntimeRead();
	return configuredActiveKeymapProfileValue();
}

bool setConfiguredActiveKeymapProfile(const std::string &value, std::string *errorMessage) {
	std::string normalized = trimAscii(value);
	std::string runtimeError;
	const std::string previousActiveProfile = configuredActiveKeymapProfileValue();
	const std::vector<MRKeymapProfile> profiles = configuredKeymapProfilesValue();

	bool activeProfileExists = false;
	for (const MRKeymapProfile &profile : profiles) {
		if (profile.name == normalized) {
			activeProfileExists = true;
			break;
		}
	}
	if (!normalized.empty() && !activeProfileExists) normalized.clear();
	if (!runtimeKeymapResolver().rebuild(profiles, normalized, &runtimeError)) return setError(errorMessage, runtimeError);
	storeConfiguredActiveKeymapProfileValue(normalized);
	if (previousActiveProfile != normalized) markConfiguredSettingsDirty();
	if (normalized.empty()) mrLogMessage("Keymap active profile cleared; built-in key handling remains active.");
	else
		mrLogMessage("Keymap active profile set to '" + normalized + "'.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}
