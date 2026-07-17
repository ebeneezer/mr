#include "MRBentoBox.hpp"
#include "MRBentoPaneFrameView.hpp"
#include "MRBentoBoxRoleSupport.hpp"

#include "../MRFrame.hpp"
#include "../MRSidekickEditor.hpp"
#include "../MRWindowSupport.hpp"

#include "../../app/MRCommandRouter.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace {

bool bentoWorkspaceModeIsValid(int mode) noexcept {
	return mode == bbmToolWorkspace || mode == bbmDocumentViewports || mode == bbmFileCompare;
}

bool bentoWorkspaceRoleIsValid(int role) noexcept {
	return role >= bprSource && role <= bprExtensionLast;
}

} // namespace

MRBentoBox::BentoLayoutNode::BentoLayoutNode() noexcept : kind(blnPane), orientation(bsoHorizontal), dividerPosition(0), firstChild(-1), secondChild(-1), leafId(-1) {
}

MRBentoBox::BentoLeaf::BentoLeaf() noexcept : id(-1), role(bprCompilerOutput), spec(), title(), pane(nullptr), bounds(0, 0, 0, 0), visible(false) {
}
void MRBentoBox::showSecondaryPane() noexcept {
	if (firstToolLeafId() < 0) {
		int sourceNode = nodeIndexForLeaf(0);
		if (sourceNode >= 0) static_cast<void>(splitLeafNode(0, bsoHorizontal, bprCompilerOutput));
	}
	secondaryPaneVisible = firstToolLeafId() >= 0;
	layoutSplitPanes();
}

bool MRBentoBox::ensureBuildDiagnosticsPanes(MREditWindow *&outputWindow, MREditWindow *&problemsWindow) {
	const int previousActiveLeaf = activeLeafId;
	int outputLeaf = leafIdForRole(bprCompilerOutput);

	if (bentoMode == bbmDocumentViewports) {
		bentoMode = bbmToolWorkspace;
		for (BentoLeaf &leaf : leaves) {
			leaf.spec = paneSpecForRole(leaf.role);
			leaf.title = titleForPaneRole(leaf.role);
			if (leaf.pane != nullptr) leaf.pane->setPaneSpec(leaf.spec, getEditor());
		}
	}
	if (outputLeaf < 0) {
		if (!hasPaneSplit()) showSecondaryPane();
		outputLeaf = leafIdForRole(bprCompilerOutput);
		if (outputLeaf < 0) outputLeaf = splitLeafNode(0, bsoHorizontal, bprCompilerOutput);
	}
	if (outputLeaf < 0) return false;

	int problemsLeaf = leafIdForRole(bprProblems);
	if (problemsLeaf < 0) problemsLeaf = splitLeafNode(outputLeaf, bsoVertical, bprProblems);
	if (previousActiveLeaf >= 0 && nodeIndexForLeaf(previousActiveLeaf) >= 0) setActivePane(previousActiveLeaf);
	outputWindow = buildOutputPane();
	problemsWindow = problemsPane();
	return outputWindow != nullptr && problemsWindow != nullptr;
}

bool MRBentoBox::ensureMacroDebuggerPanes(MREditWindow *&outputWindow, MREditWindow *&variablesWindow, MREditWindow *&watchesWindow) {
	int outputLeaf = leafIdForRole(bprDebuggerOutput);

	if (bentoMode == bbmDocumentViewports) {
		bentoMode = bbmToolWorkspace;
		for (BentoLeaf &leaf : leaves) {
			leaf.spec = paneSpecForRole(leaf.role);
			leaf.title = titleForPaneRole(leaf.role);
			if (leaf.pane != nullptr) leaf.pane->setPaneSpec(leaf.spec, getEditor());
		}
	}
	if (outputLeaf < 0) outputLeaf = splitLeafNode(0, bsoHorizontal, bprDebuggerOutput);
	if (outputLeaf < 0) return false;

	int variablesLeaf = leafIdForRole(bprVariables);
	if (variablesLeaf < 0) variablesLeaf = splitLeafNode(outputLeaf, bsoVertical, bprVariables);
	if (variablesLeaf < 0) return false;

	int watchesLeaf = leafIdForRole(bprWatches);
	if (watchesLeaf < 0) watchesLeaf = splitLeafNode(variablesLeaf, bsoHorizontal, bprWatches);
	if (watchesLeaf < 0) return false;

	secondaryPaneVisible = firstToolLeafId() >= 0;
	setActivePane(outputLeaf);
	layoutSplitPanes();
	outputWindow = debuggerOutputPane();
	variablesWindow = variablesPane();
	watchesWindow = watchesPane();
	if (!macroDebuggerVariables.empty()) refreshMacroDebuggerVariables(macroDebuggerVariables);
	return outputWindow != nullptr && variablesWindow != nullptr && watchesWindow != nullptr;
}

