#ifndef MRSETTINGSSTORAGE_HPP
#define MRSETTINGSSTORAGE_HPP

#include "MRSettingsRuntime.hpp"

#include <cstddef>
#include <string>
#include <vector>

struct MRSettingsLoadReport {
	enum Flag : unsigned int {
		None = 0,
		UnknownKeyDropped = 1u << 0,
		DuplicateKeySeen = 1u << 1,
		InvalidValueReset = 1u << 2,
		MissingCanonicalKeyDefaulted = 1u << 3,
		LegacyInlineColorsSeen = 1u << 4,
		ThemeFallbackUsed = 1u << 5,
		AnchoredSettingsPath = 1u << 6,
		ObsoleteFeProfileDropped = 1u << 7,
		VersionUpgradeRequired = 1u << 8,
	};

	unsigned int flags = None;
	std::size_t appliedAssignmentCount = 0;
	std::size_t ignoredAssignmentCount = 0;
	std::size_t duplicateAssignmentCount = 0;
	std::size_t defaultedCanonicalKeyCount = 0;

	[[nodiscard]] bool normalized() const noexcept {
		return flags != None;
	}
};

struct MRSettingsChangeEntry {
	enum class Kind {
		Added,
		Removed,
		Changed,
	};

	Kind kind = Kind::Changed;
	std::string scope;
	std::string key;
	std::string oldValue;
	std::string newValue;
};

bool buildCanonicalSettingsSource(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource, std::string *errorMessage = nullptr);
bool prepareStartupSettingsSource(const std::string &settingsPath, const std::string &source, MRSettingsLoadReport *report, std::string &canonicalSource, std::string *errorMessage = nullptr);
[[nodiscard]] std::string describeSettingsLoadReport(const MRSettingsLoadReport &report);
bool diffSettingsSources(const std::string &beforeSource, const std::string &afterSource, std::vector<MRSettingsChangeEntry> &changes, std::string *errorMessage = nullptr);
[[nodiscard]] std::string formatSettingsChangeForLog(const MRSettingsChangeEntry &change);

struct MRSettingsWriteReport {
	std::string settingsPath;
	bool fileWritten = false;
	bool contentChanged = false;
	std::size_t addedCount = 0;
	std::size_t removedCount = 0;
	std::size_t changedCount = 0;
	std::vector<std::string> logLines;
};

[[nodiscard]] std::string buildSettingsMacroSource(const MRSetupPaths &paths);
[[nodiscard]] bool configuredSettingsDirty();
void clearConfiguredSettingsDirty();
bool persistConfiguredSettingsSnapshot(std::string *errorMessage = nullptr, MRSettingsWriteReport *report = nullptr);
bool writeSettingsMacroFile(const MRSetupPaths &paths, std::string *errorMessage = nullptr, MRSettingsWriteReport *report = nullptr);
bool ensureSettingsMacroFileExists(const std::string &settingsMacroUri, std::string *errorMessage = nullptr);

#endif
