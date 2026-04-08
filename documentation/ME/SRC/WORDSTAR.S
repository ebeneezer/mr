macro_file WORDSTAR;
/******************************************************************************
														MULTI-EDIT MACRO FILE

Name:		WORDSTAR

Description:  This macro file contains macros to emulate a subset of common
							Wordstar key commands.  It is not our intention to completely
							mimic Wordstar, but to provide a less painful transition from
							Wordstar to Multi-Edit.

WORDSTAR				- Initializes the editor for Wordstar emulation
WS_PROCESS_KEY1 - Used by other Wordstar emulation macros
CTRLQ						- Traps <CtrlQ> and process the second keystroke
CTRLK						- Traps <CtrlK> and process the second keystroke
CTRLO						- Traps <CtrlO> and process the second keystroke
CTRLW						- Performs Wordstar-like scroll down
CTRLZ						- Performs Wordstar-like scroll up
DELWORDR				- Delete word right
HOMEKEY					- Special processing for the home key
ENDKEY					- Special processing for the end key
RIGHTMAR				- Sets right margin
LEFTMAR					- Sets left margin
RESETMAR				- Resets margin
INDENT					- Performs Wordstar like indent
UNDENT					- Performs Wordstar like undent
WSHELP					- Calls the Wordstar help
WSINSTALL				- Used to configure the Wordstar keymap.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*****************************************************************************/

