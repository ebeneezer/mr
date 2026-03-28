#define Uses_TKeys
#define Uses_TApplication
#define Uses_TEvent
#define Uses_TRect
#define Uses_TMenuBar
#define Uses_TMenu
#define Uses_TSubMenu
#define Uses_TMenuItem
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_TDeskTop
#define Uses_TObject
#define Uses_MsgBox
#define Uses_TWindow
#define Uses_TEditWindow
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#include <tvision/tv.h>

#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <cstdlib>

#include "mrmac/mrvm.hpp"
#include "mrmac/mrmac.h"
#include "ui/mrpalette.hpp"
#include "ui/mrmacrotest.hpp"
#include "ui/TMRMenuBar.hpp"
#include "ui/TMRStatusLine.hpp"
#include "ui/TMRDeskTop.hpp"
#include "ui/TMREditWindow.hpp"
#include "ui/mrtheme.hpp"

namespace
{
    enum : ushort
    {
        cmMrFileOpen = 1000,
        cmMrFileLoad,
        cmMrFileSave,
        cmMrFileSaveAs,
        cmMrFileInformation,
        cmMrFileMerge,
        cmMrFilePrint,
        cmMrFileShellToDos,

        cmMrEditUndo,
        cmMrEditCutToBuffer,
        cmMrEditCopyToBuffer,
        cmMrEditAppendToBuffer,
        cmMrEditCutAndAppendToBuffer,
        cmMrEditPasteFromBuffer,
        cmMrEditRepeatCommand,

        cmMrWindowOpen,
        cmMrWindowClose,
        cmMrWindowSplit,
        cmMrWindowList,
        cmMrWindowNext,
        cmMrWindowPrevious,
        cmMrWindowAdjacent,
        cmMrWindowHide,
        cmMrWindowModifySize,
        cmMrWindowZoom,
        cmMrWindowMinimize,
        cmMrWindowOrganize,
        cmMrWindowLink,
        cmMrWindowUnlink,
        cmMrWindowOrganizePlaceholder,

        cmMrBlockCopy,
        cmMrBlockMove,
        cmMrBlockDelete,
        cmMrBlockSaveToDisk,
        cmMrBlockIndent,
        cmMrBlockUndent,
        cmMrBlockWindowCopy,
        cmMrBlockWindowMove,
        cmMrBlockMarkLines,
        cmMrBlockMarkColumns,
        cmMrBlockMarkStream,
        cmMrBlockEndMarking,
        cmMrBlockTurnMarkingOff,
        cmMrBlockPersistent,

        cmMrSearchFindText,
        cmMrSearchReplace,
        cmMrSearchRepeatPrevious,
        cmMrSearchPushMarker,
        cmMrSearchGetMarker,
        cmMrSearchSetRandomAccessMark,
        cmMrSearchRetrieveRandomAccessMark,
        cmMrSearchGotoLineNumber,

        cmMrTextLayout,
        cmMrTextUpperCaseMenu,
        cmMrTextLowerCaseMenu,
        cmMrTextCenterLine,
        cmMrTextTimeDateStamp,
        cmMrTextReformatParagraph,
        cmMrTextUpperCasePlaceholder,
        cmMrTextLowerCasePlaceholder,

        cmMrOtherInstallationAndSetup,
        cmMrOtherKeystrokeMacros,
        cmMrOtherExecuteProgram,
        cmMrOtherFindNextCompilerError,
        cmMrOtherMatchBraceOrParen,
        cmMrOtherAsciiTable,

        cmMrHelpContents,
        cmMrHelpKeys,
        cmMrHelpDetailedIndex,
        cmMrHelpPreviousTopic,
        cmMrHelpAbout,

        cmMrSetupEditSettings,
        cmMrSetupDisplaySetup,
        cmMrSetupColorSetup,
        cmMrSetupKeyMapping,
        cmMrSetupMouseKeyRepeat,
        cmMrSetupFilenameExtensions,
        cmMrSetupSwappingEmsXms,
        cmMrSetupBackupsTempAutosave,
        cmMrSetupSearchAndReplaceDefaults,
        cmMrSetupUserInterfaceSettings,
        cmMrSetupSaveConfigurationAndExit,
        cmMrSetupSearchAndReplacePlaceholder,

        cmMrColorWindowColors,
        cmMrColorMenuDialogColors,
        cmMrColorHelpColors,
        cmMrColorOtherColors,

        cmMrDevRunMacro
    };

