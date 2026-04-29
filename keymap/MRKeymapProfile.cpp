#include "MRKeymapProfile.hpp"

#include "MRKeymapActionCatalog.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#include "../app/utils/MRStringUtils.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../mrmac/mrmac.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);
constexpr std::string_view kActiveKeymapProfileSetupKey = "ACTIVE_KEYMAP_PROFILE";
constexpr std::string_view kKeymapProfileSetupKey = "KEYMAP_PROFILE";
constexpr std::string_view kKeymapBindingSetupKey = "KEYMAP_BIND";
constexpr std::string_view kProfileNameMember = "NAME";
constexpr std::string_view kProfileDescriptionMember = "DESCRIPTION";
constexpr std::string_view kBindingContextMember = "CONTEXT";
constexpr std::string_view kBindingTypeMember = "TYPE";
constexpr std::string_view kBindingTargetMember = "TARGET";
constexpr std::string_view kBindingSequenceMember = "SEQUENCE";
const char *const kAssignmentPattern = "MRSETUP\\s*\\(\\s*'([^']+)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)";

struct PayloadMember {
	std::string key;
	std::string value;
};

struct TargetTypeSpec {
	std::string_view name;
	MRKeymapBindingType type;
};

constexpr std::array targetTypes{
    TargetTypeSpec{"ACTION", MRKeymapBindingType::Action},
    TargetTypeSpec{"MACRO", MRKeymapBindingType::Macro},
};

MRKeymapDiagnostic makeDiagnostic(MRKeymapDiagnosticKind kind, MRKeymapDiagnosticSeverity severity, std::size_t profileIndex, std::size_t bindingIndex, std::string message) {
	return {kind, severity, profileIndex, bindingIndex, std::move(message)};
}

bool sameBindingIdentity(const MRKeymapBindingRecord &lhs, const MRKeymapBindingRecord &rhs) noexcept {
	return lhs.profileName == rhs.profileName && lhs.context == rhs.context && lhs.target == rhs.target && lhs.sequence == rhs.sequence;
}

bool sameDispatchSlot(const MRKeymapBindingRecord &lhs, const MRKeymapBindingRecord &rhs) noexcept {
	return lhs.profileName == rhs.profileName && lhs.context == rhs.context && lhs.sequence == rhs.sequence;
}

std::string displayBindingTarget(std::string_view target) {
	std::string name(target);

	if (name.rfind("MRMAC_", 0) == 0) return name.substr(6);
	if (name.rfind("MR_", 0) == 0) return name.substr(3);
	return name;
}

bool isPhase1CtrlAsciiCollision(const MRKeymapToken &token) noexcept {
	if (!token.hasModifier(MRKeymapModifier::Ctrl) || token.hasModifier(MRKeymapModifier::Alt) || token.hasModifier(MRKeymapModifier::Shift)) return false;
	if (token.baseKey() == MRKeymapBaseKey::Backspace || token.baseKey() == MRKeymapBaseKey::Tab || token.baseKey() == MRKeymapBaseKey::Enter) return true;
	if (token.baseKey() != MRKeymapBaseKey::Printable) return false;
	switch (std::toupper(static_cast<unsigned char>(token.printableKey()))) {
		case 'H':
		case 'I':
		case 'J':
		case 'M':
			return true;
		default:
			return false;
	}
}

bool sequenceHasPhase1CtrlAsciiCollision(const MRKeymapSequence &sequence) noexcept {
	for (const MRKeymapToken &token : sequence.tokens())
		if (isPhase1CtrlAsciiCollision(token)) return true;
	return false;
}

std::string macroSpecFilePart(std::string_view target) {
	const std::size_t caretPos = target.find('^');

	return caretPos == std::string_view::npos ? std::string() : std::string(trimAscii(target.substr(0, caretPos)));
}

std::string macroSpecMacroPart(std::string_view target) {
	const std::size_t caretPos = target.find('^');

	if (caretPos == std::string_view::npos) return trimAscii(std::string(target));
	return trimAscii(std::string(target.substr(caretPos + 1)));
}

