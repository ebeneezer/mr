#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"
#include "../../app/MREditorApp.hpp"

#include <cctype>
#include <chrono>
#include <ctime>
#include <future>
#include <limits>
#include <sstream>
#include <thread>

namespace {
bool isStatefulSyntaxLanguage(MRSyntaxLanguage language) noexcept {
	return language == MRSyntaxLanguage::MRMAC || language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp || language == MRSyntaxLanguage::JavaScript || language == MRSyntaxLanguage::Python ||
	       language == MRSyntaxLanguage::Markdown || language == MRSyntaxLanguage::Latex || language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh || language == MRSyntaxLanguage::Fish || language == MRSyntaxLanguage::Perl || language == MRSyntaxLanguage::Swift || language == MRSyntaxLanguage::Rust ||
	       language == MRSyntaxLanguage::Xml ||
	       language == MRSyntaxLanguage::Go || language == MRSyntaxLanguage::Kotlin || language == MRSyntaxLanguage::CSharp || language == MRSyntaxLanguage::Pascal || language == MRSyntaxLanguage::Basic;
}

bool quitTailTraceActive() noexcept {
	const auto *app = dynamic_cast<const MREditorApp *>(TProgram::application);
	return app != nullptr && app->quitPrepared();
}

std::size_t renderedBlockOverlayEndForViewport(const MRTextBufferModel &model, std::size_t overlayStart, std::size_t overlayEnd, int overlayMode) noexcept {
	if (overlayStart > overlayEnd) std::swap(overlayStart, overlayEnd);
	if (overlayMode == 1 && overlayEnd > overlayStart && model.lineStart(overlayEnd) == overlayEnd && model.lineEnd(overlayEnd) == overlayEnd) --overlayEnd;
	return overlayEnd;
}

unsigned char fileCompareTextPaletteSlot(unsigned char lineKind) noexcept {
	switch (lineKind) {
		case mrfclkEqual:
			return kMrPaletteFileCompareTextEqual;
		case mrfclkMissing:
			return kMrPaletteFileCompareTextMissing;
		case mrfclkInsert:
			return kMrPaletteFileCompareTextInsert;
		case mrfclkOffset:
			return kMrPaletteFileCompareTextOffset;
		default:
			return 0;
	}
}

unsigned char fileCompareGutterPaletteSlot(unsigned char lineKind) noexcept {
	switch (lineKind) {
		case mrfclkEqual:
			return kMrPaletteFileCompareGutterEqual;
		case mrfclkMissing:
			return kMrPaletteFileCompareGutterMissing;
		case mrfclkInsert:
			return kMrPaletteFileCompareGutterInsert;
		case mrfclkOffset:
			return kMrPaletteFileCompareGutterOffset;
		default:
			return 0;
	}
}

char fileCompareGutterGlyph(unsigned char lineKind) noexcept {
	switch (lineKind) {
		case mrfclkEqual:
			return '=';
		case mrfclkMissing:
			return '-';
		case mrfclkInsert:
			return '+';
		case mrfclkOffset:
			return '.';
		default:
			return ' ';
	}
}

bool fileCompareGuttersContain(const std::string &gutters, char marker) noexcept {
	for (char ch : gutters)
		if (static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) == marker) return true;
	return false;
}

} // namespace

bool mrfeRenderedBlockOverlayLineRangeForRegression(const MRFileEditor &editor, std::size_t &line1, std::size_t &line2) {
	if (!editor.mBlockOverlayActive || editor.mBlockOverlayMode == 0) return false;
	if (editor.mBlockOverlayMode == 2 && editor.mBlockOverlayLineRangeValid) {
		line1 = std::min(editor.mBlockOverlayLine1, editor.mBlockOverlayLine2);
		line2 = std::max(editor.mBlockOverlayLine1, editor.mBlockOverlayLine2);
		return true;
	}
	std::size_t start = editor.mBlockOverlayAnchor;
	std::size_t end = editor.mBlockOverlayTrackCursor ? editor.mBufferModel.cursor() : editor.mBlockOverlayEnd;
	if (start > end) std::swap(start, end);
	end = renderedBlockOverlayEndForViewport(editor.mBufferModel, start, end, editor.mBlockOverlayMode);
	line1 = editor.mBufferModel.lineIndex(start);
	line2 = editor.mBufferModel.lineIndex(end);
	if (line1 > line2) std::swap(line1, line2);
	return true;
}

int mrfeLocalXForVisualColumnForRegression(const MRFileEditor &editor, int visualColumn) {
	return static_cast<int>(editor.textViewportGeometry().localXFromVisualColumn(std::max(0, visualColumn)));
}

bool mrfeRenderedColumnOverlayColumnsForRegression(MRFileEditor &editor, std::size_t lineIndex, int width, int &col1, int &col2) {
	class DrawBufferProbe : public TDrawBuffer {
	  public:
		TColorAttr attrAt(int column) const noexcept {
			return getAttr(data[column]);
		}
	};

	if (width <= 0) return false;
	DrawBufferProbe buffer;
	MRSyntaxLineResult syntaxLine;
	const bool documentLine = lineIndex < editor.mBufferModel.lineCount();
	const std::size_t lineStart = documentLine ? editor.mBufferModel.lineStartByIndex(lineIndex) : lineIndex;
	const TColorAttr selectedColor = editor.tokenColor(MRSyntaxToken::Text, true, editor.getColor(0x0201));
	col1 = -1;
	col2 = -1;
	editor.formatSyntaxLine(buffer, lineStart, lineIndex, syntaxLine, 0, width, 0, documentLine, false, false);
	for (int column = 0; column < width; ++column) {
		if (buffer.attrAt(column) == selectedColor) {
			if (col1 < 0) col1 = column;
			col2 = column + 1;
		}
	}
	return col1 >= 0 && col2 >= col1;
}

