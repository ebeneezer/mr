#include "MRVMMacroSpecRuntime.hpp"

#include "MRVMValue.hpp"

#include "../../app/utils/MRStringUtils.hpp"

#include <fstream>

namespace {

bool macroFileExists(const std::string &path) {
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	return in.good();
}

}

std::string mrvmStripMrmacExtension(const std::string &value) {
	std::string upper = mrvmUpperKey(value);
	if (upper.size() >= 6 && upper.substr(upper.size() - 6) == ".MRMAC") return value.substr(0, value.size() - 6);
	return value;
}

std::string mrvmMakeMacroFileKey(const std::string &value) {
	return mrvmUpperKey(mrvmStripMrmacExtension(trimAscii(value)));
}

std::string mrvmMakeMacroSourceIdentity(const std::string &sourcePath, const std::string &macroName) {
	const std::string normalizedPath = trimAscii(sourcePath);
	const std::string macroKey = mrvmUpperKey(trimAscii(macroName));

	if (normalizedPath.empty() || macroKey.empty()) return std::string();
	return normalizedPath + "^" + macroKey;
}

bool mrvmParseRunMacroSpec(const std::string &spec, std::string &filePart, std::string &macroPart, std::string &paramPart) {
	std::string trimmed = trimAscii(spec);
	std::size_t spacePos;
	std::string head;
	std::size_t caretPos;

	filePart.clear();
	macroPart.clear();
	paramPart.clear();

	if (trimmed.empty()) return false;

	spacePos = trimmed.find_first_of(" \t\r\n");
	if (spacePos == std::string::npos) head = trimmed;
	else {
		head = trimmed.substr(0, spacePos);
		paramPart = trimAscii(trimmed.substr(spacePos + 1));
	}

	caretPos = head.find('^');
	if (caretPos == std::string::npos) macroPart = head;
	else {
		filePart = head.substr(0, caretPos);
		macroPart = head.substr(caretPos + 1);
	}
	return !macroPart.empty();
}

std::string mrvmResolveMacroFilePath(const std::string &spec, const std::string &macroDirectoryPath) {
	std::string trimmed = trimAscii(spec);
	std::string macroDirectory = trimAscii(macroDirectoryPath);

	auto tryMacroDirectory = [&](const std::string &candidate) -> std::string {
		std::string joined;

		if (macroDirectory.empty() || candidate.empty()) return std::string();
		joined = macroDirectory;
		if (joined.back() != '/') joined += '/';
		joined += candidate;
		return macroFileExists(joined) ? joined : std::string();
	};

	if (trimmed.empty()) return std::string();
	if (macroFileExists(trimmed)) return trimmed;
	if (std::string fromMacroDirectory = tryMacroDirectory(trimmed); !fromMacroDirectory.empty()) return fromMacroDirectory;
	if (mrvmUpperKey(trimmed).size() < 6 || mrvmUpperKey(trimmed).substr(mrvmUpperKey(trimmed).size() - 6) != ".MRMAC") {
		std::string withExt = trimmed + ".mrmac";
		if (macroFileExists(withExt)) return withExt;
		if (std::string withExtFromMacroDirectory = tryMacroDirectory(withExt); !withExtFromMacroDirectory.empty()) return withExtFromMacroDirectory;
	}
	return trimmed;
}
