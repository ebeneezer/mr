#include "utils/MRFileIOUtils.hpp"
#include "utils/MRStringUtils.hpp"
#define Uses_TKeys
#define Uses_MsgBox
#define Uses_TDialog
#define Uses_TStaticText
#define Uses_TFileDialog
#define Uses_TObject
#define Uses_TApplication
#define Uses_TEvent
#define Uses_TRect
#define Uses_TView
#define Uses_TDrawBuffer
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_TDeskTop
#define Uses_TScreen
#include <tvision/tv.h>

#include "MREditorApp.hpp"

#include "../coprocessor/MRCoprocessor.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/MRVM.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../coprocessor/MRCoprocessorDispatch.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "../dialogs/MRDirtyGating.hpp"
#include "../dialogs/setup/MRSetupCommon.hpp"
#include "../coprocessor/MRPerformance.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../ui/MRDeskTop.hpp"
#include "../ui/MRBentoBox.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRMenuBar.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRStatusLine.hpp"
#include "../ui/MRPalette.hpp"
#include "../ui/MRPerformancePanel.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/MRWindowManager.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "MRAppState.hpp"
#include "MRCommandRouter.hpp"
#include "MRCommands.hpp"
#include "MRMenuFactory.hpp"

#include <algorithm>
#include <ctime>
#include <chrono>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fnmatch.h>
#include <glob.h>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace {
static constexpr std::chrono::milliseconds kRecordingBlinkInterval(450);
static constexpr std::chrono::microseconds kCoprocessorIdlePumpBudget(1000);
static constexpr std::chrono::seconds kFullscreenHintDuration(3);
static constexpr const char *kFullscreenHintText = "F11/ESC exit Fullscreen   F10 Menu";

TFrame *initMrDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

int fullscreenHintTextWidth() noexcept {
	return strwidth(kFullscreenHintText);
}

class MRFullscreenHintView final : public TView {
  public:
	explicit MRFullscreenHintView(const TRect &bounds) noexcept : TView(bounds) {
	}

	void draw() override {
		TDrawBuffer buffer;
		const TColorAttr color = TColorAttr(0x1F);

		buffer.moveStr(0, kFullscreenHintText, color, static_cast<ushort>(size.x));
		writeLine(0, 0, size.x, 1, buffer);
	}
};

bool shouldInvalidateScreenBaseForEvent(ushort eventWhat) noexcept {
	switch (eventWhat) {
		case evKeyDown:
		case evMouseDown:
		case evMouseWheel:
		case evCommand:
			return true;
		default:
			return false;
	}
}

bool isCalculatorHotkeyEvent(const TEvent &event) noexcept {
	if (event.what != evKeyDown) return false;
	const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);
	return (normalized.mods & kbAltShift) != 0 && normalized.code == 'C';
}

bool isBuildCurrentFileDefaultKey(const TEvent &event) noexcept {
	if (event.what != evKeyDown) return false;
	const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);

	return normalized == TKey(kbF9);
}

bool compilerDiagnosticsFunctionKeysActive() {
	MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(currentEditWindow());

	return bentoBox != nullptr && bentoBox->problemsPane() != nullptr && bentoBox->hasCompilerProblems();
}

MRBentoBox *currentFileCompareBentoBox() {
	TView *view = currentEditWindow();

	while (view != nullptr) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(view);
		if (bentoBox != nullptr && bentoBox->isFileCompareBox()) return bentoBox;
		view = view->owner;
	}
	return nullptr;
}

bool fileCompareFunctionKeysActive() {
	return currentFileCompareBentoBox() != nullptr;
}

bool bentoToolPaneFunctionKeysActive() {
	MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(currentEditWindow());

	return bentoBox != nullptr && bentoBox->secondaryEditWindow() != nullptr && !compilerDiagnosticsFunctionKeysActive() && !fileCompareFunctionKeysActive();
}

const std::vector<MRStatusLine::FunctionKeyLabel> &startupFunctionKeyLabels() {
	static const std::vector<MRStatusLine::FunctionKeyLabel> labels{
	    {TKey(kbF1), cmMrHelpContents, "~F1~ Help"},
	    {TKey(kbF2), cmMrFileLoad, "~F2~ Load"},
	    {TKey(kbF3), cmMrFileOpen, "~F3~ Open"},
	    {TKey(kbF4), cmMrFileAcquire, "~F4~ Acquire"},
	    {TKey(kbF5), cmMrSearchMultiFileSearch, "~F5~ MFS"},
	    {TKey(kbF6), cmMrWindowOpen, "~F6~ Win"},
	    {TKey(kbF7), cmMrSearchMultiFileSearchReplace, "~F7~ MFSAR"},
	    {TKey(kbF8), cmMrFileOpenLiveLog, "~F8~ Log"},
	    {TKey(kbF9), cmMrFileOpenJournal, "~F9~ Journal"},
	    {TKey(kbF10), cmMenu, "~F10~ Menu"},
	    {TKey(kbF11), cmMrToggleFullscreen, "~F11~ Flscr"},
	    {TKey(kbF12), cmMrSetupUserInterfaceSettings, "~F12~ Setup"},
	};
	return labels;
}

const std::vector<MRStatusLine::FunctionKeyLabel> &editorFunctionKeyLabels() {
	static const std::vector<MRStatusLine::FunctionKeyLabel> baseLabels{
	    {TKey(kbF1), cmMrHelpContents, "~F1~ Help"},
	    {TKey(kbF2), cmMrFileSave, "~F2~ Save"},
	    {TKey(kbF3), cmMrBlockLoadFromDisk, "~F3~ LoadBlk"},
	    {TKey(kbF4), cmMrBlockSaveToDisk, "~F4~ SaveBlk"},
	    {TKey(kbF5), cmMrWindowCascade, "~F5~ Casc"},
	    {TKey(kbF6), cmMrWindowTile, "~F6~ Tile"},
	    {TKey(kbF7), cmMrBlockMarkLines, "~F7~ Mark"},
	    {TKey(kbF8), cmMrBlockCopy, "~F8~ CopyBlk"},
	    {TKey(kbF9), cmMrOtherBuildCurrentFile, "~F9~ Build"},
	    {TKey(kbF10), cmMenu, "~F10~ Menu"},
	    {TKey(kbF11), cmMrToggleFullscreen, "~F11~ Flscr"},
	    {TKey(kbF12), cmMrSetupUserInterfaceSettings, "~F12~ Setup"},
	};
	static std::vector<MRStatusLine::FunctionKeyLabel> labels = baseLabels;
	MREditWindow *window = currentEditorCommandWindow();
	const bool diagnosticsActive = compilerDiagnosticsFunctionKeysActive();
	const bool fileCompareActive = fileCompareFunctionKeysActive();
	const bool bentoToolPaneActive = bentoToolPaneFunctionKeysActive();
	const bool readOnlyActive = window != nullptr && window->isReadOnly();

	labels = baseLabels;
	if (fileCompareActive) {
		labels[2] = {TKey(kbF3), cmMrWindowSplitHorizontal, "~F3~ SplitH"};
		labels[3] = {TKey(kbF4), cmMrWindowSplitVertical, "~F4~ SplitV"};
		labels[4] = {TKey(kbF5), cmMrOtherClearOutput, "~F5~ Clear"};
		labels[5] = {TKey(kbF6), cmMrWindowTile, "~F6~ Tile"};
		labels[6] = {TKey(kbShiftF8), cmMrFileComparePreviousChange, "~sF8~ Prev"};
		labels[7] = {TKey(kbF8), cmMrFileCompareNextChange, "~F8~ Next"};
	} else if (bentoToolPaneActive) {
		labels[2] = {TKey(kbF3), cmMrWindowSplitHorizontal, "~F3~ SplitH"};
		labels[3] = {TKey(kbF4), cmMrWindowSplitVertical, "~F4~ SplitV"};
		labels[4] = {TKey(kbF5), cmMrOtherClearOutput, "~F5~ Clear"};
		labels[5] = {TKey(kbF6), cmMrWindowTile, "~F6~ Tile"};
		labels[6] = {TKey(kbF7), cmMrSearchGotoLineNumber, "~F7~ Goto"};
		labels[7] = {TKey(kbF8), cmMrSearchRepeatPrevious, "~F8~ Repeat"};
	} else if (readOnlyActive) {
		labels[2] = {TKey(kbF3), cmMrFileSaveAs, "~F3~ SaveAs"};
		labels[3] = {TKey(kbF4), cmMrSearchFindText, "~F4~ Find"};
		labels[4] = {TKey(kbF5), cmMrSearchMultiFileSearch, "~F5~ MFS"};
		labels[5] = {TKey(kbF6), cmMrWindowTile, "~F6~ Tile"};
		labels[6] = {TKey(kbF7), cmMrSearchGotoLineNumber, "~F7~ Goto"};
		labels[7] = {TKey(kbF8), cmMrSearchRepeatPrevious, "~F8~ Repeat"};
	}
	if (diagnosticsActive) {
		labels[2] = {TKey(kbF3), cmMrWindowSplitHorizontal, "~F3~ SplitH"};
		labels[3] = {TKey(kbF4), cmMrWindowSplitVertical, "~F4~ SplitV"};
		labels[4] = {TKey(kbF5), cmMrOtherClearOutput, "~F5~ Clear"};
		labels[5] = {TKey(kbF6), cmMrWindowTile, "~F6~ Tile"};
		labels[6] = {TKey(kbF7), cmMrOtherFindPreviousCompilerError, "~F7~ PrevErr"};
		labels[7] = {TKey(kbF8), cmMrOtherFindNextCompilerError, "~F8~ NextErr"};
	} else if (!fileCompareActive && !bentoToolPaneActive && !readOnlyActive) {
		labels[2] = {TKey(kbF3), cmMrBlockLoadFromDisk, "~F3~ LoadBlk"};
		labels[3] = {TKey(kbF4), cmMrBlockSaveToDisk, "~F4~ SaveBlk"};
		labels[4] = {TKey(kbF5), cmMrWindowCascade, "~F5~ Casc"};
		labels[5] = {TKey(kbF6), cmMrWindowTile, "~F6~ Tile"};
		labels[7] = {TKey(kbF8), cmMrBlockCopy, "~F8~ CopyBlk"};
	}
	if (!diagnosticsActive && !fileCompareActive && !bentoToolPaneActive && !readOnlyActive && window != nullptr && window->isBlockMarking()) {
		labels[6] = {TKey(kbF7), cmMrBlockEndMarking, "~F7~ EndMark"};
	} else if (!diagnosticsActive && !fileCompareActive && !bentoToolPaneActive && !readOnlyActive)
		labels[6] = {TKey(kbF7), cmMrBlockMarkLines, "~F7~ Mark"};
	return labels;
}