unsigned char MRFileEditor::fileCompareLineKindAt(std::size_t lineIndex) const noexcept {
	if (mFileCompareGuttersConfigured && mFileCompareLineKinds->empty()) return mrfclkEqual;
	if (lineIndex >= mFileCompareLineKinds->size()) return mrfclkNone;
	return (*mFileCompareLineKinds)[lineIndex];
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
	inputs.debugGutterEnabled = mDebuggerInstructionLineValid || !mDebuggerBreakpointLines.empty();
	inputs.debugGutterPosition = "LEADING";
	if (foldingEnabled && settings.codeFolding) self->ensureVisibleFoldSpans(static_cast<std::size_t>(std::max(delta.y, 0)), inputs.visibleRows, mBufferModel.language());
	inputs.codeFoldingColumns = foldingEnabled && settings.codeFolding ? self->visibleFoldGutterColumns() : 1;
	inputs.exactLineCountKnown = !approximateLargeFileMetrics && mBufferModel.exactLineCountKnown();
	inputs.exactLineCount = inputs.exactLineCountKnown ? mBufferModel.lineCount() : 0;
	inputs.estimatedLineCount = mBufferModel.estimatedLineCount();
	if (!inputs.exactLineCountKnown) inputs.estimatedLineCount = std::max(inputs.estimatedLineCount, mLineNumberGutterLineCount);
	if (mFileCompareGuttersConfigured) {
		const bool lineNumbersLeading = fileCompareGuttersContain(mFileCompareLeftGutters, 'L');
		const bool lineNumbersTrailing = fileCompareGuttersContain(mFileCompareRightGutters, 'L');
		const bool codeFoldingLeading = fileCompareGuttersContain(mFileCompareLeftGutters, 'C');
		const bool codeFoldingTrailing = fileCompareGuttersContain(mFileCompareRightGutters, 'C');
		const bool miniMapLeading = fileCompareGuttersContain(mFileCompareLeftGutters, 'M');
		const bool miniMapTrailing = fileCompareGuttersContain(mFileCompareRightGutters, 'M');

		viewportSettings.showLineNumbers = lineNumbersLeading || lineNumbersTrailing;
		viewportSettings.lineNumbersPosition = lineNumbersLeading ? "LEADING" : (lineNumbersTrailing ? "TRAILING" : "OFF");
		viewportSettings.codeFolding = codeFoldingLeading || codeFoldingTrailing;
		viewportSettings.codeFoldingPosition = codeFoldingLeading ? "LEADING" : (codeFoldingTrailing ? "TRAILING" : "OFF");
		viewportSettings.miniMapPosition = miniMapLeading ? "LEADING" : (miniMapTrailing ? "TRAILING" : "OFF");
		inputs.gutterSidesConfigured = true;
		inputs.leadingGutters = mFileCompareLeftGutters;
		inputs.trailingGutters = mFileCompareRightGutters;
		inputs.fileCompareGutterEnabled = mFileCompareGutterVisible && (fileCompareGuttersContain(mFileCompareLeftGutters, 'D') || fileCompareGuttersContain(mFileCompareRightGutters, 'D')) && !mFileCompareLineKinds->empty();
	}
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
	TextViewportGeometry viewport = MRTextViewportLayout::geometryFor(viewportSettings, inputs);
	if (inputs.exactLineCountKnown) mLineNumberGutterLineCount = inputs.exactLineCount;
	return viewport;
}

MRFileEditor::TextViewportGeometry MRFileEditor::textViewportGeometry() const noexcept {
	return textViewportGeometryFor(effectiveEditSetupSettings());
}

bool MRFileEditor::shouldShowEditorCursor(long long x, long long y, const TextViewportGeometry &viewport) const noexcept {
	return MRTextViewportLayout::shouldShowCursor(viewport, x, y, visibleTextRows(), (state & sfActive) != 0, (state & sfSelected) != 0);
}

bool MRFileEditor::shouldShowEditorCursor(long long x, long long y) const noexcept {
	TextViewportGeometry viewport = textViewportGeometry();
	return shouldShowEditorCursor(x, y, viewport);
}

TColorAttr MRFileEditor::editorTextFillColor() noexcept {
	unsigned char configured = 0;

	if (mFileCompareGuttersConfigured && configuredColorSlotOverride(kMrPaletteFileCompareTextEqual, configured)) return static_cast<TColorAttr>(configured);
	return tokenColor(MRSyntaxToken::Text, false, getColor(0x0201));
}

int MRFileEditor::textColumnFromLocalX(int localX) const noexcept {
	return textViewportGeometry().textColumnFromLocalX(localX);
}

int MRFileEditor::textViewportWidth() const {
	return textViewportGeometry().width;
}

void MRFileEditor::invalidateFoldCache(bool preserveVisibleProjection) noexcept {
	mFoldOutlineInputCache.reset();
	mFoldState.clearVisibleState(preserveVisibleProjection);
}

int MRFileEditor::visibleFoldGutterColumns() const noexcept {
	return mFoldState.visibleGutterColumns();
}

std::size_t MRFileEditor::documentLineForVisibleLine(std::size_t visibleLine) const noexcept {
	const std::size_t totalLines = mBufferModel.exactLineCountKnown() ? std::max<std::size_t>(1, mBufferModel.lineCount()) : std::max<std::size_t>(1, mBufferModel.estimatedLineCount());
	return mFoldState.documentLineForVisibleLine(visibleLine, totalLines);
}

std::size_t MRFileEditor::visibleLineForDocumentLine(std::size_t documentLine) const noexcept {
	return mFoldState.visibleLineForDocumentLine(documentLine);
}

std::size_t MRFileEditor::foldedVisibleLineCount() const noexcept {
	std::size_t total = mBufferModel.exactLineCountKnown() ? std::max<std::size_t>(1, mBufferModel.lineCount()) : std::max<std::size_t>(1, mBufferModel.estimatedLineCount());
	return mFoldState.foldedLineCount(total);
}

