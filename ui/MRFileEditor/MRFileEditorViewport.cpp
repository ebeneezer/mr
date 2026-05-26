#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"
#include "../../app/MREditorApp.hpp"

#include <chrono>
#include <ctime>
#include <future>
#include <sstream>
#include <thread>

namespace {
bool isStatefulSyntaxLanguage(MRSyntaxLanguage language) noexcept {
	return language == MRSyntaxLanguage::MRMAC || language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp || language == MRSyntaxLanguage::JavaScript || language == MRSyntaxLanguage::Python ||
	       language == MRSyntaxLanguage::Markdown || language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh || language == MRSyntaxLanguage::Fish || language == MRSyntaxLanguage::Perl || language == MRSyntaxLanguage::Swift || language == MRSyntaxLanguage::Rust ||
	       language == MRSyntaxLanguage::Xml ||
	       language == MRSyntaxLanguage::Go || language == MRSyntaxLanguage::Kotlin || language == MRSyntaxLanguage::CSharp || language == MRSyntaxLanguage::Pascal;
}

bool quitTailTraceActive() noexcept {
	const auto *app = dynamic_cast<const MREditorApp *>(TProgram::application);
	return app != nullptr && app->quitPrepared();
}
}

MRFileEditor::TextViewportGeometry MRFileEditor::textViewportGeometryFor(const MREditSetupSettings &settings) const noexcept {
	MRTextViewportLayout::Inputs inputs;
	MRFileEditor *self = const_cast<MRFileEditor *>(this);
	const bool approximateLargeFileMetrics = useApproximateLargeFileMetrics();
	const bool foldingEnabled = foldingPipelineEnabled();
	MREditSetupSettings viewportSettings = settings;
	inputs.viewWidth = size.x;
	inputs.visibleRows = visibleTextRows();
	inputs.deltaX = delta.x;
	inputs.deltaY = delta.y;
	if (foldingEnabled && settings.codeFolding) self->ensureVisibleFoldSpans(static_cast<std::size_t>(std::max(delta.y, 0)), inputs.visibleRows, mBufferModel.language());
	inputs.codeFoldingColumns = foldingEnabled && settings.codeFolding ? self->visibleFoldGutterColumns() : 1;
	inputs.exactLineCountKnown = !approximateLargeFileMetrics && mBufferModel.exactLineCountKnown();
	inputs.exactLineCount = inputs.exactLineCountKnown ? mBufferModel.lineCount() : 0;
	inputs.estimatedLineCount = mBufferModel.estimatedLineCount();
	if (mCommunicationViewerMode) {
		viewportSettings.showLineNumbers = mCommunicationViewerLineNumbers;
		viewportSettings.lineNumbersPosition = mCommunicationViewerLineNumbers ? "LEADING" : "OFF";
		viewportSettings.codeFolding = false;
		viewportSettings.codeFoldingPosition = "OFF";
		viewportSettings.miniMapPosition = "OFF";
	}
	if (mMiniMapSuppressed) viewportSettings.miniMapPosition = "OFF";
	if (!foldingEnabled) {
		viewportSettings.codeFolding = false;
		viewportSettings.codeFoldingPosition = "OFF";
	}
	return MRTextViewportLayout::geometryFor(viewportSettings, inputs);
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

void MRFileEditor::invalidateFoldCache(bool preserveVisibleProjection) noexcept {
	mFoldState.clearVisibleState(preserveVisibleProjection);
}

int MRFileEditor::visibleFoldGutterColumns() const noexcept {
	return mFoldState.visibleGutterColumns();
}

std::size_t MRFileEditor::documentLineForVisibleLine(std::size_t visibleLine) const noexcept {
	const std::vector<MRFoldSpan> &effectiveClosedFoldSpans = mFoldState.effectiveClosedFoldSpans();
	if (effectiveClosedFoldSpans.empty()) return visibleLine;
	std::size_t documentLine = visibleLine;
	std::size_t hiddenBefore = 0;

	for (const MRFoldSpan &span : effectiveClosedFoldSpans) {
		const std::size_t hiddenLength = span.endLine > span.startLine ? span.endLine - span.startLine : 0;
		const std::size_t visibleStart = span.startLine - hiddenBefore;
		if (visibleLine <= visibleStart) break;
		documentLine += hiddenLength;
		hiddenBefore += hiddenLength;
	}
	return documentLine;
}

std::size_t MRFileEditor::visibleLineForDocumentLine(std::size_t documentLine) const noexcept {
	const std::vector<MRFoldSpan> &effectiveClosedFoldSpans = mFoldState.effectiveClosedFoldSpans();
	if (effectiveClosedFoldSpans.empty()) return documentLine;
	std::size_t hiddenBefore = 0;

	for (const MRFoldSpan &span : effectiveClosedFoldSpans) {
		const std::size_t hiddenLength = span.endLine > span.startLine ? span.endLine - span.startLine : 0;
		if (documentLine > span.startLine && documentLine <= span.endLine) return span.startLine - hiddenBefore;
		if (span.endLine < documentLine) hiddenBefore += hiddenLength;
		else
			break;
	}
	return documentLine - hiddenBefore;
}

std::size_t MRFileEditor::foldedVisibleLineCount() const noexcept {
	const std::vector<MRFoldSpan> &effectiveClosedFoldSpans = mFoldState.effectiveClosedFoldSpans();
	std::size_t total = std::max<std::size_t>(1, mBufferModel.lineCount());

	for (const MRFoldSpan &span : effectiveClosedFoldSpans)
		if (span.endLine > span.startLine) total -= (span.endLine - span.startLine);
	return std::max<std::size_t>(1, total);
}

bool MRFileEditor::toggleFoldAtLine(std::size_t lineIndex) {
	std::vector<MRFoldSpan> &visibleFoldSpans = mFoldState.visibleState().spans;
	std::map<std::size_t, MRFoldSpan> &closedFoldSpans = mFoldState.closedFoldSpans();
	for (const MRFoldSpan &span : visibleFoldSpans) {
		if (span.startLine != lineIndex) continue;
		if (span.open) closedFoldSpans[lineIndex] = MRFoldSpan(span.startLine, span.endLine, span.level, span.sourceKind, false, span.siblingContinuation);
		else
			closedFoldSpans.erase(lineIndex);
		mFoldState.rebuildEffectiveClosedFolds();
		if (span.open) {
			const std::size_t cursorLine = mBufferModel.lineIndex(mBufferModel.cursor());
			if (cursorLine > span.startLine && cursorLine <= span.endLine) moveCursor(mBufferModel.lineStartByIndex(span.startLine), false, false);
		}
		if (mFoldState.warmupState().taskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mFoldState.warmupState().taskId));
			clearFoldWarmupTask(mFoldState.warmupState().taskId);
		}
		invalidateFoldCache(true);
		return true;
	}
	return false;
}

