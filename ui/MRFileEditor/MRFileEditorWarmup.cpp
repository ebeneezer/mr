#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"
#include "../../app/MREditorApp.hpp"

#include <chrono>
#include <ctime>
#include <future>
#include <sstream>
#include <thread>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif

namespace {
bool isStatefulSyntaxLanguage(MRSyntaxLanguage language) noexcept {
	return language == MRSyntaxLanguage::MRMAC || language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp || language == MRSyntaxLanguage::JavaScript || language == MRSyntaxLanguage::Python ||
	       language == MRSyntaxLanguage::Markdown || language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh || language == MRSyntaxLanguage::Fish || language == MRSyntaxLanguage::Perl || language == MRSyntaxLanguage::Swift || language == MRSyntaxLanguage::Rust ||
	       language == MRSyntaxLanguage::Xml ||
	       language == MRSyntaxLanguage::Go || language == MRSyntaxLanguage::Pascal;
}

static constexpr auto kLargeFileViewportWarmupDebounce = std::chrono::milliseconds(180);
static constexpr auto kLargeFileMiniMapEditDebounce = std::chrono::milliseconds(500);
}

void MRFileEditor::continueComputeWarmupIfNeeded(const char *reason) {
	const MRSyntaxDerivedState::WarmupState &warmupState = mSyntaxState.warmupState();
	const MRSyntaxDerivedState::PrefetchState &prefetchState = mSyntaxState.prefetchState();
	const bool pieceTableOnly = pieceTableOnlyPhaseActive();
	const bool foldingEnabled = foldingPipelineEnabled();
	const bool miniMapEnabled = miniMapPipelineEnabled();
	if (shouldTraceLargeFileWarmupDiagnostics()) {
		std::string detail = "reason=" + std::string(reason != nullptr ? reason : "unspecified") + " line_task=" + std::to_string(mLineIndexWarmupTaskId) + " syntax_task=" + std::to_string(warmupState.taskId) +
		                     " prefetch=" + std::to_string(prefetchState.reachedBottomLine) + "/" + std::to_string(prefetchState.targetBottomLine);
		traceLargeFileWarmup(mLastComputeWarmupTrace, "continue", std::move(detail));
	}
	if (pieceTableOnly) {
		if (mLineIndexWarmupTaskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mLineIndexWarmupTaskId));
			clearLineIndexWarmupTask(mLineIndexWarmupTaskId);
		}
		if (!foldingEnabled && mFoldState.warmupState().taskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mFoldState.warmupState().taskId));
			clearFoldWarmupTask(mFoldState.warmupState().taskId);
		}
		if (!miniMapEnabled) {
			const std::uint64_t miniMapTaskId = mMiniMapState.renderer().pendingWarmupTaskId();
			if (miniMapTaskId != 0) {
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(miniMapTaskId));
				clearMiniMapWarmupTask(miniMapTaskId);
			}
		}
		clearSaveNormalizationWarmupTask();
	}
	if (!pieceTableOnly) scheduleLineIndexWarmupIfNeeded();
	if (!syntaxPipelineEnabled()) {
		if (warmupState.taskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(warmupState.taskId));
			clearSyntaxWarmupTask(warmupState.taskId);
		}
		return;
	}
	if (warmupState.taskId == 0 && prefetchState.documentId == mBufferModel.documentId() && prefetchState.version == mBufferModel.version() && prefetchState.language == mBufferModel.language() &&
		prefetchState.reachedBottomLine >= prefetchState.targetBottomLine) {
		if (shouldTraceLargeFileWarmupDiagnostics()) {
			std::string detail = "reason=" + std::string(reason != nullptr ? reason : "unspecified") + " action=skip-syntax-complete prefetch=" + std::to_string(prefetchState.reachedBottomLine) + "/" +
			                     std::to_string(prefetchState.targetBottomLine);
			traceLargeFileWarmup(mLastComputeWarmupTrace, "continue", std::move(detail));
		}
		return;
	}
	scheduleSyntaxWarmupIfNeeded();
}

bool MRFileEditor::applyLineIndexWarmup(const mr::editor::LineIndexWarmupData &warmup, std::size_t expectedVersion) {
	const bool exactLineCountWasKnown = mBufferModel.exactLineCountKnown();
	if (!mBufferModel.adoptLineIndexWarmup(warmup, expectedVersion)) return false;
	if (shouldTraceLargeFileWarmupDiagnostics()) {
		std::string detail = "action=apply checkpoints=" + std::to_string(warmup.checkpoints.size()) + " indexed_line=" + std::to_string(warmup.lazyIndexedLine) + " complete=" +
		                     std::to_string(warmup.lazyLineIndexComplete ? 1 : 0);
		if (mBufferModel.exactLineCountKnown()) detail += " exact_lines=" + std::to_string(mBufferModel.lineCount());
		traceLargeFileWarmup(mLastLineIndexWarmupTrace, "line-index", std::move(detail));
	}
	mLineIndexWarmupTaskId = 0;
	mLineIndexWarmupDocumentId = 0;
	mLineIndexWarmupVersion = 0;
	if (mBufferModel.exactLineCountKnown()) {
		const std::size_t exactLineCount = std::max<std::size_t>(1, mBufferModel.lineCount());
		const std::size_t lastValidTopLine = exactLineCount - 1;
		MRSyntaxDerivedState::PrefetchState &prefetchState = mSyntaxState.prefetchState();
		MRSyntaxDerivedState::WarmupState &warmupState = mSyntaxState.warmupState();

		if (mFoldState.warmupState().taskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mFoldState.warmupState().taskId));
			clearFoldWarmupTask(mFoldState.warmupState().taskId);
		}
		if (prefetchState.targetBottomLine > exactLineCount) prefetchState.targetBottomLine = exactLineCount;
		if (prefetchState.reachedBottomLine > exactLineCount) prefetchState.reachedBottomLine = exactLineCount;
		if (warmupState.topLine > lastValidTopLine) warmupState.topLine = lastValidTopLine;
		if (warmupState.bottomLine > exactLineCount) warmupState.bottomLine = exactLineCount;
		if (warmupState.bottomLine < warmupState.topLine) warmupState.bottomLine = exactLineCount;
		if (!exactLineCountWasKnown) applyMiniMapSignals(mMiniMapState.renderer().invalidate(false, mBufferModel.documentId()));
	}
	if (syntaxPipelineEnabled()) scheduleSyntaxWarmupIfNeeded();
	notifyWindowTaskStateChanged();
	updateMetrics();
	updateIndicator();
	drawView();
	return true;
}

