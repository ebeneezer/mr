#include "MRBentoBox.hpp"
#include "MRBentoPaneFrameView.hpp"

#include "../MRSidekickEditor.hpp"
#include "../MRWindowSupport.hpp"

#include "../../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>

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

void MRBentoBox::layoutDesktopContents() {
	if (hasPaneSplit() && !isMinimized()) {
		layoutSplitPanes();
		return;
	}
	MREditWindow::layoutDesktopContents();
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
	secondaryPaneVisible = false;
}

void MRBentoBox::ensurePaneFrameViews() {
	while (paneFrameViews.size() < leaves.size()) {
		MRBentoPaneFrameView *view = new MRBentoPaneFrameView(TRect(0, 0, 0, 0));
		view->hide();
		// Pane chrome spans the leaf rectangle. Keep it behind pane content so
		// TVision does not classify the editor canvas as geometrically hidden.
		insertBefore(view, frame);
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
					leaf.pane->show();
					leaf.pane->changeBounds(inner);
				} else {
					if (leaf.pane->horizontalEditorScrollBar() != nullptr) leaf.pane->horizontalEditorScrollBar()->hide();
					if (leaf.pane->verticalEditorScrollBar() != nullptr) leaf.pane->verticalEditorScrollBar()->hide();
					leaf.pane->hide();
				}
			}
		}
		for (MRBentoPaneFrameView *view : paneFrameViews)
			if (view != nullptr) view->hide();
		activeLeafId = 0;
		maximizedLeafId = -1;
		secondaryPaneVisible = false;
		paneRoleDropList.hide();
		paneActionDropList.hide();
		if (primaryEditor != nullptr && paneWindowForLeaf(0) == nullptr) primaryEditor->setScrollBarsAlwaysVisible(false);
		MREditWindow::changeBounds(getBounds());
		if (primaryEditor != nullptr && paneWindowForLeaf(0) == nullptr) primaryEditor->drawView();
		for (BentoLeaf &leaf : leaves)
			if (leaf.visible && leaf.pane != nullptr) leaf.pane->drawView();
		paneLayoutChanged();
		bentoProjectionDirty = bpdNone;
		return;
	}
	ensurePaneFrameViews();
	for (BentoLeaf &leaf : leaves) leaf.visible = false;
	if (maximizedLeafId >= 0 && nodeIndexForLeaf(maximizedLeafId) >= 0) {
		if (rootNode >= 0) layoutNode(rootNode, inner);
		for (BentoLeaf &leaf : leaves) leaf.visible = false;
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
			const bool focused = leaf.id == activeLeafId && (state & sfFocused) != 0;
			if (view != nullptr) {
				view->changeBounds(leaf.bounds);
				view->setPane(leaf.id, paneTitleForLeaf(leaf).c_str(), leaf.id == 0 && bentoMode != bbmDocumentViewports, focused, leaf.id == maximizedLeafId, paneCloseActionEnabled(), paneMaximizeActionEnabled(), paneFrameColor(focused));
				view->show();
			}
			if (leaf.pane != nullptr) {
				if (leaf.id == 0) hideSourcePaneChrome();
				leaf.pane->show();
				leaf.pane->changeBounds(leaf.bounds);
			} else if (leaf.id == 0) {
				layoutSourcePaneChrome(leaf.bounds);
			}
		} else {
			if (leaf.id == 0) hideSourcePaneChrome();
			if (leaf.pane != nullptr) {
				if (leaf.pane->horizontalEditorScrollBar() != nullptr) leaf.pane->horizontalEditorScrollBar()->hide();
				if (leaf.pane->verticalEditorScrollBar() != nullptr) leaf.pane->verticalEditorScrollBar()->hide();
				leaf.pane->hide();
				if (maximizedLeafId >= 0 && nodeIndexForLeaf(leaf.id) >= 0) leaf.pane->changeBounds(leaf.bounds);
			}
			if (view != nullptr) view->hide();
		}
	}
	layoutGdbDebuggerValues(false);
	layoutGdbDebuggerValues(true);
	paneLayoutChanged();
	if (frame != nullptr) frame->drawView();
	if (primaryEditor != nullptr && paneWindowForLeaf(0) == nullptr) {
		primaryEditor->drawView();
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
	if ((dirty & bpdOverlay) != 0) updateTrackedCompilerSidekick();
	if ((dirty & (bpdChrome | bpdScrollBar)) != 0) drawPaneFrames();
	paneRoleDropList.drawOpenList();
	paneActionDropList.drawOpenList();
}

