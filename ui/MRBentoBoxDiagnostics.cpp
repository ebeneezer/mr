#define Uses_TDeskTop
#include "MRBentoBox.hpp"

#include "MRSidekickEditor.hpp"

#include "../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

namespace {

static const char *kBentoProblemsPaneTitle = "Problems";

std::string pathBaseName(const std::string &path) {
	std::filesystem::path fsPath(path);
	std::string base = fsPath.filename().string();

	return base.empty() ? path : base;
}

std::string normalizedDiagnosticPath(const std::string &path) {
	std::string normalized = path;

	for (char &ch : normalized)
		if (ch == '\\') ch = '/';
	normalized = std::filesystem::path(normalized).lexically_normal().generic_string();
	if (normalized.rfind("./", 0) == 0) normalized.erase(0, 2);
	return normalized;
}

bool pathSuffixMatches(const std::string &candidatePath, const std::string &sourcePath) {
	if (candidatePath.size() > sourcePath.size()) return false;
	if (candidatePath == sourcePath) return true;
	if (sourcePath.size() <= candidatePath.size()) return false;
	if (sourcePath.compare(sourcePath.size() - candidatePath.size(), candidatePath.size(), candidatePath) != 0) return false;
	return sourcePath[sourcePath.size() - candidatePath.size() - 1] == '/';
}

bool compilerDiagnosticPathMatches(const std::string &candidatePath, const std::string &sourcePath) {
	const std::string candidate = normalizedDiagnosticPath(candidatePath);
	const std::string source = normalizedDiagnosticPath(sourcePath);

	if (candidatePath.empty() || sourcePath.empty()) return false;
	if (candidate == source || pathSuffixMatches(candidate, source)) return true;
	return pathBaseName(candidate) == pathBaseName(source);
}

bool parseUnsignedField(const std::string &text, std::size_t start, std::size_t end, std::size_t &value) {
	std::size_t parsed = 0;

	if (start >= end || std::isdigit(static_cast<unsigned char>(text[start])) == 0) return false;
	value = 0;
	while (start + parsed < end && std::isdigit(static_cast<unsigned char>(text[start + parsed])) != 0) {
		value = value * 10 + static_cast<std::size_t>(text[start + parsed] - '0');
		++parsed;
	}
	return parsed != 0 && start + parsed == end;
}

struct DiagnosticSeverityMarker {
	const char *marker;
	const char *severity;
};

bool parseCompilerDiagnosticLine(const std::string &line, const std::string &sourcePath, std::size_t outputOffset, MRCompilerDiagnostic &diagnostic) {
	static const DiagnosticSeverityMarker markers[] = {{": fatal error:", "fatal error"}, {": error:", "error"}, {": warning:", "warning"}, {": note:", "note"}};
	std::size_t markerPos = std::string::npos;
	const DiagnosticSeverityMarker *matchedMarker = nullptr;

	for (const DiagnosticSeverityMarker &marker : markers) {
		const std::size_t pos = line.find(marker.marker);
		if (pos != std::string::npos && (matchedMarker == nullptr || pos < markerPos)) {
			markerPos = pos;
			matchedMarker = &marker;
		}
	}
	if (matchedMarker == nullptr) return false;

	const std::string location = line.substr(0, markerPos);
	const std::size_t lastColon = location.rfind(':');
	std::size_t lineColon = std::string::npos;
	std::size_t sourceLine = 0;
	std::size_t sourceColumn = 1;

	if (lastColon == std::string::npos) return false;
	if (parseUnsignedField(location, lastColon + 1, location.size(), sourceColumn)) {
		lineColon = location.rfind(':', lastColon - 1);
		if (lineColon == std::string::npos) return false;
		if (!parseUnsignedField(location, lineColon + 1, lastColon, sourceLine)) return false;
	} else {
		lineColon = lastColon;
		if (!parseUnsignedField(location, lineColon + 1, location.size(), sourceLine)) return false;
		sourceColumn = 1;
	}
	if (sourceLine == 0 || sourceColumn == 0) return false;
	const std::string diagnosticPath = location.substr(0, lineColon);

	diagnostic.sourcePath = diagnosticPath;
	diagnostic.sourceLine = sourceLine;
	diagnostic.sourceColumn = sourceColumn;
	diagnostic.severity = matchedMarker->severity;
	diagnostic.text = line.substr(markerPos + std::strlen(matchedMarker->marker));
	while (!diagnostic.text.empty() && diagnostic.text.front() == ' ')
		diagnostic.text.erase(diagnostic.text.begin());
	diagnostic.sourceOffset = 0;
	diagnostic.outputOffset = outputOffset;
	diagnostic.problemOffset = 0;
	diagnostic.sourceAvailable = compilerDiagnosticPathMatches(diagnosticPath, sourcePath);
	return true;
}

bool parseCompilerDriverDiagnosticLine(const std::string &line, std::size_t outputOffset, MRCompilerDiagnostic &diagnostic) {
	static const DiagnosticSeverityMarker markers[] = {{"warning:", "warning"}, {"note:", "note"}};
	std::size_t markerPos = std::string::npos;
	const DiagnosticSeverityMarker *matchedMarker = nullptr;

	for (const DiagnosticSeverityMarker &marker : markers) {
		const std::size_t pos = line.find(marker.marker);
		if (pos != std::string::npos && (matchedMarker == nullptr || pos < markerPos)) {
			markerPos = pos;
			matchedMarker = &marker;
		}
	}
	if (matchedMarker == nullptr || markerPos == 0) return false;

	diagnostic.sourcePath = line.substr(0, markerPos);
	while (!diagnostic.sourcePath.empty() && diagnostic.sourcePath.back() == ' ')
		diagnostic.sourcePath.pop_back();
	if (!diagnostic.sourcePath.empty() && diagnostic.sourcePath.back() == ':') diagnostic.sourcePath.pop_back();
	while (!diagnostic.sourcePath.empty() && diagnostic.sourcePath.back() == ' ')
		diagnostic.sourcePath.pop_back();
	diagnostic.sourceLine = 0;
	diagnostic.sourceColumn = 0;
	diagnostic.severity = matchedMarker->severity;
	diagnostic.text = line.substr(markerPos + std::strlen(matchedMarker->marker));
	while (!diagnostic.text.empty() && diagnostic.text.front() == ' ')
		diagnostic.text.erase(diagnostic.text.begin());
	diagnostic.sourceOffset = 0;
	diagnostic.outputOffset = outputOffset;
	diagnostic.problemOffset = 0;
	diagnostic.sourceAvailable = false;
	return true;
}

bool compilerDiagnosticSeverityEnabled(const MRCompilerDiagnostic &diagnostic) {
	if (diagnostic.severity == "error" || diagnostic.severity == "fatal error") return true;
	if (diagnostic.severity == "warning") return configuredTrackCompilerWarnings();
	if (diagnostic.severity == "note") return configuredTrackCompilerNotes();
	return false;
}

std::size_t sourceOffsetForCompilerColumn(MRFileEditor *editor, std::size_t lineStart, std::size_t lineEnd, std::size_t column) {
	std::size_t offset = lineStart;

	for (std::size_t currentColumn = 1; currentColumn < column && offset < lineEnd; ++currentColumn)
		offset = editor->nextCharOffset(offset);
	return offset;
}

std::size_t lineColumnForOffset(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t lineStart, std::size_t offset) {
	return offset >= lineStart ? offset - lineStart + 1 : 1;
}

bool editTouchesDiagnosticLine(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t diagnosticOffset, std::size_t editStart, std::size_t editEnd) {
	const std::size_t lineStart = snapshot.lineStart(diagnosticOffset);
	const std::size_t lineEnd = snapshot.lineEnd(diagnosticOffset);

	if (editEnd > editStart) return editStart < lineEnd && editEnd > lineStart;
	return editStart >= lineStart && editStart <= lineEnd;
}

std::vector<MRCompilerDiagnostic> parseCompilerDiagnostics(const std::string &text, const std::string &sourcePath) {
	std::vector<MRCompilerDiagnostic> diagnostics;
	const std::size_t textLength = text.size();
	std::size_t lineStart = 0;

	while (lineStart < textLength) {
		std::size_t lineEnd = text.find('\n', lineStart);
		MRCompilerDiagnostic diagnostic;

		if (lineEnd == std::string::npos) lineEnd = textLength;
		const std::string line = text.substr(lineStart, lineEnd - lineStart);
		if (parseCompilerDiagnosticLine(line, sourcePath, lineStart, diagnostic) || parseCompilerDriverDiagnosticLine(line, lineStart, diagnostic)) diagnostics.push_back(std::move(diagnostic));
		if (lineEnd == textLength) break;
		lineStart = lineEnd + 1;
	}
	return diagnostics;
}

std::string compilerDiagnosticProblemLine(const MRCompilerDiagnostic &diagnostic) {
	std::ostringstream line;

	line << diagnostic.severity << " ";
	if (diagnostic.sourceLine == 0)
		line << (diagnostic.sourcePath.empty() ? "build" : diagnostic.sourcePath);
	else
		line << pathBaseName(diagnostic.sourcePath) << ":" << diagnostic.sourceLine << ":" << diagnostic.sourceColumn;
	if (!diagnostic.text.empty()) line << " " << diagnostic.text;
	return line.str();
}

std::string compilerDiagnosticDetailText(const MRCompilerDiagnostic &diagnostic) {
	std::ostringstream text;

	if (diagnostic.sourceLine == 0)
		text << diagnostic.severity << " from " << (diagnostic.sourcePath.empty() ? "build" : diagnostic.sourcePath) << "\n";
	else
		text << diagnostic.severity << " at " << diagnostic.sourcePath << ":" << diagnostic.sourceLine << ":" << diagnostic.sourceColumn << "\n";
	text << diagnostic.text;
	return text.str();
}


}

