#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"
#include "../../app/MREditorApp.hpp"
#include "../../outline/MROutlineFoldProducer.hpp"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif

MRFileEditor::LoadTiming::LoadTiming() noexcept : valid(false), bytes(0), lines(0), linesExact(false), mappedLoadMs(0.0), lineCountMs(0.0) {
}

MRFileEditor::MRFileEditor(const TRect &bounds, TScrollBar *aHScrollBar, TScrollBar *aVScrollBar, TIndicator *aIndicator, TStringView aFileName,
                           mr::coprocessor::ExecutionOwnerKind executionOwnerKind, std::size_t executionOwnerLocalId) noexcept
	: TScroller(bounds, aHScrollBar, aVScrollBar), mIndicator(aIndicator), mReadOnly(false), mForceBinarySave(false), mInsertMode(true), mLineDrawingEnabled(false), mLineDrawingDoubleLines(false), mAutoIndent(false), mSyntaxTitleHint(), mBufferModel(), mSelectionAnchor(0), mCursorVisualLine(0), mCursorVisualColumn(0), mIndicatorUpdateInProgress(false), mLineIndexWarmupState(), mLineIndexGenerationCounter(1), mSuppressLargeFileLineIndexWarmup(false), mDisplayWidthWarmupState(), mDisplayWidthGenerationCounter(1), mDisplayWidthPublishedLimit(1), mExecutionOwnerKind(executionOwnerKind), mExecutionOwnerLocalId(executionOwnerLocalId), mSyntaxWarmupState(), mSyntaxGenerationCounter(1), mSyntaxState(), mFoldCanonicalContextState(), mFoldWarmupState(), mFoldLevelOperationState(), mFoldGenerationCounter(1), mFoldState(), mFoldOutlineInputCache(), mMiniMapState(), mFileCompareLineKinds(std::make_shared<const std::vector<unsigned char>>()), mFileCompareMiniMapSlices(std::make_shared<const std::vector<MRFileCompareMiniMapSlice>>()), mSaveNormalizationCache(), mSaveNormalizationThroughputBytesPerMicro(0.0), mSaveNormalizationThroughputSamples(0), mMouseSelectionColumnsValid(false), mMouseSelectionLinesValid(false), mMouseSelectionAnchorColumn(0), mMouseSelectionCursorColumn(0), mMouseSelectionAnchorLine(0), mMouseSelectionCursorLine(0), mMouseSelectionModifiers(0), mBlockOverlayActive(false), mBlockOverlayMode(0), mBlockOverlayAnchor(0), mBlockOverlayEnd(0), mBlockOverlayTrackCursor(false), mBlockOverlayColumnAnchor(-1), mBlockOverlayColumnEnd(-1), mBlockOverlayLineRangeValid(false), mBlockOverlayLine1(0), mBlockOverlayLine2(0), mPreferredIndentColumn(1), mLastLoadTiming(), mCachedCursorLineDocumentId(0), mCachedCursorLineVersion(0), mCachedCursorLineOffset(0), mCachedCursorLineIndexValue(0), mCachedCursorLineExact(false) {
	fileName[0] = EOS;
	options |= ofFirstClick;
	eventMask |= evMouse | evKeyboard | evCommand;
	if (!aFileName.empty()) setPersistentFileName(aFileName);
	else
		refreshEditorSettingsSnapshot();
	syncFromEditorState(false);
}

MRFileEditor::~MRFileEditor() {
	for (const LineIndexPacketState &packet : mLineIndexWarmupState.packets) {
		if (packet.taskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(packet.taskId));
		mBufferModel.releaseLineIndexScanReservation(packet.reservationId);
	}
	for (const DisplayWidthPacketState &packet : mDisplayWidthWarmupState.packets)
		if (packet.taskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(packet.taskId));
	static_cast<void>(cancelSyntaxWarmup());
	static_cast<void>(cancelFoldWarmup());
}

bool MRFileEditor::isReadOnly() const {
	return mReadOnly;
}

void MRFileEditor::setWindowEofMarkerColorOverride(bool enabled, TColorAttr color) {
	mCustomWindowEofMarkerColorOverrideValid = enabled;
	mCustomWindowEofMarkerColorOverride = color;
	drawView();
}