macro WORDSTAR {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:		WORDSTAR

Description:  Initializes the editor for Wordstar emulation

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	Right_Margin = 80;
	Set_Global_Int('Margin_Released',FALSE);
	Set_Global_Int('Left_Margin',1);
	Set_Global_Int('Right_Margin',Right_Margin);
	Set_Global_Int('Name_La',0);
	Set_Global_Int('Stat_La',0);
	Word_Delimits = ' .,:;!?';
	Indent_Style = 0;
	Goto_Col(1);
	Set_Indent_Level;

#ifdef DEMO

Set_Global_Str('@KEYMAP_NAME@','KN=WORDSTARFN=WORDSTAR');
Make_message("Wordstar keymap enabled.  Press <F1> for help.");

// Default key assignments
MACRO_TO_KEY(<CtrlK>,'WORDSTAR^CtrlK ',EDIT);
MACRO_TO_KEY(<CtrlO>,'WORDSTAR^CtrlO ',EDIT);
MACRO_TO_KEY(<CtrlQ>,'WORDSTAR^CtrlQ ',EDIT);
MACRO_TO_KEY(<AltIns>,'MEHELP^ScreenMrk ',ALL);
MACRO_TO_KEY(<MEVENT2>,'MOUSE^MOUSEEVENT2 ',EDIT);
MACRO_TO_KEY(<MEVENT>,'MOUSE^MOUEVENT ',EDIT);
MACRO_TO_KEY(<ShftMEVENT>,'MOUSE^CUA_Shft_Button ',EDIT);
MACRO_TO_KEY(<CtrlMEVENT>,'MOUSE^MOUSE_BLOCK_OP /BT=1',EDIT);
MACRO_TO_KEY(<AltMEVENT>,'MOUSE^MOUSE_BLOCK_OP /BT=2',EDIT);
MACRO_TO_KEY(<ShftMEVENT2>,'MOUSE^MOUSE_SEARCH ',EDIT);
MACRO_TO_KEY(<CtrlMEVENT2>,'MOUSE^MOUSE_SEARCH /D=1',EDIT);
MACRO_TO_KEY(<AltENTER>,'MEUTIL3^USER ',EDIT);
Set_Global_Str('!MIM_KEY1', '<AltENTER>');
MACRO_TO_KEY(<F1>,'WORDSTAR^WSHELP ',EDIT);
FLABEL('Help',1,EDIT);
MACRO_TO_KEY(<AltH>,'USERIN^MAINHELP ',EDIT);
MACRO_TO_KEY(<F2>,'MEMENUS ',EDIT);
FLABEL('Menu',2,EDIT);
Set_Global_Str('!MM_KEY', '<F2>');
MACRO_TO_KEY(<ESC>,'MEMENUS ',EDIT);
MACRO_TO_KEY(<F3>,'MEUTIL1^SAVEFILE ',EDIT);
FLABEL('Save',3,EDIT);
Set_Global_Str('!FM_KEY4', '<F3>');
MACRO_TO_KEY(<CtrlF3>,'MEUTIL1^SAVEBLCK ',EDIT);
FLABEL('SaveBl',23,EDIT);
Set_Global_Str('!FM_KEY8', '<CtrlF3>');
MACRO_TO_KEY(<ShftF3>,'MEUTIL1^LOADFILE ',EDIT);
FLABEL('Load',13,EDIT);
Set_Global_Str('!FM_KEY2', '<ShftF3>');
MACRO_TO_KEY(<F6>,'MEUTIL2^SEARCH ',EDIT);
FLABEL('Search',6,EDIT);
Set_Global_Str('!SRM_KEY1', '<F6>');
MACRO_TO_KEY(<ShftF6>,'MEUTIL2^S_REPL ',EDIT);
FLABEL('S/Repl',16,EDIT);
Set_Global_Str('!SRM_KEY2', '<ShftF6>');
MACRO_TO_KEY(<CtrlF6>,'MEUTIL2^REPSRCH ',EDIT);
FLABEL('NxtSrc',26,EDIT);
Set_Global_Str('!SRM_KEY3', '<CtrlF6>');
MACRO_TO_KEY(<CtrlL>,'MEUTIL2^REPSRCH ',EDIT);
MACRO_TO_KEY(<AltF>,'MEUTIL2^FS ',EDIT);
Set_Global_Str('!SRM_KEY5', '<AltF>');
MACRO_TO_KEY(<AltG>,'MEUTIL2^FS /N',EDIT);
Set_Global_Str('!SRM_KEY6', '<AltG>');
MACRO_TO_KEY(<AltShftF>,'MEUTIL2^FS /M=1',EDIT);
Set_Global_Str('!SRM_KEY7', '<AltShftF>');
MACRO_TO_KEY(<ShftF7>,'MEUTIL2^MCOLBLCK ',EDIT);
FLABEL('BegCol',17,EDIT);
Set_Global_Str('!BM_KEY16', '<ShftF7>');
MACRO_TO_KEY(<F7>,'MEUTIL2^MSTRBLCK ',EDIT);
FLABEL('BegBlk',7,EDIT);
Set_Global_Str('!BM_KEY17', '<F7>');
MACRO_TO_KEY(<CtrlF9>,'MEUTIL2^BLOCKOFF ',EDIT);
FLABEL('BlkOff',29,EDIT);
Set_Global_Str('!BM_KEY18', '<CtrlF9>');
MACRO_TO_KEY(<AltU>,'MEUTIL2^UNDBLK ',EDIT);
Set_Global_Str('!BM_KEY8', '<AltU>');
MACRO_TO_KEY(<AltI>,'MEUTIL2^INDBLK ',EDIT);
Set_Global_Str('!BM_KEY7', '<AltI>');
MACRO_TO_KEY(<F9>,'MEUTIL2^BLOCKOP /BT=0',EDIT);
FLABEL('CopyBl',9,EDIT);
Set_Global_Str('!BM_KEY1', '<F9>');
MACRO_TO_KEY(<F10>,'MEUTIL2^BLOCKOP /BT=1',EDIT);
FLABEL('MoveBl',10,EDIT);
Set_Global_Str('!BM_KEY2', '<F10>');
MACRO_TO_KEY(<CtrlF10>,'MEUTIL2^BLOCKOP /BT=2',EDIT);
FLABEL('DelBl',30,EDIT);
Set_Global_Str('!BM_KEY3', '<CtrlF10>');
MACRO_TO_KEY(<ShftF9>,'MEUTIL2^BLOCKOP /BT=3',EDIT);
FLABEL('WnCopy',19,EDIT);
Set_Global_Str('!BM_KEY10', '<ShftF9>');
MACRO_TO_KEY(<ShftF10>,'MEUTIL2^BLOCKOP /BT=4',EDIT);
FLABEL('WnMove',20,EDIT);
Set_Global_Str('!BM_KEY11', '<ShftF10>');
MACRO_TO_KEY(<GREY->,'MEUTIL2^CUT /M',EDIT);
Set_Global_Str('!CPM_KEY2', '<GREY->');
MACRO_TO_KEY(<GreyENTER>,'MEUTIL2^PASTE ',EDIT);
Set_Global_Str('!CPM_KEY5', '<GreyENTER>');
MACRO_TO_KEY(<GREY+>,'MEUTIL2^CUT ',EDIT);
Set_Global_Str('!CPM_KEY1', '<GREY+>');
MACRO_TO_KEY(<CtrlGREY->,'MEUTIL2^CUT /M /A',EDIT);
Set_Global_Str('!CPM_KEY4', '<CtrlGREY->');
MACRO_TO_KEY(<CtrlGREY+>,'MEUTIL2^CUT /A',EDIT);
Set_Global_Str('!CPM_KEY3', '<CtrlGREY+>');
MACRO_TO_KEY(<CtrlF5>,'WINDOW^WINOP /T=8',EDIT);
FLABEL('Zoom',25,EDIT);
Set_Global_Str('!WM_KEY12', '<CtrlF5>');
MACRO_TO_KEY(<CtrlGREY/>,'WINDOW^WINOP /T=1',EDIT);
Set_Global_Str('!WM_KEY2', '<CtrlGREY/>');
MACRO_TO_KEY(<ScrollLockOn>,'WINDOW^WINOP /T=5',EDIT);
Set_Global_Str('!WM_KEY11', '<ScrollLockOn>');
MACRO_TO_KEY(<AltF1>,'WINDOW^WINOP /T=3',EDIT);
FLABEL('WinLst',31,EDIT);
Set_Global_Str('!WM_KEY4', '<AltF1>');
MACRO_TO_KEY(<AltW>,'WINDOW^ADJACENT_WIN ',EDIT);
Set_Global_Str('!WM_KEY8', '<AltW>');
MACRO_TO_KEY(<ShftF1>,'WINDOW^NEXTWIN ',EDIT);
FLABEL('NxtWin',11,EDIT);
Set_Global_Str('!WM_KEY6', '<ShftF1>');
MACRO_TO_KEY(<CtrlF1>,'WINDOW^LASTWIN ',EDIT);
FLABEL('LstWin',21,EDIT);
Set_Global_Str('!WM_KEY7', '<CtrlF1>');
CMD_TO_KEY(<DEL>,DEL_CHAR,EDIT);
CMD_TO_KEY(<CtrlG>,DEL_CHAR,EDIT);
MACRO_TO_KEY(<AltD>,'DELEOL ',EDIT);
MACRO_TO_KEY(<ShftDEL>,'DELWORD ',EDIT);
MACRO_TO_KEY(<CtrlT>,'WORDSTAR^DELWORDR ',EDIT);
CMD_TO_KEY(<BS>,BACK_SPACE,EDIT);
CMD_TO_KEY(<CtrlH>,BACK_SPACE,EDIT);
MACRO_TO_KEY(<CtrlBS>,'BSWORD ',EDIT);
CMD_TO_KEY(<ShftF8>,DEL_LINE,EDIT);
FLABEL('DelLin',18,EDIT);
CMD_TO_KEY(<CtrlY>,DEL_LINE,EDIT);
CMD_TO_KEY(<CtrlF7>,UNDO,EDIT);
FLABEL('UNDO',27,EDIT);
Set_Global_Str('!TXM_KEY1', '<CtrlF7>');
CMD_TO_KEY(<CtrlENTER>,UNDO,EDIT);
CMD_TO_KEY(<CtrlDEL>,REDO,EDIT);
Set_Global_Str('!TXM_KEY2', '<CtrlDEL>');
MACRO_TO_KEY(<END>,'END /T=300',EDIT);
MACRO_TO_KEY(<HOME>,'HOME /T=300',EDIT);
CMD_TO_KEY(<LF>,LEFT,EDIT);
CMD_TO_KEY(<CtrlS>,LEFT,EDIT);
CMD_TO_KEY(<RT>,RIGHT,EDIT);
CMD_TO_KEY(<CtrlD>,RIGHT,EDIT);
CMD_TO_KEY(<UP>,UP,EDIT);
CMD_TO_KEY(<CtrlE>,UP,EDIT);
CMD_TO_KEY(<DN>,DOWN,EDIT);
CMD_TO_KEY(<CtrlX>,DOWN,EDIT);
MACRO_TO_KEY(<HOME>,'WORDSTAR^HOMEKEY ',EDIT);
MACRO_TO_KEY(<END>,'WORDSTAR^ENDKEY ',EDIT);
CMD_TO_KEY(<PgUp>,PAGE_UP,EDIT);
CMD_TO_KEY(<CtrlR>,PAGE_UP,EDIT);
CMD_TO_KEY(<PgDn>,PAGE_DOWN,EDIT);
CMD_TO_KEY(<CtrlC>,PAGE_DOWN,EDIT);
CMD_TO_KEY(<CtrlHome>,TOF,EDIT);
CMD_TO_KEY(<CtrlEnd>,EOF,EDIT);
CMD_TO_KEY(<CtrlPgDn>,NEXT_PAGE_BREAK,EDIT);
Set_Global_Str('!NEXT_PB_KEY', '<CtrlPgDn>');
CMD_TO_KEY(<CtrlPgUp>,LAST_PAGE_BREAK,EDIT);
Set_Global_Str('!LAST_PB_KEY', '<CtrlPgUp>');
CMD_TO_KEY(<CtrlLF>,WORD_LEFT,EDIT);
CMD_TO_KEY(<CtrlA>,WORD_LEFT,EDIT);
CMD_TO_KEY(<CtrlRT>,WORD_RIGHT,EDIT);
CMD_TO_KEY(<CtrlF>,WORD_RIGHT,EDIT);
MACRO_TO_KEY(<CtrlZ>,'WORDSTAR^CTRLZ ',EDIT);
MACRO_TO_KEY(<CtrlW>,'WORDSTAR^CTRLW ',EDIT);
MACRO_TO_KEY(<AltF8>,'GOTOLINE ',EDIT);
FLABEL('GotoLn',38,EDIT);
Set_Global_Str('!CRSM_KEY7', '<AltF8>');
CMD_TO_KEY(<TAB>,TAB_RIGHT,EDIT);
CMD_TO_KEY(<CtrlI>,TAB_RIGHT,EDIT);
CMD_TO_KEY(<ShftTAB>,TAB_LEFT,EDIT);
CMD_TO_KEY(<F4>,INDENT,EDIT);
FLABEL('Indent',4,EDIT);
Set_Global_Str('!IND_KEY', '<F4>');
CMD_TO_KEY(<ShftF4>,UNDENT,EDIT);
FLABEL('Undent',14,EDIT);
Set_Global_Str('!UND_KEY', '<ShftF4>');
MACRO_TO_KEY(<F5>,'TEXT^MARKPOS ',EDIT);
FLABEL('Mark',5,EDIT);
Set_Global_Str('!CRSM_KEY1', '<F5>');
MACRO_TO_KEY(<ShftF5>,'TEXT^GOTOMARK ',EDIT);
FLABEL('GotoMk',15,EDIT);
Set_Global_Str('!CRSM_KEY2', '<ShftF5>');
MACRO_TO_KEY(<AltR>,'TEXT^REFORMAT ',EDIT);
Set_Global_Str('!TXM_KEY11', '<AltR>');
MACRO_TO_KEY(<CtrlB>,'TEXT^REFORMAT ',EDIT);
MACRO_TO_KEY(<AltJ>,'TEXT^JUSTIFY ',EDIT);
Set_Global_Str('!TXM_KEY12', '<AltJ>');
MACRO_TO_KEY(<ShftF2>,'DATETIME ',EDIT);
FLABEL('DatTim',12,EDIT);
Set_Global_Str('!TXM_KEY8', '<ShftF2>');
MACRO_TO_KEY(<ENTER>,'CR ',EDIT);
MACRO_TO_KEY(<CtrlN>,'CR ',EDIT);
MACRO_TO_KEY(<INS>,'INSTGL ',EDIT);
MACRO_TO_KEY(<CtrlV>,'INSTGL ',EDIT);
MACRO_TO_KEY(<F8>,'MEUTIL1^RUNMAC ',EDIT);
FLABEL('RunMac',8,EDIT);
Set_Global_Str('!MCM_KEY1', '<F8>');
CMD_TO_KEY(<AltF10>,KEY_RECORD,EDIT);
FLABEL('Record',40,EDIT);
Set_Global_Str('!RECORD_KEY', '<AltF10>');
CMD_TO_KEY(<AltF10>,KEY_RECORD,DOS_SHELL);
FLABEL('Record',40,DOS_SHELL);
MACRO_TO_KEY(<AltN>,'NOTEBOOK ',ALL);
MACRO_TO_KEY(<AltV>,'VCS ',EDIT);
Set_Global_Str('!VCS_KEY1', '<AltV>');
MACRO_TO_KEY(<AltP>,'DB /F=PHONE.DB/SRP=1',EDIT);
MACRO_TO_KEY(<AltQ>,'EXIT /NP=1',EDIT);
MACRO_TO_KEY(<AltA>,'SUPPORT^ASCII ',ALL);
Set_Global_Str('!MIM_KEY10', '<AltA>');
MACRO_TO_KEY(<AltF5>,'SUPPORT^ASCII ',EDIT);
FLABEL('ASCII',35,EDIT);
Set_Global_Str('!MIM_KEY10', '<AltF5>');
MACRO_TO_KEY(<AltA>,'SUPPORT^ASCII ',EDIT);
MACRO_TO_KEY(<AltF4>,'SUPPORT^CONDENSE ',EDIT);
FLABEL('Condns',34,EDIT);
Set_Global_Str('!MIM_KEY11', '<AltF4>');
MACRO_TO_KEY(<CtrlF4>,'DIRSHELL^DIRSHELL ',EDIT);
FLABEL('DIR',24,EDIT);
Set_Global_Str('!FM_KEY10', '<CtrlF4>');
MACRO_TO_KEY(<AltF3>,'MEUTIL1^SHELLDOS ',EDIT);
FLABEL('runDOS',33,EDIT);
Set_Global_Str('!MIM_KEY12', '<AltF3>');
MACRO_TO_KEY(<CtrlF8>,'LANGUAGE^COMPILE ',EDIT);
FLABEL('Compil',28,EDIT);
Set_Global_Str('!MIM_KEY4', '<CtrlF8>');
MACRO_TO_KEY(<CtrlF2>,'LANGUAGE^CMPERROR ',EDIT);
FLABEL('NxtErr',22,EDIT);
Set_Global_Str('!MIM_KEY5', '<CtrlF2>');
MACRO_TO_KEY(<AltE>,'LANGUAGE^CMPERROR ',EDIT);
MACRO_TO_KEY(<AltF9>,'LANGUAGE^TEMPLATE ',EDIT);
FLABEL('Templt',39,EDIT);
Set_Global_Str('!MIM_KEY6', '<AltF9>');
MACRO_TO_KEY(<AltT>,'LANGUAGE^TEMPLATE ',EDIT);
MACRO_TO_KEY(<AltF6>,'LANGUAGE^MATCH ',EDIT);
FLABEL('Match',36,EDIT);
Set_Global_Str('!MIM_KEY7', '<AltF6>');
MACRO_TO_KEY(<AltM>,'LANGUAGE^MATCH ',EDIT);
MACRO_TO_KEY(<AltF2>,'CALC ',EDIT);
FLABEL('Calc',32,EDIT);
Set_Global_Str('!MIM_KEY9', '<AltF2>');
MACRO_TO_KEY(<AltC>,'DOCUMENT^FCMD ',EDIT);
Set_Global_Str('!LM_KEY10', '<AltC>');
MACRO_TO_KEY(<AltL>,'SUPPORT^LINEDRAW ',EDIT);
Set_Global_Str('!MIM_KEY13', '<AltL>');
MACRO_TO_KEY(<AltX>,'EXIT^EXIT ',EDIT);
Set_Global_Str('!FM_KEY11', '<AltX>');
MACRO_TO_KEY(<AltK>,'SETUP^KEYCODE ',EDIT);
MACRO_TO_KEY(<AltZ>,'XZOOM ',EDIT);
Set_Global_Str('!TOG_SCR_FORM', '<AltZ>');
MACRO_TO_KEY(<AltS>,'SPELL^SPELL ',EDIT);
Set_Global_Str('!MIM_KEY16', '<AltS>');
MACRO_TO_KEY(<AltP>,'DB /F=PHONE.DB',EDIT);

if (global_int('!EVgDialectId')) {
	RM("EV_");
	RM('EVINIT');  // INIT EVOLVE IF IT WAS ENABLED BEFORE
}
#endif // DEMO

}

