#include "MRKeymapToken.hpp"

#include <array>
#include <cctype>
#include <string>

namespace {
struct NamedKeySpec {
	std::string_view canonicalName;
	MRKeymapBaseKey key;
};

struct AliasKeySpec {
	std::string_view alias;
	MRKeymapBaseKey key;
	char printable;
};

struct CombinedModifierSpec {
	std::string_view prefix;
	std::uint8_t modifiers;
};

constexpr std::uint8_t kCtrlBit = static_cast<std::uint8_t>(MRKeymapModifier::Ctrl);
constexpr std::uint8_t kAltBit = static_cast<std::uint8_t>(MRKeymapModifier::Alt);
constexpr std::uint8_t kShiftBit = static_cast<std::uint8_t>(MRKeymapModifier::Shift);

constexpr std::array namedKeys{
    NamedKeySpec{"F1", MRKeymapBaseKey::F1},
    NamedKeySpec{"F2", MRKeymapBaseKey::F2},
    NamedKeySpec{"F3", MRKeymapBaseKey::F3},
    NamedKeySpec{"F4", MRKeymapBaseKey::F4},
    NamedKeySpec{"F5", MRKeymapBaseKey::F5},
    NamedKeySpec{"F6", MRKeymapBaseKey::F6},
    NamedKeySpec{"F7", MRKeymapBaseKey::F7},
    NamedKeySpec{"F8", MRKeymapBaseKey::F8},
    NamedKeySpec{"F9", MRKeymapBaseKey::F9},
    NamedKeySpec{"F10", MRKeymapBaseKey::F10},
    NamedKeySpec{"F11", MRKeymapBaseKey::F11},
    NamedKeySpec{"F12", MRKeymapBaseKey::F12},
    NamedKeySpec{"Left", MRKeymapBaseKey::Left},
    NamedKeySpec{"Right", MRKeymapBaseKey::Right},
    NamedKeySpec{"Up", MRKeymapBaseKey::Up},
    NamedKeySpec{"Down", MRKeymapBaseKey::Down},
    NamedKeySpec{"Home", MRKeymapBaseKey::Home},
    NamedKeySpec{"End", MRKeymapBaseKey::End},
    NamedKeySpec{"PageUp", MRKeymapBaseKey::PageUp},
    NamedKeySpec{"PageDown", MRKeymapBaseKey::PageDown},
    NamedKeySpec{"Enter", MRKeymapBaseKey::Enter},
    NamedKeySpec{"Tab", MRKeymapBaseKey::Tab},
    NamedKeySpec{"Esc", MRKeymapBaseKey::Esc},
    NamedKeySpec{"Backspace", MRKeymapBaseKey::Backspace},
    NamedKeySpec{"Insert", MRKeymapBaseKey::Insert},
    NamedKeySpec{"Delete", MRKeymapBaseKey::Delete},
    NamedKeySpec{"KeypadPlus", MRKeymapBaseKey::KeypadPlus},
    NamedKeySpec{"KeypadMinus", MRKeymapBaseKey::KeypadMinus},
    NamedKeySpec{"KeypadMultiply", MRKeymapBaseKey::KeypadMultiply},
    NamedKeySpec{"KeypadEnter", MRKeymapBaseKey::KeypadEnter},
    NamedKeySpec{"MouseUp", MRKeymapBaseKey::MouseUp},
    NamedKeySpec{"MouseDown", MRKeymapBaseKey::MouseDown},
    NamedKeySpec{"MouseLeft", MRKeymapBaseKey::MouseLeft},
    NamedKeySpec{"MouseRight", MRKeymapBaseKey::MouseRight},
    NamedKeySpec{"MouseButtonLeft", MRKeymapBaseKey::MouseButtonLeft},
    NamedKeySpec{"MouseButtonRight", MRKeymapBaseKey::MouseButtonRight},
    NamedKeySpec{"MouseButtonMiddle", MRKeymapBaseKey::MouseButtonMiddle},
};

constexpr std::array aliasKeys{
    AliasKeySpec{"PGUP", MRKeymapBaseKey::PageUp, 0},
    AliasKeySpec{"PGDN", MRKeymapBaseKey::PageDown, 0},
    AliasKeySpec{"INS", MRKeymapBaseKey::Insert, 0},
    AliasKeySpec{"DEL", MRKeymapBaseKey::Delete, 0},
    AliasKeySpec{"GREY+", MRKeymapBaseKey::KeypadPlus, 0},
    AliasKeySpec{"GREY-", MRKeymapBaseKey::KeypadMinus, 0},
    AliasKeySpec{"GREY*", MRKeymapBaseKey::KeypadMultiply, 0},
    AliasKeySpec{"GREYENTER", MRKeymapBaseKey::KeypadEnter, 0},
    AliasKeySpec{"SPACE", MRKeymapBaseKey::Printable, ' '},
    AliasKeySpec{"MINUS", MRKeymapBaseKey::Printable, '-'},
    AliasKeySpec{"EQUAL", MRKeymapBaseKey::Printable, '='},
    AliasKeySpec{"MSUP", MRKeymapBaseKey::MouseUp, 0},
    AliasKeySpec{"MSDN", MRKeymapBaseKey::MouseDown, 0},
    AliasKeySpec{"MSLF", MRKeymapBaseKey::MouseLeft, 0},
    AliasKeySpec{"MSRT", MRKeymapBaseKey::MouseRight, 0},
    AliasKeySpec{"BTN0", MRKeymapBaseKey::MouseButtonLeft, 0},
    AliasKeySpec{"BTN1", MRKeymapBaseKey::MouseButtonRight, 0},
    AliasKeySpec{"BTN2", MRKeymapBaseKey::MouseButtonMiddle, 0},
};

constexpr std::array combinedModifiers{
    CombinedModifierSpec{"CTRLALTSHFT", kCtrlBit | kAltBit | kShiftBit},
    CombinedModifierSpec{"CTRLSHIFT", kCtrlBit | kShiftBit},
    CombinedModifierSpec{"CTRLSHFT", kCtrlBit | kShiftBit},
    CombinedModifierSpec{"ALTSHIFT", kAltBit | kShiftBit},
    CombinedModifierSpec{"ALTSHFT", kAltBit | kShiftBit},
    CombinedModifierSpec{"CTRLALT", kCtrlBit | kAltBit},
    CombinedModifierSpec{"CTRL", kCtrlBit},
    CombinedModifierSpec{"ALT", kAltBit},
    CombinedModifierSpec{"SHIFT", kShiftBit},
    CombinedModifierSpec{"SHFT", kShiftBit},
};

std::string trimAscii(std::string_view text) {
	std::size_t first = 0;
	while (first < text.size() &&
	       std::isspace(static_cast<unsigned char>(text[first])) != 0)
		++first;
	std::size_t last = text.size();
	while (last > first &&
	       std::isspace(static_cast<unsigned char>(text[last - 1])) != 0)
		--last;
	return std::string(text.substr(first, last - first));
}

std::string upperAscii(std::string_view text) {
	std::string upper;
	upper.reserve(text.size());
	for (const unsigned char ch : text)
		upper.push_back(static_cast<char>(std::toupper(ch)));
	return upper;
}

std::optional<MRKeymapToken> parseKeyName(std::string_view keyText, std::uint8_t modifiers) {
	if (keyText.empty())
		return std::nullopt;

	const std::string upper = upperAscii(keyText);
	if (upper.size() == 1 && std::isprint(static_cast<unsigned char>(upper[0])) != 0) {
		const char printable =
		    static_cast<char>(std::isalpha(static_cast<unsigned char>(upper[0])) != 0 ? upper[0] : keyText[0]);
		return MRKeymapToken(MRKeymapBaseKey::Printable, modifiers, printable);
	}

	for (const NamedKeySpec &entry : namedKeys)
		if (upper == upperAscii(entry.canonicalName))
			return MRKeymapToken(entry.key, modifiers);

	for (const AliasKeySpec &entry : aliasKeys)
		if (upper == entry.alias)
			return MRKeymapToken(entry.key, modifiers, entry.printable);

	return std::nullopt;
}

std::optional<MRKeymapToken> parsePlusSeparated(std::string_view inner) {
	std::uint8_t modifiers = 0;
	std::size_t partStart = 0;
	bool sawSeparator = false;

	for (std::size_t i = 0; i <= inner.size(); ++i) {
		if (i < inner.size() && inner[i] != '+')
			continue;
		const std::string_view part = inner.substr(partStart, i - partStart);
		if (part.empty())
			return std::nullopt;
		if (i < inner.size()) {
			sawSeparator = true;
			const std::string upper = upperAscii(part);
			if (upper == "CTRL")
				modifiers |= kCtrlBit;
			else if (upper == "ALT")
				modifiers |= kAltBit;
			else if (upper == "SHIFT" || upper == "SHFT")
				modifiers |= kShiftBit;
			else
				return std::nullopt;
			partStart = i + 1;
			continue;
		}
		return sawSeparator ? parseKeyName(part, modifiers) : std::nullopt;
	}
	return std::nullopt;
}

std::optional<MRKeymapToken> parseLegacyCombined(std::string_view inner) {
	const std::string upper = upperAscii(inner);
	for (const CombinedModifierSpec &entry : combinedModifiers) {
		if (!upper.starts_with(entry.prefix) || upper.size() == entry.prefix.size())
			continue;
		return parseKeyName(inner.substr(entry.prefix.size()), entry.modifiers);
	}
	return std::nullopt;
}

std::string_view canonicalName(MRKeymapBaseKey key) noexcept {
	for (const NamedKeySpec &entry : namedKeys)
		if (entry.key == key)
			return entry.canonicalName;
	return {};
}
} // namespace

