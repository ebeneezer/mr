#include "../../app/MRVersion.hpp"
#include "../../app/utils/MRFileIOUtils.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "MRSettingsColorInternal.hpp"
#include "MRSettingsHistory.hpp"
#include "MRSettingsRuntime.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "MRSettingsThemesProfiles.hpp"

#include <array>
#include <cerrno>
#include <map>
#include <regex>
#include <string>
#include <string_view>
#include <sys/stat.h>

namespace {

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

std::string fileNamePartOf(std::string_view path) {
	std::size_t sep = path.find_last_of("/\\");
	if (sep == std::string_view::npos) return std::string(path);
	return std::string(path.substr(sep + 1));
}

std::string appendFileName(std::string_view directory, const char *fileName) {
	std::string result(directory);

	if (!result.empty() && result.back() != '/' && result.back() != '\\') result.push_back('/');
	result += fileName;
	return result;
}

std::string builtInTempDirectoryPath() {
	std::string cwd;

	if (isWritableDirectory("/tmp")) return "/tmp";
	cwd = currentWorkingDirectory();
	if (!cwd.empty() && isWritableDirectory(cwd)) return cwd;
	return "/tmp";
}

std::string toUpperHexByte(unsigned char value) {
	static constexpr char digits[] = "0123456789ABCDEF";
	std::string text(2, '0');

	text[0] = digits[(value >> 4) & 0x0F];
	text[1] = digits[value & 0x0F];
	return text;
}

std::string escapeMrmacSingleQuotedLiteral(const std::string &value) {
	std::string out;

	out.reserve(value.size());
	for (char ch : value) {
		if (ch == '\'') out.push_back('\'');
		out.push_back(ch);
	}
	return out;
}

std::string unescapeMrmacSingleQuotedLiteral(const std::string &value) {
	std::string out;

	out.reserve(value.size());
	for (std::size_t i = 0; i < value.size(); ++i) {
		if (value[i] == '\'' && i + 1 < value.size() && value[i + 1] == '\'') {
			out.push_back('\'');
			++i;
		} else
			out.push_back(value[i]);
	}
	return out;
}

bool ensureDirectoryTree(const std::string &directoryPath, std::string *errorMessage) {
	struct stat st;
	std::string parentPath;

	if (directoryPath.empty() || directoryPath == "." || directoryPath == "/") {
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (::stat(directoryPath.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		}
		if (errorMessage != nullptr) *errorMessage = "Path exists and is not a path container: " + directoryPath;
		return false;
	}
	parentPath = directoryPartOf(directoryPath);
	if (!parentPath.empty() && parentPath != directoryPath)
		if (!ensureDirectoryTree(parentPath, errorMessage)) return false;
	if (::mkdir(directoryPath.c_str(), 0755) != 0 && errno != EEXIST) {
		if (errorMessage != nullptr) *errorMessage = "Unable to create path container: " + directoryPath;
		return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

template <std::size_t N> std::string formatColorListLiteral(const std::array<unsigned char, N> &values) {
	std::string out = "v1:";

	for (std::size_t i = 0; i < values.size(); ++i) {
		if (i != 0) out.push_back(',');
		out += toUpperHexByte(values[i]);
	}
	return out;
}

std::string formatWindowColorListLiteral(const std::array<unsigned char, MRColorSetupSettings::kWindowCount> &values) {
	std::string out = formatColorListLiteral(values);
	out[1] = '7';
	return out;
}

std::string formatFileCompareColorListLiteral(const std::array<unsigned char, MRColorSetupSettings::kFileCompareCount> &values) {
	std::string out = formatColorListLiteral(values);
	out[1] = '2';
	return out;
}

} // namespace

std::string defaultColorThemePathForSettings(std::string_view settingsPath) {
	std::string dir = directoryPartOf(makeAbsolutePath(std::string(settingsPath)));
	if (dir.empty()) dir = builtInTempDirectoryPath();
	return appendFileName(dir, "default-theme.mrmac");
}

bool parseThemeSetupAssignments(const std::string &source, std::map<std::string, std::string> &assignments, bool *upgradeRequired, std::string *errorMessage) {
	static const std::regex pattern("(?:MRSETUP\\s*\\(\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)|([A-Z_][A-Z0-9_]*)\\s*\\(\\s*'((?:''|[^'])*)'\\s*\\))", std::regex::icase);
	bool localUpgradeRequired = false;
	assignments.clear();

	auto begin = std::sregex_iterator(source.begin(), source.end(), pattern);
	auto end = std::sregex_iterator();

	for (auto it = begin; it != end; ++it) {
		const bool mrsetupRecord = (*it)[1].matched;
		std::string key = trimAscii(unescapeMrmacSingleQuotedLiteral(mrsetupRecord ? (*it)[1].str() : (*it)[3].str()));
		std::string value = unescapeMrmacSingleQuotedLiteral(mrsetupRecord ? (*it)[2].str() : (*it)[4].str());
		if (key.empty()) continue;
		assignments[upperAscii(key)] = value;
	}
	{
		const auto versionIt = assignments.find(std::string(mrThemeVersionSetupKey()));
		const std::uint64_t currentVersion = mrCurrentPersistenceVersion();

		if (versionIt == assignments.end()) localUpgradeRequired = true;
		else {
			const std::string versionLiteral = trimAscii(versionIt->second);
			std::uint64_t parsedVersion = 0;

			if (!mrParsePersistenceVersion(versionLiteral, parsedVersion)) return setError(errorMessage, mrInvalidPersistenceVersionMessage("theme file"));
			if (parsedVersion > currentVersion) return setError(errorMessage, mrFuturePersistenceVersionMessage("Theme file", versionLiteral));
			if (parsedVersion < currentVersion) localUpgradeRequired = true;
		}
	}

	MRColorSetupSettings defaults = resolveColorSetupDefaults();
	if (!assignments.contains("WINDOWCOLORS")) assignments["WINDOWCOLORS"] = formatWindowColorListLiteral(defaults.windowColors);
	if (!assignments.contains("MENUDIALOGCOLORS")) assignments["MENUDIALOGCOLORS"] = formatColorListLiteral(defaults.menuDialogColors);
	if (!assignments.contains("HELPCOLORS")) assignments["HELPCOLORS"] = formatColorListLiteral(defaults.helpColors);
	if (!assignments.contains("OTHERCOLORS")) assignments["OTHERCOLORS"] = formatColorListLiteral(defaults.otherColors);
	if (!assignments.contains("MINIMAPCOLORS")) assignments["MINIMAPCOLORS"] = formatColorListLiteral(defaults.miniMapColors);
	if (!assignments.contains("FILECOMPAREMINIMAPCOLORS")) assignments["FILECOMPAREMINIMAPCOLORS"] = formatColorListLiteral(defaults.fileCompareMiniMapColors);
	if (!assignments.contains("CODECOLORS")) assignments["CODECOLORS"] = formatColorListLiteral(defaults.codeColors);
	if (!assignments.contains("FILECOMPARECOLORS")) assignments["FILECOMPARECOLORS"] = formatFileCompareColorListLiteral(defaults.fileCompareColors);
	if (!assignments.contains("DEBUGGERCOLORS")) assignments["DEBUGGERCOLORS"] = formatColorListLiteral(defaults.debuggerColors);
	if (upgradeRequired != nullptr) *upgradeRequired = localUpgradeRequired;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string defaultColorThemeFilePath() {
	return defaultColorThemePathForSettings(configuredSettingsMacroFilePath());
}

bool validateColorThemeFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	struct stat st;

	if (normalized.empty()) return setError(errorMessage, "Empty color theme URI.");
	if (::stat(normalized.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return setError(errorMessage, "Color theme URI must include a filename.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setConfiguredColorThemeFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	std::string configured;

	if (!validateColorThemeFilePath(path, errorMessage)) return false;
	configured = makeAbsolutePath(normalized);
	storeConfiguredColorThemeFile(configured);
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::SetupThemeLoad, configured, nullptr));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredColorThemeFilePath() {
	const std::string configured = configuredColorThemeFile();

	recordSettingsRuntimeRead();
	if (!configured.empty()) return makeAbsolutePath(configured);
	return defaultColorThemeFilePath();
}

std::string configuredColorThemeDisplayName() {
	std::string name = trimAscii(configuredColorThemeDisplayNameValue());

	recordSettingsRuntimeRead();
	if (name.empty()) return std::string("default");
	return name;
}

bool setConfiguredColorThemeDisplayName(const std::string &name, std::string *errorMessage) {
	storeConfiguredColorThemeDisplayNameValue(trimAscii(name));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string buildColorThemeMacroSource(const MRColorSetupSettings &colors) {
	std::string source;
	const std::string themeName = trimAscii(configuredColorThemeDisplayNameValue());

	source += "$MACRO MR_COLOR_THEME FROM EDIT;\n";
	source += "THEME_RESET();\n";
	source += "THEME_VERSION('" + escapeMrmacSingleQuotedLiteral(mrCurrentPersistenceVersionString()) + "');\n";
	source += "WINDOWCOLORS('" + escapeMrmacSingleQuotedLiteral(formatWindowColorListLiteral(colors.windowColors)) + "');\n";
	source += "MENUDIALOGCOLORS('" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.menuDialogColors)) + "');\n";
	source += "HELPCOLORS('" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.helpColors)) + "');\n";
	source += "OTHERCOLORS('" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.otherColors)) + "');\n";
	source += "MINIMAPCOLORS('" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.miniMapColors)) + "');\n";
	source += "FILECOMPAREMINIMAPCOLORS('" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.fileCompareMiniMapColors)) + "');\n";
	source += "CODECOLORS('" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.codeColors)) + "');\n";
	source += "FILECOMPARECOLORS('" + escapeMrmacSingleQuotedLiteral(formatFileCompareColorListLiteral(colors.fileCompareColors)) + "');\n";
	source += "DEBUGGERCOLORS('" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.debuggerColors)) + "');\n";
	if (!themeName.empty()) source += "THEME_NAME('" + escapeMrmacSingleQuotedLiteral(themeName) + "');\n";
	source += "END_MACRO;\n";
	return source;
}