macro WS_PROCESS_KEY1 {
/* This macro takes the primary scan code KEY1, and converts it to an uppercase
alpha ASCII code to significantly simplify processing of keystrokes for the
macros CTRLQ, CTRLK, and CTRLO */

	if (key1 < 32) {
/* Convert CTRL keys to alpha */
		key1 = key1 + 64;
	}
/* Convert lower case to upper case */
	Key1 = Ascii(Caps(Char(Key1)));
}

macro CTRLQ {

	if (Global_Int('@WS_CTRL_HLP_OFF@')) {
		Make_Message('<CtrlQ>');
		Read_Key;
		Make_Message('');
	} else {
		Set_Global_Int('MENU_LEVEL',Global_Int('MENU_LEVEL') + 1);
		Put_Box(2,2,47,9,0,M_B_Color,'<CtrlQ>',true);
		Write('E - Top of screen',4,3,0,M_T_Color);
		Draw_Attr(4,3,M_S_Color,1);
		Write('X - Bottom of screen',4,4,0,M_T_Color);
		Draw_Attr(4,4,M_S_Color,1);
		Write('R - Beginning of file',4,5,0,M_T_Color);
		Draw_Attr(4,5,M_S_Color,1);
		Write('C - End of file',4,6,0,M_T_Color);
		Draw_Attr(4,6,M_S_Color,1);
		Write('S - Beginning of line',4,7,0,M_T_Color);
		Draw_Attr(4,7,M_S_Color,1);

		Write('D - End of line',27,3,0,M_T_Color);
		Draw_Attr(27,3,M_S_Color,1);
		Write('B - Block begin',27,4,0,M_T_Color);
		Draw_Attr(27,4,M_S_Color,1);
		Write('K - Block end',27,5,0,M_T_Color);
		Draw_Attr(27,5,M_S_Color,1);
		Write('P - Undo',27,6,0,M_T_Color);
		Draw_Attr(27,6,M_S_Color,1);
		Write('L - Spell checker',27,7,0,M_T_Color);
		Draw_Attr(27,7,M_S_Color,1);

		Read_Key;
		Kill_Box;
		Set_Global_Int('MENU_LEVEL',Global_Int('MENU_LEVEL') - 1);
	}

	if (Key1 != 0) {
/* Convert scan code to uppercase alpha ASCII code */
		RM('WS_PROCESS_KEY1');

		if (Key1 == 83) { /*  S  */
			Goto_Col(1);
		} else if (Key1 == 89) { /*  Y  */
/* Delete to EOL */
			Push_Undo;
			Del_Chars(Length(Get_Line) - c_col + 1);
			Pop_Undo;
		} else if (Key1 == 68) { /*  D  */
/* Move to end of line or column 80 whichever is less */
			Refresh = FALSE;
			Push_Undo;
			Eol;
			if (C_Col > 80) {
				Goto_Col(1);
				Goto_Col(80);
			}
			Pop_Undo;
			Refresh = TRUE;
			Redraw;
		} else if (Key1 == 69) { /*  E  */
			RM('TopWin');
			if (At_Eol) {
				Eol;
			}
		} else if (Key1 == 88) { /*  X  */
			RM('BotWin');
			if (At_Eol) {
				Eol;
			}
		} else if (Key1 == 76) { /*  L  */
			RM('SPELL');
		} else if (Key1 == 82) { /*  R  */
			Tof;
		} else if (Key1 == 67) { /*  C  */
			Eof;
		} else if (Key1 == 70) { /*  F  */
			RM('Meutil2^Search');
		} else if (Key1 == 65) { /*  A  */
			RM('Meutil2^S_Repl');
		} else if (Key1 == 80) { /*  P  */
			Undo;
		} else {
			if (Block_Stat) {
				if (Key1 == 66) { /*  B  */
					RM('TOPBLOCK');
					Redraw;
				} else if (Key1 == 75) { /*  K  */
					RM('ENDBLOCK');
				}
			}
		}
	} else {
		if ((Key2 == 83) | (Key2 == 147)) { /*  <DEL> or <CtrlDEL>  */
/* Delete all chars left of cursor */
			Push_Undo;
			Put_Line(Copy(Get_Line,C_Col,2048));
			Goto_Col(1);
			Redraw;
			Pop_Undo;
		}
	}
}