MRCompilerDiagnostic::MRCompilerDiagnostic() noexcept : sourcePath(), sourceLine(1), sourceColumn(1), severity(), text(), sourceOffset(0), outputOffset(0), problemOffset(0), sourceAvailable(false) {
}
void MRBentoBox::setCompilerOutputStatus(const char *status) {
	const std::string nextStatus = status != nullptr ? status : "";

	if (compilerOutputStatus == nextStatus) return;
	compilerOutputStatus = nextStatus;
	updateActivePaneFrame();
	bentoProjectionDirty |= bpdChrome;
	flushBentoProjection();
}

void MRBentoBox::clearCompilerDiagnostics() {
	MREditWindow *problemsWindow = problemsPane();
	MRFileEditor *problemsEditor = problemsWindow != nullptr ? problemsWindow->getEditor() : nullptr;

	compilerDiagnostics.clear();
	compilerProblemsStatus.clear();
	clearTrackedCompilerSidekick(true);
	if (problemsEditor != nullptr) problemsEditor->clearFindMarkerRanges();
	if (getEditor() != nullptr) getEditor()->clearCompilerDiagnosticRanges();
	if (problemsWindow != nullptr) {
		static_cast<void>(problemsWindow->replaceTextBuffer("", kBentoProblemsPaneTitle));
		problemsWindow->setReadOnly(true);
		problemsWindow->setFileChanged(false);
	}
	if (hasPaneSplit()) {
		bentoProjectionDirty |= bpdLayout;
		flushBentoProjection();
	}
}

