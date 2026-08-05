#include "../app/utils/MRFileIOUtils.hpp"
#define Uses_MsgBox
#define Uses_TDisplay
#define Uses_TApplication
#define Uses_TProgram
#include <tvision/tv.h>

#include "MRMacroRunner.hpp"

#include "MRMacroExecutionSession.hpp"
#include "mrmac.h"
#include "MRVM.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRWindowSupport.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

bool captureMacroStagedExecutionInput(MREditWindow *window, MRMacroStagedExecutionInput &input, MacroCommitConflictSnapshot &conflictSnapshot) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;

	input = MRMacroStagedExecutionInput();
	conflictSnapshot = MacroCommitConflictSnapshot();
	if (window == nullptr || editor == nullptr) return false;
	input.document = editor->documentCopy();
	input.baseVersion = editor->documentVersion();
	input.cursorOffset = editor->cursorOffset();
	input.selectionStart = editor->selectionStartOffset();
	input.selectionEnd = editor->selectionEndOffset();
	input.blockMode = window->blockStatus();
	input.blockMarkingOn = window->isBlockMarking();
	input.blockAnchor = window->blockAnchorPtr();
	input.blockEnd = window->blockEffectiveEndPtr();
	input.firstSave = window->hasBeenSavedInSession();
	input.eofInMemory = window->eofInMemory();
	input.bufferId = window->bufferId();
	input.temporaryFile = window->isTemporaryFile();
	input.temporaryFileName = window->temporaryFileName();
	input.currentWindow = mrvmUiCurrentWindowIndex(window);
	input.linkStatus = mrvmUiLinkStatus(window);
	input.windowCount = mrvmUiWindowCount();
	input.windowGeometryValid = mrvmUiWindowGeometry(window, input.windowX1, input.windowY1, input.windowX2, input.windowY2);
	mrvmUiCopyGlobals(input.globalOrder, input.globalInts, input.globalStrings);
	mrvmUiCopyLoadedMacros(input.macroOrder, input.macroDisplayNames);
	input.fileName = window->currentFileName();
	input.fileChanged = window->isFileChanged();
	input.lastSearchValid = mrvmUiCopyWindowLastSearch(window, input.fileName, input.lastSearchStart, input.lastSearchEnd, input.lastSearchCursor);
	mrvmUiCopyRuntimeOptions(input.ignoreCase, input.tabExpand);
	input.markStack = mrvmUiCopyWindowMarkStack(window);
	input.insertMode = editor->insertModeEnabled();
	input.indentLevel = window->indentLevel();
	input.pageLines = std::max(1, editor->size.y - 1);
	input.screenWidth = mrvmUiScreenWidth();
	input.screenHeight = mrvmUiScreenHeight();
	static_cast<void>(mrvmUiCursorPosition(input.screenCursorX, input.screenCursorY));
	conflictSnapshot.cursorOffset = input.cursorOffset;
	conflictSnapshot.selectionStart = input.selectionStart;
	conflictSnapshot.selectionEnd = input.selectionEnd;
	conflictSnapshot.blockMode = input.blockMode;
	conflictSnapshot.blockMarkingOn = input.blockMarkingOn;
	conflictSnapshot.blockAnchor = input.blockAnchor;
	conflictSnapshot.blockEnd = input.blockEnd;
	conflictSnapshot.insertMode = input.insertMode;
	conflictSnapshot.indentLevel = input.indentLevel;
	conflictSnapshot.fileName = input.fileName;
	conflictSnapshot.fileChanged = input.fileChanged;
	conflictSnapshot.globalOrder = input.globalOrder;
	conflictSnapshot.globalInts = input.globalInts;
	conflictSnapshot.globalStrings = input.globalStrings;
	conflictSnapshot.lastSearchValid = input.lastSearchValid;
	conflictSnapshot.lastSearchStart = input.lastSearchStart;
	conflictSnapshot.lastSearchEnd = input.lastSearchEnd;
	conflictSnapshot.lastSearchCursor = input.lastSearchCursor;
	conflictSnapshot.ignoreCase = input.ignoreCase;
	conflictSnapshot.tabExpand = input.tabExpand;
	conflictSnapshot.markStack = input.markStack;
	conflictSnapshot.bufferId = input.bufferId;
	conflictSnapshot.linkStatus = input.linkStatus;
	conflictSnapshot.windowCount = input.windowCount;
	conflictSnapshot.windowGeometryValid = input.windowGeometryValid;
	conflictSnapshot.windowX1 = input.windowX1;
	conflictSnapshot.windowY1 = input.windowY1;
	conflictSnapshot.windowX2 = input.windowX2;
	conflictSnapshot.windowY2 = input.windowY2;
	return true;
}

