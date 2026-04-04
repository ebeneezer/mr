#define Uses_MsgBox
#include <tvision/tv.h>

#include "MRMacroRunner.hpp"

#include "mrmac.h"
#include "mrvm.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../ui/TMREditWindow.hpp"
#include "../ui/MRWindowSupport.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct PendingForegroundMacro {
	std::string label;
	std::shared_ptr<VirtualMachine> vm;

	PendingForegroundMacro() : label(), vm() {
	}

	PendingForegroundMacro(std::string aLabel, std::shared_ptr<VirtualMachine> aVm)
	    : label(std::move(aLabel)), vm(std::move(aVm)) {
	}
};

std::mutex g_pendingForegroundMacrosMutex;
std::vector<PendingForegroundMacro> g_pendingForegroundMacros;
void queuePendingForegroundMacro(const std::string &label, const std::shared_ptr<VirtualMachine> &vm);

bool hasMrmacExtension(const std::string &path) {
	std::string::size_type pos = path.rfind('.');
	if (pos == std::string::npos)
		return false;

	std::string ext = path.substr(pos);
	for (std::string::size_type i = 0; i < ext.size(); ++i)
		if (ext[i] >= 'A' && ext[i] <= 'Z')
			ext[i] = static_cast<char>(ext[i] - 'A' + 'a');

	return ext == ".mrmac";
}

std::string normalizeTvPath(const std::string &path) {
	std::string result = path;
	std::size_t i;

	for (i = 0; i < result.size(); ++i)
		if (result[i] == '\\')
			result[i] = '/';
#ifdef __unix__
	if (result.size() >= 2 && ((result[0] >= 'A' && result[0] <= 'Z') ||
	                           (result[0] >= 'a' && result[0] <= 'z')) &&
	    result[1] == ':')
		result.erase(0, 2);
#endif
	return result;
}

std::string trimPathInput(const std::string &path) {
	std::size_t start = 0;
	std::size_t end = path.size();

	while (start < end && std::isspace(static_cast<unsigned char>(path[start])) != 0)
		++start;
	while (end > start &&
	       (std::isspace(static_cast<unsigned char>(path[end - 1])) != 0 ||
	        static_cast<unsigned char>(path[end - 1]) < 32))
		--end;

	std::string result = path.substr(start, end - start);
	if (result.size() >= 2 &&
	    ((result.front() == '"' && result.back() == '"') || (result.front() == '\'' && result.back() == '\'')))
		result = result.substr(1, result.size() - 2);
	return result;
}

std::string expandUserPath(const char *path) {
	std::string result;

	if (path == 0)
		return std::string();

	result = normalizeTvPath(trimPathInput(path));
	if (result.size() >= 2 && result[0] == '~' && result[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != 0 && *home != '\0')
			return std::string(home) + result.substr(1);
	}

	return result;
}

bool readTextFile(const std::string &path, std::string &outContent, std::string &outError) {
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream buffer;

	if (!in) {
		outError = "Could not open file.";
		return false;
	}

	buffer << in.rdbuf();

	if (!in.good() && !in.eof()) {
		outError = "Error while reading file.";
		return false;
	}

	outContent = buffer.str();
	return true;
}

const char *backgroundMacroPolicyText(bool staged) noexcept {
	return staged ? "policy: snapshot + staged write, UI-thread commit, conflict=abort, cancel=cooperative"
	              : "policy: snapshot read-only, cancel=cooperative";
}

std::string joinNames(const std::vector<std::string> &names) {
	std::ostringstream out;

	for (std::size_t i = 0; i < names.size(); ++i) {
		if (i != 0)
			out << ", ";
		out << names[i];
	}
	return out.str();
}

void showErrorBox(const char *title, const char *text) {
	char msg[1024];

	if (title == 0)
		title = "Error";
	if (text == 0)
		text = "Unknown error.";

	std::snprintf(msg, sizeof(msg), "%s:\n\n%s", title, text);
	messageBox(mfError | mfOKButton, "%s", msg);
}

