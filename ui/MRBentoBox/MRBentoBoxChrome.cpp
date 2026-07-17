#include "MRBentoBox.hpp"
#include "MRBentoBoxRoleSupport.hpp"
#include "MRBentoPaneFrameView.hpp"

#include "../MRFrame.hpp"

#include "../../app/MRCommandRouter.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kMinimumPaneHeight = 3;
constexpr int kMinimumPaneWidth = 20;

} // namespace

void MRBentoBox::layoutNode(int nodeIndex, const TRect &bounds) {
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) return;
	BentoLayoutNode &node = layoutTree[nodeIndex];
	if (node.kind == blnPane) {
		for (BentoLeaf &leaf : leaves) {
			if (leaf.id == node.leafId) {
				leaf.bounds = bounds;
				leaf.visible = true;
				return;
			}
		}
		return;
	}
	const int position = currentDividerPosition(nodeIndex);
	if (node.orientation == bsoVertical) {
		TRect first(bounds.a.x, bounds.a.y, std::max<int>(bounds.a.x + 1, position), bounds.b.y);
		TRect second(std::min<int>(bounds.b.x - 1, position), bounds.a.y, bounds.b.x, bounds.b.y);
		layoutNode(node.firstChild, first);
		layoutNode(node.secondChild, second);
	} else {
		TRect first(bounds.a.x, bounds.a.y, bounds.b.x, std::max<int>(bounds.a.y + 1, position));
		TRect second(bounds.a.x, std::min<int>(bounds.b.y - 1, position), bounds.b.x, bounds.b.y);
		layoutNode(node.firstChild, first);
		layoutNode(node.secondChild, second);
	}
}

void MRBentoBox::postCloseCommand() noexcept {
	TEvent event;

	windowCloseInProgress = true;
	std::memset(&event, 0, sizeof(event));
	event.what = evCommand;
	event.message.command = cmClose;
	event.message.infoPtr = this;
	putEvent(event);
}

void MRBentoBox::closePane(int leafId) noexcept {
	if (leafId == 0) {
		postCloseCommand();
		return;
	}
	if (maximizedLeafId == leafId) maximizedLeafId = -1;
	collapseLeafNode(leafId);
	if (activeLeafId == leafId || nodeIndexForLeaf(activeLeafId) < 0) setActivePane(0);
	layoutSplitPanes();
	mrMarkWorkspaceAutosaveDirty("bento pane close", this);
}

void MRBentoBox::closeSecondaryPane() noexcept {
	int toolLeaf = firstToolLeafId();
	bool changed = false;
	while (toolLeaf >= 0) {
		collapseLeafNode(toolLeaf);
		changed = true;
		toolLeaf = firstToolLeafId();
	}
	setActivePane(0);
	layoutSplitPanes();
	if (changed) mrMarkWorkspaceAutosaveDirty("bento secondary pane", this);
}

void MRBentoBox::showPaneRoleList(TPoint, int targetLeafId) {
	const int listWidth = 17;
	const int listHeight = 6;
	const bool openingRoleList = !paneRoleDropList.visible();
	TRect paneRect = paneBoundsForLeaf(targetLeafId);
	MRBentoPaneFrameView *chromeView = nullptr;

	if (!titleMenuEnabledForLeaf(targetLeafId)) return;
	for (MRBentoPaneFrameView *view : paneFrameViews)
		if (view != nullptr && view->paneLeafId() == targetLeafId) chromeView = view;
	TRect localAnchor = chromeView != nullptr ? chromeView->paneRoleListAnchor(listWidth) : TRect(1, 0, 1 + listWidth, 0);
	int left = std::clamp<int>(paneRect.a.x + localAnchor.a.x, 1, std::max(1, size.x - listWidth - 1));
	int top = std::clamp<int>(paneRect.a.y + localAnchor.a.y, 1, std::max(1, size.y - listHeight - 1));

	paneActionDropList.hide();
	fileCompareActionDropList.hide();
	pendingPaneRoleTargetLeafId = targetLeafId;
	paneRoleListAnchor = TRect(left, top, left + listWidth, top);
	if (openingRoleList && chromeView != nullptr) chromeView->setPaneRoleListTitleOpen(true, paneRoleListAnchor);
	paneRoleDropList.toggle(*this, paneRoleListAnchor, paneRoleChoices(), mr::bento::paneRoleTitle(roleForLeaf(targetLeafId)), this, mr::bento::cmPaneRoleAccepted, listHeight);
	if (!openingRoleList) updatePaneRoleListChrome();
}