macro CTRLK {
	str FName[128];

	if (Global_Int('@WS_CTRL_HLP_OFF@')) {
		Make_Message('<CtrlK>');
		Read_Key;
		Make_Message('');
	} else {
		Set_Global_Int('MENU_LEVEL',Global_Int('MENU_LEVEL') + 1);
		Put_Box(2,2,53,11,0,M_B_Color,'<CtrlK>',true);
		Write('D - Save - done',4,3,0,M_T_Color);
		Draw_Attr(4,3,M_S_Color,1);
		Write('Q - Quit',4,4,0,M_T_Color);
		Draw_Attr(4,4,M_S_Color,1);
		Write('S - Save file',4,5,0,M_T_Color);
		Draw_Attr(4,5,M_S_Color,1);
		Write('X - Save and quit',4,6,0,M_T_Color);
		Draw_Attr(4,6,M_S_Color,1);
		Write('J - Delete file',4,7,0,M_T_Color);
		Draw_Attr(4,7,M_S_Color,1);
		Write('R - Merge file from disk',4,8,0,M_T_Color);
		Draw_Attr(4,8,M_S_Color,1);
		Write('K - Mark block end',4,9,0,M_T_Color);
		Draw_Attr(4,9,M_S_Color,1);

		Write('B - Mark block begin',30,3,0,M_T_Color);
		Draw_Attr(30,3,M_S_Color,1);
		Write('C - Copy block',30,4,0,M_T_Color);
		Draw_Attr(30,4,M_S_Color,1);
		Write('V - Move block',30,5,0,M_T_Color);
		Draw_Attr(30,5,M_S_Color,1);
		Write('W - Save block',30,6,0,M_T_Color);
		Draw_Attr(30,6,M_S_Color,1);
		Write('Y - Delete Block',30,7,0,M_T_Color);
		Draw_Attr(30,7,M_S_Color,1);
		Write('H - Hide Block',30,8,0,M_T_Color);
		Draw_Attr(30,8,M_S_Color,1);
		Write('P - Print',30,9,0,M_T_Color);
		Draw_Attr(30,9,M_S_Color,1);

		Read_Key;
		Kill_Box;
		Set_Global_Int('MENU_LEVEL',Global_Int('MENU_LEVEL') - 1);
	}

	if (Key1 != 0) {
/* Convert scan code to uppercase alpha ASCII code */
		RM('WS_PROCESS_KEY1');

		if (Key1 == 89) { /*  Y  */
/* Delete block */
			RM('MEUTIL2^BLOCKOP /BT=2');
		} else if (Key1 == 74) { /*  J  */
/* Delete file from disk */
			FName = '';
			Create_Global_Str('!W_ISTR_1','');
			Create_Global_Str('!W_IPARM_1','/C=0/L=1/W=60/ML=80/H=/T=');
			RM( 'USERIN^DATA_IN /PRE=!W_/#=1/T=NAME OF FILE TO DELETE?/X=3/Y=4');
			if (Return_Int) {
				FName = Global_Str('!W_ISTR_1');
				if (File_Exists(FName)) {
					Del_File(FName);
					if (Error_Level) {
						RM('MEERROR');
					} else {
						Make_Message(Caps(FName) + ' Deleted.');
					}
				} else {
					RM('MEERROR^MessageBox /B=1/T=ERROR/M=FILE ' + Caps(FName) + ' NOT FOUND!');
				}
			}
			Set_Global_Str('!W_ISTR_1','');
		} else if (Key1 == 68) { /*  D  */
			RM('MEUTIL1^SAVEFILE');
			if (Return_Int == 1) {
				RM('MEUTIL1^LOADFILE');
			}
		} else if (Key1 == 81) { /*  Q  */
			RM('EXIT');
		} else if (Key1 == 83) { /*  S  */
			Save_File;
/* If you want to be prompted, which is not the way Wordstar does it, replace
the above line with this one.
			RM('MEUTIL1^SAVEFILE');
 */
		} else if (Key1 == 88) { /*  X  */
			RM('EXIT^AutoSave');
			RM('EXIT');
		} else if (Key1 == 80) { /*  P  */
			RM('MEUTIL3^Print');
		} else if (Key1 == 78) { /*  N  */
			/* ???????????????????????????? */
		} else if (Key1 == 66) { /*  B  */
			Make_Message('Stream Block marking on.  Press Block End to stop.');
			Str_Block_Begin;
		} else if (Key1 == 75) { /*  K  */
			Make_Message('Block marked.');
			Block_End;
		} else if (Key1 == 72) { /*  H  */
/* Hide block */
			Make_Message('Block turned off (hidden).');
			Block_Off;
		} else if (Key1 == 67) { /*  C  */
/* Block copy */
			RM('MEUTIL2^BLOCKOP /BT=0');
		} else if (Key1 == 86) { /*  V  */
/* Block move */
			RM('MEUTIL2^BLOCKOP /BT=1');
		} else if (Key1 == 87) { /*  W  */
			RM('MEUTIL1^SaveBlck');
		} else if (Key1 == 82) { /*  R  */
/* Load block */
			RM('MEUTIL1^Splice');
		}
	}
}

