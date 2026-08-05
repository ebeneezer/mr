#include "MRSettingsColorInternal.hpp"
#include "MRSettingsRuntime.hpp"
#include "MRSettingsThemesProfiles.hpp"
#include "../../app/utils/MRStringUtils.hpp"

#include <array>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

bool parseHexColorToken(const std::string &token, unsigned char &outValue) {
	std::string value = trimAscii(token);
	unsigned int parsed = 0;

	if (value.empty() || value.size() > 2) return false;
	for (char ch : value)
		if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
	parsed = static_cast<unsigned int>(std::strtoul(value.c_str(), nullptr, 16));
	if (parsed > 0xFFu) return false;
	outValue = static_cast<unsigned char>(parsed);
	return true;
}

enum WindowColorProjection {
	wcpSequential,
	wcpSkipEofMarker
};

struct WindowColorFormat {
	char version;
	std::size_t valueCount;
	std::size_t projectedValueCount;
	WindowColorProjection projection;
	const char *sizeError;
};

static const WindowColorFormat kVersionedWindowColorFormats[] = {
    {'7', MRColorSetupSettings::kWindowCount, MRColorSetupSettings::kWindowCount, wcpSequential, "Unexpected WINDOWCOLORS list size for v7."},
    {'6', MRColorSetupSettings::kWindowCount - 1, MRColorSetupSettings::kWindowCount - 1, wcpSequential, "Unexpected WINDOWCOLORS list size for v6."},
    {'5', MRColorSetupSettings::kWindowCount - 2, MRColorSetupSettings::kWindowCount - 2, wcpSequential, "Unexpected WINDOWCOLORS list size for v5."},
    {'4', MRColorSetupSettings::kWindowCount - 3, MRColorSetupSettings::kWindowCount - 3, wcpSequential, "Unexpected WINDOWCOLORS list size for v4."},
    {'3', MRColorSetupSettings::kWindowCount - 4, MRColorSetupSettings::kWindowCount - 4, wcpSequential, "Unexpected WINDOWCOLORS list size for v3."},
    {'2', 8, 7, wcpSkipEofMarker, "Unexpected WINDOWCOLORS list size for v2."},
};

static const WindowColorFormat kLegacyWindowColorFormats[] = {
    {'\0', MRColorSetupSettings::kWindowCount, MRColorSetupSettings::kWindowCount, wcpSequential, nullptr},
    {'\0', MRColorSetupSettings::kWindowCount - 1, MRColorSetupSettings::kWindowCount - 1, wcpSequential, nullptr},
    {'\0', MRColorSetupSettings::kWindowCount - 2, MRColorSetupSettings::kWindowCount - 2, wcpSequential, nullptr},
    {'\0', MRColorSetupSettings::kWindowCount - 3, MRColorSetupSettings::kWindowCount - 3, wcpSequential, nullptr},
    {'\0', MRColorSetupSettings::kWindowCount - 4, MRColorSetupSettings::kWindowCount - 4, wcpSequential, nullptr},
    {'\0', MRColorSetupSettings::kWindowCount - 5, 8, wcpSequential, nullptr},
    {'\0', MRColorSetupSettings::kWindowCount - 6, 8, wcpSequential, nullptr},
    {'\0', MRColorSetupSettings::kWindowCount - 7, 7, wcpSkipEofMarker, nullptr},
};

} // namespace

bool parseWindowColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kWindowCount> &outValues, std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::vector<unsigned char> parsed;
	const WindowColorFormat *format = nullptr;
	bool versionPrefix = false;
	unsigned char value = 0;
	std::size_t windowItemCount = 0;
	const MRColorSetupItem *windowItems = colorSetupGroupItems(MRColorSetupGroup::Window, windowItemCount);
	if (windowItems == nullptr || windowItemCount < outValues.size()) return setError(errorMessage, "WINDOWCOLORS descriptors are incomplete.");

	if (text.size() >= 3 && (text[0] == 'v' || text[0] == 'V') && text[2] == ':') {
		versionPrefix = text[1] == '1';
		for (const WindowColorFormat &candidate : kVersionedWindowColorFormats)
			if (candidate.version == text[1]) {
				format = &candidate;
				versionPrefix = true;
				break;
			}
	}
	if (versionPrefix) text = text.substr(3);
	if (text.empty()) return setError(errorMessage, "Empty color list.");

	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);

		if (!parseHexColorToken(token, value)) return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos) break;
		cursor = comma + 1;
	}

	if (format != nullptr) {
		if (parsed.size() != format->valueCount) return setError(errorMessage, format->sizeError);
	} else {
		for (const WindowColorFormat &candidate : kLegacyWindowColorFormats)
			if (candidate.valueCount == parsed.size()) {
				format = &candidate;
				break;
			}
		if (format == nullptr) return setError(errorMessage, "Unexpected WINDOWCOLORS list size.");
	}

	for (std::size_t i = 0; i < outValues.size(); ++i)
		outValues[i] = mrDefaultColorForSlot(windowItems[i].paletteIndex);
	if (format->projection == wcpSequential) {
		for (std::size_t i = 0; i < format->projectedValueCount; ++i)
			outValues[i] = parsed[i];
	} else {
		for (std::size_t i = 0; i < 3; ++i)
			outValues[i] = parsed[i];
		for (std::size_t i = 3; i < format->projectedValueCount; ++i)
			outValues[i + 1] = parsed[i];
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}
