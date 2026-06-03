#ifndef MRBENTOBOX_HPP
#define MRBENTOBOX_HPP

#include "MRDropList.hpp"
#include "MREditWindow.hpp"

#include <cstddef>
#include <string>
#include <vector>

class MRBentoPaneFrameView;

enum MRBentoPaneRole {
	bprSource = 0,
	bprCompilerOutput,
	bprAppOutput,
	bprProblems,
	bprDebuggerOutput,
	bprWatches,
	bprVariables,
	bprSplitEditor
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
	bbmDocumentViewports
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
	void drawPaneScrollBars() noexcept;
	static TFrame *initFrame(TRect bounds);

	MRBentoPaneSpec mPaneSpec;
	bool mPaneFocused;
};

class MRBentoBox : public MREditWindow {
  public:
	MRBentoBox(const TRect &bounds, const char *title, int number, MRBentoBoxMode mode = bbmToolWorkspace);
	virtual ~MRBentoBox() override;

	[[nodiscard]] MREditWindow *secondaryEditWindow() const noexcept;
	[[nodiscard]] MREditWindow *buildOutputPane() const noexcept;
	[[nodiscard]] MREditWindow *problemsPane() const noexcept;
	[[nodiscard]] MREditWindow *paneForBufferId(int bufferId) const noexcept;
	void collectVisiblePaneWindows(std::vector<MREditWindow *> &windows) const noexcept;
	void showSecondaryPane() noexcept;
	[[nodiscard]] bool ensureBuildDiagnosticsPanes(MREditWindow *&outputWindow, MREditWindow *&problemsWindow);
	void activatePrimaryPane() noexcept;
	void activateSecondaryPane() noexcept;
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

	void initializeLayoutTree() noexcept;
	void layoutSplitPanes();
	void flushBentoProjection() noexcept;
	void layoutSourcePaneChrome(const TRect &content) noexcept;
	void hideSourcePaneChrome() noexcept;
	void drawSourcePaneScrollBars() noexcept;
	void drawSharedEditorPanes() noexcept;
	void drawPaneFrames() noexcept;
	void drawPaneFrame(std::size_t leafIndex) noexcept;
	void postCloseCommand() noexcept;
	void closePane(int leafId) noexcept;
	void closeSecondaryPane() noexcept;
	void showPaneRoleList(TPoint globalMouse, int targetLeafId);
	void showPaneActionList();
	void acceptPaneRoleChoice();
	void acceptPaneActionChoice();
	void refreshCompilerProblemsPane();
	void refreshSourceCompilerDiagnosticRanges();
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
	TRect paneRoleListAnchor;
	MRBentoPaneRole pendingPaneRole;
	int pendingPaneRoleTargetLeafId;
	std::string compilerOutputStatus;
	std::string compilerProblemsStatus;
	std::vector<MRCompilerDiagnostic> compilerDiagnostics;
	bool compilerSidekickTracked;
	bool compilerSidekickUpdating;
	std::size_t compilerSidekickDiagnosticIndex;
};

#endif