macro CTRLO {

	if (Global_Int('@WS_CTRL_HLP_OFF@')) {
		Make_Message('<CtrlO>');
		Read_Key;
		Make_Message('');
	} else {
		Set_Global_Int('MENU_LEVEL',Global_Int('MENU_LEVEL') + 1);
		Put_Box(2,2,43,7,0,M_B_Color,'<CtrlO>',true);
		Write('C - Center line',4,3,0,M_T_Color);
		Draw_Attr(4,3,M_S_Color,1);
		Write('W - Word wrap ' + COPY('offon',(Not(Wrap_Stat) * 3) + 1,3),4,4,0,M_T_Color);
		Draw_Attr(4,4,M_S_Color,1);
		Write('R - Right margin',4,5,0,M_T_Color);
		Draw_Attr(4,5,M_S_Color,1);

		Write('L - Left margin',22,3,0,M_T_Color);
		Draw_Attr(22,3,M_S_Color,1);
		Write('X - Margin release',22,4,0,M_T_Color);
		Draw_Attr(22,4,M_S_Color,1);
		Write('G - Indent',22,5,0,M_T_Color);
		Draw_Attr(22,5,M_S_Color,1);
		Read_Key;
		Kill_box;
		Set_Global_Int('MENU_LEVEL',Global_Int('MENU_LEVEL') - 1);
	}

	if (Key1 != 0) {
/* Convert scan code to uppercase alpha ASCII code */
		RM('WS_PROCESS_KEY1');

		if (Key1 == 67) { /*  C  */
			RM('TEXT^Center');
		} else if (Key1 == 87) { /*  W  */
			Wrap_Stat = NOT(Wrap_Stat);
		} else if (Key1 == 82) { /*  R  */
			RM('RightMar');
		} else if (Key1 == 76) { /*  L  */
			RM('LeftMar');
		} else if (Key1 == 88) { /*  X  */
			RM('ResetMar');
		} else if (Key1 == 71) { /*  G  */
			RM('Indent');
		}
	}
}

