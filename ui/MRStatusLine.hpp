#ifndef MRSTATUSLINE_HPP
#define MRSTATUSLINE_HPP
#define Uses_TStatusLine
#define Uses_TDrawBuffer
#include "MRPalette.hpp"
#include <tvision/tv.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

void mrvmUiInvalidateScreenBase() noexcept;

class MRStatusLine : public TStatusLine {
  public:
	MRStatusLine(const TRect &r, TStatusDef &aDef)
	    : TStatusLine(r, aDef), mRecordingActive(false), mRecordingVisible(false),
	      mShowFunctionKeyLabels(true), mMacroFunctionLabels() {
	}

	void setRecordingState(bool active, bool visible) {
		if (mRecordingActive == active && mRecordingVisible == visible)
			return;
		mRecordingActive = active;
		mRecordingVisible = visible;
		drawView();
	}

	void setShowFunctionKeyLabels(bool enabled) {
		if (mShowFunctionKeyLabels == enabled)
			return;
		mShowFunctionKeyLabels = enabled;
		drawView();
	}

	void setMacroFunctionLabels(const std::vector<std::string> &labels) {
		if (mMacroFunctionLabels == labels)
			return;
		mMacroFunctionLabels = labels;
		drawView();
	}

	virtual void draw() override {
		if (!mShowFunctionKeyLabels) {
			TDrawBuffer b;
			TColorAttr color = getColor(1);
			b.moveChar(0, ' ', color, size.x);
			writeLine(0, 0, size.x, 1, b);
		} else if (!mMacroFunctionLabels.empty()) {
			drawMacroFunctionLabels();
		} else
		TStatusLine::draw();
		if (!mRecordingActive || !mRecordingVisible) {
			mrvmUiInvalidateScreenBase();
			return;
		}

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
		mrvmUiInvalidateScreenBase();
	}

  private:
	void drawMacroFunctionLabels() {
		static constexpr std::array<int, 12> visibleKeyNumbers = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 41, 42};
		TDrawBuffer buffer;
		TColorAttr color = getColor(1);
		const int segmentCount = size.x >= 120 ? 12 : (size.x >= 100 ? 10 : 8);
		const int segmentWidth = std::max(1, size.x / std::max(1, segmentCount));

		buffer.moveChar(0, ' ', color, size.x);
		for (int segment = 0; segment < segmentCount; ++segment) {
			const int keyNumber = visibleKeyNumbers[static_cast<std::size_t>(segment)];
			std::string text;
			int x = segment * segmentWidth;
			int width = segment == segmentCount - 1 ? size.x - x : segmentWidth;

			if (keyNumber > 0 && keyNumber < static_cast<int>(mMacroFunctionLabels.size()) &&
			    !mMacroFunctionLabels[static_cast<std::size_t>(keyNumber)].empty()) {
				text = "F";
				text += std::to_string(keyNumber <= 10 ? keyNumber : keyNumber - 30);
				text.push_back(' ');
				text += mMacroFunctionLabels[static_cast<std::size_t>(keyNumber)];
			}
			if (width <= 0)
				continue;
			if (static_cast<int>(text.size()) > width)
				text.resize(static_cast<std::size_t>(width));
			buffer.moveStr(static_cast<short>(x), text.c_str(), color);
		}
		writeLine(0, 0, size.x, 1, buffer);
	}

	bool mRecordingActive;
	bool mRecordingVisible;
	bool mShowFunctionKeyLabels;
	std::vector<std::string> mMacroFunctionLabels;
};
#endif
