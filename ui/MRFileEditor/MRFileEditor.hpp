#ifndef MRFILEEDITOR_HPP
#define MRFILEEDITOR_HPP

#define Uses_TScroller
#define Uses_TScrollBar
#define Uses_TEditor
#define Uses_TIndicator
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TClipboard
#define Uses_TText
#define Uses_MsgBox
#include <tvision/tv.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#include "MRCoprocessor.hpp"
#include "../../dialogs/MRDirtyGating.hpp"
#include "../../outline/MROutlineModel.hpp"
#include "../MRIndicator.hpp"
#include "MRMiniMap.hpp"
#include "MRTextFormatting.hpp"
#include "MRTextViewport.hpp"
#include "../MRTextBufferModel.hpp"
#include "../../derivedstate/MRFoldingDerivedState.hpp"
#include "../../derivedstate/MRMiniMapDerivedState.hpp"
#include "../../derivedstate/MRSyntaxDerivedState.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../app/MRCommands.hpp"
#include "../../app/utils/MRFileIOUtils.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "../MRMessageLineController.hpp"
#include "../MRWindowSupport.hpp"

class MREditWindow;
struct MRFoldOutlineInputSnapshot;

std::string mrBuildFoldTrainingAscii(const std::string &text, MRSyntaxLanguage language);
std::string mrBuildOutlineTrainingAscii(const std::string &text, MRSyntaxLanguage language);

class MRFileEditor : public TScroller {
	friend bool mrfeSeedMouseColumnStateForRegression(MRFileEditor &editor, int anchorColumn, int cursorColumn);
	friend bool mrfeRenderedBlockOverlayLineRangeForRegression(const MRFileEditor &editor, std::size_t &line1, std::size_t &line2);
	friend int mrfeLocalXForVisualColumnForRegression(const MRFileEditor &editor, int visualColumn);
	friend bool mrfeRenderedColumnOverlayColumnsForRegression(MRFileEditor &editor, std::size_t lineIndex, int width, int &col1, int &col2);

  public:
	struct LoadTiming {
		bool valid;
		std::size_t bytes;
		std::size_t lines;
		bool linesExact;
		double mappedLoadMs;
		double lineCountMs;

		LoadTiming() noexcept;
	};

	struct DestructionProbe {
		bool active = false;
		int bufferId = 0;
		std::string title;
		std::size_t length = 0;
		std::size_t addBufferLength = 0;
		std::size_t pieceCount = 0;
		std::size_t undoDepth = 0;
		std::size_t redoDepth = 0;
		std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::time_point();

		void arm(int aBufferId, const char *aTitle, std::size_t aLength, std::size_t aAddBufferLength, std::size_t aPieceCount, std::size_t aUndoDepth, std::size_t aRedoDepth) {
			active = true;
			bufferId = aBufferId;
			title = aTitle != nullptr ? aTitle : "?";
			length = aLength;
			addBufferLength = aAddBufferLength;
			pieceCount = aPieceCount;
			undoDepth = aUndoDepth;
			redoDepth = aRedoDepth;
			startedAt = std::chrono::steady_clock::now();
		}

		~DestructionProbe();
	};

	MRFileEditor(const TRect &bounds, TScrollBar *aHScrollBar, TScrollBar *aVScrollBar, TIndicator *aIndicator, TStringView aFileName,
	             mr::coprocessor::ExecutionOwnerKind executionOwnerKind = mr::coprocessor::ExecutionOwnerKind::EditorWindow,
	             std::size_t executionOwnerLocalId = 0) noexcept;

	virtual ~MRFileEditor() override;

	bool isReadOnly() const;

	void setWindowEofMarkerColorOverride(bool enabled, TColorAttr color = 0);

	void setReadOnly(bool readOnly);
	void setForceBinarySave(bool enabled) noexcept;
	[[nodiscard]] bool forceBinarySave() const noexcept;

	const char *persistentFileName() const noexcept;

	std::size_t persistentFileNameCapacity() const noexcept;

	bool hasPersistentFileName() const;

	void setPersistentFileName(TStringView name) noexcept;

	void clearPersistentFileName() noexcept;

	bool isDocumentModified() const noexcept;

	void setDocumentModified(bool changed);

	bool hasUndoHistory() const noexcept;

	bool hasRedoHistory() const noexcept;

	bool insertModeEnabled() const noexcept;
	bool lineDrawingEnabled() const noexcept;
	bool lineDrawingDoubleLines() const noexcept;
	std::size_t displayedCursorLineIndex() const noexcept;
	std::size_t blockCursorLineIndex() const noexcept;

	std::size_t originalBufferLength() const noexcept;

	std::size_t addBufferLength() const noexcept;

	std::size_t pieceCount() const noexcept;

	bool hasMappedOriginalSource() const noexcept;

	const std::string &mappedOriginalPath() const noexcept;

	std::size_t estimatedLineCount() const noexcept;

	bool exactLineCountKnown() const noexcept;

	std::size_t selectionLength() const noexcept;

	std::uint64_t pendingLineIndexWarmupTaskId() const noexcept;
	std::size_t pendingLineIndexWarmupTaskCount() const noexcept;
	bool ownsLineIndexWarmupTask(std::uint64_t taskId) const noexcept;

	std::size_t pendingDisplayWidthWarmupTaskCount() const noexcept;
	bool ownsDisplayWidthWarmupTask(std::uint64_t taskId) const noexcept;

	std::uint64_t pendingSyntaxWarmupTaskId() const noexcept;
	std::size_t pendingSyntaxWarmupTaskCount() const noexcept;
	bool ownsSyntaxWarmupTask(std::uint64_t taskId) const noexcept;

	std::uint64_t pendingFoldWarmupTaskId() const noexcept;
	std::size_t pendingFoldWarmupTaskCount() const noexcept;
	bool ownsFoldWarmupTask(std::uint64_t taskId) const noexcept;

	std::uint64_t pendingMiniMapWarmupTaskId() const noexcept;
	std::size_t pendingMiniMapWarmupTaskCount() const noexcept;
	bool ownsMiniMapWarmupTask(std::uint64_t taskId) const noexcept;
	bool miniMapProjectionAvailable() const noexcept;

	std::size_t syntaxWarmupTopLine() const noexcept;

	std::size_t syntaxWarmupBottomLine() const noexcept;

	std::size_t syntaxPrefetchTargetBottomLine() const noexcept;

	std::size_t syntaxPrefetchReachedBottomLine() const noexcept;

	bool shouldReportMiniMapInitialRender() const noexcept;

	void markMiniMapInitialRenderReported() noexcept;

	const std::string &lastUiHotpathTrace() const noexcept;

	bool lineIndexWarmupPending() const noexcept;

	bool syntaxWarmupPending() const noexcept;

	bool miniMapWarmupPending() const noexcept;

	bool usesApproximateMetrics() const noexcept;

