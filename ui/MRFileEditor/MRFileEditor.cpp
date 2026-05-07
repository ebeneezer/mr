#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"

namespace {

bool isIndentWhitespace(char ch) noexcept {
	return ch == ' ' || ch == '\t';
}

bool isStatefulSyntaxLanguage(MRSyntaxLanguage language) noexcept {
	return language == MRSyntaxLanguage::MRMAC || language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp || language == MRSyntaxLanguage::JavaScript || language == MRSyntaxLanguage::Python ||
	       language == MRSyntaxLanguage::Markdown || language == MRSyntaxLanguage::Zsh || language == MRSyntaxLanguage::Perl;
}

std::string_view trimView(std::string_view text) noexcept {
	std::size_t start = 0;
	std::size_t end = text.size();

	while (start < end && isIndentWhitespace(text[start]))
		++start;
	while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
		--end;
	return text.substr(start, end - start);
}

std::size_t lastSignificantByte(std::string_view text) noexcept {
	std::size_t end = text.size();

	while (end > 0) {
		const char ch = text[end - 1];
		if (ch != ' ' && ch != '\t' && ch != '\r') return end - 1;
		--end;
	}
	return std::string_view::npos;
}

bool startsWithCloser(std::string_view trimmed) noexcept {
	return !trimmed.empty() && (trimmed.front() == '}' || trimmed.front() == ']' || trimmed.front() == ')');
}

std::size_t leadingIndentBytes(std::string_view text) noexcept {
	std::size_t index = 0;
	while (index < text.size() && isIndentWhitespace(text[index]))
		++index;
	return index;
}

bool isPythonIndentLead(std::string_view upperLine) noexcept {
	return upperLine == "ELSE:" || upperLine == "TRY:" || upperLine == "FINALLY:" || upperLine.starts_with("IF ") || upperLine.starts_with("ELIF ") || upperLine.starts_with("FOR ") || upperLine.starts_with("WHILE ") ||
	       upperLine.starts_with("WITH ") || upperLine.starts_with("MATCH ") || upperLine.starts_with("CASE ") || upperLine.starts_with("EXCEPT ") || upperLine.starts_with("DEF ") || upperLine.starts_with("CLASS ") ||
	       upperLine.starts_with("ASYNC DEF ") || upperLine.starts_with("ASYNC FOR ") || upperLine.starts_with("ASYNC WITH ");
}

bool isPythonDedentLead(std::string_view upperLine) noexcept {
	return upperLine == "ELSE:" || upperLine == "FINALLY:" || upperLine == "EXCEPT:" || upperLine.starts_with("ELIF ") || upperLine.starts_with("CASE ") || upperLine.starts_with("EXCEPT ");
}

bool isShellIndentLead(std::string_view trimmed, std::string_view upperLine) noexcept {
	const std::size_t last = lastSignificantByte(trimmed);
	if (last != std::string_view::npos && trimmed[last] == '{') return true;
	return upperLine == "THEN" || upperLine.ends_with(" THEN") || upperLine == "DO" || upperLine.ends_with(" DO") || upperLine == "ELSE" || upperLine.starts_with("ELIF ") ||
	       (upperLine.starts_with("CASE ") && upperLine.ends_with(" IN"));
}

bool isShellDedentLead(std::string_view trimmed, std::string_view upperLine) noexcept {
	return startsWithCloser(trimmed) || upperLine == "FI" || upperLine == "DONE" || upperLine == "ESAC" || upperLine == "ELSE" || upperLine.starts_with("ELIF ");
}

bool isPerlIndentLead(std::string_view trimmed) noexcept {
	const std::size_t last = lastSignificantByte(trimmed);
	return last != std::string_view::npos && (trimmed[last] == '{' || trimmed[last] == '[' || trimmed[last] == '(');
}

bool markdownContinuationColumn(std::string_view line, int &targetColumn) noexcept {
	const std::size_t indent = leadingIndentBytes(line);
	const std::string_view trimmed = trimView(line);
	if (trimmed.empty()) return false;
	if (trimmed.front() == '>') {
		std::size_t marker = indent;
		while (marker < line.size() && line[marker] == '>')
			++marker;
		while (marker < line.size() && line[marker] == ' ')
			++marker;
		targetColumn = static_cast<int>(marker) + 1;
		return true;
	}
	if ((trimmed.front() == '-' || trimmed.front() == '*' || trimmed.front() == '+') && trimmed.size() > 1 && std::isspace(static_cast<unsigned char>(trimmed[1])) != 0) {
		std::size_t marker = indent + 2;
		if (trimmed.size() >= 5 && line.size() >= indent + 5 && line[indent + 2] == '[' && line[indent + 4] == ']') marker = indent + 6;
		targetColumn = static_cast<int>(marker) + 1;
		return true;
	}
	if (std::isdigit(static_cast<unsigned char>(trimmed.front())) != 0) {
		std::size_t marker = indent;
		while (marker < line.size() && std::isdigit(static_cast<unsigned char>(line[marker])) != 0)
			++marker;
		if (marker < line.size() && (line[marker] == '.' || line[marker] == ')')) {
			++marker;
			while (marker < line.size() && line[marker] == ' ')
				++marker;
			targetColumn = static_cast<int>(marker) + 1;
			return true;
		}
	}
	return false;
}

bool isMarkdownFenceLine(std::string_view trimmed) noexcept {
	if (trimmed.size() < 3) return false;
	const char marker = trimmed.front();
	if (marker != '`' && marker != '~') return false;
	std::size_t runLength = 0;
	while (runLength < trimmed.size() && trimmed[runLength] == marker)
		++runLength;
	return runLength >= 3;
}

bool isMarkdownSetextUnderline(std::string_view trimmed) noexcept {
	if (trimmed.size() < 3) return false;
	const char marker = trimmed.front();
	if (marker != '=' && marker != '-') return false;
	for (char ch : trimmed)
		if (ch != marker && ch != ' ' && ch != '\t') return false;
	return true;
}

bool isMakeTargetLine(std::string_view trimmed) noexcept {
	const std::size_t colon = trimmed.find(':');
	const std::size_t eq = trimmed.find('=');
	return colon != std::string_view::npos && colon > 0 && (eq == std::string_view::npos || colon < eq);
}

bool isPreprocessorFoldStart(std::string_view trimmed) noexcept {
	return trimmed.starts_with("#if") || trimmed.starts_with("#ifdef") || trimmed.starts_with("#ifndef");
}

bool isPreprocessorFoldEnd(std::string_view trimmed) noexcept {
	return trimmed.starts_with("#endif") || trimmed.starts_with("#else") || trimmed.starts_with("#elif");
}

} // namespace

MRFileEditor::LoadTiming::LoadTiming() noexcept : valid(false), bytes(0), lines(0), mappedLoadMs(0.0), lineCountMs(0.0) {
}

MRFileEditor::MRFileEditor(const TRect &bounds, TScrollBar *aHScrollBar, TScrollBar *aVScrollBar, TIndicator *aIndicator, TStringView aFileName) noexcept
    : TScroller(bounds, aHScrollBar, aVScrollBar), mIndicator(aIndicator), mReadOnly(false), mInsertMode(true), mAutoIndent(false), mSyntaxTitleHint(), mBufferModel(), mSelectionAnchor(0), mCursorVisualColumn(0), mIndicatorUpdateInProgress(false), mLineIndexWarmupTaskId(0), mLineIndexWarmupDocumentId(0), mLineIndexWarmupVersion(0), mSyntaxTokenCache(), mSyntaxCheckpoints(), mSyntaxWarmupTaskId(0), mSyntaxWarmupDocumentId(0), mSyntaxWarmupVersion(0), mSyntaxWarmupTopLine(0), mSyntaxWarmupBottomLine(0), mSyntaxWarmupLanguage(MRSyntaxLanguage::PlainText), mSyntaxPrefetchDocumentId(0), mSyntaxPrefetchVersion(0), mSyntaxPrefetchTargetBottomLine(0), mSyntaxPrefetchReachedBottomLine(0), mSyntaxPrefetchLanguage(MRSyntaxLanguage::PlainText), mMiniMapRenderer(), mSaveNormalizationCache(), mSaveNormalizationWarmupTaskId(0), mSaveNormalizationWarmupDocumentId(0), mSaveNormalizationWarmupVersion(0),
      mSaveNormalizationWarmupOptionsHash(0), mSaveNormalizationWarmupSourceBytes(0), mSaveNormalizationWarmupStartedAt(std::chrono::steady_clock::time_point()), mSaveNormalizationThroughputBytesPerMicro(0.0), mSaveNormalizationThroughputSamples(0), mMiniMapInitialRenderReportedDocumentId(0), mBlockOverlayActive(false), mBlockOverlayMode(0), mBlockOverlayAnchor(0), mBlockOverlayEnd(0), mBlockOverlayTrackingCursor(false), mPreferredIndentColumn(1), mLastLoadTiming(), mLargeFileMetricsTraceValid(false), mLastLargeFileMetricsExactKnown(false), mLastLargeFileMetricsLimitY(0), mLastLargeFileMetricsMaxY(0), mLastLargeFileMetricsDeltaY(0), mLastLargeFileMetricsNewDeltaY(0) {
	fileName[0] = EOS;
	options |= ofFirstClick;
	eventMask |= evMouse | evKeyboard | evCommand;
	if (!aFileName.empty()) setPersistentFileName(aFileName);
	syncFromEditorState(false);
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
		mBufferModel.document().flatten();
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
	return mSyntaxWarmupTaskId;
}

std::uint64_t MRFileEditor::pendingMiniMapWarmupTaskId() const noexcept {
	return mMiniMapRenderer.pendingWarmupTaskId();
}

std::uint64_t MRFileEditor::pendingSaveNormalizationWarmupTaskId() const noexcept {
	return mSaveNormalizationWarmupTaskId;
}

std::size_t MRFileEditor::syntaxWarmupTopLine() const noexcept {
	return mSyntaxWarmupTopLine;
}

std::size_t MRFileEditor::syntaxWarmupBottomLine() const noexcept {
	return mSyntaxWarmupBottomLine;
}

std::size_t MRFileEditor::syntaxPrefetchTargetBottomLine() const noexcept {
	return mSyntaxPrefetchTargetBottomLine;
}

std::size_t MRFileEditor::syntaxPrefetchReachedBottomLine() const noexcept {
	return mSyntaxPrefetchReachedBottomLine;
}

bool MRFileEditor::shouldReportMiniMapInitialRender() const noexcept {
	return mMiniMapInitialRenderReportedDocumentId != mBufferModel.documentId();
}

void MRFileEditor::markMiniMapInitialRenderReported() noexcept {
	mMiniMapInitialRenderReportedDocumentId = mBufferModel.documentId();
}

bool MRFileEditor::lineIndexWarmupPending() const noexcept {
	return mLineIndexWarmupTaskId != 0;
}

bool MRFileEditor::syntaxWarmupPending() const noexcept {
	return mSyntaxWarmupTaskId != 0;
}

