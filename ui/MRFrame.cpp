#define Uses_TWindow
#define Uses_TStaticText
#define Uses_TText
#define Uses_TDialog
#define Uses_TMenuItem
#include "MRFrame.hpp"
#include "MRBentoBox.hpp"
#include "MREditWindow.hpp"
#include "MRWindowManager.hpp"
#include "../app/MRMenuFactory.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <vector>

void mrvmUiInvalidateScreenBase() noexcept;

namespace {

static const unsigned char kInitFrame[19] = {0x06, 0x0A, 0x0C, 0x05, 0x00, 0x05, 0x03, 0x0A, 0x09, 0x16, 0x1A, 0x1C, 0x15, 0x00, 0x15, 0x13, 0x1A, 0x19, 0x00};

static const char kFrameChars[33] = "   \xC0 \xB3\xDA\xC3 \xD9\xC4\xC1\xBF\xB4\xC2\xC5   \xC8 \xBA\xC9\xC7 "
                                    "\xBC\xCD\xCF\xBB\xB6\xD1 ";

static const char *kCloseIcon = "[\xFE]";
static const char *kZoomIcon = "[▴]";
static const char *kUnZoomIcon = "[▴]";
static const char *kMinimizeIcon = "[▾]";
static const char *kDragIcon = "\xC4\xD9";
static const char *kDragLeftIcon = "\xC0\xC4";
static const char *kFocusedDragIcon = "\xCD\xBC";
static const char *kFocusedDragLeftIcon = "\xC8\xCD";

static constexpr char kDirtyMarkerIcon[] = "✎";
static constexpr char kRecordingMarkerIcon[] = "📼";
static constexpr char kActivityMarkerIcon[] = "⌬";
static constexpr const char *kTaskMarkerIcon = kActivityMarkerIcon;
static constexpr const char *kMacroBrainMarkerIcon = kActivityMarkerIcon;
static constexpr char kWorkspaceMainFileMarkerIcon[] = "♔";
static constexpr char kReadOnlyMarkerIcon[] = "⚿";
static constexpr char kInsertMarkerIcon[] = "⌶";
static constexpr char kWordWrapMarkerIcon[] = "┊↵┊"; // ┆↵┆
static constexpr int kLanguageMarkerSlotWidth = 1;
static constexpr int kDirtyMarkerSlotWidth = 2;
static constexpr int kRecordingMarkerSlotWidth = 2;
static constexpr int kTaskMarkerSlotWidth = 2;
static constexpr int kMacroBrainMarkerSlotWidth = 2;
static constexpr int kWorkspaceMainFileMarkerSlotWidth = 1;
static constexpr int kReadOnlyMarkerSlotWidth = 2;
static constexpr int kInsertMarkerSlotWidth = 1;
static constexpr int kWordWrapMarkerSlotWidth = 3;
static constexpr char kMarkerLeftBracket = '[';
static constexpr char kMarkerRightBracket = ']';
static constexpr int kMarkerGap = 1;
static constexpr int kNormalControlGap = 1;
static constexpr int kNormalRightPadding = 1;
static constexpr unsigned char kFrameMarkerHintColorSlot = 134;
static constexpr auto kTaskOverviewRefreshInterval = std::chrono::milliseconds(250);

int markerWidth(TStringView icon) noexcept {
	return std::max(1, strwidth(icon));
}

int markerSpan(TStringView icon, int minWidth = 1) noexcept {
	return std::max(minWidth, markerWidth(icon));
}

int advanceMarkerX(int x, TStringView icon, int minWidth = 1) noexcept {
	return x + markerSpan(icon, minWidth) + kMarkerGap;
}

int normalControlWidth(TStringView icon) noexcept {
	return std::max(0, strwidth(icon));
}

int normalRightControlStart(int width, TStringView icon) noexcept {
	return width - 1 - kNormalRightPadding - normalControlWidth(icon);
}

int normalZoomStart(int width, bool hasMinimizeButton) noexcept {
	const int zoomWidth = normalControlWidth(kZoomIcon);
	if (!hasMinimizeButton) return normalRightControlStart(width, kZoomIcon);
	return normalRightControlStart(width, kMinimizeIcon) - kNormalControlGap - zoomWidth;
}

bool hasMarkerBlock(const MRFrame::MarkerState &state) noexcept {
	return state.modified || state.insertMode || state.wordWrap || state.language || state.workspaceMainFile || state.recording || state.macroBrain || state.background || state.readOnly;
}

bool isFrameFocused(const MRFrame *frame) noexcept {
	const TView *owner = frame != nullptr ? frame->owner : nullptr;
	const TView *top = frame != nullptr ? static_cast<const TView *>(const_cast<MRFrame *>(frame)->TopView()) : nullptr;
	const TWindow *window = frame != nullptr ? static_cast<const TWindow *>(frame->owner) : nullptr;

	// During modal execView(), TopView points to the modal dialog.
	// Enforce a single visual focus owner: only the modal top view draws as active.
	if (top != nullptr && owner != nullptr && top->owner != nullptr && (top->state & sfModal) != 0) return top == owner;

	if (window != nullptr && (window->state & sfFocused) != 0) return true;
	return frame != nullptr && (frame->state & sfFocused) != 0;
}

TColorAttr frameMarkerHintColor() {
	unsigned char configured = 0;

	if (configuredColorSlotOverride(kFrameMarkerHintColorSlot, configured)) return static_cast<TColorAttr>(configured);
	return 0x70;
}

} // namespace

