#include "MRFileEditor.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace {

constexpr std::size_t kSyntaxCheckpointStride = 4096;
constexpr std::size_t kSyntaxPrefetchHeadroomFactor = 2;
constexpr std::size_t kSyntaxTargetPacketLines = 32;

struct SyntaxRequestedRange {
	std::size_t startLine;
	std::size_t endLine;
	mr::coprocessor::WorkDirection direction;
	std::size_t materializedStartLine;
	std::size_t materializedEndLine;

	SyntaxRequestedRange() noexcept
	    : startLine(0), endLine(0), direction(mr::coprocessor::WorkDirection::None), materializedStartLine(0), materializedEndLine(0) {
	}
};

bool syntaxLanguageCarriesLineState(MRSyntaxLanguage language) noexcept {
	switch (language) {
		case MRSyntaxLanguage::MRMAC:
		case MRSyntaxLanguage::C:
		case MRSyntaxLanguage::Cpp:
		case MRSyntaxLanguage::JavaScript:
		case MRSyntaxLanguage::Python:
		case MRSyntaxLanguage::Markdown:
		case MRSyntaxLanguage::Latex:
		case MRSyntaxLanguage::Bash:
		case MRSyntaxLanguage::Zsh:
		case MRSyntaxLanguage::Fish:
		case MRSyntaxLanguage::Perl:
		case MRSyntaxLanguage::Swift:
		case MRSyntaxLanguage::Rust:
		case MRSyntaxLanguage::Xml:
		case MRSyntaxLanguage::Go:
		case MRSyntaxLanguage::Kotlin:
		case MRSyntaxLanguage::CSharp:
		case MRSyntaxLanguage::Pascal:
			return true;
		case MRSyntaxLanguage::PlainText:
		case MRSyntaxLanguage::Json:
		case MRSyntaxLanguage::Yaml:
		case MRSyntaxLanguage::Systemd:
		case MRSyntaxLanguage::Make:
		case MRSyntaxLanguage::Basic:
			return false;
	}
	return true;
}

const char *syntaxDirectionName(mr::coprocessor::WorkDirection direction) noexcept {
	return direction == mr::coprocessor::WorkDirection::Bof ? "BOF" : "EOF";
}

std::vector<std::pair<std::size_t, std::size_t>> uncoveredSyntaxRanges(std::size_t startLine, std::size_t endLine,
	                                                                   std::vector<std::pair<std::size_t, std::size_t>> coveredRanges) {
	std::vector<std::pair<std::size_t, std::size_t>> uncovered;
	if (endLine <= startLine) return uncovered;
	std::sort(coveredRanges.begin(), coveredRanges.end(), [](const std::pair<std::size_t, std::size_t> &a, const std::pair<std::size_t, std::size_t> &b) {
		return a.first < b.first || (a.first == b.first && a.second < b.second);
	});

	std::size_t cursor = startLine;
	for (const std::pair<std::size_t, std::size_t> &range : coveredRanges) {
		if (range.second <= cursor || range.first >= endLine) continue;
		if (range.first > cursor) uncovered.push_back(std::make_pair(cursor, std::min(range.first, endLine)));
		if (range.second > cursor) cursor = std::min(range.second, endLine);
		if (cursor >= endLine) break;
	}
	if (cursor < endLine) uncovered.push_back(std::make_pair(cursor, endLine));
	return uncovered;
}

std::vector<SyntaxRequestedRange> splitSyntaxRanges(const std::vector<std::pair<std::size_t, std::size_t>> &ranges, std::size_t packetBudget,
	                                                 mr::coprocessor::WorkDirection direction, bool materialize) {
	std::vector<SyntaxRequestedRange> packets;
	std::size_t remainingLines = 0;
	for (const std::pair<std::size_t, std::size_t> &range : ranges)
		if (range.second > range.first) remainingLines += range.second - range.first;
	if (packetBudget == 0 || remainingLines == 0) return packets;
	packetBudget = std::min(packetBudget, (remainingLines + kSyntaxTargetPacketLines - 1) / kSyntaxTargetPacketLines);
	packetBudget = std::min(packetBudget, remainingLines);

	for (std::size_t rangeIndex = 0; rangeIndex < ranges.size() && packets.size() < packetBudget; ++rangeIndex) {
		std::size_t cursor = ranges[rangeIndex].first;
		const std::size_t rangeEnd = ranges[rangeIndex].second;
		while (cursor < rangeEnd && packets.size() < packetBudget) {
			const std::size_t remainingPackets = packetBudget - packets.size();
			const std::size_t targetLines = (remainingLines + remainingPackets - 1) / remainingPackets;
			SyntaxRequestedRange packet;
			packet.startLine = cursor;
			packet.endLine = std::min(rangeEnd, cursor + std::max<std::size_t>(1, targetLines));
			packet.direction = direction;
			if (materialize) {
				packet.materializedStartLine = packet.startLine;
				packet.materializedEndLine = packet.endLine;
			}
			packets.push_back(packet);
			remainingLines -= packet.endLine - packet.startLine;
			cursor = packet.endLine;
		}
	}
	return packets;
}

} // namespace

