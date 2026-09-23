#define Uses_Dialogs
#include "MRBentoBox.hpp"

#include "MRGdbTerminalPane.hpp"
#include "MRBentoBoxRoleSupport.hpp"

#include "../../app/MRCommands.hpp"
#include "../../app/MRCommandRouter.hpp"
#include "../../app/router/MRCommandRouterSearchMultiFileSession.hpp"
#include "../../app/services/MRGdbSession.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../dialogs/setup/MRSetupCommon.hpp"
#include "../../mrmac/MRVM.hpp"
#include "../../mrmac/mrmac.h"
#include "../../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../../mrmac/vm/MRVMValue.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <utility>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace {

void writeGdbInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &session, const char *key, int value);

VirtualMachine::Value gdbSessionsRoot(MRVMRuntimeKv &runtimeKv) {
	VirtualMachine::Value applicationUi = runtimeKv.ensureRoot("APPLICATIONUI");
	VirtualMachine::Value debugger = runtimeKv.ensureChild(applicationUi, "debugger");
	return runtimeKv.ensureChild(debugger, "sessions");
}

VirtualMachine::Value gdbBreakpointSourcesRoot(MRVMRuntimeKv &runtimeKv) {
	VirtualMachine::Value applicationUi = runtimeKv.ensureRoot("APPLICATIONUI");
	VirtualMachine::Value debugger = runtimeKv.ensureChild(applicationUi, "debugger");
	VirtualMachine::Value breakpoints = runtimeKv.ensureChild(debugger, "breakpoints");
	return runtimeKv.ensureChild(breakpoints, "bySource");
}

bool findGdbBreakpointLinesRoot(MRVMRuntimeKv &runtimeKv, const std::string &sourcePath, VirtualMachine::Value &lines) {
	VirtualMachine::Value applicationUi;
	VirtualMachine::Value debugger;
	VirtualMachine::Value breakpoints;
	VirtualMachine::Value sources;
	VirtualMachine::Value source;
	return runtimeKv.findRoot("APPLICATIONUI", applicationUi) && runtimeKv.findChild(applicationUi, "debugger", debugger) &&
	       runtimeKv.findChild(debugger, "breakpoints", breakpoints) && runtimeKv.findChild(breakpoints, "bySource", sources) &&
	       runtimeKv.findChild(sources, normalizeConfiguredPathInput(sourcePath), source) && runtimeKv.findChild(source, "lines", lines);
}

std::vector<int> readGdbBreakpointLines(const std::string &sourcePath) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value lines;
	std::vector<int> result;

	if (!findGdbBreakpointLinesRoot(runtimeKv, sourcePath, lines)) return result;
	for (const std::string &key : runtimeKv.globalStore().keys(lines.hashHandle)) {
		char *end = nullptr;
		const long line = std::strtol(key.c_str(), &end, 10);

		if (end != key.c_str() && *end == '\0' && line > 0) result.push_back(static_cast<int>(line));
	}
	std::sort(result.begin(), result.end());
	return result;
}

std::string readGdbBreakpointAssert(const std::string &sourcePath, int line) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	VirtualMachine::Value source, assertions;
	const VirtualMachine::Value sources = gdbBreakpointSourcesRoot(kv);
	if (!kv.findChild(sources, normalizeConfiguredPathInput(sourcePath), source) || !kv.findChild(source, "asserts", assertions)) return std::string();
	if (!kv.globalStore().contains(assertions.hashHandle, std::to_string(line))) return std::string();
	return kv.globalStore().read(assertions.hashHandle, std::to_string(line)).s;
}

bool findGdbSessionRoot(MRVMRuntimeKv &runtimeKv, int bufferId, VirtualMachine::Value &session) {
	VirtualMachine::Value applicationUi;
	VirtualMachine::Value debugger;
	VirtualMachine::Value sessions;
	return runtimeKv.findRoot("APPLICATIONUI", applicationUi) && runtimeKv.findChild(applicationUi, "debugger", debugger) &&
	       runtimeKv.findChild(debugger, "sessions", sessions) && runtimeKv.findChild(sessions, std::to_string(bufferId), session);
}

void writeGdbString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &session, const char *key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, session, key, mrvmMakeString(value));
}

void writeGdbInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &session, const char *key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, session, key, mrvmMakeInt(value));
}

std::string readGdbString(int bufferId, const char *key) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	VirtualMachine::Value session;
	if (!findGdbSessionRoot(runtimeKv, bufferId, session) || !mrvmHashContainsValue(store, store, session, key)) return std::string();
	const VirtualMachine::Value value = mrvmHashReadValue(store, store, session, key);
	return value.type == TYPE_STR ? value.s : std::string();
}

int readGdbInt(int bufferId, const char *key) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	VirtualMachine::Value session;
	if (!findGdbSessionRoot(runtimeKv, bufferId, session) || !mrvmHashContainsValue(store, store, session, key)) return 0;
	const VirtualMachine::Value value = mrvmHashReadValue(store, store, session, key);
	return value.type == TYPE_INT ? value.i : 0;
}