bool MRFileEditor::toggleFoldAtLine(std::size_t lineIndex) {
	std::vector<MRFoldSpan> &visibleFoldSpans = mFoldState.visibleState().spans;
	std::map<std::size_t, MRFoldSpan> &closedFoldSpans = mFoldState.closedFoldSpans();
	for (const MRFoldSpan &span : visibleFoldSpans) {
		if (span.startLine != lineIndex) continue;
		const bool closing = span.open;
		if (mFoldState.documentFoldLevelContains(lineIndex)) {
			const bool projectionCloses = mFoldState.documentFoldLevelCloses(lineIndex);
			if (!closing) closedFoldSpans.erase(lineIndex);
			if (closing != projectionCloses) static_cast<void>(mFoldState.toggleDocumentFoldLevelSpan(lineIndex));
		} else if (closing)
			closedFoldSpans[lineIndex] = MRFoldSpan(span.startLine, span.endLine, span.level, span.sourceKind, false, span.siblingContinuation);
		else
			closedFoldSpans.erase(lineIndex);
		mFoldState.rebuildEffectiveClosedFolds();
		mFoldState.refreshVisibleFoldOpenStates();
		if (closing) {
			const std::size_t cursorLine = mBufferModel.lineIndex(mBufferModel.cursor());
			if (cursorLine > span.startLine && cursorLine <= span.endLine) moveCursor(mBufferModel.lineStartByIndex(span.startLine), false, false);
		}
		static_cast<void>(cancelViewportFoldWarmup());
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
		static_cast<void>(cancelFoldWarmup());
		invalidateFoldCache();
		return;
	}
	const std::size_t docId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	const bool approximateLargeFileMetrics = useApproximateLargeFileMetrics();
	MRFoldingDerivedState::VisibleState &visibleState = mFoldState.visibleState();

	if (rowCount <= 0) {
		static_cast<void>(cancelViewportFoldWarmup());
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
	std::size_t foldRequestBottomLine = requestBottomLine;
	if (mFoldState.documentFoldLevelActive()) {
		const std::size_t localLineBudget = static_cast<std::size_t>(std::max(rowCount * 8, 2048));
		const std::size_t localBottomLine = topLine > std::numeric_limits<std::size_t>::max() - localLineBudget ? std::numeric_limits<std::size_t>::max() : topLine + localLineBudget;
		foldRequestBottomLine = std::min(foldRequestBottomLine, localBottomLine);
		if (mFoldLevelOperationState.generation != 0 && mFoldLevelOperationState.documentId == docId && mFoldLevelOperationState.version == version &&
		    mFoldLevelOperationState.language == language && foldRequestBottomLine > mFoldLevelOperationState.projectionTargetBottomLine) {
			mFoldLevelOperationState.projectionTargetBottomLine = foldRequestBottomLine;
			scheduleDocumentFoldLevelProjection();
		}
	}
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
			for (const MRFoldGutterBranch &branch : visibleState.branches)
				if (branch.line == documentLine) maxDisplayLevel = std::max(maxDisplayLevel, static_cast<int>(branch.level));
		}
		visibleState.gutterColumns = std::max(1, maxDisplayLevel + 1);
		visibleState.displayLevels.clear();
		visibleState.displayLevels.reserve(static_cast<std::size_t>(visibleState.gutterColumns));
		for (int level = 0; level < visibleState.gutterColumns; ++level)
			visibleState.displayLevels.push_back(static_cast<unsigned short>(level));
	};
	if (visibleState.documentId == docId && visibleState.version == version && visibleState.language == language && topLine >= visibleState.topLine &&
	    foldRequestBottomLine <= visibleState.bottomLine) {
		mFoldCanonicalContextState.requestValid = false;
		const bool currentWarmupCoversViewport = mFoldWarmupState.generation != 0 && mFoldWarmupState.documentId == docId && mFoldWarmupState.version == version &&
		                                           mFoldWarmupState.language == language && topLine >= mFoldWarmupState.visibleTopLine &&
		                                           foldRequestBottomLine <= mFoldWarmupState.visibleBottomLine;
		if (mFoldWarmupState.generation != 0 && !currentWarmupCoversViewport) supersedeViewportFoldWarmup();
		updateVisibleFoldGutterColumnsForViewport();
		return;
	}

	const int safeRowCount = std::max(1, rowCount);
	static constexpr std::size_t kCompleteLatexFoldLineBudget = 20000;
	std::size_t viewportMargin = approximateLargeFileMetrics ? static_cast<std::size_t>(std::max(safeRowCount * 2, 64)) : static_cast<std::size_t>(std::max(safeRowCount * 2, 32));
	if (language == MRSyntaxLanguage::Xml) viewportMargin = std::max<std::size_t>(viewportMargin, 512);
	std::size_t scanTopLine = topLine > viewportMargin ? topLine - viewportMargin : 0;
	std::size_t scanBottomLine = foldRequestBottomLine + viewportMargin;
	if (exactLineCountKnown && scanBottomLine > exactLineCount) scanBottomLine = exactLineCount;
	if (language == MRSyntaxLanguage::Latex && exactLineCountKnown && exactLineCount <= kCompleteLatexFoldLineBudget) {
		scanTopLine = 0;
		scanBottomLine = exactLineCount;
	}
	scheduleFoldWarmupIfNeeded(scanTopLine, scanBottomLine, topLine, foldRequestBottomLine, language);
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

bool MRFileEditor::debuggerBreakpointContainsOffset(std::size_t offset) const noexcept {
	for (const MRTextBufferModel::Range &range : mDebuggerBreakpointRanges) {
		if (range.end <= offset) continue;
		if (range.start > offset) break;
		return true;
	}
	return false;
}

bool MRFileEditor::debuggerBreakpointInactiveContainsOffset(std::size_t offset) const noexcept {
	for (const MRTextBufferModel::Range &range : mDebuggerBreakpointInactiveRanges) {
		if (range.end <= offset) continue;
		if (range.start > offset) break;
		return true;
	}
	return false;
}

bool MRFileEditor::debuggerWatchpointActiveContainsOffset(std::size_t offset) const noexcept {
	for (const MRTextBufferModel::Range &range : mDebuggerWatchpointActiveRanges) {
		if (range.end <= offset) continue;
		if (range.start > offset) break;
		return true;
	}
	return false;
}

bool MRFileEditor::debuggerWatchpointInactiveContainsOffset(std::size_t offset) const noexcept {
	for (const MRTextBufferModel::Range &range : mDebuggerWatchpointInactiveRanges) {
		if (range.end <= offset) continue;
		if (range.start > offset) break;
		return true;
	}
	return false;
}

bool MRFileEditor::debuggerWatchpointErrorContainsOffset(std::size_t offset) const noexcept {
	for (const MRTextBufferModel::Range &range : mDebuggerWatchpointErrorRanges) {
		if (range.end <= offset) continue;
		if (range.start > offset) break;
		return true;
	}
	return false;
}

bool MRFileEditor::debuggerVariableChangedContainsOffset(std::size_t offset) const noexcept {
	for (const MRTextBufferModel::Range &range : mDebuggerVariableChangedRanges) {
		if (range.end <= offset) continue;
		if (range.start > offset) break;
		return true;
	}
	return false;
}

bool MRFileEditor::debuggerBreakpointLineAt(std::size_t lineIndex) const noexcept {
	return std::binary_search(mDebuggerBreakpointLines.begin(), mDebuggerBreakpointLines.end(), lineIndex);
}

bool MRFileEditor::debuggerBreakpointInactiveLineAt(std::size_t lineIndex) const noexcept {
	return std::binary_search(mDebuggerBreakpointInactiveLines.begin(), mDebuggerBreakpointInactiveLines.end(), lineIndex);
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
	const std::size_t cursorLine = cachedCursorLineIndex();
	std::size_t lineStart = mBufferModel.lineStart(mBufferModel.cursor());
	const std::size_t distance = index > cursorLine ? index - cursorLine : cursorLine - index;
	static constexpr std::size_t kLocalLineWalkLimit = 1024;

	if (distance <= kLocalLineWalkLimit) {
		if (index < cursorLine) {
			for (std::size_t line = cursorLine; line > index; --line)
				lineStart = mBufferModel.prevLine(lineStart);
		} else {
			for (std::size_t line = cursorLine; line < index && lineStart < mBufferModel.length(); ++line)
				lineStart = mBufferModel.nextLine(lineStart);
		}
		return lineStart;
	}
	if (mBufferModel.exactLineCountKnown()) return mBufferModel.lineStartByIndex(index);

	const std::size_t estimatedLines = std::max<std::size_t>(1, mBufferModel.estimatedLineCount());
	const long double ratio = static_cast<long double>(std::min(index, estimatedLines - 1)) / static_cast<long double>(estimatedLines);
	const std::size_t estimatedOffset = static_cast<std::size_t>(ratio * static_cast<long double>(mBufferModel.length()));
	return mBufferModel.lineStart(estimatedOffset);
}

