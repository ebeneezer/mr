macro_file KEYMAC;
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	KEYMAC

Description: Macros that handle keystroke macros.

KEYMAC_LIST		- Brings up a list of keystroke macros.
KEYMAC_EDIT		- Allows user to edit individual keystrokes in a keystroke macro.
KEYMAC_CHECK	- Checks for key assignment conflicts in keystroke macros.
DOUBLE_127		- Finds any ASCII 127 '' characters in a keystroke macro data
								string and doubles it so it may be stored in .DB format.
KEYMAC_K_PROMPT - Provides a prompt for a keystroke for keystroke macro editing.
KEYMAC_ADD_TEMP - Allows user to save a temporary keystroke macros permanently.
MAKEKEYMAC		- Takes the keystroke macro information and puts it into
								a global variable.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro KEYMAC_LIST trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	KEYMAC_LIST

Description: Brings up a list of keystroke macros.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
/*
@DISPLAY_STRING=D=26K=16
/C=1/L=1/W=25 /T=Description:   /H=KM/DBF=D
/TP=9/C=1/L=2/W=15/T=Key assignment:/H=KM/DBF=K
/TP=10/C=1/L=3/H=INKE/QK=2/T=Mode:
/TP=12/C=2/L=4/H=INKE/T=Edit /DBF=M/DBV=0
/TP=12/C=12/L=4/H=INKE/T=File Manager /DBF=M/DBV=1
/TP=12/C=30/L=4/H=INKE/T=Term /DBF=M/DBV=3
/TP=12/C=40/L=4/H=INKE/T=All /DBF=M/DBV=255
/TP=8/C=0/L=5/W=0/T=/DBF=S/GSET=1/GSTR=@KM!/H=KM/ISTR=Edit keystrokes/M=KEYMAC^KEYMAC_EDIT
****START****
*/
	str K_Str[2048];
	int T_Mode,K1,K2;

/* Unload all keys first */
	RM('KEYMAC_LOAD /LM=2/KW=1');
/* Bring up editing menu */
EDIT_MENU:
	K_Str = '';
	if (global_int('!KMTRANS_COUNT') > 0) {
		Set_Global_Str('@KMB1', '/TP=11/T=Save temporary keystroke macro/KC=<F10>/W=34/K1=0/K2=68/R=100/C=6');
		K_Str = '/EBN=@KMB/EBC=1';
	}
	RM('USERIN^DB /ABT=Playback/CBT=Done/NDF=1/F=MECONFIG/DPT=KEYMAC.DB/NOALPHA=1/PRE=KM/GLO=@KMG#/LO=3/H=KM/S=' +
		Parse_Str('/S=',MParm_Str) +  K_Str +
		'/CANMAC=1/LT=KEYSTROKE MACRO MANAGER/DT=EDITING KEYSTROKE MACRO/MACRO=KEYMAC_CHECK');
	Set_Global_Str('@KMB1', '');

	T_Mode = Return_Int;
	if (T_Mode == 100) {
		RM('KEYMAC_ADD_TEMP');
		Goto EDIT_MENU;
	}
/* Load new list */
	RM('KEYMAC_LOAD');
	if (T_Mode == 1) {
/* Run keystroke macro from menu by temporarily assigning it to a non existant
keystroke, invoking the keystroke, then unassigning it. */
		K_Str = Global_Str('@KMG#');
		T_Mode = Parse_int('M=',K_Str);
		if (T_Mode == mode) {
/* Don't do it unless we are in the same mode */
			set_global_str('@KMTEMP!#',Reconvert_String(Parse_str('S=',K_Str)));
			global_to_key($FFFF,'@KMTEMP!#',T_Mode);
			Pass_Key($FF,$FF);
		}
	}

}

macro KEYMAC_EDIT trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	KEYMAC_EDIT