bool MRFileEditor::foldingGutterHit(TPoint local, std::size_t *lineIndexOut) const noexcept {
	const TextViewportGeometry viewport = textViewportGeometry();
	const int textRows = std::max(1, visibleTextRows());

	if (viewport.codeFoldingWidth <= 0) return false;
	if (local.x < viewport.codeFoldingX || local.x >= viewport.codeFoldingX + viewport.codeFoldingWidth) return false;
	if (local.y < viewport.topInset || local.y >= viewport.topInset + textRows) return false;
	if (lineIndexOut != nullptr) *lineIndexOut = documentLineForVisibleLine(static_cast<std::size_t>(std::max(0, delta.y + local.y - viewport.topInset)));
	return true;
}

void MRFileEditor::ensureVisibleFoldSpans(std::size_t topLine, int rowCount, MRSyntaxLanguage language) {
	if (!foldingPipelineEnabled()) {
		if (mFoldState.warmupState().taskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mFoldState.warmupState().taskId));
			clearFoldWarmupTask(mFoldState.warmupState().taskId);
		}
		invalidateFoldCache();
		return;
	}
	const std::size_t docId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	const bool approximateLargeFileMetrics = useApproximateLargeFileMetrics();
	MRFoldingDerivedState::VisibleState &visibleState = mFoldState.visibleState();

	if (rowCount <= 0) {
		invalidateFoldCache();
		return;
	}

	const bool exactLineCountKnown = !approximateLargeFileMetrics && mBufferModel.exactLineCountKnown();
	const std::size_t exactLineCount = exactLineCountKnown ? std::max<std::size_t>(1, mBufferModel.lineCount()) : 0;
	const std::size_t visibleTopLine = topLine;
	topLine = documentLineForVisibleLine(visibleTopLine);
	if (exactLineCountKnown && topLine >= exactLineCount) topLine = exactLineCount - 1;
	std::size_t requestBottomLine = documentLineForVisibleLine(visibleTopLine + static_cast<std::size_t>(std::max(0, rowCount))) + 1;
	if (exactLineCountKnown && requestBottomLine > exactLineCount) requestBottomLine = exactLineCount;
	auto updateVisibleFoldGutterColumnsForViewport = [&]() noexcept {
		int maxDisplayLevel = -1;
		const std::size_t visibleBottomLine = visibleTopLine + static_cast<std::size_t>(std::max(0, rowCount));

		for (std::size_t visibleLine = visibleTopLine; visibleLine < visibleBottomLine; ++visibleLine) {
			const std::size_t documentLine = documentLineForVisibleLine(visibleLine);
			for (const MRFoldSpan &span : visibleState.spans) {
				if (documentLine < span.startLine || documentLine > span.endLine) continue;
				bool glyphVisible = false;
				if (!span.open) glyphVisible = span.startLine == documentLine;
				else if (documentLine == span.startLine || documentLine == span.endLine || (documentLine > span.startLine && documentLine < span.endLine)) glyphVisible = true;
				if (!glyphVisible) continue;
				maxDisplayLevel = std::max(maxDisplayLevel, static_cast<int>(span.level));
			}
		}
		visibleState.gutterColumns = std::max(1, maxDisplayLevel + 1);
		visibleState.displayLevels.clear();
		visibleState.displayLevels.reserve(static_cast<std::size_t>(visibleState.gutterColumns));
		for (int level = 0; level < visibleState.gutterColumns; ++level)
			visibleState.displayLevels.push_back(static_cast<unsigned short>(level));
	};
	if (visibleState.documentId == docId && visibleState.version == version && visibleState.language == language && topLine >= visibleState.topLine && requestBottomLine <= visibleState.bottomLine) {
		updateVisibleFoldGutterColumnsForViewport();
		return;
	}

	const int safeRowCount = std::max(1, rowCount);
	const std::size_t viewportMargin = approximateLargeFileMetrics ? static_cast<std::size_t>(std::max(safeRowCount * 2, 64)) : static_cast<std::size_t>(std::max(safeRowCount * 2, 32));
	const std::size_t scanTopLine = topLine > viewportMargin ? topLine - viewportMargin : 0;
	std::size_t scanBottomLine = requestBottomLine + viewportMargin;
	if (exactLineCountKnown && scanBottomLine > exactLineCount) scanBottomLine = exactLineCount;
	scheduleFoldWarmupIfNeeded(scanTopLine, scanBottomLine, topLine, requestBottomLine, language);
	if (visibleState.documentId == docId && visibleState.version == version && visibleState.language == language) updateVisibleFoldGutterColumnsForViewport();
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