void MRBentoBox::showPaneActionList() {
	const int listWidth = 9;
	const int listHeight = 3;
	const short selectedIndex = paneRoleDropList.selectedIndex();
	const int selectedRow = paneRoleListAnchor.a.y + std::max<short>(0, selectedIndex);
	int left = paneRoleListAnchor.a.x - listWidth;
	int top = std::clamp<int>(selectedRow, 1, std::max(1, size.y - listHeight - 1));
	if (left < 1) left = std::clamp<int>(paneRoleListAnchor.b.x, 1, std::max(1, size.x - listWidth - 1));
	TRect anchor(left, top, left + listWidth, top);
	fileCompareActionDropList.hide();
	paneActionDropList.hide();
	paneActionDropList.toggle(*this, anchor, paneActionChoices(), mr::bento::paneActionReplace(), this, mr::bento::cmPaneActionAccepted, listHeight);
}

void MRBentoBox::showFileCompareActionList(TPoint globalMouse, int targetLeafId) {
	const int listWidth = 12;
	const int listHeight = 3;
	const TPoint localMouse = makeLocal(globalMouse);
	const int left = std::clamp<int>(localMouse.x, 1, std::max(1, size.x - listWidth - 1));
	const int top = std::clamp<int>(localMouse.y, 1, std::max(1, size.y - listHeight - 1));
	const TRect anchor(left, top, left + listWidth, top);
	const std::vector<std::string> actions{mr::bento::fileCompareActionNext(), mr::bento::fileCompareActionPrevious(), mr::bento::fileCompareActionApply()};

	if (bentoMode != bbmFileCompare || !mr::bento::paneRoleIsDiff(roleForLeaf(targetLeafId))) return;
	paneRoleDropList.hide();
	paneActionDropList.hide();
	updatePaneRoleListChrome();
	pendingFileCompareActionLeafId = targetLeafId;
	pendingFileCompareActionGroupIndex = -1;
	const MRBentoPaneRole targetRole = roleForLeaf(targetLeafId);
	MREditWindow *targetWindow = targetLeafId == 0 ? static_cast<MREditWindow *>(this) : paneWindowForLeaf(targetLeafId);
	MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	if (targetEditor != nullptr) {
		const std::size_t clickedOffset = targetEditor->offsetForGlobalPoint(globalMouse);
		const std::size_t clickedLine = targetEditor->lineIndexOfOffset(clickedOffset);
		pendingFileCompareActionGroupIndex = fileCompareChangeGroupIndexAtLine(targetRole, clickedLine, fileComparePanesEditable());
	}
	setActivePane(targetLeafId);
	fileCompareActionDropList.toggle(*this, anchor, actions, mr::bento::fileCompareActionApply(), this, mr::bento::cmFileComparePaneActionAccepted, listHeight);
}

void MRBentoBox::acceptPaneRoleChoice() {
	std::string roleTitle;
	if (!paneRoleDropList.selectedValue(roleTitle)) return;
	pendingPaneRole = mr::bento::paneRoleForTitle(roleTitle);
	showPaneActionList();
}

void MRBentoBox::acceptPaneActionChoice() {
	std::string action;
	if (!paneActionDropList.acceptSelection(action)) return;
	const MRBentoPanePlacement placement = panePlacementForAction(action);
	paneRoleDropList.hide();
	updatePaneRoleListChrome();
	static_cast<void>(placePaneRoleInContext(pendingPaneRole, placement, pendingPaneRoleTargetLeafId));
	pendingPaneRoleTargetLeafId = activeLeafId;
}

void MRBentoBox::acceptFileCompareActionChoice() {
	std::string action;
	if (!fileCompareActionDropList.acceptSelection(action)) return;
	if (nodeIndexForLeaf(pendingFileCompareActionLeafId) < 0 || !mr::bento::paneRoleIsDiff(roleForLeaf(pendingFileCompareActionLeafId))) return;
	setActivePane(pendingFileCompareActionLeafId);
	if (action == mr::bento::fileCompareActionNext()) {
		static_cast<void>(navigateFileCompareChange(true));
		return;
	}
	if (action == mr::bento::fileCompareActionPrevious()) {
		static_cast<void>(navigateFileCompareChange(false));
		return;
	}
	if (action == mr::bento::fileCompareActionApply()) {
		const MRBentoPaneRole role = roleForLeaf(pendingFileCompareActionLeafId);
		const bool originalToCompare = role == bprDiffOriginal;
		const std::size_t groupIndex = pendingFileCompareActionGroupIndex >= 0 ? static_cast<std::size_t>(pendingFileCompareActionGroupIndex) : fileCompareChangeGroups.size();
		if (groupIndex < fileCompareChangeGroups.size())
			static_cast<void>(applyFileCompareChangeGroup(originalToCompare, fileCompareChangeGroups[groupIndex]));
		else
			static_cast<void>(applyFileCompareChange(originalToCompare));
		pendingFileCompareActionGroupIndex = -1;
	}
}

