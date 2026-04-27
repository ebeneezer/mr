#include "../app/utils/MRStringUtils.hpp"
#include "MRSettingsLoader.hpp"

#include "MRDialogPaths.hpp"
#include "../ui/MRWindowSupport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

const char *keymapDiagnosticSeverityName(MRKeymapDiagnosticSeverity severity) noexcept {
	switch (severity) {
		case MRKeymapDiagnosticSeverity::Warning:
			return "warning";
		case MRKeymapDiagnosticSeverity::Error:
		default:
			return "error";
	}
}

std::string summarizeKeymapLoadForLog(const MRKeymapLoadResult &load) {
	std::string text =
	    "Keymap bootstrap parse: active='" + load.activeProfileName + "' profiles=" +
	    std::to_string(load.profiles.size()) + " diagnostics=" + std::to_string(load.diagnostics.size());

	for (const MRKeymapProfile &profile : load.profiles)
		text += " [" + profile.name + ":" + std::to_string(profile.bindings.size()) + "]";
	return text;
}

struct MRParsedSettingsAssignment {
	std::string key;
	std::string value;
};

struct MRParsedEditProfileDirective {
	std::string operation;
	std::string profileId;
	std::string arg3;
	std::string arg4;
};

struct MRParsedSettingsDocument {
	std::vector<MRParsedSettingsAssignment> assignments;
	std::vector<MRParsedEditProfileDirective> profileDirectives;
};

struct MRFlattenedEditProfile {
	std::string id;
	std::string name;
	std::vector<std::string> extensions;
	std::map<std::string, std::string> settings;
};

struct MRFlattenedSettingsDocument {
	std::map<std::string, std::string> globals;
	std::map<std::string, MRFlattenedEditProfile> profiles;
};



std::string unescapeMrmacSingleQuotedLiteral(const std::string &value) {
	std::string out;
	out.reserve(value.size());
	for (std::size_t i = 0; i < value.size(); ++i) {
		char ch = value[i];
		if (ch == '\'' && i + 1 < value.size() && value[i + 1] == '\'') {
			out.push_back('\'');
			++i;
		} else
			out.push_back(ch);
	}
	return out;
}

std::string joinStrings(const std::vector<std::string> &values, std::string_view separator) {
	std::string out;

	for (std::size_t i = 0; i < values.size(); ++i) {
		if (i != 0)
			out += separator;
		out += values[i];
	}
	return out;
}

MRParsedSettingsDocument parseSettingsDocument(std::string_view source, bool acceptLegacyFeProfileToken) {
	static const std::regex assignmentPattern(
	    "MRSETUP\\s*\\(\\s*'([^']+)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)", std::regex::icase);
	static const std::regex profilePattern(
	    "MRFEPROFILE\\s*\\(\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)",
	    std::regex::icase);
	static const std::regex profilePatternWithLegacy(
	    "(?:MRFEPROFILE|MREDITPROFILE)\\s*\\(\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)",
	    std::regex::icase);
	const std::regex &activeProfilePattern = acceptLegacyFeProfileToken ? profilePatternWithLegacy : profilePattern;
	MRParsedSettingsDocument document;
	std::smatch match;
	std::string remaining(source);

	while (std::regex_search(remaining, match, assignmentPattern)) {
		if (match.size() >= 3) {
			MRParsedSettingsAssignment assignment;
			assignment.key = upperAscii(trimAscii(match[1].str()));
			assignment.value = unescapeMrmacSingleQuotedLiteral(match[2].str());
			document.assignments.push_back(std::move(assignment));
		}
		remaining = match.suffix().str();
	}

	remaining.assign(source.data(), source.size());
	while (std::regex_search(remaining, match, activeProfilePattern)) {
		if (match.size() >= 5) {
			MRParsedEditProfileDirective directive;
			directive.operation = unescapeMrmacSingleQuotedLiteral(match[1].str());
			directive.profileId = unescapeMrmacSingleQuotedLiteral(match[2].str());
			directive.arg3 = unescapeMrmacSingleQuotedLiteral(match[3].str());
			directive.arg4 = unescapeMrmacSingleQuotedLiteral(match[4].str());
			document.profileDirectives.push_back(std::move(directive));
		}
		remaining = match.suffix().str();
	}
	return document;
}

std::size_t countLegacyFeProfileDirectives(std::string_view source) {
	static const std::regex legacyProfilePattern(
	    "MREDITPROFILE\\s*\\(\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)",
	    std::regex::icase);
	std::smatch match;
	std::string remaining(source);
	std::size_t count = 0;

	while (std::regex_search(remaining, match, legacyProfilePattern)) {
		++count;
		remaining = match.suffix().str();
	}
	return count;
}

