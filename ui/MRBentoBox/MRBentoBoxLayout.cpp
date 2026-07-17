#include "MRBentoBox.hpp"
#include "MRBentoPaneFrameView.hpp"

#include "../MRSidekickEditor.hpp"
#include "../MRWindowSupport.hpp"

#include "../../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>

namespace {

void normalizeScrollBarTrackGlyph(TScrollBar *scrollBar) noexcept {
	if (scrollBar == nullptr) return;
	scrollBar->chars[4] = scrollBar->chars[2];
}

} // namespace

void MRBentoBox::setState(ushort aState, Boolean enable) {
	if (windowCloseInProgress) {
		TWindow::setState(aState, enable);
		return;
	}
	MREditWindow::setState(aState, enable);
	if ((aState & (sfActive | sfSelected)) != 0 && enable == False) mrDropSidekickForParent(this);
	if (hasPaneSplit() && (aState & (sfFocused | sfSelected | sfActive)) != 0) {
		updateActivePaneFrame();
		bentoProjectionDirty |= bpdChrome;
		flushBentoProjection();
	}
}

void MRBentoBox::initializeLayoutTree() noexcept {
	layoutTree.clear();
	leaves.clear();
	rootNode = -1;
	nextLeafId = 0;
	BentoLeaf source;
	source.id = nextLeafId++;
	source.role = bprSource;
	source.spec = paneSpecForRole(bprSource);
	source.title = bentoMode == bbmDocumentViewports ? "" : titleForPaneRole(bprSource);
	if (bentoMode == bbmFileCompare) {
		source.role = bprDiffOriginal;
		source.spec = paneSpecForRole(bprDiffOriginal);
		source.title = titleForPaneRole(bprDiffOriginal);
	}
	source.pane = nullptr;
	source.visible = true;
	leaves.push_back(source);
	rootNode = createLeafNode(source.id);
	activeLeafId = source.id;
	maximizedLeafId = -1;
	sourceScrollBarPaletteActive = false;
	secondaryPaneVisible = false;
}

void MRBentoBox::ensurePaneFrameViews() {
	while (paneFrameViews.size() < leaves.size()) {
		MRBentoPaneFrameView *view = new MRBentoPaneFrameView(TRect(0, 0, 0, 0));
		view->hide();
		// Pane chrome is an overlay. It must precede pane windows in TVision's
		// view order, otherwise their opaque bounds clip the frame writes.
		insertBefore(view, first());
		paneFrameViews.push_back(view);
	}
}