bool MRBentoBox::handlePaneDropListEvent(TEvent &event) {
	const bool roleListVisible = paneRoleDropList.visible();
	const bool actionListVisible = paneActionDropList.visible();
	const bool fileCompareActionListVisible = fileCompareActionDropList.visible();
	if (!roleListVisible && !actionListVisible && !fileCompareActionListVisible) return false;
	if (fileCompareActionDropList.handleOpenListEvent(event, false)) return true;
	if (paneActionDropList.handleOpenListEvent(event, false)) {
		updatePaneRoleListChrome();
		return true;
	}
	if (paneRoleDropList.handleOpenListEvent(event, false)) {
		updatePaneRoleListChrome();
		return true;
	}
	if (event.what == evMouseDown && !paneRoleDropList.containsPoint(event.mouse.where) && !paneActionDropList.containsPoint(event.mouse.where) && !fileCompareActionDropList.containsPoint(event.mouse.where)) {
		paneRoleDropList.hide();
		paneActionDropList.hide();
		fileCompareActionDropList.hide();
		updatePaneRoleListChrome();
		clearEvent(event);
		return true;
	}
	if (event.what == evMouseDown && (event.mouse.buttons & mbRightButton) != 0 && paneRoleDropList.containsPoint(event.mouse.where)) {
		paneRoleDropList.focusIndex(paneRoleIndexAt(event.mouse.where));
		acceptPaneRoleChoice();
		clearEvent(event);
		return true;
	}
	if (event.what == evMouseDown && paneRoleDropList.containsPoint(event.mouse.where)) {
		TWindow::handleEvent(event);
		acceptPaneRoleChoice();
		clearEvent(event);
		return true;
	}
	if (event.what == evMouseDown && paneActionDropList.containsPoint(event.mouse.where)) {
		TWindow::handleEvent(event);
		acceptPaneActionChoice();
		clearEvent(event);
		return true;
	}
	if (event.what == evMouseDown && fileCompareActionDropList.containsPoint(event.mouse.where)) {
		TWindow::handleEvent(event);
		acceptFileCompareActionChoice();
		clearEvent(event);
		return true;
	}
	if (event.what == evKeyDown) {
		TWindow::handleEvent(event);
		clearEvent(event);
		return true;
	}
	return false;
}

bool MRBentoBox::handleOuterFrameCloseMouse(TEvent &event) {
	if (event.what != evMouseDown || (event.mouse.buttons & mbLeftButton) == 0 || (flags & wfClose) == 0) return false;
	TPoint mouse = makeLocal(event.mouse.where);
	if (mouse.y != 0 || mouse.x < 2 || mouse.x > 4) return false;
	while (mouseEvent(event, evMouse))
		;
	mouse = makeLocal(event.mouse.where);
	if (mouse.y == 0 && mouse.x >= 2 && mouse.x <= 4) postCloseCommand();
	clearEvent(event);
	return true;
}

void MRBentoBox::updatePaneRoleListChrome() noexcept {
	for (MRBentoPaneFrameView *view : paneFrameViews)
		if (view != nullptr) view->setPaneRoleListTitleOpen(false, paneRoleListAnchor);
	for (MRBentoPaneFrameView *view : paneFrameViews)
		if (view != nullptr && view->paneLeafId() == pendingPaneRoleTargetLeafId) view->setPaneRoleListTitleOpen(paneRoleDropList.visible(), paneRoleListAnchor);
	bentoProjectionDirty |= bpdChrome;
}

short MRBentoBox::paneRoleIndexAt(TPoint globalMouse) {
	const TPoint localMouse = makeLocal(globalMouse);
	const std::vector<std::string> roles = paneRoleChoices();
	const int maxIndex = std::max(0, static_cast<int>(roles.size()) - 1);
	return static_cast<short>(std::clamp(localMouse.y - paneRoleListAnchor.a.y, 0, maxIndex));
}

