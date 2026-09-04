#ifndef MRBENTOBOX_HPP
#define MRBENTOBOX_HPP

#include "../MREditWindow.hpp"
#include "../widgets/MRDropList.hpp"
#include "MRBentoBoxDerivedProjection.hpp"
#include "MRBentoBoxFileCompareProjection.hpp"
#include "MRDiff.hpp"
#include "../../mrmac/MRMacroExecutionSession.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class MRBentoPaneFrameView;
class MRGdbSession;
class MRGdbTerminalPane;
enum class MRGdbCommandKind : unsigned char;
class MRDebuggerValueInput;
struct MRMacroDebugRunResult;
enum MRMacroDebugStepMode : int;
enum MRMacroDebugWorkerAction : int;
namespace mr {
namespace coprocessor {
struct Result;
struct GdbEventPayload;
}
}

enum MRBentoPaneRole {
	bprSource = 0,
	bprCompilerOutput,
	bprAppOutput,
	bprProblems,
	bprDebuggerOutput,
	bprWatches,
	bprVariables,
	bprStructure,
	bprFunctions,
	bprSplitEditor,
	bprDiffOriginal,
	bprDiffCompare,
	bprExtensionFirst = 12,
	bprExtensionLast = bprExtensionFirst + 15,
	bprProgramTerminal = 28
};

enum MRBentoPanePlacement {
	bppReplace = 0,
	bppSplitRight,
	bppSplitDown
};

enum MRBentoPaneBufferPolicy {
	bpbOwnBuffer = 0,
	bpbSharedSourceBuffer
};

enum MRBentoPaneWidget : std::uint32_t {
	bpwNone = 0,
	bpwFoldGutter = 1U << 0,
	bpwMiniMap = 1U << 1
};

struct MRBentoPaneTitleMenuSpec {
	const char *name;
};

enum MRBentoBoxMode {
	bbmToolWorkspace = 0,
	bbmDocumentViewports,
	bbmFileCompare
};

struct MRBentoPaneSpec {
	MRBentoPaneSpec() noexcept;
	MRBentoPaneSpec(MRBentoPaneRole aRole, MRBentoPaneBufferPolicy aBufferPolicy, bool aReadOnly, bool aSuppressMiniMap, bool aSuppressWordWrap, bool aScrollBarsAlwaysVisible, const MRBentoPaneTitleMenuSpec *aTitleMenu) noexcept;
	[[nodiscard]] static std::uint32_t defaultWidgetMask(MRBentoPaneRole role) noexcept;
	[[nodiscard]] static bool widgetMaskIsValid(std::uint32_t mask) noexcept;

	MRBentoPaneRole role;
	MRBentoPaneBufferPolicy bufferPolicy;
	bool readOnly;
	std::uint32_t widgetMask;
	bool suppressMiniMap;
	bool suppressWordWrap;
	bool scrollBarsAlwaysVisible;
	const MRBentoPaneTitleMenuSpec *titleMenu;
};

struct MRBentoOutlinePaneState {
	MRBentoOutlinePaneState() noexcept;

	std::size_t documentId;
	std::size_t version;
	std::size_t targetDocumentId;
	std::size_t targetVersion;
	std::size_t textLength;
	int targetBufferId;
	std::uint64_t textHash;
	MRSyntaxLanguage language;
	bool complete;
	std::chrono::steady_clock::time_point lastRefresh;
};

struct MRBentoCompareSource {
	MRBentoCompareSource() noexcept;

	MREditWindow *window;
	int bufferId;
	std::size_t documentId;
	std::size_t version;
	bool wasVisible;
	bool wasManuallyHidden;
	std::string title;
	MRTextBufferModel::ReadSnapshot snapshot;
};

struct MRBentoCompareSetup {
	MRBentoCompareSetup() noexcept;

	MRBentoCompareSource original;
	MRBentoCompareSource compare;
};

struct MRBentoWorkspaceNode {
	MRBentoWorkspaceNode() noexcept;

	int kind;
	int orientation;
	int dividerPosition;
	int firstChild;
	int secondChild;
	int leafId;
};

struct MRBentoWorkspaceLeaf {
	MRBentoWorkspaceLeaf() noexcept;

	int id;
	MRBentoPaneRole role;
	bool visible;
	std::uint32_t widgetMask;
};

struct MRBentoWorkspaceSnapshot {
	MRBentoWorkspaceSnapshot() noexcept;

