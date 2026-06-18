#include "MRFileEditor.hpp"

#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>

namespace {

static constexpr auto kSlowNavigationTraceThreshold = std::chrono::microseconds(2000);

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

std::size_t MRFileEditor::cachedCursorLineIndex() const noexcept {
	const std::size_t documentId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	const std::size_t cursor = mBufferModel.cursor();

	if (mCachedCursorLineDocumentId == documentId && mCachedCursorLineVersion == version && mCachedCursorLineOffset == cursor) return mCachedCursorLineIndexValue;

	mCachedCursorLineDocumentId = documentId;
	mCachedCursorLineVersion = version;
	mCachedCursorLineOffset = cursor;
	mCachedCursorLineIndexValue = mBufferModel.lineIndex(cursor);
	return mCachedCursorLineIndexValue;
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

std::size_t MRFileEditor::selectionAnchorOffset() const noexcept {
	return mSelectionAnchor;
}

std::size_t MRFileEditor::selectionCursorOffset() const noexcept {
	return mBufferModel.cursor();
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
	const auto startedAt = std::chrono::steady_clock::now();
	std::size_t lineStart = mBufferModel.lineStart(start);
	std::string lineText = mBufferModel.lineText(lineStart);
	TStringView line(lineText.data(), lineText.size());
	const MREditSetupSettings settings = effectiveEditSetupSettings();
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
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowNavigationTraceThreshold) {
		std::ostringstream trace;
		trace << "Phase1 nav charColumn total_us=" << traceMicros(totalElapsed) << " start=" << start << " pos=" << pos << " line_start=" << lineStart << " end_bytes=" << end
		      << " line_bytes=" << line.size() << " len=" << mBufferModel.length() << " add=" << mBufferModel.document().addBufferLength() << " pieces=" << mBufferModel.document().pieceCount();
		appendDirectProbeLog(trace.str());
	}
	return visual;
}

void MRFileEditor::setCursorOffset(std::size_t pos, int) {
	moveCursor(std::min(pos, mBufferModel.length()), false, false);
}

void MRFileEditor::setCursorOffsetAtVisualColumn(std::size_t pos, int visualColumn) {
	moveCursor(std::min(pos, mBufferModel.length()), false, false, visualColumn);
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

bool MRFileEditor::textPointInView(TPoint where) noexcept {
	const TPoint local = makeLocal(where);
	const TextViewportGeometry viewport = textViewportGeometry();

	return viewport.containsTextPoint(local.x, local.y, visibleTextRows());
}

int MRFileEditor::currentLineNumber() const noexcept {
	return static_cast<int>(cachedCursorLineIndex()) + 1;
}

int MRFileEditor::currentColumnNumber() const noexcept {
	return displayedCursorColumn() + 1;
}

int MRFileEditor::currentViewRow() const noexcept {
	return std::max(1, static_cast<int>(visibleLineForDocumentLine(cachedCursorLineIndex())) - delta.y + 1);
}

int MRFileEditor::currentViewColumn() const noexcept {
	return std::max(1, displayedCursorColumn() - delta.x + 1);
}

int MRFileEditor::visibleViewportRows() const noexcept {
	return std::max(1, visibleTextRows());
}

TRect MRFileEditor::visibleTextViewportBounds() const noexcept {
	const TextViewportGeometry viewport = textViewportGeometry();
	return TRect(viewport.textLeft, viewport.topInset, viewport.textRight, viewport.topInset + std::max(1, visibleTextRows()));
}

bool MRFileEditor::isWordByte(char ch) noexcept {
	unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalnum(uch) != 0 || ch == '_';
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
	const auto startedAt = std::chrono::steady_clock::now();
	const std::size_t clampedPos = std::min(pos, mBufferModel.length());
	const auto lineIndexStartedAt = std::chrono::steady_clock::now();
	const std::size_t currentDocumentLine = mBufferModel.lineIndex(clampedPos);
	const auto lineIndexElapsed = std::chrono::steady_clock::now() - lineIndexStartedAt;
	const auto visibleLineStartedAt = std::chrono::steady_clock::now();
	const std::size_t currentVisibleLine = visibleLineForDocumentLine(currentDocumentLine);
	const auto visibleLineElapsed = std::chrono::steady_clock::now() - visibleLineStartedAt;
	std::size_t targetVisibleLine = currentVisibleLine;
	std::size_t targetDocumentLine = currentDocumentLine;
	std::chrono::steady_clock::duration charColumnElapsed{};
	std::chrono::steady_clock::duration documentLineElapsed{};
	std::chrono::steady_clock::duration charPtrElapsed{};

	if (targetVisualColumn < 0) {
		const auto charColumnStartedAt = std::chrono::steady_clock::now();
		targetVisualColumn = charColumn(mBufferModel.lineStart(pos), clampedPos);
		charColumnElapsed = std::chrono::steady_clock::now() - charColumnStartedAt;
	}
	if (deltaLines < 0) targetVisibleLine = currentVisibleLine > static_cast<std::size_t>(-deltaLines) ? currentVisibleLine - static_cast<std::size_t>(-deltaLines) : 0;
	else
		targetVisibleLine = currentVisibleLine + static_cast<std::size_t>(deltaLines);
	{
		const auto documentLineStartedAt = std::chrono::steady_clock::now();
		targetDocumentLine = documentLineForVisibleLine(targetVisibleLine);
		documentLineElapsed = std::chrono::steady_clock::now() - documentLineStartedAt;
	}
	std::size_t targetOffset = 0;
	{
		const auto charPtrStartedAt = std::chrono::steady_clock::now();
		targetOffset = charPtrOffset(mBufferModel.lineStartByIndex(targetDocumentLine), targetVisualColumn);
		charPtrElapsed = std::chrono::steady_clock::now() - charPtrStartedAt;
	}
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowNavigationTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 nav lineMoveOffset total_us=" << traceMicros(totalElapsed) << " line_index_us=" << traceMicros(lineIndexElapsed) << " visible_map_us=" << traceMicros(visibleLineElapsed)
		     << " char_column_us=" << traceMicros(charColumnElapsed) << " document_map_us=" << traceMicros(documentLineElapsed) << " char_ptr_us=" << traceMicros(charPtrElapsed) << " pos=" << clampedPos
		     << " delta_lines=" << deltaLines << " target_visual=" << targetVisualColumn << " current_doc_line=" << currentDocumentLine << " current_visible_line=" << currentVisibleLine
		     << " target_doc_line=" << targetDocumentLine << " target_visible_line=" << targetVisibleLine << " len=" << mBufferModel.length() << " add=" << mBufferModel.document().addBufferLength()
		     << " pieces=" << mBufferModel.document().pieceCount() << " undo=" << mBufferModel.undoStackDepth() << " redo=" << mBufferModel.redoStackDepth();
		appendDirectProbeLog(line.str());
	}
	return targetOffset;
}

std::size_t MRFileEditor::tabStopMoveOffset(std::size_t pos, bool forward) noexcept {
	const std::size_t cursor = std::min(pos, mBufferModel.length());
	const std::size_t lineStart = lineStartOffset(cursor);
	const MREditSetupSettings settings = effectiveEditSetupSettings();
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
	const auto startedAt = std::chrono::steady_clock::now();
	std::size_t lineStart = mBufferModel.lineStart(start);
	std::string lineText = mBufferModel.lineText(lineStart);
	TStringView line(lineText.data(), lineText.size());
	const MREditSetupSettings settings = effectiveEditSetupSettings();
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
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowNavigationTraceThreshold) {
		std::ostringstream trace;
		trace << "Phase1 nav charPtrOffset total_us=" << traceMicros(totalElapsed) << " start=" << start << " line_start=" << lineStart << " target_visual=" << pos << " line_bytes=" << line.size()
		      << " len=" << mBufferModel.length() << " add=" << mBufferModel.document().addBufferLength() << " pieces=" << mBufferModel.document().pieceCount();
		appendDirectProbeLog(trace.str());
	}
	return lineStart + p;
}

