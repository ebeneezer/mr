#ifndef MRVERSION_HPP
#define MRVERSION_HPP

#include <cstdint>
#include <string>
#include <string_view>

const char *mrDisplayVersion() noexcept;
std::string mrAboutDisplayVersion();
std::uint64_t mrCurrentPersistenceVersion() noexcept;
std::string mrCurrentPersistenceVersionString();
bool mrParsePersistenceVersion(std::string_view text, std::uint64_t &outVersion) noexcept;

#endif