std::size_t MRFileEditor::cancelSyntaxWarmup() noexcept {
	std::size_t cancelledCount = 0;
	for (SyntaxPacketState &packet : mSyntaxWarmupState.packets) {
		if (packet.resultLifecycle.valid) {
			mr::coprocessor::globalCoprocessor().resolveDeferredResultAdoption(packet.resultLifecycle, false);
		}
		if (packet.taskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(packet.taskId));
			packet.taskId = 0;
			++cancelledCount;
		}
	}
	const bool hadState = !mSyntaxWarmupState.packets.empty();
	mSyntaxWarmupState = SyntaxWarmupState();
	if (hadState) notifyWindowTaskStateChanged();
	return cancelledCount;
}

void MRFileEditor::clearSyntaxWarmupTask(std::uint64_t expectedTaskId) noexcept {
	if (expectedTaskId == 0) return;
	for (std::size_t i = 0; i < mSyntaxWarmupState.packets.size(); ++i) {
		if (mSyntaxWarmupState.packets[i].taskId != expectedTaskId) continue;
		mSyntaxWarmupState.packets.erase(mSyntaxWarmupState.packets.begin() + static_cast<std::ptrdiff_t>(i));
		mSyntaxWarmupState.waitingForReservedPackets = false;
		notifyWindowTaskStateChanged();
		return;
	}
}

bool MRFileEditor::syntaxCheckpointForLine(std::size_t lineIndex, MRSyntaxCheckpointEntry &checkpoint) const {
	const std::map<std::size_t, MRSyntaxCheckpointEntry> &checkpoints = mSyntaxState.checkpoints();
	std::map<std::size_t, MRSyntaxCheckpointEntry>::const_iterator found = checkpoints.upper_bound(lineIndex);
	if (found == checkpoints.begin()) return false;
	--found;
	checkpoint = found->second;
	return true;
}

void MRFileEditor::rememberSyntaxCheckpoint(std::size_t lineStart, std::size_t lineIndex, const MRSyntaxLineState &stateIn) noexcept {
	mSyntaxState.checkpoints()[lineIndex] = MRSyntaxCheckpointEntry(lineStart, lineIndex, stateIn);
}

bool MRFileEditor::syntaxConfirmedStateForLine(std::size_t lineIndex, MRSyntaxLineState &stateIn) const noexcept {
	if (lineIndex == 0) {
		stateIn = MRSyntaxLineState();
		return true;
	}
	MRSyntaxCheckpointEntry checkpoint;
	if (!syntaxCheckpointForLine(lineIndex, checkpoint)) return false;
	if (checkpoint.lineIndex > lineIndex) return false;
	stateIn = checkpoint.stateIn;
	if (checkpoint.lineIndex == lineIndex) return true;

	std::size_t stateLineIndex = checkpoint.lineIndex;
	std::size_t stateLineStart = checkpoint.lineStart;
	const std::map<std::size_t, MRSyntaxCacheEntry> &tokenCache = mSyntaxState.tokenCache();
	while (stateLineIndex < lineIndex) {
		std::map<std::size_t, MRSyntaxCacheEntry>::const_iterator found = tokenCache.find(stateLineStart);
		if (found == tokenCache.end() || found->second.stateIn != stateIn) return false;
		stateIn = found->second.syntaxLine.stateOut;
		++stateLineIndex;
		if (stateLineIndex == lineIndex) return true;
		if (stateLineStart >= mBufferModel.length()) return false;
		const std::size_t nextLineStart = mBufferModel.nextLine(stateLineStart);
		if (nextLineStart <= stateLineStart) return false;
		stateLineStart = nextLineStart;
	}
	return stateLineIndex == lineIndex;
}

