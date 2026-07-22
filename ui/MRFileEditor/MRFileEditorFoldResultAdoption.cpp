#include "MRFoldWarmupPayload.hpp"

#include <algorithm>
#include <limits>

bool MRFileEditor::publishCurrentFoldProjection() {
	const bool documentFoldLevelActive = mFoldState.documentFoldLevelActive();
	const std::size_t oldTopDocumentLine = documentFoldLevelActive ? documentLineForVisibleLine(static_cast<std::size_t>(std::max(0, delta.y))) : 0;
	std::vector<const FoldValidatedSegment *> ordered;
	for (const FoldValidatedSegment &segment : mFoldWarmupState.segments)
		if (segment.generation == mFoldWarmupState.generation) ordered.push_back(&segment);
	std::sort(ordered.begin(), ordered.end(), [](const FoldValidatedSegment *left, const FoldValidatedSegment *right) { return left->startLine < right->startLine; });
	std::size_t reachedLine = mFoldWarmupState.scanTopLine;
	std::size_t segmentCount = 0;
	for (const FoldValidatedSegment *segment : ordered) {
		if (segment->startLine != reachedLine || segment->endLine <= segment->startLine) break;
		reachedLine = segment->endLine;
		++segmentCount;
	}
	if (reachedLine < mFoldWarmupState.visibleBottomLine || segmentCount == 0) return false;

	MRFoldingDerivedState::VisibleState &visibleState = mFoldState.visibleState();
	visibleState.spans.clear();
	visibleState.branches.clear();
	visibleState.lineTexts.clear();
	for (std::size_t index = 0; index < segmentCount; ++index) {
		const FoldValidatedSegment &segment = *ordered[index];
		visibleState.lineTexts.insert(visibleState.lineTexts.end(), segment.lineTexts.begin(), segment.lineTexts.end());
		visibleState.spans.insert(visibleState.spans.end(), segment.spans.begin(), segment.spans.end());
		visibleState.branches.insert(visibleState.branches.end(), segment.branches.begin(), segment.branches.end());
	}
	++visibleState.revision;
	if (visibleState.revision == 0) ++visibleState.revision;
	visibleState.documentId = mFoldWarmupState.documentId;
	visibleState.version = mFoldWarmupState.version;
	visibleState.topLine = mFoldWarmupState.scanTopLine;
	visibleState.bottomLine = reachedLine;
	visibleState.language = mFoldWarmupState.language;
	const bool documentProjectionChanged = mFoldState.refreshDocumentFoldLevelViewportProjection();
	visibleState.displayLevels.clear();
	int visibleMaxLevel = 1;
	for (const MRFoldSpan &span : visibleState.spans)
		if (!(span.endLine < mFoldWarmupState.visibleTopLine || span.startLine >= mFoldWarmupState.visibleBottomLine))
			visibleMaxLevel = std::max(visibleMaxLevel, static_cast<int>(span.level) + 1);
	for (const MRFoldGutterBranch &branch : visibleState.branches)
		if (branch.line >= mFoldWarmupState.visibleTopLine && branch.line < mFoldWarmupState.visibleBottomLine)
			visibleMaxLevel = std::max(visibleMaxLevel, static_cast<int>(branch.level) + 1);
	visibleState.gutterColumns = visibleMaxLevel;

	std::map<std::size_t, MRFoldSpan> &closedFoldSpans = mFoldState.closedFoldSpans();
	const std::size_t cursorLine = closedFoldSpans.empty() ? 0 : cachedCursorLineIndex();
	std::size_t foldCursorTarget = 0;
	bool foldCursorTargetValid = false;
	for (std::map<std::size_t, MRFoldSpan>::iterator closed = closedFoldSpans.begin(); closed != closedFoldSpans.end();) {
		if (closed->first < visibleState.topLine || closed->first >= visibleState.bottomLine) {
			++closed;
			continue;
		}

		const MRFoldSpan *validatedSpan = nullptr;
		for (const MRFoldSpan &span : visibleState.spans)
			if (span.startLine == closed->first) {
				validatedSpan = &span;
				break;
			}
		if (validatedSpan == nullptr) {
			closed = closedFoldSpans.erase(closed);
			continue;
		}
		closed->second = MRFoldSpan(validatedSpan->startLine, validatedSpan->endLine, validatedSpan->level, validatedSpan->sourceKind, false, validatedSpan->siblingContinuation);
		++closed;
	}
	mFoldState.rebuildEffectiveClosedFolds();
	const MRFoldSpan *cursorFold = mFoldState.effectiveClosedFoldContaining(cursorLine);
	if (cursorFold != nullptr && cursorLine > cursorFold->startLine) {
		foldCursorTarget = cursorFold->startLine;
		foldCursorTargetValid = true;
	}
	if (foldCursorTargetValid) moveCursor(lineStartForIndex(foldCursorTarget), false, false);
	if (documentProjectionChanged) {
		updateMetrics();
		const std::size_t newTopVisibleLine = visibleLineForDocumentLine(oldTopDocumentLine);
		scrollTo(std::max(0, delta.x), static_cast<int>(std::min<std::size_t>(newTopVisibleLine, static_cast<std::size_t>(INT_MAX))));
		drawView();
		updateIndicator();
	} else if (!foldCursorTargetValid) {
		drawView();
	}
	return true;
}

