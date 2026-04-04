#define Uses_TWindow
#define Uses_TStaticText
#define Uses_TText
#define Uses_TDialog
#include "TMRFrame.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

static const unsigned char kInitFrame[19] = {0x06, 0x0A, 0x0C, 0x05, 0x00, 0x05, 0x03, 0x0A, 0x09, 0x16,
                                             0x1A, 0x1C, 0x15, 0x00, 0x15, 0x13, 0x1A, 0x19, 0x00};

static const char kFrameChars[33] = "   \xC0 \xB3\xDA\xC3 \xD9\xC4\xC1\xBF\xB4\xC2\xC5   \xC8 \xBA\xC9\xC7 "
                                    "\xBC\xCD\xCF\xBB\xB6\xD1 ";

static const char *kCloseIcon = "[~\xFE~]";
static const char *kZoomIcon = "[~\x18~]";
static const char *kUnZoomIcon = "[~\x12~]";
static const char *kDragIcon = "~\xC4\xD9~";
static const char *kDragLeftIcon = "~\xC0\xC4~";

static constexpr char kDirtyMarkerIcon[] = "✍";
static constexpr char kRecordingMarkerIcon[] = "📼";
static constexpr char kTaskMarkerIcon[] = "🧠";
static constexpr char kReadOnlyMarkerIcon[] = "🔒";
static constexpr char kInsertMarkerIcon[] = "✚";
static constexpr int kDirtyMarkerSlotWidth = 2;
static constexpr int kRecordingMarkerSlotWidth = 2;
static constexpr int kTaskMarkerSlotWidth = 2;
static constexpr int kReadOnlyMarkerSlotWidth = 2;
static constexpr int kInsertMarkerSlotWidth = 1;
static constexpr char kMarkerLeftBracket = '[';
static constexpr char kMarkerRightBracket = ']';
static constexpr int kMarkerGap = 0;

int markerWidth(TStringView icon) noexcept {
	return std::max(1, strwidth(icon));
}

int markerSpan(TStringView icon, int minWidth = 1) noexcept {
	return std::max(minWidth, markerWidth(icon));
}

int advanceMarkerX(int x, TStringView icon, int minWidth = 1) noexcept {
	return x + markerSpan(icon, minWidth) + kMarkerGap;
}

bool hasMarkerBlock(const TMRFrame::MarkerState &state) noexcept {
	return state.modified || state.insertMode || state.recording || state.background || state.readOnly;
}

bool isFrameFocused(const TMRFrame *frame) noexcept {
	const TView *owner = frame != nullptr ? frame->owner : nullptr;
	const TView *top =
	    frame != nullptr ? static_cast<const TView *>(const_cast<TMRFrame *>(frame)->TopView()) : nullptr;
	const TWindow *window = frame != nullptr ? static_cast<const TWindow *>(frame->owner) : nullptr;

	// During modal execView(), TopView points to the modal dialog.
	// Enforce a single visual focus owner: only the modal top view draws as active.
	if (top != nullptr && owner != nullptr && top->owner != nullptr && (top->state & sfModal) != 0)
		return top == owner;

	if (window != nullptr && (window->state & sfFocused) != 0)
		return true;
	return frame != nullptr && (frame->state & sfFocused) != 0;
}

} // namespace

TMRTaskOverviewView::TMRTaskOverviewView(const TRect &bounds) noexcept : TView(bounds) {
	eventMask = 0;
	options |= ofBuffered;
}

void TMRTaskOverviewView::setLines(const std::vector<std::string> &lines) {
	lines_ = lines;
	drawView();
}

void TMRTaskOverviewView::draw() {
	TDrawBuffer b;
	TColorAttr text = getColor(1);

	for (int y = 0; y < size.y; ++y) {
		b.moveChar(0, ' ', text, size.x);
		if (y < static_cast<int>(lines_.size())) {
			std::string textLine = lines_[static_cast<std::size_t>(y)];
			while (strwidth(textLine.c_str()) > std::max(0, size.x - 2) && !textLine.empty()) {
				std::size_t nextLen = TText::prev(textLine, textLine.size());
				if (nextLen == 0 || nextLen > textLine.size())
					break;
				textLine.resize(textLine.size() - nextLen);
			}
			for (char &ch : textLine)
				if (static_cast<unsigned char>(ch) < 32)
					ch = ' ';
			b.moveStr(1, textLine.c_str(), text);
		}
		writeBuf(0, y, size.x, 1, b);
	}
}