bool MRFileEditor::applySyntaxWarmup(const mr::coprocessor::SyntaxWarmupPayload &warmup, std::size_t expectedVersion, std::uint64_t expectedTaskId) {
	MRSyntaxDerivedState::WarmupState &warmupState = mSyntaxState.warmupState();
	MRSyntaxDerivedState::PrefetchState &prefetchState = mSyntaxState.prefetchState();
	std::map<std::size_t, MRSyntaxCacheEntry> &tokenCache = mSyntaxState.tokenCache();
	if (!syntaxPipelineEnabled()) {
		if (expectedTaskId != 0 && warmupState.taskId == expectedTaskId) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(expectedTaskId));
			clearSyntaxWarmupTask(expectedTaskId);
		}
		return false;
	}
	if (expectedTaskId == 0 || warmupState.taskId != expectedTaskId) return false;
	if (mBufferModel.documentId() != warmupState.documentId || mBufferModel.version() != expectedVersion) return false;
	if (mBufferModel.language() != warmup.language)
		return false;

	const bool statefulSyntax = isStatefulSyntaxLanguage(warmup.language);
	MRSyntaxLineState state = warmup.lines.empty() ? MRSyntaxLineState() : syntaxWarmupInitialState(warmup.lines.front().lineStart);
	std::size_t lineIndex = 0;
	std::size_t warmedBottomLine = 0;
	std::size_t warmedStartLine = 0;

	if (!warmup.lines.empty()) {
		lineIndex = mBufferModel.lineIndex(warmup.lines.front().lineStart);
		warmedStartLine = lineIndex;
		warmedBottomLine = lineIndex + warmup.lines.size();
		if (mBufferModel.exactLineCountKnown()) {
			const std::size_t exactLineCount = std::max<std::size_t>(1, mBufferModel.lineCount());
			if (warmedBottomLine > exactLineCount) warmedBottomLine = exactLineCount;
		}
	}

	for (std::size_t i = 0; i < warmup.lines.size(); ++i) {
		std::map<std::size_t, MRSyntaxCacheEntry>::iterator found = tokenCache.find(warmup.lines[i].lineStart);
		bool stable = false;

		if (found != tokenCache.end() && found->second.stateIn == state) {
			const MRSyntaxLineResult &cachedLine = found->second.syntaxLine;
			const MRSyntaxLineResult &warmLine = warmup.lines[i].syntaxLine;

			if (cachedLine.stateOut == warmLine.stateOut && cachedLine.tokenRuns.size() == warmLine.tokenRuns.size()) {
				stable = true;
				for (std::size_t runIndex = 0; runIndex < cachedLine.tokenRuns.size(); ++runIndex) {
					const MRSyntaxTokenRun &cachedRun = cachedLine.tokenRuns[runIndex];
					const MRSyntaxTokenRun &warmRun = warmLine.tokenRuns[runIndex];

					if (cachedRun.column != warmRun.column || cachedRun.length != warmRun.length || cachedRun.token != warmRun.token) {
						stable = false;
						break;
					}
				}
			}
		}
		if (!stable) tokenCache[warmup.lines[i].lineStart] = MRSyntaxCacheEntry(state, warmup.lines[i].syntaxLine);
		if (statefulSyntax) rememberSyntaxCheckpoint(warmup.lines[i].lineStart, lineIndex, state);
		if (statefulSyntax) state = warmup.lines[i].syntaxLine.stateOut;
		else
			state = MRSyntaxLineState();
		++lineIndex;
	}
	if (warmedBottomLine > warmedStartLine) rememberSyntaxWarmedLineRange(warmedStartLine, warmedBottomLine);

	if (prefetchState.documentId == mBufferModel.documentId() && prefetchState.version == expectedVersion && prefetchState.language == warmup.language && warmedBottomLine > prefetchState.reachedBottomLine)
		prefetchState.reachedBottomLine = warmedBottomLine;
	if (shouldTraceLargeFileWarmupDiagnostics()) {
		std::string detail = "action=apply task=" + std::to_string(expectedTaskId) + " lines=" + std::to_string(warmup.lines.size()) + " warmed_range=" + std::to_string(warmedStartLine) + ".." +
		                     std::to_string(warmedBottomLine) + " warmed_bottom=" + std::to_string(warmedBottomLine) + " prefetch=" + std::to_string(prefetchState.reachedBottomLine) + "/" +
		                     std::to_string(prefetchState.targetBottomLine);
		traceLargeFileWarmup(mLastSyntaxWarmupTrace, "syntax", std::move(detail));
	}
	warmupState = MRSyntaxDerivedState::WarmupState();
	notifyWindowTaskStateChanged();
	drawView();
	return true;
}

bool MRFileEditor::applyMiniMapWarmup(const mr::coprocessor::MiniMapWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId) {
	MRMiniMapRenderer::ApplyWarmupResult result = mMiniMapState.renderer().applyWarmup(payload, expectedVersion, expectedTaskId, mBufferModel.documentId(), mBufferModel.version());
	applyMiniMapSignals(result.signals);
	return result.applied;
}