std::size_t lineStartForNumber(const std::string &text, int line) {
	if (line <= 1) return 0;
	std::size_t offset = 0;
	for (int current = 1; current < line && offset < text.size(); ++current) {
		const std::size_t newline = text.find('\n', offset);
		if (newline == std::string::npos) return text.size();
		offset = newline + 1;
	}
	return offset;
}

std::size_t lineEndForStart(const std::string &text, std::size_t start) {
	const std::size_t newline = text.find('\n', start);
	return newline == std::string::npos ? text.size() : newline;
}

void projectGdbBreakpointLines(MRFileEditor *editor, const std::vector<int> &breakpointLines) {
	if (editor == nullptr) return;
	const std::string source = editor->snapshotText();
	std::vector<std::pair<std::size_t, std::size_t>> ranges;

	for (const int line : breakpointLines) {
		const std::size_t start = lineStartForNumber(source, line);

		if (start >= source.size() && !source.empty()) continue;
		ranges.push_back(std::make_pair(start, lineEndForStart(source, start)));
	}
	editor->setDebuggerBreakpointRanges(ranges, {}, {}, {});
}

} // namespace

bool MRBentoBox::startGdbDebugger(const std::string &programPath, const std::string &sourcePath, std::string &errorMessage) {
	MREditWindow *outputWindow = nullptr;
	MREditWindow *variablesWindow = nullptr;
	MREditWindow *watchesWindow = nullptr;
	MRGdbTerminalPane *terminalWindow = nullptr;

	stopGdbDebugger();
	writeGdbDebuggerRows(false, {});
	writeGdbDebuggerRows(true, {});
	if (!ensureGdbDebuggerPanes(outputWindow, variablesWindow, watchesWindow, terminalWindow)) {
		errorMessage = "Unable to establish GDB debugger panes.";
		return false;
	}
	if (getEditor() != nullptr) getEditor()->clearDebuggerInstructionLine();
	static_cast<void>(outputWindow->replaceTextBuffer(("GDB Debugger\nProgram: " + programPath + "\nSource: " + sourcePath + "\n\n").c_str(), "Debugger Output"));
	outputWindow->setReadOnly(true);
	outputWindow->setFileChanged(false);
	static_cast<void>(variablesWindow->replaceTextBuffer("(waiting for inferior stop)\n", "Variables"));
	variablesWindow->setReadOnly(true);
	variablesWindow->setFileChanged(false);
	static_cast<void>(watchesWindow->replaceTextBuffer("(no watches)\n", "Watches"));
	watchesWindow->setReadOnly(true);
	watchesWindow->setFileChanged(false);
	terminalWindow->resetTerminal();
	gdbSession = std::make_unique<MRGdbSession>();
	if (!gdbSession->start(programPath, sourcePath, bufferId(), errorMessage)) {
		gdbSession.reset();
		return false;
	}
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
		VirtualMachine::Value sessions = gdbSessionsRoot(runtimeKv);
		VirtualMachine::Value session = runtimeKv.ensureChild(sessions, std::to_string(bufferId()));
		writeGdbString(runtimeKv, session, "backend", "gdb");
		writeGdbString(runtimeKv, session, "source", normalizeConfiguredPathInput(sourcePath));
		writeGdbString(runtimeKv, session, "program", normalizeConfiguredPathInput(programPath));
		writeGdbString(runtimeKv, session, "state", "starting");
		writeGdbString(runtimeKv, session, "generation", std::to_string(gdbSession->currentGeneration()));
		writeGdbString(runtimeKv, session, "stopFile", std::string());
		writeGdbInt(runtimeKv, session, "stopLine", 0);
		writeGdbInt(runtimeKv, session, "running", 0);
	}
	const std::vector<int> breakpointLines = readGdbBreakpointLines(sourcePath);
	projectGdbBreakpointLines(getEditor(), breakpointLines);
	for (const int line : breakpointLines) {
		MRGdbCommand command(MRGdbCommandKind::AddBreakpoint);

		command.file = normalizeConfiguredPathInput(sourcePath);
		command.line = line;
		command.text = readGdbBreakpointAssert(sourcePath, line);
		static_cast<void>(gdbSession->send(std::move(command)));
	}
	resizeGdbTerminal(terminalWindow->size.x, terminalWindow->size.y);
	activatePrimaryPane();
	bentoProjectionDirty |= bpdContent | bpdChrome | bpdLayout;
	flushBentoProjection();
	errorMessage.clear();
	return true;
}

void MRBentoBox::stopGdbDebugger() noexcept {
	const std::string sourcePath = gdbDebuggerSourcePath();
	if (gdbThreadListOpen) paneActionDropList.hide();
	cancelDebuggerValueInput();
	if (gdbSession != nullptr) gdbSession->stop();
	gdbSession.reset();
	writeGdbDebuggerRows(false, {});
	writeGdbDebuggerRows(true, {});
	clearGdbDebuggerState();
	clearDebuggerUiState();
	if (getEditor() != nullptr) getEditor()->clearDebuggerInstructionLine();
	if (macroDebuggerActive) refreshMacroDebuggerBreakpointRanges();
	else if (!sourcePath.empty()) projectGdbBreakpointLines(getEditor(), readGdbBreakpointLines(sourcePath));
}

