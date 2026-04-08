macro_file BRIEF;

#include MESYS.SH

/* B_WINDOW_STYLE affects the way certain window operations:
   0 = use Multi-Edit window operations
   1 = use Brief emulation window operations */
#define B_WINDOW_STYLE 1

#define LINBLOCK		1
#define COLBLOCK		2
#define STRBLOCK		3

/*****************************************************************************
														MULTI-EDIT MACRO FILE

Name:		BRIEF

Description:  This macro file contains macros to emulate a subset of common
							B.R.I.E.F. macros.  It is not our intention to completely mimic
							BRIEF, but to provide a less painful transition from BRIEF to
							Multi-Edit.

BRIEF_HELP			- Brings up BRIEF quick reference
SRC_CASE				- Mimics Brief's Search_Case macro
DELETE_M				- Mimics Brief's Delete_Macro macro
TO_BOTTO				- Mimics Brief's To_Bottom macro
TO_TOP					- Mimics Brief's To_Top macro
OPEN_LIN				- Mimics Brief's Open_Line macro
NEXT_ERR				- Mimics Brief's Next_Error 1 macro
QUOTE						- Mimics Brief's Quote macro
TOGGLE_R				- Mimics Brief's Toggle_Re macro
SEARCH_F				- Mimics Brief's Search_Fwd macro
SEARCH_B				- Mimics Brief's Search_Back macro
SEARCH_A				- Mimics Brief's Search_Again macro
TRANSLAT				- Mimics Brief's Translate macro
XLAT_BWD				- Mimics Brief's Translate_Back macro
XLAT_AGA				- Mimics Brief's Translate_Again macro
WRITE_IT				- Mimics Brief's Write_It macro
BRIEF_TAB_RIGHT - Mimics Brief's Tab key functionality
BRIEF_TAB_LEFT  - Mimics Brief's Shift Tab key functionality
CHANGE_WINDOW  	- Mimics Brief's Change_Window macro
B_DEL_CHAR			- Mimics Brief's use of the <Del> key
BUF_LIST        - Mimics Brief's Buffer list
EDIT_NEXT_BUFFER- Mimics Brief's Edit_Next_Buffer command
EDIT_PREV_BUFFER- Mimics Brief's Edit_Prev_Buffer command
B_EDIT_FILE     - Mimics Brief's Edit_File command
B_DELETE_BUFFER - Mimics Brief's Delete_Buffer command
B_CREATE_EDGE   - Mimics Brief's Create_Edge command
B_DELETE_EDGE   - Mimics Brief's Delete_Edge command

*******************************PLEASE READ THIS*******************************
NOTES ON BRIEF WINDOWING EMULATION MACROS (BUF_LIST, EDIT_NEXT_BUFFER,
EDIT_PREV_BUFFER, B_EDIT_FILE, B_DELETE_BUFFER, B_CREATE_EDGE and
B_DELETE_EDGE)

One of the most important concepts I'm trying to convey is the differences in
Multi-Edit's windowing versus BRIEF's.  I would appreciate any comments you
can give to help me make this as complete and easy-to-understand as possible.

This set of macros is designed to approximate the BRIEF-like window
operations.  IT IS NOT intended to mimic every subtle nuance of BRIEF, but to
make the transition from BRIEF to Multi-Edit more comfortable.  It is written
in such a way as to reasonably co-exist with Multi-Edit's built-in windowing
operations.

In order to successfully use this set of macros along side Multi-Edit's
built-in windowing operations, you need to understand a little about how this
macro package is doing its thing.  In BRIEF, there are 2 types of "objects";
buffers, and windows.  A buffer is approximately equivalent to a Multi-Edit
window with the exception that it cannot be viewed or edited unless it is
attached to a window.  A window in BRIEF is merely a "view port" to a buffer
that allows editing of that buffer.  Only with the "attaching" of a buffer
to a window can the user edit a file.  In Multi-Edit, there are only windows.
However, Multi-Edit windows have an attribute associated with them that allow
the macro programmer to simulate a buffer, or a view port.  Also, unlike
BRIEF, windows can overlap, even to the extent that all are full screen.  In
BRIEF, windows can only be tiled.

Probably the most serious disparity between BRIEF and Multi-Edit is that fact
that you can load the same file into more than one window (buffer).  This may
upset some former BRIEF users.  The reason we allow this is 2 fold:

	1. Our philosophy is to refrain from restricting the user from doing
		 anything that he or she wants, even if it can be dangerous.  We do,
		 however always build adequate warning mechanisms into any feature that
		 might be potentially hazardous.  By the way, this philosophy should not
		 be foreign to any C programmer who despises the "restrictive" nature of
		 other languages like Pascal and Modula 2.

	2. Multi-Edit is a CUA compliant editor.  Although the current CUA
     specification appears to be silent on this subject (I say "appears"
 		 because the documentation isn't exactly easy to decipher) we have
 		 carefully studied other CUA compliant products such as Word for Windows
		 and the Borland IDE and have concluded that there is enough conformity
		 to easily support our suspicion that this may eventually become part of
     the CUA specification.

Having laid that ground work, I'll attempt to explain how this macro package
can co-exist with Multi-Edit's built-in windowing.  Actually, there is a way
that you need not be concerned with that.  So long as you ALWAYS create and
delete windows, switch windows and buffers, and load all files using these
macros exclusively, everything will work pretty much as a former BRIEF user
would expect.  But on to reality...


Multi-Edit's Window List
Shows all windows except the "invisible" and non-editable ones, like
the cut and paste buffer, and MECONFIG.DB.  You might discover that the same
file may appear more than once in this list and that each instance is marked
as LINKED.  Most likely, at least one instance is currently visible or in the
"foreground" while others, hopefully only one, may not be visible; i.e. in the
"background".  You can think of the "background" windows as "buffers".  You
might say that the "foreground window(s)" is(are) attached to the "background
buffer".  This window list allows you to do some things that can potentially
screw things up if you are trying to emulate BRIEF behavior:

		HIDE - It's ok if you hide a "background buffer", but it could lead to
					 confusion if you hide any "foreground" window.

		DELETE - see the section titled "Multi-Edit's Menu-Window-Close or Delete
						 from the Window List"

Our BUFF_LIST is similar, except it excludes all but one instance of a
group of linked windows, thus eliminating the redundant listing of windows
with the same file.  By doing that, it only shows you a list of editable
files, and doesn't confuse you with concepts such as LINKED.

CAUTION: If you find the same file in more than one window in BUFF_LIST, it
means you have the same file loaded into 2 non-linked windows.  As this can
lead to problems, it is advisable that you either make one of them read-only
via Menu-File-Information or delete one of the windows.


Multi-Edit's Menu-Window-Close or Delete from the Window List
If you delete a "foreground" window (this always happens with Menu-Window-
Close) and it's not linked, the file will no longer exist in memory.  If it
is linked, it would be approximately equivalent to BRIEF's DELETE_EDGE.  In
either case, if it is a full screen window (i.e. not split), it will go ahead
and delete it.  If there is more than one window or buffer, this will bring a
previously background "buffer" into the foreground.  If you delete a
"background buffer" and it's not linked, it would be approximately equivalent
to BRIEF's DELETE_BUFFER.  Our B_DELETE_EDGE and B_DELETE_BUFFER is preferred.


Multi-Edit's Menu-File-Open or Menu-Window-Open
This opens a new full screen window and prompts for a file name to load.
This means that the window (viewport) you were previously looking at is now
in the background.  This concept is not compatible with BRIEF behavior.
Our B_EDIT_FILE is preferred because the new file will end up in the same
window (viewport) as the previous one.  If you happen to use one of our built-
in window open routines by mistake, not to worry.  You can get back to the
previous window (viewport) by using the next window command Menu-Window-Next.


Multi-Edit's Menu-File-Load
It is not recommended that you use this one, unless you see the linked
indicator to the right of the window letter near the upper left corner of
the window borderÄ¿
     ÚÄÄÄÄÄÄÄÄÄÄÄÄÙ
ÉÍ¸AÕÍÍÍ
º

If the linked indicator does not show, and you load a file this way, the
file you were previously editing will no longer exist in memory.  If the
linked indicator is present, the previous file will still remain (in the
background) and can be "switched" into via EDIT_NEXT_BUFFER,
EDIT_PREV_BUFFER or BUF_LIST.

Toney Robinson

							 (C) Copyright 1993 by American Cybernetics, Inc.
******************************************************************************/