bool MRFileEditor::miniMapWarmupPending() const noexcept {
	return mMiniMapRenderer.pendingWarmupTaskId() != 0;
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

bool MRFileEditor::freeCursorMovementEnabled() const noexcept {
	return configuredCursorBehaviour() == MRCursorBehaviour::FreeMovement;
}

int MRFileEditor::actualCursorVisualColumn(std::size_t offset) const noexcept {
	return charColumn(mBufferModel.lineStart(offset), offset);
}

int MRFileEditor::displayedCursorColumn() const noexcept {
	const int actualColumn = actualCursorVisualColumn(mBufferModel.cursor());
	if (!freeCursorMovementEnabled()) return actualColumn;
	return std::max(actualColumn, mCursorVisualColumn);
}

void MRFileEditor::syncDisplayedCursorColumnFromCursor(bool preserveFreeColumn) noexcept {
	const int actualColumn = actualCursorVisualColumn(mBufferModel.cursor());

	if (!freeCursorMovementEnabled() || !preserveFreeColumn) {
		mCursorVisualColumn = actualColumn;
		return;
	}
	if (mCursorVisualColumn < actualColumn) mCursorVisualColumn = actualColumn;
}

std::size_t MRFileEditor::cursorOffset() const noexcept {
	return mBufferModel.cursor();
}

std::size_t MRFileEditor::bufferLength() const noexcept {
	return mBufferModel.length();
}

std::size_t MRFileEditor::selectionStartOffset() const noexcept {
	return mBufferModel.selectionStart();
}

std::size_t MRFileEditor::selectionEndOffset() const noexcept {
	return mBufferModel.selectionEnd();
}

bool MRFileEditor::hasTextSelection() const noexcept {
	return mBufferModel.hasSelection();
}

std::size_t MRFileEditor::lineStartOffset(std::size_t pos) const noexcept {
	return mBufferModel.lineStart(pos);
}

std::size_t MRFileEditor::lineEndOffset(std::size_t pos) const noexcept {
	return mBufferModel.lineEnd(pos);
}

std::size_t MRFileEditor::nextLineOffset(std::size_t pos) const noexcept {
	return mBufferModel.nextLine(pos);
}

std::size_t MRFileEditor::prevLineOffset(std::size_t pos) const noexcept {
	return mBufferModel.prevLine(pos);
}

std::size_t MRFileEditor::lineIndexOfOffset(std::size_t pos) const noexcept {
	return mBufferModel.lineIndex(pos);
}

std::size_t MRFileEditor::columnOfOffset(std::size_t pos) const noexcept {
	return mBufferModel.column(pos);
}

char MRFileEditor::charAtOffset(std::size_t pos) const noexcept {
	return mBufferModel.charAt(pos);
}

std::string MRFileEditor::lineTextAtOffset(std::size_t pos) const {
	return mBufferModel.lineText(pos);
}

int MRFileEditor::charColumn(std::size_t start, std::size_t pos) const noexcept {
	std::size_t lineStart = mBufferModel.lineStart(start);
	std::string lineText = mBufferModel.lineText(lineStart);
	TStringView line(lineText.data(), lineText.size());
	const MREditSetupSettings settings = configuredEditSetupSettings();
	std::size_t p = 0;
	std::size_t end = std::min(pos, mBufferModel.length()) - lineStart;
	int visual = 0;

	end = std::min(end, line.size());
	while (p < end) {
		std::size_t next = p;
		std::size_t width = 0;
		if (!nextDisplayChar(line, next, width, visual, settings)) break;
		if (next > end) break;
		visual += static_cast<int>(width);
		p = next;
	}
	return visual;
}

void MRFileEditor::setCursorOffset(std::size_t pos, int) {
	moveCursor(std::min(pos, mBufferModel.length()), false, false);
}

bool MRFileEditor::scrollWindowByLines(int deltaRows) {
	const std::size_t cursorBefore = cursorOffset();
	const int rowBefore = currentViewRow();
	const int targetVisualColumn = displayedCursorColumn();
	const std::size_t target = lineMoveOffset(cursorBefore, deltaRows, targetVisualColumn);

	if (deltaRows == 0) return true;
	if (target == cursorBefore) return false;
	moveCursor(target, false, false, targetVisualColumn);
	if (const int rowDelta = currentViewRow() - rowBefore; rowDelta != 0) scrollTo(std::max(delta.x, 0), std::max(delta.y + rowDelta, 0));
	return true;
}

std::size_t MRFileEditor::offsetForGlobalPoint(TPoint where) noexcept {
	return mouseOffset(makeLocal(where));
}

int MRFileEditor::currentLineNumber() const noexcept {
	return static_cast<int>(mBufferModel.lineIndex(mBufferModel.cursor())) + 1;
}

int MRFileEditor::currentViewRow() const noexcept {
	return std::max(1, static_cast<int>(mBufferModel.lineIndex(mBufferModel.cursor())) - delta.y + 1);
}

int MRFileEditor::visibleViewportRows() const noexcept {
	return std::max(1, visibleTextRows());
}

const MRTextBufferModel &MRFileEditor::bufferModel() const noexcept {
	return mBufferModel;
}

MRTextBufferModel &MRFileEditor::bufferModel() noexcept {
	return mBufferModel;
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

MRMiniMapRenderer::Palette MRFileEditor::resolveMiniMapPalette() {
	MRMiniMapRenderer::Palette palette;
	unsigned char configured = 0;
	const TColorAttr fallback = static_cast<TColorAttr>(getColor(0x0201));

	palette.normal = configuredColorSlotOverride(kMrPaletteMiniMapNormal, configured) ? static_cast<TColorAttr>(configured) : fallback;
	palette.viewport = configuredColorSlotOverride(kMrPaletteMiniMapViewport, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	palette.changed = configuredColorSlotOverride(kMrPaletteMiniMapChanged, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	palette.findMarker = configuredColorSlotOverride(kMrPaletteMiniMapFindMarker, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	palette.errorMarker = configuredColorSlotOverride(kMrPaletteMiniMapErrorMarker, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	return palette;
}

void MRFileEditor::refreshConfiguredVisualSettings() {
	syncDisplayedCursorColumnFromCursor(true);
	syncIndicatorVisualSettings();
	updateMetrics();
	scheduleSyntaxWarmupIfNeeded();
	updateIndicator();
	drawView();
}

void MRFileEditor::setFindMarkerRanges(const std::vector<std::pair<std::size_t, std::size_t>> &ranges) {
	std::vector<MRTextBufferModel::Range> normalized;
	const std::size_t length = mBufferModel.length();

	normalized.reserve(ranges.size());
	if (length != 0) {
		for (const auto &rangePair : ranges) {
			std::size_t start = std::min(rangePair.first, length);
			std::size_t end = std::min(rangePair.second, length);
			if (end < start) std::swap(start, end);
			if (end == start) {
				if (end < length) ++end;
				else if (start > 0)
					--start;
			}
			if (end > start) normalized.push_back(MRTextBufferModel::Range(start, end));
		}
	}
	normalizeRangeList(normalized);
	mFindMarkerRanges.swap(normalized);
	drawView();
}

void MRFileEditor::clearFindMarkerRanges() {
	if (mFindMarkerRanges.empty()) return;
	mFindMarkerRanges.clear();
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
		mrIndicator->setInsertMode(mInsertMode);
		mrIndicator->setWordWrap(configuredEditSetupSettings().wordWrap);
	}
}

void MRFileEditor::notifyWindowTaskStateChanged() {
	if (owner != nullptr) message(owner, evBroadcast, cmUpdateTitle, 0);
}

void MRFileEditor::continueComputeWarmupIfNeeded() {
	scheduleLineIndexWarmupIfNeeded();
	scheduleSyntaxWarmupIfNeeded();
}

bool MRFileEditor::applyLineIndexWarmup(const mr::editor::LineIndexWarmupData &warmup, std::size_t expectedVersion) {
	const bool exactLineCountWasKnown = mBufferModel.exactLineCountKnown();
	if (shouldTraceLargeFileDiagnostics()) {
		std::ostringstream detail;
		detail << "expected_version=" << expectedVersion << " checkpoints=" << warmup.checkpoints.size() << " indexed_offset=" << warmup.lazyIndexedOffset << " indexed_line=" << warmup.lazyIndexedLine << " complete=" << (warmup.lazyLineIndexComplete ? 1 : 0) << " total_lines=" << warmup.lazyTotalLineCount;
		traceLargeFileMessage("line-index-apply", detail.str());
	}
	if (!mBufferModel.adoptLineIndexWarmup(warmup, expectedVersion)) return false;
	mLineIndexWarmupTaskId = 0;
	mLineIndexWarmupDocumentId = 0;
	mLineIndexWarmupVersion = 0;
	if (mBufferModel.exactLineCountKnown()) {
		const std::size_t exactLineCount = std::max<std::size_t>(1, mBufferModel.lineCount());
		const std::size_t lastValidTopLine = exactLineCount - 1;

		if (mSyntaxPrefetchTargetBottomLine > exactLineCount) mSyntaxPrefetchTargetBottomLine = exactLineCount;
		if (mSyntaxPrefetchReachedBottomLine > exactLineCount) mSyntaxPrefetchReachedBottomLine = exactLineCount;
		if (mSyntaxWarmupTopLine > lastValidTopLine) mSyntaxWarmupTopLine = lastValidTopLine;
		if (mSyntaxWarmupBottomLine > exactLineCount) mSyntaxWarmupBottomLine = exactLineCount;
		if (mSyntaxWarmupBottomLine < mSyntaxWarmupTopLine) mSyntaxWarmupBottomLine = exactLineCount;
		if (!exactLineCountWasKnown) applyMiniMapSignals(mMiniMapRenderer.invalidate(false, mBufferModel.documentId()));
	}
	scheduleSyntaxWarmupIfNeeded();
	notifyWindowTaskStateChanged();
	updateMetrics();
	updateIndicator();
	drawView();
	return true;
}

bool MRFileEditor::applySyntaxWarmup(const mr::coprocessor::SyntaxWarmupPayload &warmup, std::size_t expectedVersion, std::uint64_t expectedTaskId) {
	if (expectedTaskId == 0 || mSyntaxWarmupTaskId != expectedTaskId) return false;
	if (mBufferModel.documentId() != mSyntaxWarmupDocumentId || mBufferModel.version() != expectedVersion) return false;
	if (mBufferModel.language() != warmup.language)
		return false;

	const bool statefulSyntax = isStatefulSyntaxLanguage(warmup.language);
	MRSyntaxLineState state = warmup.lines.empty() ? MRSyntaxLineState() : syntaxWarmupInitialState(warmup.lines.front().lineStart);
	std::size_t lineIndex = 0;
	std::size_t warmedBottomLine = 0;

	if (!warmup.lines.empty()) {
		lineIndex = mBufferModel.lineIndex(warmup.lines.front().lineStart);
		warmedBottomLine = lineIndex + warmup.lines.size();
		if (mBufferModel.exactLineCountKnown()) {
			const std::size_t exactLineCount = std::max<std::size_t>(1, mBufferModel.lineCount());
			if (warmedBottomLine > exactLineCount) warmedBottomLine = exactLineCount;
		}
	}

	for (std::size_t i = 0; i < warmup.lines.size(); ++i) {
		std::map<std::size_t, MRSyntaxCacheEntry>::iterator found = mSyntaxTokenCache.find(warmup.lines[i].lineStart);
		bool stable = false;

		if (found != mSyntaxTokenCache.end() && found->second.stateIn == state) {
			const MRSyntaxLineResult &cachedLine = found->second.syntaxLine;
			const MRSyntaxLineResult &warmLine = warmup.lines[i].syntaxLine;

			if (cachedLine.stateOut == warmLine.stateOut && cachedLine.tokenRuns.size() == warmLine.tokenRuns.size()) {
				stable = true;
				for (std::size_t runIndex = 0; runIndex < cachedLine.tokenRuns.size(); ++runIndex) {
					const MRSyntaxTokenRun &cachedRun = cachedLine.tokenRuns[runIndex];
					const MRSyntaxTokenRun &warmRun = warmLine.tokenRuns[runIndex];

					if (cachedRun.column != warmRun.column || cachedRun.length != warmRun.length || cachedRun.token != warmRun.token) {
						stable = false;
						break;
					}
				}
			}
		}
		if (!stable) mSyntaxTokenCache[warmup.lines[i].lineStart] = MRSyntaxCacheEntry(state, warmup.lines[i].syntaxLine);
		if (statefulSyntax) rememberSyntaxCheckpoint(warmup.lines[i].lineStart, lineIndex, state);
		if (statefulSyntax) state = warmup.lines[i].syntaxLine.stateOut;
		else
			state = MRSyntaxLineState();
		++lineIndex;
	}

	if (mSyntaxPrefetchDocumentId == mBufferModel.documentId() && mSyntaxPrefetchVersion == expectedVersion && mSyntaxPrefetchLanguage == warmup.language && warmedBottomLine > mSyntaxPrefetchReachedBottomLine)
		mSyntaxPrefetchReachedBottomLine = warmedBottomLine;
	mSyntaxWarmupTaskId = 0;
	mSyntaxWarmupDocumentId = 0;
	mSyntaxWarmupVersion = 0;
	mSyntaxWarmupTopLine = 0;
	mSyntaxWarmupBottomLine = 0;
	mSyntaxWarmupLanguage = MRSyntaxLanguage::PlainText;
	notifyWindowTaskStateChanged();
	drawView();
	return true;
}

bool MRFileEditor::applyMiniMapWarmup(const mr::coprocessor::MiniMapWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId) {
	MRMiniMapRenderer::ApplyWarmupResult result = mMiniMapRenderer.applyWarmup(payload, expectedVersion, expectedTaskId, mBufferModel.documentId(), mBufferModel.version());
	applyMiniMapSignals(result.signals);
	return result.applied;
}

bool MRFileEditor::applySaveNormalizationWarmup(const mr::coprocessor::SaveNormalizationWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId, double runMicros) {
	if (expectedTaskId == 0 || mSaveNormalizationWarmupTaskId != expectedTaskId) return false;
	if (mBufferModel.documentId() != mSaveNormalizationWarmupDocumentId || mBufferModel.version() != expectedVersion) return false;
	if (mSaveNormalizationWarmupOptionsHash != payload.optionsHash) return false;
	noteSaveNormalizationThroughput(payload.sourceBytes, runMicros);
	clearSaveNormalizationWarmupTask(expectedTaskId);
	return true;
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
	if (expectedTaskId != 0 && mSyntaxWarmupTaskId != expectedTaskId) return;
	if (mSyntaxWarmupTaskId == 0) return;
	mSyntaxWarmupTaskId = 0;
	mSyntaxWarmupDocumentId = 0;
	mSyntaxWarmupVersion = 0;
	mSyntaxWarmupTopLine = 0;
	mSyntaxWarmupBottomLine = 0;
	mSyntaxWarmupLanguage = MRSyntaxLanguage::PlainText;
	notifyWindowTaskStateChanged();
}

void MRFileEditor::clearMiniMapWarmupTask(std::uint64_t expectedTaskId) noexcept {
	applyMiniMapSignals(mMiniMapRenderer.clearWarmupTask(expectedTaskId));
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

Boolean MRFileEditor::valid(ushort command) {
	if (command == cmValid || command == cmReleasedFocus) return True;
	if (mReadOnly || !mBufferModel.isModified()) return True;
	if (!canSaveInPlace()) return confirmSaveOrDiscardUntitled();
	return confirmSaveOrDiscardNamed();
}

bool MRFileEditor::isWordByte(char ch) noexcept {
	unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalnum(uch) != 0 || ch == '_';
}

bool MRFileEditor::hasShiftModifier(ushort mods) noexcept {
	return (mods & (kbShift | kbCtrlShift | kbAltShift)) != 0;
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

std::string MRFileEditor::preferredIndentFill() const {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const int targetColumn = resolvedEditFormatIndentColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, mPreferredIndentColumn);

	return buildEditIndentFill(settings, 1, targetColumn, configuredTabExpandSetting());
}

int MRFileEditor::visibleTextRows() const noexcept {
	return std::max(0, size.y - (configuredFormatRuler() ? 1 : 0));
}

void MRFileEditor::syncScrollBarsToState() noexcept {
	bool show = (state & (sfActive | sfSelected)) != 0;
	MREditWindow *window = dynamic_cast<MREditWindow *>(owner);
	if (window != nullptr && window->isMinimized()) show = false;
	if (hScrollBar != nullptr) {
		if (show) hScrollBar->show();
		else
			hScrollBar->hide();
	}
	if (vScrollBar != nullptr) {
		if (show) vScrollBar->show();
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

bool MRFileEditor::shouldTraceLargeFileDiagnostics() const noexcept {
	return mBufferModel.document().hasMappedOriginal() && mBufferModel.document().length() >= static_cast<std::size_t>(8) * 1024 * 1024;
}

void MRFileEditor::traceLargeFileMessage(const char *stage, const std::string &detail) const {
	if (!shouldTraceLargeFileDiagnostics()) return;
	std::ostringstream line;

	line << "Large-file " << stage << ": doc=" << mBufferModel.documentId() << " ver=" << mBufferModel.version();
	if (hasPersistentFileName()) line << " file='" << fileName << "'";
	if (!detail.empty()) line << " " << detail;
	mrLogMessage(line.str());
}

void MRFileEditor::traceLargeFileMetrics(const char *stage, int limitY, int maxY, int textRows, int newDeltaY) {
	if (!shouldTraceLargeFileDiagnostics()) return;
	const bool exactKnown = mBufferModel.exactLineCountKnown();
	const bool nearBottom = std::max(delta.y, newDeltaY) >= std::max(0, maxY - 2);
	const bool clamped = newDeltaY != delta.y;

	if (!nearBottom && !clamped && exactKnown == mLastLargeFileMetricsExactKnown && mLargeFileMetricsTraceValid) return;
	if (mLargeFileMetricsTraceValid && exactKnown == mLastLargeFileMetricsExactKnown && limitY == mLastLargeFileMetricsLimitY && maxY == mLastLargeFileMetricsMaxY && delta.y == mLastLargeFileMetricsDeltaY && newDeltaY == mLastLargeFileMetricsNewDeltaY) return;
	std::ostringstream detail;
	detail << "stage=" << stage << " exact=" << (exactKnown ? 1 : 0) << " estimated_lines=" << mBufferModel.estimatedLineCount();
	if (exactKnown) detail << " line_count=" << mBufferModel.lineCount();
	detail << " cursor_line=" << mBufferModel.lineIndex(mBufferModel.cursor()) << " delta_y=" << delta.y << " new_delta_y=" << newDeltaY << " limit_y=" << limitY << " max_y=" << maxY << " text_rows=" << textRows;
	traceLargeFileMessage("metrics", detail.str());
	mLargeFileMetricsTraceValid = true;
	mLastLargeFileMetricsExactKnown = exactKnown;
	mLastLargeFileMetricsLimitY = limitY;
	mLastLargeFileMetricsMaxY = maxY;
	mLastLargeFileMetricsDeltaY = delta.y;
	mLastLargeFileMetricsNewDeltaY = newDeltaY;
}

MRFileEditor::TextViewportGeometry MRFileEditor::textViewportGeometryFor(const MREditSetupSettings &settings) const noexcept {
	MRTextViewportLayout::Inputs inputs;
	inputs.viewWidth = size.x;
	inputs.visibleRows = visibleTextRows();
	inputs.deltaX = delta.x;
	inputs.deltaY = delta.y;
	inputs.exactLineCountKnown = mBufferModel.exactLineCountKnown();
	inputs.exactLineCount = mBufferModel.lineCount();
	inputs.estimatedLineCount = mBufferModel.estimatedLineCount();
	return MRTextViewportLayout::geometryFor(settings, inputs);
}

MRFileEditor::TextViewportGeometry MRFileEditor::textViewportGeometry() const noexcept {
	return textViewportGeometryFor(configuredEditSetupSettings());
}

bool MRFileEditor::shouldShowEditorCursor(long long x, long long y, const TextViewportGeometry &viewport) const noexcept {
	return MRTextViewportLayout::shouldShowCursor(viewport, x, y, visibleTextRows(), (state & sfActive) != 0, (state & sfSelected) != 0);
}

bool MRFileEditor::shouldShowEditorCursor(long long x, long long y) const noexcept {
	TextViewportGeometry viewport = textViewportGeometry();
	return shouldShowEditorCursor(x, y, viewport);
}

int MRFileEditor::textColumnFromLocalX(int localX) const noexcept {
	return textViewportGeometry().textColumnFromLocalX(localX);
}

int MRFileEditor::textViewportWidth() const {
	return textViewportGeometry().width;
}

std::string MRFileEditor::normalizedFormatRulerLine(const MREditSetupSettings &settings, int *leftMarginOut, int *rightMarginOut) const {
	const MRTextFormatting::NormalizedFormatLine normalized = MRTextFormatting::normalizedFormatLine(settings);
	if (leftMarginOut != nullptr) *leftMarginOut = normalized.leftMargin;
	if (rightMarginOut != nullptr) *rightMarginOut = normalized.rightMargin;
	return normalized.line;
}

const char *MRFileEditor::lineIndexWarmupTaskLabel() noexcept {
	return "line-index-warmup";
}

const char *MRFileEditor::syntaxWarmupTaskLabel() noexcept {
	return "syntax-warmup";
}

const char *MRFileEditor::saveNormalizationWarmupTaskLabel() noexcept {
	return "save-normalization";
}

bool MRFileEditor::lineIntersectsDirtyRanges(std::size_t lineStart, std::size_t lineEnd) const noexcept {
	if (lineEnd <= lineStart || mDirtyRanges.empty()) return false;
	for (const MRTextBufferModel::Range &range : mDirtyRanges) {
		if (range.end <= lineStart) continue;
		if (range.start >= lineEnd) break;
		return true;
	}
	return false;
}

bool MRFileEditor::ratioCellActive(int numerator, int denominator, int cellIndex, int cellCount) noexcept {
	if (numerator <= 0 || denominator <= 0 || cellCount <= 0) return false;
	if (numerator >= denominator) return true;
	long long lhs = static_cast<long long>(numerator) * static_cast<long long>(cellCount);
	long long rhs = static_cast<long long>(cellIndex + 1) * static_cast<long long>(denominator);
	return lhs >= rhs;
}

bool MRFileEditor::nextDisplayChar(TStringView text, std::size_t &index, std::size_t &width, int visualColumn, const MREditSetupSettings &settings) noexcept {
	if (index >= text.size()) return false;
	if (text[index] == '\t') {
		++index;
		width = static_cast<std::size_t>(tabDisplayWidth(settings, visualColumn));
		return true;
	}
	return TText::next(text, index, width);
}

int MRFileEditor::displayWidthForText(TStringView text, const MREditSetupSettings &settings) noexcept {
	std::size_t index = 0;
	int visual = 0;

	while (index < text.size()) {
		std::size_t next = index;
		std::size_t width = 0;
		if (!nextDisplayChar(text, next, width, visual, settings)) break;
		visual += static_cast<int>(width);
		index = next;
	}
	return visual;
}

void MRFileEditor::writeChunk(std::ofstream &out, const char *data, std::size_t length) {
	while (length > 0) {
		std::size_t part = std::min<std::size_t>(length, static_cast<std::size_t>(INT_MAX));
		out.write(data, static_cast<std::streamsize>(part));
		data += part;
		length -= part;
	}
}

bool MRFileEditor::pathIsRegularFile(const char *path) noexcept {
	struct stat st;
	if (path == nullptr || *path == '\0') return false;
	return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool MRFileEditor::samePath(const char *lhs, const char *rhs) noexcept {
	struct stat lhsStat;
	struct stat rhsStat;
	char lhsExpanded[MAXPATH];
	char rhsExpanded[MAXPATH];
	std::size_t i = 0;

	if (lhs == nullptr || rhs == nullptr) return false;
	if (::stat(lhs, &lhsStat) == 0 && ::stat(rhs, &rhsStat) == 0) return lhsStat.st_dev == rhsStat.st_dev && lhsStat.st_ino == rhsStat.st_ino;

	strnzcpy(lhsExpanded, lhs, sizeof(lhsExpanded));
	strnzcpy(rhsExpanded, rhs, sizeof(rhsExpanded));
	fexpand(lhsExpanded);
	fexpand(rhsExpanded);
	for (i = 0; lhsExpanded[i] != EOS; ++i)
		if (lhsExpanded[i] == '\\') lhsExpanded[i] = '/';
	for (i = 0; rhsExpanded[i] != EOS; ++i)
		if (rhsExpanded[i] == '\\') rhsExpanded[i] = '/';
	return std::strcmp(lhsExpanded, rhsExpanded) == 0;
}

bool MRFileEditor::confirmOverwriteForSaveAs(const char *targetPath) const {
	if (!pathIsRegularFile(targetPath)) return true;
	return mr::dialogs::showUnsavedChangesDialog("Overwrite", "Target file exists. Overwrite?", targetPath) == mr::dialogs::UnsavedChangesChoice::Save;
}

std::size_t MRFileEditor::lineStartForIndex(std::size_t index) const noexcept {
	return mBufferModel.lineStartByIndex(index);
}

int MRFileEditor::longestLineWidth() const noexcept {
	std::size_t pos = 0;
	std::size_t len = mBufferModel.length();
	const MREditSetupSettings settings = configuredEditSetupSettings();
	int maxWidth = 1;

	while (true) {
		int width = displayWidthForText(mBufferModel.lineText(pos), settings);
		maxWidth = std::max(maxWidth, width + 1);
		if (pos >= len) break;
		std::size_t next = mBufferModel.nextLine(pos);
		if (next <= pos) break;
		pos = next;
	}
	return maxWidth;
}

bool MRFileEditor::useApproximateLargeFileMetrics() const noexcept {
	const MRTextBufferModel::Document &document = mBufferModel.document();
	return document.hasMappedOriginal() && document.length() >= static_cast<std::size_t>(8) * 1024 * 1024;
}

int MRFileEditor::dynamicLargeFileLineLimit() const noexcept {
	const std::size_t estimated = mBufferModel.estimatedLineCount();
	const std::size_t currentLine = mBufferModel.lineIndex(mBufferModel.cursor());
	const int textRows = std::max(1, visibleTextRows());
	const std::size_t minimum = static_cast<std::size_t>(textRows);
	const std::size_t margin = static_cast<std::size_t>(std::max(textRows * 4, 256));
	std::size_t limitValue = std::max<std::size_t>(estimated, currentLine + margin);
	limitValue = std::max<std::size_t>(limitValue, minimum);
	return static_cast<int>(std::min<std::size_t>(limitValue, static_cast<std::size_t>(INT_MAX)));
}

int MRFileEditor::dynamicLargeFileWidthLimit() const {
	const int viewportWidth = textViewportWidth();
	return std::max(std::max(viewportWidth, 256), std::max(delta.x + viewportWidth + 64, displayedCursorColumn() + 64));
}

bool MRFileEditor::resolveSaveOptionsForPath(const char *path, MRTextSaveOptions &options, std::size_t *optionsHash) const {
	options = effectiveTextSaveOptionsForPath(path != nullptr ? path : "", optionsHash);
	return true;
}

void MRFileEditor::invalidateSaveNormalizationCache() noexcept {
	mSaveNormalizationCache.valid = false;
	mSaveNormalizationCache.documentId = 0;
	mSaveNormalizationCache.version = 0;
	mSaveNormalizationCache.optionsHash = 0;
	mSaveNormalizationCache.sourceBytes = 0;
}

void MRFileEditor::noteSaveNormalizationThroughput(std::size_t sourceBytes, double runMicros) noexcept {
	if (sourceBytes == 0 || runMicros <= 0.0) return;
	const double sampleBytesPerMicro = static_cast<double>(sourceBytes) / std::max(1.0, runMicros);
	if (mSaveNormalizationThroughputBytesPerMicro <= 0.0) mSaveNormalizationThroughputBytesPerMicro = sampleBytesPerMicro;
	else
		mSaveNormalizationThroughputBytesPerMicro = mSaveNormalizationThroughputBytesPerMicro * 0.75 + sampleBytesPerMicro * 0.25;
	++mSaveNormalizationThroughputSamples;
}

std::size_t MRFileEditor::nextCharOffset(std::size_t pos) noexcept {
	std::size_t len = mBufferModel.length();
	char bytes[4];
	std::size_t count = 0;

	if (pos >= len) return len;
	if (mBufferModel.charAt(pos) == '\r' && pos + 1 < len && mBufferModel.charAt(pos + 1) == '\n') return std::min(len, pos + 2);
	for (; count < sizeof(bytes) && pos + count < len; ++count)
		bytes[count] = mBufferModel.charAt(pos + count);
	std::size_t step = TText::next(TStringView(bytes, count));
	return std::min(len, pos + std::max<std::size_t>(step, 1));
}

std::size_t MRFileEditor::prevCharOffset(std::size_t pos) noexcept {
	char bytes[4];
	std::size_t start = 0;
	std::size_t count = 0;

	if (pos == 0) return 0;
	if (pos > 1 && mBufferModel.charAt(pos - 2) == '\r' && mBufferModel.charAt(pos - 1) == '\n') return pos - 2;
	start = pos > sizeof(bytes) ? pos - sizeof(bytes) : 0;
	count = pos - start;
	for (std::size_t i = 0; i < count; ++i)
		bytes[i] = mBufferModel.charAt(start + i);
	std::size_t step = TText::prev(TStringView(bytes, count), count);
	return pos - std::max<std::size_t>(step, 1);
}

std::size_t MRFileEditor::lineMoveOffset(std::size_t pos, int deltaLines, int targetVisualColumn) noexcept {
	std::size_t target = std::min(pos, mBufferModel.length());

	if (targetVisualColumn < 0) targetVisualColumn = charColumn(mBufferModel.lineStart(pos), std::min(pos, mBufferModel.length()));
	if (deltaLines < 0) {
		for (std::size_t i = 0, distance = static_cast<std::size_t>(-deltaLines); i < distance; ++i) {
			std::size_t prev = prevLineOffset(target);
			if (prev == target) break;
			target = prev;
		}
	} else {
		for (int i = 0; i < deltaLines; ++i) {
			std::size_t next = nextLineOffset(target);
			if (next == target) break;
			target = next;
		}
	}
	return charPtrOffset(lineStartOffset(target), targetVisualColumn);
}

std::size_t MRFileEditor::tabStopMoveOffset(std::size_t pos, bool forward) noexcept {
	const std::size_t cursor = std::min(pos, mBufferModel.length());
	const std::size_t lineStart = lineStartOffset(cursor);
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const int currentColumn = (freeCursorMovementEnabled() && cursor == mBufferModel.cursor() && !mBufferModel.hasSelection() ? displayedCursorColumn() : charColumn(lineStart, cursor)) + 1;
	const int targetColumn = forward ? nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn) : prevResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);

	return charPtrOffset(lineStart, targetColumn - 1);
}

std::size_t MRFileEditor::prevWordOffset(std::size_t pos) noexcept {
	std::size_t p = std::min(pos, mBufferModel.length());

	while (p > 0 && !isWordByte(mBufferModel.charAt(p - 1)))
		--p;
	while (p > 0 && isWordByte(mBufferModel.charAt(p - 1)))
		--p;
	return p;
}

std::size_t MRFileEditor::nextWordOffset(std::size_t pos) noexcept {
	std::size_t p = std::min(pos, mBufferModel.length());
	std::size_t len = mBufferModel.length();

	while (p < len && isWordByte(mBufferModel.charAt(p)))
		++p;
	while (p < len && !isWordByte(mBufferModel.charAt(p)))
		++p;
	return p;
}

std::size_t MRFileEditor::charPtrOffset(std::size_t start, int pos) noexcept {
	std::size_t lineStart = mBufferModel.lineStart(start);
	std::string lineText = mBufferModel.lineText(lineStart);
	TStringView line(lineText.data(), lineText.size());
	const MREditSetupSettings settings = configuredEditSetupSettings();
	std::size_t p = 0;
	int visual = 0;
	int target = std::max(pos, 0);

	while (p < line.size()) {
		std::size_t next = p;
		std::size_t width = 0;
		if (!nextDisplayChar(line, next, width, visual, settings)) break;
		if (visual + static_cast<int>(width) > target) break;
		visual += static_cast<int>(width);
		p = next;
	}
	return lineStart + p;
}

bool MRFileEditor::canSaveInPlace() const {
	std::string persistentName;

	if (mReadOnly || !hasPersistentFileName()) return false;
	persistentName = trimAscii(fileName);
	if (upperAscii(persistentName) == "?NO-FILE?") return false;
	if (looksLikeUri(persistentName)) return false;
	return true;
}

bool MRFileEditor::canSaveAs() const {
	return !mReadOnly;
}

bool MRFileEditor::loadMappedFile(TStringView path, std::string &error) {
	MRTextBufferModel::Document document;
	const auto mapStartedAt = std::chrono::steady_clock::now();

	mLastLoadTiming = LoadTiming();
	if (!document.loadMappedFile(path, error)) return false;
	const double mappedLoadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mapStartedAt).count();
	const auto lineCountStartedAt = std::chrono::steady_clock::now();
	const std::size_t lines = document.lineCount();
	const double lineCountMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lineCountStartedAt).count();

	mLastLoadTiming.valid = true;
	mLastLoadTiming.bytes = document.length();
	mLastLoadTiming.lines = lines;
	mLastLoadTiming.mappedLoadMs = mappedLoadMs;
	mLastLoadTiming.lineCountMs = lineCountMs;
	setPersistentFileName(path);
	if (!adoptCommittedDocument(document, 0, 0, 0, false)) {
		clearPersistentFileName();
		mLastLoadTiming = LoadTiming();
		error = "Unable to adopt mapped document.";
		return false;
	}
	return true;
}

Boolean MRFileEditor::saveInPlace() noexcept {
	if (!canSaveInPlace()) return False;
	Boolean ok = writeDocumentToPath(fileName) ? True : False;
	if (ok == True) setDocumentModified(false);
	return ok;
}

Boolean MRFileEditor::saveAsWithPrompt() noexcept {
	char saveName[MAXPATH];

	if (!canSaveAs()) return False;
	if (hasPersistentFileName()) strnzcpy(saveName, fileName, sizeof(saveName));
	else
		initRememberedLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName, sizeof(saveName), "*.*");
	if (TEditor::editorDialog(edSaveAs, saveName) == cmCancel) return False;
	fexpand(saveName);
	if (!samePath(saveName, fileName) && !confirmOverwriteForSaveAs(saveName)) return False;
	if (!writeDocumentToPath(saveName)) return False;
	rememberLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName);
	setPersistentFileName(saveName);
	if (owner != nullptr) message((TView *)owner, evBroadcast, cmUpdateTitle, 0);
	setDocumentModified(false);
	return True;
}

Boolean MRFileEditor::saveAsWithoutOverwritePrompt() noexcept {
	char saveName[MAXPATH];

	if (!canSaveAs()) return False;
	if (hasPersistentFileName()) strnzcpy(saveName, fileName, sizeof(saveName));
	else
		initRememberedLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName, sizeof(saveName), "*.*");
	if (TEditor::editorDialog(edSaveAs, saveName) == cmCancel) return False;
	fexpand(saveName);
	if (!writeDocumentToPath(saveName)) return False;
	rememberLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName);
	setPersistentFileName(saveName);
	if (owner != nullptr) message((TView *)owner, evBroadcast, cmUpdateTitle, 0);
	setDocumentModified(false);
	return True;
}

void MRFileEditor::pushUndoSnapshot() {
	MRTextBufferModel::CustomUndoRecord record;
	record.preSnapshot = mBufferModel.readSnapshot();
	record.cursor = mBufferModel.cursor();
	record.modifiedState = mBufferModel.isModified();
	if (mBufferModel.hasSelection()) {
		record.selAnchor = mBufferModel.selection().range().start;
		record.selCursor = mBufferModel.selection().range().end;
	} else {
		record.selAnchor = 0;
		record.selCursor = 0;
	}
	if (owner != nullptr) {
		record.blockMode = mBlockOverlayMode;
		record.blockAnchor = mBlockOverlayAnchor;
		record.blockEnd = mBlockOverlayEnd;
		record.blockMarkingOn = mBlockOverlayActive;
	}
	mBufferModel.pushUndoSnapshot(record);
}

bool MRFileEditor::replaceBufferData(const char *data, uint length) {
	std::string text;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "replace-buffer-data");
	MRTextBufferModel::Document preview;
	MRTextBufferModel::CommitResult commit;

	if (data != nullptr && length != 0) text.assign(data, length);
	transaction.setText(text);
	preview = mBufferModel.document();
	pushUndoSnapshot();
	commit = preview.tryApply(transaction);
	if (!commit.applied()) {
		mBufferModel.popUndoSnapshot();
		return false;
	}
	return adoptCommittedDocument(preview, 0, 0, 0, false);
}

