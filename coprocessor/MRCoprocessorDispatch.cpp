#define Uses_TGroup
#define Uses_TProgram
#define Uses_TScrollBar
#include <tvision/tv.h>

#include "MRCoprocessorDispatch.hpp"
#include "MRCoprocessorDeferredPlayback.hpp"
#include "MRCoprocessorBentoDispatch.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <unistd.h>

#include "MRPerformance.hpp"
#include "MRWindowCommands.hpp"

#include "../app/MRCommands.hpp"
#include "../app/MRUpdate.hpp"
#include "../app/commands/MRExternalCommand.hpp"
#include "../app/commands/MRBuildCommands.hpp"
#include "../app/services/MRGdbSession.hpp"
#include "../app/MRMacroDebuggerCommandRoute.hpp"
#include "../app/router/MRCommandRouterGit.hpp"
#include "../app/router/MRCommandRouterSearch.hpp"
#include "../app/router/MRCommandRouterSearchCore.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../mrmac/MRMacroExecutionSession.hpp"
#include "../mrmac/MRVM.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../mrmac/vm/MRVMRuntimeState.hpp"
#include "../mrmac/vm/MRVMValue.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRFileEditor/MRFileEditor.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRBentoBox/MRBentoBox.hpp"
#include "../ui/MRBentoHexEditor/panes/MRHexPaneWindow.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {
const char *kLineIndexWarmAction = "Line index warming";
const char *kDisplayWidthWarmAction = "Display width warming";
const char *kSyntaxWarmAction = "Syntax warming";
const char *kFoldWarmAction = "Fold warming";
const char *kMiniMapRenderAction = "Mini map rendering";
const char *kHexPaneProjectionAction = "Hex pane projection";
MREditWindow *textWarmupOwnerWindow(const mr::coprocessor::Result &result) noexcept {
	switch (result.task.executionOwnerKind) {
		case mr::coprocessor::ExecutionOwnerKind::EditorWindow:
		case mr::coprocessor::ExecutionOwnerKind::BentoPane:
			return findEditWindowByBufferId(static_cast<int>(result.task.executionOwnerLocalId));
		default:
			return nullptr;
	}
}

bool traceWarmupCancelEnabled() noexcept {
	static const bool enabled = []() noexcept {
		const char *value = std::getenv("MR_TRACE_WARMUP_CANCEL");
		return value != nullptr && value[0] == '1' && value[1] == '\0';
	}();
	return enabled;
}

const char *warmupTaskKindName(mr::coprocessor::TaskKind kind) noexcept {
	switch (kind) {
		case mr::coprocessor::TaskKind::LineIndexWarmup:
			return "LineIndexWarmup";
		case mr::coprocessor::TaskKind::DisplayWidthWarmup:
			return "DisplayWidthWarmup";
		case mr::coprocessor::TaskKind::SyntaxWarmup:
			return "SyntaxWarmup";
		case mr::coprocessor::TaskKind::MiniMapWarmup:
			return "MiniMapWarmup";
		default:
			break;
	}
	return nullptr;
}

void logWarmupCancelFinish(const mr::coprocessor::Result &result) {
	if (!traceWarmupCancelEnabled()) return;
	const char *kind = warmupTaskKindName(result.task.kind);
	if (kind == nullptr) return;

	std::ostringstream line;
	line << "WARMUP-CANCEL finish kind=" << kind << " task=" << result.task.id;
	if (result.cancelled()) line << " status=cancelled";
	else if (result.completed())
		line << " status=completed";
	else if (result.failed())
		line << " status=failed";
	else
		line << " status=unknown";
	mrTraceDiagnosticMessage(line.str());
}

const char *coprocessorLaneName(mr::coprocessor::Lane lane) {
	switch (lane) {
		case mr::coprocessor::Lane::Io:
			return "io";
		case mr::coprocessor::Lane::MiniMap:
			return "minimap";
		case mr::coprocessor::Lane::Macro:
			return "macro";
		case mr::coprocessor::Lane::Extern:
			return "extern";
		case mr::coprocessor::Lane::Compute:
		default:
			return "compute";
	}
}

std::string communicationExitLine(const mr::coprocessor::ExternalIoFinishedPayload &payload) {
	std::ostringstream out;

	out << "\n[process ";
	if (payload.signaled) out << "terminated by signal " << payload.signalNumber;
	else
		out << "exited with code " << payload.exitCode;
	out << "]\n";
	return out.str();
}

std::string communicationDividerStatus(const mr::coprocessor::ExternalIoFinishedPayload &payload) {
	std::ostringstream out;

	if (payload.signaled) out << "signal " << payload.signalNumber;
	else
		out << "exit code: " << payload.exitCode;
	return out.str();
}

MRBuildHookContext buildHookContextFromPayload(const mr::coprocessor::ExternalIoFinishedPayload &payload) {
	MRBuildHookContext context;

	context.sourcePath = payload.buildSourcePath;
	context.sourceDir = payload.buildSourceDir;
	context.sourceFile = payload.buildSourceFile;
	context.sourceStem = payload.buildSourceStem;
	context.outputPath = payload.buildOutputPath;
	context.pdfPath = payload.buildPdfPath;
	context.profileId = payload.buildProfileId;
	context.profileName = payload.buildProfileName;
	context.toolchain = payload.buildToolchain;
	context.postBuildMacro = payload.postBuildMacro;
	return context;
}