TPalette &TMRTaskOverviewView::getPalette() const {
	static TPalette palette("\x06", 1);
	return palette;
}

TMRTaskOverviewWindow::TMRTaskOverviewWindow(const TRect &bounds) noexcept
    : TWindowInit(&TWindow::initFrame), TWindow(bounds, "Tasks", wnNoNumber), content_(nullptr) {
	flags = 0;
	options &= ~(ofSelectable | ofTopSelect);
	eventMask = 0;
	palette = wpGrayWindow;
	TRect inner = getExtent();
	inner.grow(-1, -1);
	content_ = new TMRTaskOverviewView(inner);
	insert(content_);
}

void TMRTaskOverviewWindow::setLines(const std::vector<std::string> &lines) {
	if (content_ != nullptr)
		content_->setLines(lines);
}

TPalette &TMRTaskOverviewWindow::getPalette() const {
	static TPalette paletteGray("\x18\x18\x18\x18\x18\x18\x18\x18", 8);
	return paletteGray;
}

TMRFrame::TMRFrame(const TRect &bounds) noexcept
    : TFrame(bounds), taskOverviewPopup_(nullptr), taskOverviewPopupOwner_(nullptr) {
}

TMRFrame::~TMRFrame() {
	hideTaskOverview();
}

void TMRFrame::setMarkerStateProvider(MarkerStateProvider provider) {
	markerStateProvider_ = std::move(provider);
}

void TMRFrame::setTaskOverviewProvider(TaskOverviewProvider provider) {
	taskOverviewProvider_ = std::move(provider);
}

TMRFrame::MarkerState TMRFrame::markerState() const {
	if (markerStateProvider_)
		return markerStateProvider_();
	return MarkerState();
}

int TMRFrame::markerStartColumn() const noexcept {
	TWindow *window = static_cast<TWindow *>(owner);
	bool controlsVisible = isFrameFocused(this);
	if (window != nullptr && (window->flags & wfClose) != 0 && controlsVisible)
		return 7;
	return 2;
}

int TMRFrame::taskMarkerColumn(const MarkerState &state) const noexcept {
	int x = markerStartColumn();
	if (state.modified)
		x = advanceMarkerX(x, kDirtyMarkerIcon, kDirtyMarkerSlotWidth);
	if (state.insertMode)
		x = advanceMarkerX(x, kInsertMarkerIcon, kInsertMarkerSlotWidth);
	if (state.recording)
		x = advanceMarkerX(x, kRecordingMarkerIcon, kRecordingMarkerSlotWidth);
	if (state.background)
		return x;
	return -1;
}

int TMRFrame::markersEndColumn(const MarkerState &state) const noexcept {
	int x = markerStartColumn();
	if (state.modified)
		x = advanceMarkerX(x, kDirtyMarkerIcon, kDirtyMarkerSlotWidth);
	if (state.insertMode)
		x = advanceMarkerX(x, kInsertMarkerIcon, kInsertMarkerSlotWidth);
	if (state.recording)
		x = advanceMarkerX(x, kRecordingMarkerIcon, kRecordingMarkerSlotWidth);
	if (state.background)
		x = advanceMarkerX(x, kTaskMarkerIcon, kTaskMarkerSlotWidth);
	if (state.readOnly)
		x = advanceMarkerX(x, kReadOnlyMarkerIcon, kReadOnlyMarkerSlotWidth);
	return x;
}

void TMRFrame::drawFrameLine(TDrawBuffer &frameBuf, short y, short n, TColorAttr color) {
	if (size.x <= 0)
		return;

	std::vector<unsigned char> frameMask(static_cast<std::size_t>(size.x));
	frameMask[0] = kInitFrame[n];
	for (int x = 1; x < size.x - 1; ++x)
		frameMask[static_cast<std::size_t>(x)] = kInitFrame[n + 1];
	frameMask[static_cast<std::size_t>(size.x - 1)] = kInitFrame[n + 2];

	if (owner != nullptr && owner->last != nullptr) {
		TView *v = owner->last->next;
		for (; v != static_cast<TView *>(this); v = v->next) {
			if ((v->options & ofFramed) && (v->state & sfVisible)) {
				ushort mask = 0;
				if (y < v->origin.y) {
					if (y == v->origin.y - 1)
						mask = 0x0A06;
				} else if (y < v->origin.y + v->size.y)
					mask = 0x0005;
				else if (y == v->origin.y + v->size.y)
					mask = 0x0A03;

				if (mask) {
					int start = std::max<int>(v->origin.x, 1);
					int end = std::min<int>(v->origin.x + v->size.x, size.x - 1);
					if (start < end) {
						unsigned char maskLow = mask & 0x00FF;
						unsigned char maskHigh = (mask & 0xFF00) >> 8;
						frameMask[static_cast<std::size_t>(start - 1)] |= maskLow;
						frameMask[static_cast<std::size_t>(end)] |= maskLow ^ maskHigh;
						if (maskLow)
							for (int x = start; x < end; ++x)
								frameMask[static_cast<std::size_t>(x)] |= maskHigh;
					}
				}
			}
		}
	}

	for (int x = 0; x < size.x; ++x) {
		frameBuf.putChar(x, kFrameChars[frameMask[static_cast<std::size_t>(x)]]);
		frameBuf.putAttribute(x, color);
	}
}

