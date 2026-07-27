#include "MRVMSystemVariables.hpp"

#include "MRVMProcessRuntime.hpp"
#include "MRVMRuntimeInternal.hpp"
#include "MRVMValue.hpp"

#include "../../app/commands/MRWindowCommands.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRWindowSupport.hpp"

#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TMenuBar
#define Uses_TProgram
#define Uses_TStatusLine
#include <tvision/tv.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>
#include <vector>

void applyVirtualDesktopConfigurationChange(int count);

namespace mrvm_runtime {

static int encodeIndentStyle(const std::string &style) {
	const std::string key = mrvmUpperKey(style);
	if (key == "AUTOMATIC") return 1;
	if (key == "SMART") return 2;
	return 0;
}

static std::string decodeIndentStyle(int value) {
	switch (value) {
		case 1:
			return "AUTOMATIC";
		case 2:
			return "SMART";
		default:
			return "OFF";
	}
}

static int encodeBackupMode(const std::string &method) {
	const std::string key = mrvmUpperKey(method);
	if (key == "BAK_FILE") return 1;
	if (key == "DIRECTORY") return 2;
	return 0;
}

static std::string defaultFormatLineValue() {
	if (!g_runtimeEnv.defaultFormat.empty()) return g_runtimeEnv.defaultFormat;
	return resolveEditSetupDefaults().formatLine;
}

static int readWindowColorValue(std::size_t index) {
	const MRColorSetupSettings colors = configuredColorSetupSettings();
	if (index >= colors.windowColors.size()) return 0;
	return colors.windowColors[index];
}

static int readMenuDialogColorValue(std::size_t index) {
	const MRColorSetupSettings colors = configuredColorSetupSettings();
	if (index >= colors.menuDialogColors.size()) return 0;
	return colors.menuDialogColors[index];
}

static int readOtherColorValue(std::size_t index) {
	const MRColorSetupSettings colors = configuredColorSetupSettings();
	if (index >= colors.otherColors.size()) return 0;
	return colors.otherColors[index];
}

static bool writeWindowColorValue(std::size_t index, int value) {
	MRColorSetupSettings colors = configuredColorSetupSettings();
	if (index >= colors.windowColors.size()) return false;
	colors.windowColors[index] = static_cast<unsigned char>(std::clamp(value, 0, 255));
	return setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, colors.windowColors.data(), colors.windowColors.size(), nullptr);
}

static bool writeMenuDialogColorValue(std::size_t index, int value) {
	MRColorSetupSettings colors = configuredColorSetupSettings();
	if (index >= colors.menuDialogColors.size()) return false;
	colors.menuDialogColors[index] = static_cast<unsigned char>(std::clamp(value, 0, 255));
	return setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, colors.menuDialogColors.data(), colors.menuDialogColors.size(), nullptr);
}

static bool writeOtherColorValue(std::size_t index, int value) {
	MRColorSetupSettings colors = configuredColorSetupSettings();
	if (index >= colors.otherColors.size()) return false;
	colors.otherColors[index] = static_cast<unsigned char>(std::clamp(value, 0, 255));
	return setConfiguredColorSetupGroupValues(MRColorSetupGroup::Other, colors.otherColors.data(), colors.otherColors.size(), nullptr);
}

static int currentStatusRowValue() {
	if (g_runtimeEnv.statusRow >= 0) return g_runtimeEnv.statusRow;
	if (TProgram::statusLine == nullptr) return 0;
	return TProgram::statusLine->getBounds().a.y + 1;
}

static int currentMessageRowValue() {
	if (g_runtimeEnv.messageRow >= 0) return g_runtimeEnv.messageRow;
	if (TProgram::menuBar == nullptr) return 0;
	return TProgram::menuBar->getBounds().a.y + 1;
}

static int currentMaxWindowRowValue() {
	if (g_runtimeEnv.maxWindowRow >= 0) return g_runtimeEnv.maxWindowRow;
	if (TProgram::deskTop == nullptr) return 0;
	return TProgram::deskTop->getBounds().b.y;
}

static int currentMinWindowRowValue() {
	if (g_runtimeEnv.minWindowRow >= 0) return g_runtimeEnv.minWindowRow;
	if (TProgram::deskTop == nullptr) return 0;
	return TProgram::deskTop->getBounds().a.y + 1;
}

static int currentWindowAttrValue() {
	MREditWindow *win = activeMacroEditWindow();
	int value = 0;
	if (win == nullptr) return 0;
	if (isWindowManuallyHidden(win) || (win->state & sfVisible) == 0) value |= 0x01;
	return value;
}

static bool setCurrentWindowAttrValue(int value) {
	MREditWindow *win = activeMacroEditWindow();
	const bool hidden = (value & 0x01) != 0;
	if (win == nullptr || TProgram::deskTop == nullptr) return false;
	setWindowManuallyHidden(win, hidden);
	if (hidden) {
		if ((win->state & sfVisible) != 0) win->hide();
		return true;
	}
	if ((win->state & sfVisible) == 0) win->show();
	TProgram::deskTop->setCurrent(win, TView::normalSelect);
	return true;
}

static std::string formatCurrentDate() {
	char buf[32];
	std::time_t now = std::time(nullptr);
	std::tm *tmv = std::localtime(&now);
	if (tmv == nullptr) return std::string();
	std::strftime(buf, sizeof(buf), "%m/%d/%y", tmv);
	return std::string(buf);
}

static std::string formatCurrentTime() {
	char buf[32];
	std::time_t now = std::time(nullptr);
	std::tm *tmv = std::localtime(&now);
	if (tmv == nullptr) return std::string();
	std::strftime(buf, sizeof(buf), "%I:%M:%S%p", tmv);
	std::string out(buf);
	for (char &i : out)
		i = static_cast<char>(std::tolower(static_cast<unsigned char>(i)));
	return out;
}