namespace {
struct PendingForegroundMacro {
	MRMacroExecutionSessionId sessionId = 0;
	std::shared_ptr<VirtualMachine> vm;

	PendingForegroundMacro() {
	}

	PendingForegroundMacro(MRMacroExecutionSessionId aSessionId, std::shared_ptr<VirtualMachine> aVm) : sessionId(aSessionId), vm(std::move(aVm)) {
	}
};

std::mutex pendingForegroundMacrosMutex;
std::vector<PendingForegroundMacro> pendingForegroundMacros;
void queuePendingForegroundMacro(const MRMacroExecutionSession &session, const std::string &label, const std::shared_ptr<VirtualMachine> &vm);
std::size_t cancelForegroundMacroDelaysForOwner(const MRMacroExecutionOwner &owner);

MRMacroExecutionOwner macroExecutionOwnerForWindow(const MREditWindow *win) noexcept {
	MRMacroExecutionOwner owner;

	if (win == nullptr) return owner;
	owner.hasBuffer = true;
	owner.bufferId = win->bufferId();
	return owner;
}

MRMacroExecutionSession makeMacroExecutionSessionForOwner(const std::string &label, MRMacroExecutionRoute route, const MREditWindow *win, const MRMacroExecutionOwner *ownerOverride) {
	return createMacroExecutionSession(label, route, ownerOverride != nullptr ? *ownerOverride : macroExecutionOwnerForWindow(win));
}

std::string sessionLogSuffix(const MRMacroExecutionSession &session) {
	return " [session #" + std::to_string(session.sessionId) + "]";
}

bool hasMrmacExtension(const std::string &path) {
	std::string::size_type pos = path.rfind('.');
	if (pos == std::string::npos) return false;

	std::string ext = path.substr(pos);
	for (std::size_t index = 0; index < ext.size(); ++index)
		if (ext[index] >= 'A' && ext[index] <= 'Z') ext[index] = static_cast<char>(ext[index] - 'A' + 'a');

	return ext == ".mrmac";
}

std::string normalizeTvPath(const std::string &path) {
	std::string result = path;
	std::size_t i;

	for (i = 0; i < result.size(); ++i)
		if (result[i] == '\\') result[i] = '/';
#ifdef __unix__
	if (result.size() >= 2 && ((result[0] >= 'A' && result[0] <= 'Z') || (result[0] >= 'a' && result[0] <= 'z')) && result[1] == ':') result.erase(0, 2);
#endif
	return result;
}

std::string trimPathInput(const std::string &path) {
	std::size_t start = 0;
	std::size_t end = path.size();

	while (start < end && std::isspace(static_cast<unsigned char>(path[start])) != 0)
		++start;
	while (end > start && (std::isspace(static_cast<unsigned char>(path[end - 1])) != 0 || static_cast<unsigned char>(path[end - 1]) < 32))
		--end;

	std::string result = path.substr(start, end - start);
	if (result.size() >= 2 && ((result.front() == '"' && result.back() == '"') || (result.front() == '\'' && result.back() == '\''))) result = result.substr(1, result.size() - 2);
	return result;
}

std::string upperAscii(std::string value) {
	for (std::size_t index = 0; index < value.size(); ++index)
		value[index] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[index])));
	return value;
}

