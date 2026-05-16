#include "MRVersion.hpp"

#include <cstdint>
#include <string>

#ifndef MR_BUILD_EPOCH
#define MR_BUILD_EPOCH 0ULL
#endif

namespace {
constexpr char kMrDisplayVersion[] = "0.2.0-dev";
constexpr char kBuildLabel[] = " (build ";
constexpr char kBuildSuffix[] = ")";
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