const char *MRFileEditor::foldWarmupTaskLabel() noexcept {
	return "fold-warmup";
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

bool MRFileEditor::findMarkerContainsOffset(std::size_t offset) const noexcept {
	for (const MRTextBufferModel::Range &range : mFindMarkerRanges) {
		if (range.end <= offset) continue;
		if (range.start > offset) break;
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
	const std::size_t currentLine = visibleLineForDocumentLine(cachedCursorLineIndex());
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
void MRFileEditor::drawFormatRulerOverlay(const TextViewportGeometry &viewport, const MREditSetupSettings &settings) {
	TDrawBuffer buffer;
	unsigned char configured = 0;
	TColorAttr normal = static_cast<TColorAttr>(getColor(0x0606));
	const TColorAttr accent = static_cast<TColorAttr>(getColor(0x0404));
	const std::string normalized = normalizedFormatRulerLine(settings);
	const std::size_t cursorLineIndex = cachedCursorLineIndex();

	if (configuredColorSlotOverride(kMrPaletteFormatRuler, configured)) normal = static_cast<TColorAttr>(configured);
	buffer.moveChar(0, ' ', normal, size.x);
	for (int x = 0; x < viewport.width; ++x) {
		const int column = viewport.deltaX + x + 1;
		const char ch = column >= 1 && column <= static_cast<int>(normalized.size()) ? normalized[static_cast<std::size_t>(column - 1)] : ' ';
		const bool atCursor = static_cast<int>(cursorLineIndex) == delta.y && displayedCursorColumn() == viewport.deltaX + x;
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
	const bool foldingEnabled = foldingPipelineEnabled();
	const bool miniMapEnabled = miniMapPipelineEnabled();
	std::size_t totalLines = 1;
	TextViewportGeometry viewport = textViewportGeometryFor(editSettings);
	bool showLineNumbers = viewport.lineNumberWidth > 0;
	bool drawCodeFolding = foldingEnabled && viewport.codeFoldingWidth > 0;
	bool zeroFillLineNumbers = showLineNumbers && editSettings.lineNumZeroFill;
	int textWidth = viewport.width;
	MRTextBufferModel::Range selection = mBufferModel.selection().range().normalized();
	const MRSyntaxLanguage syntaxLanguage = mBufferModel.language();
	const bool syntaxEnabled = syntaxPipelineEnabled();
	const bool statefulSyntax = syntaxEnabled && isStatefulSyntaxLanguage(syntaxLanguage);
	MRMiniMapRenderer::Palette miniMapPalette = resolveMiniMapPalette();
	const bool drawMiniMap = miniMapEnabled && viewport.miniMapBodyWidth > 0 && viewport.miniMapInfoX >= 0;
	const bool miniMapUseBraille = MRMiniMapRenderer::useBrailleRenderer();
	std::string viewportMarkerGlyph = MRMiniMapRenderer::normalizedViewportMarkerGlyph(editSettings.miniMapMarkerGlyph);
	const bool foldedView = foldingEnabled && !mFoldState.closedFoldSpans().empty();
	const int miniMapRows = std::max(0, visibleTextRows());
	if (mBufferModel.exactLineCountKnown()) totalLines = foldedView ? foldedVisibleLineCount() : std::max<std::size_t>(1, mBufferModel.lineCount());
	else
		totalLines = std::max<std::size_t>(1, std::max<std::size_t>(mBufferModel.estimatedLineCount(), static_cast<std::size_t>(std::max(delta.y, 0)) + static_cast<std::size_t>(std::max(miniMapRows, 1))));
	std::size_t topLine = static_cast<std::size_t>(std::max(delta.y, 0));
	if (topLine >= totalLines) topLine = totalLines - 1;
	std::size_t linePtr = mBufferModel.lineStartByIndex(documentLineForVisibleLine(topLine));
	std::size_t lineIndex = documentLineForVisibleLine(topLine);
	const MRMiniMapRenderer::Viewport miniMapViewport = {viewport.width, viewport.miniMapBodyX, viewport.miniMapBodyWidth, viewport.miniMapInfoX, viewport.miniMapSeparatorX};
	if (drawMiniMap) {
		const std::uint64_t previousMiniMapTaskId = mMiniMapState.renderer().pendingWarmupTaskId();
		MRMiniMapRenderer::Signals miniMapSignals = mMiniMapState.renderer().scheduleWarmupIfNeeded(miniMapViewport, miniMapRows, miniMapUseBraille, totalLines, topLine, mBufferModel.documentId(), mBufferModel.version(),
		                                                                                      mBufferModel.readSnapshot(), editSettings, useApproximateLargeFileMetrics());
		const std::uint64_t currentMiniMapTaskId = mMiniMapState.renderer().pendingWarmupTaskId();
		if (shouldTraceLargeFileWarmupDiagnostics() && (currentMiniMapTaskId != previousMiniMapTaskId || miniMapSignals.notifyTaskStateChanged)) {
			std::string detail = "action=" + std::string(currentMiniMapTaskId == 0 ? "idle" : (currentMiniMapTaskId == previousMiniMapTaskId ? "reuse" : "schedule")) + " task=" +
			                     std::to_string(currentMiniMapTaskId) + " top_line=" + std::to_string(topLine) + " rows=" + std::to_string(miniMapRows) + " total_lines=" + std::to_string(totalLines);
			traceLargeFileWarmup(mLastMiniMapWarmupTrace, "minimap", std::move(detail));
		}
		applyMiniMapSignals(miniMapSignals);
	}
	MRMiniMapRenderer::OverlayState miniMapOverlay;
	if (drawMiniMap) {
		auto rangeSignature = [](const std::vector<MRTextBufferModel::Range> &ranges) noexcept {
			std::uint64_t signature = 1469598103934665603ULL;
			auto mixValue = [&signature](std::size_t value) noexcept {
				signature ^= static_cast<std::uint64_t>(value) + 0x9E3779B97F4A7C15ULL + (signature << 6) + (signature >> 2);
			};
			for (const MRTextBufferModel::Range &range : ranges) {
				mixValue(range.start);
				mixValue(range.end);
			}
			return signature;
		};
		const std::uint64_t findSignature = rangeSignature(mFindMarkerRanges);
		const std::uint64_t dirtySignature = rangeSignature(mDirtyRanges);
		const bool miniMapOverlayCacheCompatible = mMiniMapState.overlayCache().documentId == mBufferModel.documentId() &&
		                                           mMiniMapState.overlayCache().documentVersion == mBufferModel.version() && mMiniMapState.overlayCache().totalLines == totalLines &&
		                                           mMiniMapState.overlayCache().viewportWidth == viewport.width && mMiniMapState.overlayCache().bodyWidth == viewport.miniMapBodyWidth &&
		                                           mMiniMapState.overlayCache().braille == miniMapUseBraille && mMiniMapState.overlayCache().selectionStart == selection.start &&
		                                           mMiniMapState.overlayCache().selectionEnd == selection.end && mMiniMapState.overlayCache().findSignature == findSignature &&
		                                           mMiniMapState.overlayCache().dirtySignature == dirtySignature;

		if (miniMapOverlayCacheCompatible) miniMapOverlay = mMiniMapState.overlayCache().overlay;
		else {
			miniMapOverlay = mMiniMapState.renderer().computeOverlayState(mBufferModel.readSnapshot(), selection, mFindMarkerRanges, mDirtyRanges, totalLines, viewport.width, viewport.miniMapBodyWidth, miniMapUseBraille, editSettings);
			mMiniMapState.overlayCache().documentId = mBufferModel.documentId();
			mMiniMapState.overlayCache().documentVersion = mBufferModel.version();
			mMiniMapState.overlayCache().totalLines = totalLines;
			mMiniMapState.overlayCache().viewportWidth = viewport.width;
			mMiniMapState.overlayCache().bodyWidth = viewport.miniMapBodyWidth;
			mMiniMapState.overlayCache().braille = miniMapUseBraille;
			mMiniMapState.overlayCache().selectionStart = selection.start;
			mMiniMapState.overlayCache().selectionEnd = selection.end;
			mMiniMapState.overlayCache().findSignature = findSignature;
			mMiniMapState.overlayCache().dirtySignature = dirtySignature;
			mMiniMapState.overlayCache().overlay = miniMapOverlay;
		}
	}
	if (editSettings.formatRuler && viewport.topInset > 0) drawFormatRulerOverlay(viewport, editSettings);
	const int textRows = std::max(0, visibleTextRows());
	for (int y = 0; y < textRows; ++y) {
		TDrawBuffer buffer;
		const std::size_t visibleLineIndex = topLine + static_cast<std::size_t>(y);
		const std::size_t currentLineIndex = foldedView ? documentLineForVisibleLine(visibleLineIndex) : lineIndex;
		const std::size_t currentLinePtr = foldedView ? mBufferModel.lineStartByIndex(currentLineIndex) : linePtr;
		bool isDocumentLine = visibleLineIndex < totalLines;
		bool drawEofMarker = editSettings.showEofMarker && visibleLineIndex == totalLines;
		bool drawEofMarkerAsEmoji = drawEofMarker && editSettings.showEofMarkerEmoji;
		MRSyntaxLineResult syntaxLine;
		if (showLineNumbers) {
			std::size_t displayLineNumber = currentLineIndex + 1;
			if (mCommunicationViewerMode && configuredLiveLogSettings().scrollDirection == MRLiveLogScrollDirection::Up) {
				const std::size_t totalLineCount =
				    mBufferModel.exactLineCountKnown() ? std::max<std::size_t>(1, mBufferModel.lineCount()) : std::max<std::size_t>(1, mBufferModel.estimatedLineCount());
				if (currentLineIndex < totalLineCount) displayLineNumber = totalLineCount - currentLineIndex;
			}
			drawLineNumberGutter(buffer, displayLineNumber, isDocumentLine, viewport.lineNumberX, viewport.lineNumberWidth, zeroFillLineNumbers);
		}
		if (drawCodeFolding) drawCodeFoldingGutter(buffer, viewport.codeFoldingX, viewport.codeFoldingWidth, currentLinePtr, currentLineIndex);
		if (drawMiniMap) mMiniMapState.renderer().drawGutter(buffer, y, miniMapRows, size.x, miniMapViewport, totalLines, topLine, miniMapUseBraille, viewportMarkerGlyph, miniMapPalette, miniMapOverlay);
		if (syntaxEnabled) {
			std::map<std::size_t, MRSyntaxCacheEntry>::const_iterator found = mSyntaxState.tokenCache().find(currentLinePtr);
			const bool statefulCacheReady = !statefulSyntax || syntaxWarmedLineRangeCovered(currentLineIndex, currentLineIndex + 1);

			if (found != mSyntaxState.tokenCache().end() && statefulCacheReady) syntaxLine = found->second.syntaxLine;
		}
		formatSyntaxLine(buffer, currentLinePtr, syntaxLine, delta.x, textWidth, viewport.textLeft, isDocumentLine, drawEofMarker, drawEofMarkerAsEmoji);
		writeBuf(0, y + viewport.topInset, size.x, 1, buffer);
		if (!foldedView) {
			if (linePtr < mBufferModel.length()) linePtr = mBufferModel.nextLine(linePtr);
			++lineIndex;
		}
	}
	if (syntaxEnabled) scheduleSyntaxWarmupIfNeeded();
	scheduleSaveNormalizationWarmupIfNeeded();
	updateIndicator();
}

void MRFileEditor::drawLineNumberGutter(TDrawBuffer &b, std::size_t lineNumber, bool showNumber, int drawX, int width, bool zeroFill) {
	TColorAttr color = static_cast<TColorAttr>(getColor(0x0606));
	char numberBuffer[32];
	int digits = std::max(1, width);

	if (width <= 0) return;
	b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
	if (!showNumber) return;
	if (zeroFill) std::snprintf(numberBuffer, sizeof(numberBuffer), "%0*lu", digits, static_cast<unsigned long>(lineNumber));
	else
		std::snprintf(numberBuffer, sizeof(numberBuffer), "%*lu", digits, static_cast<unsigned long>(lineNumber));
	b.moveStr(static_cast<ushort>(drawX), numberBuffer, color, static_cast<ushort>(width));
}

void MRFileEditor::drawCodeFoldingGutter(TDrawBuffer &b, int drawX, int width, std::size_t lineStart, std::size_t lineIndex) {
	unsigned char configured = 0;
	TColorAttr color = static_cast<TColorAttr>(getColor(0x0606));
	TColorAttr markerColor = color;
	auto branchContinuesAtSameLevel = [this](const MRFoldSpan &span) noexcept {
		for (const MRFoldSpan &candidate : mFoldState.visibleState().spans)
			if (candidate.siblingContinuation && candidate.level == span.level && candidate.startLine == span.endLine + 1) return true;
		return false;
	};
	auto displayColumnForLevel = [this](unsigned short level) noexcept -> int {
		const std::vector<unsigned short> &displayLevels = mFoldState.visibleState().displayLevels;
		const auto it = std::lower_bound(displayLevels.begin(), displayLevels.end(), level);
		if (it == displayLevels.end() || *it != level) return -1;
		return static_cast<int>(it - displayLevels.begin());
	};

	static_cast<void>(lineStart);
	if (width <= 0) return;
	if (configuredColorSlotOverride(kMrPaletteCodeFolding, configured)) color = static_cast<TColorAttr>(configured);
	markerColor = color;
	if (configuredColorSlotOverride(kMrPaletteCodeFoldingMarker, configured)) markerColor = static_cast<TColorAttr>(configured);
	b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
	if (mBufferModel.exactLineCountKnown() && lineIndex >= std::max<std::size_t>(1, mBufferModel.lineCount())) return;
	for (const MRFoldSpan &span : mFoldState.visibleState().spans) {
		const int displayColumn = displayColumnForLevel(span.level);
		if (displayColumn < 0 || displayColumn >= width) continue;
		const char *glyph = nullptr;
		if (!span.open) {
			if (span.startLine != lineIndex) continue;
			glyph = "⟦";
		} else if (lineIndex == span.startLine)
			glyph = span.siblingContinuation ? "\xE2\x94\x9C" : "\xE2\x95\xAD";
		else if (lineIndex == span.endLine)
			glyph = branchContinuesAtSameLevel(span) ? "\xE2\x94\x82" : "\xE2\x95\xB0";
		else if (lineIndex > span.startLine && lineIndex < span.endLine)
			glyph = "\xE2\x94\x82";
		if (glyph == nullptr) continue;
		b.moveStr(static_cast<ushort>(drawX + displayColumn), glyph, markerColor, 1);
	}
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
			bool findMarkedChar = !selected && findMarkerContainsOffset(documentPos);
			TAttrPair effectivePair = changedChar ? changedPair : basePair;
			tokenPair = selected ? selectionPair : effectivePair;
			color = tokenColor(token, selected, tokenPair);
			if (findMarkedChar) {
				unsigned char warningAttr = 0;
				if (configuredColorSlotOverride(kMrPaletteMessageWarning, warningAttr)) color = static_cast<TColorAttr>((color & 0xF0) | (warningAttr & 0x0F));
				else
					color = static_cast<TColorAttr>((color & 0xF0) | 0x0E);
			}
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
void MRFileEditor::updateMetrics() {
	const auto startedAt = std::chrono::steady_clock::now();
	int limitX = 1;
	int limitY = 1;
	TextViewportGeometry viewport = textViewportGeometry();
	int gutterWidth = viewport.gutterWidth;
	int rightInset = viewport.rightInset;
	int viewportWidth = viewport.width;
	const int textRows = std::max(1, visibleTextRows());
	const bool showEofMarker = configuredEditSetupSettings().showEofMarker;
	const bool quitTail = quitTailTraceActive();

	if (useApproximateLargeFileMetrics() || quitTail || !mBufferModel.exactLineCountKnown()) {
		const auto lineLimitStartedAt = std::chrono::steady_clock::now();
		limitX = dynamicLargeFileWidthLimit();
		limitY = dynamicLargeFileLineLimit();
		if (shouldTraceLargeFileWarmupDiagnostics()) {
			const auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
			const auto lineLimitUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - lineLimitStartedAt).count();
			mLastUiHotpathTrace = "metrics_us=" + std::to_string(totalUs) + " line_limit_us=" + std::to_string(lineLimitUs) + " cursor_line=" + std::to_string(cachedCursorLineIndex()) +
			                      " est_lines=" + std::to_string(mBufferModel.estimatedLineCount()) + " limitY=" + std::to_string(limitY);
		}
	} else {
		limitX = longestLineWidth();
		limitY = foldingPipelineEnabled() ? std::max<int>(1, static_cast<int>(foldedVisibleLineCount())) : std::max<int>(1, static_cast<int>(mBufferModel.lineCount()));
		mLastUiHotpathTrace.clear();
	}
	limitX = std::max(limitX, displayedCursorColumn() + 1);
	if (showEofMarker && limitY < INT_MAX) ++limitY;

	int maxX = std::max(0, limitX - viewportWidth);
	int maxY = std::max(0, limitY - textRows);
	int newDeltaX = std::min(std::max(delta.x, 0), maxX);
	int newDeltaY = std::min(std::max(delta.y, 0), maxY);

	setLimit(limitX + gutterWidth + rightInset, limitY + viewport.topInset);
	if (newDeltaX != delta.x || newDeltaY != delta.y) scrollTo(newDeltaX, newDeltaY);
}

void MRFileEditor::updateIndicator() {
	if (mIndicatorUpdateInProgress) return;
	mIndicatorUpdateInProgress = true;
	TextViewportGeometry viewport = textViewportGeometry();
	unsigned long visualColumn = static_cast<unsigned long>(displayedCursorColumn());
	unsigned long line = static_cast<unsigned long>(visibleLineForDocumentLine(cachedCursorLineIndex()));
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
