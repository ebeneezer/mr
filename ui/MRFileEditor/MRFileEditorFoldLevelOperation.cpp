#include "MRFileEditor.hpp"

#include <algorithm>
#include <limits>

namespace {

constexpr std::size_t kDocumentFoldPacketLines = 65536;

bool sameFoldBlockIdentity(const MRFoldOpenBlockState &left, const MRFoldOpenBlockState &right) noexcept {
	return left.startLine == right.startLine && left.indent == right.indent && left.sourceKind == right.sourceKind && left.closer == right.closer && left.marker == right.marker &&
	       left.markerLength == right.markerLength && left.headingLevel == right.headingLevel && left.languageBlockKind == right.languageBlockKind &&
	       left.siblingContinuation == right.siblingContinuation && left.lastContentLine == right.lastContentLine && left.xmlTagName == right.xmlTagName;
}

bool foldStateSuffixOffset(const MRFoldAnalysisState &canonical, const MRFoldAnalysisState &viewport, std::size_t &offset) noexcept {
	if (canonical.openBlocks.size() < viewport.openBlocks.size()) return false;
	offset = canonical.openBlocks.size() - viewport.openBlocks.size();
	for (std::size_t index = 0; index < viewport.openBlocks.size(); ++index)
		if (!sameFoldBlockIdentity(canonical.openBlocks[offset + index], viewport.openBlocks[index])) return false;
	return true;
}

} // namespace

bool MRFileEditor::toggleDocumentFoldLevel(unsigned short level) {
	const std::size_t oldTotalLines = mBufferModel.exactLineCountKnown() ? std::max<std::size_t>(1, mBufferModel.lineCount()) : std::max<std::size_t>(1, mBufferModel.estimatedLineCount());
	const std::size_t oldTopDocumentLine = documentLineForVisibleLine(static_cast<std::size_t>(std::max(0, delta.y)));
	const bool closeActiveLevel = mFoldState.documentFoldLevelActive() && mFoldState.documentFoldLevel() == level;
	static_cast<void>(cancelDocumentFoldLevelOperation());

	if (closeActiveLevel) {
		mFoldState.clearDocumentFoldLevel();
	} else {
		std::shared_ptr<MRFoldClosedProjection> preview = std::make_shared<MRFoldClosedProjection>();
		for (const MRFoldSpan &span : mFoldState.visibleState().spans)
			if (span.level == level) preview->spans.push_back(span);
		preview->finalize();
		mFoldState.beginDocumentFoldLevel(level, preview);

		if (mFoldGenerationCounter == 0) ++mFoldGenerationCounter;
		mFoldLevelOperationState.documentId = mBufferModel.documentId();
		mFoldLevelOperationState.version = mBufferModel.version();
		mFoldLevelOperationState.language = mBufferModel.language();
		mFoldLevelOperationState.generation = mFoldGenerationCounter++;
		mFoldLevelOperationState.level = level;
		mFoldLevelOperationState.resolvedLevel = level;
		mFoldLevelOperationState.projectionTargetBottomLine = oldTopDocumentLine + 1;
		const MRFoldingDerivedState::VisibleState &visibleState = mFoldState.visibleState();
		if (visibleState.documentId == mFoldLevelOperationState.documentId && visibleState.version == mFoldLevelOperationState.version &&
		    visibleState.language == mFoldLevelOperationState.language) {
			mFoldLevelOperationState.projectionTargetBottomLine = std::max(mFoldLevelOperationState.projectionTargetBottomLine, visibleState.bottomLine);
			mFoldLevelOperationState.contextAnchorLine = visibleState.topLine;
		}
		for (const FoldCheckpointState &checkpoint : mFoldWarmupState.checkpoints)
			if (checkpoint.generation == mFoldWarmupState.generation && checkpoint.line <= oldTopDocumentLine && checkpoint.line >= mFoldLevelOperationState.contextAnchorLine) {
				mFoldLevelOperationState.contextAnchorLine = checkpoint.line;
				mFoldLevelOperationState.viewportAnchorState = checkpoint.state;
			}
		mFoldLevelOperationState.levelResolved = mFoldLevelOperationState.contextAnchorLine == 0;
		mFoldLevelOperationState.waitingForLineIndex = !mBufferModel.exactLineCountKnown();
		continueDocumentFoldLevelOperationIfNeeded();
	}

	const MRFoldSpan *cursorFold = mFoldState.effectiveClosedFoldContaining(cachedCursorLineIndex());
	if (cursorFold != nullptr && cachedCursorLineIndex() > cursorFold->startLine) moveCursor(lineStartForIndex(cursorFold->startLine), false, false);
	invalidateFoldCache(true);
	updateMetrics();
	const std::size_t newTopVisibleLine = visibleLineForDocumentLine(std::min(oldTopDocumentLine, oldTotalLines - 1));
	scrollTo(std::max(0, delta.x), static_cast<int>(std::min<std::size_t>(newTopVisibleLine, static_cast<std::size_t>(INT_MAX))));
	drawView();
	updateIndicator();
	return true;
}