bool MRBentoBox::hasCompilerProblems() const noexcept {
	return !compilerDiagnostics.empty();
}

void MRBentoBox::refreshCompilerProblemsPane() {
	MREditWindow *problemsWindow = problemsPane();
	MRFileEditor *problemsEditor = problemsWindow != nullptr ? problemsWindow->getEditor() : nullptr;
	std::ostringstream problemText;
	std::ostringstream statusText;
	std::size_t problemOffset = 0;
	int errorCount = 0;
	int warningCount = 0;
	int noteCount = 0;

	for (MRCompilerDiagnostic &diagnostic : compilerDiagnostics) {
		const std::string line = compilerDiagnosticProblemLine(diagnostic);
		if (diagnostic.severity == "error" || diagnostic.severity == "fatal error")
			++errorCount;
		else if (diagnostic.severity == "warning")
			++warningCount;
		else if (diagnostic.severity == "note")
			++noteCount;
		diagnostic.problemOffset = problemOffset;
		problemText << line << '\n';
		problemOffset += line.size() + 1;
	}
	if (errorCount > 0) statusText << errorCount << " error" << (errorCount == 1 ? "" : "s");
	if (warningCount > 0) {
		if (statusText.tellp() > 0) statusText << ", ";
		statusText << warningCount << " warning" << (warningCount == 1 ? "" : "s");
	}
	if (noteCount > 0) {
		if (statusText.tellp() > 0) statusText << ", ";
		statusText << noteCount << " note" << (noteCount == 1 ? "" : "s");
	}
	compilerProblemsStatus = statusText.str();
	if (problemsWindow != nullptr) {
		static_cast<void>(problemsWindow->replaceTextBuffer(problemText.str().c_str(), kBentoProblemsPaneTitle));
		problemsWindow->setReadOnly(true);
		problemsWindow->setFileChanged(false);
	}
	if (problemsEditor != nullptr) problemsEditor->clearFindMarkerRanges();
	refreshSourceCompilerDiagnosticRanges();
	updateActivePaneFrame();
	if (hasPaneSplit()) {
		bentoProjectionDirty |= bpdLayout;
		flushBentoProjection();
	}
}