bool MRFileEditor::applySaveNormalizationWarmup(const mr::coprocessor::SaveNormalizationWarmupPayload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId, double runMicros) {
	if (expectedTaskId == 0 || mSaveNormalizationWarmupTaskId != expectedTaskId) return false;
	if (mBufferModel.documentId() != mSaveNormalizationWarmupDocumentId || mBufferModel.version() != expectedVersion) return false;
	if (mSaveNormalizationWarmupOptionsHash != payload.optionsHash) return false;
	noteSaveNormalizationThroughput(payload.sourceBytes, runMicros);
	clearSaveNormalizationWarmupTask(expectedTaskId);
	return true;
}
std::vector<std::size_t> MRFileEditor::syntaxWarmupLineStarts(std::size_t topLine, int rowCount) const {
	std::vector<std::size_t> lineStarts;
	if (rowCount <= 0) return lineStarts;
	if (mBufferModel.exactLineCountKnown()) {
		const std::size_t exactLineCount = std::max<std::size_t>(1, mBufferModel.lineCount());
		if (topLine >= exactLineCount) return lineStarts;
		const std::size_t remainingLines = exactLineCount - topLine;
		if (remainingLines < static_cast<std::size_t>(rowCount)) rowCount = static_cast<int>(remainingLines);
	}

	std::size_t lineStart = lineStartForIndex(topLine);
	for (int i = 0; i < rowCount; ++i) {
		lineStarts.push_back(lineStart);
		if (i + 1 >= rowCount || lineStart >= mBufferModel.length()) break;
		std::size_t next = mBufferModel.nextLine(lineStart);
		if (next <= lineStart) break;
		lineStart = next;
	}
	return lineStarts;
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
	const std::size_t checkpointStride = 256;

	if (lineIndex != 0 && lineIndex % checkpointStride != 0) return;
	mSyntaxState.checkpoints()[lineIndex] = MRSyntaxCheckpointEntry(lineStart, lineIndex, stateIn);
}

MRSyntaxLineState MRFileEditor::syntaxWarmupInitialState(std::size_t lineStart) const noexcept {
	if (lineStart == 0) return MRSyntaxLineState();

	const std::size_t lineIndex = mBufferModel.lineIndex(lineStart);
	MRSyntaxCheckpointEntry checkpoint;

	if (syntaxCheckpointForLine(lineIndex, checkpoint) && checkpoint.lineIndex == lineIndex && checkpoint.lineStart == lineStart) return checkpoint.stateIn;
	return MRSyntaxLineState();
}

bool MRFileEditor::hasSyntaxTokensForLineStarts(const std::vector<std::size_t> &lineStarts, const MRSyntaxLineState &initialState) const {
	const bool statefulSyntax = isStatefulSyntaxLanguage(mBufferModel.language());
	const std::map<std::size_t, MRSyntaxCacheEntry> &tokenCache = mSyntaxState.tokenCache();
	MRSyntaxLineState state = initialState;

	for (std::size_t i = 0; i < lineStarts.size(); ++i) {
		std::map<std::size_t, MRSyntaxCacheEntry>::const_iterator found = tokenCache.find(lineStarts[i]);

		if (found == tokenCache.end()) return false;
		if (found->second.stateIn != state) return false;
		if (statefulSyntax) state = found->second.syntaxLine.stateOut;
		else
			state = MRSyntaxLineState();
	}
	return true;
}

std::size_t MRFileEditor::syntaxCachedCoveragePrefix(const std::vector<std::size_t> &lineStarts, const MRSyntaxLineState &initialState, MRSyntaxLineState *stateOut) const {
	const bool statefulSyntax = isStatefulSyntaxLanguage(mBufferModel.language());
	const std::map<std::size_t, MRSyntaxCacheEntry> &tokenCache = mSyntaxState.tokenCache();
	MRSyntaxLineState state = initialState;
	std::size_t coveredCount = 0;

	for (; coveredCount < lineStarts.size(); ++coveredCount) {
		std::map<std::size_t, MRSyntaxCacheEntry>::const_iterator found = tokenCache.find(lineStarts[coveredCount]);

		if (found == tokenCache.end()) break;
		if (found->second.stateIn != state) break;
		if (statefulSyntax) state = found->second.syntaxLine.stateOut;
		else
			state = MRSyntaxLineState();
	}
	if (stateOut != nullptr) *stateOut = state;
	return coveredCount;
}