    std::string readTextFile(const std::string &filename)
    {
        std::ifstream file(filename.c_str(), std::ios::in | std::ios::binary);
        if (!file.is_open())
            return std::string();

        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    TRect centeredRect(int width, int height)
    {
        TRect r = TProgram::deskTop->getExtent();
        int left = r.a.x + (r.b.x - r.a.x - width) / 2;
        int top = r.a.y + (r.b.y - r.a.y - height) / 2;
        return TRect(left, top, left + width, top + height);
    }

    void insertStaticLine(TDialog *dialog, int x, int y, const char *text)
    {
        dialog->insert(new TStaticText(TRect(x, y, x + std::strlen(text) + 1, y + 1), text));
    }

    ushort execDialog(TDialog *dialog)
    {
        ushort result = cmCancel;
        if (dialog != 0)
        {
            result = TProgram::deskTop->execView(dialog);
            TObject::destroy(dialog);
        }
        return result;
    }

    TDialog *createSimplePreviewDialog(const char *title,
                                       int width,
                                       int height,
                                       const std::vector<std::string> &lines,
                                       bool showOkCancelHelp = false)
    {
        TDialog *dialog = new TDialog(centeredRect(width, height), title);
        int y = 2;

        for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it, ++y)
            insertStaticLine(dialog, 2, y, it->c_str());

        if (showOkCancelHelp)
        {
            dialog->insert(new TButton(TRect(width - 34, height - 3, width - 24, height - 1), "OK", cmOK, bfDefault));
            dialog->insert(new TButton(TRect(width - 23, height - 3, width - 10, height - 1), "Cancel", cmCancel, bfNormal));
            dialog->insert(new TButton(TRect(width - 9, height - 3, width - 2, height - 1), "Help", cmHelp, bfNormal));
        }
        else
            dialog->insert(new TButton(TRect(width / 2 - 4, height - 3, width / 2 + 4, height - 1), "Done", cmOK, bfDefault));

        return dialog;
    }

    TDialog *createInstallationAndSetupDialog()
    {
        TDialog *dialog = new TDialog(centeredRect(56, 21), "INSTALLATION AND SETUP");

        insertStaticLine(dialog, 2, 2, "DOS-5.0   CPU=80486");
        insertStaticLine(dialog, 2, 3, "Video Card = VGA Color");
        insertStaticLine(dialog, 2, 4, "ME Path = C:\\");
        insertStaticLine(dialog, 2, 5, "Serial #  Test");

        dialog->insert(new TButton(TRect(2, 7, 28, 9), "Edit settings...", cmMrSetupEditSettings, bfNormal));
        dialog->insert(new TButton(TRect(2, 9, 28, 11), "Display setup...", cmMrSetupDisplaySetup, bfNormal));
        dialog->insert(new TButton(TRect(2, 11, 28, 13), "Color setup...", cmMrSetupColorSetup, bfNormal));
        dialog->insert(new TButton(TRect(2, 13, 28, 15), "Key mapping...", cmMrSetupKeyMapping, bfNormal));
        dialog->insert(new TButton(TRect(29, 7, 54, 9), "Mouse / Key repeat...", cmMrSetupMouseKeyRepeat, bfNormal));
        dialog->insert(new TButton(TRect(29, 9, 54, 11), "Filename extensions...", cmMrSetupFilenameExtensions, bfNormal));
        dialog->insert(new TButton(TRect(29, 11, 54, 13), "Swapping / EMS / XMS...", cmMrSetupSwappingEmsXms, bfNormal));
        dialog->insert(new TButton(TRect(29, 13, 54, 15), "Backups / Temp / Autosave...", cmMrSetupBackupsTempAutosave, bfNormal));
        dialog->insert(new TButton(TRect(2, 15, 28, 17), "Search and Replace...", cmMrSetupSearchAndReplaceDefaults, bfNormal));
        dialog->insert(new TButton(TRect(29, 15, 54, 17), "User interface settings...", cmMrSetupUserInterfaceSettings, bfNormal));
        dialog->insert(new TButton(TRect(2, 18, 28, 20), "Exit Setup", cmCancel, bfNormal));
        dialog->insert(new TButton(TRect(29, 18, 54, 20), "Save configuration", cmMrSetupSaveConfigurationAndExit, bfDefault));

        return dialog;
    }

