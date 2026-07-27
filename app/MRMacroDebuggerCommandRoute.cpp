#define Uses_TEvent
#define Uses_TKeys
#include <tvision/tv.h>

#include "MRMacroDebuggerCommandRoute.hpp"

#include "MRCommands.hpp"
#include "commands/MRWindowCommands.hpp"

#include "../ui/MRBentoBox/MRBentoBox.hpp"
#include "../ui/MRWindowSupport.hpp"

#include <cstring>

namespace {

struct MacroDebuggerCommandDescriptor {
	ushort command;
	ushort keyCode;
	ushort controlKeyState;
	const char *logMessage;
};

static const MacroDebuggerCommandDescriptor kMacroDebuggerCommands[] = {
	{cmMrOtherBuildCurrentFile, kbF9, 0, "MACRODBG key stage=app-build-command"},
	{cmMrMacroDebuggerEvaluate, kbF4, 0, nullptr},
	{cmMrMacroDebuggerContinue, kbF5, 0, "MACRODBG key stage=app-continue-command"},
	{cmMrMacroDebuggerStep, kbF10, 0, nullptr},
	{cmMrMacroDebuggerStepOver, kbF11, 0, nullptr},
	{cmMrMacroDebuggerStepOut, kbShiftF11, 0, nullptr},
	{cmMrMacroDebuggerStop, kbF8, 0, "MACRODBG key stage=app-stop-command"},
	{cmMrMacroDebuggerAddWatch, kbF7, 0, nullptr},
	{cmMrMacroDebuggerEraseWatch, kbF7, kbShift, nullptr},
	{cmMrMacroDebuggerRunHere, kbF6, 0, nullptr},
};

const MacroDebuggerCommandDescriptor *macroDebuggerCommandDescriptor(ushort command) noexcept {
	for (const MacroDebuggerCommandDescriptor &descriptor : kMacroDebuggerCommands)
		if (descriptor.command == command) return &descriptor;
	return nullptr;
}

} // namespace

bool mrHandleMacroDebuggerFunctionKey(MRBentoBox *bentoBox, TEvent &event) {
	if (event.what != evKeyDown || bentoBox == nullptr) return false;
	const bool consumed = bentoBox->handleMacroDebuggerFunctionKey(event);

	if (consumed) mrLogMessage("MACRODBG key stage=app-macro-debugger-route");
	return consumed;
}

bool mrHandleMacroDebuggerCommand(MRBentoBox *bentoBox, TEvent &event) {
	const MacroDebuggerCommandDescriptor *descriptor;
	TEvent keyEvent;
	bool consumed;

	if (event.what != evCommand) return false;
	descriptor = macroDebuggerCommandDescriptor(event.message.command);
	if (descriptor == nullptr || bentoBox == nullptr) return false;
	std::memset(&keyEvent, 0, sizeof(keyEvent));
	keyEvent.what = evKeyDown;
	keyEvent.keyDown.keyCode = descriptor->keyCode;
	keyEvent.keyDown.controlKeyState = descriptor->controlKeyState;
	consumed = bentoBox->handleMacroDebuggerFunctionKey(keyEvent);
	if (!consumed) return false;
	if (descriptor->logMessage != nullptr) mrLogMessage(descriptor->logMessage);
	event.what = evNothing;
	return true;
}

bool mrMacroDebuggerObservesSourcePath(const std::string &sourcePath) {
	if (sourcePath.empty()) return false;
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);

		if (bentoBox != nullptr && bentoBox->macroDebuggerObservesSourcePath(sourcePath)) return true;
	}
	return false;
}

MRBentoBox *mrMacroDebuggerForSourcePath(const std::string &sourcePath, const MRBentoBox *excluded) {
	if (sourcePath.empty()) return nullptr;
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);

		if (bentoBox != nullptr && bentoBox != excluded && bentoBox->macroDebuggerTargetsSourcePath(sourcePath)) return bentoBox;
	}
	return nullptr;
}

bool mrAttachScheduledMacroDebuggerSession(MRMacroExecutionSessionId sessionId, const MRMacroDebugRunResult &debugResult) {
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);

		if (bentoBox != nullptr && bentoBox->acceptScheduledMacroDebuggerSession(sessionId, debugResult)) return true;
	}
	return false;
}

bool mrApplyMacroDebuggerWorkerResult(MRMacroExecutionSessionId sessionId, std::uint64_t taskId, const MRMacroDebugRunResult &debugResult, const std::string &errorMessage) {
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);

		if (bentoBox != nullptr && bentoBox->acceptMacroDebuggerWorkerResult(sessionId, taskId, debugResult, errorMessage)) return true;
	}
	return false;
}