MRTaskOverviewView::MRTaskOverviewView(const TRect &bounds) noexcept : TView(bounds) {
	eventMask = 0;
	options |= ofBuffered;
}

void MRTaskOverviewView::setLines(const std::vector<std::string> &lines) {
	if (mLines == lines) return;
	mLines = lines;
	drawView();
}

void MRTaskOverviewView::draw() {
	TDrawBuffer b;
	TColorAttr text = getColor(1);

	for (int y = 0; y < size.y; ++y) {
		b.moveChar(0, ' ', text, size.x);
		if (y < static_cast<int>(mLines.size())) {
			std::string textLine = mLines[static_cast<std::size_t>(y)];
			while (strwidth(textLine.c_str()) > std::max(0, size.x - 2) && !textLine.empty()) {
				std::size_t nextLen = TText::prev(textLine, textLine.size());
				if (nextLen == 0 || nextLen > textLine.size()) break;
				textLine.resize(textLine.size() - nextLen);
			}
			for (char &ch : textLine)
				if (static_cast<unsigned char>(ch) < 32) ch = ' ';
			b.moveStr(1, textLine.c_str(), text);
		}
		writeBuf(0, y, size.x, 1, b);
	}
	mrvmUiInvalidateScreenBase();
}

TPalette &MRTaskOverviewView::getPalette() const {
	static TPalette palette("\x06", 1);
	return palette;
}

MRTaskOverviewWindow::MRTaskOverviewWindow(const TRect &bounds, MRFrame *frame) noexcept : TWindowInit(&TWindow::initFrame), TWindow(bounds, "Tasks", wnNoNumber), mContent(nullptr), mFrame(frame) {
	flags = 0;
	options &= ~(ofSelectable | ofTopSelect);
	eventMask = evMouseDown;
	palette = wpGrayWindow;
	TRect inner = getExtent();
	inner.grow(-1, -1);
	mContent = new MRTaskOverviewView(inner);
	insert(mContent);
}

void MRTaskOverviewWindow::handleEvent(TEvent &event) {
	if (event.what == evMouseDown) {
		MRFrame *frame = mFrame;
		clearEvent(event);
		if (frame != nullptr) frame->closeTaskOverview();
		return;
	}
}

void MRTaskOverviewWindow::setLines(const std::vector<std::string> &lines) {
	if (mContent != nullptr) mContent->setLines(lines);
}

TPalette &MRTaskOverviewWindow::getPalette() const {
	static TPalette paletteGray("\x18\x18\x18\x18\x18\x18\x18\x18", 8);
	return paletteGray;
}

class MRFrameMarkerHintView : public TView {
  public:
	MRFrameMarkerHintView(const TRect &bounds, MRFrame *frame, std::string text) noexcept : TView(bounds), mFrame(frame), mText(std::move(text)) {
		options &= ~(ofSelectable | ofTopSelect);
		eventMask = evMouseDown;
	}

	virtual void draw() override {
		TDrawBuffer buffer;
		const TColorAttr color = frameMarkerHintColor();

		buffer.moveChar(0, ' ', color, size.x);
		buffer.moveStr(0, mText.c_str(), color, static_cast<ushort>(size.x));
		writeLine(0, 0, size.x, 1, buffer);
		mrvmUiInvalidateScreenBase();
	}

	virtual void handleEvent(TEvent &event) override {
		if (event.what == evMouseDown) {
			MRFrame *frame = mFrame;
			clearEvent(event);
			if (frame != nullptr) frame->updateTaskHover(TPoint(), true);
		}
	}

  private:
	MRFrame *mFrame;
	std::string mText;
};

