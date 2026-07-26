#include "MRNumericSlider.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

#include "../../config/settings/MRSettingsRuntime.hpp"

namespace {
constexpr unsigned char kPaletteDialogSelector = 58;

TAttrPair configuredPaletteSlotOr(TView *view, unsigned char paletteSlot, ushort fallbackColorIndex) {
	unsigned char biosAttr = 0;

	if (configuredColorSlotOverride(paletteSlot, biosAttr)) return TAttrPair(biosAttr);
	return view != nullptr ? view->getColor(fallbackColorIndex) : TAttrPair(0x70);
}
} // namespace

MRNumericSlider::MRNumericSlider(const TRect &bounds, int32_t aMin, int32_t aMax, int32_t aValue, int32_t aStep, int32_t aPageStep, Format aFormat, ushort aChangedCmd) noexcept : TView(bounds), minValue(std::min(aMin, aMax)), maxValue(std::max(aMin, aMax)), value(0), step(absOrOne(aStep)), pageStep(aPageStep ? absOrOne(aPageStep) : absOrOne(aStep) * 10), textWidth(1), format(aFormat), changedCmd(aChangedCmd), clusterPalette(False) {

	options |= ofSelectable;
	eventMask |= evMouseDown | evMouseWheel;
	recalcMetrics();
	setValueInternal(aValue, False, False);
}

void MRNumericSlider::draw() {
	TDrawBuffer b;
	const Boolean disabled = getState(sfDisabled);
	const Boolean focused = getState(sfFocused) && !disabled;

	const TAttrPair cTrack = clusterPalette ? (disabled ? getColor(0x0505) : focused ? getColor(0x0402) : getColor(0x0301))
	                                        : (disabled ? TAttrPair(mapColor(5)) : focused ? TAttrPair(mapColor(2)) : TAttrPair(mapColor(1)));
	const TAttrPair cHandle = disabled ? TAttrPair(mapColor(5)) : configuredPaletteSlotOr(this, kPaletteDialogSelector, focused ? 2 : 1);
	const char *trackGlyph = disabled ? "·" : focused ? "═" : "─";

	b.moveChar(0, ' ', cTrack, size.x);

	const int hw = handleWidth();
	const int hp = handlePosFromValue(value);

	for (int x = 0; x < hp; ++x)
		b.moveStr((ushort)x, trackGlyph, cTrack, 1);
	for (int x = hp + hw; x < size.x; ++x)
		b.moveStr((ushort)x, trackGlyph, cTrack, 1);

	std::array<char, 32> num{};
	std::array<char, 48> handle{};
	formatValue(num.data(), num.size(), value);
	std::snprintf(handle.data(), handle.size(), "◄%s►", num.data());
	b.moveStr((ushort)hp, handle.data(), cHandle, (ushort)hw);

	writeLine(0, 0, size.x, 1, b);
}

void MRNumericSlider::handleEvent(TEvent &event) {
	if (!getState(sfDisabled)) {
		if (event.what == evMouseDown && containsMouse(event)) {
			select();
			drag(event);
			clearEvent(event);
			return;
		}

		if (event.what == evMouseWheel && containsMouse(event)) {
			switch (event.mouse.wheel) {
				case mwUp:
				case mwRight:
					changeBy(step);
					clearEvent(event);
					return;
				case mwDown:
				case mwLeft:
					changeBy(-step);
					clearEvent(event);
					return;
			}
		}

		if (event.what == evKeyDown) {
			switch (event.keyDown.keyCode) {
				case kbLeft:
				case kbDown:
					changeBy(-step);
					clearEvent(event);
					return;
				case kbRight:
				case kbUp:
					changeBy(step);
					clearEvent(event);
					return;
				case kbPgDn:
					changeBy(-pageStep);
					clearEvent(event);
					return;
				case kbPgUp:
					changeBy(pageStep);
					clearEvent(event);
					return;
				case kbHome:
					setValueInternal(minValue, True, True);
					clearEvent(event);
					return;
				case kbEnd:
					setValueInternal(maxValue, True, True);
					clearEvent(event);
					return;
			}
		}
	}
	TView::handleEvent(event);
}

void MRNumericSlider::sizeLimits(TPoint &min, TPoint &max) {
	TView::sizeLimits(min, max);
	min.x = std::max<short>(min.x, handleWidth() + 2);
	min.y = std::max<short>(min.y, 1);
	max.y = 1;
}

ushort MRNumericSlider::dataSize() {
	return sizeof(int32_t);
}

