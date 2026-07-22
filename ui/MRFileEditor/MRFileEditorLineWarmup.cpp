#include "MRFileEditor.hpp"

#include <limits>

namespace {

constexpr std::size_t kLineIndexPacketBytes = static_cast<std::size_t>(8) * 1024 * 1024;

mr::coprocessor::WorkDirection coprocessorDirection(mr::editor::LineIndexScanDirection direction) noexcept {
	return direction == mr::editor::LineIndexScanDirection::Bof ? mr::coprocessor::WorkDirection::Bof : mr::coprocessor::WorkDirection::Eof;
}

const char *lineIndexDirectionName(mr::coprocessor::WorkDirection direction) noexcept {
	return direction == mr::coprocessor::WorkDirection::Bof ? "BOF" : "EOF";
}

} // namespace

std::size_t MRFileEditor::cancelLineIndexWarmup() noexcept {
	const std::size_t cancelledCount = mLineIndexWarmupState.packets.size();
	for (const LineIndexPacketState &packet : mLineIndexWarmupState.packets) {
		if (packet.taskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(packet.taskId));
		mBufferModel.releaseLineIndexScanReservation(packet.reservationId);
	}
	mLineIndexWarmupState = LineIndexWarmupState();
	if (cancelledCount != 0) notifyWindowTaskStateChanged();
	return cancelledCount;
}

void MRFileEditor::clearLineIndexWarmupTask(std::uint64_t expectedTaskId) noexcept {
	if (expectedTaskId == 0) return;
	for (std::size_t i = 0; i < mLineIndexWarmupState.packets.size(); ++i) {
		const LineIndexPacketState &packet = mLineIndexWarmupState.packets[i];
		if (packet.taskId != expectedTaskId) continue;
		mBufferModel.releaseLineIndexScanReservation(packet.reservationId);
		mLineIndexWarmupState.packets.erase(mLineIndexWarmupState.packets.begin() + static_cast<std::ptrdiff_t>(i));
		notifyWindowTaskStateChanged();
		return;
	}
}

bool MRFileEditor::applyLineIndexWarmup(const mr::editor::LineIndexScanPacket &packet, std::size_t expectedVersion, std::uint64_t expectedTaskId) {
	const bool exactLineCountWasKnown = mBufferModel.exactLineCountKnown();
	if (!mBufferModel.adoptLineIndexScanPacket(packet, expectedVersion)) return false;
	mCachedCursorLineDocumentId = 0;
	clearLineIndexWarmupTask(expectedTaskId);

	if (mBufferModel.exactLineCountKnown()) {
		static_cast<void>(cancelViewportFoldWarmup());
		if (!exactLineCountWasKnown) applyMiniMapSignals(mMiniMapState.renderer().invalidate(true, mBufferModel.documentId()));
		static_cast<void>(cancelLineIndexWarmup());
		continueDocumentFoldLevelOperationIfNeeded();
	}
	static_cast<void>(continuePendingDocumentLineNavigation());

	if (syntaxPipelineEnabled()) scheduleSyntaxWarmupIfNeeded();
	scheduleDisplayWidthWarmupIfNeeded();
	if (mBufferModel.exactLineCountKnown()) {
		updateMetrics();
		updateIndicator();
		drawView();
	}
	return true;
}

bool MRFileEditor::continuePendingDocumentLineNavigation() {
	if (!mPendingDocumentLineNavigationState.active) return false;
	const bool sameSource = mPendingDocumentLineNavigationState.documentId == mBufferModel.documentId() &&
	                        mPendingDocumentLineNavigationState.version == mBufferModel.version();
	const bool sameCursor = mPendingDocumentLineNavigationState.cursorOffset == mBufferModel.cursor() &&
	                        mPendingDocumentLineNavigationState.selectionStart == mBufferModel.selectionStart() &&
	                        mPendingDocumentLineNavigationState.selectionEnd == mBufferModel.selectionEnd();
	if (!sameSource || !sameCursor) {
		mPendingDocumentLineNavigationState = PendingDocumentLineNavigationState();
		return false;
	}

	if (!mBufferModel.lineStartByIndexKnown(mPendingDocumentLineNavigationState.targetLine)) return false;
	const std::size_t targetLine = mPendingDocumentLineNavigationState.targetLine;
	mPendingDocumentLineNavigationState = PendingDocumentLineNavigationState();
	moveCursorToDocumentLineTop(targetLine, 0);
	return true;
}