Description: Allows the user to edit inividual keystrokes in a keystroke macro

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str Key_Buf[2048],T_Return_Str;
	int Index,Active_Window,Menu_Window,K1,K2,Col,Row,Result,Choice,Old_Row,
					T_Insert_Mode,TBc,T_Refresh,T_Truncate_Spaces;

	Active_Window = Window_Id;
	T_Truncate_Spaces = Truncate_Spaces;
	T_Return_Str = Return_Str;
	T_Refresh = Refresh;
	Refresh = False;
	T_Insert_Mode = Insert_Mode;
	TBC = Box_Count;
	Choice = 1;
	Old_Row = 1;
	Row = Parse_Int('/Y=',MParm_Str);
	if (Row < 4) {
		Row = 4;
	}
	Col = Parse_Int('/X=',MParm_Str);
	if (Col < 1) {
		Col = 1;
	}
	Key_Buf = reconvert_string(Global_Str('@KM!'));
	Create_Window;
	Menu_Window = Window_Id;
	File_Name = Fexpand('KEYMAC.TMP');
	Index = 0;
	while (Index < Svl(Key_Buf)) {
		++Index;
		K1 = Ascii(Str_Char(Key_Buf,Index));
		++Index;
		K2 = Ascii(Str_Char(Key_Buf,Index));
		RM('SETUP^MAKEKEY /K1=' + Str(K1) + '/K2=' + Str(K2));
		Put_Line(Return_Str);
		Down;
	}
	File_Changed = False;

KEY_MENU:
	goto_line( choice );
	Set_Global_Str('KMEIPARM_1', '/TP=11/T=Add/KC=<INS>/K1=0/K2=82/R=12/L=1/C=1');
	Set_Global_Str('KMEIPARM_2', '/TP=11/T=Delete/KC=<DEL>/K1=0/K2=83/R=3/L=1/C=10');

	Set_Global_Str('KMEIPARM_3', '/TP=11/T=Change/KC=<F3>/K1=0/K2=61/R=4/L=1/C=22');
	Set_Global_Str('KMEIPARM_4', '/TP=15/L=3/C=1/MW=29/OR=' + str(old_row) + '/WIN=' + str(cur_window));
	RM('USERIN^DATA_IN /A=2/DC=1/NK=1/PRE=KME/#=4/T=EDIT KEYSTROKES/H=EDITKS/X=' +
		Str(Col) + '/Y=' + Str(Row) + '/S=4' );
	col = global_int('!DATA_IN_X');
	row = global_int('!DATA_IN_Y');
	Choice = C_Line;
	Old_Row = C_Row;
	Result = Return_Int;

	if (Result == 0) {
/* Abort edit */
		if (Global_Str('@KM!') != convert_string(Key_Buf)) {
			RM('USERIN^VERIFY /T=Abort the changes?/C=' + Str(Col + 2) + '/L=' + Str(Row + 2));
			if (Return_Int) {
				Goto EXIT;
			}
			Kill_Box;
			Goto KEY_MENU;
		} else {
			Goto EXIT;
		}
	}
	if (Result == 1) {
/* Exit and save */
		Set_Global_Str('@KM!',convert_string(Key_Buf));
		Goto EXIT;
	}
	if (Result == 12) {
/* Add a keystroke */
		RM('KEYMAC^KEYMAC_K_PROMPT /T=ADDING A KEYSTROKE/X=' + Str(Col) +
				'/Y=' + Str(Row + Choice));
		if (Get_Line != '') {
			Insert_Mode = True;
			Eol;
			Cr;
		}
		Truncate_Spaces = False;
		Put_Line(Return_Str);
		Choice = C_Line;
		Key_Buf = Str_Ins(Char(Key1) + Char(Key2),Key_Buf,(C_Line * 2) - 1);
		Kill_Box;
		Goto KEY_MENU;
	}
	if (Result == 3) {
/* Delete a keystroke */
		Key_Buf = Str_Del(Key_Buf,(C_Line * 2) - 1,2);
		Del_Line;
		if (At_Eof) {
			--Choice;
		}
		Kill_Box;
		Goto KEY_MENU;
	}
	if (Result == 4) {
/* change a keystroke */
		Key_Buf = Str_Del(Key_Buf,(C_Line * 2) - 1,2);
		RM('KEYMAC^KEYMAC_K_PROMPT /T=CHANGING A KEYSTROKE/X=' + Str(Col) +
				'/Y=' + Str(Row + Old_Row + 1));
		Truncate_Spaces = False;
		Put_Line(Return_Str);
		Key_Buf = Str_Ins(Char(Key1) + Char(Key2),Key_Buf,(C_Line * 2) - 1);
		Kill_Box;
		Goto KEY_MENU;
	}

