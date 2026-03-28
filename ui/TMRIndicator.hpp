#ifndef TMRINDICATOR_HPP
#define TMRINDICATOR_HPP

#define Uses_TIndicator
#define Uses_TDrawBuffer
#include <tvision/tv.h>

#include <cstdio>
#include <cstring>

class TMRIndicator : public TIndicator {
  public:
	TMRIndicator(const TRect &bounds) noexcept : TIndicator(bounds), readOnly_(false) {
	}

	virtual void draw() override {
		TColorAttr color;
		char frame;
		TDrawBuffer b;
		char s[32];

		if ((state & sfDragging) == 0) {
			color = getColor(1);
			frame = kDragFrame;
		} else {
			color = getColor(2);
			frame = kNormalFrame;
		}

		b.moveChar(0, frame, color, size.x);
		if (readOnly_)
			b.moveStr(0, u8"🔒", color, 2);
		else if (modified)
			b.putChar(0, 15);

		std::snprintf(s, sizeof(s), " %d:%d ", location.y + 1, location.x + 1);
		b.moveStr(8 - int(std::strchr(s, ':') - s), s, color);
		writeBuf(0, 0, size.x, 1, b);
	}

	void setReadOnly(bool readOnly) {
		if (readOnly_ != readOnly) {
			readOnly_ = readOnly;
			drawView();
		}
	}

	bool isReadOnly() const {
		return readOnly_;
	}

  private:
	static constexpr char kDragFrame = '\xCD';
	static constexpr char kNormalFrame = '\xC4';
	bool readOnly_;
};

#endif