	MRBentoBoxMode mode;
	int rootNode;
	int activeLeafId;
	int maximizedLeafId;
	std::vector<MRBentoWorkspaceNode> nodes;
	std::vector<MRBentoWorkspaceLeaf> leaves;
};

struct MRMacroDebuggerWorkspaceBreakpoint {
	MRMacroDebuggerWorkspaceBreakpoint() noexcept;

	std::string macroKey;
	std::string sourceIdentity;
	int line;
	bool enabled;
	std::string conditionText;
};

struct MRMacroDebuggerWorkspaceWatch {
	MRMacroDebuggerWorkspaceWatch() noexcept;

	std::string expression;
	bool enabled;
};

struct MRMacroDebuggerWorkspaceConfiguration {
	MRMacroDebuggerWorkspaceConfiguration() noexcept;

	std::string macroKey;
	std::string macroName;
	std::string sourceIdentity;
	std::string sourcePath;
	std::vector<MRMacroDebuggerWorkspaceBreakpoint> breakpoints;
	std::vector<MRMacroDebuggerWorkspaceWatch> watches;
};

class MRPaneEditWindow : public MREditWindow {
	friend class MRBentoBox;

  public:
	virtual ~MRPaneEditWindow() override;

  protected:
	MRPaneEditWindow(const TRect &bounds, const char *title, int number);

		virtual void changeBounds(const TRect &bounds) override;
		virtual void draw() override;
		virtual void handleEvent(TEvent &event) override;
		virtual TColorAttr mapColor(uchar index) override;
		virtual Boolean valid(ushort command) override;
		virtual void cancelTransientInput() noexcept;
		[[nodiscard]] virtual bool completeTransientInput() noexcept;
		[[nodiscard]] virtual bool usesNativeEditorChrome() const noexcept;
		[[nodiscard]] virtual bool ownsPaneWheelEvents() const noexcept;
		[[nodiscard]] virtual bool projectsPaneContentLocally() const noexcept;
		static TFrame *initFrame(TRect bounds);

  private:

	void setPaneSpec(const MRBentoPaneSpec &spec, const MRFileEditor *sourceEditor) noexcept;
		void setPaneFocused(bool focused) noexcept;
		void applyPanePolicy(const MRFileEditor *sourceEditor) noexcept;
		void layoutPaneChrome() noexcept;
		void configurePaneScrollBarColors() noexcept;
		void drawPaneScrollBars() noexcept;

	MRBentoPaneSpec mPaneSpec;
	bool mPaneFocused;
};

class MRBentoBox : public MREditWindow {
	friend class MRDebuggerValueInput;
	friend class MRPaneEditWindow;

  public:
	MRBentoBox(const TRect &bounds, const char *title, int number, MRBentoBoxMode mode = bbmToolWorkspace);
	virtual ~MRBentoBox() override;

