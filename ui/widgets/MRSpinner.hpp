#ifndef MRSPINNER_HPP
#define MRSPINNER_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#define Uses_TView
#define Uses_TEvent
#define Uses_TDrawBuffer
#define Uses_TGroup
#define Uses_TKeys
#include <tvision/tv.h>

constexpr ushort cmMRSpinnerChanged = 0x7A11;

class MRSpinner final : public TView {
  public:
	MRSpinner(const TRect &bounds, std::vector<std::string> values, std::string initialValue = std::string(), ushort changedCommand = cmMRSpinnerChanged);

	void draw() override;
	void handleEvent(TEvent &event) override;
	void sizeLimits(TPoint &min, TPoint &max) override;
	ushort dataSize() override;
	void getData(void *rec) override;
	void setData(void *rec) override;
	void setState(ushort aState, Boolean enable) override;

	[[nodiscard]] const std::string &currentValue() const noexcept;
	void setCurrentValue(const std::string &value) noexcept;
	void setCurrentIndex(int index) noexcept;
	[[nodiscard]] int currentIndex() const noexcept {
		return valueIndex;
	}

  private:
	[[nodiscard]] int normalizedIndex(int index) const noexcept;
	[[nodiscard]] int displayWidth() const noexcept;
	void changeBy(int delta) noexcept;
	void notifyChanged() noexcept;
	void drawLineText(int y, const char *text, TAttrPair color);
	[[nodiscard]] TAttrPair colorForSlot(unsigned char paletteSlot, ushort fallbackIndex, unsigned char fallbackAttr);

	std::vector<std::string> mValues;
	int valueIndex;
	ushort changedCmd;
};

#endif
