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

VirtualMachine::InstructionFlow VirtualMachine::executeEditorProcedure(MRVMProcedure procedure, const std::string &name, const std::vector<Value> &args) {
	switch (procedure) {
		case MRVMProcedure::PutLine: {
			MRFileEditor *editor;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("PUT_LINE expects one string argument.");
			editor = currentEditor();
			if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			replaceEditorLine(editor, mrvmValueAsString(args[0]));
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::Cr: {
			MRFileEditor *editor = currentEditor();
			if (!args.empty()) throw std::runtime_error("CR expects no arguments.");
			if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			carriageReturnEditor(editor);
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::DelChar: {
			MRFileEditor *editor = currentEditor();
			if (!args.empty()) throw std::runtime_error("DEL_CHAR expects no arguments.");
			if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			deleteEditorChars(editor, 1);
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::DelChars: {
			MRFileEditor *editor = currentEditor();
			if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("DEL_CHARS expects one integer argument.");
			if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			deleteEditorChars(editor, mrvmValueAsInt(args[0]));
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::DelLine: {
			MRFileEditor *editor = currentEditor();
			if (!args.empty()) throw std::runtime_error("DEL_LINE expects no arguments.");
			if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			deleteEditorLine(editor);
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::BackSpace: {
			MRFileEditor *editor = currentEditor();
			if (!args.empty()) throw std::runtime_error("BACK_SPACE expects no arguments.");
			if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			backspaceEditor(editor);
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::WordWrapLine: {
			MRFileEditor *editor = currentEditor();
			if (!args.empty()) throw std::runtime_error("WORD_WRAP_LINE expects no arguments.");
			if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			wordWrapEditorLine(editor);
			runtimeErrorLevel() = 0;
		} break;
		case MRVMProcedure::SetRandomMark:
		case MRVMProcedure::GetRandomMark: {
			if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error((name + " expects one integer argument.").c_str());
			if (currentBackgroundEditSession() != nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			const std::string sequenceText = "<" + std::to_string(args[0].i) + ">";
			runtimeErrorLevel() = dispatchMRKeymapAction(name == "SET_RANDOM_MARK" ? "MRMAC_MARK_SET_RANDOM_ACCESS" : "MRMAC_MARK_GET_RANDOM_ACCESS", sequenceText, currentEditorCommandWindow()) ? 0 : 1001;
		} break;
		case MRVMProcedure::ExtendBlockByMotion: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("EXTEND_BLOCK_BY_MOTION expects one key sequence string argument.");
			if (currentBackgroundEditSession() != nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			runtimeErrorLevel() = dispatchMRKeymapAction("MRMAC_BLOCK_EXTEND_BY_MOTION", mrvmValueAsString(args[0]), currentEditorCommandWindow()) ? 0 : 1001;
		} break;
		case MRVMProcedure::Left:
		case MRVMProcedure::Right:
		case MRVMProcedure::Up:
		case MRVMProcedure::Down:
		case MRVMProcedure::Home:
		case MRVMProcedure::Eol:
		case MRVMProcedure::Tof:
		case MRVMProcedure::Eof:
		case MRVMProcedure::WordLeft:
		case MRVMProcedure::WordRight:
		case MRVMProcedure::FirstWord:
		case MRVMProcedure::MarkPos:
		case MRVMProcedure::GotoMark:
		case MRVMProcedure::PopMark:
		case MRVMProcedure::PageUp:
		case MRVMProcedure::PageDown:
		case MRVMProcedure::NextPageBreak:
		case MRVMProcedure::LastPageBreak:
		case MRVMProcedure::TabRight:
		case MRVMProcedure::TabLeft:
		case MRVMProcedure::Indent:
		case MRVMProcedure::Undent:
		case MRVMProcedure::BlockBegin:
		case MRVMProcedure::BlockLine:
		case MRVMProcedure::ColBlockBegin:
		case MRVMProcedure::BlockCol:
		case MRVMProcedure::StrBlockBegin:
		case MRVMProcedure::BlockEnd:
		case MRVMProcedure::BlockOff:
		case MRVMProcedure::BlockToggleVisibility:
		case MRVMProcedure::BlockStat:
		case MRVMProcedure::CopyBlock:
		case MRVMProcedure::MoveBlock:
		case MRVMProcedure::DeleteBlock:
		case MRVMProcedure::CreateWindow:
		case MRVMProcedure::DeleteWindow:
		case MRVMProcedure::EraseWindow:
		case MRVMProcedure::ModifyWindow:
		case MRVMProcedure::LinkWindow:
		case MRVMProcedure::UnlinkWindow:
		case MRVMProcedure::Zoom:
		case MRVMProcedure::Redraw:
		case MRVMProcedure::NewScreen:
		case MRVMProcedure::MoveWinToNextDesktop:
		case MRVMProcedure::MoveWinToPrevDesktop:
		case MRVMProcedure::MoveViewportRight:
		case MRVMProcedure::MoveViewportLeft:
		case MRVMProcedure::SaveWorkspace:
		case MRVMProcedure::LoadWorkspace:
		case MRVMProcedure::SaveSettings: {
			MRFileEditor *editor = currentEditor();
			bool ok = false;
			int deferredError = 0;
			if (!args.empty()) throw std::runtime_error((name + " expects no arguments.").c_str());
			if (queueDeferredUiProcedure(name, args, deferredError)) {
				runtimeErrorLevel() = deferredError;
				return InstructionFlow::SkipPostInstruction;
			}
			if (editor == nullptr && currentBackgroundEditSession() == nullptr && name != "CREATE_WINDOW" && name != "BLOCK_STAT" && name != "SAVE_SETTINGS") {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			switch (procedure) {
				case MRVMProcedure::Left:
					ok = moveEditorLeft(editor);
					break;
				case MRVMProcedure::Right:
					ok = moveEditorRight(editor);
					break;
				case MRVMProcedure::Up:
					ok = moveEditorUp(editor);
					break;
				case MRVMProcedure::Down:
					ok = moveEditorDown(editor);
					break;
				case MRVMProcedure::Home:
					ok = moveEditorHome(editor);
					break;
				case MRVMProcedure::Eol:
					ok = moveEditorEol(editor);
					break;
				case MRVMProcedure::Tof:
					ok = moveEditorTof(editor);
					break;
				case MRVMProcedure::Eof:
					ok = moveEditorEof(editor);
					break;
				case MRVMProcedure::WordLeft:
					ok = moveEditorWordLeft(editor);
					break;
				case MRVMProcedure::WordRight:
					ok = moveEditorWordRight(editor);
					break;
				case MRVMProcedure::FirstWord:
					ok = moveEditorFirstWord(editor);
					break;
				case MRVMProcedure::MarkPos:
					ok = markEditorPosition(currentEditorCommandWindow(), editor);
					break;
				case MRVMProcedure::GotoMark:
					ok = gotoEditorMark(currentEditorCommandWindow(), editor);
					break;
				case MRVMProcedure::PopMark:
					ok = popEditorMark(currentEditorCommandWindow());
					break;
				case MRVMProcedure::PageUp:
					ok = moveEditorPageUp(editor);
					break;
				case MRVMProcedure::PageDown:
					ok = moveEditorPageDown(editor);
					break;
				case MRVMProcedure::NextPageBreak:
					ok = moveEditorNextPageBreak(editor);
					break;
				case MRVMProcedure::LastPageBreak:
					ok = moveEditorLastPageBreak(editor);
					break;
				case MRVMProcedure::TabRight:
					ok = moveEditorTabRight(editor);
					break;
				case MRVMProcedure::TabLeft:
					ok = moveEditorTabLeft(editor);
					break;
				case MRVMProcedure::Indent:
					ok = indentEditor(editor);
					break;
				case MRVMProcedure::Undent:
					ok = undentEditor(editor);
					break;
				case MRVMProcedure::BlockBegin:
				case MRVMProcedure::BlockLine:
					ok = mrvmUiBlockBeginLine();
					break;
				case MRVMProcedure::ColBlockBegin:
				case MRVMProcedure::BlockCol:
					ok = mrvmUiBlockBeginColumn();
					break;
				case MRVMProcedure::StrBlockBegin:
					ok = mrvmUiBlockBeginStream();
					break;
				case MRVMProcedure::BlockEnd:
					ok = mrvmUiBlockEndMarking();
					break;
				case MRVMProcedure::BlockOff:
					ok = mrvmUiBlockTurnMarkingOff();
					break;
				case MRVMProcedure::BlockToggleVisibility:
					ok = mrvmUiBlockToggleVisibility();
					break;
				case MRVMProcedure::BlockStat:
					ok = true;
					runtimeReturnInt() = blockStatusValue(currentEditorCommandWindow());
					break;
				case MRVMProcedure::CopyBlock:
				case MRVMProcedure::MoveBlock:
					ok = true;
					break;
				case MRVMProcedure::DeleteBlock:
					ok = mrvmUiDeleteBlock();
					break;
				case MRVMProcedure::CreateWindow:
					ok = mrvmUiCreateWindow();
					break;
				case MRVMProcedure::DeleteWindow:
					ok = mrvmUiDeleteCurrentWindow();
					break;
				case MRVMProcedure::EraseWindow:
					ok = mrvmUiEraseCurrentWindow();
					break;
				case MRVMProcedure::ModifyWindow:
					ok = mrvmUiModifyCurrentWindow();
					break;
				case MRVMProcedure::LinkWindow:
					ok = mrvmUiLinkCurrentWindow();
					break;
				case MRVMProcedure::UnlinkWindow:
					ok = mrvmUiUnlinkCurrentWindow();
					break;
				case MRVMProcedure::Zoom:
					ok = mrvmUiZoomCurrentWindow();
					break;
				case MRVMProcedure::Redraw:
					ok = mrvmUiRedrawCurrentWindow();
					break;
				case MRVMProcedure::NewScreen:
					ok = mrvmUiNewScreen();
					break;
				case MRVMProcedure::MoveWinToNextDesktop:
					ok = returnWithDirectScreenMutation(moveToNextVirtualDesktop());
					break;
				case MRVMProcedure::MoveWinToPrevDesktop:
					ok = returnWithDirectScreenMutation(moveToPrevVirtualDesktop());
					break;
				case MRVMProcedure::MoveViewportRight:
					ok = returnWithDirectScreenMutation(viewportRight());
					break;
				case MRVMProcedure::MoveViewportLeft:
					ok = returnWithDirectScreenMutation(viewportLeft());
					break;
				case MRVMProcedure::SaveWorkspace:
					mrSaveWorkspace("");
					ok = returnWithDirectScreenMutation(true);
					break;
				case MRVMProcedure::LoadWorkspace:
					mrLoadWorkspace("");
					ok = returnWithDirectScreenMutation(true);
					break;
				case MRVMProcedure::SaveSettings: {
					std::string errorText;
					ok = mrvmPersistConfiguredSettingsSnapshot(&errorText);
					if (!ok) throw std::runtime_error("SAVE_SETTINGS failed: " + (errorText.empty() ? std::string("Unable to persist settings snapshot.") : errorText));
					break;
				}
				default:
					break;
			}
			runtimeErrorLevel() = ok ? 0 : 1001;
		} break;
		case MRVMProcedure::GotoLine: {
			MRFileEditor *editor = currentEditor();
			if (args.empty()) {
				if (currentBackgroundEditSession() != nullptr) {
					runtimeErrorLevel() = 1001;
					return InstructionFlow::SkipPostInstruction;
				}
				runtimeErrorLevel() = dispatchMRKeymapAction("MRMAC_CURSOR_GOTO_LINE", "", currentEditorCommandWindow()) ? 0 : 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("GOTO_LINE expects zero or one integer argument.");
			if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			runtimeErrorLevel() = gotoEditorLine(editor, mrvmValueAsInt(args[0])) ? 0 : 1010;
		} break;
		case MRVMProcedure::GotoCol: {
			MRFileEditor *editor = currentEditor();
			if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("GOTO_COL expects one integer argument.");
			if (editor == nullptr && currentBackgroundEditSession() == nullptr) {
				runtimeErrorLevel() = 1001;
				return InstructionFlow::SkipPostInstruction;
			}
			runtimeErrorLevel() = gotoEditorCol(editor, mrvmValueAsInt(args[0])) ? 0 : 1010;
		} break;
		case MRVMProcedure::SwitchWindow: {
			int deferredError = 0;
			if (queueDeferredUiProcedure(name, args, deferredError)) {
				runtimeErrorLevel() = deferredError;
				return InstructionFlow::SkipPostInstruction;
			}
			if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("SWITCH_WINDOW expects one integer argument.");
			runtimeErrorLevel() = mrvmUiSwitchWindow(mrvmValueAsInt(args[0])) ? 0 : 1001;
		} break;
		case MRVMProcedure::SizeWindow: {
			int deferredError = 0;
			if (queueDeferredUiProcedure(name, args, deferredError)) {
				runtimeErrorLevel() = deferredError;
				return InstructionFlow::SkipPostInstruction;
			}
			if (args.size() != 4 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT) throw std::runtime_error("SIZE_WINDOW expects four integer arguments.");
			runtimeErrorLevel() = mrvmUiSizeCurrentWindow(mrvmValueAsInt(args[0]), mrvmValueAsInt(args[1]), mrvmValueAsInt(args[2]), mrvmValueAsInt(args[3])) ? 0 : 1010;
		} break;
		case MRVMProcedure::WindowCopy:
		case MRVMProcedure::WindowMove: {
			if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error((name + " expects one integer argument.").c_str());
			runtimeErrorLevel() = 0;
		} break;
		default:
			throw std::runtime_error("Procedure does not belong to the editor family.");
	}

	return InstructionFlow::Completed;
}
