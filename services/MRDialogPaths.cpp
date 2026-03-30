#include "MRDialogPaths.hpp"

#include <cstring>
#include <string>

namespace {

std::string &rememberedLoadDirectory() {
	static std::string value;
	return value;
}

std::string normalizeDialogPath(const char *path) {
	std::string result = path != nullptr ? path : "";
	for (std::size_t i = 0; i < result.size(); ++i)
		if (result[i] == '\\')
			result[i] = '/';
	return result;
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