std::string resolveMacroBindingFilePath(const std::string &spec) {
	std::string trimmed = trimAscii(spec);
	std::string macroDirectory = normalizeConfiguredPathInput(configuredMacroDirectoryPath());
	auto tryMacroDirectory = [&](const std::string &candidate) -> std::string {
		std::string joined;

		if (macroDirectory.empty() || candidate.empty()) return std::string();
		joined = macroDirectory;
		if (joined.back() != '/') joined += '/';
		joined += candidate;
		return ::access(joined.c_str(), F_OK) == 0 ? joined : std::string();
	};

	if (trimmed.empty()) return std::string();
	if (::access(trimmed.c_str(), F_OK) == 0) return trimmed;
	if (std::string fromMacroDirectory = tryMacroDirectory(trimmed); !fromMacroDirectory.empty()) return fromMacroDirectory;
	if (upperAscii(trimmed).size() < 6 || upperAscii(trimmed).substr(upperAscii(trimmed).size() - 6) != ".MRMAC") {
		std::string withExt = trimmed + ".mrmac";
		if (::access(withExt.c_str(), F_OK) == 0) return withExt;
		if (std::string withExtFromMacroDirectory = tryMacroDirectory(withExt); !withExtFromMacroDirectory.empty()) return withExtFromMacroDirectory;
	}
	return std::string();
}

struct MacroBindingFileValidation {
	std::set<std::string> macroNames;
	std::string compileError;
};

const MacroBindingFileValidation &validateMacroBindingFile(const std::string &filePart, std::map<std::string, MacroBindingFileValidation> &cache) {
	const std::string cacheKey = upperAscii(trimAscii(filePart));
	auto cached = cache.find(cacheKey);

	if (cached != cache.end()) return cached->second;

	MacroBindingFileValidation validation;
	std::string source;
	std::size_t bytecodeSize = 0;
	const std::string resolvedPath = resolveMacroBindingFilePath(filePart);
	unsigned char *bytecode = nullptr;

	if (resolvedPath.empty()) {
		validation.compileError = "Macro file could not be resolved.";
		return cache.emplace(cacheKey, std::move(validation)).first->second;
	}
	if (!readTextFile(resolvedPath, source)) {
		validation.compileError = "Macro file could not be read.";
		return cache.emplace(cacheKey, std::move(validation)).first->second;
	}
	bytecode = compile_macro_code(source.c_str(), &bytecodeSize);
	if (bytecode == nullptr) {
		const char *err = get_last_compile_error();
		validation.compileError = err != nullptr && *err != '\0' ? err : "Compilation failed.";
		return cache.emplace(cacheKey, std::move(validation)).first->second;
	}
	for (int i = 0; i < get_compiled_macro_count(); ++i) {
		const char *compiledName = get_compiled_macro_name(i);
		const std::string macroName = trimAscii(compiledName != nullptr ? compiledName : "");

		if (!macroName.empty()) validation.macroNames.insert(upperAscii(macroName));
	}
	std::free(bytecode);
	return cache.emplace(cacheKey, std::move(validation)).first->second;
}

std::string summarizeMacroBindingError(std::string_view message) {
	const std::size_t pos = message.find_first_of("\r\n");

	if (pos == std::string_view::npos) return trimAscii(std::string(message));
	return trimAscii(std::string(message.substr(0, pos)));
}

bool bindingHasMacroError(const MRKeymapBindingRecord &binding, std::string &errorText, std::map<std::string, MacroBindingFileValidation> &cache) {
	const std::string filePart = macroSpecFilePart(binding.target.target);
	const std::string macroPart = macroSpecMacroPart(binding.target.target);

	errorText.clear();
	if (binding.target.type != MRKeymapBindingType::Macro) return false;
	if (filePart.empty()) {
		errorText = "Macro target has no file qualifier.";
		return true;
	}
	if (macroPart.empty()) {
		errorText = "Macro target has no macro name.";
		return true;
	}
	const MacroBindingFileValidation &validation = validateMacroBindingFile(filePart, cache);
	if (!validation.compileError.empty()) {
		errorText = summarizeMacroBindingError(validation.compileError);
		return true;
	}
	if (validation.macroNames.find(upperAscii(macroPart)) == validation.macroNames.end()) {
		errorText = "Macro not found in file.";
		return true;
	}
	return false;
}

bool isSequencePrefix(const MRKeymapSequence &prefix, const MRKeymapSequence &sequence) {
	if (prefix.size() >= sequence.size()) return false;
	for (std::size_t i = 0; i < prefix.size(); ++i)
		if (prefix.tokens()[i] != sequence.tokens()[i]) return false;
	return true;
}

