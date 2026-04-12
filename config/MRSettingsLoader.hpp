#ifndef MRSETTINGSLOADER_HPP
#define MRSETTINGSLOADER_HPP

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

bool loadAndNormalizeSettingsSource(const std::string &settingsPath, const std::string &source,
                                    MRSettingsLoadReport *report = nullptr,
                                    std::string *errorMessage = nullptr);
[[nodiscard]] std::string describeSettingsLoadReport(const MRSettingsLoadReport &report);
bool diffSettingsSources(const std::string &beforeSource, const std::string &afterSource,
                         std::vector<MRSettingsChangeEntry> &changes,
                         std::string *errorMessage = nullptr);
[[nodiscard]] std::string formatSettingsChangeForLog(const MRSettingsChangeEntry &change);

#endif
