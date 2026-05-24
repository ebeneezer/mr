#include "../../app/utils/MRStringUtils.hpp"
#include "MRSettingsSourceModel.hpp"

#include <algorithm>
#include <regex>
#include <set>
#include <utility>

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
		if (i != 0) out += separator;
		out += values[i];
	}
	return out;
}

MRParsedSettingsDocument parseSettingsDocument(std::string_view source, bool acceptLegacyFeProfileToken) {
	static const std::regex assignmentPattern("MRSETUP\\s*\\(\\s*'([^']+)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)", std::regex::icase);
	static const std::regex profilePattern("MRFEPROFILE\\s*\\(\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)", std::regex::icase);
	static const std::regex compilerProfilePattern("MRCOMPILERPROFILE\\s*\\(\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)", std::regex::icase);
	static const std::regex profilePatternWithLegacy("(?:MRFEPROFILE|MREDITPROFILE)\\s*\\(\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)", std::regex::icase);
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
	remaining.assign(source.data(), source.size());
	while (std::regex_search(remaining, match, compilerProfilePattern)) {
		if (match.size() >= 5) {
			MRParsedCompilerProfileDirective directive;
			directive.operation = unescapeMrmacSingleQuotedLiteral(match[1].str());
			directive.profileId = unescapeMrmacSingleQuotedLiteral(match[2].str());
			directive.arg3 = unescapeMrmacSingleQuotedLiteral(match[3].str());
			directive.arg4 = unescapeMrmacSingleQuotedLiteral(match[4].str());
			document.compilerProfileDirectives.push_back(std::move(directive));
		}
		remaining = match.suffix().str();
	}
	return document;
}

std::size_t countLegacyFeProfileDirectives(std::string_view source) {
	static const std::regex legacyProfilePattern("MREDITPROFILE\\s*\\(\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)", std::regex::icase);
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
		if (assignment.key == "PATH_HISTORY") flattened.globals[assignment.key + "[" + std::to_string(pathHistoryIndex++) + "]"] = assignment.value;
		else if (assignment.key == "FILE_HISTORY")
			flattened.globals[assignment.key + "[" + std::to_string(fileHistoryIndex++) + "]"] = assignment.value;
		else if (assignment.key == "DIALOG_LAST_PATH")
			flattened.globals[assignment.key + "[" + std::to_string(dialogLastPathIndex++) + "]"] = assignment.value;
		else if (assignment.key == "DIALOG_PATH_HISTORY")
			flattened.globals[assignment.key + "[" + std::to_string(dialogPathHistoryIndex++) + "]"] = assignment.value;
		else if (assignment.key == "DIALOG_FILE_HISTORY")
			flattened.globals[assignment.key + "[" + std::to_string(dialogFileHistoryIndex++) + "]"] = assignment.value;
		else if (assignment.key == "AUTOEXEC_MACRO")
			flattened.globals[assignment.key + "[" + std::to_string(autoexecMacroIndex++) + "]"] = assignment.value;
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
			if (profile.name.empty()) profile.name = trimAscii(directive.arg4);
			if (profile.name.empty()) profile.name = profileId;
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
	for (const MRParsedCompilerProfileDirective &directive : document.compilerProfileDirectives) {
		const std::string op = upperAscii(trimAscii(directive.operation));
		const std::string profileId = trimAscii(directive.profileId);
		MRFlattenedEditProfile &profile = flattened.compilerProfiles[profileId];
		const std::string key = upperAscii(trimAscii(directive.arg3));

		profile.id = profileId;
		if (op == "DEFINE") {
			profile.name = trimAscii(directive.arg3);
			profile.settings["TOOLCHAIN"] = directive.arg4;
		} else if (op == "SET")
			profile.settings[key] = directive.arg4;
	}

	return flattened;
}

void appendChange(std::vector<MRSettingsChangeEntry> &changes, MRSettingsChangeEntry::Kind kind, const std::string &scope, const std::string &key, const std::string &oldValue, const std::string &newValue) {
	MRSettingsChangeEntry change;

	change.kind = kind;
	change.scope = scope;
	change.key = key;
	change.oldValue = oldValue;
	change.newValue = newValue;
	changes.push_back(std::move(change));
}

void diffFlatMap(const std::string &scope, const std::map<std::string, std::string> &beforeMap, const std::map<std::string, std::string> &afterMap, std::vector<MRSettingsChangeEntry> &changes) {
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
		if (beforeIt->second != afterIt->second) appendChange(changes, MRSettingsChangeEntry::Kind::Changed, scope, key, beforeIt->second, afterIt->second);
	}
}

void diffFlattenedDocuments(const MRFlattenedSettingsDocument &before, const MRFlattenedSettingsDocument &after, std::vector<MRSettingsChangeEntry> &changes) {
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
			if (!beforeIt->second.extensions.empty()) beforeMap["EXTENSIONS"] = joinStrings(beforeIt->second.extensions, ", ");
			for (const auto &entry : beforeIt->second.settings)
				beforeMap[entry.first] = entry.second;
		}
		if (afterIt != after.profiles.end()) {
			afterMap["PROFILE_NAME"] = afterIt->second.name;
			if (!afterIt->second.extensions.empty()) afterMap["EXTENSIONS"] = joinStrings(afterIt->second.extensions, ", ");
			for (const auto &entry : afterIt->second.settings)
				afterMap[entry.first] = entry.second;
		}
		diffFlatMap(scope, beforeMap, afterMap, changes);
	}

	profileIds.clear();
	for (const auto &entry : before.compilerProfiles)
		profileIds.insert(entry.first);
	for (const auto &entry : after.compilerProfiles)
		profileIds.insert(entry.first);
	for (const std::string &profileId : profileIds) {
		auto beforeIt = before.compilerProfiles.find(profileId);
		auto afterIt = after.compilerProfiles.find(profileId);
		const std::string scope = "compiler-profile '" + profileId + "'";
		std::map<std::string, std::string> beforeMap;
		std::map<std::string, std::string> afterMap;

		if (beforeIt != before.compilerProfiles.end()) {
			beforeMap["PROFILE_NAME"] = beforeIt->second.name;
			for (const auto &entry : beforeIt->second.settings)
				beforeMap[entry.first] = entry.second;
		}
		if (afterIt != after.compilerProfiles.end()) {
			afterMap["PROFILE_NAME"] = afterIt->second.name;
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
