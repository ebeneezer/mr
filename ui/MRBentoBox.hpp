#ifndef MRBENTOBOX_HPP
#define MRBENTOBOX_HPP

#include "MREditWindow.hpp"
#include "widgets/MRDropList.hpp"
#include "MRDiff.hpp"
#include "../mrmac/MRMacroExecutionSession.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class MRBentoBoxFileCompareRegressionHarness;
class MRBentoPaneFrameView;
class MRMacroDebuggerValueInput;
struct MRMacroDebugRunResult;
enum MRMacroDebugStepMode : int;
namespace mr {
namespace coprocessor {
struct Result;
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
	bprDiffCompare
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

	MRBentoPaneRole role;
	MRBentoPaneBufferPolicy bufferPolicy;
	bool readOnly;
	bool suppressMiniMap;
	bool suppressWordWrap;
	bool scrollBarsAlwaysVisible;
	const MRBentoPaneTitleMenuSpec *titleMenu;
};

struct MRCompilerDiagnostic {
	MRCompilerDiagnostic() noexcept;

	std::string sourcePath;
	std::size_t sourceLine;
	std::size_t sourceColumn;
	std::string severity;
	std::string text;
	std::size_t sourceOffset;
	std::size_t outputOffset;
	std::size_t problemOffset;
	bool sourceAvailable;
};

struct MRBentoOutlineEntry {
	MRBentoOutlineEntry() noexcept;

	std::size_t paneOffset;
	std::size_t sourceOffset;
	std::size_t sourceSelectionEnd;
};

struct MRBentoOutlinePaneState {
	MRBentoOutlinePaneState() noexcept;

	std::size_t documentId;
	std::size_t version;
	std::uint64_t textHash;
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
	std::string text;
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
	int line;
	bool enabled;
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
	std::vector<MRMacroDebuggerWorkspaceBreakpoint> breakpoints;
	std::vector<MRMacroDebuggerWorkspaceWatch> watches;
};

class MRPaneEditWindow : public MREditWindow {
	friend class MRBentoBox;

  public:
	[[nodiscard]] bool paneOwned() const noexcept;
	[[nodiscard]] MRBentoPaneRole paneRole() const noexcept;

  private:
	MRPaneEditWindow(const TRect &bounds, const char *title, int number);
	virtual ~MRPaneEditWindow() override;

		virtual void changeBounds(const TRect &bounds) override;
		virtual void draw() override;
		virtual TColorAttr mapColor(uchar index) override;
		virtual Boolean valid(ushort command) override;

	void setPaneSpec(const MRBentoPaneSpec &spec, const MRFileEditor *sourceEditor) noexcept;
		void setPaneFocused(bool focused) noexcept;
		void applyPanePolicy(const MRFileEditor *sourceEditor) noexcept;
		void layoutPaneChrome() noexcept;
		void configurePaneScrollBarColors() noexcept;
		void drawPaneScrollBars() noexcept;
		static TFrame *initFrame(TRect bounds);

