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
	TColorAttr configured;
	const TColorAttr fallback = static_cast<TColorAttr>(getColor(0x0201));
	const bool fileComparePalette = mFileCompareGuttersConfigured;
	const unsigned char normalSlot = fileComparePalette ? kMrPaletteFileCompareMiniMapNormal : kMrPaletteMiniMapNormal;
	const unsigned char viewportSlot = fileComparePalette ? kMrPaletteFileCompareMiniMapViewport : kMrPaletteMiniMapViewport;
	const unsigned char changedSlot = fileComparePalette ? kMrPaletteFileCompareMiniMapChanged : kMrPaletteMiniMapChanged;
	const unsigned char findMarkerSlot = fileComparePalette ? kMrPaletteFileCompareMiniMapFindMarker : kMrPaletteMiniMapFindMarker;
	const unsigned char errorMarkerSlot = fileComparePalette ? kMrPaletteFileCompareMiniMapErrorMarker : kMrPaletteMiniMapErrorMarker;

	palette.normal = configuredColorSlotOverride(normalSlot, configured) ? configured : fallback;
	palette.viewport = configuredColorSlotOverride(viewportSlot, configured) ? configured : palette.normal;
	palette.changed = configuredColorSlotOverride(changedSlot, configured) ? configured : palette.normal;
	palette.findMarker = configuredColorSlotOverride(findMarkerSlot, configured) ? configured : palette.normal;
	palette.errorMarker = configuredColorSlotOverride(errorMarkerSlot, configured) ? configured : palette.normal;
	palette.warningMarker = configuredColorSlotOverride(kMrPaletteMessageWarning, configured) ? configured : palette.changed;
	palette.diffEqual = configuredColorSlotOverride(kMrPaletteFileCompareMiniMapEqual, configured) ? configured : palette.normal;
	palette.diffMissing = configuredColorSlotOverride(kMrPaletteFileCompareMiniMapMissing, configured) ? configured : palette.errorMarker;
	palette.diffInsert = configuredColorSlotOverride(kMrPaletteFileCompareMiniMapInsert, configured) ? configured : palette.warningMarker;
	palette.diffOffset = configuredColorSlotOverride(kMrPaletteFileCompareMiniMapOffset, configured) ? configured : palette.normal;
	return palette;
}

