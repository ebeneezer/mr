#define Uses_TDeskTop
#include "MRBentoBox.hpp"

#include "../MRSidekickEditor.hpp"
#include "../MRMessageLineController.hpp"

#include "../../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

namespace {

static const char *kBentoProblemsPaneTitle = "Problems";
static constexpr std::uint64_t kEmptyProjectionTextHash = 1469598103934665603ULL;

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

std::string compilerDiagnosticDetailText(const MRCompilerDiagnostic &diagnostic) {
	std::ostringstream text;

	if (diagnostic.sourceLine == 0)
		text << diagnostic.severity << " from " << (diagnostic.sourcePath.empty() ? "build" : diagnostic.sourcePath) << "\n";
	else
		text << diagnostic.severity << " at " << diagnostic.sourcePath << ":" << diagnostic.sourceLine << ":" << diagnostic.sourceColumn << "\n";
	text << diagnostic.text;
	return text.str();
}

void postCompilerProblemNavigationUnavailable() {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "No compiler diagnostic location found.",
	                               mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
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
	const std::shared_ptr<const std::string> emptyText = std::make_shared<const std::string>();

	cancelBentoProjectionTask(diagnosticsProjectionTask);
	pendingCompilerProblemNavigation = 0;
	compilerDiagnostics = std::make_shared<const std::vector<MRCompilerDiagnostic>>();
	compilerDiagnosticSourceChanges.reset();
	compilerDiagnosticsParseSourceSnapshot = std::make_shared<const MRTextBufferModel::ReadSnapshot>(buffer().readSnapshot());
	compilerDiagnosticsDocumentId = documentId();
	compilerDiagnosticsVersion = documentVersion();
	MREditWindow *outputWindow = buildOutputPane();
	compilerDiagnosticsOutputDocumentId = outputWindow != nullptr ? outputWindow->documentId() : 0;
	compilerDiagnosticsOutputVersion = outputWindow != nullptr ? outputWindow->documentVersion() : 0;
	compilerDiagnosticsOutputBufferId = outputWindow != nullptr ? outputWindow->bufferId() : 0;
	compilerProblemsStatus.clear();
	compilerProblemsTargetDocumentId = 0;
	compilerProblemsTargetVersion = 0;
	compilerProblemsTargetBufferId = 0;
	compilerProblemsTextLength = 0;
	compilerProblemsTextHash = 0;
	compilerDiagnosticsParseRequired = true;
	compilerDiagnosticsSourceInvalidated = false;
	diagnosticsProjectionTask.sourcePath = currentFileName();
	clearTrackedCompilerSidekick(true);
	if (problemsEditor != nullptr) problemsEditor->clearFindMarkerRanges();
	if (getEditor() != nullptr) getEditor()->clearCompilerDiagnosticRanges();
	if (problemsWindow != nullptr) {
		const std::size_t expectedDocumentId = problemsWindow->documentId();
		const std::size_t expectedVersion = problemsWindow->documentVersion();

		const bool textCleared = adoptBentoProjectionText(problemsWindow, emptyText, expectedDocumentId, expectedVersion, kBentoProblemsPaneTitle);
		if (textCleared) {
			compilerProblemsTargetDocumentId = problemsWindow->documentId();
			compilerProblemsTargetVersion = problemsWindow->documentVersion();
			compilerProblemsTargetBufferId = problemsWindow->bufferId();
			compilerProblemsTextHash = kEmptyProjectionTextHash;
		}
		problemsWindow->setFileChanged(false);
		diagnosticsProjectionTask.sourceDocumentId = documentId();
		diagnosticsProjectionTask.sourceVersion = documentVersion();
		diagnosticsProjectionTask.inputDocumentId = outputWindow != nullptr ? outputWindow->documentId() : 0;
		diagnosticsProjectionTask.inputVersion = outputWindow != nullptr ? outputWindow->documentVersion() : 0;
		diagnosticsProjectionTask.inputBufferId = outputWindow != nullptr ? outputWindow->bufferId() : 0;
		diagnosticsProjectionTask.targetDocumentId = problemsWindow->documentId();
		diagnosticsProjectionTask.targetVersion = problemsWindow->documentVersion();
		diagnosticsProjectionTask.targetBufferId = problemsWindow->bufferId();
		diagnosticsProjectionTask.sourcePath = currentFileName();
		diagnosticsProjectionTask.trackWarnings = configuredTrackCompilerWarnings();
		diagnosticsProjectionTask.trackNotes = configuredTrackCompilerNotes();
		diagnosticsProjectionTask.diagnosticsRequest = bdprFormatExisting;
		diagnosticsProjectionTask.projectionCurrent = true;
		diagnosticsProjectionTask.retryBlocked = false;
	}
	if (hasPaneSplit()) {
		bentoProjectionDirty |= bpdLayout;
		flushBentoProjection();
	}
}

bool MRBentoBox::hasCompilerProblems() const noexcept {
	return compilerDiagnostics != nullptr && !compilerDiagnostics->empty();
}

bool MRBentoBox::refreshCompilerProblemsPane() {
	MREditWindow *outputWindow = buildOutputPane();

	if (compilerDiagnosticsSourceInvalidated || outputWindow == nullptr || problemsPane() == nullptr) return false;
	BentoDiagnosticsProjectionRequest request = compilerDiagnosticSourceChanges == nullptr ? bdprFormatExisting : bdprRemapExisting;
	if (compilerDiagnosticsParseRequired || compilerDiagnosticsOutputDocumentId != outputWindow->documentId() ||
	    compilerDiagnosticsOutputVersion != outputWindow->documentVersion() || compilerDiagnosticsOutputBufferId != outputWindow->bufferId())
		request = bdprParseOutput;
	if (request == bdprParseOutput) return refreshCompilerDiagnosticsFromOutput();
	return submitCompilerDiagnosticsProjection(request);
}

bool MRBentoBox::submitCompilerDiagnosticsProjection(BentoDiagnosticsProjectionRequest request) {
	MREditWindow *outputWindow = buildOutputPane();
	MREditWindow *problemsWindow = problemsPane();
	MRFileEditor *sourceEditor = getEditor();
	MRFileEditor *problemsEditor = problemsWindow != nullptr ? problemsWindow->getEditor() : nullptr;

	if (request == bdprNone || outputWindow == nullptr || problemsWindow == nullptr || sourceEditor == nullptr || problemsEditor == nullptr) return false;
	const std::size_t sourceDocumentId = sourceEditor->documentId();
	const std::size_t sourceVersion = sourceEditor->documentVersion();
	const std::size_t outputDocumentId = outputWindow->documentId();
	const std::size_t outputVersion = outputWindow->documentVersion();
	const int outputBufferId = outputWindow->bufferId();
	const std::size_t targetDocumentId = problemsEditor->documentId();
	const std::size_t targetVersion = problemsEditor->documentVersion();
	const std::string sourcePath = currentFileName();
	const bool trackWarnings = configuredTrackCompilerWarnings();
	const bool trackNotes = configuredTrackCompilerNotes();
	const std::size_t activeSourceDocumentId = diagnosticsProjectionTask.diagnosticSourceChanges != nullptr
	                                               ? diagnosticsProjectionTask.diagnosticSourceChanges->newSnapshot.documentId()
	                                               : diagnosticsProjectionTask.sourceDocumentId;
	const std::size_t activeSourceVersion = diagnosticsProjectionTask.diagnosticSourceChanges != nullptr
	                                          ? diagnosticsProjectionTask.diagnosticSourceChanges->newSnapshot.version()
	                                          : diagnosticsProjectionTask.sourceVersion;
	const bool activeInputMatches = activeSourceDocumentId == sourceDocumentId && activeSourceVersion == sourceVersion &&
	                                diagnosticsProjectionTask.inputDocumentId == outputDocumentId &&
	                                diagnosticsProjectionTask.inputVersion == outputVersion && diagnosticsProjectionTask.inputBufferId == outputBufferId &&
	                                diagnosticsProjectionTask.targetDocumentId == targetDocumentId &&
	                                diagnosticsProjectionTask.targetVersion == targetVersion &&
	                                diagnosticsProjectionTask.targetBufferId == problemsWindow->bufferId() &&
	                                diagnosticsProjectionTask.sourcePath == sourcePath && diagnosticsProjectionTask.trackWarnings == trackWarnings &&
	                                diagnosticsProjectionTask.trackNotes == trackNotes;

	if (diagnosticsProjectionTask.taskId != 0) {
		if (activeInputMatches && request == bdprFormatExisting && diagnosticsProjectionTask.diagnosticsRequest == bdprParseOutput) return true;
		if (activeInputMatches && request == diagnosticsProjectionTask.diagnosticsRequest) return true;
		diagnosticsProjectionTask.pending = true;
		if (request > diagnosticsProjectionTask.pendingDiagnosticsRequest)
			diagnosticsProjectionTask.pendingDiagnosticsRequest = request;
		return true;
	}
	if (diagnosticsProjectionTask.retryBlocked && activeInputMatches) return false;
	if (diagnosticsProjectionTask.projectionCurrent && activeInputMatches && diagnosticsProjectionTask.diagnosticsRequest == request) return true;
	const std::shared_ptr<const MRBentoDiagnosticSourceChange> requestedSourceChanges =
	    request == bdprRemapExisting || request == bdprParseOutput ? compilerDiagnosticSourceChanges : std::shared_ptr<const MRBentoDiagnosticSourceChange>();
	const MRTextBufferModel::ReadSnapshot sourceSnapshot = buffer().readSnapshot();
	const MRTextBufferModel::ReadSnapshot diagnosticSourceSnapshot =
	    request == bdprParseOutput && compilerDiagnosticsParseSourceSnapshot != nullptr ? *compilerDiagnosticsParseSourceSnapshot : sourceSnapshot;
	const MRTextBufferModel::ReadSnapshot outputSnapshot = outputWindow->buffer().readSnapshot();
	if (outputSnapshot.documentId() != outputDocumentId || outputSnapshot.version() != outputVersion) return false;
	if (sourceSnapshot.documentId() != sourceDocumentId || sourceSnapshot.version() != sourceVersion) return false;
	if (requestedSourceChanges != nullptr && (requestedSourceChanges->newSnapshot.documentId() != sourceDocumentId ||
	                                         requestedSourceChanges->newSnapshot.version() != sourceVersion))
		return false;
	if (request == bdprParseOutput && requestedSourceChanges != nullptr) {
		const MRBentoDiagnosticSourceChange *oldestSourceChange = requestedSourceChanges.get();
		while (oldestSourceChange->previous != nullptr)
			oldestSourceChange = oldestSourceChange->previous.get();
		if (oldestSourceChange->oldSnapshot.documentId() != diagnosticSourceSnapshot.documentId() ||
		    oldestSourceChange->oldSnapshot.version() != diagnosticSourceSnapshot.version())
			return false;
	}
	if (request == bdprParseOutput && requestedSourceChanges == nullptr &&
	    (diagnosticSourceSnapshot.documentId() != sourceSnapshot.documentId() || diagnosticSourceSnapshot.version() != sourceSnapshot.version()))
		return false;

	if (diagnosticsProjectionTask.generationCounter == 0) diagnosticsProjectionTask.generationCounter = 1;
	const std::uint64_t generation = diagnosticsProjectionTask.generationCounter++;
	const bool parseOutput = request == bdprParseOutput;
	const std::shared_ptr<const std::vector<MRCompilerDiagnostic>> diagnostics = parseOutput ? std::shared_ptr<const std::vector<MRCompilerDiagnostic>>() : compilerDiagnostics;
	const std::shared_ptr<const MRBentoDiagnosticSourceChange> sourceChanges = requestedSourceChanges;
	const std::uint64_t taskId = mr::coprocessor::globalCoprocessor().submitPacket(
	    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::BentoDiagnosticsProjection,
	    sourceSnapshot.documentId(), sourceSnapshot.version(), mr::coprocessor::ExecutionOwnerKind::BentoPane,
	    static_cast<std::size_t>(problemsWindow->bufferId()), generation, mr::coprocessor::WorkDirection::None, 0,
	    static_cast<std::uint64_t>(outputSnapshot.length()), "bento diagnostics",
	    [sourceSnapshot, diagnosticSourceSnapshot, outputSnapshot, targetDocumentId, targetVersion, generation, sourcePath, trackWarnings, trackNotes,
	     parseOutput, diagnostics, sourceChanges](const mr::coprocessor::TaskInfo &info) {
		    mr::coprocessor::Result result;
		    std::string errorMessage;

		    result.task = info;
		    if (info.cancelRequested()) {
			    result.status = mr::coprocessor::TaskStatus::Cancelled;
			    return result;
		    }
		    result.payload = mrBuildBentoDiagnosticsProjection(sourceSnapshot, diagnosticSourceSnapshot, outputSnapshot, targetDocumentId, targetVersion,
		                                                         generation, sourcePath, trackWarnings, trackNotes, parseOutput,
		                                                         diagnostics, sourceChanges, info.cancelFlag.get(), &errorMessage);
		    if (result.payload != nullptr)
			    result.status = mr::coprocessor::TaskStatus::Completed;
		    else if (info.cancelRequested())
			    result.status = mr::coprocessor::TaskStatus::Cancelled;
		    else {
			    result.status = mr::coprocessor::TaskStatus::Failed;
			    result.error = errorMessage.empty() ? "diagnostics projection failed" : errorMessage;
		    }
		    return result;
	    });
	if (taskId == 0) return false;

	diagnosticsProjectionTask.taskId = taskId;
	diagnosticsProjectionTask.activeGeneration = generation;
	diagnosticsProjectionTask.sourceDocumentId = sourceSnapshot.documentId();
	diagnosticsProjectionTask.sourceVersion = sourceSnapshot.version();
	diagnosticsProjectionTask.inputDocumentId = outputSnapshot.documentId();
	diagnosticsProjectionTask.inputVersion = outputSnapshot.version();
	diagnosticsProjectionTask.inputBufferId = outputBufferId;
	diagnosticsProjectionTask.targetDocumentId = targetDocumentId;
	diagnosticsProjectionTask.targetVersion = targetVersion;
	diagnosticsProjectionTask.targetBufferId = problemsWindow->bufferId();
	diagnosticsProjectionTask.sourcePath = sourcePath;
	diagnosticsProjectionTask.trackWarnings = trackWarnings;
	diagnosticsProjectionTask.trackNotes = trackNotes;
	diagnosticsProjectionTask.diagnosticsRequest = request;
	diagnosticsProjectionTask.diagnosticSourceChanges = requestedSourceChanges;
	diagnosticsProjectionTask.diagnosticBaseDocumentId = request == bdprRemapExisting ? compilerDiagnosticsDocumentId : 0;
	diagnosticsProjectionTask.diagnosticBaseVersion = request == bdprRemapExisting ? compilerDiagnosticsVersion : 0;
	diagnosticsProjectionTask.pending = false;
	diagnosticsProjectionTask.pendingDiagnosticsRequest = bdprNone;
	diagnosticsProjectionTask.projectionCurrent = false;
	diagnosticsProjectionTask.retryBlocked = false;
	diagnosticsProjectionTask.requestedAt = std::chrono::steady_clock::now();
	trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::BentoDiagnosticsProjection, "bento diagnostics");
	return true;
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
	if (sourceEditor == nullptr || compilerDiagnostics == nullptr || compilerSidekickDiagnosticIndex >= compilerDiagnostics->size()) {
		clearTrackedCompilerSidekick(true);
		return;
	}

