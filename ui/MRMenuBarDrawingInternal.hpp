#ifndef MRMENUBARDRAWINGINTERNAL_HPP
#define MRMENUBARDRAWINGINTERNAL_HPP

#include "MRMenuBar.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"

namespace mr_menu_drawing {

inline TColorAttr resolvedPaletteAttribute(unsigned char paletteIndex, unsigned char fallback) {
	TColorAttr value(fallback);

	static_cast<void>(configuredColorSlotOverride(paletteIndex, value));
	return value;
}

inline unsigned char marqueePaletteSlot(MRMenuBar::MarqueeKind kind) noexcept {
	switch (kind) {
		case MRMenuBar::MarqueeKind::Warning:
			return kMrPaletteMessageWarning;
		case MRMenuBar::MarqueeKind::Error:
			return kMrPaletteMessageError;
		case MRMenuBar::MarqueeKind::Hero:
			return kMrPaletteMessageHero;
		case MRMenuBar::MarqueeKind::Success:
		case MRMenuBar::MarqueeKind::Info:
		default:
			return kMrPaletteMessage;
	}
}

inline unsigned char marqueeFallbackAttribute(MRMenuBar::MarqueeKind kind) noexcept {
	switch (kind) {
		case MRMenuBar::MarqueeKind::Warning:
			return 0x78;
		case MRMenuBar::MarqueeKind::Error:
			return 0x2B;
		case MRMenuBar::MarqueeKind::Hero:
			return 0x2F;
		case MRMenuBar::MarqueeKind::Success:
		case MRMenuBar::MarqueeKind::Info:
		default:
			return 0x2F;
	}
}

inline int markedHotkeyColumn(const char *name) noexcept {
	int column = 0;

	if (name == nullptr) return -1;
	for (const char *pos = name; *pos != '\0'; ++pos) {
		if (pos[0] == '~' && pos[1] != '\0' && pos[2] == '~') return column;
		if (*pos != '~') ++column;
	}
	return -1;
}

} // namespace mr_menu_drawing

#endif