void MRBentoBox::refreshSourceCompilerDiagnosticRanges() {
	MRFileEditor *sourceEditor = getEditor();
	std::vector<std::pair<std::size_t, std::size_t>> errorRanges;
	std::vector<std::pair<std::size_t, std::size_t>> warningRanges;

	if (sourceEditor == nullptr) return;
	for (const MRCompilerDiagnostic &diagnostic : compilerDiagnostics) {
		if (!diagnostic.sourceAvailable) continue;
		const std::size_t start = diagnostic.sourceOffset;
		const std::size_t end = sourceEditor->nextCharOffset(start);
		if (diagnostic.severity == "warning" || diagnostic.severity == "note") warningRanges.push_back(std::make_pair(start, end));
		else if (diagnostic.severity == "error" || diagnostic.severity == "fatal error")
			errorRanges.push_back(std::make_pair(start, end));
	}
	sourceEditor->setCompilerDiagnosticRanges(errorRanges, warningRanges);
}

void MRBentoBox::clearTrackedCompilerSidekick(bool dropSidekick) noexcept {
	compilerSidekickTracked = false;
	compilerSidekickDiagnosticIndex = 0;
	if (dropSidekick) mrDropSidekickForParent(this);
}

void MRBentoBox::trackCompilerSidekick(std::size_t diagnosticIndex) noexcept {
	static_cast<void>(mrConsumeReadOnlySidekickDismissedForParent(this));
	compilerSidekickTracked = true;
	compilerSidekickDiagnosticIndex = diagnosticIndex;
}