void TMRFrame::draw() {
	TAttrPair cFrame, cTitle;
	short f;
	short width = size.x;
	TDrawBuffer b;
	MarkerState markers = markerState();
	TWindow *window = static_cast<TWindow *>(owner);
	bool isFocused = isFrameFocused(this);

	if ((this->state & sfDragging) != 0) {
		cFrame = 0x0505;
		cTitle = 0x0005;
		f = 0;
	} else if (!isFocused) {
		cFrame = 0x0101;
		cTitle = 0x0002;
		f = 0;
	} else {
		cFrame = 0x0503;
		cTitle = 0x0004;
		f = 9;
	}

	cFrame = getColor(cFrame);
	cTitle = getColor(cTitle);

	drawFrameLine(b, 0, f, cFrame);

	bool controlsVisible = isFocused;
	short titleReserveRight = width - 2;
	if (window != nullptr && window->number != wnNoNumber && window->number < 10) {
		short numberPos = (window->flags & wfZoom) != 0 ? width - 7 : width - 3;
		if (numberPos >= 1 && numberPos < width - 1)
			b.putChar(numberPos, window->number + '0');
		titleReserveRight = numberPos - 3;
	}

	if (controlsVisible && window != nullptr) {
		if ((window->flags & wfClose) != 0)
			b.moveCStr(2, kCloseIcon, cFrame);
		if ((window->flags & wfZoom) != 0) {
			TPoint minSize, maxSize;
			owner->sizeLimits(minSize, maxSize);
			if (owner->size == maxSize)
				b.moveCStr(width - 5, kUnZoomIcon, cFrame);
			else
				b.moveCStr(width - 5, kZoomIcon, cFrame);
			titleReserveRight = std::min<short>(titleReserveRight, width - 7);
		}
	}

	int markerX = markerStartColumn();
	bool showMarkerBlock = hasMarkerBlock(markers);
	if (showMarkerBlock) {
		int leftSepX = markerX - 1;
		int rightSepX = markersEndColumn(markers);
		if (leftSepX + 1 <= rightSepX - 1)
			b.moveChar(static_cast<ushort>(leftSepX + 1), ' ', cTitle, rightSepX - leftSepX - 1);
		if (leftSepX >= 1 && leftSepX < width - 1)
			b.putChar(static_cast<ushort>(leftSepX), kMarkerLeftBracket);
	}
	if (markers.modified) {
		int span = markerSpan(kDirtyMarkerIcon, kDirtyMarkerSlotWidth);
		b.moveChar(static_cast<ushort>(markerX), ' ', cTitle, span);
		b.moveStr(static_cast<ushort>(markerX), kDirtyMarkerIcon, cTitle, span);
		markerX = advanceMarkerX(markerX, kDirtyMarkerIcon, kDirtyMarkerSlotWidth);
	}
	if (markers.insertMode) {
		int span = markerSpan(kInsertMarkerIcon, kInsertMarkerSlotWidth);
		b.moveChar(static_cast<ushort>(markerX), ' ', cTitle, span);
		b.moveStr(static_cast<ushort>(markerX), kInsertMarkerIcon, cTitle, span);
		markerX = advanceMarkerX(markerX, kInsertMarkerIcon, kInsertMarkerSlotWidth);
	}
	if (markers.recording) {
		TColorAttr recordingColor = cTitle;
		setFore(recordingColor, TColorDesired(TColorRGB(0xFF, 0x55, 0x55)));
		setStyle(recordingColor, getStyle(recordingColor) | slBold);
		int span = markerSpan(kRecordingMarkerIcon, kRecordingMarkerSlotWidth);
		b.moveChar(static_cast<ushort>(markerX), ' ', cTitle, span);
		if (markers.recordingVisible)
			b.moveStr(static_cast<ushort>(markerX), kRecordingMarkerIcon, recordingColor, span);
		markerX = advanceMarkerX(markerX, kRecordingMarkerIcon, kRecordingMarkerSlotWidth);
	}
	if (markers.background) {
		TColorAttr taskColor = cTitle;
		setFore(taskColor, TColorDesired(TColorRGB(0xFF, 0x79, 0xC6)));
		setStyle(taskColor, getStyle(taskColor) | slBold);
		int span = markerSpan(kTaskMarkerIcon, kTaskMarkerSlotWidth);
		b.moveChar(static_cast<ushort>(markerX), ' ', cTitle, span);
		if (markers.backgroundVisible)
			b.moveStr(static_cast<ushort>(markerX), kTaskMarkerIcon, taskColor, span);
		markerX = advanceMarkerX(markerX, kTaskMarkerIcon, kTaskMarkerSlotWidth);
	}
	if (markers.readOnly) {
		int span = markerSpan(kReadOnlyMarkerIcon, kReadOnlyMarkerSlotWidth);
		b.moveChar(static_cast<ushort>(markerX), ' ', cTitle, span);
		if (markers.readOnlyVisible)
			b.moveStr(static_cast<ushort>(markerX), kReadOnlyMarkerIcon, cTitle, span);
		markerX = advanceMarkerX(markerX, kReadOnlyMarkerIcon, kReadOnlyMarkerSlotWidth);
	}
	if (showMarkerBlock) {
		int rightSepX = markerX;
		if (rightSepX >= 1 && rightSepX < width - 1)
			b.putChar(static_cast<ushort>(rightSepX), kMarkerRightBracket);
		markerX = rightSepX + 1;
	}

	if (window != nullptr) {
		int titleLeftLimit = std::max(markerX + (showMarkerBlock ? 1 : 0), 2);
		int available = titleReserveRight - titleLeftLimit + 1;
		int titleBudget = std::max(0, available - 4);
		const char *title = window->getTitle(titleBudget);
			if (title != nullptr && *title != '\0') {
				if (available > 0) {
					std::string decoratedTitle = "[";
					decoratedTitle += title;
					decoratedTitle += "]";
					int len = std::min<int>(strwidth(decoratedTitle.c_str()), available);
					int titleX = titleReserveRight - len + 1;
					b.moveStr(static_cast<ushort>(titleX), decoratedTitle.c_str(), cTitle, len);
				}
			}
	}

	writeLine(0, 0, size.x, 1, b);
	for (short i = 1; i <= size.y - 2; ++i) {
		drawFrameLine(b, i, f + 3, cFrame);
		writeLine(0, i, size.x, 1, b);
	}
	drawFrameLine(b, size.y - 1, f + 6, cFrame);
	if (isFocused && window != nullptr && (window->flags & wfGrow) != 0) {
		b.moveCStr(0, kDragLeftIcon, cFrame);
		b.moveCStr(width - 2, kDragIcon, cFrame);
	}
	writeLine(0, size.y - 1, size.x, 1, b);
}

