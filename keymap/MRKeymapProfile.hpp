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

enum class MRKeymapBindingType : unsigned char { Action, Macro };

[[nodiscard]] std::optional<MRKeymapBindingType> parseKeymapBindingType(std::string_view text) noexcept;
[[nodiscard]] std::string_view keymapBindingTypeName(MRKeymapBindingType type) noexcept;

struct MRKeymapBindingTarget {
	MRKeymapBindingType type{MRKeymapBindingType::Action};
	std::string target;

	auto operator==(const MRKeymapBindingTarget &) const noexcept -> bool = default;
};

struct MRKeymapBindingRecord {
	std::string profileName;
	MRKeymapContext context{MRKeymapContext::None};
	MRKeymapBindingTarget target;
	MRKeymapSequence sequence;
	std::string description;

	auto operator==(const MRKeymapBindingRecord &) const noexcept -> bool = default;
};

struct MRKeymapProfile {
	std::string name;
	std::string description;
	std::vector<MRKeymapBindingRecord> bindings;

	auto operator==(const MRKeymapProfile &) const noexcept -> bool = default;
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
	PrefixConflict,
	TerminalPrefixConflict,
	ProfileNameMismatch
};

enum class MRKeymapDiagnosticSeverity : unsigned char { Warning, Error };

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

[[nodiscard]] std::vector<MRKeymapDiagnostic> validateKeymapProfile(const MRKeymapProfile &profile, std::size_t profileIndex = static_cast<std::size_t>(-1));
[[nodiscard]] std::vector<MRKeymapDiagnostic> validateKeymapProfiles(std::span<const MRKeymapProfile> profiles);
[[nodiscard]] std::vector<MRKeymapDiagnostic> parseKeymapProfilePayload(std::string_view payload, MRKeymapProfile &profile, std::size_t profileIndex = static_cast<std::size_t>(-1));
[[nodiscard]] std::vector<MRKeymapDiagnostic> parseKeymapBindingPayload(std::string_view payload, MRKeymapBindingRecord &binding, std::size_t profileIndex = static_cast<std::size_t>(-1), std::size_t bindingIndex = static_cast<std::size_t>(-1));
[[nodiscard]] MRKeymapLoadResult loadKeymapProfilesFromSettingsSource(std::string_view source);
[[nodiscard]] std::string serializeKeymapProfilesToSettingsSource(std::span<const MRKeymapProfile> profiles, std::string_view activeProfileName);
[[nodiscard]] MRKeymapProfile builtInDefaultKeymapProfile();

#endif
