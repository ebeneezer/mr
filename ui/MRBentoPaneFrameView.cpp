#include "MRBentoPaneFrameView.hpp"

#include <algorithm>

namespace {

constexpr int kPaneChromeGap = 1;
constexpr int kPaneCloseButtonWidth = 3;
constexpr int kPaneMaximizeButtonWidth = 3;
constexpr int kPaneChromeFrameRest = 2;
static const char *kPaneCloseIcon = "[\xFE]";
static const char *kPaneMaximizeIcon = "[▴]";
static const char *kPaneRestoreIcon = "[▾]";

struct BentoFrameGlyphs {
	char singleHorizontal = '\xC4';
	char singleVertical = '\xB3';
};

constexpr BentoFrameGlyphs kFrameGlyphs;

} // namespace

MRBentoPaneFrameView::MRBentoPaneFrameView(const TRect &bounds) noexcept : TView(bounds), leafId(-1), focused(false), source(false), maximized(false), closeActionEnabled(true), maximizeActionEnabled(true), roleListTitleOpen(false), roleListTitleX(0), roleListTitleWidth(0), borderColor(0), title("Pane") {
	eventMask = 0;
	options &= static_cast<ushort>(~(ofSelectable | ofTopSelect));
}

void MRBentoPaneFrameView::setPane(int nextLeafId, const char *nextTitle, bool isSource, bool isFocused, bool isMaximized, bool nextCloseActionEnabled, bool nextMaximizeActionEnabled, TColorAttr nextBorderColor) {
	const std::string newTitle = nextTitle != nullptr && *nextTitle != '\0' ? nextTitle : "Pane";

	if (leafId == nextLeafId && title == newTitle && source == isSource && focused == isFocused && maximized == isMaximized && closeActionEnabled == nextCloseActionEnabled && maximizeActionEnabled == nextMaximizeActionEnabled && borderColor == nextBorderColor) return;
	leafId = nextLeafId;
	title = newTitle;
	source = isSource;
	focused = isFocused;
	maximized = isMaximized;
	closeActionEnabled = nextCloseActionEnabled;
	maximizeActionEnabled = nextMaximizeActionEnabled;
	borderColor = nextBorderColor;
}

int MRBentoPaneFrameView::paneLeafId() const noexcept {
	return leafId;
}

TRect MRBentoPaneFrameView::paneRoleListAnchor(int listWidth) const noexcept {
	const Layout layout = paneChromeLayout(false);
	const int rightBracketX = std::max(1, layout.titleX + layout.titleWidth - 1);
	const int left = std::max(1, rightBracketX - std::max(1, listWidth));

	return TRect(left, 0, rightBracketX, 0);
}

void MRBentoPaneFrameView::setPaneRoleListTitleOpen(bool open, const TRect &listAnchor) noexcept {
	const int titleX = std::max(0, static_cast<int>(listAnchor.a.x) - 1);
	const int titleWidth = std::max(2, static_cast<int>(listAnchor.b.x - listAnchor.a.x) + 2);

	if (roleListTitleOpen == open && roleListTitleX == titleX && roleListTitleWidth == titleWidth) return;
	roleListTitleOpen = open;
	roleListTitleX = titleX;
	roleListTitleWidth = titleWidth;
}

MRBentoPaneFrameView::HitKind MRBentoPaneFrameView::hitTest(TPoint local) const {
	if (local.y != 0) return hitNone;
	const Layout layout = paneChromeLayout();

	if (local.x >= layout.closeX && local.x < layout.closeX + layout.closeWidth) return hitClose;
	if (local.x >= layout.maximizeX && local.x < layout.maximizeX + layout.maximizeWidth) return hitMaximize;
	if (local.x >= layout.titleX && local.x < layout.titleX + layout.titleWidth) return hitTitle;
	return hitNone;
}