void MRNumericSlider::getData(void *rec) {
	if (rec) *static_cast<int32_t *>(rec) = value;
}

void MRNumericSlider::setData(void *rec) {
	if (rec) setValueInternal(*static_cast<const int32_t *>(rec), True, False);
}

void MRNumericSlider::setState(ushort aState, Boolean enable) {
	const ushort old = state;
	TView::setState(aState, enable);
	if (old != state && (aState & (sfFocused | sfDisabled | sfSelected | sfActive))) drawView();
}

TColorAttr MRNumericSlider::mapColor(uchar index) {
	if (owner) {
		if (clusterPalette) {
			switch (index) {
				case 1:
					return owner->mapColor(0x10);
				case 2:
					return owner->mapColor(0x11);
				case 5:
					return owner->mapColor(0x1f);
			}
		}
		switch (index) {
			case 1:
				return owner->mapColor(4); // passive track
			case 2:
				return owner->mapColor(5); // focused track
			case 3:
			case 4:
				return owner->mapColor(1); // unused in draw(); handle color is resolved directly via configured palette slot.
			case 5:
				return owner->mapColor(13); // disabled
		}
	}
	switch (index) {
		case 1:
			return TColorAttr(0x08);
		case 2:
			return TColorAttr(0x09);
		case 3:
		case 4:
			return TColorAttr(0x70);
		case 5:
			return TColorAttr(0x07);
		default:
			return TView::mapColor(index);
	}
}

void MRNumericSlider::setValue(int32_t aValue) noexcept {
	setValueInternal(aValue, True, False);
}

void MRNumericSlider::setRange(int32_t aMin, int32_t aMax) noexcept {
	if (aMin > aMax) std::swap(aMin, aMax);
	minValue = aMin;
	maxValue = aMax;
	recalcMetrics();
	setValueInternal(value, True, False);
}

void MRNumericSlider::setStep(int32_t aStep) noexcept {
	step = absOrOne(aStep);
}

void MRNumericSlider::setPageStep(int32_t aPageStep) noexcept {
	pageStep = absOrOne(aPageStep);
}

void MRNumericSlider::setFormat(Format aFormat) noexcept {
	if (format != aFormat) {
		format = aFormat;
		recalcMetrics();
		drawView();
	}
}

void MRNumericSlider::setClusterPalette(Boolean enable) noexcept {
	if (clusterPalette == enable) return;
	clusterPalette = enable;
	drawView();
}

int32_t MRNumericSlider::absOrOne(int32_t v) noexcept {
	return v == 0 ? 1 : (v < 0 ? -v : v);
}

int32_t MRNumericSlider::clamp64(int64_t v, int32_t lo, int32_t hi) noexcept {
	return static_cast<int32_t>(std::clamp(v, static_cast<int64_t>(lo), static_cast<int64_t>(hi)));
}

int MRNumericSlider::trackSpan() const noexcept {
	return std::max(0, size.x - handleWidth());
}

int MRNumericSlider::handlePosFromValue(int32_t v) const noexcept {
	const int span = trackSpan();
	if (span <= 0 || maxValue <= minValue) return 0;

	const int64_t num = int64_t(v - minValue) * span;
	const int64_t den = int64_t(maxValue - minValue);
	return (int)((num + den / 2) / den);
}

int32_t MRNumericSlider::valueFromHandlePos(int pos) const noexcept {
	const int span = trackSpan();
	if (span <= 0 || maxValue <= minValue) return minValue;

	pos = std::clamp(pos, 0, span);
	const int64_t num = int64_t(pos) * (maxValue - minValue);
	return minValue + (int32_t)((num + span / 2) / span);
}

int32_t MRNumericSlider::valueFromMouseX(int x, int dragOffset) const noexcept {
	return valueFromHandlePos(x - dragOffset);
}

void MRNumericSlider::recalcMetrics() noexcept {
	textWidth = calcTextWidth();
}

int MRNumericSlider::calcTextWidth() const noexcept {
	std::array<char, 32> minText{};
	std::array<char, 32> maxText{};
	formatValue(minText.data(), minText.size(), minValue);
	formatValue(maxText.data(), maxText.size(), maxValue);
	return std::max<int>(1, std::max(std::strlen(minText.data()), std::strlen(maxText.data())));
}

void MRNumericSlider::formatValue(char *dst, size_t dstSize, int32_t v) const noexcept {
	if (!dst || dstSize == 0) return;

	if (format == fmtPercent) std::snprintf(dst, dstSize, "%d%%", (int)v);
	else if (minValue >= 0)
		std::snprintf(dst, dstSize, "%0*d", textWidth ? textWidth : 1, (int)v);
	else
		std::snprintf(dst, dstSize, "%*d", textWidth ? textWidth : 1, (int)v);
}