	[[nodiscard]] MREditWindow *secondaryEditWindow() const noexcept;
		[[nodiscard]] MREditWindow *buildOutputPane() const noexcept;
		[[nodiscard]] MREditWindow *problemsPane() const noexcept;
		[[nodiscard]] MREditWindow *structurePane() const noexcept;
		[[nodiscard]] MREditWindow *functionsPane() const noexcept;
		[[nodiscard]] MREditWindow *debuggerOutputPane() const noexcept;
		[[nodiscard]] MREditWindow *variablesPane() const noexcept;
		[[nodiscard]] MREditWindow *watchesPane() const noexcept;
		[[nodiscard]] MRGdbTerminalPane *programTerminalPane() const noexcept;
		[[nodiscard]] MREditWindow *paneForBufferId(int bufferId) const noexcept;
	void collectVisiblePaneWindows(std::vector<MREditWindow *> &windows) const noexcept;
	void showSecondaryPane() noexcept;
		[[nodiscard]] bool ensureBuildDiagnosticsPanes(MREditWindow *&outputWindow, MREditWindow *&problemsWindow);
		[[nodiscard]] bool ensureMacroDebuggerPanes(MREditWindow *&outputWindow, MREditWindow *&variablesWindow, MREditWindow *&watchesWindow);
		[[nodiscard]] bool ensureGdbDebuggerPanes(MREditWindow *&outputWindow, MREditWindow *&variablesWindow, MREditWindow *&watchesWindow, MRGdbTerminalPane *&terminalWindow);
		[[nodiscard]] bool startGdbDebugger(const std::string &programPath, const std::string &sourcePath, std::string &errorMessage);
		void stopGdbDebugger() noexcept;
		void stopGdbDebuggerForRebuild() noexcept;
		[[nodiscard]] bool startGdbInferior();
		[[nodiscard]] bool acceptGdbEvent(const mr::coprocessor::GdbEventPayload &payload);
		[[nodiscard]] bool sendGdbTerminalInput(const std::string &text);
		[[nodiscard]] bool clearGdbProgramTerminal();
		[[nodiscard]] bool executeGdbSourceContextCommand(ushort command, std::size_t sourceOffset, const std::string &identifier);
		void resizeGdbTerminal(int columns, int rows);
		void setMacroDebuggerTarget(const std::string &macroKey, const std::string &macroName);
		[[nodiscard]] bool macroDebuggerWorkspaceConfiguration(MRMacroDebuggerWorkspaceConfiguration &configuration) const;
		void restoreMacroDebuggerWorkspaceConfiguration(const MRMacroDebuggerWorkspaceConfiguration &configuration);
		void setMacroDebuggerSession(MRMacroExecutionSessionId sessionId) noexcept;
		void setMacroDebuggerSession(MRMacroExecutionSessionId sessionId, const std::vector<MRMacroDebugVariableSnapshot> &variables);
		[[nodiscard]] bool macroDebuggerTargetsSourceIdentity(const std::string &sourcePath, const std::string &macroName) const noexcept;
		[[nodiscard]] const std::string &macroDebuggerSourceIdentityValue() const noexcept;
		[[nodiscard]] bool macroDebuggerObservesSourceIdentity(const std::string &sourcePath, const std::string &macroName) const noexcept;
		[[nodiscard]] bool acceptScheduledMacroDebuggerSession(MRMacroExecutionSessionId sessionId, const MRMacroDebugRunResult &debugResult);
		[[nodiscard]] bool acceptMacroDebuggerWorkerResult(MRMacroExecutionSessionId sessionId, std::uint64_t taskId, const MRMacroDebugRunResult &debugResult, const std::string &errorMessage);
	void refreshMacroDebuggerRunMarkers(const MRMacroDebugRunResult &debugResult);
	void refreshMacroDebuggerWatches();
	[[nodiscard]] bool macroDebuggerFunctionKeysActive() const noexcept;
	[[nodiscard]] bool macroDebuggerHasLiveSession() const noexcept;
	[[nodiscard]] bool macroDebuggerSessionRunning() const noexcept;
	void pumpMacroDebuggerSession();
	[[nodiscard]] bool handleMacroDebuggerFunctionKey(TEvent &event);
	[[nodiscard]] bool debuggerFunctionKeysActive() const noexcept;
	[[nodiscard]] bool debuggerHasLiveSession() const noexcept;
	[[nodiscard]] bool debuggerSessionRunning() const;
	[[nodiscard]] bool gdbDebuggerActive() const noexcept;
	[[nodiscard]] bool handleDebuggerFunctionKey(TEvent &event);
		void activatePrimaryPane() noexcept;
	void activateSecondaryPane() noexcept;
	[[nodiscard]] bool activatePaneWindow(MREditWindow *pane) noexcept;
	void toggleActivePane() noexcept;
	void setCompilerOutputStatus(const char *status);
	void clearCompilerDiagnostics();
	[[nodiscard]] bool hasCompilerProblems() const noexcept;
		[[nodiscard]] bool refreshCompilerDiagnosticsFromOutput();
		[[nodiscard]] bool requestCompilerProblemNavigation(bool forward);
		[[nodiscard]] bool jumpToProblemAtCursor();
	[[nodiscard]] bool jumpToNextProblem();
	[[nodiscard]] bool jumpToPreviousProblem();
	[[nodiscard]] bool splitActiveEditorPane(MRBentoPanePlacement placement);
		[[nodiscard]] bool initializeFileCompare(MRBentoCompareSetup setup);
		[[nodiscard]] bool startFileCompareProjection();
		[[nodiscard]] bool isFileCompareBox() const noexcept;
	[[nodiscard]] bool navigateFileCompareChange(bool next);
	[[nodiscard]] bool applyFileCompareChange(bool originalToCompare);
	[[nodiscard]] bool fileCompareWorkspaceSourcePaths(std::string &originalPath, std::string &comparePath) const;
	[[nodiscard]] bool containsFileCompareSourceWindow(const MREditWindow *window) const noexcept;
	void refreshFileCompareConfiguration();
	bool refreshFileCompareAfterEditorMutation(const MREditWindow *window);
	[[nodiscard]] bool applyFileCompareResult(const mr::coprocessor::Result &result);
	[[nodiscard]] bool applyBentoProjectionResult(const mr::coprocessor::Result &result);
	void cancelBentoProjectionForPane(int paneBufferId);
	void restoreFileCompareSources() noexcept;
	[[nodiscard]] MRBentoWorkspaceSnapshot workspaceSnapshot() const;
	[[nodiscard]] bool restoreWorkspaceSnapshot(const MRBentoWorkspaceSnapshot &snapshot);
	void dismissPaneMenus() noexcept;
	void refreshBentoColorTheme() noexcept;

