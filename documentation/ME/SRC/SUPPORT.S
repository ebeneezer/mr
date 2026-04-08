macro_file SUPPORT;
/*******************************************************************************
															MACRO FILE SUPPORT

LINEDRAW - Line and box drawing
ASCII - ASCII table
CONDENSE - Condensed display
SQUEEZE - Convert tabs to spaces, or vice versa

								Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

macro LINEDRAW TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: LINEDRAW

Description: This macro allows the user to draw the single and double line
characters using the arrow keys to create lines and boxes etc.

							(C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

/*
Here is a list of all of the linedrawing characters:
≥¥µ∂∑∏π∫ªºΩæø¿¡¬√ƒ≈∆«»… ÀÃÕŒœ–—“”‘’÷◊ÿŸ⁄
 */
/*  Define variables  */

	char U_Char,D_Char,L_Char,R_Char;
	int Temp_Integer,Present_Direction,A_Left,A_Right,A_Up,A_Down,
					Draw_Mode,
					Temp_Insert = Insert_mode,
					D_Mode,U_Mode,L_Mode,R_Mode,
					Temp_Tab_Expand = Tab_Expand,
					K1,K2,Same_Direction,
					t_undo_stat = undo_stat;

/*  Store current status of certain stuff so we can restore it upon exit.  */
	Insert_Mode = False;
	Tab_Expand = False;
	Push_Labels;
	refresh = TRUE;
	Redraw;
/*  Initialize variables  */

	Present_Direction = 0;
	A_Left = 1;
	A_Right = 2;
	A_Up = 3;
	A_Down = 4;

	Draw_Mode = 1;
  MAKE_MESSAGE('SINGLE LINE: |26=move, Shift|26=draw, F2=Œ, Esc=quit.');
	FLABEL('Double',2,$FF);
	push_undo;
	Goto WAIT_FOR_KEYPRESS;

GO_LEFT:
	Left;

WAIT_FOR_KEYPRESS:
	pop_undo;
	READ_KEY;
	push_undo;

GET_KEY_CODES:
	K1 = Key1;
	K2 = Key2;

FLUSH_KEY_REPEAT:
/* This is to take care of key type ahead filling up the buffer */
	if(  (Check_Key)  ) {
		if(  (key1 == k1) & (key2 == k2)  ) {
			goto FLUSH_KEY_REPEAT;
		} else {
			push_key(key1,key2);
		}
	}

/* This Shift_Stat stuff is kind of a kludge to work with extended keyboard.  It
is possible that this will not work properly 100% of the time
	R_AX = $0200;
	INTR($16);
	Shift_Stat = ((R_AX & $0003) > 0);
	Insert_Mode = False;
*/
	if(  (K1 == 27)  ) {
		Make_Message('');
		Goto END_OF_MAC; /*  If so, then user pressed <ESC> so... */
	}
	if(  K2 == 59  ) {
		Help('LD');
	}
	if(  K2 == 60  ) {
		if(  (Draw_Mode < 3)  ) {
			++Draw_Mode;
		} else {
			Draw_Mode = 1;
		}

		MAKE_MESSAGE(Copy(
'SINGLE LINE: |26=move, Shift|26=draw, F2=Œ, Esc=quit.    DBL LINE: |26=move, Shift|26=draw, F2=erase, Esc=quit.   ERASE: |26=move, Shift|26=erase, F2=≈, Esc=quit.         '
                                        ,((Draw_Mode - 1) * 59) + 1,59));
		FLABEL(Copy('DoubleErase Single',((Draw_Mode - 1) * 6) + 1,6),2,$FF);
		Goto WAIT_FOR_KEYPRESS;
	}

	if(  (K2 == 73)  ) {
		Page_Up;
	}

	if(  (K2 == 81)  ) {
		Page_Down;
	}

	if(  (K2 == 71)  ) {
		Home;
	}

	if(  (K2 == 79)  ) {
		Eol;
	}