void MRBentoBox::dragDivider(TEvent &event, int nodeIndex) noexcept {
	if (maximizedLeafId >= 0) return;
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) return;
	const TPoint initialLocal = makeLocal(event.mouse.where);
	int verticalNode = -1;
	int horizontalNode = -1;

	for (int candidate = 0; candidate < static_cast<int>(layoutTree.size()); ++candidate) {
		const BentoLayoutNode &node = layoutTree[candidate];
		if (node.kind != blnSplit) continue;
		const TRect bounds = nodeBounds(candidate);
		const int position = currentDividerPosition(candidate);
		if (node.orientation == bsoVertical) {
			if ((initialLocal.x == position || initialLocal.x == position - 1) && initialLocal.y >= bounds.a.y && initialLocal.y < bounds.b.y) verticalNode = candidate;
		} else if ((initialLocal.y == position || initialLocal.y == position - 1) && initialLocal.x >= bounds.a.x && initialLocal.x < bounds.b.x)
			horizontalNode = candidate;
	}
	if (verticalNode >= 0 && horizontalNode >= 0) {
		const int initialVerticalPosition = currentDividerPosition(verticalNode);
		const int initialHorizontalPosition = currentDividerPosition(horizontalNode);
		const int verticalDragOffset = initialLocal.x - initialVerticalPosition;
		const int horizontalDragOffset = initialLocal.y - initialHorizontalPosition;

		while (mouseEvent(event, evMouseMove | evMouseAuto | evMouseUp)) {
			if (event.what == evMouseUp) break;
			const TPoint local = makeLocal(event.mouse.where);
			const bool verticalChanged = projectPaneDividerPosition(verticalNode, local.x - verticalDragOffset);
			const bool horizontalChanged = projectPaneDividerPosition(horizontalNode, local.y - horizontalDragOffset);

			if (verticalChanged || horizontalChanged) layoutSplitPanes();
		}
		if (currentDividerPosition(verticalNode) != initialVerticalPosition || currentDividerPosition(horizontalNode) != initialHorizontalPosition) mrMarkWorkspaceAutosaveDirty("bento divider", this);
		return;
	}
	const int initialPosition = currentDividerPosition(nodeIndex);
	const bool vertical = layoutTree[nodeIndex].orientation == bsoVertical;
	const int dragOffset = (vertical ? initialLocal.x : initialLocal.y) - initialPosition;
	if (event.what != evMouseDown) {
		const TPoint local = makeLocal(event.mouse.where);
		setDividerPosition(nodeIndex, (vertical ? local.x : local.y) - dragOffset, false);
	} else
		setDividerPosition(nodeIndex, (vertical ? initialLocal.x : initialLocal.y) - dragOffset, false);
	while (mouseEvent(event, evMouseMove | evMouseAuto | evMouseUp)) {
		if (event.what == evMouseUp) break;
		const TPoint local = makeLocal(event.mouse.where);
		setDividerPosition(nodeIndex, (vertical ? local.x : local.y) - dragOffset, false);
	}
	if (currentDividerPosition(nodeIndex) != initialPosition) mrMarkWorkspaceAutosaveDirty("bento divider", this);
}

void MRBentoBox::setDividerY(int y) noexcept {
	setDividerPosition(y);
}

void MRBentoBox::setDividerPosition(int position) noexcept {
	setDividerPosition(rootNode, position, true);
}

void MRBentoBox::setDividerPosition(int nodeIndex, int position, bool markWorkspace) noexcept {
	if (!projectPaneDividerPosition(nodeIndex, position)) return;
	layoutSplitPanes();
	if (markWorkspace) mrMarkWorkspaceAutosaveDirty("bento divider", this);
}

bool MRBentoBox::projectPaneDividerPosition(int nodeIndex, int position) noexcept {
	return setPaneDividerPositionForLayout(nodeIndex, position);
}

int MRBentoBox::paneDividerPosition(int nodeIndex) const noexcept {
	return currentDividerPosition(nodeIndex);
}

bool MRBentoBox::setPaneDividerPositionForLayout(int nodeIndex, int position) noexcept {
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) return false;
	if (layoutTree[nodeIndex].kind != blnSplit) return false;
	const int clampedPosition = clampedDividerPosition(nodeIndex, position);
	if (layoutTree[nodeIndex].dividerPosition == clampedPosition) return false;
	layoutTree[nodeIndex].dividerPosition = clampedPosition;
	return true;
}