enum class MRVMSystemVariable {
	Unknown,
	AtEof,
	AtEol,
	Autosave,
	Backups,
	BackColor,
	BlockCol1,
	BlockCol2,
	BlockLine1,
	BlockLine2,
	BlockStat,
	BufferId,
	ChangeColor,
	Comspec,
	Cpu,
	CtrlHelp,
	CurChar,
	CurFileAttr,
	CurFileSize,
	CurWindow,
	CyclicVirtualDesktops,
	CCol,
	CLine,
	CPage,
	CRow,
	Date,
	DefaultFormat,
	DisplayTabs,
	DocMode,
	ErrorColor,
	ErrorLevel,
	Explosions,
	FileChanged,
	FileName,
	FirstMacro,
	FirstRun,
	FirstSave,
	FormatLine,
	FormatRuler,
	FormatStat,
	FoundStr,
	FoundX,
	FoundY,
	GetLine,
	IgnoreCase,
	IndentLevel,
	IndentStyle,
	InsertMode,
	InsCursor,
	Key1,
	Key2,
	LastFileAttr,
	LastFileName,
	LastFileSize,
	LastFileTime,
	LeftMargin,
	LinkStat,
	LogoScreen,
	Marking,
	MaxWindowRow,
	MemAlloc,
	MenuColor,
	Messages,
	MessageRow,
	MinWindowRow,
	Mouse,
	MouseHSense,
	MouseVSense,
	MparmStr,
	MrPath,
	NameLine,
	NextMacro,
	OsVersion,
	OvrCursor,
	PageStr,
	ParamCount,
	PgLine,
	PrintMargin,
	ReadOnly,
	Refresh,
	RegExpStat,
	ReturnInt,
	ReturnStr,
	RightMargin,
	SearchFile,
	ShadowChar,
	ShadowColor,
	StatusRow,
	StatColor,
	TabExpand,
	TempPath,
	TextColor,
	Time,
	TmpFile,
	TmpFileName,
	TruncateSpaces,
	UndoStat,
	VirtualDesktops,
	WindowAttr,
	WindowCount,
	WinX1,
	WinX2,
	WinY1,
	WinY2,
	WordDelimits,
	WrapStat,
};

struct MRVMSystemVariableEntry {
	const char *name;
	MRVMSystemVariable variable;
};

