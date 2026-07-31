#ifndef MRSETTINGSHISTORY_HPP
#define MRSETTINGSHISTORY_HPP

#include "MRSettingsRuntime.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct MRDialogHistoryEntry {
	std::string value;
	long long epoch = 0;

	auto operator==(const MRDialogHistoryEntry &) const noexcept -> bool = default;
};

struct MRScopedDialogHistoryState {
	std::string lastPath;
	std::vector<MRDialogHistoryEntry> pathHistory;
	std::vector<MRDialogHistoryEntry> fileHistory;

	auto operator==(const MRScopedDialogHistoryState &) const noexcept -> bool = default;
};

struct MRDialogHistoryScopeSpec {
	MRDialogHistoryScope scope;
	const char *name;
};

constexpr int kHistoryLimitMin = 5;
constexpr int kHistoryLimitMax = 50;
constexpr int kHistoryLimitDefault = 15;

extern const std::array<MRDialogHistoryScopeSpec, static_cast<std::size_t>(MRDialogHistoryScope::Count)> kDialogHistoryScopeSpecs;

std::array<MRScopedDialogHistoryState, static_cast<std::size_t>(MRDialogHistoryScope::Count)> configuredDialogHistoryStorage();
void storeConfiguredDialogHistoryStorage(const std::array<MRScopedDialogHistoryState, static_cast<std::size_t>(MRDialogHistoryScope::Count)> &states);
std::size_t dialogHistoryScopeIndex(MRDialogHistoryScope scope) noexcept;
MRScopedDialogHistoryState dialogHistoryState(MRDialogHistoryScope scope);
void storeDialogHistoryState(MRDialogHistoryScope scope, const MRScopedDialogHistoryState &state);
const MRDialogHistoryScopeSpec *findDialogHistoryScopeSpec(MRDialogHistoryScope scope) noexcept;
const MRDialogHistoryScopeSpec *findDialogHistoryScopeSpecByName(std::string_view name) noexcept;
const char *dialogHistoryScopeName(MRDialogHistoryScope scope) noexcept;
std::vector<MRDialogHistoryEntry> configuredMultiFilespecHistoryStorage();
void storeConfiguredMultiFilespecHistoryStorage(const std::vector<MRDialogHistoryEntry> &entries);
std::vector<MRDialogHistoryEntry> configuredMultiPathHistoryStorage();
void storeConfiguredMultiPathHistoryStorage(const std::vector<MRDialogHistoryEntry> &entries);
int configuredPathHistoryLimit();
void storeConfiguredPathHistoryLimit(int value);
int configuredFileHistoryLimit();
void storeConfiguredFileHistoryLimit(int value);
int configuredWorkspaceHistoryLimit();
void storeConfiguredWorkspaceHistoryLimit(int value);
int configuredFileHistoryLimitForScope(MRDialogHistoryScope scope);
long long configuredHistoryEpochCounter();
void storeConfiguredHistoryEpochCounter(long long value);
long long nextHistoryEpoch();
void trimHistoryToLimit(std::vector<MRDialogHistoryEntry> &entries, int limit);
void addHistoryEntry(std::vector<MRDialogHistoryEntry> &entries, const std::string &value, int limit);
void addSerializedHistoryEntry(std::vector<MRDialogHistoryEntry> &entries, const std::string &value, int limit, bool normalizeAsPath);
std::string latestReadableHistoryPath(const std::vector<MRDialogHistoryEntry> &entries);
std::string latestReadableHistoryFileDirectory(const std::vector<MRDialogHistoryEntry> &entries);
std::string latestHistoryValue(const std::vector<MRDialogHistoryEntry> &entries);
std::string effectiveRememberedLoadDirectory(MRDialogHistoryScope scope);
bool parseHistoryLimitLiteral(const std::string &value, int &outValue, std::string *errorMessage, const char *keyName);
bool setConfiguredPathHistoryLimitValue(int value, std::string *errorMessage);
bool setConfiguredFileHistoryLimitValue(int value, std::string *errorMessage);
bool setConfiguredWorkspaceHistoryLimitValue(int value, std::string *errorMessage);
[[nodiscard]] int configuredMaxPathHistory();
[[nodiscard]] int configuredMaxFileHistory();
[[nodiscard]] int configuredMaxWorkspaceHistory();
void configuredPathHistoryEntries(std::vector<std::string> &outValues);
void configuredFileHistoryEntries(std::vector<std::string> &outValues);
void configuredMultiFilespecHistoryEntries(std::vector<std::string> &outValues);
void configuredMultiPathHistoryEntries(std::vector<std::string> &outValues);
bool addConfiguredMultiFilespecHistoryEntry(const std::string &value, std::string *errorMessage);
bool addConfiguredMultiPathHistoryEntry(const std::string &value, std::string *errorMessage);
bool setScopedDialogLastPath(MRDialogHistoryScope scope, const std::string &path, std::string *errorMessage);
void initRememberedLoadDialogPath(MRDialogHistoryScope scope, char *buffer, std::size_t bufferSize, const char *pattern);
void rememberLoadDialogPath(MRDialogHistoryScope scope, const char *path);
void forgetLoadDialogPath(MRDialogHistoryScope scope, const char *path);
std::string configuredLastFileDialogFilePath(MRDialogHistoryScope scope);
std::string configuredLastFileDialogPath(MRDialogHistoryScope scope);
void configuredScopedDialogFileHistoryEntries(MRDialogHistoryScope scope, std::vector<std::string> &outValues);
void configuredScopedDialogPathHistoryEntries(MRDialogHistoryScope scope, std::vector<std::string> &outValues);
bool setConfiguredLastFileDialogPath(const std::string &path, std::string *errorMessage);
std::string configuredLastFileDialogPath();

#endif
