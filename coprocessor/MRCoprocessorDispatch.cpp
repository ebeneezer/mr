#define Uses_TGroup
#define Uses_TProgram
#define Uses_TScrollBar
#include <tvision/tv.h>

#include "MRCoprocessorDispatch.hpp"

#include <array>
#include <chrono>
#include <algorithm>
#include <deque>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "MRPerformance.hpp"
#include "MRWindowCommands.hpp"

#include "../mrmac/MRVM.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRFileEditor/MRFileEditor.hpp"
#include "../ui/MRIndicator.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {
const char *kLineIndexWarmAction = "Line index warming";
const char *kSyntaxWarmAction = "Syntax warming";
const char *kMiniMapRenderAction = "Mini map rendering";
const char *kSaveNormalizationWarmAction = "Save normalization warming";
constexpr std::size_t kMacroUiPlaybackBudgetCommands = 48;
const std::chrono::milliseconds kMacroUiPlaybackBudgetSlice(2);

constexpr std::array<const char *, mrducDelay + 1> kDeferredUiCommandNames{
    "UNKNOWN", "CREATE_WINDOW", "DELETE_WINDOW", "MODIFY_WINDOW", "LINK_WINDOW", "UNLINK_WINDOW", "ZOOM", "REDRAW", "NEW_SCREEN", "SWITCH_WINDOW", "SIZE_WINDOW", "MARQUEE", "MARQUEE_WARNING", "MARQUEE_ERROR", "MAKE_MESSAGE", "BRAIN", "PUT_BOX", "WRITE", "CLR_LINE", "GOTOXY", "PUT_LINE_NUM", "PUT_COL_NUM", "SCROLL_BOX_UP", "SCROLL_BOX_DN", "CLEAR_SCREEN", "KILL_BOX", "REGISTER_MENU_ITEM", "REMOVE_MENU_ITEM", "MESSAGEBOX", "DELAY",
};

std::optional<const char *> deferredUiCommandNameAt(int type) {
	if (type < 0) return std::nullopt;
	const std::size_t index = static_cast<std::size_t>(type);
	if (index >= kDeferredUiCommandNames.size()) return std::nullopt;
	return kDeferredUiCommandNames[index];
}

