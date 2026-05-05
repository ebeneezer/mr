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
#include "../MRIndicator.hpp"
#include "MRMiniMap.hpp"
#include "MRTextFormatting.hpp"
#include "MRTextViewport.hpp"
#include "MRTreeSitterDocument.hpp"
#include "../MRTextBufferModel.hpp"
#include "../../config/MRDialogPaths.hpp"
#include "../../app/MRCommands.hpp"
#include "../../app/utils/MRFileIOUtils.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "../MRMessageLineController.hpp"
#include "../MRWindowSupport.hpp"

class MREditWindow;

class MRFileEditor : public TScroller {
  public:
	struct LoadTiming {
		bool valid;
		std::size_t bytes;
		std::size_t lines;
		double mappedLoadMs;
		double lineCountMs;

		LoadTiming() noexcept;
	};

	MRFileEditor(const TRect &bounds, TScrollBar *aHScrollBar, TScrollBar *aVScrollBar, TIndicator *aIndicator, TStringView aFileName) noexcept;

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

	std::uint64_t pendingMiniMapWarmupTaskId() const noexcept;

	std::uint64_t pendingSaveNormalizationWarmupTaskId() const noexcept;

	bool shouldReportMiniMapInitialRender() const noexcept;

	void markMiniMapInitialRenderReported() noexcept;

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

	void syncDisplayedCursorColumnFromCursor(bool preserveFreeColumn) noexcept;

	void refreshConfiguredVisualSettings();

	std::size_t cursorOffset() const noexcept;

	std::size_t bufferLength() const noexcept;

	std::size_t selectionStartOffset() const noexcept;

	std::size_t selectionEndOffset() const noexcept;

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

	bool scrollWindowByLines(int deltaRows);

	std::size_t offsetForGlobalPoint(TPoint where) noexcept;

		void setBlockOverlayState(int mode, std::size_t anchor, std::size_t end, bool active, bool trackCursor = false);

		void setSelectionOffsets(std::size_t start, std::size_t end, Boolean = False);

	void setFindMarkerRanges(const std::vector<std::pair<std::size_t, std::size_t>> &ranges);

	void clearFindMarkerRanges();

	void revealCursor(Boolean centerCursor = True);

	void refreshViewState();

	void update(uchar);

	int currentLineNumber() const noexcept;

	int currentViewRow() const noexcept;

	int visibleViewportRows() const noexcept;

	const MRTextBufferModel &bufferModel() const noexcept;

	MRTextBufferModel &bufferModel() noexcept;

	void syncFromEditorState(bool = true);

	void syncIndicatorVisualSettings();

	void notifyWindowTaskStateChanged();

	std::string snapshotText() const;

	MRTextBufferModel::ReadSnapshot readSnapshot() const;

	MRTextBufferModel::Document documentCopy() const;

	std::size_t documentId() const noexcept;

	std::size_t documentVersion() const noexcept;

	LoadTiming lastLoadTiming() const noexcept;

	bool applyLineIndexWarmup(const mr::editor::LineIndexWarmupData &warmup, std::size_t expectedVersion);

	bool applySyntaxWarmup(const mr::coprocessor::SyntaxWarmupPayload &warmup, std::size_t expectedVersion, std::uint64_t expectedTaskId);

	bool applyMiniMapWarmup(const mr::coprocessor::MiniMapWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId);

	bool applySaveNormalizationWarmup(const mr::coprocessor::SaveNormalizationWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId, double runMicros);

	void clearLineIndexWarmupTask(std::uint64_t expectedTaskId) noexcept;

	void clearSyntaxWarmupTask(std::uint64_t expectedTaskId) noexcept;

	void clearMiniMapWarmupTask(std::uint64_t expectedTaskId) noexcept;

	void applyMiniMapSignals(const MRMiniMapRenderer::Signals &signals);

	void clearSaveNormalizationWarmupTask(std::uint64_t expectedTaskId = 0) noexcept;

	void setSyntaxTitleHint(const std::string &title);

	const char *syntaxLanguageName() const noexcept;

	MRSyntaxLanguage syntaxLanguage() const noexcept;

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

		std::string automaticIndentFillForCursor() const;

		std::string smartIndentFillForCursor();

		bool applyCurrentLineLeadingIndent(int targetColumn);

		void applyLiveSmartDedentAfterTextInput(const std::string &insertedText);

		void effectiveFormatMargins(const MREditSetupSettings &settings, int &leftMargin, int &rightMargin) const noexcept;

		bool wrapCurrentLineOnce(int leftMargin, int rightMargin);

		void applyLiveWordWrapAfterTextInput();

		virtual void draw() override;

	virtual TPalette &getPalette() const override;

		virtual void handleEvent(TEvent &event) override;

		virtual void scrollDraw() override;

		virtual void setState(ushort aState, Boolean enable) override;

	virtual Boolean valid(ushort command) override;

  private:
	static bool isWordByte(char ch) noexcept;

	static bool hasShiftModifier(ushort mods) noexcept;

	static int configuredTabSize() noexcept;

	static bool configuredDisplayTabs() noexcept;

	static bool configuredFormatRuler() noexcept;

	static int tabDisplayWidth(const MREditSetupSettings &settings, int visualColumn) noexcept;

	std::string preferredIndentFill() const;

	int visibleTextRows() const noexcept;

	void syncScrollBarsToState() noexcept;

	static int decimalDigits(std::size_t value) noexcept;

	bool shouldTraceLargeFileDiagnostics() const noexcept;