std::string baseNameOfPath(const std::string &path) {
	const std::size_t pos = path.find_last_of('/');
	return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string stemOfPath(const std::string &path) {
	std::string baseName = baseNameOfPath(path);
	const std::size_t dotPos = baseName.rfind('.');
	return dotPos == std::string::npos ? baseName : baseName.substr(0, dotPos);
}

std::string escapeMrmacSingleQuotedLiteral(std::string_view value) {
	std::string escaped;
	escaped.reserve(value.size());
	for (std::size_t index = 0; index < value.size(); ++index) {
		const char ch = value[index];

		escaped.push_back(ch);
		if (ch == '\'') escaped.push_back('\'');
	}
	return escaped;
}

bool selectPlaybackMacro(const std::string &resolvedPath, const std::string &source, std::string &macroName, std::string &errorText, MRMacroExecutionProfile *profileOut = nullptr) {
	size_t bytecodeSize = 0;
	unsigned char *bytecode = compile_macro_code(source.c_str(), &bytecodeSize);
	const int macroCount = get_compiled_macro_count();
	const std::string preferredName = upperAscii(stemOfPath(resolvedPath));

	macroName.clear();
	errorText.clear();
	if (bytecode == nullptr) {
		const char *compileError = get_last_compile_error();
		errorText = compileError != nullptr && *compileError != '\0' ? compileError : "Compilation failed.";
		return false;
	}

	if (profileOut != nullptr) *profileOut = mrvmAnalyzeBytecode(bytecode, bytecodeSize);

	for (int i = 0; i < macroCount; ++i) {
		const char *compiledName = get_compiled_macro_name(i);
		if (compiledName == nullptr || *compiledName == '\0') continue;
		if (macroName.empty()) macroName = compiledName;
		if (upperAscii(compiledName) == preferredName) {
			macroName = compiledName;
			break;
		}
	}
	std::free(bytecode);

	if (!macroName.empty()) return true;

	errorText = "No macros found in file.";
	return false;
}

std::string expandUserPath(const char *path) {
	std::string result;

	if (path == nullptr) return std::string();

	result = normalizeTvPath(trimPathInput(path));
	if (result.size() >= 2 && result[0] == '~' && result[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != nullptr && *home != '\0') return std::string(home) + result.substr(1);
	}

	return result;
}

const char *backgroundMacroPolicyText(bool staged) noexcept {
	return staged ? "policy: snapshot + staged ops, UI-thread commit/playback, conflict=abort, cancel=cooperative" : "policy: snapshot read-only, cancel=cooperative";
}

std::string joinNames(const std::vector<std::string> &names);

std::string buildExecutionRouteLogLine(const std::string &label, const char *route, const MRMacroExecutionProfile &profile) {
	std::string line = "Macro '";
	line += label;
	line += "' route=";
	line += route;
	line += " profile=";
	line += mrvmDescribeExecutionProfile(profile);

	std::vector<std::string> unsupported = mrvmUnsupportedStagedSymbols(profile);
	if (!unsupported.empty()) line += " [unsupported staged symbols: " + joinNames(unsupported) + "]";
	else if (profile.has(mrefExternalIo))
		line += " [contains external I/O]";
	return line;
}

std::string joinNames(const std::vector<std::string> &names) {
	std::ostringstream out;

	for (std::size_t i = 0; i < names.size(); ++i) {
		if (i != 0) out << ", ";
		out << names[i];
	}
	return out.str();
}

void showErrorBox(const char *title, const char *text) {
	if (title == nullptr) title = "Error";
	if (text == nullptr) text = "Unknown error.";

	messageBox(mfError | mfOKButton, "%s:\n\n%s", title, text);
}

bool runMacroSource(const char *displayName, const char *source, const MRMacroExecutionProfile *routeProfile, std::string *errorMessage, bool showErrorDialogs, MRMacroExecutionSession *sessionOut = nullptr, const MRMacroExecutionOwner *ownerOverride = nullptr, const char *unitName = nullptr, const char *closureId = nullptr, bool logRoute = true) {
	size_t bytecodeSize = 0;
	unsigned char *bytecode = nullptr;
	std::shared_ptr<VirtualMachine> vm = std::make_shared<VirtualMachine>();
	MRMacroExecutionProfile profile;
	std::vector<unsigned char> bytecodeCopy;
	std::string label = displayName != nullptr ? displayName : "Macro Loader";
	std::string selectedUnitName = unitName != nullptr ? trimPathInput(unitName) : std::string();
	std::string selectedClosureId = closureId != nullptr ? trimPathInput(closureId) : std::string();
	std::size_t entryOffset = 0;
	std::size_t profileOffset = 0;
	std::size_t profileLength = 0;
	MREditWindow *win = currentEditWindow();
	std::uint64_t taskId = 0;

	if (sessionOut != nullptr) *sessionOut = MRMacroExecutionSession();
	if (errorMessage != nullptr) errorMessage->clear();
	if (source == nullptr) {
		if (errorMessage != nullptr) *errorMessage = "No macro source available.";
		if (showErrorDialogs) showErrorBox("Macro Loader", "No macro source available.");
		return false;
	}
	if (!vm) {
		if (errorMessage != nullptr) *errorMessage = "Unable to create VM.";
		if (showErrorDialogs) showErrorBox("Macro Loader", "Unable to create VM.");
		return false;
	}

	bytecode = compile_macro_code(source, &bytecodeSize);
	if (bytecode == nullptr) {
		const char *err = get_last_compile_error();
		if (err == nullptr || *err == '\0') err = "Compilation failed.";
		if (errorMessage != nullptr) *errorMessage = err;
		if (showErrorDialogs) showErrorBox(displayName != nullptr ? displayName : "Macro Loader", err);
		return false;
	}
	if (!selectedUnitName.empty()) {
		bool found = false;
		const int macroCount = get_compiled_macro_count();
		const std::string selectedKey = upperAscii(selectedUnitName);
		int selectedIndex = -1;

		for (int i = 0; i < macroCount; ++i) {
			const char *compiledName = get_compiled_macro_name(i);
			const int entry = get_compiled_macro_entry(i);

			if (compiledName == nullptr || entry < 0) continue;
			if (upperAscii(compiledName) != selectedKey) continue;
			entryOffset = static_cast<std::size_t>(entry);
			selectedIndex = i;
			found = true;
			break;
		}
		if (!found) {
			std::free(bytecode);
			if (errorMessage != nullptr) *errorMessage = "Compiled MRMac unit not found: " + selectedUnitName;
			if (showErrorDialogs) showErrorBox(displayName != nullptr ? displayName : "Macro Loader", errorMessage != nullptr ? errorMessage->c_str() : "Compiled MRMac unit not found.");
			return false;
		}
		profileOffset = entryOffset;
		profileLength = bytecodeSize - profileOffset;
		for (int i = 0; i < macroCount; ++i) {
			const int entry = get_compiled_macro_entry(i);
			if (i == selectedIndex || entry < 0) continue;
			if (static_cast<std::size_t>(entry) > entryOffset && static_cast<std::size_t>(entry) - entryOffset < profileLength) profileLength = static_cast<std::size_t>(entry) - entryOffset;
		}
	}
	if (!selectedUnitName.empty()) label = selectedUnitName;

	profile = routeProfile != nullptr ? *routeProfile : mrvmAnalyzeBytecode(bytecode + profileOffset, profileLength != 0 ? profileLength : bytecodeSize);
	if (mrvmCanRunInBackground(profile)) {
		MRMacroExecutionSession session = makeMacroExecutionSessionForOwner(label, MRMacroExecutionRoute::Background, win, ownerOverride);
		const std::string routeLogLine = buildExecutionRouteLogLine(label, "background", profile) + sessionLogSuffix(session);
		mrLogMessage(routeLogLine.c_str());
		bytecodeCopy.assign(bytecode, bytecode + bytecodeSize);
		std::free(bytecode);
		bytecode = nullptr;
		taskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Macro, mr::coprocessor::TaskKind::MacroJob, win != nullptr ? static_cast<std::size_t>(win->bufferId()) : 0, 0, mr::coprocessor::ExecutionOwnerKind::MacroSession, static_cast<std::size_t>(session.sessionId), std::string("macro: ") + label, [label, bytecodeCopy = std::move(bytecodeCopy), entryOffset, selectedClosureId, sessionId = session.sessionId](const mr::coprocessor::TaskInfo &info) mutable {
			mr::coprocessor::Result result;
			MRMacroJobResult runResult;

			result.task = info;
			if (info.cancelRequested()) {
				result.status = mr::coprocessor::TaskStatus::Cancelled;
				return result;
			}
			runResult = mrvmRunBytecodeBackgroundAt(bytecodeCopy.data(), bytecodeCopy.size(), entryOffset, label, selectedClosureId, sessionId, info.cancelFlag);
			if (runResult.cancelled) {
				result.status = mr::coprocessor::TaskStatus::Cancelled;
				return result;
			}
			result.status = mr::coprocessor::TaskStatus::Completed;
			result.payload = std::make_shared<mr::coprocessor::MacroJobFinishedPayload>(label, std::move(runResult.logLines), std::move(runResult.execUiCommandRequests), runResult.hadError);
			return result;
		});
		if (taskId == 0) {
			if (errorMessage != nullptr) *errorMessage = "Unable to start background macro worker.";
			if (showErrorDialogs) showErrorBox(label.c_str(), "Unable to start background macro worker.");
			return false;
		}
		session.taskId = taskId;
		trackMacroExecutionSession(session);
		if (sessionOut != nullptr) *sessionOut = session;
		if (win != nullptr) {
			win->trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::MacroJob, label);
			win->noteQueuedBackgroundMacro(label, false);
		}
		if (logRoute) {
			std::string line = "Queued background-safe macro '";
			line += label;
			line += "' [session #";
			line += std::to_string(session.sessionId);
			line += ", task #";
			line += std::to_string(taskId);
			line += "] ";
			line += backgroundMacroPolicyText(false);
			mrLogMessage(line.c_str());
		}
		notifyMacroExecutionSessionChanged();
		return true;
	}

	if (selectedUnitName.empty() && mrvmCanRunStagedInBackground(profile)) {
		MRMacroStagedExecutionInput stagedInput;
		MacroCommitConflictSnapshot conflictSnapshot;
		MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

		if (win != nullptr && editor != nullptr && captureMacroStagedExecutionInput(win, stagedInput, conflictSnapshot)) {
			MRMacroExecutionSession session = makeMacroExecutionSessionForOwner(label, MRMacroExecutionRoute::StagedBackground, win, ownerOverride);
			const std::string routeLogLine = buildExecutionRouteLogLine(label, "staged", profile) + sessionLogSuffix(session);

			if (logRoute) mrLogMessage(routeLogLine.c_str());
			bytecodeCopy.assign(bytecode, bytecode + bytecodeSize);
			std::free(bytecode);
			bytecode = nullptr;

			taskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Macro, mr::coprocessor::TaskKind::MacroJob, static_cast<std::size_t>(win->bufferId()), stagedInput.baseVersion,
			                                                   mr::coprocessor::ExecutionOwnerKind::MacroSession, static_cast<std::size_t>(session.sessionId), std::string("macro: ") + label, [label, bytecodeCopy = std::move(bytecodeCopy), stagedInput = std::move(stagedInput), conflictSnapshot = std::move(conflictSnapshot), sessionId = session.sessionId](const mr::coprocessor::TaskInfo &info) mutable {
				mr::coprocessor::Result result;
				MRMacroStagedJobResult runResult;

				result.task = info;
				if (info.cancelRequested()) {
					result.status = mr::coprocessor::TaskStatus::Cancelled;
					return result;
				}

				runResult = mrvmRunBytecodeStagedBackground(bytecodeCopy.data(), bytecodeCopy.size(), stagedInput, sessionId, info.cancelFlag);
				if (runResult.cancelled) {
					result.status = mr::coprocessor::TaskStatus::Cancelled;
					return result;
				}
				runResult.conflictSnapshot = conflictSnapshot;
				result.status = mr::coprocessor::TaskStatus::Completed;
				result.payload = std::make_shared<mr::coprocessor::MacroJobStagedPayload>(label, std::move(runResult.logLines), runResult.hadError, std::move(runResult.conflictSnapshot), std::move(runResult.transaction), runResult.cursorOffset, runResult.selectionStart, runResult.selectionEnd, runResult.blockMode, runResult.blockMarkingOn, runResult.blockAnchor, runResult.blockEnd, std::move(runResult.globalOrder), std::move(runResult.globalInts), std::move(runResult.globalStrings), std::move(runResult.deferredUiCommands), runResult.lastSearchValid, runResult.lastSearchStart, runResult.lastSearchEnd, runResult.lastSearchCursor, runResult.ignoreCase, runResult.tabExpand, std::move(runResult.markStack), runResult.insertMode, runResult.indentLevel, std::move(runResult.fileName), runResult.fileChanged);
				return result;
			});
			if (taskId == 0) {
				if (errorMessage != nullptr) *errorMessage = "Unable to start staged background macro worker.";
				if (showErrorDialogs) showErrorBox(label.c_str(), "Unable to start staged background macro worker.");
				return false;
			}
			session.taskId = taskId;
			trackMacroExecutionSession(session);
			if (sessionOut != nullptr) *sessionOut = session;
			win->trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::MacroJob, label);
			win->noteQueuedBackgroundMacro(label, true);
			if (logRoute) {
				std::string line = "Queued staged macro '";
				line += label;
				line += "' [session #";
				line += std::to_string(session.sessionId);
				line += ", task #";
				line += std::to_string(taskId);
				line += "] ";
				line += backgroundMacroPolicyText(true);
				mrLogMessage(line.c_str());
			}
			notifyMacroExecutionSessionChanged();
			return true;
		}
		if (logRoute) mrLogMessage(("Staged execution skipped for macro '" + label + "': no active editor window, running on UI thread.").c_str());
	}

	MRMacroExecutionSession session = makeMacroExecutionSessionForOwner(label, MRMacroExecutionRoute::UiThread, win, ownerOverride);
	if (logRoute) {
		const std::string routeLogLine = buildExecutionRouteLogLine(label, "ui-thread", profile) + sessionLogSuffix(session);
		mrLogMessage(routeLogLine.c_str());
	}

	vm->setAsyncDelayEnabled(true);
	vm->setExecutionSessionContext(session.sessionId);
	if (!selectedClosureId.empty()) vm->setClosureContext(selectedClosureId);
	vm->executeAt(bytecode, bytecodeSize, entryOffset, std::string(), label, true, false);
	std::free(bytecode);
	if (vm->hasPendingDelay()) {
		session.route = MRMacroExecutionRoute::ForegroundDelay;
		session.state = MRMacroExecutionState::Yielded;
		queuePendingForegroundMacro(session, label, vm);
		if (sessionOut != nullptr) *sessionOut = session;
		mrLogMessage(("Macro '" + label + "' yielded on DELAY; execution will resume asynchronously" + sessionLogSuffix(session) + ".").c_str());
		notifyMacroExecutionSessionChanged();
	} else {
		session.state = vm->wasCancelled() ? MRMacroExecutionState::Cancelled : MRMacroExecutionState::Completed;
		if (sessionOut != nullptr) *sessionOut = session;
		publishMacroExecutionResult(session, session.state, vm->wasCancelled() ? "UI-thread macro session cancelled." : "UI-thread macro session completed.");
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

void queuePendingForegroundMacro(const MRMacroExecutionSession &session, const std::string &label, const std::shared_ptr<VirtualMachine> &vm) {
	std::lock_guard<std::mutex> lock(pendingForegroundMacrosMutex);
	(void)label;
	mrvmStorePendingForegroundMacroExecutionSession(session);
	pendingForegroundMacros.push_back(PendingForegroundMacro(session.sessionId, vm));
}

std::size_t cancelForegroundMacroDelaysForOwner(const MRMacroExecutionOwner &owner) {
	std::vector<MRMacroExecutionResult> results;
	{
		std::lock_guard<std::mutex> lock(pendingForegroundMacrosMutex);
		std::size_t i = 0;

		while (i < pendingForegroundMacros.size()) {
			std::vector<PendingForegroundMacro>::difference_type index = static_cast<std::vector<PendingForegroundMacro>::difference_type>(i);
			PendingForegroundMacro &pendingForegroundMacro = pendingForegroundMacros[i];
			MRMacroExecutionSession session;
			if (!mrvmReadPendingForegroundMacroExecutionSession(pendingForegroundMacro.sessionId, session) || !macroExecutionOwnerMatches(session.owner, owner)) {
				++i;
				continue;
			}
			if (pendingForegroundMacro.vm) pendingForegroundMacro.vm->cancelPendingDelay();
			session.state = MRMacroExecutionState::Cancelled;
			MRMacroExecutionResult result;
			result.session = session;
			result.state = MRMacroExecutionState::Cancelled;
			result.message = "Foreground DELAY session cancelled by owner.";
			results.push_back(result);
			mrvmRemovePendingForegroundMacroExecutionSession(pendingForegroundMacro.sessionId);
			pendingForegroundMacros.erase(pendingForegroundMacros.begin() + index);
		}
	}
	for (std::size_t index = 0; index < results.size(); ++index) {
		const MRMacroExecutionResult &result = results[index];

		publishMacroExecutionResult(result.session, result.state, result.message);
	}
	return results.size();
}
} // namespace