void MRBentoBox::activatePrimaryPane() noexcept {
	setActivePane(0);
}

void MRBentoBox::activateSecondaryPane() noexcept {
	if (firstToolLeafId() < 0) showSecondaryPane();
	int toolLeaf = firstToolLeafId();
	if (toolLeaf >= 0) setActivePane(toolLeaf);
}

void MRBentoBox::toggleActivePane() noexcept {
	if (leaves.empty()) return;
	int currentIndex = -1;
	for (std::size_t i = 0; i < leaves.size(); ++i)
		if (leaves[i].visible && leaves[i].id == activeLeafId) currentIndex = static_cast<int>(i);
	for (std::size_t step = 1; step <= leaves.size(); ++step) {
		const std::size_t next = static_cast<std::size_t>((std::max(0, currentIndex) + static_cast<int>(step)) % static_cast<int>(leaves.size()));
		if (leaves[next].visible) {
			setActivePane(leaves[next].id);
			return;
		}
	}
}
bool MRBentoBox::placePaneRole(MRBentoPaneRole role, MRBentoPanePlacement placement) {
	return placePaneRoleInContext(role, placement, activeLeafId);
}

TColorAttr MRBentoBox::mapColor(uchar index) {
	if (bentoMode == bbmFileCompare && index == 1) {
		unsigned char value = 0;

		if (configuredColorSlotOverride(kMrPaletteFileCompareBentoBorder, value)) return static_cast<TColorAttr>(value);
	}
	if (bentoMode == bbmFileCompare && index == 13) {
		unsigned char value = 0;

		if (configuredColorSlotOverride(kMrPaletteFileCompareFocusedPaneBorder, value)) return static_cast<TColorAttr>(value);
	}
	if (sourceScrollBarPaletteActive && (index == 4 || index == 5)) {
		if (bentoMode == bbmFileCompare) {
			MRFileEditor *sourceEditor = getEditor();

			if (sourceEditor != nullptr) return sourceEditor->editorTextFillColor();
		}
		return MREditWindow::mapColor(activeLeafId == 0 ? 13 : 1);
	}
	return MREditWindow::mapColor(index);
}

bool MRBentoBox::allowsDocumentViewportSplit() const noexcept {
	return bentoMode == bbmDocumentViewports;
}

MREditWindow *MRBentoBox::editorCommandTarget() noexcept {
	MRPaneEditWindow *pane = paneWindowForLeaf(activeLeafId);

	return pane != nullptr ? static_cast<MREditWindow *>(pane) : static_cast<MREditWindow *>(this);
}

const MREditWindow *MRBentoBox::editorCommandTarget() const noexcept {
	const MRPaneEditWindow *pane = paneWindowForLeaf(activeLeafId);

	return pane != nullptr ? static_cast<const MREditWindow *>(pane) : static_cast<const MREditWindow *>(this);
}

MRPaneEditWindow *MRBentoBox::createPaneWindow(const TRect &bounds, const char *title, int paneNumber, const MRBentoPaneSpec &) {
	return new MRPaneEditWindow(bounds, title, paneNumber);
}

bool MRBentoBox::primaryPaneUsesDedicatedWindow() const noexcept {
	return false;
}

bool MRBentoBox::acceptsPaneRole(MRBentoPaneRole role) const noexcept {
	return role >= bprSource && role <= bprDiffCompare;
}

const char *MRBentoBox::titleForPaneRole(MRBentoPaneRole role) const noexcept {
	return mr::bento::paneRoleTitle(role);
}

void MRBentoBox::activePaneRoleChanged(MRBentoPaneRole) noexcept {
}

bool MRBentoBox::paneCloseActionEnabled() const noexcept {
	return true;
}

bool MRBentoBox::paneMaximizeActionEnabled() const noexcept {
	return true;
}