void MRBentoBox::setActivePane(int leafId) noexcept {
	if (nodeIndexForLeaf(leafId) < 0) return;
	const int previousLeafId = activeLeafId;
	if (previousLeafId != leafId) {
		MRPaneEditWindow *previousPane = paneWindowForLeaf(previousLeafId);
		if (previousPane != nullptr && !previousPane->completeTransientInput()) return;
	}
	activeLeafId = leafId;
	MRPaneEditWindow *pane = paneWindowForLeaf(leafId);

	if (pane == nullptr) {
		MRFileEditor *sourceEditor = getEditor();
		if (sourceEditor != nullptr && sourceEditor->getState(sfVisible)) sourceEditor->select();
	} else {
		MRFileEditor *paneEditor = pane->getEditor();
		if (pane != nullptr && pane->getState(sfVisible)) pane->select();
		if (paneEditor != nullptr && paneEditor->getState(sfVisible)) paneEditor->select();
	}
	updateActivePaneFrame();
	activePaneRoleChanged(roleForLeaf(leafId));
}

void MRBentoBox::updateActivePaneFrame() noexcept {
	for (std::size_t i = 0; i < leaves.size() && i < paneFrameViews.size(); ++i) {
		MRBentoPaneFrameView *view = paneFrameViews[i];
		if (view == nullptr || !leaves[i].visible) continue;
		const bool focused = leaves[i].id == activeLeafId && (state & sfFocused) != 0;
		if (leaves[i].pane != nullptr) leaves[i].pane->setPaneFocused(focused);
			view->setPane(leaves[i].id, paneTitleForLeaf(leaves[i]).c_str(), leaves[i].id == 0 && bentoMode != bbmDocumentViewports, focused, leaves[i].id == maximizedLeafId, paneCloseActionEnabled(), paneMaximizeActionEnabled(), paneFrameColor(focused));
	}
}

void MRBentoBox::setActivePaneForMouse(TPoint globalMouse) noexcept {
	const int leaf = leafAt(makeLocal(globalMouse));
	if (leaf >= 0) setActivePane(leaf);
}

void MRBentoBox::toggleLeafMaximized(int leafId) noexcept {
	if (leafId < 0 || (leafId == 0 && bentoMode != bbmDocumentViewports && !hasPaneSplit()) || nodeIndexForLeaf(leafId) < 0) return;
	setActivePane(leafId);
	if (activeLeafId != leafId) return;
	maximizedLeafId = maximizedLeafId == leafId ? -1 : leafId;
	layoutSplitPanes();
	mrMarkWorkspaceAutosaveDirty("bento maximize", this);
}

bool MRBentoBox::handleDividerChromeMouse(TEvent &event) {
	if (event.what != evMouseDown) return false;
	const TPoint localMouse = makeLocal(event.mouse.where);
	for (std::size_t i = 0; i < leaves.size() && i < paneFrameViews.size(); ++i) {
		MRBentoPaneFrameView *view = paneFrameViews[i];
		if (view == nullptr || !leaves[i].visible) continue;
		TRect bounds = leaves[i].bounds;
		if (!pointInRect(localMouse, bounds)) continue;
		TPoint paneMouse;
		paneMouse.x = localMouse.x - bounds.a.x;
		paneMouse.y = localMouse.y - bounds.a.y;
		MRBentoPaneFrameView::HitKind hit = view->hitTest(paneMouse);
		const int leafId = view->paneLeafId();
		switch (hit) {
			case MRBentoPaneFrameView::hitTitle:
				if ((event.mouse.buttons & (mbLeftButton | mbRightButton)) == 0) return false;
				setActivePane(leafId);
				if (titleMenuEnabledForLeaf(leafId)) showPaneRoleList(event.mouse.where, leafId);
				return true;
			case MRBentoPaneFrameView::hitClose:
				if ((event.mouse.buttons & mbLeftButton) == 0) return false;
				if (!paneCloseActionEnabled()) return true;
				closePane(leafId);
				return true;
			case MRBentoPaneFrameView::hitMaximize:
				if ((event.mouse.buttons & mbLeftButton) == 0) return false;
				if (!paneMaximizeActionEnabled()) return true;
				toggleLeafMaximized(leafId);
				return true;
			case MRBentoPaneFrameView::hitNone:
				return false;
		}
	}
	return false;
}

TRect MRBentoBox::paneBoundsForLeaf(int leafId) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.id == leafId) return leaf.bounds;
	return TRect(0, 0, 0, 0);
}

int MRBentoBox::defaultDividerPosition() const noexcept {
	return defaultDividerPosition(rootNode);
}

int MRBentoBox::defaultDividerPosition(int nodeIndex) const noexcept {
	const TRect bounds = nodeBounds(nodeIndex);
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) return 0;
	if (layoutTree[nodeIndex].orientation == bsoVertical) return bounds.a.x + std::max(1, bounds.b.x - bounds.a.x) / 2;
	return bounds.a.y + std::max(1, bounds.b.y - bounds.a.y) / 2;
}