bool MRFileEditor::adoptReadySyntaxPackets(bool &tokensChanged) {
	bool adoptedAny = false;
	bool progressed = true;
	const bool statefulSyntax = syntaxLanguageCarriesLineState(mSyntaxWarmupState.language);

	while (progressed) {
		progressed = false;
		for (std::size_t packetIndex = 0; packetIndex < mSyntaxWarmupState.packets.size(); ++packetIndex) {
			SyntaxPacketState &packet = mSyntaxWarmupState.packets[packetIndex];
			if (!packet.resultReady) continue;

			MRSyntaxLineState confirmedState;
			const bool stateConfirmed = !statefulSyntax || syntaxConfirmedStateForLine(packet.startLine, confirmedState);
			if (!stateConfirmed) continue;
			if (statefulSyntax && confirmedState != packet.inputState) {
				mr::coprocessor::globalCoprocessor().resolveDeferredResultAdoption(packet.resultLifecycle, false);
				packet.inputState = confirmedState;
				packet.inputStateConfirmed = true;
				packet.resultReady = false;
				packet.checkpoints.clear();
				packet.lines.clear();
				progressed = true;
				continue;
			}

			for (const mr::coprocessor::SyntaxWarmCheckpoint &checkpoint : packet.checkpoints)
				rememberSyntaxCheckpoint(checkpoint.lineStart, checkpoint.lineIndex, checkpoint.stateIn);
			for (mr::coprocessor::SyntaxWarmLine &line : packet.lines)
				mSyntaxState.tokenCache()[line.lineStart] = MRSyntaxCacheEntry(line.stateIn, std::move(line.syntaxLine));
			if (packet.materializedEndLine > packet.materializedStartLine) {
				rememberSyntaxWarmedLineRange(packet.materializedStartLine, packet.materializedEndLine);
				tokensChanged = true;
			}
			if (packet.endLine > mSyntaxWarmupState.reachedBottomLine) mSyntaxWarmupState.reachedBottomLine = packet.endLine;
			mr::coprocessor::globalCoprocessor().resolveDeferredResultAdoption(packet.resultLifecycle, true);
			mSyntaxWarmupState.packets.erase(mSyntaxWarmupState.packets.begin() + static_cast<std::ptrdiff_t>(packetIndex));
			adoptedAny = true;
			progressed = true;
			break;
		}
	}
	return adoptedAny;
}

bool MRFileEditor::applySyntaxWarmup(const mr::coprocessor::SyntaxWarmupPayload &warmup, const mr::coprocessor::Result &result) {
	const std::size_t expectedVersion = result.task.baseVersion;
	const std::uint64_t expectedTaskId = result.task.id;
	if (!syntaxPipelineEnabled() || expectedTaskId == 0) return false;
	if (mSyntaxWarmupState.documentId != mBufferModel.documentId() || mSyntaxWarmupState.version != expectedVersion || mBufferModel.version() != expectedVersion ||
	    mSyntaxWarmupState.language != warmup.language || mBufferModel.language() != warmup.language)
		return false;

	for (SyntaxPacketState &packet : mSyntaxWarmupState.packets) {
		if (packet.taskId != expectedTaskId) continue;
		if (packet.generation != warmup.generation || packet.startLine != warmup.startLine || packet.endLine != warmup.endLine || packet.materializedStartLine != warmup.materializedStartLine ||
		    packet.materializedEndLine != warmup.materializedEndLine)
			return false;
		packet.taskId = 0;
		packet.inputState = warmup.stateIn;
		packet.checkpoints = warmup.checkpoints;
		packet.lines = warmup.lines;
		packet.resultReady = true;
		packet.resultLifecycle = mr::coprocessor::globalCoprocessor().acceptResultForDeferredAdoption(result);
		mSyntaxWarmupState.waitingForReservedPackets = false;
		notifyWindowTaskStateChanged();
		return true;
	}
	return false;
}

