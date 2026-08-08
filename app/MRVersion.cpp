#include "MRVersion.hpp"

#include <cstdint>
#include <string>

#ifndef MR_BUILD_EPOCH
#define MR_BUILD_EPOCH 0ULL
#endif

namespace {
constexpr char kMrDisplayVersion[] = "0.2.1";
constexpr char kBuildLabel[] = " (build ";
constexpr char kBuildSuffix[] = ")";
constexpr char kSettingsVersionSetupKey[] = "SETTINGS_VERSION";
constexpr char kThemeVersionSetupKey[] = "THEME_VERSION";
constexpr char kKeymapVersionSetupKey[] = "KEYMAP_VERSION";
} // namespace

const char *mrDisplayVersion() noexcept {
	return kMrDisplayVersion;
}

std::string mrAboutDisplayVersion() {
	std::string version = kMrDisplayVersion;
	version += kBuildLabel;
	version += std::to_string(static_cast<std::uint64_t>(MR_BUILD_EPOCH));
	version += kBuildSuffix;
	return version;
}

std::uint64_t mrCurrentPersistenceVersion() noexcept {
	return static_cast<std::uint64_t>(MR_BUILD_EPOCH);
}

std::string mrCurrentPersistenceVersionString() {
	return std::to_string(mrCurrentPersistenceVersion());
}

bool mrParsePersistenceVersion(std::string_view text, std::uint64_t &outVersion) noexcept {
	std::uint64_t parsed = 0;

	if (text.empty()) return false;
	for (const char ch : text) {
		if (ch < '0' || ch > '9') return false;
		parsed = parsed * 10u + static_cast<std::uint64_t>(ch - '0');
	}
	outVersion = parsed;
	return true;
}

const char *mrSettingsVersionSetupKey() noexcept {
	return kSettingsVersionSetupKey;
}

const char *mrThemeVersionSetupKey() noexcept {
	return kThemeVersionSetupKey;
}

const char *mrKeymapVersionSetupKey() noexcept {
	return kKeymapVersionSetupKey;
}

std::string mrInvalidPersistenceVersionMessage(std::string_view artifact) {
	return "Invalid " + std::string(artifact) + " version.";
}

std::string mrFuturePersistenceVersionMessagePrefix(std::string_view artifact) {
	return std::string(artifact) + " targets newer build version: ";
}

std::string mrFuturePersistenceVersionMessage(std::string_view artifact, std::string_view versionLiteral) {
	return mrFuturePersistenceVersionMessagePrefix(artifact) + std::string(versionLiteral);
}

std::string mrUnsupportedCurrentBuildVersionMessage(std::string_view artifact) {
	return "Unsupported " + std::string(artifact) + " version for current build.";
}
