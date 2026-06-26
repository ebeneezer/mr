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

	MRFileEditor(const TRect &bounds, TScrollBar *aHScrollBar, TScrollBar *aVScrollBar, TIndicator *aIndicator, TStringView aFileName) noexcept;

	virtual ~MRFileEditor() override;

	bool isReadOnly() const;

	void setWindowEofMarkerColorOverride(bool enabled, TColorAttr color = 0);

	void setReadOnly(bool readOnly);

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

	std::size_t originalBufferLength() const noexcept;

	std::size_t addBufferLength() const noexcept;

	std::size_t pieceCount() const noexcept;

	bool hasMappedOriginalSource() const noexcept;

	const std::string &mappedOriginalPath() const noexcept;

	std::size_t estimatedLineCount() const noexcept;

	bool exactLineCountKnown() const noexcept;

	std::size_t selectionLength() const noexcept;

	std::uint64_t pendingLineIndexWarmupTaskId() const noexcept;

	std::uint64_t pendingSyntaxWarmupTaskId() const noexcept;

	std::uint64_t pendingFoldWarmupTaskId() const noexcept;

	std::uint64_t pendingMiniMapWarmupTaskId() const noexcept;

	std::uint64_t pendingSaveNormalizationWarmupTaskId() const noexcept;

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

	bool saveNormalizationWarmupPending() const noexcept;

	bool usesApproximateMetrics() const noexcept;

	void setInsertModeEnabled(bool on);

	int preferredIndentColumn() const noexcept;

	void setPreferredIndentColumn(int column) noexcept;

	bool freeCursorMovementEnabled() const noexcept;

	int actualCursorVisualColumn(std::size_t offset) const noexcept;

	int displayedCursorColumn() const noexcept;

	std::size_t cachedCursorLineIndex() const noexcept;

	void syncDisplayedCursorColumnFromCursor(bool preserveFreeColumn) noexcept;

	void refreshConfiguredVisualSettings();
	void moveCursorToDocumentLineTop(std::size_t lineIndex, int visualColumn);
	void setCommunicationViewerMode(bool enabled, bool lineNumbers);
	void setCommunicationViewerOptions(bool lineNumbers);
	void setMiniMapSuppressed(bool suppressed) noexcept;
	void setWordWrapSuppressed(bool suppressed) noexcept;
	void setScrollBarsAlwaysVisible(bool visible) noexcept;
	void setFileCompareLineKinds(const std::vector<unsigned char> &lineKinds);
	void setFileCompareLineKinds(const std::vector<unsigned char> &lineKinds, const std::vector<MRFileCompareMiniMapSlice> &miniMapSlices);
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
	};

	void setBlockOverlayState(int mode, std::size_t anchor, std::size_t end, bool active, bool trackCursor = false, int columnAnchor = -1, int columnEnd = -1);
	BlockOverlayState blockOverlayState() const noexcept;

		void setSelectionOffsets(std::size_t start, std::size_t end, Boolean = False);

	bool lastMouseSelectionColumns(int &anchorColumn, int &cursorColumn) const noexcept;
	unsigned short lastMouseSelectionModifiers() const noexcept;

	void setFindMarkerRanges(const std::vector<std::pair<std::size_t, std::size_t>> &ranges);

	void clearFindMarkerRanges();

	void setCompilerDiagnosticRanges(const std::vector<std::pair<std::size_t, std::size_t>> &errorRanges, const std::vector<std::pair<std::size_t, std::size_t>> &warningRanges);

	void clearCompilerDiagnosticRanges();

	void setLspDiagnosticInformationRanges(const std::vector<std::pair<std::size_t, std::size_t>> &ranges);

	void clearLspDiagnosticInformationRanges();

	void setLspDocumentHighlightRanges(const std::vector<std::pair<std::size_t, std::size_t>> &ranges);

	void clearLspDocumentHighlightRanges();

	void revealCursor(Boolean centerCursor = True);

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

	bool applyLineIndexWarmup(const mr::editor::LineIndexWarmupData &warmup, std::size_t expectedVersion);

	bool applySyntaxWarmup(const mr::coprocessor::SyntaxWarmupPayload &warmup, std::size_t expectedVersion, std::uint64_t expectedTaskId);

	bool applyMiniMapWarmup(const mr::coprocessor::MiniMapWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId);

	bool applySaveNormalizationWarmup(const mr::coprocessor::SaveNormalizationWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId, double runMicros);

	bool applyFoldWarmup(const mr::coprocessor::Payload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId);

	void clearLineIndexWarmupTask(std::uint64_t expectedTaskId) noexcept;

	void clearSyntaxWarmupTask(std::uint64_t expectedTaskId) noexcept;

	void clearMiniMapWarmupTask(std::uint64_t expectedTaskId) noexcept;

	void applyMiniMapSignals(const MRMiniMapRenderer::Signals &signals);

	void clearSaveNormalizationWarmupTask(std::uint64_t expectedTaskId = 0) noexcept;

	void clearFoldWarmupTask(std::uint64_t expectedTaskId = 0) noexcept;

	void setSyntaxTitleHint(const std::string &title);

	const char *syntaxLanguageName() const noexcept;

	MRSyntaxLanguage syntaxLanguage() const noexcept;

	bool syntaxLanguageAutomatic() const noexcept;

	bool buildFoldOutlineSnapshot(const MROutlineRequest &request, MROutlineSnapshot &snapshot) const;
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

		bool appendBufferData(const char *data, uint length);

		bool appendBufferText(const char *text);
		bool appendLogViewerData(const char *data, uint length, const std::vector<std::pair<std::size_t, std::size_t>> *chunkFindRanges = nullptr);
		bool prependLogViewerData(const char *data, uint length, const std::vector<std::pair<std::size_t, std::size_t>> *chunkFindRanges = nullptr);

		bool replaceRangeAndSelect(uint start, uint end, const char *data, uint length);

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

		bool deleteCharsAtCursor(int count);

		bool deleteCurrentLineText();

		bool replaceWholeBuffer(const std::string &text, std::size_t cursorPos);

		MRTextBufferModel::CommitResult applyStagedTransaction(const MRTextBufferModel::StagedTransaction &transaction, std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState = true);

		bool newLineWithIndent(const std::string &fill);

		bool newLineWithPreferredIndent();

		int leadingIndentColumnForLine(std::size_t lineStart) const noexcept;

		int inferredShellIndentStepColumns(std::size_t lineStart, const MREditSetupSettings &settings) const noexcept;

		std::string automaticIndentFillForCursor() const;

		std::string smartIndentFillForCursor();

		bool applyCurrentLineLeadingIndent(int targetColumn);

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

	int configuredTabSize() const;

	bool configuredDisplayTabs() const;

	bool configuredFormatRuler() const;

	static int tabDisplayWidth(const MREditSetupSettings &settings, int visualColumn) noexcept;

	std::string preferredIndentFill() const;

	int visibleTextRows() const noexcept;

	void syncScrollBarsToState() noexcept;

	static int decimalDigits(std::size_t value) noexcept;

	bool shouldTraceLargeFileWarmupDiagnostics() const noexcept;

	void traceLargeFileWarmup(std::string &slot, const char *stage, std::string detail);

	struct SaveNormalizationCache {
		bool valid = false;
		std::size_t documentId = 0;
		std::size_t version = 0;
		std::size_t optionsHash = 0;
		std::size_t sourceBytes = 0;
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

		void drawCodeFoldingGutter(TDrawBuffer &b, int drawX, int width, std::size_t lineStart, std::size_t lineIndex);

	static const char *lineIndexWarmupTaskLabel() noexcept;

	static const char *syntaxWarmupTaskLabel() noexcept;

	static const char *saveNormalizationWarmupTaskLabel() noexcept;

	static const char *foldWarmupTaskLabel() noexcept;

	bool lineIntersectsDirtyRanges(std::size_t lineStart, std::size_t lineEnd) const noexcept;

	bool findMarkerContainsOffset(std::size_t offset) const noexcept;
	bool lspDiagnosticInformationContainsOffset(std::size_t offset) const noexcept;
	bool lspDocumentHighlightContainsOffset(std::size_t offset) const noexcept;

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

	int longestLineWidth() const noexcept;

	bool useApproximateLargeFileMetrics() const noexcept;

	int dynamicLargeFileLineLimit() const noexcept;

	int dynamicLargeFileWidthLimit() const;

	void scheduleLineIndexWarmupIfNeeded();

	void scheduleSyntaxWarmupIfNeeded();

	void scheduleFoldWarmupIfNeeded(std::size_t scanTopLine, std::size_t scanBottomLine, std::size_t topLine, std::size_t requestBottomLine, MRSyntaxLanguage language);

	bool resolveSaveOptionsForPath(const char *path, MRTextSaveOptions &options, std::size_t *optionsHash = nullptr) const;

	void invalidateSaveNormalizationCache() noexcept;

	void noteSaveNormalizationThroughput(std::size_t sourceBytes, double runMicros) noexcept;

	void scheduleSaveNormalizationWarmupIfNeeded();

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

		MREditSetupSettings effectiveEditSetupSettings() const;

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

		void refreshVisibleSyntaxCacheForImmediateDraw();

	void invalidateFoldCache(bool preserveVisibleProjection = false) noexcept;

	void ensureVisibleFoldSpans(std::size_t topLine, int rowCount, MRSyntaxLanguage language);

	int visibleFoldGutterColumns() const noexcept;

	bool toggleFoldAtLine(std::size_t lineIndex);

	bool foldingGutterHit(TPoint local, std::size_t *lineIndexOut = nullptr) const noexcept;

	std::size_t foldedVisibleLineCount() const noexcept;

		std::vector<std::size_t> syntaxWarmupLineStarts(std::size_t topLine, int rowCount) const;

		bool syntaxCheckpointForLine(std::size_t lineIndex, MRSyntaxCheckpointEntry &checkpoint) const;

		void rememberSyntaxCheckpoint(std::size_t lineStart, std::size_t lineIndex, const MRSyntaxLineState &stateIn) noexcept;

		MRSyntaxLineState syntaxWarmupInitialState(std::size_t lineStart) const noexcept;

		bool hasSyntaxTokensForLineStarts(const std::vector<std::size_t> &lineStarts, const MRSyntaxLineState &initialState = MRSyntaxLineState()) const;

		std::size_t syntaxCachedCoveragePrefix(const std::vector<std::size_t> &lineStarts, const MRSyntaxLineState &initialState, MRSyntaxLineState *stateOut = nullptr) const;

		void formatSyntaxLine(TDrawBuffer &b, std::size_t lineStart, const MRSyntaxLineResult &syntaxLine, int hScroll, int width, int drawX, bool isDocumentLine, bool drawEofMarker, bool drawEofMarkerAsEmoji);

		void drawEofMarkerGlyph(TDrawBuffer &b, int hScroll, int width, int drawX, TAttrPair basePair, bool drawEmoji);

		bool syncAfterCommittedDocument(std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState, const MRTextBufferModel::DocumentChangeSet *changeSet = nullptr);
		bool adoptCommittedDocument(const MRTextBufferModel::Document &document, std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState, const MRTextBufferModel::DocumentChangeSet *changeSet = nullptr);

	TIndicator *mIndicator;
	bool mReadOnly;
	bool mCustomWindowEofMarkerColorOverrideValid = false;
	TColorAttr mCustomWindowEofMarkerColorOverride = 0;
	bool mCommunicationViewerMode = false;
	bool mCommunicationViewerLineNumbers = true;
	bool mMiniMapSuppressed = false;
	bool mWordWrapSuppressed = false;
	bool mScrollBarsAlwaysVisible = false;
	bool mInsertMode;
	bool mAutoIndent;
		char fileName[MAXPATH];
		std::string mSyntaxTitleHint;
		DestructionProbe mDestructionProbe;
		MRTextBufferModel mBufferModel;
	MRTextBufferModel::DocumentChangeSet mLastDocumentChangeSet;
	std::size_t mSelectionAnchor;
	int mCursorVisualColumn;
	bool mIndicatorUpdateInProgress;
	std::uint64_t mLineIndexWarmupTaskId;
	std::size_t mLineIndexWarmupDocumentId;
	std::size_t mLineIndexWarmupVersion;
	bool mSuppressLargeFileLineIndexWarmup;
	MRSyntaxDerivedState mSyntaxState;
	MRFoldingDerivedState mFoldState;
	MRMiniMapDerivedState mMiniMapState;
	std::vector<unsigned char> mFileCompareLineKinds;
	std::vector<MRFileCompareMiniMapSlice> mFileCompareMiniMapSlices;
	std::string mFileCompareLeftGutters;
	std::string mFileCompareRightGutters;
	bool mFileCompareGuttersConfigured = false;
	bool mFileCompareGutterVisible = true;
	SaveNormalizationCache mSaveNormalizationCache;
	std::uint64_t mSaveNormalizationWarmupTaskId;
	std::size_t mSaveNormalizationWarmupDocumentId;
	std::size_t mSaveNormalizationWarmupVersion;
	std::size_t mSaveNormalizationWarmupOptionsHash;
	std::size_t mSaveNormalizationWarmupSourceBytes;
	std::chrono::steady_clock::time_point mSaveNormalizationWarmupStartedAt;
	double mSaveNormalizationThroughputBytesPerMicro;
	std::size_t mSaveNormalizationThroughputSamples;
	bool mMouseSelectionColumnsValid;
	int mMouseSelectionAnchorColumn;
	int mMouseSelectionCursorColumn;
	unsigned short mMouseSelectionModifiers;
	bool mBlockOverlayActive;
	int mBlockOverlayMode;
	std::size_t mBlockOverlayAnchor;
	std::size_t mBlockOverlayEnd;
	bool mBlockOverlayTrackCursor;
	int mBlockOverlayColumnAnchor;
	int mBlockOverlayColumnEnd;
	int mPreferredIndentColumn;
	std::vector<MRTextBufferModel::Range> mFindMarkerRanges;
	std::vector<MRTextBufferModel::Range> mDirtyRanges;
	std::vector<MRTextBufferModel::Range> mCompilerErrorRanges;
	std::vector<MRTextBufferModel::Range> mCompilerWarningRanges;
	std::vector<MRTextBufferModel::Range> mLspDiagnosticInformationRanges;
	std::vector<MRTextBufferModel::Range> mLspDocumentHighlightRanges;
	LoadTiming mLastLoadTiming;
	mutable std::size_t mCachedCursorLineDocumentId;
	mutable std::size_t mCachedCursorLineVersion;
	mutable std::size_t mCachedCursorLineOffset;
	mutable std::size_t mCachedCursorLineIndexValue;
	std::string mLastLineIndexWarmupTrace;
	std::string mLastSyntaxWarmupTrace;
	std::string mLastMiniMapWarmupTrace;
	std::string mLastComputeWarmupTrace;
	std::string mLastUiHotpathTrace;

	void clearDirtyRanges() noexcept;

	static void normalizePairRangeList(std::vector<std::pair<std::size_t, std::size_t>> &ranges);

	static void normalizeRangeList(std::vector<MRTextBufferModel::Range> &ranges);

	void normalizeDirtyRanges();

	void pushMappedDirtyRange(std::vector<MRTextBufferModel::Range> &mapped, std::size_t start, std::size_t end, std::size_t maxLength);

	void remapDirtyRangesForAppliedChange(const MRTextBufferModel::DocumentChangeSet &change);

	void remapLspDiagnosticInformationRangesForAppliedChange(const MRTextBufferModel::DocumentChangeSet &change);

	void addDirtyRange(MRTextBufferModel::Range range);

	bool isDirtyOffset(std::size_t pos) const noexcept;

};

#endif
