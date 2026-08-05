#include <unordered_map>
#include "../app/MRVersion.hpp"
#include "../app/utils/MRConstants.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#include "../app/utils/MRStringUtils.hpp"
#define Uses_MsgBox
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TButton
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TStaticText
#define Uses_TScrollBar
#define Uses_TListViewer
#define Uses_TStatusLine
#define Uses_TObject
#define Uses_TScreen
#define Uses_TDrawBuffer
#define Uses_TView
#define Uses_TClipboard
#include <tvision/tv.h>

#include "mrmac.h"
#include "ui/modeless/MRMacroModelessUi.hpp"
#include "MRMacroRunner.hpp"
#include "MRVM.hpp"
#include "MRVMDebugSession.hpp"
#include "vm/MRVMExecSessions.hpp"
#include "ui/conventional/MRVMDeferredUi.hpp"
#include "vm/MRVMHash.hpp"
#include "vm/MRVMIntrinsics.hpp"
#include "ui/conventional/MRVMMacroDialogRuntime.hpp"
#include "ui/modeless/MRVMMacroModelessProcedures.hpp"
#include "vm/MRVMKeymapRuntime.hpp"
#include "vm/MRVMMacroSpecRuntime.hpp"
#include "ui/modeless/MRVMModelessUiRuntime.hpp"
#include "vm/MRVMProcessRuntime.hpp"
#include "vm/MRVMProcedureCatalog.hpp"
#include "vm/MRVMRuntimeCatalog.hpp"
#include "vm/MRVMRuntimeDebugger.hpp"
#include "vm/MRVMRuntimeGlobals.hpp"
#include "vm/MRVMRuntimeInternal.hpp"
#include "vm/MRVMRuntimeKv.hpp"
#include "vm/MRVMRuntimeState.hpp"
#include "vm/MRVMValue.hpp"
#include "ui/conventional/MRVMEditor.hpp"
#include "ui/conventional/MRVMScreen.hpp"
#include "vm/MRVMSettings.hpp"
#include "vm/MRVMSystemVariables.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <glob.h>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "../ui/MREditWindow.hpp"
#include "../app/MRCommandRouter.hpp"
#include "../app/MRRuntimeScheduler.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../ui/MRMenuBar.hpp"
#include "../ui/MRStatusLine.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../dialogs/setup/MRSetupCommon.hpp"
#include "../dialogs/MRWindowList.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "../keymap/MRKeymapProfile.hpp"
#include "../ui/MRWindowSupport.hpp"

std::recursive_mutex g_vmExecutionMutex;
thread_local BackgroundEditSession *g_backgroundEditSession = nullptr;
thread_local std::shared_ptr<std::atomic_bool> g_backgroundMacroCancelFlag;
thread_local ExecutionState *g_executionState = nullptr;
thread_local MRMacroExecutionSessionId g_executionSessionId = 0;