MRFrame::MRFrame(const TRect &bounds) noexcept : TFrame(bounds), mTaskOverviewPopup(nullptr), mTaskOverviewPopupOwner(nullptr), mTaskOverviewLastRefresh(std::chrono::steady_clock::time_point()), mTaskOverviewPinned(false), mMarkerHintPopup(nullptr), mMarkerHintPopupOwner(nullptr), mMarkerHintText(), mMarkerHintColumn(-1) {
}

MRFrame::~MRFrame() {
	hideMarkerHint();
	hideTaskOverview();
}

void MRFrame::setMarkerStateProvider(MarkerStateProvider provider) {
	mMarkerStateProvider = std::move(provider);
}

void MRFrame::setTaskOverviewProvider(TaskOverviewProvider provider) {
	mTaskOverviewProvider = std::move(provider);
}

MRFrame::MarkerState MRFrame::markerState() const {
	if (mMarkerStateProvider) return mMarkerStateProvider();
	return MarkerState();
}

int MRFrame::markerStartColumn() const noexcept {
	TWindow *window = static_cast<TWindow *>(owner);
	bool controlsVisible = isFrameFocused(this);
	if (window != nullptr && (window->flags & wfClose) != 0 && controlsVisible) return 7;
	return 2;
}

int MRFrame::taskMarkerColumn(const MarkerState &state) const noexcept {
	int x = markerStartColumn();
	if (state.modified) x = advanceMarkerX(x, kDirtyMarkerIcon, kDirtyMarkerSlotWidth);
	if (state.insertMode) x = advanceMarkerX(x, kInsertMarkerIcon, kInsertMarkerSlotWidth);
	if (state.wordWrap) x = advanceMarkerX(x, kWordWrapMarkerIcon, kWordWrapMarkerSlotWidth);
	if (state.language) x = advanceMarkerX(x, state.languageMarker != nullptr ? state.languageMarker : "", kLanguageMarkerSlotWidth);
	if (state.workspaceMainFile) x = advanceMarkerX(x, kWorkspaceMainFileMarkerIcon, kWorkspaceMainFileMarkerSlotWidth);
	if (state.recording) x = advanceMarkerX(x, kRecordingMarkerIcon, kRecordingMarkerSlotWidth);
	if (state.macroBrain) x = advanceMarkerX(x, kMacroBrainMarkerIcon, kMacroBrainMarkerSlotWidth);
	if (state.background) return x;
	return -1;
}

int MRFrame::markersEndColumn(const MarkerState &state) const noexcept {
	int x = markerStartColumn();
	bool hasMarkers = false;
	if (state.modified) x = advanceMarkerX(x, kDirtyMarkerIcon, kDirtyMarkerSlotWidth), hasMarkers = true;
	if (state.insertMode) x = advanceMarkerX(x, kInsertMarkerIcon, kInsertMarkerSlotWidth), hasMarkers = true;
	if (state.wordWrap) x = advanceMarkerX(x, kWordWrapMarkerIcon, kWordWrapMarkerSlotWidth), hasMarkers = true;
	if (state.language) x = advanceMarkerX(x, state.languageMarker != nullptr ? state.languageMarker : "", kLanguageMarkerSlotWidth), hasMarkers = true;
	if (state.workspaceMainFile) x = advanceMarkerX(x, kWorkspaceMainFileMarkerIcon, kWorkspaceMainFileMarkerSlotWidth), hasMarkers = true;
	if (state.recording) x = advanceMarkerX(x, kRecordingMarkerIcon, kRecordingMarkerSlotWidth), hasMarkers = true;
	if (state.macroBrain) x = advanceMarkerX(x, kMacroBrainMarkerIcon, kMacroBrainMarkerSlotWidth), hasMarkers = true;
	if (state.background) x = advanceMarkerX(x, kTaskMarkerIcon, kTaskMarkerSlotWidth), hasMarkers = true;
	if (state.readOnly) x = advanceMarkerX(x, kReadOnlyMarkerIcon, kReadOnlyMarkerSlotWidth), hasMarkers = true;
	return hasMarkers ? x - kMarkerGap : x;
}

bool MRFrame::taskMarkerHit(TPoint localMouse, const MarkerState &state) const noexcept {
	int taskX = taskMarkerColumn(state);

	return taskX >= 0 && localMouse.y == 0 && localMouse.x >= taskX && localMouse.x < taskX + markerSpan(kTaskMarkerIcon, kTaskMarkerSlotWidth);
}