void MRNumericSlider::setValueInternal(int32_t aValue, Boolean redraw, Boolean notify) noexcept {
	const int32_t nv = clamp64(aValue, minValue, maxValue);
	const Boolean changed = nv != value;
	value = nv;
	if (redraw) drawView();
	if (changed && notify) notifyChanged();
}

void MRNumericSlider::changeBy(int32_t delta) noexcept {
	setValueInternal(clamp64(int64_t(value) + delta, minValue, maxValue), True, True);
}

void MRNumericSlider::drag(TEvent &event) noexcept {
	const int hp = handlePosFromValue(value);
	const int hw = handleWidth();
	TPoint p = makeLocal(event.mouse.where);
	int dragOffset = hw / 2;

	if (p.x >= hp && p.x < hp + hw) dragOffset = p.x - hp;
	else
		setValueInternal(valueFromMouseX(p.x, dragOffset), True, True);

	while (mouseEvent(event, evMouseMove | evMouseAuto | evMouseUp)) {
		if (event.what == evMouseUp) break;
		p = makeLocal(event.mouse.where);
		setValueInternal(valueFromMouseX(p.x, dragOffset), True, True);
	}
}

void MRNumericSlider::notifyChanged() noexcept {
	if (owner) message(owner, evBroadcast, changedCmd, this);
}

MRProgressSlider::MRProgressSlider(const TRect &bounds) : MRNumericSlider(bounds, 0, kSliderScale, 0) {
	options &= ~ofSelectable;
	eventMask &= ~evMouse;
}

void MRProgressSlider::setText(std::string value) {
	if (!progressMode && value == text) return;
	progressMode = false;
	text = std::move(value);
	MRNumericSlider::setValue(0);
}

void MRProgressSlider::setProgress(std::size_t aCompleted, std::size_t aTotal, std::string label) {
	aCompleted = std::min(aCompleted, aTotal);
	if (progressMode && aCompleted == completed && aTotal == total && label == text) return;
	progressMode = true;
	completed = aCompleted;
	total = aTotal;
	text = std::move(label);
	const int32_t sliderValue = total == 0 ? 0 : static_cast<int32_t>(static_cast<long double>(completed) * kSliderScale / static_cast<long double>(total));
	MRNumericSlider::setValue(sliderValue);
}

void MRProgressSlider::draw() {
	TDrawBuffer buffer;
	const TAttrPair textColor = owner != nullptr ? TAttrPair(owner->mapColor(1)) : TAttrPair(0x70);

	if (!progressMode) {
		buffer.moveChar(0, ' ', textColor, size.x);
		if (!text.empty()) buffer.moveStr(0, text.c_str(), textColor, size.x);
		writeLine(0, 0, size.x, 1, buffer);
		return;
	}

	const TAttrPair normalColor = owner != nullptr ? TAttrPair(owner->mapColor(kDialogInputLineNormalColor)) : TAttrPair(0x3F);
	const TAttrPair selectedColor = owner != nullptr ? TAttrPair(owner->mapColor(kDialogInputLineSelectedColor)) : TAttrPair(0x3E);
	const std::size_t width = static_cast<std::size_t>(std::max(0, size.x));
	const std::size_t filled = total == 0 ? 0 : (completed / total) * width + ((completed % total) * width) / total;
	const std::size_t labelLength = std::min(width, text.size());
	const std::size_t labelLeft = (width - labelLength) / 2;
	const std::size_t selectedLabelLength = labelLeft >= filled ? 0 : std::min(labelLength, filled - labelLeft);

	buffer.moveChar(0, ' ', normalColor, size.x);
	if (filled != 0) buffer.moveChar(0, ' ', selectedColor, static_cast<int>(filled));
	if (selectedLabelLength != 0) buffer.moveStr(static_cast<ushort>(labelLeft), text.c_str(), selectedColor, static_cast<ushort>(selectedLabelLength));
	if (selectedLabelLength < labelLength)
		buffer.moveStr(static_cast<ushort>(labelLeft + selectedLabelLength), text.c_str() + selectedLabelLength, normalColor, static_cast<ushort>(labelLength - selectedLabelLength));
	writeLine(0, 0, size.x, 1, buffer);
}

void MRProgressSlider::handleEvent(TEvent &event) {
	if ((event.what & evMouse) != 0) return;
	TView::handleEvent(event);
}