const char *coprocessorLaneName(mr::coprocessor::Lane lane) {
	switch (lane) {
		case mr::coprocessor::Lane::Io:
			return "io";
		case mr::coprocessor::Lane::MiniMap:
			return "minimap";
		case mr::coprocessor::Lane::Macro:
			return "macro";
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

void recordTaskPerformance(const mr::coprocessor::Result &result, const std::string &action, MREditWindow *win, std::size_t documentId, std::size_t bytes, const std::string &detail) {
	mr::performance::recordBackgroundResult(result, action, win != nullptr ? static_cast<std::size_t>(win->bufferId()) : 0, documentId, bytes, detail);
}

void recordMacroPerformance(const mr::coprocessor::Result &result, MREditWindow *win, std::size_t documentId, std::size_t bytes, const std::string &detail, mr::performance::Outcome outcome = mr::performance::Outcome::Completed) {
	if (outcome == mr::performance::Outcome::Completed) {
		recordTaskPerformance(result, "Background macro", win, documentId, bytes, detail);
		return;
	}
	mr::performance::recordBackgroundEvent(result.task.lane, outcome, result.timing, "Background macro", win != nullptr ? static_cast<std::size_t>(win->bufferId()) : 0, documentId, bytes, detail);
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

void postMiniMapHeroEvent(const mr::coprocessor::TaskTiming &timing, const mr::coprocessor::MiniMapWarmupPayload &payload) {
	if (payload.totalLines <= 1) return;
	const long long totalMs = roundedMilliseconds(timing.totalMs());
	const std::string timeStr = totalMs >= 1 ? std::to_string(totalMs) : "< 1";
	const std::string heroText = "mini map render " + timeStr + " ms, " + std::to_string(payload.rowCount) + "x" + std::to_string(payload.bodyWidth) + " glyphs, " + std::to_string(payload.totalLines) + " lines";

	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEvent, heroText, mr::messageline::Kind::Success, mr::messageline::kPriorityHigh);
	mr::messageline::clearOwner(mr::messageline::Owner::HeroEventFollowup);
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

const char *deferredUiCommandName(int type) {
	return deferredUiCommandNameAt(type).value_or("UNKNOWN");
}

struct DeferredUiRenderGateway {
	static bool renderDeferredCommand(const MRMacroDeferredUiCommand &command) {
		static constexpr const char *kNoActiveApplicationMessage = "Deferred UI render failed: no active application.";
		if (command.type == mrducDelay) return true;
		if (TProgram::application == nullptr) {
			mrLogMessage(kNoActiveApplicationMessage);
			return false;
		}
		return mrvmUiRenderFacadeRenderDeferredCommand(command);
	}
};

int marqueeKindFromDeferredType(int type) noexcept {
	switch (type) {
		case mrducMarqueeWarning:
			return 1;
		case mrducMarqueeError:
			return 2;
		default:
			return 0;
	}
}

struct MacroScreenModel {
	struct Cell {
		char ch;
		unsigned char attr;
		bool known;

		Cell() noexcept : ch(' '), attr(0x07), known(false) {
		}

		Cell(char aCh, unsigned char anAttr, bool isKnown) noexcept : ch(aCh), attr(anAttr), known(isKnown) {
		}
	};

	bool seeded;
	bool cursorKnown;
	int cursorX;
	int cursorY;
	bool lineNumberKnown;
	int lineNumber;
	bool colNumberKnown;
	int colNumber;
	bool brainKnown;
	bool brainEnabled;
	bool marqueeKnown;
	int marqueeKind;
	std::string marqueeText;
	int screenWidth;
	int screenHeight;
	std::vector<Cell> cells;

	MacroScreenModel() noexcept : seeded(false), cursorKnown(false), cursorX(1), cursorY(1), lineNumberKnown(false), lineNumber(0), colNumberKnown(false), colNumber(0), brainKnown(false), brainEnabled(false), marqueeKnown(false), marqueeKind(0), marqueeText(), screenWidth(0), screenHeight(0), cells() {
	}

	void seedFromRuntime() {
		int x = 1;
		int y = 1;
		screenWidth = std::max(0, mrvmUiScreenWidth());
		screenHeight = std::max(0, mrvmUiScreenHeight());
		if (screenWidth > 0 && screenHeight > 0) cells.assign(static_cast<std::size_t>(screenWidth) * static_cast<std::size_t>(screenHeight), Cell());
		else
			cells.clear();
		if (mrvmUiCursorPosition(x, y)) {
			cursorKnown = true;
			cursorX = x;
			cursorY = y;
		}
		seeded = true;
	}

	void invalidateAfterRenderFailure() {
		seeded = false;
		cursorKnown = false;
		lineNumberKnown = false;
		colNumberKnown = false;
		brainKnown = false;
		marqueeKnown = false;
		screenWidth = 0;
		screenHeight = 0;
		cells.clear();
	}

	[[nodiscard]] bool hasGrid() const noexcept {
		return screenWidth > 0 && screenHeight > 0 && cells.size() == static_cast<std::size_t>(screenWidth) * static_cast<std::size_t>(screenHeight);
	}

	[[nodiscard]] static unsigned char composeAttribute(int bgColor, int fgColor) noexcept {
		if ((bgColor & 0xFF) == 0) return static_cast<unsigned char>(fgColor & 0xFF);
		return static_cast<unsigned char>(((bgColor & 0x0F) << 4) | (fgColor & 0x0F));
	}

	[[nodiscard]] static int marqueeKindFor(int type) noexcept {
		return marqueeKindFromDeferredType(type);
	}

	[[nodiscard]] std::size_t indexFor(int x, int y) const noexcept {
		return static_cast<std::size_t>(y) * static_cast<std::size_t>(screenWidth) + static_cast<std::size_t>(x);
	}

	bool writeCell(int x, int y, char ch, unsigned char attr) {
		Cell &cell = cells[indexFor(x, y)];
		const bool changed = !cell.known || cell.ch != ch || cell.attr != attr;
		cell.ch = ch;
		cell.attr = attr;
		cell.known = true;
		return changed;
	}

	bool copyCell(int dstX, int dstY, int srcX, int srcY) {
		Cell &dst = cells[indexFor(dstX, dstY)];
		const Cell src = cells[indexFor(srcX, srcY)];
		const bool changed = dst.known != src.known || dst.ch != src.ch || dst.attr != src.attr;
		dst = src;
		return changed;
	}

	bool fillRect(int x1, int y1, int x2, int y2, char ch, unsigned char attr) {
		bool changed = false;
		for (int y = y1; y <= y2; ++y)
			for (int x = x1; x <= x2; ++x)
				changed = writeCell(x, y, ch, attr) || changed;
		return changed;
	}

	bool applyClearScreen(const MRMacroDeferredUiCommand &command) {
		const unsigned char attr = static_cast<unsigned char>(command.a1 & 0xFF);
		if (!hasGrid()) {
			cursorKnown = true;
			cursorX = 1;
			cursorY = 1;
			lineNumberKnown = false;
			lineNumber = 0;
			colNumberKnown = false;
			colNumber = 0;
			return true;
		}

		bool changed = false;
		for (std::size_t i = 0; i < cells.size(); ++i)
			changed = writeCell(static_cast<int>(i % static_cast<std::size_t>(screenWidth)), static_cast<int>(i / static_cast<std::size_t>(screenWidth)), ' ', attr) || changed;
		cursorKnown = true;
		cursorX = 1;
		cursorY = 1;
		lineNumberKnown = false;
		lineNumber = 0;
		colNumberKnown = false;
		colNumber = 0;
		return changed;
	}

	bool applyWrite(const MRMacroDeferredUiCommand &command) {
		if (!hasGrid()) return true;

		const int y = command.a2 - 1;
		if (y < 0 || y >= screenHeight) return false;

		const unsigned char attr = composeAttribute(command.a3, command.a4);
		bool changed = false;
		for (std::size_t i = 0; i < command.text.size(); ++i) {
			const int x = command.a1 - 1 + static_cast<int>(i);
			if (x < 0) continue;
			if (x >= screenWidth) break;
			changed = writeCell(x, y, command.text[i], attr) || changed;
		}
		return changed;
	}

	bool applyPutBox(const MRMacroDeferredUiCommand &command) {
		if (!hasGrid()) return true;

		int x1 = command.a1 - 1;
		int y1 = command.a2 - 1;
		int x2 = command.a3 - 1;
		int y2 = command.a4 - 1;
		const unsigned char attr = composeAttribute(command.a5, command.a6);
		const bool shadow = command.a7 != 0;
		bool changed = false;
		std::string title = command.text;

		if (x1 > x2) std::swap(x1, x2);
		if (y1 > y2) std::swap(y1, y2);
		x1 = std::max(0, std::min(x1, screenWidth - 1));
		x2 = std::max(0, std::min(x2, screenWidth - 1));
		y1 = std::max(0, std::min(y1, screenHeight - 1));
		y2 = std::max(0, std::min(y2, screenHeight - 1));
		if (x1 > x2 || y1 > y2) return false;

		changed = fillRect(x1, y1, x2, y2, ' ', attr) || changed;
		for (int x = x1 + 1; x < x2; ++x) {
			changed = writeCell(x, y1, '-', attr) || changed;
			changed = writeCell(x, y2, '-', attr) || changed;
		}
		for (int y = y1 + 1; y < y2; ++y) {
			changed = writeCell(x1, y, '|', attr) || changed;
			changed = writeCell(x2, y, '|', attr) || changed;
		}
		changed = writeCell(x1, y1, '+', attr) || changed;
		changed = writeCell(x2, y1, '+', attr) || changed;
		changed = writeCell(x1, y2, '+', attr) || changed;
		changed = writeCell(x2, y2, '+', attr) || changed;

		if (!title.empty() && x2 - x1 >= 2) {
			const int maxTitleLen = x2 - x1 - 1;
			if (maxTitleLen > 0) {
				if (static_cast<int>(title.size()) > maxTitleLen) title = title.substr(0, static_cast<std::size_t>(maxTitleLen));
				const int startX = x1 + 1 + std::max(0, (maxTitleLen - static_cast<int>(title.size())) / 2);
				for (std::size_t i = 0; i < title.size(); ++i) {
					const int x = startX + static_cast<int>(i);
					if (x >= x1 + 1 && x <= x2 - 1) changed = writeCell(x, y1, title[i], attr) || changed;
				}
			}
		}

		if (shadow) {
			if (x2 + 1 < screenWidth) changed = fillRect(x2 + 1, y1 + 1, x2 + 1, y2 + 1, ' ', 0x08) || changed;
			if (y2 + 1 < screenHeight) changed = fillRect(x1 + 1, y2 + 1, x2 + 1, y2 + 1, ' ', 0x08) || changed;
		}

		return changed;
	}

	bool applyClrLine() {
		if (!cursorKnown || !hasGrid()) return true;

		const int y = std::max(0, std::min(cursorY - 1, screenHeight - 1));
		unsigned char attr = 0x07;
		const Cell &rowHead = cells[indexFor(0, y)];
		if (rowHead.known) attr = rowHead.attr;
		return fillRect(0, y, screenWidth - 1, y, ' ', attr);
	}

	bool applyClrLine(const MRMacroDeferredUiCommand &command) {
		if (command.a3 <= 0) return applyClrLine();
		if (!hasGrid()) return true;

		int x = command.a1 - 1;
		const int y = command.a2 - 1;
		int count = command.a3;
		if (y < 0 || y >= screenHeight || count <= 0) return false;
		if (x < 0) {
			count += x;
			x = 0;
		}
		if (x >= screenWidth || count <= 0) return false;
		count = std::min(count, screenWidth - x);

		unsigned char attr = 0x07;
		const Cell &rowHead = cells[indexFor(0, y)];
		if (rowHead.known) attr = rowHead.attr;
		return fillRect(x, y, x + count - 1, y, ' ', attr);
	}

	bool applyGotoxy(const MRMacroDeferredUiCommand &command) {
		int x = command.a1;
		int y = command.a2;
		if (screenWidth > 0) x = std::max(1, std::min(x, screenWidth));
		if (screenHeight > 0) y = std::max(1, std::min(y, screenHeight));
		const bool changed = !cursorKnown || cursorX != x || cursorY != y;
		cursorKnown = true;
		cursorX = x;
		cursorY = y;
		return changed;
	}

	bool applyPutLineNum(const MRMacroDeferredUiCommand &command) {
		const bool changed = !lineNumberKnown || lineNumber != command.a1;
		lineNumberKnown = true;
		lineNumber = command.a1;
		return changed;
	}

	bool applyPutColNum(const MRMacroDeferredUiCommand &command) {
		const bool changed = !colNumberKnown || colNumber != command.a1;
		colNumberKnown = true;
		colNumber = command.a1;
		return changed;
	}

	bool applyScroll(const MRMacroDeferredUiCommand &command, bool down) {
		if (!hasGrid()) return true;

		int x1 = command.a1 - 1;
		int y1 = command.a2 - 1;
		int x2 = command.a3 - 1;
		int y2 = command.a4 - 1;
		const unsigned char attr = static_cast<unsigned char>(command.a5 & 0xFF);
		bool changed = false;

		if (x1 > x2) std::swap(x1, x2);
		if (y1 > y2) std::swap(y1, y2);
		x1 = std::max(0, std::min(x1, screenWidth - 1));
		x2 = std::max(0, std::min(x2, screenWidth - 1));
		y1 = std::max(0, std::min(y1, screenHeight - 1));
		y2 = std::max(0, std::min(y2, screenHeight - 1));
		if (x1 > x2 || y1 > y2) return false;

		if (y2 - y1 + 1 <= 1) return fillRect(x1, y1, x2, y2, ' ', attr);

		if (down) {
			for (int y = y2; y > y1; --y)
				for (int x = x1; x <= x2; ++x)
					changed = copyCell(x, y, x, y - 1) || changed;
			changed = fillRect(x1, y1, x2, y1, ' ', attr) || changed;
		} else {
			for (int y = y1; y < y2; ++y)
				for (int x = x1; x <= x2; ++x)
					changed = copyCell(x, y, x, y + 1) || changed;
			changed = fillRect(x1, y2, x2, y2, ' ', attr) || changed;
		}
		return changed;
	}

	bool applyMarquee(const MRMacroDeferredUiCommand &command) {
		const int nextKind = marqueeKindFor(command.type);
		const bool changed = !marqueeKnown || marqueeKind != nextKind || marqueeText != command.text;
		marqueeKnown = true;
		marqueeKind = nextKind;
		marqueeText = command.text;
		return changed;
	}

	bool applyBrain(const MRMacroDeferredUiCommand &command) {
		const bool enabled = command.a1 != 0;
		const bool changed = !brainKnown || brainEnabled != enabled;
		brainKnown = true;
		brainEnabled = enabled;
		return changed;
	}

	bool shouldRenderAndProject(const MRMacroDeferredUiCommand &command) {
		switch (command.type) {
			case mrducMarqueeInfo:
			case mrducMarqueeWarning:
			case mrducMarqueeError:
			case mrducMakeMessage:
				return applyMarquee(command);
			case mrducBrain:
				return applyBrain(command);
			case mrducGotoxy:
				return applyGotoxy(command);
			case mrducPutLineNum:
				return applyPutLineNum(command);
			case mrducPutColNum:
				return applyPutColNum(command);
			case mrducWrite:
				return applyWrite(command);
			case mrducPutBox:
				return applyPutBox(command);
			case mrducClrLine:
				return applyClrLine(command);
			case mrducScrollBoxUp:
				return applyScroll(command, false);
			case mrducScrollBoxDn:
				return applyScroll(command, true);
			case mrducClearScreen:
				return applyClearScreen(command);
			case mrducKillBox:
			case mrducMessageBox:
				return true;
			default:
				return true;
		}
	}
};

struct MacroScreenView {
	static bool render(const MRMacroDeferredUiCommand &command) {
		return DeferredUiRenderGateway::renderDeferredCommand(command);
	}
};

struct DeferredMacroUiPlayback {
	std::size_t documentId;
	std::string displayName;
	std::vector<MRMacroDeferredUiCommand> commands;
	std::size_t nextIndex;
	std::size_t appliedCount;
	std::size_t skippedCount;
	std::size_t failedCount;
	std::uint64_t observedScreenEpoch;
	bool waitingForDelay;
	std::chrono::steady_clock::time_point resumeAfter;
	MacroScreenModel screenModel;

	DeferredMacroUiPlayback(std::size_t aDocumentId, std::string aDisplayName, std::vector<MRMacroDeferredUiCommand> aCommands) : documentId(aDocumentId), displayName(std::move(aDisplayName)), commands(std::move(aCommands)), nextIndex(0), appliedCount(0), skippedCount(0), failedCount(0), observedScreenEpoch(0), waitingForDelay(false), resumeAfter(std::chrono::steady_clock::time_point::min()), screenModel() {
	}
};

std::deque<DeferredMacroUiPlayback> g_deferredMacroUiPlaybackQueue;

void queueDeferredMacroUiPlayback(std::size_t documentId, const std::string &displayName, const std::vector<MRMacroDeferredUiCommand> &commands) {
	if (commands.empty()) return;
	g_deferredMacroUiPlaybackQueue.emplace_back(documentId, displayName, commands);
}

void logDeferredMacroUiPlaybackSummary(const DeferredMacroUiPlayback &playback) {
	std::ostringstream summary;
	summary << "Deferred UI playback '" << playback.displayName << "' finished: applied=" << playback.appliedCount << ", skipped=" << playback.skippedCount << ", failed=" << playback.failedCount << ".";
	mrLogMessage(summary.str().c_str());
}

void pumpDeferredMacroUiPlaybackImpl() {
	const auto deadline = std::chrono::steady_clock::now() + kMacroUiPlaybackBudgetSlice;
	std::size_t remainingCommands = kMacroUiPlaybackBudgetCommands;

	while (!g_deferredMacroUiPlaybackQueue.empty() && remainingCommands > 0 && std::chrono::steady_clock::now() < deadline) {
		DeferredMacroUiPlayback &playback = g_deferredMacroUiPlaybackQueue.front();
		MREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(playback.documentId));

		if (playback.waitingForDelay) {
			if (std::chrono::steady_clock::now() < playback.resumeAfter) break;
			playback.waitingForDelay = false;
			playback.resumeAfter = std::chrono::steady_clock::time_point::min();
		}
		{
			const std::uint64_t liveEpoch = mrvmUiScreenMutationEpoch();
			if (playback.observedScreenEpoch == 0) playback.observedScreenEpoch = liveEpoch;
			else if (liveEpoch != playback.observedScreenEpoch) {
				playback.screenModel.invalidateAfterRenderFailure();
				playback.observedScreenEpoch = liveEpoch;
			}
		}
		if (!playback.screenModel.seeded) playback.screenModel.seedFromRuntime();
		if (targetWindow != nullptr) mrvmUiSetCurrentWindow(targetWindow);
		mrvmUiBeginMacroScreenBatch();

		while (playback.nextIndex < playback.commands.size() && remainingCommands > 0 && std::chrono::steady_clock::now() < deadline) {
			const MRMacroDeferredUiCommand &command = playback.commands[playback.nextIndex];
			++playback.nextIndex;
			--remainingCommands;

			if (command.type == mrducDelay) {
				const int millis = std::max(0, command.a1);
				if (millis > 0) {
					playback.waitingForDelay = true;
					playback.resumeAfter = std::chrono::steady_clock::now() + std::chrono::milliseconds(millis);
				}
				break;
			}
			if (!playback.screenModel.shouldRenderAndProject(command)) {
				++playback.skippedCount;
				continue;
			}
			if (MacroScreenView::render(command)) {
				++playback.appliedCount;
				playback.observedScreenEpoch = mrvmUiScreenMutationEpoch();
				continue;
			}
			++playback.failedCount;
			playback.screenModel.invalidateAfterRenderFailure();
			playback.observedScreenEpoch = mrvmUiScreenMutationEpoch();
			mrLogMessage((std::string("Deferred UI command failed: ") + deferredUiCommandName(command.type)).c_str());
		}
		mrvmUiEndMacroScreenBatch();

		if (playback.nextIndex >= playback.commands.size() && !playback.waitingForDelay) {
			logDeferredMacroUiPlaybackSummary(playback);
			g_deferredMacroUiPlaybackQueue.pop_front();
			continue;
		}
		break;
	}
}
} // namespace

void pumpDeferredMacroUiPlayback() {
	pumpDeferredMacroUiPlaybackImpl();
}

void handleCoprocessorResult(const mr::coprocessor::Result &result) {
	if (result.completed()) {
		const mr::coprocessor::IndicatorBlinkPayload *blink = dynamic_cast<const mr::coprocessor::IndicatorBlinkPayload *>(result.payload.get());
		if (blink != nullptr) {
			MRIndicator::applyBlinkUpdate(blink->indicatorId, blink->channel, blink->generation, blink->visible);
			return;
		}

		const mr::coprocessor::LineIndexWarmupPayload *warmup = dynamic_cast<const mr::coprocessor::LineIndexWarmupPayload *>(result.payload.get());
		if (warmup != nullptr) {
			std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
			bool recorded = false;
			for (auto &window : windows) {
				MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
				if (editor == nullptr) continue;
				if (editor->documentId() != result.task.documentId) {
					editor->clearLineIndexWarmupTask(result.task.id);
					continue;
				}
				bool applied = false;
				if (editor->documentVersion() == result.task.baseVersion) applied = editor->applyLineIndexWarmup(warmup->warmup, result.task.baseVersion);
				if (!applied) editor->clearLineIndexWarmupTask(result.task.id);
				if (!recorded) {
					recordTaskPerformance(result, kLineIndexWarmAction, window, editor->documentId(), editor->bufferLength(), window->currentFileName());
					recorded = true;
				}
			}
			if (!recorded) recordTaskPerformance(result, kLineIndexWarmAction, nullptr, result.task.documentId, 0, result.task.label);
			return;
		}

		const mr::coprocessor::SyntaxWarmupPayload *syntax = dynamic_cast<const mr::coprocessor::SyntaxWarmupPayload *>(result.payload.get());
		if (syntax != nullptr) {
			std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
			bool recorded = false;
			for (auto &window : windows) {
				MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
				if (editor == nullptr) continue;
				if (editor->documentId() != result.task.documentId) {
					editor->clearSyntaxWarmupTask(result.task.id);
					continue;
				}
				bool applied = false;
				if (editor->documentVersion() == result.task.baseVersion) applied = editor->applySyntaxWarmup(*syntax, result.task.baseVersion, result.task.id);
				if (!applied) editor->clearSyntaxWarmupTask(result.task.id);
				if (!recorded) {
					recordTaskPerformance(result, kSyntaxWarmAction, window, editor->documentId(), editor->bufferLength(), window->currentFileName());
					recorded = true;
				}
			}
			if (!recorded) recordTaskPerformance(result, kSyntaxWarmAction, nullptr, result.task.documentId, 0, result.task.label);
			return;
		}

		const mr::coprocessor::MiniMapWarmupPayload *miniMap = dynamic_cast<const mr::coprocessor::MiniMapWarmupPayload *>(result.payload.get());
		if (miniMap != nullptr) {
			std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
			bool recorded = false;
			bool postedHero = false;
			for (auto &window : windows) {
				MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
				if (editor == nullptr) continue;
				if (editor->documentId() != result.task.documentId) {
					editor->clearMiniMapWarmupTask(result.task.id);
					continue;
				}
				bool applied = false;
				if (editor->documentVersion() == result.task.baseVersion) applied = editor->applyMiniMapWarmup(*miniMap, result.task.baseVersion, result.task.id);
				if (!applied) editor->clearMiniMapWarmupTask(result.task.id);
				if (applied) {
					const bool shouldPostHero = editor->shouldReportMiniMapInitialRender();
					editor->markMiniMapInitialRenderReported();
					if (shouldPostHero && !postedHero) {
						postMiniMapHeroEvent(result.timing, *miniMap);
						postedHero = true;
					}
				}
				if (!recorded) {
					recordTaskPerformance(result, kMiniMapRenderAction, window, editor->documentId(), editor->bufferLength(), window->currentFileName());
					recorded = true;
				}
			}
			if (!recorded) recordTaskPerformance(result, kMiniMapRenderAction, nullptr, result.task.documentId, 0, result.task.label);
			return;
		}

		const mr::coprocessor::SaveNormalizationWarmupPayload *saveNormalization = dynamic_cast<const mr::coprocessor::SaveNormalizationWarmupPayload *>(result.payload.get());
		if (saveNormalization != nullptr) {
			std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
			bool recorded = false;
			for (auto &window : windows) {
				MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
				if (editor == nullptr) continue;
				if (editor->documentId() != result.task.documentId) {
					editor->clearSaveNormalizationWarmupTask(result.task.id);
					continue;
				}
				bool applied = false;
				if (editor->documentVersion() == result.task.baseVersion) applied = editor->applySaveNormalizationWarmup(*saveNormalization, result.task.baseVersion, result.task.id, static_cast<double>(result.timing.runMicros));
				if (!applied) editor->clearSaveNormalizationWarmupTask(result.task.id);
				if (!recorded) {
					recordTaskPerformance(result, kSaveNormalizationWarmAction, window, editor->documentId(), saveNormalization->sourceBytes, window->currentFileName());
					recorded = true;
				}
			}
			if (!recorded) recordTaskPerformance(result, kSaveNormalizationWarmAction, nullptr, result.task.documentId, 0, result.task.label);
			return;
		}

		const mr::coprocessor::ExternalIoChunkPayload *chunk = dynamic_cast<const mr::coprocessor::ExternalIoChunkPayload *>(result.payload.get());
		if (chunk != nullptr) {
			MREditWindow *win = findEditWindowByBufferId(static_cast<int>(chunk->channelId));
			if (win != nullptr) {
				win->appendTextBuffer(chunk->text.c_str());
				win->setReadOnly(true);
				win->setFileChanged(false);
			}
			return;
		}

		const mr::coprocessor::ExternalIoFinishedPayload *finished = dynamic_cast<const mr::coprocessor::ExternalIoFinishedPayload *>(result.payload.get());
		if (finished != nullptr) {
			std::ostringstream statusLine;
			MREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(finished->channelId));
			if (targetWindow != nullptr) {
				std::string exitLine = communicationExitLine(*finished);
				targetWindow->appendTextBuffer(exitLine.c_str());
				targetWindow->setReadOnly(true);
				targetWindow->setFileChanged(false);
				recordTaskPerformance(result, "External command", targetWindow, targetWindow->documentId(), targetWindow->bufferLength(), externalIoDisplayName(result.task));
				targetWindow->releaseCoprocessorTask(result.task.id);
			} else {
				recordTaskPerformance(result, "External command", nullptr, 0, 0, externalIoDisplayName(result.task));
			}
			mrTraceCoprocessorTaskRelease(static_cast<int>(finished->channelId), result.task.id, "finished");
			statusLine << "Communication session #" << finished->channelId << " ";
			if (finished->signaled) statusLine << "terminated by signal " << finished->signalNumber;
			else
				statusLine << "finished with exit code " << finished->exitCode;
			mrLogMessage(statusLine.str().c_str());
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
			mrLogMessage(statusSummary.c_str());
			appendMacroLogLines(staged->logLines);
			return;
		}
		if (macro != nullptr) {
			std::ostringstream statusLine;
			MREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(result.task.documentId));
			std::string statusSummary;

			recordMacroPerformance(result, targetWindow, targetWindow != nullptr ? targetWindow->documentId() : 0, targetWindow != nullptr ? targetWindow->bufferLength() : 0, macro->displayName);
			statusLine << "Background macro '" << macro->displayName << "' finished";
			if (macro->hadError) statusLine << " with VM errors";
			statusLine << formatTimingSummary(result.timing) << ".";
			statusSummary = statusLine.str();
			if (targetWindow != nullptr) targetWindow->noteBackgroundMacroCompleted(statusSummary);
			releaseMacroTask(targetWindow, result, "finished");
			mrLogMessage(statusSummary.c_str());
			appendMacroLogLines(macro->logLines);
			return;
		}
	}

	if (result.task.kind == mr::coprocessor::TaskKind::ExternalIo) {
		MREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(result.task.documentId));
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
		if (result.cancelled()) mrTraceCoprocessorTaskRelease(static_cast<int>(result.task.documentId), result.task.id, "cancelled");
		else if (result.failed())
			mrTraceCoprocessorTaskRelease(static_cast<int>(result.task.documentId), result.task.id, "failed");
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
		}
		if (result.failed()) return;
	}

	if (result.task.kind == mr::coprocessor::TaskKind::LineIndexWarmup) {
		std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
		bool recorded = false;
		for (auto &window : windows) {
			MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
			if (editor == nullptr) continue;
			if (!recorded && editor->documentId() == result.task.documentId) {
				recordTaskPerformance(result, kLineIndexWarmAction, window, editor->documentId(), editor->bufferLength(), window->currentFileName());
				recorded = true;
			}
			editor->clearLineIndexWarmupTask(result.task.id);
		}
		if (!recorded) recordTaskPerformance(result, kLineIndexWarmAction, nullptr, result.task.documentId, 0, result.task.label);
	}

	if (result.task.kind == mr::coprocessor::TaskKind::SyntaxWarmup) {
		std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
		bool recorded = false;
		for (auto &window : windows) {
			MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
			if (editor == nullptr) continue;
			if (!recorded && editor->documentId() == result.task.documentId) {
				recordTaskPerformance(result, kSyntaxWarmAction, window, editor->documentId(), editor->bufferLength(), window->currentFileName());
				recorded = true;
			}
			editor->clearSyntaxWarmupTask(result.task.id);
		}
		if (!recorded) recordTaskPerformance(result, kSyntaxWarmAction, nullptr, result.task.documentId, 0, result.task.label);
	}

	if (result.task.kind == mr::coprocessor::TaskKind::MiniMapWarmup) {
		std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
		bool recorded = false;
		for (auto &window : windows) {
			MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
			if (editor == nullptr) continue;
			if (!recorded && editor->documentId() == result.task.documentId) {
				recordTaskPerformance(result, kMiniMapRenderAction, window, editor->documentId(), editor->bufferLength(), window->currentFileName());
				recorded = true;
			}
			editor->clearMiniMapWarmupTask(result.task.id);
		}
		if (!recorded) recordTaskPerformance(result, kMiniMapRenderAction, nullptr, result.task.documentId, 0, result.task.label);
	}

	if (result.task.kind == mr::coprocessor::TaskKind::SaveNormalizationWarmup) {
		std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
		bool recorded = false;
		for (auto &window : windows) {
			MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
			if (editor == nullptr) continue;
			if (!recorded && editor->documentId() == result.task.documentId) {
				recordTaskPerformance(result, kSaveNormalizationWarmAction, window, editor->documentId(), editor->bufferLength(), window->currentFileName());
				recorded = true;
			}
			editor->clearSaveNormalizationWarmupTask(result.task.id);
		}
		if (!recorded) recordTaskPerformance(result, kSaveNormalizationWarmAction, nullptr, result.task.documentId, 0, result.task.label);
	}

	if (!result.failed()) return;

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
	std::ostringstream line;

	line << "Released coprocessor task #" << taskId << " for window #" << bufferId;
	if (state != nullptr && *state != '\0') line << " (" << state << ")";
	line << ".";
	mrLogMessage(line.str().c_str());
}
