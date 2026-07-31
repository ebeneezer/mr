#ifndef MRSETTINGSCOLORINTERNAL_HPP
#define MRSETTINGSCOLORINTERNAL_HPP

#include "MRSettingsRuntime.hpp"

#include <array>
#include <string>

[[nodiscard]] unsigned char mrDefaultColorForSlot(unsigned char paletteIndex);
bool parseWindowColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kWindowCount> &outValues, std::string *errorMessage);

#endif