void runExternalIoPostBuildMacro(const mr::coprocessor::Result &result, const mr::coprocessor::ExternalIoFinishedPayload &payload) {
	std::string statusText;
	std::string errorText;
	std::string macroError;
	int exitStatus = payload.exitCode;

	if (payload.postBuildMacro.empty()) return;
	if (result.cancelled()) {
		statusText = "CANCELLED";
		if (exitStatus == 0) exitStatus = -2;
		errorText = "cancelled";
	} else if (result.failed()) {
		statusText = "FAILED";
		if (exitStatus == 0) exitStatus = -1;
		errorText = result.error;
	} else if (payload.signaled) {
		statusText = "FAILED";
		exitStatus = -1;
		errorText = "signal " + std::to_string(payload.signalNumber);
	} else if (payload.exitCode == 0)
		statusText = "SUCCESS";
	else {
		statusText = "FAILED";
		errorText = "exit code " + std::to_string(payload.exitCode);
	}
	if (!runBuildHookMacro(payload.postBuildMacro, buildHookContextFromPayload(payload), exitStatus, statusText, errorText, &macroError)) {
		const std::string line = macroError.empty() ? "Post build macro failed." : macroError;
		mr::messageline::postAutoTimed(mr::messageline::Owner::MacroMarquee, line, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
	}
}

void setSplitDiagnosticsStatusForOutput(MREditWindow *outputWindow, const char *status) {
	MRBentoBox *split = outputWindow != nullptr ? dynamic_cast<MRBentoBox *>(outputWindow->owner) : nullptr;

	if (split != nullptr && split->buildOutputPane() == outputWindow) split->setCompilerOutputStatus(status);
}

std::string macroDisplayName(const mr::coprocessor::TaskInfo &task, const char *payloadName = nullptr) {
	if (payloadName != nullptr && *payloadName != '\0') return payloadName;
	if (task.label.rfind("macro: ", 0) == 0) return task.label.substr(7);
	if (!task.label.empty()) return task.label;
	return "macro";
}

std::string externalIoDisplayName(const mr::coprocessor::TaskInfo &task) {
	if (task.label.rfind("external-io: ", 0) == 0) return task.label.substr(13);
	if (!task.label.empty()) return task.label;
	return "external command";
}

std::string lineRangeText(std::size_t topLine, std::size_t bottomLineExclusive) {
	if (bottomLineExclusive <= topLine) return std::string();
	return "lines " + std::to_string(topLine + 1) + "-" + std::to_string(bottomLineExclusive);
}

std::string taskLabelLineRange(const mr::coprocessor::TaskInfo &task) {
	const std::string marker = " lines ";
	const std::size_t markerPos = task.label.find(marker);

	if (markerPos == std::string::npos) return std::string();
	return task.label.substr(markerPos + 1);
}

std::string detailWithLineRange(const std::string &detail, const std::string &lineRange) {
	if (lineRange.empty()) return detail;
	if (detail.empty()) return lineRange;
	return detail + " " + lineRange;
}

std::string syntaxLineRangeDetail(const mr::coprocessor::SyntaxWarmupPayload &syntax, MRFileEditor *editor, const mr::coprocessor::TaskInfo &task) {
	if (syntax.endLine <= syntax.startLine) return taskLabelLineRange(task);
	return lineRangeText(syntax.startLine, syntax.endLine);
}

std::string miniMapLineRangeDetail(const mr::coprocessor::MiniMapWarmupPayload &miniMap) {
	return lineRangeText(miniMap.windowStartLine, miniMap.windowStartLine + miniMap.windowLineCount);
}

void recordTaskPerformance(const mr::coprocessor::Result &result, const std::string &action, MREditWindow *win, std::size_t documentId, std::size_t bytes, const std::string &detail,
                           bool derivedStateApplied = false) {
	mr::performance::Outcome outcome = result.failed() ? mr::performance::Outcome::Failed : (result.cancelled() ? mr::performance::Outcome::Cancelled : mr::performance::Outcome::Completed);
	mr::performance::recordBackgroundEvent(result.task.lane, outcome, result.timing, action, win != nullptr ? static_cast<std::size_t>(win->bufferId()) : 0, documentId, bytes, detail, derivedStateApplied);
}

void handleFileCompareResult(const mr::coprocessor::Result &result) {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	MRBentoBox *target = nullptr;
	bool applied = false;
	std::size_t bytes = 0;

	for (MREditWindow *window : windows) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);
		if (bentoBox == nullptr || !bentoBox->isFileCompareBox()) continue;
		if (bentoBox->applyFileCompareResult(result)) {
			target = bentoBox;
			applied = result.completed();
			break;
		}
	}
	if (result.completed()) {
		const mr::coprocessor::FileComparePayload *payload = dynamic_cast<const mr::coprocessor::FileComparePayload *>(result.payload.get());
		if (payload != nullptr) bytes = payload->originalLineCount + payload->compareLineCount;
	}
	recordTaskPerformance(result, "File compare", target, result.task.documentId, bytes, result.task.label, applied);
	mr::coprocessor::globalCoprocessor().noteResultAdoption(result, applied);
	if (target == nullptr && result.failed()) mrLogMessage((std::string("File compare failed: ") + result.error).c_str());
}

void recordMacroPerformance(const mr::coprocessor::Result &result, MREditWindow *win, std::size_t documentId, std::size_t bytes, const std::string &detail, mr::performance::Outcome outcome = mr::performance::Outcome::Completed) {
	if (outcome == mr::performance::Outcome::Completed) {
		recordTaskPerformance(result, "Background macro", win, documentId, bytes, detail);
		return;
	}
	mr::performance::recordBackgroundEvent(result.task.lane, outcome, result.timing, "Background macro", win != nullptr ? static_cast<std::size_t>(win->bufferId()) : 0, documentId, bytes, detail);
}

std::size_t applyMacroExecUiCommandRequests(const std::vector<MRMacroExecUiCommandRequest> &requests) {
	std::size_t accepted = 0;

	for (const MRMacroExecUiCommandRequest &request : requests) {
		if (mrvmApplyExecUiCommandRequest(request)) ++accepted;
	}
	return accepted;
}

std::string formatTimingSummary(const mr::coprocessor::TaskTiming &timing) {
	return " [q " + mr::performance::formatDuration(timing.queueMs()) + ", run " + mr::performance::formatDuration(timing.runMs()) + ", total " + mr::performance::formatDuration(timing.totalMs()) + "]";
}

MacroCommitConflictSnapshot captureMacroCommitConflictSnapshot(MREditWindow *window, MRFileEditor *editor) {
	MacroCommitConflictSnapshot snapshot;

	if (window == nullptr || editor == nullptr) return snapshot;
	snapshot.cursorOffset = editor->cursorOffset();
	snapshot.selectionStart = editor->selectionStartOffset();
	snapshot.selectionEnd = editor->selectionEndOffset();
	snapshot.blockMode = window->blockStatus();
	snapshot.blockMarkingOn = window->isBlockMarking();
	snapshot.blockAnchor = window->blockAnchorPtr();
	snapshot.blockEnd = window->blockEffectiveEndPtr();
	snapshot.insertMode = editor->insertModeEnabled();
	snapshot.indentLevel = window->indentLevel();
	snapshot.fileName = window->currentFileName();
	snapshot.fileChanged = window->isFileChanged();
	mrvmUiCopyGlobals(snapshot.globalOrder, snapshot.globalInts, snapshot.globalStrings);
	snapshot.lastSearchValid = mrvmUiCopyWindowLastSearch(window, snapshot.fileName, snapshot.lastSearchStart, snapshot.lastSearchEnd, snapshot.lastSearchCursor);
	mrvmUiCopyRuntimeOptions(snapshot.ignoreCase, snapshot.tabExpand);
	snapshot.markStack = mrvmUiCopyWindowMarkStack(window);
	snapshot.bufferId = window->bufferId();
	snapshot.linkStatus = mrvmUiLinkStatus(window);
	snapshot.windowCount = mrvmUiWindowCount();
	snapshot.windowGeometryValid = mrvmUiWindowGeometry(window, snapshot.windowX1, snapshot.windowY1, snapshot.windowX2, snapshot.windowY2);
	return snapshot;
}

