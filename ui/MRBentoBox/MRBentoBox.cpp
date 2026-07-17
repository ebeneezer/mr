#include "MRBentoBox.hpp"

#include "../../mrmac/MRVM.hpp"
#include "../../mrmac/vm/MRVMRuntimeDebugger.hpp"

MRBentoOutlineEntry::MRBentoOutlineEntry() noexcept : paneOffset(0), sourceOffset(0), sourceSelectionEnd(0) {
}

MRBentoOutlinePaneState::MRBentoOutlinePaneState() noexcept : documentId(0), version(0), textHash(0), complete(false), lastRefresh(std::chrono::steady_clock::time_point()) {
}

MRBentoCompareSource::MRBentoCompareSource() noexcept : window(nullptr), bufferId(0), documentId(0), version(0), wasVisible(false), wasManuallyHidden(false), title(), text() {
}

MRBentoCompareSetup::MRBentoCompareSetup() noexcept : original(), compare() {
}

MRBentoWorkspaceNode::MRBentoWorkspaceNode() noexcept : kind(0), orientation(0), dividerPosition(0), firstChild(-1), secondChild(-1), leafId(-1) {
}

MRBentoWorkspaceLeaf::MRBentoWorkspaceLeaf() noexcept : id(-1), role(bprSource), visible(false) {
}

MRBentoWorkspaceSnapshot::MRBentoWorkspaceSnapshot() noexcept : mode(bbmToolWorkspace), rootNode(-1), activeLeafId(0), maximizedLeafId(-1), nodes(), leaves() {
}

MRMacroDebuggerWorkspaceBreakpoint::MRMacroDebuggerWorkspaceBreakpoint() noexcept : macroKey(), line(0), enabled(false) {
}

MRMacroDebuggerWorkspaceWatch::MRMacroDebuggerWorkspaceWatch() noexcept : expression(), enabled(false) {
}

MRMacroDebuggerWorkspaceConfiguration::MRMacroDebuggerWorkspaceConfiguration() noexcept : macroKey(), macroName(), breakpoints(), watches() {
}

MRBentoBox::FileCompareChangeGroup::FileCompareChangeGroup() noexcept : displayStartLine(0), originalStartLine(0), compareStartLine(0), displayLineCount(0), deletedLineCount(0), insertedLineCount(0) {
}