namespace mrvm_runtime {
using Value = VirtualMachine::Value;

std::size_t searchLimitForward(const std::string &text, std::size_t start, int numLines) {
	if (numLines <= 0) return text.size();
	std::size_t pos = start;
	int remaining = numLines;
	while (pos < text.size()) {
		if (text[pos] == '\n') {
			--remaining;
			if (remaining == 0) return pos;
		}
		++pos;
	}
	return text.size();
}

std::size_t searchLimitBackward(const std::string &text, std::size_t start, int numLines) {
	if (numLines <= 0) return 0;
	std::size_t pos = std::min(start, text.size());
	int remaining = numLines;
	while (pos > 0) {
		--pos;
		if (text[pos] == '\n') {
			--remaining;
			if (remaining == 0) return pos + 1;
		}
	}
	return 0;
}

void appendUniqueString(std::vector<std::string> &values, const std::string &value) {
	if (value.empty()) return;
	if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
}

std::string getEnvironmentValue(const std::string &entryName) {
	std::string key = trimAscii(entryName);
	std::size_t pos = key.find('=');
	if (pos != std::string::npos) key = key.substr(0, pos);
	if (key.empty()) return std::string();
	std::string direct = mrvmGetenvValue(key);
	if (!direct.empty()) return direct;
	std::string up = mrvmUpperKey(key);
	if (up == "MR_PATH") return mrvmRuntimeStateString("process", "executableDir");
	if (up == "COMSPEC") return mrvmRuntimeStateString("process", "shellPath");
	if (up == "OS_VERSION") return mrvmRuntimeStateString("process", "shellVersion");
	return std::string();
}

int findFirstFileMatch(const std::string &pattern) {
	glob_t g;
	std::string expanded = mrvmProcessExpandUserPath(trimAscii(pattern));
	int rc;
	std::vector<std::string> fileMatches;

	mrvmStoreRuntimeStateStringList("fileEnumeration", "matches", fileMatches);
	mrvmStoreRuntimeStateSize("fileEnumeration", "index", 0);
	mrvmStoreRuntimeStateString("fileEnumeration", "lastFileName", std::string());

	std::memset(&g, 0, sizeof(g));
	rc = ::glob(expanded.c_str(), 0, nullptr, &g);
	if (rc == 0) {
		for (std::size_t i = 0; i < g.gl_pathc; ++i)
			fileMatches.emplace_back(g.gl_pathv[i]);
		::globfree(&g);
		if (!fileMatches.empty()) {
			mrvmStoreRuntimeStateStringList("fileEnumeration", "matches", fileMatches);
			mrvmStoreRuntimeStateString("fileEnumeration", "lastFileName", fileMatches[0]);
			return 0;
		}
	} else
		::globfree(&g);

	if (mrvmFileExistsPath(expanded)) {
		fileMatches.push_back(expanded);
		mrvmStoreRuntimeStateStringList("fileEnumeration", "matches", fileMatches);
		mrvmStoreRuntimeStateString("fileEnumeration", "lastFileName", expanded);
		return 0;
	}

	return 18;
}

int findNextFileMatch() {
	const std::vector<std::string> fileMatches = mrvmRuntimeStateStringList("fileEnumeration", "matches");
	std::size_t index = mrvmRuntimeStateSize("fileEnumeration", "index");

	if (fileMatches.empty()) return 18;
	if (index + 1 >= fileMatches.size()) return 18;
	++index;
	mrvmStoreRuntimeStateSize("fileEnumeration", "index", index);
	mrvmStoreRuntimeStateString("fileEnumeration", "lastFileName", fileMatches[index]);
	return 0;
}

} // namespace mrvm_runtime

using namespace mrvm_runtime;

bool currentExecutingMacroSpec(std::string &macroSpec) {
	return currentExecutingMacroSpecFromRuntimeStack(macroSpec);
}

MRVMRuntimeKv &mrvmRuntimeKv() noexcept {
	static MRVMRuntimeKv runtimeKv;
	return runtimeKv;
}

std::recursive_mutex &mrvmExecutionMutex() noexcept {
	return g_vmExecutionMutex;
}

bool mrvmHasActiveBackgroundEditSession() noexcept {
	return currentBackgroundEditSession() != nullptr;
}

void mrvmSetProcessContext(int argc, char **argv) {
	mrvmProcessRuntimeSetContext(argc, argv);
}

std::vector<std::string> mrvmProcessArguments() {
	return mrvmProcessRuntimeArguments();
}

VirtualMachine::Value::Value() : type(TYPE_INT), i(0), r(0.0), c(0), hashHandle(0), arrayElementType(TYPE_INT), arrayValues(), globalStorage(false) {
}

VirtualMachine::VirtualMachine() : mHashStore(std::make_unique<MRVMHashStore>()), mClosureId(), mClosureVariableNames(), mExecutionSessionId(0), mSessionVariableNames(), verboseLogging(true), logTruncated(false), delayState(), debugState(), cancelledExecution(false) {
}

VirtualMachine::~VirtualMachine() = default;

MRVMHashStore &VirtualMachine::localHashStore() {
	return *mHashStore;
}

