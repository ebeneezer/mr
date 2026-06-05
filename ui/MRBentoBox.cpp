#include "MRBentoBox.hpp"

MRBentoOutlineEntry::MRBentoOutlineEntry() noexcept : paneOffset(0), sourceOffset(0), sourceSelectionEnd(0) {
}

MRBentoOutlinePaneState::MRBentoOutlinePaneState() noexcept : documentId(0), version(0), textHash(0), complete(false), lastRefresh(std::chrono::steady_clock::time_point()) {
}

MRBentoBox::MRBentoBox(const TRect &bounds, const char *title, int number, MRBentoBoxMode mode)
    : TWindowInit(&MRBentoBox::initFrame), MREditWindow(bounds, title, number), secondaryPane(nullptr), layoutTree(), leaves(), paneFrameViews(), rootNode(-1), activeLeafId(0), nextLeafId(0), maximizedLeafId(-1), bentoMode(mode), sourceScrollBarPaletteActive(false), secondaryPaneVisible(false), windowCloseInProgress(false), bentoProjectionDirty(bpdNone), paneRoleDropList(), paneActionDropList(), paneRoleListAnchor(), pendingPaneRole(bprCompilerOutput), pendingPaneRoleTargetLeafId(0), compilerOutputStatus(), compilerProblemsStatus(), structureOutlineStatus(), functionsOutlineStatus(), compilerDiagnostics(), structureOutlineState(), functionsOutlineState(), structureOutlineEntries(), functionsOutlineEntries(), compilerSidekickTracked(false), compilerSidekickUpdating(false), compilerSidekickDiagnosticIndex(0) {
	initializeLayoutTree();
	layoutSplitPanes();
}

MRBentoBox::~MRBentoBox() {
}

MREditWindow *MRBentoBox::secondaryEditWindow() const noexcept {
	return paneWindowForLeaf(firstToolLeafId());
}

MREditWindow *MRBentoBox::buildOutputPane() const noexcept {
	return paneWindowForLeaf(leafIdForRole(bprCompilerOutput));
}

MREditWindow *MRBentoBox::problemsPane() const noexcept {
	return paneWindowForLeaf(leafIdForRole(bprProblems));
}

MREditWindow *MRBentoBox::structurePane() const noexcept {
	return paneWindowForLeaf(leafIdForRole(bprStructure));
}

MREditWindow *MRBentoBox::functionsPane() const noexcept {
	return paneWindowForLeaf(leafIdForRole(bprFunctions));
}

MREditWindow *MRBentoBox::paneForBufferId(int bufferId) const noexcept {
	if (bufferId == this->bufferId()) return const_cast<MRBentoBox *>(this);
	for (const BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.pane != nullptr && leaf.pane->bufferId() == bufferId) return leaf.pane;
	return nullptr;
}

void MRBentoBox::collectVisiblePaneWindows(std::vector<MREditWindow *> &windows) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.pane != nullptr) windows.push_back(leaf.pane);
}
