#ifndef MRFRAME_HPP
#define MRFRAME_HPP

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

class MRTaskOverviewView : public TView {
  public:
	MRTaskOverviewView(const TRect &bounds) noexcept;
	void setLines(const std::vector<std::string> &lines);
	virtual void draw() override;
	virtual TPalette &getPalette() const override;

  private:
	std::vector<std::string> mLines;
};

class MRTaskOverviewWindow : public TWindow {
  public:
	MRTaskOverviewWindow(const TRect &bounds) noexcept;
	void setLines(const std::vector<std::string> &lines);
	virtual TPalette &getPalette() const override;

  private:
	MRTaskOverviewView *mContent;
};

class MRFrame : public TFrame {
  public:
	struct MarkerState {
		bool modified;
		bool insertMode;
		bool insertModeVisible;
		bool wordWrap;
		bool wordWrapVisible;
		bool background;
		bool backgroundVisible;
		bool readOnly;
		bool readOnlyVisible;
		bool recording;
		bool recordingVisible;
		bool macroBrain;
		bool macroBrainVisible;

		MarkerState() noexcept : modified(false), insertMode(false), insertModeVisible(false), wordWrap(false), wordWrapVisible(false), background(false), backgroundVisible(false), readOnly(false), readOnlyVisible(false), recording(false), recordingVisible(false), macroBrain(false), macroBrainVisible(false) {
		}

		MarkerState(bool aModified, bool anInsertMode, bool anInsertModeVisible, bool aWordWrap, bool aWordWrapVisible, bool aBackground, bool aBackgroundVisible, bool aReadOnly, bool aReadOnlyVisible, bool aRecording, bool aRecordingVisible, bool aMacroBrain, bool aMacroBrainVisible) noexcept : modified(aModified), insertMode(anInsertMode), insertModeVisible(anInsertModeVisible), wordWrap(aWordWrap), wordWrapVisible(aWordWrapVisible), background(aBackground), backgroundVisible(aBackgroundVisible), readOnly(aReadOnly), readOnlyVisible(aReadOnlyVisible), recording(aRecording), recordingVisible(aRecordingVisible), macroBrain(aMacroBrain), macroBrainVisible(aMacroBrainVisible) {
		}
	};

	using MarkerStateProvider = std::function<MarkerState()>;
	using TaskOverviewProvider = std::function<std::vector<std::string>()>;

	MRFrame(const TRect &bounds) noexcept;
	virtual ~MRFrame() override;

	virtual void draw() override;
	virtual void handleEvent(TEvent &event) override;
	virtual void setState(ushort aState, Boolean enable) override;

	void setMarkerStateProvider(MarkerStateProvider provider);
	void setTaskOverviewProvider(TaskOverviewProvider provider);
	void updateTaskHover(TPoint globalMouse, bool forceHide = false);
	void tickTaskOverviewAnimation();

  private:
	void drawFrameLine(TDrawBuffer &frameBuf, short y, short n, TColorAttr color);
	void dragWindow(TEvent &event, uchar mode);
	MarkerState markerState() const;
	int markerStartColumn() const noexcept;
	int taskMarkerColumn(const MarkerState &state) const noexcept;
	int markersEndColumn(const MarkerState &state) const noexcept;
	void showTaskOverview();
	void hideTaskOverview();

	MarkerStateProvider mMarkerStateProvider;
	TaskOverviewProvider mTaskOverviewProvider;
	MRTaskOverviewWindow *mTaskOverviewPopup;
	TGroup *mTaskOverviewPopupOwner;
	bool mTaskOverviewKeepAliveOnEmpty;
};

#endif