macro CTRLW {
	Push_Undo;
	RM('ScrollDn');
	if (WhereY < (Win_Y2 - 1)) {
		Down;
	}
	Pop_Undo;
}

macro CTRLZ {
	Push_Undo;
	RM('ScrollUp');
	if (WhereY > (Win_Y1 + 1)) {
		Up;
	}
	Pop_Undo;
}

macro DELWORDR {
	str Temp_Line;
	Messages = FALSE;
	Push_Undo;
	if (Cur_Char == ' ') {
		while(  Cur_Char == ' '  ) {
			Del_Char;
		}
		Goto Macro_Exit;
	}
	if (XPos(Cur_Char,'.,:;!?',1)) {
		Del_Char;
		Goto Macro_Exit;
	}
	while ((XPos(Cur_Char,' .,:;!?',1) == 0) & (NOT(At_Eol) | NOT(At_Eof))) {
		if (NOT(At_Eol & (Cur_Char == char(255)))) {
			Del_Char;
		} else {
			Down;
			Temp_Line = Get_Line;
			Del_Line;
			Up;
			Eol;
			Mark_Pos;
			Text(Temp_Line);
			Goto_Mark;
			Goto Macro_Exit;
		}
	}
	if (XPos(Cur_Char,'.,:;!?',1)) {
		Del_Char;
	}
Macro_Exit:
	Pop_Undo;
	Refresh = TRUE;
	Messages = TRUE;
}