bool hasSequencePrefixCollision(const MRKeymapBindingRecord &lhs, const MRKeymapBindingRecord &rhs) {
	return lhs.context == rhs.context && (isSequencePrefix(lhs.sequence, rhs.sequence) || isSequencePrefix(rhs.sequence, lhs.sequence));
}

std::size_t findProfileIndex(std::span<const MRKeymapProfile> profiles, std::string_view name) {
	for (std::size_t i = 0; i < profiles.size(); ++i)
		if (profiles[i].name == name) return i;
	return kNoIndex;
}

void normalizeKeymapProfile(MRKeymapProfile &profile) {
	profile.name = trimAscii(profile.name);
	profile.description = trimAscii(profile.description);
	if (upperAscii(profile.name) == "DEFAULT") profile.name = "DEFAULT";
	for (MRKeymapBindingRecord &binding : profile.bindings) {
		binding.profileName = trimAscii(binding.profileName);
		binding.target.target = trimAscii(binding.target.target);
		binding.description = trimAscii(binding.description);
	}
}

bool isPayloadKeyStart(char ch) noexcept {
	const unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalpha(uch) != 0;
}

bool isPayloadKeyChar(char ch) noexcept {
	const unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalnum(uch) != 0 || ch == '_';
}

bool isPayloadSpace(char ch) noexcept {
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

std::string unescapeMrmacSingleQuotedLiteral(std::string_view value) {
	std::string out;
	out.reserve(value.size());
	for (std::size_t i = 0; i < value.size(); ++i) {
		const char ch = value[i];
		if (ch == '\'' && i + 1 < value.size() && value[i + 1] == '\'') {
			out.push_back('\'');
			++i;
		} else
			out.push_back(ch);
	}
	return out;
}

std::string describeBindingLocation(std::size_t bindingIndex) {
	if (bindingIndex == kNoIndex) return "payload";
	return "binding payload";
}

std::string escapePayloadQuotedString(std::string_view value) {
	std::string escaped;

	escaped.reserve(value.size() + 8);
	for (const char ch : value)
		switch (ch) {
			case '"':
				escaped += "\\\"";
				break;
			case '\\':
				escaped += "\\\\";
				break;
			case '\n':
				escaped += "\\n";
				break;
			case '\r':
				escaped += "\\r";
				break;
			case '\t':
				escaped += "\\t";
				break;
			default:
				escaped.push_back(ch);
				break;
		}
	return escaped;
}

std::string escapeMrmacSingleQuotedLiteral(std::string_view value) {
	std::string escaped;

	escaped.reserve(value.size() + 4);
	for (const char ch : value) {
		escaped.push_back(ch);
		if (ch == '\'') escaped.push_back('\'');
	}
	return escaped;
}

void appendQuotedPayloadMember(std::string &payload, std::string_view key, std::string_view value) {
	if (!payload.empty()) payload.push_back(' ');
	payload += key;
	payload += "=\"";
	payload += escapePayloadQuotedString(value);
	payload.push_back('"');
}

void appendAtomPayloadMember(std::string &payload, std::string_view key, std::string_view value) {
	if (!payload.empty()) payload.push_back(' ');
	payload += key;
	payload.push_back('=');
	payload += value;
}

std::string serializeProfilePayload(const MRKeymapProfile &profile) {
	std::string payload;

	appendQuotedPayloadMember(payload, "name", profile.name);
	appendQuotedPayloadMember(payload, "description", profile.description);
	return payload;
}

std::string serializeBindingPayload(const MRKeymapBindingRecord &binding) {
	std::string payload;

	appendQuotedPayloadMember(payload, "name", binding.profileName);
	appendAtomPayloadMember(payload, "context", keymapContextName(binding.context));
	appendAtomPayloadMember(payload, "type", keymapBindingTypeName(binding.target.type));
	appendQuotedPayloadMember(payload, "target", binding.target.target);
	appendQuotedPayloadMember(payload, "sequence", binding.sequence.toString());
	appendQuotedPayloadMember(payload, "description", binding.description);
	return payload;
}

std::string serializeMrsetupRecord(std::string_view key, std::string_view payload) {
	return "MRSETUP('" + std::string(key) + "', '" + escapeMrmacSingleQuotedLiteral(payload) + "');\n";
}

std::vector<MRKeymapDiagnostic> parsePayloadMembers(std::string_view payload, std::vector<PayloadMember> &members, std::size_t profileIndex, std::size_t bindingIndex) {
	std::vector<MRKeymapDiagnostic> diagnostics;
	std::set<std::string> seenKeys;
	std::size_t pos = 0;

	while (pos < payload.size()) {
		while (pos < payload.size() && isPayloadSpace(payload[pos]))
			++pos;
		if (pos >= payload.size()) break;
		if (!isPayloadKeyStart(payload[pos])) {
			diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::PayloadSyntaxError, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Invalid payload syntax: expected member key at position " + std::to_string(pos) + "."));
			return diagnostics;
		}

		const std::size_t keyStart = pos;
		++pos;
		while (pos < payload.size() && isPayloadKeyChar(payload[pos]))
			++pos;
		std::string key = upperAscii(std::string(payload.substr(keyStart, pos - keyStart)));

		if (pos >= payload.size() || payload[pos] != '=') {
			diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::PayloadSyntaxError, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Invalid payload syntax: missing '=' after member '" + key + "'."));
			return diagnostics;
		}
		++pos;

		std::string value;
		if (pos < payload.size() && payload[pos] == '"') {
			++pos;
			while (pos < payload.size()) {
				const char ch = payload[pos++];
				if (ch == '"') break;
				if (ch == '\\') {
					if (pos >= payload.size()) {
						diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::PayloadSyntaxError, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Invalid payload syntax: dangling escape in member '" + key + "'."));
						return diagnostics;
					}
					switch (const char escaped = payload[pos++]) {
						case '"':
						case '\\':
							value.push_back(escaped);
							break;
						case 'n':
							value.push_back('\n');
							break;
						case 'r':
							value.push_back('\r');
							break;
						case 't':
							value.push_back('\t');
							break;
						default:
							diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::PayloadSyntaxError, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Invalid payload syntax: unsupported escape sequence in member '" + key + "'."));
							return diagnostics;
					}
					continue;
				}
				value.push_back(ch);
			}
			if (pos > payload.size() || payload[pos - 1] != '"') {
				diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::PayloadSyntaxError, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Invalid payload syntax: unterminated quoted value in member '" + key + "'."));
				return diagnostics;
			}
		} else {
			const std::size_t valueStart = pos;
			while (pos < payload.size() && !isPayloadSpace(payload[pos]))
				++pos;
			value.assign(payload.substr(valueStart, pos - valueStart));
			if (value.empty()) {
				diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::PayloadSyntaxError, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Invalid payload syntax: missing value for member '" + key + "'."));
				return diagnostics;
			}
		}

		if (!seenKeys.insert(key).second) diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::DuplicatePayloadMember, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Duplicate payload member '" + key + "' in " + describeBindingLocation(bindingIndex) + "."));

		members.push_back({std::move(key), std::move(value)});
	}

	return diagnostics;
}

const PayloadMember *findPayloadMember(const std::vector<PayloadMember> &members, std::string_view key) noexcept {
	for (const PayloadMember &member : members)
		if (member.key == key) return &member;
	return nullptr;
}

void requirePayloadMember(const std::vector<PayloadMember> &members, std::string_view key, std::vector<MRKeymapDiagnostic> &diagnostics, std::size_t profileIndex, std::size_t bindingIndex) {
	if (findPayloadMember(members, key) == nullptr) diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::MissingPayloadMember, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Missing payload member '" + std::string(key) + "'."));
}

void diagnoseUnknownPayloadMembers(const std::vector<PayloadMember> &members, std::span<const std::string_view> allowedKeys, std::vector<MRKeymapDiagnostic> &diagnostics, std::size_t profileIndex, std::size_t bindingIndex) {
	for (const PayloadMember &member : members) {
		bool known = false;
		for (const std::string_view allowed : allowedKeys)
			if (allowed == member.key) {
				known = true;
				break;
			}
		if (!known) diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::UnknownPayloadMember, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Unknown payload member '" + member.key + "'."));
	}
}
} // namespace