	void setInsertModeEnabled(bool on);
	void setLineDrawingEnabled(bool on);
	void setLineDrawingDoubleLines(bool on);
	void toggleLineDrawing();
	void toggleLineDrawingDoubleLines();
	bool drawLineDrawingBoxForColumnBlock(std::size_t line1, std::size_t line2, int col1, int col2);

	int preferredIndentColumn() const noexcept;

	void setPreferredIndentColumn(int column) noexcept;

	bool freeCursorMovementEnabled() const noexcept;
	bool freeCursorVirtualMovementAllowed() const noexcept;

	int actualCursorVisualColumn(std::size_t offset) const noexcept;

	int displayedCursorColumn() const noexcept;

	std::size_t cachedCursorLineIndex() const noexcept;

	void syncDisplayedCursorColumnFromCursor(bool preserveFreeColumn) noexcept;

	void refreshConfiguredVisualSettings();
	void refreshEditorSettingsSnapshot();
	void requestDocumentLineNavigation(std::size_t lineIndex);
	void moveCursorToDocumentLineTop(std::size_t lineIndex, int visualColumn);
	void restoreCursorViewState(std::size_t lineIndex, int visualColumn);
	void setCommunicationViewerMode(bool enabled, bool lineNumbers, MRLiveLogScrollDirection scrollDirection = MRLiveLogScrollDirection::Down);
	void setCommunicationViewerOptions(bool lineNumbers);
	void setCommunicationViewerOptions(bool lineNumbers, MRLiveLogScrollDirection scrollDirection);
	void setMiniMapSuppressed(bool suppressed) noexcept;
	void setWordWrapSuppressed(bool suppressed) noexcept;
	void setScrollBarsAlwaysVisible(bool visible) noexcept;
	void adoptFileCompareLineKinds(const std::shared_ptr<const std::vector<unsigned char>> &lineKinds,
	                               const std::shared_ptr<const std::vector<MRFileCompareMiniMapSlice>> &miniMapSlices);
	void clearFileCompareLineKinds();
	void setFileCompareGutters(const std::string &leftGutters, const std::string &rightGutters);
	void setFileCompareGutterVisible(bool visible) noexcept;
	void updateMetrics();

	std::size_t cursorOffset() const noexcept;

	std::size_t bufferLength() const noexcept;

	std::size_t selectionStartOffset() const noexcept;

	std::size_t selectionEndOffset() const noexcept;
	std::size_t selectionAnchorOffset() const noexcept;
	std::size_t selectionCursorOffset() const noexcept;

	bool hasTextSelection() const noexcept;

	std::size_t lineStartOffset(std::size_t pos) const noexcept;

	std::size_t lineEndOffset(std::size_t pos) const noexcept;

	std::size_t nextLineOffset(std::size_t pos) const noexcept;

	std::size_t prevLineOffset(std::size_t pos) const noexcept;

	std::size_t lineIndexOfOffset(std::size_t pos) const noexcept;

	std::size_t columnOfOffset(std::size_t pos) const noexcept;

	char charAtOffset(std::size_t pos) const noexcept;

	std::string lineTextAtOffset(std::size_t pos) const;

		std::size_t nextCharOffset(std::size_t pos) noexcept;

		std::size_t prevCharOffset(std::size_t pos) noexcept;

		std::size_t lineMoveOffset(std::size_t pos, int deltaLines, int targetVisualColumn = -1) noexcept;

		std::size_t tabStopMoveOffset(std::size_t pos, bool forward) noexcept;

		std::size_t prevWordOffset(std::size_t pos) noexcept;

		std::size_t nextWordOffset(std::size_t pos) noexcept;

		std::size_t charPtrOffset(std::size_t start, int pos) noexcept;

	int charColumn(std::size_t start, std::size_t pos) const noexcept;

	void setCursorOffset(std::size_t pos, int = 0);
	void setCursorOffsetAtVisualColumn(std::size_t pos, int visualColumn);

	bool scrollWindowByLines(int deltaRows);
	bool scrollWindowByWheel(int wheel);

	std::size_t offsetForGlobalPoint(TPoint where) noexcept;
	bool textPointInView(TPoint where) noexcept;

	struct BlockOverlayState {
		bool active = false;
		int mode = 0;
		std::size_t anchor = 0;
		std::size_t end = 0;
		bool trackCursor = false;
		int columnAnchor = -1;
		int columnEnd = -1;
		bool lineRangeValid = false;
		std::size_t line1 = 0;
		std::size_t line2 = 0;
	};

	void setBlockOverlayState(int mode, std::size_t anchor, std::size_t end, bool active, bool trackCursor = false, int columnAnchor = -1, int columnEnd = -1, bool lineRangeValid = false, std::size_t line1 = 0, std::size_t line2 = 0, bool redraw = true);
	BlockOverlayState blockOverlayState() const noexcept;

		void setSelectionOffsets(std::size_t start, std::size_t end, Boolean = False);

	bool lastMouseSelectionColumns(int &anchorColumn, int &cursorColumn) const noexcept;
	bool lastMouseSelectionLines(std::size_t &anchorLine, std::size_t &cursorLine) const noexcept;
	unsigned short lastMouseSelectionModifiers() const noexcept;

	void setFindMarkerRanges(const std::vector<std::pair<std::size_t, std::size_t>> &ranges);

	void clearFindMarkerRanges();

	void setCompilerDiagnosticRanges(const std::vector<std::pair<std::size_t, std::size_t>> &errorRanges, const std::vector<std::pair<std::size_t, std::size_t>> &warningRanges);
	void adoptCompilerDiagnosticRanges(const std::shared_ptr<const std::vector<MRTextBufferModel::Range>> &errorRanges,
	                                   const std::shared_ptr<const std::vector<MRTextBufferModel::Range>> &warningRanges);

	void clearCompilerDiagnosticRanges();
	void setDebuggerBreakpointRanges(const std::vector<std::pair<std::size_t, std::size_t>> &activeRanges, const std::vector<std::pair<std::size_t, std::size_t>> &inactiveRanges);
	void clearDebuggerBreakpointRanges();
	void setDebuggerWatchpointRanges(const std::vector<std::pair<std::size_t, std::size_t>> &activeRanges, const std::vector<std::pair<std::size_t, std::size_t>> &inactiveRanges, const std::vector<std::pair<std::size_t, std::size_t>> &errorRanges);
	void clearDebuggerWatchpointRanges();
	void setDebuggerVariableChangedRanges(const std::vector<std::pair<std::size_t, std::size_t>> &ranges);
	void clearDebuggerVariableChangedRanges();
	void setDebuggerInstructionLine(std::size_t lineIndex);
	void clearDebuggerInstructionLine();
	void revealCursor(Boolean centerCursor = True);

	void centerDocumentLocationInView(std::size_t lineIndex, int visualColumn);

	void refreshViewState();

	void update(uchar);

	int currentLineNumber() const noexcept;