bool MRFileEditor::replaceBufferText(const char *text) {
	uint length = text != nullptr ? static_cast<uint>(std::strlen(text)) : 0;
	return replaceBufferData(text, length);
}

bool MRFileEditor::appendBufferData(const char *data, uint length) {
	std::string text;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "append-buffer-data");
	MRTextBufferModel::Document preview;
	MRTextBufferModel::CommitResult commit;
	std::size_t endPtr = mBufferModel.length();

	if (length == 0) return true;
	if (data != nullptr) text.assign(data, length);
	transaction.insert(endPtr, text);
	preview = mBufferModel.document();
	pushUndoSnapshot();
	commit = preview.tryApply(transaction);
	if (!commit.applied()) {
		mBufferModel.popUndoSnapshot();
		return false;
	}
	return adoptCommittedDocument(preview, endPtr + text.size(), endPtr + text.size(), endPtr + text.size(), false);
}

bool MRFileEditor::appendBufferText(const char *text) {
	uint length = text != nullptr ? static_cast<uint>(std::strlen(text)) : 0;
	return appendBufferData(text, length);
}

bool MRFileEditor::formatParagraph(int rightMargin) {
	return formatParagraph(configuredEditSetupSettings().leftMargin, rightMargin);
}

std::string MRFileEditor::buildFormattedParagraphText(std::string_view paragraphText, int leftMargin, int rightMargin) const {
	return MRTextFormatting::formatParagraphText(paragraphText, leftMargin, rightMargin);
}

bool MRFileEditor::formatParagraph(int leftMargin, int rightMargin) {
	if (mReadOnly) return false;

	std::size_t start = mBufferModel.cursor();
	std::size_t end = start;
	while (start > 0) {
		std::size_t prevLineStart = mBufferModel.lineStart(mBufferModel.prevLine(start));
		if (isBlankString(mBufferModel.lineText(prevLineStart))) break;
		start = prevLineStart;
	}
	while (end < mBufferModel.length()) {
		std::size_t nextLineStart = mBufferModel.nextLine(end);
		if (isBlankString(mBufferModel.lineText(end))) break;
		end = nextLineStart;
	}
	if (start == end) return true;

	std::string paragraphText;
	paragraphText.reserve(end - start);
	std::size_t current = start;
	while (current < end) {
		std::string chunk = mBufferModel.document().lineText(current);
		if (!paragraphText.empty()) paragraphText.push_back('\n');
		paragraphText += chunk;
		current = mBufferModel.document().nextLine(current);
	}
	std::string formattedText = buildFormattedParagraphText(paragraphText, leftMargin, rightMargin);
	if (formattedText.empty()) return true;

	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "format-paragraph");
	transaction.replace(MRTextBufferModel::Range(start, end), formattedText);
	MRTextBufferModel::Document preview = mBufferModel.document();
	pushUndoSnapshot();
	MRTextBufferModel::CommitResult commit = preview.tryApply(transaction);
	if (!commit.applied()) {
		mBufferModel.popUndoSnapshot();
		return false;
	}
	return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
}

bool MRFileEditor::formatDocument(int leftMargin, int rightMargin) {
	std::string formattedText;
	const std::size_t length = mBufferModel.length();
	const std::size_t cursor = mBufferModel.cursor();
	std::size_t current = 0;

	if (mReadOnly) return false;
	while (current < length) {
		if (isBlankString(mBufferModel.lineText(current))) {
			formattedText.push_back('\n');
			current = mBufferModel.nextLine(current);
			continue;
		}
		std::string paragraphText;
		const std::size_t paragraphStart = current;
		std::size_t paragraphEnd = current;
		while (paragraphEnd < length && !isBlankString(mBufferModel.lineText(paragraphEnd))) {
			if (!paragraphText.empty()) paragraphText.push_back('\n');
			paragraphText += mBufferModel.document().lineText(paragraphEnd);
			paragraphEnd = mBufferModel.document().nextLine(paragraphEnd);
		}
		if (!formattedText.empty() && formattedText.back() != '\n') formattedText.push_back('\n');
		formattedText += buildFormattedParagraphText(paragraphText, leftMargin, rightMargin);
		current = paragraphEnd;
		if (current == paragraphStart) break;
	}
	return replaceWholeBuffer(formattedText, std::min(cursor, formattedText.size()));
}

bool MRFileEditor::justifyParagraph(int leftMargin, int rightMargin) {
	if (mReadOnly) return false;

	std::size_t start = mBufferModel.cursor();
	std::size_t end = start;
	std::string paragraphText;
	while (start > 0) {
		std::size_t prevLineStart = mBufferModel.lineStart(mBufferModel.prevLine(start));
		if (isBlankString(mBufferModel.lineText(prevLineStart))) break;
		start = prevLineStart;
	}
	while (end < mBufferModel.length()) {
		std::size_t nextLineStart = mBufferModel.nextLine(end);
		if (isBlankString(mBufferModel.lineText(end))) break;
		end = nextLineStart;
	}
	if (start == end) return true;
	paragraphText.reserve(end - start);
	for (std::size_t current = start; current < end; current = mBufferModel.document().nextLine(current))
		if (std::string chunk = mBufferModel.document().lineText(current); true) {
			if (!paragraphText.empty()) paragraphText.push_back('\n');
			paragraphText += chunk;
		}
	std::string justifiedText = MRTextFormatting::justifyParagraphText(paragraphText, leftMargin, rightMargin);
	if (justifiedText.empty()) return true;
	return replaceRangeAndSelect(static_cast<uint>(start), static_cast<uint>(end), justifiedText.data(), static_cast<uint>(justifiedText.size()));
}

void MRFileEditor::setBlockOverlayState(int mode, std::size_t anchor, std::size_t end, bool active, bool trackCursor) {
	const std::size_t length = mBufferModel.length();

	if (!active || mode < 1 || mode > 3) {
		mBlockOverlayActive = false;
		mBlockOverlayMode = 0;
		mBlockOverlayAnchor = 0;
		mBlockOverlayEnd = 0;
		mBlockOverlayTrackingCursor = false;
		drawView();
		return;
	}
	mBlockOverlayActive = true;
	mBlockOverlayMode = mode;
	mBlockOverlayAnchor = std::min(anchor, length);
	mBlockOverlayEnd = std::min(end, length);
	mBlockOverlayTrackingCursor = trackCursor;
	drawView();
}

void MRFileEditor::setSelectionOffsets(std::size_t start, std::size_t end, Boolean) {
	start = std::min(start, mBufferModel.length());
	end = std::min(end, mBufferModel.length());
	mSelectionAnchor = start;
	mBufferModel.setSelection(start, end);
	syncFromEditorState(false);
}

bool MRFileEditor::replaceRangeAndSelect(uint start, uint end, const char *data, uint length) {
	std::string text;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "replace-range-select");
	MRTextBufferModel::Document preview;
	MRTextBufferModel::CommitResult commit;
	MRTextBufferModel::Range range;

	if (mReadOnly) return false;
	if (end < start) std::swap(start, end);
	range = MRTextBufferModel::Range(start, end).clamped(mBufferModel.length());
	if (data != nullptr && length != 0) text.assign(data, length);
	transaction.replace(range, text);
	preview = mBufferModel.document();
	pushUndoSnapshot();
	commit = preview.tryApply(transaction);
	if (!commit.applied()) {
		mBufferModel.popUndoSnapshot();
		return false;
	}
	return adoptCommittedDocument(preview, range.start, range.start, range.start + text.size(), true, &commit.change);
}

int MRFileEditor::paddingColumnsBeforeInsertAtCursor() const noexcept {
	const std::size_t cursor = mBufferModel.cursor();
	const std::size_t lineEnd = lineEndOffset(cursor);

	if (!freeCursorMovementEnabled() || mBufferModel.hasSelection() || cursor != lineEnd) return 0;
	return std::max(0, displayedCursorColumn() - actualCursorVisualColumn(cursor));
}

bool MRFileEditor::insertBufferText(const std::string &text) {
	std::string insertedText = text;
	std::size_t start = mBufferModel.cursor();
	std::size_t end = start;
	MRTextBufferModel::Range range;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "insert-buffer-text");
	MRTextBufferModel::Document preview;
	MRTextBufferModel::CommitResult commit;

	if (mReadOnly) return false;
	if (!insertedText.empty()) {
		const int paddingColumns = paddingColumnsBeforeInsertAtCursor();
		if (paddingColumns > 0) insertedText.insert(0, static_cast<std::size_t>(paddingColumns), ' ');
	}
	if (mBufferModel.hasSelection()) {
		range = mBufferModel.selection().range();
		start = range.start;
		end = range.end;
	} else if (!mInsertMode) {
		std::size_t endSel = mBufferModel.cursor();
		for (std::string::size_type i = 0; i < insertedText.size() && endSel < lineEndOffset(start); ++i)
			endSel = nextCharOffset(endSel);
		end = endSel;
	}
	range = MRTextBufferModel::Range(start, end).clamped(mBufferModel.length());
	transaction.replace(range, insertedText);
	preview = mBufferModel.document();
	pushUndoSnapshot();
	commit = preview.tryApply(transaction);
	if (!commit.applied()) {
		mBufferModel.popUndoSnapshot();
		return false;
	}
	start = range.start + insertedText.size();
	return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
}

bool MRFileEditor::applyCurrentLineLeadingIndent(int targetColumn) {
	const std::size_t cursor = cursorOffset();
	const std::size_t lineStart = lineStartOffset(cursor);
	const std::string lineText = mBufferModel.lineText(lineStart);
	std::size_t indentBytes = 0;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "live-smart-dedent");
	std::string replacement;
	std::size_t newCursor = cursor;
	int removeSpaces = configuredTabSize();

	(void)targetColumn;

	while (indentBytes < lineText.size() && (lineText[indentBytes] == ' ' || lineText[indentBytes] == '\t'))
		++indentBytes;
	if (indentBytes == 0) return false;
	replacement.assign(lineText.data(), indentBytes);
	if (!replacement.empty() && replacement.back() == '\t') replacement.pop_back();
	else
		while (!replacement.empty() && replacement.back() == ' ' && removeSpaces-- > 0)
			replacement.pop_back();
	transaction.replace(MRTextBufferModel::Range(lineStart, lineStart + indentBytes), replacement);
	if (cursor <= lineStart + indentBytes) newCursor = lineStart + replacement.size();
	else
		newCursor = cursor - indentBytes + replacement.size();
	return applyStagedTransaction(transaction, newCursor, newCursor, newCursor, true).applied();
}