bool MRBentoBox::activatePaneWindow(MREditWindow *pane) noexcept {
	if (pane == nullptr) return false;
	if (pane == this) {
		setActivePane(0);
		return mrActivateEditWindow(this);
	}
	for (const BentoLeaf &leaf : leaves) {
		if (!leaf.visible || leaf.pane != pane) continue;
		setActivePane(leaf.id);
		return mrActivateEditWindow(this);
	}
	return false;
}

bool MRBentoBox::showsFrameGrowHandle() const noexcept {
	return false;
}

bool MRBentoBox::placePaneRoleInContext(MRBentoPaneRole role, MRBentoPanePlacement placement, int targetLeafId) {
	MRPaneEditWindow *targetPane = paneWindowForLeaf(targetLeafId);
	const MRBentoPaneSpec spec = paneSpecForRole(role);
	const MRBentoPaneRole previousRole = roleForLeaf(targetLeafId);

	if (bentoMode == bbmFileCompare && !mr::bento::paneRoleIsDiff(role)) return false;
	if (targetLeafId == 0 && placement == bppReplace && bentoMode != bbmFileCompare) return false;
	switch (placement) {
		case bppReplace:
			if (targetLeafId != 0 && targetPane == nullptr) return false;
			if (targetPane != nullptr) targetPane->setPaneSpec(spec, getEditor());
			for (BentoLeaf &leaf : leaves) {
				if (leaf.id == targetLeafId) {
					leaf.role = role;
					leaf.spec = spec;
					leaf.title = titleForPaneRole(role);
				}
			}
			if (bentoMode == bbmFileCompare && mr::bento::paneRoleIsDiff(role)) {
				refreshFileComparePanes();
			} else if (!mr::bento::paneRoleIsOutline(role)) {
				static_cast<void>(targetPane->replaceTextBuffer("", mr::bento::paneRoleTitle(role)));
				targetPane->setReadOnly(spec.readOnly);
				targetPane->setFileChanged(false);
			}
			if (previousRole != role && mr::bento::paneRoleIsOutline(previousRole)) {
				bool previousRoleStillVisible = false;
				for (const BentoLeaf &leaf : leaves)
					if (leaf.visible && leaf.id != targetLeafId && leaf.role == previousRole) previousRoleStillVisible = true;
				if (!previousRoleStillVisible) {
					if (previousRole == bprStructure) {
						structureOutlineStatus.clear();
						structureOutlineEntries.clear();
						structureOutlineState = MRBentoOutlinePaneState();
					} else if (previousRole == bprFunctions) {
						functionsOutlineStatus.clear();
						functionsOutlineEntries.clear();
						functionsOutlineState = MRBentoOutlinePaneState();
					}
				}
			}
			setActivePane(targetLeafId);
			layoutSplitPanes();
			if (mr::bento::paneRoleIsOutline(role)) refreshOutlinePanes(true);
			if (bentoMode == bbmFileCompare) refreshFileComparePanes();
			mrMarkWorkspaceAutosaveDirty("bento pane role", this);
			return true;
		case bppSplitRight:
			if (!mr::bento::paneRoleIsOutline(role)) {
				if (bentoMode == bbmFileCompare) {
					const bool ok = splitLeafNode(targetLeafId, bsoVertical, spec) >= 0;
					if (ok) refreshFileComparePanes();
					if (ok) mrMarkWorkspaceAutosaveDirty("bento pane split", this);
					return ok;
				}
				const bool ok = splitLeafNode(targetLeafId, bsoVertical, spec) >= 0;
				if (ok) mrMarkWorkspaceAutosaveDirty("bento pane split", this);
				return ok;
			}
			if (splitLeafNode(targetLeafId, bsoVertical, spec) < 0) return false;
			refreshOutlinePanes(true);
			mrMarkWorkspaceAutosaveDirty("bento pane split", this);
			return true;
		case bppSplitDown:
			if (!mr::bento::paneRoleIsOutline(role)) {
				if (bentoMode == bbmFileCompare) {
					const bool ok = splitLeafNode(targetLeafId, bsoHorizontal, spec) >= 0;
					if (ok) refreshFileComparePanes();
					if (ok) mrMarkWorkspaceAutosaveDirty("bento pane split", this);
					return ok;
				}
				const bool ok = splitLeafNode(targetLeafId, bsoHorizontal, spec) >= 0;
				if (ok) mrMarkWorkspaceAutosaveDirty("bento pane split", this);
				return ok;
			}
			if (splitLeafNode(targetLeafId, bsoHorizontal, spec) < 0) return false;
			refreshOutlinePanes(true);
			mrMarkWorkspaceAutosaveDirty("bento pane split", this);
			return true;
		default:
			return false;
	}
}