macro HOMEKEY {
	Push_Undo;
	if (Global_Int('WS_Mode')) {
		RM('^TopWin');
		Goto_Col(1);
	} else {
		if (C_Col != 1) {
			Goto_Col(1);
		} else {
			RM('^TopWin');
			Goto_Col(1);
		}
	}
	Pop_Undo;
}

macro ENDKEY {
	Push_Undo;
	if (Global_Int('WS_Mode')) {
		RM('^BotWin');
		if (At_Eol) {
			Eol;
		}
	} else {
		if (!(At_Eol)) {
			Eol;
		} else {
			RM('^BotWin');
			if (At_Eol) {
				Eol;
			}
		}
	}
	Pop_Undo;
}

macro RIGHTMAR {
	int J2,J3;

	Set_Global_Int('!W_IINT_1',Right_Margin);
	Create_Global_Str('!W_IPARM_1','/TP=1/C=1/L=1/W=3/H=WP/T=RIGHT MARGIN COLUMN NUMER (<ESC> for cursor column)?/MIN=1/MAX=254');
	RM( 'USERIN^DATA_IN /PRE=!W_/#=1/T=RIGHT MARGIN/X=8/Y=3');
	if (Return_int) {
		J2 = Global_Int('!W_IINT_1');
	} else {
		J2 = C_Col;
	}
	Set_Global_Int('!W_IINT_1',0);
	J3 = Pos('R',Format_Line);
	if (J3 == 0) {
		J3 = Pos('r',Format_Line);
	}
	if (J3 != 0) {
		Format_Line = Str_Del(Format_Line,J3,1);
		Format_Line = Str_Ins(' ',Format_Line,J3);
	}
	Format_Line = Str_Del(Format_Line,J2,1);
	Format_Line = Str_Ins('R',Format_Line,J2);
	Right_Margin = J2;
	Set_Global_Int('Right_Margin',J2);
}

