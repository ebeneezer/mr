#include "MRFoldWarmupPayload.hpp"

#include <algorithm>

namespace {

constexpr std::size_t kCanonicalFoldPacketLines = 65536;

} // namespace

bool MRFileEditor::canonicalFoldContextForViewport(std::size_t scanTopLine, std::size_t scanBottomLine, std::size_t topLine, std::size_t requestBottomLine,
                                                   MRSyntaxLanguage language, std::size_t &anchorLine, MRFoldAnalysisState &anchorState) {
	const std::size_t documentId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	const bool sameSource = mFoldCanonicalContextState.generation != 0 && mFoldCanonicalContextState.documentId == documentId &&
	                        mFoldCanonicalContextState.version == version && mFoldCanonicalContextState.language == language;
	if (!sameSource) {
		static_cast<void>(cancelCanonicalFoldContext());
		if (mFoldGenerationCounter == 0) ++mFoldGenerationCounter;
		mFoldCanonicalContextState.documentId = documentId;
		mFoldCanonicalContextState.version = version;
		mFoldCanonicalContextState.language = language;
		mFoldCanonicalContextState.generation = mFoldGenerationCounter++;
		FoldCheckpointState checkpoint;
		checkpoint.generation = mFoldCanonicalContextState.generation;
		checkpoint.line = 0;
		mFoldCanonicalContextState.checkpoints.push_back(std::move(checkpoint));
	}
	const bool requestChanged = mFoldCanonicalContextState.requestedScanTopLine != scanTopLine ||
	                            mFoldCanonicalContextState.requestedScanBottomLine != scanBottomLine ||
	                            mFoldCanonicalContextState.requestedVisibleTopLine != topLine ||
	                            mFoldCanonicalContextState.requestedVisibleBottomLine != requestBottomLine;
	if (requestChanged || !mFoldCanonicalContextState.requestValid) mFoldCanonicalContextState.failureLatched = false;

	mFoldCanonicalContextState.requestedScanTopLine = scanTopLine;
	mFoldCanonicalContextState.requestedScanBottomLine = scanBottomLine;
	mFoldCanonicalContextState.requestedVisibleTopLine = topLine;
	mFoldCanonicalContextState.requestedVisibleBottomLine = requestBottomLine;
	mFoldCanonicalContextState.requestValid = true;
	mFoldCanonicalContextState.targetLine = std::max(mFoldCanonicalContextState.targetLine, scanTopLine);
	continueCanonicalFoldContextIfNeeded();
	if (mFoldCanonicalContextState.totalLines == 0) return false;

	for (std::vector<FoldCheckpointState>::const_reverse_iterator checkpoint = mFoldCanonicalContextState.checkpoints.rbegin();
	     checkpoint != mFoldCanonicalContextState.checkpoints.rend(); ++checkpoint) {
		if (checkpoint->generation != mFoldCanonicalContextState.generation || checkpoint->line > scanTopLine) continue;
		if (scanTopLine - checkpoint->line > kCanonicalFoldPacketLines) return false;
		anchorLine = checkpoint->line;
		anchorState = checkpoint->state;
		return true;
	}
	return false;
}