MRSyntaxLineResult MRFileEditor::syntaxLineResultForLine(std::size_t lineStart, const MRSyntaxLineState &previousState) {
	std::map<std::size_t, MRSyntaxCacheEntry> &tokenCache = mSyntaxState.tokenCache();
	std::map<std::size_t, MRSyntaxCacheEntry>::iterator found = tokenCache.find(lineStart);

	if (found != tokenCache.end() && found->second.stateIn == previousState) return found->second.syntaxLine;

	MRSyntaxLineResult syntaxLine = tmrHighlightTextLine(mBufferModel.language(), mBufferModel.lineText(lineStart), previousState);

	tokenCache[lineStart] = MRSyntaxCacheEntry(previousState, syntaxLine);
	return syntaxLine;
}
void MRFileEditor::scheduleLineIndexWarmupIfNeeded() {
	if (pieceTableOnlyPhaseActive()) {
		if (mLineIndexWarmupTaskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mLineIndexWarmupTaskId));
			clearLineIndexWarmupTask(mLineIndexWarmupTaskId);
		}
		return;
	}
	if (useApproximateLargeFileMetrics()) {
		std::uint64_t cancelledTaskId = mLineIndexWarmupTaskId;
		if (cancelledTaskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
			clearLineIndexWarmupTask(cancelledTaskId);
		}
		if (shouldTraceLargeFileWarmupDiagnostics()) {
			std::string detail = "action=skip-approx existing_task=" + std::to_string(cancelledTaskId);
			traceLargeFileWarmup(mLastLineIndexWarmupTrace, "line-index", std::move(detail));
		}
		return;
	}
	if (mSuppressLargeFileLineIndexWarmup) {
		if (shouldTraceLargeFileWarmupDiagnostics()) traceLargeFileWarmup(mLastLineIndexWarmupTrace, "line-index", "action=skip-suppressed");
		return;
	}
	if (!mBufferModel.document().hasMappedOriginal() || mBufferModel.document().exactLineCountKnown()) {
		std::uint64_t cancelledTaskId = mLineIndexWarmupTaskId;
		bool hadTask = cancelledTaskId != 0;
		mLineIndexWarmupTaskId = 0;
		mLineIndexWarmupDocumentId = 0;
		mLineIndexWarmupVersion = 0;
		if (hadTask) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
			notifyWindowTaskStateChanged();
		}
		if (shouldTraceLargeFileWarmupDiagnostics()) {
			std::string detail = "action=skip-exact-known existing_task=" + std::to_string(cancelledTaskId);
			traceLargeFileWarmup(mLastLineIndexWarmupTrace, "line-index", std::move(detail));
		}
		return;
	}
	if (mBufferModel.document().hasMappedOriginal() && mBufferModel.document().addBufferLength() > 0) {
		std::uint64_t cancelledTaskId = mLineIndexWarmupTaskId;
		if (cancelledTaskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
			clearLineIndexWarmupTask(cancelledTaskId);
		}
		if (shouldTraceLargeFileWarmupDiagnostics()) {
			std::string detail = "action=skip-edited-mapped existing_task=" + std::to_string(cancelledTaskId) + " add_buffer=" + std::to_string(mBufferModel.document().addBufferLength());
			traceLargeFileWarmup(mLastLineIndexWarmupTrace, "line-index", std::move(detail));
		}
		return;
	}

	const std::size_t docId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	if (mLineIndexWarmupTaskId != 0 && mLineIndexWarmupDocumentId == docId && mLineIndexWarmupVersion == version) {
		if (shouldTraceLargeFileWarmupDiagnostics()) {
			std::string detail = "action=reuse task=" + std::to_string(mLineIndexWarmupTaskId);
			traceLargeFileWarmup(mLastLineIndexWarmupTrace, "line-index", std::move(detail));
		}
		return;
	}

	MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
	std::uint64_t previousTaskId = mLineIndexWarmupTaskId;
	if (previousTaskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
	mLineIndexWarmupDocumentId = docId;
	mLineIndexWarmupVersion = version;
	mLineIndexWarmupTaskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::LineIndexWarmup, docId, version, lineIndexWarmupTaskLabel(), [snapshot](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
		mr::coprocessor::Result result;
		mr::editor::LineIndexWarmupData warmup;
		static constexpr std::size_t kWorkerStrideBudget = 2;
		result.task = info;
		if (stopToken.stop_requested() || info.cancelRequested()) {
			result.status = mr::coprocessor::TaskStatus::Cancelled;
			return result;
		}
		if (!snapshot.warmLineIndexChunk(warmup, kWorkerStrideBudget, stopToken, info.cancelFlag.get())) {
			result.status = mr::coprocessor::TaskStatus::Cancelled;
			return result;
		}
		result.status = mr::coprocessor::TaskStatus::Completed;
		result.payload = std::make_shared<mr::coprocessor::LineIndexWarmupPayload>(warmup);
		return result;
	});
	if (shouldTraceLargeFileWarmupDiagnostics()) {
		std::string detail = "action=schedule task=" + std::to_string(mLineIndexWarmupTaskId) + " estimated_lines=" + std::to_string(mBufferModel.estimatedLineCount()) + " cursor_line=" +
		                     std::to_string(mBufferModel.lineIndex(mBufferModel.cursor()));
		traceLargeFileWarmup(mLastLineIndexWarmupTrace, "line-index", std::move(detail));
	}
	if (mLineIndexWarmupTaskId != previousTaskId) notifyWindowTaskStateChanged();
}