std::optional<MRKeymapBindingType> parseKeymapBindingType(std::string_view text) noexcept {
	const std::string upper = upperAscii(std::string(text));
	for (const TargetTypeSpec &entry : targetTypes)
		if (entry.name == upper) return entry.type;
	return std::nullopt;
}

std::string_view keymapBindingTypeName(MRKeymapBindingType type) noexcept {
	for (const TargetTypeSpec &entry : targetTypes)
		if (entry.type == type) return entry.name;
	return "ACTION";
}

std::vector<MRKeymapDiagnostic> validateKeymapProfile(const MRKeymapProfile &profile, std::size_t profileIndex) {
	std::vector<MRKeymapDiagnostic> diagnostics;

	if (profile.name.empty()) diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::UnknownProfile, MRKeymapDiagnosticSeverity::Error, profileIndex, kNoIndex, "Profile name must not be empty."));

	for (std::size_t i = 0; i < profile.bindings.size(); ++i) {
		const MRKeymapBindingRecord &binding = profile.bindings[i];
		if (binding.profileName != profile.name) diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::ProfileNameMismatch, MRKeymapDiagnosticSeverity::Error, profileIndex, i, "Binding profile name does not match owning profile."));
		if (binding.context == MRKeymapContext::None) diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::UnknownContext, MRKeymapDiagnosticSeverity::Error, profileIndex, i, "Binding context is unknown."));
		if (binding.sequence.empty()) diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::InvalidSequence, MRKeymapDiagnosticSeverity::Error, profileIndex, i, "Binding sequence must not be empty."));
		else if (sequenceHasPhase1CtrlAsciiCollision(binding.sequence))
			diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::Phase1CtrlAsciiCollision, MRKeymapDiagnosticSeverity::Error, profileIndex, i, "Binding sequence uses a Ctrl ASCII collision that is reserved in phase 1."));
		switch (binding.target.type) {
			case MRKeymapBindingType::Action:
				if (binding.target.target.empty() || !MRKeymapActionCatalog::contains(binding.target.target)) diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::UnknownAction, MRKeymapDiagnosticSeverity::Error, profileIndex, i, "Binding action target is unknown."));
				break;
			case MRKeymapBindingType::Macro:
				if (binding.target.target.empty()) diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::InvalidMacroTarget, MRKeymapDiagnosticSeverity::Warning, profileIndex, i, "Binding macro target must not be empty."));
				break;
		}

		for (std::size_t j = i + 1; j < profile.bindings.size(); ++j) {
			const MRKeymapBindingRecord &other = profile.bindings[j];
			if (sameBindingIdentity(binding, other)) diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::DuplicateBinding, MRKeymapDiagnosticSeverity::Warning, profileIndex, j, "Binding is duplicated in the same profile."));
			else if (sameDispatchSlot(binding, other))
				diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::ConflictingBinding, MRKeymapDiagnosticSeverity::Error, profileIndex, j, "colliding binding " + binding.sequence.toString() + " for " + displayBindingTarget(binding.target.target) + " & " + displayBindingTarget(other.target.target)));
		}
	}

	return diagnostics;
}

