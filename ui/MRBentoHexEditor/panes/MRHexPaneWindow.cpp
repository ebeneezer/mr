#include "MRHexPaneWindow.hpp"

#include "MRHexPaneView.hpp"
#include "../MRBentoHexEditor.hpp"
#include "../../../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>

MRHexPaneWindow::MRHexPaneWindow(const TRect &bounds, const char *title, int number, MRBentoHexEditor &editor, MRHexPaneRole role)
	: TWindowInit(&MRPaneEditWindow::initFrame), MRPaneEditWindow(bounds, title, number), mEditor(editor), mHexView(nullptr), mRole(role),
	  mSynchronizingScrollBars(false) {
	if (MRFileEditor *nativeEditor = getEditor(); nativeEditor != nullptr) nativeEditor->hide();
	if (horizontalEditorScrollBar() != nullptr) horizontalEditorScrollBar()->hide();
	if (verticalEditorScrollBar() != nullptr) verticalEditorScrollBar()->hide();
	if (editorIndicator() != nullptr) editorIndicator()->hide();
	mHexView = new MRHexPaneView(getExtent(), editor, role, static_cast<std::size_t>(bufferId()));
	insert(mHexView);
	setCurrent(mHexView, TView::normalSelect);
	layoutHexScrollBars();
}

void MRHexPaneWindow::requestHexProjection() noexcept {
	if (mHexView != nullptr) mHexView->requestProjection();
	synchronizeHexScrollBars();
}

int MRHexPaneWindow::hexViewportRowCapacity() const noexcept {
	return mHexView != nullptr ? std::max(0, static_cast<int>(mHexView->size.y)) : 0;
}

int MRHexPaneWindow::hexViewportColumnCapacity() const noexcept {
	if (mHexView == nullptr || mRole == MRHexPaneRole::Inspector) return 0;
	return std::max(0, (static_cast<int>(mHexView->size.x) - kMrHexPaneOffsetWidth) / mrHexPaneFieldWidth(mRole));
}

bool MRHexPaneWindow::applyHexProjectionResult(const mr::coprocessor::Result &result) noexcept {
	if (result.task.executionOwnerKind != mr::coprocessor::ExecutionOwnerKind::HexPane ||
	    result.task.executionOwnerLocalId != static_cast<std::size_t>(bufferId()) || mHexView == nullptr)
		return false;
	return mHexView->applyProjectionResult(result);
}

void MRHexPaneWindow::refreshHexCursor(std::size_t previousOffset, std::size_t currentOffset, bool viewportChanged) noexcept {
	if (mHexView != nullptr) mHexView->refreshCursor(previousOffset, currentOffset, viewportChanged);
	if (viewportChanged) synchronizeHexScrollBars();
}

void MRHexPaneWindow::refreshHexFocus() noexcept {
	if (mHexView != nullptr) mHexView->refreshFocus();
}

void MRHexPaneWindow::changeBounds(const TRect &bounds) {
	TWindow::changeBounds(bounds);
	layoutHexScrollBars();
}

void MRHexPaneWindow::draw() {
	layoutHexScrollBars();
	TWindow::draw();
}

void MRHexPaneWindow::handleEvent(TEvent &event) {
	if (event.what == evBroadcast && event.message.command == cmMrEditorDocumentCommitted) {
		mEditor.refreshAfterDocumentCommit();
		clearEvent(event);
		return;
	}
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
		if (mHexView != nullptr) mHexView->scrollByWheel(event.mouse.wheel);
		if (mRole == MRHexPaneRole::Inspector) synchronizeHexScrollBars();
		clearEvent(event);
		return;
	}
	if (mHexView != nullptr) mHexView->handleEvent(event);
	if (mRole == MRHexPaneRole::Inspector) synchronizeHexScrollBars();
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

bool MRHexPaneWindow::projectsPaneContentLocally() const noexcept {
	return true;
}

void MRHexPaneWindow::layoutHexScrollBars() noexcept {
	if (mHexView == nullptr) return;
	TRect content(getExtent());
	content.grow(-1, -1);
	if (mHexView->getBounds() != content) mHexView->changeBounds(content);
	if (TScrollBar *horizontal = horizontalEditorScrollBar()) {
		TRect horizontalBounds(1, size.y - 1, size.x - 1, size.y);
		if (horizontal->getBounds() != horizontalBounds) horizontal->locate(horizontalBounds);
	}
	if (TScrollBar *vertical = verticalEditorScrollBar()) {
		TRect verticalBounds(size.x - 1, 1, size.x, size.y - 1);
		if (vertical->getBounds() != verticalBounds) vertical->locate(verticalBounds);
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
	const bool paneVisible = (state & sfVisible) != 0;
	const bool always = configuredScrollbarVisibility() == MRScrollbarVisibility::Always;
	if (horizontalScrollBar != nullptr) {
		if (paneVisible && (always || horizontalScrollBar->maxVal > horizontalScrollBar->minVal)) horizontalScrollBar->show();
		else horizontalScrollBar->hide();
	}
	if (verticalScrollBar != nullptr) {
		if (paneVisible && (always || verticalScrollBar->maxVal > verticalScrollBar->minVal)) verticalScrollBar->show();
		else verticalScrollBar->hide();
	}
	mSynchronizingScrollBars = false;
}

bool MRHexPaneWindow::handlesHexScrollBar(const TEvent &event) const noexcept {
	return event.message.infoPtr == horizontalEditorScrollBar() || event.message.infoPtr == verticalEditorScrollBar();
}

void MRHexPaneWindow::acceptHexScrollBarChange(TScrollBar *scrollBar) noexcept {
	if (mHexView == nullptr || scrollBar == nullptr) return;
	if (scrollBar == horizontalEditorScrollBar()) mHexView->setHorizontalScrollBarValue(scrollBar->value);
	else if (scrollBar == verticalEditorScrollBar())
		mHexView->setVerticalScrollBarValue(scrollBar->value);
}