macro LEFTMAR {
	int J2;
	Push_Undo;

	Set_Global_Int('!W_IINT_1',Indent_Level);
	Create_Global_Str('!W_IPARM_1','/TP=1/C=1/L=1/W=3/H=WP/T=LEFT MARGIN COLUMN NUMER (<ESC> for cursor column)?/MIN=1/MAX=254');
	RM( 'USERIN^DATA_IN /PRE=!W_/#=1/T=LEFT MARGIN/X=8/Y=3');
	if (Return_int) {
		J2 = Global_Int('!W_IINT_1');
		Refresh = FALSE;
		Messages = FALSE;
		Mark_Pos;
		Goto_Col(J2);
		Set_Indent_Level;
		Goto_Mark;
		Messages = True;
		Refresh = TRUE;
	} else {
		Set_Indent_Level;
		J2 = C_Col;
	}
	Set_Global_Int('!W_IINT_1',0);

	Set_Global_Int('Left_Margin',J2);
	Pop_Undo;
}

macro RESETMAR {
	Refresh = FALSE;
	Messages = FALSE;
	Mark_Pos;
	if (NOT(Global_Int('Margin_Released'))) {
		Set_Global_Int('Margin_Released',TRUE);
		Right_Margin = 254;
		Goto_Col(1);
	} else {
		Set_Global_Int('Margin_Released',FALSE);
		Goto_Col(Global_Int('Left_Margin'));
		Right_Margin = Global_Int('Right_Margin');
	}
	Set_Indent_Level;
	Goto_Mark;
	Refresh = TRUE;
	Redraw;
	Messages = TRUE;
}

macro INDENT {
	Indent;
	Set_Global_Int('Left_Margin',C_Col);
}

macro UNDENT {
	Undent;
	Set_Global_Int('Left_Margin',C_Col);
}

macro WSHELP {
	help('WORDSTAR^*');
}


macro WSInstall {
	Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $02);
	Set_Global_Str('@KEYMAP_NAME@','KN=WORDSTARFN=WORDSTAR');
}
