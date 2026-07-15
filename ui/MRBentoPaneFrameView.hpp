#ifndef MRBENTOPANEFRAMEVIEW_HPP
#define MRBENTOPANEFRAMEVIEW_HPP

#define Uses_TView
#define Uses_TPalette
#define Uses_TRect
#include <tvision/tv.h>

#include <string>

class MRBentoPaneFrameView final : public TView {
  public:
	enum HitKind {
		hitNone = 0,
		hitTitle,
		hitClose,
		hitMaximize
	};

	explicit MRBentoPaneFrameView(const TRect &bounds) noexcept;
	void setPane(int nextLeafId, const char *nextTitle, bool isSource, bool isFocused, bool isMaximized, bool closeActionEnabled, bool maximizeActionEnabled, TColorAttr nextBorderColor);
	[[nodiscard]] int paneLeafId() const noexcept;
	[[nodiscard]] TRect paneRoleListAnchor(int listWidth) const noexcept;
	void setPaneRoleListTitleOpen(bool open, const TRect &listAnchor) noexcept;
	[[nodiscard]] HitKind hitTest(TPoint local) const;

	virtual void draw() override;
	virtual TPalette &getPalette() const override;

  private:
	struct Layout {
		std::string title;
		int titleX;
		int titleWidth;
		int maximizeX;
		int maximizeWidth;
		int closeX;
		int closeWidth;

		Layout() noexcept;
	};

	[[nodiscard]] Layout paneChromeLayout(bool includeRoleListSpan = true) const;
	void drawPaneChrome(TDrawBuffer &buffer, const Layout &layout, TAttrPair frameColor, TAttrPair titleColor, bool withControls, bool maximized) const;

	int leafId;
	bool focused;
	bool source;
	bool maximized;
	bool closeActionEnabled;
	bool maximizeActionEnabled;
	bool roleListTitleOpen;
	int roleListTitleX;
	int roleListTitleWidth;
	TColorAttr borderColor;
	std::string title;
};

#endif