bool MRFileEditor::replaceCurrentLineText(const std::string &text) {
	std::size_t start = mBufferModel.lineStart(mBufferModel.cursor());
	std::size_t end = mBufferModel.lineEnd(mBufferModel.cursor());
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "replace-current-line");
	MRTextBufferModel::Document preview;
	MRTextBufferModel::CommitResult commit;

	if (mReadOnly) return false;
	transaction.replace(MRTextBufferModel::Range(start, end), text);
	preview = mBufferModel.document();
	pushUndoSnapshot();
	commit = preview.tryApply(transaction);
	if (!commit.applied()) {
		mBufferModel.popUndoSnapshot();
		return false;
	}
	return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
}

bool MRFileEditor::centerCurrentLine(int leftMargin, int rightMargin) {
	std::string text;
	std::string trimmed;
	const int safeLeftMargin = std::max(1, leftMargin);
	const int safeRightMargin = std::max(safeLeftMargin, rightMargin);
	int contentWidth = 0;
	int padWidth = 0;

	if (mReadOnly) return false;
	text = lineTextAtOffset(cursorOffset());
	trimmed = trimAscii(text);
	if (trimmed.empty()) return replaceCurrentLineText(std::string());
	contentWidth = displayWidthForText(trimmed, configuredEditSetupSettings());
	padWidth = std::max(safeLeftMargin - 1, ((safeRightMargin - contentWidth) / 2));
	return replaceCurrentLineText(std::string(static_cast<std::size_t>(padWidth), ' ') + trimmed);
}

bool MRFileEditor::copyCharFromLineAbove() {
	const std::size_t cursor = cursorOffset();
	const std::size_t currentLineStart = lineStartOffset(cursor);
	const std::size_t previousLineStart = prevLineOffset(currentLineStart);
	const std::size_t previousLineEnd = lineEndOffset(previousLineStart);
	const int targetColumn = charColumn(currentLineStart, cursor);
	const std::size_t sourceOffset = charPtrOffset(previousLineStart, targetColumn);
	char ch = '\0';

	if (mReadOnly || currentLineStart == previousLineStart || sourceOffset >= previousLineEnd) return false;
	ch = charAtOffset(sourceOffset);
	if (ch == '\0' || ch == '\r' || ch == '\n') return false;
	return insertBufferText(std::string(1, ch));
}

bool MRFileEditor::deleteCharsAtCursor(int count) {
	std::size_t start = mBufferModel.cursor();
	std::size_t end = start;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "delete-chars-at-cursor");
	MRTextBufferModel::Document preview;
	MRTextBufferModel::CommitResult commit;

	if (mReadOnly) return false;
	if (count <= 0) return true;
	for (int i = 0; i < count && end < mBufferModel.length(); ++i)
		end = nextCharOffset(end);
	if (end <= start) return true;
	transaction.erase(MRTextBufferModel::Range(start, end));
	preview = mBufferModel.document();
	pushUndoSnapshot();
	commit = preview.tryApply(transaction);
	if (!commit.applied()) {
		mBufferModel.popUndoSnapshot();
		return false;
	}
	return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
}

bool MRFileEditor::deleteCurrentLineText() {
	std::size_t start = mBufferModel.lineStart(mBufferModel.cursor());
	std::size_t end = mBufferModel.nextLine(mBufferModel.cursor());
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "delete-current-line");
	MRTextBufferModel::Document preview;
	MRTextBufferModel::CommitResult commit;

	if (mReadOnly) return false;
	transaction.erase(MRTextBufferModel::Range(start, end));
	preview = mBufferModel.document();
	pushUndoSnapshot();
	commit = preview.tryApply(transaction);
	if (!commit.applied()) {
		mBufferModel.popUndoSnapshot();
		return false;
	}
	return adoptCommittedDocument(preview, start, start, start, true, &commit.change);
}

bool MRFileEditor::replaceWholeBuffer(const std::string &text, std::size_t cursorPos) {
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "replace-whole-buffer");
	MRTextBufferModel::Document preview;
	MRTextBufferModel::CommitResult commit;

	if (mReadOnly) return false;
	transaction.setText(text);
	preview = mBufferModel.document();
	pushUndoSnapshot();
	commit = preview.tryApply(transaction);
	if (!commit.applied()) {
		mBufferModel.popUndoSnapshot();
		return false;
	}
	cursorPos = std::min(cursorPos, text.size());
	return adoptCommittedDocument(preview, cursorPos, cursorPos, cursorPos, true, &commit.change);
}

bool MRFileEditor::adoptCommittedDocument(const MRTextBufferModel::Document &document, std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState, const MRTextBufferModel::DocumentChangeSet *changeSet) {
	cursorPos = std::min(cursorPos, document.length());
	selStart = std::min(selStart, document.length());
	selEnd = std::min(selEnd, document.length());
	if (selEnd < selStart) std::swap(selStart, selEnd);

	mBufferModel.document() = document;
	invalidateSaveNormalizationCache();
	resetSyntaxWarmupState(changeSet == nullptr || !changeSet->changed);
	if (changeSet != nullptr && changeSet->changed) invalidateSyntaxCacheFromLineStart(mBufferModel.lineStart(changeSet->touchedRange.start));
	applyMiniMapSignals(mMiniMapRenderer.invalidate(false, mBufferModel.documentId()));
	refreshSyntaxContext();
	cursorPos = canonicalCursorOffset(cursorPos);
	selStart = canonicalCursorOffset(selStart);
	selEnd = canonicalCursorOffset(selEnd);
	mBufferModel.setCursorAndSelection(cursorPos, selStart, selEnd);
	syncDisplayedCursorColumnFromCursor(false);
	mBufferModel.setModified(modifiedState);
	if (changeSet == nullptr || changeSet->changed) mFindMarkerRanges.clear();
	if (!modifiedState) clearDirtyRanges();
	else if (changeSet != nullptr && changeSet->changed) {
		remapDirtyRangesForAppliedChange(*changeSet);
		addDirtyRange(changeSet->touchedRange);
	}
	mSelectionAnchor = selStart;
	updateMetrics();
	scheduleLineIndexWarmupIfNeeded();
	scheduleSyntaxWarmupIfNeeded();
	scheduleSaveNormalizationWarmupIfNeeded();
	ensureCursorVisible(false);
	updateIndicator();
	drawView();
	return true;
}

MRTextBufferModel::CommitResult MRFileEditor::applyStagedTransaction(const MRTextBufferModel::StagedTransaction &transaction, std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState) {
	MRTextBufferModel::Document preview = mBufferModel.document();
	pushUndoSnapshot();
	MRTextBufferModel::CommitResult result = preview.tryApply(transaction);

	if (result.applied()) adoptCommittedDocument(preview, cursorPos, selStart, selEnd, modifiedState, &result.change);
	else
		mBufferModel.popUndoSnapshot();
	return result;
}

bool MRFileEditor::newLineWithIndent(const std::string &fill) {
	return insertBufferText(std::string("\n") + fill);
}

int MRFileEditor::leadingIndentColumnForLine(std::size_t lineStart) const noexcept {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	std::string lineText = mBufferModel.lineText(lineStart);
	TStringView line(lineText.data(), lineText.size());
	std::size_t index = 0;
	int visualColumn = 0;

	while (index < line.size()) {
		if (line[index] != ' ' && line[index] != '\t') break;
		std::size_t next = index;
		std::size_t width = 0;
		if (!nextDisplayChar(line, next, width, visualColumn, settings)) break;
		visualColumn += static_cast<int>(width);
		index = next;
	}
	return visualColumn + 1;
}

std::string MRFileEditor::automaticIndentFillForCursor() const {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const std::size_t lineStart = lineStartOffset(cursorOffset());
	const int targetColumn = leadingIndentColumnForLine(lineStart);

	return buildEditIndentFill(settings, 1, targetColumn, configuredTabExpandSetting());
}

std::string MRFileEditor::smartIndentFillForCursor() {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const std::size_t cursor = cursorOffset();
	const std::size_t lineStart = lineStartOffset(cursor);
	const int baseColumn = leadingIndentColumnForLine(lineStart);
	int targetColumn = baseColumn;
	std::string lineText = mBufferModel.lineText(lineStart);
	std::size_t cursorInLine = cursor > lineStart ? std::min(cursor - lineStart, lineText.size()) : 0;
	std::string_view beforeCursor(lineText.data(), cursorInLine);
	std::string_view trimmedBeforeCursor = trimView(beforeCursor);
	const MRSyntaxLanguage language = mBufferModel.language();

	if (!settings.smartIndenting) return buildEditIndentFill(settings, 1, targetColumn, configuredTabExpandSetting());
	if (language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp || language == MRSyntaxLanguage::JavaScript || language == MRSyntaxLanguage::Json) {
		const std::size_t last = lastSignificantByte(beforeCursor);
		if (last != std::string_view::npos && (beforeCursor[last] == '{' || beforeCursor[last] == '[' || beforeCursor[last] == '('))
			targetColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
	} else if (language == MRSyntaxLanguage::Python) {
		const std::string upperLine = upperAscii(std::string(trimmedBeforeCursor));
		if (!upperLine.empty() && upperLine.back() == ':' && isPythonIndentLead(upperLine))
			targetColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
	} else if (language == MRSyntaxLanguage::Zsh) {
		const std::string upperLine = upperAscii(std::string(trimmedBeforeCursor));
		if (isShellIndentLead(trimmedBeforeCursor, upperLine))
			targetColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
	} else if (language == MRSyntaxLanguage::Perl) {
		if (isPerlIndentLead(trimmedBeforeCursor))
			targetColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
	} else if (language == MRSyntaxLanguage::Markdown) {
		int markdownColumn = 0;
		if (markdownContinuationColumn(beforeCursor, markdownColumn)) targetColumn = std::max(targetColumn, markdownColumn);
	}
	return buildEditIndentFill(settings, 1, targetColumn, configuredTabExpandSetting());
}

void MRFileEditor::applyLiveSmartDedentAfterTextInput(const std::string &insertedText) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const MRSyntaxLanguage language = mBufferModel.language();

	if (!settings.smartIndenting) return;
	if (insertedText.find('\n') != std::string::npos || insertedText.find('\r') != std::string::npos) return;
	if (language != MRSyntaxLanguage::C && language != MRSyntaxLanguage::Cpp && language != MRSyntaxLanguage::JavaScript && language != MRSyntaxLanguage::Json && language != MRSyntaxLanguage::Python &&
	    language != MRSyntaxLanguage::Zsh && language != MRSyntaxLanguage::Perl) return;

	const std::size_t lineStart = lineStartOffset(cursorOffset());
	const int baseColumn = leadingIndentColumnForLine(lineStart);
	if (baseColumn <= 1) return;

	const std::string lineText = mBufferModel.lineText(lineStart);
	const std::string_view trimmedLine = trimView(lineText);
	bool shouldDedent = false;

	if (language == MRSyntaxLanguage::Python) {
		const std::string upperLine = upperAscii(std::string(trimmedLine));
		shouldDedent = isPythonDedentLead(upperLine);
	} else if (language == MRSyntaxLanguage::Zsh) {
		const std::string upperLine = upperAscii(std::string(trimmedLine));
		shouldDedent = isShellDedentLead(trimmedLine, upperLine);
	} else
		shouldDedent = startsWithCloser(trimmedLine);

	if (!shouldDedent) return;
	applyCurrentLineLeadingIndent(prevResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn));
}

bool MRFileEditor::newLineWithPreferredIndent() {
	const std::string indentStyle = upperAscii(configuredEditSetupSettings().indentStyle);
	if (indentStyle == "AUTOMATIC") return newLineWithIndent(automaticIndentFillForCursor());
	if (indentStyle == "SMART") return newLineWithIndent(smartIndentFillForCursor());
	return newLineWithIndent(preferredIndentFill());
}

void MRFileEditor::effectiveFormatMargins(const MREditSetupSettings &settings, int &leftMargin, int &rightMargin) const noexcept {
	MRTextFormatting::effectiveMargins(settings, leftMargin, rightMargin);
}

bool MRFileEditor::persistVisibleEditSetupSettings(const MREditSetupSettings &settings, const std::string &errorPrefix) {
	MREditSetupSettings previousSettings = configuredEditSetupSettings();
	MRSettingsWriteReport writeReport;
	std::string errorText;

	if (!setConfiguredEditSetupSettings(settings, &errorText)) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return false;
	}
	if (!persistConfiguredSettingsSnapshot(&errorText, &writeReport)) {
		static_cast<void>(setConfiguredEditSetupSettings(previousSettings, nullptr));
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return false;
	}
	return true;
}

bool MRFileEditor::previewVisibleEditSetupSettings(const MREditSetupSettings &settings, const std::string &errorPrefix) {
	std::string errorText;

	if (!setConfiguredEditSetupSettings(settings, &errorText)) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return false;
	}
	refreshConfiguredVisualSettings();
	return true;
}

bool MRFileEditor::finalizeVisibleEditSetupPreview(const MREditSetupSettings &previousSettings, const std::string &errorPrefix) {
	MRSettingsWriteReport writeReport;
	std::string errorText;

	if (persistConfiguredSettingsSnapshot(&errorText, &writeReport)) return true;
	static_cast<void>(setConfiguredEditSetupSettings(previousSettings, nullptr));
	refreshConfiguredVisualSettings();
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
	return false;
}

void MRFileEditor::drawFormatRulerOverlay(const TextViewportGeometry &viewport, const MREditSetupSettings &settings) {
	TDrawBuffer buffer;
	unsigned char configured = 0;
	TColorAttr normal = static_cast<TColorAttr>(getColor(0x0606));
	const TColorAttr accent = static_cast<TColorAttr>(getColor(0x0404));
	const std::string normalized = normalizedFormatRulerLine(settings);

	if (configuredColorSlotOverride(kMrPaletteFormatRuler, configured)) normal = static_cast<TColorAttr>(configured);
	buffer.moveChar(0, ' ', normal, size.x);
	for (int x = 0; x < viewport.width; ++x) {
		const int column = viewport.deltaX + x + 1;
		const char ch = column >= 1 && column <= static_cast<int>(normalized.size()) ? normalized[static_cast<std::size_t>(column - 1)] : ' ';
		const bool atCursor = static_cast<int>(mBufferModel.lineIndex(mBufferModel.cursor())) == delta.y && displayedCursorColumn() == viewport.deltaX + x;
		buffer.moveChar(static_cast<ushort>(viewport.textLeft + x), ch, atCursor ? accent : normal, 1);
	}
	writeBuf(0, 0, size.x, 1, buffer);
}

bool MRFileEditor::editFormatRulerAtLocalPoint(TPoint local, ushort modifiers) {
	MREditSetupSettings settings = configuredEditSetupSettings();
	const TextViewportGeometry viewport = textViewportGeometryFor(settings);
	if (!settings.formatRuler || local.y != 0 || !viewport.containsTextX(local.x)) return false;
	const int column = viewport.textColumnFromLocalX(local.x) + 1;
	const std::string normalized = normalizedFormatRulerLine(settings);
	const char current = column >= 1 && column <= static_cast<int>(normalized.size()) ? normalized[static_cast<std::size_t>(column - 1)] : '.';
	char symbol = current == '|' ? '.' : '|';
	std::string updated;
	int leftMargin = settings.leftMargin;
	int rightMargin = settings.rightMargin;
	if ((modifiers & kbShift) != 0) symbol = 'L';
	else if ((modifiers & kbCtrlShift) != 0)
		symbol = 'R';
	else if ((modifiers & kbAltShift) != 0)
		symbol = '.';
	else if (column <= leftMargin)
		symbol = 'L';
	else if (column >= rightMargin)
		symbol = 'R';
	if (!editFormatLineAtColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, column, symbol, updated, &leftMargin, &rightMargin, nullptr)) return true;
	settings.formatLine = updated;
	settings.leftMargin = leftMargin;
	settings.rightMargin = rightMargin;
	if (!persistVisibleEditSetupSettings(settings, "Format ruler update failed: ")) return true;
	refreshConfiguredVisualSettings();
	return true;
}

bool MRFileEditor::dragFormatRulerAtLocalPoint(TEvent &event, TPoint local) {
	const MREditSetupSettings initialSettings = configuredEditSetupSettings();
	const TextViewportGeometry viewport = textViewportGeometryFor(initialSettings);
	const ushort modifiers = event.mouse.controlKeyState;
	const int startColumn = viewport.textColumnFromLocalX(local.x) + 1;
	bool dragged = false;

	if (!initialSettings.formatRuler || local.y != 0 || !viewport.containsTextX(local.x)) return false;
	while (mouseEvent(event, evMouseMove | evMouseAuto | evMouseUp)) {
		TPoint currentLocal = makeLocal(event.mouse.where);
		MREditSetupSettings preview = initialSettings;
		std::string translated;
		int leftMargin = initialSettings.leftMargin;
		int rightMargin = initialSettings.rightMargin;
		const int currentColumn = viewport.textColumnFromLocalX(currentLocal.x) + 1;
		const int delta = currentColumn - startColumn;

		if (event.what == evMouseUp) break;
		if (delta == 0) continue;
		dragged = true;
		if (!translateEditFormatLine(initialSettings.formatLine, initialSettings.tabSize, initialSettings.leftMargin, initialSettings.rightMargin, delta, translated, &leftMargin, &rightMargin, nullptr)) continue;
		preview.formatLine = translated;
		preview.leftMargin = leftMargin;
		preview.rightMargin = rightMargin;
		if (!previewVisibleEditSetupSettings(preview, "Format ruler drag failed: ")) return true;
	}
	if (!dragged) return editFormatRulerAtLocalPoint(local, modifiers);
	static_cast<void>(finalizeVisibleEditSetupPreview(initialSettings, "Format ruler drag failed: "));
	return true;
}