void MRBentoBox::layoutSplitPanes() {
	MRFileEditor *primaryEditor = getEditor();
	TRect inner = paneLayoutBounds();
	if (!hasPaneSplit()) {
		for (BentoLeaf &leaf : leaves) {
			leaf.visible = leaf.id == 0;
			if (leaf.id == 0) leaf.bounds = inner;
			if (leaf.pane != nullptr) {
				if (leaf.id == 0) {
					hideSourcePaneChrome();
					leaf.pane->setPaneFocused((state & sfFocused) != 0);
					leaf.pane->show();
					leaf.pane->changeBounds(inner);
				} else
					leaf.pane->hide();
			}
		}
		for (MRBentoPaneFrameView *view : paneFrameViews)
			if (view != nullptr) view->hide();
		activeLeafId = 0;
		maximizedLeafId = -1;
		secondaryPaneVisible = false;
		sourceScrollBarPaletteActive = false;
		paneRoleDropList.hide();
		paneActionDropList.hide();
		if (primaryEditor != nullptr && paneWindowForLeaf(0) == nullptr) primaryEditor->setScrollBarsAlwaysVisible(false);
		MREditWindow::changeBounds(getBounds());
		if (primaryEditor != nullptr && paneWindowForLeaf(0) == nullptr) primaryEditor->drawView();
		for (BentoLeaf &leaf : leaves)
			if (leaf.visible && leaf.pane != nullptr) leaf.pane->drawView();
		bentoProjectionDirty = bpdNone;
		return;
	}
	ensurePaneFrameViews();
	for (BentoLeaf &leaf : leaves) leaf.visible = false;
	if (maximizedLeafId >= 0 && nodeIndexForLeaf(maximizedLeafId) >= 0) {
		for (BentoLeaf &leaf : leaves) {
			if (leaf.id == maximizedLeafId) {
				leaf.bounds = inner;
				leaf.visible = true;
				break;
			}
		}
	} else {
		maximizedLeafId = -1;
		if (rootNode >= 0) layoutNode(rootNode, inner);
	}
	secondaryPaneVisible = firstToolLeafId() >= 0;

	for (std::size_t i = 0; i < leaves.size(); ++i) {
		BentoLeaf &leaf = leaves[i];
		MRBentoPaneFrameView *view = i < paneFrameViews.size() ? paneFrameViews[i] : nullptr;
		if (leaf.visible) {
			const TRect content = contentBounds(leaf.bounds);
			const bool focused = leaf.id == activeLeafId && (state & sfFocused) != 0;
			if (leaf.pane != nullptr) {
				if (leaf.id == 0) hideSourcePaneChrome();
				leaf.pane->setPaneFocused(focused);
				leaf.pane->show();
				leaf.pane->changeBounds(content);
			} else if (leaf.id == 0) {
				layoutSourcePaneChrome(content);
			}
			if (view != nullptr) {
				view->changeBounds(leaf.bounds);
				view->setPane(leaf.id, paneTitleForLeaf(leaf).c_str(), leaf.id == 0 && bentoMode != bbmDocumentViewports, focused, leaf.id == maximizedLeafId, paneCloseActionEnabled(), paneMaximizeActionEnabled(), paneFrameColor(focused));
				view->show();
			}
		} else {
			if (leaf.id == 0) hideSourcePaneChrome();
			if (leaf.pane != nullptr) leaf.pane->hide();
			if (view != nullptr) view->hide();
		}
	}
	if (frame != nullptr) frame->drawView();
	if (primaryEditor != nullptr && paneWindowForLeaf(0) == nullptr) {
		primaryEditor->drawView();
		if (leaves.front().visible) drawSourcePaneScrollBars();
	}
	for (BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.pane != nullptr) leaf.pane->drawView();
	drawPaneFrames();
	bentoProjectionDirty = bpdNone;
}

void MRBentoBox::flushBentoProjection() noexcept {
	if (windowCloseInProgress || bentoProjectionDirty == bpdNone) return;
	const unsigned dirty = bentoProjectionDirty;
	bentoProjectionDirty = bpdNone;

	if (!hasPaneSplit()) return;
	if ((dirty & bpdLayout) != 0) {
		layoutSplitPanes();
		if ((dirty & bpdOverlay) != 0) updateTrackedCompilerSidekick();
		paneRoleDropList.drawOpenList();
		paneActionDropList.drawOpenList();
		return;
	}
	if ((dirty & bpdContent) != 0) drawSharedEditorPanes();
	if ((dirty & bpdScrollBar) != 0) drawSourcePaneScrollBars();
	if ((dirty & bpdOverlay) != 0) updateTrackedCompilerSidekick();
	if ((dirty & (bpdChrome | bpdScrollBar)) != 0) drawPaneFrames();
	paneRoleDropList.drawOpenList();
	paneActionDropList.drawOpenList();
}