void MRFileEditor::refreshConfiguredVisualSettings() {
	refreshEditorSettingsSnapshot();
	if (auto *mrIndicator = dynamic_cast<MRIndicator *>(mIndicator)) mrIndicator->setCursorPositionMarkerFormat(configuredCursorPositionMarker());
	syncDisplayedCursorColumnFromCursor(true);
	refreshSyntaxContext();
	static_cast<void>(cancelViewportFoldWarmup());
	invalidateFoldCache();
	syncIndicatorVisualSettings();
	scheduleDisplayWidthWarmupIfNeeded();
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

void MRFileEditor::requestDocumentLineNavigation(std::size_t lineIndex) {
	mPendingDocumentLineNavigationState.active = true;
	mPendingDocumentLineNavigationState.documentId = mBufferModel.documentId();
	mPendingDocumentLineNavigationState.version = mBufferModel.version();
	mPendingDocumentLineNavigationState.targetLine = lineIndex;
	mPendingDocumentLineNavigationState.cursorOffset = mBufferModel.cursor();
	mPendingDocumentLineNavigationState.selectionStart = mBufferModel.selectionStart();
	mPendingDocumentLineNavigationState.selectionEnd = mBufferModel.selectionEnd();

	if (!continuePendingDocumentLineNavigation()) scheduleLineIndexWarmupIfNeeded();
}

void MRFileEditor::centerDocumentLocationInView(std::size_t lineIndex, int visualColumn) {
	const std::size_t lineCount = mBufferModel.exactLineCountKnown() ? std::max<std::size_t>(1, mBufferModel.lineCount()) : std::max<std::size_t>(1, mBufferModel.estimatedLineCount());
	const std::size_t documentLine = std::min(lineIndex, lineCount - 1);
	const std::size_t visibleLine = visibleLineForDocumentLine(documentLine);
	const int textRows = std::max(1, visibleTextRows());
	const int viewportWidth = std::max(1, textViewportWidth());
	const int clampedColumn = std::max(0, visualColumn);
	const int targetX = clampedColumn > viewportWidth / 2 ? clampedColumn - viewportWidth / 2 : 0;
	const int targetY = visibleLine > static_cast<std::size_t>(textRows / 2) ? static_cast<int>(std::min<std::size_t>(visibleLine - static_cast<std::size_t>(textRows / 2), static_cast<std::size_t>(INT_MAX))) : 0;

	scrollTo(targetX, targetY);
	scheduleSyntaxWarmupIfNeeded();
	updateIndicator();
	drawView();
}

void MRFileEditor::moveCursorToDocumentLineTop(std::size_t lineIndex, int visualColumn) {
	const std::size_t lineCount = mBufferModel.exactLineCountKnown() ? std::max<std::size_t>(1, mBufferModel.lineCount()) : std::max<std::size_t>(1, mBufferModel.estimatedLineCount());
	const std::size_t documentLine = std::min(lineIndex, lineCount - 1);
	const std::size_t visibleLine = visibleLineForDocumentLine(documentLine);
	const int targetLine = static_cast<int>(std::min<std::size_t>(visibleLine, static_cast<std::size_t>(INT_MAX)));
	const std::size_t lineStart = mBufferModel.lineStartByIndex(documentLine);
	const std::size_t targetOffset = charPtrOffset(lineStart, std::max(0, visualColumn));

	scrollTo(std::max(0, delta.x), std::max(0, targetLine));
	moveCursor(targetOffset, false, false, std::max(0, visualColumn));
}

void MRFileEditor::restoreCursorViewState(std::size_t lineIndex, int visualColumn) {
	const std::size_t lineCount = mBufferModel.exactLineCountKnown() ? std::max<std::size_t>(1, mBufferModel.lineCount()) : std::max<std::size_t>(1, mBufferModel.estimatedLineCount());
	const std::size_t documentLine = std::min(lineIndex, lineCount - 1);
	const std::size_t visibleLine = visibleLineForDocumentLine(documentLine);
	const int targetLine = static_cast<int>(std::min<std::size_t>(visibleLine, static_cast<std::size_t>(INT_MAX)));
	const int targetColumn = std::max(0, visualColumn);
	const std::size_t lineStart = mBufferModel.lineStartByIndex(documentLine);
	const std::size_t targetOffset = charPtrOffset(lineStart, targetColumn);

	delta.x = std::max(0, delta.x);
	delta.y = std::max(0, targetLine);
	mBufferModel.setCursorAndSelection(targetOffset, targetOffset, targetOffset);
	mSelectionAnchor = targetOffset;
	mCursorVisualLine = cachedCursorLineIndex();
	mCursorVisualColumn = actualCursorVisualColumn(targetOffset);
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
	scheduleDisplayWidthWarmupIfNeeded();
	updateMetrics();
	syncIndicatorVisualSettings();
	updateIndicator();
}

void MRFileEditor::syncIndicatorVisualSettings() {
	if (auto *mrIndicator = dynamic_cast<MRIndicator *>(mIndicator)) {
		MREditSetupSettings settings = effectiveEditSetupSettings();
		if (mWordWrapSuppressed) settings.wordWrap = false;
		mrIndicator->setInsertMode(mInsertMode);
		mrIndicator->setWordWrap(settings.wordWrap);
	}
}

void MRFileEditor::notifyWindowTaskStateChanged() {
	if (owner != nullptr) message(owner, evBroadcast, cmUpdateTitle, 0);
}


void MRFileEditor::clearMiniMapWarmupTask(std::uint64_t expectedTaskId) noexcept {
	applyMiniMapSignals(mMiniMapState.renderer().clearWarmupTask(expectedTaskId));
}

void MRFileEditor::applyMiniMapSignals(const MRMiniMapRenderer::Signals &signals) {
	if (signals.notifyTaskStateChanged) notifyWindowTaskStateChanged();
	if (signals.redraw) drawView();
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

void MRFileEditor::refreshEditorSettingsSnapshot() {
	MREditSetupSettings settings = configuredEditSetupSettings();

	if (hasPersistentFileName()) {
		MREditSetupSettings effective;
		if (effectiveEditSetupSettingsForPath(fileName, effective, nullptr)) settings = effective;
	}
	mEffectiveEditSettings = settings;
	mCursorBehaviour = configuredCursorBehaviour();
	mScrollbarVisibility = configuredScrollbarVisibility();
}

const MREditSetupSettings &MRFileEditor::effectiveEditSetupSettings() const noexcept {
	return mEffectiveEditSettings;
}

bool MRFileEditor::configuredDisplayTabs() const {
	return effectiveEditSetupSettings().displayTabs;
}

bool MRFileEditor::configuredFormatRuler() const {
	return effectiveEditSetupSettings().formatRuler;
}

int MRFileEditor::tabDisplayWidth(const MREditSetupSettings &settings, int visualColumn) noexcept {
	const int currentColumn = std::max(1, visualColumn + 1);
	const int targetColumn = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);
	return std::max(1, targetColumn - currentColumn);
}


int MRFileEditor::visibleTextRows() const noexcept {
	return std::max(0, size.y - (configuredFormatRuler() ? 1 : 0));
}

void MRFileEditor::updateMetrics() {
	const auto startedAt = std::chrono::steady_clock::now();
	int limitX = provisionalDisplayWidthLimit();
	int limitY = 1;
	TextViewportGeometry viewport = textViewportGeometry();
	int gutterWidth = viewport.gutterWidth;
	int rightInset = viewport.rightInset;
	int viewportWidth = viewport.width;
	const int textRows = std::max(1, visibleTextRows());
	const bool approximateMetrics = useApproximateLargeFileMetrics() || !mBufferModel.exactLineCountKnown();

	if (!displayWidthLimitExact() && approximateMetrics) limitX = std::max(dynamicLargeFileWidthLimit(), limitX);
	if (approximateMetrics) {
		const auto lineLimitStartedAt = std::chrono::steady_clock::now();
		limitY = dynamicLargeFileLineLimit();
		if (shouldTraceLargeFileWarmupDiagnostics()) {
			const auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
			const auto lineLimitUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - lineLimitStartedAt).count();
			mLastUiHotpathTrace = "metrics_us=" + std::to_string(totalUs) + " line_limit_us=" + std::to_string(lineLimitUs) + " cursor_line=" + std::to_string(cachedCursorLineIndex()) +
			                      " est_lines=" + std::to_string(mBufferModel.estimatedLineCount()) + " limitY=" + std::to_string(limitY);
		}
	} else {
		limitY = foldingPipelineEnabled() ? std::max<int>(1, static_cast<int>(foldedVisibleLineCount())) : std::max<int>(1, static_cast<int>(mBufferModel.lineCount()));
		mLastUiHotpathTrace.clear();
	}
	limitX = std::max(limitX, displayedCursorColumn() + 1);
	limitY = std::max<int>(limitY, static_cast<int>(visibleLineForDocumentLine(displayedCursorLineIndex())) + 1);

	int maxX = std::max(0, limitX - viewportWidth);
	int maxY = std::max(0, limitY - textRows);
	int newDeltaX = std::min(std::max(delta.x, 0), maxX);
	int newDeltaY = std::min(std::max(delta.y, 0), maxY);

	setLimit(limitX + gutterWidth + rightInset, limitY + viewport.topInset);
	if (newDeltaX != delta.x || newDeltaY != delta.y) scrollTo(newDeltaX, newDeltaY);
	syncScrollBarsToState();
}