MRBentoBox::MRBentoBox(const TRect &bounds, const char *title, int number, MRBentoBoxMode mode)
	: TWindowInit(&MRBentoBox::initFrame), MREditWindow(bounds, title, number), secondaryPane(nullptr), layoutTree(), leaves(), paneFrameViews(), rootNode(-1), activeLeafId(0), nextLeafId(0), maximizedLeafId(-1), bentoMode(mode), sourceScrollBarPaletteActive(false), secondaryPaneVisible(false), windowCloseInProgress(false), bentoProjectionDirty(bpdNone), paneRoleDropList(), paneActionDropList(), fileCompareActionDropList(), paneRoleListAnchor(), pendingPaneRole(bprCompilerOutput), pendingPaneRoleTargetLeafId(0), pendingFileCompareActionLeafId(0), pendingFileCompareActionGroupIndex(-1), compilerOutputStatus(), compilerProblemsStatus(), structureOutlineStatus(), functionsOutlineStatus(), macroDebuggerStatus(), macroDebuggerMacroKey(), macroDebuggerMacroName(), macroDebuggerSourcePath(), macroDebuggerProjectedMacroKey(), macroDebuggerSessionId(0), macroDebuggerExecutionRunning(false), macroDebuggerActive(false), macroDebuggerWorkspacePending(), macroDebuggerValueInput(nullptr), macroDebuggerValueInputPane(nullptr), macroDebuggerVariables(), macroDebuggerVariableRows(), compilerDiagnostics(), fileCompareSetup(), fileCompareHunks(), fileCompareChangeGroups(), fileCompareOriginalLines(), fileCompareCompareLines(), fileCompareOriginalLineKinds(), fileCompareCompareLineKinds(), fileCompareOriginalMiniMapSlices(), fileCompareCompareMiniMapSlices(), fileCompareTaskId(0), fileCompareSourcesRestored(false), fileCompareDiffReady(false), fileCompareStale(false), structureOutlineState(), functionsOutlineState(), structureOutlineEntries(), functionsOutlineEntries(), compilerSidekickTracked(false), compilerSidekickUpdating(false), compilerSidekickDiagnosticIndex(0) {
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

MREditWindow *MRBentoBox::debuggerOutputPane() const noexcept {
	return paneWindowForLeaf(leafIdForRole(bprDebuggerOutput));
}

MREditWindow *MRBentoBox::variablesPane() const noexcept {
	return paneWindowForLeaf(leafIdForRole(bprVariables));
}

MREditWindow *MRBentoBox::watchesPane() const noexcept {
	return paneWindowForLeaf(leafIdForRole(bprWatches));
}

void MRBentoBox::setMacroDebuggerTarget(const std::string &macroKey, const std::string &macroName) {
	macroDebuggerMacroKey = macroKey;
	macroDebuggerMacroName = macroName;
	if (getEditor() != nullptr && getEditor()->hasPersistentFileName()) macroDebuggerSourcePath = getEditor()->persistentFileName();
	macroDebuggerProjectedMacroKey = macroKey;
	macroDebuggerActive = !macroDebuggerMacroKey.empty();
	if (!macroDebuggerActive) macroDebuggerStatus.clear();
	refreshMacroDebuggerBreakpointRanges();
}

bool MRBentoBox::macroDebuggerWorkspaceConfiguration(MRMacroDebuggerWorkspaceConfiguration &configuration) const {
	std::vector<MRMacroDebuggerBreakpoint> breakpoints;
	std::vector<MRMacroDebugWatchSnapshot> watches;

	configuration = MRMacroDebuggerWorkspaceConfiguration();
	if (!macroDebuggerActive || macroDebuggerMacroKey.empty()) return false;
	configuration.macroKey = macroDebuggerMacroKey;
	configuration.macroName = macroDebuggerMacroName;
	configuration.breakpoints = macroDebuggerWorkspacePending.breakpoints;
	configuration.watches = macroDebuggerWorkspacePending.watches;
	if (mrvmDebugLineBreakpointsForMacro(macroDebuggerMacroKey, breakpoints))
		for (const MRMacroDebuggerBreakpoint &breakpoint : breakpoints) {
			bool known = false;

			for (const MRMacroDebuggerWorkspaceBreakpoint &stored : configuration.breakpoints)
				if (stored.macroKey == breakpoint.macroKey && stored.line == breakpoint.line) known = true;
			if (!known) {
				MRMacroDebuggerWorkspaceBreakpoint stored;

				stored.macroKey = breakpoint.macroKey;
				stored.line = breakpoint.line;
				stored.enabled = breakpoint.enabled;
				configuration.breakpoints.push_back(stored);
			}
		}
	if (mrvmDebugWatchSnapshots(0, macroDebuggerMacroKey, watches))
		for (const MRMacroDebugWatchSnapshot &watch : watches) {
			bool known = false;

			for (const MRMacroDebuggerWorkspaceWatch &stored : configuration.watches)
				if (stored.expression == watch.expression) known = true;
			if (!known) {
				MRMacroDebuggerWorkspaceWatch stored;

				stored.expression = watch.expression;
				stored.enabled = watch.enabled;
				configuration.watches.push_back(stored);
			}
		}
	return true;
}

void MRBentoBox::setMacroDebuggerSession(MRMacroExecutionSessionId sessionId) noexcept {
	macroDebuggerSessionId = sessionId;
}

void MRBentoBox::setMacroDebuggerSession(MRMacroExecutionSessionId sessionId, const std::vector<MRMacroDebugVariableSnapshot> &variables) {
	macroDebuggerSessionId = sessionId;
	refreshMacroDebuggerVariables(variables);
}

bool MRBentoBox::macroDebuggerFunctionKeysActive() const noexcept {
	return macroDebuggerActive;
}

bool MRBentoBox::macroDebuggerHasLiveSession() const noexcept {
	return macroDebuggerSessionId != 0;
}

bool MRBentoBox::macroDebuggerSessionRunning() const noexcept {
	return macroDebuggerExecutionRunning;
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
