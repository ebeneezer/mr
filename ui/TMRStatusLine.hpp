#ifndef TMRSTATUSLINE_HPP
#define TMRSTATUSLINE_HPP
#define Uses_TStatusLine
#define Uses_TDrawBuffer
#include "MRPalette.hpp"
#include <tvision/tv.h>

class TMRStatusLine : public TStatusLine {
  public:
	TMRStatusLine(const TRect &r, TStatusDef &aDef)
	    : TStatusLine(r, aDef), recordingActive_(false), recordingVisible_(false),
	      showFunctionKeyLabels_(true) {
	}

	void setRecordingState(bool active, bool visible) {
		if (recordingActive_ == active && recordingVisible_ == visible)
			return;
		recordingActive_ = active;
		recordingVisible_ = visible;
		drawView();
	}

	void setShowFunctionKeyLabels(bool enabled) {
		if (showFunctionKeyLabels_ == enabled)
			return;
		showFunctionKeyLabels_ = enabled;
		drawView();
	}

	virtual void draw() override {
		if (!showFunctionKeyLabels_) {
			TDrawBuffer b;
			TColorAttr color = getColor(1);
			b.moveChar(0, ' ', color, size.x);
			writeLine(0, 0, size.x, 1, b);
		} else
		TStatusLine::draw();
		if (!recordingActive_ || !recordingVisible_)
			return;

		static const char *kRecText = " REC ";
		const int recLen = strwidth(kRecText);
		const int recX = size.x - recLen - 1;
		TDrawBuffer b;
		TColorAttr color = getColor(1);

		if (recX < 0)
			return;
		b.moveChar(0, ' ', color, recLen);
		b.moveStr(1, "REC", color);
		writeBuf(recX, 0, recLen, 1, b);
	}

  private:
	bool recordingActive_;
	bool recordingVisible_;
	bool showFunctionKeyLabels_;
};
#endif