EXIT:
/* We must double any "" characters even if we did not edit the string */
	if (Result == 0) {
		Key_Buf = reconvert_string(Global_Str('@KM!'));
	}
	Set_Global_Str('@KEYBUF',Key_Buf);
	RM('Double_127');
	Set_Global_Str('@KM!',convert_string(Global_Str('@KEYBUF')));

	while (Box_Count > TBC) {
		Kill_Box;
	}
	if (Switch_Win_Id(Menu_Window)) {
		Delete_Window;
	}
	if (Switch_Win_Id(Active_Window)) {
	}
	Insert_Mode = T_Insert_Mode;
	Return_Str = T_Return_Str;
	Refresh = T_Refresh;
	Set_Global_Int('KEYMAC_EDIT_RUN',1);
}

macro KEYMAC_CHECK trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	KEYMAC_CHECK

Description: Checks for key assignment conflicts in keystroke macros.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int T_Refresh,
					k_int,
					M_Int,
					K_line,
					T_Reg_Exp_stat,
					K_Type,
					Choice,
					fix_flag = 0,
					Cancelled = Parse_Int('/C=',MParm_Str);
	str K_Str[30],E_Str[20],M_Str[15],T_Rec[2048];

	if (Parse_Int('/P=',Mparm_str) || Cancelled) {
		T_Refresh = Refresh;
		Refresh = False;

		T_Rec = Global_Str('@KMG#');
		Fix_Flag = 0;
		Call FIX_127;
		if ((Cancelled == 0) & (Global_Int('KEYMAC_EDIT_RUN') == 0)) {
			RM('DOUBLE_127');
		}
		K_Str = Parse_Str('D=',T_Rec);
		if (Parse_Str('D=',T_Rec) == '') {
/* Don't allow a blank description, put a default one in */
			K_Str = '(unnamed)';
		}

		Set_Global_Str('@KMG#',
			'D=' + K_Str +
			'K=' + Parse_Str('K=',T_Rec) +
			'M=' + Parse_Str('M=',T_Rec) +
			'S=' + Global_Str('@KEYBUF'));

RECHECK:
		Choice = 0;
		T_Rec = Global_Str('@KMG#');
		K_int = Parse_int('K=',T_Rec);
		M_int = Parse_int('M=',T_Rec);
		if (K_int > 0) {

/* First, search for a conflict in the list */
			T_Reg_Exp_Stat = Reg_exp_stat;
			Mark_Pos;
			K_Line = C_Line;
			Tof;
			Reg_exp_stat = true;

Search_AGAIN:
			if (Search_Fwd('K=' + Str(K_Int) + '||$',0)) {
				if (C_Line == K_Line) {
					Eol;
					Goto SEARCH_AGAIN;
				}
				K_Type = -1;
				Goto KEYSTROKE_CONFLICT;
			}
/* Next, check for a conflict in memory */
			K_Type = Inq_Key(K_Int & $FF,(K_Int >> 8) & $FF,M_Int,K_Str);
/*
Beep;
Make_Message('K_Type=' + Str(K_Type) + ' Key1=' + Str(K_Int and $FF) +
' Key2=' + Str((K_Int shr 8) and $FF) + ' M_Int=' + Str(M_Int));
Read_Key;
 */
			if (K_Type) {
				M_Str = 'Ignore(KM)';
				if (K_Type == 1) {
					E_Str = 'macro.';
				} else if (K_Type == 2) {
KEYSTROKE_CONFLICT:
					E_Str = 'keystroke macro.';
					M_Str = 'Replace(KM)';
				} else {
					E_Str = 'command.';
				}
				RM('SETUP^MAKEKEY /K1=' + Str(K_Int & $FF) + '/K2=' +
					Str((K_Int >> 8) & $FF));
				RM('MEERROR^Beeps /C=1');
				Put_Box(15,5,55,10,0,M_B_Color,'WARNING',True);
				Write('The Key: ' + Return_Str + ' is already',17,6,0,M_B_Color);
				Write('assigned to a ' + E_Str,17,7,0,M_B_Color);
				RM('USERIN^XMENU /T=0/X=17/Y=8/S=1/M=Assign different key(KM)Cancel()'
					+ M_Str);
				Choice = Return_Int;
				if (Choice == 1) {
					RM('KEYMAC^KEYMAC_K_PROMPT /T=ENTER A NEW KEY ASSIGNMENT/X=20/Y=7');
					Fix_Flag = 0;
					Call FIX_127;
					Set_Global_Str('@KMG#','D=' + Parse_Str('D=',T_Rec) +
					 'K=' + Str(Return_Int)+
						'M=' + Str(M_Int) + 'S=' + Global_Str('@KEYBUF'));

				} else if (Choice == 3) {
					if (K_Type == -1) {
/* Erase the conflicting keystroke.  Leave the new one intact. */
						Fix_Flag = 1;
						Call FIX_127;
						Put_Line('D=' + Parse_Str('D=',Get_Line) +
							'M=' + Parse_Str('M=',Get_Line) +
							'S=' + Global_Str('@KEYBUF'));

						Set_Global_Int('@DB_NEED_REBUILD!',1);
					}
				} else {
					Fix_Flag = 0;
					Call FIX_127;
					Set_Global_Str('@KMG#','D=' + Parse_Str('D=',T_Rec) +
						'M=' + Str(M_Int) + 'S=' + Global_Str('@KEYBUF'));
				}
				Kill_Box;
			}

			Goto_Mark;
			Reg_Exp_Stat = T_Reg_exp_stat;

			if (Choice == 1) {
				Goto RECHECK;
			}

		}

		Refresh = T_Refresh;
	}
	Goto EXIT;

