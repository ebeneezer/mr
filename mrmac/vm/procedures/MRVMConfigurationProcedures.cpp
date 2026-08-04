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

VirtualMachine::ConfigurationProcedures::ConfigurationProcedures(VirtualMachine &machine) noexcept : vm(machine) {
}

VirtualMachine::InstructionFlow VirtualMachine::ConfigurationProcedures::execute(MRVMProcedure procedure, const std::string &name, const std::vector<Value> &args) {
	switch (procedure) {
		case MRVMProcedure::ExecAssign: {
			MRMacroExecUiCommandRequest request;
			bool accepted = false;

			if (args.size() != 3 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || !mrvmIsStringLike(args[2])) throw std::runtime_error("EXEC expects (target, command).");
			request.closureId = vm.mClosureId;
			request.target = mrvmValueAsString(args[0]);
			request.command = mrvmValueAsString(args[1]);
			request.lvalue = mrvmValueAsString(args[2]);
			if (currentBackgroundEditSession() != nullptr || g_backgroundMacroCancelFlag != nullptr) {
				vm.mExecUiCommandRequests.push_back(request);
				setRuntimeErrorLevel(0);
			} else {
				accepted = mrvmApplyExecUiCommandRequest(request);
				vm.variables[request.lvalue] = mrvmMakeInt(accepted ? 1 : 0);
				if (!vm.mClosureId.empty() && vm.mClosureVariableNames.find(request.lvalue) != vm.mClosureVariableNames.end()) mrvmExecSessionsWriteClosureVariable(mrvmRuntimeKv(), vm.mClosureId, request.lvalue, vm.variables[request.lvalue], *vm.mHashStore);
				else if (currentExecutionSessionId() != 0 && vm.mSessionVariableNames.find(request.lvalue) != vm.mSessionVariableNames.end())
					mrvmExecSessionsWriteSessionVariable(mrvmRuntimeKv(), currentExecutionSessionId(), request.lvalue, vm.variables[request.lvalue], *vm.mHashStore);
				setRuntimeErrorLevel(accepted ? 0 : 1001);
			}
		} break;
		case MRVMProcedure::KeymapReset: {
			if (!args.empty()) throw std::runtime_error("KEYMAP_RESET expects no arguments.");
			if (!setConfiguredKeymapProfiles(std::vector<MRKeymapProfile>(), nullptr)) throw std::runtime_error("KEYMAP_RESET failed: invalid keymap state.");
			if (!setConfiguredActiveKeymapProfile("", nullptr)) throw std::runtime_error("KEYMAP_RESET failed: invalid active keymap profile.");
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::KeymapVersion:
		case MRVMProcedure::ThemeVersion: {
			const std::string versionLiteral = args.size() == 1 && mrvmIsStringLike(args[0]) ? trimAscii(mrvmValueAsString(args[0])) : std::string();
			std::uint64_t parsedVersion = 0;
			const std::string artifactLabel = name == "KEYMAP_VERSION" ? "Keymap" : "Theme";

			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error(name + " expects (string).");
			if (!mrParsePersistenceVersion(versionLiteral, parsedVersion)) throw std::runtime_error(name + " failed: invalid persistence version.");
			if (parsedVersion > mrCurrentPersistenceVersion()) throw std::runtime_error(name + " failed: future build version " + versionLiteral + " is not supported.");
			if (parsedVersion < mrCurrentPersistenceVersion()) mrLogMessage((artifactLabel + " file version upgrade required: " + versionLiteral).c_str());
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::KeymapProfile: {
			std::string errorText;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("KEYMAP_PROFILE expects (string).");
			if (!mrvmApplyConfiguredKeymapProfilePayload(mrvmValueAsString(args[0]), &errorText)) throw std::runtime_error("KEYMAP_PROFILE failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::KeymapBind: {
			std::string errorText;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("KEYMAP_BIND expects (string).");
			if (!mrvmApplyConfiguredKeymapBindingPayload(mrvmValueAsString(args[0]), &errorText)) throw std::runtime_error("KEYMAP_BIND failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::ActiveKeymapProfile: {
			std::string errorText;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("ACTIVE_KEYMAP_PROFILE expects (string).");
			if (!mrvmApplyConfiguredActiveKeymapProfilePayload(mrvmValueAsString(args[0]), &errorText)) throw std::runtime_error("ACTIVE_KEYMAP_PROFILE failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::ThemeReset: {
			std::string errorText;
			const MRColorSetupSettings defaults = resolveColorSetupDefaults();
			if (!args.empty()) throw std::runtime_error("THEME_RESET expects no arguments.");
			if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, defaults.windowColors.data(), defaults.windowColors.size(), &errorText)) throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid window colors.") : errorText));
			if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, defaults.menuDialogColors.data(), defaults.menuDialogColors.size(), &errorText)) throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid menu/dialog colors.") : errorText));
			if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Help, defaults.helpColors.data(), defaults.helpColors.size(), &errorText)) throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid help colors.") : errorText));
			if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Other, defaults.otherColors.data(), defaults.otherColors.size(), &errorText)) throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid other colors.") : errorText));
			if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MiniMap, defaults.miniMapColors.data(), defaults.miniMapColors.size(), &errorText)) throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid minimap colors.") : errorText));
			if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::FileCompareMiniMap, defaults.fileCompareMiniMapColors.data(), defaults.fileCompareMiniMapColors.size(), &errorText)) throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid file compare minimap colors.") : errorText));
			if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Code, defaults.codeColors.data(), defaults.codeColors.size(), &errorText)) throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid code colors.") : errorText));
			if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::FileCompare, defaults.fileCompareColors.data(), defaults.fileCompareColors.size(), &errorText)) throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid file compare colors.") : errorText));
			if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Debugger, defaults.debuggerColors.data(), defaults.debuggerColors.size(), &errorText)) throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid debugger colors.") : errorText));
			if (!setConfiguredColorThemeDisplayName("", &errorText)) throw std::runtime_error("THEME_RESET failed: " + (errorText.empty() ? std::string("invalid theme display name.") : errorText));
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::ThemeName:
		case MRVMProcedure::WindowColors:
		case MRVMProcedure::MenuDialogColors:
		case MRVMProcedure::HelpColors:
		case MRVMProcedure::OtherColors:
		case MRVMProcedure::MiniMapColors:
		case MRVMProcedure::FileCompareMiniMapColors:
		case MRVMProcedure::CodeColors:
		case MRVMProcedure::FileCompareColors:
		case MRVMProcedure::DebuggerColors: {
			std::string errorText;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error(name + " expects (string).");
			if (name == "THEME_NAME") {
				if (!setConfiguredColorThemeDisplayName(mrvmValueAsString(args[0]), &errorText)) throw std::runtime_error("THEME_NAME failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
			} else if (!applyConfiguredColorSetupValue(name, mrvmValueAsString(args[0]), &errorText, false))
				throw std::runtime_error(name + " failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::MrSetup: {
			std::string setupKey;
			std::string errorText;
			MRSetupPaths activePaths = resolveSetupPathDefaults();

			if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("MRSETUP expects (string, string).");
			setupKey = mrvmUpperKey(trimAscii(mrvmValueAsString(args[0])));
			if (setupKey == "FILECOMPAREMINIMAPCOLORS" && !mrvmIsStartupSettingsMode()) {
				if (!applyConfiguredColorSetupValue(setupKey, mrvmValueAsString(args[1]), &errorText, false)) throw std::runtime_error("MRSETUP(" + setupKey + ") failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
				setRuntimeErrorLevel(0);
				return InstructionFlow::FinishExecution;
			}
			if (!mrvmIsStartupSettingsMode()) throw std::runtime_error("MRSETUP is only allowed in settings.mrmac during startup.");
			if (setupKey == "SETTINGS_VERSION") {
				const std::string versionLiteral = trimAscii(mrvmValueAsString(args[1]));
				std::uint64_t parsedVersion = 0;

				if (!mrParsePersistenceVersion(versionLiteral, parsedVersion)) throw std::runtime_error("MRSETUP(" + setupKey + ") failed: invalid persistence version.");
				if (parsedVersion > mrCurrentPersistenceVersion()) throw std::runtime_error("MRSETUP(" + setupKey + ") failed: future build version " + versionLiteral + " is not supported.");
			} else if (setupKey == "SETTINGSPATH") {
				if (!setConfiguredSettingsMacroFilePath(mrvmValueAsString(args[1]), &errorText)) throw std::runtime_error("MRSETUP(SETTINGSPATH) failed: " + (errorText.empty() ? std::string("invalid path.") : errorText));
				activePaths.settingsMacroUri = configuredSettingsMacroFilePath();
			} else
				switch (classifySettingsKey(setupKey)) {
					case MRSettingsKeyClass::Unknown:
						throw std::runtime_error("MRSETUP supports keys: SETTINGS_VERSION, MACROPATH, SETTINGSPATH, HELPPATH, TEMPDIR, "
						                         "SHELLPATH, WINDOW_MANAGER, MESSAGES, SEARCH_TEXT_TYPE, SEARCH_DIRECTION, "
						                         "SEARCH_MODE, SEARCH_CASE_SENSITIVE, SEARCH_GLOBAL_SEARCH, "
						                         "SEARCH_RESTRICT_MARKED_BLOCK, SEARCH_ALL_WINDOWS, "
						                         "SAR_TEXT_TYPE, SAR_DIRECTION, SAR_MODE, SAR_LEAVE_CURSOR_AT, "
						                         "SAR_CASE_SENSITIVE, SAR_GLOBAL_SEARCH, SAR_RESTRICT_MARKED_BLOCK, "
						                         "SAR_ALL_WINDOWS, "
						                         "MULTI_SEARCH_FILESPEC, MULTI_SEARCH_TEXT, MULTI_SEARCH_STARTING_PATH, "
						                         "MULTI_SEARCH_SUBDIRECTORIES, MULTI_SEARCH_CASE_SENSITIVE, "
						                         "MULTI_SEARCH_REGULAR_EXPRESSIONS, MULTI_SEARCH_FILES_IN_MEMORY, "
						                         "MULTI_SEARCH_RESTRICT_WORKSPACE, "
						                         "MULTI_SAR_FILESPEC, MULTI_SAR_TEXT, MULTI_SAR_REPLACEMENT, "
						                         "MULTI_SAR_STARTING_PATH, MULTI_SAR_SUBDIRECTORIES, "
						                         "MULTI_SAR_CASE_SENSITIVE, MULTI_SAR_REGULAR_EXPRESSIONS, "
						                         "MULTI_SAR_FILES_IN_MEMORY, MULTI_SAR_KEEP_FILES_OPEN, "
						                         "MULTI_SAR_RESTRICT_WORKSPACE, "
						                         "ACQUIRE_COMMAND, ACQUIRE_COMMAND_HISTORY, "
						                         "LIVE_LOG_REPORT_MESSAGE_LINE, LIVE_LOG_REPORT_BEEP, LIVE_LOG_REPORT_AUDIO, "
						                         "LIVE_LOG_SCROLL_DIRECTION, LIVE_LOG_LINE_NUMBERS, LIVE_LOG_TIMESTAMPS, "
						                         "LIVE_LOG_SYNTAX_HIGHLIGHTING, LIVE_LOG_AUDIO_URI, LIVE_LOG_JOURNAL_TAG_HISTORY, "
						                         "AUDIO_PLAYER, AUTODETECT_BINARY_FILES, "
						                         "VIRTUAL_DESKTOPS, CYCLIC_VIRTUAL_DESKTOPS, CURSOR_BEHAVIOUR, "
						                         "COMPILER_ERROR_MESSAGE_PLACEMENT, SCROLLBAR_VISIBILITY, TRACK_COMPILER_WARNINGS, TRACK_COMPILER_NOTES, "
						                         "UI_INDENT_STYLE, CURSOR_POSITION_MARKER, WINDOW_COLORTHEME_URI, "
						                         "FILE_COMPARE_ORIGINAL_LEADING_GUTTERS, FILE_COMPARE_ORIGINAL_TRAILING_GUTTERS, FILE_COMPARE_COMPARE_LEADING_GUTTERS, FILE_COMPARE_COMPARE_TRAILING_GUTTERS, FILE_COMPARE_START_CONFIGURATION, FILE_COMPARE_COMPARE_PANEL_READ_ONLY, "
						                         "AUTOSAVE_WORKSPACE, AUTOLOAD_WORKSPACE, LOG_HANDLING, LOGFILE, AUTOEXEC_MACRO, "
						                         "LASTFILEDIALOGPATH, "
						                         "MAX_PATH_HISTORY, MAX_FILE_HISTORY, PATH_HISTORY, FILE_HISTORY, "
						                         "DIALOG_LAST_PATH, DIALOG_PATH_HISTORY, DIALOG_FILE_HISTORY, "
						                         "MULTI_FILESPEC_HISTORY, MULTI_PATH_HISTORY, "
						                         "DEFAULT_PROFILE_DESCRIPTION, PAGE_BREAK, WORD_DELIMITERS, DEFAULT_EXTENSIONS, "
						                         "TRUNCATE_SPACES, EOF_CTRL_Z, EOF_CR_LF, TAB_EXPAND, DISPLAY_TABS, TAB_SIZE, LEFT_MARGIN, RIGHT_MARGIN, FORMAT_RULER, WORD_WRAP, "
						                         "INDENT_STYLE, CODE_LANGUAGE, CODE_COLORING, FILE_TYPE, BINARY_RECORD_LENGTH, POST_LOAD_MACRO, PRE_SAVE_MACRO, DEFAULT_PATH, "
						                         "FORMAT_LINE, BACKUP_METHOD, BACKUP_FREQUENCY, BACKUP_EXTENSION, BACKUP_DIRECTORY, "
						                         "AUTOSAVE_INACTIVITY_SECONDS, AUTOSAVE_INTERVAL_SECONDS, BACKUP_FILES, SHOW_EOF_MARKER, "
						                         "SHOW_EOF_MARKER_EMOJI, LINE_NUMBERS_POSITION, LINE_NUM_ZERO_FILL, "
						                         "MINIMAP_POSITION, MINIMAP_WIDTH, MINIMAP_MARKER_GLYPH, GUTTERS, PERSISTENT_BLOCKS, "
						                         "CODE_FOLDING_POSITION, "
						                         "BLOCK_MOVE, DEFAULT_MODE, CURSOR_STATUS_COLOR.");
					case MRSettingsKeyClass::Version:
					case MRSettingsKeyClass::Path:
					case MRSettingsKeyClass::Global:
						if (!applyConfiguredSettingsAssignment(setupKey, mrvmValueAsString(args[1]), activePaths, &errorText)) throw std::runtime_error("MRSETUP(" + setupKey + ") failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
						break;
					case MRSettingsKeyClass::Edit:
						if (!applyConfiguredEditSetupValue(setupKey, mrvmValueAsString(args[1]), &errorText)) throw std::runtime_error("MRSETUP(" + setupKey + ") failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
						if (setupKey == "TAB_EXPAND") {
							BackgroundEditSession *session = currentBackgroundEditSession();
							if (session != nullptr) session->tabExpand = configuredTabExpandSetting();
							else
								mrvmStoreRuntimeStateInt("options", "tabExpand", configuredTabExpandSetting() ? 1 : 0);
						}
						break;
					case MRSettingsKeyClass::ColorInline:
						if (!applyConfiguredColorSetupValue(setupKey, mrvmValueAsString(args[1]), &errorText)) throw std::runtime_error("MRSETUP(" + setupKey + ") failed: " + (errorText.empty() ? std::string("invalid value.") : errorText));
						break;
				}
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::MrFeProfile: {
			std::string errorText;
			if (!mrvmIsStartupSettingsMode()) throw std::runtime_error("MRFEPROFILE is only allowed in settings.mrmac during startup.");
			if (args.size() != 4 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || !mrvmIsStringLike(args[2]) || !mrvmIsStringLike(args[3])) throw std::runtime_error("MRFEPROFILE expects (string, string, string, string).");
			if (!applyConfiguredEditExtensionProfileDirective(mrvmValueAsString(args[0]), mrvmValueAsString(args[1]), mrvmValueAsString(args[2]), mrvmValueAsString(args[3]), &errorText)) throw std::runtime_error("MRFEPROFILE failed: " + (errorText.empty() ? std::string("invalid directive.") : errorText));
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::MrCompilerProfile: {
			std::string errorText;
			if (!mrvmIsStartupSettingsMode()) throw std::runtime_error("MRCOMPILERPROFILE is only allowed in settings.mrmac during startup.");
			if (args.size() != 4 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || !mrvmIsStringLike(args[2]) || !mrvmIsStringLike(args[3])) throw std::runtime_error("MRCOMPILERPROFILE expects (string, string, string, string).");
			if (!applyConfiguredCompilerProfileDirective(mrvmValueAsString(args[0]), mrvmValueAsString(args[1]), mrvmValueAsString(args[2]), mrvmValueAsString(args[3]), &errorText)) throw std::runtime_error("MRCOMPILERPROFILE failed: " + (errorText.empty() ? std::string("invalid directive.") : errorText));
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiDialog: {
			mrvmBeginMacroUiDialog(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiLabel: {
			mrvmAddMacroUiLabel(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiButton: {
			mrvmAddMacroUiButton(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiDisplay: {
			mrvmAddMacroUiDisplay(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiInput: {
			mrvmAddMacroUiInput(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiListBox: {
			mrvmAddMacroUiListBox(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiGrid: {
			mrvmAddMacroUiGrid(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiTree: {
			mrvmAddMacroUiTree(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiTreeClear: {
			mrvmClearMacroUiTree(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiTreeNode: {
			mrvmAddMacroUiTreeNode(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiTable: {
			mrvmAddMacroUiTable(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiTableClear: {
			mrvmClearMacroUiTable(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiTableColumn: {
			mrvmAddMacroUiTableColumn(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiTableRow: {
			mrvmAddMacroUiTableRow(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiListClear: {
			mrvmClearMacroUiItemList(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiListAdd: {
			mrvmAddMacroUiItemListValue(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiModelessOn: {
			mrvmBindMacroModelessButton(mrvmRuntimeKv(), args);
			setRuntimeErrorLevel(0);
		} break;
		case MRVMProcedure::UiModelessShow: {
			showMacroModelessDialog(args);
		} break;
		case MRVMProcedure::UiModelessUpdate: {
			updateMacroModelessDialog(args);
		} break;
		case MRVMProcedure::UiModelessDisplay: {
			updateMacroModelessDisplayLine(args);
		} break;
		case MRVMProcedure::UiModelessClose: {
			closeMacroModelessDialog(args);
		} break;
		case MRVMProcedure::ExecSessionList: {
			listExecSessionClosures(args);
		} break;
		case MRVMProcedure::ExecSessionStop: {
			stopExecSessionClosure(args);
		} break;
		case MRVMProcedure::CreateGlobalStr:
		case MRVMProcedure::SetGlobalStr: {
			if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error(name + " expects (string, string).");
			setGlobalValue(mrvmValueAsString(args[0]), TYPE_STR, mrvmMakeString(mrvmValueAsString(args[1])));
		} break;
		case MRVMProcedure::SetGlobalInt: {
			if (args.size() != 2 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SET_GLOBAL_INT expects (string, int).");
			setGlobalValue(mrvmValueAsString(args[0]), TYPE_INT, mrvmMakeInt(args[1].i));
		} break;
		case MRVMProcedure::SetGlobalHash: {
			if (args.size() != 2 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_HASH) throw std::runtime_error("SET_GLOBAL_HASH expects (string, hash).");
			setGlobalValueFromStore(mrvmValueAsString(args[0]), TYPE_HASH, args[1], *vm.mHashStore);
		} break;
		case MRVMProcedure::Marquee:
		case MRVMProcedure::MarqueeWarning:
		case MRVMProcedure::MarqueeError:
		case MRVMProcedure::MakeMessage:
		case MRVMProcedure::UiMessageBox:
		case MRVMProcedure::Working:
		case MRVMProcedure::Brain:
		case MRVMProcedure::DesktopBlit:
		case MRVMProcedure::DesktopClear:
		case MRVMProcedure::DesktopPutChar:
		case MRVMProcedure::DesktopPutString:
		case MRVMProcedure::DesktopSetColor:
		case MRVMProcedure::PutBox:
		case MRVMProcedure::Write:
		case MRVMProcedure::ClrLine:
		case MRVMProcedure::GotoXy:
		case MRVMProcedure::PutLineNum:
		case MRVMProcedure::PutColNum:
		case MRVMProcedure::ScrollBoxUp:
		case MRVMProcedure::ScrollBoxDn:
		case MRVMProcedure::ClearScreen:
		case MRVMProcedure::KillBox: {
			int deferredError = 0;
			if (dispatchDeferredVisualUiProcedure(name, args, deferredError)) {
				setRuntimeErrorLevel(deferredError);
				return InstructionFlow::SkipPostInstruction;
			}
		} break;
		case MRVMProcedure::RegisterMenuItem:
		case MRVMProcedure::RemoveMenuItem: {
			int deferredError = 0;
			if (dispatchDeferredMenuUiProcedure(name, args, deferredError)) {
				setRuntimeErrorLevel(deferredError);
				return InstructionFlow::SkipPostInstruction;
			}
		} break;
		default:
			throw std::runtime_error("Procedure does not belong to the configuration family.");
	}

	return InstructionFlow::Completed;
}