void MRFileEditor::continueDocumentFoldLevelOperationIfNeeded() {
	if (mFoldLevelOperationState.generation == 0) return;
	const bool sameSource = mFoldLevelOperationState.documentId == mBufferModel.documentId() && mFoldLevelOperationState.version == mBufferModel.version() &&
	                        mFoldLevelOperationState.language == mBufferModel.language();
	if (!sameSource || !foldingPipelineEnabled()) {
		static_cast<void>(cancelDocumentFoldLevelOperation());
		mFoldState.clearDocumentFoldLevel();
		return;
	}
	if (!mBufferModel.exactLineCountKnown()) {
		mFoldLevelOperationState.waitingForLineIndex = true;
		scheduleLineIndexWarmupIfNeeded();
		return;
	}
	if (mFoldLevelOperationState.totalLines == 0) {
		mFoldLevelOperationState.waitingForLineIndex = false;
		mFoldLevelOperationState.totalLines = std::max<std::size_t>(1, mBufferModel.lineCount());
		mFoldLevelOperationState.contextAnchorLine = std::min(mFoldLevelOperationState.contextAnchorLine, mFoldLevelOperationState.totalLines);
		mFoldLevelOperationState.projectionTargetBottomLine = std::min(mFoldLevelOperationState.projectionTargetBottomLine, mFoldLevelOperationState.totalLines);
		mFoldLevelOperationState.nextPacketStartLine = 0;
		mFoldLevelOperationState.nextProjectionPacketStartLine = 0;
		mFoldLevelOperationState.confirmedLine = 0;
		mFoldLevelOperationState.projectedLine = 0;
		if (!mFoldLevelOperationState.levelResolved) {
			FoldCheckpointState checkpoint;
			checkpoint.generation = mFoldLevelOperationState.generation;
			checkpoint.line = 0;
			mFoldLevelOperationState.checkpoints.push_back(std::move(checkpoint));
		}
	}
	appendDocumentFoldLevelPackets();
	submitDocumentFoldLevelPackets();
	scheduleDocumentFoldLevelProjection();
}

std::size_t MRFileEditor::cancelDocumentFoldLevelOperation() noexcept {
	std::size_t cancelledCount = 0;
	for (FoldLevelPacketState &packet : mFoldLevelOperationState.packets) {
		if (packet.taskId == 0) continue;
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(packet.taskId));
		++cancelledCount;
	}
	if (mFoldLevelOperationState.projectionTaskId != 0) {
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mFoldLevelOperationState.projectionTaskId));
		++cancelledCount;
	}
	const bool hadState = mFoldLevelOperationState.generation != 0;
	mFoldLevelOperationState = FoldLevelOperationState();
	if (hadState) notifyWindowTaskStateChanged();
	return cancelledCount;
}

void MRFileEditor::clearDocumentFoldLevelTask(std::uint64_t expectedTaskId) noexcept {
	if (expectedTaskId == 0) return;
	bool taskBelongsToOperation = mFoldLevelOperationState.projectionTaskId == expectedTaskId;
	for (const FoldLevelPacketState &packet : mFoldLevelOperationState.packets)
		if (packet.taskId == expectedTaskId) taskBelongsToOperation = true;
	if (!taskBelongsToOperation) return;
	static_cast<void>(cancelDocumentFoldLevelOperation());
	mFoldState.clearDocumentFoldLevel();
	invalidateFoldCache(true);
	updateMetrics();
	drawView();
	updateIndicator();
}