MRBentoWorkspaceSnapshot MRBentoBox::workspaceSnapshot() const {
	MRBentoWorkspaceSnapshot snapshot;

	snapshot.mode = bentoMode;
	snapshot.rootNode = rootNode;
	snapshot.activeLeafId = activeLeafId;
	snapshot.maximizedLeafId = maximizedLeafId;
	for (const BentoLayoutNode &node : layoutTree) {
		MRBentoWorkspaceNode workspaceNode;
		workspaceNode.kind = static_cast<int>(node.kind);
		workspaceNode.orientation = static_cast<int>(node.orientation);
		workspaceNode.dividerPosition = node.dividerPosition;
		workspaceNode.firstChild = node.firstChild;
		workspaceNode.secondChild = node.secondChild;
		workspaceNode.leafId = node.leafId;
		snapshot.nodes.push_back(workspaceNode);
	}
	for (const BentoLeaf &leaf : leaves) {
		MRBentoWorkspaceLeaf workspaceLeaf;
		workspaceLeaf.id = leaf.id;
		workspaceLeaf.role = leaf.role;
		workspaceLeaf.visible = leaf.visible;
		snapshot.leaves.push_back(workspaceLeaf);
	}
	return snapshot;
}

bool MRBentoBox::restoreWorkspaceSnapshot(const MRBentoWorkspaceSnapshot &snapshot) {
	bool hasSourceLeaf = false;
	int nextId = 0;

	if (!bentoWorkspaceModeIsValid(static_cast<int>(snapshot.mode))) return false;
	if (snapshot.nodes.empty() || snapshot.leaves.empty()) return false;
	if (snapshot.rootNode < 0 || snapshot.rootNode >= static_cast<int>(snapshot.nodes.size())) return false;

	for (const MRBentoWorkspaceLeaf &leaf : snapshot.leaves) {
		if (leaf.id < 0 || !bentoWorkspaceRoleIsValid(static_cast<int>(leaf.role)) || !acceptsPaneRole(leaf.role)) return false;
		if (leaf.id == 0) hasSourceLeaf = true;
		for (const MRBentoWorkspaceLeaf &other : snapshot.leaves)
			if (&leaf != &other && leaf.id == other.id) return false;
		if (snapshot.mode == bbmFileCompare) {
			if (!mr::bento::paneRoleIsDiff(leaf.role)) return false;
		} else if (mr::bento::paneRoleIsDiff(leaf.role))
			return false;
		nextId = std::max(nextId, leaf.id + 1);
	}
	if (!hasSourceLeaf) return false;

	for (std::size_t i = 0; i < snapshot.nodes.size(); ++i) {
		const MRBentoWorkspaceNode &node = snapshot.nodes[i];
		if (node.kind != blnPane && node.kind != blnSplit) return false;
		if (node.orientation != bsoHorizontal && node.orientation != bsoVertical) return false;
		if (node.kind == blnPane) {
			bool leafFound = false;
			for (const MRBentoWorkspaceLeaf &leaf : snapshot.leaves)
				if (leaf.id == node.leafId) leafFound = true;
			if (!leafFound) return false;
		} else {
			if (node.firstChild < 0 || node.firstChild >= static_cast<int>(snapshot.nodes.size())) return false;
			if (node.secondChild < 0 || node.secondChild >= static_cast<int>(snapshot.nodes.size())) return false;
			if (node.firstChild == static_cast<int>(i) || node.secondChild == static_cast<int>(i)) return false;
		}
	}
	{
		std::vector<bool> visited(snapshot.nodes.size(), false);
		std::vector<int> stack;
		stack.push_back(snapshot.rootNode);
		while (!stack.empty()) {
			const int nodeIndex = stack.back();
			stack.pop_back();
			if (nodeIndex < 0 || nodeIndex >= static_cast<int>(snapshot.nodes.size())) return false;
			if (visited[static_cast<std::size_t>(nodeIndex)]) return false;
			visited[static_cast<std::size_t>(nodeIndex)] = true;
			const MRBentoWorkspaceNode &node = snapshot.nodes[static_cast<std::size_t>(nodeIndex)];
			if (node.kind == blnSplit) {
				stack.push_back(node.secondChild);
				stack.push_back(node.firstChild);
			}
		}
	}

	cancelMacroDebuggerValueInput();
	for (BentoLeaf &leaf : leaves) {
		if (leaf.pane != nullptr) {
			remove(leaf.pane);
			TObject::destroy(leaf.pane);
			leaf.pane = nullptr;
		}
	}
	layoutTree.clear();
	leaves.clear();
	secondaryPane = nullptr;
	bentoMode = snapshot.mode;

	for (const MRBentoWorkspaceNode &workspaceNode : snapshot.nodes) {
		BentoLayoutNode node;
		node.kind = static_cast<BentoLayoutNodeKind>(workspaceNode.kind);
		node.orientation = static_cast<BentoSplitOrientation>(workspaceNode.orientation);
		node.dividerPosition = workspaceNode.dividerPosition;
		node.firstChild = workspaceNode.firstChild;
		node.secondChild = workspaceNode.secondChild;
		node.leafId = workspaceNode.leafId;
		layoutTree.push_back(node);
	}
	for (const MRBentoWorkspaceLeaf &workspaceLeaf : snapshot.leaves) {
		BentoLeaf leaf;
		leaf.id = workspaceLeaf.id;
		leaf.role = workspaceLeaf.role;
		leaf.spec = paneSpecForRole(workspaceLeaf.role);
		leaf.title = bentoMode == bbmDocumentViewports ? "" : titleForPaneRole(workspaceLeaf.role);
		leaf.visible = workspaceLeaf.visible;
		if (leaf.id != 0 || primaryPaneUsesDedicatedWindow()) {
			const std::string initialTitle = bentoMode == bbmDocumentViewports ? "Viewport" : leaf.title;
			leaf.pane = createPaneWindow(TRect(0, 0, 1, 1), initialTitle.c_str(), number, leaf.spec);
			if (leaf.pane == nullptr) return false;
			leaf.pane->setPaneSpec(leaf.spec, getEditor());
			insert(leaf.pane);
			if (leaf.id != 0 && secondaryPane == nullptr && leaf.visible) secondaryPane = leaf.pane;
		}
		leaves.push_back(leaf);
	}

	rootNode = snapshot.rootNode;
	nextLeafId = nextId;
	activeLeafId = nodeIndexForLeaf(snapshot.activeLeafId) >= 0 ? snapshot.activeLeafId : 0;
	maximizedLeafId = nodeIndexForLeaf(snapshot.maximizedLeafId) >= 0 ? snapshot.maximizedLeafId : -1;
	sourceScrollBarPaletteActive = false;
	secondaryPaneVisible = firstToolLeafId() >= 0;
	paneRoleDropList.hide();
	paneActionDropList.hide();
	layoutSplitPanes();
	refreshOutlinePanes(true);
	activePaneRoleChanged(roleForLeaf(activeLeafId));
	return true;
}