MRFlattenedSettingsDocument flattenSettingsDocument(const MRParsedSettingsDocument &document) {
	MRFlattenedSettingsDocument flattened;
	std::size_t pathHistoryIndex = 1;
	std::size_t fileHistoryIndex = 1;
	std::size_t dialogLastPathIndex = 1;
	std::size_t dialogPathHistoryIndex = 1;
	std::size_t dialogFileHistoryIndex = 1;
	std::size_t autoexecMacroIndex = 1;

	for (const MRParsedSettingsAssignment &assignment : document.assignments)
		if (assignment.key == "PATH_HISTORY")
			flattened.globals[assignment.key + "[" + std::to_string(pathHistoryIndex++) + "]"] = assignment.value;
		else if (assignment.key == "FILE_HISTORY")
			flattened.globals[assignment.key + "[" + std::to_string(fileHistoryIndex++) + "]"] = assignment.value;
		else if (assignment.key == "DIALOG_LAST_PATH")
			flattened.globals[assignment.key + "[" + std::to_string(dialogLastPathIndex++) + "]"] =
			    assignment.value;
		else if (assignment.key == "DIALOG_PATH_HISTORY")
			flattened.globals[assignment.key + "[" + std::to_string(dialogPathHistoryIndex++) + "]"] =
			    assignment.value;
		else if (assignment.key == "DIALOG_FILE_HISTORY")
			flattened.globals[assignment.key + "[" + std::to_string(dialogFileHistoryIndex++) + "]"] =
			    assignment.value;
		else if (assignment.key == "AUTOEXEC_MACRO")
			flattened.globals[assignment.key + "[" + std::to_string(autoexecMacroIndex++) + "]"] =
			    assignment.value;
		else
			flattened.globals[assignment.key] = assignment.value;

	for (const MRParsedEditProfileDirective &directive : document.profileDirectives) {
		const std::string op = upperAscii(trimAscii(directive.operation));
		const std::string profileId = trimAscii(directive.profileId);
		MRFlattenedEditProfile &profile = flattened.profiles[profileId];
		const std::string key = upperAscii(trimAscii(directive.arg3));

		profile.id = profileId;
		if (op == "DEFINE") {
			profile.name = trimAscii(directive.arg3);
			if (profile.name.empty())
				profile.name = trimAscii(directive.arg4);
			if (profile.name.empty())
				profile.name = profileId;
		} else if (op == "EXT") {
			profile.extensions.push_back(normalizeEditExtensionSelector(directive.arg3));
		} else if (op == "SET")
			profile.settings[key] = directive.arg4;
	}

	for (auto &entry : flattened.profiles) {
		auto &extensions = entry.second.extensions;
		std::sort(extensions.begin(), extensions.end());
		extensions.erase(std::unique(extensions.begin(), extensions.end()), extensions.end());
	}

	return flattened;
}

void appendChange(std::vector<MRSettingsChangeEntry> &changes, MRSettingsChangeEntry::Kind kind,
                  const std::string &scope, const std::string &key, const std::string &oldValue,
                  const std::string &newValue) {
	MRSettingsChangeEntry change;

	change.kind = kind;
	change.scope = scope;
	change.key = key;
	change.oldValue = oldValue;
	change.newValue = newValue;
	changes.push_back(std::move(change));
}

void diffFlatMap(const std::string &scope, const std::map<std::string, std::string> &beforeMap,
                 const std::map<std::string, std::string> &afterMap,
                 std::vector<MRSettingsChangeEntry> &changes) {
	std::set<std::string> keys;

	for (const auto &entry : beforeMap)
		keys.insert(entry.first);
	for (const auto &entry : afterMap)
		keys.insert(entry.first);

	for (const std::string &key : keys) {
		auto beforeIt = beforeMap.find(key);
		auto afterIt = afterMap.find(key);

		if (beforeIt == beforeMap.end()) {
			appendChange(changes, MRSettingsChangeEntry::Kind::Added, scope, key, std::string(), afterIt->second);
			continue;
		}
		if (afterIt == afterMap.end()) {
			appendChange(changes, MRSettingsChangeEntry::Kind::Removed, scope, key, beforeIt->second, std::string());
			continue;
		}
		if (beforeIt->second != afterIt->second)
			appendChange(changes, MRSettingsChangeEntry::Kind::Changed, scope, key, beforeIt->second,
			             afterIt->second);
	}
}