void MRFileEditor::appendDocumentFoldLevelPackets() {
	const std::size_t workerBudget = std::max<std::size_t>(1, mr::coprocessor::globalCoprocessor().allowedCoreCount());
	if (!mFoldLevelOperationState.levelResolved) {
		while (mFoldLevelOperationState.packets.size() < workerBudget && mFoldLevelOperationState.nextPacketStartLine < mFoldLevelOperationState.contextAnchorLine) {
			FoldLevelPacketState packet;
			packet.generation = mFoldLevelOperationState.generation;
			packet.startLine = mFoldLevelOperationState.nextPacketStartLine;
			packet.endLine = std::min(mFoldLevelOperationState.contextAnchorLine, packet.startLine + kDocumentFoldPacketLines);
			mFoldLevelOperationState.packets.push_back(packet);
			mFoldLevelOperationState.nextPacketStartLine = packet.endLine;
		}
		return;
	}

	std::size_t forwardPacketCount = 0;
	std::size_t projectionPacketCount = 0;
	for (const FoldLevelPacketState &packet : mFoldLevelOperationState.packets) {
		if (packet.projectionOnly) ++projectionPacketCount;
		else
			++forwardPacketCount;
	}
	const bool prefixProjectionPending = mFoldLevelOperationState.nextProjectionPacketStartLine < mFoldLevelOperationState.contextAnchorLine;
	const std::size_t forwardPacketBudget = prefixProjectionPending ? (workerBudget > 1 ? (workerBudget + 1) / 2 : 0) : workerBudget;
	while (mFoldLevelOperationState.packets.size() < workerBudget && forwardPacketCount < forwardPacketBudget &&
	       mFoldLevelOperationState.nextPacketStartLine < mFoldLevelOperationState.totalLines) {
		FoldLevelPacketState packet;
		packet.generation = mFoldLevelOperationState.generation;
		packet.startLine = mFoldLevelOperationState.nextPacketStartLine;
		packet.endLine = std::min(mFoldLevelOperationState.totalLines, packet.startLine + kDocumentFoldPacketLines);
		mFoldLevelOperationState.packets.push_back(packet);
		mFoldLevelOperationState.nextPacketStartLine = packet.endLine;
		++forwardPacketCount;
	}
	const std::size_t projectionPacketBudget = workerBudget - forwardPacketBudget;
	while (mFoldLevelOperationState.packets.size() < workerBudget && projectionPacketCount < projectionPacketBudget &&
	       mFoldLevelOperationState.nextProjectionPacketStartLine < mFoldLevelOperationState.contextAnchorLine) {
		std::vector<FoldCheckpointState>::iterator checkpoint = mFoldLevelOperationState.checkpoints.begin();
		while (checkpoint != mFoldLevelOperationState.checkpoints.end() && checkpoint->line != mFoldLevelOperationState.nextProjectionPacketStartLine)
			++checkpoint;
		if (checkpoint == mFoldLevelOperationState.checkpoints.end()) break;
		FoldLevelPacketState packet;
		packet.generation = mFoldLevelOperationState.generation;
		packet.startLine = mFoldLevelOperationState.nextProjectionPacketStartLine;
		packet.endLine = std::min(mFoldLevelOperationState.contextAnchorLine, packet.startLine + kDocumentFoldPacketLines);
		packet.projectionOnly = true;
		packet.confirmedInputState = std::move(checkpoint->state);
		mFoldLevelOperationState.checkpoints.erase(checkpoint);
		mFoldLevelOperationState.packets.push_back(std::move(packet));
		mFoldLevelOperationState.nextProjectionPacketStartLine = mFoldLevelOperationState.packets.back().endLine;
		++projectionPacketCount;
	}
}