int MRBentoBox::clampedDividerPosition(int position) const noexcept {
	return clampedDividerPosition(rootNode, position);
}

int MRBentoBox::clampedDividerPosition(int nodeIndex, int position) const noexcept {
	const TRect bounds = nodeBounds(nodeIndex);
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) return position;
	if (layoutTree[nodeIndex].orientation == bsoVertical) {
		const int minX = std::min<int>(bounds.b.x - 1, bounds.a.x + kMinimumPaneWidth);
		const int maxX = std::max(minX, bounds.b.x - kMinimumPaneWidth);
		return std::clamp(position, minX, maxX);
	}
	const int minY = std::min<int>(bounds.b.y - 1, bounds.a.y + kMinimumPaneHeight);
	const int maxY = std::max(minY, bounds.b.y - kMinimumPaneHeight);
	return std::clamp(position, minY, maxY);
}

int MRBentoBox::currentDividerPosition() const noexcept {
	return currentDividerPosition(rootNode);
}

int MRBentoBox::currentDividerPosition(int nodeIndex) const noexcept {
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) return 0;
	const int value = layoutTree[nodeIndex].dividerPosition;
	return clampedDividerPosition(nodeIndex, value > 0 ? value : defaultDividerPosition(nodeIndex));
}

bool MRBentoBox::hasPaneSplit() const noexcept {
	return rootNode >= 0 && rootNode < static_cast<int>(layoutTree.size()) && layoutTree[rootNode].kind == blnSplit;
}

bool MRBentoBox::pointInRect(TPoint point, const TRect &rect) const noexcept {
	return point.x >= rect.a.x && point.x < rect.b.x && point.y >= rect.a.y && point.y < rect.b.y;
}

int MRBentoBox::leafAt(TPoint point) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.visible && pointInRect(point, leaf.bounds)) return leaf.id;
	return -1;
}

int MRBentoBox::nodeAtDivider(TPoint point) const noexcept {
	if (maximizedLeafId >= 0) return -1;
	for (int i = 0; i < static_cast<int>(layoutTree.size()); ++i) {
		const BentoLayoutNode &node = layoutTree[i];
		if (node.kind != blnSplit) continue;
		const TRect bounds = nodeBounds(i);
		const int position = currentDividerPosition(i);
		if (node.orientation == bsoVertical) {
			if ((point.x == position || point.x == position - 1) && point.y >= bounds.a.y && point.y < bounds.b.y) return i;
		} else if ((point.y == position || point.y == position - 1) && point.x >= bounds.a.x && point.x < bounds.b.x)
			return i;
	}
	return -1;
}

MRPaneEditWindow *MRBentoBox::paneWindowForLeaf(int leafId) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.id == leafId) return leaf.pane;
	return nullptr;
}

MRBentoPaneRole MRBentoBox::roleForLeaf(int leafId) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.id == leafId) return leaf.role;
	return bprCompilerOutput;
}

bool MRBentoBox::titleMenuEnabledForLeaf(int leafId) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.id == leafId) return leaf.spec.titleMenu != nullptr;
	return false;
}

int MRBentoBox::firstToolLeafId() const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.id != 0 && leaf.visible) return leaf.id;
	return -1;
}

int MRBentoBox::leafIdForRole(MRBentoPaneRole role) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.role == role) return leaf.id;
	return -1;
}

int MRBentoBox::nodeIndexForLeaf(int leafId) const noexcept {
	std::vector<int> stack;

	if (rootNode >= 0) stack.push_back(rootNode);
	while (!stack.empty()) {
		const int nodeIndex = stack.back();
		stack.pop_back();
		if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) continue;
		const BentoLayoutNode &node = layoutTree[nodeIndex];
		if (node.kind == blnPane && node.leafId == leafId) return nodeIndex;
		if (node.kind == blnSplit) {
			stack.push_back(node.firstChild);
			stack.push_back(node.secondChild);
		}
	}
	return -1;
}

int MRBentoBox::parentNodeOf(int childNodeIndex) const noexcept {
	for (int i = 0; i < static_cast<int>(layoutTree.size()); ++i)
		if (layoutTree[i].kind == blnSplit && (layoutTree[i].firstChild == childNodeIndex || layoutTree[i].secondChild == childNodeIndex)) return i;
	return -1;
}

