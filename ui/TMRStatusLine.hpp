#ifndef TMRSTATUSLINE_HPP
#define TMRSTATUSLINE_HPP
#define Uses_TStatusLine
#define Uses_TDrawBuffer
#include "MRPalette.hpp"
#include <tvision/tv.h>

class TMRStatusLine : public TStatusLine {
  public:
	TMRStatusLine(const TRect &r, TStatusDef &aDef)
	    : TStatusLine(r, aDef), recordingActive_(false), recordingVisible_(false) {
	}

	void setRecordingState(bool active, bool visible) {
		if (recordingActive_ == active && recordingVisible_ == visible)
			return;
		recordingActive_ = active;
		recordingVisible_ = visible;
		drawView();
	}

	virtual void draw() override {
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
};
#endif