	int currentColumnNumber() const noexcept;

	int currentViewRow() const noexcept;
	int currentViewColumn() const noexcept;

	int visibleViewportRows() const noexcept;

	TRect visibleTextViewportBounds() const noexcept;

	std::size_t documentLineForVisibleLine(std::size_t visibleLine) const noexcept;

	std::size_t visibleLineForDocumentLine(std::size_t documentLine) const noexcept;

	const MRTextBufferModel &bufferModel() const noexcept;

	MRTextBufferModel &bufferModel() noexcept;

	bool revertUndoSuffix(std::size_t baseDepth);

	void shareContentStateFrom(const MRFileEditor &source);

	void detachContentStateCopy();

	void syncFromEditorState(bool = true);

	void syncIndicatorVisualSettings();

	void notifyWindowTaskStateChanged();
	void continueComputeWarmupIfNeeded(const char *reason = nullptr);

	std::string snapshotText() const;

	MRTextBufferModel::ReadSnapshot readSnapshot() const;

	MRTextBufferModel::Document documentCopy() const;

	std::size_t documentId() const noexcept;

	std::size_t documentVersion() const noexcept;

	const MRTextBufferModel::DocumentChangeSet &lastDocumentChangeSet() const noexcept;

	LoadTiming lastLoadTiming() const noexcept;

	bool applyLineIndexWarmup(const mr::editor::LineIndexScanPacket &packet, std::size_t expectedVersion, std::uint64_t expectedTaskId);

	bool applyDisplayWidthWarmup(const mr::coprocessor::DisplayWidthWarmupPayload &warmup, std::size_t sourceVersion, std::uint64_t expectedTaskId);

	bool applySyntaxWarmup(const mr::coprocessor::SyntaxWarmupPayload &warmup, const mr::coprocessor::Result &result);

	bool applyMiniMapWarmup(const mr::coprocessor::Payload &payload, const mr::coprocessor::Result &result);

	bool applyFoldWarmup(const mr::coprocessor::Payload &payload, const mr::coprocessor::Result &result);

	void clearLineIndexWarmupTask(std::uint64_t expectedTaskId) noexcept;
	std::size_t cancelLineIndexWarmup() noexcept;

	void clearDisplayWidthWarmupTask(std::uint64_t expectedTaskId) noexcept;

	std::size_t cancelDisplayWidthWarmup() noexcept;

	void clearSyntaxWarmupTask(std::uint64_t expectedTaskId) noexcept;
	std::size_t cancelSyntaxWarmup() noexcept;

	void clearMiniMapWarmupTask(std::uint64_t expectedTaskId) noexcept;

	void applyMiniMapSignals(const MRMiniMapRenderer::Signals &signals);

	void clearFoldWarmupTask(std::uint64_t expectedTaskId = 0);
	std::size_t cancelFoldWarmup() noexcept;

	void setSyntaxTitleHint(const std::string &title);

	const char *syntaxLanguageName() const noexcept;

	MRSyntaxLanguage syntaxLanguage() const noexcept;

	bool syntaxLanguageAutomatic() const noexcept;

	bool buildFoldOutlineSnapshot(const MROutlineRequest &request, MROutlineSnapshot &snapshot) const;
	bool captureFoldOutlineInput(const MROutlineRequest &request, MRFoldOutlineInputSnapshot &input) const;
	std::uint64_t foldOutlineInputRevision() const noexcept;
	bool completeFoldOutlineInputAvailable() const noexcept;
	bool canRequestCompleteFoldOutlineWarmup() const;
	bool requestCompleteFoldOutlineWarmup();

		bool canSaveInPlace() const;

		bool canSaveAs() const;

		bool loadMappedFile(TStringView path, std::string &error);

		Boolean saveInPlace() noexcept;

		Boolean saveAsWithPrompt() noexcept;

		Boolean saveAsWithoutOverwritePrompt() noexcept;

		void pushUndoSnapshot();

		bool replaceBufferData(const char *data, uint length);

		bool replaceBufferText(const char *text);

		bool adoptReadOnlyProjectionText(const std::shared_ptr<const std::string> &text, std::size_t expectedDocumentId, std::size_t expectedVersion);

		bool appendBufferData(const char *data, uint length);

		bool appendBufferText(const char *text);
		bool appendLogViewerData(const char *data, uint length, const std::vector<std::pair<std::size_t, std::size_t>> *chunkFindRanges = nullptr);
		bool prependLogViewerData(const char *data, uint length, const std::vector<std::pair<std::size_t, std::size_t>> *chunkFindRanges = nullptr);

		bool replaceRangeAndSelect(std::size_t start, std::size_t end, const char *data, std::size_t length);
		bool replaceRangesAndCollapse(const std::vector<MRTextBufferModel::Range> &ranges, const char *data, std::size_t length);

		int paddingColumnsBeforeInsertAtCursor() const noexcept;

		bool insertBufferText(const std::string &text);

		bool replaceCurrentLineText(const std::string &text);

		bool centerCurrentLine(int leftMargin, int rightMargin);

		bool copyCharFromLineAbove();

		bool formatParagraph(int rightMargin);

		std::string buildFormattedParagraphText(std::string_view paragraphText, int leftMargin, int rightMargin) const;

		bool formatParagraph(int leftMargin, int rightMargin);

		bool formatDocument(int leftMargin, int rightMargin);

		bool justifyParagraph(int leftMargin, int rightMargin);

		bool prettifyBlockOrFile();

		bool deleteCharsAtCursor(int count);

		bool deleteCurrentLineText();

		bool replaceWholeBuffer(const std::string &text, std::size_t cursorPos);

		MRTextBufferModel::CommitResult applyStagedTransaction(const MRTextBufferModel::StagedTransaction &transaction, std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState = true);

		bool newLineWithIndent(const std::string &fill);

		bool newLineWithPreferredIndent();

		int leadingIndentColumnForLine(std::size_t lineStart) const noexcept;

		int inferredShellIndentStepColumns(std::size_t lineStart, const MREditSetupSettings &settings) const noexcept;

		std::string automaticIndentFillForCursor() const;

		int smartIndentTargetColumnForContext(std::size_t lineStart, std::size_t cursorInLine) const;
		int smartIndentTargetColumnForContext(std::size_t lineStart, std::size_t cursorInLine, int baseColumn) const;
		int smartIndentTargetColumnForContext(std::size_t lineStart, std::size_t cursorInLine, int baseColumn, MRSyntaxLanguage language) const;
		int smartIndentTargetColumnForContext(std::size_t lineStart, std::size_t cursorInLine, int baseColumn, MRSyntaxLanguage language, bool useEditorIndentGuards) const;

		std::string smartIndentFillForCursor();

		bool applyCurrentLineLeadingIndent(int targetColumn);

		std::string activeLatexRawTextEnvironmentBeforeLine(std::size_t lineStart) const;