int MRBentoBox::viewportNumberForLeaf(int leafId) const noexcept {
	if (bentoMode != bbmDocumentViewports || rootNode < 0) return 0;

	int number = 0;
	std::vector<int> stack;
	stack.push_back(rootNode);
	while (!stack.empty()) {
		const int nodeIndex = stack.back();
		stack.pop_back();
		if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) continue;
		const BentoLayoutNode &node = layoutTree[nodeIndex];
		if (node.kind == blnPane) {
			++number;
			if (node.leafId == leafId) return number;
		} else if (node.kind == blnSplit) {
			stack.push_back(node.secondChild);
			stack.push_back(node.firstChild);
		}
	}
	return 0;
}

MRBentoPaneSpec MRBentoBox::paneSpecForRole(MRBentoPaneRole role) const noexcept {
	const MRBentoPaneTitleMenuSpec *titleMenu = mr::bento::paneRoleTitleMenu(bentoMode);
	switch (role) {
		case bprSource:
			return MRBentoPaneSpec(bprSource, bpbSharedSourceBuffer, false, false, false, false, titleMenu);
		case bprSplitEditor:
			return MRBentoPaneSpec(bprSplitEditor, bpbSharedSourceBuffer, false, false, false, bentoMode == bbmDocumentViewports, titleMenu);
		case bprDiffCompare:
			return MRBentoPaneSpec(role, bpbOwnBuffer, !fileComparePanesEditable(), false, true, true, titleMenu);
		case bprDiffOriginal:
			return MRBentoPaneSpec(role, bpbOwnBuffer, !fileComparePanesEditable(), true, true, true, titleMenu);
		case bprCompilerOutput:
		case bprAppOutput:
		case bprProblems:
		case bprDebuggerOutput:
		case bprWatches:
		case bprVariables:
		default:
			return MRBentoPaneSpec(role, bpbOwnBuffer, true, true, true, true, titleMenu);
	}
}

std::string MRBentoBox::paneTitleForLeaf(const BentoLeaf &leaf) const {
	if (bentoMode == bbmDocumentViewports) {
		const int viewportNumber = viewportNumberForLeaf(leaf.id);
		return "Viewport #" + std::to_string(std::max(1, viewportNumber));
	}
	if (bentoMode == bbmFileCompare && mr::bento::paneRoleIsDiff(leaf.role)) {
		const std::string status = fileCompareStatusForLeaf(leaf);
		if (!status.empty()) return std::string(mr::bento::paneRoleTitle(leaf.role)) + " [" + status + "]";
	}
	if (leaf.role == bprCompilerOutput && !compilerOutputStatus.empty()) return std::string(mr::bento::paneRoleTitle(leaf.role)) + " [" + compilerOutputStatus + "]";
	if (leaf.role == bprProblems && !compilerProblemsStatus.empty()) return std::string(mr::bento::paneRoleTitle(leaf.role)) + " [" + compilerProblemsStatus + "]";
	if (leaf.role == bprStructure && !structureOutlineStatus.empty()) return std::string(mr::bento::paneRoleTitle(leaf.role)) + " [" + structureOutlineStatus + "]";
	if (leaf.role == bprFunctions && !functionsOutlineStatus.empty()) return std::string(mr::bento::paneRoleTitle(leaf.role)) + " [" + functionsOutlineStatus + "]";
	if (leaf.role == bprDebuggerOutput && !macroDebuggerStatus.empty()) return std::string(mr::bento::paneRoleTitle(leaf.role)) + " [" + macroDebuggerStatus + "]";
	if (!leaf.title.empty()) return leaf.title;
	return mr::bento::paneRoleTitle(leaf.role);
}

TRect MRBentoBox::paneLayoutBounds() const noexcept {
	TRect inner = getExtent();

	if (!(fullscreenPresentation() && hasPaneSplit())) inner.grow(-1, -1);
	return inner;
}

TRect MRBentoBox::nodeBounds(int nodeIndex) const noexcept {
	TRect inner = paneLayoutBounds();
	if (nodeIndex == rootNode) return inner;
	for (const BentoLeaf &leaf : leaves)
		if (nodeIndexForLeaf(leaf.id) == nodeIndex) return leaf.bounds;
	int parent = parentNodeOf(nodeIndex);
	if (parent < 0) return inner;
	TRect parentBounds = nodeBounds(parent);
	const int position = currentDividerPosition(parent);
	if (layoutTree[parent].orientation == bsoVertical) {
		if (layoutTree[parent].firstChild == nodeIndex) return TRect(parentBounds.a.x, parentBounds.a.y, position, parentBounds.b.y);
		return TRect(position, parentBounds.a.y, parentBounds.b.x, parentBounds.b.y);
	}
	if (layoutTree[parent].firstChild == nodeIndex) return TRect(parentBounds.a.x, parentBounds.a.y, parentBounds.b.x, position);
	return TRect(parentBounds.a.x, position, parentBounds.b.x, parentBounds.b.y);
}

