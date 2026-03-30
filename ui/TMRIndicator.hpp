#ifndef TMRINDICATOR_HPP
#define TMRINDICATOR_HPP

#define Uses_TIndicator
#define Uses_TDrawBuffer
#define Uses_TEvent
#include <tvision/tv.h>

#include <chrono>
#include <cstdio>
#include <cstring>

class TMRIndicator : public TIndicator {
  public:
	TMRIndicator(const TRect &bounds) noexcept
	    : TIndicator(bounds), readOnly_(false), displayColumn_(0), displayLine_(0), blinkTimer_(0),
	      readOnlyBlinkActive_(false), readOnlyBlinkVisible_(false), readOnlyBlinkUntil_() {
		eventMask |= evBroadcast;
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
		if (readOnly_ && (!readOnlyBlinkActive_ || readOnlyBlinkVisible_))
			b.moveStr(0, "🔒", color, 2);
		else if (modified)
			b.putChar(0, '*');

		std::snprintf(s, sizeof(s), " %lu:%lu ", displayLine_ + 1UL, displayColumn_ + 1UL);
		b.moveStr(8 - int(std::strchr(s, ':') - s), s, color);
		writeBuf(0, 0, size.x, 1, b);
	}

	virtual void handleEvent(TEvent &event) override {
		TIndicator::handleEvent(event);
		if (event.what == evBroadcast && event.message.command == cmTimerExpired &&
		    event.message.infoPtr == blinkTimer_) {
			if (std::chrono::steady_clock::now() >= readOnlyBlinkUntil_) {
				stopReadOnlyBlink();
			} else {
				readOnlyBlinkVisible_ = !readOnlyBlinkVisible_;
				drawView();
			}
			clearEvent(event);
		}
	}

	void setDisplayValue(unsigned long column, unsigned long line, Boolean aModified) {
		if (displayColumn_ != column || displayLine_ != line || modified != aModified) {
			displayColumn_ = column;
			displayLine_ = line;
			modified = aModified;
			location.x = short(column > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : column);
			location.y = short(line > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : line);
			drawView();
		}
	}

	void setReadOnly(bool readOnly) {
		if (readOnly_ != readOnly) {
			bool wasReadOnly = readOnly_;
			readOnly_ = readOnly;
			if (!wasReadOnly && readOnly_)
				startReadOnlyBlink();
			else if (!readOnly_)
				stopReadOnlyBlink();
			drawView();
		}
	}

	bool isReadOnly() const {
		return readOnly_;
	}

  private:
	static constexpr char kDragFrame = '\xCD';
	static constexpr char kNormalFrame = '\xC4';

	void startReadOnlyBlink() {
		readOnlyBlinkActive_ = true;
		readOnlyBlinkVisible_ = true;
		readOnlyBlinkUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		if (blinkTimer_ == 0)
			blinkTimer_ = setTimer(250, 250);
	}

	void stopReadOnlyBlink() {
		if (blinkTimer_ != 0) {
			killTimer(blinkTimer_);
			blinkTimer_ = 0;
		}
		readOnlyBlinkActive_ = false;
		readOnlyBlinkVisible_ = true;
		drawView();
	}

	bool readOnly_;
	unsigned long displayColumn_;
	unsigned long displayLine_;
	TTimerId blinkTimer_;
	bool readOnlyBlinkActive_;
	bool readOnlyBlinkVisible_;
	std::chrono::steady_clock::time_point readOnlyBlinkUntil_;
};

#endif