static MRVMSystemVariable classifySystemVariable(const std::string &name) {
	static constexpr MRVMSystemVariableEntry entries[] = {
	    {"AT_EOF", MRVMSystemVariable::AtEof},
	    {"AT_EOL", MRVMSystemVariable::AtEol},
	    {"AUTOSAVE", MRVMSystemVariable::Autosave},
	    {"BACKUPS", MRVMSystemVariable::Backups},
	    {"BACK_COLOR", MRVMSystemVariable::BackColor},
	    {"BLOCK_COL1", MRVMSystemVariable::BlockCol1},
	    {"BLOCK_COL2", MRVMSystemVariable::BlockCol2},
	    {"BLOCK_LINE1", MRVMSystemVariable::BlockLine1},
	    {"BLOCK_LINE2", MRVMSystemVariable::BlockLine2},
	    {"BLOCK_STAT", MRVMSystemVariable::BlockStat},
	    {"BUFFER_ID", MRVMSystemVariable::BufferId},
	    {"CHANGE_COLOR", MRVMSystemVariable::ChangeColor},
	    {"COMSPEC", MRVMSystemVariable::Comspec},
	    {"CPU", MRVMSystemVariable::Cpu},
	    {"CTRL_HELP", MRVMSystemVariable::CtrlHelp},
	    {"CUR_CHAR", MRVMSystemVariable::CurChar},
	    {"CUR_FILE_ATTR", MRVMSystemVariable::CurFileAttr},
	    {"CUR_FILE_SIZE", MRVMSystemVariable::CurFileSize},
	    {"CUR_WINDOW", MRVMSystemVariable::CurWindow},
	    {"CYCLIC_VIRTUAL_DESKTOPS", MRVMSystemVariable::CyclicVirtualDesktops},
	    {"C_COL", MRVMSystemVariable::CCol},
	    {"C_LINE", MRVMSystemVariable::CLine},
	    {"C_PAGE", MRVMSystemVariable::CPage},
	    {"C_ROW", MRVMSystemVariable::CRow},
	    {"DATE", MRVMSystemVariable::Date},
	    {"DEFAULT_FORMAT", MRVMSystemVariable::DefaultFormat},
	    {"DISPLAY_TABS", MRVMSystemVariable::DisplayTabs},
	    {"DOC_MODE", MRVMSystemVariable::DocMode},
	    {"ERROR_COLOR", MRVMSystemVariable::ErrorColor},
	    {"ERROR_LEVEL", MRVMSystemVariable::ErrorLevel},
	    {"EXPLOSIONS", MRVMSystemVariable::Explosions},
	    {"FILE_CHANGED", MRVMSystemVariable::FileChanged},
	    {"FILE_NAME", MRVMSystemVariable::FileName},
	    {"FIRST_MACRO", MRVMSystemVariable::FirstMacro},
	    {"FIRST_RUN", MRVMSystemVariable::FirstRun},
	    {"FIRST_SAVE", MRVMSystemVariable::FirstSave},
	    {"FORMAT_LINE", MRVMSystemVariable::FormatLine},
	    {"FORMAT_RULER", MRVMSystemVariable::FormatRuler},
	    {"FORMAT_STAT", MRVMSystemVariable::FormatStat},
	    {"FOUND_STR", MRVMSystemVariable::FoundStr},
	    {"FOUND_X", MRVMSystemVariable::FoundX},
	    {"FOUND_Y", MRVMSystemVariable::FoundY},
	    {"GET_LINE", MRVMSystemVariable::GetLine},
	    {"IGNORE_CASE", MRVMSystemVariable::IgnoreCase},
	    {"INDENT_LEVEL", MRVMSystemVariable::IndentLevel},
	    {"INDENT_STYLE", MRVMSystemVariable::IndentStyle},
	    {"INSERT_MODE", MRVMSystemVariable::InsertMode},
	    {"INS_CURSOR", MRVMSystemVariable::InsCursor},
	    {"KEY1", MRVMSystemVariable::Key1},
	    {"KEY2", MRVMSystemVariable::Key2},
	    {"LAST_FILE_ATTR", MRVMSystemVariable::LastFileAttr},
	    {"LAST_FILE_NAME", MRVMSystemVariable::LastFileName},
	    {"LAST_FILE_SIZE", MRVMSystemVariable::LastFileSize},
	    {"LAST_FILE_TIME", MRVMSystemVariable::LastFileTime},
	    {"LEFT_MARGIN", MRVMSystemVariable::LeftMargin},
	    {"LINK_STAT", MRVMSystemVariable::LinkStat},
	    {"LOGO_SCREEN", MRVMSystemVariable::LogoScreen},
	    {"MARKING", MRVMSystemVariable::Marking},
	    {"MAX_WINDOW_ROW", MRVMSystemVariable::MaxWindowRow},
	    {"MEM_ALLOC", MRVMSystemVariable::MemAlloc},
	    {"MENU_COLOR", MRVMSystemVariable::MenuColor},
	    {"MESSAGES", MRVMSystemVariable::Messages},
	    {"MESSAGE_ROW", MRVMSystemVariable::MessageRow},
	    {"MIN_WINDOW_ROW", MRVMSystemVariable::MinWindowRow},
	    {"MOUSE", MRVMSystemVariable::Mouse},
	    {"MOUSE_H_SENSE", MRVMSystemVariable::MouseHSense},
	    {"MOUSE_V_SENSE", MRVMSystemVariable::MouseVSense},
	    {"MPARM_STR", MRVMSystemVariable::MparmStr},
	    {"MR_PATH", MRVMSystemVariable::MrPath},
	    {"NAME_LINE", MRVMSystemVariable::NameLine},
	    {"NEXT_MACRO", MRVMSystemVariable::NextMacro},
	    {"OS_VERSION", MRVMSystemVariable::OsVersion},
	    {"OVR_CURSOR", MRVMSystemVariable::OvrCursor},
	    {"PAGE_STR", MRVMSystemVariable::PageStr},
	    {"PARAM_COUNT", MRVMSystemVariable::ParamCount},
	    {"PG_LINE", MRVMSystemVariable::PgLine},
	    {"PRINT_MARGIN", MRVMSystemVariable::PrintMargin},
	    {"READ_ONLY", MRVMSystemVariable::ReadOnly},
	    {"REFRESH", MRVMSystemVariable::Refresh},
	    {"REG_EXP_STAT", MRVMSystemVariable::RegExpStat},
	    {"RETURN_INT", MRVMSystemVariable::ReturnInt},
	    {"RETURN_STR", MRVMSystemVariable::ReturnStr},
	    {"RIGHT_MARGIN", MRVMSystemVariable::RightMargin},
	    {"SEARCH_FILE", MRVMSystemVariable::SearchFile},
	    {"SHADOW_CHAR", MRVMSystemVariable::ShadowChar},
	    {"SHADOW_COLOR", MRVMSystemVariable::ShadowColor},
	    {"STATUS_ROW", MRVMSystemVariable::StatusRow},
	    {"STAT_COLOR", MRVMSystemVariable::StatColor},
	    {"TAB_EXPAND", MRVMSystemVariable::TabExpand},
	    {"TEMP_PATH", MRVMSystemVariable::TempPath},
	    {"TEXT_COLOR", MRVMSystemVariable::TextColor},
	    {"TIME", MRVMSystemVariable::Time},
	    {"TMP_FILE", MRVMSystemVariable::TmpFile},
	    {"TMP_FILE_NAME", MRVMSystemVariable::TmpFileName},
	    {"TRUNCATE_SPACES", MRVMSystemVariable::TruncateSpaces},
	    {"UNDO_STAT", MRVMSystemVariable::UndoStat},
	    {"VIRTUAL_DESKTOPS", MRVMSystemVariable::VirtualDesktops},
	    {"WINDOW_ATTR", MRVMSystemVariable::WindowAttr},
	    {"WINDOW_COUNT", MRVMSystemVariable::WindowCount},
	    {"WIN_X1", MRVMSystemVariable::WinX1},
	    {"WIN_X2", MRVMSystemVariable::WinX2},
	    {"WIN_Y1", MRVMSystemVariable::WinY1},
	    {"WIN_Y2", MRVMSystemVariable::WinY2},
	    {"WORD_DELIMITS", MRVMSystemVariable::WordDelimits},
	    {"WRAP_STAT", MRVMSystemVariable::WrapStat},
	};
	const MRVMSystemVariableEntry *first = entries;
	const MRVMSystemVariableEntry *last = entries + sizeof(entries) / sizeof(entries[0]);
	const MRVMSystemVariableEntry *found = std::lower_bound(first, last, name, [](const MRVMSystemVariableEntry &entry, const std::string &value) { return std::strcmp(entry.name, value.c_str()) < 0; });

	if (found == last || name != found->name) return MRVMSystemVariable::Unknown;
	return found->variable;
}

} // namespace mrvm_runtime

using namespace mrvm_runtime;