void MRBentoBox::layoutSourcePaneChrome(const TRect &content) noexcept {
	MRFileEditor *primaryEditor = getEditor();
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	MRIndicator *sourceIndicator = editorIndicator();
	TRect editorBounds = content;
	const bool showWithoutRange = configuredScrollbarVisibility() == MRScrollbarVisibility::Always;
	bool reserveHorizontal = showWithoutRange;
	bool reserveVertical = showWithoutRange;

	if (primaryEditor != nullptr && (primaryEditor->state & sfVisible) == 0) primaryEditor->show();
	if (primaryEditor != nullptr) primaryEditor->setScrollBarsAlwaysVisible(true);
	if (sourceIndicator != nullptr) sourceIndicator->hide();
	if (primaryEditor != nullptr && !showWithoutRange) {
		primaryEditor->changeBounds(editorBounds);
		primaryEditor->updateMetrics();
		reserveHorizontal = horizontalScrollBar != nullptr && horizontalScrollBar->maxVal > horizontalScrollBar->minVal;
		reserveVertical = verticalScrollBar != nullptr && verticalScrollBar->maxVal > verticalScrollBar->minVal;
	}
	for (int pass = 0; pass < 3; ++pass) {
		editorBounds = content;
		if (reserveVertical && content.b.x - content.a.x > 1) editorBounds.b.x = std::max<short>(editorBounds.a.x + 1, editorBounds.b.x - 1);
		if (reserveHorizontal && content.b.y - content.a.y > 1) editorBounds.b.y = std::max<short>(editorBounds.a.y + 1, editorBounds.b.y - 1);
		if (horizontalScrollBar != nullptr) {
			if (reserveHorizontal) {
				const short right = reserveVertical ? std::max<short>(content.a.x + 1, content.b.x - 1) : std::max<short>(content.a.x + 1, content.b.x);
				TRect horizontalRect(content.a.x, std::max<short>(content.a.y, content.b.y - 1), right, content.b.y);
				horizontalScrollBar->locate(horizontalRect);
			} else {
				TRect horizontalRect(content.a.x, content.b.y, content.a.x, content.b.y);
				horizontalScrollBar->locate(horizontalRect);
			}
		}
		if (verticalScrollBar != nullptr) {
			if (reserveVertical) {
				const short bottom = reserveHorizontal ? std::max<short>(content.a.y + 1, content.b.y - 1) : std::max<short>(content.a.y + 1, content.b.y);
				TRect verticalRect(std::max<short>(content.a.x, content.b.x - 1), content.a.y, content.b.x, bottom);
				verticalScrollBar->locate(verticalRect);
			} else {
				TRect verticalRect(content.b.x, content.a.y, content.b.x, content.a.y);
				verticalScrollBar->locate(verticalRect);
			}
		}
		if (primaryEditor != nullptr) primaryEditor->changeBounds(editorBounds);
		if (primaryEditor != nullptr) primaryEditor->updateMetrics();
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
	configureSourcePaneScrollBarColors();
}

void MRBentoBox::hideSourcePaneChrome() noexcept {
	MRFileEditor *primaryEditor = getEditor();
	if (primaryEditor != nullptr) {
		primaryEditor->setScrollBarsAlwaysVisible(false);
		primaryEditor->hide();
	}
	if (horizontalEditorScrollBar() != nullptr) horizontalEditorScrollBar()->hide();
	if (verticalEditorScrollBar() != nullptr) verticalEditorScrollBar()->hide();
	if (editorIndicator() != nullptr) editorIndicator()->hide();
}

void MRBentoBox::configureSourcePaneScrollBarColors() noexcept {
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	MRFileEditor *sourceEditor = getEditor();
	const TColorAttr fillAttr = sourceEditor != nullptr ? sourceEditor->editorTextFillColor() : MREditWindow::mapColor(6);
	TColorAttr sourceMarkerAttr = MREditWindow::mapColor(activeLeafId == 0 ? 13 : 1);

	if (bentoMode == bbmFileCompare) sourceMarkerAttr = fillAttr;
	if (auto *scrollBar = dynamic_cast<MREditScrollBar *>(horizontalScrollBar)) scrollBar->setColorOverride(true, fillAttr, fillAttr, sourceMarkerAttr);
	if (auto *scrollBar = dynamic_cast<MREditScrollBar *>(verticalScrollBar)) scrollBar->setColorOverride(true, fillAttr, fillAttr, sourceMarkerAttr);
}

void MRBentoBox::drawSourcePaneScrollBars() noexcept {
	if (windowCloseInProgress) return;
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	const bool showWithoutRange = configuredScrollbarVisibility() == MRScrollbarVisibility::Always;
	MRFileEditor *sourceEditor = getEditor();
	const TColorAttr fillAttr = sourceEditor != nullptr ? sourceEditor->editorTextFillColor() : MREditWindow::mapColor(6);
	TColorAttr sourceMarkerAttr = MREditWindow::mapColor(activeLeafId == 0 ? 13 : 1);

	if (bentoMode == bbmFileCompare) sourceMarkerAttr = fillAttr;
	configureSourcePaneScrollBarColors();
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
	if (sourceEditor != nullptr) {
		TRect content = sourceEditor->getBounds();

		for (const BentoLeaf &leaf : leaves)
			if (leaf.visible && leaf.id == 0) {
				content = contentBounds(leaf.bounds);
				break;
			}
		const TRect editorBounds = sourceEditor->getBounds();
		if (editorBounds.a.y > content.a.y) fillRect(TRect(content.a.x, content.a.y, content.b.x, editorBounds.a.y));
		if (editorBounds.b.y < content.b.y) fillRect(TRect(content.a.x, editorBounds.b.y, content.b.x, content.b.y));
		if (editorBounds.a.x > content.a.x) fillRect(TRect(content.a.x, editorBounds.a.y, editorBounds.a.x, editorBounds.b.y));
		if (editorBounds.b.x < content.b.x) fillRect(TRect(editorBounds.b.x, editorBounds.a.y, content.b.x, editorBounds.b.y));
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
		TColorAttr baseAttr = sourceMarkerAttr;
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
					attr = baseAttr;
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
					attr = baseAttr;
				}
				buffer.moveChar(static_cast<ushort>(column), ch, attr, 1);
			}
			writeLine(bounds.a.x, bounds.a.y, width, 1, buffer);
		}
		return true;
	};

	normalizeScrollBarTrackGlyph(horizontalScrollBar);
	normalizeScrollBarTrackGlyph(verticalScrollBar);
	sourceScrollBarPaletteActive = true;
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
	sourceScrollBarPaletteActive = false;
}