	MRBentoPaneSpec mPaneSpec;
	bool mPaneFocused;
};

class MRBentoBox : public MREditWindow {
	friend class MRBentoBoxFileCompareRegressionHarness;
	friend class MRMacroDebuggerValueInput;

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
		[[nodiscard]] MREditWindow *paneForBufferId(int bufferId) const noexcept;
	void collectVisiblePaneWindows(std::vector<MREditWindow *> &windows) const noexcept;
	void showSecondaryPane() noexcept;
		[[nodiscard]] bool ensureBuildDiagnosticsPanes(MREditWindow *&outputWindow, MREditWindow *&problemsWindow);
		[[nodiscard]] bool ensureMacroDebuggerPanes(MREditWindow *&outputWindow, MREditWindow *&variablesWindow, MREditWindow *&watchesWindow);
		void setMacroDebuggerTarget(const std::string &macroKey, const std::string &macroName);
		[[nodiscard]] bool macroDebuggerWorkspaceConfiguration(MRMacroDebuggerWorkspaceConfiguration &configuration) const;
		void restoreMacroDebuggerWorkspaceConfiguration(const MRMacroDebuggerWorkspaceConfiguration &configuration);
		void setMacroDebuggerSession(MRMacroExecutionSessionId sessionId) noexcept;
		void setMacroDebuggerSession(MRMacroExecutionSessionId sessionId, const std::vector<MRMacroDebugVariableSnapshot> &variables);
		[[nodiscard]] bool macroDebuggerObservesSourcePath(const std::string &sourcePath) const noexcept;
		[[nodiscard]] bool acceptScheduledMacroDebuggerSession(MRMacroExecutionSessionId sessionId, const MRMacroDebugRunResult &debugResult);
	void refreshMacroDebuggerRunMarkers(const MRMacroDebugRunResult &debugResult);
	void refreshMacroDebuggerWatches();
	[[nodiscard]] bool macroDebuggerFunctionKeysActive() const noexcept;
	[[nodiscard]] bool macroDebuggerHasLiveSession() const noexcept;
	[[nodiscard]] bool macroDebuggerSessionRunning() const noexcept;
	void pumpMacroDebuggerSession();
	[[nodiscard]] bool handleMacroDebuggerFunctionKey(TEvent &event);
		void activatePrimaryPane() noexcept;
	void activateSecondaryPane() noexcept;
	[[nodiscard]] bool activatePaneWindow(MREditWindow *pane) noexcept;
	void toggleActivePane() noexcept;
	void setCompilerOutputStatus(const char *status);
	void clearCompilerDiagnostics();
	[[nodiscard]] bool hasCompilerProblems() const noexcept;
	[[nodiscard]] bool refreshCompilerDiagnosticsFromOutput();
	[[nodiscard]] bool jumpToProblemAtCursor();
	[[nodiscard]] bool jumpToNextProblem();
	[[nodiscard]] bool jumpToPreviousProblem();
	[[nodiscard]] bool placePaneRole(MRBentoPaneRole role, MRBentoPanePlacement placement);
	[[nodiscard]] bool splitActiveEditorPane(MRBentoPanePlacement placement);
		[[nodiscard]] bool initializeFileCompare(const MRBentoCompareSetup &setup);
		[[nodiscard]] bool isFileCompareBox() const noexcept;
	[[nodiscard]] bool navigateFileCompareChange(bool next);
	[[nodiscard]] bool applyFileCompareChange(bool originalToCompare);
	[[nodiscard]] bool fileCompareWorkspaceSourcePaths(std::string &originalPath, std::string &comparePath) const;
	[[nodiscard]] bool containsFileCompareSourceWindow(const MREditWindow *window) const noexcept;
	void refreshFileCompareConfiguration();
	bool refreshFileCompareAfterEditorMutation(const MREditWindow *window);
	[[nodiscard]] bool applyFileCompareResult(const mr::coprocessor::Result &result);
	void setFileCompareTask(std::uint64_t taskId) noexcept;
	void restoreFileCompareSources() noexcept;
	[[nodiscard]] MRBentoWorkspaceSnapshot workspaceSnapshot() const;
	[[nodiscard]] bool restoreWorkspaceSnapshot(const MRBentoWorkspaceSnapshot &snapshot);
	void refreshBentoColorTheme() noexcept;

	virtual void draw() override;
	virtual void changeBounds(const TRect &bounds) override;
	virtual void close() override;
	virtual void handleEvent(TEvent &event) override;
	virtual void setState(ushort aState, Boolean enable) override;
	virtual void shutDown() override;
	virtual TColorAttr mapColor(uchar index) override;
	virtual bool allowsDocumentViewportSplit() const noexcept override;
	virtual MREditWindow *editorCommandTarget() noexcept override;
	virtual const MREditWindow *editorCommandTarget() const noexcept override;
	virtual bool showsFrameGrowHandle() const noexcept override;

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

	struct FileCompareChangeGroup {
		FileCompareChangeGroup() noexcept;