		int smartDedentTargetColumnForLine(std::size_t lineStart, int baseColumn) const;
		int smartDedentTargetColumnForLine(std::size_t lineStart, int baseColumn, MRSyntaxLanguage language) const;
		int smartDedentTargetColumnForLine(std::size_t lineStart, int baseColumn, MRSyntaxLanguage language, bool useEditorIndentGuards) const;
		int smartDedentTargetColumnForLine(std::size_t lineStart, int baseColumn, MRSyntaxLanguage language, bool useEditorIndentGuards, const std::vector<std::size_t> &formattedLineStarts, const std::vector<int> &formattedColumns) const;

		void applyLiveSmartDedentAfterTextInput(const std::string &insertedText);

		void effectiveFormatMargins(const MREditSetupSettings &settings, int &leftMargin, int &rightMargin) const noexcept;

		bool wrapCurrentLineOnce(int leftMargin, int rightMargin);

		void applyLiveWordWrapAfterTextInput();

		virtual void draw() override;

	virtual TPalette &getPalette() const override;

	TColorAttr editorTextFillColor() noexcept;

		virtual void handleEvent(TEvent &event) override;

		virtual void scrollDraw() override;

		virtual void setState(ushort aState, Boolean enable) override;

	virtual Boolean valid(ushort command) override;

  private:
	static bool isWordByte(char ch) noexcept;

	static bool hasShiftModifier(ushort mods) noexcept;

	bool drawLineDrawingCursorMotion(ushort key);
	bool handleLineDrawingMouse(TEvent &event, TPoint local);
	void restoreLineDrawingCursor(std::size_t visualLine, int visualColumn);
	bool materializeLineDrawingRows(std::size_t line1, std::size_t line2, int rightVisualColumn);
	bool lineDrawingCellMaskAt(std::size_t lineIndex, int visualColumn, unsigned char &mask, unsigned char &doubleMask);
	unsigned char supportedLineDrawingMask(std::size_t lineIndex, int visualColumn, unsigned char existingMask, unsigned char connectionMask);
	bool drawLineDrawingSegment(std::size_t fromLine, int fromColumn, std::size_t toLine, int toColumn);
	bool drawLineDrawingMouseSegment(std::size_t fromLine, int fromColumn, std::size_t toLine, int toColumn, int &lastAxis);

	int configuredTabSize() const;

	bool configuredDisplayTabs() const;

	bool configuredFormatRuler() const;

	static int tabDisplayWidth(const MREditSetupSettings &settings, int visualColumn) noexcept;

	std::string preferredIndentFill() const;

	int visibleTextRows() const noexcept;

	void syncScrollBarsToState() noexcept;

	static int decimalDigits(std::size_t value) noexcept;

	bool shouldTraceLargeFileWarmupDiagnostics() const noexcept;

	struct SaveNormalizationCache {
		bool valid = false;
		std::size_t documentId = 0;
		std::size_t version = 0;
		std::size_t optionsHash = 0;
		std::size_t sourceBytes = 0;
	};

	struct LineIndexPacketState {
		std::uint64_t taskId;
		std::uint64_t reservationId;
		std::uint64_t generation;
		mr::coprocessor::WorkDirection direction;
		std::size_t startOffset;
		std::size_t endOffset;

		LineIndexPacketState() noexcept
		    : taskId(0), reservationId(0), generation(0), direction(mr::coprocessor::WorkDirection::None), startOffset(0), endOffset(0) {
		}
	};

	struct LineIndexWarmupState {
		std::size_t documentId;
		std::size_t version;
		std::size_t focusBucket;
		std::size_t focusOffset;
		std::uint64_t generation;
		std::vector<LineIndexPacketState> packets;

		LineIndexWarmupState() noexcept : documentId(0), version(0), focusBucket(static_cast<std::size_t>(-1)), focusOffset(0), generation(0), packets() {
		}
	};

	struct PendingDocumentLineNavigationState {
		bool active;
		std::size_t documentId;
		std::size_t version;
		std::size_t targetLine;
		std::size_t cursorOffset;
		std::size_t selectionStart;
		std::size_t selectionEnd;

		PendingDocumentLineNavigationState() noexcept
		    : active(false), documentId(0), version(0), targetLine(0), cursorOffset(0), selectionStart(0), selectionEnd(0) {
		}
	};

	struct DisplayWidthPacketState {
		std::uint64_t taskId;
		std::size_t documentVersion;
		std::uint64_t generation;
		std::size_t startLine;
		std::size_t endLine;
		bool adopted;

		DisplayWidthPacketState() noexcept : taskId(0), documentVersion(0), generation(0), startLine(0), endLine(0), adopted(false) {
		}
	};

	struct DisplayWidthWarmupState {
		std::size_t documentId;
		std::size_t version;
		std::size_t totalLines;
		std::size_t appendStartLine;
		std::uint64_t generation;
		int maximumWidth;
		int tabSize;
		int leftMargin;
		int rightMargin;
		std::string formatLine;
		bool complete;
		bool appendTailPending;
		std::vector<DisplayWidthPacketState> packets;

		DisplayWidthWarmupState() noexcept : documentId(0), version(0), totalLines(0), appendStartLine(0), generation(0), maximumWidth(1), tabSize(0), leftMargin(0), rightMargin(0), formatLine(), complete(false), appendTailPending(false), packets() {
		}
	};

	struct SyntaxPacketState {
		std::uint64_t taskId;
		std::uint64_t generation;
		mr::coprocessor::WorkDirection direction;
		std::size_t startLine;
		std::size_t endLine;
		std::size_t materializedStartLine;
		std::size_t materializedEndLine;
		bool inputStateConfirmed;
		bool resultReady;
		MRSyntaxLineState inputState;
		mr::coprocessor::DeferredResultLifecycle resultLifecycle;
		std::vector<mr::coprocessor::SyntaxWarmCheckpoint> checkpoints;
		std::vector<mr::coprocessor::SyntaxWarmLine> lines;

		SyntaxPacketState() noexcept
		    : taskId(0), generation(0), direction(mr::coprocessor::WorkDirection::None), startLine(0), endLine(0), materializedStartLine(0), materializedEndLine(0),
		      inputStateConfirmed(false), resultReady(false), inputState(), resultLifecycle(), checkpoints(), lines() {
		}
	};

	struct SyntaxWarmupState {
		std::size_t documentId;
		std::size_t version;
		MRSyntaxLanguage language;
		std::uint64_t generation;
		std::size_t visibleTopLine;
		std::size_t visibleBottomLine;
		std::size_t targetBottomLine;
		std::size_t reachedBottomLine;
		bool waitingForReservedPackets;
		std::vector<SyntaxPacketState> packets;

		SyntaxWarmupState() noexcept
		    : documentId(0), version(0), language(MRSyntaxLanguage::PlainText), generation(0), visibleTopLine(0), visibleBottomLine(0), targetBottomLine(0), reachedBottomLine(0),
		      waitingForReservedPackets(false), packets() {
		}
	};