bool MRFileEditor::useApproximateLargeFileMetrics() const noexcept {
	const MRTextBufferModel::Document &document = mBufferModel.document();
	return document.length() >= static_cast<std::size_t>(8) * 1024 * 1024;
}

int MRFileEditor::dynamicLargeFileLineLimit() const noexcept {
	const std::size_t estimated = mBufferModel.estimatedLineCount();
	const std::size_t currentLine = visibleLineForDocumentLine(displayedCursorLineIndex());
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
	TColorAttr accent = static_cast<TColorAttr>(getColor(0x0404));
	const std::string normalized = normalizedFormatRulerLine(settings);
	const std::size_t cursorLineIndex = cachedCursorLineIndex();

	if (mFileCompareGuttersConfigured && configuredColorSlotOverride(kMrPaletteFileCompareFormatRuler, configured)) {
		normal = static_cast<TColorAttr>(configured);
		accent = normal;
	} else if (configuredColorSlotOverride(kMrPaletteFormatRuler, configured))
		normal = static_cast<TColorAttr>(configured);
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
	MREditSetupSettings settings = effectiveEditSetupSettings();
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
	const MREditSetupSettings initialSettings = effectiveEditSetupSettings();
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
	if (!mFileCompareLineKinds->empty()) hideCursor();
	syncScrollBarsToState();
	MREditSetupSettings editSettings = effectiveEditSetupSettings();
	const bool foldingEnabled = foldingPipelineEnabled();
	const bool miniMapEnabled = miniMapPipelineEnabled();
	std::size_t totalLines = 1;
	TextViewportGeometry viewport = textViewportGeometryFor(editSettings);
	bool showLineNumbers = viewport.lineNumberWidth > 0;
	bool drawCodeFolding = foldingEnabled && viewport.codeFoldingWidth > 0;
	bool drawLeadingDiffGutter = viewport.fileCompareLeadingGutterWidth > 0;
	bool drawTrailingDiffGutter = viewport.fileCompareTrailingGutterWidth > 0;
	bool zeroFillLineNumbers = showLineNumbers && editSettings.lineNumZeroFill;
	int textWidth = viewport.width;
	MRTextBufferModel::Range selection = mBufferModel.selection().range().normalized();
	const MRSyntaxLanguage syntaxLanguage = mBufferModel.language();
	const bool syntaxEnabled = syntaxPipelineEnabled();
	const bool statefulSyntax = syntaxEnabled && isStatefulSyntaxLanguage(syntaxLanguage);
	MRMiniMapRenderer::Palette miniMapPalette = resolveMiniMapPalette();
	const bool drawLeadingMiniMap = miniMapEnabled && viewport.miniMapBodyWidth > 0 && viewport.miniMapLeadingInfoX >= 0;
	const bool drawTrailingMiniMap = miniMapEnabled && viewport.miniMapBodyWidth > 0 && viewport.miniMapTrailingInfoX >= 0;
	const bool drawMiniMap = drawLeadingMiniMap || drawTrailingMiniMap;
	const bool miniMapUseBraille = MRMiniMapRenderer::useBrailleRenderer();
	std::string viewportMarkerGlyph = MRMiniMapRenderer::normalizedViewportMarkerGlyph(editSettings.miniMapMarkerGlyph);
	const bool foldedView = foldingEnabled && mFoldState.hasEffectiveClosedFolds();
	const int miniMapRows = std::max(0, visibleTextRows());
	const int textRows = std::max(0, visibleTextRows());
	const TColorAttr editorTextFill = editorTextFillColor();
	const bool exactLineCountKnown = mBufferModel.exactLineCountKnown();
	const std::size_t exactLineCount = exactLineCountKnown ? std::max<std::size_t>(1, mBufferModel.lineCount()) : 0;
	if (exactLineCountKnown) totalLines = foldedView ? foldedVisibleLineCount() : exactLineCount;
	else
		totalLines = std::max<std::size_t>(1, std::max<std::size_t>(mBufferModel.estimatedLineCount(), static_cast<std::size_t>(std::max(delta.y, 0)) + static_cast<std::size_t>(std::max(miniMapRows, 1))));
	if (!foldedView) {
		totalLines = std::max(totalLines, displayedCursorLineIndex() + 1);
		if (mBlockOverlayActive && mBlockOverlayMode == 2 && mBlockOverlayLineRangeValid) totalLines = std::max(totalLines, std::max(mBlockOverlayLine1, mBlockOverlayLine2) + 1);
	}
	std::size_t topLine = static_cast<std::size_t>(std::max(delta.y, 0));
	if (topLine >= totalLines) topLine = totalLines - 1;
	std::size_t lineIndex = documentLineForVisibleLine(topLine);
	std::size_t linePtr = lineStartForIndex(lineIndex);
	const auto miniMapViewportFor = [&](bool leadingSide) noexcept {
		if (leadingSide) return MRMiniMapRenderer::Viewport{viewport.width, viewport.miniMapLeadingBodyX, viewport.miniMapBodyWidth, viewport.miniMapLeadingInfoX, viewport.miniMapLeadingSeparatorX};
		return MRMiniMapRenderer::Viewport{viewport.width, viewport.miniMapTrailingBodyX, viewport.miniMapBodyWidth, viewport.miniMapTrailingInfoX, viewport.miniMapTrailingSeparatorX};
	};
	const MRMiniMapRenderer::Viewport miniMapViewport = miniMapViewportFor(drawLeadingMiniMap);
	if (drawMiniMap) {
		MRMiniMapRenderer::Signals miniMapSignals = mMiniMapState.renderer().scheduleWarmupIfNeeded(miniMapViewport, miniMapRows, miniMapUseBraille, totalLines, topLine, mBufferModel.documentId(), mBufferModel.version(), mExecutionOwnerKind, mExecutionOwnerLocalId,
		                                                                                      mBufferModel.readSnapshot(), editSettings, mMiniMapState.overlaySources(), selection);
		applyMiniMapSignals(miniMapSignals);
	}
	const MRMiniMapRenderer::OverlayState &miniMapOverlay = mMiniMapState.renderer().overlayProjection();
	if (size.x > 0 && size.y > 0) {
		const std::size_t nonDocumentLineIndex = std::numeric_limits<std::size_t>::max();
		const std::size_t documentRows = topLine < totalLines ? std::min<std::size_t>(static_cast<std::size_t>(textRows), totalLines - topLine) : 0;
		bool eofDocumentLineVisible = false;

		if (editSettings.showEofMarker && exactLineCountKnown && documentRows > 0) {
			const std::size_t lastVisibleDocumentLine = documentLineForVisibleLine(topLine + documentRows - 1);
			eofDocumentLineVisible = lastVisibleDocumentLine < exactLineCount && lineStartForIndex(lastVisibleDocumentLine) == mBufferModel.length();
		}

		for (int y = static_cast<int>(documentRows); y < textRows; ++y) {
			TDrawBuffer gutterBackground;
			const bool drawEofMarker = editSettings.showEofMarker && !eofDocumentLineVisible && y == static_cast<int>(documentRows);
			const std::size_t virtualLineIndex = topLine + static_cast<std::size_t>(y);

			gutterBackground.moveChar(0, ' ', editorTextFill, static_cast<ushort>(std::max(0, size.x)));
			if (showLineNumbers) drawLineNumberGutter(gutterBackground, 0, false, viewport.lineNumberX, viewport.lineNumberWidth, zeroFillLineNumbers, nonDocumentLineIndex);
			if (drawLeadingDiffGutter) drawFileCompareGutter(gutterBackground, viewport.fileCompareLeadingGutterX, viewport.fileCompareLeadingGutterWidth, nonDocumentLineIndex);
			if (drawTrailingDiffGutter) drawFileCompareGutter(gutterBackground, viewport.fileCompareTrailingGutterX, viewport.fileCompareTrailingGutterWidth, nonDocumentLineIndex);
			if (drawCodeFolding) drawCodeFoldingGutter(gutterBackground, viewport.codeFoldingX, viewport.codeFoldingWidth, 0, nonDocumentLineIndex);
			if (drawLeadingMiniMap) mMiniMapState.renderer().drawGutter(gutterBackground, y, miniMapRows, size.x, miniMapViewportFor(true), totalLines, topLine, miniMapUseBraille, viewportMarkerGlyph, miniMapPalette, miniMapOverlay);
			if (drawTrailingMiniMap) mMiniMapState.renderer().drawGutter(gutterBackground, y, miniMapRows, size.x, miniMapViewportFor(false), totalLines, topLine, miniMapUseBraille, viewportMarkerGlyph, miniMapPalette, miniMapOverlay);
			if (drawEofMarker) formatSyntaxLine(gutterBackground, virtualLineIndex, virtualLineIndex, MRSyntaxLineResult(), delta.x, textWidth, viewport.textLeft, false, true, editSettings.showEofMarkerEmoji);
			writeBuf(0, y + viewport.topInset, size.x, 1, gutterBackground);
		}
	}
	if (editSettings.formatRuler && viewport.topInset > 0) drawFormatRulerOverlay(viewport, editSettings);
	for (int y = 0; y < textRows; ++y) {
		TDrawBuffer buffer;
		const std::size_t visibleLineIndex = topLine + static_cast<std::size_t>(y);
		if (visibleLineIndex >= totalLines) break;
		const std::size_t currentLineIndex = lineIndex;
		const bool isDocumentLine = exactLineCountKnown ? currentLineIndex < exactLineCount : (linePtr < mBufferModel.length() || (mBufferModel.length() == 0 && currentLineIndex == 0));
		const std::size_t currentLinePtr = isDocumentLine ? linePtr : currentLineIndex;
		MRSyntaxLineResult syntaxLine;
		buffer.moveChar(0, ' ', editorTextFill, static_cast<ushort>(std::max(0, size.x)));
		if (showLineNumbers) {
			std::size_t displayLineNumber = currentLineIndex + 1;
			if (mCommunicationViewerMode && mCommunicationViewerScrollDirection == MRLiveLogScrollDirection::Up) {
				const std::size_t totalLineCount =
				    mBufferModel.exactLineCountKnown() ? std::max<std::size_t>(1, mBufferModel.lineCount()) : std::max<std::size_t>(1, mBufferModel.estimatedLineCount());
				if (currentLineIndex < totalLineCount) displayLineNumber = totalLineCount - currentLineIndex;
			}
			drawLineNumberGutter(buffer, displayLineNumber, isDocumentLine, viewport.lineNumberX, viewport.lineNumberWidth, zeroFillLineNumbers, currentLineIndex);
		}
		if (drawLeadingDiffGutter) drawFileCompareGutter(buffer, viewport.fileCompareLeadingGutterX, viewport.fileCompareLeadingGutterWidth, currentLineIndex);
		if (drawTrailingDiffGutter) drawFileCompareGutter(buffer, viewport.fileCompareTrailingGutterX, viewport.fileCompareTrailingGutterWidth, currentLineIndex);
		drawDebugGutter(buffer, viewport.debugGutterX, viewport.debugGutterWidth, currentLineIndex);
		if (drawCodeFolding) drawCodeFoldingGutter(buffer, viewport.codeFoldingX, viewport.codeFoldingWidth, isDocumentLine ? currentLinePtr : 0, currentLineIndex);
		if (drawLeadingMiniMap) mMiniMapState.renderer().drawGutter(buffer, y, miniMapRows, size.x, miniMapViewportFor(true), totalLines, topLine, miniMapUseBraille, viewportMarkerGlyph, miniMapPalette, miniMapOverlay);
		if (drawTrailingMiniMap) mMiniMapState.renderer().drawGutter(buffer, y, miniMapRows, size.x, miniMapViewportFor(false), totalLines, topLine, miniMapUseBraille, viewportMarkerGlyph, miniMapPalette, miniMapOverlay);
		if (syntaxEnabled && isDocumentLine) {
			std::map<std::size_t, MRSyntaxCacheEntry>::const_iterator found = mSyntaxState.tokenCache().find(currentLinePtr);
			const bool statefulCacheReady = !statefulSyntax || syntaxWarmedLineRangeCovered(currentLineIndex, currentLineIndex + 1);

			if (found != mSyntaxState.tokenCache().end() && statefulCacheReady) syntaxLine = found->second.syntaxLine;
		}
		const bool drawEofMarker = editSettings.showEofMarker && isDocumentLine && currentLinePtr == mBufferModel.length();
		formatSyntaxLine(buffer, currentLinePtr, currentLineIndex, syntaxLine, delta.x, textWidth, viewport.textLeft, isDocumentLine, drawEofMarker, drawEofMarker && editSettings.showEofMarkerEmoji);
		writeBuf(0, y + viewport.topInset, size.x, 1, buffer);
		const MRFoldSpan *closedFold = foldedView ? mFoldState.effectiveClosedFoldStartingAt(lineIndex) : nullptr;
		if (closedFold != nullptr) {
			lineIndex = closedFold->endLine + 1;
			linePtr = lineStartForIndex(lineIndex);
		} else {
			if (isDocumentLine && linePtr < mBufferModel.length()) linePtr = mBufferModel.nextLine(linePtr);
			++lineIndex;
		}
	}
	if (syntaxEnabled) scheduleSyntaxWarmupIfNeeded();
	updateIndicator();
}