void MRFileEditor::submitSyntaxPacket(SyntaxPacketState &packet, const MRTextBufferModel::ReadSnapshot &snapshot) {
	if (packet.taskId != 0 || packet.resultReady || packet.endLine <= packet.startLine) return;
	const std::size_t documentId = mSyntaxWarmupState.documentId;
	const std::size_t version = mSyntaxWarmupState.version;
	const MRSyntaxLanguage language = mSyntaxWarmupState.language;
	const bool statefulSyntax = syntaxLanguageCarriesLineState(language);
	const std::uint64_t generation = packet.generation;
	const mr::coprocessor::WorkDirection direction = packet.direction;
	const std::size_t startLine = packet.startLine;
	const std::size_t endLine = packet.endLine;
	const std::size_t materializedStartLine = packet.materializedStartLine;
	const std::size_t materializedEndLine = packet.materializedEndLine;
	const bool inputStateConfirmed = packet.inputStateConfirmed;
	const MRSyntaxLineState confirmedInputState = packet.inputState;
	const std::string label = std::string(syntaxWarmupTaskLabel()) + " " + syntaxDirectionName(direction) + " lines " + std::to_string(startLine + 1) + "-" + std::to_string(endLine);

	packet.taskId = mr::coprocessor::globalCoprocessor().submitPacket(
	    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::SyntaxWarmup, documentId, version, mExecutionOwnerKind, mExecutionOwnerLocalId, generation,
	    direction, startLine, endLine, label,
	    [snapshot, language, statefulSyntax, generation, startLine, endLine, materializedStartLine, materializedEndLine, inputStateConfirmed,
	     confirmedInputState](const mr::coprocessor::TaskInfo &info) {
		    mr::coprocessor::Result result;
		    result.task = info;
		    if (info.cancelRequested()) {
			    result.status = mr::coprocessor::TaskStatus::Cancelled;
			    return result;
		    }

		    MRSyntaxLineState state = confirmedInputState;
		    if (statefulSyntax && !inputStateConfirmed) state = tmrHighlightTextLine(language, std::string_view(), MRSyntaxLineState()).stateOut;
		    const MRSyntaxLineState packetStateIn = state;
		    std::vector<mr::coprocessor::SyntaxWarmCheckpoint> checkpoints;
		    std::vector<mr::coprocessor::SyntaxWarmLine> lines;
		    const std::size_t materializedLines = materializedEndLine > materializedStartLine ? materializedEndLine - materializedStartLine : 0;
		    lines.reserve(materializedLines);
		    checkpoints.reserve(statefulSyntax ? (endLine - startLine) / kSyntaxCheckpointStride + 2 : 0);
		    std::size_t lineStart = snapshot.lineStartByIndex(startLine);

		    for (std::size_t lineIndex = startLine; lineIndex < endLine; ++lineIndex) {
			    if (info.cancelRequested()) {
				    result.status = mr::coprocessor::TaskStatus::Cancelled;
				    return result;
			    }
			    if (statefulSyntax && (lineIndex == startLine || lineIndex % kSyntaxCheckpointStride == 0))
				    checkpoints.push_back(mr::coprocessor::SyntaxWarmCheckpoint(lineStart, lineIndex, state));
			    const MRSyntaxLineState lineStateIn = statefulSyntax ? state : MRSyntaxLineState();
			    MRSyntaxLineResult syntaxLine = tmrHighlightTextLine(language, snapshot.lineText(lineStart), lineStateIn);
			    const MRSyntaxLineState lineStateOut = syntaxLine.stateOut;
			    if (lineIndex >= materializedStartLine && lineIndex < materializedEndLine)
				    lines.push_back(mr::coprocessor::SyntaxWarmLine(lineStart, lineIndex, lineStateIn, std::move(syntaxLine)));
			    if (statefulSyntax) state = lineStateOut;
			    else
				    state = MRSyntaxLineState();
			    if (lineIndex + 1 < endLine) {
				    const std::size_t nextLineStart = snapshot.nextLine(lineStart);
				    if (nextLineStart <= lineStart) {
					    result.status = mr::coprocessor::TaskStatus::Failed;
					    result.error = "Syntax packet could not advance to its next line.";
					    return result;
				    }
				    lineStart = nextLineStart;
			    }
		    }
		    if (statefulSyntax && endLine < snapshot.lineCount())
			    checkpoints.push_back(mr::coprocessor::SyntaxWarmCheckpoint(snapshot.lineStartByIndex(endLine), endLine, state));
		    result.status = mr::coprocessor::TaskStatus::Completed;
		    result.payload = std::make_shared<mr::coprocessor::SyntaxWarmupPayload>(generation, language, startLine, endLine, materializedStartLine, materializedEndLine, packetStateIn,
		                                                                                         std::move(checkpoints), std::move(lines));
		    return result;
	    });
}

