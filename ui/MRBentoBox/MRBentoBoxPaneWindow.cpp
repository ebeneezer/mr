#include "MRBentoBox.hpp"

#include "../MRFrame.hpp"

#include "../../config/settings/MRSettingsRuntime.hpp"

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

MRPaneEditWindow::MRPaneEditWindow(const TRect &bounds, const char *title, int number)
	: TWindowInit(&MRPaneEditWindow::initFrame), MREditWindow(bounds, title, number, mr::coprocessor::ExecutionOwnerKind::BentoPane), mPaneSpec(), mPaneFocused(false) {
	flags = 0;
	state &= static_cast<ushort>(~sfShadow);
	options &= static_cast<ushort>(~(ofTileable | ofTopSelect));
	eventMask = 0;
	layoutPaneChrome();
}

MRPaneEditWindow::~MRPaneEditWindow() {
}

void MRPaneEditWindow::changeBounds(const TRect &bounds) {
	MREditWindow::changeBounds(bounds);
	layoutPaneChrome();
}

void MRPaneEditWindow::draw() {
	MREditWindow::draw();
	if (usesNativeEditorChrome()) drawPaneScrollBars();
}

void MRPaneEditWindow::handleEvent(TEvent &event) {
	MRFileEditor *committedEditor = event.what == evBroadcast && event.message.command == cmMrEditorDocumentCommitted
	                                    ? static_cast<MRFileEditor *>(event.message.infoPtr)
	                                    : nullptr;
	const bool relaySourceCommit = mPaneSpec.role == bprSplitEditor && mPaneSpec.bufferPolicy == bpbSharedSourceBuffer &&
	                               committedEditor != nullptr && committedEditor == getEditor();

	MREditWindow::handleEvent(event);
	if (relaySourceCommit)
		if (MRBentoBox *bento = dynamic_cast<MRBentoBox *>(owner)) bento->handleCommittedSourceEditor(committedEditor);
}

TColorAttr MRPaneEditWindow::mapColor(uchar index) {
	if ((mPaneSpec.role == bprDiffOriginal || mPaneSpec.role == bprDiffCompare) && (index == 4 || index == 5)) {
		MRFileEditor *paneEditor = getEditor();

		if (paneEditor != nullptr) return paneEditor->editorTextFillColor();
	}
	if (index == 4 || index == 5) return MREditWindow::mapColor(mPaneFocused ? 13 : 1);
	return MREditWindow::mapColor(index);
}

Boolean MRPaneEditWindow::valid(ushort command) {
	if (command == cmClose) return True;
	return MREditWindow::valid(command);
}

void MRPaneEditWindow::cancelTransientInput() noexcept {
}

bool MRPaneEditWindow::completeTransientInput() noexcept {
	cancelTransientInput();
	return true;
}

bool MRPaneEditWindow::usesNativeEditorChrome() const noexcept {
	return true;
}

bool MRPaneEditWindow::ownsPaneWheelEvents() const noexcept {
	return false;
}

bool MRPaneEditWindow::projectsPaneContentLocally() const noexcept {
	return false;
}

void MRPaneEditWindow::setPaneSpec(const MRBentoPaneSpec &spec, const MRFileEditor *sourceEditor) noexcept {
	const bool sameSpec = mPaneSpec.role == spec.role && mPaneSpec.bufferPolicy == spec.bufferPolicy && mPaneSpec.readOnly == spec.readOnly && mPaneSpec.widgetMask == spec.widgetMask && mPaneSpec.suppressMiniMap == spec.suppressMiniMap && mPaneSpec.suppressWordWrap == spec.suppressWordWrap && mPaneSpec.scrollBarsAlwaysVisible == spec.scrollBarsAlwaysVisible && mPaneSpec.titleMenu == spec.titleMenu;
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
	paneEditor->setCommunicationViewerMode(mPaneSpec.readOnly && mPaneSpec.role != bprDiffOriginal && mPaneSpec.role != bprDiffCompare, mPaneSpec.role != bprProblems);
	paneEditor->setMiniMapSuppressed(mPaneSpec.suppressMiniMap);
	paneEditor->setWordWrapSuppressed(mPaneSpec.suppressWordWrap);
	paneEditor->setScrollBarsAlwaysVisible(mPaneSpec.scrollBarsAlwaysVisible);
}

void MRPaneEditWindow::layoutPaneChrome() noexcept {
	if (!usesNativeEditorChrome()) return;
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
			configurePaneScrollBarColors();
		} else {
			if (horizontalScrollBar != nullptr) horizontalScrollBar->hide();
			if (verticalScrollBar != nullptr) verticalScrollBar->hide();
			paneEditor->changeBounds(editorBounds);
			paneEditor->updateMetrics();
			configurePaneScrollBarColors();
		}
		drawPaneScrollBars();
	}
}

void MRPaneEditWindow::configurePaneScrollBarColors() noexcept {
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	MRFileEditor *paneEditor = getEditor();
	const TColorAttr fillAttr = paneEditor != nullptr ? paneEditor->editorTextFillColor() : mapColor(6);
	TColorAttr markerAttr = mapColor(mPaneFocused ? 13 : 1);

	if (mPaneSpec.role == bprDiffOriginal || mPaneSpec.role == bprDiffCompare) markerAttr = fillAttr;
	if (auto *scrollBar = dynamic_cast<MREditScrollBar *>(horizontalScrollBar)) scrollBar->setColorOverride(true, fillAttr, fillAttr, markerAttr);
	if (auto *scrollBar = dynamic_cast<MREditScrollBar *>(verticalScrollBar)) scrollBar->setColorOverride(true, fillAttr, fillAttr, markerAttr);
}