void MRFileEditor::setReadOnly(bool readOnly) {
	if (mReadOnly != readOnly) {
		mReadOnly = readOnly;
		if (mReadOnly) setDocumentModified(false);
		syncFromEditorState(false);
	}
}

void MRFileEditor::setForceBinarySave(bool enabled) noexcept {
	mForceBinarySave = enabled;
}

const char *MRFileEditor::persistentFileName() const noexcept {
	return hasPersistentFileName() ? fileName : "";
}

std::size_t MRFileEditor::persistentFileNameCapacity() const noexcept {
	return sizeof(fileName);
}

bool MRFileEditor::hasPersistentFileName() const {
	return fileName[0] != EOS;
}

void MRFileEditor::setPersistentFileName(TStringView name) noexcept {
	strnzcpy(fileName, name, sizeof(fileName));
	refreshEditorSettingsSnapshot();
	refreshSyntaxContext();
	invalidateSaveNormalizationCache();
}

void MRFileEditor::clearPersistentFileName() noexcept {
	fileName[0] = EOS;
	refreshEditorSettingsSnapshot();
	refreshSyntaxContext();
	invalidateSaveNormalizationCache();
}

bool MRFileEditor::isDocumentModified() const noexcept {
	return mBufferModel.isModified();
}

void MRFileEditor::setDocumentModified(bool changed) {
	mBufferModel.setModified(changed);
	if (!changed) {
		mBufferModel.clearUndoRedo();
		clearDirtyRanges();
	}
	syncFromEditorState(false);
}

bool MRFileEditor::hasUndoHistory() const noexcept {
	return mBufferModel.undoStackDepth() > 0;
}

bool MRFileEditor::hasRedoHistory() const noexcept {
	return mBufferModel.redoStackDepth() > 0;
}

bool MRFileEditor::insertModeEnabled() const noexcept {
	return mInsertMode;
}

bool MRFileEditor::lineDrawingEnabled() const noexcept {
	return mLineDrawingEnabled;
}

bool MRFileEditor::lineDrawingDoubleLines() const noexcept {
	return mLineDrawingDoubleLines;
}

std::size_t MRFileEditor::originalBufferLength() const noexcept {
	return mBufferModel.document().originalLength();
}

std::size_t MRFileEditor::addBufferLength() const noexcept {
	return mBufferModel.document().addBufferLength();
}

std::size_t MRFileEditor::pieceCount() const noexcept {
	return mBufferModel.document().pieceCount();
}

bool MRFileEditor::hasMappedOriginalSource() const noexcept {
	return mBufferModel.document().hasMappedOriginal();
}

const std::string &MRFileEditor::mappedOriginalPath() const noexcept {
	return mBufferModel.document().mappedPath();
}

std::size_t MRFileEditor::estimatedLineCount() const noexcept {
	return mBufferModel.estimatedLineCount();
}

bool MRFileEditor::exactLineCountKnown() const noexcept {
	return mBufferModel.exactLineCountKnown();
}

std::size_t MRFileEditor::selectionLength() const noexcept {
	return mBufferModel.selection().range().length();
}

std::uint64_t MRFileEditor::pendingLineIndexWarmupTaskId() const noexcept {
	for (const LineIndexPacketState &packet : mLineIndexWarmupState.packets)
		if (packet.taskId != 0) return packet.taskId;
	return 0;
}

std::size_t MRFileEditor::pendingLineIndexWarmupTaskCount() const noexcept {
	std::size_t count = 0;
	for (const LineIndexPacketState &packet : mLineIndexWarmupState.packets)
		if (packet.taskId != 0) ++count;
	return count;
}

bool MRFileEditor::ownsLineIndexWarmupTask(std::uint64_t taskId) const noexcept {
	if (taskId == 0) return false;
	for (const LineIndexPacketState &packet : mLineIndexWarmupState.packets)
		if (packet.taskId == taskId) return true;
	return false;
}

std::size_t MRFileEditor::pendingDisplayWidthWarmupTaskCount() const noexcept {
	std::size_t count = 0;
	for (const DisplayWidthPacketState &packet : mDisplayWidthWarmupState.packets)
		if (packet.taskId != 0) ++count;
	return count;
}

bool MRFileEditor::ownsDisplayWidthWarmupTask(std::uint64_t taskId) const noexcept {
	if (taskId == 0) return false;
	for (const DisplayWidthPacketState &packet : mDisplayWidthWarmupState.packets)
		if (packet.taskId == taskId) return true;
	return false;
}