const MRVMHashStore &VirtualMachine::localHashStore() const {
	return *mHashStore;
}

void VirtualMachine::setClosureContext(const std::string &closureId) {
	mClosureId = closureId;
	mClosureVariableNames.clear();
	mExecUiCommandRequests.clear();
}

void VirtualMachine::setExecutionSessionContext(MRMacroExecutionSessionId sessionId) {
	mExecutionSessionId = sessionId;
	mSessionVariableNames.clear();
}

const std::vector<MRMacroExecUiCommandRequest> &VirtualMachine::execUiCommandRequests() const noexcept {
	return mExecUiCommandRequests;
}

void VirtualMachine::appendLogLine(const std::string &line, bool important) {
	static const std::size_t kMaxLogLines = 256;

	if (!important && !verboseLogging) return;
	if (log.size() < kMaxLogLines) {
		log.push_back(line);
		return;
	}
	if (!logTruncated) {
		log.emplace_back("VM Notice: execution log truncated.");
		logTruncated = true;
	}
}

void VirtualMachine::push(const Value &value) {
	stack.push_back(value);
}

VirtualMachine::Value VirtualMachine::pop() {
	if (!stack.empty()) {
		Value value = stack.back();
		stack.pop_back();
		return value;
	}

	appendLogLine("VM Error: Stack underflow.", true);
	return mrvmMakeInt(0);
}

int mrvmUiCurrentWindowIndex(const void *windowKey) {
	std::vector<MREditWindow *> windows;

	if (windowKey == nullptr) return currentEditWindowIndex();
	windows = allEditWindows();
	for (std::size_t i = 0; i < windows.size(); ++i)
		if (windows[i] == windowKey) return static_cast<int>(i) + 1;
	return 0;
}

int mrvmUiWindowCount() {
	return countEditWindows();
}

int mrvmUiLinkStatus(const void *windowKey) {
	const MREditWindow *win = static_cast<const MREditWindow *>(windowKey);

	if (windowKey == nullptr) return currentLinkStatus();
	return isWindowLinked(const_cast<MREditWindow *>(win)) ? 1 : 0;
}

bool mrvmUiWindowGeometry(const void *windowKey, int &x1, int &y1, int &x2, int &y2) {
	MREditWindow *win;
	TRect bounds;

	if (windowKey == nullptr) return currentWindowGeometry(x1, y1, x2, y2);
	win = const_cast<MREditWindow *>(static_cast<const MREditWindow *>(windowKey));
	if (win == nullptr) return false;
	bounds = win->getBounds();
	x1 = bounds.a.x + 1;
	y1 = bounds.a.y + 1;
	x2 = bounds.b.x;
	y2 = bounds.b.y;
	return true;
}

int mrvmUiScreenWidth() {
	return static_cast<int>(TDisplay::getCols());
}

int mrvmUiScreenHeight() {
	return static_cast<int>(TDisplay::getRows());
}

bool mrvmUiCursorPosition(int &x, int &y) {
	return currentUiCursorPosition(x, y);
}

void mrvmUiSyncLinkedWindowsFrom(MREditWindow *window) {
	syncLinkedWindowsFrom(window);
}

struct UiRenderFacade {
	static bool renderDeferredCommand(const MRMacroDeferredUiCommand &command) {
		return mrvmUiScreenRenderDeferredCommand(command);
	}
};

bool mrvmUiRenderFacadeRenderDeferredCommand(const MRMacroDeferredUiCommand &command) {
	return UiRenderFacade::renderDeferredCommand(command);
}