void MRBentoBox::drawSharedEditorPanes() noexcept {
	bool hasSharedPane = false;

	for (const BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.spec.bufferPolicy == bpbSharedSourceBuffer && leaf.id != 0) hasSharedPane = true;
	if (!hasSharedPane) return;
	if (paneWindowForLeaf(0) == nullptr && getEditor() != nullptr) getEditor()->drawView();
	for (BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.pane != nullptr && leaf.spec.bufferPolicy == bpbSharedSourceBuffer) leaf.pane->drawView();
}

void MRBentoBox::refreshPaneContentProjection() noexcept {
	if (windowCloseInProgress || !hasPaneSplit()) return;
	bentoProjectionDirty |= bpdContent | bpdChrome;
	flushBentoProjection();
}

TColorAttr MRBentoBox::paneFrameColor(bool focused) {
	TColorAttr color = mapColor(focused ? 13 : 1);

	if (bentoMode == bbmFileCompare) {
		unsigned char configuredColor = 0;
		const unsigned char paletteIndex = focused ? kMrPaletteFileCompareFocusedPaneBorder : kMrPaletteFileComparePaneBorder;

		if (configuredColorSlotOverride(paletteIndex, configuredColor)) color = static_cast<TColorAttr>(configuredColor);
	}
	return color;
}

void MRBentoBox::drawPaneFrames() noexcept {
	if (windowCloseInProgress || !hasPaneSplit()) return;
	for (BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.pane != nullptr) leaf.pane->drawPaneScrollBars();
	for (std::size_t i = 0; i < leaves.size(); ++i)
		if (leaves[i].visible && i < paneFrameViews.size() && paneFrameViews[i] != nullptr) paneFrameViews[i]->drawView();
}

void MRBentoBox::refreshBentoColorTheme() noexcept {
	if (windowCloseInProgress || (state & sfVisible) == 0) return;
	if (paneWindowForLeaf(0) == nullptr) drawSourcePaneScrollBars();
	drawPaneFrames();
}