	struct FoldPacketState {
		std::uint64_t taskId;
		std::uint64_t generation;
		mr::coprocessor::WorkDirection direction;
		std::size_t startLine;
		std::size_t endLine;
		bool contextOnly;
		bool inputStateConfirmed;
		bool resultReady;
		MRFoldAnalysisState inputState;
		MRFoldAnalysisState outputState;
		mr::coprocessor::DeferredResultLifecycle resultLifecycle;
		std::vector<std::string> lineTexts;
		std::vector<MRFoldSpan> spans;
		std::vector<MRFoldGutterBranch> branches;

		FoldPacketState() noexcept
		    : taskId(0), generation(0), direction(mr::coprocessor::WorkDirection::None), startLine(0), endLine(0), contextOnly(false), inputStateConfirmed(false), resultReady(false), inputState(),
		      outputState(), resultLifecycle(), lineTexts(), spans(), branches() {
		}
	};

	struct FoldCheckpointState {
		std::uint64_t generation;
		std::size_t line;
		MRFoldAnalysisState state;

		FoldCheckpointState() noexcept : generation(0), line(0), state() {
		}
	};

	struct FoldValidatedSegment {
		std::uint64_t generation;
		std::size_t startLine;
		std::size_t endLine;
		std::vector<std::string> lineTexts;
		std::vector<MRFoldSpan> spans;
		std::vector<MRFoldGutterBranch> branches;

		FoldValidatedSegment() noexcept : generation(0), startLine(0), endLine(0), lineTexts(), spans(), branches() {
		}
	};

	struct FoldWarmupState {
		std::size_t documentId;
		std::size_t version;
		MRSyntaxLanguage language;
		std::uint64_t generation;
		std::size_t scanTopLine;
		std::size_t scanBottomLine;
		std::size_t visibleTopLine;
		std::size_t visibleBottomLine;
		std::vector<FoldPacketState> packets;
		std::vector<FoldCheckpointState> checkpoints;
		std::vector<FoldValidatedSegment> segments;
		bool failureLatched;

		FoldWarmupState() noexcept
		    : documentId(0), version(0), language(MRSyntaxLanguage::PlainText), generation(0), scanTopLine(0), scanBottomLine(0), visibleTopLine(0), visibleBottomLine(0), packets(),
		      checkpoints(), segments(), failureLatched(false) {
		}
	};

	enum class FoldCanonicalPacketStage : unsigned char {
		AwaitingAcquisition,
		Acquiring,
		Acquired,
		Validating
	};

	struct FoldCanonicalPacketState {
		std::uint64_t taskId;
		std::uint64_t generation;
		std::size_t startLine;
		std::size_t endLine;
		FoldCanonicalPacketStage stage;
		std::shared_ptr<const std::vector<std::string>> lineTexts;

		FoldCanonicalPacketState() noexcept
		    : taskId(0), generation(0), startLine(0), endLine(0), stage(FoldCanonicalPacketStage::AwaitingAcquisition), lineTexts() {
		}
	};

	struct FoldCanonicalContextState {
		std::size_t documentId;
		std::size_t version;
		MRSyntaxLanguage language;
		std::uint64_t generation;
		std::size_t totalLines;
		std::size_t requestedScanTopLine;
		std::size_t requestedScanBottomLine;
		std::size_t requestedVisibleTopLine;
		std::size_t requestedVisibleBottomLine;
		std::size_t targetLine;
		std::size_t nextPacketStartLine;
		bool waitingForLineIndex;
		bool requestValid;
		bool failureLatched;
		std::vector<FoldCanonicalPacketState> packets;
		std::vector<FoldCheckpointState> checkpoints;

		FoldCanonicalContextState() noexcept
		    : documentId(0), version(0), language(MRSyntaxLanguage::PlainText), generation(0), totalLines(0), requestedScanTopLine(0), requestedScanBottomLine(0),
		      requestedVisibleTopLine(0), requestedVisibleBottomLine(0), targetLine(0), nextPacketStartLine(0), waitingForLineIndex(false), requestValid(false), failureLatched(false), packets(), checkpoints() {
		}
	};

	struct FoldLevelProjectionPayload final : mr::coprocessor::Payload {
		std::uint64_t generation;
		unsigned short level;
		std::size_t segmentCount;
		bool complete;
		std::shared_ptr<const MRFoldClosedProjection> projection;

		FoldLevelProjectionPayload() noexcept : generation(0), level(0), segmentCount(0), complete(false), projection() {
		}

		FoldLevelProjectionPayload(std::uint64_t aGeneration, unsigned short aLevel, std::size_t aSegmentCount, bool isComplete,
		                           std::shared_ptr<const MRFoldClosedProjection> aProjection) noexcept
		    : generation(aGeneration), level(aLevel), segmentCount(aSegmentCount), complete(isComplete), projection(std::move(aProjection)) {
		}
	};

	struct FoldLineAcquisitionPayload final : mr::coprocessor::Payload {
		std::uint64_t generation;
		MRSyntaxLanguage language;
		std::size_t startLine;
		std::size_t endLine;
		std::shared_ptr<const std::vector<std::string>> lineTexts;

		FoldLineAcquisitionPayload() noexcept : generation(0), language(MRSyntaxLanguage::PlainText), startLine(0), endLine(0), lineTexts() {
		}

		FoldLineAcquisitionPayload(std::uint64_t aGeneration, MRSyntaxLanguage aLanguage, std::size_t aStartLine, std::size_t anEndLine,
		                           std::shared_ptr<const std::vector<std::string>> acquiredLineTexts) noexcept
		    : generation(aGeneration), language(aLanguage), startLine(aStartLine), endLine(anEndLine), lineTexts(std::move(acquiredLineTexts)) {
		}
	};

	enum class FoldLevelPacketStage : unsigned char {
		AwaitingAcquisition,
		Acquiring,
		Acquired,
		Validating
	};

	struct FoldLevelPacketState {
		std::uint64_t taskId;
		std::uint64_t generation;
		std::size_t startLine;
		std::size_t endLine;
		FoldLevelPacketStage stage;
		bool projectionOnly;
		MRFoldAnalysisState confirmedInputState;
		std::shared_ptr<const std::vector<std::string>> lineTexts;

		FoldLevelPacketState() noexcept
		    : taskId(0), generation(0), startLine(0), endLine(0), stage(FoldLevelPacketStage::AwaitingAcquisition), projectionOnly(false), confirmedInputState(), lineTexts() {
		}
	};