    TDialog *createColorSetupDialog()
    {
        TDialog *dialog = new TDialog(centeredRect(32, 11), "COLOR SETUP");
        dialog->insert(new TButton(TRect(2, 2, 29, 4), "Window colors", cmMrColorWindowColors, bfNormal));
        dialog->insert(new TButton(TRect(2, 4, 29, 6), "Menu/Dialog colors", cmMrColorMenuDialogColors, bfNormal));
        dialog->insert(new TButton(TRect(2, 6, 29, 8), "Help colors", cmMrColorHelpColors, bfNormal));
        dialog->insert(new TButton(TRect(2, 8, 29, 10), "Other colors", cmMrColorOtherColors, bfNormal));
        return dialog;
    }

    const char *dummyCommandTitle(ushort command)
    {
        switch (command)
        {
        case cmMrFileOpen: return "File / Open";
        case cmMrFileLoad: return "File / Load";
        case cmMrFileSave: return "File / Save";
        case cmMrFileSaveAs: return "File / Save File As";
        case cmMrFileInformation: return "File / Information";
        case cmMrFileMerge: return "File / Merge";
        case cmMrFilePrint: return "File / Print";
        case cmMrFileShellToDos: return "File / Shell to DOS";

        case cmMrEditUndo: return "Edit / Undo";
        case cmMrEditCutToBuffer: return "Edit / Cut to buffer";
        case cmMrEditCopyToBuffer: return "Edit / Copy to buffer";
        case cmMrEditAppendToBuffer: return "Edit / Append to buffer";
        case cmMrEditCutAndAppendToBuffer: return "Edit / Cut and append to buffer";
        case cmMrEditPasteFromBuffer: return "Edit / Paste from buffer";
        case cmMrEditRepeatCommand: return "Edit / Repeat command";

        case cmMrWindowClose: return "Window / Close";
        case cmMrWindowSplit: return "Window / Split";
        case cmMrWindowList: return "Window / List";
        case cmMrWindowNext: return "Window / Next";
        case cmMrWindowPrevious: return "Window / Previous";
        case cmMrWindowAdjacent: return "Window / Adjacent";
        case cmMrWindowHide: return "Window / Hide";
        case cmMrWindowModifySize: return "Window / Modify size";
        case cmMrWindowZoom: return "Window / Zoom";
        case cmMrWindowMinimize: return "Window / Minimize";
        case cmMrWindowOrganizePlaceholder: return "Window / Organize";
        case cmMrWindowLink: return "Window / Link";
        case cmMrWindowUnlink: return "Window / Unlink";

        case cmMrBlockCopy: return "Block / Copy block";
        case cmMrBlockMove: return "Block / Move block";
        case cmMrBlockDelete: return "Block / Delete block";
        case cmMrBlockSaveToDisk: return "Block / Save block to disk";
        case cmMrBlockIndent: return "Block / Indent block";
        case cmMrBlockUndent: return "Block / Undent block";
        case cmMrBlockWindowCopy: return "Block / Window copy";
        case cmMrBlockWindowMove: return "Block / Window move";
        case cmMrBlockMarkLines: return "Block / Mark lines of text";
        case cmMrBlockMarkColumns: return "Block / Mark columns of text";
        case cmMrBlockMarkStream: return "Block / Mark stream of text";
        case cmMrBlockEndMarking: return "Block / End marking";
        case cmMrBlockTurnMarkingOff: return "Block / Turn marking off";
        case cmMrBlockPersistent: return "Block / Persistent blocks";

        case cmMrSearchFindText: return "Search / Search for text";
        case cmMrSearchReplace: return "Search / Search and Replace";
        case cmMrSearchRepeatPrevious: return "Search / Repeat previous search";
        case cmMrSearchPushMarker: return "Search / Push position onto marker stack";
        case cmMrSearchGetMarker: return "Search / Get position from marker stack";
        case cmMrSearchSetRandomAccessMark: return "Search / Set random access mark";
        case cmMrSearchRetrieveRandomAccessMark: return "Search / Retrieve random access mark";
        case cmMrSearchGotoLineNumber: return "Search / Goto line number";

        case cmMrTextLayout: return "Text / Layout";
        case cmMrTextUpperCaseMenu: return "Text / Upper case";
        case cmMrTextLowerCaseMenu: return "Text / Lower case";
        case cmMrTextCenterLine: return "Text / Center line";
        case cmMrTextTimeDateStamp: return "Text / Time/Date stamp";
        case cmMrTextReformatParagraph: return "Text / Re-format paragraph";
        case cmMrTextUpperCasePlaceholder: return "Text / Upper case";
        case cmMrTextLowerCasePlaceholder: return "Text / Lower case";

        case cmMrOtherKeystrokeMacros: return "Other / Keystroke macros";
        case cmMrOtherExecuteProgram: return "Other / Execute program";
        case cmMrOtherFindNextCompilerError: return "Other / Find next compiler error";
        case cmMrOtherMatchBraceOrParen: return "Other / Match brace or paren";
        case cmMrOtherAsciiTable: return "Other / Ascii table";

        case cmMrHelpContents: return "Help / Table of contents";
        case cmMrHelpKeys: return "Help / Keys";
        case cmMrHelpDetailedIndex: return "Help / Detailed index";
        case cmMrHelpPreviousTopic: return "Help / Previous topic";
        case cmMrHelpAbout: return "Help / About";

        case cmMrSetupKeyMapping: return "Installation / Key mapping";
        case cmMrSetupMouseKeyRepeat: return "Installation / Mouse / Key repeat setup";
        case cmMrSetupFilenameExtensions: return "Installation / Filename extensions";
        case cmMrSetupSwappingEmsXms: return "Installation / Swapping / EMS / XMS";
        case cmMrSetupBackupsTempAutosave: return "Installation / Backups / Temp files / Autosave";
        case cmMrSetupUserInterfaceSettings: return "Installation / User interface settings";
        case cmMrSetupSearchAndReplacePlaceholder: return "Installation / Search and Replace defaults";

        default: return 0;
        }
    }