void MRFileEditor::continueCanonicalFoldContextIfNeeded() {
	if (mFoldCanonicalContextState.generation == 0) return;
	const bool sameSource = mFoldCanonicalContextState.documentId == mBufferModel.documentId() && mFoldCanonicalContextState.version == mBufferModel.version() &&
	                        mFoldCanonicalContextState.language == mBufferModel.language();
	if (!sameSource || !foldingPipelineEnabled()) {
		static_cast<void>(cancelCanonicalFoldContext());
		return;
	}
	if (mFoldCanonicalContextState.failureLatched) return;
	if (!mBufferModel.exactLineCountKnown()) {
		mFoldCanonicalContextState.waitingForLineIndex = true;
		scheduleLineIndexWarmupIfNeeded();
		return;
	}
	if (mFoldCanonicalContextState.totalLines == 0) {
		mFoldCanonicalContextState.waitingForLineIndex = false;
		mFoldCanonicalContextState.totalLines = std::max<std::size_t>(1, mBufferModel.lineCount());
		mFoldCanonicalContextState.requestedScanTopLine = std::min(mFoldCanonicalContextState.requestedScanTopLine, mFoldCanonicalContextState.totalLines - 1);
		mFoldCanonicalContextState.requestedScanBottomLine = std::min(mFoldCanonicalContextState.requestedScanBottomLine, mFoldCanonicalContextState.totalLines);
		mFoldCanonicalContextState.requestedVisibleTopLine = std::min(mFoldCanonicalContextState.requestedVisibleTopLine, mFoldCanonicalContextState.totalLines - 1);
		mFoldCanonicalContextState.requestedVisibleBottomLine = std::min(mFoldCanonicalContextState.requestedVisibleBottomLine, mFoldCanonicalContextState.totalLines);
		mFoldCanonicalContextState.targetLine = mFoldCanonicalContextState.totalLines;
	}
	appendCanonicalFoldContextPackets();
	submitCanonicalFoldContextPackets();
}

void MRFileEditor::appendCanonicalFoldContextPackets() {
	if (mFoldCanonicalContextState.generation == 0 || mFoldCanonicalContextState.totalLines == 0) return;
	const std::size_t targetLine = std::min(mFoldCanonicalContextState.targetLine, mFoldCanonicalContextState.totalLines);
	const std::size_t workerBudget = std::max<std::size_t>(1, mr::coprocessor::globalCoprocessor().allowedCoreCount());
	while (mFoldCanonicalContextState.packets.size() < workerBudget && mFoldCanonicalContextState.nextPacketStartLine < targetLine) {
		FoldCanonicalPacketState packet;
		packet.generation = mFoldCanonicalContextState.generation;
		packet.startLine = mFoldCanonicalContextState.nextPacketStartLine;
		packet.endLine = std::min(targetLine, packet.startLine + kCanonicalFoldPacketLines);
		mFoldCanonicalContextState.packets.push_back(packet);
		mFoldCanonicalContextState.nextPacketStartLine = packet.endLine;
	}
}

void MRFileEditor::submitCanonicalFoldAcquisition(FoldCanonicalPacketState &packet, const MRTextBufferModel::ReadSnapshot &snapshot) {
	if (packet.taskId != 0 || packet.stage != FoldCanonicalPacketStage::AwaitingAcquisition || packet.endLine <= packet.startLine) return;
	const std::size_t documentId = mFoldCanonicalContextState.documentId;
	const std::size_t version = mFoldCanonicalContextState.version;
	const MRSyntaxLanguage language = mFoldCanonicalContextState.language;
	const std::uint64_t generation = packet.generation;
	const std::size_t startLine = packet.startLine;
	const std::size_t endLine = packet.endLine;
	const std::size_t totalLines = mFoldCanonicalContextState.totalLines;
	const std::string label = std::string(foldWarmupTaskLabel()) + " canonical acquire BOF lines " + std::to_string(startLine + 1) + "-" + std::to_string(endLine);

	packet.stage = FoldCanonicalPacketStage::Acquiring;
	packet.taskId = mr::coprocessor::globalCoprocessor().submitPacket(
	    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FoldWarmup, documentId, version, mExecutionOwnerKind, mExecutionOwnerLocalId, generation,
	    mr::coprocessor::WorkDirection::Bof, startLine, endLine, label, [snapshot, language, generation, startLine, endLine, totalLines](const mr::coprocessor::TaskInfo &info) {
		    mr::coprocessor::Result result;
		    result.task = info;
		    if (info.cancelRequested()) {
			    result.status = mr::coprocessor::TaskStatus::Cancelled;
			    return result;
		    }
		    result.payload = buildFoldLineAcquisitionPayload(snapshot, language, generation, startLine, endLine, totalLines, info.cancelFlag);
		    result.status = result.payload != nullptr ? mr::coprocessor::TaskStatus::Completed : mr::coprocessor::TaskStatus::Cancelled;
		    return result;
	    });
	if (packet.taskId == 0) packet.stage = FoldCanonicalPacketStage::AwaitingAcquisition;
}