void MRFileEditor::draw() {
	MREditWindow *window = dynamic_cast<MREditWindow *>(owner);
	if (window != nullptr && window->isMinimized()) {
		syncScrollBarsToState();
		return;
	}
	syncScrollBarsToState();
	MREditSetupSettings editSettings = configuredEditSetupSettings();
	std::size_t totalLines = 1;
	TextViewportGeometry viewport = textViewportGeometryFor(editSettings);
	bool showLineNumbers = viewport.lineNumberWidth > 0;
	bool drawCodeFolding = viewport.codeFoldingWidth > 0;
	bool zeroFillLineNumbers = showLineNumbers && editSettings.lineNumZeroFill;
	int textWidth = viewport.width;
	MRTextBufferModel::Range selection = mBufferModel.selection().range().normalized();
	const MRSyntaxLanguage syntaxLanguage = mBufferModel.language();
	const bool statefulSyntax = isStatefulSyntaxLanguage(syntaxLanguage);
	MRMiniMapRenderer::Palette miniMapPalette = resolveMiniMapPalette();
	const bool drawMiniMap = viewport.miniMapBodyWidth > 0 && viewport.miniMapInfoX >= 0;
	const bool miniMapUseBraille = MRMiniMapRenderer::useBrailleRenderer();
	std::string viewportMarkerGlyph = MRMiniMapRenderer::normalizedViewportMarkerGlyph(editSettings.miniMapMarkerGlyph);
	const int miniMapRows = std::max(0, visibleTextRows());
	if (mBufferModel.exactLineCountKnown()) totalLines = std::max<std::size_t>(1, mBufferModel.lineCount());
	else
		totalLines = std::max<std::size_t>(1, std::max<std::size_t>(mBufferModel.estimatedLineCount(), static_cast<std::size_t>(std::max(delta.y, 0)) + static_cast<std::size_t>(std::max(miniMapRows, 1))));
	std::size_t topLine = static_cast<std::size_t>(std::max(delta.y, 0));
	if (topLine >= totalLines) topLine = totalLines - 1;
	std::size_t linePtr = lineStartForIndex(topLine);
	std::size_t lineIndex = topLine;
	const MRMiniMapRenderer::Viewport miniMapViewport = {viewport.width, viewport.miniMapBodyX, viewport.miniMapBodyWidth, viewport.miniMapInfoX, viewport.miniMapSeparatorX};
	if (drawMiniMap) applyMiniMapSignals(mMiniMapRenderer.scheduleWarmupIfNeeded(miniMapViewport, miniMapRows, miniMapUseBraille, totalLines, topLine, mBufferModel.documentId(), mBufferModel.version(), mBufferModel.readSnapshot(), editSettings));
	MRMiniMapRenderer::OverlayState miniMapOverlay = mMiniMapRenderer.computeOverlayState(mBufferModel.readSnapshot(), selection, mFindMarkerRanges, mDirtyRanges, totalLines, viewport.width, viewport.miniMapBodyWidth, miniMapUseBraille, editSettings);
	std::vector<MRSyntaxLineResult> visibleSyntaxLines;

	if (editSettings.formatRuler && viewport.topInset > 0) drawFormatRulerOverlay(viewport, editSettings);
	const int textRows = std::max(0, visibleTextRows());
	std::size_t immediateSyntaxLineBudget = 96;
	std::size_t immediateSyntaxLinesUsed = 0;
	if (statefulSyntax && mSyntaxWarmupTaskId != 0 && mSyntaxWarmupDocumentId == mBufferModel.documentId() && mSyntaxWarmupVersion == mBufferModel.version() && mSyntaxWarmupLanguage == syntaxLanguage &&
		topLine >= mSyntaxWarmupTopLine && topLine + static_cast<std::size_t>(textRows) <= mSyntaxWarmupBottomLine)
		immediateSyntaxLineBudget = 24;
	if (statefulSyntax && textRows > 0) {
		std::size_t preludeLines = static_cast<std::size_t>(textRows * 4);
		std::size_t stateTopLine = topLine > preludeLines ? topLine - preludeLines : 0;
		MRSyntaxCheckpointEntry checkpoint;
		MRSyntaxLineState state;
		std::vector<std::size_t> stateLineStarts;
		bool useCheckpointStart = false;

		if (syntaxCheckpointForLine(topLine, checkpoint) && checkpoint.lineIndex > stateTopLine) {
			stateTopLine = checkpoint.lineIndex;
			state = checkpoint.stateIn;
			useCheckpointStart = true;
		}
		const int stateRowCount = static_cast<int>(topLine - stateTopLine + static_cast<std::size_t>(textRows));

		if (useCheckpointStart) {
			stateLineStarts.reserve(static_cast<std::size_t>(std::max(stateRowCount, 0)));
			std::size_t stateLinePtr = checkpoint.lineStart;
			const std::size_t exactLineCount = mBufferModel.exactLineCountKnown() ? std::max<std::size_t>(1, mBufferModel.lineCount()) : 0;
			std::size_t stateLineIndex = checkpoint.lineIndex;
			for (int i = 0; i < stateRowCount; ++i) {
				if (mBufferModel.exactLineCountKnown() && stateLineIndex >= exactLineCount) break;
				stateLineStarts.push_back(stateLinePtr);
				++stateLineIndex;
				if (i + 1 >= stateRowCount || stateLinePtr >= mBufferModel.length()) break;
				std::size_t next = mBufferModel.nextLine(stateLinePtr);
				if (next <= stateLinePtr) break;
				stateLinePtr = next;
			}
		}
		if (stateLineStarts.empty()) stateLineStarts = syntaxWarmupLineStarts(stateTopLine, stateRowCount);
		const bool haveCompleteStatefulCache = hasSyntaxTokensForLineStarts(stateLineStarts, state);

		visibleSyntaxLines.reserve(static_cast<std::size_t>(textRows));
		for (std::size_t i = 0; i < stateLineStarts.size(); ++i) {
			std::size_t stateLinePtr = stateLineStarts[i];
			std::size_t stateLineIndex = stateTopLine + i;
			MRSyntaxLineResult syntaxLine;
			std::map<std::size_t, MRSyntaxCacheEntry>::iterator found = mSyntaxTokenCache.find(stateLinePtr);

			if (haveCompleteStatefulCache) syntaxLine = found->second.syntaxLine;
			else if (found != mSyntaxTokenCache.end() && found->second.stateIn == state) syntaxLine = found->second.syntaxLine;
			else if (immediateSyntaxLinesUsed < immediateSyntaxLineBudget) {
				syntaxLine = syntaxLineResultForLine(stateLinePtr, state);
				++immediateSyntaxLinesUsed;
			} else {
				for (std::size_t plainIndex = i; plainIndex < stateLineStarts.size(); ++plainIndex) {
					std::size_t plainLineIndex = stateTopLine + plainIndex;

					if (plainLineIndex >= topLine) visibleSyntaxLines.push_back(tmrHighlightTextLine(MRSyntaxLanguage::PlainText, mBufferModel.lineText(stateLineStarts[plainIndex])));
				}
				break;
			}
			rememberSyntaxCheckpoint(stateLinePtr, stateLineIndex, state);
			state = syntaxLine.stateOut;
			if (stateLineIndex >= topLine) visibleSyntaxLines.push_back(syntaxLine);
		}
	}
	for (int y = 0; y < textRows; ++y) {
		TDrawBuffer buffer;
		bool isDocumentLine = lineIndex < totalLines;
		bool drawEofMarker = editSettings.showEofMarker && lineIndex == totalLines;
		bool drawEofMarkerAsEmoji = drawEofMarker && editSettings.showEofMarkerEmoji;
		MRSyntaxLineResult syntaxLine;
		if (showLineNumbers) drawLineNumberGutter(buffer, lineIndex, isDocumentLine, viewport.lineNumberX, viewport.lineNumberWidth, zeroFillLineNumbers);
		if (drawCodeFolding) drawCodeFoldingGutter(buffer, viewport.codeFoldingX, viewport.codeFoldingWidth, linePtr, lineIndex);
		if (drawMiniMap) mMiniMapRenderer.drawGutter(buffer, y, miniMapRows, size.x, miniMapViewport, totalLines, topLine, miniMapUseBraille, viewportMarkerGlyph, miniMapPalette, miniMapOverlay);
		if (static_cast<std::size_t>(y) < visibleSyntaxLines.size()) syntaxLine = visibleSyntaxLines[static_cast<std::size_t>(y)];
		else {
			std::map<std::size_t, MRSyntaxCacheEntry>::iterator found = mSyntaxTokenCache.find(linePtr);

			if (found != mSyntaxTokenCache.end() && found->second.stateIn == MRSyntaxLineState()) syntaxLine = found->second.syntaxLine;
			else if (immediateSyntaxLinesUsed < immediateSyntaxLineBudget) {
				syntaxLine = syntaxLineResultForLine(linePtr, MRSyntaxLineState());
				++immediateSyntaxLinesUsed;
			} else
				syntaxLine = tmrHighlightTextLine(MRSyntaxLanguage::PlainText, mBufferModel.lineText(linePtr));
		}
		formatSyntaxLine(buffer, linePtr, syntaxLine, delta.x, textWidth, viewport.textLeft, isDocumentLine, drawEofMarker, drawEofMarkerAsEmoji);
		writeBuf(0, y + viewport.topInset, size.x, 1, buffer);
		if (linePtr < mBufferModel.length()) linePtr = mBufferModel.nextLine(linePtr);
		++lineIndex;
	}
	scheduleSyntaxWarmupIfNeeded();
	scheduleSaveNormalizationWarmupIfNeeded();
	updateIndicator();
}

void MRFileEditor::drawLineNumberGutter(TDrawBuffer &b, std::size_t lineIndex, bool showNumber, int drawX, int width, bool zeroFill) {
	TColorAttr color = static_cast<TColorAttr>(getColor(0x0606));
	char numberBuffer[32];
	int digits = std::max(1, width);

	if (width <= 0) return;
	b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
	if (!showNumber) return;
	if (zeroFill) std::snprintf(numberBuffer, sizeof(numberBuffer), "%0*lu", digits, static_cast<unsigned long>(lineIndex + 1));
	else
		std::snprintf(numberBuffer, sizeof(numberBuffer), "%*lu", digits, static_cast<unsigned long>(lineIndex + 1));
	b.moveStr(static_cast<ushort>(drawX), numberBuffer, color, static_cast<ushort>(width));
}

void MRFileEditor::drawCodeFoldingGutter(TDrawBuffer &b, int drawX, int width, std::size_t lineStart, std::size_t lineIndex) {
	unsigned char configured = 0;
	TColorAttr color = static_cast<TColorAttr>(getColor(0x0606));
	TColorAttr markerColor = static_cast<TColorAttr>(getColor(0x0404));
	char marker = ' ';

	if (width <= 0) return;
	if (configuredColorSlotOverride(kMrPaletteCodeFolding, configured)) color = static_cast<TColorAttr>(configured);
	b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
	if (lineIndex >= std::max<std::size_t>(1, mBufferModel.lineCount()) && mBufferModel.exactLineCountKnown()) return;

	const std::string lineText = mBufferModel.lineText(lineStart);
	const std::string_view trimmed = trimView(lineText);
	if (trimmed.empty()) return;

	std::string nextLineText;
	std::string_view nextTrimmed;
	std::size_t nextIndent = 0;
	if (lineStart < mBufferModel.length()) {
		const std::size_t nextLineStart = mBufferModel.nextLine(lineStart);
		if (nextLineStart > lineStart) {
			nextLineText = mBufferModel.lineText(nextLineStart);
			nextTrimmed = trimView(nextLineText);
			nextIndent = leadingIndentBytes(nextLineText);
		}
	}

	const MRSyntaxLanguage language = mBufferModel.language();
	const std::size_t currentIndent = leadingIndentBytes(lineText);
	const std::size_t last = lastSignificantByte(trimmed);
	const bool trailingBlockOpen = last != std::string_view::npos && (trimmed[last] == '{' || trimmed[last] == '[' || trimmed[last] == '(');
	const bool indentOpens = !nextTrimmed.empty() && nextIndent > currentIndent;
	bool foldStart = false;
	bool foldEnd = false;

	switch (language) {
		case MRSyntaxLanguage::C:
		case MRSyntaxLanguage::Cpp:
		case MRSyntaxLanguage::JavaScript:
		case MRSyntaxLanguage::Json:
			foldStart = trailingBlockOpen || isPreprocessorFoldStart(trimmed);
			foldEnd = startsWithCloser(trimmed) || isPreprocessorFoldEnd(trimmed);
			break;
		case MRSyntaxLanguage::Python: {
			const std::string upperLine = upperAscii(std::string(trimmed));
			foldStart = indentOpens || (!upperLine.empty() && upperLine.back() == ':' && isPythonIndentLead(upperLine));
			foldEnd = isPythonDedentLead(upperLine);
			break;
		}
		case MRSyntaxLanguage::Zsh: {
			const std::string upperLine = upperAscii(std::string(trimmed));
			foldStart = indentOpens || isShellIndentLead(trimmed, upperLine);
			foldEnd = isShellDedentLead(trimmed, upperLine);
			break;
		}
		case MRSyntaxLanguage::Perl:
			foldStart = indentOpens || isPerlIndentLead(trimmed);
			foldEnd = startsWithCloser(trimmed);
			break;
		case MRSyntaxLanguage::Markdown:
			foldStart = trimmed.starts_with("#") || isMarkdownFenceLine(trimmed) || (!nextTrimmed.empty() && isMarkdownSetextUnderline(nextTrimmed));
			break;
		case MRSyntaxLanguage::Make:
			foldStart = isMakeTargetLine(trimmed) && !nextTrimmed.empty() && !nextLineText.empty() && nextLineText.front() == '\t';
			break;
		case MRSyntaxLanguage::MRMAC:
			foldStart = trailingBlockOpen || indentOpens;
			foldEnd = startsWithCloser(trimmed);
			break;
		case MRSyntaxLanguage::PlainText:
		default:
			foldStart = indentOpens;
			break;
	}

	if (foldStart) marker = '+';
	else if (foldEnd)
		marker = '-';
	if (marker == ' ') return;
	b.moveChar(static_cast<ushort>(drawX + width / 2), marker, markerColor, 1);
}

TColorAttr MRFileEditor::tokenColor(MRSyntaxToken token, bool selected, TAttrPair pair) noexcept {
	TColorAttr normal = static_cast<TColorAttr>(pair);
	TColorAttr selectedAttr = static_cast<TColorAttr>(pair >> 8);
	uchar background = static_cast<uchar>((selected ? selectedAttr : normal) & 0xF0);
	auto configuredCodeColor = [background](unsigned char paletteSlot, unsigned char fallbackForeground) noexcept -> TColorAttr {
		unsigned char configured = 0;

		if (configuredColorSlotOverride(paletteSlot, configured)) return static_cast<TColorAttr>(background | (configured & 0x0F));
		return static_cast<TColorAttr>(background | fallbackForeground);
	};

	if (selected) return selectedAttr;
	switch (token) {
		case MRSyntaxToken::Keyword:
			return configuredCodeColor(kMrPaletteCodeKeywords, 0x0E);
		case MRSyntaxToken::Directive:
			return configuredCodeColor(kMrPaletteCodeDirectives, 0x0E);
		case MRSyntaxToken::Section:
		case MRSyntaxToken::Heading:
			return configuredCodeColor(kMrPaletteCodeKeywords, 0x0E);
		case MRSyntaxToken::Type:
			return configuredCodeColor(kMrPaletteCodeTypes, 0x0B);
		case MRSyntaxToken::Key:
			return configuredCodeColor(kMrPaletteCodeConstants, 0x0B);
		case MRSyntaxToken::Delimiter:
			return configuredCodeColor(kMrPaletteCodeDelimiters, 0x09);
		case MRSyntaxToken::Number:
			return configuredCodeColor(kMrPaletteCodeNumbers, 0x0A);
		case MRSyntaxToken::String:
			return configuredCodeColor(kMrPaletteCodeStrings, 0x0D);
		case MRSyntaxToken::Comment:
			return configuredCodeColor(kMrPaletteCodeComments, 0x03);
		default:
			return normal;
	}
}

std::vector<std::size_t> MRFileEditor::syntaxWarmupLineStarts(std::size_t topLine, int rowCount) const {
	std::vector<std::size_t> lineStarts;
	if (rowCount <= 0) return lineStarts;
	if (mBufferModel.exactLineCountKnown()) {
		const std::size_t exactLineCount = std::max<std::size_t>(1, mBufferModel.lineCount());
		if (topLine >= exactLineCount) return lineStarts;
		const std::size_t remainingLines = exactLineCount - topLine;
		if (remainingLines < static_cast<std::size_t>(rowCount)) rowCount = static_cast<int>(remainingLines);
	}

	std::size_t lineStart = lineStartForIndex(topLine);
	for (int i = 0; i < rowCount; ++i) {
		lineStarts.push_back(lineStart);
		if (i + 1 >= rowCount || lineStart >= mBufferModel.length()) break;
		std::size_t next = mBufferModel.nextLine(lineStart);
		if (next <= lineStart) break;
		lineStart = next;
	}
	return lineStarts;
}

bool MRFileEditor::syntaxCheckpointForLine(std::size_t lineIndex, MRSyntaxCheckpointEntry &checkpoint) const {
	std::map<std::size_t, MRSyntaxCheckpointEntry>::const_iterator found = mSyntaxCheckpoints.upper_bound(lineIndex);

	if (found == mSyntaxCheckpoints.begin()) return false;
	--found;
	checkpoint = found->second;
	return true;
}

void MRFileEditor::rememberSyntaxCheckpoint(std::size_t lineStart, std::size_t lineIndex, const MRSyntaxLineState &stateIn) noexcept {
	const std::size_t checkpointStride = 256;

	if (lineIndex != 0 && lineIndex % checkpointStride != 0) return;
	mSyntaxCheckpoints[lineIndex] = MRSyntaxCheckpointEntry(lineStart, lineIndex, stateIn);
}

MRSyntaxLineState MRFileEditor::syntaxWarmupInitialState(std::size_t lineStart) const noexcept {
	if (lineStart == 0) return MRSyntaxLineState();

	const std::size_t lineIndex = mBufferModel.lineIndex(lineStart);
	MRSyntaxCheckpointEntry checkpoint;

	if (syntaxCheckpointForLine(lineIndex, checkpoint) && checkpoint.lineIndex == lineIndex && checkpoint.lineStart == lineStart) return checkpoint.stateIn;
	return MRSyntaxLineState();
}

bool MRFileEditor::hasSyntaxTokensForLineStarts(const std::vector<std::size_t> &lineStarts, const MRSyntaxLineState &initialState) const {
	const bool statefulSyntax = isStatefulSyntaxLanguage(mBufferModel.language());
	MRSyntaxLineState state = initialState;

	for (std::size_t i = 0; i < lineStarts.size(); ++i) {
		std::map<std::size_t, MRSyntaxCacheEntry>::const_iterator found = mSyntaxTokenCache.find(lineStarts[i]);

		if (found == mSyntaxTokenCache.end()) return false;
		if (found->second.stateIn != state) return false;
		if (statefulSyntax) state = found->second.syntaxLine.stateOut;
		else
			state = MRSyntaxLineState();
	}
	return true;
}

MRSyntaxLineResult MRFileEditor::syntaxLineResultForLine(std::size_t lineStart, const MRSyntaxLineState &previousState) {
	std::map<std::size_t, MRSyntaxCacheEntry>::iterator found = mSyntaxTokenCache.find(lineStart);

	if (found != mSyntaxTokenCache.end() && found->second.stateIn == previousState) return found->second.syntaxLine;

	MRSyntaxLineResult syntaxLine = tmrHighlightTextLine(mBufferModel.language(), mBufferModel.lineText(lineStart), previousState);

	mSyntaxTokenCache[lineStart] = MRSyntaxCacheEntry(previousState, syntaxLine);
	return syntaxLine;
}