/* We translate mouse cursor key codes up here to simplify things down there
	if(  (K2 > 239)  ) {
		if(  (K2 == 240)  ) {
			K2 = 72;
		}
		if(  (K2 == 241)  ) {
			K2 = 80;
		}
		if(  (K2 == 242)  ) {
			K2 = 75;
		}
		if(  (K2 == 243)  ) {
			K2 = 77;
		}
	}
*/
	if (K2 == 77) {                                       /* Right arrow */
		if(  c_col > 252  ) { /* Don't allow cursor to wrap down */
			Goto WAIT_FOR_KEYPRESS;
		}
		if (K1 == 0) { /* If unshifted, then move right */
			Right;
			Present_Direction = 0;
			Goto WAIT_FOR_KEYPRESS;
		}
		if (K1 == 54) { /* If shifted, then insert the appropriate char */
			if(  (Draw_Mode == 3)  ) {
				Text(' ');
				Present_Direction = 0;
				Goto WAIT_FOR_KEYPRESS;
			}
			Same_Direction = (Present_Direction == A_Right);
			if (Same_Direction) {	/* If we were previously going a
different direction, then insert char at current cusror position. Otherwise,
insert char in the next right position */
				Right;
			}
			Present_Direction = A_Right;
			Call LOOK_AROUND;

			Temp_Integer = XPos(L_Char,'ø∑∏ªŸæΩº¥∂πµ',1);
			if ((Temp_Integer) && (Same_Direction)) {
				Left;
				Text(Copy('¬“¬“¡¡––≈◊◊≈ÀÀ—À œ  ŒŒŒÿ',((Draw_Mode - 1) * 12) + Temp_Integer,1));
				Goto GO_LEFT;
			}

			if ((L_Mode > 0) & ((D_Mode + U_Mode + R_Mode) == 0)) {
				Text(Copy('ƒÕ',Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  ((D_Mode == 1) & (U_Mode == 1))  ) {
				Text(Copy('√∆√∆∆Ã√∆¥π≈Œ¥π√∆µµµµÿŒ',((L_Mode * 8) + (R_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  ((D_Mode == 2) & (U_Mode == 2))  ) {
				Text(Copy('«Ã«Ã«Ã«Ã∂π◊Œ∂Ã«Ã¥π¥π≈Œ',((L_Mode * 8) + (R_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (D_Mode == 1)  ) {
				Text(Copy('⁄’⁄’ø∏⁄’øª¬Àøª⁄’ø∏⁄∏¬—',((L_Mode * 8) + (R_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (D_Mode == 2)  ) {
				Text(Copy('÷…÷…÷…÷…∑ª“À∑ª÷…∑ª÷ª“À',((L_Mode * 8) + (R_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (U_Mode == 2)  ) {
				Text(Copy('”»”»”»”»Ωº– Ωº”»Ωº”º– ',((L_Mode * 8) + (R_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (U_Mode == 1)  ) {
				Text(Copy('¿‘¿‘Ÿæ¿‘Ÿº¡ Ÿº¿‘Ÿæ¿æ¡œ',((L_Mode * 8) + (R_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

/* If no other condition exists... */
			Text(Copy('ƒÕ',Draw_Mode,1));
			Goto GO_LEFT;
		}
	}

	if(  (K2 == 75)  ) {                                              /* Left arrow */
		if(  c_col == 1  ) { /* Don't allow cursor to wrap up */
			Goto WAIT_FOR_KEYPRESS;
		}
		if(K1 == 0) { /* If unshifted, then move left */
			Left;
			Present_Direction = 0;
			Goto WAIT_FOR_KEYPRESS;
		}
		if (K1 == 52) { /* If shifted, then insert the appropriate char */
			if(  (Draw_Mode == 3)  ) {
				Text(' ');
				Left;
				Left;
				Present_Direction = 0;
				Goto WAIT_FOR_KEYPRESS;
			}
			Same_Direction = (Present_Direction == A_Left);
			if (Same_Direction) {	/* If we were previously going a
different direction, then insert char at current cusror position. Otherwise,
insert char in the next left position */
				Left;
			}
			Present_Direction = A_Left;
			Call LOOK_AROUND;

			Temp_Integer = XPos(R_Char,'⁄÷’…¿‘”»√«Ã∆',1);
			if ((Temp_Integer) && (Same_Direction)) {
				Right;
				Text(Copy('¬“¬“¡¡––≈◊◊≈ÀÀ—À œ  ŒŒŒÿ',((Draw_Mode - 1) * 12) + Temp_Integer,1));
				Goto GO_LEFT;
			}

			if(  ((R_Mode > 0) & ((D_Mode + U_Mode + L_Mode) == 0))  ) {
				Text(Copy('ƒÕ',Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  ((D_Mode == 1) & (U_Mode == 1))  ) {
				Text(Copy('¥µ√Ã∆∆¥µ¥µ≈Œ¥∆¥µµπ√ÃÿŒ',((L_Mode * 8) + (R_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}


			if(  ((D_Mode == 2) & (U_Mode == 2))  ) {
				Text(Copy('∂π«Ã√Ã∂π∂π◊Œ√Ã∂π∂π«π≈Œ',((L_Mode * 8) + (R_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (D_Mode == 1)  ) {
				Text(Copy('ø∏⁄…⁄’ø∏ø∏¬Àø’ø∏⁄’⁄…¬—',((L_Mode * 8) + (R_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (D_Mode == 2)  ) {
				Text(Copy('∑ª÷…÷…∑ª∑ª“À∑…∑ª∑ª÷…“À',((L_Mode * 8) + (R_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (U_Mode == 2)  ) {
				Text(Copy('Ωº”»”»ΩºΩº– Ω»ΩºΩº”»– ',((L_Mode * 8) + (R_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (U_Mode == 1)  ) {
				Text(Copy('Ÿæ¿»¿‘ŸæŸæ¡ Ÿ‘ŸæŸæ¿»¡œ',((L_Mode * 8) + (R_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

/* If no other condition exists... */
			Text(Copy('ƒÕ',Draw_Mode,1));
			Goto GO_LEFT;
		}
	}

	if(  (K2 == 72)  ) {                                                /* Up arrow */
		if(  C_Line == 1  ) {
			Goto WAIT_FOR_KEYPRESS;
		}
		if (K1 == 0) { /* If unshifted, then move left */
			Up;
			Present_Direction = 0;
			Goto WAIT_FOR_KEYPRESS;
		}
		if (K1 == 56) { /* If shifted, then insert the appropriate char */
			if(  (Draw_Mode == 3)  ) {
				Text(' ');
				Left;
				Up;
				Present_Direction = 0;
				Goto WAIT_FOR_KEYPRESS;
			}
			Same_Direction = (Present_Direction == A_Up);
			if (Same_Direction) {	/* If we were previously going a
different direction, then insert char at current cusror position. Otherwise,
insert char in the next up position */
				Up;
			}
			Present_Direction = A_Up;
			Call LOOK_AROUND;


			Temp_Integer = XPos(D_Char,'ø∏∑ª⁄÷’…¬—À“',1);
			if ((Temp_Integer) && (Same_Direction)) {
				Down;
				Text(Copy('¥µ¥µ√√∆∆≈ÿÿ≈ππ∂πÃ«ÃÃŒŒŒ◊',((Draw_Mode - 1) * 12) + Temp_Integer,1));
				Goto GO_LEFT;
			}

			if(  ((D_Mode > 0) & ((R_Mode + U_Mode + L_Mode) == 0))  ) {
				Text(Copy('≥∫',Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  ((L_Mode == 1) & (R_Mode == 1))  ) {
				Text(Copy('¡–¬À““¡–¡–≈Œ¡“¡–– ¬À◊Œ',((U_Mode * 8) + (D_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  ((L_Mode == 2) & (R_Mode == 2))  ) {
				Text(Copy('œ —À¬Àœ œ ÿŒ¬Àœ œ — ≈Œ',((U_Mode * 8) + (D_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (L_Mode == 1)  ) {
				Text(Copy('ŸΩøªø∑ŸΩŸΩ¥πŸ∑ŸΩŸΩøª¥∂',((U_Mode * 8) + (D_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (L_Mode == 2)  ) {
				Text(Copy('æº∏ª∏ªæºæºµπæªæºæº∏ªµπ',((U_Mode * 8) + (D_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (R_Mode == 2)  ) {
				Text(Copy('‘»’…’…‘»‘»∆Ã‘…‘»‘»’…∆Ã',((U_Mode * 8) + (D_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (R_Mode == 1)  ) {
				Text(Copy('¿”⁄…⁄÷¿”¿”√Ã¿÷¿”¿”⁄…√«',((U_Mode * 8) + (D_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

/* If no other condition exists... */
			Text(Copy('≥∫',Draw_Mode,1));
			Goto GO_LEFT;
		}
	}

	if(  (K2 == 80)  ) {                                              /* Down arrow */
		if (K1 == 0) { /* If unshifted, then move right */
			Down;
			Present_Direction = 0;
			Goto WAIT_FOR_KEYPRESS;
		}
		if (K1 == 50) { /* If shifted, then insert the appropriate char */
			if(  (Draw_Mode == 3)  ) {
				Text(' ');
				Left;
				Down;
				Present_Direction = 0;
				Goto WAIT_FOR_KEYPRESS;
			}
			Same_Direction = (Present_Direction == A_Down);
			if (Same_Direction) {	/* If we were previously going a
different direction, then insert char at current cusror position. Otherwise,
insert char in the next down position */
				Down;
			}
			Present_Direction = A_Down;
			Call LOOK_AROUND;

			Temp_Integer = XPos(U_Char,'ŸæΩº¿”‘»¡œ –',1);
			if ((Temp_Integer) && (Same_Direction)) {
				Up;
				Text(Copy('¥µ¥µ√√∆∆≈ÿÿ≈ππ∂πÃ«ÃÃŒŒŒ◊',((Draw_Mode - 1) * 12) + Temp_Integer,1));
				Goto GO_LEFT;
			}

			if(  ((U_Mode > 0) & ((R_Mode + D_Mode + L_Mode) == 0))  ) {
				Text(Copy('≥∫',Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  ((L_Mode == 1) & (R_Mode == 1))  ) {
				Text(Copy('¬“¬““À¬“¡ ≈Œ¡ ¬“––¬–◊Œ',((U_Mode * 8) + (D_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}


			if(  ((L_Mode == 2) & (R_Mode == 2))  ) {
				Text(Copy('—À—À—À—Àœ ÿŒœÀ—À¡ ¡ ≈Œ',((U_Mode * 8) + (D_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (L_Mode == 1)  ) {
				Text(Copy('ø∑ø∑ø∑ø∑Ÿº¥πŸºø∑ŸΩøΩ¥∂',((U_Mode * 8) + (D_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (L_Mode == 2)  ) {
				Text(Copy('∏ª∏ª∏ª∏ªæºµπæº∏ªæº∏ºµπ',((U_Mode * 8) + (D_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (R_Mode == 2)  ) {
				Text(Copy('’…’…’…’…‘»∆Ã‘»’…‘»’»∆Ã',((U_Mode * 8) + (D_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

			if(  (R_Mode == 1)  ) {
				Text(Copy('⁄÷⁄÷⁄÷⁄÷¿»√Ã¿»⁄÷¿”⁄”√«',((U_Mode * 8) + (D_Mode * 2)) + Draw_Mode,1));
				Goto GO_LEFT;
			}

/* If no other condition exists... */
			Text(Copy('≥∫',Draw_Mode,1));
			Goto GO_LEFT;
		}
	}


/*******************************************************************************/
	Goto WAIT_FOR_KEYPRESS;


LOOK_AROUND:
/*  This subroutine looks at all chars surrounding CUR_CHAR and stores special
values into variables based on what it finds  */

	Undo_stat = False;
	if(  (((C_Row + 2) > Win_Y1) & ((C_Row + 4) < Win_Y2))  ) {
		Refresh = False;
	}

	if(  C_Line > 1  ) {
		UP;
		U_Char = Cur_Char;
		RIGHT;
		DOWN;
	} else {
		U_Char = '|0';
		Right;
	}
	R_Char = Cur_Char;
	DOWN;
	LEFT;
	D_Char = Cur_Char;
	if(  c_col > 1  ) {
		LEFT;
		UP;
		L_Char = Cur_Char;
		RIGHT;
	} else {
		L_Char = '|0';
		Up;
	}
	Refresh = True;

	D_mode = XPos(D_Char,'≥¥µæ¿¡√≈∆‘ÿŸœ∂π∫ºΩ«» ÃŒ–”◊',1);
	D_mode = (D_Mode > 0) + (D_Mode > 13);
	U_mode = XPos(U_Char,'≥¥µ∏ø¬√≈∆—’ÿ⁄∂∑π∫ª«…ÀÃŒ“÷◊',1);
	U_mode = (U_Mode > 0) + (U_Mode > 13);
	L_mode = XPos(L_Char,'¿¡¬√ƒ≈«–“”÷◊⁄∆»… ÀÃÕŒœ—‘’ÿ',1);
	L_mode = (L_Mode > 0) + (L_Mode > 13);
	R_mode = XPos(R_Char,'¥∂∑Ωø¡¬ƒ≈–“◊Ÿµ∏πªºæ ÀÕŒœ—ÿ',1);
	R_mode = (R_Mode > 0) + (R_Mode > 13);
	Undo_stat = t_undo_stat;
	RET;

END_OF_MAC:

	Pop_Undo;
	INSERT_MODE = Temp_Insert; /*  Switch previous insert/overwrite mode  */
	Tab_Expand = Temp_Tab_Expand;
	Pop_Labels;
	Undo_Stat = T_undo_stat;

}

macro ASCII FROM ALL TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: ASCII

Description: Displays an ASCII table of the extended IBM character set

							(C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  UL_COL, UL_ROW, LR_COL, LR_ROW, RX, CX, CP , CI;
	str Temp_String;

	UL_COL = 3;
	UL_ROW = 3;
	LR_COL = 75;
	LR_ROW = 24;

	PUT_BOX(UL_COL,UL_ROW,LR_COL,LR_ROW,0,m_B_COLOR,'ASCIIƒTABLE',true);
	GOTOXY(1,SCREEN_LENGTH + 1 );

	Set_Global_Str('EVASCII1',
			'/T=Prev Page/KC=<UP>/3D=1/W=13/K1=0/K2=72/R=1/Y=' + str(lr_row - 3) +
									'/X=' + str( ul_col + 10 ));
	Set_Global_Str('EVASCII2',
			'/T=Next Page/KC=<DOWN>/3D=1/W=15/K1=0/K2=80/R=2/Y=' + str(lr_row - 3) +
									'/X=' + str( ul_col + 28 ));
	Set_Global_Str('EVASCII3',
			'/T=Done/KC=<ESC>/3D=1/W=9/K1=27/K2=1/R=3/Y=' + str(lr_row - 3) +
									'/X=' + str( ul_col + 47 ));
	Set_Global_Str('EVASCII4','/K1=0/K2=73/R=1/ND=1');
	Set_Global_Str('EVASCII5','/K1=0/K2=81/R=2/ND=1');
	Set_Global_Str('EVASCII6','/K1=13/K2=28/R=3/ND=1');

	CP = Global_Int('ASCII_PAGE');
	if(  (CP == 3)  ) {
		Goto START_PAGE3;
	}
	if(  (CP == 2)  ) {
		Goto START_PAGE2;
	}

START_PAGE1:

	CP = 1;
	RX = UL_ROW + 1;
	CX = UL_COL + 1;

	Temp_String = '00 00    ^@ NUL ≥ 16 10   ^P DLE ≥  32 20    ≥  48 30  0 ≥  64 40  @01 01   ^A SOH ≥ 17 11   ^Q DC1 ≥  33 21  ! ≥  49 31  1 ≥  65 41  A02 02   ^B STX ≥ 18 12   ^R DC2 ≥  34 22  " ≥  50 32  2 ≥  66 42  B';
	Call WRITE_TABLE;
	Temp_String = '03 03   ^C ETX ≥ 19 13   ^S DC3 ≥  35 23  # ≥  51 33  3 ≥  67 43  C04 04   ^D EOT ≥ 20 14   ^T DC4 ≥  36 24  $ ≥  52 34  4 ≥  68 44  D05 05   ^E ENQ ≥ 21 15   ^U NAK ≥  37 25  % ≥  53 35  5 ≥  69 45  E';
	Call WRITE_TABLE;
	Temp_String = '06 06   ^F ACK ≥ 22 16   ^V SYN ≥  38 26  & ≥  54 36  6 ≥  70 46  F07 07   ^G BEL ≥ 23 17   ^W ETB ≥  39 27  '' ≥  55 37  7 ≥  71 47  G08 08 |8  ^H BS  ≥ 24 18   ^X CAN ≥  40 28  ( ≥  56 38  8 ≥  72 48  H';
	Call WRITE_TABLE;
	Temp_String = '09 09 |9  ^I HT  ≥ 25 19   ^Y EM  ≥  41 29  ) ≥  57 39  9 ≥  73 49  I10 0A 
  ^J LF  ≥ 26 1A |26  ^Z SUB ≥  42 2A  * ≥  58 3A  : ≥  74 4A  J11 0B |11  ^K VT  ≥ 27 1B   ^[ ESC ≥  43 2B  + ≥  59 3B  ; ≥  75 4B  K';
	Call WRITE_TABLE;
	Temp_String = '12 0C |12  ^L FF  ≥ 28 1C   ^\ FS  ≥  44 2C  , ≥  60 3C  < ≥  76 4C  L13 0D |13  ^M CR  ≥ 29 1D   ^] GS  ≥  45 2D  - ≥  61 3D  = ≥  77 4D  M14 0E   ^N SO  ≥ 30 1E   ^^ RS  ≥  46 2E  . ≥  62 3E  > ≥  78 4E  N';
	Call WRITE_TABLE;
	Temp_String = '15 0F   ^O SI  ≥ 31 1F   ^_ US  ≥  47 2F  / ≥  63 3F  ? ≥  79 4F  Oƒƒƒƒƒƒƒƒƒƒƒƒƒƒƒƒ¡ƒƒƒƒƒƒƒƒƒƒƒƒƒƒƒƒƒ¡ƒƒƒƒƒƒƒƒƒƒƒ¡ƒƒƒƒƒƒƒƒƒƒƒ¡ƒƒƒƒƒƒƒƒƒƒ';
	Call WRITE_TABLE;

	RM('USERIN^CheckEvents /M=2/G=EVASCII/#=6');
	KEY_LOOP1:
		READ_KEY;
		if(  (KEY1 == 0) & (KEY2 == 250)  ) {
			RM('USERIN^CheckEvents /M=1/G=EVASCII/#=6');
		} else {
			RM('USERIN^CheckEvents /M=0/G=EVASCII/#=6');
		}
		if(  Return_Int != 0  ) {
			Return_Int = Parse_Int('/R=', return_str );
			if(  Return_Int == 1  ) {
				Goto Start_Page3;
			} else if(  Return_Int == 2  ) {
				Goto Start_Page2;
			} else if(  Return_Int == 3  ) {
				Goto Do_End;
			}
		}
		Goto KEY_LOOP1;

START_PAGE2:
	CP = 2;

	RX = UL_ROW + 1;
	CX = UL_COL + 1;



	Temp_String = '80 50  P ≥  96 60  ` ≥ 112 70  p ≥ 128 80  Ä ≥ 144 90  ê ≥ 160 A0  † 81 51  Q ≥  97 61  a ≥ 113 71  q ≥ 129 81  Å ≥ 145 91  ë ≥ 161 A1  ° 82 52  R ≥  98 62  b ≥ 114 72  r ≥ 130 82  Ç ≥ 146 92  í ≥ 162 A2  ¢ ';
	Call WRITE_TABLE;
	Temp_String = '83 53  S ≥  99 63  c ≥ 115 73  s ≥ 131 83  É ≥ 147 93  ì ≥ 163 A3  £ 84 54  T ≥ 100 64  d ≥ 116 74  t ≥ 132 84  Ñ ≥ 148 94  î ≥ 164 A4  § 85 55  U ≥ 101 65  e ≥ 117 75  u ≥ 133 85  Ö ≥ 149 95  ï ≥ 165 A5  • ';
	Call WRITE_TABLE;
	Temp_String = '86 56  V ≥ 102 66  f ≥ 118 76  v ≥ 134 86  Ü ≥ 150 96  ñ ≥ 166 A6  ¶ 87 57  W ≥ 103 67  g ≥ 119 77  w ≥ 135 87  á ≥ 151 97  ó ≥ 167 A7  ß 88 58  X ≥ 104 68  h ≥ 120 78  x ≥ 136 88  à ≥ 152 98  ò ≥ 168 A8  ® ';
	Call WRITE_TABLE;
	Temp_String = '89 59  Y ≥ 105 69  i ≥ 121 79  y ≥ 137 89  â ≥ 153 99  ô ≥ 169 A9  © 90 5A  Z ≥ 106 6A  j ≥ 122 7A  z ≥ 138 8A  ä ≥ 154 9A  ö ≥ 170 AA  ™ 91 5B  [ ≥ 107 6B  k ≥ 123 7B  { ≥ 139 8B  ã ≥ 155 9B  õ ≥ 171 AB  ´ ';
	Call WRITE_TABLE;
	Temp_String = '92 5C  \ ≥ 108 6C  l ≥ 124 7C  || ≥ 140 8C  å ≥ 156 9C  ú ≥ 172 AC  ¨ 93 5D  ] ≥ 109 6D  m ≥ 125 7D  } ≥ 141 8D  ç ≥ 157 9D  ù ≥ 173 AD  ≠ 94 5E  ^ ≥ 110 6E  n ≥ 126 7E  ~ ≥ 142 8E  é ≥ 158 9E  û ≥ 174 AE  Æ ';
	Call WRITE_TABLE;
	Temp_String = '95 5F  _ ≥ 111 6F  o ≥ 127 7F   ≥ 143 8F  è ≥ 159 9F  ü ≥ 175 AF  Ø ƒƒƒƒƒƒƒƒƒ¡ƒƒƒƒƒƒƒƒƒƒƒ¡ƒƒƒƒƒƒƒƒƒƒƒ¡ƒƒƒƒƒƒƒƒƒƒƒ¡ƒƒƒƒƒƒƒƒƒƒƒ¡ƒƒƒƒƒƒƒƒƒƒ ';
	Call WRITE_TABLE;

	RM('USERIN^CheckEvents /M=2/G=EVASCII/#=6');
	KEY_LOOP2:
		READ_KEY;
		if(  (KEY1 == 0) & (KEY2 == 250)  ) {
			RM('USERIN^CheckEvents /M=1/G=EVASCII/#=6');
		} else {
			RM('USERIN^CheckEvents /M=0/G=EVASCII/#=6');
		}
		if(  Return_Int != 0  ) {
			Return_Int = Parse_Int('/R=', return_str );
			if(  Return_Int == 1  ) {
				Goto Start_Page1;
			} else if(  Return_Int == 2  ) {
				Goto Start_Page3;
			} else if(  Return_Int == 3  ) {
				Goto Do_End;
			}
		}
		Goto KEY_LOOP2;

START_PAGE3:
	CP = 3;

	RX = UL_ROW + 1;
	CX = UL_COL + 1;

	Temp_String = '176 B0  ∞ ≥ 192 C0  ¿ ≥ 208 D0  – ≥ 224 E0  ‡ ≥ 240 F0   ≥          177 B1  ± ≥ 193 C1  ¡ ≥ 209 D1  — ≥ 225 E1  · ≥ 241 F1  Ò ≥          178 B2  ≤ ≥ 194 C2  ¬ ≥ 210 D2  “ ≥ 226 E2  ‚ ≥ 242 F2  Ú ≥          ';
	Call WRITE_TABLE;
	Temp_String = '179 B3  ≥ ≥ 195 C3  √ ≥ 211 D3  ” ≥ 227 E3  „ ≥ 243 F3  Û ≥          180 B4  ¥ ≥ 196 C4  ƒ ≥ 212 D4  ‘ ≥ 228 E4  ‰ ≥ 244 F4  Ù ≥          181 B5  µ ≥ 197 C5  ≈ ≥ 213 D5  ’ ≥ 229 E5  Â ≥ 245 F5  ı ≥          ';
	Call WRITE_TABLE;
	Temp_String = '182 B6  ∂ ≥ 198 C6  ∆ ≥ 214 D6  ÷ ≥ 230 E6  Ê ≥ 246 F6  ˆ ≥          183 B7  ∑ ≥ 199 C7  « ≥ 215 D7  ◊ ≥ 231 E7  Á ≥ 247 F7  ˜ ≥          184 B8  ∏ ≥ 200 C8  » ≥ 216 D8  ÿ ≥ 232 E8  Ë ≥ 248 F8  ¯ ≥          ';
	Call WRITE_TABLE;
	Temp_String = '185 B9  π ≥ 201 C9  … ≥ 217 D9  Ÿ ≥ 233 E9  È ≥ 249 F9  ˘ ≥          186 BA  ∫ ≥ 202 CA    ≥ 218 DA  ⁄ ≥ 234 EA  Í ≥ 250 FA  ˙ ≥          187 BB  ª ≥ 203 CB  À ≥ 219 DB  € ≥ 235 EB  Î ≥ 251 FB  ˚ ≥          ';
	Call WRITE_TABLE;
	Temp_String = '188 BC  º ≥ 204 CC  Ã ≥ 220 DC  ‹ ≥ 236 EC  Ï ≥ 252 FC  ¸ ≥          189 BD  Ω ≥ 205 CD  Õ ≥ 221 DD  › ≥ 237 ED  Ì ≥ 253 FD  ˝ ≥          190 BE  æ ≥ 206 CE  Œ ≥ 222 DE  ﬁ ≥ 238 EE  Ó ≥ 254 FE  ˛ ≥          ';
	Call WRITE_TABLE;
	Temp_String = '191 BF  ø ≥ 207 CF  œ ≥ 223 DF  ﬂ ≥ 239 EF  Ô ≥ 255 FF    ≥          ƒƒƒƒƒƒƒƒƒƒ¡ƒƒƒƒƒƒƒƒƒƒƒ¡ƒƒƒƒƒƒƒƒƒƒƒ¡ƒƒƒƒƒƒƒƒƒƒƒ¡ƒƒƒƒƒƒƒƒƒƒƒ¡ƒƒƒƒƒƒƒƒƒƒ';
	Call WRITE_TABLE;

	RM('USERIN^CheckEvents /M=2/G=EVASCII/#=6');
	KEY_LOOP3:
		READ_KEY;
		if(  (KEY1 == 0) & (KEY2 == 250)  ) {
			RM('USERIN^CheckEvents /M=1/G=EVASCII/#=6');
		} else {
			RM('USERIN^CheckEvents /M=0/G=EVASCII/#=6');
		}
		if(  Return_Int != 0  ) {
			Return_Int = Parse_Int('/R=', return_str );
			if(  Return_Int == 1  ) {
				Goto Start_Page2;
			} else if(  Return_Int == 2  ) {
				Goto Start_Page1;
			} else if(  Return_Int == 3  ) {
				Goto Do_End;
			}
		}
		Goto KEY_LOOP3;

WRITE_TABLE:
	CI = 0;
	while(  (CI < 3)  ) {
		WRITE(Copy(Temp_String,(CI * 69) + 1,69),CX,RX,0,M_T_COLOR);
		++ RX;
		++ CI;
	}
	RET;

DO_END:
	RM('USERIN^CheckEvents /M=3/G=EVASCII/#=6');
	Set_Global_Int('ASCII_PAGE',CP);
	KILL_BOX;
}

macro CONDENSE FROM EDIT TRANS {
/*******************************************************************************
															 MULTI-EDIT MACRO

NAME:  CONDENSE

DESCRIPTION:  Displays the current file so that only the lines that start on
							or before a set cutoff point or according to a search expression
							are shown. allows the user to move quickly through the file.

Global Variables:
	Global_Int('Condense_Mode')
		0 = Use cutoff column
		1 = Use search expression

	Global_Str('Condense_Search')
		Contains the last used search expression

	Global_Str('Condense_Switches')
		Contains the list used search switches

	Global_Int('Cutoff_Col')
		Contains the last used cutoff column

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int Cutoff_Col,Up_Count,Temp_Line_Num,New_Line_Num,At_Top,At_Bottom,
					Temp_Integer,Line_Amount,Text_Window,Line_Window,Repaint,
					Left_Side,Width,Top,Write_Line,X2,Y2,Start_Line,Cursor_Line,Counter,
					Normal,Temp_Tab_Expand,Using_Search,Temp_Reg_Exp_Stat,
					Temp_Insert_Mode,Temp_Ignore_Case, twin,Scroll_Pos,T_Mou_X,T_Mou_Y,
					first_time;
	str Temp_String,Search_String,Search_Switches[8],Temp_Search;

/* Variables for special elapsed time code
	Def_Int(Start_Minutes,Start_Seconds,End_Minutes,End_Seconds);
 */

	Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') + 1);
	Temp_Reg_Exp_Stat = Reg_Exp_Stat;
	Temp_Ignore_Case = Ignore_Case;
	Temp_Tab_Expand = Tab_Expand;
	Temp_Insert_Mode = Insert_Mode;
	Tab_Expand = False;
	Undo_Stat = False;
	Insert_Mode = True;
	Using_Search = Global_Int('Condense_Mode');
	Search_String = Global_Str('Condense_Search');
	if(  (Search_String == '')  ) {
		Search_String = Global_Str('Search_Str');
	}
	if(  (Search_String == '')  ) {
		Using_Search = False;
	}
	Temp_Search = Search_String;

	Search_Switches = Global_Str('Condense_Switches');
	if(  (Search_Switches == '')  ) {
		if(  (XPos('I',Caps(Global_Str('Switches')),1))  ) {
			Search_Switches = 'I';
		}
		if(  (XPos('X',Caps(Global_Str('Switches')),1))  ) {
			Search_Switches = Search_Switches + 'X';
		}
	}
	Ignore_Case = (XPos('I',Search_Switches,1) > 0);
	Reg_Exp_Stat = (XPos('X',Search_Switches,1) == 0);
	if(  (Global_Int('Cutoff_Col') < 1)  ) {
		Set_Global_Int('Cutoff_Col',1);
	}
	if(  (Global_Int('Cutoff_Col') > 18)  ) {
		Set_Global_Int('Cutoff_Col',18);
	}
	Cutoff_Col = Global_Int('Cutoff_Col');
	if(  ((Not(Using_Search)) & (Cutoff_Col == 1))  ) {
		Reg_Exp_Stat = True;
		Search_String = '%[~ |9|0]';
	}
	Push_Labels;
	FLABEL('Help',1,$FF);
	Flabel('Column',2,$FF);
	Flabel('Search',3,$FF);
	Messages = False;
	Left_Side = Win_X1 + 2;
	Cursor_Line = Win_Y1 + 1;
	Width = (Win_X2 - Win_X1) - 1;
	Top = Win_Y1 + 1;
	Line_Amount = (Win_Y2 - Win_Y1) - 1;
	X2 = Win_X2;
	Y2 = Win_Y2;
	Text_Window = Cur_Window;
	Temp_Line_Num = C_Line;
	Refresh = False;
	Normal = True;

	Create_Window;
/* make window the same as the text window */
	Window_Attr = $80;
	Size_Window(Left_Side - 2,Top - 1,X2,Y2);
	Line_Window = Cur_Window;
	Switch_Window(Text_Window);
	Repaint = False;
	At_Top = False;
	At_Bottom = False;
	Write(Copy('ÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕÕ',1,Width),Win_X1 + 1,Win_Y1,0,B_Color);
	Write('CONDENSED',(Width / 2) - 5,Win_Y1,0,B_Color);
	Make_Message('');
	Start_Line = Temp_Line_Num;
	Up_Count = Line_Amount / 2;
	first_time = true;
	Call GET_LINES;
	Call GET_LINE_NUM;
	Check_Key;

MAIN_LOOP:
/*
Make_Message('AT_TOP=' + Str(AT_TOP) + ' AT_BOTTOM=' + STR(AT_BOTTOM) +
							' LINE_AMOUT=' + Str(LINE_AMOUNT) + ' C_LINE=' + Str(C_Line));
 */
	Read_Key;
	Insert_Mode = True;
	if(  ((Key1 == 27) | (Key1 == 13))  ) {
		Goto LOOP_EXIT;
	}
	if(  (Key1 == 0)  ) {
		if(  ((Key2 == 244) | (Key2 == 245))  ) {
			Goto LOOP_EXIT;
		}
		Call GET_KEY;
	}
	Goto MAIN_LOOP;

LOOP_EXIT:
	Switch_Window(Line_Window);
	Val(New_Line_Num,Get_Line);
	if(  ((Key1 == 27) | (New_Line_Num == 0) | (Key2 == 245))  ) {
		New_Line_Num = Temp_Line_Num;
	}

	Delete_Window;

	Switch_Window(Text_Window);

/* These 6 lines will center the selected line on the screen if possible */

	Refresh = True;

	while(  (C_Row > ((Win_Y2 - Win_Y1) / 2))  ) {
		Up;
	}
	while(  (C_Row < ((Win_Y2 - Win_Y1) / 2))  ) {
		Down;
	}
	Goto_Line(New_Line_Num);
	goto_col(Cutoff_Col);
	Goto END_OF_MAC;

GET_LINES:
	Switch_Window(Line_Window);
/* special development code for calculating elapsed time
If ((Val(Start_Minutes,Copy(Time,4,2))) and (Val(Start_Seconds,Copy(Time,7,2)))) Then
End;
 */

/* Determine the line to start looking at */
	if(  ((Up_Count > 0) | (Repaint == True))  ) {
		Tof;
	} else {
		Goto_Line(Line_Amount);
	}

	if(  (Get_Line != '')  ) {
		Val(Start_Line,Get_Line);
	} else {
		Start_Line = Temp_Line_Num;
	}
GET_LINES2:
/* Clear displayed lines */
	Temp_Integer = Top + Line_Amount;
	Counter = Top;
	while(  (Counter < Temp_Integer)  ) {
		Draw_Char(32,Left_Side - 1,Counter,Text_Color,Width);
		++Counter;
	}

	Tof;
/* Delete all lines in line number file */
	while(  Not(At_Eof)  ) {
		Del_Line;
	}
/* Find top line */
	Switch_Window(Text_Window);
	if(  (Normal == True)  ) {
		Goto_Line(Start_Line);
	}

	At_Top = False;

	if(  (Up_Count == 0)  ) {
		Goto PAGING_DOWN;
	}

	if(  (Repaint)  ) {
		Repaint = False;
		Switch_Window(Text_Window);
		Goto PAGING_DOWN;
	}

	Counter = Up_Count;
SEEK_UP:
	if ((Using_Search) || (Cutoff_Col == 1)) {
		if (first_time) {
			eol;
			first_time = false;
		}
		if (Search_Bwd(Search_String,0)) {
			Temp_String = Str(C_Line);
			Switch_Window(Line_Window);
			Cr;
			Up;
			Put_Line(Temp_String);
			Switch_Window(Text_Window);
			--Counter;
			if(  (C_Line == 1)  ) {
				Goto TOP_FOUND;
			}
			Up;
			goto_col(1);
			if(  (Counter > 0)  ) {
				Goto SEEK_UP;
			}
		} else {
			if(  (Error_Level)  ) {
				RM('MEERROR');
				Ret;
			}
			At_Top = True;
		}
	} else {
			First_Word;
			if(  ((c_col <= Cutoff_Col) & (Not(At_Eol)))  ) {
				Temp_String = Str(C_Line);
				Switch_Window(Line_Window);
				Cr;
				Up;
				Put_Line(Temp_String);
				Switch_Window(Text_Window);
				--Counter;
			}
			if(  (C_Line == 1)  ) {
				Goto TOP_FOUND;
			}
			Up;
			if(  (Counter > 0)  ) {
				Goto SEEK_UP;
			}
	}

TOP_FOUND:
	At_Top = (C_Line == 1) | (At_Top);
	Down;
	Switch_Window(Line_Window);
	Tof;

	while(  (Up_Count > Counter)  ) {
		Switch_Window(Line_Window);
		Val(Temp_Integer,Get_Line);
		Write_Line = (C_Line + Top) - 1;
		Down;
		Switch_Window(Text_Window);
		Goto_Line(Temp_Integer);
		Write(Copy(Get_Line,1,Width),Left_Side - 1,Write_Line,0,Text_Color);
		--Up_Count;
	}

	Switch_Window(Text_Window);
	Down;
PAGING_DOWN:
	At_Top = (C_Line == 1) | (At_Top);

	At_Bottom = False;
	Switch_Window(Line_Window);
	Write_Line = (C_Line + Top) - 2;
	Counter = C_Line;
	Switch_Window(Text_Window);
	if(  (Counter > Line_Amount)  ) {
		Goto NO_MORE_LINES;
	}

SEEK_DOWN:
	if ((Using_Search) || (Cutoff_Col == 1)) {
		if(  (Search_Fwd(Search_String,0))  ) {
			Temp_Integer = C_Line;
			Switch_Window(Line_Window);
			Put_Line(Str(Temp_Integer));
			Down;
			Switch_Window(Text_Window);
			Write(Copy(Get_Line,1,Width),Left_Side - 1,(Counter + Top) - 1,0,Text_Color);
			Down;
			goto_col(1);
			++Counter;
			if(  (At_Eof)  ) {
				Goto NO_MORE_LINES;
			}
			if(  (Counter <= Line_Amount)  ) {
				Goto SEEK_DOWN;
			}
		} else {
			if(  (Error_Level)  ) {
				RM('MEERROR');
				Ret;
			}
			At_Bottom = True;
		}
	} else {
			First_Word;
			while(  ((c_col > Cutoff_Col) | (At_Eol))  ) {
				Down;
				First_Word;
				if(  (At_Eof)  ) {
					Goto NO_MORE_LINES;
				}
			}

			Temp_Integer = C_Line;
			Switch_Window(Line_Window);
			Put_Line(Str(Temp_Integer));
			Down;
			Switch_Window(Text_Window);
			Write(Copy(Get_Line,1,Width),Left_Side - 1,(Counter + Top) - 1,0,Text_Color);
			Down;
			++Counter;
			if(  (Counter <= Line_Amount)  ) {
				Goto SEEK_DOWN;
			}
	}

NO_MORE_LINES:
	At_Bottom = (At_Eof) | (At_Bottom);
	Switch_Window(Line_Window);
	Tof;
	if(  (Search_FWD(Str(Start_Line),0) == False)  ) {
/* If we weren't on a displayable line when we entered condense, we need to find
the nearest displayable line above it. */
FIND_START_LINE:
		if(  (Val(Counter,Get_Line) == 0)  ) {
			if(  (Counter < Start_Line)  ) {
				Down;
				Goto FIND_START_LINE;
			}
		}
		Up;
	}
	goto_col(1);

	Cursor_Line = C_Line + Win_Y1;
	GotoXY(Left_Side - 1,Cursor_Line);
/* special development code to determine elapsed time
If ((Val(End_Minutes,Copy(Time,4,2))) and (Val(End_Seconds,Copy(Time,7,2)))) Then
End;
If (Start_Seconds > End_Seconds) Then
	End_Seconds := End_Seconds + 60;
	--End_Minutes;
End;
If (Start_Minutes > End_Minutes) Then
	End_Minutes := End_Minutes + 60;
End;
Make_Message('Old Elapsed time ' + Str(End_Minutes - Start_Minutes) + ':' + Str(End_Seconds - Start_Seconds));
Read_Key;
 */
	RET;

GET_KEY:
	if(  (Key2 == 251)  ) {
		if(  (Mou_Last_X == Win_X2) & (Mou_Last_y > (win_y1 + 1)) &
			(Mou_Last_y < (win_y2 - 1))  ) {
			Switch_Window(Text_Window);
			RM('MOUSE^GotoScrollBar');
			Start_Line = C_Line;
			Switch_Window(Line_Window);
			Erase_Window;
			Up_Count = Line_Amount / 2;
			Call GET_LINES2;
			Call GET_LINE_NUM;
			Ret;
		}
	}
	if(  (Key2 == 250)  ) {
		T_Mou_X = Mou_Last_X;
		T_Mou_Y = Mou_Last_Y;
		if(  (Mou_Last_X == Win_X2)  ) {
			if(  (mou_last_y == (win_y2 - 1))  ) {
				Goto SCROLL_UP;
			} else if(  (mou_last_y == (win_y1 + 1))  ) {
				Goto SCROLL_DOWN;
			} else if(  (Mou_Last_y > win_y1) & (Mou_Last_y < win_y2)  ) {
				Temp_Integer = Mou_Last_Y - Win_Y1 - 1;
				Switch_Window(Text_WIndow);
				Scroll_Pos = Cur_Scroll_Pos;
				Switch_Window(Line_WIndow);
				if(  (Temp_Integer > scroll_pos)  ) {
					Goto page_down;
				} else if(  (Temp_Integer < scroll_pos)  ) {
					Goto page_up;
				}
			}
		} else if(  (Mou_Last_Y == Fkey_ROW)  ) {
			RM('MOUSE^MOUSEFKEY');
			Ret;
		} else if(  ((Mou_Last_Y > Win_Y1) & (Mou_Last_Y < Win_Y2) &
					(Mou_Last_X > Win_X1) & (Mou_Last_X < Win_X2))  ) {
/* Check to see if mouse was clicked inside the window */
			Counter = C_Line;
			Goto_Line(Mou_Last_Y - Win_Y1);
			if(  (C_Line == Counter)  ) {
				Push_Key(13,28);
				Ret;
			}
			if(  (At_Eof)  ) {
				Goto_Line(Counter);
			}
			Cursor_Line = C_Line + Win_Y1;
			GotoXY(Left_Side - 1,Cursor_Line);
		} else {
			RM('USERIN^CHECKEVENTS /M=1/G=CONDENSEEV/#=3');
			if(  (Return_Int)  ) {
				Push_Key(Parse_Int('/K1=',Global_Str('CONDENSEEV' + Str(Return_Int))),
								 Parse_Int('/K2=',Global_Str('CONDENSEEV' + Str(Return_Int))));
				Ret;
			}
		}
	}
	if(  ((Key2 == 72) | (Key2 == 240))  ) { /* Up arrow */
		if(  (C_line > 1)  ) {
			Up;
			Eol;
			--Cursor_Line;
			GotoXY(Left_Side - 1,Cursor_Line);
		} else {
SCROLL_DOWN:
			if(  Not(At_Top)  ) { /* Scroll down */
				Counter = C_Line;
/* Check for at bottom */
				Switch_Window(Line_Window);
				Goto_Line(Line_Amount);
				Goto_Col(1);
				At_Bottom = At_Eof;
				Del_Line;
/* Goto the top line */
				Tof;
				Val(Start_Line,Get_Line);
/* Look for the next line up in the text file */
				Switch_Window(Text_Window);
				Goto_Line(Start_Line);
				Up;
				Eol;
				if(  ((Using_Search) | (Cutoff_Col == 1))  ) {
					if(  (Search_Bwd(Search_String,0))  ) {
						Temp_Integer = C_Line;
						Scroll_Box_Dn(Left_Side - 1,Top,X2 - 1,Y2 - 1,(Back_Color << 4) | Text_Color);
						Write(Copy(Get_Line,1,Width),Left_Side - 1,Top,0,Text_Color);
						At_Top = (C_Line == 1);
						Switch_Window(Line_Window);
/* Delete bottom line */
						Goto_Line(Line_Amount);
						Del_Line;
/* Goto the top line and insert new line above it */
						Tof;
						Cr;
						Up;
						Put_Line(Str(Temp_Integer));
					} else {
						At_Top = True;
					}
				} else {
						First_Word;
						while(  ((c_col > Cutoff_Col) | (At_Eol))  ) {
							Up;
							First_Word;
							if(  (C_Line == 1)  ) {
								At_Top = True;
								Goto No_Scroll_Down;
							}
						}
						At_Top = (C_Line == 1);
						Temp_Integer = C_Line;
						Scroll_Box_Dn(Left_Side - 1,Top,X2 - 1,Y2 - 1,(Back_Color << 4) | Text_Color);
						Write(Copy(Get_Line,1,Width),Left_Side - 1,Top,0,Text_Color);
						Switch_Window(Line_Window);
/* Delete bottom line */
						Goto_Line(Line_Amount);
						Del_Line;
/* Goto the top line and insert new line above it */
						Tof;
						Cr;
						Up;
						Put_Line(Str(Temp_Integer));
				}
NO_SCROLL_DOWN:
				Switch_Window(Line_Window);
				if(  (Key2 == 250)  ) {
					Goto_Line(Counter);
					Mou_Repeat = True;
CHECK_STAT2:
					Mou_Check_Status;
					if(  ((Mou_Last_Status & 1) == 0) | (T_Mou_x != Mou_Last_X) |
							(T_Mou_y != Mou_Last_Y)  ) {
						GOTO MOU_SCROLL_DOWN_EXIT;
					}
					if(  check_key  ) {
						if(  (key1 != 0) & (key2 != 250)  ) {
							push_key(key1, key2);
							GOTO MOU_SCROLL_DOWN_EXIT;
						}
						Call GET_LINE_NUM;
						goto scroll_DOWN;
					}
					goto CHECK_STAT2;
MOU_SCROLL_DOWN_EXIT:
					Mou_Repeat = False;
				}
			}
		}
	}

	if(  ((Key2 == 80) | (Key2 == 241))  ) { /* Down arrow */
		if(  ((C_line < Line_Amount) & (Not(At_Eof)))  ) {
			Down;
			Eol;
			++Cursor_Line;
			GotoXY(Left_Side - 1,Cursor_Line);
		} else {
SCROLL_UP:
			if(  Not(At_Bottom)  ) { /* Scroll up */
				Counter = C_Line;
/* Goto the bottom line */
				Goto_Line(Line_Amount);
				Val(Start_Line,Get_Line);
				Switch_Window(Text_Window);
				Goto_Line(Start_Line);
				Eol;
				if(  (At_Eof)  ) {
					At_Bottom = True;
					Goto NO_SCROLL_UP;
				}

/* Delete top line in line window */
				At_Top = False;
				Switch_Window(Line_Window);
				Tof;
				Del_Line;
				Goto_Line(Line_Amount);
				Scroll_Box_UP(Left_Side - 1,Top,X2 - 1,Y2 - 1,(Back_Color << 4) | Text_Color);

/* Look for the next line down in the text file */
				Switch_Window(Text_Window);
				if(  ((Using_Search) | (Cutoff_Col == 1))  ) {
					if(  (Search_Fwd(Search_String,0))  ) {
						Temp_Integer = C_Line;
						Switch_Window(Text_Window);
						Write(Copy(Get_Line,1,Width),Left_Side - 1,Top + Line_Amount - 1,0,Text_Color);
						Eol;
						At_Bottom = At_Eof;
						Switch_Window(Line_Window);
						Put_Line(Str(Temp_Integer));
					} else {
						At_Bottom = True;
					}
				} else {
					Down;
					First_Word;
					while(  ((c_col > Cutoff_Col) | (At_Eol))  ) {
						Down;
						First_Word;
						if(  (At_Eof)  ) {
							At_Bottom = True;
							Goto No_Scroll_Up;
						}
					}
					Temp_Integer = C_Line;
					Write(Copy(Get_Line,1,Width),Left_Side - 1,Top + Line_Amount - 1,0,Text_Color);
					At_Bottom = At_Eof;
					Switch_Window(Line_Window);
					Put_Line(Str(Temp_Integer));
				}
NO_SCROLL_UP:
				Switch_Window(Line_Window);
				if(  (Key2 == 250)  ) {
					Goto_Line(Counter);
					Mou_Repeat = True;
CHECK_STAT1:
					Mou_Check_Status;
					if(  ((Mou_Last_Status & 1) == 0) | (T_Mou_x != Mou_Last_X) |
							(T_Mou_y != Mou_Last_Y)  ) {
						GOTO MOU_SCROLL_UP_EXIT;
					}
					if(  check_key  ) {
						if(  (key1 != 0) & (key2 != 250)  ) {
							push_key(key1, key2);
							GOTO MOU_SCROLL_UP_EXIT;
						}
						Call GET_LINE_NUM;
						goto scroll_up;
					}
					goto CHECK_STAT1;
MOU_SCROLL_UP_EXIT:
					Mou_Repeat = False;
				}
			}
		}
	}
	if(  (Key2 == 73)  ) {
PAGE_UP:
		if(  (At_Top)  ) { /* Page up */
			Tof;
			Cursor_Line = Win_Y1 + 1;
			GotoXY(Left_Side - 1,Cursor_Line);
		} else {
			Up_Count = Line_Amount;
			Call GET_LINES;
		}
	}
	if(  (Key2 == 81)  ) {
PAGE_DOWN:
		Goto_Col(1);
		if(  (At_Bottom)  ) { /* Page down */
			while(  Not(AT_EOF)  ) {
				Down;
				++Cursor_Line;
			}
			Up;
			--Cursor_Line;
			GotoXY(Left_Side - 1,Cursor_Line);
		} else {
			Up_Count = 0;
			Call GET_LINES;
		}
	}
	if(  ((Key2 == 119) & (Not(At_Top)))  ) { /* Top of file */
		Switch_Window(Text_Window);
		Tof;
		Up_Count = 0;
		Normal = False;
		Call GET_LINES;
		Normal = True;
		At_Top = True;
	}
	if(  ((Key2 == 117) & (Not(At_Bottom)))  ) { /* Bottom_Of_File */
		Switch_Window(Text_Window);
		Eof;
		goto_col(1);
		while(  (At_Eol)  ) {
			Up;
		}
		Up_Count = Line_Amount;
		Normal = False;
		Call GET_LINES;
		Goto_Line(Line_Amount);
		Cursor_Line = C_Line + Win_Y1;
		GotoXY(Left_Side - 1,Cursor_Line);
		Normal = True;
		At_Bottom = True;
	}

	if(  (Key2 == 59)  ) { /* F1(Help) */
	 HELP('CN');
	}
	if(  (Key2 == 60)  ) { /* F2(set cutoff column) */
		Make_Message('');
		Return_Int = Cutoff_Col;
		RM('userin^QUERYBOX /N=1/P=Display lines with text beginning at or before' +
							' column:/MIN=1/MAX=254/H=CC/C=1/L=2/W=2/T=SET CUTOFF COLUMN');
		if(  ((Return_Str == 'FALSE') & (Using_Search == True))  ) {
/* If they pressed <ESC> and we are not already using cutoff column, then go back
to using search */
			Goto DO_SEARCH;
		}
		Set_Global_Int('Cutoff_Col',Return_Int);
		Cutoff_Col = Return_Int;
DO_CUTOFF:
		Repaint = True;
		At_Top = False;
		At_Bottom = False;
		if(  (Cutoff_Col == 1)  ) {
			Reg_Exp_Stat = True;
			Search_String = '%[~ |9|0]';
		}
		Using_Search = False;
		Set_Global_Int('Condense_Mode',False);
		Call GET_LINES;
		Repaint = False;
	}

	if(  (Key2 == 61)  ) { /* F3(set search string) */
		Make_Message('');
		Search_String = Temp_Search;
		Set_Global_Str('CMISTR_1', Search_String );
		Set_Global_Str('CMIPARM_1', '/QK=1/T=Search for:/C=1/L=1/W=64' );
		Set_Global_Str('CMIPARM_2','/TP=10');

		Set_Global_Int('CMIINT_3' , Xpos('I',Search_Switches,1) == 0);
		Set_Global_Str('CMIPARM_3','/QK=1/TP=13/T=Case sensitive      /C=1/W=1/L=3');

		Set_Global_Int('CMIINT_4' , Xpos('X',Search_Switches,1) == 0);
		Set_Global_Str('CMIPARM_4','/TP=13/QK=9/T=Regular expressions /C=1/W=1/L=4');

		RM( 'USERIN^Data_In /PRE=CM/H=CC/#=4/A=0/S=1/X=1/Y=3/T=DISPLAY LINES CONTAINING SEARCH STRING' );
		if(  ((Return_Int == 0) & (Using_Search == False))  ) {
/* If they pressed <ESC> and we are not already using search, then go back to
using cutoff column */
			Goto DO_CUTOFF;
		}
		Search_String = Global_Str('CMISTR_1');
		Search_Switches = '';
		if(  (Global_Int('CMIINT_3') == 0)  ) {
			Search_Switches = Search_Switches + 'I';
		}
		if(  (Global_Int('CMIINT_4') == 0)  ) {
			Search_Switches = Search_Switches + 'X';
		}
/*
		Search_Switches := Caps(Global_Str('CMISTR_3'));
 */
		Ignore_Case = (XPos('I',Search_Switches,1) > 0);
		Reg_Exp_Stat = (XPos('X',Search_Switches,1) == 0);
		Temp_Search = Search_String;
DO_SEARCH:
		Repaint = True;
		At_Top = False;
		At_Bottom = False;
		Using_Search = True;
		Set_Global_Int('Condense_Mode',True);
		Set_Global_Str('Condense_Search',Search_String);
		Set_Global_Str('Condense_Switches',"-" + Search_Switches);
		Kill_Box;
		Call GET_LINES;
		Repaint = False;
	}
	Call GET_LINE_NUM;
	RET;

GET_LINE_NUM:
	if(  (Val(New_Line_Num,Get_Line))  ) {
		New_Line_Num = 0;
	}
	Put_Line_Num(New_Line_Num);

	Set_Global_Str('CONDENSEEV1','/T=Select/KC=<ENTER>/W=18/K1=13/K2=28/R=13/Y=' +
			Str(Message_Row) + '/X=1');
	Set_Global_Str('CONDENSEEV2','/T=Cancel/KC=<ESC>/W=11/K1=27/K2=1/R=13/Y=' +
			Str(Message_Row) + '/X=15');
	if(  (Using_Search)  ) {
		Set_Global_Str('CONDENSEEV3','/T=Displaying by search string./KC=/W=28/K1=0/K2=61/R=61/Y=' +
				Str(Message_Row) + '/X=28');
	} else {
		Set_Global_Str('CONDENSEEV3','/T=Displaying by cutoff column./KC=/W=28/K1=0/K2=60/R=60/Y=' +
				Str(Message_Row) + '/X=28');
	}

	RM('USERIN^CHECKEVENTS /M=2/G=CONDENSEEV/#=3');
	switch_window( text_window );
	while(  c_row > 1  ) {
		up;
	}
	goto_line( new_line_num );
	update_scroll_bar;
	switch_window( line_window );
	RET;

PROCESS_MOUSE:
	RET;

END_OF_MAC:
	RM('USERIN^CHECKEVENTS /M=3/G=CONDENSEEV/#=3');
	Tab_Expand = Temp_Tab_Expand;
	Reg_Exp_Stat = Temp_Reg_Exp_Stat;

	Insert_Mode = Temp_Insert_Mode;
	Ignore_Case = Temp_Ignore_Case;
	Undo_Stat = True;
	Kill_Box;
	Refresh = True;
	Make_Message('');
	Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') - 1);
	Pop_Labels;
	Messages = True;
	New_Screen;

}

macro SQUEEZE TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:  SQUEEZE

Description:  Converts all leading space characters to the appropriate # of
							tab characters.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  jx ;
	str  tstr[2048];

	tof;
	refresh = false;
	undo_stat = false;  /* no undo, to improve speed */
	while(  NOT( At_EOF )  ) {
		first_word;
		jx = c_col;
		tstr = shorten_str(Get_Word(''));
		Put_Line('');
		goto_col( jx );
		Text( tstr );
		down;
		Put_Line_Num( c_line );
	}
	refresh = true;
	tof;
	undo_stat = true;
}