const char *macroCommitConflictMarker(const MacroCommitConflictSnapshot &base, const MacroCommitConflictSnapshot &live) {
	if (live.bufferId != base.bufferId) return "buffer";
	if (live.cursorOffset != base.cursorOffset) return "cursor";
	if (live.selectionStart != base.selectionStart || live.selectionEnd != base.selectionEnd) return "selection";
	if (live.blockMode != base.blockMode || live.blockMarkingOn != base.blockMarkingOn || live.blockAnchor != base.blockAnchor || live.blockEnd != base.blockEnd) return "block";
	if (live.insertMode != base.insertMode) return "insert-mode";
	if (live.indentLevel != base.indentLevel) return "indent-level";
	if (live.fileName != base.fileName) return "file-name";
	if (live.fileChanged != base.fileChanged) return "file-changed";
	if (live.globalOrder != base.globalOrder || live.globalInts != base.globalInts || live.globalStrings != base.globalStrings) return "globals";
	if (live.lastSearchValid != base.lastSearchValid) return "last-search";
	if (live.lastSearchValid && (live.lastSearchStart != base.lastSearchStart || live.lastSearchEnd != base.lastSearchEnd || live.lastSearchCursor != base.lastSearchCursor)) return "last-search";
	if (live.ignoreCase != base.ignoreCase || live.tabExpand != base.tabExpand) return "runtime-options";
	if (live.markStack != base.markStack) return "mark-stack";
	if (live.linkStatus != base.linkStatus) return "link-status";
	if (live.windowCount != base.windowCount) return "window-count";
	if (live.windowGeometryValid != base.windowGeometryValid) return "window-geometry";
	if (live.windowGeometryValid && (live.windowX1 != base.windowX1 || live.windowY1 != base.windowY1 || live.windowX2 != base.windowX2 || live.windowY2 != base.windowY2)) return "window-geometry";
	return nullptr;
}

long long roundedMilliseconds(double valueMs) {
	if (valueMs <= 0.0) return 0;
	return static_cast<long long>(valueMs + 0.5);
}

struct AudioPlayerInvocationSpec {
	const char *name;
	const char *args[4];
};

std::string fileNameFromPath(const std::string &path) {
	const std::size_t slash = path.find_last_of('/');

	return slash == std::string::npos ? path : path.substr(slash + 1);
}

const AudioPlayerInvocationSpec *audioPlayerInvocationSpec(const std::string &name) {
	static const AudioPlayerInvocationSpec specs[] = {
	    {"ffplay", {"-nodisp", "-autoexit", "-loglevel", "quiet"}},
	    {"mpv", {"--no-video", "--really-quiet", nullptr, nullptr}},
	    {"mplayer", {"-really-quiet", nullptr, nullptr, nullptr}},
	    {"cvlc", {"--play-and-exit", "--intf", "dummy", nullptr}},
	    {"canberra-gtk-play", {"-f", nullptr, nullptr, nullptr}},
	};

	for (const AudioPlayerInvocationSpec &spec : specs)
		if (name == spec.name) return &spec;
	return nullptr;
}

void postMiniMapHeroEvent(const mr::coprocessor::TaskTiming &timing, const mr::coprocessor::MiniMapWarmupPayload &payload, std::size_t fileBytes) {
	if (payload.totalLines <= 1) return;
	const long long totalMs = roundedMilliseconds(timing.totalMs());
	const std::string timeStr = totalMs >= 1 ? std::to_string(totalMs) : "< 1";
	const std::string heroText = "mini map render " + timeStr + " ms, " + std::to_string(payload.rowCount) + "x" + std::to_string(payload.bodyWidth) + " glyphs, " + std::to_string(payload.totalLines) + " lines";

	if (mr::messageline::postFileAutoTimed(mr::messageline::Owner::HeroEvent, heroText, mr::messageline::Kind::Success, fileBytes, mr::messageline::kPriorityHigh) != 0)
		mr::messageline::clearOwner(mr::messageline::Owner::HeroEventFollowup);
}

void playAudioSignal(const std::string &uri) {
	pid_t childPid;
	const std::string player = configuredAudioPlayerPath();
	const std::string playerName = fileNameFromPath(player);
	const AudioPlayerInvocationSpec *spec = audioPlayerInvocationSpec(playerName);

	if (uri.empty() || player.empty()) return;
	childPid = ::fork();
	if (childPid != 0) return;
	{
		const char *argv[8];
		int argc = 0;

		argv[argc++] = player.c_str();
		if (spec != nullptr)
			for (const char *arg : spec->args)
				if (arg != nullptr) argv[argc++] = arg;
		argv[argc++] = uri.c_str();
		argv[argc] = nullptr;
		::execv(player.c_str(), const_cast<char *const *>(argv));
	}
	::_exit(127);
}

void emitTerminalBell() {
	int tty = ::open("/dev/tty", O_WRONLY | O_CLOEXEC);

	if (tty >= 0) {
		static_cast<void>(::write(tty, "\a", 1));
		::close(tty);
		return;
	}
	if (::write(STDERR_FILENO, "\a", 1) == 1) return;
	static_cast<void>(::write(STDOUT_FILENO, "\a", 1));
}

bool collectCurrentLiveLogSearchMatches(std::string_view text, std::vector<SearchMatchEntry> &matches) {
	std::string pattern;
	MRSearchDialogOptions options;
	pcre2_code *code = nullptr;
	std::string regexError;
	std::string chunkText;

	currentSearchPatternSnapshot(pattern, options);
	matches.clear();
	if (pattern.empty() || text.empty()) return false;
	if (!compileSearchRegex(buildSearchPatternExpression(pattern, options.textType), !options.caseSensitive, &code, regexError)) return false;
	chunkText.assign(text.data(), text.size());
	if (!collectRegexMatches(chunkText, code, matches)) {
		pcre2_code_free(code);
		return false;
	}
	pcre2_code_free(code);
	return !matches.empty();
}

std::string baseNameOf(std::string_view path) {
	const std::size_t pos = path.find_last_of("\\/");
	if (pos == std::string_view::npos) return std::string(path);
	return std::string(path.substr(pos + 1));
}

std::string liveLogSearchHitPrefix(MREditWindow *win) {
	const char *title = win != nullptr ? win->getTitle(0) : nullptr;
	const std::string titleText = title != nullptr ? title : "";
	const std::string detail = win != nullptr ? win->windowRoleDetail() : std::string();

	if (titleText.rfind("JOURNAL: ", 0) == 0) return "[JOU]" + titleText.substr(9);
	if (!detail.empty()) return baseNameOf(detail);
	if (titleText.rfind("LIVELOG: ", 0) == 0) return titleText.substr(9);
	return titleText.empty() ? "log" : titleText;
}

