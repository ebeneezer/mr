#include "../../keymap/MRKeymapResolver.hpp"
#include "../../app/commands/MRWindowCommands.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include <tvision/tv.h>
#include "MRSettingsNormalize.hpp"
#include "MRSettingsSourceModel.hpp"

#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

const char *keymapDiagnosticSeverityName(MRKeymapDiagnosticSeverity severity) noexcept {
	switch (severity) {
		case MRKeymapDiagnosticSeverity::Warning:
			return "warning";
		case MRKeymapDiagnosticSeverity::Error:
		default:
			return "error";
	}
}

std::string keymapDiagnosticIdentity(const MRKeymapDiagnostic &diagnostic) {
	return std::to_string(static_cast<unsigned>(diagnostic.kind)) + "|" + std::to_string(static_cast<unsigned>(diagnostic.severity)) + "|" + std::to_string(diagnostic.profileIndex) + "|" + std::to_string(diagnostic.bindingIndex) + "|" + diagnostic.message;
}

std::string describeKeymapDiagnostic(std::span<const MRKeymapProfile> profiles, const MRKeymapDiagnostic &diagnostic) {
	std::string text = diagnostic.message;

	if (diagnostic.profileIndex != kNoIndex && diagnostic.profileIndex < profiles.size()) {
		const MRKeymapProfile &profile = profiles[diagnostic.profileIndex];
		text += " profile='" + profile.name + "'";
		if (diagnostic.bindingIndex != kNoIndex && diagnostic.bindingIndex < profile.bindings.size()) {
			const MRKeymapBindingRecord &binding = profile.bindings[diagnostic.bindingIndex];
			text += " binding=" + std::to_string(diagnostic.bindingIndex + 1);
			text += " target='" + binding.target.target + "'";
			text += " sequence='" + binding.sequence.toString() + "'";
		}
	}
	return text;
}

std::string summarizeKeymapLoadForLog(const MRKeymapLoadResult &load) {
	std::string text = "Keymap bootstrap parse: active='" + load.activeProfileName + "' profiles=" + std::to_string(load.profiles.size()) + " diagnostics=" + std::to_string(load.diagnostics.size());

	for (const MRKeymapProfile &profile : load.profiles)
		text += " [" + profile.name + ":" + std::to_string(profile.bindings.size()) + "]";
	return text;
}

std::string summarizeKeymapDiagnosticsForMessageLine(std::span<const MRKeymapDiagnostic> diagnostics, std::string_view operation) {
	std::set<std::string> seen;
	std::size_t errorCount = 0;
	std::size_t warningCount = 0;

	for (const MRKeymapDiagnostic &diagnostic : diagnostics) {
		if (!seen.insert(keymapDiagnosticIdentity(diagnostic)).second) continue;
		if (diagnostic.severity == MRKeymapDiagnosticSeverity::Error) ++errorCount;
		else
			++warningCount;
	}
	if (errorCount == 0 && warningCount == 0) return std::string();
	if (errorCount == 0) return std::string(operation) + ": " + std::to_string(warningCount) + " warning(s); see log.";
	return std::string(operation) + ": removed " + std::to_string(errorCount) + " invalid key binding(s); see log.";
}

} // namespace

