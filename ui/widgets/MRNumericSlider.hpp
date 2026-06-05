#ifndef MRNUMERICSLIDER_HPP
#define MRNUMERICSLIDER_HPP

#include <cstddef>
#include <cstdint>

#define Uses_TView
#define Uses_TEvent
#define Uses_TDrawBuffer
#define Uses_TGroup
#define Uses_TKeys
#include <tvision/tv.h>

constexpr ushort cmMRNumericSliderChanged = 0x7A10;

class MRNumericSlider final : public TView {
  public:
	enum Format : uchar {
		fmtRaw,
		fmtPercent
	};

	MRNumericSlider(const TRect &bounds, int32_t aMin, int32_t aMax, int32_t aValue = 0, int32_t aStep = 1, int32_t aPageStep = 10, Format aFormat = fmtRaw, ushort aChangedCmd = cmMRNumericSliderChanged) noexcept;

	void draw() override;
	void handleEvent(TEvent &event) override;
	void sizeLimits(TPoint &min, TPoint &max) override;
	ushort dataSize() override;
	void getData(void *rec) override;
	void setData(void *rec) override;
	void setState(ushort aState, Boolean enable) override;
	TColorAttr mapColor(uchar index) override;

	[[nodiscard]] int32_t getValue() const noexcept {
		return value;
	}
	void setValue(int32_t aValue) noexcept;
	void setRange(int32_t aMin, int32_t aMax) noexcept;
	void setStep(int32_t aStep) noexcept;
	void setPageStep(int32_t aPageStep) noexcept;
	void setFormat(Format aFormat) noexcept;

	[[nodiscard]] Format getFormat() const noexcept {
		return format;
	}
	[[nodiscard]] ushort getChangedCommand() const noexcept {
		return changedCmd;
	}
	void setChangedCommand(ushort cmd) noexcept {
		changedCmd = cmd;
	}

  private:
	static int32_t absOrOne(int32_t v) noexcept;
	static int32_t clamp64(int64_t v, int32_t lo, int32_t hi) noexcept;

	[[nodiscard]] int handleWidth() const noexcept {
		return textWidth + 2;
	} // ◄txt►
	[[nodiscard]] int trackSpan() const noexcept;
	[[nodiscard]] int handlePosFromValue(int32_t v) const noexcept;
	[[nodiscard]] int32_t valueFromHandlePos(int pos) const noexcept;
	[[nodiscard]] int32_t valueFromMouseX(int x, int dragOffset) const noexcept;

	void recalcMetrics() noexcept;
	[[nodiscard]] int calcTextWidth() const noexcept;
	void formatValue(char *dst, size_t dstSize, int32_t v) const noexcept;
	void setValueInternal(int32_t aValue, Boolean redraw, Boolean notify) noexcept;
	void changeBy(int32_t delta) noexcept;
	void drag(TEvent &event) noexcept;
	void notifyChanged() noexcept;

  private:
	int32_t minValue;
	int32_t maxValue;
	int32_t value;
	int32_t step;
	int32_t pageStep;
	int textWidth;
	Format format;
	ushort changedCmd;
};

#endif