bool mrvmLoadMacroFile(const std::string &spec, std::string *errorMessage) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);

	if (!loadMacroFileIntoRegistry(spec, nullptr)) {
		if (errorMessage != nullptr) {
			const char *compileError = get_last_compile_error();
			if (compileError != nullptr && *compileError != '\0') *errorMessage = compileError;
			else
				*errorMessage = "Unable to load macro file.";
		}
		return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool mrvmRunMacroSpec(const std::string &spec, std::string *errorMessage, std::vector<std::string> *logLines) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);

	if (logLines != nullptr) logLines->clear();
	if (executeRuntimeMacroSpec(spec, logLines)) {
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (errorMessage == nullptr) return false;

	switch (runtimeErrorLevel()) {
		case 5001:
			*errorMessage = "Macro specification could not be resolved.";
			break;
		case 5005:
			*errorMessage = "Macro file could not be compiled.";
			break;
		case 5006:
			*errorMessage = "Macro conflicts with a loaded or running macro.";
			break;
		case 5007:
			*errorMessage = "Macro execution stack could not be completed.";
			break;
		default:
			*errorMessage = "Macro execution failed.";
			break;
	}
	return false;
}

bool mrvmRunAssignedMacroForKey(unsigned short keyCode, unsigned short controlKeyState, std::string &executedMacroName, std::vector<std::string> *logLines) {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	TKey pressed(keyCode, controlKeyState);
	int mode = currentUiMacroMode();
	const bool rawCtrlSpace = keyCode == kbNoKey && (controlKeyState & kbCtrlShift) != 0 && (controlKeyState & (kbAltShift | kbSuperShift | kbPaste)) == 0;
	bool traceSnippetKey = false;
	auto dispatchLoadedBinding = [&]() -> bool {
		const std::vector<std::string> orderValues = macroCatalogMacroOrder();
		for (std::size_t i = orderValues.size(); i > 0; --i) {
			const std::string &macroKey = orderValues[i - 1];
			MacroRef macroRef;

			if (!readLoadedMacroByKey(macroKey, macroRef)) continue;
			if (!macroRef.hasAssignedKey) continue;
			if (!macroAllowsUiMode(macroRef, mode)) continue;
			if (!mrvmBindingKeysEqual(pressed, macroRef.assignedKey)) continue;

			mrvmLogCalculatorHotkeyState("vm-loaded-match", pressed, macroRef.displayName);
			if (traceSnippetKey) mrLogMessage(("KEYDBG vm loaded binding match macro=" + macroRef.displayName).c_str());
			executedMacroName = macroRef.displayName;
			executeLoadedMacroWithConfiguredKeymapBatch(macroKey, std::string(), logLines);
			return true;
		}
		return false;
	};

	if (rawCtrlSpace) pressed = TKey(static_cast<ushort>(' '), kbCtrlShift);
	traceSnippetKey = rawCtrlSpace || (pressed.code == static_cast<ushort>(' ') && (pressed.mods & kbCtrlShift) != 0) || (pressed.code == static_cast<ushort>('@') && (pressed.mods & kbCtrlShift) != 0);
	executedMacroName.clear();
	if (logLines != nullptr) logLines->clear();
	if (mrvmKeyReplayActive()) return false;
	if (traceSnippetKey) {
		char line[512];
		const std::size_t explicitBindingCount = mrvmRuntimeExplicitKeyBindings().size();
		std::snprintf(line, sizeof(line), "KEYDBG vm assigned-key rawCode=0x%04X rawMods=0x%04X code=0x%04X mods=0x%04X mode=%d explicit=%zu loaded=%zu", static_cast<unsigned>(keyCode), static_cast<unsigned>(controlKeyState), static_cast<unsigned>(pressed.code), static_cast<unsigned>(pressed.mods), mode, explicitBindingCount, macroCatalogLoadedMacroCount());
		mrLogMessage(line);
	}
	mrvmLogCalculatorHotkeyState("vm-enter", pressed);
	if (executeExplicitKeyBinding(pressed, mode, logLines)) {
		executedMacroName = "<bound>";
		mrvmLogCalculatorHotkeyState("vm-explicit-consumed", pressed);
		if (traceSnippetKey) mrLogMessage("KEYDBG vm assigned-key consumed by explicit binding");
		return true;
	}
	if (traceSnippetKey) mrLogMessage("KEYDBG vm assigned-key no explicit binding");
	if (dispatchLoadedBinding()) return true;
	if (traceSnippetKey) mrLogMessage("KEYDBG vm assigned-key no loaded binding");
	return false;
}