bool MRFileEditor::adoptReadyFoldPackets() {
	bool adoptedAny = false;
	bool progressed = true;
	while (progressed) {
		progressed = false;
		for (std::size_t packetIndex = 0; packetIndex < mFoldWarmupState.packets.size(); ++packetIndex) {
			FoldPacketState &packet = mFoldWarmupState.packets[packetIndex];
			if (!packet.resultReady) continue;
			MRFoldAnalysisState confirmedState;
			if (!foldConfirmedStateForPacket(packet, confirmedState)) continue;
			if (confirmedState != packet.inputState) {
				mr::coprocessor::globalCoprocessor().resolveDeferredResultAdoption(packet.resultLifecycle, false);
				packet.inputState = confirmedState;
				packet.inputStateConfirmed = true;
				packet.resultReady = false;
				packet.outputState = MRFoldAnalysisState();
				packet.lineTexts.clear();
				packet.spans.clear();
				packet.branches.clear();
				progressed = true;
				continue;
			}

			if (!packet.contextOnly) {
				FoldValidatedSegment segment;
				segment.generation = packet.generation;
				segment.startLine = packet.startLine;
				segment.endLine = packet.endLine;
				segment.lineTexts = std::move(packet.lineTexts);
				segment.spans = std::move(packet.spans);
				segment.branches = std::move(packet.branches);
				mFoldWarmupState.segments.push_back(std::move(segment));
			}
			FoldCheckpointState checkpoint;
			checkpoint.generation = packet.generation;
			checkpoint.line = packet.endLine;
			checkpoint.state = std::move(packet.outputState);
			mFoldWarmupState.checkpoints.push_back(std::move(checkpoint));
			mr::coprocessor::globalCoprocessor().resolveDeferredResultAdoption(packet.resultLifecycle, true);
			mFoldWarmupState.packets.erase(mFoldWarmupState.packets.begin() + static_cast<std::ptrdiff_t>(packetIndex));
			adoptedAny = true;
			progressed = true;
			break;
		}
	}
	if (adoptedAny) static_cast<void>(publishCurrentFoldProjection());
	return adoptedAny;
}