std::vector<MRMacroExecutionSession> pendingForegroundMacroExecutionSessions() {
	std::lock_guard<std::mutex> lock(pendingForegroundMacrosMutex);
	return mrvmPendingForegroundMacroExecutionSessions();
}

void pumpForegroundMacroDelays() {
	std::vector<MRMacroExecutionResult> results;
	{
		std::lock_guard<std::mutex> lock(pendingForegroundMacrosMutex);
		std::size_t i = 0;

		while (i < pendingForegroundMacros.size()) {
			std::vector<PendingForegroundMacro>::difference_type index = static_cast<std::vector<PendingForegroundMacro>::difference_type>(i);
			PendingForegroundMacro &pending = pendingForegroundMacros[i];
			MRMacroExecutionSession session;
			if (!mrvmReadPendingForegroundMacroExecutionSession(pending.sessionId, session)) {
				pendingForegroundMacros.erase(pendingForegroundMacros.begin() + index);
				continue;
			}
			if (!pending.vm) {
				session.state = MRMacroExecutionState::Failed;
				MRMacroExecutionResult result;
				result.session = session;
				result.state = MRMacroExecutionState::Failed;
				result.message = "Foreground DELAY session failed: missing VM.";
				results.push_back(result);
				mrvmRemovePendingForegroundMacroExecutionSession(pending.sessionId);
				pendingForegroundMacros.erase(pendingForegroundMacros.begin() + index);
				continue;
			}
			if (pending.vm->hasPendingDelay()) {
				if (pending.vm->resumePendingDelay()) {
					++i;
					continue;
				}
			}
			session.state = pending.vm->wasCancelled() ? MRMacroExecutionState::Cancelled : MRMacroExecutionState::Completed;
			MRMacroExecutionResult result;
			result.session = session;
			result.state = session.state;
			result.message = pending.vm->wasCancelled() ? "Foreground DELAY session cancelled." : "Foreground DELAY session completed.";
			results.push_back(result);
			mrvmRemovePendingForegroundMacroExecutionSession(pending.sessionId);
			pendingForegroundMacros.erase(pendingForegroundMacros.begin() + index);
		}
	}
	for (std::size_t index = 0; index < results.size(); ++index) {
		const MRMacroExecutionResult &result = results[index];

		publishMacroExecutionResult(result.session, result.state, result.message);
	}
}

