#define Uses_TMenuBar
#define Uses_TDrawBuffer
#define Uses_TMenu
#define Uses_TMenuItem
#define Uses_TRect
#define Uses_TSubMenu
#include <tvision/tv.h>

#include "TMRMenuBar.hpp"

#include <algorithm>

void TMRMenuBar::draw() {
	TAttrPair color;
	short x, l;
	TMenuItem *p;
	TDrawBuffer b;

	TAttrPair cNormal = getColor(0x0301);
	TAttrPair cSelect = getColor(0x0604);
	TAttrPair cNormDisabled = getColor(0x0202);
	TAttrPair cSelDisabled = getColor(0x0505);
	TColorAttr cStatus = TColorAttr(cNormal);
	TColorAttr cHero = TColorAttr(cNormal);
	int rightLen = rightStatus_.empty() ? 0 : static_cast<int>(rightStatus_.size());
	int heroLen = heroStatus_.empty() ? 0 : static_cast<int>(heroStatus_.size());
	int reserve = 1;

	setStyle(cStatus, getStyle(cStatus) | slBold);
	setStyle(cHero, getStyle(cHero) | slBold);
	switch (heroKind_) {
		case HeroKind::Success:
			setFore(cHero, TColorDesired(TColorRGB(0x7D, 0xFF, 0x7A)));
			break;
		case HeroKind::Warning:
			setFore(cHero, TColorDesired(TColorRGB(0xFF, 0xD3, 0x4D)));
			break;
		case HeroKind::Error:
			setFore(cHero, TColorDesired(TColorRGB(0xFF, 0x8A, 0x65)));
			break;
		case HeroKind::Info:
		default:
			setFore(cHero, TColorDesired(TColorRGB(0x7A, 0xE3, 0xFF)));
			break;
	}

	b.moveChar(0, ' ', cNormal, size.x);
	if (rightLen != 0)
		reserve += rightLen + 1;
	if (heroLen != 0)
		reserve += heroLen + 2;
	if (menu != 0) {
		x = 1;
		p = menu->items;
		while (p != 0) {
			if (p->name != 0) {
				l = cstrlen(p->name);
				if (x + l < size.x - reserve) {
					if (p->disabled)
						if (p == current)
							color = cSelDisabled;
						else
							color = cNormDisabled;
					else if (p == current)
						color = cSelect;
					else
						color = cNormal;

					b.moveChar(x, ' ', color, 1);
					b.moveCStr(x + 1, p->name, color);
					b.moveChar(x + l + 1, ' ', color, 1);
				}
				x += l + 2;
			}
			p = p->next;
		}
	}

	if (rightLen != 0) {
		int start = size.x - rightLen - 1;
		if (start < 1)
			start = 1;
		b.moveStr(static_cast<ushort>(start), rightStatus_.c_str(), cStatus,
		          static_cast<ushort>(std::min(rightLen, size.x - start)));
		if (heroLen != 0) {
			int heroStart = start - heroLen - 2;
			if (heroStart < 1)
				heroStart = 1;
			b.moveStr(static_cast<ushort>(heroStart), heroStatus_.c_str(), cHero,
			          static_cast<ushort>(std::min(heroLen, std::max(0, start - heroStart - 1))));
		}
	} else if (heroLen != 0) {
		int start = size.x - heroLen - 1;
		if (start < 1)
			start = 1;
		b.moveStr(static_cast<ushort>(start), heroStatus_.c_str(), cHero,
		          static_cast<ushort>(std::min(heroLen, size.x - start)));
	}

	writeBuf(0, 0, size.x, 1, b);
}
