#ifndef MRSETTINGSCOLORINTERNAL_HPP
#define MRSETTINGSCOLORINTERNAL_HPP

#include "MRSettingsRuntime.hpp"

#include <array>
#include <string>

[[nodiscard]] MRRgbColorAttribute mrDefaultColorForSlot(unsigned char paletteIndex);
bool parseColorListLiteral(const std::string &literal, MRRgbColorAttribute *outValues, std::size_t outCount, std::string *errorMessage);
bool parseWindowColorListLiteral(const std::string &literal, std::array<MRRgbColorAttribute, MRColorSetupSettings::kWindowCount> &outValues, std::string *errorMessage);

#endif