void MRFileEditor::submitCanonicalFoldValidation(FoldCanonicalPacketState &packet) {
	if (packet.taskId != 0 || packet.stage != FoldCanonicalPacketStage::Acquired || packet.lineTexts == nullptr ||
	    mFoldCanonicalContextState.checkpoints.empty() || packet.startLine != mFoldCanonicalContextState.checkpoints.back().line)
		return;
	const std::size_t documentId = mFoldCanonicalContextState.documentId;
	const std::size_t version = mFoldCanonicalContextState.version;
	const MRSyntaxLanguage language = mFoldCanonicalContextState.language;
	const std::uint64_t generation = packet.generation;
	const std::size_t startLine = packet.startLine;
	const std::size_t endLine = packet.endLine;
	const std::size_t totalLines = mFoldCanonicalContextState.totalLines;
	const MRFoldAnalysisState inputState = mFoldCanonicalContextState.checkpoints.back().state;
	const std::shared_ptr<const std::vector<std::string>> lineTexts = packet.lineTexts;
	const std::string label = std::string(foldWarmupTaskLabel()) + " canonical validate EOF lines " + std::to_string(startLine + 1) + "-" + std::to_string(endLine);

	packet.stage = FoldCanonicalPacketStage::Validating;
	packet.taskId = mr::coprocessor::globalCoprocessor().submitPacket(
	    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FoldWarmup, documentId, version, mExecutionOwnerKind, mExecutionOwnerLocalId, generation,
	    mr::coprocessor::WorkDirection::Eof, startLine, endLine, label,
	    [lineTexts, language, generation, startLine, endLine, totalLines, inputState](const mr::coprocessor::TaskInfo &info) {
		    mr::coprocessor::Result result;
		    result.task = info;
		    if (info.cancelRequested()) {
			    result.status = mr::coprocessor::TaskStatus::Cancelled;
			    return result;
		    }
		    result.payload = buildFoldValidationPayload(lineTexts, language, generation, startLine, endLine, totalLines, inputState, false, 0, info.cancelFlag);
		    result.status = result.payload != nullptr ? mr::coprocessor::TaskStatus::Completed : mr::coprocessor::TaskStatus::Cancelled;
		    return result;
	    });
	if (packet.taskId == 0) packet.stage = FoldCanonicalPacketStage::Acquired;
}

void MRFileEditor::submitCanonicalFoldContextPackets() {
	if (mFoldCanonicalContextState.failureLatched) return;
	const std::size_t workerBudget = std::max<std::size_t>(1, mr::coprocessor::globalCoprocessor().allowedCoreCount());
	std::size_t runningCount = 0;
	bool submittedAny = false;
	for (const FoldCanonicalPacketState &packet : mFoldCanonicalContextState.packets)
		if (packet.taskId != 0) ++runningCount;
	if (runningCount >= workerBudget) return;
	for (FoldCanonicalPacketState &packet : mFoldCanonicalContextState.packets) {
		if (packet.stage != FoldCanonicalPacketStage::Acquired || mFoldCanonicalContextState.checkpoints.empty() ||
		    packet.startLine != mFoldCanonicalContextState.checkpoints.back().line)
			continue;
		submitCanonicalFoldValidation(packet);
		if (packet.taskId != 0) {
			++runningCount;
			submittedAny = true;
		}
		break;
	}
	if (runningCount < workerBudget) {
		const MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
		for (FoldCanonicalPacketState &packet : mFoldCanonicalContextState.packets) {
			if (runningCount >= workerBudget) break;
			if (packet.stage != FoldCanonicalPacketStage::AwaitingAcquisition) continue;
			submitCanonicalFoldAcquisition(packet, snapshot);
			if (packet.taskId != 0) {
				++runningCount;
				submittedAny = true;
			}
		}
	}
	if (submittedAny) notifyWindowTaskStateChanged();
}

