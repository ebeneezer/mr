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
#include "vm/MRVMExecutionInternal.hpp"
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

using namespace mrvm_runtime;

VirtualMachine::InstructionFlow VirtualMachine::executeMacroProcedure(ExecutionFrame &frame, MRVMProcedure procedure, const std::string &name, const std::vector<Value> &args, std::size_t instructionOffset) {
	const unsigned char *bytecode = frame.bytecode;
	const std::size_t length = frame.length;
	std::size_t &ip = frame.ip;
	std::vector<std::size_t> &call_stack = frame.callStack;
	ExecutionState &state = frame.state;
	const std::string &savedParameterString = frame.savedParameterString;
	const std::string &activeMacroName = frame.activeMacroName;
	const bool activeFirstRun = frame.activeFirstRun;

	switch (procedure) {
		case MRVMProcedure::RunMacro: {
			std::string spec;
			std::string filePart;
			std::string macroPart;
			std::string paramPart;
			std::string targetFileKey;
			std::string macroKey;
			MacroRef macroRef;
			bool backgroundStaged = currentBackgroundEditSession() != nullptr;

			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("RUN_MACRO expects one string argument.");

			spec = mrvmValueAsString(args[0]);
			if (!mrvmParseRunMacroSpec(spec, filePart, macroPart, paramPart)) {
				runtimeErrorLevel() = 5001;
				return InstructionFlow::SkipPostInstruction;
			}

			macroKey = mrvmUpperKey(macroPart);
			if (!filePart.empty()) targetFileKey = resolveLoadedFileKeyForSpec(filePart);
			if (!filePart.empty() && targetFileKey.empty()) targetFileKey = mrvmMakeMacroFileKey(filePart);

			if (!readLoadedMacroByKey(macroKey, macroRef) || (!targetFileKey.empty() && macroRef.fileKey != targetFileKey)) {
				if (backgroundStaged) {
					runtimeErrorLevel() = 5001;
					return InstructionFlow::SkipPostInstruction;
				}
				if (!filePart.empty()) {
					if (!loadMacroFileIntoRegistry(filePart, &targetFileKey)) return InstructionFlow::SkipPostInstruction;
				} else {
					if (!loadMacroFileIntoRegistry(macroPart, &targetFileKey)) return InstructionFlow::SkipPostInstruction;
				}
				static_cast<void>(readLoadedMacroByKey(macroKey, macroRef));
			}

			if (macroRef.displayName.empty() || (!targetFileKey.empty() && macroRef.fileKey != targetFileKey)) {
				runtimeErrorLevel() = 5001;
				return InstructionFlow::SkipPostInstruction;
			}
			if (mDebugRunActive) {
				MacroRef childRef;
				LoadedMacroFile childFile;
				std::vector<std::size_t> childBreakpointOffsets;
				std::string childError;
				bool childFirstRun = false;
				std::unique_ptr<VirtualMachine> childVm;
				MRMacroDebugRunResult childResult;

				if (!prepareDebugMacroByKey(macroKey, mDebugStepMode == mrdStepInto, childRef, childFile, childBreakpointOffsets, childFirstRun, childError)) throw std::runtime_error(childError);
				childVm = std::make_unique<VirtualMachine>();
				childVm->setExecutionSessionContext(currentExecutionSessionId());
				if (childRef.closureUnit) childVm->setClosureContext(childRef.closureId);
				childResult = childVm->executeDebugAt(childFile.bytecode.data(), childFile.bytecode.size(), childRef.entryOffset, paramPart, childRef.displayName, childBreakpointOffsets, childFirstRun, macroKey, childFile.resolvedPath);
				if (childResult.paused) {
					mDebugChildFrame = std::make_unique<MRMacroDebugChildFrame>();
					mDebugChildFrame->vm = std::move(childVm);
					mDebugChildFrame->result = childResult;
					mDebugChildFrame->macroKey = macroKey;
					mDebugChildFrame->fileKey = childRef.fileKey;
					mDebugChildFrame->parentInstructionOffset = instructionOffset;
					mDebugChildFrame->unloadAfterCompletion = childRef.dumpAttr;
					mDebugChildFrame->evictTransientAfterCompletion = childRef.transientAttr;
					mDebugStopped = true;
					mDebugStopReason = childResult.stopReason;
					mDebugStopOffset = ip;
					mDebugStackDepth = call_stack.size();
					mDebugPaused = true;
					mDebugBytecode.assign(bytecode, bytecode + length);
					mDebugLength = length;
					mDebugIp = ip;
					mDebugCallStack = call_stack;
					mDebugReturnInt = state.returnInt;
					mDebugReturnStr = state.returnStr;
					mDebugErrorLevel = state.errorLevel;
					mDebugSavedParameterString = savedParameterString;
					mDebugMacroName = activeMacroName;
					mDebugFirstRun = activeFirstRun;
					return InstructionFlow::FinishExecution;
				}
				log.insert(log.end(), childVm->log.begin(), childVm->log.end());
				if (childRef.dumpAttr) unloadMacroFromRegistry(macroKey);
				else if (childRef.transientAttr)
					evictTransientFileImage(childRef.fileKey);
				runtimeErrorLevel() = 0;
			} else if (!executeLoadedMacroWithConfiguredKeymapBatch(macroKey, paramPart, &log))
				return InstructionFlow::SkipPostInstruction;
		} break;
		case MRVMProcedure::ExpandTabs:
		case MRVMProcedure::TabsToSpaces:
		case MRVMProcedure::Unknown:
		default: {
			if (name.size() >= 4 && name.compare(0, 4, "MMP_") == 0) {
				int modelessReturnValue = 0;
				std::string modelessError;

				if (!mrvmDispatchMacroModelessProcedure(g_runtimeEnv.runtimeKv, name, args, modelessReturnValue, modelessError)) throw std::runtime_error("Unknown MMP procedure: " + name);
				runtimeReturnInt() = modelessReturnValue;
				runtimeErrorLevel() = modelessReturnValue != 0 ? 0 : 1001;
				if (modelessReturnValue == 0 && !modelessError.empty()) throw std::runtime_error(modelessError);
			} else if (const char *actionId = mrvmKeymapActionIdForMacroCommand(name)) {
				if (!args.empty()) throw std::runtime_error((name + " expects no arguments.").c_str());
				if (currentBackgroundEditSession() != nullptr) {
					runtimeErrorLevel() = 1001;
					return InstructionFlow::SkipPostInstruction;
				}
				runtimeErrorLevel() = dispatchMRKeymapAction(actionId, "", currentEditorCommandWindow()) ? 0 : 1001;
			} else {
				throw std::runtime_error("Unknown procedure: " + name);
			}
			break;
		}
	}

	return InstructionFlow::Completed;
}
