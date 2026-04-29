#ifndef MRKEYMAPACTIONCATALOG_HPP
#define MRKEYMAPACTIONCATALOG_HPP

#include <span>
#include <string_view>

enum class MRKeymapActionOrigin : unsigned char {
	MRMAC,
	MR
};

struct MRKeymapActionDefinition {
	std::string_view id;
	MRKeymapActionOrigin origin;
	std::string_view displayName;
	std::string_view description;
};

class MRKeymapActionCatalog final {
  public:
	[[nodiscard]] static const MRKeymapActionDefinition *findById(std::string_view id) noexcept;
	[[nodiscard]] static bool contains(std::string_view id) noexcept;
	[[nodiscard]] static std::span<const MRKeymapActionDefinition> definitions() noexcept;
};

#endif
