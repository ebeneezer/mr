#ifndef MRKEYMAPPROFILE_HPP
#define MRKEYMAPPROFILE_HPP

#include "MRKeymapContext.hpp"
#include "MRKeymapSequence.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

enum class MRKeymapBindingType : unsigned char {
	Action,
	Macro
};

[[nodiscard]] std::optional<MRKeymapBindingType> parseKeymapBindingType(std::string_view text) noexcept;
[[nodiscard]] std::string_view keymapBindingTypeName(MRKeymapBindingType type) noexcept;

struct MRKeymapBindingTarget {
	MRKeymapBindingType type{MRKeymapBindingType::Action};
	std::string target;

	bool operator==(const MRKeymapBindingTarget &other) const noexcept {
		return type == other.type && target == other.target;
	}
};

struct MRKeymapBindingRecord {
	std::string profileName;
	MRKeymapContext context{MRKeymapContext::None};
	MRKeymapBindingTarget target;
	MRKeymapSequence sequence;
	std::string description;

	bool operator==(const MRKeymapBindingRecord &other) const noexcept {
		return profileName == other.profileName && context == other.context && target == other.target && sequence == other.sequence && description == other.description;
	}
};

struct MRKeymapProfile {
	std::string name;
	std::string description;
	std::vector<MRKeymapBindingRecord> bindings;

	bool operator==(const MRKeymapProfile &other) const noexcept {
		return name == other.name && description == other.description && bindings == other.bindings;
	}
};

enum class MRKeymapDiagnosticKind : unsigned char {
	UnknownProfile,
	DuplicateProfile,
	DuplicateBinding,
	ConflictingBinding,
	PayloadSyntaxError,
	DuplicatePayloadMember,
	MissingPayloadMember,
	UnknownPayloadMember,
	UnknownContext,
	InvalidSequence,
	InvalidBindingType,
	UnknownAction,
	InvalidMacroTarget,
	Phase1CtrlAsciiCollision,
	PrefixConflict,
	TerminalPrefixConflict,
	ProfileNameMismatch
};

enum class MRKeymapDiagnosticSeverity : unsigned char {
	Warning,
	Error
};

struct MRKeymapDiagnostic {
	MRKeymapDiagnosticKind kind{MRKeymapDiagnosticKind::InvalidSequence};
	MRKeymapDiagnosticSeverity severity{MRKeymapDiagnosticSeverity::Error};
	std::size_t profileIndex{static_cast<std::size_t>(-1)};
	std::size_t bindingIndex{static_cast<std::size_t>(-1)};
	std::string message;
};

struct MRKeymapLoadResult {
	std::string activeProfileName;
	std::vector<MRKeymapProfile> profiles;
	std::vector<MRKeymapDiagnostic> diagnostics;
};

enum class MRKeymapCanonicalizationMode : unsigned char {
	UntrustedIngress,
	TrustedCommit
};

struct MRKeymapCanonicalizationResult {
	std::string activeProfileName;
	std::vector<MRKeymapProfile> profiles;
	std::vector<MRKeymapDiagnostic> diagnostics;
};

[[nodiscard]] std::vector<MRKeymapDiagnostic> validateKeymapProfile(const MRKeymapProfile &profile, std::size_t profileIndex = static_cast<std::size_t>(-1));
[[nodiscard]] std::vector<MRKeymapDiagnostic> validateKeymapProfiles(std::span<const MRKeymapProfile> profiles);
[[nodiscard]] std::vector<MRKeymapDiagnostic> parseKeymapProfilePayload(std::string_view payload, MRKeymapProfile &profile, std::size_t profileIndex = static_cast<std::size_t>(-1));
[[nodiscard]] std::vector<MRKeymapDiagnostic> parseKeymapBindingPayload(std::string_view payload, MRKeymapBindingRecord &binding, std::size_t profileIndex = static_cast<std::size_t>(-1), std::size_t bindingIndex = static_cast<std::size_t>(-1));
[[nodiscard]] MRKeymapCanonicalizationResult canonicalizeKeymapProfiles(std::span<const MRKeymapProfile> profiles, std::string_view activeProfileName, MRKeymapCanonicalizationMode mode);
[[nodiscard]] MRKeymapLoadResult loadKeymapProfilesFromSettingsSource(std::string_view source);
[[nodiscard]] std::string serializeKeymapProfilesToSettingsSource(std::span<const MRKeymapProfile> profiles, std::string_view activeProfileName);
[[nodiscard]] std::string buildExecutableKeymapMacroSource(std::span<const MRKeymapProfile> profiles, std::string_view activeProfileName);

#endif
