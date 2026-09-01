#define Uses_TKeys
#define Uses_TEvent
#define Uses_TView
#define Uses_TDeskTop
#include <tvision/tv.h>

#include "MRFunctionKeyBindings.hpp"

#include "MRCommandRouter.hpp"
#include "MRCommands.hpp"

#include "../ui/MRBentoBox/MRBentoBox.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {

bool compilerDiagnosticsFunctionKeysActive() {
	MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(currentEditWindow());

	return bentoBox != nullptr && bentoBox->problemsPane() != nullptr && bentoBox->hasCompilerProblems();
}

MRBentoBox *currentFileCompareBentoBox() {
	MREditWindow *window = currentEditWindow();

	for (TView *view = window; view != nullptr; view = view->owner) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(view);
		if (bentoBox != nullptr && bentoBox->isFileCompareBox()) return bentoBox;
	}
	if (window != nullptr)
		for (MREditWindow *candidate : allEditWindowsInZOrder()) {
			MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(candidate);
			if (bentoBox != nullptr && bentoBox->isFileCompareBox() && bentoBox->containsFileCompareSourceWindow(window)) return bentoBox;
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

std::vector<MRStatusLine::FunctionKeyLabel> emptyFunctionKeyLabels(ushort modifiers) {
	return {
	    {TKey(kbF1, modifiers), 0, ""},
	    {TKey(kbF2, modifiers), 0, ""},
	    {TKey(kbF3, modifiers), 0, ""},
	    {TKey(kbF4, modifiers), 0, ""},
	    {TKey(kbF5, modifiers), 0, ""},
	    {TKey(kbF6, modifiers), 0, ""},
	    {TKey(kbF7, modifiers), 0, ""},
	    {TKey(kbF8, modifiers), 0, ""},
	    {TKey(kbF9, modifiers), 0, ""},
	    {TKey(kbF10, modifiers), 0, ""},
	    {TKey(kbF11, modifiers), 0, ""},
	    {TKey(kbF12, modifiers), 0, ""},
	};
}

} // namespace

MRBentoBox *mrCurrentMacroDebuggerBentoBox() {
	TView *view = TProgram::deskTop != nullptr ? TProgram::deskTop->current : nullptr;

	for (; view != nullptr; view = view->owner) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(view);

		if (bentoBox != nullptr && bentoBox->macroDebuggerFunctionKeysActive()) return bentoBox;
	}
	return nullptr;
}

MRBentoBox *mrCurrentDebuggerBentoBox() {
	TView *view = TProgram::deskTop != nullptr ? TProgram::deskTop->current : nullptr;

	for (; view != nullptr; view = view->owner) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(view);

		if (bentoBox != nullptr && bentoBox->debuggerFunctionKeysActive()) return bentoBox;
	}
	return nullptr;
}