bool MRFrame::markerHintAt(TPoint localMouse, const MarkerState &state, std::string &text, int &column) const {
	struct Candidate {
		bool active;
		const char *icon;
		int minWidth;
		const char *hint;
		bool taskOverview;
	};
	std::string languageHint;
	const char *languageMarker = state.languageMarker != nullptr ? state.languageMarker : "";
	if (state.language) languageHint = std::string("▲ Language: ") + languageMarker;
	const Candidate candidates[] = {
	    {state.modified, kDirtyMarkerIcon, kDirtyMarkerSlotWidth, "▲ Changed", false},
	    {state.insertMode, kInsertMarkerIcon, kInsertMarkerSlotWidth, "▲ Insert mode", false},
	    {state.wordWrap, kWordWrapMarkerIcon, kWordWrapMarkerSlotWidth, "▲ Word wrap", false},
	    {state.language, languageMarker, kLanguageMarkerSlotWidth, languageHint.c_str(), false},
	    {state.workspaceMainFile, kWorkspaceMainFileMarkerIcon, kWorkspaceMainFileMarkerSlotWidth, "▲ Main workspace file", false},
	    {state.recording, kRecordingMarkerIcon, kRecordingMarkerSlotWidth, "▲ Macro recording", false},
	    {state.macroBrain, kMacroBrainMarkerIcon, kMacroBrainMarkerSlotWidth, "▲ Macro activity", false},
	    {state.background, kTaskMarkerIcon, kTaskMarkerSlotWidth, "", true},
	    {state.readOnly, kReadOnlyMarkerIcon, kReadOnlyMarkerSlotWidth, "▲ Read only", false},
	};
	int x = markerStartColumn();

	text.clear();
	column = -1;
	if (localMouse.y != 0) return false;
	for (const Candidate &candidate : candidates) {
		if (!candidate.active) continue;
		const int span = markerSpan(candidate.icon, candidate.minWidth);
		if (localMouse.x >= x && localMouse.x < x + span) {
			if (candidate.taskOverview) return false;
			text = candidate.hint;
			column = x;
			return !text.empty();
		}
		x = advanceMarkerX(x, candidate.icon, candidate.minWidth);
	}
	return false;
}