void MRFileEditor::formatSyntaxLine(TDrawBuffer &b, std::size_t lineStart, const MRSyntaxLineResult &syntaxLine, int hScroll, int width, int drawX, bool isDocumentLine, bool drawEofMarker, bool drawEofMarkerAsEmoji) {
	TAttrPair basePair = getColor(0x0201);
	TAttrPair changedPair = getColor(0x0505);
	TAttrPair selectionPair = getColor(0x0201);
	MRTextBufferModel::Range selection;
	std::size_t documentLength = mBufferModel.length();
	std::size_t lineEnd = lineStart;
	std::size_t cursorPos = 0;
	std::size_t lineIndex = 0;
	bool currentLine = false;
	bool currentLineInBlock = false;
	bool overlayActive = false;
	int overlayMode = 0;
	std::size_t overlayStart = 0;
	std::size_t overlayEnd = 0;
	std::size_t overlayLine1 = 0;
	std::size_t overlayLine2 = 0;
	int overlayCol1 = 0;
	int overlayCol2Exclusive = 0;
	std::size_t bytePos = 0;
	int visual = 0;
	int x = 0;
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const bool displayTabs = configuredDisplayTabs();

	hScroll = std::max(hScroll, 0);
	width = std::max(width, 0);
	drawX = std::max(drawX, 0);
	if (!isDocumentLine) {
		TColorAttr color = tokenColor(MRSyntaxToken::Text, false, basePair);
		b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
		if (drawEofMarker) drawEofMarkerGlyph(b, hScroll, width, drawX, basePair, drawEofMarkerAsEmoji);
		return;
	}
	std::string lineText = mBufferModel.lineText(lineStart);
	TStringView line(lineText.data(), lineText.size());
	selection = mBufferModel.selection().range();
	lineEnd = mBufferModel.nextLine(lineStart);
	lineIndex = mBufferModel.lineIndex(lineStart);
	cursorPos = mBufferModel.cursor();
	overlayActive = mBlockOverlayActive && mBlockOverlayMode >= 1 && mBlockOverlayMode <= 3;
	if (overlayActive) {
		const std::size_t trackedEnd = mBlockOverlayTrackingCursor ? std::min(mBufferModel.cursor(), documentLength) : std::min(mBlockOverlayEnd, documentLength);
		const std::size_t trackedAnchor = std::min(mBlockOverlayAnchor, documentLength);
		overlayMode = mBlockOverlayMode;
		overlayStart = std::min(trackedAnchor, trackedEnd);
		overlayEnd = std::max(trackedAnchor, trackedEnd);
		if (overlayMode == 1 || overlayMode == 2) {
			overlayLine1 = std::min(mBufferModel.lineIndex(trackedAnchor), mBufferModel.lineIndex(trackedEnd));
			overlayLine2 = std::max(mBufferModel.lineIndex(trackedAnchor), mBufferModel.lineIndex(trackedEnd));
		}
		if (overlayMode == 2) {
			const std::size_t aLineStart = mBufferModel.lineStart(trackedAnchor);
			const std::size_t bLineStart = mBufferModel.lineStart(trackedEnd);
			const int aCol = charColumn(aLineStart, trackedAnchor);
			const int bCol = charColumn(bLineStart, trackedEnd);
			overlayCol1 = std::min(aCol, bCol);
			overlayCol2Exclusive = std::max(aCol, bCol);
			if (overlayCol2Exclusive <= overlayCol1) overlayCol2Exclusive = overlayCol1 + 1;
		}
	}
	currentLine = (lineStart <= cursorPos && cursorPos < lineEnd) || (cursorPos == documentLength && lineStart == cursorPos && lineEnd == cursorPos);
	if (overlayActive) {
		if (overlayMode == 3) currentLineInBlock = currentLine && overlayStart < overlayEnd && overlayStart < lineEnd && overlayEnd > lineStart;
		else
			currentLineInBlock = currentLine && overlayLine1 <= lineIndex && lineIndex <= overlayLine2;
	} else
		currentLineInBlock = currentLine && selection.start < selection.end && selection.start < lineEnd && selection.end > lineStart;
	if (currentLineInBlock) basePair = getColor(0x0204);
	else if (currentLine)
		basePair = getColor(0x0303);

	std::size_t runIndex = 0;
	while (bytePos < line.size() && x < width) {
		std::size_t next = bytePos;
		std::size_t charWidth = 0;
		if (!nextDisplayChar(line, next, charWidth, visual, settings)) break;

		int nextVisual = visual + static_cast<int>(charWidth);
		if (nextVisual > hScroll) {
			std::size_t documentPos = lineStart + bytePos;
			MRSyntaxToken token = MRSyntaxToken::Text;
			bool selected = false;
			TAttrPair tokenPair;
			TColorAttr color;
			int visibleWidth = 0;

			while (runIndex < syntaxLine.tokenRuns.size()) {
				const MRSyntaxTokenRun &run = syntaxLine.tokenRuns[runIndex];
				const std::size_t runStart = static_cast<std::size_t>(run.column);
				const std::size_t runEnd = runStart + static_cast<std::size_t>(run.length);
				if (bytePos < runStart) break;
				if (bytePos < runEnd) {
					token = run.token;
					break;
				}
				++runIndex;
			}

			if (overlayActive) {
				if (overlayMode == 3) selected = overlayStart <= documentPos && documentPos < overlayEnd;
				else if (overlayMode == 1)
					selected = overlayLine1 <= lineIndex && lineIndex <= overlayLine2;
				else if (overlayMode == 2)
					selected = overlayLine1 <= lineIndex && lineIndex <= overlayLine2 && visual < overlayCol2Exclusive && nextVisual > overlayCol1;
			} else
				selected = selection.start <= documentPos && documentPos < selection.end;
			bool changedChar = !currentLine && !currentLineInBlock && isDirtyOffset(documentPos);
			TAttrPair effectivePair = changedChar ? changedPair : basePair;
			tokenPair = selected ? selectionPair : effectivePair;
			color = tokenColor(token, selected, tokenPair);
			visibleWidth = nextVisual - std::max(visual, hScroll);

			if (line[bytePos] == '\t' && displayTabs && visual >= hScroll && visibleWidth > 0) {
				b.moveStr(static_cast<ushort>(drawX + x), "\xE2\x96\xB6", color, 1);
				if (visibleWidth > 1) b.moveChar(static_cast<ushort>(drawX + x + 1), ' ', color, static_cast<ushort>(visibleWidth - 1));
			} else if (line[bytePos] == '\t' || visual < hScroll)
				b.moveChar(static_cast<ushort>(drawX + x), ' ', color, static_cast<ushort>(visibleWidth));
			else
				b.moveStr(static_cast<ushort>(drawX + x), line.substr(bytePos, next - bytePos), color, static_cast<ushort>(visibleWidth));
			x += visibleWidth;
		}
		visual = nextVisual;
		bytePos = next;
	}

	if (x < width) {
		TColorAttr color = tokenColor(MRSyntaxToken::Text, false, basePair);
		b.moveChar(static_cast<ushort>(drawX + x), ' ', color, static_cast<ushort>(width - x));
	}
}

void MRFileEditor::drawEofMarkerGlyph(TDrawBuffer &b, int hScroll, int width, int drawX, TAttrPair basePair, bool drawEmoji) {
	static const char *const kEofMarkerText = "EOF";
	static const char *const kEofMarkerEmoji = "\xF0\x9F\x94\x9A";
	const char *marker = drawEmoji ? kEofMarkerEmoji : kEofMarkerText;
	int markerWidth = 0;
	TColorAttr markerColor = tokenColor(MRSyntaxToken::Text, false, basePair);
	unsigned char configuredMarkerColor = 0;

	if (width <= 0 || hScroll != 0) return;
	if (!drawEmoji && mCustomWindowEofMarkerColorOverrideValid) markerColor = mCustomWindowEofMarkerColorOverride;
	else if (!drawEmoji && configuredColorSlotOverride(kMrPaletteEofMarker, configuredMarkerColor))
		markerColor = static_cast<TColorAttr>(configuredMarkerColor);
	markerWidth = std::max(1, strwidth(marker));
	markerWidth = std::min(markerWidth, width);
	b.moveStr(static_cast<ushort>(drawX), marker, markerColor, static_cast<ushort>(markerWidth));
}

bool MRFileEditor::wrapCurrentLineOnce(int leftMargin, int rightMargin) {
	const std::size_t cursor = cursorOffset();
	const std::size_t lineStart = lineStartOffset(cursor);
	const std::size_t lineEnd = lineEndOffset(cursor);
	const int safeLeftMargin = std::max(1, leftMargin);
	const int safeRightMargin = std::max(safeLeftMargin, rightMargin);
	const int lineWidth = charColumn(lineStart, lineEnd);
	const std::string indent(static_cast<std::size_t>(safeLeftMargin - 1), ' ');
	const std::string replacement = "\n" + indent;
	std::size_t limitOffset = std::min(charPtrOffset(lineStart, safeRightMargin), lineEnd);
	std::size_t replaceStart = limitOffset;
	std::size_t replaceEnd = limitOffset;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "live-word-wrap-line");
	std::size_t newCursor = cursor;

	if (lineWidth <= safeRightMargin) return false;
	for (std::size_t probe = limitOffset; probe > lineStart; probe = prevCharOffset(probe)) {
		const std::size_t candidate = prevCharOffset(probe);
		const char ch = charAtOffset(candidate);

		if (ch != ' ' && ch != '\t') continue;
		replaceStart = candidate;
		replaceEnd = probe;
		while (replaceStart > lineStart) {
			const std::size_t previous = prevCharOffset(replaceStart);
			const char previousChar = charAtOffset(previous);
			if (previousChar != ' ' && previousChar != '\t') break;
			replaceStart = previous;
		}
		while (replaceEnd < lineEnd) {
			const char nextChar = charAtOffset(replaceEnd);
			if (nextChar != ' ' && nextChar != '\t') break;
			replaceEnd = nextCharOffset(replaceEnd);
		}
		break;
	}
	transaction.replace(MRTextBufferModel::Range(replaceStart, replaceEnd), replacement);
	if (cursor <= replaceStart) newCursor = cursor;
	else if (cursor >= replaceEnd)
		newCursor = cursor - (replaceEnd - replaceStart) + replacement.size();
	else
		newCursor = replaceStart + replacement.size();
	return applyStagedTransaction(transaction, newCursor, newCursor, newCursor, true).applied();
}

void MRFileEditor::applyLiveWordWrapAfterTextInput() {
	MREditSetupSettings settings = configuredEditSetupSettings();
	int leftMargin = 1;
	int rightMargin = 78;

	if (mReadOnly || !settings.wordWrap) return;
	effectiveFormatMargins(settings, leftMargin, rightMargin);
	for (int wraps = 0; wraps < 64; ++wraps)
		if (!wrapCurrentLineOnce(leftMargin, rightMargin)) break;
}

void MRFileEditor::ensureCursorVisible(bool centerCursor) {
	int visualColumn = displayedCursorColumn();
	int line = static_cast<int>(mBufferModel.lineIndex(mBufferModel.cursor()));
	int targetX = delta.x;
	int targetY = delta.y;
	int viewportWidth = textViewportWidth();
	int textRows = std::max(1, visibleTextRows());

	if (visualColumn < targetX) targetX = visualColumn;
	else if (visualColumn >= targetX + viewportWidth)
		targetX = visualColumn - viewportWidth + 1;
	if (centerCursor) targetY = std::max(0, line - textRows / 2);
	else if (line < targetY)
		targetY = line;
	else if (line >= targetY + textRows)
		targetY = line - textRows + 1;
	if (targetX != delta.x || targetY != delta.y) scrollTo(targetX, targetY);
}

void MRFileEditor::moveCursor(std::size_t target, bool extendSelection, bool centerCursor, int requestedVisualColumn) {
	target = canonicalCursorOffset(std::min(target, mBufferModel.length()));
	if (extendSelection) {
		std::size_t anchor = mBufferModel.hasSelection() ? mBufferModel.selection().anchor : mBufferModel.cursor();
		mSelectionAnchor = anchor;
		mBufferModel.setCursorAndSelection(target, anchor, target);
	} else {
		if (configuredPersistentBlocksSetting() && mBufferModel.hasSelection()) mBufferModel.setCursor(target);
		else
			mBufferModel.setCursorAndSelection(target, target, target);
		mSelectionAnchor = target;
	}
	if (freeCursorMovementEnabled() && requestedVisualColumn >= 0) mCursorVisualColumn = std::max(actualCursorVisualColumn(target), requestedVisualColumn);
	else
		mCursorVisualColumn = actualCursorVisualColumn(target);
	if (useApproximateLargeFileMetrics()) updateMetrics();
	ensureCursorVisible(centerCursor);
	scheduleSyntaxWarmupIfNeeded();
	updateIndicator();
	drawView();
}

bool MRFileEditor::isTextInputEvent(const TEvent &event) const {
	if (event.what != evKeyDown) return false;
	const ushort mods = event.keyDown.controlKeyState;
	const bool plainTab = (event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) && (mods & (kbShift | kbCtrlShift | kbAltShift | kbPaste)) == 0;
	return (event.keyDown.controlKeyState & kbPaste) != 0 || event.keyDown.textLength > 0 || plainTab || (event.keyDown.charScan.charCode >= 32 && event.keyDown.charScan.charCode < 255);
}

void MRFileEditor::handleTextInput(TEvent &event) {
	if (mReadOnly) {
		clearEvent(event);
		return;
	}
	if ((event.keyDown.controlKeyState & kbPaste) != 0) {
		char buf[512];
		size_t length = 0;
		while (textEvent(event, TSpan<char>(buf, sizeof(buf)), length)) {
			const std::string insertedText(buf, length);
			if (insertBufferText(insertedText)) applyLiveSmartDedentAfterTextInput(insertedText);
		}
		applyLiveWordWrapAfterTextInput();
		clearEvent(event);
		return;
	}

	const ushort mods = event.keyDown.controlKeyState;
	const bool plainTab = (event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) && (mods & (kbShift | kbCtrlShift | kbAltShift | kbPaste)) == 0;
	std::string insertedText;

	if (event.keyDown.textLength > 0) insertedText.assign(event.keyDown.text, event.keyDown.textLength);
	else if (plainTab)
		insertedText = tabKeyText();
	else
		insertedText.assign(1, static_cast<char>(event.keyDown.charScan.charCode));
	if (insertBufferText(insertedText)) applyLiveSmartDedentAfterTextInput(insertedText);
	applyLiveWordWrapAfterTextInput();
	clearEvent(event);
}

std::string MRFileEditor::tabKeyText() const {
	if (configuredTabExpandSetting()) return "\t";
	std::size_t insertPos = mBufferModel.cursor();
	const MREditSetupSettings settings = configuredEditSetupSettings();
	if (mBufferModel.hasSelection()) insertPos = mBufferModel.selection().range().start;
	int visualColumn = freeCursorMovementEnabled() && insertPos == mBufferModel.cursor() && !mBufferModel.hasSelection() ? displayedCursorColumn() : charColumn(mBufferModel.lineStart(insertPos), insertPos);
	return std::string(static_cast<std::size_t>(tabDisplayWidth(settings, visualColumn)), ' ');
}

void MRFileEditor::handleEvent(TEvent &event) {
	if (event.what == evKeyDown) {
		const ushort mods = event.keyDown.controlKeyState;
		const bool shiftTabPressed = event.keyDown.keyCode == kbShiftTab || ((event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) && hasShiftModifier(mods));
		if (shiftTabPressed) {
			handleKeyDown(event);
			return;
		}
	}

	TScroller::handleEvent(event);

	if (event.what == evBroadcast) {
		if (event.message.command == cmScrollBarClicked && (event.message.infoPtr == hScrollBar || event.message.infoPtr == vScrollBar)) {
			select();
			clearEvent(event);
			return;
		}
		if (event.message.command == cmScrollBarChanged && (event.message.infoPtr == hScrollBar || event.message.infoPtr == vScrollBar)) {
			clearEvent(event);
			return;
		}
	}

	switch (event.what) {
		case evMouseDown:
			handleMouse(event);
			break;
		case evMouseWheel:
			if (vScrollBar != nullptr) vScrollBar->handleEvent(event);
			if (event.what != evNothing && hScrollBar != nullptr) hScrollBar->handleEvent(event);
			break;
		case evKeyDown:
			handleKeyDown(event);
			break;
		case evCommand:
			handleCommand(event);
			break;
		default:
			break;
	}
}

void MRFileEditor::scrollDraw() {
	int newDeltaX = hScrollBar != nullptr ? hScrollBar->value : 0;
	int newDeltaY = vScrollBar != nullptr ? vScrollBar->value : 0;

	if (newDeltaX != delta.x || newDeltaY != delta.y) {
		delta.x = newDeltaX;
		delta.y = newDeltaY;
		if (useApproximateLargeFileMetrics()) updateMetrics();
		scheduleSyntaxWarmupIfNeeded();
		drawView();
	} else {
		if (useApproximateLargeFileMetrics()) updateMetrics();
		scheduleSyntaxWarmupIfNeeded();
		updateIndicator();
	}
}

void MRFileEditor::setState(ushort aState, Boolean enable) {
	TScroller::setState(aState, enable);
	if ((aState & (sfActive | sfSelected)) != 0) syncScrollBarsToState();
	MREditWindow *window = dynamic_cast<MREditWindow *>(owner);
	if (window != nullptr && window->isMinimized()) return;
	if (aState == sfCursorVis || mIndicatorUpdateInProgress) return;
	updateIndicator();
}

