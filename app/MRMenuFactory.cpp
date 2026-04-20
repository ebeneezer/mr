#define Uses_TKeys
#define Uses_TRect
#define Uses_TMenuBar
#define Uses_TMenu
#define Uses_TSubMenu
#define Uses_TMenuItem
#include <tvision/tv.h>

#include "MRCommands.hpp"
#include "MRMenuFactory.hpp"

#include <string>

#include "../config/MRDialogPaths.hpp"
#include "../ui/TMRMenuBar.hpp"

namespace {
TMenuItem *createOrganizeMenuItem() {
	return new TMenuItem("or~G~anize", kbNoKey,
	                     new TMenu(*new TMenuItem("~C~ascade windows", cmMrWindowCascade, kbNoKey, hcNoContext) +
	                               *new TMenuItem("~T~ile windows", cmMrWindowTile, kbNoKey, hcNoContext) + newLine() +
	                               *new TMenuItem("~N~ext workspace", cmMrWindowNextDesktop, kbNoKey, hcNoContext) +
	                               *new TMenuItem("~P~rev workspace", cmMrWindowPrevDesktop, kbNoKey, hcNoContext) +
	                               *new TMenuItem("Move to ne~X~t workspace", cmMrWindowMoveToNextDesktop, kbNoKey, hcNoContext) +
	                               *new TMenuItem("Move to p~R~ev workspace", cmMrWindowMoveToPrevDesktop, kbNoKey, hcNoContext)),
	                     hcNoContext);
}

TMenuItem *createUpperCaseItem() {
	return new TMenuItem("upper ~C~ase", cmMrTextUpperCaseMenu, kbNoKey, hcNoContext);
}

TMenuItem *createLowerCaseItem() {
	return new TMenuItem("lower c~a~se", cmMrTextLowerCaseMenu, kbNoKey, hcNoContext);
}

TSubMenu *createFileMenu() {
	return &(*new TSubMenu("~F~ile", kbAltF) +
	         *new TMenuItem("~O~pen...", cmMrFileOpen, kbF3, hcNoContext, "F3") +
	         *new TMenuItem("~L~oad...", cmMrFileLoad, kbNoKey, hcNoContext) +
	         *new TMenuItem("~S~ave", cmMrFileSave, kbF2, hcNoContext, "F2") +
	         *new TMenuItem("save file ~A~s...", cmMrFileSaveAs, kbCtrlF2, hcNoContext, "CtrlF2") +
	         *new TMenuItem("~I~nformation...", cmMrFileInformation, kbNoKey, hcNoContext) +
	         newLine() + *new TMenuItem("~M~erge...", cmMrFileMerge, kbNoKey, hcNoContext) +
	         *new TMenuItem("~P~rint...", cmMrFilePrint, kbNoKey, hcNoContext) + newLine() +
	         *new TMenuItem("~D~OS shell", cmMrFileShellToDos, kbAltF9, hcNoContext, "AltF9") +
	         newLine() + *new TMenuItem("E~x~it", cmQuit, kbAltX, hcNoContext, "Alt-X"));
}

TSubMenu *createEditMenu() {
	return &(
	    *new TSubMenu("~E~dit", kbAltE) +
	    *new TMenuItem("~U~ndo", cmMrEditUndo, kbCtrlZ, hcNoContext, "CtrlZ") +
	    *new TMenuItem("~R~edo", cmMrEditRedo, TKey('Z', kbCtrlShift | kbShift), hcNoContext, "ShiftCtrlZ") + newLine() +
	    *new TMenuItem("~C~ut to buffer", cmMrEditCutToBuffer, kbCtrlIns, hcNoContext, "CtrlIns") +
	    *new TMenuItem("co~P~y to buffer", cmMrEditCopyToBuffer, kbNoKey, hcNoContext,
	                   "CtrlGrey+") +
	    *new TMenuItem("~A~ppend to buffer", cmMrEditAppendToBuffer, kbCtrlDel, hcNoContext,
	                   "CtrlDel") +
	    *new TMenuItem("cut and ap~P~end to buffer", cmMrEditCutAndAppendToBuffer, kbNoKey,
	                   hcNoContext, "CtrlGrey-") +
	    *new TMenuItem("~P~aste from buffer", cmMrEditPasteFromBuffer, kbShiftIns, hcNoContext,
	                   "ShiftIns") +
	    newLine() +
	    *new TMenuItem("re~P~eat command", cmMrEditRepeatCommand, kbCtrlR, hcNoContext, "CtrlR"));
}

TSubMenu *createWindowMenu() {
	return &(
	    *new TSubMenu("~W~indow", kbAltW) +
	    *new TMenuItem("~O~pen...", cmMrWindowOpen, kbNoKey, hcNoContext) +
	    *new TMenuItem("~C~lose", cmMrWindowClose, kbNoKey, hcNoContext) +
	    *new TMenuItem("~S~plit...", cmMrWindowSplit, kbNoKey, hcNoContext) +
	    *new TMenuItem("~L~ist...", cmMrWindowList, kbCtrlF6, hcNoContext, "CtrlF6") + newLine() +
	    *new TMenuItem("~N~ext", cmMrWindowNext, kbF6, hcNoContext, "F6") +
	    *new TMenuItem("~P~revious", cmMrWindowPrevious, kbShiftF6, hcNoContext, "ShiftF6") +
	    *new TMenuItem("~A~djacent...", cmMrWindowAdjacent, kbNoKey, hcNoContext) + newLine() +
	    *new TMenuItem("~H~ide", cmMrWindowHide, kbNoKey, hcNoContext) +
	    *new TMenuItem("~M~odify size", cmMrWindowModifySize, kbNoKey, hcNoContext, "ScrollLock") +
	    *new TMenuItem("~Z~oom", cmMrWindowZoom, kbAltF6, hcNoContext, "AltF6") +
	    *new TMenuItem("m~I~nimize", cmMrWindowMinimize, kbNoKey, hcNoContext) +
	    *createOrganizeMenuItem() + newLine() +
	    *new TMenuItem("lin~K~...", cmMrWindowLink, kbNoKey, hcNoContext) +
	    *new TMenuItem("~U~nlink", cmMrWindowUnlink, kbNoKey, hcNoContext));
}

TSubMenu *createBlockMenu() {
	std::string persistentLabel =
	    configuredPersistentBlocksSetting() ? "~P~ersistent blocks [ON]" : "~P~ersistent blocks [OFF]";
	return &(
	    *new TSubMenu("~B~lock", kbAltB) +
	    *new TMenuItem("~C~opy block", cmMrBlockCopy, kbF8, hcNoContext, "F8") +
	    *new TMenuItem("~M~ove block", cmMrBlockMove, kbShiftF8, hcNoContext, "ShiftF8") +
	    *new TMenuItem("~D~elete block", cmMrBlockDelete, kbCtrlF8, hcNoContext, "CtrlF8") +
	    newLine() +
	    *new TMenuItem("save ~B~lock to disk", cmMrBlockSaveToDisk, kbShiftF2, hcNoContext,
	                   "ShiftF2") +
	    *new TMenuItem("~I~ndent block", cmMrBlockIndent, kbAltF3, hcNoContext, "AltF3") +
	    *new TMenuItem("~U~ndent block", cmMrBlockUndent, kbAltF2, hcNoContext, "AltF2") +
	    newLine() +
	    *new TMenuItem("~W~indow copy...", cmMrBlockWindowCopy, kbAltF8, hcNoContext, "AltF8") +
	    *new TMenuItem("w~i~ndow move...", cmMrBlockWindowMove, kbAltF7, hcNoContext, "AltF7") +
	    newLine() +
	    *new TMenuItem("mark ~L~ines of text", cmMrBlockMarkLines, kbF7, hcNoContext, "F7") +
	    *new TMenuItem("mark c~O~lumns of text", cmMrBlockMarkColumns, kbShiftF7, hcNoContext,
	                   "ShiftF7") +
	    *new TMenuItem("mark ~S~tream of text", cmMrBlockMarkStream, kbCtrlF7, hcNoContext,
	                   "CtrlF7") +
	    *new TMenuItem("~E~nd marking", cmMrBlockEndMarking, kbF7, hcNoContext, "F7") +
	    *new TMenuItem("~t~urn marking off", cmMrBlockTurnMarkingOff, kbCtrlF9, hcNoContext,
	                   "CtrlF9") +
	    newLine() +
	    *new TMenuItem(persistentLabel.c_str(), cmMrBlockPersistent, kbNoKey, hcNoContext));
}

TSubMenu *createSearchMenu() {
	return &(*new TSubMenu("~S~earch", kbAltS) +
	         *new TMenuItem("Search for ~t~ext...", cmMrSearchFindText, kbF5, hcNoContext, "F5") +
	         *new TMenuItem("search and ~R~eplace...", cmMrSearchReplace, kbShiftF5, hcNoContext,
	                        "ShiftF5") +
	         *new TMenuItem("repeat ~P~revious search", cmMrSearchRepeatPrevious, kbCtrlF5,
	                        hcNoContext, "CtrlF5") +
	         newLine() +
	         *new TMenuItem("p~U~sh position onto marker stack", cmMrSearchPushMarker, kbF4,
	                        hcNoContext, "F4") +
	         *new TMenuItem("~G~et position from marker stack", cmMrSearchGetMarker, kbShiftF4,
	                        hcNoContext, "ShiftF4") +
	         newLine() +
	         *new TMenuItem("set random ~A~ccess mark...", cmMrSearchSetRandomAccessMark, kbNoKey,
	                        hcNoContext) +
	         *new TMenuItem("re~T~rieve random access mark...", cmMrSearchRetrieveRandomAccessMark,
	                        kbNoKey, hcNoContext) +
	         newLine() +
	         *new TMenuItem("goto line ~N~umber...", cmMrSearchGotoLineNumber, kbAltF5, hcNoContext,
	                        "AltF5"));
}

TSubMenu *createTextMenu() {
	return &(*new TSubMenu("~T~ext", kbAltT) +
	         *new TMenuItem("~L~ayout...", cmMrTextLayout, kbCtrlF3, hcNoContext, "CtrlF3") +
	         *new TMenuItem("p~U~sh position onto marker stack", cmMrSearchPushMarker, kbF4,
	                        hcNoContext, "F4") +
	         *new TMenuItem("~G~et position from marker stack", cmMrSearchGetMarker, kbShiftF4,
	                        hcNoContext, "ShiftF4") +
	         newLine() + *createUpperCaseItem() + *createLowerCaseItem() +
	         *new TMenuItem("cen~T~er line", cmMrTextCenterLine, kbNoKey, hcNoContext) +
	         *new TMenuItem("time/~D~ate stamp", cmMrTextTimeDateStamp, kbNoKey, hcNoContext) +
	         newLine() +
	         *new TMenuItem("re-~F~ormat paragraph", cmMrTextReformatParagraph, kbAltR, hcNoContext,
	                        "AltR"));
}

TSubMenu *createOtherMenu() {
	return &(
	    *new TSubMenu("~O~ther", kbAltO) +
	    *new TMenuItem("~I~nstallation and setup", cmMrOtherInstallationAndSetup, kbNoKey,
	                   hcNoContext) +
	    newLine() +
	    *new TMenuItem("~M~acro manager...", cmMrOtherMacroManager, kbNoKey, hcNoContext) +
	    newLine() +
	    *new TMenuItem("~E~xecute program...", cmMrOtherExecuteProgram, kbF9, hcNoContext, "F9") +
	    *new TMenuItem("~S~top current program", cmMrOtherStopProgram, kbNoKey, hcNoContext) +
	    *new TMenuItem("~R~estart current program", cmMrOtherRestartProgram, kbNoKey, hcNoContext) +
	    *new TMenuItem("~C~lear current output", cmMrOtherClearOutput, kbNoKey, hcNoContext) +
	    *new TMenuItem("find ne~X~t compiler error", cmMrOtherFindNextCompilerError, kbShiftF9,
	                   hcNoContext, "ShiftF9") +
	    *new TMenuItem("~M~atch brace or paren", cmMrOtherMatchBraceOrParen, kbCtrlF5, hcNoContext,
	                   "CtrlF5") +
	    newLine() +
	    *new TMenuItem("~A~scii table", cmMrOtherAsciiTable, kbAltA, hcNoContext, "AltA"));
}

TSubMenu *createMacroMenu() {
	return &(
	    *new TSubMenu("~M~acro", kbAltM) +
	    *new TMenuItem("Macro ~M~anager...", cmMrOtherMacroManager, kbNoKey, hcNoContext) +
	    newLine() +
	    *new TMenuItem("~R~ecording start/stop", cmMrMacroToggleRecording, kbAltF10, hcNoContext,
	                   "AltF10"));
}

TSubMenu *createHelpMenu() {
	return &(
	    *new TSubMenu("~H~elp", kbAltH) +
	    *new TMenuItem("~T~able of contents", cmMrHelpContents, kbF1, hcNoContext, "F1") +
	    *new TMenuItem("~K~eys", cmMrHelpKeys, kbF1, hcNoContext, "F1") +
	    *new TMenuItem("detailed ~I~ndex", cmMrHelpDetailedIndex, kbShiftF1, hcNoContext,
	                   "ShiftF1") +
	    *new TMenuItem("~P~revious topic", cmMrHelpPreviousTopic, kbAltF1, hcNoContext, "AltF1") +
	    *new TMenuItem("~A~bout...", cmMrHelpAbout, kbNoKey, hcNoContext));
}

TSubMenu *createDevMenu() {
	return &(*new TSubMenu("De~V~", kbAltV) +
	         *new TMenuItem("~C~ancel background macros", cmMrDevCancelMacroTasks, kbNoKey, hcNoContext) +
	         *new TMenuItem("Test ~H~ero event", cmMrDevHeroEventProbe, kbNoKey, hcNoContext));
}
} // namespace

TMenuBar *createMRMenuBar(TRect r) {
	r.b.y = r.a.y + 1;
	return new TMRMenuBar(r, *createFileMenu() + *createEditMenu() + *createWindowMenu() +
	                             *createBlockMenu() + *createSearchMenu() + *createTextMenu() +
	                             *createOtherMenu() + *createMacroMenu() + *createHelpMenu() +
	                             *createDevMenu());
}