TRect MRBentoBox::contentBounds(const TRect &paneBounds) const noexcept {
	TRect content = paneBounds;
	if (content.b.x - content.a.x > 2 && content.b.y - content.a.y > 2) content.grow(-1, -1);
	return content;
}

int MRBentoBox::createToolLeaf(MRBentoPaneRole role) {
	return createPaneLeaf(paneSpecForRole(role));
}

int MRBentoBox::createPaneLeaf(const MRBentoPaneSpec &spec) {
	BentoLeaf leaf;
	leaf.id = nextLeafId++;
	leaf.role = spec.role;
	leaf.spec = spec;
	leaf.title = bentoMode == bbmDocumentViewports ? "" : titleForPaneRole(spec.role);
	const std::string initialTitle = bentoMode == bbmDocumentViewports ? "Viewport" : leaf.title;
	leaf.pane = createPaneWindow(TRect(0, 0, 1, 1), initialTitle.c_str(), number, spec);
	if (leaf.pane == nullptr) return -1;
	leaf.pane->setPaneSpec(spec, getEditor());
	leaf.visible = true;
	if (secondaryPane == nullptr) secondaryPane = leaf.pane;
	leaves.push_back(leaf);
	insert(leaf.pane);
	return leaf.id;
}

int MRBentoBox::createLeafNode(int leafId) {
	BentoLayoutNode node;
	node.kind = blnPane;
	node.leafId = leafId;
	layoutTree.push_back(node);
	return static_cast<int>(layoutTree.size()) - 1;
}

int MRBentoBox::splitLeafNode(int leafId, BentoSplitOrientation orientation, MRBentoPaneRole newRole) {
	return splitLeafNode(leafId, orientation, paneSpecForRole(newRole));
}

int MRBentoBox::splitLeafNode(int leafId, BentoSplitOrientation orientation, const MRBentoPaneSpec &spec) {
	int targetNode = nodeIndexForLeaf(leafId);
	if (targetNode < 0) return -1;
	int newLeaf = createPaneLeaf(spec);
	int existingLeafNode = createLeafNode(leafId);
	int newLeafNode = createLeafNode(newLeaf);
	BentoLayoutNode &target = layoutTree[targetNode];
	target.kind = blnSplit;
	target.orientation = orientation;
	target.firstChild = existingLeafNode;
	target.secondChild = newLeafNode;
	target.leafId = -1;
	target.dividerPosition = defaultDividerPosition(targetNode);
	setActivePane(newLeaf);
	layoutSplitPanes();
	return newLeaf;
}

void MRBentoBox::collapseLeafNode(int leafId) noexcept {
	int leafNode = nodeIndexForLeaf(leafId);
	if (leafNode < 0) return;
	int parent = parentNodeOf(leafNode);
	if (parent < 0) return;
	int survivor = layoutTree[parent].firstChild == leafNode ? layoutTree[parent].secondChild : layoutTree[parent].firstChild;
	layoutTree[parent] = layoutTree[survivor];
	for (BentoLeaf &leaf : leaves) {
		if (leaf.id == leafId) {
			leaf.visible = false;
			if (leaf.pane != nullptr) leaf.pane->hide();
		}
	}
	if (secondaryPane != nullptr) {
		bool stillVisible = false;
		for (const BentoLeaf &leaf : leaves)
			if (leaf.pane == secondaryPane && leaf.visible) stillVisible = true;
		if (!stillVisible) secondaryPane = paneWindowForLeaf(firstToolLeafId());
	}
	if (activeLeafId == leafId) setActivePane(0);
	if (maximizedLeafId == leafId) maximizedLeafId = -1;
}

std::vector<std::string> MRBentoBox::paneRoleChoices() const {
	return mr::bento::paneRoleChoices(bentoMode);
}

std::vector<std::string> MRBentoBox::paneActionChoices() const {
	return mr::bento::paneActionChoices();
}

MRBentoPanePlacement MRBentoBox::panePlacementForAction(const std::string &action) const noexcept {
	return mr::bento::panePlacementForAction(action);
}

TFrame *MRBentoBox::initFrame(TRect bounds) {
	return new MRFrame(bounds);
}