void MRFileEditor::handleKeyDown(TEvent &event) {
	ushort key = ctrlToArrow(event.keyDown.keyCode);
	const ushort mods = event.keyDown.controlKeyState;
	bool extend = hasShiftModifier(mods);
	const bool shiftTabPressed = event.keyDown.keyCode == kbShiftTab || ((event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) && hasShiftModifier(mods));

	if (shiftTabPressed) {
		const std::size_t target = tabStopMoveOffset(cursorOffset(), false);
		if (target != cursorOffset()) setPreferredIndentColumn(charColumn(lineStartOffset(target), target) + 1);
		moveCursor(target, false, false);
		clearEvent(event);
		return;
	}

	if (isTextInputEvent(event)) {
		handleTextInput(event);
		return;
	}

	switch (key) {
		case kbLeft:
			moveCursor(prevCharOffset(cursorOffset()), extend, false);
			break;
		case kbRight:
			moveCursor(nextCharOffset(cursorOffset()), extend, false);
			break;
		case kbUp:
			moveCursor(lineMoveOffset(cursorOffset(), -1, displayedCursorColumn()), extend, false, displayedCursorColumn());
			break;
		case kbDown:
			moveCursor(lineMoveOffset(cursorOffset(), 1, displayedCursorColumn()), extend, false, displayedCursorColumn());
			break;
		case kbHome:
			moveCursor(mAutoIndent ? charPtrOffset(lineStartOffset(cursorOffset()), 0) : lineStartOffset(cursorOffset()), extend, false);
			break;
		case kbEnd:
			moveCursor(lineEndOffset(cursorOffset()), extend, false);
			break;
		case kbPgUp:
			moveCursor(lineMoveOffset(cursorOffset(), -(std::max(2, visibleTextRows()) - 1), displayedCursorColumn()), extend, true, displayedCursorColumn());
			break;
		case kbPgDn:
			moveCursor(lineMoveOffset(cursorOffset(), std::max(2, visibleTextRows()) - 1, displayedCursorColumn()), extend, true, displayedCursorColumn());
			break;
		case kbCtrlHome:
			moveCursor(0, false, false);
			break;
		case kbCtrlEnd:
			moveCursor(bufferLength(), false, false);
			break;
		case kbCtrlLeft:
			moveCursor(prevWordOffset(cursorOffset()), extend, false);
			break;
		case kbCtrlRight:
			moveCursor(nextWordOffset(cursorOffset()), extend, false);
			break;
		case kbEnter:
			if (!mReadOnly) newLineWithPreferredIndent();
			clearEvent(event);
			return;
		case kbBack:
			if (!mReadOnly) {
				if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
				else if (cursorOffset() > 0) replaceRangeAndSelect(static_cast<uint>(prevCharOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
			}
			clearEvent(event);
			return;
		case kbDel:
			if (!mReadOnly) {
				if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
				else
					deleteCharsAtCursor(1);
			}
			clearEvent(event);
			return;
		case kbIns:
			setInsertModeEnabled(!insertModeEnabled());
			clearEvent(event);
			return;
		case kbShiftIns:
			requestSystemClipboardPaste();
			clearEvent(event);
			return;
		case kbCtrlIns:
			copySelection();
			clearEvent(event);
			return;
		case kbShiftDel:
			cutSelection();
			clearEvent(event);
			return;
		default:
			return;
	}
	clearEvent(event);
}

void MRFileEditor::handleCommand(TEvent &event) {
	switch (event.message.command) {
		case cmSave:
			saveInPlace();
			break;
		case cmSaveAs:
			saveAsWithPrompt();
			break;
		case cmCut:
			cutSelection();
			break;
		case cmCopy:
			copySelection();
			break;
		case cmPaste:
			requestSystemClipboardPaste();
			break;
		case cmMrEditUndo: {
			MRTextBufferModel::CustomUndoRecord record;
			if (mBufferModel.undo(&record)) {
				const bool modifiedState = mBufferModel.isModified();
				adoptCommittedDocument(mBufferModel.document(), mBufferModel.cursor(), mBufferModel.selectionStart(), mBufferModel.selectionEnd(), modifiedState);
				if (owner != nullptr) setBlockOverlayState(record.blockMode, record.blockAnchor, record.blockEnd, record.blockMarkingOn, false);
			}
			break;
		}
		case cmMrEditRedo: {
			MRTextBufferModel::CustomUndoRecord record;
			if (mBufferModel.redo(&record)) {
				const bool modifiedState = mBufferModel.isModified();
				adoptCommittedDocument(mBufferModel.document(), mBufferModel.cursor(), mBufferModel.selectionStart(), mBufferModel.selectionEnd(), modifiedState);
				if (owner != nullptr) setBlockOverlayState(record.blockMode, record.blockAnchor, record.blockEnd, record.blockMarkingOn, false);
			}
			break;
		}
		case cmMrTextUpperCaseMenu:
			convertSelectionToUpperCase();
			break;
		case cmMrTextLowerCaseMenu:
			convertSelectionToLowerCase();
			break;
		case cmMrTextCenterLine:
			if (!mReadOnly) {
				MREditSetupSettings settings = configuredEditSetupSettings();
				centerCurrentLine(settings.leftMargin, settings.rightMargin > 0 ? settings.rightMargin : 78);
			}
			break;
		case cmMrTextReformatParagraph:
			if (!mReadOnly) {
				MREditSetupSettings settings = configuredEditSetupSettings();
				formatParagraph(settings.leftMargin, settings.rightMargin > 0 ? settings.rightMargin : 78);
			}
			break;
		case cmClear:
			if (!mReadOnly) replaceSelectionText(std::string());
			break;
		case cmCharLeft:
			moveCursor(prevCharOffset(cursorOffset()), false, false);
			break;
		case cmCharRight:
			moveCursor(nextCharOffset(cursorOffset()), false, false);
			break;
		case cmWordLeft:
			moveCursor(prevWordOffset(cursorOffset()), false, false);
			break;
		case cmWordRight:
			moveCursor(nextWordOffset(cursorOffset()), false, false);
			break;
		case cmLineStart:
			moveCursor(lineStartOffset(cursorOffset()), false, false);
			break;
		case cmLineEnd:
			moveCursor(lineEndOffset(cursorOffset()), false, false);
			break;
		case cmLineUp:
			moveCursor(lineMoveOffset(cursorOffset(), -1, displayedCursorColumn()), false, false, displayedCursorColumn());
			break;
		case cmLineDown:
			moveCursor(lineMoveOffset(cursorOffset(), 1, displayedCursorColumn()), false, false, displayedCursorColumn());
			break;
		case cmPageUp:
			moveCursor(lineMoveOffset(cursorOffset(), -(std::max(2, visibleTextRows()) - 1), displayedCursorColumn()), false, true, displayedCursorColumn());
			break;
		case cmPageDown:
			moveCursor(lineMoveOffset(cursorOffset(), std::max(2, visibleTextRows()) - 1, displayedCursorColumn()), false, true, displayedCursorColumn());
			break;
		case cmTextStart:
			moveCursor(0, false, false);
			break;
		case cmTextEnd:
			moveCursor(bufferLength(), false, false);
			break;
		case cmNewLine:
			if (!mReadOnly) newLineWithPreferredIndent();
			break;
		case cmBackSpace:
			if (!mReadOnly) {
				if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
				else if (cursorOffset() > 0) replaceRangeAndSelect(static_cast<uint>(prevCharOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
			}
			break;
		case cmDelChar:
			if (!mReadOnly) {
				if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
				else
					deleteCharsAtCursor(1);
			}
			break;
		case cmDelWord:
			if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(cursorOffset()), static_cast<uint>(nextWordOffset(cursorOffset())), "", 0);
			break;
		case cmDelWordLeft:
			if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(prevWordOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
			break;
		case cmDelStart:
			if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(lineStartOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
			break;
		case cmDelEnd:
			if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(cursorOffset()), static_cast<uint>(lineEndOffset(cursorOffset())), "", 0);
			break;
		case cmDelLine:
			if (!mReadOnly) deleteCurrentLineText();
			break;
		case cmInsMode:
			setInsertModeEnabled(!insertModeEnabled());
			break;
		case cmSelectAll:
			mSelectionAnchor = 0;
			mBufferModel.setCursorAndSelection(mBufferModel.length(), 0, mBufferModel.length());
			revealCursor(True);
			break;
		default:
			return;
	}
	clearEvent(event);
}

void MRFileEditor::handleMouse(TEvent &event) {
	const TextViewportGeometry viewport = textViewportGeometry();

	if ((event.mouse.buttons & mbLeftButton) == 0) return;
	if (dragFormatRulerAtLocalPoint(event, makeLocal(event.mouse.where))) {
		clearEvent(event);
		return;
	}

	select();
	std::size_t anchor = (event.mouse.controlKeyState & kbShift) != 0 && mBufferModel.hasSelection() ? mBufferModel.selection().anchor : mBufferModel.cursor();
	int targetColumn = 0;
	mSelectionAnchor = anchor;
	moveCursor(mouseOffset(makeLocal(event.mouse.where), &targetColumn), (event.mouse.controlKeyState & kbShift) != 0, false, targetColumn);

	while (mouseEvent(event, evMouseMove | evMouseAuto)) {
		if (event.what == evMouseAuto) {
			TPoint mouse = makeLocal(event.mouse.where);
			int dx = delta.x;
			int dy = delta.y;
			if (mouse.x < 0) --dx;
			else if (mouse.x >= size.x)
				++dx;
			if (mouse.y < viewport.topInset) --dy;
			else if (mouse.y >= viewport.topInset + std::max(0, visibleTextRows()))
				++dy;
			scrollTo(std::max(dx, 0), std::max(dy, 0));
		}
		int dragColumn = 0;
		std::size_t target = mouseOffset(makeLocal(event.mouse.where), &dragColumn);
		mBufferModel.setCursorAndSelection(target, mSelectionAnchor, target);
		if (freeCursorMovementEnabled()) mCursorVisualColumn = std::max(actualCursorVisualColumn(target), dragColumn);
		else
			mCursorVisualColumn = actualCursorVisualColumn(target);
		updateIndicator();
		drawView();
	}
	clearEvent(event);
}

std::size_t MRFileEditor::mouseOffset(TPoint local, int *visualColumnOut) noexcept {
	TextViewportGeometry viewport = textViewportGeometry();
	const int textRows = std::max(1, visibleTextRows());
	int clampedY = std::max(0, std::min(local.y - viewport.topInset, textRows - 1));
	int row = clampedY + delta.y;
	int column = viewport.textColumnFromLocalX(local.x);
	std::size_t start = lineStartForIndex(static_cast<std::size_t>(std::max(row, 0)));
	if (visualColumnOut != nullptr) *visualColumnOut = column;
	return canonicalCursorOffset(charPtrOffset(start, column));
}

std::size_t MRFileEditor::canonicalCursorOffset(std::size_t pos) const noexcept {
	pos = std::min(pos, mBufferModel.length());
	if (pos > 0 && pos < mBufferModel.length() && mBufferModel.charAt(pos) == '\n' && mBufferModel.charAt(pos - 1) == '\r') return pos - 1;
	return pos;
}

void MRFileEditor::copySelection() {
	if (!mBufferModel.hasSelection()) return;
	MRTextBufferModel::Range range = mBufferModel.selection().range();
	const std::string text = mBufferModel.text().substr(range.start, range.length());
	TClipboard::setText(TStringView(text.data(), text.size()));
}

void MRFileEditor::cutSelection() {
	if (mReadOnly || !mBufferModel.hasSelection()) return;
	copySelection();
	replaceSelectionText(std::string());
}

void MRFileEditor::requestSystemClipboardPaste() {
	if (mReadOnly) return;
	TClipboard::requestText();
}

void MRFileEditor::replaceSelectionText(const std::string &text) {
	if (!mBufferModel.hasSelection()) {
		if (!text.empty()) insertBufferText(text);
		return;
	}
	MRTextBufferModel::Range range = mBufferModel.selection().range();
	replaceRangeAndSelect(static_cast<uint>(range.start), static_cast<uint>(range.end), text.data(), static_cast<uint>(text.size()));
}

void MRFileEditor::convertSelectionToUpperCase() {
	if (mReadOnly || !mBufferModel.hasSelection()) return;
	MRTextBufferModel::Range range = mBufferModel.selection().range();
	std::string text = mBufferModel.text().substr(range.start, range.length());
	for (char &c : text)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	replaceSelectionText(text);
	setSelectionOffsets(range.start, range.start + text.length());
}

void MRFileEditor::convertSelectionToLowerCase() {
	if (mReadOnly || !mBufferModel.hasSelection()) return;
	MRTextBufferModel::Range range = mBufferModel.selection().range();
	std::string text = mBufferModel.text().substr(range.start, range.length());
	for (char &c : text)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	replaceSelectionText(text);
	setSelectionOffsets(range.start, range.start + text.length());
}

bool MRFileEditor::writeDocumentToPath(const char *targetPath) {
	char drive[MAXDRIVE];
	char dir[MAXDIR];
	char file[MAXFILE];
	char ext[MAXEXT];
	MRTextSaveOptions saveOptions;
	const std::size_t pieceCount = mBufferModel.document().pieceCount();

	resolveSaveOptionsForPath(targetPath, saveOptions);

	if (configuredBackupFilesSetting()) {
		fnsplit(targetPath, drive, dir, file, ext);
		char backupName[MAXPATH];
		fnmerge(backupName, drive, dir, file, ".bak");
		unlink(backupName);
		rename(targetPath, backupName);
	}

	std::ofstream out(targetPath, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out) {
		TEditor::editorDialog(edCreateError, targetPath);
		return false;
	}
	auto failWrite = [&]() -> bool {
		TEditor::editorDialog(edWriteError, targetPath);
		return false;
	};

	if (saveOptions.binaryMode) {
		for (std::size_t i = 0; i < pieceCount; ++i) {
			mr::editor::PieceChunkView chunk = mBufferModel.document().pieceChunk(i);
			writeChunk(out, chunk.data, chunk.length);
			if (!out) return failWrite();
		}
		return true;
	}
	const std::size_t sourceBytes = mBufferModel.document().length();
	const auto normalizeStartedAt = std::chrono::steady_clock::now();
	const std::size_t flushThresholdBytes = static_cast<std::size_t>(256) * 1024;
	MRTextSaveStreamState normalizeState;
	std::string outputBuffer;
	auto flushOutput = [&]() -> bool {
		if (outputBuffer.empty()) return true;
		writeChunk(out, outputBuffer.data(), outputBuffer.size());
		outputBuffer.clear();
		return static_cast<bool>(out);
	};

	outputBuffer.reserve(flushThresholdBytes + 1024);
	for (std::size_t i = 0; i < pieceCount; ++i) {
		mr::editor::PieceChunkView chunk = mBufferModel.document().pieceChunk(i);
		if (chunk.length == 0) continue;
		appendNormalizedTextSaveChunk(std::string_view(chunk.data, chunk.length), saveOptions, normalizeState, outputBuffer);
		if (outputBuffer.size() >= flushThresholdBytes && !flushOutput()) return failWrite();
	}
	finalizeNormalizedTextSaveStream(saveOptions, normalizeState, outputBuffer);
	if (!flushOutput()) return failWrite();

	noteSaveNormalizationThroughput(sourceBytes, static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - normalizeStartedAt).count()));
	if (!out) return failWrite();
	return true;
}

void MRFileEditor::scheduleLineIndexWarmupIfNeeded() {
	if (!mBufferModel.document().hasMappedOriginal() || mBufferModel.document().exactLineCountKnown()) {
		std::uint64_t cancelledTaskId = mLineIndexWarmupTaskId;
		bool hadTask = cancelledTaskId != 0;
		mLineIndexWarmupTaskId = 0;
		mLineIndexWarmupDocumentId = 0;
		mLineIndexWarmupVersion = 0;
		if (hadTask) {
			if (shouldTraceLargeFileDiagnostics()) traceLargeFileMessage("line-index-cancel", "reason=exact-line-count-known");
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
			notifyWindowTaskStateChanged();
		}
		return;
	}

	const std::size_t docId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	if (mLineIndexWarmupTaskId != 0 && mLineIndexWarmupDocumentId == docId && mLineIndexWarmupVersion == version) return;

	MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
	std::uint64_t previousTaskId = mLineIndexWarmupTaskId;
	if (previousTaskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
	mLineIndexWarmupDocumentId = docId;
	mLineIndexWarmupVersion = version;
	mLineIndexWarmupTaskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::LineIndexWarmup, docId, version, lineIndexWarmupTaskLabel(), [snapshot](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
		mr::coprocessor::Result result;
		mr::editor::LineIndexWarmupData warmup;
		static constexpr std::size_t kWorkerStrideBudget = 2;
		result.task = info;
		if (stopToken.stop_requested() || info.cancelRequested()) {
			result.status = mr::coprocessor::TaskStatus::Cancelled;
			return result;
		}
		if (!snapshot.warmLineIndexChunk(warmup, kWorkerStrideBudget, stopToken, info.cancelFlag.get())) {
			result.status = mr::coprocessor::TaskStatus::Cancelled;
			return result;
		}
		result.status = mr::coprocessor::TaskStatus::Completed;
		result.payload = std::make_shared<mr::coprocessor::LineIndexWarmupPayload>(warmup);
		return result;
	});
	if (shouldTraceLargeFileDiagnostics()) {
		std::ostringstream detail;
		detail << "task=" << mLineIndexWarmupTaskId << " estimated_lines=" << mBufferModel.estimatedLineCount() << " cursor_line=" << mBufferModel.lineIndex(mBufferModel.cursor()) << " delta_y=" << delta.y;
		traceLargeFileMessage("line-index-schedule", detail.str());
	}
	if (mLineIndexWarmupTaskId != previousTaskId) notifyWindowTaskStateChanged();
}

void MRFileEditor::scheduleSyntaxWarmupIfNeeded() {
	const int textRows = visibleTextRows();

	if (mBufferModel.language() == MRSyntaxLanguage::PlainText || textRows <= 0) {
		resetSyntaxWarmupState(true);
		return;
	}

	const std::size_t docId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	const MRSyntaxLanguage language = mBufferModel.language();
	const bool exactLineCountKnown = mBufferModel.exactLineCountKnown();
	const std::size_t exactLineCount = exactLineCountKnown ? std::max<std::size_t>(1, mBufferModel.lineCount()) : 0;
	std::size_t visibleTopLine = static_cast<std::size_t>(std::max(delta.y - 4, 0));
	if (exactLineCountKnown && visibleTopLine >= exactLineCount) visibleTopLine = exactLineCount - 1;
	const int rowBudget = std::max(textRows + 8, 8);
	const int backgroundRowBudget = rowBudget * 3;
	const bool statefulSyntax = isStatefulSyntaxLanguage(language);
	MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
	std::vector<std::size_t> visibleLineStarts = syntaxWarmupLineStarts(visibleTopLine, rowBudget);
	if (visibleLineStarts.empty()) return;

	if (mSyntaxPrefetchDocumentId != docId || mSyntaxPrefetchVersion != version || mSyntaxPrefetchLanguage != language) {
		mSyntaxPrefetchDocumentId = docId;
		mSyntaxPrefetchVersion = version;
		mSyntaxPrefetchTargetBottomLine = visibleTopLine;
		mSyntaxPrefetchReachedBottomLine = visibleTopLine;
		mSyntaxPrefetchLanguage = language;
	}
	mSyntaxPrefetchTargetBottomLine = std::max(mSyntaxPrefetchTargetBottomLine, visibleTopLine + static_cast<std::size_t>(backgroundRowBudget));
	if (exactLineCountKnown) {
		if (mSyntaxPrefetchTargetBottomLine > exactLineCount) mSyntaxPrefetchTargetBottomLine = exactLineCount;
		if (mSyntaxPrefetchReachedBottomLine > exactLineCount) mSyntaxPrefetchReachedBottomLine = exactLineCount;
	}
	auto buildSyntaxRequest = [&](std::size_t requestTopLine, int requiredChunkRows, int warmupChunkRows, std::vector<std::size_t> &requiredLineStarts, std::vector<std::size_t> &warmupLineStarts,
	                              MRSyntaxLineState &requiredState, std::size_t &requestBottomLine) {
		requiredLineStarts = syntaxWarmupLineStarts(requestTopLine, requiredChunkRows);
		warmupLineStarts = requiredLineStarts;
		requiredState = MRSyntaxLineState();
		requestBottomLine = requestTopLine + requiredLineStarts.size();
		if (exactLineCountKnown && requestBottomLine > exactLineCount) requestBottomLine = exactLineCount;
		if (requiredLineStarts.empty()) return;

		if (statefulSyntax) {
			std::size_t preludeLines = static_cast<std::size_t>(rowBudget * 4);
			std::size_t stateTopLine = requestTopLine > preludeLines ? requestTopLine - preludeLines : 0;
			MRSyntaxCheckpointEntry checkpoint;
			bool useCheckpointStart = false;

			if (syntaxCheckpointForLine(requestTopLine, checkpoint) && checkpoint.lineIndex > stateTopLine) {
				stateTopLine = checkpoint.lineIndex;
				requiredState = checkpoint.stateIn;
				useCheckpointStart = true;
			}
			const int requiredRowCount = static_cast<int>(requestTopLine - stateTopLine + requiredLineStarts.size());
			const int warmupRowCount = static_cast<int>(requestTopLine - stateTopLine + static_cast<std::size_t>(warmupChunkRows));

			if (useCheckpointStart) {
				requiredLineStarts.reserve(static_cast<std::size_t>(std::max(requiredRowCount, 0)));
				warmupLineStarts.reserve(static_cast<std::size_t>(std::max(warmupRowCount, 0)));
				std::size_t lineStart = checkpoint.lineStart;
				std::size_t lineIndex = checkpoint.lineIndex;

				requiredLineStarts.clear();
				warmupLineStarts.clear();
				for (int i = 0; i < warmupRowCount; ++i) {
					if (exactLineCountKnown && lineIndex >= exactLineCount) break;
					if (i < requiredRowCount) requiredLineStarts.push_back(lineStart);
					warmupLineStarts.push_back(lineStart);
					++lineIndex;
					if (i + 1 >= warmupRowCount || lineStart >= mBufferModel.length()) break;
					std::size_t next = mBufferModel.nextLine(lineStart);
					if (next <= lineStart) break;
					lineStart = next;
				}
			}
			if (warmupLineStarts == requiredLineStarts) {
				requiredLineStarts = syntaxWarmupLineStarts(stateTopLine, requiredRowCount);
				warmupLineStarts = syntaxWarmupLineStarts(stateTopLine, warmupRowCount);
			}
			const std::size_t preludeCount = requiredLineStarts.size() > static_cast<std::size_t>(requiredChunkRows) ? requiredLineStarts.size() - static_cast<std::size_t>(requiredChunkRows) : 0;
			requestBottomLine = requestTopLine + (warmupLineStarts.size() > preludeCount ? warmupLineStarts.size() - preludeCount : 0);
			if (exactLineCountKnown && requestBottomLine > exactLineCount) requestBottomLine = exactLineCount;
		} else
			requestBottomLine = requestTopLine + warmupLineStarts.size();
		if (exactLineCountKnown && requestBottomLine > exactLineCount) requestBottomLine = exactLineCount;
	};

	std::vector<std::size_t> requiredLineStarts;
	std::vector<std::size_t> warmupLineStarts;
	MRSyntaxLineState requiredState;
	std::size_t bottomLine = 0;
	std::size_t requestTopLine = visibleTopLine;
	buildSyntaxRequest(visibleTopLine, static_cast<int>(visibleLineStarts.size()), backgroundRowBudget, requiredLineStarts, warmupLineStarts, requiredState, bottomLine);
	const bool visibleCacheComplete = hasSyntaxTokensForLineStarts(requiredLineStarts, requiredState);

	if (visibleCacheComplete) {
		std::size_t prefetchCursor = std::max(visibleTopLine, mSyntaxPrefetchReachedBottomLine);

		while (prefetchCursor < mSyntaxPrefetchTargetBottomLine) {
			std::vector<std::size_t> continuationRequiredLineStarts;
			std::vector<std::size_t> continuationWarmupLineStarts;
			MRSyntaxLineState continuationState;
			const int continuationRows = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(rowBudget), mSyntaxPrefetchTargetBottomLine - prefetchCursor));
			std::size_t continuationBottomLine = 0;

			if (continuationRows <= 0) break;
			buildSyntaxRequest(prefetchCursor, continuationRows, continuationRows, continuationRequiredLineStarts, continuationWarmupLineStarts, continuationState, continuationBottomLine);
			if (continuationRequiredLineStarts.empty()) {
				std::size_t eofBottomLine = prefetchCursor;
				if (mBufferModel.exactLineCountKnown()) eofBottomLine = std::max<std::size_t>(1, mBufferModel.lineCount());
				mSyntaxPrefetchReachedBottomLine = std::max(mSyntaxPrefetchReachedBottomLine, eofBottomLine);
				if (mSyntaxPrefetchTargetBottomLine > eofBottomLine) mSyntaxPrefetchTargetBottomLine = eofBottomLine;
				break;
			}
			if (!hasSyntaxTokensForLineStarts(continuationRequiredLineStarts, continuationState)) {
				requestTopLine = prefetchCursor;
				requiredLineStarts.swap(continuationRequiredLineStarts);
				warmupLineStarts.swap(continuationWarmupLineStarts);
				requiredState = continuationState;
				bottomLine = continuationBottomLine;
				if (exactLineCountKnown && bottomLine > exactLineCount) bottomLine = exactLineCount;
				break;
			}
			if (continuationBottomLine <= prefetchCursor) break;
			mSyntaxPrefetchReachedBottomLine = std::max(mSyntaxPrefetchReachedBottomLine, continuationBottomLine);
			if (exactLineCountKnown && mSyntaxPrefetchReachedBottomLine > exactLineCount) mSyntaxPrefetchReachedBottomLine = exactLineCount;
			prefetchCursor = mSyntaxPrefetchReachedBottomLine;
		}

		if (mSyntaxPrefetchReachedBottomLine >= mSyntaxPrefetchTargetBottomLine) {
			std::uint64_t previousTaskId = mSyntaxWarmupTaskId;
			bool hadTask = previousTaskId != 0;
			mSyntaxWarmupTaskId = 0;
			mSyntaxWarmupDocumentId = docId;
			mSyntaxWarmupVersion = version;
			mSyntaxWarmupTopLine = visibleTopLine;
			mSyntaxWarmupBottomLine = bottomLine;
			mSyntaxWarmupLanguage = language;
			if (hadTask) {
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
				notifyWindowTaskStateChanged();
			}
			return;
		}
	} else
		mSyntaxPrefetchReachedBottomLine = visibleTopLine;
	if (exactLineCountKnown && mSyntaxPrefetchReachedBottomLine > exactLineCount) mSyntaxPrefetchReachedBottomLine = exactLineCount;

	if (visibleCacheComplete && bottomLine <= visibleTopLine) return;
	if (mSyntaxWarmupTaskId != 0 && mSyntaxWarmupDocumentId == docId && mSyntaxWarmupVersion == version && mSyntaxWarmupLanguage == language && requestTopLine >= mSyntaxWarmupTopLine &&
		bottomLine <= mSyntaxWarmupBottomLine)
		return;

	if (visibleCacheComplete && requiredLineStarts.empty()) {
		std::uint64_t previousTaskId = mSyntaxWarmupTaskId;
		bool hadTask = previousTaskId != 0;
		mSyntaxWarmupTaskId = 0;
		mSyntaxWarmupDocumentId = docId;
		mSyntaxWarmupVersion = version;
		mSyntaxWarmupTopLine = visibleTopLine;
		mSyntaxWarmupBottomLine = bottomLine;
		mSyntaxWarmupLanguage = language;
		if (hadTask) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
			notifyWindowTaskStateChanged();
		}
		return;
	}

	std::uint64_t previousTaskId = mSyntaxWarmupTaskId;
	if (previousTaskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
	mSyntaxWarmupDocumentId = docId;
	mSyntaxWarmupVersion = version;
	mSyntaxWarmupTopLine = requestTopLine;
	mSyntaxWarmupBottomLine = bottomLine;
	mSyntaxWarmupLanguage = language;
	mSyntaxWarmupTaskId =
	    mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::SyntaxWarmup, docId, version, syntaxWarmupTaskLabel(),
	                                                [snapshot, language, warmupLineStarts, statefulSyntax, requiredState](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
		mr::coprocessor::Result result;
		std::vector<mr::coprocessor::SyntaxWarmLine> warmed;
		auto shouldStop = [&]() noexcept { return stopToken.stop_requested() || info.cancelRequested(); };
		result.task = info;
		if (shouldStop()) {
			result.status = mr::coprocessor::TaskStatus::Cancelled;
			return result;
		}
		warmed.reserve(warmupLineStarts.size());
		MRSyntaxLineState state = statefulSyntax ? requiredState : MRSyntaxLineState();
		for (std::size_t i = 0; i < warmupLineStarts.size(); ++i) {
			if (shouldStop()) {
				result.status = mr::coprocessor::TaskStatus::Cancelled;
				return result;
			}
			MRSyntaxLineResult syntaxLine = tmrHighlightTextLine(language, snapshot.lineText(warmupLineStarts[i]), statefulSyntax ? state : MRSyntaxLineState());
			if (statefulSyntax) state = syntaxLine.stateOut;
			warmed.push_back(mr::coprocessor::SyntaxWarmLine(warmupLineStarts[i], std::move(syntaxLine)));
		}
		result.status = mr::coprocessor::TaskStatus::Completed;
		result.payload = std::make_shared<mr::coprocessor::SyntaxWarmupPayload>(language, std::move(warmed));
		return result;
	});
	if (mSyntaxWarmupTaskId != previousTaskId) notifyWindowTaskStateChanged();
}