void MRFrame::drawFrameLine(TDrawBuffer &frameBuf, short y, short n, TColorAttr color) {
	if (size.x <= 0) return;

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
					if (y == v->origin.y - 1) mask = 0x0A06;
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

void MRFrame::draw() {
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
	if (window != nullptr) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);
		unsigned char fileCompareBentoBorder = 0;
		unsigned char fileCompareBentoBorderBold = 0;

		if (bentoBox != nullptr && bentoBox->isFileCompareBox()) {
			if (configuredColorSlotOverride(kMrPaletteFileCompareBentoBorder, fileCompareBentoBorder)) cFrame = static_cast<TColorAttr>(fileCompareBentoBorder);
			if (configuredColorSlotOverride(kMrPaletteFileCompareBentoBorderBold, fileCompareBentoBorderBold)) cTitle = static_cast<TColorAttr>(fileCompareBentoBorderBold);
		}
	}

	MREditWindow *editWindow = dynamic_cast<MREditWindow *>(window);
	if (editWindow != nullptr && MRWindowManager::isWindowMinimized(editWindow)) {
		const MRWindowManager::MinimizedGlyphs &glyphs = MRWindowManager::minimizedGlyphs();
		const MRWindowManager::MinimizedLayout layout = MRWindowManager::minimizedLayout(editWindow, width);
		const char *title = MRWindowManager::minimizedDisplayTitle(editWindow);

		b.moveChar(0, ' ', cTitle, size.x);
		if (layout.menuEnd > layout.menuStart) b.moveStr(static_cast<ushort>(layout.menuStart), glyphs.menu, cTitle, layout.menuEnd - layout.menuStart);
		if (layout.menuEnd < width) b.putChar(static_cast<ushort>(layout.menuEnd), '[');
		if (title != nullptr && layout.titleEnd > layout.titleStart) {
			b.moveStr(static_cast<ushort>(layout.titleStart), title, cTitle, layout.titleEnd - layout.titleStart);
		}
		if (layout.titleEnd < layout.restoreStart) b.putChar(static_cast<ushort>(layout.titleEnd), ']');
		if (layout.restoreEnd > layout.restoreStart) b.moveStr(static_cast<ushort>(layout.restoreStart), glyphs.restore, cTitle, layout.restoreEnd - layout.restoreStart);
		if (layout.reinsertEnd > layout.reinsertStart) b.moveStr(static_cast<ushort>(layout.reinsertStart), glyphs.reinsert, cTitle, layout.reinsertEnd - layout.reinsertStart);
		writeLine(0, 0, size.x, 1, b);
		mrvmUiInvalidateScreenBase();
		return;
	}

	drawFrameLine(b, 0, f, cFrame);

	bool controlsVisible = isFocused;
	const bool hasZoomButton = window != nullptr && (window->flags & wfZoom) != 0;
	const bool hasMinimizeButton = editWindow != nullptr;
	const int minimizeStart = hasMinimizeButton ? normalRightControlStart(width, kMinimizeIcon) : width;
	const int zoomStart = hasZoomButton ? normalZoomStart(width, hasMinimizeButton) : width;
	const int controlClusterStart = hasZoomButton ? zoomStart : minimizeStart;
	short titleReserveRight = width - 2;

	if (controlsVisible && window != nullptr) {
		if ((window->flags & wfClose) != 0) b.moveCStr(2, kCloseIcon, cFrame);
		if (hasZoomButton) {
			TPoint minSize, maxSize;
			owner->sizeLimits(minSize, maxSize);
			if (owner->size == maxSize) b.moveStr(static_cast<ushort>(zoomStart), kUnZoomIcon, cFrame, normalControlWidth(kUnZoomIcon));
			else
				b.moveStr(static_cast<ushort>(zoomStart), kZoomIcon, cFrame, normalControlWidth(kZoomIcon));
		}
		if (hasMinimizeButton) b.moveStr(static_cast<ushort>(minimizeStart), kMinimizeIcon, cFrame, normalControlWidth(kMinimizeIcon));
		if (hasMinimizeButton || hasZoomButton) titleReserveRight = std::min<short>(titleReserveRight, static_cast<short>(controlClusterStart - 2));
	}

	int markerX = markerStartColumn();
	bool showMarkerBlock = hasMarkerBlock(markers);
	if (showMarkerBlock) {
		int leftSepX = markerX - 1;
		int rightSepX = markersEndColumn(markers);
		if (leftSepX + 1 <= rightSepX - 1) b.moveChar(static_cast<ushort>(leftSepX + 1), ' ', cTitle, rightSepX - leftSepX - 1);
		if (leftSepX >= 1 && leftSepX < width - 1) b.putChar(static_cast<ushort>(leftSepX), kMarkerLeftBracket);
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
		if (markers.insertModeVisible) b.moveStr(static_cast<ushort>(markerX), kInsertMarkerIcon, cTitle, span);
		markerX = advanceMarkerX(markerX, kInsertMarkerIcon, kInsertMarkerSlotWidth);
	}
	if (markers.wordWrap) {
		int span = markerSpan(kWordWrapMarkerIcon, kWordWrapMarkerSlotWidth);
		b.moveChar(static_cast<ushort>(markerX), ' ', cTitle, span);
		if (markers.wordWrapVisible) b.moveStr(static_cast<ushort>(markerX), kWordWrapMarkerIcon, cTitle, span);
		markerX = advanceMarkerX(markerX, kWordWrapMarkerIcon, kWordWrapMarkerSlotWidth);
	}
	if (markers.language) {
		const char *languageMarker = markers.languageMarker != nullptr ? markers.languageMarker : "";
		int span = markerSpan(languageMarker, kLanguageMarkerSlotWidth);
		b.moveChar(static_cast<ushort>(markerX), ' ', cTitle, span);
		if (markers.languageVisible) b.moveStr(static_cast<ushort>(markerX), languageMarker, cTitle, span);
		markerX = advanceMarkerX(markerX, languageMarker, kLanguageMarkerSlotWidth);
	}
	if (markers.workspaceMainFile) {
		int span = markerSpan(kWorkspaceMainFileMarkerIcon, kWorkspaceMainFileMarkerSlotWidth);
		b.moveChar(static_cast<ushort>(markerX), ' ', cTitle, span);
		if (markers.workspaceMainFileVisible) b.moveStr(static_cast<ushort>(markerX), kWorkspaceMainFileMarkerIcon, cTitle, span);
		markerX = advanceMarkerX(markerX, kWorkspaceMainFileMarkerIcon, kWorkspaceMainFileMarkerSlotWidth);
	}
	if (markers.recording) {
		TColorAttr recordingColor = cTitle;
		setFore(recordingColor, TColorDesired(TColorRGB(0xFF, 0x55, 0x55)));
		setStyle(recordingColor, getStyle(recordingColor) | slBold);
		int span = markerSpan(kRecordingMarkerIcon, kRecordingMarkerSlotWidth);
		b.moveChar(static_cast<ushort>(markerX), ' ', cTitle, span);
		if (markers.recordingVisible) b.moveStr(static_cast<ushort>(markerX), kRecordingMarkerIcon, recordingColor, span);
		markerX = advanceMarkerX(markerX, kRecordingMarkerIcon, kRecordingMarkerSlotWidth);
	}
	if (markers.macroBrain) {
		int span = markerSpan(kMacroBrainMarkerIcon, kMacroBrainMarkerSlotWidth);
		b.moveChar(static_cast<ushort>(markerX), ' ', cTitle, span);
		if (markers.macroBrainVisible) b.moveStr(static_cast<ushort>(markerX), kMacroBrainMarkerIcon, cTitle, span);
		markerX = advanceMarkerX(markerX, kMacroBrainMarkerIcon, kMacroBrainMarkerSlotWidth);
	}
	if (markers.background) {
		int span = markerSpan(kTaskMarkerIcon, kTaskMarkerSlotWidth);
		b.moveChar(static_cast<ushort>(markerX), ' ', cTitle, span);
		if (markers.backgroundVisible) b.moveStr(static_cast<ushort>(markerX), kTaskMarkerIcon, cTitle, span);
		markerX = advanceMarkerX(markerX, kTaskMarkerIcon, kTaskMarkerSlotWidth);
	}
	if (markers.readOnly) {
		int span = markerSpan(kReadOnlyMarkerIcon, kReadOnlyMarkerSlotWidth);
		b.moveChar(static_cast<ushort>(markerX), ' ', cTitle, span);
		if (markers.readOnlyVisible) b.moveStr(static_cast<ushort>(markerX), kReadOnlyMarkerIcon, cTitle, span);
		markerX = advanceMarkerX(markerX, kReadOnlyMarkerIcon, kReadOnlyMarkerSlotWidth);
	}
	if (showMarkerBlock) {
		int rightSepX = markerX > markerStartColumn() ? markerX - kMarkerGap : markerX;
		if (rightSepX >= 1 && rightSepX < width - 1) b.putChar(static_cast<ushort>(rightSepX), kMarkerRightBracket);
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
	const bool showGrowHandle = editWindow == nullptr || editWindow->showsFrameGrowHandle();
	if (isFocused && window != nullptr && (window->flags & wfGrow) != 0 && showGrowHandle) {
		b.moveCStr(0, f == 9 ? kFocusedDragLeftIcon : kDragLeftIcon, cFrame);
		b.moveCStr(width - 2, f == 9 ? kFocusedDragIcon : kDragIcon, cFrame);
	}
	writeLine(0, size.y - 1, size.x, 1, b);
	if (mTaskOverviewPopup != nullptr && mTaskOverviewProvider) {
		std::vector<std::string> lines = mTaskOverviewProvider();
		if (!lines.empty()) mTaskOverviewPopup->setLines(lines);
	}
	mrvmUiInvalidateScreenBase();
}