bool MRBentoBox::gdbDebuggerCanEnd() const noexcept {
	return gdbSession != nullptr || (!macroDebuggerActive && programTerminalPane() != nullptr);
}

bool MRBentoBox::endGdbDebugger() {
	if (!gdbDebuggerCanEnd()) return false;
	stopGdbDebuggerForRebuild();
	if (getEditor() != nullptr) getEditor()->clearDebuggerBreakpointRanges();
	paneRoleDropList.hide();
	paneActionDropList.hide();
	gdbThreadListOpen = false;
	updatePaneRoleListChrome();
	setActivePane(0);
	for (const BentoLeaf &leaf : leaves)
		if (leaf.id != 0 && nodeIndexForLeaf(leaf.id) >= 0) closePane(leaf.id);
	bentoMode = bbmDocumentViewports;
	for (BentoLeaf &leaf : leaves)
		if (leaf.id == 0) {
			leaf.spec = paneSpecForRole(bprSource);
			leaf.title.clear();
			break;
		}
	layoutSplitPanes();
	mrMarkWorkspaceAutosaveDirty("debugger return to editor", this);
	return true;
}

void MRBentoBox::stopGdbDebuggerForRebuild() noexcept {
	stopGdbDebugger();
}

bool MRBentoBox::acceptGdbEvent(const mr::coprocessor::GdbEventPayload &payload) {
	if (gdbSession == nullptr || payload.generation != gdbSession->currentGeneration()) return false;
	const MRGdbEvent &event = payload.event;
	if (event.kind == MRGdbEventKind::Threads || event.kind == MRGdbEventKind::Variables || event.kind == MRGdbEventKind::Watches) {
		if (event.stopGeneration != std::strtoull(readGdbString(bufferId(), "stopGeneration").c_str(), nullptr, 10) ||
		    event.contextGeneration != std::strtoull(readGdbString(bufferId(), "contextGeneration").c_str(), nullptr, 10)) return false;
		const std::string state = readGdbString(bufferId(), "state");
		if (state != "stopped" && !(event.kind == MRGdbEventKind::Threads && state == "selecting")) return false;
		if (event.kind != MRGdbEventKind::Threads && event.threadId != gdbThreadForRole(event.kind == MRGdbEventKind::Variables ? bprVariables : bprWatches)) return false;
	}
	MREditWindow *outputWindow = debuggerOutputPane();
	MREditWindow *variablesWindow = variablesPane();
	MRGdbTerminalPane *terminalWindow = programTerminalPane();
	switch (payload.event.kind) {
		case MRGdbEventKind::Started:
			if (outputWindow != nullptr) outputWindow->appendTextBuffer(payload.event.text.c_str());
			publishGdbDebuggerState("loaded");
			break;
		case MRGdbEventKind::DebuggerOutput:
			if (outputWindow != nullptr) outputWindow->appendTextBuffer(payload.event.text.c_str());
			break;
		case MRGdbEventKind::InferiorOutput:
			if (terminalWindow != nullptr) terminalWindow->appendTerminalOutput(payload.event.text);
			break;
		case MRGdbEventKind::Running:
			if (gdbThreadListOpen) paneActionDropList.hide();
			cancelDebuggerValueInput();
			publishGdbDebuggerState("running");
			if (getEditor() != nullptr) getEditor()->clearDebuggerInstructionLine();
			break;
		case MRGdbEventKind::Stopped:
		case MRGdbEventKind::Threads: {
			cancelDebuggerValueInput();
			if (gdbThreadListOpen) paneActionDropList.hide();
			const bool exited = event.kind == MRGdbEventKind::Stopped && event.text.rfind("exited", 0) == 0;
			const bool differentThread = event.threadId != readGdbString(bufferId(), "threadId");
			const bool projectSource = event.kind == MRGdbEventKind::Stopped || readGdbInt(bufferId(), "sourceProjectionPending") != 0;
			{
				std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
				MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
				VirtualMachine::Value session;
				if (!findGdbSessionRoot(runtimeKv, bufferId(), session)) return false;
				writeGdbString(runtimeKv, session, "stopGeneration", std::to_string(event.stopGeneration));
				writeGdbString(runtimeKv, session, "contextGeneration", std::to_string(event.contextGeneration));
				writeGdbString(runtimeKv, session, "threadId", exited ? std::string() : event.threadId);
				writeGdbInt(runtimeKv, session, "contextReady", event.kind == MRGdbEventKind::Threads ? 1 : 0);
				writeGdbInt(runtimeKv, session, "sourceProjectionPending", event.kind == MRGdbEventKind::Stopped ? 1 : 0);
				writeGdbInt(runtimeKv, session, "valuesReady", 0);
				if (event.kind == MRGdbEventKind::Threads) {
					writeGdbString(runtimeKv, session, "variablesThreadId", event.variablesThreadId);
					writeGdbString(runtimeKv, session, "watchesThreadId", event.watchesThreadId);
				} else {
					writeGdbString(runtimeKv, session, "variablesThreadId", exited ? std::string() : event.threadId);
					writeGdbString(runtimeKv, session, "watchesThreadId", exited ? std::string() : event.threadId);
				}
				if (event.kind == MRGdbEventKind::Threads) {
					VirtualMachine::Value threads = runtimeKv.ensureChild(session, "threads");
					for (const MRGdbMiThread &thread : event.threads) {
						VirtualMachine::Value item;
						if (!runtimeKv.findChild(threads, thread.id, item)) continue;
						writeGdbString(runtimeKv, item, "name", thread.name);
						writeGdbString(runtimeKv, item, "function", thread.function);
						writeGdbString(runtimeKv, item, "address", thread.address);
						writeGdbString(runtimeKv, item, "file", thread.file);
						writeGdbString(runtimeKv, item, "state", thread.state);
						writeGdbInt(runtimeKv, item, "line", thread.line);
						if (thread.id == event.threadId) {
							writeGdbString(runtimeKv, session, "threadFunction", thread.function);
							writeGdbString(runtimeKv, session, "threadAddress", thread.address);
						}
					}
				}
			}
			if (differentThread || exited || event.kind == MRGdbEventKind::Stopped) {
				writeGdbDebuggerRows(false, {});
				writeGdbDebuggerRows(true, {});
				for (MREditWindow *window : {variablesWindow, watchesPane()}) {
					if (window == nullptr) continue;
					static_cast<void>(window->replaceTextBuffer(exited ? "(inferior exited)\n" : "(loading thread values)\n", window == variablesWindow ? "Variables" : "Watches"));
					window->setReadOnly(true);
					window->setFileChanged(false);
				}
			}
			publishGdbDebuggerState(exited ? "exited" : "stopped", event.file, event.line);
			MRFileEditor *sourceEditor = getEditor();
			if (projectSource && sourceEditor != nullptr) sourceEditor->clearDebuggerInstructionLine();
			if (projectSource && event.kind == MRGdbEventKind::Threads && sourceEditor != nullptr && event.line > 0 &&
			    !event.file.empty() && normalizeConfiguredPathInput(event.file) == gdbDebuggerSourcePath()) {
				const std::size_t line = static_cast<std::size_t>(event.line - 1);
				if (line >= sourceEditor->bufferModel().lineCount()) break;
				std::size_t offset = sourceEditor->bufferModel().lineStartByIndex(line);
				while (offset < sourceEditor->bufferLength() && (sourceEditor->charAtOffset(offset) == ' ' || sourceEditor->charAtOffset(offset) == '\t')) ++offset;
				sourceEditor->setCursorOffset(offset);
				sourceEditor->setDebuggerInstructionLine(line);
				sourceEditor->centerDocumentLocationInView(line, sourceEditor->actualCursorVisualColumn(offset));
			}
			break;
		}
		case MRGdbEventKind::Variables:
		case MRGdbEventKind::Watches:
			refreshGdbDebuggerValues(payload.event);
			{
				std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
				MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
				VirtualMachine::Value session;
				if (findGdbSessionRoot(runtimeKv, bufferId(), session))
					writeGdbInt(runtimeKv, session, "valuesReady", readGdbInt(bufferId(), "valuesReady") | (event.kind == MRGdbEventKind::Variables ? 1 : 2));
			}
			break;
		case MRGdbEventKind::Breakpoints:
			projectGdbBreakpointLines(getEditor(), readGdbBreakpointLines(gdbDebuggerSourcePath()));
			break;
		case MRGdbEventKind::Finished:
			if (gdbThreadListOpen) paneActionDropList.hide();
			cancelDebuggerValueInput();
			if (outputWindow != nullptr) {
				const std::string text = payload.event.text.empty() ? "\n[GDB session finished]\n" : "\n[GDB session finished: " + payload.event.text + "]\n";
				outputWindow->appendTextBuffer(text.c_str());
			}
			publishGdbDebuggerState("finished");
			if (getEditor() != nullptr) getEditor()->clearDebuggerInstructionLine();
			gdbSession->markFinished(payload.generation);
			break;
	}
	if (outputWindow != nullptr) {
		outputWindow->setReadOnly(true);
		outputWindow->setFileChanged(false);
	}
	bentoProjectionDirty |= bpdContent | bpdChrome | bpdOverlay;
	flushBentoProjection();
	return true;
}