std::vector<MRKeymapDiagnostic> validateKeymapProfiles(std::span<const MRKeymapProfile> profiles) {
	std::vector<MRKeymapDiagnostic> diagnostics;

	for (std::size_t i = 0; i < profiles.size(); ++i) {
		auto profileDiagnostics = validateKeymapProfile(profiles[i], i);
		diagnostics.insert(diagnostics.end(), profileDiagnostics.begin(), profileDiagnostics.end());
		for (std::size_t j = i + 1; j < profiles.size(); ++j)
			if (profiles[i].name == profiles[j].name) diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::DuplicateProfile, MRKeymapDiagnosticSeverity::Error, j, kNoIndex, "Profile name is defined more than once."));
	}

	return diagnostics;
}

MRKeymapCanonicalizationResult canonicalizeKeymapProfiles(std::span<const MRKeymapProfile> profiles, std::string_view activeProfileName, MRKeymapCanonicalizationMode mode) {
	MRKeymapCanonicalizationResult result;
	std::map<std::string, MacroBindingFileValidation> macroValidationCache;

	result.activeProfileName = trimAscii(std::string(activeProfileName));
	for (const MRKeymapProfile &sourceProfile : profiles) {
		MRKeymapProfile normalizedProfile = sourceProfile;

		normalizeKeymapProfile(normalizedProfile);
		if (mode == MRKeymapCanonicalizationMode::UntrustedIngress) {
			if (normalizedProfile.name.empty()) {
				result.diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::UnknownProfile, MRKeymapDiagnosticSeverity::Error, result.profiles.size(), kNoIndex, "Profile name must not be empty."));
				continue;
			}
			const std::size_t duplicateIndex = findProfileIndex(result.profiles, normalizedProfile.name);
			if (duplicateIndex != kNoIndex) {
				result.diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::DuplicateProfile, MRKeymapDiagnosticSeverity::Error, result.profiles.size(), kNoIndex, "Profile name is defined more than once."));
				continue;
			}
		}
		result.profiles.push_back(std::move(normalizedProfile));
	}

	if (findProfileIndex(result.profiles, "DEFAULT") == kNoIndex) result.profiles.insert(result.profiles.begin(), builtInDefaultKeymapProfile());

	for (std::size_t profileIndex = 0; profileIndex < result.profiles.size(); ++profileIndex) {
		MRKeymapProfile &profile = result.profiles[profileIndex];
		std::vector<MRKeymapBindingRecord> cleanedBindings;

		for (std::size_t bindingIndex = 0; bindingIndex < profile.bindings.size(); ++bindingIndex) {
			const MRKeymapBindingRecord &binding = profile.bindings[bindingIndex];
			std::string macroError;
			bool dropBinding = false;

			if (mode == MRKeymapCanonicalizationMode::UntrustedIngress) {
				if (binding.profileName != profile.name) {
					result.diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::ProfileNameMismatch, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Binding profile name does not match owning profile."));
					continue;
				}
				if (binding.context == MRKeymapContext::None) {
					result.diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::UnknownContext, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Binding context is unknown."));
					continue;
				}
				if (binding.sequence.empty()) {
					result.diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::InvalidSequence, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Binding sequence must not be empty."));
					continue;
				}
				if (sequenceHasPhase1CtrlAsciiCollision(binding.sequence)) {
					result.diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::Phase1CtrlAsciiCollision, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Binding sequence uses a Ctrl ASCII collision that is reserved in phase 1."));
					continue;
				}
				if (binding.target.type == MRKeymapBindingType::Action && !MRKeymapActionCatalog::contains(binding.target.target)) {
					result.diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::UnknownAction, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Binding action target is unknown."));
					continue;
				}
			}
			if (bindingHasMacroError(binding, macroError, macroValidationCache)) {
				result.diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::InvalidMacroTarget, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, macroError));
				continue;
			}

			for (MRKeymapBindingRecord &existing : cleanedBindings)
				if (sameBindingIdentity(existing, binding)) {
					existing = binding;
					dropBinding = true;
					break;
				}
			if (dropBinding) continue;

			for (const MRKeymapBindingRecord &existing : cleanedBindings) {
				if (sameDispatchSlot(existing, binding)) {
					result.diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::ConflictingBinding, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "colliding binding " + binding.sequence.toString() + " for " + displayBindingTarget(existing.target.target) + " & " + displayBindingTarget(binding.target.target)));
					dropBinding = true;
					break;
				}
				if (hasSequencePrefixCollision(existing, binding)) {
					result.diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::TerminalPrefixConflict, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "terminal/prefix collision between '" + existing.sequence.toString() + "' and '" + binding.sequence.toString() + "'."));
					dropBinding = true;
					break;
				}
			}
			if (!dropBinding) cleanedBindings.push_back(binding);
		}
		profile.bindings = std::move(cleanedBindings);
	}

	if (result.activeProfileName.empty() || findProfileIndex(result.profiles, result.activeProfileName) == kNoIndex) {
		if (findProfileIndex(result.profiles, "DEFAULT") != kNoIndex) result.activeProfileName = "DEFAULT";
		else if (!result.profiles.empty())
			result.activeProfileName = result.profiles.front().name;
		else
			result.activeProfileName = "DEFAULT";
	}
	return result;
}