void cancelForegroundMacroDelays() {
	std::vector<MRMacroExecutionResult> results;
	{
		std::lock_guard<std::mutex> lock(pendingForegroundMacrosMutex);

		for (std::size_t index = 0; index < pendingForegroundMacros.size(); ++index) {
			PendingForegroundMacro &pendingForegroundMacro = pendingForegroundMacros[index];

			if (pendingForegroundMacro.vm) {
				MRMacroExecutionSession session;
				if (!mrvmReadPendingForegroundMacroExecutionSession(pendingForegroundMacro.sessionId, session)) continue;
				pendingForegroundMacro.vm->cancelPendingDelay();
				session.state = MRMacroExecutionState::Cancelled;
				MRMacroExecutionResult result;
				result.session = session;
				result.state = MRMacroExecutionState::Cancelled;
				result.message = "Foreground DELAY session cancelled.";
				results.push_back(result);
				mrvmRemovePendingForegroundMacroExecutionSession(pendingForegroundMacro.sessionId);
			}
		}
		pendingForegroundMacros.clear();
	}
	for (std::size_t index = 0; index < results.size(); ++index) {
		const MRMacroExecutionResult &result = results[index];

		publishMacroExecutionResult(result.session, result.state, result.message);
	}
}

std::size_t requestMacroExecutionCancellationForOwner(const MRMacroExecutionOwner &owner) {
	std::vector<MRMacroExecutionSession> activeSessions;
	std::size_t cancelledCount = 0;

	activeSessions = activeMacroExecutionSessionsForOwner(owner);
	for (std::size_t index = 0; index < activeSessions.size(); ++index) {
		const MRMacroExecutionSession &session = activeSessions[index];

		if (session.taskId == 0) continue;
		if (!mr::coprocessor::globalCoprocessor().cancelTask(session.taskId)) continue;
		markMacroExecutionSessionCancellationRequestedForTask(session.taskId);
		++cancelledCount;
	}
	cancelledCount += cancelForegroundMacroDelaysForOwner(owner);
	return cancelledCount;
}