void MRFileEditor::scheduleSyntaxWarmupIfNeeded() {
	if (!syntaxPipelineEnabled()) {
		resetSyntaxWarmupState(true);
		return;
	}
	const int textRows = visibleTextRows();

	if (mBufferModel.language() == MRSyntaxLanguage::PlainText || textRows <= 0) {
		resetSyntaxWarmupState(true);
		return;
	}

	const std::size_t docId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	const MRSyntaxLanguage language = mBufferModel.language();
	const bool viewportLocalLargeFileWarmup = useApproximateLargeFileMetrics();
	const bool exactLineCountKnown = mBufferModel.exactLineCountKnown();
	const std::size_t exactLineCount = exactLineCountKnown ? std::max<std::size_t>(1, mBufferModel.lineCount()) : 0;
	MRSyntaxDerivedState::WarmupState &warmupState = mSyntaxState.warmupState();
	MRSyntaxDerivedState::PrefetchState &prefetchState = mSyntaxState.prefetchState();
	std::size_t visibleTopLine = static_cast<std::size_t>(std::max(delta.y - (viewportLocalLargeFileWarmup ? 1 : 4), 0));
	std::size_t documentTopLine = documentLineForVisibleLine(visibleTopLine);
	if (exactLineCountKnown && documentTopLine >= exactLineCount) documentTopLine = exactLineCount - 1;
	const int rowBudget = viewportLocalLargeFileWarmup ? std::max(textRows + 4, 8) : std::max(textRows + 8, 8);
	const int backgroundRowBudget = viewportLocalLargeFileWarmup ? rowBudget : rowBudget * 3;
	const bool statefulSyntax = isStatefulSyntaxLanguage(language);
	MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();
	std::vector<std::size_t> visibleLineStarts = syntaxWarmupLineStarts(documentTopLine, rowBudget);
	if (visibleLineStarts.empty()) return;
	mSyntaxState.ensureWarmedLineRangeOwner(docId, language);

	if (viewportLocalLargeFileWarmup || prefetchState.documentId != docId || prefetchState.version != version || prefetchState.language != language) {
		prefetchState.documentId = docId;
		prefetchState.version = version;
		prefetchState.targetBottomLine = documentTopLine;
		prefetchState.reachedBottomLine = documentTopLine;
		prefetchState.language = language;
	}
	if (viewportLocalLargeFileWarmup) {
		prefetchState.targetBottomLine = documentTopLine + static_cast<std::size_t>(backgroundRowBudget);
		prefetchState.reachedBottomLine = documentTopLine;
	} else
		prefetchState.targetBottomLine = std::max(prefetchState.targetBottomLine, documentTopLine + static_cast<std::size_t>(backgroundRowBudget));
	if (exactLineCountKnown) {
		if (prefetchState.targetBottomLine > exactLineCount) prefetchState.targetBottomLine = exactLineCount;
		if (prefetchState.reachedBottomLine > exactLineCount) prefetchState.reachedBottomLine = exactLineCount;
	}
	auto buildSyntaxRequest = [&](std::size_t requestTopLine, int requiredChunkRows, int warmupChunkRows, std::vector<std::size_t> &requiredLineStarts, std::vector<std::size_t> &warmupLineStarts,
	                              MRSyntaxLineState &requiredState, std::size_t &requestBottomLine) {
		requiredLineStarts = syntaxWarmupLineStarts(requestTopLine, requiredChunkRows);
		warmupLineStarts = requiredLineStarts;
		requiredState = MRSyntaxLineState();
		requestBottomLine = requestTopLine + requiredLineStarts.size();
		if (exactLineCountKnown && requestBottomLine > exactLineCount) requestBottomLine = exactLineCount;
		if (requiredLineStarts.empty()) return;

		if (statefulSyntax) {
			std::size_t preludeLines = static_cast<std::size_t>(rowBudget * (viewportLocalLargeFileWarmup ? 2 : 4));
			std::size_t stateTopLine = requestTopLine > preludeLines ? requestTopLine - preludeLines : 0;
			MRSyntaxCheckpointEntry checkpoint;
			bool useCheckpointStart = false;

			if (syntaxCheckpointForLine(requestTopLine, checkpoint) && checkpoint.lineIndex > stateTopLine) {
				stateTopLine = checkpoint.lineIndex;
				requiredState = checkpoint.stateIn;
				useCheckpointStart = true;
			}
			const int requiredRowCount = static_cast<int>(requestTopLine - stateTopLine + requiredLineStarts.size());
			const int warmupRowCount = static_cast<int>(requestTopLine - stateTopLine + static_cast<std::size_t>(warmupChunkRows));

			if (useCheckpointStart) {
				requiredLineStarts.reserve(static_cast<std::size_t>(std::max(requiredRowCount, 0)));
				warmupLineStarts.reserve(static_cast<std::size_t>(std::max(warmupRowCount, 0)));
				std::size_t lineStart = checkpoint.lineStart;
				std::size_t lineIndex = checkpoint.lineIndex;

				requiredLineStarts.clear();
				warmupLineStarts.clear();
				for (int i = 0; i < warmupRowCount; ++i) {
					if (exactLineCountKnown && lineIndex >= exactLineCount) break;
					if (i < requiredRowCount) requiredLineStarts.push_back(lineStart);
					warmupLineStarts.push_back(lineStart);
					++lineIndex;
					if (i + 1 >= warmupRowCount || lineStart >= mBufferModel.length()) break;
					std::size_t next = mBufferModel.nextLine(lineStart);
					if (next <= lineStart) break;
					lineStart = next;
				}
			}
			if (warmupLineStarts == requiredLineStarts) {
				requiredLineStarts = syntaxWarmupLineStarts(stateTopLine, requiredRowCount);
				warmupLineStarts = syntaxWarmupLineStarts(stateTopLine, warmupRowCount);
			}
			const std::size_t preludeCount = requiredLineStarts.size() > static_cast<std::size_t>(requiredChunkRows) ? requiredLineStarts.size() - static_cast<std::size_t>(requiredChunkRows) : 0;
			requestBottomLine = requestTopLine + (warmupLineStarts.size() > preludeCount ? warmupLineStarts.size() - preludeCount : 0);
			if (exactLineCountKnown && requestBottomLine > exactLineCount) requestBottomLine = exactLineCount;
		} else
			requestBottomLine = requestTopLine + warmupLineStarts.size();
		if (exactLineCountKnown && requestBottomLine > exactLineCount) requestBottomLine = exactLineCount;
	};

	std::vector<std::size_t> requiredLineStarts;
	std::vector<std::size_t> warmupLineStarts;
	MRSyntaxLineState requiredState;
	std::size_t bottomLine = 0;
	std::size_t requestTopLine = visibleTopLine;
	std::size_t trimmedCoveredPrefixCount = 0;
	buildSyntaxRequest(visibleTopLine, static_cast<int>(visibleLineStarts.size()), backgroundRowBudget, requiredLineStarts, warmupLineStarts, requiredState, bottomLine);
	const bool visibleRangeCovered = syntaxWarmedLineRangeCovered(requestTopLine, bottomLine);
	MRSyntaxLineState visibleCoveredState = requiredState;
	const std::size_t visibleCoveredPrefixCount = syntaxCachedCoveragePrefix(requiredLineStarts, requiredState, &visibleCoveredState);
	const bool visibleCacheComplete = visibleRangeCovered && visibleCoveredPrefixCount == requiredLineStarts.size();

	if (!visibleCacheComplete && visibleCoveredPrefixCount > 0) {
		trimmedCoveredPrefixCount = visibleCoveredPrefixCount;
		requiredState = visibleCoveredState;
		requiredLineStarts.erase(requiredLineStarts.begin(), requiredLineStarts.begin() + static_cast<std::ptrdiff_t>(visibleCoveredPrefixCount));
		if (visibleCoveredPrefixCount >= warmupLineStarts.size()) warmupLineStarts.clear();
		else
			warmupLineStarts.erase(warmupLineStarts.begin(), warmupLineStarts.begin() + static_cast<std::ptrdiff_t>(visibleCoveredPrefixCount));
		if (!requiredLineStarts.empty()) requestTopLine = mBufferModel.lineIndex(requiredLineStarts.front());
	}

	if (viewportLocalLargeFileWarmup && visibleCacheComplete) {
		std::uint64_t previousTaskId = warmupState.taskId;
		bool hadTask = previousTaskId != 0;
		warmupState = MRSyntaxDerivedState::WarmupState();
		warmupState.documentId = docId;
		warmupState.version = version;
		warmupState.topLine = visibleTopLine;
		warmupState.bottomLine = bottomLine;
		warmupState.language = language;
		prefetchState.reachedBottomLine = std::max(prefetchState.reachedBottomLine, bottomLine);
		if (exactLineCountKnown && prefetchState.reachedBottomLine > exactLineCount) prefetchState.reachedBottomLine = exactLineCount;
		if (hadTask) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
			notifyWindowTaskStateChanged();
		}
		if (shouldTraceLargeFileWarmupDiagnostics()) {
			std::string detail = "action=cancel-visible-complete previous_task=" + std::to_string(previousTaskId) + " top=" + std::to_string(visibleTopLine) + " bottom=" + std::to_string(bottomLine) +
			                     " prefetch=" + std::to_string(prefetchState.reachedBottomLine) + "/" + std::to_string(prefetchState.targetBottomLine);
			traceLargeFileWarmup(mLastSyntaxWarmupTrace, "syntax", std::move(detail));
		}
		return;
	}

	if (visibleCacheComplete) {
		std::size_t prefetchCursor = std::max(visibleTopLine, prefetchState.reachedBottomLine);

		while (prefetchCursor < prefetchState.targetBottomLine) {
			std::vector<std::size_t> continuationRequiredLineStarts;
			std::vector<std::size_t> continuationWarmupLineStarts;
			MRSyntaxLineState continuationState;
			const int continuationRows = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(rowBudget), prefetchState.targetBottomLine - prefetchCursor));
			std::size_t continuationBottomLine = 0;

			if (continuationRows <= 0) break;
			buildSyntaxRequest(prefetchCursor, continuationRows, continuationRows, continuationRequiredLineStarts, continuationWarmupLineStarts, continuationState, continuationBottomLine);
			if (continuationRequiredLineStarts.empty()) {
				std::size_t eofBottomLine = prefetchCursor;
				if (mBufferModel.exactLineCountKnown()) eofBottomLine = std::max<std::size_t>(1, mBufferModel.lineCount());
				prefetchState.reachedBottomLine = std::max(prefetchState.reachedBottomLine, eofBottomLine);
				if (prefetchState.targetBottomLine > eofBottomLine) prefetchState.targetBottomLine = eofBottomLine;
				break;
			}
			const bool continuationRangeCovered = syntaxWarmedLineRangeCovered(prefetchCursor, continuationBottomLine);
			MRSyntaxLineState continuationCoveredState = continuationState;
			const std::size_t continuationCoveredPrefixCount = syntaxCachedCoveragePrefix(continuationRequiredLineStarts, continuationState, &continuationCoveredState);

			if (!continuationRangeCovered || continuationCoveredPrefixCount != continuationRequiredLineStarts.size()) {
				trimmedCoveredPrefixCount = continuationCoveredPrefixCount;
				if (continuationCoveredPrefixCount > 0) {
					continuationState = continuationCoveredState;
					continuationRequiredLineStarts.erase(continuationRequiredLineStarts.begin(),
					                                     continuationRequiredLineStarts.begin() + static_cast<std::ptrdiff_t>(continuationCoveredPrefixCount));
					if (continuationCoveredPrefixCount >= continuationWarmupLineStarts.size()) continuationWarmupLineStarts.clear();
					else
						continuationWarmupLineStarts.erase(continuationWarmupLineStarts.begin(),
						                                   continuationWarmupLineStarts.begin() + static_cast<std::ptrdiff_t>(continuationCoveredPrefixCount));
				}
				if (continuationRequiredLineStarts.empty()) {
					prefetchState.reachedBottomLine = std::max(prefetchState.reachedBottomLine, continuationBottomLine);
					if (exactLineCountKnown && prefetchState.reachedBottomLine > exactLineCount) prefetchState.reachedBottomLine = exactLineCount;
					prefetchCursor = prefetchState.reachedBottomLine;
					continue;
				}
				requestTopLine = mBufferModel.lineIndex(continuationRequiredLineStarts.front());
				requiredLineStarts.swap(continuationRequiredLineStarts);
				warmupLineStarts.swap(continuationWarmupLineStarts);
				requiredState = continuationState;
				bottomLine = continuationBottomLine;
				if (exactLineCountKnown && bottomLine > exactLineCount) bottomLine = exactLineCount;
				break;
			}
			if (continuationBottomLine <= prefetchCursor) break;
			prefetchState.reachedBottomLine = std::max(prefetchState.reachedBottomLine, continuationBottomLine);
			if (exactLineCountKnown && prefetchState.reachedBottomLine > exactLineCount) prefetchState.reachedBottomLine = exactLineCount;
			prefetchCursor = prefetchState.reachedBottomLine;
		}

		if (prefetchState.reachedBottomLine >= prefetchState.targetBottomLine) {
			std::uint64_t previousTaskId = warmupState.taskId;
			bool hadTask = previousTaskId != 0;
			warmupState = MRSyntaxDerivedState::WarmupState();
			warmupState.documentId = docId;
			warmupState.version = version;
			warmupState.topLine = visibleTopLine;
			warmupState.bottomLine = bottomLine;
			warmupState.language = language;
			if (hadTask) {
				static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
				notifyWindowTaskStateChanged();
			}
			if (shouldTraceLargeFileWarmupDiagnostics()) {
				std::string detail = "action=skip-prefetch-complete previous_task=" + std::to_string(previousTaskId) + " prefetch=" + std::to_string(prefetchState.reachedBottomLine) + "/" +
				                     std::to_string(prefetchState.targetBottomLine);
				traceLargeFileWarmup(mLastSyntaxWarmupTrace, "syntax", std::move(detail));
			}
			return;
		}
	} else
		prefetchState.reachedBottomLine = visibleTopLine;
	if (exactLineCountKnown && prefetchState.reachedBottomLine > exactLineCount) prefetchState.reachedBottomLine = exactLineCount;

	if (visibleCacheComplete && bottomLine <= visibleTopLine) return;
	if (warmupState.taskId != 0 && warmupState.documentId == docId && warmupState.version == version && warmupState.language == language && requestTopLine >= warmupState.topLine &&
		bottomLine <= warmupState.bottomLine) {
		if (shouldTraceLargeFileWarmupDiagnostics()) {
			std::string detail = "action=reuse task=" + std::to_string(warmupState.taskId) + " top=" + std::to_string(requestTopLine) + " bottom=" + std::to_string(bottomLine);
			traceLargeFileWarmup(mLastSyntaxWarmupTrace, "syntax", std::move(detail));
		}
		return;
	}

	if (visibleCacheComplete && requiredLineStarts.empty()) {
		std::uint64_t previousTaskId = warmupState.taskId;
		bool hadTask = previousTaskId != 0;
		warmupState = MRSyntaxDerivedState::WarmupState();
		warmupState.documentId = docId;
		warmupState.version = version;
		warmupState.topLine = visibleTopLine;
		warmupState.bottomLine = bottomLine;
		warmupState.language = language;
		if (hadTask) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
			notifyWindowTaskStateChanged();
		}
		if (shouldTraceLargeFileWarmupDiagnostics()) traceLargeFileWarmup(mLastSyntaxWarmupTrace, "syntax", "action=skip-empty-request");
		return;
	}

	if (viewportLocalLargeFileWarmup && warmupState.taskId != 0 && warmupState.documentId == docId && warmupState.version == version && warmupState.language == language) {
		if (shouldTraceLargeFileWarmupDiagnostics()) {
			std::string detail = "action=defer-pending task=" + std::to_string(warmupState.taskId) + " top=" + std::to_string(requestTopLine) + " bottom=" + std::to_string(bottomLine) +
			                     " prefetch=" + std::to_string(prefetchState.reachedBottomLine) + "/" + std::to_string(prefetchState.targetBottomLine);
			traceLargeFileWarmup(mLastSyntaxWarmupTrace, "syntax", std::move(detail));
		}
		return;
	}

	if (viewportLocalLargeFileWarmup && warmupState.taskId == 0 && mSyntaxState.lastScheduledTopLine() == requestTopLine && mSyntaxState.lastScheduledBottomLine() == bottomLine) {
		const auto now = std::chrono::steady_clock::now();
		if (mSyntaxState.lastScheduledAt() != std::chrono::steady_clock::time_point() && now - mSyntaxState.lastScheduledAt() < kLargeFileViewportWarmupDebounce) {
			if (shouldTraceLargeFileWarmupDiagnostics()) {
				std::string detail = "action=defer-burst top=" + std::to_string(requestTopLine) + " bottom=" + std::to_string(bottomLine) + " prefetch=" +
				                     std::to_string(prefetchState.reachedBottomLine) + "/" + std::to_string(prefetchState.targetBottomLine);
				traceLargeFileWarmup(mLastSyntaxWarmupTrace, "syntax", std::move(detail));
			}
			return;
		}
	}

	std::uint64_t previousTaskId = warmupState.taskId;
	if (previousTaskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
	warmupState.documentId = docId;
	warmupState.version = version;
	warmupState.topLine = requestTopLine;
	warmupState.bottomLine = bottomLine;
	warmupState.language = language;
	mSyntaxState.rememberScheduledRequest(requestTopLine, bottomLine, std::chrono::steady_clock::now());
	warmupState.taskId =
	    mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::SyntaxWarmup, docId, version, syntaxWarmupTaskLabel(),
	                                                [snapshot, language, warmupLineStarts, statefulSyntax, requiredState](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
		mr::coprocessor::Result result;
		std::vector<mr::coprocessor::SyntaxWarmLine> warmed;
		auto shouldStop = [&]() noexcept { return stopToken.stop_requested() || info.cancelRequested(); };
		result.task = info;
		if (shouldStop()) {
			result.status = mr::coprocessor::TaskStatus::Cancelled;
			return result;
		}
		warmed.reserve(warmupLineStarts.size());
		MRSyntaxLineState state = statefulSyntax ? requiredState : MRSyntaxLineState();
		for (std::size_t i = 0; i < warmupLineStarts.size(); ++i) {
			if (shouldStop()) {
				result.status = mr::coprocessor::TaskStatus::Cancelled;
				return result;
			}
			MRSyntaxLineResult syntaxLine = tmrHighlightTextLine(language, snapshot.lineText(warmupLineStarts[i]), statefulSyntax ? state : MRSyntaxLineState());
			if (statefulSyntax) state = syntaxLine.stateOut;
			warmed.push_back(mr::coprocessor::SyntaxWarmLine(warmupLineStarts[i], std::move(syntaxLine)));
		}
		result.status = mr::coprocessor::TaskStatus::Completed;
		result.payload = std::make_shared<mr::coprocessor::SyntaxWarmupPayload>(language, std::move(warmed));
		return result;
		});
	if (shouldTraceLargeFileWarmupDiagnostics()) {
		std::string detail = "action=schedule task=" + std::to_string(warmupState.taskId) + " top=" + std::to_string(requestTopLine) + " bottom=" + std::to_string(bottomLine) + " lines=" +
		                     std::to_string(warmupLineStarts.size()) + " skip_prefix=" + std::to_string(trimmedCoveredPrefixCount) + " prefetch=" + std::to_string(prefetchState.reachedBottomLine) +
		                     "/" + std::to_string(prefetchState.targetBottomLine);
		traceLargeFileWarmup(mLastSyntaxWarmupTrace, "syntax", std::move(detail));
	}
	if (warmupState.taskId != previousTaskId) notifyWindowTaskStateChanged();
}
void MRFileEditor::scheduleSaveNormalizationWarmupIfNeeded() {
	if (pieceTableOnlyPhaseActive()) {
		if (mSaveNormalizationWarmupTaskId != 0) {
			std::uint64_t cancelledTaskId = mSaveNormalizationWarmupTaskId;
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
			clearSaveNormalizationWarmupTask(cancelledTaskId);
		}
		invalidateSaveNormalizationCache();
		return;
	}
	invalidateSaveNormalizationCache();
	if (mSaveNormalizationWarmupTaskId == 0) return;
	std::uint64_t cancelledTaskId = mSaveNormalizationWarmupTaskId;
	static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
	clearSaveNormalizationWarmupTask(cancelledTaskId);
}
void MRFileEditor::refreshSyntaxContext() {
	MRSyntaxLanguage oldLanguage = mBufferModel.language();
	const bool oldAutomatic = mBufferModel.languageAutomatic();
	const std::string codeLanguage = effectiveCodeLanguageSetting();
	mBufferModel.setSyntaxContext(hasPersistentFileName() ? fileName : "", mSyntaxTitleHint, codeLanguage);
	if (mBufferModel.language() != oldLanguage) resetSyntaxWarmupState(true);
	if (mBufferModel.language() != oldLanguage) {
		if (mFoldState.warmupState().taskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mFoldState.warmupState().taskId));
			clearFoldWarmupTask(mFoldState.warmupState().taskId);
		}
		mFoldState.clearClosedFolds();
		invalidateFoldCache();
	}
	if (mBufferModel.languageAutomatic() != oldAutomatic) drawView();
}