void MRBentoBox::layoutSourcePaneChrome(const TRect &bounds) noexcept {
	MRFileEditor *primaryEditor = getEditor();
	if (primaryEditor == nullptr) return;
	if ((primaryEditor->state & sfVisible) == 0) primaryEditor->show();
	primaryEditor->setScrollBarsAlwaysVisible(true);
	if (editorIndicator() != nullptr) editorIndicator()->hide();
	if (horizontalEditorScrollBar() != nullptr) {
		TRect horizontalBounds(bounds.a.x + 1, bounds.b.y - 1, bounds.b.x - 1, bounds.b.y);
		if (horizontalEditorScrollBar()->getBounds() != horizontalBounds) horizontalEditorScrollBar()->locate(horizontalBounds);
	}
	if (verticalEditorScrollBar() != nullptr) {
		TRect verticalBounds(bounds.b.x - 1, bounds.a.y + 1, bounds.b.x, bounds.b.y - 1);
		if (verticalEditorScrollBar()->getBounds() != verticalBounds) verticalEditorScrollBar()->locate(verticalBounds);
	}
	primaryEditor->changeBounds(contentBounds(bounds));
	primaryEditor->updateMetrics();
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

void MRBentoBox::drawSharedEditorPanes() noexcept {
	bool hasSharedPane = false;

	for (const BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.spec.bufferPolicy == bpbSharedSourceBuffer && leaf.id != 0) hasSharedPane = true;
	if (!hasSharedPane) return;
	if (paneWindowForLeaf(0) == nullptr && getEditor() != nullptr) getEditor()->drawView();
	for (BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.pane != nullptr && leaf.spec.bufferPolicy == bpbSharedSourceBuffer) leaf.pane->drawView();
}

void MRBentoBox::refreshPaneChromeProjection() noexcept {
	if (windowCloseInProgress || !hasPaneSplit()) return;
	bentoProjectionDirty |= bpdChrome;
	flushBentoProjection();
}

TColorAttr MRBentoBox::paneFrameColor(bool focused) {
	TColorAttr color = mapColor(focused ? 13 : 1);

	if (bentoMode == bbmFileCompare) {
		TColorAttr configuredColor;
		const unsigned char paletteIndex = focused ? kMrPaletteFileCompareFocusedPaneBorder : kMrPaletteFileComparePaneBorder;

		if (configuredColorSlotOverride(paletteIndex, configuredColor)) color = configuredColor;
	}
	return color;
}

void MRBentoBox::drawPaneFrames() noexcept {
	if (windowCloseInProgress || !hasPaneSplit()) return;
	updateActivePaneFrame();
	for (std::size_t i = 0; i < leaves.size(); ++i)
		if (leaves[i].visible && i < paneFrameViews.size() && paneFrameViews[i] != nullptr) {
			if (leaves[i].pane != nullptr) paneFrameViews[i]->drawOn(*leaves[i].pane);
			else paneFrameViews[i]->drawView();
		}
	for (BentoLeaf &leaf : leaves) {
		if (!leaf.visible) continue;
		TScrollBar *horizontal = leaf.pane != nullptr ? leaf.pane->horizontalEditorScrollBar() : horizontalEditorScrollBar();
		TScrollBar *vertical = leaf.pane != nullptr ? leaf.pane->verticalEditorScrollBar() : verticalEditorScrollBar();
		if (horizontal != nullptr && (horizontal->state & sfVisible) != 0) horizontal->drawView();
		if (vertical != nullptr && (vertical->state & sfVisible) != 0) vertical->drawView();
	}
}

void MRBentoBox::refreshBentoColorTheme() noexcept {
	if (windowCloseInProgress || (state & sfVisible) == 0) return;
	drawPaneFrames();
}