void MRFileEditor::drawLineNumberGutter(TDrawBuffer &b, std::size_t lineNumber, bool showNumber, int drawX, int width, bool zeroFill, std::size_t) {
	TColorAttr color = static_cast<TColorAttr>(getColor(0x0606));
	unsigned char configured = 0;
	char numberBuffer[32];
	int digits = std::max(1, width);
	int numberX = drawX;
	int numberWidth = width;

	if (width <= 0) return;
	if (mFileCompareGuttersConfigured && configuredColorSlotOverride(kMrPaletteFileCompareLineNumbers, configured)) color = static_cast<TColorAttr>(configured);
	b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
	if (!showNumber) return;
	digits = std::max(1, numberWidth);
	if (zeroFill) std::snprintf(numberBuffer, sizeof(numberBuffer), "%0*lu", digits, static_cast<unsigned long>(lineNumber));
	else
		std::snprintf(numberBuffer, sizeof(numberBuffer), "%*lu", digits, static_cast<unsigned long>(lineNumber));
	b.moveStr(static_cast<ushort>(numberX), numberBuffer, color, static_cast<ushort>(numberWidth));
}

void MRFileEditor::drawFileCompareGutter(TDrawBuffer &b, int drawX, int width, std::size_t lineIndex) {
	TColorAttr color = static_cast<TColorAttr>(getColor(0x0606));
	unsigned char configured = 0;
	const unsigned char lineKind = fileCompareLineKindAt(lineIndex);

	if (width <= 0) return;
	if (configuredColorSlotOverride(kMrPaletteFileCompareLineNumbers, configured)) color = static_cast<TColorAttr>(configured);
	b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
	if (lineKind == mrfclkNone) return;
	const unsigned char slot = fileCompareGutterPaletteSlot(lineKind);
	if (slot != 0 && configuredColorSlotOverride(slot, configured)) color = static_cast<TColorAttr>(configured);
	b.moveChar(static_cast<ushort>(drawX), fileCompareGutterGlyph(lineKind), color, 1);
}

