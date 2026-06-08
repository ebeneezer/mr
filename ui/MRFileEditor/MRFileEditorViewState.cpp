#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"

namespace {

void normalizeScrollBarTrackGlyph(TScrollBar *scrollBar) noexcept {
	if (scrollBar == nullptr) return;
	scrollBar->chars[4] = scrollBar->chars[2];
}

bool scrollBarHasRange(const TScrollBar *scrollBar) noexcept {
	return scrollBar != nullptr && scrollBar->maxVal > scrollBar->minVal;
}

} // namespace

MRMiniMapRenderer::Palette MRFileEditor::resolveMiniMapPalette() {
	MRMiniMapRenderer::Palette palette;
	unsigned char configured = 0;
	const TColorAttr fallback = static_cast<TColorAttr>(getColor(0x0201));
	const bool fileComparePalette = mFileCompareGuttersConfigured;
	const unsigned char normalSlot = fileComparePalette ? kMrPaletteFileCompareMiniMapNormal : kMrPaletteMiniMapNormal;
	const unsigned char viewportSlot = fileComparePalette ? kMrPaletteFileCompareMiniMapViewport : kMrPaletteMiniMapViewport;
	const unsigned char changedSlot = fileComparePalette ? kMrPaletteFileCompareMiniMapChanged : kMrPaletteMiniMapChanged;
	const unsigned char findMarkerSlot = fileComparePalette ? kMrPaletteFileCompareMiniMapFindMarker : kMrPaletteMiniMapFindMarker;
	const unsigned char errorMarkerSlot = fileComparePalette ? kMrPaletteFileCompareMiniMapErrorMarker : kMrPaletteMiniMapErrorMarker;

	palette.normal = configuredColorSlotOverride(normalSlot, configured) ? static_cast<TColorAttr>(configured) : fallback;
	palette.viewport = configuredColorSlotOverride(viewportSlot, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	palette.changed = configuredColorSlotOverride(changedSlot, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	palette.findMarker = configuredColorSlotOverride(findMarkerSlot, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	palette.errorMarker = configuredColorSlotOverride(errorMarkerSlot, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	palette.warningMarker = configuredColorSlotOverride(kMrPaletteMessageWarning, configured) ? static_cast<TColorAttr>(configured) : palette.changed;
	palette.diffEqual = configuredColorSlotOverride(kMrPaletteFileCompareMiniMapEqual, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	palette.diffMissing = configuredColorSlotOverride(kMrPaletteFileCompareMiniMapMissing, configured) ? static_cast<TColorAttr>(configured) : palette.errorMarker;
	palette.diffInsert = configuredColorSlotOverride(kMrPaletteFileCompareMiniMapInsert, configured) ? static_cast<TColorAttr>(configured) : palette.warningMarker;
	palette.diffOffset = configuredColorSlotOverride(kMrPaletteFileCompareMiniMapOffset, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	return palette;
}

void MRFileEditor::refreshConfiguredVisualSettings() {
	syncDisplayedCursorColumnFromCursor(true);
	refreshSyntaxContext();
	invalidateFoldCache();
	syncIndicatorVisualSettings();
	updateMetrics();
	scheduleSyntaxWarmupIfNeeded();
	updateIndicator();
	drawView();
}

void MRFileEditor::revealCursor(Boolean centerCursor) {
	ensureCursorVisible(centerCursor == True);
	updateIndicator();
	drawView();
}

void MRFileEditor::refreshViewState() {
	updateIndicator();
	drawView();
}

void MRFileEditor::update(uchar) {
	refreshViewState();
}

void MRFileEditor::syncFromEditorState(bool) {
	syncDisplayedCursorColumnFromCursor(true);
	refreshSyntaxContext();
	updateMetrics();
	syncIndicatorVisualSettings();
	updateIndicator();
}

void MRFileEditor::syncIndicatorVisualSettings() {
	if (auto *mrIndicator = dynamic_cast<MRIndicator *>(mIndicator)) {
		MREditSetupSettings settings = configuredEditSetupSettings();
		if (mWordWrapSuppressed) settings.wordWrap = false;
		mrIndicator->setInsertMode(mInsertMode);
		mrIndicator->setWordWrap(settings.wordWrap);
	}
}

void MRFileEditor::notifyWindowTaskStateChanged() {
	if (owner != nullptr) message(owner, evBroadcast, cmUpdateTitle, 0);
}


void MRFileEditor::clearLineIndexWarmupTask(std::uint64_t expectedTaskId) noexcept {
	if (expectedTaskId != 0 && mLineIndexWarmupTaskId != expectedTaskId) return;
	if (mLineIndexWarmupTaskId == 0) return;
	mLineIndexWarmupTaskId = 0;
	mLineIndexWarmupDocumentId = 0;
	mLineIndexWarmupVersion = 0;
	notifyWindowTaskStateChanged();
}

void MRFileEditor::clearSyntaxWarmupTask(std::uint64_t expectedTaskId) noexcept {
	MRSyntaxDerivedState::WarmupState &warmupState = mSyntaxState.warmupState();
	if (expectedTaskId != 0 && warmupState.taskId != expectedTaskId) return;
	if (warmupState.taskId == 0) return;
	warmupState = MRSyntaxDerivedState::WarmupState();
	notifyWindowTaskStateChanged();
}

void MRFileEditor::clearMiniMapWarmupTask(std::uint64_t expectedTaskId) noexcept {
	applyMiniMapSignals(mMiniMapState.renderer().clearWarmupTask(expectedTaskId));
}

void MRFileEditor::applyMiniMapSignals(const MRMiniMapRenderer::Signals &signals) {
	if (signals.notifyTaskStateChanged) notifyWindowTaskStateChanged();
	if (signals.redraw) drawView();
}

void MRFileEditor::clearSaveNormalizationWarmupTask(std::uint64_t expectedTaskId) noexcept {
	if (expectedTaskId != 0 && mSaveNormalizationWarmupTaskId != expectedTaskId) return;
	if (mSaveNormalizationWarmupTaskId == 0) return;
	mSaveNormalizationWarmupTaskId = 0;
	mSaveNormalizationWarmupDocumentId = 0;
	mSaveNormalizationWarmupVersion = 0;
	mSaveNormalizationWarmupOptionsHash = 0;
	mSaveNormalizationWarmupSourceBytes = 0;
	mSaveNormalizationWarmupStartedAt = std::chrono::steady_clock::time_point();
	notifyWindowTaskStateChanged();
}

void MRFileEditor::setSyntaxTitleHint(const std::string &title) {
	mSyntaxTitleHint = title;
	refreshSyntaxContext();
	updateMetrics();
	updateIndicator();
}

TPalette &MRFileEditor::getPalette() const {
	// 1..2: scroller text/selected text (window slots 6/7)
	// 3..5: editor-only highlight slots (window-local palette slots 9..11)
	// mapped to app palette extension 136..138.
	// 6: line number gutter (window-local slot 12, app slot 142).
	static TPalette palette("\x06\x07\x09\x0A\x0B\x0C", 6);
	return palette;
}

int MRFileEditor::configuredTabSize() noexcept {
	int tabSize = configuredTabSizeSetting();
	if (tabSize < 1) tabSize = 1;
	if (tabSize > 32) tabSize = 32;
	return tabSize;
}

bool MRFileEditor::configuredDisplayTabs() noexcept {
	return configuredDisplayTabsSetting();
}

bool MRFileEditor::configuredFormatRuler() noexcept {
	return configuredEditSetupSettings().formatRuler;
}

int MRFileEditor::tabDisplayWidth(const MREditSetupSettings &settings, int visualColumn) noexcept {
	const int currentColumn = std::max(1, visualColumn + 1);
	const int targetColumn = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);
	return std::max(1, targetColumn - currentColumn);
}


int MRFileEditor::visibleTextRows() const noexcept {
	return std::max(0, size.y - (configuredFormatRuler() ? 1 : 0));
}

void MRFileEditor::syncScrollBarsToState() noexcept {
	normalizeScrollBarTrackGlyph(hScrollBar);
	normalizeScrollBarTrackGlyph(vScrollBar);
	bool showBase = mScrollBarsAlwaysVisible || (state & (sfActive | sfSelected)) != 0;
	const bool showWithoutRange = configuredScrollbarVisibility() == MRScrollbarVisibility::Always;
	MREditWindow *window = dynamic_cast<MREditWindow *>(owner);
	if (window != nullptr && window->isMinimized()) showBase = false;
	if (hScrollBar != nullptr) {
		if (showBase && (showWithoutRange || scrollBarHasRange(hScrollBar))) hScrollBar->show();
		else
			hScrollBar->hide();
	}
	if (vScrollBar != nullptr) {
		if (showBase && (showWithoutRange || scrollBarHasRange(vScrollBar))) vScrollBar->show();
		else
			vScrollBar->hide();
	}
}

int MRFileEditor::decimalDigits(std::size_t value) noexcept {
	int digits = 1;
	while (value >= 10) {
		value /= 10;
		++digits;
	}
	return digits;
}

void MRFileEditor::setCommunicationViewerMode(bool enabled, bool lineNumbers) {
	if (mCommunicationViewerMode == enabled && mCommunicationViewerLineNumbers == lineNumbers) return;
	mCommunicationViewerMode = enabled;
	mCommunicationViewerLineNumbers = lineNumbers;
	refreshSyntaxContext();
	refreshViewState();
}

void MRFileEditor::setCommunicationViewerOptions(bool lineNumbers) {
	setCommunicationViewerMode(true, lineNumbers);
}

void MRFileEditor::setMiniMapSuppressed(bool suppressed) noexcept {
	if (mMiniMapSuppressed == suppressed) return;
	mMiniMapSuppressed = suppressed;
	refreshViewState();
}

void MRFileEditor::setWordWrapSuppressed(bool suppressed) noexcept {
	if (mWordWrapSuppressed == suppressed) return;
	mWordWrapSuppressed = suppressed;
	syncIndicatorVisualSettings();
	refreshViewState();
}

void MRFileEditor::setScrollBarsAlwaysVisible(bool visible) noexcept {
	if (mScrollBarsAlwaysVisible == visible) return;
	mScrollBarsAlwaysVisible = visible;
	syncScrollBarsToState();
	refreshViewState();
}

void MRFileEditor::setFileCompareLineKinds(const std::vector<unsigned char> &lineKinds) {
	mFileCompareLineKinds = lineKinds;
	mMiniMapState.clearOverlayCache();
	refreshViewState();
}

void MRFileEditor::clearFileCompareLineKinds() {
	if (mFileCompareLineKinds.empty()) return;
	mFileCompareLineKinds.clear();
	mMiniMapState.clearOverlayCache();
	refreshViewState();
}

void MRFileEditor::setFileCompareGutters(const std::string &leftGutters, const std::string &rightGutters) {
	if (mFileCompareGuttersConfigured && mFileCompareLeftGutters == leftGutters && mFileCompareRightGutters == rightGutters) return;
	mFileCompareGuttersConfigured = true;
	mFileCompareLeftGutters = leftGutters;
	mFileCompareRightGutters = rightGutters;
	refreshViewState();
}

void MRFileEditor::setFileCompareGutterVisible(bool visible) noexcept {
	if (mFileCompareGutterVisible == visible) return;
	mFileCompareGutterVisible = visible;
	refreshViewState();
}
