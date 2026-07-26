#include "MRBentoBox.hpp"

#include "../../mrmac/MRVM.hpp"
#include "../../mrmac/vm/MRVMRuntimeDebugger.hpp"

MRBentoOutlineEntry::MRBentoOutlineEntry() noexcept : paneOffset(0), sourceOffset(0), sourceSelectionEnd(0) {
}

MRBentoOutlinePaneState::MRBentoOutlinePaneState() noexcept
	: documentId(0), version(0), targetDocumentId(0), targetVersion(0), textLength(0), targetBufferId(0), textHash(0),
	  language(MRSyntaxLanguage::PlainText), complete(false), lastRefresh(std::chrono::steady_clock::time_point()) {
}

MRBentoCompareSource::MRBentoCompareSource() noexcept : window(nullptr), bufferId(0), documentId(0), version(0), wasVisible(false), wasManuallyHidden(false), title(), snapshot() {
}

MRBentoCompareSetup::MRBentoCompareSetup() noexcept : original(), compare() {
}

MRBentoWorkspaceNode::MRBentoWorkspaceNode() noexcept : kind(0), orientation(0), dividerPosition(0), firstChild(-1), secondChild(-1), leafId(-1) {
}

MRBentoWorkspaceLeaf::MRBentoWorkspaceLeaf() noexcept : id(-1), role(bprSource), visible(false), widgetMask(bpwNone) {
}

MRBentoWorkspaceSnapshot::MRBentoWorkspaceSnapshot() noexcept : mode(bbmToolWorkspace), rootNode(-1), activeLeafId(0), maximizedLeafId(-1), nodes(), leaves() {
}

MRMacroDebuggerWorkspaceBreakpoint::MRMacroDebuggerWorkspaceBreakpoint() noexcept : macroKey(), line(0), enabled(false) {
}

MRMacroDebuggerWorkspaceWatch::MRMacroDebuggerWorkspaceWatch() noexcept : expression(), enabled(false) {
}

MRMacroDebuggerWorkspaceConfiguration::MRMacroDebuggerWorkspaceConfiguration() noexcept : macroKey(), macroName(), breakpoints(), watches() {
}

MRBentoBox::BentoProjectionTaskState::BentoProjectionTaskState() noexcept
	: taskId(0), generationCounter(1), activeGeneration(0), sourceDocumentId(0), sourceVersion(0), inputDocumentId(0), inputVersion(0),
	  inputBufferId(0), inputRevision(0), inputLanguage(MRSyntaxLanguage::PlainText), inputComplete(false), targetDocumentId(0), targetVersion(0), targetBufferId(0), sourcePath(), trackWarnings(false), trackNotes(false),
	  diagnosticsRequest(bdprNone), pendingDiagnosticsRequest(bdprNone), diagnosticSourceChanges(), diagnosticBaseDocumentId(0), diagnosticBaseVersion(0), pending(false),
	  pendingForce(false), completeCoverageRequested(false), projectionCurrent(false), retryBlocked(false), requestedAt(std::chrono::steady_clock::time_point()) {
}

MRBentoBox::MRBentoBox(const TRect &bounds, const char *title, int number, MRBentoBoxMode mode)
	: TWindowInit(&MRBentoBox::initFrame), MREditWindow(bounds, title, number, mr::coprocessor::ExecutionOwnerKind::BentoPane), secondaryPane(nullptr), layoutTree(), leaves(), paneFrameViews(), rootNode(-1), activeLeafId(0), nextLeafId(0), maximizedLeafId(-1), bentoMode(mode), sourceScrollBarPaletteActive(false), secondaryPaneVisible(false), windowCloseInProgress(false), bentoProjectionAdoptionActive(false), bentoSourceMutationTrackingActive(false), bentoProjectionDirty(bpdNone), paneRoleDropList(), paneActionDropList(), fileCompareActionDropList(), paneRoleListAnchor(), pendingPaneRole(bprCompilerOutput), pendingPaneRoleTargetLeafId(0), pendingFileCompareActionLeafId(0), pendingFileCompareActionGroupIndex(-1), compilerOutputStatus(), compilerProblemsStatus(), structureOutlineStatus(), functionsOutlineStatus(), macroDebuggerStatus(), macroDebuggerMacroKey(), macroDebuggerMacroName(), macroDebuggerSourcePath(), macroDebuggerProjectedMacroKey(), macroDebuggerSessionId(0), macroDebuggerExecutionRunning(false), macroDebuggerActive(false), macroDebuggerWorkspacePending(), macroDebuggerValueInput(nullptr), macroDebuggerValueInputPane(nullptr), macroDebuggerVariables(), macroDebuggerVariableRows(), compilerDiagnostics(std::make_shared<const std::vector<MRCompilerDiagnostic>>()), compilerDiagnosticSourceChanges(), compilerDiagnosticsParseSourceSnapshot(), compilerDiagnosticsDocumentId(0), compilerDiagnosticsVersion(0), compilerDiagnosticsOutputDocumentId(0), compilerDiagnosticsOutputVersion(0), compilerDiagnosticsOutputBufferId(0), compilerProblemsTargetDocumentId(0), compilerProblemsTargetVersion(0), compilerProblemsTargetBufferId(0), compilerProblemsTextLength(0), compilerProblemsTextHash(0), compilerDiagnosticsParseRequired(true), compilerDiagnosticsSourceInvalidated(false), pendingCompilerProblemNavigation(0), fileCompareSetup(), fileComparePipeline(), fileCompareSourcesRestored(false), fileCompareDiffReady(false), fileCompareStale(false), fileCompareLinkedPaneSyncActive(false), structureOutlineState(), functionsOutlineState(), diagnosticsProjectionTask(), structureProjectionTask(), functionsProjectionTask(), structureOutlineEntries(std::make_shared<const std::vector<MRBentoOutlineEntry>>()), functionsOutlineEntries(std::make_shared<const std::vector<MRBentoOutlineEntry>>()), compilerSidekickTracked(false), compilerSidekickUpdating(false), compilerSidekickDiagnosticIndex(0) {
	initializeLayoutTree();
	layoutSplitPanes();
}

MRBentoBox::~MRBentoBox() {
	cancelFileComparePipeline();
	cancelAllBentoProjectionTasks();
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
		if (leaf.pane != nullptr && leaf.pane->bufferId() == bufferId) return leaf.pane;
	return nullptr;
}

void MRBentoBox::collectVisiblePaneWindows(std::vector<MREditWindow *> &windows) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.pane != nullptr) windows.push_back(leaf.pane);
}
