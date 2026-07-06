#include "MRSpinner.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#include "../../config/settings/MRSettingsRuntime.hpp"

namespace {
const char *spinnerUpGlyph() {
	return "▲";
}

const char *spinnerDownGlyph() {
	return "▼";
}
} // namespace

MRSpinner::MRSpinner(const TRect &bounds, std::vector<std::string> values, std::string initialValue, ushort changedCommand)
    : TView(bounds), mValues(std::move(values)), valueIndex(0), changedCmd(changedCommand) {
	if (mValues.empty()) mValues.push_back(" ");
	for (std::string &value : mValues)
		if (value.empty()) value = " ";
	options |= ofSelectable | ofFirstClick;
	eventMask |= evMouseDown | evMouseWheel | evKeyDown;
	setCurrentValue(initialValue);
}

void MRSpinner::draw() {
	const Boolean disabled = getState(sfDisabled);
	const Boolean focused = getState(sfFocused) && !disabled;
	const TAttrPair handleColor = disabled ? colorForSlot(kMrPaletteDialogInactiveElements, 13, 0x07)
	                                       : colorForSlot(focused ? kMrPaletteFocusedSpinnerHandles : kMrPaletteSpinnerHandles, focused ? 2 : 1, focused ? 0x70 : 0x17);
	const TAttrPair displayColor = disabled ? colorForSlot(kMrPaletteDialogInactiveElements, 13, 0x07)
	                                        : colorForSlot(focused ? kMrPaletteFocusedSpinnerDisplay : kMrPaletteSpinnerDisplay, focused ? 2 : 1, focused ? 0x70 : 0x17);

	drawLineText(0, spinnerUpGlyph(), handleColor);
	drawLineText(1, currentValue().c_str(), displayColor);
	drawLineText(2, spinnerDownGlyph(), handleColor);
}

void MRSpinner::handleEvent(TEvent &event) {
	if (!getState(sfDisabled)) {
		if (event.what == evMouseDown && containsMouse(event)) {
			select();
			TPoint point = makeLocal(event.mouse.where);

			if (point.y <= 0) changeBy(-1);
			else if (point.y >= size.y - 1)
				changeBy(1);
			clearEvent(event);
			return;
		}

		if (event.what == evMouseWheel && (getState(sfFocused) || containsMouse(event))) {
			if (containsMouse(event)) select();
			switch (event.mouse.wheel) {
				case mwUp:
				case mwLeft:
					changeBy(-1);
					clearEvent(event);
					return;
				case mwDown:
				case mwRight:
					changeBy(1);
					clearEvent(event);
					return;
			}
		}

		if (event.what == evKeyDown && getState(sfFocused)) {
			switch (event.keyDown.keyCode) {
				case kbUp:
				case kbLeft:
					changeBy(-1);
					clearEvent(event);
					return;
				case kbDown:
				case kbRight:
					changeBy(1);
					clearEvent(event);
					return;
				case kbHome:
					if (valueIndex != 0) {
						setCurrentIndex(0);
						notifyChanged();
					}
					clearEvent(event);
					return;
				case kbEnd:
					if (valueIndex != static_cast<int>(mValues.size()) - 1) {
						setCurrentIndex(static_cast<int>(mValues.size()) - 1);
						notifyChanged();
					}
					clearEvent(event);
					return;
			}
		}
	}
	TView::handleEvent(event);
}

void MRSpinner::sizeLimits(TPoint &min, TPoint &max) {
	TView::sizeLimits(min, max);
	min.x = std::max<short>(min.x, static_cast<short>(displayWidth()));
	min.y = std::max<short>(min.y, 3);
	max.y = 3;
}

ushort MRSpinner::dataSize() {
	return sizeof(int32_t);
}

void MRSpinner::getData(void *rec) {
	if (rec != nullptr) *static_cast<int32_t *>(rec) = static_cast<int32_t>(valueIndex);
}

void MRSpinner::setData(void *rec) {
	if (rec != nullptr) setCurrentIndex(static_cast<int>(*static_cast<const int32_t *>(rec)));
}

void MRSpinner::setState(ushort aState, Boolean enable) {
	const ushort old = state;

	TView::setState(aState, enable);
	if (old != state && (aState & (sfFocused | sfDisabled | sfSelected | sfActive))) drawView();
}

const std::string &MRSpinner::currentValue() const noexcept {
	return mValues[static_cast<std::size_t>(valueIndex)];
}

void MRSpinner::setCurrentValue(const std::string &value) noexcept {
	for (std::size_t i = 0; i < mValues.size(); ++i)
		if (mValues[i] == value) {
			setCurrentIndex(static_cast<int>(i));
			return;
		}
	setCurrentIndex(0);
}

void MRSpinner::setCurrentIndex(int index) noexcept {
	const int nextIndex = normalizedIndex(index);

	if (nextIndex == valueIndex) return;
	valueIndex = nextIndex;
	drawView();
}

int MRSpinner::normalizedIndex(int index) const noexcept {
	const int count = static_cast<int>(mValues.size());

	if (count <= 0) return 0;
	while (index < 0)
		index += count;
	while (index >= count)
		index -= count;
	return index;
}

int MRSpinner::displayWidth() const noexcept {
	int width = 1;

	for (const std::string &value : mValues)
		width = std::max<int>(width, static_cast<int>(std::strlen(value.c_str())));
	return width;
}

void MRSpinner::changeBy(int delta) noexcept {
	const int oldIndex = valueIndex;

	setCurrentIndex(valueIndex + delta);
	if (valueIndex != oldIndex) notifyChanged();
}

void MRSpinner::notifyChanged() noexcept {
	if (owner != nullptr) message(owner, evBroadcast, changedCmd, this);
}

void MRSpinner::drawLineText(int y, const char *text, TAttrPair color) {
	TDrawBuffer buffer;
	const int width = std::max<short>(0, size.x);
	const int textWidth = text != nullptr ? static_cast<int>(std::strlen(text)) : 0;
	const int x = std::max(0, (width - textWidth) / 2);

	buffer.moveChar(0, ' ', color, width);
	if (text != nullptr && width > 0) buffer.moveStr(static_cast<ushort>(x), text, color, static_cast<ushort>(std::max(0, width - x)));
	writeLine(0, static_cast<short>(y), size.x, 1, buffer);
}

TAttrPair MRSpinner::colorForSlot(unsigned char paletteSlot, ushort fallbackIndex, unsigned char fallbackAttr) {
	unsigned char color = 0;

	if (configuredColorSlotOverride(paletteSlot, color)) return TAttrPair(color);
	return owner != nullptr ? getColor(fallbackIndex) : TAttrPair(fallbackAttr);
}
