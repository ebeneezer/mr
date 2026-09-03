#define Uses_Dialogs
#include "MRBentoBox.hpp"

#include "MRGdbTerminalPane.hpp"

#include "../../app/MRCommands.hpp"
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

void writeGdbBreakpointLines(const std::string &sourcePath, const std::vector<int> &breakpointLines) {
	if (sourcePath.empty()) return;
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value sources = gdbBreakpointSourcesRoot(runtimeKv);
	VirtualMachine::Value source = runtimeKv.ensureChild(sources, normalizeConfiguredPathInput(sourcePath));
	VirtualMachine::Value lines = runtimeKv.replaceChild(source, "lines");

	for (const int line : breakpointLines)
		if (line > 0) writeGdbInt(runtimeKv, lines, std::to_string(line).c_str(), 1);
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
	gdbDebuggerVariableRows.clear();
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
		VirtualMachine::Value session = runtimeKv.replaceChild(sessions, std::to_string(bufferId()));
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
		MRGdbCommand command(MRGdbCommandKind::ToggleBreakpoint);

		command.file = normalizeConfiguredPathInput(sourcePath);
		command.line = line;
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
	cancelDebuggerValueInput();
	if (gdbSession != nullptr) gdbSession->stop();
	gdbSession.reset();
	gdbDebuggerVariableRows.clear();
	clearGdbDebuggerState();
	if (getEditor() != nullptr) getEditor()->clearDebuggerInstructionLine();
	if (macroDebuggerActive) refreshMacroDebuggerBreakpointRanges();
}

bool MRBentoBox::acceptGdbEvent(const mr::coprocessor::GdbEventPayload &payload) {
	if (gdbSession == nullptr || payload.generation != gdbSession->currentGeneration()) return false;
	MREditWindow *outputWindow = debuggerOutputPane();
	MREditWindow *variablesWindow = variablesPane();
	MREditWindow *watchesWindow = watchesPane();
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
			cancelDebuggerValueInput();
			publishGdbDebuggerState("running");
			if (getEditor() != nullptr) getEditor()->clearDebuggerInstructionLine();
			break;
		case MRGdbEventKind::Stopped:
			if (payload.event.text.rfind("exited", 0) == 0) {
				cancelDebuggerValueInput();
				gdbDebuggerVariableRows.clear();
				if (variablesWindow != nullptr) {
					static_cast<void>(variablesWindow->replaceTextBuffer("(inferior exited)\n", "Variables"));
					variablesWindow->setReadOnly(true);
					variablesWindow->setFileChanged(false);
				}
			}
			publishGdbDebuggerState(payload.event.text.rfind("exited", 0) == 0 ? "exited" : "stopped", payload.event.file, payload.event.line);
			if (getEditor() != nullptr) getEditor()->clearDebuggerInstructionLine();
			if (getEditor() != nullptr && payload.event.line > 0 &&
			    (payload.event.file.empty() || normalizeConfiguredPathInput(payload.event.file) == gdbDebuggerSourcePath())) {
				getEditor()->setDebuggerInstructionLine(static_cast<std::size_t>(payload.event.line - 1));
				getEditor()->centerDocumentLocationInView(static_cast<std::size_t>(payload.event.line - 1), 1);
			}
			break;
		case MRGdbEventKind::Variables:
			if (variablesWindow != nullptr) {
				std::string text;
				gdbDebuggerVariableRows.clear();
				if (payload.event.variables.empty()) text = "(no variables in current frame)\n";
				else for (const MRGdbMiVariable &variable : payload.event.variables) {
					GdbDebuggerVariableRow row;

					row.start = text.size();
					row.expression = variable.name;
					row.value = variable.value;
					text += variable.name;
					if (!variable.type.empty()) text += " [" + variable.type + "]";
					text += " = " + variable.value;
					row.end = text.size();
					gdbDebuggerVariableRows.push_back(std::move(row));
					text += "\n";
				}
				static_cast<void>(variablesWindow->replaceTextBuffer(text.c_str(), "Variables"));
				variablesWindow->setReadOnly(true);
				variablesWindow->setFileChanged(false);
			}
			break;
		case MRGdbEventKind::Watches:
			if (watchesWindow != nullptr && !payload.event.text.empty()) {
				static_cast<void>(watchesWindow->replaceTextBuffer(payload.event.text.c_str(), "Watches"));
				watchesWindow->setReadOnly(true);
				watchesWindow->setFileChanged(false);
			}
			break;
		case MRGdbEventKind::Breakpoints:
			writeGdbBreakpointLines(gdbDebuggerSourcePath(), payload.event.breakpointLines);
			projectGdbBreakpointLines(getEditor(), payload.event.breakpointLines);
			break;
		case MRGdbEventKind::Finished:
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

bool MRBentoBox::clearGdbProgramTerminal() {
	MRGdbTerminalPane *terminalWindow = programTerminalPane();

	if (!gdbDebuggerActive() || terminalWindow == nullptr) return false;
	terminalWindow->clearTerminal();
	bentoProjectionDirty |= bpdContent;
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
	} else if (keyCode == kbF5 && modifiers == 0) kind = gdbDebuggerRunning() ? MRGdbCommandKind::PauseExecution : MRGdbCommandKind::ContinueExecution;
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

bool MRBentoBox::sendGdbCommand(MRGdbCommandKind commandKind, const std::string &text) {
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
	MRGdbCommand command(commandKind);
	command.text = text;
	if (commandKind == MRGdbCommandKind::ToggleBreakpoint || commandKind == MRGdbCommandKind::RunToLocation) {
		command.file = gdbDebuggerSourcePath();
		command.line = getEditor() != nullptr ? getEditor()->currentLineNumber() : 0;
		if (command.file.empty() || command.line <= 0) return false;
	}
	return gdbSession->send(std::move(command));
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
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
		VirtualMachine::Value applicationUi;
		VirtualMachine::Value debugger;
		VirtualMachine::Value sessions;
		if (runtimeKv.findRoot("APPLICATIONUI", applicationUi) && runtimeKv.findChild(applicationUi, "debugger", debugger) && runtimeKv.findChild(debugger, "sessions", sessions))
			static_cast<void>(runtimeKv.eraseChild(sessions, std::to_string(bufferId())));
	} catch (...) {
	}
}

std::string MRBentoBox::gdbDebuggerStateText() const {
	const std::string state = readGdbString(bufferId(), "state");
	if (state == "starting") return "GDB STARTING";
	if (state == "loaded") return "INFERIOR READY";
	if (state == "running") return "INFERIOR RUNNING";
	if (state == "stopped") {
		const int line = readGdbInt(bufferId(), "stopLine");
		return line > 0 ? "INFERIOR STOPPED L" + std::to_string(line) : "INFERIOR STOPPED";
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