	struct FoldLevelOperationState {
		std::size_t documentId;
		std::size_t version;
		MRSyntaxLanguage language;
		std::uint64_t generation;
		unsigned short level;
		unsigned short resolvedLevel;
		std::size_t totalLines;
		std::size_t contextAnchorLine;
		std::size_t nextPacketStartLine;
		std::size_t nextProjectionPacketStartLine;
		std::size_t confirmedLine;
		std::size_t projectedLine;
		std::size_t projectionTargetBottomLine;
		std::uint64_t projectionTaskId;
		bool waitingForLineIndex;
		bool levelResolved;
		MRFoldAnalysisState viewportAnchorState;
		MRFoldAnalysisState confirmedState;
		std::vector<FoldLevelPacketState> packets;
		std::vector<FoldCheckpointState> checkpoints;
		std::vector<std::shared_ptr<const FoldValidatedSegment>> segments;

		FoldLevelOperationState() noexcept
		    : documentId(0), version(0), language(MRSyntaxLanguage::PlainText), generation(0), level(0), resolvedLevel(0), totalLines(0), contextAnchorLine(0), nextPacketStartLine(0),
		      nextProjectionPacketStartLine(0), confirmedLine(0), projectedLine(0), projectionTargetBottomLine(0), projectionTaskId(0), waitingForLineIndex(false), levelResolved(false),
		      viewportAnchorState(), confirmedState(), packets(), checkpoints(), segments() {
		}
	};

	using TextViewportGeometry = MRTextViewportLayout::Geometry;

	TextViewportGeometry textViewportGeometryFor(const MREditSetupSettings &settings) const noexcept;

	TextViewportGeometry textViewportGeometry() const noexcept;

	bool shouldShowEditorCursor(long long x, long long y, const TextViewportGeometry &viewport) const noexcept;

	bool shouldShowEditorCursor(long long x, long long y) const noexcept;

	int textColumnFromLocalX(int localX) const noexcept;

	int textViewportWidth() const;

	std::string normalizedFormatRulerLine(const MREditSetupSettings &settings, int *leftMarginOut = nullptr, int *rightMarginOut = nullptr) const;

	bool persistVisibleEditSetupSettings(const MREditSetupSettings &settings, const std::string &errorPrefix);

	bool previewVisibleEditSetupSettings(const MREditSetupSettings &settings, const std::string &errorPrefix);

	bool finalizeVisibleEditSetupPreview(const MREditSetupSettings &previousSettings, const std::string &errorPrefix);

	void drawFormatRulerOverlay(const TextViewportGeometry &viewport, const MREditSetupSettings &settings);

	bool editFormatRulerAtLocalPoint(TPoint local, ushort modifiers);

	bool dragFormatRulerAtLocalPoint(TEvent &event, TPoint local);

		void drawLineNumberGutter(TDrawBuffer &b, std::size_t lineNumber, bool showNumber, int drawX, int width, bool zeroFill, std::size_t lineIndex);
		void drawFileCompareGutter(TDrawBuffer &b, int drawX, int width, std::size_t lineIndex);
		void drawDebugGutter(TDrawBuffer &b, int drawX, int width, std::size_t lineIndex);

		void drawCodeFoldingGutter(TDrawBuffer &b, int drawX, int width, std::size_t lineStart, std::size_t lineIndex);

	static const char *lineIndexWarmupTaskLabel() noexcept;

	static const char *displayWidthWarmupTaskLabel() noexcept;

	static const char *syntaxWarmupTaskLabel() noexcept;

	static const char *foldWarmupTaskLabel() noexcept;

	bool lineIntersectsDirtyRanges(std::size_t lineStart, std::size_t lineEnd) const noexcept;

	bool findMarkerContainsOffset(std::size_t offset) const noexcept;
	bool debuggerBreakpointContainsOffset(std::size_t offset) const noexcept;
	bool debuggerBreakpointInactiveContainsOffset(std::size_t offset) const noexcept;
	bool debuggerWatchpointActiveContainsOffset(std::size_t offset) const noexcept;
	bool debuggerWatchpointInactiveContainsOffset(std::size_t offset) const noexcept;
	bool debuggerWatchpointErrorContainsOffset(std::size_t offset) const noexcept;
	bool debuggerVariableChangedContainsOffset(std::size_t offset) const noexcept;
	bool debuggerBreakpointLineAt(std::size_t lineIndex) const noexcept;
	bool debuggerBreakpointInactiveLineAt(std::size_t lineIndex) const noexcept;
	unsigned char fileCompareLineKindAt(std::size_t lineIndex) const noexcept;

	MRMiniMapRenderer::Palette resolveMiniMapPalette();

	static bool ratioCellActive(int numerator, int denominator, int cellIndex, int cellCount) noexcept;

	// Returns true if minimap cell [cellIndex] overlaps the content column range [from, to)
	// when the viewport has viewportWidth columns and the minimap has cellCount cells.
	static bool nextDisplayChar(TStringView text, std::size_t &index, std::size_t &width, int visualColumn, const MREditSetupSettings &settings) noexcept;

	static int displayWidthForText(TStringView text, const MREditSetupSettings &settings) noexcept;

	static void writeChunk(std::ofstream &out, const char *data, std::size_t length);

	bool writeDocumentToPath(const char *targetPath);

	static bool pathIsRegularFile(const char *path) noexcept;

	static bool samePath(const char *lhs, const char *rhs) noexcept;

	bool confirmOverwriteForSaveAs(const char *targetPath) const;

	std::size_t lineStartForIndex(std::size_t index) const noexcept;

	bool useApproximateLargeFileMetrics() const noexcept;

	int dynamicLargeFileLineLimit() const noexcept;

	int dynamicLargeFileWidthLimit() const;

	int provisionalDisplayWidthLimit() const noexcept;

	bool displayWidthLimitExact() const noexcept;

	void scheduleLineIndexWarmupIfNeeded();
	bool continuePendingDocumentLineNavigation();

	void scheduleDisplayWidthWarmupIfNeeded();

	bool prepareDisplayWidthWarmupForAppend(const MRTextBufferModel::DocumentChangeSet &changeSet);

	void resetDisplayWidthWarmup() noexcept;

	void scheduleSyntaxWarmupIfNeeded();