	virtual void draw() override;
	virtual void changeBounds(const TRect &bounds) override;
	virtual void layoutDesktopContents() override;
	virtual void close() override;
	virtual void handleEvent(TEvent &event) override;
	virtual void setState(ushort aState, Boolean enable) override;
	virtual void shutDown() override;
	virtual TColorAttr mapColor(uchar index) override;
	virtual bool allowsDocumentViewportSplit() const noexcept override;
	virtual MREditWindow *editorCommandTarget() noexcept override;
	virtual const MREditWindow *editorCommandTarget() const noexcept override;
	virtual bool showsFrameGrowHandle() const noexcept override;

  protected:
	[[nodiscard]] virtual MRPaneEditWindow *createPaneWindow(const TRect &bounds, const char *title, int number, const MRBentoPaneSpec &spec);
	[[nodiscard]] virtual bool primaryPaneUsesDedicatedWindow() const noexcept;
	[[nodiscard]] virtual bool acceptsPaneRole(MRBentoPaneRole role) const noexcept;
	[[nodiscard]] virtual const char *titleForPaneRole(MRBentoPaneRole role) const noexcept;
	[[nodiscard]] virtual MRBentoPaneSpec paneSpecForRole(MRBentoPaneRole role) const noexcept;
	virtual void activePaneRoleChanged(MRBentoPaneRole role) noexcept;
	[[nodiscard]] virtual bool paneCloseActionEnabled() const noexcept;
	[[nodiscard]] virtual bool paneMaximizeActionEnabled() const noexcept;
	[[nodiscard]] virtual bool projectPaneDividerPosition(int nodeIndex, int position) noexcept;
	virtual void paneLayoutChanged() noexcept;
	[[nodiscard]] MRPaneEditWindow *paneWindowForRole(MRBentoPaneRole role) const noexcept;
	void refreshPaneChromeProjection() noexcept;
	[[nodiscard]] int paneDividerPosition(int nodeIndex) const noexcept;
	[[nodiscard]] bool setPaneDividerPositionForLayout(int nodeIndex, int position) noexcept;
	[[nodiscard]] TRect paneLayoutBounds() const noexcept;
	static TFrame *initFrame(TRect bounds);

  private:
	enum BentoLayoutNodeKind {
		blnPane = 0,
		blnSplit
	};

	enum BentoSplitOrientation {
		bsoHorizontal = 0,
		bsoVertical
	};

	enum BentoProjectionDirty {
		bpdNone = 0,
		bpdContent = 1 << 0,
		bpdChrome = 1 << 1,
		bpdScrollBar = 1 << 2,
		bpdLayout = 1 << 3,
		bpdOverlay = 1 << 4
	};

	struct BentoLeaf;

	struct BentoLayoutNode {
		BentoLayoutNode() noexcept;

		BentoLayoutNodeKind kind;
		BentoSplitOrientation orientation;
		int dividerPosition;
		int firstChild;
		int secondChild;
		int leafId;
	};

	enum BentoDiagnosticsProjectionRequest {
		bdprNone = 0,
		bdprFormatExisting,
		bdprRemapExisting,
		bdprParseOutput
	};

	struct BentoProjectionTaskState {
		BentoProjectionTaskState() noexcept;