void MRBentoPaneFrameView::draw() {
	TDrawBuffer buffer;
	const TAttrPair frameColor = TAttrPair(borderColor);
	const Layout layout = paneChromeLayout();

	for (int y = 0; y < size.y; ++y) {
		if (size.x <= 0) continue;
		if (y == 0 || y == size.y - 1) {
			buffer.moveChar(0, kFrameGlyphs.singleHorizontal, frameColor, size.x);
			buffer.putChar(0, y == 0 ? '\xDA' : '\xC0');
			if (size.x > 1) buffer.putChar(static_cast<ushort>(size.x - 1), y == 0 ? '\xBF' : '\xD9');
			if (y == 0) drawPaneChrome(buffer, layout, frameColor, frameColor, focused, maximized);
			writeBuf(0, y, size.x, 1, buffer);
		} else {
			buffer.moveChar(0, kFrameGlyphs.singleVertical, frameColor, 1);
			writeBuf(0, y, 1, 1, buffer);
			if (size.x > 1) {
				buffer.moveChar(0, kFrameGlyphs.singleVertical, frameColor, 1);
				writeBuf(size.x - 1, y, 1, 1, buffer);
			}
		}
	}
}

TPalette &MRBentoPaneFrameView::getPalette() const {
	static TPalette palette("\x06\x05\x04\x02", 4);
	return palette;
}

MRBentoPaneFrameView::Layout::Layout() noexcept : title(), titleX(0), titleWidth(0), maximizeX(0), maximizeWidth(0), closeX(0), closeWidth(0) {
}

MRBentoPaneFrameView::Layout MRBentoPaneFrameView::paneChromeLayout(bool includeRoleListSpan) const {
	Layout layout;
	const int left = 0;
	const int right = std::max(0, size.x);
	const int closeLeftX = left + kPaneChromeFrameRest;
	const int maximizeRightX = right - kPaneChromeFrameRest;
	const bool withControls = focused;
	const int leftContentX = closeLeftX + (withControls ? kPaneCloseButtonWidth + kPaneChromeGap : 0);

	layout.title = "[" + title + "]";
	layout.closeWidth = withControls && closeActionEnabled && right >= closeLeftX + kPaneCloseButtonWidth ? kPaneCloseButtonWidth : 0;
	layout.closeX = layout.closeWidth > 0 ? closeLeftX : 0;
	layout.maximizeWidth = withControls && maximizeActionEnabled && maximizeRightX >= left + kPaneMaximizeButtonWidth ? kPaneMaximizeButtonWidth : 0;
	layout.maximizeX = layout.maximizeWidth > 0 ? maximizeRightX - kPaneMaximizeButtonWidth : size.x;
	const int titleRightX = layout.maximizeWidth > 0 ? layout.maximizeX - kPaneChromeGap : std::max(0, size.x - kPaneChromeFrameRest);
	const int boundedTitleRightX = std::min(titleRightX, std::max(left, right - kPaneChromeFrameRest));
	const int titleAvailable = std::max(0, boundedTitleRightX - leftContentX);

	layout.titleWidth = std::min(static_cast<int>(layout.title.size()), titleAvailable);
	layout.titleX = boundedTitleRightX - layout.titleWidth;
	if (withControls && includeRoleListSpan && roleListTitleOpen) {
		layout.titleX = std::clamp(roleListTitleX, leftContentX, std::max(leftContentX, titleRightX - 2));
		layout.titleWidth = std::clamp(roleListTitleWidth, 2, std::max(2, titleRightX - layout.titleX));
		layout.title.assign(static_cast<std::size_t>(layout.titleWidth), ' ');
		layout.title.front() = '[';
		layout.title.back() = ']';
	}
	return layout;
}

void MRBentoPaneFrameView::drawPaneChrome(TDrawBuffer &buffer, const Layout &layout, TAttrPair frameColor, TAttrPair titleColor, bool withControls, bool isMaximized) const {
	if (withControls && layout.closeWidth > 0) buffer.moveStr(static_cast<ushort>(layout.closeX), kPaneCloseIcon, frameColor, layout.closeWidth);
	if (layout.titleWidth > 0) buffer.moveStr(static_cast<ushort>(layout.titleX), layout.title.c_str(), titleColor, layout.titleWidth);
	if (withControls && layout.maximizeWidth > 0) buffer.moveStr(static_cast<ushort>(layout.maximizeX), isMaximized ? kPaneRestoreIcon : kPaneMaximizeIcon, frameColor, layout.maximizeWidth);
}