void MRBentoBox::updateTrackedCompilerSidekick() {
	MRFileEditor *sourceEditor = getEditor();

	if (!compilerSidekickTracked) return;
	if (compilerSidekickUpdating) return;
	if (TProgram::deskTop == nullptr || TProgram::deskTop->current != this) {
		if (mrHasReadOnlySidekickForParent(this)) mrDropSidekickForParent(this);
		return;
	}
	if (mrConsumeReadOnlySidekickDismissedForParent(this)) {
		clearTrackedCompilerSidekick(false);
		return;
	}
	if (sourceEditor == nullptr || compilerSidekickDiagnosticIndex >= compilerDiagnostics.size()) {
		clearTrackedCompilerSidekick(true);
		return;
	}

	const MRCompilerDiagnostic &diagnostic = compilerDiagnostics[compilerSidekickDiagnosticIndex];
	if (!diagnostic.sourceAvailable || !compilerDiagnosticPathMatches(diagnostic.sourcePath, currentFileName())) {
		clearTrackedCompilerSidekick(true);
		return;
	}

	MRTextBufferModel::ReadSnapshot sourceSnapshot = buffer().readSnapshot();
	const std::size_t sourceOffset = sourceSnapshot.clampOffset(diagnostic.sourceOffset);
	const std::size_t lineIndex = sourceSnapshot.lineIndex(sourceOffset);
	const std::size_t lineStart = sourceSnapshot.lineStartByIndex(lineIndex);
	const std::size_t sourceLineEnd = sourceEditor->lineEndOffset(sourceOffset);
	const int diagnosticViewColumn = sourceEditor->charColumn(lineStart, sourceOffset) - sourceEditor->delta.x + 1;
	const int diagnosticViewRow = static_cast<int>(lineIndex) - sourceEditor->delta.y + 1;
	const TRect textViewport = sourceEditor->visibleTextViewportBounds();
	const int viewportWidth = std::max(1, textViewport.b.x - textViewport.a.x);
	const int viewportRows = sourceEditor->visibleViewportRows();

	if (diagnosticViewRow < 1 || diagnosticViewRow > viewportRows || diagnosticViewColumn < 1 || diagnosticViewColumn > viewportWidth) {
		if (mrHasReadOnlySidekickForParent(this)) mrDropSidekickForParent(this);
		return;
	}

	const int lineEndViewColumn = sourceEditor->charColumn(lineStart, sourceLineEnd) - sourceEditor->delta.x + 1;
	const int sidekickViewColumn = std::max(diagnosticViewColumn, lineEndViewColumn + 2);
	const bool rightMarginSidekick = configuredCompilerErrorMessagePlacement() == MRCompilerErrorMessagePlacement::RightMargin;
	compilerSidekickUpdating = true;
	static_cast<void>(mrOpenReadOnlySidekickAt(this, compilerDiagnosticDetailText(diagnostic), "Compiler diagnostic", diagnosticViewColumn, diagnosticViewRow, rightMarginSidekick ? sidekickViewColumn : diagnosticViewColumn,
	                                          rightMarginSidekick ? MRReadOnlySidekickPlacement::RightMargin : MRReadOnlySidekickPlacement::UnderCode));
	compilerSidekickUpdating = false;
}

bool MRBentoBox::refreshCompilerDiagnosticsFromOutput() {
	MREditWindow *outputWindow = buildOutputPane();
	MREditWindow *problemsWindow = problemsPane();
	std::vector<MRCompilerDiagnostic> diagnostics;
	std::vector<MRCompilerDiagnostic> filteredDiagnostics;
	MRFileEditor *sourceEditor = getEditor();
	MRTextBufferModel::ReadSnapshot sourceSnapshot;

	if (outputWindow == nullptr || problemsWindow == nullptr || sourceEditor == nullptr) return false;
	sourceSnapshot = buffer().readSnapshot();
	diagnostics = parseCompilerDiagnostics(outputWindow->buffer().readSnapshot().text(), currentFileName());
	for (MRCompilerDiagnostic &diagnostic : diagnostics) {
		if (!compilerDiagnosticSeverityEnabled(diagnostic)) continue;
		if (!diagnostic.sourceAvailable) continue;
		const std::size_t lineStart = sourceSnapshot.lineStartByIndex(diagnostic.sourceLine > 0 ? diagnostic.sourceLine - 1 : 0);
		const std::size_t lineEnd = sourceSnapshot.lineEnd(lineStart);
		diagnostic.sourceOffset = sourceOffsetForCompilerColumn(sourceEditor, lineStart, lineEnd, diagnostic.sourceColumn);
		filteredDiagnostics.push_back(std::move(diagnostic));
	}
	compilerDiagnostics = std::move(filteredDiagnostics);
	clearTrackedCompilerSidekick(true);
	refreshCompilerProblemsPane();
	return true;
}

