#include "MRHexPaneWindow.hpp"

#include "MRHexPaneView.hpp"
#include "../MRBentoHexEditor.hpp"

#include <algorithm>

MRHexPaneWindow::MRHexPaneWindow(const TRect &bounds, const char *title, int number, MRBentoHexEditor &editor, MRHexPaneRole role)
	: TWindowInit(&MRPaneEditWindow::initFrame), MRPaneEditWindow(bounds, title, number), mHexView(nullptr), mRole(role), mSynchronizingScrollBars(false) {
	if (MRFileEditor *nativeEditor = getEditor(); nativeEditor != nullptr) nativeEditor->hide();
	if (horizontalEditorScrollBar() != nullptr) horizontalEditorScrollBar()->hide();
	if (verticalEditorScrollBar() != nullptr) verticalEditorScrollBar()->hide();
	if (editorIndicator() != nullptr) editorIndicator()->hide();
	mHexView = new MRHexPaneView(getExtent(), editor, role);
	insert(mHexView);
	setCurrent(mHexView, TView::normalSelect);
	layoutHexScrollBars();
}

void MRHexPaneWindow::changeBounds(const TRect &bounds) {
	TWindow::changeBounds(bounds);
	layoutHexScrollBars();
}

void MRHexPaneWindow::draw() {
	layoutHexScrollBars();
	if (mHexView != nullptr) mHexView->drawView();
	synchronizeHexScrollBars();
	TWindow::draw();
	drawHexScrollBars();
}

void MRHexPaneWindow::handleEvent(TEvent &event) {
	if (event.what == evBroadcast && (event.message.command == cmScrollBarClicked || event.message.command == cmScrollBarChanged) && handlesHexScrollBar(event)) {
		if (event.message.command == cmScrollBarChanged && !mSynchronizingScrollBars) acceptHexScrollBarChange(static_cast<TScrollBar *>(event.message.infoPtr));
		clearEvent(event);
		return;
	}
	if (event.what == evMouseDown) {
		TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
		TScrollBar *verticalScrollBar = verticalEditorScrollBar();

		if (horizontalScrollBar != nullptr && horizontalScrollBar->containsMouse(event)) {
			horizontalScrollBar->handleEvent(event);
			return;
		}
		if (verticalScrollBar != nullptr && verticalScrollBar->containsMouse(event)) {
			verticalScrollBar->handleEvent(event);
			return;
		}
	}
	if (event.what == evMouseWheel) {
		if (mHexView != nullptr) {
			mHexView->scrollByWheel(event.mouse.wheel);
			mHexView->drawView();
		}
		synchronizeHexScrollBars();
		drawHexScrollBars();
		clearEvent(event);
		return;
	}
	if (mHexView != nullptr) mHexView->handleEvent(event);
	synchronizeHexScrollBars();
}

void MRHexPaneWindow::cancelTransientInput() noexcept {
	if (mHexView != nullptr) mHexView->cancelPendingEdit();
}

bool MRHexPaneWindow::completeTransientInput() noexcept {
	try {
		return mHexView == nullptr || mHexView->commitPendingEdit();
	} catch (...) {
		return false;
	}
}

bool MRHexPaneWindow::usesNativeEditorChrome() const noexcept {
	return false;
}

bool MRHexPaneWindow::ownsPaneWheelEvents() const noexcept {
	return true;
}

void MRHexPaneWindow::layoutHexScrollBars() noexcept {
	if (mHexView == nullptr) return;
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	const bool horizontalScrollBarAllowed = mRole != MRHexPaneRole::Inspector;
	const bool reserveHorizontal = horizontalScrollBarAllowed;
	const bool reserveVertical = true;
	TRect content(getExtent());

	if (reserveVertical && content.b.x - content.a.x > 1) --content.b.x;
	if (reserveHorizontal && content.b.y - content.a.y > 1) --content.b.y;
	if (mHexView->getBounds() != content) mHexView->changeBounds(content);
	if (horizontalScrollBar != nullptr) {
		TRect bounds = reserveHorizontal ? TRect(0, std::max<short>(0, size.y - 1), reserveVertical ? std::max<short>(1, size.x - 1) : std::max<short>(1, size.x), size.y) : TRect(0, size.y, 0, size.y);

		if (horizontalScrollBar->getBounds() != bounds) horizontalScrollBar->locate(bounds);
		if (reserveHorizontal) horizontalScrollBar->show();
		else
			horizontalScrollBar->hide();
	}
	if (verticalScrollBar != nullptr) {
		TRect bounds = reserveVertical ? TRect(std::max<short>(0, size.x - 1), 0, size.x, reserveHorizontal ? std::max<short>(1, size.y - 1) : std::max<short>(1, size.y)) : TRect(size.x, 0, size.x, 0);

		if (verticalScrollBar->getBounds() != bounds) verticalScrollBar->locate(bounds);
		if (reserveVertical) verticalScrollBar->show();
		else
			verticalScrollBar->hide();
	}
	synchronizeHexScrollBars();
}