FIX_127:
	if (Fix_Flag) {
		Fix_Flag = XPos('S=',Get_Line,1);
		if (Fix_Flag) {
			Set_Global_Str('@KEYBUF',Copy(Get_Line,Fix_Flag + 3,2048));
		}
	} else {
		Fix_Flag = XPos('S=',T_Rec,1);
		if (Fix_Flag) {
			Set_Global_Str('@KEYBUF',Copy(T_Rec,Fix_Flag + 3,2048));
		}
	}
	if (Fix_Flag == 0) {
		Set_Global_Str('@KEYBUF','');
	}
	Fix_Flag = 0;
	RET;

EXIT:
	Set_Global_Int('KEYMAC_EDIT_RUN',0);
	Set_Global_Str('@KEYBUF','');
	Return_Int = 1;

}

macro DOUBLE_127 trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	DOUBLE_127

Description: Finds any ASCII 127 '' characters in a keystroke macro data
						 string and doubles it so it may be stored in .DB format.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str Buffer[2048];
	int K_Int;

	Buffer = Global_Str('@KEYBUF');
	K_Int = -1;
LOOP:
	K_Int = XPos('|127',Buffer,K_Int + 2);
	if (K_Int) {
		Buffer = Str_Ins('|127',Buffer,K_Int);
		Goto LOOP;
	}
	Set_Global_Str('@KEYBUF',Buffer);

}

macro KEYMAC_K_PROMPT trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	KEYMAC_K_PROMPT

Description: Provides a prompt for a keystroke for keystroke macro editing.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Col,Row,Width;
	str Title[80];

	Row = Parse_Int('/Y=',MParm_Str) + 3;
	if (Row < 4) {
		Row = 4;
	}
	if (Row > (Max_Window_Row - 4)) {
		Row = 4;
	}
	Col = Parse_Int('/X=',MParm_Str) + 1;
	if (Col < 1) {
		Col = 1;
	}
	Title = Parse_Str('/T=',MParm_Str);
	Width = 25;
	if ((Svl(Title) + 3) > Width) {
		Width = Svl(Title) + 3;
	}

	Put_Box(Col,Row,Col + Width,Row + 3,0,M_B_Color,Title,true);
	Write('Press the desired key.',(Col + 1) + (((Width - 3) - 22) / 2),Row + 1,0,M_B_Color);
	Read_Key;
	Return_Int = (Key2 << 8) | Key1;

	RM('SETUP^MAKEKEY /K1=' + Str(Key1) + '/K2=' + Str(key2));
	Kill_Box;

}