void MRFrame::dragWindow(TEvent &event, uchar mode) {
	TRect limits = owner->owner->getExtent();
	TPoint minSize, maxSize;
	owner->sizeLimits(minSize, maxSize);
	owner->dragView(event, owner->dragMode | mode, limits, minSize, maxSize);
	clearEvent(event);
}

void MRFrame::handleEvent(TEvent &event) {
	TView::handleEvent(event);

	if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEsc && mTaskOverviewPinned) {
		hideTaskOverview();
		clearEvent(event);
		return;
	}
	if (event.what == evMouseDown) {
		TPoint mouse = makeLocal(event.mouse.where);
		TWindow *window = static_cast<TWindow *>(owner);
		MREditWindow *editWindow = dynamic_cast<MREditWindow *>(window);
		if (mouse.y == 0 && window != nullptr) {
			if (editWindow != nullptr && MRWindowManager::isWindowMinimized(editWindow)) {
				const MRWindowManager::MinimizedLayout layout = MRWindowManager::minimizedLayout(editWindow, size.x);
				if (mouse.x >= layout.menuStart && mouse.x < layout.menuEnd) {
					TMenuItem *items = createMRWindowMenuPopupItems();
					if (items != nullptr) popupMenu(event.mouse.where, *items, owner);
					clearEvent(event);
				} else if (MRWindowManager::isMinimizedReinsertGlyphHit(editWindow, mouse)) {
					event.what = evCommand;
					event.message.command = cmResize;
					event.message.infoPtr = owner;
					putEvent(event);
					clearEvent(event);
				} else if ((event.mouse.eventFlags & meDoubleClick) != 0) {
					event.what = evCommand;
					event.message.command = cmMrWindowRestore;
					event.message.infoPtr = owner;
					putEvent(event);
					clearEvent(event);
				} else if (MRWindowManager::isMinimizedRestoreGlyphHit(editWindow, mouse)) {
					event.what = evCommand;
					event.message.command = cmMrWindowMinimize;
					event.message.infoPtr = owner;
					putEvent(event);
					clearEvent(event);
				} else if ((window->flags & wfMove) != 0)
					dragWindow(event, dmDragMove);
				return;
			}
			bool controlsVisible = isFrameFocused(this);
			const bool hasZoomButton = (window->flags & wfZoom) != 0;
			const bool hasMinimizeButton = editWindow != nullptr;
			const int minimizeStart = hasMinimizeButton ? normalRightControlStart(size.x, kMinimizeIcon) : size.x;
			const int zoomStart = normalZoomStart(size.x, hasMinimizeButton);
			MarkerState state = markerState();
			if (taskMarkerHit(mouse, state) && mTaskOverviewProvider) {
				if (mTaskOverviewPopup != nullptr && mTaskOverviewPinned) hideTaskOverview();
				else {
					mTaskOverviewPinned = true;
					showTaskOverview();
				}
				clearEvent(event);
			} else if ((window->flags & wfClose) != 0 && controlsVisible && mouse.x >= 2 && mouse.x <= 4) {
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
			} else if (controlsVisible && hasMinimizeButton && mouse.x >= minimizeStart && mouse.x < minimizeStart + normalControlWidth(kMinimizeIcon)) {
				event.what = evCommand;
				event.message.command = cmMrWindowMinimize;
				event.message.infoPtr = owner;
				putEvent(event);
				clearEvent(event);
			} else if (hasZoomButton && controlsVisible && ((mouse.x >= zoomStart && mouse.x < zoomStart + normalControlWidth(kZoomIcon)) || (event.mouse.eventFlags & meDoubleClick))) {
				event.what = evCommand;
				event.message.command = cmZoom;
				event.message.infoPtr = owner;
				putEvent(event);
				clearEvent(event);
			} else if ((window->flags & wfMove) != 0)
				dragWindow(event, dmDragMove);
		} else if (isFrameFocused(this) && (mouse.y >= size.y - 1) && window != nullptr && (window->flags & wfGrow)) {
			if (mouse.x >= size.x - 2) dragWindow(event, dmDragGrow);
			else if (mouse.x <= 1)
				dragWindow(event, dmDragGrowLeft);
		} else if (event.what == evMouseDown && event.mouse.buttons == mbMiddleButton && 0 < mouse.x && mouse.x < size.x - 1 && 0 < mouse.y && mouse.y < size.y - 1 && window != nullptr && (window->flags & wfMove))
			dragWindow(event, dmDragMove);
	}
}