void diffFlattenedDocuments(const MRFlattenedSettingsDocument &before, const MRFlattenedSettingsDocument &after,
                            std::vector<MRSettingsChangeEntry> &changes) {
	diffFlatMap("settings", before.globals, after.globals, changes);

	std::set<std::string> profileIds;
	for (const auto &entry : before.profiles)
		profileIds.insert(entry.first);
	for (const auto &entry : after.profiles)
		profileIds.insert(entry.first);

	for (const std::string &profileId : profileIds) {
		auto beforeIt = before.profiles.find(profileId);
		auto afterIt = after.profiles.find(profileId);
		const std::string scope = "fe-profile '" + profileId + "'";
		std::map<std::string, std::string> beforeMap;
		std::map<std::string, std::string> afterMap;

		if (beforeIt != before.profiles.end()) {
			beforeMap["PROFILE_NAME"] = beforeIt->second.name;
			if (!beforeIt->second.extensions.empty())
				beforeMap["EXTENSIONS"] = joinStrings(beforeIt->second.extensions, ", ");
			for (const auto &entry : beforeIt->second.settings)
				beforeMap[entry.first] = entry.second;
		}
		if (afterIt != after.profiles.end()) {
			afterMap["PROFILE_NAME"] = afterIt->second.name;
			if (!afterIt->second.extensions.empty())
				afterMap["EXTENSIONS"] = joinStrings(afterIt->second.extensions, ", ");
			for (const auto &entry : afterIt->second.settings)
				afterMap[entry.first] = entry.second;
		}
		diffFlatMap(scope, beforeMap, afterMap, changes);
	}
}

void markFlag(MRSettingsLoadReport &report, MRSettingsLoadReport::Flag flag) {
	report.flags |= static_cast<unsigned int>(flag);
}

bool hasFlag(const MRSettingsLoadReport &report, MRSettingsLoadReport::Flag flag) {
	return (report.flags & static_cast<unsigned int>(flag)) != 0;
}

std::string quoteValue(const std::string &value) {
	return "'" + value + "'";
}

} // namespace