std::vector<MRStatusLine::FunctionKeyLabel> mrStartupFunctionKeyLabels(ushort modifiers) {
	static const std::vector<MRStatusLine::FunctionKeyLabel> baseLabels{
	    {TKey(kbF1), cmHelp, "~F1~ Help"},
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
	static const std::vector<MRStatusLine::FunctionKeyLabel> shiftLabels{
	    {TKey(kbF1, kbShift), cmMrHelpDetailedIndex, "~S-F1~ Index"},
	    {TKey(kbF2, kbShift), 0, ""},
	    {TKey(kbF3, kbShift), 0, ""},
	    {TKey(kbF4, kbShift), 0, ""},
	    {TKey(kbF5, kbShift), 0, ""},
	    {TKey(kbF6, kbShift), 0, ""},
	    {TKey(kbF7, kbShift), 0, ""},
	    {TKey(kbF8, kbShift), 0, ""},
	    {TKey(kbF9, kbShift), 0, ""},
	    {TKey(kbF10, kbShift), 0, ""},
	    {TKey(kbF11, kbShift), 0, ""},
	    {TKey(kbF12, kbShift), 0, ""},
	};
	static const std::vector<MRStatusLine::FunctionKeyLabel> controlLabels{
	    {TKey(kbF1, kbCtrlShift), 0, ""},
	    {TKey(kbF2, kbCtrlShift), 0, ""},
	    {TKey(kbF3, kbCtrlShift), 0, ""},
	    {TKey(kbF4, kbCtrlShift), 0, ""},
	    {TKey(kbF5, kbCtrlShift), 0, ""},
	    {TKey(kbF6, kbCtrlShift), 0, ""},
	    {TKey(kbF7, kbCtrlShift), 0, ""},
	    {TKey(kbF8, kbCtrlShift), 0, ""},
	    {TKey(kbF9, kbCtrlShift), 0, ""},
	    {TKey(kbF10, kbCtrlShift), 0, ""},
	    {TKey(kbF11, kbCtrlShift), cmMrWindowPrevDesktop, "~C-F11~ ViewL"},
	    {TKey(kbF12, kbCtrlShift), cmMrWindowNextDesktop, "~C-F12~ ViewR"},
	};
	static const std::vector<MRStatusLine::FunctionKeyLabel> altLabels{
	    {TKey(kbF1, kbAltShift), cmMrHelpPreviousTopic, "~A-F1~ Prev"},
	    {TKey(kbF2, kbAltShift), 0, ""},
	    {TKey(kbF3, kbAltShift), 0, ""},
	    {TKey(kbF4, kbAltShift), 0, ""},
	    {TKey(kbF5, kbAltShift), 0, ""},
	    {TKey(kbF6, kbAltShift), 0, ""},
	    {TKey(kbF7, kbAltShift), 0, ""},
	    {TKey(kbF8, kbAltShift), 0, ""},
	    {TKey(kbF9, kbAltShift), 0, ""},
	    {TKey(kbF10, kbAltShift), cmMrMacroToggleRecording, "~A-F10~ Rec"},
	    {TKey(kbF11, kbAltShift), 0, ""},
	    {TKey(kbF12, kbAltShift), 0, ""},
	};
	switch (modifiers) {
		case 0:
			return baseLabels;
		case kbShift:
			return shiftLabels;
		case kbCtrlShift:
			return controlLabels;
		case kbAltShift:
			return altLabels;
		default:
			return emptyFunctionKeyLabels(modifiers);
	}
}

std::vector<MRStatusLine::FunctionKeyLabel> mrEditorFunctionKeyLabels(ushort modifiers) {
	static const std::vector<MRStatusLine::FunctionKeyLabel> baseLabels{
	    {TKey(kbF1), cmHelp, "~F1~ Help"},
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
	static const std::vector<MRStatusLine::FunctionKeyLabel> shiftLabels{
	    {TKey(kbF1, kbShift), cmMrHelpDetailedIndex, "~S-F1~ Index"},
	    {TKey(kbF2, kbShift), cmMrBlockSaveToDisk, "~S-F2~ SaveBlk"},
	    {TKey(kbF3, kbShift), 0, ""},
	    {TKey(kbF4, kbShift), cmMrSearchGetMarker, "~S-F4~ GetMark"},
	    {TKey(kbF5, kbShift), cmMrSearchReplace, "~S-F5~ Replace"},
	    {TKey(kbF6, kbShift), cmMrWindowPrevious, "~S-F6~ PrevWin"},
	    {TKey(kbF7, kbShift), cmMrBlockMarkColumns, "~S-F7~ MarkCol"},
	    {TKey(kbF8, kbShift), cmMrBlockMove, "~S-F8~ MoveBlk"},
	    {TKey(kbF9, kbShift), cmMrBlockToggleVisibility, "~S-F9~ ShowBlk"},
	    {TKey(kbF10, kbShift), 0, ""},
	    {TKey(kbF11, kbShift), cmMrWindowMoveToPrevDesktop, "~S-F11~ WinL"},
	    {TKey(kbF12, kbShift), cmMrWindowMoveToNextDesktop, "~S-F12~ WinR"},
	};
	static const std::vector<MRStatusLine::FunctionKeyLabel> controlLabels{
	    {TKey(kbF1, kbCtrlShift), 0, ""},
	    {TKey(kbF2, kbCtrlShift), cmMrFileSaveAs, "~C-F2~ SaveAs"},
	    {TKey(kbF3, kbCtrlShift), 0, ""},
	    {TKey(kbF4, kbCtrlShift), 0, ""},
	    {TKey(kbF5, kbCtrlShift), cmMrSearchRepeatPrevious, "~C-F5~ Repeat"},
	    {TKey(kbF6, kbCtrlShift), cmMrWindowList, "~C-F6~ WinList"},
	    {TKey(kbF7, kbCtrlShift), cmMrBlockMarkStream, "~C-F7~ MarkStr"},
	    {TKey(kbF8, kbCtrlShift), cmMrBlockDelete, "~C-F8~ DelBlk"},
	    {TKey(kbF9, kbCtrlShift), cmMrBlockTurnMarkingOff, "~C-F9~ EndMark"},
	    {TKey(kbF10, kbCtrlShift), 0, ""},
	    {TKey(kbF11, kbCtrlShift), cmMrWindowPrevDesktop, "~C-F11~ ViewL"},
	    {TKey(kbF12, kbCtrlShift), cmMrWindowNextDesktop, "~C-F12~ ViewR"},
	};
	static const std::vector<MRStatusLine::FunctionKeyLabel> altLabels{
	    {TKey(kbF1, kbAltShift), cmMrHelpPreviousTopic, "~A-F1~ Prev"},
	    {TKey(kbF2, kbAltShift), cmMrBlockUndent, "~A-F2~ Undent"},
	    {TKey(kbF3, kbAltShift), cmMrBlockIndent, "~A-F3~ Indent"},
	    {TKey(kbF4, kbAltShift), 0, ""},
	    {TKey(kbF5, kbAltShift), cmMrSearchGotoLineNumber, "~A-F5~ Goto"},
	    {TKey(kbF6, kbAltShift), cmMrWindowZoom, "~A-F6~ Zoom"},
	    {TKey(kbF7, kbAltShift), cmMrBlockWindowMove, "~A-F7~ WinMove"},
	    {TKey(kbF8, kbAltShift), cmMrBlockWindowCopy, "~A-F8~ WinCopy"},
	    {TKey(kbF9, kbAltShift), 0, ""},
	    {TKey(kbF10, kbAltShift), cmMrMacroToggleRecording, "~A-F10~ Rec"},
	    {TKey(kbF11, kbAltShift), 0, ""},
	    {TKey(kbF12, kbAltShift), 0, ""},
	};
	std::vector<MRStatusLine::FunctionKeyLabel> labels;
	MREditWindow *window = currentEditorCommandWindow();
	const bool diagnosticsActive = compilerDiagnosticsFunctionKeysActive();
	const bool fileCompareActive = fileCompareFunctionKeysActive();
	const bool bentoToolPaneActive = bentoToolPaneFunctionKeysActive();
	MRBentoBox *debuggerBento = mrCurrentDebuggerBentoBox();
	const bool debuggerActive = debuggerBento != nullptr;
	const bool debuggerLive = debuggerBento != nullptr && debuggerBento->debuggerHasLiveSession();
	const bool debuggerRunning = debuggerBento != nullptr && debuggerBento->debuggerSessionRunning();
	const bool macroDebuggerActive = debuggerBento != nullptr && debuggerBento->macroDebuggerFunctionKeysActive();
	const bool readOnlyActive = window != nullptr && window->isReadOnly();

	switch (modifiers) {
		case 0:
			labels = baseLabels;
			break;
		case kbShift:
			labels = shiftLabels;
			if (fileCompareActive) labels[7] = {TKey(kbF8, kbShift), cmMrFileComparePreviousChange, "~S-F8~ Prev"};
			else if (diagnosticsActive)
				labels[7] = {TKey(kbF8, kbShift), cmMrOtherFindPreviousCompilerError, "~S-F8~ PrevErr"};
			if (debuggerActive) {
				labels[6] = {TKey(kbF7, kbShift), cmMrDebuggerEraseWatch, "~S-F7~ Watch -"};
				labels[8] = {TKey(kbF9, kbShift), 0, "~S-F9~ BP +/-"};
				labels[10] = {TKey(kbF11, kbShift), cmMrDebuggerStepOut, debuggerLive && !debuggerRunning ? "~S-F11~ Out" : ""};
			}
			return labels;
		case kbCtrlShift:
			labels = controlLabels;
			return labels;
		case kbAltShift:
			labels = altLabels;
			return labels;
		case static_cast<ushort>(kbShift | kbCtrlShift):
			labels = emptyFunctionKeyLabels(modifiers);
			if (macroDebuggerActive) labels[8] = {TKey(kbF9, modifiers), 0, "~CS-F9~ ClearBP"};
			return labels;
		case static_cast<ushort>(kbShift | kbAltShift):
			labels = emptyFunctionKeyLabels(modifiers);
			if (macroDebuggerActive) labels[8] = {TKey(kbF9, modifiers), 0, "~AS-F9~ BP All"};
			return labels;
		default:
			labels = emptyFunctionKeyLabels(modifiers);
			return labels;
	}

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
	if (debuggerActive) {
		labels[3] = {TKey(kbF4), cmMrDebuggerEvaluate, debuggerLive && !debuggerRunning ? "~F4~ Eval" : ""};
		labels[4] = {TKey(kbF5), cmMrDebuggerContinue, debuggerRunning ? "~F5~ Pause" : (debuggerLive ? "~F5~ Cont" : "")};
		labels[5] = {TKey(kbF6), cmMrDebuggerRunHere, "~F6~ RunHere"};
		labels[6] = {TKey(kbF7), cmMrDebuggerAddWatch, "~F7~ Watch +/-"};
		labels[7] = {TKey(kbF8), cmMrDebuggerStop, debuggerLive ? "~F8~ Stop" : "~F8~ Reset"};
		labels[8] = {TKey(kbF9), cmMrDebuggerToggleBreakpoint, "~F9~ BP"};
		labels[9] = {TKey(kbF10), cmMrDebuggerStep, debuggerLive && !debuggerRunning ? "~F10~ Into" : ""};
		labels[10] = {TKey(kbF11), cmMrDebuggerStepOver, debuggerLive && !debuggerRunning ? "~F11~ Over" : ""};
		labels[11] = {TKey(kbShiftF11), cmMrDebuggerStepOut, debuggerLive && !debuggerRunning ? "~S-F11~ Out" : ""};
	}
	return labels;
}

const std::vector<std::string> &mrSnippetSidekickHintLabels() {
	static const std::vector<std::string> labels{
	    "~A-Enter~ Insert",
	    "~Esc~ Cancel",
	    "~Tab~ Next",
	    "~sTab~ Prev",
	    "~Enter~ NewLn",
	};
	return labels;
}

bool mrEditorFunctionKeyContextActive() {
	MREditWindow *editorWindow = currentEditorCommandWindow();

	return currentEditWindow() != nullptr && editorWindow != nullptr;
}

bool mrHandleStartupFunctionKey(TEvent &event) {
	const TKey pressed(event.keyDown);

	for (const MRStatusLine::FunctionKeyLabel &label : mrStartupFunctionKeyLabels()) {
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

bool mrHandleEditorFunctionKey(TEvent &event) {
	const TKey pressed(event.keyDown);

	if (fileCompareFunctionKeysActive() && (pressed == TKey(kbF8) || pressed == TKey(kbShiftF8) || (event.keyDown.keyCode == kbF8 && (event.keyDown.controlKeyState & kbShift) != 0))) return false;
	for (const MRStatusLine::FunctionKeyLabel &label : mrEditorFunctionKeyLabels()) {
		if (!(pressed == label.keyCode)) continue;
		if (label.command == cmMenu) return false;
		if (!TView::commandEnabled(label.command)) return false;
		static_cast<void>(handleMRCommand(label.command));
		return true;
	}
	return false;
}

bool mrHandleFileCompareFunctionKey(TEvent &event) {
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

bool mrHandleFileCompareCommand(TEvent &event) {
	if (event.what != evCommand) return false;
	const bool nextChange = event.message.command == cmMrFileCompareNextChange;
	const bool previousChange = event.message.command == cmMrFileComparePreviousChange;
	const bool applyOriginalToCompare = event.message.command == cmMrFileCompareApplyOriginalToCompare;
	const bool applyCompareToOriginal = event.message.command == cmMrFileCompareApplyCompareToOriginal;

	if (!nextChange && !previousChange && !applyOriginalToCompare && !applyCompareToOriginal) return false;
	MRBentoBox *bentoBox = currentFileCompareBentoBox();
	if (bentoBox != nullptr) {
		if (nextChange || previousChange) static_cast<void>(bentoBox->navigateFileCompareChange(nextChange));
		else
			static_cast<void>(bentoBox->applyFileCompareChange(applyOriginalToCompare));
	}
	event.what = evNothing;
	return true;
}
