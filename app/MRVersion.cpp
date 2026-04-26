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
}

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