void MRFileEditor::scheduleLineIndexWarmupIfNeeded() {
	if (pieceTableOnlyPhaseActive()) {
		static_cast<void>(cancelLineIndexWarmup());
		return;
	}
	if (mSuppressLargeFileLineIndexWarmup) {
		return;
	}
	if (mBufferModel.exactLineCountKnown()) {
		static_cast<void>(cancelLineIndexWarmup());
		return;
	}

	const std::size_t documentId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	const std::size_t focusOffset = std::min(mBufferModel.cursor(), mBufferModel.length());
	const std::size_t focusBucket = focusOffset / kLineIndexPacketBytes;
	const bool differentDocument = mLineIndexWarmupState.documentId != documentId || mLineIndexWarmupState.version != version;
	if (differentDocument) {
		static_cast<void>(cancelLineIndexWarmup());
		mLineIndexWarmupState.documentId = documentId;
		mLineIndexWarmupState.version = version;
	}
	if (mLineIndexWarmupState.generation == 0 || mLineIndexWarmupState.focusBucket != focusBucket) {
		if (mLineIndexGenerationCounter == 0) ++mLineIndexGenerationCounter;
		mLineIndexWarmupState.generation = mLineIndexGenerationCounter++;
		mLineIndexWarmupState.focusBucket = focusBucket;
		mLineIndexWarmupState.focusOffset = focusOffset;
	}

	std::size_t currentGenerationTasks = 0;
	for (const LineIndexPacketState &packet : mLineIndexWarmupState.packets)
		if (packet.generation == mLineIndexWarmupState.generation && packet.taskId != 0) ++currentGenerationTasks;
	const std::size_t workerBudget = std::max<std::size_t>(1, mr::coprocessor::globalCoprocessor().allowedCoreCount());
	if (currentGenerationTasks >= workerBudget) return;

	std::vector<mr::editor::LineIndexScanReservation> reservations =
	    mBufferModel.reserveLineIndexScanSpans(mLineIndexWarmupState.focusOffset, workerBudget - currentGenerationTasks, kLineIndexPacketBytes);
	if (reservations.empty()) return;
	const MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
	const std::uint64_t generation = mLineIndexWarmupState.generation;

	for (const mr::editor::LineIndexScanReservation &reservation : reservations) {
		const mr::coprocessor::WorkDirection direction = coprocessorDirection(reservation.direction);
		const std::string label = std::string(lineIndexWarmupTaskLabel()) + " " + lineIndexDirectionName(direction) + " bytes " + std::to_string(reservation.startOffset) + ".." + std::to_string(reservation.endOffset);
		const std::uint64_t taskId = mr::coprocessor::globalCoprocessor().submitPacket(
		    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::LineIndexWarmup, documentId, version, mExecutionOwnerKind, mExecutionOwnerLocalId,
		    generation, direction, reservation.startOffset, reservation.endOffset, label, [snapshot, reservation](const mr::coprocessor::TaskInfo &info) {
			    mr::coprocessor::Result result;
			    mr::editor::LineIndexScanPacket packet;
			    result.task = info;
			    if (info.cancelRequested() || !snapshot.scanLineIndexSpan(packet, reservation.reservationId, reservation.startOffset, reservation.endOffset, info.cancelFlag.get())) {
				    result.status = mr::coprocessor::TaskStatus::Cancelled;
				    return result;
			    }
			    result.status = mr::coprocessor::TaskStatus::Completed;
			    result.payload = std::make_shared<mr::coprocessor::LineIndexWarmupPayload>(std::move(packet));
			    return result;
		    });

		LineIndexPacketState state;
		state.taskId = taskId;
		state.reservationId = reservation.reservationId;
		state.generation = generation;
		state.direction = direction;
		state.startOffset = reservation.startOffset;
		state.endOffset = reservation.endOffset;
		mLineIndexWarmupState.packets.push_back(state);
	}

	notifyWindowTaskStateChanged();
}
