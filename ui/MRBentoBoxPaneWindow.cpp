#include "MRBentoBox.hpp"

#include "MRFrame.hpp"

#include "../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>

namespace {

void normalizeScrollBarTrackGlyph(TScrollBar *scrollBar) noexcept {
	if (scrollBar == nullptr) return;
	scrollBar->chars[4] = scrollBar->chars[2];
}

class MRPaneFrame : public MRFrame {
  public:
	explicit MRPaneFrame(const TRect &bounds) noexcept : MRFrame(bounds) {
		eventMask = 0;
	}

	virtual void draw() override {
	}

	virtual void handleEvent(TEvent &) override {
	}
};

}

MRPaneEditWindow::MRPaneEditWindow(const TRect &bounds, const char *title, int number) : TWindowInit(&MRPaneEditWindow::initFrame), MREditWindow(bounds, title, number), mPaneSpec(), mPaneFocused(false) {
	flags = 0;
	state &= static_cast<ushort>(~sfShadow);
	options &= static_cast<ushort>(~(ofTileable | ofTopSelect));
	eventMask = 0;
	layoutPaneChrome();
}

MRPaneEditWindow::~MRPaneEditWindow() {
}

bool MRPaneEditWindow::paneOwned() const noexcept {
	return true;
}

MRBentoPaneRole MRPaneEditWindow::paneRole() const noexcept {
	return mPaneSpec.role;
}

void MRPaneEditWindow::changeBounds(const TRect &bounds) {
	MREditWindow::changeBounds(bounds);
	layoutPaneChrome();
}

void MRPaneEditWindow::draw() {
	MREditWindow::draw();
	drawPaneScrollBars();
}

TColorAttr MRPaneEditWindow::mapColor(uchar index) {
	if (index == 4 || index == 5) return MREditWindow::mapColor(mPaneFocused ? 13 : 1);
	return MREditWindow::mapColor(index);
}

Boolean MRPaneEditWindow::valid(ushort command) {
	if (command == cmClose) return True;
	return MREditWindow::valid(command);
}

void MRPaneEditWindow::setPaneSpec(const MRBentoPaneSpec &spec, const MRFileEditor *sourceEditor) noexcept {
	const bool sameSpec = mPaneSpec.role == spec.role && mPaneSpec.bufferPolicy == spec.bufferPolicy && mPaneSpec.readOnly == spec.readOnly && mPaneSpec.suppressMiniMap == spec.suppressMiniMap && mPaneSpec.suppressWordWrap == spec.suppressWordWrap && mPaneSpec.scrollBarsAlwaysVisible == spec.scrollBarsAlwaysVisible && mPaneSpec.titleMenu == spec.titleMenu;
	const MRBentoPaneBufferPolicy oldBufferPolicy = mPaneSpec.bufferPolicy;
	mPaneSpec = spec;
	if (oldBufferPolicy == bpbSharedSourceBuffer && spec.bufferPolicy == bpbOwnBuffer && getEditor() != nullptr) getEditor()->detachContentStateCopy();
	applyPanePolicy(spec.bufferPolicy == bpbSharedSourceBuffer ? sourceEditor : nullptr);
	if (sameSpec) return;
	layoutPaneChrome();
}

void MRPaneEditWindow::setPaneFocused(bool focused) noexcept {
	if (mPaneFocused == focused) return;
	mPaneFocused = focused;
	drawPaneScrollBars();
}

void MRPaneEditWindow::applyPanePolicy(const MRFileEditor *sourceEditor) noexcept {
	MRFileEditor *paneEditor = getEditor();

	if (paneEditor == nullptr) return;
	if (mPaneSpec.bufferPolicy == bpbSharedSourceBuffer && sourceEditor != nullptr) paneEditor->shareContentStateFrom(*sourceEditor);
	setReadOnly(mPaneSpec.readOnly);
	paneEditor->setCommunicationViewerMode(mPaneSpec.readOnly, mPaneSpec.role != bprProblems);
	paneEditor->setMiniMapSuppressed(mPaneSpec.suppressMiniMap);
	paneEditor->setWordWrapSuppressed(mPaneSpec.suppressWordWrap);
	paneEditor->setScrollBarsAlwaysVisible(mPaneSpec.scrollBarsAlwaysVisible);
}

