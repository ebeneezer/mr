#include "MRFileEditorFoldAnalysis.hpp"
#include "MRFoldWarmupPayload.hpp"
#include "../../outline/MROutlineFoldProducer.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif

namespace {

static constexpr std::size_t kLargeFileWarmupTraceBytes = static_cast<std::size_t>(8) * 1024 * 1024;
static constexpr std::size_t kCompleteOutlineFoldLineBudget = 20000;
static constexpr std::size_t kOutlineCaptureByteBudget = static_cast<std::size_t>(8) * 1024 * 1024;

} // namespace

using mr::editor::fold::MRFoldScanOutput;
using mr::editor::fold::appendFoldScanLineTexts;
using mr::editor::fold::computeFoldSpansForLineTexts;
using mr::editor::fold::splitFoldTrainingLines;

std::string mrBuildFoldTrainingAscii(const std::string &text, MRSyntaxLanguage language) {
	const std::vector<std::string> lineTexts = splitFoldTrainingLines(text);
	const MRFoldScanOutput scan = computeFoldSpansForLineTexts(lineTexts, lineTexts.size(), 0, 0, lineTexts.size(), language, std::map<std::size_t, MRFoldSpan>(),
	                                                         MRFoldAnalysisState(), true);
	std::string output;
	auto branchContinuesAtSameLevel = [&scan](const MRFoldSpan &span) noexcept {
		for (const MRFoldSpan &candidate : scan.spans)
			if (candidate.siblingContinuation && candidate.level == span.level && candidate.startLine == span.endLine + 1) return true;
		return false;
	};

	for (std::size_t lineIndex = 0; lineIndex < lineTexts.size(); ++lineIndex) {
		std::vector<std::string> gutter(static_cast<std::size_t>(std::max(1, scan.visibleMaxLevel)), " ");

		for (const MRFoldSpan &span : scan.spans) {
			if (span.level >= gutter.size()) continue;
			if (lineIndex == span.startLine) gutter[span.level] = span.siblingContinuation ? "\xE2\x94\x9C" : "\xE2\x95\xAD";
			else if (lineIndex == span.endLine)
				gutter[span.level] = branchContinuesAtSameLevel(span) ? "\xE2\x94\x82" : "\xE2\x95\xB0";
			else if (lineIndex > span.startLine && lineIndex < span.endLine)
				gutter[span.level] = "\xE2\x94\x82";
		}
		for (const MRFoldGutterBranch &branch : scan.branches)
			if (lineIndex == branch.line && branch.level < gutter.size()) gutter[branch.level] = "\xE2\x94\x9C";
		char lineNumber[32];
		std::snprintf(lineNumber, sizeof(lineNumber), "%6zu", lineIndex + 1);
		output.append(lineNumber);
		output.push_back(' ');
		for (const std::string &cell : gutter)
			output.append(cell);
		output.append(" | ");
		output.append(lineTexts[lineIndex]);
		output.push_back('\n');
	}
	return output;
}

std::string mrBuildOutlineTrainingAscii(const std::string &text, MRSyntaxLanguage language) {
	const std::vector<std::string> lineTexts = splitFoldTrainingLines(text);
	const MRFoldScanOutput scan = computeFoldSpansForLineTexts(lineTexts, lineTexts.size(), 0, 0, lineTexts.size(), language, std::map<std::size_t, MRFoldSpan>(),
	                                                         MRFoldAnalysisState(), true);
	return mrBuildOutlineTrainingAsciiForFoldSpans(lineTexts, scan.spans, language);
}

bool MRFileEditor::buildFoldOutlineSnapshot(const MROutlineRequest &request, MROutlineSnapshot &snapshot) const {
	MRFoldOutlineInputSnapshot input;

	snapshot.documentId = mBufferModel.documentId();
	snapshot.version = mBufferModel.version();
	snapshot.topLine = 0;
	snapshot.bottomLine = 0;
	snapshot.complete = false;
	snapshot.nodes.clear();
	snapshot.textPool.clear();
	if (!captureFoldOutlineInput(request, input)) return false;
	return mrBuildFoldOutlineSnapshot(input, snapshot);
}

