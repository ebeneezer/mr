macro_file PASCAL;
/*******************************************************************************
														MULTI-EDIT MACRO FILE

Name: PASCAL

Description:	Language support for Pascal

PASMTCH - Construct matching
PAS_IND - Smart indent
PASTEMP - Template editing (old style)
PASSETX - Sets up the template expansion data global string.

							 (C) Copyright 1991 by American Cybernetics, Inc.
***************************************************************************** **/

macro PASMTCH TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: PASMTCH

Description:  This macro will match occurances of BEGIN/END, (), CASE/END
	and handles problems with statements embedded in quotes or comments.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str  Str1, Str2, Str3,     /* Match strings */
					 T_Str,S_str, FStr ;

	int  Direction,   /* 1 = search forward, 0 = backward */
					 B_Count,     /* Match count.  0 = match found */
					 S_Res,       /* Search result */
					 Second_Time,
           tstream_block = stream_block_mode, // Save stream_block_mode
           oldrefresh = refresh,
					 shift_stat = peek( 0, 0x417 ),
					 T_Row, T_Col, T_Line, /* Holds the original position */
					 JX,            /* General purpos */
           F_Line, F_Col; /* Found position */

	T_Line = C_Line;      /* Store the current position */
	T_Col = C_Col;

  Push_Undo;
	Mark_Pos;

	Second_Time = False;
	Refresh = False;     /* Turn screen refresh off */
  stream_block_mode = 1;  // Make stream blocks inclusive
	Str3 = '';           /* Some matchs only require 2 match strings so init the 3rd */

Find_Match_Str:

	if(  (Cur_Char == '(')  ) {   /* Setup match for '(' */
		Direction = 1;
		right;
		if (cur_char == "*") {  // This is for matching (* comments
			str1 = "(*";
			str2 = "*)";
			s_str =  "{(@*}|{@*)}";
			goto START_MATCH;
		}
		left;
		Str1 = '(';
		Str2 = ')';
		S_Str = Str1+'||'+Str2+'||[''{}]||{(@*}||{@*)}';
		GOTO Start_Match;
	}

	if(  (Cur_Char == ')')  ) {   /* Setup match for ')' */
		Direction = 0;
		if (c_col > 1) {
			left;
			if (cur_char == "*") {  // This is for matching *) comments
				str1 = "*)";
				str2 = "(*";
				s_str =  "{(@*}|{@*)}";
				goto START_MATCH;
			}
			right;
		}
		Str1 = ')';
		Str2 = '(';
		S_Str = Str1+'||'+Str2+'||[''{}]||{(@*}||{@*)}';
		GOTO Start_Match;
	}


	if(  At_EOL  ) {     /* If we are at the end of a line the go to the first word */
		First_Word;
	}

	if(  (Cur_Char == ' ') |
		 (Cur_Char == '|9') |
		 (Cur_Char == '|255')  ) {      /* If we are on a blank space then find a word */
		Word_Right;
	}

	T_Str = Caps( Get_Word(';. |9|255') );  /* Get the current word */

	if(  (T_Str == 'BEGIN') | (T_Str == 'CASE')  ) {
		Str1 = 'BEGIN';
		Str2 = 'END';
		Str3 = 'CASE';
		Direction = 1;
		S_Str = '{%||[|9 ;}]{'+Str1+'}||{'+Str2+'}||{'+Str3+'}$||[ |9;.{]}||[''{}]||{(@*}||{@*)}';
		GOTO Start_Match;
	}

	if(  T_Str == 'END'  ) {
		Str1 = 'END';
		Str2 = 'BEGIN';
		Str3 = 'CASE';
		Direction = 0;
		Word_Left;
		Left;
		S_Str = '{%||[|9 ;}]{'+Str1+'}||{'+Str2+'}||{'+Str3+'}$||[|9 ;.{]}||[''{}]||{(@*}||{@*)}';
		GOTO Start_Match;
	}

		/* If we didn''t find a word to match the first time then try again */
	if(  NOT( Second_Time )  ) {
		Second_Time = True;
		First_Word;
		GOTO Find_Match_Str;
	}

	Make_Message('NOTHING to Match');
	GOTO Macro_Exit;

Start_Match:
	Reg_Exp_Stat = True;
	Ignore_Case = True;
	B_Count = 1;
	S_Res = 1;
	Make_Message('Matching...  Press <ESC> to Stop.');
	Working;