macro KEYMAC_ADD_TEMP trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	KEYMAC_ADD_TEMP

Description: Allows user to save a temporary keystroke macros permanently.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Amount = Global_Int('!KMTRANS_COUNT'),Active_Window = Window_Id,
					T_Refresh = Refresh,Count = 0,
					T_Key;

	Refresh = False;
	if (Amount < 1) {
		Goto EXIT;
	}
	Create_Window;
	while (Count < Amount) {
		++Count;
		t_key = global_Int('!KMTRANS_KEY' + str(Count));
		RM('SETUP^MAKEKEY /K1=' + Str(T_Key & 0xFF)
			 + '/K2=' + Str((T_Key >> 8) & 0xFF));
		Put_Line(Return_Str);
		Down;
	}
	Tof;
	Set_Global_Str('KMEIPARM_1', '/TP=15/L=1/C=2/MW=29/WIN=' + str(cur_window));
	RM('USERIN^DATA_IN /A=2/DC=1/PRE=KME/#=1/T=SELECT TEMPORARY MACRO TO SAVE/H=/S=1');
	if (Return_Int == 1) {
		Count = C_Line;
		t_key = global_Int('!KMTRANS_KEY' + str(Count));
		RM('SETUP^MAKEKEY /K1=' + Str(T_Key & 0xFF)
			 + '/K2=' + Str((T_Key >> 8) & 0xFF));
		Set_Global_Str('@KEYBUF',Global_Str('!KMTRANS_BUF' + str(Count)));
		RM('KEYMAC^KEYMAC_LOAD /KW=1/LM=1/M=' + Str(Mode) + '/D=' + Return_Str +
				'/K=' + Str(global_Int('!KMTRANS_KEY' + str(Count))));

/* Get rid of the formerly temporary keystroke macro from the list */
		Return_int = count;
		RM("DELETEITEM /G=!KMTRANS_BUF/T=0/#=" + Str(amount));
		Return_int = count;
		RM("DELETEITEM /G=!KMTRANS_KEY/T=1/#=" + Str(amount));
		Set_Global_Int('!KMTRANS_COUNT',amount - 1);

	}
	Delete_Window;
EXIT:
	Switch_Win_Id(Active_Window);
	Refresh = T_Refresh;
}

/********************************MULTI-EDIT MACRO******************************

Name: MakeKeyMac

Description:  Takes the keystroke macro information and puts it into
	a global variable.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro MakeKeyMac trans2 {
	int count;

	/* Flush the keystroke buffer */
	count = 0;
	while (check_key && (count < 100)) {
		++count;
	}
	RM('USERIN^XMENU /B=1/S=2/T=0/L=KEYSTROKES RECORDED/M=Save-permanently(KM)Current-session-only()');
	if (Return_int == 1) {
/* Add the macro to the list */
		Set_Global_Str('@KEYBUF',Get_Record_Buffer);
		RM('KEYMAC_LOAD /KW=1/LM=1/M=' + Str(Mode));
/* Pop up the list and immediately get into the data_in screen for the new macro */
		Push_Key(0,61);
		Push_Key(0,79);
		RM('KEYMAC_LIST');
	} else if (Return_Int == 2) {
		RM('KEYMAC^KEYMAC_K_PROMPT /T=DEFINE KEY ASSIGNMENT');
		Count = global_int('!KMTRANS_COUNT') + 1;
		set_global_int('!KMTRANS_COUNT',Count);
		set_global_str('!KMTRANS_BUF' + str(Count),Get_Record_Buffer);
		set_global_int('!KMTRANS_KEY' + str(Count),Return_Int);
		global_to_key(Return_Int, '!KMTRANS_BUF' + str(Count),Mode);
	}
	Update_Status_Line;
EXIT:
}