bool MRBentoBox::splitActiveEditorPane(MRBentoPanePlacement placement) {
	MRBentoPaneSpec spec = paneSpecForRole(bprSplitEditor);

	switch (placement) {
		case bppSplitRight: {
			const bool ok = splitLeafNode(activeLeafId, bsoVertical, spec) >= 0;
			if (ok) mrMarkWorkspaceAutosaveDirty("bento pane split", this);
			return ok;
		}
		case bppSplitDown: {
			const bool ok = splitLeafNode(activeLeafId, bsoHorizontal, spec) >= 0;
			if (ok) mrMarkWorkspaceAutosaveDirty("bento pane split", this);
			return ok;
		}
		default:
			return false;
	}
}

void MRBentoBox::draw() {
	refreshEditorTaskMarkers();
	MREditWindow::draw();
	if (hasPaneSplit()) {
		if (paneWindowForLeaf(0) == nullptr) drawSourcePaneScrollBars();
		drawPaneFrames();
	}
}

void MRBentoBox::changeBounds(const TRect &bounds) {
	paneRoleDropList.hide();
	paneActionDropList.hide();
	fileCompareActionDropList.hide();
	updatePaneRoleListChrome();
	MREditWindow::changeBounds(bounds);
	if (hasPaneSplit()) layoutSplitPanes();
}

void MRBentoBox::close() {
	cancelMacroDebuggerValueInput();
	restoreFileCompareSources();
	windowCloseInProgress = true;
	MREditWindow::close();
}

void MRBentoBox::shutDown() {
	cancelMacroDebuggerValueInput();
	restoreFileCompareSources();
	windowCloseInProgress = true;
	MREditWindow::shutDown();
}