bool editorFunctionKeyContextActive() {
	MREditWindow *editorWindow = currentEditorCommandWindow();

	return currentEditWindow() != nullptr && editorWindow != nullptr;
}

bool handleStartupFunctionKey(TEvent &event) {
	const TKey pressed(event.keyDown);

	for (const MRStatusLine::FunctionKeyLabel &label : startupFunctionKeyLabels()) {
		if (!(pressed == label.keyCode) || !TView::commandEnabled(label.command)) continue;
		if (label.command == cmMenu) return false;
		if (label.command == cmQuit) {
			event.what = evCommand;
			event.message.command = cmQuit;
			event.message.infoPtr = nullptr;
			return false;
		}
		return handleMRCommand(label.command);
	}
	return false;
}

bool handleEditorFunctionKey(TEvent &event) {
	const TKey pressed(event.keyDown);

	if (fileCompareFunctionKeysActive() && (pressed == TKey(kbF8) || pressed == TKey(kbShiftF8) || (event.keyDown.keyCode == kbF8 && (event.keyDown.controlKeyState & kbShift) != 0))) return false;
	for (const MRStatusLine::FunctionKeyLabel &label : editorFunctionKeyLabels()) {
		if (!(pressed == label.keyCode)) continue;
		if (label.command == cmMenu) return false;
		if (!TView::commandEnabled(label.command)) return false;
		static_cast<void>(handleMRCommand(label.command));
		return true;
	}
	return false;
}

bool handleFileCompareFunctionKey(TEvent &event) {
	if (event.what != evKeyDown) return false;
	const TKey pressed(event.keyDown);
	const bool fileCompareNavigationKey = pressed == TKey(kbF8) || pressed == TKey(kbShiftF8) || (event.keyDown.keyCode == kbF8 && (event.keyDown.controlKeyState & kbShift) != 0);
	const bool nextChange = event.keyDown.keyCode == kbF8 && (event.keyDown.controlKeyState & kbShift) == 0;

	if (!fileCompareNavigationKey) return false;
	MRBentoBox *bentoBox = currentFileCompareBentoBox();
	if (bentoBox == nullptr) return false;
	if (!bentoBox->navigateFileCompareChange(nextChange)) return false;

	event.what = evNothing;
	return true;
}

bool handleFileCompareCommand(TEvent &event) {
	if (event.what != evCommand) return false;
	const bool nextChange = event.message.command == cmMrFileCompareNextChange;
	const bool previousChange = event.message.command == cmMrFileComparePreviousChange;

	if (!nextChange && !previousChange) return false;
	MRBentoBox *bentoBox = currentFileCompareBentoBox();
	if (bentoBox != nullptr) static_cast<void>(bentoBox->navigateFileCompareChange(nextChange));
	event.what = evNothing;
	return true;
}

void traceCalculatorHotkeyEvent(const char *stage, const TEvent &event) {
	if (!isCalculatorHotkeyEvent(event)) return;
	const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);
	char line[288];
	std::snprintf(line, sizeof(line), "KEYDBG calc stage=%s rawCode=0x%04X rawMods=0x%04X normCode=0x%04X normMods=0x%04X textLen=%u char=0x%02X", stage, static_cast<unsigned>(event.keyDown.keyCode), static_cast<unsigned>(event.keyDown.controlKeyState), static_cast<unsigned>(normalized.code), static_cast<unsigned>(normalized.mods), static_cast<unsigned>(event.keyDown.textLength), static_cast<unsigned>(static_cast<unsigned char>(event.keyDown.charScan.charCode)));
	mrLogMessage(line);
}

bool keyDebugEnabled() noexcept {
	static int cached = -1;

	if (cached < 0) {
		const char *value = std::getenv("MR_KEY_DEBUG");
		cached = (value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0) ? 1 : 0;
	}
	return cached == 1;
}

void traceKeyDebugEvent(const char *stage, const TEvent &event) {
	if (!keyDebugEnabled() || event.what != evKeyDown) return;
	const TKey normalized(event.keyDown.keyCode, event.keyDown.controlKeyState);
	char line[320];

	std::snprintf(line, sizeof(line), "KEYDBG event stage=%s rawCode=0x%04X rawMods=0x%04X normCode=0x%04X normMods=0x%04X textLen=%u char=0x%02X scan=0x%02X", stage, static_cast<unsigned>(event.keyDown.keyCode), static_cast<unsigned>(event.keyDown.controlKeyState), static_cast<unsigned>(normalized.code), static_cast<unsigned>(normalized.mods), static_cast<unsigned>(event.keyDown.textLength), static_cast<unsigned>(static_cast<unsigned char>(event.keyDown.charScan.charCode)), static_cast<unsigned>(static_cast<unsigned char>(event.keyDown.charScan.scanCode)));
	mrLogMessage(line);
}

void postAppError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, text, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

class TMacroBindCaptureDialog : public MRDialogFoundation {
  public:
	TMacroBindCaptureDialog() : TWindowInit(initMrDialogFrame), MRDialogFoundation(centeredSetupDialogRect(52, 8), "BIND RECORDED MACRO KEY", 52, 8, initMrDialogFrame), captureAccepted(false), capturedKeyCode(kbNoKey), capturedControlState(0) {
		insert(new TStaticText(TRect(2, 2, 50, 6), "Press key to bind the recorded macro.\nEsc = no binding."));
	}

	virtual void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown) {
			TKey pressed(event.keyDown);
			if (pressed == TKey(kbEsc)) {
				endModal(cmCancel);
				clearEvent(event);
				return;
			}
			if (pressed == TKey(kbAltF10)) {
				clearEvent(event);
				return;
			}
			captureAccepted = true;
			capturedKeyCode = event.keyDown.keyCode;
			capturedControlState = event.keyDown.controlKeyState;
			endModal(cmOK);
			clearEvent(event);
			return;
		}
		MRDialogFoundation::handleEvent(event);
	}

	bool hasCaptured() const noexcept {
		return captureAccepted;
	}

	ushort keyCode() const noexcept {
		return capturedKeyCode;
	}

	ushort controlState() const noexcept {
		return capturedControlState;
	}

  private:
	bool captureAccepted;
	ushort capturedKeyCode;
	ushort capturedControlState;
};

std::string expandUserPath(std::string_view input) {
	std::string path = trimAscii(input);
	if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != nullptr && *home != '\0') return std::string(home) + path.substr(1);
	}
	return path;
}

std::string ensureMrmacExtension(std::string_view pathView) {
	std::string path(pathView);
	std::size_t dotPos = path.rfind('.');
	if (dotPos != std::string::npos) {
		std::string ext = path.substr(dotPos);
		for (char &i : ext)
			i = static_cast<char>(std::tolower(static_cast<unsigned char>(i)));
		if (ext == ".mrmac") return path;
	}
	return path + ".mrmac";
}

bool writeTextFileWithConfiguredSaveOptions(const std::string &path, const std::string &content) {
	return writeTextFile(path, normalizeTextForSave(content, effectiveTextSaveOptionsForPath(path)));
}

std::string makeRecordedMacroName(unsigned long counter) {
	std::array<char, 32> timePart{};
	std::time_t now = std::time(nullptr);
	std::tm tmNow;

#if defined(__unix__)
	localtime_r(&now, &tmNow);
#else
	tmNow = *std::localtime(&now);
#endif
	std::strftime(timePart.data(), timePart.size(), "Recorded_%Y%m%d_%H%M%S", &tmNow);
	return std::string(timePart.data()) + "_" + std::to_string(counter);
}

void appendEscapedKeyInChar(std::string &out, unsigned char ch) {
	if (ch == '\'') {
		out += "''";
		return;
	}
	if (ch == '\r' || ch == '\n') {
		out += "<Enter>";
		return;
	}
	if (ch == '\t') {
		out += "<Tab>";
		return;
	}
	if (ch == '\b') {
		out += "<Backspace>";
		return;
	}
	if (ch == 27) {
		out += "<Esc>";
		return;
	}
	if (ch == 127) {
		out += "<Del>";
		return;
	}
	if (std::isprint(ch) == 0) return;
	out.push_back(static_cast<char>(ch));
}

