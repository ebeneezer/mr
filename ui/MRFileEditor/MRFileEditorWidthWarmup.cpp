#include "MRFileEditor.hpp"

#include <algorithm>
#include <string>

const char *MRFileEditor::displayWidthWarmupTaskLabel() noexcept {
	return "Display width warmup";
}

int MRFileEditor::provisionalDisplayWidthLimit() const noexcept {
	return std::max(mDisplayWidthPublishedLimit, mDisplayWidthWarmupState.maximumWidth);
}

bool MRFileEditor::displayWidthLimitExact() const noexcept {
	const DisplayWidthWarmupState &state = mDisplayWidthWarmupState;
	const MREditSetupSettings &settings = effectiveEditSetupSettings();
	return mBufferModel.exactLineCountKnown() && state.complete && !state.appendTailPending && state.documentId == mBufferModel.documentId() && state.version == mBufferModel.version() && state.totalLines == std::max<std::size_t>(1, mBufferModel.lineCount()) &&
	       state.tabSize == settings.tabSize && state.leftMargin == settings.leftMargin && state.rightMargin == settings.rightMargin && state.formatLine == settings.formatLine;
}

std::size_t MRFileEditor::cancelDisplayWidthWarmup() noexcept {
	std::size_t cancelledCount = 0;
	for (DisplayWidthPacketState &packet : mDisplayWidthWarmupState.packets) {
		if (packet.taskId == 0) continue;
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(packet.taskId));
		packet.taskId = 0;
		++cancelledCount;
	}
	mDisplayWidthWarmupState = DisplayWidthWarmupState();
	if (cancelledCount != 0) notifyWindowTaskStateChanged();
	return cancelledCount;
}

void MRFileEditor::resetDisplayWidthWarmup() noexcept {
	static_cast<void>(cancelDisplayWidthWarmup());
}

void MRFileEditor::clearDisplayWidthWarmupTask(std::uint64_t expectedTaskId) noexcept {
	if (expectedTaskId == 0) return;
	for (DisplayWidthPacketState &packet : mDisplayWidthWarmupState.packets) {
		if (packet.taskId != expectedTaskId) continue;
		packet.taskId = 0;
		notifyWindowTaskStateChanged();
		return;
	}
}

bool MRFileEditor::applyDisplayWidthWarmup(const mr::coprocessor::DisplayWidthWarmupPayload &warmup, std::size_t sourceVersion, std::uint64_t expectedTaskId) {
	DisplayWidthWarmupState &state = mDisplayWidthWarmupState;
	if (expectedTaskId == 0 || state.documentId != mBufferModel.documentId() || sourceVersion > state.version) return false;

	for (DisplayWidthPacketState &packet : state.packets) {
		if (packet.taskId != expectedTaskId) continue;
		// An append-only transition preserves every earlier display width. The packet's
		// source version and generation still have to match the retained ledger entry.
		if (packet.documentVersion != sourceVersion || packet.generation != warmup.generation || static_cast<std::uint64_t>(packet.startLine) != warmup.startLine || static_cast<std::uint64_t>(packet.endLine) != warmup.endLine || packet.adopted) return false;

		const int previousLimit = provisionalDisplayWidthLimit();
		packet.taskId = 0;
		packet.adopted = true;
		state.maximumWidth = std::max(state.maximumWidth, warmup.maximumWidth);
		state.complete = !state.appendTailPending;
		for (const DisplayWidthPacketState &pending : state.packets)
			if (!pending.adopted) state.complete = false;
		if (state.complete) mDisplayWidthPublishedLimit = state.maximumWidth;
		const bool metricsChanged = previousLimit != provisionalDisplayWidthLimit();

		notifyWindowTaskStateChanged();
		if (metricsChanged || state.complete) {
			updateMetrics();
			drawView();
		}
		return true;
	}
	return false;
}

bool MRFileEditor::prepareDisplayWidthWarmupForAppend(const MRTextBufferModel::DocumentChangeSet &changeSet) {
	DisplayWidthWarmupState &state = mDisplayWidthWarmupState;
	const MREditSetupSettings settings = effectiveEditSetupSettings();
	const bool pureAppend = changeSet.changed && changeSet.newLength > changeSet.oldLength && changeSet.touchedRange.start == changeSet.oldLength && changeSet.touchedRange.end == changeSet.newLength;
	const bool appendableState = state.documentId == mBufferModel.documentId() && state.version == changeSet.oldVersion && state.totalLines != 0 && state.tabSize == settings.tabSize && state.leftMargin == settings.leftMargin &&
	                             state.rightMargin == settings.rightMargin && state.formatLine == settings.formatLine;

	if (!pureAppend || !appendableState || mBufferModel.version() != changeSet.newVersion) return false;

	const std::size_t previousTotalLines = state.totalLines;
	state.version = mBufferModel.version();
	state.generation = mDisplayWidthGenerationCounter++;
	if (state.generation == 0) state.generation = mDisplayWidthGenerationCounter++;
	state.complete = false;
	const std::size_t appendStartLine = previousTotalLines - 1;
	if (!state.appendTailPending || appendStartLine < state.appendStartLine) state.appendStartLine = appendStartLine;
	state.appendTailPending = true;
	state.packets.erase(std::remove_if(state.packets.begin(), state.packets.end(), [](const DisplayWidthPacketState &packet) {
		return packet.adopted;
	}), state.packets.end());
	return true;
}