bool MRFileEditor::applyFoldWarmup(const mr::coprocessor::Payload &payload, const mr::coprocessor::Result &result) {
	if (ownsCanonicalFoldContextTask(result.task.id)) return applyCanonicalFoldContext(payload, result);
	const FoldLevelProjectionPayload *projection = dynamic_cast<const FoldLevelProjectionPayload *>(&payload);
	if (projection != nullptr) return applyDocumentFoldLevelProjection(*projection, result);
	const FoldLineAcquisitionPayload *acquisition = dynamic_cast<const FoldLineAcquisitionPayload *>(&payload);
	if (acquisition != nullptr) {
		if (result.task.id == 0 || acquisition->lineTexts == nullptr || mFoldLevelOperationState.documentId != mBufferModel.documentId() ||
		    mFoldLevelOperationState.version != result.task.baseVersion || mFoldLevelOperationState.language != acquisition->language || mBufferModel.version() != result.task.baseVersion ||
		    mBufferModel.language() != acquisition->language)
			return false;
		for (FoldLevelPacketState &packet : mFoldLevelOperationState.packets) {
			if (packet.taskId != result.task.id) continue;
			if (packet.stage != FoldLevelPacketStage::Acquiring || packet.generation != acquisition->generation || packet.startLine != acquisition->startLine ||
			    packet.endLine != acquisition->endLine || acquisition->lineTexts->size() < packet.endLine - packet.startLine)
				return false;
			packet.taskId = 0;
			packet.stage = FoldLevelPacketStage::Acquired;
			packet.lineTexts = acquisition->lineTexts;
			submitDocumentFoldLevelPackets();
			notifyWindowTaskStateChanged();
			return true;
		}
		return false;
	}
	const MRFoldWarmupPayload *foldWarmup = dynamic_cast<const MRFoldWarmupPayload *>(&payload);
	if (foldWarmup == nullptr || result.task.id == 0) return false;
	if (mFoldLevelOperationState.documentId == mBufferModel.documentId() && mFoldLevelOperationState.version == result.task.baseVersion &&
	    mFoldLevelOperationState.language == foldWarmup->language && mBufferModel.version() == result.task.baseVersion && mBufferModel.language() == foldWarmup->language) {
		for (std::size_t packetIndex = 0; packetIndex < mFoldLevelOperationState.packets.size(); ++packetIndex) {
			FoldLevelPacketState &packet = mFoldLevelOperationState.packets[packetIndex];
			if (packet.taskId != result.task.id) continue;
			const MRFoldAnalysisState &expectedInputState = packet.projectionOnly ? packet.confirmedInputState : mFoldLevelOperationState.confirmedState;
			if (packet.stage != FoldLevelPacketStage::Validating || packet.generation != foldWarmup->generation || foldWarmup->direction != mr::coprocessor::WorkDirection::Eof ||
			    packet.startLine != foldWarmup->startLine || packet.endLine != foldWarmup->endLine || (!packet.projectionOnly && packet.startLine != mFoldLevelOperationState.confirmedLine) ||
			    foldWarmup->stateIn != expectedInputState)
				return false;
			if (mFoldLevelOperationState.levelResolved) {
				std::shared_ptr<FoldValidatedSegment> segment = std::make_shared<FoldValidatedSegment>();
				segment->generation = packet.generation;
				segment->startLine = packet.startLine;
				segment->endLine = packet.endLine;
				segment->spans = foldWarmup->spans;
				std::vector<std::shared_ptr<const FoldValidatedSegment>>::iterator insertion = std::lower_bound(
				    mFoldLevelOperationState.segments.begin(), mFoldLevelOperationState.segments.end(), segment->startLine,
				    [](const std::shared_ptr<const FoldValidatedSegment> &existing, std::size_t startLine) { return existing != nullptr && existing->startLine < startLine; });
				mFoldLevelOperationState.segments.insert(insertion, segment);
			}
			if (!packet.projectionOnly) {
				mFoldLevelOperationState.confirmedLine = packet.endLine;
				mFoldLevelOperationState.confirmedState = foldWarmup->stateOut;
				if (!mFoldLevelOperationState.levelResolved) {
					FoldCheckpointState checkpoint;
					checkpoint.generation = packet.generation;
					checkpoint.line = packet.endLine;
					checkpoint.state = foldWarmup->stateOut;
					mFoldLevelOperationState.checkpoints.push_back(std::move(checkpoint));
				}
			}
			mFoldLevelOperationState.packets.erase(mFoldLevelOperationState.packets.begin() + static_cast<std::ptrdiff_t>(packetIndex));
			if (!mFoldLevelOperationState.levelResolved && mFoldLevelOperationState.confirmedLine >= mFoldLevelOperationState.contextAnchorLine) {
				if (!resolveDocumentFoldLevelTarget()) {
					const std::size_t oldTopDocumentLine = documentLineForVisibleLine(static_cast<std::size_t>(std::max(0, delta.y)));
					mFoldLevelOperationState = FoldLevelOperationState();
					mFoldState.clearDocumentFoldLevel();
					updateMetrics();
					const std::size_t newTopVisibleLine = visibleLineForDocumentLine(oldTopDocumentLine);
					scrollTo(std::max(0, delta.x), static_cast<int>(std::min<std::size_t>(newTopVisibleLine, static_cast<std::size_t>(INT_MAX))));
					drawView();
					updateIndicator();
					notifyWindowTaskStateChanged();
					return true;
				}
			}
			appendDocumentFoldLevelPackets();
			submitDocumentFoldLevelPackets();
			scheduleDocumentFoldLevelProjection();
			notifyWindowTaskStateChanged();
			return true;
		}
	}
	if (mFoldWarmupState.documentId != mBufferModel.documentId() || mFoldWarmupState.version != result.task.baseVersion || mBufferModel.version() != result.task.baseVersion ||
	    mFoldWarmupState.language != foldWarmup->language || mBufferModel.language() != foldWarmup->language)
		return false;
	for (FoldPacketState &packet : mFoldWarmupState.packets) {
		if (packet.taskId != result.task.id) continue;
		if (packet.generation != foldWarmup->generation || packet.direction != foldWarmup->direction || packet.startLine != foldWarmup->startLine || packet.endLine != foldWarmup->endLine)
			return false;
		packet.taskId = 0;
		packet.inputState = foldWarmup->stateIn;
		packet.outputState = foldWarmup->stateOut;
		packet.lineTexts = foldWarmup->lineTexts;
		packet.spans = foldWarmup->spans;
		packet.branches = foldWarmup->branches;
		packet.resultReady = true;
		packet.resultLifecycle = mr::coprocessor::globalCoprocessor().acceptResultForDeferredAdoption(result);
		static_cast<void>(adoptReadyFoldPackets());
		const MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
		const bool documentEndKnown = mBufferModel.exactLineCountKnown();
		const std::size_t totalLines = documentEndKnown ? std::max<std::size_t>(1, mBufferModel.lineCount()) : mFoldWarmupState.scanBottomLine + 1;
		submitFoldPackets(snapshot, totalLines, documentEndKnown);
		notifyWindowTaskStateChanged();
		return true;
	}
	return false;
}