    void showDummyCommandBox(const char *title)
    {
        if (title == 0)
            title = "Command";
        messageBox(mfInformation | mfOKButton, "%s\n\nDummy implementation for now.", title);
    }

    TDialog *createEditSettingsDialog()
    {
        std::vector<std::string> lines;
        lines.push_back("Page break string........ ?");
        lines.push_back("Word delimits........... .()'\",#$012%^&*+-/[]?");
        lines.push_back("Max undo count.......... 32000");
        lines.push_back("Default file extension(s) C:PAS;ASM;BAT;TXT;DO");
        lines.push_back("");
        lines.push_back("Cursor: Insert / Overwrite / Underline / 1/2 block");
        lines.push_back("        2/3 block / Full block");
        lines.push_back("Options: [X] Truncate spaces");
        lines.push_back("         [ ] Control-Z at EOF");
        lines.push_back("         [ ] CR/LF at EOF");
        lines.push_back("Tab expand: Spaces");
        lines.push_back("Column block move style: Delete space");
        lines.push_back("Default mode: Insert");
        return createSimplePreviewDialog("EDIT SETTINGS", 76, 20, lines, true);
    }

    TDialog *createDisplaySetupDialog()
    {
        std::vector<std::string> lines;
        lines.push_back("Video mode");
        lines.push_back("  (*) 25 lines");
        lines.push_back("  ( ) 30/33 lines");
        lines.push_back("  ( ) 43/50 lines");
        lines.push_back("  ( ) UltraVision");
        lines.push_back("F-key labels delay (1/10 secs): 3");
        lines.push_back("UltraVision mode (hex): 00");
        lines.push_back("");
        lines.push_back("Screen layout");
        lines.push_back("  [X] Status/message line");
        lines.push_back("  [X] Menu bar");
        lines.push_back("  [X] Function key labels");
        lines.push_back("  [X] Left-hand border");
        lines.push_back("  [X] Right-hand border");
        lines.push_back("  [X] Bottom border");
        return createSimplePreviewDialog("DISPLAY SETUP", 60, 20, lines, true);
    }

    TDialog *createWindowColorsDialog()
    {
        std::vector<std::string> lines;
        lines.push_back("Text");
        lines.push_back("Changed-Text");
        lines.push_back("Highlighted-Text");
        lines.push_back("End-Of-File");
        lines.push_back("Window-border");
        lines.push_back("window-Bold");
        lines.push_back("cUrrent line");
        lines.push_back("cuRrent line in block");
        lines.push_back("");
        lines.push_back("Preview:");
        lines.push_back("Normal text");
        lines.push_back("Changed text");
        lines.push_back("Highlighted text");
        lines.push_back("Current line");
        lines.push_back("Current line in block");
        return createSimplePreviewDialog("WINDOW COLORS", 34, 20, lines, false);
    }

