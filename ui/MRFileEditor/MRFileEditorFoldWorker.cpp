#include "MRFileEditor.hpp"

#include <algorithm>

namespace {

const char *foldDirectionName(mr::coprocessor::WorkDirection direction) noexcept {
	return direction == mr::coprocessor::WorkDirection::Bof ? "BOF" : "EOF";
}

} // namespace

void MRFileEditor::submitFoldPacket(FoldPacketState &packet, const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t totalLines, bool documentEndKnown) {
	if (packet.taskId != 0 || packet.resultReady || packet.endLine <= packet.startLine) return;
	const std::size_t documentId = mFoldWarmupState.documentId;
	const std::size_t version = mFoldWarmupState.version;
	const MRSyntaxLanguage language = mFoldWarmupState.language;
	const std::uint64_t generation = packet.generation;
	const mr::coprocessor::WorkDirection direction = packet.direction;
	const std::size_t startLine = packet.startLine;
	const std::size_t endLine = packet.endLine;
	const bool retainProjectionData = !packet.contextOnly;
	const MRFoldAnalysisState inputState = packet.inputState;
	std::map<std::size_t, MRFoldSpan> closedFoldSpans;
	if (retainProjectionData) closedFoldSpans = mFoldState.closedFoldSpans();
	const std::string label = std::string(foldWarmupTaskLabel()) + " " + foldDirectionName(direction) + " lines " + std::to_string(startLine + 1) + "-" + std::to_string(endLine);

	packet.taskId = mr::coprocessor::globalCoprocessor().submitPacket(
	    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FoldWarmup, documentId, version, mExecutionOwnerKind, mExecutionOwnerLocalId, generation,
	    direction, startLine, endLine, label,
	    [snapshot, language, generation, direction, startLine, endLine, totalLines, documentEndKnown, retainProjectionData, inputState,
	     closedFoldSpans](const mr::coprocessor::TaskInfo &info) {
		    mr::coprocessor::Result result;
		    result.task = info;
		    if (info.cancelRequested()) {
			    result.status = mr::coprocessor::TaskStatus::Cancelled;
			    return result;
		    }
		    result.payload = buildFoldWarmupPayload(snapshot, language, generation, direction, startLine, endLine, totalLines, documentEndKnown, inputState, closedFoldSpans,
		                                            info.cancelFlag, retainProjectionData);
		    result.status = result.payload != nullptr ? mr::coprocessor::TaskStatus::Completed : mr::coprocessor::TaskStatus::Cancelled;
		    return result;
	    });
}

void MRFileEditor::submitFoldPackets(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t totalLines, bool documentEndKnown) {
	if (mFoldWarmupState.failureLatched) return;
	const std::size_t workerBudget = std::max<std::size_t>(1, mr::coprocessor::globalCoprocessor().allowedCoreCount());
	std::size_t runningCount = 0;
	bool submittedAny = false;
	for (const FoldPacketState &packet : mFoldWarmupState.packets)
		if (packet.taskId != 0) ++runningCount;
	for (FoldPacketState &packet : mFoldWarmupState.packets) {
		if (runningCount >= workerBudget) break;
		if (packet.generation != mFoldWarmupState.generation || packet.taskId != 0 || packet.resultReady) continue;
		submitFoldPacket(packet, snapshot, totalLines, documentEndKnown);
		if (packet.taskId != 0) {
			++runningCount;
			submittedAny = true;
		}
	}
	if (submittedAny) notifyWindowTaskStateChanged();
}