	const MRCompilerDiagnostic &diagnostic = (*compilerDiagnostics)[compilerSidekickDiagnosticIndex];
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
	compilerDiagnosticsParseRequired = true;
	if (compilerDiagnosticsSourceInvalidated) {
		if (pendingCompilerProblemNavigation != 0) {
			pendingCompilerProblemNavigation = 0;
			postCompilerProblemNavigationUnavailable();
		}
		return false;
	}
	MREditWindow *outputWindow = buildOutputPane();
	if (outputWindow != nullptr && outputWindow->hasTrackedExternalIoTasks()) return true;
	diagnosticsProjectionTask.retryBlocked = false;
	const bool submitted = submitCompilerDiagnosticsProjection(bdprParseOutput);
	if (!submitted && pendingCompilerProblemNavigation != 0) {
		pendingCompilerProblemNavigation = 0;
		postCompilerProblemNavigationUnavailable();
	}
	return submitted;
}

bool MRBentoBox::requestCompilerProblemNavigation(bool forward) {
	pendingCompilerProblemNavigation = forward ? 1 : -1;
	diagnosticsProjectionTask.retryBlocked = false;
	if (!refreshCompilerProblemsPane()) {
		if (pendingCompilerProblemNavigation != 0) {
			pendingCompilerProblemNavigation = 0;
			postCompilerProblemNavigationUnavailable();
		}
		return true;
	}
	if (!compilerDiagnosticsCurrent()) return true;

	const int navigation = pendingCompilerProblemNavigation;
	pendingCompilerProblemNavigation = 0;
	const bool navigated = navigation > 0 ? jumpToNextProblem() : jumpToPreviousProblem();
	if (!navigated) postCompilerProblemNavigationUnavailable();
	return true;
}