void MRFileEditor::scheduleDisplayWidthWarmupIfNeeded() {
	if (!mBufferModel.exactLineCountKnown()) return;

	const std::size_t documentId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	const std::size_t totalLines = std::max<std::size_t>(1, mBufferModel.lineCount());
	const MREditSetupSettings settings = effectiveEditSetupSettings();
	DisplayWidthWarmupState &state = mDisplayWidthWarmupState;
	const bool appendStateMatches = state.appendTailPending && state.documentId == documentId && state.version == version && state.tabSize == settings.tabSize && state.leftMargin == settings.leftMargin &&
	                                state.rightMargin == settings.rightMargin && state.formatLine == settings.formatLine;
	if (appendStateMatches) {
		DisplayWidthPacketState tail;
		tail.generation = state.generation;
		tail.startLine = std::min(state.appendStartLine, totalLines - 1);
		tail.endLine = totalLines;
		state.totalLines = totalLines;
		state.appendTailPending = false;
		state.packets.push_back(tail);
	}
	const bool matchingState = state.documentId == documentId && state.version == version && state.totalLines == totalLines && state.tabSize == settings.tabSize && state.leftMargin == settings.leftMargin &&
	                           state.rightMargin == settings.rightMargin && state.formatLine == settings.formatLine;

	if (!matchingState) {
		resetDisplayWidthWarmup();
		state.documentId = documentId;
		state.version = version;
		state.totalLines = totalLines;
		state.generation = mDisplayWidthGenerationCounter++;
		if (state.generation == 0) state.generation = mDisplayWidthGenerationCounter++;
		state.tabSize = settings.tabSize;
		state.leftMargin = settings.leftMargin;
		state.rightMargin = settings.rightMargin;
		state.formatLine = settings.formatLine;
		if (mBufferModel.length() == 0) {
			state.complete = true;
			mDisplayWidthPublishedLimit = 1;
			return;
		}

	}
	if (state.complete) return;
	if (state.packets.empty()) {
		std::size_t packetCount = mr::coprocessor::globalCoprocessor().allowedCoreCount();
		if (packetCount == 0) packetCount = 1;
		packetCount = std::min(packetCount, totalLines);
		state.packets.resize(packetCount);
		const std::size_t basePacketLines = totalLines / packetCount;
		const std::size_t extraLines = totalLines % packetCount;
		std::size_t startLine = 0;
		for (std::size_t packetIndex = 0; packetIndex < packetCount; ++packetIndex) {
			DisplayWidthPacketState &packet = state.packets[packetIndex];
			packet.generation = state.generation;
			const std::size_t packetLines = basePacketLines + (packetIndex < extraLines ? 1 : 0);
			packet.startLine = startLine;
			packet.endLine = startLine + packetLines;
			startLine = packet.endLine;
		}
	}

	const MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
	bool scheduledAny = false;
	for (DisplayWidthPacketState &packet : state.packets) {
		if (packet.adopted || packet.taskId != 0 || packet.endLine <= packet.startLine) continue;
		const std::size_t startLine = packet.startLine;
		const std::size_t endLine = packet.endLine;
		const std::uint64_t generation = packet.generation;
		const std::string taskLabel = std::string(displayWidthWarmupTaskLabel()) + " lines " + std::to_string(startLine + 1) + "-" + std::to_string(endLine);
		packet.documentVersion = version;
		packet.taskId = mr::coprocessor::globalCoprocessor().submitPacket(
		    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::DisplayWidthWarmup, documentId, version, mExecutionOwnerKind, mExecutionOwnerLocalId, generation, mr::coprocessor::WorkDirection::Eof, startLine, endLine, taskLabel,
		    [snapshot, settings, generation, startLine, endLine](const mr::coprocessor::TaskInfo &info) {
			    mr::coprocessor::Result result;
			    int maximumWidth = 1;
			    std::size_t lineStart = snapshot.lineStartByIndex(startLine);
			    result.task = info;
			    for (std::size_t lineIndex = startLine; lineIndex < endLine; ++lineIndex) {
				    if (info.cancelRequested()) {
					    result.status = mr::coprocessor::TaskStatus::Cancelled;
					    return result;
				    }
				    maximumWidth = std::max(maximumWidth, MRFileEditor::displayWidthForText(snapshot.lineText(lineStart), settings) + 1);
				    if (lineIndex + 1 < endLine) lineStart = snapshot.nextLine(lineStart);
			    }
			    result.status = mr::coprocessor::TaskStatus::Completed;
			    result.payload = std::make_shared<mr::coprocessor::DisplayWidthWarmupPayload>(generation, startLine, endLine, maximumWidth);
			    return result;
		    });
		if (packet.taskId != 0) scheduledAny = true;
	}
	if (scheduledAny) notifyWindowTaskStateChanged();
}