bool MRFileEditor::captureFoldOutlineInput(const MROutlineRequest &request, MRFoldOutlineInputSnapshot &input) const {
	const MRFoldingDerivedState::VisibleState &visibleState = mFoldState.visibleState();
	std::size_t exactLineCount = 0;
	bool complete = false;

	const std::size_t documentId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	if (visibleState.documentId != documentId || visibleState.version != version || visibleState.bottomLine <= visibleState.topLine) return false;
	if (mBufferModel.exactLineCountKnown()) {
		exactLineCount = std::max<std::size_t>(1, mBufferModel.lineCount());
		complete = visibleState.topLine == 0 && visibleState.bottomLine >= exactLineCount;
	}
	if (!request.allowPartial && !complete) return false;
	std::size_t captureBytes = 0;
	for (const std::string &lineText : visibleState.lineTexts) {
		if (lineText.size() > kOutlineCaptureByteBudget - std::min(captureBytes, kOutlineCaptureByteBudget)) return false;
		captureBytes += lineText.size();
	}
	const bool cacheMatches = mFoldOutlineInputCache != nullptr && mFoldOutlineInputCache->documentId == documentId &&
	                          mFoldOutlineInputCache->version == version && mFoldOutlineInputCache->visibleRevision == visibleState.revision &&
	                          mFoldOutlineInputCache->topLine == visibleState.topLine && mFoldOutlineInputCache->bottomLine == visibleState.bottomLine &&
	                          mFoldOutlineInputCache->complete == complete;
	if (!cacheMatches) {
		std::shared_ptr<MRFoldOutlineInputSnapshot> captured = std::make_shared<MRFoldOutlineInputSnapshot>();

		captured->language = visibleState.language;
		captured->documentId = documentId;
		captured->version = version;
		captured->topLine = visibleState.topLine;
		captured->bottomLine = visibleState.bottomLine;
		captured->visibleRevision = visibleState.revision;
		captured->complete = complete;
		captured->lineTexts = std::make_shared<const std::vector<std::string>>(visibleState.lineTexts);
		captured->spans = std::make_shared<const std::vector<MRFoldSpan>>(visibleState.spans);
		captured->readSnapshot = std::make_shared<const MRTextBufferModel::ReadSnapshot>(mBufferModel.readSnapshot());
		if (captured->readSnapshot->documentId() != documentId || captured->readSnapshot->version() != version) return false;
		mFoldOutlineInputCache = captured;
	}
	input = *mFoldOutlineInputCache;
	input.request = request;
	return input.readSnapshot != nullptr && input.readSnapshot->documentId() == input.documentId && input.readSnapshot->version() == input.version;
}

std::uint64_t MRFileEditor::foldOutlineInputRevision() const noexcept {
	const MRFoldingDerivedState::VisibleState &visibleState = mFoldState.visibleState();

	if (visibleState.documentId != mBufferModel.documentId() || visibleState.version != mBufferModel.version() || visibleState.bottomLine <= visibleState.topLine)
		return 0;
	return visibleState.revision;
}

bool MRFileEditor::completeFoldOutlineInputAvailable() const noexcept {
	const MRFoldingDerivedState::VisibleState &visibleState = mFoldState.visibleState();
	const std::size_t documentId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();

	if (!mBufferModel.exactLineCountKnown()) return false;
	const std::size_t lineCount = std::max<std::size_t>(1, mBufferModel.lineCount());
	return visibleState.documentId == documentId && visibleState.version == version && visibleState.language == mBufferModel.language() &&
	       visibleState.topLine == 0 && visibleState.bottomLine >= lineCount;
}

bool MRFileEditor::canRequestCompleteFoldOutlineWarmup() const {
	if (!foldingPipelineEnabled()) return false;
	if (useApproximateLargeFileMetrics()) return false;
	if (mBufferModel.length() > kOutlineCaptureByteBudget) return false;
	if (!mBufferModel.exactLineCountKnown()) return true;
	return std::max<std::size_t>(1, mBufferModel.lineCount()) <= kCompleteOutlineFoldLineBudget;
}

bool MRFileEditor::requestCompleteFoldOutlineWarmup() {
	const MRSyntaxLanguage language = mBufferModel.language();
	std::size_t lineCount = 0;

	if (!canRequestCompleteFoldOutlineWarmup()) return false;
	if (!mBufferModel.exactLineCountKnown()) return false;
	lineCount = std::max<std::size_t>(1, mBufferModel.lineCount());
	if (completeFoldOutlineInputAvailable()) return true;
	scheduleFoldWarmupIfNeeded(0, lineCount, 0, lineCount, language);
	return true;
}

bool MRFileEditor::foldConfirmedStateForPacket(const FoldPacketState &packet, MRFoldAnalysisState &state) const noexcept {
	for (const FoldCheckpointState &checkpoint : mFoldWarmupState.checkpoints)
		if (checkpoint.generation == packet.generation && checkpoint.line == packet.startLine) {
			state = checkpoint.state;
			return true;
		}
	return false;
}

bool MRFileEditor::shouldTraceLargeFileWarmupDiagnostics() const noexcept {
	return mBufferModel.document().length() >= kLargeFileWarmupTraceBytes;
}