void MRBentoBox::syncCompilerDiagnosticsAfterSourceMutation(const MRTextBufferModel::ReadSnapshot &oldSnapshot, const MRTextBufferModel::DocumentChangeSet &changeSet) {
	if (!changeSet.changed || changeSet.oldVersion != oldSnapshot.version()) return;
	clearTrackedCompilerSidekick(true);
	const MRTextBufferModel::ReadSnapshot newSnapshot = buffer().readSnapshot();
	if (changeSet.newVersion != newSnapshot.version()) return;
	const bool diagnosticsTaskActive = diagnosticsProjectionTask.taskId != 0;
	if ((compilerDiagnostics == nullptr || compilerDiagnostics->empty()) && !diagnosticsTaskActive && !compilerDiagnosticsParseRequired) return;
	if (compilerDiagnosticSourceChanges == nullptr) {
		const std::size_t baseDocumentId = diagnosticsTaskActive && diagnosticsProjectionTask.diagnosticsRequest == bdprParseOutput
		                                       ? diagnosticsProjectionTask.sourceDocumentId
		                                       : compilerDiagnosticsDocumentId;
		const std::size_t baseVersion = diagnosticsTaskActive && diagnosticsProjectionTask.diagnosticsRequest == bdprParseOutput
		                                  ? diagnosticsProjectionTask.sourceVersion
		                                  : compilerDiagnosticsVersion;
		if (baseDocumentId != oldSnapshot.documentId() || baseVersion != oldSnapshot.version()) {
			compilerDiagnosticsSourceInvalidated = true;
			compilerDiagnosticsParseRequired = false;
			return;
		}
	} else {
		const MRTextBufferModel::ReadSnapshot &previous = compilerDiagnosticSourceChanges->newSnapshot;
		if (previous.documentId() != oldSnapshot.documentId() || previous.version() != oldSnapshot.version()) {
			compilerDiagnosticsSourceInvalidated = true;
			compilerDiagnosticsParseRequired = false;
			return;
		}
	}
	compilerDiagnosticSourceChanges = std::make_shared<const MRBentoDiagnosticSourceChange>(oldSnapshot, newSnapshot, changeSet, compilerDiagnosticSourceChanges);
	if (compilerDiagnosticsParseRequired) {
		if (diagnosticsProjectionTask.taskId != 0 && diagnosticsProjectionTask.diagnosticsRequest == bdprParseOutput)
			static_cast<void>(submitCompilerDiagnosticsProjection(bdprRemapExisting));
		return;
	}
	if (compilerDiagnostics == nullptr || compilerDiagnostics->empty()) return;
	static_cast<void>(submitCompilerDiagnosticsProjection(bdprRemapExisting));
}