void MRPaneEditWindow::drawPaneScrollBars() noexcept {
	if (!usesNativeEditorChrome()) return;
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	const bool showWithoutRange = configuredScrollbarVisibility() == MRScrollbarVisibility::Always;
	MRFileEditor *paneEditor = getEditor();
	const TColorAttr fillAttr = paneEditor != nullptr ? paneEditor->editorTextFillColor() : mapColor(6);
	TColorAttr markerAttr = mapColor(mPaneFocused ? 13 : 1);

	if (mPaneSpec.role == bprDiffOriginal || mPaneSpec.role == bprDiffCompare) markerAttr = fillAttr;
	configurePaneScrollBarColors();
	auto fillRect = [this, fillAttr](const TRect &rect) {
		const short left = std::max<short>(0, rect.a.x);
		const short top = std::max<short>(0, rect.a.y);
		const short right = std::min<short>(size.x, rect.b.x);
		const short bottom = std::min<short>(size.y, rect.b.y);
		const int width = right - left;

		if (width <= 0 || bottom <= top) return;
		TDrawBuffer buffer;
		buffer.moveChar(0, ' ', TAttrPair(fillAttr), width);
		for (short y = top; y < bottom; ++y)
			writeLine(left, y, width, 1, buffer);
	};
	if (paneEditor != nullptr) {
		const TRect extent = getExtent();
		const TRect editorBounds = paneEditor->getBounds();

		if (editorBounds.a.y > extent.a.y) fillRect(TRect(extent.a.x, extent.a.y, extent.b.x, editorBounds.a.y));
		if (editorBounds.b.y < extent.b.y) fillRect(TRect(extent.a.x, editorBounds.b.y, extent.b.x, extent.b.y));
		if (editorBounds.a.x > extent.a.x) fillRect(TRect(extent.a.x, editorBounds.a.y, editorBounds.a.x, editorBounds.b.y));
		if (editorBounds.b.x < extent.b.x) fillRect(TRect(editorBounds.b.x, editorBounds.a.y, extent.b.x, editorBounds.b.y));
	}
	auto scrollBarRequired = [showWithoutRange](TScrollBar *scrollBar) {
		if (scrollBar == nullptr) return false;
		if ((scrollBar->state & sfVisible) == 0) return false;
		if (scrollBar->size.x <= 0 || scrollBar->size.y <= 0) return false;
		if (!showWithoutRange && scrollBar->maxVal <= scrollBar->minVal) return false;
		return true;
	};
	auto drawCell = [this](short x, short y, char ch, TColorAttr attr) {
		TDrawBuffer buffer;
		buffer.moveChar(0, ch, attr, 1);
		writeBuf(x, y, 1, 1, buffer);
	};
	auto drawScrollBar = [&](TScrollBar *scrollBar) {
		if (!scrollBarRequired(scrollBar)) return false;
		const TRect bounds = scrollBar->getBounds();
		const int logicalSize = std::max(3, scrollBar->size.x == 1 ? scrollBar->size.y : scrollBar->size.x);
		const int markerPos = scrollBar->getPos();

		if (scrollBar->size.x == 1) {
			for (int row = 0; row < logicalSize; ++row) {
				char ch = scrollBar->chars[2];
				TColorAttr attr = fillAttr;
				if (row == 0) ch = scrollBar->chars[0];
				else if (row == logicalSize - 1)
					ch = scrollBar->chars[1];
				else if (scrollBar->maxVal == scrollBar->minVal)
					ch = scrollBar->chars[4];
				else if (row == markerPos) {
					ch = scrollBar->chars[3];
					attr = markerAttr;
				}
				drawCell(bounds.a.x, static_cast<short>(bounds.a.y + row), ch, attr);
			}
		} else {
			const int width = std::max(0, bounds.b.x - bounds.a.x);
			if (width <= 0) return false;
			TDrawBuffer buffer;
			buffer.moveChar(0, ' ', fillAttr, static_cast<ushort>(width));
			for (int column = 0; column < std::min(width, logicalSize); ++column) {
				char ch = scrollBar->chars[2];
				TColorAttr attr = fillAttr;
				if (column == 0) ch = scrollBar->chars[0];
				else if (column == logicalSize - 1)
					ch = scrollBar->chars[1];
				else if (scrollBar->maxVal == scrollBar->minVal)
					ch = scrollBar->chars[4];
				else if (column == markerPos) {
					ch = scrollBar->chars[3];
					attr = markerAttr;
				}
				buffer.moveChar(static_cast<ushort>(column), ch, attr, 1);
			}
			writeLine(bounds.a.x, bounds.a.y, width, 1, buffer);
		}
		return true;
	};

	normalizeScrollBarTrackGlyph(horizontalScrollBar);
	normalizeScrollBarTrackGlyph(verticalScrollBar);
	const bool drewHorizontal = drawScrollBar(horizontalScrollBar);
	const bool drewVertical = drawScrollBar(verticalScrollBar);
	if (horizontalScrollBar != nullptr && !drewHorizontal) fillRect(horizontalScrollBar->getBounds());
	if (verticalScrollBar != nullptr && !drewVertical) fillRect(verticalScrollBar->getBounds());
	if (horizontalScrollBar != nullptr && verticalScrollBar != nullptr && (horizontalScrollBar->state & sfVisible) != 0 && (verticalScrollBar->state & sfVisible) != 0) {
		const TRect horizontalBounds = horizontalScrollBar->getBounds();
		const TRect verticalBounds = verticalScrollBar->getBounds();
		if (horizontalBounds.a.y < horizontalBounds.b.y && verticalBounds.a.x < verticalBounds.b.x) {
			TDrawBuffer buffer;
			buffer.moveChar(0, ' ', TAttrPair(fillAttr), 1);
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