void MRPaneEditWindow::layoutPaneChrome() noexcept {
	TRect editorBounds(getExtent());
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	MRIndicator *paneIndicator = editorIndicator();

	if (frame != nullptr) frame->hide();
	MRFileEditor *paneEditor = getEditor();
	if (paneEditor != nullptr) {
		applyPanePolicy(nullptr);
		if (mPaneSpec.scrollBarsAlwaysVisible) {
			const bool showWithoutRange = configuredScrollbarVisibility() == MRScrollbarVisibility::Always;
			bool reserveHorizontal = showWithoutRange;
			bool reserveVertical = showWithoutRange;
			if (paneIndicator != nullptr) paneIndicator->hide();
			if (!showWithoutRange) {
				paneEditor->changeBounds(editorBounds);
				paneEditor->updateMetrics();
				reserveHorizontal = horizontalScrollBar != nullptr && horizontalScrollBar->maxVal > horizontalScrollBar->minVal;
				reserveVertical = verticalScrollBar != nullptr && verticalScrollBar->maxVal > verticalScrollBar->minVal;
			}
			for (int pass = 0; pass < 3; ++pass) {
				editorBounds = getExtent();
				if (reserveVertical && editorBounds.b.x - editorBounds.a.x > 1) editorBounds.b.x = std::max<short>(editorBounds.a.x + 1, editorBounds.b.x - 1);
				if (reserveHorizontal && editorBounds.b.y - editorBounds.a.y > 1) editorBounds.b.y = std::max<short>(editorBounds.a.y + 1, editorBounds.b.y - 1);
				if (horizontalScrollBar != nullptr) {
					if (reserveHorizontal) {
						const short right = reserveVertical ? std::max<short>(1, size.x - 1) : std::max<short>(1, size.x);
						TRect horizontalRect(0, std::max(0, size.y - 1), right, size.y);
						horizontalScrollBar->locate(horizontalRect);
						if (showWithoutRange) horizontalScrollBar->show();
					} else {
						TRect horizontalRect(0, size.y, 0, size.y);
						horizontalScrollBar->locate(horizontalRect);
					}
				}
				if (verticalScrollBar != nullptr) {
					if (reserveVertical) {
						const short bottom = reserveHorizontal ? std::max<short>(1, size.y - 1) : std::max<short>(1, size.y);
						TRect verticalRect(std::max(0, size.x - 1), 0, size.x, bottom);
						verticalScrollBar->locate(verticalRect);
						if (showWithoutRange) verticalScrollBar->show();
					} else {
						TRect verticalRect(size.x, 0, size.x, 0);
						verticalScrollBar->locate(verticalRect);
					}
				}
				paneEditor->changeBounds(editorBounds);
				paneEditor->updateMetrics();
				if (showWithoutRange) break;
				const bool nextReserveHorizontal = horizontalScrollBar != nullptr && horizontalScrollBar->maxVal > horizontalScrollBar->minVal;
				const bool nextReserveVertical = verticalScrollBar != nullptr && verticalScrollBar->maxVal > verticalScrollBar->minVal;
				if (nextReserveHorizontal == reserveHorizontal && nextReserveVertical == reserveVertical) break;
				reserveHorizontal = nextReserveHorizontal;
				reserveVertical = nextReserveVertical;
			}
			if (horizontalScrollBar != nullptr) {
				if (reserveHorizontal) horizontalScrollBar->show();
				else
					horizontalScrollBar->hide();
			}
			if (verticalScrollBar != nullptr) {
				if (reserveVertical) verticalScrollBar->show();
				else
					verticalScrollBar->hide();
			}
		} else {
			if (horizontalScrollBar != nullptr) horizontalScrollBar->hide();
			if (verticalScrollBar != nullptr) verticalScrollBar->hide();
			paneEditor->changeBounds(editorBounds);
			paneEditor->updateMetrics();
		}
		drawPaneScrollBars();
	}
}

void MRPaneEditWindow::drawPaneScrollBars() noexcept {
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	auto fillRect = [this](const TRect &rect) {
		const short left = std::max<short>(0, rect.a.x);
		const short top = std::max<short>(0, rect.a.y);
		const short right = std::min<short>(size.x, rect.b.x);
		const short bottom = std::min<short>(size.y, rect.b.y);
		const int width = right - left;

		if (width <= 0 || bottom <= top) return;
		TDrawBuffer buffer;
		buffer.moveChar(0, ' ', TAttrPair(mapColor(1)), width);
		for (short y = top; y < bottom; ++y)
			writeLine(left, y, width, 1, buffer);
	};

	normalizeScrollBarTrackGlyph(horizontalScrollBar);
	normalizeScrollBarTrackGlyph(verticalScrollBar);
	if (horizontalScrollBar != nullptr) horizontalScrollBar->drawView();
	if (verticalScrollBar != nullptr) verticalScrollBar->drawView();
	if (horizontalScrollBar != nullptr && (horizontalScrollBar->state & sfVisible) == 0) fillRect(horizontalScrollBar->getBounds());
	if (verticalScrollBar != nullptr && (verticalScrollBar->state & sfVisible) == 0) fillRect(verticalScrollBar->getBounds());
	if (horizontalScrollBar != nullptr && verticalScrollBar != nullptr && (horizontalScrollBar->state & sfVisible) != 0 && (verticalScrollBar->state & sfVisible) != 0) {
		const TRect horizontalBounds = horizontalScrollBar->getBounds();
		const TRect verticalBounds = verticalScrollBar->getBounds();
		if (horizontalBounds.a.y < horizontalBounds.b.y && verticalBounds.a.x < verticalBounds.b.x) {
			TDrawBuffer buffer;
			buffer.moveChar(0, ' ', TAttrPair(mapColor(4)), 1);
			writeBuf(verticalBounds.a.x, horizontalBounds.a.y, 1, 1, buffer);
		}
	} else if (horizontalScrollBar != nullptr && verticalScrollBar != nullptr) {
		const TRect horizontalBounds = horizontalScrollBar->getBounds();
		const TRect verticalBounds = verticalScrollBar->getBounds();
		if (horizontalBounds.a.y < horizontalBounds.b.y && verticalBounds.a.x < verticalBounds.b.x) {
			TRect corner(verticalBounds.a.x, horizontalBounds.a.y, static_cast<short>(verticalBounds.a.x + 1), static_cast<short>(horizontalBounds.a.y + 1));
			fillRect(corner);
		}
	}
}

TFrame *MRPaneEditWindow::initFrame(TRect bounds) {
	return new MRPaneFrame(bounds);
}