		std::uint64_t taskId;
		std::uint64_t generationCounter;
		std::uint64_t activeGeneration;
		std::size_t sourceDocumentId;
		std::size_t sourceVersion;
		std::size_t inputDocumentId;
		std::size_t inputVersion;
		int inputBufferId;
		std::uint64_t inputRevision;
		MRSyntaxLanguage inputLanguage;
		bool inputComplete;
		std::size_t targetDocumentId;
	std::size_t targetVersion;
	int targetBufferId;
	std::string sourcePath;
	bool trackWarnings;
	bool trackNotes;
		BentoDiagnosticsProjectionRequest diagnosticsRequest;
		BentoDiagnosticsProjectionRequest pendingDiagnosticsRequest;
		std::shared_ptr<const MRBentoDiagnosticSourceChange> diagnosticSourceChanges;
		std::size_t diagnosticBaseDocumentId;
		std::size_t diagnosticBaseVersion;
		bool pending;
		bool pendingForce;
		bool completeCoverageRequested;
		bool projectionCurrent;
		bool retryBlocked;
		std::chrono::steady_clock::time_point requestedAt;
	};

	void initializeLayoutTree() noexcept;
	void layoutSplitPanes();
		void flushBentoProjection() noexcept;
		void layoutSourcePaneChrome(const TRect &content) noexcept;
		void hideSourcePaneChrome() noexcept;
		void configureSourcePaneScrollBarColors() noexcept;
		void drawSourcePaneScrollBars() noexcept;
		void drawSharedEditorPanes() noexcept;
		TColorAttr paneFrameColor(bool focused);
	void drawPaneFrames() noexcept;
	void postCloseCommand() noexcept;
	void closePane(int leafId);
	void showPaneRoleList(TPoint globalMouse, int targetLeafId);
	void showPaneActionList();
	void showFileCompareActionList(TPoint globalMouse, int targetLeafId);
	void acceptPaneRoleChoice();
	void acceptPaneActionChoice();
	void acceptFileCompareActionChoice();
	bool refreshCompilerProblemsPane();
	bool submitCompilerDiagnosticsProjection(BentoDiagnosticsProjectionRequest request);
	bool submitOutlineProjection(MRBentoPaneRole role, bool force);
	bool adoptBentoProjectionText(MREditWindow *targetWindow, const std::shared_ptr<const std::string> &text,
	                              std::size_t expectedDocumentId, std::size_t expectedVersion, const char *title);
	void cancelBentoProjectionTask(BentoProjectionTaskState &state) noexcept;
	void cancelAllBentoProjectionTasks() noexcept;
	void handleCommittedSourceEditor(MRFileEditor *committedEditor);
	bool compilerDiagnosticsContextEstablished() const noexcept;
	bool compilerDiagnosticsCurrent() const;
	void resumePendingBentoProjection(BentoProjectionTaskState &state, MRBentoPaneRole role);
	[[nodiscard]] bool toggleMacroDebuggerBreakpointAtCursor();
	[[nodiscard]] bool toggleMacroDebuggerBreakpointEnabledAtCursor();
	[[nodiscard]] bool toggleMacroDebuggerBreakpointsEnabled();
	[[nodiscard]] bool eraseMacroDebuggerBreakpoints();
	[[nodiscard]] bool continueMacroDebuggerSession();
	[[nodiscard]] bool stepMacroDebuggerSession(MRMacroDebugStepMode mode);
	[[nodiscard]] bool stopMacroDebuggerSession();
	[[nodiscard]] bool resetMacroDebuggerSession();
	[[nodiscard]] bool runMacroDebuggerToCursor();
	[[nodiscard]] bool addMacroDebuggerWatch();
	[[nodiscard]] bool eraseMacroDebuggerWatch();
	[[nodiscard]] bool evaluateMacroDebuggerExpression();
	void refreshMacroDebuggerVariables(const std::vector<MRMacroDebugVariableSnapshot> &variables);
	[[nodiscard]] bool showMacroDebuggerValueInputAtCursor();
	[[nodiscard]] bool showGdbDebuggerValueInputAtCursor();
	[[nodiscard]] bool debuggerValueInputContains(const TPoint &point) const noexcept;
	void commitDebuggerValueInput();
	void cancelDebuggerValueInput() noexcept;
	void writeMacroDebuggerStatus(const MRMacroDebugRunResult &debugResult, const std::string &errorMessage);
	void writeMacroDebuggerNotice(const std::string &message);
	void refreshMacroDebuggerBreakpointRanges();
	void invalidateMacroDebuggerRuntime();
	[[nodiscard]] bool scheduleMacroDebuggerWorkerAction(MRMacroDebugWorkerAction action);
	[[nodiscard]] bool startMacroDebuggerSession(int temporaryStopLine);
	[[nodiscard]] bool handleGdbDebuggerFunctionKey(TEvent &event);
	[[nodiscard]] bool sendGdbCommand(MRGdbCommandKind commandKind, const std::string &text = std::string(), const std::string &objectName = std::string());
	void publishGdbDebuggerState(const char *state, const std::string &file = std::string(), int line = 0);
	void clearGdbDebuggerState() noexcept;
	[[nodiscard]] std::string gdbDebuggerStateText() const;
	[[nodiscard]] std::string gdbDebuggerSourcePath() const;
	[[nodiscard]] bool gdbDebuggerRunning() const;
	void refreshOutlinePanes(bool force = false);
	bool refreshOutlinePane(MRBentoPaneRole role, bool force);
	[[nodiscard]] bool jumpToOutlineAtCursor(MRBentoPaneRole role);
	void refreshFileComparePanes();
	void refreshFileComparePane(BentoLeaf &leaf);
	void refreshFileCompareSourceSnapshot(MRBentoCompareSource &source, MREditWindow *window, bool force);
	void refreshFileCompareCachedSnapshots(MRBentoPaneRole changedRole, bool force);
	void refreshFileCompareAfterSourceMutation(MRBentoPaneRole changedRole = bprSource);
	void cancelFileComparePipeline() noexcept;
	[[nodiscard]] bool submitFileCompareAcquisition(bool original);
	[[nodiscard]] bool submitFileCompareDiff();
	[[nodiscard]] bool submitFileComparePaneProjection(bool original);
	[[nodiscard]] MREditWindow *fileComparePaneWindow(bool original) const noexcept;
	[[nodiscard]] bool fileComparePanesEditable() const noexcept;
	[[nodiscard]] std::size_t fileCompareGroupStartLineForRole(const MRBentoFileCompareChangeGroup &group, MRBentoPaneRole role, bool editablePanes) const noexcept;
	[[nodiscard]] std::size_t fileCompareGroupLineCountForRole(const MRBentoFileCompareChangeGroup &group, MRBentoPaneRole role, bool editablePanes) const noexcept;
	[[nodiscard]] std::size_t fileCompareGroupEffectiveLineCountForRole(const MRBentoFileCompareChangeGroup &group, MRBentoPaneRole role, bool editablePanes) const noexcept;
	[[nodiscard]] std::size_t fileCompareGroupNavigationLineForRole(const MRBentoFileCompareChangeGroup &group, MRBentoPaneRole role, const MRFileEditor &editor, bool editablePanes) const;
	[[nodiscard]] std::size_t fileCompareMappedLineForRole(MRBentoPaneRole sourceRole, std::size_t sourceLine, const MRFileEditor &targetEditor, bool editablePanes) const noexcept;
	[[nodiscard]] const MRBentoFileCompareChangeGroup *fileCompareChangeGroupAtOrVisibleForRole(MRBentoPaneRole role, const MRFileEditor &editor, bool editablePanes) const noexcept;
	[[nodiscard]] int fileCompareChangeGroupIndexAtLine(MRBentoPaneRole role, std::size_t line, bool editablePanes) const noexcept;
	[[nodiscard]] int fileCompareChangeGroupIndexAtCursor(MRBentoPaneRole role, const MRFileEditor &editor, bool editablePanes) const noexcept;
	[[nodiscard]] bool moveFileCompareEditorToGroup(MRFileEditor &editor, MRBentoPaneRole role, const MRBentoFileCompareChangeGroup &group, bool editablePanes);
	[[nodiscard]] bool applyFileCompareChangeGroup(bool originalToCompare, const MRBentoFileCompareChangeGroup &group);
	[[nodiscard]] std::string fileCompareStatusForLeaf(const BentoLeaf &leaf) const;
	[[nodiscard]] bool jumpToFileCompareChange(bool next);
	void syncFileCompareLinkedPaneFrom(int sourceLeafId, bool syncCursor = true);
	void syncCompilerDiagnosticsAfterSourceMutation(const MRTextBufferModel::ReadSnapshot &oldSnapshot, const MRTextBufferModel::DocumentChangeSet &changeSet);
	void clearTrackedCompilerSidekick(bool dropSidekick) noexcept;
	void trackCompilerSidekick(std::size_t diagnosticIndex) noexcept;
	void updateTrackedCompilerSidekick();
	[[nodiscard]] bool placePaneRoleInContext(MRBentoPaneRole role, MRBentoPanePlacement placement, int targetLeafId);
	[[nodiscard]] bool handlePaneDropListEvent(TEvent &event);
	[[nodiscard]] bool handleOuterFrameCloseMouse(TEvent &event);
	void updatePaneRoleListChrome() noexcept;
	[[nodiscard]] short paneRoleIndexAt(TPoint globalMouse);
	[[nodiscard]] bool dragDivider(TEvent &event, int nodeIndex, int paneLeafId) noexcept;
	[[nodiscard]] int materializeHorizontalDividerForPane(int nodeIndex, int paneLeafId, bool &layoutChanged) noexcept;
	[[nodiscard]] int horizontalDividerNodeForPaneFrame(int leafId, TPoint point) const noexcept;
	void setDividerPosition(int nodeIndex, int position, bool markWorkspace) noexcept;
	void setDividerPosition(int position) noexcept;
	void setActivePane(int leafId) noexcept;
	void updateActivePaneFrame() noexcept;
	void setActivePaneForMouse(TPoint globalMouse) noexcept;
	void toggleLeafMaximized(int leafId) noexcept;
	[[nodiscard]] bool handleDividerChromeMouse(TEvent &event);
	[[nodiscard]] TRect paneBoundsForLeaf(int leafId) const noexcept;
	[[nodiscard]] int defaultDividerPosition() const noexcept;
	[[nodiscard]] int defaultDividerPosition(int nodeIndex) const noexcept;
	[[nodiscard]] int clampedDividerPosition(int position) const noexcept;
	[[nodiscard]] int clampedDividerPosition(int nodeIndex, int position) const noexcept;
	[[nodiscard]] int minimumNodeWidth(int nodeIndex) const noexcept;
	[[nodiscard]] int minimumNodeHeight(int nodeIndex) const noexcept;
	[[nodiscard]] int currentDividerPosition() const noexcept;
	[[nodiscard]] int currentDividerPosition(int nodeIndex) const noexcept;
	[[nodiscard]] bool hasPaneSplit() const noexcept;
	[[nodiscard]] bool pointInRect(TPoint point, const TRect &rect) const noexcept;
	[[nodiscard]] int leafAt(TPoint point) const noexcept;
	[[nodiscard]] int nodeAtDivider(TPoint point) const noexcept;
	[[nodiscard]] MRPaneEditWindow *paneWindowForLeaf(int leafId) const noexcept;
	[[nodiscard]] MRBentoPaneRole roleForLeaf(int leafId) const noexcept;
	[[nodiscard]] bool titleMenuEnabledForLeaf(int leafId) const noexcept;
	[[nodiscard]] int firstToolLeafId() const noexcept;
	[[nodiscard]] int leafIdForRole(MRBentoPaneRole role) const noexcept;
	[[nodiscard]] int nodeIndexForLeaf(int leafId) const noexcept;
	[[nodiscard]] int parentNodeOf(int childNodeIndex) const noexcept;
	[[nodiscard]] int viewportNumberForLeaf(int leafId) const noexcept;
	[[nodiscard]] TRect nodeBounds(int nodeIndex) const noexcept;
	[[nodiscard]] TRect contentBounds(const TRect &paneBounds) const noexcept;
	[[nodiscard]] int createPaneLeaf(const MRBentoPaneSpec &spec);
	[[nodiscard]] int createLeafNode(int leafId);
	[[nodiscard]] int splitLeafNode(int leafId, BentoSplitOrientation orientation, MRBentoPaneRole newRole);
	[[nodiscard]] int splitLeafNode(int leafId, BentoSplitOrientation orientation, const MRBentoPaneSpec &spec);
	void collapseLeafNode(int leafId);
	void layoutNode(int nodeIndex, const TRect &bounds);
	void ensurePaneFrameViews();
	[[nodiscard]] std::string paneTitleForLeaf(const BentoLeaf &leaf) const;
	[[nodiscard]] std::vector<std::string> paneRoleChoices() const;
	[[nodiscard]] std::vector<std::string> paneActionChoices() const;
	[[nodiscard]] MRBentoPanePlacement panePlacementForAction(const std::string &action) const noexcept;

