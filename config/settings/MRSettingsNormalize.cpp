#include "../../app/MRVersion.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "MRSettingsNormalize.hpp"
#include "MRSettingsSourceModel.hpp"

#include <set>
#include <string>

bool loadAndNormalizeSettingsSource(const std::string &settingsPath, const std::string &source, MRSettingsSnapshot &snapshot, MRSettingsLoadReport *report, std::string *errorMessage) {
	MRSettingsLoadReport localReport;
	MRSettingsLoadReport &activeReport = report != nullptr ? *report : localReport;
	MRParsedSettingsDocument document = parseSettingsDocument(source, false);
	std::set<std::string> canonicalKeysSeen;
	std::string activeSettingsPath = normalizeConfiguredPathInput(settingsPath);
	std::string applyError;
	const std::uint64_t currentPersistenceVersion = mrCurrentPersistenceVersion();

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
		if (keyClass == MRSettingsKeyClass::Version) {
			const std::string versionLiteral = trimAscii(assignment.value);
			std::uint64_t parsedVersion = 0;

			if (!mrParsePersistenceVersion(versionLiteral, parsedVersion)) {
				markFlag(activeReport, MRSettingsLoadReport::InvalidValueReset);
				++activeReport.ignoredAssignmentCount;
				continue;
			}
			if (parsedVersion > currentPersistenceVersion) {
				if (errorMessage != nullptr) *errorMessage = mrFuturePersistenceVersionMessage("Settings source", versionLiteral);
				return false;
			}
			if (parsedVersion < currentPersistenceVersion) markFlag(activeReport, MRSettingsLoadReport::VersionUpgradeRequired);
			++activeReport.appliedAssignmentCount;
			continue;
		}
		if (!applySettingsSnapshotAssignment(snapshot, assignment.key, assignment.value, &applyError)) {
			markFlag(activeReport, MRSettingsLoadReport::InvalidValueReset);
			++activeReport.ignoredAssignmentCount;
			continue;
		}
		++activeReport.appliedAssignmentCount;
	}

	if (!document.compilerProfileDirectives.empty()) snapshot.compilerProfiles.clear();
	for (const MRParsedCompilerProfileDirective &directive : document.compilerProfileDirectives)
		if (!applySettingsSnapshotCompilerProfileDirective(snapshot, directive.operation, directive.profileId, directive.arg3, directive.arg4, errorMessage)) return false;

	for (const MRParsedEditProfileDirective &directive : document.profileDirectives)
		if (!applySettingsSnapshotEditExtensionProfileDirective(snapshot, directive.operation, directive.profileId, directive.arg3, directive.arg4, errorMessage)) return false;

	if (canonicalKeysSeen.size() < canonicalSerializedSettingsKeyCount()) {
		const std::vector<std::string> canonicalKeys = canonicalSerializedSettingsKeys();

		for (const std::string &canonicalKey : canonicalKeys)
			if (canonicalKeysSeen.find(canonicalKey) == canonicalKeysSeen.end()) activeReport.defaultedCanonicalKeys.push_back(canonicalKey);
		activeReport.defaultedCanonicalKeyCount = activeReport.defaultedCanonicalKeys.size();
		markFlag(activeReport, MRSettingsLoadReport::MissingCanonicalKeyDefaulted);
	}

	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

namespace mr::settings::storage {

bool buildCanonicalSettingsSourceFromStorage(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource, std::string *errorMessage) {
	MRSettingsSnapshot snapshot;
	std::string normalizedPath = normalizeConfiguredPathInput(settingsPath);

	if (!loadAndNormalizeSettingsSource(normalizedPath, source, snapshot, report, errorMessage)) return false;
	canonicalSource = buildSettingsMacroSource(snapshot);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool prepareStartupSettingsSourceFromStorage(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource, std::string *errorMessage) {
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
	if (!activeReport.defaultedCanonicalKeys.empty()) {
		std::string defaultedKeysText;

		for (std::size_t i = 0; i < activeReport.defaultedCanonicalKeys.size(); ++i) {
			if (i != 0) defaultedKeysText += ", ";
			defaultedKeysText += activeReport.defaultedCanonicalKeys[i];
		}
		mrLogMessage(("Settings defaulted canonical keys: " + defaultedKeysText).c_str());
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

} // namespace mr::settings::storage