Value MRVMSystemVariables::load(const std::string &name, bool &handled) {
	std::string key = mrvmUpperKey(name);
	const MRVMSystemVariable variable = classifySystemVariable(key);
	handled = true;
	switch (variable) {
		case MRVMSystemVariable::ReturnInt:
			return mrvmMakeInt(runtimeReturnInt());
		case MRVMSystemVariable::ReturnStr:
			return mrvmMakeString(runtimeReturnStr());
		case MRVMSystemVariable::ErrorLevel:
			return mrvmMakeInt(runtimeErrorLevel());
		case MRVMSystemVariable::IgnoreCase:
			return mrvmMakeInt(currentRuntimeIgnoreCase() ? 1 : 0);
		case MRVMSystemVariable::RegExpStat:
			return mrvmMakeInt(currentRegexStatusValue());
		case MRVMSystemVariable::TabExpand:
			return mrvmMakeInt(currentRuntimeTabExpand() ? 1 : 0);
		case MRVMSystemVariable::DisplayTabs:
			return mrvmMakeInt(configuredDisplayTabsSetting() ? 1 : 0);
		case MRVMSystemVariable::ShadowChar:
			return mrvmMakeInt(g_runtimeEnv.shadowChar);
		case MRVMSystemVariable::Refresh:
			return mrvmMakeInt(g_runtimeEnv.refresh);
		case MRVMSystemVariable::Messages:
			return mrvmMakeInt(configuredMenulineMessages() ? 1 : 0);
		case MRVMSystemVariable::Mouse:
			return mrvmMakeInt(g_runtimeEnv.mouse);
		case MRVMSystemVariable::LogoScreen:
			return mrvmMakeInt(g_runtimeEnv.logoScreen);
		case MRVMSystemVariable::Explosions:
			return mrvmMakeInt(g_runtimeEnv.explosions);
		case MRVMSystemVariable::TruncateSpaces:
			return mrvmMakeInt(configuredEditSetupSettings().truncateSpaces ? 1 : 0);
		case MRVMSystemVariable::Backups: {
			const MREditSetupSettings settings = configuredEditSetupSettings();
			if (!settings.backupFiles) return mrvmMakeInt(0);
			return mrvmMakeInt(encodeBackupMode(settings.backupMethod));
		}
		case MRVMSystemVariable::Autosave: {
			const MREditSetupSettings settings = configuredEditSetupSettings();
			return mrvmMakeInt((settings.autosaveInactivitySeconds > 0 || settings.autosaveIntervalSeconds > 0) ? 1 : 0);
		}
		case MRVMSystemVariable::UndoStat:
			return mrvmMakeInt(g_runtimeEnv.undoStat);
		case MRVMSystemVariable::FormatStat:
			return mrvmMakeInt(g_runtimeEnv.formatStat);
		case MRVMSystemVariable::WrapStat:
			return mrvmMakeInt(configuredEditSetupSettings().wordWrap ? 1 : 0);
		case MRVMSystemVariable::MemAlloc:
			return mrvmMakeInt(g_runtimeEnv.memAlloc);
		case MRVMSystemVariable::LeftMargin:
			return mrvmMakeInt(configuredEditSetupSettings().leftMargin);
		case MRVMSystemVariable::RightMargin:
			return mrvmMakeInt(configuredEditSetupSettings().rightMargin);
		case MRVMSystemVariable::FormatRuler:
			return mrvmMakeInt(configuredEditSetupSettings().formatRuler ? 1 : 0);
		case MRVMSystemVariable::IndentStyle:
			return mrvmMakeInt(encodeIndentStyle(configuredEditSetupSettings().indentStyle));
		case MRVMSystemVariable::InsCursor:
			return mrvmMakeInt(g_runtimeEnv.insCursor);
		case MRVMSystemVariable::OvrCursor:
			return mrvmMakeInt(g_runtimeEnv.ovrCursor);
		case MRVMSystemVariable::CtrlHelp:
			return mrvmMakeInt(g_runtimeEnv.ctrlHelp);
		case MRVMSystemVariable::MouseHSense:
			return mrvmMakeInt(g_runtimeEnv.mouseHSense);
		case MRVMSystemVariable::MouseVSense:
			return mrvmMakeInt(g_runtimeEnv.mouseVSense);
		case MRVMSystemVariable::WindowAttr:
			return mrvmMakeInt(currentWindowAttrValue());
		case MRVMSystemVariable::TextColor:
			return mrvmMakeInt(readWindowColorValue(0));
		case MRVMSystemVariable::ChangeColor:
			return mrvmMakeInt(readWindowColorValue(1));
		case MRVMSystemVariable::BackColor:
			return mrvmMakeInt(readOtherColorValue(9));
		case MRVMSystemVariable::MenuColor:
			return mrvmMakeInt(readMenuDialogColorValue(0));
		case MRVMSystemVariable::StatColor:
			return mrvmMakeInt(readOtherColorValue(0));
		case MRVMSystemVariable::ErrorColor:
			return mrvmMakeInt(readOtherColorValue(4));
		case MRVMSystemVariable::ShadowColor:
			return mrvmMakeInt(readMenuDialogColorValue(7));
		case MRVMSystemVariable::StatusRow:
			return mrvmMakeInt(currentStatusRowValue());
		case MRVMSystemVariable::MessageRow:
			return mrvmMakeInt(currentMessageRowValue());
		case MRVMSystemVariable::MaxWindowRow:
			return mrvmMakeInt(currentMaxWindowRowValue());
		case MRVMSystemVariable::MinWindowRow:
			return mrvmMakeInt(currentMinWindowRowValue());
		case MRVMSystemVariable::NameLine:
			return mrvmMakeInt(g_runtimeEnv.nameLine);
		case MRVMSystemVariable::InsertMode:
			return mrvmMakeInt(currentEditorInsertMode() ? 1 : 0);
		case MRVMSystemVariable::IndentLevel:
			return mrvmMakeInt(currentEditorIndentLevel());
		case MRVMSystemVariable::MparmStr:
			return mrvmMakeString(runtimeParameterString());
		case MRVMSystemVariable::Date:
			return mrvmMakeString(formatCurrentDate());
		case MRVMSystemVariable::Time:
			return mrvmMakeString(formatCurrentTime());
		case MRVMSystemVariable::Comspec:
			return mrvmMakeString(g_runtimeEnv.shellPath);
		case MRVMSystemVariable::TempPath:
			return mrvmMakeString(configuredTempDirectoryPath());
		case MRVMSystemVariable::MrPath:
			return mrvmMakeString(g_runtimeEnv.executableDir);
		case MRVMSystemVariable::OsVersion:
			return mrvmMakeString(g_runtimeEnv.shellVersion);
		case MRVMSystemVariable::ParamCount:
			return mrvmMakeInt(static_cast<int>(g_runtimeEnv.processArgs.size()));
		case MRVMSystemVariable::Cpu:
			return mrvmMakeInt(mrvmDetectCpuCode());
		case MRVMSystemVariable::DocMode:
			return mrvmMakeInt(g_runtimeEnv.docMode);
		case MRVMSystemVariable::PrintMargin:
			return mrvmMakeInt(g_runtimeEnv.printMargin);
		case MRVMSystemVariable::CCol:
			return mrvmMakeInt(currentEditorColumn(currentEditor()));
		case MRVMSystemVariable::CLine:
			return mrvmMakeInt(currentEditorLineNumber(currentEditor()));
		case MRVMSystemVariable::CRow:
			return mrvmMakeInt(currentEditorRow(currentEditor()));
		case MRVMSystemVariable::CPage:
			return mrvmMakeInt(currentEditorPage(currentEditor()));
		case MRVMSystemVariable::PgLine:
			return mrvmMakeInt(currentEditorPageLine(currentEditor()));
		case MRVMSystemVariable::AtEof:
			return mrvmMakeInt(currentEditorAtEof(currentEditor()) ? 1 : 0);
		case MRVMSystemVariable::AtEol:
			return mrvmMakeInt(currentEditorAtEol(currentEditor()) ? 1 : 0);
		case MRVMSystemVariable::CurWindow:
			return mrvmMakeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->currentWindow : currentEditWindowIndex());
		case MRVMSystemVariable::LinkStat:
			return mrvmMakeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->linkStatus : currentLinkStatus());
		case MRVMSystemVariable::WindowCount:
			return mrvmMakeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->windowCount : countEditWindows());
		case MRVMSystemVariable::Key1:
			return mrvmMakeInt(g_runtimeEnv.key1);
		case MRVMSystemVariable::Key2:
			return mrvmMakeInt(g_runtimeEnv.key2);
		case MRVMSystemVariable::LastFileAttr:
		case MRVMSystemVariable::LastFileSize:
		case MRVMSystemVariable::LastFileTime: {
			int attr = 0;
			int size = 0;
			int packedTime = 0;
			if (!mrvmReadFileMetadata(g_runtimeEnv.lastFileName, &attr, &size, &packedTime)) return mrvmMakeInt(0);
			switch (variable) {
				case MRVMSystemVariable::LastFileAttr:
					return mrvmMakeInt(attr);
				case MRVMSystemVariable::LastFileSize:
					return mrvmMakeInt(size);
				default:
					return mrvmMakeInt(packedTime);
			}
		}
		case MRVMSystemVariable::VirtualDesktops:
			return mrvmMakeInt(configuredVirtualDesktops());
		case MRVMSystemVariable::CyclicVirtualDesktops:
			return mrvmMakeInt(configuredCyclicVirtualDesktops() ? 1 : 0);
		case MRVMSystemVariable::WinX1:
		case MRVMSystemVariable::WinY1:
		case MRVMSystemVariable::WinX2:
		case MRVMSystemVariable::WinY2: {
			BackgroundEditSession *session = currentBackgroundEditSession();
			int x1;
			int y1;
			int x2;
			int y2;
			if (session != nullptr) {
				if (!session->windowGeometryValid) return mrvmMakeInt(0);
				x1 = session->windowX1;
				y1 = session->windowY1;
				x2 = session->windowX2;
				y2 = session->windowY2;
			} else {
				if (!currentWindowGeometry(x1, y1, x2, y2)) return mrvmMakeInt(0);
			}
			switch (variable) {
				case MRVMSystemVariable::WinX1:
					return mrvmMakeInt(x1);
				case MRVMSystemVariable::WinY1:
					return mrvmMakeInt(y1);
				case MRVMSystemVariable::WinX2:
					return mrvmMakeInt(x2);
				default:
					return mrvmMakeInt(y2);
			}
		}
		case MRVMSystemVariable::BlockStat: {
			MREditWindow *win = currentEditorCommandWindow();
			return mrvmMakeInt(blockStatusValue(win));
		}
		case MRVMSystemVariable::BlockLine1: {
			MREditWindow *win = currentEditorCommandWindow();
			return mrvmMakeInt(blockLine1Value(win, currentEditor()));
		}
		case MRVMSystemVariable::BlockLine2: {
			MREditWindow *win = currentEditorCommandWindow();
			return mrvmMakeInt(blockLine2Value(win, currentEditor()));
		}
		case MRVMSystemVariable::BlockCol1: {
			MREditWindow *win = currentEditorCommandWindow();
			return mrvmMakeInt(blockCol1Value(win, currentEditor()));
		}
		case MRVMSystemVariable::BlockCol2: {
			MREditWindow *win = currentEditorCommandWindow();
			return mrvmMakeInt(blockCol2Value(win, currentEditor()));
		}
		case MRVMSystemVariable::Marking: {
			MREditWindow *win = currentEditorCommandWindow();
			return mrvmMakeInt(blockMarkingValue(win) ? 1 : 0);
		}
		case MRVMSystemVariable::LastFileName:
			return mrvmMakeString(g_runtimeEnv.lastFileName);
		case MRVMSystemVariable::FoundStr:
		case MRVMSystemVariable::SearchFile:
		case MRVMSystemVariable::FoundX:
		case MRVMSystemVariable::FoundY: {
			const SearchMatchSnapshot snapshot = currentSearchMatchSnapshot();
			if (!snapshot.valid) {
				switch (variable) {
					case MRVMSystemVariable::FoundStr:
					case MRVMSystemVariable::SearchFile:
						return mrvmMakeString("");
					default:
						return mrvmMakeInt(0);
				}
			}
			switch (variable) {
				case MRVMSystemVariable::FoundStr:
					return mrvmMakeString(snapshot.foundText);
				case MRVMSystemVariable::SearchFile:
					return mrvmMakeString(snapshot.fileName);
				case MRVMSystemVariable::FoundX:
					return mrvmMakeInt(snapshot.foundX);
				default:
					return mrvmMakeInt(snapshot.foundY);
			}
		}
		case MRVMSystemVariable::GetLine:
			return mrvmMakeString(currentEditorLineText(currentEditor()));
		case MRVMSystemVariable::FormatLine:
			return mrvmMakeString(configuredEditSetupSettings().formatLine);
		case MRVMSystemVariable::DefaultFormat:
			return mrvmMakeString(defaultFormatLineValue());
		case MRVMSystemVariable::PageStr:
			return mrvmMakeString(configuredEditSetupSettings().pageBreak);
		case MRVMSystemVariable::WordDelimits:
			return mrvmMakeString(configuredEditSetupSettings().wordDelimiters);
		case MRVMSystemVariable::CurChar:
			return currentEditorCharValue();
		case MRVMSystemVariable::FirstSave:
		case MRVMSystemVariable::BufferId:
		case MRVMSystemVariable::TmpFile:
		case MRVMSystemVariable::TmpFileName:
		case MRVMSystemVariable::FileChanged:
		case MRVMSystemVariable::FileName:
		case MRVMSystemVariable::CurFileAttr:
		case MRVMSystemVariable::CurFileSize:
		case MRVMSystemVariable::ReadOnly:
			return loadCurrentFileState(key);
		case MRVMSystemVariable::FirstRun: {
			if (!g_runtimeEnv.macroStack.empty()) return mrvmMakeInt(g_runtimeEnv.macroStack.back().firstRun ? 1 : 0);
			return mrvmMakeInt(0);
		}
		case MRVMSystemVariable::FirstMacro: {
			BackgroundEditSession *session = currentBackgroundEditSession();
			if (session != nullptr) {
				session->macroEnumIndex = 0;
				while (session->macroEnumIndex < session->macroOrder.size()) {
					const std::string &macroKey = session->macroOrder[session->macroEnumIndex++];
					std::map<std::string, std::string>::const_iterator it = session->loadedMacroDisplayNames.find(macroKey);
					if (it != session->loadedMacroDisplayNames.end()) return mrvmMakeString(it->second);
				}
			} else {
				const std::vector<std::string> orderValues = macroCatalogMacroOrder();
				std::size_t enumIndex = 0;
				setMacroCatalogMacroEnumIndex(enumIndex);
				while (enumIndex < orderValues.size()) {
					const std::string macroKey = orderValues[enumIndex++];
					MacroRef macroRef;
					setMacroCatalogMacroEnumIndex(enumIndex);
					if (readLoadedMacroByKey(macroKey, macroRef)) return mrvmMakeString(macroRef.displayName);
				}
			}
			return mrvmMakeString("");
		}
		case MRVMSystemVariable::NextMacro: {
			BackgroundEditSession *session = currentBackgroundEditSession();
			if (session != nullptr) {
				while (session->macroEnumIndex < session->macroOrder.size()) {
					const std::string &macroKey = session->macroOrder[session->macroEnumIndex++];
					std::map<std::string, std::string>::const_iterator it = session->loadedMacroDisplayNames.find(macroKey);
					if (it != session->loadedMacroDisplayNames.end()) return mrvmMakeString(it->second);
				}
			} else {
				const std::vector<std::string> orderValues = macroCatalogMacroOrder();
				std::size_t enumIndex = macroCatalogMacroEnumIndex();
				while (enumIndex < orderValues.size()) {
					const std::string macroKey = orderValues[enumIndex++];
					MacroRef macroRef;
					setMacroCatalogMacroEnumIndex(enumIndex);
					if (readLoadedMacroByKey(macroKey, macroRef)) return mrvmMakeString(macroRef.displayName);
				}
			}
			return mrvmMakeString("");
		}
		case MRVMSystemVariable::Unknown:
		default:
			handled = false;
			return mrvmMakeInt(0);
	}
}