std::size_t MRFileEditor::cancelViewportFoldWarmup() noexcept {
	std::size_t cancelledCount = 0;
	mFoldCanonicalContextState.requestValid = false;
	for (FoldPacketState &packet : mFoldWarmupState.packets) {
		if (packet.resultLifecycle.valid) mr::coprocessor::globalCoprocessor().resolveDeferredResultAdoption(packet.resultLifecycle, false);
		if (packet.taskId == 0) continue;
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(packet.taskId));
		++cancelledCount;
	}
	const bool hadState = !mFoldWarmupState.packets.empty() || !mFoldWarmupState.checkpoints.empty() || !mFoldWarmupState.segments.empty();
	mFoldWarmupState = FoldWarmupState();
	if (hadState) notifyWindowTaskStateChanged();
	return cancelledCount;
}

void MRFileEditor::supersedeViewportFoldWarmup() noexcept {
	for (FoldPacketState &packet : mFoldWarmupState.packets)
		if (packet.resultLifecycle.valid) mr::coprocessor::globalCoprocessor().resolveDeferredResultAdoption(packet.resultLifecycle, false);
	const bool hadState = mFoldWarmupState.generation != 0 || !mFoldWarmupState.packets.empty() || !mFoldWarmupState.checkpoints.empty() || !mFoldWarmupState.segments.empty();
	mFoldWarmupState = FoldWarmupState();
	if (hadState) notifyWindowTaskStateChanged();
}

std::size_t MRFileEditor::cancelFoldWarmup() noexcept {
	return cancelViewportFoldWarmup() + cancelCanonicalFoldContext() + cancelDocumentFoldLevelOperation();
}

void MRFileEditor::clearFoldWarmupTask(std::uint64_t expectedTaskId) {
	if (expectedTaskId == 0) {
		static_cast<void>(cancelFoldWarmup());
		return;
	}
	if (ownsCanonicalFoldContextTask(expectedTaskId)) {
		clearCanonicalFoldContextTask(expectedTaskId);
		return;
	}
	for (std::size_t index = 0; index < mFoldWarmupState.packets.size(); ++index) {
		FoldPacketState &packet = mFoldWarmupState.packets[index];
		if (packet.taskId != expectedTaskId) continue;
		for (FoldPacketState &ownedPacket : mFoldWarmupState.packets)
			if (ownedPacket.resultLifecycle.valid) mr::coprocessor::globalCoprocessor().resolveDeferredResultAdoption(ownedPacket.resultLifecycle, false);
		mFoldWarmupState.packets.clear();
		mFoldWarmupState.checkpoints.clear();
		mFoldWarmupState.segments.clear();
		mFoldWarmupState.failureLatched = true;
		notifyWindowTaskStateChanged();
		return;
	}
	clearDocumentFoldLevelTask(expectedTaskId);
}
