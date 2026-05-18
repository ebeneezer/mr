#ifndef MRPERFORMANCEPANEL_HPP
#define MRPERFORMANCEPANEL_HPP

#define Uses_TDrawBuffer
#define Uses_TPalette
#define Uses_TRect
#define Uses_TView
#include <tvision/tv.h>

#include <string>
#include <vector>

class MRPerformancePanel : public TView {
  public:
	static constexpr int kPreferredHeight = 9;

	explicit MRPerformancePanel(const TRect &bounds) noexcept;

	void setAnimationFrame(unsigned frame) noexcept;
	virtual void draw() override;
	virtual TPalette &getPalette() const override;

  private:
	struct PanelSegment {
		std::string text;
		TColorAttr color;
	};

	void writePanelLine(int y, const std::string &line, TColorAttr color);
	void writePanelSegments(int y, const std::vector<PanelSegment> &segments, TColorAttr fillColor);
	unsigned mAnimationFrame;
};

#endif