void MRBentoBox::syncCompilerDiagnosticsAfterSourceMutation(const MRTextBufferModel::ReadSnapshot &oldSnapshot, const MRTextBufferModel::DocumentChangeSet &changeSet) {
	MRTextBufferModel::ReadSnapshot newSnapshot;
	std::size_t newLength = 0;
	const std::size_t editStart = changeSet.touchedRange.start;
	std::size_t oldEditEnd = std::min(changeSet.touchedRange.end, changeSet.oldLength);

	if (compilerDiagnostics.empty() || !changeSet.changed || changeSet.oldVersion != oldSnapshot.version()) return;
	clearTrackedCompilerSidekick(true);
	newSnapshot = buffer().readSnapshot();
	if (changeSet.newVersion != newSnapshot.version()) return;
	newLength = newSnapshot.length();
	const long long delta = static_cast<long long>(changeSet.newLength) - static_cast<long long>(changeSet.oldLength);
	if (delta > 0 && changeSet.touchedRange.end - editStart == static_cast<std::size_t>(delta)) oldEditEnd = editStart;

	std::vector<MRCompilerDiagnostic> remapped;
	for (MRCompilerDiagnostic diagnostic : compilerDiagnostics) {
		if (!diagnostic.sourceAvailable) {
			remapped.push_back(std::move(diagnostic));
			continue;
		}
		if (editTouchesDiagnosticLine(oldSnapshot, diagnostic.sourceOffset, editStart, oldEditEnd)) continue;
		if (diagnostic.sourceOffset >= oldEditEnd) {
			const long long shifted = static_cast<long long>(diagnostic.sourceOffset) + delta;
			if (shifted < 0 || static_cast<std::size_t>(shifted) > newLength) continue;
			diagnostic.sourceOffset = static_cast<std::size_t>(shifted);
		}
		const std::size_t lineIndex = newSnapshot.lineIndex(diagnostic.sourceOffset);
		const std::size_t lineStart = newSnapshot.lineStartByIndex(lineIndex);
		diagnostic.sourceLine = lineIndex + 1;
		diagnostic.sourceColumn = lineColumnForOffset(newSnapshot, lineStart, diagnostic.sourceOffset);
		remapped.push_back(std::move(diagnostic));
	}
	compilerDiagnostics = std::move(remapped);
	refreshCompilerProblemsPane();
}