std::vector<MRKeymapDiagnostic> parseKeymapProfilePayload(std::string_view payload, MRKeymapProfile &profile, std::size_t profileIndex) {
	static constexpr std::array allowedKeys{kProfileNameMember, kProfileDescriptionMember};
	std::vector<MRKeymapDiagnostic> diagnostics;
	std::vector<PayloadMember> members;
	auto payloadDiagnostics = parsePayloadMembers(payload, members, profileIndex, kNoIndex);

	diagnostics.insert(diagnostics.end(), payloadDiagnostics.begin(), payloadDiagnostics.end());
	diagnoseUnknownPayloadMembers(members, allowedKeys, diagnostics, profileIndex, kNoIndex);
	requirePayloadMember(members, kProfileNameMember, diagnostics, profileIndex, kNoIndex);

	profile = {};
	if (const PayloadMember *name = findPayloadMember(members, kProfileNameMember)) profile.name = trimAscii(name->value);
	if (const PayloadMember *description = findPayloadMember(members, kProfileDescriptionMember)) profile.description = description->value;

	return diagnostics;
}

std::vector<MRKeymapDiagnostic> parseKeymapBindingPayload(std::string_view payload, MRKeymapBindingRecord &binding, std::size_t profileIndex, std::size_t bindingIndex) {
	static constexpr std::array allowedKeys{
	    kProfileNameMember, kBindingContextMember, kBindingTypeMember, kBindingTargetMember, kBindingSequenceMember, kProfileDescriptionMember,
	};
	std::vector<MRKeymapDiagnostic> diagnostics;
	std::vector<PayloadMember> members;
	auto payloadDiagnostics = parsePayloadMembers(payload, members, profileIndex, bindingIndex);

	diagnostics.insert(diagnostics.end(), payloadDiagnostics.begin(), payloadDiagnostics.end());
	diagnoseUnknownPayloadMembers(members, allowedKeys, diagnostics, profileIndex, bindingIndex);
	requirePayloadMember(members, kProfileNameMember, diagnostics, profileIndex, bindingIndex);
	requirePayloadMember(members, kBindingContextMember, diagnostics, profileIndex, bindingIndex);
	requirePayloadMember(members, kBindingTypeMember, diagnostics, profileIndex, bindingIndex);
	requirePayloadMember(members, kBindingTargetMember, diagnostics, profileIndex, bindingIndex);
	requirePayloadMember(members, kBindingSequenceMember, diagnostics, profileIndex, bindingIndex);

	binding = {};
	if (const PayloadMember *name = findPayloadMember(members, kProfileNameMember)) binding.profileName = trimAscii(name->value);
	if (const PayloadMember *description = findPayloadMember(members, kProfileDescriptionMember)) binding.description = description->value;
	if (const PayloadMember *context = findPayloadMember(members, kBindingContextMember)) {
		if (const auto parsedContext = parseKeymapContext(context->value)) binding.context = *parsedContext;
		else
			diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::UnknownContext, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Binding context '" + context->value + "' is unknown."));
	}
	if (const PayloadMember *type = findPayloadMember(members, kBindingTypeMember)) {
		if (const auto parsedType = parseKeymapBindingType(type->value)) binding.target.type = *parsedType;
		else
			diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::InvalidBindingType, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Binding type '" + type->value + "' is unknown."));
	}
	if (const PayloadMember *target = findPayloadMember(members, kBindingTargetMember)) binding.target.target = target->value;
	if (const PayloadMember *sequence = findPayloadMember(members, kBindingSequenceMember)) {
		if (const auto parsedSequence = MRKeymapSequence::parse(sequence->value)) binding.sequence = *parsedSequence;
		else
			diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::InvalidSequence, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Binding sequence '" + sequence->value + "' is invalid."));
	}
	if (!binding.sequence.empty() && sequenceHasPhase1CtrlAsciiCollision(binding.sequence)) diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::Phase1CtrlAsciiCollision, MRKeymapDiagnosticSeverity::Error, profileIndex, bindingIndex, "Binding sequence uses a Ctrl ASCII collision that is reserved in phase 1."));

	return diagnostics;
}