void MRFileEditor::drawDebugGutter(TDrawBuffer &b, int drawX, int width, std::size_t lineIndex) {
	unsigned char configured = 0;
	TColorAttr color = static_cast<TColorAttr>(getColor(0x0606));
	const bool instructionLine = mDebuggerInstructionLineValid && mDebuggerInstructionLine == lineIndex;
	const bool breakpointLine = debuggerBreakpointLineAt(lineIndex);
	const bool breakpointInactiveLine = debuggerBreakpointInactiveLineAt(lineIndex);

	if (width <= 0) return;
	b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
	if (!instructionLine && !breakpointLine && !breakpointInactiveLine) return;
	if (instructionLine) {
		if (configuredColorSlotOverride(kMrPaletteDebuggerInstructionPointer, configured)) color = static_cast<TColorAttr>(configured);
		else
			color = static_cast<TColorAttr>(0xE0);
		b.moveChar(static_cast<ushort>(drawX), '\x10', color, 1);
		return;
	}
	if (breakpointLine) {
		if (configuredColorSlotOverride(kMrPaletteDebuggerBreakpointActive, configured)) color = static_cast<TColorAttr>(configured);
		else
			color = static_cast<TColorAttr>(0x4E);
		b.moveChar(static_cast<ushort>(drawX), '\x07', color, 1);
		return;
	}
	if (configuredColorSlotOverride(kMrPaletteDebuggerBreakpointInactive, configured)) color = static_cast<TColorAttr>(configured);
	else
		color = static_cast<TColorAttr>(0x18);
	b.moveChar(static_cast<ushort>(drawX), '\x07', color, 1);
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
	for (const MRFoldGutterBranch &branch : mFoldState.visibleState().branches) {
		const int displayColumn = displayColumnForLevel(branch.level);

		if (branch.line != lineIndex || displayColumn < 0 || displayColumn >= width) continue;
		b.moveStr(static_cast<ushort>(drawX + displayColumn), "\xE2\x94\x9C", markerColor, 1);
	}
}