#ifdef DEMO // THIS MACRO IS ONLY FOR THE DEMO VERSION
void BRIEF trans2 {

Set_Global_Str('@KEYMAP_NAME@','KN=BRIEFFN=BRIEF');
persistent_blocks = false;
Set_Global_Str('@SCREEN_SETA','/S=1/MB=1/F=1/LB=1/RB=1/BB=1/MACRO=');
Set_Global_Str('@SCREEN_SETB','/S=1/MB=1');
Set_Global_Int('DEF_SCRN_STYLE',2);
Set_Global_Int('CUR_SCRN',2);
Set_Global_Int('SPLIT_EDGE_MODE',0);
SETSCRN(0,0,0,"",0,0,0);
New_Screen;
Make_message("Brief keymap enabled.  Press <AltH> for help.");

// Default key assignments
MACRO_TO_KEY(<MEVENT2>,'MOUSE^MOUSEEVENT2 ',EDIT);
MACRO_TO_KEY(<MEVENT>,'MOUSE^MOUEVENT ',EDIT);
MACRO_TO_KEY(<ShftMEVENT>,'MOUSE^CUA_Shft_Button ',EDIT);
MACRO_TO_KEY(<CtrlMEVENT>,'MOUSE^MOUSE_BLOCK_OP /BT=1',EDIT);
MACRO_TO_KEY(<AltMEVENT>,'MOUSE^MOUSE_BLOCK_OP /BT=2',EDIT);
MACRO_TO_KEY(<ShftMEVENT2>,'MOUSE^MOUSE_SEARCH ',EDIT);
MACRO_TO_KEY(<CtrlMEVENT2>,'MOUSE^MOUSE_SEARCH /D=1',EDIT);
MACRO_TO_KEY(<AltIns>,'MEHELP^ScreenMrk ',ALL);
MACRO_TO_KEY(<AltENTER>,'MEUTIL3^USER ',EDIT);
Set_Global_Str('!MIM_KEY1', '<AltENTER>');
MACRO_TO_KEY(<ESC>,'MEUTIL3^MEMENUS ',EDIT);
Set_Global_Str('!MM_KEY', '<ESC>');
MACRO_TO_KEY(<CtrlM>,'MEUTIL3^MEMENUS ',EDIT);
MACRO_TO_KEY(<AltH>,'BRIEF^BRIEF_HELP /CX=1',EDIT);
MACRO_TO_KEY(<AltShftH>,'USERIN^MAINHELP ',EDIT);
Set_Global_Str('!HM_KEY1', '<AltShftH>');
MACRO_TO_KEY(<ShftF1>,'MEHELP /POP=1',EDIT);
FLABEL('PrevHl',11,EDIT);
Set_Global_Str('!HM_KEY3', '<ShftF1>');
MACRO_TO_KEY(<CtrlF1>,'MEHELP /EXTHELP=1/CX=1',EDIT);
FLABEL('Contxt',21,EDIT);
Set_Global_Str('!HM_KEY5', '<CtrlF1>');
MACRO_TO_KEY(<CtrlShftH>,'MEHELP /EXTHELP=1/CX=1',EDIT);
MACRO_TO_KEY(<AltE>,'BRIEF^B_EDIT_FILE /E=1',EDIT);
MACRO_TO_KEY(<CtrlF3>,'MEUTIL1^SAVEBLCK ',EDIT);
FLABEL('SaveBl',23,EDIT);
Set_Global_Str('!FM_KEY8', '<CtrlF3>');
MACRO_TO_KEY(<AltR>,'MEUTIL1^SPLICE ',EDIT);
Set_Global_Str('!FM_KEY7', '<AltR>');
MACRO_TO_KEY(<AltW>,'BRIEF^WRITE_IT /NP=1',EDIT);
Set_Global_Str('!FM_KEY4', '<AltW>');
MACRO_TO_KEY(<F8>,'BRIEF^SVFilesStatus /S=1',EDIT);
FLABEL('SavAll',8,EDIT);
MACRO_TO_KEY(<CtrlF5>,'BRIEF^SRC_CASE ',EDIT);
FLABEL('SrcCas',25,EDIT);
MACRO_TO_KEY(<CtrlF6>,'BRIEF^TOGGLE_R ',EDIT);
FLABEL('RegExp',26,EDIT);
MACRO_TO_KEY(<AltT>,'BRIEF^TRANSLAT ',EDIT);
MACRO_TO_KEY(<F6>,'BRIEF^TRANSLAT ',EDIT);
FLABEL('XltFwd',6,EDIT);
MACRO_TO_KEY(<AltF6>,'BRIEF^XLAT_BWD ',EDIT);
FLABEL('XltBwd',36,EDIT);
MACRO_TO_KEY(<ShftF6>,'BRIEF^XLAT_AGA ',EDIT);
FLABEL('RepXlt',16,EDIT);
MACRO_TO_KEY(<ShftF5>,'BRIEF^SEARCH_A ',EDIT);
FLABEL('NxtSrc',15,EDIT);
MACRO_TO_KEY(<AltF5>,'BRIEF^SEARCH_B ',EDIT);
FLABEL('SrcBck',35,EDIT);
MACRO_TO_KEY(<AltS>,'BRIEF^SEARCH_F ',EDIT);
MACRO_TO_KEY(<F5>,'BRIEF^SEARCH_F ',EDIT);
FLABEL('SrcFwd',5,EDIT);
MACRO_TO_KEY(<AltF>,'MEUTIL2^FS ',EDIT);
Set_Global_Str('!SRM_KEY5', '<AltF>');
MACRO_TO_KEY(<CtrlF>,'MEUTIL2^FS /N',EDIT);
Set_Global_Str('!SRM_KEY6', '<CtrlF>');
MACRO_TO_KEY(<CtrlShftF>,'MEUTIL2^FS /M=1',EDIT);
Set_Global_Str('!SRM_KEY7', '<CtrlShftF>');
MACRO_TO_KEY(<AltL>,'MeUtil2^MarkBlck /O=1',EDIT);
Set_Global_Str('!BM_KEY15', '<AltL>');
MACRO_TO_KEY(<AltC>,'MeUtil2^MColBlck /O=1',EDIT);
Set_Global_Str('!BM_KEY16', '<AltC>');
MACRO_TO_KEY(<AltM>,'MeUtil2^MStrBlck /O=1',EDIT);
Set_Global_Str('!BM_KEY17', '<AltM>');
MACRO_TO_KEY(<AltA>,'MeUtil2^MStrBlck /O=1',EDIT);
MACRO_TO_KEY(<CtrlF9>,'MEUTIL2^BLOCKOFF ',EDIT);
FLABEL('BlkOff',29,EDIT);
Set_Global_Str('!BM_KEY18', '<CtrlF9>');
MACRO_TO_KEY(<GREY->,'MeUtil2^CUT /M/O=1',EDIT);
Set_Global_Str('!CPM_KEY2', '<GREY->');
MACRO_TO_KEY(<INS>,'MeUtil2^PASTE /O=1',EDIT);
Set_Global_Str('!CPM_KEY5', '<INS>');
MACRO_TO_KEY(<GREY+>,'MeUtil2^CUT /O=1',EDIT);
Set_Global_Str('!CPM_KEY1', '<GREY+>');
MACRO_TO_KEY(<CtrlGREY->,'MeUtil2^CUT /M/A/O=1',EDIT);
Set_Global_Str('!CPM_KEY4', '<CtrlGREY->');
MACRO_TO_KEY(<CtrlGREY+>,'MeUtil2^CUT /A/O=1',EDIT);
Set_Global_Str('!CPM_KEY3', '<CtrlGREY+>');
MACRO_TO_KEY(<F3>,'BRIEF^B_CREATE_EDGE /T=4',EDIT);
FLABEL('SplWin',3,EDIT);
MACRO_TO_KEY(<AltF2>,'WINDOW^WINOP /T=8',EDIT);
FLABEL('Zoom',32,EDIT);
Set_Global_Str('!WM_KEY12', '<AltF2>');
MACRO_TO_KEY(<F4>,'BRIEF^B_DELETE_EDGE /T=1',EDIT);
FLABEL('Close',4,EDIT);
MACRO_TO_KEY(<F2>,'WINDOW^WINOP /T=5',EDIT);
FLABEL('Resize',2,EDIT);
Set_Global_Str('!WM_KEY11', '<F2>');
MACRO_TO_KEY(<CtrlE>,'WINDOW^WINOP /T=0',EDIT);
Set_Global_Str('!WM_KEY1', '<CtrlE>');
MACRO_TO_KEY(<AltB>,'BRIEF^BUF_LIST /T=3',EDIT);
MACRO_TO_KEY(<AltLF>,'BRIEF^CHANGE_WINDOW /DIR=1',EDIT);
MACRO_TO_KEY(<AltShftLF>,'BRIEF^CHANGE_WINDOW /DIR=1',EDIT);
MACRO_TO_KEY(<AltRT>,'BRIEF^CHANGE_WINDOW /DIR=2',EDIT);
MACRO_TO_KEY(<AltShftRT>,'BRIEF^CHANGE_WINDOW /DIR=2',EDIT);
MACRO_TO_KEY(<AltUP>,'BRIEF^CHANGE_WINDOW /DIR=3',EDIT);
MACRO_TO_KEY(<AltShftUP>,'BRIEF^CHANGE_WINDOW /DIR=3',EDIT);
MACRO_TO_KEY(<AltDN>,'BRIEF^CHANGE_WINDOW /DIR=4',EDIT);
MACRO_TO_KEY(<AltShftDN>,'BRIEF^CHANGE_WINDOW /DIR=4',EDIT);
MACRO_TO_KEY(<AltN>,'BRIEF^EDIT_NEXT_BUFFER ',EDIT);
MACRO_TO_KEY(<Alt->,'BRIEF^EDIT_PREV_BUFFER ',EDIT);
MACRO_TO_KEY(<F1>,'WINDOW^ADJACENT_WIN ',EDIT);
FLABEL('ChngWn',1,EDIT);
Set_Global_Str('!WM_KEY8', '<F1>');
MACRO_TO_KEY(<DEL>,'Brief^B_DEL_CHAR ',EDIT);
MACRO_TO_KEY(<AltK>,'DELEOL ',EDIT);
MACRO_TO_KEY(<AltBS>,'DELWORD ',EDIT);
MACRO_TO_KEY(<ShftDEL>,'DELWORD ',EDIT);
CMD_TO_KEY(<BS>,BACK_SPACE,EDIT);
MACRO_TO_KEY(<CtrlBS>,'BSWORD ',EDIT);
CMD_TO_KEY(<ShftF8>,DEL_LINE,EDIT);
FLABEL('DelLin',18,EDIT);
CMD_TO_KEY(<AltD>,DEL_LINE,EDIT);
CMD_TO_KEY(<AltU>,UNDO,EDIT);
Set_Global_Str('!TXM_KEY1', '<AltU>');
CMD_TO_KEY(<GREY*>,UNDO,EDIT);
CMD_TO_KEY(<CtrlDEL>,REDO,EDIT);
Set_Global_Str('!TXM_KEY2', '<CtrlDEL>');
MACRO_TO_KEY(<END>,'END /T=300',EDIT);
MACRO_TO_KEY(<ShftEND>,'END /T=300',EDIT);
MACRO_TO_KEY(<HOME>,'HOME /T=300',EDIT);
MACRO_TO_KEY(<ShftHOME>,'HOME /T=300',EDIT);
MACRO_TO_KEY(<CtrlB>,'BRIEF^TO_BOTTO ',EDIT);
MACRO_TO_KEY(<CtrlT>,'BRIEF^TO_TOP ',EDIT);
CMD_TO_KEY(<LF>,LEFT,EDIT);
CMD_TO_KEY(<RT>,RIGHT,EDIT);
CMD_TO_KEY(<UP>,UP,EDIT);
CMD_TO_KEY(<DN>,DOWN,EDIT);
CMD_TO_KEY(<PgUp>,PAGE_UP,EDIT);
CMD_TO_KEY(<PgDn>,PAGE_DOWN,EDIT);
CMD_TO_KEY(<CtrlPgUp>,TOF,EDIT);
CMD_TO_KEY(<CtrlPgDn>,EOF,EDIT);
CMD_TO_KEY(<AltPgDn>,NEXT_PAGE_BREAK,EDIT);
Set_Global_Str('!NEXT_PB_KEY', '<AltPgDn>');
CMD_TO_KEY(<AltPgUp>,LAST_PAGE_BREAK,EDIT);
Set_Global_Str('!LAST_PB_KEY', '<AltPgUp>');
CMD_TO_KEY(<CtrlLF>,WORD_LEFT,EDIT);
CMD_TO_KEY(<CtrlRT>,WORD_RIGHT,EDIT);
MACRO_TO_KEY(<CtrlHOME>,'TOPWIN ',EDIT);
MACRO_TO_KEY(<CtrlEND>,'BOTWIN ',EDIT);
MACRO_TO_KEY(<CtrlU>,'SCROLLUP ',EDIT);
MACRO_TO_KEY(<CtrlD>,'SCROLLDN ',EDIT);
MACRO_TO_KEY(<AltF8>,'GOTOLINE ',EDIT);
FLABEL('GotoLn',38,EDIT);
Set_Global_Str('!CRSM_KEY7', '<AltF8>');
MACRO_TO_KEY(<AltG>,'GOTOLINE ',EDIT);
MACRO_TO_KEY(<TAB>,'BRIEF^BRIEF_TAB_RIGHT ',EDIT);
MACRO_TO_KEY(<ShftTAB>,'BRIEF^BRIEF_TAB_LEFT ',EDIT);
CMD_TO_KEY(<AltTAB>,INDENT,EDIT);
Set_Global_Str('!IND_KEY', '<AltTAB>');
CMD_TO_KEY(<AltShftTAB>,UNDENT,EDIT);
Set_Global_Str('!UND_KEY', '<AltShftTAB>');
MACRO_TO_KEY(<AltF7>,'TEXT^MARKPOS ',EDIT);
FLABEL('Mark',37,EDIT);
Set_Global_Str('!CRSM_KEY1', '<AltF7>');
MACRO_TO_KEY(<CtrlF7>,'TEXT^GOTOMARK ',EDIT);
FLABEL('GotoMk',27,EDIT);
Set_Global_Str('!CRSM_KEY2', '<CtrlF7>');
MACRO_TO_KEY(<Alt1>,'TEXT^SET_MARK 1',EDIT);
MACRO_TO_KEY(<Alt2>,'TEXT^SET_MARK 2',EDIT);
MACRO_TO_KEY(<Alt3>,'TEXT^SET_MARK 3',EDIT);
MACRO_TO_KEY(<Alt4>,'TEXT^SET_MARK 4',EDIT);
MACRO_TO_KEY(<Alt5>,'TEXT^SET_MARK 5',EDIT);
MACRO_TO_KEY(<Alt6>,'TEXT^SET_MARK 6',EDIT);
MACRO_TO_KEY(<Alt7>,'TEXT^SET_MARK 7',EDIT);
MACRO_TO_KEY(<Alt8>,'TEXT^SET_MARK 8',EDIT);
MACRO_TO_KEY(<Alt9>,'TEXT^SET_MARK 9',EDIT);
MACRO_TO_KEY(<Alt0>,'TEXT^SET_MARK 10',EDIT);
MACRO_TO_KEY(<AltJ>,'TEXT^GET_MARK ',EDIT);
MACRO_TO_KEY(<ShftLF>,'SHIFT_CURSOR ',EDIT);
MACRO_TO_KEY(<ShftUP>,'SHIFT_CURSOR ',EDIT);
MACRO_TO_KEY(<ShftRT>,'SHIFT_CURSOR ',EDIT);
MACRO_TO_KEY(<ShftDN>,'SHIFT_CURSOR ',EDIT);
MACRO_TO_KEY(<ShftPGUP>,'SHIFT_CURSOR ',EDIT);
MACRO_TO_KEY(<ShftPGDN>,'SHIFT_CURSOR ',EDIT);
MACRO_TO_KEY(<ShftEND>,'SHIFT_CURSOR ',EDIT);
MACRO_TO_KEY(<ShftHOME>,'SHIFT_CURSOR ',EDIT);
MACRO_TO_KEY(<CtrlShftRT>,'SHIFT_CURSOR ',EDIT);
MACRO_TO_KEY(<CtrlShftLF>,'SHIFT_CURSOR ',EDIT);
MACRO_TO_KEY(<CtrlShftUP>,'SHIFT_CURSOR ',EDIT);
MACRO_TO_KEY(<CtrlShftDN>,'SHIFT_CURSOR ',EDIT);
MACRO_TO_KEY(<AltShftR>,'TEXT^RULER ',EDIT);
Set_Global_Str('!TXM_KEY9', '<AltShftR>');
MACRO_TO_KEY(<CtrlENTER>,'BRIEF^OPEN_LIN ',EDIT);
MACRO_TO_KEY(<AltShftF>,'TEXT^REFORMAT ',EDIT);
Set_Global_Str('!TXM_KEY11', '<AltShftF>');
MACRO_TO_KEY(<AltShftJ>,'TEXT^JUSTIFY ',EDIT);
Set_Global_Str('!TXM_KEY12', '<AltShftJ>');
MACRO_TO_KEY(<AltShftU>,'TEXT^UNJUSTIF ',EDIT);
Set_Global_Str('!TXM_KEY13', '<AltShftU>');
MACRO_TO_KEY(<CtrlShftU>,'MENU /MN=Upper_Case/U=1',EDIT);
Set_Global_Str('!TXM_KEY5', '<CtrlShftU>');
MACRO_TO_KEY(<CtrlShftL>,'MENU /MN=Lower_Case',EDIT);
Set_Global_Str('!TXM_KEY6', '<CtrlShftL>');
MACRO_TO_KEY(<ShftF2>,'DATETIME ',EDIT);
FLABEL('DatTim',12,EDIT);
Set_Global_Str('!TXM_KEY8', '<ShftF2>');
MACRO_TO_KEY(<ENTER>,'CR ',EDIT);
MACRO_TO_KEY(<GreyENTER>,'BRIEF^B_GreyENTER ',EDIT);
MACRO_TO_KEY(<AltI>,'INSTGL ',EDIT);
MACRO_TO_KEY(<AltSPACE>,'SPACE ',EDIT);
MACRO_TO_KEY(<CtrlSPACE>,'SPACE ',EDIT);
MACRO_TO_KEY(<ShftF9>,'BRIEF^DELETE_M ',EDIT);
FLABEL('DelMac',19,EDIT);
MACRO_TO_KEY(<F9>,'MEUTIL3^LOADMACRO ',EDIT);
FLABEL('LdMac',9,EDIT);
MACRO_TO_KEY(<F10>,'MEUTIL1^RUNMAC ',EDIT);
FLABEL('RunMac',10,EDIT);
Set_Global_Str('!MCM_KEY1', '<F10>');
CMD_TO_KEY(<F7>,KEY_RECORD,EDIT);
FLABEL('Rememb',7,EDIT);
Set_Global_Str('!RECORD_KEY', '<F7>');
CMD_TO_KEY(<F7>,KEY_RECORD,DOS_SHELL);
FLABEL('Rememb',7,DOS_SHELL);
MACRO_TO_KEY(<AltShftN>,'NOTEBOOK ',ALL);
Set_Global_Str('!NB_KEY', '<AltShftN>');
MACRO_TO_KEY(<AltV>,'VCS ',EDIT);
Set_Global_Str('!VCS_KEY1', '<AltV>');
MACRO_TO_KEY(<AltShftP>,'DB /F=PHONE.DB/SRP=1/LT=Phone List/DT=Phone list:  Edit entry',EDIT);
MACRO_TO_KEY(<AltShftA>,'SUPPORT^ASCII ',ALL);
Set_Global_Str('!MIM_KEY10', '<AltShftA>');
MACRO_TO_KEY(<CtrlW>,'BRIEF^SET_BACK ',EDIT);
MACRO_TO_KEY(<AltQ>,'BRIEF^QUOTE ',EDIT);
MACRO_TO_KEY(<AltP>,'MEUTIL3^PRINT ',EDIT);
MACRO_TO_KEY(<CtrlShftA>,'SUPPORT^ASCII ',EDIT);
Set_Global_Str('!MIM_KEY10', '<CtrlShftA>');
MACRO_TO_KEY(<AltF4>,'SUPPORT^CONDENSE ',EDIT);
FLABEL('Condns',34,EDIT);
Set_Global_Str('!MIM_KEY11', '<AltF4>');
MACRO_TO_KEY(<CtrlG>,'SUPPORT^CONDENSE ',EDIT);
MACRO_TO_KEY(<CtrlF4>,'DIRSHELL^DIRSHELL ',EDIT);
FLABEL('DIR',24,EDIT);
Set_Global_Str('!FM_KEY10', '<CtrlF4>');
MACRO_TO_KEY(<AltZ>,'MEUTIL1^SHELLDOS ',EDIT);
Set_Global_Str('!MIM_KEY12', '<AltZ>');
MACRO_TO_KEY(<AltF10>,'LANGUAGE^COMPILE ',EDIT);
FLABEL('Compil',40,EDIT);
Set_Global_Str('!MIM_KEY4', '<AltF10>');
MACRO_TO_KEY(<CtrlF8>,'LANGUAGE^COMPILE ',EDIT);
FLABEL('Compil',28,EDIT);
MACRO_TO_KEY(<CtrlP>,'BRIEF^NEXT_ERR ',EDIT);
MACRO_TO_KEY(<CtrlN>,'LANGUAGE^CMPERROR ',EDIT);
Set_Global_Str('!MIM_KEY5', '<CtrlN>');
MACRO_TO_KEY(<CtrlF2>,'LANGUAGE^CMPERROR ',EDIT);
FLABEL('NxtErr',22,EDIT);
MACRO_TO_KEY(<AltF9>,'LANGUAGE^TEMPLATE ',EDIT);
FLABEL('Templt',39,EDIT);
Set_Global_Str('!MIM_KEY6', '<AltF9>');
MACRO_TO_KEY(<AltShftT>,'LANGUAGE^TEMPLATE ',EDIT);
MACRO_TO_KEY(<AltShftM>,'LANGUAGE^MATCH ',EDIT);
Set_Global_Str('!MIM_KEY7', '<AltShftM>');
MACRO_TO_KEY(<CtrlShftF2>,'CALC ',EDIT);
Set_Global_Str('!MIM_KEY9', '<CtrlShftF2>');
MACRO_TO_KEY(<AltShftC>,'CALC ',EDIT);
MACRO_TO_KEY(<CtrlShftC>,'DOCUMENT^FCMD ',EDIT);
Set_Global_Str('!LM_KEY10', '<CtrlShftC>');
MACRO_TO_KEY(<AltShftL>,'SUPPORT^LINEDRAW ',EDIT);
Set_Global_Str('!MIM_KEY13', '<AltShftL>');
MACRO_TO_KEY(<AltX>,'EXIT^EXIT ',EDIT);
Set_Global_Str('!FM_KEY11', '<AltX>');
MACRO_TO_KEY(<AltShftK>,'SETUP^KEYCODE ',EDIT);
MACRO_TO_KEY(<AltF1>,'XZOOM ',EDIT);
FLABEL('TglScr',31,EDIT);
Set_Global_Str('!TOG_SCR_FORM', '<AltF1>');
MACRO_TO_KEY(<CtrlR>,'MEUTIL1^REPEAT ',EDIT);
Set_Global_Str('!TXM_KEY15', '<CtrlR>');
MACRO_TO_KEY(<AltShftS>,'SPELL^SPELL ',EDIT);
Set_Global_Str('!MIM_KEY16', '<AltShftS>');

if (global_int('!EVgDialectId')) {
	RM("EV_");
	RM('EVINIT');  // INIT EVOLVE IF IT WAS ENABLED BEFORE
}

}
#endif // DEMO

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		BRIEF_HELP

