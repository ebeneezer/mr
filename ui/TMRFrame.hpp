#ifndef TMRFRAME_HPP
#define TMRFRAME_HPP

#define Uses_TFrame
#define Uses_TRect
#define Uses_TEvent
#define Uses_TDrawBuffer
#define Uses_TView
#define Uses_TGroup
#define Uses_TWindow
#include <tvision/tv.h>

#include <functional>
#include <string>
#include <vector>

class TMRTaskOverviewView : public TView {
  public:
	TMRTaskOverviewView(const TRect &bounds) noexcept;
	void setLines(const std::vector<std::string> &lines);
	virtual void draw() override;
	virtual TPalette &getPalette() const override;

  private:
	std::vector<std::string> lines_;
};

class TMRTaskOverviewWindow : public TWindow {
  public:
	TMRTaskOverviewWindow(const TRect &bounds) noexcept;
	void setLines(const std::vector<std::string> &lines);
	virtual TPalette &getPalette() const override;

  private:
	TMRTaskOverviewView *content_;
};

class TMRFrame : public TFrame {
  public:
	struct MarkerState {
		bool modified;
		bool insertMode;
		bool background;
		bool readOnly;

		MarkerState() noexcept : modified(false), insertMode(false), background(false), readOnly(false) {
		}

		MarkerState(bool aModified, bool anInsertMode, bool aBackground, bool aReadOnly) noexcept
		    : modified(aModified), insertMode(anInsertMode), background(aBackground), readOnly(aReadOnly) {
		}
	};

	using MarkerStateProvider = std::function<MarkerState()>;
	using TaskOverviewProvider = std::function<std::vector<std::string>()>;

	TMRFrame(const TRect &bounds) noexcept;
	virtual ~TMRFrame() override;

	virtual void draw() override;
	virtual void handleEvent(TEvent &event) override;
	virtual void setState(ushort aState, Boolean enable) override;

	void setMarkerStateProvider(MarkerStateProvider provider);
	void setTaskOverviewProvider(TaskOverviewProvider provider);
	void updateTaskHover(TPoint globalMouse, bool forceHide = false);

  private:
	void drawFrameLine(TDrawBuffer &frameBuf, short y, short n, TColorAttr color);
	void dragWindow(TEvent &event, uchar mode);
	MarkerState markerState() const;
	int markerStartColumn() const noexcept;
	int taskMarkerColumn(const MarkerState &state) const noexcept;
	int markersEndColumn(const MarkerState &state) const noexcept;
	void showTaskOverview();
	void hideTaskOverview();

	MarkerStateProvider markerStateProvider_;
	TaskOverviewProvider taskOverviewProvider_;
	TMRTaskOverviewWindow *taskOverviewPopup_;
	TGroup *taskOverviewPopupOwner_;
};

#endif
