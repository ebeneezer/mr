#include "MRBentoBox.hpp"
#include "../../outline/MROutlineFoldProducer.hpp"

#include <chrono>
#include <utility>

namespace {

MROutlineView outlineViewForBentoRole(MRBentoPaneRole role) noexcept {
	return role == bprFunctions ? mrovFunctions : mrovStructure;
}

} // namespace

void MRBentoBox::refreshOutlinePanes(bool force) {
	static_cast<void>(refreshOutlinePane(bprStructure, force));
	static_cast<void>(refreshOutlinePane(bprFunctions, force));
}

bool MRBentoBox::refreshOutlinePane(MRBentoPaneRole role, bool force) {
	return submitOutlineProjection(role, force);
}

bool MRBentoBox::submitOutlineProjection(MRBentoPaneRole role, bool force) {
	MREditWindow *outlineWindow = nullptr;
	MRFileEditor *sourceEditor = getEditor();
	BentoProjectionTaskState *taskState = nullptr;
	MRBentoOutlinePaneState *outlineState = nullptr;
	MRFoldOutlineInputSnapshot input;
	MROutlineRequest request;
	bool completeCoverageDesired = false;
	bool completeWarmupRequested = false;
	bool ready = false;
	const bool callerForce = force;

	switch (role) {
		case bprStructure:
			outlineWindow = structurePane();
			taskState = &structureProjectionTask;
			outlineState = &structureOutlineState;
			break;
		case bprFunctions:
			outlineWindow = functionsPane();
			taskState = &functionsProjectionTask;
			outlineState = &functionsOutlineState;
			break;
		default:
			return false;
	}
	MRFileEditor *outlineEditor = outlineWindow != nullptr ? outlineWindow->getEditor() : nullptr;
	if (outlineWindow == nullptr || outlineEditor == nullptr || sourceEditor == nullptr || taskState == nullptr || outlineState == nullptr) return false;
	const std::size_t currentDocumentId = sourceEditor->documentId();
	const std::size_t currentVersion = sourceEditor->documentVersion();
	const auto now = std::chrono::steady_clock::now();
	const std::uint64_t observedInputRevision = sourceEditor->foldOutlineInputRevision();
	const MRSyntaxLanguage observedInputLanguage = sourceEditor->syntaxLanguage();
	const bool observedInputComplete = sourceEditor->completeFoldOutlineInputAvailable();
	const bool activeRouteMatches = taskState->sourceDocumentId == currentDocumentId && taskState->sourceVersion == currentVersion &&
	                                taskState->inputLanguage == observedInputLanguage && taskState->targetDocumentId == outlineEditor->documentId() &&
	                                taskState->targetVersion == outlineEditor->documentVersion() && taskState->targetBufferId == outlineWindow->bufferId();
	const bool activeInputMatches = activeRouteMatches && taskState->inputRevision == observedInputRevision &&
	                                taskState->inputComplete == observedInputComplete;
	const bool completeProjectionStale = outlineState->complete &&
	                                     (outlineState->documentId != currentDocumentId || outlineState->version != currentVersion ||
	                                      outlineState->language != observedInputLanguage || outlineState->targetDocumentId != outlineEditor->documentId() ||
	                                      outlineState->targetVersion != outlineEditor->documentVersion() ||
	                                      outlineState->targetBufferId != outlineWindow->bufferId());
	const bool continueCompleteCoverage = taskState->completeCoverageRequested && !outlineState->complete;
	if (callerForce || continueCompleteCoverage || completeProjectionStale) {
		completeCoverageDesired = sourceEditor->canRequestCompleteFoldOutlineWarmup();
		if (completeCoverageDesired) completeWarmupRequested = sourceEditor->requestCompleteFoldOutlineWarmup();
		if (completeCoverageDesired) taskState->completeCoverageRequested = true;
	}
	if (continueCompleteCoverage && taskState->projectionCurrent && activeRouteMatches && !observedInputComplete) {
		if (now - taskState->requestedAt < std::chrono::milliseconds(100) || completeCoverageDesired) return true;
	}
	force = callerForce || completeProjectionStale;
	if (taskState->taskId != 0) {
		if (!force && activeInputMatches) return true;
		taskState->pending = true;
		taskState->pendingForce = taskState->pendingForce || force;
		return true;
	}
	if (taskState->retryBlocked && activeInputMatches && !callerForce && !completeProjectionStale) return false;
	if (callerForce || completeProjectionStale || !activeInputMatches) taskState->retryBlocked = false;
	if (!force && taskState->projectionCurrent && activeInputMatches) return true;

	request.view = outlineViewForBentoRole(role);
	request.allowPartial = true;
	if (taskState->taskId != 0) return true;
	ready = sourceEditor->captureFoldOutlineInput(request, input);
	std::shared_ptr<const MRTextBufferModel::ReadSnapshot> sourceSnapshot = ready ? input.readSnapshot : std::shared_ptr<const MRTextBufferModel::ReadSnapshot>();
	if (sourceSnapshot == nullptr) sourceSnapshot = std::make_shared<const MRTextBufferModel::ReadSnapshot>(buffer().readSnapshot());
	if (sourceSnapshot->documentId() != currentDocumentId || sourceSnapshot->version() != currentVersion) return false;
	if (ready && (input.documentId != sourceSnapshot->documentId() || input.version != sourceSnapshot->version())) return false;
	const std::uint64_t submittedInputRevision = ready ? input.visibleRevision : sourceEditor->foldOutlineInputRevision();
	if (taskState->generationCounter == 0) taskState->generationCounter = 1;
	const std::uint64_t generation = taskState->generationCounter++;
	const bool functions = role == bprFunctions;
	const std::size_t snapshotDocumentId = ready ? input.documentId : sourceSnapshot->documentId();
	const std::size_t snapshotVersion = ready ? input.version : sourceSnapshot->version();
	const bool snapshotComplete = ready && input.complete;
	const MRSyntaxLanguage inputLanguage = ready ? input.language : sourceEditor->syntaxLanguage();
	const std::size_t targetDocumentId = outlineEditor->documentId();
	const std::size_t targetVersion = outlineEditor->documentVersion();
	const std::uint64_t packetStart = ready ? static_cast<std::uint64_t>(input.topLine) : 0;
	const std::uint64_t packetEnd = ready ? static_cast<std::uint64_t>(input.bottomLine) : 0;
	std::shared_ptr<const MRFoldOutlineInputSnapshot> inputSnapshot;
	if (ready) inputSnapshot = std::make_shared<const MRFoldOutlineInputSnapshot>(std::move(input));
	const char *label = functions ? "bento functions" : "bento structure";
	const std::uint64_t taskId = mr::coprocessor::globalCoprocessor().submitPacket(
	    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::BentoOutlineProjection,
	    sourceSnapshot->documentId(), sourceSnapshot->version(), mr::coprocessor::ExecutionOwnerKind::BentoPane,
	    static_cast<std::size_t>(outlineWindow->bufferId()), generation, mr::coprocessor::WorkDirection::None,
	    packetStart, packetEnd, label,
	    [sourceSnapshot, inputSnapshot, inputLanguage, completeWarmupRequested, functions, targetDocumentId, targetVersion, generation,
	     submittedInputRevision](const mr::coprocessor::TaskInfo &info) {
		    mr::coprocessor::Result result;

		    result.task = info;
		    if (info.cancelRequested()) {
			    result.status = mr::coprocessor::TaskStatus::Cancelled;
			    return result;
		    }
		    MROutlineSnapshot snapshot;
		    const bool snapshotReady = inputSnapshot != nullptr && mrBuildFoldOutlineSnapshot(*inputSnapshot, snapshot, info.cancelFlag.get());
		    if (info.cancelRequested()) {
			    result.status = mr::coprocessor::TaskStatus::Cancelled;
			    return result;
		    }
		    result.payload = mrBuildBentoOutlineProjection(*sourceSnapshot, snapshot, snapshotReady, inputLanguage, completeWarmupRequested, functions,
		                                                     targetDocumentId, targetVersion, generation, submittedInputRevision, info.cancelFlag.get());
		    result.status = result.payload != nullptr ? mr::coprocessor::TaskStatus::Completed : mr::coprocessor::TaskStatus::Cancelled;
		    return result;
	    });
	if (taskId == 0) return false;
	taskState->taskId = taskId;
	taskState->activeGeneration = generation;
	taskState->sourceDocumentId = sourceSnapshot->documentId();
	taskState->sourceVersion = sourceSnapshot->version();
	taskState->inputDocumentId = snapshotDocumentId;
	taskState->inputVersion = snapshotVersion;
	taskState->inputRevision = submittedInputRevision;
	taskState->inputLanguage = inputLanguage;
	taskState->inputComplete = snapshotComplete;
	taskState->targetDocumentId = targetDocumentId;
	taskState->targetVersion = targetVersion;
	taskState->targetBufferId = outlineWindow->bufferId();
	taskState->pending = false;
	taskState->pendingForce = false;
	taskState->completeCoverageRequested = snapshotComplete || completeCoverageDesired || completeWarmupRequested;
	taskState->projectionCurrent = false;
	taskState->retryBlocked = false;
	taskState->requestedAt = now;
	trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::BentoOutlineProjection, label);
	return true;
}