void MRFrame::setState(ushort aState, Boolean enable) {
	TView::setState(aState, enable);
	if ((aState & (sfActive | sfFocused | sfDragging)) != 0) drawView();
}

void MRFrame::showMarkerHint(const std::string &text, int markerColumn) {
	TGroup *group = owner != nullptr ? owner->owner : nullptr;
	std::string displayText = text;

	if (group == nullptr || displayText.empty()) return;
	displayText.push_back(' ');
	if (mMarkerHintPopup != nullptr && mMarkerHintText == displayText && mMarkerHintColumn == markerColumn) return;

	const int width = std::max(1, strwidth(displayText.c_str()));
	TPoint topLeft = makeGlobal(TPoint(markerColumn, 1));
	topLeft = group->makeLocal(topLeft);
	int left = topLeft.x;
	int top = topLeft.y;

	if (left + width > group->size.x - 1) left = std::max(0, group->size.x - width);
	if (left < 0) left = 0;
	if (top >= group->size.y) top = std::max(0, group->size.y - 1);
	if (top < 0) top = 0;

	hideMarkerHint();
	mMarkerHintPopup = new MRFrameMarkerHintView(TRect(left, top, left + width, top + 1), this, displayText);
	group->insert(mMarkerHintPopup);
	mMarkerHintPopupOwner = group;
	mMarkerHintText = displayText;
	mMarkerHintColumn = markerColumn;
	mMarkerHintPopup->makeFirst();
	mMarkerHintPopup->setState(sfActive, False);
	mMarkerHintPopup->drawView();
}

void MRFrame::hideMarkerHint() {
	if (mMarkerHintPopup == nullptr) return;
	if (mMarkerHintPopupOwner != nullptr) mMarkerHintPopupOwner->remove(mMarkerHintPopup);
	TObject::destroy(mMarkerHintPopup);
	mMarkerHintPopup = nullptr;
	mMarkerHintPopupOwner = nullptr;
	mMarkerHintText.clear();
	mMarkerHintColumn = -1;
}