	void scheduleFoldWarmupIfNeeded(std::size_t scanTopLine, std::size_t scanBottomLine, std::size_t topLine, std::size_t requestBottomLine, MRSyntaxLanguage language);
	bool canonicalFoldContextForViewport(std::size_t scanTopLine, std::size_t scanBottomLine, std::size_t topLine, std::size_t requestBottomLine, MRSyntaxLanguage language,
	                                     std::size_t &anchorLine, MRFoldAnalysisState &anchorState);
	void continueCanonicalFoldContextIfNeeded();
	bool applyCanonicalFoldContext(const mr::coprocessor::Payload &payload, const mr::coprocessor::Result &result);
	std::size_t cancelCanonicalFoldContext() noexcept;
	void clearCanonicalFoldContextTask(std::uint64_t expectedTaskId) noexcept;
	bool ownsCanonicalFoldContextTask(std::uint64_t taskId) const noexcept;
	void appendCanonicalFoldContextPackets();
	void submitCanonicalFoldContextPackets();
	void submitCanonicalFoldAcquisition(FoldCanonicalPacketState &packet, const MRTextBufferModel::ReadSnapshot &snapshot);
	void submitCanonicalFoldValidation(FoldCanonicalPacketState &packet);
	void rememberCanonicalFoldCheckpoint(std::size_t line, const MRFoldAnalysisState &state);
	void resumeCanonicalFoldViewportIfReady();
	static std::shared_ptr<const mr::coprocessor::Payload> buildFoldWarmupPayload(const MRTextBufferModel::ReadSnapshot &snapshot, MRSyntaxLanguage language, std::uint64_t generation,
	                                                                            mr::coprocessor::WorkDirection direction, std::size_t startLine, std::size_t endLine,
	                                                                            std::size_t totalLines, bool documentEndKnown, const MRFoldAnalysisState &inputState,
	                                                                            const std::map<std::size_t, MRFoldSpan> &closedFoldSpans,
	                                                                            const std::shared_ptr<std::atomic_bool> &cancelFlag, bool retainProjectionData = true,
	                                                                            int retainedFoldLevel = -1);
	static std::shared_ptr<const mr::coprocessor::Payload> buildFoldLineAcquisitionPayload(const MRTextBufferModel::ReadSnapshot &snapshot, MRSyntaxLanguage language,
	                                                                                     std::uint64_t generation, std::size_t startLine, std::size_t endLine,
	                                                                                     std::size_t totalLines, const std::shared_ptr<std::atomic_bool> &cancelFlag);
	static std::shared_ptr<const mr::coprocessor::Payload> buildFoldValidationPayload(const std::shared_ptr<const std::vector<std::string>> &lineTexts, MRSyntaxLanguage language,
	                                                                                std::uint64_t generation, std::size_t startLine, std::size_t endLine,
	                                                                                std::size_t totalLines, const MRFoldAnalysisState &inputState, bool retainFoldSpans,
	                                                                                unsigned short retainedFoldLevel, const std::shared_ptr<std::atomic_bool> &cancelFlag);

	bool toggleDocumentFoldLevel(unsigned short level);
	void continueDocumentFoldLevelOperationIfNeeded();
	std::size_t cancelViewportFoldWarmup() noexcept;
	void supersedeViewportFoldWarmup() noexcept;
	std::size_t cancelDocumentFoldLevelOperation() noexcept;
	void clearDocumentFoldLevelTask(std::uint64_t expectedTaskId) noexcept;
	void appendDocumentFoldLevelPackets();
	void submitDocumentFoldLevelPackets();
	void submitDocumentFoldLevelAcquisition(FoldLevelPacketState &packet, const MRTextBufferModel::ReadSnapshot &snapshot);
	void submitDocumentFoldLevelValidation(FoldLevelPacketState &packet);
	bool resolveDocumentFoldLevelTarget();
	void scheduleDocumentFoldLevelProjection();
	bool applyDocumentFoldLevelProjection(const FoldLevelProjectionPayload &payload, const mr::coprocessor::Result &result);

	bool resolveSaveOptionsForPath(const char *path, MRTextSaveOptions &options, std::size_t *optionsHash = nullptr) const;

	void invalidateSaveNormalizationCache() noexcept;

	void noteSaveNormalizationThroughput(std::size_t sourceBytes, double runMicros) noexcept;

	void updateIndicator();

		void ensureCursorVisible(bool centerCursor);

		void moveCursor(std::size_t target, bool extendSelection, bool centerCursor, int requestedVisualColumn = -1);

		bool isTextInputEvent(const TEvent &event) const;

		void handleTextInput(TEvent &event);

		std::string tabKeyText() const;

		void handleKeyDown(TEvent &event);

		void handleCommand(TEvent &event);

		void handleMouse(TEvent &event);

		std::size_t mouseOffset(TPoint local, int *visualColumnOut = nullptr) noexcept;

		std::size_t canonicalCursorOffset(std::size_t pos) const noexcept;

		void copySelection();

		void cutSelection();

		void requestSystemClipboardPaste();

		void replaceSelectionText(const std::string &text);

		void convertSelectionToUpperCase();

		void convertSelectionToLowerCase();

	Boolean confirmSaveOrDiscardUntitled();

	Boolean confirmSaveOrDiscardNamed();

		TColorAttr tokenColor(MRSyntaxToken token, bool selected, TAttrPair pair) noexcept;

		void refreshSyntaxContext();

		bool pieceTableOnlyPhaseActive() const noexcept;

		const MREditSetupSettings &effectiveEditSetupSettings() const noexcept;

		std::string effectiveCodeLanguageSetting() const;

		bool languageFeaturesEnabled() const;

		bool syntaxPipelineEnabled() const;

		bool foldingPipelineEnabled() const;

		bool miniMapPipelineEnabled() const noexcept;

		void resetSyntaxWarmupState(bool clearCache) noexcept;

		void invalidateSyntaxCacheFromLineStart(std::size_t lineStart) noexcept;

		void clearSyntaxWarmedLineRanges() noexcept;

		void rememberSyntaxWarmedLineRange(std::size_t startLine, std::size_t endLine) noexcept;

		void invalidateSyntaxWarmedLineRangesFrom(std::size_t lineIndex) noexcept;

		bool syntaxWarmedLineRangeCovered(std::size_t startLine, std::size_t endLine) const noexcept;

	void invalidateFoldCache(bool preserveVisibleProjection = false) noexcept;

	void ensureVisibleFoldSpans(std::size_t topLine, int rowCount, MRSyntaxLanguage language);

	int visibleFoldGutterColumns() const noexcept;

	bool toggleFoldAtLine(std::size_t lineIndex);

	bool foldingGutterHit(TPoint local, std::size_t *lineIndexOut = nullptr) const noexcept;

	std::size_t foldedVisibleLineCount() const noexcept;

		bool syntaxCheckpointForLine(std::size_t lineIndex, MRSyntaxCheckpointEntry &checkpoint) const;

		void rememberSyntaxCheckpoint(std::size_t lineStart, std::size_t lineIndex, const MRSyntaxLineState &stateIn) noexcept;

		bool syntaxConfirmedStateForLine(std::size_t lineIndex, MRSyntaxLineState &stateIn) const noexcept;

		bool adoptReadySyntaxPackets(bool &tokensChanged);

		void submitSyntaxPacket(SyntaxPacketState &packet, const MRTextBufferModel::ReadSnapshot &snapshot);

		bool foldConfirmedStateForPacket(const FoldPacketState &packet, MRFoldAnalysisState &state) const noexcept;

		bool adoptReadyFoldPackets();

		bool publishCurrentFoldProjection();

		void submitFoldPacket(FoldPacketState &packet, const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t totalLines, bool documentEndKnown);
		void submitFoldPackets(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t totalLines, bool documentEndKnown);