bool loadAndNormalizeSettingsSource(const std::string &settingsPath, const std::string &source, MRSettingsSnapshot &snapshot, MRSettingsLoadReport *report, std::string *errorMessage) {
	MRSettingsLoadReport localReport;
	MRSettingsLoadReport &activeReport = report != nullptr ? *report : localReport;
	MRParsedSettingsDocument document = parseSettingsDocument(source, false);
	std::set<std::string> canonicalKeysSeen;
	std::string activeSettingsPath = normalizeConfiguredPathInput(settingsPath);
	std::string applyError;
	std::string themeError;

	activeReport = MRSettingsLoadReport();
	if (countLegacyFeProfileDirectives(source) != 0) markFlag(activeReport, MRSettingsLoadReport::ObsoleteFeProfileDropped);
	if (!resetSettingsSnapshot(activeSettingsPath, snapshot, errorMessage)) return false;

	for (const MRParsedSettingsAssignment &assignment : document.assignments) {
		MRSettingsKeyClass keyClass = classifySettingsKey(assignment.key);

		if (keyClass == MRSettingsKeyClass::Unknown) {
			markFlag(activeReport, MRSettingsLoadReport::UnknownKeyDropped);
			++activeReport.ignoredAssignmentCount;
			continue;
		}
		if (isCanonicalSerializedSettingsKey(assignment.key) && !canonicalKeysSeen.insert(assignment.key).second) {
			markFlag(activeReport, MRSettingsLoadReport::DuplicateKeySeen);
			++activeReport.duplicateAssignmentCount;
		}
		if (keyClass == MRSettingsKeyClass::ColorInline) markFlag(activeReport, MRSettingsLoadReport::LegacyInlineColorsSeen);
		if (assignment.key == "SETTINGSPATH" && normalizeConfiguredPathInput(assignment.value) != activeSettingsPath) markFlag(activeReport, MRSettingsLoadReport::AnchoredSettingsPath);
		if (!applySettingsSnapshotAssignment(snapshot, assignment.key, assignment.value, &applyError)) {
			markFlag(activeReport, MRSettingsLoadReport::InvalidValueReset);
			++activeReport.ignoredAssignmentCount;
			continue;
		}
		++activeReport.appliedAssignmentCount;
	}

	for (const MRParsedEditProfileDirective &directive : document.profileDirectives)
		if (!applySettingsSnapshotEditExtensionProfileDirective(snapshot, directive.operation, directive.profileId, directive.arg3, directive.arg4, errorMessage)) return false;

	{
		MRKeymapLoadResult keymapLoad = loadKeymapProfilesFromSettingsSource(source);
		const std::string diagnosticSummary = summarizeKeymapDiagnosticsForMessageLine(keymapLoad.diagnostics, "Keymap bootstrap");
		std::set<std::string> loggedDiagnostics;

		mrLogMessage(summarizeKeymapLoadForLog(keymapLoad));
		for (const MRKeymapDiagnostic &diagnostic : keymapLoad.diagnostics) {
			if (!loggedDiagnostics.insert(keymapDiagnosticIdentity(diagnostic)).second) continue;
			mrLogMessage("Keymap bootstrap diagnostic [" + std::string(keymapDiagnosticSeverityName(diagnostic.severity)) + "]: " + describeKeymapDiagnostic(keymapLoad.profiles, diagnostic));
		}
		if (!diagnosticSummary.empty()) mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, diagnosticSummary, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		if (keymapLoad.profiles.empty()) keymapLoad.profiles.push_back(builtInDefaultKeymapProfile());
		if (keymapLoad.activeProfileName.empty()) keymapLoad.activeProfileName = "DEFAULT";
		if (std::ranges::find(keymapLoad.profiles, keymapLoad.activeProfileName, &MRKeymapProfile::name) == keymapLoad.profiles.end()) keymapLoad.activeProfileName = "DEFAULT";
		if (keymapLoad.activeProfileName.empty() || std::ranges::find(keymapLoad.profiles, keymapLoad.activeProfileName, &MRKeymapProfile::name) == keymapLoad.profiles.end()) {
			mrLogMessage("Keymap bootstrap canonicalized result could not be applied; falling back to DEFAULT.");
			markFlag(activeReport, MRSettingsLoadReport::InvalidValueReset);
			snapshot.keymapProfiles = std::vector<MRKeymapProfile>{builtInDefaultKeymapProfile()};
			snapshot.activeKeymapProfile = "DEFAULT";
		} else {
			snapshot.keymapProfiles = std::move(keymapLoad.profiles);
			snapshot.activeKeymapProfile = std::move(keymapLoad.activeProfileName);
			mrLogMessage("Keymap bootstrap applied canonicalized result.");
		}
	}

	if (canonicalKeysSeen.size() < canonicalSerializedSettingsKeyCount()) {
		activeReport.defaultedCanonicalKeyCount = canonicalSerializedSettingsKeyCount() - canonicalKeysSeen.size();
		markFlag(activeReport, MRSettingsLoadReport::MissingCanonicalKeyDefaulted);
	}

	if (!loadColorThemeFileIntoSettingsSnapshot(snapshot, &themeError)) {
		markFlag(activeReport, MRSettingsLoadReport::ThemeFallbackUsed);
		mrLogMessage("Settings normalization retained staged color setup after theme load failure: " + snapshot.colorThemeFilePath + " (" + themeError + ")");
	}

	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

namespace mr::settings::storage {

bool buildCanonicalSettingsSourceImpl(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource, std::string *errorMessage) {
	MRSettingsSnapshot snapshot;
	std::string normalizedPath = normalizeConfiguredPathInput(settingsPath);

	if (!loadAndNormalizeSettingsSource(normalizedPath, source, snapshot, report, errorMessage)) return false;
	canonicalSource = buildSettingsMacroSource(snapshot);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool prepareStartupSettingsSourceImpl(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource, std::string *errorMessage) {
	MRSettingsLoadReport localReport;
	MRSettingsLoadReport &activeReport = report != nullptr ? *report : localReport;
	MRSettingsSnapshot snapshot;
	std::string normalizedPath = normalizeConfiguredPathInput(settingsPath);
	std::string rewriteError;
	std::string summary;

	activeReport = MRSettingsLoadReport();
	if (!loadAndNormalizeSettingsSource(normalizedPath, source, snapshot, &activeReport, errorMessage)) return false;
	canonicalSource = buildSettingsMacroSource(snapshot);
	if (!activeReport.normalized()) {
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (!writeNormalizedBootstrapFiles(snapshot, source, canonicalSource, &rewriteError)) {
		if (errorMessage != nullptr) *errorMessage = "Settings rewrite failed: " + rewriteError;
		return false;
	}
	summary = describeSettingsLoadReport(activeReport);
	mrLogMessage(("Settings normalized: " + normalizedPath).c_str());
	if (!summary.empty()) mrLogMessage(("Settings normalization details: " + summary).c_str());
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

} // namespace mr::settings::storage