std::uint64_t MRFileEditor::pendingSyntaxWarmupTaskId() const noexcept {
	for (const SyntaxPacketState &packet : mSyntaxWarmupState.packets)
		if (packet.taskId != 0) return packet.taskId;
	return 0;
}

std::size_t MRFileEditor::pendingSyntaxWarmupTaskCount() const noexcept {
	std::size_t count = 0;
	for (const SyntaxPacketState &packet : mSyntaxWarmupState.packets)
		if (packet.taskId != 0) ++count;
	return count;
}

bool MRFileEditor::ownsSyntaxWarmupTask(std::uint64_t taskId) const noexcept {
	if (taskId == 0) return false;
	for (const SyntaxPacketState &packet : mSyntaxWarmupState.packets)
		if (packet.taskId == taskId) return true;
	return false;
}

std::uint64_t MRFileEditor::pendingFoldWarmupTaskId() const noexcept {
	for (const FoldCanonicalPacketState &packet : mFoldCanonicalContextState.packets)
		if (packet.taskId != 0) return packet.taskId;
	for (const FoldPacketState &packet : mFoldWarmupState.packets)
		if (packet.taskId != 0) return packet.taskId;
	for (const FoldLevelPacketState &packet : mFoldLevelOperationState.packets)
		if (packet.taskId != 0) return packet.taskId;
	if (mFoldLevelOperationState.projectionTaskId != 0) return mFoldLevelOperationState.projectionTaskId;
	return 0;
}

std::size_t MRFileEditor::pendingFoldWarmupTaskCount() const noexcept {
	std::size_t count = 0;
	for (const FoldCanonicalPacketState &packet : mFoldCanonicalContextState.packets)
		if (packet.taskId != 0) ++count;
	for (const FoldPacketState &packet : mFoldWarmupState.packets)
		if (packet.taskId != 0) ++count;
	for (const FoldLevelPacketState &packet : mFoldLevelOperationState.packets)
		if (packet.taskId != 0) ++count;
	if (mFoldLevelOperationState.projectionTaskId != 0) ++count;
	return count;
}

bool MRFileEditor::ownsFoldWarmupTask(std::uint64_t taskId) const noexcept {
	if (taskId == 0) return false;
	if (ownsCanonicalFoldContextTask(taskId)) return true;
	for (const FoldPacketState &packet : mFoldWarmupState.packets)
		if (packet.taskId == taskId) return true;
	for (const FoldLevelPacketState &packet : mFoldLevelOperationState.packets)
		if (packet.taskId == taskId) return true;
	return mFoldLevelOperationState.projectionTaskId == taskId;
}

std::uint64_t MRFileEditor::pendingMiniMapWarmupTaskId() const noexcept {
	return mMiniMapState.renderer().pendingWarmupTaskId();
}

std::size_t MRFileEditor::pendingMiniMapWarmupTaskCount() const noexcept {
	return mMiniMapState.renderer().pendingWarmupTaskCount();
}

bool MRFileEditor::ownsMiniMapWarmupTask(std::uint64_t taskId) const noexcept {
	return mMiniMapState.renderer().ownsWarmupTask(taskId);
}

bool MRFileEditor::miniMapProjectionAvailable() const noexcept {
	return mMiniMapState.renderer().hasAnyProjection();
}

std::size_t MRFileEditor::syntaxWarmupTopLine() const noexcept {
	return mSyntaxWarmupState.visibleTopLine;
}

std::size_t MRFileEditor::syntaxWarmupBottomLine() const noexcept {
	return mSyntaxWarmupState.visibleBottomLine;
}

std::size_t MRFileEditor::syntaxPrefetchTargetBottomLine() const noexcept {
	return mSyntaxWarmupState.targetBottomLine;
}

std::size_t MRFileEditor::syntaxPrefetchReachedBottomLine() const noexcept {
	return mSyntaxWarmupState.reachedBottomLine;
}

bool MRFileEditor::shouldReportMiniMapInitialRender() const noexcept {
	return mMiniMapState.shouldReportInitialRender(mBufferModel.documentId());
}

void MRFileEditor::markMiniMapInitialRenderReported() noexcept {
	mMiniMapState.markInitialRenderReported(mBufferModel.documentId());
}