void MRFileEditor::scheduleSaveNormalizationWarmupIfNeeded() {
	invalidateSaveNormalizationCache();
	if (mSaveNormalizationWarmupTaskId == 0) return;
	std::uint64_t cancelledTaskId = mSaveNormalizationWarmupTaskId;
	static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
	clearSaveNormalizationWarmupTask(cancelledTaskId);
}

void MRFileEditor::updateMetrics() {
	int limitX = 1;
	int limitY = 1;
	TextViewportGeometry viewport = textViewportGeometry();
	int gutterWidth = viewport.gutterWidth;
	int rightInset = viewport.rightInset;
	int viewportWidth = viewport.width;
	const int textRows = std::max(1, visibleTextRows());
	const bool showEofMarker = configuredEditSetupSettings().showEofMarker;

	if (useApproximateLargeFileMetrics()) {
		limitX = dynamicLargeFileWidthLimit();
		if (mBufferModel.exactLineCountKnown()) limitY = std::max<int>(1, static_cast<int>(mBufferModel.lineCount()));
		else
			limitY = dynamicLargeFileLineLimit();
	} else {
		limitX = longestLineWidth();
		limitY = std::max<int>(1, static_cast<int>(mBufferModel.lineCount()));
	}
	limitX = std::max(limitX, displayedCursorColumn() + 1);
	if (showEofMarker && limitY < INT_MAX) ++limitY;

	int maxX = std::max(0, limitX - viewportWidth);
	int maxY = std::max(0, limitY - textRows);
	int newDeltaX = std::min(std::max(delta.x, 0), maxX);
	int newDeltaY = std::min(std::max(delta.y, 0), maxY);
	traceLargeFileMetrics("updateMetrics", limitY, maxY, textRows, newDeltaY);

	setLimit(limitX + gutterWidth + rightInset, limitY + viewport.topInset);
	if (newDeltaX != delta.x || newDeltaY != delta.y) scrollTo(newDeltaX, newDeltaY);
}

void MRFileEditor::updateIndicator() {
	if (mIndicatorUpdateInProgress) return;
	mIndicatorUpdateInProgress = true;
	TextViewportGeometry viewport = textViewportGeometry();
	unsigned long visualColumn = static_cast<unsigned long>(displayedCursorColumn());
	unsigned long line = static_cast<unsigned long>(mBufferModel.lineIndex(mBufferModel.cursor()));
	long long localX = viewport.localXFromVisualColumn(static_cast<long long>(visualColumn));
	long long localY = static_cast<long long>(line) - delta.y + viewport.topInset;

	if (mIndicator != nullptr) {
		if (auto *mrIndicator = dynamic_cast<MRIndicator *>(mIndicator)) mrIndicator->setDisplayValue(visualColumn, line, mBufferModel.isModified() ? True : False);
		else {
			TPoint location = {short(visualColumn > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : visualColumn), short(line > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : line)};
			mIndicator->setValue(location, mBufferModel.isModified() ? True : False);
		}
	}

	if (shouldShowEditorCursor(localX, localY, viewport)) {
		setCursor(static_cast<int>(localX), static_cast<int>(localY));
		showCursor();
	} else
		hideCursor();
	mIndicatorUpdateInProgress = false;
}

Boolean MRFileEditor::confirmSaveOrDiscardUntitled() {
	const char *detail = nullptr;
	std::string persistentName;

	if (hasPersistentFileName()) {
		persistentName = trimAscii(fileName);
		if (!persistentName.empty() && upperAscii(persistentName) != "?NO-FILE?") detail = persistentName.c_str();
	}
	switch (mr::dialogs::showUnsavedChangesDialog("Save As", "Window has unsaved changes.", detail)) {
		case mr::dialogs::UnsavedChangesChoice::Save:
			return saveAsWithPrompt();
		case mr::dialogs::UnsavedChangesChoice::Discard:
			setDocumentModified(false);
			return True;
		default:
			return False;
	}
}

Boolean MRFileEditor::confirmSaveOrDiscardNamed() {
	switch (mr::dialogs::showUnsavedChangesDialog("Save", "Save changes to:", fileName)) {
		case mr::dialogs::UnsavedChangesChoice::Save:
			return saveInPlace();
		case mr::dialogs::UnsavedChangesChoice::Discard:
			setDocumentModified(false);
			return True;
		default:
			return False;
	}
}

void MRFileEditor::refreshSyntaxContext() {
	MRSyntaxLanguage oldLanguage = mBufferModel.language();
	const bool oldAutomatic = mBufferModel.languageAutomatic();
	std::string codeLanguage = configuredEditSetupSettings().codeLanguage;

	if (hasPersistentFileName()) {
		MREditSetupSettings effective;
		if (effectiveEditSetupSettingsForPath(fileName, effective, nullptr)) codeLanguage = effective.codeLanguage;
	}
	mBufferModel.setSyntaxContext(hasPersistentFileName() ? fileName : "", mSyntaxTitleHint, codeLanguage);
	if (mBufferModel.language() != oldLanguage) resetSyntaxWarmupState(true);
	if (mBufferModel.languageAutomatic() != oldAutomatic) drawView();
}

void MRFileEditor::resetSyntaxWarmupState(bool clearCache) noexcept {
	std::uint64_t cancelledTaskId = mSyntaxWarmupTaskId;
	bool hadTask = cancelledTaskId != 0;
	if (clearCache) {
		mSyntaxTokenCache.clear();
		mSyntaxCheckpoints.clear();
	}
	mSyntaxWarmupTaskId = 0;
	mSyntaxWarmupDocumentId = 0;
	mSyntaxWarmupVersion = 0;
	mSyntaxWarmupTopLine = 0;
	mSyntaxWarmupBottomLine = 0;
	mSyntaxWarmupLanguage = MRSyntaxLanguage::PlainText;
	mSyntaxPrefetchDocumentId = 0;
	mSyntaxPrefetchVersion = 0;
	mSyntaxPrefetchTargetBottomLine = 0;
	mSyntaxPrefetchReachedBottomLine = 0;
	mSyntaxPrefetchLanguage = MRSyntaxLanguage::PlainText;
	if (hadTask) {
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
		notifyWindowTaskStateChanged();
	}
}

void MRFileEditor::invalidateSyntaxCacheFromLineStart(std::size_t lineStart) noexcept {
	std::map<std::size_t, MRSyntaxCacheEntry>::iterator firstInvalid = mSyntaxTokenCache.lower_bound(lineStart);
	std::size_t lineIndex = mBufferModel.lineIndex(lineStart);
	std::map<std::size_t, MRSyntaxCheckpointEntry>::iterator firstInvalidCheckpoint = mSyntaxCheckpoints.lower_bound(lineIndex);

	if (firstInvalid != mSyntaxTokenCache.end()) mSyntaxTokenCache.erase(firstInvalid, mSyntaxTokenCache.end());
	if (firstInvalidCheckpoint != mSyntaxCheckpoints.end()) mSyntaxCheckpoints.erase(firstInvalidCheckpoint, mSyntaxCheckpoints.end());
	if (mSyntaxPrefetchDocumentId == mBufferModel.documentId() && mSyntaxPrefetchVersion == mBufferModel.version()) {
		if (mSyntaxPrefetchReachedBottomLine > lineIndex) mSyntaxPrefetchReachedBottomLine = lineIndex;
		if (mSyntaxPrefetchTargetBottomLine < lineIndex) mSyntaxPrefetchTargetBottomLine = lineIndex;
	}
	if (mSyntaxWarmupTaskId != 0 && mSyntaxWarmupDocumentId == mBufferModel.documentId() && mSyntaxWarmupVersion == mBufferModel.version() && lineIndex <= mSyntaxWarmupBottomLine) {
		const std::uint64_t cancelledTaskId = mSyntaxWarmupTaskId;
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
		clearSyntaxWarmupTask(cancelledTaskId);
	}
}

void MRFileEditor::clearDirtyRanges() noexcept {
	mDirtyRanges.clear();
}

void MRFileEditor::normalizePairRangeList(std::vector<std::pair<std::size_t, std::size_t>> &ranges) {
	std::sort(ranges.begin(), ranges.end(), [](const std::pair<std::size_t, std::size_t> &a, const std::pair<std::size_t, std::size_t> &b) { return a.first < b.first || (a.first == b.first && a.second < b.second); });
	std::vector<std::pair<std::size_t, std::size_t>> merged;
	for (const auto &item : ranges) {
		if (item.second <= item.first) continue;
		if (merged.empty() || item.first > merged.back().second) merged.push_back(item);
		else if (item.second > merged.back().second)
			merged.back().second = item.second;
	}
	ranges.swap(merged);
}

void MRFileEditor::normalizeRangeList(std::vector<MRTextBufferModel::Range> &ranges) {
	std::sort(ranges.begin(), ranges.end(), [](const MRTextBufferModel::Range &a, const MRTextBufferModel::Range &b) { return a.start < b.start || (a.start == b.start && a.end < b.end); });
	std::vector<MRTextBufferModel::Range> merged;
	for (const MRTextBufferModel::Range &item : ranges) {
		if (item.end <= item.start) continue;
		if (merged.empty() || item.start > merged.back().end) merged.push_back(item);
		else if (item.end > merged.back().end)
			merged.back().end = item.end;
	}
	ranges.swap(merged);
}

void MRFileEditor::normalizeDirtyRanges() {
	normalizeRangeList(mDirtyRanges);
}

void MRFileEditor::pushMappedDirtyRange(std::vector<MRTextBufferModel::Range> &mapped, std::size_t start, std::size_t end, std::size_t maxLength) {
	start = std::min(start, maxLength);
	end = std::min(end, maxLength);
	if (end <= start) return;
	mapped.push_back(MRTextBufferModel::Range(start, end));
}

void MRFileEditor::remapDirtyRangesForAppliedChange(const MRTextBufferModel::DocumentChangeSet &change) {
	const std::size_t oldLength = change.oldLength;
	const std::size_t newLength = change.newLength;
	const MRTextBufferModel::Range touched = change.touchedRange.normalized();
	const long long delta = static_cast<long long>(newLength) - static_cast<long long>(oldLength);
	const std::size_t touchedLength = touched.length();
	const std::size_t editStart = std::min(touched.start, oldLength);
	std::size_t replacedOldLength = touchedLength;

	if (mDirtyRanges.empty()) return;
	if (delta >= 0) {
		const std::size_t deltaUnsigned = static_cast<std::size_t>(delta);
		replacedOldLength = touchedLength > deltaUnsigned ? touchedLength - deltaUnsigned : 0;
	}
	if (replacedOldLength > oldLength - editStart) replacedOldLength = oldLength - editStart;
	const std::size_t oldEditEnd = editStart + replacedOldLength;

	std::vector<MRTextBufferModel::Range> mapped;
	mapped.reserve(mDirtyRanges.size() + 2);

	for (std::size_t i = 0; i < mDirtyRanges.size(); ++i) {
		MRTextBufferModel::Range range = mDirtyRanges[i].clamped(oldLength).normalized();

		if (range.end <= range.start) continue;
		if (range.end <= editStart) {
			pushMappedDirtyRange(mapped, range.start, range.end, newLength);
			continue;
		}
		if (range.start >= oldEditEnd) {
			const long long shiftedStart = static_cast<long long>(range.start) + delta;
			const long long shiftedEnd = static_cast<long long>(range.end) + delta;
			if (shiftedEnd <= 0) continue;
			pushMappedDirtyRange(mapped, static_cast<std::size_t>(std::max<long long>(0, shiftedStart)), static_cast<std::size_t>(std::max<long long>(0, shiftedEnd)), newLength);
			continue;
		}

		if (range.start < editStart) pushMappedDirtyRange(mapped, range.start, editStart, newLength);
		if (range.end > oldEditEnd) {
			const long long shiftedStart = static_cast<long long>(oldEditEnd) + delta;
			const long long shiftedEnd = static_cast<long long>(range.end) + delta;
			if (shiftedEnd > 0) pushMappedDirtyRange(mapped, static_cast<std::size_t>(std::max<long long>(0, shiftedStart)), static_cast<std::size_t>(std::max<long long>(0, shiftedEnd)), newLength);
		}
	}

	mDirtyRanges.swap(mapped);
	normalizeDirtyRanges();
}

void MRFileEditor::addDirtyRange(MRTextBufferModel::Range range) {
	if (mBufferModel.length() == 0) return;
	range = range.clamped(mBufferModel.length());
	range.normalize();
	if (range.empty()) {
		std::size_t point = std::min(range.start, mBufferModel.length() - 1);
		range = MRTextBufferModel::Range(point, point + 1);
	}
	mDirtyRanges.push_back(range);
	normalizeDirtyRanges();
}

bool MRFileEditor::isDirtyOffset(std::size_t pos) const noexcept {
	if (mDirtyRanges.empty() || mBufferModel.length() == 0) return false;
	if (pos >= mBufferModel.length()) return false;
	for (const MRTextBufferModel::Range &item : mDirtyRanges) {
		if (item.end <= pos) continue;
		if (item.start > pos) break;
		return pos < item.end;
	}
	return false;
}