void MRFileEditor::scheduleFoldWarmupIfNeeded(std::size_t scanTopLine, std::size_t scanBottomLine, std::size_t topLine, std::size_t requestBottomLine, MRSyntaxLanguage language) {
	if (!foldingPipelineEnabled()) {
		static_cast<void>(cancelFoldWarmup());
		invalidateFoldCache();
		return;
	}
	const std::size_t documentId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	const bool documentEndKnown = mBufferModel.exactLineCountKnown();
	const std::size_t totalLines = documentEndKnown ? std::max<std::size_t>(1, mBufferModel.lineCount()) : scanBottomLine + 1;
	if (documentEndKnown) {
		scanTopLine = std::min(scanTopLine, totalLines - 1);
		scanBottomLine = std::min(scanBottomLine, totalLines);
		topLine = std::min(topLine, totalLines - 1);
		requestBottomLine = std::min(requestBottomLine, totalLines);
	}
	if (scanBottomLine <= scanTopLine) {
		static_cast<void>(cancelViewportFoldWarmup());
		return;
	}
	const bool differentDocument = mFoldWarmupState.documentId != 0 &&
	                               (mFoldWarmupState.documentId != documentId || mFoldWarmupState.version != version || mFoldWarmupState.language != language);
	if (differentDocument) static_cast<void>(cancelFoldWarmup());
	if (mFoldWarmupState.documentId == 0) {
		mFoldWarmupState.documentId = documentId;
		mFoldWarmupState.version = version;
		mFoldWarmupState.language = language;
	}

	static_cast<void>(adoptReadyFoldPackets());
	const bool currentGenerationCoversRequest = mFoldWarmupState.generation != 0 && scanTopLine >= mFoldWarmupState.scanTopLine && scanBottomLine <= mFoldWarmupState.scanBottomLine &&
	                                            topLine >= mFoldWarmupState.visibleTopLine && requestBottomLine <= mFoldWarmupState.visibleBottomLine;
	const bool exactGenerationRequest = scanTopLine == mFoldWarmupState.scanTopLine && scanBottomLine == mFoldWarmupState.scanBottomLine &&
	                                    topLine == mFoldWarmupState.visibleTopLine && requestBottomLine == mFoldWarmupState.visibleBottomLine;
	if (currentGenerationCoversRequest && (!mFoldWarmupState.failureLatched || exactGenerationRequest)) {
		if (!mFoldWarmupState.failureLatched) {
			const MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
			submitFoldPackets(snapshot, totalLines, documentEndKnown);
		}
		return;
	}

	// A superseded viewport generation has no cross-generation structural checkpoint.
	// Its finite workers may finish, but their results must not remain in the editor ledger.
	supersedeViewportFoldWarmup();
	mFoldWarmupState.documentId = documentId;
	mFoldWarmupState.version = version;
	mFoldWarmupState.language = language;

	std::size_t anchorLine = 0;
	MRFoldAnalysisState anchorState;
	if (!canonicalFoldContextForViewport(scanTopLine, scanBottomLine, topLine, requestBottomLine, language, anchorLine, anchorState)) {
		notifyWindowTaskStateChanged();
		return;
	}

	if (mFoldGenerationCounter == 0) ++mFoldGenerationCounter;
	mFoldWarmupState.generation = mFoldGenerationCounter++;
	mFoldWarmupState.scanTopLine = scanTopLine;
	mFoldWarmupState.scanBottomLine = scanBottomLine;
	mFoldWarmupState.visibleTopLine = topLine;
	mFoldWarmupState.visibleBottomLine = requestBottomLine;
	mFoldCanonicalContextState.requestValid = false;
	FoldCheckpointState anchor;
	anchor.generation = mFoldWarmupState.generation;
	anchor.line = anchorLine;
	anchor.state = anchorState;
	mFoldWarmupState.checkpoints.push_back(anchor);

	const std::size_t lineCount = scanBottomLine - scanTopLine;
	const std::size_t allowedCoreCount = std::max<std::size_t>(1, mr::coprocessor::globalCoprocessor().allowedCoreCount());
	const bool bridgeNeeded = anchorLine < scanTopLine;
	if (bridgeNeeded) {
		FoldPacketState bridge;
		bridge.generation = mFoldWarmupState.generation;
		bridge.direction = mr::coprocessor::WorkDirection::Bof;
		bridge.startLine = anchorLine;
		bridge.endLine = scanTopLine;
		bridge.contextOnly = true;
		bridge.inputState = anchorState;
		bridge.inputStateConfirmed = true;
		mFoldWarmupState.packets.push_back(std::move(bridge));
	}
	const std::size_t visibleWorkerLimit = bridgeNeeded && allowedCoreCount > 1 ? allowedCoreCount - 1 : allowedCoreCount;
	const std::size_t visibleWorkerBudget = std::min(lineCount, visibleWorkerLimit);
	const std::size_t packetLines = (lineCount + visibleWorkerBudget - 1) / visibleWorkerBudget;
	std::size_t packetStartLine = scanTopLine;
	while (packetStartLine < scanBottomLine) {
		FoldPacketState packet;
		packet.generation = mFoldWarmupState.generation;
		packet.startLine = packetStartLine;
		packet.endLine = std::min(scanBottomLine, packetStartLine + packetLines);
		packet.direction = packet.endLine <= topLine ? mr::coprocessor::WorkDirection::Bof : mr::coprocessor::WorkDirection::Eof;
		packet.inputStateConfirmed = foldConfirmedStateForPacket(packet, packet.inputState);
		mFoldWarmupState.packets.push_back(packet);
		packetStartLine = packet.endLine;
	}
	const MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
	submitFoldPackets(snapshot, totalLines, documentEndKnown);
}
