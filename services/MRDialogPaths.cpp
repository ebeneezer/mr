#include "MRDialogPaths.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <string>
#include <unistd.h>

namespace {

std::string &rememberedLoadDirectory() {
	static std::string value;
	return value;
}

std::string &configuredMacroDirectory() {
	static std::string value;
	return value;
}

std::string trimAscii(const std::string &value) {
	std::size_t start = 0;
	std::size_t end = value.size();

	while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0)
		++start;
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
		--end;
	return value.substr(start, end - start);
}

std::string normalizeDialogPath(const char *path) {
	std::string result = path != nullptr ? path : "";
	for (std::size_t i = 0; i < result.size(); ++i)
		if (result[i] == '\\')
			result[i] = '/';
	return result;
}

std::string expandUserPath(const std::string &input) {
	std::string path = trimAscii(input);

	if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != nullptr && *home != '\0')
			return std::string(home) + path.substr(1);
	}
	return path;
}

bool isReadableDirectory(const std::string &path) {
	struct stat st;
	if (path.empty())
		return false;
	if (::stat(path.c_str(), &st) != 0)
		return false;
	if (!S_ISDIR(st.st_mode))
		return false;
	return ::access(path.c_str(), R_OK | X_OK) == 0;
}

std::string directoryPartOf(const std::string &path) {
	if (path.empty())
		return std::string();
	std::size_t pos = path.find_last_of('/');
	if (pos == std::string::npos)
		return std::string();
	if (pos == 0)
		return "/";
	return path.substr(0, pos);
}

void copyToBuffer(char *buffer, std::size_t bufferSize, const std::string &value) {
	if (buffer == nullptr || bufferSize == 0)
		return;
	std::memset(buffer, 0, bufferSize);
	std::strncpy(buffer, value.c_str(), bufferSize - 1);
	buffer[bufferSize - 1] = '\0';
}

} // namespace

void initRememberedLoadDialogPath(char *buffer, std::size_t bufferSize, const char *pattern) {
	std::string initial;
	const std::string &dir = rememberedLoadDirectory();
	const char *safePattern = (pattern != nullptr && *pattern != '\0') ? pattern : "*.*";

	if (!dir.empty()) {
		initial = dir;
		if (initial.back() != '/')
			initial += '/';
		initial += safePattern;
	} else
		initial = safePattern;
	copyToBuffer(buffer, bufferSize, initial);
}

void rememberLoadDialogPath(const char *path) {
	std::string normalized = normalizeDialogPath(path);
	std::string dir = directoryPartOf(normalized);

	if (!dir.empty())
		rememberedLoadDirectory() = dir;
}

bool setConfiguredMacroDirectoryPath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeDialogPath(expandUserPath(path).c_str());

	if (normalized.empty()) {
		if (errorMessage != nullptr)
			*errorMessage = "Empty directory path.";
		return false;
	}
	if (!isReadableDirectory(normalized)) {
		if (errorMessage != nullptr)
			*errorMessage = "Directory is missing or not readable: " + normalized;
		return false;
	}
	configuredMacroDirectory() = normalized;
	rememberedLoadDirectory() = normalized;
	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

std::string configuredMacroDirectoryPath() {
	const std::string &configured = configuredMacroDirectory();
	if (!isReadableDirectory(configured))
		return std::string();
	return configured;
}

std::string defaultSettingsMacroFilePath() {
	const char *home = std::getenv("HOME");
	if (home != nullptr && *home != '\0')
		return std::string(home) + "/.config/mr/settings.mrmac";
	return ".config/mr/settings.mrmac";
}

std::string defaultMacroDirectoryPath() {
	char pathProbe[1024];
	std::string probe;
	std::string dir;
	std::string configured = configuredMacroDirectoryPath();

	if (!configured.empty())
		return configured;

	std::memset(pathProbe, 0, sizeof(pathProbe));
	initRememberedLoadDialogPath(pathProbe, sizeof(pathProbe), "*.mrmac");
	probe = normalizeDialogPath(pathProbe);
	dir = directoryPartOf(probe);
	if (!dir.empty() && ::access(dir.c_str(), R_OK) == 0)
		return dir;
	if (::access("mrmac/macros", R_OK) == 0)
		return "mrmac/macros";
	return ".";
}