bool MRBentoBox::jumpToOutlineAtCursor(MRBentoPaneRole role) {
	MREditWindow *outlineWindow = nullptr;
	MRFileEditor *sourceEditor = getEditor();
	MRFileEditor *outlineEditor = nullptr;
	std::shared_ptr<const std::vector<MRBentoOutlineEntry>> entries;
	const MRBentoOutlinePaneState *outlineState = nullptr;
	std::size_t cursorOffset = 0;
	const MRBentoOutlineEntry *selected = nullptr;

	switch (role) {
		case bprStructure:
			outlineWindow = structurePane();
			entries = structureOutlineEntries;
			outlineState = &structureOutlineState;
			break;
		case bprFunctions:
			outlineWindow = functionsPane();
			entries = functionsOutlineEntries;
			outlineState = &functionsOutlineState;
			break;
		default:
			return false;
	}
	outlineEditor = outlineWindow != nullptr ? outlineWindow->getEditor() : nullptr;
	if (sourceEditor == nullptr || outlineEditor == nullptr || entries == nullptr || outlineState == nullptr) return false;
	if (outlineState->documentId != sourceEditor->documentId() || outlineState->version != sourceEditor->documentVersion() ||
	    outlineState->language != sourceEditor->syntaxLanguage() || outlineState->targetDocumentId != outlineEditor->documentId() ||
	    outlineState->targetVersion != outlineEditor->documentVersion() || outlineState->targetBufferId != outlineWindow->bufferId())
		return false;
	cursorOffset = outlineEditor->cursorOffset();
	for (const MRBentoOutlineEntry &entry : *entries) {
		const std::size_t lineEnd = outlineEditor->lineEndOffset(entry.paneOffset);
		if (cursorOffset >= entry.paneOffset && cursorOffset <= lineEnd) {
			selected = &entry;
			break;
		}
	}
	if (selected == nullptr) return false;
	outlineEditor->setCursorOffset(selected->paneOffset);
	outlineEditor->setSelectionOffsets(selected->paneOffset, outlineEditor->lineEndOffset(selected->paneOffset));
	sourceEditor->setCursorOffset(selected->sourceOffset);
	sourceEditor->setSelectionOffsets(selected->sourceOffset, selected->sourceSelectionEnd);
	sourceEditor->revealCursor(True);
	activatePrimaryPane();
	return true;
}