bool MRFileEditor::applyCanonicalFoldContext(const mr::coprocessor::Payload &payload, const mr::coprocessor::Result &result) {
	if (result.task.id == 0 || mFoldCanonicalContextState.documentId != mBufferModel.documentId() ||
	    mFoldCanonicalContextState.version != result.task.baseVersion || mBufferModel.version() != result.task.baseVersion ||
	    mFoldCanonicalContextState.language != mBufferModel.language() || result.task.generation != mFoldCanonicalContextState.generation || !result.task.hasPacketSpan)
		return false;
	const FoldLineAcquisitionPayload *acquisition = dynamic_cast<const FoldLineAcquisitionPayload *>(&payload);
	if (acquisition != nullptr) {
		for (FoldCanonicalPacketState &packet : mFoldCanonicalContextState.packets) {
			if (packet.taskId != result.task.id) continue;
			if (packet.stage != FoldCanonicalPacketStage::Acquiring || packet.generation != acquisition->generation || result.task.direction != mr::coprocessor::WorkDirection::Bof ||
			    packet.generation != mFoldCanonicalContextState.generation || packet.startLine != acquisition->startLine || packet.endLine != acquisition->endLine ||
			    result.task.packetStart != packet.startLine || result.task.packetEnd != packet.endLine ||
			    acquisition->language != mFoldCanonicalContextState.language || acquisition->lineTexts == nullptr ||
			    acquisition->lineTexts->size() < packet.endLine - packet.startLine)
				return false;
			packet.taskId = 0;
			packet.stage = FoldCanonicalPacketStage::Acquired;
			packet.lineTexts = acquisition->lineTexts;
			submitCanonicalFoldContextPackets();
			notifyWindowTaskStateChanged();
			return true;
		}
		return false;
	}

	const MRFoldWarmupPayload *foldWarmup = dynamic_cast<const MRFoldWarmupPayload *>(&payload);
	if (foldWarmup == nullptr) return false;
	for (std::size_t packetIndex = 0; packetIndex < mFoldCanonicalContextState.packets.size(); ++packetIndex) {
		FoldCanonicalPacketState &packet = mFoldCanonicalContextState.packets[packetIndex];
		if (packet.taskId != result.task.id) continue;
		if (packet.stage != FoldCanonicalPacketStage::Validating || packet.generation != foldWarmup->generation || result.task.direction != mr::coprocessor::WorkDirection::Eof ||
		    packet.generation != mFoldCanonicalContextState.generation || mFoldCanonicalContextState.checkpoints.empty() ||
		    packet.startLine != mFoldCanonicalContextState.checkpoints.back().line ||
		    result.task.packetStart != packet.startLine || result.task.packetEnd != packet.endLine ||
		    packet.startLine != foldWarmup->startLine || packet.endLine != foldWarmup->endLine || foldWarmup->language != mFoldCanonicalContextState.language ||
		    foldWarmup->direction != mr::coprocessor::WorkDirection::Eof || foldWarmup->stateIn != mFoldCanonicalContextState.checkpoints.back().state)
			return false;
		packet.taskId = 0;
		rememberCanonicalFoldCheckpoint(packet.endLine, foldWarmup->stateOut);
		mFoldCanonicalContextState.packets.erase(mFoldCanonicalContextState.packets.begin() + static_cast<std::ptrdiff_t>(packetIndex));
		appendCanonicalFoldContextPackets();
		submitCanonicalFoldContextPackets();
		resumeCanonicalFoldViewportIfReady();
		notifyWindowTaskStateChanged();
		return true;
	}
	return false;
}