bool MRVMSystemVariables::store(const std::string &name, const Value &value) {
	std::string key = mrvmUpperKey(name);
	switch (classifySystemVariable(key)) {
		case MRVMSystemVariable::ReturnInt: {
			runtimeReturnInt() = mrvmValueAsInt(value);
			return true;
		}
		case MRVMSystemVariable::ReturnStr: {
			runtimeReturnStr() = mrvmValueAsString(value);
			mrvmEnforceStringLength(runtimeReturnStr());
			return true;
		}
		case MRVMSystemVariable::ErrorLevel: {
			runtimeErrorLevel() = mrvmValueAsInt(value);
			return true;
		}
		case MRVMSystemVariable::IgnoreCase: {
			BackgroundEditSession *session = currentBackgroundEditSession();
			if (session != nullptr) session->ignoreCase = mrvmValueAsInt(value) != 0;
			else
				g_runtimeEnv.ignoreCase = mrvmValueAsInt(value) != 0;
			return true;
		}
		case MRVMSystemVariable::RegExpStat:
			return setCurrentRegexStatus(mrvmValueAsInt(value) != 0);
		case MRVMSystemVariable::TabExpand: {
			BackgroundEditSession *session = currentBackgroundEditSession();
			if (session != nullptr) session->tabExpand = mrvmValueAsInt(value) != 0;
			else
				g_runtimeEnv.tabExpand = mrvmValueAsInt(value) != 0;
			return true;
		}
		case MRVMSystemVariable::ShadowChar: {
			g_runtimeEnv.shadowChar = std::clamp(mrvmValueAsInt(value), 0, 255);
			return true;
		}
		case MRVMSystemVariable::Refresh: {
			g_runtimeEnv.refresh = mrvmValueAsInt(value) != 0 ? 1 : 0;
			return true;
		}
		case MRVMSystemVariable::Messages:
			return setConfiguredMenulineMessages(mrvmValueAsInt(value) != 0, nullptr);
		case MRVMSystemVariable::Mouse: {
			g_runtimeEnv.mouse = mrvmValueAsInt(value) != 0 ? 1 : 0;
			return true;
		}
		case MRVMSystemVariable::LogoScreen: {
			g_runtimeEnv.logoScreen = mrvmValueAsInt(value) != 0 ? 1 : 0;
			return true;
		}
		case MRVMSystemVariable::Explosions: {
			g_runtimeEnv.explosions = mrvmValueAsInt(value) != 0 ? 1 : 0;
			return true;
		}
		case MRVMSystemVariable::TruncateSpaces:
			return applyConfiguredEditSetupValue("TRUNCATE_SPACES", mrvmValueAsInt(value) != 0 ? "TRUE" : "FALSE", nullptr);
		case MRVMSystemVariable::Backups: {
			MREditSetupSettings settings = configuredEditSetupSettings();
			switch (mrvmValueAsInt(value)) {
				case 2:
					settings.backupMethod = "DIRECTORY";
					settings.backupFiles = true;
					break;
				case 1:
					settings.backupMethod = "BAK_FILE";
					settings.backupFiles = true;
					break;
				default:
					settings.backupMethod = "OFF";
					settings.backupFiles = false;
					break;
			}
			return setConfiguredEditSetupSettings(settings, nullptr);
		}
		case MRVMSystemVariable::Autosave: {
			MREditSetupSettings settings = configuredEditSetupSettings();
			if (mrvmValueAsInt(value) != 0) {
				const MREditSetupSettings defaults = resolveEditSetupDefaults();
				if (settings.autosaveInactivitySeconds <= 0) settings.autosaveInactivitySeconds = defaults.autosaveInactivitySeconds;
				if (settings.autosaveIntervalSeconds <= 0) settings.autosaveIntervalSeconds = defaults.autosaveIntervalSeconds;
			} else {
				settings.autosaveInactivitySeconds = 0;
				settings.autosaveIntervalSeconds = 0;
			}
			return setConfiguredEditSetupSettings(settings, nullptr);
		}
		case MRVMSystemVariable::UndoStat: {
			g_runtimeEnv.undoStat = mrvmValueAsInt(value) != 0 ? 1 : 0;
			return true;
		}
		case MRVMSystemVariable::FormatStat: {
			g_runtimeEnv.formatStat = mrvmValueAsInt(value) != 0 ? 1 : 0;
			return true;
		}
		case MRVMSystemVariable::WrapStat:
			return applyConfiguredEditSetupValue("WORD_WRAP", mrvmValueAsInt(value) != 0 ? "TRUE" : "FALSE", nullptr);
		case MRVMSystemVariable::MemAlloc: {
			g_runtimeEnv.memAlloc = std::max(0, mrvmValueAsInt(value));
			return true;
		}
		case MRVMSystemVariable::LeftMargin:
			return applyConfiguredEditSetupValue("LEFT_MARGIN", std::to_string(mrvmValueAsInt(value)), nullptr);
		case MRVMSystemVariable::RightMargin:
			return applyConfiguredEditSetupValue("RIGHT_MARGIN", std::to_string(mrvmValueAsInt(value)), nullptr);
		case MRVMSystemVariable::FormatRuler:
			return applyConfiguredEditSetupValue("FORMAT_RULER", mrvmValueAsInt(value) != 0 ? "TRUE" : "FALSE", nullptr);
		case MRVMSystemVariable::IndentStyle:
			return applyConfiguredEditSetupValue("INDENT_STYLE", decodeIndentStyle(mrvmValueAsInt(value)), nullptr);
		case MRVMSystemVariable::InsCursor: {
			g_runtimeEnv.insCursor = std::clamp(mrvmValueAsInt(value), 0, 3);
			return true;
		}
		case MRVMSystemVariable::OvrCursor: {
			g_runtimeEnv.ovrCursor = std::clamp(mrvmValueAsInt(value), 0, 3);
			return true;
		}
		case MRVMSystemVariable::CtrlHelp: {
			g_runtimeEnv.ctrlHelp = mrvmValueAsInt(value) != 0 ? 1 : 0;
			return true;
		}
		case MRVMSystemVariable::MouseHSense: {
			g_runtimeEnv.mouseHSense = std::max(0, mrvmValueAsInt(value));
			return true;
		}
		case MRVMSystemVariable::MouseVSense: {
			g_runtimeEnv.mouseVSense = std::max(0, mrvmValueAsInt(value));
			return true;
		}
		case MRVMSystemVariable::WindowAttr:
			return setCurrentWindowAttrValue(mrvmValueAsInt(value));
		case MRVMSystemVariable::TextColor:
			return writeWindowColorValue(0, mrvmValueAsInt(value));
		case MRVMSystemVariable::ChangeColor:
			return writeWindowColorValue(1, mrvmValueAsInt(value));
		case MRVMSystemVariable::BackColor:
			return writeOtherColorValue(9, mrvmValueAsInt(value));
		case MRVMSystemVariable::MenuColor:
			return writeMenuDialogColorValue(0, mrvmValueAsInt(value));
		case MRVMSystemVariable::StatColor:
			return writeOtherColorValue(0, mrvmValueAsInt(value));
		case MRVMSystemVariable::ErrorColor:
			return writeOtherColorValue(4, mrvmValueAsInt(value));
		case MRVMSystemVariable::ShadowColor:
			return writeMenuDialogColorValue(7, mrvmValueAsInt(value));
		case MRVMSystemVariable::StatusRow: {
			g_runtimeEnv.statusRow = std::max(0, mrvmValueAsInt(value));
			return true;
		}
		case MRVMSystemVariable::MessageRow: {
			g_runtimeEnv.messageRow = std::max(0, mrvmValueAsInt(value));
			return true;
		}
		case MRVMSystemVariable::MaxWindowRow: {
			g_runtimeEnv.maxWindowRow = std::max(0, mrvmValueAsInt(value));
			return true;
		}
		case MRVMSystemVariable::MinWindowRow: {
			g_runtimeEnv.minWindowRow = std::max(0, mrvmValueAsInt(value));
			return true;
		}
		case MRVMSystemVariable::NameLine: {
			g_runtimeEnv.nameLine = mrvmValueAsInt(value) != 0 ? 1 : 0;
			return true;
		}
		case MRVMSystemVariable::InsertMode:
			return setCurrentEditorInsertMode(mrvmValueAsInt(value) != 0);
		case MRVMSystemVariable::IndentLevel:
			return setCurrentEditorIndentLevel(mrvmValueAsInt(value));
		case MRVMSystemVariable::MparmStr: {
			runtimeParameterString() = mrvmValueAsString(value);
			mrvmEnforceStringLength(runtimeParameterString());
			return true;
		}
		case MRVMSystemVariable::DocMode: {
			g_runtimeEnv.docMode = mrvmValueAsInt(value) != 0 ? 1 : 0;
			return true;
		}
		case MRVMSystemVariable::PrintMargin: {
			g_runtimeEnv.printMargin = std::max(0, mrvmValueAsInt(value));
			return true;
		}
		case MRVMSystemVariable::FormatLine:
			return applyConfiguredEditSetupValue("FORMAT_LINE", mrvmValueAsString(value), nullptr);
		case MRVMSystemVariable::DefaultFormat: {
			g_runtimeEnv.defaultFormat = mrvmValueAsString(value);
			mrvmEnforceStringLength(g_runtimeEnv.defaultFormat);
			return true;
		}
		case MRVMSystemVariable::PageStr:
			return applyConfiguredEditSetupValue("PAGE_BREAK", mrvmValueAsString(value), nullptr);
		case MRVMSystemVariable::WordDelimits:
			return applyConfiguredEditSetupValue("WORD_DELIMITERS", mrvmValueAsString(value), nullptr);
		case MRVMSystemVariable::FileChanged: {
			MREditWindow *win = activeMacroEditWindow();
			BackgroundEditSession *session = currentBackgroundEditSession();
			if (win != nullptr) win->setFileChanged(mrvmValueAsInt(value) != 0);
			else if (session != nullptr)
				session->fileChanged = mrvmValueAsInt(value) != 0;
			else
				return false;
			return true;
		}
		case MRVMSystemVariable::FileName: {
			MREditWindow *win = activeMacroEditWindow();
			BackgroundEditSession *session = currentBackgroundEditSession();
			if (win != nullptr) win->setCurrentFileName(mrvmValueAsString(value).c_str());
			else if (session != nullptr)
				session->fileName = mrvmValueAsString(value);
			else
				return false;
			return true;
		}
		case MRVMSystemVariable::VirtualDesktops: {
			if (currentBackgroundEditSession() != nullptr) throw std::runtime_error("VIRTUAL_DESKTOPS cannot be changed in background mode.");
			applyVirtualDesktopConfigurationChange(mrvmValueAsInt(value));
			return true;
		}
		case MRVMSystemVariable::CyclicVirtualDesktops: {
			if (currentBackgroundEditSession() != nullptr) throw std::runtime_error("CYCLIC_VIRTUAL_DESKTOPS cannot be changed in background mode.");
			setConfiguredCyclicVirtualDesktops(mrvmValueAsInt(value) != 0, nullptr);
			return true;
		}
		case MRVMSystemVariable::FirstRun:
		case MRVMSystemVariable::FirstMacro:
		case MRVMSystemVariable::NextMacro:
		case MRVMSystemVariable::LastFileName:
		case MRVMSystemVariable::GetLine:
		case MRVMSystemVariable::CurChar:
		case MRVMSystemVariable::CCol:
		case MRVMSystemVariable::CLine:
		case MRVMSystemVariable::CRow:
		case MRVMSystemVariable::CPage:
		case MRVMSystemVariable::PgLine:
		case MRVMSystemVariable::AtEof:
		case MRVMSystemVariable::AtEol:
		case MRVMSystemVariable::CurWindow:
		case MRVMSystemVariable::LinkStat:
		case MRVMSystemVariable::WinX1:
		case MRVMSystemVariable::WinY1:
		case MRVMSystemVariable::WinX2:
		case MRVMSystemVariable::WinY2:
		case MRVMSystemVariable::WindowCount:
		case MRVMSystemVariable::Key1:
		case MRVMSystemVariable::Key2:
		case MRVMSystemVariable::BlockStat:
		case MRVMSystemVariable::BlockLine1:
		case MRVMSystemVariable::BlockLine2:
		case MRVMSystemVariable::BlockCol1:
		case MRVMSystemVariable::BlockCol2:
		case MRVMSystemVariable::Marking:
		case MRVMSystemVariable::FirstSave:
		case MRVMSystemVariable::BufferId:
		case MRVMSystemVariable::TmpFile:
		case MRVMSystemVariable::TmpFileName:
		case MRVMSystemVariable::LastFileAttr:
		case MRVMSystemVariable::LastFileSize:
		case MRVMSystemVariable::LastFileTime:
		case MRVMSystemVariable::CurFileAttr:
		case MRVMSystemVariable::CurFileSize:
		case MRVMSystemVariable::ReadOnly:
		case MRVMSystemVariable::FoundStr:
		case MRVMSystemVariable::SearchFile:
		case MRVMSystemVariable::FoundX:
		case MRVMSystemVariable::FoundY:
		case MRVMSystemVariable::Comspec:
		case MRVMSystemVariable::TempPath:
		case MRVMSystemVariable::MrPath:
		case MRVMSystemVariable::OsVersion:
		case MRVMSystemVariable::ParamCount:
		case MRVMSystemVariable::Cpu:
		case MRVMSystemVariable::DisplayTabs:
			throw std::runtime_error("Attempt to assign to read-only system variable.");
		case MRVMSystemVariable::Unknown:
		default:
			return false;
	}
}