bool runMacroFileByPathRouted(const char *path, bool forceUiThread, std::string *errorMessage, bool showErrorDialogs) {
	std::string resolvedPath = expandUserPath(path);
	std::string source;
	std::string ioError;
	std::string macroName;
	std::string loadError;
	std::string runnerSource;
	std::string macroSpec;
	MRMacroExecutionProfile targetProfile;
	MRMacroExecutionProfile uiThreadProfile;

	if (errorMessage != nullptr) errorMessage->clear();
	if (resolvedPath.empty()) {
		if (errorMessage != nullptr) *errorMessage = "No file name specified.";
		if (showErrorDialogs) showErrorBox("Macro Loader", "No file name specified.");
		return false;
	}

	if (!hasMrmacExtension(resolvedPath)) {
		if (errorMessage != nullptr) *errorMessage = "Only .mrmac files are allowed.";
		if (showErrorDialogs) showErrorBox("Macro Loader", "Only .mrmac files are allowed.");
		return false;
	}

	if (!readTextFile(resolvedPath, source, ioError)) {
		if (errorMessage != nullptr) *errorMessage = ioError;
		if (showErrorDialogs) showErrorBox(resolvedPath.c_str(), ioError.c_str());
		return false;
	}

	if (!selectPlaybackMacro(resolvedPath, source, macroName, loadError, &targetProfile)) {
		if (errorMessage != nullptr) *errorMessage = loadError;
		if (showErrorDialogs) showErrorBox(resolvedPath.c_str(), loadError.c_str());
		return false;
	}

	if (!mrvmLoadMacroFile(resolvedPath, &loadError)) {
		if (errorMessage != nullptr) *errorMessage = loadError;
		if (showErrorDialogs) showErrorBox(resolvedPath.c_str(), loadError.c_str());
		return false;
	}

	macroSpec = resolvedPath + "^" + macroName;
	runnerSource = "$MACRO MacroPlaybackLauncher;\nRUN_MACRO('" + escapeMrmacSingleQuotedLiteral(macroSpec) + "');\nEND_MACRO;\n";
	if (!runMacroSource(macroSpec.c_str(), runnerSource.c_str(), forceUiThread ? &uiThreadProfile : &targetProfile, errorMessage, showErrorDialogs)) {
		if (errorMessage != nullptr && errorMessage->empty()) *errorMessage = "Macro execution failed.";
		return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool runMacroFileByPath(const char *path, std::string *errorMessage, bool showErrorDialogs) {
	return runMacroFileByPathRouted(path, false, errorMessage, showErrorDialogs);
}

bool runMacroFileByPathOnUiThread(const char *path, std::string *errorMessage, bool showErrorDialogs) {
	return runMacroFileByPathRouted(path, true, errorMessage, showErrorDialogs);
}

bool runMacroSourceText(const char *displayName, const char *source, std::string *errorMessage, bool showErrorDialogs) {
	if (!runMacroSource(displayName, source, nullptr, errorMessage, showErrorDialogs)) {
		if (errorMessage != nullptr && errorMessage->empty()) *errorMessage = "Macro execution failed.";
		return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool runMacroSourceTextAsExecutionSessionForOwner(const char *displayName, const char *source, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut, std::string *errorMessage, bool showErrorDialogs) {
	if (sessionOut != nullptr) *sessionOut = MRMacroExecutionSession();
	if (source == nullptr || *source == '\0') {
		if (errorMessage != nullptr) *errorMessage = "No macro source available.";
		if (showErrorDialogs) showErrorBox(displayName != nullptr ? displayName : "Macro Loader", "No macro source available.");
		return false;
	}
	if (!runMacroSource(displayName, source, nullptr, errorMessage, showErrorDialogs, sessionOut, &owner)) {
		if (errorMessage != nullptr && errorMessage->empty()) *errorMessage = "Macro execution failed.";
		return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool runMacroSourceUnitAsExecutionSessionForOwner(const char *displayName, const char *source, const char *unitName, const char *closureId, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut, std::string *errorMessage, bool showErrorDialogs) {
	if (sessionOut != nullptr) *sessionOut = MRMacroExecutionSession();
	if (source == nullptr || *source == '\0') {
		if (errorMessage != nullptr) *errorMessage = "No macro source available.";
		if (showErrorDialogs) showErrorBox(displayName != nullptr ? displayName : "Macro Loader", "No macro source available.");
		return false;
	}
	if (unitName == nullptr || *unitName == '\0') {
		if (errorMessage != nullptr) *errorMessage = "No MRMac unit specified.";
		if (showErrorDialogs) showErrorBox(displayName != nullptr ? displayName : "Macro Loader", "No MRMac unit specified.");
		return false;
	}
	if (!runMacroSource(displayName, source, nullptr, errorMessage, showErrorDialogs, sessionOut, &owner, unitName, closureId)) {
		if (errorMessage != nullptr && errorMessage->empty()) *errorMessage = "Macro execution failed.";
		return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool runMacroSpecByName(const char *macroSpec, std::string *errorMessage, bool showErrorDialogs) {
	std::string spec = macroSpec != nullptr ? trimPathInput(macroSpec) : std::string();
	std::string runError;

	if (errorMessage != nullptr) errorMessage->clear();
	if (spec.empty()) {
		if (errorMessage != nullptr) *errorMessage = "No macro specification specified.";
		if (showErrorDialogs) showErrorBox("Macro Runner", "No macro specification specified.");
		return false;
	}
	if (!mrvmRunMacroSpec(spec, &runError)) {
		if (errorMessage != nullptr) *errorMessage = runError;
		if (showErrorDialogs) showErrorBox(spec.c_str(), runError.empty() ? "Macro execution failed." : runError.c_str());
		return false;
	}
	return true;
}

namespace {
bool runMacroSpecByNameAsExecutionSessionForOwnerRouted(const char *macroSpec, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut, std::string *errorMessage, bool showErrorDialogs, bool forceUiThread) {
	std::string spec = macroSpec != nullptr ? trimPathInput(macroSpec) : std::string();
	std::string runnerSource;
	MRMacroExecutionProfile uiThreadProfile;

	if (sessionOut != nullptr) *sessionOut = MRMacroExecutionSession();
	if (errorMessage != nullptr) errorMessage->clear();
	if (spec.empty()) {
		if (errorMessage != nullptr) *errorMessage = "No macro specification specified.";
		if (showErrorDialogs) showErrorBox("Macro Runner", "No macro specification specified.");
		return false;
	}
	runnerSource = "$MACRO ScheduledMacroLauncher;\nRUN_MACRO('" + escapeMrmacSingleQuotedLiteral(spec) + "');\nEND_MACRO;\n";
	if (!runMacroSource(spec.c_str(), runnerSource.c_str(), forceUiThread ? &uiThreadProfile : nullptr, errorMessage, showErrorDialogs, sessionOut, &owner, nullptr, nullptr, false)) {
		if (errorMessage != nullptr && errorMessage->empty()) *errorMessage = "Macro execution failed.";
		return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}
} // namespace

bool runMacroSpecByNameAsExecutionSessionForOwner(const char *macroSpec, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut, std::string *errorMessage, bool showErrorDialogs) {
	return runMacroSpecByNameAsExecutionSessionForOwnerRouted(macroSpec, owner, sessionOut, errorMessage, showErrorDialogs, false);
}

bool runMacroSpecByNameAsExecutionSessionForOwnerOnUiThread(const char *macroSpec, const MRMacroExecutionOwner &owner, MRMacroExecutionSession *sessionOut, std::string *errorMessage, bool showErrorDialogs) {
	return runMacroSpecByNameAsExecutionSessionForOwnerRouted(macroSpec, owner, sessionOut, errorMessage, showErrorDialogs, true);
}

bool runMacroFileByPath(const char *path) {
	return runMacroFileByPath(path, nullptr, true);
}
