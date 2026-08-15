#include "MRSettingsColorInternal.hpp"
#include "MRSettingsRuntime.hpp"
#include "../../app/utils/MRStringUtils.hpp"

#include <array>
#include <cctype>
#include <cstdlib>
#include <string>

namespace {

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

bool parseRgbToken(const std::string &token, std::uint32_t &outValue) {
	std::string value = trimAscii(token);
	unsigned long parsed = 0;

	if (value.size() != 6) return false;
	for (char ch : value)
		if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
	parsed = std::strtoul(value.c_str(), nullptr, 16);
	if (parsed > 0xFFFFFFul) return false;
	outValue = static_cast<std::uint32_t>(parsed);
	return true;
}

} // namespace

bool parseColorListLiteral(const std::string &literal, MRRgbColorAttribute *outValues, std::size_t outCount, std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::array<MRRgbColorAttribute, MRColorSetupSettings::kCodeCount> parsedValues{};
	std::size_t cursor = 6;
	std::size_t index = 0;

	if (text.size() < 6 || upperAscii(text.substr(0, 6)) != "RGB24:") return setError(errorMessage, "Expected rgb24:RRGGBB/RRGGBB,... color list.");
	if (outValues == nullptr || outCount == 0 || outCount > parsedValues.size()) return setError(errorMessage, "Unexpected empty or oversized color group.");
	while (cursor <= text.size() && index < outCount) {
		const std::size_t comma = text.find(',', cursor);
		const std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
		const std::size_t slash = token.find('/');
		MRRgbColorAttribute parsed;

		if (slash == std::string::npos || token.find('/', slash + 1) != std::string::npos || !parseRgbToken(token.substr(0, slash), parsed.foregroundRgb) || !parseRgbToken(token.substr(slash + 1), parsed.backgroundRgb))
			return setError(errorMessage, "Expected rgb24:RRGGBB/RRGGBB,... color list.");
		parsedValues[index++] = parsed;
		if (comma == std::string::npos) {
			cursor = text.size() + 1;
			break;
		}
		cursor = comma + 1;
	}
	if (index != outCount || cursor <= text.size()) return setError(errorMessage, "Unexpected RGB color list size.");
	for (std::size_t i = 0; i < outCount; ++i)
		outValues[i] = parsedValues[i];
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseWindowColorListLiteral(const std::string &literal, std::array<MRRgbColorAttribute, MRColorSetupSettings::kWindowCount> &outValues, std::string *errorMessage) {
	return parseColorListLiteral(literal, outValues.data(), outValues.size(), errorMessage);
}
