#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"
#include "../../app/MREditorApp.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../../outline/MROutlineFoldProducer.hpp"

#include <chrono>
#include <ctime>
#include <sstream>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif

namespace {

std::string directProbeTimestamp() {
	std::array<char, 32> buffer{};
	const std::time_t now = std::time(nullptr);
	const std::tm *tmNow = std::localtime(&now);

	if (tmNow == nullptr) return std::string("--:--:--");
	if (std::strftime(buffer.data(), buffer.size(), "%H:%M:%S", tmNow) == 0) return std::string("--:--:--");
	return std::string(buffer.data());
}

void appendDirectProbeLog(std::string_view message) {
	std::ofstream out("misc/mr.log", std::ios::out | std::ios::app | std::ios::binary);

	if (!out) return;
	out << "[" << directProbeTimestamp() << "] " << message << '\n';
	out.flush();
}

template <class Duration> long long traceMicros(Duration duration) {
	return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

} // namespace


MRFileEditor::LoadTiming::LoadTiming() noexcept : valid(false), bytes(0), lines(0), linesExact(false), mappedLoadMs(0.0), lineCountMs(0.0) {
}

MRFileEditor::DestructionProbe::~DestructionProbe() {
	if (!active) return;
	std::ostringstream line;
	line << "MRFileEditor destroy buffer_id=" << bufferId << " title='" << title << "' len=" << length << " add=" << addBufferLength << " pieces=" << pieceCount << " undo=" << undoDepth
	     << " redo=" << redoDepth << " took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt).count() << ".";
	appendDirectProbeLog(line.str());
}

MRFileEditor::MRFileEditor(const TRect &bounds, TScrollBar *aHScrollBar, TScrollBar *aVScrollBar, TIndicator *aIndicator, TStringView aFileName) noexcept
    : TScroller(bounds, aHScrollBar, aVScrollBar), mIndicator(aIndicator), mReadOnly(false), mInsertMode(true), mAutoIndent(false), mSyntaxTitleHint(), mBufferModel(), mSelectionAnchor(0), mCursorVisualColumn(0), mIndicatorUpdateInProgress(false), mLineIndexWarmupTaskId(0), mLineIndexWarmupDocumentId(0), mLineIndexWarmupVersion(0), mSuppressLargeFileLineIndexWarmup(false), mSyntaxState(), mFoldState(), mMiniMapState(), mSaveNormalizationCache(), mSaveNormalizationWarmupTaskId(0), mSaveNormalizationWarmupDocumentId(0), mSaveNormalizationWarmupVersion(0),
      mSaveNormalizationWarmupOptionsHash(0), mSaveNormalizationWarmupSourceBytes(0), mSaveNormalizationWarmupStartedAt(std::chrono::steady_clock::time_point()), mSaveNormalizationThroughputBytesPerMicro(0.0), mSaveNormalizationThroughputSamples(0), mMouseSelectionColumnsValid(false), mMouseSelectionAnchorColumn(0), mMouseSelectionCursorColumn(0), mMouseSelectionModifiers(0), mBlockOverlayActive(false), mBlockOverlayMode(0), mBlockOverlayAnchor(0), mBlockOverlayEnd(0), mBlockOverlayTrackCursor(false), mBlockOverlayColumnAnchor(-1), mBlockOverlayColumnEnd(-1), mPreferredIndentColumn(1), mLastLoadTiming(), mCachedCursorLineDocumentId(0), mCachedCursorLineVersion(0), mCachedCursorLineOffset(0), mCachedCursorLineIndexValue(0) {
	fileName[0] = EOS;
	options |= ofFirstClick;
	eventMask |= evMouse | evKeyboard | evCommand;
	if (!aFileName.empty()) setPersistentFileName(aFileName);
	syncFromEditorState(false);
}

MRFileEditor::~MRFileEditor() {
	mDestructionProbe.arm(static_cast<int>(mBufferModel.documentId()), persistentFileName(), mBufferModel.length(), mBufferModel.document().addBufferLength(), mBufferModel.document().pieceCount(),
	                     mBufferModel.undoStackDepth(), mBufferModel.redoStackDepth());
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
	refreshSyntaxContext();
	scheduleSaveNormalizationWarmupIfNeeded();
}

void MRFileEditor::clearPersistentFileName() noexcept {
	fileName[0] = EOS;
	refreshSyntaxContext();
	scheduleSaveNormalizationWarmupIfNeeded();
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
	return mLineIndexWarmupTaskId;
}

std::uint64_t MRFileEditor::pendingSyntaxWarmupTaskId() const noexcept {
	return mSyntaxState.warmupState().taskId;
}

std::uint64_t MRFileEditor::pendingFoldWarmupTaskId() const noexcept {
	return mFoldState.warmupState().taskId;
}

std::uint64_t MRFileEditor::pendingMiniMapWarmupTaskId() const noexcept {
	return mMiniMapState.renderer().pendingWarmupTaskId();
}

std::uint64_t MRFileEditor::pendingSaveNormalizationWarmupTaskId() const noexcept {
	return mSaveNormalizationWarmupTaskId;
}

std::size_t MRFileEditor::syntaxWarmupTopLine() const noexcept {
	return mSyntaxState.warmupState().topLine;
}

std::size_t MRFileEditor::syntaxWarmupBottomLine() const noexcept {
	return mSyntaxState.warmupState().bottomLine;
}

std::size_t MRFileEditor::syntaxPrefetchTargetBottomLine() const noexcept {
	return mSyntaxState.prefetchState().targetBottomLine;
}

std::size_t MRFileEditor::syntaxPrefetchReachedBottomLine() const noexcept {
	return mSyntaxState.prefetchState().reachedBottomLine;
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

bool MRFileEditor::lineIndexWarmupPending() const noexcept {
	return mLineIndexWarmupTaskId != 0;
}

bool MRFileEditor::syntaxWarmupPending() const noexcept {
	return mSyntaxState.warmupState().taskId != 0;
}

bool MRFileEditor::miniMapWarmupPending() const noexcept {
	return mMiniMapState.renderer().pendingWarmupTaskId() != 0;
}

bool MRFileEditor::saveNormalizationWarmupPending() const noexcept {
	return mSaveNormalizationWarmupTaskId != 0;
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

int MRFileEditor::preferredIndentColumn() const noexcept {
	return mPreferredIndentColumn;
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
	clearDirtyRanges();
	syncFromEditorState(false);
}

void MRFileEditor::detachContentStateCopy() {
	mBufferModel.detachContentStateCopy();
	clearFindMarkerRanges();
	clearDirtyRanges();
	syncFromEditorState(false);
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
	const auto startedAt = std::chrono::steady_clock::now();
	Boolean result = !canSaveInPlace() ? confirmSaveOrDiscardUntitled() : confirmSaveOrDiscardNamed();
	std::ostringstream trace;
	trace << "Phase1 discard editor valid total_us=" << traceMicros(std::chrono::steady_clock::now() - startedAt) << " result=" << (result == True ? 1 : 0) << " len=" << mBufferModel.length()
	      << " add=" << mBufferModel.document().addBufferLength() << " pieces=" << mBufferModel.document().pieceCount() << " modified=" << (mBufferModel.isModified() ? 1 : 0);
	appendDirectProbeLog(trace.str());
	return result;
}


#if defined(__clang__)
#pragma clang diagnostic pop
#endif
