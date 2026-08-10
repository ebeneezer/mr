#include "MRVMSystemVariables.hpp"
#include "MRVMSystemVariableCatalog.hpp"

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
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>
#include <vector>

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
	const std::string configured = mrvmRuntimeStateString("system", "defaultFormat");
	if (!configured.empty()) return configured;
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
	const int configured = mrvmRuntimeStateInt("system", "statusRow", -1);
	if (configured >= 0) return configured;
	if (TProgram::statusLine == nullptr) return 0;
	return TProgram::statusLine->getBounds().a.y + 1;
}

static int currentMessageRowValue() {
	const int configured = mrvmRuntimeStateInt("system", "messageRow", -1);
	if (configured >= 0) return configured;
	if (TProgram::menuBar == nullptr) return 0;
	return TProgram::menuBar->getBounds().a.y + 1;
}

static int currentMaxWindowRowValue() {
	const int configured = mrvmRuntimeStateInt("system", "maxWindowRow", -1);
	if (configured >= 0) return configured;
	if (TProgram::deskTop == nullptr) return 0;
	return TProgram::deskTop->getBounds().b.y;
}

static int currentMinWindowRowValue() {
	const int configured = mrvmRuntimeStateInt("system", "minWindowRow", -1);
	if (configured >= 0) return configured;
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
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "shadowChar", 176));
		case MRVMSystemVariable::Refresh:
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "refresh", 1));
		case MRVMSystemVariable::Messages:
			return mrvmMakeInt(configuredMenulineMessages() ? 1 : 0);
		case MRVMSystemVariable::Mouse:
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "mouse", 1));
		case MRVMSystemVariable::LogoScreen:
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "logoScreen"));
		case MRVMSystemVariable::Explosions:
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "explosions"));
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
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "undoStat", 1));
		case MRVMSystemVariable::FormatStat:
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "formatStat"));
		case MRVMSystemVariable::WrapStat:
			return mrvmMakeInt(configuredEditSetupSettings().wordWrap ? 1 : 0);
		case MRVMSystemVariable::MemAlloc:
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "memAlloc"));
		case MRVMSystemVariable::LeftMargin:
			return mrvmMakeInt(configuredEditSetupSettings().leftMargin);
		case MRVMSystemVariable::RightMargin:
			return mrvmMakeInt(configuredEditSetupSettings().rightMargin);
		case MRVMSystemVariable::FormatRuler:
			return mrvmMakeInt(configuredEditSetupSettings().formatRuler ? 1 : 0);
		case MRVMSystemVariable::IndentStyle:
			return mrvmMakeInt(encodeIndentStyle(configuredEditSetupSettings().indentStyle));
		case MRVMSystemVariable::InsCursor:
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "insCursor"));
		case MRVMSystemVariable::OvrCursor:
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "ovrCursor", 3));
		case MRVMSystemVariable::CtrlHelp:
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "ctrlHelp"));
		case MRVMSystemVariable::MouseHSense:
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "mouseHSense", 8));
		case MRVMSystemVariable::MouseVSense:
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "mouseVSense", 8));
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
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "nameLine"));
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
			return mrvmMakeString(mrvmRuntimeStateString("process", "shellPath"));
		case MRVMSystemVariable::TempPath:
			return mrvmMakeString(configuredTempDirectoryPath());
		case MRVMSystemVariable::MrPath:
			return mrvmMakeString(mrvmRuntimeStateString("process", "executableDir"));
		case MRVMSystemVariable::ParamCount:
			return mrvmMakeInt(static_cast<int>(mrvmRuntimeStateStringList("process", "arguments").size()));
		case MRVMSystemVariable::Cpu:
			return mrvmMakeInt(mrvmDetectCpuCode());
		case MRVMSystemVariable::DocMode:
			return mrvmMakeInt(mrvmRuntimeStateInt("system", "docMode"));
		case MRVMSystemVariable::PrintMargin:
			return mrvmMakeInt(std::atoi(configuredPdfExportSettings().textWidth.c_str()));
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
			return mrvmMakeInt(mrvmRuntimeStateInt("keyInput", "key1"));
		case MRVMSystemVariable::Key2:
			return mrvmMakeInt(mrvmRuntimeStateInt("keyInput", "key2"));
		case MRVMSystemVariable::LastFileAttr:
		case MRVMSystemVariable::LastFileSize:
		case MRVMSystemVariable::LastFileTime: {
			int attr = 0;
			int size = 0;
			int packedTime = 0;
			if (!mrvmReadFileMetadata(mrvmRuntimeStateString("fileEnumeration", "lastFileName"), &attr, &size, &packedTime)) return mrvmMakeInt(0);
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
			return mrvmMakeString(mrvmRuntimeStateString("fileEnumeration", "lastFileName"));
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
			const std::vector<MacroStackFrame> macroStack = mrvmRuntimeMacroStack();
			if (!macroStack.empty()) return mrvmMakeInt(macroStack.back().firstRun ? 1 : 0);
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
			setRuntimeReturnInt(mrvmValueAsInt(value));
			return true;
		}
		case MRVMSystemVariable::ReturnStr: {
			setRuntimeReturnStr(mrvmValueAsString(value));
			mrvmEnforceStringLength(runtimeReturnStr());
			return true;
		}
		case MRVMSystemVariable::ErrorLevel: {
			setRuntimeErrorLevel(mrvmValueAsInt(value));
			return true;
		}
		case MRVMSystemVariable::IgnoreCase: {
			BackgroundEditSession *session = currentBackgroundEditSession();
			if (session != nullptr) session->ignoreCase = mrvmValueAsInt(value) != 0;
			else
				mrvmStoreRuntimeStateInt("options", "ignoreCase", mrvmValueAsInt(value) != 0 ? 1 : 0);
			return true;
		}
		case MRVMSystemVariable::RegExpStat:
			return setCurrentRegexStatus(mrvmValueAsInt(value) != 0);
		case MRVMSystemVariable::TabExpand: {
			BackgroundEditSession *session = currentBackgroundEditSession();
			if (session != nullptr) session->tabExpand = mrvmValueAsInt(value) != 0;
			else
				mrvmStoreRuntimeStateInt("options", "tabExpand", mrvmValueAsInt(value) != 0 ? 1 : 0);
			return true;
		}
		case MRVMSystemVariable::ShadowChar: {
			mrvmStoreRuntimeStateInt("system", "shadowChar", std::clamp(mrvmValueAsInt(value), 0, 255));
			return true;
		}
		case MRVMSystemVariable::Refresh: {
			mrvmStoreRuntimeStateInt("system", "refresh", mrvmValueAsInt(value) != 0 ? 1 : 0);
			return true;
		}
		case MRVMSystemVariable::Messages:
			return setConfiguredMenulineMessages(mrvmValueAsInt(value) != 0, nullptr);
		case MRVMSystemVariable::Mouse: {
			mrvmStoreRuntimeStateInt("system", "mouse", mrvmValueAsInt(value) != 0 ? 1 : 0);
			return true;
		}
		case MRVMSystemVariable::LogoScreen: {
			mrvmStoreRuntimeStateInt("system", "logoScreen", mrvmValueAsInt(value) != 0 ? 1 : 0);
			return true;
		}
		case MRVMSystemVariable::Explosions: {
			mrvmStoreRuntimeStateInt("system", "explosions", mrvmValueAsInt(value) != 0 ? 1 : 0);
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
			mrvmStoreRuntimeStateInt("system", "undoStat", mrvmValueAsInt(value) != 0 ? 1 : 0);
			return true;
		}
		case MRVMSystemVariable::FormatStat: {
			mrvmStoreRuntimeStateInt("system", "formatStat", mrvmValueAsInt(value) != 0 ? 1 : 0);
			return true;
		}
		case MRVMSystemVariable::WrapStat:
			return applyConfiguredEditSetupValue("WORD_WRAP", mrvmValueAsInt(value) != 0 ? "TRUE" : "FALSE", nullptr);
		case MRVMSystemVariable::MemAlloc: {
			mrvmStoreRuntimeStateInt("system", "memAlloc", std::max(0, mrvmValueAsInt(value)));
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
			mrvmStoreRuntimeStateInt("system", "insCursor", std::clamp(mrvmValueAsInt(value), 0, 3));
			return true;
		}
		case MRVMSystemVariable::OvrCursor: {
			mrvmStoreRuntimeStateInt("system", "ovrCursor", std::clamp(mrvmValueAsInt(value), 0, 3));
			return true;
		}
		case MRVMSystemVariable::CtrlHelp: {
			mrvmStoreRuntimeStateInt("system", "ctrlHelp", mrvmValueAsInt(value) != 0 ? 1 : 0);
			return true;
		}
		case MRVMSystemVariable::MouseHSense: {
			mrvmStoreRuntimeStateInt("system", "mouseHSense", std::max(0, mrvmValueAsInt(value)));
			return true;
		}
		case MRVMSystemVariable::MouseVSense: {
			mrvmStoreRuntimeStateInt("system", "mouseVSense", std::max(0, mrvmValueAsInt(value)));
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
			mrvmStoreRuntimeStateInt("system", "statusRow", std::max(0, mrvmValueAsInt(value)));
			return true;
		}
		case MRVMSystemVariable::MessageRow: {
			mrvmStoreRuntimeStateInt("system", "messageRow", std::max(0, mrvmValueAsInt(value)));
			return true;
		}
		case MRVMSystemVariable::MaxWindowRow: {
			mrvmStoreRuntimeStateInt("system", "maxWindowRow", std::max(0, mrvmValueAsInt(value)));
			return true;
		}
		case MRVMSystemVariable::MinWindowRow: {
			mrvmStoreRuntimeStateInt("system", "minWindowRow", std::max(0, mrvmValueAsInt(value)));
			return true;
		}
		case MRVMSystemVariable::NameLine: {
			mrvmStoreRuntimeStateInt("system", "nameLine", mrvmValueAsInt(value) != 0 ? 1 : 0);
			return true;
		}
		case MRVMSystemVariable::InsertMode:
			return setCurrentEditorInsertMode(mrvmValueAsInt(value) != 0);
		case MRVMSystemVariable::IndentLevel:
			return setCurrentEditorIndentLevel(mrvmValueAsInt(value));
		case MRVMSystemVariable::MparmStr: {
			setRuntimeParameterString(mrvmValueAsString(value));
			mrvmEnforceStringLength(runtimeParameterString());
			return true;
		}
		case MRVMSystemVariable::DocMode: {
			mrvmStoreRuntimeStateInt("system", "docMode", mrvmValueAsInt(value) != 0 ? 1 : 0);
			return true;
		}
		case MRVMSystemVariable::FormatLine:
			return applyConfiguredEditSetupValue("FORMAT_LINE", mrvmValueAsString(value), nullptr);
		case MRVMSystemVariable::DefaultFormat: {
			const std::string format = mrvmValueAsString(value);
			mrvmEnforceStringLength(format);
			mrvmStoreRuntimeStateString("system", "defaultFormat", format);
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
		case MRVMSystemVariable::ParamCount:
		case MRVMSystemVariable::Cpu:
		case MRVMSystemVariable::PrintMargin:
		case MRVMSystemVariable::DisplayTabs:
			throw std::runtime_error("Attempt to assign to read-only system variable.");
		case MRVMSystemVariable::Unknown:
		default:
			return false;
	}
}