bool MRFileEditor::pieceTableOnlyPhaseActive() const noexcept {
	return true;
}

std::string MRFileEditor::effectiveCodeLanguageSetting() const {
	std::string codeLanguage = configuredEditSetupSettings().codeLanguage;

	if (hasPersistentFileName()) {
		MREditSetupSettings effective;
		if (effectiveEditSetupSettingsForPath(fileName, effective, nullptr)) codeLanguage = effective.codeLanguage;
	}
	return upperAscii(trimAscii(codeLanguage));
}

bool MRFileEditor::languageFeaturesEnabled() const {
	const std::string codeLanguage = effectiveCodeLanguageSetting();

	if (codeLanguage.empty() || codeLanguage == "NONE") return false;
	return mBufferModel.language() != MRSyntaxLanguage::PlainText;
}

bool MRFileEditor::syntaxPipelineEnabled() const {
	return languageFeaturesEnabled();
}

bool MRFileEditor::foldingPipelineEnabled() const {
	return languageFeaturesEnabled();
}

bool MRFileEditor::miniMapPipelineEnabled() const noexcept {
	return true;
}

void MRFileEditor::resetSyntaxWarmupState(bool clearCache) noexcept {
	std::uint64_t cancelledTaskId = mSyntaxState.warmupState().taskId;
	bool hadTask = cancelledTaskId != 0;
	mSyntaxState.resetState(clearCache);
	if (hadTask) {
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
		notifyWindowTaskStateChanged();
	}
}