MATCH_LOOP:   /* Main loop */

					/* If the <ESC> key is pressed while matching then abort the search */
	if(  check_key  ) {
		if(  key1 == 27  ) {
			Make_Message('Match Aborted.');
			goto macro_exit;
		}
	}

	if(  S_Res == 0  ) {   /* If last search result was false then exit */
		GOTO Error_Exit;
	}

	if(  B_Count == 0  ) { /* If match count is 0 then success */
		GOTO Found_Exit;
	}

	if(  Direction == 1  ) { /* Perform search based on direction */
		Right;
		while(  NOT( At_EOL) & (Cur_CHar == '|255')  ) {
			Right;
		}
		S_Res = Search_Fwd(S_Str,0);
	} else {
		Left;
		while(  (Cur_CHar == '|255') |
					(Cur_Char == '|9')  ) {
			Left;
		}
		S_Res = Search_Bwd(S_Str,0);
	}

	if(  S_Res == 0  ) {   /* If search failed then exit */
		GOTO Error_Exit;
	}


	FStr = Caps(Found_Str); /* Get the found string and capatilize it */
	if(  XPOS(Copy(FStr,1,1),'|9 ;',1)   ) {  /* If the first char is a space or a ; */
		FStr = Copy(FStr,2,20);         /*   then eliminate it */
	}
																		 /* If it ended in a space, ; or . then */
	if(  XPOS(Copy(FStr,Length(FStr),1),'|9 ;.',1)  ) {
		FStr = Copy(FStr,1,Length(FStr) - 1);  /* eliminate that char */
	}
 /* 	Make_Message(FStr);
	read_key;  */

															/* If we found the first match string then */
	if(  FStr == STR1  ) {
		B_Count = B_Count + 1;   /* Inc the match count */
		GOTO Match_Loop;
	}

	if(  FStr == STR2  ) {          /* If we found the second match string then */
		B_Count = B_Count - 1;    /*   decrement the match count */
		GOTO Match_Loop;
	}

	if(  FStr == ''''''  ) {        /* If we found two single quotes the skip it */
		if(  Direction == 1  ) {
			RIGHT;
		} else {
			LEFT;
		}
		GOTO Match_Loop;
	}

															/* If we found a single quote then match it */
	if(  FStr == ''''  ) {

		Quote_Loop:

			if(  Direction == 1  ) {
				RIGHT;
			} else {
				LEFT;
			}
			if(  Direction == 1  ) {
				S_Res = Search_Fwd('''',0);
			} else {
				S_Res = Search_Bwd('''',0);
			}
			if(  S_Res == 0  ) {
				GOTO Macro_Exit;
			}
			FStr = Found_Str;
			if(  FStr == ''''''  ) {
				GOTO Quote_Loop;
			}
			GOTO Match_Loop;

	}

														/* If we found a comment then match it */
	if(  (Direction == 1) & (FStr == '{')  ) {
			S_Res = Search_Fwd('@}',0);
			GOTO Match_Loop;
	}
														/* If we found a comment then match it */
	if(  (Direction == 0) & (FStr == '}')  ) {
			S_Res = Search_Bwd('@{',0);
			GOTO Match_Loop;
	}
														/* If we found a comment then match it */
	if(  (Direction == 1) & (FStr == '(*')  ) {
			S_Res = Search_Fwd('@*)',0);
			GOTO Match_Loop;
	}
														/* If we found a comment then match it */
	if(  (Direction == 0) & (FStr == '*)')  ) {
			S_Res = Search_Bwd('(@*',0);
			GOTO Match_Loop;
	}
														/* If we found the third string then */
														/*   if forward search then */
	if(  (Direction == 0) & (FStr == Str3)  ) {
		B_Count = B_Count - 1; /*   decrement the match count */
		GOTO Match_Loop;
	}                      /*   if backward search then */
	if(  (Direction == 1) & (FStr == Str3)  ) {
		B_Count = B_Count + 1; /*   increment the match count */
		GOTO Match_Loop;
	}

Error_Exit:     /* Go here for unsucessfull match */
	goto_mark;
	Make_Message('Match NOT Found');
	GOTO Macro_Exit;

Found_Exit:       /* We go here if a match was found */

	if (str1 == "(*")
		right;

	F_Line = C_Line;  F_Col = C_Col;

	if(  C_Line > T_Line  ) {
		JX = C_Line - T_Line;
	} else {
		JX = T_Line - C_Line;
	}

	goto_mark;
	mark_pos;
	int tbl1 = block_line1,
			tbl2 = block_line2,
			tbc1 = block_col1,
			tbc2 = block_col2,
			tblx = block_linex,
			tbcx = block_colx,
			tbs = block_stat,
			tm = Marking,
			highlight_block = parse_int('/HI=', mparm_str);
	if(  jx < Screen_Length  ) {
		if( highlight_block ) {
			block_off;
			str_block_begin;
		}
		while(   jx > 0  ) {
			--jx;
			if(  f_line > t_line  ) {
				down;
			} else {
				up;
			}
		}
	}
	else
		highlight_block = false;

	goto_line( f_line );
	goto_col( f_col );
	Make_Message('Match Found.');
	if( highlight_block ) {
		int t_pb = persistent_blocks;
		persistent_blocks = TRUE;
		block_end;
		if((f_line > t_line) || ((f_line == t_line) && (f_col >= t_col)) )
			block_col2 = block_col2 + 1;
		if( parse_int('/RC=', mparm_str) ) {
			goto_mark;
		}
		else
				pop_mark;
		refresh = true;
		redraw;
		while (shift_stat == peek( 0, 0x417 )
			)
		{
			if ( check_key )
			{
				shift_stat = -1;
				push_key(key1, key2);
			}
		}
		block_off;
		block_line1 = tbl1;
		block_line2 = tbl2;
		block_col1 = tbc1;
		block_col2 = tbc2;
		block_linex = tblx;
		block_colx = tbcx;
		block_stat = tbs;
		Marking = tm;
		goto_line( c_line );
		goto_col( c_col );
		persistent_blocks = t_pb;
		refresh = TRUE;
		redraw;
	} else {
		if( parse_int('/RC=', mparm_str) ) {
			goto_mark;
		}
		else
				pop_mark;
	}
Macro_Exit:
  stream_block_mode = tstream_block;  // restore stream block mode
	Refresh = OldRefresh;
	Redraw;
	Pop_Undo;
}

macro PAS_IND {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: PAS_IND

Description:  This macro will perform a smart indent when the <ENTER> key is
	pressed.  This macro is called by the macro CR.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str C_STR;          /* Word to check for indent */
	int T_COL,T_COL2;   /* Temp column positions */
	int sig_char_found,ind_count,jx,oldrefresh = refresh;
	char found_char;
	Messages = False;

	MARK_POS;
	Reg_Exp_Stat = True;
	Down;
	Refresh = False;
	Up;
	LEFT;
	/* Check to see if we are inside a comment */
	/* Don''t go back farther than 5 lines in order to improve speed */

	if(  Search_Bwd('@{||@}||{(@*}||{@*)}',5)  ) {
		if(  (Cur_Char == '{') | (Cur_Char == '(')  ) {
			if(  (Cur_Char == '{')  ) {
				RIGHT;
			} else {
				RIGHT;
				RIGHT;
			}
			Set_Indent_Level;
			GOTO_MARK;
		/* 	Refresh := True;  */
			CR;
			GOTO MAC_EXIT;
		}
	}

	GOTO_MARK;

	MARK_POS;

	CALL SKIP_PAS_NOISE1;
	FOUND_CHAR = CUR_CHAR;
	GOTO_MARK;
 /* REFRESH := TRUE; */

	T_COL2 = C_COL;         /* Store current position */
	FIRST_WORD;              /* Go to the first word on the line */
	T_COL = C_COL;          /* Store this position */

	if(  T_COL2 < T_COL  ) {   /* If this position is greater than the original */
		T_COL = T_COL2;       /*   then store the original */
		GOTO_COL(T_COL);       /*   and go there */
	}
	if(  At_Eol == False  ) { /* If we are beyond the end of the line then */
		SET_INDENT_LEVEL;      /*   set the indent level */
	}

	T_COL = C_COL;          /* Store the current position */
													 /* Get the current word, removing any extra space */
	C_STR = ' ' + REMOVE_SPACE(CAPS( GET_WORD('; (,{') )) + ' ';
	GOTO_COL(T_COL2);        /* Put cursor on original position */
	CR;                      /* Perform a carriage return */

													 /* If the word is in this list, and the original
															position was not on the first word then
															indent */
	if(  (T_COL != T_COL2) & (LENGTH(C_STR) != 0) &
		(POS(C_STR,
	 ' PROCEDURE FUNCTION BEGIN '
	 ) != 0)  ) {
			INDENT;
	} else {
		if(  (Found_Char != ';') & (T_COL != T_COL2) & (LENGTH(C_STR) != 0)
			& (POS(C_STR,
		' VAR TYPE CONST PROCEDURE FUNCTION BEGIN IF WHILE REPEAT WITH FOR ELSE '
		) != 0)  ) {
			INDENT;
		} else {
	/***********************************************************************/
	/****>>> IF YOU DON''T WANT AN UNDENT AFTER 'END' THEN COMMENT OUT THE   */
	/****>>> FOLLOWING THREE LINES                                          */
			if(  (C_STR == ' END ')  ) {
				UNDENT;
			}
		}
	}
	GOTO MAC_EXIT;

SKIP_PAS_NOISE1:

/*  Here we look for the nearest preceding nonblank character.  If it is a
	closing comment then we find the  nearest opening comment.
 */

	if(  (SEARCH_BWD('[~ |9]', 1))  ) {
		if(  (CUR_CHAR == ')')  ) {
			LEFT;
			if(  (CUR_CHAR == '*')  ) {
				JX = SEARCH_BWD('(@*', 0);
				LEFT;
				GOTO SKIP_PAS_NOISE1;
			}
			RIGHT;
			SIG_CHAR_FOUND = TRUE;
			GOTO EXIT_SKIP_PAS;
		} else {
			if(  (CUR_CHAR == '}')  ) {
				JX = SEARCH_BWD('@{', 0);
				LEFT;
				GOTO SKIP_PAS_NOISE1;
			}
		}

		SIG_CHAR_FOUND = TRUE;
		GOTO EXIT_SKIP_PAS;
	}

/*  If we failed to find a nonblank character on the current line, and the
	cursor is on line 1, we failed to find a significant character; otherwise,
	we back up a line and try again.  */

	if(  (C_LINE == 1)  ) {
		SIG_CHAR_FOUND = FALSE;
		GOTO EXIT_SKIP_PAS;
	}
	UP;
	EOL;
	GOTO SKIP_PAS_NOISE1;

EXIT_SKIP_PAS:
 /* REFRESH := TRUE; */
	RET;

MAC_EXIT:
	REFRESH = oldrefresh;
	Messages = True;
}

macro PASTEMP TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: PASTEMP

Description: Creates pascal language constructs based on a single character
	to the left of the current cursor position.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int Temp_Col,Temp_Insert,Choice, UC;
	str XStr;
	Temp_Insert = Insert_Mode;
	if(  (At_Eol == False)  ) {
		GOTO END_OF_MAC;
	}

	Insert_Mode = True;
	Temp_Col = C_COL;

	Left;

	if(  (C_Col > 1)  ) {
		Left;
		if(  (Pos(Cur_Char,' ;})Ff|255|9') == 0)  ) {
			Goto_Col(Temp_Col);
			Goto END_OF_MAC;
		} else {
			Right;
		}
	}

	UC = true;

	if(  (Cur_Char == 'B')  ) {
		GOTO MAKEBEGIN;
	}

	if(  (Cur_Char == 'I')  ) {
		GOTO MAKEIF;
	}

	if(  (Cur_Char == 'W')  ) {
		GOTO MAKEWHILE;
	}

	if(  (Cur_Char == 'R')  ) {
		GOTO MAKEREPEAT;
	}

	if(  (Cur_Char == 'O')  ) {
		Del_Char;
		Temp_Col = Temp_Col - 1;
		GOTO MAKEFOR;
	}

	if(  (Cur_Char == 'U')  ) {
		Temp_Col = Temp_Col - 1;
		Del_Char;
		GOTO MAKEFUNCTION;
	}

	if(  (Cur_Char == 'F')  ) {

		RM('userin^xmenu /L=Select:/B=1/T=1/X=' + str(wherex) + '/Y=' + str(wherey - 2) +
				'/M=for-Next do(TE) Function()');
		choice = return_int;
		Make_Message('');
		if(  (Choice == 2)  ) {
			GOTO MAKEFUNCTION;
		} else {
			GOTO MAKEFOR;
		}
	}

	if(  (Cur_Char == 'P')  ) {
		GOTO MAKEPROCEDURE;
	}

	if(  (Cur_Char == 'C')  ) {
		GOTO MAKECASE;
	}


	UC = false;
	if(  (Cur_Char == 'b')  ) {
		GOTO MAKEBEGIN;
	}

	if(  (Cur_Char == 'i')  ) {
		GOTO MAKEIF;
	}

	if(  (Cur_Char == 'w')  ) {
		GOTO MAKEWHILE;
	}

	if(  (Cur_Char == 'r')  ) {
		GOTO MAKEREPEAT;
	}

	if(  (Cur_Char == 'o')  ) {
		Del_Char;
		Temp_Col = Temp_Col - 1;
		GOTO MAKEFOR;
	}

	if(  (Cur_Char == 'u')  ) {
		Temp_Col = Temp_Col - 1;
		Del_Char;
		GOTO MAKEFUNCTION;
	}

	if(  (Cur_Char == 'f')  ) {

		RM('userin^xmenu /L=Select:/B=1/T=1/X=' + str(wherex) + '/Y=' + str(wherey - 2) +
				'/M=for-Next do(TE) Function()');
		Choice =  return_int;

		make_message('');
		if(  (Choice == 2)  ) {
			GOTO MAKEFUNCTION;
		} else {
			GOTO MAKEFOR;
		}
	}

	if(  (Cur_Char == 'p')  ) {
		GOTO MAKEPROCEDURE;
	}

	if(  (Cur_Char == 'c')  ) {
		GOTO MAKECASE;
	}

	Goto_Col(Temp_Col);
	GOTO END_OF_MAC;

MAKEIF:
	Goto_Col(Temp_Col);
	XStr = 'f () then';
	CALL XTEXT;
	Cr;
	Goto_Col(Temp_Col);
	Indent;
	Up;
	Goto_Col(Temp_Col + 3);
	GOTO END_OF_MAC;

MAKEWHILE:
	Goto_Col(Temp_Col);
	XSTR = 'hile () Do';
	CALL XTEXT;
	Cr;
	Goto_Col(Temp_Col);
	Indent;
	Up;
	Goto_Col(Temp_Col + 6);
	GOTO END_OF_MAC;

MAKEBEGIN:
	Goto_Col(Temp_Col);
	XStr = 'egin';
	CALL XTEXT;
	First_Word;
	Temp_Col = C_Col;
	Eol;
	Cr;
	Cr;
	Goto_Col(Temp_Col);
	XStr = 'end;';
	CALL XTEXT;
	Up;
	Goto_Col(Temp_Col);
	Indent;
	GOTO END_OF_MAC;

MAKEFOR:
	Goto_Col(Temp_Col);
	XStr = 'or  :=  to  do';
	CALL XTEXT;
	Cr;
	Goto_Col(Temp_Col);
	Indent;
	Up;
	Goto_Col(Temp_Col + 4);
	GOTO END_OF_MAC;

MAKEREPEAT:
	Goto_Col(Temp_Col);
	XStr = 'epeat';
	CALL XTEXT;
	Cr;
	Goto_Col(Temp_Col - 1);
	Indent;
	Cr;
	Goto_Col(Temp_Col - 1);
	XStr = 'until ();';
	CALL XTEXT;
	Goto_Col(Temp_Col + 6);
	GOTO END_OF_MAC;

MAKEPROCEDURE:
	Goto_Col(Temp_Col);
	XStr = 'rocedure  ;';
	CALL XTEXT;
	Cr;
	Goto_Col(Temp_Col - 1);
	Indent;
	XStr = 'begin';
	CALL XTEXT;
	Cr;
	Cr;
	XStr = 'end;';
	CALL XTEXT;
	Up;
	Goto_Col(Temp_Col);
	Indent;
	Up;
	Up;
	Goto_Col(Temp_Col + 9);
	GOTO END_OF_MAC;

MAKEFUNCTION:
	Goto_Col(Temp_Col);
	XStr = 'unction  ;';
	CALL XTEXT;
	Cr;
	Goto_Col(Temp_Col - 1);
	Indent;
	XStr = 'begin';
	CALL XTEXT;
	Cr;
	Cr;
	XStr = 'end;';
	CALL XTEXT;
	Up;
	Goto_Col(Temp_Col);
	Indent;
	Up;
	Up;
	Goto_Col(Temp_Col + 8);
	GOTO END_OF_MAC;

MAKECASE:
	Goto_Col(Temp_Col);
	XStr = 'ase () of';
	CALL XTEXT;
	Cr;
	Cr;
	Goto_Col(Temp_Col - 1);
	XStr = 'end;';
	CALL XTEXT;
	Up;
	Goto_Col(Temp_Col - 1);
	Indent;
	Up;
	Goto_Col(Temp_Col + 5);
	GOTO END_OF_MAC;

XTEXT:
	if(  UC  ) {
		XSTR = CAPS(XSTR);
	}
	TEXT( XSTR );
	RET;

END_OF_MAC:
	Insert_Mode = Temp_Insert;

}

/****************************************************************************
																MULTI-EDIT MACRO

Name: PASSETX

Description:  This macro is run every time a PASCAL type file is loaded.
							The template expansion global variable is defined here the
							first time this macro gets run.  The following is a brief
							description of the control codes contained in the template.

		'îC=' = Expansion case type.
								0 = case sensitive.    Keyword  - case sensitive
																			Expansion - verbatim.
								1 = case insensitive.  Keyword  - case insensitive
																			Expansion - All caps.
								2 = case insensitive.  Keyword  - case insensitive
																			Expansion - First letter caps.
								3 = case insensitive.  Keyword  - case insensitive
																			Expansion - dependent on keyword.
		'îM=' = Minimum number of characters in keyword required for an
							expansion to occurr.
	238 - 'î' = Parameter delimiter
	127 - '' = Field separator
	 20 - '' = Carriage return (Run CR macro)
	174 - '®' = Carriage return (Goto starting column)
	196 - 'Ä' = Record cursor position
	 17 - '' = Move cursor left
	 16 - '' = Move cursor right
	 24 - '' = Move cursor up
	 25 - '' = Move cursor down
	 64 - '@' = Translate next character literally
	168 - '¨' = Remember current column position
	173 - '­' = Goto remembered column number
	240 - 'ð' = Goto starting column
	241 - 'ñ' = Toggle Insert mode
	251 - 'û' = Run macro:  "/*ûC^CCOMMENT"
	252 - 'ü' = Expand template for preceding character (be carefull about
								infinite loops)
							 (C) Copyright 1991 by American Cybernetics, Inc.
****************************************************************************/
macro PASSETX {

	if ( !Global_Int("@DA_AB_MATCH")  ) {
    key_to_window( <)>, 'pas_close_paren' );
	}


	if ( "" == Global_Str("!PAS.Tmplt0") )
	Set_Global_Str("!PAS.Tmplt0", "îC=3îM=1"+
	"beginÄ®end;"+								/* îC=1 Case insensitive. Template will expand */
	"case (Ä) of®®end;"+						/* 			to all caps (Example:  PROCEDURE) */
	"for Ä := to do"+							/*    2 Case insensitive. Template will expand */
	"function Ä;¨begin­end;"+		/*      to upper/lower (Example:  Procedure) */
	"if (Ä) then¨begin­end"+		/*    3 Case first letter case sensitive  */
	"procedure Ä;¨begin­end;"+	/*      following characters case insensitive. */
	"program Ä;"+									/*      Template will expand to case of first */
	"pgroram Ä;"+								/*      letter. */
	"repeat®®until (Ä);"+					/* îM=1 Minimum expansion of 1 (only one character */
	"while (Ä) do¨begin­end"+		/*      is requried to expand the template */
	"{"+
	"{$"+
	"{$ifdef Ä}®{$endif}"+
	"{endif}"+
	"");
}

/*-----------------09-16-92 011:00am-----------------
 * Highlights to matching open paren when a closing
 * paren is entered.
 *--------------------------------------------------*/
macro pas_close_paren
{
	push_undo;
	text(')');
	left;
  if(c_col > 1)
  {
    left;
    if(cur_char == '*')
    {
      right;
      goto done;
    }
    right;
  }
  rm('PASMTCH /RC=1/HI=1/LS=20');

done:
	right;
	pop_undo;
}