    TDialog *createMenuDialogColorsDialog()
    {
        std::vector<std::string> lines;
        lines.push_back("Menu-Text");
        lines.push_back("menu-Highlight");
        lines.push_back("menu-Bold");
        lines.push_back("menu-skip");
        lines.push_back("Menu-border");
        lines.push_back("bUtton");
        lines.push_back("button-Key");
        lines.push_back("button-shAdow");
        lines.push_back("Select");
        lines.push_back("Not-select");
        lines.push_back("Checkbox bold");
        lines.push_back("");
        lines.push_back("Preview:");
        lines.push_back("Window  Menu  File  Block");
        lines.push_back("(*) Select   ( ) Not-select");
        lines.push_back("Button<KEY>");
        return createSimplePreviewDialog("MENU / DIALOG COLORS", 38, 21, lines, false);
    }

    TDialog *createHelpColorsDialog()
    {
        std::vector<std::string> lines;
        lines.push_back("Help-Text");
        lines.push_back("help-Highlight");
        lines.push_back("help-Chapter");
        lines.push_back("help-Border");
        lines.push_back("help-Link");
        lines.push_back("help-F-keys");
        lines.push_back("help-attr-1");
        lines.push_back("help-attr-2");
        lines.push_back("help-attr-3");
        lines.push_back("");
        lines.push_back("SAMPLE HELP");
        lines.push_back("CHAPTER HEADING");
        lines.push_back("This is help text.");
        lines.push_back("This is a LINK");
        lines.push_back("Attr1, Attr2, Attr3");
        return createSimplePreviewDialog("HELP COLORS", 36, 20, lines, false);
    }

    TDialog *createOtherColorsDialog()
    {
        std::vector<std::string> lines;
        lines.push_back("statusline");
        lines.push_back("statusline-Bold");
        lines.push_back("Fkey-Labels");
        lines.push_back("fkey-Numbers");
        lines.push_back("Error");
        lines.push_back("Message");
        lines.push_back("Working");
        lines.push_back("shadow");
        lines.push_back("shadow-Character");
        lines.push_back("Background color");
        lines.push_back("");
        lines.push_back("Message  WORKING  L:333 C:10");
        lines.push_back("ERROR BOX");
        lines.push_back("1Help 2Save 3Load 4Indent");
        return createSimplePreviewDialog("OTHER COLORS", 36, 19, lines, false);
    }

    TMenuItem *createOrganizeMenuItem()
    {
        return new TMenuItem(
            "or~G~anize",
            kbNoKey,
            new TMenu(
                *new TMenuItem("Placeholder", cmMrWindowOrganizePlaceholder, kbNoKey, hcNoContext)
            ),
            hcNoContext);
    }

    TMenuItem *createUpperCaseItem()
    {
        return new TMenuItem("upper ~C~ase", cmMrTextUpperCaseMenu, kbNoKey, hcNoContext);
    }

    TMenuItem *createLowerCaseItem()
    {
        return new TMenuItem("lower c~a~se", cmMrTextLowerCaseMenu, kbNoKey, hcNoContext);
    }

    TSubMenu *createFileMenu()
    {
        return &(*new TSubMenu("~F~ile", kbAltF)
            + *new TMenuItem("~O~pen...", cmMrFileOpen, kbF3, hcNoContext, "F3")
            + *new TMenuItem("~L~oad...", cmMrFileLoad, kbNoKey, hcNoContext)
            + *new TMenuItem("~S~ave", cmMrFileSave, kbF2, hcNoContext, "F2")
            + *new TMenuItem("save file ~A~s...", cmMrFileSaveAs, kbCtrlF2, hcNoContext, "CtrlF2")
            + *new TMenuItem("~I~nformation...", cmMrFileInformation, kbNoKey, hcNoContext)
            + newLine()
            + *new TMenuItem("~M~erge...", cmMrFileMerge, kbNoKey, hcNoContext)
            + *new TMenuItem("~P~rint...", cmMrFilePrint, kbNoKey, hcNoContext)
            + newLine()
            + *new TMenuItem("~D~OS shell", cmMrFileShellToDos, kbAltF9, hcNoContext, "AltF9")
            + newLine()
            + *new TMenuItem("E~x~it", cmQuit, kbAltX, hcNoContext, "Alt-X"));
    }