void appendMessageSegment(std::vector<mr::messageline::VisibleMessage::Segment> &segments, mr::messageline::Kind kind, std::string_view text) {
	if (text.empty()) return;
	if (!segments.empty() && segments.back().kind == kind) {
		segments.back().text.append(text.data(), text.size());
		return;
	}
	mr::messageline::VisibleMessage::Segment segment;
	segment.kind = kind;
	segment.text.assign(text.data(), text.size());
	segments.push_back(std::move(segment));
}

std::vector<mr::messageline::VisibleMessage::Segment> liveLogSearchHitMessageSegments(MREditWindow *win, std::string_view text, const std::vector<SearchMatchEntry> &matches) {
	std::vector<mr::messageline::VisibleMessage::Segment> segments;
	std::size_t lineStart = 0;
	std::size_t lineEnd = text.size();

	if (matches.empty()) return segments;
	lineStart = text.rfind('\n', matches.front().start);
	if (lineStart == std::string_view::npos) lineStart = 0;
	else
		++lineStart;
	lineEnd = text.find('\n', matches.front().start);
	if (lineEnd == std::string_view::npos) lineEnd = text.size();

	appendMessageSegment(segments, mr::messageline::Kind::Info, liveLogSearchHitPrefix(win));
	appendMessageSegment(segments, mr::messageline::Kind::Info, ": ");
	std::size_t cursor = lineStart;
	for (const SearchMatchEntry &match : matches) {
		if (match.start < lineStart || match.start >= lineEnd) continue;
		const std::size_t start = std::min(match.start, lineEnd);
		const std::size_t end = std::min(match.end > match.start ? match.end : match.start + 1, lineEnd);
		if (start > cursor) appendMessageSegment(segments, mr::messageline::Kind::Info, text.substr(cursor, start - cursor));
		if (end > start) appendMessageSegment(segments, mr::messageline::Kind::Warning, text.substr(start, end - start));
		cursor = end;
	}
	if (cursor < lineEnd) appendMessageSegment(segments, mr::messageline::Kind::Info, text.substr(cursor, lineEnd - cursor));
	return segments;
}

void reportLiveLogSearchHits(const std::vector<SearchMatchEntry> &matches, MREditWindow *win, std::string_view text) {
	const MRLiveLogSettings settings = configuredLiveLogSettings();

	if (matches.empty()) return;
	if (settings.reportSearchHitsOnMessageLine) {
		const std::vector<mr::messageline::VisibleMessage::Segment> segments = liveLogSearchHitMessageSegments(win, text, matches);
		mr::messageline::postTimedSegments(mr::messageline::Owner::HeroEventFollowup, segments, mr::messageline::Kind::Info, std::chrono::seconds(2), mr::messageline::kPriorityMedium);
	}
	if (settings.reportSearchHitsWithSystemBeep) emitTerminalBell();
	if (settings.reportSearchHitsWithAudioSignal) playAudioSignal(settings.audioSignalUri);
}

void appendMacroLogLines(const std::vector<std::string> &logLines) {
	for (const auto &logLine : logLines) {
		std::string prefixed = "  ";
		prefixed += logLine;
		mrLogMessage(prefixed.c_str());
	}
}

void releaseMacroTask(MREditWindow *win, const mr::coprocessor::Result &result, const char *state) {
	int bufferId = win != nullptr ? win->bufferId() : static_cast<int>(result.task.documentId);
	if (win != nullptr) win->releaseCoprocessorTask(result.task.id);
	mrTraceCoprocessorTaskRelease(bufferId, result.task.id, state);
}

} // namespace