bool MRBentoBox::sendGdbTerminalInput(const std::string &text) {
	return sendGdbCommand(MRGdbCommandKind::TerminalInput, text);
}

bool MRBentoBox::startGdbAtFirstCodeLine() {
	return sendGdbCommand(MRGdbCommandKind::StepOver);
}

bool MRBentoBox::clearGdbProgramTerminal() {
	MRGdbTerminalPane *terminalWindow = programTerminalPane();

	if (!gdbDebuggerActive() || terminalWindow == nullptr) return false;
	terminalWindow->clearTerminal();
	bentoProjectionDirty |= bpdContent;
	return true;
}

bool MRBentoBox::editGdbBreakpointAssert(std::size_t sourceOffset) {
	MRFileEditor *editor = getEditor();
	if (!gdbDebuggerActive() || editor == nullptr) return false;
	const int line = static_cast<int>(editor->bufferModel().lineIndex(sourceOffset)) + 1;
	const std::vector<int> lines = readGdbBreakpointLines(gdbDebuggerSourcePath());
	if (std::find(lines.begin(), lines.end(), line) == lines.end()) return false;
	if (gdbDebuggerRunning()) {
		postDialogWarning("Pause the program before editing a breakpoint assert.");
		return true;
	}
	activatePrimaryPane();
	editor->setCursorOffset(sourceOffset);
	const std::string previous = readGdbBreakpointAssert(gdbDebuggerSourcePath(), line);
	char assertion[256] = {};
	std::copy_n(previous.data(), std::min(previous.size(), sizeof(assertion) - 1), assertion);
	if (mr::dialogs::execTextInputDialog("BREAKPOINT ASSERT", "Assert (empty = all threads)", assertion, sizeof(assertion) - 1) == cmCancel) return true;
	if (previous != assertion) static_cast<void>(sendGdbCommand(MRGdbCommandKind::SetBreakpointAssert, assertion));
	return true;
}