MRKeymapLoadResult loadKeymapProfilesFromSettingsSource(std::string_view source) {
	static const std::regex assignmentPattern(kAssignmentPattern, std::regex::icase);
	struct ParsedBindingPayload {
		MRKeymapBindingRecord binding;
		std::vector<MRKeymapDiagnostic> diagnostics;
	};
	MRKeymapLoadResult result;
	std::vector<ParsedBindingPayload> pendingBindings;
	std::smatch match;
	std::string remaining(source);

	while (std::regex_search(remaining, match, assignmentPattern)) {
		const std::string key = upperAscii(trimAscii(match[1].str()));
		const std::string payload = unescapeMrmacSingleQuotedLiteral(match[2].str());

		if (key == kActiveKeymapProfileSetupKey) {
			MRKeymapProfile activeRecord;
			auto diagnostics = parseKeymapProfilePayload(payload, activeRecord, kNoIndex);
			for (MRKeymapDiagnostic &diagnostic : diagnostics) {
				diagnostic.profileIndex = kNoIndex;
				diagnostic.bindingIndex = kNoIndex;
				result.diagnostics.push_back(std::move(diagnostic));
			}
			result.activeProfileName = activeRecord.name;
		} else if (key == kKeymapProfileSetupKey) {
			MRKeymapProfile profile;
			auto diagnostics = parseKeymapProfilePayload(payload, profile, result.profiles.size());
			result.diagnostics.insert(result.diagnostics.end(), diagnostics.begin(), diagnostics.end());
			result.profiles.push_back(std::move(profile));
		} else if (key == kKeymapBindingSetupKey) {
			ParsedBindingPayload parsedBinding;
			parsedBinding.diagnostics = parseKeymapBindingPayload(payload, parsedBinding.binding, kNoIndex, kNoIndex);
			pendingBindings.push_back(std::move(parsedBinding));
		}

		remaining = match.suffix().str();
	}

	for (ParsedBindingPayload &parsedBinding : pendingBindings) {
		std::size_t ownerIndex = kNoIndex;
		for (std::size_t i = 0; i < result.profiles.size(); ++i)
			if (result.profiles[i].name == parsedBinding.binding.profileName) {
				ownerIndex = i;
				break;
			}

		if (ownerIndex == kNoIndex) {
			result.diagnostics.insert(result.diagnostics.end(), parsedBinding.diagnostics.begin(), parsedBinding.diagnostics.end());
			result.diagnostics.push_back(makeDiagnostic(MRKeymapDiagnosticKind::UnknownProfile, MRKeymapDiagnosticSeverity::Error, kNoIndex, kNoIndex, "Binding references unknown profile '" + parsedBinding.binding.profileName + "'."));
			continue;
		}

		const std::size_t bindingIndex = result.profiles[ownerIndex].bindings.size();
		for (MRKeymapDiagnostic &diagnostic : parsedBinding.diagnostics) {
			diagnostic.profileIndex = ownerIndex;
			diagnostic.bindingIndex = bindingIndex;
			result.diagnostics.push_back(std::move(diagnostic));
		}
		result.profiles[ownerIndex].bindings.push_back(std::move(parsedBinding.binding));
	}

	if (result.activeProfileName.empty()) {
		for (const MRKeymapProfile &profile : result.profiles)
			if (upperAscii(profile.name) == "DEFAULT") {
				result.activeProfileName = profile.name;
				break;
			}
		if (result.activeProfileName.empty() && !result.profiles.empty()) result.activeProfileName = result.profiles.front().name;
	}
	{
		MRKeymapCanonicalizationResult canonicalized = canonicalizeKeymapProfiles(result.profiles, result.activeProfileName, MRKeymapCanonicalizationMode::UntrustedIngress);
		result.activeProfileName = std::move(canonicalized.activeProfileName);
		result.profiles = std::move(canonicalized.profiles);
		result.diagnostics.insert(result.diagnostics.end(), canonicalized.diagnostics.begin(), canonicalized.diagnostics.end());
	}
	return result;
}

std::string serializeKeymapProfilesToSettingsSource(std::span<const MRKeymapProfile> profiles, std::string_view activeProfileName) {
	std::string source;

	for (const MRKeymapProfile &profile : profiles)
		source += serializeMrsetupRecord(kKeymapProfileSetupKey, serializeProfilePayload(profile));
	for (const MRKeymapProfile &profile : profiles)
		for (const MRKeymapBindingRecord &binding : profile.bindings)
			source += serializeMrsetupRecord(kKeymapBindingSetupKey, serializeBindingPayload(binding));
	source += serializeMrsetupRecord(kActiveKeymapProfileSetupKey, "name=\"" + escapePayloadQuotedString(activeProfileName) + "\"");
	return source;
}

MRKeymapProfile builtInDefaultKeymapProfile() {
	return {"DEFAULT", "build-in defaults", {}};
}