TColorAttr MRFileEditor::tokenColor(MRSyntaxToken token, bool selected, TAttrPair pair) noexcept {
	TColorAttr normal = static_cast<TColorAttr>(pair);
	TColorAttr selectedAttr = static_cast<TColorAttr>(pair >> 8);
	uchar background = static_cast<uchar>((selected ? selectedAttr : normal) & 0xF0);
	auto configuredCodeColor = [background](unsigned char paletteSlot, unsigned char fallbackForeground) noexcept -> TColorAttr {
		unsigned char configured = 0;

		if (configuredColorSlotOverride(paletteSlot, configured)) return static_cast<TColorAttr>(configured);
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

void MRFileEditor::formatSyntaxLine(TDrawBuffer &b, std::size_t lineStart, std::size_t lineIndex, const MRSyntaxLineResult &syntaxLine, int hScroll, int width, int drawX, bool isDocumentLine, bool drawEofMarker, bool drawEofMarkerAsEmoji) {
	TAttrPair basePair = getColor(0x0201);
	TAttrPair changedPair = getColor(0x0505);
	TAttrPair selectionPair = getColor(0x0201);
	MRTextBufferModel::Range selection;
	std::size_t documentLength = mBufferModel.length();
	std::size_t lineEnd = lineStart;
	std::size_t cursorPos = 0;
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
	const MREditSetupSettings settings = effectiveEditSetupSettings();
	const bool displayTabs = configuredDisplayTabs();
	unsigned char diffLineKind = mrfclkNone;
	bool diffTextActive = false;
	TColorAttr diffTextColor = 0;

	hScroll = std::max(hScroll, 0);
	width = std::max(width, 0);
	drawX = std::max(drawX, 0);
	if (!isDocumentLine) {
		TColorAttr color = editorTextFillColor();
		const std::size_t virtualLineIndex = lineStart;
		b.moveChar(static_cast<ushort>(drawX), ' ', color, static_cast<ushort>(width));
		if (displayedCursorLineIndex() == virtualLineIndex) b.moveChar(static_cast<ushort>(drawX), ' ', static_cast<TColorAttr>(getColor(0x0303)), static_cast<ushort>(width));
		if (mBlockOverlayActive && mBlockOverlayMode == 2 && mBlockOverlayLineRangeValid) {
			const std::size_t overlayLine1 = std::min(mBlockOverlayLine1, mBlockOverlayLine2);
			const std::size_t overlayLine2 = std::max(mBlockOverlayLine1, mBlockOverlayLine2);
			const int overlayCol1 = std::min(mBlockOverlayColumnAnchor, mBlockOverlayColumnEnd);
			const int overlayCol2Exclusive = std::max(mBlockOverlayColumnAnchor, mBlockOverlayColumnEnd);

			if (overlayLine1 <= virtualLineIndex && virtualLineIndex <= overlayLine2 && overlayCol2Exclusive > overlayCol1) {
				const int selectedStartX = std::max(0, overlayCol1 - hScroll);
				const int selectedEndX = std::min(width, overlayCol2Exclusive - hScroll);

				if (selectedEndX > selectedStartX)
					b.moveChar(static_cast<ushort>(drawX + selectedStartX), ' ', static_cast<TColorAttr>(getColor(0x0201) >> 8), static_cast<ushort>(selectedEndX - selectedStartX));
			}
		}
		if (drawEofMarker) drawEofMarkerGlyph(b, hScroll, width, drawX, basePair, drawEofMarkerAsEmoji);
		return;
	}
	std::string lineText = mBufferModel.lineText(lineStart);
	TStringView line(lineText.data(), lineText.size());
	selection = mBufferModel.selection().range();
	lineEnd = mBufferModel.nextLine(lineStart);
	cursorPos = mBufferModel.cursor();
	overlayActive = mBlockOverlayActive;
	if (overlayActive) {
		overlayMode = mBlockOverlayMode;
		overlayStart = mBlockOverlayAnchor;
		overlayEnd = mBlockOverlayTrackCursor ? mBufferModel.cursor() : mBlockOverlayEnd;
		if (overlayStart > overlayEnd) std::swap(overlayStart, overlayEnd);
		overlayEnd = renderedBlockOverlayEndForViewport(mBufferModel, overlayStart, overlayEnd, overlayMode);
		if (overlayMode == 2 && mBlockOverlayLineRangeValid) {
			overlayLine1 = mBlockOverlayLine1;
			overlayLine2 = mBlockOverlayLine2;
		} else {
			overlayLine1 = mBufferModel.lineIndex(overlayStart);
			overlayLine2 = mBufferModel.lineIndex(overlayEnd);
		}
		if (overlayLine1 > overlayLine2) std::swap(overlayLine1, overlayLine2);
	}
	overlayCol1 = std::min(mBlockOverlayColumnAnchor, mBlockOverlayColumnEnd);
	overlayCol2Exclusive = std::max(mBlockOverlayColumnAnchor, mBlockOverlayColumnEnd);
	const bool emptyEofDocumentLine = lineStart == documentLength && lineEnd == documentLength;
	currentLine = !emptyEofDocumentLine && lineStart <= cursorPos && cursorPos < lineEnd;
	if (overlayActive) {
		if (overlayMode == 3) currentLineInBlock = false;
		else
			currentLineInBlock = currentLine && overlayLine1 <= lineIndex && lineIndex <= overlayLine2;
	} else
		currentLineInBlock = false;
	diffLineKind = fileCompareLineKindAt(lineIndex);
	if (currentLineInBlock) basePair = getColor(0x0204);
	else if (currentLine && diffLineKind == mrfclkNone)
		basePair = getColor(0x0303);
	if (mDebuggerInstructionLineValid && mDebuggerInstructionLine == lineIndex) {
		unsigned char executionLineAttr = 0;

		if (configuredColorSlotOverride(kMrPaletteDebuggerExecutionLine, executionLineAttr)) basePair = TAttrPair(executionLineAttr);
		else
			basePair = TAttrPair(0x1E);
	}
	if (diffLineKind != mrfclkNone) {
		const unsigned char slot = fileCompareTextPaletteSlot(diffLineKind);
			unsigned char configured = 0;

			if (slot != 0 && configuredColorSlotOverride(slot, configured)) {
				diffTextColor = static_cast<TColorAttr>(configured);
				diffTextActive = true;
			}
		}

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
			} else {
				selected = selection.start <= documentPos && documentPos < selection.end;
			}
			bool changedChar = !currentLine && !currentLineInBlock && isDirtyOffset(documentPos);
			bool findMarkedChar = !selected && findMarkerContainsOffset(documentPos);
			bool debuggerBreakpointChar = !selected && debuggerBreakpointContainsOffset(documentPos);
			bool debuggerBreakpointInactiveChar = !selected && debuggerBreakpointInactiveContainsOffset(documentPos);
			bool debuggerWatchpointActiveChar = !selected && debuggerWatchpointActiveContainsOffset(documentPos);
			bool debuggerWatchpointInactiveChar = !selected && debuggerWatchpointInactiveContainsOffset(documentPos);
			bool debuggerWatchpointErrorChar = !selected && debuggerWatchpointErrorContainsOffset(documentPos);
			bool debuggerVariableChangedChar = !selected && debuggerVariableChangedContainsOffset(documentPos);
			TAttrPair effectivePair = changedChar ? changedPair : basePair;
			TColorAttr unselectedColor = tokenColor(token, false, effectivePair);
			TColorAttr selectedColor = tokenColor(token, true, selectionPair);
			tokenPair = selected ? selectionPair : effectivePair;
			color = selected ? selectedColor : unselectedColor;
			if (findMarkedChar) {
				unsigned char highlightedTextAttr = 0;
				if (configuredColorSlotOverride(14, highlightedTextAttr)) color = static_cast<TColorAttr>(TAttrPair(highlightedTextAttr));
				else
					color = static_cast<TColorAttr>(getColor(3));
			}
			if (debuggerBreakpointChar) {
				unsigned char breakpointAttr = 0;
				if (configuredColorSlotOverride(kMrPaletteDebuggerBreakpointActive, breakpointAttr)) color = static_cast<TColorAttr>(TAttrPair(breakpointAttr));
				else
					color = static_cast<TColorAttr>(TAttrPair(0x4E));
			}
			if (debuggerBreakpointInactiveChar) {
				unsigned char breakpointAttr = 0;
				if (configuredColorSlotOverride(kMrPaletteDebuggerBreakpointInactive, breakpointAttr)) color = static_cast<TColorAttr>(TAttrPair(breakpointAttr));
				else
					color = static_cast<TColorAttr>(TAttrPair(0x18));
			}
			if (debuggerWatchpointActiveChar) {
				unsigned char watchpointAttr = 0;
				if (configuredColorSlotOverride(kMrPaletteDebuggerWatchpointActive, watchpointAttr)) color = static_cast<TColorAttr>(TAttrPair(watchpointAttr));
				else
					color = static_cast<TColorAttr>(TAttrPair(0x3E));
			}
			if (debuggerWatchpointInactiveChar) {
				unsigned char watchpointAttr = 0;
				if (configuredColorSlotOverride(kMrPaletteDebuggerWatchpointInactive, watchpointAttr)) color = static_cast<TColorAttr>(TAttrPair(watchpointAttr));
				else
					color = static_cast<TColorAttr>(TAttrPair(0x38));
			}
			if (debuggerWatchpointErrorChar) {
				unsigned char watchpointAttr = 0;
				if (configuredColorSlotOverride(kMrPaletteDebuggerWatchpointError, watchpointAttr)) color = static_cast<TColorAttr>(TAttrPair(watchpointAttr));
				else
					color = static_cast<TColorAttr>(TAttrPair(0x4F));
			}
			if (debuggerVariableChangedChar) {
				unsigned char valueChangedAttr = 0;
				if (configuredColorSlotOverride(kMrPaletteDebuggerValueChanged, valueChangedAttr)) color = static_cast<TColorAttr>(TAttrPair(valueChangedAttr));
				else
					color = static_cast<TColorAttr>(TAttrPair(0x2E));
			}
			if (diffTextActive && !selected) color = diffTextColor;
			if (!selected) unselectedColor = color;
			visibleWidth = nextVisual - std::max(visual, hScroll);

			if (line[bytePos] == '\t' && overlayActive && overlayMode == 2 && overlayLine1 <= lineIndex && lineIndex <= overlayLine2 && charWidth > 1 && visibleWidth > 0) {
				const int visibleStart = std::max(visual, hScroll);
				const int visibleEnd = nextVisual;
				int drawColumn = drawX + x;
				int cell = visibleStart;
				while (cell < visibleEnd) {
					const bool cellSelected = overlayCol1 <= cell && cell < overlayCol2Exclusive;
					const TColorAttr cellColor = cellSelected ? selectedColor : unselectedColor;
					const int segmentStart = cell;
					int segmentEnd = std::min(visibleEnd, cellSelected ? overlayCol2Exclusive : overlayCol1);
					if (segmentEnd <= segmentStart) segmentEnd = visibleEnd;
					if (displayTabs && segmentStart == visual)
						b.moveStr(static_cast<ushort>(drawColumn), "\xE2\x96\xB6", cellColor, 1);
					else
						b.moveChar(static_cast<ushort>(drawColumn), ' ', cellColor, 1);
					if (segmentEnd - segmentStart > 1) b.moveChar(static_cast<ushort>(drawColumn + 1), ' ', cellColor, static_cast<ushort>(segmentEnd - segmentStart - 1));
					drawColumn += segmentEnd - segmentStart;
					cell = segmentEnd;
				}
			} else if (line[bytePos] == '\t' && displayTabs && visual >= hScroll && visibleWidth > 0) {
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
		TColorAttr selectedColor = tokenColor(MRSyntaxToken::Text, true, selectionPair);
		int selectedStartX = width;
		int selectedEndX = width;

		if (overlayActive) {
			if (overlayMode == 1 && overlayLine1 <= lineIndex && lineIndex <= overlayLine2) {
				selectedStartX = x;
				selectedEndX = width;
			} else if (overlayMode == 2 && overlayLine1 <= lineIndex && lineIndex <= overlayLine2) {
				selectedStartX = overlayCol1 - hScroll;
				selectedEndX = overlayCol2Exclusive - hScroll;
			} else if (overlayMode == 3) {
				const std::size_t streamLine1 = mBufferModel.lineIndex(overlayStart);
				const std::size_t streamLine2 = mBufferModel.lineIndex(overlayEnd);

				if (streamLine1 <= lineIndex && lineIndex <= streamLine2) {
					int selectedStartVisual = hScroll;
					int selectedEndVisual = hScroll + width;

					if (streamLine1 == streamLine2) {
						selectedStartVisual = charColumn(mBufferModel.lineStart(overlayStart), overlayStart);
						selectedEndVisual = charColumn(mBufferModel.lineStart(overlayEnd), overlayEnd);
					} else if (lineIndex == streamLine1) {
						selectedStartVisual = charColumn(mBufferModel.lineStart(overlayStart), overlayStart);
					} else if (lineIndex == streamLine2) {
						selectedEndVisual = charColumn(mBufferModel.lineStart(overlayEnd), overlayEnd);
					}
					selectedStartX = selectedStartVisual - hScroll;
					selectedEndX = selectedEndVisual - hScroll;
				}
			}
		}
		selectedStartX = std::max(x, std::min(width, selectedStartX));
		selectedEndX = std::max(x, std::min(width, selectedEndX));
		if (diffTextActive) color = diffTextColor;
		if (selectedStartX < selectedEndX) {
			if (x < selectedStartX) b.moveChar(static_cast<ushort>(drawX + x), ' ', color, static_cast<ushort>(selectedStartX - x));
			b.moveChar(static_cast<ushort>(drawX + selectedStartX), ' ', selectedColor, static_cast<ushort>(selectedEndX - selectedStartX));
			if (selectedEndX < width) b.moveChar(static_cast<ushort>(drawX + selectedEndX), ' ', color, static_cast<ushort>(width - selectedEndX));
		} else
			b.moveChar(static_cast<ushort>(drawX + x), ' ', color, static_cast<ushort>(width - x));
	}
	if (drawEofMarker) drawEofMarkerGlyph(b, hScroll, width, drawX, basePair, drawEofMarkerAsEmoji);
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
	const bool quitTail = quitTailTraceActive();

	limitX = displayWidthLimitExact() ? provisionalDisplayWidthLimit() : std::max(dynamicLargeFileWidthLimit(), provisionalDisplayWidthLimit());
	if (useApproximateLargeFileMetrics() || quitTail || !mBufferModel.exactLineCountKnown()) {
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

void MRFileEditor::updateIndicator() {
	if (mIndicatorUpdateInProgress) return;
	mIndicatorUpdateInProgress = true;
	TextViewportGeometry viewport = textViewportGeometry();
	unsigned long visualColumn = static_cast<unsigned long>(displayedCursorColumn());
	unsigned long line = static_cast<unsigned long>(visibleLineForDocumentLine(displayedCursorLineIndex()));
	long long localX = viewport.localXFromVisualColumn(static_cast<long long>(visualColumn));
	long long localY = static_cast<long long>(line) - delta.y + viewport.topInset;

	if (mIndicator != nullptr) {
		if (auto *mrIndicator = dynamic_cast<MRIndicator *>(mIndicator)) mrIndicator->setDisplayValue(visualColumn, line, mBufferModel.isModified() ? True : False);
		else {
			TPoint location = {short(visualColumn > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : visualColumn), short(line > static_cast<unsigned long>(SHRT_MAX) ? SHRT_MAX : line)};
			mIndicator->setValue(location, mBufferModel.isModified() ? True : False);
		}
	}

	const bool cursorInViewport = viewport.containsTextPoint(localX, localY, visibleTextRows());
	if (cursorInViewport) setCursor(static_cast<int>(localX), static_cast<int>(localY));
	if (shouldShowEditorCursor(localX, localY, viewport)) {
		showCursor();
	} else
		hideCursor();
	mIndicatorUpdateInProgress = false;
}
