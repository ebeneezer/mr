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
const char *mrSettingsVersionSetupKey() noexcept;
const char *mrThemeVersionSetupKey() noexcept;
const char *mrKeymapVersionSetupKey() noexcept;
std::string mrInvalidPersistenceVersionMessage(std::string_view artifact);
std::string mrFuturePersistenceVersionMessage(std::string_view artifact, std::string_view versionLiteral);
std::string mrFuturePersistenceVersionMessagePrefix(std::string_view artifact);
std::string mrUnsupportedCurrentBuildVersionMessage(std::string_view artifact);

#endif