std::optional<MRKeymapToken> MRKeymapToken::parse(std::string_view text) {
	const std::string trimmed = trimAscii(text);
	if (trimmed.size() < 3 || trimmed.front() != '<' || trimmed.back() != '>')
		return std::nullopt;

	const std::string_view inner(trimmed.data() + 1, trimmed.size() - 2);
	if (inner.empty())
		return std::nullopt;
	if (inner.size() == 1 && std::isprint(static_cast<unsigned char>(inner.front())) != 0)
		return MRKeymapToken(MRKeymapBaseKey::Printable, 0, inner.front());
	if (const auto token = parsePlusSeparated(inner))
		return token;
	if (const auto token = parseKeyName(inner, 0))
		return token;
	return parseLegacyCombined(inner);
}

std::string MRKeymapToken::toString() const {
	std::string text = "<";
	bool needPlus = false;
	if (hasModifier(MRKeymapModifier::Ctrl)) {
		text += "Ctrl";
		needPlus = true;
	}
	if (hasModifier(MRKeymapModifier::Alt)) {
		if (needPlus)
			text += '+';
		text += "Alt";
		needPlus = true;
	}
	if (hasModifier(MRKeymapModifier::Shift)) {
		if (needPlus)
			text += '+';
		text += "Shift";
		needPlus = true;
	}
	if (needPlus)
		text += '+';

	if (baseKeyValue == MRKeymapBaseKey::Printable) {
		switch (printableKeyValue) {
			case ' ':
				text += "Space";
				break;
			default:
				text.push_back(printableKeyValue);
				break;
		}
	}
	else
		text += canonicalName(baseKeyValue);

	text += '>';
	return text;
}
