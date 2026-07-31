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

} // namespace

bool parseWindowColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kWindowCount> &outValues, std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::vector<unsigned char> parsed;
	const unsigned char defaultEofMarker = mrDefaultColorForSlot(kMrPaletteEofMarker);
	const unsigned char defaultLineNumbers = mrDefaultColorForSlot(kMrPaletteLineNumbers);
	const unsigned char defaultCodeFolding = mrDefaultColorForSlot(kMrPaletteCodeFolding);
	const unsigned char defaultCodeFoldingMarker = mrDefaultColorForSlot(kMrPaletteCodeFoldingMarker);
	const unsigned char defaultFormatRuler = mrDefaultColorForSlot(kMrPaletteFormatRuler);
	const unsigned char defaultFocusedPaneBorder = mrDefaultColorForSlot(kMrPaletteFocusedPaneBorder);
	unsigned char value = 0;
	bool v7Format = false;
	bool v6Format = false;
	bool v5Format = false;
	bool v4Format = false;
	bool v3Format = false;
	bool v2Format = false;
	std::size_t windowItemCount = 0;
	const MRColorSetupItem *windowItems = colorSetupGroupItems(MRColorSetupGroup::Window, windowItemCount);
	if (windowItems == nullptr || windowItemCount < outValues.size()) return setError(errorMessage, "WINDOWCOLORS descriptors are incomplete.");
	auto resetWindowColorDefaults = [&]() {
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = mrDefaultColorForSlot(windowItems[i].paletteIndex);
	};

	if (text.rfind("v7:", 0) == 0 || text.rfind("V7:", 0) == 0) {
		text = text.substr(3);
		v7Format = true;
	} else if (text.rfind("v6:", 0) == 0 || text.rfind("V6:", 0) == 0) {
		text = text.substr(3);
		v6Format = true;
	} else if (text.rfind("v5:", 0) == 0 || text.rfind("V5:", 0) == 0) {
		text = text.substr(3);
		v5Format = true;
	} else if (text.rfind("v4:", 0) == 0 || text.rfind("V4:", 0) == 0) {
		text = text.substr(3);
		v4Format = true;
	} else if (text.rfind("v3:", 0) == 0 || text.rfind("V3:", 0) == 0) {
		text = text.substr(3);
		v3Format = true;
	} else if (text.rfind("v2:", 0) == 0 || text.rfind("V2:", 0) == 0) {
		text = text.substr(3);
		v2Format = true;
	} else if (text.rfind("v1:", 0) == 0 || text.rfind("V1:", 0) == 0)
		text = text.substr(3);
	if (text.empty()) return setError(errorMessage, "Empty color list.");

	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);

		if (!parseHexColorToken(token, value)) return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos) break;
		cursor = comma + 1;
	}

	if (v7Format) {
		if (parsed.size() != MRColorSetupSettings::kWindowCount) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v7.");
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = parsed[i];
	} else if (v6Format) {
		if (parsed.size() != MRColorSetupSettings::kWindowCount - 1) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v6.");
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
	} else if (v5Format) {
		if (parsed.size() != MRColorSetupSettings::kWindowCount - 2) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v5.");
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[12] = defaultFocusedPaneBorder;
	} else if (v4Format) {
		if (parsed.size() != MRColorSetupSettings::kWindowCount - 3) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v4.");
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else if (v3Format) {
		if (parsed.size() != MRColorSetupSettings::kWindowCount - 4) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v3.");
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[10] = defaultCodeFoldingMarker;
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else if (v2Format) {
		if (parsed.size() != 8) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v2.");
		resetWindowColorDefaults();
		outValues[0] = parsed[0];
		outValues[1] = parsed[1];
		outValues[2] = parsed[2];
		outValues[3] = defaultEofMarker;
		outValues[4] = parsed[3];
		outValues[5] = parsed[4];
		outValues[6] = parsed[5];
		outValues[7] = parsed[6];
		outValues[8] = defaultLineNumbers;
		outValues[9] = defaultCodeFolding;
		outValues[10] = defaultCodeFoldingMarker;
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount) {
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 1) {
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 2) {
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[12] = defaultFocusedPaneBorder;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 3) {
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 4) {
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[10] = defaultCodeFoldingMarker;
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 5) {
		resetWindowColorDefaults();
		outValues[0] = parsed[0];
		outValues[1] = parsed[1];
		outValues[2] = parsed[2];
		outValues[3] = parsed[3];
		outValues[4] = parsed[4];
		outValues[5] = parsed[5];
		outValues[6] = parsed[6];
		outValues[7] = parsed[7];
		outValues[8] = defaultLineNumbers;
		outValues[9] = defaultCodeFolding;
		outValues[10] = defaultCodeFoldingMarker;
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 6) {
		resetWindowColorDefaults();
		outValues[0] = parsed[0];
		outValues[1] = parsed[1];
		outValues[2] = parsed[2];
		outValues[3] = parsed[3];
		outValues[4] = parsed[4];
		outValues[5] = parsed[5];
		outValues[6] = parsed[6];
		outValues[7] = parsed[7];
		outValues[8] = defaultLineNumbers;
		outValues[9] = defaultCodeFolding;
		outValues[10] = defaultCodeFoldingMarker;
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 7) {
		resetWindowColorDefaults();
		outValues[0] = parsed[0];
		outValues[1] = parsed[1];
		outValues[2] = parsed[2];
		outValues[3] = defaultEofMarker;
		outValues[4] = parsed[3];
		outValues[5] = parsed[4];
		outValues[6] = parsed[5];
		outValues[7] = parsed[6];
		outValues[8] = defaultLineNumbers;
		outValues[9] = defaultCodeFolding;
		outValues[10] = defaultCodeFoldingMarker;
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else {
		return setError(errorMessage, "Unexpected WINDOWCOLORS list size.");
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}