bool MRBentoBox::executeGdbSourceContextCommand(ushort command, std::size_t sourceOffset, const std::string &identifier) {
	MRFileEditor *editor = getEditor();

	if (!gdbDebuggerActive() || editor == nullptr) return false;
	editor->setCursorOffset(std::min(sourceOffset, editor->bufferLength()));
	switch (command) {
		case cmMrDebuggerToggleBreakpoint:
			return sendGdbCommand(MRGdbCommandKind::ToggleBreakpoint);
		case cmMrDebuggerRunHere:
			return sendGdbCommand(MRGdbCommandKind::RunToLocation);
		case cmMrDebuggerAddWatch:
			if (!identifier.empty()) return sendGdbCommand(MRGdbCommandKind::AddWatch, identifier);
			break;
		case cmMrDebuggerStep:
			return sendGdbCommand(MRGdbCommandKind::StepInto);
		case cmMrDebuggerStepOver:
			return sendGdbCommand(MRGdbCommandKind::StepOver);
		case cmMrDebuggerStepOut:
			return sendGdbCommand(MRGdbCommandKind::StepOut);
		case cmMrDebuggerEvaluate:
			break;
		default:
			return false;
	}
	TEvent event{};
	event.what = evKeyDown;
	event.keyDown.keyCode = command == cmMrDebuggerAddWatch ? kbF7 : kbF4;
	event.keyDown.controlKeyState = 0;
	return handleGdbDebuggerFunctionKey(event);
}

void MRBentoBox::resizeGdbTerminal(int columns, int rows) {
	if (gdbSession == nullptr || !gdbSession->active()) return;
	MRGdbCommand command(MRGdbCommandKind::ResizeTerminal);
	command.columns = columns;
	command.rows = rows;
	static_cast<void>(gdbSession->send(std::move(command)));
}

bool MRBentoBox::debuggerFunctionKeysActive() const noexcept {
	return (gdbSession != nullptr && gdbSession->active()) || macroDebuggerFunctionKeysActive();
}

bool MRBentoBox::debuggerHasLiveSession() const noexcept {
	return (gdbSession != nullptr && gdbSession->active()) || macroDebuggerHasLiveSession();
}

bool MRBentoBox::debuggerSessionRunning() const {
	return gdbSession != nullptr && gdbSession->active() ? gdbDebuggerRunning() : macroDebuggerSessionRunning();
}

bool MRBentoBox::gdbDebuggerActive() const noexcept {
	return gdbSession != nullptr && gdbSession->active();
}

bool MRBentoBox::handleDebuggerFunctionKey(TEvent &event) {
	if (gdbSession != nullptr && gdbSession->active()) return handleGdbDebuggerFunctionKey(event);
	return handleMacroDebuggerFunctionKey(event);
}