	void traceLargeFileMessage(const char *stage, const std::string &detail) const;

	void traceLargeFileMetrics(const char *stage, int limitY, int maxY, int textRows, int newDeltaY);

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

		void drawLineNumberGutter(TDrawBuffer &b, std::size_t lineIndex, bool showNumber, int drawX, int width, bool zeroFill);

		void drawCodeFoldingGutter(TDrawBuffer &b, int drawX, int width);

	static const char *lineIndexWarmupTaskLabel() noexcept;

	static const char *syntaxWarmupTaskLabel() noexcept;

	static const char *saveNormalizationWarmupTaskLabel() noexcept;

	bool lineIntersectsDirtyRanges(std::size_t lineStart, std::size_t lineEnd) const noexcept;

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

	bool resolveSaveOptionsForPath(const char *path, MRTextSaveOptions &options, std::size_t *optionsHash = nullptr) const;

	void invalidateSaveNormalizationCache() noexcept;

	void noteSaveNormalizationThroughput(std::size_t sourceBytes, double runMicros) noexcept;

	void scheduleSaveNormalizationWarmupIfNeeded();

	void updateMetrics();

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

	void resetSyntaxWarmupState(bool clearCache) noexcept;

		std::vector<std::size_t> syntaxWarmupLineStarts(std::size_t topLine, int rowCount) const;

		bool hasSyntaxTokensForLineStarts(const std::vector<std::size_t> &lineStarts) const;

		MRSyntaxTokenMap syntaxTokensForLine(std::size_t lineStart) const;

		void formatSyntaxLine(TDrawBuffer &b, std::size_t lineStart, int hScroll, int width, int drawX, bool isDocumentLine, bool drawEofMarker, bool drawEofMarkerAsEmoji);

		void drawEofMarkerGlyph(TDrawBuffer &b, int hScroll, int width, int drawX, TAttrPair basePair, bool drawEmoji);

		bool adoptCommittedDocument(const MRTextBufferModel::Document &document, std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState, const MRTextBufferModel::DocumentChangeSet *changeSet = nullptr);

	TIndicator *mIndicator;
	bool mReadOnly;
	bool mCustomWindowEofMarkerColorOverrideValid = false;
	TColorAttr mCustomWindowEofMarkerColorOverride = 0;
	bool mInsertMode;
	bool mAutoIndent;
	char fileName[MAXPATH];
	std::string mSyntaxTitleHint;
	MRTextBufferModel mBufferModel;
	std::size_t mSelectionAnchor;
	int mCursorVisualColumn;
	bool mIndicatorUpdateInProgress;
	std::uint64_t mLineIndexWarmupTaskId;
	std::size_t mLineIndexWarmupDocumentId;
	std::size_t mLineIndexWarmupVersion;
	std::map<std::size_t, MRSyntaxTokenMap> mSyntaxTokenCache;
	std::uint64_t mSyntaxWarmupTaskId;
	std::size_t mSyntaxWarmupDocumentId;
	std::size_t mSyntaxWarmupVersion;
	std::size_t mSyntaxWarmupTopLine;
	std::size_t mSyntaxWarmupBottomLine;
	MRSyntaxLanguage mSyntaxWarmupLanguage;
	MRTreeSitterDocument::Language mSyntaxWarmupTreeSitterLanguage;
	MRTreeSitterDocument mTreeSitterDocument;
	MRMiniMapRenderer mMiniMapRenderer;
	SaveNormalizationCache mSaveNormalizationCache;
	std::uint64_t mSaveNormalizationWarmupTaskId;
	std::size_t mSaveNormalizationWarmupDocumentId;
	std::size_t mSaveNormalizationWarmupVersion;
	std::size_t mSaveNormalizationWarmupOptionsHash;
	std::size_t mSaveNormalizationWarmupSourceBytes;
	std::chrono::steady_clock::time_point mSaveNormalizationWarmupStartedAt;
	double mSaveNormalizationThroughputBytesPerMicro;
	std::size_t mSaveNormalizationThroughputSamples;
	std::size_t mMiniMapInitialRenderReportedDocumentId;
	bool mBlockOverlayActive;
	int mBlockOverlayMode;
	std::size_t mBlockOverlayAnchor;
	std::size_t mBlockOverlayEnd;
	bool mBlockOverlayTrackingCursor;
	int mPreferredIndentColumn;
	std::vector<MRTextBufferModel::Range> mFindMarkerRanges;
	std::vector<MRTextBufferModel::Range> mDirtyRanges;
	LoadTiming mLastLoadTiming;
	bool mLargeFileMetricsTraceValid;
	bool mLastLargeFileMetricsExactKnown;
	int mLastLargeFileMetricsLimitY;
	int mLastLargeFileMetricsMaxY;
	int mLastLargeFileMetricsDeltaY;
	int mLastLargeFileMetricsNewDeltaY;

	void clearDirtyRanges() noexcept;

	static void normalizePairRangeList(std::vector<std::pair<std::size_t, std::size_t>> &ranges);

	static void normalizeRangeList(std::vector<MRTextBufferModel::Range> &ranges);

	void normalizeDirtyRanges();

	void pushMappedDirtyRange(std::vector<MRTextBufferModel::Range> &mapped, std::size_t start, std::size_t end, std::size_t maxLength);

	void remapDirtyRangesForAppliedChange(const MRTextBufferModel::DocumentChangeSet &change);

	void addDirtyRange(MRTextBufferModel::Range range);

	bool isDirtyOffset(std::size_t pos) const noexcept;

};

#endif