void MRFileEditor::syncScrollBarsToState() noexcept {
	normalizeScrollBarTrackGlyph(hScrollBar);
	normalizeScrollBarTrackGlyph(vScrollBar);
	bool showBase = mScrollBarsAlwaysVisible || (state & (sfActive | sfSelected)) != 0;
	const bool showWithoutRange = mScrollbarVisibility == MRScrollbarVisibility::Always;
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

void MRFileEditor::setCommunicationViewerMode(bool enabled, bool lineNumbers, MRLiveLogScrollDirection scrollDirection) {
	if (mCommunicationViewerMode == enabled && mCommunicationViewerLineNumbers == lineNumbers && mCommunicationViewerScrollDirection == scrollDirection) return;
	mCommunicationViewerMode = enabled;
	mCommunicationViewerLineNumbers = lineNumbers;
	mCommunicationViewerScrollDirection = scrollDirection;
	refreshSyntaxContext();
	refreshViewState();
}

void MRFileEditor::setCommunicationViewerOptions(bool lineNumbers) {
	setCommunicationViewerOptions(lineNumbers, MRLiveLogScrollDirection::Down);
}

void MRFileEditor::setCommunicationViewerOptions(bool lineNumbers, MRLiveLogScrollDirection scrollDirection) {
	setCommunicationViewerMode(true, lineNumbers, scrollDirection);
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

void MRFileEditor::adoptFileCompareLineKinds(const std::shared_ptr<const std::vector<unsigned char>> &lineKinds,
	                                          const std::shared_ptr<const std::vector<MRFileCompareMiniMapSlice>> &miniMapSlices) {
	const std::shared_ptr<const std::vector<unsigned char>> nextLineKinds =
		lineKinds != nullptr ? lineKinds : std::make_shared<const std::vector<unsigned char>>();
	const std::shared_ptr<const std::vector<MRFileCompareMiniMapSlice>> nextMiniMapSlices =
		miniMapSlices != nullptr ? miniMapSlices : std::make_shared<const std::vector<MRFileCompareMiniMapSlice>>();
	if (mFileCompareLineKinds == nextLineKinds && mFileCompareMiniMapSlices == nextMiniMapSlices) return;
	mFileCompareLineKinds = nextLineKinds;
	mFileCompareMiniMapSlices = nextMiniMapSlices;
	mMiniMapState.adoptFileCompareRanges(mFileCompareLineKinds, mFileCompareMiniMapSlices);
	refreshViewState();
}

void MRFileEditor::clearFileCompareLineKinds() {
	if (mFileCompareLineKinds->empty() && mFileCompareMiniMapSlices->empty()) return;
	mFileCompareLineKinds = std::make_shared<const std::vector<unsigned char>>();
	mFileCompareMiniMapSlices = std::make_shared<const std::vector<MRFileCompareMiniMapSlice>>();
	mMiniMapState.adoptFileCompareRanges(mFileCompareLineKinds, mFileCompareMiniMapSlices);
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