    TSubMenu *createEditMenu()
    {
        return &(*new TSubMenu("~E~dit", kbAltE)
            + *new TMenuItem("~U~ndo", cmMrEditUndo, kbCtrlU, hcNoContext, "CtrlU")
            + newLine()
            + *new TMenuItem("~C~ut to buffer", cmMrEditCutToBuffer, kbCtrlIns, hcNoContext, "CtrlIns")
            + *new TMenuItem("co~P~y to buffer", cmMrEditCopyToBuffer, kbNoKey, hcNoContext, "CtrlGrey+")
            + *new TMenuItem("~A~ppend to buffer", cmMrEditAppendToBuffer, kbCtrlDel, hcNoContext, "CtrlDel")
            + *new TMenuItem("cut and ap~P~end to buffer", cmMrEditCutAndAppendToBuffer, kbNoKey, hcNoContext, "CtrlGrey-")
            + *new TMenuItem("~P~aste from buffer", cmMrEditPasteFromBuffer, kbShiftIns, hcNoContext, "ShiftIns")
            + newLine()
            + *new TMenuItem("re~P~eat command", cmMrEditRepeatCommand, kbCtrlR, hcNoContext, "CtrlR"));
    }

    TSubMenu *createWindowMenu()
    {
        return &(*new TSubMenu("~W~indow", kbAltW)
            + *new TMenuItem("~O~pen...", cmMrWindowOpen, kbNoKey, hcNoContext)
            + *new TMenuItem("~C~lose", cmMrWindowClose, kbNoKey, hcNoContext)
            + *new TMenuItem("~S~plit...", cmMrWindowSplit, kbNoKey, hcNoContext)
            + *new TMenuItem("~L~ist...", cmMrWindowList, kbCtrlF6, hcNoContext, "CtrlF6")
            + newLine()
            + *new TMenuItem("~N~ext", cmMrWindowNext, kbF6, hcNoContext, "F6")
            + *new TMenuItem("~P~revious", cmMrWindowPrevious, kbShiftF6, hcNoContext, "ShiftF6")
            + *new TMenuItem("~A~djacent...", cmMrWindowAdjacent, kbNoKey, hcNoContext)
            + newLine()
            + *new TMenuItem("~H~ide", cmMrWindowHide, kbNoKey, hcNoContext)
            + *new TMenuItem("~M~odify size", cmMrWindowModifySize, kbNoKey, hcNoContext, "ScrollLock")
            + *new TMenuItem("~Z~oom", cmMrWindowZoom, kbAltF6, hcNoContext, "AltF6")
            + *new TMenuItem("m~I~nimize", cmMrWindowMinimize, kbNoKey, hcNoContext)
            + *createOrganizeMenuItem()
            + newLine()
            + *new TMenuItem("lin~K~...", cmMrWindowLink, kbNoKey, hcNoContext)
            + *new TMenuItem("~U~nlink", cmMrWindowUnlink, kbNoKey, hcNoContext));
    }

    TSubMenu *createBlockMenu()
    {
        return &(*new TSubMenu("~B~lock", kbAltB)
            + *new TMenuItem("~C~opy block", cmMrBlockCopy, kbF8, hcNoContext, "F8")
            + *new TMenuItem("~M~ove block", cmMrBlockMove, kbShiftF8, hcNoContext, "ShiftF8")
            + *new TMenuItem("~D~elete block", cmMrBlockDelete, kbCtrlF8, hcNoContext, "CtrlF8")
            + newLine()
            + *new TMenuItem("save ~B~lock to disk", cmMrBlockSaveToDisk, kbShiftF2, hcNoContext, "ShiftF2")
            + *new TMenuItem("~I~ndent block", cmMrBlockIndent, kbAltF3, hcNoContext, "AltF3")
            + *new TMenuItem("~U~ndent block", cmMrBlockUndent, kbAltF2, hcNoContext, "AltF2")
            + newLine()
            + *new TMenuItem("~W~indow copy...", cmMrBlockWindowCopy, kbAltF8, hcNoContext, "AltF8")
            + *new TMenuItem("w~i~ndow move...", cmMrBlockWindowMove, kbAltF7, hcNoContext, "AltF7")
            + newLine()
            + *new TMenuItem("mark ~L~ines of text", cmMrBlockMarkLines, kbF7, hcNoContext, "F7")
            + *new TMenuItem("mark c~O~lumns of text", cmMrBlockMarkColumns, kbShiftF7, hcNoContext, "ShiftF7")
            + *new TMenuItem("mark ~S~tream of text", cmMrBlockMarkStream, kbCtrlF7, hcNoContext, "CtrlF7")
            + *new TMenuItem("~E~nd marking", cmMrBlockEndMarking, kbF7, hcNoContext, "F7")
            + *new TMenuItem("~t~urn marking off", cmMrBlockTurnMarkingOff, kbNoKey, hcNoContext)
            + newLine()
            + *new TMenuItem("~P~ersistent blocks", cmMrBlockPersistent, kbNoKey, hcNoContext));
    }