bool MRBentoBox::handleGdbDebuggerFunctionKey(TEvent &event) {
	if (event.what != evKeyDown || gdbSession == nullptr || !gdbSession->active()) return false;
	const ushort keyCode = event.keyDown.keyCode;
	const ushort modifiers = event.keyDown.controlKeyState;
	MRGdbCommandKind kind = MRGdbCommandKind::ContinueExecution;
	bool recognized = true;
	std::string text;
	if (keyCode == kbF4 && modifiers == 0) {
		char expression[256] = {};
		if (mr::dialogs::execTextInputDialog("GDB DEBUGGER", "Evaluate or assign expression", expression, sizeof(expression) - 1) == cmCancel) { clearEvent(event); return true; }
		kind = MRGdbCommandKind::Evaluate;
		text = expression;
	} else if (keyCode == kbF5 && modifiers == 0) {
		if (readGdbString(bufferId(), "state") == "loaded") {
			clearEvent(event);
			return handleMRCommand(cmMrDebuggerRebuildAndStart, this);
		}
		kind = gdbDebuggerRunning() ? MRGdbCommandKind::PauseExecution : MRGdbCommandKind::ContinueExecution;
	}
	else if (keyCode == kbF6 && modifiers == 0) kind = MRGdbCommandKind::RunToLocation;
	else if (keyCode == kbF7 && modifiers == 0) {
		char expression[256] = {};
		if (mr::dialogs::execTextInputDialog("GDB DEBUGGER", "Watch expression", expression, sizeof(expression) - 1) == cmCancel) { clearEvent(event); return true; }
		kind = MRGdbCommandKind::AddWatch;
		text = expression;
	} else if ((keyCode == kbShiftF7 || (keyCode == kbF7 && (modifiers & kbShift) != 0))) {
		char objectName[256] = {};
		if (mr::dialogs::execTextInputDialog("GDB DEBUGGER", "Remove watch object or expression", objectName, sizeof(objectName) - 1) == cmCancel) { clearEvent(event); return true; }
		kind = MRGdbCommandKind::EraseWatch;
		text = objectName;
	} else if (keyCode == kbF8 && modifiers == 0) kind = MRGdbCommandKind::Quit;
	else if (keyCode == kbF9 && modifiers == 0) kind = MRGdbCommandKind::ToggleBreakpoint;
	else if (keyCode == kbF10 && modifiers == 0) kind = MRGdbCommandKind::StepInto;
	else if (keyCode == kbF11 && modifiers == 0) kind = MRGdbCommandKind::StepOver;
	else if (keyCode == kbShiftF11 || (keyCode == kbF11 && (modifiers & kbShift) != 0)) kind = MRGdbCommandKind::StepOut;
	else recognized = false;
	if (!recognized) return false;
	static_cast<void>(sendGdbCommand(kind, text));
	clearEvent(event);
	return true;
}

bool MRBentoBox::sendGdbCommand(MRGdbCommandKind commandKind, const std::string &text, const std::string &objectName) {
	if (commandKind == MRGdbCommandKind::AddWatch && text.empty()) return false;
	if (gdbSession == nullptr || !gdbSession->active()) return false;
	if (commandKind != MRGdbCommandKind::Quit && commandKind != MRGdbCommandKind::TerminalInput && commandKind != MRGdbCommandKind::ResizeTerminal) {
		const std::string sourcePath = gdbDebuggerSourcePath();
		const std::string programPath = readGdbString(bufferId(), "program");
		std::error_code fileError;
		bool sourceChanged = isFileChanged();
		if (!sourceChanged && !sourcePath.empty() && !programPath.empty()) {
			const std::filesystem::file_time_type sourceTime = std::filesystem::last_write_time(sourcePath, fileError);
			if (!fileError) {
				const std::filesystem::file_time_type programTime = std::filesystem::last_write_time(programPath, fileError);
				if (!fileError) sourceChanged = sourceTime > programTime;
			}
		}
		if (sourceChanged) {
			if (MREditWindow *outputWindow = debuggerOutputPane(); outputWindow != nullptr) {
				outputWindow->appendTextBuffer("\n[Source changed after build. Stop GDB, rebuild, and start a new debug session.]\n");
				outputWindow->setReadOnly(true);
				outputWindow->setFileChanged(false);
			}
			publishGdbDebuggerState("stale");
			bentoProjectionDirty |= bpdContent | bpdChrome;
			flushBentoProjection();
			return false;
		}
	}
	const bool execution = commandKind == MRGdbCommandKind::ContinueExecution || commandKind == MRGdbCommandKind::StepInto || commandKind == MRGdbCommandKind::StepOver || commandKind == MRGdbCommandKind::StepOut || commandKind == MRGdbCommandKind::RunToLocation;
	const bool selection = commandKind == MRGdbCommandKind::SelectThread || commandKind == MRGdbCommandKind::SelectVariablesThread || commandKind == MRGdbCommandKind::SelectWatchesThread;
	const bool contextual = execution || selection || commandKind == MRGdbCommandKind::Evaluate || commandKind == MRGdbCommandKind::AssignVariable || commandKind == MRGdbCommandKind::AddWatch || commandKind == MRGdbCommandKind::EraseWatch;
	const std::string state = readGdbString(bufferId(), "state");
	const bool loaded = state == "loaded" || state == "starting";
	if (contextual && !gdbDebuggerContextReady() && !(loaded && execution && commandKind != MRGdbCommandKind::StepOut)) return false;
	if (commandKind == MRGdbCommandKind::ToggleBreakpoint && !loaded && !gdbDebuggerContextReady()) return false;
	if (commandKind == MRGdbCommandKind::SetBreakpointAssert && gdbDebuggerRunning()) return false;
	if (commandKind == MRGdbCommandKind::AssignVariable && readGdbInt(bufferId(), "valuesReady") != 3) return false;
	MRGdbCommand command(commandKind);
	command.text = text;
	command.objectName = objectName;
	command.threadId = gdbThreadForRole(bprSource);
	if (commandKind == MRGdbCommandKind::AssignVariable) command.threadId = gdbThreadForRole(bprVariables);
	else if (commandKind == MRGdbCommandKind::AddWatch || commandKind == MRGdbCommandKind::EraseWatch) command.threadId = gdbThreadForRole(bprWatches);
	else if (commandKind == MRGdbCommandKind::Evaluate) command.threadId = gdbThreadForRole(roleForLeaf(activeLeafId));
	command.stopGeneration = std::strtoull(readGdbString(bufferId(), "stopGeneration").c_str(), nullptr, 10);
	command.contextGeneration = std::strtoull(readGdbString(bufferId(), "contextGeneration").c_str(), nullptr, 10);
	if (commandKind == MRGdbCommandKind::ToggleBreakpoint || commandKind == MRGdbCommandKind::SetBreakpointAssert || commandKind == MRGdbCommandKind::RunToLocation) {
		command.file = gdbDebuggerSourcePath();
		command.line = getEditor() != nullptr ? getEditor()->currentLineNumber() : 0;
		if (command.file.empty() || command.line <= 0) return false;
	}
	const bool refreshesValues = selection || commandKind == MRGdbCommandKind::Evaluate || commandKind == MRGdbCommandKind::AssignVariable || commandKind == MRGdbCommandKind::AddWatch || commandKind == MRGdbCommandKind::EraseWatch;
	if (selection) command.threadId = text;
	if (refreshesValues) ++command.contextGeneration;
	const std::uint64_t requestedContext = command.contextGeneration;
	if (!gdbSession->send(std::move(command))) return false;
	if (refreshesValues) {
		if (selection) {
			cancelDebuggerValueInput();
			writeGdbDebuggerRows(false, {});
			writeGdbDebuggerRows(true, {});
			layoutGdbDebuggerValues(false, true);
			layoutGdbDebuggerValues(true, true);
		}
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
		VirtualMachine::Value session;
		if (findGdbSessionRoot(runtimeKv, bufferId(), session)) {
			writeGdbString(runtimeKv, session, "contextGeneration", std::to_string(requestedContext));
			if (selection) {
				writeGdbString(runtimeKv, session, "state", "selecting");
				writeGdbInt(runtimeKv, session, "sourceProjectionPending", commandKind == MRGdbCommandKind::SelectThread ? 1 : 0);
				writeGdbInt(runtimeKv, session, "contextReady", 0);
			}
			writeGdbInt(runtimeKv, session, "valuesReady", 0);
		}
	} else if (execution) {
		cancelDebuggerValueInput();
		publishGdbDebuggerState("resuming");
		if (getEditor() != nullptr) getEditor()->clearDebuggerInstructionLine();
	}
	bentoProjectionDirty |= bpdChrome;
	flushBentoProjection();
	return true;
}