void MRFileEditor::submitDocumentFoldLevelAcquisition(FoldLevelPacketState &packet, const MRTextBufferModel::ReadSnapshot &snapshot) {
	if (packet.taskId != 0 || packet.stage != FoldLevelPacketStage::AwaitingAcquisition || packet.endLine <= packet.startLine) return;
	const std::size_t documentId = mFoldLevelOperationState.documentId;
	const std::size_t version = mFoldLevelOperationState.version;
	const MRSyntaxLanguage language = mFoldLevelOperationState.language;
	const std::uint64_t generation = packet.generation;
	const std::size_t startLine = packet.startLine;
	const std::size_t endLine = packet.endLine;
	const std::size_t totalLines = mFoldLevelOperationState.totalLines;
	const std::string label = std::string(foldWarmupTaskLabel()) + " acquire EOF lines " + std::to_string(startLine + 1) + "-" + std::to_string(endLine);

	packet.stage = FoldLevelPacketStage::Acquiring;
	packet.taskId = mr::coprocessor::globalCoprocessor().submitPacket(
	    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FoldWarmup, documentId, version, mExecutionOwnerKind, mExecutionOwnerLocalId, generation,
	    mr::coprocessor::WorkDirection::Eof, startLine, endLine, label, [snapshot, language, generation, startLine, endLine, totalLines](const mr::coprocessor::TaskInfo &info) {
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
}

void MRFileEditor::submitDocumentFoldLevelValidation(FoldLevelPacketState &packet) {
	if (packet.taskId != 0 || packet.stage != FoldLevelPacketStage::Acquired || packet.lineTexts == nullptr) return;
	if (!packet.projectionOnly && packet.startLine != mFoldLevelOperationState.confirmedLine) return;
	const std::size_t documentId = mFoldLevelOperationState.documentId;
	const std::size_t version = mFoldLevelOperationState.version;
	const MRSyntaxLanguage language = mFoldLevelOperationState.language;
	const std::uint64_t generation = packet.generation;
	const std::size_t startLine = packet.startLine;
	const std::size_t endLine = packet.endLine;
	const std::size_t totalLines = mFoldLevelOperationState.totalLines;
	const bool retainFoldSpans = mFoldLevelOperationState.levelResolved;
	const unsigned short retainedFoldLevel = mFoldLevelOperationState.resolvedLevel;
	const MRFoldAnalysisState inputState = packet.projectionOnly ? packet.confirmedInputState : mFoldLevelOperationState.confirmedState;
	const std::shared_ptr<const std::vector<std::string>> lineTexts = packet.lineTexts;
	const std::string label = std::string(foldWarmupTaskLabel()) + (packet.projectionOnly ? " project EOF lines " : " validate EOF lines ") + std::to_string(startLine + 1) + "-" +
	                          std::to_string(endLine);

	packet.stage = FoldLevelPacketStage::Validating;
	packet.taskId = mr::coprocessor::globalCoprocessor().submitPacket(
	    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FoldWarmup, documentId, version, mExecutionOwnerKind, mExecutionOwnerLocalId, generation,
	    mr::coprocessor::WorkDirection::Eof, startLine, endLine, label,
	    [lineTexts, language, generation, startLine, endLine, totalLines, inputState, retainFoldSpans, retainedFoldLevel](const mr::coprocessor::TaskInfo &info) {
		    mr::coprocessor::Result result;
		    result.task = info;
		    if (info.cancelRequested()) {
			    result.status = mr::coprocessor::TaskStatus::Cancelled;
			    return result;
		    }
		    result.payload = buildFoldValidationPayload(lineTexts, language, generation, startLine, endLine, totalLines, inputState, retainFoldSpans, retainedFoldLevel, info.cancelFlag);
		    result.status = result.payload != nullptr ? mr::coprocessor::TaskStatus::Completed : mr::coprocessor::TaskStatus::Cancelled;
		    return result;
	    });
}

void MRFileEditor::submitDocumentFoldLevelPackets() {
	const std::size_t workerBudget = std::max<std::size_t>(1, mr::coprocessor::globalCoprocessor().allowedCoreCount());
	std::size_t runningCount = 0;
	for (const FoldLevelPacketState &packet : mFoldLevelOperationState.packets)
		if (packet.taskId != 0) ++runningCount;
	if (runningCount >= workerBudget) return;
	for (FoldLevelPacketState &packet : mFoldLevelOperationState.packets) {
		if (runningCount >= workerBudget) break;
		if (packet.projectionOnly || packet.stage != FoldLevelPacketStage::Acquired || packet.startLine != mFoldLevelOperationState.confirmedLine) continue;
		submitDocumentFoldLevelValidation(packet);
		if (packet.taskId != 0) ++runningCount;
		break;
	}
	for (FoldLevelPacketState &packet : mFoldLevelOperationState.packets) {
		if (runningCount >= workerBudget) break;
		if (!packet.projectionOnly || packet.stage != FoldLevelPacketStage::Acquired) continue;
		submitDocumentFoldLevelValidation(packet);
		if (packet.taskId != 0) ++runningCount;
	}
	if (runningCount >= workerBudget) {
		notifyWindowTaskStateChanged();
		return;
	}
	const MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
	for (FoldLevelPacketState &packet : mFoldLevelOperationState.packets) {
		if (runningCount >= workerBudget) break;
		if (packet.stage != FoldLevelPacketStage::AwaitingAcquisition) continue;
		submitDocumentFoldLevelAcquisition(packet, snapshot);
		if (packet.taskId != 0) ++runningCount;
	}
	if (runningCount != 0) notifyWindowTaskStateChanged();
}

bool MRFileEditor::resolveDocumentFoldLevelTarget() {
	std::size_t levelOffset = 0;
	if (!foldStateSuffixOffset(mFoldLevelOperationState.confirmedState, mFoldLevelOperationState.viewportAnchorState, levelOffset)) return false;
	if (levelOffset > static_cast<std::size_t>(std::numeric_limits<unsigned short>::max() - mFoldLevelOperationState.level)) return false;
	mFoldLevelOperationState.resolvedLevel = static_cast<unsigned short>(mFoldLevelOperationState.level + levelOffset);
	mFoldLevelOperationState.levelResolved = true;
	mFoldLevelOperationState.nextPacketStartLine = mFoldLevelOperationState.contextAnchorLine;
	mFoldLevelOperationState.nextProjectionPacketStartLine = 0;
	mFoldLevelOperationState.projectedLine = 0;
	mFoldLevelOperationState.packets.clear();
	mFoldLevelOperationState.segments.clear();
	return true;
}

void MRFileEditor::scheduleDocumentFoldLevelProjection() {
	if (mFoldLevelOperationState.generation == 0 || !mFoldLevelOperationState.levelResolved || mFoldLevelOperationState.projectionTaskId != 0) return;
	std::size_t projectedEndLine = mFoldLevelOperationState.projectedLine;
	std::size_t segmentCount = 0;
	for (const std::shared_ptr<const FoldValidatedSegment> &segment : mFoldLevelOperationState.segments) {
		if (segment == nullptr || segment->startLine != projectedEndLine || segment->endLine <= segment->startLine) break;
		projectedEndLine = segment->endLine;
		++segmentCount;
	}
	if (segmentCount == 0) return;
	const bool analysisComplete = mFoldLevelOperationState.confirmedLine >= mFoldLevelOperationState.totalLines &&
	                              mFoldLevelOperationState.nextPacketStartLine >= mFoldLevelOperationState.totalLines &&
	                              mFoldLevelOperationState.nextProjectionPacketStartLine >= mFoldLevelOperationState.contextAnchorLine && mFoldLevelOperationState.packets.empty();
	const bool complete = analysisComplete && projectedEndLine >= mFoldLevelOperationState.totalLines;
	const bool visibleTargetReady = mFoldLevelOperationState.projectionTargetBottomLine > mFoldLevelOperationState.projectedLine &&
	                                projectedEndLine >= mFoldLevelOperationState.projectionTargetBottomLine;
	if (!complete && !visibleTargetReady) return;
	const std::size_t documentId = mFoldLevelOperationState.documentId;
	const std::size_t version = mFoldLevelOperationState.version;
	const std::uint64_t generation = mFoldLevelOperationState.generation;
	const unsigned short level = mFoldLevelOperationState.level;
	const std::size_t projectedStartLine = mFoldLevelOperationState.projectedLine;
	const std::shared_ptr<std::vector<std::shared_ptr<const FoldValidatedSegment>>> segments = std::make_shared<std::vector<std::shared_ptr<const FoldValidatedSegment>>>(
	    mFoldLevelOperationState.segments.begin(), mFoldLevelOperationState.segments.begin() + static_cast<std::ptrdiff_t>(segmentCount));
	const std::string label = std::string(foldWarmupTaskLabel()) + (complete ? " final project level " : " visible project level ") + std::to_string(level);

	mFoldLevelOperationState.projectionTaskId = mr::coprocessor::globalCoprocessor().submitPacket(
	    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FoldWarmup, documentId, version, mExecutionOwnerKind, mExecutionOwnerLocalId, generation,
	    mr::coprocessor::WorkDirection::Eof, projectedStartLine, projectedEndLine, label,
	    [segments, generation, level, segmentCount, complete](const mr::coprocessor::TaskInfo &info) {
		    mr::coprocessor::Result result;
		    result.task = info;
		    if (info.cancelRequested()) {
			    result.status = mr::coprocessor::TaskStatus::Cancelled;
			    return result;
		    }
		    std::shared_ptr<MRFoldClosedProjection> projection = std::make_shared<MRFoldClosedProjection>();
		    std::size_t spanCount = 0;
		    for (const std::shared_ptr<const FoldValidatedSegment> &segment : *segments)
			    if (segment != nullptr) spanCount += segment->spans.size();
		    projection->spans.reserve(spanCount);
		    for (const std::shared_ptr<const FoldValidatedSegment> &segment : *segments) {
			    if (info.cancelRequested()) {
				    result.status = mr::coprocessor::TaskStatus::Cancelled;
				    return result;
			    }
			    if (segment != nullptr) projection->spans.insert(projection->spans.end(), segment->spans.begin(), segment->spans.end());
		    }
		    projection->finalize();
		    result.payload = std::make_shared<FoldLevelProjectionPayload>(generation, level, segmentCount, complete, projection);
		    result.status = mr::coprocessor::TaskStatus::Completed;
		    return result;
	    });
	notifyWindowTaskStateChanged();
}

bool MRFileEditor::applyDocumentFoldLevelProjection(const FoldLevelProjectionPayload &payload, const mr::coprocessor::Result &result) {
	if (mFoldLevelOperationState.projectionTaskId != result.task.id || mFoldLevelOperationState.generation != payload.generation || mFoldLevelOperationState.level != payload.level ||
	    mFoldLevelOperationState.documentId != mBufferModel.documentId() || mFoldLevelOperationState.version != mBufferModel.version() || payload.projection == nullptr ||
	    payload.segmentCount > mFoldLevelOperationState.segments.size() || !result.task.hasPacketSpan || result.task.packetStart != mFoldLevelOperationState.projectedLine)
		return false;
	const std::size_t oldTopDocumentLine = documentLineForVisibleLine(static_cast<std::size_t>(std::max(0, delta.y)));
	mFoldState.adoptDocumentFoldLevelProjection(payload.level, result.task.packetEnd, payload.complete, payload.projection);
	if (payload.complete) {
		mFoldLevelOperationState = FoldLevelOperationState();
	} else {
		mFoldLevelOperationState.projectionTaskId = 0;
		mFoldLevelOperationState.projectedLine = result.task.packetEnd;
		mFoldLevelOperationState.segments.erase(mFoldLevelOperationState.segments.begin(),
		                                             mFoldLevelOperationState.segments.begin() + static_cast<std::ptrdiff_t>(payload.segmentCount));
	}
	const MRFoldSpan *cursorFold = mFoldState.effectiveClosedFoldContaining(cachedCursorLineIndex());
	if (cursorFold != nullptr && cachedCursorLineIndex() > cursorFold->startLine) moveCursor(lineStartForIndex(cursorFold->startLine), false, false);
	updateMetrics();
	const std::size_t newTopVisibleLine = visibleLineForDocumentLine(oldTopDocumentLine);
	scrollTo(std::max(0, delta.x), static_cast<int>(std::min<std::size_t>(newTopVisibleLine, static_cast<std::size_t>(INT_MAX))));
	drawView();
	updateIndicator();
	notifyWindowTaskStateChanged();
	if (!payload.complete) continueDocumentFoldLevelOperationIfNeeded();
	return true;
}