    TSubMenu *createSearchMenu()
    {
        return &(*new TSubMenu("~S~earch", kbAltS)
            + *new TMenuItem("Search for ~t~ext...", cmMrSearchFindText, kbF5, hcNoContext, "F5")
            + *new TMenuItem("search and ~R~eplace...", cmMrSearchReplace, kbShiftF5, hcNoContext, "ShiftF5")
            + *new TMenuItem("repeat ~P~revious search", cmMrSearchRepeatPrevious, kbCtrlF5, hcNoContext, "CtrlF5")
            + newLine()
            + *new TMenuItem("p~U~sh position onto marker stack", cmMrSearchPushMarker, kbF4, hcNoContext, "F4")
            + *new TMenuItem("~G~et position from marker stack", cmMrSearchGetMarker, kbShiftF4, hcNoContext, "ShiftF4")
            + newLine()
            + *new TMenuItem("set random ~A~ccess mark...", cmMrSearchSetRandomAccessMark, kbNoKey, hcNoContext)
            + *new TMenuItem("re~T~rieve random access mark...", cmMrSearchRetrieveRandomAccessMark, kbNoKey, hcNoContext)
            + newLine()
            + *new TMenuItem("goto line ~N~umber...", cmMrSearchGotoLineNumber, kbAltF5, hcNoContext, "AltF5"));
    }

    TSubMenu *createTextMenu()
    {
        return &(*new TSubMenu("~T~ext", kbAltT)
            + *new TMenuItem("~L~ayout...", cmMrTextLayout, kbCtrlF3, hcNoContext, "CtrlF3")
            + newLine()
            + *createUpperCaseItem()
            + *createLowerCaseItem()
            + *new TMenuItem("cen~T~er line", cmMrTextCenterLine, kbNoKey, hcNoContext)
            + *new TMenuItem("time/~D~ate stamp", cmMrTextTimeDateStamp, kbNoKey, hcNoContext)
            + newLine()
            + *new TMenuItem("re-~F~ormat paragraph", cmMrTextReformatParagraph, kbAltR, hcNoContext, "AltR"));
    }

    TSubMenu *createOtherMenu()
    {
        return &(*new TSubMenu("~O~ther", kbAltO)
            + *new TMenuItem("~I~nstallation and setup", cmMrOtherInstallationAndSetup, kbNoKey, hcNoContext)
            + newLine()
            + *new TMenuItem("~K~eystroke macros...", cmMrOtherKeystrokeMacros, kbNoKey, hcNoContext)
            + newLine()
            + *new TMenuItem("~E~xecute program...", cmMrOtherExecuteProgram, kbF9, hcNoContext, "F9")
            + *new TMenuItem("find ne~X~t compiler error", cmMrOtherFindNextCompilerError, kbShiftF9, hcNoContext, "ShiftF9")
            + *new TMenuItem("~M~atch brace or paren", cmMrOtherMatchBraceOrParen, kbCtrlF5, hcNoContext, "CtrlF5")
            + newLine()
            + *new TMenuItem("~A~scii table", cmMrOtherAsciiTable, kbAltA, hcNoContext, "AltA"));
    }

    TSubMenu *createHelpMenu()
    {
        return &(*new TSubMenu("~H~elp", kbAltH)
            + *new TMenuItem("~T~able of contents", cmMrHelpContents, kbF1, hcNoContext, "F1")
            + *new TMenuItem("~K~eys", cmMrHelpKeys, kbF1, hcNoContext, "F1")
            + *new TMenuItem("detailed ~I~ndex", cmMrHelpDetailedIndex, kbShiftF1, hcNoContext, "ShiftF1")
            + *new TMenuItem("~P~revious topic", cmMrHelpPreviousTopic, kbAltF1, hcNoContext, "AltF1")
            + *new TMenuItem("~A~bout...", cmMrHelpAbout, kbNoKey, hcNoContext));
    }

    TSubMenu *createDevMenu()
    {
        return &(*new TSubMenu("De~V~", kbAltV)
            + *new TMenuItem("~R~un macro file...", cmMrDevRunMacro, kbCtrlT, hcNoContext, "CtrlT"));
    }
}

class TMREditorApp : public TApplication
{
public:
    static TMenuBar *initMRMenuBar(TRect r)
    {
        r.b.y = r.a.y + 1;

        return new TMRMenuBar(
            r,
            *createFileMenu()
                + *createEditMenu()
                + *createWindowMenu()
                + *createBlockMenu()
                + *createSearchMenu()
                + *createTextMenu()
                + *createOtherMenu()
                + *createHelpMenu()
                + *createDevMenu());
    }