	struct BentoLeaf {
		BentoLeaf() noexcept;

		int id;
		MRBentoPaneRole role;
		MRBentoPaneSpec spec;
		std::string title;
		MRPaneEditWindow *pane;
		TRect bounds;
		bool visible;
	};

	MRPaneEditWindow *secondaryPane;
	std::vector<BentoLayoutNode> layoutTree;
	std::vector<BentoLeaf> leaves;
	std::vector<MRBentoPaneFrameView *> paneFrameViews;
	int rootNode;
	int activeLeafId;
	int nextLeafId;
	int maximizedLeafId;
	MRBentoBoxMode bentoMode;
	bool sourceScrollBarPaletteActive;
	bool secondaryPaneVisible;
	bool windowCloseInProgress;
	bool bentoProjectionAdoptionActive;
	bool bentoSourceMutationTrackingActive;
	unsigned bentoProjectionDirty;
	MRDropList paneRoleDropList;
	MRDropList paneActionDropList;
	MRDropList fileCompareActionDropList;
	TRect paneRoleListAnchor;
	MRBentoPaneRole pendingPaneRole;
	int pendingPaneRoleTargetLeafId;
	int pendingFileCompareActionLeafId;
	int pendingFileCompareActionGroupIndex;
	std::string compilerOutputStatus;
	std::string compilerProblemsStatus;
	std::string structureOutlineStatus;
	std::string functionsOutlineStatus;
	std::string macroDebuggerStatus;
	std::string macroDebuggerMacroKey;
	std::string macroDebuggerMacroName;
	std::string macroDebuggerSourcePath;
	std::string macroDebuggerSourceIdentity;
	std::string macroDebuggerProjectedMacroKey;
	MRMacroExecutionSessionId macroDebuggerSessionId;
	MRMacroExecutionRoute macroDebuggerExecutionRoute;
	bool macroDebuggerExecutionRunning;
	bool macroDebuggerActive;
	struct GdbDebuggerVariableRow {
		std::size_t start;
		std::size_t end;
		std::string expression;
		std::string objectName;
		std::string value;
	};