void MRFileEditor::ensureCursorVisible(bool centerCursor) {
	int visualColumn = displayedCursorColumn();
	int line = static_cast<int>(visibleLineForDocumentLine(cachedCursorLineIndex()));
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
	const auto startedAt = std::chrono::steady_clock::now();
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
	std::chrono::steady_clock::duration visualColumnElapsed{};
	std::chrono::steady_clock::duration updateMetricsElapsed{};
	std::chrono::steady_clock::duration ensureVisibleElapsed{};
	std::chrono::steady_clock::duration updateIndicatorElapsed{};
	std::chrono::steady_clock::duration drawViewElapsed{};
	{
		const auto visualColumnStartedAt = std::chrono::steady_clock::now();
		if (freeCursorMovementEnabled() && requestedVisualColumn >= 0) mCursorVisualColumn = std::max(actualCursorVisualColumn(target), requestedVisualColumn);
		else
			mCursorVisualColumn = actualCursorVisualColumn(target);
		visualColumnElapsed = std::chrono::steady_clock::now() - visualColumnStartedAt;
	}
	if (useApproximateLargeFileMetrics()) {
		const auto updateMetricsStartedAt = std::chrono::steady_clock::now();
		updateMetrics();
		updateMetricsElapsed = std::chrono::steady_clock::now() - updateMetricsStartedAt;
	}
	{
		const auto ensureVisibleStartedAt = std::chrono::steady_clock::now();
		ensureCursorVisible(centerCursor);
		ensureVisibleElapsed = std::chrono::steady_clock::now() - ensureVisibleStartedAt;
	}
	scheduleSyntaxWarmupIfNeeded();
	{
		const auto updateIndicatorStartedAt = std::chrono::steady_clock::now();
		updateIndicator();
		updateIndicatorElapsed = std::chrono::steady_clock::now() - updateIndicatorStartedAt;
	}
	{
		const auto drawViewStartedAt = std::chrono::steady_clock::now();
		drawView();
		drawViewElapsed = std::chrono::steady_clock::now() - drawViewStartedAt;
	}
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowNavigationTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 nav moveCursor total_us=" << traceMicros(totalElapsed) << " visual_column_us=" << traceMicros(visualColumnElapsed) << " update_metrics_us=" << traceMicros(updateMetricsElapsed)
		     << " ensure_visible_us=" << traceMicros(ensureVisibleElapsed) << " update_indicator_us=" << traceMicros(updateIndicatorElapsed) << " draw_view_us=" << traceMicros(drawViewElapsed)
		     << " target=" << target << " extend=" << (extendSelection ? 1 : 0) << " center=" << (centerCursor ? 1 : 0) << " requested_visual=" << requestedVisualColumn << " cursor_line=" << cachedCursorLineIndex()
		     << " delta_x=" << delta.x << " delta_y=" << delta.y << " len=" << mBufferModel.length() << " add=" << mBufferModel.document().addBufferLength() << " pieces=" << mBufferModel.document().pieceCount()
		     << " undo=" << mBufferModel.undoStackDepth() << " redo=" << mBufferModel.redoStackDepth();
		appendDirectProbeLog(line.str());
	}
}

std::size_t MRFileEditor::mouseOffset(TPoint local, int *visualColumnOut) noexcept {
	TextViewportGeometry viewport = textViewportGeometry();
	const int textRows = std::max(1, visibleTextRows());
	int clampedY = std::max(0, std::min(local.y - viewport.topInset, textRows - 1));
	int row = clampedY + delta.y;
	int column = viewport.textColumnFromLocalX(local.x);
	std::size_t start = mBufferModel.lineStartByIndex(documentLineForVisibleLine(static_cast<std::size_t>(std::max(row, 0))));
	if (visualColumnOut != nullptr) *visualColumnOut = column;
	return canonicalCursorOffset(charPtrOffset(start, column));
}

std::size_t MRFileEditor::canonicalCursorOffset(std::size_t pos) const noexcept {
	pos = std::min(pos, mBufferModel.length());
	if (pos > 0 && pos < mBufferModel.length() && mBufferModel.charAt(pos) == '\n' && mBufferModel.charAt(pos - 1) == '\r') return pos - 1;
	return pos;
}