    static TStatusLine *initMRStatusLine(TRect r)
    {
        r.a.y = r.b.y - 1;
        return new TMRStatusLine(
            r,
            *new TStatusDef(0, 0xFFFF)
                + *new TStatusItem("~F1~ Help", kbF1, cmMrHelpContents)
                + *new TStatusItem("~F10~ Menu", kbF10, cmMenu)
                + *new TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit));
    }

    static TDeskTop *initMRDeskTop(TRect r)
    {
        r.a.y++;
        r.b.y--;
        return new TMRDeskTop(r);
    }

    TMREditorApp() : TProgInit(&TMREditorApp::initMRStatusLine,
                               &TMREditorApp::initMRMenuBar,
                               &TMREditorApp::initMRDeskTop)
    {
        loadThemeMacro();
        createEditorWindow("?No-File?");
    }

    virtual void handleEvent(TEvent &event) override
    {
        TApplication::handleEvent(event);

        if (event.what != evCommand)
            return;

        if (handleMRCommand(event.message.command))
            clearEvent(event);
    }

    virtual TPalette &getPalette() const override
    {
        static TPalette palette(cpAppColor, sizeof(cpAppColor) - 1);
        return palette;
    }

private:
    void loadThemeMacro()
    {
        std::string macroSource = readTextFile("ui/theme.mrmac");
        if (!macroSource.empty())
        {
            size_t byteCodeSize = 0;
            unsigned char *byteCode = compile_macro_code(macroSource.c_str(), &byteCodeSize);

            if (byteCode != 0 && byteCodeSize > 0)
            {
                VirtualMachine vm;
                vm.execute(byteCode, byteCodeSize);
                std::free(byteCode);
            }
            else
            {
                const char *err = get_last_compile_error();
                if (err == 0 || *err == '\0')
                    err = "Unknown macro error.";

                messageBox(mfError | mfOKButton, "Macro Error:\n%s", err);
            }
        }
    }

    void createEditorWindow(const char *title)
    {
        TRect r = deskTop->getExtent();
        r.grow(-2, -1);
        deskTop->insert(new TMREditWindow(r, title, 1));
    }

    void runInstallationAndSetup()
    {
        bool running = true;

        while (running)
        {
            ushort result = execDialog(createInstallationAndSetupDialog());
            switch (result)
            {
            case cmMrSetupEditSettings:
                execDialog(createEditSettingsDialog());
                break;

            case cmMrSetupDisplaySetup:
                execDialog(createDisplaySetupDialog());
                break;

            case cmMrSetupColorSetup:
                runColorSetup();
                break;

            case cmMrSetupSearchAndReplaceDefaults:
                showDummyCommandBox("Installation / Search and Replace defaults");
                break;

            case cmMrSetupSaveConfigurationAndExit:
                showDummyCommandBox("Installation / Save configuration and exit");
                running = false;
                break;

            case cmCancel:
            default:
                running = false;
                break;
            }
        }
    }

    void runColorSetup()
    {
        bool running = true;

        while (running)
        {
            ushort result = execDialog(createColorSetupDialog());
            switch (result)
            {
            case cmMrColorWindowColors:
                execDialog(createWindowColorsDialog());
                break;

            case cmMrColorMenuDialogColors:
                execDialog(createMenuDialogColorsDialog());
                break;

            case cmMrColorHelpColors:
                execDialog(createHelpColorsDialog());
                break;

            case cmMrColorOtherColors:
                execDialog(createOtherColorsDialog());
                break;

            case cmCancel:
            default:
                running = false;
                break;
            }
        }
    }

    bool handleMRCommand(ushort command)
    {
        switch (command)
        {
        case cmMrWindowOpen:
            createEditorWindow("?No-File?");
            return true;

        case cmMrOtherInstallationAndSetup:
            runInstallationAndSetup();
            return true;

        case cmMrDevRunMacro:
            runMacroFileDialog();
            return true;

        default:
        {
            const char *title = dummyCommandTitle(command);
            if (title != 0)
            {
                showDummyCommandBox(title);
                return true;
            }
            return false;
        }
        }
    }
};

int main(int argc, char **argv)
{
    mrvmSetProcessContext(argc, argv);
    loadDefaultMultiEditPalette();
    TMREditorApp app;
    app.run();
    return 0;
}