bool MRBentoBox::jumpToProblemAtCursor() {
	MREditWindow *problemsWindow = problemsPane();
	MREditWindow *outputWindow = buildOutputPane();
	MRFileEditor *sourceEditor = getEditor();
	MRFileEditor *problemsEditor = problemsWindow != nullptr ? problemsWindow->getEditor() : nullptr;
	MRFileEditor *outputEditor = outputWindow != nullptr ? outputWindow->getEditor() : nullptr;
	MRTextBufferModel::ReadSnapshot sourceSnapshot;
	std::size_t cursorOffset;
	const MRCompilerDiagnostic *selected = nullptr;
	std::size_t selectedIndex = 0;

	if (sourceEditor == nullptr || problemsEditor == nullptr || outputEditor == nullptr) return false;
	cursorOffset = problemsEditor->cursorOffset();
	for (std::size_t i = 0; i < compilerDiagnostics.size(); ++i) {
		const MRCompilerDiagnostic &diagnostic = compilerDiagnostics[i];
		const std::size_t lineEnd = problemsEditor->lineEndOffset(diagnostic.problemOffset);
		if (cursorOffset >= diagnostic.problemOffset && cursorOffset <= lineEnd) {
			selected = &diagnostic;
			selectedIndex = i;
			break;
		}
	}
	if (selected == nullptr) return false;
	if (selected->sourceAvailable && !compilerDiagnosticPathMatches(selected->sourcePath, currentFileName())) return false;

	const std::size_t outputLineEnd = outputEditor->lineEndOffset(selected->outputOffset);
	outputEditor->setCursorOffset(selected->outputOffset);
	outputEditor->setSelectionOffsets(selected->outputOffset, outputLineEnd);

	problemsEditor->setCursorOffset(selected->problemOffset);
	problemsEditor->setSelectionOffsets(selected->problemOffset, problemsEditor->lineEndOffset(selected->problemOffset));
	problemsEditor->setFindMarkerRanges({std::make_pair(selected->problemOffset, problemsEditor->lineEndOffset(selected->problemOffset))});
	if (!selected->sourceAvailable) {
		const int outputLeaf = leafIdForRole(bprCompilerOutput);
		clearTrackedCompilerSidekick(true);
		if (outputLeaf >= 0) setActivePane(outputLeaf);
		return true;
	}

	sourceSnapshot = buffer().readSnapshot();
	const std::size_t sourceOffset = sourceSnapshot.clampOffset(selected->sourceOffset);
	const std::size_t lineStart = sourceSnapshot.lineStart(sourceOffset);
	const std::size_t sourceLineEnd = sourceEditor->lineEndOffset(sourceOffset);
	std::size_t sourceSelectionEnd = sourceOffset < sourceLineEnd ? sourceEditor->nextCharOffset(sourceOffset) : sourceOffset;
	if (sourceSelectionEnd == sourceOffset && lineStart < sourceLineEnd) sourceSelectionEnd = sourceLineEnd;
	sourceEditor->setCursorOffset(sourceOffset);
	sourceEditor->setSelectionOffsets(sourceOffset, sourceSelectionEnd);
	sourceEditor->revealCursor(True);

	activatePrimaryPane();
	trackCompilerSidekick(selectedIndex);
	updateTrackedCompilerSidekick();
	return true;
}

bool MRBentoBox::jumpToNextProblem() {
	MREditWindow *problemsWindow = problemsPane();
	MRFileEditor *problemsEditor = problemsWindow != nullptr ? problemsWindow->getEditor() : nullptr;
	std::size_t cursorOffset;
	const MRCompilerDiagnostic *next = nullptr;

	if (problemsEditor == nullptr || compilerDiagnostics.empty()) return false;
	cursorOffset = problemsEditor->cursorOffset();
	for (const MRCompilerDiagnostic &diagnostic : compilerDiagnostics)
		if (diagnostic.problemOffset > cursorOffset) {
			next = &diagnostic;
			break;
		}
	if (next == nullptr) next = &compilerDiagnostics.front();
	problemsEditor->setCursorOffset(next->problemOffset);
	problemsEditor->setSelectionOffsets(next->problemOffset, problemsEditor->lineEndOffset(next->problemOffset));
	return jumpToProblemAtCursor();
}

bool MRBentoBox::jumpToPreviousProblem() {
	MREditWindow *problemsWindow = problemsPane();
	MRFileEditor *problemsEditor = problemsWindow != nullptr ? problemsWindow->getEditor() : nullptr;
	std::size_t cursorOffset;
	const MRCompilerDiagnostic *previous = nullptr;

	if (problemsEditor == nullptr || compilerDiagnostics.empty()) return false;
	cursorOffset = problemsEditor->cursorOffset();
	for (const MRCompilerDiagnostic &diagnostic : compilerDiagnostics) {
		if (diagnostic.problemOffset >= cursorOffset) break;
		previous = &diagnostic;
	}
	if (previous == nullptr) previous = &compilerDiagnostics.back();
	problemsEditor->setCursorOffset(previous->problemOffset);
	problemsEditor->setSelectionOffsets(previous->problemOffset, problemsEditor->lineEndOffset(previous->problemOffset));
	return jumpToProblemAtCursor();
}