bool writeColorThemeFile(const std::string &themeUri, std::string *errorMessage) {
	std::string themePath = normalizeConfiguredPathInput(themeUri);
	std::string themeDir = directoryPartOf(themePath);
	std::string source;

	if (!validateColorThemeFilePath(themePath, errorMessage)) return false;
	if (!ensureDirectoryTree(themeDir, errorMessage)) return false;
	source = buildColorThemeMacroSource(configuredColorSetupSettings());
	if (!writeTextFile(themePath, source)) return setError(errorMessage, "Unable to write color theme file: " + themePath);
	if (!setConfiguredColorThemeFilePath(themePath, errorMessage)) return false;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool ensureColorThemeFileExists(const std::string &themeUri, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(themeUri);
	std::string themeDir = directoryPartOf(normalized);
	struct stat st;

	if (!validateColorThemeFilePath(normalized, errorMessage)) return false;
	if (::stat(normalized.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode)) return setError(errorMessage, "Color theme URI must include a filename.");
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (!ensureDirectoryTree(themeDir, errorMessage)) return false;
	if (!writeTextFile(normalized, buildColorThemeMacroSource(resolveColorSetupDefaults()))) return setError(errorMessage, "Unable to write color theme file: " + normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool loadColorThemeFile(const std::string &themeUri, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(themeUri);
	std::string source;
	std::map<std::string, std::string> assignments;
	MRColorSetupSettings colors = resolveColorSetupDefaults();
	std::string themeName;

	if (!validateColorThemeFilePath(normalized, errorMessage)) return false;
	if (!ensureColorThemeFileExists(normalized, errorMessage)) return false;
	if (!readTextFile(normalized, source)) return setError(errorMessage, "Unable to read color theme file: " + normalized);
	if (!parseThemeSetupAssignments(source, assignments, nullptr, errorMessage)) return false;
	const MRColorSetupGroup groups[] = {
	    MRColorSetupGroup::Window,
	    MRColorSetupGroup::MenuDialog,
	    MRColorSetupGroup::Help,
	    MRColorSetupGroup::Other,
	    MRColorSetupGroup::MiniMap,
	    MRColorSetupGroup::FileCompareMiniMap,
	    MRColorSetupGroup::Code,
	    MRColorSetupGroup::FileCompare,
	    MRColorSetupGroup::Debugger,
	};
	for (MRColorSetupGroup group : groups) {
		const char *key = colorSetupGroupKey(group);
		if (key == nullptr || !applyColorSetupValueInternal(colors, key, assignments[key], errorMessage)) return false;
	}
	storeConfiguredColorSettings(colors);
	themeName = trimAscii(assignments["THEME_NAME"]);
	if (themeName.empty()) themeName = fileNamePartOf(normalized);
	if (!setConfiguredColorThemeDisplayName(themeName, errorMessage)) return false;
	if (!setConfiguredColorThemeFilePath(normalized, errorMessage)) return false;
	recordSettingsRuntimeWrite();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool loadWindowColorThemeGroupValues(const std::string &themeUri, std::array<unsigned char, MRColorSetupSettings::kWindowCount> &outValues, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(themeUri);
	std::string source;
	std::map<std::string, std::string> assignments;

	if (!validateColorThemeFilePath(normalized, errorMessage)) return false;
	if (!ensureColorThemeFileExists(normalized, errorMessage)) return false;
	if (!readTextFile(normalized, source)) return setError(errorMessage, "Unable to read color theme file: " + normalized);
	if (!parseThemeSetupAssignments(source, assignments, nullptr, errorMessage)) return false;
	if (!parseWindowColorListLiteral(assignments["WINDOWCOLORS"], outValues, errorMessage)) return false;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}