bool MRBentoBox::compilerDiagnosticsCurrent() const {
	MREditWindow *outputWindow = buildOutputPane();
	MREditWindow *problemsWindow = problemsPane();
	MRFileEditor *sourceEditor = getEditor();
	MRFileEditor *problemsEditor = problemsWindow != nullptr ? problemsWindow->getEditor() : nullptr;

	if (compilerDiagnosticsParseRequired || compilerDiagnosticsSourceInvalidated || compilerDiagnosticSourceChanges != nullptr ||
	    diagnosticsProjectionTask.taskId != 0 || !diagnosticsProjectionTask.projectionCurrent || compilerDiagnostics == nullptr ||
	    outputWindow == nullptr || problemsWindow == nullptr || sourceEditor == nullptr || problemsEditor == nullptr)
		return false;
	return compilerDiagnosticsDocumentId == sourceEditor->documentId() && compilerDiagnosticsVersion == sourceEditor->documentVersion() &&
	       compilerDiagnosticsOutputDocumentId == outputWindow->documentId() && compilerDiagnosticsOutputVersion == outputWindow->documentVersion() &&
	       compilerDiagnosticsOutputBufferId == outputWindow->bufferId() && compilerProblemsTargetDocumentId == problemsEditor->documentId() &&
	       compilerProblemsTargetVersion == problemsEditor->documentVersion() && compilerProblemsTargetBufferId == problemsWindow->bufferId() &&
	       diagnosticsProjectionTask.sourcePath == currentFileName() && diagnosticsProjectionTask.trackWarnings == configuredTrackCompilerWarnings() &&
	       diagnosticsProjectionTask.trackNotes == configuredTrackCompilerNotes();
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

	if (sourceEditor == nullptr || problemsEditor == nullptr || outputEditor == nullptr || !compilerDiagnosticsCurrent()) return false;
	cursorOffset = problemsEditor->cursorOffset();
	if (compilerDiagnostics == nullptr) return false;
	for (std::size_t i = 0; i < compilerDiagnostics->size(); ++i) {
		const MRCompilerDiagnostic &diagnostic = (*compilerDiagnostics)[i];
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

	if (problemsEditor == nullptr || compilerDiagnostics == nullptr || compilerDiagnostics->empty() || !compilerDiagnosticsCurrent()) return false;
	cursorOffset = problemsEditor->cursorOffset();
	for (const MRCompilerDiagnostic &diagnostic : *compilerDiagnostics)
		if (diagnostic.problemOffset > cursorOffset) {
			next = &diagnostic;
			break;
		}
	if (next == nullptr) next = &compilerDiagnostics->front();
	problemsEditor->setCursorOffset(next->problemOffset);
	problemsEditor->setSelectionOffsets(next->problemOffset, problemsEditor->lineEndOffset(next->problemOffset));
	return jumpToProblemAtCursor();
}

bool MRBentoBox::jumpToPreviousProblem() {
	MREditWindow *problemsWindow = problemsPane();
	MRFileEditor *problemsEditor = problemsWindow != nullptr ? problemsWindow->getEditor() : nullptr;
	std::size_t cursorOffset;
	const MRCompilerDiagnostic *previous = nullptr;

	if (problemsEditor == nullptr || compilerDiagnostics == nullptr || compilerDiagnostics->empty() || !compilerDiagnosticsCurrent()) return false;
	cursorOffset = problemsEditor->cursorOffset();
	for (const MRCompilerDiagnostic &diagnostic : *compilerDiagnostics) {
		if (diagnostic.problemOffset >= cursorOffset) break;
		previous = &diagnostic;
	}
	if (previous == nullptr) previous = &compilerDiagnostics->back();
	problemsEditor->setCursorOffset(previous->problemOffset);
	problemsEditor->setSelectionOffsets(previous->problemOffset, problemsEditor->lineEndOffset(previous->problemOffset));
	return jumpToProblemAtCursor();
}