		std::size_t displayStartLine;
		std::size_t originalStartLine;
		std::size_t compareStartLine;
		std::size_t displayLineCount;
		std::size_t deletedLineCount;
		std::size_t insertedLineCount;
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
	void closePane(int leafId) noexcept;
	void closeSecondaryPane() noexcept;
	void showPaneRoleList(TPoint globalMouse, int targetLeafId);
	void showPaneActionList();
	void showFileCompareActionList(TPoint globalMouse, int targetLeafId);
	void acceptPaneRoleChoice();
	void acceptPaneActionChoice();
	void acceptFileCompareActionChoice();
	void refreshCompilerProblemsPane();
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
	[[nodiscard]] bool macroDebuggerValueInputContains(const TPoint &point) const noexcept;
	void commitMacroDebuggerValueInput();
	void cancelMacroDebuggerValueInput() noexcept;
	void writeMacroDebuggerStatus(const MRMacroDebugRunResult &debugResult, const std::string &errorMessage);
	void writeMacroDebuggerNotice(const std::string &message);
	void refreshMacroDebuggerBreakpointRanges();
	[[nodiscard]] bool startMacroDebuggerSession(int temporaryStopLine);
	void refreshOutlinePanes(bool force = false);
	bool refreshOutlinePane(MRBentoPaneRole role, bool force);
	[[nodiscard]] bool jumpToOutlineAtCursor(MRBentoPaneRole role);
	void refreshSourceCompilerDiagnosticRanges();
	void refreshFileComparePanes();
	void refreshFileComparePane(BentoLeaf &leaf);
	void fileCompareEditableLineKindsForRole(MRBentoPaneRole role, std::vector<unsigned char> &lineKinds, std::vector<MRFileCompareMiniMapSlice> *miniMapSlices = nullptr) const;
	void refreshFileCompareSourceSnapshot(MRBentoCompareSource &source, MREditWindow *window, std::vector<std::string> &lineCache, bool force);
	void refreshFileCompareCachedSnapshots(MRBentoPaneRole changedRole, bool force);
	void rebuildFileCompareProjectionCache();
	void refreshFileCompareAfterSourceMutation(MRBentoPaneRole changedRole = bprSource);
	void rebuildFileCompareChangeGroups();
	[[nodiscard]] bool fileComparePanesEditable() const noexcept;
	[[nodiscard]] std::size_t fileCompareGroupStartLineForRole(const FileCompareChangeGroup &group, MRBentoPaneRole role, bool editablePanes) const noexcept;
	[[nodiscard]] std::size_t fileCompareGroupLineCountForRole(const FileCompareChangeGroup &group, MRBentoPaneRole role, bool editablePanes) const noexcept;
	[[nodiscard]] std::size_t fileCompareGroupEffectiveLineCountForRole(const FileCompareChangeGroup &group, MRBentoPaneRole role, bool editablePanes) const noexcept;
	[[nodiscard]] std::size_t fileCompareGroupNavigationLineForRole(const FileCompareChangeGroup &group, MRBentoPaneRole role, const MRFileEditor &editor, bool editablePanes) const;
	[[nodiscard]] std::size_t fileCompareMappedLineForRole(MRBentoPaneRole sourceRole, std::size_t sourceLine, const MRFileEditor &targetEditor, bool editablePanes) const noexcept;
	[[nodiscard]] const FileCompareChangeGroup *fileCompareChangeGroupAtOrVisibleForRole(MRBentoPaneRole role, const MRFileEditor &editor, bool editablePanes) const noexcept;
	[[nodiscard]] int fileCompareChangeGroupIndexAtLine(MRBentoPaneRole role, std::size_t line, bool editablePanes) const noexcept;
	[[nodiscard]] int fileCompareChangeGroupIndexAtCursor(MRBentoPaneRole role, const MRFileEditor &editor, bool editablePanes) const noexcept;
	[[nodiscard]] bool moveFileCompareEditorToGroup(MRFileEditor &editor, MRBentoPaneRole role, const FileCompareChangeGroup &group, bool editablePanes);
	[[nodiscard]] bool applyFileCompareChangeGroup(bool originalToCompare, const FileCompareChangeGroup &group);
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
	void dragDivider(TEvent &event, int nodeIndex) noexcept;
	void setDividerY(int y) noexcept;
	void setDividerPosition(int nodeIndex, int position) noexcept;
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
	[[nodiscard]] TRect paneLayoutBounds() const noexcept;
	[[nodiscard]] TRect nodeBounds(int nodeIndex) const noexcept;
	[[nodiscard]] TRect contentBounds(const TRect &paneBounds) const noexcept;
	[[nodiscard]] int createToolLeaf(MRBentoPaneRole role);
	[[nodiscard]] int createPaneLeaf(const MRBentoPaneSpec &spec);
	[[nodiscard]] int createLeafNode(int leafId);
	[[nodiscard]] int splitLeafNode(int leafId, BentoSplitOrientation orientation, MRBentoPaneRole newRole);
	[[nodiscard]] int splitLeafNode(int leafId, BentoSplitOrientation orientation, const MRBentoPaneSpec &spec);
	void collapseLeafNode(int leafId) noexcept;
	void layoutNode(int nodeIndex, const TRect &bounds);
	void ensurePaneFrameViews();
	[[nodiscard]] MRBentoPaneSpec paneSpecForRole(MRBentoPaneRole role) const noexcept;
	[[nodiscard]] std::string paneTitleForLeaf(const BentoLeaf &leaf) const;
	[[nodiscard]] std::string fileCompareTextForRole(MRBentoPaneRole role, std::vector<unsigned char> *lineKinds = nullptr) const;
	[[nodiscard]] std::vector<std::string> paneRoleChoices() const;
	[[nodiscard]] std::vector<std::string> paneActionChoices() const;
	[[nodiscard]] MRBentoPanePlacement panePlacementForAction(const std::string &action) const noexcept;

