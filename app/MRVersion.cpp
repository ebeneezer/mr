#include "MRVersion.hpp"

namespace {
constexpr char kMrDisplayVersion[] = "0.2.0-dev";
}

const char *mrDisplayVersion() noexcept {
	return kMrDisplayVersion;
}
