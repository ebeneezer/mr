#include "MRFileEditor.hpp"

bool MRFileEditor::syncAfterCommittedDocument(std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState, const MRTextBufferModel::DocumentChangeSet *changeSet) {
	const MRTextBufferModel::Document &document = mBufferModel.document();
	const bool documentChanged = mCachedCursorLineDocumentId != 0 && mCachedCursorLineDocumentId != document.documentId();
	const bool wholeDocumentChanged = changeSet != nullptr && changeSet->changed && changeSet->touchedRange.start == 0 &&
	                                  changeSet->touchedRange.end >= std::max(changeSet->oldLength, changeSet->newLength);
	const bool preserveStaleMiniMapDuringEdit = changeSet != nullptr && changeSet->changed && useApproximateLargeFileMetrics();
	const bool pieceTableOnly = pieceTableOnlyPhaseActive();
	const bool miniMapEnabled = miniMapPipelineEnabled();

	if (documentChanged || wholeDocumentChanged) mLineNumberGutterLineCount = 0;
	mLastDocumentChangeSet = changeSet != nullptr ? *changeSet : MRTextBufferModel::DocumentChangeSet();
	cursorPos = std::min(cursorPos, document.length());
	selStart = std::min(selStart, document.length());
	selEnd = std::min(selEnd, document.length());
	if (selEnd < selStart) std::swap(selStart, selEnd);

	invalidateSaveNormalizationCache();
	const bool retainedDisplayWidthPrefix = changeSet != nullptr && prepareDisplayWidthWarmupForAppend(*changeSet);
	if (!retainedDisplayWidthPrefix) resetDisplayWidthWarmup();
	const bool preserveSyntaxCacheDuringEdit = changeSet != nullptr && changeSet->changed;
	resetSyntaxWarmupState(!preserveSyntaxCacheDuringEdit);
	if (preserveSyntaxCacheDuringEdit) invalidateSyntaxCacheFromLineStart(mBufferModel.lineStart(changeSet->touchedRange.start));
	static_cast<void>(cancelFoldWarmup());
	if (changeSet != nullptr && changeSet->changed) mFoldState.clearClosedFolds();
	invalidateFoldCache(changeSet != nullptr && changeSet->changed);
	if (!miniMapEnabled) {
		applyMiniMapSignals(mMiniMapState.renderer().invalidate(true, mBufferModel.documentId()));
	} else if (preserveStaleMiniMapDuringEdit) {
		for (std::uint64_t cancelledMiniMapTaskId = mMiniMapState.renderer().pendingWarmupTaskId(); cancelledMiniMapTaskId != 0;
		     cancelledMiniMapTaskId = mMiniMapState.renderer().pendingWarmupTaskId()) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledMiniMapTaskId));
			applyMiniMapSignals(mMiniMapState.renderer().clearWarmupTask(cancelledMiniMapTaskId));
		}
	}
	if (miniMapEnabled) applyMiniMapSignals(mMiniMapState.renderer().invalidate(false, mBufferModel.documentId()));
	refreshSyntaxContext();
	cursorPos = canonicalCursorOffset(cursorPos);
	selStart = canonicalCursorOffset(selStart);
	selEnd = canonicalCursorOffset(selEnd);
	mBufferModel.setCursorAndSelection(cursorPos, selStart, selEnd);
	syncDisplayedCursorColumnFromCursor(false);
	mBufferModel.setModified(modifiedState);
	if (changeSet == nullptr) {
		mFindMarkerRanges.clear();
		mMiniMapState.setFindRanges(mFindMarkerRanges);
	} else if (changeSet->changed)
		remapFindMarkerRangesForAppliedChange(*changeSet);
	if (changeSet != nullptr && changeSet->changed) remapDebuggerBreakpointLinesForAppliedChange(*changeSet);
	if (!modifiedState) clearDirtyRanges();
	else if (changeSet != nullptr && changeSet->changed) {
		remapDirtyRangesForAppliedChange(*changeSet);
		addDirtyRange(changeSet->touchedRange);
	}
	mSelectionAnchor = selStart;
	if (pieceTableOnly) {
		mSuppressLargeFileLineIndexWarmup = true;
		static_cast<void>(cancelLineIndexWarmup());
	} else
		mSuppressLargeFileLineIndexWarmup = false;
	drawLock++;
	updateMetrics();
	ensureCursorVisible(false);
	drawLock--;
	drawFlag = False;
	if (!pieceTableOnly && !mSuppressLargeFileLineIndexWarmup) scheduleLineIndexWarmupIfNeeded();
	if (!pieceTableOnly) scheduleDisplayWidthWarmupIfNeeded();
	if (syntaxPipelineEnabled()) scheduleSyntaxWarmupIfNeeded();
	updateIndicator();
	drawView();
	if (owner != nullptr) message(owner, evBroadcast, cmMrEditorDocumentCommitted, this);
	return true;
}

bool MRFileEditor::adoptCommittedDocument(const MRTextBufferModel::Document &document, std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState, const MRTextBufferModel::DocumentChangeSet *changeSet) {
	mBufferModel.document() = document;
	return syncAfterCommittedDocument(cursorPos, selStart, selEnd, modifiedState, changeSet);
}

bool MRFileEditor::adoptReadOnlyProjectionText(const std::shared_ptr<const std::string> &text, std::size_t expectedDocumentId, std::size_t expectedVersion) {
	MRTextBufferModel::CommitResult result = mBufferModel.adoptReadOnlyProjectionText(text, expectedDocumentId, expectedVersion);
	if (!result.applied()) return false;
	return syncAfterCommittedDocument(0, 0, 0, false, &result.change);
}

MRTextBufferModel::CommitResult MRFileEditor::applyStagedTransaction(const MRTextBufferModel::StagedTransaction &transaction, std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState) {
	pushUndoSnapshot();
	MRTextBufferModel::CommitResult result = mBufferModel.tryApplyStagedTransaction(transaction);
	if (result.applied()) {
		mBufferModel.updateUndoTopChangeSet(result.change);
		syncAfterCommittedDocument(cursorPos, selStart, selEnd, modifiedState, &result.change);
	}
	else
		mBufferModel.popUndoSnapshot();
	return result;
}