void MRHexPaneWindow::synchronizeHexScrollBars() noexcept {
	if (mHexView == nullptr || mSynchronizingScrollBars) return;
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();

	mSynchronizingScrollBars = true;
	if (horizontalScrollBar != nullptr) horizontalScrollBar->setParams(mHexView->horizontalScrollBarValue(), 0, mHexView->horizontalScrollBarMaximum(), mHexView->horizontalScrollBarPageStep(), 1);
	if (verticalScrollBar != nullptr) verticalScrollBar->setParams(mHexView->verticalScrollBarValue(), 0, mHexView->verticalScrollBarMaximum(), mHexView->verticalScrollBarPageStep(), 1);
	mSynchronizingScrollBars = false;
}

void MRHexPaneWindow::drawHexScrollBars() noexcept {
	if (mHexView == nullptr) return;
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	const TColorAttr fillAttr = mHexView->getColor(0x0201)[0];
	const TColorAttr markerAttr = mapColor(13);
	auto fillRect = [this, fillAttr](const TRect &rect) {
		const short left = std::max<short>(0, rect.a.x);
		const short top = std::max<short>(0, rect.a.y);
		const short right = std::min<short>(size.x, rect.b.x);
		const short bottom = std::min<short>(size.y, rect.b.y);
		const int width = right - left;

		if (width <= 0 || bottom <= top) return;
		TDrawBuffer buffer;
		buffer.moveChar(0, ' ', TAttrPair(fillAttr), static_cast<ushort>(width));
		for (short y = top; y < bottom; ++y)
			writeLine(left, y, width, 1, buffer);
	};
	auto drawScrollBar = [fillAttr, markerAttr](TScrollBar *scrollBar) {
		if (scrollBar == nullptr || (scrollBar->state & sfVisible) == 0 || scrollBar->size.x <= 0 || scrollBar->size.y <= 0) return;
		if (auto *editScrollBar = dynamic_cast<MREditScrollBar *>(scrollBar)) editScrollBar->setColorOverride(true, fillAttr, fillAttr, markerAttr);
		scrollBar->chars[4] = scrollBar->chars[2];
		scrollBar->drawView();
	};

	if (horizontalScrollBar != nullptr) fillRect(horizontalScrollBar->getBounds());
	if (verticalScrollBar != nullptr) fillRect(verticalScrollBar->getBounds());
	drawScrollBar(horizontalScrollBar);
	drawScrollBar(verticalScrollBar);
	if (horizontalScrollBar != nullptr && verticalScrollBar != nullptr && (horizontalScrollBar->state & sfVisible) != 0 && (verticalScrollBar->state & sfVisible) != 0) {
		const TRect horizontalBounds = horizontalScrollBar->getBounds();
		const TRect verticalBounds = verticalScrollBar->getBounds();

		if (horizontalBounds.a.y < horizontalBounds.b.y && verticalBounds.a.x < verticalBounds.b.x) fillRect(TRect(verticalBounds.a.x, horizontalBounds.a.y, static_cast<short>(verticalBounds.a.x + 1), static_cast<short>(horizontalBounds.a.y + 1)));
	}
}

bool MRHexPaneWindow::handlesHexScrollBar(const TEvent &event) const noexcept {
	return event.message.infoPtr == horizontalEditorScrollBar() || event.message.infoPtr == verticalEditorScrollBar();
}

void MRHexPaneWindow::acceptHexScrollBarChange(TScrollBar *scrollBar) noexcept {
	if (mHexView == nullptr || scrollBar == nullptr) return;
	if (scrollBar == horizontalEditorScrollBar()) mHexView->setHorizontalScrollBarValue(scrollBar->value);
	else if (scrollBar == verticalEditorScrollBar())
		mHexView->setVerticalScrollBarValue(scrollBar->value);
	mHexView->drawView();
	drawHexScrollBars();
}