Description:  Brings up BRIEF quick reference

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro BRIEF_HELP TRANS
{
	rm('MeHelp  '+MParm_Str+'/F=BRIEF/L=*');
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		SRC_CASE

Description:  Mimics Brief's Search_Case macro

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro SRC_CASE TRANS
{
	str		Switches=Caps(Global_Str('Switches')),
				REPL_Switches=Caps(Global_Str('REPL_Switches')),
				TStr[3];

	if ( 0!=(Return_Int=XPos('I',Switches,1)) )
	{
		while ( 0!=(Return_Int=XPos('I',Switches,1)) )
			Switches=Str_Del(Switches,Return_Int,1);
		while ( 0!=(Return_Int=XPos('I',REPL_Switches,1)) )
			REPL_Switches=Str_Del(REPL_Switches,Return_Int,1);
		TStr='on';
	}
	else
	{
		Switches=Switches+'I';
		if ( !XPos('I',REPL_Switches,1) )
			REPL_Switches=REPL_Switches+'I';
		TStr='off';
	}

	Set_Global_Str('Switches',Switches);
	Set_Global_Str('REPL_Switches',REPL_Switches);
	Make_Message('Case sensitivity '+TStr+'.');
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		DELETE_M

Description:  Mimics Brief's Delete_Macro macro.  A significant difference is
							that BRIEF can only delete entire macro files from memory, ME
							can delete individual macros

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro DELETE_M TRANS
{
	Set_Global_Str('!B_IPARM_1','/C=1/L=1/W=16/H=MC/T=Macro to unload:');
	RM( 'USERIN^DATA_IN /PRE=!B_/#=1/T=/X=5/Y=3');
	if ( Return_Int )
		UnLoad_Macro(Global_Str('!B_ISTR_1'));
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		TO_BOTTO

Description:  Mimics Brief's To_Bottom macro

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro TO_BOTTO TRANS
{
	int Ln,OldRefresh=Refresh;

	Refresh=False;
	Ln = C_Line;
	RM('BOTWIN');
	Goto_Line(Ln);
	Refresh=OldRefresh;
	Redraw;
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		TO_TOP

Description:  Mimics Brief's To_Top macro

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro TO_TOP TRANS
{
	int Ln,OldRefresh=Refresh;

	Refresh=False;
	Ln = C_Line;
	RM('TOPWIN');
	Goto_Line(Ln);
	Refresh=OldRefresh;
	Redraw;
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		OPEN_LIN

Description:  Mimics Brief's Open_Line macro

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro OPEN_LIN TRANS
{
	int Temp_Insert_Mode;
	Temp_Insert_Mode = Insert_Mode;
	Insert_Mode = True;
	Eol;
//	Cr;
	RM("CR");
	Insert_Mode = Temp_Insert_Mode;
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		NEXT_ERR

Description:  Mimics BRIEF's Next_Error 1 macro

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro NEXT_ERR TRANS
{
  int CWin=Cur_Window,i,ff;

	Refresh = False;
	for ( i=0;(i<Window_Count)&&(ff=(Caps(Truncate_Path(File_Name))!=(User_Id+'MEERR.TMP'))); i++ )
		Switch_Window(Cur_Window+1);
	if ( ff )
	{
		Make_Message('No error file loaded.');
  	Switch_Window(CWin);
	}
	else
	{
		int EWin=Cur_Window,Menu=Menu_Create,LWin,tRO;

		Create_Window;
		tRO = Read_Only;
		Read_Only = 1;
		Link_Window(EWin);
		Goto_Col(1);
    Menu_Set_Item(Menu,1,'Errors:','','/HT=16/W='+Str(Screen_Width-5)+'/L=1/C=1/WIN='+Str(LWin=Cur_Window),15,0,0);
		Return_Int=Menu;
    RM('USERIN^DATA_IN /HN=1/#=1/T=Compiler errors');
		if ( Return_Int )
		{
			int Cl=C_Line;

			Delete_Window;
			Switch_Window(EWin);
			Goto_Line(Cl);
			Up; Eol;
			rm('Language^CmpError');
		}
		else
		{
			Delete_Window;
			Switch_Window(CWin);
		}
		Read_Only = tRO;
		Menu_Delete(Menu);

	}
	Refresh = True;
	Redraw;
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		QUOTE

Description:  Mimics Brief's Quote macro

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro QUOTE TRANS
{
	Read_Key;
	if(  (Key1 != 0)  )
		Text(Char(Key1));
	else
		Pass_Key(Key1,Key2);
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		TOGGLE_R

Description:  Mimics Brief's Toggle_Re macro

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro TOGGLE_R TRANS
{
	str		Switches=Caps(Global_Str('Switches')),
				REPL_Switches=Caps(Global_Str('REPL_Switches')),
				Tstr[3];

	if ( 0!=(Return_Int=XPos('X',Switches,1)) )
	{
		while ( 0!=(Return_Int=XPos('X',Switches,1)) )
			Switches=Str_Del(Switches,Return_Int,1);
		while ( 0!=(Return_Int=XPos('X',REPL_Switches,1)) )
			REPL_Switches=Str_Del(REPL_Switches,Return_Int,1);
		TStr='on';
	}
	else
	{
		Switches=Switches+'X';
		if ( !XPos('X',REPL_Switches,1) )
			REPL_Switches=REPL_Switches+'X';
		TStr='off';
	}
	Set_Global_Str('Switches',Switches);
	Set_Global_Str('REPL_Switches',REPL_Switches);
	Make_Message('Regular expressions '+TStr+'.');
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		SEARCH_F

Description:  Mimics Brief's Search_Fwd macro

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro SEARCH_F TRANS
{
	int OldRefresh=Refresh;
	str	Switches=Caps(Global_Str('Switches'));

	while ( 0!=(Return_Int=XPos('B',Switches,1)) )
		Switches=Str_Del(Global_Str('Switches'),Return_Int,1);

	if ( Block_Stat )
	{
		int	tblx=Block_LineX,tbcx=Block_ColX,
				tbs=Block_Stat,tm=Marking;

		if ( Block_Line1==tblx )
			if ( ((Block_Stat==1)&&(Block_Line1!=Block_Line2))||
					((Block_Stat!=1)&&(Block_Col1==tbcx)) )
			{
				tblx=Block_Line2;
				tbcx=Block_Col2;
			}

		if ( !XPos('R',Switches,1) )
			Switches=Switches+'R';
		if ( !XPos('G',Switches,1) )
			Switches=Switches+'G';

		Call DoSearch;
		if ( Global_Int('Found_Count') )
		{
			Block_LineX=tblx;
			Block_ColX=tbcx;
		}
		Block_Stat=tbs;
		Marking=tm;
		Refresh=True;
		Goto_Line(C_Line);
		Goto_Col(C_Col);
		Redraw;
	}
	else
	{
		while ( 0!=(Return_Int=XPos('R',Switches,1)) )
			Switches=Str_Del(Switches,Return_Int,1);

		while ( 0!=(Return_Int=XPos('G',Switches,1)) )
			Switches=Str_Del(Switches,Return_Int,1);

		Call DoSearch;
	}
	Goto Exit;

DoSearch:
	Set_Global_Str('Switches',Switches);
	Set_Global_Int('Search_Mode',0);
	rm('MeUtil2^S_And_R');
	Set_Global_Int('RepSearch',0);
	ret;

Exit:
	Refresh=OldRefresh;
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		SEARCH_B

Description:  Mimics Brief's Search_Back macro

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro SEARCH_B TRANS
{
	int	OldRefresh=Refresh;
	str Switches=Caps(Global_Str('Switches'));

	if ( !XPos('B',Switches,1) )
		Switches=Switches+'B';

	if ( Block_Stat )
	{
		int	tblx=Block_LineX,tbcx=Block_ColX,
				tbs=Block_Stat,tm=Marking;

		if ( Block_Line2==tblx )
			if ( ((Block_Stat==1)&&(Block_LIne1!=Block_Line2))||
					((Block_Stat!=1)&&(Block_Col2==tbcx)) )
			{
				tblx=Block_Line1;
				tbcx=Block_Col1;
			}

		if ( !XPos('R',Switches,1) )
			Switches=Switches+'R';
		if ( !XPos('G',Switches,1) )
			Switches=Switches+'G';

		Call DoSearch;
		if ( Global_Int('Found_Count') )
		{
			Block_LineX=tblx;
			Block_ColX=tbcx;
		}
		Block_Stat=tbs;
		Marking=tm;
		Refresh=True;
		Goto_Line(C_Line);
		Goto_Col(C_Col);
		Redraw;
	}
	else
	{
		while ( 0!=(Return_Int=XPos('R',Switches,1)) )
			Switches=Str_Del(Switches,Return_Int,1);

		while ( 0!=(Return_Int=XPos('G',Switches,1)) )
			Switches=Str_Del(Switches,Return_Int,1);

		Call DoSearch;
	}
	Goto Exit;

DoSearch:
	Set_Global_Str('Switches',Switches);
	Set_Global_Int('Search_Mode',0);
	Set_Global_Int('RepSearch',0);
	rm('MeUtil2^S_And_R');
	ret;

Exit:
	Refresh=OldRefresh;
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		SEARCH_A

Description:  Mimics Brief's Search_Again macro

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro SEARCH_A TRANS
{
	int	OldRefresh=Refresh;
	str Switches=Caps(Global_Str('Switches'));

	if ( Block_Stat )
	{
		int	tblx=Block_LineX,tbcx=Block_ColX,
				tbs=Block_Stat,tm=Marking;

		if ( XPos('B',Switches,1) )
		{
			if ( Block_Line2==tblx )
				if ( ((Block_Stat==1)&&(Block_LIne1!=Block_Line2))||
						((Block_Stat!=1)&&(Block_Col2==tbcx)) )
				{
					tblx=Block_Line1;
					tbcx=Block_Col1;
				}
		}
		else
		{
			if ( Block_Line1==tblx )
				if ( ((Block_Stat==1)&&(Block_Line1!=Block_Line2))||
						((Block_Stat!=1)&&(Block_Col1==tbcx)) )
				{
					tblx=Block_Line2;
					tbcx=Block_Col2;
				}
		}

		if ( !XPos('R',Switches,1) )
			Switches=Switches+'R';
		if ( !XPos('G',Switches,1) )
			Switches=Switches+'G';

		Call DoSearch;
		if ( Return_Int && Global_Int('Found_Count') )
		{
			Block_LineX=tblx;
			Block_ColX=tbcx;
		}
		Block_Stat=tbs;
		Marking=tm;
		Refresh=True;
		Goto_Line(C_Line);
		Goto_Col(C_Col);
		Redraw;
	}
	else
	{
		while ( 0!=(Return_Int=XPos('R',Switches,1)) )
			Switches=Str_Del(Switches,Return_Int,1);

		while ( 0!=(Return_Int=XPos('G',Switches,1)) )
			Switches=Str_Del(Switches,Return_Int,1);

		Call DoSearch;
	}
	Goto Exit;

DoSearch:
	Set_Global_Str('Switches',Switches);
	Set_Global_Int('Search_Mode',0);
	Set_Global_Int('REPSearch',1);
	Mark_Pos;
	if ( XPos('B',Switches,1) )
		Left;
	else
	{
		Right;
		while ( !At_EOL && (Cur_Char=='|255') )
			Right;
	}
	rm('MeUtil2^S_AND_R');
	if ( Return_Int && !Global_Int('Found_Count') )
		Goto_Mark;
	else
		Pop_Mark;
	ret;

Exit:
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:	 TRANSLAT

Description:  Mimics Brief's Translate macro

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro TRANSLAT TRANS
{
	int OldRefresh=Refresh;
	str	REPL_Switches=Caps(Global_Str('REPL_Switches'));

	Push_Undo;
	Mark_Pos;

	if ( 0!=(Return_Int=XPos('B',REPL_Switches,1)) )
		REPL_Switches=Str_Del(REPL_Switches,Return_Int,1);

	if ( Block_Stat )
	{
		int	tblx=Block_LineX,tbcx=Block_ColX,
				tbs=Block_Stat,tm=Marking;

		if ( Block_Line1==tblx )
			if ( ((Block_Stat==1)&&(Block_Line1!=Block_Line2))||
					((Block_Stat!=1)&&(Block_Col1==tbcx)) )
			{
				tblx=Block_Line2;
				tbcx=Block_Col2;
			}

		if ( !XPos('R',REPL_Switches,1) )
			REPL_Switches=REPL_Switches+'R';
		if ( !XPos('G',REPL_Switches,1) )
			REPL_Switches=REPL_Switches+'G';

		Call DoSearch;
		if ( Return_Int && Global_Int('Found_Count') )
		{
			Block_LineX=tblx;
			Block_ColX=tbcx;
		}
		Block_Stat=tbs;
		Marking=tm;
		Refresh=True;
		Goto_Line(C_Line);
		Goto_Col(C_Col);
		Redraw;
	}
	else
	{
		while ( 0!=(Return_Int=XPos('R',REPL_Switches,1)) )
			REPL_Switches=Str_Del(REPL_Switches,Return_Int,1);

		while ( 0!=(Return_Int=XPos('G',REPL_Switches,1)) )
			REPL_Switches=Str_Del(REPL_Switches,Return_Int,1);

		Call DoSearch;
	}
	Goto Exit;
DoSearch:
	Set_Global_Str('REPL_Switches',REPL_Switches);
	Set_Global_Int('Search_Mode',1);
	Set_Global_Int('RepSearch',0);
	rm('MeUtil2^S_And_R');
	ret;

Exit:
	switch ( Return_Int )
	{
		case 0 :
		case 3 :
			Pop_Mark;
			break;

		case 1 :
		case 2 :
		case 4 :
			Goto_Mark;
			break;
	}
	Pop_Undo;
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		XLAT_BWD

Description:  Mimics Brief's Translate_Back macro

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro XLAT_BWD TRANS
{
	int OldRefresh=Refresh;
	str	REPL_Switches=Caps(Global_Str('REPL_Switches'));

	Push_Undo;
	Mark_Pos;

	if ( !XPos('B',REPL_Switches,1) )
		REPL_Switches=REPL_Switches+'B';

	if ( Block_Stat )
	{
		int	tblx=Block_LineX,tbcx=Block_ColX,
				tbs=Block_Stat,tm=Marking;

		if ( Block_Line2==tblx )
			if ( ((Block_Stat==1)&&(Block_LIne1!=Block_Line2))||
					((Block_Stat!=1)&&(Block_Col2==tbcx)) )
			{
				tblx=Block_Line1;
				tbcx=Block_Col1;
			}

		if ( !XPos('R',REPL_Switches,1) )
			REPL_Switches=REPL_Switches+'R';
		if ( !XPos('G',REPL_Switches,1) )
			REPL_Switches=REPL_Switches+'G';

		Call DoSearch;
		if ( Return_Int && Global_Int('Found_Count') )
		{
			Block_LineX=tblx;
			Block_ColX=tbcx;
		}
		Block_Stat=tbs;
		Marking=tm;
		Refresh=True;
		Goto_Line(C_Line);
		Goto_Col(C_Col);
		Redraw;
	}
	else
	{
		while ( 0!=(Return_Int=XPos('R',REPL_Switches,1)) )
			REPL_Switches=Str_Del(REPL_Switches,Return_Int,1);

		while ( 0!=(Return_Int=XPos('G',REPL_Switches,1)) )
			REPL_Switches=Str_Del(REPL_Switches,Return_Int,1);

		Call DoSearch;
	}
	Goto Exit;

DoSearch:
	Set_Global_Str('REPL_Switches',REPL_Switches);
	Set_Global_Int('Search_Mode',1);
	Set_Global_Int('RepSearch',0);
	rm('MeUtil2^S_And_R');
	ret;

Exit:
	switch ( Return_Int )
	{
		case 0 :
		case 3 :
			Pop_Mark;
			break;

		case 1 :
		case 2 :
		case 4 :
			Goto_Mark;
			break;
	}
	Pop_Undo;
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		XLAT_AGA

Description:  Mimics Brief's Translate_Again macro

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro XLAT_AGA TRANS
{
	int OldRefresh=Refresh;
	str	REPL_Switches=Caps(Global_Str('REPL_Switches'));

	Push_Undo;
	Mark_Pos;
	if ( Block_Stat )
	{
		int	tblx=Block_LineX,tbcx=Block_ColX,
				tbs=Block_Stat,tm=Marking;

		if ( XPos('B',REPL_Switches,1) )
		{
			if ( Block_Line2==tblx )
				if ( ((Block_Stat==1)&&(Block_LIne1!=Block_Line2))||
						((Block_Stat!=1)&&(Block_Col2==tbcx)) )
				{
					tblx=Block_Line1;
					tbcx=Block_Col1;
				}
		}
		else
		{
			if ( Block_Line1==tblx )
				if ( ((Block_Stat==1)&&(Block_Line1!=Block_Line2))||
						((Block_Stat!=1)&&(Block_Col1==tbcx)) )
				{
					tblx=Block_Line2;
					tbcx=Block_Col2;
				}
		}

		if ( !XPos('R',REPL_Switches,1) )
			REPL_Switches=REPL_Switches+'R';
		if ( !XPos('G',REPL_Switches,1) )
			REPL_Switches=REPL_Switches+'G';

		Call DoSearch;
		if ( Return_Int && Global_Int('Found_Count') )
		{
			Block_LineX=tblx;
			Block_ColX=tbcx;
		}
		Block_Stat=tbs;
		Marking=tm;
		Refresh=True;
		Goto_Line(C_Line);
		Goto_Col(C_Col);
		Redraw;

	}
	else
	{
		while ( 0!=(Return_Int=XPos('R',REPL_Switches,1)) )
			REPL_Switches=Str_Del(REPL_Switches,Return_Int,1);

		while ( 0!=(Return_Int=XPos('G',REPL_Switches,1)) )
			REPL_Switches=Str_Del(REPL_Switches,Return_Int,1);

		Call DoSearch;
	}
	Goto Exit;

DoSearch:
	Set_Global_Str('REPL_Switches',REPL_Switches);
	Set_Global_Int('Search_Mode',1);
	Set_Global_Int('REPSearch',1);
	Mark_Pos;
	if ( XPos('B',REPL_Switches,1) )
		Left;
	else
	{
		Right;
		while ( !At_EOL && (Cur_Char=='|255') )
			Right;
	}
	rm('MeUtil2^S_AND_R');
	if ( Return_Int && !Global_Int('Found_Count') )
		Goto_Mark;
	else
		Pop_Mark;
	ret;

Exit:
	Refresh=OldRefresh;
	switch ( Return_Int )
	{
		case 0 :
		case 3 :
			Pop_Mark;
			break;

		case 1 :
		case 2 :
		case 4 :
			Goto_Mark;
			break;
	}
	Pop_Undo;
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:	 WRITE_IT

Description:  Mimics Brief's Write_It macro

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro WRITE_IT TRANS
{
	if ( Block_Stat )
	{
		RM('MEUTIL1^SAVEBLCK');
		if ( Return_Int )
			Block_Off;
	}
	else
		RM('MEUTIL1^SAVEFILE ' + MParm_Str);
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:	 BRIEF_TAB_RIGHT;

Description:  Mimics Brief's Tab key functionality

							(C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro BRIEF_TAB_RIGHT
{
	if ( Block_Stat )
	{
		int EndLine=Block_Line2,ti=Insert_Mode,tr=Refresh;

		Working;
		if ( !Persistent_Blocks )
			Marking = 1;
		Push_Undo;
		if ( (Block_Stat==3) && (Block_Col2<1) )
			--EndLine;
		Insert_Mode=True;
		Refresh=False;
		Mark_Pos;
		Goto_Line(Block_Line1);
		while ( C_Line<=EndLine )
		{
			if ( 2==Block_Stat )
				Goto_Col(Block_Col1);
			else
				First_Word;
			if ( NOT(At_Eol) )
				Tab_Right;
			Down;
		}
		Insert_Mode=ti;
		Goto_Mark;
		Pop_Undo;
		Refresh=tr;
	}
	else
		Tab_Right;
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:	 BRIEF_TAB_LEFT;

Description:  Mimics Brief's Shift Tab key functionality

							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro BRIEF_TAB_LEFT
{
	if ( Block_Stat )
	{
		int i,tr=Refresh,ti=Insert_Mode,EndLine=Block_Line2;

		Working;
		if ( !Persistent_Blocks )
			Marking = 1;
		Push_Undo;
		if ( (Block_Stat==3) && (Block_Col2<1) )
			--EndLine;
		Refresh = False;
		Mark_Pos;
		Goto_Line(Block_Line1);
		if ( 2==Block_Stat )
		{
			int j;

			Goto_Col(Block_Col1);
			Insert_Mode=False;
			Tab_Right;
			j = C_Col-Block_COl1;
			while ( C_Line<=EndLine )
			{
				Goto_Col(Block_Col1);
				for ( i=j; i && XPos(Cur_Char,' |9',1) && !At_EOF; i-- )
					Del_Char;
				Down;
			}
		}
		else
		{
			while ( C_Line<=EndLine )
			{
				First_Word;
				i=C_Col;
				Tab_Left;
				if ( !At_EOL )
					Del_Chars(i-C_Col);
				Down;
			}
		}

		Goto_Mark;
		Pop_Undo;
		Refresh=tr;
		Insert_Mode=ti;
	}
	else
		Tab_Left;
}

macro CHANGE_WINDOW
{
	int Direction;

	Direction = Parse_Int('/DIR=',MParm_Str);
	switch ( Direction )
	{
		case 1 :
			Push_Key(0,75);
			break;

		case 2 :
			Push_Key(0,77);
			break;

		case 3 :
			Push_Key(0,72);
			break;

		case 4 :
			Push_Key(0,80);
			break;
	}
	RM('WINDOW^ADJACENT_WIN');
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:	 B_Del_Char;

Description:  Mimics Brief's use of the <Del> key.  If no block is marked
							then the character after the cursor is deleted.  If a block
							is marked then the marked block is deleted.


							 (C) Copyright 1992 by American Cybernetics, Inc.
******************************************************************************/
macro B_Del_Char
{
	if ( Block_Stat )
	{
		Delete_Block;
		Make_Message("Block deleted.");
	}
	else
		Del_Char;
}

/*****************************************************************************
																MULTI-EDIT MACRO

Name:		SetGlobalBkMrk

Description:	Uses  random   access  markers   and  global   variables   to
							implement GLOBAL book marks.   If SetGlobalBkMrk is  executed
							without parameters, the  user is prompted  to press a  number
							key (0-9, 0=10) to specify the marker #.  If a value of  1-10
							is passed  as a  parameter, then  the specified  mark is  set
							without prompting the user.

Parameters:		MParm_Str can contain  1-10 to denote  going directly to  the
							mark.   If  the  entire  MParm_Str  does  not contain numeric
							characters, this option will not  work, and the menu will  be
							invoked.     Under   these   circumstances,   the   following
							parameters  are  expected:   /X=	 The  X  coordinate for the
							menu /X=		The  X coordinate  for the  menu /BC=	The  amount
							of boxes which need to be killed upon exit

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
macro SetGlobalBkMrk {
	int jx, bc;

	/* If a numeric parameter was passed then use it, and bypass the prompting */
	if ( ( Val(jx, MParm_Str) == 0 ) ) {
		if ( ((jx > 0) & (jx < 11)) ) {
			goto do_set;
		}
	}

	RM('USERIN^XMENU /B=1/L=Select global marker # to set/X='+parse_str('/X=',
			mparm_str)+'/Y='+parse_str('/Y=', mparm_str)+'/M=1 (PMRANDOM)2 ()3 ()4'+
			' ()5 ()6 ()7 ()8 ()9 ()0 ()');

	if ( return_int <= 0 ) {
		goto exit;
	}
	jx = return_int;
	return_int = 100;
do_set:
	bc = parse_int('/BC=', mparm_str);
	while ( ( box_count > bc )  ) {
		kill_box;
	}
  set_global_str('W'+str(jx-1),"/WID=" + str(window_id) + "/FN=" + file_name);
	set_mark(jx);
	make_message('Global book mark #' + str(jx) + ' set.' );
exit:
}

/*******************************************************************************
																MULTI-EDIT MACRO

Name:		GetGlobalBkMrk

Description:	Uses random access markers and global variables to  implement
							GLOBAL book  marks.   If GetGlobalBkMrk  is executed  without
							parameters, the user is prompted to press a number key  (0-9,
							0=10) to specify the marker #.  If a value of 1-10 is  passed
							as a parameter, then the specified mark is retreived  without
							prompting the user.

Parameters:		MParm_Str can contain 1-10 to denote going directly to the
							mark.  If the entire MParm_Str does not contain numeric
							characters, this option will not work, and the menu will be
							invoked.  Under these circumstances, the following
							parameters are expected:  /X=		The X coordinate for the
							menu /X=		The X coordinate for the menu /BC=	The amount
							of boxes which need to be killed upon exit

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
macro GetGlobalBkMrk {
  int jx, bc,
  active_window = window_id,
  book_mark_window;

	/* If a numeric parameter was passed then use it, and bypass the prompting*/
	if ( ( Val(jx, MParm_Str) == 0 ) ) {
		if ( ( (jx > 0) & (jx < 11) ) ) {
			goto do_get;
		}
	}

	RM('USERIN^XMENU /B=1/L=Select marker # to retrieve/X=' + parse_str('/X=',
			mparm_str)+'/Y=' + parse_str('/Y=', mparm_str)+'/M=1 (PMRANDOM)2 ()3 ()4'+
			' ()5 ()6 ()7 ()8 ()9 ()0 ()');

	if ( ( return_int <= 0 ) ) {
		goto exit;
	}
	jx = return_int;
	return_int = 100;
do_get:
	bc = parse_int('/BC=', mparm_str);
	while ( ( box_count > bc )  ) {
		kill_box;
	}
  if (!switch_file(parse_str("/FN=",global_str('W'+str(jx-1))))) {
    RM("beeps /C=2");
    make_message("Can't find buffer for global book mark #" + str(jx));
    goto EXIT;
  }
  book_mark_window = window_id;
  get_mark( jx );
  if (B_WINDOW_STYLE) {
// if using the Brief emulation window operations, attempt to emulate brief here
// try to find a visible window containing this buffer
// if no visable window, switch the buffer into this window
    return_str = file_name;
    switch_win_id(active_window);
    RM("B_EDIT_FILE /NOPROMPT=1");
  }
	make_message('Global book mark #' + str(jx) + ' retrieved.' );
exit:
}

/*******************************************************************************
																MULTI-EDIT MACRO

Name:		B_GreyENTER

Description:	Mimics the use of the <GreyENTER> key in brief

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
macro B_GreyENTER
{
	int tr=Refresh,ti=Insert_Mode;

	Refresh=False;
	Insert_Mode=True;
	Mark_Pos;
	Goto_Col(1);
	Set_Indent_Level;
	Goto_Mark;
//	Cr;
	RM("CR");
	Insert_Mode=ti;
	Refresh=tr;
}

/*******************************************************************************
																MULTI-EDIT MACRO

Name:  SVFilesStatus

Description:	Saves all changed files and optionally the current status of
							editor.

Parameters:		/S= - if Non-Zero will save the status of the editor.

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
macro SVFilesStatus
{
	rm('AutoSave');
	if ( Parse_Int('/S=',MParm_Str) )
		rm('Status');
}

macro BriefInstall
{
	Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $02);
	Set_Global_Str('@KEYMAP_NAME@','KN=BRIEFFN=BRIEF');
}



macro BUF_LIST trans {
/*
 E to edit, D to delete, W to write
 If we try to emulate the above from BRIEF, it will screw up our incremental
 search by file name.
*/
  if (B_WINDOW_STYLE) {
    int active_window = window_id,
        active_screen_num = screen_num,
        current_window_id = window_id,
     t_buffer_id,
        T_Refresh = refresh,
        Menu_Window_Id = 0,
        Menu_Window_num,
        list_length,
        menu_handle = menu_create,
        path_width = screen_width - 31,
        t_undo_stat = undo_stat,
        t_insert_mode = insert_mode,
        event,
        t_window_id
        ;


       str  spaces[128] ;

       pad_str(spaces,128," ");

    Undo_Stat = False;

  // start creating the envionment for the list
    Push_Labels;
    Refresh = False;
    Flabel('Save',3,$FF);

    Switch_Window(Window_Count);
    Error_Level = 0;
    Create_Window;
    if (Error_Level != 0) {
      Make_Message('Too many windows to run this macro!');
      Error_Level = 0;
      Goto EXIT;
    }
    Menu_Window_Num = Cur_Window;
    Menu_Window_Id = Window_Id;
    File_Name = 'MEMENU.TMP';
    window_attr = $81;  // doing this simplifies the logic in CHECK_WINDOW
  // Build the list
    Switch_Win_id( Menu_Window_id );
    menu_window_num = cur_window;
    Erase_Window;
    list_length = 0;

    switch_win_id(current_window_id);
    Working;
    do {
  /* if this is a "legal" window, add to list.
     Notice the xxx= parameters appended to the line that will be off
     screen when list is displayed */
      call CHECK_WINDOW;
      if (return_int) {
        put_line_to_win(
          copy(truncate_path(file_name) + spaces,1,13) +
          copy(window_name + spaces,1,9) +
          copy("         MODIFIED ",(file_changed * 9) + 1,9) +
          copy(get_path(file_name) + spaces,1,path_width) +
          "WID=" + str(window_id) +
          "BID=" + str(buffer_id)
          ,++list_length,menu_window_num,0);
        }
      Switch_Window(cur_Window + 1);
    } while ( Window_id != current_window_id);

    switch_win_id(menu_window_id);

  /* If there are no windows to display, then.... */
    if (list_length < 2) {
      make_message("No other \"buffers\"");
      Return_Int = 0;
      Goto EXIT;
    }
    Ignore_Case = True;

  DO_MENU:
    switch_win_id(menu_window_id);
    menu_set_item(menu_handle,1,"Delete","","/KC=<DEL>/K1=0/K2=83/R=3/L=1/C=27",11,0,0);
    menu_set_item(menu_handle,2,"Save","","/KC=<F3>/K1=0/K2=61/R=4/L=1/C=40",11,0,0);
    menu_set_item(menu_handle,3,"","","/DC=1/W=72/L=3/C=1/WIN=' + str(cur_window)",15,0,0);
    switch_win_id(menu_window_id);
    goto_col(1);
    return_int = menu_handle;
    RM("USERIN^DATA_IN /HN=1/A=2/H=/#=3/T=\"BUFFER\" LIST/S=3");
    event = return_int;

    switch (event) {
      case 3 : // delete
        if (list_length < 2) {
          RM("MESSAGEBOX /B=2/M=Can't delete last buffer!");
          goto DO_MENU;
        }
        call FIND_SELECTED_WINDOW;
        if (return_int) {
          RM("B_DELETE_BUFFER /NORELINK=" + str(window_id != active_window) +
              "/SCREEN=" + str(active_screen_num));
          if (return_int > 0) {
            if (window_id != active_window) {
              RM("DELWIN");
            }
            switch_win_id(menu_window_id);
            --list_length;
            set_global_int("B_T_BUF_ID_" +
              parse_str("BID=",get_line),0); // get rid of temp global
            del_line;
            if (at_eof) {
              up;
            }
            t_window_id = parse_int("WID=",get_line);
            if (!switch_win_id(current_window_id)) { // if we blasted the current buffer...
            current_window_id = t_window_id;
            }
          }
        }
        goto DO_MENU;
        break;
      case 4 : // save
        call FIND_SELECTED_WINDOW;
        error_level = 0;
        save_file;
        switch_win_id(menu_window_id);
        if (error_level) {
          RM("MEERROR");
        } else { // overwrite the word MODIFIED with blanks
          goto_col(23);
          insert_mode = false;
          text("        ");
          insert_mode = t_insert_mode;
        }
        goto DO_MENU;
        break;
      case 1 : // select
        t_window_id = parse_int("WID=",get_line);
        if (active_window == t_window_id) {
  // if we didn't change buffers, get out now
          goto EXIT;
        }
        switch_win_id(active_window);
        RM("B_SWITCH_BUFFER /NEWBUFF=" + str(t_window_id));
        goto EXIT;
        break;
    }

    Goto EXIT;


  CHECK_WINDOW:
    return_int = 0;
  // Check to see if this is a non-switchable window like MECONFIG.DB, etc.
      if (window_attr & 0x80)
        RET;

  /* If you wish to not display the error output window, uncomment this
      if (window_id == Global_Int('~MEERR_ID'))
        RET;
  */

  // Check for redundant members of a group of linked windows
    if (global_int("B_T_BUF_ID_" + str(buffer_id)))
      RET;

  // set global to keep track of linked windows
    set_global_int("B_T_BUF_ID_" + str(buffer_id),buffer_id);
    return_int = 1;
    RET;


  FIND_SELECTED_WINDOW:
    return_int = switch_win_id(parse_int("WID=",get_line));
    RET;


  EXIT:
    Refresh = False;
    if (Switch_Win_Id(Menu_Window_Id)) {
  // clear the temporary globals used in CHECK_WINDOW
      tof;
      while (!at_eof) {
        set_global_int("B_T_BUF_ID_" + parse_str("BID=",get_line),0);
        down;
      }
      Delete_Window;
    }

    Switch_Win_Id(Active_Window);
    Refresh = T_Refresh;
    New_SCREEN;
    Pop_Labels;
    Undo_Stat = T_Undo_Stat;
    menu_delete(menu_handle);
    insert_mode = t_insert_mode;
  } else {
// If they are using the Multi-Edit window operations, run that instead
    RM("WINOP /T=3");
  }
}

macro EDIT_NEXT_BUFFER trans {
  if (B_WINDOW_STYLE) {
    RM("B_SWITCH_BUFFER " + mparm_str);
  } else {
    RM("NEXTWIN");
  }
}

macro EDIT_PREV_BUFFER trans {
  if (B_WINDOW_STYLE) {
    RM("B_SWITCH_BUFFER /PREV=1");
  } else {
    RM("LASTWIN");
  }
}

macro B_CREATE_EDGE trans {
  if (B_WINDOW_STYLE) {
    int active_window = window_id,
        new_window,
        link_win,
        t_refresh = refresh,
        t_buffer_id;

  // check to see if current window is already too small
    if (((win_x2 - win_x1) < 10) || ((win_y2 - win_y1) < 3)) {
      RM("MESSAGEBOX /B=2/M=Window too small to split!");
      goto EXIT;
    }

    RM("MAKEWIN /NC=1/NL=1");  // check to see if we can create a new window
    if (error_level) {
      RM("MESSAGEBOX /B=1/M=The maximum combination of windows and \"buffers\" has been reached.  You can use DELETE CURRRENT BUFFER to make room for more windows.");
      goto EXIT;
    }

    refresh = false;
    Create_Window;
    new_window = window_id;
    switch_win_id(active_window);
    link_win = cur_window;
    switch_win_id(active_window);

    RM("SPLITWIN /ID=" + str(new_window));

    switch_win_id(new_window);
    if (return_int) {
      Link_Window(link_win);
      RM('SetWindowNames');
    } else {  // this means they pressed <ESC> at the prompt
      delete_window;
      switch_win_id(active_window);
    }

    refresh = t_refresh;
    redraw;

  EXIT:
  } else {
// If they are using the Multi-Edit window operations, run that instead
    RM("WINOP /T=4");
  }
}

macro B_DELETE_EDGE trans {
  if (B_WINDOW_STYLE) {
    int active_window = window_id,
        ev_count = 0,
        t_refresh = refresh,
        t_win_num = cur_window;

    RM("SCREEN_SHARE");
    if (!(return_int & 0x7F)) {
      goto EXIT;
    }

    refresh = false;

  // if this window is not linked to any other, create a "buffer" and link to it
    Put_Box(25,2, 56, 13, 0, m_b_color, 'Select window edge to delete', true);

    if (Return_Int & 1) {
  /* Enable up movement */
      ++Ev_Count;
      Set_Global_Str('@WSEV#' + Str(Ev_Count), '/3D=1/T=/KC=  |30  /K1=0/K2=72/R=3/W=5/X=37/Y=4');
    }
    if (Return_Int & 2) {
  /* Enable down movement */
      ++Ev_Count;
      Set_Global_Str('@WSEV#' + Str(Ev_Count), '/3D=1/T=/KC=  |31  /K1=0/K2=80/R=4/W=5/X=37/Y=8');
    }
    if (Return_Int & 4) {
  /* Enable left movement */
      ++Ev_Count;
      Set_Global_Str('@WSEV#' + Str(Ev_Count), '/3D=1/T=/KC=  |17  /K1=0/K2=75/R=2/W=5/X=29/Y=6');
    }
    if (Return_Int & 8) {
  /* Enable right movement */
      ++Ev_Count;
      Set_Global_Str('@WSEV#' + Str(Ev_Count), '/3D=1/T=/KC=  |16  /K1=0/K2=77/R=1/W=5/X=44/Y=6');
    }
    ++Ev_Count;
    Set_Global_Str('@WSEV#' + Str(Ev_Count), '/3D=1/T=Cancel/KC=<ESC>/K1=27/K2=1/R=0/W=11/X=35/Y=10');
    RM('USERIN^CheckEvents /M=2/G=@WSEV#/#=' + Str(Ev_Count));
    Read_Key;

    if ((key1 == 0) && (key2 == 250)) {
      RM('CheckEvents /G=@WSEV#/M=1/#=' + Str(Ev_Count));
    } else {
      RM('CheckEvents /G=@WSEV#/M=0/#=' + Str(Ev_Count));
    }
    Kill_Box;

    return_int = parse_int("/R=",return_str);

    if (return_int != 0) {
      switch_win_id(Parse_Int('/' + Str(parse_int('/R=', return_str)) + '=',
                      Global_Str('!SCREEN_SHARE')));
      RM('USERIN^CheckEvents /M=3/G=@WSEV#/#=' + Str(Ev_Count));

      if (!link_stat) {
        rm("MAKE_BUFFER");
      }

      link_window(t_win_num);
      switch_win_id(active_window);
      RM("DELWIN");
    }

//    switch_win_id(active_window);

    refresh = t_refresh;
    redraw;
  EXIT:
  } else {
// If they are using the Multi-Edit window operations, run that instead
    RM("WINOP /T=1");
  }
}

macro B_DELETE_BUFFER trans {
	int link_amount = 0,
			link_id = buffer_id,
			new_window_id,
			new_buffer_num,
			active_window = window_id,
			active_screen = parse_int("/SCREEN=",Mparm_str),
			t_refresh = refresh,
			t_link_stat = link_stat,
			no_relink = parse_int("/NORELINK=",Mparm_str),
			result = 0;

	str active_file_name = caps(fexpand(file_name));
	if (active_screen < 1) {
		active_screen = screen_num;
	}
	refresh = false;

// first, find out if there is only 1 buffer left.
FIND_OTHER_BUFFER:
	switch_window(cur_window + 1);
	RM("FINDWIN /HIDDEN=1");
	if ((buffer_id == link_id) && (window_id != active_window)) {
/* if we found a window linked to the current one, we haven't found a new
   buffer yet */
		goto FIND_OTHER_BUFFER;
	}
	if (window_id == active_window) {
// if we turn full circle, this means no other buffers.
		make_message("No other buffers.");
		goto EXIT;
	}

	switch_win_id(active_window);

	if (file_changed) {
		RM("B_SAVE_FILE");

		if (return_int < 1)
			goto EXIT;
	}

	result = 1;
	if (t_link_stat) {
/* make a list of all windows linked to current buffer so we can deal with
   them later on */
		do {
			switch_window(cur_window + 1);
			if ((buffer_id == link_id) && (window_id != active_window)) {
				set_global_int("B_T_WIN_" + str(++link_amount),window_id);
			}
		} while (window_id != active_window);
	}

// link active window with new buffer
	if (!no_relink)
		RM("B_SWITCH_BUFFER /NOMAKENEW=1");

	if (t_link_stat) {
// if this window was linked to other window(s), deal with them
		for (;link_amount > 0;--link_amount) {
			switch_win_id(global_int("B_T_WIN_" + str(link_amount)));
			if ((screen_num != 0) && (screen_num == active_screen)) {
// If it is on the same visable screen, relink with new buffer.
				switch_win_id(active_window);
				new_buffer_num = cur_window;
				switch_win_id(global_int("B_T_WIN_" + str(link_amount)));
				link_window(new_buffer_num);
				RM("SETWINDOWNAMES");
			} else {
// otherwise, assume it was a background buffer and delete.
				delete_window;
			}
			set_global_int("B_T_WIN_" + str(link_amount),0); // clear temp global
		}
	}
	switch_win_id(active_window);

EXIT:
	refresh = t_refresh;
	redraw;
	return_int = result;
}

macro B_EDIT_FILE trans {
  if (B_WINDOW_STYLE) {
    str t_file_name;
    int active_window = window_id,
        t_buffer_id = buffer_id,
        t_buffer_window,
        t_refresh = refresh,
        buffer_found = false,
        already_loaded = false,
        already_loaded_id;

    refresh = false;

  if (parse_int("/NOPROMPT=",mparm_str)) {
    goto NO_PROMPT;
  }

    return_str = "";
  //  RM("FILE_PROMPT /T=NEW FILE TO EDIT/WM=1");
    RM("FILE_PROMPT /T=NEW FILE TO EDIT/FTO=1");
  // /FTO=1 means allow file type override.  A feature new to version 7.0

    if (return_int) {
  NO_PROMPT:
      t_file_name = caps(fexpand(return_str));
      if (switch_file(t_file_name)) {
        if ((window_id != active_window) && (buffer_id != t_buffer_id)) {
  /* the file has already been loaded.  If it is not the one currently being
     viewed, link with it. */
  /*
          t_buffer_window = cur_window;
          switch_win_id(active_window);
          link_window(t_buffer_window);
          RM("SETWINDOWNAMES");
          make_message("File already loaded.");
  */
  //        t_buffer_window = cur_window;
          already_loaded_id = window_id;
          switch_win_id(active_window);
          already_loaded = true;
          make_message("File already loaded.");
          goto ALREADY_LOADED;
        }
      } else {
  ALREADY_LOADED:
  // before we load a new file, be sure the one we are editing has a "buffer"
        if (link_stat) {
  /* we need to find all windows that this one is linked to position the cursor
     the same - UNLESS it is a visable window. */
          int t_col = c_col,
              t_line = c_line,
              t_row = c_row,
              t_left_offset = left_offset,
              t_screen_num = screen_num;
          do {
            switch_window(cur_window + 1);
            if ((window_id != active_window) && (buffer_id == t_buffer_id)) {
              if (!buffer_found) {
                buffer_found = true;
                t_buffer_window = window_id;
              }
              if (screen_num != t_screen_num) {
                while (c_row < t_row)
                  down;
                while (c_row > t_row)
                  up;
                left_offset = t_left_offset;

                goto_line(t_line);
                goto_col(t_col);
              }
            }
          } while (window_id != active_window);
        } else {
          RM("MAKE_BUFFER");
          t_buffer_window = return_int;
        }
        if (already_loaded) {
          switch_win_id(already_loaded_id);
          t_buffer_window = cur_window;
          switch_win_id(active_window);
          link_window(t_buffer_window);
          RM("SETWINDOWNAMES");
          goto EXIT;
        }
        erase_window;
  // load the file
  //      RM("LDFILES /NW=1/NC=1/DE=1");
        RM("LDFILES /NC=1/DE=1/FTO=" +
          str(global_int("!FILE_TYPE_OVERRIDE")));
  /* This global contains the file type override mode.  A feature new to
   version 7.0 */
        set_global_int("!FILE_TYPE_OVERRIDE", 0);
        if (error_level) {
  // if there is a problem, re-link to previous buffer
          RM("MEERROR");
          switch_win_id(t_buffer_window);
          t_buffer_window = cur_window;
          switch_win_id(active_window);
          link_window(t_buffer_window);
          RM("SETWINDOWNAMES");
        } else {
          switch_win_id(active_window);
        }
      }
    }
  EXIT:
    refresh = t_refresh;
    redraw;
  } else {
  // If they are using the Multi-Edit window operations, run that instead
   RM("LOADFILE /E=1");
  }
}

 // The macros below this are supporting macros
macro B_SWITCH_BUFFER trans {
	int active_window = window_id,
			active_buffer_id = buffer_id,
			buffer_win,
			link_win = parse_int("/NEWBUFF=",Mparm_str),
			ow,
			error_win_id = Global_Int('~MEERR_ID'),
			t_refresh = refresh,
			buffer_found = 1,
			switch_direction = 1
			;


	if (switch_win_id(link_win)) {
		goto EXPLICIT_BUFFER;
	}


/*debugging code
int bb = global_int("B_CUR_BUFFER"),
		tt;

tt =	switch_win_id(bb);

if (!tt) {
RM("MESSAGEBOX /T=DEBUGGING/M=THE WINDOW_ID "
+ str(bb) + " DOES NOT EXIST");
error_level = 0;
}
*/

	switch_win_id(global_int("B_CUR_BUFFER"));
	if (buffer_id != active_buffer_id)  // If we were not linked with B_CUR_BUFFER...
		switch_win_id(active_window);

	buffer_win = window_id;

	if (parse_int("/PREV=",Mparm_Str)) {
		switch_direction = -1;
	}
	refresh = false;

SKIP_ACTIVE:
	ow = cur_window;
	switch_window(cur_window + switch_direction);

	if (switch_direction == 1) {
		RM("FINDWIN /HIDDEN=1");
	} else {

		while ((cur_window != ow) && (((Window_Attr & $C0) != 0) || (window_id == error_win_id))) {
			switch_window(cur_window - 1);
		}
		if (cur_window ==  ow) {
			SWITCH_WINDOW(CUR_WINDOW - 1);
			while ((cur_window != ow) && (((Window_Attr & $80) != 0) || (window_id == error_win_id))) {
				switch_window(cur_window - 1);
			}
		}
	}
	if (window_id == buffer_win) {
		make_message("No other \"buffers\"");
		switch_win_id(active_window);
		buffer_found = 0;
	} else {
		if (buffer_id == active_buffer_id) {
//beep;
//make_message("FN=" + truncate_path(file_name) + "bi=" + str(buffer_id) + " WI=" + str(window_id) + " AFN=" + active_file_name + " abi=" + str(active_buffer_id) + " AWI=" + str(active_window));

// if this is the same buffer, keep looking
			goto SKIP_ACTIVE;
		}
		link_win = window_id;
/*
if (link_win == active_window) {
RM("MESSAGEBOX /T=ERROR/M=WE ARE ABOUT TO LINK ACTIVE WINDOW WITH ITSELF "
+ str(active_window) +
" LINK WINDOW=" + str(global_int("B_CUR_BUFFER")) +
" ACTIVE_BUFFER_ID=" + str(active_buffer_id) +
" BUFFER_ID=" + str(buffer_id) +
" TT=" + str(TT) +
" BB=" + str(BB)
);
error_level = 0;
}
*/

EXPLICIT_BUFFER:
		switch_win_id(active_window);
// make sure this window has a "buffer" before linking with another
		if (!parse_int("/NOMAKENEW=",mparm_str)) {
			if (!link_stat) {
				RM("MAKE_BUFFER");
			}
		}

		switch_win_id(link_win);
		set_global_int("B_CUR_BUFFER",window_id);
		link_win = cur_window;
		switch_win_id(active_window);
		if (link_stat) {
		/* we need to find all windows that this one is linked to position the cursor
   		the same, UNLESS it is a visable one. */
			int t_col = c_col,
					t_line = c_line,
					t_row = c_row,
					t_left_offset = left_offset,
					t_screen_num = screen_num;
			do {
				switch_window(cur_window + 1);
				if (buffer_id == active_buffer_id) {
					if (screen_num != t_screen_num) {
						while (c_row < t_row)
							down;
						while (c_row > t_row)
							up;
						left_offset = t_left_offset;

						goto_line(t_line);
						goto_col(t_col);
//					redraw;
					}
				}
			} while (window_id != active_window);
		}
		link_window(link_win);
/*debugging code
if (error_level) {
RM("MESSAGEBOX /T=WINDOW LINK ERROR IN B_SWITCH_BUFFER/M=HOST WINDOW="
+ str(active_window) + " LINK WINDOW=" + str(global_int("B_CUR_BUFFER")));
error_level = 0;
}
 */
		RM("SETWINDOWNAMES");
	}
	refresh = t_refresh;
	redraw;
	return_int = buffer_found;

}

macro MAKE_BUFFER trans {
// This macro will make a hidden window or "buffer" for the current window,

	int link_win = cur_window,
			active_window = window_id;
	RM("CREATEWINDOW");
//	Create_Window;
	return_int = window_id;
	link_window(link_win);
/*
 if (window_name != "OUTPUT") {
 		window_attr = window_attr | 1; // make it hidden
 }
*/
	switch_win_id(active_window);
	RM("SETWINDOWNAMES");

}

macro B_SAVE_FILE {
		int menu_handle = menu_create,
				t_window_attr = window_attr,
				t_refresh = refresh;

// capture screen so we can restore it upon exit
		save_box(1,1,screen_width,screen_length);
		window_attr = 0;
		refresh = true;
		redraw;
		menu_set_item(menu_handle,1," Delete? ","","/L=2/C=1",10,0,0);
		menu_set_item(menu_handle,2," Yes ","","/L=2/C=10/W=5/QK=2/R=1",11,0,0);
	 	menu_set_item(menu_handle,3," No ","","/L=2/C=17/W=4/QK=2/R=0",11,0,0);
		menu_set_item(menu_handle,4," Write and delete ","","/L=2/C=23/W=18/QK=2/R=3",11,0,0);
		return_int = menu_handle;
		RM("DATA_IN /H=/#=4/S=3/HN=1/T=This buffer has not been saved.");
		if (return_int == 3) {
			error_level = 0;
			save_file;
			if (error_level) {
				RM("MEERROR");
				return_int = -1;
			}
		}
		menu_delete(menu_handle);
		window_attr = t_window_attr;
		refresh = t_refresh;
		kill_box;
}