void MRFileEditor::rememberCanonicalFoldCheckpoint(std::size_t line, const MRFoldAnalysisState &state) {
	if (mFoldCanonicalContextState.generation == 0 || mFoldCanonicalContextState.documentId != mBufferModel.documentId() ||
	    mFoldCanonicalContextState.version != mBufferModel.version() || mFoldCanonicalContextState.language != mBufferModel.language() ||
	    (mFoldCanonicalContextState.totalLines != 0 && line > mFoldCanonicalContextState.totalLines))
		return;
	std::vector<FoldCheckpointState>::iterator insertion = std::lower_bound(
	    mFoldCanonicalContextState.checkpoints.begin(), mFoldCanonicalContextState.checkpoints.end(), line,
	    [](const FoldCheckpointState &checkpoint, std::size_t checkpointLine) { return checkpoint.line < checkpointLine; });
	if (insertion != mFoldCanonicalContextState.checkpoints.end() && insertion->line == line) {
		insertion->generation = mFoldCanonicalContextState.generation;
		insertion->state = state;
		return;
	}
	FoldCheckpointState checkpoint;
	checkpoint.generation = mFoldCanonicalContextState.generation;
	checkpoint.line = line;
	checkpoint.state = state;
	mFoldCanonicalContextState.checkpoints.insert(insertion, std::move(checkpoint));
}

void MRFileEditor::resumeCanonicalFoldViewportIfReady() {
	if (mFoldCanonicalContextState.generation == 0 || !mFoldCanonicalContextState.requestValid ||
	    mFoldCanonicalContextState.requestedScanBottomLine <= mFoldCanonicalContextState.requestedScanTopLine)
		return;
	std::size_t anchorLine = 0;
	MRFoldAnalysisState anchorState;
	if (!canonicalFoldContextForViewport(mFoldCanonicalContextState.requestedScanTopLine, mFoldCanonicalContextState.requestedScanBottomLine,
	                                     mFoldCanonicalContextState.requestedVisibleTopLine, mFoldCanonicalContextState.requestedVisibleBottomLine,
	                                     mFoldCanonicalContextState.language, anchorLine, anchorState))
		return;
	scheduleFoldWarmupIfNeeded(mFoldCanonicalContextState.requestedScanTopLine, mFoldCanonicalContextState.requestedScanBottomLine,
	                           mFoldCanonicalContextState.requestedVisibleTopLine, mFoldCanonicalContextState.requestedVisibleBottomLine,
	                           mFoldCanonicalContextState.language);
}

std::size_t MRFileEditor::cancelCanonicalFoldContext() noexcept {
	std::size_t cancelledCount = 0;
	for (FoldCanonicalPacketState &packet : mFoldCanonicalContextState.packets) {
		if (packet.taskId == 0) continue;
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(packet.taskId));
		++cancelledCount;
	}
	const bool hadState = mFoldCanonicalContextState.generation != 0;
	mFoldCanonicalContextState = FoldCanonicalContextState();
	if (hadState) notifyWindowTaskStateChanged();
	return cancelledCount;
}

void MRFileEditor::clearCanonicalFoldContextTask(std::uint64_t expectedTaskId) noexcept {
	if (expectedTaskId == 0) return;
	bool owned = false;
	for (const FoldCanonicalPacketState &packet : mFoldCanonicalContextState.packets)
		if (packet.taskId == expectedTaskId) {
			owned = true;
			break;
		}
	if (!owned) return;
	for (FoldCanonicalPacketState &packet : mFoldCanonicalContextState.packets) {
		if (packet.taskId == 0) continue;
		packet.taskId = 0;
		if (packet.stage == FoldCanonicalPacketStage::Validating) {
			packet.stage = FoldCanonicalPacketStage::Acquired;
		} else {
			packet.stage = FoldCanonicalPacketStage::AwaitingAcquisition;
			packet.lineTexts.reset();
		}
	}
	mFoldCanonicalContextState.failureLatched = true;
	notifyWindowTaskStateChanged();
}

bool MRFileEditor::ownsCanonicalFoldContextTask(std::uint64_t taskId) const noexcept {
	if (taskId == 0) return false;
	for (const FoldCanonicalPacketState &packet : mFoldCanonicalContextState.packets)
		if (packet.taskId == taskId) return true;
	return false;
}