bool keyInTokenFromEvent(ushort keyCode, ushort controlKeyState, std::string &outToken) {
	struct ComboSpec {
		const char *prefix;
		ushort mods;
	};
	struct NamedKeySpec {
		const char *token;
		ushort code;
	};
	static const ComboSpec combos[] = {{"", 0}, {"Shft", kbShift}, {"Ctrl", kbCtrlShift}, {"Alt", kbAltShift}, {"CtrlShft", static_cast<ushort>(kbCtrlShift | kbShift)}, {"AltShft", static_cast<ushort>(kbAltShift | kbShift)}, {"CtrlAlt", static_cast<ushort>(kbCtrlShift | kbAltShift)}, {"CtrlAltShft", static_cast<ushort>(kbCtrlShift | kbAltShift | kbShift)}, {"Super", kbSuperShift}, {"SuperShft", static_cast<ushort>(kbSuperShift | kbShift)}, {"SuperCtrl", static_cast<ushort>(kbSuperShift | kbCtrlShift)}, {"SuperAlt", static_cast<ushort>(kbSuperShift | kbAltShift)}, {"SuperCtrlShft", static_cast<ushort>(kbSuperShift | kbCtrlShift | kbShift)}, {"SuperAltShft", static_cast<ushort>(kbSuperShift | kbAltShift | kbShift)}, {"SuperCtrlAlt", static_cast<ushort>(kbSuperShift | kbCtrlShift | kbAltShift)}, {"SuperCtrlAltShft", static_cast<ushort>(kbSuperShift | kbCtrlShift | kbAltShift | kbShift)}};
	static const NamedKeySpec named[] = {{"Enter", kbEnter}, {"Tab", kbTab}, {"Esc", kbEsc}, {"Backspace", kbBack}, {"Up", kbUp}, {"Down", kbDown}, {"Left", kbLeft}, {"Right", kbRight}, {"PgUp", kbPgUp}, {"PgDn", kbPgDn}, {"Home", kbHome}, {"End", kbEnd}, {"Ins", kbIns}, {"Del", kbDel}, {"Grey-", kbGrayMinus}, {"Grey+", kbGrayPlus}, {"Grey*", static_cast<ushort>('*')}, {"Space", static_cast<ushort>(' ')}, {"Minus", static_cast<ushort>('-')}, {"Equal", static_cast<ushort>('=')}, {"F1", kbF1}, {"F2", kbF2}, {"F3", kbF3}, {"F4", kbF4}, {"F5", kbF5}, {"F6", kbF6}, {"F7", kbF7}, {"F8", kbF8}, {"F9", kbF9}, {"F10", kbF10}, {"F11", kbF11}, {"F12", kbF12}};
	TKey pressed(keyCode, controlKeyState);

	if (keyCode == kbNoKey && (controlKeyState & kbCtrlShift) != 0 && (controlKeyState & (kbAltShift | kbSuperShift | kbPaste)) == 0) {
		outToken = "<CtrlSpace>";
		mrLogMessage("KEYDBG record raw Ctrl-Space normalized to <CtrlSpace>");
		return true;
	}

	for (const ComboSpec &combo : combos)
		for (const NamedKeySpec &entry : named)
			if (pressed == TKey(entry.code, combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken += entry.token;
				outToken += ">";
				return true;
			}

	for (const ComboSpec &combo : combos) {
		for (char c = 'A'; c <= 'Z'; ++c)
			if (pressed == TKey(static_cast<ushort>(c), combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken.push_back(c);
				outToken += ">";
				return true;
			}
		for (char c = '0'; c <= '9'; ++c)
			if (pressed == TKey(static_cast<ushort>(c), combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken.push_back(c);
				outToken += ">";
				return true;
			}
	}

	if (keyCode >= 1 && keyCode <= 26 && (controlKeyState & (kbAltShift | kbPaste)) == 0) {
		outToken = "<Ctrl";
		outToken.push_back(static_cast<char>('A' + keyCode - 1));
		outToken += ">";
		return true;
	}

	if (keyCode != kbNoKey && keyCode < 256 && std::isprint(static_cast<unsigned char>(keyCode)) != 0) {
		outToken = "<";
		outToken.push_back(static_cast<char>(keyCode));
		outToken += ">";
		return true;
	}
	return false;
}

bool pathIsRegularFile(std::string_view path) {
	struct stat st;

	if (path.empty()) return false;
	return ::stat(std::string(path).c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool confirmOverwriteForPath(const char *primaryLabel, const char *headline, const std::string &targetPath) {
	if (!pathIsRegularFile(targetPath)) return true;
	return mr::dialogs::showUnsavedChangesDialog(primaryLabel, headline, targetPath.c_str()) == mr::dialogs::UnsavedChangesChoice::Save;
}

bool validateMacroSource(std::string_view source, std::string &errorText) {
	size_t bytecodeSize = 0;
	std::string sourceText(source);
	unsigned char *bytecode = compile_macro_code(sourceText.c_str(), &bytecodeSize);

	if (bytecode == nullptr) {
		const char *err = get_last_compile_error();
		errorText = (err != nullptr && *err != '\0') ? err : "Compilation failed.";
		return false;
	}
	std::free(bytecode);
	errorText.clear();
	return true;
}

[[nodiscard]] bool hasVmErrorLineSince(const std::vector<std::string> &lines, std::size_t start, std::string &outError) {
	static constexpr std::string_view prefix = "VM Error:";
	for (std::size_t i = start; i < lines.size(); ++i)
		if (lines[i].compare(0, prefix.size(), prefix.data(), prefix.size()) == 0) {
			outError = lines[i];
			return true;
		}
	return false;
}

class StartupSettingsModeGuard {
  public:
	StartupSettingsModeGuard() noexcept : previous(mrvmIsStartupSettingsMode()) {
		mrvmSetStartupSettingsMode(true);
	}

	~StartupSettingsModeGuard() {
		mrvmSetStartupSettingsMode(previous);
	}

  private:
	bool previous;
};

// This is the final startup apply path. The verified, canonicalized and compiled
// settings.mrmac source is executed by the VM in startup mode, and the in-memory
// settings state becomes authoritative only after this step succeeds.
bool applySettingsSourceViaVm(const std::string &settingsPath, const std::string &source, std::string *errorMessage) {
	size_t bytecodeSize = 0;
	unsigned char *bytecode = nullptr;
	int macroCount = 0;
	VirtualMachine vm;
	MRSetupPaths resetPaths;
	std::string normalizedSettingsPath = normalizeConfiguredPathInput(settingsPath);
	std::string compileError;
	std::string vmError;

	if (!resetConfiguredSettingsModel(normalizedSettingsPath, resetPaths, &vmError)) {
		if (errorMessage != nullptr) *errorMessage = "Settings VM preload reset failed: " + vmError;
		return false;
	}
	bytecode = compile_macro_code(source.c_str(), &bytecodeSize);
	if (bytecode == nullptr) {
		const char *err = get_last_compile_error();
		compileError = (err != nullptr && *err != '\0') ? err : "Compilation failed.";
		if (errorMessage != nullptr) *errorMessage = "Settings load failed (compile): " + compileError;
		return false;
	}
	macroCount = get_compiled_macro_count();
	if (macroCount <= 0) {
		std::free(bytecode);
		if (errorMessage != nullptr) *errorMessage = "Settings load failed: no macros found.";
		return false;
	}
	{
		StartupSettingsModeGuard startupSettingsMode;
		for (int i = 0; i < macroCount; ++i) {
			int entry = get_compiled_macro_entry(i);
			const char *macroName = get_compiled_macro_name(i);
			std::size_t logStart = vm.log.size();

			if (entry < 0 || static_cast<size_t>(entry) >= bytecodeSize) {
				std::free(bytecode);
				if (errorMessage != nullptr) *errorMessage = "Settings load failed: invalid macro entry.";
				return false;
			}
			vm.executeAt(bytecode, bytecodeSize, static_cast<size_t>(entry), std::string(), macroName != nullptr ? macroName : std::string(), i == 0, true);
			if (hasVmErrorLineSince(vm.log, logStart, vmError)) {
				std::free(bytecode);
				if (errorMessage != nullptr) *errorMessage = "Settings load failed (runtime): " + vmError;
				return false;
			}
			if (!mrvmFlushPendingStartupKeymapBatch(&vmError)) {
				std::free(bytecode);
				if (errorMessage != nullptr) *errorMessage = "Settings load failed (keymap batch): " + (vmError.empty() ? std::string("invalid keymap batch.") : vmError);
				return false;
			}
		}
	}
	std::free(bytecode);
	clearConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool applySettingsSource(const std::string &source, std::string *errorMessage) {
	MRSettingsLoadReport report;
	std::string settingsPath = configuredSettingsMacroFilePath();
	std::string canonicalSource;

	if (settingsPath.empty()) settingsPath = defaultSettingsMacroFilePath();
	if (!buildCanonicalSettingsSource(settingsPath, source, &report, canonicalSource, errorMessage)) return false;
	return applySettingsSourceViaVm(settingsPath, canonicalSource, errorMessage);
}

ushort mrEditorDialog(int dialog, ...) {
	va_list arg;
	ushort result = cmCancel;

	switch (dialog) {
		case edOutOfMemory:
			postAppError("Out of memory.");
			return cmOK;
		case edReadError: {
			const char *path = nullptr;
			va_start(arg, dialog);
			path = va_arg(arg, const char *);
			va_end(arg);
			postAppError(std::string("Error reading file: ") + ((path != nullptr && *path != '\0') ? path : "<unknown>"));
			return cmOK;
		}
		case edWriteError: {
			const char *path = nullptr;
			va_start(arg, dialog);
			path = va_arg(arg, const char *);
			va_end(arg);
			postAppError(std::string("Error writing file: ") + ((path != nullptr && *path != '\0') ? path : "<unknown>"));
			return cmOK;
		}
		case edCreateError: {
			const char *path = nullptr;
			va_start(arg, dialog);
			path = va_arg(arg, const char *);
			va_end(arg);
			postAppError(std::string("Error creating file: ") + ((path != nullptr && *path != '\0') ? path : "<unknown>"));
			return cmOK;
		}
		case edSaveModify: {
			const char *path = nullptr;
			va_start(arg, dialog);
			path = va_arg(arg, const char *);
			va_end(arg);
			return messageBox(mfInformation | mfYesNoCancel, "File modified. Save changes to:\n%s", (path != nullptr && *path != '\0') ? path : "<unnamed>");
		}
		case edSaveUntitled:
			return messageBox(mfInformation | mfYesNoCancel, "Save untitled file?");
		case edSaveAs: {
			char *target = nullptr;
			va_start(arg, dialog);
			target = va_arg(arg, char *);
			va_end(arg);
			if (target == nullptr) return cmCancel;
			std::string suggestedTarget(target);
			mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::EditorSaveAs, target, MAXPATH, "*.*");
			mr::dialogs::suggestFileDialogName(target, MAXPATH, suggestedTarget);
			result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::EditorSaveAs, "*.*", "SAVE FILE AS", "~N~ame", fdOKButton, target);
			return result;
		}
		default:
			return cmCancel;
	}
}

// This function orchestrates bootstrap staging, canonicalization and the final
// VM apply. The runtime settings state is authoritative only after the final
// applySettingsSourceViaVm call completes successfully.
bool loadStartupSettingsMacro(const std::string &overridePath, std::string *errorMessage) {
	std::string settingsPath = overridePath.empty() ? defaultSettingsMacroFilePath() : overridePath;
	std::string source;
	MRSettingsLoadReport report;
	std::string canonicalSource;
	const auto settingsStartedAt = std::chrono::steady_clock::now();
	auto phaseStartedAt = settingsStartedAt;
	auto logSettingsBootstrapPhase = [&phaseStartedAt](const char *phase) {
		const auto now = std::chrono::steady_clock::now();
		std::ostringstream line;

		line << "Bootstrap settings phase " << phase << " took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(now - phaseStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
		phaseStartedAt = now;
	};

	if (settingsPath.empty()) {
		if (errorMessage != nullptr) *errorMessage = "Settings path is empty.";
		return false;
	}
	if (!ensureSettingsMacroFileExists(settingsPath, errorMessage)) {
		logSettingsBootstrapPhase("ensure_file");
		mrLogMessage(errorMessage != nullptr ? errorMessage->c_str() : "Settings bootstrap failed (create defaults).");
		return false;
	}
	logSettingsBootstrapPhase("ensure_file");
	if (!readTextFile(settingsPath, source)) {
		source.clear();
	}
	logSettingsBootstrapPhase("read_file");
	if (!prepareStartupSettingsSource(settingsPath, source, &report, canonicalSource, errorMessage)) {
		logSettingsBootstrapPhase("prepare_canonical_source");
		mrLogMessage(errorMessage != nullptr ? errorMessage->c_str() : "Settings canonicalization failed.");
		return false;
	}
	logSettingsBootstrapPhase("prepare_canonical_source");
	if (!applySettingsSourceViaVm(settingsPath, canonicalSource, errorMessage)) {
		logSettingsBootstrapPhase("vm_apply");
		mrLogMessage((errorMessage != nullptr && !errorMessage->empty()) ? errorMessage->c_str() : "Settings VM apply failed.");
		return false;
	}
	logSettingsBootstrapPhase("vm_apply");

	mrLogMessage(("Bootstrap settings loaded path=" + settingsPath + " macropath=" + defaultMacroDirectoryPath()).c_str());
	logSettingsBootstrapPhase("post_apply_log");
	{
		std::ostringstream line;
		line << "Bootstrap settings total took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - settingsStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

struct StartupLoadRequest {
	bool recursive;
	std::vector<std::string> specs;

	StartupLoadRequest() : recursive(false), specs() {
	}
};

struct StartupAutomationRequest {
	std::vector<std::string> macroFiles;
	bool exitAfterRunMacro;

	StartupAutomationRequest() : macroFiles(), exitAfterRunMacro(false) {
	}
};

bool parseRunMacroOptionValue(const std::string &arg, std::string &value) {
	static const std::string prefix = "--run-macro=";

	if (arg.rfind(prefix, 0) != 0) return false;
	value = arg.substr(prefix.size());
	return true;
}

bool hasGlobWildcard(std::string_view pathSpec) {
	return pathSpec.find_first_of("*?[") != std::string::npos;
}

bool isReadableRegularFile(const std::filesystem::path &path) {
	std::error_code ec;
	std::string pathString = path.string();

	if (pathString.empty()) return false;
	if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
	return ::access(pathString.c_str(), R_OK) == 0;
}

std::string normalizePathForLoad(const std::filesystem::path &path) {
	std::error_code ec;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);

	if (ec || normalized.empty()) normalized = path.lexically_normal();
	return normalized.string();
}

void appendUniqueFilePath(const std::filesystem::path &path, std::vector<std::string> &paths, std::set<std::string> &seen) {
	std::string normalized;

	if (!isReadableRegularFile(path)) return;
	normalized = normalizePathForLoad(path);
	if (normalized.empty()) return;
	if (seen.insert(normalized).second) paths.push_back(normalized);
}

void appendGlobMatchesFlat(const std::string &pattern, std::vector<std::string> &paths, std::set<std::string> &seen) {
	glob_t globResult{};
	int result = ::glob(pattern.c_str(), 0, nullptr, &globResult);

	if (result == 0) {
		for (std::size_t i = 0; i < globResult.gl_pathc; ++i)
			appendUniqueFilePath(std::filesystem::path(globResult.gl_pathv[i]), paths, seen);
	} else if (result != GLOB_NOMATCH) {
		std::string line = "Startup glob failed for pattern: ";
		line += pattern;
		mrLogMessage(line.c_str());
	}
	globfree(&globResult);
}

std::filesystem::path recursiveRootForPattern(const std::string &pattern) {
	std::size_t wildcardPos = pattern.find_first_of("*?[");
	std::size_t slashPos;

	if (wildcardPos == std::string::npos) return std::filesystem::path(pattern);
	slashPos = pattern.rfind('/', wildcardPos);
	if (slashPos == std::string::npos) return std::filesystem::path(".");
	if (slashPos == 0) return std::filesystem::path("/");
	return std::filesystem::path(pattern.substr(0, slashPos));
}

void appendRecursiveGlobMatches(const std::string &pattern, std::vector<std::string> &paths, std::set<std::string> &seen) {
	std::filesystem::path rootPath = recursiveRootForPattern(pattern);
	std::size_t wildcardPos = pattern.find_first_of("*?[");
	std::size_t rootSlashPos = pattern.rfind('/', wildcardPos);
	std::string patternSuffix = rootSlashPos == std::string::npos ? pattern : pattern.substr(rootSlashPos + 1);
	const bool matchBaseName = patternSuffix.find('/') == std::string::npos;
	std::error_code ec;
	auto matchesPattern = [&](const std::filesystem::path &candidatePath, const std::filesystem::path &basePath) -> bool {
		std::string candidate;
		if (matchBaseName) candidate = candidatePath.filename().string();
		else {
			std::error_code relEc;
			std::filesystem::path relativePath = std::filesystem::relative(candidatePath, basePath, relEc);
			candidate = relEc ? candidatePath.lexically_normal().string() : relativePath.lexically_normal().string();
		}
		if (candidate.empty()) return false;
		return fnmatch(patternSuffix.c_str(), candidate.c_str(), 0) == 0;
	};

	if (rootPath.empty()) rootPath = ".";
	if (!std::filesystem::exists(rootPath, ec) || ec) return;
	if (!std::filesystem::is_directory(rootPath, ec) || ec) {
		if (matchesPattern(rootPath, rootPath.parent_path())) appendUniqueFilePath(rootPath, paths, seen);
		return;
	}

	std::filesystem::recursive_directory_iterator it(rootPath, std::filesystem::directory_options::skip_permission_denied, ec);
	std::filesystem::recursive_directory_iterator end;
	for (; !ec && it != end; it.increment(ec)) {
		if (!it->is_regular_file(ec) || ec) {
			ec.clear();
			continue;
		}
		std::filesystem::path candidatePath = it->path().lexically_normal();
		if (matchesPattern(candidatePath, rootPath)) appendUniqueFilePath(candidatePath, paths, seen);
	}
}

void appendRecursivePathFiles(const std::filesystem::path &path, std::vector<std::string> &paths, std::set<std::string> &seen) {
	std::error_code ec;

	if (isReadableRegularFile(path)) {
		appendUniqueFilePath(path, paths, seen);
		return;
	}
	if (!std::filesystem::is_directory(path, ec) || ec) return;
	std::filesystem::recursive_directory_iterator it(path, std::filesystem::directory_options::skip_permission_denied, ec);
	std::filesystem::recursive_directory_iterator end;
	for (; !ec && it != end; it.increment(ec)) {
		if (!it->is_regular_file(ec) || ec) {
			ec.clear();
			continue;
		}
		appendUniqueFilePath(it->path(), paths, seen);
	}
}

StartupLoadRequest parseStartupLoadRequest() {
	StartupLoadRequest request;
	std::vector<std::string> args = mrvmProcessArguments();
	bool skipNext = false;

	for (const std::string &arg : args) {
		std::string ignored;
		if (skipNext) {
			skipNext = false;
			continue;
		}
		if (arg == "--load-recursive" || arg == "-lr") {
			request.recursive = true;
			continue;
		}
		if (arg == "--run-macro") {
			skipNext = true;
			continue;
		}
		if (parseRunMacroOptionValue(arg, ignored)) continue;
		if (arg == "--exit-after-run-macro") continue;
		if (!arg.empty()) request.specs.push_back(arg);
	}
	return request;
}

StartupAutomationRequest parseStartupAutomationRequest() {
	StartupAutomationRequest request;
	std::vector<std::string> args = mrvmProcessArguments();
	bool expectRunMacroPath = false;

	for (const std::string &arg : args) {
		std::string value;

		if (expectRunMacroPath) {
			if (!arg.empty()) request.macroFiles.push_back(arg);
			expectRunMacroPath = false;
			continue;
		}
		if (arg == "--run-macro") {
			expectRunMacroPath = true;
			continue;
		}
		if (parseRunMacroOptionValue(arg, value)) {
			if (!value.empty()) request.macroFiles.push_back(value);
			continue;
		}
		if (arg == "--exit-after-run-macro") {
			request.exitAfterRunMacro = true;
			continue;
		}
	}
	if (expectRunMacroPath) mrLogMessage("Startup automation ignored --run-macro without a macro file.");
	return request;
}

bool runStartupAutomationFromCommandLine() {
	StartupAutomationRequest request = parseStartupAutomationRequest();

	for (const std::string &macroFile : request.macroFiles) {
		std::string errorText;
		if (runMacroFileByPathOnUiThread(macroFile.c_str(), &errorText, false)) {
			mrLogMessage(("Startup automation ran macro: " + macroFile).c_str());
			continue;
		}
		if (errorText.empty()) errorText = "Macro execution failed.";
		mrLogMessage(("Startup automation macro failed: " + macroFile + ": " + errorText).c_str());
		mr::messageline::postAutoTimed(mr::messageline::Owner::MacroMessage, "startup macro failed: " + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
	}
	return request.exitAfterRunMacro && !request.macroFiles.empty();
}

std::vector<std::string> collectStartupFilesFromRequest(const StartupLoadRequest &request) {
	std::vector<std::string> paths;
	std::set<std::string> seen;

	for (const std::string &specRaw : request.specs) {
		std::string spec = expandUserPath(specRaw);
		std::filesystem::path specPath(spec);

		if (spec.empty()) continue;
		if (request.recursive) {
			if (hasGlobWildcard(spec)) appendRecursiveGlobMatches(spec, paths, seen);
			else
				appendRecursivePathFiles(specPath, paths, seen);
			continue;
		}
		if (hasGlobWildcard(spec)) {
			appendGlobMatchesFlat(spec, paths, seen);
			continue;
		}
		appendUniqueFilePath(specPath, paths, seen);
	}
	return paths;
}

std::size_t loadStartupFilesFromCommandLine() {
	StartupLoadRequest request = parseStartupLoadRequest();
	std::vector<std::string> files;
	std::size_t loadedCount = 0;
	MREditWindow *lastLoadedWindow = nullptr;

	if (request.specs.empty()) return 0;

	files = collectStartupFilesFromRequest(request);
	if (files.empty()) {
		mrLogMessage("No readable startup files matched command-line arguments.");
		return 0;
	}
	for (const std::string &file : files) {
		MREditWindow *win = createEditorWindow(file.c_str());
		if (win == nullptr) {
			mrLogMessage("Startup load aborted: unable to create editor window.");
			break;
		}
		if (!loadResolvedFileIntoWindow(win, file, "Startup load")) {
			message(win, evCommand, cmClose, nullptr);
			continue;
		}
		lastLoadedWindow = win;
		++loadedCount;
	}
	if (lastLoadedWindow != nullptr) static_cast<void>(mrActivateEditWindow(lastLoadedWindow));
	return loadedCount;
}

std::string buildTopRightCursorStatus() {
	MREditWindow *win = currentEditWindow();
	if (win == nullptr || win->getEditor() == nullptr) return std::string();
	if (isEmptyUntitledEditableWindow(win)) return std::string();

	std::string format = configuredCursorPositionMarker();
	std::string out;
	const std::string rowText = std::to_string(win->cursorLineNumber());
	const std::string colText = std::to_string(win->cursorColumnNumber());

	if (format.empty()) format = "R:C";
	out.reserve(format.size() + rowText.size() + colText.size());
	for (char ch : format) {
		if (ch == 'R') out += rowText;
		else if (ch == 'C')
			out += colText;
		else
			out.push_back(ch);
	}
	return out;
}

MRMenuBar::MarqueeKind mapMessageNoticeKind(mr::messageline::Kind kind) {
	switch (kind) {
		case mr::messageline::Kind::Success:
			return MRMenuBar::MarqueeKind::Success;
		case mr::messageline::Kind::Warning:
			return MRMenuBar::MarqueeKind::Warning;
		case mr::messageline::Kind::Error:
			return MRMenuBar::MarqueeKind::Error;
		case mr::messageline::Kind::Info:
		default:
			return MRMenuBar::MarqueeKind::Info;
	}
}

std::vector<MRMenuBar::MarqueeSegment> mapMessageNoticeSegments(const std::vector<mr::messageline::VisibleMessage::Segment> &segments) {
	std::vector<MRMenuBar::MarqueeSegment> mapped;

	mapped.reserve(segments.size());
	for (const mr::messageline::VisibleMessage::Segment &segment : segments)
		mapped.push_back(MRMenuBar::MarqueeSegment{segment.text, mapMessageNoticeKind(segment.kind)});
	return mapped;
}

bool isHeroVisibleMessage(const mr::messageline::VisibleMessage &visible) {
	mr::messageline::VisibleMessage ownerMessage;
	if (mr::messageline::currentOwnerMessage(mr::messageline::Owner::HeroEvent, ownerMessage) && ownerMessage.kind == visible.kind && ownerMessage.text == visible.text) return true;
	if (mr::messageline::currentOwnerMessage(mr::messageline::Owner::HeroEventFollowup, ownerMessage) && ownerMessage.kind == visible.kind && ownerMessage.text == visible.text) return true;
	if (mr::messageline::currentOwnerMessage(mr::messageline::Owner::MacroBrain, ownerMessage) && ownerMessage.kind == visible.kind && ownerMessage.text == visible.text) return true;
	return false;
}

const TPalette &extendedAppBasePalette() {
	static const TPalette palette = []() -> TPalette {
		static const int kBaseSlots = 135;
		static const int kTotalSlots = kMrPaletteMax;
		static const char cp[] = cpAppColor;
		TColorAttr data[kTotalSlots];
		int i = 0;

		for (i = 0; i < kBaseSlots; ++i)
			data[i] = static_cast<unsigned char>(cp[i]);
		// Dedicated editor-only accent slots (avoid window frame/scrollbar side effects).
		data[kMrPaletteCurrentLine - 1] = data[10 - 1];
		data[kMrPaletteCurrentLineInBlock - 1] = data[12 - 1];
		data[kMrPaletteChangedText - 1] = data[14 - 1];
		data[kMrPaletteMessageError - 1] = data[42 - 1];
		data[kMrPaletteMessage - 1] = data[43 - 1];
		data[kMrPaletteMessageWarning - 1] = data[44 - 1];
		data[kMrPaletteMessageHero - 1] = data[43 - 1];
		data[kMrPaletteCursorPositionMarker - 1] = data[3 - 1];
		data[kMrPaletteLineNumbers - 1] = data[9 - 1];
		data[kMrPaletteEofMarker - 1] = data[14 - 1];
		data[kMrPaletteDialogInactiveElements - 1] = data[62 - 1];
		data[kMrPaletteMiniMapNormal - 1] = data[13 - 1];
		data[kMrPaletteMiniMapViewport - 1] = data[11 - 1];
		data[kMrPaletteMiniMapChanged - 1] = data[14 - 1];
		data[kMrPaletteMiniMapFindMarker - 1] = data[5 - 1];
		data[kMrPaletteMiniMapErrorMarker - 1] = data[42 - 1];
		data[kMrPaletteCodeFolding - 1] = data[9 - 1];
		data[kMrPaletteStatusLine - 1] = data[2 - 1];
		data[kMrPaletteStatusLineBold - 1] = data[3 - 1];
		data[kMrPaletteStatusLineFunctionDescription - 1] = data[4 - 1];
		data[kMrPaletteStatusLineFunctionKey - 1] = data[5 - 1];
		data[kMrPaletteDesktop - 1] = 0x90;
		data[kMrPaletteVirtualDesktopMarker - 1] = 0x9F;
		return TPalette(data, static_cast<ushort>(kTotalSlots));
	}();
	return palette;
}
} // namespace

TMenuBar *MREditorApp::initMRMenuBar(TRect r) {
	return createMRMenuBar(r);
}

TStatusLine *MREditorApp::initMRStatusLine(TRect r) {
	r.a.y = r.b.y - 1;
	return new MRStatusLine(r, *new TStatusDef(0, 0xFFFF) + *new TStatusItem("~F1~ Help", kbF1, cmMrHelpContents) + *new TStatusItem("~F10~ Menu", kbF10, cmMenu) + *new TStatusItem("~Alt-F10~ Rec", kbAltF10, cmMrMacroToggleRecording) + *new TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit));
}

TDeskTop *MREditorApp::initMRDeskTop(TRect r) {
	r.a.y++;
	r.b.y--;
	return new MRDeskTop(r);
}

MREditorApp::MREditorApp() : TProgInit(&MREditorApp::initMRStatusLine, &MREditorApp::initMRMenuBar, &MREditorApp::initMRDeskTop), exitPrepared(false), keystrokeRecording(false), recordingMarkerVisible(false), macroBrainMarkerVisible(false), recordedMacroCounter(0), recordingBlinkToggleAt(std::chrono::steady_clock::now() + kRecordingBlinkInterval), macroBrainBlinkToggleAt(std::chrono::steady_clock::now() + kRecordingBlinkInterval), indexedMacroWarmupActive(false), indexedMacroWarmupLoadedFiles(0), performancePanelVisible(false), performancePanel(nullptr), fullscreenHint(nullptr), performancePanelFrame(0), performancePanelRefreshAt(std::chrono::steady_clock::now()), fullscreenHintVisibleUntil(std::chrono::steady_clock::time_point::min()), startupQuitPending(false), fullscreenPresentationActive(false), fullscreenMenuBarTransientVisible(false), fullscreenWindow(nullptr), fullscreenRestoreBounds(0, 0, 0, 0) {
	const auto startupStartedAt = std::chrono::steady_clock::now();
	auto phaseStartedAt = startupStartedAt;
	auto logStartupPhase = [&phaseStartedAt](const char *phase) {
		const auto now = std::chrono::steady_clock::now();
		std::ostringstream line;

		line << "Bootstrap phase " << phase << " took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(now - phaseStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
		phaseStartedAt = now;
	};
	TEditor::editorDialog = mrEditorDialog;
	mr::coprocessor::globalCoprocessor().setResultHandler(handleCoprocessorResult);
	initializePerformancePanel();
	initializeFullscreenHint();
	loadStartupSettingsMacro(std::string(), nullptr);
	logStartupPhase("settings_bootstrap");
	applyConfiguredDisplayLayout();
	logStartupPhase("display_layout_initial");
	bootstrapIndexedMacroBindings();
	logStartupPhase("autoexec_macros");
	static_cast<void>(loadStartupFilesFromCommandLine());
	logStartupPhase("startup_files");
	startupQuitPending = runStartupAutomationFromCommandLine();
	logStartupPhase("startup_automation");
	applyConfiguredDisplayLayout();
	logStartupPhase("display_layout_final");
	static_cast<void>(mrEnsureLogWindow(false));
	logStartupPhase("log_window");
	syncRecordingUiState();
	logStartupPhase("recording_ui");
	if (auto *mrMenuBar = dynamic_cast<MRMenuBar *>(menuBar)) {
		mrMenuBar->setPersistentBlocksMenuState(configuredPersistentBlocksSetting());
		if (MREditWindow *win = currentEditWindow(); win != nullptr) mrMenuBar->setInsertModeMenuState(win->insertModeEnabled());
	}
	logStartupPhase("menu_state");

	if (configuredAutoloadWorkspace()) {
		mrLoadWorkspace("");
	}
	logStartupPhase("workspace_autoload");
	mrLogMessage("Editor session started.");
	updateAppCommandState();
	syncFunctionKeyState();
	logStartupPhase("command_state");
	{
		std::ostringstream line;

		line << "Bootstrap total took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startupStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
	}
}

bool MREditorApp::quitPrepared() const noexcept {
	return exitPrepared;
}

MREditorApp::~MREditorApp() {
	const auto prepareStartedAt = std::chrono::steady_clock::now();
	prepareForQuit();
	{
		std::ostringstream line;
		line << "App destructor phase prepare_for_quit took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - prepareStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
	}
	const auto shutdownStartedAt = std::chrono::steady_clock::now();
	mr::coprocessor::globalCoprocessor().shutdown(true);
	{
		std::ostringstream line;
		line << "App destructor phase coprocessor_shutdown took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - shutdownStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
	}
}

void MREditorApp::applyConfiguredWindowFramePolicy() {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();

	for (auto win : windows) {
		if (win == nullptr) continue;
		win->flags |= (wfMove | wfGrow | wfZoom | wfClose);
		if (win->fullscreenPresentation()) continue;
		if (win->frame != nullptr) win->frame->drawView();
	}
}

void MREditorApp::initializePerformancePanel() {
	if (performancePanel != nullptr) return;

	TRect appRect = getExtent();
	TRect panelRect(0, 1, appRect.b.x - appRect.a.x, 1 + MRPerformancePanel::kPreferredHeight);

	performancePanel = new MRPerformancePanel(panelRect);
	insert(performancePanel);
	performancePanel->hide();
}

void MREditorApp::initializeFullscreenHint() {
	if (fullscreenHint != nullptr) return;

	TRect appRect = getExtent();
	const int appWidth = std::max(1, static_cast<int>(appRect.b.x - appRect.a.x));
	const int appHeight = std::max(1, static_cast<int>(appRect.b.y - appRect.a.y));
	const int hintWidth = std::max(1, std::min(fullscreenHintTextWidth(), appWidth));
	const int hintLeft = std::max(0, (appWidth - hintWidth) / 2);
	const int hintTop = appHeight - 1;
	TRect hintRect(hintLeft, hintTop, hintLeft + hintWidth, hintTop + 1);

	fullscreenHint = new MRFullscreenHintView(hintRect);
	insert(fullscreenHint);
	fullscreenHint->hide();
}

void MREditorApp::togglePerformancePanel() {
	performancePanelVisible = !performancePanelVisible;
	applyConfiguredDisplayLayout();
	updatePerformancePanel();
}

void MREditorApp::updatePerformancePanel() {
	static constexpr std::chrono::milliseconds kPanelRefreshInterval(120);
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	if (!performancePanelVisible || performancePanel == nullptr) return;
	if (now < performancePanelRefreshAt) return;
	performancePanelRefreshAt = now + kPanelRefreshInterval;
	performancePanel->setAnimationFrame(++performancePanelFrame);
}

void MREditorApp::updateFullscreenHint() {
	if (fullscreenHint == nullptr) initializeFullscreenHint();
	if (fullscreenHint == nullptr) return;

	const auto now = std::chrono::steady_clock::now();
	if (fullscreenPresentationActive && fullscreenWindow != nullptr && !fullscreenTargetStillOpen()) fullscreenWindow = nullptr;
	const bool fullscreenDesktopEmpty = fullscreenPresentationActive && fullscreenWindow == nullptr && currentEditWindow() == nullptr;
	const bool fullscreenHintTimed = fullscreenPresentationActive && now < fullscreenHintVisibleUntil;
	const bool hintVisible = fullscreenPresentationActive && (fullscreenDesktopEmpty || fullscreenHintTimed);

	if (!hintVisible) {
		fullscreenHint->hide();
		return;
	}

	TRect appRect = getExtent();
	const int appWidth = static_cast<int>(appRect.b.x - appRect.a.x);
	const int appHeight = static_cast<int>(appRect.b.y - appRect.a.y);
	if (appWidth <= 0 || appHeight <= 0) {
		fullscreenHint->hide();
		return;
	}

	const int hintWidth = std::max(1, std::min(fullscreenHintTextWidth(), appWidth));
	const int hintLeft = std::max(0, (appWidth - hintWidth) / 2);
	const int hintTop = appHeight - 1;
	TRect hintRect(hintLeft, hintTop, hintLeft + hintWidth, hintTop + 1);

	fullscreenHint->locate(hintRect);
	fullscreenHint->show();
	fullscreenHint->drawView();
}

bool MREditorApp::fullscreenTargetStillOpen() const {
	if (fullscreenWindow == nullptr) return false;
	for (MREditWindow *window : allEditWindowsInZOrder())
		if (window == fullscreenWindow) return true;
	return false;
}

bool MREditorApp::enterFullscreenPresentation() {
	MREditWindow *window = currentEditWindow();

	if (fullscreenPresentationActive) return true;
	fullscreenWindow = window;
	if (window != nullptr) {
		if (window->isMinimized()) window->restoreWindow();
		fullscreenRestoreBounds = window->getBounds();
	}
	fullscreenPresentationActive = true;
	fullscreenMenuBarTransientVisible = false;
	fullscreenHintVisibleUntil = std::chrono::steady_clock::now() + kFullscreenHintDuration;
	if (window != nullptr) window->setFullscreenPresentation(true);
	applyConfiguredDisplayLayout();
	if (window != nullptr) window->select();
	mrvmUiInvalidateScreenBase();
	return true;
}

void MREditorApp::leaveFullscreenPresentation() {
	MREditWindow *window = fullscreenTargetStillOpen() ? fullscreenWindow : nullptr;
	TRect restoreBounds = fullscreenRestoreBounds;

	if (!fullscreenPresentationActive) return;
	fullscreenPresentationActive = false;
	fullscreenMenuBarTransientVisible = false;
	fullscreenHintVisibleUntil = std::chrono::steady_clock::time_point::min();
	fullscreenWindow = nullptr;
	if (window != nullptr) window->setFullscreenPresentation(false);
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEvent, "Fullscreen ended - welcome back!", mr::messageline::Kind::Info, mr::messageline::kPriorityHigh);
	applyConfiguredDisplayLayout();
	if (window != nullptr) {
		window->locate(restoreBounds);
		window->select();
		window->drawView();
	}
	mrvmUiInvalidateScreenBase();
}

void MREditorApp::toggleFullscreenPresentation() {
	if (fullscreenPresentationActive) leaveFullscreenPresentation();
	else
		static_cast<void>(enterFullscreenPresentation());
}

void MREditorApp::syncFunctionKeyState() {
	const bool startupActive = currentEditWindow() == nullptr;
	const bool editorActive = !startupActive && editorFunctionKeyContextActive();

	if (auto *mrStatus = dynamic_cast<MRStatusLine *>(statusLine)) {
		if (startupActive) {
			mrStatus->setContextFunctionKeyLabels(startupFunctionKeyLabels());
			mrStatus->setContextFunctionKeysActive(true);
		} else if (editorActive) {
			mrStatus->setContextFunctionKeyLabels(editorFunctionKeyLabels());
			mrStatus->setContextFunctionKeysActive(true);
		} else
			mrStatus->setContextFunctionKeysActive(false);
	}
	if (auto *mrMenuBar = dynamic_cast<MRMenuBar *>(menuBar)) {
		mrMenuBar->setStartupFunctionKeysActive(startupActive);
		mrMenuBar->setEditorFunctionKeysActive(editorActive);
	}
}

void MREditorApp::applyConfiguredDisplayLayout() {
	if (fullscreenPresentationActive && !fullscreenTargetStillOpen()) {
		fullscreenWindow = nullptr;
	}
	if (fullscreenPresentationActive && fullscreenWindow == nullptr) {
		if (MREditWindow *window = currentEditWindow(); window != nullptr) {
			if (window->isMinimized()) window->restoreWindow();
			fullscreenWindow = window;
			fullscreenRestoreBounds = window->getBounds();
			window->setFullscreenPresentation(true);
		}
	}
	const bool fullscreenActive = fullscreenPresentationActive;
	const bool fullscreenMenuBarVisible = fullscreenActive && fullscreenMenuBarTransientVisible;
	bool statusVisible = !fullscreenActive;
	TRect appRect = getExtent();
	TRect desktopRect;
	const int appHeight = appRect.b.y - appRect.a.y;
	const int maxPanelHeight = std::max(0, appHeight - 2);
	const int panelHeight = !fullscreenActive && performancePanelVisible ? std::min(MRPerformancePanel::kPreferredHeight, maxPanelHeight) : 0;

	if (menuBar != nullptr) {
		if (fullscreenActive && !fullscreenMenuBarVisible) menuBar->hide();
		else
			menuBar->show();
	}
	if (auto *mrStatus = dynamic_cast<MRStatusLine *>(statusLine)) {
		mrStatus->setShowFunctionKeyLabels(true);
		if (fullscreenActive) mrStatus->hide();
		else
			mrStatus->show();
	}
	syncFunctionKeyState();
	if (performancePanel != nullptr) {
		if (!fullscreenActive && panelHeight > 0) {
			TRect panelRect(0, 1, appRect.b.x - appRect.a.x, 1 + panelHeight);
			performancePanel->locate(panelRect);
			performancePanel->show();
			performancePanel->drawView();
		} else {
			performancePanel->hide();
		}
	}
	desktopRect.a.x = 0;
	desktopRect.b.x = appRect.b.x - appRect.a.x;
	desktopRect.a.y = fullscreenActive ? (fullscreenMenuBarVisible ? 1 : 0) : 1 + panelHeight;
	desktopRect.b.y = appRect.b.y - appRect.a.y - (statusVisible ? 1 : 0);
	if (desktopRect.b.y <= desktopRect.a.y) desktopRect.b.y = desktopRect.a.y + 1;
	if (deskTop != nullptr) deskTop->locate(desktopRect);
	applyConfiguredWindowFramePolicy();
	MRWindowManager::handleDesktopLayoutChange();
	if (fullscreenActive && fullscreenWindow != nullptr && deskTop != nullptr) {
		TRect fullscreenBounds = deskTop->getExtent();
		fullscreenWindow->setFullscreenPresentation(true);
		fullscreenWindow->locate(fullscreenBounds);
		fullscreenWindow->select();
	}
	if (deskTop != nullptr) deskTop->drawView();
	if (menuBar != nullptr) menuBar->drawView();
	if (statusLine != nullptr) statusLine->drawView();
	updateFullscreenHint();
}

void MREditorApp::prepareForQuit() {
	if (exitPrepared) return;

	const auto quitStartedAt = std::chrono::steady_clock::now();
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	std::size_t pendingTaskCount = 0;
	std::string settingsError;
	MRSettingsWriteReport settingsWriteReport;
	{
		std::ostringstream line;
		line << "Quit prepare begin windows=" << windows.size() << ".";
		mrLogMessage(line.str().c_str());
	}
	for (auto *window : windows) {
		if (window == nullptr) continue;
		std::ostringstream line;
		line << "Quit window state #" << window->number << " modified=" << (window->isFileChanged() ? 1 : 0) << " len=" << window->bufferLength() << " add=" << window->addBufferLength()
		     << " pieces=" << window->pieceCount() << " undo=" << window->undoStackDepth() << " redo=" << window->redoStackDepth() << ".";
		mrLogMessage(line.str().c_str());
	}

	const auto snapshotStartedAt = std::chrono::steady_clock::now();
	if (!persistConfiguredSettingsSnapshot(&settingsError, &settingsWriteReport) && !settingsError.empty()) mrLogMessage(("Settings snapshot on exit failed: " + settingsError).c_str());
	else
		mrLogSettingsWriteReport("exit snapshot", settingsWriteReport);
	{
		std::ostringstream line;
		line << "Quit phase settings_snapshot took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - snapshotStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
	}

	exitPrepared = true;
	const auto cancelStartedAt = std::chrono::steady_clock::now();
	for (auto &window : windows)
		if (window != nullptr) pendingTaskCount += window->prepareCoprocessorTasksForShutdown();
	{
		std::ostringstream line;
		line << "Quit phase cancel_tasks took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cancelStartedAt).count() << " pending=" << pendingTaskCount << ".";
		mrLogMessage(line.str().c_str());
	}

	if (pendingTaskCount != 0) {
		std::string line = "Exit requested; cancelling ";
		line += std::to_string(pendingTaskCount);
		line += " running or pending coprocessor task";
		if (pendingTaskCount != 1) line += "s";
		line += ".";
		mrLogMessage(line.c_str());
		const auto pumpStartedAt = std::chrono::steady_clock::now();
		mr::coprocessor::globalCoprocessor().pump(64);
		{
			std::ostringstream pumpLine;
			pumpLine << "Quit phase pump_after_cancel took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - pumpStartedAt).count() << ".";
			mrLogMessage(pumpLine.str().c_str());
		}
	}
	cancelForegroundMacroDelays();
	if (configuredLogHandling() == MRLogHandling::Persist) {
		const std::string logPath = configuredLogFilePath();
		const auto appendStartedAt = std::chrono::steady_clock::now();
		if (!mrAppendLogBufferToFile(logPath, &settingsError)) mrLogMessage(("MR log append on exit failed: " + settingsError).c_str());
		{
			std::ostringstream line;
			line << "Quit phase append_log took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - appendStartedAt).count() << ".";
			mrLogMessage(line.str().c_str());
		}
	}
	{
		std::ostringstream line;
		line << "Quit prepare end total_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - quitStartedAt).count() << ".";
		mrLogMessage(line.str().c_str());
	}
}

bool MREditorApp::isRecorderToggleKey(const TEvent &event) const {
	return event.what == evKeyDown && TKey(event.keyDown) == TKey(kbAltF10);
}

bool MREditorApp::isRecorderToggleCommand(const TEvent &event) const {
	return event.what == evCommand && event.message.command == cmMrMacroToggleRecording;
}

void MREditorApp::redrawActiveMarkerFrame() {
	MREditWindow *window = currentEditWindow();
	if (window == nullptr || window->frame == nullptr || (window->state & sfVisible) == 0) return;
	window->frame->drawView();
}

void MREditorApp::syncRecordingUiState() {
	mrSetKeystrokeRecordingActive(keystrokeRecording);
	mrSetKeystrokeRecordingMarkerVisible(keystrokeRecording && recordingMarkerVisible);
	if (auto *mrStatusLine = dynamic_cast<MRStatusLine *>(statusLine)) mrStatusLine->setRecordingState(keystrokeRecording, recordingMarkerVisible);
	redrawActiveMarkerFrame();
}

void MREditorApp::updateRecordingBlink() {
	std::chrono::steady_clock::time_point now;
	if (!keystrokeRecording) return;

	now = std::chrono::steady_clock::now();
	if (now < recordingBlinkToggleAt) return;

	recordingMarkerVisible = !recordingMarkerVisible;
	recordingBlinkToggleAt = now + kRecordingBlinkInterval;
	mrSetKeystrokeRecordingMarkerVisible(recordingMarkerVisible);
	if (auto *mrStatusLine = dynamic_cast<MRStatusLine *>(statusLine)) mrStatusLine->setRecordingState(keystrokeRecording, recordingMarkerVisible);
	redrawActiveMarkerFrame();
}

void MREditorApp::updateMacroBrainBlink() {
	std::chrono::steady_clock::time_point now;
	if (!mrIsMacroBrainMarkerActive()) return;

	now = std::chrono::steady_clock::now();
	if (now < macroBrainBlinkToggleAt) return;

	macroBrainMarkerVisible = !macroBrainMarkerVisible;
	macroBrainBlinkToggleAt = now + kRecordingBlinkInterval;
	mrSetMacroBrainMarkerVisible(macroBrainMarkerVisible);
	redrawActiveMarkerFrame();
}

void MREditorApp::startKeystrokeRecording() {
	keystrokeRecording = true;
	recordingMarkerVisible = true;
	recordingBlinkToggleAt = std::chrono::steady_clock::now() + kRecordingBlinkInterval;
	recordedKeySequence.clear();
	syncRecordingUiState();
	mr::messageline::postSticky(mr::messageline::Owner::MacroMessage, "recordings started, ALT-F10 ends", mr::messageline::Kind::Warning, mr::messageline::kPriorityHigh);
	mrLogMessage("Keystroke recording started (Alt-F10 to stop).");
}

void MREditorApp::appendRecordedKeyEvent(const TEvent &event) {
	std::string keyToken;
	ushort state;

	if (event.what != evKeyDown) return;
	state = event.keyDown.controlKeyState;

	if ((state & kbPaste) != 0 && event.keyDown.textLength > 0) {
		for (uchar i = 0; i < event.keyDown.textLength; ++i)
			appendEscapedKeyInChar(recordedKeySequence, static_cast<unsigned char>(event.keyDown.text[i]));
		return;
	}
	if (event.keyDown.textLength > 0) {
		for (uchar i = 0; i < event.keyDown.textLength; ++i)
			appendEscapedKeyInChar(recordedKeySequence, static_cast<unsigned char>(event.keyDown.text[i]));
		return;
	}
	if (keyInTokenFromEvent(event.keyDown.keyCode, state, keyToken)) recordedKeySequence += keyToken;
}

bool MREditorApp::captureBindingKeySpec(std::string &keySpec) {
	TMacroBindCaptureDialog *dialog = nullptr;
	ushort modalResult;
	bool captured = false;
	ushort keyCode = kbNoKey;
	ushort controlState = 0;

	keySpec.clear();
	dialog = new TMacroBindCaptureDialog();
	if (dialog == nullptr) return false;
	dialog->finalizeLayout();
	modalResult = deskTop != nullptr ? deskTop->execView(dialog) : cmCancel;
	captured = dialog->hasCaptured();
	keyCode = dialog->keyCode();
	controlState = dialog->controlState();
	TObject::destroy(dialog);

	if (modalResult == cmCancel || !captured) return true;
	if (!keyInTokenFromEvent(keyCode, controlState, keySpec)) {
		postAppError("Unsupported binding key. Use a function key or a Ctrl/Alt/Shift/Super combination.");
		return false;
	}
	return true;
}

void MREditorApp::finalizeKeystrokeRecording() {
	enum {
		SavePathBufferSize = 512
	};
	char savePathBuffer[SavePathBufferSize];
	std::string keySpec;
	std::string savePath;
	std::string macroName;
	std::ostringstream source;
	std::string macroSource;
	std::string validationError;
	std::string sessionPath;
	std::string loadError;
	std::string summary;

	if (recordedKeySequence.empty()) {
		messageBox(mfInformation | mfOKButton, "Keystroke recording is empty.\n\nNothing to bind or save.");
		return;
	}

	if (!captureBindingKeySpec(keySpec)) return;

	std::memset(savePathBuffer, 0, sizeof(savePathBuffer));
	if (inputBox("KEYSTROKE RECORDER", "~S~ave .mrmac (leer=nur Session-Bindung)", savePathBuffer, static_cast<uchar>(sizeof(savePathBuffer) - 1)) != cmCancel) savePath = trimAscii(savePathBuffer);
	if (!savePath.empty()) savePath = ensureMrmacExtension(expandUserPath(savePath));

	macroName = makeRecordedMacroName(++recordedMacroCounter);
	source << "$MACRO " << macroName;
	if (!keySpec.empty()) source << " TO " << keySpec << " FROM EDIT";
	else
		source << " FROM EDIT";
	source << ";\n";
	source << "KEY_IN('" << recordedKeySequence << "');\n";
	source << "END_MACRO;\n";
	macroSource = source.str();

	if (!validateMacroSource(macroSource, validationError)) {
		postAppError("Recorded macro is invalid: " + validationError);
		return;
	}

	if (!savePath.empty()) {
		if (!confirmOverwriteForPath("Overwrite", "Macro file exists. Overwrite?", savePath)) return;
		if (!writeTextFileWithConfiguredSaveOptions(savePath, macroSource)) {
			postAppError("Could not save recorded macro: " + savePath);
			return;
		}
		std::string line = "Saved recorded macro to ";
		line += savePath;
		mrLogMessage(line.c_str());
	}

	if (!keySpec.empty()) {
		if (!savePath.empty()) sessionPath = savePath;
		else {
			sessionPath = configuredTempDirectoryPath() + "/mr_recorded_" + std::to_string(static_cast<long>(::getpid())) + "_" + std::to_string(recordedMacroCounter) + ".mrmac";
			if (!writeTextFileWithConfiguredSaveOptions(sessionPath, macroSource)) {
				postAppError("Could not create session macro file.");
				return;
			}
			recordedSessionMacroFiles.push_back(sessionPath);
		}

		if (!mrvmLoadMacroFile(sessionPath, &loadError)) {
			postAppError("Could not bind recorded macro: " + loadError);
			return;
		}
		{
			std::string line = "Recorded macro bound to ";
			line += keySpec;
			line += " via ";
			line += sessionPath;
			mrLogMessage(line.c_str());
		}
	}

	summary = "Recording finalized.";
	if (!keySpec.empty()) summary += "\nBound key: " + keySpec;
	if (!savePath.empty()) summary += "\nSaved: " + savePath;
	messageBox(mfInformation | mfOKButton, "%s", summary.c_str());
}

void MREditorApp::stopKeystrokeRecording() {
	keystrokeRecording = false;
	recordingMarkerVisible = false;
	syncRecordingUiState();
	mr::messageline::clearOwner(mr::messageline::Owner::MacroMessage);
	mrLogMessage("Keystroke recording stopped.");
	finalizeKeystrokeRecording();
	recordedKeySequence.clear();
}

void MREditorApp::bootstrapIndexedMacroBindings() {
	std::vector<std::string> configuredEntries;
	std::vector<std::string> retainedEntries;
	std::vector<std::string> failedEntries;
	std::string directory = defaultMacroDirectoryPath();
	bool filteredEntries = false;
	std::size_t executedCount = 0;

	clearConfiguredAutoexecMacroDiagnostics();
	configuredAutoexecMacroEntries(configuredEntries);
	indexedMacroWarmupLoadedFiles = 0;
	indexedMacroWarmupActive = false;

	if (configuredEntries.empty()) {
		mrLogMessage("Autoexec bootstrap: no AUTOEXEC_MACRO entries configured.");
		return;
	}

	for (const std::string &fileName : configuredEntries) {
		const std::filesystem::path resolvedPath = std::filesystem::path(directory) / fileName;
		const std::string resolved = resolvedPath.lexically_normal().generic_string();
		std::string errorText;

		if (!std::filesystem::exists(resolvedPath)) {
			filteredEntries = true;
			mrLogMessage(("Autoexec bootstrap removed missing macro: " + fileName).c_str());
			continue;
		}
		if (runMacroFileByPath(resolved.c_str(), &errorText, true)) {
			retainedEntries.push_back(fileName);
			++executedCount;
			continue;
		}

		filteredEntries = true;
		failedEntries.push_back(fileName);
		rememberConfiguredAutoexecMacroDiagnostic(fileName, errorText.empty() ? "Autoexec execution failed." : errorText);
		mrLogMessage(("Autoexec bootstrap failed for " + fileName + ": " + (errorText.empty() ? std::string("unknown error") : errorText)).c_str());
	}

	if (filteredEntries) {
		std::string persistError;

		if (setConfiguredAutoexecMacroEntries(retainedEntries, &persistError) && persistConfiguredSettingsSnapshot(&persistError)) {
			mrLogMessage("Autoexec bootstrap updated AUTOEXEC_MACRO in settings.mrmac.");
		} else {
			setConfiguredAutoexecMacroEntries(configuredEntries, nullptr);
			mrLogMessage(("Autoexec bootstrap could not persist filtered AUTOEXEC_MACRO: " + persistError).c_str());
		}
	}

	{
		std::string line = "Autoexec bootstrap executed ";
		line += std::to_string(executedCount);
		line += " macro";
		if (executedCount != 1) line += "s";
		line += " from ";
		line += std::to_string(configuredEntries.size());
		line += " configured entry";
		if (configuredEntries.size() != 1) line += "s";
		line += ".";
		mrLogMessage(line.c_str());
	}

	if (!failedEntries.empty()) {
		std::ostringstream summary;

		summary << "Failed autoexec macros: ";
		for (std::size_t i = 0; i < failedEntries.size(); ++i) {
			if (i != 0) summary << ", ";
			summary << failedEntries[i];
		}
		mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, summary.str(), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
	}
}

void MREditorApp::warmIndexedMacroBindings() {
}

void MREditorApp::handleEvent(TEvent &event) {
	const ushort originalWhat = event.what;
	traceKeyDebugEvent("app-pre", event);
	traceCalculatorHotkeyEvent("app-pre", event);
	clearTransientSearchSelectionOnUserInput(event);
	if (event.what == evKeyDown && TKey(event.keyDown) == TKey(kbF11)) {
		toggleFullscreenPresentation();
		clearEvent(event);
		return;
	}
	if (fullscreenPresentationActive && event.what == evKeyDown && TKey(event.keyDown) == TKey(kbEsc)) {
		leaveFullscreenPresentation();
		clearEvent(event);
		return;
	}
	if (performancePanelVisible && event.what == evKeyDown && TKey(event.keyDown) == TKey(kbEsc)) {
		togglePerformancePanel();
		clearEvent(event);
		return;
	}
	if (event.what == evCommand && event.message.command == cmMrToggleFullscreen) {
		toggleFullscreenPresentation();
		clearEvent(event);
		return;
	}
	if (isRecorderToggleCommand(event)) {
		if (keystrokeRecording) stopKeystrokeRecording();
		else
			startKeystrokeRecording();
		mrvmUiInvalidateScreenBase();
		clearEvent(event);
		return;
	}
	if (isRecorderToggleKey(event)) {
		if (keystrokeRecording) stopKeystrokeRecording();
		else
			startKeystrokeRecording();
		mrvmUiInvalidateScreenBase();
		clearEvent(event);
		return;
	}
	if (keystrokeRecording && event.what == evKeyDown) appendRecordedKeyEvent(event);
	if (event.what == evKeyDown && (event.keyDown.controlKeyState & kbCtrlShift) != 0 && (event.keyDown.controlKeyState & kbAltShift) != 0 && (event.keyDown.controlKeyState & kbShift) == 0) {
		const TKey pressed(event.keyDown);
		if ((pressed == TKey('Z', kbCtrlShift | kbAltShift) || event.keyDown.keyCode == kbCtrlZ || event.keyDown.keyCode == kbAltZ) && TView::commandEnabled(cmMrEditRedo) && handleMRCommand(cmMrEditRedo)) {
			clearEvent(event);
			return;
		}
	}

	if (event.what == evCommand && event.message.command == cmMrDeferredActivateWindow) {
		mrLogMessage("MREditorApp handling cmMrDeferredActivateWindow");
		static_cast<void>(mrDispatchDeferredWindowActivation());
		mrvmUiInvalidateScreenBase();
		clearEvent(event);
		return;
	}
	if (event.what == evCommand && event.message.command == cmMrEnsureUsableWorkWindow) {
		mrLogMessage("MREditorApp handling cmMrEnsureUsableWorkWindow");
		static_cast<void>(mrEnsureUsableWorkWindow(false));
		mrvmUiInvalidateScreenBase();
		clearEvent(event);
		return;
	}
	if (event.what == evCommand && event.message.command == cmMrHelpPerformancePanel) {
		togglePerformancePanel();
		clearEvent(event);
		return;
	}
	if (event.what == evCommand && (event.message.command == cmMrEditUndo || event.message.command == cmMrEditRedo) && handleMRCommand(event.message.command, event.message.infoPtr)) {
		clearEvent(event);
		return;
	}
	if (handleFileCompareCommand(event)) return;
	if (event.what == evKeyDown && currentEditWindow() == nullptr) {
		std::string executedMacroName;
		if (mrvmRunAssignedMacroForKey(event.keyDown.keyCode, event.keyDown.controlKeyState, executedMacroName, nullptr)) {
			traceCalculatorHotkeyEvent("app-macro-consumed", event);
			clearEvent(event);
			return;
		}
		if (handleStartupFunctionKey(event)) {
			traceCalculatorHotkeyEvent("app-startup-fkey-consumed", event);
			clearEvent(event);
			return;
		}
	}
	if (event.what == evKeyDown && handleFileCompareFunctionKey(event)) {
		traceCalculatorHotkeyEvent("app-file-compare-fkey-consumed", event);
		return;
	}
	if (event.what == evKeyDown && editorFunctionKeyContextActive() && handleEditorFunctionKey(event)) {
		traceCalculatorHotkeyEvent("app-editor-fkey-consumed", event);
		clearEvent(event);
		return;
	}

	if (event.what == evCommand && event.message.command == cmQuit) prepareForQuit();
	if (isBuildCurrentFileDefaultKey(event) && TView::commandEnabled(cmMrOtherBuildCurrentFile) && handleMRCommand(cmMrOtherBuildCurrentFile)) {
		clearEvent(event);
		return;
	}
	const bool fullscreenMenuBarActivation = fullscreenPresentationActive && menuBar != nullptr && ((event.what == evKeyDown && TKey(event.keyDown) == TKey(kbF10)) || (event.what == evCommand && event.message.command == cmMenu));
	if (fullscreenMenuBarActivation) {
		fullscreenMenuBarTransientVisible = true;
		applyConfiguredDisplayLayout();
	}
	TApplication::handleEvent(event);
	traceCalculatorHotkeyEvent("app-post", event);
	if (shouldInvalidateScreenBaseForEvent(originalWhat)) mrvmUiInvalidateScreenBase();

	auto restoreFullscreenMenuLayout = [this, fullscreenMenuBarActivation]() {
		if (!fullscreenMenuBarActivation || !fullscreenPresentationActive) return;
		fullscreenMenuBarTransientVisible = false;
		applyConfiguredDisplayLayout();
	};

	if (event.what != evCommand) {
		restoreFullscreenMenuLayout();
		return;
	}
	if (auto *mrMenuBar = dynamic_cast<MRMenuBar *>(menuBar); mrMenuBar != nullptr && mrMenuBar->handleRuntimeCommand(event.message.command)) {
		clearEvent(event);
		restoreFullscreenMenuLayout();
		return;
	}
	if (handleMRCommand(event.message.command, event.message.infoPtr)) clearEvent(event);
	restoreFullscreenMenuLayout();
}

void MREditorApp::idle() {
	if (startupQuitPending) {
		TEvent quitEvent{};

		startupQuitPending = false;
		quitEvent.what = evCommand;
		quitEvent.message.command = cmQuit;
		putEvent(quitEvent);
	}
	TApplication::idle();
	pumpForegroundMacroDelays();
	updateRecordingBlink();
	updateMacroBrainBlink();
	warmIndexedMacroBindings();
	mr::coprocessor::globalCoprocessor().pumpFor(kCoprocessorIdlePumpBudget);
	pumpDeferredMacroUiPlayback();
	updatePerformancePanel();
	updateFullscreenHint();
	if (auto *mrMenuBar = dynamic_cast<MRMenuBar *>(menuBar)) {
		mr::messageline::VisibleMessage message;
		std::string rightStatus = buildTopRightCursorStatus();
		mrMenuBar->setRightStatus(rightStatus);
		mrMenuBar->setPersistentBlocksMenuState(configuredPersistentBlocksSetting());
		if (MREditWindow *win = currentEditWindow(); win != nullptr) mrMenuBar->setInsertModeMenuState(win->insertModeEnabled());
		else
			mrMenuBar->setInsertModeMenuState(false);
		if (mr::messageline::currentVisibleMessage(message)) {
			MRMenuBar::MarqueeKind marqueeKind = mapMessageNoticeKind(message.kind);
			if (isHeroVisibleMessage(message)) marqueeKind = MRMenuBar::MarqueeKind::Hero;
			if (!message.segments.empty()) mrMenuBar->setAutoMarqueeStatusSegments(mapMessageNoticeSegments(message.segments), marqueeKind);
			else
				mrMenuBar->setAutoMarqueeStatus(message.text, marqueeKind);
		} else
			mrMenuBar->setAutoMarqueeStatus(std::string());
		mrMenuBar->tickMarquee();
	}
	{
		std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
		for (auto *window : windows) {
			if (window == nullptr || window->frame == nullptr) continue;
			if (auto *mrFrame = dynamic_cast<MRFrame *>(window->frame)) mrFrame->tickTaskOverviewAnimation();
		}
	}
	MRWindowManager::handleDesktopLayoutChange();
	updateAppCommandState();
	syncFunctionKeyState();
}

TPalette &MREditorApp::getPalette() const {
	static const TPalette &basePalette = extendedAppBasePalette();
	static TPalette palette = basePalette;
	unsigned char overrideValue = 0;
	int slot = 0;

	// Rebuild from TV default on every call so stale overrides never leak between frames.
	palette = basePalette;

	for (slot = 1; slot <= kMrPaletteMax; ++slot)
		if (configuredColorSlotOverride(static_cast<unsigned char>(slot), overrideValue)) palette[slot] = overrideValue;

	// TVision-wide policy: Dialog scrollbars follow dialog frame color globally.
	// Applies to gray/blue/cyan dialog palette blocks, no per-view exceptions.
	auto syncDialogScrollbarsToFrame = [&](int base) {
		palette[base + 3] = palette[base + 0];  // slot 4: scrollbar page
		palette[base + 4] = palette[base + 0];  // slot 5: scrollbar controls
		palette[base + 23] = palette[base + 0]; // slot 24: history scrollbar page
		palette[base + 24] = palette[base + 0]; // slot 25: history scrollbar controls
	};
	// Blue/cyan/gray window scrollbars should not drift away from the window frame.
	auto syncWindowScrollbarsToFrame = [&](int base) {
		palette[base + 2] = palette[base + 0]; // slot 3: scrollbar page
		palette[base + 3] = palette[base + 0]; // slot 4: scrollbar controls / thumb
	};
	syncDialogScrollbarsToFrame(32);
	syncDialogScrollbarsToFrame(64);
	syncDialogScrollbarsToFrame(96);
	syncWindowScrollbarsToFrame(8);
	syncWindowScrollbarsToFrame(16);
	syncWindowScrollbarsToFrame(24);

	palette[1] = palette[kMrPaletteDesktop];
	return palette;
}

bool mrApplySettingsSourceForTesting(const std::string &source, std::string *errorMessage) {
	return applySettingsSource(source, errorMessage);
}

bool mrMigrateSettingsMacroToCurrentVersionForTesting(const std::string &settingsPath, const std::string &source, const std::string &reason, std::string *errorMessage) {
	MRSettingsLoadReport report;
	std::string canonicalSource;

	(void)reason;
	return prepareStartupSettingsSource(settingsPath, source, &report, canonicalSource, errorMessage);
}
