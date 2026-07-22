#include "MRFileEditor.hpp"

void MRFileEditor::continueComputeWarmupIfNeeded(const char *reason) {
	static_cast<void>(reason);
	const bool pieceTableOnly = pieceTableOnlyPhaseActive();
	const bool foldingEnabled = foldingPipelineEnabled();
	const bool miniMapEnabled = miniMapPipelineEnabled();
	if (pieceTableOnly) {
		static_cast<void>(cancelLineIndexWarmup());
		if (!foldingEnabled) static_cast<void>(cancelFoldWarmup());
		if (!miniMapEnabled) applyMiniMapSignals(mMiniMapState.renderer().invalidate(true, mBufferModel.documentId()));
	}
	if (!pieceTableOnly) {
		scheduleLineIndexWarmupIfNeeded();
		scheduleDisplayWidthWarmupIfNeeded();
	}
	continueCanonicalFoldContextIfNeeded();
	continueDocumentFoldLevelOperationIfNeeded();
	if (!syntaxPipelineEnabled()) {
		resetSyntaxWarmupState(true);
		return;
	}
	scheduleSyntaxWarmupIfNeeded();
}

bool MRFileEditor::applyMiniMapWarmup(const mr::coprocessor::Payload &payload, const mr::coprocessor::Result &taskResult) {
	MRMiniMapRenderer::ApplyWarmupResult applyResult = mMiniMapState.renderer().applyWarmup(payload, taskResult, mBufferModel.documentId(), mBufferModel.version());
	applyMiniMapSignals(applyResult.signals);
	return applyResult.applied;
}

void MRFileEditor::refreshSyntaxContext() {
	MRSyntaxLanguage oldLanguage = mBufferModel.language();
	const bool oldAutomatic = mBufferModel.languageAutomatic();
	const std::string codeLanguage = effectiveCodeLanguageSetting();
	mBufferModel.setSyntaxContext(hasPersistentFileName() ? fileName : "", mSyntaxTitleHint, codeLanguage);
	if (mBufferModel.language() != oldLanguage) resetSyntaxWarmupState(true);
	if (mBufferModel.language() != oldLanguage) {
		static_cast<void>(cancelFoldWarmup());
		mFoldState.clearClosedFolds();
		invalidateFoldCache();
	}
	if (mBufferModel.languageAutomatic() != oldAutomatic) drawView();
}

bool MRFileEditor::pieceTableOnlyPhaseActive() const noexcept {
	return false;
}

std::string MRFileEditor::effectiveCodeLanguageSetting() const {
	std::string codeLanguage = effectiveEditSetupSettings().codeLanguage;

	if (mCommunicationViewerMode) return "NONE";
	return upperAscii(trimAscii(codeLanguage));
}

bool MRFileEditor::languageFeaturesEnabled() const {
	const std::string codeLanguage = effectiveCodeLanguageSetting();

	if (mCommunicationViewerMode) return false;
	if (codeLanguage.empty() || codeLanguage == "NONE") return false;
	return mBufferModel.language() != MRSyntaxLanguage::PlainText;
}

bool MRFileEditor::syntaxPipelineEnabled() const {
	return languageFeaturesEnabled() && effectiveEditSetupSettings().codeColoring;
}

bool MRFileEditor::foldingPipelineEnabled() const {
	return languageFeaturesEnabled() && effectiveEditSetupSettings().codeFolding;
}

bool MRFileEditor::miniMapPipelineEnabled() const noexcept {
	return !mCommunicationViewerMode && !mMiniMapSuppressed;
}

void MRFileEditor::resetSyntaxWarmupState(bool clearCache) noexcept {
	static_cast<void>(cancelSyntaxWarmup());
	mSyntaxState.resetState(clearCache);
}

void MRFileEditor::invalidateSyntaxCacheFromLineStart(std::size_t lineStart) noexcept {
	std::map<std::size_t, MRSyntaxCacheEntry> &tokenCache = mSyntaxState.tokenCache();
	std::map<std::size_t, MRSyntaxCheckpointEntry> &checkpoints = mSyntaxState.checkpoints();
	std::map<std::size_t, MRSyntaxCacheEntry>::iterator firstInvalid = tokenCache.lower_bound(lineStart);
	std::size_t lineIndex = mBufferModel.lineIndex(lineStart);
	std::map<std::size_t, MRSyntaxCheckpointEntry>::iterator firstInvalidCheckpoint = checkpoints.lower_bound(lineIndex);

	if (firstInvalid != tokenCache.end()) tokenCache.erase(firstInvalid, tokenCache.end());
	if (firstInvalidCheckpoint != checkpoints.end()) checkpoints.erase(firstInvalidCheckpoint, checkpoints.end());
	invalidateSyntaxWarmedLineRangesFrom(lineIndex);
	static_cast<void>(cancelSyntaxWarmup());
}

void MRFileEditor::clearSyntaxWarmedLineRanges() noexcept {
	mSyntaxState.clearWarmedLineRanges();
}

void MRFileEditor::rememberSyntaxWarmedLineRange(std::size_t startLine, std::size_t endLine) noexcept {
	mSyntaxState.rememberWarmedLineRange(mBufferModel.documentId(), mBufferModel.language(), startLine, endLine);
}

void MRFileEditor::invalidateSyntaxWarmedLineRangesFrom(std::size_t lineIndex) noexcept {
	mSyntaxState.invalidateWarmedLineRangesFrom(mBufferModel.documentId(), mBufferModel.language(), lineIndex);
}

bool MRFileEditor::syntaxWarmedLineRangeCovered(std::size_t startLine, std::size_t endLine) const noexcept {
	return mSyntaxState.warmedLineRangeCovered(mBufferModel.documentId(), mBufferModel.language(), startLine, endLine);
}