		void formatSyntaxLine(TDrawBuffer &b, std::size_t lineStart, std::size_t lineIndex, const MRSyntaxLineResult &syntaxLine, int hScroll, int width, int drawX, bool isDocumentLine, bool drawEofMarker, bool drawEofMarkerAsEmoji);

		void drawEofMarkerGlyph(TDrawBuffer &b, int hScroll, int width, int drawX, TAttrPair basePair, bool drawEmoji);

		bool syncAfterCommittedDocument(std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState, const MRTextBufferModel::DocumentChangeSet *changeSet = nullptr);
		bool adoptCommittedDocument(const MRTextBufferModel::Document &document, std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState, const MRTextBufferModel::DocumentChangeSet *changeSet = nullptr);

	TIndicator *mIndicator;
	bool mReadOnly;
	bool mForceBinarySave;
	bool mCustomWindowEofMarkerColorOverrideValid = false;
	TColorAttr mCustomWindowEofMarkerColorOverride = 0;
	bool mCommunicationViewerMode = false;
	bool mCommunicationViewerLineNumbers = true;
	MRLiveLogScrollDirection mCommunicationViewerScrollDirection = MRLiveLogScrollDirection::Down;
	MREditSetupSettings mEffectiveEditSettings;
	MRCursorBehaviour mCursorBehaviour = MRCursorBehaviour::BoundToText;
	MRScrollbarVisibility mScrollbarVisibility = MRScrollbarVisibility::Smart;
	bool mMiniMapSuppressed = false;
	bool mWordWrapSuppressed = false;
	bool mScrollBarsAlwaysVisible = false;
	bool mInsertMode;
	bool mLineDrawingEnabled;
	bool mLineDrawingDoubleLines;
	bool mAutoIndent;
		char fileName[MAXPATH];
		std::string mSyntaxTitleHint;
		DestructionProbe mDestructionProbe;
		MRTextBufferModel mBufferModel;
	MRTextBufferModel::DocumentChangeSet mLastDocumentChangeSet;
	std::size_t mSelectionAnchor;
	std::size_t mCursorVisualLine;
	int mCursorVisualColumn;
	bool mIndicatorUpdateInProgress;
	LineIndexWarmupState mLineIndexWarmupState;
	PendingDocumentLineNavigationState mPendingDocumentLineNavigationState;
	std::uint64_t mLineIndexGenerationCounter;
	bool mSuppressLargeFileLineIndexWarmup;
	DisplayWidthWarmupState mDisplayWidthWarmupState;
	std::uint64_t mDisplayWidthGenerationCounter;
	int mDisplayWidthPublishedLimit;
	mr::coprocessor::ExecutionOwnerKind mExecutionOwnerKind;
	std::size_t mExecutionOwnerLocalId;
	SyntaxWarmupState mSyntaxWarmupState;
	std::uint64_t mSyntaxGenerationCounter;
	MRSyntaxDerivedState mSyntaxState;
	FoldCanonicalContextState mFoldCanonicalContextState;
	FoldWarmupState mFoldWarmupState;
	FoldLevelOperationState mFoldLevelOperationState;
	std::uint64_t mFoldGenerationCounter;
	MRFoldingDerivedState mFoldState;
	mutable std::shared_ptr<const MRFoldOutlineInputSnapshot> mFoldOutlineInputCache;
	MRMiniMapDerivedState mMiniMapState;
	std::shared_ptr<const std::vector<unsigned char>> mFileCompareLineKinds;
	std::shared_ptr<const std::vector<MRFileCompareMiniMapSlice>> mFileCompareMiniMapSlices;
	std::string mFileCompareLeftGutters;
	std::string mFileCompareRightGutters;
	bool mFileCompareGuttersConfigured = false;
	bool mFileCompareGutterVisible = true;
	SaveNormalizationCache mSaveNormalizationCache;
	double mSaveNormalizationThroughputBytesPerMicro;
	std::size_t mSaveNormalizationThroughputSamples;
	bool mMouseSelectionColumnsValid;
	bool mMouseSelectionLinesValid;
	int mMouseSelectionAnchorColumn;
	int mMouseSelectionCursorColumn;
	std::size_t mMouseSelectionAnchorLine;
	std::size_t mMouseSelectionCursorLine;
	unsigned short mMouseSelectionModifiers;
	bool mBlockOverlayActive;
	int mBlockOverlayMode;
	std::size_t mBlockOverlayAnchor;
	std::size_t mBlockOverlayEnd;
	bool mBlockOverlayTrackCursor;
	int mBlockOverlayColumnAnchor;
	int mBlockOverlayColumnEnd;
	bool mBlockOverlayLineRangeValid;
	std::size_t mBlockOverlayLine1;
	std::size_t mBlockOverlayLine2;
	int mPreferredIndentColumn;
	std::vector<MRTextBufferModel::Range> mFindMarkerRanges;
	std::vector<MRTextBufferModel::Range> mDirtyRanges;
	std::shared_ptr<const std::vector<MRTextBufferModel::Range>> mCompilerErrorRanges;
	std::shared_ptr<const std::vector<MRTextBufferModel::Range>> mCompilerWarningRanges;
	std::vector<MRTextBufferModel::Range> mDebuggerBreakpointRanges;
	std::vector<MRTextBufferModel::Range> mDebuggerBreakpointInactiveRanges;
	std::vector<MRTextBufferModel::Range> mDebuggerWatchpointActiveRanges;
	std::vector<MRTextBufferModel::Range> mDebuggerWatchpointInactiveRanges;
	std::vector<MRTextBufferModel::Range> mDebuggerWatchpointErrorRanges;
	std::vector<MRTextBufferModel::Range> mDebuggerVariableChangedRanges;
	std::vector<std::size_t> mDebuggerBreakpointLines;
	std::vector<std::size_t> mDebuggerBreakpointInactiveLines;
	bool mDebuggerInstructionLineValid = false;
	std::size_t mDebuggerInstructionLine = 0;
	LoadTiming mLastLoadTiming;
	mutable std::size_t mCachedCursorLineDocumentId;
	mutable std::size_t mCachedCursorLineVersion;
	mutable std::size_t mCachedCursorLineOffset;
	mutable std::size_t mCachedCursorLineIndexValue;
	mutable bool mCachedCursorLineExact;
	std::string mLastUiHotpathTrace;

	void clearDirtyRanges();

	static void normalizePairRangeList(std::vector<std::pair<std::size_t, std::size_t>> &ranges);

	static void normalizeRangeList(std::vector<MRTextBufferModel::Range> &ranges);

	void normalizeDirtyRanges();

	void pushMappedDirtyRange(std::vector<MRTextBufferModel::Range> &mapped, std::size_t start, std::size_t end, std::size_t maxLength);

	void remapDirtyRangesForAppliedChange(const MRTextBufferModel::DocumentChangeSet &change);
	void addDirtyRange(MRTextBufferModel::Range range);

	bool isDirtyOffset(std::size_t pos) const noexcept;

};

#endif