void MRBentoBox::publishGdbDebuggerState(const char *state, const std::string &file, int line) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value session;
	if (!findGdbSessionRoot(runtimeKv, bufferId(), session)) return;
	writeGdbString(runtimeKv, session, "state", state != nullptr ? state : std::string());
	writeGdbString(runtimeKv, session, "stopFile", file);
	writeGdbInt(runtimeKv, session, "stopLine", line);
	writeGdbInt(runtimeKv, session, "running", state != nullptr && std::string(state) == "running" ? 1 : 0);
}

void MRBentoBox::clearGdbDebuggerState() noexcept {
	try {
		MRGdbSession::releaseRuntimeState(bufferId());
	} catch (...) {
	}
}

std::string MRBentoBox::gdbDebuggerStateText(MRBentoPaneRole role) const {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	const std::string state = readGdbString(bufferId(), "state");
	const std::string thread = gdbThreadForRole(role);
	std::string threadLabel;
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value session, threads;
	if (!thread.empty() && findGdbSessionRoot(runtimeKv, bufferId(), session) && runtimeKv.findChild(session, "threads", threads) &&
	    runtimeKv.globalStore().keys(threads.hashHandle).size() > 1) threadLabel = "THREAD " + thread;
	if (state == "starting") return "GDB STARTING";
	if (state == "loaded") return "INFERIOR READY";
	if (state == "running") return threadLabel.empty() ? "RUNNING" : threadLabel + " RUNNING";
	if (state == "resuming") return threadLabel.empty() ? "RESUMING" : threadLabel + " RESUMING";
	if (state == "selecting") return "SWITCHING THREAD";
	if (state == "stopped") {
		if (readGdbInt(bufferId(), "contextReady") == 0) return "READING THREADS";
		if (thread.empty()) return "NO THREAD CONTEXT";
		return threadLabel;
	}
	if (state == "exited") return "INFERIOR EXITED";
	if (state == "stale") return "SOURCE CHANGED - REBUILD";
	if (state == "finished") return "GDB FINISHED";
	return state.empty() ? "GDB" : state;
}