const std::string &MRFileEditor::lastUiHotpathTrace() const noexcept {
	return mLastUiHotpathTrace;
}

bool MRFileEditor::usesApproximateMetrics() const noexcept {
	return useApproximateLargeFileMetrics();
}

void MRFileEditor::setInsertModeEnabled(bool on) {
	if (mInsertMode == on) return;
	mInsertMode = on;
	syncFromEditorState(false);
	if (owner != nullptr) message(owner, evBroadcast, cmUpdateTitle, 0);
}

void MRFileEditor::setLineDrawingEnabled(bool on) {
	if (mLineDrawingEnabled == on) return;
	mLineDrawingEnabled = on;
	if (!mLineDrawingEnabled) mLineDrawingDoubleLines = false;
	if (owner != nullptr) message(owner, evBroadcast, cmUpdateTitle, 0);
}

void MRFileEditor::setLineDrawingDoubleLines(bool on) {
	if (!mLineDrawingEnabled) return;
	if (mLineDrawingDoubleLines == on) return;
	mLineDrawingDoubleLines = on;
	if (owner != nullptr) message(owner, evBroadcast, cmUpdateTitle, 0);
}

void MRFileEditor::toggleLineDrawing() {
	setLineDrawingEnabled(!lineDrawingEnabled());
}

void MRFileEditor::toggleLineDrawingDoubleLines() {
	if (!lineDrawingEnabled()) return;
	setLineDrawingDoubleLines(!lineDrawingDoubleLines());
}

void MRFileEditor::setPreferredIndentColumn(int column) noexcept {
	if (column < 1) column = 1;
	if (column > 999) column = 999;
	mPreferredIndentColumn = column;
}

const MRTextBufferModel &MRFileEditor::bufferModel() const noexcept {
	return mBufferModel;
}

MRTextBufferModel &MRFileEditor::bufferModel() noexcept {
	return mBufferModel;
}

void MRFileEditor::shareContentStateFrom(const MRFileEditor &source) {
	mBufferModel.shareContentStateFrom(source.bufferModel());
	if (source.hasPersistentFileName()) setPersistentFileName(source.persistentFileName());
	else
		clearPersistentFileName();
	clearFindMarkerRanges();
	clearDebuggerBreakpointRanges();
	clearDebuggerInstructionLine();
	clearDirtyRanges();
	syncFromEditorState(false);
	scheduleLineIndexWarmupIfNeeded();
}

void MRFileEditor::detachContentStateCopy() {
	mBufferModel.detachContentStateCopy();
	clearFindMarkerRanges();
	clearDebuggerBreakpointRanges();
	clearDebuggerInstructionLine();
	clearDirtyRanges();
	syncFromEditorState(false);
	scheduleLineIndexWarmupIfNeeded();
}

std::string MRFileEditor::snapshotText() const {
	return mBufferModel.text();
}

MRTextBufferModel::ReadSnapshot MRFileEditor::readSnapshot() const {
	return mBufferModel.readSnapshot();
}

MRTextBufferModel::Document MRFileEditor::documentCopy() const {
	return mBufferModel.document();
}

std::size_t MRFileEditor::documentId() const noexcept {
	return mBufferModel.documentId();
}

std::size_t MRFileEditor::documentVersion() const noexcept {
	return mBufferModel.version();
}

const MRTextBufferModel::DocumentChangeSet &MRFileEditor::lastDocumentChangeSet() const noexcept {
	return mLastDocumentChangeSet;
}

MRFileEditor::LoadTiming MRFileEditor::lastLoadTiming() const noexcept {
	return mLastLoadTiming;
}

const char *MRFileEditor::syntaxLanguageName() const noexcept {
	return mBufferModel.languageName();
}

MRSyntaxLanguage MRFileEditor::syntaxLanguage() const noexcept {
	return mBufferModel.language();
}

bool MRFileEditor::syntaxLanguageAutomatic() const noexcept {
	return mBufferModel.languageAutomatic();
}

Boolean MRFileEditor::valid(ushort command) {
	if (command == cmValid || command == cmReleasedFocus) return True;
	if (mReadOnly || !mBufferModel.isModified()) return True;
	return !canSaveInPlace() ? confirmSaveOrDiscardUntitled() : confirmSaveOrDiscardNamed();
}


#if defined(__clang__)
#pragma clang diagnostic pop
#endif
