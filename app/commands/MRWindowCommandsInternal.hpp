#ifndef MRWINDOWCOMMANDSINTERNAL_HPP
#define MRWINDOWCOMMANDSINTERNAL_HPP

#include "../../mrmac/vm/MRVMRuntimeKv.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace mr::window_commands {

constexpr const char *kWorkspaceBranch = "workspace";
constexpr const char *kVirtualDesktopsBranch = "virtualDesktops";

void logWindowTiming(const std::string &label, long long tookUs, const std::string &detail);
void postWindowCommandError(std::string_view text);
[[nodiscard]] int applicationUiInt(MRVMRuntimeKv &runtimeKv, const char *branch, const char *key, int fallback);
void storeApplicationUiInt(MRVMRuntimeKv &runtimeKv, const char *branch, const char *key, int value);
[[nodiscard]] std::uint64_t applicationUiUnsigned(MRVMRuntimeKv &runtimeKv, const char *branch, const char *key, std::uint64_t fallback);
void storeApplicationUiUnsigned(MRVMRuntimeKv &runtimeKv, const char *branch, const char *key, std::uint64_t value);
[[nodiscard]] std::string applicationUiString(MRVMRuntimeKv &runtimeKv, const char *branch, const char *key);
void storeApplicationUiString(MRVMRuntimeKv &runtimeKv, const char *branch, const char *key, const std::string &value);
[[nodiscard]] int normalizedVirtualDesktopCount(int count);
[[nodiscard]] std::uint64_t steadyClockMilliseconds(std::chrono::steady_clock::time_point value);

} // namespace mr::window_commands

#endif
