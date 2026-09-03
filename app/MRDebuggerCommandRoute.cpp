#define Uses_TEvent
#define Uses_TKeys
#include <tvision/tv.h>

#include "MRDebuggerCommandRoute.hpp"

#include "MRCommands.hpp"
#include "commands/MRExternalCommand.hpp"
#include "commands/MRWindowCommands.hpp"
#include "router/MRCommandRouterSearchMultiFileSession.hpp"
#include "utils/MRStringUtils.hpp"

#include "../config/settings/MRSettingsRuntime.hpp"
#include "../ui/MRBentoBox/MRBentoBox.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRWindowSupport.hpp"

#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace {

struct DebuggerCommandDescriptor {
	ushort command;
	ushort keyCode;
	ushort modifiers;
};

static const DebuggerCommandDescriptor kDebuggerCommands[] = {
	{cmMrDebuggerEvaluate, kbF4, 0},
	{cmMrDebuggerContinue, kbF5, 0},
	{cmMrDebuggerRunHere, kbF6, 0},
	{cmMrDebuggerAddWatch, kbF7, 0},
	{cmMrDebuggerEraseWatch, kbF7, kbShift},
	{cmMrDebuggerStop, kbF8, 0},
	{cmMrDebuggerToggleBreakpoint, kbF9, 0},
	{cmMrDebuggerStep, kbF10, 0},
	{cmMrDebuggerStepOver, kbF11, 0},
	{cmMrDebuggerStepOut, kbShiftF11, 0},
	{cmMrMacroDebuggerEvaluate, kbF4, 0},
	{cmMrMacroDebuggerContinue, kbF5, 0},
	{cmMrMacroDebuggerRunHere, kbF6, 0},
	{cmMrMacroDebuggerToggleBreakpointEnabled, kbF9, kbShift},
	{cmMrMacroDebuggerToggleAllBreakpoints, kbF9, static_cast<ushort>(kbAltShift | kbShift)},
	{cmMrMacroDebuggerClearAllBreakpoints, kbF9, static_cast<ushort>(kbCtrlShift | kbShift)},
	{cmMrMacroDebuggerAddWatch, kbF7, 0},
	{cmMrMacroDebuggerEraseWatch, kbF7, kbShift},
	{cmMrMacroDebuggerStop, kbF8, 0},
	{cmMrMacroDebuggerStep, kbF10, 0},
	{cmMrMacroDebuggerStepOver, kbF11, 0},
	{cmMrMacroDebuggerStepOut, kbShiftF11, 0},
};

bool gdbToolchainSupported(const std::string &toolchain) {
	return toolchain == "GCC" || toolchain == "CLANG" || toolchain == "FREEBASIC" || toolchain == "GCC_C" || toolchain == "CLANG_C" || toolchain == "RUST" || toolchain == "GO" || toolchain == "FPC";
}

} // namespace

bool mrHandleDebuggerFunctionKey(MRBentoBox *bentoBox, TEvent &event) {
	return bentoBox != nullptr && bentoBox->handleDebuggerFunctionKey(event);
}

bool mrHandleDebuggerCommand(MRBentoBox *bentoBox, TEvent &event) {
	if (event.what != evCommand) return false;
	if (event.message.command == cmMrDebuggerStart) {
		static_cast<void>(mrStartGdbDebuggerForCurrentFile());
		event.what = evNothing;
		return true;
	}
	if (bentoBox == nullptr) return false;
	if (event.message.command == cmMrDebuggerClearProgramTerminal) {
		if (!bentoBox->clearGdbProgramTerminal()) return false;
		event.what = evNothing;
		return true;
	}
	for (const DebuggerCommandDescriptor &descriptor : kDebuggerCommands) {
		if (descriptor.command != event.message.command) continue;
		TEvent keyEvent;
		std::memset(&keyEvent, 0, sizeof(keyEvent));
		keyEvent.what = evKeyDown;
		keyEvent.keyDown.keyCode = descriptor.keyCode;
		keyEvent.keyDown.controlKeyState = descriptor.modifiers;
		if (!bentoBox->handleDebuggerFunctionKey(keyEvent)) return false;
		event.what = evNothing;
		return true;
	}
	return false;
}

bool mrStartGdbDebuggerForCurrentFile() {
	MREditWindow *sourceWindow = currentEditWindow();
	MRCompilerProfile profile;
	MRBuildHookContext buildContext;
	std::string matchedProfileName;
	std::string errorMessage;
	std::string sourcePath;
	MRBentoBox *bentoBox;

	if (sourceWindow == nullptr) {
		postDialogWarning("Debug current file requires an editor window.");
		return false;
	}
	bentoBox = dynamic_cast<MRBentoBox *>(sourceWindow);
	if (bentoBox == nullptr) bentoBox = dynamic_cast<MRBentoBox *>(sourceWindow->owner);
	if (bentoBox != nullptr && bentoBox->gdbDebuggerActive()) {
		postDialogWarning("A GDB session is already active for this window.");
		return false;
	}
	sourcePath = sourceWindow->currentFileName();
	if (sourcePath.empty()) {
		postDialogWarning("Debug current file requires a named source file.");
		return false;
	}
	if (sourceWindow->isFileChanged() && !sourceWindow->saveCurrentFile()) {
		postDialogWarning("Unable to save current file before debugging.");
		return false;
	}
	if (!effectiveCompilerProfileForPath(sourcePath, profile, &matchedProfileName, &errorMessage)) {
		postDialogWarning(errorMessage.empty() ? "No compiler profile for current file." : errorMessage);
		return false;
	}
	if (!gdbToolchainSupported(profile.toolchain)) {
		postDialogWarning("The active compiler profile is not GDB-compatible.");
		return false;
	}
	if (upperAscii(profile.id + " " + profile.name).find("DEBUG") == std::string::npos) {
		postDialogWarning("Select a Debug compiler profile before starting GDB.");
		return false;
	}
	buildContext = buildCompilerProfileHookContext(profile, sourcePath, sourceWindow->bufferId());
	std::error_code fileError;
	if (buildContext.outputPath.empty() || !std::filesystem::is_regular_file(buildContext.outputPath, fileError) || ::access(buildContext.outputPath.c_str(), X_OK) != 0) {
		postDialogWarning("Build the current file with its Debug profile before starting GDB.");
		return false;
	}
	const std::filesystem::file_time_type sourceTime = std::filesystem::last_write_time(sourcePath, fileError);
	if (!fileError) {
		const std::filesystem::file_time_type outputTime = std::filesystem::last_write_time(buildContext.outputPath, fileError);
		if (!fileError && sourceTime > outputTime) {
			postDialogWarning("The debug artifact is older than the source; rebuild it first.");
			return false;
		}
	}
	if (bentoBox == nullptr) bentoBox = convertEditWindowToBentoBox(sourceWindow);
	if (bentoBox == nullptr) {
		postDialogWarning("Unable to create the GDB Bento workspace.");
		return false;
	}
	if (!bentoBox->startGdbDebugger(buildContext.outputPath, sourcePath, errorMessage)) {
		postDialogWarning(errorMessage.empty() ? "Unable to start GDB." : errorMessage);
		return false;
	}
	static_cast<void>(mrActivateEditWindow(bentoBox));
	return true;
}