void TMRFrame::dragWindow(TEvent &event, uchar mode) {
	TRect limits = owner->owner->getExtent();
	TPoint minSize, maxSize;
	owner->sizeLimits(minSize, maxSize);
	owner->dragView(event, owner->dragMode | mode, limits, minSize, maxSize);
	clearEvent(event);
}

void TMRFrame::handleEvent(TEvent &event) {
	TView::handleEvent(event);

	if (event.what == evMouseDown) {
		TPoint mouse = makeLocal(event.mouse.where);
		TWindow *window = static_cast<TWindow *>(owner);
		if (mouse.y == 0 && window != nullptr) {
			bool controlsVisible = isFrameFocused(this);
			if ((window->flags & wfClose) != 0 && controlsVisible && mouse.x >= 2 && mouse.x <= 4) {
				while (mouseEvent(event, evMouse))
					;
				mouse = makeLocal(event.mouse.where);
				if (mouse.y == 0 && mouse.x >= 2 && mouse.x <= 4) {
					event.what = evCommand;
					event.message.command = cmClose;
					event.message.infoPtr = owner;
					putEvent(event);
					clearEvent(event);
				}
			} else if ((window->flags & wfZoom) != 0 && controlsVisible &&
			           ((mouse.x >= size.x - 5 && mouse.x <= size.x - 3) ||
			            (event.mouse.eventFlags & meDoubleClick))) {
				event.what = evCommand;
				event.message.command = cmZoom;
				event.message.infoPtr = owner;
				putEvent(event);
				clearEvent(event);
			} else if ((window->flags & wfMove) != 0)
				dragWindow(event, dmDragMove);
		} else if (isFrameFocused(this) && (mouse.y >= size.y - 1) && window != nullptr &&
		           (window->flags & wfGrow)) {
			if (mouse.x >= size.x - 2)
				dragWindow(event, dmDragGrow);
			else if (mouse.x <= 1)
				dragWindow(event, dmDragGrowLeft);
		} else if (event.what == evMouseDown && event.mouse.buttons == mbMiddleButton && 0 < mouse.x &&
		           mouse.x < size.x - 1 && 0 < mouse.y && mouse.y < size.y - 1 && window != nullptr &&
		           (window->flags & wfMove))
			dragWindow(event, dmDragMove);
	}
}

