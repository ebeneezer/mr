#ifndef MRKEYMAPTOKEN_HPP
#define MRKEYMAPTOKEN_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

enum class MRKeymapModifier : std::uint8_t {
	Ctrl = 1 << 0,
	Alt = 1 << 1,
	Shift = 1 << 2
};

enum class MRKeymapBaseKey : std::uint8_t {
	Printable,
	F1,
	F2,
	F3,
	F4,
	F5,
	F6,
	F7,
	F8,
	F9,
	F10,
	F11,
	F12,
	Left,
	Right,
	Up,
	Down,
	Home,
	End,
	PageUp,
	PageDown,
	Enter,
	Tab,
	Esc,
	Backspace,
	Insert,
	Delete,
	KeypadPlus,
	KeypadMinus,
	KeypadMultiply,
	KeypadEnter,
	MouseUp,
	MouseDown,
	MouseLeft,
	MouseRight,
	MouseButtonLeft,
	MouseButtonRight,
	MouseButtonMiddle
};

class MRKeymapToken final {
  public:
	MRKeymapToken(MRKeymapBaseKey key, std::uint8_t modifiers, char printable = 0) noexcept
	    : baseKeyValue(key), printableKeyValue(printable), modifierBits(modifiers) {}

	[[nodiscard]] static std::optional<MRKeymapToken> parse(std::string_view text);
	[[nodiscard]] std::string toString() const;
	[[nodiscard]] MRKeymapBaseKey baseKey() const noexcept {
		return baseKeyValue;
	}
	[[nodiscard]] char printableKey() const noexcept {
		return printableKeyValue;
	}
	[[nodiscard]] std::uint8_t modifiers() const noexcept {
		return modifierBits;
	}
	[[nodiscard]] bool hasModifier(MRKeymapModifier modifier) const noexcept {
		return (modifierBits & static_cast<std::uint8_t>(modifier)) != 0;
	}

	auto operator==(const MRKeymapToken &) const noexcept -> bool = default;

  private:
	MRKeymapBaseKey baseKeyValue{MRKeymapBaseKey::Printable};
	char printableKeyValue{0};
	std::uint8_t modifierBits{0};
};

#endif