	MRDebuggerValueInput *debuggerValueInput;
	MRPaneEditWindow *debuggerValueInputPane;
	std::string gdbDebuggerValueInputObjectName;
	std::vector<MRMacroDebugVariableSnapshot> macroDebuggerVariables;
	std::vector<std::pair<std::size_t, std::size_t>> macroDebuggerVariableRows;
	std::vector<GdbDebuggerVariableRow> gdbDebuggerVariableRows;
	std::unique_ptr<MRGdbSession> gdbSession;
	std::shared_ptr<const std::vector<MRCompilerDiagnostic>> compilerDiagnostics;
	std::shared_ptr<const MRBentoDiagnosticSourceChange> compilerDiagnosticSourceChanges;
	std::shared_ptr<const MRTextBufferModel::ReadSnapshot> compilerDiagnosticsParseSourceSnapshot;
	std::size_t compilerDiagnosticsDocumentId;
	std::size_t compilerDiagnosticsVersion;
	std::size_t compilerDiagnosticsOutputDocumentId;
	std::size_t compilerDiagnosticsOutputVersion;
	int compilerDiagnosticsOutputBufferId;
	std::size_t compilerProblemsTargetDocumentId;
	std::size_t compilerProblemsTargetVersion;
	int compilerProblemsTargetBufferId;
	std::size_t compilerProblemsTextLength;
	std::uint64_t compilerProblemsTextHash;
	bool compilerDiagnosticsParseRequired;
	bool compilerDiagnosticsSourceInvalidated;
	int pendingCompilerProblemNavigation;
	MRBentoCompareSetup fileCompareSetup;
	MRBentoFileComparePipelineState fileComparePipeline;
	bool fileCompareSourcesRestored;
	bool fileCompareDiffReady;
	bool fileCompareStale;
	bool fileCompareLinkedPaneSyncActive;
	MRBentoOutlinePaneState structureOutlineState;
	MRBentoOutlinePaneState functionsOutlineState;
	BentoProjectionTaskState diagnosticsProjectionTask;
	BentoProjectionTaskState structureProjectionTask;
	BentoProjectionTaskState functionsProjectionTask;
	std::shared_ptr<const std::vector<MRBentoOutlineEntry>> structureOutlineEntries;
	std::shared_ptr<const std::vector<MRBentoOutlineEntry>> functionsOutlineEntries;
	bool compilerSidekickTracked;
	bool compilerSidekickUpdating;
	std::size_t compilerSidekickDiagnosticIndex;
};

#endif