void TMRFrame::setState(ushort aState, Boolean enable) {
	TView::setState(aState, enable);
	if ((aState & (sfActive | sfFocused | sfDragging)) != 0)
		drawView();
}

void TMRFrame::showTaskOverview() {
	TGroup *group = owner != nullptr ? owner->owner : nullptr;
	std::vector<std::string> lines;
	MarkerState state = markerState();
	int taskX = taskMarkerColumn(state);
	int width = 14;

	if (group == nullptr || !taskOverviewProvider_ || taskX < 0)
		return;
	lines = taskOverviewProvider_();
	if (lines.empty()) {
		hideTaskOverview();
		return;
	}
	for (const std::string &line : lines)
		if (strwidth(line.c_str()) + 4 > width)
			width = strwidth(line.c_str()) + 4;
	if (width > group->size.x - 2)
		width = std::max(12, group->size.x - 2);
	int height = static_cast<int>(lines.size()) + 2;
	if (height > group->size.y - 2)
		height = std::max(3, group->size.y - 2);
	TPoint topLeft = makeGlobal(TPoint(std::max(1, taskX - 1), 1));
	topLeft = group->makeLocal(topLeft);
	int left = std::max(1, topLeft.x);
	if (left + width > group->size.x - 1)
		left = std::max(1, group->size.x - 1 - width);
	int top = std::max(1, topLeft.y);
	if (top + height > group->size.y - 1)
		top = std::max(1, group->size.y - 1 - height);
	TRect bounds(left, top, left + width, top + height);

	if (taskOverviewPopup_ != nullptr) {
		if (taskOverviewPopupOwner_ != nullptr)
			taskOverviewPopupOwner_->remove(taskOverviewPopup_);
		TObject::destroy(taskOverviewPopup_);
		taskOverviewPopup_ = nullptr;
		taskOverviewPopupOwner_ = nullptr;
	}
	taskOverviewPopup_ = new TMRTaskOverviewWindow(bounds);
	taskOverviewPopup_->setLines(lines);
	group->insert(taskOverviewPopup_);
	taskOverviewPopupOwner_ = group;
	taskOverviewPopup_->makeFirst();
	taskOverviewPopup_->setState(sfActive, False);
}

void TMRFrame::hideTaskOverview() {
	if (taskOverviewPopup_ == nullptr)
		return;
	if (taskOverviewPopupOwner_ != nullptr)
		taskOverviewPopupOwner_->remove(taskOverviewPopup_);
	TObject::destroy(taskOverviewPopup_);
	taskOverviewPopup_ = nullptr;
	taskOverviewPopupOwner_ = nullptr;
}

void TMRFrame::updateTaskHover(TPoint globalMouse, bool forceHide) {
	MarkerState state = markerState();
	int taskX = taskMarkerColumn(state);

	if (forceHide || taskX < 0 || !taskOverviewProvider_) {
		hideTaskOverview();
		return;
	}
	if (!mouseInView(globalMouse)) {
		hideTaskOverview();
		return;
	}
	TPoint local = makeLocal(globalMouse);
	if (local.y != 0 || local.x < taskX || local.x >= taskX + markerSpan(kTaskMarkerIcon, kTaskMarkerSlotWidth)) {
		hideTaskOverview();
		return;
	}
	showTaskOverview();
}