	static TFrame *initFrame(TRect bounds);

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
	std::string macroDebuggerProjectedMacroKey;
	MRMacroExecutionSessionId macroDebuggerSessionId;
	bool macroDebuggerExecutionRunning;
	bool macroDebuggerActive;
	MRMacroDebuggerWorkspaceConfiguration macroDebuggerWorkspacePending;
	MRMacroDebuggerValueInput *macroDebuggerValueInput;
	MRPaneEditWindow *macroDebuggerValueInputPane;
	std::vector<MRMacroDebugVariableSnapshot> macroDebuggerVariables;
	std::vector<std::pair<std::size_t, std::size_t>> macroDebuggerVariableRows;
	std::vector<MRCompilerDiagnostic> compilerDiagnostics;
	MRBentoCompareSetup fileCompareSetup;
	std::vector<mr::diff::MRDiffHunk> fileCompareHunks;
	std::vector<FileCompareChangeGroup> fileCompareChangeGroups;
	std::vector<std::string> fileCompareOriginalLines;
	std::vector<std::string> fileCompareCompareLines;
	std::vector<unsigned char> fileCompareOriginalLineKinds;
	std::vector<unsigned char> fileCompareCompareLineKinds;
	std::vector<MRFileCompareMiniMapSlice> fileCompareOriginalMiniMapSlices;
	std::vector<MRFileCompareMiniMapSlice> fileCompareCompareMiniMapSlices;
	std::uint64_t fileCompareTaskId;
	bool fileCompareSourcesRestored;
	bool fileCompareDiffReady;
	bool fileCompareStale;
	MRBentoOutlinePaneState structureOutlineState;
	MRBentoOutlinePaneState functionsOutlineState;
	std::vector<MRBentoOutlineEntry> structureOutlineEntries;
	std::vector<MRBentoOutlineEntry> functionsOutlineEntries;
	bool compilerSidekickTracked;
	bool compilerSidekickUpdating;
	std::size_t compilerSidekickDiagnosticIndex;
};

#endif