void MRFileEditor::scheduleSyntaxWarmupIfNeeded() {
	if (!syntaxPipelineEnabled() || mBufferModel.language() == MRSyntaxLanguage::PlainText || visibleTextRows() <= 0) {
		resetSyntaxWarmupState(true);
		return;
	}
	if (!mBufferModel.exactLineCountKnown()) return;
	bool tokensChanged = false;
	static_cast<void>(adoptReadySyntaxPackets(tokensChanged));
	if (tokensChanged) drawView();

	const std::size_t documentId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	const MRSyntaxLanguage language = mBufferModel.language();
	const std::size_t totalLines = std::max<std::size_t>(1, mBufferModel.lineCount());
	const int textRows = std::max(1, visibleTextRows());
	const bool largeFile = useApproximateLargeFileMetrics();
	const std::size_t visibleTopLine = std::min(documentLineForVisibleLine(static_cast<std::size_t>(std::max(delta.y - (largeFile ? 1 : 4), 0))), totalLines - 1);
	const std::size_t visibleBottomLine = std::min(totalLines, documentLineForVisibleLine(static_cast<std::size_t>(std::max(delta.y, 0) + textRows - 1)) + 1);
	const std::size_t backgroundRows = static_cast<std::size_t>(std::max(textRows * (largeFile ? 2 : 3), 8));
	const std::size_t requiredBottomLine = std::min(totalLines, visibleBottomLine + backgroundRows);
	const bool differentDocument = mSyntaxWarmupState.documentId != 0 &&
	                               (mSyntaxWarmupState.documentId != documentId || mSyntaxWarmupState.version != version || mSyntaxWarmupState.language != language);
	if (differentDocument) {
		const bool retainPrefix = mSyntaxWarmupState.documentId == documentId && mSyntaxWarmupState.language == language;
		static_cast<void>(cancelSyntaxWarmup());
		if (!retainPrefix) mSyntaxState.resetState(true);
	}
	if (mSyntaxWarmupState.documentId == 0) {
		mSyntaxWarmupState.documentId = documentId;
		mSyntaxWarmupState.version = version;
		mSyntaxWarmupState.language = language;
	}
	mSyntaxState.ensureWarmedLineRangeOwner(documentId, language);

	const MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
	bool submittedRetry = false;
	for (SyntaxPacketState &packet : mSyntaxWarmupState.packets) {
		if (packet.taskId != 0 || packet.resultReady) continue;
		submitSyntaxPacket(packet, snapshot);
		if (packet.taskId != 0) submittedRetry = true;
	}
	if (submittedRetry) notifyWindowTaskStateChanged();

	if (syntaxWarmedLineRangeCovered(visibleTopLine, requiredBottomLine)) {
		mSyntaxWarmupState.visibleTopLine = visibleTopLine;
		mSyntaxWarmupState.visibleBottomLine = visibleBottomLine;
		if (mSyntaxWarmupState.targetBottomLine < requiredBottomLine) mSyntaxWarmupState.targetBottomLine = requiredBottomLine;
		mSyntaxWarmupState.reachedBottomLine = std::max(mSyntaxWarmupState.reachedBottomLine, requiredBottomLine);
		return;
	}
	if (mSyntaxWarmupState.waitingForReservedPackets && visibleTopLine >= mSyntaxWarmupState.visibleTopLine && requiredBottomLine <= mSyntaxWarmupState.targetBottomLine) return;
	bool currentGenerationPending = false;
	for (const SyntaxPacketState &packet : mSyntaxWarmupState.packets)
		if (packet.generation == mSyntaxWarmupState.generation) currentGenerationPending = true;
	if (currentGenerationPending && visibleTopLine >= mSyntaxWarmupState.visibleTopLine && requiredBottomLine <= mSyntaxWarmupState.targetBottomLine) return;

	const std::size_t targetBottomLine = std::min(totalLines, visibleBottomLine + backgroundRows * kSyntaxPrefetchHeadroomFactor);

	if (mSyntaxGenerationCounter == 0) ++mSyntaxGenerationCounter;
	mSyntaxWarmupState.generation = mSyntaxGenerationCounter++;
	mSyntaxWarmupState.visibleTopLine = visibleTopLine;
	mSyntaxWarmupState.visibleBottomLine = visibleBottomLine;
	mSyntaxWarmupState.targetBottomLine = targetBottomLine;

	const bool statefulSyntax = syntaxLanguageCarriesLineState(language);
	std::size_t contextAnchorLine = visibleTopLine;
	if (statefulSyntax) {
		MRSyntaxLineState visibleInputState;
		if (!syntaxConfirmedStateForLine(visibleTopLine, visibleInputState)) {
			MRSyntaxCheckpointEntry checkpoint;
			contextAnchorLine = syntaxCheckpointForLine(visibleTopLine, checkpoint) ? checkpoint.lineIndex : 0;
		}
	}
	if (mSyntaxWarmupState.reachedBottomLine < contextAnchorLine) mSyntaxWarmupState.reachedBottomLine = contextAnchorLine;

	std::vector<std::pair<std::size_t, std::size_t>> activeContextRanges;
	std::vector<std::pair<std::size_t, std::size_t>> activeMaterializedRanges = mSyntaxState.validRanges();
	for (const SyntaxPacketState &packet : mSyntaxWarmupState.packets) {
		activeContextRanges.push_back(std::make_pair(packet.startLine, packet.endLine));
		if (packet.materializedEndLine > packet.materializedStartLine)
			activeMaterializedRanges.push_back(std::make_pair(packet.materializedStartLine, packet.materializedEndLine));
	}
	const std::vector<std::pair<std::size_t, std::size_t>> contextRanges =
	    statefulSyntax ? uncoveredSyntaxRanges(contextAnchorLine, visibleTopLine, activeContextRanges) : std::vector<std::pair<std::size_t, std::size_t>>();
	const std::vector<std::pair<std::size_t, std::size_t>> materializedRanges = uncoveredSyntaxRanges(visibleTopLine, targetBottomLine, activeMaterializedRanges);
	std::size_t workerBudget = std::max<std::size_t>(1, mr::coprocessor::globalCoprocessor().allowedCoreCount());
	std::size_t contextBudget = 0;
	std::size_t materializedBudget = 0;
	if (!contextRanges.empty() && !materializedRanges.empty()) {
		if (workerBudget == 1) contextBudget = 1;
		else {
			contextBudget = workerBudget - 1;
			materializedBudget = 1;
		}
	} else if (!contextRanges.empty())
		contextBudget = workerBudget;
	else if (!materializedRanges.empty())
		materializedBudget = workerBudget;

	std::vector<SyntaxRequestedRange> requested = splitSyntaxRanges(contextRanges, contextBudget, mr::coprocessor::WorkDirection::Bof, false);
	std::vector<SyntaxRequestedRange> materialized = splitSyntaxRanges(materializedRanges, materializedBudget, mr::coprocessor::WorkDirection::Eof, true);
	requested.insert(requested.end(), materialized.begin(), materialized.end());
	mSyntaxWarmupState.waitingForReservedPackets = requested.empty() && !mSyntaxWarmupState.packets.empty();
	for (const SyntaxRequestedRange &range : requested) {
		SyntaxPacketState packet;
		packet.generation = mSyntaxWarmupState.generation;
		packet.direction = range.direction;
		packet.startLine = range.startLine;
		packet.endLine = range.endLine;
		packet.materializedStartLine = range.materializedStartLine;
		packet.materializedEndLine = range.materializedEndLine;
		packet.inputStateConfirmed = !statefulSyntax || syntaxConfirmedStateForLine(packet.startLine, packet.inputState);
		mSyntaxWarmupState.packets.push_back(packet);
	}
	for (SyntaxPacketState &packet : mSyntaxWarmupState.packets) {
		if (packet.generation != mSyntaxWarmupState.generation || packet.taskId != 0 || packet.resultReady) continue;
		submitSyntaxPacket(packet, snapshot);
	}
	if (!requested.empty()) notifyWindowTaskStateChanged();
}