std::string MRBentoBox::gdbDebuggerSourcePath() const {
	return readGdbString(bufferId(), "source");
}

bool MRBentoBox::gdbDebuggerRunning() const {
	return readGdbInt(bufferId(), "running") != 0;
}

bool MRBentoBox::gdbDebuggerContextReady(bool values) const {
	return gdbDebuggerActive() && readGdbString(bufferId(), "state") == "stopped" && readGdbInt(bufferId(), "contextReady") != 0 && !readGdbString(bufferId(), "threadId").empty() && (!values || readGdbInt(bufferId(), "valuesReady") == 3);
}

std::string MRBentoBox::gdbThreadForRole(MRBentoPaneRole role) const {
	const char *key = role == bprVariables ? "variablesThreadId" : role == bprWatches ? "watchesThreadId" : "threadId";
	const std::string selected = readGdbString(bufferId(), key);
	return selected.empty() ? readGdbString(bufferId(), "threadId") : selected;
}

bool MRBentoBox::gdbThreadSelectionAvailable(int leafId) const {
	const MRBentoPaneRole role = roleForLeaf(leafId);
	if (role != bprSource && role != bprVariables && role != bprWatches) return false;
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	if (!gdbDebuggerContextReady()) return false;
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value session, threads;
	return findGdbSessionRoot(runtimeKv, bufferId(), session) && runtimeKv.findChild(session, "threads", threads) &&
	       runtimeKv.globalStore().keys(threads.hashHandle).size() > 1;
}

void MRBentoBox::showGdbThreadList() {
	if (!gdbThreadSelectionAvailable(pendingPaneRoleTargetLeafId)) return;
	std::vector<std::string> choices;
	std::string current;
	int textWidth = 0;
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
		MRVMHashStore &store = runtimeKv.globalStore();
		VirtualMachine::Value session, threads;
		if (!findGdbSessionRoot(runtimeKv, bufferId(), session) || !runtimeKv.findChild(session, "threads", threads)) return;
		const std::string currentThread = gdbThreadForRole(roleForLeaf(pendingPaneRoleTargetLeafId));
		for (const std::string &id : store.keys(threads.hashHandle)) {
			VirtualMachine::Value thread;
			if (!runtimeKv.findChild(threads, id, thread)) continue;
			const std::string name = mrvmHashReadValue(store, store, thread, "name").s;
			const std::string function = mrvmHashReadValue(store, store, thread, "function").s;
			const std::string state = mrvmHashReadValue(store, store, thread, "state").s;
			const std::string file = mrvmHashReadValue(store, store, thread, "file").s;
			const std::string address = mrvmHashReadValue(store, store, thread, "address").s;
			const int line = mrvmHashReadValue(store, store, thread, "line").i;
			std::string label = id;
			if (!name.empty()) label += " " + name;
			if (!state.empty()) label += " " + state;
			if (!function.empty()) label += " " + function;
			if (!file.empty()) {
				label += " " + std::filesystem::path(file).filename().string();
				if (line > 0) label += ":" + std::to_string(line);
			} else if (!address.empty()) label += " " + address;
			if (id == currentThread) current = label;
			textWidth = std::max(textWidth, strwidth(label.c_str()));
			choices.push_back(std::move(label));
		}
	}
	dismissPaneMenus();
	if (choices.size() <= 1) return;
	std::sort(choices.begin(), choices.end(), [](const std::string &a, const std::string &b) { return std::strtoull(a.c_str(), nullptr, 10) < std::strtoull(b.c_str(), nullptr, 10); });
	TRect content = paneBoundsForLeaf(pendingPaneRoleTargetLeafId);
	content.grow(-1, -1);
	const int width = std::min(textWidth + 2, content.b.x - content.a.x);
	const int height = std::min({10, static_cast<int>(choices.size()), content.b.y - content.a.y});
	if (width < 3 || height < 1) return;
	const int left = std::clamp<int>(paneRoleListAnchor.a.x, content.a.x, content.b.x - width);
	const int top = std::clamp<int>(paneRoleListAnchor.a.y, content.a.y, content.b.y - height);
	gdbThreadListOpen = true;
	paneActionDropList.toggle(*this, TRect(left, top, left + width, top), choices, current, this, mr::bento::cmGdbThreadAccepted, height);
}

void MRBentoBox::acceptGdbThreadChoice() {
	std::string label;
	if (!paneActionDropList.acceptSelection(label)) return;
	const std::string threadId = label.substr(0, label.find(' '));
	const MRBentoPaneRole role = roleForLeaf(pendingPaneRoleTargetLeafId);
	const MRGdbCommandKind kind = role == bprVariables ? MRGdbCommandKind::SelectVariablesThread : role == bprWatches ? MRGdbCommandKind::SelectWatchesThread : MRGdbCommandKind::SelectThread;
	if (role != bprSource || threadId != gdbThreadForRole(role)) static_cast<void>(sendGdbCommand(kind, threadId));
	setActivePane(pendingPaneRoleTargetLeafId);
}