bool runMacroSource(const char *displayName, const char *source) {
	size_t bytecodeSize = 0;
	unsigned char *bytecode = 0;
	std::shared_ptr<VirtualMachine> vm = std::make_shared<VirtualMachine>();
	MRMacroExecutionProfile profile;
	std::vector<unsigned char> bytecodeCopy;
	std::string label = displayName != 0 ? displayName : "Macro Loader";
	TMREditWindow *win = currentEditWindow();
	std::uint64_t taskId = 0;

	if (source == 0) {
		showErrorBox("Macro Loader", "No macro source available.");
		return false;
	}
	if (!vm) {
		showErrorBox("Macro Loader", "Unable to create VM.");
		return false;
	}

	bytecode = compile_macro_code(source, &bytecodeSize);
	if (bytecode == 0) {
		const char *err = get_last_compile_error();
		if (err == 0 || *err == '\0')
			err = "Compilation failed.";
		showErrorBox(displayName != 0 ? displayName : "Macro Loader", err);
		return false;
	}

	profile = mrvmAnalyzeBytecode(bytecode, bytecodeSize);
	if (mrvmCanRunInBackground(profile)) {
		bytecodeCopy.assign(bytecode, bytecode + bytecodeSize);
		std::free(bytecode);
		bytecode = 0;
		taskId = mr::coprocessor::globalCoprocessor().submit(
		    mr::coprocessor::Lane::Macro, mr::coprocessor::TaskKind::MacroJob,
		    win != 0 ? static_cast<std::size_t>(win->bufferId()) : 0, 0, std::string("macro: ") + label,
		    [label, bytecodeCopy = std::move(bytecodeCopy)](
		        const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) mutable {
			    mr::coprocessor::Result result;
			    MRMacroJobResult runResult;

			    result.task = info;
			    if (stopToken.stop_requested() || info.cancelRequested()) {
				    result.status = mr::coprocessor::TaskStatus::Cancelled;
				    return result;
			    }
			    runResult = mrvmRunBytecodeBackground(bytecodeCopy.data(), bytecodeCopy.size(),
			                                          stopToken, info.cancelFlag);
			    if (runResult.cancelled) {
				    result.status = mr::coprocessor::TaskStatus::Cancelled;
				    return result;
			    }
			    result.status = mr::coprocessor::TaskStatus::Completed;
			    result.payload = std::make_shared<mr::coprocessor::MacroJobFinishedPayload>(
			        label, std::move(runResult.logLines), runResult.hadError);
			    return result;
		    });
		if (taskId == 0) {
			showErrorBox(label.c_str(), "Unable to start background macro worker.");
			return false;
		}
		if (win != 0) {
			win->trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::MacroJob, label);
			win->noteQueuedBackgroundMacro(label, false);
		}
		{
			std::string line = "Queued background-safe macro '";
			line += label;
			line += "' [task #";
			line += std::to_string(taskId);
			line += "] ";
			line += backgroundMacroPolicyText(false);
			mrLogMessage(line.c_str());
		}
		return true;
	}

	if (mrvmCanRunStagedInBackground(profile)) {
		MRMacroStagedExecutionInput stagedInput;
		TMRFileEditor *editor = win != 0 ? win->getEditor() : 0;

		if (win == 0 || editor == 0) {
			showErrorBox(label.c_str(), "No active editor window available for staged macro execution.");
			std::free(bytecode);
			return false;
		}

		bytecodeCopy.assign(bytecode, bytecode + bytecodeSize);
		std::free(bytecode);
		bytecode = 0;

		stagedInput.document = editor->documentCopy();
		stagedInput.baseVersion = editor->documentVersion();
		stagedInput.cursorOffset = editor->cursorOffset();
		stagedInput.selectionStart = editor->selectionStartOffset();
		stagedInput.selectionEnd = editor->selectionEndOffset();
		stagedInput.blockMode = win->blockStatus();
		stagedInput.blockMarkingOn = win->isBlockMarking();
		stagedInput.blockAnchor = win->blockAnchorPtr();
		stagedInput.blockEnd = win->blockEffectiveEndPtr();
		stagedInput.firstSave = win->hasBeenSavedInSession();
		stagedInput.eofInMemory = win->eofInMemory();
		stagedInput.bufferId = win->bufferId();
		stagedInput.temporaryFile = win->isTemporaryFile();
		stagedInput.temporaryFileName = win->temporaryFileName();
		stagedInput.currentWindow = mrvmUiCurrentWindowIndex(win);
		stagedInput.linkStatus = mrvmUiLinkStatus(win);
		stagedInput.windowCount = mrvmUiWindowCount();
		stagedInput.windowGeometryValid = mrvmUiWindowGeometry(
		    win, stagedInput.windowX1, stagedInput.windowY1, stagedInput.windowX2,
		    stagedInput.windowY2);
		mrvmUiCopyGlobals(stagedInput.globalOrder, stagedInput.globalInts,
		                  stagedInput.globalStrings);
		mrvmUiCopyLoadedMacros(stagedInput.macroOrder, stagedInput.macroDisplayNames);
		stagedInput.fileName = win->currentFileName();
		stagedInput.fileChanged = win->isFileChanged();
		stagedInput.lastSearchValid = mrvmUiCopyWindowLastSearch(
		    win, stagedInput.fileName, stagedInput.lastSearchStart, stagedInput.lastSearchEnd,
		    stagedInput.lastSearchCursor);
		mrvmUiCopyRuntimeOptions(stagedInput.ignoreCase, stagedInput.tabExpand);
		stagedInput.markStack = mrvmUiCopyWindowMarkStack(win);
		stagedInput.insertMode = editor->insertModeEnabled();
		stagedInput.indentLevel = win->indentLevel();
		stagedInput.pageLines = std::max(1, editor->size.y - 1);

		taskId = mr::coprocessor::globalCoprocessor().submit(
		    mr::coprocessor::Lane::Macro, mr::coprocessor::TaskKind::MacroJob,
		    static_cast<std::size_t>(win->bufferId()), stagedInput.baseVersion,
		    std::string("macro: ") + label,
		    [label, bytecodeCopy = std::move(bytecodeCopy), stagedInput = std::move(stagedInput)](
		        const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) mutable {
			    mr::coprocessor::Result result;
			    MRMacroStagedJobResult runResult;

			    result.task = info;
			    if (stopToken.stop_requested() || info.cancelRequested()) {
				    result.status = mr::coprocessor::TaskStatus::Cancelled;
				    return result;
			    }

			    runResult = mrvmRunBytecodeStagedBackground(bytecodeCopy.data(), bytecodeCopy.size(),
			                                                stagedInput, stopToken, info.cancelFlag);
			    if (runResult.cancelled) {
				    result.status = mr::coprocessor::TaskStatus::Cancelled;
				    return result;
			    }
			    result.status = mr::coprocessor::TaskStatus::Completed;
			    result.payload = std::make_shared<mr::coprocessor::MacroJobStagedPayload>(
			        label, std::move(runResult.logLines), runResult.hadError,
			        std::move(runResult.transaction), runResult.cursorOffset, runResult.selectionStart,
			        runResult.selectionEnd, runResult.blockMode, runResult.blockMarkingOn,
			        runResult.blockAnchor, runResult.blockEnd, std::move(runResult.globalOrder),
			        std::move(runResult.globalInts), std::move(runResult.globalStrings),
			        std::move(runResult.deferredUiCommands),
			        runResult.lastSearchValid,
			        runResult.lastSearchStart, runResult.lastSearchEnd, runResult.lastSearchCursor,
			        runResult.ignoreCase, runResult.tabExpand, std::move(runResult.markStack),
			        runResult.insertMode,
			        runResult.indentLevel, std::move(runResult.fileName), runResult.fileChanged);
			    return result;
		    });
		if (taskId == 0) {
			showErrorBox(label.c_str(), "Unable to start staged background macro worker.");
			return false;
		}
		win->trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::MacroJob, label);
		win->noteQueuedBackgroundMacro(label, true);
		{
			std::string line = "Queued staged-write macro '";
			line += label;
			line += "' [task #";
			line += std::to_string(taskId);
			line += "] ";
			line += backgroundMacroPolicyText(true);
			mrLogMessage(line.c_str());
		}
		return true;
	}

	{
		std::string line = "Running macro '" + label + "' on UI thread: " + mrvmDescribeExecutionProfile(profile);
		std::vector<std::string> unsupported = mrvmUnsupportedStagedSymbols(profile);

		if (!unsupported.empty())
			line += " [unsupported staged symbols: " + joinNames(unsupported) + "]";
		else if (profile.has(mrefExternalIo))
			line += " [contains external I/O]";
		mrLogMessage(line.c_str());
	}

	vm->execute(bytecode, bytecodeSize);
	std::free(bytecode);
	if (vm->hasPendingDelay()) {
		queuePendingForegroundMacro(label, vm);
		mrLogMessage(("Macro '" + label + "' yielded on DELAY; execution will resume asynchronously.").c_str());
	}
	return true;
}

