#ifndef MRSETTINGSSOURCEMODEL_HPP
#define MRSETTINGSSOURCEMODEL_HPP

#include "MRSettingsStorage.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

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

std::string unescapeMrmacSingleQuotedLiteral(const std::string &value);
std::string joinStrings(const std::vector<std::string> &values, std::string_view separator);
MRParsedSettingsDocument parseSettingsDocument(std::string_view source, bool acceptLegacyFeProfileToken);
std::size_t countLegacyFeProfileDirectives(std::string_view source);
MRFlattenedSettingsDocument flattenSettingsDocument(const MRParsedSettingsDocument &document);
void appendChange(std::vector<MRSettingsChangeEntry> &changes, MRSettingsChangeEntry::Kind kind, const std::string &scope, const std::string &key, const std::string &oldValue, const std::string &newValue);
void diffFlatMap(const std::string &scope, const std::map<std::string, std::string> &beforeMap, const std::map<std::string, std::string> &afterMap, std::vector<MRSettingsChangeEntry> &changes);
void diffFlattenedDocuments(const MRFlattenedSettingsDocument &before, const MRFlattenedSettingsDocument &after, std::vector<MRSettingsChangeEntry> &changes);
void markFlag(MRSettingsLoadReport &report, MRSettingsLoadReport::Flag flag);
bool hasFlag(const MRSettingsLoadReport &report, MRSettingsLoadReport::Flag flag);
std::string quoteValue(const std::string &value);

#endif