void handleCoprocessorResult(const mr::coprocessor::Result &result) {
	logWarmupCancelFinish(result);
	if (mrAdoptUpdateCoprocessorResult(result)) return;
	if (result.task.executionOwnerKind == mr::coprocessor::ExecutionOwnerKind::Dialog) {
		void *adoptedResult = message(TProgram::deskTop, evBroadcast, cmMrCoprocessorDialogResult, const_cast<mr::coprocessor::Result *>(&result));
		mr::coprocessor::globalCoprocessor().noteResultAdoption(result, adoptedResult == &result);
		return;
	}
	if (dispatchMRGitStatusResult(result)) return;
	const mr::coprocessor::GdbEventPayload *gdbEvent = dynamic_cast<const mr::coprocessor::GdbEventPayload *>(result.payload.get());
	if (gdbEvent != nullptr) {
		MREditWindow *targetWindow = findEditWindowByBufferId(gdbEvent->targetBufferId);
		MRBentoBox *targetBento = dynamic_cast<MRBentoBox *>(targetWindow);
		const bool adopted = targetBento != nullptr && targetBento->acceptGdbEvent(*gdbEvent);
		mr::coprocessor::globalCoprocessor().noteResultAdoption(result, adopted);
		if (gdbEvent->event.kind == MRGdbEventKind::Finished) mr::coprocessor::globalCoprocessor().unregisterExternalSource(gdbEvent->sourceId);
		return;
	}
	if (result.task.kind == mr::coprocessor::TaskKind::FileCompare) {
		handleFileCompareResult(result);
		return;
	}
	if (result.task.kind == mr::coprocessor::TaskKind::BentoDiagnosticsProjection || result.task.kind == mr::coprocessor::TaskKind::BentoOutlineProjection) {
		mrDispatchBentoProjectionResult(result);
		return;
	}
	if (result.task.kind == mr::coprocessor::TaskKind::HexPaneProjection) {
		MREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(result.task.executionOwnerLocalId));
		MRHexPaneWindow *targetPane = dynamic_cast<MRHexPaneWindow *>(targetWindow);
		const bool adopted = targetPane != nullptr && targetPane->applyHexProjectionResult(result);
		const std::size_t bytes = result.task.hasPacketSpan && result.task.packetEnd >= result.task.packetStart
		                              ? static_cast<std::size_t>(result.task.packetEnd - result.task.packetStart)
		                              : 0;

		recordTaskPerformance(result, kHexPaneProjectionAction, targetWindow, result.task.documentId, bytes, result.task.label, adopted);
		mr::coprocessor::globalCoprocessor().noteResultAdoption(result, adopted);
		if (result.failed()) mrLogMessage((std::string("Hex pane projection failed: ") + result.error).c_str());
		return;
	}
	if (result.completed()) {
		const mr::coprocessor::LineIndexWarmupPayload *warmup = dynamic_cast<const mr::coprocessor::LineIndexWarmupPayload *>(result.payload.get());
		if (warmup != nullptr) {
			MREditWindow *targetWindow = textWarmupOwnerWindow(result);
			MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
			bool adopted = false;
			if (targetEditor != nullptr && targetEditor->documentId() == result.task.documentId && targetEditor->documentVersion() == result.task.baseVersion &&
			    targetEditor->ownsLineIndexWarmupTask(result.task.id))
				adopted = targetEditor->applyLineIndexWarmup(warmup->packet, result.task.baseVersion, result.task.id);
			if (targetEditor != nullptr) targetEditor->clearLineIndexWarmupTask(result.task.id);
			if (adopted && targetEditor != nullptr) targetEditor->continueComputeWarmupIfNeeded("after-line-index");
			if (targetEditor != nullptr)
				recordTaskPerformance(result, kLineIndexWarmAction, targetWindow, targetEditor->documentId(), targetEditor->bufferLength(), targetWindow->currentFileName(), adopted);
			else
				recordTaskPerformance(result, kLineIndexWarmAction, nullptr, result.task.documentId, 0, result.task.label, false);
			mr::coprocessor::globalCoprocessor().noteResultAdoption(result, adopted);
			return;
		}

		const mr::coprocessor::DisplayWidthWarmupPayload *displayWidth = dynamic_cast<const mr::coprocessor::DisplayWidthWarmupPayload *>(result.payload.get());
		if (displayWidth != nullptr) {
			MREditWindow *targetWindow = textWarmupOwnerWindow(result);
			MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
			bool adopted = false;
			if (targetEditor != nullptr && targetEditor->documentId() == result.task.documentId && targetEditor->documentVersion() == result.task.baseVersion &&
			    targetEditor->ownsDisplayWidthWarmupTask(result.task.id))
				adopted = targetEditor->applyDisplayWidthWarmup(*displayWidth, result.task.baseVersion, result.task.id);
			if (targetEditor != nullptr && !adopted) targetEditor->clearDisplayWidthWarmupTask(result.task.id);
			if (adopted) {
				MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(targetWindow);

				if (bentoBox != nullptr) bentoBox->layoutDesktopContents();
			}
			if (targetWindow != nullptr && targetEditor != nullptr)
				recordTaskPerformance(result, kDisplayWidthWarmAction, targetWindow, targetEditor->documentId(), targetEditor->bufferLength(), targetWindow->currentFileName(), adopted);
			else
				recordTaskPerformance(result, kDisplayWidthWarmAction, nullptr, result.task.documentId, 0, result.task.label, false);
			mr::coprocessor::globalCoprocessor().noteResultAdoption(result, adopted);
			return;
		}

		const mr::coprocessor::SyntaxWarmupPayload *syntax = dynamic_cast<const mr::coprocessor::SyntaxWarmupPayload *>(result.payload.get());
		if (syntax != nullptr) {
			MREditWindow *targetWindow = textWarmupOwnerWindow(result);
			MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
			bool accepted = false;
			if (targetEditor != nullptr && targetEditor->documentId() == result.task.documentId && targetEditor->documentVersion() == result.task.baseVersion &&
			    targetEditor->ownsSyntaxWarmupTask(result.task.id))
				accepted = targetEditor->applySyntaxWarmup(*syntax, result);
			if (targetEditor != nullptr && !accepted) targetEditor->clearSyntaxWarmupTask(result.task.id);
			mr::coprocessor::globalCoprocessor().noteResultAdoption(result, accepted);
			if (targetEditor != nullptr) {
				targetEditor->continueComputeWarmupIfNeeded("after-syntax");
				recordTaskPerformance(result, kSyntaxWarmAction, targetWindow, targetEditor->documentId(), targetEditor->bufferLength(),
				                      detailWithLineRange(targetWindow->currentFileName(), syntaxLineRangeDetail(*syntax, targetEditor, result.task)), false);
			} else {
				recordTaskPerformance(result, kSyntaxWarmAction, nullptr, result.task.documentId, 0, result.task.label, false);
			}
			return;
		}

		if (result.task.kind == mr::coprocessor::TaskKind::FoldWarmup && result.payload != nullptr) {
			MREditWindow *targetWindow = textWarmupOwnerWindow(result);
			MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
			bool accepted = false;
			if (targetEditor != nullptr && targetEditor->documentId() == result.task.documentId && targetEditor->documentVersion() == result.task.baseVersion &&
			    targetEditor->ownsFoldWarmupTask(result.task.id))
				accepted = targetEditor->applyFoldWarmup(*result.payload, result);
			if (targetEditor != nullptr && !accepted) targetEditor->clearFoldWarmupTask(result.task.id);
			if (targetEditor != nullptr)
				recordTaskPerformance(result, kFoldWarmAction, targetWindow, targetEditor->documentId(), targetEditor->bufferLength(),
				                      detailWithLineRange(targetWindow->currentFileName(), taskLabelLineRange(result.task)), false);
			else
				recordTaskPerformance(result, kFoldWarmAction, nullptr, result.task.documentId, 0, result.task.label, false);
			mr::coprocessor::globalCoprocessor().noteResultAdoption(result, accepted);
			return;
		}

		if (result.task.kind == mr::coprocessor::TaskKind::MiniMapWarmup && result.payload != nullptr) {
			MREditWindow *targetWindow = textWarmupOwnerWindow(result);
			MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
			bool accepted = false;
			if (targetEditor != nullptr && targetEditor->documentId() == result.task.documentId && targetEditor->documentVersion() == result.task.baseVersion &&
			    targetEditor->ownsMiniMapWarmupTask(result.task.id))
				accepted = targetEditor->applyMiniMapWarmup(*result.payload, result);
			if (targetEditor != nullptr && !accepted) targetEditor->clearMiniMapWarmupTask(result.task.id);
			const mr::coprocessor::MiniMapWarmupPayload *miniMap = dynamic_cast<const mr::coprocessor::MiniMapWarmupPayload *>(result.payload.get());
			if (targetEditor != nullptr) {
				const std::string detail = miniMap != nullptr ? miniMapLineRangeDetail(*miniMap) : result.task.label;
				recordTaskPerformance(result, kMiniMapRenderAction, targetWindow, targetEditor->documentId(), targetEditor->bufferLength(),
				                      detailWithLineRange(targetWindow->currentFileName(), detail), accepted);
				if (accepted && miniMap != nullptr && targetEditor->miniMapProjectionAvailable() && targetEditor->shouldReportMiniMapInitialRender()) {
					targetEditor->markMiniMapInitialRenderReported();
					postMiniMapHeroEvent(result.timing, *miniMap, targetEditor->bufferLength());
				}
			} else
				recordTaskPerformance(result, kMiniMapRenderAction, nullptr, result.task.documentId, 0, result.task.label);
			mr::coprocessor::globalCoprocessor().noteResultAdoption(result, accepted);
			return;
		}

		const mr::coprocessor::ExternalIoChunkPayload *chunk = dynamic_cast<const mr::coprocessor::ExternalIoChunkPayload *>(result.payload.get());
		if (chunk != nullptr) {
			const std::size_t targetBufferId = chunk->targetBufferId != 0 ? chunk->targetBufferId : chunk->channelId;
			MREditWindow *win = findEditWindowByBufferId(static_cast<int>(targetBufferId));
			if (win != nullptr) {
				const MRLiveLogSettings liveLogSettings = configuredLiveLogSettings();
				std::vector<SearchMatchEntry> searchMatches;
				std::vector<std::pair<std::size_t, std::size_t>> findRanges;

				if (collectCurrentLiveLogSearchMatches(chunk->text, searchMatches)) {
					findRanges.reserve(searchMatches.size());
					for (const SearchMatchEntry &match : searchMatches)
						if (match.end > match.start) findRanges.push_back({match.start, match.end});
				}
				if (liveLogSettings.scrollDirection == MRLiveLogScrollDirection::Up) win->prependLogViewerText(chunk->text.c_str(), &findRanges);
				else
					win->appendLogViewerText(chunk->text.c_str(), &findRanges);
				win->setReadOnly(true);
				win->setFileChanged(false);
				if (MRBentoBox *split = dynamic_cast<MRBentoBox *>(win->owner); split != nullptr && split->buildOutputPane() == win) static_cast<void>(split->refreshCompilerDiagnosticsFromOutput());
				reportLiveLogSearchHits(searchMatches, win, chunk->text);
			}
			mr::coprocessor::globalCoprocessor().noteResultAdoption(result, win != nullptr);
			return;
		}

		const mr::coprocessor::ExternalIoFinishedPayload *finished = dynamic_cast<const mr::coprocessor::ExternalIoFinishedPayload *>(result.payload.get());
		if (finished != nullptr) {
			std::ostringstream statusLine;
			const std::size_t targetBufferId = finished->targetBufferId != 0 ? finished->targetBufferId : finished->channelId;
			MREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(targetBufferId));
			if (targetWindow != nullptr) {
				std::string exitLine = communicationExitLine(*finished);
				targetWindow->appendTextBuffer(exitLine.c_str());
				targetWindow->setReadOnly(true);
				targetWindow->setFileChanged(false);
				recordTaskPerformance(result, "External command", targetWindow, targetWindow->documentId(), targetWindow->bufferLength(), externalIoDisplayName(result.task));
				targetWindow->releaseCoprocessorTask(result.task.id);
				const std::string dividerStatus = communicationDividerStatus(*finished);
				setSplitDiagnosticsStatusForOutput(targetWindow, dividerStatus.c_str());
				if (MRBentoBox *split = dynamic_cast<MRBentoBox *>(targetWindow->owner); split != nullptr && split->buildOutputPane() == targetWindow) static_cast<void>(split->refreshCompilerDiagnosticsFromOutput());
			} else {
				recordTaskPerformance(result, "External command", nullptr, 0, 0, externalIoDisplayName(result.task));
			}
			if (!finished->signaled && finished->exitCode == 0) playAudioSignal(finished->successAudioUri);
			else
				playAudioSignal(finished->failureAudioUri);
			runExternalIoPostBuildMacro(result, *finished);
			if (finished->debuggerContinuation != mr::coprocessor::BuildDebuggerContinuation::None && !result.cancelled() && !result.failed() && !finished->signaled && finished->exitCode == 0)
				static_cast<void>(mrContinueDebuggerAfterBuild(*finished));
			mrTraceCoprocessorTaskRelease(static_cast<int>(finished->channelId), result.task.id, "finished");
			mr::coprocessor::globalCoprocessor().noteResultAdoption(result, true);
			if (result.task.lane == mr::coprocessor::Lane::Extern) mr::coprocessor::globalCoprocessor().unregisterExternalSource(result.task.documentId);
			statusLine << "Communication session #" << finished->channelId << " ";
			if (finished->signaled) statusLine << "terminated by signal " << finished->signalNumber;
			else
				statusLine << "finished with exit code " << finished->exitCode;
			mrLogMessage(statusLine.str().c_str());
			return;
		}

		const mr::coprocessor::MacroDebugWorkerPausedPayload *debugPaused = dynamic_cast<const mr::coprocessor::MacroDebugWorkerPausedPayload *>(result.payload.get());
		if (debugPaused != nullptr) {
			MREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(result.task.documentId));
			const bool accepted = mrApplyMacroDebuggerWorkerResult(debugPaused->sessionId, result.task.id, debugPaused->debugResult, debugPaused->errorMessage);

			if (targetWindow != nullptr) targetWindow->releaseCoprocessorTask(result.task.id);
			mr::coprocessor::globalCoprocessor().noteResultAdoption(result, accepted);
			return;
		}

		const mr::coprocessor::MacroJobFinishedPayload *macro = dynamic_cast<const mr::coprocessor::MacroJobFinishedPayload *>(result.payload.get());
		const mr::coprocessor::MacroJobStagedPayload *staged = dynamic_cast<const mr::coprocessor::MacroJobStagedPayload *>(result.payload.get());
		if (staged != nullptr) {
			std::ostringstream statusLine;
			MREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(result.task.documentId));
			MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
			bool accepted = false;
			bool textChanged = false;
			std::size_t currentVersion = 0;
			std::string statusSummary;
			const char *conflictMarker = nullptr;

			if (targetWindow != nullptr && targetEditor != nullptr) {
				currentVersion = targetEditor->documentVersion();
				MacroCommitConflictSnapshot liveSnapshot = captureMacroCommitConflictSnapshot(targetWindow, targetEditor);
				if (currentVersion != result.task.baseVersion) conflictMarker = "document-version";
				else
					conflictMarker = macroCommitConflictMarker(staged->conflictSnapshot, liveSnapshot);
				if (conflictMarker == nullptr) {
					MRTextBufferModel::CommitResult commit = targetEditor->applyStagedTransaction(staged->transaction, staged->cursorOffset, staged->selectionStart, staged->selectionEnd, staged->fileChanged);
					if (commit.conflicted()) conflictMarker = "document-version";
					else {
						targetEditor->setInsertModeEnabled(staged->insertMode);
						targetWindow->setIndentLevel(staged->indentLevel);
						targetWindow->setCurrentFileName(staged->fileName.c_str());
						targetWindow->applyCommittedBlockState(staged->blockMode, staged->blockMarkingOn, static_cast<uint>(staged->blockAnchor), static_cast<uint>(staged->blockEnd));
						mrvmUiReplaceGlobals(staged->globalOrder, staged->globalInts, staged->globalStrings);
						mrvmUiReplaceWindowLastSearch(targetWindow, staged->fileName, staged->lastSearchValid, staged->lastSearchStart, staged->lastSearchEnd, staged->lastSearchCursor);
						mrvmUiReplaceRuntimeOptions(staged->ignoreCase, staged->tabExpand);
						mrvmUiReplaceWindowMarkStack(targetWindow, staged->markStack);
						mrvmUiSyncLinkedWindowsFrom(targetWindow);
						accepted = true;
						textChanged = commit.applied();
					}
				}
			} else
				conflictMarker = "target-window";

			statusLine << "Background staged macro '" << staged->displayName << "'";
			if (accepted) {
				if (textChanged) statusLine << " committed";
				else
					statusLine << " applied state without text changes";
				if (staged->hadError) statusLine << " with VM errors";
				statusLine << formatTimingSummary(result.timing) << ".";
				statusSummary = statusLine.str();
				recordMacroPerformance(result, targetWindow, targetEditor != nullptr ? targetEditor->documentId() : 0, targetEditor != nullptr ? targetEditor->bufferLength() : 0, staged->displayName);
				if (targetWindow != nullptr) targetWindow->noteBackgroundMacroCompleted(statusSummary);
				releaseMacroTask(targetWindow, result, textChanged ? "committed" : "state-only");
				if (!staged->deferredUiCommands.empty()) {
					queueDeferredMacroUiPlayback(result.task.documentId, staged->displayName, staged->deferredUiCommands);
					{
						std::ostringstream uiLine;
						uiLine << "Queued deferred UI playback for macro '" << staged->displayName << "': " << staged->deferredUiCommands.size() << " command(s).";
						mrLogMessage(uiLine.str().c_str());
					}
				}
			} else {
				statusLine << " conflicted with newer runtime state";
				if (conflictMarker != nullptr) statusLine << " [" << conflictMarker << "]";
				if (currentVersion != 0) statusLine << " (snapshot " << result.task.baseVersion << ", current " << currentVersion << ")";
				statusLine << "; commit aborted without rebase" << formatTimingSummary(result.timing) << ".";
				statusSummary = statusLine.str();
				recordMacroPerformance(result, targetWindow, targetEditor != nullptr ? targetEditor->documentId() : 0, targetEditor != nullptr ? targetEditor->bufferLength() : 0, staged->displayName, mr::performance::Outcome::Conflict);
				if (targetWindow != nullptr) targetWindow->noteBackgroundMacroConflict(statusSummary);
				releaseMacroTask(targetWindow, result, "conflict");
			}
			if (!accepted || textChanged || staged->hadError || !staged->deferredUiCommands.empty()) mrLogMessage(statusSummary.c_str());
			if (staged->debugSessionId != 0) {
				static_cast<void>(mrvmFinalizeStagedDebugSession(staged->debugSessionId, staged->debugResult, accepted, statusSummary));
				static_cast<void>(mrApplyMacroDebuggerWorkerResult(staged->debugSessionId, result.task.id, staged->debugResult, accepted ? std::string() : statusSummary));
			} else
				publishMacroExecutionResultForTask(result.task.id, accepted ? MRMacroExecutionState::Completed : MRMacroExecutionState::Rejected, statusSummary);
			appendMacroLogLines(staged->logLines);
			mr::coprocessor::globalCoprocessor().noteResultAdoption(result, accepted);
			return;
		}
		if (macro != nullptr) {
			std::ostringstream statusLine;
			MREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(result.task.documentId));
			std::string statusSummary;
			const std::size_t acceptedUiRequests = applyMacroExecUiCommandRequests(macro->execUiCommandRequests);

			if (macro->debugSessionId != 0) static_cast<void>(mrApplyMacroDebuggerWorkerResult(macro->debugSessionId, result.task.id, macro->debugResult, std::string()));
			recordMacroPerformance(result, targetWindow, targetWindow != nullptr ? targetWindow->documentId() : 0, targetWindow != nullptr ? targetWindow->bufferLength() : 0, macro->displayName);
			statusLine << "Background macro '" << macro->displayName << "' finished";
			if (macro->hadError) statusLine << " with VM errors";
			if (!macro->execUiCommandRequests.empty()) statusLine << "; exec-ui " << acceptedUiRequests << "/" << macro->execUiCommandRequests.size();
			statusLine << formatTimingSummary(result.timing) << ".";
			statusSummary = statusLine.str();
			if (targetWindow != nullptr) targetWindow->noteBackgroundMacroCompleted(statusSummary);
			releaseMacroTask(targetWindow, result, "finished");
			mrLogMessage(statusSummary.c_str());
			if (macro->debugSessionId == 0) publishMacroExecutionResultForTask(result.task.id, MRMacroExecutionState::Completed, statusSummary);
			appendMacroLogLines(macro->logLines);
			mr::coprocessor::globalCoprocessor().noteResultAdoption(result, true);
			return;
		}
	}

	if (result.task.kind == mr::coprocessor::TaskKind::ExternalIo) {
		const mr::coprocessor::ExternalIoFinishedPayload *finished = dynamic_cast<const mr::coprocessor::ExternalIoFinishedPayload *>(result.payload.get());
		std::size_t targetBufferId = finished != nullptr && finished->targetBufferId != 0 ? finished->targetBufferId : result.task.documentId;
		MREditWindow *targetWindow = nullptr;
		if (finished == nullptr && result.task.lane == mr::coprocessor::Lane::Extern) {
			std::vector<MREditWindow *> windows = allEditWindowsAndBentoPanesInZOrder();
			for (MREditWindow *window : windows) {
				if (window == nullptr) continue;
				for (std::size_t taskIndex = 0; taskIndex < window->trackedCoprocessorTaskCount(); ++taskIndex) {
					if (window->trackedCoprocessorTaskId(taskIndex) != result.task.id) continue;
					targetWindow = window;
					targetBufferId = static_cast<std::size_t>(window->bufferId());
					break;
				}
				if (targetWindow != nullptr) break;
			}
		} else {
			targetWindow = findEditWindowByBufferId(static_cast<int>(targetBufferId));
		}
		if (targetWindow != nullptr) {
			targetWindow->releaseCoprocessorTask(result.task.id);
			if (result.cancelled()) {
				targetWindow->appendTextBuffer("\n[process cancelled]\n");
				targetWindow->setReadOnly(true);
				targetWindow->setFileChanged(false);
			} else if (result.failed()) {
				std::string failureLine = "\n[process failed: " + result.error + "]\n";
				targetWindow->appendTextBuffer(failureLine.c_str());
				targetWindow->setReadOnly(true);
				targetWindow->setFileChanged(false);
			}
		}
		recordTaskPerformance(result, "External command", targetWindow, targetWindow != nullptr ? targetWindow->documentId() : 0, targetWindow != nullptr ? targetWindow->bufferLength() : 0, externalIoDisplayName(result.task));
		if (finished != nullptr) runExternalIoPostBuildMacro(result, *finished);
		if (result.cancelled()) mrTraceCoprocessorTaskRelease(static_cast<int>(targetBufferId), result.task.id, "cancelled");
		else if (result.failed())
			mrTraceCoprocessorTaskRelease(static_cast<int>(targetBufferId), result.task.id, "failed");
		if (result.task.lane == mr::coprocessor::Lane::Extern) mr::coprocessor::globalCoprocessor().unregisterExternalSource(result.task.documentId);
	}

	if (result.task.kind == mr::coprocessor::TaskKind::MacroJob) {
		MREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(result.task.documentId));
		std::string displayName = macroDisplayName(result.task);
		std::string statusSummary;

		if (result.cancelled()) {
			statusSummary = "Background macro '" + displayName + "' cancelled" + formatTimingSummary(result.timing) + ".";
			recordMacroPerformance(result, targetWindow, targetWindow != nullptr ? targetWindow->documentId() : 0, targetWindow != nullptr ? targetWindow->bufferLength() : 0, displayName, mr::performance::Outcome::Cancelled);
			if (targetWindow != nullptr) targetWindow->noteBackgroundMacroCancelled(statusSummary);
			releaseMacroTask(targetWindow, result, "cancelled");
			mrLogMessage(statusSummary.c_str());
			publishMacroExecutionResultForTask(result.task.id, MRMacroExecutionState::Cancelled, statusSummary);
		} else if (result.failed()) {
			std::ostringstream failureLine;
			failureLine << "Background macro '" << displayName << "' failed";
			if (!result.error.empty()) failureLine << ": " << result.error;
			failureLine << formatTimingSummary(result.timing) << ".";
			statusSummary = failureLine.str();
			recordMacroPerformance(result, targetWindow, targetWindow != nullptr ? targetWindow->documentId() : 0, targetWindow != nullptr ? targetWindow->bufferLength() : 0, displayName, mr::performance::Outcome::Failed);
			if (targetWindow != nullptr) targetWindow->noteBackgroundMacroFailed(statusSummary);
			releaseMacroTask(targetWindow, result, "failed");
			mrLogMessage(statusSummary.c_str());
			publishMacroExecutionResultForTask(result.task.id, MRMacroExecutionState::Failed, statusSummary);
		}
		if (result.failed()) return;
	}

	if (result.task.kind == mr::coprocessor::TaskKind::LineIndexWarmup) {
		MREditWindow *targetWindow = textWarmupOwnerWindow(result);
		MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
		if (targetEditor != nullptr) targetEditor->clearLineIndexWarmupTask(result.task.id);
		if (targetWindow != nullptr && targetEditor != nullptr)
			recordTaskPerformance(result, kLineIndexWarmAction, targetWindow, targetEditor->documentId(), targetEditor->bufferLength(), targetWindow->currentFileName());
		else
			recordTaskPerformance(result, kLineIndexWarmAction, nullptr, result.task.documentId, 0, result.task.label);
	}

	if (result.task.kind == mr::coprocessor::TaskKind::DisplayWidthWarmup) {
		MREditWindow *targetWindow = textWarmupOwnerWindow(result);
		MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
		if (targetEditor != nullptr) targetEditor->clearDisplayWidthWarmupTask(result.task.id);
		if (targetWindow != nullptr && targetEditor != nullptr)
			recordTaskPerformance(result, kDisplayWidthWarmAction, targetWindow, targetEditor->documentId(), targetEditor->bufferLength(), targetWindow->currentFileName());
		else
			recordTaskPerformance(result, kDisplayWidthWarmAction, nullptr, result.task.documentId, 0, result.task.label);
	}

	if (result.task.kind == mr::coprocessor::TaskKind::SyntaxWarmup) {
		MREditWindow *targetWindow = textWarmupOwnerWindow(result);
		MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
		const bool owned = targetEditor != nullptr && targetEditor->ownsSyntaxWarmupTask(result.task.id);
		if (owned) {
			targetEditor->clearSyntaxWarmupTask(result.task.id);
			targetEditor->continueComputeWarmupIfNeeded("after-syntax-failure");
			recordTaskPerformance(result, kSyntaxWarmAction, targetWindow, targetEditor->documentId(), targetEditor->bufferLength(), detailWithLineRange(targetWindow->currentFileName(), taskLabelLineRange(result.task)));
		} else
			recordTaskPerformance(result, kSyntaxWarmAction, nullptr, result.task.documentId, 0, result.task.label);
		mr::coprocessor::globalCoprocessor().noteResultAdoption(result, false);
	}

	if (result.task.kind == mr::coprocessor::TaskKind::FoldWarmup) {
		MREditWindow *targetWindow = textWarmupOwnerWindow(result);
		MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
		const bool owned = targetEditor != nullptr && targetEditor->ownsFoldWarmupTask(result.task.id);
		if (owned) {
			targetEditor->clearFoldWarmupTask(result.task.id);
			recordTaskPerformance(result, kFoldWarmAction, targetWindow, targetEditor->documentId(), targetEditor->bufferLength(), detailWithLineRange(targetWindow->currentFileName(), taskLabelLineRange(result.task)));
		} else
			recordTaskPerformance(result, kFoldWarmAction, nullptr, result.task.documentId, 0, result.task.label);
	}

	if (result.task.kind == mr::coprocessor::TaskKind::MiniMapWarmup) {
		MREditWindow *targetWindow = textWarmupOwnerWindow(result);
		MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
		const bool owned = targetEditor != nullptr && targetEditor->ownsMiniMapWarmupTask(result.task.id);
		if (owned) {
			targetEditor->clearMiniMapWarmupTask(result.task.id);
			recordTaskPerformance(result, kMiniMapRenderAction, targetWindow, targetEditor->documentId(), targetEditor->bufferLength(), detailWithLineRange(targetWindow->currentFileName(), taskLabelLineRange(result.task)));
		} else
			recordTaskPerformance(result, kMiniMapRenderAction, nullptr, result.task.documentId, 0, result.task.label);
	}

	if (!result.failed()) {
		if (result.completed()) mr::coprocessor::globalCoprocessor().noteResultAdoption(result, false);
		return;
	}

	std::ostringstream line;
	line << "Coprocessor[" << coprocessorLaneName(result.task.lane) << "] " << (result.task.label.empty() ? "task" : result.task.label) << " failed";
	if (!result.error.empty()) line << ": " << result.error;
	mrLogMessage(line.str().c_str());
}

void mrTraceCoprocessorTaskCancel(int bufferId, std::uint64_t taskId) {
	std::ostringstream line;

	line << "Cancelling coprocessor task #" << taskId << " for window #" << bufferId << ".";
	mrLogMessage(line.str().c_str());
}

void mrTraceCoprocessorTaskRelease(int bufferId, std::uint64_t taskId, const char *state) {
	static_cast<void>(bufferId);
	static_cast<void>(taskId);
	static_cast<void>(state);
}