std::shared_ptr<const mr::coprocessor::Payload> MRFileEditor::buildFoldWarmupPayload(const MRTextBufferModel::ReadSnapshot &snapshot, MRSyntaxLanguage language, std::uint64_t generation,
	                                                                                   mr::coprocessor::WorkDirection direction, std::size_t startLine, std::size_t endLine,
	                                                                                   std::size_t totalLines, bool documentEndKnown, const MRFoldAnalysisState &inputState,
	                                                                                   const std::map<std::size_t, MRFoldSpan> &closedFoldSpans, const std::shared_ptr<std::atomic_bool> &cancelFlag,
	                                                                                   bool retainProjectionData, int retainedFoldLevel) {
	auto shouldStop = [&]() noexcept { return cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire); };
	if (shouldStop()) return std::shared_ptr<const mr::coprocessor::Payload>();
	const std::size_t lookaheadEndLine = endLine < totalLines ? endLine + 1 : endLine;
	std::vector<std::string> lineTexts;
	lineTexts.reserve(lookaheadEndLine > startLine ? lookaheadEndLine - startLine : 0);
	appendFoldScanLineTexts(lineTexts, snapshot, startLine, lookaheadEndLine);
	if (shouldStop()) return std::shared_ptr<const mr::coprocessor::Payload>();
	const std::size_t processedLineCount = std::min(endLine - startLine, lineTexts.size());
	MRFoldScanOutput scan = computeFoldSpansForLineTexts(lineTexts, processedLineCount, startLine, startLine, endLine, language, closedFoldSpans, inputState,
	                                                   documentEndKnown && endLine >= totalLines);
	if (retainedFoldLevel >= 0)
		scan.spans.erase(std::remove_if(scan.spans.begin(), scan.spans.end(), [retainedFoldLevel](const MRFoldSpan &span) { return static_cast<int>(span.level) != retainedFoldLevel; }), scan.spans.end());
	if (lineTexts.size() > processedLineCount) lineTexts.resize(processedLineCount);
	if (!retainProjectionData) {
		lineTexts.clear();
		scan.spans.clear();
		scan.branches.clear();
	}
	return std::make_shared<MRFoldWarmupPayload>(generation, direction, language, startLine, startLine + processedLineCount, scan.visibleMaxLevel, inputState, std::move(scan.stateOut),
	                                           std::move(lineTexts), std::move(scan.spans), std::move(scan.branches));
}

std::shared_ptr<const mr::coprocessor::Payload> MRFileEditor::buildFoldLineAcquisitionPayload(const MRTextBufferModel::ReadSnapshot &snapshot, MRSyntaxLanguage language,
	                                                                                           std::uint64_t generation, std::size_t startLine, std::size_t endLine,
	                                                                                           std::size_t totalLines, const std::shared_ptr<std::atomic_bool> &cancelFlag) {
	auto shouldStop = [&]() noexcept { return cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire); };
	if (shouldStop() || endLine <= startLine) return std::shared_ptr<const mr::coprocessor::Payload>();
	const std::size_t lookaheadEndLine = endLine < totalLines ? endLine + 1 : endLine;
	std::shared_ptr<std::vector<std::string>> lineTexts = std::make_shared<std::vector<std::string>>();
	lineTexts->reserve(lookaheadEndLine - startLine);
	appendFoldScanLineTexts(*lineTexts, snapshot, startLine, lookaheadEndLine);
	if (shouldStop() || lineTexts->size() < endLine - startLine) return std::shared_ptr<const mr::coprocessor::Payload>();
	return std::make_shared<FoldLineAcquisitionPayload>(generation, language, startLine, endLine, lineTexts);
}

std::shared_ptr<const mr::coprocessor::Payload> MRFileEditor::buildFoldValidationPayload(const std::shared_ptr<const std::vector<std::string>> &lineTexts, MRSyntaxLanguage language,
	                                                                                      std::uint64_t generation, std::size_t startLine, std::size_t endLine,
	                                                                                      std::size_t totalLines, const MRFoldAnalysisState &inputState, bool retainFoldSpans,
	                                                                                      unsigned short retainedFoldLevel, const std::shared_ptr<std::atomic_bool> &cancelFlag) {
	auto shouldStop = [&]() noexcept { return cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire); };
	if (shouldStop() || lineTexts == nullptr || endLine <= startLine || lineTexts->size() < endLine - startLine)
		return std::shared_ptr<const mr::coprocessor::Payload>();
	const std::size_t processedLineCount = endLine - startLine;
	MRFoldScanOutput scan = computeFoldSpansForLineTexts(*lineTexts, processedLineCount, startLine, startLine, endLine, language, std::map<std::size_t, MRFoldSpan>(), inputState,
	                                                   retainFoldSpans && endLine >= totalLines);
	if (shouldStop()) return std::shared_ptr<const mr::coprocessor::Payload>();
	if (retainFoldSpans) {
		// Closing the selected-level parents also hides every deeper descendant. The
		// effective projection exposes the next descendant level only when a parent is opened explicitly.
		scan.spans.erase(std::remove_if(scan.spans.begin(), scan.spans.end(), [retainedFoldLevel](const MRFoldSpan &span) { return span.level != retainedFoldLevel; }),
		                 scan.spans.end());
	} else {
		scan.spans.clear();
	}
	return std::make_shared<MRFoldWarmupPayload>(generation, mr::coprocessor::WorkDirection::Eof, language, startLine, endLine, scan.visibleMaxLevel, inputState,
	                                           std::move(scan.stateOut), std::vector<std::string>(), std::move(scan.spans), std::vector<MRFoldGutterBranch>());
}


#if defined(__clang__)
#pragma clang diagnostic pop
#endif