void MRFileEditor::invalidateSyntaxCacheFromLineStart(std::size_t lineStart) noexcept {
	std::map<std::size_t, MRSyntaxCacheEntry> &tokenCache = mSyntaxState.tokenCache();
	std::map<std::size_t, MRSyntaxCheckpointEntry> &checkpoints = mSyntaxState.checkpoints();
	MRSyntaxDerivedState::PrefetchState &prefetchState = mSyntaxState.prefetchState();
	MRSyntaxDerivedState::WarmupState &warmupState = mSyntaxState.warmupState();
	std::map<std::size_t, MRSyntaxCacheEntry>::iterator firstInvalid = tokenCache.lower_bound(lineStart);
	std::size_t lineIndex = mBufferModel.lineIndex(lineStart);
	std::map<std::size_t, MRSyntaxCheckpointEntry>::iterator firstInvalidCheckpoint = checkpoints.lower_bound(lineIndex);

	if (firstInvalid != tokenCache.end()) tokenCache.erase(firstInvalid, tokenCache.end());
	if (firstInvalidCheckpoint != checkpoints.end()) checkpoints.erase(firstInvalidCheckpoint, checkpoints.end());
	invalidateSyntaxWarmedLineRangesFrom(lineIndex);
	if (prefetchState.documentId == mBufferModel.documentId() && prefetchState.version == mBufferModel.version()) {
		if (prefetchState.reachedBottomLine > lineIndex) prefetchState.reachedBottomLine = lineIndex;
		if (prefetchState.targetBottomLine < lineIndex) prefetchState.targetBottomLine = lineIndex;
	}
	if (warmupState.taskId != 0 && warmupState.documentId == mBufferModel.documentId() && warmupState.version == mBufferModel.version() && lineIndex <= warmupState.bottomLine) {
		const std::uint64_t cancelledTaskId = warmupState.taskId;
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
		clearSyntaxWarmupTask(cancelledTaskId);
	}
}

void MRFileEditor::clearSyntaxWarmedLineRanges() noexcept {
	mSyntaxState.clearWarmedLineRanges();
}

void MRFileEditor::rememberSyntaxWarmedLineRange(std::size_t startLine, std::size_t endLine) noexcept {
	mSyntaxState.rememberWarmedLineRange(mBufferModel.documentId(), mBufferModel.language(), startLine, endLine);
}

void MRFileEditor::invalidateSyntaxWarmedLineRangesFrom(std::size_t lineIndex) noexcept {
	mSyntaxState.invalidateWarmedLineRangesFrom(mBufferModel.documentId(), mBufferModel.language(), lineIndex);
}

bool MRFileEditor::syntaxWarmedLineRangeCovered(std::size_t startLine, std::size_t endLine) const noexcept {
	return mSyntaxState.warmedLineRangeCovered(mBufferModel.documentId(), mBufferModel.language(), startLine, endLine);
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