bool loadAndNormalizeSettingsSource(const std::string &settingsPath, const std::string &source,
                                    MRSettingsLoadReport *report, std::string *errorMessage) {
	MRSettingsLoadReport localReport;
	MRSettingsLoadReport &activeReport = report != nullptr ? *report : localReport;
	MRParsedSettingsDocument document = parseSettingsDocument(source, false);
	MRSetupPaths activePaths;
	std::set<std::string> canonicalKeysSeen;
	std::string activeSettingsPath = normalizeConfiguredPathInput(settingsPath);
	std::string applyError;
	std::string themeError;

	activeReport = MRSettingsLoadReport();
	if (countLegacyFeProfileDirectives(source) != 0)
		markFlag(activeReport, MRSettingsLoadReport::ObsoleteFeProfileDropped);
	if (!resetConfiguredSettingsModel(activeSettingsPath, activePaths, errorMessage))
		return false;

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
		if (keyClass == MRSettingsKeyClass::ColorInline)
			markFlag(activeReport, MRSettingsLoadReport::LegacyInlineColorsSeen);
		if (assignment.key == "SETTINGSPATH" && normalizeConfiguredPathInput(assignment.value) != activeSettingsPath)
			markFlag(activeReport, MRSettingsLoadReport::AnchoredSettingsPath);
		if (!applyConfiguredSettingsAssignment(assignment.key, assignment.value, activePaths, &applyError)) {
			markFlag(activeReport, MRSettingsLoadReport::InvalidValueReset);
			++activeReport.ignoredAssignmentCount;
			continue;
		}
		++activeReport.appliedAssignmentCount;
	}

	for (const MRParsedEditProfileDirective &directive : document.profileDirectives)
		if (!applyConfiguredEditExtensionProfileDirective(directive.operation, directive.profileId, directive.arg3,
		                                                 directive.arg4, errorMessage))
			return false;

	{
		MRKeymapLoadResult keymapLoad = loadKeymapProfilesFromSettingsSource(source);
		bool keymapHasError = false;

		mrLogMessage(summarizeKeymapLoadForLog(keymapLoad));
		for (const MRKeymapDiagnostic &diagnostic : keymapLoad.diagnostics)
			mrLogMessage("Keymap bootstrap diagnostic [" +
			             std::string(keymapDiagnosticSeverityName(diagnostic.severity)) + "]: " +
			             diagnostic.message);
		for (const MRKeymapDiagnostic &diagnostic : keymapLoad.diagnostics)
			if (diagnostic.severity == MRKeymapDiagnosticSeverity::Error) {
				keymapHasError = true;
				break;
			}
		if (keymapHasError) {
			mrLogMessage("Keymap bootstrap reset: falling back to DEFAULT because diagnostics contain errors.");
			markFlag(activeReport, MRSettingsLoadReport::InvalidValueReset);
			if (!setConfiguredKeymapProfiles(std::vector<MRKeymapProfile>(), errorMessage))
				return false;
			if (!setConfiguredActiveKeymapProfile("DEFAULT", errorMessage))
				return false;
		} else {
			if (!setConfiguredKeymapProfiles(keymapLoad.profiles, errorMessage))
				return false;
			if (!setConfiguredActiveKeymapProfile(keymapLoad.activeProfileName, errorMessage))
				return false;
			mrLogMessage("Keymap bootstrap applied without reset.");
		}
	}

	if (canonicalKeysSeen.size() < canonicalSerializedSettingsKeyCount()) {
		activeReport.defaultedCanonicalKeyCount = canonicalSerializedSettingsKeyCount() - canonicalKeysSeen.size();
		markFlag(activeReport, MRSettingsLoadReport::MissingCanonicalKeyDefaulted);
	}

	if (!loadColorThemeFile(configuredColorThemeFilePath(), &themeError)) {
		MRColorSetupSettings defaults = resolveColorSetupDefaults();
		markFlag(activeReport, MRSettingsLoadReport::ThemeFallbackUsed);
		if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, defaults.windowColors.data(),
		                                        defaults.windowColors.size(), errorMessage) ||
		    !setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, defaults.menuDialogColors.data(),
		                                        defaults.menuDialogColors.size(), errorMessage) ||
		    !setConfiguredColorSetupGroupValues(MRColorSetupGroup::Help, defaults.helpColors.data(),
		                                        defaults.helpColors.size(), errorMessage) ||
		    !setConfiguredColorSetupGroupValues(MRColorSetupGroup::Other, defaults.otherColors.data(),
		                                        defaults.otherColors.size(), errorMessage))
			return false;
	}

	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string describeSettingsLoadReport(const MRSettingsLoadReport &report) {
	std::vector<std::string> parts;
	std::string text;

	if (hasFlag(report, MRSettingsLoadReport::UnknownKeyDropped))
		parts.emplace_back("unknown keys dropped");
	if (hasFlag(report, MRSettingsLoadReport::DuplicateKeySeen))
		parts.emplace_back("duplicates resolved");
	if (hasFlag(report, MRSettingsLoadReport::InvalidValueReset))
		parts.emplace_back("invalid values reset to defaults");
	if (hasFlag(report, MRSettingsLoadReport::MissingCanonicalKeyDefaulted))
		parts.emplace_back("missing canonical keys defaulted");
	if (hasFlag(report, MRSettingsLoadReport::LegacyInlineColorsSeen))
		parts.emplace_back("legacy inline colors normalized");
	if (hasFlag(report, MRSettingsLoadReport::ThemeFallbackUsed))
		parts.emplace_back("theme fallback applied");
	if (hasFlag(report, MRSettingsLoadReport::AnchoredSettingsPath))
		parts.emplace_back("settings path anchored to active file");
	if (hasFlag(report, MRSettingsLoadReport::ObsoleteFeProfileDropped))
		parts.emplace_back("obsolete MREDITPROFILE directives dropped; FE profile defaults restored");
	if (parts.empty())
		return std::string();
	for (std::size_t i = 0; i < parts.size(); ++i) {
		if (i != 0)
			text += "; ";
		text += parts[i];
	}
	return text;
}

bool diffSettingsSources(const std::string &beforeSource, const std::string &afterSource,
                         std::vector<MRSettingsChangeEntry> &changes, std::string *errorMessage) {
	const MRFlattenedSettingsDocument before = flattenSettingsDocument(parseSettingsDocument(beforeSource, true));
	const MRFlattenedSettingsDocument after = flattenSettingsDocument(parseSettingsDocument(afterSource, true));

	changes.clear();
	diffFlattenedDocuments(before, after, changes);
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string formatSettingsChangeForLog(const MRSettingsChangeEntry &change) {
	std::string text = change.scope + " ";

	if (change.kind == MRSettingsChangeEntry::Kind::Added)
		text += "+ " + change.key + " = " + quoteValue(change.newValue);
	else if (change.kind == MRSettingsChangeEntry::Kind::Removed)
		text += "- " + change.key + " (was " + quoteValue(change.oldValue) + ")";
	else
		text += change.key + ": " + quoteValue(change.oldValue) + " -> " + quoteValue(change.newValue);
	return text;
}