void queuePendingForegroundMacro(const std::string &label, const std::shared_ptr<VirtualMachine> &vm) {
	std::lock_guard<std::mutex> lock(g_pendingForegroundMacrosMutex);
	g_pendingForegroundMacros.push_back(PendingForegroundMacro(label, vm));
}
} // namespace

void pumpForegroundMacroDelays() {
	std::lock_guard<std::mutex> lock(g_pendingForegroundMacrosMutex);
	std::size_t i = 0;

	while (i < g_pendingForegroundMacros.size()) {
		std::vector<PendingForegroundMacro>::difference_type index =
		    static_cast<std::vector<PendingForegroundMacro>::difference_type>(i);
		PendingForegroundMacro &pending = g_pendingForegroundMacros[i];
		if (!pending.vm) {
			g_pendingForegroundMacros.erase(g_pendingForegroundMacros.begin() + index);
			continue;
		}
		if (pending.vm->hasPendingDelay()) {
			if (pending.vm->resumePendingDelay()) {
				++i;
				continue;
			}
		}
		g_pendingForegroundMacros.erase(g_pendingForegroundMacros.begin() + index);
	}
}

void cancelForegroundMacroDelays() {
	std::lock_guard<std::mutex> lock(g_pendingForegroundMacrosMutex);

	for (std::size_t i = 0; i < g_pendingForegroundMacros.size(); ++i)
		if (g_pendingForegroundMacros[i].vm)
			g_pendingForegroundMacros[i].vm->cancelPendingDelay();
	g_pendingForegroundMacros.clear();
}

bool runMacroFileByPath(const char *path) {
	std::string resolvedPath = expandUserPath(path);
	std::string source;
	std::string ioError;

	if (resolvedPath.empty()) {
		showErrorBox("Macro Loader", "No file name specified.");
		return false;
	}

	if (!hasMrmacExtension(resolvedPath)) {
		showErrorBox("Macro Loader", "Only .mrmac files are allowed.");
		return false;
	}

	if (!readTextFile(resolvedPath, source, ioError)) {
		showErrorBox(resolvedPath.c_str(), ioError.c_str());
		return false;
	}

	return runMacroSource(resolvedPath.c_str(), source.c_str());
}
