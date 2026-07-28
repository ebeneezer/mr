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
#include "vm/MRVMProcedureExecution.hpp"
#include "vm/MRVMDelayRuntime.hpp"
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

VirtualMachine::RuntimeProcedures::RuntimeProcedures(VirtualMachine &machine) noexcept : vm(machine) {
}

VirtualMachine::InstructionFlow VirtualMachine::RuntimeProcedures::execute(MRVMProcedure procedure, const std::string &name, const std::vector<Value> &args, bool allowAsyncDelay) {
	switch (procedure) {
		case MRVMProcedure::Delay: {
			int millis = 0;
			BackgroundEditSession *session = nullptr;
			if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("DELAY expects one integer argument.");
			millis = vm.normalizeDelayMillis(mrvmValueAsInt(args[0]));
			if (millis == 0) {
				runtimeErrorLevel() = 0;
				return InstructionFlow::SkipPostInstruction;
			}
			session = currentBackgroundEditSession();
			if (session != nullptr) {
				session->deferredUiCommands.emplace_back(mrducDelay, millis);
				runtimeErrorLevel() = 0;
				return InstructionFlow::SkipPostInstruction;
			}
			if (allowAsyncDelay) throw mrvm_execution::DelayYield(millis);
			if (!mrvm_execution::sleepDelayBlocking(millis)) {
				vm.cancelledExecution = true;
				vm.appendLogLine("VM Notice: DELAY interrupted by cancellation.", true);
				runtimeErrorLevel() = 5007;
				return InstructionFlow::FinishExecution;
			}
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::Beep: {
			if (!args.empty()) throw std::runtime_error("BEEP expects no arguments.");
			static_cast<void>(::write(STDOUT_FILENO, "\a", 1));
			static_cast<void>(::fsync(STDOUT_FILENO));
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::LoadMacroFile: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("LOAD_MACRO_FILE expects one string argument.");
			loadMacroFileIntoRegistry(mrvmValueAsString(args[0]), nullptr);
		} break;
		case MRVMProcedure::UnloadMacro: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("UNLOAD_MACRO expects one string argument.");
			unloadMacroFromRegistry(mrvmValueAsString(args[0]));
		} break;
		case MRVMProcedure::ChangeDir: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("CHANGE_DIR expects one string argument.");
			if (mrvmChangeDirectoryPath(mrvmValueAsString(args[0]))) runtimeErrorLevel() = 0;
			else
				runtimeErrorLevel() = errno != 0 ? errno : 1;
		} break;
		case MRVMProcedure::DelFile: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("DEL_FILE expects one string argument.");
			if (mrvmDeleteFilePath(mrvmValueAsString(args[0]))) runtimeErrorLevel() = 0;
			else
				runtimeErrorLevel() = errno != 0 ? errno : 1;
		} break;
		case MRVMProcedure::SetFileAttr: {
			struct stat st;
			mode_t modeBits;
			std::string path;
			if (args.size() != 2 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SET_FILE_ATTR expects (string, int).");
			path = mrvmProcessExpandUserPath(mrvmValueAsString(args[0]));
			if (::stat(path.c_str(), &st) != 0) {
				runtimeErrorLevel() = errno != 0 ? errno : 1;
				return InstructionFlow::SkipPostInstruction;
			}
			modeBits = st.st_mode;
			if ((mrvmValueAsInt(args[1]) & 0x01) != 0) modeBits &= static_cast<mode_t>(~(S_IWUSR | S_IWGRP | S_IWOTH));
			else
				modeBits |= static_cast<mode_t>(S_IWUSR);
			runtimeErrorLevel() = ::chmod(path.c_str(), modeBits) == 0 ? 0 : (errno != 0 ? errno : 1);
		} break;
		case MRVMProcedure::ShellToOs: {
			int exitCode = 0;
			if (args.size() != 2 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SHELL_TO_OS expects (string, int).");
			if (currentBackgroundEditSession() != nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			(void)mrvmUiNewScreen();
			exitCode = mrvmRunShellCommand(mrvmValueAsString(args[0]), configuredShellExecutablePath());
			(void)mrvmUiNewScreen();
			runtimeErrorLevel() = exitCode;
		} break;
		case MRVMProcedure::Fork: {
			std::vector<std::string> forkArguments;
			if (args.empty()) throw std::runtime_error("FORK expects at least one string argument.");
			forkArguments.reserve(args.size());
			for (const Value &arg : args) {
				if (!mrvmIsStringLike(arg)) throw std::runtime_error("FORK expects string arguments.");
				forkArguments.push_back(mrvmValueAsString(arg));
			}
			runtimeErrorLevel() = mrvmForkProcess(forkArguments, runtimeGlobalIntValue("MR_BUILD_SOURCE_BUFFER_ID"), runtimeGlobalStringValue("MR_BUILD_SOURCE_PATH"), runtimeGlobalStringValue("MR_BUILD_PDF_PATH"));
		} break;
		case MRVMProcedure::WriteSod: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("WRITE_SOD expects one string argument.");
			mrLogMessage(mrvmValueAsString(args[0]));
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::SaveOsScreen: {
			if (!args.empty()) throw std::runtime_error("SAVE_OS_SCREEN expects no arguments.");
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::RestOsScreen: {
			if (!args.empty()) throw std::runtime_error("REST_OS_SCREEN expects no arguments.");
			(void)mrvmUiNewScreen();
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::Quit: {
			int returnCode = 0;
			if (args.size() > 1 || (args.size() == 1 && args[0].type != TYPE_INT)) throw std::runtime_error("QUIT expects zero or one integer argument.");
			if (currentBackgroundEditSession() != nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			if (!args.empty()) returnCode = mrvmValueAsInt(args[0]);
			runtimeErrorLevel() = returnCode;
			(void)dispatchApplicationCommandEvent(cmQuit);
		} break;
		case MRVMProcedure::LoadFile: {
			MREditWindow *win;
			std::string path;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("LOAD_FILE expects one string argument.");
			path = mrvmProcessExpandUserPath(mrvmValueAsString(args[0]));
			win = activeMacroEditWindow();
			if (win == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			if (!mrvmFileExistsPath(path)) {
				runtimeErrorLevel() = 3002;
				return InstructionFlow::SkipPostInstruction;
			}
			if (!win->loadFromFile(path.c_str())) {
				runtimeErrorLevel() = 3002;
				return InstructionFlow::SkipPostInstruction;
			}
			g_runtimeEnv.lastFileName = win->currentFileName();
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::LoadBlock: {
			MREditWindow *win = currentEditorCommandWindow();
			std::string path;
			if (args.empty()) {
				if (currentBackgroundEditSession() != nullptr) {
					runtimeErrorLevel() = 1001;
					return InstructionFlow::SkipPostInstruction;
				}
				runtimeErrorLevel() = dispatchMRKeymapAction("MR_LOAD_BLOCK_FROM_FILE", "", currentEditorCommandWindow()) ? 0 : 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("LOAD_BLOCK expects zero or one string argument.");
			if (win == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			path = mrvmProcessExpandUserPath(mrvmValueAsString(args[0]));
			if (!mrvmFileExistsPath(path) || !mrvmEditorLoadBlockFromFile(win, path)) {
				runtimeErrorLevel() = 3002;
				return InstructionFlow::SkipPostInstruction;
			}
			g_runtimeEnv.lastFileName = path;
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::SaveFile: {
			MREditWindow *win = activeMacroEditWindow();
			if (!args.empty()) throw std::runtime_error("SAVE_FILE expects no arguments.");
			if (win == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			if (!win->saveCurrentFile()) {
				runtimeErrorLevel() = 2002;
				return InstructionFlow::SkipPostInstruction;
			}
			g_runtimeEnv.lastFileName = win->currentFileName();
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::SaveBlock: {
			MREditWindow *win = currentEditorCommandWindow();
			MRFileEditor *editor = currentEditor();
			std::string path;
			if (args.empty()) {
				if (currentBackgroundEditSession() != nullptr) {
					runtimeErrorLevel() = 1001;
					return InstructionFlow::SkipPostInstruction;
				}
				runtimeErrorLevel() = dispatchMRKeymapAction("MR_SAVE_BLOCK_TO_FILE", "", currentEditorCommandWindow()) ? 0 : 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("SAVE_BLOCK expects zero or one string argument.");
			if (win == nullptr || editor == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			path = mrvmProcessExpandUserPath(mrvmValueAsString(args[0]));
			if (!mrvmEditorSaveCurrentBlockToFile(win, editor, path)) {
				runtimeErrorLevel() = errno != 0 ? errno : 1010;
				return InstructionFlow::SkipPostInstruction;
			}
			g_runtimeEnv.lastFileName = path;
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::SetIndentLevel: {
			if (!args.empty()) throw std::runtime_error("SET_INDENT_LEVEL expects no arguments.");
			runtimeErrorLevel() = setCurrentEditorIndentLevel(currentEditorColumn(currentEditor())) ? 0 : 1001;
		} break;
		case MRVMProcedure::Replace: {
			MRFileEditor *editor;
			bool replaced;
			BackgroundEditSession *session;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("REPLACE expects one string argument.");
			editor = currentEditor();
			session = currentBackgroundEditSession();
			if (editor == nullptr && session == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			if (editor != nullptr) replaced = replaceLastSearch(editor, mrvmValueAsString(args[0]));
			else
				replaced = replaceLastSearchBackground(mrvmValueAsString(args[0]));
			runtimeErrorLevel() = replaced ? 0 : 1010;
		} break;
		case MRVMProcedure::Text: {
			MRFileEditor *editor;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("TEXT expects one string argument.");
			editor = currentEditor();
			if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			insertEditorText(editor, mrvmValueAsString(args[0]));
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::SetClipboardText: {
			std::string text;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("SET_CLIPBOARD_TEXT expects one string argument.");
			text = mrvmValueAsString(args[0]);
			TClipboard::setText(TStringView(text.data(), text.size()));
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::KeyIn: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("KEY_IN expects one string argument.");
			if (!mrvmReplayKeyInputSequence(mrvmValueAsString(args[0]))) {
				runtimeErrorLevel() = currentBackgroundEditSession() != nullptr ? 1010 : 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::ReadKey: {
			int key1 = 0;
			int key2 = 0;
			if (!args.empty()) throw std::runtime_error("READ_KEY expects no arguments.");
			if (!readMacroKeyPair(true, key1, key2)) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::PushKey: {
			if (args.size() != 2 || args[0].type != TYPE_INT || args[1].type != TYPE_INT) throw std::runtime_error("PUSH_KEY expects two integer arguments.");
			runtimeErrorLevel() = pushQueuedKeyPair(mrvmValueAsInt(args[0]), mrvmValueAsInt(args[1])) ? 0 : 1010;
		} break;
		case MRVMProcedure::PassKey: {
			if (args.size() != 2 || args[0].type != TYPE_INT || args[1].type != TYPE_INT) throw std::runtime_error("PASS_KEY expects two integer arguments.");
			runtimeErrorLevel() = mrvmPassMacroKeyPairToUi(mrvmValueAsInt(args[0]), mrvmValueAsInt(args[1])) ? 0 : 1010;
		} break;
		case MRVMProcedure::PushLabels: {
			if (!args.empty()) throw std::runtime_error("PUSH_LABELS expects no arguments.");
			g_runtimeEnv.functionLabelStack.emplace_back();
			applyFunctionLabelState();
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::PopLabels: {
			if (!args.empty()) throw std::runtime_error("POP_LABELS expects no arguments.");
			if (g_runtimeEnv.functionLabelStack.size() > 1) g_runtimeEnv.functionLabelStack.pop_back();
			applyFunctionLabelState();
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::FLabel: {
			int keyNumber;
			int mode;
			MacroFunctionLabelFrame &frame = currentFunctionLabelFrame();
			if (args.size() != 3 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("FLABEL expects (string, int, int).");
			keyNumber = mrvmValueAsInt(args[1]);
			mode = mrvmValueAsInt(args[2]);
			if (keyNumber <= 0 || keyNumber >= 49) {
				runtimeErrorLevel() = 1010;
				return InstructionFlow::SkipPostInstruction;
			}
			if (mode == 255) mode = currentUiMacroMode();
			if (mode == MACRO_MODE_DOS_SHELL) frame.shellLabels[static_cast<std::size_t>(keyNumber)] = mrvmValueAsString(args[0]);
			else
				frame.editLabels[static_cast<std::size_t>(keyNumber)] = mrvmValueAsString(args[0]);
			applyFunctionLabelState();
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::MacroToKey: {
			TKey key;
			int mode = MACRO_MODE_EDIT;
			MRVMExplicitKeyBinding binding;
			std::string refreshError;
			if (args.size() != 3 || !mrvmIsStringLike(args[1]) || args[2].type != TYPE_INT) throw std::runtime_error("MACRO_TO_KEY expects (key, string, int).");
			if (currentBackgroundEditSession() != nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			if (!mrvmParseBindingKeyValue(args[0], key) || !mrvmParseBindingModeValue(mrvmValueAsInt(args[2]), mode)) {
				runtimeErrorLevel() = 1010;
				return InstructionFlow::SkipPostInstruction;
			}
			mrvmRemoveExplicitBindingsForKey(g_runtimeEnv.explicitKeyBindings, key, mode);
			binding.key = key;
			binding.mode = mode;
			binding.kind = MRVMExplicitBindingKind::MacroSpec;
			binding.macroSpec = mrvmValueAsString(args[1]);
			g_runtimeEnv.explicitKeyBindings.push_back(binding);
			if (!projectRuntimeMenuKeyLabelsFromExplicitBindings(&refreshError)) throw std::runtime_error("MACRO_TO_KEY could not refresh runtime menu labels: " + (refreshError.empty() ? std::string("unknown error.") : refreshError));
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::CmdToKey: {
			TKey key;
			int mode = MACRO_MODE_EDIT;
			MRVMExplicitKeyBinding binding;
			std::string refreshError;
			if (args.size() != 3 || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("CMD_TO_KEY expects (key, int, int).");
			if (currentBackgroundEditSession() != nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			if (!mrvmParseBindingKeyValue(args[0], key) || !mrvmParseBindingModeValue(mrvmValueAsInt(args[2]), mode)) {
				runtimeErrorLevel() = 1010;
				return InstructionFlow::SkipPostInstruction;
			}
			mrvmRemoveExplicitBindingsForKey(g_runtimeEnv.explicitKeyBindings, key, mode);
			binding.key = key;
			binding.mode = mode;
			binding.kind = MRVMExplicitBindingKind::Command;
			binding.commandId = mrvmValueAsInt(args[1]);
			g_runtimeEnv.explicitKeyBindings.push_back(binding);
			if (!projectRuntimeMenuKeyLabelsFromExplicitBindings(&refreshError)) throw std::runtime_error("CMD_TO_KEY could not refresh runtime menu labels: " + (refreshError.empty() ? std::string("unknown error.") : refreshError));
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::UnassignKey: {
			TKey key;
			int mode = MACRO_MODE_EDIT;
			std::string refreshError;
			if (args.size() != 2 || args[1].type != TYPE_INT) throw std::runtime_error("UNASSIGN_KEY expects (key, int).");
			if (currentBackgroundEditSession() != nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			if (!mrvmParseBindingKeyValue(args[0], key) || !mrvmParseBindingModeValue(mrvmValueAsInt(args[1]), mode)) {
				runtimeErrorLevel() = 1010;
				return InstructionFlow::SkipPostInstruction;
			}
			mrvmRemoveExplicitBindingsForKey(g_runtimeEnv.explicitKeyBindings, key, mode);
			clearRegisteredBindingsForKey(&key, mode, mode == MACRO_MODE_ALL);
			if (!projectRuntimeMenuKeyLabelsFromExplicitBindings(&refreshError)) throw std::runtime_error("UNASSIGN_KEY could not refresh runtime menu labels: " + (refreshError.empty() ? std::string("unknown error.") : refreshError));
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::UnassignAllKeys: {
			std::string refreshError;
			if (!args.empty()) throw std::runtime_error("UNASSIGN_ALL_KEYS expects no arguments.");
			if (currentBackgroundEditSession() != nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			g_runtimeEnv.explicitKeyBindings.clear();
			clearRegisteredBindingsForKey(nullptr, MACRO_MODE_ALL, true);
			if (!projectRuntimeMenuKeyLabelsFromExplicitBindings(&refreshError)) throw std::runtime_error("UNASSIGN_ALL_KEYS could not refresh runtime menu labels: " + (refreshError.empty() ? std::string("unknown error.") : refreshError));
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::KeyRecord: {
			if (!args.empty()) throw std::runtime_error("KEY_RECORD expects no arguments.");
			if (currentBackgroundEditSession() != nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			runtimeErrorLevel() = dispatchApplicationCommandEvent(cmMrMacroToggleRecording) ? 0 : 1001;
		} break;
		case MRVMProcedure::PlayKeyMacro: {
			TKey key;
			const char *text = nullptr;
			std::size_t textLength = 0;
			char textByte = '\0';
			int mode = currentUiMacroMode();
			if ((args.size() != 2 && args.size() != 3) || args[0].type != TYPE_INT || args[1].type != TYPE_INT || (args.size() == 3 && args[2].type != TYPE_INT)) throw std::runtime_error("PLAY_KEY_MACRO expects (int, int[, int]).");
			if (currentBackgroundEditSession() != nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			if (args.size() == 3 && !mrvmParseBindingModeValue(mrvmValueAsInt(args[2]), mode)) {
				runtimeErrorLevel() = 1010;
				return InstructionFlow::SkipPostInstruction;
			}
			if (!mrvmKeyPairToTKey(mrvmValueAsInt(args[0]), mrvmValueAsInt(args[1]), key, text, textLength, textByte)) {
				runtimeErrorLevel() = 1010;
				return InstructionFlow::SkipPostInstruction;
			}
			if (executeExplicitKeyBinding(key, mode, &vm.log)) {
				runtimeErrorLevel() = 0;
				return InstructionFlow::SkipPostInstruction;
			}
			runtimeErrorLevel() = 1001;
		} break;
		default:
			throw std::runtime_error("Procedure does not belong to the runtime family.");
	}

	return InstructionFlow::Completed;
}
