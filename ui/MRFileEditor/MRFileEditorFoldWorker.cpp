#include "MRFileEditor.hpp"

#include <algorithm>

namespace {

constexpr std::size_t kFoldTargetPacketLines = 256;
constexpr std::size_t kFoldRetainedLineBudget = 8192;

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
	const std::size_t visibleTop = mFoldWarmupState.visibleTopLine;
	const std::size_t visibleBottom = std::max(visibleTop + 1, mFoldWarmupState.visibleBottomLine);
	const std::size_t focusLine = std::clamp(cachedCursorLineIndex(), visibleTop, visibleBottom - 1);
	auto distanceFromFocus = [focusLine](const FoldPacketState &packet) noexcept {
		if (packet.startLine <= focusLine && focusLine < packet.endLine) return std::size_t(0);
		if (packet.endLine <= focusLine) return focusLine - packet.endLine + 1;
		return packet.startLine - focusLine;
	};
	std::stable_sort(mFoldWarmupState.packets.begin(), mFoldWarmupState.packets.end(), [&](const FoldPacketState &left, const FoldPacketState &right) {
		if (left.contextOnly != right.contextOnly) return left.contextOnly;
		return distanceFromFocus(left) < distanceFromFocus(right);
	});
	std::size_t runningCount = 0;
	bool submittedAny = false;
	for (const FoldPacketState &packet : mFoldWarmupState.packets)
		if (packet.taskId != 0) ++runningCount;
	for (FoldPacketState &packet : mFoldWarmupState.packets) {
		if (runningCount >= workerBudget) break;
		if (packet.generation != mFoldWarmupState.generation || packet.taskId != 0 || packet.resultReady) continue;
		packet.direction = packet.contextOnly || packet.endLine <= focusLine ? mr::coprocessor::WorkDirection::Bof : mr::coprocessor::WorkDirection::Eof;
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
	const bool currentGenerationCoversRequest = mFoldWarmupState.generation != 0 && scanTopLine >= mFoldWarmupState.scanTopLine && scanBottomLine <= mFoldWarmupState.scanBottomLine;
	const bool exactGenerationRequest = scanTopLine == mFoldWarmupState.scanTopLine && scanBottomLine == mFoldWarmupState.scanBottomLine &&
	                                    topLine == mFoldWarmupState.visibleTopLine && requestBottomLine == mFoldWarmupState.visibleBottomLine;
	if (currentGenerationCoversRequest && (!mFoldWarmupState.failureLatched || exactGenerationRequest)) {
		if (!mFoldWarmupState.failureLatched) {
			if (topLine < mFoldWarmupState.visibleTopLine || requestBottomLine > mFoldWarmupState.visibleBottomLine) {
				mFoldWarmupState.visibleTopLine = topLine;
				mFoldWarmupState.visibleBottomLine = requestBottomLine;
			}
			static_cast<void>(publishCurrentFoldProjection(false));
			const MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
			submitFoldPackets(snapshot, totalLines, documentEndKnown);
		}
		return;
	}

	const std::size_t oldScanTop = mFoldWarmupState.scanTopLine;
	const std::size_t oldScanBottom = mFoldWarmupState.scanBottomLine;
	const bool retainCurrent = mFoldWarmupState.generation != 0 && !mFoldWarmupState.failureLatched &&
	                           std::max(scanBottomLine, oldScanBottom) - std::min(scanTopLine, oldScanTop) <=
	                               std::max(kFoldRetainedLineBudget, scanBottomLine - scanTopLine);
	const std::size_t newScanTop = retainCurrent ? std::min(scanTopLine, oldScanTop) : scanTopLine;
	const std::size_t newScanBottom = retainCurrent ? std::max(scanBottomLine, oldScanBottom) : scanBottomLine;
	std::size_t anchorLine = 0;
	MRFoldAnalysisState anchorState;
	if (!retainCurrent || newScanTop < oldScanTop) {
		if (!canonicalFoldContextForViewport(newScanTop, newScanBottom, topLine, requestBottomLine, language, anchorLine, anchorState)) {
			notifyWindowTaskStateChanged();
			return;
		}
	}
	if (!retainCurrent) {
		supersedeViewportFoldWarmup();
		mFoldWarmupState.documentId = documentId;
		mFoldWarmupState.version = version;
		mFoldWarmupState.language = language;
		if (mFoldGenerationCounter == 0) ++mFoldGenerationCounter;
		mFoldWarmupState.generation = mFoldGenerationCounter++;
	}
	mFoldWarmupState.scanTopLine = newScanTop;
	mFoldWarmupState.scanBottomLine = newScanBottom;
	mFoldWarmupState.visibleTopLine = topLine;
	mFoldWarmupState.visibleBottomLine = requestBottomLine;
	if (!retainCurrent || newScanTop < oldScanTop) {
		mFoldCanonicalContextState.requestValid = false;
		FoldCheckpointState anchor;
		anchor.generation = mFoldWarmupState.generation;
		anchor.line = anchorLine;
		anchor.state = anchorState;
		mFoldWarmupState.checkpoints.push_back(std::move(anchor));
		if (anchorLine < newScanTop) {
			FoldPacketState bridge;
			bridge.generation = mFoldWarmupState.generation;
			bridge.direction = mr::coprocessor::WorkDirection::Bof;
			bridge.startLine = anchorLine;
			bridge.endLine = newScanTop;
			bridge.contextOnly = true;
			bridge.inputState = anchorState;
			bridge.inputStateConfirmed = true;
			mFoldWarmupState.packets.push_back(std::move(bridge));
		}
	}
	const std::size_t visibleBottom = std::max(topLine + 1, requestBottomLine);
	const std::size_t focusLine = std::clamp(cachedCursorLineIndex(), topLine, visibleBottom - 1);
	auto appendPackets = [&](std::size_t firstLine, std::size_t lastLine) {
		if (lastLine <= firstLine) return;
		std::size_t bofLine = std::clamp(focusLine, firstLine, lastLine);
		std::size_t eofLine = bofLine;
		bool preferEof = true;
		while (bofLine > firstLine || eofLine < lastLine) {
			FoldPacketState packet;
			packet.generation = mFoldWarmupState.generation;
			if ((preferEof && eofLine < lastLine) || bofLine == firstLine) {
				packet.startLine = eofLine;
				packet.endLine = eofLine + std::min(kFoldTargetPacketLines, lastLine - eofLine);
				packet.direction = mr::coprocessor::WorkDirection::Eof;
				eofLine = packet.endLine;
			} else {
				packet.endLine = bofLine;
				packet.startLine = bofLine - std::min(kFoldTargetPacketLines, bofLine - firstLine);
				packet.direction = mr::coprocessor::WorkDirection::Bof;
				bofLine = packet.startLine;
			}
			packet.inputStateConfirmed = foldConfirmedStateForPacket(packet, packet.inputState);
			mFoldWarmupState.packets.push_back(std::move(packet));
			preferEof = !preferEof;
		}
	};
	if (retainCurrent) {
		appendPackets(newScanTop, oldScanTop);
		appendPackets(oldScanBottom, newScanBottom);
	} else
		appendPackets(newScanTop, newScanBottom);
	static_cast<void>(publishCurrentFoldProjection(false));
	const MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
	submitFoldPackets(snapshot, totalLines, documentEndKnown);
}