void MRFrame::showTaskOverview() {
	TGroup *group = owner != nullptr ? owner->owner : nullptr;
	std::vector<std::string> lines;
	MarkerState state = markerState();
	int taskX = taskMarkerColumn(state);
	int width = 56;
	int minWidth = 56;
	int minHeight = 10;

	if (group == nullptr || !mTaskOverviewProvider || taskX < 0) return;
	lines = mTaskOverviewProvider();
	if (lines.empty()) {
		hideTaskOverview();
		return;
	}
	for (const std::string &line : lines)
		if (strwidth(line.c_str()) + 4 > width) width = strwidth(line.c_str()) + 4;
	if (group->size.x > 4) minWidth = std::min(minWidth, group->size.x - 2);
	width = std::max(width, minWidth);
	if (width > group->size.x - 2) width = std::max(12, group->size.x - 2);
	int height = std::max(minHeight, static_cast<int>(lines.size()) + 2);
	if (height > group->size.y - 2) height = std::max(3, group->size.y - 2);
	TPoint topLeft = makeGlobal(TPoint(std::max(1, taskX - 1), 1));
	topLeft = group->makeLocal(topLeft);
	int left = std::max(1, topLeft.x);
	if (left + width > group->size.x - 1) left = std::max(1, group->size.x - 1 - width);
	int top = std::max(1, topLeft.y);
	if (top + height > group->size.y - 1) top = std::max(1, group->size.y - 1 - height);
	TRect bounds(left, top, left + width, top + height);

	if (mTaskOverviewPopup != nullptr) {
		if (mTaskOverviewPopupOwner != nullptr) mTaskOverviewPopupOwner->remove(mTaskOverviewPopup);
		TObject::destroy(mTaskOverviewPopup);
		mTaskOverviewPopup = nullptr;
		mTaskOverviewPopupOwner = nullptr;
	}
	mTaskOverviewPopup = new MRTaskOverviewWindow(bounds, this);
	mTaskOverviewPopup->setLines(lines);
	group->insert(mTaskOverviewPopup);
	mTaskOverviewPopupOwner = group;
	mTaskOverviewLastRefresh = std::chrono::steady_clock::now();
	mTaskOverviewPopup->makeFirst();
	mTaskOverviewPopup->setState(sfActive, False);
}

void MRFrame::hideTaskOverview() {
	if (mTaskOverviewPopup == nullptr) return;
	if (mTaskOverviewPopupOwner != nullptr) mTaskOverviewPopupOwner->remove(mTaskOverviewPopup);
	TObject::destroy(mTaskOverviewPopup);
	mTaskOverviewPopup = nullptr;
	mTaskOverviewPopupOwner = nullptr;
	mTaskOverviewLastRefresh = std::chrono::steady_clock::time_point();
	mTaskOverviewPinned = false;
}

void MRFrame::updateTaskHover(TPoint globalMouse, bool forceHide) {
	MarkerState state = markerState();
	int taskX = taskMarkerColumn(state);
	std::string hintText;
	int hintColumn = -1;

	if (forceHide) {
		hideMarkerHint();
		hideTaskOverview();
		return;
	}
	if (mTaskOverviewPinned) {
		hideMarkerHint();
		return;
	}
	if (!mouseInView(globalMouse)) {
		hideMarkerHint();
		hideTaskOverview();
		return;
	}
	TPoint local = makeLocal(globalMouse);
	if (taskX >= 0 && mTaskOverviewProvider && local.y == 0 && local.x >= taskX && local.x < taskX + markerSpan(kTaskMarkerIcon, kTaskMarkerSlotWidth)) {
		hideMarkerHint();
		showTaskOverview();
		return;
	}
	if (markerHintAt(local, state, hintText, hintColumn)) {
		hideTaskOverview();
		showMarkerHint(hintText, hintColumn);
		return;
	}
	hideMarkerHint();
	hideTaskOverview();
}

void MRFrame::tickTaskOverviewAnimation() {
	if (mTaskOverviewPopup == nullptr || !mTaskOverviewProvider) return;
	const auto now = std::chrono::steady_clock::now();
	if (now - mTaskOverviewLastRefresh < kTaskOverviewRefreshInterval) return;
	mTaskOverviewLastRefresh = now;
	std::vector<std::string> lines = mTaskOverviewProvider();
	if (lines.empty()) {
		hideTaskOverview();
		return;
	}
	mTaskOverviewPopup->setLines(lines);
}

void MRFrame::closeTaskOverview() {
	hideTaskOverview();
}
